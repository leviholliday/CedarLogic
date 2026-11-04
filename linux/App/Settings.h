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
// The Actions page, and the Sync page (the last of the seven), for show().
constexpr int kActionsPage = 4;
constexpr int kSyncPage = 6;
void show(CircuitWindow* from, int page = -1);
GtkWidget* window();
// The page's content changed size (Sync's states): the window follows.
void pageChanged();
// The theme changed: redraw what's drawn.
void themeChanged();

// For --settings-check (SettingsCheck.cpp): the pages, one shown at once
// without its fade, its scroller and its content.
int pageCount();
const char* pageTitle(int page);
void selectNow(int page);
GtkWidget* pageScroller();
GtkWidget* currentPage();
// --settings-check <folder> [tag]: every page and Customize Actions on this
// screen, pictured into <folder> as <tag>-settings-N.png and checked (they
// fit, read at 4.5:1, and the wheel never changes a setting). 0 if all passed.
int check(CircuitWindow* w, const std::string& dir, const std::string& tag);
}

#endif  // CL_LINUX_SETTINGS_H
