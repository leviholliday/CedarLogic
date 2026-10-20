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
#include <cstring>
#include <string>
#include <vector>

static int fails = 0;
static char err0[256];
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
		CHECK(cl_document_page_index(doc, 1ULL << 60) == -1, "an unknown id finds nothing");
	}

	printf("Redo of a tab close after the tabs moved closes that same tab\n");
	{
		CLDocument* d = cl_document_new();
		cl_document_add_page(d);
		cl_document_add_page(d);
		cl_document_rename_page(d, 0, "A");
		cl_document_rename_page(d, 1, "B");
		cl_document_rename_page(d, 2, "C");
		auto names = [&] { std::string n; for (int i = 0; i < cl_document_page_count(d); i++) n += cl_document_page_name(d, i); return n; };
		CHECK(cl_document_close_page(d, 1) && names() == "AC", "B closed");
		cl_edit_undo(d);
		CHECK(names() == "ABC", "Undo: B is back in its place");
		cl_document_move_page(d, 1, 0);
		CHECK(names() == "BAC", "B moved to the front");
		cl_edit_redo(d);
		CHECK(names() == "AC", "Redo closes B again, not the tab now where it was");
		cl_edit_undo(d);
		CHECK(names() == "BAC", "and Undo brings it back where it was closed from");
		// A page's id is never another's, even one made after a page was freed.
		const unsigned long long gone = cl_document_page_id(d, 2);
		cl_document_delete_page(d, 2);
		const int fresh = cl_document_add_page(d);
		CHECK(cl_document_page_id(d, fresh) != gone && cl_document_page_index(d, gone) == -1, "a new page never takes a freed page's id");
		cl_document_close(d);
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

	printf("a wire C made mid-move: Undo then Redo give it the shape it was dropped with\n");
	{
		// C joins the light to the switch where it floats; it moves on, the
		// wire stretching after it, and is dropped. Redo of the connection
		// routed the wire afresh from the pins, not the shape it was dropped with.
		CLDocument* d = cl_document_new();
		cl_edit_add_gate(d, 0, "AA_TOGGLE", 0, 0);
		guiGate* sw = newest(d, 0);
		float ox, oy;
		std::string opin;
		pin(sw, "OUT", ox, oy, &opin);
		cl_edit_add_gate(d, 0, "GA_LED", 10, 0);
		guiGate* led = newest(d, 0);
		float lx, ly, px, py;
		led->getGLcoords(lx, ly);
		pin(led, "", px, py);
		CHECK(cl_edit_float_begin(d, 0, lx, ly), "the light floats");
		const float ax = lx + (ox - px) + 1, ay = ly + (oy - py);
		cl_edit_hover(d, 0, ax, ay, 0.05);
		CHECK(cl_edit_connect_while_moving(d, 0, 0.05) == 1, "C connects it");
		cl_edit_hover(d, 0, ax + 6, ay - 9, 0.05);
		cl_edit_press(d, 0, ax + 6, ay - 9, 0, 0.05);
		cl_edit_release(d, ax + 6, ay - 9);
		CHECK(sw->isConnected(opin), "dropped, connected");
		const std::string dropped = cl_document_save_text(d);
		cl_edit_undo(d);
		CHECK(!sw->isConnected(opin), "Undo takes back the connection");
		cl_edit_redo(d);
		CHECK(std::string(cl_document_save_text(d)) == dropped, "Redo gives the wire the shape it was dropped with");
		cl_edit_undo(d);
		cl_edit_undo(d);
		cl_edit_redo(d);
		cl_edit_redo(d);
		CHECK(std::string(cl_document_save_text(d)) == dropped, "and so do two Undos and two Redos");
		cl_document_close(d);
	}

	printf("a part dragged away and back to where it started leaves its wires as they were\n");
	{
		// The wire stretched with the part and kept a jog of no length where it
		// bent; the next open merged that away, so Save, Open, Save changed the file.
		CLDocument* d = cl_document_new();
		cl_edit_add_gate(d, 0, "AA_TOGGLE", 0, 0);
		guiGate* sw = newest(d, 0);
		cl_edit_add_gate(d, 0, "GA_LED", 10, 0);
		guiGate* led = newest(d, 0);
		wire(d, sw, "OUT", led, "");
		cl_edit_select_none(d, 0);
		const std::string before = cl_document_save_text(d);
		const int u0 = cl_edit_undo_count(d);
		float gx, gy;
		led->getGLcoords(gx, gy);
		cl_edit_press(d, 0, gx, gy, 0, 0.05);
		cl_edit_drag(d, gx, gy + 1);
		cl_edit_drag(d, gx + 1, gy + 2);
		cl_edit_drag(d, gx, gy);
		cl_edit_release(d, gx, gy);
		const std::string after = cl_document_save_text(d);
		CHECK(cl_edit_undo_count(d) == u0, "no move recorded");
		CHECK(after == before, "the wire is as it was");
		char e[256];
		CLDocument* back = cl_document_open_text(after.c_str(), (long)after.size(), e, sizeof e);
		CHECK(back && std::string(cl_document_save_text(back)) == after, "Save, Open, Save: the same");
		if (back) cl_document_close(back);
		// The same for a part floated away and back, then joined with C to a
		// switch whose pin is just above its own, and dropped where it began:
		// the new wire (one upright run) is tidied as a drop anywhere else
		// tidies it, not left with a jog of no length at the light's pin.
		cl_edit_add_gate(d, 0, "AA_TOGGLE", 0, -20);
		guiGate* sw2 = newest(d, 0);
		float ox, oy, lx, ly, px, py;
		pin(sw2, "OUT", ox, oy);
		led->getHotspotCoords(led->getHotspotList().begin()->first, px, py);   // the light's pin, from the light already there
		float l0x, l0y;
		led->getGLcoords(l0x, l0y);
		cl_edit_add_gate(d, 0, "GA_LED", ox + (l0x - px) + 0, oy + (l0y - py) - 0.5f);
		guiGate* led2 = newest(d, 0);
		led2->getGLcoords(lx, ly);
		const int u1 = cl_edit_undo_count(d);
		CHECK(cl_edit_float_begin(d, 0, lx, ly), "a second light floats");
		cl_edit_hover(d, 0, lx + 3, ly - 2, 0.05);
		cl_edit_hover(d, 0, lx, ly, 0.05);
		CHECK(cl_edit_connect_while_moving(d, 0, 0.05) == 1, "C joins it to the other switch");
		cl_edit_press(d, 0, lx, ly, 0, 0.05);
		cl_edit_release(d, lx, ly);
		CHECK(cl_edit_undo_count(d) == u1 + 1 && !cl_edit_is_floating(d), "dropped where it began: only the connection recorded");
		const std::string joined = cl_document_save_text(d);
		back = cl_document_open_text(joined.c_str(), (long)joined.size(), e, sizeof e);
		CHECK(back && std::string(cl_document_save_text(back)) == joined, "Save, Open, Save: the same");
		if (back) cl_document_close(back);
		cl_document_close(d);
	}

	printf("a damaged file whose wire lost its id opens with a notice, not a crash\n");
	{
		// "(id" where "(ids" belongs (one damaged byte): connecting the gates'
		// pins made a wire from an empty id list and read past it.
		const char* text =
			"(cedarlogic\n  (version 3)\n  (generator \"CedarLogic Online\")\n  (page 0\n"
			"    (gate \"AA_TOGGLE\"\n      (uuid \"1\")\n      (at 6 -8)\n      (angle 0))\n"
			"    (gate \"GA_LED\"\n      (uuid \"2\")\n      (at 14 -8)\n      (angle 0))\n"
			"    (gate \"GA_LED\"\n      (uuid \"3\")\n      (at 14 -4)\n      (angle 0))\n"
			"    (wire\n      (id \"10\")\n      (seg \"0\" h\n        (pts 8 -8 13 -8)\n"
			"        (connect \"1\" \"OUT_0\")\n        (connect \"2\" \"N_in0\")))\n"
			"    (wire\n      (ids \"11\")\n      (seg \"0\" h\n        (pts 8 -4 13 -4)\n"
			"        (connect \"3\" \"N_in0\")))))\n";
		char e[256];
		CLDocument* d = cl_document_open_text(text, (long)strlen(text), e, sizeof e);
		CHECK(d != nullptr, "it opens");
		if (d) {
			bool said = false;
			for (int i = 0; i < cl_document_notice_count(d); i++) if (strstr(cl_document_notice(d, i), "no id")) said = true;
			CHECK(said, "with a notice that a wire carries no id");
			CHECK(d->page(0)->getGateList()->size() == 3, "its parts are all there");
			const std::string saved = cl_document_save_text(d);
			CLDocument* back = cl_document_open_text(saved.c_str(), (long)saved.size(), e, sizeof e);
			CHECK(back && std::string(cl_document_save_text(back)) == saved, "and saves, opens and saves the same");
			if (back) cl_document_close(back);
			cl_document_close(d);
		}
	}

	// Every wire's pins are on gates still on its page (what a save writes).
	auto wiresHoldLiveGates = [&](CLDocument* d) {
		for (int p = 0; p < cl_document_page_count(d); p++)
			for (auto& w : *d->page(p)->getWireList()) {
				if (!w.second) continue;
				for (auto& c : w.second->getConnections()) {
					auto it = d->page(p)->getGateList()->find(c.gid);
					if (it == d->page(p)->getGateList()->end() || !it->second) return false;
				}
				for (auto& s : w.second->getSegmentMap())
					for (auto& c : s.second.connections) {
						auto it = d->page(p)->getGateList()->find(c.gid);
						if (it == d->page(p)->getGateList()->end() || !it->second) return false;
					}
			}
		return true;
	};

	printf("Undo while a paste follows the pointer takes the paste back\n");
	{
		CLDocument* d = cl_document_new();
		cl_edit_add_gate(d, 0, "AA_TOGGLE", 0, 0);
		guiGate* sw = newest(d, 0);
		cl_edit_add_gate(d, 0, "GA_LED", 10, 0);
		guiGate* led = newest(d, 0);
		cl_edit_select_all(d, 0);
		std::string block = cl_edit_copy(d, 0);
		cl_edit_select_none(d, 0);
		const int u0 = cl_edit_undo_count(d);
		const char* back = nullptr;
		CHECK(cl_edit_paste(d, 0, block.c_str(), 0, 3, true, &back), "pasted");
		CHECK(cl_edit_float_begin(d, 0, 0, 3), "it follows the pointer");
		// Bring the pasted light's pin onto the first switch's output and press C.
		float sx, sy, lx, ly;
		std::string spin, lpin;
		pin(sw, "OUT", sx, sy, &spin);
		guiGate* pastedLed = newest(d, 0);
		pin(pastedLed, "", lx, ly, &lpin);
		cl_edit_hover(d, 0, 0 + (sx - lx), 3 + (sy - ly), 0.05);
		CHECK(cl_edit_connect_while_moving(d, 0, 0.05) >= 1 && sw->isConnected(spin), "C connects it while it floats");
		cl_edit_undo(d);   // Cmd-Z instead of Escape
		CHECK(!cl_edit_is_floating(d), "it no longer follows the pointer");
		CHECK(d->page(0)->getGateList()->size() == 2 && cl_edit_undo_count(d) == u0, "the paste is gone, and only it");
		CHECK(!sw->isConnected(spin), "and so is the connection C made");
		cl_edit_press(d, 0, 5, 5, 0, 0.05);   // the next click
		cl_edit_release(d, 5, 5);
		CHECK(wiresHoldLiveGates(d) && cl_edit_undo_count(d) == u0, "the next click records nothing");
		cl_edit_redo(d);
		CHECK(d->page(0)->getGateList()->size() == 4 && wiresHoldLiveGates(d), "Redo brings the paste back");
		(void)led;
		cl_document_close(d);
	}

	printf("an edit while a paste floats puts the paste down first\n");
	{
		CLDocument* d = cl_document_new();
		cl_edit_add_gate(d, 0, "AA_AND2", 0, 0);
		guiGate* g = newest(d, 0);
		const int u0 = cl_edit_undo_count(d);
		CHECK(cl_edit_float_begin(d, 0, 0, 0), "it floats");
		cl_edit_hover(d, 0, 4, 2, 0.05);
		cl_edit_delete(d, 0);   // the Delete key
		CHECK(!cl_edit_is_floating(d) && d->page(0)->getGateList()->empty(), "dropped, then deleted");
		cl_edit_undo(d);
		float gx, gy;
		g = newest(d, 0);
		if (g) g->getGLcoords(gx, gy);
		CHECK(g && std::fabs(gx - 4) < 1e-3 && std::fabs(gy - 2) < 1e-3, "Undo brings it back where it was dropped");
		cl_edit_undo(d);
		g = newest(d, 0);
		if (g) g->getGLcoords(gx, gy);
		CHECK(g && cl_edit_undo_count(d) == u0 && std::fabs(gx) < 1e-3 && std::fabs(gy) < 1e-3, "and the next Undo takes back the move");
		cl_document_close(d);
	}

	printf("a press after a drag whose release never came keeps the drag\n");
	{
		CLDocument* d = cl_document_new();
		cl_edit_add_gate(d, 0, "AA_AND2", 0, 0);
		guiGate* g = newest(d, 0);
		const int u0 = cl_edit_undo_count(d);
		float gx, gy;
		g->getGLcoords(gx, gy);
		cl_edit_press(d, 0, gx, gy, 0, 0.05);
		cl_edit_drag(d, gx + 3, gy + 2);
		cl_edit_drag(d, gx + 6, gy + 4);
		cl_edit_press(d, 0, 30, 30, 0, 0.05);   // no release for the drag
		cl_edit_release(d, 30, 30);
		float nx, ny;
		g->getGLcoords(nx, ny);
		CHECK(std::fabs(nx - gx - 6) < 1e-3 && cl_edit_undo_count(d) == u0 + 1, "the move stays, as one undo step");
		cl_edit_undo(d);
		g = newest(d, 0);
		if (g) g->getGLcoords(nx, ny);
		CHECK(g && std::fabs(nx - gx) < 1e-3 && std::fabs(ny - gy) < 1e-3, "and Undo takes it back");
		cl_document_close(d);
	}

	printf("renaming a wired TO saves the same after a reopen\n");
	{
		CLDocument* d = cl_document_new();
		cl_edit_add_gate(d, 0, "AA_TOGGLE", 0, 0);
		guiGate* sw = newest(d, 0);
		cl_edit_add_gate(d, 0, "DE_TO", 8, 0);
		guiGate* to = newest(d, 0);
		wire(d, sw, "OUT", to, "");
		CHECK(cl_gate_set_setting(d, (long)to->getID(), "JUNCTION_ID", "Sum"), "renamed");
		std::string a = cl_document_save_text(d);
		CLDocument* r = cl_document_open_text(a.c_str(), (long)a.size(), err0, sizeof err0);
		CHECK(r && std::string(cl_document_save_text(r)) == a, "save, reopen, save: the same text");
		if (r) cl_document_close(r);
		cl_edit_undo(d);
		a = cl_document_save_text(d);
		r = cl_document_open_text(a.c_str(), (long)a.size(), err0, sizeof err0);
		CHECK(r && std::string(cl_document_save_text(r)) == a, "and after Undo of the rename");
		if (r) cl_document_close(r);
		cl_document_close(d);
	}

	printf("Undo of a delete or a Disconnect gives the wires back their bends\n");
	{
		CLDocument* d = cl_document_new();
		cl_edit_add_gate(d, 0, "AA_TOGGLE", 0, 0);
		guiGate* sw = newest(d, 0);
		cl_edit_add_gate(d, 0, "AA_AND2", 12, 6);
		guiGate* a1 = newest(d, 0);
		const unsigned long a1id = a1->getID();
		cl_edit_add_gate(d, 0, "AA_AND2", 12, -6);
		guiGate* a2 = newest(d, 0);
		wire(d, sw, "OUT", a1, "IN");
		guiWire* w = nullptr;
		for (auto& e : *d->page(0)->getWireList()) if (e.second) w = e.second;
		auto run = [&](bool vertical, float& rx, float& ry) {
			for (auto& s : w->getSegmentMap()) {
				const wireSegment& g = s.second;
				const float len = vertical ? g.end.y - g.begin.y : g.end.x - g.begin.x;
				if (g.isVertical() == vertical && len > 1) { rx = (g.begin.x + g.end.x) / 2; ry = (g.begin.y + g.end.y) / 2; return true; }
			}
			return false;
		};
		float vx = 0, vy = 0;
		CHECK(w && run(true, vx, vy), "a wire with a vertical run");
		// Give it a bend of the student's own: drag that run to the left.
		cl_edit_select_none(d, 0);
		cl_edit_press(d, 0, vx, vy, 0, 0.05);
		cl_edit_drag(d, vx - 1.5f, vy);
		cl_edit_drag(d, vx - 3, vy);
		cl_edit_release(d, vx - 3, vy);
		// And the second AND onto it: a wire with three pins.
		float ix, iy, hx = 0, hy = 0;
		std::string ipin;
		pin(a2, "IN", ix, iy, &ipin);
		CHECK(run(true, hx, hy), "the run moved");
		cl_edit_press(d, 0, ix, iy, 0, 0.05);
		cl_edit_drag(d, (ix + hx) / 2, (iy + hy) / 2);
		cl_edit_drag(d, hx, hy);
		cl_edit_release(d, hx, hy);
		CHECK(a2->isConnected(ipin) && a2->getConnection(ipin) == w, "three pins on one wire");
		const std::string shaped = cl_document_save_text(d);
		cl_edit_select_none(d, 0);
		a1->select();
		cl_edit_delete(d, 0);
		cl_edit_undo(d);
		CHECK(std::string(cl_document_save_text(d)) == shaped, "Delete a part, Undo: the wire's bends are as they were");
		float px, py;
		a2->getHotspotCoords(ipin, px, py);
		CHECK(cl_edit_context(d, 0, px, py, 0.05) == CL_CONTEXT_PIN, "right-click the wired pin");
		cl_edit_disconnect_pin(d, 0, px, py, 0.05);
		CHECK(!a2->isConnected(ipin), "Disconnect takes it off");
		cl_edit_undo(d);
		CHECK(std::string(cl_document_save_text(d)) == shaped, "Disconnect, Undo: the same");

		// Drag the first AND down past the bend (and nudge it): Undo gives the
		// bends back, Redo the shape it was dropped with.
		float sx, sy;
		a1 = newest(d, 0) ? d->circuit.getGate(a1id) : nullptr;   // the delete's Undo made it anew
		a1->getGLcoords(sx, sy);
		cl_edit_select_none(d, 0);
		cl_edit_press(d, 0, sx, sy, 0, 0.05);
		cl_edit_drag(d, sx, sy - 3);
		cl_edit_drag(d, sx, sy - 6);
		cl_edit_release(d, sx, sy - 6);
		const std::string dropped = cl_document_save_text(d);
		CHECK(dropped != shaped, "the AND moved");
		cl_edit_undo(d);
		CHECK(std::string(cl_document_save_text(d)) == shaped, "drag a part, Undo: its wire's bends are back");
		cl_edit_redo(d);
		CHECK(std::string(cl_document_save_text(d)) == dropped, "and Redo gives the shape it was dropped with");
		cl_edit_undo(d);
		cl_edit_nudge(d, 0, 0, 3);
		cl_edit_undo(d);
		CHECK(std::string(cl_document_save_text(d)) == shaped, "an arrow-key nudge, Undo: the same");
		(void)sw;
		cl_document_close(d);
	}

	printf("a wire on a RAM's data pin keeps its trunk when straightened\n");
	{
		// A RAM's DATA_IN_n and DATA_OUT_n share one spot and a wire holds
		// both, so a wire from it to one more pin has three. Routed the simple
		// way (what Straighten and Tidy Up fall back to when the page router
		// finds no way through), the third branch took the trunk's id and the
		// wire came out as two dots, one on each part.
		CLDocument* d = cl_document_new();
		cl_edit_add_gate(d, 0, "AE_RAM_8x8", 0, 0);
		guiGate* r = newest(d, 0);
		cl_edit_add_gate(d, 0, "AA_TOGGLE", -20, -15);
		guiGate* sw = newest(d, 0);
		wire(d, r, "DATA_IN_0", sw, "OUT");
		guiWire* w = r->getConnection("DATA_IN_0");
		CHECK(w && w == r->getConnection("DATA_OUT_0") && w == sw->getConnection("OUT_0") && w->getConnections().size() == 3,
			"the switch is wired to the shared spot: three pins on one wire");
		if (w) {
			w->straightenRoute();
			std::vector<wireSegment> segs;
			for (const auto& sg : w->getSegmentMap()) segs.push_back(sg.second);
			auto on = [](const wireSegment& a, float x, float y) {
				return x > std::min(a.begin.x, a.end.x) - 0.02f && x < std::max(a.begin.x, a.end.x) + 0.02f &&
				       y > std::min(a.begin.y, a.end.y) - 0.02f && y < std::max(a.begin.y, a.end.y) + 0.02f;
			};
			std::vector<int> up(segs.size());
			for (size_t i = 0; i < up.size(); i++) up[i] = (int)i;
			auto find = [&](int i) { while (up[(size_t)i] != i) i = up[(size_t)i]; return i; };
			for (size_t i = 0; i < segs.size(); i++)
				for (size_t j = i + 1; j < segs.size(); j++)
					if (on(segs[i], segs[j].begin.x, segs[j].begin.y) || on(segs[i], segs[j].end.x, segs[j].end.y) ||
					    on(segs[j], segs[i].begin.x, segs[i].begin.y) || on(segs[j], segs[i].end.x, segs[i].end.y))
						up[(size_t)find((int)i)] = find((int)j);
			int pieces = 0;
			for (size_t i = 0; i < up.size(); i++) if (find((int)i) == (int)i) pieces++;
			CHECK(pieces == 1, "the straightened wire is one piece");
			bool carried = true;
			for (const wireConnection& c : w->getConnections()) {
				float x, y;
				d->circuit.getGate(c.gid)->getHotspotCoords(c.connection, x, y);
				bool found = false;
				for (const wireSegment& sg : segs)
					for (const wireConnection& k : sg.connections)
						if (k.gid == c.gid && k.connection == c.connection && on(sg, x, y)) found = true;
				carried = carried && found;
			}
			CHECK(carried, "and every pin is on a segment that holds it");
			const std::string t = cl_document_save_text(d);
			char e[256];
			CLDocument* back = cl_document_open_text(t.c_str(), (long)t.size(), e, sizeof e);
			CHECK(back && std::string(cl_document_save_text(back)) == t, "Save, Open, Save: the same");
			if (back) cl_document_close(back);
		}
		cl_document_close(d);
	}

	printf("a wire with two pins on one part stays one piece when its parts move\n");
	{
		// Two encoders, one over the other, and one wire on IN_5 and IN_6 of
		// one and IN_1 of the other (seed 31 of the fuzzer's session sweep).
		// Moving both moves the wire whole, and then each pin moved reshapes
		// it in turn, leaving dots of no length; the dot that was the only
		// join between the run across and the run up was removed on the
		// drop, and the wire came out as two pieces: a run up the first part
		// and a dot on the second's pin.
		const char* text = R"cdl((cedarlogic
  (version 3)
  (generator "CedarLogic 4.2.0-native")
  (page 0
    (name "A")
    (gate "CC_PRI_ENCODER_8x3"
      (uuid "30")
      (at 48.5 -55.5)
      (angle 0)
      (lparam "INPUT_BITS" "8"))
    (gate "CC_PRI_ENCODER_8x3"
      (uuid "80")
      (at 47.5 -59.5)
      (angle 0)
      (lparam "INPUT_BITS" "8"))
    (wire
      (ids "58")
      (seg "0" h
        (pts 44.5 -59 45.5 -59)
        (connect "80" "IN_5")
        (connect "30" "IN_1")
        (cross 44.5 "2"))
      (seg "2" v
        (pts 44.5 -59 44.5 -58)
        (connect "80" "IN_6")
        (cross -59 "0")))
))
)cdl";
		auto onePiece = [](guiWire* w) {
			std::vector<wireSegment> segs;
			for (const auto& sg : w->getSegmentMap()) segs.push_back(sg.second);
			auto touch = [](const wireSegment& a, const wireSegment& b) {
				return std::min(a.end.x, b.end.x) + 0.05f >= std::max(a.begin.x, b.begin.x) &&
				       std::min(a.end.y, b.end.y) + 0.05f >= std::max(a.begin.y, b.begin.y);
			};
			std::vector<int> up(segs.size());
			for (size_t i = 0; i < up.size(); i++) up[i] = (int)i;
			auto find = [&](int i) { while (up[(size_t)i] != i) i = up[(size_t)i]; return i; };
			for (size_t i = 0; i < segs.size(); i++)
				for (size_t j = i + 1; j < segs.size(); j++)
					if (touch(segs[i], segs[j])) up[(size_t)find((int)i)] = find((int)j);
			int pieces = 0;
			for (size_t i = 0; i < up.size(); i++) if (find((int)i) == (int)i) pieces++;
			return pieces == 1;
		};
		auto allOnePiece = [&](CLDocument* d) {
			bool ok = true;
			for (auto& w : *d->page(0)->getWireList()) if (w.second) ok = ok && onePiece(w.second);
			return ok;
		};
		char e[256];
		CLDocument* d = cl_document_open_text(text, (long)strlen(text), e, sizeof e);
		CHECK(d && d->page(0)->getWireList()->size() == 1 && allOnePiece(d), "the two parts and their wire open");
		if (d) {
			// Float a copy of both and drop it.
			cl_edit_select_all(d, 0);
			const std::string copied = cl_edit_copy(d, 0);
			const char* back = nullptr;
			CHECK(cl_edit_paste(d, 0, copied.c_str(), 0, 0, true, &back) && cl_edit_float_begin(d, 0, 0, 0), "a copy floats");
			cl_edit_hover(d, 0, 1, -2.5, 0.05);
			cl_edit_press(d, 0, 1, -2.5, 0, 0.05);
			cl_edit_release(d, 1, -2.5);
			CHECK(!cl_edit_is_floating(d) && d->page(0)->getWireList()->size() == 2, "and drops");
			CHECK(allOnePiece(d), "the dropped copy's wire is one piece");
			cl_edit_undo(d);
			// Drag both parts.
			cl_edit_select_all(d, 0);
			cl_edit_press(d, 0, 47.5, -59.5, 0, 0.05);
			cl_edit_drag(d, 48, -61);
			cl_edit_drag(d, 48.5, -62);
			cl_edit_release(d, 48.5, -62);
			CHECK(allOnePiece(d), "dragging both parts: the wire is one piece");
			cl_document_close(d);
		}
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
