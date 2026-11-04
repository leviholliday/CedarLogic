// The actions the Actions panel ("the rail" in the code) and the Customize
// list offer (the Mac's Actions.swift ActionRegistry): every
// command the menus have, by the id the Mac saves it under, with its name as
// its menu has it, its menu, its icon and the command it runs -- and the
// order and ticks the person chose (prefs().actionsOrder / actionsShown),
// shared by the rail's own drag and the Customize list's. The rail itself is
// Rail.h; what it watches for (the edge's hot strip, the pointer's dwell and
// leaving) is here, apart from any window, so --rail-check can try it.

#ifndef CL_WINDOWS_ACTIONS_H
#define CL_WINDOWS_ACTIONS_H

#include "App.h"
#include <string>
#include <vector>

class CircuitWindow;

namespace actions {

struct Def {
	const char* id;      // as the Mac saves it (cl.actions.order)
	const char* title;   // as its menu says it
	const char* menu;    // the menu it's in (the Customize list says it)
	wchar_t glyph;       // Segoe Fluent / MDL2 (0: drawn, as the toolbar's New, Open and New Tab are)
	int command;         // the CMD_ it runs (Commands.h)
};

// In menu order: File, Edit, Circuit, View, Simulate, Window, Help.
const std::vector<Def>& all();
const Def* find(const std::string& id);
const Def* forCommand(int command);
// What the rail starts with, and Reset to Default goes back to (the Mac's six).
const std::vector<std::string>& defaults();

// The title without its "…" (the rail's label, a tooltip).
std::string plainTitle(const Def& d);
// Its shortcut as Settings > Shortcuts has it now ("Shift+T"), or "".
std::string keyLabel(const Def& d);
// "Check My Circuit (Shift+T)".
std::string titleWithKey(const Def& d);

// The Customize list: every action, in order (the saved order, then any it
// doesn't know yet). The rail (or the toolbar): the ticked ones, in that order.
std::vector<std::string> listed();
std::vector<std::string> rail();
bool shown(const std::string& id);
void setShown(const std::string& id, bool on);
// A drag in the Customize list: the row at `from` (of listed()) to `to`.
void moveListed(int from, int to);
// A drag on the rail: `id` to place `index` among rail()'s items, the rest
// of the order as it was, so the Customize list shows the same order.
void moveRail(const std::string& id, int index);
void reset();
bool isDefault();

// The words a search looks in: the name, the menu and the shortcut, as it
// reads and spelled out ("ctrl control shift t"). Every word typed has to be
// in them; one of a single letter or digit (a key) has to be a word of its own.
std::string words(const Def& d);
bool matches(const Def& d, const std::string& query);

// Saved, and every window shows it (its panel, its menus).
void changed();

// The panel's two switches as Settings > Actions, the Customize sheet, the
// welcome and What's New word them (the Mac's ActionsSwitches), and their
// hints: on, off (with View > Show Actions' keys), open on hover, and the
// line under Settings' list about folding it.
extern const char* const kShowSwitch;
extern const char* const kHoverSwitch;
std::string showHint();
std::string offHint();
std::string hoverHint();
std::string foldHint();

// In `w` now: whether it can run (dimmed if not), and whether it's switched
// on (drawn lit, as the toolbar's toggles are).
bool enabled(const CircuitWindow* w, const Def& d);
bool isOn(const CircuitWindow* w, const Def& d);

}  // namespace actions

namespace rail {

// In points, as the Mac's ActionsRail.
const float kWidth = 46;         // open
const float kTabWidth = 6;       // folded
const float kPeekWidth = 10;     // folded, the pointer at the edge: lit and a little wider, over the canvas
const float kHotWidth = 12;      // folded, the strip along the window's right edge that slides it out
const float kNarrowWidth = 900;  // under this window width it folds by itself
const float kPitch = 36;         // an item and the gap under it
const float kItemTop = 8;
const float kDragStart = 6;      // moved this far with the button down, a press is a drag...
const double kHoldTime = 0.45;   // ...or held this long (then any movement drags)
const double kOpenDelay = 0.1;   // in the strip this long slides it out (passing through doesn't)...
const double kCloseDelay = 0.4;  // ...and it goes this long after the pointer has left the strip and the rail
const double kSlideTime = 0.18;

// The hot strip, in the window's client pixels: kHotWidth points wide along
// the right edge, from `top` to `bottom` (the canvas's height), its right
// edge counting too -- the last column, and the one past it, where the
// pointer stops when it's pushed against the screen's edge.
bool inHotStrip(const RECT& client, int top, int bottom, POINT p, double scale);

// Folded, what the pointer is doing at the edge: in the strip it peeks at
// once (lit); after kOpenDelay there the rail slides out over the canvas; it
// stays while the pointer is over it or the strip, and goes kCloseDelay after
// it has left both -- but not while its right-click menu is open or a drag
// is going on. Escape puts it back, and it doesn't come out again until the
// pointer has left. The Mac's ActionsRail hoverChanged, without a window.
struct Hover {
	bool overStrip = false, overRail = false;
	bool slidOut = false;
	bool dismissed = false;   // Escape put it back: in until the pointer has left
	bool menuOpen = false, dragging = false;
	int pending = 0;          // 1 to slide out, -1 to go back, at `due`
	double due = 0;

	// The pointer now (in the strip; over the slid-out rail), and whether
	// the rail is folded with hover on. True when slidOut changed.
	bool update(double now, bool strip, bool over, bool folded, bool hoverOn);
	// Something changed that hoverChanged hears (a menu closed, a drag ended).
	void changed(double now, bool folded, bool hoverOn);
	void slide(bool out);
	bool lit() const { return overStrip; }
};

}  // namespace rail

#endif  // CL_WINDOWS_ACTIONS_H
