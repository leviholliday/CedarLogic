
#pragma once
#include "klsCommand.h"
#include <vector>

class wxBookCtrlBase;

//JV - cmdAddTab - add a new tab into canvasBook
class cmdAddTab : public klsCommand {
public:
	cmdAddTab(GUICircuit* gCircuit, wxBookCtrlBase* book,
		std::vector<GUICanvas *> *canvases);

	bool Do();

	bool Undo();

	int pageToShow(bool isUndo) const override;

private:
	wxBookCtrlBase* canvasBook;
	std::vector<GUICanvas *>* canvases;

	// The canvas this command added, kept across an undo rather than destroyed.
	// Every command recorded on that tab remembers the canvas it belongs to,
	// and the undo history hands those pointers back as it steps through. So
	// destroying the canvas on undo and building a different one on redo left
	// all of them pointing at freed memory. cmdDeleteTab already works this
	// way: take the page out of the notebook and hide it, keep the object.
	GUICanvas* addedCanvas = nullptr;
};