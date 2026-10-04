// The toolbar, drawn as the Mac app's (CLToolbar) in its three styles:
// Seamless (the default: one surface with the canvas, tools quiet until you
// point at them), Classic (tools in soft capsules on their own bar) and
// Minimal (the essentials and the circuit's name; the rest is behind •••).
// In the title bar's place: the circuit's name with its menu, the tools, the
// zoom and the simulation speed, ••• for every menu, and the window's own
// minimize, maximize and close buttons at the right. Settings > Toolbar
// picks the style and hides groups of tools.
//
// Where nothing is under the pointer, the bar is the window's title bar: it
// drags the window, double-clicks maximize, and right-clicks bring up the
// window menu. The maximize button answers as Windows' own (HTMAXBUTTON), so
// Windows 11 shows its snap layouts over it.

#ifndef CL_WINDOWS_TOOLBAR_H
#define CL_WINDOWS_TOOLBAR_H

#include "Drawn.h"
#include <string>
#include <vector>

class CircuitWindow;

// Tool groups that can be hidden, one bit each in prefs().toolbarHidden (the
// Mac's ToolGroup).
enum ToolGroup { TGFile, TGUndo, TGClipboard, TGZoom, TGSim, TGRun, TGLock, TGTheme, TGTab, TGFeedback, TGCount };
const char* toolGroupName(int g);
// The styles (prefs().toolbarStyle), as the Mac numbers them.
enum ToolbarStyle { TSClassic = 0, TSMinimal = 2, TSSeamless = 3 };

class Toolbar : public Drawn {
public:
	Toolbar(CircuitWindow* window, HWND parent);
	static float barHeight() { return 48; }   // points

	// For the frame: where the maximize button is (window client pixels;
	// empty when hidden), and whether the pointer is over it.
	RECT maximizeRect() const;
	void setMaximizeHot(bool hot, bool pressed);
	void layoutNow() { relayout = true; redraw(); }
	// Where a command's button is, on the screen (the guided tour points at it).
	RECT commandRect(int command) const;
	// For --click-test: the middle of a button, in the bar's pixels -- a
	// command's, or kMinimize or kClose for the window's own. False when
	// it isn't shown (the bar left it out at this width).
	static const int kMinimize = -1, kClose = -2;
	bool buttonPoint(int command, POINT& p);
	// Whether the style in use has that button at all (Minimal leaves most
	// to •••), shown at this width or not.
	bool hasButton(int command);
	// For Settings' pictures of each style: drawn in `style`, w x h points,
	// without the window's buttons and not clickable.
	void paintPicture(ID2D1RenderTarget* rt, float w, float h, int style);

protected:
	void paint(ID2D1RenderTarget* rt, float w, float h) override;
	void mouseMove(float x, float y) override;
	void mouseLeave() override;
	void mouseDown(int button, float x, float y, bool doubleClick) override;
	void mouseUp(int button, float x, float y) override;
	void captureLost() override;
	LRESULT hitTest(float x, float y) override;
	LRESULT message(UINT msg, WPARAM wp, LPARAM lp, bool& handled) override;

private:
	enum Kind { Button, Zoom, Speed, Title, More, CaptionMin, CaptionMax, CaptionClose };
	struct Item {
		Kind kind;
		int command;           // for a Button
		int group;             // its ToolGroup: hidden together, a capsule in Classic (-1: always there)
		bool right;            // laid out from the right edge
		D2D1_RECT_F rect{};
		bool shown = true;
	};

	CircuitWindow* win;
	std::vector<Item> items;
	int hot = -1, pressed = -1;
	bool maxHot = false, maxPressed = false;
	bool draggingSpeed = false;
	bool relayout = true;
	float lastW = 0;
	int laidStyle = -1;
	std::string laidTitle;   // the name (and Minimal's page) it was laid out for

	void build(int style, bool live);
	void layout(float w, float h, int style, bool live = true);
	void draw(ID2D1RenderTarget* rt, float w, float h, int style, bool live);
	int itemAt(float x, float y) const;
	wchar_t iconFor(const Item& it) const;
	std::string tipFor(const Item& it) const;
	bool isOn(const Item& it) const;
	bool isEnabled(const Item& it) const;
	D2D1_RECT_F speedTrack(const Item& it) const;
	void setSpeedAt(const Item& it, float x);
	void activate(int index);
};

#endif  // CL_WINDOWS_TOOLBAR_H
