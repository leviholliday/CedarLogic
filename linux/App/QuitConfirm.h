// Ctrl+Q asks first, as the Mac app does (QuitConfirm.swift): a card in the
// middle of the screen in the app's own look -- Enter quits, Escape (or
// Cancel) stays, "Always Quit" turns the question off (Settings > General).

#ifndef CL_LINUX_QUITCONFIRM_H
#define CL_LINUX_QUITCONFIRM_H

#include "App.h"

// True to quit (asked, or not asked because Settings says not to).
bool confirmQuitting(GtkWindow* parent);

#endif  // CL_LINUX_QUITCONFIRM_H
