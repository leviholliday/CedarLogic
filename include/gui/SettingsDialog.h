#ifndef SETTINGSDIALOG_H_
#define SETTINGSDIALOG_H_

#include "MainApp.h"
#include "Settings.h"
#include <wx/dialog.h>
#include <wx/checkbox.h>
#include <wx/spinctrl.h>
#include <wx/choice.h>
#include <wx/textctrl.h>

class SettingsDialog : public wxDialog {
public:
	SettingsDialog(wxWindow* parent);

	bool getWireConnVisible() const;
	double getWireConnRadius() const;
	bool getGridlineVisible() const;
	bool getRightClickRotate() const;
	int getRefreshRate() const;
	int getAutosaveSeconds() const;   // 0 = off

	ThemeMode getThemeMode() const;
	bool getThemeShortcutEnabled() const;
	int getThemeShortcutKeyCode() const;
	int getThemeShortcutModifiers() const;
	bool getShowThemeToggleButton() const;

private:
	wxCheckBox* wireConnVisibleCtrl;
	wxSpinCtrlDouble* wireConnRadiusCtrl;
	wxCheckBox* gridlineVisibleCtrl;
	wxCheckBox* rightClickRotateCtrl;
	wxSpinCtrl* refreshRateCtrl;
	wxSpinCtrl* autosaveMinutesCtrl;

	wxChoice* themeModeCtrl;
	wxCheckBox* themeShortcutEnabledCtrl;
	// A read-only text field, not a button: on macOS a plain wxButton doesn't
	// reliably become the key (first-responder) window on a mouse click, which
	// left keystrokes with nowhere reliable to land. A text control is built to
	// take keyboard focus on click, so capture "just works" the way typing into
	// any other field does -- it only differs in that its EVT_KEY_DOWN handler
	// records the combo instead of inserting characters.
	wxTextCtrl* themeShortcutCaptureCtrl;
	wxCheckBox* showThemeToggleButtonCtrl;

	// The shortcut pending in the dialog (starts as the saved one; overwritten
	// only if the capture field records a new combo before OK is pressed).
	int pendingShortcutKeyCode;
	int pendingShortcutModifiers;
	bool capturingShortcut = false;
	void OnCaptureShortcutSetFocus(wxFocusEvent& event);
	void OnCaptureShortcutKillFocus(wxFocusEvent& event);
	void OnCaptureShortcutKeyDown(wxKeyEvent& event);
	void UpdateCaptureButtonLabel(bool listening);
};

#endif
