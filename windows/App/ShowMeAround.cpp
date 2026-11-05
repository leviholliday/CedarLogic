// Show Me Around (see ShowMeAround.h).

#include "ShowMeAround.h"
#include "Privacy.h"
#include "Brand.h"
#include "Chrome.h"
#include "Commands.h"
#include "Fade.h"
#include "FeedbackCapture.h"
#include "Intro.h"
#include "Shortcuts.h"
#include "Welcome.h"
#include "Window.h"

#include <dwmapi.h>

#include <algorithm>
#include <cmath>

using namespace brand;

namespace showme {

bool quiet = false;
bool assumeFront = false;

namespace {

const DWRITE_FONT_WEIGHT kBold = DWRITE_FONT_WEIGHT_BOLD, kSemi = DWRITE_FONT_WEIGHT_SEMI_BOLD, kMedium = DWRITE_FONT_WEIGHT_MEDIUM,
                         kNormal = DWRITE_FONT_WEIGHT_NORMAL;

// The bubble's sides, as the Mac's.
enum Side { Right, Left, Below, Above, Inside };

struct StepDef {
	Spot spot;
	const char* title;
	std::vector<Side> sides;   // where the bubble would rather go, before trying the others
};

const StepDef kSteps[] = {
	{ Palette, "Parts", { Right } },
	{ Canvas, "Your circuit", { Inside } },
	{ ToolbarSpot, "The toolbar", { Below } },
	{ Actions, "The Actions panel", { Left } },
	{ Run, "Run it", { Below } },
	{ Tabs, "Tabs", { Below } },
	{ MenuBar, "Help and Settings", { Below } },
};
const int kStepCount = 7;

// Sizes in points, as the Mac's overlay.
const float kPad = 6, kGap = 14, kMargin = 12, kRadius = 10, kBubblePad = 16;

double easeOut(double t) {
	t = std::min(1.0, std::max(0.0, t));
	return 1 - (1 - t) * (1 - t) * (1 - t);
}

bool alive(CircuitWindow* w) { return w && std::find(circuitWindows().begin(), circuitWindows().end(), w) != circuitWindows().end(); }

D2D1_RECT_F lerp(const D2D1_RECT_F& a, const D2D1_RECT_F& b, float t) {
	return D2D1::RectF(a.left + (b.left - a.left) * t, a.top + (b.top - a.top) * t, a.right + (b.right - a.right) * t,
	                   a.bottom + (b.bottom - a.bottom) * t);
}
bool same(const D2D1_RECT_F& a, const D2D1_RECT_F& b) {
	return std::fabs(a.left - b.left) < 0.5f && std::fabs(a.top - b.top) < 0.5f && std::fabs(a.right - b.right) < 0.5f &&
	       std::fabs(a.bottom - b.bottom) < 0.5f;
}
bool emptyF(const D2D1_RECT_F& r) { return r.right - r.left < 0.5f || r.bottom - r.top < 0.5f; }

struct Hit { D2D1_RECT_F r; int id; };
enum { kSkip = 1, kBack, kNext, kLesson };

// The bubble's pointer: on which edge (0 top, 1 bottom, 2 left, 3 right; -1 none), how far along.
struct Caret { int edge = -1; float at = 0; };

struct State {
	bool active = false;
	CircuitWindow* target = nullptr;
	int step = 0;
	UINT_PTR settleTimer = 0;
	// The overlay.
	HWND overlay = nullptr;
	RECT client{};                  // the target's client area on the screen, as the overlay was last placed
	double scale = 1;
	float W = 0, H = 0;             // the overlay in points
	// Where each place is (overlay points), last measured.
	D2D1_RECT_F frames[SpotCount]{};
	double measuredAt = -10;
	// The hole and the bubble: from, to, and when they set off.
	D2D1_RECT_F holeFrom{}, holeTo{};
	D2D1_POINT_2F bubbleFrom{}, bubbleTo{};
	Caret caret;
	double moveAt = -10;
	bool placed = false;
	// Animation effects off: the bubble fades out (phase 1), the stop
	// changes, and it fades in (phase 2).
	int fadePhase = 0;
	double fadeAt = 0;
	int pendingStep = -1;
	// The whole overlay fading in as it starts, and out as it goes.
	double appearAt = -10;
	bool leaving = false;
	double leaveAt = 0;
	bool picture = false;           // a preview: nothing fades
	// Its bubble's controls this frame, and the one under the pointer.
	std::vector<Hit> hits;
	int hot = -1;
	// The drawing: a DIB the size of the overlay.
	HDC memDC = nullptr;
	HBITMAP dib = nullptr;
	HGDIOBJ oldBmp = nullptr;
	void* bits = nullptr;
	int pw = 0, ph = 0;
	ID2D1DCRenderTarget* rt = nullptr;
	bool dirty = true;
	// What comes after it.
	std::function<void(CircuitWindow*)> then;
	// The watch for the moment to start by itself.
	bool wanted = false;
	UINT_PTR autoTimer = 0;
	double readySince = -1;
};
State g;

// ---- The stops ------------------------------------------------------------------------

bool spotShowing(Spot s) {
	if (s == Actions && !prefs().actionsOn) return false;
	return !emptyF(g.frames[s]);
}

const wchar_t* kClass = L"CedarLogicShowMeAround";
LRESULT CALLBACK overlayProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);

void registerClass() {
	static bool done = false;
	if (done) return;
	done = true;
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof wc;
	wc.lpfnWndProc = overlayProc;
	wc.hInstance = appInstance();
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.lpszClassName = kClass;
	RegisterClassExW(&wc);
}

// The target's client area on the screen.
RECT clientOnScreen() {
	RECT r = { 0, 0, 0, 0 };
	if (!g.target) return r;
	HWND h = g.target->window();
	GetClientRect(h, &r);
	MapWindowPoints(h, nullptr, (POINT*)&r, 2);
	return r;
}

// Reads where each place is now, in the overlay's points.
void measure() {
	g.measuredAt = nowSeconds();
	for (int i = 0; i < SpotCount; i++) g.frames[i] = D2D1::RectF(0, 0, 0, 0);
	if (!alive(g.target)) return;
	const RECT c = clientOnScreen();
	const float s = (float)g.scale;
	for (int i = 0; i < SpotCount; i++) {
		RECT r = g.target->tourSpot(i);
		RECT in;
		if (IsRectEmpty(&r) || !IntersectRect(&in, &r, &c)) continue;
		g.frames[i] = D2D1::RectF((in.left - c.left) / s, (in.top - c.top) / s, (in.right - c.left) / s, (in.bottom - c.top) / s);
	}
}

// The lit place, a little larger than it is, and at least a finger's width
// (the folded panel's tab is 6 points), inside the overlay.
D2D1_RECT_F holeFor(int step) {
	const D2D1_RECT_F f = g.frames[kSteps[std::min(std::max(step, 0), kStepCount - 1)].spot];
	if (emptyF(f)) return D2D1::RectF(g.W / 2, g.H / 2, g.W / 2, g.H / 2);
	D2D1_RECT_F r = D2D1::RectF(f.left - kPad, f.top - kPad, f.right + kPad, f.bottom + kPad);
	if (r.right - r.left < 28) {
		const float more = (28 - (r.right - r.left)) / 2;
		r.left -= more;
		r.right += more;
	}
	const D2D1_RECT_F inside = D2D1::RectF(std::max(r.left, 2.0f), std::max(r.top, 2.0f), std::min(r.right, g.W - 2), std::min(r.bottom, g.H - 2));
	return emptyF(inside) ? r : inside;
}

float bubbleWidth() { return std::min(300.0f, std::max(200.0f, g.W - 2 * kMargin)); }

// The bubble: drawn at (x, y) when rt is set, measured either way; returns its height.
float bubble(ID2D1RenderTarget* rt, float x, float y, int step);

// Beside the place, on the side it prefers if it fits there, else any side
// that does, else inside a big place, else the window's foot (the Mac's).
void place(int step, const D2D1_RECT_F& h, D2D1_POINT_2F& origin, Caret& caret) {
	const float bw = bubbleWidth(), bh = bubble(nullptr, 0, 0, step);
	const float m = kMargin, gap = kGap, W = g.W, H = g.H;
	auto clampX = [&](float x) { return std::min(std::max(x, m), std::max(m, W - bw - m)); };
	auto clampY = [&](float y) { return std::min(std::max(y, m), std::max(m, H - bh - m)); };
	std::vector<Side> sides = kSteps[step].sides;
	for (Side s : { Below, Right, Left, Above, Inside }) sides.push_back(s);
	const float midX = (h.left + h.right) / 2, midY = (h.top + h.bottom) / 2;
	caret = Caret();
	for (Side s : sides) {
		switch (s) {
		case Right:
			if (W - h.right - gap - m < bw) continue;
			origin = D2D1::Point2F(h.right + gap, clampY(midY - bh / 2));
			caret = { 2, midY - origin.y };
			return;
		case Left:
			if (h.left - gap - m < bw) continue;
			origin = D2D1::Point2F(h.left - gap - bw, clampY(midY - bh / 2));
			caret = { 3, midY - origin.y };
			return;
		case Below:
			if (H - h.bottom - gap - m < bh) continue;
			origin = D2D1::Point2F(clampX(midX - bw / 2), h.bottom + gap);
			caret = { 0, midX - origin.x };
			return;
		case Above:
			if (h.top - gap - m < bh) continue;
			origin = D2D1::Point2F(clampX(midX - bw / 2), h.top - gap - bh);
			caret = { 1, midX - origin.x };
			return;
		case Inside:
			if (h.right - h.left < bw + 2 * m || h.bottom - h.top < bh + 2 * m) continue;
			origin = D2D1::Point2F(clampX(midX - bw / 2), clampY(midY - bh / 2));
			return;
		}
	}
	origin = D2D1::Point2F(clampX(W / 2 - bw / 2), clampY(H - bh - m));
}

// ---- Drawing ----------------------------------------------------------------------------

double moveProgress() {
	if (g.picture || !fade::animations()) return 1;
	return easeOut((nowSeconds() - g.moveAt) / 0.42);
}

float bubbleAlpha() {
	if (g.picture) return 1;
	const double t = nowSeconds() - g.fadeAt;
	if (g.fadePhase == 1) return (float)std::max(0.0, 1 - t / 0.1);
	if (g.fadePhase == 2) return (float)std::min(1.0, t / 0.14);
	return 1;
}

float overlayAlpha() {
	if (g.picture || !fade::animations()) return g.leaving ? 0.0f : 1.0f;
	const double now = nowSeconds();
	if (g.leaving) return (float)std::max(0.0, 1 - (now - g.leaveAt) / 0.2);
	return (float)easeOut((now - g.appearAt) / 0.2);
}

bool animating() {
	if (!g.active && !g.leaving) return false;
	if (g.leaving) return true;
	if (g.fadePhase) return true;
	if (moveProgress() < 1) return true;
	return overlayAlpha() < 1;
}

void binoculars(ID2D1RenderTarget* rt, float x, float y, D2D1_COLOR_F c) {
	fillCircle(rt, D2D1::Point2F(x + 3, y + 7), 3, c);
	fillCircle(rt, D2D1::Point2F(x + 10, y + 7), 3, c);
	fillRound(rt, D2D1::RectF(x + 0.5f, y + 1.5f, x + 5, y + 6), 1.5f, c);
	fillRound(rt, D2D1::RectF(x + 8, y + 1.5f, x + 12.5f, y + 6), 1.5f, c);
	fillRect(rt, D2D1::RectF(x + 5, y + 4, x + 8, y + 6.5f), c);
}

float bubble(ID2D1RenderTarget* rt, float x, float y, int step) {
	const float bw = bubbleWidth(), inner = bw - 2 * kBubblePad;
	const std::vector<int> s = shown();
	const int count = std::max(1, (int)s.size());
	const int index = (int)(std::find(s.begin(), s.end(), step) - s.begin()) + 1;
	const bool last = s.empty() || step >= s.back();
	const bool first = s.empty() || step <= s.front();
	float cy = y + kBubblePad;
	const float left = x + kBubblePad;
	// The header: where you are, and Skip.
	if (rt) {
		binoculars(rt, left, cy, kNeon);
		text(rt, strf("SHOW ME AROUND  ·  %d OF %d", std::min(index, count), count), left + 19, cy, 10, kBold, kNeon, 0,
		     DWRITE_TEXT_ALIGNMENT_LEADING, 1.1f);
		if (!last) {
			const float sw = textWidth("Skip", 11.5f, kMedium);
			const D2D1_RECT_F r = D2D1::RectF(x + bw - kBubblePad - sw - 4, cy - 3, x + bw - kBubblePad + 2, cy + 16);
			text(rt, "Skip", r.left + 4, cy - 1, 11.5f, kMedium, g.hot == kSkip ? alpha(kPrimary, 0.75f) : kFaint);
			g.hits.push_back({ r, kSkip });
		}
	}
	cy += 14 + 9;
	if (index == 1) {
		const std::string intro = strf("A quick look around CedarLogic, %d stops. Skip any time.", count);
		cy += text(rt, intro, left, cy, 12, kSemi, alpha(kPrimary, 0.85f), inner) + 9;
	}
	cy += text(rt, stepTitle(step), left, cy, 16, kBold, kPrimary, inner) + 8;
	cy += text(rt, stepBody(step), left, cy, 12.5f, kNormal, kSecondary, inner) + 11;
	// The dots, Back and Next (Done at the end).
	const float bh = 32;
	if (rt) {
		float dx = left;
		for (int i = 0; i < count; i++) {
			const bool on = i == index - 1;
			const float w = on ? 14.0f : 5.0f;
			fillRound(rt, D2D1::RectF(dx, cy + bh / 2 - 2.5f, dx + w, cy + bh / 2 + 2.5f), 2.5f, on ? kNeon : D2D1::ColorF(1, 1, 1, 0.2f));
			dx += w + 4;
		}
		const D2D1_RECT_F nextR = D2D1::RectF(x + bw - kBubblePad - 74, cy, x + bw - kBubblePad, cy + bh);
		button(rt, nextR, last ? "Done" : "Next", true, g.hot == kNext);
		g.hits.push_back({ nextR, kNext });
		if (!first) {
			const D2D1_RECT_F backR = D2D1::RectF(nextR.left - 8 - 64, cy, nextR.left - 8, cy + bh);
			button(rt, backR, "Back", false, g.hot == kBack);
			g.hits.push_back({ backR, kBack });
		}
	}
	cy += bh;
	// The last stop offers the lesson.
	if (last) {
		cy += 10;
		const std::string line = "Next, learn logic: the Guided Tour builds a circuit with you";
		const float lh = text(nullptr, line, 0, 0, 11.5f, kMedium, kNeon, inner - 18);
		if (rt) {
			const D2D1_COLOR_F c = alpha(kNeon, g.hot == kLesson ? 1.0f : 0.9f);
			drawIcon(rt, 0xE805, D2D1::RectF(left - 2, cy - 1, left + 14, cy + 15), 11, c);
			text(rt, line, left + 18, cy, 11.5f, kMedium, c, inner - 18);
			g.hits.push_back({ D2D1::RectF(left - 2, cy - 3, left + inner, cy + lh + 3), kLesson });
		}
		cy += lh;
	}
	cy += kBubblePad;
	return cy - y;
}

// The dim with its hole: the whole overlay, less a rounded rectangle.
void dim(ID2D1RenderTarget* rt, const D2D1_RECT_F& hole) {
	const D2D1_COLOR_F c = D2D1::ColorF(0, 0, 0, prefs().dark ? 0.6f : 0.48f);
	ID2D1Factory* f = d2dFactory();
	ID2D1RectangleGeometry* all = nullptr;
	ID2D1RoundedRectangleGeometry* lit = nullptr;
	ID2D1PathGeometry* path = nullptr;
	ID2D1GeometrySink* sink = nullptr;
	ID2D1SolidColorBrush* b = nullptr;
	bool drawn = false;
	if (!emptyF(hole) && SUCCEEDED(f->CreateRectangleGeometry(D2D1::RectF(0, 0, g.W, g.H), &all)) &&
	    SUCCEEDED(f->CreateRoundedRectangleGeometry(D2D1::RoundedRect(hole, kRadius, kRadius), &lit)) &&
	    SUCCEEDED(f->CreatePathGeometry(&path)) && SUCCEEDED(path->Open(&sink)) &&
	    SUCCEEDED(all->CombineWithGeometry(lit, D2D1_COMBINE_MODE_EXCLUDE, nullptr, sink)) && SUCCEEDED(sink->Close()) &&
	    SUCCEEDED(rt->CreateSolidColorBrush(c, &b))) {
		rt->FillGeometry(path, b);
		drawn = true;
	}
	if (b) b->Release();
	if (sink) sink->Release();
	if (path) path->Release();
	if (lit) lit->Release();
	if (all) all->Release();
	if (!drawn) fillRect(rt, D2D1::RectF(0, 0, g.W, g.H), c);
	if (emptyF(hole)) return;
	// The ring, with a glow outside it (only outside: the hole stays clear, for clicks).
	for (int i = 6; i >= 1; i--) {
		const float k = (float)i * 1.4f;
		strokeRound(rt, D2D1::RectF(hole.left - k, hole.top - k, hole.right + k, hole.bottom + k), kRadius + k,
		            alpha(kNeon, 0.1f * (1 - i / 7.0f)), 1.6f);
	}
	strokeRound(rt, hole, kRadius, alpha(kNeon, 0.85f), 2);
}

void drawBubble(ID2D1RenderTarget* rt, D2D1_POINT_2F o, int step, const Caret& caret, float a) {
	if (a <= 0.003f) return;
	const float bw = bubbleWidth(), bh = bubble(nullptr, 0, 0, step);
	const D2D1_RECT_F r = D2D1::RectF(o.x, o.y, o.x + bw, o.y + bh);
	rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), a), nullptr);
	glow(rt, D2D1::RectF(r.left, r.top + 6, r.right, r.bottom + 6), 14, D2D1::ColorF(0, 0, 0, 0.45f), 16);
	// The pointer, on the edge facing the place.
	if (caret.edge >= 0) {
		const float w = 16, d = 8;
		const float along = caret.edge <= 1 ? bw : bh;
		const float at = std::min(std::max(caret.at, 20.0f), along - 20);
		D2D1_POINT_2F p[3];
		switch (caret.edge) {
		case 0: p[0] = { r.left + at - w / 2, r.top + 1 }; p[1] = { r.left + at, r.top - d }; p[2] = { r.left + at + w / 2, r.top + 1 }; break;
		case 1: p[0] = { r.left + at - w / 2, r.bottom - 1 }; p[1] = { r.left + at, r.bottom + d }; p[2] = { r.left + at + w / 2, r.bottom - 1 }; break;
		case 2: p[0] = { r.left + 1, r.top + at - w / 2 }; p[1] = { r.left - d, r.top + at }; p[2] = { r.left + 1, r.top + at + w / 2 }; break;
		default: p[0] = { r.right - 1, r.top + at - w / 2 }; p[1] = { r.right + d, r.top + at }; p[2] = { r.right - 1, r.top + at + w / 2 }; break;
		}
		ID2D1PathGeometry* tri = nullptr;
		ID2D1GeometrySink* sink = nullptr;
		ID2D1SolidColorBrush* b = nullptr;
		if (SUCCEEDED(d2dFactory()->CreatePathGeometry(&tri)) && SUCCEEDED(tri->Open(&sink))) {
			sink->BeginFigure(p[0], D2D1_FIGURE_BEGIN_FILLED);
			sink->AddLine(p[1]);
			sink->AddLine(p[2]);
			sink->EndFigure(D2D1_FIGURE_END_CLOSED);
			sink->Close();
			if (SUCCEEDED(rt->CreateSolidColorBrush(kInk, &b))) rt->FillGeometry(tri, b);
		}
		if (b) b->Release();
		if (sink) sink->Release();
		if (tri) tri->Release();
	}
	// The card: the brand's ground, in its rounded corners.
	ID2D1RoundedRectangleGeometry* shape = nullptr;
	if (SUCCEEDED(d2dFactory()->CreateRoundedRectangleGeometry(D2D1::RoundedRect(r, 14, 14), &shape))) {
		rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), shape), nullptr);
		D2D1_MATRIX_3X2_F was;
		rt->GetTransform(&was);
		rt->SetTransform(D2D1::Matrix3x2F::Translation(r.left, r.top) * was);
		ground(rt, bw, bh, 0.1f, 0, 22);
		rt->SetTransform(was);
		rt->PopLayer();
		shape->Release();
	} else {
		fillRound(rt, r, 14, kInk);
	}
	strokeRound(rt, D2D1::RectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f), 14, D2D1::ColorF(1, 1, 1, 0.18f));
	bubble(rt, o.x, o.y, step);
	rt->PopLayer();
}

void releaseSurface() {
	if (g.rt) g.rt->Release();
	g.rt = nullptr;
	if (g.memDC) {
		if (g.oldBmp) SelectObject(g.memDC, g.oldBmp);
		DeleteDC(g.memDC);
	}
	if (g.dib) DeleteObject(g.dib);
	g.memDC = nullptr;
	g.dib = nullptr;
	g.oldBmp = nullptr;
	g.bits = nullptr;
	g.pw = g.ph = 0;
}

bool surface(int w, int h) {
	if (g.memDC && g.pw == w && g.ph == h && g.rt) return true;
	releaseSurface();
	if (w <= 0 || h <= 0) return false;
	HDC screen = GetDC(nullptr);
	g.memDC = CreateCompatibleDC(screen);
	ReleaseDC(nullptr, screen);
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof bi.bmiHeader;
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	g.dib = g.memDC ? CreateDIBSection(g.memDC, &bi, DIB_RGB_COLORS, &g.bits, nullptr, 0) : nullptr;
	if (!g.dib || !g.bits) { releaseSurface(); return false; }
	g.oldBmp = SelectObject(g.memDC, g.dib);
	g.pw = w;
	g.ph = h;
	const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
		D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), (float)(96 * g.scale),
		(float)(96 * g.scale));
	if (FAILED(d2dFactory()->CreateDCRenderTarget(&props, &g.rt))) { releaseSurface(); return false; }
	return true;
}

// The whole overlay into its bitmap, and onto the screen.
void render() {
	if (!g.overlay) return;
	const int w = g.client.right - g.client.left, h = g.client.bottom - g.client.top;
	if (!surface(w, h)) return;
	RECT all = { 0, 0, w, h };
	if (FAILED(g.rt->BindDC(g.memDC, &all))) return;
	g.hits.clear();
	g.rt->BeginDraw();
	g.rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
	g.rt->Clear(D2D1::ColorF(0, 0, 0, 0));
	const float t = (float)moveProgress();
	const D2D1_RECT_F hole = lerp(g.holeFrom, g.holeTo, t);
	dim(g.rt, hole);
	const D2D1_POINT_2F o = D2D1::Point2F(g.bubbleFrom.x + (g.bubbleTo.x - g.bubbleFrom.x) * t, g.bubbleFrom.y + (g.bubbleTo.y - g.bubbleFrom.y) * t);
	Caret c = g.caret;
	if (c.edge >= 0) {
		// The pointer keeps to the place as the bubble slides.
		const D2D1_POINT_2F mid = D2D1::Point2F((hole.left + hole.right) / 2, (hole.top + hole.bottom) / 2);
		c.at = c.edge <= 1 ? mid.x - o.x : mid.y - o.y;
	}
	drawBubble(g.rt, o, g.step, c, bubbleAlpha());
	if (g.rt->EndDraw() == D2DERR_RECREATE_TARGET) { releaseSurface(); return; }
	POINT dst = { g.client.left, g.client.top }, src = { 0, 0 };
	SIZE size = { w, h };
	BLENDFUNCTION blend = { AC_SRC_OVER, 0, (BYTE)std::lround(255 * std::min(1.0f, std::max(0.0f, overlayAlpha()))), AC_SRC_ALPHA };
	UpdateLayeredWindow(g.overlay, nullptr, &dst, &size, g.memDC, &src, 0, &blend, ULW_ALPHA);
	g.dirty = false;
}

// Where the hole and bubble are going for this stop; `cut`: there at once.
void aim(bool cut) {
	const D2D1_RECT_F hole = holeFor(g.step);
	D2D1_POINT_2F o;
	Caret c;
	place(g.step, hole, o, c);
	const bool moved = !same(hole, g.holeTo) || std::fabs(o.x - g.bubbleTo.x) > 0.5f || std::fabs(o.y - g.bubbleTo.y) > 0.5f;
	if (cut || !g.placed) {
		g.holeFrom = g.holeTo = hole;
		g.bubbleFrom = g.bubbleTo = o;
		g.moveAt = -10;
		g.placed = true;
		g.dirty = true;
	} else if (moved) {
		// From where it is now (mid-slide or not) to the new place.
		const float t = (float)moveProgress();
		g.holeFrom = lerp(g.holeFrom, g.holeTo, t);
		g.bubbleFrom = D2D1::Point2F(g.bubbleFrom.x + (g.bubbleTo.x - g.bubbleFrom.x) * t, g.bubbleFrom.y + (g.bubbleTo.y - g.bubbleFrom.y) * t);
		g.holeTo = hole;
		g.bubbleTo = o;
		g.moveAt = nowSeconds();
		if (!fade::animations()) { g.holeFrom = hole; g.bubbleFrom = o; }
		g.dirty = true;
	}
	g.caret = c;
}

void destroyOverlay() {
	if (g.overlay) {
		KillTimer(g.overlay, 1);
		DestroyWindow(g.overlay);
	}
	g.overlay = nullptr;
	g.leaving = false;
	releaseSurface();
}

// The panel opens for its stop and folds back after: the window lays out again.
void panelMaybeChanged(int fromStep, int toStep) {
	const bool was = fromStep >= 0 && kSteps[fromStep].spot == Actions, now = toStep >= 0 && kSteps[toStep].spot == Actions;
	if (was != now && alive(g.target)) g.target->actionsChanged();
}

void setStep(int n) {
	const int was = g.step;
	g.step = n;
	panelMaybeChanged(was, n);
	measure();
}

// Every 15 ms while it's on: keeps to its window, follows the places, draws.
void tick() {
	if (g.leaving) {
		if (overlayAlpha() <= 0) { destroyOverlay(); return; }
		render();
		return;
	}
	if (!g.active) return;
	if (!alive(g.target)) { stop(); return; }
	HWND w = g.target->window();
	// Under a dialog, minimized or hidden: out of sight until it's back.
	if (IsIconic(w) || !IsWindowVisible(w) || !IsWindowEnabled(w)) {
		if (IsWindowVisible(g.overlay)) ShowWindow(g.overlay, SW_HIDE);
		return;
	}
	const RECT c = clientOnScreen();
	const UINT dpi = dpiOf(w);
	if (!EqualRect(&c, &g.client) || std::fabs(g.scale - dpi / 96.0) > 1e-6) {
		g.client = c;
		g.scale = dpi / 96.0;
		g.W = (float)((c.right - c.left) / g.scale);
		g.H = (float)((c.bottom - c.top) / g.scale);
		measure();
		g.dirty = true;
	} else if (nowSeconds() - g.measuredAt > 0.12) {
		measure();
	}
	// Animation effects off: the bubble out, the stop changed, the bubble in.
	if (g.fadePhase == 1 && nowSeconds() - g.fadeAt >= 0.1) {
		setStep(g.pendingStep);
		g.pendingStep = -1;
		aim(true);
		g.fadePhase = 2;
		g.fadeAt = nowSeconds();
	} else if (g.fadePhase == 2 && nowSeconds() - g.fadeAt >= 0.14) {
		g.fadePhase = 0;
		g.dirty = true;
	}
	// The place it's on went (the panel turned off): on to the next.
	const std::vector<int> s = shown();
	if (s.empty()) { stop(); return; }
	if (std::find(s.begin(), s.end(), g.step) == s.end() && g.fadePhase == 0) {
		int n = s.back();
		for (int i : s) if (i > g.step) { n = i; break; }
		setStep(n);
	}
	if (g.fadePhase != 1) aim(false);
	if (!IsWindowVisible(g.overlay)) {
		g.dirty = true;
		ShowWindow(g.overlay, SW_SHOWNOACTIVATE);
	}
	if (g.dirty || animating()) render();
}

void hitAction(int id) {
	switch (id) {
	case kSkip: end(); break;
	case kBack: back(); break;
	case kNext: next(); break;
	case kLesson:
		if (!g.then) g.then = [](CircuitWindow* w) { welcome::startTourOn(w); };
		end();
		break;
	default: break;
	}
}

int hitAt(float x, float y) {
	for (auto it = g.hits.rbegin(); it != g.hits.rend(); ++it)
		if (x >= it->r.left && x < it->r.right && y >= it->r.top && y < it->r.bottom) return it->id;
	return -1;
}

LRESULT CALLBACK overlayProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
	LRESULT result = 0;
	bool handled = true;
	guarded("Show Me Around", [&] {
		const float s = (float)g.scale;
		switch (msg) {
		case WM_MOUSEACTIVATE: result = MA_NOACTIVATE; break;
		case WM_NCHITTEST: result = HTCLIENT; break;
		case WM_TIMER: tick(); break;
		case WM_MOUSEMOVE: {
			TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 };
			TrackMouseEvent(&t);
			const int id = hitAt(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s);
			if (id != g.hot) { g.hot = id; g.dirty = true; }
			SetCursor(LoadCursor(nullptr, id >= 0 ? IDC_HAND : IDC_ARROW));
			break;
		}
		case WM_MOUSELEAVE: if (g.hot != -1) { g.hot = -1; g.dirty = true; } break;
		case WM_SETCURSOR: SetCursor(LoadCursor(nullptr, g.hot >= 0 ? IDC_HAND : IDC_ARROW)); result = TRUE; break;
		case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_LBUTTONDBLCLK: case WM_MOUSEWHEEL: break;   // the dim takes them
		case WM_LBUTTONUP: {
			const int id = hitAt(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s);
			if (id >= 0) hitAction(id);
			break;
		}
		default: handled = false; break;
		}
	});
	return handled ? result : DefWindowProcW(h, msg, wp, lp);
}

// On: the overlay over the target, at the first stop it has from `from`.
void begin(int from, bool picture) {
	if (!alive(g.target)) { g.target = nullptr; return; }
	destroyOverlay();
	registerClass();
	HWND w = g.target->window();
	g.client = clientOnScreen();
	g.scale = dpiOf(w) / 96.0;
	g.W = (float)((g.client.right - g.client.left) / g.scale);
	g.H = (float)((g.client.bottom - g.client.top) / g.scale);
	g.active = true;
	g.picture = picture;
	g.placed = false;
	g.fadePhase = 0;
	g.pendingStep = -1;
	g.hot = -1;
	g.step = 0;
	measure();
	const std::vector<int> s = shown();
	int first = s.empty() ? 0 : s.front();
	for (int i : s) if (i >= from) { first = i; break; }
	setStep(first);
	g.overlay = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClass, L"Show Me Around", WS_POPUP, g.client.left,
	                            g.client.top, std::max<LONG>(1, g.client.right - g.client.left), std::max<LONG>(1, g.client.bottom - g.client.top),
	                            w, nullptr, appInstance(), nullptr);
	g.appearAt = nowSeconds();
	aim(true);
	render();
	ShowWindow(g.overlay, SW_SHOWNOACTIVATE);
	SetTimer(g.overlay, 1, 15, nullptr);
}

void CALLBACK settleTick(HWND, UINT, UINT_PTR id, DWORD) {
	KillTimer(nullptr, id);
	if (g.settleTimer != id) return;
	g.settleTimer = 0;
	guarded("Show Me Around", [] { begin(0, false); });
}

CircuitWindow* g_thenWindow = nullptr;
std::function<void(CircuitWindow*)> g_thenPending;
void CALLBACK thenTick(HWND, UINT, UINT_PTR id, DWORD) {
	KillTimer(nullptr, id);
	std::function<void(CircuitWindow*)> f = std::move(g_thenPending);
	g_thenPending = nullptr;
	CircuitWindow* w = alive(g_thenWindow) ? g_thenWindow : (circuitWindows().empty() ? nullptr : circuitWindows().back());
	g_thenWindow = nullptr;
	if (f && w) guarded("Show Me Around", [&] { f(w); });
}

void CALLBACK autoTick(HWND, UINT, UINT_PTR, DWORD) {
	guarded("Show Me Around", [] {
		if (prefs().showMeAroundSeen || g.active || g.settleTimer) { stopWatching(); return; }
		CircuitWindow* w = readyWindow();
		if (!w) { g.readySince = -1; return; }
		// Ready for a moment (the window settled, the circuit's card played).
		const double now = nowSeconds();
		if (g.readySince >= 0 && now - g.readySince > 0.7) {
			stopWatching();
			start(w, 0.2);
		} else if (g.readySince < 0) {
			g.readySince = now;
		}
	});
}

// The circuit window in front, if any.
CircuitWindow* frontWindow() {
	if (assumeFront) return circuitWindows().empty() ? nullptr : circuitWindows().back();
	HWND fg = GetForegroundWindow();
	for (CircuitWindow* w : circuitWindows()) if (w->window() == fg) return w;
	return nullptr;
}

}  // namespace

// ---- The stops ------------------------------------------------------------------------

int stepCount() { return kStepCount; }
Spot stepSpot(int i) { return kSteps[std::min(std::max(i, 0), kStepCount - 1)].spot; }
std::string stepTitle(int i) { return kSteps[std::min(std::max(i, 0), kStepCount - 1)].title; }

std::string stepBody(int i) {
	switch (stepSpot(i)) {
	case Palette: return "Drag any part from here onto the canvas, or press " + shortcuts::keysFor(CMD_ADD_GATE, "A") + " and type its name.";
	case Canvas: return "Wire two parts by dragging from one pin to another, and click a switch to flip it.";
	case ToolbarSpot: return "New, undo, copy and paste, and zoom live up here; rest on a button for its name and shortcut.";
	case Actions:
		return prefs().actionsCollapsed
		           ? std::string("The buttons you use most, down the right edge. You keep it folded to a thin strip there: ") +
		                 (prefs().actionsHoverOpen ? "rest the pointer on it or click it" : "click it") + " to bring it out."
		           : std::string("The buttons you use most, down the right edge. Click its left edge to fold it away, and right-click it to "
		                         "choose what’s on it.");
	case Run:
		return "Pause, step and set the speed here; the play button opens Simulation View" + shortcuts::tipKeys(CMD_SIM_VIEW) +
		       " to watch signals move.";
	case Tabs: return "A circuit can have several pages: the + adds one, and a click on a tab goes to it.";
	case MenuBar:
	default:
		return "Help is in the menus at the top of the window, and Settings is under File" + shortcuts::tipKeys(CMD_PREFERENCES) +
		       ". Help ▸ Show Me Around plays this again.";
	}
}

std::vector<int> shown() {
	std::vector<int> out;
	for (int i = 0; i < kStepCount; i++) if (spotShowing(kSteps[i].spot)) out.push_back(i);
	return out;
}

int position() {
	const std::vector<int> s = shown();
	return (int)(std::find(s.begin(), s.end(), g.step) - s.begin()) + 1;
}
bool isFirst() { const std::vector<int> s = shown(); return s.empty() || g.step <= s.front(); }
bool isLast() { const std::vector<int> s = shown(); return s.empty() || g.step >= s.back(); }

// ---- Starting and stopping -------------------------------------------------------------

void start(CircuitWindow* window, double settle) {
	if (!alive(window)) return;
	stop();
	stopWatching();
	welcome::stopTour();
	g.target = window;
	// Out of focus mode and Simulation View (they hide the places it shows).
	if (window->focusMode()) window->toggleFocusMode();
	if (window->simView()) window->toggleSimView();
	if (!quiet) SetForegroundWindow(window->window());
	if (settle > 0) g.settleTimer = SetTimer(nullptr, 0, (UINT)std::lround(settle * 1000), settleTick);
	else begin(0, false);
}

void preview(CircuitWindow* window, int step) {
	if (!alive(window)) return;
	stop();
	g.target = window;
	if (window->focusMode()) window->toggleFocusMode();
	if (window->simView()) window->toggleSimView();
	begin(std::min(std::max(step, 0), kStepCount - 1), true);
	// The panel may have opened for its stop: measured again, in place.
	measure();
	aim(true);
	render();
}

void end() {
	if (!g.active) return;
	CircuitWindow* w = g.target;
	prefs().showMeAroundSeen = true;
	prefs().save();
	stop();
	if (g.then) {
		g_thenPending = std::move(g.then);
		g.then = nullptr;
		g_thenWindow = w;
		SetTimer(nullptr, 0, 250, thenTick);
	}
}

void stop() {
	if (g.settleTimer) KillTimer(nullptr, g.settleTimer);
	g.settleTimer = 0;
	const bool wasActive = g.active;
	const int was = g.step;
	g.active = false;
	if (wasActive) {
		panelMaybeChanged(was, -1);
		if (g.overlay && !g.picture && fade::animations()) {
			g.leaving = true;
			g.leaveAt = nowSeconds();
		} else {
			destroyOverlay();
		}
	}
	g.picture = false;
	g.target = nullptr;
}

bool active() { return g.active; }
CircuitWindow* target() { return g.active ? g.target : nullptr; }
int step() { return g.step; }

void next() {
	if (!g.active) return;
	const std::vector<int> s = shown();
	for (int i : s)
		if (i > g.step) {
			if (!fade::animations()) {
				g.fadePhase = 1;
				g.fadeAt = nowSeconds();
				g.pendingStep = i;
			} else {
				setStep(i);
				aim(false);
			}
			g.dirty = true;
			return;
		}
	end();
}

void back() {
	if (!g.active) return;
	const std::vector<int> s = shown();
	for (auto it = s.rbegin(); it != s.rend(); ++it)
		if (*it < g.step) {
			if (!fade::animations()) {
				g.fadePhase = 1;
				g.fadeAt = nowSeconds();
				g.pendingStep = *it;
			} else {
				setStep(*it);
				aim(false);
			}
			g.dirty = true;
			return;
		}
}

bool seen() { return prefs().showMeAroundSeen; }
void setThen(std::function<void(CircuitWindow*)> then) { g.then = std::move(then); }
bool hasThen() { return (bool)g.then; }

bool welcomeClosed(WelcomeExit exit) {
	if (seen()) return false;
	if (exit == WelcomeExit::GuidedTour) g.then = [](CircuitWindow* w) { welcome::startTourOn(w); };
	startWhenReady();
	return true;
}

void startWhenReady() {
	if (seen() || g.active || g.settleTimer) return;
	g.wanted = true;
	if (quiet || g.autoTimer) return;
	g.readySince = -1;
	g.autoTimer = SetTimer(nullptr, 0, 300, autoTick);
}

bool watching() { return g.wanted; }

void stopWatching() {
	if (g.autoTimer) KillTimer(nullptr, g.autoTimer);
	g.autoTimer = 0;
	g.wanted = false;
}

CircuitWindow* readyWindow() {
	if (splash::active() || intro::active() || welcome::isOpen() || whatsnew::isOpen() || welcome::tourWindow()) return nullptr;
	// The privacy question (Privacy.h), if it's owed, comes first.
	if (privacy::holdsShowMeAround()) return nullptr;
	CircuitWindow* w = frontWindow();
	if (!w) return nullptr;
	HWND h = w->window();
	// Not under a dialog (a modal one disables it), minimized or hidden.
	if (!IsWindowVisible(h) || IsIconic(h) || !IsWindowEnabled(h)) return nullptr;
	return w;
}

bool holdsPanelOpen(const CircuitWindow* w) {
	if (!g.active || g.target != w) return false;
	return kSteps[std::min(std::max(g.step, 0), kStepCount - 1)].spot == Actions;
}

bool key(const MSG& m) {
	if (!g.active || m.message != WM_KEYDOWN || !alive(g.target)) return false;
	HWND w = g.target->window();
	if (GetAncestor(m.hwnd, GA_ROOT) != w || !IsWindowEnabled(w)) return false;
	if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000)) return false;
	switch (m.wParam) {
	case VK_ESCAPE: end(); return true;
	case VK_RIGHT: case VK_RETURN: case VK_SPACE: next(); return true;
	case VK_LEFT: back(); return true;
	// Shift, and the keys that only move between windows, go through.
	case VK_SHIFT: case VK_CONTROL: case VK_MENU: case VK_TAB: case VK_LWIN: case VK_RWIN: return false;
	default: return true;   // the canvas doesn't get plain keys meanwhile
	}
}

void windowClosed(CircuitWindow* w) {
	if (g.target == w) {
		g.target = nullptr;   // gone: nothing of it to lay out again
		g.active = false;
		g.picture = false;
		if (g.settleTimer) KillTimer(nullptr, g.settleTimer);
		g.settleTimer = 0;
		destroyOverlay();
	}
	if (g_thenWindow == w) g_thenWindow = nullptr;
}

bool overlayShown() { return g.overlay && IsWindowVisible(g.overlay) && g.active; }

RECT holeOnScreen() {
	RECT r = { 0, 0, 0, 0 };
	if (!g.active) return r;
	const D2D1_RECT_F h = g.holeTo;
	const float s = (float)g.scale;
	r = { g.client.left + (LONG)std::lround(h.left * s), g.client.top + (LONG)std::lround(h.top * s), g.client.left + (LONG)std::lround(h.right * s),
	      g.client.top + (LONG)std::lround(h.bottom * s) };
	return r;
}

bool saveScreenshot(const std::string& file) {
	if (!g.active || !alive(g.target)) return false;
	// The overlay drawn whole (no fade), then the window under it.
	g.picture = true;
	measure();
	aim(true);
	render();
	HWND h = g.target->window();
	RECT wr;
	GetWindowRect(h, &wr);
	const int w = wr.right - wr.left, ht = wr.bottom - wr.top;
	if (w <= 0 || ht <= 0 || !g.bits) return false;
	HDC screen = GetDC(nullptr);
	HDC mem = CreateCompatibleDC(screen);
	ReleaseDC(nullptr, screen);
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof bi.bmiHeader;
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -ht;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void* px = nullptr;
	HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &px, nullptr, 0);
	bool ok = false;
	if (dib && px) {
		HGDIOBJ old = SelectObject(mem, dib);
		if (PrintWindow(h, mem, PW_RENDERFULLCONTENT)) {
			GdiFlush();
			uint32_t* dst = static_cast<uint32_t*>(px);
			const uint32_t* src = static_cast<const uint32_t*>(g.bits);
			const int ox = g.client.left - wr.left, oy = g.client.top - wr.top;
			for (int y = 0; y < ht; y++)
				for (int x = 0; x < w; x++) {
					uint32_t d = dst[(size_t)y * w + x] | 0xFF000000u;
					const int sx = x - ox, sy = y - oy;
					if (sx >= 0 && sy >= 0 && sx < g.pw && sy < g.ph) {
						// Premultiplied over: out = s + d (1 - a).
						const uint32_t s = src[(size_t)sy * g.pw + sx];
						const uint32_t a = s >> 24, k = 255 - a;
						auto ch = [&](int shift) {
							const uint32_t v = ((s >> shift) & 0xFF) + (((d >> shift) & 0xFF) * k + 127) / 255;
							return std::min<uint32_t>(255, v) << shift;
						};
						d = 0xFF000000u | ch(16) | ch(8) | ch(0);
					}
					dst[(size_t)y * w + x] = d;
				}
			// Cut to the window you see (not the invisible edges it's resized by).
			RECT v = wr;
			if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &v, sizeof v)) || !IntersectRect(&v, &v, &wr)) v = wr;
			const int cx = v.left - wr.left, cy = v.top - wr.top, cw = v.right - v.left, chh = v.bottom - v.top;
			IWICImagingFactory* wic = nullptr;
			CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
			IWICBitmap* b = nullptr;
			if (wic && cw > 0 && chh > 0 &&
			    SUCCEEDED(wic->CreateBitmapFromMemory((UINT)cw, (UINT)chh, GUID_WICPixelFormat32bppPBGRA, (UINT)w * 4, (UINT)(w * (chh - 1) + cw) * 4,
			                                          (BYTE*)(dst + (size_t)cy * w + cx), &b)))
				ok = capture::savePng(b, file);
			if (b) b->Release();
			if (wic) wic->Release();
		}
		SelectObject(mem, old);
	}
	if (dib) DeleteObject(dib);
	DeleteDC(mem);
	return ok;
}

}  // namespace showme
