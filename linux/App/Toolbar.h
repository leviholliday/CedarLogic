// The toolbar, drawn as the Mac app's (CLToolbar, its Classic style) and the
// Windows app's: the window's title bar, with the circuit's name and its
// menu, the tools in soft capsules, the zoom and the simulation speed, •••
// for every menu, and the window's buttons where the desktop puts them (its
// gtk-decoration-layout: GNOME's lone close on the right, Ubuntu's three,
// a left-handed layout on the left).
//
// Where nothing is under the pointer, the bar is the title bar: it drags the
// window, double-clicks maximize, and right-clicks bring up the window menu.

#ifndef CL_LINUX_TOOLBAR_H
#define CL_LINUX_TOOLBAR_H

#include "Drawn.h"
#include <string>
#include <vector>

class CircuitWindow;

class Toolbar : public Drawn {
public:
	explicit Toolbar(CircuitWindow* window);
	static float barHeight() { return 48; }   // points
	void layoutNow() { relayout = true; redraw(); }
	// Where an action's button is, on the screen (the guided tour points at it).
	GdkRectangle actionRect(const char* action) const;

protected:
	void paint(cairo_t* cr, float w, float h) override;
	void mouseMove(float x, float y) override;
	void mouseLeave() override;
	void mouseDown(int button, float x, float y, bool doubleClick, GdkEventButton* e) override;
	void mouseUp(int button, float x, float y) override;
	void sizeChanged() override { relayout = true; }

private:
	enum Kind { Button, Zoom, Speed, Title, More, WinMin, WinMax, WinClose };
	struct Item {
		Kind kind;
		const char* action;    // for a Button: "app.new", "win.save"...
		int group;             // capsules: items with the same group share one (-1 none)
		RectF rect{};
		bool shown = true;
		bool leftSide = false; // window buttons: on the left
	};

	CircuitWindow* win;
	std::vector<Item> items;
	int hot = -1, pressed = -1;
	bool draggingSpeed = false;
	bool relayout = true;
	float lastW = 0;

	void build();
	void layout(float w, float h);
	int itemAt(float x, float y) const;
	const char* iconFor(const Item& it) const;
	std::string tipFor(const Item& it) const;
	bool isOn(const Item& it) const;
	bool isEnabled(const Item& it) const;
	RectF speedTrack(const Item& it) const;
	void setSpeedAt(const Item& it, float x);
	void activate(int index);
	GtkWindow* topWindow() const;
};

#endif  // CL_LINUX_TOOLBAR_H
