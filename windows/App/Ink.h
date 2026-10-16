// Drawing on the circuit (the website's docs/DRAWING-NOTES.md, the Mac's
// Drawing.swift): in Draw mode the pointer draws with a pen or a
// highlighter, or erases whole strokes, instead of editing. The strokes are
// the core's (cl_ink_*): it takes the samples, simplifies them, caps them and
// draws them in their layers (highlighter under the parts, pen over them),
// and each stroke, eraser drag or Clear is one undo step. Here: the tools and
// what they last were (InkSettings on the Mac), kept in the settings. The
// canvas has the pointer, the keys (P, H, E, 1, 2, 3, Escape) and the
// drawing bar along its bottom (Canvas.cpp); the window has Draw mode.

#ifndef CL_WINDOWS_INK_H
#define CL_WINDOWS_INK_H

#include "App.h"
#include <cmath>
#include <string>
#include <vector>

namespace ink {

enum Tool { Pen = 0, Highlighter = 1, Eraser = 2 };

inline const std::vector<std::string>& penColors() {
	static const std::vector<std::string> c = { "ink", "red", "orange", "green", "blue", "purple" };
	return c;
}
inline const std::vector<std::string>& highlighterColors() {
	static const std::vector<std::string> c = { "yellow", "green", "blue", "pink" };
	return c;
}
inline const double* penWidths() { static const double w[3] = { 0.12, 0.25, 0.5 }; return w; }
inline const double* highlighterWidths() { static const double w[3] = { 0.8, 1.2, 2.0 }; return w; }
inline const char* widthName(int i) { static const char* n[3] = { "Thin", "Medium", "Thick" }; return n[i < 0 ? 0 : i > 2 ? 2 : i]; }
inline const char* toolName(int t) { static const char* n[3] = { "Pen", "Highlighter", "Eraser" }; return n[t < 0 ? 0 : t > 2 ? 2 : t]; }
inline char toolKey(int t) { return "PHE"[t < 0 ? 0 : t > 2 ? 2 : t]; }

inline int tool() { return prefs().inkTool; }
// The tool that draws (the eraser's colour and width are the pen's).
inline bool drawsWithHighlighter() { return prefs().inkTool == Highlighter; }
inline const std::vector<std::string>& colors() { return drawsWithHighlighter() ? highlighterColors() : penColors(); }
inline const double* widths() { return drawsWithHighlighter() ? highlighterWidths() : penWidths(); }

// The colour in use: one of the tool's own, else its first.
inline std::string color() {
	const std::string& c = drawsWithHighlighter() ? prefs().highlighterColor : prefs().penColor;
	for (const std::string& o : colors()) if (o == c) return c;
	return colors().front();
}
inline double width() {
	const double w = drawsWithHighlighter() ? prefs().highlighterWidth : prefs().penWidth;
	for (int i = 0; i < 3; i++) if (std::fabs(widths()[i] - w) < 1e-9) return w;
	return widths()[1];
}
inline int widthIndex() {
	const double w = width();
	for (int i = 0; i < 3; i++) if (std::fabs(widths()[i] - w) < 1e-9) return i;
	return 1;
}
inline void setColor(const std::string& c) {
	(drawsWithHighlighter() ? prefs().highlighterColor : prefs().penColor) = c;
	prefs().save();
}
inline void setWidthIndex(int i) {
	if (i < 0 || i > 2) return;
	(drawsWithHighlighter() ? prefs().highlighterWidth : prefs().penWidth) = widths()[i];
	prefs().save();
}
inline void setTool(int t) {
	if (t < Pen || t > Eraser) return;
	prefs().inkTool = t;
	prefs().save();
}

// "Red pen", "Yellow highlighter": a colour in plain words.
inline std::string colorName(const std::string& token, bool highlighter) {
	std::string s = token;
	if (!s.empty()) s[0] = (char)toupper((unsigned char)s[0]);
	return s + (highlighter ? " highlighter" : " pen");
}

// A token's colour as the canvas draws it, light or dark (a highlighter's at
// full strength, so the swatches can be told apart).
inline D2D1_COLOR_F swatch(const std::string& token, bool highlighter, bool dark) {
	double r = 0, g = 0, b = 0, a = 1;
	cl_ink_color(token.c_str(), highlighter ? CL_INK_HIGHLIGHTER : CL_INK_PEN, dark ? 1 : 0, &r, &g, &b, &a);
	return D2D1::ColorF((float)r, (float)g, (float)b, highlighter ? 1.0f : (float)a);
}

inline const char* fullNote() { return "This drawing is full. Erase some, or clear a page."; }
inline const char* readOnlyNote() {
	return "This page has a drawing from a newer CedarLogic. It's kept, but this version can't show or change it.";
}

}  // namespace ink

#endif  // CL_WINDOWS_INK_H
