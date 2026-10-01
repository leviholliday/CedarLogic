// The toolbar, drawn as the Mac app's (CLToolbar, its Classic style): in the
// title bar's place, the circuit's name with its menu, the tools in soft
// capsules, the zoom and the simulation speed, ••• for every menu, and the
// window's own minimize, maximize and close buttons at the right.
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

class Toolbar : public Drawn {
public:
	Toolbar(CircuitWindow* window, HWND parent);
	static float barHeight() { return 48; }   // points

	// For the frame: where the maximize button is (window client pixels;
	// empty when hidden), and whether the pointer is over it.
	RECT maximizeRect() const;
	void setMaximizeHot(bool hot, bool pressed);
	void layoutNow() { relayout = true; redraw(); }

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
		int group;             // capsules: items with the same group share one (-1 none)
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

	void build();
	void layout(float w, float h);
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
