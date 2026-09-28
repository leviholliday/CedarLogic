// Headless check of the native renderer: draw one page of a .cdl to a PNG
// through CedarCore, fitted to the image.
//   render_png <cl_gatedefs.xml> <in.cdl> <page> <out.png> [width height]
// With CL_RENDER_FITTED=light|dark|print it draws through the export path
// (cl_document_draw_fitted) instead.
#include "Bitmap.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

int main(int argc, char** argv) {
	if (argc < 5) { fprintf(stderr, "usage: render_png lib.xml in.cdl page out.png [w h]\n"); return 2; }
	if (!cl_library_load(argv[1])) { fprintf(stderr, "library failed\n"); return 1; }
	char err[512];
	CLDocument* doc = cl_document_open(argv[2], err, sizeof err);
	if (!doc) { fprintf(stderr, "open failed: %s\n", err); return 1; }
	const int page = atoi(argv[3]);
	const int W = argc > 6 ? atoi(argv[5]) : 1400, H = argc > 6 ? atoi(argv[6]) : 1000;
	const double sf = 2.0;   // draw as a Retina view would
	double l, b, r, t;
	if (!cl_document_page_bounds(doc, page, &l, &b, &r, &t)) { l = -10; b = -10; r = 10; t = 10; }
	const double wPts = W / sf, hPts = H / sf, pad = 2.0;
	const double upp = std::max((r - l + 2 * pad) / wPts, (t - b + 2 * pad) / hPts);
	const double ox = (l + r) / 2 - wPts * upp / 2, oy = (b + t) / 2 + hPts * upp / 2;
	Bitmap bitmap(W, H);
	// A flipped view in points: y down, scaled by the backing factor.
	bitmap.flip(sf);
	CLContext ctx = bitmap.ctx();
	if (const char* fitted = getenv("CL_RENDER_FITTED")) {
		const int style = !strcmp(fitted, "print") ? CL_STYLE_PRINT : !strcmp(fitted, "dark") ? CL_STYLE_DARK : CL_STYLE_LIGHT;
		if (!cl_document_draw_fitted(doc, page, ctx, wPts, hPts, 12, sf, style)) printf("empty page\n");
	} else {
		cl_document_draw(doc, page, ctx, sf, ox, oy, upp, false);
	}
	const bool ok = bitmap.savePng(argv[4]);
	printf("pages=%d page=%d ok=%d\n", cl_document_page_count(doc), page, ok);
	cl_document_close(doc);
	return ok ? 0 : 1;
}
