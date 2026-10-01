// The launch screen, the welcome and the guided tour (see Welcome.h). All in
// the brand's look -- the icon's near-black green, its neon, brushed silver --
// drawn with Direct2D, as the Mac's Splash.swift, Welcome.swift and
// BrandUI.swift draw them.

#include "Welcome.h"
#include "Chrome.h"
#include "Commands.h"
#include "Window.h"

#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace {

// ---- The brand -------------------------------------------------------------------

const D2D1_COLOR_F kInk = D2D1::ColorF(0.035f, 0.063f, 0.047f), kInkDeep = D2D1::ColorF(0.012f, 0.024f, 0.018f);
const D2D1_COLOR_F kNeon = D2D1::ColorF(0.22f, 1.0f, 0.42f), kNeonDeep = D2D1::ColorF(0.05f, 0.78f, 0.26f);
const D2D1_COLOR_F kSilver = D2D1::ColorF(0.93f, 0.95f, 0.94f), kDim = D2D1::ColorF(0.62f, 0.74f, 0.66f);

double easeOut(double t) { t = std::min(1.0, std::max(0.0, t)); return 1 - std::pow(1 - t, 3); }

void roundCorners(HWND hwnd) {
	const DWORD round = 2;   // DWMWCP_ROUND (Windows 11; ignored before)
	DwmSetWindowAttribute(hwnd, 33, &round, sizeof round);
}

// The ground: the icon's dark green, falling to deeper green, its grid
// showing faintly, a neon bloom near the top.
void drawGround(ID2D1RenderTarget* rt, float w, float h, float bloomY) {
	ID2D1GradientStopCollection* stops = nullptr;
	D2D1_GRADIENT_STOP s[2] = { { 0, kInk }, { 1, kInkDeep } };
	if (SUCCEEDED(rt->CreateGradientStopCollection(s, 2, &stops))) {
		ID2D1LinearGradientBrush* b = nullptr;
		if (SUCCEEDED(rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, h)), stops, &b))) {
			rt->FillRectangle(D2D1::RectF(0, 0, w, h), b);
			b->Release();
		}
		stops->Release();
	}
	// The grid, strongest towards the middle.
	ID2D1SolidColorBrush* line = nullptr;
	if (SUCCEEDED(rt->CreateSolidColorBrush(kNeon, &line))) {
		const float step = 28, cx = w / 2, cy = h / 2;
		for (float x = std::fmod(w, step) / 2; x < w; x += step) {
			const float d = std::fabs(x - cx) / (w / 2);
			line->SetOpacity(0.06f * std::max(0.0f, 1 - d * d));
			rt->DrawLine(D2D1::Point2F(x, 0), D2D1::Point2F(x, h), line, 0.5f);
		}
		for (float y = std::fmod(h, step) / 2; y < h; y += step) {
			const float d = std::fabs(y - cy) / (h / 2);
			line->SetOpacity(0.06f * std::max(0.0f, 1 - d * d));
			rt->DrawLine(D2D1::Point2F(0, y), D2D1::Point2F(w, y), line, 0.5f);
		}
		line->Release();
	}
	// The bloom.
	D2D1_GRADIENT_STOP g[2] = { { 0, withAlpha(kNeon, 0.16f) }, { 1, withAlpha(kNeon, 0) } };
	ID2D1GradientStopCollection* gs = nullptr;
	if (SUCCEEDED(rt->CreateGradientStopCollection(g, 2, &gs))) {
		ID2D1RadialGradientBrush* rb = nullptr;
		if (SUCCEEDED(rt->CreateRadialGradientBrush(
				D2D1::RadialGradientBrushProperties(D2D1::Point2F(w / 2, bloomY), D2D1::Point2F(0, 0), 360, 360), gs, &rb))) {
			rt->FillRectangle(D2D1::RectF(0, 0, w, h), rb);
			rb->Release();
		}
		gs->Release();
	}
}

// A soft glow: the shape stroked a few times, wider and fainter each time,
// under a crisp final pass.
void glow(ID2D1RenderTarget* rt, ID2D1Geometry* g, float width, D2D1_COLOR_F c, float alpha) {
	ID2D1SolidColorBrush* b = nullptr;
	if (FAILED(rt->CreateSolidColorBrush(c, &b))) return;
	ID2D1Factory* f = nullptr;
	rt->GetFactory(&f);
	ID2D1StrokeStyle* round = nullptr;
	if (f) f->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
	                                                         D2D1_LINE_JOIN_ROUND), nullptr, 0, &round);
	for (int pass = 3; pass >= 1; pass--) {
		b->SetOpacity(alpha * 0.10f);
		rt->DrawGeometry(g, b, width + pass * 3.5f, round);
	}
	b->SetOpacity(alpha);
	rt->DrawGeometry(g, b, width, round);
	if (round) round->Release();
	if (f) f->Release();
	b->Release();
}

// The brand mark: a switch feeding an AND gate feeding a lit light.
void drawMark(ID2D1RenderTarget* rt, float cx, float cy, float s, float shine) {
	ID2D1Factory* f = nullptr;
	rt->GetFactory(&f);
	if (f == nullptr) return;
	// A halo behind it all.
	D2D1_GRADIENT_STOP hs[2] = { { 0, withAlpha(kNeon, 0.20f * shine) }, { 1, withAlpha(kNeon, 0) } };
	ID2D1GradientStopCollection* stops = nullptr;
	if (SUCCEEDED(rt->CreateGradientStopCollection(hs, 2, &stops))) {
		ID2D1RadialGradientBrush* rb = nullptr;
		if (SUCCEEDED(rt->CreateRadialGradientBrush(
				D2D1::RadialGradientBrushProperties(D2D1::Point2F(cx, cy), D2D1::Point2F(0, 0), 130 * s, 130 * s), stops, &rb))) {
			rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 130 * s, 130 * s), rb);
			rb->Release();
		}
		stops->Release();
	}
	const float x0 = cx - 95 * s, x1 = cx - 30 * s, x3 = cx + 95 * s, r = 22 * s;
	ID2D1PathGeometry* g = nullptr;
	if (FAILED(f->CreatePathGeometry(&g))) { f->Release(); return; }
	ID2D1GeometrySink* k = nullptr;
	g->Open(&k);
	// The switch.
	k->BeginFigure(D2D1::Point2F(x0 - 16 * s, cy - 16 * s), D2D1_FIGURE_BEGIN_HOLLOW);
	k->AddLine(D2D1::Point2F(x0 + 16 * s, cy - 16 * s));
	k->AddLine(D2D1::Point2F(x0 + 16 * s, cy + 16 * s));
	k->AddLine(D2D1::Point2F(x0 - 16 * s, cy + 16 * s));
	k->EndFigure(D2D1_FIGURE_END_CLOSED);
	// The wire in.
	k->BeginFigure(D2D1::Point2F(x0 + 16 * s, cy), D2D1_FIGURE_BEGIN_HOLLOW);
	k->AddLine(D2D1::Point2F(x1 - 22 * s, cy));
	k->EndFigure(D2D1_FIGURE_END_OPEN);
	// The AND gate.
	k->BeginFigure(D2D1::Point2F(x1 - 22 * s, cy - 24 * s), D2D1_FIGURE_BEGIN_HOLLOW);
	k->AddLine(D2D1::Point2F(x1, cy - 24 * s));
	k->AddBezier(D2D1::BezierSegment(D2D1::Point2F(x1 + 38 * s, cy - 24 * s), D2D1::Point2F(x1 + 38 * s, cy + 24 * s),
	                                 D2D1::Point2F(x1, cy + 24 * s)));
	k->AddLine(D2D1::Point2F(x1 - 22 * s, cy + 24 * s));
	k->EndFigure(D2D1_FIGURE_END_CLOSED);
	// The wire out.
	k->BeginFigure(D2D1::Point2F(x1 + 29 * s, cy), D2D1_FIGURE_BEGIN_HOLLOW);
	k->AddLine(D2D1::Point2F(x3 - 2 * r - 2 * s, cy));
	k->EndFigure(D2D1_FIGURE_END_OPEN);
	k->Close();
	k->Release();
	glow(rt, g, 2.2f * s, kNeon, 0.9f);
	g->Release();
	// The light, lit.
	fillCircle(rt, D2D1::Point2F(x3 - r, cy), r, withAlpha(kNeon, 0.30f * shine));
	ID2D1EllipseGeometry* bulb = nullptr;
	if (SUCCEEDED(f->CreateEllipseGeometry(D2D1::Ellipse(D2D1::Point2F(x3 - r, cy), r, r), &bulb))) {
		glow(rt, bulb, 2.2f * s, kNeon, 0.95f);
		bulb->Release();
	}
	f->Release();
}

// Text that wraps within its box (drawText keeps to one line).
void drawWrapped(ID2D1RenderTarget* rt, const std::string& text, const D2D1_RECT_F& box, float size, D2D1_COLOR_F color,
                 bool bold = false, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING) {
	IDWriteFactory* dw = dwFactory();
	if (dw == nullptr || text.empty()) return;
	IDWriteTextFormat* f = nullptr;
	if (FAILED(dw->CreateTextFormat(L"Segoe UI", nullptr, bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
	                                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"", &f)))
		return;
	f->SetTextAlignment(align);
	f->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
	ID2D1SolidColorBrush* b = nullptr;
	if (SUCCEEDED(rt->CreateSolidColorBrush(color, &b))) {
		const std::wstring w = W(text);
		rt->DrawText(w.c_str(), (UINT32)w.size(), f, box, b);
		b->Release();
	}
	f->Release();
}

// A pill button: neon (primary) with dark ink on it, or glass.
void pill(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, const std::string& label, bool primary, bool hot) {
	const float rad = (r.bottom - r.top) / 2;
	if (primary) {
		fillRound(rt, D2D1::RectF(r.left, r.top + 2, r.right, r.bottom + 2), rad, withAlpha(kNeon, hot ? 0.30f : 0.18f));
		fillRound(rt, r, rad, hot ? D2D1::ColorF(0.40f, 1.0f, 0.56f) : kNeon);
	} else {
		fillRound(rt, r, rad, D2D1::ColorF(1, 1, 1, hot ? 0.10f : 0.06f));
		strokeRound(rt, r, rad, D2D1::ColorF(1, 1, 1, 0.12f));
	}
	drawText(rt, label, r, 13, primary ? kInk : kSilver, TextAlign::Center, true);
}

// A glass card; `lit` gives it the neon edge.
void card(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, bool lit) {
	fillRound(rt, r, 14, lit ? withAlpha(kNeon, 0.09f) : D2D1::ColorF(1, 1, 1, 0.045f));
	strokeRound(rt, r, 14, lit ? withAlpha(kNeon, 0.6f) : D2D1::ColorF(1, 1, 1, 0.09f), lit ? 1.3f : 1.0f);
}

// The eyebrow, title and line over each page.
float heading(ID2D1RenderTarget* rt, float x, float y, float w, const char* eyebrow, const char* title, const char* line) {
	std::string e = eyebrow;
	for (char& c : e) c = (char)toupper((unsigned char)c);
	drawText(rt, e, D2D1::RectF(x, y, x + w, y + 16), 11, kNeon, TextAlign::Leading, true);
	drawWrapped(rt, title, D2D1::RectF(x, y + 22, x + w, y + 62), 28, kSilver, true);
	drawWrapped(rt, line, D2D1::RectF(x, y + 66, x + w, y + 110), 13.5f, kDim);
	return y + 110;
}

// A plain popup window whose drawing and clicks a lambda handles.
struct Panel {
	HWND hwnd = nullptr;
	WindowSurface surface;
	std::function<void(ID2D1RenderTarget*, float, float)> paint;
	std::function<void(float, float)> click;
	std::function<void(float, float)> hover;
	std::function<void(UINT)> key;
	std::function<void()> timer;

	static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
		Panel* p = reinterpret_cast<Panel*>(GetWindowLongPtrW(h, GWLP_USERDATA));
		if (p == nullptr) return DefWindowProcW(h, msg, wp, lp);
		const double s = dpiOf(h) / 96.0;
		const float x = (float)(GET_X_LPARAM(lp) / s), y = (float)(GET_Y_LPARAM(lp) / s);
		LRESULT result = 0;
		bool handled = true;
		guarded("a window", [&] {
			switch (msg) {
			case WM_PAINT: {
				PAINTSTRUCT ps;
				BeginPaint(h, &ps);
				if (ID2D1HwndRenderTarget* rt = p->surface.begin(h)) {
					RECT rc;
					GetClientRect(h, &rc);
					if (p->paint) p->paint(rt, (float)(rc.right / s), (float)(rc.bottom / s));
					p->surface.end();
				}
				EndPaint(h, &ps);
				break;
			}
			case WM_ERASEBKGND: result = 1; break;
			case WM_LBUTTONUP: if (p->click) p->click(x, y); break;
			case WM_MOUSEMOVE: if (p->hover) p->hover(x, y); break;
			case WM_KEYDOWN: if (p->key) p->key((UINT)wp); break;
			case WM_TIMER: if (p->timer) p->timer(); break;
			case WM_SETCURSOR: SetCursor(LoadCursor(nullptr, IDC_ARROW)); result = TRUE; break;
			case WM_MOUSEACTIVATE: result = MA_ACTIVATE; break;
			default: handled = false; break;
			}
		});
		return handled ? result : DefWindowProcW(h, msg, wp, lp);
	}

	void create(HWND owner, DWORD style, DWORD ex, int w, int h, int x, int y) {
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
		hwnd = CreateWindowExW(ex, L"CedarLogicBrandPanel", L"CedarLogic", style, x, y, w, h, owner, nullptr, appInstance(), nullptr);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)this);
		roundCorners(hwnd);
	}
	void destroy() {
		if (hwnd) {
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
			DestroyWindow(hwnd);
			hwnd = nullptr;
		}
	}
	void redraw() { if (hwnd) InvalidateRect(hwnd, nullptr, FALSE); }
};

// Centred on a window (or the screen it's on), w x h points.
RECT centeredOn(HWND anchor, int w, int h) {
	RECT a;
	if (anchor == nullptr || !GetWindowRect(anchor, &a)) {
		POINT p = { 0, 0 };
		HMONITOR m = MonitorFromPoint(p, MONITOR_DEFAULTTOPRIMARY);
		MONITORINFO mi = { sizeof mi };
		GetMonitorInfoW(m, &mi);
		a = mi.rcWork;
	}
	UINT dpi = 96;
	if (anchor) dpi = dpiOf(anchor);
	else {
		HDC dc = GetDC(nullptr);
		dpi = (UINT)GetDeviceCaps(dc, LOGPIXELSX);
		ReleaseDC(nullptr, dc);
	}
	const int pw = scaled(w, dpi), ph = scaled(h, dpi);
	const int x = (a.left + a.right - pw) / 2, y = (a.top + a.bottom - ph) / 2;
	return RECT{ x, y, x + pw, y + ph };
}

}  // namespace

// ---- The launch screen ------------------------------------------------------------

namespace splash {

namespace {
const double kMinVisible = 1.1;   // seconds: long enough to see it animate
Panel* g_panel = nullptr;
double g_shownAt = 0;
std::string g_status = "Starting up…";
}  // namespace

void show() {
	if (g_panel) return;
	g_panel = new Panel();
	g_shownAt = nowSeconds();
	g_panel->paint = [](ID2D1RenderTarget* rt, float w, float h) {
		const double t = (nowSeconds() - g_shownAt) / kMinVisible;
		const float p = (float)easeOut(t);
		const float shine = (float)(0.85 + 0.15 * std::sin(t * 9.0));
		drawGround(rt, w, h, h * 0.3f);
		strokeRound(rt, D2D1::RectF(0, 0, w, h), 8, withAlpha(kNeon, 0.20f), 1.4f);
		drawMark(rt, w / 2, h * 0.36f, 0.62f, shine);
		// "CedarLogic" in silver, a faint green glow behind it.
		const D2D1_RECT_F title = D2D1::RectF(0, h * 0.53f, w, h * 0.66f);
		for (float d : { -2.0f, 2.0f })
			drawText(rt, "CedarLogic", D2D1::RectF(title.left, title.top + d, title.right, title.bottom + d), 30,
			         withAlpha(kNeon, 0.25f), TextAlign::Center, true);
		drawText(rt, "CedarLogic", title, 30, kSilver, TextAlign::Center, true);
		drawText(rt, g_status, D2D1::RectF(0, h * 0.68f, w, h * 0.76f), 12, withAlpha(kDim, 0.85f), TextAlign::Center);
		// The thin neon bar along the bottom, with a bright head.
		const float barX = 40, barW = w - 80, barY = h - 34, barH = 4;
		fillRound(rt, D2D1::RectF(barX, barY, barX + barW, barY + barH), barH / 2, D2D1::ColorF(1, 1, 1, 0.08f));
		if (p > 0.01f) {
			const float fw = std::max(barH, barW * p);
			fillRound(rt, D2D1::RectF(barX, barY, barX + fw, barY + barH), barH / 2, kNeonDeep);
			fillRound(rt, D2D1::RectF(barX + fw * 0.5f, barY, barX + fw, barY + barH), barH / 2, kNeon);
			fillCircle(rt, D2D1::Point2F(barX + fw, barY + barH / 2), 3.2f, withAlpha(kNeon, 0.9f * shine));
		}
	};
	g_panel->timer = [] { g_panel->redraw(); };
	const RECT r = centeredOn(nullptr, 460, 300);
	g_panel->create(nullptr, WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_TOPMOST, r.right - r.left, r.bottom - r.top, r.left, r.top);
	ShowWindow(g_panel->hwnd, SW_SHOWNORMAL);
	UpdateWindow(g_panel->hwnd);
	SetTimer(g_panel->hwnd, 1, 30, nullptr);
}

bool active() { return g_panel != nullptr; }

void setStatus(const char* status) {
	g_status = status;
	if (g_panel) {
		g_panel->redraw();
		UpdateWindow(g_panel->hwnd);
	}
}

void hideSoon(std::function<void()> then) {
	if (g_panel == nullptr) { if (then) then(); return; }
	static std::function<void()> pending;
	pending = std::move(then);
	const double wait = std::max(0.0, kMinVisible - (nowSeconds() - g_shownAt));
	SetTimer(nullptr, 0, (UINT)(wait * 1000) + 1, [](HWND, UINT, UINT_PTR id, DWORD) {
		KillTimer(nullptr, id);
		if (g_panel) {
			KillTimer(g_panel->hwnd, 1);
			g_panel->destroy();
			delete g_panel;
			g_panel = nullptr;
		}
		std::function<void()> f = std::move(pending);
		pending = nullptr;
		if (f) guarded("starting up", [&] { f(); });
	});
}

}  // namespace splash

// ---- The welcome -------------------------------------------------------------------

namespace welcome {

namespace {

struct Welcome {
	Panel panel;
	CircuitWindow* window = nullptr;
	int page = 0;
	int hot = -1;   // 0 skip, 1 back, 2 next, 3 tour
	D2D1_RECT_F skip{}, back{}, next{}, tour{};
};
Welcome* g_welcome = nullptr;

void finish(bool tourNow) {
	Welcome* w = g_welcome;
	if (w == nullptr) return;
	g_welcome = nullptr;
	prefs().hasSeenWelcome = true;
	prefs().seenWhatsNew = whatsnew::kVersion;   // new to it all: nothing to catch up on
	prefs().save();
	CircuitWindow* window = w->window;
	HWND owner = window && std::find(circuitWindows().begin(), circuitWindows().end(), window) != circuitWindows().end()
	             ? window->window() : nullptr;
	if (owner) EnableWindow(owner, TRUE);
	w->panel.destroy();
	delete w;
	if (owner) SetForegroundWindow(owner);
	if (tourNow && owner) startTour(window);
}

void paint(ID2D1RenderTarget* rt, float w, float h) {
	Welcome* wl = g_welcome;
	if (wl == nullptr) return;
	drawGround(rt, w, h, 70);
	strokeRound(rt, D2D1::RectF(0, 0, w, h), 8, withAlpha(kNeon, 0.18f), 1.2f);
	const float pad = 40, inner = w - 2 * pad;
	switch (wl->page) {
	case 0: {
		drawMark(rt, w / 2, 86, 0.7f, 1.0f);
		const float y = heading(rt, pad, 170, inner, "CedarLogic", "Build it. Watch it think.",
		                        "Design digital logic circuits, run them live, and see exactly what every wire is doing.");
		struct Point { const char* title; const char* line; };
		const Point points[] = {
			{ "It keeps itself", "Keeps a copy if it ever closes unexpectedly, and offers it back." },
			{ "It shows its work", "Live wires, a truth table on one key, and an oscilloscope." },
			{ "It stays out of the way", "Nearly everything has a key. You won't need the menus." },
		};
		const float cw = (inner - 24) / 3;
		for (int i = 0; i < 3; i++) {
			const D2D1_RECT_F r = D2D1::RectF(pad + i * (cw + 12), y + 6, pad + i * (cw + 12) + cw, y + 112);
			card(rt, r, i == 0);
			drawWrapped(rt, points[i].title, D2D1::RectF(r.left + 14, r.top + 12, r.right - 14, r.top + 32), 13, kSilver, true);
			drawWrapped(rt, points[i].line, D2D1::RectF(r.left + 14, r.top + 36, r.right - 14, r.bottom - 8), 12, kDim);
		}
		break;
	}
	case 1: {
		float y = heading(rt, pad, 40, inner, "Six keys", "Worth knowing by heart.",
		                  "Click the canvas first, then just press the key. No menu needed.");
		struct Key { const char* key; const char* what; };
		const Key keys[] = { { "A", "Add a gate by typing its name" }, { "R", "Rotate the selection" },
		                     { "S", "Straighten the selected wires" }, { "Shift+S", "Tidy up the whole layout" },
		                     { "T", "Truth table for this page" }, { "Space", "Tap: fit the page. Hold and drag: move around" } };
		y += 6;
		for (const Key& k : keys) {
			const D2D1_RECT_F chip = D2D1::RectF(pad, y, pad + 78, y + 30);
			card(rt, chip, false);
			drawText(rt, k.key, chip, 13, kNeon, TextAlign::Center, true);
			drawText(rt, k.what, D2D1::RectF(pad + 94, y, pad + inner, y + 30), 13.5f, kSilver);
			y += 40;
		}
		break;
	}
	default: {
		drawMark(rt, w / 2, 110, 0.85f, 1.0f);
		const float y = heading(rt, pad, 210, inner, "Ready", "Let's build something.",
		                        "A short guided tour shows you around, one part at a time. Or just start.");
		wl->tour = D2D1::RectF(pad, y + 10, pad + 230, y + 50);
		pill(rt, wl->tour, "Take the Guided Tour", true, wl->hot == 3);
		break;
	}
	}

	// Along the bottom: the page dots, Skip, Back, Next.
	const float by = h - 62;
	fillRect(rt, D2D1::RectF(0, by - 14, w, by - 13), D2D1::ColorF(1, 1, 1, 0.06f));
	for (int i = 0; i < 3; i++)
		fillCircle(rt, D2D1::Point2F(pad + 4 + i * 16, by + 18), 3.5f, i == wl->page ? kNeon : D2D1::ColorF(1, 1, 1, 0.22f));
	const bool last = wl->page == 2;
	wl->next = D2D1::RectF(w - pad - (last ? 150 : 130), by, w - pad, by + 36);
	wl->back = D2D1::RectF(wl->next.left - 100, by, wl->next.left - 12, by + 36);
	wl->skip = D2D1::RectF(wl->back.left - 80, by, wl->back.left - 12, by + 36);
	if (!last) drawText(rt, "Skip", wl->skip, 13, withAlpha(kDim, wl->hot == 0 ? 1.0f : 0.75f), TextAlign::Center);
	if (wl->page > 0) pill(rt, wl->back, "Back", false, wl->hot == 1);
	pill(rt, wl->next, wl->page == 0 ? "Get Started" : last ? "Start Building" : "Next", !last, wl->hot == 2);
}

int hitAt(float x, float y) {
	Welcome* w = g_welcome;
	if (w == nullptr) return -1;
	if (w->page < 2 && inRect(w->skip, x, y)) return 0;
	if (w->page > 0 && inRect(w->back, x, y)) return 1;
	if (inRect(w->next, x, y)) return 2;
	if (w->page == 2 && inRect(w->tour, x, y)) return 3;
	return -1;
}

void go(int delta) {
	Welcome* w = g_welcome;
	if (w == nullptr) return;
	if (w->page + delta > 2) { finish(false); return; }
	w->page = std::max(0, w->page + delta);
	w->panel.redraw();
}

}  // namespace

bool offer(CircuitWindow* window) {
	if (prefs().hasSeenWelcome || window == nullptr || g_welcome) return false;
	g_welcome = new Welcome();
	g_welcome->window = window;
	Panel& p = g_welcome->panel;
	p.paint = paint;
	p.hover = [](float x, float y) {
		const int h = hitAt(x, y);
		if (g_welcome && h != g_welcome->hot) { g_welcome->hot = h; g_welcome->panel.redraw(); }
	};
	p.click = [](float x, float y) {
		switch (hitAt(x, y)) {
		case 0: finish(false); break;
		case 1: go(-1); break;
		case 2: go(1); break;
		case 3: finish(true); break;
		default: break;
		}
	};
	p.key = [](UINT vk) {
		if (vk == VK_ESCAPE) finish(false);
		else if (vk == VK_RETURN || vk == VK_RIGHT) go(1);
		else if (vk == VK_LEFT) go(-1);
	};
	const RECT r = centeredOn(window->window(), 640, 520);
	p.create(window->window(), WS_POPUP, 0, r.right - r.left, r.bottom - r.top, r.left, r.top);
	EnableWindow(window->window(), FALSE);
	ShowWindow(p.hwnd, SW_SHOWNORMAL);
	SetForegroundWindow(p.hwnd);
	SetFocus(p.hwnd);
	return true;
}

// ---- The guided tour --------------------------------------------------------------
// A bubble beside each part in turn: the palette, the canvas, Run, the tabs.

namespace {

struct Step { int anchor; const char* title; const char* text; };
const Step kSteps[] = {
	{ 0, "The gate palette", "Click a gate here, then click on the canvas to place it. Or drag it there." },
	{ 1, "The canvas", "Click a switch to flip it. Drag from a pin to wire two parts together. Right-click for more." },
	{ 2, "Run and pause", "The simulation runs live as you build. Pause it here, or press Space in Simulation View." },
	{ 3, "Pages", "A circuit can have more than one page. Add another with the + after the tabs." },
};
const int kStepCount = (int)(sizeof kSteps / sizeof kSteps[0]);

struct Tour {
	Panel panel;
	CircuitWindow* window = nullptr;
	int step = 0;
	int hot = -1;   // 0 skip, 1 next
	D2D1_RECT_F skip{}, next{};
};
Tour* g_tour = nullptr;

bool windowAlive(CircuitWindow* w) {
	return std::find(circuitWindows().begin(), circuitWindows().end(), w) != circuitWindows().end();
}

void endTour() {
	if (g_tour == nullptr) return;
	Tour* t = g_tour;
	g_tour = nullptr;
	KillTimer(t->panel.hwnd, 1);
	t->panel.destroy();
	CircuitWindow* w = t->window;
	delete t;
	if (windowAlive(w)) SetForegroundWindow(w->window());
}

// Beside the anchor: to its right for the palette, inside the canvas near
// its top left, under the button for Run and the tabs.
void place() {
	Tour* t = g_tour;
	if (t == nullptr || !windowAlive(t->window)) { endTour(); return; }
	const RECT a = t->window->tourAnchor(kSteps[t->step].anchor);
	const UINT dpi = dpiOf(t->window->window());
	const int bw = scaled(320, dpi), bh = scaled(150, dpi), gap = scaled(12, dpi);
	int x, y;
	switch (kSteps[t->step].anchor) {
	case 0: x = a.right + gap; y = a.top + scaled(60, dpi); break;
	case 1: x = a.left + scaled(40, dpi); y = a.top + scaled(40, dpi); break;
	default: x = std::max<int>(a.left - bw / 2 + (a.right - a.left) / 2, scaled(8, dpi)); y = a.bottom + gap; break;
	}
	RECT win;
	GetWindowRect(t->window->window(), &win);
	x = std::min(x, (int)win.right - bw - scaled(8, dpi));
	SetWindowPos(t->panel.hwnd, HWND_TOP, x, y, bw, bh, SWP_NOACTIVATE);
}

void paintTour(ID2D1RenderTarget* rt, float w, float h) {
	Tour* t = g_tour;
	if (t == nullptr) return;
	rt->Clear(kInk);
	fillRound(rt, D2D1::RectF(0, 0, w, h), 10, kInk);
	strokeRound(rt, D2D1::RectF(0, 0, w, h), 10, withAlpha(kNeon, 0.6f), 1.3f);
	const Step& s = kSteps[t->step];
	drawText(rt, strf("STEP %d OF %d", t->step + 1, kStepCount), D2D1::RectF(18, 14, w - 18, 30), 10, kNeon, TextAlign::Leading, true);
	drawText(rt, s.title, D2D1::RectF(18, 32, w - 18, 56), 16, kSilver, TextAlign::Leading, true);
	drawWrapped(rt, s.text, D2D1::RectF(18, 58, w - 18, h - 46), 12.5f, kDim);
	const bool last = t->step == kStepCount - 1;
	t->next = D2D1::RectF(w - 18 - 84, h - 42, w - 18, h - 12);
	t->skip = D2D1::RectF(t->next.left - 80, h - 42, t->next.left - 8, h - 12);
	if (!last) drawText(rt, "Skip", t->skip, 12.5f, withAlpha(kDim, t->hot == 0 ? 1.0f : 0.75f), TextAlign::Center);
	pill(rt, t->next, last ? "Done" : "Next", true, t->hot == 1);
}

}  // namespace

void startTour(CircuitWindow* window) {
	if (window == nullptr) return;
	if (g_tour) endTour();
	g_tour = new Tour();
	g_tour->window = window;
	Panel& p = g_tour->panel;
	p.paint = paintTour;
	p.hover = [](float x, float y) {
		if (g_tour == nullptr) return;
		const int h = inRect(g_tour->next, x, y) ? 1 : (g_tour->step < kStepCount - 1 && inRect(g_tour->skip, x, y)) ? 0 : -1;
		if (h != g_tour->hot) { g_tour->hot = h; g_tour->panel.redraw(); }
	};
	p.click = [](float x, float y) {
		if (g_tour == nullptr) return;
		if (inRect(g_tour->next, x, y)) {
			if (++g_tour->step >= kStepCount) { endTour(); return; }
			g_tour->hot = -1;
			place();
			g_tour->panel.redraw();
		} else if (g_tour->step < kStepCount - 1 && inRect(g_tour->skip, x, y)) {
			endTour();
		}
	};
	p.key = [](UINT vk) { if (vk == VK_ESCAPE) endTour(); };
	// It follows the window if that moves.
	p.timer = [] { place(); };
	p.create(window->window(), WS_POPUP, WS_EX_TOOLWINDOW, 320, 150, 0, 0);
	place();
	ShowWindow(p.hwnd, SW_SHOWNA);
	SetTimer(p.hwnd, 1, 250, nullptr);
}

}  // namespace welcome

// ---- What's New ------------------------------------------------------------------

namespace whatsnew {

const char* const kVersion = "native-1";

namespace {

struct Point { wchar_t icon; const char* title; const char* line; };
struct Chapter {
	const char* eyebrow; const char* title; const char* line;
	Point points[3];
	const char* tryTitle;
	int tryCommand;
};

const Chapter kChapters[] = {
	{ "Your circuits", "Everything in one place",
	  "Every circuit lives in Your Circuits and saves itself as you go. No files to lose.",
	  { { 0xE8B7, "Your Circuits (Ctrl+O)", "Open, rename, delete. A new circuit joins as soon as there's something on it." },
	    { 0xE8C8, "Files come in as copies", "Open a .cdl from anywhere and you work on a copy; Export gets one out." },
	    { 0xE81C, "Versions that mean something", "Ctrl+S keeps a version. Version History has every one, with a picture." } },
	  "Open Your Circuits", CMD_OPEN },
	{ "Start ahead", "Templates and your own parts", "Stop rebuilding the same thing every lab.",
	  { { 0xE8A5, "New from Template", "A Lab Page with your name on it, a 4-bit counter, a 7-segment starter, or your own." },
	    { 0xE7B8, "My Parts", "Select some gates, Save as Part, name it. Drag it from the side panel or find it with A." },
	    { 0xE713, "Save your own templates", "Any circuit can be a template for the next one." } },
	  "Browse Templates", CMD_NEW_TEMPLATE },
	{ "Check your work", "Truth tables that do the algebra", "Press T, and CedarLogic hands you the simplest answer too.",
	  { { 0xE80A, "Karnaugh maps and formulas", "The truth table has tabs: the table, a K-map for each light, and the simplest SOP and POS." },
	    { 0xE943, "Build from a Formula", "Type F = AB + C' (or Σm(1,3,5)) and get the gates, wired and labelled." },
	    { 0xE721, "Find (Ctrl+F)", "Labels, TO/FROM names and parts on every page, one Enter away." } },
	  "Build from a Formula", CMD_BUILD_FORMULA },
	{ "See it think", "Watch the signals", "The circuit shows you what it's doing, and your report shows it too.",
	  { { 0xE9D9, "A new oscilloscope (Ctrl+G)", "A time cursor reads every signal at once. Copy it as a timing diagram for a lab report." },
	    { 0xEB9F, "Export as Image (Ctrl+E)", "With your name and whether the circuit works under it, in colour or black and white." },
	    { 0xE768, "Simulation View (Ctrl+R)", "Lit wires with the signal marching along them." } },
	  nullptr, 0 },
	{ "Made for Windows", "Native, and nothing else to install", "Rebuilt from the ground up on Windows' own parts.",
	  { { 0xE7C4, "Light on its feet", "Plain Windows and Direct2D on the CedarLogic engine. It opens fast and stays fast." },
	    { 0xE8AB, "Ctrl+Tab between tabs", "Tap it for the last tab; hold Ctrl for a picture of each." },
	    { 0xED15, "Send Feedback", "Help ▸ Send Feedback sends a note and screenshots straight to the developer." } },
	  nullptr, 0 },
};
const int kChapterCount = (int)(sizeof kChapters / sizeof kChapters[0]);
const int kPages = kChapterCount + 2;

struct WhatsNew {
	Panel panel;
	CircuitWindow* window = nullptr;
	int page = 0;
	int hot = -1;   // 0 skip, 1 back, 2 next, 3 try, 10+ an intro line, 20+ a finale tile
	D2D1_RECT_F skip{}, back{}, next{}, tryIt{};
	std::vector<D2D1_RECT_F> lines, tiles;
};
WhatsNew* g_new = nullptr;

void close(int command) {
	WhatsNew* w = g_new;
	if (w == nullptr) return;
	g_new = nullptr;
	CircuitWindow* window = w->window;
	HWND owner = window && std::find(circuitWindows().begin(), circuitWindows().end(), window) != circuitWindows().end()
	             ? window->window() : nullptr;
	if (owner) EnableWindow(owner, TRUE);
	w->panel.destroy();
	delete w;
	if (owner) {
		SetForegroundWindow(owner);
		if (command == CMD_TOUR) welcome::startTour(window);
		else if (command) PostMessageW(owner, WM_COMMAND, command, 0);
	}
}

void paint(ID2D1RenderTarget* rt, float w, float h) {
	WhatsNew* wn = g_new;
	if (wn == nullptr) return;
	drawGround(rt, w, h, 70);
	strokeRound(rt, D2D1::RectF(0, 0, w, h), 8, withAlpha(kNeon, 0.18f), 1.2f);
	const float pad = 48;
	wn->lines.clear();
	wn->tiles.clear();
	if (wn->page == 0) {
		const float tw = 400;
		drawText(rt, "WHAT'S NEW", D2D1::RectF(pad, 44, pad + tw, 60), 11, kNeon, TextAlign::Leading, true);
		drawWrapped(rt, "CedarLogic for Windows", D2D1::RectF(pad, 64, pad + tw, 110), 32, kSilver, true);
		drawWrapped(rt, "A native Windows app now, with a lot more inside. Here's everything that's new since the old one, a minute's read.",
		            D2D1::RectF(pad, 112, pad + tw, 170), 14, kDim);
		float y = 186;
		for (int i = 0; i < kChapterCount; i++) {
			const D2D1_RECT_F r = D2D1::RectF(pad - 8, y - 4, pad + tw, y + 24);
			wn->lines.push_back(r);
			if (wn->hot == 10 + i) fillRound(rt, r, 8, D2D1::ColorF(1, 1, 1, 0.06f));
			drawIcon(rt, kChapters[i].points[0].icon, D2D1::RectF(pad, y, pad + 18, y + 20), 12, kNeon);
			drawText(rt, kChapters[i].title, D2D1::RectF(pad + 28, y + 1, pad + tw, y + 20), 13, kSilver, TextAlign::Leading, true);
			const float tx = pad + 28 + textWidth(kChapters[i].title, 13, true) + 10;
			drawText(rt, kChapters[i].eyebrow, D2D1::RectF(tx, y + 3, pad + tw, y + 20), 11, withAlpha(kDim, 0.8f));
			y += 32;
		}
		drawMark(rt, w - 190, 230, 0.95f, 1.0f);
	} else if (wn->page <= kChapterCount) {
		const Chapter& c = kChapters[wn->page - 1];
		const float cw = 440;
		float y = heading(rt, pad, 36, cw, c.eyebrow, c.title, c.line);
		for (const Point& p : c.points) {
			const D2D1_RECT_F r = D2D1::RectF(pad, y, pad + cw, y + 62);
			card(rt, r, false);
			drawIcon(rt, p.icon, D2D1::RectF(r.left + 12, r.top + 12, r.left + 36, r.top + 34), 15, kNeon);
			drawText(rt, p.title, D2D1::RectF(r.left + 48, r.top + 10, r.right - 12, r.top + 28), 13, kSilver, TextAlign::Leading, true);
			drawWrapped(rt, p.line, D2D1::RectF(r.left + 48, r.top + 29, r.right - 12, r.bottom - 4), 11.5f, kDim);
			y += 72;
		}
		wn->tryIt = c.tryTitle ? D2D1::RectF(pad, y + 4, pad + textWidth(c.tryTitle, 13, true) + 44, y + 40) : D2D1::RectF(0, 0, 0, 0);
		if (c.tryTitle) pill(rt, wn->tryIt, c.tryTitle, false, wn->hot == 3);
		// Beside it, the theme's icon, large, in a glow.
		const float cx = (pad + cw + w) / 2, cy = 220;
		D2D1_GRADIENT_STOP g[2] = { { 0, withAlpha(kNeon, 0.22f) }, { 1, withAlpha(kNeon, 0) } };
		ID2D1GradientStopCollection* gs = nullptr;
		if (SUCCEEDED(rt->CreateGradientStopCollection(g, 2, &gs))) {
			ID2D1RadialGradientBrush* rb = nullptr;
			if (SUCCEEDED(rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(cx, cy), D2D1::Point2F(0, 0), 130, 130), gs, &rb))) {
				rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 130, 130), rb);
				rb->Release();
			}
			gs->Release();
		}
		card(rt, D2D1::RectF(cx - 70, cy - 70, cx + 70, cy + 70), true);
		drawIcon(rt, c.points[0].icon, D2D1::RectF(cx - 70, cy - 70, cx + 70, cy + 70), 64, kNeon);
	} else {
		float y = heading(rt, pad, 36, w - 2 * pad, "That's the tour", "Go build something", "Everything here is in Help too, whenever you want it.");
		struct Tile { const char* title; const char* line; wchar_t icon; };
		const Tile tiles[] = {
			{ "Take the guided tour", "Two switches, a gate and a light: around the app in a minute.", 0xE7C1 },
			{ "Start from a template", "The Lab Page has your name on it already.", 0xE8A5 },
			{ "Send feedback", "What's working, what's broken, what you'd love to see.", 0xED15 },
		};
		for (int i = 0; i < 3; i++) {
			const D2D1_RECT_F r = D2D1::RectF(pad, y, w - pad, y + 66);
			wn->tiles.push_back(r);
			card(rt, r, wn->hot == 20 + i);
			drawIcon(rt, tiles[i].icon, D2D1::RectF(r.left + 16, r.top, r.left + 50, r.bottom), 18, kNeon);
			drawText(rt, tiles[i].title, D2D1::RectF(r.left + 64, r.top + 14, r.right - 40, r.top + 34), 13.5f, kSilver, TextAlign::Leading, true);
			drawText(rt, tiles[i].line, D2D1::RectF(r.left + 64, r.top + 35, r.right - 40, r.bottom - 6), 11.5f, kDim);
			drawIcon(rt, 0xE72A, D2D1::RectF(r.right - 40, r.top, r.right - 12, r.bottom), 12, withAlpha(kDim, 0.8f));   // arrow
			y += 78;
		}
	}

	// Along the bottom: the page dots, Skip, Back, Next.
	const float by = h - 62;
	fillRect(rt, D2D1::RectF(0, by - 14, w, by - 13), D2D1::ColorF(1, 1, 1, 0.06f));
	for (int i = 0; i < kPages; i++) {
		const float x = pad + i * 16 + (i > wn->page ? 14 : 0);
		if (i == wn->page) fillRound(rt, D2D1::RectF(x, by + 14, x + 22, by + 22), 4, kNeon);
		else fillCircle(rt, D2D1::Point2F(x + 4, by + 18), 4, D2D1::ColorF(1, 1, 1, 0.18f));
	}
	const bool last = wn->page == kPages - 1;
	wn->next = D2D1::RectF(w - pad - (last ? 150 : 130), by, w - pad, by + 36);
	wn->back = D2D1::RectF(wn->next.left - 100, by, wn->next.left - 12, by + 36);
	wn->skip = D2D1::RectF(wn->back.left - 80, by, wn->back.left - 12, by + 36);
	if (!last) drawText(rt, "Skip", wn->skip, 13, withAlpha(kDim, wn->hot == 0 ? 1.0f : 0.75f), TextAlign::Center);
	if (wn->page > 0) pill(rt, wn->back, "Back", false, wn->hot == 1);
	pill(rt, wn->next, wn->page == 0 ? "Show Me" : last ? "Start Building" : "Next", true, wn->hot == 2);
}

int hitAt(float x, float y) {
	WhatsNew* w = g_new;
	if (w == nullptr) return -1;
	const bool last = w->page == kPages - 1;
	if (!last && inRect(w->skip, x, y)) return 0;
	if (w->page > 0 && inRect(w->back, x, y)) return 1;
	if (inRect(w->next, x, y)) return 2;
	if (w->page >= 1 && w->page <= kChapterCount && kChapters[w->page - 1].tryTitle && inRect(w->tryIt, x, y)) return 3;
	for (size_t i = 0; i < w->lines.size(); i++) if (inRect(w->lines[i], x, y)) return 10 + (int)i;
	for (size_t i = 0; i < w->tiles.size(); i++) if (inRect(w->tiles[i], x, y)) return 20 + (int)i;
	return -1;
}

void go(int delta) {
	WhatsNew* w = g_new;
	if (w == nullptr) return;
	if (w->page + delta > kPages - 1) { close(0); return; }
	w->page = std::max(0, w->page + delta);
	w->hot = -1;
	w->panel.redraw();
}

}  // namespace

void show(CircuitWindow* window, int page) {
	if (window == nullptr) return;
	if (g_new) { SetForegroundWindow(g_new->panel.hwnd); return; }
	prefs().seenWhatsNew = kVersion;
	prefs().save();
	g_new = new WhatsNew();
	g_new->window = window;
	g_new->page = std::max(0, std::min(kPages - 1, page));
	Panel& p = g_new->panel;
	p.paint = paint;
	p.hover = [](float x, float y) {
		const int h = hitAt(x, y);
		if (g_new && h != g_new->hot) { g_new->hot = h; g_new->panel.redraw(); }
	};
	p.click = [](float x, float y) {
		const int h = hitAt(x, y);
		if (h == 0) close(0);
		else if (h == 1) go(-1);
		else if (h == 2) go(1);
		else if (h == 3) close(kChapters[g_new->page - 1].tryCommand);
		else if (h >= 10 && h < 20) go(h - 10 + 1 - g_new->page);
		else if (h == 20) close(CMD_TOUR);
		else if (h == 21) close(CMD_NEW_TEMPLATE);
		else if (h == 22) close(CMD_FEEDBACK);
	};
	p.key = [](UINT vk) {
		if (vk == VK_ESCAPE) close(0);
		else if (vk == VK_RETURN || vk == VK_RIGHT) go(1);
		else if (vk == VK_LEFT) go(-1);
	};
	const RECT r = centeredOn(window->window(), 780, 560);
	p.create(window->window(), WS_POPUP, 0, r.right - r.left, r.bottom - r.top, r.left, r.top);
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
