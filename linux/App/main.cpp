// CedarLogic for Linux (native): the application -- its menus and shortcuts,
// opening circuits, and the list of open windows.

#include "App.h"
#include "Canvas.h"
#include "Recovery.h"
#include "Splash.h"
#include "Updater.h"
#include "Welcome.h"
#include "Collections.h"
#include "Dialogs.h"
#include "Feedback.h"
#include "Help.h"
#include "Library.h"
#include "LibraryWindow.h"
#include "Window.h"

#include <algorithm>
#include <cstring>

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
// --splash-frame <t> <out.png> [--first]: the launch screen at t seconds.
double gSplashAt = -1;
std::string gSplashFile;
bool gFirstLaunch = false;

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

// The window --show opened, pictured; then the app ends (a modal window may
// be running its own loop, so this doesn't wait for it).
gboolean showCaptureCb(gpointer) {
	GtkWidget* top = nullptr;
	const std::string what = gShow.substr(0, gShow.find(':'));
	if (what == "welcome") top = welcome::window();
	else if (what == "whatsnew") top = whatsnew::window();
	else if (what == "help") top = help::window();
	else if (what == "truth") top = toplevelTitled("Truth Table");
	else if (what == "feedback") top = toplevelTitled("Send Feedback");
	else if (what == "templates") top = toplevelTitled("New from Template");
	else if (what == "export") top = toplevelTitled("Export as Image");
	else if (what == "scope") top = toplevelTitled("Oscilloscope");
	else if (!circuitWindows().empty()) top = GTK_WIDGET(circuitWindows().back()->window());
	const bool ok = top && writeWindow(top, gScreenshot);
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
	else if (what == "feedback") feedback::show(w);
	else if (what == "templates") templates::showPicker(w);
	else if (what == "tour") welcome::startTour(w);
	else if (what == "find") w->runAction("win.find");
	else if (what == "export") w->exportImage();
	else if (what == "scope") w->toggleScope();
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
	g_menu_append(s2, "Save as Te_mplate…", "win.save-template");
	g_menu_append(s2, "_Export as Image…", "win.export-image");
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
	g_menu_append_section(edit, nullptr, G_MENU_MODEL(e2));
	GMenu* e3 = g_menu_new();
	g_menu_append(e3, "Add a _Gate…   (A)", "win.add-gate");
	g_menu_append(e3, "Gate _Settings…", "win.gate-settings");
	g_menu_append(e3, "_Rotate   (R)", "win.rotate");
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
	g_menu_append(v2, "Gate _Palette", "win.palette");
	g_menu_append(v2, "_Status Bar", "win.status-bar");
	g_menu_append_section(view, nullptr, G_MENU_MODEL(v2));
	g_menu_append_submenu(bar, "_View", G_MENU_MODEL(view));

	GMenu* sim = g_menu_new();
	GMenu* m1 = g_menu_new();
	g_menu_append(m1, "_Running", "win.running");
	g_menu_append(m1, "_Step Once", "win.step");
	g_menu_append(m1, "Simulation _View", "win.sim-view");
	g_menu_append(m1, "_Lock the Circuit", "win.lock");
	g_menu_append_section(sim, nullptr, G_MENU_MODEL(m1));
	GMenu* m2 = g_menu_new();
	g_menu_append(m2, "_Truth Table   (T)", "win.truth-table");
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

void setAccels(GtkApplication* app) {
	struct { const char* action; const char* keys[4]; } accels[] = {
		{ "app.new", { "<Primary>n" } },
		{ "app.open", { "<Primary>o" } },
		{ "app.import", { "<Primary>i" } },
		{ "app.quit", { "<Primary>q" } },
		{ "win.save", { "<Primary>s" } },
		{ "win.save-as", { "<Primary><Shift>s" } },
		{ "win.export-image", { "<Primary>e" } },
		{ "win.print", { "<Primary>p" } },
		{ "win.close", { "<Primary><Shift>w" } },
		{ "win.undo", { "<Primary>z" } },
		{ "win.redo", { "<Primary><Shift>z", "<Primary>y" } },
		{ "win.cut", { "<Primary>x" } },
		{ "win.copy", { "<Primary>c" } },
		{ "win.paste", { "<Primary>v" } },
		{ "win.duplicate", { "<Primary>d" } },
		{ "win.select-all", { "<Primary>a" } },
		{ "win.find", { "<Primary>f" } },
		{ "win.zoom-in", { "<Primary>equal", "<Primary>plus", "<Primary>KP_Add" } },
		{ "win.zoom-out", { "<Primary>minus", "<Primary>KP_Subtract" } },
		{ "win.zoom-fit", { "<Primary>0", "<Primary>KP_0" } },
		{ "win.zoom-actual", { "<Primary>1", "<Primary>KP_1" } },
		{ "win.dark", { "<Primary><Shift>d" } },
		{ "win.palette", { "<Primary>period" } },
		{ "win.preferences", { "<Primary>comma" } },
		{ "win.step", { "<Primary><Shift>r" } },
		{ "win.sim-view", { "<Primary>r" } },
		{ "win.scope", { "<Primary>g" } },
		{ "win.new-tab", { "<Primary>t" } },
		{ "win.close-tab", { "<Primary>w" } },
		{ "win.reopen-tab", { "<Primary><Shift>t" } },
		{ "win.next-tab", { "<Primary>Page_Down" } },
		{ "win.previous-tab", { "<Primary>Page_Up" } },
		{ "win.shortcuts", { "<Primary>question", "<Primary>slash" } },
		{ "win.help", { "F1" } },
	};
	for (auto& a : accels) gtk_application_set_accels_for_action(app, a.action, a.keys);
}

CircuitWindow* activeWindow(GtkApplication* app) {
	GtkWindow* w = gtk_application_get_active_window(app);
	for (CircuitWindow* c : circuitWindows()) if (c->window() == w) return c;
	return circuitWindows().empty() ? nullptr : circuitWindows().back();
}

void newCb(GSimpleAction*, GVariant*, gpointer app) { newCircuitWindow(GTK_APPLICATION(app)); }

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

void quitCb(GSimpleAction*, GVariant*, gpointer app) { quitApp(GTK_APPLICATION(app)); }

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

void startupCb(GApplication* gapp, gpointer) {
	GtkApplication* app = GTK_APPLICATION(gapp);
	// Not for --screenshot: CI wants one deterministic frame, not a race
	// with a timed splash.
	if (gScreenshot.empty() && gSplashFile.empty()) gSplash = showSplash();
	prefs().load();
	if (gTheme >= 0) prefs().dark = gTheme == 1;
	applyTheme();
	loadCss();
	setIcon();

	if (gSplash) splashSetStatus(gSplash, "Loading the gate library…");
	const std::string lib = resourcesDir().empty() ? std::string() : resourcesDir() + "/cl_gatedefs.xml";
	gLibraryLoaded = !lib.empty() && cl_library_load(lib.c_str());
	prefs().applyWireDots();
	if (gSplash) splashSetStatus(gSplash, gLibraryLoaded ? "Opening the workspace…" : "Couldn't find the gate library");

	const GActionEntry entries[] = {
		{ "new", newCb, nullptr, nullptr, nullptr, { 0 } },
		{ "open", openCb, nullptr, nullptr, nullptr, { 0 } },
		{ "import", importCb, nullptr, nullptr, nullptr, { 0 } },
		{ "open-recent", openRecentCb, "s", nullptr, nullptr, { 0 } },
		{ "open-sample", openSampleCb, nullptr, nullptr, nullptr, { 0 } },
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
	Updater_Initialize(app);
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

void activateCb(GApplication* gapp, gpointer) {
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
			if (!gShow.empty()) g_timeout_add(1200, showCb, app);
			else g_timeout_add(2000, screenshotCb, app);
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
}

void openFilesCb(GApplication* gapp, GFile** files, gint n, const gchar*, gpointer) {
	if (!libraryOrComplain()) {
		gExitCode = 1;
		if (gSplash) { gtk_widget_destroy(gSplash); gSplash = nullptr; }
		g_application_quit(gapp);
		return;
	}
	bool any = false;
	for (gint i = 0; i < n; i++) {
		gchar* path = g_file_get_path(files[i]);
		if (path == nullptr) continue;
		any = openCircuit(GTK_APPLICATION(gapp), path, nullptr) || any;
		g_free(path);
	}
	if (!any && circuitWindows().empty()) newCircuitWindow(GTK_APPLICATION(gapp));
	if (gSplash) for (CircuitWindow* c : circuitWindows()) gtk_widget_hide(GTK_WIDGET(c->window()));
	hideSplashSoon(gSplash, +[](gpointer app) -> gboolean {
		for (CircuitWindow* c : circuitWindows()) { gtk_widget_show(GTK_WIDGET(c->window())); if (gScreenshot.empty()) c->beginOpening(); }
		if (!gScreenshot.empty()) {
			if (gSimView) for (CircuitWindow* c : circuitWindows()) c->toggleSimView();
			if (!gShow.empty()) g_timeout_add(1200, showCb, app);
			else g_timeout_add(2000, screenshotCb, app);
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
		gchar* dir = g_path_get_dirname(path.c_str());
		prefs().lastFolder = dir;
		g_free(dir);
	}
	CircuitWindow* w;
	if (from && from->isPristine()) { from->replaceDocument(doc, target); w = from; }
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
		// Underscores in a file name aren't mnemonics.
		gchar* label = g_strdup(baseName(path).c_str());
		std::string escaped;
		for (const char* p = label; *p; p++) { if (*p == '_') escaped += '_'; escaped += *p; }
		g_free(label);
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

int main(int argc, char** argv) {
	// Our own options, taken out before GTK sees the rest (files to open).
	std::vector<char*> args;
	for (int i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--version") == 0) { printf("CedarLogic %s (native Linux)\n", CL_VERSION); return 0; }
		if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) { gScreenshot = argv[++i]; continue; }
		if (strcmp(argv[i], "--dark") == 0 || strcmp(argv[i], "--light") == 0) { gTheme = strcmp(argv[i], "--dark") == 0; continue; }
		if (strcmp(argv[i], "--sim-view") == 0) { gSimView = true; continue; }
		if (strcmp(argv[i], "--show") == 0 && i + 1 < argc) { gShow = argv[++i]; continue; }
		if (strcmp(argv[i], "--first") == 0) { gFirstLaunch = true; continue; }
		if (strcmp(argv[i], "--splash-frame") == 0 && i + 2 < argc) { gSplashAt = atof(argv[++i]); gSplashFile = argv[++i]; continue; }
		if (strcmp(argv[i], "--feedback-probe") == 0) {
			const int code = feedback::probe();
			printf("feedback server: %d\n", code);
			return code == 403 ? 0 : 1;
		}
		args.push_back(argv[i]);
	}
	args.push_back(nullptr);
	argc = (int)args.size() - 1;
	argv = args.data();
	// The window class and the name the desktop shows.
	g_set_prgname("CedarLogic");
	g_set_application_name("CedarLogic");
	// Each launch is its own process: one that misbehaves can't take the
	// others with it.
	GtkApplication* app = gtk_application_new(CL_APP_ID, (GApplicationFlags)(G_APPLICATION_NON_UNIQUE | G_APPLICATION_HANDLES_OPEN));
	g_signal_connect(app, "startup", G_CALLBACK(startupCb), nullptr);
	g_signal_connect(app, "activate", G_CALLBACK(activateCb), nullptr);
	g_signal_connect(app, "open", G_CALLBACK(openFilesCb), nullptr);
	const int status = g_application_run(G_APPLICATION(app), argc, argv);
	prefs().save();
	g_object_unref(app);
	return status ? status : gExitCode;
}
