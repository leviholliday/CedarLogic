/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   GUICircuit: Contains GUI circuit manipulation functions
*****************************************************************************/

#include "GUICircuit.h"
#include "SimBridge.h"
#include "GateLibrary.h"
#include "MainApp.h"
#include "GUICanvas.h"
#include "OscopeFrame.h"
#include "guiWire.h"

DECLARE_APP(MainApp)
IMPLEMENT_DYNAMIC_CLASS(GUICircuit, wxDocument)

void GUICircuit::reInitializeLogicCircuit() {
	// REINITIALIZE must bypass a step that was in flight in the old circuit.
	// MainFrame first waits for that core batch to finish and clears both bridge
	// queues; this local queue is the third place an old edit can be waiting.
	waitToSendMessage = false;
	sendMessageToCore(klsMessage::Message(klsMessage::MT_REINITIALIZE));
	messageQueue.clear();
	// Indexes first, then the owners. buslineToWire only borrows, and leaving it
	// populated past the wires it points at is exactly the SIGSEGV in issue #100:
	// syncWireStates() walked it on the next step after a file open and wrote
	// through freed pointers. The pending states go too, since they describe the
	// old circuit and its ids get reused from zero.
	buslineToWire.clear();
	{
		wxMutexLocker lock(simBridge().wireStateMutex);
		simBridge().wireStateBuffer.clear();
	}

	// Clearing the maps destroys everything in them.
	gateList.clear();
	gateListVersion++;
	wireList.clear();
	nextGateID = nextWireID = 0;
	waitToSendMessage = true;
	simulate = true;
	stepTimingExempt = false;
	catchingUp = false;
	lateSteps = 0;
	lastLogicTime = lastTime = lastTimeMod = lastNumSteps = 0;
}

void GUICircuit::gateTypesChanged() {
	myOscope->UpdateMenu();
}

void GUICircuit::Render() {
	return;
}

void GUICircuit::syncWireStates() {
	// These are updates, not durable state. Leaving them in the shared map let
	// a deleted wire ID apply its old value to a later wire that reused the ID.
	// Taking the batch also keeps the cross-thread mutex out of rendering code.
	std::unordered_map<IDType, StateType> updates;
	{
		wxMutexLocker lock(simBridge().wireStateMutex);
		updates.swap(simBridge().wireStateBuffer);
	}
	for (const auto& entry : updates) {
		auto wire = buslineToWire.find(entry.first);
		if (wire != buslineToWire.end() && wire->second != nullptr)
			wire->second->setSubState(entry.first, entry.second);
	}
}

void GUICircuit::parseMessage(klsMessage::Message message) {
	string temp, type;
	switch (message.mType) {
		case klsMessage::MT_SET_GATE_PARAM: {
			// SET GATE id PARAMETER name val
			const klsMessage::Message_SET_GATE_PARAM& msg = message.as<klsMessage::Message_SET_GATE_PARAM>();
			if (guiGate *gate = getGate(msg.gateId)) gate->setLogicParam(msg.paramName, msg.paramValue);
			if( msg.paramName == "PAUSE_SIM" ){
				pausing = true;
				panic = true;
			}
			break;
		}
		case klsMessage::MT_DONESTEP: { // DONESTEP
			simulate = true;
			int logicTime = message.as<klsMessage::Message_DONESTEP>().logicTime;
			// Did the core take longer than the wall time it was catching up on?
			// Keep a 3ms buffer. A step that was making up for a stall is exempt:
			// it was handed a pile of work on purpose and being slow is the point.
			lastLogicTime = logicTime;
			const bool late = (logicTime > lastTime + 3) &&
			                  !catchingUp && !stepTimingExempt;
			lateSteps = late ? lateSteps + 1 : 0;
			catchingUp = false;
			stepTimingExempt = false;
			// Only call it an overload once the core has been late repeatedly.
			// A single slow step is noise, and the old check fired on the first
			// one, which is why waking a sleeping laptop raised an alert.
			if (lateSteps >= kLateStepsBeforePanic) {
				panic = true;
				lateSteps = 0;
			}
			// Now we can send the waiting messages
			for (unsigned int i = 0; i < messageQueue.size(); i++) sendMessageToCore(messageQueue[i]);
			messageQueue.clear();
			// Sync wire states and always refresh
			syncWireStates();
			gCanvas->Refresh();
			break;
		}
		case klsMessage::MT_COMPLETE_INTERIM_STEP: {// COMPLETE INTERIM STEP - UPDATE OSCOPE
			syncWireStates();
			myOscope->UpdateData();
			break;
		}
		default:
			break;
	}
}

void GUICircuit::sendMessageToCore(klsMessage::Message message) {
	wxMutexLocker lock(simBridge().mexMessages);

	bool queuedForLogic = false;
	if (waitToSendMessage) {

		if (simulate) {
			simBridge().dGUItoLOGIC.push_back(message);
			queuedForLogic = true;
		} else{
			messageQueue.push_back(message);
		}
	} else{
		simBridge().dGUItoLOGIC.push_back(message);
		queuedForLogic = true;
	}
	// Wake the logic thread if we actually gave it work (it blocks on this
	// condition rather than polling). Signaled under mexMessages, held here.
	if (queuedForLogic) simBridge().msgForLogic.Signal();
}


void GUICircuit::printState() {
	wxGetApp().logfile << "print state" << endl << flush;
	for (const auto &entry : wireList) {
		wxGetApp().logfile << "wire " << entry.first << endl << flush;
	}
	for (const auto &entry : gateList) {
		wxGetApp().logfile << "gate " << entry.first << endl << flush;
	}
	
}
