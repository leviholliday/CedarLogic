// The circuit canvas (see Canvas.h).

#include "Canvas.h"
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
		return;
	}
	const RGBA a = accentColor(dark);
	cl_edit_draw_overlay(doc, p, rt, scale, originX, originY, upp, a.r, a.g, a.b);
	double l, b, r, t, alpha;
	if (cl_edit_box(doc, &l, &b, &r, &t)) drawBox(rt, l, b, r, t, a, 1);
	else if (win->dragFadeBox(l, b, r, t, alpha) && alpha > 0) drawBox(rt, l, b, r, t, a, alpha);
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
	switch (drag) {
	case Drag::Pan:
		pan(dx, dy);
		break;
	case Drag::Edit:
		cl_edit_drag(doc, wx, wy);
		redraw();
		break;
	case Drag::None:
		if (win->hasPendingGate() && win->placePendingGate(wx, wy)) { redraw(); break; }
		if (win->simView()) break;
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
