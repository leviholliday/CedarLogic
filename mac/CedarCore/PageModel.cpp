// The native page model -- see include/gui/native/PageModel.h. Mirrors the
// matching GUICanvas methods in src/gui/GUICanvas.cpp.

#include "GUICanvas.h"
#include "guiGate.h"
#include "guiWire.h"

void GUICanvas::insertGate(unsigned long id, guiGate* gate, float x, float y) {
	if (gate == nullptr) return;
	gate->setGLcoords(x, y);
	gateList[id] = gate;
	collisionChecker.addObject(gate);
}

void GUICanvas::removeGate(unsigned long id) {
	auto it = gateList.find(id);
	if (it == gateList.end()) return;
	collisionChecker.removeObject(it->second);
	collisionChecker.update();
	gateList.erase(it);
}

void GUICanvas::insertWire(guiWire* wire) {
	if (wire == nullptr) return;
	wireList[wire->getID()] = wire;
	collisionChecker.addObject(wire);
}

void GUICanvas::removeWire(unsigned long wireId) {
	auto it = wireList.find(wireId);
	if (it == wireList.end()) return;
	guiWire* wire = it->second;
	collisionChecker.removeObject(wire);
	collisionChecker.update();
	for (IDType busLineId : wire->getIDs()) wireList.erase(busLineId);
}
