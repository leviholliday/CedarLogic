/*****************************************************************************
   Project: CEDAR Logic Simulator
   StatusStrip: the status bar, drawn by us. See StatusStrip.h.
*****************************************************************************/

#include "StatusStrip.h"
#include "UiKit.h"

#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/settings.h>

namespace {

// The same colour as the strip of window around the canvas, so the bottom of
// the window reads as one surface with it.
wxColour stripColour() {
	return ui::isDark() ? wxColour(22, 24, 28) : wxColour(233, 234, 238);
}

}  // namespace

StatusStrip::StatusStrip(wxWindow* parent, wxWindowID id, long style, const wxString& name)
	// No grip: Windows 11 resizes from any edge, and the grip was the one
	// piece of 1990s left on the window.
	: wxStatusBar(parent, id, style & ~wxSTB_SIZEGRIP, name) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetFont(wxFont(wxFontInfo(9)));
	Bind(wxEVT_PAINT, &StatusStrip::OnPaint, this);
	Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
}

void StatusStrip::OnPaint(wxPaintEvent&) {
	wxAutoBufferedPaintDC dc(this);
	const wxSize sz = GetClientSize();
	dc.SetBackground(wxBrush(stripColour()));
	dc.Clear();
	std::unique_ptr<wxGraphicsContext> gc(ui::graphics(dc));
	if (!gc) return;

	gc->SetPen(wxPen(ui::isDark() ? wxColour(255, 255, 255, 18) : wxColour(0, 0, 0, 20), 1));
	gc->StrokeLine(0, 0.5, sz.x, 0.5);

	gc->SetFont(GetFont(), ui::dim());
	for (int i = 0; i < GetFieldsCount(); i++) {
		const wxString text = GetStatusText(i);
		if (text.empty()) continue;
		wxRect r;
		if (!GetFieldRect(i, r)) continue;
		double tw, th;
		gc->GetTextExtent(text, &tw, &th);
		// The message field reads from the left; the figures after it (zoom,
		// position, counts) sit against the right of their fields.
		const double x = (i == 0) ? r.x + FromDIP(10) : r.GetRight() - tw - FromDIP(10);
		gc->Clip(r.x, 0, r.width, sz.y);
		gc->DrawText(text, x, (sz.y - th) / 2.0 + 0.5);
		gc->ResetClip();
	}
}
