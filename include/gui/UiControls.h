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

}  // namespace ui

#endif
