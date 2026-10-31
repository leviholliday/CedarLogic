// The circuit canvas (see Canvas.h).

#include "Canvas.h"
#include "Brand.h"
#include "Chrome.h"
#include "DragLog.h"
#include "Ink.h"
#include "Shortcuts.h"
#include "SessionCheck.h"
#include "Window.h"

#include <imm.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

const wchar_t* kClass = L"CedarLogicCanvas";
const double kMinUpp = 0.004;   // very close
const double kMaxUpp = 1.0;     // very far
const double kZoomTime = 0.14;  // seconds, as the wx app's eased zoom
const UINT_PTR kTagTimer = 1;   // the pointer has rested on a wire
const UINT_PTR kOwedFrameTimer = 2;   // a frame put off while dragging (see WM_PAINT)

// The WM_MOUSEMOVE being handled, for the drag log.
LPARAM gMoveLp = 0;
bool gMoveHasLp = false;

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

// The wire tag's text: fixed-width and bold, as the Mac's.
IDWriteTextFormat* tagFormat() {
	static IDWriteTextFormat* f = nullptr;
	static bool tried = false;
	if (!tried && dwFactory()) {
		tried = true;
		dwFactory()->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12,
		                              L"", &f);
		if (f) {
			f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
			f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
		}
	}
	return f;
}

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
	health::liveCanvases++;
	hwnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 10, 10, parent, nullptr, appInstance(), this);
	// No input method here: nothing is typed on the canvas, and a Chinese,
	// Japanese or Korean one would take its single keys (A, R, S...) as
	// the start of a word. Text boxes keep theirs.
	if (hwnd) ImmAssociateContextEx(hwnd, nullptr, 0);
}

Canvas::~Canvas() {
	if (hwnd) {
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
		DestroyWindow(hwnd);
	}
	health::liveCanvases--;
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

// Windows sends WM_PAINT only once no input is waiting. A mouse sending
// 500-1000 moves a second keeps input waiting for as long as it moves, so a
// drag that only invalidated got its frames late and irregularly: the part
// stood still, then jumped to the pointer (windows/DRAG-DIAGNOSIS.md). Here
// a frame is drawn as soon as the display could show a new one, with
// wherever the pointer is then; moves in between only update the engine.
// The minimap, which shows nothing new until the drop, waits for it.
void Canvas::redrawWhileMoving() {
	InvalidateRect(hwnd, nullptr, FALSE);
	if (draglog::legacy()) { win->redrawMiniMap(); return; }
	paceFrame();
}

void Canvas::paceFrame() {
	if (draglog::legacy()) return;
	if (nowSeconds() - lastPaintAt >= frameSeconds * 0.9) {
		// A frame put off (WM_PAINT below) left nothing to update: asked again.
		if (frameOwed) InvalidateRect(hwnd, nullptr, FALSE);
		UpdateWindow(hwnd);
	}
}

// A drag, a pan, or gates on the pointer: frames go at the display's pace.
bool Canvas::pacingFrames() const {
	if (draglog::legacy()) return false;
	return drag == Drag::Edit || drag == Drag::Pan || win->isFloating();
}

void Canvas::noteFrameRate() {
	HDC dc = GetDC(hwnd);
	const int hz = dc ? GetDeviceCaps(dc, VREFRESH) : 0;   // 0 or 1: the hardware's default
	if (dc) ReleaseDC(hwnd, dc);
	frameSeconds = 1.0 / (hz >= 30 && hz <= 500 ? hz : 60);
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

bool Canvas::pointerOver() const {
	if (!IsWindowVisible(hwnd)) return false;
	POINT p;
	GetCursorPos(&p);
	double vx, vy;
	screenToView(p, vx, vy);
	return vx >= 0 && vy >= 0 && vx < width() && vy < height();
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
	const double t0 = nowSeconds();
	lastPaintAt = t0;
	double t1 = t0;
	if (ID2D1HwndRenderTarget* rt = surface.begin(hwnd)) {
		drawInto(rt, surface.scale());
		t1 = nowSeconds();
		surface.end();
	}
	EndPaint(hwnd, &ps);
	if (draglog::on() && (drag == Drag::Edit || win->isFloating())) {
		const double t2 = nowSeconds();
		draglog::paint(draglog::View{ hwnd, win->document(), page(), originX, originY, upp }, (t1 - t0) * 1000, (t2 - t1) * 1000);
	}
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
	CLDrawOptions o{};   // zeroed first: a field the engine adds later is off, not garbage
	o.dark = dark;
	o.accent = prefs().accent;
	o.wireScale = prefs().wireScale();
	o.simView = sim;
	o.thumbnail = false;
	o.showSelection = true;
	o.selectionFade = win->selectionFade();
	o.ink = CL_INK_FOLLOW;
	const bool projector = sim && prefs().projector;
	if (sim) {
		// Simulation View as the Mac draws it, so projector mode (much
		// thicker wires and outlines, bigger labels and lights) comes too.
		CLSimViewStyle st{};
		st.accent = prefs().accent;
		st.wireScale = prefs().wireScale();
		st.projector = projector;
		// Predict: the lights dark and every wire "off" until Reveal.
		st.predict = win->predictCovers();
		st.ink = CL_INK_FOLLOW;
		cl_simview_draw_page(doc, p, rt, scale, originX, originY, upp, &st);
	} else {
		cl_document_draw_ex(doc, p, rt, scale, originX, originY, upp, &o);
	}
	if (sim) {
		// (Not while Predict covers the lights: the flow would give the answer away.)
		if (!win->predictCovers())
			cl_simview_draw_flow(doc, p, rt, scale, originX, originY, upp, win->flowPhase(),
			                     prefs().wireScale() * (projector ? CL_PROJECTOR_WIRE_SCALE : 1));
		// Predict's covers' guesses, or the rings after Reveal.
		const std::vector<CLPredictMark> marks = win->predictMarks();
		if (!marks.empty()) cl_simview_draw_predict(doc, p, rt, scale, originX, originY, upp, marks.data(), (int)marks.size(), projector);
		// The wire under the pointer, lit up whole.
		const RGBA a = accentColor(true);
		cl_edit_draw_overlay(doc, p, rt, scale, originX, originY, upp, a.r, a.g, a.b);
	} else {
		const RGBA a = accentColor(dark);
		cl_edit_draw_overlay(doc, p, rt, scale, originX, originY, upp, a.r, a.g, a.b);
		double l, b, r, t, alpha;
		if (cl_edit_box(doc, &l, &b, &r, &t)) drawBox(rt, l, b, r, t, a, 1);
		else if (win->dragFadeBox(l, b, r, t, alpha) && alpha > 0) drawBox(rt, l, b, r, t, a, alpha);
	}
	// The stroke being drawn and what the pointer would draw, over everything.
	if (cl_ink_live_page(doc) == p) cl_ink_draw_live(doc, rt, scale, originX, originY, upp, dark, projector);
	drawOverlays(rt, (float)w, (float)h);
}

// ---- What a wire carries -------------------------------------------------------------

bool Canvas::wireTagText(std::string& text, char& state) const {
	const int p = page();
	if (p < 0) return false;
	char buf[72] = "";
	const int bits = cl_edit_hover_wire_state(win->document(), p, buf, sizeof buf);
	if (bits <= 0) return false;
	const std::string s = buf;
	state = s.find('!') != std::string::npos ? '!' : s.find('X') != std::string::npos ? 'X' : s.find('Z') != std::string::npos ? 'Z'
	        : bits == 1 ? (s.empty() ? '0' : s[0]) : 'b';
	if (bits == 1) {
		switch (state) {
		case '1': text = "1"; break;
		case '0': text = "0"; break;
		case 'Z': text = "Z \u00B7 floating (nothing drives it)"; break;
		case '!': text = "! \u00B7 conflict (outputs disagree)"; break;
		default: text = "X \u00B7 unknown"; break;
		}
	} else if (state == 'b' && bits <= 64) {
		// A bus: its bits, and what they make.
		unsigned long long v = 0;
		for (char c : s) v = v * 2 + (c == '1' ? 1 : 0);
		text = s + " = " + std::to_string(v);
	} else {
		text = s;
	}
	return true;
}

void Canvas::hideWireTag() {
	if (tagWaiting) { KillTimer(hwnd, kTagTimer); tagWaiting = false; }
	if (tagShown) { tagShown = false; InvalidateRect(hwnd, nullptr, FALSE); }
}

// Shown once the pointer has rested on a wire (sooner in Simulation View),
// and then it follows the pointer along the wire.
void Canvas::updateWireTag() {
	std::string text;
	char state = 0;
	if (!prefs().wireValueTag || !wireTagText(text, state)) { hideWireTag(); return; }
	if (tagShown) { InvalidateRect(hwnd, nullptr, FALSE); return; }
	if (tagWaiting) return;
	tagWaiting = true;
	SetTimer(hwnd, kTagTimer, win->simView() ? 600 : 1100, nullptr);
}

// A small chip beside the pointer: green for 1, grey for 0, blue for
// floating, red for a conflict, orange for unknown.
void Canvas::drawWireTag(ID2D1RenderTarget* rt, float w, float h) {
	std::string text;
	char state = 0;
	IDWriteTextFormat* f = tagFormat();
	if (!tagShown || f == nullptr || !wireTagText(text, state)) return;
	const D2D1_COLOR_F c = state == '1' ? D2D1::ColorF(0.13f, 0.68f, 0.3f) : state == '0' ? D2D1::ColorF(0.42f, 0.42f, 0.42f)
	                     : state == 'Z' ? D2D1::ColorF(0.2f, 0.47f, 0.96f) : state == '!' ? D2D1::ColorF(0.92f, 0.26f, 0.24f)
	                     : state == 'X' ? D2D1::ColorF(0.96f, 0.58f, 0.13f) : D2D1::ColorF(0.25f, 0.25f, 0.25f);
	const std::wstring wt = W(text);
	IDWriteTextLayout* layout = nullptr;
	if (FAILED(dwFactory()->CreateTextLayout(wt.c_str(), (UINT32)wt.size(), f, 2000, 20, &layout))) return;
	DWRITE_TEXT_METRICS m = {};
	layout->GetMetrics(&m);
	const float tw = m.widthIncludingTrailingWhitespace;
	const float x = (float)lastX, y = (float)lastY;
	D2D1_RECT_F r = D2D1::RectF(x + 14, y - 30, x + 14 + tw + 14, y - 10);
	if (r.right > w - 4) { const float rw = r.right - r.left; r.left = x - 14 - rw; r.right = r.left + rw; }
	if (r.top < 4) { r.top = y + 14; r.bottom = r.top + 20; }
	(void)h;
	fillRound(rt, D2D1::RectF(r.left, r.top + 1, r.right, r.bottom + 2), 10, D2D1::ColorF(0, 0, 0, 0.25f));
	fillRound(rt, r, 10, c);
	ID2D1SolidColorBrush* white = nullptr;
	if (SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &white))) {
		rt->DrawTextLayout(D2D1::Point2F(r.left + 7, r.top), layout, white);
		white->Release();
	}
	layout->Release();
}

bool Canvas::showWireTagNow() {
	CLDocument* doc = win->document();
	const int p = page();
	const double w = width(), h = height();
	if (doc == nullptr || p < 0 || w < 2 || h < 2) return false;
	// Every few points across the view, until a wire carrying a 1 (or the
	// first wire, if none does).
	double firstX = -1, firstY = -1;
	for (double vy = 40; vy < h - 40; vy += 3) {
		for (double vx = 40; vx < w - 40; vx += 3) {
			double wx, wy;
			worldPoint(vx, vy, wx, wy);
			cl_edit_hover_wire(doc, p, wx, wy, upp);
			char buf[72] = "";
			if (cl_edit_hover_wire_state(doc, p, buf, sizeof buf) <= 0) continue;
			if (firstX < 0) { firstX = vx; firstY = vy; }
			if (strchr(buf, '1') == nullptr) continue;
			firstX = vx;
			firstY = vy;
			vy = h;
			break;
		}
	}
	if (firstX < 0) { cl_edit_hover_clear(doc); return false; }
	double wx, wy;
	worldPoint(firstX, firstY, wx, wy);
	cl_edit_hover_wire(doc, p, wx, wy, upp);
	lastX = firstX;
	lastY = firstY;
	tagShown = true;
	redraw();
	return true;
}

// ---- Overlays ------------------------------------------------------------------

void Canvas::drawOverlays(ID2D1RenderTarget* rt, float w, float h) {
	hits.clear();
	sliderTrack = D2D1::RectF(0, 0, 0, 0);
	// In a split, Simulation View's bar sits under the first side, and the
	// banner is the side's you're working in.
	const bool working = win->currentCanvas() == this;
	if (win->simView()) { if (win->paneOf(this) == 0) drawSimBar(rt, w, h); }
	else if (working && !win->drawing()) drawBanner(rt, w);
	// The drawing tools, above Simulation View's bar when it's up.
	if (working && win->drawing()) drawDrawingBar(rt, w, h);
	drawWireTag(rt, w, h);   // over the bars, as it's beside the pointer
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
	// A narrow canvas (the notes open, a split view): the speed's track
	// shortens, then its "ms / step" goes, then the SIMULATION / LIVE words
	// (the light stays), so nothing runs into Done.
	const float buttonsW = (prefs().showStepClock ? 3 : 2) * 40.0f + 6 + 15;
	const float wordsW = 84, msW = 98, doneRoom = 70 + 18 + 14;
	float trackW = 150;
	bool showMs = true, showWords = true;
	float need = 18 + 28 + wordsW + 15 + buttonsW + trackW + 12 + 15 + msW + doneRoom;
	if (need > barW) { const float cut = std::min(trackW - 70, need - barW); trackW -= cut; need -= cut; }
	if (need > barW) { showMs = false; need -= msW; }
	if (need > barW) { showWords = false; need -= wordsW; }
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
	if (showWords) {
		drawText(rt, "SIMULATION", D2D1::RectF(x, cy - 15, x + 80, cy - 3), 9, dim);
		drawText(rt, paused ? "PAUSED" : "LIVE", D2D1::RectF(x, cy - 3, x + 80, cy + 13), 13, paused ? amber : ink, TextAlign::Leading, true);
		x += 70 + 14;
	}
	auto divider = [&] { fillRect(rt, D2D1::RectF(x, cy - 14, x + 1, cy + 14), D2D1::ColorF(1, 1, 1, 0.08f)); x += 1 + 14; };
	divider();
	auto button = [&](wchar_t glyph, bool lit, D2D1_COLOR_F color, int command, bool enabled = true) {
		const D2D1_RECT_F r = D2D1::RectF(x, cy - 16, x + 32, cy + 16);
		const bool hot = enabled && hotHit == (int)hits.size();
		fillRound(rt, r, 9, lit ? withAlpha(on, 0.18f) : D2D1::ColorF(1, 1, 1, hot ? 0.12f : 0.07f));
		strokeRound(rt, r, 9, D2D1::ColorF(1, 1, 1, 0.08f));
		drawIcon(rt, glyph, r, 13, enabled ? color : withAlpha(color, 0.3f));
		if (enabled) hits.push_back({ r, command });
		x += 32 + 8;
	};
	button(paused ? Icon::Play : Icon::Pause, paused, paused ? on : ink, CMD_RUNNING);
	button(Icon::Step, false, ink, CMD_STEP);
	// Step Clock beside Step, off until the page has a clock set to move only on it.
	if (prefs().showStepClock) button(Icon::StepClock, false, ink, CMD_STEP_CLOCK, win->hasManualClock());
	x += 6;
	divider();
	// Speed, fast on the right.
	drawText(rt, "SPEED", D2D1::RectF(x, cy - 18, x + 60, cy - 6), 9, dim);
	const float ty = cy + 5;
	sliderTrack = D2D1::RectF(x, ty - 10, x + trackW, ty + 10);
	const float f = (float)speedFraction(cl_document_step_ms(win->document()));
	fillRound(rt, D2D1::RectF(x, ty - 2, x + trackW, ty + 2), 2, D2D1::ColorF(1, 1, 1, 0.12f));
	fillRound(rt, D2D1::RectF(x, ty - 2, x + std::max(4.0f, trackW * f), ty + 2), 2, withAlpha(on, 0.75f));
	fillCircle(rt, D2D1::Point2F(x + trackW * f, ty), 10, withAlpha(on, 0.14f));
	fillCircle(rt, D2D1::Point2F(x + trackW * f, ty), 6, D2D1::ColorF(0.92f, 1, 1));
	x += trackW + 12;
	if (showMs) {
		drawText(rt, strf("%d ms / step", cl_document_step_ms(win->document())), D2D1::RectF(x, ty - 8, x + 84, ty + 8), 11, ink);
		x += 84 + 14;
	}
	divider();

	// The Done button at the right end; the chips in what's left.
	const float doneW = 70;
	const D2D1_RECT_F done = D2D1::RectF(x0 + barW - 18 - doneW, cy - 15, x0 + barW - 18, cy + 15);
	const bool doneHot = hotHit == (int)hits.size();
	fillRound(rt, done, 9, D2D1::ColorF(1, 1, 1, doneHot ? 0.12f : 0.07f));
	strokeRound(rt, done, 9, D2D1::ColorF(1, 1, 1, 0.10f));
	drawText(rt, "Done", D2D1::RectF(done.left + 13, done.top, done.left + 46, done.bottom), 12, ink);
	drawText(rt, "Esc", D2D1::RectF(done.left + 44, done.top + 1, done.right, done.bottom), 9, dim);
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

// The drawing tools while Draw is on (the Mac's DrawingBar): pen,
// highlighter, eraser; the tool's colours and its three widths; Clear;
// Show or Hide; Done. A capsule over the bottom of the canvas.
void Canvas::drawDrawingBar(ID2D1RenderTarget* rt, float w, float h) {
	const bool dark = prefs().dark || win->simView();
	const Chrome c{ dark };
	const D2D1_COLOR_F ink = c.barInk(), accent = d2dColor(accentColor(dark));
	const D2D1_COLOR_F dim = withAlpha(ink, 0.55f);
	const std::string blocked = win->inkBlockedReason();
	const bool readOnly = win->pageInkReadOnly();
	const int tool = ink::tool();
	const bool hl = ink::drawsWithHighlighter();
	const std::vector<std::string>& colors = ink::colors();
	const float btnW = 28, btnH = 26, colorW = 26, widthW = 30, gap = 9;
	const char* eraserText = "Erases whole marks";
	const float doneW = textWidth("Done", 12, true) + 16;
	float rowW = 3 * btnW + gap;
	if (tool == ink::Eraser) rowW += textWidth(eraserText, 11) + 12;
	else rowW += colorW * colors.size() + gap + textWidth("Width", 11, true) + 6 + 3 * widthW;
	rowW += gap + 2 * btnW + doneW;
	const float padX = 10, rowH = 38, noteH = blocked.empty() ? 0.0f : 18.0f;
	const float barW = std::max(rowW, blocked.empty() ? 0.0f : textWidth(blocked, 11) + 8) + 2 * padX;
	if (barW > w - 16) return;
	const float barH = rowH + noteH;
	const float lift = win->simView() && win->paneOf(this) == 0 ? 14 + 52 + 10 : 14;
	const float x0 = std::floor((w - barW) / 2), y0 = h - lift - barH;
	const D2D1_RECT_F pill = D2D1::RectF(x0, y0, x0 + barW, y0 + barH);
	const float radius = blocked.empty() ? barH / 2 : 16;
	fillRound(rt, D2D1::RectF(pill.left, pill.top + 2, pill.right, pill.bottom + 3), radius, D2D1::ColorF(0, 0, 0, dark ? 0.35f : 0.12f));
	fillRound(rt, pill, radius, dark ? rgb255(40, 43, 50, 0.97f) : D2D1::ColorF(1, 1, 1, 0.97f));
	strokeRound(rt, pill, radius, withAlpha(ink, dark ? 0.16f : 0.12f));
	if (!blocked.empty()) drawText(rt, blocked, D2D1::RectF(pill.left + padX, y0 + 6, pill.right - padX, y0 + 6 + 14), 11, dim, TextAlign::Center);
	const float cy = y0 + noteH + rowH / 2;
	float x = x0 + padX + (barW - 2 * padX - rowW) / 2;
	auto divider = [&] { fillRect(rt, D2D1::RectF(x + 4, cy - 9, x + 5, cy + 9), withAlpha(ink, 0.18f)); x += gap; };
	auto hot = [&]() { return hotHit == (int)hits.size(); };
	// A tool: on in the accent.
	const wchar_t toolGlyph[3] = { 0xE76D, 0xE7E6, 0xE75C };   // InkingTool, Highlight, EraseTool
	for (int t = 0; t < 3; t++) {
		const D2D1_RECT_F r = D2D1::RectF(x, cy - btnH / 2, x + btnW, cy + btnH / 2);
		const bool on = tool == t;
		const bool off = readOnly || (t != ink::Eraser && !blocked.empty());
		if (on) fillRound(rt, r, 6, withAlpha(accent, 0.16f));
		else if (!off && hot()) fillRound(rt, r, 6, withAlpha(ink, 0.08f));
		drawIcon(rt, toolGlyph[t], r, 14, off ? withAlpha(ink, 0.3f) : on ? accent : withAlpha(ink, 0.8f));
		if (!off) hits.push_back({ r, CMD_INK_PEN + t });
		x += btnW;
	}
	divider();
	if (tool == ink::Eraser) {
		drawText(rt, eraserText, D2D1::RectF(x + 6, cy - 9, x + 6 + textWidth(eraserText, 11) + 2, cy + 9), 11, dim);
		x += textWidth(eraserText, 11) + 12;
	} else {
		const std::string current = ink::color();
		for (size_t i = 0; i < colors.size(); i++) {
			const D2D1_POINT_2F m = D2D1::Point2F(x + colorW / 2, cy);
			const bool on = colors[i] == current;
			if (on) {
				ID2D1SolidColorBrush* b = nullptr;
				if (SUCCEEDED(rt->CreateSolidColorBrush(accent, &b))) { rt->DrawEllipse(D2D1::Ellipse(m, 11, 11), b, 2); b->Release(); }
			} else if (hot()) {
				fillCircle(rt, m, 12, withAlpha(ink, 0.08f));
			}
			fillCircle(rt, m, 8, ink::swatch(colors[i], hl, dark));
			ID2D1SolidColorBrush* edge = nullptr;
			if (SUCCEEDED(rt->CreateSolidColorBrush(withAlpha(ink, 0.25f), &edge))) { rt->DrawEllipse(D2D1::Ellipse(m, 8, 8), edge, 0.5f); edge->Release(); }
			hits.push_back({ D2D1::RectF(x, cy - 13, x + colorW, cy + 13), CMD_INK_COLOR + (int)i });
			x += colorW;
		}
		divider();
		const float lw = textWidth("Width", 11, true);
		drawText(rt, "Width", D2D1::RectF(x + 2, cy - 9, x + 2 + lw + 2, cy + 9), 11, dim, TextAlign::Leading, true);
		x += lw + 6;
		const D2D1_COLOR_F lineColor = ink::swatch(current, hl, dark);
		const float thick[3] = { 2, 4, 7 };
		const int at = ink::widthIndex();
		for (int i = 0; i < 3; i++) {
			const D2D1_RECT_F r = D2D1::RectF(x, cy - btnH / 2, x + widthW, cy + btnH / 2);
			const bool on = at == i;
			if (on) {
				fillRound(rt, r, 6, withAlpha(accent, 0.16f));
				strokeRound(rt, D2D1::RectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f), 6, withAlpha(accent, 0.7f));
			} else if (hot()) {
				fillRound(rt, r, 6, withAlpha(ink, 0.08f));
			}
			const float mx = (r.left + r.right) / 2;
			fillRound(rt, D2D1::RectF(mx - 9, cy - thick[i] / 2, mx + 9, cy + thick[i] / 2), thick[i] / 2, withAlpha(lineColor, on ? 1.0f : 0.6f));
			hits.push_back({ r, CMD_INK_WIDTH + i });
			x += widthW;
		}
	}
	divider();
	auto iconButton = [&](wchar_t glyph, int command, bool enabled) {
		const D2D1_RECT_F r = D2D1::RectF(x, cy - btnH / 2, x + btnW, cy + btnH / 2);
		if (enabled && hot()) fillRound(rt, r, 6, withAlpha(ink, 0.08f));
		drawIcon(rt, glyph, r, 13, withAlpha(ink, enabled ? 0.8f : 0.3f));
		if (enabled) hits.push_back({ r, command });
		x += btnW;
	};
	iconButton(0xE74D, CMD_CLEAR_DRAWING, win->pageHasInk() && !readOnly);   // Delete
	iconButton(win->drawingShown() ? 0xE890 : 0xED1A, CMD_SHOW_DRAWING, true);   // View, Hide
	const D2D1_RECT_F done = D2D1::RectF(x, cy - btnH / 2, x + doneW, cy + btnH / 2);
	if (hot()) fillRound(rt, done, 6, withAlpha(ink, 0.08f));
	drawText(rt, "Done", done, 12, ink, TextAlign::Center, true);
	hits.push_back({ done, CMD_DRAW });
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

void Canvas::setCamera(double ox, double oy, double unitsPerPoint) {
	zooming = false;
	needsFit = false;
	originX = ox;
	originY = oy;
	upp = unitsPerPoint;
	redraw();
	win->statusNeedsUpdate();
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
	if (draglog::on() && (drag != Drag::None || win->isFloating()) && (w != lastW || h != lastH))
		draglog::note(strf("WM_SIZE mid-drag: the canvas went from %.1fx%.1f to %.1fx%.1f points; the view shifts by half that", lastW, lastH, w, h));
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
		// While something follows the pointer: one frame a display refresh.
		// Windows also paints whenever the queue empties between two moves
		// -- after nearly every move, with draws this quick -- so a drag drew
		// some 330 frames a second on a 60 Hz screen, four in five of them
		// never shown (windows/PC-TEST.md). One that comes sooner than the
		// screen could show it is put off: the next move's paceFrame draws
		// it at the refresh (as before, so moves still can't starve the
		// paint), or, if the pointer has stopped, the timer does.
		if (pacingFrames() && nowSeconds() - lastPaintAt < frameSeconds * 0.9) {
			ValidateRect(hwnd, nullptr);
			if (!frameOwed) {
				frameOwed = true;
				const double wait = frameSeconds - (nowSeconds() - lastPaintAt);
				SetTimer(hwnd, kOwedFrameTimer, (UINT)std::max(1.0, std::ceil(wait * 1000)), nullptr);
			}
			return 0;
		}
		if (frameOwed) { KillTimer(hwnd, kOwedFrameTimer); frameOwed = false; }
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
		gMoveLp = lp;   // for the drag log
		gMoveHasLp = true;
		onMotion(vx, vy);
		gMoveHasLp = false;
		return 0;
	case WM_MOUSELEAVE:
		pointerInside = false;
		hideWireTag();
		if (drag == Drag::None && win->drawing()) { cl_ink_hover_clear(win->document()); InvalidateRect(hwnd, nullptr, FALSE); }
		if (drag == Drag::None && cl_edit_hover_clear(win->document())) redraw();
		return 0;
	case WM_TIMER:
		if (wp == kOwedFrameTimer) {
			// The frame put off below: due now (the pointer stopped, so no
			// move will ask for it).
			KillTimer(hwnd, kOwedFrameTimer);
			frameOwed = false;
			InvalidateRect(hwnd, nullptr, FALSE);
		}
		if (wp == kTagTimer) {
			KillTimer(hwnd, kTagTimer);
			tagWaiting = false;
			std::string text;
			char state = 0;
			if (pointerInside && wireTagText(text, state)) { tagShown = true; InvalidateRect(hwnd, nullptr, FALSE); }
		}
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
		if ((HWND)lp != hwnd && drag != Drag::None) {
			if (draglog::on()) draglog::note(strf("WM_CAPTURECHANGED: the pointer went to window %p mid-drag; the drag ends", (void*)lp));
			cancelDrag();
		}
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
	case WM_POINTERDOWN:
	case WM_POINTERUPDATE:
	case WM_POINTERUP:
		if (onPointer(msg, wp, lp)) return 0;
		break;
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
		cl_edit_release_ex(win->document(), wx, wy, false);   // a gesture cut short: no clock steps
		win->edited();
	}
	if (was == Drag::Ink) {
		// What was drawn so far is kept (one step), as a release would.
		if (inkErasing) { if (cl_ink_erase_end(win->document()) > 0) win->inkEdited(); }
		else if (cl_ink_end(win->document()) > 0) win->inkEdited();
		inkErasing = false;
	}
	if (GetCapture() == hwnd) ReleaseCapture();
	setCursor(win->drawing() ? IDC_CROSS : nullptr);
	redraw();
}

bool Canvas::moving() const { return drag == Drag::Edit || win->isFloating(); }

void Canvas::onPress(int button, double vx, double vy, bool doubleClick, WPARAM keys) {
	CLDocument* doc = win->document();
	const int p = page();
	if (doc == nullptr || p < 0) return;
	SetFocus(hwnd);
	hideWireTag();
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
		if (win->drawing()) { POINT screen; GetCursorPos(&screen); inkMenu(screen); return; }
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
	// Draw mode: the pointer draws (Space or Ctrl held moves around, as ever).
	if (win->drawing() && !spaceDown && !(keys & MK_CONTROL)) { inkPress(vx, vy); return; }

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
			if (win->simView() && win->predictTap(wx, wy)) return;   // a guess on a covered light
			if (cl_document_click(doc, p, wx, wy)) {
				win->advancedByHand();   // a switch, a keypad, or a clock set to "Only on Step Clock"
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
		noteFrameRate();
		if (draglog::on()) draglog::press(draglog::View{ hwnd, doc, p, originX, originY, upp }, vx, vy, wx, wy);
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
	if (draglog::on())
		draglog::note(strf("WM_LBUTTONDBLCLK at %.1f,%.1f: the press is taken as a double-click; the drag ends here (%s)", vx, vy,
		                   cl_edit_single_gate(doc, p) >= 0 ? "gate settings open" : "zoom to fit, animated"));
	if (was == Drag::Edit) cl_edit_release_ex(doc, wx, wy, false);   // the first click already stepped a clock
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
		if (h >= 0 || inRect(sliderTrack, (float)vx, (float)vy)) {
			if (win->drawing()) { cl_ink_hover_clear(doc); InvalidateRect(hwnd, nullptr, FALSE); }
			setCursor(IDC_HAND);
			return;
		}
		if (cursorNow == IDC_HAND && !spaceDown) setCursor(win->drawing() ? IDC_CROSS : nullptr);
	}
	switch (drag) {
	case Drag::Slider:
		setSpeedAt(vx);
		break;
	case Drag::Pan:
		pan(dx, dy);
		paceFrame();
		break;
	case Drag::Ink:
		inkMotion(vx, vy);
		break;
	case Drag::Edit: {
		const double t0 = draglog::on() ? nowSeconds() : 0;
		cl_edit_drag(doc, wx, wy);
		if (draglog::on())
			draglog::motion(draglog::View{ hwnd, doc, p, originX, originY, upp }, "edit", gMoveLp, gMoveHasLp, vx, vy, wx, wy,
			                (nowSeconds() - t0) * 1000);
		redrawWhileMoving();
		break;
	}
	case Drag::None: {
		if (win->drawing()) { if (!spaceDown) inkHover(vx, vy); break; }
		if (win->hasPendingGate() && win->placePendingGate(wx, wy)) { redraw(); break; }
		// Gates on the pointer float on their own side: the other one only
		// watches them go by.
		const bool floating = win->isFloating();
		if (floating && this != win->currentCanvas()) { hideWireTag(); break; }
		// Simulation View lights the whole wire under the pointer.
		const double t0 = draglog::on() ? nowSeconds() : 0;
		const bool changed = win->simView() ? cl_edit_hover_wire(doc, p, wx, wy, upp) : cl_edit_hover(doc, p, wx, wy, upp);
		if (floating && draglog::on())
			draglog::motion(draglog::View{ hwnd, doc, p, originX, originY, upp }, "float", gMoveLp, gMoveHasLp, vx, vy, wx, wy,
			                (nowSeconds() - t0) * 1000);
		if (changed) { if (floating) redrawWhileMoving(); else redraw(); }
		updateWireTag();
		break;
	}
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
		// A click (no drag) on a clock set to "Only on Step Clock" steps it.
		const bool stepped = cl_edit_release_ex(doc, wx, wy, true) == CL_CLICK_CLOCK_STEP;
		if (draglog::on()) draglog::release(draglog::View{ hwnd, doc, page(), originX, originY, upp }, wx, wy);
		win->edited();
		win->noteLockedHeld();   // a drag that left locked parts behind says why
		if (stepped) win->advancedByHand();
	} else if (drag == Drag::Ink && button == 1) {
		inkRelease(vx, vy);
		return;
	} else if (drag == Drag::Slider && button == 1) {
		drag = Drag::None;
		if (GetCapture() == hwnd) ReleaseCapture();
	} else if (drag == Drag::Pan && (button == 1 || button == 2)) {
		drag = Drag::None;
		if (GetCapture() == hwnd) ReleaseCapture();
	}
	setCursor(spaceDown ? IDC_HAND : win->drawing() ? IDC_CROSS : nullptr);
	redraw();
}

// Per device (Settings > Canvas): a wheel mouse zooms and a touchpad moves
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

// ---- Drawing on the circuit (Ink.h) -----------------------------------------

void Canvas::drawingChanged() {
	CLDocument* doc = win->document();
	if (!win->drawing()) {
		if (drag == Drag::Ink) {
			cl_ink_cancel(doc);
			if (inkErasing) cl_ink_erase_end(doc);
			inkErasing = false;
			drag = Drag::None;
			if (GetCapture() == hwnd) ReleaseCapture();
		}
		tabletEraser = false;
		setCursor(nullptr);
	} else {
		if (drag == Drag::Edit) cancelDrag();
		hideWireTag();
		if (cl_edit_hover_clear(doc)) redraw();
		setCursor(IDC_CROSS);
		inkToolChanged();
	}
	InvalidateRect(hwnd, nullptr, FALSE);
}

void Canvas::inkToolChanged() {
	if (!win->drawing() || drag != Drag::None || !pointerInside) return;
	POINT p;
	GetCursorPos(&p);
	double vx, vy;
	screenToView(p, vx, vy);
	inkHover(vx, vy);
}

// The eraser tool, or a pen's eraser end.
bool Canvas::erases() const { return ink::tool() == ink::Eraser || tabletEraser; }

void Canvas::inkSample(double vx, double vy) {
	double wx, wy;
	worldPoint(vx, vy, wx, wy);
	CLInkPoint pt = { wx, wy, penPressure };
	// Cut at the cap: what's drawn so far is kept.
	if (cl_ink_add(win->document(), &pt, 1) == CL_INK_FULL) win->note(ink::fullNote());
}

bool Canvas::inkPress(double vx, double vy) {
	CLDocument* doc = win->document();
	const int p = page();
	if (doc == nullptr || p < 0) return false;
	SetFocus(hwnd);
	hideWireTag();
	zooming = false;
	lastX = vx;
	lastY = vy;
	cl_ink_hover_clear(doc);
	double wx, wy;
	worldPoint(vx, vy, wx, wy);
	if (erases()) {
		if (cl_ink_page_read_only(doc, p)) { win->note(ink::readOnlyNote()); return true; }
		cl_ink_erase_begin(doc, p);
		inkErasing = true;
		cl_ink_erase_to(doc, wx, wy, eraserRadius());
	} else {
		const int r = cl_ink_begin(doc, p, ink::drawsWithHighlighter() ? CL_INK_HIGHLIGHTER : CL_INK_PEN, ink::color().c_str(), ink::width(), upp);
		if (r != CL_INK_OK) {
			win->note(r == CL_INK_FULL ? ink::fullNote() : ink::readOnlyNote());
			MessageBeep(MB_OK);
			return true;
		}
		inkErasing = false;
		inkSample(vx, vy);
	}
	drag = Drag::Ink;
	noteFrameRate();
	SetCapture(hwnd);
	InvalidateRect(hwnd, nullptr, FALSE);
	return true;
}

void Canvas::inkMotion(double vx, double vy) {
	CLDocument* doc = win->document();
	const int p = page();
	if (doc == nullptr || p < 0) return;
	if (inkErasing) {
		double wx, wy;
		worldPoint(vx, vy, wx, wy);
		const int before = cl_ink_stroke_count(doc, p);
		cl_ink_erase_to(doc, wx, wy, eraserRadius());
		cl_ink_hover(doc, p, wx, wy, CL_INK_ERASER, "ink", eraserRadius());
		if (cl_ink_stroke_count(doc, p) != before) win->redrawMiniMap();
	} else {
		inkSample(vx, vy);
	}
	redrawWhileMoving();
}

void Canvas::inkRelease(double vx, double vy) {
	CLDocument* doc = win->document();
	const int p = page();
	drag = Drag::None;
	if (GetCapture() == hwnd) ReleaseCapture();
	if (doc == nullptr) return;
	if (inkErasing) {
		inkErasing = false;
		if (cl_ink_erase_end(doc) > 0) win->inkEdited();
	} else {
		inkSample(vx, vy);
		if (cl_ink_end(doc) > 0) win->inkEdited();
		if (p >= 0 && cl_ink_can_draw(doc, p) == CL_INK_FULL) win->note(ink::fullNote());
	}
	inkHover(vx, vy);
	setCursor(IDC_CROSS);
	redraw();
}

// What the pointer would draw: a dot of the stroke, or the eraser's ring.
void Canvas::inkHover(double vx, double vy) {
	CLDocument* doc = win->document();
	const int p = page();
	if (doc == nullptr || p < 0) return;
	double wx, wy;
	worldPoint(vx, vy, wx, wy);
	if (erases()) cl_ink_hover(doc, p, wx, wy, CL_INK_ERASER, "ink", eraserRadius());
	else cl_ink_hover(doc, p, wx, wy, ink::drawsWithHighlighter() ? CL_INK_HIGHLIGHTER : CL_INK_PEN, ink::color().c_str(), ink::width());
	if (cursorNow != IDC_CROSS && !spaceDown) setCursor(IDC_CROSS);
	InvalidateRect(hwnd, nullptr, FALSE);
}

// A right-click while drawing: what the Mac's offers.
void Canvas::inkMenu(POINT screen) {
	HMENU m = CreatePopupMenu();
	const bool canClear = win->pageHasInk() && !win->pageInkReadOnly();
	AppendMenuW(m, MF_STRING | (canClear ? 0 : MF_GRAYED), CMD_CLEAR_DRAWING, L"&Clear Drawing on This Page");
	AppendMenuW(m, MF_STRING, CMD_SHOW_DRAWING, L"&Hide Drawing");
	AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(m, MF_STRING, CMD_DRAW, L"&Done Drawing");
	const int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, screen.x, screen.y, 0, hwnd, nullptr);
	DestroyMenu(m);
	if (cmd) win->run(cmd);
}

// P, H and E pick a tool, 1, 2 and 3 a width; Escape ends a stroke, then Draw mode.
bool Canvas::inkKey(UINT vk) {
	CLDocument* doc = win->document();
	if (vk == VK_ESCAPE) {
		if (drag == Drag::Ink) {
			if (inkErasing) { if (cl_ink_erase_end(doc) > 0) win->inkEdited(); }
			else cl_ink_cancel(doc);
			inkErasing = false;
			drag = Drag::None;
			if (GetCapture() == hwnd) ReleaseCapture();
			redraw();
		} else {
			win->setDrawing(false);
		}
		return true;
	}
	if (!bareKey() || down(VK_SHIFT)) return false;
	if (vk >= '1' && vk <= '3') { win->setInkWidth((int)(vk - '1')); return true; }
	if (vk == 'P') { win->setInkTool(ink::Pen); return true; }
	if (vk == 'H') { win->setInkTool(ink::Highlighter); return true; }
	if (vk == 'E') { win->setInkTool(ink::Eraser); return true; }
	return false;
}

// A pen (WM_POINTER): its pressure thins the line a little, and its eraser
// end erases. Only while drawing, and only a pen: otherwise Windows makes
// the usual mouse messages from it.
bool Canvas::onPointer(UINT msg, WPARAM wp, LPARAM lp) {
	(void)lp;
	if (!win->drawing()) return false;
	const UINT32 id = GET_POINTERID_WPARAM(wp);
	POINTER_INPUT_TYPE type = PT_POINTER;
	if (!GetPointerType(id, &type) || type != PT_PEN) return false;
	POINTER_PEN_INFO info = {};
	if (!GetPointerPenInfo(id, &info)) return false;
	const bool wasEraser = tabletEraser;
	tabletEraser = (info.penFlags & (PEN_FLAG_ERASER | PEN_FLAG_INVERTED)) != 0;
	penPressure = (info.penMask & PEN_MASK_PRESSURE) ? std::min(1.0, info.pressure / 1024.0) : -1;
	double vx, vy;
	screenToView(info.pointerInfo.ptPixelLocation, vx, vy);
	if (msg == WM_POINTERDOWN) {
		if (overlayPress(vx, vy)) return true;
		inkPress(vx, vy);
	} else if (msg == WM_POINTERUPDATE) {
		lastX = vx;
		lastY = vy;
		if (drag == Drag::Ink) inkMotion(vx, vy);
		else if (drag == Drag::Slider) setSpeedAt(vx);
		else if (drag == Drag::None) { if (wasEraser != tabletEraser || !(info.pointerInfo.pointerFlags & POINTER_FLAG_INCONTACT)) inkHover(vx, vy); }
	} else {
		if (drag == Drag::Ink) inkRelease(vx, vy);
		else if (drag == Drag::Slider) { drag = Drag::None; if (GetCapture() == hwnd) ReleaseCapture(); }
		penPressure = -1;
	}
	return true;
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
	// The key Settings > Shortcuts gives one of the canvas's actions (A, R,
	// S, Shift+S, T, C, V, X, D, unless they've been changed), if any.
	const shortcuts::Action* act = shortcuts::match(shortcuts::pressed(vk), true);
	auto is = [&](const char* id) { return act && std::strcmp(act->id, id) == 0; };
	// Draw mode's own keys first: P, H, E, 1, 2, 3 and Escape.
	if (win->drawing() && inkKey(vk)) return true;

	// Simulation View: Escape leaves, Space runs and pauses, arrows move around.
	if (win->simView()) {
		if (win->predictKey(vk)) return true;   // Predict's guesses, Tab and Enter
		if (!bare && !act) return false;
		if (vk == VK_ESCAPE) win->toggleSimView();
		else if (vk == VK_SPACE) { if (!spaceDown && !repeat) { spaceDown = true; win->toggleRunning(); } }
		else if (arrow) pan(isLeft ? 40 : isRight ? -40 : 0, isUp ? 40 : (isLeft || isRight) ? 0 : -40);
		else if (vk == VK_OEM_PLUS || vk == VK_ADD) animateZoom(1 / 0.75);
		else if (vk == VK_OEM_MINUS || vk == VK_SUBTRACT) animateZoom(0.75);
		else if (is("truthTable")) win->makeTruthTable();
		else if (is("checkCircuit")) win->checkCircuit();
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

	// The canvas's actions, before anything else (the fixed keys below can't
	// be given to one).
	if (act) {
		if (is("quickCopy")) {
			// C while something is moving (dragged, or floating on the pointer):
			// connect it to the pins it's next to and keep moving. The
			// connections are kept at the drop; Escape takes back just them.
			// (Not while it's still over the side panel, as on the Mac: it
			// would land out of sight. Asked of the pointer's place: a gate
			// dragged from the panel never sends this canvas a mouse move, so
			// pointerInside stayed false and C did nothing -- only the drop's
			// own connection of pins that touch.) Within the Mac's reach, 20
			// points on screen, at any zoom.
			if (moving()) {
				if (win->canEdit() && pointerOver()) {
					const int n = cl_edit_connect_while_moving(doc, page(), upp);
					if (n > 0) win->note(strf("Connected %d pin%s. Escape takes it back.", n, n == 1 ? "" : "s"));
					else if (n == 0) win->note("Nothing close enough to connect.");
					win->redraw();
				}
				return true;
			}
			win->copy();
			return true;
		}
		if (is("truthTable")) { win->makeTruthTable(); return true; }
		if (is("checkCircuit")) { win->checkCircuit(); return true; }
		if (!win->canEdit()) { win->lockNudge(); return true; }
		if (is("quickPaste")) win->paste();
		else if (is("quickCut")) win->cut();
		else if (is("quickDuplicate")) win->duplicate();
		else if (is("addGate")) win->quickAdd();
		else if (is("rotate")) win->rotate();
		else if (is("straighten")) win->straighten();
		else if (is("tidy")) win->tidy();
		else return false;
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
