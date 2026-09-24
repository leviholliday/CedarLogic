// LogicHost -- see LogicHost.h. The message handling mirrors
// threadLogic::parseMessage (src/gui/threadLogic.cpp) case for case; keep the
// two in step.

#include "LogicHost.h"
#include "GUICircuit.h"
#include "guiGate.h"
#include "guiWire.h"
#include "logic_circuit.h"
#include "logic_gate.h"
#include "logic_wire.h"

#include <unordered_map>
#include <vector>

namespace {
std::unordered_map<const GUICircuit*, LogicHost*>& hosts() {
	static std::unordered_map<const GUICircuit*, LogicHost*> map;
	return map;
}
}

LogicHost* logicHostFor(const GUICircuit* circuit) {
	auto it = hosts().find(circuit);
	return it == hosts().end() ? nullptr : it->second;
}

void registerLogicHost(const GUICircuit* circuit, LogicHost* host) {
	if (host == nullptr) hosts().erase(circuit);
	else hosts()[circuit] = host;
}

LogicHost::LogicHost(GUICircuit& circuit) : circuit(circuit), cir(new Circuit()) {}

LogicHost::~LogicHost() = default;

void LogicHost::deliver(const klsMessage::Message& input) {
	using namespace klsMessage;
	switch (input.mType) {
	case MT_REINITIALIZE:
		cir.reset(new Circuit());
		logicIDs.clear();
		break;
	case MT_CREATE_GATE: {
		const Message_CREATE_GATE& msg = input.as<Message_CREATE_GATE>();
		cir->newGate(msg.gateType, msg.gateId);
		break;
	}
	case MT_CREATE_WIRE: {
		const IDType id = input.as<Message_CREATE_WIRE>().wireId;
		logicIDs[id] = cir->newWire(id);
		break;
	}
	case MT_DELETE_GATE:
		cir->deleteGate(input.as<Message_DELETE_GATE>().gateId);
		break;
	case MT_DELETE_WIRE: {
		const IDType id = input.as<Message_DELETE_WIRE>().wireId;
		cir->deleteWire(logicIDs[id]);
		break;
	}
	case MT_SET_GATE_INPUT: {
		const Message_SET_GATE_INPUT& msg = input.as<Message_SET_GATE_INPUT>();
		if (msg.disconnect) {
			cir->disconnectGateInput(msg.gateId, msg.inputId);
		} else if (logicIDs.find(msg.wireId) == logicIDs.end()) {
			logicIDs[msg.wireId] = cir->connectGateInput(msg.gateId, msg.inputId, msg.wireId);
		} else {
			cir->connectGateInput(msg.gateId, msg.inputId, logicIDs[msg.wireId]);
		}
		break;
	}
	case MT_SET_GATE_INPUT_PARAM: {
		const Message_SET_GATE_INPUT_PARAM& msg = input.as<Message_SET_GATE_INPUT_PARAM>();
		cir->setGateInputParameter(msg.gateId, msg.inputId, msg.paramName, msg.paramValue);
		break;
	}
	case MT_SET_GATE_OUTPUT: {
		const Message_SET_GATE_OUTPUT& msg = input.as<Message_SET_GATE_OUTPUT>();
		if (msg.disconnect) {
			cir->disconnectGateOutput(msg.gateId, msg.outputId);
		} else if (logicIDs.find(msg.wireId) == logicIDs.end()) {
			logicIDs[msg.wireId] = cir->connectGateOutput(msg.gateId, msg.outputId, msg.wireId);
		} else {
			cir->connectGateOutput(msg.gateId, msg.outputId, logicIDs[msg.wireId]);
		}
		break;
	}
	case MT_SET_GATE_OUTPUT_PARAM: {
		const Message_SET_GATE_OUTPUT_PARAM& msg = input.as<Message_SET_GATE_OUTPUT_PARAM>();
		cir->setGateOutputParameter(msg.gateId, msg.outputId, msg.paramName, msg.paramValue);
		break;
	}
	case MT_SET_GATE_PARAM: {
		const Message_SET_GATE_PARAM& msg = input.as<Message_SET_GATE_PARAM>();
		cir->setGateParameter(msg.gateId, msg.paramName, msg.paramValue);
		break;
	}
	case MT_STEPSIM:
		step(input.as<Message_STEPSIM>().numSteps);
		break;
	case MT_UPDATE_GATES:
		// Let gates answer parameter changes without moving time on.
		cir->stepOnlyGates();
		applyResults(nullptr);
		break;
	default:
		break;
	}
}

int LogicHost::applyResults(const ID_SET<IDType>* changedWires) {
	int changed = 0;
	if (changedWires != nullptr) {
		for (IDType id : *changedWires) {
			if (guiWire* wire = circuit.getWire(id)) {
				wire->setSubState(id, (StateType)cir->getWireState(id));
				changed++;
			}
		}
	}
	// Parameters the gates changed (an LED lighting, a register's value, a
	// keypad's display), handed back the way GUICircuit::parseMessage does.
	std::vector<changedParam> params = cir->getParamUpdateList();
	cir->clearParamUpdateList();
	for (const changedParam& p : params) {
		const std::string value = cir->getGateParameter(p.gateID, p.paramName);
		if (value.empty()) continue;
		if (guiGate* gate = circuit.getGate(p.gateID)) gate->setLogicParam(p.paramName, value);
		if (p.paramName == "PAUSE_SIM") pauseRequested = true;
		changed++;
	}
	return changed;
}

int LogicHost::step(int steps) {
	int ran = 0;
	for (; ran < steps; ran++) {
		ID_SET<IDType> changedWires;
		cir->step(&changedWires);
		stepCount++;
		applyResults(&changedWires);
		if (afterStep) afterStep();
		if (pauseRequested) { ran++; break; }
	}
	return ran;
}

bool LogicHost::settle(int maxSteps) {
	int quiet = 0;
	for (int i = 0; i < maxSteps && quiet < 3; i++) {
		ID_SET<IDType> changedWires;
		cir->step(&changedWires);
		stepCount++;
		quiet = applyResults(&changedWires) == 0 ? quiet + 1 : 0;
	}
	pauseRequested = false;
	return quiet >= 3;
}
