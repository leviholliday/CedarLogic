// The Actions rail's list (the Mac's mac/App/Actions.swift): every command
// the menus offer, each with its name as in its menu, the menu it's in, an
// icon, its window action ("win.save", "app.new") and its shortcut in
// Settings > Shortcuts. The rail on the window's right edge (Rail.h), its
// toolbar version (Toolbar.cpp) and the Customize list (in Settings >
// Actions, and the rail's Customize Actions... window) all work from it.
//
// Also here, without a window so --actions-check can try them: the order
// and the choice (kept in prefs().actionsOrder / actionsShown, shared by the
// list and a drag on the rail), the search, and the folded rail's hot strip
// and hover timing (EdgeHover).

#ifndef CL_LINUX_ACTIONS_H
#define CL_LINUX_ACTIONS_H

#include "Chrome.h"
#include <functional>
#include <string>
#include <vector>

class CircuitWindow;

namespace actions {

// Where the actions go (Settings > Actions; prefs().actionsMode).
enum Mode { OnScreen = 0, InToolbar = 1, Hidden = 2 };
const char* modeName(int mode);

// Can't run outside the editing view (dimmed in Simulation View and when locked).
enum { NeedsEdit = 1 };

struct Item {
	const char* id;         // saved under this (the Mac's ActionID)
	const char* title;      // as in its menu
	const char* menu;       // File, Edit, View, Simulation, Window, Help
	const char* icon;       // a symbolic icon, or one Chrome.h draws (Icon::)
	const char* action;     // what it runs: "win.save", "app.new"
	const char* shortcut;   // its id in Settings > Shortcuts, or nullptr
	int flags;
};

// Every action, in menu order.
const std::vector<Item>& all();
const Item* find(const std::string& id);
const Item* byAction(const char* action);
// What the rail starts with, and Reset to Default goes back to: Check My
// Circuit, Predict, Tidy Up, Share Link, Export as Image, Lab Report.
const std::vector<std::string>& defaults();

// ---- The order and the choice --------------------------------------------------------

// A saved order with every action it doesn't name after it (new ones; all of
// them, the defaults first, when it's empty), and names it doesn't know left out.
std::vector<std::string> completeOrder(const std::vector<std::string>& saved);
// A drag on the rail: `id` to `index` among the shown ones (`rail`, top to
// bottom), the rest of `order` kept as it is, so the Customize list has the
// same order. Returns the new order.
std::vector<std::string> moveOnRail(const std::vector<std::string>& order, const std::vector<std::string>& rail, const std::string& id,
                                    int index);
// A drag in the Customize list: the one at `from` to `to` (indices in the order).
std::vector<std::string> moveInList(const std::vector<std::string>& order, int from, int to);

// On prefs(): every action in the Customize list's order; the shown ones.
std::vector<const Item*> order();
std::vector<const Item*> rail();
bool shown(const Item& a);
void setShown(const Item& a, bool on);
void moveListItem(int from, int to);
void moveRailItem(const std::string& id, int index);
void reset();
bool isDefault();

// ---- Search ----------------------------------------------------------------------------
// The words a search looks in: the name, the menu, and the shortcut as it
// reads ("Ctrl+Shift+T") and spelled out ("control ctrl shift t").
std::string words(const Item& a);
// Every word typed is somewhere in the action's words (any case).
bool matches(const Item& a, const std::string& query);

// ---- Running ---------------------------------------------------------------------------
std::string keyLabel(const Item& a);       // "Shift+T", or ""
std::string plainTitle(const Item& a);     // without the "..."
std::string titleWithKey(const Item& a);   // "Check My Circuit (Shift+T)"
bool enabled(const Item& a, const CircuitWindow* w);
bool isOn(const Item& a, const CircuitWindow* w);
void run(const Item& a, CircuitWindow* w);
// Its icon, `size` points, centred in `box`; a letter in a rounded square
// when the desktop's icon set hasn't it.
void drawIcon(cairo_t* cr, const Item& a, const RectF& box, float size, const Color& color);
bool iconMissing(const Item& a);

// The order, the choice or the mode changed: saved, and every window's rail,
// toolbar and menus, and any open Customize list, follow.
void changed();
// Told of every change (the Customize lists); the number takes it away.
int listen(std::function<void()> f);
void unlisten(int id);

// ---- The folded rail's edge ------------------------------------------------------------
// Sizes in points, as the Mac's ActionsRail.
const float kRailWidth = 46, kTabWidth = 6, kPeekWidth = 10, kHotWidth = 12;
const float kPitch = 36, kItemW = 34, kItemH = 32, kTop = 8;
const float kDragStart = 6;
const double kHoldTime = 0.45, kOpenDelay = 0.1, kCloseDelay = 0.4;
// Under this window width the rail folds by itself.
const float kNarrowWidth = 900;
// Past the right edge, the window's resize border still counts.
const float kEdgeSlop = 8;

// The hot strip: along the window's right edge, the whole height of the
// area beside it (w by h, the pointer at x, y in it), kHotWidth wide and
// taking in the last column of pixels (and the window's resize border past
// it), so a pointer pushed against the screen's edge always finds it.
bool inHotStrip(double x, double y, double w, double h);
// The slid-out rail, for keeping it out: its kRailWidth at the right edge.
bool onSlidRail(double x, double y, double w, double h);

// When the folded rail slides out and back (the Mac's hoverChanged): in the
// strip kOpenDelay slides it out (a pointer passing through doesn't), it
// stays while the pointer is on it or the strip, and goes kCloseDelay after
// it leaves both, but not while its menu is open or a drag is under way.
// Escape puts it back, and it stays back until the pointer has left.
// Times are seconds, handed in, so the check can run it.
struct EdgeHover {
	bool overStrip = false, overRail = false;
	bool slidOut = false, menuOpen = false, dragging = false, dismissed = false;
	int pending = 0;     // 1 slide out, -1 back, 0 nothing
	double due = 0;
	// `folded`: the rail is folded (else none of this applies); `enabled`:
	// Settings > Actions' "Open it when the mouse reaches the right edge".
	void changed(double now, bool folded, bool enabled);
	void setStrip(bool in, double now, bool folded, bool enabled);
	void setRail(bool in, double now, bool folded, bool enabled);
	// Due now: 1 it slid out, -1 it went back, 0 nothing happened.
	int tick(double now, bool folded, bool enabled);
	// Escape: back in (true), when it's out and nothing holds it.
	bool escape();
	// Unfolded, or the setting turned off: back in, nothing waiting.
	void close();
	// The tab lit and wider: the pointer is in the strip and the rail isn't out.
	bool peek() const { return overStrip && !slidOut; }
};

// Folded or open, for one window (the Mac's ActionsRail.folded and the
// window's peek): the saved state (prefs().actionsCollapsed), a narrow
// window and focus mode fold it; a click on the tab there opens it only
// for now (`peek`), so leaving focus mode, or widening the window, gives
// back the rail as it was.
struct FoldState {
	bool narrow = false, focus = false, peek = false, peekBeforeFocus = false;
	bool folded(bool collapsed) const { return (collapsed || narrow || focus) && !peek; }
	void setFocus(bool on);
	void setNarrow(bool on);
	// A click on the tab (or Show Actions); on the edge (or Hide Actions).
	void unfold(bool& collapsed);
	void fold(bool& collapsed);
};

// ---- The Customize list and Settings > Actions ---------------------------------------------
// Settings > Actions: where they go, the edge setting, and the list.
GtkWidget* settingsPage();
// The rail's Customize Actions... window, over `w` (with `query` typed in its search).
void showCustomize(CircuitWindow* w, const std::string& query = "");
GtkWidget* customizeWindow();

// --actions-check: the order, the search, the start, the edge's geometry
// and timing, without a window. Returns the exit code.
int check();

}  // namespace actions

#endif  // CL_LINUX_ACTIONS_H
