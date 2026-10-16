// Random-edit fuzzer for the shared engine, through the C interface.
//   fuzz_edit <cl_gatedefs.xml> <first-seed> <seed-count> <ops-per-seed> [start.cdl]
// Each seed starts from a new (or the given) document and applies random
// placements, drags, wire drawing, deletes, rotates, pastes, undo/redo, page
// changes, Straighten/Tidy, locks and simulation ticks. Build it with
// sanitizers (fuzz-check.sh) so memory errors and undefined behaviour stop it.
// After every seed the document is saved, read back and saved again: the two
// texts must match. A seed that fails prints its number, so it can be rerun.
#include "DocumentImpl.h"
#include "guiGate.h"
#include "guiWire.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

static void gesture(CLDocument* doc, int page, double x0, double y0, double x1, double y1, int mods) {
	const double upp = 0.05;
	cl_edit_press(doc, page, x0, y0, mods, upp);
	if (getenv("CL_FUZZ_TRACE")) { static const char* names[] = { "None", "Pressed", "Moving", "BoxSelect", "Connect", "WireSeg" }; printf("    press mode %s at (%.2f, %.2f), drag to (%.2f, %.2f)\n", names[(int)doc->gesture.mode], x0, y0, x1, y1); }
	int steps = 1 + ri(5);
	watchPrint(doc, "pressed");
	for (int i = 1; i <= steps; i++) { cl_edit_drag(doc, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps); watchPrint(doc, "drag"); }
	if (ri(12) == 0) cl_edit_cancel(doc); else cl_edit_release(doc, x1, y1);
	watchPrint(doc, "released");
}


// Every connection on every wire must name a gate that is still on the page.
static bool danglingConnection(CLDocument* doc, std::string& what) {
	for (int p = 0; p < cl_document_page_count(doc); p++) {
		auto* gl = doc->page(p)->getGateList();
		auto* wl = doc->page(p)->getWireList();
		if (!gl || !wl) continue;
		for (auto& w : *wl) {
			if (!w.second) continue;
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
					if (g == gl->end()) {
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

static bool oneSeed(unsigned long long seed, int ops, const char* startFile) {
	rs = seed * 2654435761ULL + 88172645463325252ULL;
	for (int i = 0; i < 5; i++) rnd();
	char err[256];
	CLDocument* doc = startFile ? cl_document_open(startFile, err, sizeof err) : cl_document_new();
	if (!doc) { printf("seed %llu: could not open: %s\n", seed, startFile ? err : "new"); return false; }
	int pages = cl_document_page_count(doc);
	for (int n = 0; n < ops; n++) {
		pages = cl_document_page_count(doc);
		int page = ri(pages);
		const int op = ri(24);
		if (getenv("CL_FUZZ_TRACE")) printf("  op %d: case %d (page %d of %d)\n", n, op, page, pages);
		switch (op) {
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
		{ std::string what; if (getenv("CL_FUZZ_INVARIANT") && danglingConnection(doc, what)) { printf("seed %llu: after op %d (case %d): %s\n", seed, n, op, what.c_str()); cl_document_close(doc); return false; } }
	}
	// Save, read back, save again: the text must be stable.
	std::string a = cl_document_save_text(doc);
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
	if (argc < 5 || !cl_library_load(argv[1])) { fprintf(stderr, "usage: fuzz_edit gatedefs first-seed count ops [start.cdl]\n"); return 2; }
	for (int c = 0; c < cl_library_category_count(); c++)
		for (int g = 0; g < cl_library_gate_count(c); g++) libGates.push_back(cl_library_gate(c, g));
	if (libGates.empty()) { fprintf(stderr, "no gates in the library\n"); return 2; }
	unsigned long long first = strtoull(argv[2], 0, 10);
	int count = atoi(argv[3]), ops = atoi(argv[4]), bad = 0;
	for (int i = 0; i < count; i++) {
		if (!oneSeed(first + i, ops, argc > 5 ? argv[5] : nullptr)) bad++;
		if ((i + 1) % 25 == 0) { printf("%d/%d seeds, %d failed\n", i + 1, count, bad); fflush(stdout); }
	}
	printf("%d seeds, %d failed\n", count, bad);
	return bad ? 1 : 0;
}
