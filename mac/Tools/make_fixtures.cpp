// Writes the Tidy Up test circuits (the ones from the owner's Full-rearrange
// complaints) as .cdl files, built through CedarCore so their wires are real:
//   make_fixtures <cl_gatedefs.xml> <out dir>
// Makes tidy-adder.cdl (three identical BCD-adder slices, carry chain right to
// left) and tidy-inverters.cdl (a keypad, four inverters, labels A A' B B' ..).
#include "DocumentImpl.h"
#include "CircuitEdits.h"
#include "guiGate.h"
#include "guiWire.h"
#include "cmdSetParams.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Part { const char* gate; double x, y; int angle = 0; const char* junction = nullptr; };
struct Link { int a; const char* pa; int b; const char* pb; };

bool write(CLDocument* doc, const std::string& path) {
	const char* text = cl_document_save_text(doc);
	FILE* f = fopen(path.c_str(), "w");
	if (!f) return false;
	fputs(text, f);
	fclose(f);
	return true;
}

// Builds the parts and wires on page 0 of a new document and saves it.
bool build(const std::vector<Part>& parts, const std::vector<Link>& links, const std::string& path) {
	CLDocument* doc = cl_document_new();
	GUICanvas* page = doc->page(0);
	std::vector<CLBuildGate> gates;
	for (const Part& p : parts) gates.push_back({ p.gate, p.x, p.y, nullptr });
	const int made = cl_edit_build(doc, 0, gates.data(), (int)gates.size(), nullptr, 0, "Fixture");
	if (made != (int)parts.size()) { fprintf(stderr, "%s: built %d of %zu parts\n", path.c_str(), made, parts.size()); return false; }
	// Parts come out with rising ids in build order.
	std::vector<unsigned long> ids;
	for (auto& e : *page->getGateList()) if (e.second) ids.push_back(e.first);
	std::sort(ids.begin(), ids.end());
	for (size_t i = 0; i < parts.size(); i++) {
		ParameterMap gui, logic;
		if (parts[i].angle) gui["angle"] = std::to_string(parts[i].angle);
		if (parts[i].junction) logic["JUNCTION_ID"] = parts[i].junction;
		if (gui.empty() && logic.empty()) continue;
		cmdSetParams* c = new cmdSetParams(&doc->circuit, ids[i], paramSet(gui.empty() ? nullptr : &gui, logic.empty() ? nullptr : &logic));
		c->setCanvas(page);
		c->Do();
		delete c;
	}
	for (const Link& l : links) {
		klsCommand* c = edits::gateConnection(&doc->circuit, page, ids[l.a], l.pa, ids[l.b], l.pb);
		if (!c) { fprintf(stderr, "%s: no wire %d.%s -> %d.%s\n", path.c_str(), l.a, l.pa, l.b, l.pb); return false; }
		c->setCanvas(page);
		c->Do();
		delete c;
	}
	page->collisionUpdate();
	std::vector<unsigned long> wires;
	for (auto& w : *page->getWireList()) if (w.second) wires.push_back(w.first);
	std::sort(wires.begin(), wires.end());
	edits::rerouteWires(page, wires);
	page->unselectAllGates();
	page->unselectAllWires();
	const bool ok = write(doc, path);
	printf("%s: %zu parts, %zu wires\n", path.c_str(), parts.size(), wires.size());
	cl_document_close(doc);
	return ok;
}

// One BCD adder digit: two keypads into a 4-bit adder, a correction OR with
// two ANDs, ground, a second adder and a display. The OR's output is the
// carry to the next (more significant, left-hand) digit.
void adderSlice(double X, std::vector<Part>& parts, std::vector<Link>& links, std::vector<int>& orOut, std::vector<int>& carryIn) {
	const int base = (int)parts.size();
	parts.push_back({ "DD_KEYPAD_HEX", X + 0, 29 });          // 0 keypad A
	parts.push_back({ "DD_KEYPAD_HEX", X + 0, 17 });          // 1 keypad B
	parts.push_back({ "AE_FULLADDER_4BIT", X + 12, 6 });      // 2 adder 1
	parts.push_back({ "AA_AND2", X + 9, -4 });                // 3
	parts.push_back({ "AA_AND2", X + 9, -9 });                // 4
	parts.push_back({ "AE_OR3", X - 4, -6, 180 });            // 5 OR, output to the left
	parts.push_back({ "FF_GND", X + 5, -14 });                // 6
	parts.push_back({ "AE_FULLADDER_4BIT", X + 12, -17 });    // 7 adder 2
	parts.push_back({ "GE_LED_DISPLAY_4BIT", X + 18, -26 }); // 8 display
	const char* kp[4] = { "OUT_0", "OUT_1", "OUT_2", "OUT_3" };
	const char* xin[4] = { "IN_0", "IN_1", "IN_2", "IN_3" };
	const char* yin[4] = { "IN_B_0", "IN_B_1", "IN_B_2", "IN_B_3" };
	for (int i = 0; i < 4; i++) {
		links.push_back({ base + 0, kp[i], base + 2, xin[i] });
		links.push_back({ base + 1, kp[i], base + 2, yin[i] });
		links.push_back({ base + 2, kp[i], base + 7, xin[i] });
		links.push_back({ base + 7, kp[i], base + 8, xin[i] });
	}
	links.push_back({ base + 2, "OUT_3", base + 3, "IN_0" });
	links.push_back({ base + 2, "OUT_2", base + 3, "IN_1" });
	links.push_back({ base + 2, "OUT_3", base + 4, "IN_0" });
	links.push_back({ base + 2, "OUT_1", base + 4, "IN_1" });
	links.push_back({ base + 3, "OUT", base + 5, "IN_0" });
	links.push_back({ base + 4, "OUT", base + 5, "IN_1" });
	links.push_back({ base + 2, "carry_out", base + 5, "IN_2" });
	links.push_back({ base + 5, "OUT", base + 7, "IN_B_1" });
	links.push_back({ base + 5, "OUT", base + 7, "IN_B_2" });
	links.push_back({ base + 6, "OUT_0", base + 7, "IN_B_0" });
	links.push_back({ base + 6, "OUT_0", base + 7, "IN_B_3" });
	links.push_back({ base + 6, "OUT_0", base + 7, "carry_in" });
	orOut.push_back(base + 5);
	carryIn.push_back(base + 2);
}

}  // namespace

int main(int argc, char** argv) {
	if (argc < 3) { fprintf(stderr, "usage: make_fixtures lib.xml outdir\n"); return 2; }
	if (!cl_library_load(argv[1])) { fprintf(stderr, "library failed\n"); return 1; }
	const std::string dir = argv[2];
	bool ok = true;
	{
		std::vector<Part> parts;
		std::vector<Link> links;
		std::vector<int> orOut, carryIn;
		for (int s = 0; s < 3; s++) adderSlice(s * 42.0, parts, links, orOut, carryIn);
		// The carry runs right to left: a digit's OR feeds the carry-in of the digit on its left.
		for (int s = 1; s < 3; s++) links.push_back({ orOut[s], "OUT", carryIn[s - 1], "carry_in" });
		ok &= build(parts, links, dir + "/tidy-adder.cdl");
	}
	{
		std::vector<Part> parts;
		std::vector<Link> links;
		parts.push_back({ "DD_KEYPAD_HEX", 0, 0 });
		const char* names[8] = { "A", "A'", "B", "B'", "C", "C'", "D", "D'" };
		const char* kp[4] = { "OUT_3", "OUT_2", "OUT_1", "OUT_0" };
		for (int i = 0; i < 4; i++) {
			const double y = 3 - 5 * i;
			const int inv = (int)parts.size();
			parts.push_back({ "AA_INVERTER", 30, y - 2 });
			const int plain = (int)parts.size();
			parts.push_back({ "DE_TO", 42, y, 0, names[2 * i] });
			const int bar = (int)parts.size();
			parts.push_back({ "DE_TO", 42, y - 2, 0, names[2 * i + 1] });
			links.push_back({ 0, kp[i], plain, "IN_0" });
			links.push_back({ 0, kp[i], inv, "IN_0" });
			links.push_back({ inv, "OUT_0", bar, "IN_0" });
		}
		ok &= build(parts, links, dir + "/tidy-inverters.cdl");
	}
	return ok ? 0 : 1;
}
