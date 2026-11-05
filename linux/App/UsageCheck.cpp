// The usage stats engine's checks (see PrivacyCheck.h): --usage-check and
// --usage-live, with no window.

#include "PrivacyCheck.h"
#include "App.h"
#include "SyncJson.h"
#include "SyncPlatform.h"
#include "UsageReport.h"

#include <glib/gstdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

namespace privacycheck {

namespace json = clsync::json;

namespace {

int gFailed = 0, gPassed = 0;
std::string gBase, gHome;

void check(bool ok, const std::string& what) {
	printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
	fflush(stdout);
	(ok ? gPassed : gFailed)++;
}

int finish(const char* name) {
	printf("%s: %d passed, %d failed\n", name, gPassed, gFailed);
	return gFailed ? 1 : 0;
}

std::string envVar(const char* name) {
	const char* v = g_getenv(name);
	return v ? std::string(v) : std::string();
}

std::string configDir() {
	gchar* d = g_build_filename(g_get_user_config_dir(), "CedarLogic", nullptr);
	std::string s = d;
	g_free(d);
	g_mkdir_with_parents(s.c_str(), 0700);
	return s;
}

void removeTree(const std::string& dir) {
	if (dir.size() < 8) return;
	if (GDir* d = g_dir_open(dir.c_str(), 0, nullptr)) {
		while (const gchar* n = g_dir_read_name(d)) g_remove((dir + "/" + n).c_str());
		g_dir_close(d);
	}
	g_rmdir(dir.c_str());
}

std::string readFile(const std::string& path) {
	gchar* t = nullptr;
	gsize n = 0;
	std::string s;
	if (g_file_get_contents(path.c_str(), &t, &n, nullptr)) s.assign(t, n);
	g_free(t);
	return s;
}

bool modeIs(const std::string& path, unsigned mode) {
	struct stat st;
	return stat(path.c_str(), &st) == 0 && (st.st_mode & 0777) == mode && st.st_uid == getuid();
}

std::string join(const std::set<std::string>& s) {
	std::string out = "(";
	for (const std::string& x : s) out += (out.size() > 1 ? ", " : "") + x;
	return out + ")";
}

std::set<std::string> keysOf(const json::Value* o) {
	std::set<std::string> k;
	if (o && o->isObject())
		for (const auto& kv : o->o) k.insert(kv.first);
	return k;
}

json::Value get(const std::string& path) {
	clsync::HttpRequest r;
	r.method = "GET";
	r.url = gBase + path;
	json::Value v;
	json::parse(syncplatform::http(r, 20).body, v);
	return v;
}

void control(const std::string& body) {
	clsync::HttpRequest r;
	r.method = "POST";
	r.url = gBase + "/control";
	r.headers.push_back({ "Content-Type", "application/json" });
	r.body = body;
	syncplatform::http(r, 20);
}

std::vector<json::Value> requests() {
	json::Value v = get("/log");
	return v.isArray() ? v.a : std::vector<json::Value>();
}

std::vector<json::Value> posts() {
	std::vector<json::Value> out;
	for (const json::Value& e : requests())
		if (e.str("method") == "POST") out.push_back(e);
	return out;
}

json::Value lastBody() {
	const std::vector<json::Value> p = posts();
	if (p.empty()) return json::Value::object();
	const json::Value* b = p.back().get("body");
	return b ? *b : json::Value::object();
}

bool noProblems(const json::Value& entry) {
	const json::Value* p = entry.get("problems");
	return p && p->isArray() && p->a.empty();
}

std::string problemsOf(const json::Value& entry) {
	const json::Value* p = entry.get("problems");
	std::string s;
	if (p && p->isArray())
		for (const json::Value& x : p->a) s += (s.empty() ? "" : "; ") + x.s;
	return s.empty() ? "-" : s;
}

std::map<std::string, int> actionsOf(const json::Value& body) {
	std::map<std::string, int> m;
	const json::Value* c = body.get("counters");
	const json::Value* a = c ? c->get("actions") : nullptr;
	if (a && a->isObject())
		for (const auto& kv : a->o) m[kv.first] = (int)kv.second.i();
	return m;
}

}  // namespace

// ---- --usage-check ---------------------------------------------------------------------

int usageCheck() {
	gBase = envVar("CL_USAGE_URL");
	while (!gBase.empty() && gBase.back() == '/') gBase.pop_back();
	if (gBase.empty()) {
		printf("usage check: set CL_USAGE_URL to the stand-in (mac/Tools/usage-check/server.py)\n");
		return 1;
	}
	gHome = configDir() + format("/usage-check-%d", (int)getpid());
	removeTree(gHome);
	const std::string dir = gHome;
	long long clock = 1793000000;   // 2026-10-26
	bool syncOn = false, policy = false;
	Prefs tp;   // the settings it reads (defaults to begin with)
	usage::Engine engine(dir);
	engine.now = [&] { return clock; };
	engine.endpoint = [] { return usage::endpointUrl(true); };
	engine.policyOff = [&] { return policy; };
	auto now = [&] {
		usage::Now n;
		n.settings = usage::nonDefault(tp);
		n.syncOn = syncOn;
		return n;
	};
	auto advance = [&](double hours) { clock += (long long)std::llround(hours * 3600); };

	// Where it goes.
	check(usage::endpointUrl(true) == gBase + "/api/usage", "CL_USAGE_URL points it at the stand-in on this computer (" + gBase + ")");
	g_setenv("CL_USAGE_URL", "https://usage-stats-test--cedarlogic.netlify.app/api/usage", TRUE);
	check(usage::endpointUrl(true) == "https://usage-stats-test--cedarlogic.netlify.app/api/usage", "...or at one of the site's drafts, over HTTPS");
	g_setenv("CL_USAGE_URL", "http://usage-stats-test--cedarlogic.netlify.app", TRUE);
	check(usage::endpointUrl(true).empty(), "...never over plain HTTP");
	g_setenv("CL_USAGE_URL", "https://example.com", TRUE);
	check(usage::endpointUrl(true).empty(), "...but never at another host");
	g_unsetenv("CL_USAGE_URL");
	check(usage::endpointUrl(true).empty(), "a test run without one sends nowhere");
	{
		// (No CL_ variable and no CI: as a real launch is.)
		const std::string ci = envVar("CI");
		g_unsetenv("CI");
		const std::string real = usage::endpointUrl(false);
		g_setenv("CI", "true", TRUE);
		const std::string inCi = usage::endpointUrl(false);
		if (ci.empty()) g_unsetenv("CI"); else g_setenv("CI", ci.c_str(), TRUE);
		check(real == "https://cedarlogic.netlify.app/api/usage" && inCi.empty(),
		      "a real launch sends to https://cedarlogic.netlify.app/api/usage (" + real + "), never in CI");
	}
	g_setenv("CL_USAGE_URL", gBase.c_str(), TRUE);
	check(engine.app.platform == "linux" && (engine.app.arch == "x86_64" || engine.app.arch == "aarch64") && !engine.app.os.empty() &&
	          engine.app.os.size() <= 16,
	      "the app: linux, " + engine.app.arch + ", " + engine.app.os + ", " + engine.app.lang + ", version " + engine.app.version +
	          format(" build %lld", engine.app.build));
	check(usage::osName("ID=debian\nVERSION_ID=\"12\"\n") == "debian 12" && usage::osName("ID=ubuntu\nVERSION_ID=\"24.04\"\n") == "ubuntu 24" &&
	          usage::osName("ID=raspbian\nVERSION_ID=\"12\"\n") == "raspbian 12" && usage::osName("ID=arch\n") == "arch" &&
	          usage::osName("NAME=Something\n") == "linux" && usage::osName("ID=\"/home/someone/evil name\"\nVERSION_ID=1.2\n").size() <= 16,
	      "app.os is the distribution and its major version, from /etc/os-release (\"debian 12\", \"ubuntu 24\", \"raspbian 12\")");

	// Off: nothing at all.
	control("{\"reset\": true, \"platform\": \"linux\"}");
	engine.count("save");
	engine.noteSession();
	engine.sendIfDue(now(), true);
	check(requests().empty() && engine.counters().isZero() && engine.id().empty(), "off: nothing counted, nothing sent, no id");

	// A few settings changed, and things that must never go.
	const std::string profile = g_get_home_dir() ? std::string(g_get_home_dir()) : std::string("/home/someone");
	tp.showGrid = false;
	tp.accent = 2;
	tp.wireDotSize = 0.25;
	tp.studentName = "Secret Student Name";
	tp.newTemplate = profile + "/.local/share/CedarLogic/Templates/My Lab";
	tp.updateChannel = 1;
	tp.showGateNames = false;

	// On: a fresh id, then a snapshot.
	engine.set(true);
	const std::string id1 = engine.id();
	check(usage::Engine::isUsageId(id1) && id1.size() == 32, "switched on: a random id of 32 hex digits (" + id1 + ")");
	engine.noteSession();
	engine.count("save");
	engine.count("save");
	engine.count("tidy");
	engine.count("not an id!");
	engine.addTime(300, 60, 30);
	syncOn = true;
	engine.sendIfDue(now());
	std::vector<json::Value> p = posts();
	json::Value b = lastBody();
	check(p.size() == 1 && b.str("kind") == "snapshot", format("the first report is a snapshot (%zu sent)", p.size()));
	check(!p.empty() && noProblems(p.back()) && p.back().integer("answer") == 200,
	      "...in the contract's shape (" + (p.empty() ? std::string("-") : problemsOf(p.back())) + ")");
	check(!p.empty() && p.back().flag("hashOk"), "...its settingsHash is SHA-256 of the canonical JSON (the server makes the same one)");
	check(!p.empty() && p.back().str("origin").empty() && p.back().str("ctype") == "application/json",
	      "...sent as JSON (" + (p.empty() ? std::string() : p.back().str("ctype")) + "), with no Origin header");
	const std::set<std::string> s1 = keysOf(b.get("settings"));
	check(s1 == std::set<std::string>{ "showGrid", "accent", "wireDotSize", "newTemplate", "testingGroup", "gateLabels" },
	      "...with every setting that isn't its default " + join(s1));
	const json::Value* st = b.get("settings");
	check(st && st->str("newTemplate") == "yours" && st->get("wireDotSize") && st->get("wireDotSize")->n == 0.25 && st->str("testingGroup") == "beta",
	      "...a template of yours is just \"yours\"; numbers and words as they are");
	const json::Value* c1 = b.get("counters");
	check(c1 && c1->integer("sessions") == 1 && c1->integer("activeSeconds") == 300 && c1->integer("focusSeconds") == 60 &&
	          c1->integer("simViewSeconds") == 30 && c1->flag("syncOn") && c1->integer("classroomSessions", -1) == 0,
	      "...and the counters");
	check(actionsOf(b) == std::map<std::string, int>{ { "save", 2 }, { "tidy", 1 } }, "...actions counted by id, nothing that isn't one");
	const std::string sentAt = b.str("sentAt");
	check(sentAt.size() == 17 && sentAt.back() == 'Z', "sentAt is UTC to the minute (" + sentAt + ")");
	const json::Value* app = b.get("app");
	check(app && app->str("platform") == "linux" && app->str("os") == engine.app.os, "app.platform is linux, app.os " + engine.app.os);
	check(engine.counters().isZero() && engine.lastSentKind() == "snapshot", "the counters start again once it's confirmed");

	// The file: only this user's, its folder too.
	engine.count("save");
	engine.save();
	check(modeIs(engine.file(), 0600) && modeIs(dir, 0700), "the local file is 0600 (only you can read it), its folder 0700");
	{
		usage::Engine reloaded(dir);
		check(reloaded.counters().actions["save"] == 1 && reloaded.lastSentKind() == "snapshot" && reloaded.id() == id1, "...and it reads back");
	}

	// Not due again within the day.
	advance(3);
	engine.sendIfDue(now());
	check(posts().size() == 1, "not again within 24 hours");

	// A day later, one setting changed: a delta with only that.
	advance(22);
	tp.gateSize = 60;
	engine.count("zoomIn");
	engine.sendIfDue(now());
	b = lastBody();
	check(b.str("kind") == "delta" && keysOf(b.get("settings")) == std::set<std::string>{ "gateSize" },
	      "a day on, a delta with only the setting that changed " + join(keysOf(b.get("settings"))));
	check(actionsOf(b) == std::map<std::string, int>{ { "save", 1 }, { "zoomIn", 1 } }, "...and the counters since the last one");

	// Nothing changed, nothing counted: a ping.
	advance(25);
	engine.sendIfDue(now());
	b = lastBody();
	check(b.str("kind") == "ping" && !b.get("settings") && !b.get("counters") && !b.str("settingsHash").empty(),
	      "nothing new: a ping, with no settings or counters, just the hash");

	// A setting back to its default: a delta saying so.
	advance(25);
	tp.showGrid = true;
	engine.sendIfDue(now());
	b = lastBody();
	check(b.str("kind") == "delta" && keysOf(b.get("settings")) == std::set<std::string>{ "showGrid" } && b.get("settings")->flag("showGrid"),
	      "a setting put back to its default goes as a delta with the default");
	{
		json::Value store = get("/store");
		const json::Value* held = store.get(id1);
		const json::Value* hs = held ? held->get("settings") : nullptr;
		check(hs && hs->integer("gateSize") == 60, "the server has the snapshot with the deltas applied");
	}

	// haveSnapshot false: a snapshot follows.
	control("{\"noSnapshot\": 1}");
	advance(25);
	engine.count("save");
	size_t before = posts().size();
	engine.sendIfDue(now());
	p = posts();
	std::string kinds;
	for (size_t i = before; i < p.size(); i++) kinds += (kinds.empty() ? "" : ",") + (p[i].get("body") ? p[i].get("body")->str("kind") : std::string());
	check(kinds == "delta,snapshot", "haveSnapshot false: a snapshot goes straight after (" + kinds + ")");
	b = lastBody();
	check(keysOf(b.get("settings")) == std::set<std::string>{ "accent", "wireDotSize", "newTemplate", "testingGroup", "gateSize", "gateLabels" },
	      "...with every non-default setting " + join(keysOf(b.get("settings"))));

	// The server down: reports kept, 7 at most, then all sent.
	control("{\"fail\": 1000, \"status\": 503}");
	for (int i = 0; i < 9; i++) {
		advance(25);
		engine.count("save");
		engine.sendIfDue(now());
	}
	check(engine.keptCount() == 7, format("the server down for 9 days: 7 reports kept (%d)", engine.keptCount()));
	control("{\"fail\": 0}");
	before = posts().size();
	advance(1);
	engine.sendIfDue(now());
	p = posts();
	check(engine.keptCount() == 0 && p.size() - before == 7, format("back up: the 7 kept go at the next chance, oldest first (%zu)", p.size() - before));
	bool allTaken = p.size() >= 7;
	for (size_t i = p.size() >= 7 ? p.size() - 7 : 0; i < p.size(); i++) allTaken = allTaken && p[i].integer("answer") == 200;
	check(allTaken, "...and each is taken");

	// 429: wait a day.
	control("{\"tooMany\": 1}");
	advance(25);
	engine.count("save");
	engine.sendIfDue(now());
	const size_t after429 = posts().size();
	advance(6);
	engine.sendIfDue(now());
	check(posts().size() == after429 && engine.keptCount() == 1, "429: kept, and not tried again for 24 hours");
	advance(19);
	engine.sendIfDue(now());
	check(posts().size() == after429 + 2 && engine.keptCount() == 0, "...then it goes (with the day's new one after it)");

	// Policy: DisableUsageStats.
	policy = true;
	advance(25);
	engine.count("save");
	const size_t beforePolicy = requests().size();
	engine.sendIfDue(now(), true);
	check(requests().size() == beforePolicy && engine.counters().isZero(), "DisableUsageStats: nothing counted, nothing sent");
	policy = false;

	// The 16 KB cap and 200 actions.
	for (int i = 0; i < 230; i++) engine.count(format("action%034d", i));
	check(engine.counters().actions.size() == 200, format("at most 200 actions are counted (%zu)", engine.counters().actions.size()));
	const std::string big = engine.encoded(engine.nextReport(now()), id1, clock);
	check(big.size() <= 16 * 1024, format("a report is 16 KB at most (%zu bytes)", big.size()));
	advance(25);
	engine.sendIfDue(now());
	p = posts();
	check(!p.empty() && noProblems(p.back()) && p.back().integer("bytes", 99999) <= 16384, "...and the server takes it");

	// The leak scan, on real reports.
	tp.studentName = g_get_user_name() ? std::string(g_get_user_name()) : std::string("Alex Student");
	tp.recent = { profile + "/Documents/Half Adder Lab.cdl" };
	tp.lastFolder = profile + "/Documents/Logic";
	tp.lastCircuit = profile + "/.local/share/CedarLogic/Library/Half Adder Lab/circuit.cdl";
	tp.feedbackEmail = "someone.private@example.com";
	tp.feedbackName = "Secret Student Name";
	tp.feedbackInstallId = "0123456789abcdef0123456789abcdef";
	engine.count("save");
	std::vector<std::string> texts = { engine.preview(now()), engine.encoded(engine.nextReport(now()), id1, clock) };
	for (const json::Value& e : posts()) texts.push_back(e.str("raw"));
	std::vector<std::string> needles = usage::machineNeedles();
	// (And a sync code's and a classroom code's shapes, should one ever be read.)
	for (const char* s : { "Half Adder Lab", "Secret Student Name", "Templates/My Lab", "My Lab", "someone.private@example.com",
	                       "0123456789abcdef0123456789abcdef", "Documents/Logic", "ABCD-EFGH-JKMN-PQRS-TVWX-YZ23-4567", "CLASS-7Q4K" })
		needles.push_back(s);
	std::set<std::string> leaked;
	for (const std::string& t : texts)
		for (const std::string& l : usage::leaks(t, needles)) leaked.insert(l);
	check(leaked.empty(), format("the leak scan finds nothing in %zu reports (home folder, user and real name, host name, recent files, names, e-mail, codes): ", texts.size()) +
	                          join(leaked));
	check(!usage::leaks("{\"x\":\"" + profile + "/Documents\"}", needles).empty(), "...and does find one when it's there (" + join(std::set<std::string>(needles.begin(), needles.begin() + std::min<size_t>(3, needles.size()))) + ")");

	// Delete what's been sent: the server forgets it, a new id follows.
	const size_t beforeDelete = requests().size();
	const bool deleted = engine.deleteSent();
	std::vector<json::Value> r = requests();
	check(deleted && r.size() == beforeDelete + 1 && r.back().str("method") == "DELETE" && r.back().str("id") == id1 && r.back().str("origin").empty(),
	      "Delete what's been sent: DELETE ?id=<the id>, no Origin header");
	check(!get("/store").get(id1), "...the server has nothing for it");
	const std::string id2 = engine.id();
	check(usage::Engine::isUsageId(id2) && id2 != id1, "...and a new id, still on");
	advance(1);
	engine.sendIfDue(now());
	b = lastBody();
	check(b.str("kind") == "snapshot" && b.str("id") == id2, "...whose first report is a snapshot");

	// Off: DELETE, the id and the counts gone.
	engine.count("save");
	engine.set(false);
	engine.flushDeletes();
	r = requests();
	check(!r.empty() && r.back().str("method") == "DELETE" && r.back().str("id") == id2 && engine.id().empty(),
	      "switched off: the server is told to delete it, and the id is forgotten");
	check(engine.counters().isZero() && engine.keptCount() == 0 && readFile(engine.file()).find(id2) == std::string::npos,
	      "...and what was counted is gone too (from the file as well)");
	const size_t beforeOff = requests().size();
	advance(48);
	engine.count("save");
	engine.sendIfDue(now(), true);
	check(requests().size() == beforeOff, "off: nothing sent");

	// On again: a fresh id. Off with the server unreachable: the DELETE waits.
	engine.set(true);
	const std::string id3 = engine.id();
	check(id3 != id1 && id3 != id2 && usage::Engine::isUsageId(id3), "on again: a fresh id");
	engine.endpoint = [] { return std::string("http://127.0.0.1:9/api/usage"); };
	engine.set(false);
	engine.flushDeletes();
	check(engine.pendingDeletes() == std::vector<std::string>{ id3 }, "off with no connection: the DELETE is kept");
	engine.endpoint = [] { return usage::endpointUrl(true); };
	engine.sendIfDue(now());
	r = requests();
	check(!r.empty() && r.back().str("method") == "DELETE" && r.back().str("id") == id3 && engine.pendingDeletes().empty(), "...and goes at the next chance");

	// Every report the server saw fitted the contract, as JSON with no Origin.
	int bad = 0;
	// (One the stand-in answered 503 or 429 before looking at it has no problems listed.)
	for (const json::Value& e : posts()) {
		const json::Value* pr = e.get("problems");
		if ((pr && pr->isArray() && !pr->a.empty()) || !e.str("origin").empty()) bad++;
	}
	check(bad == 0, format("every report fitted the contract (%d didn't)", bad));

	// Samples for the record.
	const std::string samples = envVar("USAGE_SAMPLES");
	if (!samples.empty()) {
		std::set<std::string> seen;
		std::string text;
		for (const json::Value& e : posts()) {
			const json::Value* body = e.get("body");
			const std::string kind = body ? body->str("kind") : std::string();
			if (kind.empty() || seen.count(kind)) continue;
			seen.insert(kind);
			text += "// " + kind + "\n" + e.str("raw") + "\n\n";
		}
		g_file_set_contents(samples.c_str(), text.c_str(), (gssize)text.size(), nullptr);
	}
	removeTree(dir);
	return finish("usage check");
}

// ---- --usage-live ------------------------------------------------------------------------

int usageLive() {
	const std::string api = usage::endpointUrl(true);
	check(!api.empty(), "live: pointed at " + (api.empty() ? std::string("nowhere (CL_USAGE_URL must be the site or a draft)") : api));
	if (api.empty()) return finish("usage check --live");
	gHome = configDir() + format("/usage-live-%d", (int)getpid());
	Prefs tp;
	tp.showGrid = false;
	tp.accent = 2;
	const std::string dir = gHome + "-live";
	usage::Engine e(dir);
	e.endpoint = [api] { return api; };
	auto now = [&] {
		usage::Now n;
		n.settings = usage::nonDefault(tp);
		return n;
	};
	auto last = [&] {
		const std::vector<std::string> l = e.log();
		return l.empty() ? std::string("-") : l.back();
	};
	e.set(true);
	const std::string id = e.id();
	e.noteSession();
	e.count("save");
	e.addTime(60, 0, 0);
	e.sendIfDue(now(), true);
	check(e.lastSentKind() == "snapshot" && e.lastHaveSnapshot() == 1, "live snapshot: taken, haveSnapshot true (" + last() + ")");
	tp.gateSize = 60;
	e.count("tidy");
	e.sendIfDue(now(), true);
	check(e.lastSentKind() == "delta" && e.lastHaveSnapshot() == 1, "live delta: taken, haveSnapshot true (" + last() + ")");
	e.sendIfDue(now(), true);
	check(e.lastSentKind() == "ping" && e.lastHaveSnapshot() == 1, "live ping: taken, its hash matches what the server holds (" + last() + ")");
	e.set(false);
	e.flushDeletes();
	check(e.pendingDeletes().empty() && last() == "deleted", "live DELETE ?id=: 200 (" + last() + ")");
	// The id is gone there: a ping for it finds no snapshot.
	usage::Pending ping;
	ping.kind = "ping";
	ping.from = ping.to = usage::Engine::date(time(nullptr));
	ping.hash = usage::hash(usage::Settings());
	clsync::HttpRequest r;
	r.method = "POST";
	r.url = api;
	r.headers.push_back({ "Content-Type", "application/json" });
	r.body = e.encoded(ping, id, time(nullptr));
	const clsync::HttpResponse a = syncplatform::http(r, 30);
	json::Value v;
	json::parse(a.body, v);
	check(a.status == 200 && v.isObject() && v.get("haveSnapshot") && !v.flag("haveSnapshot", true),
	      format("after DELETE, a ping for the id: haveSnapshot false (%d %s)", a.status, a.body.c_str()));
	clsync::HttpRequest d;
	d.method = "DELETE";
	d.url = api + "?id=" + id;
	const clsync::HttpResponse da = syncplatform::http(d, 30);
	check(da.status == 200, format("DELETE again: 200 (it's idempotent), and the id is gone (%d)", da.status));
	removeTree(dir);
	return finish("usage check --live");
}

}  // namespace privacycheck
