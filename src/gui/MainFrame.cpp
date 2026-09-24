/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   MainFrame: Main frame object
*****************************************************************************/

#include "UiControls.h"
#include "MainApp.h"
#include "CedarLogic.h"     // publisher name for the About panel
#include "wx/aboutdlg.h"
#include "PaletteDrag.h"
#include "RenderMode.h"
#ifdef WITH_SKIA
#include "render/SkiaProbe.h"    // skiaRenderToPng (no Skia headers leak here)
#include "render/Scene.h"
#include "render/RenderStyle.h"
#endif
#include "SimBridge.h"
#include "Settings.h"
#ifdef __APPLE__
#include "MacAppearance.h"
#endif
#include "EmbeddedRes.h"
#include "ToolbarIcons.h"
#include "GateLibrary.h"
#include "guiWire.h"
#include <fstream>
#include "MainFrame.h"
#include "TabStrip.h"
#include "Welcome.h"
#include "ShortcutsSheet.h"
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include "AutosaveStore.h"
#include "FileLock.h"
#include "wx/filedlg.h"
#include "wx/timer.h"
#include "wx/wfstream.h"
#include "wx/image.h"
#include "wx/thread.h"
#include "wx/toolbar.h"
#include "wx/clipbrd.h"
#include "wx/dataobj.h"
#include "wx/config.h"
#include "wx/checkbox.h"
#include "wx/radiobox.h"
#include "wx/stattext.h"
#include "wx/sizer.h"
#include "wx/dialog.h"
#include "wx/button.h"
#include "wx/bmpbndl.h"
#include "wx/settings.h"
#include "wx/file.h"
#include "CircuitParse.h"
#include "migrate.hpp"   // cl::LoadResult, held across the validate/apply split
#include "OscopeFrame.h"
#include "PreferencesWindow.h"
#include "TruthTableDialog.h"
#include "ModernToolbar.h"
#include "CircuitLibrary.h"
#include "LibraryDialogs.h"
#include <wx/progdlg.h>
#include <algorithm>
#ifdef __APPLE__
#include "TabSwitcherMac.h"
#endif
#include "wx/docview.h"
#include "commands.h"
#include "../version.h"
#ifdef __APPLE__
#include "SparkleUpdater.h"
#include "NativeIcons.h"
#endif
#ifdef _WIN32
#include "WinAppearance.h"
#include "WinSparkleUpdater.h"
#endif
#include "UiKit.h"
#include "StatusStrip.h"
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>   // DeferWindowPos, for the focus-mode slide
#endif
#include "UpdateInfo.h"   // cl::update::checksDisabled, for managed deployments

DECLARE_APP(MainApp)

// How often the sim/idle timers poll (ms). Kept at roughly half the refresh-rate
// target (appSettings.refreshRate, ~16 ms) so the render cadence can actually
// reach it: the cadence is a round-trip through two of these timers -- OnTimer
// sends a step, the sim thread replies, OnIdle drains the reply and repaints --
// so with two poll intervals per frame, ~8 ms gives a ~16 ms round-trip (~60
// fps). The old 20 ms poll was coarser than the target and capped it near 16
// fps. Simulation *speed* is unaffected: each step advances by
// elapsed-time/timeStepMod (with the remainder carried over), not per-tick, so
// a finer poll just means smaller, more frequent steps -- not a faster sim.
static const int TIMER_POLL_MS = 8;

// The most steps one catch-up is allowed to run. Anything beyond this is time
// the app was not running for (backgrounded, or the machine asleep), and there
// is nothing to be gained by simulating it: the user was not watching, and the
// work would take longer than the gap it is chasing.
static const int MAX_CATCHUP_STEPS = 10;

// How long settleSimulation waits for the logic thread to answer one step
// before giving up. Generous: it only has to exceed the slowest single step a
// circuit can take, and hitting it at all means the core has stopped replying.
static const int SETTLE_STEP_TIMEOUT_MS = 5000;

BEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_MENU(wxID_EXIT,  MainFrame::OnQuit)
    EVT_MENU(wxID_ABOUT, MainFrame::OnAbout)
    EVT_MENU(wxID_HELP_CONTENTS, MainFrame::OnHelpContents)
    EVT_MENU(Help_KeyboardShortcuts, MainFrame::OnKeyboardShortcuts)
    EVT_MENU(wxID_NEW, MainFrame::OnNew)
    EVT_MENU(wxID_OPEN, MainFrame::OnOpen)
    EVT_MENU(wxID_SAVE, MainFrame::OnSave)
    EVT_MENU(wxID_SAVEAS, MainFrame::OnSaveAs)
	EVT_MENU(File_Export, MainFrame::OnExportBitmap)
	EVT_MENU(File_ExportLegacy, MainFrame::OnExportLegacy)
	EVT_MENU(File_ExportV2, MainFrame::OnExportV2)
	EVT_MENU(File_ClipCopy, MainFrame::OnCopyToClipboard)
	
	EVT_MENU(wxID_UNDO, MainFrame::OnUndo)
	EVT_MENU(wxID_REDO, MainFrame::OnRedo)
	EVT_MENU(wxID_CUT, MainFrame::OnCut)
	EVT_MENU(wxID_COPY, MainFrame::OnCopy)
	EVT_MENU(wxID_PASTE, MainFrame::OnPaste)
	
    EVT_MENU(View_Oscope, MainFrame::OnOscope)
    EVT_MENU(View_Gridline, MainFrame::OnViewGridline)
    EVT_MENU(View_WireConn, MainFrame::OnViewWireConn)
    EVT_MENU(View_DarkMode, MainFrame::OnViewDarkMode)
    EVT_TOOL(Tool_ThemeToggle, MainFrame::OnViewDarkMode)
    EVT_MENU(wxID_PREFERENCES, MainFrame::OnPreferences)
    
	EVT_TOOL(Tool_Pause, MainFrame::OnPause)
	EVT_TOOL(Tool_Step, MainFrame::OnStep)
	EVT_TOOL(Tool_ZoomIn, MainFrame::OnZoomIn)
	EVT_TOOL(Tool_ZoomOut, MainFrame::OnZoomOut)
	EVT_SCROLL(MainFrame::OnTimeStepModSlider)
	EVT_TOOL(Tool_Lock, MainFrame::OnLock)
	EVT_TOOL(Tool_NewTab, MainFrame::OnNewTab)

	//EVT_MENU(Help_ReportABug, MainFrame::OnReportABug)
	//EVT_MENU(Help_RequestAFeature, MainFrame::OnRequestAFeature)
	EVT_MENU(Help_DownloadLatestVersion, MainFrame::OnDownloadLatestVersion)
	
    //EVT_SIZE(MainFrame::OnSize)
    //EVT_MAXIMIZE(MainFrame::OnMaximize)
    
	EVT_TIMER(TIMER_ID, MainFrame::OnTimer)
	EVT_TIMER(IDLETIMER_ID, MainFrame::OnIdle)
	EVT_TIMER(AUTOSAVE_TIMER_ID, MainFrame::OnAutosaveTimer)

	EVT_CLOSE(MainFrame::OnClose)
END_EVENT_TABLE()

#define ID_TEXTCTRL 5001

// Global print data object:
wxPrintData *g_printData = (wxPrintData*) NULL;


namespace {

// A copy of `from` for use as a popup: a menu can only belong to one menu bar
// or popup at a time, so the bar's own menus cannot be shown directly.
wxMenu* cloneMenu(const wxMenu* from) {
	wxMenu* to = new wxMenu;
	for (const wxMenuItem* item : from->GetMenuItems()) {
		if (item->IsSeparator()) { to->AppendSeparator(); continue; }
		if (item->IsSubMenu()) {
			to->AppendSubMenu(cloneMenu(item->GetSubMenu()), item->GetItemLabel());
			continue;
		}
		wxMenuItem* copy = to->Append(item->GetId(), item->GetItemLabel(), item->GetHelp(), item->GetKind());
		if (item->IsCheckable()) copy->Check(item->IsChecked());
		copy->Enable(item->IsEnabled());
	}
	return to;
}

}  // namespace

void MainFrame::RunMenuCommand(int id) {
	if (id == View_DarkMode) { ToggleDarkMode(); return; }
	wxCommandEvent evt(wxEVT_MENU, id);
	if (wxMenuBar* mb = GetMenuBar()) {
		if (wxMenuItem* item = mb->FindItem(id)) {
			if (!item->IsEnabled()) { wxBell(); return; }
			if (item->IsCheckable()) {
				item->Check(!item->IsChecked());
				evt.SetInt(item->IsChecked() ? 1 : 0);
			}
		}
	}
	evt.SetEventObject(this);
	ProcessWindowEvent(evt);
}

void MainFrame::ShowAppMenu(wxWindow* from, const wxPoint& at, bool quick) {
	wxMenuBar* mb = GetMenuBar();
	if (mb == nullptr || from == nullptr) return;
	mb->UpdateMenus();   // enabled and checked states as of now
	wxMenu menu;
	if (quick) {
		menu.Append(wxID_NEW, "New");
		menu.Append(wxID_OPEN, "Open...");
		menu.Append(wxID_SAVE, "Save");
		menu.AppendSeparator();
		menu.Append(View_TruthTable, "Truth Table...");
		menu.Append(Tool_NewTab, "New Tab");
		menu.AppendSeparator();
	}
	for (size_t i = 0; i < mb->GetMenuCount(); i++)
		menu.AppendSubMenu(cloneMenu(mb->GetMenu(i)), mb->GetMenuLabel(i));
	const int chosen = from->GetPopupMenuSelectionFromUser(menu, at);
	if (chosen != wxID_NONE) RunMenuCommand(chosen);
}

#ifdef __WXMSW__
wxStatusBar* MainFrame::OnCreateStatusBar(int number, long style, wxWindowID id,
                                          const wxString& name) {
	StatusStrip* bar = new StatusStrip(this, id, style, name);
	bar->SetFieldsCount(number);
	return bar;
}
#endif

MainFrame::MainFrame(const wxString& title, string cmdFilename)
       : wxFrame(NULL, wxID_ANY, title, wxDefaultPosition, wxSize(1800,900))
{
#ifdef __WXMSW__
    // Frame icon (title bar, taskbar button, Alt-Tab). Loaded by name from the
    // resource compiled in via icon.rc. macOS takes it from the bundle and Linux
    // from the .desktop file, so neither needs this.
    SetIcon(wxICON(appicon));
#endif
	currentCanvas = nullptr;

	// Set default locations
	if (appConfig().appSettings.lastDir == "") lastDirectory = wxGetHomeDir();
	else lastDirectory = appConfig().appSettings.lastDir;  // added cast KAS

	//////////////////////////////////////////////////////////////////////////
    // create a menu bar
	//////////////////////////////////////////////////////////////////////////
    wxMenu *fileMenu = new wxMenu; // FILE MENU
	// Circuits live in the app's library and save themselves (see
	// CircuitLibrary.h); files on disk come in through Import and go out
	// through Export.
	fileMenu->Append(wxID_NEW, "&New\tCtrl+N", "Start a new circuit");
	fileMenu->Append(wxID_OPEN, "&Open...\tCtrl+O", "Open one of your circuits");
	fileMenu->Append(File_Import, "&Import File...\tCtrl+Shift+O", "Bring a .cdl file into your circuits");
	fileMenu->Append(wxID_SAVE, "&Save\tCtrl+S", "Save now and keep a version");
	fileMenu->Append(File_Rename, "&Rename...", "Rename this circuit");
	fileMenu->Append(File_VersionHistory, "&Version History...", "See and restore earlier versions");
	fileMenu->Append(File_CloseCircuit, "Close &Circuit\tCtrl+Shift+W", "Close this circuit (it stays in your circuits)");
	fileMenu->AppendSeparator();
	// Tabs are documents, not edits -- they belong beside New and Open.
	fileMenu->Append(Tool_NewTab, "New &Tab\tCtrl+T", "Open a new tab");
	fileMenu->Append(Tool_CloseTab, "&Close Tab\tCtrl+W", "Close the current tab");
	fileMenu->Append(Tool_ReopenTab, "&Reopen Closed Tab\tCtrl+Shift+T", "Bring back the tab you just closed");
	fileMenu->Append(Tool_SplitRight, "Split &View\tCtrl+Alt+S", ui::platformKeys("Show another tab beside this one (Cmd+Option+S)"));
	fileMenu->Append(Tool_SplitClose, "Close Split\tCtrl+Alt+W", ui::platformKeys("Put the split tab back in the tab strip (Cmd+Option+W)"));
	fileMenu->Append(Tool_FocusOtherPane, "Switch Pane\tCtrl+Alt+Right", ui::platformKeys("Work in the other side of the split (Cmd+Option+Right)"));
	fileMenu->AppendSeparator();
	fileMenu->Append(wxID_SAVEAS, "Export as CedarLogic File...\tCtrl+Shift+E", "Save a .cdl copy anywhere, to share or submit");
	fileMenu->Append(File_Export, "Export as Image...\tCtrl+E", "Export or copy circuit image");
	fileMenu->Append(File_ExportV2, "Export as V2 (legacy XML)...", "Save a copy in the pre-V3 XML format");
	fileMenu->Append(File_ExportLegacy, "Export as V1.x Compatible...", "Save a copy in the oldest format");
	fileMenu->AppendSeparator();
	// Stock id with no label: wx supplies the platform's own wording and
	// accelerator ("Quit\tCtrl+Q", and macOS moves it to the application menu).
	fileMenu->Append(wxID_EXIT);

    wxMenu *viewMenu = new wxMenu; // VIEW MENU
    // Zoom is on the toolbar and the keyboard but was never in a menu, so it was
    // undiscoverable. Same ids as the toolbar tools -- wx routes tool and menu
    // commands through the same event, so the existing handlers pick these up.
    viewMenu->Append(Tool_ZoomIn, "Zoom &In\tCtrl+=", "Zoom in");
    viewMenu->Append(Tool_ZoomOut, "Zoom &Out\tCtrl+-", "Zoom out");
    viewMenu->Append(View_ZoomFit, "Zoom to &Fit\tCtrl+0", "Show the whole circuit");
    viewMenu->Append(View_ZoomActual, "&Actual Size\tCtrl+1", "Zoom to 100%");
    viewMenu->AppendSeparator();
    viewMenu->AppendCheckItem(View_FocusMode, "&Focus Mode\tCtrl+.", "Slide the side panel and toolbar away, leaving just the canvas");
    viewMenu->AppendSeparator();
    viewMenu->AppendCheckItem(View_Gridline, "Display &Gridlines", "Toggle gridline display");
    viewMenu->AppendCheckItem(View_WireConn, "Display &Wire Connection Points", "Toggle wire connection points");
    viewMenu->AppendSeparator();
    // Label carries the current shortcut as text (set/refreshed by
    // ApplyThemeShortcutLabel) rather than a static "\tCtrl+Shift+D", since the
    // shortcut itself is user-configurable from Preferences.
    viewMenu->AppendCheckItem(View_DarkMode, "&Dark Mode", "Toggle dark mode");
    // No Ctrl+Shift+T here: that is Reopen Closed Tab, as in a browser. The
    // canvas opens the truth table on a bare T (GUICanvas::OnKeyDown).
    viewMenu->Append(View_TruthTable, "&Truth Table...", "Make a truth table from the switches and lights (T)");
    viewMenu->AppendCheckItem(View_SimView, "&Simulation View\tCtrl+R", "Watch the circuit run: live, animated wires and a control bar");
    viewMenu->AppendSeparator();
    viewMenu->Append(View_Oscope, "&Oscope\tCtrl+G", "Show the Oscope");

    wxMenu *helpMenu = new wxMenu; // HELP MENU
    helpMenu->Append(wxID_HELP_CONTENTS, "&Contents...\tF1", "Show Help system");
	helpMenu->Append(Help_KeyboardShortcuts, "&Keyboard Shortcuts...", "Show keyboard shortcuts");
	helpMenu->AppendSeparator();
	helpMenu->Append(Help_Welcome, "Welcome to CedarLogic...", "The first-run introduction, again");
	helpMenu->Append(Help_SetUp, "Set Up CedarLogic...", "Walk through the settings one at a time");
	helpMenu->Append(Help_Tour, "Guided Tour", "Build a working circuit step by step");
	helpMenu->AppendSeparator();
	//helpMenu->Append(Help_ReportABug, "Report a bug...");
	//helpMenu->Append(Help_RequestAFeature, "Request a feature...");
#if defined(__APPLE__) || defined(_WIN32)
	helpMenu->Append(Help_DownloadLatestVersion, "Check for Updates...");
#else
	helpMenu->Append(Help_DownloadLatestVersion, "Download latest version...");
#endif
	// An administrator can turn update checking off for a managed deployment, so
	// the organisation owns the installed version. Leave the item visible but
	// disabled: a greyed-out entry explains why nothing happens, where a missing
	// one just looks like the feature vanished.
	if (cl::update::checksDisabled()) {
		helpMenu->Enable(Help_DownloadLatestVersion, false);
	}
	helpMenu->AppendSeparator();
    helpMenu->Append(wxID_ABOUT, "&About...", "Show about dialog");

	wxMenu *editMenu = new wxMenu; // EDIT MENU
	editMenu->Append(wxID_UNDO, "Undo\tCtrl+Z", "Undo last operation");
	editMenu->Append(wxID_REDO, "Redo\tCtrl+Shift+Z", "Redo last operation");
	editMenu->AppendSeparator();
	editMenu->Append(wxID_CUT, "Cut\tCtrl+X", "Cut selection to clipboard");
	editMenu->Append(wxID_COPY, "Copy\tCtrl+C", "Copy selection to clipboard");
	editMenu->Append(wxID_PASTE, "Paste\tCtrl+V", "Paste selection from clipboard");
	editMenu->Append(Edit_Duplicate, "Duplicate\tCtrl+D", "Copy the selection and place it with the mouse");
	editMenu->Append(wxID_SELECTALL, "Select All\tCtrl+A", "Select every gate and wire on this page");
	editMenu->AppendSeparator();
	// wxID_PREFERENCES, not an id of our own: that is what makes macOS lift this
	// into the application menu as "Settings..." with its usual Cmd+, -- which
	// is where a Mac user looks for it, rather than under View. Windows and GTK
	// keep it here at the foot of Edit, which is their convention.
	editMenu->Append(wxID_PREFERENCES, "&Preferences...\tCtrl+,", "Open preferences dialog");

    // now append the freshly created menu to the menu bar...
    wxMenuBar *menuBar = new wxMenuBar();
    menuBar->Append(fileMenu, "&File");
    menuBar->Append(editMenu, "&Edit");
    menuBar->Append(viewMenu, "&View");
    menuBar->Append(helpMenu, "&Help");

    // set checkmarks on the view toggles
    menuBar->Check(View_Gridline, appConfig().appSettings.gridlineVisible);
    menuBar->Check(View_WireConn, appConfig().appSettings.wireConnVisible);
    menuBar->Check(View_DarkMode, renderMode().darkMode);
    
    // ... and attach this menu bar to the frame
    SetMenuBar(menuBar);
#ifdef __WXMSW__
    // Hidden, so the top of the window is one bar rather than a title bar, a
    // menu bar and a toolbar stacked up. Everything in it is behind the
    // toolbar's menu button (ShowAppMenu). Its shortcuts keep working: wxMSW
    // translates accelerators from the bar's own table, attached or not.
    ::SetMenu(GetHWND(), nullptr);
#endif

    // The canvas holds keyboard focus, and menu-bar accelerators don't reach it;
    // meanwhile wxMSW compiles every menu accelerator into the frame's
    // accelerator table, so SetAcceleratorTable would wipe them all. So route the
    // edit shortcuts through a frame CHAR_HOOK, which sees the key first
    // regardless of focus and touches no table. Windows fires an accelerator OR
    // dispatches the key here, never both, so there's no double-firing.
    //   Redo: Ctrl+Shift+Z and Ctrl+Y (Windows convention).
    //   Cut / Copy / Paste: Ctrl+X / Ctrl+C / Ctrl+V.
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent &e) {
        // The dark-mode toggle shortcut, matched with themeModsFromKeyEvent so
        // this agrees with what the Preferences window's shortcut field recorded (see
        // its definition in Settings.cpp for why a direct MetaDown() check is
        // wrong on macOS).
        const auto &ts = appConfig().appSettings;
        if (ts.themeShortcutEnabled && e.GetKeyCode() == ts.themeShortcutKeyCode) {
            const int mods = themeModsFromKeyEvent(e);
            if (mods != 0 && mods == ts.themeShortcutModifiers) {
                ToggleDarkMode();
                return;
            }
        }
        const int k = e.GetKeyCode();
        const bool ctrl = e.ControlDown() || e.CmdDown();
        // Ctrl+Tab / Ctrl+Shift+Tab: the page switcher. On macOS that's the
        // real Control key (RawControlDown); ControlDown() there means Cmd.
#ifdef __WXOSX__
        const bool switcherMod = e.RawControlDown() && !e.CmdDown();
#else
        const bool switcherMod = e.ControlDown();
#endif
        if (k == WXK_TAB && switcherMod && !e.AltDown()) {
            handleTabSwitchKey(e.ShiftDown());
            return;
        }
        if (tabSwitchActive && k == WXK_ESCAPE) {
            cancelTabSwitch();
            return;
        }
        if (!ctrl && !e.AltDown() && (k == '?' || (k == '/' && e.ShiftDown()))) {
            wxCommandEvent evt(wxEVT_MENU, Help_KeyboardShortcuts);
            ProcessWindowEvent(evt);
            return;
        }
        // Shift+1..9: jump straight to the Nth palette section (Basic Gates,
        // Input/Output, ...) without reaching for the mouse and the dropdown.
        // GetKeyCode() is the unmodified key even with Shift held (same as the
        // Ctrl+Shift+Z check below relies on), so this doesn't need the digit
        // row's shifted symbols ('!', '@', ...).
        // Shift+0 is the tenth, the way the digit row reads.
        if (e.ShiftDown() && !ctrl && !e.AltDown() && k >= '0' && k <= '9' && gatePalette) {
            gatePalette->SelectSectionByIndex(k == '0' ? 9u : (unsigned int)(k - '1'));
            return;
        }
        // Cmd+Shift+Left/Right resize the split, the way Arc does it.
        if (ctrl && e.ShiftDown() && !e.AltDown() && (k == WXK_LEFT || k == WXK_RIGHT)) {
            if (IsSplit()) {
                NudgeSplitSash(k == WXK_RIGHT ? 60 : -60);
                return;
            }
        }
        int cmd = 0;
        if (ctrl && !e.AltDown()) {
            if (e.ShiftDown()) {
                if (k == 'Z' || k == 'z') cmd = wxID_REDO;
            } else {
                if      (k == 'Y' || k == 'y') cmd = wxID_REDO;
                else if (k == 'X' || k == 'x') cmd = wxID_CUT;
                else if (k == 'C' || k == 'c') cmd = wxID_COPY;
                else if (k == 'V' || k == 'v') cmd = wxID_PASTE;
            }
        }
        if (cmd != 0) {
            wxCommandEvent evt(wxEVT_MENU, cmd);
            ProcessWindowEvent(evt);
        } else {
            e.Skip();
        }
    });

	//////////////////////////////////////////////////////////////////////////
    // parse a gate library
	//////////////////////////////////////////////////////////////////////////
	LibraryParse newLib(cl::res::text("cl_gatedefs.xml"));
	gateLibrary().libParser = newLib;
	
	//////////////////////////////////////////////////////////////////////////
    // create a toolbar
	//////////////////////////////////////////////////////////////////////////
	toolBar = new wxToolBar(this, TOOLBAR_ID, wxPoint(0,0), wxDefaultSize, wxTB_HORIZONTAL|wxNO_BORDER| wxTB_FLAT);

	// One toolbar, described once. Each button names its icon twice, the Apple
	// way and ours, and cl::toolbarIcon picks (see ToolbarIcons.h). SF Symbols
	// want 18pt to sit right beside a 24px SVG.
	//
	// Nothing calls SetToolBitmapSize, and that is the point. Naming a size makes
	// wx scale it by whole factors only (wxToolBarBase::AdjustToolBitmapSize),
	// which rounds 125% and 150% displays down to 1x and leaves 24px icons in a
	// bar sized for 30 or 36. Left alone, wx takes the size from the icons
	// themselves and each one rasterizes to fit.
	auto icon = [&](const char* sfSymbol, const char* svgName) {
		return cl::toolbarIcon(toolBar, sfSymbol, 18, svgName, wxSize(24, 24));
	};

	toolBar->AddTool(wxID_NEW, "New", icon("doc.badge.plus", "new"), "New");
	toolBar->AddTool(wxID_OPEN, "Open", icon("folder", "open"), "Open");
	toolBar->AddTool(wxID_SAVE, "Save", icon("square.and.arrow.down", "save"), "Save");
	toolBar->AddSeparator();
	toolBar->AddTool(wxID_UNDO, "Undo", icon("arrow.uturn.backward", "undo"), "Undo");
	toolBar->AddTool(wxID_REDO, "Redo", icon("arrow.uturn.forward", "redo"), "Redo");
	toolBar->AddSeparator();
	toolBar->AddTool(wxID_COPY, "Copy", icon("doc.on.doc", "copy"), "Copy");
	toolBar->AddTool(wxID_PASTE, "Paste", icon("clipboard", "paste"), "Paste");
	toolBar->AddSeparator();
	toolBar->AddTool(Tool_ZoomIn, "Zoom In", icon("plus.magnifyingglass", "zoomin"), "Zoom In");
	toolBar->AddTool(Tool_ZoomOut, "Zoom Out", icon("minus.magnifyingglass", "zoomout"), "Zoom Out");
	toolBar->AddSeparator();
	// Held onto because OnPause and OnLock swap them in as the state changes.
	pauseIcon = icon("pause.fill", "pause");
	playIcon = icon("play.fill", "play");
	lockedIcon = icon("lock.fill", "locked");
	unlockedIcon = icon("lock.open.fill", "unlocked");
	toolBar->AddTool(Tool_SimView, "Run", icon("waveform.path.ecg", "play"), "Simulation View (" + wxString(
#ifdef __WXOSX__
		"Cmd"
#else
		"Ctrl"
#endif
		) + "+R)", wxITEM_CHECK);
	toolBar->AddTool(Tool_Pause, "Pause/Resume", pauseIcon, "Pause/Resume", wxITEM_CHECK);
	toolBar->AddTool(Tool_Step, "Step", icon("forward.frame.fill", "step"), "Step");
	timeStepModSlider = new wxSlider(toolBar, wxID_ANY, appConfig().timeStepMod, 1, 500, wxDefaultPosition, wxSize(125,-1), wxSL_HORIZONTAL);
	wxString oss;
	oss << appConfig().timeStepMod << "ms";
	timeStepModVal = new wxStaticText(toolBar, wxID_ANY, oss, wxDefaultPosition, wxSize(45, -1), wxSUNKEN_BORDER | wxALIGN_RIGHT | wxST_NO_AUTORESIZE);
	// Label + tooltip so it's clear this sets the simulation step size / speed.
	wxStaticText* timeStepModLabel = new wxStaticText(toolBar, wxID_ANY, "Sim step ");
	const wxString stepTip = "Simulation time per step (ms). Lower = faster simulation, higher = slower.";
	timeStepModLabel->SetToolTip(stepTip);
	timeStepModSlider->SetToolTip(stepTip);
	timeStepModVal->SetToolTip(stepTip);
	toolBar->AddControl( timeStepModLabel );
	toolBar->AddControl( timeStepModSlider );
	toolBar->AddControl( timeStepModVal );
	toolBar->AddSeparator();
	toolBar->AddTool(Tool_Lock, "Lock state", unlockedIcon, "Lock state", wxITEM_CHECK);
	toolBar->AddSeparator();
	// A visible on-canvas switch for dark mode, in addition to the View menu
	// item and the configurable shortcut -- so it can be flipped without
	// leaving the drawing. Hideable from Preferences (showThemeToggleButton)
	// for anyone who'd rather not have it in view; see ApplyThemeToggleVisibility.
	sunIcon = icon("sun.max.fill", "sun");
	moonIcon = icon("moon.fill", "moon");
	toolBar->AddTool(Tool_ThemeToggle, "Dark Mode", renderMode().darkMode ? moonIcon : sunIcon,
	                 "Toggle dark mode", wxITEM_CHECK);
	toolBar->AddSeparator();
	toolBar->AddTool(wxID_ABOUT, "About", icon("info.circle", "about"), "About");
	toolBar->AddSeparator();
	toolBar->AddTool(Tool_NewTab, "New Tab", icon("plus.square", "newtab"), "New Tab");
#ifdef __WXMSW__
	toolBar->AddSeparator();
	toolBar->AddTool(Tool_AppMenu, "Menu", icon("ellipsis.circle", "more"), "Menu");
	Bind(wxEVT_TOOL, [this](wxCommandEvent&) {
		ShowAppMenu(toolBar, toolBar->ScreenToClient(wxGetMousePosition()));
	}, Tool_AppMenu);
#endif
#ifdef __WXOSX__
	// Keeps the tools left-aligned under the unified title bar, which otherwise
	// spreads them across the full window width.
	toolBar->AddStretchableSpace();
#endif
	SetToolBar(toolBar);
	toolBar->Show(true);
	toolBar->ToggleTool(Tool_ThemeToggle, renderMode().darkMode);
	ApplyThemeToggleVisibility();
	ApplyThemeShortcutLabel();
	// ApplyTheme() itself waits until the end of the constructor -- it touches
	// the canvases, minimap and oscope panel, none of which exist yet here.

    // One flat field, keeping the default resize grip. Two artifacts fixed:
    //   - the old second field (never written) left a divider down the middle,
    //     so use a single field;
    //   - the field's sunken 3-D border rendered as a white edge line, because
    //     wx 3.2's native status bar doesn't theme its borders for dark mode
    //     (wxWidgets#25521; real dark-mode support is 3.3+). wxSB_FLAT drops the
    //     border -- and it was the border, not the grip, so the grip stays.
    // Field 0 still carries wx's automatic menu-hover help text.
    // Fields 1-3 (zoom, cursor position, counts) are filled by UpdateStatusInfo.
    // Every field is wxSB_FLAT: wx 3.2's native bar draws white borders
    // between fields in dark mode otherwise (wxWidgets#25521).
    CreateStatusBar(1);
    ApplyStatusInfoVisibility();
    SetStatusText("");

	mainSizer = new wxBoxSizer( wxHORIZONTAL );
	wxBoxSizer* leftPaneSizer = new wxBoxSizer( wxVERTICAL );
	wxSize sz = this->GetClientSize();
	
	// now a gate palette for the library
	gatePalette = new PaletteFrame(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
	leftPaneSizer->Add( gatePalette, wxSizerFlags(1).Expand().Border(wxALL, 0) );
	leftPaneSizer->Show( gatePalette );
	miniMap = new klsMiniMap(this, wxID_ANY, wxDefaultPosition, wxSize(130, 100));
	leftPaneSizer->Add( miniMap, wxSizerFlags(0).Expand().Border(wxALL, 0) );
	mainSizer->Add( leftPaneSizer, wxSizerFlags(0).Expand().Border(wxALL, 0) );
	
	// set up the panel and make canvases
	gCircuit = new GUICircuit();
	commandProcessor = new wxCommandProcessor();
	gCircuit->SetCommandProcessor(commandProcessor);
	gCircuit->GetCommandProcessor()->SetEditMenu(editMenu);
	gCircuit->GetCommandProcessor()->Initialize();

	// Create splitter for canvasBook (top) and oscope (bottom)
	rightSplitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_3D | wxSP_LIVE_UPDATE);
	rightSplitter->SetMinimumPaneSize(100);

	// The tab strip lives inside a splitter, so a second canvas can sit beside
	// it without the rest of the window knowing anything changed.
	canvasSplit = new wxSplitterWindow(rightSplitter, wxID_ANY, wxDefaultPosition,
	                                   wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
	canvasSplit->SetMinimumPaneSize(220);
	canvasSplit->SetSashGravity(0.5);
	usingClassicTabs = appConfig().appSettings.classicTabs;
	buildPane(0);
	canvasBook = panes[0].book;
	// Hidden for good; see the member's comment.
	canvasParking = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(1, 1));
	canvasParking->Hide();

	//add 1 tab: Left loop to allow for different default
	for (int i = 0; i < 1; i++) {
		canvases.push_back(new GUICanvas(canvasBook, gCircuit, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS));
		wxString oss;
		oss << "Page " << (i+1);
		canvasBook->AddPage(canvases[i], oss);
	}

	currentCanvas = canvases[0];
	gCircuit->setCurrentCanvas(currentCanvas);
	currentCanvas->setMinimap(miniMap);
	currentCanvas->SetFocus();
	// Focus set before the window shows can end up in the palette's search
	// field instead, so keys like Cmd+A missed the canvas on launch.
	Bind(wxEVT_SHOW, [this](wxShowEvent& e) {
		if (e.IsShown()) CallAfter([this]() { if (currentCanvas) currentCanvas->SetFocus(); });
		e.Skip();
	});
	noteCanvasUsed(currentCanvas);

	Bind(wxEVT_MENU, [this](wxCommandEvent&) {
		if (currentCanvas) { currentCanvas->setZoomAll(); currentCanvas->Refresh(); }
	}, View_ZoomFit);
	Bind(wxEVT_MENU, [this](wxCommandEvent&) {
		if (currentCanvas) currentCanvas->duplicateSelection();
	}, Edit_Duplicate);
	Bind(wxEVT_MENU, [this](wxCommandEvent&) {
		// A text field with focus (a search box, a name) keeps its own Select All.
		// A read-only or hidden one (focus can land there at launch) doesn't
		// count -- Cmd+A then seemed to do nothing.
		wxWindow* focus = wxWindow::FindFocus();
		wxTextEntry* text = dynamic_cast<wxTextEntry*>(focus);
		if (text && text->IsEditable() && focus->IsShownOnScreen()) { text->SelectAll(); return; }
		if (currentCanvas) currentCanvas->selectAll();
	}, wxID_SELECTALL);
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { SetSimView(!IsSimView()); }, View_SimView);
	Bind(wxEVT_MENU, &MainFrame::OnTruthTable, this, View_TruthTable);
	Bind(wxEVT_MENU, &MainFrame::OnImport, this, File_Import);
	Bind(wxEVT_MENU, &MainFrame::OnRenameCircuit, this, File_Rename);
	Bind(wxEVT_MENU, &MainFrame::OnVersionHistory, this, File_VersionHistory);
	Bind(wxEVT_MENU, &MainFrame::OnCloseCircuit, this, File_CloseCircuit);
	Bind(wxEVT_TOOL, [this](wxCommandEvent&) { SetSimView(!IsSimView()); }, Tool_SimView);
	Bind(wxEVT_MENU, [this](wxCommandEvent&) {
		if (currentCanvas) currentCanvas->animateZoomTo(DEFAULT_ZOOM);
	}, View_ZoomActual);
	Bind(wxEVT_MENU, [this](wxCommandEvent& e) {
		// Focus mode: the canvas gets the whole window, and the side panel
		// slides out of the way rather than blinking out of existence.
		animateSidePanel(!e.IsChecked());
	}, View_FocusMode);

	statusTimer = new wxTimer(this, wxWindow::NewControlId());
	Bind(wxEVT_TIMER, [this](wxTimerEvent&) { UpdateStatusInfo(); }, statusTimer->GetId());
	statusTimer->Start(100);

	tabSwitchTimer = new wxTimer(this, wxWindow::NewControlId());
	Bind(wxEVT_TIMER, &MainFrame::OnTabSwitchTimer, this, tabSwitchTimer->GetId());
	closeTabTimer = new wxTimer(this, wxWindow::NewControlId());
	Bind(wxEVT_TIMER, [this](wxTimerEvent&) { flushPendingClose(); }, closeTabTimer->GetId());
#ifdef __APPLE__
	MacTabSwitcher_InstallKeyMonitor(
		[this](bool shift) {
			if (!IsActive()) return false;
			handleTabSwitchKey(shift);
			return true;
		},
		[this]() {
			if (!tabSwitchActive) return false;
			cancelTabSwitch();
			return true;
		});
#endif

	// Initialize splitter showing only canvasBook (oscope hidden)
	canvasSplit->Initialize(panes[0].host);
	rightSplitter->Initialize(canvasSplit);
	// A thin divider to drag the side panel wider or narrower.
	sidePanelSash = new wxWindow(this, wxID_ANY, wxDefaultPosition, wxSize(5, -1));
	sidePanelSash->SetCursor(wxCursor(wxCURSOR_SIZEWE));
	sidePanelSash->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { if (!sidePanelSash->HasCapture()) sidePanelSash->CaptureMouse(); });
	sidePanelSash->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent&) {
		if (sidePanelSash->HasCapture()) sidePanelSash->ReleaseMouse();
		saveSettings();   // the width you dragged to is the one you get next time
	});
	sidePanelSash->Bind(wxEVT_MOUSE_CAPTURE_LOST, [](wxMouseCaptureLostEvent&) {});
	sidePanelSash->Bind(wxEVT_MOTION, [this](wxMouseEvent& e) {
		if (!sidePanelSash->HasCapture() || !e.LeftIsDown()) return;
		const int x = ScreenToClient(wxGetMousePosition()).x - gatePalette->GetPosition().x;
		const int w = wxMax(SIDE_PANEL_MIN_WIDTH, wxMin(SIDE_PANEL_MAX_WIDTH, x));
		if (w == appConfig().appSettings.sidePanelWidth) return;
		appConfig().appSettings.sidePanelWidth = w;
		ApplySidePanelWidth();
	});
	mainSizer->Add( sidePanelSash, wxSizerFlags(0).Expand() );
	mainSizer->Add( rightSplitter, wxSizerFlags(1).Expand().Border(wxALL, 0) );

	// Whichever window holds the mouse grab gets every click in the app. One
	// left behind makes the toolbar, the Oscope and Preferences all ignore
	// clicks until a relaunch, so check for one regularly and on activation.
	captureWatchdog = new wxTimer(this);
	Bind(wxEVT_TIMER, [this](wxTimerEvent&) { releaseStaleCapture(); }, captureWatchdog->GetId());
	captureWatchdog->Start(1000);
	Bind(wxEVT_ACTIVATE, [this](wxActivateEvent& e) { releaseStaleCapture(); e.Skip(); });

	modernBar = new ModernToolbar(this, this);
	rootSizer = new wxBoxSizer(wxVERTICAL);
	rootSizer->Add(modernBar, 0, wxEXPAND);
	rootSizer->Add(mainSizer, 1, wxEXPAND);
	SetSizer( rootSizer );
	ApplySidePanelWidth();
		
	threadLogic *thread = CreateThread();
	
    if ( thread->Run() != wxTHREAD_NO_ERROR )
    {
       wxLogError("Can't start thread!");
    }
	
	simTimer = new wxTimer(this, TIMER_ID);
	idleTimer = new wxTimer(this, IDLETIMER_ID);
	stopTimers();

	// The cadence pump (see MainFrame.h). One event in flight at a time, so a
	// busy GUI thread cannot accumulate a backlog of pump events.
	// Bound here, but neither the timers nor the thread start until the end of
	// the constructor: a pump event runs drainLogicMessages, which opens the
	// file named on the command line, and offerRecovery below puts up a modal
	// dialog whose nested event loop would dispatch exactly that. The circuit
	// would be torn down and rebuilt underneath the recovery prompt.
	Bind(wxEVT_THREAD, &MainFrame::OnSimPump, this, ID_SIM_PUMP);

	// Setup the "Maximize Catch" flag:
	sizeChanged = false;
	
	oscopePanel = new OscopeFrame(rightSplitter, gCircuit);
	oscopePanel->Hide();
	gCircuit->setOscope(oscopePanel);
	
	toolBar->Realize();

	// Create the print data object:
	g_printData = new wxPrintData;
	g_printData->SetOrientation(wxLANDSCAPE);
	
	this->SetSize( appConfig().appSettings.mainFrameLeft, appConfig().appSettings.mainFrameTop, appConfig().appSettings.mainFrameWidth, appConfig().appSettings.mainFrameHeight );

	// Page changes are bound per book, in buildPane. Close and Reopen are on
	// every platform: no tab bar here has a close button of the system's own.
	Bind(wxEVT_MENU, &MainFrame::OnCloseTab, this, Tool_CloseTab);
	Bind(wxEVT_MENU, &MainFrame::OnReopenTab, this, Tool_ReopenTab);
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowWelcome(this, false); }, Help_Welcome);
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowWelcome(this, true); }, Help_SetUp);
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { StartTutorial(this); }, Help_Tour);
	Bind(wxEVT_MENU, &MainFrame::OnSplitRight, this, Tool_SplitRight);
	Bind(wxEVT_MENU, &MainFrame::OnSplitClose, this, Tool_SplitClose);
	Bind(wxEVT_MENU, &MainFrame::OnFocusOtherPane, this, Tool_FocusOtherPane);
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { NudgeSplitSash(60); }, Tool_SplitWider);
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { NudgeSplitSash(-60); }, Tool_SplitNarrower);
	// The split-resize keys go through the frame's CHAR_HOOK like every other
	// shortcut here. They used to be an accelerator table, which on this frame
	// replaces the menu accelerators wholesale -- taking Cmd+Z with it.

	// Paint the theme MainApp resolved at launch (see MainApp::loadSettings).
	// Everything it touches -- canvases, minimap, oscope panel, macOS chrome --
	// exists by now, unlike earlier in this constructor.
	ApplyTheme();
	ApplyToolbarStyle();
	RenumberTabs();   // first paint of the tab strip

	// First launch: the welcome, once the window is actually on screen so it
	// has something to sit in front of.
	if (!appConfig().appSettings.hasSeenWelcome && !renderMode().headlessRender) {
		CallAfter([this] { ShowWelcome(this, false); });
	}

	if (appConfig().appSettings.mainFrameMaximized && !renderMode().headlessRender) Maximize();

	// Show the main window. On Windows it fades in: the first few layout passes
	// (toolbar, tab strip, side panel, the circuit framing itself) happen after
	// Show, and without a fade you watch them happen. macOS animates a new
	// window itself.
#ifdef __WXMSW__
	const bool fadeIn = !renderMode().headlessRender && CanSetTransparent();
	if (fadeIn) SetTransparent(0);
#endif
	Show(true);
#ifdef __WXMSW__
	if (fadeIn) {
		wxTimer* fade = new wxTimer(this, wxWindow::NewControlId());
		const wxLongLong start = wxGetLocalTimeMillis() + 60;   // let layout settle
		Bind(wxEVT_TIMER, [this, fade, start](wxTimerEvent&) {
			const double t = (wxGetLocalTimeMillis() - start).ToDouble() / 180.0;
			if (t < 0) return;
			if (t >= 1.0) {
				fade->Stop();
				SetTransparent(255);   // back to a plain, unlayered window
				CallAfter([fade] { delete fade; });
				return;
			}
			const double e = 1.0 - std::pow(1.0 - t, 3.0);   // ease out
			SetTransparent((wxByte)std::lround(e * 254));
		}, fade->GetId());
		fade->Start(15);
	}
#endif

#ifdef __WXOSX__
	NativeWindow_ConfigureTitleBar(this);
#endif

	// A headless render (--render and friends) loads its file itself and must
	// leave the library alone. Handing it the file here as well had the pump
	// import it: every Version History preview added the version it drew to
	// Your Circuits as a new circuit.
	doOpenFile = (cmdFilename.size() > 0) && !renderMode().headlessRender;
	this->openedFilename = cmdFilename;
	// Reopen whatever was open at quit (unless a file was handed to us).
	if (cmdFilename.empty() && !renderMode().headlessRender &&
	    library::exists(appConfig().appSettings.lastLibraryDoc))
		pendingLibraryOpen = appConfig().appSettings.lastLibraryDoc;
	updateDocumentTitle();

	offerRecovery();

	// Now that the recovery prompt has been answered, let the clock run. Doing
	// this earlier let a pump event load a file while the prompt was still open.
	startTimers(TIMER_POLL_MS);
	simPumpRun = true;
	simPumpThread = std::thread([this]() {
		while (simPumpRun.load()) {
			// Poll well under the step interval so a step fires close to when it is
			// actually due; a coarse poll is what quantised the cadence (see OnTimer).
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
			if (!simPumpRun.load()) break;
			if (simPumpPending.exchange(true)) continue;
			wxQueueEvent(this, new wxThreadEvent(wxEVT_THREAD, ID_SIM_PUMP));
		}
	});

	// Autosave on the GUI thread. It used to run on its own thread, which walked
	// every gate and wire to serialise them while the main thread was free to be
	// editing the same lists, and finished with a wx GUI call from off the GUI
	// thread. A timer handler runs between events, so it cannot overlap an edit
	// -- which is also why the old `handlingEvent` flag is gone: it was a check,
	// not a lock, and the main thread could start work right after it was read.
	autosaveTimer = new wxTimer(this, AUTOSAVE_TIMER_ID);
	applyAutosaveInterval();
	currentTempNum = 0;
	wxInitAllImageHandlers(); //Julian: Added to allow saving all types of image files

	// Colin: for testing dynamic gates
	//DynamicGate* dg = new DynamicGate(currentCanvas, gCircuit, gCircuit->getNextAvailableGateID(), 3, 0, 0, "AND");
}

MainFrame::~MainFrame() {
#ifdef __APPLE__
	// The app-wide key monitor outlives this window, and its callbacks point
	// into it. Empty callbacks let every key through untouched from here on.
	MacTabSwitcher_InstallKeyMonitor(nullptr, nullptr);
#endif

	saveSettings();

	// Stop the cadence pump before anything it touches goes away.
	simPumpRun = false;
	if (simPumpThread.joinable()) simPumpThread.join();

	stopTimers();
	if (autosaveTimer) autosaveTimer->Stop();
	if (captureWatchdog) captureWatchdog->Stop();
	// Ours no longer: a lock outliving the session that took it is the thing
	// everyone else's users complain about.
	documentLock.release();

	// Shut down the detached thread and wait for it to exit
	simBridge().logicThread->Delete();

	
	simBridge().m_semAllDone.Wait();
	
	
	
	// Delete the various objects
	delete wxGetApp().helpController;
	wxGetApp().helpController = NULL;
	
	
	
	//Edit by Joshua Lansford 10/18/2007.
	//Commented out the delete on the toolbar.
	//wxWidets auto deletes toolBars.  See the destructor for
	//wxFrame.
	//delete toolBar;
	
	
	delete gCircuit;
	gCircuit = NULL;
	
	//Joshua Lansford Edit 10/18/07
	//Removed the delete of systemTime because it was causeing a
	//crash on close.  In stead, I changed it from a pointer
	//to a local var so that it would not need to be deleted.

	delete simTimer;
	simTimer = NULL;
	delete idleTimer;
	idleTimer = NULL;
	delete g_printData;
	g_printData = NULL;
}

threadLogic *MainFrame::CreateThread()
{
	threadLogic *thread = new threadLogic();
    if ( thread->Create() != wxTHREAD_NO_ERROR )
    {
        wxLogError("Can't create thread!");
    }

    wxCriticalSectionLocker enter(simBridge().m_critsect);
	simBridge().logicThread = thread;
	
    return thread;
}



// event handlers

void MainFrame::OnClose(wxCloseEvent& event) {
	//Edit by Joshua Lansford 10/18/07
	//Calling Destroy is not what was crashing the system.
	//Deleting simBridge().appSystemTime in MainFrame::~MainFrame
	//was crashing the system.  This problem was solved by
	//replaceing the pointer appSystemTime with the non pointer
	//appSystemTime.  Now it doesn't need to be deleted, and
	//the system doesn't crash on close.
	
	//If Destroy is replaced with Close, then you get in
	//an infinite loop because Close calls this method we
	//are in right now.

	//This synopsis is wrong... See comment above. ~JEL 10/18/07
	
	// The call to destroy the application window was causing abnormal
	// termination.  I'm not sure why, but I'm guessing that after the
	// window was destroyed, there was another reference to the window
	// object (or one of its children) as the application closed down.
	// I modified this event handler to note the destroy request with
	// the static boolean variable below, and then in the presence of
	// such a request close the window.  This more gentle manner of
	// termination seems to allow time for whatever needs to clean up
	// so that the application terminates normally.  KAS 4/26/07
	static bool destroy = false;

	pauseTimers();

	// A tab still dimming on its way out is closed for real first, so it
	// is not saved -- and reopened next launch -- after the user closed it.
	flushPendingClose();

	// No "save?" prompt: the circuit saves itself into the library, with a
	// version for this session. Only if that save fails is there anything to
	// ask -- and then staying keeps the work on screen, unless the system is
	// shutting down and will not wait.
	if (!destroy) {
		if (!saveBeforeLeaving() && event.CanVeto()) {
			resumeTimers(TIMER_POLL_MS);
			event.Veto();
			return;
		}
		if (!libraryId.empty()) library::snapshot(libraryId);
	}
	destroy = true;      // postpone destruction until wxWidgets cleans up, KAS 4/26/07

	resumeTimers(TIMER_POLL_MS);

	if (destroy)
	{
		DismissPreferencesWindow();
		// These timers belong to this frame and would fire into it after it
		// is gone.
		if (statusTimer) statusTimer->Stop();
		if (closeTabTimer) closeTabTimer->Stop();
		cancelTabSwitch();
		removeTempFile();
	}
	else
	{
	}

	//Edit by Joshua Lansford 10/18/07
	//KAS replaced the destroy method with a close method.
	//However the close method simply calls the method we
	//are currently in.  This causes an ininitue loop which
	//wxWidgets detects and terminates.
	//While this did remove the crash on close, it did so
	//by makeing the program simple give up before it ever got
	//to the crashing code.  The destructor of the MainFrame
	//never was being called and the save settings function
	//in it never was being called.
	//See 
	//http://www.wxwidgets.org/manuals/stable/wx_windowdeletionoverview.html
	if (destroy) this->Destroy();
}

void MainFrame::OnQuit(wxCommandEvent& WXUNUSED(event)) {
    // true is to force the frame to close, so pass false to allow OnClose to handle
    Close(false);
}

void MainFrame::OnAbout(wxCommandEvent& WXUNUSED(event)) {
    // The native About panel on macOS: app icon, name, version and credits
    // laid out the way every other Mac app shows them.
    wxAboutDialogInfo info;
    info.SetName("CedarLogic");
    info.SetVersion(VERSION_NUMBER(), "Version " + VERSION_NUMBER_STRING());
#ifdef __WXOSX__
    info.SetDescription("A digital logic simulator, redesigned for the Mac by Claude.");
#else
    info.SetDescription("A digital logic simulator, redesigned by Claude.");
#endif
    info.SetCopyright(wxString::FromUTF8("\u00A9 2026 " CEDARLOGIC_PUBLISHER
        ". Based on CedarLogic by Cedarville University\n"
        "and Kieran Klukas's modernized CedarLogic."));
    // The redesign is Claude's work (Anthropic's AI model), so it gets the credit.
    info.AddDeveloper("Claude (Anthropic)");
    info.AddDeveloper("Kieran Klukas");
    info.AddDeveloper("Cedarville University CedarLogic contributors");
    info.SetLicence("GNU General Public License v3.0");
    wxAboutBox(info, this);
}

void MainFrame::OnNew(wxCommandEvent& WXUNUSED(event)) {
	// Nothing to ask: the current circuit is saved in the library.
	if (!saveBeforeLeaving()) return;
	clearToNewCircuit();
}

void MainFrame::clearToNewCircuit() {
	pauseTimers();

	// Wait for any batch the logic thread already swapped out of dGUItoLOGIC,
	// then replace the GUI and core state while that thread is excluded. Merely
	// clearing the deques is insufficient: an old step can be running locally
	// and otherwise publish DONESTEP/wire states after IDs are reused.
	{
		wxCriticalSectionLocker quiesce(simBridge().m_critsect);
		{
			wxMutexLocker lock(simBridge().mexMessages);
			simBridge().dGUItoLOGIC.clear();
			simBridge().dLOGICtoGUI.clear();
		}

		for (GUICanvas* canvas : canvases) canvas->clearCircuit();
		gCircuit->reInitializeLogicCircuit();
		commandProcessor->ClearCommands();
		commandProcessor->SetMenuStrings();
		// New starts with one tab.
		dropExtraTabs();
	}

	// DeletePage destroys those canvas windows, and three things were still
	// pointing at them: this frame's current canvas, the circuit's idea of the
	// current canvas, and the minimap's borrowed gate and wire lists. Starting
	// a new circuit from any tab but the first then drew through freed memory.
	// The open path below does exactly this; new never did.
	currentCanvas = canvases[0];
	gCircuit->setCurrentCanvas(currentCanvas);
	currentCanvas->setMinimap(miniMap);
	RenumberTabs();

	currentCanvas->Update(); // Render();
	removeTempFile();
	currentTempNum++;
    openedFilename = "";
	recoveredFrom = "";
	recoveredUnsaved = false;
	loadedFileFormat = 3;  // a fresh circuit saves as v3
	saveFormatDecided = false;
	libraryId.clear();
	appConfig().appSettings.lastLibraryDoc.clear();
	updateDocumentTitle();

	resumeTimers(TIMER_POLL_MS);

}

void MainFrame::OnOpen(wxCommandEvent& event) {
	const LibraryChoice choice = ShowLibraryDialog(this, libraryId);
	if (choice.action == LibraryChoice::Import) OnImport(event);
	else if (choice.action == LibraryChoice::Open && choice.id != libraryId) openLibraryCircuit(choice.id);
	updateDocumentTitle();   // it may have been renamed in there
}

void MainFrame::OnImport(wxCommandEvent& WXUNUSED(event)) {
	wxFileDialog dialog(this, "Import a circuit", wxEmptyString, wxEmptyString,
	                    "Circuit files (*.cdl)|*.cdl", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	dialog.SetDirectory(lastDirectory);
	if (dialog.ShowModal() != wxID_OK) return;
	lastDirectory = dialog.GetDirectory();
	importCircuitFile(dialog.GetPath());
}

bool MainFrame::importCircuitFile(const wxString& path) {
	// A headless render is a read-only pass; nothing it does belongs in the
	// user's library.
	if (renderMode().headlessRender) return false;
	cl::LoadResult check;
	std::string error;
	if (!CircuitParse::readCircuit(path.ToStdString(), check, error)) {
		if (!renderMode().headlessRender)
			ui::Message(wxString(error), "Import Error", wxOK | wxICON_ERROR, this);
		return false;
	}
	// A copy: the file on disk is never touched again.
	const std::string id = library::create(wxFileName(path).GetName());
	if (!wxCopyFile(path, library::circuitPath(id), true)) {
		library::remove(id);
		ui::Message("Couldn't copy that file into your circuits.", "Import Error", wxOK | wxICON_ERROR, this);
		return false;
	}
	library::snapshot(id);   // the original, as it came in
	return openLibraryCircuit(id);
}

bool MainFrame::openLibraryCircuit(const std::string& id) {
	if (!library::exists(id)) return false;
	if (!saveBeforeLeaving()) return false;
	currentCanvas->getCircuit()->setSimulate(false);
	pauseTimers();
	const bool ok = loadCircuitFile(library::circuitPath(id).ToStdString(), false);
	if (ok) {
		libraryId = id;
		lastSnapshotMs = 0;
		appConfig().appSettings.lastLibraryDoc = id;
	}
	currentCanvas->Update();
	currentCanvas->getCircuit()->setSimulate(true);
	resumeTimers(TIMER_POLL_MS);
	updateDocumentTitle();
	return ok;
}

bool MainFrame::saveToLibrary(bool explicitSave) {
	// A headless render shares the user's library folder but is only there to
	// draw a picture; it never writes to it.
	if (renderMode().headlessRender) return false;

	// A tab the user just closed is still on screen for a moment while it
	// dims. It is gone as far as they are concerned, so it goes before the save.
	flushPendingClose();

	// A new circuit gets its library entry the first time there's something
	// to keep.
	if (libraryId.empty() || !library::exists(libraryId)) {
		const wxString name = recoveredFrom.empty() ? library::nextUntitledName()
		                                            : wxFileName(recoveredFrom).GetName();
		libraryId = library::create(name);
		openedFilename = library::circuitPath(libraryId);
		documentLock.acquire(openedFilename.ToStdString());
	}
	if (!save(openedFilename.ToStdString(), 3)) {
		if (explicitSave)
			ui::Message("Couldn't save:\n\n" + lastSaveError, "Save Error", wxOK | wxICON_ERROR, this);
		return false;
	}
	commandProcessor->MarkAsSaved();
	recoveredUnsaved = false;
	removeTempFile();
	// A version on every Cmd+S, and every few minutes of autosaving.
	const wxLongLong now = wxGetLocalTimeMillis();
	if (explicitSave || now - lastSnapshotMs > 5 * 60 * 1000) {
		library::snapshot(libraryId);
		lastSnapshotMs = now;
	}
	appConfig().appSettings.lastLibraryDoc = libraryId;
	updateDocumentTitle();
	return true;
}

bool MainFrame::saveBeforeLeaving() {
	if (!fileIsDirty()) return true;
	if (saveToLibrary(false)) return true;
	if (renderMode().headlessRender) return true;   // nobody to ask
	ui::MessageDialog ask(this,
		"Your latest changes to this circuit couldn't be saved.",
		"Couldn't Save", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
	ask.SetExtendedMessage(wxString(lastSaveError) +
		"\n\nCancel keeps them on screen, so you can free up space and try again, "
		"or use File > Export as CedarLogic File to save a copy somewhere else.");
	ask.SetYesNoLabels("Discard Changes", "Cancel");
	return ask.ShowModal() == wxID_YES;
}

void MainFrame::updateDocumentTitle() {
	SetTitle(libraryId.empty() ? wxString("Untitled") : library::name(libraryId));
}

void MainFrame::OnRenameCircuit(wxCommandEvent& WXUNUSED(event)) {
	if (libraryId.empty() && !saveToLibrary(true)) return;
	wxTextEntryDialog ask(this, "Name:", "Rename Circuit", library::name(libraryId));
	if (ask.ShowModal() != wxID_OK || ask.GetValue().Strip(wxString::both).empty()) return;
	library::rename(libraryId, ask.GetValue());
	updateDocumentTitle();
}

void MainFrame::OnVersionHistory(wxCommandEvent& WXUNUSED(event)) {
	if (libraryId.empty() && !saveToLibrary(true)) return;
	const wxString version = ShowVersionHistoryDialog(this, libraryId);
	if (version.empty()) return;
	// Keep what's on screen as a version first, so restoring loses nothing --
	// and if that cannot be done, do not restore over it. (saveToLibrary has
	// already said why.)
	if (!saveToLibrary(true)) return;
	const std::string id = libraryId;
	// Check the version reads before it replaces the circuit: a copy that then
	// fails to load would leave the library holding something that can't open.
	{
		cl::LoadResult check;
		std::string error;
		if (!CircuitParse::readCircuit(version.ToStdString(), check, error)) {
			ui::Message("That version can't be opened:\n\n" + wxString(error),
			             "Restore Version", wxOK | wxICON_ERROR, this);
			return;
		}
	}
	if (!wxCopyFile(version, library::circuitPath(id), true)) {
		ui::Message("Couldn't restore that version: it could not be copied back into your circuits.",
		             "Restore Version", wxOK | wxICON_ERROR, this);
		return;
	}
	commandProcessor->MarkAsSaved();   // don't save the old contents back over it
	loadCircuitFile(library::circuitPath(id).ToStdString(), false);
	libraryId = id;
	updateDocumentTitle();
	SetStatusText("Restored the version from " + wxFileName(version).GetName());
}

void MainFrame::OnCloseCircuit(wxCommandEvent& WXUNUSED(event)) {
	if (!saveBeforeLeaving()) return;
	clearToNewCircuit();   // it stays in the library, but won't reopen at launch
}

//Edit by Joshua Lansford 2/15/07
//Purpose of edit:  by obstracting the loading of
//circuit files out of the onOpen rutine,
//I can now make it so that if a circuit file
//is specified as an argument to cedarls when
//it starts, that cedarls can load that file
//by calling this method.
void MainFrame::loadCircuitFile( string fileName ){
	loadCircuitFile(fileName, false);
}

bool MainFrame::loadCircuitFile( string fileName, bool asCopy ){
	wxString path = fileName;

	// Someone else editing this? Advisory only -- we can still open it, and
	// still save over it. The point is that both people find out now rather
	// than by losing an afternoon's work to whoever saves last.
	// Reopening the file we already have open -- restoring a version, say --
	// would otherwise find our own lock and warn about ourselves.
	const bool reopeningOurs = (path == openedFilename);
	const std::string holder = (asCopy || reopeningOurs) ? std::string() : FileLock::heldBy(fileName);
	if (!holder.empty() && !renderMode().headlessRender) {
		ui::MessageDialog dialog(this,
			"Someone else is editing this circuit right now.",
			"Open a Copy?", wxYES_NO | wxCANCEL | wxYES_DEFAULT | wxICON_QUESTION);
		dialog.SetExtendedMessage(
			wxString(holder) + " has it open.\n\n"
			"Working on a copy is the safe choice: your changes save separately "
			"and their work stays untouched. Opening the original means whoever "
			"saves last overwrites the other.");
		dialog.SetYesNoCancelLabels("Open a Copy", "Open the Original", "Cancel");
		const int answer = dialog.ShowModal();
		if (answer == wxID_CANCEL) return false;
		if (answer == wxID_YES) {
			// A copy: load the contents but hold no path, so Save goes to Save
			// As and cannot land on the file the other session is editing.
			return loadCircuitFile(fileName, /*asCopy=*/true);
		}
	}

	// Read and validate before touching the open document. Everything below this
	// point is destructive -- it clears the canvases and rebinds the window to the
	// new path -- so a file we cannot open has to fail here, while the user's
	// circuit is still on screen and still bound to its own filename.
	cl::LoadResult loaded;
	string loadError;
	if (!CircuitParse::readCircuit(path.ToStdString(), loaded, loadError)) {
		if (!renderMode().headlessRender)
			ui::Message(wxString(loadError), "Load Error", wxOK | wxICON_ERROR, this);
		return false;
	}

	openedFilename = asCopy ? "" : path;
	recoveredFrom = "";   // whatever was recovered before, this is not it
	recoveredUnsaved = false;
	// Not in a headless render: that is a read-only pass over the file, and
	// taking the lock there would stomp on whoever actually has it open.
	if (!asCopy && !renderMode().headlessRender) documentLock.acquire(fileName);
	this->SetTitle(VERSION_TITLE() + " - " +
	               (asCopy ? wxFileName(path).GetFullName() + " (copy)" : path));

	// Preserve the caller's exact timer state. Normal opens arrive with both
	// timers running, recovery happens before they start, and headless rendering
	// deliberately stops them; load must not silently change any of those modes.
	const bool restartSimTimer = simTimer && simTimer->IsRunning();
	const bool restartIdleTimer = idleTimer && idleTimer->IsRunning();
	simBridge().appSystemTime.Pause();
	stopTimers();

	// Exclude a batch already being processed by the logic thread before
	// throwing away the old GUI/core state. Once REINITIALIZE is queued, the
	// lock can be released: all subsequently loaded create/connect messages are
	// ordered behind it, and no old reply remains to cross the boundary.
	{
		wxCriticalSectionLocker quiesce(simBridge().m_critsect);
		{
			wxMutexLocker lock(simBridge().mexMessages);
			simBridge().dGUItoLOGIC.clear();
			simBridge().dLOGICtoGUI.clear();
		}
		for (GUICanvas* canvas : canvases) canvas->clearCircuit();
		gCircuit->reInitializeLogicCircuit();
		commandProcessor->ClearCommands();
		commandProcessor->SetMenuStrings();
		// Delete all but the first tab before the parsed pages are built.
		dropExtraTabs();
	}
	
    CircuitParse cirp(path.ToStdString(), canvases);
	canvases = cirp.applyLoaded(loaded);
	loadedFileFormat = cirp.getLoadedFormatCode();
	saveFormatDecided = false;  // a freshly opened file hasn't been answered yet

	//JV - Put pages back into canvas book
	for (unsigned int i = 1; i < canvases.size(); i++) 
	{
		ostringstream oss;
		oss << "Page " << (i + 1);
		canvasBook->AddPage(canvases[i], oss.str(), (i == 0 ? true : false));
	}
	currentCanvas = canvases[0];
	gCircuit->setCurrentCanvas(currentCanvas);
	currentCanvas->setMinimap(miniMap);
	mainSizer->Show(rightSplitter);
	currentCanvas->SetFocus();
	RenumberTabs();   // the loaded pages are new tabs

	if (restartSimTimer) {
		simBridge().appSystemTime.Start(0);
		simTimer->Start(TIMER_POLL_MS);
	}
	if (restartIdleTimer) idleTimer->Start(TIMER_POLL_MS);

	removeTempFile();

	// Frame the circuit the way Space does, once the notebook has laid the pages
	// out -- setZoomAll fits to the client size, and right after a load that size
	// is still whatever the pages were created with.
	if (!renderMode().headlessRender) {
		CallAfter([this]() {
			for (unsigned int i = 0; i < canvases.size(); i++) {
				if (canvases[i]->GetClientSize().GetWidth() <= 0) continue;
				canvases[i]->setZoomAll();
			}
			if (currentCanvas != NULL) currentCanvas->Update();
			Refresh();
		});
	}

	// Offer to migrate an older file to the latest format up front. Declining
	// leaves it undecided, so the same choice is offered again when they save.
	// A recovery snapshot is always current-format, so this never fires for one.
	if ((loadedFileFormat == 1 || loadedFileFormat == 2)
	    && !renderMode().headlessRender
	    && !path.StartsWith(library::root())) {   // library copies always save as V3
		wxString v = (loadedFileFormat == 1) ? "V1" : "V2";
		ui::MessageDialog dialog(this,
			"This circuit was saved in an older file format (" + v + ").\n\n"
			"Convert it to V3 now? If not, you can convert it later when you save.",
			"Older File Format", wxYES_NO | wxICON_QUESTION);
		dialog.SetYesNoLabels("Convert to V3", "Not Now");
		if (dialog.ShowModal() == wxID_YES) {
			CircuitParse saver(currentCanvas);
			if (saver.saveCircuitV3(fileName, canvases)) {
				loadedFileFormat = 3;
				saveFormatDecided = true;
				commandProcessor->MarkAsSaved();
			} else {
				ui::Message("Could not convert the file:\n\n" + saver.getLastError(),
					"Save Error", wxOK | wxICON_ERROR, this);
			}
		}
	}

	return true;
}

void MainFrame::OnSave(wxCommandEvent& WXUNUSED(event)) {
	if (saveToLibrary(true)) {
		explicitSaves++;
		SetStatusText("Saved. A version was added to Version History.");
	}
}

// Ask which format to write an old-format file in. v3/new circuits save as v3
// with no prompt; v1/v2 files prompt so opening one never silently upgrades it.
int MainFrame::chooseSaveFormat() {
	if (loadedFileFormat != 1 && loadedFileFormat != 2) return 3;
	if (saveFormatDecided) return loadedFileFormat;  // already answered for this file

	wxString v = (loadedFileFormat == 1) ? "V1" : "V2";
	ui::MessageDialog dialog(this,
		"This circuit was opened in an older file format (" + v + ").\n\n"
		"Convert it to V3, or keep " + v + "?\n\n"
		"V3 files cannot be opened by older versions of CedarLogic.",
		"Save Circuit", wxYES_NO | wxCANCEL | wxICON_QUESTION);
	dialog.SetYesNoCancelLabels("Convert to V3", "Keep " + v, "Cancel");

	int result = dialog.ShowModal();
	if (result == wxID_CANCEL) return -1;
	saveFormatDecided = true;  // don't ask again for this file
	if (result == wxID_YES) { loadedFileFormat = 3; return 3; }  // future saves stay v3
	return loadedFileFormat;  // keep the original format
}

// File > Export as CedarLogic File: a .cdl copy anywhere on disk, to share or
// hand in. The circuit itself stays in the library.
void MainFrame::OnSaveAs(wxCommandEvent& WXUNUSED(event)) {
	wxFileDialog dialog(this, "Export as CedarLogic File", wxEmptyString, GetDocumentTitle() + ".cdl",
	                    "Circuit files (*.cdl)|*.cdl", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
	dialog.SetDirectory(lastDirectory);
	if (dialog.ShowModal() != wxID_OK) return;
	lastDirectory = dialog.GetDirectory();
	if (!save(dialog.GetPath().ToStdString(), 3))
		ui::Message("Couldn't export:\n\n" + lastSaveError, "Export Error", wxOK | wxICON_ERROR, this);
}

void MainFrame::OnOscope(wxCommandEvent& WXUNUSED(event)) {
	if (rightSplitter->IsSplit()) {
		rightSplitter->Unsplit(oscopePanel);
	} else {
		oscopePanel->Show();
		// canvasSplit, not the book: the book lives inside a pane now, and a
		// splitter will only split its own children.
		rightSplitter->SplitHorizontally(canvasSplit, oscopePanel, -250);
	}
}

void MainFrame::OnViewGridline(wxCommandEvent& event) {
	appConfig().appSettings.gridlineVisible = event.IsChecked();
	if (currentCanvas != NULL) currentCanvas->Update();
}

void MainFrame::OnViewWireConn(wxCommandEvent& event) {
	appConfig().appSettings.wireConnVisible = event.IsChecked();
	if (currentCanvas != NULL) currentCanvas->Update();
}

// The theme shortcut is both a menu accelerator and a CHAR_HOOK match (the
// hook covers focus cases the menu misses). If one key press reaches both,
// only the first counts.
static bool themeToggleIsRepeat() {
	static wxLongLong last = 0;
	const wxLongLong now = wxGetLocalTimeMillis();
	const bool repeat = now - last < 150;
	if (!repeat) last = now;
	return repeat;
}

void MainFrame::OnViewDarkMode(wxCommandEvent& event) {
	if (themeToggleIsRepeat()) {
		// The menu item / toolbar switch already flipped its own check.
		ApplyTheme();
		return;
	}
	// Fires from both the View menu item and the toolbar switch; either one
	// already flipped its OWN visual state before sending this, so read that as
	// the intent and let ApplyTheme sync the other control to match.
	renderMode().darkMode = event.IsChecked();
	ApplyTheme();
}

void MainFrame::ToggleDarkMode() {
	if (themeToggleIsRepeat()) return;
	renderMode().darkMode = !renderMode().darkMode;
	ApplyTheme();
}

void MainFrame::ApplyTheme() {
	const bool dark = renderMode().darkMode;

	// The tab strip and the panel behind it are drawn by us, so they only
	// follow the theme if we tell them to.
	for (CanvasPane& p : panes) {
		if (p.host == nullptr) continue;
		p.host->SetBackgroundColour(dark ? wxColour(22, 24, 28) : wxColour(233, 234, 238));
		p.host->Refresh();
		if (p.strip) p.strip->Refresh();
	}

	if (wxMenuBar* mb = GetMenuBar()) mb->Check(View_DarkMode, dark);
	if (modernBar) ApplyToolbarStyle();
	if (toolBar->FindById(Tool_ThemeToggle) != nullptr) {
		if (toolBar->GetToolState(Tool_ThemeToggle) != dark)
			toolBar->ToggleTool(Tool_ThemeToggle, dark);
		setToolIcon(Tool_ThemeToggle, dark ? moonIcon : sunIcon, dark ? "moon.fill" : "sun.max.fill");
	}
	// A little tonal separation between the toolbar and the (near-)white canvas
	// under it -- left at native default the two were nearly indistinguishable
	// in light mode. Dark mode gets the same treatment for consistency, a shade
	// lighter than the canvas rather than matching it exactly.
	toolBar->SetBackgroundColour(dark ? wxColour(28, 31, 38) : wxColour(237, 238, 240));
	toolBar->Refresh();

#ifdef __APPLE__
	// Pin the WHOLE app's native chrome (menus, dialogs, scrollbars) to match --
	// always an explicit light/dark, never "follow system", because a manual
	// toggle here is the person overriding the OS setting for this session.
	MacSetApplicationAppearance(dark ? 2 : 1);
#elif defined(_WIN32)
	// Windows draws the caption itself; without this it stays white over a
	// dark app. Dialogs opened later pick it up in MainApp::FilterEvent.
	WinSetDarkTitlebars(dark);
	WinSetAppDarkMode(dark);          // right-click and dropdown menus
	WinThemeControls(this, dark);     // scrollbars and the palette's dropdown
	applyTitlebarForTopRow();         // the title bar takes the new colours
	PreferencesThemeChanged();        // an open Settings window follows
#endif

	// Repaint every live view: all canvas tabs (only one is visible, but a
	// background tab must be correct when its tab is picked), the minimap, and
	// the oscilloscope, which sits outside the sceneKey cache these share.
	for (GUICanvas* c : canvases) if (c) c->Refresh();
	if (miniMap) miniMap->Refresh();
	if (oscopePanel) oscopePanel->RefreshCanvas();
	if (gatePalette) gatePalette->ApplyTheme();
	if (sidePanelSash) {
		sidePanelSash->SetBackgroundColour(dark ? wxColour(40, 43, 50) : wxColour(218, 220, 224));
		sidePanelSash->Refresh();
	}
	Refresh();
}

void MainFrame::ApplyThemeShortcutLabel() {
	wxMenuBar* mb = GetMenuBar();
	if (!mb) return;
	wxMenuItem* item = mb->FindItem(View_DarkMode);
	if (!item) return;
	const auto& s = appConfig().appSettings;
	// No "\t..." text: wx's accelerator parser doesn't know "Cmd", so the text
	// "Shift+Cmd+D" registered a second, bare Shift+D shortcut. Build the
	// entry from flags instead.
	item->SetItemLabel("&Dark Mode");
	if (!s.themeShortcutEnabled) return;
	const int m = s.themeShortcutModifiers;
	int flags = 0;
	if (m & ThemeShortcutMod::Shift) flags |= wxACCEL_SHIFT;
	if (m & ThemeShortcutMod::Alt)   flags |= wxACCEL_ALT;
#ifdef __WXOSX__
	if (m & ThemeShortcutMod::Ctrl)  flags |= wxACCEL_RAW_CTRL;
	if (m & ThemeShortcutMod::Meta)  flags |= wxACCEL_CTRL;   // wx's Ctrl is Cmd on macOS
#else
	if (m & ThemeShortcutMod::Ctrl)  flags |= wxACCEL_CTRL;
	// The Windows key can't be a menu accelerator; CHAR_HOOK still handles it.
	if (m & ThemeShortcutMod::Meta) return;
#endif
	wxAcceleratorEntry accel(flags, s.themeShortcutKeyCode, View_DarkMode);
	item->SetAccel(&accel);
}

void MainFrame::ApplyThemeToggleVisibility() {
	// The same "Dark mode" group the modern toolbars show or hide, so one
	// checkbox in the Toolbar settings covers every style.
	auto& settings = appConfig().appSettings;
	settings.showThemeToggleButton = !(settings.toolbarHidden & (1 << cl::tb::GTheme));
	const bool want = settings.showThemeToggleButton;
	const bool have = toolBar->FindById(Tool_ThemeToggle) != nullptr;
	if (want == have) return;
	if (want) {
		const bool dark = renderMode().darkMode;
		// Back in its original slot, between the separators before About --
		// AddTool would put it at the far end of the bar.
		const int aboutPos = toolBar->GetToolPos(wxID_ABOUT);
		const size_t pos = aboutPos > 0 ? (size_t)(aboutPos - 1) : toolBar->GetToolsCount();
		toolBar->InsertTool(pos, Tool_ThemeToggle, "Dark Mode", dark ? moonIcon : sunIcon,
		                    wxNullBitmap, wxITEM_CHECK, "Toggle dark mode");
		toolBar->Realize();
		toolBar->ToggleTool(Tool_ThemeToggle, dark);
		// Same native symbol ApplyTheme uses, or it comes back tinted wrong.
		setToolIcon(Tool_ThemeToggle, dark ? moonIcon : sunIcon, dark ? "moon.fill" : "sun.max.fill");
	} else {
		toolBar->DeleteTool(Tool_ThemeToggle);
	}
}

void MainFrame::OnPreferences(wxCommandEvent& event) {
	ShowPreferencesWindow(this);
#ifdef __APPLE__
	// Preferences is a window of its own, and macOS follows a window to
	// whatever space it lives in -- which dropped a full-screen session back
	// onto the desktop. Let it join the space we are already in instead. It
	// has to happen after the window exists, hence here rather than at setup.
	CallAfter([this] { MacKeepPanelsOnActiveSpace(MacGetTopLevelWindowRef()); });
#endif
}

void MainFrame::ApplyPreferences() {
	applyAutosaveInterval();
	ApplyStatusInfoVisibility();
	ApplyToolbarStyle();
	rebuildTabUi();   // no-op unless the tab bar setting actually changed
	gatePalette->ApplyGateSize();
	ApplyThemeShortcutLabel();
	ApplyThemeToggleVisibility();

	// The same two settings are reachable from the View menu; keep its
	// checkmarks in step with Preferences.
	GetMenuBar()->Check(View_Gridline, appConfig().appSettings.gridlineVisible);
	GetMenuBar()->Check(View_WireConn, appConfig().appSettings.wireConnVisible);

	// Repaint everything a setting can colour or resize -- the accent is the
	// dot on the active tab, the selection glow and the palette's highlight.
	// Update() alone only flushes a repaint that is already pending, so the
	// tab dot kept its old colour until you switched tabs.
	for (CanvasPane& p : panes) {
		if (p.host) p.host->Refresh();
		if (p.strip) p.strip->Refresh();
	}
	for (GUICanvas* c : canvases) if (c) c->Refresh();
	if (miniMap) miniMap->Refresh();
	if (gatePalette) gatePalette->Refresh();
	if (modernBar) modernBar->Refresh();
	saveSettings();   // a change made in Preferences is on disk right away
}

// Cadence pump event (posted from simPumpThread). Runs the same work the two
// wxTimers do, but arrives as a normal queued message so mouse/paint traffic
// can't starve it. Gated on the timers actually running, so pause/resume and the
// panic stop still control whether the sim advances. Both handlers are already
// self-limiting (OnTimer no-ops until refreshRate has elapsed; OnIdle just drains
// whatever the logic thread has queued), so the extra calls are cheap no-ops.
void MainFrame::OnSimPump(wxThreadEvent& WXUNUSED(event)) {
	simPumpPending = false;
	if (simTimer && simTimer->IsRunning())   stepSimulation();
	if (idleTimer && idleTimer->IsRunning()) drainLogicMessages();
}

void MainFrame::OnTimer(wxTimerEvent& event) { stepSimulation(); }

void MainFrame::stepSimulation() {
	if (!(currentCanvas->getCircuit()->getSimulate())) return;
	// Step as soon as a whole step's worth of wall time has accrued. Gating on
	// refreshRate instead quantised the sim to the GUI poll interval: a 25ms step
	// could only land on a ~16ms grid, giving alternating 16/32ms gaps forever.
	// Since the oscilloscope plots one sample per step, that 2:1 jitter was
	// exactly the uneven trace (and the uneven clock on the canvas).
	if (simBridge().appSystemTime.Time() < appConfig().timeStepMod) return;
	simBridge().appSystemTime.Pause();
	if (gCircuit->panic) return;

	const long step = appConfig().timeStepMod;
	long elapsed = simBridge().appSystemTime.Time();

	// Cap how much wall time a single step is allowed to make up. The clock runs
	// whether or not the app does, so backgrounding the window or sleeping the
	// machine hands the next step the whole gap at once. Simulating minutes of
	// circuit takes longer than the minutes took to pass, which is precisely what
	// the overload check measures, so an uncapped catch-up reported an overload
	// every time the user came back. Past the cap, drop the backlog instead: the
	// simulation resumes where it left off rather than racing to a wall clock
	// nobody was watching.
	const long maxCatchUp = step * MAX_CATCHUP_STEPS;
	const bool over = elapsed > maxCatchUp;
	// A large gap has two possible causes, and they need opposite treatment. If
	// the core spent that time working, the circuit is genuinely too heavy and
	// the overload check must still see it. If the core was idle, nothing was
	// running and there is nothing to report. Compare the gap against the work.
	const bool coreWasBusy = gCircuit->lastLogicTime * 2 >= elapsed;
	const bool stalled = over && !coreWasBusy;
	if (over) elapsed = maxCatchUp;  // cap either way, so one step cannot spiral

	gCircuit->lastTime = (int)elapsed;
	gCircuit->lastTimeMod = (int)step;
	gCircuit->lastNumSteps = (int)(elapsed / step);
	gCircuit->catchingUp = stalled;
	gCircuit->sendMessageToCore(klsMessage::Message(klsMessage::MT_STEPSIM, new klsMessage::Message_STEPSIM(elapsed / step)));
	currentCanvas->getCircuit()->setSimulate(false);
	// After a dropped backlog the leftover is meaningless, so start clean.
	simBridge().appSystemTime.Start(stalled ? 0 : elapsed % step);
}

void MainFrame::OnIdle(wxTimerEvent& event) { drainLogicMessages(); }

// Run the simulation to a fixed point. See MainFrame.h.
//
// A headless render used to photograph the circuit mid-flight: load it, pump
// the event loop once, draw. How far the logic thread had got by then came down
// to thread scheduling, so the same file rendered twice could differ -- a net
// came out driven in one run and high-impedance in the next. That left the
// golden-image harness unable to tell a real change from a coin toss.
//
// Stepping to a fixed point removes the race at its source rather than papering
// over it with a sleep: keep stepping until a whole step goes by with no wire
// changing state, and the picture is of the settled circuit every time.
bool MainFrame::settleSimulation(int maxSteps) {
	if (gCircuit == nullptr) return true;

	// Take sole charge of stepping. A running sim timer would interleave steps
	// of its own with the ones below, which is the race being removed here.
	stopTimers();

	auto wireStates = [&]() {
		std::vector<StateType> snapshot;
		for (const auto &w : gCircuit->wires()) {
			if (!w.second) continue;
			for (StateType s : w.second->getState()) snapshot.push_back(s);
		}
		return snapshot;
	};

	// One synchronized step: ask for it, then wait for the core's answer.
	// getSimulate() goes back to true when MT_DONESTEP is drained, and draining
	// that is also what applies the step's wire states, so it marks the point
	// where the step is fully visible here.
	auto stepOnce = [&]() {
		gCircuit->exemptNextStepFromTiming();
		gCircuit->sendMessageToCore(klsMessage::Message(
			klsMessage::MT_STEPSIM, new klsMessage::Message_STEPSIM(1)));
		// Queue the step while simulate is still true. Otherwise the normal
		// in-flight edit gate holds the step in messageQueue waiting for the very
		// DONESTEP this step was supposed to produce.
		gCircuit->setSimulate(false);
		const wxLongLong deadline =
			wxGetLocalTimeMillis() + SETTLE_STEP_TIMEOUT_MS;
		while (!gCircuit->getSimulate() && wxGetLocalTimeMillis() < deadline) {
			drainLogicMessages();
			if (gCircuit->getSimulate()) break;
			wxMilliSleep(1);
		}
		drainLogicMessages();   // anything queued behind the DONESTEP
		return gCircuit->getSimulate();
	};

	// A step the sim timer sent before stopTimers() may still be running.
	// getSimulate() is false until its DONESTEP arrives, and stepOnce reads the
	// next DONESTEP as its own -- so that stray one would pass for the answer to
	// ours, and a truth-table row could be read before our step had run. Let it
	// land first.
	{
		const wxLongLong deadline = wxGetLocalTimeMillis() + SETTLE_STEP_TIMEOUT_MS;
		drainLogicMessages();
		while (!gCircuit->getSimulate() && wxGetLocalTimeMillis() < deadline) {
			wxMilliSleep(1);
			drainLogicMessages();
		}
		if (!gCircuit->getSimulate()) return false;   // the core stopped answering
	}

	// Apply anything the load left in flight, then let the first synchronized
	// step set the baseline. Comparing against the state as loaded would mean
	// comparing against however much had been applied by then.
	drainLogicMessages();
	if (!stepOnce()) return false;
	std::vector<StateType> before = wireStates();

	for (int step = 1; step < maxSteps; step++) {
		if (!stepOnce()) return false;             // the core stopped answering
		std::vector<StateType> after = wireStates();
		if (after == before) return true;          // a step changed nothing
		before.swap(after);
	}
	return false;   // still moving: a clock, or something that never settles
}

void MainFrame::drainLogicMessages() {
	wxCriticalSectionLocker locker(simBridge().m_critsect);
	// Take the whole pending batch under the lock, then process it with the lock
	// released. The old TryLock+wxYield spin pumped the GUI event loop while
	// waiting on the logic thread, letting a menu action (undo/redo/paste)
	// re-enter mid-drain -- the crash class #32 worked around. Holding
	// mexMessages only for the O(1) swap removes the spin and the reentrancy.
	deque< klsMessage::Message > batch;
	{
		wxMutexLocker lock(simBridge().mexMessages);
		batch.swap(simBridge().dLOGICtoGUI);
	}
	while (!batch.empty()) {
		gCircuit->parseMessage(batch.front());
		batch.pop_front();
	}

	if (mainSizer == NULL) return;
	
	if ( doOpenFile ) {
		doOpenFile = false;
		importCircuitFile(openedFilename);   // a copy, into the library
	} else if (!pendingLibraryOpen.empty()) {
		const std::string id = pendingLibraryOpen;
		pendingLibraryOpen.clear();
		openLibraryCircuit(id);
	}
	
	if ( gCircuit->panic ) {
		gCircuit->panic = false;
		toolBar->ToggleTool( Tool_Pause, true );
		setToolIcon(Tool_Pause, playIcon, "play.fill");
		simTimer->Stop();
		simBridge().appSystemTime.Start(0);
		simBridge().appSystemTime.Pause();
		//Edit by Joshua Lansford 11/24/06
		//I have overloaded the meaning of panic
		//panic is now also used to pause the system.
		//thus we don't want to shout if we are just pausing
		//This edit was made so that the Z_80LogicGate
		//can 'step' through instructions.
		//see the location were pausing is set to true
		//for further explination in GUICircuit::parseMessage
		if( !gCircuit->pausing ){
			// Say it in the status bar rather than a modal box. The simulation has
			// already stopped and the toolbar shows play, so the user can see that
			// something happened; a dialog on top of that only demands a click.
			SetStatusText("Simulation paused: the circuit needs more time per step. "
			              "Raise 'Sim step' on the toolbar, then press play.");
		}
		gCircuit->pausing = false;
	}

	if( sizeChanged ) {	
		sizeChanged = false;
		wxSizeEvent temp;
	}
}
void MainFrame::OnSize(wxSizeEvent& event) {
	if (currentCanvas != NULL) currentCanvas->Update();
	if (mainSizer != NULL) mainSizer->Layout();
}

void MainFrame::OnMaximize(wxMaximizeEvent& event) {
	// Setup the "Maximize Catch" flag:
	sizeChanged = true;
}

void MainFrame::OnNotebookPage(wxBookCtrlEvent& event) {
	const int page = event.GetSelection();
	if (page == wxNOT_FOUND || currentCanvas == NULL) return;
	// The book that changed, which with a split open is not always the first
	// pane's: its page number means nothing in any other book.
	wxBookCtrlBase* book = wxDynamicCast(event.GetEventObject(), wxBookCtrlBase);
	if (book == nullptr || page >= (int)book->GetPageCount()) return;
	GUICanvas* chosen = static_cast<GUICanvas*>(book->GetPage(page));
	if (chosen == nullptr || chosen == currentCanvas) return;
	//**********************************
	//Edit by Joshua Lansford 4/9/07
	//This edit is to make the minimap
	//only be controled by the current
	//Canvase.
	//This will avoid the minimap
	//spazing out when the mainFrame is
	//resized
	currentCanvas->setMinimap( NULL );
	//End of Edit*********************
	currentCanvas = chosen;
	gCircuit->setCurrentCanvas(currentCanvas);
	currentCanvas->setMinimap(miniMap);
	currentCanvas->SetFocus();
	currentCanvas->Update();
	noteCanvasUsed(currentCanvas);
}

void MainFrame::OnUndo(wxCommandEvent& event) {
	// Quiesce the sim timers while the command re-runs its structural edits -- the
	// same guard the New/Open/Close handlers use. Undo/redo mutate the gate/wire
	// lists and fire core messages; with a running simulation the step timer is
	// concurrently syncing wire state and repainting (MT_DONESTEP), and the two
	// race and crash. Pausing the sim by hand avoids it, and so does this.
	pauseTimers();
	// A tab closed a moment ago is closed: finish it, so this undo is the one
	// that brings it back rather than whatever came before it.
	flushPendingClose();
	// Switch to the page this command affects, so an undo on another tab is shown
	// where it happens instead of silently changing an off-screen page.
	klsCommand *cmd = (klsCommand *)commandProcessor->GetCurrentCommand();
	if (cmd != NULL) switchToCanvas(cmd->getCanvas());
	commandProcessor->Undo();
	// Tab commands can't be followed by pointer; they name a page to show after.
	if (cmd != NULL) { int p = cmd->pageToShow(true); if (p >= 0) showCanvasIndex(p); }
	resumeTimers(TIMER_POLL_MS);
	currentCanvas->Update();
}

void MainFrame::OnRedo(wxCommandEvent& event) {
	pauseTimers();
	flushPendingClose();   // see OnUndo
	// The redo target is the command just after the current position; switch to
	// its page before re-doing it (see OnUndo).
	wxList &cmds = commandProcessor->GetCommands();
	wxCommand *current = commandProcessor->GetCurrentCommand();
	klsCommand *cmd = NULL;
	if (current == NULL) {
		if (!cmds.IsEmpty()) cmd = (klsCommand *)cmds.GetFirst()->GetData();
	} else {
		wxList::compatibility_iterator node = cmds.Find(current);
		if (node && node->GetNext()) cmd = (klsCommand *)node->GetNext()->GetData();
	}
	if (cmd != NULL) switchToCanvas(cmd->getCanvas());
	commandProcessor->Redo();
	if (cmd != NULL) { int p = cmd->pageToShow(false); if (p >= 0) showCanvasIndex(p); }
	resumeTimers(TIMER_POLL_MS);
	currentCanvas->Update();
}

void MainFrame::showCanvasIndex(int idx) {
	if (idx < 0 || idx >= (int)canvases.size()) return;
	GUICanvas *target = canvases[idx];
	// ChangeSelection switches without firing a page-changed event (which would
	// re-enter mid-undo); mirror the state OnNotebookPage would set, by hand.
	// currentCanvas may be a just-removed (hidden) page here, so guard it.
	const int pane = PaneIndexOf(target);
	if (pane >= 0) {
		const int page = panes[pane].book->FindPage(target);
		if (page != wxNOT_FOUND) panes[pane].book->ChangeSelection(page);
	}
	if (currentCanvas != NULL && currentCanvas != target) currentCanvas->setMinimap(NULL);
	currentCanvas = target;
	gCircuit->setCurrentCanvas(currentCanvas);
	currentCanvas->setMinimap(miniMap);
	noteCanvasUsed(currentCanvas);
	RenumberTabs();
}

void MainFrame::OnTruthTable(wxCommandEvent& WXUNUSED(event)) {
	if (currentCanvas == nullptr) return;
	auto* gates = currentCanvas->getGateList();

	// The selected switches and lights if any are selected, else the page's.
	bool useSelection = false;
	for (auto& g : *gates)
		if (g.second->isSelected() &&
		    (dynamic_cast<guiGateTOGGLE*>(g.second) || dynamic_cast<guiGateLED*>(g.second)))
			useSelection = true;
	std::vector<guiGate*> ins, outs;
	bool sequential = false;
	for (auto& g : *gates) {
		const std::string type = g.second->getLibraryGateName();
		for (const char* seq : { "CLOCK", "FF", "LATCH", "REGISTER", "COUNTER", "RAM", "ROM" })
			if (type.find(seq) != std::string::npos) sequential = true;
		if (useSelection && !g.second->isSelected()) continue;
		if (dynamic_cast<guiGateTOGGLE*>(g.second)) ins.push_back(g.second);
		else if (dynamic_cast<guiGateLED*>(g.second)) outs.push_back(g.second);
	}
	if (ins.empty() || outs.empty()) {
		ui::Message("A truth table needs at least one switch (an input) and one light (an output)"
		             + wxString(useSelection ? " in the selection." : " on this page."),
		             "Truth Table", wxOK | wxICON_INFORMATION, this);
		return;
	}
	if (ins.size() > 8) {
		ui::Message(wxString::Format("That's %zu switches -- %s rows. Select up to 8 switches "
		             "(and the lights you care about) and try again.", ins.size(),
		             ins.size() > 16 ? "far too many" : wxString::Format("%lu", 1UL << ins.size())),
		             "Truth Table", wxOK | wxICON_INFORMATION, this);
		return;
	}

	// Columns in drawing order: top to bottom, then left to right. The first
	// input is the most significant bit.
	auto pos = [](guiGate* g) { float x, y; g->getGLcoords(x, y); return std::make_pair(x, y); };
	auto byPlace = [&](guiGate* a, guiGate* b) {
		const auto pa = pos(a), pb = pos(b);
		return pa.second != pb.second ? pa.second > pb.second : pa.first < pb.first;
	};
	std::sort(ins.begin(), ins.end(), byPlace);
	std::sort(outs.begin(), outs.end(), byPlace);

	// Names: the nearest unused text label, if one is close; otherwise A, B,
	// C... for inputs and Y (or Y1, Y2...) for outputs.
	std::vector<guiGate*> labels;
	for (auto& g : *gates)
		if (dynamic_cast<guiLabel*>(g.second) && !g.second->getGUIParam("LABEL_TEXT").empty())
			labels.push_back(g.second);
	std::vector<bool> labelUsed(labels.size(), false);
	auto nameFor = [&](guiGate* port, const wxString& fallback) {
		const auto p = pos(port);
		int best = -1;
		float bestD = 8.0f;   // world units: a couple of gate widths
		for (size_t i = 0; i < labels.size(); i++) {
			if (labelUsed[i]) continue;
			const std::string text = labels[i]->getGUIParam("LABEL_TEXT");
			if (text.size() > 16) continue;   // a note, not a name
			const auto q = pos(labels[i]);
			const float d = std::hypot(q.first - p.first, q.second - p.second);
			if (d < bestD) { bestD = d; best = (int)i; }
		}
		if (best < 0) return fallback;
		labelUsed[best] = true;
		return wxString::FromUTF8(labels[best]->getGUIParam("LABEL_TEXT").c_str());
	};
	TruthTableData data;
	data.sequential = sequential;
	for (size_t i = 0; i < ins.size(); i++) data.inputNames.push_back(nameFor(ins[i], wxString((char)('A' + i))));
	for (size_t i = 0; i < outs.size(); i++)
		data.outputNames.push_back(nameFor(outs[i], outs.size() == 1 ? wxString("Y") : wxString::Format("Y%zu", i + 1)));

	auto setSwitch = [&](guiGate* g, const std::string& v) {
		g->setLogicParam("OUTPUT_NUM", v);
		gCircuit->sendMessageToCore(klsMessage::Message(klsMessage::MT_SET_GATE_PARAM,
			new klsMessage::Message_SET_GATE_PARAM(g->getID(), "OUTPUT_NUM", v)));
	};
	auto readLight = [](guiGate* g) {
		for (auto& hs : g->getHotspotList()) {
			if (!g->isConnected(hs.first)) continue;
			const std::vector<StateType>& st = g->getConnection(hs.first)->getState();
			if (st.empty()) return 'X';
			switch (st[0]) {
				case ONE: return '1';
				case ZERO: return '0';
				case HI_Z: return 'Z';
				case CONFLICT: return '!';
				default: return 'X';
			}
		}
		return '-';
	};

	std::vector<std::string> original;
	for (guiGate* g : ins) original.push_back(g->getLogicParam("OUTPUT_NUM"));

	const int n = (int)ins.size();
	const int rows = 1 << n;
	std::unique_ptr<wxProgressDialog> progress;
	if (rows > 16)
		progress.reset(new wxProgressDialog("Truth Table", "Trying every combination...", rows, this,
		                                     wxPD_APP_MODAL | wxPD_AUTO_HIDE));
	for (int r = 0; r < rows; r++) {
		std::vector<char> row;
		for (int i = 0; i < n; i++) {
			const bool bit = (r >> (n - 1 - i)) & 1;
			setSwitch(ins[i], bit ? "1" : "0");
			row.push_back(bit ? '1' : '0');
		}
		if (!settleSimulation()) data.unsettledRows++;
		for (guiGate* g : outs) row.push_back(readLight(g));
		data.rows.push_back(row);
		if (progress) progress->Update(r + 1);
	}
	progress.reset();

	// Put the switches back the way they were.
	for (size_t i = 0; i < ins.size(); i++) setSwitch(ins[i], original[i].empty() ? "0" : original[i]);
	settleSimulation();
	resumeTimers(TIMER_POLL_MS);
	currentCanvas->Refresh();

	ShowTruthTableDialog(this, data);
}

bool MainFrame::IsSimView() const { return renderMode().simView; }

void MainFrame::SetSimView(bool on) {
	if (on == renderMode().simView || currentCanvas == nullptr) return;
	if (on) {
		currentCanvas->cancelDrag();
		currentCanvas->unselectAllGates();
		currentCanvas->unselectAllWires();
		if (IsSimPaused()) SetSimPaused(false);   // "Run" means run
	}
	renderMode().simView = on;
	GetMenuBar()->Check(View_SimView, on);
	if (toolBar->GetToolState(Tool_SimView) != on) toolBar->ToggleTool(Tool_SimView, on);
	for (GUICanvas* c : canvases) if (c) c->Refresh();
	if (modernBar) ApplyToolbarStyle();   // Seamless follows the canvas color
	currentCanvas->SetFocus();
}

bool MainFrame::IsSimPaused() { return toolBar->GetToolState(Tool_Pause); }

void MainFrame::SetSimPaused(bool paused) {
	if (IsSimPaused() == paused) return;
	toolBar->ToggleTool(Tool_Pause, paused);
	PauseSim();
	if (currentCanvas) currentCanvas->Refresh();
}

void MainFrame::StepSimOnce() {
	// Stepping only means something while paused; otherwise the running
	// simulation swallows it. So a step pauses first.
	if (!IsSimPaused()) SetSimPaused(true);
	wxCommandEvent e(wxEVT_TOOL, Tool_Step);
	ProcessWindowEvent(e);
}

int MainFrame::GetStepMs() const { return appConfig().timeStepMod; }

void MainFrame::SetStepMs(int ms) {
	ms = wxMax(timeStepModSlider->GetMin(), wxMin(timeStepModSlider->GetMax(), ms));
	timeStepModSlider->SetValue(ms);
	appConfig().timeStepMod = ms;
	wxString oss;
	oss << ms << "ms";
	timeStepModVal->SetLabel(oss);
}

// The side panel leaves the way a browser's sidebar does: it slides out past
// the left edge while the canvas grows into the space, instead of vanishing
// and letting everything else jump sideways.
//
// The windows are placed by hand for the length of the animation rather than
// re-laid-out: shrinking the palette would re-flow its gate tiles into fewer
// columns on every frame, which is the jumping all over again, only slower.
// Sliding keeps it at its full width and lets the window edge clip it.
void MainFrame::animateSidePanel(bool show) {
	if (gatePalette == nullptr || sidePanelSash == nullptr || rightSplitter == nullptr) return;

	if (sidePanelTimer == nullptr) {
		sidePanelTimer = new wxTimer(this, wxWindow::NewControlId());
		Bind(wxEVT_TIMER, [this](wxTimerEvent&) { stepSidePanelAnim(); }, sidePanelTimer->GetId());
	}

	// Focus mode takes the toolbar as well as the side panel: the canvas gets
	// the window. The custom bar rises out of the top; the native one has no
	// geometry of ours to animate, so it simply goes.
	const bool customBar = modernBar != nullptr &&
	                       appConfig().appSettings.toolbarStyle != cl::tb::Classic;

	if (show) {
		// Put them back first, so a real layout can say where they belong,
		// then start the animation from off-screen.
		gatePalette->Show();
		miniMap->Show();
		sidePanelSash->Show();
		if (customBar) modernBar->Show();
		else if (toolBar) { toolBar->Show(true); SendSizeEvent(); }
		Layout();
	} else if (!customBar && toolBar) {
		toolBar->Show(false);
		SendSizeEvent();
	}
	panelRect = gatePalette->GetRect();
	miniRect  = miniMap->GetRect();
	sashRect  = sidePanelSash->GetRect();
	splitRect = rightSplitter->GetRect();
	barRect   = customBar ? modernBar->GetRect() : wxRect();
	barTravel = customBar ? barRect.height : 0;
	// A hidden panel reports a stale rect; recover the travel from the sash.
	if (panelRect.width <= 0) panelRect.width = appConfig().appSettings.sidePanelWidth;

	panelAnimShowing = show;
	panelAnimT = show ? 0.0 : 1.0;
	panelAnimFrom = panelAnimT;
	panelAnimStart = wxGetLocalTimeMillis();
	sidePanelTimer->Start(16);
	stepSidePanelAnim();
}

void MainFrame::stepSidePanelAnim() {
	// Progress by the clock, not by ticks. It used to add a twelfth per timer
	// tick, and Windows delivers timer ticks only when nothing else is queued:
	// with the simulation running and the canvas repainting, ticks came late
	// and the slide crawled -- sometimes for seconds. Late ticks now just mean
	// fewer frames of the same ~190ms slide.
	const double kSlideMs = 190.0;
	const double elapsed = (wxGetLocalTimeMillis() - panelAnimStart).ToDouble() / kSlideMs;
	panelAnimT = panelAnimShowing ? panelAnimFrom + elapsed : panelAnimFrom - elapsed;
	const bool done = panelAnimShowing ? (panelAnimT >= 1.0) : (panelAnimT <= 0.0);
	panelAnimT = std::max(0.0, std::min(1.0, panelAnimT));

	// Ease out: quick to leave, gentle to land.
	const double e = 1.0 - std::pow(1.0 - panelAnimT, 3.0);
	const int travel = sashRect.GetRight() > 0 ? sashRect.GetRight() : panelRect.width;
	const int offset = (int)std::lround((1.0 - e) * travel);      // sideways
	const int rise = (int)std::lround((1.0 - e) * barTravel);     // and upwards

	std::vector<std::pair<wxWindow*, wxRect>> moves;
	if (barTravel > 0 && modernBar)
		moves.push_back({ modernBar, wxRect(barRect.x, barRect.y - rise, barRect.width, barRect.height) });
	moves.push_back({ gatePalette, wxRect(panelRect.x - offset, panelRect.y - rise, panelRect.width, panelRect.height) });
	moves.push_back({ miniMap, wxRect(miniRect.x - offset, miniRect.y - rise, miniRect.width, miniRect.height) });
	moves.push_back({ sidePanelSash, wxRect(sashRect.x - offset, sashRect.y - rise, sashRect.width, sashRect.height) });
	// The canvas takes the room the other two give up.
	moves.push_back({ rightSplitter, wxRect(splitRect.x - offset, splitRect.y - rise,
	                                        splitRect.width + offset, splitRect.height + rise) });
#ifdef __WXMSW__
	// All in one go, and without SWP's default of carrying each window's old
	// pixels to its new spot. The canvas is OpenGL, whose pixels GDI cannot
	// copy: what got carried along was garbage, and nothing ever repainted it
	// -- the white and black lines focus mode left behind. NOCOPYBITS makes
	// each moved window repaint itself instead.
	bool moved = false;
	if (HDWP batch = ::BeginDeferWindowPos((int)moves.size())) {
		for (const auto& m : moves) {
			batch = ::DeferWindowPos(batch, (HWND)m.first->GetHWND(), nullptr,
			                         m.second.x, m.second.y, m.second.width, m.second.height,
			                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOCOPYBITS);
			if (!batch) break;
		}
		moved = batch && ::EndDeferWindowPos(batch);
	}
	if (!moved)
#endif
	for (const auto& m : moves) m.first->SetSize(m.second);

	if (!done) return;
	sidePanelTimer->Stop();
	if (!panelAnimShowing) {
		gatePalette->Hide();
		miniMap->Hide();
		sidePanelSash->Hide();
		if (barTravel > 0 && modernBar) modernBar->Hide();
	}
	applyTitlebarForTopRow();
	Layout();   // hand the geometry back to the sizer
	RenumberTabs();   // the strip may have just inherited the title bar row
	// One clean repaint of everything the slide moved. Mid-slide frames only
	// repaint what each resize invalidated, and on Windows a strip the canvas
	// had just grown into could stay unpainted: the white lines along the edge.
	Refresh();
	Update();
}

void MainFrame::ApplySidePanelWidth() {
	int& w = appConfig().appSettings.sidePanelWidth;
	// First launch: roomy enough that the gate names and the section list read
	// in full. Its natural width was the narrowest that fit, which cut names
	// short with "..." until you dragged it wider.
	if (w <= 0) w = wxMax(gatePalette->GetBestSize().x, FromDIP(270));
	w = wxMax(SIDE_PANEL_MIN_WIDTH, wxMin(SIDE_PANEL_MAX_WIDTH, w));
	gatePalette->SetMinSize(wxSize(w, -1));
	gatePalette->SetMaxSize(wxSize(w, -1));
	miniMap->SetMinSize(wxSize(w, miniMap->GetMinSize().y));
	Layout();
}

void MainFrame::ApplyStatusInfoVisibility() {
	wxStatusBar* sb = GetStatusBar();
	if (sb == nullptr) return;
	const int n = appConfig().appSettings.showStatusInfo ? 4 : 1;
	if (sb->GetFieldsCount() == n && n == 1) {
		const int style = wxSB_FLAT;
		sb->SetStatusStyles(1, &style);
		return;
	}
	if (sb->GetFieldsCount() == n) return;
	const int styles[4] = { wxSB_FLAT, wxSB_FLAT, wxSB_FLAT, wxSB_FLAT };
	const int widths[4] = { -1, 90, 150, 190 };
	sb->SetFieldsCount(n, widths);
	sb->SetStatusStyles(n, styles);
	// Force a rewrite next tick -- the fields just came back empty.
	statusZoom.clear(); statusPos.clear(); statusCounts.clear();
}

bool MainFrame::IsLockToolOn() { return toolBar->GetToolState(Tool_Lock); }

void MainFrame::SetLockTool(bool on) {
	if (IsLockToolOn() == on) return;
	toolBar->ToggleTool(Tool_Lock, on);
	wxCommandEvent ev(wxEVT_TOOL, Tool_Lock);
	ProcessWindowEvent(ev);   // OnLock reads the toggle state
}

bool MainFrame::CanUndoCommand() { return commandProcessor && commandProcessor->CanUndo(); }
bool MainFrame::CanRedoCommand() { return commandProcessor && commandProcessor->CanRedo(); }

int MainFrame::GetZoomPercent() {
	if (currentCanvas == nullptr || currentCanvas->getZoom() <= 0) return 100;
	return (int)(100.0 * DEFAULT_ZOOM / currentCanvas->getZoom() + 0.5);
}

wxString MainFrame::GetDocumentTitle() {
	return GetTitle();   // the library name, kept current by updateDocumentTitle
}

wxString MainFrame::GetDocumentSubtitle() {
	const wxString page = currentCanvas ? TabLabel(currentCanvas) : wxString("Page 1");
	return page + wxString::FromUTF8(" \u00B7 ") + (fileIsDirty() ? "Edited" : "Saved");
}

void MainFrame::ApplyToolbarStyle() {
	const int style = appConfig().appSettings.toolbarStyle;
	const bool classic = style == cl::tb::Classic;
	// Focus mode keeps both bars away. Sim view restyles the bar through here,
	// and showing it unconditionally brought it back mid-focus mode.
	wxMenuBar* mb = GetMenuBar();
	const bool focus = mb != nullptr && mb->IsChecked(View_FocusMode);
	if (toolBar->IsShown() != (classic && !focus)) toolBar->Show(classic && !focus);
	if (modernBar->IsShown() != (!classic && !focus)) modernBar->Show(!classic && !focus);
	if (!classic) modernBar->Reconfigure();
	// Whoever ends up in the top row sets the title bar up for itself -- in
	// focus mode that is the tab strip, not either toolbar.
	applyTitlebarForTopRow();
	Layout();
	SendSizeEvent();   // the content area just grew into (or out of) the title bar
}

void MainFrame::UpdateStatusInfo() {
	if (modernBar) modernBar->Poll();
	wxStatusBar* sb = GetStatusBar();
	if (sb == nullptr || sb->GetFieldsCount() < 4 || currentCanvas == nullptr) return;

	const double z = currentCanvas->getZoom();
	const wxString zoom = wxString::Format("%d%%", z > 0 ? (int)(100.0 * DEFAULT_ZOOM / z + 0.5) : 100);

	const GLPoint2f m = currentCanvas->getMouseCoords();
	const wxString pos = wxString::Format("x %.1f   y %.1f", m.x, m.y);

	int selected = 0;
	for (auto& g : *currentCanvas->getGateList()) if (g.second && g.second->isSelected()) selected++;
	for (auto& w : *currentCanvas->getWireList()) if (w.second && w.second->isSelected()) selected++;
	const size_t gates = currentCanvas->getGateList()->size();
	wxString counts = wxString::Format("%zu gate%s", gates, gates == 1 ? "" : "s");
	if (selected > 0) counts += wxString::Format(L" \u00B7 %d selected", selected);

	if (zoom != statusZoom)     { statusZoom = zoom;     sb->SetStatusText(zoom, 1); }
	if (pos != statusPos)       { statusPos = pos;       sb->SetStatusText(pos, 2); }
	if (counts != statusCounts) { statusCounts = counts; sb->SetStatusText(counts, 3); }
}

void MainFrame::noteCanvasUsed(GUICanvas* canvas) {
	if (canvas == nullptr) return;
	canvasMRU.erase(std::remove(canvasMRU.begin(), canvasMRU.end(), canvas), canvasMRU.end());
	canvasMRU.insert(canvasMRU.begin(), canvas);
}

// Closing a tab should land you where you just were. `canvasMRU` already
// tracks that order for the Ctrl+Tab switcher, so reuse it: take the most
// recently used tab that is still open, and fall back to the neighbour if
// nothing in the history survives (a fresh session, say).
void MainFrame::selectTabAfterClosing(GUICanvas* closed, int closedIndex) {
	canvasMRU.erase(std::remove(canvasMRU.begin(), canvasMRU.end(), closed), canvasMRU.end());
	for (GUICanvas* c : canvasMRU) {
		if (std::find(canvases.begin(), canvases.end(), c) == canvases.end()) continue;
		if (PaneIndexOf(c) < 0) continue;
		SelectCanvas(c);
		return;
	}
	// A freshly loaded multi-page circuit may have no MRU entry beyond its
	// first page. If that page is closed, leaving currentCanvas on the parked
	// window makes the minimap, shortcuts and the next simulation tick operate
	// on a hidden tab. Choose the tab that shifted into its slot, or the previous
	// one when the closed tab was last.
	if (!canvases.empty()) {
		const int neighbor = std::max(0, std::min(closedIndex, (int)canvases.size() - 1));
		SelectCanvas(canvases[neighbor]);
	}
}

// Closing in two halves so the tab can dim on its way out: start the
// animation, then remove the page once it has played.
void MainFrame::beginCloseTab(GUICanvas* canvas) {
	if (canvas == nullptr || canvas == pendingCloseCanvas) return;
	// One at a time. Restarting the timer for a second tab used to strand the
	// first one: dimmed, never closed, and repainting forever.
	flushPendingClose();
	if (canvases.size() < 2 ||
	    std::find(canvases.begin(), canvases.end(), canvas) == canvases.end()) return;
	// Hold the canvas itself, not its index: the list can shift while the
	// animation plays, and closing the wrong tab is unrecoverable.
	pendingCloseCanvas = canvas;
	canvas->playCloseAnimation();
	closeTabTimer->StartOnce(GUICanvas::closeAnimationMs());
}

void MainFrame::finishCloseTab(GUICanvas* canvas) {
	if (canvas == nullptr) return;
	// Whatever happens next, the dimming is over. A canvas left dimmed comes
	// back blank if the close is undone.
	canvas->cancelCloseAnimation();
	if (canvases.size() < 2) return;
	int canvasID = -1;
	for (size_t i = 0; i < canvases.size(); i++) if (canvases[i] == canvas) canvasID = (int)i;
	if (canvasID < 0) return;   // already gone
	gCircuit->GetCommandProcessor()->Submit(
		(wxCommand*)(new cmdDeleteTab(gCircuit, canvas, canvasBook, &canvases, canvasID)));
	selectTabAfterClosing(canvas, canvasID);
}

void MainFrame::flushPendingClose() {
	if (pendingCloseCanvas == nullptr) return;
	if (closeTabTimer) closeTabTimer->Stop();
	GUICanvas* closing = pendingCloseCanvas;
	pendingCloseCanvas = nullptr;   // before finishing, so nothing re-enters it
	finishCloseTab(closing);
}

void MainFrame::cancelPendingClose() {
	if (pendingCloseCanvas == nullptr) return;
	if (closeTabTimer) closeTabTimer->Stop();
	pendingCloseCanvas->cancelCloseAnimation();
	pendingCloseCanvas = nullptr;
}

// Cmd+Shift+T, the way a browser does it. Undo still undoes -- this only
// reaches for a tab close, and only while it is the most recent thing done.
void MainFrame::OnReopenTab(wxCommandEvent& event) {
	// Caught while it is still dimming: it simply stays.
	if (GUICanvas* staying = pendingCloseCanvas) {
		cancelPendingClose();
		SelectCanvas(staying);
		SetStatusText("Reopened the closed tab.");
		return;
	}
	wxCommandProcessor* cp = gCircuit->GetCommandProcessor();
	if (dynamic_cast<cmdDeleteTab*>(cp->GetCurrentCommand()) == nullptr) {
		SetStatusText("No closed tab to reopen.");
		wxBell();
		return;
	}
	// Through OnUndo, not straight to the command processor: that is where
	// the sim timers are held off while an undo rebuilds gates and wires.
	OnUndo(event);
	if (currentCanvas) currentCanvas->playAppearAnimation();
	RenumberTabs();
	SetStatusText("Reopened the closed tab.");
}

// A Yes/No prompt that answers to Y, N and Escape as well as Return, which is
// what you want when it interrupts you mid-keyboard. On macOS ui::MessageDialog
// is a system alert whose buttons swallow those keys, so there it is an
// NSAlert with a key monitor (MacAskYesNo). Elsewhere it is the stock dialog.
bool MainFrame::AskYesNo(const wxString& title, const wxString& message) {
#ifdef __APPLE__
	return MacAskYesNo(title.utf8_str(), message.utf8_str(), renderMode().darkMode);
#else
	ui::MessageDialog ask(this, message, title, wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
	return ask.ShowModal() == wxID_YES;
#endif
}

wxString MainFrame::TabLabel(GUICanvas* canvas) {
	auto it = tabNames.find(canvas);
	if (it != tabNames.end() && !it->second.empty()) return it->second;
	return wxString::Format("Page %d", TabNumber(canvas));
}

wxString MainFrame::SavedTabName(GUICanvas* canvas) const {
	auto it = tabNames.find(canvas);
	return it == tabNames.end() ? wxString() : it->second;
}

void MainFrame::SetTabName(GUICanvas* canvas, const wxString& name) {
	if (canvas == nullptr) return;
	if (name.empty()) tabNames.erase(canvas);
	else tabNames[canvas] = name;
	RenumberTabs();   // our strip, or the classic tabs' page text
}

void MainFrame::RenameTab(GUICanvas* canvas) {
	if (canvas == nullptr || canvas == pendingCloseCanvas) return;
	wxTextEntryDialog ask(this, "Name this tab:", "Rename Tab", TabLabel(canvas));
	if (ask.ShowModal() != wxID_OK) return;
	// The dialog is modal, not frozen: a close that was already dimming can
	// finish underneath it. Naming a tab that has gone would only leave a
	// stale entry keyed by its address.
	if (std::find(canvases.begin(), canvases.end(), canvas) == canvases.end()) return;
	const wxString name = ask.GetValue().Strip(wxString::both);
	SetTabName(canvas, name);
	if (commandProcessor) commandProcessor->SetMenuStrings();
	saveToLibrary(false);   // the name is part of the circuit now
}

int MainFrame::TabNumber(GUICanvas* canvas) {
	auto it = tabNumbers.find(canvas);
	if (it != tabNumbers.end()) return it->second;
	int n = 1;
	for (bool taken = true; taken; ) {
		taken = false;
		for (const auto& kv : tabNumbers) if (kv.second == n) { taken = true; n++; break; }
	}
	tabNumbers[canvas] = n;
	return n;
}

void MainFrame::RenumberTabs() {
	for (GUICanvas* c : canvases) TabNumber(c);
	for (CanvasPane& p : panes) {
		if (p.strip) { p.strip->Rebuild(); continue; }
		if (p.book == nullptr) continue;
		for (size_t i = 0; i < p.book->GetPageCount(); i++)
			p.book->SetPageText(i, TabLabel(static_cast<GUICanvas*>(p.book->GetPage(i))));
	}
}

int MainFrame::PaneCount() const { return panes[1].host != nullptr ? 2 : 1; }

// True when no toolbar of either kind is on screen, so the tab strip is the
// top row of the window.
bool MainFrame::tabStripIsTopRow() const {
	const bool modernShown = modernBar != nullptr && modernBar->IsShown();
	const bool classicShown = toolBar != nullptr && toolBar->IsShown();
	return !modernShown && !classicShown;
}

int MainFrame::TabStripLeftInset() const {
#ifdef __APPLE__
	// The toolbar styles leave room for the window's red/yellow/green buttons.
	// Take the toolbar away -- focus mode -- and the tab strip inherits that
	// row, so it has to leave the same room. Full screen has no buttons.
	if (tabStripIsTopRow() && !IsFullScreen()) return 86;
#endif
	return 8;
}

// Whoever owns the top row decides how the title bar behaves. With a toolbar
// there, that is the toolbar's business; with the tab strip there, the window
// title has to be hidden or macOS draws "Untitled" straight across the tabs.
void MainFrame::applyTitlebarForTopRow() {
#ifdef __WXMSW__
	// Windows 11 draws the title bar in whatever colour the row under it is,
	// so the top of the window reads as one bar, the way the Mac's does.
	const bool dark = renderMode().darkMode;
	if (tabStripIsTopRow())
		WinSetCaptionColour(this, dark ? wxColour(22, 24, 28) : wxColour(233, 234, 238),
		                    dark ? wxColour(228, 232, 240) : wxColour(32, 35, 42));
	else if (modernBar && modernBar->IsShown())
		WinSetCaptionColour(this, modernBar->BarColour(), modernBar->InkColour());
	else
		WinSetCaptionColour(this, toolBar->GetBackgroundColour(),
		                    dark ? wxColour(228, 232, 240) : wxColour(32, 35, 42));
#endif
#ifdef __APPLE__
	const bool classic = appConfig().appSettings.toolbarStyle == cl::tb::Classic;
	if (tabStripIsTopRow())
		MacSetCustomTitlebar(MacGetTopLevelWindowRef(), true, TabStrip::BarHeight());
	else
		MacSetCustomTitlebar(MacGetTopLevelWindowRef(), !classic, ModernToolbar::BarHeight());
#endif
}

int MainFrame::PaneIndexOf(GUICanvas* canvas) const {
	for (int i = 0; i < 2; i++)
		if (panes[i].book && panes[i].book->FindPage(canvas) != wxNOT_FOUND) return i;
	return -1;
}

std::vector<GUICanvas*> MainFrame::PaneCanvases(int pane) const {
	std::vector<GUICanvas*> out;
	if (pane < 0 || pane > 1 || panes[pane].book == nullptr) return out;
	for (size_t i = 0; i < panes[pane].book->GetPageCount(); i++)
		out.push_back(static_cast<GUICanvas*>(panes[pane].book->GetPage(i)));
	return out;
}

// Which pane a screen point is over, for a tab dragged from the other side.
int MainFrame::PaneAtScreen(const wxPoint& p) const {
	for (int i = 0; i < 2; i++) {
		if (panes[i].host == nullptr) continue;
		const wxRect r(panes[i].host->GetScreenPosition(), panes[i].host->GetSize());
		if (r.Contains(p)) return i;
	}
	return -1;
}

wxRect MainFrame::PaneScreenRect(int pane) const {
	if (pane < 0 || pane > 1 || panes[pane].host == nullptr) return wxRect();
	return wxRect(panes[pane].host->GetScreenPosition(), panes[pane].host->GetSize());
}

// Build a pane: its own tab strip over its own pageless book.
void MainFrame::buildPane(int index) {
	CanvasPane& p = panes[index];
	const wxWindowID bookId = index == 0 ? NOTEBOOK_ID : wxID_ANY;
	p.host = new wxPanel(canvasSplit);
	p.host->SetBackgroundColour(renderMode().darkMode ? wxColour(22, 24, 28) : wxColour(233, 234, 238));
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	if (usingClassicTabs) {
		// The system's own tabs: it draws them, so there is no strip of ours
		// and no dragging.
		p.book = new wxNotebook(p.host, bookId, wxDefaultPosition, wxDefaultSize, wxNB_TOP);
		p.strip = nullptr;
	} else {
		p.book = new wxSimplebook(p.host, bookId);
		p.strip = new TabStrip(p.host, this, index);
		sizer->Add(p.strip, 0, wxEXPAND);
	}
	// Either kind of book reports its page changes the same way -- the
	// classic tabs when clicked, and both when a page is inserted or removed
	// with the selection on it -- so every pane's book is watched.
	p.book->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, &MainFrame::OnNotebookPage, this);
	sizer->Add(p.book, 1, wxEXPAND);
	p.host->SetSizer(sizer);
}

// Switching tab bars keeps every tab: the pages move to a freshly built pane
// and the old one goes away under them.
void MainFrame::rebuildTabUi() {
	const bool classic = appConfig().appSettings.classicTabs;
	if (classic == usingClassicTabs) return;
	CloseSplit();   // the classic tabs cannot be dragged between panes

	GUICanvas* keep = currentCanvas;
	std::vector<GUICanvas*> order = PaneCanvases(0);
	wxPanel* oldHost = panes[0].host;
	// Emptying the old book moves its selection page by page; none of that
	// is the user switching tabs, so it must not reach OnNotebookPage.
	panes[0].book->SetEvtHandlerEnabled(false);
	while (panes[0].book->GetPageCount() > 0) panes[0].book->RemovePage(0);

	usingClassicTabs = classic;
	buildPane(0);
	canvasBook = panes[0].book;
	for (GUICanvas* c : order) {
		c->Reparent(panes[0].book);
		panes[0].book->AddPage(c, "", c == keep);
	}
	if (!canvasSplit->ReplaceWindow(oldHost, panes[0].host))
		canvasSplit->Initialize(panes[0].host);
	oldHost->Destroy();
	canvasSplit->Refresh();
	RenumberTabs();
	if (keep) { currentCanvas = nullptr; FocusCanvas(keep); }
}

// Put `canvas` in `pane` at `slot`, splitting the window first if the second
// pane does not exist yet. This is the one path everything else goes through.
void MainFrame::MoveCanvasToPane(GUICanvas* canvas, int pane, int slot, bool onRight) {
	if (canvas == nullptr || pane < 0 || pane > 1) return;
	// Only a tab that is open: this can run a moment after the drag that
	// asked for it, and a tab closed in between is not coming back.
	if (std::find(canvases.begin(), canvases.end(), canvas) == canvases.end()) return;
	const int from = PaneIndexOf(canvas);
	if (from < 0) return;
	if (pane == 1 && panes[1].host == nullptr) {
		// The last tab of pane 0 cannot leave it empty.
		if (from == 0 && panes[0].book->GetPageCount() < 2) {
			gCircuit->GetCommandProcessor()->Submit(
				(wxCommand*)new cmdAddTab(gCircuit, canvasBook, &canvases));
		}
		buildPane(1);
		const int width = canvasSplit->GetClientSize().x;
		if (onRight) canvasSplit->SplitVertically(panes[0].host, panes[1].host, width / 2);
		else         canvasSplit->SplitVertically(panes[1].host, panes[0].host, width / 2);
	}
	{
		const int page = panes[from].book->FindPage(canvas);
		if (page != wxNOT_FOUND) panes[from].book->RemovePage(page);
	}
	canvas->Reparent(panes[pane].book);
	const size_t count = panes[pane].book->GetPageCount();
	const size_t at = (slot < 0 || (size_t)slot > count) ? count : (size_t)slot;
	panes[pane].book->InsertPage(at, canvas, "", true);
	canvas->Show();

	// Emptying a pane closes it: that is how you leave a split, by moving or
	// closing its last tab.
	if (from != pane) collapsePaneIfEmpty(from);
	RenumberTabs();
	FocusCanvas(canvas);
	canvasSplit->Refresh();
}

// Take a canvas out of whichever pane is showing it. Undo and redo run long
// after the drag that moved it, so nothing may assume which pane that is.
// Starting or opening a circuit throws away every tab but the first. The page
// to destroy has to be found by identity: with a split open, or after tabs
// have been dragged about, a canvas's place in the list is not its page
// number, and deleting by number destroyed the wrong tab and left the right
// one dangling.
void MainFrame::dropExtraTabs() {
	// The circuit is being replaced: a tab dimming its way out has nothing
	// left to close, and a Ctrl+Tab list names tabs about to be destroyed.
	cancelPendingClose();
	cancelTabSwitch();
	CloseSplit();   // one strip again before any page is torn down
	while (canvases.size() > 1) {
		GUICanvas* doomed = canvases.back();
		canvases.pop_back();
		forgetCanvas(doomed);
		const int page = panes[0].book->FindPage(doomed);
		if (page != wxNOT_FOUND) panes[0].book->DeletePage(page);
		else doomed->Destroy();
	}
	// Closed tabs were kept only for the undo history, which our callers have
	// just cleared. Nothing refers to them any more.
	if (canvasParking) {
		const wxWindowList parked = canvasParking->GetChildren();
		for (wxWindow* w : parked) {
			forgetCanvas(static_cast<GUICanvas*>(w));
			w->Destroy();
		}
	}
	// A new circuit starts from Page 1 with no names: the tab that stays was
	// "Page 3" or "Adder" in the circuit it belonged to, not in this one.
	tabNumbers.clear();
	tabNames.clear();
	canvasMRU.clear();
	if (!canvases.empty()) {
		noteCanvasUsed(canvases[0]);
		// currentCanvas may have been one of the tabs just destroyed. Callers go
		// on to show modal dialogs (the migration notices) whose event loop runs
		// paint and timer handlers, so it must not dangle even for a moment.
		currentCanvas = canvases[0];
		gCircuit->setCurrentCanvas(currentCanvas);
	}
	RenumberTabs();
}

// A destroyed canvas must not stay in any of our tables: a later canvas can
// land on the same address and inherit its name.
void MainFrame::forgetCanvas(GUICanvas* canvas) {
	tabNumbers.erase(canvas);
	tabNames.erase(canvas);
	canvasMRU.erase(std::remove(canvasMRU.begin(), canvasMRU.end(), canvas), canvasMRU.end());
	if (pendingCloseCanvas == canvas) pendingCloseCanvas = nullptr;
}

void MainFrame::DetachCanvasPage(GUICanvas* canvas) {
	const int pane = PaneIndexOf(canvas);
	if (pane < 0) return;
	const int page = panes[pane].book->FindPage(canvas);
	if (page != wxNOT_FOUND) panes[pane].book->RemovePage(page);
	canvas->Hide();
	// Out of the book's children as well as its pages, before that book can
	// go: emptying a split pane destroys it, and it would destroy this canvas
	// with it while the undo history still holds it.
	if (canvasParking) canvas->Reparent(canvasParking);
	collapsePaneIfEmpty(pane);
}

void MainFrame::DiscardDetachedCanvas(GUICanvas* canvas) {
	if (canvas == nullptr) return;
	// Command destructors must never be able to destroy a live tab. A canvas is
	// disposable only after both sources of document ownership have released
	// it: it is absent from the canonical list and from both pane books.
	if (std::find(canvases.begin(), canvases.end(), canvas) != canvases.end() ||
	    PaneIndexOf(canvas) >= 0) {
		wxFAIL_MSG("attempted to discard an attached canvas");
		return;
	}
	forgetCanvas(canvas);
	canvas->Destroy();
}

// Put it back, in its place in the canvas order. It always returns to the
// tab strip rather than to a split pane: the pane it came from may be long
// gone, and a reopened tab appearing in the main strip is what you expect.
void MainFrame::AttachCanvasPage(GUICanvas* canvas, int canvasIndex) {
	if (canvas == nullptr) return;
	if (PaneIndexOf(canvas) >= 0) { FocusCanvas(canvas); return; }   // already showing
	int at = 0;
	for (int i = 0; i < canvasIndex && i < (int)canvases.size(); i++)
		if (panes[0].book->FindPage(canvases[i]) != wxNOT_FOUND) at++;
	canvas->Reparent(panes[0].book);
	canvas->Show();
	panes[0].book->InsertPage(at, canvas, "", true);
	RenumberTabs();
	FocusCanvas(canvas);
}

void MainFrame::collapsePaneIfEmpty(int pane) {
	if (pane < 0 || pane > 1 || panes[pane].host == nullptr) return;
	if (panes[pane].book->GetPageCount() > 0) return;
	if (PaneCount() < 2) return;

	if (pane == 0) {
		// Pane 0 is the one everything else holds a pointer to, so the other
		// pane's tabs move into it rather than the other way round.
		for (GUICanvas* c : PaneCanvases(1)) {
			panes[1].book->RemovePage(panes[1].book->FindPage(c));
			c->Reparent(panes[0].book);
			panes[0].book->AddPage(c, "", true);
		}
	}
	canvasSplit->Unsplit(panes[1].host);
	panes[1].host->Destroy();
	panes[1] = CanvasPane();
	RenumberTabs();
	if (panes[0].book->GetSelection() != wxNOT_FOUND)
		FocusCanvas(static_cast<GUICanvas*>(panes[0].book->GetPage(panes[0].book->GetSelection())));
}

// The canvas to show in a new split: whatever was used most recently other
// than the one in front, or a new tab if this is the only one.
GUICanvas* MainFrame::pickSplitPartner() {
	flushPendingClose();   // a tab on its way out is no partner
	for (GUICanvas* c : canvasMRU)
		if (c != currentCanvas && std::find(canvases.begin(), canvases.end(), c) != canvases.end())
			return c;
	for (GUICanvas* c : canvases)
		if (c != currentCanvas) return c;
	gCircuit->GetCommandProcessor()->Submit((wxCommand*)new cmdAddTab(gCircuit, canvasBook, &canvases));
	RenumberTabs();
	return canvases.empty() ? nullptr : canvases.back();
}

void MainFrame::SplitWith(GUICanvas* canvas, bool onRight) {
	if (canvas == nullptr) return;
	MoveCanvasToPane(canvas, 1, -1, onRight);
}

void MainFrame::CloseSplit() {
	if (PaneCount() < 2) return;
	for (GUICanvas* c : PaneCanvases(1)) {
		panes[1].book->RemovePage(panes[1].book->FindPage(c));
		c->Reparent(panes[0].book);
		panes[0].book->AddPage(c, "", true);
	}
	canvasSplit->Unsplit(panes[1].host);
	panes[1].host->Destroy();
	panes[1] = CanvasPane();
	RenumberTabs();
	if (panes[0].book->GetSelection() != wxNOT_FOUND)
		FocusCanvas(static_cast<GUICanvas*>(panes[0].book->GetPage(panes[0].book->GetSelection())));
}

// Clicking in either pane makes that canvas the one everything else acts on.
void MainFrame::FocusCanvas(GUICanvas* canvas) {
	if (canvas == nullptr) return;
	if (canvas != currentCanvas) {
		if (currentCanvas != nullptr) currentCanvas->setMinimap(NULL);
		currentCanvas = canvas;
		gCircuit->setCurrentCanvas(currentCanvas);
		currentCanvas->setMinimap(miniMap);
		currentCanvas->SetFocus();
		noteCanvasUsed(currentCanvas);
		UpdateStatusInfo();
	}
	for (CanvasPane& p : panes) if (p.strip) p.strip->Refresh();
}

void MainFrame::SelectCanvas(GUICanvas* canvas) {
	if (canvas == nullptr) return;
	const int pane = PaneIndexOf(canvas);
	if (pane >= 0) {
		const int page = panes[pane].book->FindPage(canvas);
		if (page != wxNOT_FOUND && page != panes[pane].book->GetSelection())
			panes[pane].book->SetSelection(page);
	}
	FocusCanvas(canvas);
}

void MainFrame::CloseTabCanvas(GUICanvas* canvas) {
	if (canvas == nullptr || canvas == pendingCloseCanvas) return;   // already going
	// Settle a close still in progress first, so the count below is true.
	flushPendingClose();
	if (canvases.size() < 2 ||
	    std::find(canvases.begin(), canvases.end(), canvas) == canvases.end()) { wxBell(); return; }
	if (!canvas->getGateList()->empty() &&
	    !AskYesNo("Close Tab", "All work on this tab will be lost. Would you like to close it?"))
		return;
	beginCloseTab(canvas);
}

void MainFrame::NewTabInPane(int pane) {
	pendingNewTabPane = pane;
	wxCommandEvent dummy;
	OnNewTab(dummy);
	pendingNewTabPane = 0;
}

void MainFrame::OnNewTabHere(wxCommandEvent&) { NewTabInPane(PaneIndexOf(currentCanvas)); }

void MainFrame::NewTabFromStrip() { NewTabInPane(0); }

// Dragging a tab along its own strip.
void MainFrame::MoveTab(int from, int to) {
	const std::vector<GUICanvas*> inPane = PaneCanvases(PaneIndexOf(currentCanvas));
	if (from < 0 || from >= (int)inPane.size()) return;
	MoveCanvasToPane(inPane[from], PaneIndexOf(inPane[from]), to, true);
}

void MainFrame::NudgeSplitSash(int dx) {
	if (!IsSplit()) return;
	canvasSplit->SetSashPosition(canvasSplit->GetSashPosition() + dx);
}

void MainFrame::OnSplitRight(wxCommandEvent& WXUNUSED(event)) {
	if (IsSplit()) { CloseSplit(); return; }   // the command toggles
	SplitWith(pickSplitPartner(), /*onRight=*/true);
}

void MainFrame::OnSplitClose(wxCommandEvent& WXUNUSED(event)) {
	if (!IsSplit()) { wxBell(); return; }
	CloseSplit();
}

void MainFrame::OnFocusOtherPane(wxCommandEvent& WXUNUSED(event)) {
	if (!IsSplit()) { wxBell(); return; }
	const int other = PaneIndexOf(currentCanvas) == 0 ? 1 : 0;
	const int sel = panes[other].book->GetSelection();
	if (sel != wxNOT_FOUND) FocusCanvas(static_cast<GUICanvas*>(panes[other].book->GetPage(sel)));
}

void MainFrame::handleTabSwitchKey(bool backwards) {
	if (tabSwitchActive) {
		const int n = (int)tabSwitchList.size();
		tabSwitchSel = (tabSwitchSel + (backwards ? n - 1 : 1)) % n;
#ifdef __APPLE__
		if (tabSwitchShown) MacTabSwitcher_Select(tabSwitchSel);
#endif
		return;
	}

	// A tab still dimming from a close is not one to switch to.
	flushPendingClose();

	// Current page first, then the rest by how recently they were used;
	// pages never visited go last, in tab order.
	auto has = [](const vector<GUICanvas*>& v, GUICanvas* c) {
		return std::find(v.begin(), v.end(), c) != v.end();
	};
	tabSwitchList.clear();
	if (currentCanvas) tabSwitchList.push_back(currentCanvas);
	for (GUICanvas* c : canvasMRU)
		if (has(canvases, c) && !has(tabSwitchList, c)) tabSwitchList.push_back(c);
	for (GUICanvas* c : canvases)
		if (!has(tabSwitchList, c)) tabSwitchList.push_back(c);
	if (tabSwitchList.size() > 10) tabSwitchList.resize(10);
	if (tabSwitchList.size() < 2) { tabSwitchList.clear(); return; }

	tabSwitchActive = true;
	tabSwitchShown = false;
	tabSwitchSel = backwards ? (int)tabSwitchList.size() - 1 : 1;
	tabSwitchStart = wxGetLocalTimeMillis();
	tabSwitchTimer->Start(15);
}

void MainFrame::OnTabSwitchTimer(wxTimerEvent& WXUNUSED(event)) {
	if (!tabSwitchActive) { tabSwitchTimer->Stop(); return; }
#ifdef __APPLE__
	const bool held = MacControlKeyDown();
#else
	const bool held = wxGetKeyState(WXK_CONTROL);
#endif
	if (!held) {
		commitTabSwitch(tabSwitchSel);
		return;
	}
	// Only show the panel if Ctrl is still down after a moment -- a quick
	// Ctrl+Tab just flips to the previous page.
	if (!tabSwitchShown && wxGetLocalTimeMillis() - tabSwitchStart >= 180) showTabSwitcher();
}

void MainFrame::showTabSwitcher() {
	tabSwitchShown = true;
#ifdef __APPLE__
	const bool dark = renderMode().darkMode;
	const double sf = GetContentScaleFactor();
	// Matches the card size in TabSwitcherMac.mm (208 x 130 points).
	const int tw = (int)(208 * sf), th = (int)(130 * sf);
	std::vector<TabSwitcherCard> cards;
	for (GUICanvas* c : tabSwitchList) {
		TabSwitcherCard card;
		card.title = TabLabel(c);
		card.thumbnail = wxBitmap(c->renderThumbnail(tw, th, dark), -1, sf);
		cards.push_back(card);
	}
	MacTabSwitcher_Show(this, cards, tabSwitchSel, dark,
		[this](int i) {
			tabSwitchSel = i;
			MacTabSwitcher_Select(i);
		},
		// Deferred: switching tears the panel down, and this runs inside its
		// own mouse handler.
		[this](int i) { CallAfter([this, i]() { commitTabSwitch(i); }); });
#endif
}

void MainFrame::commitTabSwitch(int index) {
	if (!tabSwitchActive) return;
	GUICanvas* target = (index >= 0 && index < (int)tabSwitchList.size()) ? tabSwitchList[index] : nullptr;
	cancelTabSwitch();
	if (target == nullptr || target == currentCanvas) return;
	SelectCanvas(target);   // finds its pane and page; focuses it
}

void MainFrame::cancelTabSwitch() {
	if (tabSwitchTimer) tabSwitchTimer->Stop();
#ifdef __APPLE__
	if (tabSwitchShown) MacTabSwitcher_Hide();
#endif
	tabSwitchActive = false;
	tabSwitchShown = false;
	tabSwitchList.clear();
}

void MainFrame::switchToCanvas(GUICanvas *canvas) {
	if (canvas == NULL || canvas == currentCanvas) return;
	// Which pane is showing it, and which page it is there -- a tab dragged
	// into a split is no longer page N of the strip, and selecting by its
	// place in the canvas list landed on a different tab or on nothing.
	const int pane = PaneIndexOf(canvas);
	if (pane < 0) return;   // not on screen (an undone tab), nothing to show
	const int page = panes[pane].book->FindPage(canvas);
	// ChangeSelection switches the tab WITHOUT firing a page-changed event --
	// SetSelection would, re-entering the GUI mid-undo. Mirror the parts of
	// OnNotebookPage we actually need, by hand.
	if (page != wxNOT_FOUND) panes[pane].book->ChangeSelection(page);
	if (currentCanvas != NULL) currentCanvas->setMinimap(NULL);
	currentCanvas = canvas;
	gCircuit->setCurrentCanvas(currentCanvas);
	currentCanvas->setMinimap(miniMap);
	noteCanvasUsed(currentCanvas);
	RenumberTabs();
}

void MainFrame::OnCut(wxCommandEvent& event) {
	currentCanvas->cutSelectionToClipboard();
}

void MainFrame::OnCopy(wxCommandEvent& event) {
	currentCanvas->copyBlockToClipboard();
}

void MainFrame::OnPaste(wxCommandEvent& event) {
	currentCanvas->pasteBlockFromClipboard();
}

void MainFrame::OnExportBitmap(wxCommandEvent& event) {
	// Create unified export dialog with horizontal layout
	wxDialog exportDialog(this, wxID_ANY, "Export as Image", wxDefaultPosition, wxDefaultSize);
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	// Preview panel — sized dynamically on first render
	const int previewMaxW = 560, previewMaxH = 220;
	wxStaticBitmap* previewBitmap = new wxStaticBitmap(&exportDialog, wxID_ANY, wxNullBitmap,
		wxDefaultPosition, wxDefaultSize);
	mainSizer->Add(previewBitmap, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 15);

	// Grid option
	wxCheckBox* gridCheck = new wxCheckBox(&exportDialog, wxID_ANY, "Include grid lines");
	gridCheck->SetValue(false);
	mainSizer->Add(gridCheck, 0, wxLEFT | wxRIGHT, 15);

	// Name and result: printed in a strip under the circuit.
	mainSizer->AddSpacer(10);
	wxStaticBoxSizer* infoBox = new wxStaticBoxSizer(wxVERTICAL, &exportDialog, "Name and result");
	wxCheckBox* infoCheck = new wxCheckBox(&exportDialog, wxID_ANY, "Add my name and whether the circuit works");
	infoCheck->SetValue(appConfig().appSettings.exportInfoEnabled);
	infoBox->Add(infoCheck, 0, wxALL, 5);
	wxBoxSizer* nameRow = new wxBoxSizer(wxHORIZONTAL);
	nameRow->Add(new wxStaticText(&exportDialog, wxID_ANY, "Your name:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
	wxTextCtrl* nameCtrl = new wxTextCtrl(&exportDialog, wxID_ANY,
		wxString::FromUTF8(appConfig().appSettings.studentName.c_str()));
	nameCtrl->SetHint("First and last name");
	nameRow->Add(nameCtrl, 1, wxALIGN_CENTER_VERTICAL);
	infoBox->Add(nameRow, 0, wxALL | wxEXPAND, 5);
	wxRadioButton* worksRadio = new wxRadioButton(&exportDialog, wxID_ANY, "My circuit works properly",
		wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	wxRadioButton* brokenRadio = new wxRadioButton(&exportDialog, wxID_ANY, "My circuit does not work because...");
	worksRadio->SetValue(lastExportInfo.works);
	brokenRadio->SetValue(!lastExportInfo.works);
	infoBox->Add(worksRadio, 0, wxLEFT | wxRIGHT | wxTOP, 5);
	infoBox->Add(brokenRadio, 0, wxLEFT | wxRIGHT | wxTOP, 5);
	wxTextCtrl* whyCtrl = new wxTextCtrl(&exportDialog, wxID_ANY, lastExportInfo.why,
		wxDefaultPosition, wxSize(-1, 56), wxTE_MULTILINE);
	whyCtrl->SetHint("Explain what doesn't work (required)");
	infoBox->Add(whyCtrl, 0, wxALL | wxEXPAND, 5);
	mainSizer->Add(infoBox, 0, wxLEFT | wxRIGHT | wxEXPAND, 15);

	// Horizontal sizer for output style and resolution side-by-side
	mainSizer->AddSpacer(10);
	wxBoxSizer* optionsSizer = new wxBoxSizer(wxHORIZONTAL);

	// Output style box with better spacing
	wxStaticBoxSizer* styleBox = new wxStaticBoxSizer(wxVERTICAL, &exportDialog, "Output style");
	wxRadioButton* colorRadio = new wxRadioButton(&exportDialog, wxID_ANY, "Color", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	wxRadioButton* bwRadio = new wxRadioButton(&exportDialog, wxID_ANY, "Black && White");
	colorRadio->SetValue(true);
	styleBox->Add(colorRadio, 0, wxALL, 5);
	styleBox->Add(bwRadio, 0, wxALL, 5);
	optionsSizer->Add(styleBox, 1, wxRIGHT | wxEXPAND, 10);

	// Resolution box with better spacing
	wxStaticBoxSizer* resBox = new wxStaticBoxSizer(wxVERTICAL, &exportDialog, "Resolution");
	// The multiplication sign goes in as a \u escape in a wide literal. Written
	// as raw UTF-8 bytes in a narrow literal it gets re-read one byte at a time
	// under the Windows ANSI code page and reaches the dialog as mojibake.
	wxRadioButton* screen2x = new wxRadioButton(&exportDialog, wxID_ANY, L"Screen (2\u00d7)", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	wxRadioButton* print4x = new wxRadioButton(&exportDialog, wxID_ANY, L"Print (4\u00d7)");
	wxRadioButton* high6x = new wxRadioButton(&exportDialog, wxID_ANY, L"High Quality (6\u00d7)");
	print4x->SetValue(true); // Default to Print
	resBox->Add(screen2x, 0, wxALL, 5);
	resBox->Add(print4x, 0, wxALL, 5);
	resBox->Add(high6x, 0, wxALL, 5);
	optionsSizer->Add(resBox, 1, wxLEFT | wxEXPAND, 10);

	mainSizer->Add(optionsSizer, 0, wxLEFT | wxRIGHT | wxEXPAND, 15);

	// Buttons with proper spacing and styling
	mainSizer->AddSpacer(25);
	wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);

	wxButton* saveBtn = new wxButton(&exportDialog, wxID_OK, "Export to File...");
	wxButton* copyBtn = new wxButton(&exportDialog, wxID_APPLY, "Copy to Clipboard");
	wxButton* cancelBtn = new wxButton(&exportDialog, wxID_CANCEL, "Cancel");

	copyBtn->Bind(wxEVT_BUTTON, [&exportDialog](wxCommandEvent&) {
		exportDialog.EndModal(wxID_APPLY);
	});

	saveBtn->SetDefault(); // Make it blue (default button)

	buttonSizer->Add(0, 0, 1); // Stretchable space
	buttonSizer->Add(saveBtn, 0, wxRIGHT, 10);
	buttonSizer->Add(copyBtn, 0, wxRIGHT, 10);
	buttonSizer->Add(cancelBtn, 0, 0, 0);
	buttonSizer->Add(0, 0, 1); // Stretchable space

	mainSizer->Add(buttonSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 20);

	exportDialog.SetSizer(mainSizer);

	wxString fileLabel;
	fileLabel = GetDocumentTitle();
	auto currentInfo = [&]() {
		ExportInfo info;
		info.enabled = infoCheck->GetValue();
		info.name = nameCtrl->GetValue();
		info.works = worksRadio->GetValue();
		info.why = whyCtrl->GetValue();
		info.fileName = fileLabel;
		return info;
	};
	// A "does not work" answer needs its reason before anything can go out.
	auto updateInfoState = [&]() {
		const bool on = infoCheck->GetValue();
		nameCtrl->Enable(on);
		worksRadio->Enable(on);
		brokenRadio->Enable(on);
		whyCtrl->Enable(on && brokenRadio->GetValue());
		const bool ok = !on || worksRadio->GetValue() || !whyCtrl->GetValue().Strip(wxString::both).empty();
		saveBtn->Enable(ok);
		copyBtn->Enable(ok);
	};

	// Preview update helper — renders at full canvas resolution and downscales for crisp preview
	double contentScaleFactor = exportDialog.GetContentScaleFactor();
	auto updatePreview = [&]() {
		bool showGrid = gridCheck->GetValue();
		bool noColor = bwRadio->GetValue();

		// Render at native canvas size (1:1 with what the user sees)
		const ExportInfo info = currentInfo();
		wxBitmap bmp = getBitmap(showGrid, noColor, 1, &info);
		wxImage img = bmp.ConvertToImage();

		// Compute logical thumbnail size preserving aspect ratio
		int srcW = img.GetWidth(), srcH = img.GetHeight();
		double scale = std::min((double)previewMaxW / srcW, (double)previewMaxH / srcH);
		if (scale > 1.0) scale = 1.0;
		int logicalW = std::max(1, (int)(srcW * scale));
		int logicalH = std::max(1, (int)(srcH * scale));

		// Scale to Retina physical pixels for sharp rendering
		int physW = (int)(logicalW * contentScaleFactor);
		int physH = (int)(logicalH * contentScaleFactor);
		img.Rescale(physW, physH, wxIMAGE_QUALITY_HIGH);

		// Create a Retina-aware bitmap at the logical size
		wxBitmap retinaThumb(img);
		retinaThumb.SetScaleFactor(contentScaleFactor);

		previewBitmap->SetBitmap(retinaThumb);
		previewBitmap->SetMinSize(wxSize(logicalW, logicalH));
		exportDialog.GetSizer()->Layout();
	};

	// Bind option changes to refresh preview
	auto onOptionChange = [&](wxCommandEvent&) { updatePreview(); };
	gridCheck->Bind(wxEVT_CHECKBOX, onOptionChange);
	colorRadio->Bind(wxEVT_RADIOBUTTON, onOptionChange);
	bwRadio->Bind(wxEVT_RADIOBUTTON, onOptionChange);
	auto onInfoChange = [&](wxCommandEvent&) { updateInfoState(); updatePreview(); };
	infoCheck->Bind(wxEVT_CHECKBOX, onInfoChange);
	worksRadio->Bind(wxEVT_RADIOBUTTON, onInfoChange);
	brokenRadio->Bind(wxEVT_RADIOBUTTON, onInfoChange);
	nameCtrl->Bind(wxEVT_TEXT, onInfoChange);
	whyCtrl->Bind(wxEVT_TEXT, onInfoChange);
	updateInfoState();

	// Generate initial preview and size dialog to fit
	updatePreview();
	exportDialog.Fit();
	exportDialog.Centre();

	int result = exportDialog.ShowModal();
	// Remember the answers (and the name, for good) even on Cancel.
	lastExportInfo = currentInfo();
	appConfig().appSettings.exportInfoEnabled = lastExportInfo.enabled;
	appConfig().appSettings.studentName = std::string(nameCtrl->GetValue().Strip(wxString::both).ToUTF8());
	if (result == wxID_CANCEL) return;
	const ExportInfo info = lastExportInfo;

	// Get user choices
	bool showGrid = gridCheck->GetValue();
	bool useNoColor = bwRadio->GetValue();
	int multiplier = screen2x->GetValue() ? 2 : (print4x->GetValue() ? 4 : 6);

	// Generate bitmap
	wxBitmap bitmap = getBitmap(showGrid, useNoColor, multiplier, &info);

	// Handle action
	if (result == wxID_APPLY) {
		// Copy to clipboard
		if (wxTheClipboard->Open()) {
			wxTheClipboard->SetData(new wxBitmapDataObject(bitmap));
			wxTheClipboard->Flush();
			wxTheClipboard->Close();
		}
	} else if (result == wxID_OK) {
		// Save to file
		wxString caption = "Export Circuit";
		wxString wildcard = "SVG (*.svg)|*.svg|PNG (*.png)|*.png|Bitmap (*.bmp)|*.bmp";
		wxFileDialog saveDialog(this, caption, wxEmptyString, "", wildcard, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
		saveDialog.SetDirectory(lastDirectory);

		if (saveDialog.ShowModal() == wxID_OK) {
			wxString path = saveDialog.GetPath();
			wxString ext = path.SubString(path.find_last_of(".") + 1, path.length());

			if (ext == "svg") {
				// Vector SVG through the Scene seam (Skia) -- the same renderer
				// the canvas uses, so the export is faithful. The page is the
				// canvas size x the chosen multiplier; SVG stays crisp anyway.
				wxSize sz = currentCanvas->GetClientSize();
				bool success = renderToSvgSkia(path, sz.GetWidth() * multiplier,
				                               sz.GetHeight() * multiplier,
				                               showGrid, useNoColor, &info);
				if (!success) {
					ui::Message("Failed to export SVG file.", "Export Error", wxOK | wxICON_ERROR);
				}
			} else {
				// Export as bitmap (PNG default; BMP when explicitly chosen).
				wxBitmapType fileType = (ext == "bmp") ? wxBITMAP_TYPE_BMP
				                                       : wxBITMAP_TYPE_PNG;
				bitmap.SaveFile(path, fileType);
			}
		}
	}
}

void MainFrame::OnExportLegacy(wxCommandEvent& event) {

	wxString caption = "Export v1.x Compatible Circuit";
	wxString wildcard = "Circuit files (*.cdl)|*.cdl";
	wxString defaultFilename = "";
	wxFileDialog dialog(this, caption, wxEmptyString, defaultFilename, wildcard, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
	dialog.SetDirectory(lastDirectory);
	if (dialog.ShowModal() == wxID_OK) {
		wxString path = dialog.GetPath();

		// Pause system during save (restoring the step-in-flight flag after,
		// not forcing it on -- see save())
		lock();
		const bool wasSimulating = gCircuit->getSimulate();
		gCircuit->setSimulate(false);

		// Save in legacy format
		CircuitParse cirp(currentCanvas);
		bool success = cirp.saveCircuitLegacy((string)path, canvases);

		// Resume system
		gCircuit->setSimulate(wasSimulating);
		if (!(toolBar->GetToolState(Tool_Lock))) {
			unlock();
		}

		if (!success) {
			// Check the error message to distinguish between I/O error and bus features
			CircuitParse cirpCheck(currentCanvas);
			string errorMsg = cirp.getLastError();

			if (errorMsg.find("Warning:") == 0) {
				// This is a bus features warning, file was saved successfully
				ui::Message(errorMsg, "Export Warning", wxOK | wxICON_WARNING);
			} else {
				// This is an I/O error
				wxString fullMsg = "Failed to export file:\n\n" + errorMsg;
				ui::Message(fullMsg, "Export Error", wxOK | wxICON_ERROR);
			}
		}
	}
}

// Export a copy in the v2 (pre-v3 XML) format without changing the open file.
void MainFrame::OnExportV2(wxCommandEvent& event) {

	wxString wildcard = "Circuit files (*.cdl)|*.cdl";
	wxFileDialog dialog(this, "Export v2 (legacy XML) Circuit", wxEmptyString, "",
	                    wildcard, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
	dialog.SetDirectory(lastDirectory);
	if (dialog.ShowModal() == wxID_OK) {
		wxString path = dialog.GetPath();

		lock();
		const bool wasSimulating = gCircuit->getSimulate();   // see save()
		gCircuit->setSimulate(false);

		CircuitParse cirp(currentCanvas);
		bool success = cirp.saveCircuit((string)path, canvases);

		gCircuit->setSimulate(wasSimulating);
		if (!(toolBar->GetToolState(Tool_Lock))) unlock();

		if (!success) {
			ui::Message("Failed to export file:\n\n" + cirp.getLastError(),
			             "Export Error", wxOK | wxICON_ERROR);
		}
	}
}

void MainFrame::OnCopyToClipboard(wxCommandEvent& event) {
	// Redirect to unified export dialog
	OnExportBitmap(event);
}

#ifdef WITH_SKIA
// Lays out the name-and-result strip that goes under an exported circuit and,
// when `scene` is given, draws it with its top edge at `yTop` of a `totalH`
// tall page. Returns the strip's height. Black on white so it prints in B&W.
// `m` scales it with the export resolution.
static float exportInfoStrip(cl::render::Scene* scene, const MainFrame::ExportInfo& info,
                             float W, float yTop, float totalH, float m) {
	using namespace cl::render;
	// Sized to be read at a glance by a grader flipping through printouts.
	const float pad = 22.0f * m, bodyPx = 17.0f * m, smallPx = 11.0f * m, lineGap = 8.0f * m;
	const float maxW = W - 2 * pad;

	// The statement, word-wrapped to the page width.
	const std::string statement = info.works
		? std::string("My circuit works properly.")
		: "My circuit does not work because " + std::string(info.why.Strip(wxString::both).ToUTF8());
	std::vector<std::string> lines;
	{
		std::istringstream words(statement);
		std::string word, line;
		while (words >> word) {
			const std::string trial = line.empty() ? word : line + " " + word;
			if (!line.empty() && measuredTextWidth(trial.c_str(), bodyPx) > maxW) {
				lines.push_back(line);
				line = word;
			} else {
				line = trial;
			}
		}
		if (!line.empty()) lines.push_back(line);
	}
	const float height = pad + bodyPx + lineGap * 1.6f + lines.size() * (bodyPx + lineGap) + pad * 0.5f;
	if (scene == nullptr) return height;

	Transform t;   // top-down page px; text() wants the Y-up flip
	t.a = 1; t.b = 0; t.c = 0; t.d = -1; t.e = 0; t.f = totalH;
	scene->setViewport(t);
	auto at = [totalH](float x, float y) { return Point(x, totalH - y); };
	const Color ink(0, 0, 0, 1), gray(0.40f, 0.40f, 0.40f, 1);

	const Point rule[2] = { at(pad, yTop + 0.5f * m), at(W - pad, yTop + 0.5f * m) };
	scene->lines(rule, 2, Stroke(ink, 1.0f * m));

	float y = yTop + pad;
	const std::string label = "Name:";
	scene->text(at(pad, y), label.c_str(), bodyPx, gray);
	const std::string name = info.name.Strip(wxString::both).empty()
		? std::string("________________") : std::string(info.name.Strip(wxString::both).ToUTF8());
	scene->text(at(pad + measuredTextWidth(label.c_str(), bodyPx) + 8 * m, y), name.c_str(), bodyPx, ink);

	std::string meta = std::string(wxDateTime::Now().FormatDate().ToUTF8());
	if (!info.fileName.empty()) meta = std::string(info.fileName.ToUTF8()) + "   " + meta;
	scene->text(at(W - pad - measuredTextWidth(meta.c_str(), smallPx), y + (bodyPx - smallPx)),
	            meta.c_str(), smallPx, gray);

	y += bodyPx + lineGap * 1.6f;
	for (const std::string& l : lines) {
		scene->text(at(pad, y), l.c_str(), bodyPx, ink);
		y += bodyPx + lineGap;
	}
	return height;
}
#endif

wxBitmap MainFrame::getBitmap(bool withGrid, bool noColor, int multiplier, const ExportInfo* info) {
	bool gridlineVisible = appConfig().appSettings.gridlineVisible;
	appConfig().appSettings.gridlineVisible = withGrid;
	renderMode().doingBitmapExport = true;

	// Render through Skia into an offscreen raster surface. This replaces the old
	// offscreen-GL readback: no second (unshared) GL context, and the same
	// renderer as the screen, so an export matches what the canvas shows.
	// noColor renders gates/wires as black line drawings, for printing.
	wxSize imageSize = currentCanvas->GetClientSize();
	const int w = imageSize.GetWidth() * multiplier;
	const int h = imageSize.GetHeight() * multiplier;
	const bool strip = info != nullptr && info->enabled;
	const float stripH = strip ? exportInfoStrip(nullptr, *info, (float)w, (float)h, 0, (float)multiplier) : 0.0f;
	const int totalH = h + (int)std::ceil(stripH);
	wxImage circuitImage(w, totalH);
	{
		GUICanvas *canvas = currentCanvas;
		cl::render::RenderStyle style = noColor ? cl::render::RenderStyle::print()
		                                        : cl::render::RenderStyle::screen();
		style.showGrid = withGrid;
		if (!cl::render::skiaRenderToRGB(w, totalH,
				[canvas, &style, w, h, strip, info, totalH, multiplier](cl::render::Scene &scene) {
					canvas->renderToScene(scene, style, w, h);
					if (strip) exportInfoStrip(&scene, *info, (float)w, (float)h, (float)totalH, (float)multiplier);
				},
				circuitImage.GetData())) {
			circuitImage.Clear(0xFF);   // white, so a failure exports blank not garbage
		}
	}
	wxBitmap circuitBitmap(circuitImage);

	// restore grid display setting
	appConfig().appSettings.gridlineVisible = gridlineVisible;
	renderMode().doingBitmapExport = false;

	return circuitBitmap;
}

void MainFrame::OnPause(wxCommandEvent& event) {
	PauseSim();
}

void MainFrame::OnStep(wxCommandEvent& event) {
	if (!(currentCanvas->getCircuit()->getSimulate())) {
		return;
	}
	gCircuit->exemptNextStepFromTiming();
	gCircuit->sendMessageToCore(klsMessage::Message(klsMessage::MT_STEPSIM, new klsMessage::Message_STEPSIM(1)));
	currentCanvas->getCircuit()->setSimulate(false);
}

void MainFrame::setToolIcon(int toolId, const wxBitmapBundle& icon, const char* sfSymbol) {
#ifdef __WXOSX__
	// Straight to AppKit: wx 3.2 derives its own alternate (checked-state) image
	// for a toggle tool on this platform and gets it wrong. See NativeIcons.h.
	NativeIcon_SetToolbarSFSymbol(toolBar, toolId, sfSymbol, 18);
#else
	toolBar->SetToolNormalBitmap(toolId, icon);
#endif
}

void MainFrame::OnLock(wxCommandEvent& event) {
	if (toolBar->GetToolState(Tool_Lock)) {
		lock();
		setToolIcon(Tool_Lock, lockedIcon, "lock.fill");
	} else {
		unlock();
		setToolIcon(Tool_Lock, unlockedIcon, "lock.open.fill");
	}
}

void MainFrame::OnZoomIn(wxCommandEvent& event) {
	//TODO: There is no way to check if currentCanvas is valid first!!!
	currentCanvas->zoomIn();
}

void MainFrame::OnZoomOut(wxCommandEvent& event) {
	//TODO: There is no way to check if currentCanvas is valid first!!!
	currentCanvas->zoomOut();
}

void MainFrame::OnHelpContents(wxCommandEvent& event) {
	wxGetApp().ensureHelpBookLoaded();   // parsed on demand, not at launch
	wxGetApp().helpController->DisplayContents();
}

void MainFrame::OnTimeStepModSlider(wxScrollEvent& event) {
	// Update the value first, then rebuild the label from it -- otherwise the
	// readout lags one change behind the slider.
	appConfig().timeStepMod = timeStepModSlider->GetValue();
	wxString oss;
	oss << appConfig().timeStepMod << "ms";
	timeStepModVal->SetLabel(oss);
}


void MainFrame::saveSettings() {
	wxConfigBase *conf = wxConfigBase::Get();
	auto settings = appConfig().appSettings;

	// A maximized or minimized window's rect is not the one to come back to:
	// keep the last normal one and remember the state beside it.
	conf->Write("FrameMaximized", IsMaximized());
	if (!IsMaximized() && !IsIconized() && !IsFullScreen()) {
		conf->Write("FrameWidth", GetSize().GetWidth());
		conf->Write("FrameHeight", GetSize().GetHeight());
		conf->Write("FrameLeft", GetPosition().x);
		conf->Write("FrameTop", GetPosition().y);
	}
	conf->Write("TimeStep", appConfig().timeStepMod);
	conf->Write("RefreshRate", settings.refreshRate);
	conf->Write("AutosaveSeconds", settings.autosaveSeconds);
	conf->Write("LastDirectory", lastDirectory);
	conf->Write("LastLibraryDoc", wxString(appConfig().appSettings.lastLibraryDoc));
	conf->Write("StudentName", wxString::FromUTF8(settings.studentName.c_str()));
	conf->Write("ExportInfoEnabled", settings.exportInfoEnabled);
	conf->Write("ToolbarStyle", settings.toolbarStyle);
	conf->Write("ToolbarHidden", settings.toolbarHidden);
	conf->Write("WireConnRadius", settings.wireConnRadius);
	conf->Write("WireConnVisible", settings.wireConnVisible);
	conf->Write("GridlineVisible", settings.gridlineVisible);
	conf->Write("MajorGridVisible", settings.majorGridVisible);
	conf->Write("ClassicTabs", settings.classicTabs);
	conf->Write("HasSeenWelcome", settings.hasSeenWelcome);
	conf->Write("GridStyle", settings.gridStyle);
	conf->Write("AccentColor", settings.accentColor);
	conf->Write("WireThickness", settings.wireThickness);
	conf->Write("ShowStatusInfo", settings.showStatusInfo);
	conf->Write("MouseWheelAction", settings.mouseWheelAction);
	conf->Write("TrackpadScrollAction", settings.trackpadScrollAction);
	conf->Write("ReverseTrackpadZoom", settings.reverseTrackpadZoom);
	conf->Write("ReverseWheelZoom", settings.reverseWheelZoom);
	conf->Write("SidePanelWidth", settings.sidePanelWidth);
	conf->Write("PaletteGateSize", settings.paletteGateSize);
	conf->Write("DuplicateUsesClipboard", settings.duplicateUsesClipboard);
	conf->Write("RightClickRotate", settings.rightClickRotate);

	conf->Write("ThemeMode", settings.themeMode);
	// The CURRENT theme, not whatever settings.lastDarkMode still holds from
	// launch -- so ThemeMode::RememberLast reopens in whatever this session
	// ended up in, including a mid-session toggle.
	conf->Write("ThemeLastDark", renderMode().darkMode);
	conf->Write("ThemeShortcutEnabled", settings.themeShortcutEnabled);
	conf->Write("ThemeShortcutKeyCode", settings.themeShortcutKeyCode);
	conf->Write("ThemeShortcutModifiers", settings.themeShortcutModifiers);
	conf->Write("ThemeToggleButtonVisible", settings.showThemeToggleButton);
	// To disk now rather than whenever wx deletes the config: a crash, a forced
	// quit or the Windows _Exit in MainApp::OnExit would otherwise lose it all.
	conf->Flush();
}

void MainFrame::ResumeExecution() {
	if (toolBar->GetToolState(Tool_Pause)) {
		toolBar->ToggleTool(Tool_Pause, false);
		PauseSim();
	}
	else {
		//do nothing
	}
}

void MainFrame::PauseSim() {
	// Whatever the last stop was about, the user has decided; clear the notice.
	SetStatusText("");
	gCircuit->lateSteps = 0;
	if (toolBar->GetToolState(Tool_Pause)) {
		simTimer->Stop();
		simBridge().appSystemTime.Start(0);
		simBridge().appSystemTime.Pause();
		setToolIcon(Tool_Pause, playIcon, "play.fill");
	}
	else {
		simBridge().appSystemTime.Start(0);
		simTimer->Start(TIMER_POLL_MS);
		setToolIcon(Tool_Pause, pauseIcon, "pause.fill");
	}
}

//Julian: Added to simplify timer use.
void MainFrame::stopTimers() {
	simTimer->Stop();
	idleTimer->Stop();
}

void MainFrame::startTimers(int at) {
	if (!(toolBar->GetToolState(Tool_Pause)))
	{
		// The stopwatch too: a load paused it (startup recovery loads before
		// the timers first start), and stepSimulation only steps once it has
		// accrued a step's worth of time, so a paused one froze the circuit.
		simBridge().appSystemTime.Start(0);
		simTimer->Start(at);
	}
	idleTimer->Start(at);
}

void MainFrame::pauseTimers() {
	simBridge().appSystemTime.Pause();
	stopTimers();
}
void MainFrame::resumeTimers(int at) {
	if (!(toolBar->GetToolState(Tool_Pause)))
	{
		simBridge().appSystemTime.Start(0);
		simTimer->Start(at);
	}
	idleTimer->Start(at);
}

//Julian: All of the following functions were added to support autosave functionality.

// The Preferences setting, in milliseconds. CEDAR_AUTOSAVE_SECONDS overrides it
// so the recovery path can be exercised without waiting out a real interval or
// disturbing the user's own preference.
int MainFrame::autosaveIntervalMs() {
	const char* override = getenv("CEDAR_AUTOSAVE_SECONDS");
	const int seconds = override ? atoi(override)
	                             : appConfig().appSettings.autosaveSeconds;
	return seconds > 0 ? seconds * 1000 : 0;   // 0 = off
}

// Start, restart or stop the timer to match the current setting. Called at
// startup and whenever Preferences is accepted.
void MainFrame::applyAutosaveInterval() {
	if (!autosaveTimer) return;
	// Always on: every few seconds, whatever changed goes into the library.
	autosaveTimer->Stop();
	if (captureWatchdog) captureWatchdog->Stop();
	autosaveTimer->Start(4000);
}

// Offer back anything a session that died left behind. Each entry names the
// document it was taken from and when, because "there may have been a crash" is
// no help to someone deciding whether it is worth recovering.
void MainFrame::offerRecovery() {
	// A headless render is a one-shot pass with nobody at the keyboard, so a
	// prompt there hangs the process instead of asking anyone anything. Leave
	// the snapshots for the next real session to offer.
	if (renderMode().headlessRender) return;

	// Whether a leftover file holds a circuit, which is how a snapshot caught
	// mid-write is told from one that was merely never renamed into place.
	autosaveStore::ReadableTest readable = [](const std::string& path) {
		cl::LoadResult ignored;
		std::string error;
		return CircuitParse::readCircuit(path, ignored, error);
	};

	std::vector<AutosaveEntry> pending = autosaveStore::findRecoverable(readable);
	for (unsigned int i = 0; i < pending.size(); i++) {
		const AutosaveEntry& entry = pending[i];
		wxString of = entry.originalPath.empty()
			? wxString("a circuit that was never saved")
			: wxFileName(entry.originalPath).GetFullName();
		wxString message =
			"CedarLogic closed unexpectedly with unsaved work.\n\n"
			"Recover " + of + "?\n"
			"Last autosaved " + entry.takenAt + ".";
		ui::MessageDialog dialog(this, message, "Recover Work",
		                       wxYES_DEFAULT | wxYES_NO | wxICON_QUESTION);
		if (dialog.ShowModal() != wxID_YES) {
			autosaveStore::discard(entry);   // they have seen it and said no
			continue;
		}

		doOpenFile = false;
		// As a copy, which is exactly what a snapshot is: it leaves
		// openedFilename empty and takes no lock on the snapshot file. The
		// path is deliberately NOT the recovered document's -- the snapshot
		// is not that file, and Save must not quietly overwrite the last
		// good copy on disk with it. Empty sends Ctrl+S to Save As, where
		// the original name is offered as the default.
		if (!loadCircuitFile(entry.snapshotPath, /*asCopy=*/true)) {
			// Unreadable, so it is not ours to delete: the user has been told
			// why, and a file on disk can still be looked at by hand.
			continue;
		}
		recoveredFrom = entry.originalPath;
		SetTitle(VERSION_TITLE() + " - recovered " + of);

		// Take the snapshot over rather than deleting it. Recovering puts the
		// work back on screen, not on disk -- it still has no file of its own --
		// so dropping the snapshot here would leave one copy, in memory, until
		// this session's first autosave, and a second crash in that window takes
		// the lot. Adopting it keeps something on disk the whole way through,
		// and counting the document as unsaved means closing asks about it and
		// autosave refreshes it even if they never touch a gate.
		autosaveStore::adopt(entry);
		recoveredUnsaved = true;
		// They asked for this back, so it stays in front: reopening the last
		// library circuit now would save it away and put something else up.
		pendingLibraryOpen.clear();
	}
}

// Fires on the GUI thread every AUTOSAVE_INTERVAL_MS. Nothing else can be part
// way through an edit here, so the circuit is safe to walk.
void MainFrame::OnAutosaveTimer(wxTimerEvent& WXUNUSED(event)) {
	if (renderMode().headlessRender || !fileIsDirty()) return;
	if (wxGetMouseState().LeftIsDown()) return;   // mid-drag: next tick
	if (saveToLibrary(false)) {
		if (autosaveFailing) SetStatusText("Saved.");
		autosaveFailing = false;
		return;
	}
	// The app promises that work saves itself, so a save that doesn't is
	// worth interrupting for -- once. After that the status bar keeps saying
	// so, and it is tried again every tick until it works.
	SetStatusText("Couldn't save automatically: " + wxString(lastSaveError));
	if (autosaveFailing || renderMode().headlessRender) return;
	autosaveFailing = true;
	ui::Message("Your circuit couldn't be saved automatically:\n\n" + wxString(lastSaveError) +
	             "\n\nIt is still on screen, and saving will be retried every few seconds. "
	             "File > Export as CedarLogic File saves a copy somewhere else.",
	             "Couldn't Save", wxOK | wxICON_WARNING, this);
}

void MainFrame::autosave() {
	// Best effort: if it fails the user can still save by hand. Worth knowing
	// about while developing, hence the log rather than a silent drop.
	if (!save(autosaveStore::pendingPath())) {
		wxLogDebug("Autosave failed: %s", lastSaveError.c_str());
		return;
	}
	// The record names the snapshot only once the snapshot is really there.
	if (autosaveStore::commitPending()) {
		// A recovered circuit has no path of its own yet, but it is still of
		// some document, and that name is what makes the prompt after a second
		// crash say more than "a circuit that was never saved".
		autosaveStore::writeRecord(openedFilename.empty()
			? recoveredFrom : openedFilename.ToStdString());
	} else {
		wxLogDebug("Autosave failed: could not replace the previous snapshot");
	}
}

bool MainFrame::save(string filename, int format) {
	//Pause system so that user can't modify during save
	lock();
	// Put back whatever it was, rather than forcing it on afterwards. `simulate`
	// is false while a step is in flight, and that is what holds new messages
	// back for the core (GUICircuit::sendMessageToCore) until the step's
	// MT_DONESTEP flushes them in order. Switching it on mid-step let later
	// messages overtake held ones -- and autosave runs every few seconds, so
	// mid-step is not rare.
	const bool wasSimulating = gCircuit->getSimulate();
	gCircuit->setSimulate(false);

	// Disabling timers from autosave thread caused an assertion fail.
	//pauseTimers();

	//Save file in the requested format (v3 by default).
	CircuitParse cirp(currentCanvas);
	bool success;
	if (format == 1) success = cirp.saveCircuitLegacy(filename, canvases);
	else if (format == 2) success = cirp.saveCircuit(filename, canvases);
	else success = cirp.saveCircuitV3(filename, canvases);

	// Store the error message for the caller
	if (!success) {
		lastSaveError = cirp.getLastError();
	}

	// Disabling timers from autosave thread caused an assertion fail.
	//Resume system
	//resumeTimers(20);

	gCircuit->setSimulate(wasSimulating);
	if (!(toolBar->GetToolState(Tool_Lock))) {
		unlock();
	}

	return success;
}

bool MainFrame::fileIsDirty() {
	// Recovered work counts as unsaved before it is edited at all: the command
	// stack is empty, but the circuit on screen answers to no file on disk.
	return commandProcessor->IsDirty() || recoveredUnsaved;
}

void MainFrame::removeTempFile() {
	autosaveStore::clearOwn();
}


void MainFrame::lock() {
	for (unsigned int i = 0; i < canvases.size(); i++) {
		canvases[i]->lockCanvas();
	}
}

void MainFrame::unlock() {
	for (unsigned int i = 0; i < canvases.size(); i++) {
		canvases[i]->unlockCanvas();
	}
}

void MainFrame::load(string filename) {
	loadCircuitFile(filename);
}

bool MainFrame::renderToPngSkia(const wxString &path, int width, int height) {
#ifdef WITH_SKIA
	if (currentCanvas == NULL) return false;
	GUICanvas *canvas = currentCanvas;
	// Draw through the engine-neutral Scene into a Skia raster surface. The
	// callback keeps Skia headers out of this TU (see SkiaProbe.h).
	cl::render::RenderStyle style = cl::render::RenderStyle::screen();
	return cl::render::skiaRenderToPng(
		path.ToStdString().c_str(), width, height,
		[canvas, &style, width, height](cl::render::Scene &scene) {
			canvas->renderToScene(scene, style, width, height);
		});
#else
	(void)path; (void)width; (void)height;
	return false;
#endif
}

bool MainFrame::renderSingleGate(const std::string &gateName, const std::string &angle,
                                 const wxString &path, int width, int height) {
	if (currentCanvas == NULL || gCircuit == NULL) return false;

	// Build one gate of this type through the real creation path, so its shape,
	// hotspots and params are exactly what the app would produce. createGate
	// registers it in the circuit and stamps its id; we still add it to the
	// canvas list that the renderer iterates.
	guiGate *g = gCircuit->createGate(gateName, -1);
	if (g == NULL) return false;

	// Rotate before placing: insertGate -> setGLcoords -> updateBBoxes reads the
	// "angle" gparam to build the gate's model matrix.
	g->setGUIParam("angle", angle);
	currentCanvas->insertGate(g->getID(), g, 0.0f, 0.0f);
	currentCanvas->Update();

	return renderToPngSkia(path, width, height);
}

// Shared scene for the wire-router test hooks.
struct ProbeScene {
	guiWire *wire = nullptr;
	guiGate *A = nullptr;
	guiGate *B = nullptr;
	std::string outName, inName;
};

// Two gates a fixed distance apart, then a wire from A's first output to B's
// first input -- this drives guiWire::addConnection -> calcShape, so a dump
// captures exactly what the router produced. Deterministic: gate/hotspot choice
// and positions are fixed, so before/after dumps diff cleanly.
static ProbeScene buildProbeWire(GUICircuit *gCircuit, GUICanvas *canvas,
                                 const std::string &gateA, const std::string &gateB,
                                 const std::string &angleA, const std::string &angleB) {
	ProbeScene sc;
	sc.A = gCircuit->createGate(gateA, -1);
	sc.B = gCircuit->createGate(gateB, -1);
	if (sc.A == NULL || sc.B == NULL) { sc.A = sc.B = nullptr; return sc; }
	sc.A->setGUIParam("angle", angleA);
	sc.B->setGUIParam("angle", angleB);
	canvas->insertGate(sc.A->getID(), sc.A, -8.0f, 0.0f);
	canvas->insertGate(sc.B->getID(), sc.B, 8.0f, 0.0f);
	canvas->Update();

	for (const auto &kv : sc.A->getHotspotList())
		if (!sc.A->isConnectionInput(kv.first)) { sc.outName = kv.first; break; }
	for (const auto &kv : sc.B->getHotspotList())
		if (sc.B->isConnectionInput(kv.first)) { sc.inName = kv.first; break; }
	if (sc.outName.empty() || sc.inName.empty()) return sc;

	std::vector<IDType> wireIds = { gCircuit->getNextAvailableWireID() };
	gCircuit->setWireConnection(wireIds, sc.A->getID(), sc.outName);
	sc.wire = gCircuit->setWireConnection(wireIds, sc.B->getID(), sc.inName);
	return sc;
}

// Dump a wire's segment map as deterministic text under `label`.
static void dumpSegMapTo(std::ofstream &f, guiWire *wire, const char *label) {
	f << "-- " << label << " --\n";
	std::map<long, wireSegment> sm = wire->getSegmentMap();
	for (const auto &kv : sm) {
		const wireSegment &s = kv.second;
		f << "seg " << s.id << (s.verticalSeg ? " V " : " H ")
		  << "(" << s.begin.x << "," << s.begin.y << ")-("
		  << s.end.x << "," << s.end.y << ") conn=[";
		for (const auto &c : s.connections) f << c.connection << ":" << c.gid << " ";
		f << "] xs={";
		for (const auto &ix : s.intersects) {
			f << ix.first << ":";
			for (long id : ix.second) f << id << ",";
			f << " ";
		}
		f << "}\n";
	}
}

bool MainFrame::dumpWireShape(const std::string &gateA, const std::string &gateB,
                              const std::string &angleA, const std::string &angleB,
                              const wxString &path) {
	if (currentCanvas == NULL || gCircuit == NULL) return false;
	ProbeScene sc = buildProbeWire(gCircuit, currentCanvas, gateA, gateB, angleA, angleB);
	if (sc.wire == NULL) return false;

	std::ofstream f(path.ToStdString().c_str());
	if (!f) return false;
	f << "wire " << gateA << "@" << angleA << "." << sc.outName
	  << " -> " << gateB << "@" << angleB << "." << sc.inName << "\n";

	dumpSegMapTo(f, sc.wire, "create"); // guiWire::calcShape output
	sc.B->setGLcoords(11.0f, 2.0f);     // move B -> guiGate::updateBBoxes notifies the
	currentCanvas->Update();            // wire, driving updateConnectionPos/updateSegDrag
	dumpSegMapTo(f, sc.wire, "after move B");
	return true;
}

bool MainFrame::dumpWireDrag(const std::string &gateA, const std::string &gateB,
                             const std::string &angleA, const std::string &angleB,
                             const wxString &path) {
	if (currentCanvas == NULL || gCircuit == NULL) return false;
	ProbeScene sc = buildProbeWire(gCircuit, currentCanvas, gateA, gateB, angleA, angleB);
	if (sc.wire == NULL) return false;

	std::ofstream f(path.ToStdString().c_str());
	if (!f) return false;
	f << "drag " << gateA << "@" << angleA << "." << sc.outName
	  << " -> " << gateB << "@" << angleB << "." << sc.inName << "\n";
	dumpSegMapTo(f, sc.wire, "create");

	// Pick the longest segment (begin <= end always, so no abs needed) and grab
	// its midpoint. A zero-size mouse box exactly on that segment selects it, the
	// same point-box the canvas passes to startSegDrag/updateSegDrag (snapMouse).
	std::map<long, wireSegment> sm = sc.wire->getSegmentMap();
	const wireSegment *pick = nullptr; float bestLen = -1.0f;
	for (const auto &kv : sm) {
		const wireSegment &s = kv.second;
		float len = (s.end.x - s.begin.x) + (s.end.y - s.begin.y);
		if (len > bestLen) { bestLen = len; pick = &kv.second; }
	}
	if (pick == nullptr) { f << "-- no segment to drag --\n"; return true; }
	GLPoint2f mid((pick->begin.x + pick->end.x) * 0.5f,
	              (pick->begin.y + pick->end.y) * 0.5f);
	bool vertical = pick->verticalSeg;

	klsCollisionObject mouse(COLL_MOUSEBOX);
	klsBBox startBox; startBox.addPoint(mid); mouse.setBBox(startBox);
	if (!sc.wire->startSegDrag(&mouse)) { f << "-- drag skipped (no segment under mouse) --\n"; return true; }

	// Drag perpendicular by a fixed grid delta (x for a vertical seg, y for a
	// horizontal one) so the reshape is real and reproducible.
	GLPoint2f target = vertical ? GLPoint2f(mid.x + 2.0f, mid.y)
	                            : GLPoint2f(mid.x, mid.y + 2.0f);
	klsBBox endBox; endBox.addPoint(target); mouse.setBBox(endBox);
	sc.wire->updateSegDrag(&mouse);
	sc.wire->endSegDrag();
	dumpSegMapTo(f, sc.wire, "after drag");
	return true;
}

// Build the export RenderStyle from the dialog's choices: black-on-white with
// no live signal colors when "Black & White" is picked (print intent), full
// color otherwise. Selection overlays never belong in an exported file.
#ifdef WITH_SKIA
static cl::render::RenderStyle exportStyle(bool showGrid, bool noColor) {
	cl::render::RenderStyle style;
	style.colorOutput = !noColor;
	style.showLiveState = !noColor;
	style.showGrid = showGrid;
	style.showSelection = false;
	return style;
}
#endif

bool MainFrame::renderToSvgSkia(const wxString &path, int width, int height,
                                bool showGrid, bool noColor, const ExportInfo* info) {
#ifdef WITH_SKIA
	if (currentCanvas == NULL) return false;
	GUICanvas *canvas = currentCanvas;
	cl::render::RenderStyle style = exportStyle(showGrid, noColor);
	const bool strip = info != nullptr && info->enabled;
	const float m = width / (float)std::max(1, currentCanvas->GetClientSize().GetWidth());
	const float stripH = strip ? exportInfoStrip(nullptr, *info, (float)width, (float)height, 0, m) : 0.0f;
	const int totalH = height + (int)std::ceil(stripH);
	return cl::render::skiaRenderToSvg(
		path.ToStdString().c_str(), width, totalH,
		[canvas, &style, width, height, strip, info, totalH, m](cl::render::Scene &scene) {
			canvas->renderToScene(scene, style, width, height);
			if (strip) exportInfoStrip(&scene, *info, (float)width, (float)height, (float)totalH, m);
		});
#else
	(void)path; (void)width; (void)height; (void)showGrid; (void)noColor; (void)info;
	return false;
#endif
}

bool MainFrame::renderToPdfSkia(const wxString &path, int width, int height,
                                bool showGrid, bool noColor) {
#ifdef WITH_SKIA
	if (currentCanvas == NULL) return false;
	GUICanvas *canvas = currentCanvas;
	cl::render::RenderStyle style = exportStyle(showGrid, noColor);
	return cl::render::skiaRenderToPdf(
		path.ToStdString().c_str(), width, height,
		[canvas, &style, width, height](cl::render::Scene &scene) {
			canvas->renderToScene(scene, style, width, height);
		});
#else
	(void)path; (void)width; (void)height; (void)showGrid; (void)noColor;
	return false;
#endif
}

void MainFrame::openFileFromFinder(const wxString& fileName) {
	doOpenFile = true;
	openedFilename = fileName;
}

void MainFrame::PreGateDrag() {
	currentCanvas->CaptureMouse();
}

//JV - Make new canvas and add it to canvases and canvasBook
//TODO - Find a way to put a tab button in correct place
void MainFrame::OnNewTab(wxCommandEvent& event) {
	int canSize = canvases.size();

	if (canSize < 42) {
		gCircuit->GetCommandProcessor()->Submit((wxCommand*)new cmdAddTab(gCircuit, canvasBook, &canvases));
		// Go to the new tab. SetSelection fires OnNotebookPage, which does the rest.
		if ((int)canvases.size() > canSize) {
			GUICanvas* added = canvases.back();
			if (pendingNewTabPane == 1 && PaneCount() > 1) MoveCanvasToPane(added, 1, -1);
			else SelectCanvas(added);
			added->playAppearAnimation();
			RenumberTabs();
		}
	}
	else {
		ui::Message("You have reached the maximum number of tabs.", "Close", wxOK);
	}
	 
/*	canvases.push_back(new GUICanvas(canvasBook, gCircuit, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS));
	ostringstream oss;
	oss << "Page " << canvases.size();
	canvasBook->AddPage(canvases[canvases.size()-1], oss.str(), false);*/

}

// File > Close Tab (Cmd/Ctrl+W). The tab being worked in -- with a split
// open, that is not necessarily a page of the first pane at all.
void MainFrame::OnCloseTab(wxCommandEvent& WXUNUSED(event)) {
	// A second Cmd+W while the first tab is still dimming closes the next
	// one, as in a browser, rather than being swallowed by the first.
	flushPendingClose();
	CloseTabCanvas(currentCanvas);
}

void MainFrame::OnReportABug(wxCommandEvent& event) {
	// Tyler Drake can remap the url using cedar.to/create
	// Don't change the url here!
	//wxLaunchDefaultBrowser("https://cedar.to/XoQJpX", 0);
	ui::Message("Feature temporarily unavailable!");
}

void MainFrame::OnRequestAFeature(wxCommandEvent& event) {
	// Tyler Drake can remap the url using cedar.to/create
	// Don't change the url here!
	//wxLaunchDefaultBrowser("https://cedar.to/6IlP8c", 0);
	ui::Message("Feature temporarily unavailable!");
}

void MainFrame::OnDownloadLatestVersion(wxCommandEvent& event) {
	// Belt and braces: the menu item is disabled under policy, but an
	// accelerator or a programmatic menu event can still reach this handler,
	// and it would otherwise put a request on the wire.
	if (cl::update::checksDisabled()) {
		ui::Message("Updates for CedarLogic are managed by your administrator.",
		             "Updates are managed", wxOK | wxICON_INFORMATION, this);
		return;
	}
#ifdef __APPLE__
	SparkleUpdater_CheckForUpdates();
#elif defined(_WIN32)
	WinSparkleUpdater_CheckForUpdates();
#else
	// Tyler Drake can remap the url using cedar.to/create
	// Don't change the url here!
	wxLaunchDefaultBrowser("https://cedar.to/vjyQw7", 0);
#endif
}

void MainFrame::OnKeyboardShortcuts(wxCommandEvent& WXUNUSED(event)) {
	// A searchable, scrolling sheet (ShortcutsSheet.cpp). The plain grid that
	// was here grew taller than the screen and ran under its own OK button.
	ShowShortcutsSheet(this);
}

void MainFrame::releaseStaleCapture() {
	wxWindow* holder = wxWindow::GetCapture();
	if (holder == nullptr) return;
	// A button still down means a real drag -- leave it alone.
	const wxMouseState ms = wxGetMouseState();
	if (ms.LeftIsDown() || ms.RightIsDown() || ms.MiddleIsDown()) return;
	// A canvas may hold the mouse with the button up on purpose: a paste or a
	// new gate following the pointer, or a click-to-connect line.
	if (GUICanvas* canvas = dynamic_cast<GUICanvas*>(holder)) {
		if (!canvas->isIdleForCapture()) return;
		// Clear its drag flags too, so the next drag starts cleanly.
		canvas->endDrag(BUTTON_LEFT);
		canvas->endDrag(BUTTON_MIDDLE);
		canvas->endDrag(BUTTON_RIGHT);
	}
	while (wxWindow* w = wxWindow::GetCapture()) {
		w->ReleaseMouse();
		if (wxWindow::GetCapture() == w) break;   // don't spin if it won't let go
	}
}
