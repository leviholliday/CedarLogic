// The toolbar (see Toolbar.h).

#include "Toolbar.h"
#include "Canvas.h"
#include "Chrome.h"
#include "Window.h"

#include <algorithm>
#include <cmath>

namespace {

// Sizes in points, as the Mac's TBButton and TBSpeed.
const float kButtonW = 34, kButtonH = 30, kGroupGap = 12, kZoomW = 48, kSpeedW = 92, kCaptionW = 46;
const float kTrackW = 58;

}  // namespace

Toolbar::Toolbar(CircuitWindow* window, HWND parent) : win(window) {
	create(parent);
	build();
}

void Toolbar::build() {
	items.clear();
	auto add = [&](Kind k, int command, int group) { items.push_back(Item{ k, command, group }); };
	add(Title, 0, -1);
	add(Button, CMD_NEW, 1); add(Button, CMD_OPEN, 1); add(Button, CMD_SAVE, 1);
	add(Button, CMD_UNDO, 2); add(Button, CMD_REDO, 2);
	add(Button, CMD_COPY, 3); add(Button, CMD_PASTE, 3);
	add(Button, CMD_ZOOM_OUT, 4); add(Zoom, 0, 4); add(Button, CMD_ZOOM_IN, 4);
	// The right-hand side, laid out from the right edge.
	add(Button, CMD_RUNNING, 10); add(Button, CMD_STEP, 10); add(Speed, 0, 10);
	add(Button, CMD_SIM_VIEW, 11);
	add(Button, CMD_LOCK, 12);
	add(Button, CMD_NEW_TAB, 13);
	add(More, 0, -1);
	add(CaptionMin, 0, -1); add(CaptionMax, 0, -1); add(CaptionClose, 0, -1);
}

void Toolbar::layout(float w, float h) {
	relayout = false;
	lastW = w;
	const float by = std::floor((h - kButtonH) / 2);
	auto widthOf = [&](const Item& it) -> float {
		switch (it.kind) {
		case Zoom: return kZoomW;
		case Speed: return kSpeedW;
		case Title: return std::min(220.0f, textWidth(win->titleText(), 13, true)) + 30;
		case CaptionMin: case CaptionMax: case CaptionClose: return kCaptionW;
		default: return kButtonW;
		}
	};
	auto place = [&](Item& it, float left) {
		it.rect = D2D1::RectF(left, by, left + widthOf(it), by + kButtonH);
		it.shown = true;
	};

	// From the right: the window's buttons (the bar's full height), •••,
	// then the simulation groups, each in its capsule (3 points of padding
	// either side).
	float x = w;
	for (int i = (int)items.size() - 1; i >= 0; i--) {
		Item& it = items[i];
		if (it.kind != CaptionMin && it.kind != CaptionMax && it.kind != CaptionClose) continue;
		x -= kCaptionW;
		it.rect = D2D1::RectF(x, 0, x + kCaptionW, h);
		it.shown = true;
	}
	x -= 8;
	for (Item& it : items) if (it.kind == More) { x -= kButtonW; place(it, x); }
	x -= 8;
	for (int g = 13; g >= 10; g--) {
		x -= 3;
		for (int i = (int)items.size() - 1; i >= 0; i--) {
			if (items[i].group != g) continue;
			x -= widthOf(items[i]);
			place(items[i], x);
		}
		x -= 3 + kGroupGap;
	}
	const float rightStart = x + kGroupGap;

	// From the left: the circuit's name, then the tool groups while they fit;
	// the ones that don't are left out, last first (everything is in ••• too).
	x = 10;
	float groupEnd[5] = { 0, 0, 0, 0, 0 };
	for (Item& it : items) if (it.kind == Title) { place(it, x); x += widthOf(it) + 6; }
	for (int g = 1; g <= 4; g++) {
		x += 3;
		for (Item& it : items) {
			if (it.group != g) continue;
			place(it, x);
			x += widthOf(it);
		}
		x += 3;
		groupEnd[g] = x;
		x += kGroupGap;
	}
	for (int g = 1; g <= 4; g++)
		if (groupEnd[g] > rightStart - 8) for (Item& it : items) if (it.group == g) it.shown = false;
	// The name alone, cut short, when even that doesn't fit.
	for (Item& it : items)
		if (it.kind == Title) it.rect.right = std::max(it.rect.left + 40, std::min(it.rect.right, rightStart - 8));

	std::vector<Tip> tips;
	for (const Item& it : items)
		if (it.shown && it.kind != CaptionMax && it.kind != Speed) tips.push_back({ it.rect, tipFor(it) });
	setTips(tips);
}

bool Toolbar::isOn(const Item& it) const {
	if (it.kind != Button) return false;
	switch (it.command) {
	case CMD_RUNNING: return !win->running();
	case CMD_SIM_VIEW: return win->simView();
	case CMD_LOCK: return win->locked();
	default: return false;
	}
}

bool Toolbar::isEnabled(const Item& it) const {
	if (it.kind != Button) return true;
	switch (it.command) {
	case CMD_UNDO: case CMD_REDO: return win->commandEnabled(it.command);
	default: return true;
	}
}

wchar_t Toolbar::iconFor(const Item& it) const {
	switch (it.kind) {
	case More: return Icon::More;
	case CaptionMin: return Icon::Minimize;
	case CaptionMax: return IsZoomed(GetAncestor(hwnd, GA_ROOT)) ? Icon::Restore : Icon::Maximize;
	case CaptionClose: return Icon::Close;
	default: break;
	}
	switch (it.command) {
	case CMD_NEW: return Icon::Page;
	case CMD_OPEN: return Icon::Open;
	case CMD_SAVE: return Icon::Save;
	case CMD_UNDO: return Icon::Undo;
	case CMD_REDO: return Icon::Redo;
	case CMD_COPY: return Icon::Copy;
	case CMD_PASTE: return Icon::Paste;
	case CMD_ZOOM_OUT: return Icon::ZoomOut;
	case CMD_ZOOM_IN: return Icon::ZoomIn;
	case CMD_RUNNING: return win->running() ? Icon::Pause : Icon::Play;
	case CMD_STEP: return Icon::Step;
	case CMD_SIM_VIEW: return win->simView() ? Icon::StopSolid : Icon::Play;
	case CMD_LOCK: return win->locked() ? Icon::Lock : Icon::Unlock;
	case CMD_NEW_TAB: return Icon::NewTab;
	default: return L'?';
	}
}

std::string Toolbar::tipFor(const Item& it) const {
	switch (it.kind) {
	case Zoom: return "Zoom level. Click for 100%";
	case More: return "Every menu (Alt)";
	case CaptionMin: return "Minimize";
	case CaptionClose: return "Close";
	case Title: return "Rename, duplicate or find this circuit";
	default: break;
	}
	switch (it.command) {
	case CMD_NEW: return "New circuit (Ctrl+N)";
	case CMD_OPEN: return "Open (Ctrl+O)";
	case CMD_SAVE: return "Save (Ctrl+S)";
	case CMD_UNDO: return "Undo (Ctrl+Z)";
	case CMD_REDO: return "Redo (Ctrl+Y)";
	case CMD_COPY: return "Copy (Ctrl+C)";
	case CMD_PASTE: return "Paste (Ctrl+V)";
	case CMD_ZOOM_OUT: return "Zoom out (Ctrl+-)";
	case CMD_ZOOM_IN: return "Zoom in (Ctrl+=)";
	case CMD_RUNNING: return "Pause or resume the simulation";
	case CMD_STEP: return "Step once (Ctrl+Shift+R)";
	case CMD_SIM_VIEW: return "Simulation View (Ctrl+R)";
	case CMD_LOCK: return "Lock the circuit so it can't be edited";
	case CMD_NEW_TAB: return "New tab (Ctrl+T)";
	default: return std::string();
	}
}

D2D1_RECT_F Toolbar::speedTrack(const Item& it) const {
	const float cy = (it.rect.top + it.rect.bottom) / 2;
	const float left = it.rect.left + 6 + 16 + 6;
	return D2D1::RectF(left, cy - 10, left + kTrackW, cy + 10);
}

void Toolbar::setSpeedAt(const Item& it, float x) {
	const D2D1_RECT_F t = speedTrack(it);
	win->setStepMs(speedFromFraction((x - t.left) / (t.right - t.left)));
	redraw();
}

int Toolbar::itemAt(float x, float y) const {
	for (int i = 0; i < (int)items.size(); i++)
		if (items[i].shown && inRect(items[i].rect, x, y)) return i;
	return -1;
}

void Toolbar::paint(ID2D1RenderTarget* rt, float w, float h) {
	if (relayout || w != lastW) layout(w, h);
	const Chrome c = chrome();
	const bool dark = c.dark;
	const bool active = GetActiveWindow() == GetAncestor(hwnd, GA_ROOT);
	rt->Clear(c.bar());
	fillRect(rt, D2D1::RectF(0, h - 1, w, h), dark ? D2D1::ColorF(0, 0, 0, 0.35f) : D2D1::ColorF(0, 0, 0, 0.08f));
	const D2D1_COLOR_F ink = c.barInk();
	const D2D1_COLOR_F accent = c.accent();
	const D2D1_COLOR_F green = dark ? rgb255(64, 214, 110) : rgb255(36, 168, 76);

	// The capsules behind each group.
	for (int g = 1; g <= 13; g++) {
		D2D1_RECT_F u = D2D1::RectF(1e9f, 1e9f, -1e9f, -1e9f);
		bool any = false;
		for (const Item& it : items) {
			if (it.group != g || !it.shown) continue;
			any = true;
			u.left = std::min(u.left, it.rect.left); u.top = std::min(u.top, it.rect.top);
			u.right = std::max(u.right, it.rect.right); u.bottom = std::max(u.bottom, it.rect.bottom);
		}
		if (!any) continue;
		const D2D1_RECT_F cap = D2D1::RectF(u.left - 3, u.top - 1, u.right + 3, u.bottom + 1);
		const float r = (cap.bottom - cap.top) / 2;
		fillRound(rt, cap, r, dark ? D2D1::ColorF(1, 1, 1, 0.063f) : D2D1::ColorF(0, 0, 0, 0.043f));
		strokeRound(rt, cap, r, dark ? D2D1::ColorF(1, 1, 1, 0.07f) : D2D1::ColorF(0, 0, 0, 0.063f));
	}

	for (int i = 0; i < (int)items.size(); i++) {
		const Item& it = items[i];
		if (!it.shown) continue;
		const bool isHot = i == hot && isEnabled(it), isPressed = i == pressed && isHot;
		const D2D1_RECT_F r = it.rect;
		switch (it.kind) {
		case Title: {
			if (isHot) fillRound(rt, r, 7, withAlpha(ink, 0.08f));
			const float tw = std::min(textWidth(win->titleText(), 13, true), r.right - r.left - 30);
			drawText(rt, win->titleText(), D2D1::RectF(r.left + 8, r.top, r.left + 8 + tw + 1, r.bottom), 13,
			         withAlpha(ink, active ? 1.0f : 0.6f), TextAlign::Leading, true);
			drawIcon(rt, Icon::ChevronDown, D2D1::RectF(r.left + 10 + tw, r.top, r.left + 24 + tw, r.bottom), 8,
			         withAlpha(ink, isHot ? 0.7f : 0.35f));
			break;
		}
		case Zoom: {
			if (isHot) fillRound(rt, D2D1::RectF(r.left + 2, r.top, r.right - 2, r.bottom), 7, withAlpha(ink, 0.08f));
			Canvas* cv = win->currentCanvas();
			drawText(rt, strf("%d%%", cv ? cv->zoomPercent() : 100), r, 12, withAlpha(ink, 0.92f), TextAlign::Center);
			break;
		}
		case Speed: {
			const float cy = (r.top + r.bottom) / 2;
			drawIcon(rt, Icon::Speed, D2D1::RectF(r.left + 4, r.top, r.left + 24, r.bottom), 13, withAlpha(ink, 0.9f));
			const D2D1_RECT_F t = speedTrack(it);
			const float f = (float)speedFraction(win->stepMs());
			fillRound(rt, D2D1::RectF(t.left, cy - 1.5f, t.right, cy + 1.5f), 1.5f, withAlpha(ink, 0.16f));
			fillRound(rt, D2D1::RectF(t.left, cy - 1.5f, t.left + std::max(3.0f, kTrackW * f), cy + 1.5f), 1.5f, withAlpha(ink, 0.7f));
			const D2D1_POINT_2F knob = D2D1::Point2F(t.left + kTrackW * f, cy);
			fillCircle(rt, D2D1::Point2F(knob.x, knob.y + 0.5f), 7, D2D1::ColorF(0, 0, 0, 0.18f));
			fillCircle(rt, knob, 6.5f, D2D1::ColorF(1, 1, 1, 1));
			if (ID2D1SolidColorBrush* b = nullptr; SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.2f), &b))) {
				rt->DrawEllipse(D2D1::Ellipse(knob, 6, 6), b, 1);
				b->Release();
			}
			break;
		}
		case CaptionMin: case CaptionMax: case CaptionClose: {
			const bool closeKind = it.kind == CaptionClose;
			const bool h2 = it.kind == CaptionMax ? maxHot : isHot;
			const bool p2 = it.kind == CaptionMax ? maxPressed : isPressed;
			if (closeKind && (h2 || p2)) fillRect(rt, r, p2 ? rgb255(148, 31, 21) : rgb255(196, 43, 28));
			else if (h2) fillRect(rt, r, withAlpha(ink, p2 ? 0.14f : 0.08f));
			const D2D1_COLOR_F g = closeKind && (h2 || p2) ? D2D1::ColorF(1, 1, 1, 1) : withAlpha(ink, active ? 0.9f : 0.45f);
			drawIcon(rt, iconFor(it), r, 10, g);
			break;
		}
		default: {
			const bool on = isOn(it);
			const bool colored = it.command == CMD_SIM_VIEW;
			if (on || isHot) {
				const D2D1_COLOR_F onColor = colored ? green : accent;
				const D2D1_COLOR_F bg = on ? withAlpha(onColor, isPressed ? 0.30f : (isHot ? 0.24f : 0.18f))
				                           : (dark ? D2D1::ColorF(1, 1, 1, isPressed ? 0.13f : 0.08f)
				                                   : D2D1::ColorF(0, 0, 0, isPressed ? 0.094f : 0.05f));
				fillRound(rt, D2D1::RectF(r.left + 2, r.top + 1, r.right - 2, r.bottom - 1), 7, bg);
			}
			D2D1_COLOR_F fg = withAlpha(ink, isEnabled(it) ? 0.92f : 0.3f);
			if (colored) fg = green;
			else if (on) fg = accent;
			if (it.command == CMD_NEW) drawNewDocIcon(rt, r, fg);
			else if (it.command == CMD_NEW_TAB) drawNewTabIcon(rt, r, fg);
			else if (it.command == CMD_OPEN) drawFolderIcon(rt, r, fg);
			else drawIcon(rt, iconFor(it), r, colored && on ? 12 : 15, fg);
			break;
		}
		}
	}
}

LRESULT Toolbar::hitTest(float x, float y) {
	// The top few points are the window's resize edge.
	if (y < 4 && !IsZoomed(GetAncestor(hwnd, GA_ROOT))) return HTTRANSPARENT;
	const int i = itemAt(x, y);
	if (i < 0 || items[i].kind == CaptionMax) return HTTRANSPARENT;   // the frame: caption, or maximize
	return HTCLIENT;
}

RECT Toolbar::maximizeRect() const {
	for (const Item& it : items) {
		if (it.kind != CaptionMax) continue;
		const double s = scale();
		RECT r = { (LONG)(it.rect.left * s), (LONG)(it.rect.top * s), (LONG)(it.rect.right * s), (LONG)(it.rect.bottom * s) };
		MapWindowPoints(hwnd, GetParent(hwnd), (POINT*)&r, 2);
		return r;
	}
	return RECT{ 0, 0, 0, 0 };
}

RECT Toolbar::commandRect(int command) const {
	const double s = scale();
	for (const Item& it : items) {
		if (it.kind != Button || it.command != command) continue;
		RECT r = { (LONG)(it.rect.left * s), (LONG)(it.rect.top * s), (LONG)(it.rect.right * s), (LONG)(it.rect.bottom * s) };
		MapWindowPoints(hwnd, nullptr, (POINT*)&r, 2);
		return r;
	}
	RECT r;
	GetWindowRect(hwnd, &r);
	return r;
}

void Toolbar::setMaximizeHot(bool isHot, bool isPressed) {
	if (isHot == maxHot && isPressed == maxPressed) return;
	maxHot = isHot;
	maxPressed = isPressed;
	redraw();
}

void Toolbar::mouseMove(float x, float y) {
	if (draggingSpeed) {
		for (const Item& it : items) if (it.kind == Speed) setSpeedAt(it, x);
		return;
	}
	const int i = itemAt(x, y);
	if (i != hot) { hot = i; redraw(); }
}

void Toolbar::mouseLeave() {
	if (hot != -1 && !draggingSpeed) { hot = -1; redraw(); }
}

void Toolbar::mouseDown(int button, float x, float y, bool doubleClick) {
	(void)doubleClick;
	if (button != 1) return;
	const int i = itemAt(x, y);
	if (i < 0) return;
	if (items[i].kind == Speed) {
		draggingSpeed = true;
		setSpeedAt(items[i], x);
		return;
	}
	pressed = i;
	hot = i;
	redraw();
	// Menus open on the press, as menus do, under their button.
	if (items[i].kind == Title || items[i].kind == More) {
		if (GetCapture() == hwnd) ReleaseCapture();
		const double s = scale();
		const Kind k = items[i].kind;
		POINT p = { (LONG)((k == More ? items[i].rect.right : items[i].rect.left) * s), (LONG)(items[i].rect.bottom * s + 2) };
		ClientToScreen(hwnd, &p);
		pressed = -1;
		if (k == Title) win->titleMenu(p);
		else win->moreMenu(p, true);
		hot = -1;
		redraw();
	}
}

void Toolbar::mouseUp(int button, float x, float y) {
	if (button != 1) return;
	if (draggingSpeed) { draggingSpeed = false; return; }
	const int i = pressed;
	pressed = -1;
	redraw();
	if (i >= 0 && itemAt(x, y) == i) activate(i);
}

void Toolbar::captureLost() {
	draggingSpeed = false;
	if (pressed != -1) { pressed = -1; redraw(); }
}

void Toolbar::activate(int index) {
	const Item& it = items[index];
	HWND top = GetAncestor(hwnd, GA_ROOT);
	switch (it.kind) {
	case Zoom: win->run(CMD_ZOOM_ACTUAL); break;
	case CaptionMin: ShowWindow(top, SW_MINIMIZE); break;
	case CaptionClose: PostMessageW(top, WM_CLOSE, 0, 0); break;
	case Button: if (isEnabled(it)) win->run(it.command); break;
	default: break;
	}
}

LRESULT Toolbar::message(UINT msg, WPARAM, LPARAM, bool& handled) {
	handled = false;
	if (msg == WM_SIZE) relayout = true;
	return 0;
}
