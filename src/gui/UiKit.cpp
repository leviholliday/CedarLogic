/*****************************************************************************
   Project: CEDAR Logic Simulator
   UiKit: shared drawing for the hand-drawn windows. See UiKit.h.
*****************************************************************************/

#include "UiKit.h"
#include "MainApp.h"
#include "Settings.h"
#include "RenderMode.h"
#include "render/RenderStyle.h"

#include <wx/graphics.h>
#include <wx/font.h>
#include <algorithm>
#include <cmath>

namespace ui {

std::unique_ptr<wxGraphicsContext> graphics(wxDC& dc) {
#if defined(__WXMSW__) && wxUSE_GRAPHICS_DIRECT2D
	// Null where Direct2D is missing or will not take this kind of DC; the
	// default renderer below still draws it, just more slowly.
	if (wxGraphicsRenderer* d2d = wxGraphicsRenderer::GetDirect2DRenderer())
		if (wxGraphicsContext* gc = d2d->CreateContextFromUnknownDC(dc))
			return std::unique_ptr<wxGraphicsContext>(gc);
#endif
	return std::unique_ptr<wxGraphicsContext>(wxGraphicsContext::CreateFromUnknownDC(dc));
}

bool isDark() { return renderMode().darkMode; }

wxColour withAlpha(const wxColour& c, double a) {
	a = std::max(0.0, std::min(1.0, a));
	return wxColour(c.Red(), c.Green(), c.Blue(), (unsigned char)std::lround(255 * a));
}

wxColour accentFor(int index) {
	cl::render::RenderStyle rs;
	rs.darkMode = isDark();
	rs.accentIndex = index;
	const cl::render::Color c = rs.accent();
	return wxColour((unsigned char)std::lround(c.r * 255), (unsigned char)std::lround(c.g * 255),
	                (unsigned char)std::lround(c.b * 255));
}

wxColour accent() { return accentFor(appConfig().appSettings.accentColor); }
wxColour paper()  { return isDark() ? wxColour(28, 31, 37) : wxColour(250, 250, 252); }
wxColour ink()    { return isDark() ? wxColour(230, 234, 242) : wxColour(26, 29, 36); }
wxColour dim()    { return withAlpha(ink(), 0.6); }
wxColour success() { return isDark() ? wxColour(74, 211, 125) : wxColour(30, 150, 80); }

void drawButton(wxGraphicsContext* gc, const wxRect& r, const wxString& label,
                bool filled, bool hot) {
	const wxColour ac = accent(), in = ink();
	if (filled) {
		gc->SetBrush(gc->CreateLinearGradientBrush(0, r.y, 0, r.GetBottom(),
			withAlpha(ac, hot ? 1.0 : 0.94), withAlpha(ac, hot ? 0.88 : 0.78)));
		gc->SetPen(*wxTRANSPARENT_PEN);
	} else {
		gc->SetBrush(wxBrush(withAlpha(in, hot ? 0.13 : 0.07)));
		gc->SetPen(wxPen(withAlpha(in, 0.16), 1));
	}
	gc->DrawRoundedRectangle(r.x + 0.5, r.y + 0.5, r.width - 1, r.height - 1, r.height / 2.0);
	gc->SetFont(wxFont(wxFontInfo(13).Bold(filled)), filled ? *wxWHITE : in);
	double tw, th;
	gc->GetTextExtent(label, &tw, &th);
	gc->DrawText(label, r.x + (r.width - tw) / 2, r.y + (r.height - th) / 2);
}

wxString platformKeys(const wxString& text) {
#ifdef __WXOSX__
	return text;
#else
	wxString s = text;
	s.Replace("Cmd", "Ctrl");
	s.Replace("Option", "Alt");
	s.Replace("press Return", "press Enter");
	return s;
#endif
}

wxString keyGlyph(const wxString& token) {
#ifdef __WXOSX__
	if (token == "Cmd")    return wxString::FromUTF8("⌘");
	if (token == "Shift")  return wxString::FromUTF8("⇧");
	if (token == "Option" || token == "Alt") return wxString::FromUTF8("⌥");
	if (token == "Ctrl")   return wxString::FromUTF8("⌃");
	if (token == "Return") return wxString::FromUTF8("↩");
	if (token == "Delete") return wxString::FromUTF8("⌫");
	if (token == "Tab")    return wxString::FromUTF8("⇥");
#else
	if (token == "Cmd")    return "Ctrl";
	if (token == "Option") return "Alt";
	if (token == "Return") return "Enter";
#endif
	if (token == "Escape") return "Esc";
	if (token == "Left")   return wxString::FromUTF8("←");
	if (token == "Right")  return wxString::FromUTF8("→");
	if (token == "Up")     return wxString::FromUTF8("↑");
	if (token == "Down")   return wxString::FromUTF8("↓");
	return token;
}

double drawKeyCap(wxGraphicsContext* gc, double x, double y, double h,
                  const wxString& text, bool measureOnly) {
	const wxColour in = ink();
	// A single glyph gets a square cap; words get room to breathe.
	gc->SetFont(wxFont(wxFontInfo(h * 0.46).Bold()), in);
	double tw, th;
	gc->GetTextExtent(text, &tw, &th);
	const double w = std::max(h, tw + h * 0.62);
	if (measureOnly) return w;

	// Raised: a darker lip under a lighter face.
	gc->SetPen(*wxTRANSPARENT_PEN);
	gc->SetBrush(wxBrush(withAlpha(in, isDark() ? 0.22 : 0.16)));
	gc->DrawRoundedRectangle(x, y + 1.5, w, h, h * 0.24);
	gc->SetBrush(wxBrush(isDark() ? wxColour(52, 57, 67) : *wxWHITE));
	gc->SetPen(wxPen(withAlpha(in, isDark() ? 0.20 : 0.14), 1));
	gc->DrawRoundedRectangle(x + 0.5, y + 0.5, w - 1, h - 1, h * 0.24);
	gc->DrawText(text, x + (w - tw) / 2, y + (h - th) / 2 - 0.5);
	return w;
}

double drawKeys(wxGraphicsContext* gc, double x, double y, double h,
                const std::vector<wxString>& keys, bool measureOnly) {
	const double gap = h * 0.16;
	double at = x;
	for (size_t i = 0; i < keys.size(); i++) {
		if (i) at += gap;
		const wxString& k = keys[i];
		// Lower-case tokens are gestures ("drag", "click", "scroll"), not
		// keys: written out in plain text beside the caps.
		if (!k.empty() && wxIslower(k[0])) {
			gc->SetFont(wxFont(wxFontInfo(h * 0.5)), dim());
			double tw, th;
			gc->GetTextExtent(k, &tw, &th);
			if (!measureOnly) gc->DrawText(k, at + 1, y + (h - th) / 2);
			at += tw + 2;
			continue;
		}
		at += drawKeyCap(gc, at, y, h, keyGlyph(k), measureOnly);
	}
	return at - x;
}

std::vector<wxString> wrap(wxGraphicsContext* gc, const wxString& text, double width) {
	std::vector<wxString> lines;
	wxString rest = text, line;
	double tw, th;
	while (!rest.empty()) {
		const int space = rest.Find(' ');
		const wxString word = space == wxNOT_FOUND ? rest : rest.Left(space);
		rest = space == wxNOT_FOUND ? wxString() : rest.Mid(space + 1);
		const wxString tryLine = line.empty() ? word : line + " " + word;
		gc->GetTextExtent(tryLine, &tw, &th);
		if (tw > width && !line.empty()) {
			lines.push_back(line);
			line = word;
		} else {
			line = tryLine;
		}
	}
	if (!line.empty()) lines.push_back(line);
	return lines;
}

}  // namespace ui
