// The window's own buttons (minimize, maximize, close), drawn where the
// desktop puts them -- its gtk-decoration-layout: GNOME's lone close on the
// right, Ubuntu's three, a left-handed layout on the left. Whatever is the
// window's top row draws them: the toolbar, or in focus mode the tab strip.

#ifndef CL_LINUX_TITLEBUTTONS_H
#define CL_LINUX_TITLEBUTTONS_H

#include "Anim.h"
#include "Chrome.h"
#include <vector>

struct TitleButtons {
	enum Kind { Min, Max, Close };
	struct Button { Kind kind; bool left; RectF rect; };
	std::vector<Button> list;
	anim::HoverFade fade;
	int pressed = -1;
	static constexpr float kWidth = 34;

	TitleButtons() { load(); }
	// Read the desktop's layout again.
	void load();
	// Room they take on each side, with their margins.
	float leftRoom() const;
	float rightRoom() const;
	// Place them in a bar w x h points.
	void layout(float w, float h);
	int at(float x, float y) const;
	void setHot(int i) { fade.setHot(i); }
	bool animating() const { return fade.active(); }
	void paint(cairo_t* cr, GtkWindow* window, const Color& ink) const;
	void activate(int i, GtkWindow* window) const;
};

// The empty part of a window's top row: drag the window, double-click
// (maximize, unless the caller handles it), right-click the window menu.
void titleRowPress(GtkWindow* window, GdkEventButton* e, bool doubleClickMaximizes = true);

#endif  // CL_LINUX_TITLEBUTTONS_H
