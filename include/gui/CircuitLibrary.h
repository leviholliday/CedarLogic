/*****************************************************************************
   Project: CEDAR Logic Simulator
   CircuitLibrary: circuits kept inside the app, with version history.

   Each circuit is a folder under <user data>/Library/<id>/:
     circuit.cdl        the current version (what's opened and autosaved)
     name.txt           its display name
     versions/<t>.cdl   snapshots, named by the time <t> they were taken
   Deleting moves the folder to Library/.Trash rather than erasing it.
*****************************************************************************/

#ifndef CIRCUITLIBRARY_H_
#define CIRCUITLIBRARY_H_

#include <string>
#include <vector>
#include <wx/string.h>
#include <wx/datetime.h>

namespace library {

struct Doc {
	std::string id;
	wxString name;
	wxDateTime modified;
};

struct Version {
	wxString path;
	wxDateTime when;
};

wxString root();
wxString circuitPath(const std::string& id);
bool exists(const std::string& id);

// A new, empty entry (no circuit.cdl yet) named `name`. Returns its id.
std::string create(const wxString& name);
wxString name(const std::string& id);
void rename(const std::string& id, const wxString& newName);
// "Untitled", then "Untitled 2", "Untitled 3", ... whichever is free.
wxString nextUntitledName();

// Every circuit, most recently changed first.
std::vector<Doc> list();

// Copy the current circuit.cdl into versions/, then thin out old versions:
// everything from the last day, one per hour for the last week, one per day
// before that.
void snapshot(const std::string& id);
// Newest first.
std::vector<Version> versions(const std::string& id);

void remove(const std::string& id);

// On first run, put the built-in practice circuit (res/samples/practice.cdl,
// three tabs of gates to play with) into the library.
void seedSamples();

}  // namespace library

#endif
