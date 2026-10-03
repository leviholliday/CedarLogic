// Your Circuits (see Library.h).

#include "Library.h"

#include <glib/gstdio.h>
#include <sys/stat.h>

#include <algorithm>
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
		if (kept.count(bucket)) g_remove(v.path.c_str());
		else kept.insert(bucket);
	}
}

}  // namespace

std::string root() {
	static std::string r = [] {
		const std::string dir = std::string(g_get_user_data_dir()) + "/CedarLogic/Library";
		g_mkdir_with_parents(dir.c_str(), 0755);
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
		out.push_back(Version{ p, modifiedTime(p) });
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
	g_mkdir_with_parents(out.versionsFolder().c_str(), 0755);
	if (!writeFile(out.folder + "/name.txt", name) || !writeFile(out.circuit(), text)) return false;
	if (!source.empty()) writeFile(out.folder + "/source.txt", sourceMark(source, readFile(source)));
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

void rename(const Item& item, const std::string& name) { writeFile(item.folder + "/name.txt", name); }

bool moveToTrash(const Item& item) {
	const std::string trash = root() + "/.Trash";
	g_mkdir_with_parents(trash.c_str(), 0755);
	std::string dest = trash + "/" + item.id;
	for (int n = 2; fileExists(dest); n++) dest = trash + "/" + item.id + format(" %d", n);
	return ::g_rename(item.folder.c_str(), dest.c_str()) == 0;
}

// It saves itself every few seconds, so not every save is a version: the
// latest waits in versions/.pending.cdl, and becomes a version when you come
// back after a break (10 minutes without saving), after half an hour of
// steady work, or at once on Ctrl+S.
bool noteSaved(const std::string& path, bool explicitSave) {
	Item item;
	if (!itemFor(path, item)) return false;
	g_mkdir_with_parents(item.versionsFolder().c_str(), 0755);
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
		const bool longSession = t - (vs.empty() ? 0 : vs.front().time) > 30 * 60;
		if (afterBreak || longSession) keep(pending, pendingTime);
	} else if (vs.empty()) {
		keep(path, t);   // a circuit's first save
	}
	g_remove(pending.c_str());
	copyFile(path, pending);
	thin(item);
	return kept;
}

bool restore(const Item& item, const Version& v) {
	g_mkdir_with_parents(item.versionsFolder().c_str(), 0755);
	copyFile(item.circuit(), item.versionsFolder() + "/" + stamp(now()) + ".cdl");
	const std::string text = readFile(v.path);
	return !text.empty() && writeFile(item.circuit(), text);
}

std::string friendlyTime(double t) {
	GDateTime* when = g_date_time_new_from_unix_local((gint64)t);
	GDateTime* today = g_date_time_new_now_local();
	if (when == nullptr || today == nullptr) {
		if (when) g_date_time_unref(when);
		if (today) g_date_time_unref(today);
		return std::string();
	}
	gchar* clock = g_date_time_format(when, "%l:%M %p");
	std::string c = clock ? trim(clock) : std::string();
	g_free(clock);
	const int days = (g_date_time_get_year(today) - g_date_time_get_year(when)) * 366 + g_date_time_get_day_of_year(today) -
	                 g_date_time_get_day_of_year(when);
	std::string out;
	if (days == 0) out = "Today at " + c;
	else if (days == 1) out = "Yesterday at " + c;
	else {
		gchar* date = g_date_time_format(when, g_date_time_get_year(when) == g_date_time_get_year(today) ? "%b %e" : "%b %e, %Y");
		std::string d = date ? date : "";
		g_free(date);
		// "%e" pads with a space: "Sep  3" -> "Sep 3".
		const size_t dbl = d.find("  ");
		if (dbl != std::string::npos) d.erase(dbl, 1);
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
