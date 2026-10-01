// The look around the canvas, matched to the Mac app's (CLChrome, CLPalette):
// its colours, its icons (Windows' own icon font, Segoe Fluent Icons, or
// Segoe MDL2 Assets on Windows 10), and the drawing helpers the toolbar,
// tabs, side panel and canvas overlays share.

#ifndef CL_WINDOWS_CHROME_H
#define CL_WINDOWS_CHROME_H

#include "App.h"
#include <string>

// Colours, light or dark (0..255 values as the Mac app writes them).
struct Chrome {
	bool dark;
	D2D1_COLOR_F canvas() const;
	D2D1_COLOR_F bar() const;          // the toolbar (classicBar)
	D2D1_COLOR_F barInk() const;       // its icons and text
	D2D1_COLOR_F tabBar() const;
	D2D1_COLOR_F tabCard() const;
	D2D1_COLOR_F tabInk() const;
	D2D1_COLOR_F panel() const;        // the side panel
	D2D1_COLOR_F hairline() const;
	D2D1_COLOR_F sash() const;
	D2D1_COLOR_F accent() const;
	// The ink at a strength (the Mac's ink.opacity(a)).
	D2D1_COLOR_F ink(float alpha) const;
	COLORREF gdi(const D2D1_COLOR_F& c) const;
};
Chrome chrome();   // for the current theme

// Icon font glyphs (the codepoints are the same in Fluent and MDL2).
namespace Icon {
const wchar_t Page = 0xE7C3, Open = 0xE8B7, Save = 0xE74E, Undo = 0xE7A7, Redo = 0xE7A6, Copy = 0xE8C8,
              Paste = 0xE77F, ZoomOut = 0xE71F, ZoomIn = 0xE8A3, Pause = 0xE769, Play = 0xE768, Step = 0xE893,
              Speed = 0xEC4A, Lock = 0xE72E, Unlock = 0xE785, NewTab = 0xE78B, More = 0xE712, ChevronDown = 0xE70D,
              ChevronUpDown = 0xE70D, Stop = 0xE71A, Close = 0xE8BB, Add = 0xE710, Search = 0xE721,
              Minimize = 0xE921, StopSolid = 0xE73B, Maximize = 0xE922, Restore = 0xE923, Dismiss = 0xE711;
}
void drawIcon(ID2D1RenderTarget* rt, wchar_t glyph, const D2D1_RECT_F& box, float size, const D2D1_COLOR_F& color);
// Two the icon font hasn't as the Mac has them, drawn: a page with a plus
// (New circuit), and a square with a plus over another (New tab).
void drawNewDocIcon(ID2D1RenderTarget* rt, const D2D1_RECT_F& box, const D2D1_COLOR_F& color);
void drawNewTabIcon(ID2D1RenderTarget* rt, const D2D1_RECT_F& box, const D2D1_COLOR_F& color);
// A folder (Open): Windows 10's icon font has no outline folder that reads
// as one at this size.
void drawFolderIcon(ID2D1RenderTarget* rt, const D2D1_RECT_F& box, const D2D1_COLOR_F& color);

// Shapes, filled and outlined.
void fillRound(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, float radius, const D2D1_COLOR_F& c);
void strokeRound(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, float radius, const D2D1_COLOR_F& c, float width = 1);
void fillRect(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, const D2D1_COLOR_F& c);
void fillCircle(ID2D1RenderTarget* rt, D2D1_POINT_2F center, float radius, const D2D1_COLOR_F& c);
inline D2D1_COLOR_F withAlpha(D2D1_COLOR_F c, float a) { c.a = a; return c; }
inline D2D1_COLOR_F rgb255(int r, int g, int b, float a = 1) { return D2D1::ColorF(r / 255.f, g / 255.f, b / 255.f, a); }
inline bool inRect(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

// The speed slider's mapping (fast on the right): a step length of 1..500 ms
// to 0..1 and back, on a log scale as the Mac's.
double speedFraction(int stepMs);
int speedFromFraction(double f);

#endif  // CL_WINDOWS_CHROME_H
