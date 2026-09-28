// CairoScene -- see CairoScene.h.

#include "CairoScene.h"
#include "render/SkiaProbe.h"   // measuredTextWidth/Height, supplied below

#include <cairo-ft.h>
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H

#include <cmath>
#include <cstdlib>
#include <unistd.h>

namespace cl {
namespace cairo {

namespace {

// Glyph outlines are built at this size and scaled into place, as the Skia
// backend does (SkiaScene.h), so labels measure and sit the same in every app.
const double kGlyphUnits = 100.0;

cairo_matrix_t toCairo(const render::Transform& t) {
	// Scene transforms are [a c e ; b d f]; cairo_matrix_init takes them in
	// the same order (xx, yx, xy, yy, x0, y0).
	cairo_matrix_t m;
	cairo_matrix_init(&m, t.a, t.b, t.c, t.d, t.e, t.f);
	return m;
}

// A transform Cairo can draw through. A singular one (a zero scale) would put
// the context into an error state that silently ends all drawing on it.
bool usable(const cairo_matrix_t& m) {
	const double det = m.xx * m.yy - m.yx * m.xy;
	return std::isfinite(det) && std::fabs(det) > 1e-12 &&
	       std::isfinite(m.x0) && std::isfinite(m.y0);
}

cairo_line_cap_t toCap(render::Cap c) {
	switch (c) {
	case render::Cap::Round:  return CAIRO_LINE_CAP_ROUND;
	case render::Cap::Square: return CAIRO_LINE_CAP_SQUARE;
	default:                  return CAIRO_LINE_CAP_BUTT;
	}
}

// Outlines, not grid-fitted shapes, with true fractional advances -- what the
// Skia backend asks for.
cairo_font_options_t* outlineOptions() {
	static cairo_font_options_t* o = [] {
		cairo_font_options_t* f = cairo_font_options_create();
		cairo_font_options_set_hint_style(f, CAIRO_HINT_STYLE_NONE);
		cairo_font_options_set_hint_metrics(f, CAIRO_HINT_METRICS_OFF);
		return f;
	}();
	return o;
}

cairo_font_face_t* faceFromFile(const char* path) {
	if (path == nullptr || *path == 0 || access(path, R_OK) != 0) return nullptr;
	FcPattern* p = FcPatternCreate();
	FcPatternAddString(p, FC_FILE, (const FcChar8*)path);
	FcPatternAddInteger(p, FC_INDEX, 0);
	cairo_font_face_t* f = cairo_ft_font_face_create_for_pattern(p);
	FcPatternDestroy(p);
	return f;
}

cairo_font_face_t* faceFromFontconfig(const char* name) {
	FcPattern* p = FcNameParse((const FcChar8*)name);
	if (p == nullptr) return nullptr;
	FcConfigSubstitute(nullptr, p, FcMatchPattern);
	FcDefaultSubstitute(p);
	FcResult result;
	FcPattern* match = FcFontMatch(nullptr, p, &result);
	FcPatternDestroy(p);
	if (match == nullptr) return nullptr;
	cairo_font_face_t* f = cairo_ft_font_face_create_for_pattern(match);
	FcPatternDestroy(match);
	return f;
}

// The label font at kGlyphUnits, for layout and measuring (null if none).
cairo_scaled_font_t* labelFont() {
	static cairo_scaled_font_t* font = []() -> cairo_scaled_font_t* {
		cairo_font_face_t* face = labelFace();
		if (face == nullptr) return nullptr;
		cairo_matrix_t size, identity;
		cairo_matrix_init_scale(&size, kGlyphUnits, kGlyphUnits);
		cairo_matrix_init_identity(&identity);
		cairo_scaled_font_t* f = cairo_scaled_font_create(face, &size, &identity, outlineOptions());
		if (cairo_scaled_font_status(f) != CAIRO_STATUS_SUCCESS) {
			cairo_scaled_font_destroy(f);
			return nullptr;
		}
		return f;
	}();
	return font;
}

// The height of the capitals at kGlyphUnits: the font's own figure (what
// Skia reads), else its ascent.
double capHeight() {
	static double cap = []() -> double {
		cairo_scaled_font_t* f = labelFont();
		if (f == nullptr) return 0.0;
		double h = 0.0;
		if (FT_Face face = cairo_ft_scaled_font_lock_face(f)) {
			const TT_OS2* os2 = (const TT_OS2*)FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
			if (os2 != nullptr && os2->version >= 2 && os2->sCapHeight > 0 && face->units_per_EM > 0)
				h = os2->sCapHeight * kGlyphUnits / face->units_per_EM;
			cairo_ft_scaled_font_unlock_face(f);
		}
		if (h <= 0.0) {
			cairo_font_extents_t fe;
			cairo_scaled_font_extents(f, &fe);
			h = fe.ascent;
		}
		return h;
	}();
	return cap;
}

}  // namespace

cairo_font_face_t* labelFace() {
	// The wx app's search on Linux (SkiaBackend::defaultFont): an explicit
	// file, the usual bold sans faces, then fontconfig's bold sans-serif.
	static cairo_font_face_t* face = []() -> cairo_font_face_t* {
		const char* candidates[] = {
			std::getenv("CEDAR_FONT_FILE"),
			"/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
			"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
			"/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
			"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
			"/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
			"/usr/share/fonts/dejavu/DejaVuSans.ttf",
		};
		for (const char* path : candidates) {
			if (cairo_font_face_t* f = faceFromFile(path)) return f;
		}
		return faceFromFontconfig("sans-serif:bold");
	}();
	return face;
}

CairoScene::CairoScene(cairo_t* ctx) : ctx(ctx) {
	cairo_matrix_init_identity(&viewport);
}

cairo_matrix_t CairoScene::current() const {
	// The innermost local transform applies first, the viewport last.
	cairo_matrix_t m = viewport;
	for (const cairo_matrix_t& t : stack) {
		cairo_matrix_t r;
		cairo_matrix_multiply(&r, &t, &m);
		m = r;
	}
	return m;
}

void CairoScene::setViewport(const render::Transform& worldToDevice) {
	viewport = toCairo(worldToDevice);
	stack.clear();
}

void CairoScene::pushTransform(const render::Transform& local) {
	stack.push_back(toCairo(local));
}

void CairoScene::popTransform() {
	if (!stack.empty()) stack.pop_back();
}

bool CairoScene::remapLowWire = false;
render::Color CairoScene::lowWire(0.62f, 0.67f, 0.76f, 1);

render::Color CairoScene::mapped(const render::Color& c) const {
	if (!wiresPhase || !remapLowWire) return c;
	const float g = 0.35f, e = 0.004f;
	if (std::fabs(c.r - g) < e && std::fabs(c.g - g) < e && std::fabs(c.b - g) < e)
		return render::Color(lowWire.r, lowWire.g, lowWire.b, c.a);
	return c;
}

void CairoScene::moveTo(const cairo_matrix_t& m, const render::Point& p) {
	double x = p.x, y = p.y;
	cairo_matrix_transform_point(&m, &x, &y);
	if (std::isfinite(x) && std::isfinite(y)) cairo_move_to(ctx, x, y);
}

void CairoScene::lineTo(const cairo_matrix_t& m, const render::Point& p) {
	double x = p.x, y = p.y;
	cairo_matrix_transform_point(&m, &x, &y);
	if (std::isfinite(x) && std::isfinite(y)) cairo_line_to(ctx, x, y);
}

void CairoScene::addCircle(const cairo_matrix_t& m, render::Point center, float radius) {
	if (!usable(m) || !(radius > 0)) return;
	cairo_save(ctx);
	cairo_transform(ctx, &m);
	cairo_new_sub_path(ctx);
	cairo_arc(ctx, center.x, center.y, radius, 0, 2 * M_PI);
	cairo_close_path(ctx);
	cairo_restore(ctx);   // the path stays; strokes are sized in device pixels
}

void CairoScene::stroke(const render::Stroke& s0) {
	const render::Color c = mapped(s0.color);
	cairo_save(ctx);
	// Device pixels, like the wx app; zero is a hairline, as in Skia.
	cairo_set_line_width(ctx, s0.width > 0 ? s0.width : 1.0);
	cairo_set_line_cap(ctx, toCap(s0.cap));
	if (s0.dashed) {
		const double dash[] = { 3.0, 3.0 };
		cairo_set_dash(ctx, dash, 2, 0);
	}
	cairo_set_source_rgba(ctx, c.r, c.g, c.b, c.a);
	cairo_stroke(ctx);
	cairo_restore(ctx);
}

void CairoScene::fill(const render::Color& c0) {
	const render::Color c = mapped(c0);
	cairo_save(ctx);
	cairo_set_source_rgba(ctx, c.r, c.g, c.b, c.a);
	cairo_fill(ctx);
	cairo_restore(ctx);
}

void CairoScene::lines(const render::Point* pts, std::size_t count, const render::Stroke& s) {
	if (count < 2) return;
	const cairo_matrix_t m = current();
	cairo_new_path(ctx);
	for (std::size_t i = 0; i + 1 < count; i += 2) {
		moveTo(m, pts[i]);
		lineTo(m, pts[i + 1]);
	}
	stroke(s);
}

void CairoScene::polyline(const render::Point* pts, std::size_t count, const render::Stroke& s, bool closed) {
	if (count < 2) return;
	const cairo_matrix_t m = current();
	cairo_new_path(ctx);
	moveTo(m, pts[0]);
	for (std::size_t i = 1; i < count; i++) lineTo(m, pts[i]);
	if (closed) cairo_close_path(ctx);
	stroke(s);
}

void CairoScene::fillPolygon(const render::Point* pts, std::size_t count, const render::Color& c) {
	if (count < 3) return;
	const cairo_matrix_t m = current();
	cairo_new_path(ctx);
	moveTo(m, pts[0]);
	for (std::size_t i = 1; i < count; i++) lineTo(m, pts[i]);
	cairo_close_path(ctx);
	fill(c);
}

void CairoScene::fillCircle(render::Point center, float radius, const render::Color& c) {
	cairo_new_path(ctx);
	addCircle(current(), center, radius);
	fill(c);
}

void CairoScene::strokeCircle(render::Point center, float radius, const render::Stroke& s) {
	cairo_new_path(ctx);
	addCircle(current(), center, radius);
	stroke(s);
}

void CairoScene::arc(render::Point center, float radius, float startDeg, float sweepDeg, const render::Stroke& s) {
	const cairo_matrix_t m = current();
	cairo_new_path(ctx);
	if (std::fabs(sweepDeg) >= 360.0f) {
		// A whole turn is asked for as a circle (see SkiaScene::arc for why).
		addCircle(m, center, radius);
	} else if (usable(m) && radius > 0) {
		// Scene angles run clockwise from +Y; Cairo measures from +X toward +Y
		// (in this y-up world space, counterclockwise). Clockwise sweeps stay
		// clockwise: decreasing angles.
		const double k = M_PI / 180.0;
		const double a0 = (90.0 - startDeg) * k, a1 = (90.0 - (startDeg + sweepDeg)) * k;
		cairo_save(ctx);
		cairo_transform(ctx, &m);
		cairo_new_sub_path(ctx);
		if (sweepDeg > 0) cairo_arc_negative(ctx, center.x, center.y, radius, a0, a1);
		else cairo_arc(ctx, center.x, center.y, radius, a0, a1);
		cairo_restore(ctx);
	}
	stroke(s);
}

void CairoScene::fillRect(render::Point lo, render::Point hi, const render::Color& c) {
	const cairo_matrix_t m = current();
	const float l = std::fmin(lo.x, hi.x), r = std::fmax(lo.x, hi.x);
	const float b = std::fmin(lo.y, hi.y), t = std::fmax(lo.y, hi.y);
	cairo_new_path(ctx);
	moveTo(m, render::Point(l, b));
	lineTo(m, render::Point(r, b));
	lineTo(m, render::Point(r, t));
	lineTo(m, render::Point(l, t));
	cairo_close_path(ctx);
	fill(c);
}

void CairoScene::text(render::Point origin, const char* utf8, float pixelHeight, const render::Color& c) {
	if (utf8 == nullptr || *utf8 == 0 || !(pixelHeight > 0)) return;
	cairo_scaled_font_t* font = labelFont();
	if (font == nullptr) return;
	cairo_glyph_t* glyphs = nullptr;
	int n = 0;
	if (cairo_scaled_font_text_to_glyphs(font, 0, 0, utf8, -1, &glyphs, &n, nullptr, nullptr, nullptr)
	    != CAIRO_STATUS_SUCCESS || n <= 0) {
		if (glyphs) cairo_glyph_free(glyphs);
		return;
	}
	// `origin` is the top of the capitals (see SkiaScene::text): the baseline
	// sits a cap height below it. Glyph outlines are y-down; the world is y-up.
	const double k = pixelHeight / kGlyphUnits;
	cairo_matrix_t place, full;
	cairo_matrix_init(&place, k, 0, 0, -k, origin.x, origin.y - capHeight() * k);
	const cairo_matrix_t m = current();
	cairo_matrix_multiply(&full, &place, &m);
	cairo_new_path(ctx);
	if (usable(full)) {
		cairo_save(ctx);
		cairo_transform(ctx, &full);
		cairo_set_font_face(ctx, labelFace());
		cairo_set_font_size(ctx, kGlyphUnits);
		cairo_set_font_options(ctx, outlineOptions());
		cairo_glyph_path(ctx, glyphs, n);
		cairo_restore(ctx);
		fill(c);
	}
	cairo_glyph_free(glyphs);
}

}  // namespace cairo

// Text metrics for the gate code's hit boxes, measured the way text() draws.
namespace render {

float measuredTextWidth(const char* utf8, float pixelHeight) {
	if (utf8 == nullptr || *utf8 == 0) return 0.0f;
	cairo_scaled_font_t* f = cl::cairo::labelFont();
	if (f == nullptr) return 0.0f;
	cairo_text_extents_t e;
	cairo_scaled_font_text_extents(f, utf8, &e);
	return (float)(e.x_advance * pixelHeight / cl::cairo::kGlyphUnits);
}

float measuredTextHeight(float pixelHeight) {
	cairo_scaled_font_t* f = cl::cairo::labelFont();
	if (f == nullptr) return 0.0f;
	cairo_font_extents_t fe;
	cairo_scaled_font_extents(f, &fe);
	return (float)((cl::cairo::capHeight() + fe.descent) * pixelHeight / cl::cairo::kGlyphUnits);
}

}  // namespace render
}  // namespace cl
