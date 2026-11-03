// `CedarLogic.exe --rail-check`: the Actions rail's checks, the Windows twin
// of the Mac launch-selftest's `actions` state. In a home folder of its own
// (CEDARLOGIC_HOME), so your settings are never touched: a new install gets
// the rail on screen and an earlier one Hidden with the status line once;
// the rail's drag and the Customize list keep one order; the search finds
// actions by name, menu and shortcut; the edge's hot strip is where it should
// be (the last column of pixels too), and the pointer's dwell, pass-through,
// leaving, its menu, a drag and Escape do what they should; and in a real
// window: the rail's width, folding, focus mode folding it (not hiding it)
// and giving it back as it was, a narrow window, sliding out over the canvas
// without moving it, Show/Hide Actions, the toolbar and hidden modes, a click
// and a drag on the rail, Predict from the rail and its shortcut. Then the
// intro's rules (Intro.h; the Mac launch-selftest's intro checks): its
// length, which launch plays which intro and with the sound or not, "Play
// the intro every time CedarLogic opens" off by default, and Help > Play the
// Intro in the actions and the menu bar, played over a window. One PASS or
// FAIL line each; the exit code is 0 when all passed.

#ifndef CL_WINDOWS_RAILCHECK_H
#define CL_WINDOWS_RAILCHECK_H

#include <functional>
#include <string>

namespace railcheck {
// First thing, before the settings are read: the home folder of its own.
void prepare();
// With the gate library loaded and the window classes registered.
int run(const std::function<void(const std::string&)>& out);
}  // namespace railcheck

#endif  // CL_WINDOWS_RAILCHECK_H
