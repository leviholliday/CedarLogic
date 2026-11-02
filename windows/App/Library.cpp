// Your Circuits (see Library.h).

#include "Library.h"

#include <algorithm>
#include <cmath>
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

std::function<void()>& changedHook() {
	static std::function<void()> hook;
	return hook;
}

void changed() {
	if (changedHook()) changedHook()();
}

// A version's note (sync writes one beside what it keeps: <stamp>.txt).
std::string noteFor(const std::string& versionPath) {
	if (versionPath.size() < 4) return std::string();
	const std::string text = trim(readFile(versionPath.substr(0, versionPath.size() - 4) + ".txt"));
	return text.substr(0, text.find('\n'));
}

// Old versions thin as the wx app's do: everything from the last day, then
// one an hour for a week, then one a day. A note beside a version goes with it.
void thin(const Item& item) {
	std::set<std::string> kept;
	const double t = now();
	for (const Version& v : versions(item)) {
		const double age = t - v.time;
		if (age < 86400) continue;
		const std::string s = stamp(v.time);
		const std::string bucket = age < 7 * 86400 ? "h" + s.substr(0, 11) : "d" + s.substr(0, 8);
		if (kept.count(bucket)) {
			DeleteFileW(W(v.path).c_str());
			if (v.path.size() > 4) DeleteFileW(W(v.path.substr(0, v.path.size() - 4) + ".txt").c_str());
		} else {
			kept.insert(bucket);
		}
	}
}

}  // namespace

void setChangedHook(std::function<void()> hook) { changedHook() = std::move(hook); }

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
		Version v{ item.versionsFolder() + "\\" + name, toSeconds(fd.ftLastWriteTime), std::string() };
		v.note = noteFor(v.path);
		out.push_back(v);
	} while (FindNextFileW(h, &fd));
	FindClose(h);
	std::sort(out.begin(), out.end(), [](const Version& a, const Version& b) { return a.time > b.time; });
	return out;
}

bool create(const std::string& name, const std::string& text, const std::string& source, Item& out) {
	// Its folder is claimed (made) before anything goes in it, and the number
	// is this process's own, so two imports in the same second -- a file
	// each, opened together from Explorer -- never share one.
	static unsigned next = (unsigned)GetCurrentProcessId() * 2654435761u + (unsigned)GetTickCount();
	makeDirs(root());
	std::string id;
	for (int tries = 0; id.empty(); tries++) {
		const std::string t = stamp(now()) + strf("-%d", 1000 + (int)(next++ % 99000));
		if (CreateDirectoryW(W(root() + "\\" + t).c_str(), nullptr)) id = t;
		else if (GetLastError() != ERROR_ALREADY_EXISTS || tries >= 100) return false;
	}
	out.id = id;
	out.folder = root() + "\\" + id;
	out.name = name;
	out.modified = now();
	makeDirs(out.versionsFolder());
	if (!writeFile(out.folder + "\\name.txt", name) || !writeFile(out.circuit(), text)) return false;
	if (!source.empty()) writeFile(out.folder + "\\source.txt", sourceMark(source, readFile(source)));
	changed();
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

void rename(const Item& item, const std::string& name) {
	writeFile(item.folder + "\\name.txt", name);
	changed();
}

bool moveToTrash(const Item& item) {
	const std::string trash = root() + "\\.Trash";
	CreateDirectoryW(W(trash).c_str(), nullptr);
	std::string dest = trash + "\\" + item.id;
	for (int n = 2; GetFileAttributesW(W(dest).c_str()) != INVALID_FILE_ATTRIBUTES; n++) dest = trash + "\\" + item.id + strf(" %d", n);
	const bool moved = MoveFileExW(W(item.folder).c_str(), W(dest).c_str(), 0) != FALSE;
	if (moved) {
		stampDeleted(dest, now());
		changed();
	}
	return moved;
}

// ---- Recently Deleted ------------------------------------------------------------------

namespace {

const char* kStampFile = "deleted.txt";

std::string isoTime(double t) {
	const time_t tt = (time_t)t;
	struct tm g;
	gmtime_s(&g, &tt);
	char buf[32];
	strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &g);
	return buf;
}

bool parseIso(const std::string& s, double& t) {
	struct tm g = {};
	if (sscanf_s(s.c_str(), "%d-%d-%dT%d:%d:%d", &g.tm_year, &g.tm_mon, &g.tm_mday, &g.tm_hour, &g.tm_min, &g.tm_sec) != 6) return false;
	g.tm_year -= 1900;
	g.tm_mon -= 1;
	const time_t tt = _mkgmtime(&g);
	if (tt == (time_t)-1) return false;
	t = (double)tt;
	return true;
}

// When the folder was moved into the trash: its change time (NTFS updates
// it on a rename or move), else its last write.
double movedTime(const std::string& folder) {
	HANDLE h = CreateFileW(W(folder).c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
	                       FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	double t = 0;
	if (h != INVALID_HANDLE_VALUE) {
		FILE_BASIC_INFO info = {};
		if (GetFileInformationByHandleEx(h, FileBasicInfo, &info, sizeof info) && info.ChangeTime.QuadPart > 0) {
			FILETIME ft;
			ft.dwLowDateTime = info.ChangeTime.LowPart;
			ft.dwHighDateTime = (DWORD)info.ChangeTime.HighPart;
			t = toSeconds(ft);
		}
		CloseHandle(h);
	}
	return t > 0 ? t : modifiedTime(folder);
}

// A folder and everything in it.
bool removeTree(const std::string& folder) {
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW(W(folder + "\\*").c_str(), &fd);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			const std::string n = U(fd.cFileName);
			if (n == "." || n == "..") continue;
			const std::string p = folder + "\\" + n;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) removeTree(p);
			else {
				SetFileAttributesW(W(p).c_str(), FILE_ATTRIBUTE_NORMAL);
				DeleteFileW(W(p).c_str());
			}
		} while (FindNextFileW(h, &fd));
		FindClose(h);
	}
	return RemoveDirectoryW(W(folder).c_str()) != FALSE;
}

}  // namespace

void stampDeleted(const std::string& folder, double when) { writeFile(folder + "\\" + kStampFile, isoTime(when) + "\n"); }

std::vector<Deleted> deletedItems(const std::string& libraryRoot, double nowTime) {
	const std::string bin = (libraryRoot.empty() ? root() : libraryRoot) + "\\.Trash";
	const double t = nowTime > 0 ? nowTime : now();
	std::vector<Deleted> out;
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW(W(bin + "\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return out;
	do {
		const std::string n = U(fd.cFileName);
		if (n.empty() || n[0] == '.' || !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
		Deleted d;
		d.id = n;
		d.folder = bin + "\\" + n;
		// Not a circuit ("repeated versions"): left alone.
		if (!fileExists(d.circuit())) continue;
		d.name = trim(readFile(d.folder + "\\name.txt"));
		if (d.name.empty()) d.name = "Untitled";
		if (!parseIso(trim(readFile(d.folder + "\\" + kStampFile)), d.deleted)) {
			const double moved = movedTime(d.folder);
			const bool fresh = moved > 0 && moved <= t && t - moved < kKeepDeletedDays * 86400.0;
			d.deleted = fresh ? moved : t;
			stampDeleted(d.folder, d.deleted);
		}
		out.push_back(d);
	} while (FindNextFileW(h, &fd));
	FindClose(h);
	std::sort(out.begin(), out.end(), [](const Deleted& a, const Deleted& b) { return a.deleted > b.deleted; });
	return out;
}

int sweepDeleted(const std::string& libraryRoot, double nowTime) {
	const double t = nowTime > 0 ? nowTime : now();
	const double limit = t - kKeepDeletedDays * 86400.0;
	int n = 0;
	for (const Deleted& d : deletedItems(libraryRoot, t))
		if (d.deleted < limit && removeTree(d.folder)) n++;
	return n;
}

std::string restoreDeleted(const Deleted& d) {
	// "<id> 2" was the second circuit with that id in the trash.
	std::string id = d.id;
	const size_t sp = id.find_last_of(' ');
	if (sp != std::string::npos && sp + 1 < id.size() && id.find_first_not_of("0123456789", sp + 1) == std::string::npos) id = id.substr(0, sp);
	const std::string libRoot = d.folder.substr(0, d.folder.rfind("\\.Trash\\"));
	if (id.empty() || GetFileAttributesW(W(libRoot + "\\" + id).c_str()) != INVALID_FILE_ATTRIBUTES) {
		static unsigned next = (unsigned)GetCurrentProcessId() * 2654435761u + (unsigned)GetTickCount();
		for (int tries = 0; tries < 100; tries++) {
			id = stamp(now()) + strf("-%d", 1000 + (int)(next++ % 99000));
			if (GetFileAttributesW(W(libRoot + "\\" + id).c_str()) == INVALID_FILE_ATTRIBUTES) break;
		}
	}
	const std::string dest = libRoot + "\\" + id;
	if (!MoveFileExW(W(d.folder).c_str(), W(dest).c_str(), 0)) return std::string();
	DeleteFileW(W(dest + "\\" + kStampFile).c_str());
	// At the top of Your Circuits.
	HANDLE h = CreateFileW(W(dest + "\\circuit.cdl").c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
	                       nullptr);
	if (h != INVALID_HANDLE_VALUE) {
		FILETIME ft;
		GetSystemTimeAsFileTime(&ft);
		SetFileTime(h, nullptr, nullptr, &ft);
		CloseHandle(h);
	}
	changed();
	return id;
}

bool deleteNow(const Deleted& d) { return removeTree(d.folder); }

int trashSelfTest(std::function<void(const std::string&)> say) {
	int fails = 0;
	auto check = [&](bool ok, const std::string& what) {
		say(std::string(ok ? "PASS  " : "FAIL  ") + what + "\n");
		if (!ok) fails++;
	};
	// A library of its own, in the temp folder.
	wchar_t tmp[MAX_PATH] = L"";
	GetTempPathW(MAX_PATH, tmp);
	const std::string home = U(tmp) + strf("cedarlogic-trash-%lu", GetCurrentProcessId());
	SetEnvironmentVariableW(L"CEDARLOGIC_HOME", W(home).c_str());
	removeTree(home);
	const std::string lib = root();
	makeDirs(lib);
	auto circuitText = [](int gates) {
		std::string s = "\n<circuit>\n<CurrentPage>0</CurrentPage>\n<page 0>\n";
		for (int i = 0; i < gates; i++) s += strf("<gate>\n<ID>%d</ID>\n<type>AA_AND2</type>\n<position>%d,0</position>\n</gate>\n", i + 1, i * 8);
		return s + "</page 0>\n</circuit>\n";
	};
	// Three circuits, as the Mac's state makes them; "Counter" is deleted from Your Circuits.
	Item adder, half, counter;
	create("Adder", circuitText(2), "", adder);
	create("Half adder", circuitText(2), "", half);
	create("Counter", circuitText(4), "", counter);
	check(moveToTrash(counter), "Delete puts “Counter” in the trash");
	const std::string bin = lib + "\\.Trash";
	auto trashed = [&](const std::string& folder, const std::string& name, double deleted) {
		const std::string f = bin + "\\" + folder;
		makeDirs(f + "\\versions");
		writeFile(f + "\\name.txt", name);
		writeFile(f + "\\circuit.cdl", circuitText(3));
		if (deleted > 0) stampDeleted(f, deleted);
	};
	const double t = now();
	trashed("20240101-120000-11111", "Old one", t - 31 * 86400.0);
	trashed("20250101-120000-22222", "From the phone", 0);   // sync's: no date
	trashed(half.id + " 2", "Half adder again", t - 86400.0);
	makeDirs(bin + "\\repeated versions\\" + half.id);
	writeFile(bin + "\\repeated versions\\" + half.id + "\\20240101-000000.cdl", circuitText(1));

	std::vector<Deleted> before = deletedItems();
	std::set<std::string> names;
	std::string list;
	for (const Deleted& d : before) { names.insert(d.name); list += (list.empty() ? "" : ", ") + d.name; }
	check(before.size() == 4 && names == std::set<std::string>{ "Counter", "Old one", "From the phone", "Half adder again" },
	      "Recently Deleted shows the 4 deleted circuits (" + list + ")");
	check(!before.empty() && (before[0].name == "Counter" || before[0].name == "From the phone"), "newest first");
	bool repeatedListed = false;
	for (const Deleted& d : before) repeatedListed = repeatedListed || d.id == "repeated versions";
	check(!repeatedListed && GetFileAttributesW(W(bin + "\\repeated versions").c_str()) != INVALID_FILE_ATTRIBUTES,
	      "the repeated versions folder isn't listed, and is left alone");
	for (const Deleted& d : before) {
		if (d.name == "Counter") check(std::fabs(d.deleted - t) < 600, "a circuit deleted from Your Circuits is dated when it was deleted");
		if (d.name == "From the phone")
			check(fileExists(d.folder + "\\" + kStampFile) && std::fabs(d.deleted - t) < 600,
			      "one sync moved there with no date is dated when it was moved, and stamped");
	}
	const int swept = sweepDeleted();
	std::vector<Deleted> after = deletedItems();
	bool oldThere = false;
	for (const Deleted& d : after) oldThere = oldThere || d.name == "Old one";
	check(swept == 1 && !oldThere, strf("the sweep deleted the one from 31 days ago, only (%d)", swept));
	check(sweepDeleted() == 0, "a second sweep deletes nothing");

	// Restore: under its own id when that's free...
	bool found = false;
	for (const Deleted& d : after) {
		if (d.name != "Counter") continue;
		found = true;
		const std::string id = restoreDeleted(d);
		const std::vector<Item> now = items();
		check(id == d.id && !now.empty() && now.front().id == id && now.front().name == "Counter",
		      "Restore puts “Counter” back in Your Circuits under its own id, at the top");
		check(!fileExists(lib + "\\" + id + "\\" + kStampFile), "without its deleted date");
	}
	if (!found) check(false, "Counter is in Recently Deleted");
	// ...and under a new one when its id is taken (a second copy).
	found = false;
	for (const Deleted& d : after) {
		if (d.name != "Half adder again") continue;
		found = true;
		const std::string id = restoreDeleted(d);
		bool both = false, again = false;
		for (const Item& it : items()) {
			both = both || it.id == half.id;
			again = again || (it.id == id && it.name == "Half adder again");
		}
		check(!id.empty() && id != half.id && id.find(' ') == std::string::npos && both && again,
		      "a second copy of a circuit that's still there comes back beside it, under a new id (" + id + ")");
	}
	if (!found) check(false, "the second copy is in Recently Deleted");
	check(items().size() == 4, strf("Your Circuits has 4 circuits now (%d)", (int)items().size()));
	// Delete Now.
	found = false;
	for (const Deleted& d : deletedItems()) {
		if (d.name != "From the phone") continue;
		found = true;
		check(deleteNow(d) && GetFileAttributesW(W(d.folder).c_str()) == INVALID_FILE_ATTRIBUTES, "Delete Now deletes it for good");
	}
	if (!found) check(false, "the sync-trashed circuit is in Recently Deleted");
	check(deletedItems().empty(), "Recently Deleted is empty");

	removeTree(home);
	SetEnvironmentVariableW(L"CEDARLOGIC_HOME", nullptr);
	say(fails == 0 ? "trash-check: all passed\n" : strf("trash-check: %d failed\n", fails));
	return fails;
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
	changed();
	return kept;
}

bool restore(const Item& item, const Version& v) {
	makeDirs(item.versionsFolder());
	CopyFileW(W(item.circuit()).c_str(), W(item.versionsFolder() + "\\" + stamp(now()) + ".cdl").c_str(), FALSE);
	const std::string text = readFile(v.path);
	const bool ok = !text.empty() && writeFile(item.circuit(), text);
	if (ok) changed();
	return ok;
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
	// Yesterday is the day before today's date (Dec 31 on Jan 1 too).
	struct tm y = ln;
	y.tm_mday -= 1;
	y.tm_hour = 12;
	y.tm_isdst = -1;
	mktime(&y);
	if (lt.tm_year == ln.tm_year && lt.tm_yday == ln.tm_yday) return "Today at " + U(clock);
	if (lt.tm_year == y.tm_year && lt.tm_yday == y.tm_yday) return "Yesterday at " + U(clock);
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
