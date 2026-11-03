// The Actions rail (the Mac's ActionsRail, mac/App/Actions.swift): a strip
// down the circuit window's right edge, the side panel's mirror, of the
// actions chosen in Settings > Actions (Actions.h), each an icon. Resting on
// one shows its name and shortcut beside it; a click runs it. Pressing one
// and moving it 6 points, or holding it 0.45 s, picks it up: the others make
// room around a dashed space, and letting go puts it there, in the order the
// Customize list keeps too. A click on its left edge folds it to a 6-point
// tab, a click on the tab brings it back, and it stays as it was left. In a
// window narrower than 900 points it folds by itself; in focus mode it folds
// to a quieter tab (not hidden), and comes back as it was afterwards.
//
// Folded, with "Open it when the mouse reaches the right edge" on (as it is
// to begin with): a 12-point strip the height of the canvas along the
// window's right edge -- its last column of pixels too -- lights the tab at
// once; 100 ms there slides the rail out over the canvas (passing through
// doesn't), where it stays while the pointer is on it or the strip and goes
// 400 ms after it has left both, but not while its right-click menu is open
// or a drag is going on. Escape puts it back. Its right-click menu: Customize
// Actions..., fold or show it, the actions in the toolbar instead, or none.

#ifndef CL_WINDOWS_RAIL_H
#define CL_WINDOWS_RAIL_H

#include "Actions.h"
#include "Drawn.h"
#include <string>
#include <vector>

class CircuitWindow;

class ActionsRail : public Drawn {
public:
	ActionsRail(CircuitWindow* window, HWND parent);
	~ActionsRail() override;

	bool onScreen() const { return prefs().actionsMode == 0; }
	// Folded to its tab: as it was left, or by itself (a narrow window,
	// focus mode) unless its tab was clicked there.
	bool folded() const { return (prefs().actionsCollapsed || narrow || focus) && !peek; }
	bool slidOut() const { return hov.slidOut; }
	// On screen and open, pinned or slid out (View > Hide Actions then).
	bool showing() const { return onScreen() && (!folded() || hov.slidOut); }
	// Points the window keeps for it at its right edge (0, the tab, or the
	// rail), and how wide the rail's window is: wider than that over the
	// canvas when it's slid out or peeking.
	float reserve() const;
	float widgetWidth() const;
	// The band it runs down, in the window's client pixels (the window's layout).
	void setBand(int top, int bottom) { bandTop = top; bandBottom = bottom; }
	// The window is narrow (under rail::kNarrowWidth); it's in focus mode.
	void setNarrow(bool on);
	void setFocus(bool on);
	// View > Show/Hide Actions with the rail on screen: slid out, back in;
	// folded, open; open, folded.
	void toggle();
	void fold();
	void unfold();
	// From the window's clock: the pointer at the edge, the slide, a press held.
	void tick(double now);
	// Escape in the window: a rail that slid out goes back (true if it took the key).
	bool escape();
	// What it shows changed (Simulation View, the selection, a key...).
	void refresh();
	void hideLabel();
	HWND labelWindow() const { return label && IsWindowVisible(label) ? label : nullptr; }

	// --rail-check and CI's pictures.
	rail::Hover& hover() { return hov; }
	// As the clock does with the pointer: in the strip, over the slid-out rail.
	void pointerForTest(double now, bool strip, bool over);
	bool peeking() const { return peek; }
	// A press, a drag and a release on item `index`, `dy` points down (0: a click).
	void pressForTest(int index, float dy);
	// For the pictures: an item's label showing, slid out, peeking, mid-drag.
	struct Picture {
		std::string label;
		bool slidOut = false, peek = false;
		std::string dragId;
		float dragDy = 0;
	};
	static Picture picture;
	// Off for test and picture runs: the pointer's place isn't looked at,
	// and the one-time status line waits for a real launch.
	static bool watchPointer;
	static bool quiet;

protected:
	void paint(ID2D1RenderTarget* rt, float w, float h) override;
	void mouseMove(float x, float y) override;
	void mouseLeave() override;
	void mouseDown(int button, float x, float y, bool doubleClick) override;
	void mouseUp(int button, float x, float y) override;
	void captureLost() override;

private:
	CircuitWindow* win;
	rail::Hover hov;
	bool narrow = false, focus = false;
	bool peek = false;              // unfolded by its tab in a narrow window or focus mode
	bool peekBeforeFocus = false;   // what peek was as the window went into focus mode
	int bandTop = 0, bandBottom = 0;
	// The slide out over the canvas: 0 in, 1 out.
	double slideFrom = 0, slideTo = 0, slideAt = -1;
	bool wasLit = false;
	// The pointer over it.
	int hotItem = -1;
	bool edgeHot = false;
	// A press on an item, and the drag it may become.
	int pressed = -1;
	int pressZone = 0;              // 1 an item, 2 the edge, 3 the tab
	float pressX = 0, pressY = 0;
	double pressAt = 0;
	bool held = false;
	bool dragging = false;
	std::string dragId;
	int dragFrom = 0;
	float dragDy = 0;
	// The name-and-shortcut label beside the item pointed at (a popup).
	HWND label = nullptr;
	std::string labelId;
	WindowSurface labelSurface;
	std::string tipsFor;            // the tooltips' state, as last set
	// The status line for an earlier install, once.
	double hintAt = -1;
	int hintTries = 0;

	double slideAmount() const;
	void startSlide(bool out, double now);
	float contentLeft(float w) const;   // where the rail is drawn in its window (sliding in from the right)
	std::vector<std::string> items() const;
	int itemAt(float x, float y) const;
	bool onEdge(float x, float y) const;
	int target(int count) const;
	float shift(int i, int count) const;
	bool enabled(const actions::Def& d) const;
	bool isOn(const actions::Def& d) const;
	void paintTab(ID2D1RenderTarget* rt, float w, float h);
	void paintRail(ID2D1RenderTarget* rt, float left, float h, bool slid);
	void paintItem(ID2D1RenderTarget* rt, const actions::Def& d, const D2D1_RECT_F& r, bool lit, bool lifted);
	void showLabel(const std::string& id);
	void paintLabel();
	void updateTips();
	void menu(POINT screen);
	void endDrag(bool drop);
	void collapsedChanged(bool animate);
	static LRESULT CALLBACK labelProc(HWND, UINT, WPARAM, LPARAM);
};

// The Customize sheet (the rail's right-click Customize Actions...): every
// action with a tick, in order, a search, Reset to Default and where they
// go; Settings > Actions has the same list (ActionsList.cpp).
void showCustomizeActions(CircuitWindow* w, const std::string& search = std::string());

#endif  // CL_WINDOWS_RAIL_H
