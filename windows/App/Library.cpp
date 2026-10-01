// Your Circuits (see Library.h).

#include "Library.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace library {

namespace {

std::string readFile(const std::string& path) {
	std::ifstream in(W(path).c_str(), std::ios::binary);
	std::ostringstream s;
	s << in.rdbuf();
	return s.str();
}

bool writeFile(const std::string& path, const std::string& text) {
	const std::wstring target = W(path), tmp = target + L".tmp";
	HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	DWORD wrote = 0;
	const bool ok = WriteFile(h, text.data(), (DWORD)text.size(), &wrote, nullptr) && wrote == text.size();
	CloseHandle(h);
	if (ok && MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) return true;
	DeleteFileW(tmp.c_str());
	return false;
}

std::string trim(const std::string& s) {
	const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
	return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

double toSeconds(const FILETIME& ft) {
	ULARGE_INTEGER u;
	u.LowPart = ft.dwLowDateTime;
	u.HighPart = ft.dwHighDateTime;
	return (double)(u.QuadPart - 116444736000000000ULL) / 1e7;
}

double modifiedTime(const std::string& path) {
	WIN32_FILE_ATTRIBUTE_DATA a;
	if (!GetFileAttributesExW(W(path).c_str(), GetFileExInfoStandard, &a)) return 0;
	return toSeconds(a.ftLastWriteTime);
}

double now() {
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	return toSeconds(ft);
}

void makeDirs(const std::string& path) {
	std::string at;
	for (size_t i = 0; i <= path.size(); i++) {
		if (i == path.size() || path[i] == '\\') {
			if (!at.empty() && at.back() != ':') CreateDirectoryW(W(at).c_str(), nullptr);
		}
		if (i < path.size()) at += path[i];
	}
}

std::string stamp(double t) {
	const time_t tt = (time_t)t;
	struct tm lt;
	localtime_s(&lt, &tt);
	char buf[32];
	strftime(buf, sizeof buf, "%Y%m%d-%H%M%S", &lt);
	return buf;
}

// What an imported circuit remembers of its file: where it was and what was
// in it, so opening that same file again finds the copy -- but a file that's
// changed since comes in afresh.
std::string sourceMark(const std::string& file, const std::string& text) {
	unsigned long long h = 0xcbf29ce484222325ULL;
	for (unsigned char c : text) h = (h ^ c) * 0x100000001b3ULL;
	return lowerCase(file) + "\n" + strf("%016llx", h);
}

bool sameFolder(const std::string& a, const std::string& b) { return lowerCase(a) == lowerCase(b); }

// What a version is, for telling versions apart: the file less what running
// it changes (switch settings, what registers hold) and less the app that
// wrote it.
std::string shape(const std::string& path) {
	std::istringstream in(readFile(path));
	std::string out, line;
	while (std::getline(in, line)) {
		const size_t a = line.find_first_not_of(' ');
		const std::string t = a == std::string::npos ? std::string() : line.substr(a);
		if (t.rfind("(lparam \"OUTPUT_NUM\"", 0) == 0 || t.rfind("(lparam \"CURRENT_VALUE\"", 0) == 0 || t.rfind("(generator ", 0) == 0)
			continue;
		out += line;
		out += '\n';
	}
	return out;
}

// Old versions thin as the wx app's do: everything from the last day, then
// one an hour for a week, then one a day.
void thin(const Item& item) {
	std::set<std::string> kept;
	const double t = now();
	for (const Version& v : versions(item)) {
		const double age = t - v.time;
		if (age < 86400) continue;
		const std::string s = stamp(v.time);
		const std::string bucket = age < 7 * 86400 ? "h" + s.substr(0, 11) : "d" + s.substr(0, 8);
		if (kept.count(bucket)) DeleteFileW(W(v.path).c_str());
		else kept.insert(bucket);
	}
}

}  // namespace

std::string root() { return settingsDir() + "\\Library"; }

std::vector<Item> items() {
	std::vector<Item> out;
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW(W(root() + "\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return out;
	do {
		const std::string id = U(fd.cFileName);
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || id.empty() || id[0] == '.') continue;
		Item it;
		it.id = id;
		it.folder = root() + "\\" + id;
		if (!fileExists(it.circuit())) continue;
		it.name = trim(readFile(it.folder + "\\name.txt"));
		if (it.name.empty()) it.name = "Untitled";
		it.modified = modifiedTime(it.circuit());
		out.push_back(it);
	} while (FindNextFileW(h, &fd));
	FindClose(h);
	std::sort(out.begin(), out.end(), [](const Item& a, const Item& b) { return a.modified > b.modified; });
	return out;
}

bool contains(const std::string& path) {
	const std::string r = lowerCase(root()) + "\\";
	return lowerCase(path).compare(0, r.size(), r) == 0;
}

bool itemFor(const std::string& path, Item& out) {
	if (lowerCase(baseName(path)) != "circuit" || !contains(path)) return false;
	const std::string folder = dirName(path);
	if (!sameFolder(dirName(folder), root())) return false;
	out.folder = folder;
	out.id = folder.substr(folder.find_last_of('\\') + 1);
	out.name = trim(readFile(folder + "\\name.txt"));
	if (out.name.empty()) out.name = "Untitled";
	out.modified = modifiedTime(path);
	return true;
}

std::vector<Version> versions(const Item& item) {
	std::vector<Version> out;
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW(W(item.versionsFolder() + "\\*.cdl").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return out;
	do {
		const std::string name = U(fd.cFileName);
		if (name.empty() || name[0] == '.') continue;
		out.push_back(Version{ item.versionsFolder() + "\\" + name, toSeconds(fd.ftLastWriteTime) });
	} while (FindNextFileW(h, &fd));
	FindClose(h);
	std::sort(out.begin(), out.end(), [](const Version& a, const Version& b) { return a.time > b.time; });
	return out;
}

bool create(const std::string& name, const std::string& text, const std::string& source, Item& out) {
	const std::string id = stamp(now()) + strf("-%d", 1000 + rand() % 99000);
	out.id = id;
	out.folder = root() + "\\" + id;
	out.name = name;
	out.modified = now();
	makeDirs(out.versionsFolder());
	if (!writeFile(out.folder + "\\name.txt", name) || !writeFile(out.circuit(), text)) return false;
	if (!source.empty()) writeFile(out.folder + "\\source.txt", sourceMark(source, readFile(source)));
	return true;
}

bool imported(const std::string& file, Item& out) {
	const std::string text = readFile(file);
	if (text.empty()) return false;
	const std::string mark = sourceMark(file, text);
	for (const Item& it : items()) {
		const std::string stored = readFile(it.folder + "\\source.txt");
		if (stored == mark) { out = it; return true; }
	}
	return false;
}

void rename(const Item& item, const std::string& name) { writeFile(item.folder + "\\name.txt", name); }

bool moveToTrash(const Item& item) {
	const std::string trash = root() + "\\.Trash";
	CreateDirectoryW(W(trash).c_str(), nullptr);
	std::string dest = trash + "\\" + item.id;
	for (int n = 2; GetFileAttributesW(W(dest).c_str()) != INVALID_FILE_ATTRIBUTES; n++) dest = trash + "\\" + item.id + strf(" %d", n);
	return MoveFileExW(W(item.folder).c_str(), W(dest).c_str(), 0) != FALSE;
}

// It saves itself every few seconds, so not every save is a version: the
// latest waits in versions\.pending.cdl, and becomes a version when you come
// back after a break (10 minutes without saving), after half an hour of
// steady work, or at once on Ctrl+S.
bool noteSaved(const std::string& path, bool explicitSave) {
	Item item;
	if (!itemFor(path, item)) return false;
	makeDirs(item.versionsFolder());
	const std::string pending = item.versionsFolder() + "\\.pending.cdl";
	const double t = now();
	const std::vector<Version> vs = versions(item);
	bool kept = false;
	auto keep = [&](const std::string& src, double when) {
		// Not twice the same circuit in a row.
		if (!vs.empty() && shape(vs.front().path) == shape(src)) return;
		const std::string dest = item.versionsFolder() + "\\" + stamp(when) + ".cdl";
		if (CopyFileW(W(src).c_str(), W(dest).c_str(), FALSE)) kept = true;
	};
	if (explicitSave) {
		keep(path, t);
	} else if (fileExists(pending)) {
		const double pendingTime = modifiedTime(pending);
		const bool afterBreak = t - pendingTime > 10 * 60;
		const bool longSession = t - (vs.empty() ? 0 : vs.front().time) > 30 * 60;
		if (afterBreak || longSession) keep(pending, pendingTime);
	} else if (vs.empty()) {
		keep(path, t);   // a circuit's first save
	}
	DeleteFileW(W(pending).c_str());
	CopyFileW(W(path).c_str(), W(pending).c_str(), FALSE);
	SetFileAttributesW(W(pending).c_str(), FILE_ATTRIBUTE_HIDDEN);
	thin(item);
	return kept;
}

bool restore(const Item& item, const Version& v) {
	makeDirs(item.versionsFolder());
	CopyFileW(W(item.circuit()).c_str(), W(item.versionsFolder() + "\\" + stamp(now()) + ".cdl").c_str(), FALSE);
	const std::string text = readFile(v.path);
	return !text.empty() && writeFile(item.circuit(), text);
}

std::string friendlyTime(double t) {
	const time_t tt = (time_t)t, nowT = (time_t)now();
	struct tm lt, ln;
	localtime_s(&lt, &tt);
	localtime_s(&ln, &nowT);
	SYSTEMTIME st = {};
	st.wYear = (WORD)(lt.tm_year + 1900);
	st.wMonth = (WORD)(lt.tm_mon + 1);
	st.wDay = (WORD)lt.tm_mday;
	st.wHour = (WORD)lt.tm_hour;
	st.wMinute = (WORD)lt.tm_min;
	wchar_t clock[64] = L"";
	GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, clock, 64);
	const int days = (int)((ln.tm_year - lt.tm_year) * 366 + (ln.tm_yday - lt.tm_yday));
	if (days == 0) return "Today at " + U(clock);
	if (days == 1) return "Yesterday at " + U(clock);
	wchar_t date[64] = L"";
	GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, lt.tm_year == ln.tm_year ? L"MMM d" : L"MMM d, yyyy", date, 64, nullptr);
	return U(date) + " at " + U(clock);
}

std::string agoText(double t) {
	const double s = now() - t;
	if (s < 60) return "just now";
	if (s < 3600) return strf("%d min ago", (int)(s / 60));
	if (s < 86400) return strf("%d hr ago", (int)(s / 3600));
	return strf("%d days ago", (int)(s / 86400));
}

int gateCount(const std::string& path) {
	static std::map<std::string, std::pair<double, int>> cache;
	const double m = modifiedTime(path);
	auto it = cache.find(path);
	if (it != cache.end() && it->second.first == m) return it->second.second;
	const std::string text = readFile(path);
	int n = 0;
	for (size_t at = text.find("(gate "); at != std::string::npos; at = text.find("(gate ", at + 6)) n++;
	cache[path] = { m, n };
	return n;
}

std::string lastCircuit() { return prefs().lastCircuit; }

void noteLastCircuit(const std::string& path) {
	if (path.empty() || !contains(path) || path.find("\\.Trash\\") != std::string::npos) return;
	if (prefs().lastCircuit == path) return;
	prefs().lastCircuit = path;
	prefs().save();
}

}  // namespace library
