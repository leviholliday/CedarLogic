// CedarLogic for Linux (native): the application -- its menus and shortcuts,
// opening circuits, and the list of open windows.

#include <deque>

#include "App.h"
#include "Bench.h"
#include "Canvas.h"
#include "Recovery.h"
#include "SessionCheck.h"
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
#include "Toolbar.h"
#include "MenuBar.h"
#include "Capture.h"
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
GMenu* gWindowMenu = nullptr;   // Window's list of the open circuits
GMenuModel* gMenubar = nullptr;
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
std::string gCaptureTest;   // --capture-test <folder>
// This process is the running CedarLogic (not a launch that handed over to
// it): it loaded the settings, so it may save them.
bool gPrimary = false;
// --bench-drag [steps] <circuit.cdl>: times the canvas while dragging and
// quits (Bench.cpp, linux/SMOOTHNESS.md). It runs as a screenshot run does
// (no splash, updater or sync), with its settings and Your Circuits in a
// temporary folder, so the person's own are never touched.
int gBench = 0;
std::string gBenchHome;
// --session-check [actions] [seed]: a long session in the window, checked
// (SessionCheck.cpp); as --bench-drag runs, with a home of its own.
int gSession = 0;
unsigned long long gSessionSeed = 1;

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
	else if (what == "truth") {
		// A page with no truth table to make -- a clocked circuit with more
		// than 8 switches (Lab5 has 10) or none -- opens Check My Circuit on
		// its own instead, as the Mac's does: that's the window to picture.
		top = toplevelTitled("Truth Table");
		if (!top && (top = toplevelTitled("Check My Circuit")))
			fprintf(stderr, "truth: no truth table for this page; Check My Circuit opened by itself, pictured instead\n");
	}
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
	if (what == "scope" && !circuitWindows().empty())
		if (ScopeWindow* scope = circuitWindows().back()->scopeWindow()) scope->describe();
	const bool ok = top && (what == "menu" || what == "submenu" || what == "combo" || what == "primary" ? writeWindowWithMenu(top, gScreenshot)
	                                                                                                      : writeWindow(top, gScreenshot));
	if (!top) {
		fprintf(stderr, "nothing to picture for --show %s; the windows open:", gShow.c_str());
		GList* all = gtk_window_list_toplevels();
		for (GList* l = all; l; l = l->next)
			if (gtk_widget_get_visible(GTK_WIDGET(l->data))) {
				const char* t = gtk_window_get_title(GTK_WINDOW(l->data));
				fprintf(stderr, " \"%s\" (%s)", t ? t : "", gtk_widget_get_name(GTK_WIDGET(l->data)));
			}
		g_list_free(all);
		fprintf(stderr, "\n");
	}
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
	else if (what == "submenu" || what == "primary" || what == "menu") {
		// The menu bar's menus, open as Alt and a letter opens them: menu:N
		// the Nth (0 File ... 5 Help); primary Help; submenu View with its
		// Drawing submenu open, over Simulation View, as on the Pi.
		static int open = 0;
		open = what == "primary" ? 5 : what == "submenu" ? 2 : page;
		if (what == "submenu") w->toggleSimView();
		g_timeout_add(400, +[](gpointer data) -> gboolean {
			CircuitWindow* cw = circuitWindows().empty() ? nullptr : circuitWindows().back();
			if (!cw || !cw->menuRowWidget() || !cw->menuRowWidget()->openMenu(open)) fprintf(stderr, "menu: no menu %d\n", open);
			return G_SOURCE_REMOVE;
		}, nullptr);
		if (what == "submenu")
			g_timeout_add(900, +[](gpointer) -> gboolean {
				GList* all = gtk_window_list_toplevels();
				for (GList* l = all; l; l = l->next) {
					GtkWidget* child = gtk_bin_get_child(GTK_BIN(l->data));
					if (!gtk_widget_get_visible(GTK_WIDGET(l->data)) || !child || !GTK_IS_MENU(child)) continue;
					GList* items = gtk_container_get_children(GTK_CONTAINER(child));
					for (GList* i = items; i; i = i->next) {
						const char* label = GTK_IS_MENU_ITEM(i->data) ? gtk_menu_item_get_label(GTK_MENU_ITEM(i->data)) : nullptr;
						if (label && strstr(label, "Dra_wing")) {
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
		prefs().inkTool = 0;   // the pen, whatever an earlier run left
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
	else if (what == "projector") {
		// Projector Mode: on (it opens Simulation View), pictured; Escape's
		// way out (leaving Simulation View) is run and reported, then it's
		// shown again for the picture. :1 turns it off again too.
		prefs().projector = false;
		w->toggleProjector();
		fprintf(stderr, "projector: on %d, Simulation View %d\n", prefs().projector, w->simView());
		w->toggleSimView();
		fprintf(stderr, "projector: after Escape, Simulation View %d (the editing view is back)\n", w->simView());
		w->toggleSimView();
		if (page == 1) {
			w->toggleProjector();
			fprintf(stderr, "projector: off again %d\n", !prefs().projector);
		}
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

gboolean sessionCb(gpointer app) {
	gExitCode = session::run(GTK_APPLICATION(app), gSession, gSessionSeed);
	for (CircuitWindow* c : std::vector<CircuitWindow*>(circuitWindows())) gtk_widget_destroy(GTK_WIDGET(c->window()));
	g_application_quit(G_APPLICATION(app));
	return G_SOURCE_REMOVE;
}

// --capture-test <folder>: Send Feedback's screenshot and recording of the
// window, checked (Capture.h), the files left in the folder; then quits.
gboolean captureTestCb(gpointer app) {
	CircuitWindow* w = circuitWindows().empty() ? nullptr : circuitWindows().back();
	gExitCode = 1;
	if (w) guarded("the capture test", [&] { gExitCode = capture::selfTest(w, gCaptureTest); });
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

// The menu bar every window shows under its title bar: File, Edit, View,
// Simulation, Window and Help, the Mac app's menus in its order, with the
// desktop's habits (mnemonics, Quit in File, Settings at the foot of Edit as
// Firefox and Inkscape keep it, About in Help). Keys show at the right of
// each item: a command's own come from the app (and follow Settings >
// Shortcuts by themselves); the canvas's single keys (A, R, S, T...) are
// written on their items here (an "accel" attribute, shown but never bound:
// the model isn't the application's menubar, so GTK doesn't take them as
// shortcuts) and follow Settings too (refreshMenuKeys).
GMenu* section(GMenu* menu) {
	GMenu* s = g_menu_new();
	g_menu_append_section(menu, nullptr, G_MENU_MODEL(s));
	g_object_unref(s);
	return s;
}

void add(GMenu* menu, const char* label, const char* action, const char* shownKeys = nullptr) {
	GMenuItem* item = g_menu_item_new(label, action);
	if (shownKeys) g_menu_item_set_attribute(item, "accel", "s", shownKeys);
	g_menu_append_item(menu, item);
	g_object_unref(item);
}

GMenu* submenu(GMenu* menu, const char* label) {
	GMenu* sub = g_menu_new();
	g_menu_append_submenu(menu, label, G_MENU_MODEL(sub));
	g_object_unref(sub);
	return sub;
}

GMenuModel* buildMenubar() {
	GMenu* bar = g_menu_new();

	GMenu* file = submenu(bar, "_File");
	GMenu* s = section(file);
	add(s, "_New Circuit", "app.new");
	add(s, "New from _Template…", "win.new-template");
	add(s, "Your _Circuits…", "app.open");
	add(s, "_Import a File…", "app.import");
	gRecentMenu = submenu(s, "Open _Recent");
	add(s, "Open the _Practice Circuit", "app.open-sample");
	s = section(file);
	add(s, "_Save a Version", "win.save");
	add(s, "Re_name…", "win.rename-circuit");
	add(s, "Du_plicate Circuit", "win.duplicate-circuit");
	add(s, "_Version History…", "win.versions");
	s = section(file);
	add(s, "_Export as Image…", "win.export-image");
	add(s, "Export _Lab Report…", "win.export-report");
	add(s, "Export as CedarLogic _File…", "win.save-as");
	GMenu* older = submenu(s, "Export for _Older CedarLogic");
	add(older, "For CedarLogic _2…", "win.export-v2");
	add(older, "For CedarLogic _1.x…", "win.export-v1");
	add(s, "Share Lin_k…", "app.share-link");
	add(s, "Save as Te_mplate…", "win.save-template");
	s = section(file);
	add(s, "P_rint…", "win.print");
	s = section(file);
	add(s, "_Close Window", "win.close");
	add(s, "_Quit", "app.quit");

	GMenu* edit = submenu(bar, "_Edit");
	s = section(edit);
	add(s, "_Undo", "win.undo");
	add(s, "_Redo", "win.redo");
	s = section(edit);
	add(s, "Cu_t", "win.cut");
	add(s, "_Copy", "win.copy");
	add(s, "_Paste", "win.paste");
	add(s, "D_uplicate", "win.duplicate");
	add(s, "_Delete", "win.delete", "Delete");
	add(s, "Select _All", "win.select-all");
	s = section(edit);
	add(s, "Add a _Gate…", "win.add-gate");
	add(s, "R_otate", "win.rotate");
	add(s, "_Lock in Place", "win.lock-in-place");
	add(s, "Unlock All Pa_rts", "win.unlock-all");
	add(s, "Gate _Settings…", "win.gate-settings");
	s = section(edit);
	GMenu* find = submenu(s, "_Find");
	add(find, "_Find…", "win.find");
	add(find, "Find _Next", "win.find-next");
	add(find, "Find _Previous", "win.find-previous");
	s = section(edit);
	add(s, "Save as Par_t…", "win.save-part");
	add(s, "_Build from Formula…", "win.build-formula");
	add(s, "Straighten _Wires", "win.straighten");
	add(s, "Tidy Up: _Keep My Layout", "win.tidy");
	add(s, "Tidy Up: _Full Rearrange", "win.tidy-flow");
	add(s, "Connect _Nearby Pins", "win.connect-nearby");
	s = section(edit);
	add(s, "S_ettings…", "win.preferences");

	GMenu* view = submenu(bar, "_View");
	s = section(view);
	add(s, "Zoom _In", "win.zoom-in");
	add(s, "Zoom _Out", "win.zoom-out");
	add(s, "Zoom to _Fit", "win.zoom-fit");
	add(s, "_Actual Size", "win.zoom-actual");
	s = section(view);
	add(s, "Focus _Mode", "win.focus-mode");
	add(s, "F_ull Screen", "win.fullscreen");
	add(s, "Side _Panel", "win.palette");
	add(s, "Status _Bar", "win.status-bar");
	add(s, "_Gridlines", "win.show-grid");
	add(s, "Wire Connection _Dots", "win.wire-dots");
	add(s, "Dar_k Mode", "win.dark");
	s = section(view);
	add(s, "_Simulation View", "win.sim-view");
	add(s, "Pro_jector Mode", "win.projector");
	add(s, "Osci_lloscope", "win.scope");
	s = section(view);
	add(s, "Draw on the _Circuit", "win.draw");
	GMenu* drawing = submenu(s, "Dra_wing");
	GMenu* tools = section(drawing);
	add(tools, "_Pen", "win.ink-pen", "p");
	add(tools, "_Highlighter", "win.ink-highlighter", "h");
	add(tools, "_Eraser", "win.ink-eraser", "e");
	add(section(drawing), "_Clear Drawing on This Page", "win.clear-drawing");
	add(s, "Show D_rawing", "win.show-drawing");
	add(s, "Show _Notes", "win.notes");

	GMenu* sim = submenu(bar, "_Simulation");
	s = section(sim);
	add(s, "_Simulation View", "win.sim-view");
	add(s, "_Running", "win.running");
	add(s, "S_tep Once", "win.step");
	add(s, "Step _Clock", "win.step-clock");
	s = section(sim);
	add(s, "_Truth Table…", "win.truth-table");
	add(s, "C_heck My Circuit…", "win.check-circuit");
	add(s, "_Oscilloscope", "win.scope");
	s = section(sim);
	add(s, "_Lock the Circuit", "win.lock");

	// Tabs live here (the Mac keeps them in File, under its own Window menu's
	// tab commands); then the open circuits, to bring one forward.
	GMenu* window = submenu(bar, "_Window");
	s = section(window);
	add(s, "_New Tab", "win.new-tab");
	add(s, "_Close Tab", "win.close-tab");
	add(s, "_Reopen Closed Tab", "win.reopen-tab");
	add(s, "Re_name Tab…", "win.rename-tab");
	add(s, "Ne_xt Tab", "win.next-tab");
	add(s, "_Previous Tab", "win.previous-tab");
	s = section(window);
	add(s, "_Split View", "win.split-view");
	add(s, "S_witch Side", "win.switch-pane");
	add(s, "Close Split _View", "win.close-split");
	// Which pages' TO/FROM links connect (docs/PAGE-LINKS.md); a tab's
	// right-click menu connects pages one by one.
	s = section(window);
	add(s, "Connect _All Pages", "win.connect-all-pages");
	add(s, "_Disconnect All Pages", "win.disconnect-all-pages");
	s = section(window);
	add(s, "_Minimize", "win.minimize");
	gWindowMenu = section(window);

	GMenu* help = submenu(bar, "_Help");
	s = section(help);
	add(s, "CedarLogic _Help", "win.help");
	add(s, "_Keyboard Shortcuts", "win.shortcuts");
	add(s, "Guided _Tour", "app.guided-tour");
	s = section(help);
	add(s, "_What's New in CedarLogic", "win.whats-new");
	add(s, "W_elcome to CedarLogic", "win.welcome");
	s = section(help);
	add(s, "Send _Feedback…", "win.feedback");
	add(s, "Check for _Updates…", "app.check-updates");
	s = section(help);
	add(s, "_About CedarLogic", "win.about");
	return G_MENU_MODEL(bar);
}

// The keys a canvas command has now, as an accelerator ("" none).
std::string canvasKeys(const char* id) {
	const shortcuts::Action* a = shortcuts::find(id);
	return a ? shortcuts::keys(*a) : std::string();
}

// An item's shown keys set to `keys` (in every menu under `model`).
void showKeys(GMenuModel* model, const char* action, const std::string& keys) {
	for (int i = 0; i < g_menu_model_get_n_items(model); i++) {
		gchar* a = nullptr;
		if (g_menu_model_get_item_attribute(model, i, G_MENU_ATTRIBUTE_ACTION, "s", &a) && a && std::strcmp(a, action) == 0 && G_IS_MENU(model)) {
			gchar* now = nullptr;
			g_menu_model_get_item_attribute(model, i, "accel", "s", &now);
			if (keys != (now ? now : "")) {
				GMenuItem* item = g_menu_item_new_from_model(model, i);
				if (keys.empty()) g_menu_item_set_attribute_value(item, "accel", nullptr);
				else g_menu_item_set_attribute(item, "accel", "s", keys.c_str());
				g_menu_remove(G_MENU(model), i);
				g_menu_insert_item(G_MENU(model), i, item);
				g_object_unref(item);
			}
			g_free(now);
		}
		g_free(a);
		GMenuLinkIter* links = g_menu_model_iterate_item_links(model, i);
		GMenuModel* sub = nullptr;
		while (g_menu_link_iter_get_next(links, nullptr, &sub)) {
			showKeys(sub, action, keys);
			g_object_unref(sub);
		}
		g_object_unref(links);
	}
}

}  // namespace

GMenuModel* appMenubar() { return gMenubar; }

void refreshMenuKeys() {
	if (gMenubar == nullptr) return;
	static const struct { const char* action; const char* id; } kCanvasKeys[] = {
		{ "win.add-gate", "addGate" }, { "win.rotate", "rotate" }, { "win.straighten", "straighten" },
		{ "win.step-clock", "stepClock" }, { "win.truth-table", "truthTable" }, { "win.check-circuit", "checkCircuit" },
	};
	for (const auto& k : kCanvasKeys) showKeys(gMenubar, k.action, canvasKeys(k.id));
	// Tidy's key runs the way Settings > Editing picks; the other way is a click.
	const std::string tidy = canvasKeys("tidy");
	showKeys(gMenubar, "win.tidy", prefs().tidyMode == 0 ? tidy : std::string());
	showKeys(gMenubar, "win.tidy-flow", prefs().tidyMode == 1 ? tidy : std::string());
}

void rebuildWindowMenu() {
	if (gWindowMenu == nullptr) return;
	g_menu_remove_all(gWindowMenu);
	const std::vector<CircuitWindow*>& all = circuitWindows();
	for (size_t i = 0; i < all.size() && i < 20; i++) {
		std::string label = all[i]->titleText();
		std::string escaped;
		for (char c : label) { if (c == '_') escaped += '_'; escaped += c; }
		GMenuItem* item = g_menu_item_new(escaped.c_str(), nullptr);
		g_menu_item_set_action_and_target_value(item, "app.show-window", g_variant_new_int32((gint32)i));
		g_menu_append_item(gWindowMenu, item);
		g_object_unref(item);
	}
}

namespace {

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
		// Window > (an open circuit): that window to the front.
		{ "show-window", [](GSimpleAction*, GVariant* v, gpointer) {
			const int i = g_variant_get_int32(v);
			if (i >= 0 && i < (int)circuitWindows().size()) gtk_window_present(circuitWindows()[i]->window());
		  }, "i", nullptr, nullptr, { 0 } },
	};
	g_action_map_add_action_entries(G_ACTION_MAP(app), entries, G_N_ELEMENTS(entries), app);
	setAccels(app);
	// Each window shows the bar itself (Window.cpp); it isn't the
	// application's menubar, so the canvas keys written on it stay unbound.
	gMenubar = buildMenubar();
	refreshMenuKeys();
	rebuildRecentMenus();
	rebuildWindowMenu();
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
			if (gSession > 0) g_timeout_add(1500, sessionCb, app);
			else if (gBench > 0) g_timeout_add(1500, benchCb, app);
			else if (!gCaptureTest.empty()) g_timeout_add(1500, captureTestCb, app);
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
			if (gSession > 0) g_timeout_add(1500, sessionCb, app);
			else if (gBench > 0) g_timeout_add(1500, benchCb, app);
			else if (!gCaptureTest.empty()) g_timeout_add(1500, captureTestCb, app);
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
	// The .deb update's root step (Updater.h), run by pkexec: before anything
	// else, and nothing but that.
	if (argc >= 2 && strcmp(argv[1], "--install-update") == 0) return Updater_InstallUpdateAsRoot(argc, argv);
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
		if (strcmp(argv[i], "--capture-test") == 0 && i + 1 < argc) {
			gCaptureTest = argv[++i];
			if (gScreenshot.empty()) gScreenshot = gCaptureTest + "/window.png";   // runs as a picture run does
			continue;
		}
		if (strcmp(argv[i], "--bench-drag") == 0) {
			gBench = 60;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') gBench = std::max(5, atoi(argv[++i]));
			continue;
		}
		if (strcmp(argv[i], "--session-check") == 0) {
			gSession = 300;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') gSession = std::max(1, atoi(argv[++i]));
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') gSessionSeed = strtoull(argv[++i], nullptr, 10);
			continue;
		}
		if (strcmp(argv[i], "--splash-frame") == 0 && i + 2 < argc) { gSplashAt = atof(argv[++i]); gSplashFile = argv[++i]; continue; }
		// --sync-test [--server <url>] [--only <name>]: the sync engine's own self-test with this
		// machine's hooks (libcrypto, zlib, curl); with a server, also against a mock one.
		if (strcmp(argv[i], "--sync-test") == 0) return syncTest(argc, argv);
		// --update-self-test [--release <folder> --key <hex>]: signed updates (Updater.h).
		if (strcmp(argv[i], "--update-self-test") == 0) return Updater_SelfTest(argc, argv);
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
	if (gSession > 0) session::prepare();
	if (gBench > 0 || gSession > 0) {
		// Settings and Your Circuits in a folder of its own (removed at the end),
		// and the screenshot runs' quiet start; the picture itself isn't taken.
		gchar* home = g_dir_make_tmp(gSession > 0 ? "cl-session-XXXXXX" : "cl-bench-XXXXXX", nullptr);
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
