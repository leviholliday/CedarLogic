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

#endif
