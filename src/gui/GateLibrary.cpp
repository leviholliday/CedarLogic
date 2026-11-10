/*****************************************************************************
   Project: CEDAR Logic Simulator

   GateLibrary: process-wide gate-library accessor. See GateLibrary.h.
*****************************************************************************/

#include "GateLibrary.h"

LibraryGate GateLibrary::definition(const std::string& library, const std::string& gate) const {
	auto lib = libraries.find(library);
	if (lib == libraries.end()) return LibraryGate();
	auto def = lib->second.find(gate);
	return def == lib->second.end() ? LibraryGate() : def->second;
}

GateLibrary& gateLibrary() {
	static GateLibrary instance;
	return instance;
}
