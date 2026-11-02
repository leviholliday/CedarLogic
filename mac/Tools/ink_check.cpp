// Headless check of drawing on the circuit and student notes (the website's
// docs/DRAWING-NOTES.md) through the C interface and the whole core:
// every sample in format/tests/fixtures/drawing opened and saved again (the
// same bytes, but for the generator), the Share Link texts, the sync
// structure text, strokes drawn, split, capped, erased, cleared and undone,
// show and hide, the notes, the <version> hazard, older formats, read-only
// pages, and PNGs of a drawing in each look.
//   ink_check <cl_gatedefs.xml> <fixtures/drawing dir> [png out dir]
#include "CedarCore.h"
#include "SyncInternal.h"
#include "circuit_file_io.hpp"
#include "ink.hpp"
#include <algorithm>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <CoreServices/CoreServices.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int failures = 0, checks = 0;

static void check(bool ok, const std::string& what) {
	checks++;
	if (!ok) failures++;
	printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
}

static std::string slurp(const std::string& path) {
	std::ifstream in(path, std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

static CLDocument* openText(const std::string& text, std::string* why = nullptr) {
	char err[512] = "";
	CLDocument* doc = cl_document_open_text(text.data(), (long)text.size(), err, sizeof err);
	if (why) *why = err;
	return doc;
}

// The generator line put to the core's, so texts written elsewhere compare.
static std::string withGenerator(const std::string& text, const std::string& gen) {
	const size_t a = text.find("(generator ");
	if (a == std::string::npos) return text;
	const size_t b = text.find(")\n", a);
	return text.substr(0, a) + "(generator " + gen + text.substr(b);
}

static std::string coreGenerator() {
	CLDocument* d = cl_document_new();
	const std::string t = cl_document_save_text(d);
	cl_document_close(d);
	const size_t a = t.find("(generator ") + 11;
	return t.substr(a, t.find(")\n", a) - a);
}

// Two texts hold the same circuit, drawing and notes: the core writes gates
// in id order, and the samples were written by CedarLogic Online, so the
// bytes of the gates may differ; everything the drawing adds must not.
static bool sameCircuit(const std::string& a, const std::string& b, std::string& why) {
	cl::CircuitFile x = cl::readCircuitFile(a), y = cl::readCircuitFile(b);
	auto byId = [](cl::CircuitFile& f) {
		for (cl::Page& p : f.pages) {
			std::sort(p.gates.begin(), p.gates.end(), [](const cl::GateInstance& g, const cl::GateInstance& h) { return g.uuid < h.uuid; });
			std::sort(p.wires.begin(), p.wires.end(), [](const cl::WireInstance& g, const cl::WireInstance& h) { return g.ids < h.ids; });
			for (cl::GateInstance& g : p.gates) std::sort(g.params.begin(), g.params.end(), [](const cl::Param& m, const cl::Param& n) { return m.name < n.name; });
		}
		f.generator.clear();
	};
	byId(x);
	byId(y);
	if (x.notes != y.notes) { why = "notes"; return false; }
	if (x.inkHidden != y.inkHidden) { why = "hidden"; return false; }
	if (x.pages.size() != y.pages.size()) { why = "pages"; return false; }
	for (size_t i = 0; i < x.pages.size(); i++) {
		if (!(x.pages[i].ink == y.pages[i].ink)) { why = "page " + std::to_string(i) + " drawing"; return false; }
		// The parts: the same gates and wires. (Their bytes are the core's own
		// business: it keeps positions as floats and leaves out what the
		// library owns, as it always has.)
		std::vector<std::string> gx, gy, wx, wy;
		for (auto& g : x.pages[i].gates) gx.push_back(g.uuid + g.libName);
		for (auto& g : y.pages[i].gates) gy.push_back(g.uuid + g.libName);
		for (auto& w : x.pages[i].wires) wx.push_back(w.ids.empty() ? "" : w.ids[0]);
		for (auto& w : y.pages[i].wires) wy.push_back(w.ids.empty() ? "" : w.ids[0]);
		if (gx != gy || wx != wy || x.pages[i].name != y.pages[i].name) { why = "page " + std::to_string(i) + " parts"; return false; }
	}
	return true;
}

// Every node the drawing adds, as the writer writes it, in file order.
static std::string inkNodes(const std::string& text) {
	std::string out;
	cl::SNode root = cl::parseSexpr(text);
	for (const cl::SNode& c : root.items) {
		if (c.head() == "notes" || c.head() == "show-drawing") out += cl::writeSexpr(c);
		if (c.head() == "page")
			for (const cl::SNode& e : c.items) if (e.head() == "drawing") out += cl::writeSexpr(e);
	}
	return out;
}

// A stroke drawn by "hand": samples `step` apart along a zigzag.
static int drawZigzag(CLDocument* doc, int page, int tool, int samples, double x0, double y0, double step,
                      double upp = 0.05, bool pressure = false) {
	if (cl_ink_begin(doc, page, tool, tool == CL_INK_HIGHLIGHTER ? "yellow" : "red", tool == CL_INK_HIGHLIGHTER ? 1.2 : 0.25, upp) != CL_INK_OK)
		return -1;
	std::vector<CLInkPoint> pts;
	for (int i = 0; i < samples; i++)
		pts.push_back({ x0 + i * step, y0 + ((i % 2) ? 0.4 : 0), pressure ? 0.2 + 0.6 * (i % 5) / 4.0 : -1 });
	cl_ink_add(doc, pts.data(), (int)pts.size());
	return cl_ink_end(doc);
}

static std::string structure(const std::string& text) {
	return clsync::structureText(text, [](const std::string& lib, bool gui, const std::string& name) -> std::string {
		char buf[512];
		return cl_sync_core_gate_default(nullptr, lib.c_str(), gui, name.c_str(), buf, sizeof buf) ? std::string(buf) : std::string("\x01");
	});
}

// A tiny reader for structure.json: "name": { ... "structureText": "..." }.
static std::string jsonString(const std::string& json, size_t& at) {
	std::string out;
	at = json.find('"', at) + 1;
	while (json[at] != '"') {
		char c = json[at++];
		if (c == '\\') {
			char e = json[at++];
			if (e == 'n') out += '\n';
			else if (e == 't') out += '\t';
			else if (e == 'u') {
				unsigned v = (unsigned)strtoul(json.substr(at, 4).c_str(), nullptr, 16);
				at += 4;
				if (v < 0x80) out += (char)v;
				else if (v < 0x800) { out += (char)(0xC0 | (v >> 6)); out += (char)(0x80 | (v & 0x3F)); }
				else { out += (char)(0xE0 | (v >> 12)); out += (char)(0x80 | ((v >> 6) & 0x3F)); out += (char)(0x80 | (v & 0x3F)); }
			} else out += e;
		} else out += c;
	}
	at++;
	return out;
}

static bool writePng(CLDocument* doc, int page, const std::string& path, int style, bool ink, int W = 1200, int H = 800) {
	CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
	CGContextRef ctx = CGBitmapContextCreate(nullptr, W, H, 8, 0, cs, kCGImageAlphaPremultipliedLast);
	if (style == CL_STYLE_DARK) CGContextSetRGBFillColor(ctx, 0.075, 0.082, 0.098, 1);
	else CGContextSetRGBFillColor(ctx, 1, 1, 1, 1);
	CGContextFillRect(ctx, CGRectMake(0, 0, W, H));
	CGContextTranslateCTM(ctx, 0, H);
	CGContextScaleCTM(ctx, 2, -2);
	const bool drew = cl_document_draw_fitted_ink(doc, page, ctx, W / 2.0, H / 2.0, 16, 2, style, ink);
	CGImageRef img = CGBitmapContextCreateImage(ctx);
	CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8*)path.c_str(), (CFIndex)path.size(), false);
	CGImageDestinationRef dest = CGImageDestinationCreateWithURL(url, kUTTypePNG, 1, nullptr);
	CGImageDestinationAddImage(dest, img, nullptr);
	const bool ok = CGImageDestinationFinalize(dest);
	CFRelease(dest); CFRelease(url); CGImageRelease(img); CGContextRelease(ctx); CGColorSpaceRelease(cs);
	return drew && ok;
}

// Pixels of a fitted drawing (RGBA), for comparing with and without ink.
static std::vector<unsigned char> pixels(CLDocument* doc, int page, int style, bool ink, int W = 300, int H = 200) {
	std::vector<unsigned char> buf((size_t)W * H * 4, 0);
	CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
	CGContextRef ctx = CGBitmapContextCreate(buf.data(), W, H, 8, (size_t)W * 4, cs, kCGImageAlphaPremultipliedLast);
	CGContextSetRGBFillColor(ctx, 1, 1, 1, 1);
	CGContextFillRect(ctx, CGRectMake(0, 0, W, H));
	CGContextTranslateCTM(ctx, 0, H);
	CGContextScaleCTM(ctx, 1, -1);
	cl_document_draw_fitted_ink(doc, page, ctx, W, H, 8, 1, style, ink);
	CGContextRelease(ctx);
	CGColorSpaceRelease(cs);
	return buf;
}

int main(int argc, char** argv) {
	if (argc < 3) { fprintf(stderr, "usage: ink_check <cl_gatedefs.xml> <fixtures/drawing> [png dir]\n"); return 2; }
	if (!cl_library_load(argv[1])) { fprintf(stderr, "library failed\n"); return 1; }
	cl_set_settle_on_open(false);
	const std::string dir = std::string(argv[2]) + "/";
	const std::string pngDir = argc > 3 ? argv[3] : "";
	const std::string gen = coreGenerator();
	check(gen == "\"CedarLogic 4.2.0-native\"", "the core writes as CedarLogic 4.2.0-native (" + gen + ")");

	// ---- The samples, through the whole core ----
	struct Sample { const char* name; const char* rewrite; const char* link; };
	const Sample samples[] = {
		{ "drawing-and-notes", nullptr, "drawing-and-notes.link" },
		{ "hidden-drawing", nullptr, "hidden-drawing.link" },
		{ "notes-only", nullptr, nullptr },
		{ "hidden-flag-without-drawing", "hidden-flag-without-drawing.rewritten", nullptr },
		{ "odd-strokes", "odd-strokes.rewritten", nullptr },
		{ "newer-drawing", nullptr, nullptr },
		{ "drawing-and-notes.link", nullptr, nullptr },
		{ "hidden-drawing.link", nullptr, nullptr },
		{ "base-and", nullptr, nullptr },
		{ "base-half-adder", nullptr, nullptr },
	};
	for (const Sample& s : samples) {
		const std::string text = slurp(dir + "cdl/" + s.name + ".cdl");
		std::string why;
		CLDocument* doc = openText(text, &why);
		check(doc != nullptr, std::string("opens ") + s.name + (doc ? "" : ": " + why));
		why.clear();
		if (!doc) continue;
		const std::string want = withGenerator(s.rewrite ? slurp(dir + "cdl/" + s.rewrite + ".cdl") : text, gen);
		const std::string got = cl_document_save_text(doc);
		check(sameCircuit(got, want, why), std::string(s.name) + ": saved again, the same circuit, drawing and notes" +
		      (s.rewrite ? " as its .rewritten" : "") + (why.empty() ? "" : " (" + why + " differ)"));
		check(inkNodes(got) == inkNodes(want), std::string(s.name) + ": the drawing and notes nodes byte for byte");
		CLDocument* again = openText(got);
		check(again && std::string(cl_document_save_text(again)) == got, std::string(s.name) + ": a second save writes the same bytes");
		if (again) cl_document_close(again);
		if (s.link) {
			const int flags = CL_SAVE_NO_NOTES | (cl_ink_shown(doc) ? 0 : CL_SAVE_NO_INK);
			const std::string link = cl_document_save_text_ex(doc, flags);
			const std::string wantLink = withGenerator(slurp(dir + "cdl/" + s.link + ".cdl"), gen);
			check(sameCircuit(link, wantLink, why) && inkNodes(link) == inkNodes(wantLink), std::string(s.name) + ": the Share Link text");
			check(link.find("(notes") == std::string::npos, std::string(s.name) + ": no notes in a link");
		}
		cl_document_close(doc);
	}
	{
		std::string why;
		CLDocument* doc = openText(slurp(dir + "cdl/notes-version-hazard.cdl"), &why);
		check(doc == nullptr && why.find("newer version") != std::string::npos, "the unescaped <version> sample is refused, as by every app");
		if (doc) cl_document_close(doc);
	}

	// ---- The sync structure text (structure.json) ----
	{
		const std::string json = slurp(dir + "structure.json");
		size_t at = 0;
		int n = 0;
		while ((at = json.find("\n \"", at)) != std::string::npos) {
			at += 2;
			const std::string name = jsonString(json, at);
			const size_t st = json.find("\"structureText\"", at);
			size_t vat = st + 16;
			const std::string want = jsonString(json, vat);
			const std::string got = structure(slurp(dir + "cdl/" + name + ".cdl"));
			check(got == want, "structure text: " + name);
			if (got != want) printf("  got:\n%s  want:\n%s", got.c_str(), want.c_str());
			n++;
			at = vat;
		}
		check(n == 8, "structure.json: every sample (" + std::to_string(n) + ")");
		// A circuit without drawing or notes keeps today's text: no D or N line.
		const std::string base = structure(slurp(dir + "cdl/base-half-adder.cdl"));
		check(base.find("\nD ") == std::string::npos && base.find("\nN ") == std::string::npos, "structure text: no D/N lines without a drawing");
	}

	// ---- Drawing through the C interface ----
	{
		CLDocument* doc = cl_document_new();
		const int undo0 = cl_edit_undo_count(doc);
		check(drawZigzag(doc, 0, CL_INK_PEN, 30, 0, 0, 0.2) == 1, "a stroke: one stroke added");
		check(cl_edit_undo_count(doc) == undo0 + 1 && std::string(cl_edit_undo_name(doc)) == "Draw", "a stroke: one \"Draw\" step");
		check(cl_ink_stroke_count(doc, 0) == 1 && cl_ink_any(doc), "a stroke: on the page");
		double l, b, r, t;
		check(cl_ink_bounds(doc, 0, &l, &b, &r, &t) && std::fabs(l - (-0.125)) < 1e-9 && std::fabs(r - (5.8 + 0.125)) < 1e-9,
		      "a stroke: its bounds include half its width");
		// Samples closer than 0.75 points (at 0.05 units a point: 0.0375) are skipped.
		check(drawZigzag(doc, 0, CL_INK_PEN, 50, 10, 0, 0.01) == 1, "close samples: still one stroke");
		// It saves and opens the same, quantized.
		std::string text = cl_document_save_text(doc);
		CLDocument* back = openText(text);
		check(back && cl_ink_stroke_count(back, 0) == 2 && std::string(cl_document_save_text(back)) == text,
		      "a drawing saves and reopens unchanged");
		if (back) cl_document_close(back);
		// Undo, redo, and the page it was on.
		cl_edit_undo(doc);
		check(cl_ink_stroke_count(doc, 0) == 1 && cl_ink_history_page(doc) == 0, "undo takes the stroke back (page 0)");
		cl_edit_redo(doc);
		check(cl_ink_stroke_count(doc, 0) == 2 && cl_ink_history_page(doc) == 0, "redo puts it back");
		// A long stroke: split at 2,000 samples, one step.
		const int u = cl_edit_undo_count(doc);
		const int parts = drawZigzag(doc, 0, CL_INK_PEN, 4500, 0, 10, 0.1);
		check(parts == 3 && cl_edit_undo_count(doc) == u + 1, "4,500 samples: 3 strokes in one step (" + std::to_string(parts) + ")");
		cl_edit_undo(doc);
		check(cl_ink_stroke_count(doc, 0) == 2, "undo takes the whole long stroke back");
		// Pressure: a pen keeps it, a highlighter doesn't.
		drawZigzag(doc, 0, CL_INK_PEN, 10, 0, 20, 0.3, 0.05, true);
		drawZigzag(doc, 0, CL_INK_HIGHLIGHTER, 10, 0, 22, 0.3, 0.05, true);
		text = cl_document_save_text(doc);
		check(text.find("(stroke pen red 0.25 \"") != std::string::npos && text.find("(stroke highlighter yellow 1.2 \"") != std::string::npos,
		      "pen and highlighter are saved with their tokens and widths");
		{
			const size_t hl = text.find("(stroke highlighter");
			const size_t end = text.find(")", hl);
			const std::string line = text.substr(hl, end - hl);
			check(std::count(line.begin(), line.end(), '"') == 2, "the highlighter keeps no pressure");
			const size_t pen = text.rfind("(stroke pen red 0.25", hl);
			const std::string pl = text.substr(pen, text.find(")", pen) - pen);
			check(std::count(pl.begin(), pl.end(), '"') == 4, "a tablet pen keeps pressure");
		}
		// A tap is a dot.
		cl_ink_begin(doc, 0, CL_INK_PEN, "blue", 0.5, 0.05);
		CLInkPoint dot = { 3.333, -4.444, -1 };
		cl_ink_add(doc, &dot, 1);
		check(cl_ink_end(doc) == 1, "a tap: a dot");
		text = cl_document_save_text(doc);
		check(text.find("(stroke pen blue 0.5 \"" + cl::ink::encodePoints({ 333, -444 }) + "\")") != std::string::npos,
		      "a dot: one quantized point (333, -444)");

		// ---- The eraser ----
		const int before = cl_ink_stroke_count(doc, 0);
		const int ue = cl_edit_undo_count(doc);
		cl_ink_erase_begin(doc, 0);
		// A swept drag from below the first zigzag to above it, fast (two samples).
		cl_ink_erase_to(doc, 2.05, -3, 0.1);
		const int erased = cl_ink_erase_to(doc, 2.05, 3, 0.1);
		check(cl_ink_erase_end(doc) == erased && erased >= 1, "the eraser takes whole strokes it sweeps over (" + std::to_string(erased) + ")");
		check(cl_ink_stroke_count(doc, 0) == before - erased && cl_edit_undo_count(doc) == ue + 1 &&
		      std::string(cl_edit_undo_name(doc)) == "Erase", "an eraser drag is one \"Erase\" step");
		const std::string afterErase = cl_document_save_text(doc);
		cl_edit_undo(doc);
		check(cl_ink_stroke_count(doc, 0) == before && std::string(cl_document_save_text(doc)) == text,
		      "undoing an erase puts the strokes back where they were");
		cl_edit_redo(doc);
		check(std::string(cl_document_save_text(doc)) == afterErase, "redoing it erases them again");
		cl_ink_erase_begin(doc, 0);
		cl_ink_erase_to(doc, 100, 100, 0.1);
		const int ue2 = cl_edit_undo_count(doc);
		check(cl_ink_erase_end(doc) == 0 && cl_edit_undo_count(doc) == ue2, "an eraser drag that erases nothing makes no step");

		// ---- Clear ----
		const int n = cl_ink_stroke_count(doc, 0);
		check(cl_ink_clear(doc, 0) == n && cl_ink_stroke_count(doc, 0) == 0 && std::string(cl_edit_undo_name(doc)) == "Clear Drawing",
		      "Clear Drawing: every stroke, one step");
		cl_edit_undo(doc);
		check(cl_ink_stroke_count(doc, 0) == n, "undoing Clear brings them back");

		// ---- Show and hide: saved, not an undo step ----
		const int us = cl_edit_undo_count(doc);
		cl_ink_set_shown(doc, false);
		check(!cl_ink_shown(doc) && cl_edit_undo_count(doc) == us && cl_document_is_edited(doc), "hiding: not an undo step, but an edit");
		text = cl_document_save_text(doc);
		check(text.find("(show-drawing no)") != std::string::npos, "hidden: (show-drawing no) is saved");
		back = openText(text);
		check(back && !cl_ink_shown(back), "hidden: it reopens hidden");
		check(back && std::string(cl_document_save_text_ex(back, CL_SAVE_NO_NOTES | CL_SAVE_NO_INK)).find("drawing") == std::string::npos,
		      "a link without the drawing has neither strokes nor the flag");
		if (back) cl_document_close(back);
		cl_ink_set_shown(doc, true);

		// ---- Notes ----
		const int un = cl_edit_undo_count(doc);
		std::string big = "Line 1\r\nLine 2\rTab\there\x01";
		big += std::string(25000, 'x');
		cl_notes_set(doc, big.c_str());
		const std::string notes = cl_notes(doc);
		check(notes.rfind("Line 1\nLine 2\nTab\there", 0) == 0 && notes.find('\x01') == std::string::npos, "notes: normalized");
		check(notes.size() == 20000, "notes: cut at 20,000 characters (" + std::to_string(notes.size()) + ")");
		check(cl_edit_undo_count(doc) == un, "notes: not an undo step");
		cl_notes_set(doc, "Teacher said <version>9.0</version> is just text \"quoted\" \\ done");
		text = cl_document_save_text(doc);
		check(text.find("<version>") == std::string::npos, "notes with <version>: escaped in the file");
		std::string why;
		back = openText(text, &why);
		check(back && std::string(cl_notes(back)) == "Teacher said <version>9.0</version> is just text \"quoted\" \\ done",
		      "notes with <version>: the file opens, the text the same" + (back ? std::string() : ": " + why));
		if (back) cl_document_close(back);
		check(std::string(cl_document_save_text_ex(doc, CL_SAVE_NO_NOTES)).find("(notes") == std::string::npos, "a link leaves the notes out");
		// A label too (every string): text through a gate's setting.
		cl_notes_set(doc, "");
		check(std::string(cl_document_save_text(doc)).find("(notes") == std::string::npos, "empty notes: not written");

		// ---- Older formats ----
		char err[512] = "";
		const std::string v2 = "/private/tmp/ink_check_v2.cdl";
		const int r2 = cl_document_export_legacy(doc, v2.c_str(), 2, err, sizeof err);
		check(r2 == 1 && std::string(err).find("drawing and notes aren't kept") != std::string::npos, "Export V2: written, with the warning");
		const int r1 = cl_document_export_legacy(doc, "/private/tmp/ink_check_v1.cdl", 1, err, sizeof err);
		check(r1 == 1 && std::string(err).find("drawing and notes aren't kept") != std::string::npos, "Export V1.x: written, with the warning");
		remove(v2.c_str());
		remove("/private/tmp/ink_check_v1.cdl");
		cl_document_close(doc);
	}

	// ---- The caps ----
	{
		CLDocument* doc = cl_document_new();
		// Zigzags don't simplify: 10 strokes of 2,000 points fill a page.
		int made = 0;
		for (int i = 0; i < 10; i++) made += drawZigzag(doc, 0, CL_INK_PEN, 2000, 0, i * 2.0, 0.2, 0.05);
		check(made == 10 && cl_ink_point_count(doc, 0) == 20000, "a page takes 20,000 points (" + std::to_string(cl_ink_point_count(doc, 0)) + ")");
		check(cl_ink_can_draw(doc, 0) == CL_INK_FULL && cl_ink_begin(doc, 0, CL_INK_PEN, "ink", 0.25, 0.05) == CL_INK_FULL,
		      "a full page: no more strokes");
		cl_edit_undo(doc);
		check(cl_ink_can_draw(doc, 0) == CL_INK_OK, "after an undo there's room again");
		// A stroke longer than the room left is cut at the last point that fits.
		check(drawZigzag(doc, 0, CL_INK_PEN, 1500, 0, 50, 0.2) == 1 && drawZigzag(doc, 0, CL_INK_PEN, 1500, 0, 60, 0.2) >= 1,
		      "strokes past the room left still commit");
		check(cl_ink_point_count(doc, 0) == 20000, "cut at the cap: exactly 20,000 points (" + std::to_string(cl_ink_point_count(doc, 0)) + ")");
		// The circuit: 60,000 over three pages.
		cl_document_add_page(doc);
		cl_document_add_page(doc);
		cl_document_add_page(doc);
		for (int p = 1; p <= 3; p++)
			for (int i = 0; i < 10; i++) drawZigzag(doc, p, CL_INK_PEN, 2000, 0, i * 2.0, 0.2, 0.05);
		check(cl_ink_point_count(doc, -1) == 60000 && cl_ink_can_draw(doc, 3) == CL_INK_FULL,
		      "the circuit takes 60,000 points (" + std::to_string(cl_ink_point_count(doc, -1)) + ")");
		cl_document_close(doc);
	}

	// ---- A read-only page (a newer drawing) ----
	{
		CLDocument* doc = openText(slurp(dir + "cdl/newer-drawing.cdl"));
		check(doc && cl_ink_page_read_only(doc, 0) && cl_ink_page_read_only(doc, 1), "newer drawings: read-only pages");
		check(doc && cl_ink_can_draw(doc, 0) == CL_INK_READ_ONLY && cl_ink_clear(doc, 0) == 0 &&
		      cl_ink_begin(doc, 0, CL_INK_PEN, "ink", 0.25, 0.05) == CL_INK_READ_ONLY, "newer drawings: no drawing, erasing or clearing");
		bool noted = false;
		for (int i = 0; doc && i < cl_document_notice_count(doc); i++)
			if (strstr(cl_document_notice(doc, i), "newer CedarLogic") && !cl_document_notice_is_warning(doc, i)) noted = true;
		check(noted, "newer drawings: a note (not a warning) says so");
		if (doc) cl_document_close(doc);
		doc = openText(slurp(dir + "cdl/odd-strokes.cdl"));
		bool warned = false;
		for (int i = 0; doc && i < cl_document_notice_count(doc); i++)
			if (strstr(cl_document_notice(doc, i), "3 marks in the drawing couldn't be read") && cl_document_notice_is_warning(doc, i)) warned = true;
		check(warned, "unreadable strokes: one warning with how many");
		if (doc) cl_document_close(doc);
	}

	// ---- Drawing it ----
	{
		CLDocument* doc = openText(slurp(dir + "cdl/drawing-and-notes.cdl"));
		const auto with = pixels(doc, 0, CL_STYLE_LIGHT, true), without = pixels(doc, 0, CL_STYLE_LIGHT, false);
		check(with != without, "Include drawing changes the picture");
		cl_ink_set_shown(doc, false);
		check(pixels(doc, 0, CL_STYLE_LIGHT, true) == with, "Include drawing draws it even when it's hidden");
		std::vector<unsigned char> plain(4 * 300 * 200);
		{
			// cl_document_draw follows show/hide.
			CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
			auto draw = [&](std::vector<unsigned char>& buf) {
				CGContextRef ctx = CGBitmapContextCreate(buf.data(), 300, 200, 8, 1200, cs, kCGImageAlphaPremultipliedLast);
				CGContextSetRGBFillColor(ctx, 1, 1, 1, 1);
				CGContextFillRect(ctx, CGRectMake(0, 0, 300, 200));
				CGContextTranslateCTM(ctx, 0, 200);
				CGContextScaleCTM(ctx, 1, -1);
				cl_document_draw(doc, 0, ctx, 1, -2, 2, 0.12, false);
				CGContextRelease(ctx);
			};
			std::vector<unsigned char> hidden(4 * 300 * 200), shown(4 * 300 * 200);
			draw(hidden);
			cl_ink_set_shown(doc, true);
			draw(shown);
			check(hidden != shown, "the canvas draws the drawing only while it's shown");
			CGColorSpaceRelease(cs);
		}
		if (!pngDir.empty()) {
			bool ok = writePng(doc, 0, pngDir + "/ink-light.png", CL_STYLE_LIGHT, true) &&
			          writePng(doc, 0, pngDir + "/ink-dark.png", CL_STYLE_DARK, true) &&
			          writePng(doc, 0, pngDir + "/ink-print-bw.png", CL_STYLE_PRINT, true) &&
			          writePng(doc, 0, pngDir + "/ink-none.png", CL_STYLE_LIGHT, false) &&
			          writePng(doc, 1, pngDir + "/ink-page2.png", CL_STYLE_LIGHT, true);
			check(ok, "PNGs written to " + pngDir);
		}
		cl_document_close(doc);
	}

	printf("%d of %d checks passed\n", checks - failures, checks);
	return failures ? 1 : 0;
}
