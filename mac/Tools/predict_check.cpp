// Headless check of Simulation View's classroom drawing (Predict, then
// reveal, and projector mode): list a page's lights and displays, find each
// one again by a click on it, and draw the page plain, in projector mode,
// covered (with guesses) and revealed. A covered page must give nothing away:
// it has to draw the same whatever the switches say.
//   predict_check <cl_gatedefs.xml> <in.cdl> <page> <out-dir>
#include "CedarCore.h"
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <CoreServices/CoreServices.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

const int W = 1200, H = 800;
const double SF = 2;

struct Shot { std::vector<unsigned char> pixels; double ox = 0, oy = 0, upp = 1; };

// Draws the page as Simulation View does, with covers or rings when `marks`
// are given, and writes it to `out` (when not null).
Shot draw(CLDocument* doc, int page, bool projector, bool predict, const std::vector<CLPredictMark>& marks, const std::string& out) {
	double l, b, r, t;
	cl_document_page_bounds(doc, page, &l, &b, &r, &t);
	const double wP = W / SF, hP = H / SF;
	const double upp = std::max((r - l + 6) / wP, (t - b + 6) / hP);
	const double ox = (l + r) / 2 - wP * upp / 2, oy = (b + t) / 2 + hP * upp / 2;
	Shot s;
	s.ox = ox; s.oy = oy; s.upp = upp;
	s.pixels.resize((size_t)W * H * 4);
	CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
	CGContextRef ctx = CGBitmapContextCreate(s.pixels.data(), W, H, 8, W * 4, cs, kCGImageAlphaPremultipliedLast);
	CGContextSetRGBFillColor(ctx, 0.030, 0.038, 0.050, 1); CGContextFillRect(ctx, CGRectMake(0, 0, W, H));
	CGContextTranslateCTM(ctx, 0, H); CGContextScaleCTM(ctx, SF, -SF);
	CLSimViewStyle st = { 6, 1.0, projector, predict };
	cl_simview_draw_page(doc, page, ctx, SF, ox, oy, upp, &st);
	if (!predict) cl_simview_draw_flow(doc, page, ctx, SF, ox, oy, upp, 0, projector ? CL_PROJECTOR_WIRE_SCALE : 1);
	if (!marks.empty()) cl_simview_draw_predict(doc, page, ctx, SF, ox, oy, upp, marks.data(), (int)marks.size(), projector);
	if (!out.empty()) {
		CGImageRef img = CGBitmapContextCreateImage(ctx);
		CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8*)out.c_str(), out.size(), false);
		CGImageDestinationRef d = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, nullptr);
		CGImageDestinationAddImage(d, img, nullptr);
		CGImageDestinationFinalize(d);
		CFRelease(d); CFRelease(url); CGImageRelease(img);
	}
	CGContextRelease(ctx);
	CGColorSpaceRelease(cs);
	return s;
}

}  // namespace

int main(int argc, char** argv) {
	if (argc < 5) { fprintf(stderr, "usage: predict_check <gatedefs> <in.cdl> <page> <out-dir>\n"); return 2; }
	if (!cl_library_load(argv[1])) return 1;
	char err[512];
	CLDocument* doc = cl_document_open(argv[2], err, sizeof err);
	if (!doc) { fprintf(stderr, "%s\n", err); return 1; }
	const int page = atoi(argv[3]);
	const std::string dir = argv[4];
	for (int i = 0; i < 50; i++) cl_document_step(doc);
	int fails = 0;

	CLSimLight lights[64];
	const int n = std::min(64, cl_simview_lights(doc, page, lights, 64));
	printf("%d lights\n", n);
	if (n == 0) { printf("FAIL no lights on the page\n"); return 1; }
	std::vector<std::string> names;
	for (int i = 0; i < n; i++) {
		const CLSimLight& l = lights[i];
		char name[64];
		cl_simview_light_name(doc, page, l.gate, name, sizeof name);
		int digit = -1;
		const long hit = cl_simview_light_at(doc, page, (l.left + l.right) / 2, (l.bottom + l.top) / 2, &digit);
		printf("  gate %ld %s \"%s\" value %d box %.2f,%.2f..%.2f,%.2f  click finds %ld\n", l.gate,
		       l.digits ? "display" : "light", name, l.value, l.left, l.bottom, l.right, l.top, hit);
		if (hit != l.gate) { printf("FAIL a click on gate %ld's light found %ld\n", l.gate, hit); fails++; }
		// Every light has a name of its own (a screen reader tells them apart).
		const std::string full = std::string(l.digits ? "Display " : "Light ") + name;
		if (!name[0]) { printf("FAIL gate %ld has no name\n", l.gate); fails++; }
		if (std::find(names.begin(), names.end(), full) != names.end()) { printf("FAIL two lights are both \"%s\"\n", full.c_str()); fails++; }
		names.push_back(full);
	}
	if (cl_simview_light_at(doc, page, 1e6, 1e6, nullptr) != -1) { printf("FAIL a click far away found a light\n"); fails++; }

	// Covered, the page must look the same whatever the switches say: flip
	// every switch we can find and compare.
	std::vector<CLPredictMark> covered;
	for (int i = 0; i < n; i++) covered.push_back({ lights[i].gate, i % 2 == 0 ? (lights[i].digits ? 0xA : 1) : -1, CL_PREDICT_COVERED, i == 0 });
	draw(doc, page, false, false, {}, dir + "/sim.png");
	draw(doc, page, true, false, {}, dir + "/sim-projector.png");
	const Shot before = draw(doc, page, false, true, covered, dir + "/predict-covered.png");
	double l, b, r, t;
	cl_document_page_bounds(doc, page, &l, &b, &r, &t);
	int flips = 0;
	std::vector<std::pair<double, double>> flipped;
	for (double y = t; y >= b; y -= 0.25)
		for (double x = l; x <= r; x += 0.25)
			if (cl_document_click(doc, page, x, y)) {
				flips++;
				flipped.push_back({ x, y });
				// Past this part, so one click each.
				x += 2.5;
			}
	for (int i = 0; i < 50; i++) cl_document_step(doc);
	const Shot after = draw(doc, page, false, true, covered, dir + "/predict-covered-flipped.png");
	CLSimLight now[64];
	cl_simview_lights(doc, page, now, 64);
	int changed = 0;
	for (int i = 0; i < n; i++) if (now[i].value != lights[i].value) changed++;
	// The switches themselves show their new state; nothing else may change.
	size_t diff = 0, nearSwitches = 0;
	for (int py = 0; py < H; py++)
		for (int px = 0; px < W; px++) {
			const size_t i = ((size_t)py * W + px) * 4;
			if (memcmp(&before.pixels[i], &after.pixels[i], 4) == 0) continue;
			const double wx = before.ox + px / SF * before.upp, wy = before.oy - py / SF * before.upp;
			bool atSwitch = false;
			for (auto& f : flipped) if (std::abs(wx - f.first) < 2.2 && std::abs(wy - f.second) < 2.2) atSwitch = true;
			(atSwitch ? nearSwitches : diff)++;
		}
	printf("flipped %d switches: %d lights changed; covered, %zu pixels changed at the switches and %zu elsewhere\n",
	       flips, changed, nearSwitches, diff);
	if (changed == 0) printf("note: no light changed, so hiding wasn't tested\n");
	if (diff > 0) { printf("FAIL the covered page gives something away\n"); fails++; }
	const Shot plain1 = draw(doc, page, false, false, {}, "");
	for (auto& f : flipped) cl_document_click(doc, page, f.first, f.second);
	for (int i = 0; i < 50; i++) cl_document_step(doc);
	const Shot plain2 = draw(doc, page, false, false, {}, "");
	if (changed > 0 && plain1.pixels == plain2.pixels) { printf("FAIL the uncovered page didn't change\n"); fails++; }
	for (auto& f : flipped) cl_document_click(doc, page, f.first, f.second);
	for (int i = 0; i < 50; i++) cl_document_step(doc);

	// Revealed: rings by how each guess did.
	std::vector<CLPredictMark> revealed;
	for (int i = 0; i < n; i++) {
		const int guess = covered[i].guess;
		const int mark = now[i].value < 0 ? CL_PREDICT_UNCLEAR : guess < 0 ? CL_PREDICT_UNGUESSED
		               : guess == now[i].value ? CL_PREDICT_RIGHT : CL_PREDICT_WRONG;
		revealed.push_back({ now[i].gate, guess, mark, false });
	}
	draw(doc, page, false, false, revealed, dir + "/predict-revealed.png");
	draw(doc, page, true, true, covered, dir + "/predict-projector.png");
	cl_document_close(doc);
	printf(fails ? "FAILED\n" : "ok\n");
	return fails ? 1 : 0;
}
