// LogicHost -- runs the logic engine for one circuit in the native Mac front
// end. The wx app runs it on a thread and passes messages both ways
// (threadLogic.cpp); here it runs on the main thread, one call at a time, so
// there is nothing to lock and no reply to wait for. It understands the same
// GUI -> core messages the wx thread does, handled the same way, and applies
// each step's results (wire states, gate parameters) straight to the model.

#ifndef CL_MAC_LOGICHOST_H
#define CL_MAC_LOGICHOST_H

#include "klsMessage.h"
#include "logic_defaults.h"
#include "logic_values.h"
#include <map>
#include <memory>

class Circuit;
class GUICircuit;

class LogicHost {
public:
	explicit LogicHost(GUICircuit& circuit);
	~LogicHost();

	// Handle one GUI -> core message now.
	void deliver(const klsMessage::Message& message);

	// Run up to `steps` steps; stops early when a gate asks to pause the
	// simulation. Returns how many ran.
	int step(int steps);

	// Step until the wires stop changing, so a circuit opens settled (what the
	// wx app's settleSimulation does). Capped for circuits that never settle.
	void settle(int maxSteps = 400);

	// A gate asked to pause (the PAUSE_SIM parameter); cleared when read.
	bool takePauseRequest() { bool p = pauseRequested; pauseRequested = false; return p; }

	unsigned long long stepsRun() const { return stepCount; }

private:
	// Push a step's results into the model; returns how many wires changed.
	int applyResults(const ID_SET<IDType>* changedWires);

	GUICircuit& circuit;
	std::unique_ptr<Circuit> cir;
	std::map<IDType, IDType> logicIDs;
	bool pauseRequested = false;
	unsigned long long stepCount = 0;
};

// Which host runs a circuit's logic, for GUICircuit::sendMessageToCore.
LogicHost* logicHostFor(const GUICircuit* circuit);
void registerLogicHost(const GUICircuit* circuit, LogicHost* host);   // null removes

#endif  // CL_MAC_LOGICHOST_H
