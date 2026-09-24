// Headless check of simulation and clicks: open a page, render it, click the
// first spot a part takes (scanning the page), step, render again.
//   sim_check <cl_gatedefs.xml> <in.cdl> <page> <before.png> <after.png>
#include "CedarCore.h"
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <CoreServices/CoreServices.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static bool render(CLDocument* doc, int page, const char* out) {
	const int W = 1200, H = 800; const double sf = 2;
	double l, b, r, t;
	if (!cl_document_page_bounds(doc, page, &l, &b, &r, &t)) return false;
	const double wP = W / sf, hP = H / sf;
	const double upp = std::max((r - l + 4) / wP, (t - b + 4) / hP);
	CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
	CGContextRef ctx = CGBitmapContextCreate(nullptr, W, H, 8, 0, cs, kCGImageAlphaPremultipliedLast);
	CGContextSetRGBFillColor(ctx, 1, 1, 1, 1); CGContextFillRect(ctx, CGRectMake(0, 0, W, H));
	CGContextTranslateCTM(ctx, 0, H); CGContextScaleCTM(ctx, sf, -sf);
	cl_document_draw(doc, page, ctx, sf, (l + r) / 2 - wP * upp / 2, (b + t) / 2 + hP * upp / 2, upp, false);
	CGImageRef img = CGBitmapContextCreateImage(ctx);
	CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8*)out, strlen(out), false);
	CGImageDestinationRef d = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, nullptr);
	CGImageDestinationAddImage(d, img, nullptr);
	return CGImageDestinationFinalize(d);
}

int main(int argc, char** argv) {
	if (argc < 6) { fprintf(stderr, "usage\n"); return 2; }
	if (!cl_library_load(argv[1])) return 1;
	char err[512];
	CLDocument* doc = cl_document_open(argv[2], err, sizeof err);
	if (!doc) { fprintf(stderr, "%s\n", err); return 1; }
	const int page = atoi(argv[3]);
	printf("notices=%d\n", cl_document_notice_count(doc));
	render(doc, page, argv[4]);
	double l, b, r, t;
	cl_document_page_bounds(doc, page, &l, &b, &r, &t);
	bool clicked = false;
	for (double y = t; y >= b && !clicked; y -= 0.25)
		for (double x = l; x <= r && !clicked; x += 0.25)
			if (cl_document_click(doc, page, x, y)) { printf("clicked at %.2f,%.2f\n", x, y); clicked = true; }
	for (int i = 0; i < 10; i++) cl_document_step(doc);
	render(doc, page, argv[5]);
	printf("clicked=%d\n", clicked);
	cl_document_close(doc);
	return clicked ? 0 : 1;
}
