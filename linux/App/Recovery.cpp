// Recovery copies: every open circuit with unsaved changes is written to
// ~/.local/share/CedarLogic/recovery now and then (Window.cpp decides when).
// A clean close removes its copy; a copy still there at the next launch, from
// a CedarLogic that isn't running any more, is offered back.

#include "Recovery.h"
#include "Alert.h"
#include "Window.h"

#include <algorithm>
#include <cstring>
#include <glib/gstdio.h>
#include <unistd.h>

namespace recovery {

namespace {

std::string dir() {
	gchar* d = g_build_filename(g_get_user_data_dir(), "CedarLogic", "recovery", nullptr);
	g_mkdir_with_parents(d, 0700);
	std::string s = d;
	g_free(d);
	return s;
}

std::string file(const std::string& base, const char* ext) { return dir() + "/" + base + ext; }

// Whether a CedarLogic with this process id is running now (a reused id
// belonging to something else doesn't count).
bool running(long pid) {
	if (pid <= 0) return false;
	if (pid == (long)getpid()) return true;
	// /proc/<pid>/stat is "pid (name) state ...": a zombie (Z) or dead (X)
	// process has stopped, whatever its name.
	gchar* stat = nullptr;
	const std::string path = format("/proc/%ld/stat", pid);
	if (!g_file_get_contents(path.c_str(), &stat, nullptr, nullptr)) return false;
	const char* open = strchr(stat, '(');
	const char* close = strrchr(stat, ')');
	bool ours = false;
	if (open && close && close > open) {
		const std::string name(open + 1, close);
		const char state = close[1] == ' ' ? close[2] : 0;
		ours = (name.rfind("cedarlogic", 0) == 0 || name.rfind("CedarLogic", 0) == 0) && state != 'Z' && state != 'X';
	}
	g_free(stat);
	return ours;
}

struct Found { std::string base, path, name; };

std::vector<Found> orphans() {
	std::vector<Found> out;
	GDir* d = g_dir_open(dir().c_str(), 0, nullptr);
	if (d == nullptr) return out;
	while (const gchar* entry = g_dir_read_name(d)) {
		if (!g_str_has_suffix(entry, ".info")) continue;
		std::string base(entry, strlen(entry) - 5);
		if (running(strtol(base.c_str(), nullptr, 10))) continue;
		if (!g_file_test(file(base, ".cdl").c_str(), G_FILE_TEST_EXISTS)) { remove(base); continue; }
		GKeyFile* k = g_key_file_new();
		Found f{ base, "", "" };
		if (g_key_file_load_from_file(k, file(base, ".info").c_str(), G_KEY_FILE_NONE, nullptr)) {
			if (gchar* p = g_key_file_get_string(k, "recovery", "path", nullptr)) { f.path = p; g_free(p); }
			if (gchar* n = g_key_file_get_string(k, "recovery", "name", nullptr)) { f.name = n; g_free(n); }
		}
		g_key_file_free(k);
		out.push_back(f);
	}
	g_dir_close(d);
	return out;
}

}  // namespace

std::string newBase() {
	static int n = 0;
	return format("%ld-%d", (long)getpid(), ++n);
}

bool write(const std::string& base, const std::string& text, const std::string& path, const std::string& name) {
	if (!g_file_set_contents(file(base, ".cdl").c_str(), text.data(), (gssize)text.size(), nullptr)) return false;
	GKeyFile* k = g_key_file_new();
	g_key_file_set_string(k, "recovery", "path", path.c_str());
	g_key_file_set_string(k, "recovery", "name", name.c_str());
	g_key_file_set_int64(k, "recovery", "written", g_get_real_time() / G_USEC_PER_SEC);
	const bool ok = g_key_file_save_to_file(k, file(base, ".info").c_str(), nullptr);
	g_key_file_free(k);
	return ok;
}

void remove(const std::string& base) {
	if (base.empty()) return;
	g_unlink(file(base, ".cdl").c_str());
	g_unlink(file(base, ".info").c_str());
}

void offer(GtkApplication* app, CircuitWindow* reuse) {
	GtkWindow* parent = reuse ? reuse->window() : nullptr;
	const std::vector<Found> found = orphans();
	if (found.empty()) return;
	std::string names;
	for (const Found& f : found) names += "• " + (f.name.empty() ? std::string("Untitled") : f.name) + "\n";
	Alert a;
	a.heading = found.size() == 1 ? "CedarLogic closed before this circuit was saved"
	                              : "CedarLogic closed before these circuits were saved";
	a.text = names + "A copy of the work from just before it closed was kept. Open it again?";
	a.badge = 2;
	a.buttons = { { "Open", 1, 1 }, { "Later", 0, 0 }, { "Throw Away", 2, 2, true } };
	a.enter = 1;
	a.escape = 0;
	const int answer = runAlert(parent, a);
	if (answer == 0) return;   // asked again next launch
	for (const Found& f : found) {
		if (answer == 1) {
			char err[512] = "";
			CLDocument* doc = cl_document_open(file(f.base, ".cdl").c_str(), err, sizeof err);
			if (doc == nullptr) {
				showMessage(parent, GTK_MESSAGE_WARNING, "A kept copy couldn't be opened", err);
				continue;   // left in place, in case a later version can read it
			}
			// Back under its own name, marked unsaved: saving puts it where it was.
			// The circuit may be open already (launch opens the last one, as it
			// was before the lost changes): the copy takes that window's place,
			// so two windows never save over the same file. One with changes of
			// its own keeps them, and the copy comes back as a circuit apart.
			CircuitWindow* already = nullptr;
			if (!f.path.empty())
				for (CircuitWindow* c : circuitWindows()) if (c->filePath() == f.path) already = c;
			std::string path = f.path, name = f.name;
			CircuitWindow* w = reuse && reuse->isPristine() ? reuse : nullptr;
			if (already && !already->isDirty()) w = already;
			else if (already) { path.clear(); name = (name.empty() ? std::string("Untitled") : name) + " (recovered)"; }
			if (w) { w->replaceDocument(doc, path); gtk_window_present(w->window()); }
			else w = new CircuitWindow(app, doc, path);
			w->markRecovered(name);
			reuse = nullptr;
		}
		remove(f.base);
	}
}

}  // namespace recovery
