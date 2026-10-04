// Every command's keys, changeable (Settings > Shortcuts), as the Mac app's
// ShortcutStore and the Linux app's Shortcuts: the defaults, with the changes
// kept in prefs().shortcuts. The window's key handling (handleShortcut in
// main.cpp) runs the menu commands' keys; the canvas's single keys (A, R, S,
// T, C, V, X, D...) are looked up here by the canvas itself, so they still
// type in a text box. The menus and the toolbar's tips show the keys in use.
//
// Keys are kept as text, "Ctrl+Shift+S": Ctrl and Shift, then a key's name
// (a letter, a digit, F1...F24, Equals, Minus, Comma, Period, Slash,
// PageDown, Num0, NumPlus...). Alt stays the menus', as on Windows.

#ifndef CL_WINDOWS_SHORTCUTS_H
#define CL_WINDOWS_SHORTCUTS_H

#include "App.h"
#include <string>
#include <vector>

namespace shortcuts {

// A key with Ctrl and Shift (or not).
struct Key {
	UINT vk = 0;
	bool ctrl = false, shift = false;
	bool valid() const { return vk != 0; }
	bool operator==(const Key& o) const { return vk == o.vk && ctrl == o.ctrl && shift == o.shift; }
};

struct Action {
	const char* id;
	const char* section;
	const char* name;
	int command;          // the CMD_ it runs (Commands.h)
	bool canvas;          // one of the canvas's keys: works while the canvas has the keyboard
	const char* keys;     // the defaults, separated by '|' ("" none)
};

const std::vector<Action>& all();
const Action* find(const std::string& id);
// The action a menu item or a button stands for: the menu's own before the
// canvas's (Copy is Ctrl+C there, not C).
const Action* forCommand(int command);

// The keys an action has now, the one to show first.
std::vector<Key> keys(const Action& a);
Key first(const Action& a);
bool isCustom(const Action& a);
bool anyCustom();
// Give an action this key (an empty Key: none). Another action that had it
// loses just that key (and keeps any others): its id comes back, or "".
std::string set(const Action& a, const Key& key);
void reset(const Action& a);
void resetAll();

// The key being pressed now (WM_KEYDOWN's vk, with Ctrl and Shift as they
// are). An empty Key for a lone Ctrl, Shift, Alt or Windows key.
Key pressed(UINT vk);
// The action that has this key: a menu command's, or (`canvas`) the canvas's.
const Action* match(const Key& k, bool canvas);
// What a key is kept for, when it's one of the fixed keys (Space, the
// arrows, Shift+1...0, Ctrl+Tab, Delete...) that no command can have; else "".
std::string reserved(const Key& k);

// "Ctrl+Shift+S" as the menus show it, in the keyboard's own characters
// ("Ctrl+=" on a US keyboard); and as key caps ("Ctrl", "Shift", "S").
std::string label(const Key& k);
std::vector<std::string> caps(const Key& k);
// " (Ctrl+N)" for a tooltip, "\tCtrl+N" for a menu: a command's keys now, or "".
std::string tipKeys(int command);
std::string menuKeys(int command);
// Every item of a menu (and its submenus) labelled with its command's keys.
void relabel(HMENU menu);

// The keys as text, and back ("" and an empty Key for none or nonsense).
std::string text(const Key& k);
Key parse(const std::string& text);

// Keys drawn as key caps, ending at `right` (points), as the Mac's
// KeyCap: "drag", "click a pin" and the like are words, not caps.
float capsWidth(const std::vector<std::string>& caps);
void drawCaps(ID2D1RenderTarget* rt, const std::vector<std::string>& caps, float right, float midY, const D2D1_COLOR_F& ink);

}  // namespace shortcuts

#endif  // CL_WINDOWS_SHORTCUTS_H
