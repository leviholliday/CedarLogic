// How long the shared engine takes per pointer move while dragging, on a real circuit.
//   bench_drag <cl_gatedefs.xml> <circuit.cdl> <page> [steps]
// Drags (a) the most-wired part and (b) everything on the page, step by step as a
// mouse would, and prints the time of each cl_edit_drag call: mean, median, 95th
// percentile and worst, in milliseconds. A step over ~16 ms means a 60 Hz screen
// can't keep up on this computer; a slower computer takes proportionally longer.
#include "DocumentImpl.h"
#include "guiGate.h"
#include "guiWire.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

static void report(const char* what, std::vector<double> t) {
	if (t.empty()) return;
	std::sort(t.begin(), t.end());
	double sum = 0;
	for (double v : t) sum += v;
	printf("  %-34s mean %7.2f  median %7.2f  p95 %7.2f  worst %7.2f ms  (%zu steps)\n", what, sum / t.size(), t[t.size() / 2],
	       t[(size_t)(t.size() * 0.95)], t.back(), t.size());
}

int main(int argc, char** argv) {
	if (argc < 4 || !cl_library_load(argv[1])) return 2;
	char err[256];
	CLDocument* doc = cl_document_open(argv[2], err, sizeof err);
	if (!doc) { printf("%s\n", err); return 1; }
	const int page = atoi(argv[3]);
	const int steps = argc > 4 ? atoi(argv[4]) : 120;
	const double upp = 0.05;
	auto* gl = doc->page(page)->getGateList();
	auto* wl = doc->page(page)->getWireList();
	printf("%s page %d: %zu parts, %zu wires\n", argv[2], page, gl->size(), wl->size());

	// The part with the most wired pins.
	guiGate* best = nullptr;
	int bestWired = -1;
	for (auto& g : *gl) {
		if (!g.second) continue;
		int wired = 0;
		for (auto& hs : g.second->getHotspotList()) if (g.second->isConnected(hs.first)) wired++;
		if (wired > bestWired) { bestWired = wired; best = g.second; }
	}
	if (!best) return 1;

	for (int mode = 0; mode < 2; mode++) {
		cl_edit_select_none(doc, page);
		if (mode == 1) cl_edit_select_all(doc, page);
		klsBBox b = best->getSelectionBBox();
		const double px = (b.getLeft() + b.getRight()) / 2, py = (b.getBottom() + b.getTop()) / 2;
		cl_edit_press(doc, page, px, py, 0, upp);
		std::vector<double> t;
		// a circle of radius 10 world units around the start, `steps` moves
		for (int i = 1; i <= steps; i++) {
			const double a = i * 6.2831853 / steps;
			const double x = px + 10 * std::sin(a), y = py + 10 * (1 - std::cos(a));
			auto t0 = Clock::now();
			cl_edit_drag(doc, x, y);
			t.push_back(ms(t0, Clock::now()));
		}
		auto r0 = Clock::now();
		cl_edit_release(doc, px, py);
		const double rel = ms(r0, Clock::now());
		report(mode == 0 ? "drag the most-wired part" : "drag everything (select all)", t);
		printf("  %-34s %7.2f ms\n", "  release (commit the move)", rel);
		cl_edit_undo(doc);
	}
	return 0;
}
