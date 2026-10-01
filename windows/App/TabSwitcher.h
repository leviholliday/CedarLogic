// Ctrl+Tab, as the Mac app's TabSwitcher: tabs in the order you last used
// them, the one you're on first. A quick Ctrl+Tab flips to the previous tab;
// held for a moment, a panel of the tabs shows (a picture of each), Tab and
// Shift+Tab move through it, the pointer picks, and letting go of Ctrl
// switches. Escape changes nothing.

#ifndef CL_WINDOWS_TABSWITCHER_H
#define CL_WINDOWS_TABSWITCHER_H

#include "App.h"
#include <cstdint>
#include <vector>

class CircuitWindow;

class TabSwitcher {
public:
	explicit TabSwitcher(CircuitWindow* window) : win(window) {}
	~TabSwitcher();
	// Ctrl+Tab (or with Shift). False when there's nothing to switch between.
	bool key(bool backwards);
	void cancel();
	bool active() const { return isActive; }
	// From the window's timer, while active: Ctrl let go, or time to show.
	void tick();

private:
	CircuitWindow* win;
	bool isActive = false;
	double started = 0;
	std::vector<int> tabs;   // indexes into the window's tabs, most recent first
	int selected = 0;
	HWND panel = nullptr;
	WindowSurface surface;
	std::vector<D2D1_RECT_F> cards;
	POINT shownAt{};

	void commit(int index);
	void show();
	void hide();
	void paint();
	static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
};

#endif  // CL_WINDOWS_TABSWITCHER_H
