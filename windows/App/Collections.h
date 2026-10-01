// Templates and My Parts, as the Mac app has them (Templates.swift,
// MyParts.swift).
//
// Templates: File > New from Template starts a circuit from a built-in
// starter (a lab page, a counter, a 7-segment decoder) or one of yours;
// File > Save as Template keeps the circuit you're in as one of yours, in
// %APPDATA%\CedarLogic\Templates (a folder each: name.txt, template.cdl).
//
// My Parts: a selection saved under a name, to use again. It sits in the side
// panel's "My Parts" and in Add a Gate, and drops in as a copy of those
// gates and wires -- ordinary gates, so any CedarLogic can open the circuit.
// %APPDATA%\CedarLogic\Parts holds a folder each: name.txt, part.txt (the
// selection's clipboard text).

#ifndef CL_WINDOWS_COLLECTIONS_H
#define CL_WINDOWS_COLLECTIONS_H

#include "App.h"
#include <string>
#include <vector>

class CircuitWindow;

namespace templates {
void showPicker(CircuitWindow* from);
void saveCurrent(CircuitWindow* window);
}  // namespace templates

namespace parts {

// How the palette and Add a Gate name a part: a "gate" called "part:<id>".
const char* const kPrefix = "part:";

struct Part {
	std::string id, name, folder;
	std::string gate() const { return kPrefix + id; }
	std::string text() const;
};

std::vector<Part> all();   // by name
bool isPart(const std::string& gateName);
bool find(const std::string& gateName, Part& out);
// A picture of the part, fitted to w x h points (the palette's tiles).
void draw(const std::string& gateName, ID2D1RenderTarget* rt, double w, double h, double scale, bool dark);
void saveSelection(CircuitWindow* window);
// Right-click on a part's tile: Rename, Delete. True when something changed.
bool tileMenu(HWND owner, const std::string& gateName, POINT screen);

}  // namespace parts

#endif  // CL_WINDOWS_COLLECTIONS_H
