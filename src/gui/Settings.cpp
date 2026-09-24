/*****************************************************************************
   Project: CEDAR Logic Simulator

   Settings: process-wide settings accessor. See Settings.h.
*****************************************************************************/

#include "Settings.h"
#ifndef CL_NO_WX
#include <wx/event.h>
#endif

Settings& appConfig() {
	static Settings instance;
	return instance;
}

#ifndef CL_NO_WX
int themeModsFromKeyEvent(const wxKeyEvent& e) {
	int mods = 0;
	if (e.ShiftDown()) mods |= ThemeShortcutMod::Shift;
	if (e.AltDown())   mods |= ThemeShortcutMod::Alt;
#ifdef __WXOSX__
	// See the ThemeShortcutMod comment in Settings.h: on Mac, ControlDown()
	// fires for the physical Cmd key and RawControlDown() for physical Control.
	if (e.RawControlDown()) mods |= ThemeShortcutMod::Ctrl;
	if (e.ControlDown())    mods |= ThemeShortcutMod::Meta;
#else
	if (e.ControlDown()) mods |= ThemeShortcutMod::Ctrl;
	if (e.MetaDown())    mods |= ThemeShortcutMod::Meta;
#endif
	return mods;
}
#endif

std::string formatThemeShortcut(int modifiers, int keyCode) {
	std::string s;
#ifdef __APPLE__
	if (modifiers & ThemeShortcutMod::Ctrl)  s += "Ctrl+";
	if (modifiers & ThemeShortcutMod::Alt)   s += "Opt+";
	if (modifiers & ThemeShortcutMod::Shift) s += "Shift+";
	if (modifiers & ThemeShortcutMod::Meta)  s += "Cmd+";
#else
	if (modifiers & ThemeShortcutMod::Ctrl)  s += "Ctrl+";
	if (modifiers & ThemeShortcutMod::Alt)   s += "Alt+";
	if (modifiers & ThemeShortcutMod::Shift) s += "Shift+";
	if (modifiers & ThemeShortcutMod::Meta)  s += "Win+";
#endif
	if (keyCode >= 32 && keyCode < 127) {
		s += (char)keyCode;
	} else {
		s += "Key" + std::to_string(keyCode);
	}
	return s;
}
