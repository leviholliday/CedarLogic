/*****************************************************************************
   Project: CEDAR Logic Simulator
   LibraryDialogs: the Library and Version History windows.

   Both are the same window: a search field over a list of custom-drawn rows,
   each with a small preview tile, a name and a quiet second line.
*****************************************************************************/

#include "LibraryDialogs.h"
#include "CircuitLibrary.h"
#include "MainApp.h"
#include "Settings.h"
#include "RenderMode.h"
#include "render/RenderStyle.h"

#include <wx/dialog.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/srchctrl.h>
#include <wx/textdlg.h>
#include <wx/msgdlg.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/file.h>
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/settings.h>
#include <wx/process.h>
#include <wx/stdpaths.h>
#include <wx/image.h>
#include <wx/utils.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

namespace {

// ---------------------------------------------------------------- colours --

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

wxColour paperColour() { return isDark() ? wxColour(28, 31, 37) : wxColour(250, 250, 252); }
wxColour inkColour()   { return isDark() ? wxColour(226, 230, 238) : wxColour(30, 33, 40); }
wxColour dimColour()   { return withAlpha(inkColour(), 0.55); }

// ------------------------------------------------------------------ times --

// "Today at 3:42 PM", "Yesterday at 9:10 AM", "Mar 4 at 1:15 PM".
wxString friendlyTime(const wxDateTime& t) {
	if (!t.IsValid()) return "";
	const wxDateTime today = wxDateTime::Today();
	wxString clock = t.Format("%I:%M %p");
	if (clock.StartsWith("0")) clock = clock.Mid(1);   // "3:42 PM", not "03:42 PM"
	if (t >= today) return "Today at " + clock;
	if (t >= today - wxDateSpan::Day()) return "Yesterday at " + clock;
	if (t.GetYear() == today.GetYear()) return t.Format("%b %d at ") + clock;
	return t.Format("%b %d, %Y at ") + clock;
}

wxString agoText(const wxDateTime& t) {
	const wxTimeSpan d = wxDateTime::Now() - t;
	if (d.GetMinutes() < 1) return "just now";
	if (d.GetMinutes() < 60) return wxString::Format("%lld min ago", (long long)d.GetMinutes());
	if (d.GetHours() < 24) return wxString::Format("%lld hr ago", (long long)d.GetHours());
	return wxString::Format("%d days ago", d.GetDays());
}

// How many gates a saved circuit holds, for the second line. The files are
// small, so counting the markers is cheap enough to do while the list loads.
int gateCount(const wxString& path) {
	wxFile f(path);
	if (!f.IsOpened()) return -1;
	const wxFileOffset len = f.Length();
	if (len <= 0 || len > 8 * 1024 * 1024) return -1;
	std::vector<char> buf((size_t)len + 1, 0);
	if (f.Read(buf.data(), (size_t)len) != len) return -1;
	const wxString text = wxString::FromUTF8(buf.data());
	int n = 0;
	for (size_t at = text.find("(gate "); at != wxString::npos; at = text.find("(gate ", at + 1)) n++;
	return n;
}

// ------------------------------------------------------------------- rows --

struct Row {
	wxString title;      // the name, or the time a version was saved
	wxString subtitle;   // "8 gates · Today at 3:42 PM"
	wxString badge;      // "OPEN" / "NEWEST", or empty
};

// A scrolling list of Rows, drawn by hand: a gate tile, a name, a quiet
// second line, and an accent-tinted highlight on the selected row.
class RowList : public wxScrolledCanvas {
public:
	static const int ROW_H = 62;

	RowList(wxWindow* parent) : wxScrolledCanvas(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
	                                             wxBORDER_NONE | wxVSCROLL) {
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		SetBackgroundColour(paperColour());
		SetScrollRate(0, 1);   // scroll by the pixel, not by the row
		Bind(wxEVT_PAINT, &RowList::OnPaint, this);
		Bind(wxEVT_MOTION, &RowList::OnMotion, this);
		Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { hover = -1; Refresh(); });
		Bind(wxEVT_LEFT_DOWN, &RowList::OnDown, this);
		Bind(wxEVT_LEFT_DCLICK, &RowList::OnDouble, this);
		Bind(wxEVT_MOUSEWHEEL, &RowList::OnWheel, this);
		// Dragging the scrollbar moves the view directly; keep the glide with it.
		Bind(wxEVT_SCROLLWIN_THUMBTRACK, [this](wxScrollWinEvent& e) { e.Skip(); CallAfter([this] { Sync(); }); });
		Bind(wxEVT_SCROLLWIN_THUMBRELEASE, [this](wxScrollWinEvent& e) { e.Skip(); CallAfter([this] { Sync(); }); });
		glide.Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Step(); });
	}

	void SetRows(std::vector<Row> r) {
		rows = std::move(r);
		if (selection >= (int)rows.size()) selection = (int)rows.size() - 1;
		if (selection < 0 && !rows.empty()) selection = 0;
		SetVirtualSize(0, (int)rows.size() * ROW_H);
		glide.Stop();
		here = target = 0;
		Scroll(0, 0);
		Refresh();
	}
	const std::vector<Row>& Rows() const { return rows; }
	int Selection() const { return rows.empty() ? -1 : selection; }
	void Select(int i) {
		if (rows.empty()) return;
		const int was = selection;
		selection = std::max(0, std::min((int)rows.size() - 1, i));
		ScrollIntoView();
		Refresh();
		if (selection != was && onSelection) onSelection();
	}
	void Move(int delta) { Select(selection + delta); }

	std::function<void()> onActivate;    // double-click or Return
	std::function<void()> onSelection;   // the highlighted row changed

private:
	int MaxScroll() const { return std::max(0, (int)rows.size() * ROW_H - GetClientSize().y); }

	// Glide toward `y` instead of jumping there.
	void GlideTo(double y) {
		target = std::max(0.0, std::min((double)MaxScroll(), y));
		if (!glide.IsRunning()) glide.Start(16);
	}
	void Step() {
		const double d = target - here;
		if (std::fabs(d) < 0.5) { here = target; glide.Stop(); }
		else here += d * 0.28;                       // ease out, ~5 frames
		Scroll(0, (int)std::lround(here));
	}

	void Sync() { glide.Stop(); here = target = GetViewStart().y; }

	void OnWheel(wxMouseEvent& e) {
		if (e.GetWheelAxis() != wxMOUSE_WHEEL_VERTICAL) { e.Skip(); return; }
		const double lines = (double)e.GetWheelRotation() / (e.GetWheelDelta() ? e.GetWheelDelta() : 120);
		GlideTo(target - lines * 3 * 18);
	}

	void ScrollIntoView() {
		const int h = GetClientSize().y;
		const int rowTop = selection * ROW_H, rowBot = rowTop + ROW_H;
		if (rowTop < target) GlideTo(rowTop - 4);
		else if (rowBot > target + h) GlideTo(rowBot - h + 4);
	}

	int RowAt(const wxPoint& p) const {
		const int y = p.y + GetViewStart().y;
		const int i = y / ROW_H;
		return (y >= 0 && i < (int)rows.size()) ? i : -1;
	}

	void OnMotion(wxMouseEvent& e) {
		const int h = RowAt(e.GetPosition());
		if (h != hover) { hover = h; Refresh(); }
	}
	void OnDown(wxMouseEvent& e) {
		const int i = RowAt(e.GetPosition());
		if (i >= 0) Select(i);
		SetFocus();
	}
	void OnDouble(wxMouseEvent& e) {
		const int i = RowAt(e.GetPosition());
		if (i >= 0) { Select(i); if (onActivate) onActivate(); }
	}

	// A small rounded tile with a logic-gate silhouette, tinted by the accent.
	void DrawTile(wxGraphicsContext* gc, const wxRect& r, const wxColour& accent, bool on) {
		gc->SetBrush(wxBrush(withAlpha(accent, on ? 0.22 : 0.13)));
		gc->SetPen(*wxTRANSPARENT_PEN);
		gc->DrawRoundedRectangle(r.x, r.y, r.width, r.height, 11);

		const double s = r.width / 40.0, cx = r.x + r.width / 2.0, cy = r.y + r.height / 2.0;
		wxGraphicsPath body = gc->CreatePath();
		body.MoveToPoint(cx - 8 * s, cy - 8 * s);
		body.AddLineToPoint(cx - 1 * s, cy - 8 * s);
		body.AddCurveToPoint(cx + 9 * s, cy - 8 * s, cx + 9 * s, cy + 8 * s, cx - 1 * s, cy + 8 * s);
		body.AddLineToPoint(cx - 8 * s, cy + 8 * s);
		body.CloseSubpath();
		gc->SetPen(wxPen(withAlpha(accent, 0.95), 1.6 * s));
		gc->StrokePath(body);

		gc->SetPen(wxPen(withAlpha(accent, 0.7), 1.6 * s));
		gc->StrokeLine(cx - 14 * s, cy - 4.5 * s, cx - 8 * s, cy - 4.5 * s);
		gc->StrokeLine(cx - 14 * s, cy + 4.5 * s, cx - 8 * s, cy + 4.5 * s);
		gc->StrokeLine(cx + 6.5 * s, cy, cx + 14 * s, cy);
	}

	void OnPaint(wxPaintEvent&) {
		wxAutoBufferedPaintDC dc(this);
		dc.SetBackground(wxBrush(paperColour()));
		dc.Clear();
		std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
		if (!gc) return;
		gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

		const int top = GetViewStart().y;
		const int w = GetClientSize().x;
		const wxColour accent = accentColour(), ink = inkColour();

		if (rows.empty()) {
			gc->SetFont(wxFont(wxFontInfo(13)), dimColour());
			const wxString msg = "Nothing here yet.";
			double tw, th;
			gc->GetTextExtent(msg, &tw, &th);
			gc->DrawText(msg, (w - tw) / 2.0, 40);
			return;
		}

		gc->Translate(0, -top);
		const int first = std::max(0, top / ROW_H);
		const int last = std::min((int)rows.size() - 1, (top + GetClientSize().y) / ROW_H);
		for (int i = first; i <= last; i++) {
			const Row& row = rows[i];
			const int y = i * ROW_H;
			const bool sel = (i == selection), hot = (i == hover);

			if (sel || hot) {
				gc->SetBrush(wxBrush(sel ? withAlpha(accent, isDark() ? 0.26 : 0.16)
				                         : withAlpha(ink, 0.06)));
				gc->SetPen(*wxTRANSPARENT_PEN);
				gc->DrawRoundedRectangle(8, y + 4, w - 16, ROW_H - 8, 12);
			}

			DrawTile(gc.get(), wxRect(20, y + 11, 40, 40), accent, sel);

			gc->SetFont(wxFont(wxFontInfo(13).Bold()), ink);
			gc->DrawText(row.title, 76, y + 13);
			gc->SetFont(wxFont(wxFontInfo(11)), dimColour());
			gc->DrawText(row.subtitle, 76, y + 33);

			if (!row.badge.empty()) {
				gc->SetFont(wxFont(wxFontInfo(9).Bold()), accent);
				double tw, th;
				gc->GetTextExtent(row.badge, &tw, &th);
				const double bx = w - 24 - tw - 16;
				gc->SetBrush(wxBrush(withAlpha(accent, 0.18)));
				gc->SetPen(*wxTRANSPARENT_PEN);
				gc->DrawRoundedRectangle(bx, y + ROW_H / 2.0 - 10, tw + 16, 20, 10);
				gc->SetFont(wxFont(wxFontInfo(9).Bold()), accent);
				gc->DrawText(row.badge, bx + 8, y + ROW_H / 2.0 - th / 2.0);
			}

			if (i < (int)rows.size() - 1 && !sel && !hot) {
				gc->SetPen(wxPen(withAlpha(ink, 0.08), 1));
				gc->StrokeLine(76, y + ROW_H - 0.5, w - 20, y + ROW_H - 0.5);
			}
		}
	}

	std::vector<Row> rows;
	int selection = 0, hover = -1;
	wxTimer glide;
	double here = 0, target = 0;   // where the view is, and where it's heading
};

// ---------------------------------------------------------------- preview --

// Shows one saved version as a picture. The circuit can't be drawn in this
// process without disturbing the one on screen -- the simulation engine is
// shared -- so a headless copy of the app renders it to a PNG off to the side.
class PreviewPane : public wxPanel {
public:
	PreviewPane(wxWindow* parent) : wxPanel(parent, wxID_ANY) {
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		SetBackgroundColour(paperColour());
		alive = std::make_shared<bool>(true);
		Bind(wxEVT_PAINT, &PreviewPane::OnPaint, this);
	}
	~PreviewPane() override {
		*alive = false;
		if (!tempDir.empty()) wxFileName::Rmdir(tempDir, wxPATH_RMDIR_RECURSIVE);
	}

	void Show(const wxString& cdlPath) {
		want = cdlPath;
		auto cached = shots.find(cdlPath);
		if (cached != shots.end()) { shown = cached->second; busy = false; Refresh(); return; }
		shown = wxBitmap();
		busy = true;
		Refresh();
		// One render at a time. Each is a whole copy of the app, and arrowing
		// down a long history started one per row, all at once. The one that
		// is running picks up whatever is wanted by the time it finishes.
		if (!rendering) Render(cdlPath);
	}

private:
	void Render(const wxString& cdlPath) {
		if (tempDir.empty()) {
			tempDir = wxFileName::CreateTempFileName("cedarpreview");
			wxRemoveFile(tempDir);
			wxFileName::Mkdir(tempDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
		}
		const wxString png = tempDir + wxFILE_SEP_PATH +
			wxString::Format("p%u.png", (unsigned)shots.size() + 1);
		const wxString cmd = wxString::Format("\"%s\" --render \"%s\" \"%s\" 1200 820",
			wxStandardPaths::Get().GetExecutablePath(), cdlPath, png);

		// No parent: this can finish after the window is gone, and a wxProcess
		// hands its end event on to its parent -- which would be freed memory.
		wxProcess* proc = new wxProcess();
		auto flag = alive;
		proc->Bind(wxEVT_END_PROCESS, [this, flag, cdlPath, png](wxProcessEvent& e) {
			// Left unhandled on purpose: an unhandled end event is what makes a
			// wxProcess delete itself. Handling it leaked one per preview.
			e.Skip();
			if (!*flag) return;
			rendering = false;
			wxImage img;
			if (e.GetExitCode() == 0 && wxFileName::FileExists(png) && img.LoadFile(png, wxBITMAP_TYPE_PNG))
				shots[cdlPath] = wxBitmap(img);
			else
				shots[cdlPath] = wxBitmap();   // remember the failure; don't retry in a loop
			if (want == cdlPath) { shown = shots[cdlPath]; busy = false; Refresh(); }
			else if (shots.find(want) == shots.end()) Render(want);   // moved on meanwhile
		});
		rendering = true;
		if (wxExecute(cmd, wxEXEC_ASYNC, proc) <= 0) {
			delete proc;
			rendering = false;
			shots[cdlPath] = wxBitmap();
			if (want == cdlPath) busy = false;
			Refresh();
		}
	}

	void OnPaint(wxPaintEvent&) {
		wxAutoBufferedPaintDC dc(this);
		dc.SetBackground(wxBrush(paperColour()));
		dc.Clear();
		std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
		if (!gc) return;
		gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
		const wxSize sz = GetClientSize();
		const wxColour ink = inkColour();

		// The page the circuit sits on.
		gc->SetBrush(wxBrush(isDark() ? wxColour(22, 24, 29) : *wxWHITE));
		gc->SetPen(wxPen(withAlpha(ink, 0.12), 1));
		gc->DrawRoundedRectangle(0.5, 0.5, sz.x - 1, sz.y - 1, 12);

		if (shown.IsOk()) {
			const double k = std::min((sz.x - 24.0) / shown.GetWidth(), (sz.y - 24.0) / shown.GetHeight());
			const double w = shown.GetWidth() * k, h = shown.GetHeight() * k;
			gc->DrawBitmap(shown, (sz.x - w) / 2, (sz.y - h) / 2, w, h);
			return;
		}

		gc->SetFont(wxFont(wxFontInfo(12)), dimColour());
		const wxString msg = busy ? "Drawing this version..." : "No preview for this version.";
		double tw, th;
		gc->GetTextExtent(msg, &tw, &th);
		gc->DrawText(msg, (sz.x - tw) / 2, (sz.y - th) / 2);
	}

	std::map<wxString, wxBitmap> shots;
	wxBitmap shown;
	wxString want, tempDir;
	bool busy = false;
	bool rendering = false;        // a render process is running
	std::shared_ptr<bool> alive;   // the renders outlive a closed window
};

// The shared window: a heading, an optional search field, the list, buttons.
struct Picker {
	wxDialog* dlg = nullptr;
	RowList* list = nullptr;
	wxSearchCtrl* search = nullptr;
	wxBoxSizer* buttons = nullptr;
};

Picker buildPicker(wxWindow* parent, const wxString& title, const wxString& heading,
                   const wxString& hint, const wxString& searchHint, bool withSearch) {
	Picker p;
	p.dlg = new wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxSize(600, 540),
	                     wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	p.dlg->SetBackgroundColour(paperColour());
	wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);

	wxStaticText* head = new wxStaticText(p.dlg, wxID_ANY, heading);
	head->SetFont(wxFont(wxFontInfo(19).Bold()));
	head->SetForegroundColour(inkColour());
	top->Add(head, 0, wxLEFT | wxRIGHT | wxTOP, 22);

	wxStaticText* sub = new wxStaticText(p.dlg, wxID_ANY, hint);
	sub->SetForegroundColour(dimColour());
	top->Add(sub, 0, wxLEFT | wxRIGHT | wxTOP, 22);

	if (withSearch) {
		p.search = new wxSearchCtrl(p.dlg, wxID_ANY);
		p.search->ShowCancelButton(true);
		p.search->SetDescriptiveText(searchHint);
		top->Add(p.search, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, 22);
	}

	p.list = new RowList(p.dlg);
	top->Add(p.list, 1, wxALL | wxEXPAND, 14);

	p.buttons = new wxBoxSizer(wxHORIZONTAL);
	top->Add(p.buttons, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 22);

	p.dlg->SetSizer(top);
	return p;
}

}  // namespace

// ----------------------------------------------------------------- library --

LibraryChoice ShowLibraryDialog(wxWindow* parent, const std::string& currentId) {
	LibraryChoice choice;
	Picker p = buildPicker(parent, "Your Circuits", "Your Circuits",
		"Everything here saves itself. Use Import to bring in a .cdl file.",
		"Search circuits", true);
	std::unique_ptr<wxDialog> owner(p.dlg);

	std::vector<library::Doc> shown;
	auto reload = [&]() {
		const wxString q = p.search->GetValue().Lower().Strip(wxString::both);
		shown.clear();
		std::vector<Row> rows;
		for (const library::Doc& d : library::list()) {
			if (!q.empty() && !d.name.Lower().Contains(q)) continue;
			const int gates = gateCount(library::circuitPath(d.id));
			wxString sub = friendlyTime(d.modified);
			if (gates >= 0) sub = wxString::Format("%d gate%s · ", gates, gates == 1 ? "" : "s") + sub;
			Row r;
			r.title = d.name;
			r.subtitle = sub;
			r.badge = (d.id == currentId) ? "OPEN" : "";
			rows.push_back(r);
			shown.push_back(d);
		}
		p.list->SetRows(rows);
	};

	wxButton* importBtn = new wxButton(p.dlg, wxID_ANY, "Import File...");
	wxButton* renameBtn = new wxButton(p.dlg, wxID_ANY, "Rename...");
	wxButton* deleteBtn = new wxButton(p.dlg, wxID_ANY, "Delete");
	importBtn->SetToolTip("Bring in a .cdl file as a copy  (⌘I)");
	renameBtn->SetToolTip("Rename the selected circuit  (⌘R)");
	deleteBtn->SetToolTip("Delete the selected circuit  (⌘⌫)");
	wxButton* cancelBtn = new wxButton(p.dlg, wxID_CANCEL, "Cancel");
	wxButton* openBtn = new wxButton(p.dlg, wxID_OK, "Open");
	openBtn->SetDefault();
	p.buttons->Add(importBtn, 0, wxRIGHT, 8);
	p.buttons->Add(renameBtn, 0, wxRIGHT, 8);
	p.buttons->Add(deleteBtn, 0);
	p.buttons->AddStretchSpacer(1);
	p.buttons->Add(cancelBtn, 0, wxRIGHT, 8);
	p.buttons->Add(openBtn, 0);

	auto openSelected = [&]() {
		const int i = p.list->Selection();
		if (i < 0 || i >= (int)shown.size()) return;
		choice.action = LibraryChoice::Open;
		choice.id = shown[i].id;
		p.dlg->EndModal(wxID_OK);
	};
	auto doImport = [&]() {
		choice.action = LibraryChoice::Import;
		p.dlg->EndModal(wxID_OK);
	};
	auto doRename = [&]() {
		const int i = p.list->Selection();
		if (i < 0 || i >= (int)shown.size()) return;
		wxTextEntryDialog ask(p.dlg, "Name:", "Rename Circuit", shown[i].name);
		if (ask.ShowModal() != wxID_OK || ask.GetValue().Strip(wxString::both).empty()) return;
		library::rename(shown[i].id, ask.GetValue());
		reload();
	};
	auto doDelete = [&]() {
		const int i = p.list->Selection();
		if (i < 0 || i >= (int)shown.size()) return;
		if (shown[i].id == currentId) {
			wxMessageBox("That circuit is open. Open a different one first, then delete it.",
			             "Delete Circuit", wxOK | wxICON_INFORMATION, p.dlg);
			return;
		}
		wxMessageDialog confirm(p.dlg, "Delete \"" + shown[i].name + "\" and all its versions?",
		                        "Delete Circuit", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
		confirm.SetYesNoLabels("Delete", "Cancel");
		if (confirm.ShowModal() != wxID_YES) return;
		library::remove(shown[i].id);
		reload();
	};

	p.list->onActivate = openSelected;
	openBtn->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) { openSelected(); });
	importBtn->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) { doImport(); });
	renameBtn->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) { doRename(); });
	deleteBtn->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) { doDelete(); });
	p.search->Bind(wxEVT_TEXT, [&](wxCommandEvent&) { reload(); });

	// Type to search, arrows to move, Return to open — wherever the focus is.
	p.dlg->Bind(wxEVT_CHAR_HOOK, [&](wxKeyEvent& e) {
		const bool cmd = e.CmdDown();
		switch (e.GetKeyCode()) {
			case WXK_DOWN:     p.list->Move(1); return;
			case WXK_UP:       p.list->Move(-1); return;
			case WXK_PAGEDOWN: p.list->Move(8); return;
			case WXK_PAGEUP:   p.list->Move(-8); return;
			case WXK_HOME:     if (!cmd) break; p.list->Select(0); return;
			case WXK_END:      if (!cmd) break; p.list->Select((int)p.list->Rows().size() - 1); return;
			case WXK_RETURN:
			case WXK_NUMPAD_ENTER: openSelected(); return;
			// Cmd+Delete deletes, so plain Delete still edits the search text.
			case WXK_BACK:
			case WXK_DELETE:   if (!cmd) break; doDelete(); return;
			case 'R': if (!cmd) break; doRename(); return;
			case 'I': if (!cmd) break; doImport(); return;
			case 'F': if (!cmd) break; p.search->SetFocus(); p.search->SelectAll(); return;
			default: break;
		}
		e.Skip();
	});

	reload();
	p.dlg->CentreOnParent();
	p.search->SetFocus();
	p.dlg->ShowModal();
	return choice;
}

// --------------------------------------------------------- version history --

wxString ShowVersionHistoryDialog(wxWindow* parent, const std::string& id) {
	wxString restorePath;
	const std::vector<library::Version> versions = library::versions(id);
	if (versions.empty()) {
		wxMessageBox("No earlier versions yet. A version is kept each time you save (Cmd+S), "
		             "and every few minutes while you work.", "Version History",
		             wxOK | wxICON_INFORMATION, parent);
		return restorePath;
	}

	// Two panes, like a document's revision history: the picture of a version
	// on the left, the list of versions on the right.
	wxDialog* dlg = new wxDialog(parent, wxID_ANY, "Version History", wxDefaultPosition,
	                             wxSize(940, 620), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	dlg->SetBackgroundColour(paperColour());
	std::unique_ptr<wxDialog> owner(dlg);

	wxBoxSizer* outer = new wxBoxSizer(wxVERTICAL);
	wxStaticText* head = new wxStaticText(dlg, wxID_ANY, library::name(id));
	head->SetFont(wxFont(wxFontInfo(19).Bold()));
	head->SetForegroundColour(inkColour());
	outer->Add(head, 0, wxLEFT | wxRIGHT | wxTOP, 22);
	wxStaticText* hint = new wxStaticText(dlg, wxID_ANY,
		"Pick a version to see it. Restoring keeps your current one too, so you can always come back.");
	hint->SetForegroundColour(dimColour());
	outer->Add(hint, 0, wxLEFT | wxRIGHT | wxTOP, 22);

	wxBoxSizer* panes = new wxBoxSizer(wxHORIZONTAL);
	PreviewPane* preview = new PreviewPane(dlg);
	panes->Add(preview, 1, wxEXPAND | wxRIGHT, 14);
	RowList* list = new RowList(dlg);
	list->SetMinSize(wxSize(300, -1));
	panes->Add(list, 0, wxEXPAND);
	outer->Add(panes, 1, wxALL | wxEXPAND, 22);

	wxBoxSizer* buttonRow = new wxBoxSizer(wxHORIZONTAL);
	outer->Add(buttonRow, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 22);
	dlg->SetSizer(outer);

	// Keep the rest of this function reading the way the library one does.
	struct { wxDialog* dlg; RowList* list; wxBoxSizer* buttons; } p{ dlg, list, buttonRow };

	std::vector<Row> rows;
	for (const library::Version& v : versions) {
		const int gates = gateCount(v.path);
		wxString sub = agoText(v.when);
		if (gates >= 0) sub = wxString::Format("%d gate%s · ", gates, gates == 1 ? "" : "s") + sub;
		Row r;
		r.title = friendlyTime(v.when);
		r.subtitle = sub;
		rows.push_back(r);
	}
	rows[0].badge = "NEWEST";
	p.list->SetRows(rows);
	p.list->onSelection = [&]() {
		const int i = p.list->Selection();
		if (i >= 0) preview->Show(versions[i].path);
	};

	wxButton* exportBtn = new wxButton(p.dlg, wxID_ANY, "Export Copy...");
	wxButton* closeBtn = new wxButton(p.dlg, wxID_CANCEL, "Close");
	wxButton* restoreBtn = new wxButton(p.dlg, wxID_OK, "Restore");
	restoreBtn->SetDefault();
	p.buttons->Add(exportBtn, 0);
	p.buttons->AddStretchSpacer(1);
	p.buttons->Add(closeBtn, 0, wxRIGHT, 8);
	p.buttons->Add(restoreBtn, 0);

	exportBtn->SetToolTip("Save a copy of the selected version  (⌘E)");
	restoreBtn->SetToolTip("Bring the selected version back  (↩)");

	auto restore = [&]() {
		const int i = p.list->Selection();
		if (i < 0) return;
		restorePath = versions[i].path;
		p.dlg->EndModal(wxID_OK);
	};
	auto doExport = [&]() {
		const int i = p.list->Selection();
		if (i < 0) return;
		wxFileDialog save(p.dlg, "Export Version", wxEmptyString,
			library::name(id) + " (" + versions[i].when.Format("%b %d %H-%M") + ").cdl",
			"Circuit files (*.cdl)|*.cdl", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
		if (save.ShowModal() == wxID_OK && !wxCopyFile(versions[i].path, save.GetPath(), true))
			wxMessageBox("Couldn't save a copy there. Try another folder.",
			             "Export Version", wxOK | wxICON_ERROR, p.dlg);
	};

	p.list->onActivate = restore;
	restoreBtn->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) { restore(); });
	exportBtn->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) { doExport(); });

	p.dlg->Bind(wxEVT_CHAR_HOOK, [&](wxKeyEvent& e) {
		switch (e.GetKeyCode()) {
			case WXK_DOWN:     p.list->Move(1); return;
			case WXK_UP:       p.list->Move(-1); return;
			case WXK_PAGEDOWN: p.list->Move(8); return;
			case WXK_PAGEUP:   p.list->Move(-8); return;
			case WXK_RETURN:
			case WXK_NUMPAD_ENTER: restore(); return;
			case 'E': if (!e.CmdDown()) break; doExport(); return;
			default: break;
		}
		e.Skip();
	});

	p.dlg->CentreOnParent();
	p.list->SetFocus();
	preview->Show(versions[0].path);
	p.dlg->ShowModal();
	return restorePath;
}
