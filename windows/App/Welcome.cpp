// The welcome, the guided tour and What's New (see Welcome.h), as the Mac
// app's Welcome.swift and WhatsNew.swift: in the brand's look (Brand.h),
// with the wx and Mac page slide -- the new page drifts in as it fades up,
// the old one away as it fades out. (The launch screen is Splash.cpp.)

#include "Welcome.h"
#include "Brand.h"
#include "Chrome.h"
#include "Commands.h"
#include "Dialogs.h"
#include "Window.h"

#include <commctrl.h>
#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

using namespace brand;

namespace {

const DWRITE_FONT_WEIGHT kBold = DWRITE_FONT_WEIGHT_BOLD, kSemi = DWRITE_FONT_WEIGHT_SEMI_BOLD, kMedium = DWRITE_FONT_WEIGHT_MEDIUM,
                         kNormal = DWRITE_FONT_WEIGHT_NORMAL;

bool windowAlive(CircuitWindow* w) {
	return w && std::find(circuitWindows().begin(), circuitWindows().end(), w) != circuitWindows().end();
}

bool reduceMotion() {
	BOOL animations = TRUE;
	SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
	return !animations;
}

// Centred on a window (or the screen it's on), w x h points.
RECT centeredOn(HWND anchor, int w, int h) {
	RECT a;
	if (anchor == nullptr || !GetWindowRect(anchor, &a)) {
		POINT p = { 0, 0 };
		MONITORINFO mi = { sizeof mi };
		GetMonitorInfoW(MonitorFromPoint(p, MONITOR_DEFAULTTOPRIMARY), &mi);
		a = mi.rcWork;
	}
	const UINT dpi = anchor ? dpiOf(anchor) : 96;
	const int pw = scaled(w, dpi), ph = scaled(h, dpi);
	int x = (a.left + a.right - pw) / 2, y = (a.top + a.bottom - ph) / 2;
	MONITORINFO mi = { sizeof mi };
	if (GetMonitorInfoW(MonitorFromRect(&a, MONITOR_DEFAULTTONEAREST), &mi)) {
		x = std::max<int>(mi.rcWork.left, std::min<int>(x, mi.rcWork.right - pw));
		y = std::max<int>(mi.rcWork.top, std::min<int>(y, mi.rcWork.bottom - ph));
	}
	return RECT{ x, y, x + pw, y + ph };
}

// ---- A brand window -----------------------------------------------------------
// A plain popup whose drawing, clicks and keys lambdas handle; it can be
// dragged by any part that isn't a control. It redraws every frame while
// `animating` says so.

struct Hit { D2D1_RECT_F r; int id; };

struct Panel {
	HWND hwnd = nullptr;
	WindowSurface surface;
	std::vector<Hit> hits;            // this frame's controls
	int hot = -1;
	std::function<void(ID2D1RenderTarget*, float, float)> paint;
	std::function<void(int id)> click;
	std::function<bool(UINT vk)> key;
	std::function<void(wchar_t c)> chr;
	std::function<bool()> animating;
	std::function<bool(UINT, WPARAM, LPARAM, LRESULT&)> message;   // anything else, first

	void addHit(const D2D1_RECT_F& r, int id) { hits.push_back({ r, id }); }
	int hitAt(float x, float y) const {
		for (auto it = hits.rbegin(); it != hits.rend(); ++it) if (inRect(it->r, x, y)) return it->id;
		return -1;
	}
	void redraw() { if (hwnd) InvalidateRect(hwnd, nullptr, FALSE); }

	static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
		Panel* p = reinterpret_cast<Panel*>(GetWindowLongPtrW(h, GWLP_USERDATA));
		if (p == nullptr) return DefWindowProcW(h, msg, wp, lp);
		const double s = dpiOf(h) / 96.0;
		LRESULT result = 0;
		bool handled = true;
		guarded("a window", [&] {
			if (p->message && p->message(msg, wp, lp, result)) return;
			switch (msg) {
			case WM_PAINT: {
				PAINTSTRUCT ps;
				BeginPaint(h, &ps);
				if (ID2D1HwndRenderTarget* rt = p->surface.begin(h)) {
					RECT rc;
					GetClientRect(h, &rc);
					p->hits.clear();
					if (p->paint) p->paint(rt, (float)(rc.right / s), (float)(rc.bottom / s));
					p->surface.end();
				}
				EndPaint(h, &ps);
				break;
			}
			case WM_ERASEBKGND: result = 1; break;
			case WM_NCHITTEST: {
				POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
				ScreenToClient(h, &pt);
				// Dragged by its ground; the controls take clicks.
				result = p->hitAt((float)(pt.x / s), (float)(pt.y / s)) >= 0 ? HTCLIENT : HTCAPTION;
				break;
			}
			case WM_LBUTTONUP: {
				const int id = p->hitAt((float)(GET_X_LPARAM(lp) / s), (float)(GET_Y_LPARAM(lp) / s));
				if (id >= 0 && p->click) p->click(id);
				break;
			}
			case WM_MOUSEMOVE: {
				TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 };
				TrackMouseEvent(&t);
				const int id = p->hitAt((float)(GET_X_LPARAM(lp) / s), (float)(GET_Y_LPARAM(lp) / s));
				if (id != p->hot) { p->hot = id; p->redraw(); }
				SetCursor(LoadCursor(nullptr, id >= 0 ? IDC_HAND : IDC_ARROW));
				break;
			}
			case WM_MOUSELEAVE: if (p->hot != -1) { p->hot = -1; p->redraw(); } break;
			case WM_SETCURSOR:
				if (LOWORD(lp) == HTCLIENT) { SetCursor(LoadCursor(nullptr, p->hot >= 0 ? IDC_HAND : IDC_ARROW)); result = TRUE; }
				else handled = false;
				break;
			case WM_KEYDOWN: if (!p->key || !p->key((UINT)wp)) handled = false; break;
			case WM_CHAR: if (p->chr) p->chr((wchar_t)wp); break;
			case WM_TIMER: if (p->animating && p->animating()) p->redraw(); break;
			case WM_DESTROY: {
				// Gone some other way than destroy() (which detaches first):
				// the owner it disabled takes input again.
				HWND owner = GetWindow(h, GW_OWNER);
				if (owner && !IsWindowEnabled(owner)) EnableWindow(owner, TRUE);
				p->hwnd = nullptr;
				handled = false;
				break;
			}
			default: handled = false; break;
			}
		});
		return handled ? result : DefWindowProcW(h, msg, wp, lp);
	}

	void create(HWND owner, int w, int h, int x, int y, DWORD ex = 0) {
		static bool registered = false;
		if (!registered) {
			registered = true;
			WNDCLASSEXW wc = {};
			wc.cbSize = sizeof wc;
			wc.style = CS_DROPSHADOW;
			wc.lpfnWndProc = proc;
			wc.hInstance = appInstance();
			wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
			wc.lpszClassName = L"CedarLogicBrandPanel";
			RegisterClassExW(&wc);
		}
		hwnd = CreateWindowExW(ex, L"CedarLogicBrandPanel", L"CedarLogic", WS_POPUP | WS_CLIPCHILDREN, x, y, w, h, owner, nullptr, appInstance(),
		                       nullptr);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)this);
		const DWORD round = 2;   // DWMWCP_ROUND (Windows 11; ignored before)
		DwmSetWindowAttribute(hwnd, 33, &round, sizeof round);
		const BOOL dark = TRUE;
		DwmSetWindowAttribute(hwnd, 20, &dark, sizeof dark);
		SetTimer(hwnd, 1, 15, nullptr);
	}
	void destroy() {
		if (hwnd) {
			KillTimer(hwnd, 1);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
			DestroyWindow(hwnd);
			hwnd = nullptr;
		}
	}
};

// The page slide: 0.32 s; the new page drifts 60 points in as it fades up,
// the old one 60 points away as it fades out.
struct Pager {
	int page = 0, from = -1;
	bool forward = true;
	double changedAt = -10;
	bool calm = reduceMotion();

	void go(int to) {
		if (to == page) return;
		forward = to > page;
		from = page;
		page = to;
		changedAt = nowSeconds();
	}
	double progress() const { return calm ? 1 : std::min(1.0, (nowSeconds() - changedAt) / 0.32); }
	bool sliding() const { return from >= 0 && progress() < 1; }
	// Draws the page (and, while sliding, the one it came from) in the area.
	void draw(ID2D1RenderTarget* rt, Panel& panel, const D2D1_RECT_F& area, const std::function<void(int page)>& paintPage) {
		rt->PushAxisAlignedClip(area, D2D1_ANTIALIAS_MODE_ALIASED);
		D2D1_MATRIX_3X2_F was;
		rt->GetTransform(&was);
		const double e = slideCurve(progress());
		auto one = [&](int pg, float dx, float opacity, bool live) {
			if (opacity <= 0.003f) return;
			const size_t hitsBefore = panel.hits.size();
			rt->SetTransform(D2D1::Matrix3x2F::Translation(dx, 0) * was);
			rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), opacity),
			              nullptr);
			paintPage(pg);
			rt->PopLayer();
			rt->SetTransform(was);
			// Only the page arriving takes clicks; its controls are where it's going.
			if (!live) panel.hits.resize(hitsBefore);
		};
		if (sliding()) {
			one(from, (float)((forward ? -60 : 60) * e), (float)(1 - e), false);
			one(page, (float)((forward ? 60 : -60) * (1 - e)), (float)e, true);
		} else {
			one(page, 0, 1, true);
		}
		rt->PopAxisAlignedClip();
	}
};

// The footer: the page dots (the one you're on a neon capsule).
void dots(ID2D1RenderTarget* rt, float x, float cy, int count, int current) {
	for (int i = 0; i < count; i++) {
		const bool on = i == current;
		const float w = on ? 22.0f : 8.0f;
		const D2D1_RECT_F r = D2D1::RectF(x, cy - 4, x + w, cy + 4);
		if (on) glow(rt, r, 4, alpha(kNeon, 0.6f), 5);
		fillRound(rt, r, 4, on ? kNeon : D2D1::ColorF(1, 1, 1, 0.18f));
		x += w + 6;
	}
}

// A glyph from the system's icon font, in neon.
void glyph(ID2D1RenderTarget* rt, wchar_t g, float x, float y, float size, D2D1_COLOR_F c = kNeon) {
	drawIcon(rt, g, D2D1::RectF(x, y, x + size * 1.6f, y + size * 1.6f), size, c);
}

// A card with an icon, a title and a line (the Mac's BrandCard rows).
float pointCard(ID2D1RenderTarget* rt, float x, float y, float w, wchar_t icon, const std::string& title, const std::string& line,
                float minH = 0) {
	const float textW = w - 66 - 18;
	const float lh = text(nullptr, line, 0, 0, 12.5f, kNormal, kSecondary, textW);
	const float h = std::max(minH, 16 + 20 + 4 + lh + 16);
	card(rt, D2D1::RectF(x, y, x + w, y + h));
	glyph(rt, icon, x + 14, y + 13, 15);
	text(rt, title, x + 48, y + 14, 14, kBold, kPrimary, textW + 18);
	text(rt, line, x + 48, y + 36, 12.5f, kNormal, kSecondary, textW + 18);
	return h;
}

}  // namespace

// ---- The welcome ------------------------------------------------------------------

namespace welcome {

namespace {

enum { kSkip = 1, kBack, kNext, kJustStart, kTheme0 = 10, kSwatch0 = 20, kGrid0 = 40, kReady0 = 50 };
const int kPages = 5;
const float kWW = 780, kWH = 580;
const int kAccentOrder[] = { 6, 0, 1, 2, 3, 4, 5 };
const char* kAccentNames[] = { "Blue", "Purple", "Pink", "Orange", "Green", "Graphite", "CedarLogic" };

struct KeyCard { const char* key; const char* title; const char* line; };
const KeyCard kKeys[] = {
	{ "A", "Add a gate", "Type part of its name, press Enter, click to drop it." },
	{ "C", "Copy, or connect", "Copies the selection. While dragging a gate, drops it and wires it to pins nearby." },
	{ "D", "Duplicate", "Copies the selection and puts the copy on your mouse." },
	{ "R", "Rotate", "Turns the selected gates a quarter turn." },
	{ "S", "Straighten", "Tidies the selected wires into clean routes." },
	{ "T", "Truth table", "Tries every switch combination and writes down the lights." },
};

struct Welcome {
	Panel panel;
	Pager pager;
	CircuitWindow* window = nullptr;
	double opened = nowSeconds();
	int readyChoice = 0;
	double pressedAt[6] = { -10, -10, -10, -10, -10, -10 };
	HWND name = nullptr;
	HFONT nameFont = nullptr;
	HBRUSH nameBrush = nullptr;
	D2D1_RECT_F nameBox{};
};
Welcome* g_welcome = nullptr;

void startTourFrom(CircuitWindow* w);

void finish(int then = 0) {
	Welcome* w = g_welcome;
	if (w == nullptr) return;
	g_welcome = nullptr;
	prefs().hasSeenWelcome = true;
	prefs().seenWhatsNew = whatsnew::kVersion;   // new to it all: nothing to catch up on
	prefs().save();
	CircuitWindow* window = windowAlive(w->window) ? w->window : nullptr;
	if (window) EnableWindow(window->window(), TRUE);
	w->panel.destroy();
	if (w->nameFont) DeleteObject(w->nameFont);
	if (w->nameBrush) DeleteObject(w->nameBrush);
	delete w;
	if (window == nullptr) return;
	SetForegroundWindow(window->window());
	if (then == CMD_TOUR) startTourFrom(window);
	else if (then) PostMessageW(window->window(), WM_COMMAND, then, 0);
}

void go(int d) {
	Welcome* w = g_welcome;
	if (w == nullptr) return;
	const int to = std::max(0, std::min(kPages - 1, w->pager.page + d));
	if (to == w->pager.page) return;
	w->pager.go(to);
	ShowWindow(w->name, SW_HIDE);
	SetFocus(w->panel.hwnd);
	w->panel.redraw();
}

void readyAction(int i) {
	switch (i) {
	case 0: finish(CMD_TOUR); break;
	case 1: finish(CMD_NEW_TEMPLATE); break;
	case 2: finish(); break;
	default: finish(CMD_SHORTCUTS); break;
	}
}

void setTheme(int mode) {
	Prefs& p = prefs();
	p.themeMode = mode;
	p.dark = mode == 2 ? true : mode == 1 ? false : systemPrefersDark();
	p.save();
	applyTheme();
}

void applyLook() {
	prefs().save();
	for (CircuitWindow* w : circuitWindows()) w->prefsChanged();
	applyTheme();
}

// The pages ------------------------------------------------------------------------

void pageIntro(ID2D1RenderTarget* rt, double t) {
	icon(rt, 56, 34, 58, 0.45f);
	text(rt, "WELCOME TO", 130, 40, 10.5f, kSemi, kDim, 0, DWRITE_TEXT_ALIGNMENT_LEADING, 2.2f);
	text(rt, "CedarLogic", 130, 54, 30, kSemi, D2D1::ColorF(0.88f, 0.9f, 0.89f));
	hero(rt, D2D1::RectF(0, 100, kWW, 260), t);
	text(rt, "Build it. Watch it think.", 56, 262, 24, kBold, kPrimary);
	text(rt, "Design logic circuits, run them live, and hand them in, in the time it takes to sketch one on paper.", 56, 296, 13.5f, kNormal,
	     kSecondary, kWW - 112);
	struct P { wchar_t icon; const char* title; const char* line; };
	const P points[] = {
		{ 0xE8F1, "It keeps itself", "Everything saves as you go, in Your Circuits, with versions to go back to." },
		{ 0xE9D9, "It shows its work", "Live wires, a truth table on one key, and an oscilloscope." },
		{ 0xE765, "It stays out of the way", "Nearly everything has a key. You won't need the menus." },
	};
	const float cw = (kWW - 112 - 28) / 3;
	for (int i = 0; i < 3; i++) {
		const float x = 56 + i * (cw + 14), y = 334;
		const float lh = text(nullptr, points[i].line, 0, 0, 11.5f, kNormal, kSecondary, cw - 28);
		card(rt, D2D1::RectF(x, y, x + cw, y + std::max(88.0f, 14 + 20 + 7 + lh + 14)));
		glyph(rt, points[i].icon, x + 12, y + 12, 12);
		text(rt, points[i].title, x + 36, y + 13, 13, kBold, kPrimary, cw - 46);
		text(rt, points[i].line, x + 14, y + 41, 11.5f, kNormal, kSecondary, cw - 28);
	}
}

void pageSetup(ID2D1RenderTarget* rt, Panel& panel) {
	float y = heading(rt, 56, 34, kWW - 112, "Make it yours", "Your canvas, your way",
	                  "It all applies as you pick it; the window behind this one is the preview. Preferences (Ctrl+,) has the rest.");
	y += 22;
	label(rt, 56, y, "Appearance");
	std::vector<D2D1_RECT_F> hits;
	segmented(rt, 56, y + 24, { "System", "Light", "Dark" }, std::min(prefs().themeMode, 2), hits);
	for (size_t i = 0; i < hits.size(); i++) panel.addHit(hits[i], kTheme0 + (int)i);
	y += 24 + 34 + 22;
	label(rt, 56, y, "The app's colour");
	y += 26;
	for (int i = 0; i < 7; i++) {
		const int a = kAccentOrder[i];
		const bool on = prefs().accent == a;
		double r, g, b;
		cl_accent_color(a, true, &r, &g, &b);
		const D2D1_COLOR_F c = D2D1::ColorF((float)r, (float)g, (float)b);
		const float cx = 56 + 31 + i * (62 + 12), cy = y + 14;
		if (on) {
			fillCircle(rt, D2D1::Point2F(cx, cy), 24, alpha(c, 0.22f));
			ID2D1SolidColorBrush* ring = nullptr;
			if (SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.9f), &ring))) {
				rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 17, 17), ring, 2);
				ring->Release();
			}
		}
		fillCircle(rt, D2D1::Point2F(cx, cy), 14, c);
		text(rt, kAccentNames[a], cx - 31, cy + 22, 10, on ? kBold : kNormal, on ? kPrimary : kFaint, 62, DWRITE_TEXT_ALIGNMENT_CENTER);
		panel.addHit(D2D1::RectF(cx - 31, cy - 18, cx + 31, cy + 36), kSwatch0 + i);
	}
	y += 28 + 6 + 14 + 22;
	label(rt, 56, y, "Grid");
	const int grid = !prefs().showGrid ? 2 : prefs().gridStyle == 1 ? 1 : 0;
	segmented(rt, 56, y + 24, { "Lines", "Dots", "Off" }, grid, hits);
	for (size_t i = 0; i < hits.size(); i++) panel.addHit(hits[i], kGrid0 + (int)i);
	const char* blurbs[] = { "Fine lines, every fifth one darker, like graph paper.", "A dot where the lines would cross: quieter, just as easy to line up.",
	                         "A clean canvas. Gates still snap into place." };
	text(rt, blurbs[grid], 56, y + 24 + 34 + 14, 11.5f, kNormal, kSecondary);
}

void pageName(ID2D1RenderTarget* rt, Welcome* w) {
	const float left = 56, colW = 420;
	float y = heading(rt, left, 34, colW, "One more thing", "Who's handing this in?",
	                  "Your name goes on every circuit you export, above the line that says whether it works: the first thing a grader looks for.");
	y += 20;
	label(rt, left, y, "Your name");
	y += 24;
	w->nameBox = D2D1::RectF(left, y, left + 300, y + 38);
	// Opaque, the colour of the box typed in on it.
	fillRound(rt, w->nameBox, 9, D2D1::ColorF(25 / 255.0f, 33 / 255.0f, 28 / 255.0f));
	strokeRound(rt, D2D1::RectF(w->nameBox.left + 0.5f, w->nameBox.top + 0.5f, w->nameBox.right - 0.5f, w->nameBox.bottom - 0.5f), 9,
	            alpha(kNeon, 0.45f));
	if (prefs().studentName.empty() && GetFocus() != w->name)
		text(rt, "First and last name", left + 13, y + 9, 15, kNormal, alpha(kPrimary, 0.3f));
	text(rt, "Rather not? Leave it blank. It's in Preferences whenever you want it.", left, y + 38 + 16, 11.5f, kNormal, kFaint);
	// What an export's footer looks like, with the name in it.
	D2D1_MATRIX_3X2_F was;
	rt->GetTransform(&was);
	const D2D1_RECT_F paper = D2D1::RectF(474, 104, 724, 254);
	rt->SetTransform(D2D1::Matrix3x2F::Rotation(2, D2D1::Point2F((paper.left + paper.right) / 2, (paper.top + paper.bottom) / 2)) * was);
	glow(rt, paper, 12, alpha(kNeon, 0.3f), 18);
	fillRound(rt, paper, 12, D2D1::ColorF(1, 1, 1));
	const D2D1_COLOR_F faintInk = D2D1::ColorF(0, 0, 0, 0.4f);
	const float mx = (paper.left + paper.right) / 2;
	strokeRound(rt, D2D1::RectF(mx - 15, paper.top + 22, mx + 15, paper.top + 46), 3, faintInk, 1.2f);
	fillRect(rt, D2D1::RectF(mx - 0.6f, paper.top + 46, mx + 0.6f, paper.top + 54), faintInk);
	fillCircle(rt, D2D1::Point2F(mx, paper.top + 55), 2.6f, faintInk);
	fillRect(rt, D2D1::RectF(mx - 15, paper.top + 55, mx + 15, paper.top + 56), faintInk);
	fillRect(rt, D2D1::RectF(paper.left + 20, paper.top + 70, paper.right - 20, paper.top + 71), D2D1::ColorF(0, 0, 0, 0.15f));
	const std::string shown = prefs().studentName.empty() ? "________________" : prefs().studentName;
	text(rt, shown, paper.left + 20, paper.top + 82, 15, kBold, D2D1::ColorF(0, 0, 0), 210);
	text(rt, "This circuit works as specified.", paper.left + 20, paper.top + 108, 11, kNormal, D2D1::ColorF(0, 0, 0, 0.6f));
	rt->SetTransform(was);
}

void pageKeys(ID2D1RenderTarget* rt, Welcome* w) {
	heading(rt, 56, 34, kWW - 112, "The fast way", "Six keys worth knowing",
	        "Try them now: press any of these and watch it light up. Press ? any time for the full list.");
	const float cw = (kWW - 112 - 28) / 3, ch = 136;
	const double now = nowSeconds();
	for (int i = 0; i < 6; i++) {
		const float x = 56 + (i % 3) * (cw + 14), y = 134 + (i / 3) * (ch + 14);
		// Lit for a moment after its key, then easing off.
		const double since = now - w->pressedAt[i];
		const bool lit = since < 0.45 + 0.3;
		card(rt, D2D1::RectF(x, y, x + cw, y + ch), lit);
		keycap(rt, x + 14, y + 14, kKeys[i].key, lit);
		text(rt, kKeys[i].title, x + 14, y + 62, 13, kBold, kPrimary, cw - 28);
		text(rt, kKeys[i].line, x + 14, y + 84, 11, kNormal, kSecondary, cw - 28);
	}
}

void pageReady(ID2D1RenderTarget* rt, Welcome* w, double t) {
	heading(rt, 56, 30, kWW - 112, "Ready", "Build your first circuit", "↑↓ and Enter work here too; ← goes back.");
	struct Tile { const char* title; const char* line; wchar_t icon; };
	const Tile tiles[] = {
		{ "Take the guided tour", "Two switches, a gate and a light, in about five minutes. Recommended.", 0 },
		{ "Start from a template", "A lab page with your name, a counter, a 7-segment starter, or your own.", 0xE8C8 },
		{ "Start with a blank canvas", "Jump straight in. Help ▸ Guided Tour replays the tour.", 0xE710 },
		{ "See every shortcut", "The whole list, searchable.", 0xE765 },
	};
	float y = 126;
	for (int i = 0; i < 4; i++) {
		const bool on = i == w->readyChoice;
		const D2D1_RECT_F r = D2D1::RectF(56, y, kWW - 56, y + 62);
		card(rt, r, on, 13);
		text(rt, tiles[i].title, r.left + 18, y + 12, 13.5f, kBold, kPrimary);
		text(rt, tiles[i].line, r.left + 18, y + 33, 11.5f, kNormal, kSecondary, r.right - r.left - 220);
		if (tiles[i].icon) glyph(rt, tiles[i].icon, r.right - 52, y + 18, 16, on ? kNeon : kFaint);
		else hero(rt, D2D1::RectF(r.right - 190, y + 6, r.right - 20, y + 56), t);
		w->panel.addHit(r, kReady0 + i);
		y += 72;
	}
	glyph(rt, 0xED15, 56, y + 4, 12);
	text(rt, "Something odd, or an idea? The speech bubble in the toolbar (or Help ▸ Send Feedback) sends it straight to the developer.", 80,
	     y + 4, 11.5f, kNormal, kFaint, kWW - 136);
}

void paint(ID2D1RenderTarget* rt, float w, float h) {
	Welcome* wl = g_welcome;
	if (wl == nullptr) return;
	const double t = nowSeconds() - wl->opened;
	ground(rt, w, h);
	const float footer = h - 74;
	wl->pager.draw(rt, wl->panel, D2D1::RectF(0, 0, w, footer - 1), [&](int pg) {
		switch (pg) {
		case 0: pageIntro(rt, t); break;
		case 1: pageSetup(rt, wl->panel); break;
		case 2: pageName(rt, wl); break;
		case 3: pageKeys(rt, wl); break;
		default: pageReady(rt, wl, t); break;
		}
	});
	const int page = wl->pager.page;
	// The name's box, once its page has landed.
	if (page == 2 && !wl->pager.sliding() && wl->name) {
		const UINT dpi = dpiOf(wl->panel.hwnd);
		const int x = scaled((int)wl->nameBox.left + 13, dpi), y = scaled((int)wl->nameBox.top + 9, dpi);
		SetWindowPos(wl->name, HWND_TOP, x, y, scaled(274, dpi), scaled(22, dpi), SWP_NOACTIVATE | SWP_SHOWWINDOW);
	}
	if (page < kPages - 1) {
		const D2D1_RECT_F skip = D2D1::RectF(w - 26 - 44, 18, w - 26, 40);
		text(rt, "Skip", skip.left, skip.top, 12.5f, kMedium, wl->panel.hot == kSkip ? alpha(kPrimary, 0.7f) : kFaint, skip.right - skip.left,
		     DWRITE_TEXT_ALIGNMENT_TRAILING);
		wl->panel.addHit(skip, kSkip);
	}
	fillRect(rt, D2D1::RectF(0, footer - 1, w, footer), D2D1::ColorF(1, 1, 1, 0.07f));
	dots(rt, 44, footer + 37, kPages, page);
	float x = w - 44;
	const D2D1_RECT_F next = page < kPages - 1 ? D2D1::RectF(x - 142, footer + 19, x, footer + 55) : D2D1::RectF(x - 110, footer + 19, x, footer + 55);
	button(rt, next, page == 0 ? "Get Started" : page < kPages - 1 ? "Continue" : "Just Start", page < kPages - 1,
	       wl->panel.hot == (page < kPages - 1 ? kNext : kJustStart));
	wl->panel.addHit(next, page < kPages - 1 ? kNext : kJustStart);
	if (page > 0) {
		const D2D1_RECT_F back = D2D1::RectF(next.left - 8 - 96, footer + 19, next.left - 8, footer + 55);
		button(rt, back, "Back", false, wl->panel.hot == kBack);
		wl->panel.addHit(back, kBack);
	}
}

void click(int id) {
	Welcome* w = g_welcome;
	if (w == nullptr) return;
	if (id == kSkip || id == kJustStart) finish();
	else if (id == kNext) go(1);
	else if (id == kBack) go(-1);
	else if (id >= kTheme0 && id < kTheme0 + 3) setTheme(id - kTheme0);
	else if (id >= kSwatch0 && id < kSwatch0 + 7) { prefs().accent = kAccentOrder[id - kSwatch0]; applyLook(); }
	else if (id >= kGrid0 && id < kGrid0 + 3) {
		prefs().showGrid = id != kGrid0 + 2;
		if (id != kGrid0 + 2) prefs().gridStyle = id - kGrid0;
		applyLook();
	} else if (id >= kReady0 && id < kReady0 + 4) {
		readyAction(id - kReady0);
		return;
	}
	if (g_welcome) g_welcome->panel.redraw();
}

bool key(UINT vk) {
	Welcome* w = g_welcome;
	if (w == nullptr) return false;
	const bool last = w->pager.page == kPages - 1;
	switch (vk) {
	case VK_ESCAPE: finish(); return true;
	case VK_RETURN: if (last) readyAction(w->readyChoice); else go(1); return true;
	case VK_LEFT: go(-1); return true;
	case VK_RIGHT: go(1); return true;
	case VK_DOWN: if (last) { w->readyChoice = std::min(3, w->readyChoice + 1); w->panel.redraw(); } return true;
	case VK_UP: if (last) { w->readyChoice = std::max(0, w->readyChoice - 1); w->panel.redraw(); } return true;
	default: break;
	}
	if (w->pager.page == 3) {
		for (int i = 0; i < 6; i++)
			if (vk == (UINT)kKeys[i].key[0] && !(GetKeyState(VK_SHIFT) & 0x8000)) { w->pressedAt[i] = nowSeconds(); w->panel.redraw(); return true; }
	}
	return false;
}

// The name box: Enter goes on, Escape closes, Tab leaves it.
LRESULT CALLBACK nameProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
	if (msg == WM_KEYDOWN && (wp == VK_RETURN || wp == VK_ESCAPE)) { key((UINT)wp); return 0; }
	if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE || wp == VK_TAB)) return 0;
	if (msg == WM_KEYDOWN && wp == VK_TAB && g_welcome) { SetFocus(g_welcome->panel.hwnd); g_welcome->panel.redraw(); return 0; }
	return DefSubclassProc(h, msg, wp, lp);
}

}  // namespace

bool offer(CircuitWindow* window) {
	if (prefs().hasSeenWelcome || window == nullptr || g_welcome) return false;
	g_welcome = new Welcome();
	Welcome* w = g_welcome;
	w->window = window;
	Panel& p = w->panel;
	p.paint = paint;
	p.click = click;
	p.key = key;
	p.chr = [](wchar_t c) {
		if (c == L'?' && g_welcome) readyAction(3);
	};
	p.animating = [] {
		Welcome* wl = g_welcome;
		if (wl == nullptr) return false;
		const int pg = wl->pager.page;
		bool lit = false;
		for (double at : wl->pressedAt) lit = lit || nowSeconds() - at < 0.8;
		return wl->pager.sliding() || pg == 0 || pg == 4 || lit;
	};
	p.message = [](UINT msg, WPARAM wp, LPARAM lp, LRESULT& r) {
		Welcome* wl = g_welcome;
		if (wl == nullptr) return false;
		// Alt+F4 is Skip: the circuit window comes back.
		if (msg == WM_CLOSE) { finish(); r = 0; return true; }
		if (msg == WM_CTLCOLOREDIT && (HWND)lp == wl->name) {
			SetTextColor((HDC)wp, RGB(242, 242, 242));
			SetBkColor((HDC)wp, RGB(25, 33, 28));
			r = (LRESULT)wl->nameBrush;
			return true;
		}
		if (msg == WM_COMMAND && (HWND)lp == wl->name && HIWORD(wp) == EN_CHANGE) {
			prefs().studentName = windowText(wl->name);
			prefs().save();
			wl->panel.redraw();
			r = 0;
			return true;
		}
		return false;
	};
	const RECT r = centeredOn(window->window(), (int)kWW, (int)kWH);
	p.create(window->window(), r.right - r.left, r.bottom - r.top, r.left, r.top);
	// The name box: a plain edit on the drawn field.
	const UINT dpi = dpiOf(p.hwnd);
	LOGFONTW lf = {};
	lf.lfHeight = -MulDiv(15, (int)dpi, 96);
	lf.lfWeight = FW_NORMAL;
	lf.lfQuality = CLEARTYPE_QUALITY;
	wcscpy(lf.lfFaceName, L"Segoe UI");
	w->nameFont = CreateFontIndirectW(&lf);
	w->nameBrush = CreateSolidBrush(RGB(25, 33, 28));
	w->name = CreateWindowExW(0, L"EDIT", W(prefs().studentName).c_str(), WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10, p.hwnd, nullptr, appInstance(),
	                          nullptr);
	SendMessageW(w->name, WM_SETFONT, (WPARAM)w->nameFont, TRUE);
	SetWindowSubclass(w->name, nameProc, 1, 0);
	EnableWindow(window->window(), FALSE);
	ShowWindow(p.hwnd, SW_SHOWNORMAL);
	SetForegroundWindow(p.hwnd);
	SetFocus(p.hwnd);
	return true;
}

bool pageForScreenshot(int page) {
	if (g_welcome == nullptr) return false;
	g_welcome->pager.page = std::max(0, std::min(kPages - 1, page));
	g_welcome->pager.from = -1;
	g_welcome->panel.redraw();
	return true;
}

// ---- The guided tour --------------------------------------------------------------
// It builds an AND circuit with you on a circuit of its own (a new one, so
// it never touches your work). Its card sits in the corner of that circuit's
// window; each step moves on by itself once it's done.

namespace {

struct Step {
	const char* title;
	std::function<std::string()> body;
	std::function<std::vector<std::string>()> keys;
	// Null: press Next.
	std::function<bool(const CLTourStatus&)> check;
};

struct Tour {
	Panel panel;
	CircuitWindow* window = nullptr;
	int step = 0;
	bool done = false;
	double doneAt = 0, stepStart = 0, lastCheck = 0;
	bool sawLit = false, sawSimView = false, sawTruthTable = false;
	float shownProgress = 0;
	int height = 0;
	enum { kClose = 1, kNextStep };
};
Tour* g_tour = nullptr;

std::vector<Step>& steps() {
	static std::vector<Step> s;
	if (!s.empty()) return s;
	auto keys = [](std::vector<std::string> k) { return [k] { return k; }; };
	s = {
		{ "Add a switch",
		  [] { return std::string("Switches are your inputs. Press A, type toggle and press Enter; the switch follows your mouse, and a click drops it. "
		                          "(Or drag a Toggle Switch out of Input/Output in the panel on the left.)"); },
		  keys({ "A" }), [](const CLTourStatus& st) { return st.switches >= 1; } },
		{ "Add a second switch",
		  [] { return std::string("An AND gate has two inputs, so it needs two switches. Put another one below the first: press A again, or select "
		                          "the first and press D to duplicate it."); },
		  keys({ "D" }), [](const CLTourStatus& st) { return st.switches >= 2; } },
		{ "Add an AND gate", [] { return std::string("Press A and type and. Drop the gate to the right of your switches."); }, keys({ "A" }),
		  [](const CLTourStatus& st) { return st.hasAnd; } },
		{ "Add a light", [] { return std::string("A light (an LED) shows an output. Press A, type led, and drop it to the right of the gate."); },
		  keys({ "A" }), [](const CLTourStatus& st) { return st.lights > 0; } },
		{ "Wire in the switches",
		  [] { return std::string("Drag from a switch's pin (the little stub on its edge) to one of the gate's inputs. Or click the pin, let go, "
		                          "and click the other one. Do it for both switches."); },
		  keys({ "drag" }), [](const CLTourStatus& st) { return st.andInputsWired >= 2; } },
		{ "Wire in the light", [] { return std::string("Now connect the gate's output, on its right-hand side, to the light."); }, keys({ "drag" }),
		  [](const CLTourStatus& st) { return st.lightWired; } },
		{ "Switch them both on",
		  [] { return std::string("Click the middle of each switch. When both are on, the light comes on: that is all AND means, this and that."); },
		  keys({ "click" }),
		  [](const CLTourStatus& st) {
			  if (st.lightOn) g_tour->sawLit = true;
			  return g_tour->sawLit;
		  } },
		{ "Now turn one off", [] { return std::string("Click either switch. The light goes out: AND needs every input on."); }, keys({ "click" }),
		  [](const CLTourStatus& st) { return !st.lightOn && st.switchesOn < 2; } },
		{ "Watch it run",
		  [] {
			  return g_tour->window->simView() || g_tour->sawSimView
			             ? std::string("Signals move along every wire carrying a 1. Flip a switch and watch them go. Press Esc when you've seen enough.")
			             : std::string("Simulation View shows the circuit working: lit wires, with the signal marching along them. Press Ctrl+R.");
		  },
		  [] {
			  return g_tour->window->simView() || g_tour->sawSimView ? std::vector<std::string>{ "Esc" } : std::vector<std::string>{ "Ctrl", "R" };
		  },
		  [](const CLTourStatus&) {
			  if (g_tour->window->simView()) g_tour->sawSimView = true;
			  return g_tour->sawSimView && !g_tour->window->simView();
		  } },
		{ "Check it with a truth table",
		  [] { return std::string("Press T. CedarLogic tries every combination of the switches and writes down what the light did: a quick way to "
		                          "check your work before you hand it in. Close it when you're done."); },
		  keys({ "T" }),
		  [](const CLTourStatus&) {
			  if (truthTableOpen()) g_tour->sawTruthTable = true;
			  return g_tour->sawTruthTable && !truthTableOpen();
		  } },
		{ "Give it a name",
		  [] { return std::string("It's already in Your Circuits (Ctrl+O) and saves itself as you go. Click “Untitled Circuit” at the top "
		                          "of the window and choose Rename… to name it. (Ctrl+S keeps a version you can go back to.)"); },
		  keys({}),
		  [](const CLTourStatus&) {
			  const std::string n = g_tour->window->titleText();
			  return !g_tour->window->filePath().empty() && n != "Untitled" && n != "Untitled Circuit";
		  } },
		{ "You built a working circuit",
		  [] { return std::string("That's the loop: add, wire, try it, check it. Press ? whenever you want every shortcut, and Help ▸ Guided Tour "
		                          "brings this back. Something odd, or an idea? The speech bubble in the toolbar sends it in."); },
		  keys({ "?" }), nullptr },
	};
	return s;
}

void endTour() {
	if (g_tour == nullptr) return;
	Tour* t = g_tour;
	g_tour = nullptr;
	t->panel.destroy();
	delete t;
}

const float kCardW = 360;

// The card's height for this step.
int cardHeight(Tour* t) {
	const Step& s = steps()[t->step];
	float h = 18 + 18 + 12 + 3 + 12 + 24 + 8;
	h += text(nullptr, s.body(), 0, 0, 12.5f, kNormal, kSecondary, kCardW - 36);
	if (!s.keys().empty()) h += 12 + 26;
	h += 14 + 36 + 18;
	return (int)std::ceil(h);
}

// In the corner of its circuit's window, over the canvas.
void place() {
	Tour* t = g_tour;
	if (t == nullptr) return;
	if (!windowAlive(t->window)) { endTour(); return; }
	HWND owner = t->window->window();
	if (IsIconic(owner)) { ShowWindow(t->panel.hwnd, SW_HIDE); return; }
	RECT rc;
	GetClientRect(owner, &rc);
	POINT br = { rc.right, rc.bottom };
	ClientToScreen(owner, &br);
	const UINT dpi = dpiOf(owner);
	const int w = scaled((int)kCardW, dpi), h = scaled(cardHeight(t), dpi);
	SetWindowPos(t->panel.hwnd, HWND_TOP, br.x - scaled(18, dpi) - w, br.y - scaled(44, dpi) - h, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void next() {
	Tour* t = g_tour;
	if (t == nullptr) return;
	if (t->step + 1 >= (int)steps().size()) { endTour(); return; }
	t->step++;
	t->done = false;
	t->stepStart = nowSeconds();
	place();
	t->panel.redraw();
}

// Four times a second: is this step done? (Moves on a moment after.)
void check() {
	Tour* t = g_tour;
	if (t == nullptr || !windowAlive(t->window)) { endTour(); return; }
	const double now = nowSeconds();
	if (now - t->lastCheck < 0.25) return;
	t->lastCheck = now;
	place();
	const Step& s = steps()[t->step];
	if (t->done) {
		if (now - t->doneAt > 1.1) next();
		return;
	}
	// A gate still following the pointer isn't placed yet.
	if (!s.check || now - t->stepStart < 0.6 || t->window->isFloating()) return;
	CLTourStatus st = {};
	cl_tour_status(t->window->document(), t->window->currentPage(), &st);
	if (s.check(st)) {
		t->done = true;
		t->doneAt = now;
		t->panel.redraw();
	}
}

void paintTour(ID2D1RenderTarget* rt, float w, float h) {
	Tour* t = g_tour;
	if (t == nullptr) return;
	const Step& s = steps()[t->step];
	const int count = (int)steps().size();
	const bool last = t->step == count - 1;
	ground(rt, w, h, 0.15f, 0, 22);
	strokeRound(rt, D2D1::RectF(0.5f, 0.5f, w - 0.5f, h - 0.5f), 8, alpha(kNeon, 0.18f));
	float y = 18;
	icon(rt, 18, y, 18);
	text(rt, strf("GUIDED TOUR  ·  %d OF %d", t->step + 1, count), 44, y + 1, 10.5f, kBold, kNeon, 0, DWRITE_TEXT_ALIGNMENT_LEADING, 1.2f);
	const D2D1_RECT_F close = D2D1::RectF(w - 18 - 22, y - 2, w - 18, y + 20);
	fillCircle(rt, D2D1::Point2F((close.left + close.right) / 2, (close.top + close.bottom) / 2), 11,
	           D2D1::ColorF(1, 1, 1, t->panel.hot == Tour::kClose ? 0.16f : 0.08f));
	drawIcon(rt, Icon::Dismiss, close, 9, kSecondary);
	t->panel.addHit(close, Tour::kClose);
	y += 18 + 12;
	// How far along: eases to where it's going.
	const float target = (float)(t->step + (t->done ? 1 : 0)) / count;
	t->shownProgress += (target - t->shownProgress) * 0.18f;
	if (std::fabs(target - t->shownProgress) < 0.002f) t->shownProgress = target;
	const float bw = w - 36;
	fillRound(rt, D2D1::RectF(18, y, 18 + bw, y + 3), 1.5f, D2D1::ColorF(1, 1, 1, 0.1f));
	const float fw = std::max(4.0f, bw * t->shownProgress);
	fillRound(rt, D2D1::RectF(16, y - 2, 20 + fw, y + 5), 3.5f, alpha(kNeon, 0.2f));
	fillRound(rt, D2D1::RectF(18, y, 18 + fw, y + 3), 1.5f, kNeon);
	y += 3 + 12;
	text(rt, s.title, 18, y, 17, kBold, kPrimary, w - 36);
	y += 24 + 8;
	y += text(rt, s.body(), 18, y, 12.5f, kNormal, kSecondary, w - 36);
	const std::vector<std::string> keys = s.keys();
	if (!keys.empty()) {
		y += 12;
		float x = 18;
		for (const std::string& k : keys) {
			if (k == "click" || k == "drag") {
				const std::string label = k == "click" ? "Click" : "Drag";
				const float pw = textWidth(label, 11, kSemi) + 18;
				fillRound(rt, D2D1::RectF(x, y + 1, x + pw, y + 25), 12, D2D1::ColorF(1, 1, 1, 0.07f));
				text(rt, label, x, y + 5, 11, kSemi, kSecondary, pw, DWRITE_TEXT_ALIGNMENT_CENTER);
				x += pw + 5;
			} else {
				keycap(rt, x, y, k, false, 26);
				x += keycapWidth(k, 26) + 5;
			}
		}
		y += 26;
	}
	y += 14;
	// Done, or your turn; and Next / Skip / Finish.
	if (s.check) {
		const D2D1_POINT_2F c = D2D1::Point2F(27, y + 18);
		ID2D1SolidColorBrush* b = nullptr;
		if (SUCCEEDED(rt->CreateSolidColorBrush(t->done ? kNeon : kFaint, &b))) {
			if (t->done) {
				fillCircle(rt, c, 13, alpha(kNeon, 0.18f));
				rt->FillEllipse(D2D1::Ellipse(c, 9, 9), b);
				b->SetColor(kInk);
				rt->DrawLine(D2D1::Point2F(c.x - 4, c.y), D2D1::Point2F(c.x - 1, c.y + 3), b, 2);
				rt->DrawLine(D2D1::Point2F(c.x - 1, c.y + 3), D2D1::Point2F(c.x + 4.5f, c.y - 3.5f), b, 2);
			} else {
				for (int i = 0; i < 12; i++) {
					const double a = i * 3.14159265 / 6;
					fillCircle(rt, D2D1::Point2F(c.x + 8 * (float)std::cos(a), c.y + 8 * (float)std::sin(a)), 0.9f, kFaint);
				}
			}
			b->Release();
		}
		text(rt, t->done ? "Nice, that's it." : "Your turn. This moves on by itself.", 44, y + 10, 12, kNormal, t->done ? kPrimary : kFaint,
		     w - 44 - 110);
	}
	const D2D1_RECT_F nb = D2D1::RectF(w - 18 - 84, y, w - 18, y + 36);
	button(rt, nb, last ? "Finish" : (s.check && !t->done ? "Skip" : "Next"), last || !s.check || t->done, t->panel.hot == Tour::kNextStep);
	t->panel.addHit(nb, Tour::kNextStep);
	const int needed = cardHeight(t);
	if (needed != t->height) { t->height = needed; PostMessageW(t->panel.hwnd, WM_NULL, 0, 0); place(); }
}

void startTourFrom(CircuitWindow* from) {
	// On a circuit of its own: the one in front if it's empty and new,
	// else a new one.
	CircuitWindow* target = from;
	bool empty = target && target->filePath().empty();
	if (empty)
		for (int p = 0; p < cl_document_page_count(target->document()); p++) empty = empty && cl_document_gate_count(target->document(), p) == 0;
	if (!empty) target = newCircuitWindow();
	if (target == nullptr) return;
	startTour(target);
}

}  // namespace

void startTour(CircuitWindow* window) {
	if (window == nullptr) return;
	if (g_tour) endTour();
	g_tour = new Tour();
	Tour* t = g_tour;
	t->window = window;
	t->stepStart = nowSeconds();
	Panel& p = t->panel;
	p.paint = paintTour;
	p.click = [](int id) {
		if (g_tour == nullptr) return;
		if (id == Tour::kClose) endTour();
		else if (id == Tour::kNextStep) next();
	};
	p.animating = [] {
		check();
		if (g_tour == nullptr) return false;
		const float target = (float)(g_tour->step + (g_tour->done ? 1 : 0)) / steps().size();
		return std::fabs(target - g_tour->shownProgress) > 0.001f;
	};
	// Clicks, but never the keyboard: that stays with the circuit.
	p.message = [](UINT msg, WPARAM, LPARAM, LRESULT& r) {
		if (msg == WM_MOUSEACTIVATE) { r = MA_NOACTIVATE; return true; }
		if (msg == WM_CLOSE) { endTour(); r = 0; return true; }
		return false;
	};
	p.create(window->window(), 10, 10, 0, 0, WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
	t->height = cardHeight(t);
	place();
	ShowWindow(p.hwnd, SW_SHOWNOACTIVATE);
	SetForegroundWindow(window->window());
}

void startTourOn(CircuitWindow* window) { startTourFrom(window); }

HWND tourWindow() { return g_tour ? g_tour->panel.hwnd : nullptr; }

}  // namespace welcome

// ---- What's New ------------------------------------------------------------------

namespace whatsnew {

const char* const kVersion = "native-1";

namespace {

enum { kSkip = 1, kBack, kNext, kTry, kLine0 = 10, kTile0 = 20 };
const float kNW = 820, kNH = 600;

struct Point { wchar_t icon; const char* title; const char* line; };
struct Chapter {
	const char* eyebrow; const char* title; const char* line;
	Point points[3];
	const char* tryTitle;
	int tryCommand;
};

const Chapter kChapters[] = {
	{ "Your circuits", "Everything in one place", "Every circuit lives in Your Circuits and saves itself as you go. No files to lose.",
	  { { 0xE8F1, "Your Circuits (Ctrl+O)", "Open, rename, delete. A new circuit joins as soon as there's something on it." },
	    { 0xE8C8, "Files come in as copies", "Open a .cdl from anywhere and you work on a copy; Export gets one out." },
	    { 0xE81C, "Versions that mean something", "Ctrl+S keeps a version. Version History has every one, with a picture." } },
	  "Open Your Circuits", CMD_OPEN },
	{ "Start ahead", "Templates and your own parts", "Stop rebuilding the same thing every lab.",
	  { { 0xE8C8, "New from Template", "A Lab Page with your name on it, a 4-bit counter, a 7-segment starter, or your own." },
	    { 0xE7B8, "My Parts", "Select some gates, Edit ▸ Save as Part, name it. Drag it from the side panel or find it with A." },
	    { 0xE713, "Save your own templates", "Any circuit can be the start of the next one: File ▸ Save as Template." } },
	  "Browse Templates", CMD_NEW_TEMPLATE },
	{ "Check your work", "Truth tables that do the algebra", "Press T, and CedarLogic hands you the simplest answer too.",
	  { { 0xE80A, "Karnaugh maps and formulas", "The truth table has tabs: the table, a K-map for each light, and the simplest SOP and POS." },
	    { 0xE943, "Build from a Formula", "Type F = AB + C' (or Σm(1,3,5)) and get the gates, wired and labelled." },
	    { 0xE721, "Find (Ctrl+F)", "Labels, TO/FROM names and parts on every page, one Enter away." } },
	  "Build from a Formula", CMD_BUILD_FORMULA },
	{ "See it think", "Watch the signals", "The circuit shows you what it's doing.",
	  { { 0xE71B, "Point at a wire", "Every branch of it lights up, so you can follow it across the page." },
	    { 0xE9D9, "Timing diagrams", "Share the oscilloscope (Ctrl+G) as a picture, in colour or black and white." },
	    { 0xE768, "Simulation View", "Lit wires with the signal marching along them. Press Ctrl+R." } },
	  nullptr, 0 },
	{ "Made for Windows", "Faster, calmer, greener", "A new look, and a lot of care in the small things.",
	  { { 0xE790, "CedarLogic green", "The icon's colour is the app's colour now. Preferences has the others." },
	    { 0xE9E9, "Gate settings and memory", "Double-click a gate or a RAM for clean, quick editors. Enter is Done." },
	    { 0xED15, "Send Feedback", "The speech bubble in the toolbar sends a note and screenshots straight to the developer." } },
	  nullptr, 0 },
};
const int kChapterCount = 5;
const int kPages = kChapterCount + 2;

struct WhatsNew {
	Panel panel;
	Pager pager;
	CircuitWindow* window = nullptr;
	double opened = nowSeconds();
};
WhatsNew* g_new = nullptr;

void close(int command) {
	WhatsNew* w = g_new;
	if (w == nullptr) return;
	g_new = nullptr;
	CircuitWindow* window = windowAlive(w->window) ? w->window : nullptr;
	if (window) EnableWindow(window->window(), TRUE);
	w->panel.destroy();
	delete w;
	if (window == nullptr) return;
	SetForegroundWindow(window->window());
	if (command == CMD_TOUR) welcome::startTourOn(window);
	else if (command) PostMessageW(window->window(), WM_COMMAND, command, 0);
}

void go(int d) {
	WhatsNew* w = g_new;
	if (w == nullptr) return;
	if (w->pager.page + d > kPages - 1) { close(0); return; }
	w->pager.go(std::max(0, w->pager.page + d));
	w->panel.redraw();
}

// The art beside each chapter.
void art(ID2D1RenderTarget* rt, int chapter, float x, float y, double t) {
	switch (chapter) {
	case 0: {   // Your Circuits
		struct Row { const char* name; const char* meta; };
		const Row rows[] = { { "Lab 5: Traffic Light", "42 gates · Today at 2:14 PM" }, { "Full Adder", "18 gates · Yesterday" },
		                     { "BCD to 7 Segment", "96 gates · Sep 24" }, { "Counter", "12 gates · Sep 22" } };
		for (int i = 0; i < 4; i++) {
			const D2D1_RECT_F r = D2D1::RectF(x, y + i * 58, x + 290, y + i * 58 + 48);
			card(rt, r, i == 0, 12);
			fillRound(rt, D2D1::RectF(r.left + 10, r.top + 9, r.left + 40, r.top + 39), 8, alpha(kNeon, 0.12f));
			glyph(rt, 0xE964, r.left + 13, r.top + 12, 15);
			text(rt, rows[i].name, r.left + 52, r.top + 7, 12.5f, kBold, kPrimary);
			text(rt, rows[i].meta, r.left + 52, r.top + 26, 10.5f, kNormal, kFaint);
			if (i == 0) {
				fillRound(rt, D2D1::RectF(r.right - 52, r.top + 15, r.right - 12, r.top + 33), 9, alpha(kNeon, 0.18f));
				text(rt, "OPEN", r.right - 52, r.top + 17, 9, kBold, kNeon, 40, DWRITE_TEXT_ALIGNMENT_CENTER, 0.8f);
			}
		}
		break;
	}
	case 1: {   // templates and parts
		struct T { wchar_t icon; const char* name; };
		const T tiles[] = { { 0xE8A5, "Lab Page" }, { 0xE8EF, "Counter" }, { 0xE8F9, "7-Segment" } };
		for (int i = 0; i < 3; i++) {
			const D2D1_RECT_F r = D2D1::RectF(x + i * 98, y, x + i * 98 + 86, y + 92);
			card(rt, r, false, 12);
			drawIcon(rt, tiles[i].icon, D2D1::RectF(r.left, r.top + 18, r.right, r.top + 52), 22, kNeon);
			text(rt, tiles[i].name, r.left, r.top + 60, 10.5f, kSemi, kSecondary, 86, DWRITE_TEXT_ALIGNMENT_CENTER);
		}
		const D2D1_RECT_F mine = D2D1::RectF(x, y + 104, x + 290, y + 160);
		card(rt, mine, true, 12);
		glyph(rt, 0xE7B8, mine.left + 14, mine.top + 15, 18);
		text(rt, "My Parts", mine.left + 52, mine.top + 11, 12, kBold, kPrimary);
		text(rt, "Full Adder · 2-to-4 Decoder · Debouncer", mine.left + 52, mine.top + 30, 10, kNormal, kFaint);
		break;
	}
	case 2: {   // a K-map with its groups, and the answer
		const int ones[] = { 1, 3, 5, 7, 13, 15 };
		const int gray[] = { 0, 1, 3, 2 };
		for (int r = 0; r < 4; r++)
			for (int c = 0; c < 4; c++) {
				const int m = gray[r] * 4 + gray[c];
				const bool one = std::find(std::begin(ones), std::end(ones), m) != std::end(ones);
				const D2D1_RECT_F cell = D2D1::RectF(x + 26 + c * 52, y + r * 44, x + 26 + c * 52 + 48, y + r * 44 + 40);
				fillRound(rt, cell, 6, D2D1::ColorF(1, 1, 1, 0.05f));
				text(rt, one ? "1" : "0", cell.left, cell.top + 9, 15, kBold, one ? kNeon : kFaint, 48, DWRITE_TEXT_ALIGNMENT_CENTER);
			}
		ID2D1SolidColorBrush* b = nullptr;
		if (SUCCEEDED(rt->CreateSolidColorBrush(kNeon, &b))) {
			fillRound(rt, D2D1::RectF(x + 74, y - 4, x + 182, y + 86), 12, alpha(kNeon, 0.06f));
			rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x + 76, y - 2, x + 180, y + 84), 10, 10), b, 2);
			b->SetColor(D2D1::ColorF(0.45f, 0.8f, 1));
			rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x + 76, y + 42, x + 180, y + 128), 10, 10), b, 2);
			b->Release();
		}
		text(rt, "F = A'D + BD", x, y + 186, 17, kBold, kPrimary, 260, DWRITE_TEXT_ALIGNMENT_CENTER);
		break;
	}
	case 3: {   // the live circuit over a timing diagram
		hero(rt, D2D1::RectF(x - 5, y, x + 295, y + 120), t);
		const D2D1_RECT_F r = D2D1::RectF(x - 5, y + 130, x + 295, y + 230);
		card(rt, r, false, 12);
		const int rows[3][10] = { { 0, 0, 1, 1, 0, 0, 1, 1, 0, 0 }, { 0, 1, 0, 1, 0, 1, 0, 1, 0, 1 }, { 0, 0, 0, 1, 0, 0, 0, 1, 0, 0 } };
		ID2D1SolidColorBrush* b = nullptr;
		if (SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.55f), &b))) {
			const float step = (r.right - r.left - 20) / 10;
			for (int i = 0; i < 3; i++) {
				b->SetColor(i == 2 ? kNeon : D2D1::ColorF(1, 1, 1, 0.55f));
				const float top = r.top + 14 + i * 30, hgt = 16;
				float lastY = -1;
				for (int j = 0; j < 10; j++) {
					const float x0 = r.left + 10 + j * step, yy = rows[i][j] ? top : top + hgt;
					if (lastY >= 0 && lastY != yy) rt->DrawLine(D2D1::Point2F(x0, lastY), D2D1::Point2F(x0, yy), b, 1.6f);
					rt->DrawLine(D2D1::Point2F(x0, yy), D2D1::Point2F(x0 + step, yy), b, 1.6f);
					lastY = yy;
				}
			}
			b->Release();
		}
		break;
	}
	default: {   // the colours, and a memory editor
		const int order[] = { 6, 0, 1, 2, 3, 4, 5 };
		float cx = x + 20;
		for (int i = 0; i < 7; i++) {
			double r, g, b;
			cl_accent_color(order[i], true, &r, &g, &b);
			const float rad = i == 0 ? 17.0f : 11.0f;
			if (i == 0) fillCircle(rt, D2D1::Point2F(cx, y + 17), 26, alpha(kNeon, 0.18f));
			fillCircle(rt, D2D1::Point2F(cx, y + 17), rad, D2D1::ColorF((float)r, (float)g, (float)b));
			cx += rad + 12 + (i == 0 ? 11 : 11);
		}
		const D2D1_RECT_F r = D2D1::RectF(x, y + 50, x + 290, y + 140);
		card(rt, r, false, 14);
		fillRound(rt, D2D1::RectF(r.left + 14, r.top + 14, r.left + 50, r.top + 44), 8, D2D1::ColorF(1, 1, 1, 0.06f));
		glyph(rt, 0xE964, r.left + 20, r.top + 17, 14);
		text(rt, "8x8 RAM", r.left + 60, r.top + 19, 13, kBold, kPrimary);
		fillRound(rt, D2D1::RectF(r.right - 76, r.top + 16, r.right - 14, r.top + 42), 13, kNeon);
		text(rt, "Done ↵", r.right - 76, r.top + 20, 11, kSemi, kInk, 62, DWRITE_TEXT_ALIGNMENT_CENTER);
		const char* vals[] = { "00", "07", "0E", "15", "1C", "23" };
		for (int i = 0; i < 6; i++) {
			const D2D1_RECT_F v = D2D1::RectF(r.left + 14 + i * 38, r.top + 54, r.left + 14 + i * 38 + 34, r.top + 76);
			fillRound(rt, v, 5, i == 2 ? alpha(kNeon, 0.35f) : D2D1::ColorF(1, 1, 1, 0.05f));
			text(rt, vals[i], v.left, v.top + 3, 11, kNormal, kPrimary, 34, DWRITE_TEXT_ALIGNMENT_CENTER);
		}
		break;
	}
	}
}

void pageIntro(ID2D1RenderTarget* rt, WhatsNew* wn, double t) {
	const float x = 56, tw = 380;
	text(rt, "WHAT'S NEW", x, 92, 11, kBold, kNeon, 0, DWRITE_TEXT_ALIGNMENT_LEADING, 1.8f);
	float y = 112 + text(rt, "CedarLogic for Windows", x, 112, 34, kBold, D2D1::ColorF(0.88f, 0.9f, 0.89f), tw + 40) + 14;
	y += text(rt, "A native Windows app now, with a new look and a lot more inside. Here's everything that's new since the old one, a minute's read.",
	          x, y, 14, kNormal, kSecondary, tw) + 20;
	for (int i = 0; i < kChapterCount; i++) {
		const D2D1_RECT_F r = D2D1::RectF(x - 8, y - 4, x + tw, y + 24);
		if (wn->panel.hot == kLine0 + i) fillRound(rt, r, 8, D2D1::ColorF(1, 1, 1, 0.06f));
		glyph(rt, kChapters[i].points[0].icon, x, y + 2, 12);
		text(rt, kChapters[i].title, x + 28, y + 1, 13, kSemi, kPrimary);
		text(rt, kChapters[i].eyebrow, x + 28 + textWidth(kChapters[i].title, 13, kSemi) + 10, y + 3, 11, kNormal, kFaint);
		wn->panel.addHit(r, kLine0 + i);
		y += 24;
	}
	// The icon, large, glowing, gently floating.
	const float cx = 640, cy = 210 + 6 * (float)std::sin(t * 1.4);
	const float glowR = 170 * (float)(1 + 0.04 * std::sin(t * 1.4));
	D2D1_GRADIENT_STOP gs[3] = { { 0, alpha(kNeon, 0.28f) }, { 10.0f / 170, alpha(kNeon, 0.28f) }, { 1, alpha(kNeon, 0) } };
	ID2D1GradientStopCollection* stops = nullptr;
	ID2D1RadialGradientBrush* halo = nullptr;
	if (SUCCEEDED(rt->CreateGradientStopCollection(gs, 3, &stops)) &&
	    SUCCEEDED(rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(cx, cy), D2D1::Point2F(0, 0), glowR, glowR), stops,
	                                            &halo)))
		rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), glowR, glowR), halo);
	if (halo) halo->Release();
	if (stops) stops->Release();
	icon(rt, cx - 105, cy - 105, 210, 0.6f);
}

void pageChapter(ID2D1RenderTarget* rt, WhatsNew* wn, int i, double t) {
	const Chapter& c = kChapters[i];
	const float x = 48, cw = 420;
	float y = heading(rt, x, 34, cw, c.eyebrow, c.title, c.line) + 18;
	for (const Point& p : c.points) y += pointCard(rt, x, y, cw, p.icon, p.title, p.line) + 10;
	if (c.tryTitle) {
		const float bw = textWidth(c.tryTitle, 13, kSemi) + 40;
		const D2D1_RECT_F r = D2D1::RectF(x, y + 6, x + bw, y + 42);
		button(rt, r, c.tryTitle, false, wn->panel.hot == kTry);
		wn->panel.addHit(r, kTry);
	}
	art(rt, i, 500, 84, t);
}

void pageFinale(ID2D1RenderTarget* rt, WhatsNew* wn) {
	float y = heading(rt, 56, 34, kNW - 112, "That's the tour", "Go build something", "Everything here is in Help too, whenever you want it.") + 22;
	struct Tile { const char* title; const char* line; wchar_t icon; };
	const Tile tiles[] = {
		{ "Take the guided tour", "Two switches, a gate and a light, on a circuit of its own.", 0xE805 },
		{ "Start from a template", "The Lab Page has your name on it already.", 0xE8C8 },
		{ "Open CedarLogic Help", "Every feature, with its keys.", 0xE897 },
	};
	for (int i = 0; i < 3; i++) {
		const D2D1_RECT_F r = D2D1::RectF(56, y, kNW - 56, y + 66);
		card(rt, r, wn->panel.hot == kTile0 + i, 13);
		glyph(rt, tiles[i].icon, r.left + 20, r.top + 21, 15);
		text(rt, tiles[i].title, r.left + 62, r.top + 14, 13.5f, kBold, kPrimary);
		text(rt, tiles[i].line, r.left + 62, r.top + 36, 11.5f, kNormal, kSecondary);
		drawIcon(rt, 0xE72A, D2D1::RectF(r.right - 44, r.top, r.right - 14, r.bottom), 12, kFaint);
		wn->panel.addHit(r, kTile0 + i);
		y += 78;
	}
}

void paint(ID2D1RenderTarget* rt, float w, float h) {
	WhatsNew* wn = g_new;
	if (wn == nullptr) return;
	const double t = nowSeconds() - wn->opened;
	ground(rt, w, h);
	const float footer = h - 74;
	wn->pager.draw(rt, wn->panel, D2D1::RectF(0, 0, w, footer - 1), [&](int pg) {
		if (pg == 0) pageIntro(rt, wn, t);
		else if (pg <= kChapterCount) pageChapter(rt, wn, pg - 1, t);
		else pageFinale(rt, wn);
	});
	const int page = wn->pager.page;
	const bool last = page == kPages - 1;
	if (!last) {
		const D2D1_RECT_F skip = D2D1::RectF(w - 26 - 44, 18, w - 26, 40);
		text(rt, "Skip", skip.left, skip.top, 12.5f, kMedium, wn->panel.hot == kSkip ? alpha(kPrimary, 0.7f) : kFaint, skip.right - skip.left,
		     DWRITE_TEXT_ALIGNMENT_TRAILING);
		wn->panel.addHit(skip, kSkip);
	}
	fillRect(rt, D2D1::RectF(0, footer - 1, w, footer), D2D1::ColorF(1, 1, 1, 0.07f));
	dots(rt, 44, footer + 37, kPages, page);
	const D2D1_RECT_F next = D2D1::RectF(w - 44 - (last ? 150 : 142), footer + 19, w - 44, footer + 55);
	button(rt, next, page == 0 ? "Show Me" : last ? "Start Building" : "Next", true, wn->panel.hot == kNext);
	wn->panel.addHit(next, kNext);
	if (page > 0) {
		const D2D1_RECT_F back = D2D1::RectF(next.left - 8 - 96, footer + 19, next.left - 8, footer + 55);
		button(rt, back, "Back", false, wn->panel.hot == kBack);
		wn->panel.addHit(back, kBack);
	}
}

}  // namespace

void show(CircuitWindow* window, int page) {
	if (window == nullptr) return;
	if (g_new) { SetForegroundWindow(g_new->panel.hwnd); return; }
	prefs().seenWhatsNew = kVersion;
	prefs().save();
	g_new = new WhatsNew();
	g_new->window = window;
	g_new->pager.page = std::max(0, std::min(kPages - 1, page));
	Panel& p = g_new->panel;
	p.paint = paint;
	p.animating = [] { return g_new && (g_new->pager.sliding() || g_new->pager.page == 0 || g_new->pager.page == 4); };
	p.click = [](int id) {
		WhatsNew* wn = g_new;
		if (wn == nullptr) return;
		if (id == kSkip) close(0);
		else if (id == kBack) go(-1);
		else if (id == kNext) go(1);
		else if (id == kTry && wn->pager.page >= 1 && wn->pager.page <= kChapterCount) close(kChapters[wn->pager.page - 1].tryCommand);
		else if (id >= kLine0 && id < kLine0 + kChapterCount) go(id - kLine0 + 1 - wn->pager.page);
		else if (id == kTile0) close(CMD_TOUR);
		else if (id == kTile0 + 1) close(CMD_NEW_TEMPLATE);
		else if (id == kTile0 + 2) close(CMD_HELP);
	};
	// Alt+F4 is Skip: the circuit window comes back.
	p.message = [](UINT msg, WPARAM, LPARAM, LRESULT& r) {
		if (msg != WM_CLOSE || g_new == nullptr) return false;
		close(0);
		r = 0;
		return true;
	};
	p.key = [](UINT vk) {
		if (vk == VK_ESCAPE) close(0);
		else if (vk == VK_RETURN || vk == VK_RIGHT) go(1);
		else if (vk == VK_LEFT) go(-1);
		else return false;
		return true;
	};
	const RECT r = centeredOn(window->window(), (int)kNW, (int)kNH);
	p.create(window->window(), r.right - r.left, r.bottom - r.top, r.left, r.top);
	EnableWindow(window->window(), FALSE);
	ShowWindow(p.hwnd, SW_SHOWNORMAL);
	SetForegroundWindow(p.hwnd);
	SetFocus(p.hwnd);
}

bool offer(CircuitWindow* window) {
	if (!prefs().hasSeenWelcome || prefs().seenWhatsNew == kVersion || window == nullptr) return false;
	show(window);
	return true;
}

}  // namespace whatsnew
