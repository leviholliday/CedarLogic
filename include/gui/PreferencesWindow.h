/*****************************************************************************
   Project: CEDAR Logic Simulator
   PreferencesWindow: the app's settings, as a native preferences window
   (icon tabs on macOS, changes apply as they're made; OK/Cancel elsewhere).
*****************************************************************************/

#ifndef PREFERENCESWINDOW_H_
#define PREFERENCESWINDOW_H_

class wxWindow;

// Build the window on first use, then show (or raise) it.
void ShowPreferencesWindow(wxWindow* parent);

// Close it if open -- call before the main frame goes away.
void DismissPreferencesWindow();

// The app's theme changed: repaint the window in the new colours, if open.
void PreferencesThemeChanged();

#ifdef __WXMSW__
// --render-windows: the open Settings window showing page `page`.
wxWindow* PreferencesWindowForCapture(int page);
#endif

#endif
