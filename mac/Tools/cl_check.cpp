// Headless check of what the CedarLogic interface adds to the engine: a
// circuit built from nothing the way the guided tour builds it (gates that
// follow the pointer until dropped, wires between pins), what the tour and
// Simulation View read from it, connect-nearby, closing and reopening pages,
// moving pages, memory contents, and the new drawing calls.
//   cl_check <cl_gatedefs.xml>
#include "Bitmap.h"   // first: on Windows it brings the system headers
#include "DocumentImpl.h"
#include "guiGate.h"
#include "guiWire.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(c, what) do { if (c) printf("  ok   %s\n", what); else { printf("  FAIL %s\n", what); fails++; } } while (0)

static guiGate* newest(CLDocument* doc, int page) {
	guiGate* best = nullptr;
	for (auto& g : *doc->page(page)->getGateList())
		if (g.second && (best == nullptr || g.first > best->getID())) best = g.second;
	return best;
}

static bool pin(guiGate* g, const std::string& prefix, float& x, float& y, std::string* nameOut = nullptr) {
	for (auto& hs : g->getHotspotList())
		if (hs.first.rfind(prefix, 0) == 0 && !g->isConnected(hs.first)) {
			g->getHotspotCoords(hs.first, x, y);
			if (nameOut) *nameOut = hs.first;
			return true;
		}
	return false;
}

static void wire(CLDocument* doc, guiGate* a, const std::string& ap, guiGate* b, const std::string& bp) {
	float ax, ay, bx, by;
	pin(a, ap, ax, ay);
	pin(b, bp, bx, by);
	cl_edit_press(doc, 0, ax, ay, 0, 0.05);
	cl_edit_drag(doc, (ax + bx) / 2, (ay + by) / 2);
	cl_edit_drag(doc, bx, by);
	cl_edit_release(doc, bx, by);
}

static void click(CLDocument* doc, guiGate* g) {
	float x, y;
	g->getGLcoords(x, y);
	cl_document_click(doc, 0, x, y);
	for (int i = 0; i < 10; i++) cl_document_tick(doc, 50);
}

int main(int argc, char** argv) {
	if (argc < 2 || !cl_library_load(argv[1])) return 2;
	CLDocument* doc = cl_document_new();
	cl_document_set_running(doc, true);

	printf("a gate follows the pointer until a click drops it\n");
	CHECK(cl_edit_add_gate(doc, 0, "AA_TOGGLE", 0, 4), "switch added");
	guiGate* s1 = newest(doc, 0);
	const int undo0 = cl_edit_undo_count(doc);
	CHECK(cl_edit_float_begin(doc, 0, 0, 4), "it floats");
	CHECK(cl_edit_is_floating(doc), "floating");
	cl_edit_hover(doc, 0, 1, 5, 0.05);
	cl_edit_hover(doc, 0, 2, 6, 0.05);
	float x, y;
	s1->getGLcoords(x, y);
	CHECK(std::fabs(x - 2) < 1e-3 && std::fabs(y - 6) < 1e-3, "it follows the pointer");
	cl_edit_press(doc, 0, 2, 6, 0, 0.05);
	cl_edit_release(doc, 2, 6);
	CHECK(!cl_edit_is_floating(doc), "the click dropped it");
	CHECK(cl_edit_undo_count(doc) == undo0 + 1, "the drop is one undo step");
	cl_edit_undo(doc);
	s1->getGLcoords(x, y);
	CHECK(std::fabs(x) < 1e-3 && std::fabs(y - 4) < 1e-3, "undo puts it where it was added");
	cl_edit_redo(doc);

	printf("escape while floating\n");
	cl_edit_add_gate(doc, 0, "AA_TOGGLE", 0, -4);
	guiGate* s2 = newest(doc, 0);
	cl_edit_float_begin(doc, 0, 0, -4);
	cl_edit_hover(doc, 0, 3, -7, 0.05);
	cl_edit_cancel(doc);
	s2->getGLcoords(x, y);
	CHECK(std::fabs(x) < 1e-3 && std::fabs(y + 4) < 1e-3, "cancel puts it back");

	printf("build the tour's circuit\n");
	cl_edit_add_gate(doc, 0, "AA_AND2", 10, 1);
	guiGate* andg = newest(doc, 0);
	cl_edit_add_gate(doc, 0, "GA_LED", 20, 1);
	guiGate* led = newest(doc, 0);
	CLTourStatus st;
	cl_tour_status(doc, 0, &st);
	CHECK(st.switches == 2 && st.hasAnd && st.lights == 1, "tour sees two switches, an AND and a light");
	wire(doc, s1, "OUT", andg, "IN");
	wire(doc, s2, "OUT", andg, "IN");
	cl_tour_status(doc, 0, &st);
	printf("  AND inputs wired: %d\n", st.andInputsWired);
	CHECK(st.andInputsWired == 2, "both switches wired to the AND");
	wire(doc, andg, "OUT", led, "");
	cl_tour_status(doc, 0, &st);
	CHECK(st.lightWired, "the light is wired to the AND");
	click(doc, s1);
	click(doc, s2);
	cl_tour_status(doc, 0, &st);
	CHECK(st.switchesOn == 2 && st.lightOn, "both on: the light is on");

	printf("Simulation View\n");
	CLSimChip chips[8];
	const int n = cl_simview_chips(doc, 0, chips, 8);
	CHECK(n == 3, "three chips: two switches, one light");
	CHECK(n == 3 && chips[0].isInput && chips[1].isInput && !chips[2].isInput, "inputs first");
	CHECK(n == 3 && chips[0].lit && chips[1].lit && chips[2].lit, "all lit");
	{
		Bitmap bitmap(400, 300, false);
		CLContext ctx = bitmap.ctx();
		CLDrawOptions o = { true, 3, 1.0, true, false, false, 1.0 };
		cl_document_draw_ex(doc, 0, ctx, 2, -5, 12, 0.1, &o);
		cl_simview_draw_flow(doc, 0, ctx, 2, -5, 12, 0.1, 7.0, 1.0);
		o.simView = false; o.thumbnail = true;
		cl_document_draw_ex(doc, 0, ctx, 2, -5, 12, 0.1, &o);
		const std::vector<unsigned char> px = bitmap.pixels();
		int inked = 0;
		for (int i = 0; i < 400 * 300; i++) if (px[i * 4 + 3] != 0) inked++;
		CHECK(inked > 200, "styled, flow and thumbnail drawing put ink down");
	}

	printf("connect nearby (C)\n");
	cl_edit_add_gate(doc, 0, "AA_AND2", 10, -12);
	guiGate* and2 = newest(doc, 0);
	float ox, oy;
	pin(and2, "OUT", ox, oy);
	cl_edit_add_gate(doc, 0, "GA_LED", 30, -12);
	guiGate* led2 = newest(doc, 0);
	float lx, ly, cx, cy;
	std::string ledPin;
	pin(led2, "", lx, ly, &ledPin);
	led2->getGLcoords(cx, cy);
	led2->setGLcoords(cx + (ox + 0.5f) - lx, cy + oy - ly);   // its pin half a square from the output
	cl_edit_select_none(doc, 0);
	led2->select();
	const int made = cl_edit_connect_nearby(doc, 0, 0.05);
	CHECK(made == 1 && led2->isConnected(ledPin), "the light's pin connects to the AND's output");
	cl_edit_undo(doc);
	CHECK(!led2->isConnected(ledPin), "one undo takes it off");

	printf("pages close with an undo\n");
	const int p1 = cl_document_add_page(doc);
	cl_edit_add_gate(doc, p1, "AA_TOGGLE", 0, 0);
	const size_t gatesBefore = doc->circuit.gates().size();
	CHECK(cl_document_close_page(doc, p1), "closed");
	CHECK(cl_document_page_count(doc) == 1, "one page left");
	CHECK(doc->circuit.gates().size() == gatesBefore - 1, "its gate left the circuit");
	CHECK(cl_edit_undo_is_close_page(doc), "Reopen is on offer");
	cl_edit_undo(doc);
	CHECK(cl_document_page_count(doc) == 2 && doc->page(1)->getGateList()->size() == 1, "reopened with its gate");
	CHECK(cl_document_page_to_show(doc) == 1, "and shown");
	cl_edit_redo(doc);
	CHECK(cl_document_page_count(doc) == 1, "redo closes it again");
	cl_edit_undo(doc);
	cl_document_rename_page(doc, 1, "Second");
	cl_document_move_page(doc, 1, 0);
	CHECK(std::string(cl_document_page_name(doc, 0)) == "Second", "moved to the front");
	cl_document_move_page(doc, 0, 1);

	printf("memory\n");
	cl_edit_add_gate(doc, 0, "AA_RAM_4x4", -20, 0);
	const long ram = (long)newest(doc, 0)->getID();
	int ab = 0, db = 0;
	CHECK(cl_ram_info(doc, ram, &ab, &db) && ab == 4 && db == 4, "a 4x4 RAM");
	CHECK(!cl_ram_info(doc, (long)s1->getID(), nullptr, nullptr), "a switch isn't memory");
	cl_ram_set(doc, ram, 3, 9);
	for (int i = 0; i < 5; i++) cl_document_tick(doc, 50);
	printf("  address 3 holds %lu\n", cl_ram_value(doc, ram, 3));
	CHECK(cl_ram_value(doc, ram, 3) == 9, "a word written reads back");

	printf("page ids follow their pages\n");
	{
		const unsigned long long id0 = cl_document_page_id(doc, 0), id1 = cl_document_page_id(doc, 1);
		CHECK(id0 != 0 && id1 != 0 && id0 != id1, "each page has its own id");
		CHECK(cl_document_page_index(doc, id1) == 1, "an id finds its page");
		cl_document_move_page(doc, 1, 0);
		CHECK(cl_document_page_index(doc, id1) == 0 && cl_document_page_index(doc, id0) == 1, "moving a tab moves its id with it");
		cl_document_move_page(doc, 1, 0);
		CHECK(cl_document_page_index(doc, 123) == -1, "an unknown id finds nothing");
	}

	printf("split view: a gesture stays on its own page\n");
	{
		CHECK(cl_edit_add_gate(doc, 1, "AA_AND2", 0, 0), "a gate on the second page");
		guiGate* g = newest(doc, 1);
		CHECK(cl_edit_float_begin(doc, 1, 0, 0), "it floats there");
		cl_edit_hover(doc, 1, 3, 3, 0.05);
		cl_edit_hover(doc, 0, 40, 40, 0.05);   // the pointer over the other side
		float gx, gy;
		g->getGLcoords(gx, gy);
		CHECK(std::fabs(gx - 3) < 1e-3 && std::fabs(gy - 3) < 1e-3, "the other side's pointer doesn't move it");
		cl_edit_press(doc, 0, 40, 40, 0, 0.05);
		cl_edit_release(doc, 40, 40);
		g->getGLcoords(gx, gy);
		CHECK(!cl_edit_is_floating(doc), "a click on the other side puts it down");
		CHECK(std::fabs(gx - 3) < 1e-3 && std::fabs(gy - 3) < 1e-3, "where it last was, on its own page");

		cl_edit_add_gate(doc, 1, "GA_LED", 12, 0);
		guiGate* l = newest(doc, 1);
		float ax, ay, bx, by;
		std::string apin, bpin;
		pin(g, "OUT", ax, ay, &apin);
		pin(l, "", bx, by, &bpin);
		cl_edit_press(doc, 1, ax, ay, 0, 0.05);
		cl_edit_drag(doc, (ax + bx) / 2, ay + 2);
		CHECK(!doc->hoverPin, "no box between pins");
		cl_edit_drag(doc, bx, by);
		CHECK(doc->hoverPin && doc->hoverPage == 1, "the pin a wire would join is boxed, on its page");
		cl_edit_release(doc, bx, by);
		CHECK(g->isConnected(apin), "and the wire joins it");
	}

	printf("C while a gate floats: connects and keeps it moving\n");
	{
		cl_edit_add_gate(doc, 1, "AA_TOGGLE", 30, 0);
		guiGate* sw = newest(doc, 1);
		float ox, oy;
		std::string opin;
		pin(sw, "OUT", ox, oy, &opin);
		cl_edit_add_gate(doc, 1, "GA_LED", 40, 0);
		guiGate* led2 = newest(doc, 1);
		// Float the light so its pin sits right on the switch's output.
		float lx, ly, px, py;
		led2->getGLcoords(lx, ly);
		std::string lpin;
		pin(led2, "", px, py, &lpin);
		CHECK(cl_edit_float_begin(doc, 1, lx, ly), "the light floats");
		cl_edit_hover(doc, 1, lx + (ox - px), ly + (oy - py), 0.05);
		const int u0 = cl_edit_undo_count(doc);
		CHECK(cl_edit_connect_while_moving(doc, 1, 0.05) == 1, "C connects it");
		CHECK(cl_edit_is_floating(doc) && sw->isConnected(opin), "connected, and still on the pointer");
		CHECK(cl_edit_take_back_connects(doc) == 1 && !sw->isConnected(opin), "Escape takes back just the connection");
		CHECK(cl_edit_is_floating(doc), "and it's still moving");
		cl_edit_connect_while_moving(doc, 1, 0.05);
		cl_edit_hover(doc, 1, lx + (ox - px) + 3, ly + (oy - py) + 2, 0.05);
		CHECK(sw->isConnected(opin), "the wire follows it as it moves on");
		cl_edit_press(doc, 1, lx + (ox - px) + 3, ly + (oy - py) + 2, 0, 0.05);
		cl_edit_release(doc, lx + (ox - px) + 3, ly + (oy - py) + 2);
		CHECK(!cl_edit_is_floating(doc) && sw->isConnected(opin), "dropped, still connected");
		CHECK(cl_edit_undo_count(doc) > u0, "the drop is on the undo stack");
		cl_edit_undo(doc);
		float nx, ny;
		led2->getGLcoords(nx, ny);
		CHECK(!sw->isConnected(opin) && std::fabs(nx - (lx + (ox - px) + 3)) < 1e-3, "the first undo takes back the connection, not the move");
	}

	printf("a paste dropped where it already sits\n");
	{
		// Duplicating onto the very spot moves the copy by nothing; that
		// used to install an empty wire shape and crash.
		cl_edit_select_all(doc, 0);
		float minX = 1e9f, maxY = -1e9f;
		for (auto& g : *doc->page(0)->getGateList()) {
			float gx, gy;
			g.second->getGLcoords(gx, gy);
			minX = std::min(minX, gx);
			maxY = std::max(maxY, gy);
		}
		const std::string copied = cl_edit_copy(doc, 0);
		const size_t wiresBefore = doc->page(0)->getWireList()->size();
		const char* back = nullptr;
		const bool pasted = cl_edit_paste(doc, 0, copied.c_str(), minX, maxY, false, &back);
		CHECK(pasted && doc->page(0)->getWireList()->size() == 2 * wiresBefore, "pasted in place, wires and all");
		bool shaped = true;
		for (auto& w : *doc->page(0)->getWireList()) shaped = shaped && !w.second->getSegmentMap().empty();
		CHECK(shaped, "every wire keeps its shape");
		cl_edit_undo(doc);
		CHECK(doc->page(0)->getWireList()->size() == wiresBefore, "undo takes the paste back");
	}

	printf("save round trip keeps it all\n");
	std::string text = cl_document_save_text(doc);
	char err[256];
	CLDocument* again = cl_document_open_text(text.c_str(), (long)text.size(), err, sizeof err);
	CHECK(again && cl_document_page_count(again) == 2, "reopens with both pages");
	if (again) cl_document_close(again);

	cl_document_close(doc);
	printf(fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails ? 1 : 0;
}
