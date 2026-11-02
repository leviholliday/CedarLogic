/*****************************************************************************
   Project: CEDAR Logic Simulator
   WinAppearance: match the Windows title bar to the app's light/dark theme.
*****************************************************************************/

#ifndef WINAPPEARANCE_H_
#define WINAPPEARANCE_H_

#ifdef _WIN32

class wxTopLevelWindow;

// Dark or light caption for one window. The canvas and panels are drawn by us
// and follow the theme on their own; the caption is drawn by Windows, which
// otherwise leaves a white bar above a dark app. Windows 10 before 1809 has no
// dark caption, and there this does nothing.
void WinSetDarkTitlebar(wxTopLevelWindow* window, bool dark);

// The same for every top-level window that is open right now.
void WinSetDarkTitlebars(bool dark);

#endif

#endif
