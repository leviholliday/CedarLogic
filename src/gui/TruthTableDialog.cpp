/*****************************************************************************
   Project: CEDAR Logic Simulator
   TruthTableDialog: shows a truth table generated from the circuit.
*****************************************************************************/

#include "UiControls.h"
#include "TruthTableDialog.h"
#include "RenderMode.h"

#include <wx/dialog.h>
#include <wx/grid.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/textdlg.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/filedlg.h>
#include <wx/wfstream.h>
#include <wx/txtstrm.h>
#include <wx/settings.h>
#include <wx/msgdlg.h>
#include <algorithm>

namespace {

wxString columnName(const TruthTableData& d, int col) {
	const int n = (int)d.inputNames.size();
	return col < n ? d.inputNames[col] : d.outputNames[col - n];
}

void setColumnName(TruthTableData& d, int col, const wxString& name) {
	const int n = (int)d.inputNames.size();
	if (col < n) d.inputNames[col] = name;
	else d.outputNames[col - n] = name;
}

// Tab-separated with a header row: pastes as a real table into Word, Excel,
// Google Docs and Sheets.
wxString asTabbedText(const TruthTableData& d) {
	const int cols = (int)(d.inputNames.size() + d.outputNames.size());
	wxString out;
	for (int c = 0; c < cols; c++) out << (c ? "\t" : "") << columnName(d, c);
	out << "\n";
	for (const auto& row : d.rows) {
		for (int c = 0; c < cols; c++) out << (c ? "\t" : "") << wxString(1, row[c]);
		out << "\n";
	}
	return out;
}

} // namespace

void ShowTruthTableDialog(wxWindow* parent, TruthTableData& data) {
	const bool dark = renderMode().darkMode;
	const int n = (int)data.inputNames.size();
	const int m = (int)data.outputNames.size();
	const int cols = n + m;

	wxDialog dlg(parent, wxID_ANY, "Truth Table", wxDefaultPosition, wxDefaultSize,
	             wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);

	wxString summary;
	summary << n << (n == 1 ? " input, " : " inputs, ") << m << (m == 1 ? " output, " : " outputs, ")
	        << data.rows.size() << " rows";
	wxStaticText* summaryText = new wxStaticText(&dlg, wxID_ANY, summary);
	top->Add(summaryText, 0, wxLEFT | wxRIGHT | wxTOP, 16);

	wxStaticText* hint = new wxStaticText(&dlg, wxID_ANY, "Double-click a column name to rename it.");
	hint->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
	top->Add(hint, 0, wxLEFT | wxRIGHT | wxTOP, 16);
	top->AddSpacer(4);

	if (data.sequential || data.unsettledRows > 0) {
		wxString warn;
		if (data.unsettledRows > 0)
			warn << data.unsettledRows << (data.unsettledRows == 1 ? " row never" : " rows never")
			     << " settled (the outputs kept changing). ";
		if (data.sequential)
			warn << "This circuit has a clock or memory (flip-flops, registers), so outputs can depend "
			        "on what happened before, not just on the switches.";
		wxStaticText* w = new wxStaticText(&dlg, wxID_ANY, warn);
		w->SetForegroundColour(dark ? wxColour(255, 190, 90) : wxColour(170, 100, 0));
		w->Wrap(460);
		top->Add(w, 0, wxLEFT | wxRIGHT | wxTOP, 16);
	}

	wxGrid* grid = new wxGrid(&dlg, wxID_ANY);
	grid->CreateGrid((int)data.rows.size(), cols);
	grid->EnableEditing(false);
	grid->EnableDragRowSize(false);
	grid->EnableDragGridSize(false);
	grid->SetRowLabelSize(0);
	grid->UseNativeColHeader(false);
	grid->SetDefaultCellAlignment(wxALIGN_CENTRE, wxALIGN_CENTRE);
	grid->SetColLabelAlignment(wxALIGN_CENTRE, wxALIGN_CENTRE);
	grid->SetDefaultRowSize(24, true);
	grid->SetColLabelSize(28);

	// Inputs on a neutral background, outputs tinted, so the two halves read
	// apart at a glance; 1s bold.
	const wxColour bg     = dark ? wxColour(28, 31, 37)  : wxColour(255, 255, 255);
	const wxColour inBg   = dark ? wxColour(34, 38, 45)  : wxColour(246, 247, 249);
	const wxColour outBg  = dark ? wxColour(26, 44, 60)  : wxColour(234, 243, 255);
	const wxColour ink    = dark ? wxColour(225, 230, 238) : wxColour(20, 22, 26);
	const wxColour faint  = dark ? wxColour(120, 128, 140) : wxColour(150, 155, 162);
	const wxColour warnC  = dark ? wxColour(255, 170, 80) : wxColour(190, 90, 0);
	grid->SetDefaultCellBackgroundColour(bg);
	grid->SetDefaultCellTextColour(ink);
	grid->SetGridLineColour(dark ? wxColour(52, 57, 66) : wxColour(226, 229, 234));
	grid->SetLabelBackgroundColour(dark ? wxColour(40, 44, 52) : wxColour(236, 238, 242));
	grid->SetLabelTextColour(ink);
	wxFont labelFont = grid->GetLabelFont();
	labelFont.SetWeight(wxFONTWEIGHT_BOLD);
	grid->SetLabelFont(labelFont);
	wxFont cellFont = grid->GetDefaultCellFont();
	cellFont.SetFamily(wxFONTFAMILY_TELETYPE);
	grid->SetDefaultCellFont(cellFont);
	wxFont oneFont = cellFont;
	oneFont.SetWeight(wxFONTWEIGHT_BOLD);

	for (int c = 0; c < cols; c++) {
		grid->SetColLabelValue(c, columnName(data, c));
		grid->SetColSize(c, 64);
	}
	for (int r = 0; r < (int)data.rows.size(); r++) {
		for (int c = 0; c < cols; c++) {
			const char v = data.rows[r][c];
			grid->SetCellValue(r, c, wxString(1, v));
			grid->SetCellBackgroundColour(r, c, c < n ? inBg : outBg);
			if (v == '1') grid->SetCellFont(r, c, oneFont);
			else if (v == '0') grid->SetCellTextColour(r, c, faint);
			else grid->SetCellTextColour(r, c, warnC);
		}
	}

	grid->Bind(wxEVT_GRID_LABEL_LEFT_DCLICK, [&](wxGridEvent& e) {
		const int c = e.GetCol();
		if (c < 0) return;
		wxTextEntryDialog ask(&dlg, "Column name:", "Rename Column", columnName(data, c));
		if (ask.ShowModal() != wxID_OK) return;
		const wxString name = ask.GetValue().Strip(wxString::both);
		if (name.empty()) return;
		setColumnName(data, c, name);
		grid->SetColLabelValue(c, name);
	});

	// Tall tables scroll; short ones fit without empty space.
	const int visibleRows = std::min<int>((int)data.rows.size(), 16);
	grid->SetMinSize(wxSize(cols * 64 + 4, 28 + visibleRows * 24 + 4));
	top->Add(grid, 1, wxALL | wxEXPAND, 16);

	wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
	wxButton* copyBtn = new wxButton(&dlg, wxID_ANY, "Copy");
	copyBtn->SetToolTip("Copies as a table you can paste into Word, Excel, or Google Docs");
	wxButton* csvBtn = new wxButton(&dlg, wxID_ANY, "Save as CSV...");
	wxButton* closeBtn = new wxButton(&dlg, wxID_OK, "Done");
	closeBtn->SetDefault();
	buttons->Add(copyBtn, 0, wxRIGHT, 8);
	buttons->Add(csvBtn, 0);
	buttons->AddStretchSpacer(1);
	buttons->Add(closeBtn, 0);
	top->Add(buttons, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 16);

	copyBtn->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
		if (!wxTheClipboard->Open()) return;
		wxTheClipboard->SetData(new wxTextDataObject(asTabbedText(data)));
		wxTheClipboard->Close();
		copyBtn->SetLabel("Copied");
	});
	csvBtn->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
		wxFileDialog save(&dlg, "Save Truth Table", wxEmptyString, "truth table.csv",
		                  "CSV (*.csv)|*.csv", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
		if (save.ShowModal() != wxID_OK) return;
		wxFileOutputStream file(save.GetPath());
		if (!file.IsOk()) { ui::Message("Couldn't write that file.", "Truth Table", wxOK | wxICON_ERROR, &dlg); return; }
		wxTextOutputStream out(file);
		wxString text = asTabbedText(data);
		text.Replace("\t", ",");
		out << text;
	});

	dlg.SetSizerAndFit(top);
	dlg.CentreOnParent();
	// Escape closes it, like every other window in the app. ("Done" is a
	// wxID_OK button, which Escape would not otherwise reach.)
	dlg.Bind(wxEVT_CHAR_HOOK, [&dlg](wxKeyEvent& e) {
		if (e.GetKeyCode() == WXK_ESCAPE) dlg.EndModal(wxID_OK);
		else e.Skip();
	});
	dlg.ShowModal();
}
