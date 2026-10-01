// CedarLogic's own look (see Brand.h).

#include "Brand.h"
#include "Chrome.h"
#include "Images.h"

#include <d2d1_1.h>
#include <d2d1effects.h>
#include <dwrite_1.h>

#include <algorithm>
#include <cmath>
#include <map>

namespace brand {

const D2D1_COLOR_F kInk = D2D1::ColorF(0.035f, 0.063f, 0.047f), kInkDeep = D2D1::ColorF(0.012f, 0.024f, 0.018f);
const D2D1_COLOR_F kNeon = D2D1::ColorF(0.22f, 1.0f, 0.42f), kNeonDeep = D2D1::ColorF(0.05f, 0.78f, 0.26f);
const D2D1_COLOR_F kDim = D2D1::ColorF(0.62f, 0.74f, 0.66f);
const D2D1_COLOR_F kPrimary = D2D1::ColorF(0.95f, 0.95f, 0.95f), kSecondary = D2D1::ColorF(0.62f, 0.74f, 0.66f),
                   kFaint = D2D1::ColorF(1, 1, 1, 0.4f);

double easeOut(double t) { const double c = std::min(1.0, std::max(0.0, t)); return 1 - std::pow(1 - c, 3); }

// A cubic Bezier timing curve from (0,0) to (1,1) with control points
// (0.33, 1) and (0.68, 1): solved for x by bisection.
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

template <typename T> void release(T*& p) { if (p) { p->Release(); p = nullptr; } }

ID2D1LinearGradientBrush* linear(ID2D1RenderTarget* rt, D2D1_POINT_2F a, D2D1_POINT_2F b, std::initializer_list<D2D1_GRADIENT_STOP> stops) {
	std::vector<D2D1_GRADIENT_STOP> s(stops);
	ID2D1GradientStopCollection* c = nullptr;
	ID2D1LinearGradientBrush* br = nullptr;
	if (SUCCEEDED(rt->CreateGradientStopCollection(s.data(), (UINT32)s.size(), &c)))
		rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(a, b), c, &br);
	release(c);
	return br;
}

ID2D1RadialGradientBrush* radial(ID2D1RenderTarget* rt, D2D1_POINT_2F center, float radius, std::initializer_list<D2D1_GRADIENT_STOP> stops) {
	std::vector<D2D1_GRADIENT_STOP> s(stops);
	ID2D1GradientStopCollection* c = nullptr;
	ID2D1RadialGradientBrush* br = nullptr;
	if (SUCCEEDED(rt->CreateGradientStopCollection(s.data(), (UINT32)s.size(), &c)))
		rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(center, D2D1::Point2F(0, 0), radius, radius), c, &br);
	release(c);
	return br;
}

// A soft glow around a rounded rect: a few wider, fainter layers.
void glowRound(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, float radius, D2D1_COLOR_F c, float spread) {
	for (int i = 3; i >= 1; i--) {
		const float g = spread * i / 3;
		fillRound(rt, D2D1::RectF(r.left - g, r.top - g, r.right + g, r.bottom + g), radius + g, alpha(c, c.a * 0.28f));
	}
}

IDWriteTextFormat* format(float size, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT align, bool wrap) {
	static std::map<std::tuple<int, int, int, bool>, IDWriteTextFormat*> made;
	const auto key = std::make_tuple((int)(size * 10), (int)weight, (int)align, wrap);
	auto it = made.find(key);
	if (it != made.end()) return it->second;
	IDWriteTextFormat* f = nullptr;
	// Windows 11's display and text cuts of Segoe; Segoe UI before it.
	const wchar_t* family = size >= 20 ? L"Segoe UI Variable Display" : L"Segoe UI Variable Text";
	IDWriteFontCollection* fonts = nullptr;
	UINT32 index = 0;
	BOOL exists = FALSE;
	if (dwFactory() && SUCCEEDED(dwFactory()->GetSystemFontCollection(&fonts))) fonts->FindFamilyName(family, &index, &exists);
	release(fonts);
	if (!exists) family = L"Segoe UI";
	if (dwFactory() && SUCCEEDED(dwFactory()->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
	                                                          size, L"", &f))) {
		f->SetTextAlignment(align);
		f->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
	}
	made[key] = f;
	return f;
}

IDWriteTextLayout* layout(const std::string& s, float size, DWRITE_FONT_WEIGHT weight, float width, DWRITE_TEXT_ALIGNMENT align,
                          float spacing) {
	IDWriteTextFormat* f = format(size, weight, align, width > 0);
	IDWriteTextLayout* l = nullptr;
	const std::wstring w = W(s);
	if (f == nullptr || FAILED(dwFactory()->CreateTextLayout(w.c_str(), (UINT32)w.size(), f, width > 0 ? width : 4000, 2000, &l))) return nullptr;
	if (spacing != 0) {
		IDWriteTextLayout1* l1 = nullptr;
		if (SUCCEEDED(l->QueryInterface(__uuidof(IDWriteTextLayout1), (void**)&l1))) {
			l1->SetCharacterSpacing(0, spacing, 0, DWRITE_TEXT_RANGE{ 0, (UINT32)w.size() });
			l1->Release();
		}
	}
	return l;
}

}  // namespace

float text(ID2D1RenderTarget* rt, const std::string& s, float x, float y, float size, DWRITE_FONT_WEIGHT weight, const D2D1_COLOR_F& color,
           float width, DWRITE_TEXT_ALIGNMENT align, float spacing) {
	if (s.empty()) return 0;
	IDWriteTextLayout* l = layout(s, size, weight, width, align, spacing);
	if (l == nullptr) return 0;
	DWRITE_TEXT_METRICS m = {};
	l->GetMetrics(&m);
	if (rt) {
		ID2D1SolidColorBrush* b = nullptr;
		if (SUCCEEDED(rt->CreateSolidColorBrush(color, &b))) {
			rt->DrawTextLayout(D2D1::Point2F(x, y), l, b);
			b->Release();
		}
	}
	l->Release();
	return m.height;
}

float textWidth(const std::string& s, float size, DWRITE_FONT_WEIGHT weight, float spacing) {
	IDWriteTextLayout* l = layout(s, size, weight, 0, DWRITE_TEXT_ALIGNMENT_LEADING, spacing);
	if (l == nullptr) return 0;
	DWRITE_TEXT_METRICS m = {};
	l->GetMetrics(&m);
	l->Release();
	return m.widthIncludingTrailingWhitespace;
}

void ground(ID2D1RenderTarget* rt, float w, float h, float bloomX, float bloomY, float gridStep) {
	if (ID2D1LinearGradientBrush* b = linear(rt, D2D1::Point2F(0, 0), D2D1::Point2F(0, h), { { 0, kInk }, { 1, kInkDeep } })) {
		rt->FillRectangle(D2D1::RectF(0, 0, w, h), b);
		b->Release();
	}
	// The grid, through a mask that fades it out from the middle.
	ID2D1RadialGradientBrush* mask = radial(rt, D2D1::Point2F(w / 2, h / 2), 520,
	                                        { { 0, D2D1::ColorF(1, 1, 1, 1) }, { 60.0f / 520, D2D1::ColorF(1, 1, 1, 1) },
	                                          { 1, D2D1::ColorF(1, 1, 1, 0) } });
	ID2D1SolidColorBrush* line = nullptr;
	if (mask && SUCCEEDED(rt->CreateSolidColorBrush(alpha(kNeon, 0.06f), &line))) {
		rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), 1, mask),
		              nullptr);
		for (float x = std::fmod(w, gridStep) / 2; x < w; x += gridStep) rt->DrawLine(D2D1::Point2F(x, 0), D2D1::Point2F(x, h), line, 0.5f);
		for (float y = std::fmod(h, gridStep) / 2; y < h; y += gridStep) rt->DrawLine(D2D1::Point2F(0, y), D2D1::Point2F(w, y), line, 0.5f);
		rt->PopLayer();
	}
	release(line);
	release(mask);
	if (ID2D1RadialGradientBrush* bloom = radial(rt, D2D1::Point2F(w * bloomX, h * bloomY), 360,
	                                             { { 0, alpha(kNeon, 0.16f) }, { 4.0f / 360, alpha(kNeon, 0.16f) }, { 1, alpha(kNeon, 0) } })) {
		rt->FillRectangle(D2D1::RectF(0, 0, w, h), bloom);
		bloom->Release();
	}
}

void card(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, bool lit, float radius) {
	if (lit) glowRound(rt, r, radius, alpha(kNeon, 0.18f), 10);
	fillRound(rt, r, radius, lit ? alpha(kNeon, 0.09f) : D2D1::ColorF(1, 1, 1, 0.045f));
	const D2D1_RECT_F in = D2D1::RectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f);
	strokeRound(rt, in, radius, lit ? alpha(kNeon, 0.6f) : D2D1::ColorF(1, 1, 1, 0.09f), lit ? 1.3f : 1.0f);
}

void button(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, const std::string& label, bool primary, bool hot) {
	const float rad = (r.bottom - r.top) / 2;
	const D2D1_RECT_F in = D2D1::RectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f);
	if (primary) {
		glowRound(rt, r, rad, alpha(kNeon, hot ? 0.55f : 0.4f), 10);
		if (ID2D1LinearGradientBrush* b = linear(rt, D2D1::Point2F(0, r.top), D2D1::Point2F(0, r.bottom),
		                                         { { 0, hot ? D2D1::ColorF(0.42f, 1.0f, 0.58f) : kNeon }, { 1, kNeonDeep } })) {
			rt->FillRoundedRectangle(D2D1::RoundedRect(r, rad, rad), b);
			b->Release();
		}
		strokeRound(rt, in, rad, D2D1::ColorF(1, 1, 1, 0.35f));
	} else {
		fillRound(rt, r, rad, D2D1::ColorF(1, 1, 1, hot ? 0.14f : 0.08f));
		strokeRound(rt, in, rad, D2D1::ColorF(1, 1, 1, 0.14f));
	}
	const float th = 13 * 1.34f;
	text(rt, label, r.left, (r.top + r.bottom - th) / 2, 13, DWRITE_FONT_WEIGHT_SEMI_BOLD, primary ? kInk : kPrimary, r.right - r.left,
	     DWRITE_TEXT_ALIGNMENT_CENTER);
}

float keycapWidth(const std::string& label, float size) {
	return std::max(size, textWidth(label, size * 0.42f, DWRITE_FONT_WEIGHT_BOLD) + (label.size() > 1 ? 12 + size * 0.4f : 0));
}

void keycap(ID2D1RenderTarget* rt, float x, float y, const std::string& label, bool lit, float size) {
	const float w = keycapWidth(label, size), rad = size * 0.22f;
	const D2D1_RECT_F r = D2D1::RectF(x, y, x + w, y + size);
	if (lit) glowRound(rt, r, rad, alpha(kNeon, 0.6f), 10);
	fillRound(rt, r, rad, lit ? kNeon : D2D1::ColorF(1, 1, 1, 0.07f));
	strokeRound(rt, D2D1::RectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f), rad,
	            lit ? D2D1::ColorF(1, 1, 1, 0.4f) : D2D1::ColorF(1, 1, 1, 0.16f));
	const float ts = size * 0.42f;
	text(rt, label, x, y + (size - ts * 1.34f) / 2, ts, DWRITE_FONT_WEIGHT_BOLD, lit ? kInk : kPrimary, w, DWRITE_TEXT_ALIGNMENT_CENTER);
}

void segmented(ID2D1RenderTarget* rt, float x, float y, const std::vector<std::string>& options, int chosen,
               std::vector<D2D1_RECT_F>& hits) {
	hits.clear();
	float total = 6;
	std::vector<float> widths;
	for (size_t i = 0; i < options.size(); i++) {
		const bool on = (int)i == chosen;
		widths.push_back(textWidth(options[i], 12.5f, on ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_MEDIUM) + 32);
		total += widths.back() + (i ? 2 : 0);
	}
	const D2D1_RECT_F pill = D2D1::RectF(x, y, x + total, y + 34);
	fillRound(rt, pill, 17, D2D1::ColorF(1, 1, 1, 0.06f));
	strokeRound(rt, D2D1::RectF(pill.left + 0.5f, pill.top + 0.5f, pill.right - 0.5f, pill.bottom - 0.5f), 16.5f, D2D1::ColorF(1, 1, 1, 0.1f));
	float sx = x + 3;
	for (size_t i = 0; i < options.size(); i++) {
		const bool on = (int)i == chosen;
		const D2D1_RECT_F r = D2D1::RectF(sx, y + 3, sx + widths[i], y + 31);
		if (on) fillRound(rt, r, 14, kNeon);
		text(rt, options[i], r.left, r.top + 5, 12.5f, on ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_MEDIUM,
		     on ? kInk : alpha(kPrimary, 0.85f), widths[i], DWRITE_TEXT_ALIGNMENT_CENTER);
		hits.push_back(r);
		sx += widths[i] + 2;
	}
}

float heading(ID2D1RenderTarget* rt, float x, float y, float width, const std::string& eyebrow, const std::string& title,
              const std::string& line) {
	std::string e = eyebrow;
	for (char& c : e) c = (char)toupper((unsigned char)c);
	float at = y;
	at += text(rt, e, x, at, 11, DWRITE_FONT_WEIGHT_BOLD, kNeon, 0, DWRITE_TEXT_ALIGNMENT_LEADING, 1.6f) + 8;
	at += text(rt, title, x, at, 28, DWRITE_FONT_WEIGHT_BOLD, kPrimary, width) + 8;
	at += text(rt, line, x, at, 13.5f, DWRITE_FONT_WEIGHT_NORMAL, kSecondary, width);
	return at;
}

void label(ID2D1RenderTarget* rt, float x, float y, const std::string& s) {
	std::string u = s;
	for (char& c : u) c = (char)toupper((unsigned char)c);
	text(rt, u, x, y, 10.5f, DWRITE_FONT_WEIGHT_BOLD, kFaint, 0, DWRITE_TEXT_ALIGNMENT_LEADING, 1.2f);
}

void icon(ID2D1RenderTarget* rt, float x, float y, float size, float glowAmount) {
	static std::map<std::pair<ID2D1RenderTarget*, int>, ID2D1Bitmap*> made;
	float dx = 96, dy = 96;
	rt->GetDpi(&dx, &dy);
	const int px = (int)std::ceil(size * dx / 96);
	ID2D1Bitmap*& bmp = made[{ rt, px }];
	if (bmp == nullptr) {
		if (IWICBitmap* src = images::loadResource(2, px, px)) {
			rt->CreateBitmapFromWicBitmap(src, D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dx, dy),
			                              &bmp);
			src->Release();
		}
	}
	if (bmp == nullptr) return;
	const D2D1_RECT_F r = D2D1::RectF(x, y, x + size, y + size);
	ID2D1DeviceContext* dc = nullptr;
	if (glowAmount > 0 && SUCCEEDED(rt->QueryInterface(__uuidof(ID2D1DeviceContext), (void**)&dc))) {
		ID2D1Effect* shadow = nullptr;
		if (SUCCEEDED(dc->CreateEffect(CLSID_D2D1Shadow, &shadow))) {
			shadow->SetInput(0, bmp);
			shadow->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, size * 0.12f);
			shadow->SetValue(D2D1_SHADOW_PROP_COLOR, D2D1::Vector4F(kNeon.r, kNeon.g, kNeon.b, glowAmount));
			D2D1_MATRIX_3X2_F was;
			dc->GetTransform(&was);
			const D2D1_SIZE_F bs = bmp->GetSize();
			dc->SetTransform(D2D1::Matrix3x2F::Scale(size / bs.width, size / bs.height) * D2D1::Matrix3x2F::Translation(x, y) * was);
			dc->DrawImage(shadow);
			dc->SetTransform(was);
			shadow->Release();
		}
		dc->Release();
	}
	rt->DrawBitmap(bmp, r, 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

void hero(ID2D1RenderTarget* rt, const D2D1_RECT_F& box, double t) {
	const D2D1_COLOR_F accent = kNeon, ink = D2D1::ColorF(1, 1, 1);
	const D2D1_COLOR_F lampOn = D2D1::ColorF(1, 196 / 255.0f, 64 / 255.0f);
	const float w = box.right - box.left, h = box.bottom - box.top;
	const float cx = box.left + w / 2, cy = box.top + h / 2;
	const float k = std::min(w / 520, h / 200);
	const int state = (int)(t / 0.95) % 4;
	const bool a = (state & 2) != 0, b = (state & 1) != 0, out = a && b;
	const float bodyL = cx - 30 * k, bodyR = cx + 30 * k, hh = 44 * k, nose = bodyR + 44 * k;
	ID2D1SolidColorBrush* br = nullptr;
	if (FAILED(rt->CreateSolidColorBrush(ink, &br))) return;
	ID2D1StrokeStyle* round = nullptr;
	d2dFactory()->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
	                                                            D2D1_LINE_JOIN_MITER), nullptr, 0, &round);
	struct Wire { std::vector<D2D1_POINT_2F> pts; bool on; };
	const Wire wires[] = {
		{ { { cx - 188 * k, cy - 50 * k }, { cx - 100 * k, cy - 50 * k }, { cx - 100 * k, cy - 24 * k }, { bodyL, cy - 24 * k } }, a },
		{ { { cx - 188 * k, cy + 50 * k }, { cx - 100 * k, cy + 50 * k }, { cx - 100 * k, cy + 24 * k }, { bodyL, cy + 24 * k } }, b },
		{ { { nose, cy }, { cx + 150 * k, cy } }, out },
	};
	for (const Wire& wr : wires) {
		ID2D1PathGeometry* g = nullptr;
		ID2D1GeometrySink* sink = nullptr;
		if (FAILED(d2dFactory()->CreatePathGeometry(&g)) || FAILED(g->Open(&sink))) { release(g); continue; }
		sink->BeginFigure(wr.pts[0], D2D1_FIGURE_BEGIN_HOLLOW);
		for (size_t i = 1; i < wr.pts.size(); i++) sink->AddLine(wr.pts[i]);
		sink->EndFigure(D2D1_FIGURE_END_OPEN);
		sink->Close();
		sink->Release();
		if (wr.on) {
			br->SetColor(alpha(accent, 0.18f));
			rt->DrawGeometry(g, br, 9 * k, round);
		}
		br->SetColor(wr.on ? accent : alpha(ink, 0.32f));
		rt->DrawGeometry(g, br, 3 * k, round);
		g->Release();
		if (!wr.on) continue;
		// Signals running along it.
		double len = 0;
		for (size_t i = 1; i < wr.pts.size(); i++) len += std::hypot(wr.pts[i].x - wr.pts[i - 1].x, wr.pts[i].y - wr.pts[i - 1].y);
		const double spacing = 34 * k;
		br->SetColor(D2D1::ColorF(1, 1, 1));
		for (double d = std::fmod(t * 95 * k, spacing); d < len; d += spacing) {
			double left = d;
			D2D1_POINT_2F q = wr.pts.back();
			for (size_t i = 1; i < wr.pts.size(); i++) {
				const double seg = std::hypot(wr.pts[i].x - wr.pts[i - 1].x, wr.pts[i].y - wr.pts[i - 1].y);
				if (left <= seg) {
					const float f = seg > 0 ? (float)(left / seg) : 0;
					q = D2D1::Point2F(wr.pts[i - 1].x + (wr.pts[i].x - wr.pts[i - 1].x) * f, wr.pts[i - 1].y + (wr.pts[i].y - wr.pts[i - 1].y) * f);
					break;
				}
				left -= seg;
			}
			rt->FillEllipse(D2D1::Ellipse(q, 2.6f * k, 2.6f * k), br);
		}
	}
	// The switches.
	const std::pair<float, bool> switches[] = { { cy - 50 * k, a }, { cy + 50 * k, b } };
	for (const auto& sw : switches) {
		const D2D1_RECT_F r = D2D1::RectF(cx - 232 * k, sw.first - 18 * k, cx - 188 * k, sw.first + 18 * k);
		fillRound(rt, r, 8 * k, sw.second ? alpha(accent, 0.22f) : alpha(ink, 0.05f));
		strokeRound(rt, r, 8 * k, sw.second ? accent : alpha(ink, 0.35f), 2);
		const float ts = std::max(8.0f, 15 * k);
		text(rt, sw.second ? "1" : "0", r.left, (r.top + r.bottom) / 2 - ts * 0.67f, ts, DWRITE_FONT_WEIGHT_BOLD,
		     sw.second ? accent : alpha(ink, 0.55f), r.right - r.left, DWRITE_TEXT_ALIGNMENT_CENTER);
	}
	// The gate.
	ID2D1PathGeometry* body = nullptr;
	ID2D1GeometrySink* sink = nullptr;
	if (SUCCEEDED(d2dFactory()->CreatePathGeometry(&body)) && SUCCEEDED(body->Open(&sink))) {
		sink->BeginFigure(D2D1::Point2F(bodyL, cy - hh), D2D1_FIGURE_BEGIN_FILLED);
		sink->AddLine(D2D1::Point2F(bodyR, cy - hh));
		sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(bodyR + 60 * k, cy - hh), D2D1::Point2F(bodyR + 60 * k, cy + hh), D2D1::Point2F(bodyR, cy + hh)));
		sink->AddLine(D2D1::Point2F(bodyL, cy + hh));
		sink->EndFigure(D2D1_FIGURE_END_CLOSED);
		sink->Close();
		sink->Release();
		br->SetColor(alpha(accent, 0.12f));
		rt->FillGeometry(body, br);
		br->SetColor(accent);
		rt->DrawGeometry(body, br, 3.2f * k);
		body->Release();
	}
	const float as = std::max(8.0f, 13 * k);
	text(rt, "AND", cx + 8 * k - 40, cy - as * 0.67f, as, DWRITE_FONT_WEIGHT_BOLD, alpha(accent, 0.9f), 80, DWRITE_TEXT_ALIGNMENT_CENTER);
	// The lamp.
	const float lx = cx + 172 * k, lr = 22 * k;
	if (out)
		for (int i = 3; i >= 1; i--) {
			const float gr = lr + (4 - i) * 9 * k;
			fillCircle(rt, D2D1::Point2F(lx, cy), gr, alpha(lampOn, 0.10f * i));
		}
	fillCircle(rt, D2D1::Point2F(lx, cy), lr, out ? lampOn : alpha(ink, 0.06f));
	br->SetColor(out ? D2D1::ColorF(230 / 255.0f, 160 / 255.0f, 20 / 255.0f) : alpha(ink, 0.35f));
	rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(lx, cy), lr, lr), br, 2.5f * k);
	release(round);
	br->Release();
}

}  // namespace brand
