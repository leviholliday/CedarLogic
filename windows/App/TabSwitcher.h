// Ctrl+Tab, as the Mac app's TabSwitcher: tabs in the order you last used
// them, the one you're on first. A quick Ctrl+Tab flips to the previous tab;
// held for a moment, a panel of the tabs shows (a picture of each), Tab and
// Shift+Tab move through it, the pointer picks, and letting go of Ctrl
// switches. Escape changes nothing.
//
// The panel is the Mac's HUD: a rounded card with a soft shadow (a layered
// window with its own alpha, so the corners are really round on Windows 10
// too), fading in over 0.12 s and out over 0.1 s. The tab you'd go to has an
// accent ring round its picture and its name in bold.

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
	// --dialog switcher (CI's picture): the panel up at once, fully in,
	// without watching Ctrl; then its picture saved as a PNG (with its
	// see-through corners and shadow).
	bool showForTest();
	bool saveFrame(const std::string& file);

private:
	CircuitWindow* win;
	bool isActive = false;
	double started = 0;
	std::vector<int> tabs;   // indexes into the window's tabs, most recent first
	int selected = 0;
	HWND panel = nullptr;
	std::vector<D2D1_RECT_F> cards;   // in points, from the panel's top-left (inside the shadow's margin)
	POINT shownAt{};
	float panelW = 0, panelH = 0;     // points
	float scale = 1;
	double shownTime = -1;            // when it began to fade in
	BYTE alpha = 0;
	// Its picture: a 32-bit bitmap drawn with Direct2D and laid on the
	// screen with UpdateLayeredWindow.
	HDC memDC = nullptr;
	HBITMAP dib = nullptr;
	HGDIOBJ oldBitmap = nullptr;
	int pixelW = 0, pixelH = 0;
	ID2D1DCRenderTarget* rt = nullptr;
	std::vector<ID2D1Bitmap*> thumbs;  // each tab's picture, made once a showing

	void commit(int index);
	void show();
	void hide();
	void render();
	void present();
	void release();
	void makeThumbs();
	static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
};

#endif  // CL_WINDOWS_TABSWITCHER_H
