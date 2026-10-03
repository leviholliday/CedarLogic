// CedarLogic's own look (see Brand.h).

#include "Brand.h"

#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>
#include <map>

namespace brand {

const Color kInk = colorF(0.035f, 0.063f, 0.047f), kInkDeep = colorF(0.012f, 0.024f, 0.018f);
const Color kNeon = colorF(0.22f, 1.0f, 0.42f), kNeonDeep = colorF(0.05f, 0.78f, 0.26f);
const Color kDim = colorF(0.62f, 0.74f, 0.66f);
const Color kPrimary = colorF(0.95f, 0.95f, 0.95f), kSecondary = colorF(0.62f, 0.74f, 0.66f), kFaint = colorF(1, 1, 1, 0.4f);

double easeOut(double t) { const double c = std::min(1.0, std::max(0.0, t)); return 1 - std::pow(1 - c, 3); }

double slideCurve(double t) {
	t = std::min(1.0, std::max(0.0, t));
	auto bez = [](double a, double b, double s) { return 3 * a * s * (1 - s) * (1 - s) + 3 * b * s * s * (1 - s) + s * s * s; };
	double lo = 0, hi = 1, s = t;
	for (int i = 0; i < 24; i++) {
		s = (lo + hi) / 2;
		if (bez(0.33, 0.68, s) < t) lo = s; else hi = s;
	}
	return bez(1, 1, s);
}

namespace {

void stop(cairo_pattern_t* p, double at, const Color& c) { cairo_pattern_add_color_stop_rgba(p, at, c.r, c.g, c.b, c.a); }

const std::string& family() {
	static std::string f = [] {
		std::string out = "Sans";
		gchar* name = nullptr;
		if (GtkSettings* s = gtk_settings_get_default()) g_object_get(s, "gtk-font-name", &name, nullptr);
		if (name) {
			PangoFontDescription* d = pango_font_description_from_string(name);
			if (const char* fam = pango_font_description_get_family(d)) out = fam;
			pango_font_description_free(d);
			g_free(name);
		}
		return out;
	}();
	return f;
}

PangoLayout* layout(cairo_t* cr, const std::string& s, float size, Weight weight, float width, TextAlign align, float spacing) {
	static cairo_surface_t* scratch = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
	static cairo_t* measure = cairo_create(scratch);
	PangoLayout* l = pango_cairo_create_layout(cr ? cr : measure);
	PangoFontDescription* d = pango_font_description_new();
	pango_font_description_set_family(d, family().c_str());
	pango_font_description_set_weight(d, (PangoWeight)weight);
	pango_font_description_set_absolute_size(d, size * PANGO_SCALE);
	pango_layout_set_font_description(l, d);
	pango_font_description_free(d);
	pango_layout_set_text(l, s.c_str(), -1);
	if (width > 0) {
		pango_layout_set_width(l, (int)(width * PANGO_SCALE));
		pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
		pango_layout_set_alignment(l, align == TextAlign::Center ? PANGO_ALIGN_CENTER : align == TextAlign::Trailing ? PANGO_ALIGN_RIGHT
		                                                                                                           : PANGO_ALIGN_LEFT);
	}
	if (spacing != 0) {
		PangoAttrList* a = pango_attr_list_new();
		pango_attr_list_insert(a, pango_attr_letter_spacing_new((int)(spacing * PANGO_SCALE)));
		pango_layout_set_attributes(l, a);
		pango_attr_list_unref(a);
	}
	return l;
}

}  // namespace

void glow(cairo_t* cr, const RectF& r, float radius, Color c, float spread) {
	// A few wider, fainter layers: a blur near enough at these sizes.
	for (int i = 8; i >= 1; i--) {
		const float g = spread * i / 8;
		fillRound(cr, rectF(r.left - g, r.top - g, r.right + g, r.bottom + g), radius + g, alpha(c, c.a * 0.09f));
	}
}

float text(cairo_t* cr, const std::string& s, float x, float y, float size, Weight weight, const Color& color, float width, TextAlign align,
           float spacing) {
	if (s.empty()) return 0;
	PangoLayout* l = layout(cr, s, size, weight, width, align, spacing);
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
	return logical.height / (float)PANGO_SCALE;
}

float textWidth(const std::string& s, float size, Weight weight, float spacing) {
	if (s.empty()) return 0;
	PangoLayout* l = layout(nullptr, s, size, weight, 0, TextAlign::Leading, spacing);
	PangoRectangle logical;
	pango_layout_get_extents(l, nullptr, &logical);
	g_object_unref(l);
	return logical.width / (float)PANGO_SCALE;
}

void ground(cairo_t* cr, float w, float h, float bloomX, float bloomY, float gridStep) {
	cairo_save(cr);
	cairo_pattern_t* g = cairo_pattern_create_linear(0, 0, 0, h);
	stop(g, 0, kInk);
	stop(g, 1, kInkDeep);
	cairo_set_source(cr, g);
	cairo_paint(cr);
	cairo_pattern_destroy(g);
	// The grid, through a mask that fades it out from the middle.
	cairo_push_group(cr);
	cairo_set_line_width(cr, 0.5);
	setColor(cr, alpha(kNeon, 0.06f));
	for (float x = std::fmod(w, gridStep) / 2; x < w; x += gridStep) { cairo_move_to(cr, x, 0); cairo_line_to(cr, x, h); }
	for (float y = std::fmod(h, gridStep) / 2; y < h; y += gridStep) { cairo_move_to(cr, 0, y); cairo_line_to(cr, w, y); }
	cairo_stroke(cr);
	cairo_pop_group_to_source(cr);
	cairo_pattern_t* mask = cairo_pattern_create_radial(w / 2, h / 2, 0, w / 2, h / 2, 520);
	cairo_pattern_add_color_stop_rgba(mask, 0, 0, 0, 0, 1);
	cairo_pattern_add_color_stop_rgba(mask, 60.0 / 520, 0, 0, 0, 1);
	cairo_pattern_add_color_stop_rgba(mask, 1, 0, 0, 0, 0);
	cairo_mask(cr, mask);
	cairo_pattern_destroy(mask);
	cairo_pattern_t* bloom = cairo_pattern_create_radial(w * bloomX, h * bloomY, 0, w * bloomX, h * bloomY, 360);
	stop(bloom, 0, alpha(kNeon, 0.16f));
	stop(bloom, 4.0 / 360, alpha(kNeon, 0.16f));
	stop(bloom, 1, alpha(kNeon, 0));
	cairo_set_source(cr, bloom);
	cairo_paint(cr);
	cairo_pattern_destroy(bloom);
	cairo_restore(cr);
}

void card(cairo_t* cr, const RectF& r, bool lit, float radius) {
	if (lit) glow(cr, r, radius, alpha(kNeon, 0.18f), 10);
	fillRound(cr, r, radius, lit ? alpha(kNeon, 0.09f) : colorF(1, 1, 1, 0.045f));
	const RectF in = rectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f);
	strokeRound(cr, in, radius, lit ? alpha(kNeon, 0.6f) : colorF(1, 1, 1, 0.09f), lit ? 1.3f : 1.0f);
}

void button(cairo_t* cr, const RectF& r, const std::string& label, bool primary, bool hot) {
	const float rad = (r.bottom - r.top) / 2;
	const RectF in = rectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f);
	if (primary) {
		glow(cr, r, rad, alpha(kNeon, hot ? 0.6f : 0.42f), 10);
		cairo_pattern_t* g = cairo_pattern_create_linear(0, r.top, 0, r.bottom);
		stop(g, 0, hot ? colorF(0.42f, 1.0f, 0.58f) : kNeon);
		stop(g, 1, kNeonDeep);
		roundedPath(cr, r, rad);
		cairo_set_source(cr, g);
		cairo_fill(cr);
		cairo_pattern_destroy(g);
		strokeRound(cr, in, rad, colorF(1, 1, 1, 0.35f));
	} else {
		fillRound(cr, r, rad, colorF(1, 1, 1, hot ? 0.14f : 0.08f));
		strokeRound(cr, in, rad, colorF(1, 1, 1, 0.14f));
	}
	const float th = text(nullptr, label, 0, 0, 13, SemiBold, kPrimary);
	text(cr, label, r.left, (r.top + r.bottom - th) / 2, 13, SemiBold, primary ? kInk : kPrimary, r.right - r.left, TextAlign::Center);
}

float keycapWidth(const std::string& label, float size) {
	return std::max(size, textWidth(label, size * 0.42f, Bold) + (label.size() > 1 ? 12 + size * 0.4f : 0));
}

void keycap(cairo_t* cr, float x, float y, const std::string& label, bool lit, float size) {
	const float w = keycapWidth(label, size), rad = size * 0.22f;
	const RectF r = rectF(x, y, x + w, y + size);
	if (lit) glow(cr, r, rad, alpha(kNeon, 0.6f), 10);
	fillRound(cr, r, rad, lit ? kNeon : colorF(1, 1, 1, 0.07f));
	strokeRound(cr, rectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f), rad,
	            lit ? colorF(1, 1, 1, 0.4f) : colorF(1, 1, 1, 0.16f));
	const float ts = size * 0.42f;
	const float th = text(nullptr, label, 0, 0, ts, Bold, kPrimary);
	text(cr, label, x, y + (size - th) / 2, ts, Bold, lit ? kInk : kPrimary, w, TextAlign::Center);
}

void segmented(cairo_t* cr, float x, float y, const std::vector<std::string>& options, int chosen, std::vector<RectF>& hits) {
	hits.clear();
	float total = 6;
	std::vector<float> widths;
	for (size_t i = 0; i < options.size(); i++) {
		const bool on = (int)i == chosen;
		widths.push_back(textWidth(options[i], 12.5f, on ? Bold : Medium) + 32);
		total += widths.back() + (i ? 2 : 0);
	}
	const RectF pill = rectF(x, y, x + total, y + 34);
	fillRound(cr, pill, 17, colorF(1, 1, 1, 0.06f));
	strokeRound(cr, rectF(pill.left + 0.5f, pill.top + 0.5f, pill.right - 0.5f, pill.bottom - 0.5f), 16.5f, colorF(1, 1, 1, 0.1f));
	float sx = x + 3;
	for (size_t i = 0; i < options.size(); i++) {
		const bool on = (int)i == chosen;
		const RectF r = rectF(sx, y + 3, sx + widths[i], y + 31);
		if (on) fillRound(cr, r, 14, kNeon);
		text(cr, options[i], r.left, r.top + 5, 12.5f, on ? Bold : Medium, on ? kInk : alpha(kPrimary, 0.85f), widths[i], TextAlign::Center);
		hits.push_back(r);
		sx += widths[i] + 2;
	}
}

float heading(cairo_t* cr, float x, float y, float width, const std::string& eyebrow, const std::string& title, const std::string& line) {
	gchar* up = g_utf8_strup(eyebrow.c_str(), -1);
	float at = y;
	at += text(cr, up, x, at, 11, Bold, kNeon, 0, TextAlign::Leading, 1.6f) + 8;
	g_free(up);
	at += text(cr, title, x, at, 28, Bold, kPrimary, width) + 8;
	at += text(cr, line, x, at, 13.5f, Normal, kSecondary, width);
	return at;
}

void label(cairo_t* cr, float x, float y, const std::string& s) {
	gchar* up = g_utf8_strup(s.c_str(), -1);
	text(cr, up, x, y, 10.5f, Bold, kFaint, 0, TextAlign::Leading, 1.2f);
	g_free(up);
}

namespace {

GdkPixbuf* iconPixbuf() {
	static GdkPixbuf* pb = [] {
		GdkPixbuf* p = nullptr;
		for (const char* name : { "/cedarlogic.png", "/icon.png" }) {
			p = gdk_pixbuf_new_from_file((resourcesDir() + name).c_str(), nullptr);
			if (p) break;
		}
		if (p == nullptr)
			p = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(), "cedarlogic", 256, (GtkIconLookupFlags)0, nullptr);
		return p;
	}();
	return pb;
}

}  // namespace

void icon(cairo_t* cr, float x, float y, float size, float glowAmount) {
	GdkPixbuf* pb = iconPixbuf();
	if (glowAmount > 0) {
		cairo_pattern_t* halo = cairo_pattern_create_radial(x + size / 2, y + size / 2, size * 0.3, x + size / 2, y + size / 2, size * 0.85);
		stop(halo, 0, alpha(kNeon, 0.35f * glowAmount));
		stop(halo, 1, alpha(kNeon, 0));
		cairo_set_source(cr, halo);
		cairo_arc(cr, x + size / 2, y + size / 2, size * 0.85, 0, 2 * G_PI);
		cairo_fill(cr);
		cairo_pattern_destroy(halo);
	}
	if (pb == nullptr) return;
	cairo_save(cr);
	const int pw = gdk_pixbuf_get_width(pb), ph = gdk_pixbuf_get_height(pb);
	cairo_translate(cr, x, y);
	cairo_scale(cr, size / pw, size / ph);
	gdk_cairo_set_source_pixbuf(cr, pb, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_paint(cr);
	cairo_restore(cr);
}

void hero(cairo_t* cr, const RectF& box, double t) {
	const Color accent = kNeon, ink = colorF(1, 1, 1);
	const Color lampOn = colorF(1, 196 / 255.0f, 64 / 255.0f);
	const float w = box.right - box.left, h = box.bottom - box.top;
	const float cx = box.left + w / 2, cy = box.top + h / 2;
	const float k = std::min(w / 520, h / 200);
	const int state = (int)(t / 0.95) % 4;
	const bool a = (state & 2) != 0, b = (state & 1) != 0, out = a && b;
	const float bodyL = cx - 30 * k, bodyR = cx + 30 * k, hh = 44 * k, nose = bodyR + 44 * k;
	cairo_save(cr);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);
	struct Wire { std::vector<PointF> pts; bool on; };
	const Wire wires[] = {
		{ { { cx - 188 * k, cy - 50 * k }, { cx - 100 * k, cy - 50 * k }, { cx - 100 * k, cy - 24 * k }, { bodyL, cy - 24 * k } }, a },
		{ { { cx - 188 * k, cy + 50 * k }, { cx - 100 * k, cy + 50 * k }, { cx - 100 * k, cy + 24 * k }, { bodyL, cy + 24 * k } }, b },
		{ { { nose, cy }, { cx + 150 * k, cy } }, out },
	};
	for (const Wire& wr : wires) {
		auto path = [&] {
			cairo_new_path(cr);
			cairo_move_to(cr, wr.pts[0].x, wr.pts[0].y);
			for (size_t i = 1; i < wr.pts.size(); i++) cairo_line_to(cr, wr.pts[i].x, wr.pts[i].y);
		};
		if (wr.on) {
			path();
			setColor(cr, alpha(accent, 0.18f));
			cairo_set_line_width(cr, 9 * k);
			cairo_stroke(cr);
		}
		path();
		setColor(cr, wr.on ? accent : alpha(ink, 0.32f));
		cairo_set_line_width(cr, 3 * k);
		cairo_stroke(cr);
		if (!wr.on) continue;
		// Signals running along it.
		double len = 0;
		for (size_t i = 1; i < wr.pts.size(); i++) len += std::hypot(wr.pts[i].x - wr.pts[i - 1].x, wr.pts[i].y - wr.pts[i - 1].y);
		const double spacing = 34 * k;
		for (double d = std::fmod(t * 95 * k, spacing); d < len; d += spacing) {
			double left = d;
			PointF q = wr.pts.back();
			for (size_t i = 1; i < wr.pts.size(); i++) {
				const double seg = std::hypot(wr.pts[i].x - wr.pts[i - 1].x, wr.pts[i].y - wr.pts[i - 1].y);
				if (left <= seg) {
					const float f = seg > 0 ? (float)(left / seg) : 0;
					q = pointF(wr.pts[i - 1].x + (wr.pts[i].x - wr.pts[i - 1].x) * f, wr.pts[i - 1].y + (wr.pts[i].y - wr.pts[i - 1].y) * f);
					break;
				}
				left -= seg;
			}
			fillCircle(cr, q, 2.6f * k, colorF(1, 1, 1));
		}
	}
	// The switches.
	const std::pair<float, bool> switches[] = { { cy - 50 * k, a }, { cy + 50 * k, b } };
	for (const auto& sw : switches) {
		const RectF r = rectF(cx - 232 * k, sw.first - 18 * k, cx - 188 * k, sw.first + 18 * k);
		fillRound(cr, r, 8 * k, sw.second ? alpha(accent, 0.22f) : alpha(ink, 0.05f));
		strokeRound(cr, r, 8 * k, sw.second ? accent : alpha(ink, 0.35f), 2);
		const float ts = std::max(8.0f, 15 * k);
		const float th = text(nullptr, "0", 0, 0, ts, Bold, ink);
		text(cr, sw.second ? "1" : "0", r.left, (r.top + r.bottom - th) / 2, ts, Bold, sw.second ? accent : alpha(ink, 0.55f),
		     r.right - r.left, TextAlign::Center);
	}
	// The gate.
	cairo_new_path(cr);
	cairo_move_to(cr, bodyL, cy - hh);
	cairo_line_to(cr, bodyR, cy - hh);
	cairo_curve_to(cr, bodyR + 60 * k, cy - hh, bodyR + 60 * k, cy + hh, bodyR, cy + hh);
	cairo_line_to(cr, bodyL, cy + hh);
	cairo_close_path(cr);
	setColor(cr, alpha(accent, 0.12f));
	cairo_fill_preserve(cr);
	setColor(cr, accent);
	cairo_set_line_width(cr, 3.2 * k);
	cairo_stroke(cr);
	const float as = std::max(8.0f, 13 * k);
	const float ah = text(nullptr, "AND", 0, 0, as, Bold, ink);
	text(cr, "AND", cx + 8 * k - 40, cy - ah / 2, as, Bold, alpha(accent, 0.9f), 80, TextAlign::Center);
	// The lamp.
	const float lx = cx + 172 * k, lr = 22 * k;
	if (out)
		for (int i = 3; i >= 1; i--) fillCircle(cr, pointF(lx, cy), lr + (4 - i) * 9 * k, alpha(lampOn, 0.10f * i));
	fillCircle(cr, pointF(lx, cy), lr, out ? lampOn : alpha(ink, 0.06f));
	strokeCircle(cr, pointF(lx, cy), lr, out ? colorF(230 / 255.0f, 160 / 255.0f, 20 / 255.0f) : alpha(ink, 0.35f), 2.5f * k);
	cairo_restore(cr);
}

void playChime() {
	const std::string file = resourcesDir() + "/FirstLaunch.wav";
	if (!g_file_test(file.c_str(), G_FILE_TEST_EXISTS)) return;
	// PipeWire, then PulseAudio, then plain ALSA.
	for (const char* player : { "pw-play", "paplay", "aplay" }) {
		gchar* found = g_find_program_in_path(player);
		if (found == nullptr) continue;
		const gchar* argv[] = { found, player[0] == 'a' ? "-q" : file.c_str(), player[0] == 'a' ? file.c_str() : nullptr, nullptr };
		g_spawn_async(nullptr, (gchar**)argv, nullptr,
		              (GSpawnFlags)(G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL), nullptr, nullptr, nullptr, nullptr);
		g_free(found);
		return;
	}
}

}  // namespace brand
