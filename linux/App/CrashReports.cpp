// Crash reports (see CrashReports.h).

#include "CrashReports.h"
#include "App.h"
#include "Sync.h"
#include "SyncPlatform.h"
#include "UsageReport.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>
#include <vector>

#ifdef CL_SENTRY
#include <sentry.h>
#endif

#ifndef CL_VERSION
#define CL_VERSION "0.4.2"
#endif
#ifndef CL_BUILD_NUMBER
#define CL_BUILD_NUMBER 0
#endif

namespace crashreports {

namespace {

bool g_started = false;
bool g_testRun = false;   // as the launch said (start)

std::string lower(std::string s) {
	for (char& c : s) c = (char)g_ascii_tolower(c);
	return s;
}

std::string envVar(const char* name) {
	const char* v = g_getenv(name);
	return v ? v : "";
}

// The address: CL_SENTRY_DSN for a check, only if it's this computer (a stand-in
// for Sentry: linux/Tools/sentry-mock.py); else the build's.
std::string dsn() {
	const std::string test = envVar("CL_SENTRY_DSN");
	if (!test.empty()) {
		const std::string l = lower(test);
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

// Where Sentry keeps a report until it goes: ~/.local/share/CedarLogic/crash-reports.
std::string databaseDir() {
	gchar* dir = g_build_filename(g_get_user_data_dir(), "CedarLogic", "crash-reports", nullptr);
	g_mkdir_with_parents(dir, 0700);
	std::string s = dir;
	g_free(dir);
	return s;
}

bool stopsName(const std::string& t, size_t i, const std::string& sep) {
	const char c = t[i];
	return c == '"' || c == '\n' || c == ' ' || c == '\t' || c == ':' || c == ';' || c == ',' || c == ')' || c == '\'' || c == '\\' ||
	       t.compare(i, sep.size(), sep) == 0;
}

// <pattern><name> -> <pattern><user>, for one way of writing the separator.
std::string userSegments(const std::string& text, const std::string& pattern, const std::string& sep) {
	const std::string low = lower(text);
	std::string out;
	size_t from = 0, at;
	while ((at = low.find(pattern, from)) != std::string::npos) {
		const size_t start = at + pattern.size();
		size_t end = start;
		while (end < text.size() && !stopsName(text, end, sep)) end++;
		out += text.substr(from, start - from);
		if (end > start) out += "<user>";
		from = end;
	}
	return out + text.substr(from);
}

bool wordChar(char c) { return g_ascii_isalnum(c) || c == '_' || (unsigned char)c >= 0x80; }

std::string replaceAllOf(const std::string& text, const std::string& what, const std::string& with) {
	if (what.empty()) return text;
	std::string out;
	size_t from = 0, at;
	while ((at = text.find(what, from)) != std::string::npos) {
		out += text.substr(from, at - from) + with;
		from = at + what.size();
	}
	return out + text.substr(from);
}

// Every place `name` (any case) stands on its own -> `with`.
std::string replaceName(const std::string& text, const std::string& name, const std::string& with) {
	if (name.size() < 3 || name == "user") return text;
	const std::string low = lower(text), n = lower(name);
	std::string out;
	size_t from = 0, at = 0;
	while ((at = low.find(n, at)) != std::string::npos) {
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

// This user's and this computer's names, longest first (names), and the
// home folder as written (paths).
void needles(std::vector<std::string>& names, std::vector<std::string>& paths) {
	for (const std::string& s : usage::machineNeedles()) {
		if (s.find('/') != std::string::npos) {
			if (s.size() > 2) paths.push_back(s);
		} else {
			names.push_back(s);
		}
	}
	auto longer = [](const std::string& a, const std::string& b) { return a.size() > b.size(); };
	std::sort(names.begin(), names.end(), longer);
	names.erase(std::unique(names.begin(), names.end()), names.end());
	std::sort(paths.begin(), paths.end(), longer);
}

// What's left, checked once more: names this computer has that are still in the text.
bool leaksLeft(const std::string& text) {
	return !usage::leaks(replaceAllOf(text, "<user>", ""), usage::machineNeedles()).empty();
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
	if (leaksLeft(text)) return sentry_value_new_null();
	return sentry_value_from_json(text.c_str(), text.size());
}

// ---- The transport ---------------------------------------------------------------------------
// sentry-native hands a finished envelope to this at the next launch (the crash itself only writes
// it down). It's cleaned and checked once more, then sent by the curl program, on a thread of its
// own, so a slow network never holds the app up. (No libcurl: nothing extra to install.)

std::atomic<int> g_sending{ 0 };

// https://<key>@<host>/<project> -> the envelope address and the auth header.
bool endpoint(const std::string& d, std::string& url, std::string& auth) {
	const size_t scheme = d.find("://");
	const size_t at = d.find('@');
	const size_t slash = at == std::string::npos ? std::string::npos : d.find('/', at);
	if (scheme == std::string::npos || at == std::string::npos || slash == std::string::npos) return false;
	const std::string key = d.substr(scheme + 3, at - scheme - 3), project = d.substr(slash + 1);
	if (key.empty() || project.empty() || key.find(':') != std::string::npos) return false;
	url = d.substr(0, scheme + 3) + d.substr(at + 1, slash - at - 1) + "/api/" + project + "/envelope/";
	auth = "Sentry sentry_version=7, sentry_client=cedarlogic-native/" CL_VERSION ", sentry_key=" + key;
	return true;
}

void sendEnvelope(sentry_envelope_t* envelope, void*) {
	size_t size = 0;
	char* raw = sentry_envelope_serialize(envelope, &size);
	sentry_envelope_free(envelope);
	if (raw == nullptr) return;
	std::string body = scrubbed(std::string(raw, size));
	sentry_free(raw);
	if (leaksLeft(body)) return;   // dropped rather than sent
	std::string url, auth;
	if (!endpoint(dsn(), url, auth)) return;
	g_sending++;
	std::thread([url, auth, body] {
		clsync::HttpRequest r;
		r.method = "POST";
		r.url = url;
		r.headers = { { "content-type", "application/x-sentry-envelope" }, { "x-sentry-auth", auth } };
		r.body = body;
		syncplatform::http(r, 15);
		g_sending--;
	}).detach();
}

// Waits (a couple of seconds at most) for what's on its way.
int finishSending(uint64_t timeoutMs, void*) {
	const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs < 3000 ? timeoutMs : 3000);
	while (g_sending > 0 && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(20));
	return 0;
}
#endif

}  // namespace

std::string scrubbed(const std::string& text) {
	std::string s = text;
	for (const char* where : { "/home/", "/users/" }) {
		s = userSegments(s, where, "/");
		s = userSegments(s, replaceAllOf(where, "/", "\\/"), "\\/");   // as JSON writes it
	}
	std::vector<std::string> names, paths;
	needles(names, paths);
	for (const std::string& p : paths) s = replaceAllOf(s, p, "~");
	for (const std::string& n : names) s = replaceName(s, n, "<user>");
	return s;
}

bool inBuild() {
#ifdef CL_SENTRY
	return !dsn().empty();
#else
	return false;
#endif
}

bool policyOff() { return usage::policySet("DisableCrashReporting"); }
bool available() { return inBuild() && !policyOff(); }
int allowed() { return prefs().crashReports; }
bool started() { return g_started; }

void start(bool testRun) {
	g_testRun = testRun;
	if (g_started || testRun || !available() || allowed() != 1) return;
#ifdef CL_SENTRY
	sentry_options_t* o = sentry_options_new();
	sentry_options_set_dsn(o, dsn().c_str());
	sentry_options_set_release(o, format("cedarlogic-native@%s+%lld", CL_VERSION, (long long)CL_BUILD_NUMBER).c_str());
	sentry_options_set_environment(o, prefs().updateChannel == 1 ? "beta" : "normal");
	sentry_options_set_database_path(o, databaseDir().c_str());
	// Crashes only: no sessions, no trail of what led up to it.
	sentry_options_set_auto_session_tracking(o, 0);
	sentry_options_set_max_breadcrumbs(o, 0);
	sentry_options_set_sample_rate(o, 1.0);
	sentry_options_set_before_send(o, beforeSend, nullptr);
	sentry_transport_t* t = sentry_transport_new(sendEnvelope);
	sentry_transport_set_flush_func(t, finishSending);
	sentry_transport_set_shutdown_func(t, finishSending);
	sentry_options_set_transport(o, t);
	if (sentry_init(o) != 0) return;
	sentry_set_tag("platform", "linux");
	g_started = true;
	// Development and the check: CL_TEST_CRASH crashes the app a few seconds
	// after it starts, on a thread of its own (so nothing catches it), and
	// the report goes the next time it opens.
	if (!envVar("CL_TEST_CRASH").empty()) {
		std::thread([] {
			std::this_thread::sleep_for(std::chrono::seconds(4));
			volatile int* nothing = nullptr;
			*nothing = 1;
		}).detach();
	}
#endif
}

void answerChanged() {
	if (allowed() == 1) start(g_testRun);
}

void shutdown() {
#ifdef CL_SENTRY
	if (g_started) sentry_close();
	g_started = false;
#endif
}

int selfTest() {
	int failed = 0, passed = 0;
	auto check = [&](bool ok, const std::string& what) {
		(ok ? passed : failed)++;
		printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
	};
	const std::vector<std::string> machine = usage::machineNeedles();
	std::string home = g_get_home_dir() ? g_get_home_dir() : "/home/someone";
	const std::string user = g_get_user_name() ? g_get_user_name() : "someone", pc = g_get_host_name() ? g_get_host_name() : "a-computer";
	auto jsonPath = [](const std::string& p) { return replaceAllOf(p, "/", "\\/"); };
	// A made-up report with this computer's paths and names everywhere a real one could have them.
	const std::string exe = home + "/.local/bin/cedarlogic";
	const std::string report =
		"{\"debug_meta\":{\"images\":[{\"code_file\":\"" + exe + "\",\"debug_file\":\"" + home + "/src/cedarlogic.debug\"},"
		"{\"code_file\":\"/usr/lib/x86_64-linux-gnu/libgtk-3.so.0\"},{\"code_file\":\"" + jsonPath(home + "/.cache/thing/lib.so") + "\"}]},"
		"\"exception\":{\"values\":[{\"type\":\"SIGSEGV\",\"value\":\"Fatal Error\",\"stacktrace\":{\"frames\":[{"
		"\"package\":\"" + exe + "\",\"function\":\"main\",\"instruction_addr\":\"0x55e612341234\"}]}}]},"
		"\"message\":{\"formatted\":\"opened /Users/" + user + "/Documents and " + home + "/Documents on " + pc + "\"},"
		"\"contexts\":{\"os\":{\"name\":\"Linux\",\"version\":\"6.1.0\"}}}";
	const std::string clean = scrubbed(report);
	const std::vector<std::string> found = usage::leaks(replaceAllOf(clean, "<user>", ""), machine);
	std::string list;
	for (const std::string& f : found) list += f + "; ";
	check(found.empty(), "the cleaning takes out this computer's home folder, user name and host name (" + list + ")");
	check(!usage::leaks(report, machine).empty(), "...which the made-up report did have");
	check(clean.find("/home/<user>/.local/bin/cedarlogic") != std::string::npos || clean.find("~/.local/bin/cedarlogic") != std::string::npos,
	      "a module's path keeps its shape: /home/<user>/.local/bin/cedarlogic");
	check(clean.find("/usr/lib/x86_64-linux-gnu/libgtk-3.so.0") != std::string::npos && clean.find("\"function\":\"main\"") != std::string::npos &&
	          clean.find("0x55e612341234") != std::string::npos,
	      "the system's own paths, the functions and the addresses stay");
	check(clean.find("/Users/<user>/Documents") != std::string::npos, "a /Users/<name>/ path loses its name too");
	check(scrubbed("{\"f\":\"\\/home\\/alexs\\/x.so\"}") == "{\"f\":\"\\/home\\/<user>\\/x.so\"}", "a path as JSON escapes it (\\/home\\/name\\/) loses its name");
	check(scrubbed("/home/alexs/x.so and /home/alexs") == "/home/<user>/x.so and /home/<user>", "...and so does one at the end of a sentence");
	check(policyOff() == usage::policySet("DisableCrashReporting"),
	      std::string("the policy DisableCrashReporting is read from ") + usage::policyFile() + ": " + (policyOff() ? "set" : "not set"));
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
		g_setenv("CL_SENTRY_DSN", "http://0123456789abcdef0123456789abcdef@127.0.0.1:9/1", TRUE);
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
		check(!sent.empty() && !leaksLeft(sent) && sent.find("server_name") == std::string::npos,
		      "Sentry's before_send: the event goes out clean, without server_name");
		std::string url, auth;
		check(endpoint("https://36b411e83b623ea3bc3a7bc6fda107a0@o4512167663239168.ingest.us.sentry.io/4512167685849088", url, auth) &&
		          url == "https://o4512167663239168.ingest.us.sentry.io/api/4512167685849088/envelope/" && auth.find("sentry_key=36b411e83b623ea3bc3a7bc6fda107a0") != std::string::npos,
		      "the address Sentry's envelopes go to is made from the build's");
		shutdown();
		check(!started(), "...and stops");
		g_unsetenv("CL_SENTRY_DSN");
	}
#else
	check(!inBuild(), "this build has no sentry-native: crash reports say \"Not in this build\"");
#endif
	prefs().crashReports = was;
	printf("crash check: %d passed, %d failed%s\n", passed, failed, failed == 0 ? " -- all passed" : "");
	return failed == 0 && passed > 0 ? 0 : 1;
}

}  // namespace crashreports
