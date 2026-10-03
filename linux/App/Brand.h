// CedarLogic's own look, for the places that introduce it (the launch
// screen, the welcome, What's New, the guided tour, the band over the truth
// table), whatever the app's theme: the icon's near-black green, its faint
// grid, a neon bloom, glass cards and neon buttons with dark ink on them --
// the Mac's BrandUI.swift, as the Windows app draws it, in Cairo.

#ifndef CL_LINUX_BRAND_H
#define CL_LINUX_BRAND_H

#include "Chrome.h"
#include <string>
#include <vector>

namespace brand {

extern const Color kInk, kInkDeep, kNeon, kNeonDeep, kDim;
// Text on the ground.
extern const Color kPrimary, kSecondary, kFaint;

inline Color alpha(Color c, float a) { c.a = a; return c; }
double easeOut(double t);     // cubic
// The wx and Mac page slide's curve: (0.33, 1, 0.68, 1).
double slideCurve(double t);

enum Weight { Normal = 400, Medium = 500, SemiBold = 600, Bold = 700 };

// The ground: the icon's dark green, its grid showing faintly towards the
// middle, a green bloom at (bloomX, bloomY) as shares of the size.
void ground(cairo_t* cr, float w, float h, float bloomX = 0.5f, float bloomY = 0.18f, float gridStep = 28);
// A glass card; `lit` gives it the neon edge and glow.
void card(cairo_t* cr, const RectF& r, bool lit = false, float radius = 14);
// A soft glow around a rounded rect.
void glow(cairo_t* cr, const RectF& r, float radius, Color color, float blur);
// Neon (primary) or glass (secondary) pill buttons.
void button(cairo_t* cr, const RectF& r, const std::string& label, bool primary, bool hot = false);
// A key as drawn on the brand pages; `lit` while it's pressed.
void keycap(cairo_t* cr, float x, float y, const std::string& label, bool lit = false, float size = 38);
float keycapWidth(const std::string& label, float size = 38);
// A row of options in a capsule, the chosen one lit in neon. Fills `hits`.
void segmented(cairo_t* cr, float x, float y, const std::vector<std::string>& options, int chosen, std::vector<RectF>& hits);

// Text in the UI face, its top at y. Wrapped to `width` when width > 0
// (and aligned in it); returns the height used. `spacing` adds room between
// the letters, in points.
float text(cairo_t* cr, const std::string& s, float x, float y, float size, Weight weight, const Color& color, float width = 0,
           TextAlign align = TextAlign::Leading, float spacing = 0);
float textWidth(const std::string& s, float size, Weight weight, float spacing = 0);
// The eyebrow, title and line over each page; returns the bottom.
float heading(cairo_t* cr, float x, float y, float width, const std::string& eyebrow, const std::string& title, const std::string& line);
// A small label over a setting (APPEARANCE, YOUR NAME...).
void label(cairo_t* cr, float x, float y, const std::string& s);

// The app's icon at size points, with an optional green glow.
void icon(cairo_t* cr, float x, float y, float size, float glow = 0);

// The app's own subject, alive: two switches stepping through 00, 01, 10,
// 11 into an AND gate, and a lamp that lights only for the last. t in seconds.
void hero(cairo_t* cr, const RectF& box, double t);

// The launch chime (FirstLaunch.wav), through whatever plays sound here
// (PipeWire, PulseAudio or ALSA); quietly nothing when none does.
void playChime();

}  // namespace brand

#endif  // CL_LINUX_BRAND_H
