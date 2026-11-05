// The Actions rail's list, order, search and edge timing (see Actions.h),
// and the Customize list in Settings > Actions and its own window.

#include "Actions.h"
#include "Dialogs.h"
#include "Settings.h"
#include "Shortcuts.h"
#include "Window.h"

#include <glib/gstdio.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

namespace actions {

namespace {

// Every action, in menu order: File, Edit, View, Simulation, Window, Help
// (the Mac's list, with the Linux menus' own names and places).
const std::vector<Item> kAll = {
	// File
	{ "newCircuit", "New Circuit", "File", "document-new-symbolic", "app.new", "newCircuit", 0 },
	{ "newFromTemplate", "New from Template…", "File", "view-grid-symbolic", "win.new-template", nullptr, 0 },
	{ "openLibrary", "Your Circuits…", "File", "folder-symbolic", "app.open", "openLibrary", 0 },
	{ "importFile", "Import a File…", "File", "document-open-symbolic", "app.import", "importFile", 0 },
	{ "practice", "Open the Practice Circuit", "File", "applications-science-symbolic", "app.open-sample", nullptr, 0 },
	{ "save", "Save a Version", "File", "document-save-symbolic", "win.save", "save", 0 },
	{ "rename", "Rename…", "File", "document-edit-symbolic", "win.rename-circuit", nullptr, 0 },
	{ "duplicateCircuit", "Duplicate Circuit", "File", "edit-copy-symbolic", "win.duplicate-circuit", nullptr, 0 },
	{ "versionHistory", "Version History…", "File", "document-open-recent-symbolic", "win.versions", nullptr, 0 },
	{ "exportImage", "Export as Image…", "File", "image-x-generic-symbolic", "win.export-image", "exportImage", 0 },
	{ "exportReport", "Export Lab Report…", "File", "x-office-document-symbolic", "win.export-report", "exportReport", 0 },
	{ "exportFile", "Export as CedarLogic File…", "File", "document-send-symbolic", "win.save-as", "exportFile", 0 },
	{ "exportV2", "Export for CedarLogic 2…", "File", "document-save-as-symbolic", "win.export-v2", nullptr, 0 },
	{ "exportV1", "Export for CedarLogic 1.x…", "File", "document-save-as-symbolic", "win.export-v1", nullptr, 0 },
	{ "shareLink", "Share Link…", "File", Icon::Link, "app.share-link", nullptr, 0 },
	{ "saveAsTemplate", "Save as Template…", "File", "bookmark-new-symbolic", "win.save-template", nullptr, 0 },
	{ "print", "Print…", "File", "document-print-symbolic", "win.print", "print", 0 },
	{ "settings", "Settings…", "File", "preferences-system-symbolic", "win.preferences", "preferences", 0 },
	// Edit
	{ "undo", "Undo", "Edit", "edit-undo-symbolic", "win.undo", "undo", 0 },
	{ "redo", "Redo", "Edit", "edit-redo-symbolic", "win.redo", "redo", 0 },
	{ "cut", "Cut", "Edit", "edit-cut-symbolic", "win.cut", "cut", 0 },
	{ "copy", "Copy", "Edit", "edit-copy-symbolic", "win.copy", "copy", 0 },
	{ "paste", "Paste", "Edit", "edit-paste-symbolic", "win.paste", "paste", NeedsEdit },
	{ "duplicate", "Duplicate", "Edit", "list-add-symbolic", "win.duplicate", "duplicate", 0 },
	{ "delete", "Delete", "Edit", "edit-delete-symbolic", "win.delete", nullptr, 0 },
	{ "selectAll", "Select All", "Edit", "edit-select-all-symbolic", "win.select-all", "selectAll", 0 },
	{ "addGate", "Add a Gate…", "Edit", "list-add-symbolic", "win.add-gate", "addGate", NeedsEdit },
	{ "rotate", "Rotate", "Edit", "object-rotate-right-symbolic", "win.rotate", "rotate", 0 },
	{ "lockInPlace", "Lock in Place", "Edit", "view-pin-symbolic", "win.lock-in-place", "lockInPlace", 0 },
	{ "unlockAllParts", "Unlock All Parts", "Edit", "changes-allow-symbolic", "win.unlock-all", nullptr, 0 },
	{ "gateSettings", "Gate Settings…", "Edit", "preferences-other-symbolic", "win.gate-settings", nullptr, 0 },
	{ "find", "Find…", "Edit", "edit-find-symbolic", "win.find", "find", 0 },
	{ "findNext", "Find Next", "Edit", "go-down-symbolic", "win.find-next", "findNext", 0 },
	{ "findPrevious", "Find Previous", "Edit", "go-up-symbolic", "win.find-previous", "findPrevious", 0 },
	{ "saveAsPart", "Save as Part…", "Edit", "package-x-generic-symbolic", "win.save-part", nullptr, 0 },
	{ "buildFormula", "Build from Formula…", "Edit", "accessories-calculator-symbolic", "win.build-formula", "buildFormula", NeedsEdit },
	{ "straighten", "Straighten Wires", "Edit", "format-justify-fill-symbolic", "win.straighten", "straighten", NeedsEdit },
	{ "tidy", "Tidy Up", "Edit", Icon::Wand, "win.tidy-up", "tidy", NeedsEdit },
	{ "tidyOtherWay", "Tidy Up the Other Way", "Edit", "view-refresh-symbolic", "win.tidy-other", nullptr, NeedsEdit },
	{ "connectNearby", "Connect Nearby Pins", "Edit", "network-wired-symbolic", "win.connect-nearby", nullptr, NeedsEdit },
	// View
	{ "zoomIn", "Zoom In", "View", "zoom-in-symbolic", "win.zoom-in", "zoomIn", 0 },
	{ "zoomOut", "Zoom Out", "View", "zoom-out-symbolic", "win.zoom-out", "zoomOut", 0 },
	{ "zoomFit", "Zoom to Fit", "View", "zoom-fit-best-symbolic", "win.zoom-fit", "zoomFit", 0 },
	{ "zoomActual", "Actual Size", "View", "zoom-original-symbolic", "win.zoom-actual", "zoomActual", 0 },
	{ "focusMode", "Focus Mode", "View", "view-conceal-symbolic", "win.focus-mode", "focusMode", 0 },
	{ "fullScreen", "Full Screen", "View", "view-fullscreen-symbolic", "win.fullscreen", "fullScreen", 0 },
	{ "gridlines", "Gridlines", "View", "view-grid-symbolic", "win.show-grid", nullptr, 0 },
	{ "wireDots", "Wire Connection Dots", "View", "media-record-symbolic", "win.wire-dots", nullptr, 0 },
	{ "darkMode", "Dark Mode", "View", "weather-clear-night-symbolic", "win.dark", "darkMode", 0 },
	{ "simView", "Simulation View", "View", "media-playback-start-symbolic", "win.sim-view", "simView", 0 },
	{ "projector", "Projector Mode", "View", "video-display-symbolic", "win.projector", nullptr, 0 },
	{ "draw", "Draw on the Circuit", "View", Icon::Draw, "win.draw", "draw", 0 },
	{ "clearDrawing", "Clear Drawing on This Page", "View", "edit-clear-symbolic", "win.clear-drawing", nullptr, 0 },
	{ "showDrawing", "Show Drawing", "View", Icon::Eye, "win.show-drawing", "showDrawing", 0 },
	{ "notes", "Show Notes", "View", Icon::Notes, "win.notes", "notes", 0 },
	// Simulation
	{ "pause", "Running", "Simulation", "media-playback-pause-symbolic", "win.running", nullptr, 0 },
	{ "step", "Step Once", "Simulation", "media-skip-forward-symbolic", "win.step", "step", 0 },
	{ "stepClock", "Step Clock", "Simulation", Icon::StepClock, "win.step-clock", "stepClock", 0 },
	{ "predict", "Predict, Then Reveal", "Simulation", Icon::Predict, "win.predict", nullptr, 0 },
	{ "reveal", "Reveal", "Simulation", "view-reveal-symbolic", "win.reveal", nullptr, 0 },
	{ "truthTable", "Truth Table…", "Simulation", "x-office-spreadsheet-symbolic", "win.truth-table", "truthTable", 0 },
	{ "checkCircuit", "Check My Circuit…", "Simulation", "emblem-ok-symbolic", "win.check-circuit", "checkCircuit", 0 },
	{ "scope", "Oscilloscope", "Simulation", Icon::Wave, "win.scope", "scope", 0 },
	{ "lock", "Lock the Circuit", "Simulation", "changes-prevent-symbolic", "win.lock", "lock", 0 },
	// Window (the Linux app keeps its tabs here)
	{ "newTab", "New Tab", "Window", "tab-new-symbolic", "win.new-tab", "newTab", 0 },
	{ "closeTab", "Close Tab", "Window", "window-close-symbolic", "win.close-tab", "closeTab", 0 },
	{ "reopenTab", "Reopen Closed Tab", "Window", "document-revert-symbolic", "win.reopen-tab", "reopenTab", 0 },
	{ "renameTab", "Rename Tab…", "Window", "document-edit-symbolic", "win.rename-tab", nullptr, 0 },
	{ "splitView", "Split View", "Window", "view-dual-symbolic", "win.split-view", "splitView", 0 },
	{ "switchPane", "Switch Side", "Window", "object-flip-horizontal-symbolic", "win.switch-pane", "switchPane", 0 },
	{ "closeSplit", "Close Split View", "Window", "view-paged-symbolic", "win.close-split", "closeSplit", 0 },
	{ "connectAllPages", "Connect All Pages", "Window", "emblem-shared-symbolic", "win.connect-all-pages", nullptr, 0 },
	{ "disconnectAllPages", "Disconnect All Pages", "Window", "network-offline-symbolic", "win.disconnect-all-pages", nullptr, 0 },
	// Help
	{ "help", "CedarLogic Help", "Help", "help-browser-symbolic", "win.help", "help", 0 },
	{ "shortcuts", "Keyboard Shortcuts", "Help", "input-keyboard-symbolic", "win.shortcuts", "shortcuts", 0 },
	{ "showMeAround", "Show Me Around", "Help", "find-location-symbolic", "win.show-me-around", nullptr, 0 },
	{ "tour", "Learn Logic: Guided Tour", "Help", "applications-science-symbolic", "app.guided-tour", nullptr, 0 },
	// More lessons, with live circuits, on the website (in the browser).
	{ "learnOnline", "Learn Logic Online…", "Help", "web-browser-symbolic", "app.learn-online", nullptr, 0 },
	{ "whatsNew", "What's New in CedarLogic", "Help", "starred-symbolic", "win.whats-new", nullptr, 0 },
	{ "playIntro", "Play the Intro", "Help", "media-playback-start-symbolic", "app.play-intro", nullptr, 0 },
	{ "welcome", "Welcome to CedarLogic", "Help", "face-smile-symbolic", "win.welcome", nullptr, 0 },
	{ "feedback", "Send Feedback…", "Help", "mail-message-new-symbolic", "win.feedback", "feedback", 0 },
	{ "checkForUpdates", "Check for Updates…", "Help", "software-update-available-symbolic", "app.check-updates", nullptr, 0 },
};

const std::vector<std::string> kDefaults = { "checkCircuit", "predict", "tidy", "shareLink", "exportImage", "exportReport" };

std::string lower(const std::string& s) {
	gchar* l = g_utf8_strdown(s.c_str(), -1);
	std::string out = l ? l : "";
	g_free(l);
	return out;
}

std::vector<std::string> ids(const std::vector<const Item*>& items) {
	std::vector<std::string> out;
	for (const Item* a : items) out.push_back(a->id);
	return out;
}

}  // namespace

const char* const kShowSwitch = "Show the Actions panel";
const char* const kHoverSwitch = "Open on hover";

const std::vector<Item>& all() { return kAll; }
const std::vector<std::string>& defaults() { return kDefaults; }

const Item* find(const std::string& id) {
	for (const Item& a : kAll) if (id == a.id) return &a;
	return nullptr;
}

const Item* byAction(const char* action) {
	if (action == nullptr) return nullptr;
	for (const Item& a : kAll) if (std::strcmp(action, a.action) == 0) return &a;
	return nullptr;
}

// ---- The order and the choice --------------------------------------------------------

std::vector<std::string> completeOrder(const std::vector<std::string>& saved) {
	std::vector<std::string> out;
	std::set<std::string> have;
	auto add = [&](const std::string& id) {
		if (have.count(id) || find(id) == nullptr) return;
		have.insert(id);
		out.push_back(id);
	};
	for (const std::string& id : saved.empty() ? kDefaults : saved) add(id);
	for (const Item& a : kAll) add(a.id);
	return out;
}

std::vector<std::string> moveOnRail(const std::vector<std::string>& order, const std::vector<std::string>& railIds, const std::string& id,
                                    int index) {
	std::vector<std::string> rail = railIds;
	auto at = std::find(rail.begin(), rail.end(), id);
	if (at == rail.end()) return order;
	const int from = (int)(at - rail.begin());
	rail.erase(at);
	const int to = std::min(std::max(index, 0), (int)rail.size());
	if (to == from) return order;
	std::vector<std::string> out = order;
	out.erase(std::remove(out.begin(), out.end(), id), out.end());
	if (to < (int)rail.size()) {
		out.insert(std::find(out.begin(), out.end(), rail[(size_t)to]), id);
	} else if (!rail.empty()) {
		auto last = std::find(out.begin(), out.end(), rail.back());
		out.insert(last == out.end() ? out.end() : last + 1, id);
	} else {
		out.insert(out.begin(), id);
	}
	return out;
}

std::vector<std::string> moveInList(const std::vector<std::string>& order, int from, int to) {
	if (from < 0 || from >= (int)order.size()) return order;
	std::vector<std::string> out = order;
	const std::string id = out[(size_t)from];
	out.erase(out.begin() + from);
	to = std::min(std::max(to, 0), (int)out.size());
	out.insert(out.begin() + to, id);
	return out;
}

std::vector<const Item*> order() {
	std::vector<const Item*> out;
	for (const std::string& id : completeOrder(prefs().actionsOrder)) out.push_back(find(id));
	return out;
}

bool shown(const Item& a) {
	const std::vector<std::string>& s = prefs().actionsShown;
	return std::find(s.begin(), s.end(), a.id) != s.end();
}

std::vector<const Item*> rail() {
	std::vector<const Item*> out;
	for (const Item* a : order()) if (shown(*a)) out.push_back(a);
	return out;
}

void setShown(const Item& a, bool on) {
	std::vector<std::string>& s = prefs().actionsShown;
	s.erase(std::remove(s.begin(), s.end(), std::string(a.id)), s.end());
	if (on) s.push_back(a.id);
	changed();
}

void moveListItem(int from, int to) {
	prefs().actionsOrder = moveInList(completeOrder(prefs().actionsOrder), from, to);
	changed();
}

void moveRailItem(const std::string& id, int index) {
	prefs().actionsOrder = moveOnRail(completeOrder(prefs().actionsOrder), ids(rail()), id, index);
	changed();
}

void reset() {
	prefs().actionsOrder.clear();
	prefs().actionsShown = kDefaults;
	changed();
}

bool isDefault() {
	std::set<std::string> s(prefs().actionsShown.begin(), prefs().actionsShown.end());
	return s == std::set<std::string>(kDefaults.begin(), kDefaults.end()) && completeOrder(prefs().actionsOrder) == completeOrder({});
}

// ---- Search ----------------------------------------------------------------------------

std::string keyLabel(const Item& a) {
	if (a.shortcut == nullptr) return "";
	const shortcuts::Action* s = shortcuts::find(a.shortcut);
	return s ? shortcuts::label(shortcuts::keys(*s)) : std::string();
}

std::string plainTitle(const Item& a) {
	std::string t = a.title;
	const std::string dots = "…";
	if (t.size() >= dots.size() && t.compare(t.size() - dots.size(), dots.size(), dots) == 0) t.erase(t.size() - dots.size());
	return t;
}

std::string titleWithKey(const Item& a) {
	const std::string k = keyLabel(a);
	return k.empty() ? plainTitle(a) : plainTitle(a) + " (" + k + ")";
}

std::string words(const Item& a) {
	std::string w = plainTitle(a) + " " + a.menu;
	const std::string k = keyLabel(a);
	if (!k.empty()) {
		w += " " + k;
		// Spelled out: "Ctrl+Shift+T" is also "control ctrl shift t".
		size_t start = 0;
		while (start <= k.size()) {
			size_t plus = k.find('+', start + 1);   // a key that is "+" itself stays
			if (plus == std::string::npos) plus = k.size();
			const std::string part = lower(k.substr(start, plus - start));
			if (part == "ctrl") w += " control ctrl";
			else if (part == "alt") w += " alt option opt";
			else if (part == "super") w += " super meta";
			else w += " " + part;
			start = plus + 1;
		}
	}
	return lower(w);
}

bool matches(const Item& a, const std::string& query) {
	const std::string hay = words(a);
	const std::string q = lower(query);
	size_t i = 0;
	while (i < q.size()) {
		while (i < q.size() && std::isspace((unsigned char)q[i])) i++;
		size_t j = i;
		while (j < q.size() && !std::isspace((unsigned char)q[j])) j++;
		if (j > i && hay.find(q.substr(i, j - i)) == std::string::npos) return false;
		i = j;
	}
	return true;
}

// ---- Running ---------------------------------------------------------------------------

bool enabled(const Item& a, const CircuitWindow* w) {
	if (w == nullptr) return false;
	if ((a.flags & NeedsEdit) && !w->canEdit()) return false;
	return w->actionEnabled(a.action);
}

bool isOn(const Item& a, const CircuitWindow* w) {
	if (w == nullptr) return false;
	if (std::strcmp(a.id, "scope") == 0) return w->scopeWindow() && w->scopeWindow()->visible();
	if (!g_str_has_prefix(a.action, "win.")) return false;
	GActionGroup* g = G_ACTION_GROUP(w->window());
	const char* bare = a.action + 4;
	if (!g_action_group_has_action(g, bare)) return false;
	GVariant* s = g_action_group_get_action_state(g, bare);
	if (s == nullptr) return false;
	const bool on = g_variant_is_of_type(s, G_VARIANT_TYPE_BOOLEAN) && g_variant_get_boolean(s);
	g_variant_unref(s);
	// Running is lit when it's paused (the Mac's Pause or Resume).
	return std::strcmp(a.id, "pause") == 0 ? !on : on;
}

void run(const Item& a, CircuitWindow* w) {
	if (w == nullptr) return;
	if (!enabled(a, w)) {
		if ((a.flags & NeedsEdit) && !w->canEdit()) w->lockNudge();
		else gdk_display_beep(gdk_display_get_default());
		return;
	}
	w->runAction(a.action);
}

bool iconMissing(const Item& a) {
	if (g_str_has_prefix(a.icon, "cedarlogic-")) return false;
	static std::map<std::string, bool> known;
	auto it = known.find(a.icon);
	if (it != known.end()) return it->second;
	const bool missing = !gtk_icon_theme_has_icon(gtk_icon_theme_get_default(), a.icon);
	known[a.icon] = missing;
	return missing;
}

void drawIcon(cairo_t* cr, const Item& a, const RectF& box, float size, const Color& color) {
	if (!iconMissing(a)) { ::drawIcon(cr, a.icon, box, size, color); return; }
	// The set hasn't it: its first letter, in a square.
	const float cx = (box.left + box.right) / 2, cy = (box.top + box.bottom) / 2, r = size / 2;
	strokeRound(cr, rectF(cx - r, cy - r, cx + r, cy + r), 3, color, 1.2f);
	drawTextMid(cr, std::string(1, a.title[0]), rectF(cx - r, cy - r, cx + r, cy + r), size * 0.62f, color, TextAlign::Center, true);
}

// ---- Telling everyone ---------------------------------------------------------------

namespace {
std::map<int, std::function<void()>>& listeners() {
	static std::map<int, std::function<void()>> m;
	return m;
}
}  // namespace

int listen(std::function<void()> f) {
	static int next = 1;
	listeners()[next] = std::move(f);
	return next++;
}

void unlisten(int id) { listeners().erase(id); }

void changed() {
	prefs().save();
	for (CircuitWindow* w : circuitWindows()) w->actionsChanged();
	// (A copy: a listener can take itself away.)
	std::vector<std::function<void()>> fs;
	for (const auto& kv : listeners()) fs.push_back(kv.second);
	for (const auto& f : fs) f();
}

// ---- The edge ------------------------------------------------------------------------------

bool inHotStrip(double x, double y, double w, double h) {
	return w > 0 && h > 0 && x >= w - kHotWidth && x < w + kEdgeSlop && y >= 0 && y < h;
}

bool onSlidRail(double x, double y, double w, double h) {
	return w > 0 && h > 0 && x >= w - kRailWidth && x < w + kEdgeSlop && y >= 0 && y < h;
}

void EdgeHover::changed(double now, bool folded, bool enabled) {
	pending = 0;
	const bool inside = overStrip || overRail;
	if (!inside) dismissed = false;
	if (!folded || !enabled || dismissed) return;
	if (inside && !slidOut) { pending = 1; due = now + kOpenDelay; }
	else if (!inside && slidOut && !menuOpen && !dragging) { pending = -1; due = now + kCloseDelay; }
}

void EdgeHover::setStrip(bool in, double now, bool folded, bool enabled) {
	if (in == overStrip) return;
	overStrip = in;
	changed(now, folded, enabled);
}

void EdgeHover::setRail(bool in, double now, bool folded, bool enabled) {
	if (in == overRail) return;
	overRail = in;
	changed(now, folded, enabled);
}

int EdgeHover::tick(double now, bool folded, bool enabled) {
	if (pending == 0 || now < due) return 0;
	const int p = pending;
	pending = 0;
	const bool inside = overStrip || overRail;
	if (p == 1 && folded && enabled && inside && !slidOut && !dismissed) { slidOut = true; return 1; }
	if (p == -1 && !inside && slidOut && !menuOpen && !dragging) { slidOut = false; overRail = false; return -1; }
	return 0;
}

bool EdgeHover::escape() {
	if (!slidOut || dragging || menuOpen) return false;
	dismissed = true;
	slidOut = false;
	overRail = false;
	pending = 0;
	return true;
}

void EdgeHover::close() {
	slidOut = false;
	overRail = false;
	pending = 0;
}

void FoldState::setFocus(bool on) {
	if (on == focus) return;
	focus = on;
	if (on) { peekBeforeFocus = peek; peek = false; }
	else peek = peekBeforeFocus;
}

void FoldState::setNarrow(bool on) {
	narrow = on;
	if (!on && peek && !focus) peek = false;
}

void FoldState::unfold(bool& collapsed) {
	if (focus) { peek = true; return; }
	collapsed = false;
	if (narrow) peek = true;
}

void FoldState::fold(bool& collapsed) {
	peek = false;
	if (!focus) collapsed = true;
}

// ---- The Customize list ------------------------------------------------------------------

namespace {

const GtkTargetEntry kRowTarget = { (gchar*)"CEDARLOGIC_ACTION_ROW", GTK_TARGET_SAME_APP, 0 };

// A Customize list: its search, the actions a row each (a box to tick, the
// icon, the name, the keys, the menu, a handle to drag by), and its footer.
struct List {
	GtkWidget* box = nullptr;
	GtkWidget* search = nullptr;
	GtkWidget* rows = nullptr;
	GtkWidget* footer = nullptr;
	GtkWidget* resetButton = nullptr;
	GtkWidget* empty = nullptr;
	std::string query;
	int listening = 0;
	guint idle = 0;
	bool busy = false;   // its own change, already showing

	bool searching() const {
		for (char c : query) if (!std::isspace((unsigned char)c)) return true;
		return false;
	}
	void fill();
	void footerText();
	void later() {
		if (idle == 0) idle = g_idle_add([](gpointer p) -> gboolean {
			List* l = static_cast<List*>(p);
			l->idle = 0;
			guarded("the Customize list", [&] { l->fill(); });
			return G_SOURCE_REMOVE;
		}, this);
	}
};

gboolean drawRowIcon(GtkWidget* w, cairo_t* cr, gpointer data) {
	const Item* a = static_cast<const Item*>(data);
	const float W = (float)gtk_widget_get_allocated_width(w), H = (float)gtk_widget_get_allocated_height(w);
	actions::drawIcon(cr, *a, rectF(0, 0, W, H), 15, chrome().accent());
	return TRUE;
}

gboolean drawHandle(GtkWidget* w, cairo_t* cr, gpointer) {
	const float W = (float)gtk_widget_get_allocated_width(w), H = (float)gtk_widget_get_allocated_height(w);
	drawHamburgerIcon(cr, rectF(0, 0, W, H), chrome().ink(0.35f));
	return TRUE;
}

GtkWidget* dimLabel(const std::string& text, double scale) {
	GtkWidget* l = gtk_label_new(text.c_str());
	gtk_style_context_add_class(gtk_widget_get_style_context(l), "hint");
	PangoAttrList* at = pango_attr_list_new();
	pango_attr_list_insert(at, pango_attr_scale_new(scale));
	gtk_label_set_attributes(GTK_LABEL(l), at);
	pango_attr_list_unref(at);
	return l;
}

int listIndex(const std::string& id) {
	const std::vector<std::string> o = completeOrder(prefs().actionsOrder);
	auto it = std::find(o.begin(), o.end(), id);
	return it == o.end() ? -1 : (int)(it - o.begin());
}

void List::footerText() {
	const int total = (int)all().size();
	int matching = 0;
	for (const Item* a : order()) if (!searching() || matches(*a, query)) matching++;
	const std::string t = searching() ? format("%d of %d match. Clear the search to drag them into order.", matching, total)
	                                  : format("%d shown. Drag to change the order, here or on the panel.", (int)rail().size());
	gtk_label_set_text(GTK_LABEL(footer), t.c_str());
	gtk_widget_set_sensitive(resetButton, !isDefault());
}

void List::fill() {
	GList* kids = gtk_container_get_children(GTK_CONTAINER(rows));
	for (GList* k = kids; k; k = k->next) gtk_widget_destroy(GTK_WIDGET(k->data));
	g_list_free(kids);
	const bool filtering = searching();
	int n = 0;
	for (const Item* a : order()) {
		if (filtering && !matches(*a, query)) continue;
		n++;
		GtkWidget* row = gtk_list_box_row_new();
		g_object_set_data(G_OBJECT(row), "cl-action", (gpointer)a);
		GtkWidget* line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
		gtk_widget_set_margin_start(line, 6);
		gtk_widget_set_margin_end(line, 6);
		gtk_widget_set_margin_top(line, 2);
		gtk_widget_set_margin_bottom(line, 2);
		GtkWidget* tick = gtk_check_button_new();
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tick), shown(*a));
		gtk_widget_set_tooltip_text(tick, ("Show " + plainTitle(*a) + " on the panel").c_str());
		g_object_set_data(G_OBJECT(tick), "cl-action", (gpointer)a);
		g_signal_connect(tick, "toggled", CL_CALLBACK(+[](GtkToggleButton* t, gpointer self) {
			List* l = static_cast<List*>(self);
			const Item* item = static_cast<const Item*>(g_object_get_data(G_OBJECT(t), "cl-action"));
			l->busy = true;
			guarded("a Customize box", [&] { setShown(*item, gtk_toggle_button_get_active(t)); });
			l->busy = false;
			l->footerText();
		}), this);
		gtk_box_pack_start(GTK_BOX(line), tick, FALSE, FALSE, 0);
		GtkWidget* icon = gtk_drawing_area_new();
		gtk_widget_set_size_request(icon, 20, 20);
		g_signal_connect(icon, "draw", G_CALLBACK(drawRowIcon), (gpointer)a);
		gtk_box_pack_start(GTK_BOX(line), icon, FALSE, FALSE, 0);
		GtkWidget* title = gtk_label_new(a->title);
		gtk_label_set_xalign(GTK_LABEL(title), 0);
		gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
		gtk_box_pack_start(GTK_BOX(line), title, TRUE, TRUE, 0);
		const std::string k = keyLabel(*a);
		if (!k.empty()) gtk_box_pack_start(GTK_BOX(line), dimLabel(k, 0.92), FALSE, FALSE, 0);
		GtkWidget* menu = dimLabel(a->menu, 0.85);
		gtk_widget_set_size_request(menu, 76, -1);
		gtk_label_set_xalign(GTK_LABEL(menu), 1);
		gtk_box_pack_start(GTK_BOX(line), menu, FALSE, FALSE, 0);
		// The handle: drag by it (not while searching: the list is only some of them).
		GtkWidget* handle = gtk_event_box_new();
		GtkWidget* lines = gtk_drawing_area_new();
		gtk_widget_set_size_request(lines, 18, 20);
		g_signal_connect(lines, "draw", G_CALLBACK(drawHandle), nullptr);
		gtk_container_add(GTK_CONTAINER(handle), lines);
		gtk_widget_set_opacity(handle, filtering ? 0 : 1);
		if (!filtering) {
			gtk_widget_set_tooltip_text(handle, "Drag to change the order");
			gtk_drag_source_set(handle, GDK_BUTTON1_MASK, &kRowTarget, 1, GDK_ACTION_MOVE);
			g_object_set_data(G_OBJECT(handle), "cl-row", row);
			// The row itself goes with the pointer.
			g_signal_connect(handle, "drag-begin", CL_CALLBACK(+[](GtkWidget* h, GdkDragContext* ctx, gpointer) {
				GtkWidget* r = GTK_WIDGET(g_object_get_data(G_OBJECT(h), "cl-row"));
				GtkAllocation al;
				gtk_widget_get_allocation(r, &al);
				cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, std::max(1, al.width), std::max(1, al.height));
				cairo_t* c = cairo_create(s);
				const Chrome ch = chrome();
				setColor(c, ch.tabCard());
				cairo_paint(c);
				gtk_widget_draw(r, c);
				cairo_destroy(c);
				int hx = 0, hy = 0;
				gtk_widget_translate_coordinates(h, r, 0, 0, &hx, &hy);
				cairo_surface_set_device_offset(s, -hx - 6, -hy - 8);
				gtk_drag_set_icon_surface(ctx, s);
				cairo_surface_destroy(s);
			}), nullptr);
			g_signal_connect(handle, "drag-data-get", CL_CALLBACK(+[](GtkWidget* h, GdkDragContext*, GtkSelectionData* data, guint, guint, gpointer) {
				GtkWidget* r = GTK_WIDGET(g_object_get_data(G_OBJECT(h), "cl-row"));
				const Item* item = static_cast<const Item*>(g_object_get_data(G_OBJECT(r), "cl-action"));
				gtk_selection_data_set(data, gdk_atom_intern_static_string("CEDARLOGIC_ACTION_ROW"), 8, (const guchar*)item->id,
				                       (gint)std::strlen(item->id));
			}), nullptr);
			gtk_drag_dest_set(row, GTK_DEST_DEFAULT_ALL, &kRowTarget, 1, GDK_ACTION_MOVE);
			g_signal_connect(row, "drag-motion", CL_CALLBACK(+[](GtkWidget* r, GdkDragContext*, gint, gint, guint, gpointer) -> gboolean {
				if (GtkWidget* lb = gtk_widget_get_parent(r)) gtk_list_box_drag_highlight_row(GTK_LIST_BOX(lb), GTK_LIST_BOX_ROW(r));
				return FALSE;
			}), nullptr);
			g_signal_connect(row, "drag-leave", CL_CALLBACK(+[](GtkWidget* r, GdkDragContext*, guint, gpointer) {
				if (GtkWidget* lb = gtk_widget_get_parent(r)) gtk_list_box_drag_unhighlight_row(GTK_LIST_BOX(lb));
			}), nullptr);
			g_signal_connect(row, "drag-data-received", CL_CALLBACK(+[](GtkWidget* r, GdkDragContext*, gint, gint, GtkSelectionData* data, guint,
			                                                             guint, gpointer self) {
				const guchar* raw = gtk_selection_data_get_data(data);
				const gint len = gtk_selection_data_get_length(data);
				if (raw == nullptr || len <= 0) return;
				const std::string from((const char*)raw, (size_t)len);
				const Item* onto = static_cast<const Item*>(g_object_get_data(G_OBJECT(r), "cl-action"));
				const int a = listIndex(from), b = listIndex(onto->id);
				if (a < 0 || b < 0 || a == b) return;
				List* l = static_cast<List*>(self);
				guarded("the Customize list", [&] { moveListItem(a, b); });
				l->later();
			}), this);
		}
		gtk_box_pack_start(GTK_BOX(line), handle, FALSE, FALSE, 0);
		gtk_container_add(GTK_CONTAINER(row), line);
		gtk_list_box_insert(GTK_LIST_BOX(rows), row, -1);
	}
	gtk_label_set_text(GTK_LABEL(empty), format("No actions match “%s”.", query.c_str()).c_str());
	gtk_widget_show_all(rows);
	(void)n;
	footerText();
}

// Alt+Up and Alt+Down move the row with the keyboard (not while searching).
gboolean listKey(GtkWidget* lb, GdkEventKey* e, gpointer self) {
	List* l = static_cast<List*>(self);
	if (!(e->state & GDK_MOD1_MASK) || (e->keyval != GDK_KEY_Up && e->keyval != GDK_KEY_Down) || l->searching()) return FALSE;
	GtkListBoxRow* row = gtk_list_box_get_selected_row(GTK_LIST_BOX(lb));
	if (row == nullptr) {
		GtkWidget* f = gtk_window_get_focus(GTK_WINDOW(gtk_widget_get_toplevel(lb)));
		while (f && !GTK_IS_LIST_BOX_ROW(f)) f = gtk_widget_get_parent(f);
		row = f ? GTK_LIST_BOX_ROW(f) : nullptr;
	}
	if (row == nullptr) return FALSE;
	const Item* a = static_cast<const Item*>(g_object_get_data(G_OBJECT(row), "cl-action"));
	const int i = listIndex(a->id);
	const int to = i + (e->keyval == GDK_KEY_Up ? -1 : 1);
	if (i < 0 || to < 0 || to >= (int)all().size()) return TRUE;
	moveListItem(i, to);
	l->fill();
	// The moved row keeps the keyboard.
	GList* kids = gtk_container_get_children(GTK_CONTAINER(lb));
	for (GList* k = kids; k; k = k->next)
		if (g_object_get_data(G_OBJECT(k->data), "cl-action") == a) {
			gtk_list_box_select_row(GTK_LIST_BOX(lb), GTK_LIST_BOX_ROW(k->data));
			gtk_widget_grab_focus(GTK_WIDGET(k->data));
		}
	g_list_free(kids);
	return TRUE;
}

// The list, `height` points tall, with `query` in its search.
GtkWidget* makeList(int height, const std::string& query, GtkWidget** searchOut) {
	List* l = new List();
	l->query = query;
	l->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	l->search = gtk_search_entry_new();
	gtk_widget_set_name(l->search, "actions-search");
	gtk_entry_set_placeholder_text(GTK_ENTRY(l->search), "Search actions");
	gtk_widget_set_tooltip_text(l->search, "Search actions by name, menu or shortcut");
	gtk_entry_set_text(GTK_ENTRY(l->search), query.c_str());
	g_signal_connect(l->search, "search-changed", CL_CALLBACK(+[](GtkSearchEntry* s, gpointer self) {
		List* l = static_cast<List*>(self);
		l->query = gtk_entry_get_text(GTK_ENTRY(s));
		guarded("the Customize list", [&] { l->fill(); });
	}), l);
	// Escape clears the search first.
	g_signal_connect(l->search, "stop-search", CL_CALLBACK(+[](GtkSearchEntry* s, gpointer) {
		gtk_entry_set_text(GTK_ENTRY(s), "");
	}), nullptr);
	gtk_box_pack_start(GTK_BOX(l->box), l->search, FALSE, FALSE, 0);
	GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
	gtk_widget_set_size_request(scroll, -1, height);
	// Settings > Actions makes it shorter on a small screen (Settings.h).
	g_object_set_data(G_OBJECT(scroll), settings::kFitList, GINT_TO_POINTER(height));
	l->rows = gtk_list_box_new();
	gtk_widget_set_name(l->rows, "actions-list");
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(l->rows), GTK_SELECTION_SINGLE);
	l->empty = dimLabel("", 1.0);
	gtk_widget_set_margin_top(l->empty, 12);
	gtk_widget_show(l->empty);
	gtk_list_box_set_placeholder(GTK_LIST_BOX(l->rows), l->empty);
	g_signal_connect(l->rows, "key-press-event", G_CALLBACK(listKey), l);
	gtk_container_add(GTK_CONTAINER(scroll), l->rows);
	gtk_box_pack_start(GTK_BOX(l->box), scroll, TRUE, TRUE, 0);
	GtkWidget* bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	l->footer = dimLabel("", 0.92);
	gtk_label_set_xalign(GTK_LABEL(l->footer), 0);
	gtk_label_set_line_wrap(GTK_LABEL(l->footer), TRUE);
	gtk_box_pack_start(GTK_BOX(bottom), l->footer, TRUE, TRUE, 0);
	l->resetButton = gtk_button_new_with_label("Reset to Default");
	g_signal_connect(l->resetButton, "clicked", CL_CALLBACK(+[](GtkButton*, gpointer self) {
		List* l = static_cast<List*>(self);
		guarded("Reset to Default", [&] { reset(); });
		l->later();
	}), l);
	gtk_box_pack_end(GTK_BOX(bottom), l->resetButton, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(l->box), bottom, FALSE, FALSE, 0);
	// A change from anywhere else (a drag on the rail, the other list): shown again.
	l->listening = listen([l] { if (!l->busy) l->later(); });
	g_signal_connect(l->box, "destroy", CL_CALLBACK(+[](GtkWidget*, gpointer self) {
		List* l = static_cast<List*>(self);
		unlisten(l->listening);
		if (l->idle) g_source_remove(l->idle);
		delete l;
	}), l);
	l->fill();
	if (searchOut) *searchOut = l->search;
	return l->box;
}

std::string toggleKey() {
	const shortcuts::Action* a = shortcuts::find("toggleActions");
	const std::string k = a ? shortcuts::label(shortcuts::keys(*a)) : std::string();
	return k.empty() ? std::string() : " (" + k + ")";
}

const char* const kShowHint = "The commands you use most, down the right edge of the window. Rest on one for its name and shortcut, click to run "
                              "it, drag it up or down to move it.";
const char* const kHoverHint = "When the panel is folded, move the pointer to the window’s right edge and it slides out. Move away and it goes back.";
std::string offHint() { return "Everything is still in the menus, with its shortcut. View ▸ Show Actions" + toggleKey() + " turns it back on."; }
std::string foldHint() {
	return "Click the panel’s left edge to fold it away, or use View ▸ Hide Actions" + toggleKey() +
	       ". Right-click it to change what’s on it. Help ▸ Show Me Around points it out.";
}

// The two switches (Settings > Actions, the Customize window): "Show the
// Actions panel", and "Open on hover" under it, greyed while it's off. They
// follow a change made anywhere else (F9, the welcome's switches).
struct Switches {
	GtkWidget *show = nullptr, *hover = nullptr;
	GtkWidget *showHint = nullptr, *hoverHint = nullptr;   // Settings' only
	GtkWidget* list = nullptr;                            // greyed with the panel off
	int listening = 0;
	bool busy = false;
	void follow() {
		busy = true;
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(show), prefs().actionsOn);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(hover), prefs().actionsHoverOpen);
		busy = false;
		gtk_widget_set_sensitive(hover, prefs().actionsOn);
		if (hoverHint) gtk_widget_set_sensitive(hoverHint, prefs().actionsOn);
		if (showHint) gtk_label_set_text(GTK_LABEL(showHint), prefs().actionsOn ? kShowHint : offHint().c_str());
		if (list) gtk_widget_set_sensitive(list, prefs().actionsOn);
	}
};

// Made, wired and destroyed with `owner`.
Switches* makeSwitches(GtkWidget* owner) {
	Switches* sw = new Switches();
	sw->show = gtk_check_button_new_with_label(kShowSwitch);
	sw->hover = gtk_check_button_new_with_label(kHoverSwitch);
	gtk_widget_set_name(sw->show, "actions-show");
	gtk_widget_set_name(sw->hover, "actions-hover");
	for (GtkWidget* b : { sw->show, sw->hover }) gtk_widget_set_halign(b, GTK_ALIGN_START);
	gtk_widget_set_tooltip_text(sw->hover, kHoverHint);
	g_signal_connect(sw->show, "toggled", CL_CALLBACK(+[](GtkToggleButton* t, gpointer self) {
		Switches* sw = static_cast<Switches*>(self);
		if (sw->busy) return;
		prefs().actionsOn = gtk_toggle_button_get_active(t);
		guarded("Show the Actions panel", [] { changed(); });
	}), sw);
	g_signal_connect(sw->hover, "toggled", CL_CALLBACK(+[](GtkToggleButton* t, gpointer self) {
		Switches* sw = static_cast<Switches*>(self);
		if (sw->busy) return;
		prefs().actionsHoverOpen = gtk_toggle_button_get_active(t);
		guarded("Open on hover", [] { changed(); });
	}), sw);
	sw->listening = listen([sw] { sw->follow(); });
	g_signal_connect(owner, "destroy", CL_CALLBACK(+[](GtkWidget*, gpointer self) {
		Switches* sw = static_cast<Switches*>(self);
		unlisten(sw->listening);
		delete sw;
	}), sw);
	return sw;
}

GtkWidget* g_customize = nullptr;

}  // namespace

GtkWidget* settingsPage() {
	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_widget_set_margin_start(box, 28);
	gtk_widget_set_margin_end(box, 28);
	gtk_widget_set_margin_top(box, 22);
	gtk_widget_set_margin_bottom(box, 22);
	GtkWidget* grid = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
	GtkWidget* label = gtk_label_new("Actions:");
	settings::formLabel(label);
	gtk_widget_set_margin_top(label, 5);
	gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
	GtkWidget* right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_widget_set_hexpand(right, TRUE);
	Switches* sw = makeSwitches(box);
	sw->showHint = gtk_label_new(kShowHint);
	sw->hoverHint = gtk_label_new(kHoverHint);
	for (GtkWidget* h : { sw->showHint, sw->hoverHint }) {
		settings::hintStyle(h);
		gtk_widget_set_margin_start(h, 26);
	}
	gtk_widget_set_margin_top(sw->hover, 6);
	gtk_box_pack_start(GTK_BOX(right), sw->show, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(right), sw->showHint, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(right), sw->hover, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(right), sw->hoverHint, FALSE, FALSE, 0);
	gtk_grid_attach(GTK_GRID(grid), right, 1, 0, 1, 1);
	gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);
	GtkWidget* heading = gtk_label_new("Show these, in this order:");
	gtk_style_context_add_class(gtk_widget_get_style_context(heading), "heading");
	gtk_label_set_xalign(GTK_LABEL(heading), 0);
	gtk_widget_set_margin_top(heading, 8);
	gtk_box_pack_start(GTK_BOX(box), heading, FALSE, FALSE, 0);
	sw->list = makeList(300, "", nullptr);
	gtk_box_pack_start(GTK_BOX(box), sw->list, TRUE, TRUE, 0);
	GtkWidget* fold = gtk_label_new(foldHint().c_str());
	settings::hintStyle(fold);
	gtk_box_pack_start(GTK_BOX(box), fold, FALSE, FALSE, 0);
	sw->follow();
	return box;
}

GtkWidget* customizeWindow() { return g_customize; }

void showCustomize(CircuitWindow* w, const std::string& query) {
	if (g_customize) { gtk_window_present(GTK_WINDOW(g_customize)); return; }
	GtkWidget* win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	g_customize = win;
	gtk_widget_set_name(win, "customize-actions");
	gtk_window_set_title(GTK_WINDOW(win), "Customize Actions");
	gtk_window_set_icon_name(GTK_WINDOW(win), "cedarlogic");
	gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
	if (w) {
		gtk_window_set_transient_for(GTK_WINDOW(win), w->window());
		gtk_window_set_destroy_with_parent(GTK_WINDOW(win), TRUE);
		gtk_window_set_modal(GTK_WINDOW(win), TRUE);
	}
	gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER_ON_PARENT);
	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_margin_start(box, 20);
	gtk_widget_set_margin_end(box, 20);
	gtk_widget_set_margin_top(box, 20);
	gtk_widget_set_margin_bottom(box, 20);
	gtk_widget_set_size_request(box, 520 - 40, -1);
	GtkWidget* title = gtk_label_new("Customize Actions");
	gtk_style_context_add_class(gtk_widget_get_style_context(title), "sheet-title");
	gtk_label_set_xalign(GTK_LABEL(title), 0);
	GtkWidget* line = gtk_label_new("Tick what goes on the Actions panel, anything from any menu. They show in this order, top to bottom. "
	                                "You can also drag them up or down on the panel itself.");
	gtk_style_context_add_class(gtk_widget_get_style_context(line), "hint");
	settings::hintStyle(line);
	GtkWidget* search = nullptr;
	GtkWidget* list = makeList(360, query, &search);
	g_object_set_data(G_OBJECT(win), "cl-search", search);
	GtkWidget* bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	{
		Switches* sw = makeSwitches(win);
		gtk_box_pack_start(GTK_BOX(bottom), sw->show, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(bottom), sw->hover, FALSE, FALSE, 0);
		sw->follow();
	}
	GtkWidget* done = gtk_button_new_with_label("Done");
	gtk_style_context_add_class(gtk_widget_get_style_context(done), "suggested-action");
	g_signal_connect_swapped(done, "clicked", G_CALLBACK(gtk_widget_destroy), win);
	gtk_box_pack_end(GTK_BOX(bottom), done, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), line, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), list, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(box), bottom, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(win), box);
	{
		GtkWidget* scroll = nullptr;
		std::function<void(GtkWidget*)> find = [&](GtkWidget* x) {
			if (!scroll && g_object_get_data(G_OBJECT(x), settings::kFitList)) scroll = x;
			if (!scroll && GTK_IS_CONTAINER(x)) gtk_container_foreach(GTK_CONTAINER(x), [](GtkWidget* c, gpointer f) {
				(*static_cast<std::function<void(GtkWidget*)>*>(f))(c); }, &find);
		};
		find(list);
		gtk_widget_show_all(box);
		int minH = 0, natH = 0;
		gtk_widget_get_preferred_height_for_width(box, 520 - 40, &minH, &natH);
		const int over = natH + 40 - usableArea(w ? GTK_WIDGET(w->window()) : nullptr).height;
		// (Down to 110 points of list: the Raspberry Pi touchscreen's 480 under Wayland, with its panel and a title bar.)
		if (scroll && over > 0) gtk_widget_set_size_request(scroll, -1, std::max(110, 360 - over));
	}
	// Escape clears a search first, then closes.
	g_signal_connect(win, "key-press-event", CL_CALLBACK(+[](GtkWidget* self, GdkEventKey* e, gpointer) -> gboolean {
		if (e->keyval != GDK_KEY_Escape) return FALSE;
		GtkWidget* s = GTK_WIDGET(g_object_get_data(G_OBJECT(self), "cl-search"));
		if (s && *gtk_entry_get_text(GTK_ENTRY(s))) { gtk_entry_set_text(GTK_ENTRY(s), ""); return TRUE; }
		gtk_widget_destroy(self);
		return TRUE;
	}), nullptr);
	g_signal_connect(win, "destroy", CL_CALLBACK(+[](GtkWidget* self, gpointer) { if (g_customize == self) g_customize = nullptr; }), nullptr);
	gtk_widget_show_all(win);
	gtk_window_set_focus(GTK_WINDOW(win), done);
}

// ---- --actions-check ----------------------------------------------------------------------

namespace {

int gFailures = 0;
void expect(bool ok, const std::string& what) {
	printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
	if (!ok) gFailures++;
}

std::string joined(const std::vector<std::string>& v) {
	std::string s;
	for (const std::string& x : v) s += (s.empty() ? "" : " ") + x;
	return s;
}

std::string configFile() {
	gchar* f = g_build_filename(g_get_user_config_dir(), "CedarLogic", "native.ini", nullptr);
	std::string s = f;
	g_free(f);
	return s;
}

// The settings as a launch reads them, from `text` (nullptr: no file at all).
void launchWith(const char* text) {
	const std::string f = configFile();
	gchar* dir = g_path_get_dirname(f.c_str());
	g_mkdir_with_parents(dir, 0700);
	g_free(dir);
	if (text) g_file_set_contents(f.c_str(), text, -1, nullptr);
	else g_remove(f.c_str());
	prefs() = Prefs();
	prefs().load();
}

void relaunch() {
	prefs().save();
	prefs() = Prefs();
	prefs().load();
}

}  // namespace

int check() {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	// Its own settings only (main gives it a folder of its own).
	if (!g_getenv("CL_ACTIONS_CHECK_HOME")) { printf("FAIL --actions-check runs with a settings folder of its own\n"); return 1; }

	// ---- 0.4: on and open for everyone, once; the choice kept after ----
	auto fileText = [] {
		gchar* t = nullptr;
		std::string out;
		if (g_file_get_contents(configFile().c_str(), &t, nullptr, nullptr)) out = t;
		g_free(t);
		return out;
	};
	auto onOpen = [] { return prefs().actionsOn && !prefs().actionsCollapsed; };

	// ---- Opening a circuit replaces the one you're in, once for everyone ----
	launchWith(nullptr);
	expect(prefs().openReplaces, "a new install: opening a circuit replaces the one you're in");
	launchWith("[CedarLogic]\nopenReplaces=false\n");
	expect(prefs().openReplaces && !prefs().openReplacesSet,
	       "an earlier build's openReplaces=false, never chosen (no openReplacesSet): replaces, as everyone's default");
	launchWith("[CedarLogic]\nopenReplaces=false\nopenReplacesSet=true\n");
	expect(!prefs().openReplaces, "openReplaces=false chosen in Settings (openReplacesSet=true): kept, a new window each time");
	relaunch();
	expect(!prefs().openReplaces && prefs().openReplacesSet && fileText().find("openReplacesSet=true") != std::string::npos,
	       "and still so after a save and another launch");
	launchWith("[CedarLogic]\nopenReplaces=true\nopenReplacesSet=true\n");
	expect(prefs().openReplaces, "openReplaces=true chosen: kept");
	auto state = [] { return format("on %d, folded %d", prefs().actionsOn, prefs().actionsCollapsed); };
	launchWith(nullptr);
	expect(onOpen() && prefs().actionsHoverOpen && !prefs().showMeAroundSeen,
	       "a new install has the panel on and open, Open on hover on, Show Me Around not seen yet (" + state() + ")");
	expect(ids(rail()) == kDefaults, "with the six default actions (" + joined(ids(rail())) + ")");
	relaunch();
	expect(onOpen(), "the next launch keeps it on and open (" + state() + ")");
	struct Old { const char* what; const char* ini; };
	const Old olds[] = {
		{ "settings from before the panel (no actionsMode)", "[CedarLogic]\nbrandAccentSet=true\nshowGrid=true\n" },
		{ "an older file with only the welcome seen", "[CedarLogic]\nhasSeenWelcome=true\n" },
		{ "Hidden, with the one-time line owed", "[CedarLogic]\nbrandAccentSet=true\nactionsMode=2\nactionsHint=true\n" },
		{ "In the toolbar", "[CedarLogic]\nbrandAccentSet=true\nactionsMode=1\nactionsHint=false\n" },
		{ "On screen and folded", "[CedarLogic]\nbrandAccentSet=true\nactionsMode=0\nactionsCollapsed=true\nactionsHoverOpen=false\n" },
		{ "Hidden and folded", "[CedarLogic]\nbrandAccentSet=true\nactionsMode=2\nactionsCollapsed=true\n" },
		{ "on but folded, the first step done and not the second", "[CedarLogic]\nactionsOn04=true\nactionsOn=true\nactionsCollapsed=true\n" },
	};
	for (const Old& o : olds) {
		launchWith(o.ini);
		const bool ok = onOpen();
		relaunch();
		const std::string f = fileText();
		expect(ok && onOpen() && f.find("actionsMode") == std::string::npos && f.find("actionsHint") == std::string::npos &&
		           f.find("actionsOn04=true") != std::string::npos && f.find("actionsOpen04=true") != std::string::npos,
		       std::string(o.what) + ": on and open, the old mode gone from the file, both steps marked done (" + state() + ")");
	}
	launchWith("[CedarLogic]\nbrandAccentSet=true\nactionsMode=0\nactionsCollapsed=true\nactionsHoverOpen=false\n");
	expect(!prefs().actionsHoverOpen, "Open on hover, turned off before, stays off");
	// After the migration, every choice sticks.
	prefs().actionsOn = false;
	relaunch();
	expect(!prefs().actionsOn, "turned off after the migration, it stays off (" + state() + ")");
	relaunch();
	expect(!prefs().actionsOn, "and the launch after that too");
	prefs().actionsOn = true;
	prefs().actionsCollapsed = true;
	relaunch();
	expect(prefs().actionsOn && prefs().actionsCollapsed, "folded after the migration, it stays folded (" + state() + ")");
	prefs().showMeAroundSeen = true;
	relaunch();
	expect(prefs().showMeAroundSeen, "Show Me Around, once seen, stays seen");
	{
		const ActionsStart n = actionsStart(false, false, false, false, true), m = actionsStart(true, false, false, false, true),
		                   d = actionsStart(true, true, false, true, true), h = actionsStart(true, true, true, false, true);
		expect(n.on && !n.collapsed && m.on && !m.collapsed && !d.on && d.collapsed && h.on && !h.collapsed, "actionsStart's cases");
	}

	// ---- Settings > Actions: two switches, no toolbar mode ----
	{
		launchWith(nullptr);
		GtkWidget* page = settingsPage();
		g_object_ref_sink(page);
		std::vector<GtkWidget*> checks, combos;
		std::function<void(GtkWidget*)> walk = [&](GtkWidget* x) {
			if (GTK_IS_CHECK_BUTTON(x) && gtk_widget_get_name(x) && std::string(gtk_widget_get_name(x)).rfind("actions-", 0) == 0) checks.push_back(x);
			if (GTK_IS_COMBO_BOX(x)) combos.push_back(x);
			if (GTK_IS_CONTAINER(x))
				gtk_container_foreach(GTK_CONTAINER(x), [](GtkWidget* c, gpointer f) { (*static_cast<std::function<void(GtkWidget*)>*>(f))(c); }, &walk);
		};
		walk(page);
		auto labelOf = [](GtkWidget* b) { const char* l = gtk_button_get_label(GTK_BUTTON(b)); return std::string(l ? l : ""); };
		const bool two = checks.size() == 2 && labelOf(checks[0]) == kShowSwitch && labelOf(checks[1]) == kHoverSwitch;
		expect(two && combos.empty(), format("Settings > Actions has \"Show the Actions panel\" and \"Open on hover\" under it, and no "
		                                     "On screen / In the toolbar / Hidden choice (%zu switches, %zu dropdowns)", checks.size(), combos.size()));
		if (two) {
			expect(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checks[0])) && gtk_widget_get_sensitive(checks[1]), "on, with Open on hover live");
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checks[0]), FALSE);
			expect(!prefs().actionsOn && !gtk_widget_get_sensitive(checks[1]), "turned off: the panel is off and Open on hover is greyed");
			relaunch();
			expect(!prefs().actionsOn, "and that's saved");
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checks[0]), TRUE);
			expect(prefs().actionsOn && gtk_widget_get_sensitive(checks[1]), "on again: Open on hover is live again");
			prefs().actionsOn = false;
			changed();
			expect(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checks[0])), "a change made elsewhere (F9, the welcome) shows in Settings too");
			prefs().actionsOn = true;
			changed();
		}
		g_object_unref(page);
	}
	expect(find("showMeAround") && std::string(find("showMeAround")->title) == "Show Me Around" &&
	           std::string(find("showMeAround")->action) == "win.show-me-around",
	       "Help ▸ Show Me Around is in the list");
	expect(find("tour") && std::string(find("tour")->title) == "Learn Logic: Guided Tour", "the lesson is \"Learn Logic: Guided Tour\"");
	expect(std::string("mailto:") + CL_CONTACT_EMAIL == "mailto:cedarlogic@proton.me", "About's address is mailto:cedarlogic@proton.me");

	// ---- The order: one order for the rail and the list ----
	launchWith(nullptr);
	const std::vector<std::string> full = completeOrder({});
	bool every = full.size() == all().size();
	for (const Item& a : all()) every = every && std::count(full.begin(), full.end(), std::string(a.id)) == 1;
	expect(every && std::equal(kDefaults.begin(), kDefaults.end(), full.begin()), format("the list has every action once (%zu), the defaults first", full.size()));
	const std::vector<std::string> kept = completeOrder({ "zoomIn", "noSuchAction", "tidy" });
	expect(kept.size() == all().size() && kept[0] == "zoomIn" && kept[1] == "tidy", "a saved order is kept, an unknown name dropped, new actions after");
	moveRailItem("checkCircuit", 2);
	const std::vector<std::string> afterDrag = ids(rail());
	expect(afterDrag == std::vector<std::string>({ "predict", "tidy", "checkCircuit", "shareLink", "exportImage", "exportReport" }),
	       "a drag on the rail moves it there (" + joined(afterDrag) + ")");
	std::vector<std::string> listShown;
	for (const Item* a : order()) if (shown(*a)) listShown.push_back(a->id);
	expect(listShown == afterDrag, "and the Customize list has the same order");
	relaunch();
	expect(ids(rail()) == afterDrag, "which a relaunch keeps");
	{
		const int from = (int)(std::find(full.begin(), full.end(), "exportReport") - full.begin());
		const std::vector<std::string> o = completeOrder(prefs().actionsOrder);
		const int at = (int)(std::find(o.begin(), o.end(), "exportReport") - o.begin());
		moveListItem(at, 0);
		(void)from;
	}
	expect(!rail().empty() && std::string(rail()[0]->id) == "exportReport", "a drag in the list moves it on the rail too (" + joined(ids(rail())) + ")");
	moveRailItem("exportReport", 99);
	expect(std::string(rail().back()->id) == "exportReport", "dragged past the end, it's last");
	setShown(*find("zoomIn"), true);
	expect(ids(rail()).size() == 7 && shown(*find("zoomIn")), "ticking one adds it to the rail");
	expect(!isDefault(), "and that's not the default");
	reset();
	expect(isDefault() && ids(rail()) == kDefaults, "Reset to Default gives back the six, in order");
	expect(moveOnRail(full, kDefaults, "tidy", 2) == full, "a drag that lets go where it was changes nothing");

	// ---- Search ----
	auto hits = [](const std::string& q) {
		std::vector<std::string> out;
		for (const Item* a : order()) if (matches(*a, q)) out.push_back(a->id);
		return out;
	};
	auto has = [](const std::vector<std::string>& v, const char* id) { return std::count(v.begin(), v.end(), std::string(id)) == 1; };
	expect(hits("check") == std::vector<std::string>({ "checkCircuit", "checkForUpdates" }), "\"check\" finds Check My Circuit and Check for Updates by name (" +
	                                                                                           joined(hits("check")) + ")");
	const std::vector<std::string> ex = hits("export");
	bool exportsOnly = !ex.empty();
	for (const std::string& id : ex) exportsOnly = exportsOnly && lower(find(id)->title).find("export") != std::string::npos;
	expect(exportsOnly && ex.size() == 5, "\"export\" finds the five exports and nothing else (" + joined(ex) + ")");
	const std::vector<std::string> st = hits("shift t"), cst = hits("ctrl shift t"), ce = hits("control e");
	expect(has(st, "checkCircuit"), "\"shift t\" finds Check My Circuit by its shortcut, Shift+T (" + joined(st) + ")");
	expect(has(cst, "reopenTab") && !has(cst, "checkCircuit"), "\"ctrl shift t\" finds Reopen Closed Tab and not Shift+T's (" + joined(cst) + ")");
	expect(has(ce, "exportImage"), "\"control e\", spelled out, finds Export as Image (Ctrl+E)");
	expect(hits("simulation predict") == std::vector<std::string>({ "predict" }), "\"simulation predict\" finds Predict by its menu and name");
	expect(hits("EXPORT IMAGE") == std::vector<std::string>({ "exportImage" }), "any case, every word has to match (" + joined(hits("EXPORT IMAGE")) + ")");
	expect(hits("zzzz").empty(), "a word nothing has finds nothing");
	expect(hits("  ").size() == all().size(), "an empty search shows them all");

	// ---- The edge's hot strip ----
	const double W = 1000, H = 700;
	expect(inHotStrip(W - 1, 350, W, H), "the last column of pixels is in the hot strip");
	expect(inHotStrip(W - 12, 10, W, H) && !inHotStrip(W - 12.5, 10, W, H), "it's 12 points wide");
	expect(inHotStrip(W - 5, 0, W, H) && inHotStrip(W - 5, H - 1, W, H) && !inHotStrip(W - 5, H, W, H) && !inHotStrip(W - 5, -1, W, H),
	       "and the whole height of the area beside it");
	expect(inHotStrip(W + 3, 300, W, H) && !inHotStrip(W + kEdgeSlop, 300, W, H), "past the edge, the window's resize border still counts");
	expect(onSlidRail(W - 46, 10, W, H) && !onSlidRail(W - 47, 10, W, H), "the slid-out rail is the right 46 points");

	// ---- Its timing ----
	{
		EdgeHover h;
		h.setStrip(true, 0, true, true);
		expect(h.peek() && h.tick(0.05, true, true) == 0 && !h.slidOut, "in the strip, it peeks at once and waits");
		expect(h.tick(0.101, true, true) == 1 && h.slidOut, "and slides out after 100 ms");
		h.setStrip(false, 1.0, true, true);
		expect(h.tick(1.2, true, true) == 0 && h.slidOut, "left, it's still out 200 ms later");
		expect(h.tick(1.401, true, true) == -1 && !h.slidOut, "and back in at 400 ms");
	}
	{
		EdgeHover h;
		h.setStrip(true, 0, true, true);
		h.setStrip(false, 0.04, true, true);
		expect(h.tick(0.2, true, true) == 0 && !h.slidOut, "passing through in 40 ms doesn't open it");
	}
	{
		EdgeHover h;
		h.setStrip(true, 0, true, true);
		h.tick(0.1, true, true);
		h.setRail(true, 0.5, true, true);
		h.setStrip(false, 0.5, true, true);
		expect(h.tick(2, true, true) == 0 && h.slidOut, "it stays out while the pointer is on it");
		h.menuOpen = true;
		h.setRail(false, 2, true, true);
		expect(h.tick(3, true, true) == 0 && h.slidOut, "and while its right-click menu is open");
		h.menuOpen = false;
		h.dragging = true;
		h.changed(3, true, true);
		expect(h.tick(4, true, true) == 0 && h.slidOut, "and during a drag");
		h.dragging = false;
		h.changed(4, true, true);
		expect(h.tick(4.401, true, true) == -1, "then goes 400 ms after");
	}
	{
		EdgeHover h;
		h.setStrip(true, 0, true, true);
		h.tick(0.1, true, true);
		expect(h.escape() && !h.slidOut, "Escape puts back a rail slid out on hover");
		h.changed(0.2, true, true);
		expect(h.tick(1, true, true) == 0 && !h.slidOut, "and it doesn't come out again while the pointer's still there");
		h.setStrip(false, 1, true, true);
		h.setStrip(true, 1.1, true, true);
		expect(h.tick(1.25, true, true) == 1, "until it has left and come back");
	}
	{
		EdgeHover h;
		h.setStrip(true, 0, true, false);
		expect(h.tick(1, true, false) == 0 && h.peek(), "with the setting off it only peeks");
		EdgeHover o;
		o.setStrip(true, 0, false, true);
		expect(o.tick(1, false, true) == 0, "an open rail has no hover to do");
	}

	// ---- Focus mode folds it, and gives it back ----
	{
		bool collapsed = false;
		FoldState f;
		expect(!f.folded(collapsed), "open to start");
		f.setFocus(true);
		expect(f.folded(collapsed) && !collapsed, "focus mode folds it to its tab (the saved state isn't touched)");
		f.unfold(collapsed);
		expect(!f.folded(collapsed) && !collapsed, "a click on the tab opens it in focus mode");
		f.setFocus(false);
		expect(!f.folded(collapsed), "and leaving focus mode it's open, as before");
		collapsed = true;
		FoldState g;
		g.setFocus(true);
		g.unfold(collapsed);
		expect(collapsed && !g.folded(collapsed), "folded before focus mode, opened in it from the tab...");
		g.setFocus(false);
		expect(g.folded(collapsed), "...it's folded again after");
		FoldState n;
		n.setNarrow(true);
		bool c2 = false;
		expect(n.folded(c2), "a narrow window folds it by itself");
		n.unfold(c2);
		expect(!n.folded(c2) && !c2, "a click on the tab opens it there");
		n.setNarrow(false);
		expect(!n.folded(c2) && !n.peek, "and widening the window keeps it open, as saved");
	}

	// ---- Icons ----
	int missing = 0;
	std::string which;
	for (const Item& a : all())
		if (iconMissing(a)) { missing++; which += std::string(" ") + a.id + "=" + a.icon; }
	printf("icons: %d of %zu not in this desktop's icon set (drawn as a letter):%s\n", missing, all().size(), which.c_str());
	for (const std::string& d : kDefaults) expect(!iconMissing(*find(d)), "the default " + d + " has its icon");

	printf("%s\n", gFailures == 0 ? "actions-check: all passed" : format("actions-check: %d failed", gFailures).c_str());
	return gFailures == 0 ? 0 : 1;
}

}  // namespace actions
