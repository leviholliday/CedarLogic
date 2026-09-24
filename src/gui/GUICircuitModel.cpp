/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.

   GUICircuitModel: building the circuit -- making, connecting and removing
   gates and wires. Nothing here touches the window, the simulator thread or
   wxWidgets, so the native Mac front end compiles it too; the rest of
   GUICircuit (simulation messages, the oscope, repaints) is in GUICircuit.cpp.
*****************************************************************************/

#include "GUICircuit.h"
#include "GateLibrary.h"
#include "guiGate.h"
#include "guiWire.h"

GUICircuit::GUICircuit() {
	nextGateID = nextWireID = 0;
	myOscope = nullptr;
	gCanvas = nullptr;
	simulate = true;
	waitToSendMessage = true;
	panic = false;
	pausing = false;
	return;
}

GUICircuit::~GUICircuit() {

}

guiGate* GUICircuit::createGate(string gateName, long id, bool noOscope) {
	// Look the name up without inserting it. operator[] on these maps used to add
	// an entry for every unknown gate a file named, so after one bad file the app
	// treated that name as a real, empty gate type for the rest of the session.
	string libName;
	LibraryGate gateDef;
	auto owner = gateLibrary().gateNameToLibrary.find(gateName);
	if (owner != gateLibrary().gateNameToLibrary.end()) {
		libName = owner->second;
		auto lib = gateLibrary().libraries.find(libName);
		if (lib != gateLibrary().libraries.end()) {
			auto def = lib->second.find(gateName);
			if (def != lib->second.end()) gateDef = def->second;
		}
	}

	if (id == -1) id = getNextAvailableGateID();

	// Refuse an id that is already taken. Storing over the entry would destroy
	// the gate living there, and the canvas, the collision checker and every
	// wire connected to it all keep raw pointers that would be left dangling.
	// A file naming the same gate id twice is enough to reach this. Every
	// caller already handles a null return as "could not make that gate".
	if (gateList.find(id) != gateList.end()) return nullptr;

	guiGate* newGate = NULL;
	

	string ggt = gateDef.guiType;
	
	if (ggt == "REGISTER")
		newGate = (guiGate*)(new guiGateREGISTER());
	else if (ggt == "TO" || ggt == "FROM")
		newGate = (guiGate*)(new guiTO_FROM());
	else if (ggt == "LABEL")
		newGate = (guiGate*)(new guiLabel());
	else if (ggt == "LED")
		newGate = (guiGate*)(new guiGateLED());
	else if (ggt == "TOGGLE")
		newGate = (guiGate*)(new guiGateTOGGLE());
	else if (ggt == "KEYPAD")
		newGate = (guiGate*)(new guiGateKEYPAD());
	else if (ggt == "PULSE")
		newGate = (guiGate*)(new guiGatePULSE());
	else if (ggt == "RAM"){
		newGate = (guiGate*)(new guiGateRAM());
	}
	else
		newGate = new guiGate();

	newGate->setLibraryName( libName, gateName );

	for (unsigned int i = 0; i < gateDef.shape.size(); i++) {
		lgLine tempLine = gateDef.shape[i];
		newGate->insertLine(tempLine.x1, tempLine.y1, tempLine.x2, tempLine.y2, tempLine.labelGroup);
	}
	for (unsigned int i = 0; i < gateDef.arcs.size(); i++) {
		lgArc a = gateDef.arcs[i];
		newGate->insertArc(a.cx, a.cy, a.r, a.startDeg, a.sweepDeg, a.isLabel);
	}
	for (unsigned int i = 0; i < gateDef.circles.size(); i++) {
		lgCircle c = gateDef.circles[i];
		newGate->insertCircle(c.cx, c.cy, c.r, c.segs, c.isLabel);
	}
	for (unsigned int i = 0; i < gateDef.hotspots.size(); i++) {
		lgHotspot tempHS = gateDef.hotspots[i];
		newGate->insertHotspot(tempHS.x, tempHS.y, tempHS.name, tempHS.busLines);
		if (tempHS.isInput) newGate->declareInput(tempHS.name);
		else newGate->declareOutput(tempHS.name);
	}
	map < string, string >::iterator paramWalk = gateDef.guiParams.begin();
	while (paramWalk != gateDef.guiParams.end()) {
		newGate->setGUIParam(paramWalk->first, paramWalk->second);
		paramWalk++;
	}
	paramWalk = gateDef.logicParams.begin();
	while (paramWalk != gateDef.logicParams.end()) {
		newGate->setLogicParam(paramWalk->first, paramWalk->second);
		paramWalk++;
	}
	newGate->calcBBox();
	newGate->setID(id);
	gateList[id] = std::unique_ptr<guiGate>(newGate);
	gateListVersion++;
	
	// Update the OScope with the new info:
	if(ggt == "TO" && !noOscope) {
		oscopeSignalsChanged();
	}
	
	return newGate;
}

void GUICircuit::deleteGate(unsigned long gid, bool waitToUpdate) {
	
	//Declaration Of Variables
	bool updateMenu = false;
	
	guiGate *gate = getGate(gid);
	if (gate == nullptr) return;

	//Update Oscope
	if(!waitToUpdate && gate->getGUIType() == "TO") {
		updateMenu = true;
	}

	// Take this gate out of every wire that names it before it stops existing.
	// Commands normally disconnect first and this does nothing; it is here so
	// that "no wire outlives a gate it is connected to" is a property of the
	// circuit rather than a habit of its callers, which is what lets guiWire
	// dereference gateOf() without a null check at thirteen call sites.
	for (const auto &connection : gate->getConnections()) {
		if (connection.second != nullptr) connection.second->removeConnection(gid, connection.first);
	}

	gateList.erase(gid);
	gateListVersion++;

	//Call Update Oscope
	if(updateMenu)
	{
		oscopeSignalsChanged();
	}		
}

guiWire* GUICircuit::createWire(const std::vector<IDType> &wireIds) {
	if (guiWire *existing = getWire(wireIds[0])) return existing;

	auto wire = std::make_unique<guiWire>();
	wire->setCircuit(this); // so the wire can resolve connection gids to live gates
	wire->setIDs(wireIds);

	// buslineToWire claims every id the wire owns, which is what marks them as
	// used. wireList holds the wire once, under its head id: it used to also
	// hold a nullptr for each remaining bus line, so iterating it meant
	// remembering to skip holes and indexing it could hand back a null.
	guiWire *borrowed = wire.get();
	for (IDType id : wireIds) {
		buslineToWire[id] = borrowed;
	}
	wireList[wireIds[0]] = std::move(wire);
	return borrowed;
}

void GUICircuit::deleteWire(unsigned long wireId) {

	auto it = wireList.find(wireId);
	if (it == wireList.end()) return;

	// Drop the index entries first: they borrow the wire we are about to destroy.
	for (int busLineId : it->second->getIDs()) {
		buslineToWire.erase(busLineId);
	}

	wireList.erase(it);
}

std::unique_ptr<guiGate> GUICircuit::releaseGate(unsigned long gid) {
	auto it = gateList.find(gid);
	if (it == gateList.end()) return nullptr;

	std::unique_ptr<guiGate> gate = std::move(it->second);
	gateList.erase(it);
	gateListVersion++;
	return gate;
}

guiWire* GUICircuit::setWireConnection(const vector<IDType> &wireIds, long gid, string connection, bool openMode) {
	if (getGate(gid) == nullptr) return NULL; // error: gate not found
	guiWire *wire = createWire(wireIds); // do we need to init the wire first? if not then no effect.
	wire->addConnection(getGate(gid), connection, openMode);
	getGate(gid)->addConnection(connection, wire);
	return wire;
}

