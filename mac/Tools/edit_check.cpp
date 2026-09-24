// Headless check of editing through the C interface, reading the model
// directly to confirm each edit and each undo.
//   edit_check <cl_gatedefs.xml> <in.cdl> <page>
#include "DocumentImpl.h"
#include "guiGate.h"
#include "guiWire.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <map>
#include <vector>
#include <string>

static int fails = 0;
#define CHECK(c, what) do { if (c) printf("  ok   %s\n", what); else { printf("  FAIL %s\n", what); fails++; } } while (0)

static guiGate* firstGate(CLDocument* doc, int page, bool unwired) {
	for (auto& g : *doc->page(page)->getGateList()) {
		if (!g.second) continue;
		bool wired = false;
		for (auto& hs : g.second->getHotspotList()) if (g.second->isConnected(hs.first)) wired = true;
		if (unwired == !wired) return g.second;
	}
	return nullptr;
}

int main(int argc, char** argv) {
	if (argc < 4 || !cl_library_load(argv[1])) return 2;
	char err[256];
	CLDocument* doc = cl_document_open(argv[2], err, sizeof err);
	if (!doc) { printf("%s\n", err); return 1; }
	const int page = atoi(argv[3]);
	const size_t gates0 = doc->page(page)->getGateList()->size();
	const double upp = 0.05;

	printf("drag a wired gate\n");
	guiGate* g = firstGate(doc, page, false);
	float x0, y0; g->getGLcoords(x0, y0);
	klsBBox b = g->getSelectionBBox();
	const double px = (b.getLeft() + b.getRight()) / 2, py = (b.getBottom() + b.getTop()) / 2;
	CHECK(cl_edit_press(doc, page, px, py, 0, upp) == CL_PRESS_PART, "press lands on the gate");
	CHECK(g->isSelected(), "gate selected");
	cl_edit_drag(doc, px + 1.0, py);
	cl_edit_drag(doc, px + 3.0, py - 2.0);
	cl_edit_release(doc, px + 3.0, py - 2.0);
	float x1, y1; g->getGLcoords(x1, y1);
	CHECK(std::fabs(x1 - x0 - 3) < 1e-3 && std::fabs(y1 - y0 + 2) < 1e-3, "gate moved by (3, -2)");
	CHECK(cl_edit_can_undo(doc), "move is undoable");
	printf("  undo name: %s\n", cl_edit_undo_name(doc));
	cl_edit_undo(doc);
	g->getGLcoords(x1, y1);
	CHECK(std::fabs(x1 - x0) < 1e-3 && std::fabs(y1 - y0) < 1e-3, "undo puts it back");
	cl_edit_redo(doc);
	g->getGLcoords(x1, y1);
	CHECK(std::fabs(x1 - x0 - 3) < 1e-3, "redo moves it again");
	cl_edit_undo(doc);

	printf("delete and undo\n");
	cl_edit_select_none(doc, page);
	g->select();
	cl_edit_delete(doc, page);
	CHECK(doc->page(page)->getGateList()->size() == gates0 - 1, "gate deleted");
	cl_edit_undo(doc);
	CHECK(doc->page(page)->getGateList()->size() == gates0, "undo brings it back");

	printf("add a gate\n");
	CHECK(cl_edit_add_gate(doc, page, "AA_AND2", 40, -40), "AND gate placed");
	CHECK(doc->page(page)->getGateList()->size() == gates0 + 1, "one more gate");
	CHECK(cl_edit_selected_gate_count(doc, page) == 1, "new gate is the selection");
	const long nid = cl_edit_single_gate(doc, page);
	printf("  caption: %s, settings: %d\n", cl_gate_caption(doc, nid), cl_gate_setting_count(doc, nid));

	printf("rotate the new (unwired) gate\n");
	guiGate* ng = doc->circuit.getGate((unsigned long)nid);
	const std::string a0 = ng->getGUIParam("angle");
	cl_edit_rotate(doc, page);
	CHECK(ng->getGUIParam("angle") != a0, "angle changed");
	printf("  angle %s -> %s\n", a0.c_str(), ng->getGUIParam("angle").c_str());
	cl_edit_undo(doc);
	CHECK(ng->getGUIParam("angle") == a0 || (a0.empty() && ng->getGUIParam("angle") == "0"), "undo rotation");

	printf("copy and paste\n");
	const std::string text = cl_edit_copy(doc, page);
	CHECK(!text.empty(), "copied text");
	const char* back = nullptr;
	CHECK(cl_edit_paste(doc, page, text.c_str(), 50, -50, false, &back), "pasted");
	CHECK(doc->page(page)->getGateList()->size() == gates0 + 2, "one more gate again");
	cl_edit_undo(doc);
	CHECK(doc->page(page)->getGateList()->size() == gates0 + 1, "undo paste");

	printf("box select\n");
	double l, bt, r, t;
	cl_document_page_bounds(doc, page, &l, &bt, &r, &t);
	cl_edit_press(doc, page, l - 5, t + 5, 0, upp);
	cl_edit_drag(doc, r + 5, bt - 5);
	double bl, bb, br, btop;
	CHECK(cl_edit_box(doc, &bl, &bb, &br, &btop), "box reported while dragging");
	cl_edit_release(doc, r + 5, bt - 5);
	CHECK(cl_edit_selected_gate_count(doc, page) == (int)gates0 + 1, "box selects every gate");

	printf("connect two pins by dragging\n");
	cl_edit_select_none(doc, page);
	cl_edit_add_gate(doc, page, "AA_AND2", 80, -80);
	guiGate* ga = doc->circuit.getGate((unsigned long)cl_edit_single_gate(doc, page));
	cl_edit_add_gate(doc, page, "AA_AND2", 92, -80);
	guiGate* gb = doc->circuit.getGate((unsigned long)cl_edit_single_gate(doc, page));
	std::string outPin, inPin;
	for (auto& hs : ga->getHotspotList()) if (!ga->isConnectionInput(hs.first)) outPin = hs.first;
	for (auto& hs : gb->getHotspotList()) if (gb->isConnectionInput(hs.first)) { inPin = hs.first; break; }
	float ox, oy, ix, iy;
	ga->getHotspotCoords(outPin, ox, oy);
	gb->getHotspotCoords(inPin, ix, iy);
	const size_t wires0 = doc->page(page)->getWireList()->size();
	CHECK(cl_edit_press(doc, page, ox, oy, 0, upp) == CL_PRESS_PART, "press on an output pin");
	cl_edit_drag(doc, (ox + ix) / 2, (oy + iy) / 2);
	cl_edit_drag(doc, ix, iy);
	cl_edit_release(doc, ix, iy);
	CHECK(doc->page(page)->getWireList()->size() == wires0 + 1, "a wire joins them");
	CHECK(ga->isConnected(outPin) && gb->isConnected(inPin), "both pins connected");
	printf("  undo name: %s\n", cl_edit_undo_name(doc));
	cl_edit_undo(doc);
	CHECK(doc->page(page)->getWireList()->size() == wires0 && !ga->isConnected(outPin), "undo removes the wire");

	printf("click a pin, then click the target\n");
	cl_edit_press(doc, page, ox, oy, 0, upp);
	cl_edit_release(doc, ox, oy);
	CHECK(cl_edit_is_connecting(doc), "line follows the pointer");
	cl_edit_hover(doc, page, ix, iy, upp);
	cl_edit_press(doc, page, ix, iy, 0, upp);
	cl_edit_release(doc, ix, iy);
	CHECK(ga->isConnected(outPin) && gb->isConnected(inPin), "click-click connects");
	CHECK(!cl_edit_is_connecting(doc), "and stops following");

	printf("drag a wire segment\n");
	guiWire* w = ga->getConnection(outPin);
	const auto shape0 = w->getSegmentMap();
	wireSegment seg = shape0.begin()->second;
	for (auto& e : shape0) if (e.second.isVertical() || (e.second.end.x - e.second.begin.x) > 2) { seg = e.second; break; }
	const float mx = (seg.begin.x + seg.end.x) / 2, my = (seg.begin.y + seg.end.y) / 2;
	CHECK(cl_edit_press(doc, page, mx, my, 0, upp) == CL_PRESS_PART, "press on the wire");
	cl_edit_drag(doc, mx + 2, my + 2);
	cl_edit_drag(doc, mx + 3, my + 3);
	cl_edit_release(doc, mx + 3, my + 3);
	bool changed = w->getSegmentMap().size() != shape0.size();
	for (auto& e : w->getSegmentMap()) {
		auto it = shape0.find(e.first);
		if (it == shape0.end() || it->second.begin.x != e.second.begin.x || it->second.begin.y != e.second.begin.y) changed = true;
	}
	CHECK(changed, "the wire's shape changed");
	printf("  undo name: %s\n", cl_edit_undo_name(doc));
	cl_edit_undo(doc);

	printf("straighten\n");
	cl_edit_select_none(doc, page);
	w->select();
	cl_edit_straighten(doc, page);
	printf("  undo name: %s\n", cl_edit_undo_name(doc));
	CHECK(cl_edit_can_undo(doc), "straighten is undoable");
	CHECK(ga->isConnected(outPin) && gb->isConnected(inPin), "still connected");

	printf("disconnect a pin (right-click)\n");
	CHECK(cl_edit_context(doc, page, ix, iy, upp) == CL_CONTEXT_PIN, "right-click finds the connected pin");
	cl_edit_disconnect_pin(doc, page, ix, iy, upp);
	CHECK(!gb->isConnected(inPin), "pin disconnected");
	cl_edit_undo(doc);
	CHECK(gb->isConnected(inPin), "undo reconnects");

	printf("tidy up\n");
	cl_edit_select_none(doc, page);
	std::map<unsigned long, std::pair<float, float>> pos;
	for (auto& e : *doc->page(page)->getGateList()) { float x, y; e.second->getGLcoords(x, y); pos[e.first] = {x, y}; }
	auto samePlaces = [&] {
		for (auto& e : *doc->page(page)->getGateList()) { float x, y; e.second->getGLcoords(x, y); if (pos[e.first] != std::make_pair(x, y)) return false; }
		return true;
	};
	const bool began = cl_edit_tidy_begin(doc, page, 1);
	CHECK(began && cl_edit_tidy_active(doc), "full rearrange shows a preview");
	CHECK(!samePlaces(), "gates moved in the preview");
	cl_edit_tidy_end(doc, false);
	CHECK(samePlaces(), "revert puts every gate back");
	cl_edit_tidy_begin(doc, page, 1);
	cl_edit_tidy_end(doc, true);
	printf("  undo name: %s\n", cl_edit_undo_name(doc));
	CHECK(!samePlaces(), "kept");
	cl_edit_undo(doc);
	CHECK(samePlaces(), "one undo puts it all back");
	cl_edit_tidy_begin(doc, page, 0);
	cl_edit_delete(doc, page);   // any other edit keeps the preview
	CHECK(!cl_edit_tidy_active(doc), "another edit keeps the preview");

	printf("simulation keeps running\n");
	const int before = (int)doc->sim->stepsRun();
	cl_document_tick(doc, 500);
	CHECK((int)doc->sim->stepsRun() > before, "steps ran");

	printf("oscilloscope\n");
	cl_scope_clear(doc);
	cl_document_set_running(doc, true);
	for (int i = 0; i < 40; i++) cl_document_tick(doc, 100);
	const int nsig = cl_scope_signal_count(doc);
	const long long len = cl_scope_length(doc);
	printf("  %d signals, %lld samples\n", nsig, len);
	CHECK(nsig > 0, "TO labels become signals");
	CHECK(len > 0, "samples recorded as it runs");
	int changing = 0;
	for (int sgi = 0; sgi < nsig; sgi++) {
		std::vector<unsigned char> buf((size_t)len);
		cl_scope_samples(doc, sgi, 0, (int)len, buf.data());
		bool changes = false;
		for (size_t i = 1; i < buf.size(); i++) if (buf[i] != buf[0]) changes = true;
		if (changes) changing++;
		printf("  %-10s", cl_scope_signal(doc, sgi));
		for (size_t i = 0; i < buf.size() && i < 60; i++) printf("%c", "01Z!?"[buf[i] < 5 ? buf[i] : 4]);
		printf("\n");
	}
	CHECK(changing > 0, "a clocked signal changes over time");

	cl_document_close(doc);
	printf(fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails ? 1 : 0;
}
