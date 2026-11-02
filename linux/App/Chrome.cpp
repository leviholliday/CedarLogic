// The look around the canvas (see Chrome.h).

#include "Chrome.h"

#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

Color Chrome::canvas() const { return dark ? rgb255(19, 21, 25) : rgb255(255, 255, 255); }
Color Chrome::bar() const { return dark ? rgb255(44, 47, 54) : rgb255(236, 237, 240); }
Color Chrome::barInk() const { return dark ? rgb255(210, 215, 224) : rgb255(52, 56, 64); }
Color Chrome::tabBar() const { return dark ? rgb255(22, 24, 28) : rgb255(233, 234, 238); }
Color Chrome::tabCard() const { return dark ? rgb255(32, 35, 41) : rgb255(255, 255, 255); }
Color Chrome::tabBarTop() const { return dark ? rgb255(38, 41, 48) : rgb255(250, 250, 252); }
Color Chrome::tabCardLit() const { return dark ? rgb255(44, 48, 56) : rgb255(255, 255, 255); }
Color Chrome::minimalBar() const { return dark ? rgb255(28, 31, 37) : rgb255(246, 247, 249); }
Color Chrome::tabInk() const { return dark ? rgb255(228, 232, 240) : rgb255(32, 35, 42); }
Color Chrome::panel() const { return dark ? colorF(0.105f, 0.115f, 0.135f) : colorF(0.965f, 0.968f, 0.975f); }
Color Chrome::hairline() const { return dark ? colorF(1, 1, 1, 0.08f) : colorF(0, 0, 0, 0.09f); }
Color Chrome::sash() const { return dark ? rgb255(40, 43, 50) : rgb255(218, 220, 224); }
Color Chrome::accent() const { return fromRGBA(accentColor(dark)); }
Color Chrome::onAccent() const {
	// White or near-black, whichever stands out more (the WCAG contrast
	// ratio), leaning to white as the Mac does: mid greens get dark text.
	const Color a = accent();
	auto lin = [](float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); };
	const float lum = 0.2126f * lin(a.r) + 0.7152f * lin(a.g) + 0.0722f * lin(a.b);
	const float onWhite = 1.05f / (lum + 0.05f), onDark = (lum + 0.05f) / 0.054f;
	return onDark > onWhite * 1.3f ? colorF(0.035f, 0.063f, 0.047f) : colorF(1, 1, 1);
}
Color Chrome::ink(float alpha) const { return withAlpha(barInk(), alpha); }

Chrome chrome() { return Chrome{ prefs().dark }; }

// ---- Shapes ------------------------------------------------------------------------

void setColor(cairo_t* cr, const Color& c) { cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a); }

void roundedPath(cairo_t* cr, const RectF& r, float radius) {
	const double w = r.right - r.left, h = r.bottom - r.top;
	const double rad = std::max(0.0, std::min<double>(radius, std::min(w, h) / 2));
	cairo_new_sub_path(cr);
	cairo_arc(cr, r.right - rad, r.top + rad, rad, -M_PI / 2, 0);
	cairo_arc(cr, r.right - rad, r.bottom - rad, rad, 0, M_PI / 2);
	cairo_arc(cr, r.left + rad, r.bottom - rad, rad, M_PI / 2, M_PI);
	cairo_arc(cr, r.left + rad, r.top + rad, rad, M_PI, 3 * M_PI / 2);
	cairo_close_path(cr);
}

void fillRound(cairo_t* cr, const RectF& r, float radius, const Color& c) {
	if (c.a <= 0) return;
	cairo_save(cr);
	roundedPath(cr, r, radius);
	setColor(cr, c);
	cairo_fill(cr);
	cairo_restore(cr);
}

void strokeRound(cairo_t* cr, const RectF& r, float radius, const Color& c, float width) {
	if (c.a <= 0) return;
	const float h = width / 2;
	cairo_save(cr);
	roundedPath(cr, rectF(r.left + h, r.top + h, r.right - h, r.bottom - h), radius);
	setColor(cr, c);
	cairo_set_line_width(cr, width);
	cairo_stroke(cr);
	cairo_restore(cr);
}

void fillRect(cairo_t* cr, const RectF& r, const Color& c) {
	if (c.a <= 0) return;
	cairo_save(cr);
	cairo_rectangle(cr, r.left, r.top, r.right - r.left, r.bottom - r.top);
	setColor(cr, c);
	cairo_fill(cr);
	cairo_restore(cr);
}

void fillCircle(cairo_t* cr, PointF center, float radius, const Color& c) {
	if (c.a <= 0) return;
	cairo_save(cr);
	cairo_new_sub_path(cr);
	cairo_arc(cr, center.x, center.y, radius, 0, 2 * M_PI);
	setColor(cr, c);
	cairo_fill(cr);
	cairo_restore(cr);
}

void strokeCircle(cairo_t* cr, PointF center, float radius, const Color& c, float width) {
	if (c.a <= 0) return;
	cairo_save(cr);
	cairo_new_sub_path(cr);
	cairo_arc(cr, center.x, center.y, radius, 0, 2 * M_PI);
	setColor(cr, c);
	cairo_set_line_width(cr, width);
	cairo_stroke(cr);
	cairo_restore(cr);
}

void drawLine(cairo_t* cr, PointF a, PointF b, const Color& c, float width) {
	if (c.a <= 0) return;
	cairo_save(cr);
	cairo_move_to(cr, a.x, a.y);
	cairo_line_to(cr, b.x, b.y);
	setColor(cr, c);
	cairo_set_line_width(cr, width);
	cairo_stroke(cr);
	cairo_restore(cr);
}

// ---- Text --------------------------------------------------------------------------

namespace {

// The desktop's UI face (GNOME's Cantarell, Ubuntu, DejaVu Sans...).
const std::string& uiFamily() {
	static std::string family = [] {
		std::string f = "Sans";
		gchar* name = nullptr;
		if (GtkSettings* s = gtk_settings_get_default()) g_object_get(s, "gtk-font-name", &name, nullptr);
		if (name) {
			PangoFontDescription* d = pango_font_description_from_string(name);
			if (const char* fam = pango_font_description_get_family(d)) f = fam;
			pango_font_description_free(d);
			g_free(name);
		}
		return f;
	}();
	return family;
}

PangoLayout* layoutFor(cairo_t* cr, const std::string& text, float size, bool bold) {
	static cairo_surface_t* scratch = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
	static cairo_t* measure = cairo_create(scratch);
	PangoLayout* l = pango_cairo_create_layout(cr ? cr : measure);
	PangoFontDescription* d = pango_font_description_new();
	pango_font_description_set_family(d, uiFamily().c_str());
	pango_font_description_set_weight(d, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
	// Sizes as the Mac and Windows apps write them: points of the view.
	pango_font_description_set_absolute_size(d, size * PANGO_SCALE);
	pango_layout_set_font_description(l, d);
	pango_font_description_free(d);
	pango_layout_set_text(l, text.c_str(), (int)text.size());
	return l;
}

}  // namespace

void drawText(cairo_t* cr, const std::string& text, const RectF& box, float size, const Color& color, TextAlign align, bool bold) {
	if (text.empty() || color.a <= 0) return;
	PangoLayout* l = layoutFor(cr, text, size, bold);
	pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
	pango_layout_set_width(l, (int)std::max(1.0f, box.right - box.left) * PANGO_SCALE);
	pango_layout_set_alignment(l, align == TextAlign::Center ? PANGO_ALIGN_CENTER : align == TextAlign::Trailing ? PANGO_ALIGN_RIGHT
	                                                                                                           : PANGO_ALIGN_LEFT);
	cairo_save(cr);
	setColor(cr, color);
	cairo_move_to(cr, box.left, box.top);
	pango_cairo_show_layout(cr, l);
	cairo_restore(cr);
	g_object_unref(l);
}

void drawTextMid(cairo_t* cr, const std::string& text, const RectF& box, float size, const Color& color, TextAlign align, bool bold) {
	if (text.empty()) return;
	PangoLayout* l = layoutFor(nullptr, text, size, bold);
	PangoRectangle logical;
	pango_layout_get_extents(l, nullptr, &logical);
	g_object_unref(l);
	const float h = logical.height / (float)PANGO_SCALE;
	const float top = (box.top + box.bottom) / 2 - h / 2;
	drawText(cr, text, rectF(box.left, top, box.right, top + h), size, color, align, bold);
}

float drawFace(cairo_t* cr, const std::string& text, float x, float y, const char* family, float size, const Color& color, bool bold,
               bool italic) {
	if (text.empty()) return 0;
	static cairo_surface_t* scratch = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
	cairo_t* use = cr;
	if (use == nullptr) use = cairo_create(scratch);
	PangoLayout* l = pango_cairo_create_layout(use);
	PangoFontDescription* d = pango_font_description_new();
	pango_font_description_set_family(d, family);
	pango_font_description_set_weight(d, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
	pango_font_description_set_style(d, italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
	pango_font_description_set_absolute_size(d, size * PANGO_SCALE);
	pango_layout_set_font_description(l, d);
	pango_font_description_free(d);
	pango_layout_set_text(l, text.c_str(), -1);
	PangoRectangle logical;
	pango_layout_get_extents(l, nullptr, &logical);
	if (cr) {
		cairo_save(cr);
		setColor(cr, color);
		cairo_move_to(cr, x, y);
		pango_cairo_show_layout(cr, l);
		cairo_restore(cr);
	}
	g_object_unref(l);
	if (cr == nullptr) cairo_destroy(use);
	return logical.width / (float)PANGO_SCALE;
}

float textWidth(const std::string& text, float size, bool bold) {
	if (text.empty()) return 0;
	PangoLayout* l = layoutFor(nullptr, text, size, bold);
	PangoRectangle ink, logical;
	pango_layout_get_extents(l, &ink, &logical);
	g_object_unref(l);
	return logical.width / (float)PANGO_SCALE;
}

float drawWrapped(cairo_t* cr, const std::string& text, const RectF& box, float size, const Color& color, bool bold, TextAlign align) {
	if (text.empty()) return 0;
	PangoLayout* l = layoutFor(cr, text, size, bold);
	pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
	pango_layout_set_width(l, (int)std::max(1.0f, box.right - box.left) * PANGO_SCALE);
	pango_layout_set_alignment(l, align == TextAlign::Center ? PANGO_ALIGN_CENTER : align == TextAlign::Trailing ? PANGO_ALIGN_RIGHT
	                                                                                                           : PANGO_ALIGN_LEFT);
	PangoRectangle ink, logical;
	pango_layout_get_extents(l, &ink, &logical);
	if (cr && color.a > 0) {
		cairo_save(cr);
		setColor(cr, color);
		cairo_move_to(cr, box.left, box.top);
		pango_cairo_show_layout(cr, l);
		cairo_restore(cr);
	}
	g_object_unref(l);
	return logical.height / (float)PANGO_SCALE;
}

// ---- Icons ---------------------------------------------------------------------------

void drawIcon(cairo_t* cr, const char* name, const RectF& box, float size, const Color& color) {
	if (name == nullptr || color.a <= 0) return;
	// Loaded at the screen's scale, tinted, and kept.
	double sx = 1, sy = 1;
	cairo_user_to_device_distance(cr, &sx, &sy);
	const int scale = std::max(1, (int)std::ceil(std::fabs(sx)));
	const int px = std::max(8, (int)std::lround(size * scale));
	using Key = std::tuple<std::string, int, int, int, int>;
	const Key key = std::make_tuple(std::string(name), px, (int)std::lround(color.r * 255), (int)std::lround(color.g * 255),
	                                (int)std::lround(color.b * 255));
	static std::map<Key, GdkPixbuf*> cache;
	GdkPixbuf* pix = nullptr;
	auto it = cache.find(key);
	if (it != cache.end()) {
		pix = it->second;
	} else {
		GtkIconTheme* theme = gtk_icon_theme_get_default();
		GtkIconInfo* info = gtk_icon_theme_lookup_icon(theme, name, px, (GtkIconLookupFlags)(GTK_ICON_LOOKUP_FORCE_SIZE | GTK_ICON_LOOKUP_FORCE_SYMBOLIC));
		if (info) {
			GdkRGBA fg = { color.r, color.g, color.b, 1 };
			pix = gtk_icon_info_load_symbolic(info, &fg, nullptr, nullptr, nullptr, nullptr, nullptr);
			g_object_unref(info);
		}
		cache[key] = pix;
	}
	if (pix == nullptr) return;
	const double w = gdk_pixbuf_get_width(pix) / (double)scale, h = gdk_pixbuf_get_height(pix) / (double)scale;
	const double x = (box.left + box.right - w) / 2, y = (box.top + box.bottom - h) / 2;
	cairo_save(cr);
	cairo_translate(cr, std::round(x * scale) / scale, std::round(y * scale) / scale);
	cairo_scale(cr, 1.0 / scale, 1.0 / scale);
	gdk_cairo_set_source_pixbuf(cr, pix, 0, 0);
	cairo_paint_with_alpha(cr, color.a);
	cairo_restore(cr);
}

void drawNewDocIcon(cairo_t* cr, const RectF& box, const Color& color) {
	// The page, nudged left and up, and a plus on its lower right.
	const float cx = (box.left + box.right) / 2, cy = (box.top + box.bottom) / 2;
	cairo_save(cr);
	setColor(cr, color);
	cairo_set_line_width(cr, 1.2);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	const float l = cx - 7, t = cy - 8, r = cx + 4, b = cy + 6;
	cairo_move_to(cr, l, t + 1);
	cairo_line_to(cr, r - 4, t);
	cairo_line_to(cr, r, t + 4);
	cairo_line_to(cr, r, cy + 1);
	cairo_move_to(cr, cx - 1, b);
	cairo_line_to(cr, l, b);
	cairo_line_to(cr, l, t + 1);
	cairo_move_to(cr, r - 4, t);
	cairo_line_to(cr, r - 4, t + 4);
	cairo_line_to(cr, r, t + 4);
	cairo_stroke(cr);
	cairo_set_line_width(cr, 1.4);
	const float px = cx + 5, py = cy + 5;
	cairo_move_to(cr, px - 3.5, py);
	cairo_line_to(cr, px + 3.5, py);
	cairo_move_to(cr, px, py - 3.5);
	cairo_line_to(cr, px, py + 3.5);
	cairo_stroke(cr);
	cairo_restore(cr);
}

void drawNewTabIcon(cairo_t* cr, const RectF& box, const Color& color) {
	const float cx = (box.left + box.right) / 2, cy = (box.top + box.bottom) / 2;
	cairo_save(cr);
	setColor(cr, color);
	cairo_set_line_width(cr, 1.1);
	// The square behind, showing only its top and left edges.
	cairo_move_to(cr, cx - 7, cy + 3);
	cairo_line_to(cr, cx - 7, cy - 5);
	cairo_move_to(cr, cx - 7, cy - 7);
	cairo_line_to(cr, cx + 3, cy - 7);
	cairo_stroke(cr);
	roundedPath(cr, rectF(cx - 4, cy - 4, cx + 7, cy + 7), 2);
	cairo_stroke(cr);
	cairo_move_to(cr, cx + 1.5, cy - 1);
	cairo_line_to(cr, cx + 1.5, cy + 4);
	cairo_move_to(cr, cx - 1, cy + 1.5);
	cairo_line_to(cr, cx + 4, cy + 1.5);
	cairo_stroke(cr);
	cairo_restore(cr);
}

void drawFolderIcon(cairo_t* cr, const RectF& box, const Color& color) {
	const float cx = (box.left + box.right) / 2, cy = (box.top + box.bottom) / 2;
	const float l = cx - 7.5f, r = cx + 7.5f, t = cy - 5.5f, b = cy + 5.5f;
	cairo_save(cr);
	setColor(cr, color);
	cairo_set_line_width(cr, 1.1);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_move_to(cr, l, b - 1);
	cairo_line_to(cr, l, t + 1);
	cairo_line_to(cr, l + 1, t);
	cairo_line_to(cr, l + 5, t);
	cairo_line_to(cr, l + 6.5, t + 2);
	cairo_line_to(cr, r - 1, t + 2);
	cairo_line_to(cr, r, t + 3);
	cairo_line_to(cr, r, b - 1);
	cairo_line_to(cr, r - 1, b);
	cairo_line_to(cr, l + 1, b);
	cairo_close_path(cr);
	cairo_move_to(cr, l, t + 4);
	cairo_line_to(cr, r, t + 4);
	cairo_stroke(cr);
	cairo_restore(cr);
}

void drawBubbleIcon(cairo_t* cr, const RectF& box, const Color& color) {
	// A speech bubble with an exclamation mark (the Mac's exclamationmark.bubble).
	const float cx = (box.left + box.right) / 2, cy = (box.top + box.bottom) / 2;
	cairo_save(cr);
	setColor(cr, color);
	cairo_set_line_width(cr, 1.2);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	roundedPath(cr, rectF(cx - 8, cy - 7, cx + 8, cy + 5), 3.5f);
	cairo_stroke(cr);
	cairo_move_to(cr, cx - 4, cy + 5);
	cairo_line_to(cr, cx - 5, cy + 8.5);
	cairo_line_to(cr, cx - 0.5, cy + 5);
	cairo_stroke(cr);
	cairo_set_line_width(cr, 1.5);
	cairo_move_to(cr, cx, cy - 4);
	cairo_line_to(cr, cx, cy);
	cairo_stroke(cr);
	cairo_arc(cr, cx, cy + 2.5, 0.9, 0, 2 * M_PI);
	cairo_fill(cr);
	cairo_restore(cr);
}

void drawZoomIcon(cairo_t* cr, const RectF& box, const Color& color, bool in) {
	const float cx = (box.left + box.right) / 2 - 1.5f, cy = (box.top + box.bottom) / 2 - 1.5f;
	cairo_save(cr);
	setColor(cr, color);
	cairo_set_line_width(cr, 1.3);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_new_sub_path(cr);
	cairo_arc(cr, cx, cy, 5.5, 0, 2 * M_PI);
	cairo_stroke(cr);
	cairo_set_line_width(cr, 1.8);
	cairo_move_to(cr, cx + 4, cy + 4);
	cairo_line_to(cr, cx + 8, cy + 8);
	cairo_stroke(cr);
	cairo_set_line_width(cr, 1.2);
	cairo_move_to(cr, cx - 2.6, cy);
	cairo_line_to(cr, cx + 2.6, cy);
	if (in) {
		cairo_move_to(cr, cx, cy - 2.6);
		cairo_line_to(cr, cx, cy + 2.6);
	}
	cairo_stroke(cr);
	cairo_restore(cr);
}

void drawGaugeIcon(cairo_t* cr, const RectF& box, const Color& color) {
	const float cx = (box.left + box.right) / 2, cy = (box.top + box.bottom) / 2 + 1.5f;
	cairo_save(cr);
	setColor(cr, color);
	cairo_set_line_width(cr, 1.3);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_new_sub_path(cr);
	cairo_arc(cr, cx, cy, 6.5, M_PI * 0.8, M_PI * 2.2);
	cairo_stroke(cr);
	cairo_move_to(cr, cx, cy);
	cairo_line_to(cr, cx + 3.2, cy - 3.6);
	cairo_stroke(cr);
	cairo_arc(cr, cx, cy, 1.4, 0, 2 * M_PI);
	cairo_fill(cr);
	cairo_restore(cr);
}

void drawSaveIcon(cairo_t* cr, const RectF& box, const Color& color) {
	const float cx = (box.left + box.right) / 2, cy = (box.top + box.bottom) / 2;
	cairo_save(cr);
	setColor(cr, color);
	cairo_set_line_width(cr, 1.3);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	// The tray.
	cairo_move_to(cr, cx - 7, cy + 1);
	cairo_line_to(cr, cx - 7, cy + 7);
	cairo_line_to(cr, cx + 7, cy + 7);
	cairo_line_to(cr, cx + 7, cy + 1);
	cairo_stroke(cr);
	// The arrow down into it.
	cairo_move_to(cr, cx, cy - 8);
	cairo_line_to(cr, cx, cy + 3);
	cairo_move_to(cr, cx - 3.5, cy - 0.5);
	cairo_line_to(cr, cx, cy + 3);
	cairo_line_to(cr, cx + 3.5, cy - 0.5);
	cairo_stroke(cr);
	cairo_restore(cr);
}

double speedFraction(int stepMs) {
	return 1 - std::min(1.0, std::max(0.0, std::log((double)std::max(1, stepMs)) / std::log(500.0)));
}

int speedFromFraction(double f) {
	f = std::min(1.0, std::max(0.0, f));
	return std::max(1, std::min(500, (int)std::lround(std::pow(500.0, 1 - f))));
}
