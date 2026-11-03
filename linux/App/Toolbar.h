// The toolbar, drawn as the Mac app's (CLToolbar) in its three styles:
// Seamless (the default: one surface with the canvas, tools quiet until you
// point at them), Classic (tools in soft capsules on their own bar) and
// Minimal (the essentials and the circuit's name; the rest is in the menus).
// It's part of the window's title bar, under the menu row (MenuBar.h): the
// circuit's name and its menu, the tools, the zoom and the simulation speed.
// Settings > Toolbar hides groups; every tool is in the menus too.
//
// Where nothing is under the pointer, the bar is the title bar: it drags the
// window, double-clicks maximize, and right-clicks bring up the window menu.
// (The window's own buttons are the menu row's; the toolbar draws them only
// when it's the top row itself, setWindowButtons.)

#ifndef CL_LINUX_TOOLBAR_H
#define CL_LINUX_TOOLBAR_H

#include "Anim.h"
#include "Drawn.h"
#include "TitleButtons.h"
#include <string>
#include <vector>

class CircuitWindow;

// Tool groups that can be hidden, one bit each (the Mac's ToolGroup).
// (TGDraw is last so the bits kept in Settings stay the same.)
enum ToolGroup { TGFile, TGUndo, TGClipboard, TGZoom, TGSim, TGRun, TGLock, TGTheme, TGTab, TGFeedback, TGDraw, TGCount };
const char* toolGroupName(int g);

class Toolbar : public Drawn {
public:
	explicit Toolbar(CircuitWindow* window);
	static float barHeight() { return 48; }   // points
	void layoutNow() { relayout = true; redraw(); }
	// Where an action's button is, on the screen (the guided tour points at it).
	GdkRectangle actionRect(const char* action) const;
	// The bar's colour as drawn now, and whether its ink is the dark theme's
	// (the menu row above it matches).
	Color barColor() const;
	bool barIsDark() const;
	// Draw the window's buttons (minimize, maximize, close) at its ends.
	void setWindowButtons(bool on);
	// For Settings' pictures of each style: drawn in `style`, not clickable.
	void paintPicture(cairo_t* cr, float w, float h, int style);

protected:
	void paint(cairo_t* cr, float w, float h) override;
	void mouseMove(float x, float y) override;
	void mouseLeave() override;
	void mouseDown(int button, float x, float y, bool doubleClick, GdkEventButton* e) override;
	void mouseUp(int button, float x, float y) override;
	void sizeChanged() override {
		relayout = true;
		buttons.load();
		if (!ownButtons) buttons.list.clear();
	}
	bool animating() override { return fade.active() || buttons.animating(); }

private:
	enum Kind { Button, Zoom, Speed, Title };
	struct Item {
		Kind kind;
		const char* action;    // for a Button: "app.new", "win.save"...
		int group;             // ToolGroup (-1: always there)
		bool right;            // laid out from the right edge
		RectF rect{};
		bool shown = true;
	};

	CircuitWindow* win;
	std::vector<Item> items;
	TitleButtons buttons;
	bool ownButtons = true;
	anim::HoverFade fade;
	int hot = -1, pressed = -1;
	bool draggingSpeed = false;
	bool relayout = true;
	float lastW = 0;
	int laidStyle = -1;

	void build(int style);
	void layout(float w, float h, int style, bool live = true);
	void draw(cairo_t* cr, float w, float h, int style, bool live);
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
