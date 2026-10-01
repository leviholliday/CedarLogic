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
