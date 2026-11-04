// The welcome, the lesson ("Learn Logic: Guided Tour") and What's New (see
// Welcome.h), as the Mac app's Welcome.swift and WhatsNew.swift and the
// Windows app's: in the
// brand's look (Brand.h), with the wx and Mac page slide -- the new page
// drifts in as it fades up, the old one away as it fades out.

#include "Welcome.h"
#include "Brand.h"
#include "Canvas.h"
#include "Settings.h"
#include "Actions.h"
#include "Shortcuts.h"
#include "ShowMeAround.h"
#include "Sync.h"
#include "Window.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

using namespace brand;

namespace {

double nowSeconds() { return g_get_monotonic_time() / 1e6; }

bool windowAlive(CircuitWindow* w) {
	return w && std::find(circuitWindows().begin(), circuitWindows().end(), w) != circuitWindows().end();
}

bool reduceMotion() {
	gboolean animations = TRUE;
	if (GtkSettings* s = gtk_settings_get_default()) g_object_get(s, "gtk-enable-animations", &animations, nullptr);
	return !animations;
}

// ---- A brand window -----------------------------------------------------------
// A plain window whose drawing, clicks and keys lambdas handle; it can be
// dragged by any part that isn't a control. It redraws every frame while
// `animating` says so.

struct Hit { RectF r; int id; };

struct Panel {
	GtkWidget* win = nullptr;
	GtkWidget* area = nullptr;
	GtkWidget* overlay = nullptr;
	std::vector<Hit> hits;            // this frame's controls
	int hot = -1, pressed = -1;
	bool composited = false;
	guint tickId = 0;
	std::function<void(cairo_t*, float, float)> paint;
	std::function<void(int id)> click;
	std::function<bool(guint key, guint state)> key;
	std::function<bool()> animating;

	void addHit(const RectF& r, int id) { hits.push_back({ r, id }); }
	int hitAt(float x, float y) const {
		for (auto it = hits.rbegin(); it != hits.rend(); ++it) if (inRect(it->r, x, y)) return it->id;
		return -1;
	}
	void redraw() { if (area) gtk_widget_queue_draw(area); }

	void create(GtkWindow* owner, int w, int h) {
		win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
		gtk_window_set_title(GTK_WINDOW(win), "CedarLogic");
		gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
		gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
		gtk_window_set_default_size(GTK_WINDOW(win), w, h);
		gtk_window_set_skip_taskbar_hint(GTK_WINDOW(win), TRUE);
		if (owner) {
			gtk_window_set_transient_for(GTK_WINDOW(win), owner);
			gtk_window_set_modal(GTK_WINDOW(win), TRUE);
			gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER_ON_PARENT);
		} else {
			gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER);
		}
		GdkScreen* screen = gtk_widget_get_screen(win);
		GdkVisual* rgba = gdk_screen_get_rgba_visual(screen);
		composited = rgba && gdk_screen_is_composited(screen);
		if (composited) {
			gtk_widget_set_visual(win, rgba);
			gtk_widget_set_app_paintable(win, TRUE);
		}
		overlay = gtk_overlay_new();
		area = gtk_drawing_area_new();
		gtk_widget_set_can_focus(area, TRUE);
		gtk_widget_add_events(area, GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
		gtk_container_add(GTK_CONTAINER(overlay), area);
		gtk_container_add(GTK_CONTAINER(win), overlay);
		g_signal_connect(area, "draw", G_CALLBACK(drawCb), this);
		g_signal_connect(area, "motion-notify-event", G_CALLBACK(motionCb), this);
		g_signal_connect(area, "leave-notify-event", G_CALLBACK(leaveCb), this);
		g_signal_connect(area, "button-press-event", G_CALLBACK(pressCb), this);
		g_signal_connect(area, "button-release-event", G_CALLBACK(releaseCb), this);
		g_signal_connect(win, "key-press-event", G_CALLBACK(keyCb), this);
		g_signal_connect(win, "delete-event", CL_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer self) -> gboolean {
			Panel* p = static_cast<Panel*>(self);
			if (p->key) p->key(GDK_KEY_Escape, 0);
			return TRUE;
		}), this);
		tickId = gtk_widget_add_tick_callback(area, tickCb, this, nullptr);
	}
	void show() {
		gtk_widget_show_all(win);
		gtk_window_present(GTK_WINDOW(win));
		gtk_widget_grab_focus(area);
	}
	void destroy() {
		if (win == nullptr) return;
		gtk_widget_remove_tick_callback(area, tickId);
		g_signal_handlers_disconnect_by_data(area, this);
		g_signal_handlers_disconnect_by_data(win, this);
		gtk_widget_destroy(win);
		win = area = overlay = nullptr;
	}

	static gboolean drawCb(GtkWidget* w, cairo_t* cr, gpointer self) {
		Panel* p = static_cast<Panel*>(self);
		guarded("a window", [&] {
			const float ww = gtk_widget_get_allocated_width(w), wh = gtk_widget_get_allocated_height(w);
			if (p->composited) {
				cairo_save(cr);
				cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
				cairo_set_source_rgba(cr, 0, 0, 0, 0);
				cairo_paint(cr);
				cairo_restore(cr);
				roundedPath(cr, rectF(0, 0, ww, wh), 14);
				cairo_clip(cr);
			}
			p->hits.clear();
			if (p->paint) p->paint(cr, ww, wh);
			if (p->composited) strokeRound(cr, rectF(0.5f, 0.5f, ww - 0.5f, wh - 0.5f), 13.5f, colorF(1, 1, 1, 0.1f));
		});
		return TRUE;
	}
	static gboolean motionCb(GtkWidget* w, GdkEventMotion* e, gpointer self) {
		Panel* p = static_cast<Panel*>(self);
		const int id = p->hitAt((float)e->x, (float)e->y);
		if (id != p->hot) {
			p->hot = id;
			GdkWindow* gw = gtk_widget_get_window(w);
			GdkCursor* c = id >= 0 ? gdk_cursor_new_from_name(gdk_window_get_display(gw), "pointer") : nullptr;
			gdk_window_set_cursor(gw, c);
			if (c) g_object_unref(c);
			p->redraw();
		}
		return TRUE;
	}
	static gboolean leaveCb(GtkWidget*, GdkEventCrossing*, gpointer self) {
		Panel* p = static_cast<Panel*>(self);
		if (p->hot != -1) { p->hot = -1; p->redraw(); }
		return FALSE;
	}
	static gboolean pressCb(GtkWidget* w, GdkEventButton* e, gpointer self) {
		Panel* p = static_cast<Panel*>(self);
		gtk_widget_grab_focus(w);
		if (e->type != GDK_BUTTON_PRESS || e->button != 1) return TRUE;
		p->pressed = p->hitAt((float)e->x, (float)e->y);
		// Dragged by its ground; the controls take clicks.
		if (p->pressed < 0)
			gtk_window_begin_move_drag(GTK_WINDOW(p->win), (gint)e->button, (gint)e->x_root, (gint)e->y_root, e->time);
		return TRUE;
	}
	static gboolean releaseCb(GtkWidget*, GdkEventButton* e, gpointer self) {
		Panel* p = static_cast<Panel*>(self);
		if (e->button != 1) return TRUE;
		const int id = p->hitAt((float)e->x, (float)e->y);
		const int was = p->pressed;
		p->pressed = -1;
		if (id >= 0 && id == was && p->click) {
			auto click = p->click;   // the panel may go in it
			guarded("a click", [&] { click(id); });
		}
		return TRUE;
	}
	static gboolean keyCb(GtkWidget* w, GdkEventKey* e, gpointer self) {
		Panel* p = static_cast<Panel*>(self);
		// A text field keeps its own keys, but Return, Escape and Tab.
		GtkWidget* focus = gtk_window_get_focus(GTK_WINDOW(w));
		if (focus && GTK_IS_ENTRY(focus) && e->keyval != GDK_KEY_Return && e->keyval != GDK_KEY_KP_Enter && e->keyval != GDK_KEY_Escape &&
		    e->keyval != GDK_KEY_Tab)
			return FALSE;
		if (focus && GTK_IS_ENTRY(focus) && e->keyval == GDK_KEY_Tab) { gtk_widget_grab_focus(p->area); p->redraw(); return TRUE; }
		if (!p->key) return FALSE;
		auto key = p->key;
		return guarded("a key", [&] { return key(e->keyval, e->state); }) ? TRUE : FALSE;
	}
	static gboolean tickCb(GtkWidget*, GdkFrameClock*, gpointer self) {
		Panel* p = static_cast<Panel*>(self);
		if (p->animating && guarded("an animation", [&] { return p->animating(); })) p->redraw();
		return G_SOURCE_CONTINUE;
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
	void draw(cairo_t* cr, Panel& panel, const RectF& area, const std::function<void(int page)>& paintPage) {
		cairo_save(cr);
		cairo_rectangle(cr, area.left, area.top, area.right - area.left, area.bottom - area.top);
		cairo_clip(cr);
		const double e = slideCurve(progress());
		auto one = [&](int pg, float dx, float opacity, bool live) {
			if (opacity <= 0.003f) return;
			const size_t hitsBefore = panel.hits.size();
			cairo_save(cr);
			cairo_translate(cr, dx, 0);
			cairo_push_group(cr);
			paintPage(pg);
			cairo_pop_group_to_source(cr);
			cairo_paint_with_alpha(cr, opacity);
			cairo_restore(cr);
			// Only the page arriving takes clicks; its controls are where it's going.
			if (!live) panel.hits.resize(hitsBefore);
		};
		if (sliding()) {
			one(from, (float)((forward ? -60 : 60) * e), (float)(1 - e), false);
			one(page, (float)((forward ? 60 : -60) * (1 - e)), (float)e, true);
		} else {
			one(page, 0, 1, true);
		}
		cairo_restore(cr);
	}
};

// The footer: the page dots (the one you're on a neon capsule).
void dots(cairo_t* cr, float x, float cy, int count, int current) {
	for (int i = 0; i < count; i++) {
		const bool on = i == current;
		const float w = on ? 22.0f : 8.0f;
		const RectF r = rectF(x, cy - 4, x + w, cy + 4);
		if (on) glow(cr, r, 4, alpha(kNeon, 0.6f), 5);
		fillRound(cr, r, 4, on ? kNeon : colorF(1, 1, 1, 0.18f));
		x += w + 6;
	}
}

// An icon from the desktop's symbolic set, in neon.
void glyph(cairo_t* cr, const char* icon, float x, float y, float size, Color c = kNeon) {
	drawIcon(cr, icon, rectF(x, y, x + size * 1.6f, y + size * 1.6f), size, c);
}

// A card with an icon, a title and a line (the Mac's BrandCard rows).
float pointCard(cairo_t* cr, float x, float y, float w, const char* icon, const std::string& title, const std::string& line, float minH = 0,
                float extra = 0) {
	const float textW = w - 66 - 18;
	const float lh = text(nullptr, line, 0, 0, 12.5f, Normal, kSecondary, textW + 18);
	const float h = std::max(minH, 16 + 20 + 4 + lh + 16 + extra);
	card(cr, rectF(x, y, x + w, y + h));
	glyph(cr, icon, x + 14, y + 13, 15);
	text(cr, title, x + 48, y + 14, 14, Bold, kPrimary, textW + 18);
	text(cr, line, x + 48, y + 36, 12.5f, Normal, kSecondary, textW + 18);
	return h;
}

void runAction(CircuitWindow* w, const char* action) {
	if (windowAlive(w)) w->runAction(action);
}

RectF inflate(const RectF& r, float d) { return rectF(r.left - d, r.top - d, r.right + d, r.bottom + d); }

// ---- The Actions panel's switches and picture ----------------------------------------
// The welcome's page and What's New's "Show or Hide It" card (the Mac's
// ActionsPanelChoice): "Show the Actions panel" and "Open on hover" beside a
// small picture of a window with the panel in it. They change the real
// setting as they're flipped.

// A neon switch: a capsule with a knob, lit when on.
void brandSwitch(cairo_t* cr, float x, float y, bool on, bool hot) {
	const RectF r = rectF(x, y, x + 40, y + 23);
	if (on) glow(cr, r, 11.5f, alpha(kNeon, 0.45f), 7);
	fillRound(cr, r, 11.5f, on ? kNeon : colorF(1, 1, 1, hot ? 0.2f : 0.14f));
	strokeRound(cr, rectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f), 11, colorF(1, 1, 1, on ? 0.35f : 0.16f));
	fillCircle(cr, pointF(on ? r.right - 11.5f : r.left + 11.5f, y + 11.5f), 8.5f, on ? kInk : colorF(1, 1, 1, 0.85f));
}

// A small window, dark: the toolbar, the side panel, a gate and a light on
// the canvas, and (when on) the panel down the right edge, its first button
// named as the pointer rests on it. With `hover`, a pointer waits at the edge.
void panelPicture(cairo_t* cr, float x, float y, bool on, bool hover) {
	const float W = 300, H = 200;
	const Chrome ch{ true };
	const RectF win = rectF(x, y, x + W, y + H);
	fillRound(cr, rectF(win.left - 4, win.top + 4, win.right + 4, win.bottom + 12), 14, colorF(0, 0, 0, 0.25f));
	if (on) glow(cr, win, 10, alpha(kNeon, 0.12f), 16);
	cairo_save(cr);
	roundedPath(cr, win, 10);
	cairo_clip(cr);
	fillRect(cr, win, ch.canvas());
	// The toolbar.
	fillRect(cr, rectF(x, y, x + W, y + 22), ch.bar());
	for (int i = 0; i < 4; i++) fillRound(cr, rectF(x + 10 + i * 17, y + 7, x + 22 + i * 17, y + 15), 2, withAlpha(ch.barInk(), 0.35f));
	for (int i = 0; i < 3; i++) fillCircle(cr, pointF(x + W - 12 - i * 13, y + 11), 3.5f, withAlpha(ch.barInk(), 0.3f));
	cairo_move_to(cr, x + W - 54, y + 7);
	cairo_line_to(cr, x + W - 47, y + 11);
	cairo_line_to(cr, x + W - 54, y + 15);
	cairo_close_path(cr);
	setColor(cr, alpha(kNeon, 0.8f));
	cairo_fill(cr);
	// The side panel.
	const float top = y + 22;
	fillRect(cr, rectF(x, top, x + 54, y + H), ch.tabBar());
	fillRect(cr, rectF(x + 54, top, x + 55, y + H), ch.sash());
	for (int r = 0; r < 5; r++)
		for (int c = 0; c < 2; c++)
			strokeRound(cr, rectF(x + 7 + c * 23 + 0.5f, top + 8 + r * 21 + 0.5f, x + 25 + c * 23 - 0.5f, top + 24 + r * 21 - 0.5f), 3,
			            withAlpha(ch.barInk(), 0.3f));
	// The canvas: dots, two switches into a gate, and a light.
	const float cl = x + 55, cr2 = x + W - (on ? 30 : 0);
	setColor(cr, colorF(1, 1, 1, 0.12f));
	for (float dx = cl + 8; dx < cr2; dx += 12)
		for (float dy = top + 8; dy < y + H; dy += 12) cairo_rectangle(cr, dx, dy, 1, 1);
	cairo_fill(cr);
	const float cy = top + (H - 22) / 2, gx = (cl + cr2) / 2 - 26;
	for (float wy : { cy - 10, cy + 10 }) {
		drawLine(cr, pointF(cl + 18, wy), pointF(gx, wy), kNeon, 1.6f);
		strokeRound(cr, rectF(cl + 6, wy - 6, cl + 18, wy + 6), 3, kNeon, 1.3f);
	}
	drawLine(cr, pointF(gx + 40, cy), pointF(gx + 70, cy), kNeon, 1.6f);
	cairo_move_to(cr, gx, cy - 18);
	cairo_line_to(cr, gx + 18, cy - 18);
	cairo_curve_to(cr, gx + 42, cy - 18, gx + 42, cy + 18, gx + 18, cy + 18);
	cairo_line_to(cr, gx, cy + 18);
	cairo_close_path(cr);
	setColor(cr, alpha(kNeon, 0.12f));
	cairo_fill_preserve(cr);
	setColor(cr, kNeon);
	cairo_set_line_width(cr, 1.6);
	cairo_stroke(cr);
	const Color lamp = colorF(1, 0.77f, 0.25f);
	fillCircle(cr, pointF(gx + 78, cy), 13, alpha(lamp, 0.18f));
	fillCircle(cr, pointF(gx + 78, cy), 8, lamp);
	if (on) {
		// The panel, its first button named.
		const float rl = x + W - 30;
		fillRect(cr, rectF(rl, top, x + W, y + H), ch.canvas());
		fillRect(cr, rectF(rl, top, rl + 1, y + H), hover ? alpha(kNeon, 0.5f) : ch.sash());
		const std::vector<std::string>& ids = actions::defaults();
		for (size_t i = 0; i < ids.size(); i++) {
			const RectF b = rectF(rl + 4, top + 6 + i * 25, rl + 26, top + 27 + i * 25);
			if (i == 0) {
				fillRound(cr, b, 4, alpha(kNeon, 0.14f));
				strokeRound(cr, b, 4, alpha(kNeon, 0.9f), 1);
			}
			if (const actions::Item* a = actions::find(ids[i])) actions::drawIcon(cr, *a, b, 10, withAlpha(ch.barInk(), 0.9f));
		}
		const float tw = textWidth("Check My Circuit", 8.5f, Medium);
		const RectF tip = rectF(rl - 8 - tw - 10, top + 9, rl - 8, top + 24);
		fillRound(cr, tip, 4, ch.tabCard());
		strokeRound(cr, tip, 4, colorF(1, 1, 1, 0.12f));
		text(cr, "Check My Circuit", tip.left + 5, tip.top + 2, 8.5f, Medium, ch.tabInk());
		if (hover) {
			// A pointer resting at the edge.
			const float px = rl - 12, py = top + 120;
			cairo_move_to(cr, px, py);
			cairo_line_to(cr, px, py + 15);
			cairo_line_to(cr, px + 4, py + 11);
			cairo_line_to(cr, px + 7, py + 17);
			cairo_line_to(cr, px + 9.5f, py + 16);
			cairo_line_to(cr, px + 6.5f, py + 10);
			cairo_line_to(cr, px + 11, py + 10);
			cairo_close_path(cr);
			setColor(cr, colorF(1, 1, 1));
			cairo_fill_preserve(cr);
			setColor(cr, colorF(0, 0, 0, 0.8f));
			cairo_set_line_width(cr, 1);
			cairo_stroke(cr);
		}
	}
	cairo_restore(cr);
	strokeRound(cr, rectF(win.left + 0.5f, win.top + 0.5f, win.right - 0.5f, win.bottom - 0.5f), 10, colorF(1, 1, 1, 0.16f));
	text(cr, on ? (hover ? "On, and it opens on hover when folded." : "On. Folded, a click on its edge opens it.") : "Off. Everything is still in the menus.",
	     x, y + H + 10, 10.5f, SemiBold, on ? kNeon : kFaint, W, TextAlign::Center, 0.4f);
}

std::string toggleActionsKey() {
	const shortcuts::Action* a = shortcuts::find("toggleActions");
	const std::string k = a ? shortcuts::label(shortcuts::keys(*a)) : std::string();
	return k.empty() ? std::string() : " (" + k + ")";
}

// The two switch rows and the picture, at (x, y); a click on a row's
// `showId` / `hoverId` hit flips it (choiceClick). Returns the bottom.
float panelChoice(cairo_t* cr, Panel& panel, float x, float y, int showId, int hoverId) {
	const float colW = 290;
	const bool on = prefs().actionsOn, hover = prefs().actionsHoverOpen;
	float ry = y;
	struct Row { const char* title; const char* line; bool value; int id; bool enabled; };
	const Row rows[] = {
		{ actions::kShowSwitch, "The commands you use most, a click each, down the window\u2019s right edge.", on, showId, true },
		{ actions::kHoverSwitch, "Folded away, it slides out when the pointer reaches the right edge, and goes back when you move off.", hover, hoverId, on },
	};
	for (const Row& r : rows) {
		const float lw = colW - 40 - 14;
		const float lh = text(nullptr, r.line, 0, 0, 11.5f, Normal, kSecondary, lw);
		const float h = std::max(23.0f, 18 + 4 + lh);
		if (!r.enabled) cairo_push_group(cr);
		text(cr, r.title, x, ry, 13.5f, Bold, kPrimary, lw);
		text(cr, r.line, x, ry + 22, 11.5f, Normal, kSecondary, lw);
		brandSwitch(cr, x + colW - 40, ry + 1, r.value, panel.hot == r.id);
		if (!r.enabled) {
			cairo_pop_group_to_source(cr);
			cairo_paint_with_alpha(cr, 0.45);
		} else {
			panel.addHit(rectF(x - 6, ry - 6, x + colW + 6, ry + h + 6), r.id);
		}
		ry += h + 16;
	}
	const std::string note = std::string("Change it any time: Settings \u25B8 Actions, or View \u25B8 ") + (on ? "Hide" : "Show") + " Actions" +
	                         toggleActionsKey() + ".";
	ry += text(cr, note, x, ry, 11.5f, Normal, kFaint, colW);
	panelPicture(cr, x + colW + 30, y, on, hover);
	return std::max(ry, y + 200 + 30);
}

// A click on one of panelChoice's switches.
bool choiceClick(int id, int showId, int hoverId) {
	if (id == showId) prefs().actionsOn = !prefs().actionsOn;
	else if (id == hoverId && prefs().actionsOn) prefs().actionsHoverOpen = !prefs().actionsHoverOpen;
	else return false;
	guarded("the Actions panel", [] { actions::changed(); });
	return true;
}

}  // namespace

// ---- The welcome ------------------------------------------------------------------

namespace welcome {

namespace {

enum { kSkip = 1, kBack, kNext, kJustStart, kShowSwitch, kHoverSwitch, kTheme0 = 10, kSwatch0 = 20, kGrid0 = 40, kReady0 = 50 };
const int kPages = 6;
const float kWW = 780, kWH = 580;
const int kAccentOrder[] = { 6, 0, 1, 2, 3, 4, 5 };
const char* kAccentNames[] = { "Blue", "Purple", "Pink", "Orange", "Green", "Graphite", "CedarLogic" };

struct KeyCard { const char* key; guint keyval; const char* title; const char* line; };
const KeyCard kKeys[] = {
	{ "A", GDK_KEY_a, "Add a gate", "Type part of its name, press Enter, click to drop it." },
	{ "C", GDK_KEY_c, "Copy, or connect", "Copies the selection. While dragging a gate, drops it and wires it to pins nearby." },
	{ "D", GDK_KEY_d, "Duplicate", "Copies the selection and puts the copy on your mouse." },
	{ "R", GDK_KEY_r, "Rotate", "Turns the selected gates a quarter turn." },
	{ "S", GDK_KEY_s, "Straighten", "Tidies the selected wires into clean routes." },
	{ "T", GDK_KEY_t, "Truth table", "Tries every switch combination and writes down the lights." },
};

struct Welcome {
	Panel panel;
	Pager pager;
	CircuitWindow* window = nullptr;
	double opened = nowSeconds();
	int readyChoice = 0;
	double pressedAt[6] = { -10, -10, -10, -10, -10, -10 };
	GtkWidget* name = nullptr;
	RectF nameBox{};
};
Welcome* g_welcome = nullptr;

// Closed, by any of its buttons or keys: Show Me Around follows (until it's
// been seen), then the lesson if that was the choice; a template's picker
// and the shortcut list open first, and the walk waits for them.
void finish(showme::WelcomeExit exit, const char* then = nullptr) {
	Welcome* w = g_welcome;
	if (w == nullptr) return;
	g_welcome = nullptr;
	prefs().hasSeenWelcome = true;
	prefs().seenWhatsNew = whatsnew::kVersion;   // new to it all: nothing to catch up on
	prefs().save();
	CircuitWindow* window = windowAlive(w->window) ? w->window : nullptr;
	w->panel.destroy();
	delete w;
	if (window == nullptr) return;
	gtk_window_present(window->window());
	if (then) runAction(window, then);
	if (!showme::welcomeClosed(exit) && exit == showme::WelcomeExit::GuidedTour) startTourOn(window);
}

void go(int d) {
	Welcome* w = g_welcome;
	if (w == nullptr) return;
	const int to = std::max(0, std::min(kPages - 1, w->pager.page + d));
	if (to == w->pager.page) return;
	w->pager.go(to);
	gtk_widget_hide(w->name);
	gtk_widget_grab_focus(w->panel.area);
	w->panel.redraw();
}

void readyAction(int i) {
	switch (i) {
	case 0: finish(showme::WelcomeExit::GuidedTour); break;
	case 1: finish(showme::WelcomeExit::Template, "win.new-template"); break;
	case 2: finish(showme::WelcomeExit::Blank); break;
	default: finish(showme::WelcomeExit::Shortcuts, "win.shortcuts"); break;
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

void pageIntro(cairo_t* cr, double t) {
	icon(cr, 56, 34, 58, 0.45f);
	text(cr, "WELCOME TO", 130, 40, 10.5f, SemiBold, kDim, 0, TextAlign::Leading, 2.2f);
	text(cr, "CedarLogic", 130, 54, 30, SemiBold, colorF(0.88f, 0.9f, 0.89f));
	hero(cr, rectF(0, 100, kWW, 260), t);
	text(cr, "Build it. Watch it think.", 56, 262, 24, Bold, kPrimary);
	text(cr, "Design logic circuits, run them live, and hand them in, in the time it takes to sketch one on paper.", 56, 296, 13.5f, Normal,
	     kSecondary, kWW - 112);
	struct P { const char* icon; const char* title; const char* line; };
	const P points[] = {
		{ "folder-symbolic", "It keeps itself", "Everything saves as you go, in Your Circuits, with versions to go back to." },
		{ "utilities-system-monitor-symbolic", "It shows its work", "Live wires, a truth table on one key, and an oscilloscope." },
		{ "input-keyboard-symbolic", "It stays out of the way", "Nearly everything has a key. You won't need the menus." },
	};
	const float cw = (kWW - 112 - 28) / 3;
	for (int i = 0; i < 3; i++) {
		const float x = 56 + i * (cw + 14), y = 334;
		const float lh = text(nullptr, points[i].line, 0, 0, 11.5f, Normal, kSecondary, cw - 28);
		card(cr, rectF(x, y, x + cw, y + std::max(88.0f, 14 + 20 + 7 + lh + 14)));
		glyph(cr, points[i].icon, x + 12, y + 12, 12);
		text(cr, points[i].title, x + 36, y + 13, 13, Bold, kPrimary, cw - 46);
		text(cr, points[i].line, x + 14, y + 41, 11.5f, Normal, kSecondary, cw - 28);
	}
}

void pageSetup(cairo_t* cr, Panel& panel) {
	float y = heading(cr, 56, 34, kWW - 112, "Make it yours", "Your canvas, your way",
	                  "It all applies as you pick it; the window behind this one is the preview. Settings (Ctrl+,) has the rest.");
	y += 22;
	label(cr, 56, y, "Appearance");
	std::vector<RectF> hits;
	segmented(cr, 56, y + 24, { "System", "Light", "Dark" }, std::min(prefs().themeMode, 2), hits);
	for (size_t i = 0; i < hits.size(); i++) panel.addHit(hits[i], kTheme0 + (int)i);
	y += 24 + 34 + 22;
	label(cr, 56, y, "The app's colour");
	y += 26;
	for (int i = 0; i < 7; i++) {
		const int a = kAccentOrder[i];
		const bool on = prefs().accent == a;
		double r, g, b;
		cl_accent_color(a, true, &r, &g, &b);
		const Color c = colorF((float)r, (float)g, (float)b);
		const float cx = 56 + 31 + i * (62 + 12), cy = y + 14;
		if (on) {
			fillCircle(cr, pointF(cx, cy), 24, alpha(c, 0.22f));
			strokeCircle(cr, pointF(cx, cy), 17, colorF(1, 1, 1, 0.9f), 2);
		}
		fillCircle(cr, pointF(cx, cy), 14, c);
		text(cr, kAccentNames[a], cx - 31, cy + 22, 10, on ? Bold : Normal, on ? kPrimary : kFaint, 62, TextAlign::Center);
		panel.addHit(rectF(cx - 31, cy - 18, cx + 31, cy + 36), kSwatch0 + i);
	}
	y += 28 + 6 + 14 + 22;
	label(cr, 56, y, "Grid");
	const int grid = !prefs().showGrid ? 2 : prefs().gridStyle == 1 ? 1 : 0;
	segmented(cr, 56, y + 24, { "Lines", "Dots", "Off" }, grid, hits);
	for (size_t i = 0; i < hits.size(); i++) panel.addHit(hits[i], kGrid0 + (int)i);
	const char* blurbs[] = { "Fine lines, every fifth one darker, like graph paper.", "A dot where the lines would cross: quieter, just as easy to line up.",
	                         "A clean canvas. Gates still snap into place." };
	text(cr, blurbs[grid], 56, y + 24 + 34 + 14, 11.5f, Normal, kSecondary);
}

void pageActions(cairo_t* cr, Panel& panel) {
	const float y = heading(cr, 56, 34, kWW - 112, "One click away", "The Actions panel",
	                        "The buttons you use most, down the right edge of every window: Check My Circuit, Tidy Up, Share Link and more. "
	                        "Show Me Around points it out in a moment.");
	panelChoice(cr, panel, 56, y + 22, kShowSwitch, kHoverSwitch);
}

void pageName(cairo_t* cr, Welcome* w) {
	const float left = 56, colW = 420;
	float y = heading(cr, left, 34, colW, "One more thing", "Who's handing this in?",
	                  "Your name goes on every circuit you export, above the line that says whether it works: the first thing a grader looks for.");
	y += 20;
	label(cr, left, y, "Your name");
	y += 24;
	w->nameBox = rectF(left, y, left + 300, y + 38);
	fillRound(cr, w->nameBox, 9, colorF(25 / 255.0f, 33 / 255.0f, 28 / 255.0f));
	strokeRound(cr, rectF(w->nameBox.left + 0.5f, w->nameBox.top + 0.5f, w->nameBox.right - 0.5f, w->nameBox.bottom - 0.5f), 9, alpha(kNeon, 0.45f));
	if (prefs().studentName.empty() && !(w->name && gtk_widget_has_focus(w->name)))
		text(cr, "First and last name", left + 13, y + 9, 15, Normal, alpha(kPrimary, 0.3f));
	text(cr, "Rather not? Leave it blank. It's in Settings whenever you want it.", left, y + 38 + 16, 11.5f, Normal, kFaint);
	// What an export's footer looks like, with the name in it.
	const RectF paper = rectF(474, 104, 724, 254);
	cairo_save(cr);
	cairo_translate(cr, (paper.left + paper.right) / 2, (paper.top + paper.bottom) / 2);
	cairo_rotate(cr, 2 * G_PI / 180);
	cairo_translate(cr, -(paper.left + paper.right) / 2, -(paper.top + paper.bottom) / 2);
	glow(cr, paper, 12, alpha(kNeon, 0.3f), 18);
	fillRound(cr, paper, 12, colorF(1, 1, 1));
	const Color faintInk = colorF(0, 0, 0, 0.4f);
	const float mx = (paper.left + paper.right) / 2;
	strokeRound(cr, rectF(mx - 15, paper.top + 22, mx + 15, paper.top + 46), 3, faintInk, 1.2f);
	fillRect(cr, rectF(mx - 0.6f, paper.top + 46, mx + 0.6f, paper.top + 54), faintInk);
	fillCircle(cr, pointF(mx, paper.top + 55), 2.6f, faintInk);
	fillRect(cr, rectF(mx - 15, paper.top + 55, mx + 15, paper.top + 56), faintInk);
	fillRect(cr, rectF(paper.left + 20, paper.top + 70, paper.right - 20, paper.top + 71), colorF(0, 0, 0, 0.15f));
	const std::string shown = prefs().studentName.empty() ? "________________" : prefs().studentName;
	text(cr, shown, paper.left + 20, paper.top + 82, 15, Bold, colorF(0, 0, 0), 210);
	text(cr, "This circuit works as specified.", paper.left + 20, paper.top + 108, 11, Normal, colorF(0, 0, 0, 0.6f));
	cairo_restore(cr);
}

void pageKeys(cairo_t* cr, Welcome* w) {
	heading(cr, 56, 34, kWW - 112, "The fast way", "Six keys worth knowing",
	        "Try them now: press any of these and watch it light up. Press ? any time for the full list.");
	const float cw = (kWW - 112 - 28) / 3, ch = 136;
	const double t = nowSeconds();
	for (int i = 0; i < 6; i++) {
		const float x = 56 + (i % 3) * (cw + 14), y = 134 + (i / 3) * (ch + 14);
		const bool lit = t - w->pressedAt[i] < 0.75;
		card(cr, rectF(x, y, x + cw, y + ch), lit);
		keycap(cr, x + 14, y + 14, kKeys[i].key, lit);
		text(cr, kKeys[i].title, x + 14, y + 62, 13, Bold, kPrimary, cw - 28);
		text(cr, kKeys[i].line, x + 14, y + 84, 11, Normal, kSecondary, cw - 28);
	}
}

void pageReady(cairo_t* cr, Welcome* w, double t) {
	heading(cr, 56, 30, kWW - 112, "Ready", "Build your first circuit", "↑↓ and Enter work here too; ← goes back.");
	struct Tile { const char* title; const char* line; const char* icon; };
	const Tile tiles[] = {
		{ "Learn logic: the Guided Tour", "Build two switches, a gate and a light, in about five minutes. Recommended.", nullptr },
		{ "Start from a template", "A lab page with your name, a counter, a 7-segment starter, or your own.", "document-new-symbolic" },
		{ "Start with a blank canvas", "Jump straight in. A quick look around the window comes first.", "list-add-symbolic" },
		{ "See every shortcut", "The whole list, searchable.", "input-keyboard-symbolic" },
	};
	float y = 126;
	for (int i = 0; i < 4; i++) {
		const bool on = i == w->readyChoice;
		const RectF r = rectF(56, y, kWW - 56, y + 62);
		card(cr, r, on, 13);
		text(cr, tiles[i].title, r.left + 18, y + 12, 13.5f, Bold, kPrimary);
		text(cr, tiles[i].line, r.left + 18, y + 33, 11.5f, Normal, kSecondary, r.right - r.left - 220);
		if (tiles[i].icon) glyph(cr, tiles[i].icon, r.right - 52, y + 18, 16, on ? kNeon : kFaint);
		else hero(cr, rectF(r.right - 190, y + 6, r.right - 20, y + 56), t);
		w->panel.addHit(r, kReady0 + i);
		y += 72;
	}
	glyph(cr, "mail-message-new-symbolic", 56, y + 4, 12);
	text(cr, "Something odd, or an idea? The speech bubble in the toolbar (or Help ▸ Send Feedback) sends it straight to the developer.", 80,
	     y + 4, 11.5f, Normal, kFaint, kWW - 136);
}

void paint(cairo_t* cr, float w, float h) {
	Welcome* wl = g_welcome;
	if (wl == nullptr) return;
	const double t = nowSeconds() - wl->opened;
	ground(cr, w, h);
	const float footer = h - 74;
	wl->pager.draw(cr, wl->panel, rectF(0, 0, w, footer - 1), [&](int pg) {
		switch (pg) {
		case 0: pageIntro(cr, t); break;
		case 1: pageSetup(cr, wl->panel); break;
		case 2: pageActions(cr, wl->panel); break;
		case 3: pageName(cr, wl); break;
		case 4: pageKeys(cr, wl); break;
		default: pageReady(cr, wl, t); break;
		}
	});
	const int page = wl->pager.page;
	// The name's box, once its page has landed.
	if (page == 3 && !wl->pager.sliding() && wl->name && !gtk_widget_get_visible(wl->name)) {
		gtk_widget_set_margin_start(wl->name, (int)wl->nameBox.left + 8);
		gtk_widget_set_margin_top(wl->name, (int)wl->nameBox.top + 3);
		gtk_widget_show(wl->name);
	}
	if (page < kPages - 1) {
		const RectF skip = rectF(w - 26 - 44, 18, w - 26, 40);
		text(cr, "Skip", skip.left, skip.top, 12.5f, Medium, wl->panel.hot == kSkip ? alpha(kPrimary, 0.7f) : kFaint, skip.right - skip.left,
		     TextAlign::Trailing);
		wl->panel.addHit(skip, kSkip);
	}
	fillRect(cr, rectF(0, footer - 1, w, footer), colorF(1, 1, 1, 0.07f));
	dots(cr, 44, footer + 37, kPages, page);
	const float x = w - 44;
	const RectF next = page < kPages - 1 ? rectF(x - 142, footer + 19, x, footer + 55) : rectF(x - 110, footer + 19, x, footer + 55);
	button(cr, next, page == 0 ? "Get Started" : page < kPages - 1 ? "Continue" : "Just Start", page < kPages - 1,
	       wl->panel.hot == (page < kPages - 1 ? kNext : kJustStart));
	wl->panel.addHit(next, page < kPages - 1 ? kNext : kJustStart);
	if (page > 0) {
		const RectF back = rectF(next.left - 8 - 96, footer + 19, next.left - 8, footer + 55);
		button(cr, back, "Back", false, wl->panel.hot == kBack);
		wl->panel.addHit(back, kBack);
	}
}

void click(int id) {
	Welcome* w = g_welcome;
	if (w == nullptr) return;
	if (id == kSkip) finish(showme::WelcomeExit::Skip);
	else if (id == kJustStart) finish(showme::WelcomeExit::Blank);
	else if (choiceClick(id, kShowSwitch, kHoverSwitch)) {}
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

bool key(guint k, guint state) {
	Welcome* w = g_welcome;
	if (w == nullptr) return false;
	const bool last = w->pager.page == kPages - 1;
	switch (k) {
	case GDK_KEY_Escape: finish(showme::WelcomeExit::Skip); return true;
	case GDK_KEY_Return:
	case GDK_KEY_KP_Enter: if (last) readyAction(w->readyChoice); else go(1); return true;
	case GDK_KEY_Left: go(-1); return true;
	case GDK_KEY_Right: go(1); return true;
	case GDK_KEY_Down: if (last) { w->readyChoice = std::min(3, w->readyChoice + 1); w->panel.redraw(); } return true;
	case GDK_KEY_Up: if (last) { w->readyChoice = std::max(0, w->readyChoice - 1); w->panel.redraw(); } return true;
	case GDK_KEY_question: readyAction(3); return true;
	default: break;
	}
	if (w->pager.page == 4 && !(state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK))) {
		const guint lower = gdk_keyval_to_lower(k);
		for (int i = 0; i < 6; i++)
			if (lower == kKeys[i].keyval) { w->pressedAt[i] = nowSeconds(); w->panel.redraw(); return true; }
	}
	return false;
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
	p.animating = [] {
		Welcome* wl = g_welcome;
		if (wl == nullptr) return false;
		const int pg = wl->pager.page;
		bool lit = false;
		for (double at : wl->pressedAt) lit = lit || nowSeconds() - at < 0.8;
		return wl->pager.sliding() || pg == 0 || pg == kPages - 1 || lit;
	};
	p.create(window->window(), (int)kWW, (int)kWH);
	// The name box: a bare entry on the drawn field.
	w->name = gtk_entry_new();
	gtk_widget_set_name(w->name, "welcome-name");
	gtk_entry_set_has_frame(GTK_ENTRY(w->name), FALSE);
	gtk_entry_set_text(GTK_ENTRY(w->name), prefs().studentName.c_str());
	gtk_widget_set_halign(w->name, GTK_ALIGN_START);
	gtk_widget_set_valign(w->name, GTK_ALIGN_START);
	gtk_widget_set_size_request(w->name, 284, 32);
	gtk_widget_set_no_show_all(w->name, TRUE);
	gtk_overlay_add_overlay(GTK_OVERLAY(p.overlay), w->name);
	g_signal_connect(w->name, "changed", CL_CALLBACK(+[](GtkEditable* e, gpointer) {
		prefs().studentName = gtk_entry_get_text(GTK_ENTRY(e));
		prefs().save();
		if (g_welcome) g_welcome->panel.redraw();
	}), nullptr);
	p.show();
	return true;
}

bool pageForScreenshot(int page) {
	if (g_welcome == nullptr) return false;
	g_welcome->pager.page = std::max(0, std::min(kPages - 1, page));
	g_welcome->pager.from = -1;
	g_welcome->panel.redraw();
	return true;
}

GtkWidget* window() { return g_welcome ? g_welcome->panel.win : nullptr; }

void closeForCheck(int choice) {
	if (g_welcome == nullptr) return;
	if (choice < 0) finish(showme::WelcomeExit::Skip);
	else readyAction(std::min(3, choice));
}

// ---- Learn Logic: Guided Tour --------------------------------------------------------------
// It builds an AND circuit with you on a circuit of its own (a new one, so
// it never touches your work). Its card sits in the corner of that circuit's
// window, over the canvas; each step moves on by itself once it's done.

namespace {

struct Step {
	const char* title;
	std::function<std::string()> body;
	std::function<std::vector<std::string>()> keys;
	std::function<bool(const CLTourStatus&)> check;   // null: press Next
};

struct Tour {
	CircuitWindow* window = nullptr;
	GtkWidget* area = nullptr;
	guint timer = 0;
	int step = 0;
	bool done = false;
	double doneAt = 0, stepStart = 0, lastCheck = 0;
	bool sawLit = false, sawSimView = false, sawTruthTable = false;
	float shownProgress = 0;
	int height = 0;
	int hot = -1;
	std::vector<Hit> hits;
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
		  [] { return std::string("That's the loop: add, wire, try it, check it. Press Ctrl+? whenever you want every shortcut, and Help ▸ Learn "
		                          "Logic: Guided Tour brings this back. Something odd, or an idea? The speech bubble in the toolbar sends it in."); },
		  keys({ "Ctrl", "?" }), nullptr },
	};
	return s;
}

const float kCardW = 360;

int cardHeight(Tour* t) {
	const Step& s = steps()[t->step];
	float h = 18 + 18 + 12 + 3 + 12 + 24 + 8;
	h += text(nullptr, s.body(), 0, 0, 12.5f, Normal, kSecondary, kCardW - 36);
	if (!s.keys().empty()) h += 12 + 26;
	h += 14 + 36 + 18;
	return (int)std::ceil(h);
}

void endTour() {
	Tour* t = g_tour;
	if (t == nullptr) return;
	g_tour = nullptr;
	if (t->timer) g_source_remove(t->timer);
	if (t->area) {
		g_object_remove_weak_pointer(G_OBJECT(t->area), (gpointer*)&t->area);
		g_signal_handlers_disconnect_by_data(t->area, t);
		gtk_widget_destroy(t->area);
	}
	delete t;
}

void next() {
	Tour* t = g_tour;
	if (t == nullptr) return;
	if (t->step + 1 >= (int)steps().size()) { endTour(); return; }
	t->step++;
	t->done = false;
	t->stepStart = nowSeconds();
	t->height = cardHeight(t);
	if (t->area == nullptr) return;
	gtk_widget_set_size_request(t->area, (int)kCardW, t->height);
	gtk_widget_queue_draw(t->area);
}

void check() {
	Tour* t = g_tour;
	if (t == nullptr) return;
	if (!windowAlive(t->window)) { endTour(); return; }
	const double now = nowSeconds();
	if (now - t->lastCheck < 0.25) return;
	t->lastCheck = now;
	const Step& s = steps()[t->step];
	if (t->done) {
		if (now - t->doneAt > 1.1) next();
		return;
	}
	if (!s.check || now - t->stepStart < 0.6 || t->window->isFloating()) return;
	CLTourStatus st = {};
	cl_tour_status(t->window->document(), t->window->currentPage(), &st);
	if (s.check(st)) {
		t->done = true;
		t->doneAt = now;
		if (t->area) gtk_widget_queue_draw(t->area);
	}
}

void paintTour(cairo_t* cr, float w, float h) {
	Tour* t = g_tour;
	if (t == nullptr) return;
	t->hits.clear();
	const Step& s = steps()[t->step];
	const int count = (int)steps().size();
	const bool last = t->step == count - 1;
	cairo_save(cr);
	roundedPath(cr, rectF(0, 0, w, h), 12);
	cairo_clip(cr);
	ground(cr, w, h, 0.15f, 0, 22);
	cairo_restore(cr);
	strokeRound(cr, rectF(0.5f, 0.5f, w - 0.5f, h - 0.5f), 12, alpha(kNeon, 0.18f));
	float y = 18;
	icon(cr, 18, y, 18);
	text(cr, format("LEARN LOGIC  ·  %d OF %d", t->step + 1, count), 44, y + 1, 10.5f, Bold, kNeon, 0, TextAlign::Leading, 1.2f);
	const RectF close = rectF(w - 18 - 22, y - 2, w - 18, y + 20);
	fillCircle(cr, pointF((close.left + close.right) / 2, (close.top + close.bottom) / 2), 11, colorF(1, 1, 1, t->hot == Tour::kClose ? 0.16f : 0.08f));
	drawIcon(cr, Icon::Dismiss, close, 9, kSecondary);
	t->hits.push_back({ close, Tour::kClose });
	y += 18 + 12;
	const float target = (float)(t->step + (t->done ? 1 : 0)) / count;
	t->shownProgress += (target - t->shownProgress) * 0.18f;
	if (std::fabs(target - t->shownProgress) < 0.002f) t->shownProgress = target;
	const float bw = w - 36;
	fillRound(cr, rectF(18, y, 18 + bw, y + 3), 1.5f, colorF(1, 1, 1, 0.1f));
	const float fw = std::max(4.0f, bw * t->shownProgress);
	fillRound(cr, rectF(16, y - 2, 20 + fw, y + 5), 3.5f, alpha(kNeon, 0.2f));
	fillRound(cr, rectF(18, y, 18 + fw, y + 3), 1.5f, kNeon);
	y += 3 + 12;
	text(cr, s.title, 18, y, 17, Bold, kPrimary, w - 36);
	y += 24 + 8;
	y += text(cr, s.body(), 18, y, 12.5f, Normal, kSecondary, w - 36);
	const std::vector<std::string> keys = s.keys();
	if (!keys.empty()) {
		y += 12;
		float x = 18;
		for (const std::string& k : keys) {
			if (k == "click" || k == "drag") {
				const std::string lbl = k == "click" ? "Click" : "Drag";
				const float pw = textWidth(lbl, 11, SemiBold) + 18;
				fillRound(cr, rectF(x, y + 1, x + pw, y + 25), 12, colorF(1, 1, 1, 0.07f));
				text(cr, lbl, x, y + 5, 11, SemiBold, kSecondary, pw, TextAlign::Center);
				x += pw + 5;
			} else {
				keycap(cr, x, y, k, false, 26);
				x += keycapWidth(k, 26) + 5;
			}
		}
		y += 26;
	}
	y += 14;
	if (s.check) {
		const PointF c = pointF(27, y + 18);
		if (t->done) {
			fillCircle(cr, c, 13, alpha(kNeon, 0.18f));
			fillCircle(cr, c, 9, kNeon);
			drawLine(cr, pointF(c.x - 4, c.y), pointF(c.x - 1, c.y + 3), kInk, 2);
			drawLine(cr, pointF(c.x - 1, c.y + 3), pointF(c.x + 4.5f, c.y - 3.5f), kInk, 2);
		} else {
			for (int i = 0; i < 12; i++) {
				const double a = i * G_PI / 6;
				fillCircle(cr, pointF(c.x + 8 * (float)std::cos(a), c.y + 8 * (float)std::sin(a)), 0.9f, kFaint);
			}
		}
		text(cr, t->done ? "Nice, that's it." : "Your turn. This moves on by itself.", 44, y + 10, 12, Normal, t->done ? kPrimary : kFaint,
		     w - 44 - 110);
	}
	const RectF nb = rectF(w - 18 - 84, y, w - 18, y + 36);
	button(cr, nb, last ? "Finish" : (s.check && !t->done ? "Skip" : "Next"), last || !s.check || t->done, t->hot == Tour::kNextStep);
	t->hits.push_back({ nb, Tour::kNextStep });
}

int tourHit(Tour* t, float x, float y) {
	for (auto it = t->hits.rbegin(); it != t->hits.rend(); ++it) if (inRect(it->r, x, y)) return it->id;
	return -1;
}

}  // namespace

void startTour(CircuitWindow* window) {
	if (window == nullptr || window->overlay() == nullptr) return;
	if (g_tour) endTour();
	g_tour = new Tour();
	Tour* t = g_tour;
	t->window = window;
	t->stepStart = nowSeconds();
	t->height = cardHeight(t);
	// A card over the canvas, in its corner. It takes clicks, never the
	// keyboard: that stays with the circuit.
	t->area = gtk_drawing_area_new();
	// Closing the window destroys the card with it: t->area goes back to
	// null then, so endTour doesn't touch a freed widget.
	g_object_add_weak_pointer(G_OBJECT(t->area), (gpointer*)&t->area);
	gtk_widget_set_halign(t->area, GTK_ALIGN_END);
	gtk_widget_set_valign(t->area, GTK_ALIGN_END);
	gtk_widget_set_margin_end(t->area, 18);
	gtk_widget_set_margin_bottom(t->area, 44);
	gtk_widget_set_size_request(t->area, (int)kCardW, t->height);
	gtk_widget_set_can_focus(t->area, FALSE);
	gtk_widget_add_events(t->area, GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
	g_signal_connect(t->area, "draw", CL_CALLBACK(+[](GtkWidget* a, cairo_t* cr, gpointer) -> gboolean {
		guarded("the tour", [&] { paintTour(cr, (float)gtk_widget_get_allocated_width(a), (float)gtk_widget_get_allocated_height(a)); });
		return TRUE;
	}), t);
	g_signal_connect(t->area, "motion-notify-event", CL_CALLBACK(+[](GtkWidget* a, GdkEventMotion* e, gpointer data) -> gboolean {
		Tour* tour = static_cast<Tour*>(data);
		const int id = tourHit(tour, (float)e->x, (float)e->y);
		if (id != tour->hot) { tour->hot = id; gtk_widget_queue_draw(a); }
		return TRUE;
	}), t);
	g_signal_connect(t->area, "leave-notify-event", CL_CALLBACK(+[](GtkWidget* a, GdkEventCrossing*, gpointer data) -> gboolean {
		static_cast<Tour*>(data)->hot = -1;
		gtk_widget_queue_draw(a);
		return FALSE;
	}), t);
	g_signal_connect(t->area, "button-press-event", CL_CALLBACK(+[](GtkWidget*, GdkEventButton*, gpointer) -> gboolean { return TRUE; }), t);
	g_signal_connect(t->area, "button-release-event", CL_CALLBACK(+[](GtkWidget*, GdkEventButton* e, gpointer data) -> gboolean {
		Tour* tour = static_cast<Tour*>(data);
		const int id = e->button == 1 ? tourHit(tour, (float)e->x, (float)e->y) : -1;
		if (id == Tour::kClose) endTour();
		else if (id == Tour::kNextStep) next();
		return TRUE;
	}), t);
	gtk_overlay_add_overlay(GTK_OVERLAY(window->overlay()), t->area);
	gtk_widget_show(t->area);
	t->timer = g_timeout_add(16, [](gpointer) -> gboolean {
		if (g_tour == nullptr) return G_SOURCE_REMOVE;
		guarded("the tour", [] { check(); });
		Tour* tour = g_tour;
		if (tour == nullptr) return G_SOURCE_REMOVE;
		const float target = (float)(tour->step + (tour->done ? 1 : 0)) / steps().size();
		if (tour->area && std::fabs(target - tour->shownProgress) > 0.001f) gtk_widget_queue_draw(tour->area);
		return G_SOURCE_CONTINUE;
	}, nullptr);
	gtk_window_present(window->window());
}

void startTourOn(CircuitWindow* from) {
	// On a circuit of its own: the one in front if it's empty and new, else a new one.
	CircuitWindow* target = from;
	bool empty = target && target->filePath().empty();
	if (empty)
		for (int p = 0; p < cl_document_page_count(target->document()); p++) empty = empty && cl_document_gate_count(target->document(), p) == 0;
	if (!empty) target = from ? newCircuitWindow(from->application()) : nullptr;
	if (target == nullptr) return;
	welcome::startTour(target);
}

bool tourActive() { return g_tour != nullptr; }
void endTourNow() { endTour(); }

}  // namespace welcome

// ---- What's New ------------------------------------------------------------------

namespace whatsnew {

const char* const kVersion = "0.4";

namespace {

enum { kSkip = 1, kBack, kNext, kTry, kPanelLink, kCardDim, kCardBody, kCardDone, kShowSwitch, kHoverSwitch, kLine0 = 10, kTile0 = 20 };
const float kNW = 820, kNH = 600;

struct Point { const char* icon; const char* title; const char* line; };
struct Chapter {
	const char* eyebrow; const char* title; const char* line;
	Point points[3];
	const char* tryTitle;
	const char* tryAction;
};

const Chapter kChapters[] = {
	{ "Sync", "Your circuits, on every device", "Turn on sync and a circuit you save here is waiting on your other devices. There's no account.",
	  { { "emblem-synchronizing-symbolic", "Settings \xE2\x96\xB8 Sync \xE2\x96\xB8 Turn On Sync", "It makes a sync code, and a QR code for it. Your other devices join with it." },
	    { "dialog-password-symbolic", "Locked before it leaves", "Circuits are encrypted on your computer. The website keeps them, but it can't read them." },
	    { "network-workgroup-symbolic", "Every kind of device", "Macs, Windows PCs, Raspberry Pis and other Linux PCs, CedarLogic Online and the phone app." } },
	  "Open Sync Settings", "sync" },
	{ "Add a device", "Scan it, don't type it", "A new device doesn't need a long code typed in.",
	  { { "view-grid-symbolic", "Show a QR code", "On the new device, Settings \xE2\x96\xB8 Sync \xE2\x96\xB8 I Have a Code. Typing or pasting the code still works." },
	    { "camera-photo-symbolic", "Scan it with your phone", "On a phone that syncs: Your Circuits \xE2\x96\xB8 Sync \xE2\x96\xB8 Add a Device. The new device gets the code." },
	    { "document-open-recent-symbolic", "Nothing gets lost", "If two devices change a circuit, the other edit goes to Version History. A circuit you delete goes to Recently Deleted on the others." } },
	  nullptr, nullptr },
	{ "Hand it in", "Check, report, share", "Three new ways to finish a lab.",
	  { { "emblem-ok-symbolic", "Check My Circuit (Shift+T)", "Type the formula or minterms you were given, or paste the truth table. It marks the rows your circuit gets wrong." },
	    { "x-office-document-symbolic", "Export Lab Report", "File \xE2\x96\xB8 Export Lab Report makes one PDF to hand in. You pick what goes in: the circuit, truth table, K-maps, timing." },
	    { "insert-link-symbolic", "Share Link", "File \xE2\x96\xB8 Share Link copies a link. Anyone can open it in CedarLogic Online, in any browser. CedarLogic opens cedarlogic:// links too." } },
	  "Check My Circuit", "win.check-circuit" },
	{ "Actions", "One click away", "A strip of buttons down the window's right edge, the side panel's mirror.",
	  { { Icon::Rail, "The Actions panel", "The buttons you use most, down the right edge of the window, one click each. Drag them into your order; right-click to choose your own." },
	    { "view-conceal-symbolic", "Folds out of the way", "Click its edge to fold it to a thin tab. Move the pointer to the window's right edge and it slides out. F9 shows or hides it." },
	    { "preferences-system-symbolic", "Yours to choose", "Settings \xE2\x96\xB8 Actions: anything from any menu, in your order. One switch there turns the panel off." } },
	  "Show the Actions", "win.show-actions" },
	{ "Tidy Up", "Rebuilt for cleaner layouts", "Tidy Up by Signal Flow lays a page out again, and it's quick.",
	  { { "view-sort-ascending-symbolic", "Cleaner results", "Groups you drew stay together, labels keep their order, and straight wires stay straight." },
	    { "media-seek-forward-symbolic", "Fast on big pages", "A page with hundreds of parts takes a second or two." },
	    { "edit-undo-symbolic", "Still a preview", "It shows you first: Enter keeps it, Escape puts it back, Tab tries the other way." } },
	  "Tidy Up by Signal Flow", "win.tidy-flow" },
	{ "Fixes", "And the small things", "Fixed since the last version.",
	  { { "emblem-ok-symbolic", "Truth tables with FROM labels", "A FROM label or a ground no longer makes the truth table say the page has clocks or flip-flops." },
	    { "emblem-ok-symbolic", "Lab reports read better", "A heading stays with what's under it, and a truth table's note stays with its table." },
	    { "emblem-ok-symbolic", "Build from a Formula", "With NOR only, a formula like (AB)' now builds the right circuit." } },
	  nullptr, nullptr },
};
const int kChapterCount = 6;
const int kPages = kChapterCount + 2;

struct WhatsNew {
	Panel panel;
	Pager pager;
	CircuitWindow* window = nullptr;
	double opened = nowSeconds();
	bool panelCard = false;   // the Actions panel's switches, over the page
};
WhatsNew* g_new = nullptr;

void close(const char* action) {
	WhatsNew* w = g_new;
	if (w == nullptr) return;
	g_new = nullptr;
	CircuitWindow* window = windowAlive(w->window) ? w->window : nullptr;
	w->panel.destroy();
	delete w;
	if (window == nullptr) return;
	gtk_window_present(window->window());
	const std::string a = action ? action : "";
	if (a == "showme") {
		// The finale's Show Me Around, once What's New has gone.
		g_timeout_add(300, [](gpointer data) -> gboolean {
			CircuitWindow* cw = static_cast<CircuitWindow*>(data);
			if (windowAlive(cw)) guarded("Show Me Around", [&] { showme::start(cw); });
			return G_SOURCE_REMOVE;
		}, window);
		return;
	}
	if (a == "tour") welcome::startTourOn(window);
	else if (a == "sync") settings::show(window, settings::kSyncPage);
	else if (action) runAction(window, action);
	// Show Me Around follows, the first time (ShowMeAround.h).
	showme::startWhenReady();
}

void go(int d) {
	WhatsNew* w = g_new;
	if (w == nullptr) return;
	if (w->pager.page + d > kPages - 1) { close(nullptr); return; }
	w->pager.go(std::max(0, w->pager.page + d));
	w->panel.redraw();
}

// The art beside each chapter.
void art(cairo_t* cr, int chapter, float x, float y, double) {
	switch (chapter) {
	case 0: {
		struct Dev { const char* icon; const char* name; const char* meta; };
		const Dev devs[] = { { "computer-symbolic", "Laptop", "Lab 5: Traffic Light" }, { "computer-symbolic", "Raspberry Pi", "Lab 5: Traffic Light" },
		                     { "computer-symbolic", "Windows PC", "Lab 5: Traffic Light" }, { "phone-symbolic", "Phone", "Lab 5: Traffic Light" } };
		for (int i = 0; i < 4; i++) {
			const RectF r = rectF(x, y + i * 56, x + 290, y + i * 56 + 46);
			card(cr, r, i == 0, 12);
			fillRound(cr, rectF(r.left + 10, r.top + 8, r.left + 40, r.top + 38), 8, alpha(kNeon, 0.12f));
			glyph(cr, devs[i].icon, r.left + 13, r.top + 11, 15);
			text(cr, devs[i].name, r.left + 52, r.top + 6, 12.5f, Bold, kPrimary);
			text(cr, devs[i].meta, r.left + 52, r.top + 25, 10.5f, Normal, kFaint);
			glyph(cr, "emblem-ok-symbolic", r.right - 32, r.top + 11, 12);
		}
		const RectF lock = rectF(x, y + 236, x + 290, y + 282);
		fillRound(cr, lock, 12, alpha(kNeon, 0.08f));
		glyph(cr, "changes-prevent-symbolic", lock.left + 14, lock.top + 11, 13);
		text(cr, "Locked on your device first", lock.left + 52, lock.top + 14, 12, SemiBold, kPrimary);
		break;
	}
	case 1: {
		// A QR code (a real one, of the website's sync page), as I Have a Code shows.
		const char* const sample = "https://cedarlogic.netlify.app/sync/";
		int n = 0;
		const std::vector<bool> bits = clsync::qr(sample, n);
		const float box = 180;
		const RectF r = rectF(x + 55, y + 6, x + 55 + box, y + 6 + box);
		fillRound(cr, r, 10, colorF(1, 1, 1));
		if (n > 0) {
			const float m = box / (n + 6);
			setColor(cr, colorF(0, 0, 0));
			for (int row = 0; row < n; row++)
				for (int col = 0; col < n; col++)
					if (bits[(size_t)row * n + col]) cairo_rectangle(cr, r.left + (col + 3) * m, r.top + (row + 3) * m, m, m);
			cairo_fill(cr);
		}
		text(cr, "Scan with your phone", x, y + 206, 14, Bold, kPrimary, 290, TextAlign::Center);
		text(cr, "Your Circuits \xE2\x96\xB8 Sync \xE2\x96\xB8 Add a Device", x, y + 230, 11, Normal, kFaint, 290, TextAlign::Center);
		break;
	}
	case 2: {
		const RectF r = rectF(x, y, x + 290, y + 214);
		card(cr, r, false, 12);
		text(cr, "A formula or a table", r.left + 16, r.top + 12, 10.5f, SemiBold, kFaint);
		text(cr, "F = A'B + AB'", r.left + 16, r.top + 30, 15, Bold, kPrimary);
		const char* head[] = { "A", "B", "Should be", "Yours" };
		const float cx[] = { 24, 66, 112, 206 };
		for (int c = 0; c < 4; c++) text(cr, head[c], r.left + cx[c], r.top + 66, 10.5f, SemiBold, kFaint);
		const int rows[4][4] = { { 0, 0, 0, 0 }, { 0, 1, 1, 1 }, { 1, 0, 1, 0 }, { 1, 1, 0, 0 } };
		for (int i = 0; i < 4; i++) {
			const float ry = r.top + 88 + i * 24;
			const bool wrong = rows[i][2] != rows[i][3];
			if (wrong) fillRound(cr, rectF(r.left + 12, ry - 3, r.right - 12, ry + 19), 6, colorF(1, 0.35f, 0.35f, 0.16f));
			for (int c = 0; c < 4; c++)
				text(cr, rows[i][c] ? "1" : "0", r.left + cx[c], ry, 12, Bold,
				     c == 3 && wrong ? colorF(1, 0.5f, 0.5f) : c >= 2 ? kNeon : kSecondary);
		}
		text(cr, "Doesn't match: F.", x, y + 232, 13, Bold, colorF(1, 0.5f, 0.5f), 290, TextAlign::Center);
		break;
	}
	case 3: {
		// A window's edge with the rail on it, one action named.
		const RectF r = rectF(x, y, x + 290, y + 282);
		card(cr, r, false, 12);
		const RectF strip = rectF(r.right - 52, r.top + 10, r.right - 10, r.bottom - 10);
		fillRound(cr, strip, 9, colorF(1, 1, 1, 0.05f));
		const char* const icons[] = { "emblem-ok-symbolic", Icon::Predict, Icon::Wand, Icon::Link, "image-x-generic-symbolic",
		                              "x-office-document-symbolic" };
		for (int i = 0; i < 6; i++) {
			const RectF b = rectF(strip.left + 4, strip.top + 8 + i * 40, strip.right - 4, strip.top + 8 + i * 40 + 34);
			if (i == 0) {
				fillRound(cr, b, 7, alpha(kNeon, 0.14f));
				strokeRound(cr, b, 7, alpha(kNeon, 0.8f), 1.5f);
			}
			drawIcon(cr, icons[i], b, 16, i == 0 ? kNeon : colorF(1, 1, 1, 0.75f));
		}
		const float nameW = textWidth("Check My Circuit", 12, Bold), keyW = textWidth("Shift+T", 11, Normal);
		const RectF tip = rectF(strip.left - 10 - (10 + nameW + 10 + keyW + 10), strip.top + 12, strip.left - 10, strip.top + 38);
		card(cr, tip, true, 7);
		text(cr, "Check My Circuit", tip.left + 10, tip.top + 5, 12, Bold, kPrimary);
		text(cr, "Shift+T", tip.left + 20 + nameW, tip.top + 6, 11, Normal, kFaint);
		text(cr, "Drag one up or down", r.left + 18, r.bottom - 64, 12.5f, SemiBold, kSecondary);
		text(cr, "to put them in your order.", r.left + 18, r.bottom - 44, 12.5f, Normal, kFaint);
		break;
	}
	case 4: {
		// A page before Tidy Up and after: the same gates and wires.
		const char* const labels[] = { "Before", "After" };
		const float jitter[2][6] = { { 0, 22, -14, 30, -6, 16 }, { 0, 0, 0, 0, 0, 0 } };
		for (int side = 0; side < 2; side++) {
			const RectF r = rectF(x, y + side * 130, x + 290, y + side * 130 + 116);
			card(cr, r, side == 1, 12);
			text(cr, labels[side], r.left + 14, r.top + 8, 10.5f, SemiBold, side == 1 ? kNeon : kFaint);
			const float cx[3] = { r.left + 36, r.left + 126, r.left + 216 };
			const float cy[2] = { r.top + 44, r.top + 82 };
			for (int c = 0; c < 3; c++)
				for (int k = 0; k < 2; k++) {
					const float gy = cy[k] + jitter[side][c * 2 + k] * 0.6f + (side == 0 ? (c == 1 ? 6.0f : 0.0f) : 0.0f);
					const RectF g = rectF(cx[c], gy - 10, cx[c] + 38, gy + 10);
					fillRound(cr, g, 5, colorF(1, 1, 1, 0.1f));
					strokeRound(cr, g, 5, colorF(1, 1, 1, 0.45f), 1.2f);
					if (c < 2) {
						const float ny = cy[k] + jitter[side][(c + 1) * 2 + k] * 0.6f + (side == 0 ? (c + 1 == 1 ? 6.0f : 0.0f) : 0.0f);
						const float mx = side == 0 ? cx[c] + 38 + 22 + 4 * k : cx[c] + 38 + 25;
						drawLine(cr, pointF(g.right, gy), pointF(mx, gy), side == 1 ? kNeon : colorF(1, 1, 1, 0.5f), 1.5f);
						drawLine(cr, pointF(mx, gy), pointF(mx, ny), side == 1 ? kNeon : colorF(1, 1, 1, 0.5f), 1.5f);
						drawLine(cr, pointF(mx, ny), pointF(cx[c + 1], ny), side == 1 ? kNeon : colorF(1, 1, 1, 0.5f), 1.5f);
					}
				}
		}
		break;
	}
	default: {
		const char* const names[] = { "Truth tables", "Lab reports", "Build from a Formula" };
		for (int i = 0; i < 3; i++) {
			const RectF r = rectF(x, y + i * 56, x + 290, y + i * 56 + 46);
			card(cr, r, false, 12);
			fillRound(cr, rectF(r.left + 10, r.top + 8, r.left + 40, r.top + 38), 8, alpha(kNeon, 0.12f));
			glyph(cr, "emblem-ok-symbolic", r.left + 13, r.top + 11, 15);
			text(cr, names[i], r.left + 52, r.top + 14, 12.5f, Bold, kPrimary);
		}
		break;
	}
	}
}

void pageIntro(cairo_t* cr, WhatsNew* wn, double t) {
	const float x = 56, tw = 380;
	text(cr, "WHAT'S NEW", x, 92, 11, Bold, kNeon, 0, TextAlign::Leading, 1.8f);
	float y = 112 + text(cr, "CedarLogic 0.4", x, 112, 34, Bold, colorF(0.88f, 0.9f, 0.89f), tw + 40) + 14;
	y += text(cr, "Sync your circuits between devices, check your work, hand in a lab report, share a link, keep what you use most on the right, and tidy a big page fast. Here's what's new, a minute's read.",
	          x, y, 14, Normal, kSecondary, tw) + 20;
	for (int i = 0; i < kChapterCount; i++) {
		const RectF r = rectF(x - 8, y - 4, x + tw, y + 24);
		if (wn->panel.hot == kLine0 + i) fillRound(cr, r, 8, colorF(1, 1, 1, 0.06f));
		glyph(cr, kChapters[i].points[0].icon, x, y + 2, 12);
		text(cr, kChapters[i].title, x + 28, y + 1, 13, SemiBold, kPrimary);
		text(cr, kChapters[i].eyebrow, x + 28 + textWidth(kChapters[i].title, 13, SemiBold) + 10, y + 3, 11, Normal, kFaint);
		wn->panel.addHit(r, kLine0 + i);
		y += 24;
	}
	// The icon, large, glowing, gently floating.
	const float cx = 640, cy = 210 + 6 * (float)std::sin(t * 1.4);
	const float glowR = 170 * (float)(1 + 0.04 * std::sin(t * 1.4));
	cairo_pattern_t* halo = cairo_pattern_create_radial(cx, cy, 0, cx, cy, glowR);
	cairo_pattern_add_color_stop_rgba(halo, 0, kNeon.r, kNeon.g, kNeon.b, 0.28);
	cairo_pattern_add_color_stop_rgba(halo, 10.0 / 170, kNeon.r, kNeon.g, kNeon.b, 0.28);
	cairo_pattern_add_color_stop_rgba(halo, 1, kNeon.r, kNeon.g, kNeon.b, 0);
	cairo_set_source(cr, halo);
	cairo_arc(cr, cx, cy, glowR, 0, 2 * G_PI);
	cairo_fill(cr);
	cairo_pattern_destroy(halo);
	icon(cr, cx - 105, cy - 105, 210, 0.6f);
}

void pageChapter(cairo_t* cr, WhatsNew* wn, int i, double t) {
	const Chapter& c = kChapters[i];
	const float x = 48, cw = 420;
	float y = heading(cr, x, 34, cw, c.eyebrow, c.title, c.line) + 18;
	for (const Point& p : c.points) {
		// The panel's card: on or off, open on hover.
		const bool panel = p.icon == Icon::Rail;
		const float h = pointCard(cr, x, y, cw, p.icon, p.title, p.line, 0, panel ? 22 : 0);
		if (panel) {
			const char* link = "Show or Hide It\u2026";
			const RectF r = rectF(x + 48, y + h - 34, x + 48 + textWidth(link, 11.5f, SemiBold), y + h - 16);
			text(cr, link, r.left, r.top, 11.5f, SemiBold, wn->panel.hot == kPanelLink ? kPrimary : kNeon);
			wn->panel.addHit(inflate(r, 4), kPanelLink);
		}
		y += h + 10;
	}
	if (c.tryTitle) {
		const float bw = textWidth(c.tryTitle, 13, SemiBold) + 40;
		const RectF r = rectF(x, y + 6, x + bw, y + 42);
		button(cr, r, c.tryTitle, false, wn->panel.hot == kTry);
		wn->panel.addHit(r, kTry);
	}
	art(cr, i, 500, 84, t);
}

void pageFinale(cairo_t* cr, WhatsNew* wn) {
	float y = heading(cr, 56, 34, kNW - 112, "That's the tour", "Go build something", "It's under Help ▸ What's New in CedarLogic whenever you want it back.") + 22;
	struct Tile { const char* title; const char* line; const char* icon; };
	const Tile tiles[] = {
		{ "Show me around", "A minute\u2019s walk round the window: where everything is.", "find-location-symbolic" },
		{ "Learn logic: the Guided Tour", "Two switches, a gate and a light, on a circuit of its own.", "applications-science-symbolic" },
		{ "Start from a template", "The Lab Page has your name on it already.", "document-new-symbolic" },
		{ "Open CedarLogic Help", "Every feature, with its keys.", "help-browser-symbolic" },
	};
	for (int i = 0; i < 4; i++) {
		const RectF r = rectF(56, y, kNW - 56, y + 66);
		card(cr, r, wn->panel.hot == kTile0 + i, 13);
		glyph(cr, tiles[i].icon, r.left + 20, r.top + 21, 15);
		text(cr, tiles[i].title, r.left + 62, r.top + 14, 13.5f, Bold, kPrimary);
		text(cr, tiles[i].line, r.left + 62, r.top + 36, 11.5f, Normal, kSecondary);
		drawIcon(cr, "go-next-symbolic", rectF(r.right - 44, r.top, r.right - 14, r.bottom), 12, kFaint);
		wn->panel.addHit(r, kTile0 + i);
		y += 78;
	}
}

void paint(cairo_t* cr, float w, float h) {
	WhatsNew* wn = g_new;
	if (wn == nullptr) return;
	const double t = nowSeconds() - wn->opened;
	ground(cr, w, h);
	const float footer = h - 74;
	wn->pager.draw(cr, wn->panel, rectF(0, 0, w, footer - 1), [&](int pg) {
		if (pg == 0) pageIntro(cr, wn, t);
		else if (pg <= kChapterCount) pageChapter(cr, wn, pg - 1, t);
		else pageFinale(cr, wn);
	});
	const int page = wn->pager.page;
	const bool last = page == kPages - 1;
	if (!last) {
		const RectF skip = rectF(w - 26 - 44, 18, w - 26, 40);
		text(cr, "Skip", skip.left, skip.top, 12.5f, Medium, wn->panel.hot == kSkip ? alpha(kPrimary, 0.7f) : kFaint, skip.right - skip.left,
		     TextAlign::Trailing);
		wn->panel.addHit(skip, kSkip);
	}
	fillRect(cr, rectF(0, footer - 1, w, footer), colorF(1, 1, 1, 0.07f));
	dots(cr, 44, footer + 37, kPages, page);
	const RectF next = rectF(w - 44 - (last ? 150 : 142), footer + 19, w - 44, footer + 55);
	button(cr, next, page == 0 ? "Show Me" : last ? "Start Building" : "Next", true, wn->panel.hot == kNext);
	wn->panel.addHit(next, kNext);
	if (page > 0) {
		const RectF back = rectF(next.left - 8 - 96, footer + 19, next.left - 8, footer + 55);
		button(cr, back, "Back", false, wn->panel.hot == kBack);
		wn->panel.addHit(back, kBack);
	}
	if (wn->panelCard) {
		// The Actions panel's switches, over the page: the setting itself.
		// Nothing under it takes a click meanwhile.
		wn->panel.hits.clear();
		fillRect(cr, rectF(0, 0, w, h), colorF(0, 0, 0, 0.55f));
		wn->panel.addHit(rectF(0, 0, w, h), kCardDim);
		const RectF card = rectF(36, 70, w - 36, h - 70);
		glow(cr, card, 18, colorF(0, 0, 0, 0.5f), 24);
		cairo_save(cr);
		roundedPath(cr, card, 18);
		cairo_clip(cr);
		cairo_translate(cr, card.left, card.top);
		ground(cr, rectWidth(card), rectHeight(card), 0.2f, 0, 24);
		cairo_restore(cr);
		strokeRound(cr, rectF(card.left + 0.5f, card.top + 0.5f, card.right - 0.5f, card.bottom - 0.5f), 17.5f, colorF(1, 1, 1, 0.14f));
		wn->panel.addHit(card, kCardBody);
		const float y = heading(cr, card.left + 28, card.top + 28, rectWidth(card) - 56, "The Actions panel", "Yours to show or hide",
		                        std::string("It\u2019s on for everyone in ") + kVersion + ". Flip these to suit you; they change it straight away.");
		panelChoice(cr, wn->panel, card.left + 28, y + 18, kShowSwitch, kHoverSwitch);
		const RectF done = rectF(card.right - 28 - 110, card.bottom - 28 - 36, card.right - 28, card.bottom - 28);
		button(cr, done, "Done", true, wn->panel.hot == kCardDone);
		wn->panel.addHit(done, kCardDone);
	}
}

}  // namespace

void show(CircuitWindow* window, int page, bool panelCard) {
	if (window == nullptr) return;
	if (g_new) { gtk_window_present(GTK_WINDOW(g_new->panel.win)); return; }
	prefs().seenWhatsNew = kVersion;
	prefs().save();
	g_new = new WhatsNew();
	g_new->window = window;
	g_new->pager.page = std::max(0, std::min(kPages - 1, page));
	g_new->panelCard = panelCard;
	Panel& p = g_new->panel;
	p.paint = paint;
	p.animating = [] { return g_new && (g_new->pager.sliding() || g_new->pager.page == 0); };
	p.click = [](int id) {
		WhatsNew* wn = g_new;
		if (wn == nullptr) return;
		if (wn->panelCard) {
			if (id == kCardDim || id == kCardDone) wn->panelCard = false;
			else choiceClick(id, kShowSwitch, kHoverSwitch);
			wn->panel.redraw();
			return;
		}
		if (id == kPanelLink) wn->panelCard = true;
		else if (id == kSkip) close(nullptr);
		else if (id == kBack) go(-1);
		else if (id == kNext) go(1);
		else if (id == kTry && wn->pager.page >= 1 && wn->pager.page <= kChapterCount) close(kChapters[wn->pager.page - 1].tryAction);
		else if (id >= kLine0 && id < kLine0 + kChapterCount) go(id - kLine0 + 1 - wn->pager.page);
		else if (id == kTile0) close("showme");
		else if (id == kTile0 + 1) {
			// Show Me Around first, if it hasn't been seen.
			if (prefs().showMeAroundSeen) close("tour");
			else { showme::setThen([](CircuitWindow* cw) { welcome::startTourOn(cw); }); close(nullptr); }
		}
		else if (id == kTile0 + 2) close("win.new-template");
		else if (id == kTile0 + 3) close("win.help");
		if (g_new) g_new->panel.redraw();
	};
	p.key = [](guint k, guint) {
		if (g_new && g_new->panelCard) {
			if (k == GDK_KEY_Escape || k == GDK_KEY_Return || k == GDK_KEY_KP_Enter) { g_new->panelCard = false; g_new->panel.redraw(); return true; }
			return k == GDK_KEY_Left || k == GDK_KEY_Right;
		}
		if (k == GDK_KEY_Escape) close(nullptr);
		else if (k == GDK_KEY_Return || k == GDK_KEY_KP_Enter || k == GDK_KEY_Right) go(1);
		else if (k == GDK_KEY_Left) go(-1);
		else return false;
		return true;
	};
	p.create(window->window(), (int)kNW, (int)kNH);
	p.show();
}

bool offer(CircuitWindow* window) {
	if (!prefs().hasSeenWelcome || prefs().seenWhatsNew == kVersion || window == nullptr) return false;
	show(window);
	return true;
}

GtkWidget* window() { return g_new ? g_new->panel.win : nullptr; }

}  // namespace whatsnew
