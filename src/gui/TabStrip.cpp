/*****************************************************************************
   Project: CEDAR Logic Simulator
   TabStrip: the row of tabs above the canvas, drawn by hand.
*****************************************************************************/

#include "TabStrip.h"
#include "MainFrame.h"
#include "MainApp.h"
#include "Settings.h"
#include "RenderMode.h"
#include "GUICanvas.h"
#include "render/RenderStyle.h"

#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/frame.h>
#include <wx/display.h>
#include <algorithm>
#include <cmath>

DECLARE_APP(MainApp)

namespace {

const int TAB_H = 30, TAB_MIN = 86, TAB_MAX = 220, TAB_GAP = 2, EDGE = 8, PLUS_W = 26;

bool isDark() { return renderMode().darkMode; }

wxColour accentColour() {
	cl::render::RenderStyle rs;
	rs.darkMode = isDark();
	rs.accentIndex = appConfig().appSettings.accentColor;
	const cl::render::Color c = rs.accent();
	return wxColour((unsigned char)(c.r * 255), (unsigned char)(c.g * 255), (unsigned char)(c.b * 255));
}

wxColour withAlpha(const wxColour& c, double a) {
	return wxColour(c.Red(), c.Green(), c.Blue(), (unsigned char)std::lround(255 * a));
}

wxColour barColour() { return isDark() ? wxColour(22, 24, 28) : wxColour(233, 234, 238); }
wxColour inkColour() { return isDark() ? wxColour(228, 232, 240) : wxColour(32, 35, 42); }
// The active tab is a card of the canvas's own colour, so it reads as the
// front of the page rather than as a button sitting on top of it.
wxColour cardColour() { return isDark() ? wxColour(32, 35, 41) : *wxWHITE; }

}  // namespace

// A translucent accent panel over half the canvas, shown while a tab is held
// out over it: the same "drop here" affordance a browser gives you. It is its
// own borderless window because the canvas draws through OpenGL/Skia, and a
// sibling panel over that does not reliably stay on top.
class DropHint : public wxFrame {
public:
	DropHint(wxWindow* parent)
		: wxFrame(parent, wxID_ANY, "", wxDefaultPosition, wxSize(10, 10),
		          wxFRAME_NO_TASKBAR | wxFRAME_FLOAT_ON_PARENT | wxBORDER_NONE | wxSTAY_ON_TOP) {
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		SetTransparent(160);
		Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
			const wxString msg = label;
			wxAutoBufferedPaintDC dc(this);
			const wxSize sz = GetClientSize();
			dc.SetBackground(wxBrush(isDark() ? wxColour(18, 20, 24) : *wxWHITE));
			dc.Clear();
			std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
			if (!gc) return;
			const wxColour accent = accentColour();
			gc->SetBrush(wxBrush(withAlpha(accent, 0.30)));
			gc->SetPen(wxPen(accent, 2));
			gc->DrawRoundedRectangle(3, 3, sz.x - 6, sz.y - 6, 14);
			gc->SetFont(wxFont(wxFontInfo(13).Bold()), accent);
			double tw, th;
			gc->GetTextExtent(msg, &tw, &th);
			gc->DrawText(msg, (sz.x - tw) / 2, (sz.y - th) / 2);
		});
	}
	void SetLabel2(const wxString& s) { if (label != s) { label = s; Refresh(); } }
private:
	wxString label = "Drop to split here";
};

TabStrip::TabStrip(wxWindow* parent, MainFrame* frame, int pane)
	: wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, BarHeight())), frame(frame), pane(pane) {
	MainFrame* mf = frame;
	const int myPane = pane;
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetMinSize(wxSize(-1, BarHeight()));
	Bind(wxEVT_PAINT, &TabStrip::OnPaint, this);
	Bind(wxEVT_LEFT_DOWN, &TabStrip::OnDown, this);
	Bind(wxEVT_LEFT_UP, &TabStrip::OnUp, this);
	Bind(wxEVT_MOTION, &TabStrip::OnMotion, this);
	Bind(wxEVT_LEAVE_WINDOW, &TabStrip::OnLeave, this);
	Bind(wxEVT_MOUSE_CAPTURE_LOST, &TabStrip::OnCaptureLost, this);
	Bind(wxEVT_SIZE, [this](wxSizeEvent& e) { layout(); Refresh(); e.Skip(); });
	// A double-click arrives as its second press, and the first press has
	// already acted. Only when both land on the same tab's name does it mean
	// "rename"; after + or a close button the tabs have moved under the
	// pointer, and renaming whatever slid there was never what was meant.
	Bind(wxEVT_LEFT_DCLICK, [this, mf, myPane](wxMouseEvent& e) {
		const wxPoint p = e.GetPosition();
		const int i = tabAt(p);
		const bool onClose = i >= 0 && tabs.size() > 1 && tabs[i].close.Contains(p);
		if (i >= 0 && !onClose && tabs[i].canvas == downOnTab) {
			GUICanvas* canvas = tabs[i].canvas;
			if (HasCapture()) ReleaseMouse();
			endDrag(true);
			mf->RenameTab(canvas);
			return;
		}
		if (i < 0 && downOnEmpty && !plusRect.Contains(p)) {
			mf->NewTabInPane(myPane);   // double-click the empty strip: a new tab
			return;
		}
		OnDown(e);   // otherwise it is simply another click
	});
}

TabStrip::~TabStrip() {
	if (hint) hint->Destroy();
}

void TabStrip::Rebuild() {
	GUICanvas* dragged = (dragTab >= 0 && dragTab < (int)tabs.size()) ? tabs[dragTab].canvas : nullptr;
	tabs.clear();
	for (GUICanvas* c : frame->PaneCanvases(pane)) {
		Tab t;
		t.canvas = c;
		t.label = frame->TabLabel(c);
		tabs.push_back(t);
	}
	// A drag in progress follows its tab to wherever it sits now, or ends if
	// the tab has gone (closed from the keyboard mid-drag, say).
	if (dragTab >= 0) {
		dragTab = -1;
		for (size_t i = 0; i < tabs.size(); i++) if (tabs[i].canvas == dragged) dragTab = (int)i;
		if (dragTab < 0) {
			if (HasCapture()) ReleaseMouse();
			endDrag(true);
		}
	}
	if (hover >= (int)tabs.size()) hover = -1;
	if (hoverClose >= (int)tabs.size()) hoverClose = -1;
	layout();
	Refresh();
}

void TabStrip::layout() {
	const int w = GetClientSize().x;
	// Only the leftmost strip can collide with the window's own buttons; the
	// right-hand pane of a split sits well clear of them.
	const int left = (pane == 0) ? frame->TabStripLeftInset() : EDGE;
	if (tabs.empty()) { plusRect = wxRect(left, 4, PLUS_W, TAB_H); return; }
	const int room = w - left - EDGE - PLUS_W - TAB_GAP;
	int tw = (room - TAB_GAP * ((int)tabs.size() - 1)) / (int)tabs.size();
	tw = std::max(TAB_MIN, std::min(TAB_MAX, tw));
	if ((int)tabs.size() * (TAB_MAX + TAB_GAP) < room) tw = std::min(tw, 160);
	int x = left;
	for (Tab& t : tabs) {
		t.rect = wxRect(x, (BarHeight() - TAB_H) / 2, tw, TAB_H);
		t.close = wxRect(x + tw - 24, t.rect.y + (TAB_H - 16) / 2, 16, 16);
		x += tw + TAB_GAP;
	}
	plusRect = wxRect(x, (BarHeight() - TAB_H) / 2, PLUS_W, TAB_H);
}

int TabStrip::tabAt(const wxPoint& p) const {
	for (size_t i = 0; i < tabs.size(); i++) if (tabs[i].rect.Contains(p)) return (int)i;
	return -1;
}

void TabStrip::OnPaint(wxPaintEvent&) {
	wxAutoBufferedPaintDC dc(this);
	dc.SetBackground(wxBrush(barColour()));
	dc.Clear();
	std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
	if (!gc) return;
	gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

	const wxColour accent = accentColour(), ink = inkColour();
	const wxSize barSize = GetClientSize();
	// In a split, the side you are working in is the one with the accent rail
	// under its tabs; the other side reads as quiet.
	const bool split = frame->PaneCount() > 1;
	const bool activePane = !split || frame->PaneIndexOf(frame->CurrentCanvas()) == pane;

	// Glass: a gentle top-to-bottom gradient with a bright hairline along the
	// very top, so the strip catches light like a real surface instead of
	// sitting there as a flat grey block.
	{
		const wxColour top = isDark() ? wxColour(38, 41, 48) : wxColour(250, 250, 252);
		const wxColour bottom = barColour();
		gc->SetBrush(gc->CreateLinearGradientBrush(0, 0, 0, barSize.y, top, bottom));
		gc->SetPen(*wxTRANSPARENT_PEN);
		gc->DrawRectangle(0, 0, barSize.x, barSize.y);
		gc->SetPen(wxPen(withAlpha(isDark() ? *wxWHITE : *wxWHITE, isDark() ? 0.07 : 0.9), 1));
		gc->StrokeLine(0, 0.5, barSize.x, 0.5);
	}
	GUICanvas* current = frame->CurrentCanvas();

	// The gap a dragged tab would drop into.
	if (dragging && dropAt >= 0 && hintSide == 0) {
		const int x = dropAt < (int)tabs.size() ? tabs[dropAt].rect.x - TAB_GAP / 2
		                                        : plusRect.x - TAB_GAP / 2;
		gc->SetBrush(wxBrush(withAlpha(accent, 0.7)));
		gc->SetPen(*wxTRANSPARENT_PEN);
		gc->DrawRoundedRectangle(x - 1, 5, 2.5, BarHeight() - 10, 1.25);
	}

	for (size_t i = 0; i < tabs.size(); i++) {
		const Tab& t = tabs[i];
		wxRect r = t.rect;
		if (dragging && (int)i == dragTab) r.x += dragDx;
		const bool active = (t.canvas == current);
		const bool inSplit = false;   // panes have their own strips now
		const bool hot = ((int)i == hover);

		if (active) {
			// A soft drop shadow, then the card.
			gc->SetPen(*wxTRANSPARENT_PEN);
			gc->SetBrush(wxBrush(withAlpha(*wxBLACK, isDark() ? 0.26 : 0.10)));
			gc->DrawRoundedRectangle(r.x + 0.5, r.y + 1.5, r.width - 1, r.height, 8);
			const wxColour card = cardColour();
			const wxColour lit = isDark() ? wxColour(44, 48, 56) : *wxWHITE;
			gc->SetBrush(gc->CreateLinearGradientBrush(0, r.y, 0, r.GetBottom(), lit, card));
			gc->SetPen(wxPen(withAlpha(ink, isDark() ? 0.18 : 0.10), 1));
			gc->DrawRoundedRectangle(r.x + 0.5, r.y + 0.5, r.width - 1, r.height - 1, 8);
			gc->SetPen(wxPen(withAlpha(*wxWHITE, isDark() ? 0.10 : 0.85), 1));
			gc->StrokeLine(r.x + 8, r.y + 1.0, r.GetRight() - 8, r.y + 1.0);
		} else if (hot) {
			gc->SetBrush(wxBrush(withAlpha(ink, 0.06)));
			gc->SetPen(*wxTRANSPARENT_PEN);
			gc->DrawRoundedRectangle(r.x, r.y, r.width, r.height, 8);
		}

		// A hairline between quiet tabs, the way a browser separates them --
		// but not next to a card or a highlight, which already have an edge.
		if (!active && !hot && i + 1 < tabs.size()) {
			const bool nextLoud = (tabs[i + 1].canvas == current) || ((int)(i + 1) == hover);
			if (!nextLoud) {
				gc->SetPen(wxPen(withAlpha(ink, 0.14), 1));
				gc->StrokeLine(r.GetRight() + TAB_GAP / 2.0, r.y + 7,
				               r.GetRight() + TAB_GAP / 2.0, r.GetBottom() - 6);
			}
		}

		// The accent belongs to the tab's own mark, not its whole background:
		// a small dot, filled while this tab is the one you are working in.
		const double dx = r.x + 12, dy = r.y + r.height / 2.0;
		gc->SetPen(*wxTRANSPARENT_PEN);
		gc->SetBrush(wxBrush(active ? accent : (inSplit ? withAlpha(accent, 0.55)
		                                                : withAlpha(ink, 0.28))));
		gc->DrawEllipse(dx - 3, dy - 3, 6, 6);

		// The label, clipped to leave room for the close button.
		gc->SetFont(wxFont(wxFontInfo(11.5).Bold(active)), active ? ink : withAlpha(ink, 0.7));
		wxString label = t.label;
		double tw, th;
		gc->GetTextExtent(label, &tw, &th);
		const double room = r.width - 26 - 24;
		while (tw > room && label.length() > 1) {
			label = label.Left(label.length() - 2) + wxString::FromUTF8("\u2026");
			gc->GetTextExtent(label, &tw, &th);
		}
		gc->DrawText(label, r.x + 24, r.y + (r.height - th) / 2);

		// The close cross, once there is more than one tab to close.
		if (tabs.size() > 1 && (active || hot)) {
			const wxRect c(r.x + r.width - 24, t.close.y, 16, 16);
			if ((int)i == hoverClose) {
				gc->SetBrush(wxBrush(withAlpha(ink, 0.16)));
				gc->SetPen(*wxTRANSPARENT_PEN);
				gc->DrawRoundedRectangle(c.x - 2, c.y - 2, 20, 20, 6);
			}
			// wxGraphicsPenInfo, not wxPen: a wxPen's width is a whole number,
			// so 1.3 quietly became 1.
			gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(withAlpha(ink, 0.7)).Width(1.3)));
			gc->StrokeLine(c.x + 4, c.y + 4, c.x + 12, c.y + 12);
			gc->StrokeLine(c.x + 12, c.y + 4, c.x + 4, c.y + 12);
		}
	}

	// New tab.
	const bool plusHot = (hover == -2);
	if (plusHot) {
		gc->SetBrush(wxBrush(withAlpha(ink, 0.08)));
		gc->SetPen(*wxTRANSPARENT_PEN);
		gc->DrawRoundedRectangle(plusRect.x, plusRect.y, plusRect.width, plusRect.height, 9);
	}
	gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(withAlpha(ink, 0.75)).Width(1.6)));
	const double cx = plusRect.x + plusRect.width / 2.0, cy = plusRect.y + plusRect.height / 2.0;
	gc->StrokeLine(cx - 5, cy, cx + 5, cy);
	gc->StrokeLine(cx, cy - 5, cx, cy + 5);

	// A hairline under the strip, so the tabs sit on the canvas rather than
	// float -- or, in a split, an accent rail marking the side in use.
	if (split && activePane) {
		gc->SetBrush(wxBrush(accent));
		gc->SetPen(*wxTRANSPARENT_PEN);
		gc->DrawRectangle(0, BarHeight() - 2, barSize.x, 2);
	} else {
		gc->SetPen(wxPen(withAlpha(ink, 0.12), 1));
		gc->StrokeLine(0, BarHeight() - 0.5, barSize.x, BarHeight() - 0.5);
	}

	// The quiet side steps back a little, the way an unfocused window does.
	if (split && !activePane) {
		gc->SetBrush(wxBrush(withAlpha(barColour(), 0.45)));
		gc->SetPen(*wxTRANSPARENT_PEN);
		gc->DrawRectangle(0, 0, barSize.x, barSize.y - 1);
	}
}

void TabStrip::OnDown(wxMouseEvent& e) {
	const wxPoint p = e.GetPosition();
	downOnTab = nullptr;
	downOnEmpty = false;
	if (plusRect.Contains(p)) { frame->NewTabInPane(pane); return; }
	const int i = tabAt(p);
	if (i < 0) { downOnEmpty = true; return; }
	if (tabs.size() > 1 && tabs[i].close.Contains(p)) {
		frame->CloseTabCanvas(tabs[i].canvas);
		return;
	}
	GUICanvas* canvas = tabs[i].canvas;
	downOnTab = canvas;
	frame->SelectCanvas(canvas);
	// The canvas keeps the keyboard, not the strip: clicking a tab should not
	// cost you the arrow keys, Delete, or anything else the canvas handles.
	canvas->SetFocus();
	// Selecting can rebuild the strip; find the tab again rather than trust i.
	dragTab = -1;
	for (size_t k = 0; k < tabs.size(); k++) if (tabs[k].canvas == canvas) dragTab = (int)k;
	if (dragTab < 0) return;
	dragStart = p;
	dragDx = 0;
	dropAt = -1;
	if (!HasCapture()) CaptureMouse();
}

void TabStrip::OnMotion(wxMouseEvent& e) {
	const wxPoint p = e.GetPosition();

	if (dragTab < 0) {
		const int was = hover, wasClose = hoverClose;
		hover = plusRect.Contains(p) ? -2 : tabAt(p);
		hoverClose = (hover >= 0 && tabs[hover].close.Contains(p)) ? hover : -1;
		if (hover != was || hoverClose != wasClose) Refresh();
		return;
	}

	if (dragTab >= (int)tabs.size()) { endDrag(true); return; }
	dragDx = p.x - dragStart.x;
	if (!dragging && (std::abs(dragDx) > 6 || std::abs(p.y - dragStart.y) > 8)) dragging = true;
	if (!dragging) return;

	// Out of this strip: either onto the other pane, or down over a canvas to
	// split it off.
	const wxPoint screen = ClientToScreen(p);
	const int overPane = dropPane(screen);
	const int side = dropSide(screen);
	const int wasPane = hintPane;
	hintPane = (overPane >= 0 && overPane != pane) ? overPane : -1;
	if (side != hintSide || hintPane != wasPane) { hintSide = side; showHint(side); }

	if (side == 0 && hintPane < 0) {
		// Sideways: find the slot this tab would land in.
		const int cx = tabs[dragTab].rect.x + dragDx + tabs[dragTab].rect.width / 2;
		dropAt = (int)tabs.size();
		for (size_t i = 0; i < tabs.size(); i++)
			if (cx < tabs[i].rect.GetRight()) { dropAt = (int)i; break; }
	} else {
		dropAt = -1;
	}
	Refresh();
}

int TabStrip::dropPane(const wxPoint& screenPos) const {
	return frame->PaneAtScreen(screenPos);
}

// Below a strip, over the canvas: the left or right half says which side a
// new split would open on. Only offered when there is no split yet -- once
// there are two panes, dragging between them moves the tab instead.
int TabStrip::dropSide(const wxPoint& screenPos) const {
	if (frame->PaneCount() > 1) return 0;
	wxWindow* area = GetParent();
	if (area == nullptr) return 0;
	const wxPoint origin = area->GetScreenPosition();
	const wxSize size = area->GetClientSize();
	const wxPoint p = screenPos - origin;
	if (p.y < BarHeight() + 12 || p.y > size.y || p.x < 0 || p.x > size.x) return 0;
	return p.x < size.x / 2 ? -1 : 1;
}

void TabStrip::showHint(int side) {
	// Moving to the other pane: light up that whole pane instead of a half.
	if (hintPane >= 0) {
		const wxRect r = frame->PaneScreenRect(hintPane);
		if (r.width > 0) {
			if (hint == nullptr) hint = new DropHint(frame);
			hint->SetLabel2("Drop to move here");
			hint->SetSize(r);
			hint->Show();
			hint->Raise();
		}
		return;
	}
	if (side == 0) {
		if (hint) { hint->Hide(); }
		return;
	}
	if (hint) hint->SetLabel2("Drop to split here");
	wxWindow* area = GetParent();
	if (area == nullptr) return;
	if (hint == nullptr) hint = new DropHint(frame);
	const wxPoint origin = area->GetScreenPosition();
	const wxSize size = area->GetClientSize();
	const int top = origin.y + BarHeight();
	const int h = size.y - BarHeight();
	const int w = size.x / 2;
	hint->SetSize(side < 0 ? origin.x : origin.x + w, top, w, h);
	hint->Show();
	hint->Raise();
}

void TabStrip::OnUp(wxMouseEvent& e) {
	if (HasCapture()) ReleaseMouse();
	if (dragTab < 0 || dragTab >= (int)tabs.size()) { endDrag(true); return; }
	const int tab = dragTab, side = hintSide, at = dropAt, otherPane = hintPane;
	const bool wasDragging = dragging;
	GUICanvas* canvas = tabs[tab].canvas;
	endDrag(false);

	if (!wasDragging) return;
	if (otherPane >= 0) {                       // into the other side of the split
		// Moving this pane's last tab away closes the pane, and this strip
		// with it -- so not from inside the strip's own mouse handler.
		MainFrame* f = frame;
		f->CallAfter([f, canvas, otherPane] { f->MoveCanvasToPane(canvas, otherPane, -1); });
		return;
	}
	if (side != 0) {                            // out over the canvas: split here
		frame->SplitWith(canvas, /*onRight=*/side > 0);
		return;
	}
	if (at >= 0 && at != tab && at != tab + 1)
		frame->MoveCanvasToPane(canvas, pane, at > tab ? at - 1 : at);
}

void TabStrip::endDrag(bool) {
	dragTab = -1;
	dragging = false;
	dragDx = 0;
	dropAt = -1;
	hintSide = 0;
	hintPane = -1;
	if (hint) hint->Hide();
	Refresh();
}

void TabStrip::OnLeave(wxMouseEvent& e) {
	if (dragTab < 0 && (hover != -1 || hoverClose != -1)) {
		hover = hoverClose = -1;
		Refresh();
	}
	e.Skip();
}

void TabStrip::OnCaptureLost(wxMouseCaptureLostEvent&) { endDrag(true); }
