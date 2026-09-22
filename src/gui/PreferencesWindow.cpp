/*****************************************************************************
   Project: CEDAR Logic Simulator
   PreferencesWindow: the app's settings, as a native preferences window.
*****************************************************************************/

#include "PreferencesWindow.h"
#include "MainApp.h"
#include "MainFrame.h"
#include "Settings.h"
#ifdef __APPLE__
#include "NativeIcons.h"
#endif

#include <wx/preferences.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/spinctrl.h>
#include <wx/textctrl.h>
#include <wx/settings.h>
#include <memory>

DECLARE_APP(MainApp)

namespace {

// Shared layout and apply plumbing: a two-column grid (right-aligned label,
// control) with a line of gray help text under each control, like the
// settings panes in macOS apps.
class PrefsPanel : public wxPanel {
public:
	explicit PrefsPanel(wxWindow* parent) : wxPanel(parent) {
		grid = new wxFlexGridSizer(2, 6, 12);
		grid->AddGrowableCol(1, 1);
		wxBoxSizer* outer = new wxBoxSizer(wxVERTICAL);
		outer->Add(grid, 1, wxALL | wxEXPAND, 20);
		SetSizer(outer);
	}

	// Windows/Linux show OK/Cancel: OK lands here.
	bool TransferDataFromWindow() override { apply(); return true; }

protected:
	// Write this page's controls into appSettings and push them live.
	virtual void apply() = 0;

	void changed() {
		if (wxPreferencesEditor::ShouldApplyChangesImmediately()) apply();
	}

	void pushLive() {
		if (wxGetApp().mainframe) wxGetApp().mainframe->ApplyPreferences();
	}

	void addRow(const wxString& label, wxWindow* ctrl, const wxString& help) {
		grid->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL);
		grid->Add(ctrl, 0, wxALIGN_CENTER_VERTICAL);
		addHelp(help);
	}

	wxCheckBox* addCheck(const wxString& label, const wxString& caption, bool value, const wxString& help) {
		wxCheckBox* cb = new wxCheckBox(this, wxID_ANY, caption);
		cb->SetValue(value);
		cb->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { changed(); });
		addRow(label, cb, help);
		return cb;
	}

	void addHelp(const wxString& help) {
		if (help.empty()) return;
		grid->AddSpacer(0);
		wxStaticText* t = new wxStaticText(this, wxID_ANY, help);
		wxFont f = t->GetFont();
		f.SetPointSize(f.GetPointSize() - 2);
		t->SetFont(f);
		t->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
		t->Wrap(340);
		grid->Add(t, 0, wxBOTTOM, 6);
	}

	wxFlexGridSizer* grid;
};

// ---- General ---------------------------------------------------------------

class GeneralPanel : public PrefsPanel {
public:
	explicit GeneralPanel(wxWindow* parent) : PrefsPanel(parent) {
		auto& s = appConfig().appSettings;

		autosave = new wxSpinCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(80, -1),
			wxSP_ARROW_KEYS, 0, 60, (s.autosaveSeconds + 59) / 60);
		autosave->Bind(wxEVT_SPINCTRL, [this](wxCommandEvent&) { changed(); });
		addRow("Autosave every:", withUnit(autosave, "minutes"),
			"How often a backup of your open circuit is saved. 0 turns it off.");

		int fps = (s.refreshRate > 0) ? 1000 / s.refreshRate : 60;
		refresh = new wxSpinCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(80, -1),
			wxSP_ARROW_KEYS, 10, 1000, fps);
		refresh->Bind(wxEVT_SPINCTRL, [this](wxCommandEvent&) { changed(); });
		addRow("Refresh rate:", withUnit(refresh, "frames per second"),
			"How often the canvas redraws while the simulation runs.");

		statusInfo = addCheck("Status bar:", "Show zoom, cursor position, and counts", s.showStatusInfo,
			"The readout in the bottom-right corner of the window.");

		GetSizer()->SetSizeHints(this);
	}

protected:
	void apply() override {
		auto& s = appConfig().appSettings;
		s.showStatusInfo = statusInfo->GetValue();
		s.autosaveSeconds = autosave->GetValue() * 60;
		int fps = refresh->GetValue();
		s.refreshRate = (fps > 0) ? 1000 / fps : 16;
		pushLive();
	}

private:
	wxWindow* withUnit(wxWindow* ctrl, const wxString& unit) {
		// Put the control and its unit label side by side in one grid cell.
		wxPanel* box = new wxPanel(this);
		ctrl->Reparent(box);
		wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
		row->Add(ctrl, 0, wxALIGN_CENTER_VERTICAL);
		row->Add(new wxStaticText(box, wxID_ANY, unit), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
		box->SetSizer(row);
		return box;
	}

	wxSpinCtrl* autosave;
	wxSpinCtrl* refresh;
	wxCheckBox* statusInfo;
};

// ---- Appearance ------------------------------------------------------------

class AppearancePanel : public PrefsPanel {
public:
	explicit AppearancePanel(wxWindow* parent) : PrefsPanel(parent) {
		auto& s = appConfig().appSettings;

		themeMode = new wxChoice(this, wxID_ANY);
		// Order matches ThemeMode's numeric values.
		themeMode->Append("Match System");
		themeMode->Append("Light");
		themeMode->Append("Dark");
		themeMode->Append("Same as Last Time");
		themeMode->SetSelection(s.themeMode);
		themeMode->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { changed(); });
		addRow("Theme at launch:", themeMode,
			"Which theme the app opens in. You can still switch any time from the toolbar or View menu.");

		showToggle = addCheck("Toolbar:", "Show the dark mode switch", s.showThemeToggleButton,
			"Hide it if you only switch themes with the shortcut or the View menu.");

		accent = new wxChoice(this, wxID_ANY);
		// Order matches RenderStyle::accent()'s table.
		for (const char* name : {"Blue", "Purple", "Pink", "Orange", "Green", "Graphite"}) accent->Append(name);
		accent->SetSelection(s.accentColor);
		accent->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { changed(); });
		addRow("Accent color:", accent,
			"Used for selections and highlights. Wire colors that show signal state never change.");

		grid_ = addCheck("Canvas:", "Show the grid", s.gridlineVisible,
			"The background grid gates snap to. Printing never includes it.");

		gridStyle = new wxChoice(this, wxID_ANY);
		gridStyle->Append("Lines");
		gridStyle->Append("Dots");
		gridStyle->SetSelection(s.gridStyle);
		gridStyle->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { changed(); });
		addRow("Grid style:", gridStyle, "Dots are quieter; lines make alignment easier to see.");

		majorGrid = addCheck("", "Darker line every 5 squares", s.majorGridVisible,
			"Makes distances easy to judge at a glance. Off: every grid line looks the same.");

		wireThickness = new wxChoice(this, wxID_ANY);
		wireThickness->Append("Thin");
		wireThickness->Append("Normal");
		wireThickness->Append("Thick");
		wireThickness->SetSelection(s.wireThickness);
		wireThickness->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { changed(); });
		addRow("Wire thickness:", wireThickness, "On screen only. Printouts always use the standard weight.");

		wireConn = addCheck("", "Show dots at wire bends", s.wireConnVisible,
			"Marks every corner of a wire. Junctions where wires join always get a dot.");

		wireRadius = new wxSpinCtrlDouble(this, wxID_ANY, "", wxDefaultPosition, wxSize(80, -1),
			wxSP_ARROW_KEYS, 0.05, 1.0, s.wireConnRadius, 0.01);
		wireRadius->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent&) { changed(); });
		addRow("Wire dot size:", wireRadius, "Radius of the dots on wires, in grid units.");

		GetSizer()->SetSizeHints(this);
	}

protected:
	void apply() override {
		auto& s = appConfig().appSettings;
		s.themeMode = themeMode->GetSelection();
		s.showThemeToggleButton = showToggle->GetValue();
		s.gridlineVisible = grid_->GetValue();
		s.majorGridVisible = majorGrid->GetValue();
		s.accentColor = accent->GetSelection();
		s.gridStyle = gridStyle->GetSelection();
		s.wireThickness = wireThickness->GetSelection();
		s.wireConnVisible = wireConn->GetValue();
		s.wireConnRadius = (float)wireRadius->GetValue();
		pushLive();
	}

private:
	wxChoice* themeMode;
	wxCheckBox* showToggle;
	wxCheckBox* grid_;
	wxCheckBox* majorGrid;
	wxChoice* accent;
	wxChoice* gridStyle;
	wxChoice* wireThickness;
	wxCheckBox* wireConn;
	wxSpinCtrlDouble* wireRadius;
};

// ---- Canvas ----------------------------------------------------------------

class CanvasPanel : public PrefsPanel {
public:
	explicit CanvasPanel(wxWindow* parent) : PrefsPanel(parent) {
		auto& s = appConfig().appSettings;
		rightClickRotate = addCheck("Right-click:", "Rotates the gate", s.rightClickRotate,
			"Off: right-clicking a gate opens a menu with Rotate and Delete instead.");
		GetSizer()->SetSizeHints(this);
	}

protected:
	void apply() override {
		appConfig().appSettings.rightClickRotate = rightClickRotate->GetValue();
		pushLive();
	}

private:
	wxCheckBox* rightClickRotate;
};

// ---- Shortcuts -------------------------------------------------------------

class ShortcutsPanel : public PrefsPanel {
public:
	explicit ShortcutsPanel(wxWindow* parent) : PrefsPanel(parent) {
		auto& s = appConfig().appSettings;
		keyCode = s.themeShortcutKeyCode;
		mods = s.themeShortcutModifiers;

		enabled = addCheck("Dark mode:", "Use a keyboard shortcut", s.themeShortcutEnabled,
			"Switch between light and dark without leaving the keyboard.");

		// An editable text field, not a button or read-only field: on macOS
		// neither of those reliably takes keyboard focus on click, so capture
		// silently did nothing. Typed characters are vetoed below; the key-down
		// handler records the combo instead.
		capture = new wxTextCtrl(this, wxID_ANY, formatThemeShortcut(mods, keyCode),
			wxDefaultPosition, wxSize(180, -1), wxTE_CENTRE | wxTE_PROCESS_TAB);
		capture->SetToolTip("Click, then press the new key combination");
		capture->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& e) { listening = true; showLabel(); e.Skip(); });
		capture->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) { listening = false; showLabel(); e.Skip(); });
		capture->Bind(wxEVT_KEY_DOWN, &ShortcutsPanel::OnCaptureKey, this);
		capture->Bind(wxEVT_CHAR, [](wxKeyEvent&) {});
		addRow("Shortcut:", capture,
			"Click the field, then press the keys you want. Needs at least one modifier (Cmd, Shift, Option, Control).");

		GetSizer()->SetSizeHints(this);
	}

protected:
	void apply() override {
		auto& s = appConfig().appSettings;
		s.themeShortcutEnabled = enabled->GetValue();
		s.themeShortcutKeyCode = keyCode;
		s.themeShortcutModifiers = mods;
		pushLive();
	}

private:
	void OnCaptureKey(wxKeyEvent& event) {
		if (!listening) { event.Skip(); return; }
		const int k = event.GetKeyCode();
		if (k == WXK_ESCAPE) {
			listening = false;
			showLabel();
			capture->Navigate();   // move focus off so a second Escape isn't swallowed here
			return;
		}
		// A bare modifier isn't a combo yet -- keep listening.
		if (k == WXK_SHIFT || k == WXK_ALT || k == WXK_CONTROL || k == WXK_COMMAND ||
		    k == WXK_RAW_CONTROL || k == WXK_WINDOWS_LEFT || k == WXK_WINDOWS_RIGHT) {
			return;
		}
		const int m = themeModsFromKeyEvent(event);
		if (m == 0) return;   // a bare letter would fight with typing elsewhere
		keyCode = k;
		mods = m;
		listening = false;
		showLabel();
		changed();
	}

	void showLabel() {
		capture->ChangeValue(listening ? "Press keys... (Esc to cancel)" : formatThemeShortcut(mods, keyCode));
	}

	wxCheckBox* enabled;
	wxTextCtrl* capture;
	int keyCode, mods;
	bool listening = false;
};

// ---- Pages -----------------------------------------------------------------

template <class Panel>
class Page : public wxPreferencesPage {
public:
	Page(const wxString& name, const char* sfSymbol) : name(name), sfSymbol(sfSymbol) {}
	wxString GetName() const override { return name; }
	wxBitmapBundle GetIcon() const override {
#ifdef __APPLE__
		wxBitmap bmp = NativeIcon_GetSFSymbol(sfSymbol, 20);
		if (bmp.IsOk()) return wxBitmapBundle(bmp);
#endif
		return wxBitmapBundle();
	}
	wxWindow* CreateWindow(wxWindow* parent) override { return new Panel(parent); }

private:
	wxString name;
	const char* sfSymbol;
};

std::unique_ptr<wxPreferencesEditor> g_editor;

} // namespace

void ShowPreferencesWindow(wxWindow* parent) {
	if (!g_editor) {
		g_editor.reset(new wxPreferencesEditor());
		g_editor->AddPage(new Page<GeneralPanel>("General", "gearshape"));
		g_editor->AddPage(new Page<AppearancePanel>("Appearance", "paintpalette"));
		g_editor->AddPage(new Page<CanvasPanel>("Canvas", "cursorarrow.rays"));
		g_editor->AddPage(new Page<ShortcutsPanel>("Shortcuts", "keyboard"));
	}
	g_editor->Show(parent);
}

void DismissPreferencesWindow() {
	if (g_editor) {
		g_editor->Dismiss();
		g_editor.reset();
	}
}
