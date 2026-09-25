/*****************************************************************************
   Project: CEDAR Logic Simulator

   Settings: user/application settings and derived timing.

   Extracted from the MainApp God-singleton (Workstream C). Reach it through the
   free accessor appConfig() instead of wxGetApp(). ApplicationSettings lived on
   MainApp.h; it moves here so the settings are a cohesive unit and MainApp.h
   pulls it back in via this header.
*****************************************************************************/

#pragma once

#include <string>

// Modifier bits for ApplicationSettings::themeShortcutModifiers -- deliberately
// our own flags, not wx's wxMOD_*/wxACCEL_* families. Reason: on wxOSX, the
// physical Cmd key is reported through wxKeyEvent::ControlDown() (wx's own
// "primary modifier" convention -- see wx/kbdstate.h and
// src/osx/cocoa/utils.mm's SetControlDown(modifiers & NSCommandKeyMask)) and
// MetaDown() is never set at all; the physical Control key is RawControlDown()
// instead. Meta here means "the Cmd key on macOS, the Windows key elsewhere" --
// use themeModsFromKeyEvent() below to read it correctly rather than calling
// MetaDown()/ControlDown() directly, on either the capture button
// (PreferencesWindow) or the live shortcut match (MainFrame's CHAR_HOOK).
namespace ThemeShortcutMod {
	const int Shift = 1;
	const int Alt   = 2;
	const int Ctrl  = 4;   // the physical Control key
	const int Meta  = 8;   // Cmd on macOS, the Windows key elsewhere
}

class wxKeyEvent;

// Reads the four ThemeShortcutMod bits off a live key event, correctly for the
// platform (see the namespace comment above for why this isn't just four
// direct .*Down() calls). Shared by MainFrame's shortcut match and
// the Preferences window's shortcut field so they can never disagree on what a given
// keystroke means.
int themeModsFromKeyEvent(const wxKeyEvent& event);

enum class ThemeMode {
	System = 0,        // follow the OS light/dark setting
	Light = 1,          // always launch light
	Dark = 2,           // always launch dark
	RememberLast = 3    // launch in whatever theme was active when the app last closed
};

struct ApplicationSettings {
	std::string helpFile;
	std::string lastDir;
	// The library circuit that was open at quit, reopened on the next launch.
	std::string lastLibraryDoc;
	// Printed under exported circuits (see MainFrame::ExportInfo).
	std::string studentName;
	bool exportInfoEnabled = true;
	// Toolbar: style (see cl::tb::Style; 0 = the native Classic toolbar) and
	// which tool groups are hidden (bit per cl::tb::Group).
	int toolbarStyle = 0;
	int toolbarHidden = 0;
	int mainFrameWidth;
	int mainFrameHeight;
	int mainFrameLeft;
	int mainFrameTop;
	int timePerStep;
	int refreshRate;
	int autosaveSeconds;   // 0 disables autosave entirely
	float wireConnRadius;
	bool wireConnVisible;
	bool gridlineVisible;
	// Draw every 5th grid line darker (a ruler-style guide).
	bool majorGridVisible = true;

	// The tab bar: our own, with dragging and split view, or the plain system
	// one this app used to have.
	bool classicTabs = false;

	// The first-run welcome has been through once.
	bool hasSeenWelcome = false;
	int gridStyle = 0;       // 0 = lines, 1 = dots
	int accentColor = 0;     // index into RenderStyle::accent()'s choices
	int wireThickness = 1;   // 0 = thin, 1 = normal, 2 = thick (screen only)
	bool showStatusInfo = true;
	int updateChannel = 0;   // 0 = normal tester, 1 = beta tester (see Updater.h)   // zoom / position / counts in the status bar
	// What a plain scroll does on the canvas, per device (0 = zoom, 1 = move
	// around). Only macOS can tell a trackpad from a wheel; elsewhere every
	// scroll uses the mouse setting. Cmd/Ctrl+scroll always zooms and
	// Shift+scroll always pans sideways.
	int mouseWheelAction = 0;
	int trackpadScrollAction = 1;
	// Flip which way scrolling zooms, per device. The mouse one is on by
	// default: tools like Scroll Reverser change the direction the app sees.
	bool reverseWheelZoom = true;
	bool reverseTrackpadZoom = false;
	// Width of the gate palette / minimap column, in pixels. 0 = not chosen
	// yet; MainFrame measures the natural width on first launch.
	int sidePanelWidth = 0;
	// Target edge of a gate tile in the palette, in pixels. Tiles stretch a
	// little past it to fill the panel's width exactly.
	int paletteGateSize = 48;
	// Cmd+D: false = duplicate without touching the clipboard; true = copy to
	// the clipboard then paste, like Cmd+C, Cmd+V.
	bool duplicateUsesClipboard = false;
	bool rightClickRotate;

	// Dark mode. themeMode picks the launch behavior (see ThemeMode); lastDarkMode
	// is the actual on/off state when the app last closed, consulted only when
	// themeMode == RememberLast. themeShortcut* configure the toggle hotkey (see
	// MainFrame::ApplyThemeShortcut); showThemeToggleButton controls the small
	// sun/moon switch on the toolbar so it can be hidden if it's distracting.
	int themeMode = (int)ThemeMode::System;
	bool lastDarkMode = false;
	bool themeShortcutEnabled = true;
	int themeShortcutKeyCode = 'D';
#ifdef __APPLE__
	int themeShortcutModifiers = ThemeShortcutMod::Meta | ThemeShortcutMod::Shift;   // Cmd+Shift+D
#else
	int themeShortcutModifiers = ThemeShortcutMod::Ctrl | ThemeShortcutMod::Shift;   // Ctrl+Shift+D
#endif
	bool showThemeToggleButton = true;
};

class Settings {
public:
	ApplicationSettings appSettings;
	// Milliseconds of simulated time per step (derived from appSettings.timePerStep).
	unsigned long timeStepMod = 0;
	// Directory the on-disk resources load from (may differ from cwd). Only the
	// help book lives there now; see EmbeddedRes.h for the rest.
	std::string resourcesDir;
};

// The process-wide settings. Lives for the whole run; constructed on first use.
Settings& appConfig();

// Human-readable form of a theme-toggle shortcut ("Ctrl+Shift+D", "Cmd+Shift+D"),
// for the View menu item and the the Preferences window's shortcut field. Shared so the
// two stay in sync without duplicating the modifier-name logic.
std::string formatThemeShortcut(int modifiers, int keyCode);
