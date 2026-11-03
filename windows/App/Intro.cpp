// The intro, as the Mac app's (Intro.swift), moment for moment. About 5
// seconds:
//
//   0.00-0.45  over the circuit window, the dark canvas fades in from
//              nothing, its grid and the two input pins easing in with it.
//   0.40-1.96  two wires trace in from their pins, a bright head running
//              along them and slowing into each end; the letters of the
//              icon snap into place as the wires reach them (the C, the D
//              -- an AND gate -- and the L, which the output runs down to
//              the LED); a seven-segment display draws in beside it.
//   1.99-2.58  a clock pulse runs through, the D lights, and as it lands
//              the LED lights (the sound's bell chord lands here) and the
//              display spells the version, a digit at a time.
//   2.58-3.05  a beat, everything lit, so it registers.
//   3.05-3.64  the LED blooms into the icon itself, which flies into the
//              launch screen's panel (Splash.cpp) as that fades in over it,
//              part-way into its own appearance: its traces drawing in, its
//              name writing itself in, its bar filling ...
//   3.52-4.42  ... and its sweep of light, at full length, as on any launch;
//   4.42-4.97  then it dissolves into the window (and the welcome, or What's
//              New, on top), a crossfade.
//
// Once drawn, the wires are the canvas's own: lines as wide as the canvas
// draws them, in the dark canvas's low-wire colour with a dot where each
// meets its pin, and green while they're high, with the glow Simulation
// View gives a lit wire.
//
// On screen it's a layered window laid exactly over the circuit window (still
// hidden), drawn with Direct2D every timer tick and faded as a whole; the
// launch screen's own window stays above it. A key or click on it skips.

#include "Intro.h"
#include "Images.h"
#include "Welcome.h"
#include "Window.h"

#include <d2d1_1.h>
#include <dwmapi.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <vector>

namespace intro {

double inOut(double u) {
	const double c = std::min(1.0, std::max(0.0, u));
	return c < 0.5 ? 2 * c * c : 1 - std::pow(-2 * c + 2, 2) / 2;
}

double fadeInShown(double t) { return inOut(t / kFadeIn); }

namespace {

// ---- Time ----------------------------------------------------------------------

double clamp01(double x) { return std::min(1.0, std::max(0.0, x)); }
// 0...1 between a and b, soft at both ends.
double smooth(double t, double a, double b) { const double c = clamp01((t - a) / (b - a)); return c * c * (3 - 2 * c); }
// 0...1 between a and b, fast out of the gate.
double ease(double t, double a, double b) { const double c = clamp01((t - a) / (b - a)); return 1 - std::pow(1 - c, 3); }
// A signal's run along a wire: away gently, then slowing all the way into
// the far end (an ease out, never past it).
double run(double t, double a, double b) {
	const double c = clamp01((t - a) / (b - a));
	const double lift = c * c * (3 - 2 * c), land = 1 - std::pow(1 - c, 3.2);
	return lift * 0.25 + land * 0.75;
}
// A snap into place: from small up past full size and back.
double pop(double t, double at, double from = 0.6) {
	const double u = clamp01((t - at) / 0.3);
	if (u <= 0) return from;
	const double s = 1 - std::pow(1 - u, 3);
	return from + (1 - from) * s + 0.22 * std::sin(u * M_PI) * (1 - u);
}

// ---- The look ------------------------------------------------------------------

D2D1_COLOR_F rgb(double r, double g, double b, double a = 1) { return D2D1::ColorF((float)r, (float)g, (float)b, (float)a); }
D2D1_COLOR_F withAlpha(D2D1_COLOR_F c, double a) { c.a = (float)(c.a * a); return c; }

const D2D1_COLOR_F kNeon = rgb(0.22, 1.0, 0.42);
const D2D1_COLOR_F kHead = rgb(0.82, 1.0, 0.86);
const D2D1_COLOR_F kSilver = rgb(0.86, 0.9, 0.88);
const D2D1_COLOR_F kGridMinor = rgb(0.16, 0.17, 0.20), kGridMajor = rgb(0.23, 0.25, 0.29);
const D2D1_COLOR_F kLedCore = rgb(0.85, 1, 0.9);
// The dark canvas (Palette::canvas, the Mac's Graphite theme).
D2D1_COLOR_F canvasColor() { return d2dColor(Palette{ true, false }.canvas()); }

// How the canvas draws a wire: a line `width` points wide, butt ends, square
// bends, a dot of `dot` points' radius where it meets each pin (when the
// dots are on), in the dark canvas's low-wire colour.
struct WireLook {
	float width = 1;
	float dot = 0.18f / 0.05f;   // wireDotSize (world units) at the canvas's opening zoom
	bool dots = true;
	D2D1_COLOR_F low = rgb(0.62, 0.67, 0.76);

	static WireLook current() {
		const Prefs& p = prefs();
		WireLook l;
		l.width = (float)p.wireScale();
		l.dot = (float)(p.wireDotSize / 0.05);
		l.dots = p.wireDots;
		static const double colors[][3] = { { 0.62, 0.67, 0.76 }, { 0.47, 0.58, 0.84 }, { 0.86, 0.87, 0.90 } };
		l.low = p.lowWire >= 0 && p.lowWire < 3 ? rgb(colors[p.lowWire][0], colors[p.lowWire][1], colors[p.lowWire][2]) : rgb(0.35, 0.35, 0.35);
		return l;
	}
};

// ---- Shapes --------------------------------------------------------------------

typedef D2D1_POINT_2F Pt;
// A run of points, closed or not; a shape is a few of them in turn (as a
// path's subpaths), so a part of its length can be stroked as Core
// Animation's strokeStart and strokeEnd do.
struct Figure { std::vector<Pt> pts; bool closed = false; };
typedef std::vector<Figure> Shape;

float dist(Pt a, Pt b) { return std::hypot(b.x - a.x, b.y - a.y); }
float length(const Figure& f) {
	float l = 0;
	for (size_t i = 1; i < f.pts.size(); i++) l += dist(f.pts[i - 1], f.pts[i]);
	if (f.closed && f.pts.size() > 1) l += dist(f.pts.back(), f.pts.front());
	return l;
}
float length(const Shape& s) { float l = 0; for (const Figure& f : s) l += length(f); return l; }

// An arc's points (degrees, y down, as the Mac's paths in the icon's units).
void arc(std::vector<Pt>& out, float cx, float cy, float r, float from, float to) {
	const int n = std::max(8, (int)std::ceil(std::fabs(to - from) / 2));
	for (int i = 0; i <= n; i++) {
		const double a = (from + (to - from) * i / n) * M_PI / 180;
		out.push_back(D2D1::Point2F((float)(cx + r * std::cos(a)), (float)(cy + r * std::sin(a))));
	}
}

ID2D1PathGeometry* newPath(ID2D1GeometrySink** sink) {
	ID2D1PathGeometry* g = nullptr;
	if (FAILED(d2dFactory()->CreatePathGeometry(&g)) || g == nullptr) return nullptr;
	if (FAILED(g->Open(sink))) { g->Release(); return nullptr; }
	return g;
}

// The part of `s` from `a` to `b` (fractions of its whole length), to
// stroke. Null when there's none of it.
ID2D1PathGeometry* part(const Shape& s, double a, double b) {
	const float total = length(s);
	if (total <= 0 || b <= a || b <= 0) return nullptr;
	const float A = (float)(std::max(0.0, a) * total), B = (float)(std::min(1.0, b) * total);
	if (B <= A) return nullptr;
	ID2D1GeometrySink* sink = nullptr;
	ID2D1PathGeometry* g = newPath(&sink);
	if (g == nullptr) return nullptr;
	float base = 0;
	for (const Figure& f : s) {
		const float L = length(f);
		const float lo = std::max(A, base), hi = std::min(B, base + L);
		if (hi > lo && f.pts.size() > 1) {
			if (f.closed && lo <= base + 0.01f && hi >= base + L - 0.01f) {
				// All of it: closed, so its corners all join.
				sink->BeginFigure(f.pts[0], D2D1_FIGURE_BEGIN_HOLLOW);
				for (size_t i = 1; i < f.pts.size(); i++) sink->AddLine(f.pts[i]);
				sink->EndFigure(D2D1_FIGURE_END_CLOSED);
			} else {
				std::vector<Pt> pts = f.pts;
				if (f.closed) pts.push_back(f.pts[0]);
				bool begun = false;
				float at = base;
				for (size_t i = 1; i < pts.size(); i++) {
					const float l = dist(pts[i - 1], pts[i]);
					const float s0 = at, s1 = at + l;
					at = s1;
					if (s1 <= lo || s0 >= hi || l <= 0) continue;
					auto point = [&](float d) {
						const float u = std::min(1.0f, std::max(0.0f, (d - s0) / l));
						return D2D1::Point2F(pts[i - 1].x + (pts[i].x - pts[i - 1].x) * u, pts[i - 1].y + (pts[i].y - pts[i - 1].y) * u);
					};
					if (!begun) { sink->BeginFigure(point(std::max(lo, s0)), D2D1_FIGURE_BEGIN_HOLLOW); begun = true; }
					sink->AddLine(point(std::min(hi, s1)));
				}
				if (begun) sink->EndFigure(D2D1_FIGURE_END_OPEN);
			}
		}
		base += L;
	}
	sink->Close();
	sink->Release();
	return g;
}

// `s` to fill, its figures closed (overlaps cut out, even-odd, or not).
ID2D1PathGeometry* fillOf(const Shape& s, bool evenOdd) {
	ID2D1GeometrySink* sink = nullptr;
	ID2D1PathGeometry* g = newPath(&sink);
	if (g == nullptr) return nullptr;
	sink->SetFillMode(evenOdd ? D2D1_FILL_MODE_ALTERNATE : D2D1_FILL_MODE_WINDING);
	for (const Figure& f : s) {
		if (f.pts.size() < 2) continue;
		sink->BeginFigure(f.pts[0], D2D1_FIGURE_BEGIN_FILLED);
		for (size_t i = 1; i < f.pts.size(); i++) sink->AddLine(f.pts[i]);
		sink->EndFigure(D2D1_FIGURE_END_CLOSED);
	}
	sink->Close();
	sink->Release();
	return g;
}

void release(ID2D1PathGeometry*& g) { if (g) g->Release(); g = nullptr; }

// ---- The scene -----------------------------------------------------------------

// What's made once for a render target: brushes and the icon.
struct Paint {
	ID2D1RenderTarget* owner = nullptr;
	ID2D1SolidColorBrush* solid = nullptr;
	ID2D1RadialGradientBrush* glow = nullptr;      // a soft green glow, white in the middle
	ID2D1RadialGradientBrush* bloom = nullptr;     // the LED's flash
	ID2D1RadialGradientBrush* vignette = nullptr;  // darker towards the edges
	ID2D1Bitmap* icon = nullptr;

	void release() {
		if (solid) solid->Release();
		if (glow) glow->Release();
		if (bloom) bloom->Release();
		if (vignette) vignette->Release();
		if (icon) icon->Release();
		*this = Paint();
	}
};

ID2D1RadialGradientBrush* radial(ID2D1RenderTarget* rt, const std::vector<D2D1_GRADIENT_STOP>& stops) {
	ID2D1GradientStopCollection* c = nullptr;
	ID2D1RadialGradientBrush* b = nullptr;
	if (SUCCEEDED(rt->CreateGradientStopCollection(stops.data(), (UINT32)stops.size(), &c)))
		rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, 0), 1, 1), c, &b);
	if (c) c->Release();
	return b;
}

ID2D1StrokeStyle* strokeStyle(D2D1_CAP_STYLE cap, D2D1_LINE_JOIN join) {
	ID2D1StrokeStyle* s = nullptr;
	d2dFactory()->CreateStrokeStyle(D2D1::StrokeStyleProperties(cap, cap, cap, join, 10), nullptr, 0, &s);
	return s;
}

class Scene {
public:
	Scene(float w, float h, Pt cardIcon, const WireLook& look);
	~Scene();
	void draw(ID2D1RenderTarget* rt, double t);
	void releasePaint() { paint.release(); }

	static const float kCardIconSize;

private:
	struct Wire {
		Shape path;            // one open figure
		std::vector<Pt> pts;
		std::vector<float> lens;   // running length at each point
		double d0, d1, p0, p1;     // drawn in, then high behind the clock pulse
		float length = 1;
	};
	struct Letter {
		Shape outline;
		ID2D1PathGeometry* fill = nullptr;
		Pt center;
		double popAt, tr0, tr1;
	};
	struct Digit {
		ID2D1PathGeometry* off = nullptr;
		ID2D1PathGeometry* on = nullptr;
		double at;
	};

	float W, H, k;
	Pt cardIcon;
	WireLook look;
	Paint paint;
	std::vector<Wire> wires;
	std::vector<Letter> letters;
	std::vector<Digit> digits;
	ID2D1PathGeometry* gateInside = nullptr;
	Shape housing;
	ID2D1PathGeometry* housingFill = nullptr;
	ID2D1StrokeStyle* roundAll = nullptr;    // round ends and joins (the trail)
	ID2D1StrokeStyle* roundJoin = nullptr;   // butt ends, round joins (the outlines)

	// Icon units (512 square, y down, the circuit's middle at 300, 247) to points.
	Pt p(float x, float y) const { return D2D1::Point2F(W / 2 + (x - 300) * k, H / 2 + (y - 247) * k); }
	Shape mapped(const Shape& s) const {
		Shape out = s;
		for (Figure& f : out)
			for (Pt& q : f.pts) q = p(q.x, q.y);
		return out;
	}
	void prepare(ID2D1RenderTarget* rt);
	void buildDisplay();
	void drawWire(ID2D1RenderTarget* rt, const Wire& w, double t);
	Pt pointOn(const Wire& w, double f) const;
	void stroke(ID2D1RenderTarget* rt, ID2D1Geometry* g, D2D1_COLOR_F c, float width, ID2D1StrokeStyle* style = nullptr) {
		if (g == nullptr || c.a <= 0.001f) return;
		paint.solid->SetColor(c);
		rt->DrawGeometry(g, paint.solid, width, style);
	}
	void fill(ID2D1RenderTarget* rt, ID2D1Geometry* g, D2D1_COLOR_F c) {
		if (g == nullptr || c.a <= 0.001f) return;
		paint.solid->SetColor(c);
		rt->FillGeometry(g, paint.solid);
	}
	void strokePart(ID2D1RenderTarget* rt, const Shape& s, double a, double b, D2D1_COLOR_F c, float width, ID2D1StrokeStyle* style = nullptr) {
		if (c.a <= 0.001f) return;
		ID2D1PathGeometry* g = part(s, a, b);
		stroke(rt, g, c, width, style);
		release(g);
	}
	void glowAt(ID2D1RenderTarget* rt, ID2D1RadialGradientBrush* b, Pt c, float rx, float ry, double opacity) {
		if (b == nullptr || opacity <= 0.002 || rx <= 0 || ry <= 0) return;
		b->SetCenter(c);
		b->SetRadiusX(rx);
		b->SetRadiusY(ry);
		b->SetOpacity((float)opacity);
		rt->FillEllipse(D2D1::Ellipse(c, rx, ry), b);
	}
};

const float Scene::kCardIconSize = 128;

Scene::Scene(float w, float h, Pt icon, const WireLook& l) : W(w), H(h), cardIcon(icon), look(l) {
	// The circuit and its display span about 580 units across.
	k = std::min(W * 0.55f / 580, H * 0.84f / 512);
	roundAll = strokeStyle(D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
	roundJoin = strokeStyle(D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN_ROUND);

	// The AND gate's inside, lit when its output goes high (behind all).
	{
		Figure f;
		f.closed = true;
		f.pts = { D2D1::Point2F(220, 148) };
		arc(f.pts, 228, 247, 99, -90, 90);
		f.pts.push_back(D2D1::Point2F(220, 346));
		gateInside = fillOf(mapped({ f }), false);
	}

	// The wires (in icon units), routed as the canvas routes them, square
	// bends: two inputs through the C into the D (an AND gate), and its
	// output down the L to the LED.
	auto wire = [&](std::vector<Pt> pts, double d0, double d1, double p0, double p1) {
		Wire w;
		for (Pt& q : pts) q = p(q.x, q.y);
		w.pts = pts;
		w.lens.push_back(0);
		for (size_t i = 1; i < pts.size(); i++) w.lens.push_back(w.lens.back() + dist(pts[i - 1], pts[i]));
		w.length = std::max(1.0f, w.lens.back());
		Figure f;
		f.pts = pts;
		w.path = { f };
		w.d0 = d0; w.d1 = d1; w.p0 = p0; w.p1 = p1;
		wires.push_back(w);
	};
	using D2D1::Point2F;
	wire({ Point2F(20, 217.5f), Point2F(124, 217.5f), Point2F(124, 206), Point2F(196, 206) }, 0.40, 1.29, 1.99, 2.29);
	wire({ Point2F(20, 253.5f), Point2F(132, 253.5f), Point2F(132, 288), Point2F(196, 288) }, 0.45, 1.31, 1.99, 2.29);
	wire({ Point2F(351, 247), Point2F(376, 247), Point2F(376, 316), Point2F(486, 316) }, 1.43, 1.96, 2.31, kLedAt);

	// The letters, as outlines of the icon's: the C (its stroke's outline,
	// inside edge first, as the Mac's path runs) ...
	auto letter = [&](const Shape& icon, double popAt, double tr0, double tr1) {
		Letter l;
		l.outline = mapped(icon);
		l.fill = fillOf(l.outline, true);
		float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
		for (const Figure& f : l.outline)
			for (const Pt& q : f.pts) { x0 = std::min(x0, q.x); y0 = std::min(y0, q.y); x1 = std::max(x1, q.x); y1 = std::max(y1, q.y); }
		l.center = Point2F((x0 + x1) / 2, (y0 + y1) / 2);
		l.popAt = popAt; l.tr0 = tr0; l.tr1 = tr1;
		letters.push_back(l);
	};
	{
		Figure c;
		c.closed = true;
		arc(c.pts, 156, 247, 88 - 12, 55, 305);
		arc(c.pts, 156, 247, 88 + 12, 305, 55);
		letter({ c }, 0.64, 0.54, 1.12);
	}
	// ... the D, which is an AND gate (its outside, then its inside) ...
	{
		Shape d;
		const float xs[] = { 196, 220 }, rs[] = { 123, 99 };
		for (int i = 0; i < 2; i++) {
			Figure f;
			f.closed = true;
			f.pts = { Point2F(xs[i], 247 - rs[i]) };
			arc(f.pts, 228, 247, rs[i], -90, 90);
			f.pts.push_back(Point2F(xs[i], 247 + rs[i]));
			d.push_back(f);
		}
		letter(d, 1.26, 1.19, 1.58);
	}
	// ... and the L, leaning as the icon's does.
	{
		Figure f;
		f.closed = true;
		f.pts = { Point2F(395, 172), Point2F(419, 172), Point2F(377, 325), Point2F(470, 325), Point2F(455, 350), Point2F(345, 350) };
		letter({ f }, 1.55, 1.48, 1.89);
	}

	buildDisplay();
}

Scene::~Scene() {
	paint.release();
	release(gateInside);
	release(housingFill);
	for (Letter& l : letters) release(l.fill);
	for (Digit& d : digits) { release(d.off); release(d.on); }
	if (roundAll) roundAll->Release();
	if (roundJoin) roundJoin->Release();
}

// A seven-segment display beside the LED, in the outlines' style, that
// spells the version as the LED lights, a digit at a time.
void Scene::buildDisplay() {
	const std::string version = whatsnew::kVersion;
	int count = 0;
	for (char c : version) count += c >= '0' && c <= '9';
	count = std::max(1, count);
	const float cellW = 24, gap = 16, h = 46, x0 = 528, y0 = 293;
	const float boxW = count * cellW + (count - 1) * gap + 22;
	{
		// The housing: a rounded box, drawn in from its right side's middle.
		const float bx = x0 - 11, by = y0 - 12, bw = boxW, bh = h + 24, r = 7;
		Figure f;
		f.closed = true;
		f.pts.push_back(D2D1::Point2F(bx + bw, by + bh / 2));
		arc(f.pts, bx + bw - r, by + bh - r, r, 0, 90);
		arc(f.pts, bx + r, by + bh - r, r, 90, 180);
		arc(f.pts, bx + r, by + r, r, 180, 270);
		arc(f.pts, bx + bw - r, by + r, r, 270, 360);
		housing = mapped({ f });
		housingFill = fillOf(housing, false);
	}
	// The segments of one digit (a to g), as hexagonal bars, leaning a
	// little, as a display's digits do (about the baseline).
	const float t = 4.6f;
	const float base = y0 + h;
	auto lean = [&](Pt q) { return p(q.x - 0.1f * (q.y - base), q.y); };
	auto bar = [&](Pt a, Pt b) {
		Figure f;
		f.closed = true;
		using D2D1::Point2F;
		if (a.y == b.y)   // across
			f.pts = { Point2F(a.x + t / 2, a.y), Point2F(a.x + t, a.y - t / 2), Point2F(b.x - t, b.y - t / 2),
			          Point2F(b.x - t / 2, b.y), Point2F(b.x - t, b.y + t / 2), Point2F(a.x + t, a.y + t / 2) };
		else              // down
			f.pts = { Point2F(a.x, a.y + t / 2), Point2F(a.x + t / 2, a.y + t), Point2F(b.x + t / 2, b.y - t),
			          Point2F(b.x, b.y - t / 2), Point2F(b.x - t / 2, b.y - t), Point2F(a.x - t / 2, a.y + t) };
		for (Pt& q : f.pts) q = lean(q);
		return f;
	};
	auto segments = [&](float x) {
		using D2D1::Point2F;
		const float l = x, r = x + cellW, top = y0, mid = y0 + h / 2, bottom = y0 + h;
		return std::vector<Figure>{ bar(Point2F(l, top), Point2F(r, top)),          // a
		                            bar(Point2F(r, top), Point2F(r, mid)),          // b
		                            bar(Point2F(r, mid), Point2F(r, bottom)),       // c
		                            bar(Point2F(l, bottom), Point2F(r, bottom)),    // d
		                            bar(Point2F(l, mid), Point2F(l, bottom)),       // e
		                            bar(Point2F(l, top), Point2F(l, mid)),          // f
		                            bar(Point2F(l, mid), Point2F(r, mid)) };        // g
	};
	static const char* lit[10] = { "abcdef", "bc", "abged", "abgcd", "fgbc", "afgcd", "afedcg", "abc", "abcdefg", "abcdfg" };
	float x = x0;
	int order = 0;
	for (char ch : version) {
		Shape off, on;
		if (ch >= '0' && ch <= '9') {
			const std::vector<Figure> segs = segments(x);
			const std::string which = lit[ch - '0'];
			for (int i = 0; i < 7; i++) (which.find((char)('a' + i)) != std::string::npos ? on : off).push_back(segs[i]);
			x += cellW + gap;
		} else if (ch == '.') {
			Figure dp;
			dp.closed = true;
			for (int i = 0; i < 24; i++) {
				const double a = i * 2 * M_PI / 24;
				dp.pts.push_back(lean(D2D1::Point2F((float)(x - gap / 2 + 3 * std::cos(a)), (float)(y0 + h + 3 * std::sin(a)))));
			}
			on.push_back(dp);
		} else {
			continue;
		}
		Digit d;
		d.off = off.empty() ? nullptr : fillOf(off, false);
		d.on = fillOf(on, false);
		d.at = kLedAt + 0.1 * order;
		digits.push_back(d);
		order++;
	}
}

void Scene::prepare(ID2D1RenderTarget* rt) {
	if (paint.owner == rt) return;
	paint.release();
	paint.owner = rt;
	rt->CreateSolidColorBrush(kNeon, &paint.solid);
	paint.glow = radial(rt, { { 0, rgb(1, 1, 1, 1) }, { 0.12f, rgb(0.6, 1, 0.72, 0.85) }, { 0.4f, rgb(0.22, 1, 0.42, 0.25) },
	                          { 1, rgb(0.22, 1, 0.42, 0) } });
	paint.bloom = radial(rt, { { 0, rgb(0.9, 1, 0.93, 1) }, { 0.05f, rgb(0.45, 1, 0.6, 0.7) }, { 0.22f, rgb(0.22, 1, 0.42, 0.16) },
	                           { 0.5f, rgb(0.22, 1, 0.42, 0.04) }, { 1, rgb(0.22, 1, 0.42, 0) } });
	paint.vignette = radial(rt, { { 0, rgb(0, 0, 0, 0) }, { 0.45f, rgb(0, 0, 0, 0.02) }, { 0.8f, rgb(0, 0, 0, 0.16) }, { 1, rgb(0, 0, 0, 0.3) } });
	// The launch screen's icon (the Mac's LaunchIcon.png, 512 pixels).
	if (IWICBitmap* src = images::loadResource(2, 512, 512)) {
		rt->CreateBitmapFromWicBitmap(src, D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96),
		                              &paint.icon);
		src->Release();
	}
}

Pt Scene::pointOn(const Wire& w, double f) const {
	const float target = (float)(f * w.length);
	for (size_t i = 1; i < w.pts.size(); i++) {
		if (w.lens[i] >= target) {
			const float span = w.lens[i] - w.lens[i - 1];
			const float u = span > 0 ? (target - w.lens[i - 1]) / span : 0;
			return D2D1::Point2F(w.pts[i - 1].x + (w.pts[i].x - w.pts[i - 1].x) * u, w.pts[i - 1].y + (w.pts[i].y - w.pts[i - 1].y) * u);
		}
	}
	return w.pts.back();
}

// A wire at t: laid in behind its head, which slows into the far end and
// settles there as its trail draws in after it; then the same again,
// brighter, for the clock pulse, the wire high behind it.
void Scene::drawWire(ID2D1RenderTarget* rt, const Wire& w, double t) {
	const double d = run(t, w.d0, w.d1), q = run(t, w.p0, w.p1);
	const float lw = look.width;
	// At rest, as the canvas draws it ...
	if (d > 0) strokePart(rt, w.path, 0, d, look.low, lw);
	// (the dots where it meets its pins: the near one as the run leaves it,
	// the far one as it lands, low and then lit)
	auto dots = [&](bool high) {
		if (!look.dots) return;
		for (int end = 0; end < 2; end++) {
			const double at = high ? (end == 0 ? w.p0 : w.p1 - 0.02) : (end == 0 ? w.d0 : w.d1 - 0.02);
			const double o = smooth(t, at, at + 0.08);
			if (o <= 0) continue;
			paint.solid->SetColor(withAlpha(high ? kNeon : look.low, o));
			rt->FillEllipse(D2D1::Ellipse(end == 0 ? w.pts.front() : w.pts.back(), look.dot, look.dot), paint.solid);
		}
	};
	dots(false);
	// ... high, in green with Simulation View's glow ...
	if (q > 0) {
		ID2D1PathGeometry* g = part(w.path, 0, q);
		stroke(rt, g, withAlpha(kNeon, 0.10), lw + 7);
		stroke(rt, g, withAlpha(kNeon, 0.22), lw + 3);
		stroke(rt, g, kNeon, lw);
		release(g);
	}
	dots(true);
	// ... and the head's trail and glow, on top: whichever run is going (or
	// has just landed), the pulse, else the drawing in.
	const bool pulsing = t >= w.p0;
	const double f = pulsing ? q : d, start = pulsing ? w.p0 : w.d0, end = pulsing ? w.p1 : w.d1;
	// After landing, the trail draws in after the head and it all fades.
	const double settle = ease(t, end, end + 0.22);
	const double shown = smooth(t, start, start + 0.05) * (1 - settle);
	if (shown <= 0) return;
	// A trail about 90 points long, or the wire, whichever is shorter.
	const double trail = std::min(1.0, 90.0 / w.length) * (1 - settle);
	strokePart(rt, w.path, std::max(0.0, f - trail * 0.6), f, withAlpha(kNeon, 0.28 * shown), lw + 5, roundAll);
	const int n = 6;
	for (int i = 0; i < n; i++) {
		// The steps overlap at the head: brightest there, fading back.
		const double share = double(n - i) / n;
		strokePart(rt, w.path, std::max(0.0, f - trail * share), f, withAlpha(kHead, (0.22 + 0.13 * i) * shown * (pulsing ? 1 : 0.8)),
		           lw + 0.6f, roundAll);
	}
	// The head: a soft light that narrows as it lands.
	const double sc = (pulsing ? 1.15 : 0.9) * (1 - 0.45 * settle);
	glowAt(rt, paint.glow, pointOn(w, f), (float)(15 * sc), (float)(15 * sc), shown);
}

// Everything where it is t seconds in.
void Scene::draw(ID2D1RenderTarget* rt, double t) {
	prepare(rt);
	if (paint.solid == nullptr) return;
	D2D1_MATRIX_3X2_F base;
	rt->GetTransform(&base);
	const D2D1_RECT_F all = D2D1::RectF(0, 0, W, H);
	const D2D1_COLOR_F canvas = canvasColor();

	// The canvas itself (it fades in with the window as a whole:
	// fadeInShown); its grid comes up a little after, minor and major lines
	// as the app draws it.
	paint.solid->SetColor(canvas);
	rt->FillRectangle(all, paint.solid);
	const double gridShown = smooth(t, 0.1, 0.75) * 0.85;
	if (gridShown > 0) {
		const float step = 20, cx = W / 2, cy = H / 2;
		for (int pass = 0; pass < 2; pass++) {
			paint.solid->SetColor(withAlpha(pass == 0 ? kGridMinor : kGridMajor, gridShown));
			const float width = pass == 0 ? 0.5f : 0.75f;
			for (int i = -(int)(cx / step) - 1; i <= (int)(cx / step) + 1; i++)
				if ((i % 5 == 0) == (pass == 1)) rt->DrawLine(D2D1::Point2F(cx + i * step, 0), D2D1::Point2F(cx + i * step, H), paint.solid, width);
			for (int i = -(int)(cy / step) - 1; i <= (int)(cy / step) + 1; i++)
				if ((i % 5 == 0) == (pass == 1)) rt->DrawLine(D2D1::Point2F(0, cy + i * step), D2D1::Point2F(W, cy + i * step), paint.solid, width);
		}
	}
	// Darker towards the edges, so the eye goes to the middle.
	if (paint.vignette) {
		paint.vignette->SetCenter(D2D1::Point2F(W / 2, H / 2));
		paint.vignette->SetRadiusX(W / 2);
		paint.vignette->SetRadiusY(H / 2);
		paint.vignette->SetOpacity(1);
		rt->FillRectangle(all, paint.vignette);
	}

	const double bloomAt = kBloomAt;
	const Pt ledC = p(494, 316);
	const float lr = 8 * k;

	// The circuit (wires, pins, letters, LED), faded as one as the icon
	// arrives over it.
	const double circuitShown = 1 - smooth(t, bloomAt + 0.12, bloomAt + 0.32);
	if (circuitShown > 0.002) {
		rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(),
		                                    (float)circuitShown),
		              nullptr);
		// The gate fires: its inside glows green.
		if (t >= 1.26) fill(rt, gateInside, withAlpha(kNeon, 0.08 + 0.32 * ease(t, 2.29, 2.45)));

		for (const Wire& w : wires) drawWire(rt, w, t);

		// The pins ease in with the canvas, just before the wires leave them.
		for (int i = 0; i < 2; i++) {
			const double at = 0.16 + 0.07 * i;
			const double o = smooth(t, at, at + 0.2);
			if (o <= 0) continue;
			const Pt c = p(14, i == 0 ? 217.5f : 253.5f);
			const float sc = (float)pop(t, at, 0.2);
			rt->SetTransform(D2D1::Matrix3x2F::Scale(sc, sc, c) * base);
			paint.solid->SetColor(withAlpha(canvas, o));
			rt->FillEllipse(D2D1::Ellipse(c, 6 * k, 6 * k), paint.solid);
			paint.solid->SetColor(withAlpha(kNeon, o));
			rt->DrawEllipse(D2D1::Ellipse(c, 6 * k, 6 * k), paint.solid, std::max(1.5f, 1.4f * k));
			rt->SetTransform(base);
		}

		for (const Letter& l : letters) {
			const double shown = smooth(t, l.popAt - 0.06, l.popAt + 0.04);
			if (shown <= 0) continue;
			const double tr = smooth(t, l.tr0, l.tr1);
			const float sc = (float)pop(t, l.popAt, 0.82);
			rt->SetTransform(D2D1::Matrix3x2F::Scale(sc, sc, l.center) * base);
			// The clock pulse catches the outlines as it passes, and they
			// stay lit until the bloom.
			const double glow = 0.22 + 0.4 * smooth(t, 2.16, 2.4) * (1 - smooth(t, bloomAt, bloomAt + 0.2));
			strokePart(rt, l.outline, 0, tr, withAlpha(kNeon, glow * shown), 7 * k, roundJoin);
			// Filled faintly once outlined: silver glass.
			fill(rt, l.fill, withAlpha(kSilver, 0.13 * smooth(t, l.tr1 - 0.15, l.tr1 + 0.1) * shown));
			strokePart(rt, l.outline, 0, tr, withAlpha(kSilver, shown), std::max(1.4f, 1.5f * k), roundJoin);
			rt->SetTransform(base);
		}

		// The LED: in when the output reaches it, lit as the pulse lands
		// with a flash that settles, held lit, then swelling into the bloom.
		const double lit = ease(t, kLedAt, kLedAt + 0.06);
		const double fu = clamp01((t - kLedAt) / 0.45);
		const double flash = fu > 0 ? std::pow(1 - fu, 2) * std::min(1.0, fu / 0.08) : 0;
		const double g = 0.6 + 0.8 * lit + 0.55 * flash + 0.4 * smooth(t, bloomAt - 0.22, bloomAt);
		glowAt(rt, paint.glow, ledC, (float)(75 * k * g), (float)(75 * k * g), lit);
		const double ringShown = smooth(t, 1.88, 1.98);
		if (ringShown > 0) {
			const float sc = (float)pop(t, 1.91, 0.3);
			rt->SetTransform(D2D1::Matrix3x2F::Scale(sc, sc, ledC) * base);
			paint.solid->SetColor(withAlpha(canvas, ringShown));
			rt->FillEllipse(D2D1::Ellipse(ledC, lr, lr), paint.solid);
			paint.solid->SetColor(withAlpha(kNeon, ringShown));
			rt->DrawEllipse(D2D1::Ellipse(ledC, lr, lr), paint.solid, std::max(1.5f, 1.6f * k));
			rt->SetTransform(base);
		}
		if (lit > 0) {
			const float sc = (float)pop(t, kLedAt, 0.7);
			rt->SetTransform(D2D1::Matrix3x2F::Scale(sc, sc, ledC) * base);
			paint.solid->SetColor(withAlpha(kLedCore, lit));
			rt->FillEllipse(D2D1::Ellipse(ledC, lr - 2.5f * k, lr - 2.5f * k), paint.solid);
			rt->SetTransform(base);
		}
		rt->PopLayer();
	}

	// The seven-segment display, which stays a little longer: until the
	// launch screen's panel comes in.
	const double displayShown = smooth(t, 1.86, 2.0) * (1 - smooth(t, kCardIn, kCardIn + kCardFade));
	if (displayShown > 0.002) {
		rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(),
		                                    (float)displayShown),
		              nullptr);
		fill(rt, housingFill, withAlpha(kSilver, 0.06));
		strokePart(rt, housing, 0, smooth(t, 1.84, 2.13), kSilver, std::max(1.4f, 1.5f * k));
		// The version, a digit at a time, each flaring as it lights.
		for (const Digit& d : digits) {
			fill(rt, d.off, withAlpha(kNeon, 0.07));
			const double on = ease(t, d.at, d.at + 0.06);
			if (on <= 0) continue;
			stroke(rt, d.on, withAlpha(kNeon, 0.3 * on * (0.6 + 0.4 * (1 - smooth(t, d.at + 0.06, d.at + 0.36)))), 5, roundJoin);
			fill(rt, d.on, withAlpha(kNeon, on));
		}
		rt->PopLayer();
	}

	// The icon: over the circuit, its own art where the circuit was, then
	// flying to where the launch screen's own icon is, and handing over
	// (landed just as the panel is half in, so they don't double up).
	const double fly = smooth(t, bloomAt + 0.14, bloomAt + 0.44);
	const Pt bigAt = p(0, 0);
	const float bigSide = 512 * k, cardSide = kCardIconSize;
	auto mix = [&](float a, float b) { return (float)(a + (b - a) * fly); };
	const float side = mix(bigSide, cardSide);
	const float x = mix(bigAt.x, cardIcon.x - cardSide / 2), y = mix(bigAt.y, cardIcon.y - cardSide / 2);
	const Pt mid = D2D1::Point2F(x + side / 2, y + side / 2);
	// Gone into the panel's own icon as the panel finishes coming in.
	const double landed = kCardIn + kCardFade;
	const double iconShown = smooth(t, bloomAt + 0.05, bloomAt + 0.26) * (1 - smooth(t, landed - 0.14, landed));
	if (iconShown > 0.002) {
		glowAt(rt, paint.glow, mid, side * 1.9f / 2, side * 1.9f / 2, iconShown * (0.55 + 0.25 * (1 - fly)));
		if (paint.icon) rt->DrawBitmap(paint.icon, D2D1::RectF(x, y, x + side, y + side), (float)iconShown, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
	}

	// The bloom: a flash out of the LED that fills the canvas.
	const double bl = ease(t, bloomAt, bloomAt + 0.44);
	const double bs = 0.04 + 0.96 * bl;
	const double bloomShown = 0.9 * smooth(t, bloomAt - 0.02, bloomAt + 0.08) * (1 - smooth(t, bloomAt + 0.14, bloomAt + 0.56));
	const float bloomR = (float)(std::max(W, H) * 1.6 / 2 * bs);
	glowAt(rt, paint.bloom, ledC, bloomR, bloomR, bloomShown);
}

// ---- Settings and the environment -------------------------------------------------

bool envSet(const wchar_t* name) { return GetEnvironmentVariableW(name, nullptr, 0) > 0; }
std::string envValue(const wchar_t* name) {
	wchar_t buf[64] = L"";
	const DWORD n = GetEnvironmentVariableW(name, buf, 64);
	return n > 0 && n < 64 ? U(buf) : std::string();
}

// Never in the self-tests, the picture modes, CI or any run with a --
// option (they all are tests or pictures).
bool allowed() {
	for (const wchar_t* n : { L"CL_NO_INTRO", L"CL_NO_SPLASH", L"CI" })
		if (envSet(n)) return false;
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	bool option = false;
	for (int i = 1; argv && i < argc; i++)
		if (wcsncmp(argv[i], L"--", 2) == 0) option = true;
	if (argv) LocalFree(argv);
	return !option;
}

// Which intro the saved settings say this launch is owed (none in the usual
// case). The same tests as the launch screen's first-launch look and What's
// New, so it can't disagree with them.
Trigger owed() {
	const Prefs& p = prefs();
	if (!p.hasSeenWelcome && !p.firstLaunchPlayed && !p.introFirstLaunchPlayed) return Trigger::First;
	if (p.hasSeenWelcome && p.seenWhatsNew != whatsnew::kVersion && p.introUpdatePlayed != whatsnew::kVersion) return Trigger::Update;
	return Trigger::None;
}

// ---- Sound ------------------------------------------------------------------------

// FirstLaunch.wav (resource 3), as 16-bit PCM.
struct Wav {
	const unsigned char* pcm = nullptr;
	size_t bytes = 0;
	WORD channels = 0, block = 0, bits = 0;
	DWORD rate = 0;
};

bool loadWav(Wav& w) {
	HRSRC r = FindResourceW(appInstance(), MAKEINTRESOURCEW(3), L"WAVE");
	HGLOBAL g = r ? LoadResource(appInstance(), r) : nullptr;
	const unsigned char* d = g ? (const unsigned char*)LockResource(g) : nullptr;
	const DWORD size = r ? SizeofResource(appInstance(), r) : 0;
	if (d == nullptr || size < 12 || memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "WAVE", 4) != 0) return false;
	for (DWORD i = 12; i + 8 <= size;) {
		DWORD len = 0;
		memcpy(&len, d + i + 4, 4);
		if (memcmp(d + i, "fmt ", 4) == 0 && len >= 16 && i + 8 + 16 <= size) {
			WORD format = 0;
			memcpy(&format, d + i + 8, 2);
			memcpy(&w.channels, d + i + 10, 2);
			memcpy(&w.rate, d + i + 12, 4);
			memcpy(&w.block, d + i + 20, 2);
			memcpy(&w.bits, d + i + 22, 2);
			if (format != 1) return false;
		} else if (memcmp(d + i, "data", 4) == 0) {
			w.pcm = d + i + 8;
			w.bytes = std::min<size_t>(len, size - (i + 8));
		}
		i += 8 + len + (len & 1);
	}
	return w.pcm && w.bits == 16 && w.block == w.channels * 2 && w.rate > 0;
}

// What PlaySound is playing from memory: kept until the app quits (it reads
// them as it plays; a later one stops the one before).
std::vector<std::vector<unsigned char>>& soundBuffers() {
	static std::vector<std::vector<unsigned char>> all;
	return all;
}
double gSoundFrom = 0, gSoundStarted = -1;

// The sound from `from` seconds in: `seconds` of it (all the rest: 0),
// faded up over `fadeUp`, or from `gain` down to nothing (`fadeDown`).
bool playFrom(double from, double seconds, double fadeUp, bool fadeDown, double gain = 1) {
	Wav w;
	if (!loadWav(w)) return false;
	const size_t frames = w.bytes / w.block;
	const size_t first = std::min(frames, (size_t)(from * w.rate));
	size_t count = frames - first;
	if (seconds > 0) count = std::min(count, (size_t)(seconds * w.rate));
	if (count == 0) return false;
	std::vector<unsigned char> buf(44 + count * w.block);
	auto put32 = [&](size_t at, DWORD v) { memcpy(&buf[at], &v, 4); };
	auto put16 = [&](size_t at, WORD v) { memcpy(&buf[at], &v, 2); };
	memcpy(&buf[0], "RIFF", 4);
	put32(4, (DWORD)(buf.size() - 8));
	memcpy(&buf[8], "WAVEfmt ", 8);
	put32(16, 16);
	put16(20, 1);
	put16(22, w.channels);
	put32(24, w.rate);
	put32(28, w.rate * w.block);
	put16(32, w.block);
	put16(34, 16);
	memcpy(&buf[36], "data", 4);
	put32(40, (DWORD)(count * w.block));
	const int16_t* src = (const int16_t*)(w.pcm + first * w.block);
	int16_t* dst = (int16_t*)&buf[44];
	for (size_t f = 0; f < count; f++) {
		const double at = double(f) / w.rate;
		double g = gain;
		if (fadeUp > 0) g *= std::min(1.0, at / fadeUp);
		if (fadeDown) g *= std::max(0.0, 1 - double(f) / count);
		for (WORD c = 0; c < w.channels; c++) dst[f * w.channels + c] = (int16_t)std::lround(src[f * w.channels + c] * g);
	}
	soundBuffers().push_back(std::move(buf));
	return PlaySoundW((LPCWSTR)soundBuffers().back().data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT) != FALSE;
}

// ---- The window -------------------------------------------------------------------

struct Player {
	HWND hwnd = nullptr;
	HDC memDC = nullptr;
	HBITMAP dib = nullptr;
	HGDIOBJ oldBitmap = nullptr;
	int pw = 0, ph = 0;
	float scale = 1, corner = 0;
	ID2D1DCRenderTarget* rt = nullptr;
	Scene* scene = nullptr;
	double start = 0, skipAt = -1, freezeAt = -1;
	bool passThrough = false;

	bool makeTarget() {
		const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
			D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96 * scale, 96 * scale);
		return SUCCEEDED(d2dFactory()->CreateDCRenderTarget(&props, &rt));
	}

	bool makeSurface() {
		HDC screen = GetDC(nullptr);
		memDC = CreateCompatibleDC(screen);
		ReleaseDC(nullptr, screen);
		BITMAPINFO bi = {};
		bi.bmiHeader.biSize = sizeof bi.bmiHeader;
		bi.bmiHeader.biWidth = pw;
		bi.bmiHeader.biHeight = -ph;
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;
		void* bits = nullptr;
		dib = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
		if (dib == nullptr) return false;
		oldBitmap = SelectObject(memDC, dib);
		return makeTarget();
	}

	// How much of it shows: faded in as a whole, then out with the launch
	// screen (quickly, when skipped).
	double shown(double now) const {
		const double t = now - start;
		const double out = skipAt >= 0 ? inOut((now - skipAt) / kSkipFade) : inOut((t - kDissolveAt) / kFade);
		return fadeInShown(t) * (1 - out);
	}
	bool finished(double now) const {
		return skipAt >= 0 ? now - skipAt >= kSkipFade + 0.05 : now - start >= kTotal + 0.05;
	}

	void frame() {
		if (rt == nullptr || scene == nullptr) return;
		const double now = nowSeconds();
		double t = now - start;
		if (freezeAt >= 0) t = std::min(t, freezeAt);
		// Dissolving: the windows are coming in under it, and the pointer
		// goes through to them.
		if (!passThrough && (skipAt >= 0 || now - start >= kDissolveAt)) {
			passThrough = true;
			SetWindowLongPtrW(hwnd, GWL_EXSTYLE, GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_TRANSPARENT);
		}
		RECT r = { 0, 0, pw, ph };
		if (FAILED(rt->BindDC(memDC, &r))) return;
		rt->BeginDraw();
		rt->Clear(D2D1::ColorF(0, 0, 0, 0));
		ID2D1RoundedRectangleGeometry* shape = nullptr;
		const float w = pw / scale, h = ph / scale;
		if (corner > 0) {
			d2dFactory()->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(0, 0, w, h), corner, corner), &shape);
			if (shape) rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), shape), nullptr);
		}
		scene->draw(rt, t);
		if (shape) {
			rt->PopLayer();
			shape->Release();
		}
		if (rt->EndDraw() == D2DERR_RECREATE_TARGET) {
			scene->releasePaint();
			rt->Release();
			rt = nullptr;
			makeTarget();
			return;
		}
		POINT src = { 0, 0 };
		SIZE size = { pw, ph };
		const double a = shown(now);
		BLENDFUNCTION blend = { AC_SRC_OVER, 0, (BYTE)std::lround(255 * std::min(1.0, std::max(0.0, a))), AC_SRC_ALPHA };
		UpdateLayeredWindow(hwnd, nullptr, nullptr, &size, memDC, &src, 0, &blend, ULW_ALPHA);
	}

	void destroy() {
		if (hwnd) { KillTimer(hwnd, 1); DestroyWindow(hwnd); hwnd = nullptr; }
		delete scene;
		scene = nullptr;
		if (rt) { rt->Release(); rt = nullptr; }
		if (memDC) { SelectObject(memDC, oldBitmap); DeleteDC(memDC); memDC = nullptr; }
		if (dib) { DeleteObject(dib); dib = nullptr; }
	}
};

Player* g_player = nullptr;

LRESULT CALLBACK introProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
	switch (msg) {
	case WM_TIMER:
		if (g_player && h == g_player->hwnd) {
			Player* p = g_player;
			guarded("the intro", [&] {
				p->frame();
				if (p->finished(nowSeconds())) {
					g_player = nullptr;
					p->destroy();
					delete p;
				}
			});
		}
		return 0;
	// Any key or click skips it (and goes no further).
	case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: case WM_XBUTTONDOWN:
		guarded("the intro", [] { skip(); });
		return 0;
	case WM_SYSCHAR: case WM_CHAR: case WM_KEYUP: case WM_SYSKEYUP:
		return 0;
	case WM_SETCURSOR:
		SetCursor(LoadCursor(nullptr, IDC_ARROW));
		return TRUE;
	}
	return DefWindowProcW(h, msg, wp, lp);
}

UINT monitorDpi(HMONITOR m) {
	UINT x = 96, y = 96;
	typedef HRESULT(WINAPI * GetDpiForMonitorFn)(HMONITOR, int, UINT*, UINT*);
	static auto getDpi = (GetDpiForMonitorFn)(void*)GetProcAddress(LoadLibraryW(L"shcore.dll"), "GetDpiForMonitor");
	if (getDpi) getDpi(m, 0, &x, &y);
	return x;
}

bool windows11() {
	OSVERSIONINFOW v = { sizeof v };
	typedef LONG(WINAPI * RtlGetVersionFn)(OSVERSIONINFOW*);
	if (auto f = (RtlGetVersionFn)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")) f(&v);
	return v.dwMajorVersion == 10 && v.dwBuildNumber >= 22000;
}

// Where the circuit window will come in (screen pixels, as you'll see it:
// without Windows' invisible edge), and its scale. On the launch screen's
// screen when it's somewhere else (and can go without changing size).
RECT windowArea(HMONITOR splashScreen, float& scale) {
	CircuitWindow* w = circuitWindows().empty() ? nullptr : circuitWindows().front();
	MONITORINFO mi = { sizeof mi };
	if (w == nullptr) {
		GetMonitorInfoW(splashScreen, &mi);
		scale = monitorDpi(splashScreen) / 96.0f;
		const int ww = (int)(1280 * scale), wh = (int)(800 * scale);
		const RECT& a = mi.rcWork;
		const int x = (a.left + a.right - ww) / 2, y = (a.top + a.bottom - wh) / 2;
		return RECT{ x, y, x + ww, y + wh };
	}
	const HWND h = w->window();
	const UINT dpi = dpiOf(h);
	scale = dpi / 96.0f;
	HMONITOR m = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
	if (m != splashScreen && !w->opensMaximized() && monitorDpi(splashScreen) == dpi && GetMonitorInfoW(splashScreen, &mi)) {
		RECT r;
		GetWindowRect(h, &r);
		const RECT& a = mi.rcWork;
		const int ww = std::min<int>(r.right - r.left, a.right - a.left), wh = std::min<int>(r.bottom - r.top, a.bottom - a.top);
		SetWindowPos(h, nullptr, (a.left + a.right - ww) / 2, (a.top + a.bottom - wh) / 2, ww, wh, SWP_NOZORDER | SWP_NOACTIVATE);
		m = splashScreen;
	}
	if (w->opensMaximized()) {
		GetMonitorInfoW(m, &mi);
		return mi.rcWork;
	}
	RECT r, v;
	GetWindowRect(h, &r);
	if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &v, sizeof v)) && !EqualRect(&v, &r) && v.right > v.left &&
	    v.left >= r.left && v.right <= r.right && v.bottom <= r.bottom)
		return v;
	// (Hidden, Windows may not say: its edge is the frame the window keeps
	// on the left, right and bottom, WM_NCCALCSIZE.)
	const int fx = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
	const int fy = GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
	return RECT{ r.left + fx - 1, r.top, r.right - fx + 1, r.bottom - fy + 1 };
}

}  // namespace

// ---- What it says ------------------------------------------------------------------

Trigger pending() {
	static const Trigger once = [] {
		if (!allowed()) return Trigger::None;
		if (envSet(L"CL_INTRO")) return envValue(L"CL_INTRO") == "update" ? Trigger::Update : Trigger::First;
		return owed();
	}();
	return once;
}

void markPlayed(Trigger t) {
	if (t == Trigger::First) prefs().introFirstLaunchPlayed = true;
	else if (t == Trigger::Update) prefs().introUpdatePlayed = whatsnew::kVersion;
	else return;
	prefs().save();
}

void playSound(bool calm) {
	prefs().firstLaunchPlayed = true;
	prefs().save();
	gSoundFrom = calm ? kSoundHit - 0.15 - kCalmHold : kSoundHit - kLedAt;
	gSoundStarted = nowSeconds();
	if (!playFrom(gSoundFrom, 0, 0.3, false)) gSoundStarted = -1;
}

bool active() { return g_player != nullptr; }

void skip() {
	Player* p = g_player;
	if (p == nullptr || p->skipAt >= 0) return;
	p->skipAt = nowSeconds();
	splash::skipIntro();
	// The sound fades out with the picture.
	if (gSoundStarted >= 0) {
		const double in = nowSeconds() - gSoundStarted;
		if (in < 8) playFrom(gSoundFrom + in, 0.35, 0, true, std::min(1.0, in / 0.3));
		gSoundStarted = -1;
	}
}

double play(POINT cardIcon) {
	if (g_player) return -1;
	HMONITOR splashScreen = MonitorFromPoint(cardIcon, MONITOR_DEFAULTTOPRIMARY);
	Player* p = new Player();
	const RECT area = windowArea(splashScreen, p->scale);
	p->pw = area.right - area.left;
	p->ph = area.bottom - area.top;
	if (p->pw < 200 || p->ph < 150) { delete p; return -1; }
	// Windows 11 rounds a window's corners; 10 doesn't.
	p->corner = windows11() && !(circuitWindows().size() && circuitWindows().front()->opensMaximized()) ? 8.0f : 0.0f;
	const std::string freeze = envValue(L"CL_INTRO_FREEZE");
	if (!freeze.empty()) p->freezeAt = atof(freeze.c_str());
	const Pt icon = D2D1::Point2F((cardIcon.x - area.left) / p->scale, (cardIcon.y - area.top) / p->scale);
	p->scene = new Scene(p->pw / p->scale, p->ph / p->scale, icon, WireLook::current());
	static bool registered = false;
	if (!registered) {
		registered = true;
		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof wc;
		wc.lpfnWndProc = introProc;
		wc.hInstance = appInstance();
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.lpszClassName = L"CedarLogicIntro";
		RegisterClassExW(&wc);
	}
	p->hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"CedarLogicIntro", L"CedarLogic", WS_POPUP, area.left,
	                          area.top, p->pw, p->ph, nullptr, nullptr, appInstance(), nullptr);
	if (p->hwnd == nullptr || !p->makeSurface()) { p->destroy(); delete p; return -1; }
	g_player = p;
	// Drawn once (unseen: it fades in from nothing), then up, in front, for
	// the keys, under the launch screen's panel.
	p->start = nowSeconds();
	p->frame();
	ShowWindow(p->hwnd, SW_SHOW);
	SetForegroundWindow(p->hwnd);
	SetFocus(p->hwnd);
	splash::raise();
	p->start = nowSeconds();
	SetTimer(p->hwnd, 1, 10, nullptr);
	return p->start;
}

// ---- Rendering it -------------------------------------------------------------------

bool render(const std::string& dir, Trigger trigger, IWICBitmap* behind, std::string& report) {
	const float width = 1280, height = 800, scale = 2;
	SHCreateDirectoryExW(nullptr, W(dir).c_str(), nullptr);
	IWICImagingFactory* wic = nullptr;
	if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) || wic == nullptr) {
		report = "Windows Imaging didn't start";
		return false;
	}
	IWICBitmap* bmp = nullptr;
	ID2D1RenderTarget* rt = nullptr;
	const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
		D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96 * scale, 96 * scale);
	if (FAILED(wic->CreateBitmap((UINT)(width * scale), (UINT)(height * scale), GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp)) ||
	    FAILED(d2dFactory()->CreateWicBitmapRenderTarget(bmp, props, &rt))) {
		if (bmp) bmp->Release();
		wic->Release();
		report = "couldn't make a picture to draw on";
		return false;
	}
	rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
	// The launch screen's panel: 560 by 372, a little above the middle.
	const Pt panel = D2D1::Point2F(width / 2 - 280, height / 2 - 36 - 186);
	Scene* scene = new Scene(width, height, D2D1::Point2F(width / 2, panel.y + 44 + 64), WireLook::current());
	ID2D1Bitmap* window = nullptr;
	if (behind) rt->CreateBitmapFromWicBitmap(behind, &window);
	const int frames = (int)std::lround(kTotal * 60);
	int written = 0;
	for (int f = 0; f <= frames; f++) {
		const double t = f / 60.0;
		rt->BeginDraw();
		// On black: the intro fades in from nothing (over the desktop, on
		// screen), and a video can't show see-through.
		rt->Clear(D2D1::ColorF(0, 0, 0, 1));
		// The window under it all, put in as the dissolve begins, the intro
		// and the panel fading off it.
		const double out = inOut((t - kDissolveAt) / kFade);
		if (window && t >= kDissolveAt) rt->DrawBitmap(window, D2D1::RectF(0, 0, width, height), 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
		const double shown = (1 - out) * fadeInShown(t);
		if (shown > 0.002) {
			rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(),
			                                    (float)shown),
			              nullptr);
			scene->draw(rt, t);
			rt->PopLayer();
		}
		// The launch screen's panel, from its own drawing at its own time.
		if (t >= kCardIn) splash::drawAfterIntro(rt, panel, t);
		if (FAILED(rt->EndDraw())) { report = strf("frame %d didn't draw", f); break; }
		if (!images::savePng(bmp, dir + strf("\\frame-%04d.png", f))) { report = strf("couldn't write frame %d", f); break; }
		written++;
	}
	delete scene;
	splash::releaseIntroPicture();
	if (window) window->Release();
	rt->Release();
	bmp->Release();
	wic->Release();
	if (written != frames + 1) return false;
	report = strf("%d frames (%s) in %s", written, trigger == Trigger::Update ? "update" : "first", dir.c_str());
	return true;
}

}  // namespace intro
