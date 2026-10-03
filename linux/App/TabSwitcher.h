// Ctrl+Tab, as the Mac app's TabSwitcher: tabs in the order you last used
// them, the one you're on first. A quick Ctrl+Tab flips to the previous tab;
// held for a moment, a panel of the tabs shows (a picture of each), Tab and
// Shift+Tab move through it, a click picks, and letting go of Ctrl switches.
// Escape changes nothing.

#ifndef CL_LINUX_TABSWITCHER_H
#define CL_LINUX_TABSWITCHER_H

#include "Chrome.h"
#include <vector>

class CircuitWindow;

class TabSwitcher {
public:
	explicit TabSwitcher(CircuitWindow* window) : win(window) {}
	~TabSwitcher();
	// Ctrl+Tab (or with Shift). False when there's nothing to switch between.
	bool key(bool backwards);
	// Ctrl let go: switch to the one chosen.
	void release() { commit(selected); }
	void cancel();
	bool active() const { return isActive; }

private:
	CircuitWindow* win;
	bool isActive = false;
	std::vector<int> tabs;   // tab indexes, most recent first
	int selected = 0;
	GtkWidget* panel = nullptr;
	GtkWidget* area = nullptr;
	guint showTimer = 0;
	std::vector<RectF> cards;

	void commit(int index);
	void show();
	void hide();
	void paint(cairo_t* cr);
	static gboolean drawCb(GtkWidget*, cairo_t*, gpointer);
	static gboolean pressCb(GtkWidget*, GdkEventButton*, gpointer);
	static gboolean motionCb(GtkWidget*, GdkEventMotion*, gpointer);
};

#endif  // CL_LINUX_TABSWITCHER_H
