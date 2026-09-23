/*****************************************************************************
   Project: CEDAR Logic Simulator
   TabStrip: the row of tabs above the canvas, drawn by hand.

   The native tab control reports nothing about a tab being dragged, so tabs
   could not be reordered and could not be thrown into a split. This draws its
   own tabs over a pageless wxSimplebook and handles the dragging itself:
   sideways to reorder, out over the canvas to split the view.
*****************************************************************************/

#ifndef TABSTRIP_H_
#define TABSTRIP_H_

#include <wx/panel.h>
#include <vector>

class MainFrame;
class GUICanvas;
class wxGraphicsContext;
class DropHint;

class TabStrip : public wxPanel {
public:
	// `pane` is which side of a split this strip belongs to (0 when there
	// is no split).
	TabStrip(wxWindow* parent, MainFrame* frame, int pane);
	~TabStrip() override;

	// Tabs were added, removed, renumbered or switched.
	void Rebuild();

	static int BarHeight() { return 36; }

private:
	struct Tab {
		GUICanvas* canvas = nullptr;
		wxString label;
		wxRect rect;
		wxRect close;
	};

	void layout();
	int tabAt(const wxPoint& p) const;
	// Which half of the canvas area the pointer is over: -1 left, 1 right,
	// 0 neither (still over the strip, or the drag has not left it).
	// Where a dragged tab would land: which pane, and which half of it.
	int dropSide(const wxPoint& screenPos) const;
	int dropPane(const wxPoint& screenPos) const;
	void showHint(int side);

	void OnPaint(wxPaintEvent& e);
	void OnDown(wxMouseEvent& e);
	void OnUp(wxMouseEvent& e);
	void OnMotion(wxMouseEvent& e);
	void OnLeave(wxMouseEvent& e);
	void OnCaptureLost(wxMouseCaptureLostEvent& e);
	void endDrag(bool cancelled);

	MainFrame* frame;
	int pane = 0;
	std::vector<Tab> tabs;
	wxRect plusRect;
	int hover = -1, hoverClose = -1;

	// Dragging one tab: where it started, how far it has moved, and whether
	// that is far enough to count as a drag rather than a click.
	int dragTab = -1;
	bool dragging = false;
	wxPoint dragStart;
	int dragDx = 0;
	int dropAt = -1;       // insertion slot while reordering
	int hintSide = 0;      // the split side being offered
	int hintPane = -1;     // the pane a cross-pane drop would land in
	DropHint* hint = nullptr;

	// What the last press landed on, so a double-click can tell a second
	// press on the same tab (rename) from one on whatever slid under it.
	GUICanvas* downOnTab = nullptr;
	bool downOnEmpty = false;
};

#endif
