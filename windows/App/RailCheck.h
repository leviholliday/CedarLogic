// `CedarLogic.exe --rail-check`: the Actions rail's checks, the Windows twin
// of the Mac launch-selftest's `actions` state. In a home folder of its own
// (CEDARLOGIC_HOME), so your settings are never touched: a new install gets
// the panel on, and 0.4's one-time migrations turn it on and open for every
// earlier setting (from before the panel, Hidden, In the toolbar, folded),
// the choice sticking after;
// the rail's drag and the Customize list keep one order; the search finds
// actions by name, menu and shortcut; the edge's hot strip is where it should
// be (the last column of pixels too), and the pointer's dwell, pass-through,
// leaving, its menu, a drag and Escape do what they should; and in a real
// window: the rail's width, folding, focus mode folding it (not hiding it)
// and giving it back as it was, a narrow window, sliding out over the canvas
// without moving it, Show/Hide Actions, the panel off (and no toolbar mode:
// no actions in any toolbar style), a click and a drag on the rail, Predict
// from the rail and its shortcut. Show Me Around (ShowMeAround.h): its
// stops and hole in a real window, the keys, the panel's stop opening a
// folded panel and folding it back, Escape, the panel's stop passed over
// when it's off, starting by itself on a launch, after each of the welcome's
// ways out (the lesson after it when chosen) and after What's New -- never
// over them, never again once seen; About's mailto link; the wheel over a
// Settings choice leaving its value. Then the
// intro's rules (Intro.h; the Mac launch-selftest's intro checks): its
// length, which launch plays which intro and with the sound or not, "Play
// the intro every time CedarLogic opens" on by default (off once turned off), and Help > Play the
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
