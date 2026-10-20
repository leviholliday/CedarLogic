// --session-check [actions] [seed]: a long working session in a real circuit
// window, on its own (CI runs it under Xvfb). The Mac app's
// `--session-check` (mac/App/SessionCheck.swift), for this app.
//
// The window is the app's own, with its own settings and Your Circuits in a
// temporary folder (as --bench-drag runs). Seeded random actions arrive as a
// person makes them: pointer presses, moves and releases on the canvas and
// the tab strips, keys to the window (so the menus' shortcuts and the
// canvas's keys go the way they do from a keyboard), and the window's own
// commands. Windows that open over it (a question, the truth table, a
// gate's settings, Settings, a menu) are poked at and closed as a person
// would: a watchdog sends them clicks and keys, then Escape.
//
// Checked all along, PASS or FAIL each at the end (exit 1 on a failure):
// no crash (a signal prints the action it came in and a backtrace), no GTK
// or GLib warning or critical, an edit leaves the window unsaved until
// autosave saves it, at each pause the file on disk is the circuit as it
// is and opens again to the same circuit, undo then redo brings the circuit
// back, the history stays within the engine's 500 steps, every tab is a page
// that exists on the side it should be, windows over it close when asked, a
// setting changed in Settings is saved, memory levels off, and closing the
// window frees it, its canvases, its circuit, its clock and its timers. The
// circuit saved last opens again in a new window to the same circuit.

#ifndef CL_LINUX_SESSIONCHECK_H
#define CL_LINUX_SESSIONCHECK_H

#include <gtk/gtk.h>

namespace session {

// Before GTK starts: the crash handler and the warning counter.
void prepare();
// Once the first window is up: the session, then every window closed.
// Returns the exit code.
int run(GtkApplication* app, int actions, unsigned long long seed);

}  // namespace session

#endif  // CL_LINUX_SESSIONCHECK_H
