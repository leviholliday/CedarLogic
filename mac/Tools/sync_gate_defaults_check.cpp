// Checks that the core's gate library (res/cl_gatedefs.xml, read by the apps)
// gives the structure digest the same parameter defaults as the design's
// ref/gate_defaults.json (SyncVectors.h), which CedarLogic Online uses
// (SYNC.md 2.4): a disagreement would make that gate type look changed.
//
//   mac/build.sh && GATES=1 mac/Tools/sync-selftest.sh

#include "CedarCore.h"
#include "GateLibrary.h"
#include "SyncVectors.h"

#include <cstdio>
#include <map>
#include <set>
#include <string>

// Parameters the digest leaves out whatever their default (runtime state, click and display boxes).
static bool ignored(bool gui, const std::string& name) {
	if (!gui) return name == "OUTPUT_NUM" || name == "CURRENT_VALUE";
	return name == "angle" || name.find("_BOX") != std::string::npos;
}

int main(int argc, char** argv) {
	if (argc < 2 || !cl_library_load(argv[1])) {
		fprintf(stderr, "usage: sync_gate_defaults_check <cl_gatedefs.xml>\n");
		return 2;
	}
	std::map<std::string, std::string> table;   // lib \1 g|l \1 name -> value
	std::set<std::string> libs;
	for (const GateDefault& d : kGateDefaults) {
		table[std::string(d.lib) + "\x01" + (d.gui ? "g" : "l") + "\x01" + d.name] = d.value;
		libs.insert(d.lib);
	}
	int bad = 0, checked = 0;
	// Every default in the table, from the core.
	for (const GateDefault& d : kGateDefaults) {
		char out[4096];
		if (ignored(d.gui, d.name)) continue;
		checked++;
		if (!cl_sync_core_gate_default(nullptr, d.lib, d.gui, d.name, out, sizeof out)) {
			printf("MISSING in the core: %s %s %s = %s\n", d.lib, d.gui ? "gparam" : "lparam", d.name, d.value);
			bad++;
		} else if (std::string(out) != d.value) {
			printf("DIFFERENT: %s %s %s core '%s' table '%s'\n", d.lib, d.gui ? "gparam" : "lparam", d.name, out, d.value);
			bad++;
		}
	}
	// Every default of every core gate, in the table.
	for (const auto& cat : gateLibrary().libraries)
		for (const auto& gate : cat.second) {
			LibraryGate g;
			if (!gateLibrary().libParser.getGate(gate.first, g)) continue;
			for (int gui = 0; gui < 2; gui++)
				for (const auto& kv : gui ? g.guiParams : g.logicParams) {
					if (ignored(gui != 0, kv.first)) continue;
					checked++;
					const std::string key = gate.first + "\x01" + (gui ? "g" : "l") + "\x01" + kv.first;
					if (!table.count(key)) {
						printf("MISSING in the table: %s %s %s = %s\n", gate.first.c_str(), gui ? "gparam" : "lparam",
						       kv.first.c_str(), kv.second.c_str());
						bad++;
					}
				}
		}
	printf("%d checked, %d differ\n", checked, bad);
	return bad ? 1 : 0;
}
