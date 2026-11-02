// Saving must not change a circuit. For each file: open, save, reopen the
// saved text, save again (text must match), and compare every page's gates,
// wires and a rendering against the original.
//   save_check <cl_gatedefs.xml> <file.cdl>...
#include "Bitmap.h"   // first: on Windows it brings the system headers
#include "DocumentImpl.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static std::vector<unsigned char> pixels(CLDocument* doc, int page) {
	const int W = 600, H = 400;
	double l, b, r, t;
	if (!cl_document_page_bounds(doc, page, &l, &b, &r, &t)) return std::vector<unsigned char>((size_t)W * H * 4, 0);
	Bitmap bitmap(W, H, false);
	bitmap.flip(1);
	const double upp = std::max((r - l + 4) / W, (t - b + 4) / H);
	cl_document_draw(doc, page, bitmap.ctx(), 1, (l + r) / 2 - W * upp / 2, (b + t) / 2 + H * upp / 2, upp, false);
	return bitmap.pixels();
}

int main(int argc, char** argv) {
	if (argc < 3 || !cl_library_load(argv[1])) return 2;
	cl_set_settle_on_open(false);   // compare the files, not a simulation's progress
	int fails = 0;
	for (int i = 2; i < argc; i++) {
		char err[256];
		CLDocument* a = cl_document_open(argv[i], err, sizeof err);
		if (!a) { printf("SKIP %s: %s\n", argv[i], err); continue; }
		const std::string t1 = cl_document_save_text(a);
		CLDocument* b = cl_document_open_text(t1.data(), (long)t1.size(), err, sizeof err);
		if (!b) { printf("FAIL %s: saved text won't open: %s\n", argv[i], err); fails++; continue; }
		const std::string t2 = cl_document_save_text(b);
		bool ok = t1 == t2;
		if (!ok) printf("  text differs on the second save\n");
		if (a->pages.size() != b->pages.size()) { ok = false; printf("  page count %zu vs %zu\n", a->pages.size(), b->pages.size()); }
		for (size_t p = 0; ok && p < a->pages.size(); p++) {
			if (a->pages[p]->getGateList()->size() != b->pages[p]->getGateList()->size() ||
			    a->pages[p]->getWireList()->size() != b->pages[p]->getWireList()->size()) {
				ok = false; printf("  page %zu: gate/wire counts differ\n", p);
			}
			if (a->pages[p]->name != b->pages[p]->name) { ok = false; printf("  page %zu: name differs\n", p); }
			if (pixels(a, (int)p) != pixels(b, (int)p)) { ok = false; printf("  page %zu: draws differently\n", p); }
		}
		printf("%s %s (%zu pages, %zu bytes)\n", ok ? "ok  " : "FAIL", argv[i], a->pages.size(), t1.size());
		if (!ok) fails++;
		cl_document_close(a); cl_document_close(b);
	}
	printf(fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails ? 1 : 0;
}
