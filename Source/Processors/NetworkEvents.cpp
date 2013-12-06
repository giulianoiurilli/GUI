/*
------------------------------------------------------------------

This file is part of the Open Ephys GUI
Copyright (C) 2013 Open Ephys

------------------------------------------------------------------

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <stdio.h>
#include "NetworkEvents.h"
#include "Editors/NetworkEventsEditor.h"
#include "../UI/UIComponent.h"

const int MAX_MESSAGE_LENGTH = 32000;


StringTS::StringTS()
{

	str = nullptr;
	len= 0;
	timestamp = 0;
}


std::vector<String> StringTS::splitString(char sep)
{
	String S((const char*)str,len);
	std::list<String> ls;
	String  curr;
	for (int k=0;k < S.length();k++) {
		if (S[k] != sep) {
			curr+=S[k];
		}
		else
		{
			ls.push_back(curr);
			while (S[k] == sep && k < S.length())
				k++;

			curr = "";
			if (S[k] != sep && k < S.length())
				curr+=S[k];
		}
	}
	if (S.length() > 0)
	{
		if (S[S.length()-1] != sep)
			ls.push_back(curr);
	}
	std::vector<String> Svec(ls.begin(), ls.end()); 
	return Svec;

}

StringTS::StringTS(MidiMessage &event) 
{
	const uint8* dataptr = event.getRawData();
	int bufferSize = event.getRawDataSize();
	len = bufferSize-4-8; // -4 for initial event prefix, -8 for timestamp at the end

	memcpy(&timestamp, dataptr + 4+len, 8); // remember to skip first four bytes
	str = new uint8[len];
	memcpy(str,dataptr+4,len);
}


String StringTS::getString()
{

	return String((const char*)str,len);
}
StringTS::StringTS(String S)
{
	Time t;
	str = new uint8[S.length()];
	memcpy(str,S.toRawUTF8(),S.length());
	timestamp = t.getHighResolutionTicks();

	len = S.length();
}

StringTS::StringTS(String S, int64 ts_software)
{
	str = new uint8[S.length()];
	memcpy(str,S.toRawUTF8(),S.length());
	timestamp = ts_software;

	len = S.length();
}

StringTS::StringTS(const StringTS &s)
{
	str = new uint8[s.len];
	memcpy(str,s.str,s.len);
	timestamp = s.timestamp;
	len = s.len;
}


StringTS::StringTS(unsigned char *buf, int _len, int64 ts_software) : len(_len),timestamp(ts_software) {
	str = new juce::uint8[len];
	for (int k=0;k<len;k++)
		str[k] = buf[k];
}

StringTS::~StringTS() {
	delete str;
}

/*********************************************/
NetworkEvents::NetworkEvents(void *zmq_context)
	:Thread("NetworkThread"), GenericProcessor("Network Events"), threshold(200.0), bufferZone(5.0f), state(false)

{
	zmqcontext = zmq_context;
	firstTime = true;
	responder = nullptr;
	urlport = 5556;
	threadRunning = false;
	opensocket();
	
	
	//parameters.add(Parameter("thresh", 0.0, 500.0, 200.0, 0));
}

void NetworkEvents::setNewListeningPort(int port)
{
	// first, close existing thread.
	disable();
	// allow some time for thread to quit
	sleep(300);
	urlport = port;
	opensocket();
}

NetworkEvents::~NetworkEvents()
{
	disable();
}

bool NetworkEvents::disable()
{
	if (threadRunning)
	{
		zmq_ctx_destroy(zmqcontext); // this will cause the thread to exit
		zmqcontext = getProcessorGraph()->createZmqContext();// and this will take care that processor graph doesn't attempt to delete the context again
	}
	return true;
}

AudioProcessorEditor* NetworkEvents::createEditor()
{
    editor = new NetworkEventsEditor(this, true);

    return editor;


}


void NetworkEvents::setParameter(int parameterIndex, float newValue)
{
/*
	editor->updateParameterButtons(parameterIndex);

	Parameter& p =  parameters.getReference(parameterIndex);
	p.setValue(newValue, 0);

	threshold = newValue;
	*/
	//std::cout << float(p[0]) << std::endl;

}

void NetworkEvents::initSimulation()
{
	Time t;

	int64 secondsToTicks = t.getHighResolutionTicksPerSecond();
	simulationStartTime=3*secondsToTicks + t.getHighResolutionTicks(); // start 10 seconds after
		
	simulation.push(StringTS("ClearDesign",simulationStartTime));
	simulation.push(StringTS("NewDesign Test",simulationStartTime+0.5*secondsToTicks));
	simulation.push(StringTS("AddCondition Name GoRight TrialTypes 1 2 3",simulationStartTime+0.6*secondsToTicks));
	simulation.push(StringTS("AddCondition Name GoLeft TrialTypes 4 5 6",simulationStartTime+0.6*secondsToTicks));


	
}

void NetworkEvents::simulateDesignAndTrials(juce::MidiBuffer& events)
{
	Time t;
	while (simulation.size() > 0) 
	{
		int64 currenttime = t.getHighResolutionTicks();
		StringTS S = simulation.front();
		if (currenttime > S.timestamp) {
			
			 // handle special messages
			 handleSpecialMessages(S);

			 postTimestamppedStringToMidiBuffer(S,events);
			 getUIComponent()->getLogWindow()->addLineToLog(S.getString());
			simulation.pop();
		} else
			break;
	}

}


void NetworkEvents::handleEvent(int eventType, juce::MidiMessage& event, int samplePosition)
{

}

void NetworkEvents::postTimestamppedStringToMidiBuffer(StringTS s, MidiBuffer& events)
{
	uint8* msg_with_ts = new uint8[s.len+8]; // for the two timestamps
	memcpy(msg_with_ts, s.str, s.len);	
	memcpy(msg_with_ts+s.len, &s.timestamp, 8);
	addEvent(events, 
			 (uint8) NETWORK,
			 0,
			 0,
			 (uint8) GENERIC_EVENT,
			 (uint8) s.len+8,
			 msg_with_ts);

	delete msg_with_ts;
}

void NetworkEvents::simulateStopRecord()
{
Time t;
	simulation.push(StringTS("StopRecord",t.getHighResolutionTicks()));

}

void NetworkEvents::simulateStartRecord()
{
Time t;
	simulation.push(StringTS("StartRecord",t.getHighResolutionTicks()));

}

void NetworkEvents::simulateSingleTrial()
{
	int numTrials = 1;
	float ITI = 0.7;
	float TrialLength = 0.4;
	Time t;

	if (firstTime) {
		firstTime = false;
		initSimulation();
	}

	int64 secondsToTicks = t.getHighResolutionTicksPerSecond();
	simulationStartTime=3*secondsToTicks + t.getHighResolutionTicks(); // start 10 seconds after

	// trial every 5 seconds
	for (int k=0;k<numTrials;k++) {
		simulation.push(StringTS("TrialStart",simulationStartTime+ITI*k*secondsToTicks));
		if (k%2 == 0) 
			simulation.push(StringTS("TrialType 2",simulationStartTime+(ITI*k+0.1)*secondsToTicks)); // 100 ms after trial start
		else
			simulation.push(StringTS("TrialType 4",simulationStartTime+(ITI*k+0.1)*secondsToTicks)); // 100 ms after trial start

		simulation.push(StringTS("TrialAlign",simulationStartTime+(ITI*k+0.1)*secondsToTicks)); // 100 ms after trial start
		simulation.push(StringTS("TrialOutcome 1",simulationStartTime+(ITI*k+0.3)*secondsToTicks)); // 300 ms after trial start
		simulation.push(StringTS("TrialEnd",simulationStartTime+(ITI*k+TrialLength)*secondsToTicks)); // 400 ms after trial start

	}
}


String NetworkEvents::handleSpecialMessages(StringTS msg)
{
	std::vector<String> input = msg.splitString(' ');
	if (input[0] == "StartRecord")
	{
		 getUIComponent()->getLogWindow()->addLineToLog("Remote triggered start recording");
		
		if (input.size() > 1)
		{
			getUIComponent()->getLogWindow()->addLineToLog("Remote setting session name to "+input[1]);
			// session name was also given.
			getProcessorGraph()->getRecordNode()->setDirectoryName(input[1]);
		}
        const MessageManagerLock mmLock;
	    getControlPanel()->setRecordState(true);
		return String("OK");      
	//	getControlPanel()->placeMessageInQueue("StartRecord");
	} if (input[0] == "SetSessionName")
	{
			getProcessorGraph()->getRecordNode()->setDirectoryName(input[1]);
	} else if (input[0] == "StopRecord")
	{
		const MessageManagerLock mmLock;
		//getControlPanel()->placeMessageInQueue("StopRecord");
		  getControlPanel()->setRecordState(false);
  		return String("OK");      
	} else if (input[0] == "ProcessorCommunication")
	{
		ProcessorGraph *g = getProcessorGraph();
		Array<GenericProcessor*> p = g->getListOfProcessors();
		for (int k=0;k<p.size();k++)
		{
			if (p[k]->getName().toLowerCase() == input[1].toLowerCase())
			{
				String Query="";
				for (int i=2;i<input.size();i++)
				{
					if (i == input.size()-1)
						Query+=input[i];
					else
						Query+=input[i]+" ";
				}
					
				return p[k]->interProcessorCommunication(Query);
			}
		}

  		return String("OK");      
	} 


	return String("NotHandled");
}

void NetworkEvents::process(AudioSampleBuffer& buffer,
							MidiBuffer& events,
							int& nSamples)
{
	checkForEvents(events);
	simulateDesignAndTrials(events);

	//std::cout << *buffer.getSampleData(0, 0) << std::endl;
	
	 while (!networkMessagesQueue.empty()) {
			 StringTS msg = networkMessagesQueue.front();
			 postTimestamppedStringToMidiBuffer(msg, events);
			 getUIComponent()->getLogWindow()->addLineToLog(msg);
		     networkMessagesQueue.pop();
	 }
	
}


void NetworkEvents::opensocket()
{
	startThread();
}

void NetworkEvents::run() {

 responder = zmq_socket (zmqcontext, ZMQ_REP);
  String url= String("tcp://*:")+String(urlport);
  int rc = zmq_bind (responder, url.toRawUTF8());
  
  if (rc != 0) {
	  // failed to open socket?
	  return;
  }

  threadRunning = true;
	 unsigned char *buffer = new unsigned char[MAX_MESSAGE_LENGTH];
	 int result=-1;
 
	while (threadRunning) {
		
         result = zmq_recv (responder, buffer, MAX_MESSAGE_LENGTH-1, 0); // blocking
		
         juce::int64 timestamp_software = timer.getHighResolutionTicks();
		
		 if (result < 0) // will only happen when responder dies.
			break;

		StringTS Msg(buffer, result, timestamp_software);
		if (result > 0) {
			networkMessagesQueue.push(Msg);
		    
			// handle special messages
			String response = handleSpecialMessages(Msg);
	
			zmq_send (responder, response.getCharPointer(), response.length(), 0);
		} else 
		{
			String zeroMessageError = "Recieved Zero Message?!?!?";
			zmq_send (responder, zeroMessageError.getCharPointer(), zeroMessageError.length(), 0);
		}
    }
	

	zmq_close(responder);
	delete buffer;
	threadRunning = false;
    return;
}






bool NetworkEvents::isReady()
{
   
        return true;
    
}


float NetworkEvents::getDefaultSampleRate()
{
    return 30000.0f;
}

int NetworkEvents::getDefaultNumOutputs()
{
    return 0;
}

float NetworkEvents::getDefaultBitVolts()
{
    return 0.05f;
}

void NetworkEvents::enabledState(bool t)
{

    isEnabled = t;

}

bool NetworkEvents::isSource()
{
	return true;
}

void NetworkEvents::saveCustomParametersToXml(XmlElement* parentElement)
{
    XmlElement* mainNode = parentElement->createNewChildElement("NETWORKEVENTS");
    mainNode->setAttribute("port", urlport);
}


void NetworkEvents::loadCustomParametersFromXml()
{

	if (parametersAsXml != nullptr)
	{

		int electrodeIndex = -1;

		forEachXmlChildElement(*parametersAsXml, mainNode)
		{
			if (mainNode->hasTagName("NETWORKEVENTS"))
			{
				setNewListeningPort(mainNode->getIntAttribute("port"));
			}
		}
	}
}

