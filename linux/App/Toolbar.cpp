// The toolbar (see Toolbar.h).

#include "Toolbar.h"
#include "Canvas.h"
#include "Window.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// Sizes in points, as the Mac's TBButton and TBSpeed.
const float kButtonW = 34, kButtonH = 30, kGroupGap = 12, kZoomW = 48, kSpeedW = 92, kTrackW = 58;
enum { kClassic = 0, kMinimal = 2, kSeamless = 3 };

bool is(const char* a, const char* b) { return a && b && std::strcmp(a, b) == 0; }

bool groupShown(int g) { return g < 0 || (prefs().toolbarHidden & (1 << g)) == 0; }

}  // namespace

const char* toolGroupName(int g) {
	static const char* names[] = { "New, Open, Save", "Undo and Redo", "Copy and Paste", "Zoom", "Pause, Step, Speed",
	                               "Run (Simulation View)", "Lock", "Dark mode", "New tab", "Send feedback" };
	return g >= 0 && g < TGCount ? names[g] : "";
}

Toolbar::Toolbar(CircuitWindow* window) : win(window) {
	create();
	gtk_widget_set_size_request(area, -1, (int)barHeight());
	fade.in = 0.12;
	fade.out = 0.15;
}

GtkWindow* Toolbar::topWindow() const { return win->window(); }

void Toolbar::build(int style) {
	items.clear();
	auto add = [&](Kind k, const char* action, int group, bool right) { items.push_back(Item{ k, action, group, right }); };
	if (style == kMinimal) {
		add(Title, nullptr, -1, false);
		add(Button, "win.undo", TGUndo, false); add(Button, "win.redo", TGUndo, false);
		add(Button, "win.running", TGSim, true);
		add(Button, "win.sim-view", TGRun, true);
		add(More, nullptr, -1, true);
		return;
	}
	if (prefs().showTitle) add(Title, nullptr, -1, false);
	add(Button, "app.new", TGFile, false); add(Button, "app.open", TGFile, false); add(Button, "win.save", TGFile, false);
	add(Button, "win.undo", TGUndo, false); add(Button, "win.redo", TGUndo, false);
	add(Button, "win.copy", TGClipboard, false); add(Button, "win.paste", TGClipboard, false);
	add(Button, "win.zoom-out", TGZoom, false); add(Zoom, nullptr, TGZoom, false); add(Button, "win.zoom-in", TGZoom, false);
	add(Button, "win.running", TGSim, true); add(Button, "win.step", TGSim, true); add(Speed, nullptr, TGSim, true);
	add(Button, "win.sim-view", TGRun, true);
	add(Button, "win.lock", TGLock, true);
	if (prefs().showThemeToggle) add(Button, "win.dark", TGTheme, true);
	add(Button, "win.new-tab", TGTab, true);
	add(Button, "win.feedback", TGFeedback, true);
	add(More, nullptr, -1, true);
}

void Toolbar::layout(float w, float h, int style, bool live) {
	relayout = false;
	lastW = w;
	laidStyle = style;
	build(style);
	buttons.layout(w, h);
	const float by = std::floor((h - kButtonH) / 2);
	auto widthOf = [&](const Item& it) -> float {
		switch (it.kind) {
		case Zoom: return kZoomW;
		case Speed: return kSpeedW;
		case Title: return std::min(220.0f, textWidth(win->titleText(), 13, true)) + 30;
		default: return kButtonW;
		}
	};
	auto place = [&](Item& it, float left) {
		it.rect = rectF(left, by, left + widthOf(it), by + kButtonH);
		it.shown = groupShown(it.group);
	};
	const float leftEdge = live && buttons.leftRoom() > 0 ? buttons.leftRoom() : 10;
	const float rightEdge = w - (live && buttons.rightRoom() > 0 ? buttons.rightRoom() - 4 : 10);

	// From the right edge: •••, then the groups, last first.
	float x = rightEdge;
	int lastGroup = -2;
	for (int i = (int)items.size() - 1; i >= 0; i--) {
		Item& it = items[i];
		if (!it.right) continue;
		if (!groupShown(it.group)) { it.shown = false; continue; }
		if (it.group != lastGroup && lastGroup != -2) x -= style == kMinimal ? 6 : kGroupGap;
		lastGroup = it.group;
		x -= widthOf(it);
		place(it, x);
	}
	const float rightStart = x;

	if (style == kMinimal) {
		// The name in the middle, with the page under it.
		x = leftEdge;
		for (Item& it : items) {
			if (it.right) continue;
			if (it.kind == Title) {
				const float tw = std::min(260.0f, std::max(textWidth(win->titleText(), 13, true), textWidth(win->pageName(win->currentPage()), 10.5f)) + 30);
				it.rect = rectF((w - tw) / 2, by - 4, (w + tw) / 2, by + kButtonH + 4);
				it.shown = prefs().showTitle;
				continue;
			}
			place(it, x);
			x += widthOf(it);
		}
	} else {
		// From the left: the name, then the groups while they fit; the ones
		// that don't are left out, last first (everything is in ••• too).
		x = leftEdge;
		lastGroup = -2;
		std::vector<std::pair<int, float>> groupEnds;
		for (Item& it : items) {
			if (it.right) continue;
			if (!groupShown(it.group)) { it.shown = false; continue; }
			if (it.kind == Title) { place(it, x); x += widthOf(it) + kGroupGap - 6; continue; }
			if (it.group != lastGroup && lastGroup != -2) x += kGroupGap;
			lastGroup = it.group;
			place(it, x);
			x += widthOf(it);
			groupEnds.push_back({ it.group, x });
		}
		for (const auto& ge : groupEnds)
			if (ge.second > rightStart - 8)
				for (Item& it : items) if (!it.right && it.group == ge.first) it.shown = false;
		for (Item& it : items)
			if (it.kind == Title) it.rect.right = std::max(it.rect.left + 40, std::min(it.rect.right, rightStart - 8));
	}

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
	if (is(it.action, "win.dark")) return prefs().dark;
	return false;
}

bool Toolbar::isEnabled(const Item& it) const {
	if (it.kind != Button) return true;
	return win->actionEnabled(it.action);
}

const char* Toolbar::iconFor(const Item& it) const {
	if (it.kind == More) return Icon::More;
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
	if (is(a, "win.dark")) return prefs().dark ? "weather-clear-night-symbolic" : "weather-clear-symbolic";
	if (is(a, "win.new-tab")) return Icon::NewTab;
	if (is(a, "win.feedback")) return Icon::Feedback;
	return nullptr;
}

std::string Toolbar::tipFor(const Item& it) const {
	switch (it.kind) {
	case Zoom: return "Zoom level. Click for 100%";
	case More: return "Every menu (F10)";
	case Title: return "This circuit: rename it, see its versions";
	default: break;
	}
	const char* a = it.action;
	if (is(a, "app.new")) return "New circuit (Ctrl+N)";
	if (is(a, "app.open")) return "Your circuits (Ctrl+O)";
	if (is(a, "win.save")) return "Save, and keep a version (Ctrl+S)";
	if (is(a, "win.undo")) return "Undo (Ctrl+Z)";
	if (is(a, "win.redo")) return "Redo (Ctrl+Shift+Z)";
	if (is(a, "win.copy")) return "Copy (Ctrl+C)";
	if (is(a, "win.paste")) return "Paste (Ctrl+V)";
	if (is(a, "win.zoom-out")) return "Zoom out (Ctrl+-)";
	if (is(a, "win.zoom-in")) return "Zoom in (Ctrl+=)";
	if (is(a, "win.running")) return win->running() ? "Pause the simulation" : "Resume the simulation";
	if (is(a, "win.step")) return "Step once (Ctrl+Shift+R)";
	if (is(a, "win.sim-view")) return win->simView() ? "Leave Simulation View (Esc)" : "Simulation View (Ctrl+R)";
	if (is(a, "win.lock")) return win->locked() ? "Unlock the circuit" : "Lock the circuit so it can't be edited";
	if (is(a, "win.dark")) return "Dark mode (Ctrl+Shift+D)";
	if (is(a, "win.new-tab")) return "New tab (Ctrl+T)";
	if (is(a, "win.feedback")) return "Send feedback: a bug, an idea, anything";
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
	const int style = prefs().toolbarStyle;
	if (relayout || w != lastW || style != laidStyle) layout(w, h, style);
	draw(cr, w, h, style, true);
}

void Toolbar::paintPicture(cairo_t* cr, float w, float h, int style) {
	const int keepStyle = laidStyle;
	const float keepW = lastW;
	std::vector<Item> keep = items;
	layout(w, h, style, false);
	draw(cr, w, h, style, false);
	items = keep;
	laidStyle = keepStyle;
	lastW = keepW;
	relayout = true;
}

void Toolbar::draw(cairo_t* cr, float w, float h, int style, bool live) {
	const Chrome c = chrome();
	const bool dark = c.dark;
	const bool active = !live || gtk_window_is_active(topWindow());
	const bool quiet = style == kSeamless;
	Color bg = style == kClassic ? c.bar() : style == kMinimal ? c.minimalBar() : c.canvas();
	if (style == kSeamless && win->simView()) bg = fromRGBA(Palette{ true, true }.canvas());
	fillRect(cr, rectF(0, 0, w, h), bg);
	if (style != kSeamless) fillRect(cr, rectF(0, h - 1, w, h), dark ? colorF(0, 0, 0, 0.35f) : colorF(0, 0, 0, 0.08f));
	const Chrome ic{ dark || (style == kSeamless && win->simView()) };
	const Color ink = ic.barInk();
	const Color accent = c.accent();
	const Color green = ic.dark ? rgb255(64, 214, 110) : rgb255(36, 168, 76);

	// Classic groups related tools in a soft capsule, the way macOS does.
	if (style == kClassic) {
		for (int g = 0; g < TGCount; g++) {
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
	}

	for (int i = 0; i < (int)items.size(); i++) {
		const Item& it = items[i];
		if (!it.shown) continue;
		const float hov = live && isEnabled(it) ? (float)fade.amount(i) : 0;
		const bool isPressed = live && i == pressed;
		const RectF r = it.rect;
		const float idle = quiet ? 0.5f : 0.92f;
		const float inkA = idle + (0.92f - idle) * hov;
		switch (it.kind) {
		case Title: {
			if (hov > 0.01f) fillRound(cr, r, 7, withAlpha(ink, 0.08f * hov));
			if (style == kMinimal) {
				const float tw = std::min(textWidth(win->titleText(), 13, true), r.right - r.left - 30);
				drawTextMid(cr, win->titleText(), rectF(r.left, r.top + 3, r.right - 14, r.top + 21), 13, withAlpha(ink, active ? 1.0f : 0.6f),
				            TextAlign::Center, true);
				drawTextMid(cr, win->pageName(win->currentPage()), rectF(r.left, r.top + 20, r.right - 14, r.bottom - 3), 10.5f,
				            withAlpha(ink, 0.55f), TextAlign::Center);
				drawIcon(cr, Icon::ChevronDown, rectF((r.left + r.right + tw) / 2 - 4, r.top + 4, (r.left + r.right + tw) / 2 + 10, r.top + 20), 10,
				         withAlpha(ink, 0.35f + 0.35f * hov));
				break;
			}
			const float tw = std::min(textWidth(win->titleText(), 13, true), r.right - r.left - 30);
			drawTextMid(cr, win->titleText(), rectF(r.left + 8, r.top, r.left + 8 + tw + 1, r.bottom), 13, withAlpha(ink, active ? 1.0f : 0.6f),
			            TextAlign::Leading, true);
			drawIcon(cr, Icon::ChevronDown, rectF(r.left + 10 + tw, r.top, r.left + 24 + tw, r.bottom), 10, withAlpha(ink, 0.35f + 0.35f * hov));
			break;
		}
		case Zoom: {
			if (hov > 0.01f) fillRound(cr, rectF(r.left + 2, r.top, r.right - 2, r.bottom), 7, withAlpha(ink, 0.08f * hov));
			Canvas* cv = win->currentCanvas();
			drawTextMid(cr, format("%d%%", cv ? cv->zoomPercent() : 100), r, 12, withAlpha(ink, inkA), TextAlign::Center);
			break;
		}
		case Speed: {
			const float cy = (r.top + r.bottom) / 2;
			const float sh = live && hot == i ? 1.0f : 0.0f;
			drawGaugeIcon(cr, rectF(r.left + 4, r.top, r.left + 24, r.bottom), withAlpha(ink, quiet && sh == 0 ? 0.5f : 0.9f));
			const RectF t = speedTrack(it);
			const float f = (float)speedFraction(win->stepMs());
			fillRound(cr, rectF(t.left, cy - 1.5f, t.right, cy + 1.5f), 1.5f, withAlpha(ink, 0.16f));
			fillRound(cr, rectF(t.left, cy - 1.5f, t.left + std::max(3.0f, kTrackW * f), cy + 1.5f), 1.5f, withAlpha(ink, quiet ? 0.5f : 0.7f));
			const PointF knob = pointF(t.left + kTrackW * f, cy);
			fillCircle(cr, pointF(knob.x, knob.y + 0.5f), 7, colorF(0, 0, 0, 0.18f));
			fillCircle(cr, knob, 6.5f, colorF(1, 1, 1, 1));
			strokeCircle(cr, knob, 6, colorF(0, 0, 0, 0.2f), 1);
			break;
		}
		default: {
			const bool on = isOn(it);
			const bool colored = is(it.action, "win.sim-view");
			const float bgA = on ? 1.0f : hov;
			if (bgA > 0.01f) {
				const Color onColor = colored ? green : accent;
				const Color bgc = on ? withAlpha(onColor, isPressed ? 0.30f : (0.18f + 0.06f * hov))
				                     : (ic.dark ? colorF(1, 1, 1, (isPressed ? 0.13f : 0.08f) * bgA) : colorF(0, 0, 0, (isPressed ? 0.094f : 0.05f) * bgA));
				fillRound(cr, rectF(r.left + 2, r.top + 1, r.right - 2, r.bottom - 1), 7, bgc);
			}
			Color fg = withAlpha(ink, isEnabled(it) ? inkA : 0.3f);
			if (colored) fg = green;
			else if (on) fg = accent;
			if (is(it.action, "app.new")) drawNewDocIcon(cr, r, fg);
			else if (is(it.action, "win.new-tab")) drawNewTabIcon(cr, r, fg);
			else if (is(it.action, "win.feedback")) drawBubbleIcon(cr, r, fg);
			else if (is(it.action, "app.open")) drawFolderIcon(cr, r, fg);
			else if (is(it.action, "win.save")) drawSaveIcon(cr, r, fg);
			else if (is(it.action, "win.zoom-in") || is(it.action, "win.zoom-out")) drawZoomIcon(cr, r, fg, is(it.action, "win.zoom-in"));
			else drawIcon(cr, iconFor(it), r, colored && on ? 13 : (it.kind == More ? 15 : 16), fg);
			break;
		}
		}
	}
	if (live) buttons.paint(cr, topWindow(), ink);
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
	const int b = buttons.at(x, y);
	buttons.setHot(b);
	const int i = b >= 0 ? -1 : itemAt(x, y);
	if (i != hot) { hot = i; fade.setHot(i); }
	animate();
}

void Toolbar::mouseLeave() {
	if (draggingSpeed) return;
	hot = -1;
	fade.setHot(-1);
	buttons.setHot(-1);
	animate();
}

void Toolbar::mouseDown(int button, float x, float y, bool doubleClick, GdkEventButton* e) {
	const int b = buttons.at(x, y);
	if (b >= 0) {
		if (button == 1) { buttons.pressed = b; redraw(); }
		return;
	}
	const int i = itemAt(x, y);
	// The bar's empty parts are the title bar.
	if (i < 0) { titleRowPress(topWindow(), e); (void)doubleClick; return; }
	if (button != 1) return;
	if (items[i].kind == Speed) {
		draggingSpeed = true;
		setSpeedAt(items[i], x);
		return;
	}
	pressed = i;
	redraw();
	// Menus open on the press, as menus do, under their button.
	if (items[i].kind == Title || items[i].kind == More) {
		pressed = -1;
		GdkRectangle anchor = { (int)items[i].rect.left, (int)items[i].rect.top, (int)rectWidth(items[i].rect), (int)rectHeight(items[i].rect) };
		if (items[i].kind == Title) win->titleMenu(area, anchor, (GdkEvent*)e);
		else win->moreMenu(area, anchor, (GdkEvent*)e);
		hot = -1;
		fade.setHot(-1);
		animate();
	}
}

void Toolbar::mouseUp(int button, float x, float y) {
	if (button != 1) return;
	if (buttons.pressed >= 0) {
		const int b = buttons.pressed;
		buttons.pressed = -1;
		if (buttons.at(x, y) == b) buttons.activate(b, topWindow());
		redraw();
		return;
	}
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
	case Button: if (isEnabled(it)) win->runAction(it.action); break;
	default: break;
	}
}
