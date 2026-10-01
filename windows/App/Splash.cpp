// The launch screen, as the Mac app's (Splash.swift), moment for moment: a
// glass panel in the icon's own colours in the middle of the screen. The
// icon rises and sharpens out of a blur, its circuit traces draw in,
// "CedarLogic" writes itself in a letter at a time, a line says what's being
// done and a thin neon bar along the bottom fills as it's done; a band of
// light passes over the glass. Then it dissolves into the window. About two
// and a half seconds; the very first time, slower, with its own sound (the
// Mac's FirstLaunch, built in as a WAV), fading into the welcome.
//
// It's a layered window drawn with Direct2D 1.1 (effects for the blurs and
// glows) into a bitmap with real transparency, so the panel's corners are
// round and its shadow soft. Without Direct2D 1.1 it draws the same, minus
// the blurs.

#include "Welcome.h"
#include "Chrome.h"
#include "Images.h"

#include <d2d1_1.h>
#include <d2d1effects.h>
#include <mmsystem.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace splash {

namespace {

// ---- The look ------------------------------------------------------------------

const float kW = 560, kH = 372;     // the panel, in points
const float kMargin = 44;           // room around it for the shadow
const float kRadius = 30;

const D2D1_COLOR_F kNeon = D2D1::ColorF(0.22f, 1.0f, 0.42f), kNeonDeep = D2D1::ColorF(0.05f, 0.78f, 0.26f);
const D2D1_COLOR_F kDim = D2D1::ColorF(0.62f, 0.74f, 0.66f);
// The glass: the icon's near-black green over a dark tint (the Mac's glass
// with its 0.72 tint and the ground's 0.78..0.9 over it).
const D2D1_COLOR_F kGroundTop = D2D1::ColorF(0.042f, 0.078f, 0.056f), kGroundBottom = D2D1::ColorF(0.014f, 0.029f, 0.021f);

D2D1_COLOR_F alpha(D2D1_COLOR_F c, float a) { c.a = a; return c; }
double ease(double x) { const double c = std::min(1.0, std::max(0.0, x)); return 1 - std::pow(1 - c, 3); }
double span(double t, double a, double b) { return ease((t - a) / (b - a)); }
double smooth(double x) { const double c = std::min(1.0, std::max(0.0, x)); return c * c * (3 - 2 * c); }

// ---- The timeline --------------------------------------------------------------

struct Timeline {
	bool first = false;     // the very first launch: slower, with the sound
	bool calm = false;      // Windows' animations are off
	double dissolveAt = 2.25, fadeTime = 0.42, readyAt = -1;
	struct Step { double at; std::string text; double progress; };
	std::vector<Step> steps;

	// What was done, a line at a time (all done by now; each line stays up
	// long enough to read).
	void plan(int gates, int families, const std::string& opening) {
		fadeTime = first ? 1.1 : 0.42;
		if (calm) dissolveAt = 1.1;
		const double spacing = calm ? 0.12 : first ? 0.72 : 0.32;
		double at = 0;
		auto step = [&](const std::string& text, double progress) {
			at += spacing;
			steps.push_back({ at, text, progress });
		};
		step("Loading the gate library", 0.12);
		step(strf("%d gates in %d families", gates, families), 0.34);
		step("Drawing the palette", 0.5);
		step("Starting the simulator", 0.66);
		step(opening.empty() ? std::string("Opening a new circuit") : "Opening “" + opening + "”", 0.86);
		step("Ready", 1);
		readyAt = at + 0.3;
		// The first time, the panel holds until the sound's big moment (4 s)
		// has rung out a little, then fades slowly into the welcome.
		dissolveAt = first ? std::max(4.9, std::min(7.0, readyAt + 0.3))
		                   : std::min(6.0, std::max(calm ? 1.1 : 2.3, readyAt + 0.3));
	}

	// The line showing at t, the one before it, and how far the change has gone.
	void status(double t, std::string& now, std::string& before, double& mix) const {
		now = "Starting up";
		before.clear();
		double changedAt = -1;
		for (const Step& s : steps) {
			if (s.at > t) break;
			before = now;
			now = s.text;
			changedAt = s.at;
		}
		mix = changedAt < 0 ? 1 : std::min(1.0, (t - changedAt) / 0.2);
	}

	// The bar glides, every frame: along one smooth curve towards 92% while
	// the work goes on, then from wherever it is to the end once it's done.
	double progress(double t) const {
		const double pace = calm ? 0.9 : first ? 3.9 : 2.1;
		auto early = [&](double x) { return 0.92 * smooth(x / pace); };
		if (readyAt < 0 || t <= readyAt) return early(t);
		const double from = early(readyAt);
		const double end = std::max(readyAt + 0.25, dissolveAt - 0.05);
		return from + (1 - from) * smooth((t - readyAt) / (end - readyAt));
	}
};

// ---- Drawing -------------------------------------------------------------------

IDWriteTextFormat* textFormat(const wchar_t* family, float size, DWRITE_FONT_WEIGHT weight) {
	static std::map<std::wstring, IDWriteTextFormat*> made;
	const std::wstring key = std::wstring(family) + L"|" + std::to_wstring((int)(size * 10)) + L"|" + std::to_wstring((int)weight);
	auto it = made.find(key);
	if (it != made.end()) return it->second;
	IDWriteTextFormat* f = nullptr;
	if (dwFactory()) dwFactory()->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"", &f);
	made[key] = f;
	return f;
}

// One run of text: its width, and (with rt) drawn with its top-left at x, y.
float text(ID2D1RenderTarget* rt, const std::wstring& s, float x, float y, IDWriteTextFormat* f, ID2D1Brush* brush) {
	IDWriteTextLayout* layout = nullptr;
	if (f == nullptr || FAILED(dwFactory()->CreateTextLayout(s.c_str(), (UINT32)s.size(), f, 2000, 200, &layout))) return 0;
	DWRITE_TEXT_METRICS m = {};
	layout->GetMetrics(&m);
	if (rt && brush) rt->DrawTextLayout(D2D1::Point2F(x, y), layout, brush, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
	layout->Release();
	return m.widthIncludingTrailingWhitespace;
}

// Text with extra room between the letters, centred on cx.
void spaced(ID2D1RenderTarget* rt, const std::string& s, float cx, float y, IDWriteTextFormat* f, float spacing, ID2D1Brush* brush) {
	const std::wstring w = W(s);
	std::vector<float> widths;
	float total = 0;
	for (wchar_t c : w) { widths.push_back(text(nullptr, std::wstring(1, c), 0, 0, f, nullptr)); total += widths.back() + spacing; }
	float x = cx - (total - spacing) / 2;
	for (size_t i = 0; i < w.size(); i++) { text(rt, std::wstring(1, w[i]), x, y, f, brush); x += widths[i] + spacing; }
}

ID2D1PathGeometry* polyline(const std::vector<D2D1_POINT_2F>& pts, double progress) {
	ID2D1PathGeometry* g = nullptr;
	if (FAILED(d2dFactory()->CreatePathGeometry(&g))) return nullptr;
	ID2D1GeometrySink* sink = nullptr;
	g->Open(&sink);
	double total = 0;
	for (size_t i = 1; i < pts.size(); i++) total += std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
	double left = total * std::min(1.0, std::max(0.0, progress));
	sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_HOLLOW);
	for (size_t i = 1; i < pts.size() && left > 0; i++) {
		const double len = std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
		if (len <= left) { sink->AddLine(pts[i]); left -= len; continue; }
		const float k = (float)(left / len);
		sink->AddLine(D2D1::Point2F(pts[i - 1].x + (pts[i].x - pts[i - 1].x) * k, pts[i - 1].y + (pts[i].y - pts[i - 1].y) * k));
		left = 0;
	}
	sink->EndFigure(D2D1_FIGURE_END_OPEN);
	sink->Close();
	sink->Release();
	return g;
}

// What's made once for a render target: the icon at its size, a bitmap of
// each letter of the title, the panel's own surface.
struct Resources {
	ID2D1RenderTarget* owner = nullptr;
	ID2D1Bitmap* icon = nullptr;
	ID2D1BitmapRenderTarget* panel = nullptr;
	std::vector<ID2D1Bitmap*> letters;
	std::vector<float> letterW;
	float titleW = 0;
	ID2D1StrokeStyle* round = nullptr;

	void release() {
		if (icon) icon->Release();
		if (panel) panel->Release();
		for (ID2D1Bitmap* b : letters) if (b) b->Release();
		if (round) round->Release();
		*this = Resources();
	}
};

const char* kTitle = "CedarLogic";
const float kLetterPadX = 14, kLetterPadY = 12, kTitleSize = 36, kLetterGap = 0.5f;

void prepare(Resources& r, ID2D1RenderTarget* rt) {
	if (r.owner == rt) return;
	r.release();
	r.owner = rt;
	d2dFactory()->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
	                                                            D2D1_LINE_JOIN_ROUND), nullptr, 0, &r.round);
	// The icon, drawn to 128 points (sharp at the screen's scale).
	float dx = 96, dy = 96;
	rt->GetDpi(&dx, &dy);
	const UINT px = (UINT)std::ceil(128 * dx / 96);
	if (IWICBitmap* src = images::loadResource(2, px, px)) {
		ID2D1Bitmap* full = nullptr;
		if (SUCCEEDED(rt->CreateBitmapFromWicBitmap(src, D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
		                                                                                          D2D1_ALPHA_MODE_PREMULTIPLIED), dx, dy), &full)))
			r.icon = full;
		src->Release();
	}
	rt->CreateCompatibleRenderTarget(D2D1::SizeF(kW, kH), &r.panel);
	// Each letter of the title on its own, in brushed silver.
	IDWriteTextFormat* f = textFormat(L"Segoe UI Variable Display", kTitleSize, DWRITE_FONT_WEIGHT_SEMI_BOLD);
	for (const char* c = kTitle; *c; c++) {
		const std::wstring ch(1, (wchar_t)*c);
		const float w = text(nullptr, ch, 0, 0, f, nullptr);
		r.letterW.push_back(w);
		r.titleW += w + kLetterGap;
		ID2D1BitmapRenderTarget* lrt = nullptr;
		ID2D1Bitmap* bmp = nullptr;
		if (SUCCEEDED(rt->CreateCompatibleRenderTarget(D2D1::SizeF(w + 2 * kLetterPadX, kTitleSize * 1.4f + 2 * kLetterPadY), &lrt))) {
			lrt->BeginDraw();
			lrt->Clear(D2D1::ColorF(0, 0, 0, 0));
			D2D1_GRADIENT_STOP s[3] = { { 0, D2D1::ColorF(0.97f, 0.97f, 0.97f) }, { 0.5f, D2D1::ColorF(0.74f, 0.74f, 0.74f) },
			                            { 1, D2D1::ColorF(0.9f, 0.9f, 0.9f) } };
			ID2D1GradientStopCollection* stops = nullptr;
			ID2D1LinearGradientBrush* silver = nullptr;
			if (SUCCEEDED(lrt->CreateGradientStopCollection(s, 3, &stops)) &&
			    SUCCEEDED(lrt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, kLetterPadY + 6),
			                                                                                 D2D1::Point2F(0, kLetterPadY + kTitleSize * 1.15f)),
			                                             stops, &silver)))
				text(lrt, ch, kLetterPadX, kLetterPadY, f, silver);
			if (silver) silver->Release();
			if (stops) stops->Release();
			lrt->EndDraw();
			lrt->GetBitmap(&bmp);
			lrt->Release();
		}
		r.letters.push_back(bmp);
	}
	r.titleW -= kLetterGap;
}

// An image drawn through effects where there are effects: blurred by `blur`
// points, with a coloured glow (or shadow) under it, at `opacity`.
struct Glow { D2D1_COLOR_F color; float radius; float dy; };
void drawSoft(ID2D1RenderTarget* rt, ID2D1Bitmap* bmp, D2D1_POINT_2F at, float blur, float opacity, const std::vector<Glow>& glows) {
	if (bmp == nullptr || opacity <= 0.002f) return;
	ID2D1DeviceContext* dc = nullptr;
	rt->QueryInterface(__uuidof(ID2D1DeviceContext), (void**)&dc);
	const D2D1_SIZE_F size = bmp->GetSize();
	if (dc == nullptr) {
		rt->DrawBitmap(bmp, D2D1::RectF(at.x, at.y, at.x + size.width, at.y + size.height), opacity);
		return;
	}
	dc->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), opacity),
	              nullptr);
	for (const Glow& g : glows) {
		if (g.color.a <= 0.002f) continue;
		ID2D1Effect* shadow = nullptr;
		if (SUCCEEDED(dc->CreateEffect(CLSID_D2D1Shadow, &shadow))) {
			shadow->SetInput(0, bmp);
			shadow->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, std::max(0.1f, g.radius / 2));
			shadow->SetValue(D2D1_SHADOW_PROP_COLOR, D2D1::Vector4F(g.color.r, g.color.g, g.color.b, g.color.a));
			dc->DrawImage(shadow, D2D1::Point2F(at.x, at.y + g.dy));
			shadow->Release();
		}
	}
	if (blur > 0.05f) {
		ID2D1Effect* fx = nullptr;
		if (SUCCEEDED(dc->CreateEffect(CLSID_D2D1GaussianBlur, &fx))) {
			fx->SetInput(0, bmp);
			fx->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, blur / 2);
			fx->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);
			dc->DrawImage(fx, at);
			fx->Release();
		}
	} else {
		dc->DrawBitmap(bmp, D2D1::RectF(at.x, at.y, at.x + size.width, at.y + size.height));
	}
	dc->PopLayer();
	dc->Release();
}

// The panel itself, 560 x 372 points at the origin, at time t.
void drawPanel(ID2D1RenderTarget* rt, Resources& res, const Timeline& tl, double t) {
	const bool calm = tl.calm;
	const double rise = calm ? span(t, 0, 0.3) : span(t, 0.08, 0.85);
	const double glow = span(t, 0.3, 1.1) * (0.85 + 0.15 * std::sin(t * 2.6));
	rt->Clear(D2D1::ColorF(0, 0, 0, 0));
	ID2D1RoundedRectangleGeometry* shape = nullptr;
	d2dFactory()->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(0, 0, kW, kH), kRadius, kRadius), &shape);
	rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), shape), nullptr);

	// The icon's ground: near-black green.
	{
		D2D1_GRADIENT_STOP s[2] = { { 0, kGroundTop }, { 1, kGroundBottom } };
		ID2D1GradientStopCollection* stops = nullptr;
		ID2D1LinearGradientBrush* b = nullptr;
		if (SUCCEEDED(rt->CreateGradientStopCollection(s, 2, &stops)) &&
		    SUCCEEDED(rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, kH)), stops, &b)))
			rt->FillRectangle(D2D1::RectF(0, 0, kW, kH), b);
		if (b) b->Release();
		if (stops) stops->Release();
	}
	// Its grid, showing faintly, strongest in the middle.
	{
		D2D1_GRADIENT_STOP s[3] = { { 0, D2D1::ColorF(1, 1, 1, 1) }, { 40.0f / 320, D2D1::ColorF(1, 1, 1, 1) }, { 1, D2D1::ColorF(1, 1, 1, 0) } };
		ID2D1GradientStopCollection* stops = nullptr;
		ID2D1RadialGradientBrush* mask = nullptr;
		ID2D1SolidColorBrush* line = nullptr;
		if (SUCCEEDED(rt->CreateGradientStopCollection(s, 3, &stops)) &&
		    SUCCEEDED(rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(kW / 2, kH / 2), D2D1::Point2F(0, 0), 320, 320),
		                                            stops, &mask)) &&
		    SUCCEEDED(rt->CreateSolidColorBrush(alpha(kNeon, (float)(0.07 * span(t, 0.1, 0.9))), &line))) {
			rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), 1, mask),
			              nullptr);
			const float step = 28;
			for (float x = std::fmod(kW, step) / 2; x < kW; x += step) rt->DrawLine(D2D1::Point2F(x, 0), D2D1::Point2F(x, kH), line, 0.5f);
			for (float y = std::fmod(kH, step) / 2; y < kH; y += step) rt->DrawLine(D2D1::Point2F(0, y), D2D1::Point2F(kW, y), line, 0.5f);
			rt->PopLayer();
		}
		if (line) line->Release();
		if (mask) mask->Release();
		if (stops) stops->Release();
	}
	// A green bloom behind the icon.
	{
		D2D1_GRADIENT_STOP s[3] = { { 0, alpha(kNeon, (float)(0.30 * glow)) }, { 4.0f / 210, alpha(kNeon, (float)(0.30 * glow)) },
		                            { 1, alpha(kNeon, 0) } };
		ID2D1GradientStopCollection* stops = nullptr;
		ID2D1RadialGradientBrush* b = nullptr;
		if (SUCCEEDED(rt->CreateGradientStopCollection(s, 3, &stops)) &&
		    SUCCEEDED(rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(kW * 0.5f, kH * 0.36f), D2D1::Point2F(0, 0),
		                                                                                210, 210),
		                                            stops, &b)))
			rt->FillRectangle(D2D1::RectF(0, 0, kW, kH), b);
		if (b) b->Release();
		if (stops) stops->Release();
	}
	// The traces from the icon, drawn in: two coming in from the left, one
	// leaving to the right, each ending in a dot.
	{
		const double progress = calm ? 1 : span(t, 0.35, 1.15);
		const float cy = 44 + 64, left = kW / 2 - 64 - 6, right = kW / 2 + 64 + 6;
		const std::vector<std::vector<D2D1_POINT_2F>> lines = {
			{ { 26, cy - 12 }, { left - 40, cy - 12 }, { left - 22, cy - 4 }, { left, cy - 4 } },
			{ { 26, cy + 12 }, { left - 40, cy + 12 }, { left - 22, cy + 4 }, { left, cy + 4 } },
			{ { right, cy + 14 }, { kW - 26, cy + 14 } },
		};
		ID2D1SolidColorBrush* b = nullptr;
		rt->CreateSolidColorBrush(kNeon, &b);
		for (size_t i = 0; i < lines.size() && b; i++) {
			if (ID2D1PathGeometry* g = polyline(lines[i], progress)) {
				b->SetColor(alpha(kNeon, (float)(0.18 * glow)));
				rt->DrawGeometry(g, b, 7, res.round);
				b->SetColor(alpha(kNeon, 0.85f));
				rt->DrawGeometry(g, b, 1.6f, res.round);
				g->Release();
			}
			const D2D1_POINT_2F end = i < 2 ? lines[i].front() : lines[i].back();
			if (progress > 0.96) {
				b->SetColor(alpha(kNeon, (float)(0.18 * glow)));
				rt->FillEllipse(D2D1::Ellipse(end, 8, 8), b);
				b->SetColor(kNeon);
				rt->FillEllipse(D2D1::Ellipse(end, 4, 4), b);
			}
		}
		if (b) b->Release();
	}
	// The icon: rises, grows and sharpens, a green glow under it.
	if (res.icon) {
		const float s = (float)(0.9 + 0.1 * rise), dy = calm ? 0 : (float)(26 * (1 - rise));
		D2D1_MATRIX_3X2_F was;
		rt->GetTransform(&was);
		rt->SetTransform(D2D1::Matrix3x2F::Scale(s, s, D2D1::Point2F(64, 64)) * D2D1::Matrix3x2F::Translation(kW / 2 - 64, 44 + dy) * was);
		drawSoft(rt, res.icon, D2D1::Point2F(0, 0), calm ? 0 : (float)(16 * (1 - rise)), (float)rise,
		         { { alpha(kNeon, (float)(0.45 * glow)), 22, 0 }, { D2D1::ColorF(0, 0, 0, 0.5f), 12, 8 } });
		rt->SetTransform(was);
	}
	// "CedarLogic", a letter at a time, in brushed silver with a green glow.
	{
		float x = kW / 2 - res.titleW / 2;
		const float top = 44 + 128 + 20;
		for (size_t i = 0; i < res.letters.size(); i++) {
			const double p = calm ? span(t, 0.2, 0.5) : span(t, 0.55 + i * 0.045, 0.9 + i * 0.045);
			const float dy = calm ? 0 : (float)(9 * (1 - p));
			drawSoft(rt, res.letters[i], D2D1::Point2F(x - kLetterPadX, top - kLetterPadY + dy), calm ? 0 : (float)(5 * (1 - p)), (float)p,
			         { { alpha(kNeon, (float)(0.35 * p)), 10, 0 } });
			x += res.letterW[i] + kLetterGap;
		}
	}
	// What it is, and where.
	ID2D1SolidColorBrush* dim = nullptr;
	rt->CreateSolidColorBrush(alpha(kDim, (float)(0.75 * span(t, 1.05, 1.45))), &dim);
	if (dim) {
		spaced(rt, "LOGIC SIMULATOR  ·  FOR WINDOWS", kW / 2, 44 + 128 + 20 + 50 + 8,
		       textFormat(L"Segoe UI", 10, DWRITE_FONT_WEIGHT_SEMI_BOLD), 2.4f, dim);
		// The line saying what's being done, each fading into the next.
		std::string now, before;
		double mix = 1;
		tl.status(t, now, before, mix);
		const float statusTop = kH - 24 - 3 - 14 - 16;
		IDWriteTextFormat* sf = textFormat(L"Segoe UI", 11.5f, DWRITE_FONT_WEIGHT_MEDIUM);
		const float shown = (float)span(t, 0.5, 0.9);
		auto line = [&](const std::string& s, float a) {
			if (s.empty() || a <= 0.01f) return;
			dim->SetColor(alpha(kDim, a * shown));
			const std::wstring w = W(s);
			text(rt, w, kW / 2 - text(nullptr, w, 0, 0, sf, nullptr) / 2, statusTop, sf, dim);
		};
		line(before, (float)(1 - mix));
		line(now, (float)mix);
		dim->Release();
	}
	// The thin neon bar along the bottom, with a bright head.
	{
		const double fill = tl.progress(t);
		const float bx = 30, bw = kW - 60, by = kH - 24 - 3, bh = 3;
		fillRound(rt, D2D1::RectF(bx, by, bx + bw, by + bh), bh / 2, D2D1::ColorF(1, 1, 1, 0.08f));
		const float w = (float)(bw * fill);
		if (w > 0.5f) {
			// Its glow, then the bar (the deep green into the neon).
			fillRound(rt, D2D1::RectF(bx - 3, by - 3, bx + w + 3, by + bh + 3), 4.5f, alpha(kNeon, 0.16f));
			D2D1_GRADIENT_STOP s[2] = { { 0, kNeonDeep }, { 1, kNeon } };
			ID2D1GradientStopCollection* stops = nullptr;
			ID2D1LinearGradientBrush* b = nullptr;
			if (SUCCEEDED(rt->CreateGradientStopCollection(s, 2, &stops)) &&
			    SUCCEEDED(rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(bx, 0), D2D1::Point2F(bx + std::max(w, 1.0f), 0)),
			                                            stops, &b)))
				rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(bx, by, bx + w, by + bh), bh / 2, bh / 2), b);
			if (b) b->Release();
			if (stops) stops->Release();
			if (fill > 0.01 && fill < 0.999) {
				fillCircle(rt, D2D1::Point2F(bx + w, by + bh / 2), 6, alpha(kNeon, 0.25f));
				fillCircle(rt, D2D1::Point2F(bx + w, by + bh / 2), 2.5f, D2D1::ColorF(1, 1, 1));
			}
		}
	}
	// A soft band of light passing across the glass.
	if (!calm) {
		const double phase = span(t, 0.85, 1.75);
		if (phase > 0 && phase < 1) {
			const float bw = 150, bh = kH * 1.6f;
			const float x = (float)(-200 + (kW + 400) * phase), y = -kH * 0.3f;
			D2D1_GRADIENT_STOP s[3] = { { 0, D2D1::ColorF(1, 1, 1, 0) }, { 0.5f, D2D1::ColorF(1, 1, 1, 0.13f) }, { 1, D2D1::ColorF(1, 1, 1, 0) } };
			ID2D1GradientStopCollection* stops = nullptr;
			ID2D1LinearGradientBrush* b = nullptr;
			if (SUCCEEDED(rt->CreateGradientStopCollection(s, 3, &stops)) &&
			    SUCCEEDED(rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(x, 0), D2D1::Point2F(x + bw, 0)), stops, &b))) {
				D2D1_MATRIX_3X2_F was;
				rt->GetTransform(&was);
				rt->SetTransform(D2D1::Matrix3x2F::Rotation(18, D2D1::Point2F(x + bw / 2, y + bh / 2)) * was);
				rt->FillRectangle(D2D1::RectF(x, y, x + bw, y + bh), b);
				rt->SetTransform(was);
			}
			if (b) b->Release();
			if (stops) stops->Release();
		}
	}
	rt->PopLayer();
	if (shape) shape->Release();
	// The glass's edge: brighter at the top.
	{
		D2D1_GRADIENT_STOP s[3] = { { 0, D2D1::ColorF(1, 1, 1, 0.22f) }, { 0.5f, alpha(kNeon, 0.12f) }, { 1, D2D1::ColorF(1, 1, 1, 0.05f) } };
		ID2D1GradientStopCollection* stops = nullptr;
		ID2D1LinearGradientBrush* b = nullptr;
		if (SUCCEEDED(rt->CreateGradientStopCollection(s, 3, &stops)) &&
		    SUCCEEDED(rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, kH)), stops, &b)))
			rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, kW - 0.5f, kH - 0.5f), kRadius - 0.5f, kRadius - 0.5f), b, 1);
		if (b) b->Release();
		if (stops) stops->Release();
	}
}

// Everything, onto a transparent target (kW + 2 margins wide): the panel's
// shadow, then the panel, coming in over `appear` and dissolving at the end.
void drawAll(ID2D1RenderTarget* rt, Resources& res, const Timeline& tl, double t, double appear) {
	prepare(res, rt);
	rt->Clear(D2D1::ColorF(0, 0, 0, 0));
	if (res.panel == nullptr) return;
	res.panel->BeginDraw();
	drawPanel(res.panel, res, tl, t);
	res.panel->EndDraw();
	ID2D1Bitmap* panel = nullptr;
	res.panel->GetBitmap(&panel);
	if (panel == nullptr) return;
	const double out = span(t, tl.dissolveAt, tl.dissolveAt + tl.fadeTime);
	const float opacity = (float)(appear * (1 - out));
	const float s = tl.calm ? 1 : (float)(1 + 0.035 * out);
	D2D1_MATRIX_3X2_F was;
	rt->GetTransform(&was);
	rt->SetTransform(D2D1::Matrix3x2F::Scale(s, s, D2D1::Point2F(kW / 2, kH / 2)) * D2D1::Matrix3x2F::Translation(kMargin, kMargin) * was);
	drawSoft(rt, panel, D2D1::Point2F(0, 0), tl.calm ? 0 : (float)(7 * out), opacity, { { D2D1::ColorF(0, 0, 0, 0.55f), 30, 14 } });
	rt->SetTransform(was);
	panel->Release();
}

// ---- The window ----------------------------------------------------------------

struct Splash {
	HWND hwnd = nullptr;
	HDC memDC = nullptr;
	HBITMAP dib = nullptr;
	HGDIOBJ oldBitmap = nullptr;
	int pw = 0, ph = 0;
	float scale = 1;
	ID2D1DCRenderTarget* rt = nullptr;
	Resources res;
	Timeline tl;
	double start = -1;            // when it began playing (-1: not yet)
	bool presented = false;       // the windows it held are in
	std::function<void()> then;
	std::string opening;

	double elapsed() const { return start < 0 ? 0 : nowSeconds() - start; }

	bool makeSurface() {
		HDC screen = GetDC(nullptr);
		memDC = CreateCompatibleDC(screen);
		ReleaseDC(nullptr, screen);
		BITMAPINFO bi = {};
		bi.bmiHeader.biSize = sizeof bi.bmiHeader;
		bi.bmiHeader.biWidth = pw;
		bi.bmiHeader.biHeight = -ph;
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;
		void* bits = nullptr;
		dib = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
		if (dib == nullptr) return false;
		oldBitmap = SelectObject(memDC, dib);
		const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
			D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96 * scale, 96 * scale);
		return SUCCEEDED(d2dFactory()->CreateDCRenderTarget(&props, &rt));
	}

	void frame() {
		if (rt == nullptr) return;
		RECT r = { 0, 0, pw, ph };
		if (FAILED(rt->BindDC(memDC, &r))) return;
		const double t = elapsed();
		const double appear = start < 0 ? 0 : std::min(1.0, t / 0.2);
		rt->BeginDraw();
		drawAll(rt, res, tl, t, appear);
		if (rt->EndDraw() == D2DERR_RECREATE_TARGET) {
			res.release();
			rt->Release();
			rt = nullptr;
			const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
				D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96 * scale, 96 * scale);
			d2dFactory()->CreateDCRenderTarget(&props, &rt);
			return;
		}
		POINT src = { 0, 0 };
		SIZE size = { pw, ph };
		BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
		UpdateLayeredWindow(hwnd, nullptr, nullptr, &size, memDC, &src, 0, &blend, ULW_ALPHA);
	}

	void destroy() {
		if (hwnd) { KillTimer(hwnd, 1); DestroyWindow(hwnd); hwnd = nullptr; }
		res.release();
		if (rt) { rt->Release(); rt = nullptr; }
		if (memDC) { SelectObject(memDC, oldBitmap); DeleteDC(memDC); memDC = nullptr; }
		if (dib) { DeleteObject(dib); dib = nullptr; }
	}
};

Splash* g_splash = nullptr;

bool reduceMotion() {
	BOOL animations = TRUE;
	SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
	return !animations;
}

void libraryCounts(int& gates, int& families) {
	gates = 0;
	families = cl_library_category_count();
	for (int c = 0; c < families; c++) gates += cl_library_gate_count(c);
}

LRESULT CALLBACK splashProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
	if (msg == WM_TIMER && g_splash && h == g_splash->hwnd) {
		Splash* s = g_splash;
		guarded("the launch screen", [&] {
			s->frame();
			const double t = s->elapsed();
			if (s->start < 0) return;
			// The windows come in as it starts to dissolve...
			if (!s->presented && t >= s->tl.dissolveAt) {
				s->presented = true;
				std::function<void()> f = std::move(s->then);
				s->then = nullptr;
				if (f) f();
				// ...under it: it stays on top until it's gone.
				if (g_splash) SetWindowPos(g_splash->hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			}
			if (g_splash && t >= s->tl.dissolveAt + s->tl.fadeTime + 0.05) {
				g_splash = nullptr;
				s->destroy();
				delete s;
			}
		});
		return 0;
	}
	if (msg == WM_NCHITTEST) return HTTRANSPARENT;   // it can't be clicked or skipped
	return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

void show() {
	if (g_splash) return;
	Splash* s = new Splash();
	s->tl.first = !prefs().hasSeenWelcome && !prefs().firstLaunchPlayed;
	s->tl.calm = reduceMotion();
	// Where you're looking: the screen with the pointer on it.
	POINT cursor;
	GetCursorPos(&cursor);
	HMONITOR mon = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
	MONITORINFO mi = { sizeof mi };
	GetMonitorInfoW(mon, &mi);
	UINT dpiX = 96, dpiY = 96;
	typedef HRESULT(WINAPI * GetDpiForMonitorFn)(HMONITOR, int, UINT*, UINT*);
	if (auto getDpi = (GetDpiForMonitorFn)(void*)GetProcAddress(LoadLibraryW(L"shcore.dll"), "GetDpiForMonitor")) getDpi(mon, 0, &dpiX, &dpiY);
	s->scale = dpiX / 96.0f;
	s->pw = (int)std::ceil((kW + 2 * kMargin) * s->scale);
	s->ph = (int)std::ceil((kH + 2 * kMargin) * s->scale);
	const RECT& work = mi.rcWork;
	const int x = (work.left + work.right - s->pw) / 2;
	const int y = (work.top + work.bottom - s->ph) / 2 - (int)((work.bottom - work.top) * 0.04);
	static bool registered = false;
	if (!registered) {
		registered = true;
		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof wc;
		wc.lpfnWndProc = splashProc;
		wc.hInstance = appInstance();
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.lpszClassName = L"CedarLogicSplash";
		RegisterClassExW(&wc);
	}
	s->hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT, L"CedarLogicSplash",
	                          L"CedarLogic", WS_POPUP, x, y, s->pw, s->ph, nullptr, nullptr, appInstance(), nullptr);
	if (s->hwnd == nullptr || !s->makeSurface()) { s->destroy(); delete s; return; }
	g_splash = s;
	// Up, but unseen (drawn clear) until the launch's work is done and it begins.
	s->frame();
	ShowWindow(s->hwnd, SW_SHOWNOACTIVATE);
	SetTimer(s->hwnd, 1, 10, nullptr);
}

bool active() { return g_splash != nullptr; }

// The work happens before it plays (it lists what was done, a line at a
// time, once it begins), so what's said meanwhile isn't shown.
void setStatus(const char*) {}

void setOpening(const std::string& name) {
	if (g_splash) g_splash->opening = name;
}

void hideSoon(std::function<void()> then) {
	Splash* s = g_splash;
	if (s == nullptr) { if (then) then(); return; }
	if (!then) {   // something went wrong: out of the way at once
		g_splash = nullptr;
		s->destroy();
		delete s;
		return;
	}
	// The launch's heavy work is done: play it now, on an idle thread.
	s->then = std::move(then);
	int gates = 0, families = 0;
	libraryCounts(gates, families);
	s->tl.plan(gates, families, s->opening);
	s->start = nowSeconds();
	if (s->tl.first) {
		// The first launch's sound: once ever.
		prefs().firstLaunchPlayed = true;
		prefs().save();
		PlaySoundW(MAKEINTRESOURCEW(3), appInstance(), SND_RESOURCE | SND_ASYNC | SND_NODEFAULT);
	}
}

bool renderFrame(double t, bool first, const std::string& file) {
	Timeline tl;
	tl.first = first;
	int gates = 0, families = 0;
	libraryCounts(gates, families);
	tl.plan(gates, families, "practice");
	Resources res;
	IWICBitmap* bmp = images::render(kW + 2 * kMargin, kH + 2 * kMargin, 2, false, [&](ID2D1RenderTarget* rt) {
		drawAll(rt, res, tl, t, 1);
	});
	res.release();
	if (bmp == nullptr) return false;
	const bool ok = images::savePng(bmp, file);
	bmp->Release();
	return ok;
}

}  // namespace splash
