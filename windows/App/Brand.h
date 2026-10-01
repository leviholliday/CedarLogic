// CedarLogic's own look, for the places that introduce it (the launch
// screen, the welcome, What's New, the guided tour), whatever the app's
// theme: the icon's near-black green, its faint grid, a neon bloom, glass
// cards and neon buttons with dark ink on them -- the Mac's BrandUI.swift,
// drawn with Direct2D.

#ifndef CL_WINDOWS_BRAND_H
#define CL_WINDOWS_BRAND_H

#include "App.h"
#include <dwrite.h>
#include <string>
#include <vector>

namespace brand {

extern const D2D1_COLOR_F kInk, kInkDeep, kNeon, kNeonDeep, kDim;
// Text on the ground.
extern const D2D1_COLOR_F kPrimary, kSecondary, kFaint;

inline D2D1_COLOR_F alpha(D2D1_COLOR_F c, float a) { c.a = a; return c; }
double easeOut(double t);     // cubic
// The wx and Mac page slide's curve: (0.33, 1, 0.68, 1) over 0.32 s.
double slideCurve(double t);

// The ground: the icon's dark green, its grid showing faintly towards the
// middle, a green bloom at (bloomX, bloomY) as shares of the size.
void ground(ID2D1RenderTarget* rt, float w, float h, float bloomX = 0.5f, float bloomY = 0.18f, float gridStep = 28);
// A glass card; `lit` gives it the neon edge and glow.
void card(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, bool lit = false, float radius = 14);
// Neon (primary) or glass (secondary) pill buttons.
void button(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, const std::string& label, bool primary, bool hot = false);
// A key as drawn on the brand pages; `lit` while it's pressed.
void keycap(ID2D1RenderTarget* rt, float x, float y, const std::string& label, bool lit = false, float size = 38);
float keycapWidth(const std::string& label, float size = 38);
// A row of options in a capsule, the chosen one lit in neon. Fills `hits`.
void segmented(ID2D1RenderTarget* rt, float x, float y, const std::vector<std::string>& options, int chosen,
               std::vector<D2D1_RECT_F>& hits);

// Text in the system's UI face. Wrapped to `width` when width > 0; returns
// the height used. `spacing` adds room between the letters.
float text(ID2D1RenderTarget* rt, const std::string& s, float x, float y, float size, DWRITE_FONT_WEIGHT weight,
           const D2D1_COLOR_F& color, float width = 0, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
           float spacing = 0);
float textWidth(const std::string& s, float size, DWRITE_FONT_WEIGHT weight, float spacing = 0);
// The eyebrow, title and line over each page; returns the bottom.
float heading(ID2D1RenderTarget* rt, float x, float y, float width, const std::string& eyebrow, const std::string& title,
              const std::string& line);
// A small label over a setting (APPEARANCE, YOUR NAME...).
void label(ID2D1RenderTarget* rt, float x, float y, const std::string& s);

// The app's icon (the launch screen's), at size points, with an optional
// green glow; made once per render target.
void icon(ID2D1RenderTarget* rt, float x, float y, float size, float glow = 0);

// The app's own subject, alive: two switches stepping through 00, 01, 10,
// 11 into an AND gate, and a lamp that lights only for the last. Wires
// carrying a 1 glow with signals running along them. t in seconds.
void hero(ID2D1RenderTarget* rt, const D2D1_RECT_F& box, double t);

}  // namespace brand

#endif  // CL_WINDOWS_BRAND_H
