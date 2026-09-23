
#pragma once
#include "klsCommand.h"
#include <stack>
#include <memory>
#include <vector>

class wxBookCtrlBase;

//JV - cmdDeleteTab - delete a tab from canvasBook
class cmdDeleteTab : public klsCommand {
public:
	cmdDeleteTab(GUICircuit* gCircuit, GUICanvas* gCanvas, wxBookCtrlBase* book,
		std::vector<GUICanvas *> *canvases, unsigned long ID);

	virtual ~cmdDeleteTab();

	bool Do();

	bool Undo();

	int pageToShow(bool isUndo) const override;

protected:
	std::vector < unsigned long > gates;
	std::vector < unsigned long > wires;
	std::stack<std::unique_ptr<klsCommand>> cmdList;
	wxBookCtrlBase* canvasBook;
	std::vector< GUICanvas* >* canvases;
	unsigned long canvasID;
	// True while this command's canvas is detached and therefore owned by the
	// command rather than by the visible document. Its destructor releases the
	// hidden window if the command leaves history in this state.
	bool deleted = false;

};
