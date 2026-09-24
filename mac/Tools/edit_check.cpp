// Headless check of editing through the C interface, reading the model
// directly to confirm each edit and each undo.
//   edit_check <cl_gatedefs.xml> <in.cdl> <page>
#include "DocumentImpl.h"
#include "guiGate.h"
#include "guiWire.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>

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

	printf("simulation keeps running\n");
	const int before = (int)doc->sim->stepsRun();
	cl_document_tick(doc, 500);
	CHECK((int)doc->sim->stepsRun() > before, "steps ran");

	cl_document_close(doc);
	printf(fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails ? 1 : 0;
}
