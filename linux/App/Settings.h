// Settings (Ctrl+,), laid out like the Mac app's (SettingsView.swift): the
// pages as icons in the window's top row -- General, Appearance, Canvas,
// Toolbar, Shortcuts, Sync -- the window easing to each page's height as you
// switch, and each setting a label on the left with its control and a line
// of explanation on the right.

#ifndef CL_LINUX_SETTINGS_H
#define CL_LINUX_SETTINGS_H

#include "App.h"

class CircuitWindow;

namespace settings {
// The Sync page (the last of the six), for show().
constexpr int kSyncPage = 5;
void show(CircuitWindow* from, int page = -1);
GtkWidget* window();
// The page's content changed size (Sync's states): the window follows.
void pageChanged();
// The theme changed: redraw what's drawn.
void themeChanged();
}

#endif  // CL_LINUX_SETTINGS_H
