// Headless check of parts locked in place: locking and unlocking (and their
// undo), what's saved (only (gparam "LOCKED" "true"), in v3, v2 and v1.x) and
// read back, moves, nudges, deletes, rotates and cuts that leave locked parts
// put, wiring to them, switches that still flip, pasted copies that come
// unlocked, Tidy Up and Straighten keeping them where they are, and the lock
// badge drawn only on the canvas being edited.
//   lock_check <cl_gatedefs.xml> [out.png]
#include "DocumentImpl.h"
#include "guiGate.h"
#include "guiWire.h"
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <CoreServices/CoreServices.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(c, what) do { if (c) printf("  ok   %s\n", what); else { printf("  FAIL %s\n", what); fails++; } } while (0)

static const double upp = 0.05;

static guiGate* newest(CLDocument* doc, int page) {
	guiGate* best = nullptr;
	for (auto& g : *doc->page(page)->getGateList())
		if (g.second && (best == nullptr || g.first > best->getID())) best = g.second;
	return best;
}

static guiGate* add(CLDocument* doc, int page, const char* name, double x, double y) {
	if (!cl_edit_add_gate(doc, page, name, x, y)) return nullptr;
	return newest(doc, page);
}

static bool pinAt(guiGate* g, const std::string& name, float& x, float& y) {
	auto hs = g->getHotspotList();
	if (hs.find(name) == hs.end()) return false;
	g->getHotspotCoords(name, x, y);
	return true;
}

// A wire dragged from one pin to another, as the canvas makes it.
static bool wire(CLDocument* doc, guiGate* a, const std::string& ap, guiGate* b, const std::string& bp) {
	float ax, ay, bx, by;
	if (!pinAt(a, ap, ax, ay) || !pinAt(b, bp, bx, by)) return false;
	cl_edit_press(doc, 0, ax, ay, 0, upp);
	cl_edit_drag(doc, (ax + bx) / 2, (ay + by) / 2);
	cl_edit_drag(doc, bx, by);
	cl_edit_release(doc, bx, by);
	return a->isConnected(ap) && b->isConnected(bp);
}

static void select(CLDocument* doc, int page, std::vector<guiGate*> gates) {
	cl_edit_select_none(doc, page);
	for (guiGate* g : gates) g->select();
}

struct Pos { float x, y; };
static Pos at(guiGate* g) { Pos p; g->getGLcoords(p.x, p.y); return p; }
static bool same(Pos a, Pos b) { return std::fabs(a.x - b.x) < 1e-4 && std::fabs(a.y - b.y) < 1e-4; }

static int count(const std::string& text, const std::string& what) {
	int n = 0;
	for (size_t i = text.find(what); i != std::string::npos; i = text.find(what, i + 1)) n++;
	return n;
}

static std::string readFile(const char* path) {
	std::ifstream in(path, std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

static guiGate* byLibName(CLDocument* doc, int page, const char* name, int nth = 0) {
	std::vector<unsigned long> ids;
	for (auto& g : *doc->page(page)->getGateList())
		if (g.second && g.second->getLibraryGateName() == name) ids.push_back(g.first);
	std::sort(ids.begin(), ids.end());
	return nth < (int)ids.size() ? doc->circuit.getGate(ids[(size_t)nth]) : nullptr;
}

// Draws a page as the canvas does (or as a picture, or Simulation View) into
// a bitmap; returns how many pixels differ from a drawing of the same with
// nothing locked, so the badge is what's counted.
static CGContextRef drawPage(CLDocument* doc, int W, int H, double ox, double oy, double scaleUpp,
                             bool showSelection, bool simView) {
	CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
	CGContextRef ctx = CGBitmapContextCreate(nullptr, W, H, 8, 0, cs, kCGImageAlphaPremultipliedLast);
	CGColorSpaceRelease(cs);
	CGContextSetRGBFillColor(ctx, 1, 1, 1, 1);
	CGContextFillRect(ctx, CGRectMake(0, 0, W, H));
	CGContextTranslateCTM(ctx, 0, H);
	CGContextScaleCTM(ctx, 2, -2);
	CLDrawOptions o = { false, 0, 1.0, simView, false, showSelection, 1.0 };
	cl_document_draw_ex(doc, 0, ctx, 2, ox, oy, scaleUpp, &o);
	return ctx;
}

static int diff(CGContextRef a, CGContextRef b, int W, int H) {
	const unsigned char* pa = (const unsigned char*)CGBitmapContextGetData(a);
	const unsigned char* pb = (const unsigned char*)CGBitmapContextGetData(b);
	const size_t rowA = CGBitmapContextGetBytesPerRow(a), rowB = CGBitmapContextGetBytesPerRow(b);
	int n = 0;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
			if (memcmp(pa + y * rowA + x * 4, pb + y * rowB + x * 4, 4) != 0) n++;
	return n;
}

int main(int argc, char** argv) {
	setvbuf(stdout, nullptr, _IONBF, 0);
	if (argc < 2 || !cl_library_load(argv[1])) return 2;
	const char* pngPath = argc > 2 ? argv[2] : "/private/tmp/lock_check_badge.png";
	CLDocument* doc = cl_document_new();
	cl_document_set_running(doc, true);

	// Two switches into an AND, into a light.
	guiGate* s1 = add(doc, 0, "AA_TOGGLE", 0, 0);
	guiGate* s2 = add(doc, 0, "AA_TOGGLE", 0, -6);
	guiGate* andg = add(doc, 0, "AA_AND2", 10, -3);
	guiGate* led = add(doc, 0, "GA_LED", 20, -3);
	if (!s1 || !s2 || !andg || !led) { printf("couldn't add the parts\n"); return 1; }
	cl_edit_select_none(doc, 0);
	const std::string unlockedText = cl_document_save_text(doc);

	printf("lock in place\n");
	select(doc, 0, { s1, s2, led });
	CHECK(cl_edit_selected_locked_count(doc, 0, false) == 3, "three unlocked parts selected");
	const int undo0 = cl_edit_undo_count(doc);
	CHECK(cl_edit_lock_selection(doc, 0, true) == 3, "three parts locked");
	CHECK(s1->isLocked() && s2->isLocked() && led->isLocked() && !andg->isLocked(), "the switches and the light are locked, the AND isn't");
	CHECK(cl_gate_is_locked(doc, (long)s1->getID()) && !cl_gate_is_locked(doc, (long)andg->getID()), "cl_gate_is_locked says so");
	CHECK(cl_edit_undo_count(doc) == undo0 + 1, "locking is one undo step");
	CHECK(!strcmp(cl_edit_undo_name(doc), "Lock in Place"), "named Lock in Place");
	CHECK(cl_document_locked_count(doc) == 3, "three locked on the circuit");
	CHECK(cl_edit_lock_selection(doc, 0, true) == 0, "locking them again changes nothing");
	cl_edit_undo(doc);
	CHECK(!s1->isLocked() && !s2->isLocked() && !led->isLocked(), "undo unlocks them");
	CHECK(s1->getAllGUIParams()->count("LOCKED") == 0, "undo takes the param off (no empty value left)");
	CHECK(cl_document_save_text(doc) == unlockedText, "unlocked again, it saves exactly as before");
	cl_edit_redo(doc);
	CHECK(s1->isLocked() && s2->isLocked() && led->isLocked(), "redo locks them again");

	printf("wiring to locked parts\n");
	CHECK(wire(doc, s1, "OUT_0", andg, "IN_0"), "a wire from a locked switch to the AND");
	CHECK(wire(doc, andg, "IN_1", s2, "OUT_0"), "a wire from the AND to a locked switch");
	CHECK(wire(doc, andg, "OUT", led, "N_in0"), "a wire from the AND to the locked light");
	const size_t wires0 = doc->page(0)->getWireList()->size();
	CHECK(wires0 == 3, "three wires");

	printf("switches still flip\n");
	{
		guiWire* w = s1->getConnection("OUT_0");
		for (int i = 0; i < 10; i++) cl_document_tick(doc, 50);
		const StateType before = w->getState().empty() ? UNKNOWN : w->getState()[0];
		klsBBox b = s1->getBBox();
		float sx, sy; s1->getGLcoords(sx, sy);
		cl_edit_select_none(doc, 0);
		cl_edit_press(doc, 0, sx, sy, 0, upp);
		cl_edit_release(doc, sx, sy);
		for (int i = 0; i < 10; i++) cl_document_tick(doc, 50);
		const StateType after = w->getState().empty() ? UNKNOWN : w->getState()[0];
		CHECK(before != after, "a click on a locked switch flips it");
		(void)b;
	}

	printf("save and reopen\n");
	const std::string text = cl_document_save_text(doc);
	CHECK(count(text, "(gparam \"LOCKED\" \"true\")") == 3, "v3: three (gparam \"LOCKED\" \"true\")");
	CHECK(count(text, "LOCKED") == 3, "v3: nothing else says LOCKED (no false, no empty value)");
	{
		char err[256];
		CLDocument* re = cl_document_open_text(text.data(), (long)text.size(), err, sizeof err);
		CHECK(re != nullptr, "the saved text opens");
		if (re) {
			CHECK(cl_document_locked_count(re) == 3, "reopened: three locked");
			CHECK(cl_document_notice_count(re) == 0, "reopened: no notices");
			CHECK(byLibName(re, 0, "AA_AND2") && !byLibName(re, 0, "AA_AND2")->isLocked(), "reopened: the AND isn't locked");
			CHECK(cl_document_save_text(re) == text, "reopened: saves the same text");
			cl_document_close(re);
		}
		for (int format = 2; format >= 1; format--) {
			const char* path = format == 2 ? "/private/tmp/lock_check_v2.cdl" : "/private/tmp/lock_check_v1.cdl";
			const int r = cl_document_export_legacy(doc, path, format, err, sizeof err);
			CHECK(r >= 0, format == 2 ? "exported as v2" : "exported as v1.x");
			const std::string legacy = readFile(path);
			CHECK(count(legacy, "<gparam>LOCKED true</gparam>") == 3 && count(legacy, "LOCKED") == 3,
			      format == 2 ? "v2: three <gparam>LOCKED true</gparam>, nothing else" : "v1.x: three <gparam>LOCKED true</gparam>, nothing else");
			CLDocument* back = cl_document_open(path, err, sizeof err);
			CHECK(back && cl_document_locked_count(back) == 3, format == 2 ? "v2 reopened: three locked" : "v1.x reopened: three locked");
			if (back) {
				const std::string again = cl_document_save_text(back);
				CHECK(count(again, "(gparam \"LOCKED\" \"true\")") == 3, "and saved as v3 again, still three");
				cl_document_close(back);
			}
			remove(path);
		}
	}

	printf("moving\n");
	const Pos ps1 = at(s1), ps2 = at(s2), pled = at(led), pand = at(andg);
	{
		cl_edit_select_all(doc, 0);
		klsBBox b = andg->getSelectionBBox();
		const double px = (b.getLeft() + b.getRight()) / 2, py = (b.getBottom() + b.getTop()) / 2;
		const int u = cl_edit_undo_count(doc);
		CHECK(cl_edit_press(doc, 0, px, py, 0, upp) == CL_PRESS_PART, "press on the AND (everything selected)");
		cl_edit_drag(doc, px + 1, py);
		cl_edit_drag(doc, px + 3, py - 2);
		cl_edit_release(doc, px + 3, py - 2);
		const Pos p = at(andg);
		CHECK(std::fabs(p.x - pand.x - 3) < 1e-3 && std::fabs(p.y - pand.y + 2) < 1e-3, "the AND moved by (3, -2)");
		CHECK(same(at(s1), ps1) && same(at(s2), ps2) && same(at(led), pled), "the locked parts stayed put");
		CHECK(cl_edit_locked_held(doc), "the core says locked parts were held");
		CHECK(s1->isConnected("OUT_0") && andg->isConnected("IN_0") && s1->getConnection("OUT_0") == andg->getConnection("IN_0"),
		      "the wire still joins the locked switch to the AND");
		CHECK(cl_edit_undo_count(doc) == u + 1, "one undo step");
		cl_edit_undo(doc);
		CHECK(same(at(andg), pand), "undo puts the AND back");
	}
	{
		// A drag that starts on a locked part on its own moves nothing.
		cl_edit_select_none(doc, 0);
		klsBBox b = led->getSelectionBBox();
		const double px = (b.getLeft() + b.getRight()) / 2, py = (b.getBottom() + b.getTop()) / 2;
		const int u = cl_edit_undo_count(doc);
		cl_edit_press(doc, 0, px, py, 0, upp);
		cl_edit_drag(doc, px + 2, py + 2);
		cl_edit_drag(doc, px + 4, py + 4);
		cl_edit_release(doc, px + 4, py + 4);
		CHECK(same(at(led), pled), "dragging a locked light leaves it put");
		CHECK(cl_edit_locked_held(doc), "and says so");
		CHECK(cl_edit_undo_count(doc) == u, "no undo step");
	}
	{
		// A box drawn round everything, then a move from the AND.
		cl_edit_select_none(doc, 0);
		cl_edit_press(doc, 0, -10, 10, 0, upp);
		cl_edit_drag(doc, 30, -20);
		cl_edit_release(doc, 30, -20);
		CHECK(cl_edit_selected_gate_count(doc, 0) == 4, "the box selects all four");
		klsBBox b = andg->getSelectionBBox();
		const double px = (b.getLeft() + b.getRight()) / 2, py = (b.getBottom() + b.getTop()) / 2;
		cl_edit_press(doc, 0, px, py, 0, upp);
		cl_edit_drag(doc, px, py + 3);
		cl_edit_release(doc, px, py + 3);
		CHECK(std::fabs(at(andg).y - pand.y - 3) < 1e-3, "the box move took the AND");
		CHECK(same(at(s1), ps1) && same(at(led), pled), "and left the locked parts");
		cl_edit_undo(doc);
	}
	{
		cl_edit_select_all(doc, 0);
		cl_edit_nudge(doc, 0, 1, 0);
		CHECK(std::fabs(at(andg).x - pand.x - 1) < 1e-3, "an arrow key nudges the AND");
		CHECK(same(at(s2), ps2) && same(at(led), pled), "but not the locked parts");
		CHECK(cl_edit_locked_held(doc), "and says so");
		cl_edit_undo(doc);
		select(doc, 0, { s1 });
		const int u = cl_edit_undo_count(doc);
		cl_edit_nudge(doc, 0, 1, 0);
		CHECK(same(at(s1), ps1) && cl_edit_undo_count(doc) == u, "nudging only a locked part does nothing");
	}

	printf("deleting and cutting\n");
	{
		const size_t gates0 = doc->page(0)->getGateList()->size();
		select(doc, 0, { s1 });
		const int u = cl_edit_undo_count(doc);
		cl_edit_delete(doc, 0);
		CHECK(doc->page(0)->getGateList()->size() == gates0 && cl_edit_undo_count(doc) == u, "a locked part alone isn't deleted");
		CHECK(cl_edit_locked_held(doc), "and the core says so");
		const unsigned long andId = andg->getID();
		cl_edit_select_all(doc, 0);
		cl_edit_delete(doc, 0);
		CHECK(doc->page(0)->getGateList()->size() == gates0 - 1 && doc->circuit.getGate(s1->getID()) &&
		      doc->circuit.getGate(led->getID()), "deleting everything leaves the three locked parts");
		CHECK(doc->page(0)->getWireList()->empty(), "the wires went with the AND");
		cl_edit_undo(doc);
		CHECK(doc->page(0)->getGateList()->size() == gates0 && doc->page(0)->getWireList()->size() == wires0, "undo brings them back");
		andg = doc->circuit.getGate(andId);   // undo made it anew
		// A wire on a locked part can still go.
		cl_edit_select_none(doc, 0);
		s1->getConnection("OUT_0")->select();
		cl_edit_delete(doc, 0);
		CHECK(doc->page(0)->getWireList()->size() == wires0 - 1, "a wire on a locked switch can be deleted");
		cl_edit_undo(doc);
		CHECK(doc->page(0)->getWireList()->size() == wires0, "undo puts it back");
		// Cut: the locked parts are left out first.
		cl_edit_select_all(doc, 0);
		CHECK(cl_edit_deselect_locked(doc, 0) == 3, "a cut leaves out the three locked parts");
		CHECK(cl_edit_selected_gate_count(doc, 0) == 1 && andg->isSelected(), "only the AND is left to cut");
		const std::string clip = cl_edit_copy(doc, 0);
		CHECK(clip.find("LOCKED") == std::string::npos, "the cut's clipboard has no locked parts");
	}

	printf("rotating\n");
	{
		guiGate* loose = add(doc, 0, "AA_TOGGLE", 30, 10);
		loose->setLocked(true);
		const std::string a0 = loose->getGUIParam("angle");
		select(doc, 0, { loose });
		const int u = cl_edit_undo_count(doc);
		cl_edit_rotate(doc, 0);
		CHECK(loose->getGUIParam("angle") == a0 && cl_edit_undo_count(doc) == u, "a locked part doesn't rotate");
		CHECK(cl_edit_locked_held(doc), "and the core says so");
		loose->setLocked(false);
		cl_edit_rotate(doc, 0);
		CHECK(loose->getGUIParam("angle") != a0, "unlocked, it does");
		cl_edit_undo(doc);
		cl_edit_undo(doc);   // the add
	}

	printf("copy and paste\n");
	{
		select(doc, 0, { s1 });
		const std::string clip = cl_edit_copy(doc, 0);
		CHECK(clip.find("LOCKED") != std::string::npos, "copying a locked switch is fine (the clipboard holds it)");
		const char* back = nullptr;
		CHECK(cl_edit_paste(doc, 0, clip.c_str(), 40, 20, true, &back), "pasted");
		guiGate* copy = newest(doc, 0);
		CHECK(copy != s1 && !copy->isLocked() && copy->getAllGUIParams()->count("LOCKED") == 0, "the pasted copy comes unlocked");
		cl_edit_undo(doc);
		cl_edit_redo(doc);
		copy = newest(doc, 0);
		CHECK(copy != s1 && !copy->isLocked(), "and stays unlocked after undo and redo");
		cl_edit_undo(doc);
		// A lone TO's junction name still counts up through a locked copy.
		guiGate* to = add(doc, 0, "DE_TO", 50, 0);
		CHECK(to != nullptr, "a TO added");
		if (to) {
			select(doc, 0, { to });
			cl_edit_lock_selection(doc, 0, true);
			// Settings still change on a locked part.
			CHECK(cl_gate_set_setting(doc, (long)to->getID(), "JUNCTION_ID", "sig1") &&
			      to->getLogicParam("JUNCTION_ID") == "sig1" && to->isLocked(), "a locked TO's name can still be changed");
			const std::string toClip = cl_edit_copy(doc, 0);
			const char* rewritten = nullptr;
			cl_edit_paste(doc, 0, toClip.c_str(), 55, 0, false, &rewritten);
			guiGate* pasted = newest(doc, 0);
			printf("  pasted TO: %s\n", pasted->getLogicParam("JUNCTION_ID").c_str());
			CHECK(pasted != to && !pasted->isLocked() && pasted->getLogicParam("JUNCTION_ID") == "sig2",
			      "a pasted locked TO comes unlocked, its name counting up");
			cl_edit_undo(doc);
			cl_edit_undo(doc);
			cl_edit_undo(doc);
			cl_edit_undo(doc);
		}
	}

	printf("Tidy Up and Straighten\n");
	{
		// Untidy it: the AND off to one side.
		select(doc, 0, { andg });
		cl_edit_nudge(doc, 0, 3.5, 4.5);
		const Pos a1 = at(s1), a2 = at(s2), al = at(led);
		for (int mode = 0; mode <= 1; mode++) {
			cl_edit_select_none(doc, 0);
			const bool began = cl_edit_tidy_begin(doc, 0, mode);
			CHECK(same(at(s1), a1) && same(at(s2), a2) && same(at(led), al),
			      mode == 0 ? "Tidy Up (keep layout) leaves the locked parts put" : "Tidy Up (rearrange) leaves the locked parts put");
			if (began) cl_edit_tidy_end(doc, true);
			CHECK(same(at(s1), a1) && same(at(s2), a2) && same(at(led), al), "and so does keeping it");
			printf("  mode %d: %s, the AND at (%.1f, %.1f)\n", mode, began ? "tidied" : "nothing to do", at(andg).x, at(andg).y);
			if (began) cl_edit_undo(doc);
		}
		// The same page unlocked: Tidy Up would have moved them.
		cl_edit_unlock_all(doc, 0);
		cl_edit_select_none(doc, 0);
		bool wouldMove = false;
		{
			cl_edit_tidy_begin(doc, 0, 1);
			wouldMove = !same(at(s1), a1) || !same(at(s2), a2) || !same(at(led), al);
			printf("  unlocked, rearranged: switch at (%.1f, %.1f), light at (%.1f, %.1f)\n", at(s1).x, at(s1).y, at(led).x, at(led).y);
			cl_edit_tidy_end(doc, false);
		}
		CHECK(wouldMove, "(unlocked, the same Tidy Up moves them)");
		CHECK(same(at(s1), a1) && same(at(led), al), "(and putting it back puts them back)");
		cl_edit_undo(doc);
		CHECK(s1->isLocked() && s2->isLocked() && led->isLocked(), "(locked again)");
		cl_edit_select_all(doc, 0);
		const bool began = cl_edit_tidy_begin(doc, 0, 1);
		CHECK(same(at(s1), a1) && same(at(s2), a2) && same(at(led), al), "Tidy Up of a selection with locked parts leaves them put");
		if (began) cl_edit_tidy_end(doc, false);
		cl_edit_select_all(doc, 0);
		cl_edit_straighten(doc, 0);
		CHECK(same(at(s1), a1) && same(at(s2), a2) && same(at(led), al), "Straighten leaves them put");
		CHECK(s1->getConnection("OUT_0") && s1->getConnection("OUT_0") == andg->getConnection("IN_0"), "and their wires joined");
	}

	printf("unlock all\n");
	{
		const int p2 = cl_document_add_page(doc);
		guiGate* other = add(doc, p2, "AA_TOGGLE", 0, 0);
		select(doc, p2, { other });
		cl_edit_lock_selection(doc, p2, true);
		CHECK(cl_document_locked_count(doc) == 4, "four locked across two pages");
		const int u = cl_edit_undo_count(doc);
		CHECK(cl_edit_unlock_all(doc, 0) == 4, "Unlock All Parts unlocks all four");
		CHECK(cl_document_locked_count(doc) == 0 && !other->isLocked(), "none locked, on either page");
		CHECK(cl_edit_undo_count(doc) == u + 1 && !strcmp(cl_edit_undo_name(doc), "Unlock All Parts"), "one undo step, Unlock All Parts");
		cl_edit_undo(doc);
		CHECK(cl_document_locked_count(doc) == 4 && other->isLocked() && s1->isLocked(), "undo locks them again");
		cl_edit_redo(doc);
		CHECK(cl_document_locked_count(doc) == 0, "redo unlocks them");
		cl_edit_undo(doc);
		select(doc, 0, { s1, andg });
		CHECK(cl_edit_lock_selection(doc, 0, false) == 1 && !s1->isLocked(), "Unlock on a mixed selection unlocks the locked one");
		CHECK(!strcmp(cl_edit_undo_name(doc), "Unlock Part"), "named Unlock Part");
		cl_edit_undo(doc);
		CHECK(s1->isLocked(), "undo locks it again");
	}

	printf("the lock badge\n");
	{
		double l, b, r, t;
		cl_document_page_bounds(doc, 0, &l, &b, &r, &t);
		const int W = 1000, H = 600;
		const double u = std::max((r - l + 4) / (W / 2.0), (t - b + 4) / (H / 2.0));
		const double ox = (l + r) / 2 - W / 4.0 * u, oy = (b + t) / 2 + H / 4.0 * u;
		cl_edit_select_none(doc, 0);
		CGContextRef locked = drawPage(doc, W, H, ox, oy, u, true, false);
		CGContextRef picture = drawPage(doc, W, H, ox, oy, u, false, false);
		CGContextRef sim = drawPage(doc, W, H, ox, oy, u, true, true);
		cl_edit_unlock_all(doc, 0);
		CGContextRef unlocked = drawPage(doc, W, H, ox, oy, u, true, false);
		CGContextRef pictureU = drawPage(doc, W, H, ox, oy, u, false, false);
		CGContextRef simU = drawPage(doc, W, H, ox, oy, u, true, true);
		cl_edit_undo(doc);
		const int badge = diff(locked, unlocked, W, H);
		printf("  badge pixels: canvas %d, picture %d, Simulation View %d\n", badge, diff(picture, pictureU, W, H), diff(sim, simU, W, H));
		CHECK(badge > 300, "the canvas draws a badge on locked parts");
		CHECK(diff(picture, pictureU, W, H) == 0, "a picture (no selection shown) draws none");
		CHECK(diff(sim, simU, W, H) == 0, "Simulation View draws none");
		CGImageRef img = CGBitmapContextCreateImage(locked);
		CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8*)pngPath, strlen(pngPath), false);
		CGImageDestinationRef dest = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, nullptr);
		CGImageDestinationAddImage(dest, img, nullptr);
		CHECK(CGImageDestinationFinalize(dest), "wrote the canvas picture");
		printf("  %s\n", pngPath);
		// Dark, zoomed in, too.
		CGContextRef dark;
		{
			CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
			dark = CGBitmapContextCreate(nullptr, 600, 400, 8, 0, cs, kCGImageAlphaPremultipliedLast);
			CGColorSpaceRelease(cs);
			CGContextSetRGBFillColor(dark, 0.075, 0.082, 0.098, 1);
			CGContextFillRect(dark, CGRectMake(0, 0, 600, 400));
			CGContextTranslateCTM(dark, 0, 400);
			CGContextScaleCTM(dark, 2, -2);
			float sx, sy; s1->getGLcoords(sx, sy);
			CLDrawOptions o = { true, 0, 1.0, false, false, true, 1.0 };
			cl_document_draw_ex(doc, 0, dark, 2, sx - 7.5, sy + 5, 0.05, &o);
			CGImageRef di = CGBitmapContextCreateImage(dark);
			std::string darkPath = pngPath;
			darkPath.insert(darkPath.size() - 4, "_dark");
			CFURLRef du = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8*)darkPath.c_str(), darkPath.size(), false);
			CGImageDestinationRef dd = CGImageDestinationCreateWithURL(du, CFSTR("public.png"), 1, nullptr);
			CGImageDestinationAddImage(dd, di, nullptr);
			CHECK(CGImageDestinationFinalize(dd), "wrote a dark, zoomed-in picture");
			printf("  %s\n", darkPath.c_str());
		}
	}

	cl_document_close(doc);
	printf(fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails ? 1 : 0;
}
