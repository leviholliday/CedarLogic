// One page of a circuit in the native Mac front end (CL_NO_WX): which gates
// and wires are on it, and the collision checker hit-testing uses. Named
// GUICanvas because the shared editing commands (src/gui/command) add and
// remove things through a GUICanvas; this answers the same calls, with no
// window behind it.

#ifndef CL_PAGEMODEL_H
#define CL_PAGEMODEL_H

#include <string>
#include <unordered_map>
#include "klsCollisionChecker.h"
#include "CanvasTypes.h"
#include "GUICircuit.h"
#include "guiGate.h"
#include "guiWire.h"

class GUICircuit;
class guiGate;
class guiWire;

class GUICanvas {
public:
	explicit GUICanvas(GUICircuit* circuit) : gCircuit(circuit) {}

	std::unordered_map<unsigned long, guiGate*>* getGateList() { return &gateList; }
	std::unordered_map<unsigned long, guiWire*>* getWireList() { return &wireList; }

	void insertGate(unsigned long id, guiGate* gate, float x, float y);
	void removeGate(unsigned long id);
	void insertWire(guiWire* wire);
	void removeWire(unsigned long id);

	GUICircuit* getCircuit() { return gCircuit; }
	void collisionUpdate() { collisionChecker.update(); }

	// Asked of a page by the wx canvas's callers; a page model has no window.
	void Refresh() {}
	// The part of the page last on screen (legacy saves record it).
	void getViewport(GLPoint2f& topLeft, GLPoint2f& bottomRight) { topLeft = viewTopLeft; bottomRight = viewBottomRight; }
	GLPoint2f viewTopLeft = GLPoint2f(-20, 15), viewBottomRight = GLPoint2f(20, -15);

	std::string name;   // what the user called the page; empty for "Page N"
	klsCollisionChecker collisionChecker;

private:
	GUICircuit* gCircuit;
	std::unordered_map<unsigned long, guiGate*> gateList;
	std::unordered_map<unsigned long, guiWire*> wireList;
};

#endif  // CL_PAGEMODEL_H
