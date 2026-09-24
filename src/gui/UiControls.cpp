/*****************************************************************************
   Project: CEDAR Logic Simulator
   UiControls: controls drawn by us. See UiControls.h.
*****************************************************************************/

#include "UiControls.h"
#include "UiKit.h"

#include <wx/dcbuffer.h>
#include <wx/dcmemory.h>
#include <wx/graphics.h>
#include <wx/app.h>
#ifdef __WXMSW__
#include "WinAppearance.h"
#endif
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

// ---- SearchField --------------------------------------------------------------

namespace ui {

namespace {
wxColour fieldColour() { return isDark() ? wxColour(40, 43, 50) : wxColour(255, 255, 255); }
}

SearchField::SearchField(wxWindow* parent, wxWindowID id) : wxPanel(parent, wxID_ANY) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetMinSize(wxSize(-1, FromDIP(38)));
	edit = new wxTextCtrl(this, id, "", wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
	edit->SetFont(wxFont(wxFontInfo(11)));
	edit->SetBackgroundColour(fieldColour());
	edit->SetForegroundColour(ink());
	Bind(wxEVT_PAINT, &SearchField::OnPaint, this);
	Bind(wxEVT_SIZE, [this](wxSizeEvent& e) { place(); Refresh(); e.Skip(); });
	edit->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); });
	edit->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); });
	edit->Bind(wxEVT_TEXT, [this](wxCommandEvent& e) { Refresh(); e.Skip(); });   // the clear button
	Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
		if (cancel && !edit->IsEmpty() && clearRect().Contains(e.GetPosition())) edit->Clear();
		edit->SetFocus();
	});
	Bind(wxEVT_MOTION, [this](wxMouseEvent& e) {
		SetCursor(cancel && !edit->IsEmpty() && clearRect().Contains(e.GetPosition())
		          ? wxCursor(wxCURSOR_HAND) : wxCursor(wxCURSOR_IBEAM));
	});
}

wxRect SearchField::clearRect() const {
	const wxSize sz = GetClientSize();
	const int d = FromDIP(22);
	return wxRect(sz.x - FromDIP(8) - d, (sz.y - d) / 2, d, d);
}

void SearchField::place() {
	const wxSize sz = GetClientSize();
	const int left = FromDIP(36), right = FromDIP(36);
	const int h = edit->GetBestSize().y;
	edit->SetSize(left, (sz.y - h) / 2, std::max(10, sz.x - left - right), h);
}

void SearchField::OnPaint(wxPaintEvent&) {
	wxAutoBufferedPaintDC dc(this);
	dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
	dc.Clear();
	std::unique_ptr<wxGraphicsContext> gc(graphics(dc));
	if (!gc) return;
	const wxSize sz = GetClientSize();
	const wxColour in = ink();
	const bool focus = edit->HasFocus();
	const double r = FromDIP(8);

	gc->SetBrush(wxBrush(fieldColour()));
	gc->SetPen(wxPen(withAlpha(in, isDark() ? 0.12 : 0.14), 1));
	gc->DrawRoundedRectangle(0.5, 0.5, sz.x - 1, sz.y - 1, r);
	if (focus) {   // Windows 11 marks the field you are typing in along its foot
		gc->SetPen(*wxTRANSPARENT_PEN);
		gc->SetBrush(wxBrush(accent()));
		gc->DrawRoundedRectangle(r / 2, sz.y - FromDIP(2.5), sz.x - r, FromDIP(2), FromDIP(1));
	}

	// The magnifier.
	const double cx = FromDIP(18), cy = sz.y / 2.0 - FromDIP(1), lr = FromDIP(5.5);
	gc->SetBrush(*wxTRANSPARENT_BRUSH);
	gc->SetPen(wxPen(withAlpha(in, 0.6), FromDIP(2)));
	gc->DrawEllipse(cx - lr, cy - lr, 2 * lr, 2 * lr);
	gc->StrokeLine(cx + lr * 0.72, cy + lr * 0.72, cx + lr * 1.55, cy + lr * 1.55);

	if (cancel && !edit->IsEmpty()) {   // a quiet round clear button
		const wxRect c = clearRect();
		const double ccx = c.x + c.width / 2.0, ccy = c.y + c.height / 2.0, cr = c.width / 2.0 - 2;
		gc->SetPen(*wxTRANSPARENT_PEN);
		gc->SetBrush(wxBrush(withAlpha(in, 0.10)));
		gc->DrawEllipse(ccx - cr, ccy - cr, 2 * cr, 2 * cr);
		gc->SetPen(wxPen(withAlpha(in, 0.7), FromDIP(1.5)));
		const double k = cr * 0.4;
		gc->StrokeLine(ccx - k, ccy - k, ccx + k, ccy + k);
		gc->StrokeLine(ccx - k, ccy + k, ccx + k, ccy - k);
	}
}

}  // namespace ui

// ---- MessageDialog ------------------------------------------------------------

namespace ui {

#ifdef __WXMSW__

namespace {
const int kWidth = 440;   // DIP
}

MessageDialog::MessageDialog(wxWindow* parent, const wxString& message,
                             const wxString& caption, long style)
	: wxDialog(parent, wxID_ANY, caption, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE),
	  message(message), caption(caption), style(style) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	// Answers left to right, the main one last and filled, the way Windows 11
	// lays out its own dialogs.
	if (style & wxYES_NO) {
		const bool noDefault = (style & wxNO_DEFAULT) != 0;
		if (style & wxCANCEL) buttons.push_back({ wxID_CANCEL, "Cancel", false, wxRect() });
		buttons.push_back({ wxID_NO, "No", noDefault, wxRect() });
		buttons.push_back({ wxID_YES, "Yes", !noDefault, wxRect() });
	} else {
		if (style & wxCANCEL) buttons.push_back({ wxID_CANCEL, "Cancel", false, wxRect() });
		buttons.push_back({ wxID_OK, "OK", true, wxRect() });
	}
	for (size_t i = 0; i < buttons.size(); i++) if (buttons[i].primary) focused = (int)i;

	Bind(wxEVT_PAINT, &MessageDialog::OnPaint, this);
	Bind(wxEVT_CHAR_HOOK, &MessageDialog::OnKey, this);
	Bind(wxEVT_MOTION, [this](wxMouseEvent& e) {
		int h = -1;
		for (size_t i = 0; i < buttons.size(); i++) if (buttons[i].rect.Contains(e.GetPosition())) h = (int)i;
		if (h != hot) { hot = h; SetCursor(h >= 0 ? wxCursor(wxCURSOR_HAND) : wxNullCursor); Refresh(); }
	});
	Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { hot = -1; Refresh(); });
	Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& e) {
		for (const Button& b : buttons) if (b.rect.Contains(e.GetPosition())) { finish(b.id); return; }
	});
	Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) {
		// Closing is the safe answer: Cancel if there is one, else No, else OK.
		for (int id : { wxID_CANCEL, wxID_NO, wxID_OK })
			for (const Button& b : buttons) if (b.id == id) { finish(id); return; }
	});
}

void MessageDialog::relabel(int id, const wxString& label) {
	for (Button& b : buttons) if (b.id == id) b.label = label;
}
bool MessageDialog::SetYesNoLabels(const wxString& yes, const wxString& no) {
	relabel(wxID_YES, yes); relabel(wxID_NO, no); return true;
}
bool MessageDialog::SetYesNoCancelLabels(const wxString& yes, const wxString& no, const wxString& cancel) {
	relabel(wxID_YES, yes); relabel(wxID_NO, no); relabel(wxID_CANCEL, cancel); return true;
}
bool MessageDialog::SetOKLabel(const wxString& ok) { relabel(wxID_OK, ok); return true; }
bool MessageDialog::SetOKCancelLabels(const wxString& ok, const wxString& cancel) {
	relabel(wxID_OK, ok); relabel(wxID_CANCEL, cancel); return true;
}

// Lay out (and, with `paint`, draw) the card. Returns the height it needs.
int MessageDialog::layout(bool paint, wxGraphicsContext* gc) {
	const int W = FromDIP(kWidth), pad = FromDIP(24);
	const wxColour in = ink();
	double y = pad;

	// A small round badge that says what kind of message this is.
	wxColour badge = accent();
	wxString glyph = "i";
	if (style & wxICON_ERROR) { badge = isDark() ? wxColour(255, 107, 107) : wxColour(214, 48, 49); glyph = "!"; }
	else if (style & (wxICON_WARNING | wxICON_EXCLAMATION)) { badge = wxColour(240, 160, 30); glyph = "!"; }
	else if (style & wxICON_QUESTION) glyph = "?";
	const double br = FromDIP(14);
	if (paint) {
		gc->SetPen(*wxTRANSPARENT_PEN);
		gc->SetBrush(wxBrush(withAlpha(badge, 0.16)));
		gc->DrawEllipse(pad, y, 2 * br, 2 * br);
		gc->SetFont(wxFont(wxFontInfo(12).Bold()), badge);
		double gw, gh;
		gc->GetTextExtent(glyph, &gw, &gh);
		gc->DrawText(glyph, pad + br - gw / 2, y + br - gh / 2);
	}
	const double tx = pad + 2 * br + FromDIP(14);
	const double textW = W - tx - pad;

	auto block = [&](const wxString& text, const wxFont& font, const wxColour& colour, double after) {
		if (text.empty()) return;
		gc->SetFont(font, colour);
		for (const wxString& line : wrap(gc, text, textW)) {
			double lw, lh;
			gc->GetTextExtent(line.empty() ? wxString("Ay") : line, &lw, &lh);
			if (paint) gc->DrawText(line, tx, y);
			y += lh + FromDIP(2);
		}
		y += after;
	};
	// A generic caption says nothing, so the message itself becomes the title.
	const bool captionSaysSomething = !caption.empty() && caption != "Message";
	y += FromDIP(3);
	if (captionSaysSomething) {
		block(caption, wxFont(wxFontInfo(12.5).Bold()), in, FromDIP(6));
		block(message, wxFont(wxFontInfo(10.5)), in, FromDIP(4));
	} else {
		block(message, wxFont(wxFontInfo(11.5).Bold()), in, FromDIP(4));
	}
	block(extended, wxFont(wxFontInfo(10)), dim(), 0);
	y = std::max(y, (double)pad + 2 * br) + FromDIP(22);

	// The buttons, right-aligned along the foot.
	const int bh = FromDIP(34);
	double x = W - pad;
	gc->SetFont(wxFont(wxFontInfo(10.5)), in);
	for (int i = (int)buttons.size() - 1; i >= 0; i--) {
		double lw, lh;
		gc->GetTextExtent(buttons[i].label, &lw, &lh);
		const int bw = std::max(FromDIP(88), (int)lw + FromDIP(36));
		x -= bw;
		buttons[i].rect = wxRect((int)x, (int)y, bw, bh);
		x -= FromDIP(8);
	}
	if (paint) {
		for (size_t i = 0; i < buttons.size(); i++) {
			const Button& b = buttons[i];
			drawButton(gc, b.rect, b.label, b.primary, (int)i == hot);
			if ((int)i == focused && !b.primary) {
				gc->SetBrush(*wxTRANSPARENT_BRUSH);
				gc->SetPen(wxPen(withAlpha(in, 0.6), 1));
				gc->DrawRoundedRectangle(b.rect.x - 2.5, b.rect.y - 2.5, b.rect.width + 5, b.rect.height + 5,
				                         (b.rect.height + 5) / 2.0);
			}
		}
	}
	return (int)(y + bh + pad);
}

void MessageDialog::OnPaint(wxPaintEvent&) {
	wxAutoBufferedPaintDC dc(this);
	dc.SetBackground(wxBrush(cardColour()));
	dc.Clear();
	std::unique_ptr<wxGraphicsContext> gc(graphics(dc));
	if (!gc) return;
	const wxSize sz = GetClientSize();
	// Windows 11 draws its own edge round the corners; Windows 10 gets this.
	gc->SetBrush(*wxTRANSPARENT_BRUSH);
	gc->SetPen(wxPen(withAlpha(ink(), 0.12), 1));
	gc->DrawRectangle(0.5, 0.5, sz.x - 1, sz.y - 1);
	layout(true, gc.get());
}

void MessageDialog::OnKey(wxKeyEvent& e) {
	const int k = e.GetKeyCode();
	auto has = [this](int id) { for (const Button& b : buttons) if (b.id == id) return true; return false; };
	if (k == WXK_RETURN || k == WXK_NUMPAD_ENTER || k == WXK_SPACE) { finish(buttons[focused].id); return; }
	if (k == WXK_ESCAPE) { Close(); return; }
	if (k == WXK_TAB || k == WXK_LEFT || k == WXK_RIGHT) {
		const int step = (k == WXK_LEFT || (k == WXK_TAB && e.ShiftDown())) ? -1 : 1;
		focused = (focused + step + (int)buttons.size()) % (int)buttons.size();
		Refresh();
		return;
	}
	// Y and N answer a yes-or-no question without reaching for the mouse.
	if ((k == 'Y' || k == 'y') && has(wxID_YES)) { finish(wxID_YES); return; }
	if ((k == 'N' || k == 'n') && has(wxID_NO))  { finish(wxID_NO); return; }
	e.Skip();
}

void MessageDialog::finish(int id) {
	if (IsModal()) EndModal(id);
	else { SetReturnCode(id); Hide(); }
}

int MessageDialog::ShowModal() {
	Prepare();
	return wxDialog::ShowModal();
}

void MessageDialog::Prepare() {
	// Measure on a scratch context, size the card to fit, then place it.
	{
		wxBitmap scratch(1, 1);
		wxMemoryDC mdc(scratch);
		std::unique_ptr<wxGraphicsContext> gc(graphics(mdc));
		if (gc) SetClientSize(FromDIP(kWidth), layout(false, gc.get()));
	}
	wxWindow* over = GetParent() ? GetParent() : wxTheApp->GetTopWindow();
	if (over) CentreOnParent(); else Centre();
	WinRoundCorners(this);
}

#endif  // __WXMSW__

int Message(const wxString& message, const wxString& caption, long style, wxWindow* parent) {
#ifdef __WXMSW__
	if (parent == nullptr) parent = wxTheApp ? wxTheApp->GetTopWindow() : nullptr;
	MessageDialog dlg(parent, message, caption, style);
	switch (dlg.ShowModal()) {
		case wxID_YES:    return wxYES;
		case wxID_NO:     return wxNO;
		case wxID_CANCEL: return wxCANCEL;
		default:          return wxOK;
	}
#else
	return wxMessageBox(message, caption, style, parent);
#endif
}

}  // namespace ui
