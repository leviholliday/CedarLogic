// The Actions rail on a circuit window's right edge (the Mac's ActionsRail
// and ActionsWindowParts, mac/App/Actions.swift): the palette's mirror, a
// strip of the chosen actions (Actions.h), each named with its shortcut as
// the pointer rests on it. A click runs it; dragging one up or down (6
// points, or after holding it 0.45 s) moves it, in the order the Customize
// list keeps. A click on its left edge folds it to a 6-point tab and a click
// on the tab brings it back. Folded, the window's right edge is a hot strip
// (Actions.h's EdgeHover): the tab peeks, and after 100 ms the rail slides
// out over the canvas (a GtkOverlay, so the canvas keeps its width) until
// the pointer has been away 400 ms. A narrow window and focus mode fold it
// too, and give it back as it was.
//
// Three widgets: the dock (the rail or its tab, in the window's row, taking
// its width from the canvas), the floater (over the canvas: the peeking
// tab, or the rail slid out), and the labels (over the canvas, left of the
// rail, taking no clicks). The pointer for the hot strip comes from a
// motion controller on the whole window, in its capture phase, so whatever
// is under the pointer, the strip sees it.

#ifndef CL_LINUX_RAIL_H
#define CL_LINUX_RAIL_H

#include "Actions.h"
#include "Anim.h"
#include "Drawn.h"

#include <string>
#include <vector>

class CircuitWindow;

class Rail {
public:
	explicit Rail(CircuitWindow* window);
	~Rail();
	// The window's row (side panel, canvas, notes) in the overlay the rail
	// slides out over; pack what this returns where the row was.
	GtkWidget* wrap(GtkWidget* row);
	// At the row's end, once everything else is in it.
	void addDock(GtkWidget* row);

	// The mode, the order, the choice or a setting changed (Actions.h's changed()).
	void refresh();
	void tick();
	void setFocus(bool on);
	// View > Show/Hide Actions (F9): with the actions somewhere else (or
	// nowhere), the rail on screen and open; slid out, back in; folded,
	// open; open, folded.
	void toggle();
	// On screen and open (What's New's Show the Actions).
	void showOpen();
	// Escape: a rail slid out on hover goes back (true when it took the key).
	bool escape();
	// Open on screen now, pinned or slid out (Show or Hide Actions in the menu).
	bool shownOnScreen() const;
	bool folded() const;
	bool slidOut() const { return hover.slidOut; }

	// --show rail:N (CI's pictures): 0 open with a label, 1 folded, 2 the
	// tab peeking, 3 slid out with a label, 4 mid-drag, 5 focus mode, 6 focus
	// mode slid out, 7 a narrow window, 8 Simulation View, 9 in the toolbar,
	// 10 focus mode peeking.
	void picture(int kind);

	// For --session-check.
	GtkWidget* dockWidget() const;
	GtkWidget* floaterWidget() const;
	int itemCount() const { return (int)items.size(); }
	// The hover's state, for a FAIL line.
	std::string describe() const;
	// An item's middle, in the dock's points.
	void itemCenter(int i, double& x, double& y) const;

private:
	friend class RailView;
	friend class RailLabels;
	CircuitWindow* win;
	GtkWidget* overlay = nullptr;
	class RailView* dock = nullptr;
	class RailView* floater = nullptr;
	class RailLabels* labels = nullptr;
	GtkEventController* motion = nullptr;
	GtkWidget* menu = nullptr;

	actions::FoldState fold;
	actions::EdgeHover hover;
	std::vector<const actions::Item*> items;
	int hovered = -1;
	bool edgeHot = false;          // the pointer on the rail's left edge (fold / keep open)
	bool peekShown = false;
	int floaterState = 0;          // 0 hidden, 1 peeking, 2 the rail
	// In focus mode the tab strip is the title row, with the window's
	// buttons: the strip and the slid-out rail start under it.
	int inset = 0;
	int topInset() const;
	anim::Tween slide;             // 0 in, 1 out
	cairo_surface_t* slideImage = nullptr;   // the rail as it slides, drawn once
	// A press on an item: let go where it was, a click; moved kDragStart or
	// held kHoldTime, a drag that moves it.
	int pressed = -1;
	bool pressedOnFloater = false;
	double pressX = 0, pressY = 0, pressAt = 0;
	bool held = false, dragging = false;
	double dragDy = 0;
	// The one-time line for those who had CedarLogic before the rail.
	double hintAt = -1;
	int hintTries = 0;
	// The pictures.
	bool pictureFold = false, picturePeek = false, pictureSlid = false;
	int pictureLabel = -1, pictureDrag = -1;
	double pictureDy = 0;

	bool onScreen() const;
	bool hoverEnabled() const;
	void apply();
	void updateFloater();
	void startSlide(bool out);
	void unfold();
	void foldAway();
	void pointerAt(double x, double y);
	void pointerGone();
	void setHovered(int i);
	int target() const;
	double shiftOf(int i) const;
	void endPress(bool run);
	void showMenu(GdkEventButton* e);
	void redrawAll();

	// Drawing, shared by the dock and the floater.
	void paintRail(cairo_t* cr, float w, float h, bool slid);
	void paintTab(cairo_t* cr, float w, float h, bool lit);
	int itemAt(float x, float y) const;

	static void motionCb(GtkEventControllerMotion*, double, double, gpointer self);
	static gboolean leaveCb(GtkWidget*, GdkEventCrossing*, gpointer self);
};

#endif  // CL_LINUX_RAIL_H
