/*****************************************************************************
   Project: CEDAR Logic Simulator
   Welcome: the first-run flow, the setup walkthrough, and the guided tour.

   The welcome is five pages drawn by hand -- hello, make it yours, your
   name, the keys worth knowing, and what to do first -- sliding from one to
   the next. The tour is a small card that floats over the window and
   watches what you do: it moves on by itself when a step is done, so there
   is nothing to click but the circuit.
*****************************************************************************/

#include "Welcome.h"
#include "MainFrame.h"
#include "MainApp.h"
#include "Settings.h"
#include "RenderMode.h"
#include "GUICanvas.h"
#include "guiGate.h"
#include "guiWire.h"
#include "UiKit.h"

#include <wx/dialog.h>
#include <wx/frame.h>
#include <wx/panel.h>
#include <wx/dcbuffer.h>
#include <wx/dcmemory.h>
#include <wx/graphics.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/settings.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <functional>
#include <vector>

DECLARE_APP(MainApp)

namespace {

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point t) {
	return std::chrono::duration<double>(Clock::now() - t).count();
}

// Copy is written in UTF-8 (it has real dashes); read it as UTF-8 everywhere
// rather than through the platform's narrow-string encoding.
wxString U(const char* s) { return wxString::FromUTF8(s); }

double easeOut(double t) {
	t = std::max(0.0, std::min(1.0, t));
	return 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);
}

// ------------------------------------------------------------------ hero --

// A point `d` of the way along a polyline, by length.
wxPoint2DDouble alongPath(const std::vector<wxPoint2DDouble>& pts, double d) {
	for (size_t i = 1; i < pts.size(); i++) {
		const double seg = std::hypot(pts[i].m_x - pts[i - 1].m_x, pts[i].m_y - pts[i - 1].m_y);
		if (d <= seg || i + 1 == pts.size()) {
			const double t = seg > 0 ? std::min(1.0, d / seg) : 0.0;
			return wxPoint2DDouble(pts[i - 1].m_x + (pts[i].m_x - pts[i - 1].m_x) * t,
			                       pts[i - 1].m_y + (pts[i].m_y - pts[i - 1].m_y) * t);
		}
		d -= seg;
	}
	return pts.empty() ? wxPoint2DDouble() : pts.back();
}

double pathLength(const std::vector<wxPoint2DDouble>& pts) {
	double n = 0;
	for (size_t i = 1; i < pts.size(); i++)
		n += std::hypot(pts[i].m_x - pts[i - 1].m_x, pts[i].m_y - pts[i - 1].m_y);
	return n;
}

// The app's own subject, alive: two switches stepping through 00, 01, 10,
// 11 into an AND gate, and a lamp that lights only for the last. Wires that
// carry a 1 glow in the accent with signals running along them.
void drawHero(wxGraphicsContext* gc, const wxRect& r, double t) {
	const wxColour accent = ui::accent(), ink = ui::ink();
	const wxColour lampOn(255, 196, 64);
	const double cx = r.x + r.width / 2.0, cy = r.y + r.height / 2.0;
	const double k = std::min(r.width / 520.0, r.height / 200.0);

	const int state = (int)(t / 1.3) % 4;
	const bool a = (state & 2) != 0, b = (state & 1) != 0, out = a && b;

	const double bodyL = cx - 30 * k, bodyR = cx + 30 * k, hh = 44 * k;
	const double nose = bodyR + 44 * k;

	struct Wire { std::vector<wxPoint2DDouble> pts; bool on; };
	std::vector<Wire> wires = {
		{ { { cx - 188 * k, cy - 50 * k }, { cx - 100 * k, cy - 50 * k },
		    { cx - 100 * k, cy - 24 * k }, { bodyL, cy - 24 * k } }, a },
		{ { { cx - 188 * k, cy + 50 * k }, { cx - 100 * k, cy + 50 * k },
		    { cx - 100 * k, cy + 24 * k }, { bodyL, cy + 24 * k } }, b },
		{ { { nose, cy }, { cx + 150 * k, cy } }, out },
	};
	for (const Wire& w : wires) {
		wxGraphicsPath p = gc->CreatePath();
		p.MoveToPoint(w.pts[0]);
		for (size_t i = 1; i < w.pts.size(); i++) p.AddLineToPoint(w.pts[i]);
		if (w.on) {   // a soft glow under a lit wire
			gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(ui::withAlpha(accent, 0.18)).Width(9 * k)));
			gc->StrokePath(p);
		}
		gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(w.on ? accent : ui::withAlpha(ink, 0.32)).Width(3 * k)));
		gc->StrokePath(p);
		if (!w.on) continue;
		const double len = pathLength(w.pts), spacing = 34 * k;
		const double phase = std::fmod(t * 70 * k, spacing);
		gc->SetPen(*wxTRANSPARENT_PEN);
		gc->SetBrush(wxBrush(*wxWHITE));
		for (double d = phase; d < len; d += spacing) {
			const wxPoint2DDouble q = alongPath(w.pts, d);
			gc->DrawEllipse(q.m_x - 2.6 * k, q.m_y - 2.6 * k, 5.2 * k, 5.2 * k);
		}
	}

	// The switches: a key-like tile showing its bit.
	auto drawSwitch = [&](double y, bool on) {
		const wxRect2DDouble s(cx - 232 * k, y - 18 * k, 44 * k, 36 * k);
		gc->SetPen(wxPen(on ? accent : ui::withAlpha(ink, 0.35), 2));
		gc->SetBrush(wxBrush(on ? ui::withAlpha(accent, 0.22) : ui::withAlpha(ink, 0.05)));
		gc->DrawRoundedRectangle(s.m_x, s.m_y, s.m_width, s.m_height, 8 * k);
		gc->SetFont(wxFont(wxFontInfo(std::max(8.0, 15 * k)).Bold()), on ? accent : ui::dim());
		double tw, th;
		gc->GetTextExtent(on ? "1" : "0", &tw, &th);
		gc->DrawText(on ? "1" : "0", s.m_x + (s.m_width - tw) / 2, s.m_y + (s.m_height - th) / 2);
	};
	drawSwitch(cy - 50 * k, a);
	drawSwitch(cy + 50 * k, b);

	// The gate: flat back, round nose, labelled.
	wxGraphicsPath body = gc->CreatePath();
	body.MoveToPoint(bodyL, cy - hh);
	body.AddLineToPoint(bodyR, cy - hh);
	body.AddCurveToPoint(bodyR + 60 * k, cy - hh, bodyR + 60 * k, cy + hh, bodyR, cy + hh);
	body.AddLineToPoint(bodyL, cy + hh);
	body.CloseSubpath();
	gc->SetBrush(wxBrush(ui::withAlpha(accent, 0.12)));
	gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(accent).Width(3.2 * k)));
	gc->DrawPath(body);
	gc->SetFont(wxFont(wxFontInfo(std::max(8.0, 13 * k)).Bold()), ui::withAlpha(accent, 0.9));
	double tw, th;
	gc->GetTextExtent("AND", &tw, &th);
	gc->DrawText("AND", cx + 8 * k - tw / 2, cy - th / 2);

	// The lamp, with a glow when lit.
	const double lx = cx + 172 * k, lr = 22 * k;
	if (out) {
		for (int i = 3; i >= 1; i--) {
			gc->SetPen(*wxTRANSPARENT_PEN);
			gc->SetBrush(wxBrush(ui::withAlpha(lampOn, 0.10 * i)));
			const double gr = lr + (4 - i) * 9 * k;
			gc->DrawEllipse(lx - gr, cy - gr, gr * 2, gr * 2);
		}
	}
	gc->SetBrush(wxBrush(out ? lampOn : ui::withAlpha(ink, 0.06)));
	gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(out ? wxColour(230, 160, 20) : ui::withAlpha(ink, 0.35)).Width(2.5 * k)));
	gc->DrawEllipse(lx - lr, cy - lr, lr * 2, lr * 2);
}

// ------------------------------------------------------------- welcome ----

struct Hit {
	wxRect rect;
	int id = 0;
};

enum {
	H_NEXT = 1, H_BACK, H_SKIP, H_TOUR, H_BLANK, H_SHORTCUTS,
	H_DOT = 50,         // + page
	H_THEME = 100,      // + 0 system, 1 light, 2 dark
	H_ACCENT = 200,     // + index
	H_TABS = 300,       // + 0 modern, 1 classic
	H_TOOLBAR = 400     // + style
};

enum Page { P_HELLO, P_LOOK, P_NAME, P_KEYS, P_READY, PAGE_COUNT };

struct KeyCard { const char* key; const char* title; const char* line; };
const KeyCard KEY_CARDS[] = {
	{ "A", "Add a gate", "Type part of its name, press Return, click to drop it." },
	{ "C", "Copy, or connect", "Copies the selection. Held while dragging a gate, it wires it to pins nearby." },
	{ "D", "Duplicate", "Copies the selection and puts the copy on your mouse." },
	{ "R", "Rotate", "Turns the selected gates a quarter turn." },
	{ "S", "Straighten", "Tidies the selected wires into clean routes." },
	{ "T", "Truth table", "Tries every switch combination and writes down the lights." },
};

class WelcomeWindow : public wxDialog {
public:
	WelcomeWindow(MainFrame* frame, bool startAtSetup)
		: wxDialog(frame, wxID_ANY, "Welcome to CedarLogic", wxDefaultPosition,
		           wxDefaultSize, wxDEFAULT_DIALOG_STYLE), frame(frame) {
		page = startAtSetup ? P_LOOK : P_HELLO;
		shownFrom = page;
		SetClientSize(W, H);
		SetBackgroundColour(ui::paper());
		face = new wxPanel(this, wxID_ANY, wxPoint(0, 0), wxSize(W, H));
		face->SetBackgroundStyle(wxBG_STYLE_PAINT);
		face->Bind(wxEVT_PAINT, &WelcomeWindow::OnPaint, this);
		face->Bind(wxEVT_LEFT_DOWN, &WelcomeWindow::OnDown, this);
		face->Bind(wxEVT_MOTION, &WelcomeWindow::OnMotion, this);
		face->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { hot = 0; face->Refresh(); });

		name = new wxTextCtrl(face, wxID_ANY,
			wxString::FromUTF8(appConfig().appSettings.studentName.c_str()),
			wxPoint(60, 212), wxSize(380, -1));
		name->SetHint("Your full name");
		name->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { face->Refresh(); });
		name->Hide();

		CentreOnParent();
		started = Clock::now();
		beat.Bind(wxEVT_TIMER, [this](wxTimerEvent&) { face->Refresh(); });
		beat.Start(30);

		Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { finish(Ending::Close); });
		Bind(wxEVT_CHAR_HOOK, &WelcomeWindow::OnKey, this);
		layoutPage();
	}

private:
	static const int W = 780, H = 580;
	enum class Ending { Close, Tour, Blank, Shortcuts };

	// --- moving between pages ------------------------------------------------

	void go(int to) {
		to = std::max(0, std::min((int)PAGE_COUNT - 1, to));
		if (to == page) return;
		saveName();
		shownFrom = page;
		page = to;
		pageChanged = Clock::now();
		name->Hide();   // back once the slide has settled
		layoutPage();
		face->Refresh();
	}

	double slide() const { return easeOut(secondsSince(pageChanged) / 0.32); }

	void layoutPage() {
		if (page != P_NAME) { name->Hide(); return; }
		// Shown when the page has slid into place; see OnPaint.
	}

	void saveName() {
		appConfig().appSettings.studentName =
			std::string(name->GetValue().Strip(wxString::both).ToUTF8());
	}

	void finish(Ending how) {
		if (closing) return;
		closing = true;
		saveName();
		beat.Stop();
		appConfig().appSettings.hasSeenWelcome = true;
		MainFrame* f = frame;
		if (f) f->ApplyPreferences();
		EndModal(wxID_OK);
		// This window is a local of ShowWelcome and gone by the time these run,
		// so they capture only the frame.
		if (!f) return;
		if (how == Ending::Tour) f->CallAfter([f] { StartTutorial(f); });
		if (how == Ending::Shortcuts) f->CallAfter([f] {
			wxCommandEvent evt(wxEVT_MENU, Help_KeyboardShortcuts);
			f->ProcessWindowEvent(evt);
		});
	}

	// --- painting ------------------------------------------------------------

	void OnPaint(wxPaintEvent&) {
		wxAutoBufferedPaintDC dc(face);
		paintAll(dc, true);
	}

public:
	// Test hook: one page, settled, as a picture.
	wxBitmap snapshot(int which) {
		page = shownFrom = which;
		pageChanged = Clock::now() - std::chrono::seconds(5);
		started = Clock::now() - std::chrono::milliseconds(4100);   // the lamp lit
		wxBitmap bmp(W, H, 24);
		{
			wxMemoryDC dc(bmp);
			paintAll(dc, false);
		}
		return bmp;
	}

private:
	void paintAll(wxDC& dc, bool live) {
		dc.SetBackground(wxBrush(ui::paper()));
		dc.Clear();
		std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
		if (!gc) return;
		gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

		const double p = slide();
		const double t = secondsSince(started);
		const int dir = page > shownFrom ? 1 : -1;

		// The page on its way out, then the one coming in; only the incoming
		// one's controls can be clicked.
		if (p < 1.0 && shownFrom != page) {
			hits.clear();
			gc->PushState();
			gc->Translate(-dir * 60 * p, 0);
			gc->BeginLayer(1.0 - p);
			paintPage(gc.get(), shownFrom, t);
			gc->EndLayer();
			gc->PopState();
		}
		hits.clear();
		gc->PushState();
		if (p < 1.0 && shownFrom != page) {
			gc->Translate(dir * 60 * (1.0 - p), 0);
			gc->BeginLayer(p);
		}
		paintPage(gc.get(), page, t);
		if (p < 1.0 && shownFrom != page) gc->EndLayer();
		gc->PopState();

		paintFooter(gc.get());

		if (!live) return;
		// The name field appears once its page has arrived.
		const bool wantName = page == P_NAME && p >= 1.0;
		if (wantName != name->IsShown()) {
			name->Show(wantName);
			if (wantName) { name->SetFocus(); name->SetInsertionPointEnd(); }
		}
	}

	void paintPage(wxGraphicsContext* gc, int which, double t) {
		switch (which) {
			case P_HELLO: paintHello(gc, t); break;
			case P_LOOK:  paintLook(gc); break;
			case P_NAME:  paintName(gc); break;
			case P_KEYS:  paintKeys(gc, t); break;
			default:      paintReady(gc, t); break;
		}
	}

	void heading(wxGraphicsContext* gc, const wxString& eyebrow, const wxString& title,
	             const char* subUtf8, double y) {
		const wxString sub = U(subUtf8);
		gc->SetFont(wxFont(wxFontInfo(11).Bold()), ui::accent());
		gc->DrawText(eyebrow.Upper(), 60, y);
		gc->SetFont(wxFont(wxFontInfo(27).Bold()), ui::ink());
		gc->DrawText(title, 60, y + 20);
		gc->SetFont(wxFont(wxFontInfo(13.5)), ui::dim());
		double yy = y + 62;
		for (const wxString& line : ui::wrap(gc, sub, W - 120)) {
			gc->DrawText(line, 60, yy);
			yy += 20;
		}
	}

	void paintHello(wxGraphicsContext* gc, double t) {
		const wxColour accent = ui::accent();
		gc->SetPen(*wxTRANSPARENT_PEN);
		gc->SetBrush(gc->CreateLinearGradientBrush(0, 0, 0, 280, ui::withAlpha(accent, 0.16),
		                                           ui::withAlpha(accent, 0.0)));
		gc->DrawRectangle(0, 0, W, 280);
		drawHero(gc, wxRect(40, 34, W - 80, 210), t);

		heading(gc, "CedarLogic", "Build it. Watch it think.",
			"Design logic circuits, run them live, and hand them in \u2014 in the time it takes "
			"to sketch one on paper.", 262);

		struct Point { const char* title; const char* line; };
		const Point points[3] = {
			{ "It keeps itself", "Saves every few seconds, with versions to go back to." },
			{ "It shows its work", "Live wires, a truth table on one key, and an oscilloscope." },
			{ "It stays out of the way", "Nearly everything has a key. You won't need the menus." },
		};
		for (int i = 0; i < 3; i++) {
			const double x = 60 + i * 226, y = 372;
			gc->SetPen(*wxTRANSPARENT_PEN);
			gc->SetBrush(wxBrush(ui::withAlpha(ui::ink(), 0.045)));
			gc->DrawRoundedRectangle(x, y, 210, 96, 14);
			gc->SetBrush(wxBrush(accent));
			gc->DrawEllipse(x + 16, y + 20, 8, 8);
			gc->SetFont(wxFont(wxFontInfo(13).Bold()), ui::ink());
			gc->DrawText(points[i].title, x + 32, y + 14);
			gc->SetFont(wxFont(wxFontInfo(11.5)), ui::dim());
			double yy = y + 40;
			for (const wxString& line : ui::wrap(gc, U(points[i].line), 178)) {
				gc->DrawText(line, x + 16, yy);
				yy += 17;
			}
		}
	}

	// A row of choices drawn as one segmented pill.
	void segmented(wxGraphicsContext* gc, double x, double y, double w,
	               const std::vector<wxString>& labels, int chosen, int idBase) {
		const wxColour accent = ui::accent(), ink = ui::ink();
		const double h = 34, seg = w / labels.size();
		gc->SetBrush(wxBrush(ui::withAlpha(ink, 0.06)));
		gc->SetPen(wxPen(ui::withAlpha(ink, 0.12), 1));
		gc->DrawRoundedRectangle(x + 0.5, y + 0.5, w - 1, h - 1, 9);
		for (size_t i = 0; i < labels.size(); i++) {
			const wxRect r((int)(x + i * seg), (int)y, (int)seg, (int)h);
			const bool on = ((int)i == chosen);
			if (on) {
				gc->SetBrush(wxBrush(ui::isDark() ? wxColour(58, 63, 74) : *wxWHITE));
				gc->SetPen(wxPen(ui::withAlpha(accent, 0.7), 1));
				gc->DrawRoundedRectangle(r.x + 2.5, r.y + 2.5, r.width - 5, r.height - 5, 7);
			} else if (hot == idBase + (int)i) {
				gc->SetBrush(wxBrush(ui::withAlpha(ink, 0.06)));
				gc->SetPen(*wxTRANSPARENT_PEN);
				gc->DrawRoundedRectangle(r.x + 2.5, r.y + 2.5, r.width - 5, r.height - 5, 7);
			}
			gc->SetFont(wxFont(wxFontInfo(12).Bold(on)), on ? ink : ui::withAlpha(ink, 0.7));
			double tw, th;
			gc->GetTextExtent(labels[i], &tw, &th);
			gc->DrawText(labels[i], r.x + (r.width - tw) / 2, r.y + (r.height - th) / 2);
			hits.push_back({ r, idBase + (int)i });
		}
	}

	void label(wxGraphicsContext* gc, const wxString& text, double x, double y) {
		gc->SetFont(wxFont(wxFontInfo(11).Bold()), ui::withAlpha(ui::ink(), 0.55));
		gc->DrawText(text.Upper(), x, y);
	}

	// A small picture of the canvas as it will look: the theme's paper, the
	// grid, a selected gate haloed in the accent, a lit wire and the tabs.
	void paintPreview(wxGraphicsContext* gc, const wxRect& r) {
		const auto& s = appConfig().appSettings;
		const bool dark = ui::isDark();
		const wxColour accent = ui::accent();
		const wxColour canvas = dark ? wxColour(19, 21, 25) : *wxWHITE;
		const wxColour grid = dark ? wxColour(40, 44, 52) : wxColour(226, 229, 236);
		const wxColour line = dark ? wxColour(200, 206, 216) : wxColour(30, 33, 40);

		gc->SetPen(*wxTRANSPARENT_PEN);
		gc->SetBrush(wxBrush(ui::withAlpha(*wxBLACK, dark ? 0.35 : 0.10)));
		gc->DrawRoundedRectangle(r.x + 2, r.y + 5, r.width, r.height, 16);
		gc->SetBrush(wxBrush(canvas));
		gc->SetPen(wxPen(ui::withAlpha(ui::ink(), 0.15), 1));
		gc->DrawRoundedRectangle(r.x + 0.5, r.y + 0.5, r.width - 1, r.height - 1, 16);

		// The tab strip.
		const double stripH = 30;
		gc->SetPen(*wxTRANSPARENT_PEN);
		gc->SetBrush(wxBrush(dark ? wxColour(22, 24, 28) : wxColour(233, 234, 238)));
		wxGraphicsPath top = gc->CreatePath();
		top.AddRoundedRectangle(r.x + 1, r.y + 1, r.width - 2, stripH + 14, 15);
		gc->DrawPath(top);
		gc->SetBrush(wxBrush(canvas));
		gc->DrawRectangle(r.x + 1, r.y + stripH, r.width - 2, 16);
		if (s.classicTabs) {
			gc->SetBrush(wxBrush(ui::withAlpha(ui::ink(), 0.10)));
			gc->DrawRoundedRectangle(r.x + r.width / 2.0 - 70, r.y + 7, 140, 18, 5);
		} else {
			gc->SetBrush(wxBrush(canvas));
			gc->DrawRoundedRectangle(r.x + 10, r.y + 5, 96, 22, 6);
			gc->SetBrush(wxBrush(accent));
			gc->DrawEllipse(r.x + 19, r.y + 13, 6, 6);
			gc->SetFont(wxFont(wxFontInfo(9.5).Bold()), ui::ink());
			gc->DrawText("Page 1", r.x + 31, r.y + 9);
			gc->SetFont(wxFont(wxFontInfo(9.5)), ui::dim());
			gc->DrawText("Page 2", r.x + 122, r.y + 9);
		}

		// Grid, clipped to the canvas area. Scoped with Push/PopState, not
		// Clip()/ResetClip(): this paints nested inside paintAll's own
		// PushState() (for the slide transition), and wx's macOS backend
		// asserts on ResetClip() while a PushState() is active -- ResetClip()
		// clears back to the DC's original clip, not to whatever was current
		// before this Clip(), so it can't nest. PushState()/PopState() saves
		// and restores exactly that, and nests however deep it needs to.
		gc->PushState();
		gc->Clip(r.x + 1, r.y + stripH, r.width - 2, r.height - stripH - 2);
		const double step = 14;
		if (s.gridlineVisible) {
			if (s.gridStyle == 1) {
				gc->SetPen(*wxTRANSPARENT_PEN);
				gc->SetBrush(wxBrush(grid));
				for (double x = r.x + 7; x < r.GetRight(); x += step)
					for (double y = r.y + stripH + 7; y < r.GetBottom(); y += step)
						gc->DrawEllipse(x - 1, y - 1, 2, 2);
			} else {
				gc->SetPen(wxPen(grid, 1));
				for (double x = r.x + 7; x < r.GetRight(); x += step) gc->StrokeLine(x, r.y + stripH, x, r.GetBottom());
				for (double y = r.y + stripH + 7; y < r.GetBottom(); y += step) gc->StrokeLine(r.x, y, r.GetRight(), y);
			}
		}

		// A gate, selected, with one lit input.
		const double gx = r.x + r.width / 2.0 + 6, gy = r.y + stripH + (r.height - stripH) / 2.0;
		wxGraphicsPath body = gc->CreatePath();
		body.MoveToPoint(gx - 24, gy - 26);
		body.AddLineToPoint(gx + 6, gy - 26);
		body.AddCurveToPoint(gx + 40, gy - 26, gx + 40, gy + 26, gx + 6, gy + 26);
		body.AddLineToPoint(gx - 24, gy + 26);
		body.CloseSubpath();
		gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(ui::withAlpha(accent, 0.35)).Width(9)));
		gc->StrokePath(body);
		gc->SetBrush(*wxTRANSPARENT_BRUSH);
		gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(line).Width(2)));
		gc->DrawPath(body);
		gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(wxColour(222, 52, 52)).Width(2)));
		gc->StrokeLine(r.x + 20, gy - 13, gx - 24, gy - 13);
		gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(line).Width(2)));
		gc->StrokeLine(r.x + 20, gy + 13, gx - 24, gy + 13);
		gc->StrokeLine(gx + 32, gy, r.GetRight() - 20, gy);
		gc->PopState();
	}

	void paintLook(wxGraphicsContext* gc) {
		auto& s = appConfig().appSettings;
		heading(gc, "Make it yours", "Your canvas, your way",
			"It all applies as you pick it \u2014 the window behind this one is the preview.", 40);

		label(gc, "Appearance", 60, 150);
		segmented(gc, 60, 170, 300, { "System", "Light", "Dark" },
		          s.themeMode > 2 ? 0 : s.themeMode, H_THEME);

		label(gc, "Accent colour", 60, 224);
		for (int i = 0; i < 6; i++) {
			const wxRect r(60 + i * 46, 244, 36, 36);
			const wxColour c = ui::accentFor(i);
			gc->SetPen(*wxTRANSPARENT_PEN);
			gc->SetBrush(wxBrush(c));
			gc->DrawEllipse(r.x + 4, r.y + 4, 28, 28);
			if (s.accentColor == i) {
				gc->SetBrush(*wxTRANSPARENT_BRUSH);
				gc->SetPen(wxPen(ui::withAlpha(ui::ink(), 0.8), 2));
				gc->DrawEllipse(r.x, r.y, 36, 36);
			} else if (hot == H_ACCENT + i) {
				gc->SetBrush(*wxTRANSPARENT_BRUSH);
				gc->SetPen(wxPen(ui::withAlpha(ui::ink(), 0.3), 2));
				gc->DrawEllipse(r.x, r.y, 36, 36);
			}
			hits.push_back({ r, H_ACCENT + i });
		}

		label(gc, "Toolbar", 60, 304);
		segmented(gc, 60, 324, 360, { "Classic", "Segmented", "Minimal", "Seamless" },
		          s.toolbarStyle, H_TOOLBAR);

		label(gc, "Tabs", 60, 378);
		segmented(gc, 60, 398, 240, { "Modern", "Classic" }, s.classicTabs ? 1 : 0, H_TABS);
		gc->SetFont(wxFont(wxFontInfo(11)), ui::dim());
		gc->DrawText(s.classicTabs ? "The plain system tabs." : "Drag to reorder or split; double-click to rename.",
		             60, 440);

		paintPreview(gc, wxRect(460, 150, 262, 250));
		gc->SetFont(wxFont(wxFontInfo(11)), ui::dim());
		const wxString cap = "Preview";
		double tw, th;
		gc->GetTextExtent(cap, &tw, &th);
		gc->DrawText(cap, 460 + (262 - tw) / 2, 412);
	}

	void paintName(wxGraphicsContext* gc) {
		heading(gc, "One more thing", "Who's handing this in?",
			"Your name goes on every circuit you export or print, above the line that says "
			"whether it works \u2014 the first thing a grader looks for.", 40);

		label(gc, "Your name", 60, 188);
		// The field itself is a real text control, placed in the constructor.

		gc->SetFont(wxFont(wxFontInfo(11.5)), ui::dim());
		gc->DrawText("Rather not? Leave it blank. It's in Preferences whenever you want it.", 60, 256);

		// What the strip under an exported circuit will say.
		const wxRect page(60, 296, W - 120, 172);
		gc->SetPen(*wxTRANSPARENT_PEN);
		gc->SetBrush(wxBrush(ui::withAlpha(*wxBLACK, ui::isDark() ? 0.3 : 0.08)));
		gc->DrawRoundedRectangle(page.x + 2, page.y + 4, page.width, page.height, 12);
		gc->SetBrush(wxBrush(*wxWHITE));   // exports are always on white paper
		gc->SetPen(wxPen(wxColour(210, 212, 218), 1));
		gc->DrawRoundedRectangle(page.x + 0.5, page.y + 0.5, page.width - 1, page.height - 1, 12);
		// A hint of the circuit above the strip, in print's black on white.
		{
			const wxColour line(150, 154, 164);
			const double gx = page.x + page.width / 2.0, gy = page.y + 48;
			gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(line).Width(1.6)));
			gc->SetBrush(*wxTRANSPARENT_BRUSH);
			wxGraphicsPath g = gc->CreatePath();
			g.MoveToPoint(gx - 18, gy - 20);
			g.AddLineToPoint(gx + 4, gy - 20);
			g.AddCurveToPoint(gx + 30, gy - 20, gx + 30, gy + 20, gx + 4, gy + 20);
			g.AddLineToPoint(gx - 18, gy + 20);
			g.CloseSubpath();
			gc->DrawPath(g);
			gc->StrokeLine(gx - 90, gy - 10, gx - 18, gy - 10);
			gc->StrokeLine(gx - 90, gy + 10, gx - 18, gy + 10);
			gc->StrokeLine(gx + 24, gy, gx + 90, gy);
			gc->DrawRoundedRectangle(gx - 106, gy - 18, 16, 16, 3);
			gc->DrawRoundedRectangle(gx - 106, gy + 2, 16, 16, 3);
			gc->DrawEllipse(gx + 90, gy - 8, 16, 16);
		}
		gc->SetPen(wxPen(wxColour(210, 212, 218), 1));
		gc->StrokeLine(page.x + 20, page.y + 96, page.GetRight() - 20, page.y + 96);
		wxString who = name ? name->GetValue().Strip(wxString::both) : wxString();
		const bool empty = who.empty();
		if (empty) who = "Your Name";
		gc->SetFont(wxFont(wxFontInfo(15).Bold()), empty ? wxColour(170, 172, 180) : wxColour(20, 22, 28));
		gc->DrawText(who, page.x + 22, page.y + 108);
		gc->SetFont(wxFont(wxFontInfo(11.5)), wxColour(90, 94, 104));
		gc->DrawText("This circuit works as specified.", page.x + 22, page.y + 136);
	}

	void paintKeys(wxGraphicsContext* gc, double t) {
		heading(gc, "The fast way", "Six keys worth knowing",
			"Try them now \u2014 press any of these and watch it light up. Press ? any time "
			"for the full list.", 40);
		const wxColour accent = ui::accent(), ink = ui::ink();
		for (int i = 0; i < 6; i++) {
			const double x = 60 + (i % 3) * 226, y = 150 + (i / 3) * 154;
			const double since = pressedAt[i] ? secondsSince(pressedWhen[i]) : 9.0;
			const double glow = since < 0.9 ? 1.0 - since / 0.9 : 0.0;
			gc->SetPen(glow > 0 ? wxPen(ui::withAlpha(accent, 0.4 + 0.6 * glow), 2)
			                    : wxPen(ui::withAlpha(ink, 0.10), 1));
			gc->SetBrush(wxBrush(glow > 0 ? ui::withAlpha(accent, 0.08 + 0.18 * glow)
			                              : ui::withAlpha(ink, 0.04)));
			gc->DrawRoundedRectangle(x, y, 210, 142, 14);
			const double lift = glow * 3;
			ui::drawKeyCap(gc, x + 16, y + 16 - lift, 38, KEY_CARDS[i].key);
			if (pressedAt[i]) {   // tried it: a small tick
				gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(ui::success()).Width(2.2)));
				gc->StrokeLine(x + 184, y + 28, x + 189, y + 33);
				gc->StrokeLine(x + 189, y + 33, x + 198, y + 21);
			}
			gc->SetFont(wxFont(wxFontInfo(13).Bold()), ink);
			gc->DrawText(KEY_CARDS[i].title, x + 16, y + 66);
			gc->SetFont(wxFont(wxFontInfo(11)), ui::dim());
			double yy = y + 88;
			for (const wxString& line : ui::wrap(gc, U(KEY_CARDS[i].line), 180)) {
				gc->DrawText(line, x + 16, yy);
				yy += 15;
			}
		}
		(void)t;
	}

	void paintReady(wxGraphicsContext* gc, double t) {
		heading(gc, "Ready", "Build your first circuit",
			"About five minutes, and it moves on by itself as you go.", 40);

		struct Tile { int id; const char* title; const char* line; bool primary; };
		const Tile tiles[3] = {
			{ H_TOUR, "Take the guided tour", "Two switches, a gate and a light. Recommended.", true },
			{ H_BLANK, "Start with a blank canvas", "Jump straight in. Help > Guided Tour replays it.", false },
			{ H_SHORTCUTS, "See every shortcut", "The whole list, searchable.", false },
		};
		const wxColour accent = ui::accent(), ink = ui::ink();
		for (int i = 0; i < 3; i++) {
			const wxRect r(60, 150 + i * 96, W - 120, 82);
			const bool isHot = hot == tiles[i].id;
			gc->SetPen(tiles[i].primary ? wxPen(ui::withAlpha(accent, isHot ? 1.0 : 0.6), 2)
			                            : wxPen(ui::withAlpha(ink, isHot ? 0.25 : 0.12), 1));
			gc->SetBrush(wxBrush(tiles[i].primary ? ui::withAlpha(accent, isHot ? 0.18 : 0.10)
			                                      : ui::withAlpha(ink, isHot ? 0.07 : 0.035)));
			gc->DrawRoundedRectangle(r.x + 0.5, r.y + 0.5, r.width - 1, r.height - 1, 16);
			if (tiles[i].primary) {
				drawHero(gc, wxRect(r.GetRight() - 230, r.y + 4, 220, r.height - 8), t);
			} else {
				// An arrow, pointing on.
				gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(ui::withAlpha(ink, 0.45)).Width(2)));
				const double ax = r.GetRight() - 34, ay = r.y + r.height / 2.0;
				gc->StrokeLine(ax - 10, ay, ax + 6, ay);
				gc->StrokeLine(ax, ay - 6, ax + 6, ay);
				gc->StrokeLine(ax, ay + 6, ax + 6, ay);
			}
			gc->SetFont(wxFont(wxFontInfo(15).Bold()), ink);
			gc->DrawText(tiles[i].title, r.x + 24, r.y + 18);
			gc->SetFont(wxFont(wxFontInfo(12)), ui::dim());
			gc->DrawText(U(tiles[i].line), r.x + 24, r.y + 44);
			hits.push_back({ r, tiles[i].id });
		}
	}

	// Progress dots, Back, and the way on -- on every page.
	void paintFooter(wxGraphicsContext* gc) {
		const wxColour accent = ui::accent(), ink = ui::ink();
		const double y = H - 58;
		gc->SetPen(wxPen(ui::withAlpha(ink, 0.08), 1));
		gc->StrokeLine(0, y - 16.5, W, y - 16.5);

		for (int i = 0; i < PAGE_COUNT; i++) {
			const double w = (i == page) ? 22 : 8;
			double x = 60;
			for (int j = 0; j < i; j++) x += (j == page ? 22 : 8) + 8;
			gc->SetPen(*wxTRANSPARENT_PEN);
			gc->SetBrush(wxBrush(i == page ? accent : ui::withAlpha(ink, hot == H_DOT + i ? 0.4 : 0.2)));
			gc->DrawRoundedRectangle(x, y + 15, w, 8, 4);
			hits.push_back({ wxRect((int)x - 3, (int)y + 8, (int)w + 6, 22), H_DOT + i });
		}

		if (page != P_READY) {
			gc->SetFont(wxFont(wxFontInfo(12)), ui::withAlpha(ink, hot == H_SKIP ? 0.9 : 0.5));
			const wxString skip = "Skip";
			double tw, th;
			gc->GetTextExtent(skip, &tw, &th);
			const wxRect sr(W - 60 - (int)tw - 8, 18, (int)tw + 16, 26);
			gc->DrawText(skip, sr.x + 8, sr.y + (sr.height - th) / 2);
			hits.push_back({ sr, H_SKIP });
		}

		if (page > 0) {
			const wxRect r(W - 312, (int)y, 96, 38);
			ui::drawButton(gc, r, "Back", false, hot == H_BACK);
			hits.push_back({ r, H_BACK });
		}
		const wxString next = page == P_HELLO ? "Get Started"
		                    : page == P_READY ? "Just Start"
		                    : "Continue";
		const wxRect nr(W - 204, (int)y, 144, 38);
		ui::drawButton(gc, nr, next, page != P_READY, hot == H_NEXT);
		hits.push_back({ nr, H_NEXT });
	}

	// --- input ---------------------------------------------------------------

	int hitAt(const wxPoint& p) const {
		for (auto it = hits.rbegin(); it != hits.rend(); ++it) if (it->rect.Contains(p)) return it->id;
		return 0;
	}

	void OnMotion(wxMouseEvent& e) {
		const int was = hot;
		hot = hitAt(e.GetPosition());
		if (hot != was) face->SetCursor(hot ? wxCursor(wxCURSOR_HAND) : wxNullCursor);
	}

	void OnDown(wxMouseEvent& e) {
		const int id = hitAt(e.GetPosition());
		auto& s = appConfig().appSettings;
		switch (id) {
			case 0: return;
			case H_NEXT:  page == P_READY ? finish(Ending::Blank) : go(page + 1); return;
			case H_BACK:  go(page - 1); return;
			case H_SKIP:  finish(Ending::Close); return;
			case H_TOUR:  finish(Ending::Tour); return;
			case H_BLANK: finish(Ending::Blank); return;
			case H_SHORTCUTS: finish(Ending::Shortcuts); return;
			default: break;
		}
		if (id >= H_DOT && id < H_DOT + PAGE_COUNT) { go(id - H_DOT); return; }

		if (id >= H_TOOLBAR) s.toolbarStyle = id - H_TOOLBAR;
		else if (id >= H_TABS) s.classicTabs = (id - H_TABS) == 1;
		else if (id >= H_ACCENT) s.accentColor = id - H_ACCENT;
		else if (id >= H_THEME) {
			const int choice = id - H_THEME;   // 0 system, 1 light, 2 dark
			s.themeMode = choice;
			const bool wantDark = choice == 2 ||
				(choice == 0 && wxSystemSettings::GetAppearance().IsDark());
			if (wantDark != renderMode().darkMode && frame) frame->ToggleDarkMode();
		}
		// Applied as it is picked, so the app behind this window is the preview.
		if (frame) frame->ApplyPreferences();
		face->Refresh();
	}

	void OnKey(wxKeyEvent& e) {
		const int k = e.GetKeyCode();
		const bool typing = name->IsShown() && name->HasFocus();
		if (k == WXK_ESCAPE) { finish(Ending::Close); return; }
		if (k == WXK_RETURN || k == WXK_NUMPAD_ENTER) {
			if (page == P_READY) finish(Ending::Tour);
			else go(page + 1);
			return;
		}
		if (!typing && (k == WXK_RIGHT || k == WXK_LEFT)) { go(page + (k == WXK_RIGHT ? 1 : -1)); return; }
		// The keys page: every key it describes answers.
		if (page == P_KEYS && !e.HasAnyModifiers()) {
			for (int i = 0; i < 6; i++) {
				const int c = (unsigned char)KEY_CARDS[i].key[0];
				if (k == c || k == std::tolower(c)) {
					pressedAt[i] = true;
					pressedWhen[i] = Clock::now();
					return;
				}
			}
		}
		if (!typing && (k == '?' || (k == '/' && e.ShiftDown()))) { finish(Ending::Shortcuts); return; }
		e.Skip();
	}

	MainFrame* frame;
	wxPanel* face = nullptr;
	wxTextCtrl* name = nullptr;
	std::vector<Hit> hits;
	int page = 0, shownFrom = 0, hot = 0;
	bool closing = false;
	Clock::time_point started, pageChanged;
	bool pressedAt[6] = { false, false, false, false, false, false };
	Clock::time_point pressedWhen[6];
	wxTimer beat;
};

// ------------------------------------------------------------- the tour ---

// What the tour can see of the circuit it is watching.
struct Circuit {
	GUICanvas* c;

	std::vector<guiGate*> gates() const {
		std::vector<guiGate*> out;
		if (c) for (auto& g : *c->getGateList()) if (g.second) out.push_back(g.second);
		return out;
	}
	std::vector<guiGate*> switches() const {
		std::vector<guiGate*> out;
		for (guiGate* g : gates()) if (dynamic_cast<guiGateTOGGLE*>(g)) out.push_back(g);
		return out;
	}
	std::vector<guiGate*> lights() const {
		std::vector<guiGate*> out;
		for (guiGate* g : gates()) if (dynamic_cast<guiGateLED*>(g)) out.push_back(g);
		return out;
	}
	// Any AND gate (not a NAND): AA_AND2, AA_AND3, ...
	guiGate* andGate() const {
		for (guiGate* g : gates()) {
			const std::string n = g->getLibraryGateName();
			if (n.find("AND") != std::string::npos && n.find("NAND") == std::string::npos) return g;
		}
		return nullptr;
	}
	int andInputsWired() const {
		guiGate* g = andGate();
		if (!g) return 0;
		int n = 0;
		for (const auto& hs : g->getHotspotList())
			if (hs.first.rfind("IN", 0) == 0 && g->isConnected(hs.first)) n++;
		return n;
	}
	// The gate's output wire reaches a light.
	bool lightWired() const {
		guiGate* g = andGate();
		if (!g || !g->isConnected("OUT")) return false;
		guiWire* out = g->getConnection("OUT");
		for (guiGate* l : lights())
			for (const auto& conn : l->getConnections())
				if (conn.second == out) return true;
		return false;
	}
	bool lightOn() const {
		for (guiGate* l : lights())
			for (const auto& conn : l->getConnections())
				if (conn.second && !conn.second->getState().empty() && conn.second->getState()[0] == ONE)
					return true;
		return false;
	}
	int switchesOn() const {
		int n = 0;
		for (guiGate* s : switches()) if (s->getLogicParam("OUTPUT_NUM") == "1") n++;
		return n;
	}
};

// Everything a step can check, carried from one tick to the next.
struct TourState {
	MainFrame* frame = nullptr;
	GUICanvas* canvas = nullptr;
	bool sawLit = false;
	bool sawSimView = false;
	bool sawTruthTable = false;
	int savesAtStart = 0;
};

struct TourStep {
	wxString title;
	std::function<wxString(const TourState&)> body;
	std::function<std::vector<wxString>(const TourState&)> keys;   // shown as caps
	std::function<bool(TourState&)> done;                             // null: press Next
	std::function<void(TourState&)> enter;                            // on arriving
};

std::vector<TourStep> buildTour() {
	auto text = [](const char* utf8) {
		const wxString s = U(utf8);
		return [s](const TourState&) { return s; };
	};
	auto caps = [](std::vector<wxString> k) { return [k](const TourState&) { return k; }; };
	std::vector<TourStep> steps;

	steps.push_back({ "Add a switch",
		text("Switches are your inputs. Press A, type toggle and press Return \u2014 the switch "
		     "follows your mouse; click to drop it. (Or drag a Toggle Switch from Input/Output "
		     "in the panel on the left.)"),
		caps({ "A" }),
		[](TourState& s) { return Circuit{ s.canvas }.switches().size() >= 1; }, nullptr });

	steps.push_back({ "Add a second switch",
		text("An AND gate has two inputs, so it needs two switches. Put another one below the "
		     "first \u2014 press A again, or select the first and press D to duplicate it."),
		caps({ "D" }),
		[](TourState& s) { return Circuit{ s.canvas }.switches().size() >= 2; }, nullptr });

	steps.push_back({ "Add an AND gate",
		text("Press A and type and. Drop the gate to the right of your switches."),
		caps({ "A" }),
		[](TourState& s) { return Circuit{ s.canvas }.andGate() != nullptr; }, nullptr });

	steps.push_back({ "Add a light",
		text("A light (an LED) shows an output. Press A, type led, and drop it to the right "
		     "of the gate."),
		caps({ "A" }),
		[](TourState& s) { return !Circuit{ s.canvas }.lights().empty(); }, nullptr });

	steps.push_back({ "Wire in the switches",
		text("Drag from a switch's pin \u2014 the little stub on its edge \u2014 to one of the gate's "
		     "inputs. Or click the pin, let go, and click the other one. Do it for both switches."),
		caps({}),
		[](TourState& s) { return Circuit{ s.canvas }.andInputsWired() >= 2; }, nullptr });

	steps.push_back({ "Wire in the light",
		text("Now connect the gate's output, on its right-hand side, to the light."),
		caps({}),
		[](TourState& s) { return Circuit{ s.canvas }.lightWired(); }, nullptr });

	steps.push_back({ "Switch them both on",
		text("Click the middle of each switch. When both are on, the light comes on \u2014 that is "
		     "all AND means: this and that."),
		caps({ "click" }),
		[](TourState& s) {
			if (Circuit{ s.canvas }.lightOn()) s.sawLit = true;
			return s.sawLit;
		}, nullptr });

	steps.push_back({ "Now turn one off",
		text("Click either switch. The light goes out: AND needs every input on."),
		caps({ "click" }),
		[](TourState& s) {
			const Circuit c{ s.canvas };
			return !c.lightOn() && c.switchesOn() < 2;
		}, nullptr });

	steps.push_back({ "Watch it run",
		[](const TourState& s) {
			return U(renderMode().simView || s.sawSimView
				? "Signals move along every wire carrying a 1. Flip a switch and watch them go. "
				  "Press Escape when you've seen enough."
				: "Simulation View shows the circuit working: lit wires, with the signal "
				  "marching along them.");
		},
		[](const TourState& s) {
			return renderMode().simView || s.sawSimView ? std::vector<wxString>{ "Escape" }
			                                            : std::vector<wxString>{ "Cmd", "R" };
		},
		[](TourState& s) {
			if (renderMode().simView) s.sawSimView = true;
			return s.sawSimView && !renderMode().simView;
		}, nullptr });

	steps.push_back({ "Check it with a truth table",
		text("Press T. CedarLogic tries every combination of the switches and writes down what "
		     "the light did \u2014 a quick way to check your work before you hand it in."),
		caps({ "T" }),
		[](TourState& s) {
			for (wxWindow* w : wxTopLevelWindows)
				if (w && w->IsShown() && w->GetLabel() == "Truth Table") s.sawTruthTable = true;
			// Done once it has been seen and closed again.
			bool open = false;
			for (wxWindow* w : wxTopLevelWindows)
				if (w && w->IsShown() && w->GetLabel() == "Truth Table") open = true;
			return s.sawTruthTable && !open;
		}, nullptr });

	steps.push_back({ "Keep a version",
		text("Your work saves itself every few seconds. When you reach a good point, save \u2014 "
		     "that keeps a version you can go back to from File > Version History."),
		caps({ "Cmd", "S" }),
		[](TourState& s) { return s.frame && s.frame->ExplicitSaveCount() > s.savesAtStart; },
		[](TourState& s) { if (s.frame) s.savesAtStart = s.frame->ExplicitSaveCount(); } });

	steps.push_back({ "You built a working circuit",
		text("That's the loop: add, wire, try it, check it. Press ? whenever you want every "
		     "shortcut, and Help > Guided Tour brings this back."),
		caps({ "?" }),
		nullptr, nullptr });
	return steps;
}

// The card that floats over the window and walks through the tour. It never
// takes the keyboard: the canvas keeps it, since pressing keys there is half
// of what the tour teaches.
class TutorialCoach : public wxFrame {
public:
	TutorialCoach(MainFrame* frame, GUICanvas* canvas, bool offscreen = false)
		: wxFrame(frame, wxID_ANY, "Guided Tour", wxDefaultPosition, wxSize(CARD_W, 200),
		          wxFRAME_NO_TASKBAR | wxFRAME_FLOAT_ON_PARENT | wxBORDER_NONE | wxFRAME_SHAPED),
		  steps(buildTour()) {
		state.frame = frame;
		state.canvas = canvas;
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		Bind(wxEVT_PAINT, &TutorialCoach::OnPaint, this);
		Bind(wxEVT_LEFT_DOWN, &TutorialCoach::OnDown, this);
		Bind(wxEVT_LEFT_UP, &TutorialCoach::OnUp, this);
		Bind(wxEVT_MOTION, &TutorialCoach::OnMotion, this);
		Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) { dragging = false; });
		Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { hot = 0; Refresh(); });
		ticker.Bind(wxEVT_TIMER, [this](wxTimerEvent&) { tick(); });
		if (offscreen) return;   // snapshot() only
		ticker.Start(40);
		enterStep();
		fitToContent();
		reposition();
		ShowWithoutActivating();
	}

	// Test hook: step `which` as a picture.
	wxBitmap snapshot(int which) {
		step = which;
		stepStart = Clock::now() - std::chrono::seconds(1);
		fitToContent();
		wxBitmap bmp(CARD_W, height, 24);
		{
			wxMemoryDC dc(bmp);
			paintCard(dc);
		}
		return bmp;
	}
	int StepCount() const { return (int)steps.size(); }

	void Finish() {
		ticker.Stop();
		Hide();
		Destroy();
	}

private:
	static const int CARD_W = 390;
	enum { B_NEXT = 1, B_BACK, B_CLOSE };

	TourState state;
	std::vector<TourStep> steps;
	int step = 0;
	int hot = 0;
	std::vector<Hit> hits;
	wxTimer ticker;
	int tickCount = 0;
	bool celebrating = false;
	Clock::time_point celebrateStart, stepStart = Clock::now();
	int height = 200;

	// Where the card sits, relative to the main window's top-right corner. It
	// can be dragged anywhere and stays put relative to the window after.
	wxPoint offset = wxPoint(-(CARD_W + 26), 104);
	bool dragging = false;
	wxPoint dragFrom;

	MainFrame* frame() const { return state.frame; }

	bool canvasAlive() const {
		if (!frame() || !state.canvas) return false;
		const auto& all = frame()->Canvases();
		return std::find(all.begin(), all.end(), state.canvas) != all.end();
	}

	void enterStep() {
		celebrating = false;
		stepStart = Clock::now();
		if (step < (int)steps.size() && steps[step].enter) steps[step].enter(state);
	}

	void tick() {
		reposition();
		if (!canvasAlive()) {   // its tab was closed, or a new circuit replaced it
			Finish();
			return;
		}
		Refresh();   // the waiting pulse and the check animate
		if (++tickCount % 6 != 0 && !celebrating) return;

		if (celebrating) {
			if (secondsSince(celebrateStart) > 1.0) next();
			return;
		}
		const TourStep& s = steps[step];
		// A beat before judging, so a step is never "done" the instant it
		// appears because of what the last one left behind.
		if (s.done && secondsSince(stepStart) > 0.6 && s.done(state)) {
			celebrating = true;
			celebrateStart = Clock::now();
		}
	}

	void next() {
		if (step + 1 >= (int)steps.size()) { Finish(); return; }
		step++;
		enterStep();
		fitToContent();
		Refresh();
	}

	void back() {
		if (step == 0) return;
		step--;
		enterStep();
		fitToContent();
		Refresh();
	}

	void reposition() {
		if (!frame() || dragging) return;
		const wxRect r(frame()->GetScreenPosition(), frame()->GetSize());
		wxPoint want(r.GetRight() + offset.x, r.y + offset.y);
		// Keep it on the window.
		want.x = std::max(r.x + 8, std::min(want.x, r.GetRight() - CARD_W - 8));
		want.y = std::max(r.y + 8, std::min(want.y, r.GetBottom() - height - 8));
		if (GetScreenPosition() != want) SetPosition(want);
	}

	// Give the focus back to the canvas after the card is clicked: the next
	// thing the tour asks for is nearly always a key.
	void returnFocus() {
		MainFrame* f = frame();
		GUICanvas* c = canvasAlive() ? state.canvas : nullptr;
		if (!f) return;
		f->CallAfter([f, c] {
			f->Raise();
			if (c) c->SetFocus();
		});
	}

	// --- layout and painting ------------------------------------------------

	// Lay the card out (and paint it, if `gc` draws to the window). Returns
	// the height it needs.
	int layout(wxGraphicsContext* gc, bool paint) {
		const wxColour accent = ui::accent(), ink = ui::ink();
		const TourStep& s = steps[step];
		const int pad = 20;
		hits.clear();
		double y = 18;

		// Progress: a caption, a bar, and a close cross.
		gc->SetFont(wxFont(wxFontInfo(10).Bold()), accent);
		const wxString caption = wxString::Format("GUIDED TOUR  ·  %d OF %d", step + 1, (int)steps.size());
		if (paint) gc->DrawText(caption, pad, y);
		const wxRect closeR(CARD_W - pad - 18, (int)y - 3, 22, 22);
		if (paint) {
			if (hot == B_CLOSE) {
				gc->SetPen(*wxTRANSPARENT_PEN);
				gc->SetBrush(wxBrush(ui::withAlpha(ink, 0.10)));
				gc->DrawEllipse(closeR.x, closeR.y, closeR.width, closeR.height);
			}
			gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(ui::withAlpha(ink, 0.6)).Width(1.6)));
			const double cx = closeR.x + 11, cy = closeR.y + 11;
			gc->StrokeLine(cx - 4, cy - 4, cx + 4, cy + 4);
			gc->StrokeLine(cx + 4, cy - 4, cx - 4, cy + 4);
		}
		hits.push_back({ closeR, B_CLOSE });
		y += 22;
		if (paint) {
			const double full = CARD_W - pad * 2;
			const bool finished = celebrating || step + 1 >= (int)steps.size();
			const double doneFrac = (step + (finished ? 1.0 : 0.0)) / steps.size();
			gc->SetPen(*wxTRANSPARENT_PEN);
			gc->SetBrush(wxBrush(ui::withAlpha(ink, 0.10)));
			gc->DrawRoundedRectangle(pad, y, full, 4, 2);
			gc->SetBrush(wxBrush(accent));
			gc->DrawRoundedRectangle(pad, y, std::max(4.0, full * doneFrac), 4, 2);
		}
		y += 18;

		// Title and instruction.
		gc->SetFont(wxFont(wxFontInfo(16.5).Bold()), ink);
		double tw, th;
		gc->GetTextExtent(s.title, &tw, &th);
		if (paint) gc->DrawText(s.title, pad, y);
		y += th + 8;
		gc->SetFont(wxFont(wxFontInfo(12.5)), ui::withAlpha(ink, 0.78));
		for (const wxString& line : ui::wrap(gc, s.body(state), CARD_W - pad * 2)) {
			gc->GetTextExtent(line, &tw, &th);
			if (paint) gc->DrawText(line, pad, y);
			y += th + 3;
		}

		// The keys this step is about.
		const std::vector<wxString> keys = s.keys ? s.keys(state) : std::vector<wxString>();
		if (!keys.empty()) {
			y += 8;
			if (paint) ui::drawKeys(gc, pad, y, 26, keys);
			y += 26;
		}
		y += 16;

		// Status: waiting, or done.
		if (s.done) {
			if (celebrating) {
				const double p = easeOut(secondsSince(celebrateStart) / 0.35);
				if (paint) {
					const wxColour ok = ui::success();
					gc->SetPen(*wxTRANSPARENT_PEN);
					gc->SetBrush(wxBrush(ok));
					const double r = 9 * p;
					gc->DrawEllipse(pad + 9 - r, y + 9 - r, r * 2, r * 2);
					gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(*wxWHITE).Width(2)));
					if (p > 0.6) {
						gc->StrokeLine(pad + 5, y + 9, pad + 8, y + 12);
						gc->StrokeLine(pad + 8, y + 12, pad + 13.5, y + 6);
					}
					gc->SetFont(wxFont(wxFontInfo(12.5).Bold()), ok);
					gc->DrawText(U("Nice \u2014 that's it."), pad + 28, y);
				}
			} else if (paint) {
				// A slow pulse: the tour is watching, not waiting on a button.
				const double pulse = 0.5 + 0.5 * std::sin(secondsSince(stepStart) * 3.2);
				gc->SetPen(*wxTRANSPARENT_PEN);
				gc->SetBrush(wxBrush(ui::withAlpha(accent, 0.18 + 0.22 * pulse)));
				gc->DrawEllipse(pad + 9 - 7 - 2 * pulse, y + 9 - 7 - 2 * pulse, 14 + 4 * pulse, 14 + 4 * pulse);
				gc->SetBrush(wxBrush(accent));
				gc->DrawEllipse(pad + 5, y + 5, 8, 8);
				gc->SetFont(wxFont(wxFontInfo(11.5)), ui::dim());
				gc->DrawText(U("Your turn \u2014 this moves on by itself."), pad + 28, y + 1);
			}
			y += 30;
		}

		// Buttons: Back, and Next (or Skip, while it is watching).
		y += 4;
		const bool last = step + 1 >= (int)steps.size();
		const wxString nextLabel = last ? "Finish" : (s.done && !celebrating ? "Skip" : "Next");
		const wxRect nextR(CARD_W - pad - 96, (int)y, 96, 32);
		if (paint) ui::drawButton(gc, nextR, nextLabel, !(s.done && !celebrating), hot == B_NEXT);
		hits.push_back({ nextR, B_NEXT });
		if (step > 0) {
			const wxRect backR(CARD_W - pad - 96 - 8 - 76, (int)y, 76, 32);
			if (paint) ui::drawButton(gc, backR, "Back", false, hot == B_BACK);
			hits.push_back({ backR, B_BACK });
		}
		y += 32 + 18;
		return (int)std::ceil(y);
	}

	// Size the card to what this step says, and round its corners.
	void fitToContent() {
		wxBitmap scratch(1, 1);
		wxMemoryDC mdc(scratch);
		std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(mdc));
		if (!gc) return;
		// Measure with the celebration line in place, so the card doesn't
		// change height as a step completes.
		const int h = layout(gc.get(), false);
		if (h != height || GetClientSize().y != h) {
			height = h;
			SetSize(CARD_W, height);
			wxGraphicsPath shape = wxGraphicsRenderer::GetDefaultRenderer()->CreatePath();
			shape.AddRoundedRectangle(0, 0, CARD_W, height, 16);
			SetShape(shape);
			reposition();
		}
	}

	void OnPaint(wxPaintEvent&) {
		wxAutoBufferedPaintDC dc(this);
		paintCard(dc);
	}

	void paintCard(wxDC& dc) {
		dc.SetBackground(wxBrush(ui::paper()));
		dc.Clear();
		std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
		if (!gc) return;
		gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
		const wxSize sz(CARD_W, height);
		gc->SetBrush(wxBrush(ui::paper()));
		gc->SetPen(wxPen(ui::withAlpha(ui::ink(), 0.18), 1));
		gc->DrawRoundedRectangle(0.5, 0.5, sz.x - 1, sz.y - 1, 16);
		gc->SetPen(wxPen(ui::withAlpha(ui::accent(), 0.9), 3));
		gc->StrokeLine(16, 1.5, 64, 1.5);   // a small accent tab at the top
		layout(gc.get(), true);
	}

	// --- input -----------------------------------------------------------------

	int hitAt(const wxPoint& p) const {
		for (const Hit& h : hits) if (h.rect.Contains(p)) return h.id;
		return 0;
	}

	void OnMotion(wxMouseEvent& e) {
		if (dragging) {
			const wxPoint now = ClientToScreen(e.GetPosition());
			SetPosition(GetScreenPosition() + (now - dragFrom));
			dragFrom = now;
			return;
		}
		const int was = hot;
		hot = hitAt(e.GetPosition());
		if (hot != was) { SetCursor(hot ? wxCursor(wxCURSOR_HAND) : wxNullCursor); Refresh(); }
	}

	void OnDown(wxMouseEvent& e) {
		const int id = hitAt(e.GetPosition());
		if (id == 0) {   // anywhere else on the card: move it
			dragging = true;
			dragFrom = ClientToScreen(e.GetPosition());
			if (!HasCapture()) CaptureMouse();
			return;
		}
		if (id == B_CLOSE) { returnFocus(); Finish(); return; }
		if (id == B_BACK) back();
		if (id == B_NEXT) next();
		returnFocus();
	}

	void OnUp(wxMouseEvent&) {
		if (!dragging) return;
		dragging = false;
		if (HasCapture()) ReleaseMouse();
		// Remember where it was put, relative to the window's top-right.
		if (frame()) {
			const wxRect r(frame()->GetScreenPosition(), frame()->GetSize());
			offset = GetScreenPosition() - wxPoint(r.GetRight(), r.y);
		}
		returnFocus();
	}
};

TutorialCoach* g_coach = nullptr;

}  // namespace

void ShowWelcome(MainFrame* frame, bool startAtSetup) {
	WelcomeWindow dlg(frame, startAtSetup);
	dlg.ShowModal();
}

void StartTutorial(MainFrame* frame) {
	StopTutorial();
	if (frame == nullptr) return;

	// The tour needs a clear page, and things it can edit.
	if (frame->IsSimView()) frame->SetSimView(false);
	if (frame->IsLockToolOn()) frame->SetLockTool(false);
	GUICanvas* canvas = frame->CurrentCanvas();
	if (canvas && !canvas->getGateList()->empty()) {
		// Their work stays where it is; the tour gets a tab of its own.
		const int pane = std::max(0, frame->PaneIndexOf(canvas));
		frame->NewTabInPane(pane);
		if (frame->CurrentCanvas() != canvas) {
			canvas = frame->CurrentCanvas();
			frame->SetTabName(canvas, "Tour");
		}
	}
	if (canvas == nullptr) return;
	canvas->SetFocus();

	g_coach = new TutorialCoach(frame, canvas);
	g_coach->Bind(wxEVT_DESTROY, [](wxWindowDestroyEvent& e) {
		if (e.GetEventObject() == g_coach) g_coach = nullptr;
		e.Skip();
	});
}

void StopTutorial() {
	if (g_coach) { g_coach->Finish(); g_coach = nullptr; }
}

bool RenderWelcomeSnapshots(MainFrame* frame, const wxString& dir) {
	bool ok = true;
	for (int dark = 0; dark <= 1; dark++) {
		renderMode().darkMode = dark != 0;
		const wxString theme = dark ? "dark" : "light";
		{
			WelcomeWindow w(frame, false);
			for (int p = 0; p < PAGE_COUNT; p++)
				ok &= w.snapshot(p).SaveFile(wxString::Format("%s/welcome-%s-%d.png", dir, theme, p),
				                             wxBITMAP_TYPE_PNG);
		}
		TutorialCoach* coach = new TutorialCoach(frame, frame->CurrentCanvas(), true);
		for (int i = 0; i < coach->StepCount(); i++)
			ok &= coach->snapshot(i).SaveFile(wxString::Format("%s/tour-%s-%02d.png", dir, theme, i + 1),
			                                  wxBITMAP_TYPE_PNG);
		coach->Destroy();
	}
	return ok;
}
