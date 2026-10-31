// Your Circuits (see Library.h).

#include "Library.h"
#include "SyncApp.h"

#include <glib/gstdio.h>
#include <sys/stat.h>
#include <utime.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <set>
#include <sstream>

namespace library {

namespace {

std::string readFile(const std::string& path) {
	gchar* data = nullptr;
	gsize len = 0;
	if (!g_file_get_contents(path.c_str(), &data, &len, nullptr)) return std::string();
	std::string out(data, len);
	g_free(data);
	return out;
}

// Written to a temporary beside it and moved over it (GLib does both).
bool writeFile(const std::string& path, const std::string& text) {
	return g_file_set_contents(path.c_str(), text.data(), (gssize)text.size(), nullptr) != FALSE;
}

bool copyFile(const std::string& from, const std::string& to) {
	gchar* data = nullptr;
	gsize len = 0;
	if (!g_file_get_contents(from.c_str(), &data, &len, nullptr)) return false;
	const bool ok = g_file_set_contents(to.c_str(), data, (gssize)len, nullptr) != FALSE;
	g_free(data);
	return ok;
}

std::string trim(const std::string& s) {
	const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
	return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

double modifiedTime(const std::string& path) {
	struct stat st;
	if (stat(path.c_str(), &st) != 0) return 0;
	return (double)st.st_mtim.tv_sec + st.st_mtim.tv_nsec / 1e9;
}

double now() { return g_get_real_time() / 1e6; }

bool fileExists(const std::string& path) { return g_file_test(path.c_str(), G_FILE_TEST_EXISTS) != FALSE; }

std::string stamp(double t) {
	const time_t tt = (time_t)t;
	struct tm lt;
	localtime_r(&tt, &lt);
	char buf[32];
	strftime(buf, sizeof buf, "%Y%m%d-%H%M%S", &lt);
	return buf;
}

// When a version's work was done, from its name (stamp's); its file's date
// is when it was put away, which after a break is later. 0 if it isn't one.
double stampTime(const std::string& name) {
	int y, mo, d, h, mi, s;
	char rest[8] = "";
	if (sscanf(name.c_str(), "%4d%2d%2d-%2d%2d%2d%7s", &y, &mo, &d, &h, &mi, &s, rest) != 7 || std::string(rest) != ".cdl") return 0;
	GDateTime* t = g_date_time_new_local(y, mo, d, h, mi, s);
	if (t == nullptr) return 0;
	const double out = (double)g_date_time_to_unix(t);
	g_date_time_unref(t);
	return out;
}

std::string parentOf(const std::string& path) {
	const size_t slash = path.find_last_of('/');
	return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

std::string lastPart(const std::string& path) {
	const size_t slash = path.find_last_of('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

// What an imported circuit remembers of its file: where it was and what was
// in it, so opening that same file again finds the copy -- but a file that's
// changed since comes in afresh.
std::string sourceMark(const std::string& file, const std::string& text) {
	unsigned long long h = 0xcbf29ce484222325ULL;
	for (unsigned char c : text) h = (h ^ c) * 0x100000001b3ULL;
	return file + "\n" + format("%016llx", h);
}

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
		if (kept.count(bucket)) {
			g_remove(v.path.c_str());
			// A version sync made has a note beside it (where it came from): it goes too.
			g_remove((v.path.substr(0, v.path.size() - 4) + ".txt").c_str());
		} else {
			kept.insert(bucket);
		}
	}
}

}  // namespace

std::string root() {
	static std::string r = [] {
		// Your circuits are yours alone: owner-only (0700), a folder made before that tightened too.
		const std::string dir = std::string(g_get_user_data_dir()) + "/CedarLogic/Library";
		g_mkdir_with_parents(dir.c_str(), 0700);
		g_chmod(dir.c_str(), 0700);
		return dir;
	}();
	return r;
}

std::vector<Item> items() {
	std::vector<Item> out;
	GDir* d = g_dir_open(root().c_str(), 0, nullptr);
	if (d == nullptr) return out;
	while (const gchar* name = g_dir_read_name(d)) {
		const std::string id = name;
		if (id.empty() || id[0] == '.') continue;
		Item it;
		it.id = id;
		it.folder = root() + "/" + id;
		if (!g_file_test(it.folder.c_str(), G_FILE_TEST_IS_DIR) || !fileExists(it.circuit())) continue;
		it.name = trim(readFile(it.folder + "/name.txt"));
		if (it.name.empty()) it.name = "Untitled";
		it.modified = modifiedTime(it.circuit());
		out.push_back(it);
	}
	g_dir_close(d);
	std::sort(out.begin(), out.end(), [](const Item& a, const Item& b) { return a.modified > b.modified; });
	return out;
}

bool contains(const std::string& path) {
	const std::string r = root() + "/";
	return path.compare(0, r.size(), r) == 0;
}

bool itemFor(const std::string& path, Item& out) {
	if (lastPart(path) != "circuit.cdl" || !contains(path)) return false;
	const std::string folder = parentOf(path);
	if (parentOf(folder) != root()) return false;
	out.folder = folder;
	out.id = lastPart(folder);
	out.name = trim(readFile(folder + "/name.txt"));
	if (out.name.empty()) out.name = "Untitled";
	out.modified = modifiedTime(path);
	return true;
}

std::vector<Version> versions(const Item& item) {
	std::vector<Version> out;
	GDir* d = g_dir_open(item.versionsFolder().c_str(), 0, nullptr);
	if (d == nullptr) return out;
	while (const gchar* name = g_dir_read_name(d)) {
		const std::string n = name;
		if (n.empty() || n[0] == '.' || n.size() < 5 || n.compare(n.size() - 4, 4, ".cdl") != 0) continue;
		const std::string p = item.versionsFolder() + "/" + n;
		const double named = stampTime(n);
		out.push_back(Version{ p, named > 0 ? named : modifiedTime(p) });
	}
	g_dir_close(d);
	std::sort(out.begin(), out.end(), [](const Version& a, const Version& b) { return a.time > b.time; });
	return out;
}

bool create(const std::string& name, const std::string& text, const std::string& source, Item& out) {
	const std::string id = stamp(now()) + format("-%d", 1000 + g_random_int_range(0, 99000));
	out.id = id;
	out.folder = root() + "/" + id;
	out.name = name;
	out.modified = now();
	g_mkdir_with_parents(out.versionsFolder().c_str(), 0700);
	if (!writeFile(out.folder + "/name.txt", name) || !writeFile(out.circuit(), text)) return false;
	if (!source.empty()) writeFile(out.folder + "/source.txt", sourceMark(source, readFile(source)));
	syncapp::libraryChanged();
	return true;
}

bool imported(const std::string& file, Item& out) {
	const std::string text = readFile(file);
	if (text.empty()) return false;
	const std::string mark = sourceMark(file, text);
	for (const Item& it : items()) {
		if (readFile(it.folder + "/source.txt") == mark) { out = it; return true; }
	}
	return false;
}

void rename(const Item& item, const std::string& name) {
	writeFile(item.folder + "/name.txt", name);
	syncapp::libraryChanged();
}

bool moveToTrash(const Item& item) {
	const std::string trash = trashFolder();
	g_mkdir_with_parents(trash.c_str(), 0700);
	std::string dest = trash + "/" + item.id;
	for (int n = 2; fileExists(dest); n++) dest = trash + "/" + item.id + format(" %d", n);
	const bool moved = ::g_rename(item.folder.c_str(), dest.c_str()) == 0;
	if (moved) {
		stampDeleted(dest, now());
		syncapp::libraryChanged();
	}
	return moved;
}

// ---- Recently Deleted (docs/RECENTLY-DELETED.md) --------------------------------------

namespace {

const char* kStampFile = "deleted.txt";

// A folder and everything in it.
bool removeTree(const std::string& path) {
	if (g_file_test(path.c_str(), G_FILE_TEST_IS_SYMLINK) || !g_file_test(path.c_str(), G_FILE_TEST_IS_DIR))
		return g_remove(path.c_str()) == 0;
	if (GDir* d = g_dir_open(path.c_str(), 0, nullptr)) {
		while (const gchar* n = g_dir_read_name(d)) removeTree(path + "/" + n);
		g_dir_close(d);
	}
	return g_rmdir(path.c_str()) == 0;
}

// When it was deleted: its stamp, or (none yet) the time it was moved into
// the trash (the folder's status-change time) when that's within 30 days,
// else now, so an old one still gets its 30 days in the list. Stamped then.
double deletedTime(const std::string& folder) {
	const std::string text = trim(readFile(folder + "/" + kStampFile));
	if (!text.empty()) {
		GTimeZone* utc = g_time_zone_new_utc();
		GDateTime* t = g_date_time_new_from_iso8601(text.c_str(), utc);
		g_time_zone_unref(utc);
		if (t) {
			const double s = (double)g_date_time_to_unix(t);
			g_date_time_unref(t);
			return s;
		}
	}
	const double n = now();
	struct stat st;
	const double moved = stat(folder.c_str(), &st) == 0 ? (double)st.st_ctim.tv_sec : n;
	const double d = moved <= n && n - moved < kKeepDays * 86400.0 ? moved : n;
	stampDeleted(folder, d);
	return d;
}

}  // namespace

std::string trashFolder() { return root() + "/.Trash"; }

void stampDeleted(const std::string& folder, double when) {
	GDateTime* t = g_date_time_new_from_unix_utc((gint64)when);
	if (t == nullptr) return;
	gchar* s = g_date_time_format(t, "%Y-%m-%dT%H:%M:%SZ");
	g_date_time_unref(t);
	if (s) writeFile(folder + "/" + kStampFile, std::string(s) + "\n");
	g_free(s);
}

std::vector<Deleted> recentlyDeleted() {
	std::vector<Deleted> out;
	const std::string bin = trashFolder();
	GDir* d = g_dir_open(bin.c_str(), 0, nullptr);
	if (d == nullptr) return out;
	while (const gchar* n = g_dir_read_name(d)) {
		const std::string id = n;
		if (id.empty() || id[0] == '.') continue;
		Deleted c;
		c.id = id;
		c.folder = bin + "/" + id;
		if (!g_file_test(c.folder.c_str(), G_FILE_TEST_IS_DIR) || !fileExists(c.circuit())) continue;
		c.name = trim(readFile(c.folder + "/name.txt"));
		if (c.name.empty()) c.name = "Untitled";
		c.deleted = deletedTime(c.folder);
		out.push_back(c);
	}
	g_dir_close(d);
	std::sort(out.begin(), out.end(), [](const Deleted& a, const Deleted& b) { return a.deleted > b.deleted; });
	return out;
}

int sweepDeleted() {
	const double limit = now() - kKeepDays * 86400.0;
	int n = 0;
	for (const Deleted& c : recentlyDeleted())
		if (c.deleted < limit && removeTree(c.folder)) n++;
	return n;
}

std::string restoreDeleted(const Deleted& c) {
	// "<id> 2" was the second circuit with that id in the trash.
	std::string id = c.id;
	const size_t sp = id.find_last_of(' ');
	if (sp != std::string::npos && sp + 1 < id.size() && id.find_first_not_of("0123456789", sp + 1) == std::string::npos) id = id.substr(0, sp);
	if (id.empty() || fileExists(root() + "/" + id)) id = stamp(now()) + format("-%d", 1000 + g_random_int_range(0, 99000));
	const std::string dest = root() + "/" + id;
	if (::g_rename(c.folder.c_str(), dest.c_str()) != 0) return "";
	g_remove((dest + "/" + kStampFile).c_str());
	g_utime((dest + "/circuit.cdl").c_str(), nullptr);   // now: the top of Your Circuits
	syncapp::libraryChanged();
	return id;
}

bool deleteNow(const Deleted& c) { return removeTree(c.folder); }

int trashCheck() {
	int fails = 0;
	auto check = [&](bool ok, const std::string& what) {
		printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
		if (!ok) fails++;
	};
	auto circuitText = [](int gates) {
		std::string t = "(circuit\n";
		for (int g = 0; g < gates; g++) t += format("  (gate \"%s\" (uuid \"%d\") (at %d 0))\n", g % 2 == 0 ? "AA_AND2" : "AA_TOGGLE", g + 1, g * 8);
		return t + ")\n";
	};
	// The Mac's `trash` state: three circuits, the newest ("Counter") deleted
	// just now; one deleted 31 days ago; one moved there by sync with no
	// date; a second copy of a circuit still in the library; and the
	// "repeated versions" folder.
	Item made[3];
	const char* names[] = { "Half adder", "Traffic light", "Counter" };
	for (int i = 0; i < 3; i++) {
		create(names[i], circuitText(3), "", made[i]);
		g_usleep(1100000);   // (ids and times a second apart)
	}
	moveToTrash(made[2]);
	const std::string bin = trashFolder();
	auto trashed = [&](const std::string& folder, const std::string& name, double deleted) {
		const std::string f = bin + "/" + folder;
		g_mkdir_with_parents((f + "/versions").c_str(), 0700);
		writeFile(f + "/name.txt", name);
		writeFile(f + "/circuit.cdl", circuitText(3));
		if (deleted > 0) stampDeleted(f, deleted);
	};
	trashed("20240101-120000-11111", "Old one", now() - 31 * 86400.0);
	trashed("20250101-120000-22222", "From the phone", 0);
	trashed(made[0].id + " 2", "Half adder again", now() - 86400.0);
	const std::string repeated = bin + "/repeated versions/" + made[0].id;
	g_mkdir_with_parents(repeated.c_str(), 0700);
	writeFile(repeated + "/20240101-000000.cdl", circuitText(1));

	const gint64 t0 = g_get_monotonic_time();
	const std::vector<Deleted> before = recentlyDeleted();
	check((g_get_monotonic_time() - t0) < 1000000, "Recently Deleted lists the trash in under a second");
	std::set<std::string> got;
	std::string listed;
	for (const Deleted& c : before) { got.insert(c.name); listed += (listed.empty() ? "" : ", ") + c.name; }
	check(before.size() == 4 && got == std::set<std::string>{ "Counter", "Old one", "From the phone", "Half adder again" },
	      "Recently Deleted shows the 4 deleted circuits (" + listed + ")");
	check(!before.empty() && (before[0].name == "Counter" || before[0].name == "From the phone"),
	      "newest first (" + (before.empty() ? std::string("none") : before[0].name) + ")");
	bool listsRepeats = false;
	for (const Deleted& c : before) listsRepeats = listsRepeats || c.id == "repeated versions";
	check(!listsRepeats && fileExists(bin + "/repeated versions"), "the repeated versions folder isn't listed, and is left alone");
	for (const Deleted& c : before) {
		if (c.name == "Counter") check(std::abs(c.deleted - now()) < 600, "a circuit deleted from Your Circuits is dated when it was deleted");
		if (c.name == "From the phone")
			check(fileExists(c.folder + "/" + kStampFile) && std::abs(c.deleted - now()) < 600,
			      "one sync moved there with no date is dated when it was moved, and stamped");
	}
	const int swept = sweepDeleted();
	std::vector<Deleted> after = recentlyDeleted();
	bool oldThere = false;
	for (const Deleted& c : after) oldThere = oldThere || c.name == "Old one";
	check(swept == 1 && !oldThere, format("the sweep deleted the one from 31 days ago, only (%d)", swept));
	check(sweepDeleted() == 0, "a second sweep deletes nothing");

	// Restore: under its own id when that's free...
	auto findIn = [](const std::vector<Deleted>& v, const char* name, Deleted& out) {
		for (const Deleted& c : v) if (c.name == name) { out = c; return true; }
		return false;
	};
	Deleted c;
	if (findIn(after, "Counter", c)) {
		const std::string id = restoreDeleted(c);
		const std::vector<Item> items = library::items();
		const Item* back = nullptr;
		for (const Item& it : items) if (it.id == id) back = &it;
		check(id == c.id && back && back->name == "Counter", "Restore puts “Counter” back in Your Circuits under its own id");
		check(back && !fileExists(back->folder + "/" + kStampFile), "without its deleted date");
		check(!items.empty() && items[0].name == "Counter", "at the top of Your Circuits");
	} else {
		check(false, "Counter is in Recently Deleted");
	}
	// ...and under a new one when its id is taken (a second copy).
	if (findIn(after, "Half adder again", c)) {
		const std::string taken = c.id.substr(0, c.id.size() - 2);
		const std::string id = restoreDeleted(c);
		bool takenThere = false, newThere = false;
		for (const Item& it : library::items()) {
			takenThere = takenThere || it.id == taken;
			newThere = newThere || (it.id == id && it.name == "Half adder again");
		}
		check(!id.empty() && id != taken && id.find(' ') == std::string::npos && takenThere && newThere,
		      "a second copy of a circuit that's still there comes back beside it, under a new id (" + (id.empty() ? std::string("none") : id) + ")");
	} else {
		check(false, "the second copy is in Recently Deleted");
	}
	check(library::items().size() == 4, format("Your Circuits has 4 circuits now (%d)", (int)library::items().size()));
	// Delete Now.
	if (findIn(recentlyDeleted(), "From the phone", c)) check(deleteNow(c) && !fileExists(c.folder), "Delete Now deletes it for good");
	else check(false, "the sync-trashed circuit is in Recently Deleted");
	check(recentlyDeleted().empty(), "Recently Deleted is empty");
	printf("%s\n", fails == 0 ? "trash-check: all passed" : format("trash-check: %d failed", fails).c_str());
	return fails == 0 ? 0 : 1;
}

// It saves itself every few seconds, so not every save is a version: the
// latest waits in versions/.pending.cdl, and becomes a version when you come
// back after a break (10 minutes without saving), after half an hour of
// steady work, or at once on Ctrl+S.
bool noteSaved(const std::string& path, bool explicitSave) {
	Item item;
	if (!itemFor(path, item)) return false;
	g_mkdir_with_parents(item.versionsFolder().c_str(), 0700);
	const std::string pending = item.versionsFolder() + "/.pending.cdl";
	const double t = now();
	const std::vector<Version> vs = versions(item);
	bool kept = false;
	auto keep = [&](const std::string& src, double when) {
		// Not twice the same circuit in a row.
		if (!vs.empty() && shape(vs.front().path) == shape(src)) return;
		const std::string dest = item.versionsFolder() + "/" + stamp(when) + ".cdl";
		if (copyFile(src, dest)) kept = true;
	};
	if (explicitSave) {
		keep(path, t);
	} else if (fileExists(pending)) {
		const double pendingTime = modifiedTime(pending);
		const bool afterBreak = t - pendingTime > 10 * 60;
		// Half an hour since the last version was put away (its file's date).
		const bool longSession = t - (vs.empty() ? 0 : modifiedTime(vs.front().path)) > 30 * 60;
		if (afterBreak || longSession) keep(pending, pendingTime);
	} else if (vs.empty()) {
		keep(path, t);   // a circuit's first save
	}
	g_remove(pending.c_str());
	copyFile(path, pending);
	thin(item);
	syncapp::libraryChanged();
	return kept;
}

bool restore(const Item& item, const Version& v) {
	g_mkdir_with_parents(item.versionsFolder().c_str(), 0700);
	copyFile(item.circuit(), item.versionsFolder() + "/" + stamp(now()) + ".cdl");
	const std::string text = readFile(v.path);
	const bool ok = !text.empty() && writeFile(item.circuit(), text);
	if (ok) syncapp::libraryChanged();
	return ok;
}

std::string versionNote(const Version& v) {
	if (v.path.size() < 5) return std::string();
	return trim(readFile(v.path.substr(0, v.path.size() - 4) + ".txt"));
}

std::string friendlyTime(double t) {
	GDateTime* when = g_date_time_new_from_unix_local((gint64)t);
	GDateTime* today = g_date_time_new_now_local();
	if (when == nullptr || today == nullptr) {
		if (when) g_date_time_unref(when);
		if (today) g_date_time_unref(today);
		return std::string();
	}
	// 3:42 PM, or 15:42 where the language has no AM and PM.
	gchar* ampm = g_date_time_format(when, "%p");
	const bool twelveHour = ampm && *ampm;
	g_free(ampm);
	gchar* clock = g_date_time_format(when, twelveHour ? "%-l:%M %p" : "%H:%M");
	std::string c = clock ? trim(clock) : std::string();
	g_free(clock);
	// Calendar days between them (Dec 31 is yesterday on Jan 1).
	GDate* whenDay = g_date_new_dmy((GDateDay)g_date_time_get_day_of_month(when), (GDateMonth)g_date_time_get_month(when),
	                                (GDateYear)g_date_time_get_year(when));
	GDate* todayDay = g_date_new_dmy((GDateDay)g_date_time_get_day_of_month(today), (GDateMonth)g_date_time_get_month(today),
	                                 (GDateYear)g_date_time_get_year(today));
	const int days = g_date_days_between(whenDay, todayDay);
	g_date_free(whenDay);
	g_date_free(todayDay);
	std::string out;
	if (days == 0) out = "Today at " + c;
	else if (days == 1) out = "Yesterday at " + c;
	else {
		// "%-d", not "%e", which pads with a figure space: "Sep 3".
		gchar* date = g_date_time_format(when, g_date_time_get_year(when) == g_date_time_get_year(today) ? "%b %-d" : "%b %-d, %Y");
		std::string d = date ? date : "";
		g_free(date);
		out = d + " at " + c;
	}
	g_date_time_unref(when);
	g_date_time_unref(today);
	return out;
}

std::string agoText(double t) {
	const double s = now() - t;
	if (s < 60) return "just now";
	if (s < 3600) return format("%d min ago", (int)(s / 60));
	if (s < 86400) return format("%d hr ago", (int)(s / 3600));
	return format("%d days ago", (int)(s / 86400));
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
	if (path.empty() || !contains(path) || path.find("/.Trash/") != std::string::npos) return;
	if (prefs().lastCircuit == path) return;
	prefs().lastCircuit = path;
	prefs().save();
}

}  // namespace library
