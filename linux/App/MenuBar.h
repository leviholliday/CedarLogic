// The menu row: the window's top row, above the toolbar. A GtkMenuBar with
// File, Edit, View, Simulation, Window and Help (main.cpp's model, the
// Mac's menus in its order) on the toolbar's own colour, and the window's
// buttons where the desktop puts them. Its empty part is the title bar: it
// drags the window, double-clicks maximize and right-clicks bring up the
// window menu. Alt and a letter (Alt+F...) or F10 open the menus; focus
// mode slides it away with the toolbar.

#ifndef CL_LINUX_MENUBAR_H
#define CL_LINUX_MENUBAR_H

#include "Drawn.h"
#include "TitleButtons.h"

class CircuitWindow;

class MenuRow : public Drawn {
public:
	explicit MenuRow(CircuitWindow* window);
	static float rowHeight() { return 32; }   // points
	// What goes in the window: the row and the bar over it.
	GtkWidget* outer() const { return overlay; }
	GtkWidget* bar() const { return menubar; }
	// Shown and on screen (not in focus mode).
	bool showing() const;
	// Colours again: the theme, the toolbar's style, Simulation View.
	void refresh();
	// One of the menus open under its title (0 File ... 5 Help), as Alt and
	// its letter would; false when there's no such menu.
	bool openMenu(int index);

protected:
	void paint(cairo_t* cr, float w, float h) override;
	void mouseMove(float x, float y) override;
	void mouseLeave() override;
	void mouseDown(int button, float x, float y, bool doubleClick, GdkEventButton* e) override;
	void mouseUp(int button, float x, float y) override;
	void sizeChanged() override;
	bool animating() override { return buttons.animating(); }

private:
	CircuitWindow* win;
	GtkWidget* overlay = nullptr;
	GtkWidget* menubar = nullptr;
	TitleButtons buttons;
	bool onDark = false;
};

#endif  // CL_LINUX_MENUBAR_H
