// Every command's keys, changeable (Settings > Shortcuts), as the Mac app's
// ShortcutStore: the defaults, with the changes kept in prefs().shortcuts.
// Menu commands are GTK accelerators; the canvas's single keys (A, R, S,
// T, C, V, X, D...) are looked up here by the canvas itself.

#ifndef CL_LINUX_SHORTCUTS_H
#define CL_LINUX_SHORTCUTS_H

#include "App.h"
#include <string>
#include <vector>

namespace shortcuts {

struct Action {
	const char* id;
	const char* section;
	const char* name;
	const char* gaction;   // "win.save"; nullptr for a canvas key
	const char* keys;      // the defaults, GTK accelerators separated by '|' ("" none)
};

const std::vector<Action>& all();
const Action* find(const std::string& id);
// The keys an action has now (the first, for showing), or "" for none.
std::string keys(const Action& a);
bool isCustom(const Action& a);
// Change one ("" for none). Another action that had those keys loses them:
// its id comes back, or "".
std::string set(const Action& a, const std::string& accel);
void reset(const Action& a);
void resetAll();
bool anyCustom();
// Every menu command's keys, given to GTK.
void apply(GtkApplication* app);
// The canvas key a key press is, or "".
std::string canvasAction(const GdkEventKey* e);
// "Ctrl+Shift+S", and the same as key caps.
std::string label(const std::string& accel);
std::vector<std::string> caps(const std::string& accel);
// What a key press would be as an accelerator ("" for a lone modifier).
std::string fromEvent(const GdkEventKey* e);

}  // namespace shortcuts

#endif  // CL_LINUX_SHORTCUTS_H
