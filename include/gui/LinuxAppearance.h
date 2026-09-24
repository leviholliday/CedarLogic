/*****************************************************************************
   Project: CEDAR Logic Simulator
   LinuxAppearance: GTK-specific window behaviour -- matching GTK's own widgets
   to the app's light/dark theme, and knowing when a window is really up.
*****************************************************************************/

#ifndef LINUXAPPEARANCE_H_
#define LINUXAPPEARANCE_H_

#ifdef __WXGTK__

class wxWindow;

// Asks GTK for the dark (or light) variant of the current theme. The canvas and
// panels are drawn by us and follow the theme on their own; menus, dialogs,
// scrollbars and the title bar are GTK's, and without this they stay light over
// a dark app. A theme with no dark variant ignores it.
void GtkSetPreferDarkTheme(bool dark);

// Whether the window manager has actually put this top-level window on screen.
// wxGTK defers a frame's real show until the window manager reports its border
// sizes, so for a moment after Show(true) the frame counts as shown while it is
// not mapped yet; a dialog opened in that gap ends up underneath it.
bool GtkIsMappedOnScreen(wxWindow* window);

#endif

#endif
