// One consent for crash reports and usage stats (see Privacy.h).

#include "Privacy.h"
#include "CrashReports.h"
#include "ShowMeAround.h"
#include "UsageReport.h"
#include "UsageStats.h"
#include "Welcome.h"
#include "Window.h"

#include <algorithm>

namespace privacy {

// ---- The words -------------------------------------------------------------------

const char* const kEyebrow = "Help improve CedarLogic";
const char* const kTitle = "A small favour";
const char* const kNote =
	"I work on this version of CedarLogic in my spare time, for free. I can't see what breaks or what people actually use unless you tell me. "
	"If you're willing, let the app send anonymous crash reports and usage stats. It would genuinely help, and nothing identifies you: no "
	"name, no circuits, no files. Thank you either way.";
const char* const kSignature = "Levi";
const char* const kYes = "Yes, send anonymous reports";
const char* const kNo = "No thanks";
const char* const kLater = "You can change this any time in Settings > Privacy.";
const char* const kCrashLine =
	"If CedarLogic crashes: where in the app it went wrong (the code's stack), the Linux and CedarLogic versions. Paths in it lose your "
	"user name. Sent through Sentry, only after a crash.";
const char* const kUsageLine =
	"At most once a day: how often CedarLogic is opened and for how long, the time spent in focus mode and Simulation View, whether sync "
	"is on, how many times each command is used, and which settings you've changed from their defaults. Only what's new since the last "
	"report goes; when nothing is, a short \"still here\".";
const char* const kIdLine =
	"Each report carries a random id, made when you turn this on (nothing about your computer). Turning it off, or Delete what's been sent, "
	"deletes everything sent under it.";

const std::vector<std::string>& neverSent() {
	static const std::vector<std::string> v = {
		"Your name, or the name on your exports",
		"Your circuits: their names, contents, notes or drawings",
		"File names, folders or anything you type",
		"Your sync code",
		"Your computer's name, your user name, its serial number or IP address (the server keeps no IP)",
		"Your time zone or exact times (dates, and the minute in UTC)",
	};
	return v;
}

// ---- The answer ------------------------------------------------------------------

bool quiet = false;

bool asked() { return prefs().privacyAsked; }

Choice choice() {
	const std::string& c = prefs().privacyChoice;
	return c == "yes" ? Choice::Yes : c == "no" ? Choice::No : c == "custom" ? Choice::Custom : Choice::None;
}

const char* choiceKey(Choice c) {
	switch (c) {
	case Choice::Yes: return "yes";
	case Choice::No: return "no";
	case Choice::Custom: return "custom";
	default: return "";
	}
}

bool crashInBuild() { return crashreports::inBuild(); }
bool crashPolicyOff() { return usage::policySet("DisableCrashReporting"); }
bool crashOffered() { return crashInBuild() && !crashPolicyOff(); }
bool usagePolicyOff() { return usagestats::policyOff(); }
bool usageOffered() { return !usagePolicyOff(); }
// (The question is asked for usage stats alone in a build without the crash
// reporter; with both turned off by policy there's nothing to ask.)
bool anythingToAsk() { return crashOffered() || usageOffered(); }
int crashAllowed() { return prefs().crashReports; }
bool usageOn() { return usagestats::on(); }

void record(Choice c, bool crash, bool usage) {
	// Crash reports: the answer is kept, unless a policy says no. Yes starts them now; No takes
	// effect the next time CedarLogic opens (CrashReports.h).
	if (!crashPolicyOff()) {
		prefs().crashReports = crash ? 1 : 0;
		crashreports::answerChanged();
	}
	if (usageOffered() && usageOn() != usage) usagestats::set(usage);
	prefs().privacyAsked = true;
	prefs().privacyChoice = choiceKey(c);
	prefs().save();
}

void answer(Choice c, bool crash, bool usage) {
	switch (c) {
	case Choice::Yes: record(Choice::Yes, true, true); break;
	case Choice::No: record(Choice::No, crashAllowed() == 1, false); break;
	default: record(Choice::Custom, crash, usage); break;
	}
}

bool owed() { return prefs().hasSeenWelcome && !asked() && anythingToAsk(); }

// ---- When to ask -----------------------------------------------------------------

namespace {

guint g_timer = 0;
double g_readySince = -1;

double nowSeconds() { return g_get_monotonic_time() / 1e6; }

gboolean tick(gpointer) {
	bool keep = true;
	guarded("the privacy question", [&] {
		if (asked() || !anythingToAsk() || sheetOpen()) { g_timer = 0; keep = false; return; }
		CircuitWindow* w = owed() && !showme::active() ? showme::readyWindow() : nullptr;
		if (w == nullptr) { g_readySince = -1; return; }
		const double now = nowSeconds();
		if (g_readySince < 0) { g_readySince = now; return; }
		if (now - g_readySince < 0.6) return;
		g_timer = 0;
		keep = false;
		presentSheet(w);
	});
	return keep ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

}  // namespace

void askWhenReady() {
	if (asked() || !anythingToAsk() || quiet || g_timer || sheetOpen()) return;
	g_readySince = -1;
	g_timer = g_timeout_add(300, tick, nullptr);
}

bool watching() { return g_timer != 0; }

void stopWatching() {
	if (g_timer) g_source_remove(g_timer);
	g_timer = 0;
}

bool holdsShowMeAround() { return sheetOpen() || (owed() && g_timer != 0); }

// ---- What's sent -------------------------------------------------------------------

namespace {

GtkWidget* g_sentWindow = nullptr;

GtkWidget* line(const std::string& text, bool heading = false) {
	GtkWidget* l = gtk_label_new(nullptr);
	if (heading) {
		gchar* m = g_markup_printf_escaped("<b>%s</b>", text.c_str());
		gtk_label_set_markup(GTK_LABEL(l), m);
		g_free(m);
	} else {
		gtk_label_set_text(GTK_LABEL(l), text.c_str());
	}
	gtk_label_set_xalign(GTK_LABEL(l), 0);
	gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
	gtk_label_set_line_wrap_mode(GTK_LABEL(l), PANGO_WRAP_WORD_CHAR);
	gtk_label_set_max_width_chars(GTK_LABEL(l), 20);
	gtk_label_set_selectable(GTK_LABEL(l), !heading);
	return l;
}

}  // namespace

void showWhatsSent(GtkWindow* owner) {
	if (g_sentWindow) {
		gtk_window_present(GTK_WINDOW(g_sentWindow));
		return;
	}
	GtkWidget* win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	g_sentWindow = win;
	gtk_widget_set_name(win, "whats-sent");
	gtk_window_set_title(GTK_WINDOW(win), "Exactly what's sent");
	gtk_window_set_icon_name(GTK_WINDOW(win), "cedarlogic");
	if (owner) {
		gtk_window_set_transient_for(GTK_WINDOW(win), owner);
		gtk_window_set_modal(GTK_WINDOW(win), TRUE);
		gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER_ON_PARENT);
	}
	gtk_window_set_type_hint(GTK_WINDOW(win), GDK_WINDOW_TYPE_HINT_DIALOG);
	// As big as it wants, or as the screen has room for (it scrolls).
	const GdkRectangle room = usableArea(owner ? GTK_WIDGET(owner) : nullptr);
	gtk_window_set_default_size(GTK_WINDOW(win), std::min(620, room.width), std::min(680, room.height));
	GtkWidget* outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(win), outer);
	GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start(GTK_BOX(outer), scroll, TRUE, TRUE, 0);
	GtkWidget* v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	for (auto set : { gtk_widget_set_margin_start, gtk_widget_set_margin_end, gtk_widget_set_margin_top, gtk_widget_set_margin_bottom }) set(v, 18);
	gtk_container_add(GTK_CONTAINER(scroll), v);
	gtk_box_pack_start(GTK_BOX(v), line("Usage stats", true), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(v), line(kUsageLine), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(v), line("The next usage report, exactly as it would go now:", true), FALSE, FALSE, 4);
	// The real report, pretty-printed, in a fixed-width face.
	GtkWidget* json = gtk_text_view_new();
	gtk_widget_set_name(json, "whats-sent-json");
	gtk_text_view_set_editable(GTK_TEXT_VIEW(json), FALSE);
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(json), TRUE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(json), GTK_WRAP_CHAR);
	gtk_text_view_set_left_margin(GTK_TEXT_VIEW(json), 8);
	gtk_text_view_set_top_margin(GTK_TEXT_VIEW(json), 6);
	gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(json), 6);
	const std::string report = usagestats::preview();
	gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(json)), report.c_str(), -1);
	GtkWidget* frame = gtk_frame_new(nullptr);
	gtk_container_add(GTK_CONTAINER(frame), json);
	gtk_box_pack_start(GTK_BOX(v), frame, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(v), line("Crash reports", true), FALSE, FALSE, 4);
	gtk_box_pack_start(GTK_BOX(v), line(crashInBuild() ? std::string(kCrashLine) : std::string(kCrashLine) + " (Not in this build.)"),
	                   FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(v), line("Never sent", true), FALSE, FALSE, 4);
	std::string never;
	for (const std::string& n : neverSent()) never += (never.empty() ? "" : "\n") + std::string("✕  ") + n;
	gtk_box_pack_start(GTK_BOX(v), line(never), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(v), line(kIdLine), FALSE, FALSE, 4);
	// Done, at the bottom (never scrolled away).
	GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	for (auto set : { gtk_widget_set_margin_start, gtk_widget_set_margin_end, gtk_widget_set_margin_top, gtk_widget_set_margin_bottom }) set(bar, 12);
	GtkWidget* done = gtk_button_new_with_label("Done");
	gtk_widget_set_name(done, "whats-sent-done");
	gtk_style_context_add_class(gtk_widget_get_style_context(done), "suggested-action");
	gtk_box_pack_end(GTK_BOX(bar), done, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(outer), bar, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(outer), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
	g_signal_connect_swapped(done, "clicked", G_CALLBACK(gtk_widget_destroy), win);
	g_signal_connect(win, "key-press-event", CL_CALLBACK(+[](GtkWidget* w, GdkEventKey* e, gpointer) -> gboolean {
		if (e->keyval != GDK_KEY_Escape) return FALSE;
		gtk_widget_destroy(w);
		return TRUE;
	}), nullptr);
	g_signal_connect(win, "destroy", CL_CALLBACK(+[](GtkWidget* w, gpointer) {
		if (g_sentWindow == w) g_sentWindow = nullptr;
	}), nullptr);
	gtk_widget_show_all(win);
	gtk_window_present(GTK_WINDOW(win));
	gtk_widget_grab_focus(done);
}

GtkWidget* whatsSentWindow() { return g_sentWindow; }

// ---- Settings > Privacy -----------------------------------------------------------------

std::string crashHint() {
	if (crashPolicyOff()) return "Turned off on this computer by its administrator (" + usage::policyFile() + ": DisableCrashReporting).";
	if (!crashInBuild()) return "Not in this build.";
	return std::string(kCrashLine) + " Off takes effect the next time CedarLogic opens.";
}

std::string usageHint() {
	if (usagePolicyOff()) return "Turned off on this computer by its administrator (" + usage::policyFile() + ": DisableUsageStats).";
	return kUsageLine;
}

std::string lastSentLine() {
	const std::string last = usagestats::lastSent();
	std::string s = !last.empty() ? last : usageOn() ? "Nothing yet" : "Not sending";
	const int kept = usagestats::kept();
	if (kept > 0) s += format(" · %d report%s waiting to go (no connection last time)", kept, kept == 1 ? "" : "s");
	return s;
}

}  // namespace privacy
