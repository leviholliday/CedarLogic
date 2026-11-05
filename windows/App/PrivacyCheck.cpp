// The checks of crash reports and usage stats (see PrivacyCheck.h).

#include "PrivacyCheck.h"
#include "App.h"
#include "CrashReports.h"
#include "Dialogs.h"
#include "Http.h"
#include "Privacy.h"
#include "ShowMeAround.h"
#include "SyncJson.h"
#include "UsageReport.h"
#include "UsageStats.h"
#include "Welcome.h"
#include "Window.h"

#include <aclapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>
#include <set>
#include <sstream>

namespace privacycheck {

namespace json = clsync::json;

namespace {

std::string gHome;
int gFailed = 0, gPassed = 0;
Out gOut;

void check(bool ok, const std::string& what) {
	(ok ? gPassed : gFailed)++;
	gOut(std::string(ok ? "PASS " : "FAIL ") + what + "\n");
}

int finish(const char* name) {
	gOut(strf("%s: %d passed, %d failed%s\n", name, gPassed, gFailed, gFailed == 0 ? " -- all passed" : ""));
	return gFailed == 0 && gPassed > 0 ? 0 : 1;
}

std::string envVar(const wchar_t* name) {
	wchar_t buf[2048];
	const DWORD n = GetEnvironmentVariableW(name, buf, 2048);
	return n > 0 && n < 2048 ? U(std::wstring(buf, n)) : std::string();
}

void removeTree(const std::string& dir) {
	if (dir.empty()) return;
	std::wstring from = W(dir);
	from.push_back(L'\0');
	SHFILEOPSTRUCTW op = {};
	op.wFunc = FO_DELETE;
	op.pFrom = from.c_str();
	op.fFlags = FOF_NO_UI;
	SHFileOperationW(&op);
}

std::string readFile(const std::string& path) {
	std::ifstream in(W(path).c_str(), std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

std::string join(const std::set<std::string>& s) {
	std::string o;
	for (const std::string& x : s) o += (o.empty() ? "" : ",") + x;
	return "[" + o + "]";
}

std::set<std::string> keysOf(const json::Value* o) {
	std::set<std::string> k;
	if (o && o->isObject())
		for (const auto& kv : o->o) k.insert(kv.first);
	return k;
}

// ---- The stand-in server --------------------------------------------------------------

std::string gBase;

json::Value get(const std::string& path) {
	http::Request r;
	r.method = "GET";
	r.url = gBase + path;
	json::Value v;
	json::parse(http::send(r).body, v);
	return v;
}

void control(const std::string& body) {
	http::Request r;
	r.method = "POST";
	r.url = gBase + "/control";
	r.headers.push_back({ "Content-Type", "application/json" });
	r.body = body;
	http::send(r);
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

// This user's SID, and whether `path` is this user's alone (one ACE allowing
// it, nothing inherited).
bool ownerOnly(const std::string& path) {
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
	DWORD size = 0;
	GetTokenInformation(token, TokenUser, nullptr, 0, &size);
	std::vector<unsigned char> buf(size ? size : 1);
	const bool gotUser = size && GetTokenInformation(token, TokenUser, buf.data(), size, &size);
	CloseHandle(token);
	if (!gotUser) return false;
	PSID me = ((TOKEN_USER*)buf.data())->User.Sid;
	PACL dacl = nullptr;
	PSECURITY_DESCRIPTOR sd = nullptr;
	if (GetNamedSecurityInfoW(W(path).c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr, &sd) != ERROR_SUCCESS)
		return false;
	SECURITY_DESCRIPTOR_CONTROL ctl = 0;
	DWORD rev = 0;
	GetSecurityDescriptorControl(sd, &ctl, &rev);
	bool ok = dacl && (ctl & SE_DACL_PROTECTED) && dacl->AceCount == 1;
	if (ok) {
		void* ace = nullptr;
		ok = GetAce(dacl, 0, &ace) && ((ACE_HEADER*)ace)->AceType == ACCESS_ALLOWED_ACE_TYPE &&
		     EqualSid((PSID) & ((ACCESS_ALLOWED_ACE*)ace)->SidStart, me);
	}
	LocalFree(sd);
	return ok;
}

}  // namespace

void prepare(const char* name) {
	wchar_t tmp[MAX_PATH] = L"";
	GetTempPathW(MAX_PATH, tmp);
	gHome = U(tmp) + strf("cedarlogic-%s-%lu", name, (unsigned long)GetCurrentProcessId());
	CreateDirectoryW(W(gHome).c_str(), nullptr);
	SetEnvironmentVariableW(L"CEDARLOGIC_HOME", W(gHome).c_str());
}

void cleanUp() { removeTree(gHome); }

// ---- --usage-check ---------------------------------------------------------------------

int usageCheck(const Out& out) {
	gOut = out;
	gBase = envVar(L"CL_USAGE_URL");
	while (!gBase.empty() && gBase.back() == '/') gBase.pop_back();
	if (gBase.empty()) {
		out("usage check: set CL_USAGE_URL to the stand-in (mac/Tools/usage-check/server.py)\n");
		return 1;
	}
	const std::string dir = gHome + "\\Usage";
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
	check(usage::endpointUrl(true) == gBase + "/api/usage", "CL_USAGE_URL points it at the stand-in on this PC (" + gBase + ")");
	SetEnvironmentVariableW(L"CL_USAGE_URL", L"https://usage-stats-test--cedarlogic.netlify.app/api/usage");
	check(usage::endpointUrl(true) == "https://usage-stats-test--cedarlogic.netlify.app/api/usage", "...or at one of the site's drafts, over HTTPS");
	SetEnvironmentVariableW(L"CL_USAGE_URL", L"http://usage-stats-test--cedarlogic.netlify.app");
	check(usage::endpointUrl(true).empty(), "...never over plain HTTP");
	SetEnvironmentVariableW(L"CL_USAGE_URL", L"https://example.com");
	check(usage::endpointUrl(true).empty(), "...but never at another host");
	SetEnvironmentVariableW(L"CL_USAGE_URL", nullptr);
	check(usage::endpointUrl(true).empty(), "a test run without one sends nowhere");
	{
		const std::string ci = envVar(L"CI"), home = envVar(L"CEDARLOGIC_HOME");
		SetEnvironmentVariableW(L"CI", nullptr);
		SetEnvironmentVariableW(L"CEDARLOGIC_HOME", nullptr);
		const std::string real = usage::endpointUrl(false);
		SetEnvironmentVariableW(L"CI", L"true");
		const std::string inCi = usage::endpointUrl(false);
		SetEnvironmentVariableW(L"CI", ci.empty() ? nullptr : W(ci).c_str());
		SetEnvironmentVariableW(L"CEDARLOGIC_HOME", W(home).c_str());
		check(real == "https://cedarlogic.netlify.app/api/usage" && inCi.empty(),
		      "a real launch sends to https://cedarlogic.netlify.app/api/usage (" + real + "), never in CI");
	}
	SetEnvironmentVariableW(L"CL_USAGE_URL", W(gBase).c_str());
	check(engine.app.platform == "windows" && (engine.app.arch == "x86_64" || engine.app.arch == "aarch64") &&
	          engine.app.os.find('.') != std::string::npos && (engine.app.os.compare(0, 3, "10.") == 0 || engine.app.os.compare(0, 3, "11.") == 0),
	      "the app: windows, " + engine.app.arch + ", Windows " + engine.app.os + ", " + engine.app.lang + ", version " + engine.app.version +
	          strf(" build %lld", engine.app.build));

	// Off: nothing at all.
	control("{\"reset\": true, \"platform\": \"windows\"}");
	engine.count("save");
	engine.noteSession();
	engine.sendIfDue(now(), true);
	check(requests().empty() && engine.counters().isZero() && engine.id().empty(), "off: nothing counted, nothing sent, no id");

	// A few settings changed, and things that must never go.
	const std::string profile = envVar(L"USERPROFILE").empty() ? std::string("C:\\Users\\Someone") : envVar(L"USERPROFILE");
	tp.showGrid = false;
	tp.accent = 2;
	tp.wireDotSize = 0.25;
	tp.studentName = "Secret Student Name";
	tp.newTemplate = profile + "\\AppData\\Roaming\\CedarLogic\\Templates\\My Lab";
	tp.betaTester = true;
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
	check(p.size() == 1 && b.str("kind") == "snapshot", strf("the first report is a snapshot (%zu sent)", p.size()));
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
	check(app && app->str("platform") == "windows" && app->str("os") == engine.app.os, "app.platform is windows, app.os " + engine.app.os);
	check(engine.counters().isZero() && engine.lastSentKind() == "snapshot", "the counters start again once it's confirmed");

	// The file: only this user's, its folder too.
	engine.count("save");
	engine.save();
	check(ownerOnly(engine.file()) && ownerOnly(dir), "the local file and its folder (%APPDATA%\\CedarLogic\\Usage) are this Windows user's alone");
	const std::string ini = tp.text();
	check(ini.find(id1) == std::string::npos && ini.find("usageStats=") != std::string::npos,
	      "native.ini has the switch, never the id or anything counted");
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
	check(engine.keptCount() == 7, strf("the server down for 9 days: 7 reports kept (%d)", engine.keptCount()));
	control("{\"fail\": 0}");
	before = posts().size();
	advance(1);
	engine.sendIfDue(now());
	p = posts();
	check(engine.keptCount() == 0 && p.size() - before == 7, strf("back up: the 7 kept go at the next chance, oldest first (%zu)", p.size() - before));
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
	for (int i = 0; i < 230; i++) engine.count(strf("action%034d", i));
	check(engine.counters().actions.size() == 200, strf("at most 200 actions are counted (%zu)", engine.counters().actions.size()));
	const std::string big = engine.encoded(engine.nextReport(now()), id1, clock);
	check(big.size() <= 16 * 1024, strf("a report is 16 KB at most (%zu bytes)", big.size()));
	advance(25);
	engine.sendIfDue(now());
	p = posts();
	check(!p.empty() && noProblems(p.back()) && p.back().integer("bytes", 99999) <= 16384, "...and the server takes it");

	// The leak scan, on real reports.
	tp.studentName = envVar(L"USERNAME").empty() ? std::string("Alex Student") : envVar(L"USERNAME");
	tp.recent = { profile + "\\Documents\\Half Adder Lab.cdl" };
	tp.lastFolder = profile + "\\Documents\\Logic";
	tp.lastCircuit = profile + "\\AppData\\Roaming\\CedarLogic\\Library\\Half Adder Lab\\circuit.cdl";
	tp.feedbackEmail = "someone.private@example.com";
	tp.installId = "0123456789abcdef0123456789abcdef";
	engine.count("save");
	std::vector<std::string> texts = { engine.preview(now()), engine.encoded(engine.nextReport(now()), id1, clock) };
	for (const json::Value& e : posts()) texts.push_back(e.str("raw"));
	std::vector<std::string> needles = usage::machineNeedles();
	for (const char* s : { "Half Adder Lab", "Secret Student Name", "Templates\\\\My Lab", "My Lab", "someone.private@example.com",
	                       "0123456789abcdef0123456789abcdef", "Documents\\\\Logic" })
		needles.push_back(s);
	std::set<std::string> leaked;
	for (const std::string& t : texts)
		for (const std::string& l : usage::leaks(t, needles)) leaked.insert(l);
	check(leaked.empty(), strf("the leak scan finds nothing in %zu reports (profile, user, computer, recent files, names, e-mail): ", texts.size()) +
	                          join(leaked));
	std::string escaped;
	for (char c : profile) escaped += c == '\\' ? std::string("\\\\") : std::string(1, c);
	check(!usage::leaks("{\"x\":\"" + escaped + "\"}", needles).empty(), "...and does find one when it's there");

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
	for (const json::Value& e : posts())
		if (!noProblems(e) || !e.str("origin").empty()) bad++;
	check(bad == 0, strf("every report fitted the contract (%d didn't)", bad));

	// Samples for the record.
	const std::string samples = envVar(L"USAGE_SAMPLES");
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
		std::ofstream o(W(samples).c_str(), std::ios::binary);
		o << text;
	}
	return finish("usage check");
}

// ---- --usage-live ------------------------------------------------------------------------

int usageLive(const Out& out) {
	gOut = out;
	const std::string api = usage::endpointUrl(true);
	check(!api.empty(), "live: pointed at " + (api.empty() ? std::string("nowhere (CL_USAGE_URL must be the site or a draft)") : api));
	if (api.empty()) return finish("usage check --live");
	Prefs tp;
	tp.showGrid = false;
	tp.accent = 2;
	usage::Engine e(gHome + "\\Live");
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
	http::Request r;
	r.method = "POST";
	r.url = api;
	r.headers.push_back({ "Content-Type", "application/json" });
	r.body = e.encoded(ping, id, time(nullptr));
	const http::Response a = http::send(r);
	json::Value v;
	json::parse(a.body, v);
	check(a.status == 200 && v.isObject() && v.get("haveSnapshot") && !v.flag("haveSnapshot", true),
	      strf("after DELETE, a ping for the id: haveSnapshot false (%d %s)", a.status, a.body.c_str()));
	http::Request d;
	d.method = "DELETE";
	d.url = api + "?id=" + id;
	const http::Response da = http::send(d);
	check(da.status == 200, strf("DELETE again: 200 (it's idempotent), and the id is gone (%d)", da.status));
	return finish("usage check --live");
}

// ---- --privacy-check: the consent, in real windows ------------------------------------------

namespace {

void pump(double seconds) {
	const double until = nowSeconds() + seconds;
	while (nowSeconds() < until) {
		MSG m;
		while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
			TranslateMessage(&m);
			DispatchMessageW(&m);
		}
		Sleep(5);
	}
}

bool waitFor(const std::function<bool()>& f, double seconds) {
	const double until = nowSeconds() + seconds;
	while (nowSeconds() < until) {
		if (f()) return true;
		pump(0.05);
	}
	return f();
}

// The real pointer (SendInput), at `at` (screen pixels), with a button's flags or none.
void pointerTo(POINT at, DWORD flags = 0) {
	const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
	const int vw = std::max(1, GetSystemMetrics(SM_CXVIRTUALSCREEN) - 1), vh = std::max(1, GetSystemMetrics(SM_CYVIRTUALSCREEN) - 1);
	SetCursorPos(at.x, at.y);
	INPUT in = {};
	in.type = INPUT_MOUSE;
	in.mi.dx = (LONG)std::lround((at.x - vx) * 65535.0 / vw);
	in.mi.dy = (LONG)std::lround((at.y - vy) * 65535.0 / vh);
	in.mi.dwFlags = flags | MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
	SendInput(1, &in, sizeof in);
}

// A real click in the middle of one of the question's controls. False (with
// why) when it isn't showing or something else is under the pointer there.
bool clickControl(privacy::Control c, std::string& why) {
	RECT r;
	if (!privacy::controlRect(c, r)) { why = "not showing"; return false; }
	const HWND top = privacy::askWindow();
	SetWindowPos(top, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	const POINT mid = { (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
	pointerTo(mid);
	pump(0.08);
	const HWND under = WindowFromPoint(mid);
	if (under != top) { why = "another window is under the pointer"; return false; }
	pointerTo(mid, MOUSEEVENTF_LEFTDOWN);
	pump(0.05);
	pointerTo(mid, MOUSEEVENTF_LEFTUP);
	why.clear();
	return true;
}

// A real key press (the window in front has the keyboard).
void pressKey(WORD vk) {
	INPUT in[2] = {};
	in[0].type = INPUT_KEYBOARD;
	in[0].ki.wVk = vk;
	in[1] = in[0];
	in[1].ki.dwFlags = KEYEVENTF_KEYUP;
	SendInput(2, in, sizeof(INPUT));
}

void bringToFront(HWND h) {
	SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	SetForegroundWindow(h);
	SetFocus(h);
	pump(0.1);
}

std::string ini() { return readFile(settingsDir() + "\\native.ini"); }
bool iniHas(const std::string& line) { return ("\n" + ini()).find("\n" + line + "\n") != std::string::npos; }

void notAsked() {
	prefs().privacyAsked = false;
	prefs().privacyChoice.clear();
	prefs().save();
}

// "See exactly what's sent", opened by the click: seen, then Done.
bool g_sentSeen = false;
std::string g_sentRows;
void CALLBACK closeSent(HWND, UINT, UINT_PTR id, DWORD) {
	const HWND d = privacy::whatsSentWindow();
	if (d == nullptr) return;
	KillTimer(nullptr, id);
	g_sentSeen = IsWindowVisible(d) != FALSE;
	g_sentRows = windowText(d);
	PostMessageW(d, WM_COMMAND, IDOK, 0);
}

// Settings > Privacy, opened under the policy: both switches greyed out.
int g_policySwitches = -1;   // how many of the two are enabled (-1: not seen)
void CALLBACK readSettings(HWND, UINT, UINT_PTR id, DWORD) {
	Form* f = openPagedForm();
	if (f == nullptr || f->dialog == nullptr) return;
	KillTimer(nullptr, id);
	int enabled = 0, found = 0;
	for (const FormField& x : f->fields)
		if (x.kind == FormField::Check && (x.label == "Send crash reports" || x.label == "Send anonymous usage stats") && x.hwnd) {
			found++;
			if (IsWindowEnabled(x.hwnd)) enabled++;
		}
	g_policySwitches = found == 2 ? enabled : -2;
	PostMessageW(f->dialog, WM_CLOSE, 0, 0);
}

void policyChecks(CircuitWindow* w) {
	check(crashreports::policyOff() && usagestats::policyOff(), "policy: DisableCrashReporting and DisableUsageStats are read from HKLM\\Software\\Policies\\CedarLogic");
	check(!privacy::crashOffered() && !privacy::usageOffered() && !privacy::anythingToAsk(), "policy: neither is offered");
	prefs().hasSeenWelcome = true;
	notAsked();
	privacy::askWhenReady();
	pump(1.5);
	check(!privacy::owed() && !privacy::sheetOpen() && !privacy::watching(), "policy: nobody is asked (no sheet)");
	usage::Engine e(gHome + "\\PolicyUsage");
	e.policyOff = [] { return usagestats::policyOff(); };
	e.set(true);
	e.count("save");
	e.noteSession();
	check(!e.enabled() && e.counters().isZero(), "policy: even with the switch on, nothing is counted");
	setPreferencesPage(7);
	SetTimer(nullptr, 0, 300, readSettings);
	showPreferencesDialog(w->window());
	check(g_policySwitches == 0, strf("policy: Settings > Privacy has both switches greyed out (%d enabled)", g_policySwitches));
}

}  // namespace

int consentCheck(const Out& out) {
	gOut = out;
	CircuitWindow* w = newCircuitWindow();
	if (w == nullptr) {
		check(false, "a circuit window");
		return finish("privacy check");
	}
	w->present();
	pump(1.0);
	// The question asks by itself here, as in a real launch (on this window).
	privacy::quiet = false;
	privacy::assumeFront = true;
	if (!envVar(L"CL_EXPECT_POLICY").empty()) {
		guarded("the privacy check", [&] { policyChecks(w); });
		privacy::quiet = true;
		privacy::assumeFront = false;
		w->destroy();
		pump(0.2);
		return finish("privacy check (policy)");
	}
	const bool crashOk = privacy::crashOffered();
	gOut(std::string("     (crash reports ") + (crashOk ? "in this build" : "not in this build") + ")\n");
	std::string why;
	guarded("the privacy check", [&] {
		// ---- The welcome's "A small favour" page.
		prefs().hasSeenWelcome = false;
		notAsked();
		welcome::show(w);
		welcome::pageForScreenshot(5);
		pump(0.8);
		const HWND page = privacy::askWindow();
		check(page != nullptr && IsWindowVisible(page), "welcome: \"A small favour\" is up, page 6 of 7, in a real window");
		bringToFront(page);
		RECT tmp;
		check(privacy::controlRect(privacy::CtlYes, tmp) && privacy::controlRect(privacy::CtlNo, tmp) && privacy::controlRect(privacy::CtlChoose, tmp) &&
		          privacy::controlRect(privacy::CtlSeeSent, tmp),
		      "welcome: Yes, No thanks, Choose what to send and See exactly what's sent are drawn");
		pressKey(VK_RIGHT);
		pressKey(VK_RETURN);
		pump(0.6);
		check(privacy::askWindow() == page && !prefs().privacyAsked, "welcome: no Continue -- Right and Enter don't go on without an answer");
		const bool clicked = clickControl(privacy::CtlNo, why);
		pump(1.0);
		check(clicked, "welcome: No thanks clicked with the real pointer " + why);
		check(prefs().privacyAsked && prefs().privacyChoice == "no" && !prefs().usageStats && !usagestats::on() &&
		          prefs().crashReports == (crashOk ? 0 : -1),
		      "welcome: No thanks -- asked, \"no\", usage stats off, crash reports off");
		check(iniHas("privacyAsked=true") && iniHas("privacyChoice=no") && iniHas("usageStats=false"), "welcome: ...saved in native.ini");
		check(welcome::isOpen() && privacy::askWindow() == nullptr, "welcome: and on to the last page by itself, no crash");
		welcome::finishForTest(showme::WelcomeExit::Blank);
		pump(0.4);
		privacy::askWhenReady();
		pump(1.2);
		check(!privacy::sheetOpen(), "asked once: no sheet after the welcome answered it");

		// ---- The updaters' sheet, in a real launch's order.
		prefs().hasSeenWelcome = true;
		prefs().showMeAroundSeen = false;
		prefs().seenWhatsNew = "an older version";
		notAsked();
		showme::quiet = false;
		showme::assumeFront = true;
		whatsnew::show(w);
		privacy::askWhenReady();
		showme::startWhenReady();
		pump(1.6);
		check(whatsnew::isOpen() && !privacy::sheetOpen() && !showme::active(), "sheet: never over What's New");
		whatsnew::closeForTest();
		const bool up = waitFor([] { return privacy::sheetOpen() && privacy::askWindow() != nullptr; }, 5);
		check(up && !showme::active(), "sheet: after What's New closes it comes up over the circuit window, before Show Me Around");
		const HWND sheet = privacy::askWindow();
		RECT r1 = {}, r2 = {};
		if (sheet) GetWindowRect(sheet, &r1);
		const int want = scaled(640, sheet ? dpiOf(sheet) : 96);
		check(sheet && IsWindowVisible(sheet) && std::abs((r1.right - r1.left) - want) <= 2 && !IsWindowEnabled(w->window()),
		      strf("sheet: a real window, %ld px wide (640 points), the circuit window under it disabled", (long)(r1.right - r1.left)));
		pump(2.8);   // (the card's glow, all the way through)
		if (sheet) GetWindowRect(sheet, &r2);
		check(EqualRect(&r1, &r2) != FALSE, "sheet: its size holds as it settles");
		if (sheet) bringToFront(sheet);
		const bool yes = clickControl(privacy::CtlYes, why);
		pump(0.8);
		check(yes, "sheet: Yes clicked with the real pointer " + why);
		check(!privacy::sheetOpen() && prefs().privacyAsked && prefs().privacyChoice == "yes" && prefs().usageStats && usagestats::on() &&
		          prefs().crashReports == (crashOk ? 1 : -1) && !crashreports::started(),
		      "sheet: Yes -- closed, \"yes\", usage stats on with a random id, crash reports on (never started in a check), no crash");
		check(iniHas("privacyAsked=true") && iniHas("privacyChoice=yes") && iniHas("usageStats=true") && IsWindowEnabled(w->window()),
		      "sheet: ...saved in native.ini, and the circuit window takes input again");
		check(waitFor([] { return showme::active(); }, 5), "sheet: Show Me Around follows it");
		showme::end();
		pump(0.4);
		showme::quiet = true;
		showme::assumeFront = false;
		privacy::askWhenReady();
		pump(1.2);
		check(!privacy::sheetOpen(), "asked once: no sheet again");

		// ---- Escape is No thanks (and keeps an earlier crash reports yes).
		notAsked();
		privacy::presentSheet(w);
		pump(0.6);
		if (privacy::askWindow()) bringToFront(privacy::askWindow());
		pressKey(VK_ESCAPE);
		pump(0.6);
		check(!privacy::sheetOpen() && prefs().privacyChoice == "no" && !usagestats::on(), "sheet: Escape is No thanks (usage stats off again)");
		check(!crashOk || prefs().crashReports == 1, "sheet: No thanks leaves crash reports on for someone who'd said yes to them");

		// ---- Choose what to send: the switches, then Continue with These.
		notAsked();
		privacy::presentSheet(w);
		pump(0.6);
		if (privacy::askWindow()) bringToFront(privacy::askWindow());
		const bool chose = clickControl(privacy::CtlChoose, why);
		pump(0.5);
		check(chose && privacy::choosing() && privacy::controlRect(privacy::CtlContinue, tmp) && !privacy::controlRect(privacy::CtlYes, tmp),
		      "sheet: Choose what to send shows the two switches and Continue with These " + why);
		const bool usageBefore = privacy::switchState(false);
		const bool flipped = clickControl(privacy::CtlUsageSwitch, why);
		pump(0.3);
		check(flipped && privacy::switchState(false) != usageBefore, "sheet: the usage stats switch flips with a click " + why);
		bool crashWant = privacy::switchState(true);
		if (crashOk) {
			clickControl(privacy::CtlCrashSwitch, why);
			pump(0.3);
			check(privacy::switchState(true) != crashWant, "sheet: the crash reports switch flips with a click " + why);
			crashWant = !crashWant;
		} else {
			check(!privacy::controlRect(privacy::CtlCrashSwitch, tmp), "sheet: crash reports' switch is greyed out (not in this build)");
		}
		const bool usageWant = privacy::switchState(false);
		const bool cont = clickControl(privacy::CtlContinue, why);
		pump(0.6);
		check(cont && !privacy::sheetOpen() && prefs().privacyChoice == "custom" && usagestats::on() == usageWant &&
		          (!crashOk || prefs().crashReports == (crashWant ? 1 : 0)),
		      strf("sheet: Continue with These -- \"custom\", usage stats %s, crash reports %s", usageWant ? "on" : "off",
		           crashOk ? (crashWant ? "on" : "off") : "not in this build"));
		check(iniHas("privacyChoice=custom") && iniHas(std::string("usageStats=") + (usageWant ? "true" : "false")), "sheet: ...saved in native.ini");

		// ---- See exactly what's sent: opens over the sheet, Done closes it.
		notAsked();
		privacy::presentSheet(w);
		pump(0.6);
		if (privacy::askWindow()) bringToFront(privacy::askWindow());
		g_sentSeen = false;
		SetTimer(nullptr, 0, 250, closeSent);
		const bool sentClick = clickControl(privacy::CtlSeeSent, why);
		pump(1.5);
		check(sentClick && g_sentSeen && privacy::whatsSentWindow() == nullptr && privacy::sheetOpen(),
		      "sheet: See exactly what's sent opens (\"" + g_sentRows + "\") and Done closes it, the sheet still up " + why);
		json::Value preview;
		const bool parsed = json::parse(usagestats::preview(), preview);
		const json::Value* app = parsed ? preview.get("app") : nullptr;
		check(parsed && preview.integer("v") == 1 && app && app->str("platform") == "windows" && !preview.str("settingsHash").empty(),
		      "what's sent: the real report, as JSON (v 1, platform windows, a settings hash)");
		if (privacy::askWindow()) bringToFront(privacy::askWindow());
		pressKey(VK_ESCAPE);
		pump(0.6);
		check(!privacy::sheetOpen() && prefs().privacyAsked, "sheet: and Escape answers it");
	});
	privacy::quiet = true;
	privacy::assumeFront = false;
	if (privacy::sheetOpen()) privacy::closeSheet();
	w->destroy();
	pump(0.2);
	return finish("privacy check");
}

}  // namespace privacycheck
