// Recovery copies: every open circuit with unsaved changes is written to
// %LOCALAPPDATA%\CedarLogic\Recovery now and then (Window.cpp decides when).
// A clean close removes its copy; a copy still there at the next launch, from
// a CedarLogic that isn't running any more, is offered back.

#include "Recovery.h"
#include "Window.h"

#include <shlobj.h>

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace recovery {

namespace {

std::string dir() {
	PWSTR p = nullptr;
	std::string d;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p)) && p) d = U(p) + "\\CedarLogic";
	if (p) CoTaskMemFree(p);
	if (d.empty()) d = settingsDir();
	CreateDirectoryW(W(d).c_str(), nullptr);
	d += "\\Recovery";
	CreateDirectoryW(W(d).c_str(), nullptr);
	return d;
}

std::string file(const std::string& base, const char* ext) { return dir() + "\\" + base + ext; }

bool writeFile(const std::string& path, const std::string& text) {
	HANDLE h = CreateFileW(W(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	DWORD wrote = 0;
	const bool ok = WriteFile(h, text.data(), (DWORD)text.size(), &wrote, nullptr) && wrote == text.size();
	CloseHandle(h);
	return ok;
}

std::string readFile(const std::string& path) {
	std::ifstream in(W(path).c_str(), std::ios::binary);
	std::ostringstream s;
	s << in.rdbuf();
	return s.str();
}

// Whether a CedarLogic with this process id is running now (a reused id
// belonging to something else doesn't count).
bool running(DWORD pid) {
	if (pid == 0) return false;
	if (pid == GetCurrentProcessId()) return true;
	HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
	if (h == nullptr) return false;
	bool ours = false;
	if (WaitForSingleObject(h, 0) == WAIT_TIMEOUT) {
		wchar_t name[MAX_PATH * 2];
		DWORD n = (DWORD)(sizeof name / sizeof name[0]);
		if (QueryFullProcessImageNameW(h, 0, name, &n)) ours = lowerCase(baseName(U(std::wstring(name, n)))).find("cedarlogic") == 0;
	}
	CloseHandle(h);
	return ours;
}

struct Found { std::string base, path, name; };

std::vector<Found> orphans() {
	std::vector<Found> out;
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW(W(dir() + "\\*.info").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return out;
	do {
		const std::string entry = U(fd.cFileName);
		if (entry.size() < 6) continue;
		const std::string base = entry.substr(0, entry.size() - 5);
		if (running((DWORD)strtoul(base.c_str(), nullptr, 10))) continue;
		if (!fileExists(file(base, ".cdl"))) { remove(base); continue; }
		Found f{ base, "", "" };
		std::istringstream info(readFile(file(base, ".info")));
		std::string line;
		while (std::getline(info, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.compare(0, 5, "path=") == 0) f.path = line.substr(5);
			else if (line.compare(0, 5, "name=") == 0) f.name = line.substr(5);
		}
		out.push_back(f);
	} while (FindNextFileW(h, &fd));
	FindClose(h);
	return out;
}

}  // namespace

std::string newBase() {
	static int n = 0;
	return strf("%lu-%d", (unsigned long)GetCurrentProcessId(), ++n);
}

bool write(const std::string& base, const std::string& text, const std::string& path, const std::string& name) {
	if (!writeFile(file(base, ".cdl"), text)) return false;
	return writeFile(file(base, ".info"), "path=" + path + "\nname=" + name + "\n");
}

void remove(const std::string& base) {
	if (base.empty()) return;
	DeleteFileW(W(file(base, ".cdl")).c_str());
	DeleteFileW(W(file(base, ".info")).c_str());
}

void offer(CircuitWindow* reuse) {
	HWND parent = reuse ? reuse->window() : nullptr;
	const std::vector<Found> found = orphans();
	if (found.empty()) return;
	std::string names;
	for (const Found& f : found) names += "• " + (f.name.empty() ? std::string("Untitled") : f.name) + "\n";
	const std::string text = std::string(found.size() == 1 ? "CedarLogic closed before this circuit was saved:"
	                                                       : "CedarLogic closed before these circuits were saved:") +
	                         "\n\n" + names +
	                         "\nA copy of the work from just before it closed was kept. Open it again?\n\n"
	                         "Yes opens it. No throws the copy away. Cancel asks again next time.";
	const int r = MessageBoxW(parent, W(text).c_str(), L"CedarLogic", MB_YESNOCANCEL | MB_ICONQUESTION);
	if (r == IDCANCEL) return;   // asked again next launch
	for (const Found& f : found) {
		if (r == IDYES) {
			char err[512] = "";
			CLDocument* doc = cl_document_open(file(f.base, ".cdl").c_str(), err, sizeof err);
			if (doc == nullptr) {
				showMessage(parent, Tone::Warning, "A kept copy couldn't be opened", err);
				continue;   // left in place, in case a later version can read it
			}
			// Back under its own name, marked unsaved: saving puts it where it was.
			CircuitWindow* w = reuse && reuse->isPristine() ? reuse : nullptr;
			if (w) w->replaceDocument(doc, f.path);
			else w = new CircuitWindow(doc, f.path);
			w->markRecovered(f.name);
			reuse = nullptr;
		}
		remove(f.base);
	}
}

}  // namespace recovery
