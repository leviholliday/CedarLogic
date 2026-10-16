// CedarLogic for Linux (native): the application -- its menus and shortcuts,
// opening circuits, and the list of open windows.

#include <deque>

#include "App.h"
#include "Bench.h"
#include "Canvas.h"
#include "Recovery.h"
#include "ShareLink.h"
#include "Splash.h"
#include "Updater.h"
#include "Welcome.h"
#include "Collections.h"
#include "Dialogs.h"
#include "Feedback.h"
#include "Help.h"
#include "QuitConfirm.h"
#include "Settings.h"
#include "Shortcuts.h"
#include "SyncApp.h"
#include "SyncPlatform.h"
#include "SyncUI.h"
#include "Library.h"
#include "LibraryWindow.h"
#include "FindBar.h"
#include "Notes.h"
#include "Window.h"

#include <algorithm>
#include <glib/gstdio.h>

#include <clocale>
#include <cmath>
#include <cstring>
#include <functional>

namespace {

GMenu* gRecentMenu = nullptr;
bool gLibraryLoaded = false;
GtkWidget* gSplash = nullptr;
// --screenshot <out.png>: once the window is up, draw it to a PNG and quit
// (a check that a build really starts, drawing and all; used by CI).
std::string gScreenshot;
int gExitCode = 0;
// For the screenshot runs: --dark or --light for this run, --sim-view on.
int gTheme = -1;
bool gSimView = false;
// --show <what>: for the screenshot runs, a window to open and picture
// instead of the circuit's (welcome:N, whatsnew:N, help, truth, feedback,
// templates, tour, find).
std::string gShow;
// --lab-report <in.cdl> <out.pdf> [bw]: writes a lab report without a window.
std::string gReportIn, gReportOut;
bool gReportBW = false;
// --splash-frame <t> <out.png> [--first]: the launch screen at t seconds.
double gSplashAt = -1;
std::string gSplashFile;
bool gFirstLaunch = false;
// --hold <seconds>: wait this long before the screenshot (the drag test moves the pointer meanwhile).
int gHold = 2;
// This process is the running CedarLogic (not a launch that handed over to
// it): it loaded the settings, so it may save them.
bool gPrimary = false;
// --bench-drag [steps] <circuit.cdl>: times the canvas while dragging and
// quits (Bench.cpp, linux/SMOOTHNESS.md). It runs as a screenshot run does
// (no splash, updater or sync), with its settings and Your Circuits in a
// temporary folder, so the person's own are never touched.
int gBench = 0;
std::string gBenchHome;

// A window's picture, as a PNG.
bool writeWindow(GtkWidget* top, const std::string& file) {
	const int width = gtk_widget_get_allocated_width(top), height = gtk_widget_get_allocated_height(top);
	cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, std::max(1, width), std::max(1, height));
	cairo_t* cr = cairo_create(s);
	gtk_widget_draw(top, cr);
	cairo_destroy(cr);
	const bool ok = cairo_surface_write_to_png(s, file.c_str()) == CAIRO_STATUS_SUCCESS;
	cairo_surface_destroy(s);
	fprintf(stderr, "%s %s (%dx%d)\n", ok ? "wrote" : "couldn't write", file.c_str(), width, height);
	return ok;
}

GtkWidget* toplevelTitled(const char* title) {
	GtkWidget* found = nullptr;
	GList* all = gtk_window_list_toplevels();
	for (GList* l = all; l; l = l->next) {
		GtkWindow* w = GTK_WINDOW(l->data);
		const char* t = gtk_window_get_title(w);
		if (t && strcmp(t, title) == 0 && gtk_widget_get_visible(GTK_WIDGET(w))) found = GTK_WIDGET(w);
	}
	g_list_free(all);
	return found;
}

// The window with its open menu over it, where the menu is on screen.
bool writeWindowWithMenu(GtkWidget* top, const std::string& file) {
	const int width = gtk_widget_get_allocated_width(top), height = gtk_widget_get_allocated_height(top);
	cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, std::max(1, width), std::max(1, height));
	cairo_t* cr = cairo_create(s);
	gtk_widget_draw(top, cr);
	int tx = 0, ty = 0;
	gdk_window_get_origin(gtk_widget_get_window(top), &tx, &ty);
	GList* all = gtk_window_list_toplevels();
	for (GList* l = all; l; l = l->next) {
		GtkWidget* pop = GTK_WIDGET(l->data);
		GtkWidget* child = gtk_bin_get_child(GTK_BIN(pop));
		if (!gtk_widget_get_visible(pop) || !child || !GTK_IS_MENU(child)) continue;
		if (!gtk_menu_shell_get_selected_item(GTK_MENU_SHELL(child))) gtk_menu_shell_select_first(GTK_MENU_SHELL(child), FALSE);
		int px = 0, py = 0;
		gdk_window_get_origin(gtk_widget_get_window(pop), &px, &py);
		cairo_save(cr);
		cairo_translate(cr, px - tx, py - ty);
		gtk_widget_draw(pop, cr);
		cairo_restore(cr);
	}
	g_list_free(all);
	cairo_destroy(cr);
	const bool ok = cairo_surface_write_to_png(s, file.c_str()) == CAIRO_STATUS_SUCCESS;
	cairo_surface_destroy(s);
	fprintf(stderr, "%s %s (%dx%d)\n", ok ? "wrote" : "couldn't write", file.c_str(), width, height);
	return ok;
}

GtkWidget* toplevelNamed(const char* name) {
	GtkWidget* found = nullptr;
	GList* all = gtk_window_list_toplevels();
	for (GList* l = all; l; l = l->next)
		if (strcmp(gtk_widget_get_name(GTK_WIDGET(l->data)), name) == 0 && gtk_widget_get_visible(GTK_WIDGET(l->data))) found = GTK_WIDGET(l->data);
	g_list_free(all);
	return found;
}

// The window --show opened, pictured; then the app ends (a modal window may
// be running its own loop, so this doesn't wait for it).
gboolean showCaptureCb(gpointer) {
	GtkWidget* top = nullptr;
	const std::string what = gShow.substr(0, gShow.find(':'));
	if (what == "welcome") top = welcome::window();
	else if (what == "whatsnew") top = whatsnew::window();
	else if (what == "help") top = help::window();
	else if (what == "truth") top = toplevelTitled("Truth Table");
	else if (what == "checkclocked") top = toplevelTitled("Check My Circuit") ? toplevelTitled("Check My Circuit") : toplevelTitled("Truth Table");
	else if (what == "feedback") top = toplevelTitled("Send Feedback");
	else if (what == "templates") top = toplevelTitled("New from Template");
	else if (what == "export") top = toplevelTitled("Export as Image");
	else if (what == "report") top = toplevelTitled("Export Lab Report");
	else if (what == "settings" || what == "sync") top = settings::window();
	else if (what == "synccode") top = toplevelTitled("Your sync code");
	else if (what == "synclink" || what == "syncpair") top = toplevelTitled("Link this computer");
	else if (what == "syncconfirm") top = toplevelTitled("Link this computer?");
	else if (what == "scope" && !circuitWindows().empty()) top = GTK_WIDGET(circuitWindows().back()->window());
	else if (what == "quit") top = toplevelTitled("Quit CedarLogic");
	else if (what == "shortcuts") top = toplevelTitled("Keyboard Shortcuts");
	else if (what == "quickadd") top = toplevelTitled("Add a Gate");
	else if (what == "scope") top = toplevelTitled("Oscilloscope");
	else if (what == "about") top = toplevelTitled("About CedarLogic");
	else if (what == "rename") top = toplevelTitled("Rename Circuit");
	else if (what == "ram") top = toplevelNamed("cl-sheet-ram");
	else if (what == "gatesettings") top = toplevelNamed("gate-settings");
	else if (!circuitWindows().empty()) top = GTK_WIDGET(circuitWindows().back()->window());
	if (what == "combo") top = settings::window();
	const bool ok = top && (what == "menu" || what == "submenu" || what == "combo" ? writeWindowWithMenu(top, gScreenshot) : writeWindow(top, gScreenshot));
	if (!top) fprintf(stderr, "nothing to picture for --show %s\n", gShow.c_str());
	prefs().save();
	fflush(stderr);
	_exit(ok ? 0 : 1);
	return G_SOURCE_REMOVE;
}

gboolean showCb(gpointer) {
	CircuitWindow* w = circuitWindows().empty() ? nullptr : circuitWindows().back();
	if (w == nullptr) return G_SOURCE_REMOVE;
	const size_t colon = gShow.find(':');
	const std::string what = gShow.substr(0, colon);
	const int page = colon == std::string::npos ? 0 : atoi(gShow.c_str() + colon + 1);
	g_timeout_add(1800, showCaptureCb, nullptr);
	if (what == "welcome") { prefs().hasSeenWelcome = false; welcome::offer(w); welcome::pageForScreenshot(page); }
	else if (what == "whatsnew") whatsnew::show(w, page);
	else if (what == "help") help::show(w, page > 0 ? "analysis" : "");
	else if (what == "truth") w->makeTruthTable();
	else if (what == "checkclocked") {
		// Check My Circuit clock pulse by clock pulse, on a counter (CI opens
		// the 3-bit counter template): :0 a count it matches, :1 one it
		// doesn't (the strip marks the first wrong step), :2 a state table.
		const char* keys[] = { "0, 1, 2, 3, 4, 5, 6, 7, repeat", "0, 2, 4, 6, repeat",
		                       "Q2 Q1 Q0 | Q2+ Q1+ Q0+\n0 0 0 | 0 0 1\n0 0 1 | 0 1 0\n0 1 0 | 0 1 1\n0 1 1 | 1 0 0" };
		setCheckForScreenshot(keys[std::max(0, std::min(2, page))]);
		fprintf(stderr, "check (clocked): this page %s be checked clock pulse by clock pulse\n",
		        cl_check_clocked_page(w->document(), w->currentPage()) ? "can" : "can't");
		w->checkCircuit();
	}
	else if (what == "feedback") feedback::show(w);
	else if (what == "templates") templates::showPicker(w, page == 1 ? "builtin-ff-d" : "");
	else if (what == "tour") welcome::startTour(w);
	else if (what == "find" && page == 1) {
		// Find Next and Find Previous: "A" on the practice circuit, then
		// Next twice and Previous once, each reported.
		w->runAction("win.find");
		if (FindBar* fb = w->findBarWidget()) {
			fb->open("A");
			fprintf(stderr, "find: %d hit(s), at %d\n", fb->count(), fb->current() + 1);
			w->runAction("win.find-next");
			w->runAction("win.find-next");
			fprintf(stderr, "find: Next twice, at %d\n", fb->current() + 1);
			w->runAction("win.find-previous");
			fprintf(stderr, "find: Previous once, at %d\n", fb->current() + 1);
		}
	}
	else if (what == "find") w->runAction("win.find");
	else if (what == "export") w->exportImage();
	else if (what == "report") w->exportReport();
	else if (what == "settings") settings::show(w, page);
	else if (what == "sync" || what == "synccode" || what == "synclink" || what == "syncpair" || what == "syncconfirm") syncui::showForScreenshot(w, what, page);
	else if (what == "quit") confirmQuitting(w->window());
	else if (what == "focus") w->toggleFocusMode();
	else if (what == "shortcuts") w->showShortcuts();
	else if (what == "quickadd") w->quickAdd();
	else if (what == "sim") w->toggleSimView();
	else if (what == "lock") w->toggleLock();
	else if (what == "split") w->toggleSplit();
	else if (what == "toolbar") { prefs().toolbarStyle = page; w->prefsChanged(); }
	else if (what == "dark-split") { w->toggleSplit(); }
	else if (what == "scope") w->toggleScope();
	else if (what == "about") w->showAbout();
	else if (what == "rename") w->renameFile();
	else if (what == "combo") {
		// Settings' first dropdown, open.
		settings::show(w, 0);
		g_timeout_add(700, +[](gpointer) -> gboolean {
			std::function<GtkWidget*(GtkWidget*)> find = [&](GtkWidget* x) -> GtkWidget* {
				if (GTK_IS_COMBO_BOX(x) && gtk_widget_get_mapped(x)) return x;
				if (!GTK_IS_CONTAINER(x)) return nullptr;
				GList* kids = gtk_container_get_children(GTK_CONTAINER(x));
				GtkWidget* got = nullptr;
				for (GList* l = kids; l && !got; l = l->next) got = find(GTK_WIDGET(l->data));
				g_list_free(kids);
				return got;
			};
			if (GtkWidget* sw = settings::window())
				if (GtkWidget* combo = find(sw)) gtk_combo_box_popup(GTK_COMBO_BOX(combo));
			return G_SOURCE_REMOVE;
		}, nullptr);
	}
	else if (what == "submenu") {
		// The ••• menu with Tabs open, over Simulation View, as on the Pi.
		w->toggleSimView();
		GtkWidget* top = GTK_WIDGET(w->window());
		GdkRectangle r = { gtk_widget_get_allocated_width(top) / 2, 6, 32, 32 };   // room for the submenu in the picture
		w->moreMenu(top, r, nullptr);
		g_timeout_add(500, +[](gpointer) -> gboolean {
			GList* all = gtk_window_list_toplevels();
			for (GList* l = all; l; l = l->next) {
				GtkWidget* child = gtk_bin_get_child(GTK_BIN(l->data));
				if (!gtk_widget_get_visible(GTK_WIDGET(l->data)) || !child || !GTK_IS_MENU(child)) continue;
				GList* items = gtk_container_get_children(GTK_CONTAINER(child));
				for (GList* i = items; i; i = i->next) {
					const char* label = gtk_menu_item_get_label(GTK_MENU_ITEM(i->data));
					if (label && strstr(label, "Tabs")) {
						gtk_menu_shell_select_item(GTK_MENU_SHELL(child), GTK_WIDGET(i->data));
						g_signal_emit_by_name(i->data, "activate-item");   // opens its submenu now
					}
				}
				g_list_free(items);
			}
			g_list_free(all);
			return G_SOURCE_REMOVE;
		}, nullptr);
	}
	else if (what == "menu") {
		GtkWidget* top = GTK_WIDGET(w->window());
		GdkRectangle r = { gtk_widget_get_allocated_width(top) - 48, 6, 32, 32 };
		w->moreMenu(top, r, nullptr);
	}
	else if (what == "ram") {
		// An 8x8 RAM with something in it.
		CLDocument* doc = w->document();
		const int pg = w->currentPage();
		if (cl_edit_add_gate(doc, pg, "AE_RAM_8x8", 40, -20)) {
			const long g = cl_edit_single_gate(doc, pg);
			const unsigned long values[] = { 0x3C, 0x42, 0x81, 0xA5, 0xFF, 0x18, 0x7E, 0x01, 0x80, 0x55, 0xAA };
			for (unsigned long a = 0; a < 11; a++) cl_ram_set(doc, g, a * 3, values[a]);
			if (g >= 0) w->showRam(g);
		}
	}
	else if (what == "gatesettings") {
		// A clock: it has settings of each kind.
		CLDocument* doc = w->document();
		if (cl_edit_add_gate(doc, w->currentPage(), "BB_CLOCK", 40, -20)) w->showSettings();
	}
	else if (what == "stepclock") {
		// A clock set to "Only on Step Clock": Step Clock three times, then a
		// click on the clock (the canvas's own calls), each reported; the
		// picture shows the toolbar's Step Clock button on (:1 in Simulation
		// View, with the bar's).
		CLDocument* doc = w->document();
		const int pg = w->currentPage();
		if (cl_edit_add_gate(doc, pg, "BB_CLOCK", 40, -20)) {
			const long g = cl_edit_single_gate(doc, pg);
			cl_gate_set_setting(doc, g, "MANUAL", "true");
			cl_edit_select_none(doc, pg);
			w->edited();
		}
		fprintf(stderr, "step clock: %d manual clock(s) on the page\n", cl_document_manual_clock_count(doc, pg));
		for (int i = 1; i <= 3; i++) fprintf(stderr, "step clock %d: %s\n", i, cl_document_clock_step(doc, pg) ? "stepped" : "nothing to step");
		const double upp = w->currentCanvas() ? w->currentCanvas()->unitsPerPoint() : 0.05;
		cl_edit_press(doc, pg, 40, -20, 0, upp);
		const int click = cl_edit_release_ex(doc, 40, -20, true);
		fprintf(stderr, "step clock: a click on the clock %s\n", click == CL_CLICK_CLOCK_STEP ? "stepped it" : "did not step it");
		w->clockStepped();
		w->edited();
		if (page == 1) w->toggleSimView();
	}
	else if (what == "pagelinks") {
		// Page links: every page on its own, then the first two connected
		// (their tabs share a dot), each reported.
		CLDocument* doc = w->document();
		while (cl_document_page_count(doc) < 3) cl_document_add_page(doc);
		w->disconnectAllPages();
		fprintf(stderr, "page links: none linked %d\n", cl_pages_none_linked(doc));
		w->setPagesLinked(0, 1, true);
		fprintf(stderr, "page links: 1-2 linked %d, 1-3 linked %d, marks %d %d %d\n", cl_pages_linked(doc, 0, 1), cl_pages_linked(doc, 0, 2),
		        cl_page_link_mark(doc, 0), cl_page_link_mark(doc, 1), cl_page_link_mark(doc, 2));
		w->edited();
	}
	else if (what == "draw") {
		// Drawing on the circuit: Draw mode on, a pen stroke and a highlighter
		// stroke round the circuit (through the engine, as the canvas makes
		// them), saved and opened again, each reported. :0 pictures the
		// drawing bar; :1 the drawing hidden (the Draw button's dot) and the
		// notes pane with notes, which Share Link leaves out; :2 the eraser
		// takes a stroke away and Undo brings it back.
		CLDocument* doc = w->document();
		const int pg = w->currentPage();
		double l = -20, b = -15, r = 20, t = 15;
		cl_document_page_bounds(doc, pg, &l, &b, &r, &t);
		const double upp = w->currentCanvas() ? w->currentCanvas()->unitsPerPoint() : 0.05;
		w->setDrawing(true);
		auto stroke = [&](int tool, const char* color, double width, double y0, double y1) {
			cl_ink_begin(doc, pg, tool, color, width, upp);
			for (int i = 0; i <= 40; i++) {
				const double f = i / 40.0;
				CLInkPoint p = { l + (r - l) * f, y0 + (y1 - y0) * f + std::sin(f * 9) * 1.2, -1 };
				cl_ink_add(doc, &p, 1);
			}
			if (cl_ink_end(doc) > 0) w->inkEdited();
		};
		stroke(CL_INK_HIGHLIGHTER, "yellow", 1.2, (b + t) / 2, (b + t) / 2);
		stroke(CL_INK_PEN, "red", 0.25, b - 1, t + 1);
		fprintf(stderr, "drawing: %d stroke(s) on the page, %ld point(s)\n", cl_ink_stroke_count(doc, pg), cl_ink_point_count(doc, -1));
		const std::string saved = cl_document_save_text_ex(doc, 0);
		char err[256] = "";
		if (CLDocument* again = cl_document_open_text(saved.c_str(), (long)saved.size(), err, sizeof err)) {
			fprintf(stderr, "drawing: saved and opened again: %d stroke(s)\n", cl_ink_stroke_count(again, pg));
			cl_document_close(again);
		} else {
			fprintf(stderr, "drawing: saved, but it couldn't be opened again: %s\n", err);
		}
		if (page == 1) {
			w->setDrawingShown(false);
			fprintf(stderr, "drawing: hidden %d, Draw mode %d\n", !cl_ink_shown(doc), w->drawing());
			cl_notes_set(doc, "Q1: the output is high only when both inputs are.\nTry the clock at a slower speed next time.");
			w->notesPane()->load();
			w->toggleNotes();
			const std::string share = cl_document_save_text_ex(doc, CL_SAVE_NO_NOTES);
			fprintf(stderr, "notes: %zu character(s); Share Link's copy leaves them out: %s\n", strlen(cl_notes(doc)),
			        share.find("both inputs") == std::string::npos ? "yes" : "NO");
		} else if (page == 2) {
			const int before = cl_ink_stroke_count(doc, pg);
			cl_ink_erase_begin(doc, pg);
			for (int i = 0; i <= 20; i++) cl_ink_erase_to(doc, l + (r - l) * i / 20.0, b - 1 + (t - b + 2) * i / 20.0, 1.0);
			const int erased = cl_ink_erase_end(doc);
			w->inkEdited();
			fprintf(stderr, "eraser: %d of %d stroke(s) erased, %d left\n", erased, before, cl_ink_stroke_count(doc, pg));
			w->undo();
			fprintf(stderr, "eraser: after Undo, %d stroke(s)\n", cl_ink_stroke_count(doc, pg));
			w->setInkTool(2);
		}
		w->redraw();
	}
	else if (what == "lockinplace") {
		// Every part locked in place (a lock on each), then a nudge that
		// leaves them put, each reported; :1 unlocks one again.
		CLDocument* doc = w->document();
		const int pg = w->currentPage();
		cl_edit_select_all(doc, pg);
		w->lockSelection(true);
		fprintf(stderr, "lock in place: %d part(s) locked\n", cl_document_locked_count(doc));
		w->nudge(2, 0);
		fprintf(stderr, "lock in place: a nudge %s\n", cl_edit_locked_held(doc) ? "left them put" : "moved them");
		if (page == 1) {
			cl_edit_select_none(doc, pg);
			w->unlockAll();
			fprintf(stderr, "lock in place: %d part(s) locked after Unlock All Parts\n", cl_document_locked_count(doc));
		}
		w->selectionChanged();
	}
	return G_SOURCE_REMOVE;
}

gboolean screenshotCb(gpointer app) {
	CircuitWindow* w = circuitWindows().empty() ? nullptr : circuitWindows().back();
	gExitCode = 1;
	if (w) {
		GtkWidget* top = GTK_WIDGET(w->window());
		const int width = gtk_widget_get_allocated_width(top), height = gtk_widget_get_allocated_height(top);
		cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, std::max(1, width), std::max(1, height));
		cairo_t* cr = cairo_create(s);
		gtk_widget_draw(top, cr);
		cairo_destroy(cr);
		if (cairo_surface_write_to_png(s, gScreenshot.c_str()) == CAIRO_STATUS_SUCCESS) gExitCode = 0;
		cairo_surface_destroy(s);
		fprintf(stderr, "%s %s (%dx%d)\n", gExitCode ? "couldn't write" : "wrote", gScreenshot.c_str(), width, height);
	}
	for (CircuitWindow* c : std::vector<CircuitWindow*>(circuitWindows())) gtk_widget_destroy(GTK_WIDGET(c->window()));
	g_application_quit(G_APPLICATION(app));
	return G_SOURCE_REMOVE;
}

gboolean benchCb(gpointer app) {
	CircuitWindow* w = circuitWindows().empty() ? nullptr : circuitWindows().back();
	gExitCode = 1;
	if (w) guarded("the drag benchmark", [&] { gExitCode = bench::dragBenchmark(w, gBench); });
	for (CircuitWindow* c : std::vector<CircuitWindow*>(circuitWindows())) gtk_widget_destroy(GTK_WIDGET(c->window()));
	g_application_quit(G_APPLICATION(app));
	return G_SOURCE_REMOVE;
}

std::string samplesDir() { return resourcesDir() + "/samples"; }

// The whole menu bar. Bare-key shortcuts (R, S, T...) are the canvas's own,
// so they're only mentioned in the labels, never bound here.
GMenuModel* buildMenubar() {
	GMenu* bar = g_menu_new();

	GMenu* file = g_menu_new();
	GMenu* s1 = g_menu_new();
	g_menu_append(s1, "_New Circuit", "app.new");
	g_menu_append(s1, "New from _Template…", "win.new-template");
	g_menu_append(s1, "Your _Circuits…", "app.open");
	g_menu_append(s1, "_Import a File…", "app.import");
	gRecentMenu = g_menu_new();
	g_menu_append_submenu(s1, "Open _Recent", G_MENU_MODEL(gRecentMenu));
	g_menu_append(s1, "Open the _Practice Circuit", "app.open-sample");
	g_menu_append_section(file, nullptr, G_MENU_MODEL(s1));
	GMenu* s2 = g_menu_new();
	g_menu_append(s2, "_Save a Version", "win.save");
	g_menu_append(s2, "_Version History…", "win.versions");
	g_menu_append(s2, "_Rename…", "win.rename-circuit");
	g_menu_append(s2, "Du_plicate Circuit", "win.duplicate-circuit");
	g_menu_append(s2, "E_xport as CedarLogic File…", "win.save-as");
	g_menu_append(s2, "Share _Link…", "app.share-link");
	g_menu_append(s2, "Save as Te_mplate…", "win.save-template");
	g_menu_append(s2, "_Export as Image…", "win.export-image");
	g_menu_append(s2, "Export _Lab Report…", "win.export-report");
	GMenu* older = g_menu_new();
	g_menu_append(older, "For CedarLogic _2…", "win.export-v2");
	g_menu_append(older, "For CedarLogic _1.x…", "win.export-v1");
	g_menu_append_submenu(s2, "Export for _Older CedarLogic", G_MENU_MODEL(older));
	g_menu_append(s2, "_Print…", "win.print");
	g_menu_append_section(file, nullptr, G_MENU_MODEL(s2));
	GMenu* s3 = g_menu_new();
	g_menu_append(s3, "_Close Window", "win.close");
	g_menu_append(s3, "_Quit", "app.quit");
	g_menu_append_section(file, nullptr, G_MENU_MODEL(s3));
	g_menu_append_submenu(bar, "_File", G_MENU_MODEL(file));

	GMenu* edit = g_menu_new();
	GMenu* e1 = g_menu_new();
	g_menu_append(e1, "_Undo", "win.undo");
	g_menu_append(e1, "_Redo", "win.redo");
	g_menu_append_section(edit, nullptr, G_MENU_MODEL(e1));
	GMenu* e2 = g_menu_new();
	g_menu_append(e2, "Cu_t", "win.cut");
	g_menu_append(e2, "_Copy", "win.copy");
	g_menu_append(e2, "_Paste", "win.paste");
	g_menu_append(e2, "D_uplicate", "win.duplicate");
	g_menu_append(e2, "_Delete", "win.delete");
	g_menu_append(e2, "Select _All", "win.select-all");
	g_menu_append(e2, "_Find…", "win.find");
	g_menu_append(e2, "Find _Next", "win.find-next");
	g_menu_append(e2, "Find Pre_vious", "win.find-previous");
	g_menu_append_section(edit, nullptr, G_MENU_MODEL(e2));
	GMenu* e3 = g_menu_new();
	g_menu_append(e3, "Add a _Gate…   (A)", "win.add-gate");
	g_menu_append(e3, "Gate _Settings…", "win.gate-settings");
	g_menu_append(e3, "_Rotate   (R)", "win.rotate");
	g_menu_append(e3, "_Lock in Place", "win.lock-in-place");
	g_menu_append(e3, "Unlock All _Parts", "win.unlock-all");
	g_menu_append(e3, "Straighten _Wires   (S)", "win.straighten");
	g_menu_append(e3, "_Tidy Up   (Shift+S)", "win.tidy");
	g_menu_append(e3, "Tidy Up by Signal _Flow", "win.tidy-flow");
	g_menu_append(e3, "Connect _Nearby Pins", "win.connect-nearby");
	g_menu_append(e3, "Save as _Part…", "win.save-part");
	g_menu_append(e3, "_Build from Formula…", "win.build-formula");
	g_menu_append_section(edit, nullptr, G_MENU_MODEL(e3));
	GMenu* e4 = g_menu_new();
	g_menu_append(e4, "Pr_eferences", "win.preferences");
	g_menu_append_section(edit, nullptr, G_MENU_MODEL(e4));
	g_menu_append_submenu(bar, "_Edit", G_MENU_MODEL(edit));

	GMenu* view = g_menu_new();
	GMenu* v1 = g_menu_new();
	g_menu_append(v1, "Zoom _In", "win.zoom-in");
	g_menu_append(v1, "Zoom _Out", "win.zoom-out");
	g_menu_append(v1, "Zoom to _Fit", "win.zoom-fit");
	g_menu_append(v1, "_Actual Size", "win.zoom-actual");
	g_menu_append_section(view, nullptr, G_MENU_MODEL(v1));
	GMenu* v2 = g_menu_new();
	g_menu_append(v2, "_Dark Mode", "win.dark");
	g_menu_append(v2, "_Focus Mode", "win.focus-mode");
	g_menu_append(v2, "Side _Panel", "win.palette");
	g_menu_append(v2, "_Status Bar", "win.status-bar");
	g_menu_append_section(view, nullptr, G_MENU_MODEL(v2));
	// Drawing on the circuit and the notes (Ink.h, Notes.h), as the Mac's View menu.
	GMenu* v3 = g_menu_new();
	g_menu_append(v3, "Draw on the _Circuit", "win.draw");
	GMenu* drawing = g_menu_new();
	g_menu_append(drawing, "_Pen   (P)", "win.ink-pen");
	g_menu_append(drawing, "_Highlighter   (H)", "win.ink-highlighter");
	g_menu_append(drawing, "_Eraser   (E)", "win.ink-eraser");
	GMenu* clear = g_menu_new();
	g_menu_append(clear, "_Clear Drawing on This Page", "win.clear-drawing");
	g_menu_append_section(drawing, nullptr, G_MENU_MODEL(clear));
	g_menu_append_submenu(v3, "Dra_wing", G_MENU_MODEL(drawing));
	g_menu_append(v3, "Show D_rawing", "win.show-drawing");
	g_menu_append(v3, "Show _Notes", "win.notes");
	g_menu_append_section(view, nullptr, G_MENU_MODEL(v3));
	g_menu_append_submenu(bar, "_View", G_MENU_MODEL(view));

	GMenu* sim = g_menu_new();
	GMenu* m1 = g_menu_new();
	g_menu_append(m1, "_Running", "win.running");
	g_menu_append(m1, "_Step Once", "win.step");
	g_menu_append(m1, "Step _Clock   (K)", "win.step-clock");
	g_menu_append(m1, "Simulation _View", "win.sim-view");
	g_menu_append(m1, "_Lock the Circuit", "win.lock");
	g_menu_append_section(sim, nullptr, G_MENU_MODEL(m1));
	GMenu* m2 = g_menu_new();
	g_menu_append(m2, "_Truth Table   (T)", "win.truth-table");
	g_menu_append(m2, "_Check My Circuit   (Shift+T)", "win.check-circuit");
	g_menu_append(m2, "_Oscilloscope", "win.scope");
	g_menu_append_section(sim, nullptr, G_MENU_MODEL(m2));
	g_menu_append_submenu(bar, "_Simulate", G_MENU_MODEL(sim));

	GMenu* tabs = g_menu_new();
	g_menu_append(tabs, "_New Tab", "win.new-tab");
	g_menu_append(tabs, "_Close Tab", "win.close-tab");
	g_menu_append(tabs, "_Reopen Closed Tab", "win.reopen-tab");
	g_menu_append(tabs, "Re_name Tab…", "win.rename-tab");
	g_menu_append(tabs, "Ne_xt Tab", "win.next-tab");
	g_menu_append(tabs, "_Previous Tab", "win.previous-tab");
	GMenu* splitSection = g_menu_new();
	g_menu_append(splitSection, "_Split View", "win.split-view");
	g_menu_append(splitSection, "S_witch Side", "win.switch-pane");
	g_menu_append(splitSection, "Close Split Vie_w", "win.close-split");
	g_menu_append_section(tabs, nullptr, G_MENU_MODEL(splitSection));
	// Which pages' TO/FROM links connect (docs/PAGE-LINKS.md); a tab's
	// right-click menu connects pages one by one.
	GMenu* linkSection = g_menu_new();
	g_menu_append(linkSection, "Connect _All Pages", "win.connect-all-pages");
	g_menu_append(linkSection, "_Disconnect All Pages", "win.disconnect-all-pages");
	g_menu_append_section(tabs, nullptr, G_MENU_MODEL(linkSection));
	g_menu_append_submenu(bar, "_Tabs", G_MENU_MODEL(tabs));

	GMenu* help = g_menu_new();
	g_menu_append(help, "_Keyboard Shortcuts", "win.shortcuts");
	g_menu_append(help, "Guided _Tour", "app.guided-tour");
	g_menu_append(help, "CedarLogic _Help", "win.help");
	g_menu_append(help, "_What's New in CedarLogic", "win.whats-new");
	g_menu_append(help, "Send _Feedback…", "win.feedback");
	g_menu_append(help, "Check for _Updates…", "app.check-updates");
	g_menu_append(help, "_About CedarLogic", "win.about");
	g_menu_append_submenu(bar, "_Help", G_MENU_MODEL(help));
	return G_MENU_MODEL(bar);
}

// Every command's keys: the defaults, with Settings > Shortcuts' changes.
void setAccels(GtkApplication* app) { shortcuts::apply(app); }

CircuitWindow* activeWindow(GtkApplication* app) {
	GtkWindow* w = gtk_application_get_active_window(app);
	for (CircuitWindow* c : circuitWindows()) if (c->window() == w) return c;
	return circuitWindows().empty() ? nullptr : circuitWindows().back();
}

// Ctrl+N: a blank page, or the template Settings > General names; in this
// window's place when Settings says new circuits replace the one you're in
// (it's saved first, as everything is).
void newCb(GSimpleAction*, GVariant*, gpointer app) {
	GtkApplication* a = GTK_APPLICATION(app);
	CircuitWindow* from = activeWindow(a);
	const bool replace = prefs().openReplaces && from;
	if (!prefs().newTemplate.empty() && templates::startFrom(prefs().newTemplate, from, a, replace)) return;
	if (replace && from->saveQuietly(false)) { from->replaceDocument(cl_document_new(), ""); return; }
	newCircuitWindow(a);
}

// Ctrl+O: Your Circuits (as the Mac's); Ctrl+I brings in a file from anywhere.
void openCb(GSimpleAction*, GVariant*, gpointer app) {
	CircuitWindow* w = activeWindow(GTK_APPLICATION(app));
	if (w) showYourCircuits(w);
	else chooseAndOpen(GTK_APPLICATION(app), nullptr);
}

void importCb(GSimpleAction*, GVariant*, gpointer app) {
	CircuitWindow* w = activeWindow(GTK_APPLICATION(app));
	chooseAndOpen(GTK_APPLICATION(app), w ? w->window() : nullptr);
}

// File > Share Link…: the circuit's link on the clipboard (the website's, which
// opens in CedarLogic Online and offers the app). Too long for a link says so.
void shareLinkCb(GSimpleAction*, GVariant*, gpointer app) {
	CircuitWindow* w = activeWindow(GTK_APPLICATION(app));
	if (!w) return;
	CLDocument* doc = w->document();
	bool any = false;
	for (int p = 0; p < cl_document_page_count(doc); p++) any = any || cl_document_gate_count(doc, p) > 0;
	if (!any) { w->note("There's nothing on the circuit to share yet."); return; }
	// The notes never go in a link (they're the student's own, and links get
	// pasted into chats); the drawing goes when it's shown and the link still
	// fits (the website's DRAWING-NOTES.md 4.11), as on the Mac.
	const std::string name = w->titleText();
	auto make = [&](int flags) {
		const std::string data = sharelink::encode(cl_document_save_text_ex(doc, flags));
		return data.empty() ? std::string() : std::string(sharelink::kWebBase) + "#" + sharelink::fragment(data, name);
	};
	const bool withInk = cl_ink_any(doc) && cl_ink_shown(doc);
	std::string link = make(withInk ? CL_SAVE_NO_NOTES : CL_SAVE_NO_NOTES | CL_SAVE_NO_INK);
	if (link.empty()) { w->note("Couldn't make a link for this circuit."); return; }
	bool leftOutDrawing = false;
	if (withInk && link.size() > sharelink::kMaxWebLink) {
		const std::string without = make(CL_SAVE_NO_NOTES | CL_SAVE_NO_INK);
		if (!without.empty()) { link = without; leftOutDrawing = true; }
	}
	if (link.size() > sharelink::kMaxWebLink) {
		showMessage(w->window(), GTK_MESSAGE_INFO, "This circuit is too big for a link",
		            format("A link holds the whole circuit, and this one would be about %d KB. Use File \u25B8 Export as CedarLogic File… and send the file instead.",
		                   (int)(link.size() / 1024)));
		return;
	}
	gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), link.c_str(), -1);
	w->note(leftOutDrawing ? "Link copied. The link leaves out the drawing; it's too big for a link. Send the file to include it."
	                       : "Link copied. Anyone who opens it gets this circuit; it isn't stored anywhere.");
}

void openRecentCb(GSimpleAction*, GVariant* param, gpointer app) {
	const gchar* path = g_variant_get_string(param, nullptr);
	if (path) openCircuit(GTK_APPLICATION(app), path, activeWindow(GTK_APPLICATION(app)));
}

// The practice circuit opens as a new, untitled copy, so saving asks where.
void openSampleCb(GSimpleAction*, GVariant*, gpointer app) {
	const std::string file = samplesDir() + "/practice.cdl";
	CircuitWindow* from = activeWindow(GTK_APPLICATION(app));
	char err[512] = "";
	CLDocument* doc = cl_document_open(file.c_str(), err, sizeof err);
	if (doc == nullptr) {
		showMessage(from ? from->window() : nullptr, GTK_MESSAGE_WARNING, "The practice circuit couldn't be opened", err);
		return;
	}
	if (from && from->isPristine()) from->replaceDocument(doc, "");
	else new CircuitWindow(GTK_APPLICATION(app), doc, "");
}

void quitCb(GSimpleAction*, GVariant*, gpointer app) {
	CircuitWindow* w = activeWindow(GTK_APPLICATION(app));
	if (confirmQuitting(w ? w->window() : nullptr)) quitApp(GTK_APPLICATION(app));
}

void loadCss() { applyStyle(); }

void setIcon() {
	const std::string candidates[] = { resourcesDir() + "/cedarlogic.png", resourcesDir() + "/../linux/res/cedarlogic.png" };
	for (const std::string& c : candidates) {
		if (g_file_test(c.c_str(), G_FILE_TEST_EXISTS) && gtk_window_set_default_icon_from_file(c.c_str(), nullptr)) return;
	}
	gtk_window_set_default_icon_name("cedarlogic");
}

}  // namespace

// Each window asks about its own changes; stop at the first "Cancel".
bool quitApp(GtkApplication* app) {
	std::vector<CircuitWindow*> all = circuitWindows();
	for (CircuitWindow* w : all) {
		gtk_window_present(w->window());
		if (!w->confirmClose()) return false;
	}
	for (CircuitWindow* w : all) gtk_widget_destroy(GTK_WIDGET(w->window()));
	g_application_quit(G_APPLICATION(app));
	return true;
}

namespace {

// One CedarLogic at a time: this launch owns the app's name on the session
// bus before startup begins, so another launch -- the menu clicked again
// while nothing has appeared yet, a .cdl double-clicked -- can hand over
// while this one is still starting. GTK delivers it whenever events are
// handled: as startup puts the launch screen up, or under a question the
// first windows ask (a last circuit that couldn't be opened). Such a launch
// waits for this one rather than running a second launch inside it (or, in
// startup, finding no gate library yet and quitting with an error).
bool gInStartup = false;   // in startupCb
bool gLaunching = false;   // in a launch's activate or open, until its windows are made
// The first windows are made: a later launch hands over to them.
bool gStarted = false;
// Files handed over meanwhile, opened once the first windows are made.
std::vector<std::string> gPendingOpens;
// cedarlogic://sync links handed over: Settings > Sync with the code, once the windows (and the
// launch screen) are there. Never linking by themselves.
std::vector<std::string> gPendingSyncLinks;

gboolean syncLinksCb(gpointer) {
	if (!gStarted || splashActive()) return G_SOURCE_CONTINUE;
	std::vector<std::string> links;
	links.swap(gPendingSyncLinks);
	for (const std::string& link : links) guarded("a sync link", [&] { syncapp::handleLink(link); });
	return G_SOURCE_REMOVE;
}

void routeSyncLink(const std::string& link) {
	gPendingSyncLinks.push_back(link);
	g_timeout_add(300, syncLinksCb, nullptr);
}

bool launchUnderWay() { return gInStartup || gLaunching; }

void startupCb(GApplication* gapp, gpointer) {
	gInStartup = true;
	struct Done { ~Done() { gInStartup = false; } } done;
	GtkApplication* app = GTK_APPLICATION(gapp);
	// GTK set every part of the locale from the desktop's; numbers go back
	// to "C", so 1.5 reads and writes as 1.5 everywhere (circuit files, the
	// engine's settings, CSS) in German, French and other comma locales.
	setlocale(LC_NUMERIC, "C");
	sturdyUiFont();
	// Before the launch screen: it asks the settings whether this is the
	// first launch ever (the slower one, with the sound).
	prefs().load();
	gPrimary = true;
	// Not for --screenshot: CI wants one deterministic frame, not a race
	// with a timed splash.
	if (gScreenshot.empty() && gSplashFile.empty() && gReportIn.empty()) gSplash = showSplash();
	if (gTheme >= 0) prefs().dark = gTheme == 1;
	applyTheme();
	loadCss();
	setIcon();

	if (gSplash) splashSetStatus(gSplash, "Loading the gate library…");
	const std::string lib = resourcesDir().empty() ? std::string() : resourcesDir() + "/cl_gatedefs.xml";
	gLibraryLoaded = !lib.empty() && cl_library_load(lib.c_str());
	prefs().applyWireDots();
	if (!gReportIn.empty()) {
		// Not a window: the circuit run a while, then its lab report.
		char err[512] = "";
		CLDocument* doc = gLibraryLoaded ? cl_document_open(gReportIn.c_str(), err, sizeof err) : nullptr;
		if (doc == nullptr) { fprintf(stderr, "couldn't open %s: %s\n", gReportIn.c_str(), err); exit(1); }
		for (int i = 0; i < 120; i++) cl_document_step(doc);
		if (prefs().studentName.empty()) prefs().studentName = "Alex Student";
		LabReportOptions o;
		o.color = !gReportBW;
		std::string error;
		if (!writeLabReport(doc, baseName(gReportIn), o, gReportOut, error)) { fprintf(stderr, "%s\n", error.c_str()); exit(1); }
		exit(0);
	}
	if (gSplash) splashSetStatus(gSplash, gLibraryLoaded ? "Opening the workspace…" : "Couldn't find the gate library");
	// Sync (docs/SYNC.md): the engine is made, and syncs if a code is stored. The picture runs leave
	// the person's own library and code alone, unless they point it at a test server.
	if (gLibraryLoaded && gSplashFile.empty()) {
		syncapp::start(app);
		if (gScreenshot.empty() || g_getenv("CL_SYNC_URL")) syncapp::begin();
	}

	const GActionEntry entries[] = {
		{ "new", newCb, nullptr, nullptr, nullptr, { 0 } },
		{ "open", openCb, nullptr, nullptr, nullptr, { 0 } },
		{ "import", importCb, nullptr, nullptr, nullptr, { 0 } },
		{ "open-recent", openRecentCb, "s", nullptr, nullptr, { 0 } },
		{ "open-sample", openSampleCb, nullptr, nullptr, nullptr, { 0 } },
		{ "share-link", shareLinkCb, nullptr, nullptr, nullptr, { 0 } },
		{ "quit", quitCb, nullptr, nullptr, nullptr, { 0 } },
		{ "check-updates", [](GSimpleAction*, GVariant*, gpointer app) { Updater_CheckNow(GTK_APPLICATION(app)); },
		  nullptr, nullptr, nullptr, { 0 } },
		{ "guided-tour", [](GSimpleAction*, GVariant*, gpointer app) { startTour(activeWindow(GTK_APPLICATION(app))); },
		  nullptr, nullptr, nullptr, { 0 } },
	};
	g_action_map_add_action_entries(G_ACTION_MAP(app), entries, G_N_ELEMENTS(entries), app);
	setAccels(app);
	GMenuModel* bar = buildMenubar();
	gtk_application_set_menubar(app, bar);
	g_object_unref(bar);
	rebuildRecentMenus();
	// Not in the screenshot runs: an update question would outlive them.
	if (gScreenshot.empty()) {
		Updater_Initialize(app);
		Updater_IntegrateAppImage();
	}
}

bool libraryOrComplain() {
	if (gLibraryLoaded) return true;
	showMessage(nullptr, GTK_MESSAGE_ERROR, "CedarLogic can't find its gate library",
	            "cl_gatedefs.xml wasn't found next to the app. Reinstall CedarLogic, or set CEDARLOGIC_RESOURCES "
	            "to the folder that has it.");
	return false;
}

// Once the first window is up, offer back work a CedarLogic that stopped
// unexpectedly left behind.
gboolean offerRecoveryCb(gpointer app) {
	CircuitWindow* w = circuitWindows().empty() ? nullptr : circuitWindows().front();
	guarded("recovering work", [&] { recovery::offer(GTK_APPLICATION(app), w); });
	return G_SOURCE_REMOVE;
}

gboolean offerWelcomeCb(gpointer app) {
	CircuitWindow* w = circuitWindows().empty() ? nullptr : circuitWindows().front();
	guarded("the welcome window", [&] { offerWelcome(GTK_APPLICATION(app), w); });
	return G_SOURCE_REMOVE;
}

// Launched again while running (the menu, the dock): this CedarLogic comes
// forward, as the Mac's does, rather than a second window on the same circuit.
bool presentRunning(GApplication* gapp) {
	if (!gStarted || circuitWindows().empty()) return false;
	// While the launch screen is up its windows are still hidden; they come in with it.
	if (!splashActive()) gtk_window_present(activeWindow(GTK_APPLICATION(gapp))->window());
	return true;
}

// Files handed over by another launch (a .cdl double-clicked in the file
// manager): each opens here, in a window of its own (or the one that has it
// already comes forward). An untouched new window -- the blank Untitled one
// -- takes the first one's place, as with Open and a drop; a circuit you're
// in is left as it is, whatever Settings says about opening replacing it.
void openHandedOver(GtkApplication* app, const std::vector<std::string>& paths) {
	auto stillOpen = [](CircuitWindow* w) {
		const std::vector<CircuitWindow*>& all = circuitWindows();
		return w && std::find(all.begin(), all.end(), w) != all.end();
	};
	CircuitWindow* blank = activeWindow(app);
	for (const std::string& path : paths) {
		if (sharelink::isSyncLink(path)) { routeSyncLink(path); continue; }
		// Still there and still blank: a question opening a file asks lets
		// other things happen meanwhile. Not while a dialog or menu is open
		// (over it, perhaps, working on what it shows): a window of its own then.
		if (!stillOpen(blank) || !blank->isPristine() || gtk_grab_get_current() != nullptr) blank = nullptr;
		openCircuit(app, path, blank);
		// It has the file now: it comes forward, as a new window would.
		if (stillOpen(blank) && !blank->filePath().empty()) {
			if (!splashHoldsWindows()) gtk_window_present(blank->window());
			blank = nullptr;
		}
	}
	if (circuitWindows().empty()) newCircuitWindow(app);
	// Handed over while the launch screen still plays, before it brings the
	// windows in: these come in with them. Asked now, not before: a question
	// opening a file asked may have outlasted it, and the windows are in.
	if (splashHoldsWindows()) for (CircuitWindow* c : circuitWindows()) gtk_widget_hide(GTK_WIDGET(c->window()));
}

// The first windows are made (behind the launch screen while it plays): a
// later launch hands over to them now, and files handed over meanwhile open.
void launched(GtkApplication* app) {
	gStarted = true;
	gLaunching = false;
	std::vector<std::string> paths;
	paths.swap(gPendingOpens);
	if (!paths.empty()) openHandedOver(app, paths);
}

void activateCb(GApplication* gapp, gpointer) {
	// Launched again while this launch is under way: it brings a window up anyway.
	if (launchUnderWay()) return;
	if (presentRunning(gapp)) return;
	// From here a question (no gate library, a circuit that couldn't be
	// opened) lets a later launch in: it waits for this one.
	gLaunching = true;
	if (!libraryOrComplain()) {
		gExitCode = 1;
		if (gSplash) { gtk_widget_destroy(gSplash); gSplash = nullptr; }
		g_application_quit(gapp);
		return;
	}
	if (!gSplashFile.empty()) {
		const bool ok = renderSplashFrame(gSplashAt, gFirstLaunch, gSplashFile);
		fprintf(stderr, "%s %s\n", ok ? "wrote" : "couldn't write", gSplashFile.c_str());
		gExitCode = ok ? 0 : 1;
		g_application_quit(gapp);
		return;
	}
	// Nothing asked for: the circuit you were last in, as the wx and Mac apps
	// do; else the most recent one; else a new circuit.
	if (gScreenshot.empty()) {
		std::string last = library::lastCircuit();
		if (last.empty() || !g_file_test(last.c_str(), G_FILE_TEST_EXISTS)) {
			const std::vector<library::Item> all = library::items();
			last = all.empty() ? std::string() : all.front().circuit();
		}
		if (!last.empty()) openCircuit(GTK_APPLICATION(gapp), last, nullptr);
	}
	CircuitWindow* w = circuitWindows().empty() ? newCircuitWindow(GTK_APPLICATION(gapp)) : circuitWindows().front();
	if (gSplash && w && !w->filePath().empty()) splashSetOpening(w->titleText());
	if (gSplash && w) gtk_widget_hide(GTK_WIDGET(w->window()));
	hideSplashSoon(gSplash, +[](gpointer app) -> gboolean {
		for (CircuitWindow* c : circuitWindows()) { gtk_widget_show(GTK_WIDGET(c->window())); if (gScreenshot.empty()) c->beginOpening(); }
		if (!gScreenshot.empty()) {
			if (gSimView) for (CircuitWindow* c : circuitWindows()) c->toggleSimView();
			if (gBench > 0) g_timeout_add(1500, benchCb, app);
			else if (!gShow.empty()) g_timeout_add(1200, showCb, app);
			else g_timeout_add(gHold * 1000, screenshotCb, app);
		}
		else if (!prefs().hasSeenWelcome) g_idle_add(offerWelcomeCb, app);
		else {
			g_idle_add(offerRecoveryCb, app);
			g_idle_add([](gpointer) -> gboolean {
				if (!circuitWindows().empty()) guarded("What's New", [] { whatsnew::offer(circuitWindows().front()); });
				return G_SOURCE_REMOVE;
			}, app);
		}
		return G_SOURCE_REMOVE;
	}, gapp);
	gSplash = nullptr;
	launched(GTK_APPLICATION(gapp));
}

void openFilesCb(GApplication* gapp, GFile** files, gint n, const gchar*, gpointer) {
	std::vector<std::string> paths;
	for (gint i = 0; i < n; i++) {
		gchar* path = g_file_get_path(files[i]);
		if (path) {
			paths.push_back(path);
		} else {
			// Not a file: a cedarlogic://sync link (the desktop passes it on as an address).
			gchar* uri = g_file_get_uri(files[i]);
			if (uri && sharelink::isSyncLink(uri)) paths.push_back(uri);
			g_free(uri);
		}
		g_free(path);
	}
	// Handed over while this launch is under way: they open once its windows are made.
	if (launchUnderWay()) {
		gPendingOpens.insert(gPendingOpens.end(), paths.begin(), paths.end());
		return;
	}
	if (gStarted) {
		openHandedOver(GTK_APPLICATION(gapp), paths);
		return;
	}
	gLaunching = true;
	if (!libraryOrComplain()) {
		gExitCode = 1;
		if (gSplash) { gtk_widget_destroy(gSplash); gSplash = nullptr; }
		g_application_quit(gapp);
		return;
	}
	bool any = false;
	for (const std::string& path : paths) {
		if (sharelink::isSyncLink(path)) { routeSyncLink(path); continue; }
		any = openCircuit(GTK_APPLICATION(gapp), path, nullptr) || any;
	}
	if (!any && circuitWindows().empty()) newCircuitWindow(GTK_APPLICATION(gapp));
	if (gSplash) for (CircuitWindow* c : circuitWindows()) gtk_widget_hide(GTK_WIDGET(c->window()));
	hideSplashSoon(gSplash, +[](gpointer app) -> gboolean {
		for (CircuitWindow* c : circuitWindows()) { gtk_widget_show(GTK_WIDGET(c->window())); if (gScreenshot.empty()) c->beginOpening(); }
		if (!gScreenshot.empty()) {
			if (gSimView) for (CircuitWindow* c : circuitWindows()) c->toggleSimView();
			if (gBench > 0) g_timeout_add(1500, benchCb, app);
			else if (!gShow.empty()) g_timeout_add(1200, showCb, app);
			else g_timeout_add(gHold * 1000, screenshotCb, app);
		}
		else if (!prefs().hasSeenWelcome) g_idle_add(offerWelcomeCb, app);
		else {
			g_idle_add(offerRecoveryCb, app);
			g_idle_add([](gpointer) -> gboolean {
				if (!circuitWindows().empty()) guarded("What's New", [] { whatsnew::offer(circuitWindows().front()); });
				return G_SOURCE_REMOVE;
			}, app);
		}
		return G_SOURCE_REMOVE;
	}, gapp);
	gSplash = nullptr;
	launched(GTK_APPLICATION(gapp));
}

// Restarted after an update (Updater.cpp): the copy before this one is still
// quitting. Wait for it to go, so this one becomes the running CedarLogic
// rather than handing its launch to that one.
void waitForPreviousCopy() {
	const char* was = g_getenv("CEDARLOGIC_RESTART_AFTER");
	const long pid = was ? strtol(was, nullptr, 10) : 0;
	g_unsetenv("CEDARLOGIC_RESTART_AFTER");
	for (int i = 0; pid > 0 && i < 100; i++) {
		gchar* stat = nullptr;
		if (!g_file_get_contents(format("/proc/%ld/stat", pid).c_str(), &stat, nullptr, nullptr)) return;
		// "pid (name) state ...": a zombie has gone, whatever its parent does.
		const char* close = strrchr(stat, ')');
		const bool gone = close && close[1] == ' ' && (close[2] == 'Z' || close[2] == 'X');
		g_free(stat);
		if (gone) return;
		g_usleep(100000);
	}
}

}  // namespace

// ---- Shared with the windows -----------------------------------------------------

std::vector<CircuitWindow*>& circuitWindows() {
	static std::vector<CircuitWindow*> all;
	return all;
}

CircuitWindow* newCircuitWindow(GtkApplication* app) {
	return new CircuitWindow(app, cl_document_new(), "");
}

// Every circuit lives in Your Circuits: a .cdl file from elsewhere carries
// on as a copy there (the file itself is left alone; Export gets one out),
// and opening the same file again finds that copy.
bool openCircuit(GtkApplication* app, const std::string& path, CircuitWindow* from) {
	std::string target = path;
	library::Item existing;
	if (!library::contains(path) && library::imported(path, existing)) target = existing.circuit();
	// Already open: bring that window forward.
	for (CircuitWindow* w : circuitWindows()) {
		if (!w->filePath().empty() && w->filePath() == target) { gtk_window_present(w->window()); return true; }
	}
	char err[512] = "";
	CLDocument* doc = cl_document_open(target.c_str(), err, sizeof err);
	if (doc == nullptr) {
		showMessage(from ? from->window() : nullptr, GTK_MESSAGE_ERROR,
		            format("“%s” couldn't be opened", baseName(path).c_str()), err);
		// A file that's gone leaves the recent list.
		std::vector<std::string>& r = prefs().recent;
		if (!g_file_test(path.c_str(), G_FILE_TEST_EXISTS)) {
			r.erase(std::remove(r.begin(), r.end(), path), r.end());
			prefs().save();
			rebuildRecentMenus();
		}
		return false;
	}
	bool importedNow = false;
	if (!library::contains(target)) {
		library::Item it;
		if (library::create(baseName(path), cl_document_save_text(doc), path, it)) {
			target = it.circuit();
			importedNow = true;
		}
	}
	// A file from elsewhere: its folder is where the file dialogs start next,
	// and it goes in the desktop's recent list (Open Recent has its copy).
	if (!library::contains(path)) {
		gchar* dir = g_path_get_dirname(path.c_str());
		prefs().lastFolder = dir;
		g_free(dir);
		noteDesktopRecent(path);
	}
	CircuitWindow* w;
	// In the window's place: an untouched new one, or any when Settings says
	// opening replaces the circuit you're in (saved first).
	if (from && (from->isPristine() || (prefs().openReplaces && from->saveQuietly(false)))) { from->replaceDocument(doc, target); w = from; }
	else w = new CircuitWindow(app, doc, target);
	prefs().noteRecent(target);
	library::noteLastCircuit(target);
	// What loading had to say (an older format converted, an unknown gate...).
	std::string notes;
	bool warning = false;
	for (int i = 0; i < cl_document_notice_count(doc); i++) {
		notes += std::string("• ") + cl_document_notice(doc, i) + "\n";
		warning = warning || cl_document_notice_is_warning(doc, i);
	}
	if (warning) showMessage(w->window(), GTK_MESSAGE_WARNING, "Opened, with notes", notes);
	else if (!notes.empty()) w->note(notes.substr(2, notes.find('\n') - 2));
	else if (importedNow) w->note("In Your Circuits now, as a copy. The file itself is left as it was.");
	return true;
}

void chooseAndOpen(GtkApplication* app, GtkWindow* parent) {
	GtkFileChooserNative* c = gtk_file_chooser_native_new("Open Circuit", parent, GTK_FILE_CHOOSER_ACTION_OPEN,
	                                                      "_Open", "_Cancel");
	GtkFileChooser* fc = GTK_FILE_CHOOSER(c);
	gtk_file_chooser_set_select_multiple(fc, TRUE);
	GtkFileFilter* f = gtk_file_filter_new();
	gtk_file_filter_set_name(f, "CedarLogic circuits (*.cdl)");
	gtk_file_filter_add_pattern(f, "*.cdl");
	gtk_file_filter_add_pattern(f, "*.CDL");
	gtk_file_chooser_add_filter(fc, f);
	GtkFileFilter* anyFile = gtk_file_filter_new();
	gtk_file_filter_set_name(anyFile, "All files");
	gtk_file_filter_add_pattern(anyFile, "*");
	gtk_file_chooser_add_filter(fc, anyFile);
	if (!prefs().lastFolder.empty()) gtk_file_chooser_set_current_folder(fc, prefs().lastFolder.c_str());
	std::vector<std::string> files;
	if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(c)) == GTK_RESPONSE_ACCEPT) {
		GSList* list = gtk_file_chooser_get_filenames(fc);
		for (GSList* l = list; l; l = l->next) { files.push_back((const char*)l->data); g_free(l->data); }
		g_slist_free(list);
	}
	g_object_unref(c);
	CircuitWindow* from = nullptr;
	for (CircuitWindow* w : circuitWindows()) if (w->window() == parent) from = w;
	for (const std::string& file : files) {
		openCircuit(app, file, from);
		from = nullptr;   // the rest get windows of their own
	}
}

void rebuildRecentMenus() {
	if (gRecentMenu == nullptr) return;
	g_menu_remove_all(gRecentMenu);
	int shown = 0;
	for (const std::string& path : prefs().recent) {
		if (!g_file_test(path.c_str(), G_FILE_TEST_EXISTS)) continue;
		GMenuItem* item = g_menu_item_new(nullptr, nullptr);
		// A circuit in Your Circuits by its name there (its file is always
		// circuit.cdl). Underscores in a name aren't mnemonics.
		library::Item it;
		const std::string label = library::itemFor(path, it) ? it.name : baseName(path);
		std::string escaped;
		for (const char* p = label.c_str(); *p; p++) { if (*p == '_') escaped += '_'; escaped += *p; }
		g_menu_item_set_label(item, escaped.c_str());
		g_menu_item_set_action_and_target_value(item, "app.open-recent", g_variant_new_string(path.c_str()));
		g_menu_append_item(gRecentMenu, item);
		g_object_unref(item);
		if (++shown >= 10) break;
	}
	if (shown == 0) {
		GMenuItem* item = g_menu_item_new("No recent circuits", "app.none");
		g_menu_append_item(gRecentMenu, item);
		g_object_unref(item);
	}
}

// A folder and everything in it, gone.
static void removeTree(const std::string& path) {
	if (g_file_test(path.c_str(), G_FILE_TEST_IS_DIR) && !g_file_test(path.c_str(), G_FILE_TEST_IS_SYMLINK)) {
		if (GDir* d = g_dir_open(path.c_str(), 0, nullptr)) {
			while (const gchar* n = g_dir_read_name(d)) removeTree(path + "/" + n);
			g_dir_close(d);
		}
		g_rmdir(path.c_str());
	} else {
		g_remove(path.c_str());
	}
}

static int syncTest(int argc, char** argv) {
	std::string server, only;
	if (const char* u = g_getenv("CL_SYNC_URL")) server = u;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--server") == 0 && i + 1 < argc) server = argv[++i];
		else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc) only = argv[++i];
	}
	gchar* dir = g_dir_make_tmp("cl-sync-test-XXXXXX", nullptr);
	if (!dir) { fprintf(stderr, "no temporary folder\n"); return 2; }
	std::string report;
	const bool ok = syncplatform::selfTest(dir, server, only, report);
	fputs(report.c_str(), stdout);
	removeTree(dir);
	g_free(dir);
	printf("%s\n", ok ? "sync self-test: all passed" : "sync self-test: FAILED");
	return ok ? 0 : 1;
}

int main(int argc, char** argv) {
	// Our own options, taken out before GTK sees the rest (files to open).
	std::vector<char*> args;
	for (int i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--version") == 0) { printf("CedarLogic %s (native Linux)\n", CL_VERSION); return 0; }
		if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) { gScreenshot = argv[++i]; continue; }
		if (strcmp(argv[i], "--dark") == 0 || strcmp(argv[i], "--light") == 0) { gTheme = strcmp(argv[i], "--dark") == 0; continue; }
		if (strcmp(argv[i], "--sim-view") == 0) { gSimView = true; continue; }
		if (strcmp(argv[i], "--show") == 0 && i + 1 < argc) { gShow = argv[++i]; continue; }
		if (strcmp(argv[i], "--lab-report") == 0 && i + 2 < argc) {
			gReportIn = argv[++i];
			gReportOut = argv[++i];
			if (i + 1 < argc && strcmp(argv[i + 1], "bw") == 0) { gReportBW = true; i++; }
			continue;
		}
		if (strcmp(argv[i], "--first") == 0) { gFirstLaunch = true; continue; }
		if (strcmp(argv[i], "--hold") == 0 && i + 1 < argc) { gHold = std::max(1, atoi(argv[++i])); continue; }
		if (strcmp(argv[i], "--bench-drag") == 0) {
			gBench = 60;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') gBench = std::max(5, atoi(argv[++i]));
			continue;
		}
		if (strcmp(argv[i], "--splash-frame") == 0 && i + 2 < argc) { gSplashAt = atof(argv[++i]); gSplashFile = argv[++i]; continue; }
		// --sync-test [--server <url>] [--only <name>]: the sync engine's own self-test with this
		// machine's hooks (libcrypto, zlib, curl); with a server, also against a mock one.
		if (strcmp(argv[i], "--sync-test") == 0) return syncTest(argc, argv);
		if (strcmp(argv[i], "--feedback-probe") == 0) {
			const int code = feedback::probe();
			printf("feedback server: %d\n", code);
			return code == 403 ? 0 : 1;
		}
		// A cedarlogic://open#c=… link (the website's Open in the App, a link
		// in a chat): its circuit goes in a file, which then opens as any
		// file handed over does, here or in the CedarLogic already running.
		// A cedarlogic://sync#k=… link carries the secret code: it's copied out of the process's
		// arguments (where any user could read them) before it goes on.
		if (sharelink::isSyncLink(argv[i])) {
			static std::deque<std::string> syncLinks;
			syncLinks.push_back(argv[i]);
			memset(argv[i], 0, strlen(argv[i]));
			args.push_back(const_cast<char*>(syncLinks.back().c_str()));
			continue;
		}
		if (strncmp(argv[i], "cedarlogic:", 11) == 0) {
			static std::deque<std::string> linkFiles;   // (they stay where they are as it grows)
			std::string why;
			const std::string file = sharelink::fileForLink(argv[i], why);
			if (file.empty()) { g_printerr("CedarLogic: that link couldn't be opened (%s)\n", why.c_str()); continue; }
			linkFiles.push_back(file);
			args.push_back(const_cast<char*>(linkFiles.back().c_str()));
			continue;
		}
		args.push_back(argv[i]);
	}
	args.push_back(nullptr);
	argc = (int)args.size() - 1;
	argv = args.data();
	if (gBench > 0) {
		// Settings and Your Circuits in a folder of its own (removed at the end),
		// and the screenshot runs' quiet start; the picture itself isn't taken.
		gchar* home = g_dir_make_tmp("cl-bench-XXXXXX", nullptr);
		if (home == nullptr) { fprintf(stderr, "bench-drag: no temporary folder\n"); return 2; }
		gBenchHome = home;
		g_free(home);
		g_setenv("XDG_DATA_HOME", (gBenchHome + "/data").c_str(), TRUE);
		g_setenv("XDG_CONFIG_HOME", (gBenchHome + "/config").c_str(), TRUE);
		gScreenshot = gBenchHome + "/unused.png";
	}
	// The window class and the name the desktop shows.
	g_set_prgname("CedarLogic");
	g_set_application_name("CedarLogic");
	// One CedarLogic at a time: launching it again (the menu, a .cdl
	// double-clicked) hands over to the one running, so two copies never edit
	// -- and save over -- the same circuit, or each other's settings. The
	// screenshot runs are each their own.
	const bool pictureRun = !gScreenshot.empty() || !gSplashFile.empty();
	if (!pictureRun) waitForPreviousCopy();
	GtkApplication* app = gtk_application_new(CL_APP_ID, (GApplicationFlags)(G_APPLICATION_HANDLES_OPEN | (pictureRun ? G_APPLICATION_NON_UNIQUE : 0)));
	g_signal_connect(app, "startup", G_CALLBACK(startupCb), nullptr);
	g_signal_connect(app, "activate", G_CALLBACK(activateCb), nullptr);
	g_signal_connect(app, "open", G_CALLBACK(openFilesCb), nullptr);
	const int status = g_application_run(G_APPLICATION(app), argc, argv);
	// A launch that handed over never loaded the settings: saving its
	// defaults would undo the running one's.
	if (gPrimary) prefs().save();
	// Every window is closed and saved: what sync hasn't sent yet goes now (five seconds at most).
	if (gPrimary) syncapp::quit();
	g_object_unref(app);
	if (!gBenchHome.empty()) removeTree(gBenchHome);
	return status ? status : gExitCode;
}
