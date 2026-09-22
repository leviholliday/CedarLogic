/*****************************************************************************
   Project: CEDAR Logic Simulator
   LibraryDialogs: the Library window (your saved circuits) and the Version
   History window for one circuit.
*****************************************************************************/

#ifndef LIBRARYDIALOGS_H_
#define LIBRARYDIALOGS_H_

#include <string>
#include <wx/string.h>

class wxWindow;

// What the Library window asked for.
struct LibraryChoice {
	enum { None, Open, Import } action = None;
	std::string id;   // for Open
};

// `currentId` is the circuit on screen (it can't be deleted from here).
LibraryChoice ShowLibraryDialog(wxWindow* parent, const std::string& currentId);

// The path of the version to restore, or empty if none was chosen.
wxString ShowVersionHistoryDialog(wxWindow* parent, const std::string& id);

#endif
