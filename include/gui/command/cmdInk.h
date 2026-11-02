#pragma once

// Drawing on a page (the website's docs/DRAWING-NOTES.md 4.6): one undo step
// per stroke (with the parts a long stroke was split into), per eraser drag
// and per Clear. The strokes live on the page (GUICanvas::ink), so the steps
// hold the page, never a part.

#include "klsCommand.h"
#include "circuit_file.hpp"

#include <utility>
#include <vector>

// "Draw": strokes added to the end of the page's drawing.
class cmdInkAdd : public klsCommand {
public:
	cmdInkAdd(GUICanvas *page, std::vector<cl::InkStroke> strokes);
	bool Do() override;
	bool Undo() override;
private:
	std::vector<cl::InkStroke> strokes;
};

// "Erase": whole strokes taken out, each with where it was. Made after the
// eraser has already taken them (they vanish as it passes), so it is stored
// rather than submitted; Do takes them out again on a redo.
class cmdInkErase : public klsCommand {
public:
	cmdInkErase(GUICanvas *page, std::vector<std::pair<size_t, cl::InkStroke>> erased);
	bool Do() override;
	bool Undo() override;
private:
	std::vector<std::pair<size_t, cl::InkStroke>> erased;   // ascending index
};

// "Clear Drawing": every stroke on the page.
class cmdInkClear : public klsCommand {
public:
	explicit cmdInkClear(GUICanvas *page);
	bool Do() override;
	bool Undo() override;
private:
	std::vector<cl::InkStroke> cleared;
};
