
#include "cmdTidy.h"
#include "../GUICircuit.h"
#include "../GUICanvas.h"
#include "../guiGate.h"
#include "../guiWire.h"

cmdTidy::cmdTidy(GUICircuit* gCircuit, GUICanvas* gCanvas, const std::vector<GateMove>& moves,
		const std::vector<WireReshape>& wires) :
			klsCommand(true, "Tidy Up") {
	this->gCircuit = gCircuit;
	this->gCanvas = gCanvas;
	this->moves = moves;
	this->wires = wires;
}

bool cmdTidy::Do() {
	if (applied) { applied = false; return true; }   // already on screen
	for (const GateMove& m : moves)
		if (guiGate* g = gCircuit->getGate(m.id)) g->setGLcoords(m.toX, m.toY, true);
	for (const WireReshape& w : wires)
		if (guiWire* wire = gCircuit->getWire(w.id)) wire->setSegmentMap(w.after);
	if (gCanvas) gCanvas->collisionUpdate();
	return true;
}

bool cmdTidy::Undo() {
	for (const GateMove& m : moves)
		if (guiGate* g = gCircuit->getGate(m.id)) g->setGLcoords(m.fromX, m.fromY, true);
	for (const WireReshape& w : wires)
		if (guiWire* wire = gCircuit->getWire(w.id)) wire->setSegmentMap(w.before);
	if (gCanvas) gCanvas->collisionUpdate();
	return true;
}
