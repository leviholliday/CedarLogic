// The look around the canvas, matched to the Mac app's (CLChrome, CLPalette)
// as the Windows app has it: its colours, its icons (the desktop's symbolic
// icon set, which GTK always brings, tinted), and the drawing helpers the
// toolbar, tabs, side panel and canvas overlays share -- Cairo and Pango
// under the same names the Windows app's Direct2D helpers have, so drawing
// code moves between the two with few changes.

#ifndef CL_LINUX_CHROME_H
#define CL_LINUX_CHROME_H

#include "App.h"
#include <string>

// ---- Geometry and colour, in points ------------------------------------------------

struct Color { float r = 0, g = 0, b = 0, a = 1; };
struct RectF { float left = 0, top = 0, right = 0, bottom = 0; };
struct PointF { float x = 0, y = 0; };

inline Color colorF(float r, float g, float b, float a = 1) { return Color{ r, g, b, a }; }
inline RectF rectF(float l, float t, float r, float b) { return RectF{ l, t, r, b }; }
inline PointF pointF(float x, float y) { return PointF{ x, y }; }
inline Color withAlpha(Color c, float a) { c.a = a; return c; }
inline Color rgb255(int r, int g, int b, float a = 1) { return Color{ r / 255.f, g / 255.f, b / 255.f, a }; }
inline Color fromRGBA(const RGBA& c) { return Color{ (float)c.r, (float)c.g, (float)c.b, (float)c.a }; }
inline bool inRect(const RectF& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
inline float rectWidth(const RectF& r) { return r.right - r.left; }
inline float rectHeight(const RectF& r) { return r.bottom - r.top; }

// Colours, light or dark (0..255 values as the Mac app writes them).
struct Chrome {
	bool dark;
	Color canvas() const;
	Color bar() const;          // the toolbar (classicBar)
	Color barInk() const;       // its icons and text
	Color tabBar() const;
	Color tabCard() const;
	Color tabInk() const;
	Color panel() const;        // the side panel
	Color hairline() const;
	Color sash() const;
	Color accent() const;
	// Text and symbols on an accent-filled button: white, or the icon's dark
	// ink on a light accent (the green, on the dark theme).
	Color onAccent() const;
	// The ink at a strength (the Mac's ink.opacity(a)).
	Color ink(float alpha) const;
};
Chrome chrome();   // for the current theme

// ---- Icons -------------------------------------------------------------------------
// The desktop's symbolic icons (Adwaita's names, which every GTK theme
// answers), drawn in any colour. A few the set hasn't as the Mac has them are
// drawn by hand.
namespace Icon {
constexpr const char* Page = "document-new-symbolic";
constexpr const char* Open = "document-open-symbolic";
constexpr const char* Save = "document-save-symbolic";
constexpr const char* Undo = "edit-undo-symbolic";
constexpr const char* Redo = "edit-redo-symbolic";
constexpr const char* Copy = "edit-copy-symbolic";
constexpr const char* Paste = "edit-paste-symbolic";
constexpr const char* ZoomOut = "zoom-out-symbolic";
constexpr const char* ZoomIn = "zoom-in-symbolic";
constexpr const char* Pause = "media-playback-pause-symbolic";
constexpr const char* Play = "media-playback-start-symbolic";
constexpr const char* Step = "media-skip-forward-symbolic";
constexpr const char* Speed = "speedometer-symbolic";
constexpr const char* Lock = "changes-prevent-symbolic";
constexpr const char* Unlock = "changes-allow-symbolic";
constexpr const char* NewTab = "tab-new-symbolic";
constexpr const char* More = "view-more-horizontal-symbolic";
constexpr const char* ChevronDown = "pan-down-symbolic";
constexpr const char* Stop = "media-playback-stop-symbolic";
constexpr const char* StopSolid = "media-playback-stop-symbolic";
constexpr const char* Close = "window-close-symbolic";
constexpr const char* Dismiss = "window-close-symbolic";
constexpr const char* Add = "list-add-symbolic";
constexpr const char* Search = "edit-find-symbolic";
constexpr const char* Minimize = "window-minimize-symbolic";
constexpr const char* Maximize = "window-maximize-symbolic";
constexpr const char* Restore = "window-restore-symbolic";
constexpr const char* Feedback = "mail-message-new-symbolic";
}
void drawIcon(cairo_t* cr, const char* name, const RectF& box, float size, const Color& color);
// Drawn: a page with a plus (New circuit), a square with a plus over
// another (New tab), a folder (Open), a speech bubble (Send Feedback).
void drawNewDocIcon(cairo_t* cr, const RectF& box, const Color& color);
void drawNewTabIcon(cairo_t* cr, const RectF& box, const Color& color);
void drawFolderIcon(cairo_t* cr, const RectF& box, const Color& color);
void drawBubbleIcon(cairo_t* cr, const RectF& box, const Color& color);
// A magnifier with a minus or plus (zoom), a gauge (speed), a tray with an
// arrow into it (save).
void drawZoomIcon(cairo_t* cr, const RectF& box, const Color& color, bool in);
void drawGaugeIcon(cairo_t* cr, const RectF& box, const Color& color);
void drawSaveIcon(cairo_t* cr, const RectF& box, const Color& color);

// ---- Shapes ---------------------------------------------------------------------------

void setColor(cairo_t* cr, const Color& c);
void roundedPath(cairo_t* cr, const RectF& r, float radius);
void fillRound(cairo_t* cr, const RectF& r, float radius, const Color& c);
void strokeRound(cairo_t* cr, const RectF& r, float radius, const Color& c, float width = 1);
void fillRect(cairo_t* cr, const RectF& r, const Color& c);
void fillCircle(cairo_t* cr, PointF center, float radius, const Color& c);
void strokeCircle(cairo_t* cr, PointF center, float radius, const Color& c, float width = 1);
void drawLine(cairo_t* cr, PointF a, PointF b, const Color& c, float width = 1);

// ---- Text ------------------------------------------------------------------------------
// In the desktop's UI face, `size` in points; drawn from the top of `box`.

enum class TextAlign { Leading, Center, Trailing };
void drawText(cairo_t* cr, const std::string& text, const RectF& box, float size, const Color& color,
              TextAlign align = TextAlign::Leading, bool bold = false);
float textWidth(const std::string& text, float size, bool bold = false);
// Wrapped to the box's width; returns the height it took (drawn when cr).
float drawWrapped(cairo_t* cr, const std::string& text, const RectF& box, float size, const Color& color, bool bold = false,
                  TextAlign align = TextAlign::Leading);

// The speed slider's mapping (fast on the right): a step length of 1..500 ms
// to 0..1 and back, on a log scale as the Mac's.
double speedFraction(int stepMs);
int speedFromFraction(double f);

#endif  // CL_LINUX_CHROME_H
