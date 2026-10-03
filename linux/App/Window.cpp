// A circuit window (see Window.h).

#include "Window.h"
#include "Alert.h"
#include "Canvas.h"
#include "Collections.h"
#include "Dialogs.h"
#include "Feedback.h"
#include "FindBar.h"
#include "Help.h"
#include "Settings.h"
#include "Brand.h"
#include "Sheet.h"
#include "Splash.h"
#include "StatusBar.h"
#include "TitleButtons.h"
#include "Welcome.h"
#include "TabSwitcher.h"
#include "Formula.h"
#include "MiniMap.h"
#include "Palette.h"
#include "Library.h"
#include "LibraryWindow.h"
#include "Recovery.h"
#include "TabStrip.h"
#include "Toolbar.h"

#include <cairo-pdf.h>
#include <cairo-svg.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace {

const double kSelectionFadeTime = 0.13, kAppearTime = 0.32, kDragFadeTime = 0.18, kNoteTime = 4.0;

double secondsSince(gint64 t) { return (g_get_monotonic_time() - t) / 1e6; }

// Every command, whichever way it arrives, by action name.
struct Command {
	const char* name;
	void (*run)(CircuitWindow*);
	bool toggle;   // a check item in the menus
};

const Command kCommands[] = {
	{ "save", [](CircuitWindow* w) { w->save(); }, false },
	{ "save-as", [](CircuitWindow* w) { w->exportCopy(); }, false },
	{ "rename-circuit", [](CircuitWindow* w) { w->renameFile(); }, false },
	{ "duplicate-circuit", [](CircuitWindow* w) { w->duplicateCircuit(); }, false },
	{ "versions", [](CircuitWindow* w) { showVersionHistory(w); }, false },
	{ "library", [](CircuitWindow* w) { showYourCircuits(w); }, false },
	{ "new-template", [](CircuitWindow* w) { templates::showPicker(w); }, false },
	{ "save-template", [](CircuitWindow* w) { templates::saveCurrent(w); }, false },
	{ "save-part", [](CircuitWindow* w) { parts::saveSelection(w); }, false },
	{ "build-formula", [](CircuitWindow* w) { if (w->canEdit()) showBuildFormula(w); else w->lockNudge(); }, false },
	{ "find", [](CircuitWindow* w) { w->find(); }, false },
	{ "feedback", [](CircuitWindow* w) { feedback::show(w); }, false },
	{ "whats-new", [](CircuitWindow* w) { whatsnew::show(w); }, false },
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
	{ "focus-mode", [](CircuitWindow* w) { w->toggleFocusMode(); }, true },
	{ "split-view", [](CircuitWindow* w) { w->toggleSplit(); }, true },
	{ "switch-pane", [](CircuitWindow* w) { w->switchPane(); }, false },
	{ "close-split", [](CircuitWindow* w) { w->closeSplit(); }, false },
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
	guarded(b->command->name, [&] { b->command->run(b->window); });
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
	lastRecovery = lastTick;
	timer = g_timeout_add(16, tickCb, this);
	updateTitle();
	updateActions();
	updateRunUI();
	updateBanner();
	gtk_widget_show_all(win);
	gtk_widget_set_visible(paneBoxes[1], splitOpen());
	gtk_revealer_set_reveal_child(GTK_REVEALER(statusBar->outer()), prefs().showStatus);
	updateBanner();
	gtk_widget_set_visible(banner, FALSE);
	if (Canvas* c = currentCanvas()) gtk_widget_grab_focus(c->widget());
	// While the launch screen is up, it does the introducing (main.cpp
	// begins the card as the windows come in).
	if (!splashActive()) beginOpening();
}

// The card plays once the window is in place and drawn.
void CircuitWindow::beginOpening() {
	openingAt = g_get_monotonic_time() / 1e6 + 0.08;
	openingRevealed = false;
}

bool CircuitWindow::openingCard(double& t) const {
	if (openingAt < 0) return false;
	t = g_get_monotonic_time() / 1e6 - openingAt;
	return true;
}

std::string CircuitWindow::openingDetail() const {
	const int pages = cl_document_page_count(doc);
	long gates = 0;
	for (int p = 0; p < pages; p++) gates += cl_document_gate_count(doc, p);
	if (gates == 0) return pages > 1 ? format("%d empty tabs", pages) : std::string("A blank page, ready to build");
	return format("%d tab%s  \u00B7  %ld gate%s", pages, pages == 1 ? "" : "s", gates, gates == 1 ? "" : "s");
}

CircuitWindow::~CircuitWindow() {
	if (timer) g_source_remove(timer);
	if (autosaveId) g_source_remove(autosaveId);
	timer = 0;
	delete scope;
	scope = nullptr;
	delete switcher;
	switcher = nullptr;
	delete findBar;
	findBar = nullptr;
	for (Canvas* c : canvases) delete c;
	canvases.clear();
	delete palette;
	delete miniMap;
	delete toolbar;
	delete strips[0];
	delete strips[1];
	delete statusBar;
	for (GtkWidget*& b : paneBoxes) if (b) { g_object_unref(b); b = nullptr; }
	std::vector<CircuitWindow*>& all = circuitWindows();
	all.erase(std::remove(all.begin(), all.end(), this), all.end());
	// Closed on purpose (saved, or the changes let go): no copy to offer back.
	recovery::remove(recoveryBase);
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
	g_signal_connect(win, "key-release-event", G_CALLBACK(keyReleaseCb), this);
	g_signal_connect(win, "focus-out-event", CL_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer self) -> gboolean {
		CircuitWindow* w = static_cast<CircuitWindow*>(self);
		if (w->switcher) w->switcher->cancel();
		return FALSE;
	}), this);
	g_signal_connect(win, "window-state-event", G_CALLBACK(stateCb), this);
	g_signal_connect(win, "size-allocate", G_CALLBACK(sizeCb), this);
	addActions();

	GtkWidget* v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(win), v);
	// The toolbar is the title bar (the Mac's and the Windows app's), and
	// every menu is behind its •••. Focus mode slides it away.
	toolbar = new Toolbar(this);
	titleRevealer = gtk_revealer_new();
	gtk_revealer_set_transition_type(GTK_REVEALER(titleRevealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
	gtk_revealer_set_transition_duration(GTK_REVEALER(titleRevealer), 240);
	gtk_revealer_set_reveal_child(GTK_REVEALER(titleRevealer), TRUE);
	gtk_container_add(GTK_CONTAINER(titleRevealer), toolbar->widget());
	gtk_widget_show_all(titleRevealer);
	gtk_window_set_titlebar(GTK_WINDOW(win), titleRevealer);
	gtk_application_window_set_show_menubar(GTK_APPLICATION_WINDOW(win), FALSE);

	// A bar for Tidy Up's preview, Simulation View and Lock (drawn on the
	// canvas now; kept for its text).
	banner = gtk_info_bar_new();
	gtk_info_bar_set_message_type(GTK_INFO_BAR(banner), GTK_MESSAGE_INFO);
	bannerLabel = gtk_label_new("");
	gtk_container_add(GTK_CONTAINER(gtk_info_bar_get_content_area(GTK_INFO_BAR(banner))), bannerLabel);
	bannerButtons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_container_add(GTK_CONTAINER(gtk_info_bar_get_content_area(GTK_INFO_BAR(banner))), bannerButtons);
	gtk_box_pack_start(GTK_BOX(v), banner, FALSE, FALSE, 0);

	GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(v), row, TRUE, TRUE, 0);
	// The side panel (the Mac's CLSidePanel): as wide as its gates need, and
	// sliding away in focus mode.
	GtkWidget* leftBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_name(leftBox, "sidepanel");
	palette = new GatePalette(this);
	paletteBox = leftBox;
	gtk_widget_set_size_request(leftBox, paletteWidth(), -1);
	gtk_box_pack_start(GTK_BOX(leftBox), palette->widget(), TRUE, TRUE, 0);
	GtkWidget* mapLine = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_name(mapLine, "sash");
	gtk_widget_set_size_request(mapLine, -1, 1);
	gtk_box_pack_start(GTK_BOX(leftBox), mapLine, FALSE, FALSE, 0);
	miniMap = new MiniMap(this);
	gtk_box_pack_start(GTK_BOX(leftBox), miniMap->widget(), FALSE, FALSE, 0);
	GtkWidget* sideRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(sideRow), leftBox, FALSE, FALSE, 0);
	GtkWidget* sash = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_name(sash, "sash");
	gtk_widget_set_size_request(sash, 1, -1);
	gtk_box_pack_start(GTK_BOX(sideRow), sash, FALSE, FALSE, 0);
	sideRevealer = gtk_revealer_new();
	gtk_revealer_set_transition_type(GTK_REVEALER(sideRevealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_RIGHT);
	gtk_revealer_set_transition_duration(GTK_REVEALER(sideRevealer), 240);
	gtk_container_add(GTK_CONTAINER(sideRevealer), sideRow);
	gtk_revealer_set_reveal_child(GTK_REVEALER(sideRevealer), prefs().showPalette);
	gtk_box_pack_start(GTK_BOX(row), sideRevealer, FALSE, FALSE, 0);

	// The canvas area: a side (its own tab strip over its pages, which stay
	// in a notebook of their own with its tabs hidden), or two side by side.
	splitPaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	g_object_set(splitPaned, "wide-handle", TRUE, nullptr);
	for (int pane = 0; pane < 2; pane++) {
		GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		strips[pane] = new TabStrip(this, pane);
		gtk_box_pack_start(GTK_BOX(box), strips[pane]->outer(), FALSE, FALSE, 0);
		notebooks[pane] = gtk_notebook_new();
		gtk_notebook_set_show_tabs(GTK_NOTEBOOK(notebooks[pane]), FALSE);
		gtk_notebook_set_show_border(GTK_NOTEBOOK(notebooks[pane]), FALSE);
		g_signal_connect(notebooks[pane], "switch-page", G_CALLBACK(switchPageCb), this);
		if (pane == 0) {
			// The find bar floats over the top of the first side's page.
			GtkWidget* over = gtk_overlay_new();
			pageOverlay = over;
			gtk_container_add(GTK_CONTAINER(over), notebooks[pane]);
			findBar = new FindBar(this);
			gtk_overlay_add_overlay(GTK_OVERLAY(over), findBar->widget());
			gtk_box_pack_start(GTK_BOX(box), over, TRUE, TRUE, 0);
		} else {
			gtk_box_pack_start(GTK_BOX(box), notebooks[pane], TRUE, TRUE, 0);
		}
		paneBoxes[pane] = box;
		g_object_ref_sink(box);   // moved between the paned's two halves
	}
	switcher = new TabSwitcher(this);
	gtk_paned_pack1(GTK_PANED(splitPaned), paneBoxes[0], TRUE, FALSE);
	gtk_paned_pack2(GTK_PANED(splitPaned), paneBoxes[1], TRUE, FALSE);
	// "Drop to split here": drawn over the whole area while a tab is held.
	area = gtk_overlay_new();
	gtk_container_add(GTK_CONTAINER(area), splitPaned);
	hintLayer = gtk_drawing_area_new();
	gtk_widget_set_no_show_all(hintLayer, TRUE);
	g_signal_connect(hintLayer, "draw", G_CALLBACK(drawHintCb), this);
	gtk_overlay_add_overlay(GTK_OVERLAY(area), hintLayer);
	gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(area), hintLayer, TRUE);
	// The oscilloscope docks under the canvas, as on the Mac.
	scopePaned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
	g_object_set(scopePaned, "wide-handle", TRUE, nullptr);
	gtk_paned_pack1(GTK_PANED(scopePaned), area, TRUE, FALSE);
	gtk_box_pack_start(GTK_BOX(row), scopePaned, TRUE, TRUE, 0);

	statusBar = new StatusBar(this);
	gtk_box_pack_start(GTK_BOX(v), statusBar->outer(), FALSE, FALSE, 0);
}

// The side panel is as wide as its gates need (Settings > Appearance >
// Gate size), as the Mac's is.
int CircuitWindow::paletteWidth() const { return std::max(176, std::min(prefs().gateSize * 3 + 30, 340)); }

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

// ---- Tabs and split view ------------------------------------------------------------

std::string CircuitWindow::pageName(int page) const {
	const char* n = cl_document_page_name(doc, page);
	if (n && *n) return n;
	return format("Page %d", page + 1);
}

void CircuitWindow::updateTabLabels() { redrawStrips(); }

void CircuitWindow::redrawStrips() {
	for (TabStrip* t : strips) if (t) t->redraw();
}

int CircuitWindow::paneOf(const Canvas* c) const { return c && sideKeys.count(c->pageKey()) ? 1 : 0; }

std::vector<int> CircuitWindow::panePages(int pane) const {
	std::vector<int> out;
	for (int i = 0; i < (int)canvases.size(); i++)
		if (paneOf(canvases[i]) == pane) out.push_back(i);
	return out;
}

Canvas* CircuitWindow::paneCanvas(int pane) const {
	GtkWidget* nb = notebooks[pane];
	if (nb == nullptr) return nullptr;
	const int i = gtk_notebook_get_current_page(GTK_NOTEBOOK(nb));
	GtkWidget* w = i >= 0 ? gtk_notebook_get_nth_page(GTK_NOTEBOOK(nb), i) : nullptr;
	for (Canvas* c : canvases) if (c->widget() == w) return c;
	return nullptr;
}

int CircuitWindow::shownPage(int pane) const {
	Canvas* c = paneCanvas(pane);
	return c ? c->page() : -1;
}

// Everything in line after pages were added, closed, reopened or moved: a
// side left with every page gives them back (one strip again), and the
// split's pages that are gone are forgotten.
void CircuitWindow::reconcileSplit() {
	std::set<uint64_t> keep;
	for (Canvas* c : canvases) if (sideKeys.count(c->pageKey())) keep.insert(c->pageKey());
	if (!keep.empty() && keep.size() == canvases.size()) keep.clear();   // the first side ran out
	sideKeys = keep;
	if (sideKeys.empty()) focusPane = 0;
}

// Make the tabs match the document's pages: after opening, a new page, a
// close, an undo that brings one back, a move, a split.
void CircuitWindow::syncTabs() {
	syncing = true;
	Canvas* front[2] = { paneCanvas(0), paneCanvas(1) };
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
		if (GtkWidget* parent = gtk_widget_get_parent(c->widget())) gtk_container_remove(GTK_CONTAINER(parent), c->widget());
		if (front[0] == c) front[0] = nullptr;
		if (front[1] == c) front[1] = nullptr;
		delete c;
	}
	canvases = want;
	reconcileSplit();
	// Each page in its side's notebook, in the document's order.
	for (int pane = 0; pane < 2; pane++) {
		GtkNotebook* nb = GTK_NOTEBOOK(notebooks[pane]);
		int at = 0;
		for (Canvas* c : canvases) {
			if (paneOf(c) != pane) continue;
			GtkWidget* w = c->widget();
			GtkWidget* parent = gtk_widget_get_parent(w);
			if (parent != notebooks[pane]) {
				if (parent) gtk_container_remove(GTK_CONTAINER(parent), w);
				gtk_notebook_insert_page(nb, w, nullptr, at);
				gtk_widget_show(w);
			} else if (gtk_notebook_page_num(nb, w) != at) {
				gtk_notebook_reorder_child(nb, w, at);
			}
			at++;
		}
		// The page it showed, if it's still here.
		if (front[pane] && paneOf(front[pane]) == pane) {
			const int i = gtk_notebook_page_num(nb, front[pane]->widget());
			if (i >= 0) gtk_notebook_set_current_page(nb, i);
		}
	}
	lastPageCount = n;
	layoutSplit();
	syncing = false;
	redrawStrips();
	redrawMiniMap();
}

// The two sides in their places, or one when there's no split.
void CircuitWindow::layoutSplit() {
	GtkPaned* pp = GTK_PANED(splitPaned);
	GtkWidget* want1 = sideFirst ? paneBoxes[1] : paneBoxes[0];
	GtkWidget* want2 = sideFirst ? paneBoxes[0] : paneBoxes[1];
	if (gtk_paned_get_child1(pp) != want1) {
		if (GtkWidget* c = gtk_paned_get_child1(pp)) gtk_container_remove(GTK_CONTAINER(pp), c);
		if (GtkWidget* c = gtk_paned_get_child2(pp)) gtk_container_remove(GTK_CONTAINER(pp), c);
		gtk_paned_pack1(pp, want1, TRUE, FALSE);
		gtk_paned_pack2(pp, want2, TRUE, FALSE);
	}
	const bool open = splitOpen();
	if (gtk_widget_get_visible(paneBoxes[1]) != open) {
		gtk_widget_set_visible(paneBoxes[1], open);
		if (open) {
			// Halves, as the Mac opens them.
			const int w = gtk_widget_get_allocated_width(splitPaned);
			if (w > 50) gtk_paned_set_position(pp, w / 2);
		}
	}
	redrawStrips();
}

bool CircuitWindow::stripIsLeftmost(int pane) const { return !splitOpen() || (pane == 1) == sideFirst; }
bool CircuitWindow::stripIsRightmost(int pane) const { return !splitOpen() || (pane == 1) != sideFirst; }

Canvas* CircuitWindow::currentCanvas() const {
	Canvas* c = paneCanvas(splitOpen() ? focusPane : 0);
	return c ? c : paneCanvas(0);
}

int CircuitWindow::currentPage() const {
	Canvas* c = currentCanvas();
	const int p = c ? c->page() : -1;
	return p >= 0 ? p : 0;
}

void CircuitWindow::activatePane(int pane) {
	if (!splitOpen()) pane = 0;
	if (pane == focusPane) return;
	if (Canvas* old = currentCanvas()) old->cancelDrag();
	focusPane = pane;
	if (Canvas* c = currentCanvas()) {
		recentKeys.erase(std::remove(recentKeys.begin(), recentKeys.end(), c->pageKey()), recentKeys.end());
		recentKeys.insert(recentKeys.begin(), c->pageKey());
	}
	statusDirty = true;
	selectionSignature.clear();
	updateActions();
	updateTitle();
	redrawStrips();
	redrawMiniMap();
	if (toolbar) toolbar->redraw();
}

void CircuitWindow::showPage(int page) {
	if (page < 0 || page >= (int)canvases.size()) return;
	Canvas* c = canvases[page];
	const int pane = paneOf(c);
	GtkNotebook* nb = GTK_NOTEBOOK(notebooks[pane]);
	const int i = gtk_notebook_page_num(nb, c->widget());
	activatePane(pane);
	if (i >= 0 && gtk_notebook_get_current_page(nb) != i) gtk_notebook_set_current_page(nb, i);
	gtk_widget_grab_focus(c->widget());
	redrawStrips();
}

void CircuitWindow::switchPageCb(GtkNotebook* nb, GtkWidget* page, guint, gpointer self) {
	CircuitWindow* w = static_cast<CircuitWindow*>(self);
	if (w->syncing) return;
	const int pane = GTK_WIDGET(nb) == w->notebooks[1] ? 1 : 0;
	// Leaving a page lets go of its selection, as the wx app does.
	if (Canvas* old = w->paneCanvas(pane)) {
		if (old->widget() != page && old->page() >= 0) {
			old->cancelDrag();
			cl_edit_select_none(w->doc, old->page());
			old->dropBuffers();
		}
	}
	w->statusDirty = true;
	w->selectionSignature.clear();
	w->focusPane = w->splitOpen() ? pane : 0;
	for (Canvas* c : w->canvases) {
		if (c->widget() != page) continue;
		w->recentKeys.erase(std::remove(w->recentKeys.begin(), w->recentKeys.end(), c->pageKey()), w->recentKeys.end());
		w->recentKeys.insert(w->recentKeys.begin(), c->pageKey());
		gtk_widget_grab_focus(c->widget());
		if (!w->seenPages[c->pageKey()]) { w->seenPages[c->pageKey()] = true; w->appearStart = g_get_monotonic_time(); }
	}
	g_idle_add([](gpointer self) -> gboolean {
		for (CircuitWindow* o : circuitWindows())
			if (o == self) { o->updateActions(); o->updateTitle(); o->redrawStrips(); o->toolbar->redraw(); o->redrawMiniMap(); }
		return G_SOURCE_REMOVE;
	}, w);
}

// A tab dragged along its strip: the page moves to where `to` is.
void CircuitWindow::movePage(int from, int to) {
	if (from < 0 || to < 0 || from >= (int)canvases.size() || to >= (int)canvases.size() || from == to) return;
	guarded("moving a tab", [&] {
		// A tab without a name of its own is called by its place ("Page 2"):
		// pin those names first, so moving a tab doesn't rename the others.
		for (int i = 0; i < cl_document_page_count(doc); i++) {
			const char* n = cl_document_page_name(doc, i);
			if (n == nullptr || *n == 0) cl_document_rename_page(doc, i, pageName(i).c_str());
		}
		cl_document_move_page(doc, from, to);
		changes++;
		syncTabs();
		updateTitle();
	});
}

// Split View from the menu or keys: the tab used most recently beside the
// one in front, or a new tab if it's the only one. Again: closes it.
void CircuitWindow::toggleSplit() {
	if (splitOpen()) { closeSplit(); return; }
	const int front = currentPage();
	int partner = -1;
	for (uint64_t key : recentKeys) {
		const int i = cl_document_page_index(doc, key);
		if (i >= 0 && i != front) { partner = i; break; }
	}
	if (partner < 0) for (int i = 0; i < (int)canvases.size(); i++) if (i != front) { partner = i; break; }
	if (partner < 0) {
		newPage();
		partner = cl_document_page_count(doc) - 1;
		showPage(front);
	}
	splitWith(partner, true);
}

// Split the view with `page` on one side (the wx app's SplitWith). The first
// side can't be left empty: taking its last tab gives it a new one.
void CircuitWindow::splitWith(int page, bool onRight) {
	if (splitOpen() || page < 0 || page >= (int)canvases.size()) return;
	if (cl_document_page_count(doc) < 2) {
		newPage();
		page = 0;
	}
	const uint64_t key = canvases[page]->pageKey();
	sideFirst = !onRight;
	sideKeys = { key };
	syncTabs();
	showPage(cl_document_page_index(doc, key));
	note("Split view. Drag tabs between the two sides; the split closes when a side runs out.");
	// Each side fits its page in its half, once the halves are laid out.
	g_idle_add([](gpointer self) -> gboolean {
		for (CircuitWindow* o : circuitWindows())
			if (o == self) for (int pane = 0; pane < 2; pane++) if (Canvas* c = o->paneCanvas(pane)) c->zoomToFit(true);
		return G_SOURCE_REMOVE;
	}, this);
}

void CircuitWindow::movePageToPane(int page, int pane) {
	if (!splitOpen() || page < 0 || page >= (int)canvases.size()) return;
	const uint64_t key = canvases[page]->pageKey();
	if (pane == 1) sideKeys.insert(key);
	else sideKeys.erase(key);
	syncTabs();
	const int now = cl_document_page_index(doc, key);
	if (now >= 0) showPage(now);
}

// One strip again, with every tab; the side you were in stays in front.
void CircuitWindow::closeSplit() {
	if (!splitOpen()) return;
	const int keepFront = focusPane == 1 ? shownPage(1) : shownPage(0);
	sideKeys.clear();
	focusPane = 0;
	syncTabs();
	if (keepFront >= 0) showPage(keepFront);
	showDropHint(DropHint());
}

void CircuitWindow::switchPane() {
	if (!splitOpen()) return;
	const int other = 1 - focusPane;
	const int p = shownPage(other);
	if (p >= 0) showPage(p);
}

// Where a tab held at (x, y) in `from` would go: half the area to split it
// (no split yet), or the other side to move it there.
CircuitWindow::DropHint CircuitWindow::dropHintAt(int fromPane, GtkWidget* from, double x, double y) const {
	DropHint h;
	int ax = 0, ay = 0;
	if (!gtk_widget_translate_coordinates(from, area, (int)x, (int)y, &ax, &ay)) return h;
	const int w = gtk_widget_get_allocated_width(area), hgt = gtk_widget_get_allocated_height(area);
	if (ax < 0 || ay < 0 || ax > w || ay > hgt) return h;
	if (splitOpen()) {
		const int other = 1 - fromPane;
		GtkAllocation a;
		gtk_widget_get_allocation(paneBoxes[other], &a);
		int ox = 0, oy = 0;
		gtk_widget_translate_coordinates(paneBoxes[other], area, 0, 0, &ox, &oy);
		if (ax >= ox && ax < ox + a.width && ay >= oy && ay < oy + a.height) { h.kind = 2; h.side = other; }
		return h;
	}
	if (fromPane != 0 || canvases.empty()) return h;
	if (ay < TabStrip::stripHeight() + 12) return h;
	h.kind = 1;
	h.side = ax < w / 2 ? -1 : 1;
	return h;
}

void CircuitWindow::showDropHint(const DropHint& h) {
	if (h == hint) return;
	hint = h;
	if (h.kind != 0) {
		hintShown = h;
		gtk_widget_show(hintLayer);
		hintFade.go(1, 0.15);
	} else {
		hintFade.go(0, 0.15);
	}
	// Redrawn every frame while it fades.
	gtk_widget_add_tick_callback(hintLayer, [](GtkWidget* layer, GdkFrameClock*, gpointer self) -> gboolean {
		CircuitWindow* w = static_cast<CircuitWindow*>(self);
		gtk_widget_queue_draw(layer);
		if (w->hintFade.active()) return G_SOURCE_CONTINUE;
		if (w->hint.kind == 0) gtk_widget_hide(layer);
		return G_SOURCE_REMOVE;
	}, this, nullptr);
}

gboolean CircuitWindow::drawHintCb(GtkWidget* layer, cairo_t* cr, gpointer self) {
	CircuitWindow* w = static_cast<CircuitWindow*>(self);
	const float a = (float)w->hintFade.value();
	if (a <= 0.01f || w->hintShown.kind == 0) return TRUE;
	const float W = gtk_widget_get_allocated_width(layer), H = gtk_widget_get_allocated_height(layer);
	RectF r;
	if (w->hintShown.kind == 1) {
		const float top = TabStrip::stripHeight();
		r = w->hintShown.side < 0 ? rectF(0, top, W / 2, H) : rectF(W / 2, top, W, H);
	} else {
		GtkAllocation al;
		gtk_widget_get_allocation(w->paneBoxes[w->hintShown.side], &al);
		int ox = 0, oy = 0;
		gtk_widget_translate_coordinates(w->paneBoxes[w->hintShown.side], w->area, 0, 0, &ox, &oy);
		r = rectF((float)ox, (float)oy, (float)(ox + al.width), (float)(oy + al.height));
	}
	r = rectF(r.left + 6, r.top + 6, r.right - 6, r.bottom - 6);
	const Color accent = chrome().accent();
	fillRound(cr, r, 14, withAlpha(accent, 0.18f * a));
	strokeRound(cr, r, 14, withAlpha(accent, a), 2);
	drawTextMid(cr, w->hintShown.kind == 2 ? "Drop to move here" : "Drop to split here", r, 13, withAlpha(accent, a), TextAlign::Center, true);
	return TRUE;
}

void CircuitWindow::tabDropped(int page, const DropHint& h) {
	showDropHint(DropHint());
	if (h.kind == 2) movePageToPane(page, h.side);
	else if (h.kind == 1) splitWith(page, h.side > 0);
}

// ---- The toolbar's and the tab strip's side ----------------------------------------

GtkWidget* CircuitWindow::runButtonForTour() const { return toolbar->widget(); }
GtkWidget* CircuitWindow::tabStripForTour() const { return strips[0]->widget(); }

void CircuitWindow::runAction(const char* name) {
	if (name == nullptr) return;
	const bool onApp = g_str_has_prefix(name, "app.");
	const char* bare = std::strchr(name, '.') ? std::strchr(name, '.') + 1 : name;
	GActionGroup* group = onApp ? G_ACTION_GROUP(app) : G_ACTION_GROUP(win);
	if (g_action_group_has_action(group, bare)) g_action_group_activate_action(group, bare, nullptr);
}

bool CircuitWindow::actionEnabled(const char* name) const {
	if (name == nullptr) return false;
	const bool onApp = g_str_has_prefix(name, "app.");
	const char* bare = std::strchr(name, '.') ? std::strchr(name, '.') + 1 : name;
	GActionGroup* group = onApp ? G_ACTION_GROUP(app) : G_ACTION_GROUP(win);
	return g_action_group_has_action(group, bare) && g_action_group_get_action_enabled(group, bare);
}

// A menu from a model, popped up under a rectangle and gone when it closes.
// Opened from the keyboard (no event), its first item is picked, so the
// arrow keys go on from there.
static void popupModel(GtkWidget* attach, GMenuModel* model, GtkWidget* from, GdkRectangle anchor, GdkEvent* e, bool rightAligned) {
	GtkWidget* menu = gtk_menu_new_from_model(model);
	gtk_menu_attach_to_widget(GTK_MENU(menu), attach, nullptr);
	g_signal_connect(menu, "deactivate", CL_CALLBACK(+[](GtkMenuShell* m, gpointer) {
		g_idle_add([](gpointer m) -> gboolean { gtk_widget_destroy(GTK_WIDGET(m)); return G_SOURCE_REMOVE; }, m);
	}), nullptr);
	gtk_menu_popup_at_rect(GTK_MENU(menu), gtk_widget_get_window(from), &anchor,
	                       rightAligned ? GDK_GRAVITY_SOUTH_EAST : GDK_GRAVITY_SOUTH_WEST,
	                       rightAligned ? GDK_GRAVITY_NORTH_EAST : GDK_GRAVITY_NORTH_WEST, e);
	if (e == nullptr) gtk_menu_shell_select_first(GTK_MENU_SHELL(menu), FALSE);
}

void CircuitWindow::moreMenu(GtkWidget* from, GdkRectangle anchor, GdkEvent* e) {
	if (GMenuModel* bar = gtk_application_get_menubar(app)) popupModel(win, bar, from, anchor, e, true);
}

// GTK's own F10 belongs to a menu bar, and there is none: the menus are
// behind •••, so F10 opens them there (at the top right in focus mode, with
// the bar away).
void CircuitWindow::menusFromKeyboard() {
	GdkRectangle r;
	if (!focusOn && toolbar && toolbar->moreRect(r)) moreMenu(toolbar->widget(), r, nullptr);
	else moreMenu(win, GdkRectangle{ gtk_widget_get_allocated_width(win) - 48, 6, 32, 32 }, nullptr);
}

void CircuitWindow::contextMenuFromKeyboard() {
	if (simViewOn) return;
	if (lockedOn) { lockNudge(); return; }
	const int p = currentPage();
	const int target = cl_edit_selected_gate_count(doc, p) > 0 ? CL_CONTEXT_GATE
	                 : cl_edit_selected_wire_count(doc, p) > 0 ? CL_CONTEXT_WIRE : CL_CONTEXT_NOTHING;
	double wx = 0, wy = 0;
	placePoint(wx, wy);
	showContextMenu(target, wx, wy, nullptr);
}

void CircuitWindow::titleMenu(GtkWidget* from, GdkRectangle anchor, GdkEvent* e) {
	GMenu* m = g_menu_new();
	GMenu* a = g_menu_new();
	g_menu_append(a, "_Rename…", "win.rename-circuit");
	g_menu_append(a, "_Duplicate", "win.duplicate-circuit");
	g_menu_append(a, "_Version History…", "win.versions");
	g_menu_append_section(m, nullptr, G_MENU_MODEL(a));
	GMenu* b = g_menu_new();
	g_menu_append(b, "_Export…", "win.save-as");
	g_menu_append(b, "Your _Circuits…", "app.open");
	g_menu_append_section(m, nullptr, G_MENU_MODEL(b));
	popupModel(win, G_MENU_MODEL(m), from, anchor, e, false);
	g_object_unref(a);
	g_object_unref(b);
	g_object_unref(m);
}

struct TabMenuData { CircuitWindow* w; int page; };

static void tabMenuItem(GtkWidget* menu, const char* label, void (*act)(CircuitWindow*, int), CircuitWindow* w, int page, bool enabled = true) {
	GtkWidget* item = gtk_menu_item_new_with_mnemonic(label);
	TabMenuData* d = new TabMenuData{ w, page };
	g_object_set_data_full(G_OBJECT(item), "cl-tab", d, [](gpointer p) { delete static_cast<TabMenuData*>(p); });
	g_object_set_data(G_OBJECT(item), "cl-act", (gpointer)act);
	g_signal_connect(item, "activate", CL_CALLBACK(+[](GtkMenuItem* it, gpointer) {
		TabMenuData* d = static_cast<TabMenuData*>(g_object_get_data(G_OBJECT(it), "cl-tab"));
		auto act = reinterpret_cast<void (*)(CircuitWindow*, int)>(g_object_get_data(G_OBJECT(it), "cl-act"));
		const TabMenuData copy = *d;   // the menu goes once its item has run
		guarded("a tab's menu", [&] { act(copy.w, copy.page); });
	}), nullptr);
	gtk_widget_set_sensitive(item, enabled);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}

// A tab's own menu (the Mac's): Rename, the split's moves, Close.
void CircuitWindow::tabContextMenu(int page, GdkEvent* e) {
	GtkWidget* menu = gtk_menu_new();
	if (page >= 0) {
		showPage(page);
		const int pane = paneOf(canvases[page]);
		tabMenuItem(menu, "_Rename…", [](CircuitWindow* w, int p) { w->strips[w->paneOf(w->canvases[p])]->beginRename(p); }, this, page);
		if (splitOpen()) {
			tabMenuItem(menu, pane == 0 ? "Move to the _Other Side" : "Move to the _Other Side",
			            [](CircuitWindow* w, int p) { w->movePageToPane(p, 1 - w->paneOf(w->canvases[p])); }, this, page);
			tabMenuItem(menu, "Close _Split View", [](CircuitWindow* w, int) { w->closeSplit(); }, this, page);
		} else {
			tabMenuItem(menu, "Open in _Split View", [](CircuitWindow* w, int p) { w->splitWith(p, true); }, this, page);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
		tabMenuItem(menu, "_Close Tab", [](CircuitWindow* w, int p) { w->closePage(p); }, this, page, cl_document_page_count(doc) > 1);
	} else {
		tabMenuItem(menu, "_New Tab", [](CircuitWindow* w, int) { w->newPage(); }, this, page);
		tabMenuItem(menu, "Re_open Closed Tab", [](CircuitWindow* w, int) { w->reopenPage(); }, this, page, cl_edit_undo_is_close_page(doc));
		if (splitOpen()) tabMenuItem(menu, "Close _Split View", [](CircuitWindow* w, int) { w->closeSplit(); }, this, page);
	}
	gtk_widget_show_all(menu);
	gtk_menu_attach_to_widget(GTK_MENU(menu), win, nullptr);
	g_signal_connect(menu, "deactivate", CL_CALLBACK(+[](GtkMenuShell* mm, gpointer) {
		g_idle_add([](gpointer mm) -> gboolean { gtk_widget_destroy(GTK_WIDGET(mm)); return G_SOURCE_REMOVE; }, mm);
	}), nullptr);
	gtk_menu_popup_at_pointer(GTK_MENU(menu), e);
}

// ---- The clock -------------------------------------------------------------------

gboolean CircuitWindow::tickCb(gpointer self) {
	guarded("the simulation", [&] { static_cast<CircuitWindow*>(self)->tick(); });
	return G_SOURCE_CONTINUE;
}

void CircuitWindow::tick() {
	const gint64 t = g_get_monotonic_time();
	const double elapsed = (t - lastTick) / 1000.0;   // ms
	lastTick = t;
	Canvas* c = currentCanvas();
	if (c) c->stepAnimation();
	// The opening card: the circuit fades up as it lifts away.
	if (openingAt >= 0) {
		const double ot = t / 1e6 - openingAt;
		if (ot >= 0.58 && !openingRevealed) { openingRevealed = true; appearStart = t; }
		if (ot >= 0.88) openingAt = -1;
		redraw();
	}
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
	// The toolbar's zoom follows the camera.
	if (c && c->zoomPercent() != lastZoomShown) { lastZoomShown = c->zoomPercent(); toolbar->redraw(); }
	if ((t - lastTitle) > 500000) { lastTitle = t; updateTitle(); }
	// A recovery copy of unsaved work, at most every 20 seconds.
	if (changes != changesAtRecovery && (t - lastRecovery) > 20 * G_USEC_PER_SEC) writeRecovery();
	// A note on the canvas: redrawn while it fades in and out.
	if (messageAt) {
		const double age = secondsSince(messageAt);
		if (age < 0.25 || (age > kNoteTime - 0.55 && age < kNoteTime + 0.1)) redraw();
	}
	if (messageAt && secondsSince(messageAt) > 5) { messageAt = 0; redraw(); }
	if (statusBar) statusBar->tick();
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
	// Both sides of a split show the same circuit running.
	for (int pane = 0; pane < 2; pane++)
		if (Canvas* c = paneCanvas(pane)) c->redraw();
}

void CircuitWindow::redrawMiniMap() {
	if (miniMap) miniMap->queueDraw();
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
	changes++;
	// Saving as you go: a couple of seconds after the last change.
	if (autosaveId) g_source_remove(autosaveId);
	autosaveId = g_timeout_add(2000, autosaveCb, this);
	if (cl_document_page_count(doc) != lastPageCount) syncTabs();
	redraw();
	selectionChanged();
	updateActions();
	updateTitle();
	updateBanner();
	updateTabLabels();
	if (findBar && findBar->isOpen()) findBar->run(false);
	statusDirty = true;
}

void CircuitWindow::note(const std::string& message) {
	if (statusBar) statusBar->note(message);
	noteText = message;
	messageAt = g_get_monotonic_time();
	redraw();
}

bool CircuitWindow::toast(std::string& text, double& alpha) const {
	if (messageAt <= 0 || noteText.empty()) return false;
	const double age = secondsSince(messageAt);
	if (age > kNoteTime) return false;
	text = noteText;
	alpha = std::min(1.0, std::min(age / 0.2, (kNoteTime - age) / 0.5));
	return alpha > 0;
}

bool CircuitWindow::bannerFor(std::string& text, std::vector<BannerButton>& buttons) const {
	buttons.clear();
	if (cl_edit_tidy_active(doc)) {
		text = cl_edit_tidy_mode(doc) == 1 ? "Tidy Up by signal flow: a preview. Enter keeps it, Esc puts it back."
		                                   : "Tidy Up: a preview. Enter keeps it, Esc puts it back.";
		buttons = { { "Keep", "win.tidy-keep" }, { "Put Back", "win.tidy-revert" }, { "Other Way", "win.tidy-switch" } };
		return true;
	}
	if (lockedOn && !simViewOn) {
		text = "Locked: switches still work; nothing else changes.";
		buttons = { { "Unlock", "win.lock" } };
		return true;
	}
	return false;
}

void CircuitWindow::lockNudge() {
	if (simViewOn) note("Leave Simulation View (Escape) to edit.");
	else note("The circuit is locked. Unlock it (Simulate > Lock) to edit.");
	gtk_widget_error_bell(win);
}

std::string CircuitWindow::displayName() const {
	library::Item it;
	if (library::itemFor(path, it)) return it.name;
	if (!recoveredName.empty()) return recoveredName;
	if (!path.empty()) return baseName(path);
	return "Untitled";
}

gboolean CircuitWindow::autosaveCb(gpointer self) {
	CircuitWindow* w = static_cast<CircuitWindow*>(self);
	w->autosaveId = 0;
	guarded("saving", [&] {
		// Not in the middle of something (a drag, a gate on the pointer,
		// Tidy Up's preview): then a moment later.
		Canvas* c = w->currentCanvas();
		const bool busy = (c && c->isDragging()) || w->isFloating() || w->tidyActive() || cl_edit_is_connecting(w->doc);
		if (busy) w->autosaveId = g_timeout_add(1000, autosaveCb, w);
		else if (w->isDirty()) w->saveQuietly(false);
	});
	return G_SOURCE_REMOVE;
}

// Into Your Circuits: a circuit that isn't there yet joins it once there's
// something on it (so an empty new window leaves nothing behind). A version
// is kept when one's due, or now when `explicitSave` (Ctrl+S).
bool CircuitWindow::saveQuietly(bool explicitSave) {
	library::Item it;
	const bool inLibrary = library::itemFor(path, it);
	bool hasGates = false;
	for (int p = 0; p < cl_document_page_count(doc) && !hasGates; p++) hasGates = cl_document_gate_count(doc, p) > 0;
	if (!inLibrary && !hasGates && !explicitSave) return true;   // nothing to keep
	const std::string text = cl_document_save_text(doc);   // marks the engine's copy saved
	std::string err;
	if (inLibrary) {
		GError* e = nullptr;
		if (!g_file_set_contents(path.c_str(), text.data(), (gssize)text.size(), &e)) err = e ? e->message : "The file couldn't be written.";
		if (e) g_error_free(e);
	} else {
		std::string name = displayName();
		if (name == "Untitled") name = "Untitled Circuit";
		if (library::create(name, text, "", it)) path = it.circuit();
		else err = "Your Circuits' folder couldn't be written to.";
	}
	if (!err.empty()) {
		forceDirty = true;
		if (explicitSave) showMessage(GTK_WINDOW(win), GTK_MESSAGE_ERROR, "The circuit couldn't be saved", err);
		else note("Couldn't save just now. Your work is still here; try Ctrl+S.");
		updateTitle();
		return false;
	}
	forceDirty = false;
	recoveredName.clear();
	// Saved: the recovery copy isn't needed until the next change.
	recovery::remove(recoveryBase);
	changesAtRecovery = changes;
	const bool kept = library::noteSaved(path, explicitSave);
	library::noteLastCircuit(path);
	if (explicitSave) note(kept ? "Saved, and a version was kept." : "Saved.");
	updateTitle();
	return true;
}

// The name it has in Your Circuits (one not in it yet joins under it).
void CircuitWindow::renameFile() {
	std::string name = displayName();
	if (!askText(GTK_WINDOW(win), "Rename Circuit", "The name it has in Your Circuits:", name) || name.empty() || name == displayName()) return;
	library::Item it;
	if (library::itemFor(path, it)) {
		library::rename(it, name);
		for (CircuitWindow* w : circuitWindows()) w->libraryChanged();
	} else {
		recoveredName = name;
		forceDirty = true;
		saveQuietly(false);
	}
	updateTitle();
	note("Renamed.");
}

// A copy, as a new circuit in Your Circuits, in a window of its own.
void CircuitWindow::duplicateCircuit() {
	saveQuietly(false);
	const std::string text = cl_document_save_text(doc);
	library::Item copy;
	if (!library::create(displayName() + " copy", text, "", copy)) {
		showMessage(GTK_WINDOW(win), GTK_MESSAGE_ERROR, "The circuit couldn't be duplicated", "");
		return;
	}
	char err[512] = "";
	CLDocument* d = cl_document_open(copy.circuit().c_str(), err, sizeof err);
	if (d == nullptr) { showMessage(GTK_WINDOW(win), GTK_MESSAGE_ERROR, "The circuit couldn't be duplicated", err); return; }
	CircuitWindow* w = new CircuitWindow(app, d, copy.circuit());
	w->note("A copy, in Your Circuits as “" + copy.name + "”.");
}

void CircuitWindow::discard() {
	forceDirty = false;
	gtk_widget_destroy(win);
}

void CircuitWindow::reloadFromDisk(const std::string& message) {
	char err[512] = "";
	CLDocument* fresh = cl_document_open(path.c_str(), err, sizeof err);
	if (fresh == nullptr) { showMessage(GTK_WINDOW(win), GTK_MESSAGE_ERROR, "The circuit couldn't be opened again", err); return; }
	replaceDocument(fresh, path);
	if (!message.empty()) note(message);
}

void CircuitWindow::libraryChanged() {
	rebuildRecentMenus();   // Open Recent names circuits by their names in Your Circuits
	updateTitle();
	if (toolbar) toolbar->layoutNow();
}

void CircuitWindow::markRecovered(const std::string& name) {
	recoveredName = name;
	forceDirty = true;
	changes++;
	updateTitle();
	note("Brought back from the copy kept when CedarLogic closed. Save it to keep it.");
}

void CircuitWindow::writeRecovery() {
	lastRecovery = g_get_monotonic_time();
	changesAtRecovery = changes;
	if (!isDirty()) { recovery::remove(recoveryBase); return; }
	// Writing the text marks the engine's copy saved; it isn't, so say so.
	const std::string text = cl_document_save_text(doc);
	forceDirty = true;
	if (recoveryBase.empty()) recoveryBase = recovery::newBase();
	recovery::write(recoveryBase, text, path, displayName());
}

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
	if (now == nullptr || t != now) {
		gtk_window_set_title(GTK_WINDOW(win), t.c_str());
		if (toolbar) toolbar->layoutNow();
	}
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
	(void)s;
	if (statusBar) statusBar->update();
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
	setChecked(win, "focus-mode", focusOn);
	setChecked(win, "split-view", splitOpen());
	setEnabled(win, "switch-pane", splitOpen());
	setEnabled(win, "close-split", splitOpen());
	setChecked(win, "status-bar", prefs().showStatus);
	// The menus' Undo and Redo say what they undo.
	(void)edit;
	if (toolbar) toolbar->redraw();
}

void CircuitWindow::updateRunUI() {
	setChecked(win, "running", isRunning);
	if (toolbar) toolbar->redraw();
	redrawStrips();
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
	// Drawn on the canvas now, as the Mac's and the Windows app's.
	gtk_label_set_text(GTK_LABEL(bannerLabel), text.c_str());
	gtk_widget_set_visible(banner, FALSE);
	redraw();
}

void CircuitWindow::themeChanged() {
	updateActions();
	if (palette) palette->themeChanged();
	for (Canvas* c : canvases) c->redraw();
	redrawMiniMap();
	if (scope) scope->update();
}

void CircuitWindow::prefsChanged() {
	if (!focusOn) gtk_revealer_set_reveal_child(GTK_REVEALER(sideRevealer), prefs().showPalette);
	gtk_widget_set_size_request(paletteBox, paletteWidth(), -1);
	if (statusBar) statusBar->settingsChanged();
	if (toolbar) toolbar->layoutNow();
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
	prefs().save();
	// GTK takes the widgets apart after this handler; the tabs switching
	// page on the way out mustn't reach a window that's gone.
	for (GtkWidget* nb : w->notebooks) g_signal_handlers_disconnect_by_data(nb, w);
	g_signal_handlers_disconnect_by_data(w->win, w);
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
	// held, the Mac's switcher (tabs in the order last used).
	if ((e->state & GDK_CONTROL_MASK) && (e->keyval == GDK_KEY_Tab || e->keyval == GDK_KEY_ISO_Left_Tab ||
	                                      e->keyval == GDK_KEY_KP_Tab)) {
		const bool back = e->keyval == GDK_KEY_ISO_Left_Tab || (e->state & GDK_SHIFT_MASK);
		if (!w->switcher || !w->switcher->key(back)) w->cyclePage(back ? -1 : 1);
		return TRUE;
	}
	if (w->switcher && w->switcher->active() && e->keyval == GDK_KEY_Escape) { w->switcher->cancel(); return TRUE; }
	// The menus from the keyboard: F10 every menu; Shift+F10 or the Menu key
	// the canvas's own, for what's selected.
	const guint mods = e->state & gtk_accelerator_get_default_mod_mask();
	if (e->keyval == GDK_KEY_F10 && mods == 0) { guarded("the menus", [&] { w->menusFromKeyboard(); }); return TRUE; }
	if ((e->keyval == GDK_KEY_F10 && mods == GDK_SHIFT_MASK) || (e->keyval == GDK_KEY_Menu && mods == 0)) {
		guarded("the menu", [&] { w->contextMenuFromKeyboard(); });
		return TRUE;
	}
	return FALSE;
}

gboolean CircuitWindow::keyReleaseCb(GtkWidget*, GdkEventKey* e, gpointer self) {
	CircuitWindow* w = static_cast<CircuitWindow*>(self);
	if (w->switcher && w->switcher->active() && (e->keyval == GDK_KEY_Control_L || e->keyval == GDK_KEY_Control_R))
		guarded("switching tabs", [&] { w->switcher->release(); });
	return FALSE;
}

gboolean CircuitWindow::stateCb(GtkWidget*, GdkEventWindowState* e, gpointer) {
	prefs().windowMaximized = (e->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) != 0;
	return FALSE;
}

void CircuitWindow::sizeCb(GtkWidget* widget, GdkRectangle*, gpointer self) {
	// Focus mode's window has no title bar: its size isn't the window's own.
	if (prefs().windowMaximized || static_cast<CircuitWindow*>(self)->focusOn) return;
	int w = 0, h = 0;
	gtk_window_get_size(GTK_WINDOW(widget), &w, &h);
	if (w > 0 && h > 0) { prefs().windowWidth = w; prefs().windowHeight = h; }
}

// Circuits save themselves, so closing doesn't ask: it saves. Only when that
// fails is there a question.
bool CircuitWindow::confirmClose() {
	if (!isDirty()) return true;
	if (saveQuietly(false)) return true;
	return askConfirm(GTK_WINDOW(win), "This circuit couldn't be saved", "Close it anyway? The changes since it last saved will be lost.",
	                  "Close Anyway", "Cancel", true);
}

// ---- Files ---------------------------------------------------------------------

void CircuitWindow::replaceDocument(CLDocument* newDoc, const std::string& newPath) {
	for (Canvas* c : canvases) c->cancelDrag();
	syncing = true;
	for (GtkWidget* nb : notebooks)
		while (gtk_notebook_get_n_pages(GTK_NOTEBOOK(nb)) > 0) gtk_notebook_remove_page(GTK_NOTEBOOK(nb), 0);
	sideKeys.clear();
	focusPane = 0;
	for (Canvas* c : canvases) delete c;
	canvases.clear();
	syncing = false;
	if (scope) { delete scope; scope = nullptr; }
	cl_document_close(doc);
	doc = newDoc;
	path = newPath;
	forceDirty = false;
	recovery::remove(recoveryBase);
	recoveredName.clear();
	changesAtRecovery = changes;
	pendingGate.clear();
	selectionSignature.clear();
	seenPages.clear();
	isRunning = cl_document_is_running(doc);
	simViewOn = false;
	lockedOn = false;
	syncTabs();
	appearStart = g_get_monotonic_time();
	beginOpening();
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
	recoveredName.clear();
	// Saved: the recovery copy isn't needed until the next change.
	recovery::remove(recoveryBase);
	changesAtRecovery = changes;
	prefs().noteRecent(file);
	updateTitle();
	note("Saved.");
	return true;
}

bool CircuitWindow::save() { return saveQuietly(true); }

static GtkFileFilter* cdlFilter() {
	GtkFileFilter* f = gtk_file_filter_new();
	gtk_file_filter_set_name(f, "CedarLogic circuits (*.cdl)");
	gtk_file_filter_add_pattern(f, "*.cdl");
	gtk_file_filter_add_pattern(f, "*.CDL");
	return f;
}

std::string chooseSavePath(GtkWindow* parent, const char* title, const std::string& suggested, GtkFileFilter* filter, const char* ext) {
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
	if (!out.empty()) {
		gchar* dir = g_path_get_dirname(out.c_str());
		prefs().lastFolder = dir;   // the next dialog starts here
		g_free(dir);
	}
	if (!out.empty() && ext) {
		// Add the extension when it was left off (other dots don't count:
		// "lab3 v1.2"). The dialog asked about replacing the name as typed, so
		// a file with the extension added is asked about here.
		const size_t n = strlen(ext);
		if (out.size() < n || g_ascii_strcasecmp(out.c_str() + out.size() - n, ext) != 0) {
			out += ext;
			if (g_file_test(out.c_str(), G_FILE_TEST_EXISTS)) {
				gchar* base = g_path_get_basename(out.c_str());
				const std::string heading = format("“%s” already exists", base);
				g_free(base);
				if (!askConfirm(parent, heading, "Replace it? What's in it now will be lost.", "Replace", "Cancel", true)) return "";
			}
		}
	}
	return out;
}

std::string chooseSaveFile(GtkWindow* parent, const std::string& title, const std::string& suggested) {
	return chooseSavePath(parent, title.c_str(), suggested, cdlFilter(), ".cdl");
}

// A copy of the circuit as a .cdl file, anywhere.
bool CircuitWindow::exportCopy() {
	const std::string file = chooseSavePath(GTK_WINDOW(win), "Export", displayName() + ".cdl", cdlFilter(), ".cdl");
	if (file.empty()) return false;
	const bool wasDirty = isDirty();
	const std::string text = cl_document_save_text(doc);
	forceDirty = forceDirty || wasDirty;   // asking for the text marked it saved
	GError* e = nullptr;
	if (!g_file_set_contents(file.c_str(), text.data(), (gssize)text.size(), &e)) {
		showMessage(GTK_WINDOW(win), GTK_MESSAGE_ERROR, "The circuit couldn't be exported", e ? e->message : "");
		if (e) g_error_free(e);
		return false;
	}
	note("Exported " + baseName(file) + ".cdl.");
	return true;
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

// Export as Image: ExportImage.cpp.
void CircuitWindow::exportImage() { showExportImage(this, currentPage()); }

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
			showPage(show);
			appearStart = g_get_monotonic_time();
		}
	}
	edited();
}

void CircuitWindow::undo() {
	if (simViewOn) return;
	// Gates still on the pointer (a paste, a duplicate, a new gate): undo
	// takes them back, as Escape does, and ends the float with them.
	if (isFloating()) { cancelFloating(); return; }
	if (Canvas* c = currentCanvas()) if (c->isDragging()) c->cancelDrag();
	if (cl_edit_undo(doc)) refreshAfterHistory();
	else gtk_widget_error_bell(win);
}

void CircuitWindow::redo() {
	if (simViewOn) return;
	// Nothing to redo past gates still on the pointer (placing them is next).
	if (isFloating()) { gtk_widget_error_bell(win); return; }
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
	guarded("pasting", [&] { req->window->pasteText(text, true, req->shift); });
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
	// One of My Parts: its gates and wires, pasted, on the pointer.
	if (parts::isPart(name)) {
		parts::Part p;
		if (!parts::find(name, p)) return false;
		const std::string text = p.text();
		const char* back = nullptr;
		if (!cl_edit_paste(doc, currentPage(), text.c_str(), wx, wy, true, &back)) return false;
		floatSelection(wx, wy);
		redraw();
		return true;
	}
	if (!cl_edit_add_gate(doc, currentPage(), name.c_str(), wx, wy)) return false;
	floatSelection(wx, wy);
	redraw();
	return true;
}

bool CircuitWindow::buildPlan(const formula::Plan& plan, bool onNewPage, const std::string& newPageName) {
	if (plan.parts.empty()) return false;
	if (!canEdit()) { lockNudge(); return false; }
	int target = currentPage();
	double dx = 0, dy = 0;
	if (onNewPage) {
		const int i = cl_document_add_page(doc);
		if (i < 0) return false;
		target = i;
		if (!newPageName.empty()) cl_document_rename_page(doc, i, newPageName.c_str());
	} else {
		double l, b, r, t;
		if (cl_document_page_bounds(doc, target, &l, &b, &r, &t)) {
			double minX = 1e9, maxY = -1e9;
			for (const formula::Plan::Part& p : plan.parts) { minX = std::min(minX, p.x); maxY = std::max(maxY, p.y); }
			dx = r + 16 - minX;
			dy = t - maxY;
		}
	}
	std::vector<CLBuildGate> gates;
	for (const formula::Plan::Part& p : plan.parts)
		gates.push_back({ p.gate.c_str(), p.x + dx, p.y + dy, p.label.empty() ? nullptr : p.label.c_str() });
	std::vector<CLBuildWire> wires;
	for (const formula::Plan::Wire& w : plan.wires) wires.push_back({ w.from, w.fromPin.c_str(), w.to, w.toPin.c_str() });
	if (cl_edit_build(doc, target, gates.data(), (int)gates.size(), wires.empty() ? nullptr : wires.data(), (int)wires.size(),
	                  "Build from Formula") <= 0)
		return false;
	if (onNewPage) {
		syncTabs();
		showPage(target);
		appearStart = g_get_monotonic_time();
	}
	edited();
	if (Canvas* c = currentCanvas()) c->zoomToFit(true);
	note(plan.summary());
	return true;
}

std::vector<int> CircuitWindow::recentTabs() const {
	std::vector<int> out;
	const int current = currentTab();
	if (current >= 0 && current < (int)canvases.size()) out.push_back(current);
	for (uint64_t key : recentKeys)
		for (int i = 0; i < (int)canvases.size(); i++)
			if (canvases[i]->pageKey() == key && std::find(out.begin(), out.end(), i) == out.end()) out.push_back(i);
	for (int i = 0; i < (int)canvases.size(); i++)
		if (std::find(out.begin(), out.end(), i) == out.end()) out.push_back(i);
	return out;
}

void CircuitWindow::showFoundGate(int page, long gate, double x, double y) {
	for (int i = 0; i < (int)canvases.size(); i++)
		if (canvases[i]->page() == page) showTab(i);
	cl_edit_select_gate(doc, page, gate);
	if (Canvas* c = currentCanvas()) c->panTo(x, y);
	selectionChanged();
	redraw();
}

void CircuitWindow::find() {
	// A label or TO/FROM selected: look for its name.
	const long g = cl_edit_single_gate(doc, currentPage());
	const char* name = g >= 0 ? cl_gate_find_name(doc, g) : nullptr;
	findBar->open(name ? name : "");
}

void CircuitWindow::partsChanged() {
	if (palette) palette->partsChanged();
}

void CircuitWindow::startAs(const std::string& name) {
	recoveredName = name;
	forceDirty = true;
	saveQuietly(false);
	updateTitle();
	note("A new circuit from \u201C" + name + "\u201D, in Your Circuits.");
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
	guarded("disconnecting", [&] {
		cl_edit_disconnect_pin(mp->w->document(), mp->w->currentPage(), mp->x, mp->y, c->unitsPerPoint());
		mp->w->edited();
	});
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
	g_signal_connect(menu, "deactivate", CL_CALLBACK(+[](GtkMenuShell* m, gpointer) {
		// Destroy it once its item has run.
		g_idle_add([](gpointer m) -> gboolean { gtk_widget_destroy(GTK_WIDGET(m)); return G_SOURCE_REMOVE; }, m);
	}), nullptr);
	// From the keyboard: in the middle of the canvas, its first item picked.
	Canvas* c = currentCanvas();
	if (e == nullptr && c) {
		gtk_menu_popup_at_widget(GTK_MENU(menu), c->widget(), GDK_GRAVITY_CENTER, GDK_GRAVITY_NORTH_WEST, nullptr);
		gtk_menu_shell_select_first(GTK_MENU_SHELL(menu), FALSE);
	} else {
		gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*)e);
	}
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

int CircuitWindow::stepMs() const { return cl_document_step_ms(doc); }

void CircuitWindow::setStepMs(int ms) {
	if (ms == cl_document_step_ms(doc)) return;
	cl_document_set_step_ms(doc, ms);
	if (toolbar) toolbar->redraw();
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

// The oscilloscope docks under the canvas, as on the Mac (Ctrl+G again, or
// its ×, puts it away).
void CircuitWindow::toggleScope() {
	if (scope && scope->visible()) { scope->close(); if (Canvas* c = currentCanvas()) gtk_widget_grab_focus(c->widget()); return; }
	if (scope == nullptr) {
		scope = new ScopeWindow(this);
		gtk_paned_pack2(GTK_PANED(scopePaned), scope->widget(), FALSE, FALSE);
	}
	scope->present();
	const int h = gtk_widget_get_allocated_height(scopePaned);
	if (h > 400) gtk_paned_set_position(GTK_PANED(scopePaned), h - 220);
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
	// A new tab opens in the side you're working in.
	if (focusPane == 1 && splitOpen()) { sideKeys.insert(cl_document_page_id(doc, i)); syncTabs(); }
	showPage(i);
	appearStart = g_get_monotonic_time();
	edited();
}

void CircuitWindow::closePage(int page) {
	if (cl_document_page_count(doc) < 2) return;
	// A tab with work on it asks first (wx CloseTabCanvas).
	if (cl_document_gate_count(doc, page) > 0 &&
	    !askConfirm(GTK_WINDOW(win), "Close this tab?", "All work on this tab will be lost. Ctrl+Shift+T brings it back.", "Close Tab",
	                "Cancel", true))
		return;
	for (Canvas* c : canvases) c->cancelDrag();
	cl_edit_select_none(doc, page);
	if (cl_document_close_page(doc, page)) {
		const int show = std::min(cl_document_page_to_show(doc), cl_document_page_count(doc) - 1);
		syncTabs();
		if (show >= 0) showPage(show);
		edited();
	}
}

void CircuitWindow::reopenPage() {
	if (!cl_edit_undo_is_close_page(doc)) { note("No closed tab to reopen."); gtk_widget_error_bell(win); return; }
	undo();
	appearStart = g_get_monotonic_time();
	note("Reopened the closed tab.");
}

// Renamed in place, on its tab (the Mac's).
void CircuitWindow::renamePage(int page) {
	if (page < 0 || page >= (int)canvases.size()) return;
	strips[paneOf(canvases[page])]->beginRename(page);
}

void CircuitWindow::cyclePage(int delta) {
	// Through the tabs of the side you're working in.
	const std::vector<int> order = panePages(splitOpen() ? focusPane : 0);
	const int n = (int)order.size();
	if (n < 2) return;
	const int at = (int)(std::find(order.begin(), order.end(), currentPage()) - order.begin());
	showPage(order[((at + delta) % n + n) % n]);
}

// ---- Focus mode ----------------------------------------------------------------
// The Mac's: the toolbar and the side panel slide away, and the tab strips
// become the window's top row (dragging it, with its buttons).

void CircuitWindow::toggleFocusMode() {
	focusOn = !focusOn;
	gtk_revealer_set_reveal_child(GTK_REVEALER(titleRevealer), !focusOn);
	gtk_revealer_set_reveal_child(GTK_REVEALER(sideRevealer), !focusOn && prefs().showPalette);
	for (TabStrip* t : strips) if (t) t->setTitleRow(focusOn);
	updateActions();
	if (focusOn) note("Focus mode. Ctrl+. brings the toolbar and the side panel back.");
	if (Canvas* c = currentCanvas()) gtk_widget_grab_focus(c->widget());
}

// ---- The side panel's drag --------------------------------------------------------

Canvas* CircuitWindow::canvasUnder(GtkWidget* from, double x, double y, double& cx, double& cy) const {
	Canvas* nearest = nullptr;
	int nearestLeft = 1 << 30, nx = 0, ny = 0;
	for (int pane = 0; pane < 2; pane++) {
		if (pane == 1 && !splitOpen()) continue;
		Canvas* c = paneCanvas(pane);
		if (c == nullptr || !gtk_widget_get_realized(c->widget())) continue;
		int tx = 0, ty = 0;
		if (!gtk_widget_translate_coordinates(from, c->widget(), (int)x, (int)y, &tx, &ty)) continue;
		if (tx >= 0 && ty >= 0 && tx < c->width() && ty < c->height()) { cx = tx; cy = ty; return c; }
		// Over the panel: the side next to it.
		int ox = 0, oy = 0;
		gtk_widget_translate_coordinates(c->widget(), win, 0, 0, &ox, &oy);
		if (ox < nearestLeft) { nearestLeft = ox; nearest = c; nx = tx; ny = ty; }
	}
	cx = nx;
	cy = ny;
	return nearest;
}

bool CircuitWindow::addGateFloatingOn(Canvas* c, const std::string& name, double wx, double wy) {
	if (c == nullptr) return false;
	const int p = c->page();
	if (p < 0) return false;
	showPage(p);
	return addGateFloating(name, wx, wy);
}

// ---- App-wide ------------------------------------------------------------------

void CircuitWindow::toggleDark() {
	prefs().dark = !prefs().dark;
	if (prefs().themeMode == 0) prefs().themeMode = 3;   // the choice sticks
	prefs().save();
	applyTheme();
}

void CircuitWindow::togglePalette() {
	if (focusOn) { toggleFocusMode(); if (prefs().showPalette) return; }
	prefs().showPalette = !prefs().showPalette;
	prefs().save();
	for (CircuitWindow* w : circuitWindows()) w->prefsChanged();
}

void CircuitWindow::showPreferences() { settings::show(this); }
void CircuitWindow::showShortcuts() { showShortcutsWindow(GTK_WINDOW(win)); }

void CircuitWindow::showHelp() { help::show(this); }

// About: a card in the brand's look (the launch screen's colours).
void CircuitWindow::showAbout() {
	Sheet sh;
	sh.title = "About CedarLogic";
	sh.width = 460;
	sh.height = 420;
	sh.minWidth = 460;
	sh.minHeight = 420;
	sh.resizable = false;
	const double opened = anim::now();
	sh.animating = true;
	std::string after;
	sh.paint = [&](Sheet& s, cairo_t* cr, float w, float h) {
		const double t = anim::now() - opened;
		brand::ground(cr, w, h, 0.5f, 0.22f, 26);
		const float k = (float)anim::easeOut(t / 0.5);
		brand::icon(cr, w / 2 - 48, 40 + 8 * (1 - k), 96, 0.55f * k);
		brand::text(cr, "CedarLogic", 0, 152, 28, brand::Bold, brand::kPrimary, w, TextAlign::Center);
		brand::text(cr, std::string("Version ") + CL_VERSION + "  ·  native Linux test build  ·  " + std::string(CL_GIT_COMMIT).substr(0, 7), 0,
		            192, 12, brand::Medium, brand::kNeon, w, TextAlign::Center);
		brand::text(cr, "A digital logic simulator, from Cedarville University. Rebuilt natively for Linux: GTK and Cairo on the shared "
		                "CedarLogic engine, with no OpenGL, so it runs the same on a Raspberry Pi as on a PC.",
		            40, 222, 13, brand::Normal, brand::kSecondary, w - 80, TextAlign::Center);
		const RectF site = rectF(w / 2 - 170, h - 70, w / 2 - 60, h - 36);
		const RectF news = rectF(w / 2 - 50, h - 70, w / 2 + 60, h - 36);
		const RectF close = rectF(w / 2 + 70, h - 70, w / 2 + 170, h - 36);
		brand::button(cr, site, "Website", false, s.hotNext());
		s.hit(site, [&] { after = "site"; s.close(); });
		brand::button(cr, news, "What's New", false, s.hotNext());
		s.hit(news, [&] { after = "news"; s.close(); });
		brand::button(cr, close, "Close", true, s.hotNext());
		s.hit(close, [&s] { s.close(); });
		if (t > 0.6) s.animating = false;
	};
	sh.onKey = [](Sheet& s, guint k, guint) -> bool {
		if (k == GDK_KEY_Return || k == GDK_KEY_KP_Enter) { s.close(); return true; }
		return false;
	};
	sh.run(GTK_WINDOW(win));
	if (after == "site") openExternally(GTK_WINDOW(win), "https://cedarlogic.netlify.app");
	else if (after == "news") whatsnew::show(this);
}

void CircuitWindow::rebuildRecentMenu() {}
