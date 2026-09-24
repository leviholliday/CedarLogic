// Pieces of GUICircuit the wx app implements with its windows and simulator
// thread (GUICircuit.cpp), supplied here for the native front end.

#include "GUICircuit.h"

// No oscope window yet (stage 5), so nothing to refresh.
void GUICircuit::oscopeSignalsChanged() {}

#include "LogicHost.h"
#include "version.h"

// The wx app queues these for its logic thread; the native host handles each
// one as it's sent.
void GUICircuit::sendMessageToCore(klsMessage::Message message) {
	if (LogicHost* host = logicHostFor(this)) host->deliver(message);
}

void GUICircuit::reInitializeLogicCircuit() {
	sendMessageToCore(klsMessage::Message(klsMessage::MT_REINITIALIZE));
}

std::string VERSION_NUMBER_STRING() { return "4.1.0-native"; }
std::string VERSION_NUMBER() { return "4.1.0"; }
