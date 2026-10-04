// The toolbar (see Toolbar.h).

#include "Toolbar.h"
#include "Canvas.h"
#include "Chrome.h"
#include "Shortcuts.h"
#include "Window.h"

#include <algorithm>
#include <climits>
#include <cmath>

namespace {

// Sizes in points, as the Mac's TBButton and TBSpeed.
const float kButtonW = 34, kButtonH = 30, kGroupGap = 12, kZoomW = 48, kSpeedW = 92, kCaptionW = 46;
const float kTrackW = 58;
// Glyphs for the dark mode switch: the moon and the sun.
const wchar_t kMoon = 0xE708, kSun = 0xE706;

bool groupShown(int g) { return g < 0 || (prefs().toolbarHidden & (1 << g)) == 0; }

}  // namespace

const char* toolGroupName(int g) {
	static const char* names[] = { "New, Open, Save", "Undo and Redo", "Copy and Paste", "Zoom", "Pause, Step, Speed",
	                               "Run (Simulation View)", "Lock", "Dark mode", "New tab", "Send feedback" };
	return g >= 0 && g < TGCount ? names[g] : "";
}

Toolbar::Toolbar(CircuitWindow* window, HWND parent) : win(window) {
	create(parent);
	build(prefs().toolbarStyle, true);
}

void Toolbar::build(int style, bool live) {
	items.clear();
	auto add = [&](Kind k, int command, int group, bool right) { items.push_back(Item{ k, command, group, right }); };
	if (style == TSMinimal) {
		add(Title, 0, -1, false);
		add(Button, CMD_UNDO, TGUndo, false); add(Button, CMD_REDO, TGUndo, false);
		add(Button, CMD_RUNNING, TGSim, true);
		add(Button, CMD_SIM_VIEW, TGRun, true);
		add(More, 0, -1, true);
	} else {
		if (prefs().showTitle) add(Title, 0, -1, false);
		add(Button, CMD_NEW, TGFile, false); add(Button, CMD_OPEN, TGFile, false); add(Button, CMD_SAVE, TGFile, false);
		add(Button, CMD_UNDO, TGUndo, false); add(Button, CMD_REDO, TGUndo, false);
		add(Button, CMD_COPY, TGClipboard, false); add(Button, CMD_PASTE, TGClipboard, false);
		add(Button, CMD_ZOOM_OUT, TGZoom, false); add(Zoom, 0, TGZoom, false); add(Button, CMD_ZOOM_IN, TGZoom, false);
		// The right-hand side, laid out from the right edge.
		add(Button, CMD_RUNNING, TGSim, true); add(Button, CMD_STEP, TGSim, true); add(Speed, 0, TGSim, true);
		add(Button, CMD_SIM_VIEW, TGRun, true);
		add(Button, CMD_LOCK, TGLock, true);
		if (prefs().showThemeToggle) add(Button, CMD_DARK, TGTheme, true);
		add(Button, CMD_NEW_TAB, TGTab, true);
		add(Button, CMD_FEEDBACK, TGFeedback, true);
		add(More, 0, -1, true);
	}
	if (live) { add(CaptionMin, 0, -1, true); add(CaptionMax, 0, -1, true); add(CaptionClose, 0, -1, true); }
}

void Toolbar::layout(float w, float h, int style, bool live) {
	relayout = false;
	lastW = w;
	laidStyle = style;
	laidTitle = win->titleText() + (style == TSMinimal ? "\n" + win->pageName(win->currentPage()) : std::string());
	build(style, live);
	const float by = std::floor((h - kButtonH) / 2);
	// Room either side of a group (Classic's capsule; the others keep its spacing).
	const float pad = style == TSMinimal ? 0.0f : 3.0f;
	const float groupGap = style == TSMinimal ? 6.0f : kGroupGap;
	auto isCaption = [](const Item& it) { return it.kind == CaptionMin || it.kind == CaptionMax || it.kind == CaptionClose; };
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
	// Between two neighbouring groups (-1: the name or •••, which have no capsule).
	auto gapBetween = [&](int leftGroup, int rightGroup) {
		return (leftGroup >= 0 ? pad : 0) + (rightGroup >= 0 ? pad : 0) + (leftGroup < 0 || rightGroup < 0 ? 8.0f : groupGap);
	};
	for (Item& it : items) it.shown = false;

	// From the right: the window's buttons (the bar's full height), •••,
	// then the groups, last first.
	float x = w;
	for (int i = (int)items.size() - 1; i >= 0; i--) {
		Item& it = items[i];
		if (!isCaption(it)) continue;
		x -= kCaptionW;
		it.rect = D2D1::RectF(x, 0, x + kCaptionW, h);
		it.shown = true;
	}
	x -= live ? 8 : 10;
	int prev = INT_MIN;
	for (int i = (int)items.size() - 1; i >= 0; i--) {
		Item& it = items[i];
		if (!it.right || isCaption(it) || !groupShown(it.group)) continue;
		if (prev == INT_MIN) x -= it.group >= 0 ? pad : 0;
		else if (it.group != prev) x -= gapBetween(it.group, prev);
		x -= widthOf(it);
		place(it, x);
		prev = it.group;
	}
	const float rightStart = x - (prev >= 0 ? pad : 0);

	x = 10;
	if (style == TSMinimal) {
		// The name in the middle, with the page under it; Undo and Redo at the left.
		for (Item& it : items) {
			if (it.right || !groupShown(it.group)) continue;
			if (it.kind == Title) continue;
			place(it, x);
			x += widthOf(it);
		}
		for (Item& it : items) {
			if (it.kind != Title) continue;
			const std::string page = win->pageName(win->currentPage());
			const float tw = std::min(260.0f, std::max(textWidth(win->titleText(), 13, true), textWidth(page, 10.5f)) + 30);
			it.rect = D2D1::RectF((w - tw) / 2, by - 4, (w + tw) / 2, by + kButtonH + 4);
			// Kept clear of the tools either side.
			it.rect.left = std::max(it.rect.left, x + 8);
			it.rect.right = std::min(it.rect.right, rightStart - 8);
			it.shown = prefs().showTitle && it.rect.right - it.rect.left > 60;
		}
	} else {
		// From the left: the circuit's name, then the tool groups while they
		// fit; the ones that don't are left out, last first (everything is in
		// ••• too).
		prev = INT_MIN;
		std::vector<std::pair<int, float>> groupEnds;
		for (Item& it : items) {
			if (it.right || !groupShown(it.group)) continue;
			if (prev == INT_MIN) x += it.group >= 0 ? pad : 0;
			else if (it.group != prev) x += gapBetween(prev, it.group);
			place(it, x);
			x += widthOf(it);
			prev = it.group;
			if (it.group >= 0) groupEnds.push_back({ it.group, x + pad });
		}
		for (const auto& ge : groupEnds)
			if (ge.second > rightStart - 8)
				for (Item& it : items) if (!it.right && it.group == ge.first) it.shown = false;
		// The name alone, cut short, when even that doesn't fit.
		for (Item& it : items)
			if (it.kind == Title) it.rect.right = std::max(it.rect.left + 40, std::min(it.rect.right, rightStart - 8));
	}

	if (!live) return;
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
	case CMD_DARK: return prefs().dark;
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
	case CMD_DARK: return prefs().dark ? kMoon : kSun;
	case CMD_NEW_TAB: return Icon::NewTab;
	case CMD_FEEDBACK: return Icon::Feedback;
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
	// With the keys each has now (Settings > Shortcuts changes them).
	const std::string keys = shortcuts::tipKeys(it.command);
	switch (it.command) {
	case CMD_NEW: return "New circuit" + keys;
	case CMD_OPEN: return "Your circuits" + keys;
	case CMD_SAVE: return "Save a version" + keys;
	case CMD_UNDO: return "Undo" + keys;
	case CMD_REDO: return "Redo" + keys;
	case CMD_COPY: return "Copy" + keys;
	case CMD_PASTE: return "Paste" + keys;
	case CMD_ZOOM_OUT: return "Zoom out" + keys;
	case CMD_ZOOM_IN: return "Zoom in" + keys;
	case CMD_RUNNING: return "Pause or resume the simulation";
	case CMD_STEP: return "Step once" + keys;
	case CMD_SIM_VIEW: return "Simulation View" + keys;
	case CMD_LOCK: return "Lock the circuit so it can't be edited";
	case CMD_DARK: return "Dark mode" + keys;
	case CMD_NEW_TAB: return "New tab" + keys;
	case CMD_FEEDBACK: return "Send feedback: a bug, an idea, anything";
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
	const int style = prefs().toolbarStyle;
	const std::string title = win->titleText() + (style == TSMinimal ? "\n" + win->pageName(win->currentPage()) : std::string());
	if (relayout || w != lastW || style != laidStyle || title != laidTitle) layout(w, h, style);
	draw(rt, w, h, style, true);
}

void Toolbar::paintPicture(ID2D1RenderTarget* rt, float w, float h, int style) {
	const std::vector<Item> keep = items;
	const int keepStyle = laidStyle;
	const float keepW = lastW;
	const std::string keepTitle = laidTitle;
	const int keepHot = hot, keepPressed = pressed;
	hot = pressed = -1;
	layout(w, h, style, false);
	draw(rt, w, h, style, false);
	items = keep;
	laidStyle = keepStyle;
	lastW = keepW;
	laidTitle = keepTitle;
	hot = keepHot;
	pressed = keepPressed;
	// Laid out afresh when next drawn: a change Settings made before this
	// picture (a group hidden, the name turned off) still has to reach the bar.
	relayout = true;
}

void Toolbar::draw(ID2D1RenderTarget* rt, float w, float h, int style, bool live) {
	const Chrome c = chrome();
	const bool dark = c.dark;
	const bool active = !live || GetActiveWindow() == GetAncestor(hwnd, GA_ROOT);
	// Seamless: the canvas's own colour, no line under it, the tools quiet
	// until pointed at (and dark over Simulation View's dark canvas).
	const bool quiet = style == TSSeamless;
	const bool overSim = quiet && win->simView();
	D2D1_COLOR_F bg = style == TSClassic ? c.bar() : style == TSMinimal ? (dark ? rgb255(28, 31, 37) : rgb255(246, 247, 249)) : c.canvas();
	if (overSim) bg = d2dColor(Palette{ true, true }.canvas());
	fillRect(rt, D2D1::RectF(0, 0, w, h), bg);
	if (!quiet) fillRect(rt, D2D1::RectF(0, h - 1, w, h), dark ? D2D1::ColorF(0, 0, 0, 0.35f) : D2D1::ColorF(0, 0, 0, 0.08f));
	const Chrome ic{ dark || overSim };
	const D2D1_COLOR_F ink = ic.barInk();
	const D2D1_COLOR_F accent = c.accent();
	const D2D1_COLOR_F green = ic.dark ? rgb255(64, 214, 110) : rgb255(36, 168, 76);

	// Classic: the capsules behind each group.
	if (style == TSClassic) {
		for (int g = 0; g < TGCount; g++) {
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
	}

	for (int i = 0; i < (int)items.size(); i++) {
		const Item& it = items[i];
		if (!it.shown) continue;
		const bool isHot = live && i == hot && isEnabled(it), isPressed = live && i == pressed && isHot;
		const D2D1_RECT_F r = it.rect;
		const float inkA = quiet && !isHot ? 0.5f : 0.92f;
		switch (it.kind) {
		case Title: {
			if (isHot) fillRound(rt, r, 7, withAlpha(ink, 0.08f));
			const std::string name = win->titleText();
			if (style == TSMinimal) {
				// The name over the page's, centred.
				const float tw = std::min(textWidth(name, 13, true), r.right - r.left - 30);
				const float mid = (r.left + r.right - 14) / 2;
				drawText(rt, name, D2D1::RectF(r.left, r.top + 3, r.right - 14, r.top + 21), 13, withAlpha(ink, active ? 1.0f : 0.6f),
				         TextAlign::Center, true);
				drawText(rt, win->pageName(win->currentPage()), D2D1::RectF(r.left, r.top + 20, r.right - 14, r.bottom - 3), 10.5f,
				         withAlpha(ink, 0.55f), TextAlign::Center);
				drawIcon(rt, Icon::ChevronDown, D2D1::RectF(mid + tw / 2 + 2, r.top + 4, mid + tw / 2 + 16, r.top + 20), 8,
				         withAlpha(ink, isHot ? 0.7f : 0.35f));
				break;
			}
			const float tw = std::min(textWidth(name, 13, true), r.right - r.left - 30);
			drawText(rt, name, D2D1::RectF(r.left + 8, r.top, r.left + 8 + tw + 1, r.bottom), 13,
			         withAlpha(ink, active ? 1.0f : 0.6f), TextAlign::Leading, true);
			drawIcon(rt, Icon::ChevronDown, D2D1::RectF(r.left + 10 + tw, r.top, r.left + 24 + tw, r.bottom), 8,
			         withAlpha(ink, isHot ? 0.7f : 0.35f));
			break;
		}
		case Zoom: {
			if (isHot) fillRound(rt, D2D1::RectF(r.left + 2, r.top, r.right - 2, r.bottom), 7, withAlpha(ink, 0.08f));
			Canvas* cv = win->currentCanvas();
			drawText(rt, strf("%d%%", cv ? cv->zoomPercent() : 100), r, 12, withAlpha(ink, inkA), TextAlign::Center);
			break;
		}
		case Speed: {
			const float cy = (r.top + r.bottom) / 2;
			const bool held = live && (i == hot || draggingSpeed);
			drawIcon(rt, Icon::Speed, D2D1::RectF(r.left + 4, r.top, r.left + 24, r.bottom), 13, withAlpha(ink, quiet && !held ? 0.5f : 0.9f));
			const D2D1_RECT_F t = speedTrack(it);
			const float f = (float)speedFraction(win->stepMs());
			fillRound(rt, D2D1::RectF(t.left, cy - 1.5f, t.right, cy + 1.5f), 1.5f, withAlpha(ink, 0.16f));
			fillRound(rt, D2D1::RectF(t.left, cy - 1.5f, t.left + std::max(3.0f, kTrackW * f), cy + 1.5f), 1.5f, withAlpha(ink, quiet ? 0.5f : 0.7f));
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
				const D2D1_COLOR_F back = on ? withAlpha(onColor, isPressed ? 0.30f : (isHot ? 0.24f : 0.18f))
				                             : (ic.dark ? D2D1::ColorF(1, 1, 1, isPressed ? 0.13f : 0.08f)
				                                        : D2D1::ColorF(0, 0, 0, isPressed ? 0.094f : 0.05f));
				fillRound(rt, D2D1::RectF(r.left + 2, r.top + 1, r.right - 2, r.bottom - 1), 7, back);
			}
			D2D1_COLOR_F fg = withAlpha(ink, isEnabled(it) ? inkA : 0.3f);
			if (colored) fg = withAlpha(green, quiet && !on && !isHot ? 0.75f : 1.0f);
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
		if (it.kind != Button || it.command != command || !it.shown) continue;
		RECT r = { (LONG)(it.rect.left * s), (LONG)(it.rect.top * s), (LONG)(it.rect.right * s), (LONG)(it.rect.bottom * s) };
		MapWindowPoints(hwnd, nullptr, (POINT*)&r, 2);
		return r;
	}
	RECT r;
	GetWindowRect(hwnd, &r);
	return r;
}

bool Toolbar::buttonPoint(int command, POINT& p) {
	if (relayout || (float)width() != lastW || prefs().toolbarStyle != laidStyle) layout((float)width(), (float)height(), prefs().toolbarStyle);
	const double s = scale();
	for (const Item& it : items) {
		const bool match = command == kMinimize ? it.kind == CaptionMin
		                 : command == kClose    ? it.kind == CaptionClose
		                                        : it.kind == Button && it.command == command;
		if (!match) continue;
		if (!it.shown) return false;
		p = { (LONG)((it.rect.left + it.rect.right) / 2 * s), (LONG)((it.rect.top + it.rect.bottom) / 2 * s) };
		return true;
	}
	return false;
}

bool Toolbar::hasButton(int command) {
	if (relayout || (float)width() != lastW || prefs().toolbarStyle != laidStyle) layout((float)width(), (float)height(), prefs().toolbarStyle);
	for (const Item& it : items) {
		const bool match = command == kMinimize ? it.kind == CaptionMin
		                 : command == kClose    ? it.kind == CaptionClose
		                                        : it.kind == Button && it.command == command;
		if (match) return groupShown(it.group);
	}
	return false;
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
		const HWND self = hwnd;
		if (k == Title) win->titleMenu(p);
		else win->moreMenu(p, true);
		if (!IsWindow(self)) return;   // what was chosen closed the window, this bar with it
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
