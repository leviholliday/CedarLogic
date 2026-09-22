#include "SettingsDialog.h"
#include "Settings.h"
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/statline.h>

DECLARE_APP(MainApp)

SettingsDialog::SettingsDialog(wxWindow* parent)
	: wxDialog(parent, wxID_ANY, "Preferences", wxDefaultPosition, wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE) {

	auto& settings = appConfig().appSettings;
	pendingShortcutKeyCode = settings.themeShortcutKeyCode;
	pendingShortcutModifiers = settings.themeShortcutModifiers;

	wxFlexGridSizer* grid = new wxFlexGridSizer(10, 2, 8, 12);
	grid->AddGrowableCol(1, 1);

	grid->Add(new wxStaticText(this, wxID_ANY, "Wire Connection Points"), 0, wxALIGN_CENTER_VERTICAL);
	wireConnVisibleCtrl = new wxCheckBox(this, wxID_ANY, "");
	wireConnVisibleCtrl->SetValue(settings.wireConnVisible);
	grid->Add(wireConnVisibleCtrl, 0, wxALIGN_CENTER_VERTICAL);

	grid->Add(new wxStaticText(this, wxID_ANY, "Wire Connection Radius"), 0, wxALIGN_CENTER_VERTICAL);
	wireConnRadiusCtrl = new wxSpinCtrlDouble(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0.05, 1.0, settings.wireConnRadius, 0.01);
	grid->Add(wireConnRadiusCtrl, 0, wxEXPAND);

	grid->Add(new wxStaticText(this, wxID_ANY, "Display Gridlines"), 0, wxALIGN_CENTER_VERTICAL);
	gridlineVisibleCtrl = new wxCheckBox(this, wxID_ANY, "");
	gridlineVisibleCtrl->SetValue(settings.gridlineVisible);
	grid->Add(gridlineVisibleCtrl, 0, wxALIGN_CENTER_VERTICAL);

	grid->Add(new wxStaticText(this, wxID_ANY, "Right-Click Rotates Gates"), 0, wxALIGN_CENTER_VERTICAL);
	rightClickRotateCtrl = new wxCheckBox(this, wxID_ANY, "");
	rightClickRotateCtrl->SetValue(settings.rightClickRotate);
	grid->Add(rightClickRotateCtrl, 0, wxALIGN_CENTER_VERTICAL);

	grid->Add(new wxStaticText(this, wxID_ANY, "Refresh Rate (FPS)"), 0, wxALIGN_CENTER_VERTICAL);
	int currentFps = (settings.refreshRate > 0) ? 1000 / settings.refreshRate : 60;
	refreshRateCtrl = new wxSpinCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 10, 1000, currentFps);
	grid->Add(refreshRateCtrl, 0, wxEXPAND);

	// Minutes, because that is how someone thinks about how much work they are
	// willing to lose. Zero turns it off, for anyone editing on a slow share who
	// would rather not have the app touch the disk on a timer.
	grid->Add(new wxStaticText(this, wxID_ANY, "Autosave Every (minutes, 0 = off)"), 0, wxALIGN_CENTER_VERTICAL);
	int currentMinutes = (settings.autosaveSeconds + 59) / 60;   // round up; 0 stays 0
	autosaveMinutesCtrl = new wxSpinCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 60, currentMinutes);
	grid->Add(autosaveMinutesCtrl, 0, wxEXPAND);

	// Theme: launch behavior, the shortcut, and whether the on-canvas switch
	// shows at all -- so the toggle can be reached three ways (menu, canvas
	// switch, shortcut) but doesn't have to be looked at while working.
	grid->Add(new wxStaticText(this, wxID_ANY, "On Launch"), 0, wxALIGN_CENTER_VERTICAL);
	themeModeCtrl = new wxChoice(this, wxID_ANY);
	// Order matches ThemeMode's numeric values exactly -- GetSelection() is fed
	// straight back as the enum in getThemeMode().
	themeModeCtrl->Append("Follow System Appearance");
	themeModeCtrl->Append("Always Light");
	themeModeCtrl->Append("Always Dark");
	themeModeCtrl->Append("Same as When Last Closed");
	themeModeCtrl->SetSelection(settings.themeMode);
	grid->Add(themeModeCtrl, 0, wxEXPAND);

	grid->Add(new wxStaticText(this, wxID_ANY, "Enable Dark Mode Shortcut"), 0, wxALIGN_CENTER_VERTICAL);
	themeShortcutEnabledCtrl = new wxCheckBox(this, wxID_ANY, "");
	themeShortcutEnabledCtrl->SetValue(settings.themeShortcutEnabled);
	grid->Add(themeShortcutEnabledCtrl, 0, wxALIGN_CENTER_VERTICAL);

	grid->Add(new wxStaticText(this, wxID_ANY, "Dark Mode Shortcut"), 0, wxALIGN_CENTER_VERTICAL);
	// Deliberately NOT wxTE_READONLY: on macOS a non-editable NSTextField
	// doesn't reliably take first-responder (keyboard focus) on click either --
	// the same problem a plain button had. An ordinary editable field is
	// guaranteed to focus on click; EVT_CHAR is vetoed below so no keystroke
	// actually lands in it as text.
	themeShortcutCaptureCtrl = new wxTextCtrl(this, wxID_ANY,
		formatThemeShortcut(pendingShortcutModifiers, pendingShortcutKeyCode),
		wxDefaultPosition, wxDefaultSize, wxTE_CENTRE | wxTE_PROCESS_TAB);
	themeShortcutCaptureCtrl->SetToolTip("Click, then press the new key combination");
	themeShortcutCaptureCtrl->Bind(wxEVT_SET_FOCUS, &SettingsDialog::OnCaptureShortcutSetFocus, this);
	themeShortcutCaptureCtrl->Bind(wxEVT_KILL_FOCUS, &SettingsDialog::OnCaptureShortcutKillFocus, this);
	themeShortcutCaptureCtrl->Bind(wxEVT_KEY_DOWN, &SettingsDialog::OnCaptureShortcutKeyDown, this);
	// Consume every character event so nothing actually gets typed into the
	// field -- OnCaptureShortcutKeyDown is the sole source of truth for what
	// was pressed.
	themeShortcutCaptureCtrl->Bind(wxEVT_CHAR, [](wxKeyEvent&) {});
	grid->Add(themeShortcutCaptureCtrl, 0, wxEXPAND);

	grid->Add(new wxStaticText(this, wxID_ANY, "Show Dark Mode Switch on Toolbar"), 0, wxALIGN_CENTER_VERTICAL);
	showThemeToggleButtonCtrl = new wxCheckBox(this, wxID_ANY, "");
	showThemeToggleButtonCtrl->SetValue(settings.showThemeToggleButton);
	grid->Add(showThemeToggleButtonCtrl, 0, wxALIGN_CENTER_VERTICAL);

	wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);
	topSizer->Add(grid, 1, wxALL | wxEXPAND, 16);
	topSizer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxALL | wxEXPAND, 8);

	SetSizerAndFit(topSizer);
}

void SettingsDialog::OnCaptureShortcutSetFocus(wxFocusEvent& event) {
	capturingShortcut = true;
	UpdateCaptureButtonLabel(true);
	event.Skip();
}

void SettingsDialog::OnCaptureShortcutKillFocus(wxFocusEvent& event) {
	// Clicking away (OK/Cancel, another field) without finishing a capture --
	// fall back to whatever was last actually recorded rather than leaving
	// "Press keys..." stuck on screen.
	capturingShortcut = false;
	UpdateCaptureButtonLabel(false);
	event.Skip();
}

void SettingsDialog::OnCaptureShortcutKeyDown(wxKeyEvent& event) {
	if (!capturingShortcut) { event.Skip(); return; }
	const int k = event.GetKeyCode();
	if (k == WXK_ESCAPE) {
		capturingShortcut = false;
		UpdateCaptureButtonLabel(false);
		// Move focus off the field so a second Escape (e.g. to close the whole
		// dialog) isn't swallowed here instead.
		themeShortcutCaptureCtrl->Navigate();
		return;
	}
	// A bare modifier isn't a combo yet -- keep listening for the key it's held
	// down for.
	if (k == WXK_SHIFT || k == WXK_ALT || k == WXK_CONTROL || k == WXK_COMMAND ||
	    k == WXK_RAW_CONTROL || k == WXK_WINDOWS_LEFT || k == WXK_WINDOWS_RIGHT) {
		return;
	}
	const int mods = themeModsFromKeyEvent(event);
	// Require at least one modifier: a bare letter would fight with typing
	// everywhere else the key is used, so keep listening instead of accepting it.
	if (mods == 0) return;

	pendingShortcutKeyCode = k;
	pendingShortcutModifiers = mods;
	UpdateCaptureButtonLabel(false);
}

void SettingsDialog::UpdateCaptureButtonLabel(bool listening) {
	themeShortcutCaptureCtrl->ChangeValue(listening
		? "Press keys... (Esc to cancel)"
		: formatThemeShortcut(pendingShortcutModifiers, pendingShortcutKeyCode));
}

bool SettingsDialog::getWireConnVisible() const { return wireConnVisibleCtrl->GetValue(); }
double SettingsDialog::getWireConnRadius() const { return wireConnRadiusCtrl->GetValue(); }
bool SettingsDialog::getGridlineVisible() const { return gridlineVisibleCtrl->GetValue(); }
bool SettingsDialog::getRightClickRotate() const { return rightClickRotateCtrl->GetValue(); }
int SettingsDialog::getAutosaveSeconds() const {
	return autosaveMinutesCtrl->GetValue() * 60;
}

int SettingsDialog::getRefreshRate() const {
	int fps = refreshRateCtrl->GetValue();
	return (fps > 0) ? 1000 / fps : 16;
}

ThemeMode SettingsDialog::getThemeMode() const { return (ThemeMode)themeModeCtrl->GetSelection(); }
bool SettingsDialog::getThemeShortcutEnabled() const { return themeShortcutEnabledCtrl->GetValue(); }
int SettingsDialog::getThemeShortcutKeyCode() const { return pendingShortcutKeyCode; }
int SettingsDialog::getThemeShortcutModifiers() const { return pendingShortcutModifiers; }
bool SettingsDialog::getShowThemeToggleButton() const { return showThemeToggleButtonCtrl->GetValue(); }
