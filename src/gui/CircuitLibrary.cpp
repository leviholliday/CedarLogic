/*****************************************************************************
   Project: CEDAR Logic Simulator
   CircuitLibrary: circuits kept inside the app, with version history.
*****************************************************************************/

#include "CircuitLibrary.h"

#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/dir.h>
#include <wx/file.h>
#include <wx/textfile.h>
#include <algorithm>
#include <map>
#include <random>

namespace library {

namespace {

const char* STAMP_FORMAT = "%Y%m%d-%H%M%S";

wxString docDir(const std::string& id) {
	return root() + wxFILE_SEP_PATH + wxString::FromUTF8(id.c_str());
}

wxString versionsDir(const std::string& id) {
	return docDir(id) + wxFILE_SEP_PATH + "versions";
}

wxDateTime parseStamp(const wxString& stamp) {
	wxDateTime t;
	if (!t.ParseFormat(stamp, STAMP_FORMAT)) return wxDateTime();
	return t;
}

}  // namespace

wxString root() {
	const wxString r = wxStandardPaths::Get().GetUserDataDir() + wxFILE_SEP_PATH + "Library";
	if (!wxFileName::DirExists(r)) wxFileName::Mkdir(r, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
	return r;
}

wxString circuitPath(const std::string& id) {
	return docDir(id) + wxFILE_SEP_PATH + "circuit.cdl";
}

bool exists(const std::string& id) {
	return !id.empty() && wxFileName::DirExists(docDir(id));
}

std::string create(const wxString& docName) {
	// Time-ordered and unique: the creation time plus a little randomness.
	std::random_device rd;
	const std::string id = wxDateTime::Now().Format(STAMP_FORMAT).ToStdString() + "-" +
	                       std::to_string(rd() % 100000);
	wxFileName::Mkdir(versionsDir(id), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
	rename(id, docName);
	return id;
}

wxString name(const std::string& id) {
	wxTextFile f(docDir(id) + wxFILE_SEP_PATH + "name.txt");
	if (!f.Exists() || !f.Open() || f.GetLineCount() == 0) return "Untitled";
	return f.GetFirstLine();
}

void rename(const std::string& id, const wxString& newName) {
	wxFile f(docDir(id) + wxFILE_SEP_PATH + "name.txt", wxFile::write);
	if (f.IsOpened()) f.Write(newName.Strip(wxString::both), wxConvUTF8);
}

wxString nextUntitledName() {
	std::vector<Doc> docs = list();
	auto taken = [&](const wxString& n) {
		for (const Doc& d : docs) if (d.name == n) return true;
		return false;
	};
	if (!taken("Untitled")) return "Untitled";
	for (int i = 2;; i++) {
		const wxString n = wxString::Format("Untitled %d", i);
		if (!taken(n)) return n;
	}
}

std::vector<Doc> list() {
	std::vector<Doc> out;
	wxDir dir(root());
	if (!dir.IsOpened()) return out;
	wxString sub;
	for (bool ok = dir.GetFirst(&sub, wxEmptyString, wxDIR_DIRS); ok; ok = dir.GetNext(&sub)) {
		if (sub.StartsWith(".")) continue;
		const std::string id = sub.ToStdString();
		const wxString path = circuitPath(id);
		if (!wxFileName::FileExists(path)) continue;   // created but never saved
		Doc d{ id, name(id), wxFileName(path).GetModificationTime() };
		out.push_back(d);
	}
	std::sort(out.begin(), out.end(), [](const Doc& a, const Doc& b) { return a.modified > b.modified; });
	return out;
}

void snapshot(const std::string& id) {
	const wxString src = circuitPath(id);
	if (!wxFileName::FileExists(src)) return;
	wxFileName::Mkdir(versionsDir(id), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
	const wxString dst = versionsDir(id) + wxFILE_SEP_PATH + wxDateTime::Now().Format(STAMP_FORMAT) + ".cdl";
	wxCopyFile(src, dst, true);

	// Thin out: keep all from the last day; the newest in each hour for the
	// last week; the newest in each day before that.
	const wxDateTime now = wxDateTime::Now();
	std::map<wxString, bool> keptBucket;
	for (const Version& v : versions(id)) {   // newest first
		if (!v.when.IsValid()) continue;
		const wxTimeSpan age = now - v.when;
		if (age.GetHours() < 24) continue;
		const wxString bucket = age.GetDays() < 7 ? v.when.Format("h%Y%m%d%H") : v.when.Format("d%Y%m%d");
		if (keptBucket[bucket]) wxRemoveFile(v.path);
		else keptBucket[bucket] = true;
	}
}

std::vector<Version> versions(const std::string& id) {
	std::vector<Version> out;
	wxDir dir(versionsDir(id));
	if (!dir.IsOpened()) return out;
	wxString file;
	for (bool ok = dir.GetFirst(&file, "*.cdl", wxDIR_FILES); ok; ok = dir.GetNext(&file)) {
		Version v{ versionsDir(id) + wxFILE_SEP_PATH + file, parseStamp(wxFileName(file).GetName()) };
		if (v.when.IsValid()) out.push_back(v);
	}
	std::sort(out.begin(), out.end(), [](const Version& a, const Version& b) { return a.when > b.when; });
	return out;
}

void remove(const std::string& id) {
	const wxString trash = root() + wxFILE_SEP_PATH + ".Trash";
	wxFileName::Mkdir(trash, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
	wxRenameFile(docDir(id), trash + wxFILE_SEP_PATH + wxString::FromUTF8(id.c_str()), true);
}

}  // namespace library
