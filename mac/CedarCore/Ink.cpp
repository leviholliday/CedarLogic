// Drawing on the circuit and student notes (the website's
// docs/DRAWING-NOTES.md): strokes captured, split, simplified and capped as
// they're drawn, the eraser, Clear, show and hide, the notes, and drawing
// the strokes in their layers through the render Scene (so another Scene
// backend draws them the same way). The model and file format are format/'s
// (circuit_file.hpp, ink.hpp); the undo steps are src/gui/command/cmdInk.

#include "DocumentImpl.h"
#include "CGScene.h"
#include "CircuitParse.h"
#include "ink.hpp"
#include "command/cmdInk.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace {

using cl::InkStroke;
using cl::PageInk;
using cl::render::Color;
using cl::render::Point;

// ---- Colours (DRAWING-NOTES 4.8; palette.json) --------------------------------

struct PenColour { const char* token; unsigned light, dark, print; };
const PenColour kPen[] = {
	{ "ink", 0x1d1d1f, 0xf5f5f7, 0x000000 },
	{ "red", 0xd70015, 0xff6961, 0xd70015 },
	{ "orange", 0xc93400, 0xffb340, 0xc93400 },
	{ "yellow", 0xb25000, 0xffd60a, 0xb25000 },
	{ "green", 0x248a3d, 0x30d158, 0x248a3d },
	{ "blue", 0x0040dd, 0x409cff, 0x0040dd },
	{ "purple", 0x8944ab, 0xda8fff, 0x8944ab },
	{ "pink", 0xd30f45, 0xff6482, 0xd30f45 },
};
struct HighlightColour { const char* token; unsigned rgb; };
const HighlightColour kHighlight[] = {
	{ "yellow", 0xffd60a }, { "green", 0x30d158 }, { "blue", 0x64d2ff }, { "pink", 0xff6482 },
	{ "orange", 0xff9f0a }, { "red", 0xff453a }, { "purple", 0xbf5af2 }, { "ink", 0x8e8e93 },
};

Color rgb(unsigned v, float a) {
	return Color(((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f, a);
}

bool isHighlighter(const std::string& tool) { return tool == "highlighter"; }

Color inkColour(const std::string& token, bool highlighter, int look) {
	if (look == kInkLookBW) return highlighter ? Color(0, 0, 0, 0.15f) : Color(0, 0, 0, 1);
	if (highlighter) {
		const float a = look == kInkLookLight ? 0.40f : 0.35f;
		for (const HighlightColour& h : kHighlight) if (token == h.token) return rgb(h.rgb, a);
		return rgb(kHighlight[0].rgb, a);   // unknown: yellow
	}
	const PenColour* c = &kPen[0];         // unknown: ink
	for (const PenColour& p : kPen) if (token == p.token) c = &p;
	return rgb(look == kInkLookDark ? c->dark : look == kInkLookPrint ? c->print : c->light, 1);
}

// ---- Drawing a stroke --------------------------------------------------------------

Point worldAt(const InkStroke& s, size_t i) { return Point(s.xy[2 * i] / 100.0f, s.xy[2 * i + 1] / 100.0f); }

Point mid(Point a, Point b) { return Point((a.x + b.x) / 2, (a.y + b.y) / 2); }

// A quadratic from a to c around b, flattened into `out` (a not repeated).
void flattenQuad(std::vector<Point>& out, Point a, Point b, Point c, float pixelsPerUnit) {
	const float len = (std::hypot(b.x - a.x, b.y - a.y) + std::hypot(c.x - b.x, c.y - b.y)) * pixelsPerUnit;
	const int steps = std::max(2, std::min(16, (int)std::ceil(len / 4)));
	for (int k = 1; k <= steps; k++) {
		const float t = (float)k / steps, u = 1 - t;
		out.push_back(Point(u * u * a.x + 2 * u * t * b.x + t * t * c.x, u * u * a.y + 2 * u * t * b.y + t * t * c.y));
	}
}

// The spec's path, M p0, Q p_i mid(p_i, p_i+1) ..., L p_n-1, as elements:
// element e runs from its start to its end, each a short polyline.
struct Element { std::vector<Point> pts; double p0, p1; };

std::vector<Element> pathElements(const InkStroke& s, float pixelsPerUnit) {
	std::vector<Element> out;
	const size_t n = s.points();
	const bool pr = !s.pressure.empty();
	auto pAt = [&](size_t i) { return pr ? cl::ink::pressureAt(s.pressure, i) : 1.0; };
	if (n < 2) return out;
	if (n == 2) {
		out.push_back({ { worldAt(s, 0), worldAt(s, 1) }, pAt(0), pAt(1) });
		return out;
	}
	Point from = worldAt(s, 0);
	double fromP = pAt(0);
	for (size_t i = 1; i + 1 < n; i++) {
		const Point to = mid(worldAt(s, i), worldAt(s, i + 1));
		const double toP = (pAt(i) + pAt(i + 1)) / 2;
		Element e{ { from }, fromP, toP };
		flattenQuad(e.pts, from, worldAt(s, i), to, pixelsPerUnit);
		out.push_back(std::move(e));
		from = to;
		fromP = toP;
	}
	out.push_back({ { from, worldAt(s, n - 1) }, fromP, pAt(n - 1) });
	return out;
}

void drawStroke(cl::render::Scene& scene, const InkStroke& s, const Color& colour, float pixelsPerUnit, bool projector) {
	const size_t n = s.points();
	if (n == 0) return;
	const float minPx = projector ? 3.0f : 1.0f;
	const float widthWorld = s.widthCenti / 100.0f;
	const float widthPx = std::max(widthWorld * pixelsPerUnit, minPx);
	if (n == 1) {
		scene.fillCircle(worldAt(s, 0), widthPx / pixelsPerUnit / 2, colour);
		return;
	}
	cl::render::Stroke st(colour, widthPx);
	st.cap = cl::render::Cap::Round;
	st.roundJoin = true;
	std::vector<Element> els = pathElements(s, pixelsPerUnit);
	// A highlighter, or a pen without pressure: one path, so a see-through
	// stroke doesn't darken where it overlaps itself.
	if (s.pressure.empty() || isHighlighter(s.tool)) {
		std::vector<Point> all;
		for (const Element& e : els) all.insert(all.end(), all.empty() ? e.pts.begin() : e.pts.begin() + 1, e.pts.end());
		scene.polyline(all, st, false);
		return;
	}
	// With pressure: element by element, each at the mean width of its ends,
	// runs of the same width (to a quarter pixel) drawn as one.
	std::vector<Point> run;
	float runWidth = -1;
	auto flush = [&]() {
		if (run.size() >= 2) { st.width = runWidth; scene.polyline(run, st, false); }
		run.clear();
	};
	for (const Element& e : els) {
		const double p = (e.p0 + e.p1) / 2;
		const float w = std::max((float)(widthWorld * (0.3 + 0.7 * p)) * pixelsPerUnit, minPx);
		const float bucket = std::round(w * 4) / 4;
		if (bucket != runWidth) {
			flush();
			runWidth = bucket;
		}
		run.insert(run.end(), run.empty() ? e.pts.begin() : e.pts.begin() + 1, e.pts.end());
	}
	flush();
}

// ---- The caps (DRAWING-NOTES 3.9) ---------------------------------------------------

size_t pagePoints(const GUICanvas* p) {
	size_t n = 0;
	for (const InkStroke& s : p->ink.strokes) n += s.points();
	return n;
}

size_t docPoints(const CLDocument* doc) {
	size_t n = 0;
	for (auto& p : doc->pages) n += pagePoints(p.get());
	return n;
}

double eps(const InkGesture& g) { return std::max(0.01, 0.35 * g.unitsPerPoint); }

const char* toolName(int tool) { return tool == CL_INK_HIGHLIGHTER ? "highlighter" : "pen"; }

// The part being captured, as a stroke (simplified, quantized).
InkStroke capturedStroke(const InkGesture& g) {
	return cl::ink::makeStroke(toolName(g.tool), g.color, g.width, g.xy, g.pressure ? g.pr : std::vector<double>(), eps(g));
}

// How many more points the page (and the circuit) can take, and strokes.
size_t roomFor(const CLDocument* doc, const GUICanvas* p, size_t pendingPoints) {
	const size_t pp = pagePoints(p) + pendingPoints, dp = docPoints(doc) + pendingPoints;
	if (pp >= cl::ink::kWritePagePoints || dp >= cl::ink::kWriteDocPoints) return 0;
	return std::min(cl::ink::kWritePagePoints - pp, cl::ink::kWriteDocPoints - dp);
}

// ---- The eraser's geometry -------------------------------------------------------------

double pointSeg(double px, double py, double ax, double ay, double bx, double by) {
	const double dx = bx - ax, dy = by - ay, L = dx * dx + dy * dy;
	double t = L == 0 ? 0 : ((px - ax) * dx + (py - ay) * dy) / L;
	t = std::max(0.0, std::min(1.0, t));
	return std::hypot(px - (ax + t * dx), py - (ay + t * dy));
}

double cross(double ax, double ay, double bx, double by, double cx, double cy) {
	return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

double segSeg(double ax, double ay, double bx, double by, double cx, double cy, double dx, double dy) {
	const double d1 = cross(ax, ay, bx, by, cx, cy), d2 = cross(ax, ay, bx, by, dx, dy);
	const double d3 = cross(cx, cy, dx, dy, ax, ay), d4 = cross(cx, cy, dx, dy, bx, by);
	if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))) return 0;
	return std::min(std::min(pointSeg(ax, ay, cx, cy, dx, dy), pointSeg(bx, by, cx, cy, dx, dy)),
	                std::min(pointSeg(cx, cy, ax, ay, bx, by), pointSeg(dx, dy, ax, ay, bx, by)));
}

// Whether a stroke comes within `reach` of the swept segment a-b.
bool touches(const InkStroke& s, double ax, double ay, double bx, double by, double reach) {
	const size_t n = s.points();
	if (n == 0) return false;
	if (n == 1) return pointSeg(s.xy[0] / 100.0, s.xy[1] / 100.0, ax, ay, bx, by) <= reach;
	for (size_t i = 0; i + 1 < n; i++) {
		if (segSeg(ax, ay, bx, by, s.xy[2 * i] / 100.0, s.xy[2 * i + 1] / 100.0,
		           s.xy[2 * i + 2] / 100.0, s.xy[2 * i + 3] / 100.0) <= reach) return true;
	}
	return false;
}

void submitInk(CLDocument* doc, GUICanvas* page, klsCommand* cmd, bool alreadyDone) {
	cmd->setCanvas(page);
	if (alreadyDone) doc->circuit.GetCommandProcessor()->Store(cmd);
	else doc->circuit.GetCommandProcessor()->Submit(cmd);
	doc->edited = true;
}

void settleTidyForInk(CLDocument* doc) {
	if (doc->tidy.active) cl_edit_tidy_end(doc, true);
}

}  // namespace

// ---- Drawing, for Document.cpp ----------------------------------------------------------

void clDrawInkLayer(cl::render::Scene& scene, const PageInk& ink, bool highlighter, int look,
                    float pixelsPerUnit, bool projector) {
	if (pixelsPerUnit <= 0) return;
	for (const InkStroke& s : ink.strokes) {
		const bool hl = isHighlighter(s.tool);   // any other tool draws as a pen
		if (hl != highlighter) continue;
		drawStroke(scene, s, inkColour(s.color, hl, look), pixelsPerUnit, projector);
	}
}

bool clInkDrawn(const CLDocument* doc, int inkMode) {
	if (doc == nullptr) return false;
	switch (inkMode) {
	case CL_INK_ALWAYS:
	case CL_INK_ALWAYS_PRINT: return true;
	case CL_INK_FOLLOW: return !doc->circuit.inkHidden;
	default: return false;
	}
}

extern "C" {

bool cl_ink_shown(const CLDocument* doc) { return doc && !doc->circuit.inkHidden; }

void cl_ink_set_shown(CLDocument* doc, bool shown) {
	if (doc == nullptr || doc->circuit.inkHidden == !shown) return;
	doc->circuit.inkHidden = !shown;
	doc->edited = true;
}

bool cl_ink_any(const CLDocument* doc) {
	if (doc == nullptr) return false;
	for (auto& p : doc->pages) if (!p->ink.empty()) return true;
	return false;
}

int cl_ink_stroke_count(const CLDocument* doc, int page) {
	GUICanvas* p = doc ? doc->page(page) : nullptr;
	return p ? (int)p->ink.strokes.size() : 0;
}

long cl_ink_point_count(const CLDocument* doc, int page) {
	if (doc == nullptr) return 0;
	if (page < 0) return (long)docPoints(doc);
	GUICanvas* p = doc->page(page);
	return p ? (long)pagePoints(p) : 0;
}

bool cl_ink_page_read_only(const CLDocument* doc, int page) {
	GUICanvas* p = doc ? doc->page(page) : nullptr;
	return p && p->ink.readOnly();
}

int cl_ink_can_draw(const CLDocument* doc, int page) {
	GUICanvas* p = doc ? doc->page(page) : nullptr;
	if (p == nullptr || p->ink.readOnly()) return CL_INK_READ_ONLY;
	if (p->ink.strokes.size() >= cl::ink::kWritePageStrokes || roomFor(doc, p, 0) == 0) return CL_INK_FULL;
	return CL_INK_OK;
}

bool cl_ink_bounds(const CLDocument* doc, int page, double* left, double* bottom, double* right, double* top) {
	GUICanvas* p = doc ? doc->page(page) : nullptr;
	if (p == nullptr || p->ink.strokes.empty()) return false;
	double l = 1e300, b = 1e300, r = -1e300, t = -1e300;
	for (const InkStroke& s : p->ink.strokes) {
		const double h = s.widthCenti / 200.0;
		for (size_t i = 0; i < s.points(); i++) {
			const double x = s.xy[2 * i] / 100.0, y = s.xy[2 * i + 1] / 100.0;
			l = std::min(l, x - h); r = std::max(r, x + h);
			b = std::min(b, y - h); t = std::max(t, y + h);
		}
	}
	*left = l; *bottom = b; *right = r; *top = t;
	return true;
}

int cl_ink_begin(CLDocument* doc, int page, int tool, const char* color, double width, double unitsPerPoint) {
	if (doc == nullptr) return CL_INK_READ_ONLY;
	const int can = cl_ink_can_draw(doc, page);
	if (can != CL_INK_OK) return can;
	settleTidyForInk(doc);
	InkGesture& g = doc->ink;
	g = InkGesture();
	g.active = true;
	g.page = page;
	g.tool = tool == CL_INK_HIGHLIGHTER ? CL_INK_HIGHLIGHTER : CL_INK_PEN;
	g.color = color && cl::ink::isToken(color) ? color : (g.tool == CL_INK_HIGHLIGHTER ? "yellow" : "ink");
	g.width = width > 0 ? width : 0.25;
	g.unitsPerPoint = unitsPerPoint > 0 ? unitsPerPoint : 0.05;
	return CL_INK_OK;
}

int cl_ink_add(CLDocument* doc, const CLInkPoint* points, int count) {
	if (doc == nullptr || !doc->ink.active) return CL_INK_READ_ONLY;
	InkGesture& g = doc->ink;
	GUICanvas* p = doc->page(g.page);
	if (p == nullptr) { g.active = false; return CL_INK_READ_ONLY; }
	for (int k = 0; k < count && !g.full; k++) {
		const CLInkPoint& s = points[k];
		if (!std::isfinite(s.x) || !std::isfinite(s.y)) continue;
		// A pen's pressure, from its first sample on (a tablet's, never the highlighter's).
		const bool hasPressure = s.pressure >= 0 && g.tool == CL_INK_PEN;
		if (g.xy.empty() && g.parts.empty()) g.pressure = hasPressure;
		if (hasPressure) g.lastPressure = std::min(1.0, s.pressure);
		// Closer than 0.75 points to the last sample kept: skipped.
		const size_t m = g.xy.size();
		if (m >= 2 && std::hypot(s.x - g.xy[m - 2], s.y - g.xy[m - 1]) < 0.75 * g.unitsPerPoint) continue;
		// 2,000 samples and another coming: that part is committed, and the
		// next starts where it ended (the person sees one line).
		if (m / 2 >= cl::ink::kWriteStrokePoints) {
			if (p->ink.strokes.size() + g.parts.size() + 2 > cl::ink::kWritePageStrokes) { g.full = true; break; }
			g.parts.push_back(capturedStroke(g));
			const double x = g.xy[m - 2], y = g.xy[m - 1];
			g.xy = { x, y };
			if (g.pressure) g.pr = { g.pr.back() };
		}
		g.xy.push_back(s.x);
		g.xy.push_back(s.y);
		if (g.pressure) g.pr.push_back(g.lastPressure);
		// The caps: counted on what's been captured (more than it will be), and
		// near them on what it simplifies to.
		size_t pending = 0;
		for (const InkStroke& part : g.parts) pending += part.points();
		if (p->ink.strokes.size() + g.parts.size() + 1 > cl::ink::kWritePageStrokes) { g.full = true; break; }
		if (g.xy.size() / 2 > roomFor(doc, p, pending) && capturedStroke(g).points() > roomFor(doc, p, pending)) {
			g.full = true;
			break;
		}
	}
	return g.full ? CL_INK_FULL : CL_INK_OK;
}

int cl_ink_end(CLDocument* doc) {
	if (doc == nullptr || !doc->ink.active) return 0;
	InkGesture g = std::move(doc->ink);
	doc->ink = InkGesture();
	GUICanvas* p = doc->page(g.page);
	if (p == nullptr || p->ink.readOnly()) return 0;
	std::vector<InkStroke> strokes = std::move(g.parts);
	if (!g.xy.empty()) strokes.push_back(capturedStroke(g));
	// Cut at the last point that fits the caps.
	std::vector<InkStroke> out;
	size_t pending = 0;
	for (InkStroke& s : strokes) {
		if (p->ink.strokes.size() + out.size() >= cl::ink::kWritePageStrokes) break;
		const size_t room = roomFor(doc, p, pending);
		if (room == 0) break;
		if (s.points() > room) {
			s.xy.resize(room * 2);
			if (!s.pressure.empty()) s.pressure.resize(room);
		}
		pending += s.points();
		out.push_back(std::move(s));
	}
	if (out.empty()) return 0;
	const int n = (int)out.size();
	submitInk(doc, p, new cmdInkAdd(p, std::move(out)), false);
	return n;
}

void cl_ink_cancel(CLDocument* doc) {
	if (doc) doc->ink = InkGesture();
}

bool cl_ink_drawing(const CLDocument* doc) { return doc && doc->ink.active; }

void cl_ink_erase_begin(CLDocument* doc, int page) {
	if (doc == nullptr) return;
	doc->inkErase = InkErase();
	GUICanvas* p = doc->page(page);
	if (p == nullptr || p->ink.readOnly()) return;
	settleTidyForInk(doc);
	doc->inkErase.active = true;
	doc->inkErase.page = page;
	doc->inkErase.before = p->ink.strokes;
	doc->inkErase.gone.assign(p->ink.strokes.size(), false);
}

int cl_ink_erase_to(CLDocument* doc, double x, double y, double radius) {
	if (doc == nullptr || !doc->inkErase.active) return 0;
	InkErase& e = doc->inkErase;
	GUICanvas* p = doc->page(e.page);
	if (p == nullptr) return 0;
	const double ax = e.haveLast ? e.lastX : x, ay = e.haveLast ? e.lastY : y;
	e.haveLast = true;
	e.lastX = x;
	e.lastY = y;
	bool changed = false;
	int count = 0;
	for (size_t i = 0; i < e.before.size(); i++) {
		if (!e.gone[i] && touches(e.before[i], ax, ay, x, y, radius + e.before[i].widthCenti / 200.0)) {
			e.gone[i] = true;
			changed = true;
		}
		if (e.gone[i]) count++;
	}
	if (changed) {
		// They vanish at once; the drag is one step at its end.
		std::vector<InkStroke> left;
		for (size_t i = 0; i < e.before.size(); i++) if (!e.gone[i]) left.push_back(e.before[i]);
		p->ink.strokes.swap(left);
	}
	return count;
}

int cl_ink_erase_end(CLDocument* doc) {
	if (doc == nullptr || !doc->inkErase.active) return 0;
	InkErase e = std::move(doc->inkErase);
	doc->inkErase = InkErase();
	GUICanvas* p = doc->page(e.page);
	std::vector<std::pair<size_t, InkStroke>> erased;
	for (size_t i = 0; i < e.before.size(); i++) if (e.gone[i]) erased.push_back({ i, e.before[i] });
	if (p == nullptr || erased.empty()) return 0;
	const int n = (int)erased.size();
	submitInk(doc, p, new cmdInkErase(p, std::move(erased)), true);
	return n;
}

int cl_ink_clear(CLDocument* doc, int page) {
	GUICanvas* p = doc ? doc->page(page) : nullptr;
	if (p == nullptr || p->ink.readOnly() || p->ink.strokes.empty()) return 0;
	settleTidyForInk(doc);
	const int n = (int)p->ink.strokes.size();
	submitInk(doc, p, new cmdInkClear(p), false);
	return n;
}

int cl_ink_history_page(const CLDocument* doc) { return doc ? doc->inkHistoryPage : -1; }

void cl_ink_hover(CLDocument* doc, int page, double x, double y, int tool, const char* color, double width) {
	if (doc == nullptr) return;
	InkHover& h = doc->inkHover;
	h.on = true;
	h.page = page;
	h.x = x;
	h.y = y;
	h.tool = tool;
	h.color = color && cl::ink::isToken(color) ? color : "ink";
	h.width = width;
}

void cl_ink_hover_clear(CLDocument* doc) {
	if (doc) doc->inkHover = InkHover();
}

int cl_ink_live_page(const CLDocument* doc) {
	if (doc == nullptr) return -1;
	if (doc->ink.active) return doc->ink.page;
	if (doc->inkHover.on) return doc->inkHover.page;
	return -1;
}

void cl_ink_draw_live(CLDocument* doc, CGContextRef ctx, double backingScale,
                      double originX, double originY, double unitsPerPoint, bool dark, bool projector) {
	if (doc == nullptr || ctx == nullptr || unitsPerPoint <= 0) return;
	CGContextSaveGState(ctx);
	CGContextScaleCTM(ctx, 1.0 / backingScale, 1.0 / backingScale);
	const float scale = (float)(backingScale / unitsPerPoint);
	cl::render::Transform t;
	t.a = scale; t.b = 0; t.c = 0; t.d = -scale;
	t.e = (float)(-originX * scale); t.f = (float)(originY * scale);
	cl::mac::CGScene scene(ctx);
	scene.setViewport(t);
	const int look = dark ? kInkLookDark : kInkLookLight;
	const InkGesture& g = doc->ink;
	if (g.active) {
		// What's captured so far, as drawn: unsimplified, quantized.
		PageInk live;
		live.strokes = g.parts;
		if (!g.xy.empty()) {
			InkStroke s;
			s.tool = toolName(g.tool);
			s.color = g.color;
			s.widthCenti = std::max(cl::ink::kWidthMinCenti, std::min(cl::ink::kWidthMaxCenti, (int)cl::ink::quantize(g.width)));
			for (double v : g.xy) s.xy.push_back(cl::ink::quantize(v));
			if (g.pressure) s.pressure = cl::ink::encodePressure(g.pr);
			live.strokes.push_back(std::move(s));
		}
		clDrawInkLayer(scene, live, true, look, scale, projector);
		clDrawInkLayer(scene, live, false, look, scale, projector);
	} else if (doc->inkHover.on) {
		const InkHover& h = doc->inkHover;
		const cl::render::Point at((float)h.x, (float)h.y);
		if (h.tool == CL_INK_ERASER) {
			cl::render::Stroke ring(dark ? Color(1, 1, 1, 0.7f) : Color(0, 0, 0, 0.55f), (float)backingScale * 1.25f);
			scene.strokeCircle(at, (float)h.width, ring);
		} else {
			Color c = inkColour(h.color, h.tool == CL_INK_HIGHLIGHTER, look);
			c.a *= 0.5f;
			const float minPx = projector ? 3.0f : 1.0f;
			scene.fillCircle(at, std::max((float)h.width * scale, minPx) / scale / 2, c);
		}
	}
	CGContextRestoreGState(ctx);
}

void cl_ink_color(const char* token, int tool, int look, double* r, double* g, double* b, double* a) {
	const Color c = inkColour(token ? token : "", tool == CL_INK_HIGHLIGHTER, std::max(0, std::min(3, look)));
	if (r) *r = c.r;
	if (g) *g = c.g;
	if (b) *b = c.b;
	if (a) *a = c.a;
}

// ---- Notes ----------------------------------------------------------------------------

const char* cl_notes(const CLDocument* doc) {
	if (doc == nullptr) return "";
	CLDocument* d = const_cast<CLDocument*>(doc);
	d->notesScratch = doc->circuit.circuitNotes;
	return d->notesScratch.c_str();
}

void cl_notes_set(CLDocument* doc, const char* utf8) {
	if (doc == nullptr) return;
	const std::string n = cl::ink::normalizeNotes(utf8 ? utf8 : "", cl::ink::kWriteNotesChars);
	if (n == doc->circuit.circuitNotes) return;
	doc->circuit.circuitNotes = n;
	doc->edited = true;
}

// ---- Saving for others -------------------------------------------------------------------

const char* cl_document_save_text_ex(CLDocument* doc, int flags) {
	static std::string text;
	text.clear();
	if (doc == nullptr) return "";
	std::vector<GUICanvas*> pages;
	for (auto& p : doc->pages) pages.push_back(p.get());
	CircuitParse writer(pages);
	text = writer.textV3(pages, !(flags & CL_SAVE_NO_NOTES), !(flags & CL_SAVE_NO_INK));
	return text.c_str();
}

}  // extern "C"
