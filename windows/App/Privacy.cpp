// One consent for crash reports and usage stats (see Privacy.h).

#include "Privacy.h"
#include "CrashReports.h"
#include "Dialogs.h"
#include "Intro.h"
#include "ShowMeAround.h"
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
	"If CedarLogic crashes: where in the app it went wrong (the code's stack), the Windows and CedarLogic versions. Paths in it lose your "
	"user name. Sent through Sentry, only after a crash.";
const char* const kUsageLine =
	"At most once a day: how often CedarLogic is opened and for how long, the time spent in focus mode and Simulation View, whether sync "
	"is on, how many times each command is used, and which settings you've changed from their defaults. Only what's new since the last "
	"report goes; when nothing is, a short \"still here\".";
const char* const kIdLine =
	"Each report carries a random id, made when you turn this on (nothing about your PC). Turning it off, or Delete What's Been Sent, "
	"deletes everything sent under it.";

const std::vector<std::string>& neverSent() {
	static const std::vector<std::string> v = {
		"Your name, or the name on your exports",
		"Your circuits: their names, contents, notes or drawings",
		"File names, folders or anything you type",
		"Your sync code, classroom codes, class names or students",
		"Your PC's name, your user name, its serial number or IP address (the server keeps no IP)",
		"Your time zone or exact times (dates, and the minute in UTC)",
	};
	return v;
}

// ---- The answer ------------------------------------------------------------------

bool quiet = false;
bool assumeFront = false;

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

bool crashOffered() { return crashreports::available(); }
bool crashPolicyOff() { return crashreports::policyOff(); }
bool usagePolicyOff() { return usagestats::policyOff(); }
bool usageOffered() { return !usagePolicyOff(); }
bool anythingToAsk() { return crashOffered() || usageOffered(); }
int crashAllowed() { return crashreports::allowed(); }
bool usageOn() { return usagestats::on(); }

void record(Choice c, bool crash, bool usage) {
	if (crashOffered() && (crashAllowed() < 0 || (crashAllowed() == 1) != crash)) crashreports::set(crash);
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

UINT_PTR g_timer = 0;
double g_readySince = -1;

CircuitWindow* frontWindow() {
	if (assumeFront) return circuitWindows().empty() ? nullptr : circuitWindows().back();
	const HWND fg = GetForegroundWindow();
	for (CircuitWindow* w : circuitWindows())
		if (w->window() == fg) return w;
	return nullptr;
}

// A circuit window in front and taking input, nothing over it.
CircuitWindow* readyWindow() {
	if (splash::active() || intro::active() || welcome::isOpen() || whatsnew::isOpen() || welcome::tourWindow() || showme::active() ||
	    sheetOpen())
		return nullptr;
	CircuitWindow* w = frontWindow();
	if (w == nullptr) return nullptr;
	const HWND h = w->window();
	if (!IsWindowVisible(h) || IsIconic(h) || !IsWindowEnabled(h)) return nullptr;
	return w;
}

void CALLBACK tick(HWND, UINT, UINT_PTR, DWORD) {
	guarded("the privacy question", [] {
		if (asked() || !anythingToAsk()) { stopWatching(); return; }
		CircuitWindow* w = owed() ? readyWindow() : nullptr;
		if (w == nullptr) { g_readySince = -1; return; }
		const double now = nowSeconds();
		if (g_readySince < 0) { g_readySince = now; return; }
		if (now - g_readySince < 0.6) return;
		stopWatching();
		presentSheet(w);
	});
}

}  // namespace

void askWhenReady() {
	if (asked() || !anythingToAsk() || quiet || g_timer || sheetOpen()) return;
	g_readySince = -1;
	g_timer = SetTimer(nullptr, 0, 300, tick);
}

bool watching() { return g_timer != 0; }

void stopWatching() {
	if (g_timer) KillTimer(nullptr, g_timer);
	g_timer = 0;
}

bool holdsShowMeAround() { return sheetOpen() || (owed() && g_timer != 0); }

// ---- What's sent -------------------------------------------------------------------

namespace {

HWND g_sentWindow = nullptr;

// Lines a note needs at the dialog's width (about 88 characters a line).
int linesFor(const std::string& s, int perLine = 88) {
	int lines = 0, at = 0;
	for (char c : s) {
		if (c == '\n') { lines++; at = 0; continue; }
		if (++at >= perLine) { lines++; at = 0; }
	}
	return lines + 1;
}

FormField note(const std::string& text, bool heading = false) {
	FormField x;
	x.kind = FormField::Note;
	x.label = text;
	x.heading = heading;
	x.lines = heading ? 1 : linesFor(text);
	return x;
}

}  // namespace

void showWhatsSent(HWND owner) {
	std::vector<std::vector<std::string>> rows;
	{
		const std::string json = usagestats::preview();
		size_t at = 0;
		while (at <= json.size()) {
			size_t end = json.find('\n', at);
			if (end == std::string::npos) end = json.size();
			rows.push_back({ json.substr(at, end - at) });
			at = end + 1;
		}
	}
	std::string never;
	for (const std::string& line : neverSent()) never += (never.empty() ? "" : "\n") + std::string("✕  ") + line;
	Form f;
	f.title = "Exactly what's sent";
	f.width = 600;
	f.okText = "Done";
	f.cancelText = "";
	f.add(note(kUsageLine));
	f.add(note("The next usage report, as it would go now:", true));
	FormField list;
	list.kind = FormField::List;
	list.mono = true;
	list.lines = 12;
	const int listField = f.add(list);
	f.add(note("Crash reports", true));
	f.add(note(crashreports::inBuild() ? std::string(kCrashLine) : std::string(kCrashLine) + " (Not in this build.)"));
	f.add(note("Never sent", true));
	FormField n = note(never);
	n.lines = (int)neverSent().size() + 1;
	f.add(n);
	f.add(note(kIdLine));
	f.onInit = [&](Form& form) {
		g_sentWindow = form.dialog;
		form.setRows(listField, rows);
	};
	f.run(owner);
	g_sentWindow = nullptr;
}

HWND whatsSentWindow() { return g_sentWindow; }

// ---- Settings > Privacy -----------------------------------------------------------------

std::string crashHint() {
	if (crashPolicyOff()) return "Turned off on this PC by its administrator (DisableCrashReporting).";
	if (!crashreports::inBuild()) return "Not in this build.";
	return std::string(kCrashLine) + " Off takes effect the next time CedarLogic opens.";
}

std::string usageHint() {
	if (usagePolicyOff()) return "Turned off on this PC by its administrator (DisableUsageStats).";
	return kUsageLine;
}

std::string lastSentLine() {
	const std::string last = usagestats::lastSent();
	std::string s = !last.empty() ? last : usageOn() ? "Nothing yet" : "Not sending";
	const int kept = usagestats::kept();
	if (kept > 0) s += strf(" · %d report%s waiting to go (no connection last time)", kept, kept == 1 ? "" : "s");
	return s;
}

}  // namespace privacy
