// The look around the canvas (see Chrome.h).

#include "Chrome.h"

#include <algorithm>
#include <cmath>
#include <map>

D2D1_COLOR_F Chrome::canvas() const { return dark ? rgb255(19, 21, 25) : rgb255(255, 255, 255); }
D2D1_COLOR_F Chrome::bar() const { return dark ? rgb255(44, 47, 54) : rgb255(236, 237, 240); }
D2D1_COLOR_F Chrome::barInk() const { return dark ? rgb255(210, 215, 224) : rgb255(52, 56, 64); }
D2D1_COLOR_F Chrome::tabBar() const { return dark ? rgb255(22, 24, 28) : rgb255(233, 234, 238); }
D2D1_COLOR_F Chrome::tabCard() const { return dark ? rgb255(32, 35, 41) : rgb255(255, 255, 255); }
D2D1_COLOR_F Chrome::tabInk() const { return dark ? rgb255(228, 232, 240) : rgb255(32, 35, 42); }
D2D1_COLOR_F Chrome::panel() const { return dark ? D2D1::ColorF(0.105f, 0.115f, 0.135f) : D2D1::ColorF(0.965f, 0.968f, 0.975f); }
D2D1_COLOR_F Chrome::hairline() const { return dark ? D2D1::ColorF(1, 1, 1, 0.08f) : D2D1::ColorF(0, 0, 0, 0.09f); }
D2D1_COLOR_F Chrome::sash() const { return dark ? rgb255(40, 43, 50) : rgb255(218, 220, 224); }
D2D1_COLOR_F Chrome::accent() const { return d2dColor(accentColor(dark)); }
D2D1_COLOR_F Chrome::ink(float alpha) const { return withAlpha(barInk(), alpha); }

COLORREF Chrome::gdi(const D2D1_COLOR_F& c) const {
	return RGB((int)std::lround(c.r * 255), (int)std::lround(c.g * 255), (int)std::lround(c.b * 255));
}

Chrome chrome() { return Chrome{ prefs().dark }; }

namespace {

// Segoe Fluent Icons on Windows 11, Segoe MDL2 Assets on Windows 10.
const wchar_t* iconFamily() {
	static const wchar_t* family = [] {
		IDWriteFontCollection* fonts = nullptr;
		const wchar_t* found = L"Segoe MDL2 Assets";
		if (IDWriteFactory* dw = dwFactory()) {
			if (SUCCEEDED(dw->GetSystemFontCollection(&fonts, FALSE)) && fonts) {
				UINT32 i = 0;
				BOOL exists = FALSE;
				if (SUCCEEDED(fonts->FindFamilyName(L"Segoe Fluent Icons", &i, &exists)) && exists) found = L"Segoe Fluent Icons";
				fonts->Release();
			}
		}
		return found;
	}();
	return family;
}

IDWriteTextFormat* iconFormat(float size) {
	static std::map<int, IDWriteTextFormat*> cache;
	const int key = (int)std::lround(size * 10);
	auto it = cache.find(key);
	if (it != cache.end()) return it->second;
	IDWriteTextFormat* f = nullptr;
	if (IDWriteFactory* dw = dwFactory()) {
		dw->CreateTextFormat(iconFamily(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
		                     DWRITE_FONT_STRETCH_NORMAL, size, L"", &f);
		if (f) {
			f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
			f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
			f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
		}
	}
	cache[key] = f;
	return f;
}

ID2D1SolidColorBrush* brushFor(ID2D1RenderTarget* rt, const D2D1_COLOR_F& c) {
	ID2D1SolidColorBrush* b = nullptr;
	rt->CreateSolidColorBrush(c, &b);
	return b;
}

}  // namespace

void drawIcon(ID2D1RenderTarget* rt, wchar_t glyph, const D2D1_RECT_F& box, float size, const D2D1_COLOR_F& color) {
	IDWriteTextFormat* f = iconFormat(size);
	ID2D1SolidColorBrush* b = brushFor(rt, color);
	if (f && b) rt->DrawText(&glyph, 1, f, box, b, D2D1_DRAW_TEXT_OPTIONS_NONE);
	if (b) b->Release();
}

void drawNewDocIcon(ID2D1RenderTarget* rt, const D2D1_RECT_F& box, const D2D1_COLOR_F& color) {
	// The page, nudged left and up, and a plus badge on its lower right with
	// a ring of the background cut out of the page behind it.
	const float cx = (box.left + box.right) / 2, cy = (box.top + box.bottom) / 2;
	drawIcon(rt, Icon::Page, D2D1::RectF(box.left - 2, box.top - 1, box.right - 2, box.bottom - 1), 15, color);
	ID2D1SolidColorBrush* b = nullptr;
	if (FAILED(rt->CreateSolidColorBrush(color, &b))) return;
	const D2D1_POINT_2F c = D2D1::Point2F(cx + 5, cy + 5);
	rt->DrawLine(D2D1::Point2F(c.x - 3.5f, c.y), D2D1::Point2F(c.x + 3.5f, c.y), b, 1.4f);
	rt->DrawLine(D2D1::Point2F(c.x, c.y - 3.5f), D2D1::Point2F(c.x, c.y + 3.5f), b, 1.4f);
	b->Release();
}

void drawNewTabIcon(ID2D1RenderTarget* rt, const D2D1_RECT_F& box, const D2D1_COLOR_F& color) {
	const float cx = (box.left + box.right) / 2, cy = (box.top + box.bottom) / 2;
	ID2D1SolidColorBrush* b = nullptr;
	if (FAILED(rt->CreateSolidColorBrush(color, &b))) return;
	// The square behind, showing only its top and left edges.
	rt->DrawLine(D2D1::Point2F(cx - 7, cy + 3), D2D1::Point2F(cx - 7, cy - 5), b, 1.1f);
	rt->DrawLine(D2D1::Point2F(cx - 7, cy - 7), D2D1::Point2F(cx + 3, cy - 7), b, 1.1f);
	// The square in front, and its plus.
	rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - 4, cy - 4, cx + 7, cy + 7), 2, 2), b, 1.1f);
	rt->DrawLine(D2D1::Point2F(cx + 1.5f, cy - 1), D2D1::Point2F(cx + 1.5f, cy + 4), b, 1.1f);
	rt->DrawLine(D2D1::Point2F(cx - 1, cy + 1.5f), D2D1::Point2F(cx + 4, cy + 1.5f), b, 1.1f);
	b->Release();
}

void drawFolderIcon(ID2D1RenderTarget* rt, const D2D1_RECT_F& box, const D2D1_COLOR_F& color) {
	const float cx = (box.left + box.right) / 2, cy = (box.top + box.bottom) / 2;
	ID2D1Factory* f = nullptr;
	rt->GetFactory(&f);
	ID2D1PathGeometry* g = nullptr;
	ID2D1SolidColorBrush* b = nullptr;
	if (f && SUCCEEDED(f->CreatePathGeometry(&g)) && SUCCEEDED(rt->CreateSolidColorBrush(color, &b))) {
		ID2D1GeometrySink* sink = nullptr;
		if (SUCCEEDED(g->Open(&sink))) {
			// The tab on the top left, then down and round the body.
			const float l = cx - 7.5f, r = cx + 7.5f, t = cy - 5.5f, bot = cy + 5.5f;
			sink->BeginFigure(D2D1::Point2F(l, bot - 1), D2D1_FIGURE_BEGIN_HOLLOW);
			sink->AddLine(D2D1::Point2F(l, t + 1));
			sink->AddLine(D2D1::Point2F(l + 1, t));
			sink->AddLine(D2D1::Point2F(l + 5, t));
			sink->AddLine(D2D1::Point2F(l + 6.5f, t + 2));
			sink->AddLine(D2D1::Point2F(r - 1, t + 2));
			sink->AddLine(D2D1::Point2F(r, t + 3));
			sink->AddLine(D2D1::Point2F(r, bot - 1));
			sink->AddLine(D2D1::Point2F(r - 1, bot));
			sink->AddLine(D2D1::Point2F(l + 1, bot));
			sink->EndFigure(D2D1_FIGURE_END_CLOSED);
			sink->BeginFigure(D2D1::Point2F(l, t + 4), D2D1_FIGURE_BEGIN_HOLLOW);
			sink->AddLine(D2D1::Point2F(r, t + 4));
			sink->EndFigure(D2D1_FIGURE_END_OPEN);
			sink->Close();
			sink->Release();
			rt->DrawGeometry(g, b, 1.1f);
		}
	}
	if (b) b->Release();
	if (g) g->Release();
	if (f) f->Release();
}

void fillRound(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, float radius, const D2D1_COLOR_F& c) {
	if (c.a <= 0) return;
	if (ID2D1SolidColorBrush* b = brushFor(rt, c)) {
		rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), b);
		b->Release();
	}
}

void strokeRound(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, float radius, const D2D1_COLOR_F& c, float width) {
	if (c.a <= 0) return;
	if (ID2D1SolidColorBrush* b = brushFor(rt, c)) {
		const float h = width / 2;
		rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left + h, r.top + h, r.right - h, r.bottom - h), radius, radius), b, width);
		b->Release();
	}
}

void fillRect(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, const D2D1_COLOR_F& c) {
	if (c.a <= 0) return;
	if (ID2D1SolidColorBrush* b = brushFor(rt, c)) {
		rt->FillRectangle(r, b);
		b->Release();
	}
}

void fillCircle(ID2D1RenderTarget* rt, D2D1_POINT_2F center, float radius, const D2D1_COLOR_F& c) {
	if (c.a <= 0) return;
	if (ID2D1SolidColorBrush* b = brushFor(rt, c)) {
		rt->FillEllipse(D2D1::Ellipse(center, radius, radius), b);
		b->Release();
	}
}

double speedFraction(int stepMs) {
	return 1 - std::min(1.0, std::max(0.0, std::log((double)std::max(1, stepMs)) / std::log(500.0)));
}

int speedFromFraction(double f) {
	f = std::min(1.0, std::max(0.0, f));
	return std::max(1, std::min(500, (int)std::lround(std::pow(500.0, 1 - f))));
}
