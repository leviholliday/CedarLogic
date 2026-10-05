// The sync engine's gate defaults (SYNC.md 2.4) from the core's own gate
// library (cl_gatedefs.xml), for the apps' gate_default hook. Kept apart from
// the rest of the engine (which takes defaults through Config::gateDefault),
// since this one file needs the core's gate library.

#include "CedarSync.h"
#include "GateLibrary.h"

#include <cstring>
#include <string>

extern "C" bool cl_sync_core_gate_default(void* ctx, const char* lib, bool gui, const char* name, char* out, size_t outLen) {
	(void)ctx;
	if (!lib || !name || !out || outLen == 0) return false;
	LibraryGate g;
	if (!gateLibrary().libParser.getGate(lib, g)) return false;
	const auto& params = gui ? g.guiParams : g.logicParams;
	auto it = params.find(name);
	if (it == params.end() || it->second.size() + 1 > outLen) return false;
	memcpy(out, it->second.c_str(), it->second.size() + 1);
	return true;
}
