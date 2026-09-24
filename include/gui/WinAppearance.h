/*****************************************************************************
   Project: CEDAR Logic Simulator
   WinAppearance: match the Windows title bar to the app's light/dark theme.
*****************************************************************************/

#ifndef WINAPPEARANCE_H_
#define WINAPPEARANCE_H_

#ifdef _WIN32

class wxTopLevelWindow;
class wxWindow;

// Dark or light caption for one window. The canvas and panels are drawn by us
// and follow the theme on their own; the caption is drawn by Windows, which
// otherwise leaves a white bar above a dark app. Windows 10 before 1809 has no
// dark caption, and there this does nothing.
void WinSetDarkTitlebar(wxTopLevelWindow* window, bool dark);

// The same for every top-level window that is open right now.
void WinSetDarkTitlebars(bool dark);

// Windows 10 1809+ has a dark mode for its own controls that wxWidgets 3.2
// never turns on: scrollbars, right-click menus and dropdowns stay white in a
// dark app. This switches the app's menus over, and themes the scrollbars and
// dropdowns under `root` to match. Light undoes it.
void WinSetAppDarkMode(bool dark);
void WinThemeControls(wxWindow* root, bool dark);

// Paint the title bar `bar` with `text` on it (Windows 11), so it reads as
// one surface with whatever sits under it. Windows 10 keeps its own colours.
void WinSetCaptionColour(wxTopLevelWindow* window, const wxColour& bar, const wxColour& text);

// The window as it looks on screen, children and all, saved as a PNG. For
// --render-windows, which is how a Windows build gets looked at from a Mac.
bool WinCaptureWindow(wxWindow* window, const wxString& pngPath);

// Round a borderless popup's corners the Windows 11 way: antialiased, with
// the system's thin border and shadow. Does nothing on Windows 10.
void WinRoundCorners(wxTopLevelWindow* window);

#endif

#endif
