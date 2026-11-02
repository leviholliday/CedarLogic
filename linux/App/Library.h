// Your Circuits: where circuits live (the Mac app's Library, LibraryView.swift).
//
// The library is the same one the wx app keeps, in ~/.local/share/CedarLogic/Library
// -- one folder per circuit holding name.txt, circuit.cdl and
// versions/<timestamp>.cdl -- so a circuit saved in one shows up in the other.
// Circuits save themselves as you go; a version is kept on Ctrl+S, after a
// break, and every half hour of work. A .cdl file opened from elsewhere comes
// in as a copy (the file itself is left alone); Export writes one out.
// (The Windows app's Library, on GLib.)

#ifndef CL_LINUX_LIBRARY_H
#define CL_LINUX_LIBRARY_H

#include "App.h"
#include <string>
#include <vector>

namespace library {

struct Item {
	std::string id, name, folder;
	double modified = 0;   // seconds since 1970
	std::string circuit() const { return folder + "/circuit.cdl"; }
	std::string versionsFolder() const { return folder + "/versions"; }
};

struct Version {
	std::string path;
	double time = 0;
};

std::string root();
// Newest first.
std::vector<Item> items();
// The library circuit a file is, if it's one.
bool itemFor(const std::string& path, Item& out);
bool contains(const std::string& path);
std::vector<Version> versions(const Item& item);

// A new circuit holding `text`; `source` is the file it was imported from,
// if any, so opening that file again finds it.
bool create(const std::string& name, const std::string& text, const std::string& source, Item& out);
// The circuit already imported from a file as it is now, if there is one.
bool imported(const std::string& file, Item& out);
void rename(const Item& item, const std::string& name);
// Into the library's own trash folder (the wx app's too).
bool moveToTrash(const Item& item);

// A library circuit was saved: keep a version when it's due (or now, if
// `explicitSave`), and thin out old ones. True when a version was kept.
bool noteSaved(const std::string& path, bool explicitSave);
// Put a version back as the circuit, keeping the current one as a version.
bool restore(const Item& item, const Version& v);

// For the lists: "Today at 3:42 PM", "12 min ago", "8 gates".
std::string friendlyTime(double t);
std::string agoText(double t);
int gateCount(const std::string& path);

// The circuit you were last in (reopened at launch).
std::string lastCircuit();
void noteLastCircuit(const std::string& path);

}  // namespace library

#endif  // CL_LINUX_LIBRARY_H
