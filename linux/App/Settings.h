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
// The pages' parts, shared with Settings > Actions and Sync: a setting's name
// in the label column (right-aligned, the column's 150 points, wrapping
// rather than widening it), and a line of help under its control (small,
// solid grey, wrapping at whatever width the page gives it).
// A list on a page that should take what height the screen leaves (Shortcuts,
// Actions): its scrolled window carries this, with its tallest as an int.
constexpr const char* kFitList = "cl-fit-list";
void formLabel(GtkWidget* label);
void hintStyle(GtkWidget* label);
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
// --menu-check <folder> <tag>: every menu's text read off the screen as GTK
// draws it, as it is and lit (the pointer on it): the menu bar's titles, each
// menu and its submenus, the canvas's and a part's right-click menus, a
// menu of every kind of row, a dropdown's list, and a menu as a popover; at
// 4.5:1 or better. Pictures of the open menus go in <folder>. 0 if all passed.
int menuCheck(CircuitWindow* w, const std::string& dir, const std::string& tag);
}

#endif  // CL_LINUX_SETTINGS_H
