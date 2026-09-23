
#include "cmdAddTab.h"
#include "wx/simplebook.h"
#include "../GUICanvas.h"
#include "../MainApp.h"
#include "../MainFrame.h"

DECLARE_APP(MainApp)
#include <algorithm>

cmdAddTab::cmdAddTab(GUICircuit* gCircuit, wxBookCtrlBase* book,
		std::vector<GUICanvas *> *canvases) :
			klsCommand(true, "Add Tab") {

	this->gCircuit = gCircuit;
	this->canvasBook = book;
	this->canvases = canvases;
}

cmdAddTab::~cmdAddTab() {
	// If Add Tab is currently undone, its canvas is hidden in MainFrame's
	// parking panel. Once this command is discarded there is no route by which
	// redo can reattach it, so keeping the window leaks the canvas (including
	// its GL state and timers) for the rest of the session.
	if (!attached && addedCanvas != nullptr) {
		if (MainFrame* frame = wxGetApp().mainframe)
			frame->DiscardDetachedCanvas(addedCanvas);
		else
			addedCanvas->Destroy();
		addedCanvas = nullptr;
	}
}

bool cmdAddTab::Do() {
	MainFrame* frame = wxGetApp().mainframe;
	// Reuse the canvas from the first Do, so a redo restores the very object
	// the rest of the undo history is still pointing at. The book is only
	// good for that first Do: by a redo the tab bar may have been rebuilt.
	if (addedCanvas == nullptr) {
		addedCanvas = new GUICanvas(canvasBook, gCircuit, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS);
	}
	canvases->push_back(addedCanvas);
	if (frame) {
		// Back into the tab strip from wherever an undo left it waiting.
		frame->AttachCanvasPage(addedCanvas, (int)canvases->size() - 1);
	} else {
		addedCanvas->Show();
		canvasBook->AddPage(addedCanvas, "Page", false);
	}
	attached = true;
	return true;
}

bool cmdAddTab::Undo() {
	canvases->erase(std::remove(canvases->begin(), canvases->end(), addedCanvas), canvases->end());
	// The page is removed, not deleted: destroying the window would leave every
	// command still in the undo history pointing at freed memory.
	if (wxGetApp().mainframe) {
		wxGetApp().mainframe->DetachCanvasPage(addedCanvas);
		wxGetApp().mainframe->RenumberTabs();
	}
	attached = false;
	return true;
}

int cmdAddTab::pageToShow(bool /*isUndo*/) const {
	// Redo shows the newly added (now last) tab; undo shows the new last tab
	// after the added one is removed. Both are the last existing page.
	return (int)canvases->size() - 1;
}
