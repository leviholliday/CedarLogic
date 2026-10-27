// Ctrl+Tab (see TabSwitcher.h).

#include "TabSwitcher.h"
#include "Canvas.h"
#include "Window.h"
#include "Anim.h"

#include <algorithm>

namespace {
// In points, as the Mac's.
const float kThumbW = 208, kThumbH = 130, kLabelH = 18, kLabelGap = 8, kCardPad = 10, kGap = 6, kPad = 14;
const int kPerRow = 5;
const float kRadius = 28, kShadow = 28;
const double kFadeIn = 0.12, kFadeOut = 0.10;

// The old panel, fading out on its own after the switcher let go of it.
gboolean fadeOutCb(gpointer data) {
	GtkWidget* panel = GTK_WIDGET(data);
	const gint64 start = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(panel), "fade-start"));
	const double t = (g_get_monotonic_time() - (gint64)start) / 1e6 / kFadeOut;
	if (t >= 1) {
		gtk_widget_destroy(panel);
		g_object_unref(panel);
		return G_SOURCE_REMOVE;
	}
	gtk_widget_set_opacity(panel, 1 - t);
	return G_SOURCE_CONTINUE;
}
}

TabSwitcher::~TabSwitcher() { cancel(); }

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

void TabSwitcher::show() {
	GdkScreen* screen = gtk_widget_get_screen(GTK_WIDGET(win->window()));
	GdkVisual* rgba = gdk_screen_get_rgba_visual(screen);
	glass = rgba != nullptr && gdk_screen_is_composited(screen);
	margin = glass ? kShadow : 0;
	haveBase = armed = false;
	const int n = (int)keys.size(), perRow = std::min(n, kPerRow), rows = (n + kPerRow - 1) / kPerRow;
	const float cellW = kThumbW + 2 * kCardPad, cellH = kCardPad + kThumbH + kLabelGap + kLabelH + kCardPad;
	const float w = perRow * cellW + (perRow - 1) * kGap + 2 * kPad, h = rows * cellH + (rows - 1) * kGap + 2 * kPad;
	const float fullW = w + 2 * margin, fullH = h + 2 * margin;
	cards.clear();
	for (int i = 0; i < n; i++) {
		const float x = margin + kPad + (i % kPerRow) * (cellW + kGap), y = margin + kPad + (i / kPerRow) * (cellH + kGap);
		cards.push_back(rectF(x, y, x + cellW, y + cellH));
	}
	panel = gtk_window_new(GTK_WINDOW_POPUP);
	gtk_window_set_transient_for(GTK_WINDOW(panel), win->window());
	gtk_window_set_type_hint(GTK_WINDOW(panel), GDK_WINDOW_TYPE_HINT_POPUP_MENU);
	gtk_window_set_default_size(GTK_WINDOW(panel), (int)fullW, (int)fullH);
	if (glass) {
		gtk_widget_set_visual(panel, rgba);
		gtk_widget_set_app_paintable(panel, TRUE);
		if (anim::enabled()) gtk_widget_set_opacity(panel, 0);
	}
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
	gtk_window_move(GTK_WINDOW(panel), ox + (ow - (int)fullW) / 2, oy + (oh - (int)fullH) / 2);
	gtk_widget_show_all(panel);
	if (glass && anim::enabled()) {
		fadeStart = g_get_monotonic_time();
		fadeTimer = g_timeout_add(15, [](gpointer self) -> gboolean {
			TabSwitcher* t = static_cast<TabSwitcher*>(self);
			if (t->panel == nullptr) { t->fadeTimer = 0; return G_SOURCE_REMOVE; }
			const double f = (g_get_monotonic_time() - t->fadeStart) / 1e6 / kFadeIn;
			gtk_widget_set_opacity(t->panel, anim::easeOut(f));
			if (f >= 1) { t->fadeTimer = 0; return G_SOURCE_REMOVE; }
			return G_SOURCE_CONTINUE;
		}, this);
	}
}

void TabSwitcher::hide() {
	if (fadeTimer) { g_source_remove(fadeTimer); fadeTimer = 0; }
	if (panel == nullptr) return;
	g_signal_handlers_disconnect_by_data(area, this);
	GtkWidget* old = panel;
	panel = area = nullptr;
	if (glass && anim::enabled()) {
		// It fades away by itself; the switcher is free to come up again.
		g_object_ref(old);
		g_object_set_data(G_OBJECT(old), "fade-start", GSIZE_TO_POINTER((gsize)g_get_monotonic_time()));
		g_timeout_add(15, fadeOutCb, old);
		return;
	}
	gtk_widget_destroy(old);
}

void TabSwitcher::paint(cairo_t* cr) {
	const Chrome c = chrome();
	const bool dark = c.dark;
	const float w = gtk_widget_get_allocated_width(area), h = gtk_widget_get_allocated_height(area);
	const Color bg = dark ? rgb255(34, 37, 43) : rgb255(242, 243, 246);
	const Color ink0 = dark ? colorF(1, 1, 1) : colorF(0, 0, 0);
	if (glass) {
		cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
		cairo_paint(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
		// A soft shadow, a little lower than the panel.
		const RectF body = rectF(margin, margin, w - margin, h - margin);
		for (int i = 0; i < 12; i++) {
			const float grow = i * 2.2f;
			fillRound(cr, rectF(body.left - grow, body.top - grow + 8, body.right + grow, body.bottom + grow + 8), kRadius + grow,
			          colorF(0, 0, 0, (dark ? 0.045f : 0.028f) * (1 - i / 12.0f)));
		}
		fillRound(cr, body, kRadius, withAlpha(bg, 0.9f));
		strokeRound(cr, rectF(body.left + 0.5f, body.top + 0.5f, body.right - 0.5f, body.bottom - 0.5f), kRadius, withAlpha(ink0, dark ? 0.16f : 0.1f));
	} else {
		fillRect(cr, rectF(0, 0, w, h), bg);
		strokeRound(cr, rectF(0.5f, 0.5f, w - 0.5f, h - 0.5f), 0, withAlpha(c.ink(1), 0.18f));
	}
	const Color ink = dark ? colorF(1, 1, 1) : colorF(0, 0, 0);
	const double scale = std::max(1, gtk_widget_get_scale_factor(area));
	for (size_t i = 0; i < cards.size() && i < keys.size(); i++) {
		const RectF& r = cards[i];
		const bool on = (int)i == selected;
		if (on) {
			fillRound(cr, r, 18, withAlpha(ink, dark ? 0.16f : 0.08f));
			strokeRound(cr, rectF(r.left + 1, r.top + 1, r.right - 1, r.bottom - 1), 17, withAlpha(c.accent(), 0.9f), 2);
		}
		const RectF thumb = rectF(r.left + kCardPad, r.top + kCardPad, r.left + kCardPad + kThumbW, r.top + kCardPad + kThumbH);
		fillRound(cr, thumb, 10, c.canvas());
		const int page = pageOf(i);
		if (page >= 0 && page < cl_document_page_count(win->document())) {
			cairo_save(cr);
			roundedPath(cr, thumb, 10);
			cairo_clip(cr);
			cairo_translate(cr, thumb.left, thumb.top);
			Canvas* shown = win->canvasOfPage(page);
			Canvas* front = win->currentCanvas();
			if (!(shown && shown->drawThumbnail(cr, kThumbW, kThumbH, (int)scale, dark, front ? front->width() : 0, front ? front->height() : 0)))
				cl_document_draw_fitted(win->document(), page, cr, kThumbW, kThumbH, 10, scale, dark ? CL_STYLE_DARK : CL_STYLE_LIGHT);
			cairo_restore(cr);
		}
		strokeRound(cr, thumb, 10, withAlpha(ink, dark ? 0.14f : 0.12f), 0.5f);
		drawTextMid(cr, page >= 0 ? win->tabName(page) : std::string("Closed"), rectF(thumb.left, thumb.bottom + kLabelGap, thumb.right, thumb.bottom + kLabelGap + kLabelH), 12,
		            withAlpha(ink, on ? 0.95f : 0.6f), TextAlign::Center, on);
	}
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
	if (!t->armed) {
		// The first pointer event is where it rests; a pick needs it to move.
		if (!t->haveBase) { t->haveBase = true; t->baseX = e->x; t->baseY = e->y; return TRUE; }
		if (std::hypot(e->x - t->baseX, e->y - t->baseY) < 4) return TRUE;
		t->armed = true;
	}
	for (size_t i = 0; i < t->cards.size(); i++)
		if (inRect(t->cards[i], (float)e->x, (float)e->y) && t->selected != (int)i) {
			t->selected = (int)i;
			gtk_widget_queue_draw(t->area);
		}
	return TRUE;
}
