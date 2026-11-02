// The window's own buttons (see TitleButtons.h).

#include "TitleButtons.h"

#include <string>

void TitleButtons::load() {
	list.clear();
	gchar* setting = nullptr;
	if (GtkSettings* s = gtk_settings_get_default()) g_object_get(s, "gtk-decoration-layout", &setting, nullptr);
	const std::string layout = setting ? setting : "menu:minimize,maximize,close";
	g_free(setting);
	const size_t colon = layout.find(':');
	auto side = [&](const std::string& part, bool left) {
		size_t at = 0;
		while (at <= part.size()) {
			const size_t comma = part.find(',', at);
			const std::string name = part.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
			if (name == "minimize") list.push_back({ Min, left, {} });
			else if (name == "maximize") list.push_back({ Max, left, {} });
			else if (name == "close") list.push_back({ Close, left, {} });
			if (comma == std::string::npos) break;
			at = comma + 1;
		}
	};
	side(colon == std::string::npos ? std::string() : layout.substr(0, colon), true);
	side(colon == std::string::npos ? layout : layout.substr(colon + 1), false);
}

float TitleButtons::leftRoom() const {
	int n = 0;
	for (const Button& b : list) if (b.left) n++;
	return n ? 8 + n * (kWidth + 2) + 8 : 0;
}

float TitleButtons::rightRoom() const {
	int n = 0;
	for (const Button& b : list) if (!b.left) n++;
	return n ? 8 + n * (kWidth + 2) + 8 : 0;
}

void TitleButtons::layout(float w, float h) {
	const float top = std::floor((h - 30) / 2);
	float x = 8;
	for (Button& b : list) if (b.left) { b.rect = rectF(x, top, x + kWidth, top + 30); x += kWidth + 2; }
	x = w - 8;
	for (int i = (int)list.size() - 1; i >= 0; i--) {
		if (list[i].left) continue;
		x -= kWidth;
		list[i].rect = rectF(x, top, x + kWidth, top + 30);
		x -= 2;
	}
}

int TitleButtons::at(float x, float y) const {
	for (int i = 0; i < (int)list.size(); i++) if (inRect(list[i].rect, x, y)) return i;
	return -1;
}

void TitleButtons::paint(cairo_t* cr, GtkWindow* window, const Color& ink) const {
	const bool active = window && gtk_window_is_active(window);
	for (int i = 0; i < (int)list.size(); i++) {
		const Button& b = list[i];
		const RectF r = b.rect;
		const PointF mid = pointF((r.left + r.right) / 2, (r.top + r.bottom) / 2);
		const float h = (float)fade.amount(i);
		// The desktop's round buttons (Adwaita's look), lighting under the pointer.
		fillCircle(cr, mid, 12, withAlpha(ink, 0.06f + 0.06f * h + (pressed == i ? 0.08f : 0)));
		if (b.kind == Close && h > 0.01f) fillCircle(cr, mid, 12, colorF(0.90f, 0.29f, 0.25f, 0.85f * h));
		const char* icon = b.kind == Min ? Icon::Minimize
		                 : b.kind == Max ? (window && gtk_window_is_maximized(window) ? Icon::Restore : Icon::Maximize)
		                                 : Icon::Close;
		const Color fg = b.kind == Close && h > 0.5f ? colorF(1, 1, 1) : withAlpha(ink, active ? 0.9f : 0.5f);
		drawIcon(cr, icon, r, 14, fg);
	}
}

void TitleButtons::activate(int i, GtkWindow* window) const {
	if (i < 0 || i >= (int)list.size() || window == nullptr) return;
	switch (list[i].kind) {
	case Min: gtk_window_iconify(window); break;
	case Max:
		if (gtk_window_is_maximized(window)) gtk_window_unmaximize(window);
		else gtk_window_maximize(window);
		break;
	case Close: gtk_window_close(window); break;
	}
}

void titleRowPress(GtkWindow* window, GdkEventButton* e, bool doubleClickMaximizes) {
	if (window == nullptr || e == nullptr) return;
	if (e->button == 1 && e->type == GDK_2BUTTON_PRESS) {
		if (!doubleClickMaximizes) return;
		if (gtk_window_is_maximized(window)) gtk_window_unmaximize(window);
		else gtk_window_maximize(window);
	} else if (e->button == 1) {
		gtk_window_begin_move_drag(window, (int)e->button, (int)e->x_root, (int)e->y_root, e->time);
	} else if (e->button == 3) {
		if (GdkWindow* gw = gtk_widget_get_window(GTK_WIDGET(window))) gdk_window_show_window_menu(gw, (GdkEvent*)e);
	}
}
