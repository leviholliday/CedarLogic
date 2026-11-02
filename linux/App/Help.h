// Help (F1), as the Mac app's (Help.swift): pages on how this app works,
// written for it and its Linux keys, with one search over them all; the
// shortcut list; and Classic Help -- the original CedarLogic Help, opened in
// the browser as it always was.

#ifndef CL_LINUX_HELP_H
#define CL_LINUX_HELP_H

#include "App.h"
#include <string>

class CircuitWindow;

namespace help {
// Opens Help (or brings it forward) on a page by id ("" for the first).
void show(CircuitWindow* from, const std::string& page = "");
// The Help window, if it's open (for --screenshot).
GtkWidget* window();
}

#endif  // CL_LINUX_HELP_H
