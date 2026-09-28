// A circuit window (see Window.h).

#include "Window.h"
#include "Canvas.h"
#include "Dialogs.h"
#include "Palette.h"

#include <cairo-pdf.h>
#include <cairo-svg.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace {

const double kSelectionFadeTime = 0.13, kAppearTime = 0.32, kDragFadeTime = 0.18;

double secondsSince(gint64 t) { return (g_get_monotonic_time() - t) / 1e6; }

// Every command, whichever way it arrives, by action name.
struct Command {
	const char* name;
	void (*run)(CircuitWindow*);
	bool toggle;   // a check item in the menus
};

const Command kCommands[] = {
	{ "save", [](CircuitWindow* w) { w->save(); }, false },
	{ "save-as", [](CircuitWindow* w) { w->saveAs(); }, false },
	{ "export-image", [](CircuitWindow* w) { w->exportImage(); }, false },
	{ "export-v2", [](CircuitWindow* w) { w->exportOlder(2); }, false },
	{ "export-v1", [](CircuitWindow* w) { w->exportOlder(1); }, false },
	{ "print", [](CircuitWindow* w) { w->print(); }, false },
	{ "close", [](CircuitWindow* w) { gtk_window_close(w->window()); }, false },
	{ "undo", [](CircuitWindow* w) { w->undo(); }, false },
	{ "redo", [](CircuitWindow* w) { w->redo(); }, false },
	{ "cut", [](CircuitWindow* w) { if (w->canEdit()) w->cut(); else w->lockNudge(); }, false },
	{ "copy", [](CircuitWindow* w) { w->copy(); }, false },
	{ "paste", [](CircuitWindow* w) { if (w->canEdit()) w->paste(); else w->lockNudge(); }, false },
	{ "duplicate", [](CircuitWindow* w) { if (w->canEdit()) w->duplicate(); else w->lockNudge(); }, false },
	{ "delete", [](CircuitWindow* w) { if (w->canEdit()) w->deleteSelection(); else w->lockNudge(); }, false },
	{ "select-all", [](CircuitWindow* w) { w->selectAll(); }, false },
	{ "add-gate", [](CircuitWindow* w) { if (w->canEdit()) w->quickAdd(); else w->lockNudge(); }, false },
	{ "rotate", [](CircuitWindow* w) { if (w->canEdit()) w->rotate(); else w->lockNudge(); }, false },
	{ "straighten", [](CircuitWindow* w) { if (w->canEdit()) w->straighten(); else w->lockNudge(); }, false },
	{ "tidy", [](CircuitWindow* w) { if (w->canEdit()) w->tidy(0); else w->lockNudge(); }, false },
	{ "tidy-flow", [](CircuitWindow* w) { if (w->canEdit()) w->tidy(1); else w->lockNudge(); }, false },
	{ "tidy-keep", [](CircuitWindow* w) { w->endTidy(true); }, false },
	{ "tidy-revert", [](CircuitWindow* w) { w->endTidy(false); }, false },
	{ "tidy-switch", [](CircuitWindow* w) { w->switchTidyMode(); }, false },
	{ "connect-nearby", [](CircuitWindow* w) { if (w->canEdit()) w->connectNearby(false); else w->lockNudge(); }, false },
	{ "gate-settings", [](CircuitWindow* w) { w->showSettings(); }, false },
	{ "zoom-in", [](CircuitWindow* w) { if (Canvas* c = w->currentCanvas()) c->animateZoom(1 / 0.75); }, false },
	{ "zoom-out", [](CircuitWindow* w) { if (Canvas* c = w->currentCanvas()) c->animateZoom(0.75); }, false },
	{ "zoom-fit", [](CircuitWindow* w) { if (Canvas* c = w->currentCanvas()) c->zoomToFit(true); }, false },
	{ "zoom-actual", [](CircuitWindow* w) { if (Canvas* c = w->currentCanvas()) c->zoomActual(); }, false },
	{ "dark", [](CircuitWindow* w) { w->toggleDark(); }, true },
	{ "palette", [](CircuitWindow* w) { w->togglePalette(); }, true },
	{ "status-bar", [](CircuitWindow*) {
		prefs().showStatus = !prefs().showStatus;
		prefs().save();
		for (CircuitWindow* o : circuitWindows()) o->prefsChanged();
	}, true },
	{ "preferences", [](CircuitWindow* w) { w->showPreferences(); }, false },
	{ "running", [](CircuitWindow* w) { w->toggleRunning(); }, true },
	{ "step", [](CircuitWindow* w) { w->stepOnce(); }, false },
	{ "sim-view", [](CircuitWindow* w) { w->toggleSimView(); }, true },
	{ "lock", [](CircuitWindow* w) { w->toggleLock(); }, true },
	{ "truth-table", [](CircuitWindow* w) { w->makeTruthTable(); }, false },
	{ "scope", [](CircuitWindow* w) { w->toggleScope(); }, false },
	{ "new-tab", [](CircuitWindow* w) { w->newPage(); }, false },
	{ "close-tab", [](CircuitWindow* w) {
		if (cl_document_page_count(w->document()) > 1) w->closePage(w->currentPage());
		else gtk_window_close(w->window());
	}, false },
	{ "reopen-tab", [](CircuitWindow* w) { w->reopenPage(); }, false },
	{ "rename-tab", [](CircuitWindow* w) { w->renamePage(w->currentPage()); }, false },
	{ "next-tab", [](CircuitWindow* w) { w->cyclePage(1); }, false },
	{ "previous-tab", [](CircuitWindow* w) { w->cyclePage(-1); }, false },
	{ "shortcuts", [](CircuitWindow* w) { w->showShortcuts(); }, false },
	{ "help", [](CircuitWindow* w) { w->showHelp(); }, false },
	{ "about", [](CircuitWindow* w) { w->showAbout(); }, false },
};

struct Binding {
	CircuitWindow* window;
	const Command* command;
};

void runCommandCb(GSimpleAction*, GVariant*, gpointer data) {
	Binding* b = static_cast<Binding*>(data);
	b->command->run(b->window);
}

GSimpleAction* findAction(GtkWidget* win, const char* name) {
	GAction* a = g_action_map_lookup_action(G_ACTION_MAP(win), name);
	return a ? G_SIMPLE_ACTION(a) : nullptr;
}

void setEnabled(GtkWidget* win, const char* name, bool on) {
	if (GSimpleAction* a = findAction(win, name)) g_simple_action_set_enabled(a, on);
}

void setChecked(GtkWidget* win, const char* name, bool on) {
	GSimpleAction* a = findAction(win, name);
	if (a == nullptr) return;
	GVariant* s = g_action_get_state(G_ACTION(a));
	const bool now = s && g_variant_get_boolean(s);
	if (s) g_variant_unref(s);
	if (now != on) g_simple_action_set_state(a, g_variant_new_boolean(on));
}

}  // namespace

// ---- Building the window -----------------------------------------------------------

CircuitWindow::CircuitWindow(GtkApplication* application, CLDocument* d, const std::string& p)
	: app(application), doc(d), path(p) {
	isRunning = cl_document_is_running(doc);
	build();
	circuitWindows().push_back(this);
	syncTabs();
	appearStart = g_get_monotonic_time();
	lastTick = g_get_monotonic_time();
	timer = g_timeout_add(16, tickCb, this);
	updateTitle();
	updateActions();
	updateRunUI();
	updateBanner();
	gtk_widget_show_all(win);
	gtk_widget_set_visible(paletteBox, prefs().showPalette);
	gtk_widget_set_visible(statusBar, prefs().showStatus);
	updateBanner();
	if (Canvas* c = currentCanvas()) gtk_widget_grab_focus(c->widget());
}

CircuitWindow::~CircuitWindow() {
	if (timer) g_source_remove(timer);
	timer = 0;
	delete scope;
	scope = nullptr;
	for (Canvas* c : canvases) delete c;
	canvases.clear();
	delete palette;
	std::vector<CircuitWindow*>& all = circuitWindows();
	all.erase(std::remove(all.begin(), all.end(), this), all.end());
	if (doc) cl_document_close(doc);
}

void CircuitWindow::build() {
	win = gtk_application_window_new(app);
	gtk_window_set_default_size(GTK_WINDOW(win), prefs().windowWidth, prefs().windowHeight);
	if (prefs().windowMaximized) gtk_window_maximize(GTK_WINDOW(win));
	gtk_window_set_icon_name(GTK_WINDOW(win), "cedarlogic");
	g_signal_connect(win, "delete-event", G_CALLBACK(deleteCb), this);
	g_signal_connect(win, "destroy", G_CALLBACK(destroyCb), this);
	g_signal_connect(win, "key-press-event", G_CALLBACK(keyCb), this);
	g_signal_connect(win, "window-state-event", G_CALLBACK(stateCb), this);
	g_signal_connect(win, "size-allocate", G_CALLBACK(sizeCb), this);
	addActions();

	GtkWidget* v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(win), v);
	gtk_box_pack_start(GTK_BOX(v), buildToolbar(), FALSE, FALSE, 0);

	// A bar for Tidy Up's preview, Simulation View and Lock.
	banner = gtk_info_bar_new();
	gtk_info_bar_set_message_type(GTK_INFO_BAR(banner), GTK_MESSAGE_INFO);
	bannerLabel = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(bannerLabel), 0);
	gtk_label_set_line_wrap(GTK_LABEL(bannerLabel), TRUE);
	gtk_container_add(GTK_CONTAINER(gtk_info_bar_get_content_area(GTK_INFO_BAR(banner))), bannerLabel);
	bannerButtons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_container_add(GTK_CONTAINER(gtk_info_bar_get_content_area(GTK_INFO_BAR(banner))), bannerButtons);
	gtk_box_pack_start(GTK_BOX(v), banner, FALSE, FALSE, 0);

	paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_box_pack_start(GTK_BOX(v), paned, TRUE, TRUE, 0);
	palette = new GatePalette(this);
	paletteBox = palette->widget();
	gtk_widget_set_size_request(paletteBox, 120, -1);
	gtk_paned_pack1(GTK_PANED(paned), paletteBox, FALSE, FALSE);
	gtk_paned_set_position(GTK_PANED(paned), prefs().paletteWidth);

	notebook = gtk_notebook_new();
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), TRUE);
	gtk_notebook_set_show_border(GTK_NOTEBOOK(notebook), FALSE);
	g_signal_connect(notebook, "switch-page", G_CALLBACK(switchPageCb), this);
	g_signal_connect(notebook, "page-reordered", G_CALLBACK(reorderCb), this);
	// A + at the end of the tabs for a new page.
	GtkWidget* plus = gtk_button_new_from_icon_name("list-add-symbolic", GTK_ICON_SIZE_MENU);
	gtk_button_set_relief(GTK_BUTTON(plus), GTK_RELIEF_NONE);
	gtk_widget_set_tooltip_text(plus, "New tab (Ctrl+T)");
	gtk_actionable_set_action_name(GTK_ACTIONABLE(plus), "win.new-tab");
	gtk_widget_show(plus);
	gtk_notebook_set_action_widget(GTK_NOTEBOOK(notebook), plus, GTK_PACK_END);
	gtk_paned_pack2(GTK_PANED(paned), notebook, TRUE, FALSE);

	statusBar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_widget_set_name(statusBar, "status");
	gtk_container_set_border_width(GTK_CONTAINER(statusBar), 3);
	statusMessage = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(statusMessage), 0);
	gtk_label_set_ellipsize(GTK_LABEL(statusMessage), PANGO_ELLIPSIZE_END);
	statusInfo = gtk_label_new("");
	gtk_style_context_add_class(gtk_widget_get_style_context(statusInfo), "dim-label");
	gtk_box_pack_start(GTK_BOX(statusBar), statusMessage, TRUE, TRUE, 6);
	gtk_box_pack_end(GTK_BOX(statusBar), statusInfo, FALSE, FALSE, 6);
	gtk_box_pack_start(GTK_BOX(v), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(v), statusBar, FALSE, FALSE, 0);
}

void CircuitWindow::addActions() {
	for (const Command& c : kCommands) {
		GSimpleAction* a = c.toggle ? g_simple_action_new_stateful(c.name, nullptr, g_variant_new_boolean(FALSE))
		                            : g_simple_action_new(c.name, nullptr);
		Binding* b = new Binding{ this, &c };
		g_signal_connect_data(a, "activate", G_CALLBACK(runCommandCb), b,
		                      [](gpointer data, GClosure*) { delete static_cast<Binding*>(data); }, (GConnectFlags)0);
		g_action_map_add_action(G_ACTION_MAP(win), G_ACTION(a));
		g_object_unref(a);
	}
}

static GtkToolItem* toolButton(const char* icon, const char* tip, const char* action) {
	GtkToolItem* b = gtk_tool_button_new(nullptr, tip);
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(b), icon);
	gtk_tool_item_set_tooltip_text(b, tip);
	gtk_actionable_set_action_name(GTK_ACTIONABLE(b), action);
	return b;
}

static GtkToolItem* toggleButton(const char* icon, const char* label, const char* tip, const char* action) {
	GtkToolItem* b = gtk_toggle_tool_button_new();
	gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(b), icon);
	gtk_tool_button_set_label(GTK_TOOL_BUTTON(b), label);
	gtk_tool_item_set_tooltip_text(b, tip);
	gtk_actionable_set_action_name(GTK_ACTIONABLE(b), action);
	return b;
}

static void stepSpinCb(GtkSpinButton* s, gpointer self) {
	static_cast<CircuitWindow*>(self)->setStepMs(gtk_spin_button_get_value_as_int(s));
}

GtkWidget* CircuitWindow::buildToolbar() {
	GtkWidget* bar = gtk_toolbar_new();
	gtk_toolbar_set_style(GTK_TOOLBAR(bar), GTK_TOOLBAR_ICONS);
	gtk_toolbar_set_icon_size(GTK_TOOLBAR(bar), GTK_ICON_SIZE_LARGE_TOOLBAR);
	GtkToolbar* t = GTK_TOOLBAR(bar);
	gtk_toolbar_insert(t, toolButton("document-new", "New circuit (Ctrl+N)", "app.new"), -1);
	gtk_toolbar_insert(t, toolButton("document-open", "Open (Ctrl+O)", "app.open"), -1);
	gtk_toolbar_insert(t, toolButton("document-save", "Save (Ctrl+S)", "win.save"), -1);
	gtk_toolbar_insert(t, gtk_separator_tool_item_new(), -1);
	gtk_toolbar_insert(t, toolButton("edit-undo", "Undo (Ctrl+Z)", "win.undo"), -1);
	gtk_toolbar_insert(t, toolButton("edit-redo", "Redo (Ctrl+Shift+Z)", "win.redo"), -1);
	gtk_toolbar_insert(t, gtk_separator_tool_item_new(), -1);
	gtk_toolbar_insert(t, toolButton("zoom-in", "Zoom in (Ctrl+=)", "win.zoom-in"), -1);
	gtk_toolbar_insert(t, toolButton("zoom-out", "Zoom out (Ctrl+-)", "win.zoom-out"), -1);
	gtk_toolbar_insert(t, toolButton("zoom-fit-best", "Zoom to fit (Ctrl+0, or tap Space)", "win.zoom-fit"), -1);
	gtk_toolbar_insert(t, gtk_separator_tool_item_new(), -1);

	GtkToolItem* run = toggleButton("media-playback-start", "Run", "Run or pause the simulation", "win.running");
	runButton = GTK_WIDGET(run);
	gtk_toolbar_insert(t, run, -1);
	gtk_toolbar_insert(t, toolButton("media-skip-forward", "Step once (Ctrl+Shift+R)", "win.step"), -1);

	// The step length, as the wx app's timestep box.
	GtkToolItem* speedItem = gtk_tool_item_new();
	GtkWidget* speed = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	GtkWidget* speedLabel = gtk_label_new("Step");
	stepSpin = gtk_spin_button_new_with_range(1, 500, 1);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(stepSpin), cl_document_step_ms(doc));
	gtk_entry_set_width_chars(GTK_ENTRY(stepSpin), 4);
	gtk_widget_set_tooltip_text(stepSpin, "Milliseconds of circuit time per simulation step");
	g_signal_connect(stepSpin, "value-changed", G_CALLBACK(stepSpinCb), this);
	gtk_box_pack_start(GTK_BOX(speed), speedLabel, FALSE, FALSE, 4);
	gtk_box_pack_start(GTK_BOX(speed), stepSpin, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(speed), gtk_label_new("ms"), FALSE, FALSE, 0);
	gtk_widget_set_valign(speed, GTK_ALIGN_CENTER);
	gtk_container_add(GTK_CONTAINER(speedItem), speed);
	gtk_toolbar_insert(t, speedItem, -1);
	gtk_toolbar_insert(t, gtk_separator_tool_item_new(), -1);

	GtkToolItem* sv = toggleButton("video-display", "Simulation View", "Simulation View: watch it run (Ctrl+R)", "win.sim-view");
	simViewButton = GTK_WIDGET(sv);
	gtk_toolbar_insert(t, sv, -1);
	gtk_toolbar_insert(t, toggleButton("changes-prevent", "Lock", "Lock: switches still work, nothing else changes", "win.lock"), -1);

	GtkToolItem* spacer = gtk_separator_tool_item_new();
	gtk_separator_tool_item_set_draw(GTK_SEPARATOR_TOOL_ITEM(spacer), FALSE);
	gtk_tool_item_set_expand(spacer, TRUE);
	gtk_toolbar_insert(t, spacer, -1);
	gtk_toolbar_insert(t, toggleButton("weather-clear-night", "Dark", "Dark mode (Ctrl+Shift+D)", "win.dark"), -1);
	return bar;
}

// ---- Tabs ------------------------------------------------------------------------

std::string CircuitWindow::pageName(int page) const {
	const char* n = cl_document_page_name(doc, page);
	if (n && *n) return n;
	return format("Page %d", page + 1);
}

// The window a canvas belongs to (set when the canvas is made).
static CircuitWindow* windowOf(Canvas* c) {
	return static_cast<CircuitWindow*>(g_object_get_data(G_OBJECT(c->widget()), "cl-window"));
}

static gboolean tabPressCb(GtkWidget*, GdkEventButton* e, gpointer data) {
	Canvas* c = static_cast<Canvas*>(data);
	CircuitWindow* w = windowOf(c);
	const int p = c->page();
	if (w == nullptr || p < 0) return FALSE;
	if (e->type == GDK_2BUTTON_PRESS && e->button == 1) { w->renamePage(p); return TRUE; }
	// A middle click closes the tab, as in browsers.
	if (e->type == GDK_BUTTON_PRESS && e->button == 2) { w->closePage(p); return TRUE; }
	return FALSE;
}

static void tabCloseCb(GtkButton*, gpointer data) {
	Canvas* c = static_cast<Canvas*>(data);
	CircuitWindow* w = windowOf(c);
	const int p = c->page();
	if (w && p >= 0) w->closePage(p);
}

GtkWidget* CircuitWindow::tabLabel(Canvas* c) {
	GtkWidget* ev = gtk_event_box_new();
	gtk_event_box_set_visible_window(GTK_EVENT_BOX(ev), FALSE);
	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	GtkWidget* label = gtk_label_new("");
	GtkWidget* close = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
	gtk_button_set_relief(GTK_BUTTON(close), GTK_RELIEF_NONE);
	gtk_widget_set_focus_on_click(close, FALSE);
	gtk_widget_set_tooltip_text(close, "Close tab (Ctrl+W)");
	g_signal_connect(close, "clicked", G_CALLBACK(tabCloseCb), c);
	gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(box), close, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(ev), box);
	g_object_set_data(G_OBJECT(ev), "label", label);
	g_object_set_data(G_OBJECT(ev), "close", close);
	gtk_widget_set_tooltip_text(ev, "Double-click to rename; drag to reorder");
	g_signal_connect(ev, "button-press-event", G_CALLBACK(tabPressCb), c);
	gtk_widget_show_all(ev);
	return ev;
}

void CircuitWindow::updateTabLabels() {
	const int n = (int)canvases.size();
	for (int i = 0; i < n; i++) {
		GtkWidget* ev = gtk_notebook_get_tab_label(GTK_NOTEBOOK(notebook), canvases[i]->widget());
		if (ev == nullptr) continue;
		GtkWidget* label = GTK_WIDGET(g_object_get_data(G_OBJECT(ev), "label"));
		GtkWidget* close = GTK_WIDGET(g_object_get_data(G_OBJECT(ev), "close"));
		const int p = canvases[i]->page();
		if (label && p >= 0) gtk_label_set_text(GTK_LABEL(label), pageName(p).c_str());
		if (close) gtk_widget_set_visible(close, n > 1);
	}
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(notebook), TRUE);
}

// Make the tabs match the document's pages: after opening, a new page, a
// close, an undo that brings one back, a move.
void CircuitWindow::syncTabs() {
	syncing = true;
	const int n = cl_document_page_count(doc);
	std::vector<Canvas*> want;
	for (int i = 0; i < n; i++) {
		const uint64_t key = cl_document_page_id(doc, i);
		Canvas* found = nullptr;
		for (Canvas* c : canvases) if (c->pageKey() == key) found = c;
		if (found == nullptr) found = new Canvas(this, key);
		want.push_back(found);
	}
	for (Canvas* c : canvases) {
		if (std::find(want.begin(), want.end(), c) != want.end()) continue;
		const int i = gtk_notebook_page_num(GTK_NOTEBOOK(notebook), c->widget());
		if (i >= 0) gtk_notebook_remove_page(GTK_NOTEBOOK(notebook), i);
		delete c;
	}
	for (int i = 0; i < n; i++) {
		GtkWidget* w = want[i]->widget();
		const int at = gtk_notebook_page_num(GTK_NOTEBOOK(notebook), w);
		if (at < 0) {
			gtk_notebook_insert_page(GTK_NOTEBOOK(notebook), w, tabLabel(want[i]), i);
			gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(notebook), w, TRUE);
			gtk_widget_show(w);
		} else if (at != i) {
			gtk_notebook_reorder_child(GTK_NOTEBOOK(notebook), w, i);
		}
	}
	canvases = want;
	lastPageCount = n;
	updateTabLabels();
	syncing = false;
}

Canvas* CircuitWindow::currentCanvas() const {
	if (notebook == nullptr) return nullptr;
	const int i = gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook));
	GtkWidget* w = i >= 0 ? gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), i) : nullptr;
	for (Canvas* c : canvases) if (c->widget() == w) return c;
	return nullptr;
}

int CircuitWindow::currentPage() const {
	Canvas* c = currentCanvas();
	const int p = c ? c->page() : -1;
	return p >= 0 ? p : 0;
}

void CircuitWindow::switchPageCb(GtkNotebook*, GtkWidget* page, guint, gpointer self) {
	CircuitWindow* w = static_cast<CircuitWindow*>(self);
	if (w->syncing) return;
	// Leaving a page lets go of its selection, as the wx app does.
	if (Canvas* old = w->currentCanvas()) {
		if (old->widget() != page && old->page() >= 0) {
			old->cancelDrag();
			cl_edit_select_none(w->doc, old->page());
			old->dropBuffers();
		}
	}
	w->statusDirty = true;
	w->selectionSignature.clear();
	for (Canvas* c : w->canvases) {
		if (c->widget() != page) continue;
		gtk_widget_grab_focus(c->widget());
		if (!w->seenPages[c->pageKey()]) { w->seenPages[c->pageKey()] = true; w->appearStart = g_get_monotonic_time(); }
	}
	g_idle_add([](gpointer self) -> gboolean {
		for (CircuitWindow* o : circuitWindows())
			if (o == self) { o->updateActions(); o->updateTitle(); }
		return G_SOURCE_REMOVE;
	}, w);
}

void CircuitWindow::reorderCb(GtkNotebook*, GtkWidget* child, guint to, gpointer self) {
	CircuitWindow* w = static_cast<CircuitWindow*>(self);
	if (w->syncing) return;
	Canvas* moved = nullptr;
	for (Canvas* c : w->canvases) if (c->widget() == child) moved = c;
	if (moved == nullptr) return;
	const int from = moved->page();
	if (from < 0 || from == (int)to) return;
	// A tab without a name of its own is called by its place ("Page 2"): pin
	// those names first, so moving a tab doesn't rename the others.
	for (int i = 0; i < cl_document_page_count(w->doc); i++) {
		const char* n = cl_document_page_name(w->doc, i);
		if (n == nullptr || *n == 0) cl_document_rename_page(w->doc, i, w->pageName(i).c_str());
	}
	cl_document_move_page(w->doc, from, (int)to);
	w->syncTabs();
	w->updateTitle();
}

// ---- The clock -------------------------------------------------------------------

gboolean CircuitWindow::tickCb(gpointer self) {
	static_cast<CircuitWindow*>(self)->tick();
	return G_SOURCE_CONTINUE;
}

void CircuitWindow::tick() {
	const gint64 t = g_get_monotonic_time();
	const double elapsed = (t - lastTick) / 1000.0;   // ms
	lastTick = t;
	Canvas* c = currentCanvas();
	if (c) c->stepAnimation();
	// Fades in progress (a new selection's halo, a page appearing, the drag box).
	if (secondsSince(selectionChangedAt) < kSelectionFadeTime || secondsSince(appearStart) < kAppearTime ||
	    (hasDragFade && secondsSince(dragFadeStart) < kDragFadeTime))
		redraw();
	if (hasDragFade && secondsSince(dragFadeStart) >= kDragFadeTime) { hasDragFade = false; redraw(); }

	// Simulation View's dashes march at the simulation's speed: 40 points a
	// second at 25 ms a step, faster as steps get shorter.
	if (simViewOn && isRunning) {
		const int ms = std::max(1, cl_document_step_ms(doc));
		const double pps = std::min(240.0, std::max(8.0, 40 * std::sqrt(25.0 / ms)));
		phase += std::min(0.05, elapsed / 1000) * pps;
		redraw();
	}
	if (isRunning) {
		const int r = cl_document_tick(doc, elapsed);
		// Redraw only when a step changed something that shows: a running
		// circuit with nothing moving costs next to nothing.
		if (r & CL_TICK_SHOWN) redraw();
		// The oscilloscope's traces grow with every step, changes or not.
		if ((r & CL_TICK_CHANGED) && scope && scope->visible() && (t - lastScope) > 1000000 / 15) {
			lastScope = t;
			scope->update();
		}
		if (r & CL_TICK_PAUSED) { isRunning = false; updateRunUI(); note("A part paused the simulation."); }
	}
	if (statusDirty && (t - lastStatus) > 100000) { statusDirty = false; lastStatus = t; updateStatus(); }
	if ((t - lastTitle) > 500000) { lastTitle = t; updateTitle(); }
	if (messageAt && secondsSince(messageAt) > 5) { messageAt = 0; gtk_label_set_text(GTK_LABEL(statusMessage), ""); }
}

double CircuitWindow::selectionFade() const {
	return std::min(1.0, std::max(0.0, secondsSince(selectionChangedAt) / kSelectionFadeTime));
}

double CircuitWindow::appearProgress() const {
	const double t = std::min(1.0, std::max(0.0, secondsSince(appearStart) / kAppearTime));
	return 1 - std::pow(1 - t, 3);
}

bool CircuitWindow::dragFadeBox(double& l, double& b, double& r, double& t, double& alpha) const {
	if (!hasDragFade) return false;
	l = fadeL; b = fadeB; r = fadeR; t = fadeT;
	alpha = std::max(0.0, 1 - secondsSince(dragFadeStart) / kDragFadeTime);
	return true;
}

void CircuitWindow::fadeOutDragBox(double l, double b, double r, double t) {
	fadeL = l; fadeB = b; fadeR = r; fadeT = t;
	hasDragFade = true;
	dragFadeStart = g_get_monotonic_time();
}

// ---- State shown around the canvas -------------------------------------------------

void CircuitWindow::redraw() {
	if (Canvas* c = currentCanvas()) c->redraw();
}

void CircuitWindow::pointerMoved(double wx, double wy) {
	pointerX = wx;
	pointerY = wy;
	statusDirty = true;
}

void CircuitWindow::selectionChanged() {
	// A new selection's halo fades in; clicking what's already selected
	// doesn't restart it.
	const int p = currentPage();
	const std::string sig = format("%d/%d/%ld", cl_edit_selected_gate_count(doc, p),
	                               cl_edit_selected_wire_count(doc, p), cl_edit_single_gate(doc, p));
	if (sig != selectionSignature) {
		selectionSignature = sig;
		if (sig != "0/0/-1") selectionChangedAt = g_get_monotonic_time();
		updateActions();
	}
	statusDirty = true;
}

void CircuitWindow::edited() {
	if (cl_document_page_count(doc) != lastPageCount) syncTabs();
	redraw();
	selectionChanged();
	updateActions();
	updateTitle();
	updateBanner();
	updateTabLabels();
	statusDirty = true;
}

void CircuitWindow::note(const std::string& message) {
	gtk_label_set_text(GTK_LABEL(statusMessage), message.c_str());
	messageAt = g_get_monotonic_time();
}

void CircuitWindow::lockNudge() {
	if (simViewOn) note("Leave Simulation View (Escape) to edit.");
	else note("The circuit is locked. Unlock it (Simulate > Lock) to edit.");
	gtk_widget_error_bell(win);
}

std::string CircuitWindow::displayName() const { return path.empty() ? "Untitled" : baseName(path); }

bool CircuitWindow::isDirty() const { return forceDirty || cl_document_is_edited(doc); }

bool CircuitWindow::isPristine() const {
	if (!path.empty() || isDirty() || cl_document_page_count(doc) != 1) return false;
	return cl_document_gate_count(doc, 0) == 0 && !cl_edit_can_undo(doc);
}

void CircuitWindow::updateTitle() {
	std::string t = displayName();
	if (isDirty()) t = "*" + t;
	if (cl_document_page_count(doc) > 1) t += " - " + pageName(currentPage());
	t += " — " CL_APP_NAME;
	const char* now = gtk_window_get_title(GTK_WINDOW(win));
	if (now == nullptr || t != now) gtk_window_set_title(GTK_WINDOW(win), t.c_str());
}

void CircuitWindow::updateStatus() {
	Canvas* c = currentCanvas();
	const int p = currentPage();
	std::string s = format("%d gates", cl_document_gate_count(doc, p));
	const int sg = cl_edit_selected_gate_count(doc, p), sw = cl_edit_selected_wire_count(doc, p);
	if (sg + sw > 0) s += format(" · %d selected", sg + sw);
	if (c) s += format(" · %d%%", c->zoomPercent());
	s += format(" · %.1f, %.1f", pointerX, pointerY);
	s += isRunning ? " · Running" : " · Paused";
	gtk_label_set_text(GTK_LABEL(statusInfo), s.c_str());
}

bool CircuitWindow::hasSelection() const {
	const int p = currentPage();
	return cl_edit_selected_gate_count(doc, p) + cl_edit_selected_wire_count(doc, p) > 0;
}

void CircuitWindow::updateActions() {
	const bool edit = canEdit();
	const bool sel = hasSelection();
	setEnabled(win, "undo", cl_edit_can_undo(doc) && !simViewOn);
	setEnabled(win, "redo", cl_edit_can_redo(doc) && !simViewOn);
	setEnabled(win, "cut", edit && sel);
	setEnabled(win, "copy", sel);
	setEnabled(win, "duplicate", edit && sel);
	setEnabled(win, "delete", edit && sel);
	setEnabled(win, "rotate", edit && cl_edit_selected_gate_count(doc, currentPage()) > 0);
	setEnabled(win, "gate-settings", cl_edit_single_gate(doc, currentPage()) >= 0);
	setEnabled(win, "reopen-tab", cl_edit_undo_is_close_page(doc));
	setChecked(win, "running", isRunning);
	setChecked(win, "sim-view", simViewOn);
	setChecked(win, "lock", lockedOn);
	setChecked(win, "dark", prefs().dark);
	setChecked(win, "palette", prefs().showPalette);
	setChecked(win, "status-bar", prefs().showStatus);
	// The menus' Undo and Redo say what they undo.
	(void)edit;
}

void CircuitWindow::updateRunUI() {
	if (runButton) {
		gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(runButton),
		                              isRunning ? "media-playback-pause" : "media-playback-start");
		gtk_tool_button_set_label(GTK_TOOL_BUTTON(runButton), isRunning ? "Pause" : "Run");
		gtk_widget_set_tooltip_text(runButton, isRunning ? "Pause the simulation" : "Run the simulation");
	}
	setChecked(win, "running", isRunning);
	statusDirty = true;
}

static void bannerButton(GtkWidget* box, const char* label, const char* action) {
	GtkWidget* b = gtk_button_new_with_mnemonic(label);
	gtk_actionable_set_action_name(GTK_ACTIONABLE(b), action);
	gtk_box_pack_start(GTK_BOX(box), b, FALSE, FALSE, 0);
	gtk_widget_show(b);
}

void CircuitWindow::updateBanner() {
	GList* kids = gtk_container_get_children(GTK_CONTAINER(bannerButtons));
	for (GList* k = kids; k; k = k->next) gtk_widget_destroy(GTK_WIDGET(k->data));
	g_list_free(kids);
	std::string text;
	if (cl_edit_tidy_active(doc)) {
		text = cl_edit_tidy_mode(doc) == 1
			? "Tidy Up by signal flow: a preview. Return keeps it, Escape puts it back, Tab tries keeping the shape."
			: "Tidy Up: a preview. Return keeps it, Escape puts it back, Tab tries arranging by signal flow.";
		bannerButton(bannerButtons, "_Keep", "win.tidy-keep");
		bannerButton(bannerButtons, "_Put Back", "win.tidy-revert");
		bannerButton(bannerButtons, "_Other Way", "win.tidy-switch");
	} else if (simViewOn) {
		text = "Simulation View: switches and keypads still work. Space pauses; Escape goes back to editing.";
		bannerButton(bannerButtons, "_Edit", "win.sim-view");
	} else if (lockedOn) {
		text = "Locked: switches and keypads still work, but nothing can be moved or changed.";
		bannerButton(bannerButtons, "_Unlock", "win.lock");
	}
	gtk_label_set_text(GTK_LABEL(bannerLabel), text.c_str());
	gtk_widget_set_visible(banner, !text.empty());
	if (!text.empty()) gtk_widget_show_all(bannerButtons);
}

void CircuitWindow::themeChanged() {
	updateActions();
	if (palette) palette->themeChanged();
	for (Canvas* c : canvases) c->redraw();
	if (scope) scope->update();
}

void CircuitWindow::prefsChanged() {
	gtk_widget_set_visible(paletteBox, prefs().showPalette);
	gtk_widget_set_visible(statusBar, prefs().showStatus);
	themeChanged();
}

void CircuitWindow::showPaletteCategory(int index) {
	if (!prefs().showPalette) togglePalette();
	palette->showCategory(index);
}

// ---- Window events -------------------------------------------------------------

gboolean CircuitWindow::deleteCb(GtkWidget*, GdkEvent*, gpointer self) {
	return static_cast<CircuitWindow*>(self)->confirmClose() ? FALSE : TRUE;
}

void CircuitWindow::destroyCb(GtkWidget*, gpointer self) {
	CircuitWindow* w = static_cast<CircuitWindow*>(self);
	prefs().paletteWidth = gtk_paned_get_position(GTK_PANED(w->paned));
	prefs().save();
	delete w;
}

gboolean CircuitWindow::keyCb(GtkWidget* widget, GdkEventKey* e, gpointer self) {
	CircuitWindow* w = static_cast<CircuitWindow*>(self);
	// Text boxes get their keys before the menus' shortcuts, so Ctrl+C in
	// the palette's search copies text rather than gates.
	GtkWidget* focus = gtk_window_get_focus(GTK_WINDOW(widget));
	if (focus && (GTK_IS_EDITABLE(focus) || GTK_IS_TEXT_VIEW(focus))) {
		if (gtk_window_propagate_key_event(GTK_WINDOW(widget), e)) return TRUE;
	}
	// Ctrl+Tab and Ctrl+Shift+Tab go through the tabs (GTK would move the
	// keyboard focus instead).
	if ((e->state & GDK_CONTROL_MASK) && (e->keyval == GDK_KEY_Tab || e->keyval == GDK_KEY_ISO_Left_Tab ||
	                                      e->keyval == GDK_KEY_KP_Tab)) {
		w->cyclePage((e->keyval == GDK_KEY_ISO_Left_Tab || (e->state & GDK_SHIFT_MASK)) ? -1 : 1);
		return TRUE;
	}
	return FALSE;
}

gboolean CircuitWindow::stateCb(GtkWidget*, GdkEventWindowState* e, gpointer) {
	prefs().windowMaximized = (e->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) != 0;
	return FALSE;
}

void CircuitWindow::sizeCb(GtkWidget* widget, GdkRectangle*, gpointer) {
	if (prefs().windowMaximized) return;
	int w = 0, h = 0;
	gtk_window_get_size(GTK_WINDOW(widget), &w, &h);
	if (w > 0 && h > 0) { prefs().windowWidth = w; prefs().windowHeight = h; }
}

bool CircuitWindow::confirmClose() {
	if (!isDirty()) return true;
	GtkWidget* d = gtk_message_dialog_new(GTK_WINDOW(win), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
	                                      "Save the changes to “%s” before closing?", displayName().c_str());
	gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "If you don't save, your changes will be lost.");
	gtk_dialog_add_buttons(GTK_DIALOG(d), "Close _without Saving", GTK_RESPONSE_REJECT, "_Cancel", GTK_RESPONSE_CANCEL,
	                       "_Save", GTK_RESPONSE_ACCEPT, nullptr);
	gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_ACCEPT);
	const int r = gtk_dialog_run(GTK_DIALOG(d));
	gtk_widget_destroy(d);
	if (r == GTK_RESPONSE_REJECT) return true;
	if (r == GTK_RESPONSE_ACCEPT) return save();
	return false;
}

// ---- Files ---------------------------------------------------------------------

void CircuitWindow::replaceDocument(CLDocument* newDoc, const std::string& newPath) {
	for (Canvas* c : canvases) c->cancelDrag();
	syncing = true;
	while (gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook)) > 0) gtk_notebook_remove_page(GTK_NOTEBOOK(notebook), 0);
	for (Canvas* c : canvases) delete c;
	canvases.clear();
	syncing = false;
	if (scope) { delete scope; scope = nullptr; }
	cl_document_close(doc);
	doc = newDoc;
	path = newPath;
	forceDirty = false;
	pendingGate.clear();
	selectionSignature.clear();
	seenPages.clear();
	isRunning = cl_document_is_running(doc);
	simViewOn = false;
	lockedOn = false;
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(stepSpin), cl_document_step_ms(doc));
	syncTabs();
	appearStart = g_get_monotonic_time();
	updateTitle();
	updateActions();
	updateRunUI();
	updateBanner();
	if (Canvas* c = currentCanvas()) gtk_widget_grab_focus(c->widget());
}

bool CircuitWindow::writeTo(const std::string& file) {
	const std::string text = cl_document_save_text(doc);   // marks the engine's copy saved
	GError* e = nullptr;
	if (!g_file_set_contents(file.c_str(), text.data(), (gssize)text.size(), &e)) {
		forceDirty = true;
		showMessage(GTK_WINDOW(win), GTK_MESSAGE_ERROR, "The circuit couldn't be saved",
		            e ? e->message : "The file couldn't be written.");
		if (e) g_error_free(e);
		updateTitle();
		return false;
	}
	forceDirty = false;
	path = file;
	prefs().noteRecent(file);
	updateTitle();
	note("Saved.");
	return true;
}

bool CircuitWindow::save() {
	if (path.empty()) return saveAs();
	return writeTo(path);
}

static GtkFileFilter* cdlFilter() {
	GtkFileFilter* f = gtk_file_filter_new();
	gtk_file_filter_set_name(f, "CedarLogic circuits (*.cdl)");
	gtk_file_filter_add_pattern(f, "*.cdl");
	gtk_file_filter_add_pattern(f, "*.CDL");
	return f;
}

static std::string chooseSavePath(GtkWindow* parent, const char* title, const std::string& suggested,
                                  GtkFileFilter* filter, const char* ext) {
	GtkFileChooserNative* chooser = gtk_file_chooser_native_new(title, parent, GTK_FILE_CHOOSER_ACTION_SAVE,
	                                                            "_Save", "_Cancel");
	GtkFileChooser* fc = GTK_FILE_CHOOSER(chooser);
	gtk_file_chooser_set_do_overwrite_confirmation(fc, TRUE);
	if (filter) gtk_file_chooser_add_filter(fc, filter);
	if (!prefs().lastFolder.empty()) gtk_file_chooser_set_current_folder(fc, prefs().lastFolder.c_str());
	gtk_file_chooser_set_current_name(fc, suggested.c_str());
	std::string out;
	if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
		if (gchar* f = gtk_file_chooser_get_filename(fc)) { out = f; g_free(f); }
	}
	g_object_unref(chooser);
	if (!out.empty() && ext) {
		// Add the extension when it was left off.
		const size_t n = strlen(ext);
		gchar* base = g_path_get_basename(out.c_str());
		const bool hasDot = strchr(base, '.') != nullptr;
		g_free(base);
		if (!hasDot && (out.size() < n || g_ascii_strcasecmp(out.c_str() + out.size() - n, ext) != 0)) out += ext;
	}
	return out;
}

bool CircuitWindow::saveAs() {
	const std::string file = chooseSavePath(GTK_WINDOW(win), "Save Circuit", displayName() + ".cdl", cdlFilter(), ".cdl");
	if (file.empty()) return false;
	return writeTo(file);
}

void CircuitWindow::open() { chooseAndOpen(app, GTK_WINDOW(win)); }
void CircuitWindow::newCircuit() { newCircuitWindow(app); }

void CircuitWindow::exportOlder(int format) {
	const std::string suggested = displayName() + (format == 1 ? " (v1.x).cdl" : " (v2).cdl");
	const std::string file = chooseSavePath(GTK_WINDOW(win),
		format == 1 ? "Export for CedarLogic 1.x" : "Export for CedarLogic 2", suggested, cdlFilter(), ".cdl");
	if (file.empty()) return;
	char why[512] = "";
	const int rc = cl_document_export_legacy(doc, file.c_str(), format, why, sizeof why);
	if (rc == 0) { note("Exported."); return; }
	showMessage(GTK_WINDOW(win), rc > 0 ? GTK_MESSAGE_INFO : GTK_MESSAGE_WARNING,
	            rc > 0 ? "Exported, with one thing left out" : "The circuit couldn't be exported", why);
}

// Export the page in front as a picture: PNG, or PDF or SVG (which stay
// sharp at any size), in the style chosen.
void CircuitWindow::exportImage() {
	const int p = currentPage();
	double l, b, r, t;
	if (!cl_document_page_bounds(doc, p, &l, &b, &r, &t)) { note("This page is empty: nothing to export."); return; }
	GtkWidget* d = gtk_file_chooser_dialog_new("Export as Image", GTK_WINDOW(win), GTK_FILE_CHOOSER_ACTION_SAVE,
	                                           "_Cancel", GTK_RESPONSE_CANCEL, "_Export", GTK_RESPONSE_ACCEPT, nullptr);
	GtkFileChooser* fc = GTK_FILE_CHOOSER(d);
	gtk_file_chooser_set_do_overwrite_confirmation(fc, TRUE);
	if (!prefs().lastFolder.empty()) gtk_file_chooser_set_current_folder(fc, prefs().lastFolder.c_str());
	std::string name = displayName();
	if (cl_document_page_count(doc) > 1) name += " - " + pageName(p);
	gtk_file_chooser_set_current_name(fc, (name + ".png").c_str());
	GtkWidget* extra = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_pack_start(GTK_BOX(extra), gtk_label_new("Style:"), FALSE, FALSE, 0);
	GtkWidget* style = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(style), "Black on white, for printing");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(style), "Light, with signal colours");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(style), "Dark, with signal colours");
	gtk_combo_box_set_active(GTK_COMBO_BOX(style), 1);
	gtk_box_pack_start(GTK_BOX(extra), style, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(extra), gtk_label_new("Type .png, .pdf or .svg at the end of the name."), FALSE, FALSE, 8);
	gtk_widget_show_all(extra);
	gtk_file_chooser_set_extra_widget(fc, extra);
	std::string file;
	int choice = 1;
	if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT) {
		if (gchar* f = gtk_file_chooser_get_filename(fc)) { file = f; g_free(f); }
		choice = gtk_combo_box_get_active(GTK_COMBO_BOX(style));
	}
	gtk_widget_destroy(d);
	if (file.empty()) return;
	const int clStyle = choice == 0 ? CL_STYLE_PRINT : choice == 2 ? CL_STYLE_DARK : CL_STYLE_LIGHT;
	gchar* lowerName = g_ascii_strdown(file.c_str(), -1);
	const std::string lf = lowerName;
	g_free(lowerName);
	auto endsWith = [&](const char* s) { const size_t n = strlen(s); return lf.size() >= n && lf.compare(lf.size() - n, n, s) == 0; };
	if (!endsWith(".png") && !endsWith(".pdf") && !endsWith(".svg")) file += ".png";
	const bool vector = endsWith(".pdf") || endsWith(".svg");

	// Ten points a grid unit for vector files; two pixels a point for PNG.
	const double margin = 16;
	const double wPts = std::min(4000.0, std::max(300.0, (r - l) * 10 + 2 * margin));
	const double hPts = std::min(4000.0, std::max(200.0, (t - b) * 10 + 2 * margin));
	cairo_surface_t* s;
	double scale = 1;
	if (endsWith(".pdf")) s = cairo_pdf_surface_create(file.c_str(), wPts, hPts);
	else if (endsWith(".svg")) s = cairo_svg_surface_create(file.c_str(), wPts, hPts);
	else { scale = 2; s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)(wPts * scale), (int)(hPts * scale)); }
	cairo_t* cr = cairo_create(s);
	cairo_scale(cr, scale, scale);
	if (clStyle == CL_STYLE_DARK) {
		const RGBA bg = Palette{ true, false }.canvas();
		cairo_set_source_rgb(cr, bg.r, bg.g, bg.b);
	} else {
		cairo_set_source_rgb(cr, 1, 1, 1);
	}
	cairo_paint(cr);
	cl_document_draw_fitted(doc, p, cr, wPts, hPts, margin, scale, clStyle);
	cairo_destroy(cr);
	cairo_status_t st = CAIRO_STATUS_SUCCESS;
	if (!vector) st = cairo_surface_write_to_png(s, file.c_str());
	cairo_surface_finish(s);
	if (st == CAIRO_STATUS_SUCCESS) st = cairo_surface_status(s);
	cairo_surface_destroy(s);
	if (st != CAIRO_STATUS_SUCCESS) {
		showMessage(GTK_WINDOW(win), GTK_MESSAGE_ERROR, "The image couldn't be saved", cairo_status_to_string(st));
		return;
	}
	gchar* dir = g_path_get_dirname(file.c_str());
	prefs().lastFolder = dir;
	g_free(dir);
	note("Exported " + baseName(file) + ".");
}

struct PrintJob { CLDocument* doc; int page; };

static void drawPrintPageCb(GtkPrintOperation*, GtkPrintContext* ctx, gint, gpointer data) {
	PrintJob* j = static_cast<PrintJob*>(data);
	cairo_t* cr = gtk_print_context_get_cairo_context(ctx);
	cl_document_draw_fitted(j->doc, j->page, cr, gtk_print_context_get_width(ctx),
	                        gtk_print_context_get_height(ctx), 12, 1, CL_STYLE_PRINT);
}

void CircuitWindow::print() {
	PrintJob job{ doc, currentPage() };
	GtkPrintOperation* op = gtk_print_operation_new();
	gtk_print_operation_set_n_pages(op, 1);
	gtk_print_operation_set_job_name(op, displayName().c_str());
	double l, b, r, t;
	if (cl_document_page_bounds(doc, job.page, &l, &b, &r, &t) && (r - l) > (t - b)) {
		GtkPageSetup* setup = gtk_page_setup_new();
		gtk_page_setup_set_orientation(setup, GTK_PAGE_ORIENTATION_LANDSCAPE);
		gtk_print_operation_set_default_page_setup(op, setup);
		g_object_unref(setup);
	}
	g_signal_connect(op, "draw-page", G_CALLBACK(drawPrintPageCb), &job);
	GError* e = nullptr;
	const GtkPrintOperationResult res = gtk_print_operation_run(op, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
	                                                            GTK_WINDOW(win), &e);
	if (res == GTK_PRINT_OPERATION_RESULT_ERROR) {
		showMessage(GTK_WINDOW(win), GTK_MESSAGE_ERROR, "The page couldn't be printed", e ? e->message : "");
		if (e) g_error_free(e);
	}
	g_object_unref(op);
}

// ---- Editing ---------------------------------------------------------------------

void CircuitWindow::refreshAfterHistory() {
	// An undo or redo that closed or reopened a page shows that page.
	if (cl_document_page_count(doc) != lastPageCount) {
		const int show = cl_document_page_to_show(doc);
		syncTabs();
		if (show >= 0 && show < (int)canvases.size()) {
			gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), show);
			appearStart = g_get_monotonic_time();
		}
	}
	edited();
}

void CircuitWindow::undo() {
	if (simViewOn) return;
	if (Canvas* c = currentCanvas()) if (c->isDragging()) c->cancelDrag();
	if (cl_edit_undo(doc)) refreshAfterHistory();
	else gtk_widget_error_bell(win);
}

void CircuitWindow::redo() {
	if (simViewOn) return;
	if (Canvas* c = currentCanvas()) if (c->isDragging()) c->cancelDrag();
	if (cl_edit_redo(doc)) refreshAfterHistory();
	else gtk_widget_error_bell(win);
}

void CircuitWindow::copy() {
	const std::string text = cl_edit_copy(doc, currentPage());
	if (text.empty()) { note("Nothing is selected to copy."); return; }
	gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text.c_str(), (gint)text.size());
	note("Copied.");
}

void CircuitWindow::cut() {
	const std::string text = cl_edit_copy(doc, currentPage());
	if (text.empty()) return;
	gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text.c_str(), (gint)text.size());
	deleteSelection();
}

bool CircuitWindow::placePoint(double& wx, double& wy) const {
	Canvas* c = currentCanvas();
	if (c == nullptr) return false;
	if (!c->pointerWorld(wx, wy)) c->center(wx, wy);
	return true;
}

void CircuitWindow::floatSelection(double wx, double wy) {
	if (cl_edit_float_begin(doc, currentPage(), wx, wy)) {
		if (Canvas* c = currentCanvas()) gtk_widget_grab_focus(c->widget());
	}
	edited();
}

bool CircuitWindow::isFloating() const { return cl_edit_is_floating(doc); }

void CircuitWindow::cancelFloating() {
	if (!isFloating()) return;
	// It was never placed, so take it back.
	cl_edit_cancel(doc);
	cl_edit_undo(doc);
	edited();
}

struct PasteRequest { CircuitWindow* window; bool shift; };

void CircuitWindow::clipboardCb(GtkClipboard*, const gchar* text, gpointer data) {
	std::unique_ptr<PasteRequest> req(static_cast<PasteRequest*>(data));
	std::vector<CircuitWindow*>& all = circuitWindows();
	if (std::find(all.begin(), all.end(), req->window) == all.end()) return;   // closed meanwhile
	if (text == nullptr) { req->window->note("Nothing to paste."); return; }
	req->window->pasteText(text, true, req->shift);
}

void CircuitWindow::paste() {
	if (!canEdit()) { lockNudge(); return; }
	GdkKeymap* keymap = gdk_keymap_get_for_display(gtk_widget_get_display(win));
	const bool shift = (gdk_keymap_get_modifier_state(keymap) & GDK_SHIFT_MASK) != 0;
	gtk_clipboard_request_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), clipboardCb, new PasteRequest{ this, shift });
}

void CircuitWindow::pasteText(const std::string& text, bool floating, bool shift) {
	double wx, wy;
	if (!canEdit() || !placePoint(wx, wy)) return;
	const char* back = nullptr;
	if (!cl_edit_paste(doc, currentPage(), text.c_str(), wx, wy, shift, &back)) { note("Nothing to paste."); return; }
	if (back && *back) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), back, -1);
	if (floating) floatSelection(wx, wy);
	else edited();
}

void CircuitWindow::duplicate() {
	double wx, wy;
	if (!canEdit() || !placePoint(wx, wy)) return;
	const std::string text = cl_edit_copy(doc, currentPage());
	if (text.empty()) return;
	if (prefs().duplicateUsesClipboard)
		gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text.c_str(), (gint)text.size());
	const char* back = nullptr;
	cl_edit_paste(doc, currentPage(), text.c_str(), wx, wy, false, &back);
	floatSelection(wx, wy);
}

void CircuitWindow::selectAll() { cl_edit_select_all(doc, currentPage()); redraw(); selectionChanged(); }
void CircuitWindow::selectNone() { cl_edit_select_none(doc, currentPage()); redraw(); selectionChanged(); }
void CircuitWindow::deleteSelection() { cl_edit_delete(doc, currentPage()); edited(); }
void CircuitWindow::rotate() { cl_edit_rotate(doc, currentPage()); edited(); }
void CircuitWindow::straighten() { cl_edit_straighten(doc, currentPage()); edited(); }
void CircuitWindow::nudge(double dx, double dy) { cl_edit_nudge(doc, currentPage(), dx, dy); edited(); }

void CircuitWindow::tidy(int mode) {
	if (!cl_edit_tidy_begin(doc, currentPage(), mode < 0 ? prefs().tidyMode : mode)) note("Nothing to tidy on this page.");
	edited();
}

void CircuitWindow::endTidy(bool keep) { cl_edit_tidy_end(doc, keep); edited(); }

void CircuitWindow::switchTidyMode() {
	const int other = 1 - cl_edit_tidy_mode(doc);
	cl_edit_tidy_end(doc, false);
	cl_edit_tidy_begin(doc, currentPage(), other);
	edited();
}

bool CircuitWindow::tidyActive() const { return cl_edit_tidy_active(doc); }

void CircuitWindow::connectNearby(bool quietly) {
	Canvas* c = currentCanvas();
	if (!canEdit() || c == nullptr) return;
	const int n = cl_edit_connect_nearby(doc, currentPage(), c->unitsPerPoint());
	if (n > 0) note(format("Connected %d pin%s.", n, n == 1 ? "" : "s"));
	else if (!quietly) note("Nothing close enough to connect.");
	edited();
}

void CircuitWindow::quickAdd() { showQuickAdd(this); }

void CircuitWindow::addGateOnNextMove(const std::string& name) {
	if (!canEdit()) { lockNudge(); return; }
	pendingGate = name;
	note("Move onto the canvas: the gate follows the pointer until you click to drop it.");
	if (Canvas* c = currentCanvas()) {
		gtk_widget_grab_focus(c->widget());
		// Already over the canvas (the keyboard chose it): there at once.
		double wx, wy;
		if (c->pointerWorld(wx, wy)) placePendingGate(wx, wy);
	}
}

bool CircuitWindow::placePendingGate(double wx, double wy) {
	if (pendingGate.empty()) return false;
	const std::string name = pendingGate;
	pendingGate.clear();
	return addGateFloating(name, wx, wy);
}

bool CircuitWindow::addGateFloating(const std::string& name, double wx, double wy) {
	if (!canEdit()) { lockNudge(); return false; }
	if (!cl_edit_add_gate(doc, currentPage(), name.c_str(), wx, wy)) return false;
	floatSelection(wx, wy);
	redraw();
	return true;
}

void CircuitWindow::showSettings() {
	const long g = cl_edit_single_gate(doc, currentPage());
	if (g < 0) return;
	int addressBits = 0, dataBits = 0;
	if (cl_ram_info(doc, g, &addressBits, &dataBits)) showRamEditor(this, g);
	else showGateSettings(this, g);
}

void CircuitWindow::showRam(long gate) { showRamEditor(this, gate); }

// ---- The right-click menu ------------------------------------------------------

struct MenuPoint { CircuitWindow* w; double x, y; };

static void menuItem(GtkWidget* menu, const char* label, const char* action) {
	GtkWidget* item = gtk_menu_item_new_with_mnemonic(label);
	gtk_actionable_set_action_name(GTK_ACTIONABLE(item), action);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}

static void disconnectCb(GtkMenuItem*, gpointer data) {
	MenuPoint* mp = static_cast<MenuPoint*>(data);
	std::vector<CircuitWindow*>& all = circuitWindows();
	if (std::find(all.begin(), all.end(), mp->w) == all.end()) return;
	Canvas* c = mp->w->currentCanvas();
	if (c == nullptr) return;
	cl_edit_disconnect_pin(mp->w->document(), mp->w->currentPage(), mp->x, mp->y, c->unitsPerPoint());
	mp->w->edited();
}

void CircuitWindow::showContextMenu(int target, double wx, double wy, GdkEventButton* e) {
	GtkWidget* menu = gtk_menu_new();
	gtk_menu_attach_to_widget(GTK_MENU(menu), win, nullptr);
	switch (target) {
	case CL_CONTEXT_PIN: {
		GtkWidget* item = gtk_menu_item_new_with_mnemonic("_Disconnect");
		MenuPoint* mp = new MenuPoint{ this, wx, wy };
		g_signal_connect_data(item, "activate", G_CALLBACK(disconnectCb), mp,
		                      [](gpointer d, GClosure*) { delete static_cast<MenuPoint*>(d); }, (GConnectFlags)0);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		break;
	}
	case CL_CONTEXT_WIRE:
		menuItem(menu, "_Straighten Route", "win.straighten");
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
		menuItem(menu, "_Delete", "win.delete");
		break;
	case CL_CONTEXT_GATE:
		menuItem(menu, "_Settings…", "win.gate-settings");
		menuItem(menu, "_Rotate", "win.rotate");
		menuItem(menu, "Straighten Its _Wires", "win.straighten");
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
		menuItem(menu, "Cu_t", "win.cut");
		menuItem(menu, "_Copy", "win.copy");
		menuItem(menu, "D_uplicate", "win.duplicate");
		menuItem(menu, "_Delete", "win.delete");
		break;
	default:
		menuItem(menu, "_Paste", "win.paste");
		menuItem(menu, "Select _All", "win.select-all");
		menuItem(menu, "_Add a Gate…", "win.add-gate");
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
		menuItem(menu, "Zoom to _Fit", "win.zoom-fit");
		break;
	}
	updateActions();
	gtk_widget_show_all(menu);
	g_signal_connect(menu, "deactivate", G_CALLBACK(+[](GtkMenuShell* m, gpointer) {
		// Destroy it once its item has run.
		g_idle_add([](gpointer m) -> gboolean { gtk_widget_destroy(GTK_WIDGET(m)); return G_SOURCE_REMOVE; }, m);
	}), nullptr);
	gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*)e);
}

// ---- Simulation --------------------------------------------------------------------

void CircuitWindow::setRunning(bool run) {
	isRunning = run;
	cl_document_set_running(doc, run);
	lastTick = g_get_monotonic_time();
	updateRunUI();
}

void CircuitWindow::stepOnce() {
	// Stepping a running circuit means little: pause first.
	if (isRunning) setRunning(false);
	cl_document_step(doc);
	redraw();
	if (scope) scope->update();
}

void CircuitWindow::setStepMs(int ms) {
	if (ms == cl_document_step_ms(doc)) return;
	cl_document_set_step_ms(doc, ms);
	note(format("Each step is now %d ms of circuit time.", cl_document_step_ms(doc)));
}

void CircuitWindow::toggleSimView() {
	simViewOn = !simViewOn;
	if (simViewOn) {
		if (Canvas* c = currentCanvas()) c->cancelDrag();
		cl_edit_cancel(doc);
		if (cl_edit_tidy_active(doc)) cl_edit_tidy_end(doc, true);
		cl_edit_select_none(doc, currentPage());
		pendingGate.clear();
		if (!isRunning) setRunning(true);   // "Run" means run
	}
	updateActions();
	updateBanner();
	for (Canvas* c : canvases) c->redraw();
}

void CircuitWindow::toggleLock() {
	lockedOn = !lockedOn;
	if (lockedOn) {
		if (Canvas* c = currentCanvas()) c->cancelDrag();
		pendingGate.clear();
	}
	note(lockedOn ? "Locked: switches still work; nothing else changes." : "Unlocked.");
	updateActions();
	updateBanner();
}

void CircuitWindow::makeTruthTable() { showTruthTable(this, currentPage()); redraw(); }

void CircuitWindow::toggleScope() {
	if (scope && scope->visible()) { scope->close(); return; }
	if (scope == nullptr) scope = new ScopeWindow(this);
	scope->present();
}

// ---- Pages ---------------------------------------------------------------------

void CircuitWindow::newPage() {
	int taken = cl_document_page_count(doc) + 1;
	std::vector<std::string> names;
	for (int i = 0; i < cl_document_page_count(doc); i++) names.push_back(pageName(i));
	while (std::find(names.begin(), names.end(), format("Page %d", taken)) != names.end()) taken++;
	const int i = cl_document_add_page(doc);
	if (i < 0) return;
	cl_document_rename_page(doc, i, format("Page %d", taken).c_str());
	syncTabs();
	gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), i);
	appearStart = g_get_monotonic_time();
	edited();
}

void CircuitWindow::closePage(int page) {
	if (cl_document_page_count(doc) < 2) return;
	// A tab with work on it asks first (wx CloseTabCanvas).
	if (cl_document_gate_count(doc, page) > 0 &&
	    !askYesNo(GTK_WINDOW(win), "Close Tab", "All work on this tab will be lost. Would you like to close it?\n\n"
	                                            "(Ctrl+Shift+T brings it back.)"))
		return;
	for (Canvas* c : canvases) c->cancelDrag();
	cl_edit_select_none(doc, page);
	if (cl_document_close_page(doc, page)) {
		const int show = std::min(cl_document_page_to_show(doc), cl_document_page_count(doc) - 1);
		syncTabs();
		if (show >= 0) gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), show);
		edited();
	}
}

void CircuitWindow::reopenPage() {
	if (!cl_edit_undo_is_close_page(doc)) { note("No closed tab to reopen."); gtk_widget_error_bell(win); return; }
	undo();
	appearStart = g_get_monotonic_time();
	note("Reopened the closed tab.");
}

void CircuitWindow::renamePage(int page) {
	if (page < 0 || page >= cl_document_page_count(doc)) return;
	std::string name = pageName(page);
	if (!askText(GTK_WINDOW(win), "Rename Tab", "The tab's name:", name)) return;
	if (name.empty()) return;
	cl_document_rename_page(doc, page, name.c_str());
	updateTabLabels();
	updateTitle();
}

void CircuitWindow::cyclePage(int delta) {
	const int n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook));
	if (n < 2) return;
	const int at = gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook));
	gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), ((at + delta) % n + n) % n);
}

// ---- App-wide ------------------------------------------------------------------

void CircuitWindow::toggleDark() {
	prefs().dark = !prefs().dark;
	if (prefs().themeMode == 0) prefs().themeMode = 3;   // the choice sticks
	prefs().save();
	applyTheme();
}

void CircuitWindow::togglePalette() {
	prefs().showPalette = !prefs().showPalette;
	prefs().save();
	for (CircuitWindow* w : circuitWindows()) w->prefsChanged();
}

void CircuitWindow::showPreferences() { showPreferencesDialog(GTK_WINDOW(win)); }
void CircuitWindow::showShortcuts() { showShortcutsWindow(GTK_WINDOW(win)); }

void CircuitWindow::showHelp() {
	const std::string page = resourcesDir() + "/help/Introduction.htm";
	if (g_file_test(page.c_str(), G_FILE_TEST_EXISTS)) {
		gchar* uri = g_filename_to_uri(page.c_str(), nullptr, nullptr);
		if (uri) { openExternally(GTK_WINDOW(win), uri); g_free(uri); }
	} else {
		showShortcuts();
	}
}

void CircuitWindow::showAbout() {
	const char* authors[] = { "Cedarville University", "Contributors to CedarLogic", nullptr };
	gtk_show_about_dialog(GTK_WINDOW(win),
		"program-name", "CedarLogic",
		"version", CL_VERSION " (native Linux, testing)",
		"comments", "A digital logic simulator.\nThis is the native Linux app: GTK and Cairo on the shared "
		            "CedarLogic engine, with no OpenGL.",
		"logo-icon-name", "cedarlogic",
		"authors", authors,
		"license-type", GTK_LICENSE_GPL_2_0,
		"website", "https://github.com/leviholliday/cedarlogic",
		nullptr);
}

void CircuitWindow::rebuildRecentMenu() {}
