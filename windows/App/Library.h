// Your Circuits: where circuits live (the Mac app's Library, LibraryView.swift).
//
// The library is the same one the wx app keeps, in %APPDATA%\CedarLogic\Library
// -- one folder per circuit holding name.txt, circuit.cdl and
// versions\<timestamp>.cdl -- so a circuit saved in one shows up in the other.
// Circuits save themselves as you go; a version is kept on Ctrl+S, after a
// break, and every half hour of work. A .cdl file opened from elsewhere comes
// in as a copy (the file itself is left alone); Export writes one out.

#ifndef CL_WINDOWS_LIBRARY_H
#define CL_WINDOWS_LIBRARY_H

#include "App.h"
#include <functional>
#include <string>
#include <vector>

namespace library {

struct Item {
	std::string id, name, folder;
	double modified = 0;   // seconds since 1970
	std::string circuit() const { return folder + "\\circuit.cdl"; }
	std::string versionsFolder() const { return folder + "\\versions"; }
};

struct Version {
	std::string path;
	double time = 0;
	std::string note;      // why it was kept, when sync kept it ("From Levi’s iPhone · edited 4 Oct 10:31"); else empty
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
// Into the library's own trash folder (the wx app's too), dated (deleted.txt).
bool moveToTrash(const Item& item);

// Recently Deleted (docs/RECENTLY-DELETED.md, the Mac's RecentlyDeleted.swift):
// the circuits in <library>\.Trash, kept 30 days, then swept. `id` is the
// folder's name there ("<id>", or "<id> 2" for a second one). The
// "repeated versions" folder (no circuit.cdl) is never listed or swept.
const int kKeepDeletedDays = 30;
struct Deleted {
	std::string id, name, folder;
	double deleted = 0;   // seconds since 1970
	std::string circuit() const { return folder + "\\circuit.cdl"; }
};
// Newest first. One with no date (sync, the wx app, an older version put it
// there) is dated the first time it's listed: when it was moved there if
// that's within 30 days, else now.
std::vector<Deleted> deletedItems(const std::string& libraryRoot = std::string(), double nowTime = 0);
// Those deleted more than 30 days ago go for good; how many went.
int sweepDeleted(const std::string& libraryRoot = std::string(), double nowTime = 0);
// Back into Your Circuits under its own id (a new one if that's taken), at
// the top of the list. Its library id, or "" if it couldn't be moved.
std::string restoreDeleted(const Deleted& d);
// Gone for good, now.
bool deleteNow(const Deleted& d);
// Writes deleted.txt in a trashed circuit's folder (ISO 8601, UTC).
void stampDeleted(const std::string& folder, double when);
// --trash-check: the Mac's launch-selftest `trash` state, in a library of
// its own: the list and its dates, the 30-day sweep, Restore (its own id, or
// a new one when taken) and Delete Now. Returns how many failed.
int trashSelfTest(std::function<void(const std::string&)> say);

// A library circuit was saved: keep a version when it's due (or now, if
// `explicitSave`), and thin out old ones. True when a version was kept.
bool noteSaved(const std::string& path, bool explicitSave);
// Put a version back as the circuit, keeping the current one as a version.
bool restore(const Item& item, const Version& v);

// For the lists: "Today at 3:42 PM", "12 min ago", "8 gates".
std::string friendlyTime(double t);
std::string agoText(double t);
int gateCount(const std::string& path);

// Called after every change to Your Circuits made here (a circuit saved,
// made, renamed, put in the trash or restored), so sync can look at it soon.
// Not for what sync itself writes.
void setChangedHook(std::function<void()> hook);

// The circuit you were last in (reopened at launch).
std::string lastCircuit();
void noteLastCircuit(const std::string& path);

}  // namespace library

#endif  // CL_WINDOWS_LIBRARY_H
