// Ctrl+Tab (see TabSwitcher.h).

#include "TabSwitcher.h"
#include "Brand.h"
#include "Canvas.h"
#include "Window.h"

#include <algorithm>
#include <cmath>

namespace {
// In points, as the Mac's.
const float kThumbW = 208, kThumbH = 130, kLabelH = 18, kLabelGap = 8, kCardPad = 10, kGap = 6, kPad = 14, kRadius = 28;
const int kPerRow = 5;
// Room around the panel for its shadow, where the desktop composites.
const float kShadow = 26;

// A page as you last saw it (the Mac's thumbnail): the centre and zoom of
// its canvas, scaled so all it showed fits; a page never looked at whole.
cairo_surface_t* thumbnail(CircuitWindow* win, int page, uint64_t key, int scale, bool dark) {
	cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)(kThumbW * scale), (int)(kThumbH * scale));
	cairo_surface_set_device_scale(s, scale, scale);
	cairo_t* cr = cairo_create(s);
	fillRect(cr, rectF(0, 0, kThumbW, kThumbH), Chrome{ dark }.canvas());
	const Canvas* c = win->canvasWithKey(key);
	const double cw = c ? c->width() : 0, ch = c ? c->height() : 0;
	if (c && cw > 50 && ch > 50) {
		double ox = 0, oy = 0;
		c->origin(ox, oy);
		const double upp = c->unitsPerPoint();
		const double cx = ox + cw / 2 * upp, cy = oy - ch / 2 * upp;   // world y points up
		const double tupp = std::max(cw * upp / kThumbW, ch * upp / kThumbH);
		CLDrawOptions o{};
		o.dark = dark;
		o.accent = prefs().accent;
		o.wireScale = prefs().wireScale();
		o.simView = false;
		o.thumbnail = false;
		o.showSelection = false;
		o.selectionFade = 1;
		o.ink = CL_INK_FOLLOW;
		cl_document_draw_ex(win->document(), page, cr, scale, cx - kThumbW / 2 * tupp, cy + kThumbH / 2 * tupp, tupp, &o);
	} else {
		cl_document_draw_fitted(win->document(), page, cr, kThumbW, kThumbH, 10, scale, dark ? CL_STYLE_DARK : CL_STYLE_LIGHT);
	}
	cairo_destroy(cr);
	return s;
}
}

// Going with its window: no fade (nothing of the window is left to draw it from).
TabSwitcher::~TabSwitcher() {
	composited = false;
	cancel();
}

int TabSwitcher::pageOf(size_t i) const { return i < keys.size() ? cl_document_page_index(win->document(), keys[i]) : -1; }

bool TabSwitcher::key(bool backwards) {
	if (isActive) {
		const int n = (int)keys.size();
		if (n > 0) selected = (selected + (backwards ? n - 1 : 1)) % n;
		if (area) gtk_widget_queue_draw(area);
		return true;
	}
	std::vector<int> tabs = win->recentTabs();
	if (tabs.size() > 10) tabs.resize(10);
	if (tabs.size() < 2) return false;
	keys.clear();
	for (int t : tabs) keys.push_back(cl_document_page_id(win->document(), t));
	isActive = true;
	selected = backwards ? (int)keys.size() - 1 : 1;
	// Only if Ctrl is still down after a moment: a quick Ctrl+Tab just flips
	// to the previous tab.
	showTimer = g_timeout_add(180, [](gpointer self) -> gboolean {
		TabSwitcher* t = static_cast<TabSwitcher*>(self);
		t->showTimer = 0;
		if (t->isActive) t->show();
		return G_SOURCE_REMOVE;
	}, this);
	return true;
}

void TabSwitcher::commit(int index) {
	if (!isActive) return;
	const int target = index >= 0 && index < (int)keys.size() ? pageOf((size_t)index) : -1;
	cancel();
	// The first card is the tab you were on: back to it, should a new tab
	// (Ctrl+T, Ctrl still down) or a closed one have put another in front.
	if (target >= 0 && target != win->currentTab()) win->showTab(target);
}

void TabSwitcher::cancel() {
	isActive = false;
	if (showTimer) { g_source_remove(showTimer); showTimer = 0; }
	hide();
}

// The Mac's panel: a rounded glass card (radius 28) floating over the
// middle of the window with a soft shadow, a picture of each tab on it, the
// chosen one in a lighter rounded cell. It fades in (0.12 s) and out (0.1 s).
void TabSwitcher::show() {
	const int n = (int)keys.size(), perRow = std::min(n, kPerRow), rows = (n + kPerRow - 1) / kPerRow;
	const float cellW = kThumbW + 2 * kCardPad, cellH = kCardPad + kThumbH + kLabelGap + kLabelH + kCardPad;
	const float w = perRow * cellW + (perRow - 1) * kGap + 2 * kPad, h = rows * cellH + (rows - 1) * kGap + 2 * kPad;
	panel = gtk_window_new(GTK_WINDOW_POPUP);
	gtk_widget_set_name(panel, "tab-switcher");
	GdkScreen* screen = gtk_widget_get_screen(panel);
	GdkVisual* rgba = gdk_screen_get_rgba_visual(screen);
	composited = rgba && gdk_screen_is_composited(screen);
	if (composited) {
		gtk_widget_set_visual(panel, rgba);
		gtk_widget_set_app_paintable(panel, TRUE);
	}
	margin = composited ? kShadow : 0;
	cards.clear();
	for (int i = 0; i < n; i++) {
		const float x = margin + kPad + (i % kPerRow) * (cellW + kGap), y = margin + kPad + (i / kPerRow) * (cellH + kGap);
		cards.push_back(rectF(x, y, x + cellW, y + cellH));
	}
	// The pictures, made once (not again on every Tab).
	dark = chrome().dark;
	const int scale = std::max(1, gtk_widget_get_scale_factor(GTK_WIDGET(win->window())));
	for (size_t i = 0; i < keys.size(); i++) {
		const int page = pageOf(i);
		thumbs.push_back(page >= 0 && page < cl_document_page_count(win->document()) ? thumbnail(win, page, keys[i], scale, dark) : nullptr);
	}
	hoverArmed = false;
	hoverX = -1e9;
	gtk_window_set_transient_for(GTK_WINDOW(panel), win->window());
	gtk_window_set_type_hint(GTK_WINDOW(panel), GDK_WINDOW_TYPE_HINT_POPUP_MENU);
	gtk_window_set_default_size(GTK_WINDOW(panel), (int)(w + 2 * margin), (int)(h + 2 * margin));
	area = gtk_drawing_area_new();
	gtk_widget_add_events(area, GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK);
	gtk_container_add(GTK_CONTAINER(panel), area);
	g_signal_connect(area, "draw", G_CALLBACK(drawCb), this);
	g_signal_connect(area, "button-press-event", G_CALLBACK(pressCb), this);
	g_signal_connect(area, "motion-notify-event", G_CALLBACK(motionCb), this);
	// Over the middle of the window.
	int ox = 0, oy = 0, ow = 0, oh = 0;
	gtk_window_get_position(win->window(), &ox, &oy);
	gtk_window_get_size(win->window(), &ow, &oh);
	gtk_window_move(GTK_WINDOW(panel), ox + (ow - (int)(w + 2 * margin)) / 2, oy + (oh - (int)(h + 2 * margin)) / 2);
	if (composited) anim::fadeIn(panel, 0.12);
	gtk_widget_show_all(panel);
}

void TabSwitcher::hide() {
	if (panel != nullptr) {
		GtkWidget* p = panel;
		GtkWidget* a = area;
		const bool fade = composited && anim::enabled() && gtk_widget_get_mapped(p);
		if (fade) {
			// It fades away on its own (the switcher may open again
			// meanwhile), showing a picture of how it last looked.
			const int scale = std::max(1, gtk_widget_get_scale_factor(a));
			const int w = gtk_widget_get_allocated_width(a), h = gtk_widget_get_allocated_height(a);
			cairo_surface_t* snap = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w * scale, h * scale);
			cairo_surface_set_device_scale(snap, scale, scale);
			cairo_t* cr = cairo_create(snap);
			guarded("the tab switcher", [&] { paint(cr); });
			cairo_destroy(cr);
			g_signal_handlers_disconnect_by_data(a, this);
			g_object_set_data_full(G_OBJECT(a), "cl-snap", snap, [](gpointer s) { cairo_surface_destroy(static_cast<cairo_surface_t*>(s)); });
			g_signal_connect(a, "draw", CL_CALLBACK(+[](GtkWidget* w, cairo_t* cr, gpointer) -> gboolean {
				cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
				cairo_set_source_surface(cr, static_cast<cairo_surface_t*>(g_object_get_data(G_OBJECT(w), "cl-snap")), 0, 0);
				cairo_paint(cr);
				return TRUE;
			}), nullptr);
			gtk_widget_set_sensitive(p, FALSE);
			anim::fadeTo(p, 0, 0.1, [](GtkWidget* w, gpointer) { gtk_widget_destroy(w); });
		} else {
			g_signal_handlers_disconnect_by_data(a, this);
			gtk_widget_destroy(p);
		}
		panel = area = nullptr;
	}
	for (cairo_surface_t* s : thumbs) if (s) cairo_surface_destroy(s);
	thumbs.clear();
}

void TabSwitcher::paint(cairo_t* cr) {
	const Chrome c = chrome();
	const float w = gtk_widget_get_allocated_width(area), h = gtk_widget_get_allocated_height(area);
	const RectF card = rectF(margin, margin, w - margin, h - margin);
	if (composited) {
		cairo_save(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_set_source_rgba(cr, 0, 0, 0, 0);
		cairo_paint(cr);
		cairo_restore(cr);
		brand::glow(cr, rectF(card.left, card.top + 8, card.right, card.bottom + 8), kRadius, colorF(0, 0, 0, dark ? 0.5f : 0.22f), 20);
		// Not under the glass itself (it would darken through it).
		cairo_save(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
		roundedPath(cr, card, kRadius);
		cairo_fill(cr);
		cairo_restore(cr);
	}
	// The glass: the Mac's HUD material, a little see-through where it can be.
	const float glass = composited ? 0.94f : 1.0f;
	const float radius = composited ? kRadius : 0;
	fillRound(cr, card, radius, dark ? rgb255(38, 41, 48, glass) : rgb255(246, 247, 250, glass));
	{
		// A faint sheen across the top, as light on glass.
		cairo_save(cr);
		roundedPath(cr, card, radius);
		cairo_clip(cr);
		cairo_pattern_t* g = cairo_pattern_create_linear(0, card.top, 0, card.top + 90);
		cairo_pattern_add_color_stop_rgba(g, 0, 1, 1, 1, dark ? 0.06 : 0.5);
		cairo_pattern_add_color_stop_rgba(g, 1, 1, 1, 1, 0);
		cairo_set_source(cr, g);
		cairo_paint(cr);
		cairo_pattern_destroy(g);
		cairo_restore(cr);
	}
	strokeRound(cr, rectF(card.left + 0.5f, card.top + 0.5f, card.right - 0.5f, card.bottom - 0.5f), radius,
	            dark ? colorF(1, 1, 1, 0.12f) : colorF(0, 0, 0, 0.10f), 1);
	const Color ink = dark ? colorF(1, 1, 1) : colorF(0, 0, 0);
	for (size_t i = 0; i < cards.size() && i < keys.size(); i++) {
		const RectF& r = cards[i];
		const bool on = (int)i == selected;
		if (on) {
			fillRound(cr, r, 18, withAlpha(ink, dark ? 0.16f : 0.08f));
			strokeRound(cr, rectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f), 17.5f, withAlpha(ink, dark ? 0.14f : 0.06f));
		}
		const RectF thumb = rectF(r.left + kCardPad, r.top + kCardPad, r.left + kCardPad + kThumbW, r.top + kCardPad + kThumbH);
		fillRound(cr, thumb, 10, Chrome{ dark }.canvas());
		const int page = pageOf(i);
		if (i < thumbs.size() && thumbs[i] && page >= 0) {
			cairo_save(cr);
			roundedPath(cr, thumb, 10);
			cairo_clip(cr);
			cairo_set_source_surface(cr, thumbs[i], thumb.left, thumb.top);
			cairo_paint(cr);
			cairo_restore(cr);
		}
		strokeRound(cr, rectF(thumb.left + 0.25f, thumb.top + 0.25f, thumb.right - 0.25f, thumb.bottom - 0.25f), 10,
		            withAlpha(ink, dark ? 0.14f : 0.12f), 0.5f);
		drawTextMid(cr, page >= 0 ? win->tabName(page) : std::string("Closed"),
		            rectF(thumb.left, thumb.bottom + kLabelGap, thumb.right, thumb.bottom + kLabelGap + kLabelH), 12,
		            on ? withAlpha(ink, 0.92f) : (dark ? rgb255(170, 176, 188) : rgb255(96, 100, 110)), TextAlign::Center, on);
	}
	(void)c;
}

gboolean TabSwitcher::drawCb(GtkWidget*, cairo_t* cr, gpointer self) {
	guarded("the tab switcher", [&] { static_cast<TabSwitcher*>(self)->paint(cr); });
	return TRUE;
}

gboolean TabSwitcher::pressCb(GtkWidget*, GdkEventButton* e, gpointer self) {
	TabSwitcher* t = static_cast<TabSwitcher*>(self);
	for (size_t i = 0; i < t->cards.size(); i++)
		if (inRect(t->cards[i], (float)e->x, (float)e->y)) { t->commit((int)i); return TRUE; }
	return TRUE;
}

gboolean TabSwitcher::motionCb(GtkWidget*, GdkEventMotion* e, gpointer self) {
	TabSwitcher* t = static_cast<TabSwitcher*>(self);
	// Not until the pointer really moves: the panel opening under it isn't a pick (the Mac's).
	if (!t->hoverArmed) {
		if (t->hoverX < -1e8) { t->hoverX = e->x; t->hoverY = e->y; return TRUE; }
		if (std::fabs(e->x - t->hoverX) < 3 && std::fabs(e->y - t->hoverY) < 3) return TRUE;
		t->hoverArmed = true;
	}
	for (size_t i = 0; i < t->cards.size(); i++)
		if (inRect(t->cards[i], (float)e->x, (float)e->y) && t->selected != (int)i) {
			t->selected = (int)i;
			gtk_widget_queue_draw(t->area);
		}
	return TRUE;
}
