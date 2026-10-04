// The circuit canvas (see Canvas.h).

#include "Canvas.h"
#include "Brand.h"
#include "Chrome.h"
#include "Window.h"

#include <algorithm>
#include <cmath>

namespace {

const wchar_t* kClass = L"CedarLogicCanvas";
const double kMinUpp = 0.004;   // very close
const double kMaxUpp = 1.0;     // very far
const double kZoomTime = 0.14;  // seconds, as the wx app's eased zoom

double clampUpp(double u) { return std::min(std::max(u, kMinUpp), kMaxUpp); }

bool down(int vk) { return (GetKeyState(vk) & 0x8000) != 0; }

int modifiersNow() {
	int m = 0;
	if (down(VK_SHIFT)) m |= CL_MOD_SHIFT;
	if (down(VK_MENU)) m |= CL_MOD_OPTION;
	return m;
}

// Shift and nothing else counts as a bare key (Shift+S is Tidy Up).
bool bareKey() { return !down(VK_CONTROL) && !down(VK_MENU) && !down(VK_LWIN) && !down(VK_RWIN); }

}  // namespace

void registerCanvasClass() {
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof wc;
	wc.style = CS_DBLCLKS;
	wc.lpfnWndProc = Canvas::proc;
	wc.hInstance = appInstance();
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.lpszClassName = kClass;
	RegisterClassExW(&wc);
}

Canvas::Canvas(CircuitWindow* window, HWND parent, uint64_t pageKey) : win(window), key(pageKey) {
	hwnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 10, 10, parent, nullptr, appInstance(), this);
}

Canvas::~Canvas() {
	if (hwnd) {
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
		DestroyWindow(hwnd);
	}
}

LRESULT CALLBACK Canvas::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
	if (msg == WM_NCCREATE) {
		Canvas* c = static_cast<Canvas*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
		c->hwnd = h;
		SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)c);
	}
	Canvas* c = reinterpret_cast<Canvas*>(GetWindowLongPtrW(h, GWLP_USERDATA));
	if (c == nullptr) return DefWindowProcW(h, msg, wp, lp);
	LRESULT r = 0;
	bool ok = false;
	guarded("the canvas", [&] { r = c->handle(msg, wp, lp); ok = true; });
	return ok ? r : DefWindowProcW(h, msg, wp, lp);
}

int Canvas::page() const { return cl_document_page_index(win->document(), key); }

void Canvas::redraw() {
	InvalidateRect(hwnd, nullptr, FALSE);
	win->redrawMiniMap();
}

void Canvas::show(bool visible) {
	ShowWindow(hwnd, visible ? SW_SHOW : SW_HIDE);
	if (!visible) dropBuffers();
}

double Canvas::scale() const { return dpiOf(hwnd) / 96.0; }

double Canvas::width() const {
	RECT rc;
	GetClientRect(hwnd, &rc);
	return (rc.right - rc.left) / scale();
}

double Canvas::height() const {
	RECT rc;
	GetClientRect(hwnd, &rc);
	return (rc.bottom - rc.top) / scale();
}

void Canvas::viewPoint(LPARAM lp, double& vx, double& vy) const {
	const double s = scale();
	vx = GET_X_LPARAM(lp) / s;
	vy = GET_Y_LPARAM(lp) / s;
}

void Canvas::screenToView(POINT screen, double& vx, double& vy) const {
	ScreenToClient(hwnd, &screen);
	const double s = scale();
	vx = screen.x / s;
	vy = screen.y / s;
}

void Canvas::worldPoint(double vx, double vy, double& wx, double& wy) const {
	wx = originX + vx * upp;
	wy = originY - vy * upp;
}

void Canvas::center(double& wx, double& wy) const { worldPoint(width() / 2, height() / 2, wx, wy); }

bool Canvas::pointerWorld(double& wx, double& wy) const {
	if (!IsWindowVisible(hwnd)) return false;
	POINT p;
	GetCursorPos(&p);
	if (WindowFromPoint(p) != hwnd) return false;
	double vx, vy;
	screenToView(p, vx, vy);
	if (vx < 0 || vy < 0 || vx >= width() || vy >= height()) return false;
	worldPoint(vx, vy, wx, wy);
	return true;
}

void Canvas::setCursor(LPCWSTR which) {
	cursorNow = which;
	POINT p;
	GetCursorPos(&p);
	if (WindowFromPoint(p) == hwnd) SetCursor(LoadCursor(nullptr, which ? which : IDC_ARROW));
}

// ---- Drawing -------------------------------------------------------------------

void Canvas::paint() {
	PAINTSTRUCT ps;
	BeginPaint(hwnd, &ps);
	if (ID2D1HwndRenderTarget* rt = surface.begin(hwnd)) {
		drawInto(rt, surface.scale());
		surface.end();
	}
	EndPaint(hwnd, &ps);
}

void Canvas::drawInto(ID2D1RenderTarget* rt, double scale) {
	const double w = width(), h = height();
	if (needsFit && w > 1 && h > 1) zoomToFit(false);
	const bool sim = win->simView();
	const bool dark = prefs().dark || sim;
	const Palette pal{ dark, sim };
	rt->Clear(d2dColor(pal.canvas()));
	if (prefs().showGrid) drawGrid(rt, pal, scale, win->appearProgress());

	CLDocument* doc = win->document();
	const int p = page();
	if (doc == nullptr || p < 0) return;
	CLDrawOptions o;
	o.dark = dark;
	o.accent = prefs().accent;
	o.wireScale = prefs().wireScale();
	o.simView = sim;
	o.thumbnail = false;
	o.showSelection = true;
	o.selectionFade = win->selectionFade();
	cl_document_draw_ex(doc, p, rt, scale, originX, originY, upp, &o);
	if (sim) {
		cl_simview_draw_flow(doc, p, rt, scale, originX, originY, upp, win->flowPhase(), prefs().wireScale());
	} else {
		const RGBA a = accentColor(dark);
		cl_edit_draw_overlay(doc, p, rt, scale, originX, originY, upp, a.r, a.g, a.b);
		double l, b, r, t, alpha;
		if (cl_edit_box(doc, &l, &b, &r, &t)) drawBox(rt, l, b, r, t, a, 1);
		else if (win->dragFadeBox(l, b, r, t, alpha) && alpha > 0) drawBox(rt, l, b, r, t, a, alpha);
	}
	drawOverlays(rt, (float)w, (float)h);
}

// ---- Overlays ------------------------------------------------------------------

void Canvas::drawOverlays(ID2D1RenderTarget* rt, float w, float h) {
	hits.clear();
	sliderTrack = D2D1::RectF(0, 0, 0, 0);
	// In a split, Simulation View's bar sits under the first side; the
	// banner and the note are the side's you're working in.
	const bool working = win->currentCanvas() == this;
	if (win->simView()) { if (win->paneOf(this) == 0) drawSimBar(rt, w, h); }
	else if (working) drawBanner(rt, w);
	if (working) drawToast(rt, w, h);
	double t;
	if (win->openingCard(t)) drawOpeningCard(rt, w, h, t);
}

// A circuit opening (the Mac's OpeningCard): over the canvas, a small glass
// card with its name and what's in it, a sweep of light and a quick line
// filling; then the card lifts away and the circuit fades up. 0.88 s.
void Canvas::drawOpeningCard(ID2D1RenderTarget* rt, float w, float h, double t) {
	auto ease = [](double x) { const double c = std::min(1.0, std::max(0.0, x)); return 1 - std::pow(1 - c, 3); };
	auto smooth = [](double x) { const double c = std::min(1.0, std::max(0.0, x)); return c * c * c * (c * (c * 6 - 15) + 10); };
	const bool dark = prefs().dark;
	const double inP = t < 0 ? 0 : ease(t / 0.24), line = smooth((t - 0.1) / 0.45), out = smooth((t - 0.58) / 0.3);
	const double sweep = smooth((t - 0.14) / 0.5);
	// The canvas, held back until the card lifts.
	const Palette pal{ dark, false };
	fillRect(rt, D2D1::RectF(0, 0, w, h), withAlpha(d2dColor(pal.canvas()), (float)(1 - out)));
	const float opacity = (float)(inP * (1 - out));
	if (opacity <= 0.003f) return;
	const std::string title = win->titleText(), detail = win->openingDetail();
	const float textW = std::max({ textWidth(title, 15, true), textWidth(detail, 11.5f), 170.0f });
	const float cw = std::max(300.0f, 20 + 46 + 14 + textW + 20), ch = 16 + 46 + 16 + 4;
	const D2D1_RECT_F card = D2D1::RectF((w - cw) / 2, (h - ch) / 2, (w + cw) / 2, (h + ch) / 2);
	const float s = (float)((0.94 + 0.06 * inP) * (1 + 0.04 * out));
	D2D1_MATRIX_3X2_F was;
	rt->GetTransform(&was);
	rt->SetTransform(D2D1::Matrix3x2F::Scale(s, s, D2D1::Point2F(w / 2, h / 2)) * was);
	rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), opacity), nullptr);
	brand::glow(rt, D2D1::RectF(card.left, card.top + 10, card.right, card.bottom + 10), 20, D2D1::ColorF(0, 0, 0, dark ? 0.45f : 0.18f), 22);
	fillRound(rt, card, 20, dark ? D2D1::ColorF(0.17f, 0.18f, 0.21f, 0.97f) : D2D1::ColorF(0.985f, 0.985f, 0.99f, 0.97f));
	strokeRound(rt, D2D1::RectF(card.left + 0.4f, card.top + 0.4f, card.right - 0.4f, card.bottom - 0.4f), 19.6f,
	            dark ? D2D1::ColorF(1, 1, 1, 0.14f) : D2D1::ColorF(0, 0, 0, 0.08f), 0.8f);
	const float ix = card.left + 20, iy = card.top + 16;
	brand::icon(rt, ix, iy, 46, 0.35f);
	const float tx = ix + 46 + 14;
	drawText(rt, title, D2D1::RectF(tx, iy - 1, card.right - 16, iy + 20), 15, dark ? D2D1::ColorF(1, 1, 1) : D2D1::ColorF(0, 0, 0, 0.85f),
	         TextAlign::Leading, true);
	drawText(rt, detail, D2D1::RectF(tx, iy + 21, card.right - 16, iy + 37), 11.5f,
	         dark ? D2D1::ColorF(1, 1, 1, 0.55f) : D2D1::ColorF(0, 0, 0, 0.5f));
	const D2D1_RECT_F track = D2D1::RectF(tx, iy + 44, tx + 170, iy + 46.5f);
	fillRound(rt, track, 1.25f, dark ? D2D1::ColorF(1, 1, 1, 0.08f) : D2D1::ColorF(0, 0, 0, 0.08f));
	if (line > 0.01) {
		const D2D1_RECT_F fill = D2D1::RectF(track.left, track.top, track.left + (float)(170 * line), track.bottom);
		fillRound(rt, D2D1::RectF(fill.left - 2, fill.top - 2, fill.right + 2, fill.bottom + 2), 3, withAlpha(brand::kNeon, 0.2f));
		fillRound(rt, fill, 1.25f, brand::kNeon);
	}
	// The sweep of light across the card.
	if (sweep > 0 && sweep < 1) {
		rt->PushAxisAlignedClip(card, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
		const float bx = (float)(card.left - 120 + (cw + 240) * sweep);
		D2D1_GRADIENT_STOP st[3] = { { 0, D2D1::ColorF(1, 1, 1, 0) }, { 0.5f, D2D1::ColorF(1, 1, 1, dark ? 0.12f : 0.35f) }, { 1, D2D1::ColorF(1, 1, 1, 0) } };
		ID2D1GradientStopCollection* stops = nullptr;
		ID2D1LinearGradientBrush* b = nullptr;
		if (SUCCEEDED(rt->CreateGradientStopCollection(st, 3, &stops)) &&
		    SUCCEEDED(rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(bx, 0), D2D1::Point2F(bx + 90, 0)), stops, &b))) {
			D2D1_MATRIX_3X2_F in;
			rt->GetTransform(&in);
			rt->SetTransform(D2D1::Matrix3x2F::Rotation(18, D2D1::Point2F(bx + 45, (card.top + card.bottom) / 2)) * in);
			rt->FillRectangle(D2D1::RectF(bx, card.top - ch / 2, bx + 90, card.bottom + ch / 2), b);
			rt->SetTransform(in);
		}
		if (b) b->Release();
		if (stops) stops->Release();
		rt->PopAxisAlignedClip();
	}
	rt->PopLayer();
	rt->SetTransform(was);
}

// Simulation View's control bar (SimBar.swift): a dark glass panel along the
// bottom with a breathing LIVE light, pause and step, the speed, a chip for
// every switch and light, and Done.
void Canvas::drawSimBar(ID2D1RenderTarget* rt, float w, float h) {
	const D2D1_COLOR_F on = D2D1::ColorF(0.28f, 0.93f, 1.0f), ink = D2D1::ColorF(0.86f, 0.93f, 1.0f);
	const D2D1_COLOR_F dim = D2D1::ColorF(0.48f, 0.58f, 0.68f), live = D2D1::ColorF(0.36f, 1.0f, 0.62f);
	const D2D1_COLOR_F amber = D2D1::ColorF(1.0f, 0.72f, 0.25f);
	const float barW = std::min(1040.0f, w - 28), barH = 52;
	if (barW < 360) return;
	const float x0 = (w - barW) / 2, y0 = h - 14 - barH, cy = y0 + barH / 2;
	const D2D1_RECT_F bar = D2D1::RectF(x0, y0, x0 + barW, y0 + barH);
	fillRound(rt, D2D1::RectF(bar.left, bar.top + 3, bar.right, bar.bottom + 3), 14, D2D1::ColorF(0, 0, 0, 0.30f));
	fillRound(rt, bar, 14, D2D1::ColorF(0.055f, 0.070f, 0.090f, 0.94f));
	strokeRound(rt, bar, 14, withAlpha(on, 0.22f));

	const bool paused = !win->running();
	const double t = nowSeconds();
	const float breathe = paused ? 1.0f : (float)(0.6 + 0.4 * std::sin(t * 3.2));
	const D2D1_COLOR_F light = paused ? amber : live;
	float x = x0 + 18;
	fillCircle(rt, D2D1::Point2F(x + 9, cy), 9, withAlpha(light, 0.12f * breathe));
	fillCircle(rt, D2D1::Point2F(x + 9, cy), 4, withAlpha(light, 0.55f + 0.45f * breathe));
	x += 28;
	drawText(rt, "SIMULATION", D2D1::RectF(x, cy - 15, x + 80, cy - 3), 9, dim);
	drawText(rt, paused ? "PAUSED" : "LIVE", D2D1::RectF(x, cy - 3, x + 80, cy + 13), 13, paused ? amber : ink, TextAlign::Leading, true);
	x += 70 + 14;
	auto divider = [&] { fillRect(rt, D2D1::RectF(x, cy - 14, x + 1, cy + 14), D2D1::ColorF(1, 1, 1, 0.08f)); x += 1 + 14; };
	divider();
	auto button = [&](wchar_t glyph, bool lit, D2D1_COLOR_F color, int command) {
		const D2D1_RECT_F r = D2D1::RectF(x, cy - 16, x + 32, cy + 16);
		const bool hot = hotHit == (int)hits.size();
		fillRound(rt, r, 9, lit ? withAlpha(on, 0.18f) : D2D1::ColorF(1, 1, 1, hot ? 0.12f : 0.07f));
		strokeRound(rt, r, 9, D2D1::ColorF(1, 1, 1, 0.08f));
		drawIcon(rt, glyph, r, 13, color);
		hits.push_back({ r, command });
		x += 32 + 8;
	};
	button(paused ? Icon::Play : Icon::Pause, paused, paused ? on : ink, CMD_RUNNING);
	button(Icon::Step, false, ink, CMD_STEP);
	x += 6;
	divider();
	// Speed, fast on the right.
	drawText(rt, "SPEED", D2D1::RectF(x, cy - 18, x + 60, cy - 6), 9, dim);
	const float trackW = 150, ty = cy + 5;
	sliderTrack = D2D1::RectF(x, ty - 10, x + trackW, ty + 10);
	const float f = (float)speedFraction(cl_document_step_ms(win->document()));
	fillRound(rt, D2D1::RectF(x, ty - 2, x + trackW, ty + 2), 2, D2D1::ColorF(1, 1, 1, 0.12f));
	fillRound(rt, D2D1::RectF(x, ty - 2, x + std::max(4.0f, trackW * f), ty + 2), 2, withAlpha(on, 0.75f));
	fillCircle(rt, D2D1::Point2F(x + trackW * f, ty), 10, withAlpha(on, 0.14f));
	fillCircle(rt, D2D1::Point2F(x + trackW * f, ty), 6, D2D1::ColorF(0.92f, 1, 1));
	x += trackW + 12;
	drawText(rt, strf("%d ms / step", cl_document_step_ms(win->document())), D2D1::RectF(x, ty - 8, x + 84, ty + 8), 11, ink);
	x += 84 + 14;
	divider();

	// The Done button at the right end; the chips in what's left.
	const float doneW = 70;
	const D2D1_RECT_F done = D2D1::RectF(x0 + barW - 18 - doneW, cy - 15, x0 + barW - 18, cy + 15);
	const bool doneHot = hotHit == (int)hits.size();
	fillRound(rt, done, 9, D2D1::ColorF(1, 1, 1, doneHot ? 0.12f : 0.07f));
	strokeRound(rt, done, 9, D2D1::ColorF(1, 1, 1, 0.10f));
	drawText(rt, "Done", D2D1::RectF(done.left + 13, done.top, done.left + 46, done.bottom), 12, ink);
	drawText(rt, "esc", D2D1::RectF(done.left + 44, done.top + 1, done.right, done.bottom), 9, dim);
	hits.push_back({ done, CMD_SIM_VIEW });

	CLSimChip chips[64];
	const int n = std::min(64, cl_simview_chips(win->document(), page(), chips, 64));
	std::vector<CLSimChip> ins, outs;
	for (int i = 0; i < n; i++) (chips[i].isInput ? ins : outs).push_back(chips[i]);
	const float room = done.left - 14 - x;
	auto rowWidth = [](size_t count) { return count ? 22.0f + 15.0f * count : 0.0f; };
	size_t cap = 24;
	while (cap > 0 && rowWidth(std::min(cap, ins.size())) + rowWidth(std::min(cap, outs.size())) + 14 > room) cap /= 2;
	if (cap == 0) return;
	auto row = [&](const char* label, const std::vector<CLSimChip>& list) {
		if (list.empty()) return;
		drawText(rt, label, D2D1::RectF(x, cy - 8, x + 24, cy + 8), 9, dim);
		x += 22;
		for (size_t i = 0; i < std::min(cap, list.size()); i++) {
			const D2D1_RECT_F c = D2D1::RectF(x, cy - 5, x + 10, cy + 5);
			if (list[i].lit) fillRound(rt, D2D1::RectF(c.left - 3, c.top - 3, c.right + 3, c.bottom + 3), 5, withAlpha(on, 0.16f));
			fillRound(rt, c, 3, list[i].lit ? on : D2D1::ColorF(1, 1, 1, 0.10f));
			x += 15;
		}
		x += 14;
	};
	row("IN", ins);
	row("OUT", outs);
}

// Tidy Up's preview and Lock: a bar over the top of the canvas with what's
// happening and its buttons.
void Canvas::drawBanner(ID2D1RenderTarget* rt, float w) {
	std::string text;
	std::vector<CircuitWindow::BannerButton> buttons;
	if (!win->banner(text, buttons)) return;
	const Chrome c = chrome();
	const D2D1_COLOR_F ink = c.barInk();
	std::vector<float> bw;
	float total = 16 + textWidth(text, 12) + 12;
	for (const auto& b : buttons) { bw.push_back(textWidth(b.label, 12) + 22); total += bw.back() + 6; }
	total += 6;
	if (win->locked()) total += 20;
	const float x0 = std::max(8.0f, (w - total) / 2), y0 = 12, hgt = 38;
	const D2D1_RECT_F pill = D2D1::RectF(x0, y0, x0 + total, y0 + hgt);
	fillRound(rt, D2D1::RectF(pill.left, pill.top + 2, pill.right, pill.bottom + 2), hgt / 2, D2D1::ColorF(0, 0, 0, c.dark ? 0.35f : 0.10f));
	fillRound(rt, pill, hgt / 2, c.dark ? rgb255(40, 43, 50, 0.97f) : D2D1::ColorF(1, 1, 1, 0.97f));
	strokeRound(rt, pill, hgt / 2, withAlpha(ink, c.dark ? 0.14f : 0.10f));
	float x = x0 + 16;
	if (win->locked()) {
		drawIcon(rt, Icon::Lock, D2D1::RectF(x - 2, y0, x + 16, y0 + hgt), 12, withAlpha(ink, 0.8f));
		x += 20;
	}
	drawText(rt, text, D2D1::RectF(x, y0, x + textWidth(text, 12) + 2, y0 + hgt), 12, ink);
	x += textWidth(text, 12) + 12;
	for (size_t i = 0; i < buttons.size(); i++) {
		const D2D1_RECT_F r = D2D1::RectF(x, y0 + 6, x + bw[i], y0 + hgt - 6);
		const bool hot = hotHit == (int)hits.size();
		const bool primary = i == 0;
		fillRound(rt, r, 7, primary ? withAlpha(c.accent(), hot ? 1.0f : 0.9f) : withAlpha(ink, hot ? 0.14f : 0.08f));
		drawText(rt, buttons[i].label, r, 12, primary ? c.onAccent() : ink, TextAlign::Center, primary);
		hits.push_back({ r, buttons[i].command });
		x += bw[i] + 6;
	}
}

// A note (Saved, Copied...) over the bottom of the canvas, as a dark pill
// that fades.
void Canvas::drawToast(ID2D1RenderTarget* rt, float w, float h) {
	std::string text;
	double alpha = 0;
	if (!win->toast(text, alpha)) return;
	const float a = (float)alpha;
	const float tw = std::min(w - 40, textWidth(text, 12) + 32), th = 32;
	const float bottom = h - 16 - (win->simView() && win->paneOf(this) == 0 ? 52 + 14 : 0);   // over the bar
	const D2D1_RECT_F r = D2D1::RectF((w - tw) / 2, bottom - th, (w + tw) / 2, bottom);
	const bool dark = prefs().dark || win->simView();
	fillRound(rt, D2D1::RectF(r.left, r.top + 2, r.right, r.bottom + 2), th / 2, D2D1::ColorF(0, 0, 0, 0.18f * a));
	fillRound(rt, r, th / 2, dark ? D2D1::ColorF(0.24f, 0.26f, 0.30f, 0.96f * a) : D2D1::ColorF(0.13f, 0.14f, 0.16f, 0.92f * a));
	drawText(rt, text, D2D1::RectF(r.left + 14, r.top, r.right - 14, r.bottom), 12, D2D1::ColorF(1, 1, 1, a), TextAlign::Center);
}

bool Canvas::overlayPress(double vx, double vy) {
	const float x = (float)vx, y = (float)vy;
	if (inRect(sliderTrack, x, y) && sliderTrack.right > sliderTrack.left) {
		drag = Drag::Slider;
		SetCapture(hwnd);
		setSpeedAt(vx);
		return true;
	}
	for (const OverlayHit& h : hits) {
		if (!inRect(h.rect, x, y)) continue;
		win->run(h.command);
		redraw();
		return true;
	}
	return false;
}

void Canvas::setSpeedAt(double vx) {
	const float f = (float)((vx - sliderTrack.left) / std::max(1.0f, sliderTrack.right - sliderTrack.left));
	win->setStepMs(speedFromFraction(f));
	redraw();
}

void Canvas::drawBox(ID2D1RenderTarget* rt, double l, double b, double r, double t, const RGBA& accent, double alpha) {
	const float x = (float)((l - originX) / upp), y = (float)((originY - t) / upp);
	const float w = (float)((r - l) / upp), h = (float)((t - b) / upp);
	ID2D1SolidColorBrush* brush = nullptr;
	if (FAILED(rt->CreateSolidColorBrush(D2D1::ColorF((float)accent.r, (float)accent.g, (float)accent.b, (float)(0.25 * alpha)), &brush)))
		return;
	rt->FillRectangle(D2D1::RectF(x, y, x + w, y + h), brush);
	brush->SetColor(D2D1::ColorF((float)accent.r, (float)accent.g, (float)accent.b, (float)alpha));
	const float px = 1.0f / (float)scale(), half = px / 2;
	rt->DrawRectangle(D2D1::RectF(x + half, y + half, x + std::max(half, w - half), y + std::max(half, h - half)), brush, px);
	brush->Release();
}

// GUICanvas::drawGridInto: a line (or dot) every grid unit, spread out so
// they're never closer than 13 pixels; every fifth one darker.
void Canvas::drawGrid(ID2D1RenderTarget* rt, const Palette& pal, double scale, double fade) {
	const double w = width(), h = height();
	const double unitsPerPixel = upp / scale;
	const int space = std::max(1, (int)(13 * unitsPerPixel));
	const float hair = (float)(1 / scale);
	const long x0 = (long)std::floor(originX / space), x1 = (long)std::ceil((originX + w * upp) / space);
	const long y0 = (long)std::floor((originY - h * upp) / space), y1 = (long)std::ceil(originY / space);
	if (x1 < x0 || y1 < y0 || x1 - x0 >= 4000 || y1 - y0 >= 4000) return;
	const bool majorOn = prefs().majorGrid;
	auto major = [&](long i) { return majorOn && ((i % 5) + 5) % 5 == 0; };
	auto sx = [&](long i) { return ((double)i * space - originX) / upp; };
	auto sy = [&](long i) { return (originY - (double)i * space) / upp; };
	ID2D1SolidColorBrush* brush = nullptr;
	if (FAILED(rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0), &brush))) return;

	if (prefs().gridStyle == 1) {
		if ((x1 - x0 + 1) * (y1 - y0 + 1) < 60000) {
			const float r = (float)(1.1 / scale), rMajor = (float)(1.7 / scale);
			const RGBA dot = pal.grid(0.08 * 3 * fade), dotMajor = pal.grid(0.08 * 5 * fade);
			for (long ix = x0; ix <= x1; ix++) {
				for (long iy = y0; iy <= y1; iy++) {
					const bool isMajor = major(ix) && major(iy);
					brush->SetColor(d2dColor(isMajor ? dotMajor : dot));
					const float rr = isMajor ? rMajor : r;
					rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F((float)sx(ix), (float)sy(iy)), rr, rr), brush);
				}
			}
		}
		brush->Release();
		return;
	}
	// Crisp one-pixel lines: no antialiasing, each on a pixel's middle.
	rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
	for (int pass = 0; pass < 2; pass++) {
		const bool isMajor = pass == 1;
		// On the dark canvas the darker lines stay quieter, so low wires
		// don't read as grid.
		brush->SetColor(d2dColor(pal.grid((isMajor ? 0.08 * (pal.dark && !pal.simView ? 1.6 : 2.5) : 0.08) * fade)));
		for (long i = x0; i <= x1; i++) {
			if (major(i) != isMajor) continue;
			const float x = (float)((std::floor(sx(i) * scale) + 0.5) / scale);
			rt->DrawLine(D2D1::Point2F(x, 0), D2D1::Point2F(x, (float)h), brush, hair);
		}
		for (long i = y0; i <= y1; i++) {
			if (major(i) != isMajor) continue;
			const float y = (float)((std::floor(sy(i) * scale) + 0.5) / scale);
			rt->DrawLine(D2D1::Point2F(0, y), D2D1::Point2F((float)w, y), brush, hair);
		}
	}
	rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	brush->Release();
}

// ---- Camera --------------------------------------------------------------------

bool Canvas::fitBoxOfPage(double& l, double& b, double& r, double& t) const {
	const int p = page();
	if (p < 0 || !cl_document_page_bounds(win->document(), p, &l, &b, &r, &t)) {
		l = -20; b = -15; r = 20; t = 15;
		return false;
	}
	return true;
}

void Canvas::zoomToFit(bool animate) {
	const double w = width(), h = height();
	if (w <= 1 || h <= 1) { needsFit = true; return; }
	needsFit = false;
	double l, b, r, t;
	fitBoxOfPage(l, b, r, t);
	const double pad = 3;
	const double u = clampUpp(std::max((r - l + 2 * pad) / w, (t - b + 2 * pad) / h));
	const double ox = (l + r) / 2 - w * u / 2, oy = (b + t) / 2 + h * u / 2;
	if (animate) {
		startZoom(ox, oy, u);
	} else {
		zooming = false;
		upp = u; originX = ox; originY = oy;
		redraw();
		win->statusNeedsUpdate();
	}
}

void Canvas::zoomBy(double factor, double vx, double vy) {
	if (!(factor > 0) || !std::isfinite(factor)) return;
	zooming = false;
	double wx, wy;
	worldPoint(vx, vy, wx, wy);
	upp = clampUpp(upp / factor);
	originX = wx - vx * upp;
	originY = wy + vy * upp;
	redraw();
	win->statusNeedsUpdate();
}

void Canvas::animateZoom(double factor) {
	// From where an eased zoom in progress is heading, so quick presses add up.
	const double bx = zooming ? toX : originX, by = zooming ? toY : originY, bu = zooming ? toUpp : upp;
	const double cx = width() / 2, cy = height() / 2;
	const double wx = bx + cx * bu, wy = by - cy * bu;
	const double u = clampUpp(bu / factor);
	startZoom(wx - cx * u, wy + cy * u, u);
}

void Canvas::zoomActual() { animateZoom((zooming ? toUpp : upp) / 0.1); }

int Canvas::zoomPercent() const { return upp > 0 ? (int)std::lround(100 * 0.1 / upp) : 100; }

void Canvas::startZoom(double ox, double oy, double u) {
	fromX = originX; fromY = originY; fromUpp = upp;
	toX = ox; toY = oy; toUpp = u;
	zoomStart = nowSeconds();
	zooming = true;
	stepAnimation();
}

bool Canvas::stepAnimation() {
	if (!zooming) return false;
	const double t = std::min(1.0, (nowSeconds() - zoomStart) / kZoomTime);
	const double e = 1 - std::pow(1 - t, 3);
	// The scale eases geometrically; the origin follows.
	upp = fromUpp * std::pow(toUpp / fromUpp, e);
	originX = fromX + (toX - fromX) * e;
	originY = fromY + (toY - fromY) * e;
	if (t >= 1) { zooming = false; originX = toX; originY = toY; upp = toUpp; }
	redraw();
	win->statusNeedsUpdate();
	return zooming;
}

void Canvas::pan(double dx, double dy) {
	zooming = false;
	originX -= dx * upp;
	originY += dy * upp;
	redraw();
	win->statusNeedsUpdate();
}

void Canvas::centerOn(double wx, double wy) {
	const double u = zooming ? toUpp : upp;
	startZoom(wx - width() / 2 * u, wy + height() / 2 * u, u);
}

void Canvas::panTo(double wx, double wy) {
	zooming = false;
	originX = wx - width() / 2 * upp;
	originY = wy + height() / 2 * upp;
	redraw();
	win->statusNeedsUpdate();
}

void Canvas::onSize(double w, double h) {
	// Keep the middle of the view where it was as the window resizes.
	if (lastW > 1 && lastH > 1 && !needsFit && w > 1 && h > 1) {
		originX -= (w - lastW) / 2.0 * upp;
		originY += (h - lastH) / 2.0 * upp;
	}
	if (w > 1 && h > 1) { lastW = w; lastH = h; }
	InvalidateRect(hwnd, nullptr, FALSE);
}

// ---- Window messages -------------------------------------------------------------

LRESULT Canvas::handle(UINT msg, WPARAM wp, LPARAM lp) {
	double vx, vy;
	switch (msg) {
	case WM_PAINT:
		paint();
		return 0;
	case WM_ERASEBKGND:
		return 1;
	case WM_SIZE:
		onSize(LOWORD(lp) / scale(), HIWORD(lp) / scale());
		return 0;
	case WM_SETCURSOR:
		if (LOWORD(lp) == HTCLIENT) {
			SetCursor(LoadCursor(nullptr, cursorNow ? cursorNow : IDC_ARROW));
			return TRUE;
		}
		break;
	case WM_MOUSEACTIVATE:
		SetFocus(hwnd);
		return MA_ACTIVATE;
	case WM_LBUTTONDOWN: viewPoint(lp, vx, vy); onPress(1, vx, vy, false, wp); return 0;
	case WM_LBUTTONDBLCLK: viewPoint(lp, vx, vy); onPress(1, vx, vy, true, wp); return 0;
	case WM_MBUTTONDOWN: viewPoint(lp, vx, vy); onPress(2, vx, vy, false, wp); return 0;
	case WM_RBUTTONDOWN: viewPoint(lp, vx, vy); onPress(3, vx, vy, false, wp); return 0;
	case WM_LBUTTONUP: viewPoint(lp, vx, vy); onRelease(1, vx, vy); return 0;
	case WM_MBUTTONUP: viewPoint(lp, vx, vy); onRelease(2, vx, vy); return 0;
	case WM_MOUSEMOVE:
		if (!pointerInside) {
			pointerInside = true;
			TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, hwnd, 0 };
			TrackMouseEvent(&t);
		}
		viewPoint(lp, vx, vy);
		onMotion(vx, vy);
		return 0;
	case WM_MOUSELEAVE:
		pointerInside = false;
		if (drag == Drag::None && cl_edit_hover_clear(win->document())) redraw();
		return 0;
	case WM_MOUSEWHEEL:
	case WM_MOUSEHWHEEL: {
		POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
		onWheel(GET_WHEEL_DELTA_WPARAM(wp), msg == WM_MOUSEHWHEEL, GET_KEYSTATE_WPARAM(wp), p);
		return 0;
	}
	case WM_CAPTURECHANGED:
		// Something took the pointer mid-drag (a menu, another window):
		// finish the gesture where it is, so nothing is left half-done.
		if ((HWND)lp != hwnd && drag != Drag::None) cancelDrag();
		return 0;
	case WM_KILLFOCUS:
		spaceDown = false;
		return 0;
	case WM_SETFOCUS:
		win->canvasFocused(this);   // a click in a split's other side works there
		return 0;
	case WM_NCHITTEST:
		// The lines that drag (the side panel's edge, a split's middle, the
		// oscilloscope's top) are the window's.
		if (win->onDivider(POINT{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) })) return HTTRANSPARENT;
		break;
	case WM_KEYDOWN:
		if (onKeyDown((UINT)wp, lp)) return 0;
		break;
	case WM_KEYUP:
		if (onKeyUp((UINT)wp)) return 0;
		break;
	case WM_CHAR:
		if (onChar((wchar_t)wp)) return 0;
		return 0;   // no beep for letters the canvas has no use for
	case WM_GETDLGCODE:
		return DLGC_WANTALLKEYS;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---- Pointer -------------------------------------------------------------------

void Canvas::cancelDrag() {
	const Drag was = drag;
	drag = Drag::None;
	if (was == Drag::Edit) {
		double wx, wy;
		worldPoint(lastX, lastY, wx, wy);
		cl_edit_release(win->document(), wx, wy);
		win->edited();
	}
	if (GetCapture() == hwnd) ReleaseCapture();
	setCursor(nullptr);
	redraw();
}

bool Canvas::moving() const { return drag == Drag::Edit || win->isFloating(); }

void Canvas::onPress(int button, double vx, double vy, bool doubleClick, WPARAM keys) {
	CLDocument* doc = win->document();
	const int p = page();
	if (doc == nullptr || p < 0) return;
	SetFocus(hwnd);
	zooming = false;
	lastX = vx;
	lastY = vy;
	double wx, wy;
	worldPoint(vx, vy, wx, wy);

	if (button == 2) {   // the middle button moves around
		drag = Drag::Pan;
		SetCapture(hwnd);
		setCursor(IDC_SIZEALL);
		return;
	}
	if (button == 3) {
		if (win->simView()) return;
		if (win->locked()) { win->lockNudge(); return; }
		const int target = cl_edit_context(doc, p, wx, wy, upp);
		if (prefs().rightClickRotate && target == CL_CONTEXT_GATE) {
			win->rotate();
			return;
		}
		win->selectionChanged();
		redraw();
		POINT screen;
		GetCursorPos(&screen);
		win->showContextMenu(target, wx, wy, screen);
		return;
	}
	if (button != 1) return;
	if (overlayPress(vx, vy)) return;

	// Windows sends the second press of a double-click as a double-click
	// only; GTK and AppKit send it as a press too. Take it as a press first.
	[&] {
		if (win->hasPendingGate() && win->placePendingGate(wx, wy)) {
			redraw();
			return;   // it's on the pointer now; the next click drops it
		}
		if (spaceDown || (keys & MK_CONTROL)) {
			drag = Drag::Pan;
			pannedWhileSpaceDown = true;
			SetCapture(hwnd);
			setCursor(IDC_SIZEALL);
			return;
		}
		// Simulation View and Lock: parts still take clicks (switches,
		// keypads); anywhere else, a drag moves around.
		if (win->simView() || win->locked()) {
			if (cl_document_click(doc, p, wx, wy)) {
				win->redraw();
			} else {
				drag = Drag::Pan;
				SetCapture(hwnd);
				setCursor(IDC_SIZEALL);
				if (win->locked() && !win->simView()) win->lockNudge();
			}
			return;
		}
		cl_edit_press(doc, p, wx, wy, modifiersNow(), upp);
		drag = Drag::Edit;
		SetCapture(hwnd);
		redraw();
		win->selectionChanged();
	}();
	if (!doubleClick) return;

	// The double-click: end the gesture in place, then open the gate's
	// settings, or fit the page.
	if (win->isFloating()) return;
	const Drag was = drag;
	drag = Drag::None;
	if (was == Drag::Edit) cl_edit_release(doc, wx, wy);
	if (GetCapture() == hwnd) ReleaseCapture();
	setCursor(nullptr);
	if (win->simView() || win->locked()) return;
	if (cl_edit_single_gate(doc, p) >= 0) win->showSettings();
	else zoomToFit(true);
	win->selectionChanged();
	redraw();
}

void Canvas::onMotion(double vx, double vy) {
	CLDocument* doc = win->document();
	const int p = page();
	if (doc == nullptr || p < 0) return;
	const double dx = vx - lastX, dy = vy - lastY;
	lastX = vx;
	lastY = vy;
	double wx, wy;
	worldPoint(vx, vy, wx, wy);
	win->pointerMoved(wx, wy);
	if (drag == Drag::None) {
		int h = -1;
		for (int i = 0; i < (int)hits.size(); i++) if (inRect(hits[i].rect, (float)vx, (float)vy)) h = i;
		if (h != hotHit) { hotHit = h; InvalidateRect(hwnd, nullptr, FALSE); }
		if (h >= 0 || inRect(sliderTrack, (float)vx, (float)vy)) { setCursor(IDC_HAND); return; }
		if (cursorNow == IDC_HAND && !spaceDown) setCursor(nullptr);
	}
	switch (drag) {
	case Drag::Slider:
		setSpeedAt(vx);
		break;
	case Drag::Pan:
		pan(dx, dy);
		break;
	case Drag::Edit:
		cl_edit_drag(doc, wx, wy);
		redraw();
		break;
	case Drag::None:
		if (win->hasPendingGate() && win->placePendingGate(wx, wy)) { redraw(); break; }
		if (win->simView() || (win->isFloating() && this != win->currentCanvas())) break;   // it floats on its own side
		if (cl_edit_hover(doc, p, wx, wy, upp)) redraw();
		break;
	}
}

void Canvas::onRelease(int button, double vx, double vy) {
	CLDocument* doc = win->document();
	lastX = vx;
	lastY = vy;
	if (drag == Drag::Edit && button == 1 && doc) {
		double l, b, r, t;
		if (cl_edit_box(doc, &l, &b, &r, &t)) win->fadeOutDragBox(l, b, r, t);
		double wx, wy;
		worldPoint(vx, vy, wx, wy);
		drag = Drag::None;
		if (GetCapture() == hwnd) ReleaseCapture();
		cl_edit_release(doc, wx, wy);
		win->edited();
	} else if (drag == Drag::Slider && button == 1) {
		drag = Drag::None;
		if (GetCapture() == hwnd) ReleaseCapture();
	} else if (drag == Drag::Pan && (button == 1 || button == 2)) {
		drag = Drag::None;
		if (GetCapture() == hwnd) ReleaseCapture();
	}
	setCursor(spaceDown ? IDC_HAND : nullptr);
	redraw();
}

// Per device (Preferences > Canvas): a wheel mouse zooms and a touchpad moves
// around, by default. Ctrl+scroll always zooms (a touchpad's pinch arrives
// as that); Shift+scroll moves sideways.
void Canvas::onWheel(int delta, bool horizontal, WPARAM keys, POINT screen) {
	zooming = false;
	const Prefs& pr = prefs();
	// A wheel mouse turns in whole notches (120); a precision touchpad
	// scrolls in finer steps.
	const bool touchpad = delta % WHEEL_DELTA != 0;
	const bool ctrl = (keys & MK_CONTROL) != 0, shift = (keys & MK_SHIFT) != 0;
	const double notches = (double)delta / WHEEL_DELTA;
	// Deltas as GTK counts them: +y is scrolling down, +x to the right.
	const double dx = horizontal ? notches : 0, dy = horizontal ? 0 : -notches;
	if (dx == 0 && dy == 0) return;
	const bool vertical = !horizontal;
	const bool zooms = touchpad ? pr.touchpadScroll == 0 : pr.mouseWheel == 0;
	const double inSign = (touchpad ? pr.reverseTouchpad : pr.reverseWheel) ? -1 : 1;
	double vx, vy;
	screenToView(screen, vx, vy);
	// "In" is scrolling up (away), one wheel notch a ZOOM_STEP (0.75) as in wx.
	if (ctrl || (zooms && !shift && vertical)) {
		const double steps = -dy * (ctrl ? 1 : inSign);
		zoomBy(std::pow(1 / 0.75, steps), vx, vy);
	} else if (shift && vertical) {
		pan(-dy * 40, 0);
	} else {
		pan(-dx * 40, -dy * 40);
	}
}

// ---- Keys ----------------------------------------------------------------------

bool Canvas::onKeyDown(UINT vk, LPARAM lp) {
	CLDocument* doc = win->document();
	if (doc == nullptr) return false;
	const bool shift = down(VK_SHIFT);
	const bool bare = bareKey();
	const bool repeat = (lp & (1 << 30)) != 0;
	const bool arrow = vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN;
	const bool isLeft = vk == VK_LEFT, isRight = vk == VK_RIGHT, isUp = vk == VK_UP;
	const bool isReturn = vk == VK_RETURN;
	const bool isDelete = vk == VK_DELETE || vk == VK_BACK;

	// Simulation View: Escape leaves, Space runs and pauses, arrows move around.
	if (win->simView()) {
		if (!bare) return false;
		if (vk == VK_ESCAPE) win->toggleSimView();
		else if (vk == VK_SPACE) { if (!spaceDown && !repeat) { spaceDown = true; win->toggleRunning(); } }
		else if (arrow) pan(isLeft ? 40 : isRight ? -40 : 0, isUp ? 40 : (isLeft || isRight) ? 0 : -40);
		else if (vk == VK_OEM_PLUS || vk == VK_ADD) animateZoom(1 / 0.75);
		else if (vk == VK_OEM_MINUS || vk == VK_SUBTRACT) animateZoom(0.75);
		else if (vk == 'T' && !shift) win->makeTruthTable();
		return true;
	}

	// Tidy Up on show: Return keeps it, Escape puts it back, Tab tries the
	// other mode. Anything else keeps it and carries on.
	if (win->tidyActive() && bare) {
		if (isReturn) { win->endTidy(true); return true; }
		if (vk == VK_ESCAPE) { win->endTidy(false); return true; }
		if (vk == VK_TAB) { win->switchTidyMode(); return true; }
		if (vk != VK_SHIFT && vk != VK_LSHIFT && vk != VK_RSHIFT) win->endTidy(true);
	}

	if (vk == VK_ESCAPE) {
		if (win->hasPendingGate()) { win->clearPendingGate(); return true; }
		// Mid-move: first just the connections C made, then the move.
		if (moving() && cl_edit_take_back_connects(doc) > 0) {
			win->note("Took back the connections.");
			win->redraw();
			return true;
		}
		if (win->isFloating()) { win->cancelFloating(); return true; }
		if (drag == Drag::Edit || cl_edit_is_connecting(doc)) {
			drag = Drag::None;
			if (GetCapture() == hwnd) ReleaseCapture();
			cl_edit_cancel(doc);
			win->edited();
			return true;
		}
		win->selectNone();
		return true;
	}

	if (!bare) return false;   // Ctrl and Alt combinations are the menus'

	// Arrows nudge the selection, or move around when nothing's selected.
	if (arrow) {
		if (win->hasSelection() && win->canEdit()) {
			const double s = shift ? 2.5 : 0.5;
			win->nudge(isLeft ? -s : isRight ? s : 0, isUp ? s : (isLeft || isRight) ? 0 : -s);
		} else {
			const double s = shift ? 200 : 40;
			pan(isLeft ? s : isRight ? -s : 0, isUp ? s : (isLeft || isRight) ? 0 : -s);
		}
		return true;
	}

	if (vk == VK_SPACE) {
		if (!spaceDown) {
			spaceDown = true;
			pannedWhileSpaceDown = false;
			if (drag == Drag::None) setCursor(IDC_HAND);
		}
		return true;
	}

	if (isDelete) {
		if (!win->canEdit()) { win->lockNudge(); return true; }
		win->deleteSelection();
		return true;
	}

	// Shift+1...9, 0: that palette category (the tenth is 0).
	if (shift && vk >= '0' && vk <= '9') {
		const int n = vk == '0' ? 10 : (int)(vk - '0');
		win->showPaletteCategory(n - 1);
		return true;
	}

	// The single-letter keys, as in the wx app.
	switch (vk) {
	case 'C':
		if (shift) break;
		// C while something is moving (dragged, or floating on the pointer):
		// connect it to the pins it's next to and keep moving. The
		// connections are kept at the drop; Escape takes back just them.
		if (moving()) {
			if (win->canEdit() && pointerInside) {
				const int n = cl_edit_connect_while_moving(doc, page(), upp);
				if (n > 0) win->note(strf("Connected %d pin%s. Escape takes it back.", n, n == 1 ? "" : "s"));
				else if (n == 0) win->note("Nothing close enough to connect.");
				win->redraw();
			}
			return true;
		}
		win->copy();
		return true;
	case 'V': if (shift) break; if (win->canEdit()) win->paste(); else win->lockNudge(); return true;
	case 'X': if (shift) break; if (win->canEdit()) win->cut(); else win->lockNudge(); return true;
	case 'D': if (shift) break; if (win->canEdit()) win->duplicate(); else win->lockNudge(); return true;
	case 'A': if (shift) break; if (win->canEdit()) win->quickAdd(); else win->lockNudge(); return true;
	case 'R': if (shift) break; if (win->canEdit()) win->rotate(); else win->lockNudge(); return true;
	case 'S':
		if (!win->canEdit()) { win->lockNudge(); return true; }
		if (shift) win->tidy(); else win->straighten();
		return true;
	case 'T': if (shift) break; win->makeTruthTable(); return true;
	default: break;
	}
	return false;
}

bool Canvas::onChar(wchar_t c) {
	if (c == L'?') { win->showShortcuts(); return true; }
	return false;
}

bool Canvas::onKeyUp(UINT vk) {
	if (vk != VK_SPACE) return false;
	const bool was = spaceDown;
	spaceDown = false;
	if (drag == Drag::None) setCursor(nullptr);
	if (!was || win->simView()) return true;   // there Space pauses (on the way down)
	// A tap zooms to fit (a hold-and-drag moved around).
	if (!pannedWhileSpaceDown) zoomToFit(true);
	return true;
}
