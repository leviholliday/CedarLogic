// Crash reports (see CrashReports.h).

#include "CrashReports.h"
#include "App.h"
#include "UsageReport.h"

#include <shlobj.h>

#include <algorithm>
#include <cctype>
#include <thread>
#include <vector>

#ifdef CL_SENTRY
#include <sentry.h>
#endif

#ifndef CL_VERSION
#define CL_VERSION "0.4.0"
#endif
#ifndef CL_BUILD_NUMBER
#define CL_BUILD_NUMBER 0
#endif

namespace crashreports {

namespace {

bool g_started = false;
bool g_testRun = false;   // as the launch said (start)

std::string envVar(const wchar_t* name) {
	wchar_t buf[2048];
	const DWORD n = GetEnvironmentVariableW(name, buf, 2048);
	return n > 0 && n < 2048 ? U(std::wstring(buf, n)) : std::string();
}

// The address: CL_SENTRY_DSN for a check, only if it's this PC (a stand-in
// for Sentry: windows/Tools/sentry-mock.py); else the build's.
std::string dsn() {
	const std::string test = envVar(L"CL_SENTRY_DSN");
	if (!test.empty()) {
		const std::string l = lowerCase(test);
		const size_t at = l.find('@');
		const std::string host = at == std::string::npos ? std::string() : l.substr(at + 1);
		if (l.compare(0, 7, "http://") == 0 && (host.compare(0, 10, "127.0.0.1:") == 0 || host.compare(0, 10, "localhost:") == 0)) return test;
		return std::string();
	}
#ifdef CL_SENTRY_DSN
	return CL_SENTRY_DSN;
#else
	return std::string();
#endif
}

// Where Sentry keeps a report until it goes: %LOCALAPPDATA%\CedarLogic\Crash
// reports (CEDARLOGIC_HOME's, in a check).
std::wstring databaseDir() {
	const std::string home = envVar(L"CEDARLOGIC_HOME");
	if (!home.empty()) return W(home + "\\Crash reports");
	PWSTR p = nullptr;
	std::wstring dir;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p)) && p) dir = std::wstring(p) + L"\\CedarLogic";
	if (p) CoTaskMemFree(p);
	if (dir.empty()) return W(settingsDir() + "\\Crash reports");
	CreateDirectoryW(dir.c_str(), nullptr);
	return dir + L"\\Crash reports";
}

// Case-insensitive search from `at`.
size_t findNoCase(const std::string& lower, const std::string& needleLower, size_t at) { return lower.find(needleLower, at); }

// <sep>Users<sep><name> -> <sep>Users<sep><user>, for one way of writing the separator.
std::string userSegments(const std::string& text, const std::string& sep) {
	const std::string pattern = sep + "users" + sep;
	std::string lower = lowerCase(text), out;
	size_t from = 0, at;
	while ((at = findNoCase(lower, pattern, from)) != std::string::npos) {
		const size_t start = at + pattern.size();
		size_t end = start;
		while (end < text.size() && text[end] != '"' && text.compare(end, sep.size(), sep) != 0 && text[end] != '\n') end++;
		out += text.substr(from, start - from);
		if (end > start) out += "<user>";
		from = end;
	}
	return out + text.substr(from);
}

bool wordChar(char c) { return isalnum((unsigned char)c) || c == '_' || (unsigned char)c >= 0x80; }

// Every place `name` (any case) stands on its own -> `with`.
std::string replaceName(const std::string& text, const std::string& name, const std::string& with) {
	if (name.size() < 3) return text;
	const std::string lower = lowerCase(text), n = lowerCase(name);
	std::string out;
	size_t from = 0, at = 0;
	while ((at = lower.find(n, at)) != std::string::npos) {
		const bool before = at == 0 || !wordChar(text[at - 1]);
		const bool after = at + n.size() >= text.size() || !wordChar(text[at + n.size()]);
		if (before && after) {
			out += text.substr(from, at - from) + with;
			from = at + n.size();
		}
		at += n.size();
	}
	return out + text.substr(from);
}

// This user's and this PC's names (not the paths: those go by userSegments).
std::vector<std::string> names() {
	std::vector<std::string> n;
	for (const std::string& s : usage::machineNeedles())
		if (s.find_first_of("\\/:") == std::string::npos) n.push_back(s);
	std::sort(n.begin(), n.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
	n.erase(std::unique(n.begin(), n.end()), n.end());
	return n;
}

#ifdef CL_SENTRY
// Sentry's before_send: called with the finished report (the crash's stack,
// the modules, the scope) on the way out. Anything that could be about the
// person goes; what's left is cleaned, read back, and checked once more: a
// report that still has a name in it is dropped rather than sent.
sentry_value_t beforeSend(sentry_value_t event, sentry_hint_t*, void*) {
	for (const char* k : { "user", "server_name", "request", "breadcrumbs", "extra", "modules" }) sentry_value_remove_by_key(event, k);
	char* raw = sentry_value_to_json(event);
	sentry_value_decref(event);
	if (raw == nullptr) return sentry_value_new_null();
	const std::string text = scrubbed(raw);
	sentry_free(raw);
	std::vector<std::string> needles = usage::machineNeedles();
	if (!usage::leaks(text, needles).empty()) return sentry_value_new_null();
	sentry_value_t clean = sentry_value_from_json(text.c_str(), text.size());
	return clean;
}
#endif

}  // namespace

std::string scrubbed(const std::string& text) {
	std::string s = userSegments(text, "\\\\");   // as JSON writes C:\Users\name
	s = userSegments(s, "\\");
	s = userSegments(s, "/");
	for (const std::string& n : names()) s = replaceName(s, n, "<user>");
	return s;
}

bool inBuild() {
#ifdef CL_SENTRY
	return !dsn().empty();
#else
	return false;
#endif
}

bool policyOff() { return usage::policySet(L"DisableCrashReporting"); }
bool available() { return inBuild() && !policyOff(); }
int allowed() { return prefs().crashReports; }
bool started() { return g_started; }

void start(bool testRun) {
	g_testRun = testRun;
	if (g_started || testRun || !available() || allowed() != 1) return;
#ifdef CL_SENTRY
	sentry_options_t* o = sentry_options_new();
	sentry_options_set_dsn(o, dsn().c_str());
	sentry_options_set_release(o, strf("cedarlogic-native@%s+%lld", CL_VERSION, (long long)CL_BUILD_NUMBER).c_str());
	sentry_options_set_environment(o, prefs().betaTester ? "beta" : "normal");
	sentry_options_set_database_pathw(o, databaseDir().c_str());
	// Crashes only: no sessions, no trail of what led up to it.
	sentry_options_set_auto_session_tracking(o, 0);
	sentry_options_set_max_breadcrumbs(o, 0);
	sentry_options_set_sample_rate(o, 1.0);
	sentry_options_set_before_send(o, beforeSend, nullptr);
	if (sentry_init(o) != 0) return;
	sentry_set_tag("platform", "windows");
	g_started = true;
	// Development and the check: CL_TEST_CRASH crashes the app a few seconds
	// after it starts, on a thread of its own (so nothing catches it), and
	// the report goes the next time it opens.
	if (!envVar(L"CL_TEST_CRASH").empty()) {
		// (No "stopped working" box after it: the check waits for no one.)
		SetErrorMode(GetErrorMode() | SEM_NOGPFAULTERRORBOX);
		std::thread([] {
			Sleep(4000);
			volatile int* nothing = nullptr;
			*nothing = 1;
		}).detach();
	}
#endif
}

void set(bool on) {
	prefs().crashReports = on ? 1 : 0;
	prefs().save();
	if (on) start(g_testRun);
}

void shutdown() {
#ifdef CL_SENTRY
	if (g_started) sentry_close();
	g_started = false;
#endif
}

int selfTest(const std::function<void(const std::string&)>& out) {
	int failed = 0, passed = 0;
	auto check = [&](bool ok, const std::string& what) {
		(ok ? passed : failed)++;
		out(std::string(ok ? "PASS " : "FAIL ") + what + "\n");
	};
	const std::vector<std::string> needles = usage::machineNeedles();
	std::string profile = envVar(L"USERPROFILE");
	if (profile.empty()) profile = "C:\\Users\\Someone";
	std::string user = envVar(L"USERNAME"), pc = envVar(L"COMPUTERNAME");
	auto jsonPath = [](std::string p) {
		std::string o;
		for (char c : p) o += c == '\\' ? std::string("\\\\") : std::string(1, c);
		return o;
	};
	// A made-up report with this PC's paths and names everywhere a real one could have them.
	const std::string exe = profile + "\\AppData\\Local\\Programs\\CedarLogic\\CedarLogic.exe";
	const std::string report =
		"{\"debug_meta\":{\"images\":[{\"code_file\":\"" + jsonPath(exe) + "\",\"debug_file\":\"" + jsonPath(profile + "\\src\\CedarLogic.pdb") +
		"\"},{\"code_file\":\"C:\\\\WINDOWS\\\\System32\\\\USER32.dll\"},{\"code_file\":\"" +
		jsonPath(profile + "\\AppData\\Local\\Microsoft\\OneDrive\\FileSyncShell64.dll") +
		"\"}]},\"exception\":{\"values\":[{\"type\":\"EXCEPTION_ACCESS_VIOLATION_WRITE\",\"value\":\"Fatal Error\",\"stacktrace\":{\"frames\":[{"
		"\"package\":\"" + jsonPath(exe) + "\",\"function\":\"wWinMain\",\"instruction_addr\":\"0x7ff612341234\"}]}}]},"
		"\"message\":{\"formatted\":\"opened /Users/" + user + "/Documents and " + jsonPath(profile + "\\Documents") + " on " + pc + "\"},"
		"\"contexts\":{\"os\":{\"name\":\"Windows\",\"version\":\"10.0.26100\"}}}";
	const std::string clean = scrubbed(report);
	const std::vector<std::string> found = usage::leaks(clean, needles);
	std::string list;
	for (const std::string& f : found) list += f + "; ";
	check(found.empty(), "the cleaning takes out this PC's profile folder, user name and computer name (" + list + ")");
	check(!usage::leaks(report, needles).empty(), "...which the made-up report did have");
	check(clean.find("C:\\\\Users\\\\<user>\\\\AppData\\\\Local\\\\Programs\\\\CedarLogic\\\\CedarLogic.exe") != std::string::npos,
	      "a module's path keeps its shape: C:\\Users\\<user>\\AppData\\...\\CedarLogic.exe");
	check(clean.find("C:\\\\WINDOWS\\\\System32\\\\USER32.dll") != std::string::npos && clean.find("wWinMain") != std::string::npos &&
	          clean.find("0x7ff612341234") != std::string::npos,
	      "Windows' own paths, the functions and the addresses stay");
	check(clean.find("/Users/<user>/Documents") != std::string::npos, "a /Users/<name>/ path loses its name too");
	check(scrubbed("C:\\Users\\Alex Student\\x.dll") == "C:\\Users\\<user>\\x.dll", "a name with a space in it goes whole");
	check(policyOff() == usage::policySet(L"DisableCrashReporting"),
	      std::string("the policy DisableCrashReporting is read from HKLM\\Software\\Policies\\CedarLogic: ") + (policyOff() ? "set" : "not set"));
	// A test run never starts it, whatever was chosen.
	const int was = prefs().crashReports;
	prefs().crashReports = 1;
	start(true);
	check(!started(), "a test or picture run never starts crash reports");
	prefs().crashReports = 0;
	start(false);
	check(!started(), "No: crash reports don't start");
#ifdef CL_SENTRY
	check(inBuild(), "this build has sentry-native and an address (" + std::string(available() ? "available" : "turned off by policy") + ")");
	// Started for real against a stand-in that isn't there (127.0.0.1:9): it
	// starts, stops, and sends nothing anywhere else.
	if (available()) {
		SetEnvironmentVariableW(L"CL_SENTRY_DSN", L"http://0123456789abcdef0123456789abcdef@127.0.0.1:9/1");
		prefs().crashReports = 1;
		start(false);
		check(started(), "Yes: Sentry starts (in-process, against a stand-in)");
		// before_send on a made-up event: nothing personal survives the way out.
		sentry_value_t e = sentry_value_from_json(report.c_str(), report.size());
		sentry_value_set_by_key(e, "server_name", sentry_value_new_string(pc.c_str()));
		sentry_value_t out2 = beforeSend(e, nullptr, nullptr);
		char* j = sentry_value_is_null(out2) ? nullptr : sentry_value_to_json(out2);
		const std::string sent = j ? j : "";
		if (j) sentry_free(j);
		sentry_value_decref(out2);
		check(!sent.empty() && usage::leaks(sent, needles).empty() && sent.find("server_name") == std::string::npos,
		      "Sentry's before_send: the event goes out clean, without server_name");
		shutdown();
		check(!started(), "...and stops");
		SetEnvironmentVariableW(L"CL_SENTRY_DSN", nullptr);
	}
#else
	check(!inBuild(), "this build has no sentry-native: crash reports say \"Not in this build\"");
#endif
	prefs().crashReports = was;
	out(strf("crash check: %d passed, %d failed%s\n", passed, failed, failed == 0 ? " -- all passed" : ""));
	return failed == 0 && passed > 0 ? 0 : 1;
}

}  // namespace crashreports
