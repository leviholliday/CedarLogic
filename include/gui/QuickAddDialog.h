#ifndef QUICKADDDIALOG_H_
#define QUICKADDDIALOG_H_

#include "MainApp.h"
#include "LibraryParse.h"
#include <vector>
#include <string>
#include <map>
#include <wx/bitmap.h>

using namespace std;

class wxSearchCtrl;
class GateResultList;

// The "A" picker: type a few letters, arrow to the gate you want, Enter to
// pick it up. Each result carries a drawing of the gate, so you can recognise
// one without knowing its name.
class QuickAddDialog : public wxDialog {
public:
	QuickAddDialog(wxWindow* parent);

	string getSelectedGate() const { return selectedGate; }

	// The gate's drawing at this size, rendered once and kept.
	wxBitmap previewFor(const string& gateName, int size);

private:
	void updateList(const string& query);
	void confirm();
	int fuzzyScore(const string& query, const string& target);
	wxBitmap renderGatePreview(const string& gateName, int width, int height);

	wxSearchCtrl* searchField;
	GateResultList* resultList;
	string selectedGate;

	struct GateEntry {
		string gateName;
		string caption;
		string libraryName;
	};
	vector<GateEntry> allGates;
	map<string, wxBitmap> previewCache;
};

#endif
