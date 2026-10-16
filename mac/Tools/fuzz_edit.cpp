// Random-edit fuzzer for the shared engine, through the C interface.
//   fuzz_edit <cl_gatedefs.xml> <first-seed> <seed-count> <ops-per-seed> [start.cdl...]
// Each seed starts from a new (or the given) document and applies random
// placements, drags, wire drawing, deletes, rotates, pastes, undo/redo, page
// changes, Straighten/Tidy, locks and simulation ticks. Build it with
// sanitizers (fuzz-check.sh) so memory errors and undefined behaviour stop it.
// After every seed the document is saved, read back and saved again: the two
// texts must match. A seed that fails prints its number, so it can be rerun.
// With several start files, seed s starts from file s % count.
//
// Environment (docs/V4-CORE-DIAGNOSIS.md lists them all): the checks
// CL_FUZZ_INVARIANT, _BOTH, _REACH, _DUP, _NOSIM, _DUMP, _WATCH, _TRACE, and
//   CL_FUZZ_WIDE=1      what a ten-minute session adds: settings, segment drags,
//                       C mid-move, floating parts and pastes, Straighten/Tidy
//                       with and without a selection, page links, page changes
//                       mid-gesture, cross-page paste, deletes while running,
//                       switch/keypad/clock clicks, undo runs, Disconnect,
//                       click-started wires, save-and-reopen, memory, truth tables
//   CL_FUZZ_SIM=1       odd seeds run the simulation throughout (16.7 ms ticks
//                       between pointer events and operations)
//   CL_FUZZ_ODD=1       out-of-range settings and two-line text too
//   CL_FUZZ_REOPEN=k    every k ops: the save reopens with no notices and saves the same
//   CL_FUZZ_UNDOALL=k   every k ops and at the end: undo all, redo all, same circuit
//   CL_FUZZ_UNDOSTEP=1  each op that adds one undo step: Undo gives back the
//                       circuit before it, Redo the one after
//   CL_FUZZ_CHAIN=1     each seed starts from the circuit the previous one saved
//   CL_FUZZ_SAYSEED=1   print each seed as it starts (to find the one that crashed)
//   CL_FUZZ_TREE=1      every wire is one connected piece
#include "DocumentImpl.h"
#include "guiGate.h"
#include "guiWire.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

static unsigned long long rs;
static unsigned rnd() { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 11); }
static int ri(int n) { return n <= 0 ? 0 : (int)(rnd() % (unsigned)n); }
static double rf(double a, double b) { return a + (b - a) * (rnd() % 100000) / 100000.0; }

static std::vector<std::string> libGates;

struct Pin { double x, y; bool input; bool ok; };

static guiGate* randomGate(CLDocument* doc, int page) {
	auto* gl = doc->page(page)->getGateList();
	if (!gl || gl->empty()) return nullptr;
	int k = ri((int)gl->size());
	for (auto& g : *gl) if (g.second && k-- == 0) return g.second;
	return nullptr;
}

static Pin randomPin(guiGate* g) {
	Pin p{0, 0, false, false};
	auto hl = g->getHotspotList();
	if (hl.empty()) return p;
	int k = ri((int)hl.size());
	for (auto& h : hl) if (k-- == 0) {
		float x, y; g->getHotspotCoords(h.first, x, y);
		p = {x, y, g->isConnectionInput(h.first), true};
		break;
	}
	return p;
}


// CL_FUZZ_WATCH=<wire>:<gate>:<pin> prints that wire's segments and that pin's position.
static void watchPrint(CLDocument* doc, const char* when) {
	const char* w = getenv("CL_FUZZ_WATCH");
	if (!w) return;
	unsigned long wid = 0, gid = 0; char pin[64] = "";
	if (sscanf(w, "%lu:%lu:%63s", &wid, &gid, pin) != 3) return;
	for (int p = 0; p < cl_document_page_count(doc); p++) {
		auto* wl = doc->page(p)->getWireList(); auto* gl = doc->page(p)->getGateList();
		auto wi = wl->find(wid); auto gi = gl->find(gid);
		if (wi == wl->end() || gi == gl->end() || !wi->second || !gi->second) continue;
		float px = 0, py = 0; gi->second->getHotspotCoords(pin, px, py);
		printf("      [%s] pin (%.2f, %.2f) selected=%d; wire:", when, px, py, (int)gi->second->isSelected());
		for (const auto& seg : wi->second->getSegmentMap())
			printf(" [%ld %c %.2f,%.2f->%.2f,%.2f]", seg.first, seg.second.isVertical() ? 'v' : 'h', seg.second.begin.x, seg.second.begin.y, seg.second.end.x, seg.second.end.y);
		printf("\n");
	}
}

// CL_FUZZ_SIM=1: in some seeds the simulation runs the whole time, as the app's
// display link does: a 16.7 ms tick between pointer events and between operations.
static bool simFrames = false;
static void frame(CLDocument* doc) { if (simFrames) cl_document_tick(doc, 16.7); }

static void gesture(CLDocument* doc, int page, double x0, double y0, double x1, double y1, int mods) {
	const double upp = 0.05;
	cl_edit_press(doc, page, x0, y0, mods, upp);
	if (getenv("CL_FUZZ_TRACE")) { static const char* names[] = { "None", "Pressed", "Moving", "BoxSelect", "Connect", "WireSeg" }; printf("    press mode %s at (%.2f, %.2f), drag to (%.2f, %.2f)\n", names[(int)doc->gesture.mode], x0, y0, x1, y1); }
	int steps = 1 + ri(5);
	watchPrint(doc, "pressed");
	frame(doc);
	for (int i = 1; i <= steps; i++) { cl_edit_drag(doc, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps); watchPrint(doc, "drag"); frame(doc); }
	if (ri(12) == 0) cl_edit_cancel(doc); else cl_edit_release(doc, x1, y1);
	watchPrint(doc, "released");
}

static guiWire* randomWire(CLDocument* doc, int page) {
	auto* wl = doc->page(page)->getWireList();
	if (!wl || wl->empty()) return nullptr;
	int k = ri((int)wl->size());
	for (auto& w : *wl) if (w.second && k-- == 0) return w.second;
	return nullptr;
}

static void centre(guiGate* g, double& x, double& y) {
	klsBBox b = g->getSelectionBBox();
	if (b.empty()) b = g->getBBox();
	x = (b.getLeft() + b.getRight()) / 2; y = (b.getBottom() + b.getTop()) / 2;
}

// A random wired pin of a random gate on the page.
static bool randomWiredPin(CLDocument* doc, int page, double& x, double& y) {
	guiGate* g = randomGate(doc, page);
	if (!g) return false;
	std::vector<std::string> wired;
	for (auto& h : g->getHotspotList()) if (g->isConnected(h.first)) wired.push_back(h.first);
	if (wired.empty()) return false;
	float fx, fy; g->getHotspotCoords(wired[(size_t)ri((int)wired.size())], fx, fy);
	x = fx; y = fy;
	return true;
}

// A gate of one of these library types on the page (switches, keypads, clocks...).
static guiGate* gateOfType(CLDocument* doc, int page, const std::vector<std::string>& types) {
	std::vector<guiGate*> found;
	for (auto& g : *doc->page(page)->getGateList())
		if (g.second && std::find(types.begin(), types.end(), g.second->getLibraryGateName()) != types.end()) found.push_back(g.second);
	return found.empty() ? nullptr : found[(size_t)ri((int)found.size())];
}

// Text a student might type into a label or a TO/FROM name.
static std::string oddText() {
	static const char* t[] = { "", "A", "Q0", "CLK", "a b", "x\"y", "(paren)", ")(", "back\\slash", "\xC3\xBCn\xC3\xAF \xE2\x9C\x93",
		"tab\there", "  lead", "trail  ", "#1", "<b>&amp;", "\"", "'", ";", "0", "-1", "1e9" };
	const int n = (int)(sizeof t / sizeof t[0]);
	int k = ri(n + 2);
	if (k < n) return t[k];
	if (k == n) return std::string(120 + ri(200), 'W');
	return getenv("CL_FUZZ_ODD") ? "two\nlines" : "Sum";
}

// A value for a gate setting, as the settings sheet would send it: in range
// for numbers (the sheet refuses others), anything for text.
// CL_FUZZ_ODD=1 sends out-of-range and non-numbers too.
static bool settingValue(const CLGateSetting& s, std::string& v) {
	const std::string type = s.type;
	char buf[64];
	const bool odd = getenv("CL_FUZZ_ODD") && ri(4) == 0;
	if (type == "BOOL") { v = ri(2) ? "true" : "false"; if (odd) v = ri(2) ? "" : "maybe"; return true; }
	if (type == "INT" || type == "FLOAT") {
		double lo = s.min, hi = s.max;
		if (!(hi < 1e30)) { lo = type == "INT" ? 0 : 0.001; hi = type == "INT" ? 64 : 1000; }
		double x;
		switch (ri(4)) { case 0: x = lo; break; case 1: x = hi; break; default: x = rf(lo, hi); }
		if (odd) { static const char* o[] = { "", "abc", "-5", "99999999999", "1e400", "nan", "0x10" }; v = o[ri(7)]; return true; }
		if (type == "INT") snprintf(buf, sizeof buf, "%d", (int)std::lround(x)); else snprintf(buf, sizeof buf, "%g", x);
		v = buf;
		return true;
	}
	if (type == "STRING") { v = oddText(); return true; }
	return false;   // FILE_IN / FILE_OUT: a file dialog, not fuzzed
}

// Wide sessions (CL_FUZZ_WIDE=1): what a student does in ten minutes beyond the
// basic edits. Each returns quietly when it doesn't apply. `doc` may be
// replaced (a save and reopen).
static void wideOp(CLDocument*& doc, int op, int page, int pages) {
	const double upp = ri(3) == 0 ? rf(0.02, 0.3) : 0.05;
	switch (op) {
	case 0: {   // change a gate setting
		guiGate* g = randomGate(doc, page);
		if (!g) break;
		const long id = (long)g->getID();
		const int n = cl_gate_setting_count(doc, id);
		if (n <= 0) break;
		CLGateSetting s;
		if (!cl_gate_setting(doc, id, ri(n), &s)) break;
		const std::string name = s.name;
		std::string v;
		if (!settingValue(s, v)) break;
		if (getenv("CL_FUZZ_TRACE")) printf("    set gate %ld %s = \"%s\"\n", id, name.c_str(), v.c_str());
		cl_gate_set_setting(doc, id, name.c_str(), v.c_str());
		break; }
	case 1: {   // drag a wire's segment from somewhere along it
		guiWire* w = randomWire(doc, page);
		if (!w) break;
		auto sm = w->getSegmentMap();
		if (sm.empty()) break;
		auto it = sm.begin(); std::advance(it, ri((int)sm.size()));
		const wireSegment& s = it->second;
		const double t = ri(2) ? 0.5 : rf(0, 1);
		const double mx = s.begin.x + (s.end.x - s.begin.x) * t, my = s.begin.y + (s.end.y - s.begin.y) * t;
		if (ri(2)) cl_edit_select_none(doc, page);
		double dx = rf(-8, 8), dy = rf(-8, 8);
		if (ri(3)) { if (s.isVertical()) dy = rf(-0.3, 0.3); else dx = rf(-0.3, 0.3); }   // mostly across the segment
		gesture(doc, page, mx, my, mx + dx, my + dy, 0);
		break; }
	case 2: {   // drag a part, press C on the way (connect while moving), maybe Escape
		guiGate* g = randomGate(doc, page);
		if (!g) break;
		double x, y; centre(g, x, y);
		cl_edit_press(doc, page, x, y, 0, upp);
		frame(doc);
		double tx = x, ty = y;
		for (int i = 0, n = 1 + ri(4); i < n; i++) { tx += rf(-3, 3); ty += rf(-3, 3); cl_edit_drag(doc, tx, ty); frame(doc); }
		cl_edit_connect_while_moving(doc, page, upp);
		for (int i = 0, n = ri(3); i < n; i++) { tx += rf(-2, 2); ty += rf(-2, 2); cl_edit_drag(doc, tx, ty); frame(doc); }
		if (ri(3) == 0) cl_edit_connect_while_moving(doc, page, upp);
		if (ri(4) == 0) cl_edit_take_back_connects(doc);
		if (ri(5) == 0) cl_edit_cancel(doc); else cl_edit_release(doc, tx, ty);
		break; }
	case 3: {   // connect nearby with what's selected
		if (ri(2)) { guiGate* g = randomGate(doc, page); if (g) { cl_edit_select_none(doc, page); g->select(); } }
		cl_edit_connect_nearby(doc, page, upp * 4);
		break; }
	case 4: case 5: {   // a new part, paste or duplicate on the pointer, moved, then dropped or Escaped
		bool made = false;
		const int how = ri(3);
		double ax = rf(-30, 30), ay = rf(-30, 30);
		if (how == 0) made = cl_edit_add_gate(doc, page, libGates[ri((int)libGates.size())].c_str(), ax, ay);
		else {
			const int from = how == 1 ? ri(pages) : page;
			if (how == 2) cl_edit_select_all(doc, from);
			std::string t = cl_edit_copy(doc, from);
			const char* back = nullptr;
			if (!t.empty()) made = cl_edit_paste(doc, page, t.c_str(), ax, ay, true, &back);
		}
		if (!made || !cl_edit_float_begin(doc, page, ax, ay)) break;
		for (int i = 0, n = 1 + ri(6); i < n; i++) { ax += rf(-4, 4); ay += rf(-4, 4); cl_edit_hover(doc, page, ax, ay, upp); frame(doc); }
		if (ri(3) == 0) cl_edit_connect_while_moving(doc, page, upp);
		const int end = ri(10);
		if (end < 6) { cl_edit_press(doc, page, ax, ay, 0, upp); cl_edit_release(doc, ax, ay); }
		else if (end < 8) {   // Escape: first the connections, then the whole thing
			if (cl_edit_take_back_connects(doc) == 0) { cl_edit_cancel(doc); if (cl_edit_can_undo(doc)) cl_edit_undo(doc); }
		}
		// else: left on the pointer; whatever comes next happens with it floating
		break; }
	case 6: {   // Straighten or Tidy Up, with or without a selection
		const int sel = ri(3);
		if (sel == 0) cl_edit_select_none(doc, page);
		else if (sel == 1) { guiGate* g = randomGate(doc, page); cl_edit_select_none(doc, page); if (g) g->select(); if (guiWire* w = ri(2) ? randomWire(doc, page) : nullptr) w->select(); }
		if (ri(2)) { cl_edit_straighten(doc, page); break; }
		if (!cl_edit_tidy_begin(doc, page, ri(2))) break;
		frame(doc);
		switch (ri(5)) {
		case 0: cl_edit_tidy_end(doc, true); break;
		case 1: cl_edit_tidy_end(doc, false); break;
		case 2: cl_edit_add_gate(doc, page, libGates[ri((int)libGates.size())].c_str(), rf(-30, 30), rf(-30, 30)); break;   // any edit keeps it
		case 3: if (cl_edit_can_undo(doc)) cl_edit_undo(doc); break;
		default: break;   // left showing
		}
		break; }
	case 7: {   // page links
		switch (ri(4)) {
		case 0: cl_pages_connect_all(doc); break;
		case 1: cl_pages_disconnect_all(doc); break;
		default: {
			std::vector<char> w((size_t)pages);
			for (auto& b : w) b = (char)ri(2);
			bool with[8] = {};
			for (int i = 0; i < pages && i < 8; i++) with[i] = w[(size_t)i];
			cl_page_connect_to(doc, page, with, std::min(pages, 8));
		} }
		// a TO here and a FROM on another page with the same name
		if (ri(2)) {
			static const char* names[] = { "A", "B", "CLK", "x\"y" };
			const char* nm = names[ri(4)];
			const int other = ri(pages);
			if (cl_edit_add_gate(doc, page, "DE_TO", rf(-30, 30), rf(-30, 30))) cl_gate_set_setting(doc, cl_edit_single_gate(doc, page), "JUNCTION_ID", nm);
			if (cl_edit_add_gate(doc, other, "DA_FROM", rf(-30, 30), rf(-30, 30))) cl_gate_set_setting(doc, cl_edit_single_gate(doc, other), "JUNCTION_ID", nm);
		}
		break; }
	case 8: {   // pages change in the middle of a drag or a floating paste
		guiGate* g = randomGate(doc, page);
		if (!g) break;
		double x, y; centre(g, x, y);
		if (ri(2)) { cl_edit_press(doc, page, x, y, 0, upp); cl_edit_drag(doc, x + rf(-4, 4), y + rf(-4, 4)); }
		else { cl_edit_select_none(doc, page); g->select(); cl_edit_float_begin(doc, page, x, y); cl_edit_hover(doc, page, x + 2, y + 1, upp); }
		frame(doc);
		switch (ri(4)) {
		case 0: if (pages < 6) cl_document_add_page(doc); break;
		case 1: cl_document_close_page(doc, ri(pages)); break;
		case 2: cl_document_move_page(doc, ri(pages), ri(pages)); break;
		default: if (ri(3) == 0) cl_document_delete_page(doc, ri(pages)); break;
		}
		const int np = cl_document_page_count(doc);
		cl_edit_drag(doc, x + rf(-4, 4), y + rf(-4, 4));
		frame(doc);
		const int at = ri(np);
		cl_edit_press(doc, at, rf(-30, 30), rf(-30, 30), 0, upp);
		cl_edit_release(doc, rf(-30, 30), rf(-30, 30));
		break; }
	case 9: {   // copy on one page, paste on another
		const int from = ri(pages), to = ri(pages);
		if (ri(2)) cl_edit_select_all(doc, from);
		std::string t = cl_edit_copy(doc, from);
		if (t.empty()) break;
		const char* back = nullptr;
		cl_edit_paste(doc, to, t.c_str(), rf(-30, 30), rf(-30, 30), ri(2), &back);
		if (back && *back && ri(2)) { std::string again = back; cl_edit_paste(doc, to, again.c_str(), rf(-30, 30), rf(-30, 30), false, &back); }
		break; }
	case 10: {   // delete while the simulation runs
		cl_document_set_running(doc, true);
		for (int i = 0, n = 1 + ri(4); i < n; i++) cl_document_tick(doc, 16.7);
		if (ri(2)) { guiGate* g = randomGate(doc, page); cl_edit_select_none(doc, page); if (g) g->select(); }
		cl_edit_delete(doc, page);
		for (int i = 0, n = 1 + ri(4); i < n; i++) cl_document_tick(doc, 16.7);
		if (ri(2) && cl_edit_can_undo(doc)) { cl_edit_undo(doc); cl_document_tick(doc, 16.7); }
		break; }
	case 11: {   // click switches, keypads, clocks, pulses
		static const std::vector<std::string> inputs = { "AA_TOGGLE", "DD_KEYPAD_HEX", "BB_CLOCK", "CC_PULSE" };
		guiGate* g = gateOfType(doc, page, inputs);
		if (!g) { if (ri(4) == 0) cl_edit_add_gate(doc, page, inputs[(size_t)ri(4)].c_str(), rf(-30, 30), rf(-30, 30)); break; }
		klsBBox b = g->getBBox();
		const double x = rf(b.getLeft(), b.getRight()), y = rf(b.getBottom(), b.getTop());
		if (ri(2)) cl_document_click(doc, page, x, y);
		else { cl_edit_press(doc, page, x, y, 0, upp); cl_edit_release_ex(doc, x, y, ri(4) != 0); }
		for (int i = 0, n = ri(4); i < n; i++) cl_document_tick(doc, 16.7);
		break; }
	case 12: {   // many undos in a row, then (the next ops) edits
		for (int i = 0, n = 5 + ri(30); i < n && cl_edit_can_undo(doc); i++) cl_edit_undo(doc);
		if (ri(3) == 0) for (int i = 0, n = ri(10); i < n && cl_edit_can_redo(doc); i++) cl_edit_redo(doc);
		break; }
	case 13: {   // right-click a wired pin: Disconnect
		double x, y;
		if (!randomWiredPin(doc, page, x, y)) break;
		if (cl_edit_context(doc, page, x, y, upp) == CL_CONTEXT_PIN) cl_edit_disconnect_pin(doc, page, x, y, upp);
		break; }
	case 14: {   // a click on a pin starts a wire that follows the pointer; the next click ends it
		guiGate* a = randomGate(doc, page);
		if (!a) break;
		Pin pa = randomPin(a);
		if (!pa.ok) break;
		cl_edit_press(doc, page, pa.x, pa.y, 0, upp);
		cl_edit_release(doc, pa.x, pa.y);
		for (int i = 0, n = ri(4); i < n; i++) { cl_edit_hover(doc, page, rf(-30, 30), rf(-30, 30), upp); frame(doc); }
		double tx = rf(-30, 30), ty = rf(-30, 30);
		const int to = ri(3);
		if (to == 0) { if (guiGate* b = randomGate(doc, page)) { Pin pb = randomPin(b); if (pb.ok) { tx = pb.x; ty = pb.y; } } }
		else if (to == 1) { if (guiWire* w = randomWire(doc, page)) { auto sm = w->getSegmentMap(); if (!sm.empty()) { const wireSegment& s = sm.begin()->second; tx = (s.begin.x + s.end.x) / 2; ty = (s.begin.y + s.end.y) / 2; } } }
		cl_edit_press(doc, page, tx, ty, 0, upp);
		cl_edit_release(doc, tx, ty);
		break; }
	case 15: {   // save and reopen, then carry on with the reopened circuit
		std::string text = cl_document_save_text(doc);
		char err[256];
		CLDocument* back = cl_document_open_text(text.c_str(), (long)text.size(), err, sizeof err);
		if (!back) { printf("    reopen failed: %s\n", err); break; }
		cl_document_close(doc);
		doc = back;
		break; }
	case 16: {   // hover around
		for (int i = 0, n = 1 + ri(5); i < n; i++) {
			const double x = rf(-30, 30), y = rf(-30, 30);
			cl_edit_hover(doc, page, x, y, upp);
			char st[80]; cl_edit_hover_wire_state(doc, page, st, sizeof st);
		}
		if (ri(2)) cl_edit_hover_clear(doc);
		break; }
	case 17: {   // rename or move a page
		if (ri(2)) cl_document_rename_page(doc, page, oddText().c_str());
		else cl_document_move_page(doc, ri(pages), ri(pages));
		break; }
	case 18: {   // memory contents
		static const std::vector<std::string> mems = { "AA_RAM_4x4", "AE_RAM_8x8", "AI_RAM_12x16", "AM_RAM_16x16", "BA_ROM_4x4", "BE_ROM_8x8", "BI_ROM_12x16", "BM_ROM_16x16" };
		guiGate* g = gateOfType(doc, page, mems);
		if (!g) { if (ri(3) == 0) cl_edit_add_gate(doc, page, mems[(size_t)ri((int)mems.size())].c_str(), rf(-30, 30), rf(-30, 30)); break; }
		int ab = 0, db = 0;
		if (!cl_ram_info(doc, (long)g->getID(), &ab, &db)) break;
		for (int i = 0, n = 1 + ri(6); i < n; i++) {
			const unsigned long addr = (unsigned long)ri(1 << std::min(ab, 16)), val = (unsigned long)rnd() & ((db >= 32) ? 0xffffffffUL : ((1UL << db) - 1));
			cl_ram_set(doc, (long)g->getID(), addr, val);
			cl_ram_value(doc, (long)g->getID(), addr);
		}
		cl_ram_last_read(doc, (long)g->getID()); cl_ram_last_written(doc, (long)g->getID());
		break; }
	case 19: {   // Step Clock, single steps, step length
		if (ri(2)) cl_document_clock_step(doc, page);
		if (ri(2)) cl_document_step(doc);
		if (ri(4) == 0) cl_document_set_step_ms(doc, 1 + ri(500));
		break; }
	case 20: {   // a truth table
		char err[256];
		if (CLTruthTable* tt = cl_truth_table(doc, page, err, sizeof err)) cl_tt_free(tt);
		break; }
	case 21: {   // a rubber-band selection, then something done to it
		const double x = rf(-30, 30), y = rf(-30, 30);
		gesture(doc, page, x, y, x + rf(-20, 20), y + rf(-20, 20), ri(4) == 0 ? CL_MOD_SHIFT : 0);
		switch (ri(5)) {
		case 0: cl_edit_delete(doc, page); break;
		case 1: cl_edit_rotate(doc, page); break;
		case 2: cl_edit_nudge(doc, page, ri(3) - 1, ri(3) - 1); break;
		case 3: cl_edit_straighten(doc, page); break;
		default: { std::string t = cl_edit_copy(doc, page); if (!t.empty()) { cl_edit_delete(doc, page); const char* back = nullptr; cl_edit_paste(doc, page, t.c_str(), x, y, false, &back); } }   // cut and paste
		}
		break; }
	}
}


// Every connection on every wire must name a gate that is still on the page.
static bool danglingConnection(CLDocument* doc, std::string& what) {
	// CL_FUZZ_DUP=1: each gate and wire is on one page, and the circuit knows it
	// by that id (a save writes each page's lists).
	if (getenv("CL_FUZZ_DUP")) {
		std::map<unsigned long, int> gateOn, wireOn;
		for (int p = 0; p < cl_document_page_count(doc); p++) {
			for (auto& g : *doc->page(p)->getGateList()) {
				if (!g.second) continue;
				if (gateOn.count(g.first)) { what = "gate " + std::to_string(g.first) + " is on page " + std::to_string(gateOn[g.first]) + " and page " + std::to_string(p); return true; }
				gateOn[g.first] = p;
				if (doc->circuit.getGate(g.first) != g.second) { what = "gate " + std::to_string(g.first) + " on page " + std::to_string(p) + " is not the circuit's gate of that id"; return true; }
			}
			for (auto& w : *doc->page(p)->getWireList()) {
				if (!w.second) continue;
				if (wireOn.count(w.first)) { what = "wire " + std::to_string(w.first) + " is on page " + std::to_string(wireOn[w.first]) + " and page " + std::to_string(p); return true; }
				wireOn[w.first] = p;
				if (doc->circuit.getWire(w.first) != w.second) { what = "wire " + std::to_string(w.first) + " on page " + std::to_string(p) + " is not the circuit's wire of that id"; return true; }
			}
		}
	}
	for (int p = 0; p < cl_document_page_count(doc); p++) {
		auto* gl = doc->page(p)->getGateList();
		auto* wl = doc->page(p)->getWireList();
		if (!gl || !wl) continue;
		for (auto& w : *wl) {
			if (!w.second) continue;
			// CL_FUZZ_TREE=1: a wire's segments are one connected piece (touching
			// within 0.05), as it is drawn: a wire in two pieces looks like two.
			if (getenv("CL_FUZZ_TREE") && !cl_edit_is_floating(doc)) {
				std::vector<wireSegment> segs;
				for (const auto& s : w.second->getSegmentMap()) segs.push_back(s.second);
				std::vector<int> up(segs.size());
				for (size_t i = 0; i < up.size(); i++) up[i] = (int)i;
				std::function<int(int)> find = [&](int i) { return up[(size_t)i] == i ? i : up[(size_t)i] = find(up[(size_t)i]); };
				const float e = 0.05f;
				for (size_t i = 0; i < segs.size(); i++)
					for (size_t j = i + 1; j < segs.size(); j++) {
						const wireSegment &a = segs[i], &b = segs[j];
						const bool touch = std::min(a.end.x, b.end.x) + e >= std::max(a.begin.x, b.begin.x) &&
						                   std::min(a.end.y, b.end.y) + e >= std::max(a.begin.y, b.begin.y);
						if (touch) up[(size_t)find((int)i)] = find((int)j);
					}
				int pieces = 0;
				for (size_t i = 0; i < up.size(); i++) if (find((int)i) == (int)i) pieces++;
				if (pieces > 1) {
					what = "wire " + std::to_string(w.first) + " on page " + std::to_string(p) + " is in " + std::to_string(pieces) + " pieces";
					for (const auto& s : w.second->getSegmentMap())
						what += " [seg " + std::to_string(s.first) + (s.second.isVertical() ? " v " : " h ") + std::to_string(s.second.begin.x) + "," + std::to_string(s.second.begin.y) + " -> " + std::to_string(s.second.end.x) + "," + std::to_string(s.second.end.y) + "]";
					return true;
				}
			}
			// CL_FUZZ_DUP=1: no pin may be recorded twice on a wire's segments (a save
			// writes each record, and a reload keeps one).
			if (getenv("CL_FUZZ_DUP") && !cl_edit_is_floating(doc)) {
				std::vector<std::pair<unsigned long, std::string>> seen;
				for (const auto& seg : w.second->getSegmentMap())
					for (const auto& sc : seg.second.connections) {
						std::pair<unsigned long, std::string> k(sc.gid, sc.connection);
						if (std::find(seen.begin(), seen.end(), k) != seen.end()) {
							what = "wire " + std::to_string(w.first) + " records gate " + std::to_string(sc.gid) + " pin " + sc.connection + " twice";
							return true;
						}
						seen.push_back(k);
					}
				for (const auto& seg : w.second->getSegmentMap())
					for (const auto& x : seg.second.intersects)
						for (size_t i = 0; i < x.second.size(); i++)
							for (size_t j = i + 1; j < x.second.size(); j++)
								if (x.second[i] == x.second[j]) {
									what = "wire " + std::to_string(w.first) + " segment " + std::to_string(seg.first) + " records its crossing with segment " + std::to_string(x.second[i]) + " twice";
									return true;
								}
				seen.clear();
				for (const auto& sc : w.second->getConnections()) {
					std::pair<unsigned long, std::string> k(sc.gid, sc.connection);
					if (std::find(seen.begin(), seen.end(), k) != seen.end()) {
						what = "wire " + std::to_string(w.first) + " holds gate " + std::to_string(sc.gid) + " pin " + sc.connection + " twice";
						return true;
					}
					seen.push_back(k);
				}
			}
			for (auto& c : w.second->getConnections())
				{
					auto g = gl->find(c.gid);
					if (g == gl->end() || !g->second) {   // (a null entry: the gate is gone too)
						what = "wire " + std::to_string(w.first) + " on page " + std::to_string(p) + " holds gate " + std::to_string(c.gid) + " (pin " + c.connection + ") which is gone";
						return true;
					}
					if (cl_edit_is_floating(doc)) continue;   // a paste or new part following the pointer isn't placed yet
				if (getenv("CL_FUZZ_BOTH") && g->second) {
						if (!g->second->isConnected(c.connection)) {
							what = "wire " + std::to_string(w.first) + " says it is on gate " + std::to_string(c.gid) + " pin " + c.connection + ", but the gate says that pin is free";
							return true;
						}
						if (getenv("CL_FUZZ_REACH")) {
							float px = 0, py = 0;
							g->second->getHotspotCoords(c.connection, px, py);
							bool reached = false;
							for (const auto& seg : w.second->getSegmentMap()) {
								const double l = std::min(seg.second.begin.x, seg.second.end.x) - 0.05, r = std::max(seg.second.begin.x, seg.second.end.x) + 0.05;
								const double b = std::min(seg.second.begin.y, seg.second.end.y) - 0.05, t = std::max(seg.second.begin.y, seg.second.end.y) + 0.05;
								if (px >= l && px <= r && py >= b && py <= t) reached = true;   // on the segment (axis-aligned)
							}
							if (!reached) {
								what = "wire " + std::to_string(w.first) + " does not reach gate " + std::to_string(c.gid) + " pin " + c.connection + " at (" + std::to_string(px) + ", " + std::to_string(py) + ")";
								for (const auto& seg : w.second->getSegmentMap())
									what += " [seg " + std::to_string(seg.first) + (seg.second.isVertical() ? " v " : " h ") + std::to_string(seg.second.begin.x) + "," + std::to_string(seg.second.begin.y) + " -> " + std::to_string(seg.second.end.x) + "," + std::to_string(seg.second.end.y) + "]";
								return true;
							}
						}
						guiWire* other = g->second->getConnection(c.connection);
						if (other != w.second) {
							what = "wire " + std::to_string(w.first) + " says it is on gate " + std::to_string(c.gid) + " pin " + c.connection + ", but the gate says that pin belongs to wire " + (other ? std::to_string(other->getID()) : std::string("none"));
							return true;
						}
					}
				}
		}
	}
	return false;
}


// The saved text with each run of (connect ...) lines in sorted order and without the closing
// brackets that ride on the last line: two saves that differ only in that order are the same.
static std::string canonical(const std::string& text) {
	std::vector<std::string> lines, run;
	std::string out, line;
	auto flush = [&] { std::sort(run.begin(), run.end()); for (auto& r : run) out += r + "\n"; run.clear(); };
	for (size_t i = 0; i <= text.size(); i++) {
		if (i == text.size() || text[i] == '\n') {
			size_t a = line.find_first_not_of(" \t");
			// CL_FUZZ_NOSIM=1: leave out what the simulation writes (a counter's or
			// display's CURRENT_VALUE, a Pauseulator's PAUSE_SIM), which a reload
			// recomputes by running the circuit again.
			if (getenv("CL_FUZZ_NOSIM") && a != std::string::npos) {
				static const std::string cv = "(lparam \"CURRENT_VALUE\"", ps = "(lparam \"PAUSE_SIM\"";
				if (line.compare(a, cv.size(), cv) == 0 || line.compare(a, ps.size(), ps) == 0) { line.clear(); continue; }
				while (!line.empty() && line.back() == ')') line.pop_back();   // the gate's closing bracket may ride on it
			}
			if (a != std::string::npos && line.compare(a, 8, "(connect") == 0) {
				while (!line.empty() && line.back() == ')') line.pop_back();
				run.push_back(line);
			} else { flush(); out += line + "\n"; }
			line.clear();
		} else line += text[i];
	}
	flush();
	return out;
}

// The first lines where two texts differ, for a report.
static std::string firstDiff(const std::string& a, const std::string& b) {
	size_t i = 0, line = 1, start = 0;
	while (i < a.size() && i < b.size() && a[i] == b[i]) { if (a[i] == '\n') { line++; start = i + 1; } i++; }
	auto lineAt = [](const std::string& s, size_t from) { size_t e = s.find('\n', from); return s.substr(from, e == std::string::npos ? std::string::npos : e - from); };
	return "line " + std::to_string(line) + ": \"" + lineAt(a, std::min(start, a.size())) + "\" vs \"" + lineAt(b, std::min(start, b.size())) + "\"";
}

static bool idle(CLDocument* doc) { return doc->gesture.mode == EditGesture::None && !cl_edit_tidy_active(doc); }

// CL_FUZZ_REOPEN=k: every k operations, the saved text must reopen with no
// notices and save the same again (what autosave and Revert rely on).
static bool reopenCheck(CLDocument* doc, std::string& what) {
	std::string a = cl_document_save_text(doc);
	char err[256];
	cl_set_settle_on_open(false);
	CLDocument* back = cl_document_open_text(a.c_str(), (long)a.size(), err, sizeof err);
	cl_set_settle_on_open(true);
	if (!back) { what = std::string("saved text did not reopen: ") + err; return false; }
	bool ok = true;
	if (cl_document_notice_count(back) > 0) {
		what = "reopening the saved text gave " + std::to_string(cl_document_notice_count(back)) + " notice(s): " + cl_document_notice(back, 0);
		if (getenv("CL_FUZZ_TRACE")) for (int p = 0; p < cl_document_page_count(doc); p++) {
			printf("    page %d gates:", p);
			for (auto& g : *doc->page(p)->getGateList()) printf(" %lu%s", g.first, g.second ? "" : "(null)");
			printf("\n    wires:");
			for (auto& w : *doc->page(p)->getWireList()) { printf(" %lu[", w.first); if (w.second) for (auto& c : w.second->getConnections()) printf("%lu:%s ", c.gid, c.connection.c_str()); printf("]"); }
			printf("\n");
		}
		ok = false;
	} else {
		std::string b = cl_document_save_text(back);
		if (canonical(a) != canonical(b)) { what = "save/reopen/save differs at " + firstDiff(canonical(a), canonical(b)); ok = false; }
	}
	if (!ok) if (const char* dir = getenv("CL_FUZZ_DUMP")) {
		std::string path = std::string(dir) + "/reopen.cdl";
		if (FILE* f = fopen(path.c_str(), "w")) { fwrite(a.data(), 1, a.size(), f); fclose(f); }
	}
	cl_document_close(back);
	return ok;
}

// Two saved texts describe the same circuit: the same lines, except that
// numbers may differ by float noise (a move and its undo: x + d - d), and a
// wire's segments may be numbered differently (a redo rebuilds them), so each
// wire is compared as its set of segments -- orientation, ends, pins and the
// segments it crosses, named by their ends rather than their numbers.
static std::vector<std::string> words(const std::string& s) {
	std::vector<std::string> out; std::string w;
	for (char c : s) { if (c == ' ' || c == '\t' || c == '\n') { if (!w.empty()) out.push_back(w); w.clear(); } else w += c; }
	if (!w.empty()) out.push_back(w);
	return out;
}
static bool sameWords(const std::string& a, const std::string& b) {
	std::vector<std::string> x = words(a), y = words(b);
	if (x.size() != y.size()) return false;
	for (size_t i = 0; i < x.size(); i++) {
		if (x[i] == y[i]) continue;
		char *ea, *eb;
		const double u = strtod(x[i].c_str(), &ea), v = strtod(y[i].c_str(), &eb);
		if (*ea || *eb || ea == x[i].c_str() || eb == y[i].c_str() || std::fabs(u - v) > 1e-3) return false;
	}
	return true;
}
// The first place sameWords finds a real difference, with the words around it.
static std::string firstWordDiff(const std::string& a, const std::string& b) {
	std::vector<std::string> x = words(a), y = words(b);
	size_t i = 0;
	for (; i < x.size() && i < y.size(); i++) {
		if (x[i] == y[i]) continue;
		char *ea, *eb;
		const double u = strtod(x[i].c_str(), &ea), v = strtod(y[i].c_str(), &eb);
		if (*ea || *eb || ea == x[i].c_str() || eb == y[i].c_str() || std::fabs(u - v) > 1e-3) break;
	}
	auto around = [&](const std::vector<std::string>& w) { std::string r; for (size_t k = i > 6 ? i - 6 : 0; k < std::min(w.size(), i + 8); k++) r += (k == i ? ">>" : "") + w[k] + " "; return r; };
	return "word " + std::to_string(i) + ": \"" + around(x) + "\" vs \"" + around(y) + "\"";
}
static std::string wiresAsSets(const std::string& text) {
	std::vector<std::string> lines; { std::string l; for (char c : text) { if (c == '\n') { lines.push_back(l); l.clear(); } else l += c; } lines.push_back(l); }
	auto trim = [](const std::string& l) { size_t a = l.find_first_not_of(" \t"); return a == std::string::npos ? std::string() : l.substr(a); };
	std::string out;
	for (size_t i = 0; i < lines.size(); ) {
		std::string t = trim(lines[i]);
		if (t.compare(0, 5, "(seg ") != 0) { out += lines[i] + "\n"; i++; continue; }
		// a wire's run of segments
		struct Seg { std::string id, head, pts; std::vector<std::string> rest; std::vector<std::pair<std::string, std::string>> cross; };
		std::vector<Seg> segs;
		while (i < lines.size()) {
			std::string u = trim(lines[i]);
			if (u.compare(0, 5, "(seg ") == 0) {
				Seg s; std::vector<std::string> w = words(u);
				s.id = w.size() > 1 ? w[1] : ""; s.head = w.size() > 2 ? w[2] : "";
				segs.push_back(s);
			} else if (!segs.empty() && u.compare(0, 5, "(pts ") == 0) segs.back().pts = u;
			else if (!segs.empty() && u.compare(0, 7, "(cross ") == 0) { std::vector<std::string> w = words(u); segs.back().cross.push_back({ w.size() > 1 ? w[1] : "", w.size() > 2 ? w[2] : "" }); }
			else if (!segs.empty() && u.compare(0, 9, "(connect ") == 0) segs.back().rest.push_back(u);
			else break;
			i++;
		}
		std::vector<std::string> flat;
		for (auto& s : segs) {
			// A segment that is a single point has no direction to compare.
			std::string bare = s.pts;
			while (!bare.empty() && bare.back() == ')') bare.pop_back();
			std::vector<std::string> pw = words(bare);
			const bool dot = pw.size() >= 5 && std::fabs(atof(pw[1].c_str()) - atof(pw[3].c_str())) < 1e-3 && std::fabs(atof(pw[2].c_str()) - atof(pw[4].c_str())) < 1e-3;
			std::string f = (dot ? std::string("dot") : s.head) + " " + s.pts;
			std::sort(s.rest.begin(), s.rest.end());
			for (auto& r : s.rest) f += " | " + r;
			std::vector<std::string> xs;
			for (auto& c : s.cross) { std::string target = "?"; for (auto& o : segs) if (o.id == c.second) target = o.pts; xs.push_back(c.first + " -> " + target); }
			std::sort(xs.begin(), xs.end());
			for (auto& x : xs) f += " x " + x;
			flat.push_back(f);
		}
		std::sort(flat.begin(), flat.end());
		for (auto& f : flat) out += f + "\n";
	}
	return out;
}
// What isn't an undo step, by design: a switch or keypad's position, a pulse,
// a manual clock's level and memory contents (clicks and the memory editor).
// A redo that makes a part again makes it fresh, so these are left out.
static std::string withoutClickState(const std::string& text) {
	static const char* names[] = { "(lparam \"OUTPUT_NUM\"", "(lparam \"PULSE\"", "(lparam \"MANUAL_LEVEL\"", "(lparam \"Address:" };
	std::string out, line;
	for (size_t i = 0; i <= text.size(); i++) {
		if (i < text.size() && text[i] != '\n') { line += text[i]; continue; }
		size_t a = line.find_first_not_of(" \t");
		bool skip = false;
		if (a != std::string::npos) for (const char* n : names) if (line.compare(a, strlen(n), n) == 0) skip = true;
		if (!skip) { while (!line.empty() && line.back() == ')') line.pop_back(); out += line + "\n"; }
		line.clear();
	}
	return out;
}
// The circuit without wire geometry: every line but a wire's segments, and
// for each wire the sorted pins it connects.
static std::string netlist(const std::string& text) {
	std::string out, line;
	std::vector<std::string> pins;
	bool inWire = false;
	auto flushWire = [&] { std::sort(pins.begin(), pins.end()); for (auto& p : pins) out += "  pin " + p + "\n"; pins.clear(); };
	for (size_t i = 0; i <= text.size(); i++) {
		if (i < text.size() && text[i] != '\n') { line += text[i]; continue; }
		size_t a = line.find_first_not_of(" \t");
		std::string t = a == std::string::npos ? std::string() : line.substr(a);
		while (!t.empty() && t.back() == ')') t.pop_back();
		if (t.compare(0, 5, "(wire") == 0 || t.compare(0, 5, "(gate") == 0 || t.compare(0, 5, "(page") == 0) { flushWire(); inWire = t.compare(0, 5, "(wire") == 0; out += t + "\n"; }
		else if (inWire && t.compare(0, 9, "(connect ") == 0) pins.push_back(t);
		else if (inWire && (t.compare(0, 5, "(seg ") == 0 || t.compare(0, 5, "(pts ") == 0 || t.compare(0, 7, "(cross ") == 0)) {}
		else out += t + "\n";
		line.clear();
	}
	flushWire();
	return out;
}
static std::string semantic(const std::string& text) { return wiresAsSets(withoutClickState(text)); }
static bool sameCircuit(const std::string& a, const std::string& b) {
	if (a == b) return true;
	return sameWords(semantic(a), semantic(b));
}

// CL_FUZZ_UNDOALL=k: every k operations (and at the end), undo everything
// then redo everything: the circuit must save as it did before.
static bool undoAllCheck(CLDocument* doc, std::string& what) {
	std::string a = canonical(cl_document_save_text(doc));
	int undone = 0;
	while (cl_edit_can_undo(doc) && undone < 100000) { cl_edit_undo(doc); undone++; }
	if (getenv("CL_FUZZ_INVARIANT") && danglingConnection(doc, what)) { what = "after undoing all " + std::to_string(undone) + " steps: " + what; return false; }
	int redone = 0;   // as many as were undone (steps already undone before stay so)
	while (redone < undone && cl_edit_can_redo(doc)) { cl_edit_redo(doc); redone++; }
	if (getenv("CL_FUZZ_INVARIANT") && danglingConnection(doc, what)) { what = "after redoing all " + std::to_string(redone) + " steps: " + what; return false; }
	std::string b = canonical(cl_document_save_text(doc));
	if (redone != undone) { what = "undid " + std::to_string(undone) + " steps but could redo only " + std::to_string(redone); return false; }
	if (!sameCircuit(a, b)) {
		// Which kind: the parts, settings and what each wire connects (a lost or
		// added connection, a part in the wrong place), or only the wires' routes.
		const bool shapeOnly = sameWords(netlist(withoutClickState(a)), netlist(withoutClickState(b)));
		what = "undo all (" + std::to_string(undone) + ") then redo all changed " + (shapeOnly ? "only wire shapes" : "the circuit") + " at " +
			(shapeOnly ? firstWordDiff(semantic(a), semantic(b)) : firstWordDiff(netlist(withoutClickState(a)), netlist(withoutClickState(b))));
		if (const char* dir = getenv("CL_FUZZ_DUMP")) {
			for (int w = 0; w < 2; w++) {
				std::string path = std::string(dir) + (w ? "/undoall-b.cdl" : "/undoall-a.cdl");
				if (FILE* f = fopen(path.c_str(), "w")) { const std::string& s = w ? b : a; fwrite(s.data(), 1, s.size(), f); fclose(f); }
			}
		}
		return false;
	}
	return true;
}

static int envInt(const char* name) { const char* v = getenv(name); return v ? atoi(v) : 0; }

// `startText`, when not empty, is the circuit to start from (a previous seed's
// saved text, CL_FUZZ_CHAIN); it is replaced with this seed's.
static bool oneSeed(unsigned long long seed, int ops, const char* startFile, std::string* startText) {
	rs = seed * 2654435761ULL + 88172645463325252ULL;
	for (int i = 0; i < 5; i++) rnd();
	char err[256];
	CLDocument* doc = (startText && !startText->empty()) ? cl_document_open_text(startText->c_str(), (long)startText->size(), err, sizeof err)
		: startFile ? cl_document_open(startFile, err, sizeof err) : cl_document_new();
	if (!doc) { printf("seed %llu: could not open: %s\n", seed, startFile ? err : "new"); return false; }
	const bool wide = getenv("CL_FUZZ_WIDE") != nullptr;
	const int reopenEvery = envInt("CL_FUZZ_REOPEN"), undoAllEvery = envInt("CL_FUZZ_UNDOALL");
	const bool undoStep = getenv("CL_FUZZ_UNDOSTEP") != nullptr;
	// CL_FUZZ_SIM=1: half the seeds run the simulation the whole session.
	simFrames = getenv("CL_FUZZ_SIM") && (seed % 2 == 1);
	if (simFrames) cl_document_set_running(doc, true);
	int pages = cl_document_page_count(doc);
	for (int n = 0; n < ops; n++) {
		pages = cl_document_page_count(doc);
		int page = ri(pages);
		const int op = ri(wide ? 46 : 24);
		if (getenv("CL_FUZZ_TRACE")) printf("  op %d: case %d (page %d of %d)%s\n", n, op, page, pages, cl_edit_is_floating(doc) ? " floating" : "");
		// CL_FUZZ_UNDOSTEP=1: an operation that adds one undo step is undone and
		// redone at once: Undo must give back the circuit before it, Redo after it.
		const bool stepCheck = undoStep && idle(doc);
		CLDocument* const before = doc;
		std::string pre;
		const int preSteps = stepCheck ? cl_edit_undo_count(doc) : 0;
		if (stepCheck) pre = canonical(cl_document_save_text(doc));
		// Pages added, moved or renamed aren't undo steps: an op that did that too is not checked.
		auto pageKey = [](CLDocument* d) { std::string k; for (int i = 0; i < cl_document_page_count(d); i++) k += std::to_string(cl_document_page_id(d, i)) + ":" + cl_document_page_name(d, i) + ";"; return k; };
		const std::string prePages = stepCheck ? pageKey(doc) : std::string();
		if (op >= 24) wideOp(doc, op - 24, page, pages);
		else switch (op) {
		case 0: case 1: case 2: case 3:
			cl_edit_add_gate(doc, page, libGates[ri((int)libGates.size())].c_str(), rf(-30, 30), rf(-30, 30));
			break;
		case 4: case 5: case 6: {   // drag a part (or empty canvas: a box)
			guiGate* g = randomGate(doc, page);
			double x0 = rf(-30, 30), y0 = rf(-30, 30);
			if (g && ri(4)) { klsBBox b = g->getSelectionBBox(); x0 = (b.getLeft() + b.getRight()) / 2; y0 = (b.getBottom() + b.getTop()) / 2; }
			gesture(doc, page, x0, y0, x0 + rf(-15, 15), y0 + rf(-15, 15), ri(5) == 0 ? CL_MOD_SHIFT : 0);
			break; }
		case 7: case 8: case 9: {   // draw a wire between two pins
			guiGate *a = randomGate(doc, page), *b = randomGate(doc, page);
			if (!a || !b) break;
			Pin pa = randomPin(a), pb = randomPin(b);
			if (pa.ok && pb.ok) gesture(doc, page, pa.x, pa.y, pb.x, pb.y, 0);
			break; }
		case 10: cl_edit_select_all(doc, page); break;
		case 11: cl_edit_select_none(doc, page); break;
		case 12: if (ri(3) == 0) cl_edit_delete(doc, page); break;
		case 13: cl_edit_rotate(doc, page); break;
		case 14: cl_edit_nudge(doc, page, rf(-2, 2), rf(-2, 2)); break;
		case 15: {   // copy then paste
			std::string t = cl_edit_copy(doc, page);
			if (getenv("CL_FUZZ_TRACE")) printf("    copied:\n%s", t.c_str());
			if (!t.empty()) { const char* back = nullptr; cl_edit_paste(doc, page, t.c_str(), rf(-30, 30), rf(-30, 30), ri(2), &back); }
			break; }
		case 16: case 17: if (cl_edit_can_undo(doc)) cl_edit_undo(doc); break;
		case 18: if (cl_edit_can_redo(doc)) cl_edit_redo(doc); break;
		case 19:
			if (ri(6) == 0 && pages < 6) cl_document_add_page(doc);
			else if (ri(6) == 0 && pages > 1) cl_document_delete_page(doc, page);
			else if (ri(8) == 0 && pages > 1) cl_document_close_page(doc, page);
			break;
		case 20: if (ri(2)) cl_edit_straighten(doc, page); else if (cl_edit_tidy_begin(doc, page, ri(2))) cl_edit_tidy_end(doc, ri(2)); break;
		case 21: if (ri(2)) cl_edit_lock_selection(doc, page, true); else cl_edit_unlock_all(doc, page); break;
		case 22:
			cl_document_set_running(doc, ri(3) != 0);
			for (int i = 0; i < 1 + ri(8); i++) cl_document_tick(doc, rf(1, 120));
			if (ri(3) == 0) cl_document_step(doc);
			break;
		case 23: cl_document_click(doc, page, rf(-30, 30), rf(-30, 30)); break;
		}
		watchPrint(doc, "after op");
		if (stepCheck) { std::string what; if (getenv("CL_FUZZ_INVARIANT") && danglingConnection(doc, what)) { printf("seed %llu: after op %d (case %d): %s\n", seed, n, op, what.c_str()); cl_document_close(doc); return false; } }
		if (stepCheck && doc == before && idle(doc) && cl_edit_undo_count(doc) == preSteps + 1 && !cl_edit_can_redo(doc) && pageKey(doc) == prePages) {
			const std::string name = cl_edit_undo_name(doc);
			const std::string post = canonical(cl_document_save_text(doc));
			cl_edit_undo(doc);
			const std::string undone = canonical(cl_document_save_text(doc));
			cl_edit_redo(doc);
			const std::string redone = canonical(cl_document_save_text(doc));
			for (int w = 0; w < 2; w++) {
				const std::string& want = w ? post : pre;
				const std::string& got = w ? redone : undone;
				if (sameCircuit(want, got)) continue;
				const bool shapeOnly = sameWords(netlist(withoutClickState(want)), netlist(withoutClickState(got)));
				printf("seed %llu: after op %d (case %d): %s of \"%s\" changed %s at %s\n", seed, n, op, w ? "Redo" : "Undo", name.c_str(),
					shapeOnly ? "only wire shapes" : "the circuit",
					(shapeOnly ? firstWordDiff(semantic(want), semantic(got)) : firstWordDiff(netlist(withoutClickState(want)), netlist(withoutClickState(got)))).c_str());
				if (const char* dir = getenv("CL_FUZZ_DUMP")) {
					for (int k = 0; k < 2; k++) {
						std::string path = std::string(dir) + (k ? "/step-got.cdl" : "/step-want.cdl");
						if (FILE* f = fopen(path.c_str(), "w")) { const std::string& t = k ? got : want; fwrite(t.data(), 1, t.size(), f); fclose(f); }
					}
				}
				cl_document_close(doc);
				return false;
			}
		}
		if (simFrames) for (int i = 0, k = 1 + ri(3); i < k; i++) cl_document_tick(doc, 16.7);
		{ std::string what; if (getenv("CL_FUZZ_INVARIANT") && danglingConnection(doc, what)) { printf("seed %llu: after op %d (case %d): %s\n", seed, n, op, what.c_str()); cl_document_close(doc); return false; } }
		if (reopenEvery > 0 && (n + 1) % reopenEvery == 0 && idle(doc)) {
			std::string what;
			if (!reopenCheck(doc, what)) { printf("seed %llu: after op %d (case %d): %s\n", seed, n, op, what.c_str()); cl_document_close(doc); return false; }
		}
		if (undoAllEvery > 0 && (n + 1) % undoAllEvery == 0 && idle(doc)) {
			std::string what;
			if (!undoAllCheck(doc, what)) { printf("seed %llu: after op %d (case %d): %s\n", seed, n, op, what.c_str()); cl_document_close(doc); return false; }
		}
		fflush(stdout);
	}
	// Whatever is still on the pointer is dropped, and a Tidy preview kept, before the save.
	if (cl_edit_is_floating(doc)) { cl_edit_press(doc, doc->gesture.page, doc->gesture.current.x, doc->gesture.current.y, 0, 0.05); cl_edit_release(doc, 0, 0); }
	cl_edit_tidy_end(doc, true);
	if (undoAllEvery > 0) {
		std::string what;
		if (!undoAllCheck(doc, what)) { printf("seed %llu: at the end: %s\n", seed, what.c_str()); cl_document_close(doc); return false; }
	}
	// Save, read back, save again: the text must be stable.
	std::string a = cl_document_save_text(doc);
	if (startText) *startText = a;
	CLDocument* back = cl_document_open_text(a.c_str(), (long)a.size(), err, sizeof err);
	bool ok = true;
	if (!back) { printf("seed %llu: saved text did not reopen: %s\n", seed, err); ok = false; }
	else {
		std::string b = cl_document_save_text(back);
		if (canonical(a) != canonical(b)) {
			printf("seed %llu: save/reload/save is not stable (%zu vs %zu bytes)\n", seed, a.size(), b.size()); ok = false;
			if (const char* dir = getenv("CL_FUZZ_DUMP")) {   // keep both texts, to diff
				for (int w = 0; w < 2; w++) {
					std::string path = std::string(dir) + "/seed" + std::to_string(seed) + (w ? "-b.cdl" : "-a.cdl");
					if (FILE* f = fopen(path.c_str(), "w")) { const std::string& s = w ? b : a; fwrite(s.data(), 1, s.size(), f); fclose(f); }
				}
			}
		}
		if (cl_document_page_count(back) != cl_document_page_count(doc))
			{ printf("seed %llu: page count changed on reload\n", seed); ok = false; }
		cl_document_close(back);
	}
	cl_document_close(doc);
	return ok;
}

int main(int argc, char** argv) {
	if (argc < 5 || !cl_library_load(argv[1])) { fprintf(stderr, "usage: fuzz_edit gatedefs first-seed count ops [start.cdl...]\n"); return 2; }
	for (int c = 0; c < cl_library_category_count(); c++)
		for (int g = 0; g < cl_library_gate_count(c); g++) libGates.push_back(cl_library_gate(c, g));
	if (libGates.empty()) { fprintf(stderr, "no gates in the library\n"); return 2; }
	unsigned long long first = strtoull(argv[2], 0, 10);
	int count = atoi(argv[3]), ops = atoi(argv[4]), bad = 0;
	// Several start files: seed s starts from file s % count. CL_FUZZ_CHAIN=1:
	// each seed carries on from the circuit the one before it saved.
	std::vector<const char*> starts(argv + 5, argv + argc);
	std::string carry;
	const bool chain = getenv("CL_FUZZ_CHAIN") != nullptr;
	for (int i = 0; i < count; i++) {
		const unsigned long long seed = first + i;
		const char* start = starts.empty() ? nullptr : starts[seed % starts.size()];
		if (getenv("CL_FUZZ_SAYSEED")) { printf("seed %llu from %s\n", seed, chain && !carry.empty() ? "the previous seed" : start ? start : "new"); fflush(stdout); }
		if (!oneSeed(seed, ops, start, chain ? &carry : nullptr)) bad++;
		if ((i + 1) % 25 == 0) { printf("%d/%d seeds, %d failed\n", i + 1, count, bad); fflush(stdout); }
	}
	printf("%d seeds, %d failed\n", count, bad);
	return bad ? 1 : 0;
}
