// Headless Tidy Up: open a .cdl, tidy page 0 (mode 0 keeps the layout, 1
// rearranges, -1 leaves it alone), save the result and draw it to a PNG in
// the dark style, fitted.
//   tidy_render <cl_gatedefs.xml> <in.cdl> <mode> <out.cdl|-> <out.png|-> [width height]
#include "DocumentImpl.h"
#include "guiGate.h"
#include "klsBBox.h"
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <CoreServices/CoreServices.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
	if (argc < 6) { fprintf(stderr, "usage: tidy_render lib.xml in.cdl mode out.cdl out.png [w h]\n"); return 2; }
	if (!cl_library_load(argv[1])) { fprintf(stderr, "library failed\n"); return 1; }
	char err[512];
	cl_set_settle_on_open(false);
	CLDocument* doc = cl_document_open(argv[2], err, sizeof err);
	if (!doc) { fprintf(stderr, "open failed: %s\n", err); return 1; }
	const int mode = atoi(argv[3]);
	if (getenv("CL_TIDY_DEBUG")) {
		for (auto& e : *doc->page(0)->getGateList()) {
			guiGate* g = e.second;
			if (!g) continue;
			klsBBox b = g->getSelectionBBox();
			if (b.empty()) b = g->getBBox();
			float x, y;
			g->getGLcoords(x, y);
			klsBBox f = g->getBBox(); printf("%-22s at %6.1f %6.1f  body %6.2f %6.2f %6.2f %6.2f  full %6.2f %6.2f %6.2f %6.2f\n", g->getLibraryGateName().c_str(), x, y, b.getLeft(), b.getBottom(), b.getRight(), b.getTop(), f.getLeft(), f.getBottom(), f.getRight(), f.getTop());
		}
	}
	if (mode >= 0) {
		if (!cl_edit_tidy_begin(doc, 0, mode)) printf("nothing to tidy\n");
		cl_edit_tidy_end(doc, true);
	}
	if (strcmp(argv[4], "-") != 0) {
		FILE* f = fopen(argv[4], "w");
		if (!f) { fprintf(stderr, "cannot write %s\n", argv[4]); return 1; }
		fputs(cl_document_save_text(doc), f);
		fclose(f);
	}
	bool ok = true;
	if (strcmp(argv[5], "-") != 0) {
		const int W = argc > 7 ? atoi(argv[6]) : 1600, H = argc > 7 ? atoi(argv[7]) : 1000;
		const double sf = 2.0;
		CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
		CGContextRef ctx = CGBitmapContextCreate(nullptr, W, H, 8, 0, cs, kCGImageAlphaPremultipliedLast);
		CGContextTranslateCTM(ctx, 0, H);
		CGContextScaleCTM(ctx, sf, -sf);
		if (!cl_document_draw_fitted(doc, 0, ctx, W / sf, H / sf, 12, sf, CL_STYLE_DARK)) printf("empty page\n");
		CGImageRef img = CGBitmapContextCreateImage(ctx);
		CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8*)argv[5], strlen(argv[5]), false);
		CGImageDestinationRef dest = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, nullptr);
		CGImageDestinationAddImage(dest, img, nullptr);
		ok = CGImageDestinationFinalize(dest);
	}
	cl_document_close(doc);
	return ok ? 0 : 1;
}
