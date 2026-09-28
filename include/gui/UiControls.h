/*****************************************************************************
   Project: CEDAR Logic Simulator
   UiControls: a few controls drawn by us, for the windows that are ours on
   Windows (Settings, messages, export). Windows' stock checkboxes and panels
   look like Windows 7 and ignore the app's theme; these follow it.
*****************************************************************************/

#ifndef UICONTROLS_H_
#define UICONTROLS_H_

#include <wx/control.h>
#include <wx/panel.h>
#include <wx/timer.h>
#include <wx/dialog.h>
#include <wx/msgdlg.h>
#include <wx/srchctrl.h>
#include <wx/textctrl.h>
#include <vector>

class wxGraphicsContext;

namespace ui {

// The colours of a settings-style window: the page, the cards on it, and the
// sidebar beside it.
wxColour pageColour();
wxColour cardColour();
wxColour sidebarColour();

// An on/off switch in the Windows 11 style. Same use as a wxCheckBox:
// GetValue/SetValue, and it sends wxEVT_CHECKBOX when clicked.
class ToggleSwitch : public wxControl {
public:
	ToggleSwitch(wxWindow* parent, bool value);
	bool GetValue() const { return on; }
	void SetValue(bool value);

	bool AcceptsFocus() const override { return true; }
	bool AcceptsFocusFromKeyboard() const override { return true; }
	bool HasTransparentBackground() override { return true; }

protected:
	wxSize DoGetBestSize() const override;

private:
	void OnPaint(wxPaintEvent&);
	void OnClick(wxMouseEvent&);
	void OnKey(wxKeyEvent&);
	void toggle();

	bool on = false;
	bool hot = false;
	double knob = 0.0;   // 0 at the left, 1 at the right; animates to `on`
	wxTimer anim;
};

// A rounded card on a page, holding one setting or a group of them.
class Card : public wxPanel {
public:
	explicit Card(wxWindow* parent);

private:
	void OnPaint(wxPaintEvent&);
};

// A search box: a rounded field with a magnifier and a clear button, holding a
// borderless text field. Same use as wxSearchCtrl for what the app needs of
// it; its wxEVT_TEXT reaches handlers bound on the box.
class SearchField : public wxPanel {
public:
	SearchField(wxWindow* parent, wxWindowID id = wxID_ANY);
	void ShowCancelButton(bool show) { cancel = show; Refresh(); }
	void SetDescriptiveText(const wxString& hint) { edit->SetHint(hint); }
	wxString GetValue() const { return edit->GetValue(); }
	void Clear() { edit->Clear(); }
	void SelectAll() { edit->SelectAll(); }
	void SetFocus() override { edit->SetFocus(); }

private:
	void OnPaint(wxPaintEvent&);
	void place();
	wxRect clearRect() const;

	wxTextCtrl* edit;
	bool cancel = false;
};

// The search box to use: ours on Windows, the system's elsewhere.
#ifdef __WXMSW__
using SearchBox = SearchField;
#else
using SearchBox = wxSearchCtrl;
#endif

// A message or question. Same use as wxMessageDialog -- the few setters the
// app needs, and ShowModal returning wxID_YES / NO / OK / CANCEL. On Windows
// it is ours: a rounded card with the answer buttons, where the stock one is
// a grey Windows 7 box that ignores dark mode. Elsewhere it is the stock one.
#ifdef __WXMSW__
class MessageDialog : public wxDialog {
public:
	MessageDialog(wxWindow* parent, const wxString& message,
	              const wxString& caption = "Message", long style = wxOK | wxCENTRE);
	void SetExtendedMessage(const wxString& text) { extended = text; }
	bool SetYesNoLabels(const wxString& yes, const wxString& no);
	bool SetYesNoCancelLabels(const wxString& yes, const wxString& no, const wxString& cancel);
	bool SetOKLabel(const wxString& ok);
	bool SetOKCancelLabels(const wxString& ok, const wxString& cancel);
	int ShowModal() override;
	// Size and place the card without showing it (ShowModal does this first;
	// --render-windows calls it to capture one).
	void Prepare();

private:
	struct Button { int id; wxString label; bool primary; wxRect rect; };
	int layout(bool paint, wxGraphicsContext* gc);
	void OnPaint(wxPaintEvent&);
	void OnKey(wxKeyEvent&);
	void finish(int id);
	void relabel(int id, const wxString& label);

	wxString message, caption, extended;
	long style;
	std::vector<Button> buttons;
	int focused = 0, hot = -1;
};
#else
using MessageDialog = wxMessageDialog;
#endif

// wxMessageBox, the same way round: wxYES, wxNO, wxOK or wxCANCEL back.
int Message(const wxString& message, const wxString& caption = "Message",
            long style = wxOK | wxCENTRE, wxWindow* parent = nullptr);

}  // namespace ui

#endif
