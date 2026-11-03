// Drawing on the circuit: the tools (see Ink.h).

#include "Ink.h"

#include <algorithm>
#include <cmath>

namespace ink {

const char* const kFullNote = "This drawing is full. Erase some, or clear a page.";
const char* const kReadOnlyNote =
	"This page has a drawing from a newer CedarLogic. It's kept, but this version can't show or change it.";

namespace {

// The Mac's InkSettings: the same colours and widths (world units).
const std::vector<std::string> kPenColors = { "ink", "red", "orange", "green", "blue", "purple" };
const std::vector<std::string> kHighlighterColors = { "yellow", "green", "blue", "pink" };
const std::vector<double> kPenWidths = { 0.12, 0.25, 0.5 };
const std::vector<double> kHighlighterWidths = { 0.8, 1.2, 2.0 };

bool has(const std::vector<double>& list, double w) {
	for (double x : list) if (std::fabs(x - w) < 1e-9) return true;
	return false;
}

}  // namespace

const char* toolName(int t) { return t == Highlighter ? "Highlighter" : t == Eraser ? "Eraser" : "Pen"; }
const char* toolKey(int t) { return t == Highlighter ? "H" : t == Eraser ? "E" : "P"; }
const char* widthName(int i) { return i == 0 ? "Thin" : i == 2 ? "Thick" : "Medium"; }

int tool() { return std::min(2, std::max(0, prefs().inkTool)); }

void setTool(int t) {
	prefs().inkTool = std::min(2, std::max(0, t));
	prefs().save();
}

bool highlighter() { return tool() == Highlighter; }
int engineTool() { return highlighter() ? CL_INK_HIGHLIGHTER : CL_INK_PEN; }
const std::string& color() { return highlighter() ? prefs().inkHighlighterColor : prefs().inkPenColor; }
double width() { return highlighter() ? prefs().inkHighlighterWidth : prefs().inkPenWidth; }
const std::vector<std::string>& colors() { return highlighter() ? kHighlighterColors : kPenColors; }
const std::vector<double>& widths() { return highlighter() ? kHighlighterWidths : kPenWidths; }

int widthIndex() {
	const std::vector<double>& w = widths();
	for (size_t i = 0; i < w.size(); i++) if (std::fabs(w[i] - width()) < 1e-9) return (int)i;
	return 1;
}

void setColor(const std::string& token) {
	(highlighter() ? prefs().inkHighlighterColor : prefs().inkPenColor) = token;
	prefs().save();
}

void setWidthIndex(int i) {
	const std::vector<double>& w = widths();
	if (i < 0 || i >= (int)w.size()) return;
	(highlighter() ? prefs().inkHighlighterWidth : prefs().inkPenWidth) = w[i];
	prefs().save();
}

void validate() {
	Prefs& p = prefs();
	if (std::find(kPenColors.begin(), kPenColors.end(), p.inkPenColor) == kPenColors.end()) p.inkPenColor = "ink";
	if (std::find(kHighlighterColors.begin(), kHighlighterColors.end(), p.inkHighlighterColor) == kHighlighterColors.end())
		p.inkHighlighterColor = "yellow";
	if (!has(kPenWidths, p.inkPenWidth)) p.inkPenWidth = 0.25;
	if (!has(kHighlighterWidths, p.inkHighlighterWidth)) p.inkHighlighterWidth = 1.2;
}

std::string colorName(const std::string& token, bool hl) {
	std::string s = token;
	if (!s.empty()) s[0] = (char)std::toupper((unsigned char)s[0]);
	return s + (hl ? " highlighter" : " pen");
}

Color swatch(const std::string& token, bool hl, bool dark) {
	double r = 0, g = 0, b = 0, a = 1;
	cl_ink_color(token.c_str(), hl ? CL_INK_HIGHLIGHTER : CL_INK_PEN, dark ? 1 : 0, &r, &g, &b, &a);
	return colorF((float)r, (float)g, (float)b, hl ? 1.0f : (float)a);
}

}  // namespace ink
