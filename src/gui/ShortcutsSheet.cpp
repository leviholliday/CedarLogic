/*****************************************************************************
   Project: CEDAR Logic Simulator
   ShortcutsSheet: every keyboard shortcut, searchable. See ShortcutsSheet.h.
*****************************************************************************/

#include "ShortcutsSheet.h"
#include "MainFrame.h"
#include "MainApp.h"
#include "Settings.h"
#include "UiKit.h"
#include "GUICanvas.h"

#include <wx/dialog.h>
#include <wx/scrolwin.h>
#include <wx/srchctrl.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/display.h>
#include <wx/menu.h>
#include <wx/dcmemory.h>
#include <algorithm>
#include <functional>
#include <memory>

DECLARE_APP(MainApp)

namespace {

struct Shortcut {
	wxString section;
	std::vector<wxString> keys;   // "Cmd", "Shift", "T"; lower case = a gesture
	wxString what;
	int command = 0;              // a menu command it runs, a canvas key (keyCommand), or 0
};

// "Cmd Shift D" for the dark-mode shortcut the user set in Preferences.
std::vector<wxString> themeShortcutKeys() {
	const auto& s = appConfig().appSettings;
	std::vector<wxString> keys;
	const int m = s.themeShortcutModifiers;
	if (m & ThemeShortcutMod::Ctrl)  keys.push_back("Ctrl");
	if (m & ThemeShortcutMod::Alt)   keys.push_back("Option");
	if (m & ThemeShortcutMod::Shift) keys.push_back("Shift");
#ifdef __WXOSX__
	if (m & ThemeShortcutMod::Meta)  keys.push_back("Cmd");
#else
	if (m & ThemeShortcutMod::Meta)  keys.push_back("Win");
#endif
	const int k = s.themeShortcutKeyCode;
	if (k > 32 && k < 127) keys.push_back(wxString((wxChar)wxToupper(k)));
	else keys.push_back(wxString::Format("Key %d", k));
	return keys;
}

std::vector<Shortcut> allShortcuts() {
	std::vector<Shortcut> v;
	wxString section;
	// A bare-key action the canvas handles itself (A, R, ...) has no menu
	// command; running it from here presses the key on the canvas instead.
	auto keyCommand = [](int key) { return -key; };
	auto add = [&](std::vector<wxString> keys, const wxString& what, int command = 0) {
		v.push_back({ section, std::move(keys), what, command });
	};

	section = "Circuits";
	add({ "Cmd", "N" }, "New circuit", wxID_NEW);
	add({ "Cmd", "O" }, "Open one of your circuits", wxID_OPEN);
	add({ "Cmd", "S" }, "Save now and keep a version", wxID_SAVE);
	add({ "Cmd", "Shift", "O" }, "Import a .cdl file", File_Import);
	add({ "Cmd", "Shift", "E" }, "Export as a CedarLogic file", wxID_SAVEAS);
	add({ "Cmd", "E" }, "Export as an image", File_Export);
	add({ "Cmd", "Shift", "W" }, "Close this circuit", File_CloseCircuit);

	section = "Editing";
	add({ "Cmd", "Z" }, "Undo", wxID_UNDO);
	add({ "Cmd", "Shift", "Z" }, "Redo", wxID_REDO);
	add({ "Cmd", "X" }, "Cut", wxID_CUT);
	add({ "Cmd", "C" }, "Copy", wxID_COPY);
	add({ "Cmd", "V" }, "Paste", wxID_PASTE);
	add({ "Cmd", "D" }, "Duplicate the selection", Edit_Duplicate);
	add({ "Cmd", "A" }, "Select everything on the page", wxID_SELECTALL);
	add({ "Delete" }, "Delete the selection");
	add({ "Escape" }, "Cancel a drag, paste or connection");
	add({ "Shift", "click" }, "Add to or remove from the selection");

	section = "Building";
	add({ "A" }, "Add a gate by name", keyCommand('A'));
	add({ "Shift", "1-0" }, "Jump to a gate category (0 = 10th)");
	add({ "R" }, "Rotate the selection", keyCommand('R'));
	add({ "Up", "Down", "Left", "Right" }, "Nudge (Shift: 5 squares)");
	add({ "click a pin, then another" }, "Connect them");
	add({ "C", "while dragging" }, "Connect to pins nearby");
	add({ "S" }, "Straighten selected wires");
	add({ "right-click a wire" }, "Straighten or delete it");
	add({ "double-click a gate" }, "Change its settings");

	section = "Quick keys";
	add({ "C" }, "Copy", wxID_COPY);
	add({ "V" }, "Paste", wxID_PASTE);
	add({ "X" }, "Cut", wxID_CUT);
	add({ "D" }, "Duplicate", Edit_Duplicate);
	add({ "T" }, "Truth table", View_TruthTable);

	section = "Moving around";
	add({ "Cmd", "=" }, "Zoom in", Tool_ZoomIn);
	add({ "Cmd", "-" }, "Zoom out", Tool_ZoomOut);
	add({ "Cmd", "0" }, "Zoom to fit", View_ZoomFit);
	add({ "Cmd", "1" }, "Actual size", View_ZoomActual);
	add({ "Space" }, "Zoom to fit (tap)");
	add({ "Space", "drag" }, "Move around");
	add({ "Cmd", "drag" }, "Move around");
	add({ "Cmd", "scroll" }, "Zoom (or pinch)");
	add({ "Shift", "scroll" }, "Move sideways");
	add({ "Up", "Down", "Left", "Right" }, "Move around (nothing selected)");
	add({ "Cmd", "." }, "Focus mode: hide the side panel", View_FocusMode);
	if (appConfig().appSettings.themeShortcutEnabled)
		add(themeShortcutKeys(), "Dark mode on or off", View_DarkMode);

	section = "Simulation";
	add({ "Cmd", "R" }, "Simulation view", View_SimView);
	add({ "Space" }, "Pause or resume (in simulation view)");
	add({ "Escape" }, "Leave simulation view");
	add({ "T" }, "Truth table from the switches and lights", View_TruthTable);
	add({ "Cmd", "G" }, "Oscilloscope", View_Oscope);

	section = "Tabs and split view";
	add({ "Cmd", "T" }, "New tab", Tool_NewTab);
	add({ "Cmd", "W" }, "Close tab", Tool_CloseTab);
	add({ "Cmd", "Shift", "T" }, "Reopen the tab you closed", Tool_ReopenTab);
	add({ "Ctrl", "Tab" }, "Switch tabs (hold Ctrl to see them all)");
	add({ "double-click a tab" }, "Rename it");
	add({ "drag a tab aside" }, "Split the view");
	add({ "Cmd", "Option", "S" }, "Split view", Tool_SplitRight);
	add({ "Cmd", "Option", "W" }, "Close the split", Tool_SplitClose);
	add({ "Cmd", "Option", "Right" }, "Work in the other side", Tool_FocusOtherPane);
	add({ "Cmd", "Shift", "Left", "Right" }, "Make the split wider or narrower");

	section = "App";
	add({ "Cmd", "," }, "Preferences", wxID_PREFERENCES);
	add({ "?" }, "This list");
	add({ "F1" }, "Help", wxID_HELP_CONTENTS);
	add({ "Cmd", "Q" }, "Quit", wxID_EXIT);
	return v;
}

// What a search matches against: the words, the section, and the keys
// spelled out ("cmd shift t") so typing a key's name finds it.
wxString searchText(const Shortcut& s) {
	wxString t = s.what + " " + s.section;
	for (const wxString& k : s.keys) t += " " + k;
	return t.Lower();
}

// The list: sections laid out in columns, rows drawn as keys and words.
class SheetList : public wxScrolledCanvas {
public:
	SheetList(wxWindow* parent) : wxScrolledCanvas(parent, wxID_ANY, wxDefaultPosition,
	                                               wxDefaultSize, wxBORDER_NONE | wxVSCROLL) {
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		SetBackgroundColour(ui::paper());
		SetScrollRate(0, 12);
		Bind(wxEVT_PAINT, &SheetList::OnPaint, this);
		Bind(wxEVT_SIZE, [this](wxSizeEvent& e) { dirty = true; Refresh(); e.Skip(); });
		Bind(wxEVT_MOTION, [this](wxMouseEvent& e) {
			const int i = rowAt(e.GetPosition());
			if (i != hover) {
				hover = i;
				SetCursor(i >= 0 && rows[i].item->command ? wxCursor(wxCURSOR_HAND) : wxNullCursor);
				Refresh();
			}
		});
		Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { hover = -1; Refresh(); });
		Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
			const int i = rowAt(e.GetPosition());
			if (i < 0) return;
			selected = i;
			Refresh();
			if (rows[i].item->command && onRun) onRun(rows[i].item->command);
		});
	}

	void SetItems(std::vector<const Shortcut*> items) {
		shown = std::move(items);
		selected = shown.empty() ? -1 : 0;
		hover = -1;
		dirty = true;
		Scroll(0, 0);
		Refresh();
	}

	void Move(int delta) {
		if (rows.empty()) return;
		selected = std::max(0, std::min((int)rows.size() - 1, (selected < 0 ? 0 : selected) + delta));
		// Keep it in view.
		int ux, uy;
		GetScrollPixelsPerUnit(&ux, &uy);
		const int top = GetViewStart().y * uy, h = GetClientSize().y;
		const wxRect r = rows[selected].rect;
		if (r.y < top + 8) Scroll(0, std::max(0, (r.y - 40) / uy));
		else if (r.GetBottom() > top + h - 8) Scroll(0, (r.GetBottom() - h + 40) / uy);
		Refresh();
	}
	int SelectedCommand() const {
		return (selected >= 0 && selected < (int)rows.size()) ? rows[selected].item->command : 0;
	}

	std::function<void(int)> onRun;

private:
	struct Row { const Shortcut* item; wxRect rect; double keyW; int col; };
	struct Header { wxString title; wxPoint at; };

	// Place every row: whole sections go to whichever column is shorter.
	void layout(wxGraphicsContext* gc) {
		rows.clear();
		headers.clear();
		const int W = GetClientSize().x;
		const int cols = W >= 700 ? 2 : 1;
		const int pad = 22, gapX = 28;
		const int colW = (W - pad * 2 - gapX * (cols - 1)) / cols;
		std::vector<int> colY(cols, 8);

		for (size_t i = 0; i < shown.size();) {
			const wxString sec = shown[i]->section;
			size_t j = i;
			while (j < shown.size() && shown[j]->section == sec) j++;
			const int c = (int)(std::min_element(colY.begin(), colY.end()) - colY.begin());
			const int x = pad + c * (colW + gapX);
			headers.push_back({ sec, wxPoint(x, colY[c] + 10) });
			colY[c] += 36;
			for (size_t k = i; k < j; k++) {
				Row r;
				r.item = shown[k];
				r.rect = wxRect(x - 8, colY[c], colW + 16, ROW_H);
				r.keyW = ui::drawKeys(gc, 0, 0, KEY_H, r.item->keys, true);
				r.col = c;
				rows.push_back(r);
				colY[c] += ROW_H;
			}
			colY[c] += 10;
			i = j;
		}
		// The words line up down each column, after its widest keys.
		keyColW.assign(cols, 0.0);
		for (const Row& r : rows) keyColW[r.col] = std::max(keyColW[r.col], r.keyW);
		const int height = *std::max_element(colY.begin(), colY.end()) + 16;
		SetVirtualSize(W, height);
		dirty = false;
	}

	int rowAt(const wxPoint& p) const {
		int ux, uy;
		GetScrollPixelsPerUnit(&ux, &uy);
		const wxPoint at(p.x, p.y + GetViewStart().y * uy);
		for (size_t i = 0; i < rows.size(); i++) if (rows[i].rect.Contains(at)) return (int)i;
		return -1;
	}

	void OnPaint(wxPaintEvent&) {
		wxAutoBufferedPaintDC dc(this);
		paint(dc);
	}

public:
	void paint(wxDC& dc) {
		dc.SetBackground(wxBrush(ui::paper()));
		dc.Clear();
		std::unique_ptr<wxGraphicsContext> gc(ui::graphics(dc));
		if (!gc) return;
		gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
		if (dirty) layout(gc.get());

		const wxColour ink = ui::ink(), accent = ui::accent();
		if (rows.empty()) {
			gc->SetFont(wxFont(wxFontInfo(13)), ui::dim());
			const wxString msg = "No shortcut matches that.";
			double tw, th;
			gc->GetTextExtent(msg, &tw, &th);
			gc->DrawText(msg, (GetClientSize().x - tw) / 2, 40);
			return;
		}

		int ux, uy;
		GetScrollPixelsPerUnit(&ux, &uy);
		const int top = GetViewStart().y * uy;
		gc->Translate(0, -top);

		for (const Header& h : headers) {
			gc->SetFont(wxFont(wxFontInfo(11).Bold()), ui::withAlpha(accent, 0.95));
			gc->DrawText(h.title.Upper(), h.at.x, h.at.y);
		}
		// The widest keys in each column set where the words start.
		for (size_t i = 0; i < rows.size(); i++) {
			const Row& r = rows[i];
			if (r.rect.GetBottom() < top || r.rect.y > top + GetClientSize().y) continue;
			const bool runnable = r.item->command != 0;
			const bool sel = ((int)i == selected), hot = ((int)i == hover);
			if (sel || hot) {
				gc->SetPen(*wxTRANSPARENT_PEN);
				gc->SetBrush(wxBrush(sel ? ui::withAlpha(accent, ui::isDark() ? 0.24 : 0.14)
				                         : ui::withAlpha(ink, 0.06)));
				gc->DrawRoundedRectangle(r.rect.x, r.rect.y + 1, r.rect.width, r.rect.height - 2, 8);
			}
			const double keysX = r.rect.x + 8;
			ui::drawKeys(gc.get(), keysX, r.rect.y + (ROW_H - KEY_H) / 2.0, KEY_H, r.item->keys);
			const double textX = keysX + std::max(keyColW[r.col], 60.0) + 16;
			const double room = r.rect.GetRight() - 8 - textX;
			// Fonts differ per platform: a line that fits on a Mac can run into
			// the next column on Windows. Step the size down first, and only
			// then cut it short.
			wxString what = r.item->what;
			double tw, th;
			gc->SetFont(wxFont(wxFontInfo(12.5)), ink);
			gc->GetTextExtent(what, &tw, &th);
			if (tw > room) {
				gc->SetFont(wxFont(wxFontInfo(11)), ink);
				gc->GetTextExtent(what, &tw, &th);
			}
			while (tw > room && what.length() > 1) {
				what = what.Left(what.length() - 2) + wxString::FromUTF8("\u2026");
				gc->GetTextExtent(what, &tw, &th);
			}
			gc->DrawText(what, textX, r.rect.y + (ROW_H - th) / 2);
			// A runnable row says so when you point at it; a gesture row says
			// where to do it instead, so clicking one doesn't look broken.
			if (hot || sel) {
				gc->SetFont(wxFont(wxFontInfo(10.5)), ui::dim());
#ifdef __WXOSX__
				const wxString run = hot ? "Click to do it" : "Return to do it";
#else
				const wxString run = hot ? "Click to do it" : "Enter to do it";
#endif
				const wxString go = runnable ? run : wxString("Try it on the canvas");
				double gw, gh;
				gc->GetTextExtent(go, &gw, &gh);
				if (textX + tw + 16 + gw < r.rect.GetRight() - 8)
					gc->DrawText(go, r.rect.GetRight() - 8 - gw, r.rect.y + (ROW_H - gh) / 2);
			}
		}
	}

private:
	static const int ROW_H = 32, KEY_H = 22;
	std::vector<const Shortcut*> shown;
	std::vector<Row> rows;
	std::vector<Header> headers;
	std::vector<double> keyColW;
	int hover = -1, selected = -1;
	bool dirty = true;
};

// Run a menu command as if it had been picked from the menu. Check items
// (Focus Mode, Dark Mode) carry their new state in the event, as a real
// menu click would.
void runCommand(MainFrame* frame, int id) {
	if (id < 0) {   // a canvas key: press it there, so it does exactly what the key does
		GUICanvas* canvas = frame->CurrentCanvas();
		if (canvas == nullptr) return;
		canvas->SetFocus();
		wxKeyEvent key(wxEVT_KEY_DOWN);
		key.m_keyCode = -id;
		key.SetEventObject(canvas);
		canvas->ProcessWindowEvent(key);
		return;
	}
	frame->RunMenuCommand(id);
}

}  // namespace

bool RenderShortcutsSnapshot(MainFrame* frame, const wxString& path, int w, int h,
                             const wxString& query) {
	const std::vector<Shortcut> all = allShortcuts();
	wxDialog host(frame, wxID_ANY, "", wxDefaultPosition, wxSize(w + 40, h + 40));
	SheetList* list = new SheetList(&host);
	list->SetSize(0, 0, w, h);
	std::vector<const Shortcut*> items;
	for (const Shortcut& s : all)
		if (query.empty() || searchText(s).Contains(query.Lower())) items.push_back(&s);
	list->SetItems(items);
	wxBitmap bmp(w, h, 24);
	{
		wxMemoryDC dc(bmp);
		list->paint(dc);
	}
	return bmp.SaveFile(path, wxBITMAP_TYPE_PNG);
}

void ShowShortcutsSheet(MainFrame* frame) {
	if (frame == nullptr) return;
	const std::vector<Shortcut> all = allShortcuts();

	// As tall as it comfortably can be on this screen, never taller: the list
	// scrolls inside it.
	int displayIndex = wxDisplay::GetFromWindow(frame);
	if (displayIndex == wxNOT_FOUND) displayIndex = 0;
	const wxRect screen = wxDisplay(displayIndex).GetClientArea();
	const wxSize size(std::min(880, screen.width - 80), std::min(720, screen.height - 100));

	wxDialog dlg(frame, wxID_ANY, "Keyboard Shortcuts", wxDefaultPosition, size,
	             wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	dlg.SetBackgroundColour(ui::paper());
	dlg.SetMinSize(wxSize(420, 360));
	wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* headRow = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* titles = new wxBoxSizer(wxVERTICAL);
	wxStaticText* title = new wxStaticText(&dlg, wxID_ANY, "Keyboard Shortcuts");
	title->SetFont(wxFont(wxFontInfo(19).Bold()));
	title->SetForegroundColour(ui::ink());
	titles->Add(title);
	wxStaticText* sub = new wxStaticText(&dlg, wxID_ANY,
		"Type to search. Click one that has a menu command, or pick it and press Return, to do it now.");
	sub->SetForegroundColour(ui::dim());
	titles->Add(sub, 0, wxTOP, 4);
	headRow->Add(titles, 1, wxALIGN_CENTER_VERTICAL);
	top->Add(headRow, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, 22);

	wxSearchCtrl* search = new wxSearchCtrl(&dlg, wxID_ANY);
	search->ShowCancelButton(true);
	search->SetDescriptiveText("Search shortcuts (try \"zoom\" or \"tab\")");
	top->Add(search, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, 22);

	SheetList* list = new SheetList(&dlg);
	top->Add(list, 1, wxALL | wxEXPAND, 10);
	dlg.SetSizer(top);

	int chosen = 0;
	list->onRun = [&](int command) { chosen = command; dlg.EndModal(wxID_OK); };

	auto refilter = [&]() {
		const wxString q = search->GetValue().Lower().Strip(wxString::both);
		std::vector<const Shortcut*> items;
		for (const Shortcut& s : all) {
			if (q.empty()) { items.push_back(&s); continue; }
			// Every word typed has to appear somewhere.
			bool ok = true;
			const wxString hay = searchText(s);
			wxString rest = q;
			while (ok && !rest.empty()) {
				const wxString word = rest.BeforeFirst(' ');
				rest = rest.AfterFirst(' ');
				if (!word.empty() && !hay.Contains(word)) ok = false;
			}
			if (ok) items.push_back(&s);
		}
		list->SetItems(items);
	};
	search->Bind(wxEVT_TEXT, [&](wxCommandEvent&) { refilter(); });

	dlg.Bind(wxEVT_CHAR_HOOK, [&](wxKeyEvent& e) {
		switch (e.GetKeyCode()) {
			case WXK_ESCAPE:
				if (!search->GetValue().empty()) search->Clear();
				else dlg.EndModal(wxID_CANCEL);
				return;
			case WXK_DOWN:     list->Move(1); return;
			case WXK_UP:       list->Move(-1); return;
			case WXK_PAGEDOWN: list->Move(8); return;
			case WXK_PAGEUP:   list->Move(-8); return;
			case WXK_RETURN:
			case WXK_NUMPAD_ENTER:
				if (const int c = list->SelectedCommand()) { chosen = c; dlg.EndModal(wxID_OK); }
				else wxBell();
				return;
			default: e.Skip();
		}
	});

	refilter();
	dlg.CentreOnParent();
	search->SetFocus();
	dlg.ShowModal();

	// After the sheet is gone, so the command acts on the window behind it.
	if (chosen != 0) frame->CallAfter([frame, chosen] { runCommand(frame, chosen); });
}
