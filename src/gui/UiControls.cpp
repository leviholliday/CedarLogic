/*****************************************************************************
   Project: CEDAR Logic Simulator
   UiControls: controls drawn by us. See UiControls.h.
*****************************************************************************/

#include "UiControls.h"
#include "UiKit.h"

#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <algorithm>
#include <cmath>

namespace ui {

wxColour pageColour()    { return isDark() ? wxColour(28, 31, 37) : wxColour(243, 244, 247); }
wxColour cardColour()    { return isDark() ? wxColour(38, 41, 48) : wxColour(255, 255, 255); }
wxColour sidebarColour() { return isDark() ? wxColour(22, 24, 29) : wxColour(236, 238, 242); }

// ---- ToggleSwitch -----------------------------------------------------------

ToggleSwitch::ToggleSwitch(wxWindow* parent, bool value)
	: wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxWANTS_CHARS),
	  on(value), knob(value ? 1.0 : 0.0), anim(this) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetCursor(wxCursor(wxCURSOR_HAND));
	SetInitialSize(DoGetBestSize());
	Bind(wxEVT_PAINT, &ToggleSwitch::OnPaint, this);
	Bind(wxEVT_LEFT_DOWN, &ToggleSwitch::OnClick, this);
	Bind(wxEVT_LEFT_DCLICK, &ToggleSwitch::OnClick, this);
	Bind(wxEVT_KEY_DOWN, &ToggleSwitch::OnKey, this);
	Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) { hot = true; Refresh(); });
	Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { hot = false; Refresh(); });
	Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); });
	Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); });
	anim.Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
		const double target = on ? 1.0 : 0.0;
		knob += (target - knob) * 0.45;   // quick to leave, soft to land
		if (std::fabs(target - knob) < 0.02) { knob = target; anim.Stop(); }
		Refresh();
	});
}

wxSize ToggleSwitch::DoGetBestSize() const { return FromDIP(wxSize(44, 24)); }

void ToggleSwitch::SetValue(bool value) {
	on = value;
	knob = value ? 1.0 : 0.0;
	Refresh();
}

void ToggleSwitch::toggle() {
	on = !on;
	anim.Start(15);
	Refresh();
	wxCommandEvent e(wxEVT_CHECKBOX, GetId());
	e.SetEventObject(this);
	e.SetInt(on ? 1 : 0);
	ProcessWindowEvent(e);
}

void ToggleSwitch::OnClick(wxMouseEvent&) {
	SetFocus();
	toggle();
}

void ToggleSwitch::OnKey(wxKeyEvent& e) {
	const int k = e.GetKeyCode();
	if (k == WXK_SPACE || k == WXK_RETURN || k == WXK_NUMPAD_ENTER) { toggle(); return; }
	if (k == WXK_TAB) { Navigate(e.ShiftDown() ? wxNavigationKeyEvent::IsBackward
	                                            : wxNavigationKeyEvent::IsForward); return; }
	e.Skip();
}

void ToggleSwitch::OnPaint(wxPaintEvent&) {
	wxAutoBufferedPaintDC dc(this);
	dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
	dc.Clear();
	std::unique_ptr<wxGraphicsContext> gc(graphics(dc));
	if (!gc) return;
	const wxSize sz = GetClientSize();
	const double h = FromDIP(20), w = FromDIP(40);
	const double x = (sz.x - w) / 2.0, y = (sz.y - h) / 2.0;
	const wxColour ac = accent(), in = ink();

	// The track: the accent when on, an outline when off, crossfading as the
	// knob travels.
	if (knob > 0.0) {
		gc->SetPen(*wxTRANSPARENT_PEN);
		gc->SetBrush(wxBrush(withAlpha(ac, knob)));
		gc->DrawRoundedRectangle(x, y, w, h, h / 2);
	}
	if (knob < 1.0) {
		gc->SetBrush(*wxTRANSPARENT_BRUSH);
		gc->SetPen(wxPen(withAlpha(in, 0.55 * (1.0 - knob)), 1));
		gc->DrawRoundedRectangle(x + 0.5, y + 0.5, w - 1, h - 1, (h - 1) / 2);
	}

	// The knob: grows a little when pointed at, the way Windows 11's does.
	const double r = FromDIP(hot ? 7.0 : 6.0) + knob * FromDIP(0.5);
	const double pad = h / 2.0;
	const double cx = x + pad + knob * (w - 2 * pad);
	const double cy = y + h / 2.0;
	const wxColour offKnob = withAlpha(in, 0.75);
	const wxColour c(
		(unsigned char)std::lround(offKnob.Red()   + (255 - offKnob.Red())   * knob),
		(unsigned char)std::lround(offKnob.Green() + (255 - offKnob.Green()) * knob),
		(unsigned char)std::lround(offKnob.Blue()  + (255 - offKnob.Blue())  * knob),
		(unsigned char)std::lround(offKnob.Alpha() + (255 - offKnob.Alpha()) * knob));
	gc->SetPen(*wxTRANSPARENT_PEN);
	gc->SetBrush(wxBrush(c));
	gc->DrawEllipse(cx - r, cy - r, 2 * r, 2 * r);

	if (HasFocus()) {
		gc->SetBrush(*wxTRANSPARENT_BRUSH);
		gc->SetPen(wxPen(withAlpha(in, 0.7), 1));
		gc->DrawRoundedRectangle(x - 2.5, y - 2.5, w + 5, h + 5, (h + 5) / 2);
	}
}

// ---- Card -------------------------------------------------------------------

Card::Card(wxWindow* parent) : wxPanel(parent, wxID_ANY) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetBackgroundColour(cardColour());   // what the controls inside sit on
	Bind(wxEVT_PAINT, &Card::OnPaint, this);
	Bind(wxEVT_SIZE, [this](wxSizeEvent& e) { Refresh(); e.Skip(); });
}

void Card::OnPaint(wxPaintEvent&) {
	wxAutoBufferedPaintDC dc(this);
	// The corners show the page behind the card.
	dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
	dc.Clear();
	std::unique_ptr<wxGraphicsContext> gc(graphics(dc));
	if (!gc) return;
	const wxSize sz = GetClientSize();
	gc->SetBrush(wxBrush(cardColour()));
	gc->SetPen(wxPen(withAlpha(ink(), isDark() ? 0.08 : 0.07), 1));
	gc->DrawRoundedRectangle(0.5, 0.5, sz.x - 1, sz.y - 1, FromDIP(8));
}

}  // namespace ui
