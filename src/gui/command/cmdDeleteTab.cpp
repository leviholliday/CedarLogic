
#include "cmdDeleteTab.h"
#include "wx/simplebook.h"
#include "GUICanvas.h"
#include "MainApp.h"
#include "MainFrame.h"

DECLARE_APP(MainApp)
#include <algorithm>

// Tab titles are "Page 1", "Page 2", ... in canvas order; after any add or
// delete they all have to be rewritten.
static void renumberTabs() {
	if (wxGetApp().mainframe) wxGetApp().mainframe->RenumberTabs();
}

class guiGate;
class guiWire;

cmdDeleteTab::cmdDeleteTab(GUICircuit* gCircuit, GUICanvas* gCanvas,
		wxBookCtrlBase* book, std::vector< GUICanvas* >* canvases,
		unsigned long ID) :
			klsCommand(true, "Delete Tab") {

	this->gCircuit = gCircuit;
	this->gCanvas = gCanvas;
	this->canvasBook = book;
	this->canvases = canvases;
	this->canvasID = ID;


	std::unordered_map< unsigned long, guiGate* >* gateList = gCanvas->getGateList();
	std::unordered_map< unsigned long, guiGate* >::iterator thisGate = gateList->begin();
	while (thisGate != gateList->end()) {
		this->gates.push_back(thisGate->first);
		thisGate++;
	}
	std::unordered_map< unsigned long, guiWire* >* wireList = gCanvas->getWireList();
	std::unordered_map< unsigned long, guiWire* >::iterator thisWire = wireList->begin();
	while (thisWire != wireList->end()) {
		this->wires.push_back(thisWire->first);
		thisWire++;
	}
}

cmdDeleteTab::~cmdDeleteTab() {
	while (!(cmdList.empty())) {
		cmdList.pop();
	}
	// An applied Delete Tab leaves the canvas parked so Undo can restore the
	// same object. If this command is now leaving history, Undo is no longer
	// possible and the command is the final owner of that hidden window.
	if (deleted && gCanvas != nullptr) {
		if (MainFrame* frame = wxGetApp().mainframe)
			frame->DiscardDetachedCanvas(gCanvas);
		else
			gCanvas->Destroy();
		gCanvas = nullptr;
	}
}

bool cmdDeleteTab::Do() {
	cmdList.push(std::unique_ptr<klsCommand>(new cmdDeleteSelection(gCircuit, gCanvas, gates, wires)));
	cmdList.top()->Do();

	canvases->erase(std::remove(canvases->begin(), canvases->end(), gCanvas), canvases->end());
	// The frame takes it off whichever pane is showing it: a tab dragged into
	// a split is not a page of the main strip any more, and this command may
	// be undone long after that move.
	if (wxGetApp().mainframe) wxGetApp().mainframe->DetachCanvasPage(gCanvas);
	renumberTabs();
	//TODO fix canvases not refreshing
	gCanvas->Hide();
	deleted = true;
	return true;
}
bool cmdDeleteTab::Undo() {
	canvases->insert(canvases->begin() + canvasID, gCanvas);
	if (wxGetApp().mainframe)
		wxGetApp().mainframe->AttachCanvasPage(gCanvas, (int)canvasID);
	renumberTabs();
	while (!(cmdList.empty())) {
		cmdList.top()->Undo();
		cmdList.pop();
	}
	deleted = false;
	return true;
}

int cmdDeleteTab::pageToShow(bool isUndo) const {
	// Undo re-inserts the tab at canvasID -- show the restored tab. Redo deletes
	// it -- show the tab that shifted into its slot, or the last if it was last.
	if (isUndo) return (int)canvasID;
	int last = (int)canvases->size() - 1;
	return (int)canvasID <= last ? (int)canvasID : last;
}
