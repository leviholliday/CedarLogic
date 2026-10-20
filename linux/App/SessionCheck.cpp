// --session-check (see SessionCheck.h).

#include "SessionCheck.h"
#include "Canvas.h"
#include "Dialogs.h"
#include "FindBar.h"
#include "Library.h"
#include "Notes.h"
#include "Settings.h"
#include "TabStrip.h"
#include "TabSwitcher.h"
#include "Window.h"

#include <execinfo.h>
#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

// ---- What the session is doing, for the crash report and the log ---------------

namespace {

volatile sig_atomic_t gStep = -1;
const char* volatile gWhat = "starting";
bool gActive = false;   // the session is under way (the watchdog runs)

// Warnings and criticals from GTK, GLib and the rest, with the action each came in.
int gWarnings = 0;
std::vector<std::string> gWarningLines;

void crashed(int sig) {
	char line[512];
	const char* what = gWhat ? gWhat : "?";
	const int n = snprintf(line, sizeof line, "\nFAIL no crash: signal %d at action %d (%s)\n", (int)sig, (int)gStep, what);
	if (n > 0) {
		ssize_t w = write(STDOUT_FILENO, line, (size_t)n);
		w = write(STDERR_FILENO, line, (size_t)n);
		(void)w;
	}
	void* frames[96];
	const int k = backtrace(frames, 96);
	backtrace_symbols_fd(frames, k, STDERR_FILENO);
	backtrace_symbols_fd(frames, k, STDOUT_FILENO);
	signal(sig, SIG_DFL);
	raise(sig);
}

GLogWriterOutput logWriter(GLogLevelFlags level, const GLogField* fields, gsize n, gpointer) {
	if (level & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING)) {
		std::string domain, message;
		for (gsize i = 0; i < n; i++) {
			if (strcmp(fields[i].key, "GLIB_DOMAIN") == 0 && fields[i].length < 0) domain = (const char*)fields[i].value;
			if (strcmp(fields[i].key, "MESSAGE") == 0 && fields[i].length < 0) message = (const char*)fields[i].value;
		}
		gWarnings++;
		if (gWarningLines.size() < 30) {
			char where[160];
			snprintf(where, sizeof where, "action %d (%s): ", (int)gStep, gWhat ? gWhat : "?");
			gWarningLines.push_back(std::string(where) + (level & G_LOG_LEVEL_CRITICAL ? "CRITICAL " : "WARNING ") +
			                        (domain.empty() ? "" : domain + ": ") + message);
		}
	}
	return g_log_writer_default(level, fields, n, nullptr);
}

double nowMs() { return g_get_monotonic_time() / 1000.0; }

// The main loop, for a while: drawing, timers, the X server's events.
void pump(double ms) {
	const double end = nowMs() + ms;
	do {
		while (g_main_context_iteration(nullptr, FALSE)) {}
		if (nowMs() >= end) break;
		g_usleep(1000);
	} while (true);
}

struct Rng {
	unsigned long long s;
	unsigned long long next() {
		s += 0x9E3779B97F4A7C15ull;
		unsigned long long z = s;
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
		return z ^ (z >> 31);
	}
	int below(int n) { return n <= 1 ? 0 : (int)(next() % (unsigned long long)n); }
	double unit() { return (double)(next() % 1000000) / 1e6; }
	bool chance(double p) { return unit() < p; }
};

// The circuit's text less what running it changes (switches, registers,
// the app that wrote it), as the Mac's check compares them.
// A segment's connections in one order: the engine writes them in the order
// they're held, which a reopened file needn't repeat (the same circuit).
void sortConnects(std::vector<std::string>& lines) {
	for (size_t i = 0; i < lines.size();) {
		auto isConnect = [&](size_t k) { return k < lines.size() && lines[k].find("(connect ") != std::string::npos; };
		if (!isConnect(i)) { i++; continue; }
		size_t j = i;
		while (isConnect(j)) j++;
		if (j - i > 1) {
			// The last carries the closing parentheses of what it ends.
			std::string& last = lines[j - 1];
			size_t core = last.find(')') + 1;
			const std::string tail = last.substr(core);
			last = last.substr(0, core);
			std::sort(lines.begin() + (long)i, lines.begin() + (long)j);
			lines[j - 1] += tail;
		}
		i = j;
	}
}

std::string shape(const std::string& text) {
	std::vector<std::string> kept;
	std::string out;
	size_t at = 0;
	while (at < text.size()) {
		size_t end = text.find('\n', at);
		if (end == std::string::npos) end = text.size();
		std::string line = text.substr(at, end - at);
		size_t k = 0;
		while (k < line.size() && (line[k] == ' ' || line[k] == '\t')) k++;
		const std::string t = line.substr(k);
		if (t.rfind("(lparam \"OUTPUT_NUM\"", 0) != 0 && t.rfind("(lparam \"CURRENT_VALUE\"", 0) != 0 && t.rfind("(generator ", 0) != 0)
			kept.push_back(line);
		at = end + 1;
	}
	sortConnects(kept);
	for (const std::string& l : kept) { out += l; out += '\n'; }
	return out;
}

std::string readFile(const std::string& path) {
	gchar* data = nullptr;
	gsize len = 0;
	if (!g_file_get_contents(path.c_str(), &data, &len, nullptr)) return std::string();
	std::string s(data, len);
	g_free(data);
	return s;
}

long residentKB() {
	FILE* f = fopen("/proc/self/statm", "r");
	if (f == nullptr) return 0;
	long pages = 0, resident = 0;
	if (fscanf(f, "%ld %ld", &pages, &resident) != 2) resident = 0;
	fclose(f);
	return resident * (sysconf(_SC_PAGESIZE) / 1024);
}

// The first line where two texts differ, for a FAIL line.
std::string firstDifference(const std::string& a, const std::string& b) {
	size_t i = 0;
	while (i < a.size() && i < b.size() && a[i] == b[i]) i++;
	auto lineAt = [](const std::string& s, size_t i) {
		size_t from = s.rfind('\n', i == 0 ? 0 : i - 1);
		from = from == std::string::npos ? 0 : from + 1;
		size_t to = s.find('\n', i);
		std::string l = s.substr(from, (to == std::string::npos ? s.size() : to) - from);
		if (l.size() > 90) l = l.substr(0, 90) + "...";
		return l;
	};
	return "\"" + lineAt(a, i) + "\" vs \"" + lineAt(b, i) + "\"";
}

// Pins of the parts placed, from the gate library: offsets from the part's middle.
std::map<std::string, std::vector<std::pair<double, double>>> gPins;

void loadPins(const std::vector<std::string>& names) {
	const std::string lib = readFile(resourcesDir() + "/cl_gatedefs.xml");
	for (const std::string& name : names) {
		const size_t at = lib.find("<name>" + name + "</name>");
		if (at == std::string::npos) continue;
		const size_t end = lib.find("</gate>", at);
		size_t p = at;
		while (true) {
			const size_t in = lib.find("<input>", p), outp = lib.find("<output>", p);
			const size_t pin = std::min(in, outp);
			if (pin == std::string::npos || pin > end) break;
			const size_t pt = lib.find("<point>", pin);
			if (pt == std::string::npos || pt > end) break;
			double x = 0, y = 0;
			if (sscanf(lib.c_str() + pt + 7, "%lf,%lf", &x, &y) == 2) gPins[name].push_back({ x, y });
			p = pt + 7;
		}
	}
}

const char* const kParts[] = { "AA_AND2", "AE_OR2", "AA_INVERTER", "AI_XOR2", "AA_TOGGLE", "GA_LED", "BB_CLOCK", "AA_DFF", "DE_TO", "AA_LABEL" };

// Menu items a random pick may run (the rest open files, the web, or quit).
const char* const kSafeMenuActions[] = {
	"win.undo", "win.redo", "win.cut", "win.copy", "win.paste", "win.duplicate", "win.delete", "win.select-all",
	"win.rotate", "win.straighten", "win.tidy", "win.tidy-flow", "win.connect-nearby", "win.lock-in-place",
	"win.unlock-parts", "win.unlock-all", "win.zoom-in", "win.zoom-out", "win.zoom-fit", "win.zoom-actual",
	"win.split-view", "win.switch-pane", "win.close-split", "win.running", "win.step", "win.step-clock",
	"win.sim-view", "win.lock", "win.scope", "win.new-tab", "win.reopen-tab", "win.next-tab", "win.previous-tab",
	"win.draw", "win.show-drawing", "win.notes", "win.connect-all-pages", "win.disconnect-all-pages",
	"win.status-bar", "win.palette", "win.find", "win.gate-settings", "win.truth-table", "win.check-circuit",
	"win.ink-pen", "win.ink-highlighter", "win.ink-eraser", "win.clear-drawing", "win.tidy-keep", "win.tidy-revert",
	"win.tidy-switch",
};

bool safeMenuAction(const char* a) {
	if (a == nullptr) return true;   // a submenu, or the tab menu's own items
	for (const char* s : kSafeMenuActions) if (strcmp(s, a) == 0) return true;
	return false;
}

}  // namespace

// ---- Input, as the display delivers it ----------------------------------------

struct SessionDriver {
	GtkApplication* app = nullptr;
	CircuitWindow* w = nullptr;
	Rng rng{ 1 };
	int failures = 0;
	std::vector<std::string> notes;   // the first few details of each failure

	// Counters for the invariants (checked at the end).
	int editsChecked = 0, editsLost = 0, firstLost = -1;
	int pauses = 0, pausesUnsaved = 0, pausesStale = 0, firstStale = -1, reopenMismatch = 0, firstReopen = -1;
	int roundTrips = 0, roundTripBad = 0, firstRoundTrip = -1;
	int historyBad = 0, firstHistory = -1, maxUndo = 0;
	int pagesBad = 0, firstPages = -1;
	int stuckWindows = 0;
	int switches = 0, switchesWrong = 0, staleStamps = 0;
	std::string lastSwitchIssue;
	int prefsChecks = 0, prefsUnsaved = 0;
	bool settingsClosed = false;
	int modalsSeen = 0, menusSeen = 0, menuItemsRun = 0;
	int counts[64] = {};
	std::string lastPageIssue, lastStaleIssue, lastRoundTripIssue, lastReopenIssue, lastPrefsIssue;

	static SessionDriver* current;

	void fail(const std::string& what) {
		if (notes.size() < 40) notes.push_back(what);
	}
	bool check(bool ok, const std::string& what) {
		printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
		if (!ok) failures++;
		return ok;
	}

	CLDocument* doc() const { return w->document(); }
	bool alive() const { return std::find(circuitWindows().begin(), circuitWindows().end(), w) != circuitWindows().end(); }
	int windowCloses = 0;
	// A circuit's texts, kept for a FAIL (SESSION_DUMP names a folder for them).
	void dump(const std::string& name, const std::string& text) {
		const char* dir = g_getenv("SESSION_DUMP");
		if (dir == nullptr || *dir == 0) return;
		g_mkdir_with_parents(dir, 0755);
		const std::string file = std::string(dir) + "/" + name;
		g_file_set_contents(file.c_str(), text.data(), (gssize)text.size(), nullptr);
		printf("  (kept %s)\n", file.c_str());
	}
	Canvas* canvas() const { return w->currentCanvas(); }
	GtkWidget* top() const { return GTK_WIDGET(w->window()); }

	static GdkDevice* pointer() {
		GdkSeat* seat = gdk_display_get_default_seat(gdk_display_get_default());
		return seat ? gdk_seat_get_pointer(seat) : nullptr;
	}
	static GdkDevice* keyboard() {
		GdkSeat* seat = gdk_display_get_default_seat(gdk_display_get_default());
		return seat ? gdk_seat_get_keyboard(seat) : nullptr;
	}
	static guint32 eventTime() { return (guint32)(g_get_monotonic_time() / 1000); }

	// The real pointer goes there too (the app asks where it is: a paste lands
	// under it, a gate from the side panel appears at it).
	static void warp(GdkWindow* gw, double x, double y) {
		int ox = 0, oy = 0;
		gdk_window_get_origin(gw, &ox, &oy);
		if (GdkDevice* p = pointer()) gdk_device_warp(p, gdk_window_get_screen(gw), ox + (int)x, oy + (int)y);
	}

	static void button(GtkWidget* widget, GdkEventType type, double x, double y, guint b, guint state) {
		GdkWindow* gw = gtk_widget_get_window(widget);
		if (gw == nullptr || !gtk_widget_get_mapped(widget)) return;
		GdkEvent* e = gdk_event_new(type);
		e->button.window = GDK_WINDOW(g_object_ref(gw));
		e->button.time = eventTime();
		e->button.x = x;
		e->button.y = y;
		int ox = 0, oy = 0;
		gdk_window_get_origin(gw, &ox, &oy);
		e->button.x_root = ox + x;
		e->button.y_root = oy + y;
		e->button.state = state;
		e->button.button = b;
		if (GdkDevice* p = pointer()) { gdk_event_set_device(e, p); gdk_event_set_source_device(e, p); }
		gtk_main_do_event(e);
		gdk_event_free(e);
	}

	static void motion(GtkWidget* widget, double x, double y, guint state) {
		GdkWindow* gw = gtk_widget_get_window(widget);
		if (gw == nullptr || !gtk_widget_get_mapped(widget)) return;
		warp(gw, x, y);
		GdkEvent* e = gdk_event_new(GDK_MOTION_NOTIFY);
		e->motion.window = GDK_WINDOW(g_object_ref(gw));
		e->motion.time = eventTime();
		e->motion.x = x;
		e->motion.y = y;
		int ox = 0, oy = 0;
		gdk_window_get_origin(gw, &ox, &oy);
		e->motion.x_root = ox + x;
		e->motion.y_root = oy + y;
		e->motion.state = state;
		if (GdkDevice* p = pointer()) { gdk_event_set_device(e, p); gdk_event_set_source_device(e, p); }
		gtk_main_do_event(e);
		gdk_event_free(e);
	}

	static void scroll(GtkWidget* widget, double x, double y, double dy, guint state) {
		GdkWindow* gw = gtk_widget_get_window(widget);
		if (gw == nullptr || !gtk_widget_get_mapped(widget)) return;
		GdkEvent* e = gdk_event_new(GDK_SCROLL);
		e->scroll.window = GDK_WINDOW(g_object_ref(gw));
		e->scroll.time = eventTime();
		e->scroll.x = x;
		e->scroll.y = y;
		e->scroll.state = state;
		e->scroll.direction = dy < 0 ? GDK_SCROLL_UP : GDK_SCROLL_DOWN;
		if (GdkDevice* p = pointer()) { gdk_event_set_device(e, p); gdk_event_set_source_device(e, p); }
		gtk_main_do_event(e);
		gdk_event_free(e);
	}

	// A key to a window, as the keyboard sends it: the window's shortcuts
	// first, then the widget with the keyboard.
	static void key(GtkWidget* toplevel, guint keyval, guint state, bool press) {
		GdkWindow* gw = toplevel ? gtk_widget_get_window(toplevel) : nullptr;
		if (gw == nullptr) return;
		GdkEvent* e = gdk_event_new(press ? GDK_KEY_PRESS : GDK_KEY_RELEASE);
		e->key.window = GDK_WINDOW(g_object_ref(gw));
		e->key.time = eventTime();
		e->key.state = state;
		e->key.keyval = keyval;
		GdkKeymapKey* keys = nullptr;
		gint n = 0;
		if (gdk_keymap_get_entries_for_keyval(gdk_keymap_get_for_display(gdk_window_get_display(gw)), keyval, &keys, &n) && n > 0) {
			e->key.hardware_keycode = (guint16)keys[0].keycode;
			e->key.group = (guint8)keys[0].group;
		}
		g_free(keys);
		const gunichar c = gdk_keyval_to_unicode(keyval);
		char utf8[8] = "";
		if (c && !(state & GDK_CONTROL_MASK)) utf8[g_unichar_to_utf8(c, utf8)] = 0;
		e->key.string = g_strdup(utf8);
		e->key.length = (gint)strlen(utf8);
		if (GdkDevice* k = keyboard()) { gdk_event_set_device(e, k); gdk_event_set_source_device(e, k); }
		gtk_main_do_event(e);
		gdk_event_free(e);
	}
	static void tap(GtkWidget* toplevel, guint keyval, guint state = 0) {
		if (toplevel == nullptr) return;
		// The key may close the window (Escape): the release then goes nowhere.
		g_object_ref(toplevel);
		key(toplevel, keyval, state, true);
		if (!gtk_widget_in_destruction(toplevel)) key(toplevel, keyval, state, false);
		g_object_unref(toplevel);
	}

	// ---- The canvas, in view points --------------------------------------------

	static void toView(Canvas* c, double wx, double wy, double& vx, double& vy) {
		vx = (wx - c->originX) / c->upp;
		vy = (c->originY - wy) / c->upp;
	}
	static bool inView(Canvas* c, double vx, double vy) { return vx >= 2 && vy >= 2 && vx < c->width() - 2 && vy < c->height() - 2; }

	void click(Canvas* c, double vx, double vy, guint b = 1, guint state = 0) {
		GtkWidget* a = c->widget();
		motion(a, vx, vy, state);
		button(a, GDK_BUTTON_PRESS, vx, vy, b, state);
		pump(8);
		button(a, GDK_BUTTON_RELEASE, vx, vy, b, state | (b == 1 ? GDK_BUTTON1_MASK : b == 2 ? GDK_BUTTON2_MASK : GDK_BUTTON3_MASK));
	}

	// Press, a few moves (each drawn, as on screen), and maybe a release.
	void drag(GtkWidget* a, double x0, double y0, double x1, double y1, int steps, guint state, guint b = 1, bool release = true) {
		const guint held = b == 1 ? GDK_BUTTON1_MASK : b == 2 ? GDK_BUTTON2_MASK : GDK_BUTTON3_MASK;
		motion(a, x0, y0, state);
		button(a, GDK_BUTTON_PRESS, x0, y0, b, state);
		for (int i = 1; i <= steps; i++) {
			const double t = (double)i / steps;
			motion(a, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, state | held);
			pump(4 + rng.below(14));
		}
		if (release) button(a, GDK_BUTTON_RELEASE, x1, y1, b, state | held);
	}

	// Parts on the page in front: where, and which.
	struct Spot { long id; double x, y; std::string type; };
	std::vector<Spot> parts() {
		std::vector<Spot> out;
		std::set<long> seen;
		const int page = w->currentPage();
		static CLFindResult found[512];
		for (const char* q : { "aa_", "ae_", "ai_", "ga_", "bb_", "de_" }) {
			const int n = std::min(512, cl_find(doc(), q, found, 512));
			for (int i = 0; i < n; i++) {
				if (found[i].page != page || seen.count(found[i].gate)) continue;
				seen.insert(found[i].gate);
				const char* t = cl_gate_library_name(doc(), found[i].gate);
				out.push_back({ found[i].gate, found[i].x, found[i].y, t ? t : "" });
			}
		}
		return out;
	}
	// A pin of a part, in the world (one of four turns, as the part may have been rotated).
	bool pinOf(const Spot& s, double& x, double& y) {
		auto it = gPins.find(s.type);
		if (it == gPins.end() || it->second.empty()) return false;
		const auto& p = it->second[(size_t)rng.below((int)it->second.size())];
		const int turn = rng.chance(0.6) ? 0 : rng.below(4);
		double dx = p.first, dy = p.second;
		for (int i = 0; i < turn; i++) { const double t = dx; dx = dy; dy = -t; }
		x = s.x + dx;
		y = s.y + dy;
		return true;
	}
	// Somewhere on a wire near a part, found by the hover (false: none close).
	bool wirePoint(const Spot& s, double& x, double& y) {
		const int page = w->currentPage();
		char bits[8];
		for (int i = 0; i < 40; i++) {
			const double px = s.x + (rng.unit() - 0.5) * 14, py = s.y + (rng.unit() - 0.5) * 10;
			cl_edit_hover_wire(doc(), page, px, py, canvas()->upp);
			if (cl_edit_hover_wire_state(doc(), page, bits, sizeof bits) > 0) { x = px; y = py; return true; }
		}
		cl_edit_hover_clear(doc());
		return false;
	}
	void randomWorld(double& x, double& y) {
		Canvas* c = canvas();
		c->worldPoint(20 + rng.unit() * std::max(10.0, c->width() - 40), 20 + rng.unit() * std::max(10.0, c->height() - 40), x, y);
	}

	// ---- Invariants after every action -----------------------------------------

	std::string text() { return shape(cl_document_save_text_ex(doc(), 0)); }

	void checkPages(int at) {
		std::string issue;
		const int n = cl_document_page_count(doc());
		if ((int)w->canvases.size() != n) issue = format("%zu tabs for %d pages", w->canvases.size(), n);
		for (size_t i = 0; i < w->canvases.size() && issue.empty(); i++)
			if (w->canvases[i]->page() != (int)i) issue = format("tab %zu shows page %d", i, w->canvases[i]->page());
		const int cur = w->currentPage();
		if (issue.empty() && (cur < 0 || cur >= n)) issue = format("the page in front is %d of %d", cur, n);
		for (int pane = 0; pane < 2 && issue.empty(); pane++) {
			if (pane == 1 && !w->splitOpen()) break;
			const int s = w->shownPage(pane);
			if (s < 0 || s >= n) issue = format("side %d shows page %d of %d", pane, s, n);
			else if (w->paneOf(w->canvases[(size_t)s]) != pane) issue = format("side %d shows page %d, which is on the other side", pane, s);
			else if (w->panePages(pane).empty()) issue = format("side %d has no tabs", pane);
		}
		if (issue.empty() && w->splitOpen() && (w->panePages(0).empty() || w->panePages(1).empty())) issue = "a split with an empty side";
		for (uint64_t k : w->sideKeys)
			if (issue.empty() && cl_document_page_index(doc(), k) < 0) issue = "the second side keeps a page that's gone";
		if (issue.empty() && Canvas::liveCount < (int)w->canvases.size()) issue = "fewer canvases alive than tabs";
		if (!issue.empty()) {
			pagesBad++;
			if (firstPages < 0) { firstPages = at; lastPageIssue = issue; }
		}
		const int undo = cl_edit_undo_count(doc());
		maxUndo = std::max(maxUndo, undo);
		if (undo < 0 || undo > 500 || (undo > 0) != cl_edit_can_undo(doc())) {
			historyBad++;
			if (firstHistory < 0) firstHistory = at;
		}
	}

	void checkEdited(int at, const std::string& before) {
		// Tidy Up's preview isn't an edit until it's kept (Return, or the next edit).
		if (w->tidyActive()) return;
		const std::string after = text();
		if (after == before) return;
		editsChecked++;
		if (w->isDirty()) return;
		// Saved already (autosave ran during the action): the file has it.
		const std::string disk = w->filePath().empty() ? std::string() : shape(readFile(w->filePath()));
		if (disk == after) return;
		editsLost++;
		if (firstLost < 0) { firstLost = at; fail(format("action %d (%s): the circuit changed, the window says saved, the file is older", at, (const char*)gWhat)); }
	}

	// Nothing half-done (autosave waits for a drag, a gate on the pointer,
	// Tidy Up's preview), then a pause for autosave.
	void settle() {
		pump(150);   // a paste asked for lands (the clipboard answers later)
		Canvas* c = canvas();
		if (c && c->isDragging()) c->cancelDrag();
		if (w->hasPendingGate()) w->clearPendingGate();
		if (w->isFloating()) { double vx = c->width() / 2, vy = c->height() / 2; click(c, vx, vy); }
		if (w->isFloating()) w->cancelFloating();
		if (w->tidyActive()) w->endTidy(true);
		if (cl_edit_is_connecting(doc())) cl_edit_cancel(doc());
		pump(30);
	}

	void pause(int at) {
		settle();
		pump(2700);
		pauses++;
		bool anyGates = false;
		for (int p = 0; p < cl_document_page_count(doc()) && !anyGates; p++) anyGates = cl_document_gate_count(doc(), p) > 0;
		if (w->filePath().empty() && !anyGates) return;   // an empty new circuit isn't kept
		if (w->isDirty()) {
			pausesUnsaved++;
			fail(format("pause at action %d: still unsaved (autosave %s, busy %d, floating %d, tidy %d, connecting %d)", at,
			            w->autosaveId ? "waiting" : "not asked for", w->busyEditing(), w->isFloating(), w->tidyActive(),
			            cl_edit_is_connecting(doc())));
		}
		const std::string onDisk = readFile(w->filePath());
		const std::string now = text();
		if (shape(onDisk) != now) {
			pausesStale++;
			if (firstStale < 0) {
				firstStale = at;
				lastStaleIssue = firstDifference(shape(onDisk), now);
				dump(format("stale-%d-disk.cdl", at), onDisk);
				dump(format("stale-%d-window.cdl", at), cl_document_save_text_ex(doc(), 0));
			}
		}
		// The file opens again to the same circuit.
		char err[256] = "";
		CLDocument* again = cl_document_open_text(onDisk.data(), (long)onDisk.size(), err, sizeof err);
		const std::string rereadFull = again ? std::string(cl_document_save_text_ex(again, 0)) : std::string("(didn't open: ") + err + ")";
		const std::string reread = again ? shape(rereadFull) : rereadFull;
		if (again) cl_document_close(again);
		if (reread != shape(onDisk)) {
			reopenMismatch++;
			if (firstReopen < 0) {
				firstReopen = at;
				lastReopenIssue = firstDifference(shape(onDisk), reread);
				dump(format("reopen-%d-saved.cdl", at), onDisk);
				dump(format("reopen-%d-reopened.cdl", at), rereadFull);
			}
		}
	}

	// Undo some steps, redo them: the circuit is back as it was.
	void undoRedo(int at, int k) {
		settle();
		if (Canvas* c = canvas()) gtk_widget_grab_focus(c->widget());
		const std::string before = text();
		int undone = 0;
		for (int i = 0; i < k; i++) {
			if (!cl_edit_can_undo(doc())) break;
			tap(top(), GDK_KEY_z, GDK_CONTROL_MASK);
			undone++;
			if (i % 3 == 2) pump(5);
		}
		for (int i = 0; i < undone; i++) {
			if (rng.chance(0.5)) tap(top(), GDK_KEY_Z, GDK_CONTROL_MASK | GDK_SHIFT_MASK);
			else tap(top(), GDK_KEY_y, GDK_CONTROL_MASK);
			if (i % 3 == 2) pump(5);
		}
		pump(10);
		roundTrips++;
		const std::string after = text();
		if (after != before) {
			roundTripBad++;
			if (firstRoundTrip < 0) {
				firstRoundTrip = at;
				lastRoundTripIssue = format("%d undone and redone: ", undone) + firstDifference(before, after);
				dump(format("undo-%d-before.cdl", at), before);
				dump(format("undo-%d-after.cdl", at), after);
			}
		}
	}

	// ---- Windows over the circuit window: poked at, then closed ----------------

	struct Seen { double since; int pokes, wanted, closes; bool settings; };
	std::map<GtkWidget*, Seen> seen;

	static bool isCircuitWindow(GtkWidget* t) {
		for (CircuitWindow* c : circuitWindows()) if (GTK_WIDGET(c->window()) == t) return true;
		return false;
	}

	static GtkWidget* menuIn(GtkWidget* t) {
		GtkWidget* child = GTK_IS_BIN(t) ? gtk_bin_get_child(GTK_BIN(t)) : nullptr;
		return child && GTK_IS_MENU(child) ? child : nullptr;
	}

	static void collect(GtkWidget* wdg, GType type, std::vector<GtkWidget*>& out) {
		if (G_TYPE_CHECK_INSTANCE_TYPE(wdg, type) && gtk_widget_is_sensitive(wdg) && gtk_widget_get_visible(wdg)) out.push_back(wdg);
		if (GTK_IS_CONTAINER(wdg)) {
			GList* kids = gtk_container_get_children(GTK_CONTAINER(wdg));
			for (GList* k = kids; k; k = k->next) collect(GTK_WIDGET(k->data), type, out);
			g_list_free(kids);
		}
	}

	// A menu: one of its items run (a safe one), or closed.
	void pokeMenu(GtkWidget* menu) {
		menusSeen++;
		std::vector<GtkWidget*> items;
		GList* kids = gtk_container_get_children(GTK_CONTAINER(menu));
		for (GList* k = kids; k; k = k->next) {
			GtkWidget* it = GTK_WIDGET(k->data);
			if (!GTK_IS_MENU_ITEM(it) || GTK_IS_SEPARATOR_MENU_ITEM(it) || !gtk_widget_is_sensitive(it) || !gtk_widget_get_visible(it)) continue;
			if (gtk_menu_item_get_submenu(GTK_MENU_ITEM(it))) continue;
			const char* action = GTK_IS_ACTIONABLE(it) ? gtk_actionable_get_action_name(GTK_ACTIONABLE(it)) : nullptr;
			if (safeMenuAction(action)) items.push_back(it);
		}
		g_list_free(kids);
		if (!items.empty() && rng.chance(0.6)) {
			GtkWidget* it = items[(size_t)rng.below((int)items.size())];
			menuItemsRun++;
			gtk_menu_shell_activate_item(GTK_MENU_SHELL(menu), it, TRUE);
		} else {
			gtk_menu_shell_cancel(GTK_MENU_SHELL(menu));
		}
	}

	// Settings: a few of its switches, boxes and lists changed (not Shortcuts
	// or Sync: those take the keys or reach the internet).
	void pokeSettings(GtkWidget* t) {
		std::vector<GtkWidget*> found;
		collect(t, GTK_TYPE_SWITCH, found);
		collect(t, GTK_TYPE_TOGGLE_BUTTON, found);
		collect(t, GTK_TYPE_COMBO_BOX, found);
		if (found.empty()) return;
		GtkWidget* c = found[(size_t)rng.below((int)found.size())];
		if (GTK_IS_SWITCH(c)) gtk_switch_set_active(GTK_SWITCH(c), !gtk_switch_get_active(GTK_SWITCH(c)));
		else if (GTK_IS_TOGGLE_BUTTON(c)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(c), !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(c)));
		else if (GTK_IS_COMBO_BOX(c)) {
			GtkTreeModel* m = gtk_combo_box_get_model(GTK_COMBO_BOX(c));
			const int n = m ? gtk_tree_model_iter_n_children(m, nullptr) : 0;
			if (n > 0) gtk_combo_box_set_active(GTK_COMBO_BOX(c), rng.below(n));
		}
	}

	// Anything else over the window (a question, a drawn sheet, a dialog):
	// clicks and keys inside it.
	void pokeWindow(GtkWidget* t) {
		GtkWidget* target = t;
		// The deepest window-owning drawing area (a sheet's), else the window.
		std::vector<GtkWidget*> areas;
		collect(t, GTK_TYPE_DRAWING_AREA, areas);
		if (!areas.empty()) target = areas[(size_t)rng.below((int)areas.size())];
		const int ww = gtk_widget_get_allocated_width(target), hh = gtk_widget_get_allocated_height(target);
		if (rng.chance(0.65) && ww > 4 && hh > 4 && !GTK_IS_FILE_CHOOSER(t)) {
			const double x = 2 + rng.unit() * (ww - 4), y = 2 + rng.unit() * (hh - 4);
			motion(target, x, y, 0);
			button(target, GDK_BUTTON_PRESS, x, y, 1, 0);
			button(target, GDK_BUTTON_RELEASE, x, y, 1, GDK_BUTTON1_MASK);
		} else {
			static const guint keys[] = { GDK_KEY_Tab, GDK_KEY_Down, GDK_KEY_Up, GDK_KEY_Left, GDK_KEY_Right, GDK_KEY_a, GDK_KEY_1, GDK_KEY_space };
			tap(t, keys[rng.below(G_N_ELEMENTS(keys))]);
		}
	}

	// Every window over the circuit window, looked at: poked a few times, then
	// asked to close (Escape, then its close button); one that won't is a FAIL.
	void watch() {
		const double now = nowMs();
		GList* all = gtk_window_list_toplevels();
		std::vector<GtkWidget*> tops;
		// Held while looked at: closing one can take others with it (its popups).
		for (GList* l = all; l; l = l->next) tops.push_back(GTK_WIDGET(g_object_ref(l->data)));
		g_list_free(all);
		struct Unref {
			std::vector<GtkWidget*>& v;
			~Unref() { for (GtkWidget* t : v) g_object_unref(t); }
		} unref{ tops };
		std::set<GtkWidget*> present;
		for (GtkWidget* t : tops) {
			if (gtk_widget_in_destruction(t) || !gtk_widget_get_visible(t) || isCircuitWindow(t)) continue;
			GtkWidget* menu = menuIn(t);
			const bool popup = gtk_window_get_window_type(GTK_WINDOW(t)) == GTK_WINDOW_POPUP;
			if (popup && !menu) continue;   // a tooltip
			present.insert(t);
			auto it = seen.find(t);
			if (it == seen.end()) {
				const bool settingsWin = strcmp(gtk_widget_get_name(t), "settings") == 0;
				Seen s{ now, 0, menu ? 0 : rng.below(settingsWin ? 6 : 4), 0, settingsWin };
				it = seen.emplace(t, s).first;
				if (!menu) modalsSeen++;
				continue;
			}
			Seen& s = it->second;
			if (now - s.since < 140) continue;
			s.since = now;
			if (menu) { pokeMenu(menu); continue; }
			const bool settingsWin = strcmp(gtk_widget_get_name(t), "settings") == 0;
			if (s.pokes < s.wanted) {
				s.pokes++;
				if (settingsWin) {
					if (rng.chance(0.3)) tap(t, GDK_KEY_Tab, GDK_CONTROL_MASK);   // the next page (General -> Toolbar only)
					pokeSettings(t);
				} else {
					pokeWindow(t);
				}
				continue;
			}
			// Close it as a person would: Escape (or Return on a question), then its close button.
			s.closes++;
			if (s.closes <= 3) {
				const bool plainWindow = !GTK_IS_DIALOG(t) && !settingsWin;
				tap(t, plainWindow && rng.chance(0.3) ? GDK_KEY_Return : GDK_KEY_Escape);
			} else if (s.closes <= 6) {
				gtk_window_close(GTK_WINDOW(t));
			} else {
				stuckWindows++;
				const char* title = gtk_window_get_title(GTK_WINDOW(t));
				fail(format("a window (\"%s\") didn't close on Escape or its close button", title ? title : gtk_widget_get_name(t)));
				gtk_widget_destroy(t);
			}
		}
		for (auto it = seen.begin(); it != seen.end();) {
			if (present.count(it->first)) { ++it; continue; }
			if (it->second.settings) settingsClosed = true;
			it = seen.erase(it);
		}
	}

	static gboolean watchCb(gpointer) {
		if (!gActive || current == nullptr) return G_SOURCE_REMOVE;
		// The next look first: a window opened from here (a question over a
		// question) runs its own loop, and still gets looked at.
		g_timeout_add(70, watchCb, nullptr);
		guarded("the session's watchdog", [] { current->watch(); });
		return G_SOURCE_REMOVE;
	}

	// The settings file has what's in memory (a setting changed in Settings was saved).
	void checkPrefs() {
		gchar* file = g_build_filename(g_get_user_config_dir(), "CedarLogic", "native.ini", nullptr);
		const std::string path = file;
		g_free(file);
		auto load = [](const std::string& p) {
			std::map<std::string, std::string> kv;
			GKeyFile* k = g_key_file_new();
			if (g_key_file_load_from_file(k, p.c_str(), G_KEY_FILE_NONE, nullptr)) {
				gsize n = 0;
				gchar** keys = g_key_file_get_keys(k, "CedarLogic", &n, nullptr);
				for (gsize i = 0; keys && i < n; i++) {
					gchar* v = g_key_file_get_value(k, "CedarLogic", keys[i], nullptr);
					kv[keys[i]] = v ? v : "";
					g_free(v);
				}
				g_strfreev(keys);
			}
			g_key_file_free(k);
			return kv;
		};
		const auto onDisk = load(path);
		prefs().save();
		const auto inMemory = load(path);
		prefsChecks++;
		static const std::set<std::string> geometry = { "windowWidth", "windowHeight", "windowMaximized", "recent", "lastCircuit", "lastFolder" };
		for (const auto& kv : inMemory) {
			if (geometry.count(kv.first)) continue;
			auto d = onDisk.find(kv.first);
			if (d == onDisk.end() || d->second != kv.second) {
				prefsUnsaved++;
				if (lastPrefsIssue.empty())
					lastPrefsIssue = kv.first + " is " + kv.second + " in the app, " + (d == onDisk.end() ? std::string("missing") : d->second) + " in the file";
				break;
			}
		}
	}

	// ---- The actions -----------------------------------------------------------

	void placePart() {
		Canvas* c = canvas();
		if (!w->canEdit()) { w->runAction(w->simView() ? "win.sim-view" : "win.lock"); return; }
		const char* part = kParts[rng.below(rng.chance(0.85) ? 8 : (int)G_N_ELEMENTS(kParts))];
		// As the side panel's tile does: on the pointer at the next move, dropped with a click.
		w->addGateOnNextMove(part);
		const double vx = 30 + rng.unit() * (c->width() - 60), vy = 30 + rng.unit() * (c->height() - 60);
		motion(c->widget(), vx - 5, vy - 3, 0);
		pump(10);
		motion(c->widget(), vx, vy, 0);
		pump(10);
		if (rng.chance(0.08)) { tap(top(), GDK_KEY_Escape); return; }   // changed their mind
		click(c, vx, vy);
	}

	void wire(bool toWire) {
		Canvas* c = canvas();
		std::vector<Spot> ps = parts();
		if (ps.size() < 2) { placePart(); return; }
		const Spot& a = ps[(size_t)rng.below((int)ps.size())];
		const Spot& b = ps[(size_t)rng.below((int)ps.size())];
		double ax, ay, bx, by;
		if (!pinOf(a, ax, ay)) return;
		if (toWire) { if (!wirePoint(b, bx, by) && !pinOf(b, bx, by)) return; }
		else if (!pinOf(b, bx, by)) return;
		double vx0, vy0, vx1, vy1;
		toView(c, ax, ay, vx0, vy0);
		toView(c, bx, by, vx1, vy1);
		if (!inView(c, vx0, vy0) || !inView(c, vx1, vy1)) { w->runAction("win.zoom-fit"); pump(200); return; }
		if (rng.chance(0.2)) {
			// Click the pin, then click the target.
			click(c, vx0, vy0);
			pump(10);
			motion(c->widget(), (vx0 + vx1) / 2, (vy0 + vy1) / 2, 0);
			pump(10);
			click(c, vx1, vy1);
		} else {
			drag(c->widget(), vx0, vy0, vx1, vy1, 3 + rng.below(6), 0);
		}
	}

	void dragSomething() {
		Canvas* c = canvas();
		std::vector<Spot> ps = parts();
		double x0, y0;
		if (!ps.empty() && rng.chance(0.75)) {
			const Spot& s = ps[(size_t)rng.below((int)ps.size())];
			toView(c, s.x + (rng.unit() - 0.5), s.y + (rng.unit() - 0.5), x0, y0);
			if (!inView(c, x0, y0)) { w->runAction("win.zoom-fit"); pump(200); return; }
		} else {
			x0 = 10 + rng.unit() * (c->width() - 20);
			y0 = 10 + rng.unit() * (c->height() - 20);
		}
		const double x1 = std::min(std::max(x0 + (rng.unit() - 0.5) * 300, 4.0), c->width() - 4);
		const double y1 = std::min(std::max(y0 + (rng.unit() - 0.5) * 240, 4.0), c->height() - 4);
		const guint state = rng.chance(0.15) ? GDK_SHIFT_MASK : 0;
		const bool escape = rng.chance(0.12);
		drag(c->widget(), x0, y0, x1, y1, 3 + rng.below(12), state, 1, !escape);
		if (escape) {
			// Escape mid-drag, then the button comes up.
			tap(top(), GDK_KEY_Escape);
			pump(10);
			button(c->widget(), GDK_BUTTON_RELEASE, x1, y1, 1, GDK_BUTTON1_MASK);
		} else if (rng.chance(0.15)) {
			// C while moving: connect to what's near.
			tap(top(), GDK_KEY_c);
		}
	}

	void boxThenDrag() {
		Canvas* c = canvas();
		const double x0 = 10 + rng.unit() * (c->width() / 2), y0 = 10 + rng.unit() * (c->height() / 2);
		drag(c->widget(), x0, y0, x0 + 80 + rng.unit() * (c->width() / 2), y0 + 60 + rng.unit() * (c->height() / 2), 4, 0);
		pump(20);
		dragSomething();
	}

	void tabStrip() {
		const int pane = w->splitOpen() ? rng.below(2) : 0;
		TabStrip* s = w->strips[pane];
		GtkWidget* a = s ? s->widget() : nullptr;
		if (a == nullptr || !gtk_widget_get_mapped(a)) return;
		const double sw = gtk_widget_get_allocated_width(a);
		const double x = 6 + rng.unit() * std::min(sw - 12, 160.0 * (w->panePages(pane).size() + 1));
		const double y = TabStrip::stripHeight() / 2;
		const int roll = rng.below(10);
		if (roll < 4) {
			motion(a, x, y, 0);
			button(a, GDK_BUTTON_PRESS, x, y, 1, 0);
			button(a, GDK_BUTTON_RELEASE, x, y, 1, GDK_BUTTON1_MASK);
		} else if (roll < 6) {
			// Along the strip (reorder), or down onto the canvas (split, or move to the other side).
			const bool down = rng.chance(0.4);
			drag(a, x, y, down ? x + (rng.unit() - 0.3) * 500 : x + (rng.unit() - 0.5) * 400, down ? 200 + rng.unit() * 300 : y, 6, 0);
		} else if (roll < 8) {
			motion(a, x, y, 0);
			button(a, GDK_BUTTON_PRESS, x, y, 2, 0);   // middle-click closes
			button(a, GDK_BUTTON_RELEASE, x, y, 2, GDK_BUTTON2_MASK);
		} else {
			motion(a, x, y, 0);
			button(a, GDK_BUTTON_PRESS, x, y, 3, 0);   // its menu
			button(a, GDK_BUTTON_RELEASE, x, y, 3, GDK_BUTTON3_MASK);
		}
	}

	void drawInk() {
		Canvas* c = canvas();
		if (!w->drawing()) { tap(top(), GDK_KEY_p, GDK_CONTROL_MASK | GDK_MOD1_MASK); pump(10); }
		if (!w->drawing()) return;
		static const guint tools[] = { GDK_KEY_p, GDK_KEY_h, GDK_KEY_e, GDK_KEY_1, GDK_KEY_2, GDK_KEY_3 };
		if (rng.chance(0.4)) tap(top(), tools[rng.below(G_N_ELEMENTS(tools))]);
		const double x0 = 20 + rng.unit() * (c->width() - 40), y0 = 20 + rng.unit() * (c->height() - 40);
		drag(c->widget(), x0, y0, x0 + (rng.unit() - 0.5) * 200, y0 + (rng.unit() - 0.5) * 160, 4 + rng.below(10), 0);
		if (rng.chance(0.35)) tap(top(), GDK_KEY_Escape);   // done drawing
	}

	void typeNotes() {
		if (!w->notesShown()) { tap(top(), GDK_KEY_n, GDK_CONTROL_MASK | GDK_MOD1_MASK); pump(20); }
		NotesPane* n = w->notesPane();
		if (n == nullptr || !w->notesShown()) return;
		std::vector<GtkWidget*> views;
		collect(n->widget(), GTK_TYPE_TEXT_VIEW, views);
		if (views.empty()) return;
		gtk_widget_grab_focus(views[0]);
		static const char* words[] = { "Lab 6 ", "check the clock ", "Q goes high ", "\n", "AND then OR " };
		for (const char* p = words[rng.below(G_N_ELEMENTS(words))]; *p; p++) tap(top(), *p == '\n' ? GDK_KEY_Return : gdk_unicode_to_keyval((guint32)(unsigned char)*p));
		if (rng.chance(0.3)) tap(top(), GDK_KEY_z, GDK_CONTROL_MASK);   // the notes' own undo
		if (rng.chance(0.5)) { if (Canvas* c = canvas()) gtk_widget_grab_focus(c->widget()); }
		if (rng.chance(0.3)) tap(top(), GDK_KEY_n, GDK_CONTROL_MASK | GDK_MOD1_MASK);
	}

	void findBar() {
		tap(top(), GDK_KEY_f, GDK_CONTROL_MASK);
		pump(20);
		static const char* queries[] = { "and", "led", "to", "clock", "xyz", "a" };
		for (const char* p = queries[rng.below(G_N_ELEMENTS(queries))]; *p; p++) tap(top(), gdk_unicode_to_keyval((guint32)(unsigned char)*p));
		pump(10);
		for (int i = rng.below(4); i > 0; i--) tap(top(), GDK_KEY_Return, rng.chance(0.3) ? GDK_SHIFT_MASK : 0);
		if (rng.chance(0.3)) tap(top(), GDK_KEY_F3);
		if (rng.chance(0.7)) tap(top(), GDK_KEY_Escape);
		if (Canvas* c = canvas()) if (rng.chance(0.5)) gtk_widget_grab_focus(c->widget());
	}

	void selectOne() {
		std::vector<Spot> ps = parts();
		if (ps.empty()) return;
		const Spot& s = ps[(size_t)rng.below((int)ps.size())];
		Canvas* c = canvas();
		double vx, vy;
		toView(c, s.x, s.y, vx, vy);
		if (!inView(c, vx, vy)) { w->runAction("win.zoom-fit"); pump(200); toView(c, s.x, s.y, vx, vy); }
		if (inView(c, vx, vy)) click(c, vx, vy);
	}

	void gateSettings() {
		selectOne();
		Canvas* c = canvas();
		if (rng.chance(0.5)) {
			std::vector<Spot> ps = parts();
			if (ps.empty()) return;
			const Spot& s = ps[(size_t)rng.below((int)ps.size())];
			double vx, vy;
			toView(c, s.x, s.y, vx, vy);
			if (!inView(c, vx, vy)) return;
			// A double-click, as GTK sends one.
			motion(c->widget(), vx, vy, 0);
			button(c->widget(), GDK_BUTTON_PRESS, vx, vy, 1, 0);
			button(c->widget(), GDK_BUTTON_RELEASE, vx, vy, 1, GDK_BUTTON1_MASK);
			button(c->widget(), GDK_BUTTON_PRESS, vx, vy, 1, 0);
			button(c->widget(), GDK_2BUTTON_PRESS, vx, vy, 1, 0);
			button(c->widget(), GDK_BUTTON_RELEASE, vx, vy, 1, GDK_BUTTON1_MASK);
		} else {
			w->runAction("win.gate-settings");
		}
	}

	void rightClick() {
		Canvas* c = canvas();
		std::vector<Spot> ps = parts();
		double vx = 20 + rng.unit() * (c->width() - 40), vy = 20 + rng.unit() * (c->height() - 40);
		if (!ps.empty() && rng.chance(0.6)) {
			const Spot& s = ps[(size_t)rng.below((int)ps.size())];
			double px = s.x, py = s.y;
			if (rng.chance(0.4)) pinOf(s, px, py);
			toView(c, px, py, vx, vy);
			if (!inView(c, vx, vy)) return;
		}
		motion(c->widget(), vx, vy, 0);
		button(c->widget(), GDK_BUTTON_PRESS, vx, vy, 3, 0);
		button(c->widget(), GDK_BUTTON_RELEASE, vx, vy, 3, GDK_BUTTON3_MASK);
	}

	void zoomAround() {
		Canvas* c = canvas();
		const double vx = rng.unit() * c->width(), vy = rng.unit() * c->height();
		switch (rng.below(6)) {
		case 0: for (int i = rng.below(4) + 1; i > 0; i--) scroll(c->widget(), vx, vy, rng.chance(0.5) ? -1 : 1, 0); break;
		case 1: scroll(c->widget(), vx, vy, rng.chance(0.5) ? -1 : 1, GDK_CONTROL_MASK); break;
		case 2: drag(c->widget(), vx, vy, vx + (rng.unit() - 0.5) * 300, vy + (rng.unit() - 0.5) * 300, 5, 0, 2); break;   // middle-drag pans
		case 3: tap(top(), GDK_KEY_0, GDK_CONTROL_MASK); break;
		case 4: tap(top(), rng.chance(0.5) ? GDK_KEY_equal : GDK_KEY_minus, GDK_CONTROL_MASK); break;
		default:   // Space: a tap fits the page, held while dragging pans
			key(top(), GDK_KEY_space, 0, true);
			if (rng.chance(0.5)) drag(c->widget(), vx, vy, vx + 80, vy + 40, 3, 0);
			key(top(), GDK_KEY_space, 0, false);
			break;
		}
	}

	// The engine's history, past its 500 steps: every step is still saved, and
	// undoing everything then redoing it brings the circuit back.
	void longHistory(int at) {
		settle();
		if (!w->canEdit()) { if (w->simView()) w->toggleSimView(); if (w->locked()) w->toggleLock(); }
		if (w->drawing()) w->setDrawing(false);
		if (Canvas* c = canvas()) gtk_widget_grab_focus(c->widget());
		if (cl_document_gate_count(doc(), w->currentPage()) == 0) placePart();
		w->runAction("win.unlock-all");
		tap(top(), GDK_KEY_a, GDK_CONTROL_MASK);
		pump(10);
		const int nudges = 520;
		for (int i = 0; i < nudges; i++) {
			tap(top(), (i & 1) ? GDK_KEY_Left : GDK_KEY_Right);
			if (i % 20 == 19) { pump(3); checkPages(at); }
		}
		pump(20);
		const int steps = cl_edit_undo_count(doc());
		check(steps == 500, format("the history holds the engine's 500 steps after %d nudges (%d)", nudges, steps));
		check(w->isDirty() || shape(readFile(w->filePath())) == text(), "nudges past the 500th step leave the window unsaved");
		const std::string moved = text();
		int undone = 0;
		while (cl_edit_can_undo(doc()) && undone < 600) {
			tap(top(), GDK_KEY_z, GDK_CONTROL_MASK);
			undone++;
			if (undone % 25 == 0) pump(2);
		}
		tap(top(), GDK_KEY_z, GDK_CONTROL_MASK);   // one more: nothing left (a bell)
		int redone = 0;
		while (cl_edit_can_redo(doc()) && redone < 600) {
			tap(top(), GDK_KEY_y, GDK_CONTROL_MASK);
			redone++;
			if (redone % 25 == 0) pump(2);
		}
		pump(20);
		check(undone == redone && text() == moved,
		      format("undoing all %d steps and redoing them brings the circuit back (%d redone)", undone, redone) +
		          (text() == moved ? std::string() : ": " + firstDifference(moved, text())));
		pause(at);
		check(!w->isDirty() && shape(readFile(w->filePath())) == text(), "after the long history the file on disk is the circuit");
	}

	// The circuit reloaded from its file in place (Version History's
	// restore, another device's change): the window carries on.
	void reload() {
		settle();
		if (w->filePath().empty() || w->isDirty()) { tap(top(), GDK_KEY_s, GDK_CONTROL_MASK); pump(40); }
		if (w->filePath().empty()) return;
		const unsigned stamp = w->editStamp();
		if (rng.chance(0.5)) w->reloadForSync("Reloaded by the session check.");
		else w->reloadFromDisk("");
		// The pictures kept of the circuit (the minimap's) are keyed on the
		// edit stamp: a circuit replaced in place has to move it.
		if (w->editStamp() == stamp) {
			staleStamps++;
			if (staleStamps == 1) fail(format("action %d: the circuit was reloaded in place and the window's edit stamp didn't move (the minimap keeps the old picture)", (int)gStep));
		}
	}

	void act(int a) {
		static const char* names[] = {
			"place a part", "wire pin to pin", "wire pin to wire", "drag", "box then drag", "click", "rotate (R)",
			"delete", "cut", "copy", "paste", "duplicate", "nudge", "undo", "redo", "undo and redo", "new tab",
			"close tab", "next tab", "reopen tab", "tab strip", "split view", "switch side", "Simulation View",
			"run or pause", "step", "Step Clock", "scope", "truth table", "find", "gate settings", "Settings",
			"draw", "notes", "Straighten", "Tidy Up", "lock", "lock in place", "save", "reload", "zoom and pan",
			"right-click", "select all", "the ••• menu", "connect nearby", "dark mode", "focus mode", "add a gate (A)",
			"canvas drawing path", "duplicate the circuit", "Ctrl+Tab held",
		};
		static const int weights[] = {
			12, 7, 4, 10, 3, 4, 3,
			3, 1, 2, 2, 2, 3, 5, 3, 3, 2,
			2, 2, 1, 4, 2, 2, 2,
			2, 2, 1, 2, 1, 2, 2, 1,
			2, 1, 2, 2, 1, 1, 1, 1, 4,
			3, 2, 1, 1, 1, 1, 1,
			1, 1, 2,
		};
		static_assert(G_N_ELEMENTS(names) == G_N_ELEMENTS(weights), "a weight for each action");
		int total = 0;
		for (int x : weights) total += x;
		int pick = rng.below(total), which = 0;
		while (pick >= weights[which]) pick -= weights[which++];
		gWhat = names[which];
		counts[which]++;
		Canvas* c = canvas();
		if (c == nullptr) return;
		GtkWidget* t = top();
		switch (which) {
		case 0: placePart(); break;
		case 1: wire(false); break;
		case 2: wire(true); break;
		case 3: dragSomething(); break;
		case 4: boxThenDrag(); break;
		case 5: click(c, rng.unit() * c->width(), rng.unit() * c->height()); break;
		case 6: if (rng.chance(0.5)) selectOne(); tap(t, GDK_KEY_r); break;
		case 7: if (rng.chance(0.6)) selectOne(); tap(t, rng.chance(0.5) ? GDK_KEY_Delete : GDK_KEY_BackSpace); break;
		case 8: if (rng.chance(0.6)) selectOne(); tap(t, GDK_KEY_x, rng.chance(0.7) ? GDK_CONTROL_MASK : 0); break;
		case 9: if (rng.chance(0.4)) selectOne(); tap(t, GDK_KEY_c, GDK_CONTROL_MASK); break;
		case 10:
			tap(t, GDK_KEY_v, rng.chance(0.7) ? GDK_CONTROL_MASK : 0);
			pump(60);   // the clipboard answers
			if (w->isFloating()) { motion(c->widget(), c->width() / 3 + rng.unit() * 200, c->height() / 3 + rng.unit() * 200, 0); pump(10); }
			if (w->isFloating() && rng.chance(0.85)) click(c, c->width() / 3 + rng.unit() * 200, c->height() / 3 + rng.unit() * 200);
			break;
		case 11:
			if (rng.chance(0.5)) selectOne();
			tap(t, GDK_KEY_d, rng.chance(0.5) ? GDK_CONTROL_MASK : 0);
			pump(10);
			if (w->isFloating()) click(c, c->width() / 2 + (rng.unit() - 0.5) * 300, c->height() / 2 + (rng.unit() - 0.5) * 200);
			break;
		case 12: {
			if (rng.chance(0.5)) selectOne();
			static const guint arrows[] = { GDK_KEY_Left, GDK_KEY_Right, GDK_KEY_Up, GDK_KEY_Down };
			for (int i = 1 + rng.below(5); i > 0; i--) tap(t, arrows[rng.below(4)], rng.chance(0.2) ? GDK_SHIFT_MASK : 0);
			break;
		}
		case 13: for (int i = 1 + rng.below(4); i > 0; i--) tap(t, GDK_KEY_z, GDK_CONTROL_MASK); break;
		case 14: for (int i = 1 + rng.below(3); i > 0; i--) tap(t, GDK_KEY_Z, GDK_CONTROL_MASK | GDK_SHIFT_MASK); break;
		case 15: undoRedo((int)gStep, 1 + rng.below(8)); break;
		case 16: tap(t, GDK_KEY_t, GDK_CONTROL_MASK); break;
		case 17: tap(t, GDK_KEY_w, GDK_CONTROL_MASK); break;   // a tab with parts asks first
		case 18: tap(t, rng.chance(0.5) ? GDK_KEY_Page_Down : GDK_KEY_Page_Up, GDK_CONTROL_MASK); break;
		case 19: tap(t, GDK_KEY_T, GDK_CONTROL_MASK | GDK_SHIFT_MASK); break;
		case 20: tabStrip(); break;
		case 21: tap(t, GDK_KEY_s, GDK_CONTROL_MASK | GDK_MOD1_MASK); break;
		case 22: tap(t, GDK_KEY_F6); break;
		case 23:
			tap(t, GDK_KEY_r, GDK_CONTROL_MASK);
			pump(30);
			if (w->simView()) {
				// Switches still work; Space pauses; the speed slider.
				std::vector<Spot> ps = parts();
				for (int i = 0; i < 3 && !ps.empty(); i++) {
					const Spot& s = ps[(size_t)rng.below((int)ps.size())];
					double vx, vy;
					toView(c, s.x, s.y, vx, vy);
					if (inView(c, vx, vy)) click(c, vx, vy);
					pump(20);
				}
				if (rng.chance(0.3)) { key(t, GDK_KEY_space, 0, true); key(t, GDK_KEY_space, 0, false); }
				if (rng.chance(0.7)) tap(t, GDK_KEY_Escape);
			}
			break;
		case 24: w->runAction("win.running"); break;
		case 25: tap(t, GDK_KEY_R, GDK_CONTROL_MASK | GDK_SHIFT_MASK); break;
		case 26: tap(t, GDK_KEY_k); break;
		case 27: tap(t, GDK_KEY_g, GDK_CONTROL_MASK); break;
		case 28: tap(t, rng.chance(0.7) ? GDK_KEY_t : GDK_KEY_T, rng.chance(0.7) ? 0 : GDK_SHIFT_MASK); break;   // a window of its own, closed by the watchdog
		case 29: findBar(); break;
		case 30: gateSettings(); break;
		case 31: {
			tap(t, GDK_KEY_comma, GDK_CONTROL_MASK);
			pump(400 + rng.below(800));   // the watchdog changes things in it and closes it
			break;
		}
		case 32: drawInk(); break;
		case 33: typeNotes(); break;
		case 34: if (rng.chance(0.5)) selectOne(); tap(t, GDK_KEY_s); break;
		case 35: {
			tap(t, GDK_KEY_S, GDK_SHIFT_MASK);
			pump(60);
			static const guint after[] = { GDK_KEY_Return, GDK_KEY_Escape, GDK_KEY_Tab, GDK_KEY_r };
			if (rng.chance(0.85)) tap(t, after[rng.below(4)]);
			break;
		}
		case 36: w->runAction("win.lock"); break;
		case 37: if (rng.chance(0.6)) selectOne(); tap(t, GDK_KEY_l, GDK_CONTROL_MASK); break;
		case 38: tap(t, GDK_KEY_s, GDK_CONTROL_MASK); break;
		case 39: reload(); break;
		case 40: zoomAround(); break;
		case 41: rightClick(); break;
		case 42: tap(t, GDK_KEY_a, GDK_CONTROL_MASK); break;
		case 43: tap(t, GDK_KEY_F10, rng.chance(0.3) ? GDK_SHIFT_MASK : 0); break;   // every menu, or the canvas's
		case 44: w->runAction("win.connect-nearby"); break;
		case 45: tap(t, GDK_KEY_D, GDK_CONTROL_MASK | GDK_SHIFT_MASK); break;
		case 46: tap(t, GDK_KEY_period, GDK_CONTROL_MASK); break;
		case 47: tap(t, GDK_KEY_a); break;   // Add a Gate: a picker of its own
		case 48: {
			// The drag's two ways of drawing (SMOOTHNESS.md): through the frame
			// or straight onto the window, and the static layer or the whole page.
			for (Canvas* k : w->canvases) { k->pathOverride = rng.below(3); k->drawAll = rng.chance(0.3); k->dropBuffers(); k->redraw(); }
			break;
		}
		case 50: {
			// Ctrl held, Tab a few times (the switcher comes up), maybe Ctrl+W
			// or Ctrl+T on the way, then Ctrl let go: the card picked is the
			// tab shown.
			key(t, GDK_KEY_Control_L, 0, true);
			for (int i = 1 + rng.below(3); i > 0; i--) {
				key(t, GDK_KEY_Tab, GDK_CONTROL_MASK, true);
				key(t, GDK_KEY_Tab, GDK_CONTROL_MASK, false);
				pump(40);
			}
			pump(220);
			uint64_t want = 0;
			TabSwitcher* sw = w->switcher;
			if (sw && sw->isActive && sw->selected >= 0 && sw->selected < (int)sw->keys.size()) want = sw->keys[(size_t)sw->selected];
			if (rng.chance(0.3)) tap(t, GDK_KEY_w, GDK_CONTROL_MASK);   // with Ctrl still down
			else if (rng.chance(0.2)) tap(t, GDK_KEY_t, GDK_CONTROL_MASK);
			pump(20);
			if (!alive()) break;   // Ctrl+W on the last tab closes the window
			key(t, GDK_KEY_Control_L, GDK_CONTROL_MASK, false);
			pump(20);
			if (want && cl_document_page_index(doc(), want) >= 0 && canvas()) {
				switches++;
				if (canvas()->pageKey() != want) {
					switchesWrong++;
					if (lastSwitchIssue.empty())
						lastSwitchIssue = format("action %d: the card picked was page %d, the tab shown is page %d", (int)gStep,
						                         cl_document_page_index(doc(), want), w->currentPage());
				}
			}
			break;
		}
		case 49: {
			// A copy in a window of its own, closed again: windows come and go mid-session.
			const int before = CircuitWindow::liveCount;
			w->duplicateCircuit();
			pump(200);
			for (CircuitWindow* o : std::vector<CircuitWindow*>(circuitWindows()))
				if (o != w) gtk_window_close(o->window());
			pump(100);
			if (CircuitWindow::liveCount != before) fail(format("a duplicate's window, closed, left %d windows alive (%d before)", CircuitWindow::liveCount, before));
			break;
		}
		}
	}

	// ---- The session -------------------------------------------------------------

	int run(int actions, unsigned long long seed) {
		setvbuf(stdout, nullptr, _IOLBF, 0);
		rng.s = seed;
		std::vector<std::string> names(std::begin(kParts), std::end(kParts));
		loadPins(names);
		check(!gPins.empty(), format("the parts' pins read from the gate library (%zu parts)", gPins.size()));
		current = this;
		gActive = true;
		g_timeout_add(70, watchCb, nullptr);
		pump(500);
		w = circuitWindows().front();
		gtk_window_present(w->window());
		// The settings file as it starts: what Settings changes later is compared with it.
		prefs().save();
		pump(300);
		const int windowsAtStart = CircuitWindow::liveCount;
		long rss[3] = { residentKB(), 0, 0 };
		const double started = nowMs();
		const int pauseEvery = 50;
		for (int a = 0; a < actions; a++) {
			gStep = a;
			// A window could have been replaced (a reload makes canvases anew), never the window itself.
			if (std::find(circuitWindows().begin(), circuitWindows().end(), w) == circuitWindows().end()) {
				check(false, format("the session's window is still open at action %d", a));
				break;
			}
			const std::string before = text();
			const std::string pathBefore = w->filePath();
			guarded("a session action", [&] { act(a); });
			pump(15 + rng.below(30));
			if (!alive()) {
				// Ctrl+W on its last tab closes the window (saving it): opened
				// again from Your Circuits, as a person would, and on from there.
				windowCloses++;
				if (CircuitWindow::liveCount != windowsAtStart - 1)
					fail(format("action %d: the window closed with Ctrl+W, and %d circuit windows are still alive", a, CircuitWindow::liveCount));
				if (!pathBefore.empty() && g_file_test(pathBefore.c_str(), G_FILE_TEST_EXISTS)) openCircuit(app, pathBefore, nullptr);
				if (circuitWindows().empty()) newCircuitWindow(app);
				w = circuitWindows().back();
				pump(300);
				checkPages(a);
				continue;
			}
			checkEdited(a, before);
			checkPages(a);
			if (settingsClosed) { settingsClosed = false; checkPrefs(); }
			if (a % pauseEvery == pauseEvery - 1) { gWhat = "pause for autosave"; pause(a); }
			if (a == actions / 2) { gWhat = "the long history"; longHistory(a); }
			if (a == actions / 4) rss[1] = residentKB();
			if (a % 50 == 0) printf("session: action %d of %d, %.0f s, %ld MB\n", a, actions, (nowMs() - started) / 1000, residentKB() / 1024);
		}
		gStep = actions;
		gWhat = "the end";
		settle();
		// Windows over it go first, as they would before quitting.
		for (int i = 0; i < 40 && !seen.empty(); i++) pump(100);
		pause(actions);
		rss[2] = residentKB();
		printf("session: %d actions in %.0f s; %d undo steps at most; %d pauses; %d windows over it, %d menus (%d items run); %d pages now; the window closed and reopened %d times\n",
		       actions, (nowMs() - started) / 1000, maxUndo, pauses, modalsSeen, menusSeen, menuItemsRun, cl_document_page_count(doc()), windowCloses);

		check(editsChecked > 0 && editsLost == 0,
		      format("every edit leaves the window unsaved until it's saved (%d of %d", editsChecked - editsLost, editsChecked) +
		          (firstLost >= 0 ? format(", first lost at action %d)", firstLost) : std::string(")")));
		check(pauses > 0 && pausesUnsaved == 0, format("autosave saved the circuit at every pause (%d of %d)", pauses - pausesUnsaved, pauses));
		check(pausesStale == 0, format("the file on disk is the circuit as it is, at every pause (%d stale", pausesStale) +
		                            (firstStale >= 0 ? format(", first at action %d: %s)", firstStale, lastStaleIssue.c_str()) : std::string(")")));
		check(reopenMismatch == 0, format("the saved file opens again to the same circuit (%d differ", reopenMismatch) +
		                               (firstReopen >= 0 ? format(", first at action %d: %s)", firstReopen, lastReopenIssue.c_str()) : std::string(")")));
		check(roundTripBad == 0, format("undo then redo brings the circuit back (%d of %d", roundTrips - roundTripBad, roundTrips) +
		                             (firstRoundTrip >= 0 ? format("; first wrong at action %d: %s)", firstRoundTrip, lastRoundTripIssue.c_str()) : std::string(")")));
		check(historyBad == 0, format("the undo history stays within 500 steps (%d at most%s)", maxUndo,
		                              firstHistory >= 0 ? format("; wrong at action %d", firstHistory).c_str() : ""));
		check(pagesBad == 0, format("every tab is a page that exists, on its side (%d wrong", pagesBad) +
		                         (firstPages >= 0 ? format(", first at action %d: %s)", firstPages, lastPageIssue.c_str()) : std::string(")")));
		check(switchesWrong == 0, format("Ctrl+Tab shows the tab picked (%d of %d", switches - switchesWrong, switches) +
		                              (lastSwitchIssue.empty() ? std::string(")") : "; " + lastSwitchIssue + ")"));
		check(stuckWindows == 0, format("every window over the circuit closed when asked (%d seen, %d stuck)", modalsSeen, stuckWindows));
		check(prefsUnsaved == 0, format("a setting changed in Settings is saved (%d checks", prefsChecks) +
		                             (lastPrefsIssue.empty() ? std::string(")") : ": " + lastPrefsIssue + ")"));
		const long growth = (rss[2] - rss[1]) / 1024;
		printf("memory: %ld MB at the start, %ld MB a quarter in, %ld MB at the end\n", rss[0] / 1024, rss[1] / 1024, rss[2] / 1024);
#if defined(__SANITIZE_ADDRESS__)
		printf("(memory growth not checked under AddressSanitizer)\n");
#else
		check(rss[1] == 0 || growth < 150, format("memory over the last three quarters grew %ld MB (under 150)", growth));
#endif

		// ---- Closing the window, as its close button does ----
		gWhat = "closing the window";
		const std::string path = w->filePath();
		const std::string last = text();
		const guint timer = w->timer;
		GObject* winObject = G_OBJECT(w->window());
		bool winFinalized = false;
		g_object_weak_ref(winObject, [](gpointer done, GObject*) { *static_cast<bool*>(done) = true; }, &winFinalized);
		std::vector<bool*> areasGone;
		for (Canvas* k : w->canvases) {
			bool* gone = new bool(false);
			g_object_weak_ref(G_OBJECT(k->widget()), [](gpointer done, GObject*) { *static_cast<bool*>(done) = true; }, gone);
			areasGone.push_back(gone);
		}
		const int canvasesBefore = Canvas::liveCount;
		const int tabs = (int)w->canvases.size();
		gtk_window_close(w->window());
		pump(1500);
		const bool closed = std::find(circuitWindows().begin(), circuitWindows().end(), w) == circuitWindows().end();
		check(closed && CircuitWindow::liveCount == windowsAtStart - 1, format("the window closed, and was freed with its circuit (%d circuit windows alive, %d before)",
		                                                                       CircuitWindow::liveCount, windowsAtStart));
		w = nullptr;
		check(Canvas::liveCount == canvasesBefore - tabs, format("its %d canvases went with it (%d alive)", tabs, Canvas::liveCount));
		check(g_main_context_find_source_by_id(nullptr, timer) == nullptr, "its 60-a-second clock stopped");
		int areasLeft = 0;
		for (bool* g : areasGone) { if (!*g) areasLeft++; }
		check(winFinalized, "the window itself was freed (nothing held on to it)");
		check(areasLeft == 0, format("every canvas's drawing area was freed (%d kept)", areasLeft));
		for (bool* g : areasGone) if (*g) delete g;   // (the kept ones' flags stay for their callbacks)
		if (!winFinalized) g_object_weak_unref(winObject, [](gpointer done, GObject*) { *static_cast<bool*>(done) = true; }, &winFinalized);

		// ---- And opened again ----
		gWhat = "opening it again";
		if (!path.empty()) {
			const int before = CircuitWindow::liveCount;
			check(openCircuit(app, path, nullptr), "the circuit opens again from Your Circuits");
			pump(800);
			CircuitWindow* again = circuitWindows().empty() ? nullptr : circuitWindows().back();
			check(again && CircuitWindow::liveCount == before + 1 && again->filePath() == path, "in a window of its own");
			if (again) {
				const std::string reopened = shape(cl_document_save_text_ex(again->document(), 0));
				check(reopened == last, "it's the circuit as it was closed" + (reopened == last ? std::string() : ": " + firstDifference(last, reopened)));
				gtk_window_close(again->window());
				pump(500);
			}
		}
		gActive = false;
		current = nullptr;
		check(gWarnings == 0, format("no GTK or GLib warnings (%d)", gWarnings));
		for (const std::string& l : gWarningLines) printf("  %s\n", l.c_str());
		for (const std::string& n : notes) printf("  note: %s\n", n.c_str());
		if (!notes.empty()) failures++;
		printf("actions:");
		for (size_t i = 0; i < G_N_ELEMENTS(counts) && i < 50; i++) if (counts[i]) printf(" %zu:%d", i, counts[i]);
		printf("\n");
		printf("%s\n", failures == 0 ? "session-check: all passed" : format("session-check: %d failed", failures).c_str());
		return failures == 0 ? 0 : 1;
	}
};

SessionDriver* SessionDriver::current = nullptr;

namespace session {

void prepare() {
	static char altStack[1 << 16];
	stack_t ss = {};
	ss.ss_sp = altStack;
	ss.ss_size = sizeof altStack;
	sigaltstack(&ss, nullptr);
	struct sigaction sa = {};
	sa.sa_handler = crashed;
	sa.sa_flags = SA_ONSTACK | SA_RESETHAND;
	sigemptyset(&sa.sa_mask);
#if defined(__SANITIZE_ADDRESS__)
	// AddressSanitizer reports bad memory itself, then aborts: the action is printed after its report.
	for (int sig : { SIGABRT }) sigaction(sig, &sa, nullptr);
#else
	for (int sig : { SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL }) sigaction(sig, &sa, nullptr);
#endif
	// G_DEBUG=fatal-criticals stops at a critical with a breakpoint trap.
	sigaction(SIGTRAP, &sa, nullptr);
	g_log_set_writer_func(logWriter, nullptr, nullptr);
}

int run(GtkApplication* app, int actions, unsigned long long seed) {
	static SessionDriver d;
	d.app = app;
	int code = 1;
	guarded("the session check", [&] { code = d.run(actions, seed); });
	gStep = -1;
	gWhat = "quitting";
	return code;
}

}  // namespace session
