// The app's alert card (see Alert.h).

#include "Alert.h"
#include "Anim.h"
#include "Brand.h"
#include "Sheet.h"

#include <algorithm>
#include <functional>

namespace {

const float kPad = 40;     // room around the card for its shadow, where the desktop composites
const float kWidth = 440;  // the card
const float kInset = 22;

// The badge over the icon's corner: amber for a warning, red for a problem,
// the accent for a note.
void drawBadge(cairo_t* cr, float cx, float cy, int badge) {
	if (badge == 0) return;
	const Color fill = badge == 2 ? colorF(0.98f, 0.70f, 0.12f) : badge == 3 ? colorF(0.90f, 0.24f, 0.21f) : brand::kNeonDeep;
	cairo_save(cr);
	cairo_arc(cr, cx, cy, 10.5, 0, 2 * G_PI);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.25);
	cairo_fill(cr);
	cairo_arc(cr, cx, cy, 9, 0, 2 * G_PI);
	cairo_set_source_rgba(cr, fill.r, fill.g, fill.b, 1);
	cairo_fill(cr);
	cairo_restore(cr);
	const char* mark = badge == 1 ? "i" : badge == 2 ? "!" : "×";
	drawTextMid(cr, mark, rectF(cx - 9, cy - 9.5f, cx + 9, cy + 8.5f), 12, badge == 2 ? colorF(0.2f, 0.12f, 0) : colorF(1, 1, 1),
	            TextAlign::Center, true);
}

}  // namespace

int runAlert(GtkWindow* parent, Alert& a) {
	GdkScreen* screen = gdk_screen_get_default();
	const bool glass = screen && gdk_screen_get_rgba_visual(screen) && gdk_screen_is_composited(screen);
	const float pad = glass ? kPad : 0;
	const float textW = kWidth - 2 * kInset;
	const float headH = brand::text(nullptr, a.heading, 0, 0, 16, brand::Bold, brand::kPrimary, textW);
	const float bodyH = a.text.empty() ? 0 : brand::text(nullptr, a.text, 0, 0, 12.5f, brand::Normal, brand::kPrimary, textW);
	const float headTop = kInset + 52, bodyTop = headTop + headH + 5;
	const float fieldTop = bodyTop + bodyH + (bodyH > 0 ? 14 : 8);
	const float fieldH = a.field ? 34 : 0;
	const float cardH = fieldTop + fieldH + (a.field ? 20 : 8) + 34 + kInset;

	int answer = a.escape;
	const double shown = anim::now();
	GtkWidget* entry = nullptr;
	Sheet s;
	s.title = a.title.empty() ? a.heading : a.title;
	s.width = (int)(kWidth + 2 * pad);
	s.height = (int)(cardH + 2 * pad);
	s.minWidth = s.width;
	s.minHeight = s.height;
	s.resizable = false;
	s.decorated = false;
	s.transparent = glass;
	s.animating = true;
	// The Mac's arrival: solid in about 0.14 s, its drop easing into place over 0.35 s.
	auto drop = [&] { return (float)(1 - anim::easeOut((anim::now() - shown) / 0.35)); };
	auto appear = [&] { return (float)anim::easeOut((anim::now() - shown) / 0.14); };
	s.onOpen = [&](Sheet& sh) {
		if (!a.field) return;
		entry = gtk_entry_new();
		gtk_widget_set_name(entry, "alert-entry");
		gtk_entry_set_text(GTK_ENTRY(entry), a.value.c_str());
		gtk_entry_set_placeholder_text(GTK_ENTRY(entry), a.placeholder.c_str());
		gtk_entry_set_has_frame(GTK_ENTRY(entry), FALSE);
		gtk_widget_set_halign(entry, GTK_ALIGN_START);
		gtk_widget_set_valign(entry, GTK_ALIGN_START);
		gtk_widget_set_size_request(entry, (int)(textW - 20), (int)fieldH - 6);
		gtk_widget_set_margin_start(entry, (int)(pad + kInset + 10));
		gtk_widget_set_margin_top(entry, (int)(pad + fieldTop + 3 - 20));
		gtk_widget_set_opacity(entry, 0);
		gtk_overlay_add_overlay(GTK_OVERLAY(sh.overlay), entry);
		sh.initialFocus = entry;
		// The text is kept as it's typed: the field is gone once the card closes.
		g_signal_connect(entry, "changed", CL_CALLBACK(+[](GtkEditable* e, gpointer p) {
			static_cast<Alert*>(p)->value = gtk_entry_get_text(GTK_ENTRY(e));
		}), &a);
		auto redraw = +[](GtkWidget*, GdkEvent*, gpointer area) -> gboolean { gtk_widget_queue_draw(GTK_WIDGET(area)); return FALSE; };
		g_signal_connect(entry, "focus-in-event", G_CALLBACK(redraw), sh.area);
		g_signal_connect(entry, "focus-out-event", G_CALLBACK(redraw), sh.area);
	};
	s.onClose = [&](Sheet& sh) {
		if (!entry) return;
		g_signal_handlers_disconnect_by_data(entry, &a);
		g_signal_handlers_disconnect_by_data(entry, sh.area);
	};
	s.onTick = [&](Sheet& sh) {
		if (entry) {
			gtk_widget_set_margin_top(entry, (int)(pad + fieldTop + 3 - 20 * drop()));
			gtk_widget_set_opacity(entry, appear());
		}
		if (anim::now() - shown > 0.5) sh.animating = false;
	};
	s.paint = [&](Sheet& sh, cairo_t* cr, float w, float h) {
		const bool dark = prefs().dark;
		const Color ink = dark ? colorF(0.94f, 0.94f, 0.94f) : colorF(0.1f, 0.1f, 0.1f);
		const Color dim = dark ? colorF(0.66f, 0.66f, 0.66f) : colorF(0.38f, 0.38f, 0.38f);
		const float ox = pad, oy = pad - 20 * drop();
		const RectF card = glass ? rectF(ox, oy, ox + kWidth, oy + cardH) : rectF(0, 0, w, h);
		const float radius = glass ? 18 : 0;
		cairo_save(cr);
		cairo_push_group(cr);
		if (glass) brand::glow(cr, rectF(card.left, card.top + 10, card.right, card.bottom + 10), 18, colorF(0, 0, 0, 0.35f), 22);
		fillRound(cr, card, radius, dark ? colorF(0.075f, 0.085f, 0.1f, 0.97f) : colorF(0.97f, 0.975f, 0.98f, 0.98f));
		// A breath of the accent along the top, as the Mac's sheets have.
		cairo_save(cr);
		roundedPath(cr, card, radius);
		cairo_clip(cr);
		cairo_pattern_t* tint = cairo_pattern_create_linear(0, card.top, 0, card.top + 90);
		const RGBA acc = accentColor(dark);
		cairo_pattern_add_color_stop_rgba(tint, 0, acc.r, acc.g, acc.b, dark ? 0.10 : 0.07);
		cairo_pattern_add_color_stop_rgba(tint, 1, acc.r, acc.g, acc.b, 0);
		cairo_set_source(cr, tint);
		cairo_paint(cr);
		cairo_pattern_destroy(tint);
		cairo_restore(cr);
		strokeRound(cr, card, radius, colorF(1, 1, 1, dark ? 0.1f : 0.5f));
		const float x = card.left + kInset, y = card.top;
		brand::icon(cr, x, y + kInset, 40);
		drawBadge(cr, x + 38, y + kInset + 38, a.badge);
		brand::text(cr, a.heading, x, y + headTop, 16, brand::Bold, ink, textW);
		if (!a.text.empty()) brand::text(cr, a.text, x, y + bodyTop, 12.5f, brand::Normal, dim, textW);
		if (a.field) {
			const RectF f = rectF(x, y + fieldTop, card.right - kInset, y + fieldTop + fieldH);
			const bool focused = entry && gtk_widget_has_focus(entry);
			fillRound(cr, f, 9, withAlpha(ink, dark ? 0.07f : 0.05f));
			strokeRound(cr, f, 9, focused ? withAlpha(colorF((float)acc.r, (float)acc.g, (float)acc.b), 0.8f) : withAlpha(ink, 0.12f),
			            focused ? 2 : 1);
		}
		auto pill = [&](const AlertButton& b, float left, float right) {
			const char* key = b.answer == a.enter ? "↩" : b.answer == a.escape ? "esc" : nullptr;
			const float tw = textWidth(b.label, 13, true) + (key ? textWidth(key, 10, true) + 17 : 0) + 28;
			if (left < 0) left = right - tw;
			const RectF r = rectF(left, card.bottom - kInset - 34, left + tw, card.bottom - kInset);
			const bool hot = sh.hotNext();
			const Color red = colorF(0.86f, 0.22f, 0.2f);
			fillRound(cr, r, 9, b.kind == 1 ? withAlpha(brand::kNeonDeep, hot ? 1.0f : 0.92f)
			                    : b.kind == 2 ? withAlpha(red, hot ? 1.0f : 0.9f)
			                                  : withAlpha(ink, hot ? 0.12f : 0.08f));
			const Color fg = b.kind ? colorF(1, 1, 1) : ink;
			drawTextMid(cr, b.label, rectF(r.left + 14, r.top, r.right, r.bottom), 13, fg, TextAlign::Leading, true);
			if (key) {
				const float kx = r.left + 14 + textWidth(b.label, 13, true) + 7;
				const RectF kr = rectF(kx, r.top + 9, kx + textWidth(key, 10, true) + 10, r.bottom - 9);
				fillRound(cr, kr, 4, withAlpha(fg, 0.16f));
				drawTextMid(cr, key, kr, 10, fg, TextAlign::Center, true);
			}
			const int ans = b.answer;
			sh.hit(r, [&, ans] { answer = ans; sh.close(); });
			return r.left;
		};
		float right = card.right - kInset;
		for (const AlertButton& b : a.buttons)
			if (!b.apart) right = pill(b, -1, right) - 10;
		for (const AlertButton& b : a.buttons)
			if (b.apart) pill(b, card.left + kInset, 0);
		cairo_pop_group_to_source(cr);
		cairo_paint_with_alpha(cr, appear());
		cairo_restore(cr);
	};
	s.onKey = [&](Sheet& sh, guint k, guint) -> bool {
		if (k == GDK_KEY_Return || k == GDK_KEY_KP_Enter) { answer = a.enter; sh.close(); return true; }
		if (k == GDK_KEY_Escape) { answer = a.escape; sh.close(); return true; }
		return false;
	};
	s.run(parent);
	if (a.field) {
		gchar* t = g_strstrip(g_strdup(a.value.c_str()));
		a.value = t;
		g_free(t);
	}
	return answer;
}

bool askConfirm(GtkWindow* parent, const std::string& heading, const std::string& text, const std::string& yes, const std::string& no,
                bool destructive) {
	Alert a;
	a.heading = heading;
	a.text = text;
	a.badge = destructive ? 2 : 0;
	a.buttons = { { yes, 1, destructive ? 2 : 1 }, { no, 0, 0 } };
	a.escape = 0;
	a.enter = 1;
	return runAlert(parent, a) == 1;
}
