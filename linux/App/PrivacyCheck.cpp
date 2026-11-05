// --privacy-check: the consent in real windows, clicked with the real
// pointer (see PrivacyCheck.h). The Mac's consent sheet once crashed on a
// real window on first launch, so this is never only a picture.

#include "PrivacyCheck.h"
#include "App.h"
#include "Privacy.h"
#include "Settings.h"
#include "ShowMeAround.h"
#include "SyncJson.h"
#include "UsageReport.h"
#include "UsageStats.h"
#include "Welcome.h"
#include "Window.h"

#include <glib/gstdio.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>

namespace privacycheck {

namespace json = clsync::json;

namespace {

int gFailed = 0, gPassed = 0;

void check(bool ok, const std::string& what) {
	printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
	fflush(stdout);
	(ok ? gPassed : gFailed)++;
}

double nowMs() { return g_get_monotonic_time() / 1000.0; }

// The main loop, for a while: drawing, timers, the X server's events.
void pump(double ms) {
	const double end = nowMs() + ms;
	do {
		while (g_main_context_iteration(nullptr, FALSE)) {}
		if (nowMs() >= end) break;
		g_usleep(2000);
	} while (true);
}

bool waitFor(const std::function<bool()>& f, double ms) {
	const double end = nowMs() + ms;
	while (nowMs() < end) {
		if (f()) return true;
		pump(50);
	}
	return f();
}

bool xdo(const std::vector<std::string>& args) {
	std::vector<gchar*> argv;
	argv.push_back((gchar*)"xdotool");
	for (const std::string& a : args) argv.push_back(const_cast<gchar*>(a.c_str()));
	argv.push_back(nullptr);
	gint status = 1;
	const bool ran = g_spawn_sync(nullptr, argv.data(), nullptr, (GSpawnFlags)(G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL),
	                              nullptr, nullptr, nullptr, nullptr, &status, nullptr);
	return ran && g_spawn_check_exit_status(status, nullptr);
}

// A control of the question clicked with the real pointer: moved there, pressed and let go.
bool clickControl(privacy::Control c, std::string& why) {
	int x = 0, y = 0;
	if (!privacy::controlPoint(c, x, y)) {
		why = "(not drawn)";
		return false;
	}
	why = format("(at %d, %d)", x, y);
	const bool ok = xdo({ "mousemove", "--sync", format("%d", x), format("%d", y) });
	pump(120);
	return ok && xdo({ "click", "1" });
}

// A key as the keyboard sends it, to a window (as the session check's).
void pressKey(GtkWidget* toplevel, guint keyval) {
	if (toplevel == nullptr) return;
	g_object_ref(toplevel);
	for (bool press : { true, false }) {
		GdkWindow* gw = gtk_widget_in_destruction(toplevel) ? nullptr : gtk_widget_get_window(toplevel);
		if (gw == nullptr) break;
		GdkEvent* e = gdk_event_new(press ? GDK_KEY_PRESS : GDK_KEY_RELEASE);
		e->key.window = GDK_WINDOW(g_object_ref(gw));
		e->key.time = GDK_CURRENT_TIME;
		e->key.keyval = keyval;
		GdkKeymapKey* keys = nullptr;
		gint n = 0;
		if (gdk_keymap_get_entries_for_keyval(gdk_keymap_get_for_display(gdk_window_get_display(gw)), keyval, &keys, &n) && n > 0) {
			e->key.hardware_keycode = (guint16)keys[0].keycode;
			e->key.group = (guint8)keys[0].group;
		}
		g_free(keys);
		e->key.string = g_strdup("");
		if (GdkSeat* seat = gdk_display_get_default_seat(gdk_window_get_display(gw)))
			if (GdkDevice* k = gdk_seat_get_keyboard(seat)) gdk_event_set_device(e, k);
		gtk_main_do_event(e);
		gdk_event_free(e);
		pump(30);
	}
	g_object_unref(toplevel);
}

std::string ini() {
	gchar* path = g_build_filename(g_get_user_config_dir(), "CedarLogic", "native.ini", nullptr);
	gchar* t = nullptr;
	std::string s;
	if (g_file_get_contents(path, &t, nullptr, nullptr)) s = t;
	g_free(t);
	g_free(path);
	return s;
}
bool iniHas(const std::string& line) { return ("\n" + ini()).find("\n" + line + "\n") != std::string::npos; }

void notAsked() {
	prefs().privacyAsked = false;
	prefs().privacyChoice.clear();
	prefs().save();
}

void walk(GtkWidget* w, const std::function<void(GtkWidget*)>& f) {
	f(w);
	if (GTK_IS_CONTAINER(w)) {
		GList* kids = gtk_container_get_children(GTK_CONTAINER(w));
		for (GList* l = kids; l; l = l->next) walk(GTK_WIDGET(l->data), f);
		g_list_free(kids);
	}
}

// The window fits the screen it's on.
bool onScreen(GtkWidget* win, std::string& why) {
	GdkWindow* gw = win ? gtk_widget_get_window(win) : nullptr;
	if (gw == nullptr) return false;
	GdkMonitor* mon = gdk_display_get_monitor_at_window(gdk_window_get_display(gw), gw);
	GdkRectangle m{};
	if (mon) gdk_monitor_get_geometry(mon, &m);
	int x = 0, y = 0;
	gdk_window_get_origin(gw, &x, &y);
	const int w = gdk_window_get_width(gw), h = gdk_window_get_height(gw);
	why = format("%dx%d at %d,%d on a %dx%d screen", w, h, x, y, m.width, m.height);
	return mon && x >= m.x && y >= m.y && x + w <= m.x + m.width && y + h <= m.y + m.height;
}

void policyChecks(CircuitWindow* w) {
	check(privacy::crashPolicyOff() && privacy::usagePolicyOff(),
	      "policy: DisableCrashReporting and DisableUsageStats are read from " + usage::policyFile());
	check(!privacy::crashOffered() && !privacy::usageOffered() && !privacy::anythingToAsk(), "policy: neither is offered");
	prefs().hasSeenWelcome = true;
	notAsked();
	privacy::askWhenReady();
	pump(1500);
	check(!privacy::owed() && !privacy::sheetOpen() && !privacy::watching(), "policy: nobody is asked (no sheet)");
	gchar* tmp = g_dir_make_tmp("cl-policy-XXXXXX", nullptr);
	{
		usage::Engine e(tmp ? tmp : "/tmp");
		e.policyOff = [] { return usagestats::policyOff(); };
		e.set(true);
		e.count("save");
		e.noteSession();
		check(!e.enabled() && e.counters().isZero(), "policy: even with the switch on, nothing is counted");
	}
	if (tmp) {
		gchar* f = g_build_filename(tmp, "usage.json", nullptr);
		g_remove(f);
		g_free(f);
		g_rmdir(tmp);
		g_free(tmp);
	}
	// The welcome passes "A small favour" by.
	prefs().hasSeenWelcome = false;
	welcome::offer(w);
	welcome::pageForScreenshot(4);
	pump(400);
	pressKey(welcome::window(), GDK_KEY_Right);
	pump(600);
	check(welcome::window() && privacy::askWindow() == nullptr, "policy: the welcome goes past \"A small favour\"");
	welcome::closeForCheck(2);
	pump(300);
	// Settings > Privacy: both switches greyed out.
	settings::show(w, settings::kPrivacyPage);
	pump(800);
	int switches = 0, enabled = 0;
	if (GtkWidget* page = settings::currentPage())
		walk(page, [&](GtkWidget* x) {
			if (!GTK_IS_SWITCH(x)) return;
			switches++;
			if (gtk_widget_is_sensitive(x)) enabled++;
		});
	check(switches == 2 && enabled == 0, format("policy: Settings > Privacy has both switches greyed out (%d of %d enabled)", enabled, switches));
	if (GtkWidget* s = settings::window()) gtk_widget_destroy(s);
	pump(200);
}

}  // namespace

int consentCheck(CircuitWindow* w) {
	if (w == nullptr) {
		check(false, "a circuit window");
		return 1;
	}
	const bool x11 = strcmp(G_OBJECT_TYPE_NAME(gdk_display_get_default()), "GdkX11Display") == 0;
	gchar* tool = g_find_program_in_path("xdotool");
	check(x11 && tool, "an X display and xdotool, for the real pointer");
	g_free(tool);
	if (gFailed) return 1;
	gtk_window_present(w->window());
	pump(800);
	// The question asks by itself here, as in a real launch (on this window).
	privacy::quiet = false;
	showme::setAssumeKeys(true);
	if (g_getenv("CL_EXPECT_POLICY")) {
		guarded("the privacy check", [&] { policyChecks(w); });
		privacy::quiet = true;
		printf("privacy check (policy): %d passed, %d failed\n", gPassed, gFailed);
		return gFailed ? 1 : 0;
	}
	printf("     (crash reports %s)\n", privacy::crashInBuild() ? "in this build" : "not in this build: the answer is kept, nothing sent");
	std::string why;
	guarded("the privacy check", [&] {
		// ---- The welcome's "A small favour" page.
		prefs().hasSeenWelcome = false;
		notAsked();
		welcome::offer(w);
		welcome::pageForScreenshot(5);
		pump(900);
		GtkWidget* page = privacy::askWindow();
		check(page != nullptr && gtk_widget_get_mapped(page), "welcome: \"A small favour\" is up, page 6 of 7, in a real window");
		check(page && onScreen(page, why), "welcome: it fits the screen (" + why + ")");
		int tx, ty;
		check(privacy::controlPoint(privacy::CtlYes, tx, ty) && privacy::controlPoint(privacy::CtlNo, tx, ty) &&
		          privacy::controlPoint(privacy::CtlChoose, tx, ty) && privacy::controlPoint(privacy::CtlSeeSent, tx, ty),
		      "welcome: Yes, No thanks, Choose what to send and See exactly what's sent are drawn");
		pressKey(page, GDK_KEY_Right);
		pressKey(page, GDK_KEY_Return);
		pump(600);
		check(privacy::askWindow() == page && !prefs().privacyAsked, "welcome: no Continue -- Right and Enter don't go on without an answer");
		const bool clicked = clickControl(privacy::CtlNo, why);
		pump(1000);
		check(clicked, "welcome: No thanks clicked with the real pointer " + why);
		check(prefs().privacyAsked && prefs().privacyChoice == "no" && !prefs().usageStats && !usagestats::on() && prefs().crashReports == 0,
		      "welcome: No thanks -- asked, \"no\", usage stats off, crash reports off");
		check(iniHas("privacyAsked=true") && iniHas("privacyChoice=no") && iniHas("usageStats=false") && iniHas("crashReports=0"),
		      "welcome: ...saved in native.ini");
		check(welcome::window() != nullptr && privacy::askWindow() == nullptr, "welcome: and on to the last page by itself, no crash");
		welcome::closeForCheck(2);
		pump(400);
		privacy::askWhenReady();
		pump(1200);
		check(!privacy::sheetOpen(), "asked once: no sheet after the welcome answered it");
		showme::stop();
		showme::stopWatching();

		// ---- The updaters' sheet, in a real launch's order.
		prefs().hasSeenWelcome = true;
		prefs().showMeAroundSeen = false;
		prefs().seenWhatsNew = "an older version";
		notAsked();
		showme::setQuiet(false);
		whatsnew::show(w);
		privacy::askWhenReady();
		showme::startWhenReady();
		pump(1600);
		check(whatsnew::window() != nullptr && !privacy::sheetOpen() && !showme::active(), "sheet: never over What's New (nor Show Me Around)");
		pressKey(whatsnew::window(), GDK_KEY_Escape);
		const bool up = waitFor([] { return privacy::sheetOpen() && privacy::askWindow() != nullptr; }, 5000);
		check(up && !showme::active(), "sheet: after What's New closes it comes up over the circuit window, before Show Me Around");
		GtkWidget* sheet = privacy::askWindow();
		int w1 = 0, h1 = 0, w2 = 0, h2 = 0;
		if (sheet) gtk_window_get_size(GTK_WINDOW(sheet), &w1, &h1);
		const GdkRectangle room = usableArea(sheet);
		check(sheet && gtk_widget_get_mapped(sheet) && w1 == std::min(640, room.width) &&
		          gtk_window_get_transient_for(GTK_WINDOW(sheet)) == w->window() && gtk_window_get_modal(GTK_WINDOW(sheet)),
		      format("sheet: a real window, %dx%d (640 wide, or the screen's room), modal over the circuit window", w1, h1));
		check(sheet && onScreen(sheet, why), "sheet: it fits the screen (" + why + ")");
		pump(2800);   // (the card's glow, all the way through)
		if (sheet && privacy::askWindow() == sheet) gtk_window_get_size(GTK_WINDOW(sheet), &w2, &h2);
		check(w1 == w2 && h1 == h2, "sheet: its size holds as it settles");
		const bool yes = clickControl(privacy::CtlYes, why);
		pump(900);
		check(yes, "sheet: Yes clicked with the real pointer " + why);
		check(!privacy::sheetOpen() && prefs().privacyAsked && prefs().privacyChoice == "yes" && prefs().usageStats && usagestats::on() &&
		          prefs().crashReports == 1,
		      "sheet: Yes -- closed, \"yes\", usage stats on with a random id, crash reports' answer kept (on), no crash");
		check(iniHas("privacyAsked=true") && iniHas("privacyChoice=yes") && iniHas("usageStats=true"), "sheet: ...saved in native.ini");
		check(waitFor([] { return showme::active(); }, 5000), "sheet: Show Me Around follows it");
		showme::end();
		pump(400);
		showme::setQuiet(true);
		privacy::askWhenReady();
		pump(1200);
		check(!privacy::sheetOpen(), "asked once: no sheet again");

		// ---- Escape is No thanks (and keeps an earlier crash reports yes).
		notAsked();
		privacy::presentSheet(w);
		pump(700);
		pressKey(privacy::askWindow(), GDK_KEY_Escape);
		pump(600);
		check(!privacy::sheetOpen() && prefs().privacyChoice == "no" && !usagestats::on(), "sheet: Escape is No thanks (usage stats off again)");
		check(prefs().crashReports == 1, "sheet: No thanks leaves crash reports' yes as it was");

		// ---- Choose what to send: the switches, then Continue with these.
		notAsked();
		privacy::presentSheet(w);
		pump(700);
		const bool chose = clickControl(privacy::CtlChoose, why);
		pump(500);
		int tx2, ty2;
		check(chose && privacy::choosing() && privacy::controlPoint(privacy::CtlContinue, tx2, ty2) && !privacy::controlPoint(privacy::CtlYes, tx2, ty2),
		      "sheet: Choose what to send shows the two switches and Continue with these " + why);
		const bool usageBefore = privacy::switchState(false), crashBefore = privacy::switchState(true);
		const bool flipped = clickControl(privacy::CtlUsageSwitch, why);
		pump(300);
		check(flipped && privacy::switchState(false) != usageBefore, "sheet: the usage stats switch flips with a click " + why);
		clickControl(privacy::CtlCrashSwitch, why);
		pump(300);
		check(privacy::switchState(true) != crashBefore, "sheet: the crash reports switch flips with a click " + why);
		const bool usageWant = privacy::switchState(false), crashWant = privacy::switchState(true);
		const bool cont = clickControl(privacy::CtlContinue, why);
		pump(700);
		check(cont && !privacy::sheetOpen() && prefs().privacyChoice == "custom" && usagestats::on() == usageWant &&
		          prefs().crashReports == (crashWant ? 1 : 0),
		      format("sheet: Continue with these -- \"custom\", usage stats %s, crash reports %s", usageWant ? "on" : "off", crashWant ? "on" : "off"));
		check(iniHas("privacyChoice=custom") && iniHas(std::string("usageStats=") + (usageWant ? "true" : "false")), "sheet: ...saved in native.ini");

		// ---- See exactly what's sent: opens over the sheet, Done closes it.
		notAsked();
		privacy::presentSheet(w);
		pump(700);
		const bool sentClick = clickControl(privacy::CtlSeeSent, why);
		const bool sentUp = waitFor([] { return privacy::whatsSentWindow() && gtk_widget_get_mapped(privacy::whatsSentWindow()); }, 3000);
		std::string shown;
		if (GtkWidget* sw = privacy::whatsSentWindow())
			walk(sw, [&](GtkWidget* x) {
				if (!GTK_IS_TEXT_VIEW(x)) return;
				GtkTextBuffer* b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(x));
				GtkTextIter s, e;
				gtk_text_buffer_get_bounds(b, &s, &e);
				gchar* t = gtk_text_buffer_get_text(b, &s, &e, FALSE);
				shown = t ? t : "";
				g_free(t);
			});
		check(sentClick && sentUp && privacy::sheetOpen(), "sheet: See exactly what's sent opens over it " + why);
		json::Value preview;
		const bool parsed = json::parse(shown, preview);
		const json::Value* app = parsed ? preview.get("app") : nullptr;
		check(parsed && preview.integer("v") == 1 && app && app->str("platform") == "linux" && !preview.str("settingsHash").empty() &&
		          shown.find('\n') != std::string::npos,
		      "what's sent: the real report, pretty-printed JSON (v 1, platform linux, a settings hash)");
		const std::vector<std::string> leaked = usage::leaks(shown, usage::machineNeedles());
		check(leaked.empty(), "what's sent: no user name, host name or home folder in it");
		GtkWidget* done = nullptr;
		if (GtkWidget* sw = privacy::whatsSentWindow())
			walk(sw, [&](GtkWidget* x) { if (GTK_IS_BUTTON(x) && strcmp(gtk_widget_get_name(x), "whats-sent-done") == 0) done = x; });
		if (done) gtk_button_clicked(GTK_BUTTON(done));
		pump(400);
		check(privacy::whatsSentWindow() == nullptr && privacy::sheetOpen(), "what's sent: Done closes it, the sheet still up");
		pressKey(privacy::askWindow(), GDK_KEY_Escape);
		pump(600);
		check(!privacy::sheetOpen() && prefs().privacyAsked, "sheet: and Escape answers it");

		// ---- Settings > Privacy: the switch, Last sent, Delete what's been sent.
		settings::show(w, settings::kPrivacyPage);
		pump(800);
		int switches = 0, enabled = 0;
		if (GtkWidget* pg = settings::currentPage())
			walk(pg, [&](GtkWidget* x) {
				if (!GTK_IS_SWITCH(x)) return;
				switches++;
				if (gtk_widget_is_sensitive(x)) enabled++;
			});
		check(switches == 2 && enabled == 2, format("Settings > Privacy: two switches, both take a click (%d of %d)", enabled, switches));
		if (GtkWidget* s = settings::window()) gtk_widget_destroy(s);
		pump(200);
	});
	privacy::quiet = true;
	if (privacy::sheetOpen()) privacy::closeSheet();
	printf("privacy check: %d passed, %d failed\n", gPassed, gFailed);
	return gFailed ? 1 : 0;
}

}  // namespace privacycheck
