// The Actions rail (see Rail.h).

#include "Rail.h"
#include "Chrome.h"
#include "Commands.h"
#include "Fade.h"
#include "Shortcuts.h"
#include "Window.h"

#include <dwmapi.h>

#include <algorithm>
#include <cmath>

namespace {

const wchar_t* kLabelClass = L"CedarLogicRailLabel";
const float kItemW = 34, kItemH = 32, kEdgeW = 5;
const float kLabelH = 26, kLabelPad = 9, kLabelGap = 8;

double easeOut(double t) {
	t = std::min(1.0, std::max(0.0, t));
	return 1 - (1 - t) * (1 - t) * (1 - t);
}

void registerLabelClass() {
	static bool done = false;
	if (done) return;
	done = true;
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof wc;
	wc.style = CS_DROPSHADOW;
	wc.lpfnWndProc = DefWindowProcW;
	wc.hInstance = appInstance();
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.lpszClassName = kLabelClass;
	RegisterClassExW(&wc);
}

}  // namespace

ActionsRail::Picture ActionsRail::picture;
bool ActionsRail::watchPointer = true;
bool ActionsRail::quiet = false;

ActionsRail::ActionsRail(CircuitWindow* window, HWND parent) : win(window) {
	create(parent, WS_CHILD | WS_CLIPSIBLINGS);
	// The one-time status line for someone who had CedarLogic before the
	// rail: after the launch (and any welcome or What's New over it).
	if (prefs().actionsHint && !quiet) hintAt = nowSeconds() + 4;
}

ActionsRail::~ActionsRail() {
	labelSurface.release();
	if (label) DestroyWindow(label);
}

// ---- State -------------------------------------------------------------------------

float ActionsRail::reserve() const {
	if (!onScreen()) return 0;
	return folded() ? rail::kTabWidth : rail::kWidth;
}

double ActionsRail::slideAmount() const {
	if (picture.slidOut) return 1;
	if (slideAt < 0) return slideTo;
	return slideFrom + (slideTo - slideFrom) * easeOut((nowSeconds() - slideAt) / rail::kSlideTime);
}

float ActionsRail::widgetWidth() const {
	if (!onScreen()) return 0;
	if (!folded()) return rail::kWidth;
	const float base = hov.lit() || picture.peek ? rail::kPeekWidth : rail::kTabWidth;
	return std::max(base, (float)(rail::kWidth * slideAmount()));
}

void ActionsRail::startSlide(bool out, double now) {
	slideFrom = slideAmount();
	slideTo = out ? 1 : 0;
	slideAt = fade::animations() ? now : -1;
	if (!out) {
		hideLabel();
		hotItem = -1;
		edgeHot = false;
	}
	win->placeRail();
	redraw();
}

void ActionsRail::setNarrow(bool on) {
	// Wide again: a rail opened by its tab in the narrow window goes back to
	// how it was left.
	if (!on && narrow && peek && !focus) peek = false;
	narrow = on;
}

void ActionsRail::setFocus(bool on) {
	if (on == focus) return;
	// Into focus mode: folded. Out of it: as it was before.
	if (on) {
		peekBeforeFocus = peek;
		peek = false;
	} else {
		peek = peekBeforeFocus;
	}
	focus = on;
	if (!folded()) { hov = rail::Hover(); slideTo = 0; slideAt = -1; }
	redraw();
}

void ActionsRail::collapsedChanged(bool animate) {
	if (animate) fade::crossfade(win->window(), nullptr, 190);
	if (!folded()) {
		hov.update(nowSeconds(), false, false, false, prefs().actionsHoverOpen);
		slideTo = 0;
		slideAt = -1;
	}
	hideLabel();
	actions::changed();
}

// In focus mode these change only peek, so leaving it gives back the saved state.
void ActionsRail::fold() {
	peek = false;
	if (!focus) prefs().actionsCollapsed = true;
	collapsedChanged(true);
}

void ActionsRail::unfold() {
	if (focus) {
		peek = true;
	} else {
		prefs().actionsCollapsed = false;
		if (narrow) peek = true;
	}
	collapsedChanged(true);
}

void ActionsRail::toggle() {
	if (hov.slidOut) {
		hov.dismissed = hov.overStrip || hov.overRail;
		hov.slide(false);
		startSlide(false, nowSeconds());
	} else if (folded()) {
		unfold();
	} else {
		fold();
	}
}

bool ActionsRail::escape() {
	if (!hov.slidOut || dragging || hov.menuOpen || !IsWindowEnabled(win->window())) return false;
	hov.dismissed = true;
	hov.slide(false);
	startSlide(false, nowSeconds());
	return true;
}

void ActionsRail::refresh() {
	if (IsWindowVisible(hwnd)) redraw();
}

void ActionsRail::pointerForTest(double now, bool strip, bool over) {
	// (The slide itself goes by the real clock, as the window's ticks draw it.)
	if (hov.update(now, strip, over, onScreen() && folded(), prefs().actionsHoverOpen)) startSlide(hov.slidOut, nowSeconds());
	if (hov.lit() != wasLit) { wasLit = hov.lit(); win->placeRail(); redraw(); }
}

void ActionsRail::tick(double now) {
	if (!onScreen()) return;
	const bool isFolded = folded();
	bool strip = hov.overStrip, over = hov.overRail;
	if (watchPointer) {
		strip = over = false;
		if (isFolded && IsWindowVisible(win->window())) {
			POINT p;
			GetCursorPos(&p);
			const HWND under = WindowFromPoint(p);
			DWORD pid = 0;
			GetWindowThreadProcessId(GetForegroundWindow(), &pid);
			const bool ours = under && (GetAncestor(under, GA_ROOT) == win->window() || under == label);
			if (ours && pid == GetCurrentProcessId() && IsWindowEnabled(win->window())) {
				POINT c = p;
				ScreenToClient(win->window(), &c);
				RECT rc;
				GetClientRect(win->window(), &rc);
				strip = rail::inHotStrip(rc, bandTop, bandBottom, c, scale());
				RECT wr;
				GetWindowRect(hwnd, &wr);
				over = hov.slidOut && PtInRect(&wr, p);
			}
		}
		if (hov.update(now, strip, over, isFolded, prefs().actionsHoverOpen)) startSlide(hov.slidOut, now);
		if (hov.lit() != wasLit) {
			wasLit = hov.lit();
			win->placeRail();
			redraw();
		}
	}
	// The slide, a frame at a time.
	if (slideAt >= 0) {
		if (now - slideAt >= rail::kSlideTime) slideAt = -1;
		win->placeRail();
		redraw();
	}
	// CI's pictures: an item's label showing.
	if (!picture.label.empty() && labelId != picture.label && IsWindowVisible(hwnd) && IsWindowVisible(win->window()) &&
	    (!folded() || slideAmount() >= 1))
		showLabel(picture.label);
	// Held without moving: picked up (any movement now drags it).
	if (pressed >= 0 && pressZone == 1 && !dragging && !held && now - pressAt >= rail::kHoldTime) {
		held = true;
		hideLabel();
		redraw();
	}
	// The status line, once, when nothing else is over the window.
	if (hintAt >= 0 && now >= hintAt) {
		if (!prefs().actionsHint) {
			hintAt = -1;
		} else if ((!IsWindowVisible(win->window()) || !IsWindowEnabled(win->window())) && hintTries < 60) {
			hintTries++;
			hintAt = now + 2;
		} else {
			hintAt = -1;
			prefs().actionsHint = false;
			prefs().save();
			win->note("New: an Actions rail. Settings ▸ Actions.");
		}
	}
}

// ---- Geometry (points, in the rail's window) ----------------------------------------

float ActionsRail::contentLeft(float w) const { return folded() ? w - rail::kWidth : 0; }

std::vector<std::string> ActionsRail::items() const { return actions::rail(); }

int ActionsRail::itemAt(float x, float y) const {
	if (folded() && slideAmount() <= 0.001) return -1;
	const float left = contentLeft((float)width());
	const int n = (int)items().size();
	for (int i = 0; i < n; i++) {
		const float top = rail::kItemTop + i * rail::kPitch;
		if (x >= left + 6 && x < left + 6 + kItemW && y >= top && y < top + kItemH) return i;
	}
	return -1;
}

bool ActionsRail::onEdge(float x, float) const {
	if (folded() && slideAmount() <= 0.001) return false;
	const float left = contentLeft((float)width());
	return x >= left && x < left + kEdgeW;
}

// Where the one being dragged would go if let go now.
int ActionsRail::target(int count) const {
	int from = dragFrom;
	float dy = dragDy;
	if (!dragging && !picture.dragId.empty()) {
		const std::vector<std::string> ids = items();
		from = (int)(std::find(ids.begin(), ids.end(), picture.dragId) - ids.begin());
		dy = picture.dragDy;
	}
	return std::min(std::max(from + (int)std::lround(dy / rail::kPitch), 0), std::max(count - 1, 0));
}

// How far an item is drawn from its place: the dragged one under the
// pointer (no further than the ends), the others out of its way.
float ActionsRail::shift(int i, int count) const {
	const bool drag = dragging || !picture.dragId.empty();
	if (!drag) return 0;
	int from = dragFrom;
	float dy = dragDy;
	if (!dragging) {
		const std::vector<std::string> ids = items();
		from = (int)(std::find(ids.begin(), ids.end(), picture.dragId) - ids.begin());
		dy = picture.dragDy;
	}
	if (i == from) return std::min(std::max(dy, -from * rail::kPitch - 4), (count - 1 - from) * rail::kPitch + 4);
	const int to = target(count);
	if (from < to && i > from && i <= to) return -rail::kPitch;
	if (to < from && i >= to && i < from) return rail::kPitch;
	return 0;
}

bool ActionsRail::enabled(const actions::Def& d) const { return actions::enabled(win, d); }
bool ActionsRail::isOn(const actions::Def& d) const { return actions::isOn(win, d); }

// ---- Drawing ----------------------------------------------------------------------

void ActionsRail::paint(ID2D1RenderTarget* rt, float w, float h) {
	updateTips();
	const bool f = folded();
	const double amount = slideAmount();
	if (f && amount <= 0.001) {
		paintTab(rt, w, h);
		return;
	}
	if (f) fillRect(rt, D2D1::RectF(0, 0, w, h), chrome().canvas());
	paintRail(rt, contentLeft(w), h, f);
}

// Folded: a tab the height of the window. Lit (the pointer at the edge),
// the accent and a little wider; in focus mode the canvas's own colour, its
// grip fainter, until the pointer comes.
void ActionsRail::paintTab(ID2D1RenderTarget* rt, float w, float h) {
	const Chrome c = chrome();
	const D2D1_COLOR_F accent = c.accent();
	const bool lit = hov.lit() || picture.peek;
	fillRect(rt, D2D1::RectF(0, 0, w, h), lit ? withAlpha(accent, 0.35f) : focus ? c.canvas() : c.tabBar());
	if (lit) fillRect(rt, D2D1::RectF(0, 0, 1, h), withAlpha(accent, 0.7f));
	else if (!focus) fillRect(rt, D2D1::RectF(0, 0, 1, h), c.sash());
	const float gw = lit ? 3.0f : 2.0f, gh = lit ? 40.0f : 32.0f;
	const float cx = (w + (lit ? 0.5f : 1.0f)) / 2, cy = h / 2;
	fillRound(rt, D2D1::RectF(cx - gw / 2, cy - gh / 2, cx + gw / 2, cy + gh / 2), gw / 2,
	          lit ? accent : withAlpha(c.barInk(), focus ? 0.22f : 0.45f));
}

void ActionsRail::paintItem(ID2D1RenderTarget* rt, const actions::Def& d, const D2D1_RECT_F& r, bool lit, bool lifted) {
	const Chrome c = chrome();
	const D2D1_COLOR_F accent = c.accent();
	const bool en = enabled(d), on = isOn(d);
	D2D1_MATRIX_3X2_F base;
	rt->GetTransform(&base);
	if (lifted) {
		const D2D1_POINT_2F mid = D2D1::Point2F((r.left + r.right) / 2, (r.top + r.bottom) / 2);
		rt->SetTransform(D2D1::Matrix3x2F::Scale(1.08f, 1.08f, mid) * base);
		fillRound(rt, D2D1::RectF(r.left + 1, r.top + 3, r.right - 1, r.bottom + 3), 6, D2D1::ColorF(0, 0, 0, c.dark ? 0.35f : 0.12f));
		fillRound(rt, r, 6, c.tabCard());
	}
	if (on) fillRound(rt, r, 6, withAlpha(accent, lit ? 0.24f : 0.18f));
	else if (lit) fillRound(rt, r, 6, withAlpha(accent, 0.10f));
	if (lit) strokeRound(rt, D2D1::RectF(r.left + 0.75f, r.top + 0.75f, r.right - 0.75f, r.bottom - 0.75f), 6, withAlpha(accent, 0.9f), 1.5f);
	const D2D1_COLOR_F fg = on ? accent : withAlpha(c.barInk(), en ? 0.92f : 0.3f);
	if (d.command == CMD_NEW) drawNewDocIcon(rt, r, fg);
	else if (d.command == CMD_NEW_TAB) drawNewTabIcon(rt, r, fg);
	else if (d.command == CMD_OPEN) drawFolderIcon(rt, r, fg);
	else drawIcon(rt, d.glyph, r, 15, fg);
	rt->SetTransform(base);
}

void ActionsRail::paintRail(ID2D1RenderTarget* rt, float left, float h, bool slid) {
	const Chrome c = chrome();
	const D2D1_COLOR_F accent = c.accent();
	fillRect(rt, D2D1::RectF(left, 0, left + rail::kWidth, h), c.canvas());
	const std::vector<std::string> ids = items();
	const int n = (int)ids.size();
	const bool drag = dragging || (!picture.dragId.empty() && std::find(ids.begin(), ids.end(), picture.dragId) != ids.end());
	const std::string& dragged = dragging ? dragId : picture.dragId;
	// Where the dragged one will go: a dashed space the others open up.
	if (drag) {
		const float top = rail::kItemTop + target(n) * rail::kPitch;
		const D2D1_RECT_F slot = D2D1::RectF(left + 6.75f, top + 0.75f, left + 6 + kItemW - 0.75f, top + kItemH - 0.75f);
		fillRound(rt, slot, 6, withAlpha(accent, 0.07f));
		ID2D1Factory* f = nullptr;
		rt->GetFactory(&f);
		ID2D1StrokeStyle* dashed = nullptr;
		const float dashes[] = { 2, 2 };
		if (f) f->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN_ROUND,
		                                                         10, D2D1_DASH_STYLE_CUSTOM, 0), dashes, 2, &dashed);
		ID2D1SolidColorBrush* b = nullptr;
		if (SUCCEEDED(rt->CreateSolidColorBrush(withAlpha(accent, 0.6f), &b))) {
			rt->DrawRoundedRectangle(D2D1::RoundedRect(slot, 6, 6), b, 1.5f, dashed);
			b->Release();
		}
		if (dashed) dashed->Release();
		if (f) f->Release();
	}
	int liftedAt = -1;
	for (int i = 0; i < n; i++) {
		const actions::Def* d = actions::find(ids[i]);
		if (!d) continue;
		const bool isDragged = drag && ids[i] == dragged;
		if (isDragged) { liftedAt = i; continue; }   // drawn last, over the others
		const float top = rail::kItemTop + i * rail::kPitch + shift(i, n);
		const bool lifted = held && i == pressed;
		paintItem(rt, *d, D2D1::RectF(left + 6, top, left + 6 + kItemW, top + kItemH), (!drag && hotItem == i && enabled(*d)) || lifted, lifted);
	}
	if (liftedAt >= 0) {
		const float top = rail::kItemTop + liftedAt * rail::kPitch + shift(liftedAt, n);
		paintItem(rt, *actions::find(ids[liftedAt]), D2D1::RectF(left + 6, top, left + 6 + kItemW, top + kItemH), true, true);
	}
	// The edge toward the canvas: the side panel's sash, which folds it (or,
	// slid out, keeps it open).
	if (edgeHot) {
		fillRect(rt, D2D1::RectF(left, 0, left + 2, h), withAlpha(accent, 0.6f));
		const D2D1_RECT_F card = D2D1::RectF(left + 1, h / 2 - 15, left + 11, h / 2 + 15);
		fillRound(rt, card, 5, c.tabCard());
		strokeRound(rt, D2D1::RectF(card.left + 0.5f, card.top + 0.5f, card.right - 0.5f, card.bottom - 0.5f), 4.5f, withAlpha(accent, 0.5f));
		drawIcon(rt, slid ? 0xE76B : 0xE76C, card, 8, accent);   // ChevronLeft, ChevronRight
	} else {
		fillRect(rt, D2D1::RectF(left, 0, left + 1, h), c.sash());
	}
}

// The tooltips: the tab's and the edge's (the items have their labels).
void ActionsRail::updateTips() {
	const bool f = folded(), out = slideAmount() > 0.001;
	const float w = (float)width(), h = (float)height();
	const std::string state = strf("%d%d%.0f%.0f", f, out, w, h);
	if (state == tipsFor) return;
	tipsFor = state;
	std::vector<Tip> tips;
	if (f && !out) {
		tips.push_back({ D2D1::RectF(0, 0, w, h), "Show Actions" + shortcuts::tipKeys(CMD_TOGGLE_ACTIONS) });
	} else {
		const float left = contentLeft(w);
		tips.push_back({ D2D1::RectF(left, 0, left + kEdgeW, h), f ? "Keep Actions open" : "Fold Actions away" });
	}
	setTips(tips);
}

// ---- The label ----------------------------------------------------------------------

LRESULT CALLBACK ActionsRail::labelProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
	ActionsRail* r = reinterpret_cast<ActionsRail*>(GetWindowLongPtrW(h, GWLP_USERDATA));
	switch (msg) {
	case WM_NCHITTEST: return HTTRANSPARENT;
	case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
	case WM_ERASEBKGND: return 1;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		BeginPaint(h, &ps);
		if (r) guarded("the rail's label", [&] { r->paintLabel(); });
		EndPaint(h, &ps);
		return 0;
	}
	}
	return DefWindowProcW(h, msg, wp, lp);
}

void ActionsRail::paintLabel() {
	const actions::Def* d = actions::find(labelId);
	ID2D1HwndRenderTarget* rt = labelSurface.begin(label);
	if (!rt) return;
	const Chrome c = chrome();
	RECT rc;
	GetClientRect(label, &rc);
	const float s = (float)labelSurface.scale();
	const float w = rc.right / s, h = rc.bottom / s;
	fillRect(rt, D2D1::RectF(0, 0, w, h), c.tabCard());
	strokeRound(rt, D2D1::RectF(0.5f, 0.5f, w - 0.5f, h - 0.5f), 4, withAlpha(c.tabInk(), 0.12f));
	if (d) {
		const bool en = enabled(*d);
		const std::string title = actions::plainTitle(*d), key = actions::keyLabel(*d);
		const float tw = textWidth(title, 12);
		drawText(rt, title, D2D1::RectF(kLabelPad, 0, kLabelPad + tw + 2, h), 12, withAlpha(c.tabInk(), en ? 1.0f : 0.5f));
		if (!key.empty())
			drawText(rt, key, D2D1::RectF(kLabelPad + tw + kLabelGap, 0, w - kLabelPad + 2, h), 11.5f, withAlpha(c.tabInk(), 0.5f));
	}
	labelSurface.end();
}

void ActionsRail::showLabel(const std::string& id) {
	const actions::Def* d = actions::find(id);
	const std::vector<std::string> ids = items();
	const auto at = std::find(ids.begin(), ids.end(), id);
	if (!d || at == ids.end() || !IsWindowVisible(hwnd)) { hideLabel(); return; }
	registerLabelClass();
	if (!label) {
		label = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kLabelClass, L"", WS_POPUP, 0, 0, 10, 10, win->window(), nullptr,
		                        appInstance(), nullptr);
		if (!label) return;
		SetWindowLongPtrW(label, GWLP_USERDATA, (LONG_PTR)this);
		SetWindowLongPtrW(label, GWLP_WNDPROC, (LONG_PTR)labelProc);
		// Windows 11's small rounded corners, as its own tooltips have.
		const DWORD round = 3;   // DWMWCP_ROUNDSMALL
		DwmSetWindowAttribute(label, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &round, sizeof round);
	}
	labelId = id;
	const std::string title = actions::plainTitle(*d), key = actions::keyLabel(*d);
	const float lw = kLabelPad + textWidth(title, 12) + (key.empty() ? 0 : kLabelGap + textWidth(key, 11.5f)) + kLabelPad + 2;
	const double s = scale();
	const int i = (int)(at - ids.begin());
	const float left = contentLeft((float)width());
	const float mid = rail::kItemTop + i * rail::kPitch + kItemH / 2;
	POINT p = { (LONG)std::lround((left - 6 - lw) * s), (LONG)std::lround((mid - kLabelH / 2) * s) };
	ClientToScreen(hwnd, &p);
	SetWindowPos(label, HWND_TOP, p.x, p.y, (int)std::lround(lw * s), (int)std::lround(kLabelH * s), SWP_NOACTIVATE | SWP_SHOWWINDOW);
	InvalidateRect(label, nullptr, FALSE);
	UpdateWindow(label);
}

void ActionsRail::hideLabel() {
	if (label && IsWindowVisible(label)) ShowWindow(label, SW_HIDE);
	labelId.clear();
}

// ---- The pointer ----------------------------------------------------------------------

void ActionsRail::mouseMove(float x, float y) {
	if (pressed >= 0 && pressZone == 1) {
		const float dx = x - pressX, dy = y - pressY;
		if (!dragging && std::hypot(dx, dy) >= (held ? 1.0f : rail::kDragStart)) {
			const std::vector<std::string> ids = items();
			if (pressed < (int)ids.size()) {
				dragging = true;
				dragId = ids[pressed];
				dragFrom = pressed;
				hov.dragging = true;
				hotItem = -1;
				hideLabel();
			}
		}
		if (dragging) { dragDy = dy; redraw(); }
		return;
	}
	const int item = itemAt(x, y);
	const bool edge = onEdge(x, y);
	if (item != hotItem || edge != edgeHot) {
		hotItem = item;
		edgeHot = edge;
		const std::vector<std::string> ids = items();
		if (item >= 0 && item < (int)ids.size()) showLabel(ids[item]);
		else hideLabel();
		redraw();
	}
}

void ActionsRail::mouseLeave() {
	if (dragging) return;
	hotItem = -1;
	edgeHot = false;
	hideLabel();
	redraw();
}

void ActionsRail::mouseDown(int button, float x, float y, bool) {
	if (button != 1) return;
	pressX = x;
	pressY = y;
	pressAt = nowSeconds();
	held = false;
	pressed = itemAt(x, y);
	// Folded (its tab, or slid out over the canvas), a click anywhere else
	// on it keeps it open.
	pressZone = pressed >= 0 ? 1 : onEdge(x, y) ? 2 : folded() ? 3 : 0;
}

void ActionsRail::endDrag(bool drop) {
	if (dragging && drop) {
		const int to = target((int)items().size());
		actions::moveRail(dragId, to);
	}
	const bool was = dragging;
	dragging = false;
	hov.dragging = false;
	dragId.clear();
	dragDy = 0;
	held = false;
	pressed = -1;
	pressZone = 0;
	if (was) {
		// Dragged out of a slid-out rail: it goes back now.
		hov.changed(nowSeconds(), folded(), prefs().actionsHoverOpen);
		actions::changed();
	}
	redraw();
}

void ActionsRail::mouseUp(int button, float x, float y) {
	if (button == 3) {
		POINT p = { (LONG)std::lround(x * scale()), (LONG)std::lround(y * scale()) };
		ClientToScreen(hwnd, &p);
		menu(p);
		return;
	}
	if (button != 1) return;
	const int zone = pressZone, item = pressed;
	const bool wasHeld = held;
	if (dragging) { endDrag(true); return; }
	pressed = -1;
	pressZone = 0;
	held = false;
	redraw();
	if (zone == 1) {
		// Let go where it was: a click (held to move but not moved: nothing runs).
		const std::vector<std::string> ids = items();
		if (item >= 0 && item < (int)ids.size() && itemAt(x, y) == item && !wasHeld) {
			const actions::Def* d = actions::find(ids[item]);
			if (d && enabled(*d)) {
				hideLabel();
				win->run(d->command);
			} else {
				MessageBeep(MB_OK);
			}
		}
	} else if (zone == 2 && onEdge(x, y)) {
		// The edge: fold it away, or keep a slid-out rail open.
		if (folded()) unfold();
		else fold();
	} else if (zone == 3) {
		unfold();
	}
}

void ActionsRail::captureLost() {
	if (dragging) endDrag(false);
	pressed = -1;
	pressZone = 0;
	held = false;
}

void ActionsRail::pressForTest(int index, float dy, double hold) {
	const float x = contentLeft((float)width()) + 6 + kItemW / 2, y = rail::kItemTop + index * rail::kPitch + kItemH / 2;
	mouseDown(1, x, y, false);
	if (hold > 0) {
		pressAt -= hold;
		tick(nowSeconds());
	}
	if (dy != 0) {
		mouseMove(x, y + dy / 2);
		mouseMove(x, y + dy);
	}
	mouseUp(1, x, y + dy);
}

// ---- Its menu -------------------------------------------------------------------------

void ActionsRail::menu(POINT screen) {
	enum { CUSTOMIZE = 1, FOLD, TOOLBAR, HIDE };
	const bool f = folded();
	HMENU m = CreatePopupMenu();
	AppendMenuW(m, MF_STRING, CUSTOMIZE, L"&Customize Actions…");
	AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(m, MF_STRING, FOLD, f ? (hov.slidOut ? L"&Keep Actions Open" : L"&Show Actions") : L"&Fold Actions Away");
	AppendMenuW(m, MF_STRING, TOOLBAR, L"Show Them in the &Toolbar Instead");
	AppendMenuW(m, MF_STRING, HIDE, L"&Hide Actions");
	// Its menu open keeps a slid-out rail out; closed, it goes if the pointer has.
	if (hov.slidOut && (hov.overStrip || hov.overRail)) hov.menuOpen = true;
	hov.pending = 0;
	hideLabel();
	const int chosen = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, screen.x, screen.y, 0, win->window(), nullptr);
	DestroyMenu(m);
	if (hov.menuOpen) {
		hov.menuOpen = false;
		hov.changed(nowSeconds(), folded(), prefs().actionsHoverOpen);
	}
	switch (chosen) {
	case CUSTOMIZE: PostMessageW(win->window(), WM_COMMAND, CMD_CUSTOMIZE_ACTIONS, 0); break;
	case FOLD: if (folded()) unfold(); else fold(); break;
	case TOOLBAR: prefs().actionsMode = 1; actions::changed(); break;
	case HIDE: prefs().actionsMode = 2; actions::changed(); break;
	default: break;
	}
}
