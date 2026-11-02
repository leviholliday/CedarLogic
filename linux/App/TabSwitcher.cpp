// Ctrl+Tab (see TabSwitcher.h).

#include "TabSwitcher.h"
#include "Canvas.h"
#include "Window.h"

#include <algorithm>

namespace {
// In points, as the Mac's.
const float kThumbW = 208, kThumbH = 130, kLabelH = 18, kLabelGap = 8, kCardPad = 10, kGap = 6, kPad = 14;
const int kPerRow = 5;
}

TabSwitcher::~TabSwitcher() { cancel(); }

bool TabSwitcher::key(bool backwards) {
	if (isActive) {
		const int n = (int)tabs.size();
		if (n > 0) selected = (selected + (backwards ? n - 1 : 1)) % n;
		if (area) gtk_widget_queue_draw(area);
		return true;
	}
	tabs = win->recentTabs();
	if (tabs.size() > 10) tabs.resize(10);
	if (tabs.size() < 2) return false;
	isActive = true;
	selected = backwards ? (int)tabs.size() - 1 : 1;
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
	const int target = index >= 0 && index < (int)tabs.size() ? tabs[index] : -1;
	cancel();
	if (target >= 0 && index != 0) win->showTab(target);
}

void TabSwitcher::cancel() {
	isActive = false;
	if (showTimer) { g_source_remove(showTimer); showTimer = 0; }
	hide();
}

void TabSwitcher::show() {
	const int n = (int)tabs.size(), perRow = std::min(n, kPerRow), rows = (n + kPerRow - 1) / kPerRow;
	const float cellW = kThumbW + 2 * kCardPad, cellH = kCardPad + kThumbH + kLabelGap + kLabelH + kCardPad;
	const float w = perRow * cellW + (perRow - 1) * kGap + 2 * kPad, h = rows * cellH + (rows - 1) * kGap + 2 * kPad;
	cards.clear();
	for (int i = 0; i < n; i++) {
		const float x = kPad + (i % kPerRow) * (cellW + kGap), y = kPad + (i / kPerRow) * (cellH + kGap);
		cards.push_back(rectF(x, y, x + cellW, y + cellH));
	}
	panel = gtk_window_new(GTK_WINDOW_POPUP);
	gtk_window_set_transient_for(GTK_WINDOW(panel), win->window());
	gtk_window_set_type_hint(GTK_WINDOW(panel), GDK_WINDOW_TYPE_HINT_POPUP_MENU);
	gtk_window_set_default_size(GTK_WINDOW(panel), (int)w, (int)h);
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
	gtk_window_move(GTK_WINDOW(panel), ox + (ow - (int)w) / 2, oy + (oh - (int)h) / 2);
	gtk_widget_show_all(panel);
}

void TabSwitcher::hide() {
	if (panel == nullptr) return;
	g_signal_handlers_disconnect_by_data(area, this);
	gtk_widget_destroy(panel);
	panel = area = nullptr;
}

void TabSwitcher::paint(cairo_t* cr) {
	const Chrome c = chrome();
	const bool dark = c.dark;
	const float w = gtk_widget_get_allocated_width(area), h = gtk_widget_get_allocated_height(area);
	fillRect(cr, rectF(0, 0, w, h), dark ? rgb255(34, 37, 43) : rgb255(242, 243, 246));
	strokeRound(cr, rectF(0.5f, 0.5f, w - 0.5f, h - 0.5f), 0, withAlpha(c.ink(1), 0.18f));
	const Color ink = dark ? colorF(1, 1, 1) : colorF(0, 0, 0);
	const double scale = std::max(1, gtk_widget_get_scale_factor(area));
	for (size_t i = 0; i < cards.size() && i < tabs.size(); i++) {
		const RectF& r = cards[i];
		const bool on = (int)i == selected;
		if (on) {
			fillRound(cr, r, 18, withAlpha(ink, dark ? 0.16f : 0.08f));
			strokeRound(cr, r, 18, withAlpha(ink, dark ? 0.14f : 0.06f));
		}
		const RectF thumb = rectF(r.left + kCardPad, r.top + kCardPad, r.left + kCardPad + kThumbW, r.top + kCardPad + kThumbH);
		fillRound(cr, thumb, 10, c.canvas());
		const int page = tabs[i];
		if (page >= 0 && page < cl_document_page_count(win->document())) {
			cairo_save(cr);
			roundedPath(cr, thumb, 10);
			cairo_clip(cr);
			cairo_translate(cr, thumb.left, thumb.top);
			cl_document_draw_fitted(win->document(), page, cr, kThumbW, kThumbH, 10, scale, dark ? CL_STYLE_DARK : CL_STYLE_LIGHT);
			cairo_restore(cr);
		}
		strokeRound(cr, thumb, 10, withAlpha(ink, dark ? 0.14f : 0.12f), 0.5f);
		drawTextMid(cr, win->tabName(tabs[i]), rectF(thumb.left, thumb.bottom + kLabelGap, thumb.right, thumb.bottom + kLabelGap + kLabelH), 12,
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
	for (size_t i = 0; i < t->cards.size(); i++)
		if (inRect(t->cards[i], (float)e->x, (float)e->y) && t->selected != (int)i) {
			t->selected = (int)i;
			gtk_widget_queue_draw(t->area);
		}
	return TRUE;
}
