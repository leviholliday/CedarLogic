/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                    Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.

   OscopeFrame: Docked panel for the Oscope
*****************************************************************************/

#include "MainApp.h"
#include "EmbeddedRes.h"
#include "ToolbarIcons.h"
#include "OscopeFrame.h"
#include "wx/filedlg.h"
#include "wx/menu.h"
#include "wx/settings.h"
#include "GUICircuit.h"
#include "wx/clipbrd.h"
#include "wx/bmpbndl.h"
#include "wx/file.h"
#include <fstream>
#include <iomanip>

#include "UiKit.h"
#include "ModernToolbar.h"
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <memory>
#ifdef __APPLE__
#include "NativeIcons.h"
#endif

#ifdef __WXMSW__
namespace {

// The oscilloscope's header on Windows: its name and its tools as quiet icon
// buttons, drawn like the main toolbar. The stock toolbar there was a strip
// of tiny grey Windows 95 buttons. Each button sends the same wxEVT_TOOL the
// stock one did, so the handlers are unchanged.
class OscopeBar : public wxPanel {
public:
	struct Tool { int id; const char* icon; const char* tip; bool toggle; wxRect rect; };

	OscopeBar(wxWindow* parent, wxToolBar* stock) : wxPanel(parent), stock(stock) {
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		SetMinSize(wxSize(-1, FromDIP(38)));
		tools = {
			{ ID_OSCOPE_PAUSE,  "pause", "Pause (click again to reset)", true, wxRect() },
			{ ID_OSCOPE_ADD,    "plus",  "Add a signal", false, wxRect() },
			{ ID_OSCOPE_REMOVE, "minus", "Remove the selected signal", false, wxRect() },
			{ ID_OSCOPE_EXPORT, "copy",  "Copy the trace as a picture", false, wxRect() },
			{ ID_OSCOPE_LOAD,   "open",  "Load a saved layout", false, wxRect() },
			{ ID_OSCOPE_SAVE,   "save",  "Save this layout", false, wxRect() },
		};
		Bind(wxEVT_PAINT, &OscopeBar::OnPaint, this);
		Bind(wxEVT_SIZE, [this](wxSizeEvent& e) { Refresh(); e.Skip(); });
		Bind(wxEVT_MOTION, [this](wxMouseEvent& e) {
			const int h = at(e.GetPosition());
			if (h != hot) {
				hot = h;
				SetCursor(h >= 0 ? wxCursor(wxCURSOR_HAND) : wxNullCursor);
				UnsetToolTip();
				if (h >= 0) SetToolTip(tools[h].tip);
				Refresh();
			}
		});
		Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { hot = -1; Refresh(); });
		Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
			const int i = at(e.GetPosition());
			if (i < 0) return;
			const Tool& t = tools[i];
			// The stock bar still holds the pause state the handlers read.
			if (t.toggle) this->stock->ToggleTool(t.id, !this->stock->GetToolState(t.id));
			wxCommandEvent ev(wxEVT_TOOL, t.id);
			ev.SetEventObject(this);
			GetParent()->ProcessWindowEvent(ev);
			Refresh();
		});
	}

private:
	int at(const wxPoint& p) const {
		for (size_t i = 0; i < tools.size(); i++) if (tools[i].rect.Contains(p)) return (int)i;
		return -1;
	}

	void OnPaint(wxPaintEvent&) {
		wxAutoBufferedPaintDC dc(this);
		dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
		dc.Clear();
		std::unique_ptr<wxGraphicsContext> gc(ui::graphics(dc));
		if (!gc) return;
		const wxSize sz = GetClientSize();
		const wxColour in = ui::ink();
		gc->SetPen(wxPen(ui::withAlpha(in, 0.08), 1));
		gc->StrokeLine(0, 0.5, sz.x, 0.5);
		gc->SetFont(wxFont(wxFontInfo(10).Bold()), in);
		double tw, th;
		gc->GetTextExtent("Oscilloscope", &tw, &th);
		gc->DrawText("Oscilloscope", FromDIP(12), (sz.y - th) / 2);

		const int b = FromDIP(30);
		int x = FromDIP(12) + (int)tw + FromDIP(16);
		const double scale = GetContentScaleFactor();
		for (size_t i = 0; i < tools.size(); i++) {
			Tool& t = tools[i];
			if (i == 1 || i == 3) x += FromDIP(10);   // pause | signals | files
			t.rect = wxRect(x, (sz.y - b) / 2, b, b);
			x += b + FromDIP(2);
			const bool on = t.toggle && stock->GetToolState(t.id);
			if ((int)i == hot || on) {
				gc->SetPen(*wxTRANSPARENT_PEN);
				gc->SetBrush(wxBrush(on ? ui::withAlpha(ui::accent(), 0.22) : ui::withAlpha(in, 0.08)));
				gc->DrawRoundedRectangle(t.rect.x, t.rect.y, t.rect.width, t.rect.height, FromDIP(6));
			}
			const char* name = (t.toggle && on) ? "play" : t.icon;
			const wxBitmap ic = cl::tb::ToolIcon(name, on ? ui::accent() : ui::withAlpha(in, 0.85), 16, scale);
			if (ic.IsOk()) {
				const double s = FromDIP(16);
				gc->DrawBitmap(ic, t.rect.x + (b - s) / 2, t.rect.y + (b - s) / 2, s, s);
			}
		}
	}

	wxToolBar* stock;
	std::vector<Tool> tools;
	int hot = -1;
};

}  // namespace
#endif

OscopeFrame::OscopeFrame(wxWindow *parent, GUICircuit* gCircuit)
       : wxPanel(parent, wxID_ANY)
{
	this->gCircuit = gCircuit;
	paused = false;

	oSizer = new wxBoxSizer( wxVERTICAL );

	// Create the toolbar
	oscopeToolBar = new wxToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTB_HORIZONTAL | wxTB_FLAT | wxTB_NODIVIDER);

	// Same arrangement as the main toolbar, smaller: each button names its icon
	// the Apple way and ours, and cl::toolbarIcon picks. No SetToolBitmapSize,
	// for the reason spelled out over in MainFrame's toolbar setup.
	auto icon = [&](const char* sfSymbol, const char* svgName) {
		return cl::toolbarIcon(oscopeToolBar, sfSymbol, 15, svgName, wxSize(18, 18));
	};

	oscopeToolBar->AddTool(ID_OSCOPE_PAUSE, "Pause", icon("pause.fill", "pause"), "Pause/Reset", wxITEM_CHECK);
	oscopeToolBar->AddSeparator();
	oscopeToolBar->AddTool(ID_OSCOPE_ADD, "Add Signal", icon("plus", "plus"), "Add signal");
	oscopeToolBar->AddTool(ID_OSCOPE_REMOVE, "Remove Signal", icon("minus", "minus"), "Remove selected signal");
	oscopeToolBar->AddSeparator();
	oscopeToolBar->AddTool(ID_OSCOPE_EXPORT, "Export", icon("doc.on.clipboard", "copy"), "Export to clipboard");
	oscopeToolBar->AddTool(ID_OSCOPE_LOAD, "Load", icon("folder", "open"), "Load layout");
	oscopeToolBar->AddTool(ID_OSCOPE_SAVE, "Save", icon("square.and.arrow.down", "save"), "Save layout");

	oscopeToolBar->Realize();
#ifdef __WXOSX__
	// Set up both normal and alternate (checked) SF Symbol images on the
	// native NSButton so macOS handles the toggle automatically.
	NativeIcon_ConfigureEmbeddedToggleTool(oscopeToolBar, ID_OSCOPE_PAUSE,
		"pause.fill", "arrow.trianglehead.counterclockwise", 15);
#endif
#ifdef __WXMSW__
	oscopeToolBar->Hide();   // kept for its pause state; OscopeBar is the face
	oSizer->Add(new OscopeBar(this, oscopeToolBar), wxSizerFlags(0).Expand());
#else
	oSizer->Add(oscopeToolBar, wxSizerFlags(0).Expand());
#endif

	// Create horizontal sizer for signal list + canvas
	wxBoxSizer* contentSizer = new wxBoxSizer( wxHORIZONTAL );

#ifdef __WXMSW__
	signalList = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(140, -1), 0, nullptr,
	                           wxLB_SINGLE | wxBORDER_NONE);
#else
	signalList = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(120, -1), 0, nullptr, wxLB_SINGLE);
#endif
	contentSizer->Add(signalList, wxSizerFlags(0).Expand().Border(wxALL, 2));

#ifdef __WXMSW__
	// No sunken 3-D edge: that border is the most Windows 95 thing there is.
	theCanvas = new OscopeCanvas(this, gCircuit, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS | wxBORDER_NONE);
#else
	theCanvas = new OscopeCanvas(this, gCircuit, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS|wxSUNKEN_BORDER);
#endif
	contentSizer->Add(theCanvas, wxSizerFlags(1).Expand());

	oSizer->Add(contentSizer, wxSizerFlags(1).Expand());
	SetSizer(oSizer);

	// Bind events
	Bind(wxEVT_TOOL, &OscopeFrame::OnPauseToggle, this, ID_OSCOPE_PAUSE);
	Bind(wxEVT_TOOL, &OscopeFrame::OnAddSignal, this, ID_OSCOPE_ADD);
	Bind(wxEVT_TOOL, &OscopeFrame::OnRemoveSignal, this, ID_OSCOPE_REMOVE);
	Bind(wxEVT_TOOL, &OscopeFrame::OnExport, this, ID_OSCOPE_EXPORT);
	Bind(wxEVT_TOOL, &OscopeFrame::OnLoad, this, ID_OSCOPE_LOAD);
	Bind(wxEVT_TOOL, &OscopeFrame::OnSave, this, ID_OSCOPE_SAVE);
	ApplyTheme();
}

void OscopeFrame::ApplyTheme() {
#ifdef __WXMSW__
	const bool dark = ui::isDark();
	SetBackgroundColour(dark ? wxColour(22, 24, 28) : wxColour(233, 234, 238));
	signalList->SetBackgroundColour(dark ? wxColour(28, 31, 37) : wxColour(250, 250, 252));
	signalList->SetForegroundColour(ui::ink());
	Refresh();
#endif
}

void OscopeFrame::UpdateData(void){
	if (!paused) {
		theCanvas->UpdateData();
	}
}

void OscopeFrame::UpdateMenu(void){
	theCanvas->UpdateMenu();
}

void OscopeFrame::RefreshCanvas(void){
	theCanvas->Refresh();
}

void OscopeFrame::OnPauseToggle( wxCommandEvent& event ){
	paused = oscopeToolBar->GetToolState(ID_OSCOPE_PAUSE);
	if (!paused) {
		theCanvas->clearData();
	}
}

void OscopeFrame::OnAddSignal( wxCommandEvent& event ){
	if (availableFeeds.empty()) return;

	wxMenu menu;
	for (unsigned int i = 0; i < availableFeeds.size(); ++i) {
		menu.Append(ID_OSCOPE_SIGNAL_MENU_BASE + i, availableFeeds[i]);
	}

	menu.Bind(wxEVT_MENU, &OscopeFrame::OnSignalMenuSelect, this);

	wxPoint pos = oscopeToolBar->GetPosition();
	PopupMenu(&menu, pos.x, pos.y + oscopeToolBar->GetSize().GetHeight());
}

void OscopeFrame::OnSignalMenuSelect( wxCommandEvent& event ){
	int idx = event.GetId() - ID_OSCOPE_SIGNAL_MENU_BASE;
	if (idx >= 0 && idx < (int)availableFeeds.size()) {
		string name = availableFeeds[idx];
		// Don't add duplicates
		for (unsigned int i = 0; i < feedNames.size(); ++i) {
			if (feedNames[i] == name) return;
		}
		appendNewFeed(name);
		theCanvas->UpdateMenu();
	}
}

void OscopeFrame::OnRemoveSignal( wxCommandEvent& event ){
	int sel = signalList->GetSelection();
	if (sel == wxNOT_FOUND) return;
	removeFeed(sel);
	theCanvas->UpdateMenu();
}

void OscopeFrame::OnExport( wxCommandEvent& event ){
	wxSize canvasSize = theCanvas->GetClientSize();
	wxImage circuitImage = theCanvas->generateImage();
	wxBitmap circuitBitmap(circuitImage);

	int labelAreaWidth = 100;
	int totalWidth = labelAreaWidth + canvasSize.GetWidth();
	int totalHeight = canvasSize.GetHeight();

	wxMemoryDC memDC;
	wxBitmap labelBitmap(totalWidth, totalHeight);
	memDC.SelectObject(labelBitmap);
	memDC.SetBackground(*wxWHITE_BRUSH);
	memDC.Clear();
	wxFont font(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
	memDC.SetFont(font);
	memDC.SetTextForeground(*wxBLACK);
	memDC.SetTextBackground(*wxWHITE);
	for (unsigned int i = 0; i < numberOfFeeds(); ++i) {
		memDC.DrawText(getFeedName(i), wxPoint(5, getFeedYPos(i)));
	}
	memDC.DrawBitmap(circuitBitmap, labelAreaWidth, 0, false);
	memDC.SelectObject(wxNullBitmap);

	if (wxTheClipboard->Open()) {
		wxTheClipboard->SetData(new wxBitmapDataObject(labelBitmap));
		wxTheClipboard->Close();
	}
}

void OscopeFrame::OnLoad( wxCommandEvent& event ){
	wxString caption = "Open an O-scope Layout";
	wxString wildcard = "CEDAR O-scope Layout files (*.cdo)|*.cdo";
	wxString defaultFilename = "";
	wxFileDialog dialog(this, caption, wxEmptyString, defaultFilename, wildcard, wxFD_OPEN | wxFD_FILE_MUST_EXIST);

	if (dialog.ShowModal() == wxID_OK) {
		wxString path = dialog.GetPath();
		ifstream inFile(path.ToStdString());
		string lineFile;
		getline(inFile, lineFile, '\n');
		if (lineFile != "OSCOPE LAYOUT FILE") return;
		unsigned int numLines = 0;
		inFile >> numLines;
		getline(inFile, lineFile, '\n');

		// Remove old feeds
		feedNames.clear();
		signalList->Clear();

		for (unsigned int i = 0; i < numLines; i++) {
			getline(inFile, lineFile, '\n');
			if (lineFile != NONE_STR) {
				appendNewFeed(lineFile);
			}
		}
		Layout();
		theCanvas->UpdateMenu();
		theCanvas->clearData();
	}
}

void OscopeFrame::OnSave( wxCommandEvent& event ){
	wxString caption = "Save o-scope layout";
	wxString wildcard = "CEDAR O-scope Layout files (*.cdo)|*.cdo";
	wxString defaultFilename = "";
	wxFileDialog dialog(this, caption, wxEmptyString, defaultFilename, wildcard, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
	if (dialog.ShowModal() == wxID_OK) {
		wxString path = dialog.GetPath();
		string openedFilename = path.ToStdString();
		ofstream outFile(openedFilename);
		outFile << "OSCOPE LAYOUT FILE" << endl;
		outFile << numberOfFeeds() << " : following lines are order of inputs" << endl;
		for (unsigned int i = 0; i < numberOfFeeds(); i++) outFile << getFeedName(i) << endl;
		outFile.close();
	}
}

void OscopeFrame::appendNewFeed( string newName ){
	if (newName == NONE_STR) return;
	feedNames.push_back(newName);
	signalList->Append(newName);
}

void OscopeFrame::setFeedName( int i, string newName ){
	if (i < 0 || i >= (int)feedNames.size()) return;
	feedNames[i] = newName;
	signalList->SetString(i, newName);
}

unsigned int OscopeFrame::numberOfFeeds(){
	return feedNames.size();
}

void OscopeFrame::removeFeed( int i ){
	if (i < 0 || i >= (int)feedNames.size()) return;
	feedNames.erase(feedNames.begin() + i);
	signalList->Delete(i);
}

string OscopeFrame::getFeedName( int i ){
	if (i < 0 || i >= (int)feedNames.size()) return NONE_STR;
	return feedNames[i];
}

void OscopeFrame::cancelFeed( int i ){
	removeFeed(i);
}

int OscopeFrame::getFeedYPos( int i ){
	if (numberOfFeeds() == 0) return 0;
	// Match the coordinate mapping OscopeCanvas::OnRenderSkia builds:
	//   world box (0, -0.25) .. (OSCOPE_HORIZONTAL, numberOfWires * 1.5)
	// Wire i occupies world y range [i*1.5, i*1.5+1], center at i*1.5+0.5
	// top = -0.25, bottom = n * 1.5
	// pixelY = (glY + 0.25) / (n * 1.5 + 0.25) * canvasHeight
	wxSize canvasSize = theCanvas->GetClientSize();
	int canvasHeight = canvasSize.GetHeight();
	if (canvasHeight <= 0) canvasHeight = 200;

	unsigned int n = numberOfFeeds();
	double glY = i * 1.5 + 0.5;
	return (int)((glY + 0.25) / (n * 1.5 + 0.25) * canvasHeight);
}

void OscopeFrame::updatePossableFeeds( vector< string >* newPossabilities ){
	availableFeeds = *newPossabilities;

	// Remove any active feeds that are no longer valid
	for (int i = (int)feedNames.size() - 1; i >= 0; --i) {
		bool found = false;
		for (unsigned int j = 0; j < availableFeeds.size(); ++j) {
			if (availableFeeds[j] == feedNames[i]) {
				found = true;
				break;
			}
		}
		if (!found) {
			removeFeed(i);
		}
	}
}
