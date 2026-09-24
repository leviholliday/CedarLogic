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
#include <wx/slider.h>
#include <wx/radiobut.h>
#include <wx/statbmp.h>
#include "ModernToolbar.h"
#include "RenderMode.h"
#include "UiKit.h"
#include "UiControls.h"
#include <wx/settings.h>
#include <wx/frame.h>
#include <wx/scrolwin.h>
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <functional>
#include <wx/weakref.h>
#include <memory>
#ifdef __WXMSW__
#include "WinAppearance.h"
#endif

DECLARE_APP(MainApp)

namespace {

// Shared layout and apply plumbing: a two-column grid (right-aligned label,
// control) with a line of gray help text under each control, like the
// settings panes in macOS apps.
// True while the Shortcuts page is waiting for a key combination, so Escape
// cancels that instead of closing the window out from under it.
bool g_capturingShortcut = false;

// On Windows each setting is a card with a switch, Windows 11 style; the
// stock checkbox there looks like Windows 7 and ignores dark mode. Both have
// GetValue() and send wxEVT_CHECKBOX, so the pages below don't care which.
#ifdef __WXMSW__
using PrefCheck = ui::ToggleSwitch;
#else
using PrefCheck = wxCheckBox;
#endif

class PrefsPanel : public wxPanel {
public:
	explicit PrefsPanel(wxWindow* parent) : wxPanel(parent) {
#ifdef __WXMSW__
		SetBackgroundColour(ui::pageColour());
		column = new wxBoxSizer(wxVERTICAL);
		SetSizer(column);
#else
		grid = new wxFlexGridSizer(2, 6, 12);
		grid->AddGrowableCol(1, 1);
		wxBoxSizer* outer = new wxBoxSizer(wxVERTICAL);
		outer->Add(grid, 1, wxALL | wxEXPAND, 20);
		SetSizer(outer);
#endif
		// Escape closes this window, the way it closes every other one -- but
		// only once it has nothing nearer to back out of first: a shortcut
		// being recorded, or a field being typed in.
		Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
			if (e.GetKeyCode() != WXK_ESCAPE) { e.Skip(); return; }
			if (g_capturingShortcut) { e.Skip(); return; }
			wxWindow* focus = wxWindow::FindFocus();
			if (dynamic_cast<wxTextCtrl*>(focus) && focus->IsDescendant(this)) {
				focus->Navigate();   // step out of the field; a second Escape closes
				return;
			}
			wxTheApp->CallAfter([] { DismissPreferencesWindow(); });
		});
	}

	// Windows/Linux show OK/Cancel: OK lands here.
	bool TransferDataFromWindow() override { apply(); return true; }

protected:
	// Write this page's controls into appSettings and push them live.
	virtual void apply() = 0;

	// Every platform applies as you go. wx says Windows and Linux should wait
	// for OK, but that meant OK, look, reopen, adjust -- over and over.
	void changed() { apply(); }

	void pushLive() {
		if (wxGetApp().mainframe) wxGetApp().mainframe->ApplyPreferences();
	}

	void addRow(const wxString& label, wxWindow* ctrl, const wxString& help) {
#ifdef __WXMSW__
		wxString title = label;
		if (title.EndsWith(":")) title.RemoveLast();
		addCard(title, help, ctrl);
#else
		grid->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL);
		grid->Add(ctrl, 0, wxALIGN_CENTER_VERTICAL);
		addHelp(help);
#endif
	}

	PrefCheck* addCheck(const wxString& label, const wxString& caption, bool value, const wxString& help) {
#ifdef __WXMSW__
		// The card's own title says what it does, so the grid's group label
		// ("Canvas:") has nothing left to do here.
		(void)label;
		PrefCheck* cb = new PrefCheck(this, value);
		cb->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { changed(); });
		addCard(caption, help, cb);
#else
		wxCheckBox* cb = new wxCheckBox(this, wxID_ANY, caption);
		cb->SetValue(value);
		cb->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { changed(); });
		addRow(label, cb, help);
#endif
		return cb;
	}

	void addHelp(const wxString& help) {
		if (help.empty()) return;
#ifdef __WXMSW__
		// A note under the cards, not tied to any one of them.
		wxStaticText* note = new wxStaticText(this, wxID_ANY, help);
		note->SetFont(wxFont(wxFontInfo(9)));
		note->SetForegroundColour(ui::dim());
		note->Wrap(FromDIP(560));
		column->Add(note, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, FromDIP(6));
		return;
#endif
		grid->AddSpacer(0);
		wxStaticText* t = new wxStaticText(this, wxID_ANY, help);
		wxFont f = t->GetFont();
		f.SetPointSize(f.GetPointSize() - 2);
		t->SetFont(f);
		t->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
		t->Wrap(340);
		grid->Add(t, 0, wxBOTTOM, 6);
	}

#ifdef __WXMSW__
	// One setting: what it is and what it does on the left, its control on
	// the right, on a rounded card.
	void addCard(const wxString& title, const wxString& help, wxWindow* ctrl) {
		ui::Card* card = new ui::Card(this);
		wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
		wxBoxSizer* texts = new wxBoxSizer(wxVERTICAL);
		wxStaticText* t = new wxStaticText(card, wxID_ANY, title);
		t->SetFont(wxFont(wxFontInfo(10.5)));
		t->SetForegroundColour(ui::ink());
		texts->Add(t);
		if (!help.empty()) {
			wxStaticText* h = new wxStaticText(card, wxID_ANY, help);
			h->SetFont(wxFont(wxFontInfo(9)));
			h->SetForegroundColour(ui::dim());
			h->Wrap(FromDIP(360));
			texts->Add(h, 0, wxTOP, FromDIP(2));
		}
		row->Add(texts, 1, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(14));
		ctrl->Reparent(card);
		onCard(ctrl);
		row->Add(ctrl, 0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM | wxRIGHT, FromDIP(14));
		card->SetSizer(row);
		column->Add(card, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
	}

	// A control that is a little panel of its own (a field and its unit)
	// takes the card's colour, and so does its text.
	static void onCard(wxWindow* w) {
		if (wxDynamicCast(w, wxPanel) && !wxDynamicCast(w, ui::Card)) {
			w->SetBackgroundColour(ui::cardColour());
			for (wxWindowList::compatibility_iterator n = w->GetChildren().GetFirst(); n; n = n->GetNext())
				if (wxDynamicCast(n->GetData(), wxStaticText)) n->GetData()->SetForegroundColour(ui::dim());
		}
	}

	wxBoxSizer* column = nullptr;
#endif
	wxFlexGridSizer* grid = nullptr;
};

// ---- General ---------------------------------------------------------------

class GeneralPanel : public PrefsPanel {
public:
	explicit GeneralPanel(wxWindow* parent) : PrefsPanel(parent) {
		auto& s = appConfig().appSettings;

		int fps = (s.refreshRate > 0) ? 1000 / s.refreshRate : 60;
		refresh = new wxSpinCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(80, -1),
			wxSP_ARROW_KEYS, 10, 1000, fps);
		refresh->Bind(wxEVT_SPINCTRL, [this](wxCommandEvent&) { changed(); });
		addRow("Refresh rate:", withUnit(refresh, "frames per second"),
			"How often the canvas redraws while the simulation runs.");

		name = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(s.studentName.c_str()),
			wxDefaultPosition, wxSize(220, -1));
		name->SetHint("First and last name");
		name->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { changed(); });
		addRow("Your name:", name, "Printed under your circuit when you export it (File > Export as Image).");

		statusInfo = addCheck("Status bar:", "Show zoom, cursor position, and counts", s.showStatusInfo,
			"The readout in the bottom-right corner of the window.");

		GetSizer()->SetSizeHints(this);
	}

protected:
	void apply() override {
		auto& s = appConfig().appSettings;
		s.showStatusInfo = statusInfo->GetValue();
		s.studentName = std::string(name->GetValue().Strip(wxString::both).ToUTF8());
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

	wxSpinCtrl* refresh;
	PrefCheck* statusInfo;
	wxTextCtrl* name;
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

		tabBar = new wxChoice(this, wxID_ANY);
		tabBar->Append("Modern");          // index 0 == classicTabs false
		tabBar->Append("Classic");
		tabBar->SetSelection(s.classicTabs ? 1 : 0);
		tabBar->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { changed(); });
		addRow("Tabs:", tabBar,
			"Modern tabs can be dragged to reorder, renamed by double-clicking, and dragged "
			"aside to open a split view. Classic uses the plain system tabs, which do none of that.");

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

		gateSize = new wxSlider(this, wxID_ANY, s.paletteGateSize, 32, 110,
			wxDefaultPosition, wxSize(220, -1));
		gateSize->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) { changed(); });
		addRow("Gate size:", gateSize,
			"How big the gates in the side panel are. Drag the divider next to the panel to change its width.");

		GetSizer()->SetSizeHints(this);
	}

protected:
	void apply() override {
		auto& s = appConfig().appSettings;
		s.themeMode = themeMode->GetSelection();
		s.classicTabs = (tabBar->GetSelection() == 1);
		s.gridlineVisible = grid_->GetValue();
		s.majorGridVisible = majorGrid->GetValue();
		s.accentColor = accent->GetSelection();
		s.gridStyle = gridStyle->GetSelection();
		s.wireThickness = wireThickness->GetSelection();
		s.wireConnVisible = wireConn->GetValue();
		s.wireConnRadius = (float)wireRadius->GetValue();
		s.paletteGateSize = gateSize->GetValue();
		pushLive();
	}

private:
	wxChoice* themeMode;
	wxChoice* tabBar;
	PrefCheck* grid_;
	PrefCheck* majorGrid;
	wxChoice* accent;
	wxChoice* gridStyle;
	wxChoice* wireThickness;
	PrefCheck* wireConn;
	wxSpinCtrlDouble* wireRadius;
	wxSlider* gateSize;
};

// ---- Canvas ----------------------------------------------------------------

class CanvasPanel : public PrefsPanel {
public:
	explicit CanvasPanel(wxWindow* parent) : PrefsPanel(parent) {
		auto& s = appConfig().appSettings;
		mouseAction = actionChoice(s.mouseWheelAction);
		addRow("Mouse wheel:", mouseAction, "");
		reverseWheel = addCheck("", "Reverse zoom direction", s.reverseWheelZoom,
#ifdef __APPLE__
			"Flip this if rolling the wheel up zooms out. Apps like Scroll Reverser change the direction.");
#else
			"Flip this if rolling the wheel up zooms out.");
#endif

#ifdef __APPLE__
		trackpadAction = actionChoice(s.trackpadScrollAction);
		addRow("Trackpad scroll:", trackpadAction, "Pinching always zooms.");
		reverseTrackpad = addCheck("", "Reverse zoom direction", s.reverseTrackpadZoom,
			"Only matters when trackpad scrolling is set to zoom.");
#endif
		addHelp(ui::platformKeys("Cmd+scroll always zooms. Shift+scroll always moves sideways."));

		rightClickRotate = addCheck("Right-click:", "Rotates the gate", s.rightClickRotate,
			"Off: right-clicking a gate opens a menu with Rotate and Delete instead.");

		duplicate = new wxChoice(this, wxID_ANY);
		duplicate->Append("Leaves the clipboard alone");   // false
		duplicate->Append("Copies to the clipboard too");   // true
		duplicate->SetSelection(s.duplicateUsesClipboard ? 1 : 0);
		duplicate->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { changed(); });
		addRow(ui::platformKeys("Duplicate (Cmd+D):"), duplicate,
			ui::platformKeys("Second option: the copy stays on the clipboard, so Cmd+V pastes more of it."));
		GetSizer()->SetSizeHints(this);
	}

protected:
	void apply() override {
		appConfig().appSettings.rightClickRotate = rightClickRotate->GetValue();
		appConfig().appSettings.duplicateUsesClipboard = duplicate->GetSelection() == 1;
		auto& s = appConfig().appSettings;
		s.mouseWheelAction = mouseAction->GetSelection();
		s.reverseWheelZoom = reverseWheel->GetValue();
#ifdef __APPLE__
		s.trackpadScrollAction = trackpadAction->GetSelection();
		s.reverseTrackpadZoom = reverseTrackpad->GetValue();
#endif
		pushLive();
	}

private:
	PrefCheck* rightClickRotate;
	wxChoice* actionChoice(int value) {
		wxChoice* c = new wxChoice(this, wxID_ANY);
		c->Append("Zooms");   // order matches the settings: 0 = zoom, 1 = move
		c->Append("Moves around");
		c->SetSelection(value);
		c->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { changed(); });
		return c;
	}

	wxChoice* duplicate;
	wxChoice* mouseAction;
	PrefCheck* reverseWheel;
	wxChoice* trackpadAction = nullptr;
	PrefCheck* reverseTrackpad = nullptr;
};

// ---- Toolbar ---------------------------------------------------------------

#ifdef __WXMSW__
// One toolbar style to pick: its name, a line about it and a picture of it,
// ringed in the accent when it is the one in use. The whole tile is the
// button -- the radio button beside it was the only way in before.
class StyleTile : public wxPanel {
public:
	StyleTile(wxWindow* parent, int style, std::function<void(int)> onPick)
		: wxPanel(parent), style(style), onPick(onPick) {
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		SetCursor(wxCursor(wxCURSOR_HAND));
		picture = ModernToolbar::RenderPreview(style, renderMode().darkMode, previewW(), 1.0);
		SetMinSize(wxSize(previewW() + 2 * pad(), FromDIP(58) + picture.GetHeight() + pad()));
		Bind(wxEVT_PAINT, &StyleTile::OnPaint, this);
		Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { if (this->onPick) this->onPick(this->style); });
		Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) { hot = true; Refresh(); });
		Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { hot = false; Refresh(); });
	}
	void Repicture() {
		picture = ModernToolbar::RenderPreview(style, renderMode().darkMode, previewW(), 1.0);
		Refresh();
	}

private:
	int previewW() const { return FromDIP(520); }
	int pad() const { return FromDIP(14); }

	void OnPaint(wxPaintEvent&) {
		wxAutoBufferedPaintDC dc(this);
		dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
		dc.Clear();
		std::unique_ptr<wxGraphicsContext> gc(ui::graphics(dc));
		if (!gc) return;
		const wxSize sz = GetClientSize();
		const bool chosen = appConfig().appSettings.toolbarStyle == style;
		const wxColour ac = ui::accent(), in = ui::ink();
		gc->SetBrush(wxBrush(hot && !chosen ? ui::withAlpha(in, ui::isDark() ? 0.06 : 0.035) : ui::cardColour()));
		gc->SetPen(chosen ? wxPen(ac, 2) : wxPen(ui::withAlpha(in, 0.08), 1));
		const double inset = chosen ? 1.0 : 0.5;
		gc->DrawRoundedRectangle(inset, inset, sz.x - 2 * inset, sz.y - 2 * inset, FromDIP(8));

		// A filled circle with a tick when chosen, an empty ring when not.
		const double cx = pad() + FromDIP(8), cy = pad() + FromDIP(9), r = FromDIP(8);
		if (chosen) {
			gc->SetPen(*wxTRANSPARENT_PEN);
			gc->SetBrush(wxBrush(ac));
			gc->DrawEllipse(cx - r, cy - r, 2 * r, 2 * r);
			gc->SetPen(wxPen(*wxWHITE, FromDIP(2)));
			gc->StrokeLine(cx - r * 0.45, cy, cx - r * 0.1, cy + r * 0.35);
			gc->StrokeLine(cx - r * 0.1, cy + r * 0.35, cx + r * 0.45, cy - r * 0.3);
		} else {
			gc->SetBrush(*wxTRANSPARENT_BRUSH);
			gc->SetPen(wxPen(ui::withAlpha(in, 0.45), 1));
			gc->DrawEllipse(cx - r + 0.5, cy - r + 0.5, 2 * r - 1, 2 * r - 1);
		}
		const double tx = cx + r + FromDIP(10);
		gc->SetFont(wxFont(wxFontInfo(10.5).Bold()), in);
		gc->DrawText(cl::tb::styleName(style), tx, pad());
		gc->SetFont(wxFont(wxFontInfo(9)), ui::dim());
		gc->DrawText(wxString::FromUTF8(cl::tb::styleBlurb(style)), tx, pad() + FromDIP(20));
		gc->DrawBitmap(picture, pad(), FromDIP(56), picture.GetWidth(), picture.GetHeight());
	}

	int style;
	std::function<void(int)> onPick;
	wxBitmap picture;
	bool hot = false;
};
#endif

// Pick a toolbar style from pictures of each (drawn by the toolbar's own
// code, so they always match), and choose which tools it shows.
class ToolbarPanel : public wxPanel {
public:
	explicit ToolbarPanel(wxWindow* parent) : wxPanel(parent) {
		auto& s = appConfig().appSettings;
		wxBoxSizer* outer = new wxBoxSizer(wxVERTICAL);
#ifdef __WXMSW__
		SetBackgroundColour(ui::pageColour());
		for (int st = 0; st < cl::tb::StyleCount; st++) {
			StyleTile* tile = new StyleTile(this, st, [this](int picked) {
				appConfig().appSettings.toolbarStyle = picked;
				for (StyleTile* t : tiles) t->Refresh();
				changed();
			});
			tiles.push_back(tile);
			outer->Add(tile, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
		}

		ui::Card* groups = new ui::Card(this);
		wxBoxSizer* inCard = new wxBoxSizer(wxVERTICAL);
		wxStaticText* head = new wxStaticText(groups, wxID_ANY, "Show in the toolbar");
		head->SetFont(wxFont(wxFontInfo(10.5)));
		head->SetForegroundColour(ui::ink());
		inCard->Add(head, 0, wxALL, FromDIP(14));
		wxFlexGridSizer* switches = new wxFlexGridSizer(3, FromDIP(10), FromDIP(28));
		for (int g = 0; g < cl::tb::GroupCount; g++) {
			wxBoxSizer* pair = new wxBoxSizer(wxHORIZONTAL);
			PrefCheck* sw = new PrefCheck(groups, !(s.toolbarHidden & (1 << g)));
			sw->Bind(wxEVT_CHECKBOX, [this, g](wxCommandEvent& e) {
				int& mask = appConfig().appSettings.toolbarHidden;
				mask = e.IsChecked() ? (mask & ~(1 << g)) : (mask | (1 << g));
				for (StyleTile* t : tiles) t->Repicture();   // the pictures show the tools you chose
				changed();
			});
			wxStaticText* name = new wxStaticText(groups, wxID_ANY, cl::tb::groupName(g));
			name->SetForegroundColour(ui::ink());
			pair->Add(sw, 0, wxALIGN_CENTER_VERTICAL);
			pair->Add(name, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
			switches->Add(pair);
		}
		inCard->Add(switches, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(14));
		groups->SetSizer(inCard);
		outer->Add(groups, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

		wxStaticText* note = new wxStaticText(this, wxID_ANY,
			"Applies to the custom styles. Hidden tools are still in the menu and keep their shortcuts.");
		note->SetFont(wxFont(wxFontInfo(9)));
		note->SetForegroundColour(ui::dim());
		outer->Add(note, 0, wxALL, FromDIP(6));
		SetSizerAndFit(outer);
		return;
#endif

		const double scale = GetContentScaleFactor();
		const int previewW = 520;
		for (int st = 0; st < cl::tb::StyleCount; st++) {
			wxRadioButton* rb = new wxRadioButton(this, wxID_ANY, cl::tb::styleName(st), wxDefaultPosition,
				wxDefaultSize, st == 0 ? wxRB_GROUP : 0);
			rb->SetValue(s.toolbarStyle == st);
			wxFont f = rb->GetFont();
			f.SetWeight(wxFONTWEIGHT_BOLD);
			rb->SetFont(f);
			rb->Bind(wxEVT_RADIOBUTTON, [this, st](wxCommandEvent&) { appConfig().appSettings.toolbarStyle = st; changed(); });
			outer->Add(rb, 0, wxLEFT | wxRIGHT | wxTOP, 16);

			wxStaticText* blurb = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(cl::tb::styleBlurb(st)));
			blurb->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
			outer->Add(blurb, 0, wxLEFT | wxRIGHT, 38);

			wxStaticBitmap* pic = new wxStaticBitmap(this, wxID_ANY,
				ModernToolbar::RenderPreview(st, renderMode().darkMode, previewW, scale));
			previews.push_back(pic);
			// Clicking the picture picks the style too.
			pic->Bind(wxEVT_LEFT_DOWN, [rb, st, this](wxMouseEvent&) {
				rb->SetValue(true);
				appConfig().appSettings.toolbarStyle = st;
				changed();
			});
			outer->Add(pic, 0, wxLEFT | wxRIGHT | wxTOP, 38 - 22);
			outer->AddSpacer(6);
		}

		wxStaticText* showLabel = new wxStaticText(this, wxID_ANY, "Show in the toolbar:");
		wxFont bf = showLabel->GetFont();
		bf.SetWeight(wxFONTWEIGHT_BOLD);
		showLabel->SetFont(bf);
		outer->Add(showLabel, 0, wxLEFT | wxRIGHT | wxTOP, 16);
		wxGridSizer* grid = new wxGridSizer(3, 6, 18);
		for (int g = 0; g < cl::tb::GroupCount; g++) {
			wxCheckBox* cb = new wxCheckBox(this, wxID_ANY, cl::tb::groupName(g));
			cb->SetValue(!(s.toolbarHidden & (1 << g)));
			cb->Bind(wxEVT_CHECKBOX, [this, g](wxCommandEvent& e) {
				int& mask = appConfig().appSettings.toolbarHidden;
				mask = e.IsChecked() ? (mask & ~(1 << g)) : (mask | (1 << g));
				refreshPreviews();   // the pictures show the tools you chose
				changed();
			});
			grid->Add(cb);
		}
		outer->Add(grid, 0, wxALL, 16);
		wxStaticText* note = new wxStaticText(this, wxID_ANY,
			"Applies to the custom styles. Hidden tools are still in the menus and keep their shortcuts.");
		note->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
		outer->Add(note, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16);
		SetSizerAndFit(outer);
	}
	bool TransferDataFromWindow() override { apply(); return true; }

private:
	// Redraw every style's picture: they are rendered by the toolbar's own
	// code, so they have to be rebuilt whenever what it would draw changes.
	void refreshPreviews() {
		const double scale = GetContentScaleFactor();
		for (size_t i = 0; i < previews.size(); i++)
			previews[i]->SetBitmap(ModernToolbar::RenderPreview((int)i, renderMode().darkMode, 520, scale));
		Refresh();
	}

	void changed() { apply(); }   // as you go, on every platform (see above)
	void apply() { if (wxGetApp().mainframe) wxGetApp().mainframe->ApplyPreferences(); }

	std::vector<wxStaticBitmap*> previews;
#ifdef __WXMSW__
	std::vector<StyleTile*> tiles;
#endif
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
		capture->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& e) {
			listening = g_capturingShortcut = true; showLabel(); e.Skip(); });
		capture->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) {
			listening = g_capturingShortcut = false; showLabel(); e.Skip(); });
		capture->Bind(wxEVT_KEY_DOWN, &ShortcutsPanel::OnCaptureKey, this);
		capture->Bind(wxEVT_CHAR, [](wxKeyEvent&) {});
		addRow("Shortcut:", capture,
#ifdef __WXOSX__
			"Click the field, then press the keys you want. Needs at least one modifier (Cmd, Shift, Option, Control).");
#else
			"Click the field, then press the keys you want. Needs at least one modifier (Ctrl, Shift, Alt, Win).");
#endif

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
			listening = g_capturingShortcut = false;
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
		listening = g_capturingShortcut = false;
		showLabel();
		changed();
	}

	void showLabel() {
		capture->ChangeValue(listening ? "Press keys... (Esc to cancel)" : formatThemeShortcut(mods, keyCode));
	}

	PrefCheck* enabled;
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

#ifdef __WXMSW__
// ---- The Windows window --------------------------------------------------------
//
// wx's own preferences window on Windows is a tabbed dialog with OK and
// Cancel that blocks the app while it is open. This is a window of its own
// instead, like Windows 11's Settings: pages down the left, the page as
// cards on the right, every change live, and the app usable behind it.

struct PageDef {
	const char* name;
	std::function<wxWindow*(wxWindow*)> make;
};

std::vector<PageDef> pageDefs() {
	return {
		{ "General",    [](wxWindow* p) { return (wxWindow*)new GeneralPanel(p); } },
		{ "Appearance", [](wxWindow* p) { return (wxWindow*)new AppearancePanel(p); } },
		{ "Canvas",     [](wxWindow* p) { return (wxWindow*)new CanvasPanel(p); } },
		{ "Toolbar",    [](wxWindow* p) { return (wxWindow*)new ToolbarPanel(p); } },
		{ "Shortcuts",  [](wxWindow* p) { return (wxWindow*)new ShortcutsPanel(p); } },
	};
}

// The list of pages down the left.
class PageList : public wxPanel {
public:
	PageList(wxWindow* parent, std::function<void(int)> onPick)
		: wxPanel(parent), onPick(onPick), pages(pageDefs()) {
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		SetMinSize(wxSize(FromDIP(200), -1));
		Bind(wxEVT_PAINT, &PageList::OnPaint, this);
		Bind(wxEVT_MOTION, [this](wxMouseEvent& e) {
			const int h = at(e.GetPosition());
			if (h != hot) { hot = h; SetCursor(h >= 0 ? wxCursor(wxCURSOR_HAND) : wxNullCursor); Refresh(); }
		});
		Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { hot = -1; Refresh(); });
		Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
			const int i = at(e.GetPosition());
			if (i >= 0 && this->onPick) this->onPick(i);
		});
	}
	void SetSelected(int i) { selected = i; Refresh(); }

private:
	wxRect rowRect(int i) const {
		return wxRect(FromDIP(10), FromDIP(64) + i * FromDIP(40), GetClientSize().x - FromDIP(20), FromDIP(36));
	}
	int at(const wxPoint& p) const {
		for (int i = 0; i < (int)pages.size(); i++) if (rowRect(i).Contains(p)) return i;
		return -1;
	}
	void OnPaint(wxPaintEvent&) {
		wxAutoBufferedPaintDC dc(this);
		dc.SetBackground(wxBrush(ui::sidebarColour()));
		dc.Clear();
		std::unique_ptr<wxGraphicsContext> gc(ui::graphics(dc));
		if (!gc) return;
		const wxColour in = ui::ink(), ac = ui::accent();
		gc->SetFont(wxFont(wxFontInfo(15).Bold()), in);
		gc->DrawText("Settings", FromDIP(20), FromDIP(20));
		for (int i = 0; i < (int)pages.size(); i++) {
			const wxRect r = rowRect(i);
			if (i == selected || i == hot) {
				gc->SetPen(*wxTRANSPARENT_PEN);
				gc->SetBrush(wxBrush(ui::withAlpha(in, i == selected ? (ui::isDark() ? 0.09 : 0.06)
				                                                    : (ui::isDark() ? 0.05 : 0.035))));
				gc->DrawRoundedRectangle(r.x, r.y, r.width, r.height, FromDIP(6));
			}
			if (i == selected) {   // the pill Windows 11 puts beside the current page
				gc->SetBrush(wxBrush(ac));
				gc->DrawRoundedRectangle(r.x, r.y + r.height / 2.0 - FromDIP(8), FromDIP(3), FromDIP(16), FromDIP(1.5));
			}
			gc->SetFont(wxFont(wxFontInfo(10.5)), in);
			double tw, th;
			gc->GetTextExtent(pages[i].name, &tw, &th);
			gc->DrawText(pages[i].name, r.x + FromDIP(14), r.y + (r.height - th) / 2);
		}
	}

	std::function<void(int)> onPick;
	std::vector<PageDef> pages;
	int selected = 0, hot = -1;
};

class SettingsWindow : public wxFrame {
public:
	explicit SettingsWindow(wxWindow* parent)
		: wxFrame(parent, wxID_ANY, "Settings", wxDefaultPosition, wxDefaultSize,
		          (wxDEFAULT_FRAME_STYLE & ~wxMAXIMIZE_BOX) | wxFRAME_FLOAT_ON_PARENT | wxFRAME_NO_TASKBAR) {
		SetClientSize(FromDIP(wxSize(860, 640)));
		SetMinClientSize(FromDIP(wxSize(760, 480)));
		list = new PageList(this, [this](int i) { Select(i); });
		content = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxBORDER_NONE);
		content->SetScrollRate(0, FromDIP(16));
		wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
		row->Add(list, 0, wxEXPAND);
		row->Add(content, 1, wxEXPAND);
		SetSizer(row);
		Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
			if (e.GetKeyCode() == WXK_ESCAPE && !g_capturingShortcut) { Close(); return; }
			e.Skip();
		});
		Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { Destroy(); });
		CentreOnParent();
		Retheme();
	}

	void Select(int i) {
		const std::vector<PageDef> defs = pageDefs();
		if (i < 0 || i >= (int)defs.size()) return;
		current = i;
		list->SetSelected(i);
		content->Freeze();
		content->DestroyChildren();
		wxBoxSizer* col = new wxBoxSizer(wxVERTICAL);
		wxStaticText* title = new wxStaticText(content, wxID_ANY, defs[i].name);
		title->SetFont(wxFont(wxFontInfo(20).Bold()));
		title->SetForegroundColour(ui::ink());
		col->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(28));
		wxWindow* page = defs[i].make(content);
		col->Add(page, 0, wxEXPAND | wxALL, FromDIP(28) - FromDIP(6));
		content->SetSizer(col);
		WinThemeControls(content, renderMode().darkMode);
		content->FitInside();
		content->Scroll(0, 0);
		content->Layout();
		content->Thaw();
		content->Refresh();
	}

	// The app's theme changed underneath: new colours, and the page rebuilt
	// so every control on it picks them up.
	void Retheme() {
		const bool dark = renderMode().darkMode;
		SetBackgroundColour(ui::pageColour());
		content->SetBackgroundColour(ui::pageColour());
		WinSetDarkTitlebar(this, dark);
		WinSetCaptionColour(this, ui::sidebarColour(), ui::ink());
		list->Refresh();
		Select(current);
	}

	int PageCount() const { return (int)pageDefs().size(); }

private:
	PageList* list;
	wxScrolledWindow* content;
	int current = 0;
};

wxWeakRef<SettingsWindow> g_settings;
#endif

} // namespace

void ShowPreferencesWindow(wxWindow* parent) {
#ifdef __WXMSW__
	if (g_settings) { g_settings->Raise(); return; }
	SettingsWindow* w = new SettingsWindow(parent);
	g_settings = w;
	w->Show();
	return;
#endif
	if (!g_editor) {
		g_editor.reset(new wxPreferencesEditor());
		g_editor->AddPage(new Page<GeneralPanel>("General", "gearshape"));
		g_editor->AddPage(new Page<AppearancePanel>("Appearance", "paintpalette"));
		g_editor->AddPage(new Page<CanvasPanel>("Canvas", "cursorarrow.rays"));
		g_editor->AddPage(new Page<ToolbarPanel>("Toolbar", "menubar.rectangle"));
		g_editor->AddPage(new Page<ShortcutsPanel>("Shortcuts", "keyboard"));
	}
	g_editor->Show(parent);
}

void PreferencesThemeChanged() {
#ifdef __WXMSW__
	if (g_settings) g_settings->Retheme();
#endif
}

#ifdef __WXMSW__
wxWindow* PreferencesWindowForCapture(int page) {
	if (!g_settings) return nullptr;
	g_settings->Select(page);
	return g_settings;
}
#endif

void DismissPreferencesWindow() {
#ifdef __WXMSW__
	if (g_settings) g_settings->Destroy();
	g_settings = nullptr;
#endif
	if (g_editor) {
		g_editor->Dismiss();
		g_editor.reset();
	}
}
