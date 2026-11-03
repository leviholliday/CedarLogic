// Drawing on the circuit (the website's docs/DRAWING-NOTES.md, the Mac's
// Drawing.swift): the tools and what they remember. The strokes themselves
// are the engine's (cl_ink_*): it captures, simplifies, caps and draws them in
// their layers (highlighter under the parts, pen over them), and each stroke,
// eraser drag or Clear is one undo step. The canvas does the pointer, the keys
// (P, H, E, 1 2 3, Escape) and the drawing bar; the window the menus.

#ifndef CL_LINUX_INK_H
#define CL_LINUX_INK_H

#include "App.h"
#include "Chrome.h"
#include <string>
#include <vector>

namespace ink {

enum Tool { Pen = 0, Highlighter = 1, Eraser = 2 };

extern const char* const kFullNote;      // a cap was reached
extern const char* const kReadOnlyNote;  // a page with a drawing from a newer CedarLogic

const char* toolName(int tool);   // "Pen", "Highlighter", "Eraser"
const char* toolKey(int tool);    // "P", "H", "E"
const char* widthName(int index); // "Thin", "Medium", "Thick"

int tool();
void setTool(int tool);
// The tool that draws (the eraser's colour and width are the pen's).
bool highlighter();
int engineTool();                 // CL_INK_PEN or CL_INK_HIGHLIGHTER
const std::string& color();
double width();
const std::vector<std::string>& colors();
const std::vector<double>& widths();
int widthIndex();
void setColor(const std::string& token);
void setWidthIndex(int index);
// Prefs read from an older or hand-edited file: back to ones the bar has.
void validate();

// "Red pen", "Yellow highlighter".
std::string colorName(const std::string& token, bool highlighter);
// A token's colour as the canvas draws it, light or dark (a highlighter's
// at full strength, so the swatches can be told apart).
Color swatch(const std::string& token, bool highlighter, bool dark);

}  // namespace ink

#endif  // CL_LINUX_INK_H
