/*****************************************************************************
   Project: CEDAR Logic Simulator
   TruthTableDialog: shows a truth table generated from the circuit's
   switches (inputs) and lights (outputs). Column names are editable.
*****************************************************************************/

#ifndef TRUTHTABLEDIALOG_H_
#define TRUTHTABLEDIALOG_H_

#include <vector>
#include <wx/string.h>

class wxWindow;

struct TruthTableData {
	std::vector<wxString> inputNames;
	std::vector<wxString> outputNames;
	// One entry per row: input values then output values, each '0', '1', or
	// a problem marker ('X' unknown, 'Z' floating, '!' conflict, '-' unconnected).
	std::vector<std::vector<char>> rows;
	bool sequential = false;   // clocks / flip-flops: rows depend on prior state
	int unsettledRows = 0;     // rows where the circuit never stopped changing
};

// Modal. Column names the user edits are written back into `data`.
void ShowTruthTableDialog(wxWindow* parent, TruthTableData& data);

#endif
