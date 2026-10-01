// D2DScene -- see D2DScene.h.

#include <d2d1.h>
#include <dwrite.h>

#include "D2DScene.h"
#include "render/SkiaProbe.h"   // measuredTextWidth/Height, supplied below

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <tuple>

namespace cl {
namespace d2d {

namespace {

// Glyph outlines are built at this size and scaled into place, as the Skia
// backend does (SkiaScene.h), so labels measure and sit the same in every app.
const float kGlyphUnits = 100.0f;
const double kPi = 3.14159265358979323846;

template <class T> void release(T*& p) {
	if (p) p->Release();
	p = nullptr;
}

static_assert(sizeof(Matrix) == sizeof(D2D1_MATRIX_3X2_F), "Matrix is D2D1_MATRIX_3X2_F");
static_assert(sizeof(Point2) == sizeof(D2D1_POINT_2F), "Point2 is D2D1_POINT_2F");

// Scene transforms are [a c e ; b d f]; Direct2D's row-vector matrices hold
// the same numbers as _11 _12 / _21 _22 / _31 _32.
Matrix toD2D(const render::Transform& t) {
	Matrix m;
	m._11 = t.a; m._12 = t.b;
	m._21 = t.c; m._22 = t.d;
	m._31 = t.e; m._32 = t.f;
	return m;
}

// a then b.
Matrix multiply(const Matrix& a, const Matrix& b) {
	Matrix r;
	r._11 = a._11 * b._11 + a._12 * b._21;
	r._12 = a._11 * b._12 + a._12 * b._22;
	r._21 = a._21 * b._11 + a._22 * b._21;
	r._22 = a._21 * b._12 + a._22 * b._22;
	r._31 = a._31 * b._11 + a._32 * b._21 + b._31;
	r._32 = a._31 * b._12 + a._32 * b._22 + b._32;
	return r;
}

Matrix identity() {
	Matrix m = {};
	m._11 = m._22 = 1;
	return m;
}

// A transform geometry can go through: a singular one (a zero scale) would
// draw nothing sensible.
bool usable(const Matrix& m) {
	const double det = (double)m._11 * m._22 - (double)m._12 * m._21;
	return std::isfinite(det) && std::fabs(det) > 1e-12 && std::isfinite(m._31) && std::isfinite(m._32);
}

bool apply(const Matrix& m, const render::Point& p, Point2& out) {
	out.x = p.x * m._11 + p.y * m._21 + m._31;
	out.y = p.x * m._12 + p.y * m._22 + m._32;
	return std::isfinite(out.x) && std::isfinite(out.y);
}

D2D1_CAP_STYLE toCap(render::Cap c) {
	switch (c) {
	case render::Cap::Round:  return D2D1_CAP_STYLE_ROUND;
	case render::Cap::Square: return D2D1_CAP_STYLE_SQUARE;
	default:                  return D2D1_CAP_STYLE_FLAT;
	}
}

// ---- The label font (the wx app's on Windows: Arial Bold) --------------------

struct LabelFont {
	IDWriteFontFace* face = nullptr;
	DWRITE_FONT_METRICS metrics = {};
};

const LabelFont& labelFont() {
	static LabelFont font = [] {
		LabelFont f;
		IDWriteFactory* dw = writeFactory();
		if (dw == nullptr) return f;
		IDWriteFontCollection* fonts = nullptr;
		if (FAILED(dw->GetSystemFontCollection(&fonts, FALSE)) || fonts == nullptr) return f;
		const wchar_t* families[] = { L"Arial", L"Segoe UI", L"Tahoma" };
		for (const wchar_t* name : families) {
			UINT32 index = 0;
			BOOL exists = FALSE;
			if (FAILED(fonts->FindFamilyName(name, &index, &exists)) || !exists) continue;
			IDWriteFontFamily* family = nullptr;
			IDWriteFont* match = nullptr;
			if (SUCCEEDED(fonts->GetFontFamily(index, &family)) &&
			    SUCCEEDED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STRETCH_NORMAL,
			                                           DWRITE_FONT_STYLE_NORMAL, &match)) &&
			    SUCCEEDED(match->CreateFontFace(&f.face))) {
				f.face->GetMetrics(&f.metrics);
			}
			release(match);
			release(family);
			if (f.face) break;
		}
		release(fonts);
		return f;
	}();
	return font;
}

// UTF-8 to code points (a bad byte reads as U+FFFD).
std::vector<UINT32> codePoints(const char* utf8) {
	std::vector<UINT32> out;
	const unsigned char* s = (const unsigned char*)utf8;
	while (*s) {
		UINT32 c = *s;
		int extra = 0;
		if (c < 0x80) extra = 0;
		else if ((c & 0xE0) == 0xC0) { c &= 0x1F; extra = 1; }
		else if ((c & 0xF0) == 0xE0) { c &= 0x0F; extra = 2; }
		else if ((c & 0xF8) == 0xF0) { c &= 0x07; extra = 3; }
		else { out.push_back(0xFFFD); s++; continue; }
		s++;
		bool ok = true;
		for (int i = 0; i < extra; i++) {
			if ((*s & 0xC0) != 0x80) { ok = false; break; }
			c = (c << 6) | (*s & 0x3F);
			s++;
		}
		out.push_back(ok ? c : 0xFFFD);
	}
	return out;
}

// A line of text's glyphs and their advances at kGlyphUnits.
struct Shaped {
	std::vector<UINT16> glyphs;
	std::vector<FLOAT> advances;
	float width = 0;
};

bool shape(const char* utf8, Shaped& out) {
	const LabelFont& f = labelFont();
	if (f.face == nullptr || utf8 == nullptr || *utf8 == 0 || f.metrics.designUnitsPerEm == 0) return false;
	const std::vector<UINT32> cps = codePoints(utf8);
	if (cps.empty()) return false;
	out.glyphs.assign(cps.size(), 0);
	if (FAILED(f.face->GetGlyphIndices(cps.data(), (UINT32)cps.size(), out.glyphs.data()))) return false;
	std::vector<DWRITE_GLYPH_METRICS> gm(cps.size());
	if (FAILED(f.face->GetDesignGlyphMetrics(out.glyphs.data(), (UINT32)out.glyphs.size(), gm.data(), FALSE)))
		return false;
	const float k = kGlyphUnits / f.metrics.designUnitsPerEm;
	out.advances.resize(gm.size());
	out.width = 0;
	for (size_t i = 0; i < gm.size(); i++) {
		out.advances[i] = gm[i].advanceWidth * k;
		out.width += out.advances[i];
	}
	return true;
}

float capHeight() {
	const LabelFont& f = labelFont();
	if (f.metrics.designUnitsPerEm == 0) return 0;
	const float cap = f.metrics.capHeight > 0 ? f.metrics.capHeight : f.metrics.ascent;
	return cap * kGlyphUnits / f.metrics.designUnitsPerEm;
}

}  // namespace

ID2D1Factory* factory() {
	static ID2D1Factory* f = [] {
		ID2D1Factory* made = nullptr;
		D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr, (void**)&made);
		return made;
	}();
	return f;
}

IDWriteFactory* writeFactory() {
	static IDWriteFactory* f = [] {
		IUnknown* made = nullptr;
		DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &made);
		return (IDWriteFactory*)made;
	}();
	return f;
}

SavedTransform scaleTransform(ID2D1RenderTarget* rt, double factor) {
	SavedTransform saved;
	rt->GetTransform((D2D1_MATRIX_3X2_F*)&saved.m);
	Matrix s = identity();
	s._11 = s._22 = (float)factor;
	const Matrix m = multiply(s, saved.m);
	rt->SetTransform((const D2D1_MATRIX_3X2_F*)&m);
	return saved;
}

void restoreTransform(ID2D1RenderTarget* rt, const SavedTransform& saved) {
	rt->SetTransform((const D2D1_MATRIX_3X2_F*)&saved.m);
}

D2DScene::D2DScene(ID2D1RenderTarget* rt) : rt(rt), viewport(identity()) {
	rt->GetFactory(&factory);
	rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &brush);
}

D2DScene::~D2DScene() {
	release(brush);
	release(factory);
}

Matrix D2DScene::current() const {
	// The innermost local transform applies first, the viewport last.
	Matrix m = viewport;
	for (const Matrix& t : stack) m = multiply(t, m);
	return m;
}

void D2DScene::setViewport(const render::Transform& worldToDevice) {
	viewport = toD2D(worldToDevice);
	stack.clear();
}

void D2DScene::pushTransform(const render::Transform& local) { stack.push_back(toD2D(local)); }

void D2DScene::popTransform() {
	if (!stack.empty()) stack.pop_back();
}

bool D2DScene::remapLowWire = false;
render::Color D2DScene::lowWire(0.62f, 0.67f, 0.76f, 1);

render::Color D2DScene::mapped(const render::Color& c) const {
	if (!wiresPhase || !remapLowWire) return c;
	const float g = 0.35f, e = 0.004f;
	if (std::fabs(c.r - g) < e && std::fabs(c.g - g) < e && std::fabs(c.b - g) < e)
		return render::Color(lowWire.r, lowWire.g, lowWire.b, c.a);
	return c;
}

ID2D1SolidColorBrush* D2DScene::paint(const render::Color& c0) {
	if (brush == nullptr) return nullptr;
	const render::Color c = mapped(c0);
	brush->SetColor(D2D1::ColorF(c.r, c.g, c.b, c.a));
	return brush;
}

ID2D1StrokeStyle* D2DScene::strokeStyle(const render::Stroke& s) {
	if (factory == nullptr) return nullptr;
	// Kept for the life of the app: a circuit asks for the same handful over
	// and over, every frame. A style belongs to the factory that made it, so
	// the factory is part of the key.
	const float w = s.width > 0 ? s.width : 1.0f;
	const auto key = std::make_tuple((void*)factory, (int)s.cap, s.dashed, s.dashed ? (int)std::lround(w * 100) : 0);
	static std::map<std::tuple<void*, int, bool, int>, ID2D1StrokeStyle*> cache;
	auto it = cache.find(key);
	if (it != cache.end()) return it->second;
	const D2D1_CAP_STYLE cap = toCap(s.cap);
	D2D1_STROKE_STYLE_PROPERTIES p = D2D1::StrokeStyleProperties(cap, cap, cap, D2D1_LINE_JOIN_MITER, 10.0f,
	                                                             s.dashed ? D2D1_DASH_STYLE_CUSTOM : D2D1_DASH_STYLE_SOLID, 0.0f);
	ID2D1StrokeStyle* style = nullptr;
	if (s.dashed) {
		// 3 pixels on, 3 off, as the other backends draw it. Direct2D measures
		// dashes in stroke widths.
		const float dash[] = { 3.0f / w, 3.0f / w };
		factory->CreateStrokeStyle(p, dash, 2, &style);
	} else {
		factory->CreateStrokeStyle(p, nullptr, 0, &style);
	}
	cache[key] = style;
	return style;
}

void D2DScene::strokeGeometry(ID2D1Geometry* g, const render::Stroke& s) {
	if (g == nullptr) return;
	ID2D1SolidColorBrush* b = paint(s.color);
	if (b == nullptr) return;
	ID2D1StrokeStyle* style = strokeStyle(s);
	// Device pixels, like the wx app; zero is a hairline, as in Skia.
	rt->DrawGeometry(g, b, s.width > 0 ? s.width : 1.0f, style);
}

void D2DScene::fillGeometry(ID2D1Geometry* g, const render::Color& c) {
	if (g == nullptr) return;
	if (ID2D1SolidColorBrush* b = paint(c)) rt->FillGeometry(g, b);
}

ID2D1PathGeometry* D2DScene::path(const std::vector<Point2>& pts, bool closed, bool filled) {
	if (factory == nullptr || pts.size() < 2) return nullptr;
	ID2D1PathGeometry* g = nullptr;
	if (FAILED(factory->CreatePathGeometry(&g))) return nullptr;
	ID2D1GeometrySink* sink = nullptr;
	if (FAILED(g->Open(&sink))) { release(g); return nullptr; }
	sink->BeginFigure(D2D1::Point2F(pts[0].x, pts[0].y), filled ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
	sink->AddLines((const D2D1_POINT_2F*)pts.data() + 1, (UINT32)(pts.size() - 1));
	sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
	sink->Close();
	release(sink);
	return g;
}

ID2D1Geometry* D2DScene::circle(const Matrix& m, render::Point center, float radius) {
	if (factory == nullptr || !usable(m) || !(radius > 0)) return nullptr;
	ID2D1EllipseGeometry* e = nullptr;
	if (FAILED(factory->CreateEllipseGeometry(D2D1::Ellipse(D2D1::Point2F(center.x, center.y), radius, radius), &e)))
		return nullptr;
	ID2D1TransformedGeometry* t = nullptr;
	factory->CreateTransformedGeometry(e, (const D2D1_MATRIX_3X2_F*)&m, &t);
	release(e);
	return t;
}

void D2DScene::lines(const render::Point* pts, std::size_t count, const render::Stroke& s) {
	if (count < 2) return;
	ID2D1SolidColorBrush* b = paint(s.color);
	if (b == nullptr) return;
	ID2D1StrokeStyle* style = strokeStyle(s);
	const Matrix m = current();
	const float w = s.width > 0 ? s.width : 1.0f;
	for (std::size_t i = 0; i + 1 < count; i += 2) {
		Point2 a, z;
		if (apply(m, pts[i], a) && apply(m, pts[i + 1], z)) rt->DrawLine(D2D1::Point2F(a.x, a.y), D2D1::Point2F(z.x, z.y), b, w, style);
	}
}

void D2DScene::polyline(const render::Point* pts, std::size_t count, const render::Stroke& s, bool closed) {
	if (count < 2) return;
	const Matrix m = current();
	std::vector<Point2> out;
	out.reserve(count);
	for (std::size_t i = 0; i < count; i++) {
		Point2 p;
		if (apply(m, pts[i], p)) out.push_back(p);
	}
	ID2D1PathGeometry* g = path(out, closed, false);
	strokeGeometry(g, s);
	release(g);
}

void D2DScene::fillPolygon(const render::Point* pts, std::size_t count, const render::Color& c) {
	if (count < 3) return;
	const Matrix m = current();
	std::vector<Point2> out;
	out.reserve(count);
	for (std::size_t i = 0; i < count; i++) {
		Point2 p;
		if (apply(m, pts[i], p)) out.push_back(p);
	}
	if (out.size() < 3) return;
	ID2D1PathGeometry* g = path(out, true, true);
	fillGeometry(g, c);
	release(g);
}

void D2DScene::fillCircle(render::Point center, float radius, const render::Color& c) {
	ID2D1Geometry* g = circle(current(), center, radius);
	fillGeometry(g, c);
	release(g);
}

void D2DScene::strokeCircle(render::Point center, float radius, const render::Stroke& s) {
	ID2D1Geometry* g = circle(current(), center, radius);
	strokeGeometry(g, s);
	release(g);
}

void D2DScene::arc(render::Point center, float radius, float startDeg, float sweepDeg, const render::Stroke& s) {
	const Matrix m = current();
	if (std::fabs(sweepDeg) >= 360.0f) {
		// A whole turn is asked for as a circle (see SkiaScene::arc for why).
		strokeCircle(center, radius, s);
		return;
	}
	if (!usable(m) || !(radius > 0)) return;
	// Scene angles run clockwise from +Y (in this y-up world space). The arc
	// is laid down as short chords, fine enough (at most 2 degrees) to read
	// as a curve at any zoom the canvas allows.
	const int segs = std::max(8, (int)std::ceil(std::fabs(sweepDeg) / 2.0f));
	std::vector<Point2> out;
	out.reserve(segs + 1);
	for (int i = 0; i <= segs; i++) {
		const double d = (startDeg + sweepDeg * (double)i / segs) * kPi / 180.0;
		Point2 p;
		if (apply(m, render::Point((float)(center.x + radius * std::sin(d)), (float)(center.y + radius * std::cos(d))), p))
			out.push_back(p);
	}
	ID2D1PathGeometry* g = path(out, false, false);
	strokeGeometry(g, s);
	release(g);
}

void D2DScene::fillRect(render::Point lo, render::Point hi, const render::Color& c) {
	const float l = std::fmin(lo.x, hi.x), r = std::fmax(lo.x, hi.x);
	const float b = std::fmin(lo.y, hi.y), t = std::fmax(lo.y, hi.y);
	const render::Point corners[4] = { render::Point(l, b), render::Point(r, b), render::Point(r, t), render::Point(l, t) };
	fillPolygon(corners, 4, c);
}

// A glyph outline, taken as Direct2D path figures.
void D2DScene::text(render::Point origin, const char* utf8, float pixelHeight, const render::Color& c) {
	if (factory == nullptr || !(pixelHeight > 0)) return;
	Shaped line;
	if (!shape(utf8, line)) return;
	ID2D1PathGeometry* outline = nullptr;
	if (FAILED(factory->CreatePathGeometry(&outline))) return;
	ID2D1GeometrySink* sink = nullptr;
	if (FAILED(outline->Open(&sink))) { release(outline); return; }
	const HRESULT hr = labelFont().face->GetGlyphRunOutline(kGlyphUnits, line.glyphs.data(), line.advances.data(),
	                                                         nullptr, (UINT32)line.glyphs.size(), FALSE, FALSE, sink);
	sink->Close();
	release(sink);
	if (FAILED(hr)) { release(outline); return; }
	// `origin` is the top of the capitals (see SkiaScene::text): the baseline
	// sits a cap height below it. Glyph outlines are y-down; the world is y-up.
	const float k = pixelHeight / kGlyphUnits;
	Matrix place = identity();
	place._11 = k;
	place._22 = -k;
	place._31 = origin.x;
	place._32 = origin.y - capHeight() * k;
	const Matrix full = multiply(place, current());
	if (usable(full)) {
		ID2D1TransformedGeometry* t = nullptr;
		if (SUCCEEDED(factory->CreateTransformedGeometry(outline, (const D2D1_MATRIX_3X2_F*)&full, &t))) fillGeometry(t, c);
		release(t);
	}
	release(outline);
}

}  // namespace d2d

// Text metrics for the gate code's hit boxes, measured the way text() draws.
namespace render {

float measuredTextWidth(const char* utf8, float pixelHeight) {
	d2d::Shaped line;
	if (!d2d::shape(utf8, line)) return 0.0f;
	return line.width * pixelHeight / d2d::kGlyphUnits;
}

float measuredTextHeight(float pixelHeight) {
	const d2d::LabelFont& f = d2d::labelFont();
	if (f.metrics.designUnitsPerEm == 0) return 0.0f;
	const float descent = f.metrics.descent * d2d::kGlyphUnits / f.metrics.designUnitsPerEm;
	return (d2d::capHeight() + descent) * pixelHeight / d2d::kGlyphUnits;
}

}  // namespace render
}  // namespace cl
