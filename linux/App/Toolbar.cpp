// The toolbar (see Toolbar.h).

#include "Toolbar.h"
#include "Canvas.h"
#include "Window.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// Sizes in points, as the Mac's TBButton and TBSpeed.
const float kButtonW = 34, kButtonH = 30, kGroupGap = 12, kZoomW = 48, kSpeedW = 92, kWinButtonW = 34;
const float kTrackW = 58;

bool is(const char* a, const char* b) { return a && b && std::strcmp(a, b) == 0; }

}  // namespace

Toolbar::Toolbar(CircuitWindow* window) : win(window) {
	create();
	gtk_widget_set_size_request(area, -1, (int)barHeight());
	build();
}

GtkWindow* Toolbar::topWindow() const { return win->window(); }

void Toolbar::build() {
	items.clear();
	auto add = [&](Kind k, const char* action, int group) { items.push_back(Item{ k, action, group }); };
	add(Title, nullptr, -1);
	add(Button, "app.new", 1); add(Button, "app.open", 1); add(Button, "win.save", 1);
	add(Button, "win.undo", 2); add(Button, "win.redo", 2);
	add(Button, "win.copy", 3); add(Button, "win.paste", 3);
	add(Button, "win.zoom-out", 4); add(Zoom, nullptr, 4); add(Button, "win.zoom-in", 4);
	// The right-hand side, laid out from the right edge.
	add(Button, "win.running", 10); add(Button, "win.step", 10); add(Speed, nullptr, 10);
	add(Button, "win.sim-view", 11);
	add(Button, "win.lock", 12);
	add(Button, "win.new-tab", 13);
	add(More, nullptr, -1);
	// The window's buttons, as the desktop lays them out ("menu:minimize,maximize,close").
	gchar* layoutSetting = nullptr;
	if (GtkSettings* s = gtk_settings_get_default()) g_object_get(s, "gtk-decoration-layout", &layoutSetting, nullptr);
	const std::string decoration = layoutSetting ? layoutSetting : "menu:minimize,maximize,close";
	g_free(layoutSetting);
	const size_t colon = decoration.find(':');
	auto addSide = [&](const std::string& part, bool left) {
		size_t at = 0;
		while (at <= part.size()) {
			const size_t comma = part.find(',', at);
			const std::string name = part.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
			Kind k = Button;
			if (name == "minimize") k = WinMin;
			else if (name == "maximize") k = WinMax;
			else if (name == "close") k = WinClose;
			if (k != Button) { items.push_back(Item{ k, nullptr, -1 }); items.back().leftSide = left; }
			if (comma == std::string::npos) break;
			at = comma + 1;
		}
	};
	addSide(colon == std::string::npos ? std::string() : decoration.substr(0, colon), true);
	addSide(colon == std::string::npos ? decoration : decoration.substr(colon + 1), false);
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
		case WinMin: case WinMax: case WinClose: return kWinButtonW;
		default: return kButtonW;
		}
	};
	auto place = [&](Item& it, float left) {
		it.rect = rectF(left, by, left + widthOf(it), by + kButtonH);
		it.shown = true;
	};

	// The window's buttons on their sides.
	float leftX = 8, x = w - 8;
	for (Item& it : items) {
		if ((it.kind == WinMin || it.kind == WinMax || it.kind == WinClose) && it.leftSide) { place(it, leftX); leftX += kWinButtonW + 2; }
	}
	for (int i = (int)items.size() - 1; i >= 0; i--) {
		Item& it = items[i];
		if ((it.kind == WinMin || it.kind == WinMax || it.kind == WinClose) && !it.leftSide) { x -= kWinButtonW; place(it, x); x -= 2; }
	}
	if (x < w - 8) x -= 10;
	for (Item& it : items) if (it.kind == More) { x -= kButtonW; place(it, x); }
	x -= 8;
	for (int g = 14; g >= 10; g--) {
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
	x = leftX > 8 ? leftX + 8 : 10;
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
	for (Item& it : items)
		if (it.kind == Title) it.rect.right = std::max(it.rect.left + 40, std::min(it.rect.right, rightStart - 8));

	std::vector<Tip> tips;
	for (const Item& it : items)
		if (it.shown && it.kind != Speed) tips.push_back({ it.rect, tipFor(it) });
	setTips(tips);
}

bool Toolbar::isOn(const Item& it) const {
	if (it.kind != Button) return false;
	if (is(it.action, "win.running")) return !win->running();
	if (is(it.action, "win.sim-view")) return win->simView();
	if (is(it.action, "win.lock")) return win->locked();
	return false;
}

bool Toolbar::isEnabled(const Item& it) const {
	if (it.kind != Button) return true;
	return win->actionEnabled(it.action);
}

const char* Toolbar::iconFor(const Item& it) const {
	switch (it.kind) {
	case More: return Icon::More;
	case WinMin: return Icon::Minimize;
	case WinMax: return gtk_window_is_maximized(topWindow()) ? Icon::Restore : Icon::Maximize;
	case WinClose: return Icon::Close;
	default: break;
	}
	const char* a = it.action;
	if (is(a, "app.new")) return Icon::Page;
	if (is(a, "app.open")) return Icon::Open;
	if (is(a, "win.save")) return Icon::Save;
	if (is(a, "win.undo")) return Icon::Undo;
	if (is(a, "win.redo")) return Icon::Redo;
	if (is(a, "win.copy")) return Icon::Copy;
	if (is(a, "win.paste")) return Icon::Paste;
	if (is(a, "win.zoom-out")) return Icon::ZoomOut;
	if (is(a, "win.zoom-in")) return Icon::ZoomIn;
	if (is(a, "win.running")) return win->running() ? Icon::Pause : Icon::Play;
	if (is(a, "win.step")) return Icon::Step;
	if (is(a, "win.sim-view")) return win->simView() ? Icon::StopSolid : Icon::Play;
	if (is(a, "win.lock")) return win->locked() ? Icon::Lock : Icon::Unlock;
	if (is(a, "win.new-tab")) return Icon::NewTab;
	return nullptr;
}

std::string Toolbar::tipFor(const Item& it) const {
	switch (it.kind) {
	case Zoom: return "Zoom level. Click for 100%";
	case More: return "Every menu (F10)";
	case WinMin: return "Minimize";
	case WinMax: return "Maximize";
	case WinClose: return "Close";
	case Title: return "This circuit";
	default: break;
	}
	const char* a = it.action;
	if (is(a, "app.new")) return "New circuit (Ctrl+N)";
	if (is(a, "app.open")) return "Open (Ctrl+O)";
	if (is(a, "win.save")) return "Save (Ctrl+S)";
	if (is(a, "win.undo")) return "Undo (Ctrl+Z)";
	if (is(a, "win.redo")) return "Redo (Ctrl+Shift+Z)";
	if (is(a, "win.copy")) return "Copy (Ctrl+C)";
	if (is(a, "win.paste")) return "Paste (Ctrl+V)";
	if (is(a, "win.zoom-out")) return "Zoom out (Ctrl+-)";
	if (is(a, "win.zoom-in")) return "Zoom in (Ctrl+=)";
	if (is(a, "win.running")) return "Pause or resume the simulation";
	if (is(a, "win.step")) return "Step once (Ctrl+Shift+R)";
	if (is(a, "win.sim-view")) return "Simulation View (Ctrl+R)";
	if (is(a, "win.lock")) return "Lock the circuit so it can't be edited";
	if (is(a, "win.new-tab")) return "New tab (Ctrl+T)";
	return std::string();
}

RectF Toolbar::speedTrack(const Item& it) const {
	const float cy = (it.rect.top + it.rect.bottom) / 2;
	const float left = it.rect.left + 6 + 16 + 6;
	return rectF(left, cy - 10, left + kTrackW, cy + 10);
}

void Toolbar::setSpeedAt(const Item& it, float x) {
	const RectF t = speedTrack(it);
	win->setStepMs(speedFromFraction((x - t.left) / (t.right - t.left)));
	redraw();
}

int Toolbar::itemAt(float x, float y) const {
	for (int i = 0; i < (int)items.size(); i++)
		if (items[i].shown && inRect(items[i].rect, x, y)) return i;
	return -1;
}

void Toolbar::paint(cairo_t* cr, float w, float h) {
	if (relayout || w != lastW) layout(w, h);
	const Chrome c = chrome();
	const bool dark = c.dark;
	const bool active = gtk_window_is_active(topWindow());
	fillRect(cr, rectF(0, 0, w, h), c.bar());
	fillRect(cr, rectF(0, h - 1, w, h), dark ? colorF(0, 0, 0, 0.35f) : colorF(0, 0, 0, 0.08f));
	const Color ink = c.barInk();
	const Color accent = c.accent();
	const Color green = dark ? rgb255(64, 214, 110) : rgb255(36, 168, 76);

	// The capsules behind each group.
	for (int g = 1; g <= 14; g++) {
		RectF u = rectF(1e9f, 1e9f, -1e9f, -1e9f);
		bool any = false;
		for (const Item& it : items) {
			if (it.group != g || !it.shown) continue;
			any = true;
			u.left = std::min(u.left, it.rect.left); u.top = std::min(u.top, it.rect.top);
			u.right = std::max(u.right, it.rect.right); u.bottom = std::max(u.bottom, it.rect.bottom);
		}
		if (!any) continue;
		const RectF cap = rectF(u.left - 3, u.top - 1, u.right + 3, u.bottom + 1);
		const float r = (cap.bottom - cap.top) / 2;
		fillRound(cr, cap, r, dark ? colorF(1, 1, 1, 0.063f) : colorF(0, 0, 0, 0.043f));
		strokeRound(cr, cap, r, dark ? colorF(1, 1, 1, 0.07f) : colorF(0, 0, 0, 0.063f));
	}

	for (int i = 0; i < (int)items.size(); i++) {
		const Item& it = items[i];
		if (!it.shown) continue;
		const bool isHot = i == hot && isEnabled(it), isPressed = i == pressed && isHot;
		const RectF r = it.rect;
		switch (it.kind) {
		case Title: {
			if (isHot) fillRound(cr, r, 7, withAlpha(ink, 0.08f));
			const float tw = std::min(textWidth(win->titleText(), 13, true), r.right - r.left - 30);
			const float ty = (r.top + r.bottom) / 2 - 9;
			drawText(cr, win->titleText(), rectF(r.left + 8, ty, r.left + 8 + tw + 1, ty + 20), 13, withAlpha(ink, active ? 1.0f : 0.6f),
			         TextAlign::Leading, true);
			drawIcon(cr, Icon::ChevronDown, rectF(r.left + 10 + tw, r.top, r.left + 24 + tw, r.bottom), 10, withAlpha(ink, isHot ? 0.7f : 0.35f));
			break;
		}
		case Zoom: {
			if (isHot) fillRound(cr, rectF(r.left + 2, r.top, r.right - 2, r.bottom), 7, withAlpha(ink, 0.08f));
			Canvas* cv = win->currentCanvas();
			const float ty = (r.top + r.bottom) / 2 - 8;
			drawText(cr, format("%d%%", cv ? cv->zoomPercent() : 100), rectF(r.left, ty, r.right, ty + 18), 12, withAlpha(ink, 0.92f),
			         TextAlign::Center);
			break;
		}
		case Speed: {
			const float cy = (r.top + r.bottom) / 2;
			drawGaugeIcon(cr, rectF(r.left + 4, r.top, r.left + 24, r.bottom), withAlpha(ink, 0.9f));
			const RectF t = speedTrack(it);
			const float f = (float)speedFraction(win->stepMs());
			fillRound(cr, rectF(t.left, cy - 1.5f, t.right, cy + 1.5f), 1.5f, withAlpha(ink, 0.16f));
			fillRound(cr, rectF(t.left, cy - 1.5f, t.left + std::max(3.0f, kTrackW * f), cy + 1.5f), 1.5f, withAlpha(ink, 0.7f));
			const PointF knob = pointF(t.left + kTrackW * f, cy);
			fillCircle(cr, pointF(knob.x, knob.y + 0.5f), 7, colorF(0, 0, 0, 0.18f));
			fillCircle(cr, knob, 6.5f, colorF(1, 1, 1, 1));
			strokeCircle(cr, knob, 6, colorF(0, 0, 0, 0.2f), 1);
			break;
		}
		case WinMin: case WinMax: case WinClose: {
			// The desktop's round buttons (Adwaita's look).
			const PointF mid = pointF((r.left + r.right) / 2, (r.top + r.bottom) / 2);
			if (isHot || isPressed) fillCircle(cr, mid, 12, withAlpha(ink, isPressed ? 0.2f : 0.12f));
			else fillCircle(cr, mid, 12, withAlpha(ink, 0.06f));
			drawIcon(cr, iconFor(it), r, 14, withAlpha(ink, active ? 0.9f : 0.5f));
			break;
		}
		default: {
			const bool on = isOn(it);
			const bool colored = is(it.action, "win.sim-view");
			if (on || isHot) {
				const Color onColor = colored ? green : accent;
				const Color bg = on ? withAlpha(onColor, isPressed ? 0.30f : (isHot ? 0.24f : 0.18f))
				                    : (dark ? colorF(1, 1, 1, isPressed ? 0.13f : 0.08f) : colorF(0, 0, 0, isPressed ? 0.094f : 0.05f));
				fillRound(cr, rectF(r.left + 2, r.top + 1, r.right - 2, r.bottom - 1), 7, bg);
			}
			Color fg = withAlpha(ink, isEnabled(it) ? 0.92f : 0.3f);
			if (colored) fg = green;
			else if (on) fg = accent;
			if (is(it.action, "app.new")) drawNewDocIcon(cr, r, fg);
			else if (is(it.action, "win.new-tab")) drawNewTabIcon(cr, r, fg);
			else if (is(it.action, "app.open")) drawFolderIcon(cr, r, fg);
			else if (is(it.action, "win.save")) drawSaveIcon(cr, r, fg);
			else if (is(it.action, "win.zoom-in") || is(it.action, "win.zoom-out")) drawZoomIcon(cr, r, fg, is(it.action, "win.zoom-in"));
			else drawIcon(cr, iconFor(it), r, colored && on ? 13 : 16, fg);
			break;
		}
		}
	}
}

GdkRectangle Toolbar::actionRect(const char* action) const {
	GdkRectangle out = { 0, 0, 0, 0 };
	for (const Item& it : items) {
		if (it.kind != Button || !is(it.action, action)) continue;
		int x = 0, y = 0;
		if (GdkWindow* gw = gtk_widget_get_window(area)) gdk_window_get_origin(gw, &x, &y);
		GtkAllocation a;
		gtk_widget_get_allocation(area, &a);
		out = { x + a.x + (int)it.rect.left, y + a.y + (int)it.rect.top, (int)rectWidth(it.rect), (int)rectHeight(it.rect) };
	}
	return out;
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

void Toolbar::mouseDown(int button, float x, float y, bool doubleClick, GdkEventButton* e) {
	const int i = itemAt(x, y);
	// The bar's empty parts are the title bar.
	if (i < 0) {
		if (button == 1 && doubleClick) {
			if (gtk_window_is_maximized(topWindow())) gtk_window_unmaximize(topWindow());
			else gtk_window_maximize(topWindow());
		} else if (button == 1) {
			gtk_window_begin_move_drag(topWindow(), (int)e->button, (int)e->x_root, (int)e->y_root, e->time);
		} else if (button == 3) {
			if (GdkWindow* gw = gtk_widget_get_window(GTK_WIDGET(topWindow()))) gdk_window_show_window_menu(gw, (GdkEvent*)e);
		}
		return;
	}
	if (button != 1) return;
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
		pressed = -1;
		GdkRectangle anchor = { (int)items[i].rect.left, (int)items[i].rect.top, (int)rectWidth(items[i].rect), (int)rectHeight(items[i].rect) };
		if (items[i].kind == Title) win->titleMenu(area, anchor, (GdkEvent*)e);
		else win->moreMenu(area, anchor, (GdkEvent*)e);
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

void Toolbar::activate(int index) {
	const Item& it = items[index];
	switch (it.kind) {
	case Zoom: win->runAction("win.zoom-actual"); break;
	case WinMin: gtk_window_iconify(topWindow()); break;
	case WinMax:
		if (gtk_window_is_maximized(topWindow())) gtk_window_unmaximize(topWindow());
		else gtk_window_maximize(topWindow());
		break;
	case WinClose: gtk_window_close(topWindow()); break;
	case Button: if (isEnabled(it)) win->runAction(it.action); break;
	default: break;
	}
}
