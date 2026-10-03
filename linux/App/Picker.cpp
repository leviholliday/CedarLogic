// The pickers' window (see Picker.h).

#include "Picker.h"
#include "Anim.h"

#include <algorithm>
#include <cmath>

namespace picker {

void gateTile(cairo_t* cr, const RectF& r, Color accent, bool on) {
	fillRound(cr, r, 11, withAlpha(accent, on ? 0.22f : 0.13f));
	const float s = (r.right - r.left) / 40, cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
	cairo_save(cr);
	cairo_set_line_width(cr, 1.6 * s);
	setColor(cr, withAlpha(accent, 0.95f));
	cairo_move_to(cr, cx - 8 * s, cy - 8 * s);
	cairo_line_to(cr, cx - 1 * s, cy - 8 * s);
	cairo_curve_to(cr, cx + 9 * s, cy - 8 * s, cx + 9 * s, cy + 8 * s, cx - 1 * s, cy + 8 * s);
	cairo_line_to(cr, cx - 8 * s, cy + 8 * s);
	cairo_close_path(cr);
	cairo_stroke(cr);
	setColor(cr, withAlpha(accent, 0.7f));
	cairo_move_to(cr, cx - 14 * s, cy - 4.5 * s);
	cairo_line_to(cr, cx - 8 * s, cy - 4.5 * s);
	cairo_move_to(cr, cx - 14 * s, cy + 4.5 * s);
	cairo_line_to(cr, cx - 8 * s, cy + 4.5 * s);
	cairo_move_to(cr, cx + 6.5 * s, cy);
	cairo_line_to(cr, cx + 14 * s, cy);
	cairo_stroke(cr);
	cairo_restore(cr);
}

float Picker::clientW() const { return area ? (float)gtk_widget_get_allocated_width(area) : width; }
float Picker::clientH() const { return area ? (float)gtk_widget_get_allocated_height(area) : height; }

RectF Picker::listRect() const {
	const float w = clientW(), h = clientH();
	if (listWidth > 0 && listOnLeft) return rectF(kMargin - 8, top(), kMargin + listWidth, h - kMargin - 30 - 18);
	const float left = listWidth > 0 ? w - kMargin - listWidth : kMargin - 8;
	return rectF(left, top(), w - kMargin + 8, h - kMargin - 30 - 18);
}

void Picker::clampScroll() {
	const RectF l = listRect();
	scroll = std::max(0.0f, std::min(scroll, shown.size() * kRowH - (l.bottom - l.top - 8)));
}

void Picker::reload() {
	const std::string id = selection >= 0 && selection < (int)shown.size() ? shown[selection].id : std::string();
	shown = rows(entry ? std::string(gtk_entry_get_text(GTK_ENTRY(entry))) : std::string());
	selection = 0;
	while (selection < (int)shown.size() && shown[selection].heading) selection++;
	for (int i = 0; i < (int)shown.size(); i++) if (!shown[i].heading && shown[i].id == id) selection = i;
	clampScroll();
	if (onSelect) onSelect(*this);
	redraw();
}

void Picker::select(int i, int direction) {
	if (shown.empty()) return;
	const int was = selection;
	i = std::max(0, std::min((int)shown.size() - 1, i));
	// Past a heading, the way the move was going (or back, at an end).
	while (i >= 0 && i < (int)shown.size() && shown[i].heading) i += direction;
	if (i < 0 || i >= (int)shown.size()) {
		i = std::max(0, std::min((int)shown.size() - 1, i));
		while (i >= 0 && i < (int)shown.size() && shown[i].heading) i -= direction;
	}
	if (i < 0 || i >= (int)shown.size()) return;
	selection = i;
	// Into view.
	const RectF l = listRect();
	const float topY = selection * kRowH, viewH = l.bottom - l.top - 8;
	if (topY < scroll) scroll = topY;
	if (topY + kRowH > scroll + viewH) scroll = topY + kRowH - viewH;
	clampScroll();
	if (was != selection && onSelect) onSelect(*this);
	redraw();
}

const Row* Picker::selected() const {
	return selection >= 0 && selection < (int)shown.size() && !shown[selection].heading ? &shown[selection] : nullptr;
}

void Picker::redraw() {
	if (area) gtk_widget_queue_draw(area);
}

void Picker::paint(cairo_t* cr) {
	const Look look{ prefs().dark };
	const Color accent = chrome().accent();
	const float w = clientW(), h = clientH();
	fillRect(cr, rectF(0, 0, w, h), look.paper());
	drawText(cr, title, rectF(kMargin, kMargin, w - kMargin, kMargin + 26), 19, look.ink(), TextAlign::Leading, true);
	drawWrapped(cr, line, rectF(kMargin, kMargin + 34, w - kMargin, kMargin + 34 + 40), 13, look.ink(0.55f));
	if (search) {
		const float fieldTop = kMargin + 34 + 44;
		const RectF field = rectF(kMargin, fieldTop, w - kMargin, fieldTop + 30);
		fillRound(cr, field, 8, look.dark ? rgb255(40, 44, 52) : rgb255(255, 255, 255));
		strokeRound(cr, field, 8, gtk_widget_has_focus(entry) ? accent : look.ink(0.14f));
		drawIcon(cr, Icon::Search, rectF(field.left + 6, field.top, field.left + 28, field.bottom), 14, look.ink(0.5f));
	}

	if (preview) {
		const RectF l = listRect();
		const RectF pr = listOnLeft ? rectF(l.right + 14, top(), w - kMargin, l.bottom) : rectF(kMargin, top(), l.left - 14, l.bottom);
		fillRound(cr, pr, 12, look.sheet());
		strokeRound(cr, pr, 12, look.ink(0.12f));
		cairo_save(cr);
		preview(cr, rectF(pr.left + 12, pr.top + 12, pr.right - 12, pr.bottom - 12));
		cairo_restore(cr);
	}

	// The rows.
	const RectF l = listRect();
	cairo_save(cr);
	cairo_rectangle(cr, l.left, l.top, l.right - l.left, l.bottom - l.top);
	cairo_clip(cr);
	for (int i = 0; i < (int)shown.size(); i++) {
		const float y = l.top + 4 + i * kRowH - scroll;
		if (y + kRowH < l.top || y > l.bottom) continue;
		const Row& row = shown[i];
		if (row.heading) {
			std::string hd = row.title;
			for (char& c : hd) c = (char)toupper((unsigned char)c);
			drawText(cr, hd, rectF(l.left + 22, y + kRowH - 26, l.right - 8, y + kRowH - 8), 10.5f, look.ink(0.45f), TextAlign::Leading, true);
			if (!row.subtitle.empty()) drawText(cr, row.subtitle, rectF(l.left + 22, y + kRowH, l.right - 8, y + kRowH + 22), 12, look.ink(0.5f));
			continue;
		}
		const bool sel = i == selection, isHot = i == hot;
		const RectF r = rectF(l.left + 8, y + 4, l.right - 8, y + kRowH - 4);
		if (sel) fillRound(cr, r, 12, withAlpha(accent, look.dark ? 0.26f : 0.16f));
		else if (isHot) fillRound(cr, r, 12, look.ink(0.06f));
		const float textLeft = row.tile ? r.left + 68 : r.left + 14;
		if (row.tile) {
			if (drawTile) drawTile(cr, row, rectF(r.left + 12, y + 11, r.left + 52, y + 51));
			else gateTile(cr, rectF(r.left + 12, y + 11, r.left + 52, y + 51), accent, sel);
		}
		float right = r.right - 16;
		if (!row.badge.empty()) {
			const float bw = textWidth(row.badge, 9, true) + 16;
			const RectF badge = rectF(right - bw, y + 21, right, y + 41);
			fillRound(cr, badge, 10, withAlpha(accent, 0.18f));
			drawText(cr, row.badge, rectF(badge.left, badge.top + 3, badge.right, badge.bottom), 9, accent, TextAlign::Center, true);
			right -= bw + 8;
		}
		drawText(cr, row.title, rectF(textLeft, y + 12, right, y + 31), 13, look.ink(), TextAlign::Leading, true);
		drawText(cr, row.subtitle, rectF(textLeft, y + 32, right, y + 48), 11, look.ink(0.55f));
		if (!sel && !isHot && i + 1 < (int)shown.size() && !shown[i + 1].heading && selection != i + 1 && hot != i + 1)
			fillRect(cr, rectF(textLeft, y + kRowH - 1, r.right - 12, y + kRowH), look.ink(0.08f));
	}
	if (shown.empty()) drawText(cr, emptyText, rectF(l.left, l.top + 30, l.right, l.top + 60), 13, look.ink(0.55f), TextAlign::Center);
	cairo_restore(cr);

	// The buttons along the bottom.
	buttons.clear();
	const float by = h - kMargin - 30;
	float x = kMargin;
	for (size_t i = 0; i < leftButtons.size(); i++) {
		const float bw = std::max(80.0f, textWidth(leftButtons[i], 12.5f) + 28);
		buttons.push_back({ rectF(x, by, x + bw, by + 30), (int)i, leftButtons[i], false });
		x += bw + 8;
	}
	x = w - kMargin;
	for (int i = (int)rightButtons.size() - 1; i >= 0; i--) {
		const float bw = std::max(84.0f, textWidth(rightButtons[i], 12.5f, i == (int)rightButtons.size() - 1) + 28);
		x -= bw;
		buttons.push_back({ rectF(x, by, x + bw, by + 30), 100 + i, rightButtons[i], i == (int)rightButtons.size() - 1 });
		x -= 8;
	}
	for (size_t i = 0; i < buttons.size(); i++) {
		const Button& b = buttons[i];
		const bool bh = (int)i == hotButton;
		if (b.primary) fillRound(cr, b.rect, 8, withAlpha(accent, bh ? 1.0f : 0.92f));
		else {
			fillRound(cr, b.rect, 8, look.ink(bh ? 0.12f : 0.07f));
			strokeRound(cr, b.rect, 8, look.ink(0.10f));
		}
		drawText(cr, b.label, rectF(b.rect.left, b.rect.top + 6, b.rect.right, b.rect.bottom), 12.5f, b.primary ? chrome().onAccent() : look.ink(),
		         TextAlign::Center, b.primary);
	}
}

int Picker::rowAt(float x, float y) const {
	const RectF l = listRect();
	if (!inRect(l, x, y)) return -1;
	const int i = (int)((y - l.top - 4 + scroll) / kRowH);
	return i >= 0 && i < (int)shown.size() ? i : -1;
}

int Picker::buttonAt(float x, float y) const {
	for (size_t i = 0; i < buttons.size(); i++) if (inRect(buttons[i].rect, x, y)) return (int)i;
	return -1;
}

void Picker::press(int id) {
	if (onButton && onButton(*this, id)) close();
	else redraw();
}

bool Picker::key(guint k, bool ctrl) {
	if (onKey && onKey(*this, k, ctrl)) return true;
	switch (k) {
	case GDK_KEY_Down: select(selection + 1); return true;
	case GDK_KEY_Up: select(selection - 1, -1); return true;
	case GDK_KEY_Page_Down: select(selection + 8); return true;
	case GDK_KEY_Page_Up: select(selection - 8, -1); return true;
	case GDK_KEY_Return: case GDK_KEY_KP_Enter:
		if (!rightButtons.empty()) press(100 + (int)rightButtons.size() - 1);
		return true;
	case GDK_KEY_Escape: close(); return true;
	default: return false;
	}
}

gboolean Picker::drawCb(GtkWidget*, cairo_t* cr, gpointer self) {
	guarded("drawing", [&] { static_cast<Picker*>(self)->paint(cr); });
	return TRUE;
}

gboolean Picker::pressCb(GtkWidget*, GdkEventButton* e, gpointer self) {
	Picker* p = static_cast<Picker*>(self);
	if (e->button != 1) return TRUE;
	const int row = p->rowAt((float)e->x, (float)e->y);
	guarded("choosing", [&] {
		if (e->type == GDK_2BUTTON_PRESS) {
			if (row >= 0 && !p->shown[row].heading && !p->rightButtons.empty()) p->press(100 + (int)p->rightButtons.size() - 1);
		} else if (e->type == GDK_BUTTON_PRESS && row >= 0 && !p->shown[row].heading) {
			p->select(row);
		}
	});
	return TRUE;
}

gboolean Picker::releaseCb(GtkWidget*, GdkEventButton* e, gpointer self) {
	Picker* p = static_cast<Picker*>(self);
	if (e->button != 1) return TRUE;
	const int b = p->buttonAt((float)e->x, (float)e->y);
	if (b >= 0) guarded("a button", [&] { p->press(p->buttons[b].id); });
	return TRUE;
}

gboolean Picker::motionCb(GtkWidget*, GdkEventMotion* e, gpointer self) {
	Picker* p = static_cast<Picker*>(self);
	const int row = p->rowAt((float)e->x, (float)e->y), b = p->buttonAt((float)e->x, (float)e->y);
	if (row != p->hot || b != p->hotButton) { p->hot = row; p->hotButton = b; p->redraw(); }
	return FALSE;
}

gboolean Picker::leaveCb(GtkWidget*, GdkEventCrossing*, gpointer self) {
	Picker* p = static_cast<Picker*>(self);
	p->hot = p->hotButton = -1;
	p->redraw();
	return FALSE;
}

gboolean Picker::scrollCb(GtkWidget*, GdkEventScroll* e, gpointer self) {
	Picker* p = static_cast<Picker*>(self);
	double dy = 0;
	if (e->direction == GDK_SCROLL_UP) dy = -1;
	else if (e->direction == GDK_SCROLL_DOWN) dy = 1;
	else if (e->direction == GDK_SCROLL_SMOOTH) { double dx; gdk_event_get_scroll_deltas((GdkEvent*)e, &dx, &dy); }
	p->scroll += (float)dy * 62;
	p->clampScroll();
	p->redraw();
	return TRUE;
}

gboolean Picker::keyCb(GtkWidget*, GdkEventKey* e, gpointer self) {
	Picker* p = static_cast<Picker*>(self);
	const bool ctrl = (e->state & GDK_CONTROL_MASK) != 0;
	// The list's keys reach it even from the search field.
	const guint k = e->keyval;
	const bool mine = k == GDK_KEY_Up || k == GDK_KEY_Down || k == GDK_KEY_Page_Up || k == GDK_KEY_Page_Down || k == GDK_KEY_Return ||
	                  k == GDK_KEY_KP_Enter || k == GDK_KEY_Escape || ctrl || k == GDK_KEY_Delete;
	if (!mine) return FALSE;
	bool used = false;
	guarded("a key", [&] { used = p->key(k, ctrl); });
	return used;
}

gboolean Picker::deleteCb(GtkWidget*, GdkEvent*, gpointer self) {
	static_cast<Picker*>(self)->close();
	return TRUE;
}

void Picker::changedCb(GtkEditable*, gpointer self) {
	Picker* p = static_cast<Picker*>(self);
	p->scroll = 0;
	guarded("searching", [&] { p->reload(); });
}

void Picker::run(GtkWindow* owner) {
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), title.c_str());
	gtk_window_set_transient_for(GTK_WINDOW(window), owner);
	gtk_window_set_modal(GTK_WINDOW(window), TRUE);
	gtk_window_set_destroy_with_parent(GTK_WINDOW(window), TRUE);
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER_ON_PARENT);
	gtk_window_set_default_size(GTK_WINDOW(window), (int)width, (int)height);
	gtk_window_set_type_hint(GTK_WINDOW(window), GDK_WINDOW_TYPE_HINT_DIALOG);
	GtkWidget* overlay = gtk_overlay_new();
	gtk_container_add(GTK_CONTAINER(window), overlay);
	area = gtk_drawing_area_new();
	gtk_widget_set_size_request(area, (int)(width * 0.85f), (int)(height * 0.75f));
	gtk_widget_add_events(area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK |
	                            GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
	gtk_widget_set_can_focus(area, TRUE);
	gtk_container_add(GTK_CONTAINER(overlay), area);
	g_signal_connect(area, "draw", G_CALLBACK(drawCb), this);
	g_signal_connect(area, "button-press-event", G_CALLBACK(pressCb), this);
	g_signal_connect(area, "button-release-event", G_CALLBACK(releaseCb), this);
	g_signal_connect(area, "motion-notify-event", G_CALLBACK(motionCb), this);
	g_signal_connect(area, "leave-notify-event", G_CALLBACK(leaveCb), this);
	g_signal_connect(area, "scroll-event", G_CALLBACK(scrollCb), this);
	if (search) {
		// A plain entry over the drawn field.
		entry = gtk_entry_new();
		gtk_entry_set_has_frame(GTK_ENTRY(entry), FALSE);
		gtk_entry_set_placeholder_text(GTK_ENTRY(entry), ("Search " + std::string(g_utf8_strdown(title.c_str(), -1))).c_str());
		gtk_widget_set_name(entry, "picker-search");
		gtk_widget_set_halign(entry, GTK_ALIGN_FILL);
		gtk_widget_set_valign(entry, GTK_ALIGN_START);
		gtk_widget_set_margin_start(entry, (int)(kMargin + 30));
		gtk_widget_set_margin_end(entry, (int)(kMargin + 8));
		gtk_widget_set_margin_top(entry, (int)(kMargin + 34 + 44 + 3));
		gtk_overlay_add_overlay(GTK_OVERLAY(overlay), entry);
		g_signal_connect(entry, "changed", G_CALLBACK(changedCb), this);
		g_signal_connect(entry, "notify::has-focus", CL_CALLBACK(+[](GObject*, GParamSpec*, gpointer self) {
			static_cast<Picker*>(self)->redraw();
		}), this);
	}
	g_signal_connect(window, "key-press-event", G_CALLBACK(keyCb), this);
	g_signal_connect(window, "delete-event", G_CALLBACK(deleteCb), this);
	reload();
	gtk_widget_show_all(window);
	anim::fadeIn(window);
	gtk_widget_grab_focus(entry ? entry : area);
	loop = g_main_loop_new(nullptr, FALSE);
	if (!done) g_main_loop_run(loop);
	g_main_loop_unref(loop);
	loop = nullptr;
	gtk_widget_destroy(window);
	window = entry = area = nullptr;
	gtk_window_present(owner);
}

}  // namespace picker
