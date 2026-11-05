// Anonymous usage stats: the engine (see UsageReport.h).

#include "UsageReport.h"
#include "Intro.h"
#include "SyncJson.h"
#include "SyncPlatform.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <random>
#include <sstream>

#ifndef CL_BUILD_NUMBER
#define CL_BUILD_NUMBER 0
#endif

namespace usage {

namespace json = clsync::json;

namespace {
std::string lowerCase(std::string s) {
	for (char& c : s) c = (char)tolower((unsigned char)c);
	return s;
}
std::string readText(const std::string& path, size_t limit = 64 * 1024) {
	gchar* text = nullptr;
	gsize n = 0;
	std::string out;
	if (g_file_get_contents(path.c_str(), &text, &n, nullptr)) out.assign(text, std::min<size_t>(n, limit));
	g_free(text);
	return out;
}
}  // namespace

// ---- Values ----------------------------------------------------------------------

bool Value::operator==(const Value& o) const {
	if (kind != o.kind) return false;
	switch (kind) {
	case Bool: return b == o.b;
	case Int: return i == o.i;
	case Double: return d == o.d;
	default: return s == o.s;
	}
}

std::string jsNumber(double d) {
	if (!std::isfinite(d)) return "0";
	if (d == std::floor(d) && std::fabs(d) < 1e15) return std::to_string((long long)d);
	// The shortest that reads back the same, as JavaScript writes it.
	char buf[64] = "";
	for (int p = 1; p <= 17; p++) {
		snprintf(buf, sizeof buf, "%.*g", p, d);
		if (strtod(buf, nullptr) == d) break;
	}
	return buf;
}

std::string quoted(const std::string& s) {
	std::string out = "\"";
	for (unsigned char c : s) {
		switch (c) {
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if (c < 0x20) out += format("\\u%04x", c);
			else out += (char)c;
		}
	}
	return out + "\"";
}

std::string Json::text(bool pretty, const std::string& indent) const {
	switch (kind) {
	case Null: return "null";
	case Bool: return b ? "true" : "false";
	case Int: return std::to_string(i);
	case Double: return jsNumber(d);
	case String: return quoted(s);
	default: break;
	}
	if (o.empty()) return "{}";
	std::string out = "{";
	const std::string inner = indent + "  ";
	bool first = true;
	for (const auto& kv : o) {   // (a std::map: sorted, by bytes, as JavaScript sorts ASCII keys)
		if (!first) out += ",";
		first = false;
		if (pretty) out += "\n" + inner;
		out += quoted(kv.first) + (pretty ? ": " : ":") + kv.second.text(pretty, inner);
	}
	if (pretty) out += "\n" + indent;
	return out + "}";
}

Json toJson(const Value& v) {
	switch (v.kind) {
	case Value::Bool: return Json::boolean(v.b);
	case Value::Int: return Json::integer(v.i);
	case Value::Double: return Json::number(v.d);
	default: return Json::string(v.s);
	}
}

Json toJson(const Settings& s) {
	Json o = Json::object();
	for (const auto& kv : s) o.o[kv.first] = toJson(kv.second);
	return o;
}

// ---- The allowlist ------------------------------------------------------------------

namespace {

Value B(bool v) { return Value::boolean(v); }
Value I(long long v) { return Value::integer(v); }

// The template a new circuit starts from: a built-in one's id says which;
// one of yours is just "yours" (its id or name never goes).
Value templateValue(const std::string& t) {
	if (t.empty()) return Value::word("");
	bool builtIn = t.compare(0, 8, "builtin-") == 0 && t.size() <= 32;
	for (unsigned char c : t) builtIn = builtIn && c < 0x80 && (isalnum(c) || c == '-');
	return Value::word(builtIn ? t : "yours");
}

}  // namespace

const std::vector<SettingDef>& table() {
	static const std::vector<SettingDef> t = {
		// Appearance
		{ "themeMode", I(0), {}, [](const Prefs& p) { return I(p.themeMode); } },   // 0 the desktop's, 1 light, 2 dark, 3 last time
		{ "accent", I(6), {}, [](const Prefs& p) { return I(p.accent); } },
		{ "classicTabs", B(false), {}, [](const Prefs& p) { return B(p.classicTabs); } },
		{ "showGrid", B(true), {}, [](const Prefs& p) { return B(p.showGrid); } },
		{ "gridStyle", I(0), {}, [](const Prefs& p) { return I(p.gridStyle); } },
		{ "majorGrid", B(true), {}, [](const Prefs& p) { return B(p.majorGrid); } },
		{ "wireThickness", I(1), {}, [](const Prefs& p) { return I(p.wireThickness); } },
		{ "lowWire", I(0), {}, [](const Prefs& p) { return I(p.lowWire); } },
		{ "wireDots", B(true), {}, [](const Prefs& p) { return B(p.wireDots); } },
		{ "wireDotSize", Value::number(0.18), {}, [](const Prefs& p) { return Value::number(p.wireDotSize); } },
		{ "gateSize", I(48), {}, [](const Prefs& p) { return I(p.gateSize); } },
		{ "gateLabels", B(true), {}, [](const Prefs& p) { return B(p.showGateNames); } },
		{ "categoryShortcuts", B(true), {}, [](const Prefs& p) { return B(p.showCategoryKeys); } },
		{ "showPalette", B(true), {}, [](const Prefs& p) { return B(p.showPalette); } },   // View > Side Panel
		// Toolbar and window
		{ "toolbarStyle", I(3), {}, [](const Prefs& p) { return I(p.toolbarStyle); } },
		{ "toolbarHidden", I(0), {}, [](const Prefs& p) { return I(p.toolbarHidden); } },   // which tool groups are hidden (bits)
		{ "showTitle", B(true), {}, [](const Prefs& p) { return B(p.showTitle); } },
		{ "showStepClock", B(true), {}, [](const Prefs& p) { return B(p.showStepClock); } },
		{ "showThemeToggle", B(false), {}, [](const Prefs& p) { return B(p.showThemeToggle); } },   // (off to begin with on Linux)
		{ "showStatus", B(true), {}, [](const Prefs& p) { return B(p.showStatus); } },
		{ "actionsOn", B(true), {}, [](const Prefs& p) { return B(p.actionsOn); } },
		{ "actionsCollapsed", B(false), {}, [](const Prefs& p) { return B(p.actionsCollapsed); } },
		{ "actionsHoverOpen", B(true), {}, [](const Prefs& p) { return B(p.actionsHoverOpen); } },
		// Canvas and editing
		{ "mouseWheel", I(0), {}, [](const Prefs& p) { return I(p.mouseWheel); } },
		{ "trackpadScroll", I(1), {}, [](const Prefs& p) { return I(p.touchpadScroll); } },   // a touchpad's two-finger scroll
		{ "reverseWheel", B(false), {}, [](const Prefs& p) { return B(p.reverseWheel); } },
		{ "reverseTrackpad", B(false), {}, [](const Prefs& p) { return B(p.reverseTouchpad); } },
		{ "rightClickRotate", B(false), {}, [](const Prefs& p) { return B(p.rightClickRotate); } },
		{ "duplicateClipboard", B(false), {}, [](const Prefs& p) { return B(p.duplicateUsesClipboard); } },
		{ "wireValueTag", B(false), {}, [](const Prefs& p) { return B(p.wireValueTag); } },
		{ "tidyMode", I(0), {}, [](const Prefs& p) { return I(p.tidyMode); } },
		// Circuits and pages
		{ "openReplaces", B(true), {}, [](const Prefs& p) { return B(p.openReplaces); } },
		{ "newTemplate", Value::word(""), {}, [](const Prefs& p) { return templateValue(p.newTemplate); } },
		{ "newPagesShareLinks", B(true), {}, [](const Prefs& p) { return B(p.newPagesShareLinks); } },
		// Export, simulation, the app
		{ "exportInfo", B(true), {}, [](const Prefs& p) { return B(p.exportInfo); } },
		{ "timingColor", B(false), {}, [](const Prefs& p) { return B(p.timingInColor); } },
		{ "projector", B(false), {}, [](const Prefs& p) { return B(p.projector); } },
		{ "confirmQuit", B(true), {}, [](const Prefs& p) { return B(p.confirmQuit); } },
		{ "atLaunch", Value::word("short"), { "short", "full", "classic", "none" },   // When CedarLogic opens
		  [](const Prefs& p) { return Value::word(intro::atLaunchKey(p.introAtLaunch)); } },
		{ "testingGroup", Value::word("normal"), { "normal", "beta" }, [](const Prefs& p) { return Value::word(p.updateChannel == 1 ? "beta" : "normal"); } },
		{ "checkUpdates", B(true), {}, [](const Prefs& p) { return B(p.checkUpdates); } },   // Settings > General > Updates
	};
	return t;
}

Value valueOf(const SettingDef& s, const Prefs& p) {
	Value v = s.read(p);
	if (v.kind != s.def.kind) return s.def;
	switch (v.kind) {
	case Value::Int: v.i = std::max<long long>(-1000000, std::min<long long>(1000000, v.i)); break;
	case Value::Double:
		if (!std::isfinite(v.d)) return s.def;
		v.d = std::round(v.d * 100) / 100;
		break;
	case Value::String:
		if (s.name == std::string("newTemplate")) break;   // (made safe as it's read)
		if (std::find(s.words.begin(), s.words.end(), v.s) == s.words.end()) v.s = "other";
		break;
	default: break;
	}
	return v;
}

Settings nonDefault(const Prefs& p) {
	Settings out;
	for (const SettingDef& s : table()) {
		const Value v = valueOf(s, p);
		if (v != s.def) out[s.name] = v;
	}
	return out;
}

const Value* defaultValue(const std::string& name) {
	for (const SettingDef& s : table())
		if (name == s.name) return &s.def;
	return nullptr;
}

std::string sha256Hex(const std::string& bytes) {
	uint8_t digest[32] = {};
	syncplatform::crypto().sha256((const uint8_t*)bytes.data(), bytes.size(), digest);
	std::string hex;
	for (unsigned char c : digest) hex += format("%02x", c);
	return hex;
}

std::string hash(const Settings& s) { return sha256Hex(toJson(s).text()); }

// ---- What's counted -------------------------------------------------------------------

bool Counters::isZero() const {
	if (sessions || activeSeconds || focusSeconds || simViewSeconds || classroomSessions) return false;
	for (const auto& kv : actions)
		if (kv.second) return false;
	return true;
}

bool Counters::isActionId(const std::string& s) {
	if (s.empty() || s.size() > 40) return false;
	for (unsigned char c : s)
		if (c >= 0x80 || !(isalnum(c) || c == '_')) return false;
	return true;
}

void Counters::count(const std::string& action) {
	if (!isActionId(action)) return;
	if (actions.find(action) == actions.end() && (int)actions.size() >= maxActions) return;
	actions[action]++;
}

void Pending::makeSnapshot() {
	kind = "snapshot";
	hasSettings = true;
	settings = full;
	hasCounters = true;
}

// ---- The app ---------------------------------------------------------------------------

// The distribution and its major version, from /etc/os-release (or
// /usr/lib/os-release): its ID and VERSION_ID up to the first dot --
// "debian 12", "ubuntu 24", "raspbian 12", "fedora 41"; a rolling one (Arch)
// just its ID. Letters, digits, "." "_" "-" only, 16 characters at most;
// "linux" when there's nothing to go on.
std::string osName(const std::string& osRelease) {
	auto field = [&](const char* key) {
		const std::string want = std::string(key) + "=";
		size_t at = 0;
		while (at < osRelease.size()) {
			size_t end = osRelease.find('\n', at);
			if (end == std::string::npos) end = osRelease.size();
			std::string line = osRelease.substr(at, end - at);
			at = end + 1;
			if (line.compare(0, want.size(), want) != 0) continue;
			std::string v = line.substr(want.size());
			if (v.size() >= 2 && (v[0] == '"' || v[0] == '\'') && v.back() == v[0]) v = v.substr(1, v.size() - 2);
			return v;
		}
		return std::string();
	};
	std::string id;
	for (char c : lowerCase(field("ID")))
		if (isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-') id += c;
	if (id.size() > 10) id.resize(10);
	if (id.empty() || !isalnum((unsigned char)id[0])) return "linux";
	std::string major;
	for (char c : field("VERSION_ID")) {
		if (!isdigit((unsigned char)c)) break;
		major += c;
	}
	if (major.size() > 5) major.resize(5);
	return major.empty() ? id : id + " " + major;
}

AppInfo AppInfo::current() {
	AppInfo a;
	std::string v = CL_VERSION;
	bool plain = !v.empty() && v.size() <= 16;
	for (char c : v) plain = plain && (isdigit((unsigned char)c) || c == '.');
	a.version = plain ? v : "0";
	a.build = (long long)CL_BUILD_NUMBER;
	std::string rel = readText("/etc/os-release");
	if (rel.empty()) rel = readText("/usr/lib/os-release");
	a.os = osName(rel);
#if defined(__aarch64__) || defined(__arm64__)
	a.arch = "aarch64";
#else
	a.arch = "x86_64";
#endif
	// The language only ("en"), never the region: the first of the user's
	// languages (LANGUAGE, LC_ALL, LC_MESSAGES, LANG) that is one.
	std::string lang;
	for (const gchar* const* l = g_get_language_names(); l && *l && lang.empty(); l++) {
		std::string n = *l;
		const size_t cut = n.find_first_of("_-.@");
		if (cut != std::string::npos) n.resize(cut);
		bool letters = n.size() >= 2 && n.size() <= 3;
		for (char c : n) letters = letters && isalpha((unsigned char)c) && (unsigned char)c < 0x80;
		if (letters) lang = lowerCase(n);
	}
	a.lang = lang.empty() ? "und" : lang;
	return a;
}

Json AppInfo::json() const {
	Json o = Json::object();
	o.set("platform", Json::string(platform));
	o.set("version", Json::string(version));
	o.set("build", Json::integer(build));
	o.set("os", Json::string(os));
	o.set("arch", Json::string(arch));
	o.set("lang", Json::string(lang));
	return o;
}

// ---- The file --------------------------------------------------------------------------

namespace {

json::Value toValue(const Settings& s) {
	json::Value o = json::Value::object();
	for (const auto& kv : s) {
		const Value& v = kv.second;
		o.set(kv.first, v.kind == Value::Bool ? json::Value::boolean(v.b)
		              : v.kind == Value::Int ? json::Value::integer(v.i)
		              : v.kind == Value::Double ? json::Value::number(v.d) : json::Value::string(v.s));
	}
	return o;
}

// The kinds come back as the allowlist has them (a number for a fraction
// setting stays a fraction, 2 not 2.0).
Settings settingsFrom(const json::Value* o) {
	Settings out;
	if (o == nullptr || !o->isObject()) return out;
	for (const auto& kv : o->o) {
		const json::Value& v = kv.second;
		const Value* def = defaultValue(kv.first);
		if (v.isBool()) out[kv.first] = Value::boolean(v.b);
		else if (v.isString()) out[kv.first] = Value::word(v.s);
		else if (v.isNumber()) {
			if (def && def->kind == Value::Double) out[kv.first] = Value::number(v.n);
			else out[kv.first] = Value::integer((long long)std::llround(v.n));
		}
	}
	return out;
}

json::Value toValue(const Counters& c) {
	json::Value o = json::Value::object();
	o.set("sessions", json::Value::integer(c.sessions));
	o.set("activeSeconds", json::Value::integer(c.activeSeconds));
	o.set("focusSeconds", json::Value::integer(c.focusSeconds));
	o.set("simViewSeconds", json::Value::integer(c.simViewSeconds));
	o.set("classroomSessions", json::Value::integer(c.classroomSessions));
	json::Value a = json::Value::object();
	for (const auto& kv : c.actions) a.set(kv.first, json::Value::integer(kv.second));
	o.set("actions", a);
	return o;
}

Counters countersFrom(const json::Value* o) {
	Counters c;
	if (o == nullptr || !o->isObject()) return c;
	auto n = [&](const char* k) { return (int)std::max<int64_t>(0, std::min<int64_t>(1000000000, o->integer(k, 0))); };
	c.sessions = n("sessions");
	c.activeSeconds = n("activeSeconds");
	c.focusSeconds = n("focusSeconds");
	c.simViewSeconds = n("simViewSeconds");
	c.classroomSessions = n("classroomSessions");
	if (const json::Value* a = o->get("actions"))
		if (a->isObject())
			for (const auto& kv : a->o)
				if (Counters::isActionId(kv.first) && kv.second.isNumber() && (int)c.actions.size() < Counters::maxActions)
					c.actions[kv.first] = (int)std::max<int64_t>(0, kv.second.i());
	return c;
}

json::Value toValue(const Pending& p) {
	json::Value o = json::Value::object();
	o.set("kind", json::Value::string(p.kind));
	o.set("from", json::Value::string(p.from));
	o.set("to", json::Value::string(p.to));
	if (p.hasSettings) o.set("settings", toValue(p.settings));
	o.set("full", toValue(p.full));
	o.set("hash", json::Value::string(p.hash));
	if (p.hasCounters) o.set("counters", toValue(p.counters));
	o.set("syncOn", json::Value::boolean(p.syncOn));
	o.set("tries", json::Value::integer(p.tries));
	return o;
}

bool pendingFrom(const json::Value& o, Pending& p) {
	if (!o.isObject()) return false;
	p.kind = o.str("kind");
	if (p.kind != "snapshot" && p.kind != "delta" && p.kind != "ping") return false;
	p.from = o.str("from");
	p.to = o.str("to");
	p.hasSettings = o.get("settings") != nullptr;
	p.settings = settingsFrom(o.get("settings"));
	p.full = settingsFrom(o.get("full"));
	p.hash = o.str("hash");
	p.hasCounters = o.get("counters") != nullptr;
	p.counters = countersFrom(o.get("counters"));
	p.syncOn = o.flag("syncOn");
	p.tries = (int)o.integer("tries", 0);
	return true;
}

}  // namespace

Engine::Engine(const std::string& directory) : dir_(directory), file_(directory + "/usage.json") {
	now = [] { return (long long)time(nullptr); };
	endpoint = [] { return std::string(); };
	policyOff = [] { return false; };
	load();
}

void Engine::load() {
	std::lock_guard<std::recursive_mutex> g(m_);
	const std::string text = readText(file_, 1024 * 1024);
	if (text.empty()) return;
	json::Value v;
	if (!json::parse(text, v) || !v.isObject()) return;
	State s;
	s.id = v.str("id");
	if (!isUsageId(s.id)) s.id.clear();
	s.counters = countersFrom(v.get("counters"));
	s.periodFrom = v.str("periodFrom");
	s.hasConfirmed = v.get("confirmed") != nullptr;
	s.confirmed = settingsFrom(v.get("confirmed"));
	s.confirmedHash = v.str("confirmedHash");
	s.needSnapshot = v.flag("needSnapshot", true);
	s.lastSentAt = v.integer("lastSentAt", 0);
	s.lastBuiltAt = v.integer("lastBuiltAt", 0);
	s.retryAfter = v.integer("retryAfter", 0);
	s.lastSentKind = v.str("lastSentKind");
	if (const json::Value* q = v.get("queue"))
		if (q->isArray())
			for (const json::Value& e : q->a) {
				Pending p;
				if (pendingFrom(e, p) && (int)s.queue.size() < maxKept) s.queue.push_back(p);
			}
	if (const json::Value* d = v.get("pendingDeletes"))
		if (d->isArray())
			for (const json::Value& e : d->a)
				if (e.isString() && isUsageId(e.s)) s.pendingDeletes.push_back(e.s);
	state_ = s;
}

void Engine::save() {
	std::lock_guard<std::recursive_mutex> g(m_);
	saveLocked();
}

void Engine::saveLocked() {
	json::Value v = json::Value::object();
	v.set("id", json::Value::string(state_.id));
	v.set("counters", toValue(state_.counters));
	v.set("periodFrom", json::Value::string(state_.periodFrom));
	if (state_.hasConfirmed) v.set("confirmed", toValue(state_.confirmed));
	v.set("confirmedHash", json::Value::string(state_.confirmedHash));
	v.set("needSnapshot", json::Value::boolean(state_.needSnapshot));
	v.set("lastSentAt", json::Value::integer(state_.lastSentAt));
	v.set("lastBuiltAt", json::Value::integer(state_.lastBuiltAt));
	v.set("retryAfter", json::Value::integer(state_.retryAfter));
	v.set("lastSentKind", json::Value::string(state_.lastSentKind));
	json::Value q = json::Value::array();
	for (const Pending& p : state_.queue) q.push(toValue(p));
	v.set("queue", q);
	json::Value d = json::Value::array();
	for (const std::string& id : state_.pendingDeletes) d.push(json::Value::string(id));
	v.set("pendingDeletes", d);
	const std::string text = json::write(v);
	// The folder, only this user's; then the file the same (0600), written
	// whole to a temporary beside it and moved over the old one.
	g_mkdir_with_parents(dir_.c_str(), 0700);
	const std::string tmp = dir_ + "/.usage.json.tmp";
	const int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0) return;
	fchmod(fd, 0600);
	size_t done = 0;
	while (done < text.size()) {
		const ssize_t n = write(fd, text.data() + done, text.size() - done);
		if (n <= 0) break;
		done += (size_t)n;
	}
	const bool ok = done == text.size() && fsync(fd) == 0;
	close(fd);
	if (ok) rename(tmp.c_str(), file_.c_str());
	else unlink(tmp.c_str());
}

// ---- On and off ---------------------------------------------------------------------

void Engine::setSwitch(bool on) {
	std::lock_guard<std::recursive_mutex> g(m_);
	switched_ = on;
}

bool Engine::switchedOn() const {
	std::lock_guard<std::recursive_mutex> g(m_);
	return switched_;
}

bool Engine::isUsageId(const std::string& s) {
	if (s.size() < 22 || s.size() > 64) return false;
	for (unsigned char c : s)
		if (c >= 0x80 || !(isalnum(c) || c == '_' || c == '-')) return false;
	return true;
}

std::string Engine::newId() {
	uint8_t bytes[16] = {};
	if (!syncplatform::crypto().random(bytes, sizeof bytes)) {
		std::random_device rd;
		for (uint8_t& b : bytes) b = (uint8_t)rd();
	}
	std::string hex;
	for (uint8_t b : bytes) hex += format("%02x", b);
	return hex;
}

std::string Engine::id() const {
	std::lock_guard<std::recursive_mutex> g(m_);
	return state_.id;
}

bool Engine::enabled() const {
	std::lock_guard<std::recursive_mutex> g(m_);
	return switched_ && !(policyOff && policyOff()) && isUsageId(state_.id);
}

void Engine::resetLocked() {
	const std::vector<std::string> deletes = state_.pendingDeletes;
	state_ = State();
	state_.pendingDeletes = deletes;
}

void Engine::set(bool on) {
	std::lock_guard<std::recursive_mutex> g(m_);
	if (on) {
		if (switched_ && isUsageId(state_.id)) return;
		switched_ = true;
		resetLocked();
		state_.id = newId();
		state_.periodFrom = date(now());
	} else {
		switched_ = false;
		const std::string old = state_.id;
		resetLocked();
		if (isUsageId(old) && std::find(state_.pendingDeletes.begin(), state_.pendingDeletes.end(), old) == state_.pendingDeletes.end())
			state_.pendingDeletes.push_back(old);
	}
	saveLocked();
}

bool Engine::deleteSent() {
	std::string old;
	{
		std::lock_guard<std::recursive_mutex> g(m_);
		old = state_.id;
		if (!isUsageId(old)) return true;
		if (std::find(state_.pendingDeletes.begin(), state_.pendingDeletes.end(), old) == state_.pendingDeletes.end())
			state_.pendingDeletes.push_back(old);
		saveLocked();
	}
	flushDeletes();
	std::lock_guard<std::recursive_mutex> g(m_);
	const bool done = std::find(state_.pendingDeletes.begin(), state_.pendingDeletes.end(), old) == state_.pendingDeletes.end();
	if (done && switched_ && state_.id == old) {
		resetLocked();
		state_.id = newId();
		state_.periodFrom = date(now());
		saveLocked();
	}
	return done;
}

void Engine::flushDeletes() {
	std::vector<std::string> ids;
	{
		std::lock_guard<std::recursive_mutex> g(m_);
		ids = state_.pendingDeletes;
	}
	const std::string base = endpoint ? endpoint() : std::string();
	if (ids.empty() || base.empty()) return;
	for (const std::string& old : ids) {
		clsync::HttpRequest r;
		r.method = "DELETE";
		r.url = base + "?id=" + old;
		const clsync::HttpResponse a = syncplatform::http(r, 30);
		std::lock_guard<std::recursive_mutex> g(m_);
		if (a.status == 200 || a.status == 404) {
			state_.pendingDeletes.erase(std::remove(state_.pendingDeletes.begin(), state_.pendingDeletes.end(), old),
			                            state_.pendingDeletes.end());
			log_.push_back("deleted");
		} else {
			log_.push_back(format("delete kept: %d", a.status));
		}
	}
	std::lock_guard<std::recursive_mutex> g(m_);
	saveLocked();
}

// ---- Counting ---------------------------------------------------------------------------

void Engine::noteSession() {
	std::lock_guard<std::recursive_mutex> g(m_);
	if (enabled()) state_.counters.sessions++;
}
void Engine::noteClassroom() {
	std::lock_guard<std::recursive_mutex> g(m_);
	if (enabled()) state_.counters.classroomSessions++;
}
void Engine::count(const std::string& action) {
	std::lock_guard<std::recursive_mutex> g(m_);
	if (enabled()) state_.counters.count(action);
}
void Engine::addTime(int active, int focus, int simView) {
	std::lock_guard<std::recursive_mutex> g(m_);
	if (!enabled()) return;
	state_.counters.activeSeconds += active;
	state_.counters.focusSeconds += focus;
	state_.counters.simViewSeconds += simView;
}
Counters Engine::counters() const {
	std::lock_guard<std::recursive_mutex> g(m_);
	return state_.counters;
}
long long Engine::lastSentAt() const {
	std::lock_guard<std::recursive_mutex> g(m_);
	return state_.lastSentAt;
}
std::string Engine::lastSentKind() const {
	std::lock_guard<std::recursive_mutex> g(m_);
	return state_.lastSentKind;
}
int Engine::keptCount() const {
	std::lock_guard<std::recursive_mutex> g(m_);
	return (int)state_.queue.size();
}
std::vector<std::string> Engine::pendingDeletes() const {
	std::lock_guard<std::recursive_mutex> g(m_);
	return state_.pendingDeletes;
}
std::vector<std::string> Engine::log() const {
	std::lock_guard<std::recursive_mutex> g(m_);
	return log_;
}

// ---- Reports ------------------------------------------------------------------------------

namespace {
std::string utc(long long t, const char* format) {
	const time_t tt = (time_t)t;
	struct tm u = {};
	gmtime_r(&tt, &u);
	char buf[40] = "";
	strftime(buf, sizeof buf, format, &u);
	return buf;
}
}  // namespace

std::string Engine::date(long long t) { return utc(t, "%Y-%m-%d"); }
std::string Engine::minute(long long t) { return utc(t, "%Y-%m-%dT%H:%MZ"); }

Pending Engine::nextReport(const Now& n) const {
	std::lock_guard<std::recursive_mutex> g(m_);
	return nextReportLocked(n);
}

Pending Engine::nextReportLocked(const Now& n) const {
	Pending p;
	p.full = n.settings;
	p.hash = hash(p.full);
	p.from = state_.periodFrom.empty() ? date(now()) : state_.periodFrom;
	p.to = date(now());
	p.syncOn = n.syncOn;
	if (p.from > p.to) p.from = p.to;   // (a clock put back)
	if (state_.needSnapshot || !state_.hasConfirmed) {
		p.kind = "snapshot";
		p.hasSettings = true;
		p.settings = p.full;
		p.hasCounters = true;
		p.counters = state_.counters;
		return p;
	}
	Settings changed;
	for (const auto& kv : p.full) {
		auto it = state_.confirmed.find(kv.first);
		if (it == state_.confirmed.end() || it->second != kv.second) changed[kv.first] = kv.second;
	}
	// Back to its default: the default is what it is now.
	for (const auto& kv : state_.confirmed)
		if (p.full.find(kv.first) == p.full.end())
			if (const Value* d = defaultValue(kv.first)) changed[kv.first] = *d;
	if (changed.empty() && state_.counters.isZero()) {
		p.kind = "ping";
		return p;
	}
	p.kind = "delta";
	p.hasSettings = true;
	p.settings = changed;
	p.hasCounters = true;
	p.counters = state_.counters;
	return p;
}

Json Engine::body(const Pending& p, const std::string& id, long long sentAt) const {
	Json o = Json::object();
	o.set("v", Json::integer(1));
	o.set("id", Json::string(id));
	o.set("app", app.json());
	o.set("kind", Json::string(p.kind));
	o.set("sentAt", Json::string(minute(sentAt)));
	Json period = Json::object();
	period.set("from", Json::string(p.from));
	period.set("to", Json::string(p.to));
	o.set("period", period);
	o.set("settingsHash", Json::string(p.hash));
	if (p.kind != "ping" && p.hasSettings) o.set("settings", toJson(p.settings));
	if (p.hasCounters && !(p.kind == "ping" && p.counters.isZero())) {
		const Counters& c = p.counters;
		Json k = Json::object();
		k.set("sessions", Json::integer(c.sessions));
		k.set("activeSeconds", Json::integer(c.activeSeconds));
		k.set("focusSeconds", Json::integer(c.focusSeconds));
		k.set("simViewSeconds", Json::integer(c.simViewSeconds));
		k.set("classroomSessions", Json::integer(c.classroomSessions));
		k.set("syncOn", Json::boolean(p.syncOn));
		Json a = Json::object();
		for (const auto& kv : c.actions) a.set(kv.first, Json::integer(kv.second));
		k.set("actions", a);
		o.set("counters", k);
	}
	return o;
}

std::string Engine::encoded(const Pending& p0, const std::string& id, long long sentAt) const {
	Pending p = p0;
	std::string text = body(p, id, sentAt).text();
	while ((int)text.size() > maxBytes && p.hasCounters && !p.counters.actions.empty()) {
		// The least used first (ties: the last by name).
		std::vector<std::pair<std::string, int>> v(p.counters.actions.begin(), p.counters.actions.end());
		std::sort(v.begin(), v.end(), [](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
			return a.second != b.second ? a.second < b.second : a.first > b.first;
		});
		const size_t drop = std::max<size_t>(1, v.size() / 4);
		for (size_t i = 0; i < drop && i < v.size(); i++) p.counters.actions.erase(v[i].first);
		text = body(p, id, sentAt).text();
	}
	return text;
}

std::string Engine::preview(const Now& n) const {
	std::lock_guard<std::recursive_mutex> g(m_);
	const std::string shown = isUsageId(state_.id) ? state_.id : "made-when-you-turn-this-on";
	return body(nextReportLocked(n), shown, now()).text(true);
}

// ---- Sending ----------------------------------------------------------------------------------

Engine::Answer Engine::post(const std::string& body, const std::string& api, bool& haveSnapshot, int& status) {
	clsync::HttpRequest r;
	r.method = "POST";
	r.url = api;
	// JSON, and nothing a browser would say (no Origin): the server takes
	// reports from the apps only. (curl, from its argv: no shell, no ~/.curlrc.)
	r.headers.push_back({ "Content-Type", "application/json" });
	r.body = body;
	const clsync::HttpResponse a = syncplatform::http(r, 30);
	status = a.status;
	haveSnapshot = true;
	if (a.status == 200) {
		json::Value v;
		if (json::parse(a.body, v) && v.isObject()) haveSnapshot = v.flag("haveSnapshot", true);
		return Answer::Ok;
	}
	if (a.status == 429) return Answer::TooMany;
	if (a.status >= 400 && a.status < 500 && a.status != 408) return Answer::Refused;
	return Answer::Failed;
}

void Engine::enqueueLocked(const Pending& p) {
	state_.queue.push_back(p);
	if ((int)state_.queue.size() > maxKept) state_.queue.erase(state_.queue.begin(), state_.queue.end() - maxKept);
	state_.counters = Counters();
	state_.periodFrom = date(now());
	saveLocked();
}

bool Engine::sendIfDue(const Now& n, bool force) {
	if (sending_.exchange(true)) return false;
	struct Done { std::atomic<bool>& f; ~Done() { f = false; } } done{ sending_ };
	flushDeletes();
	const std::string base = endpoint ? endpoint() : std::string();
	std::string id;
	{
		std::lock_guard<std::recursive_mutex> g(m_);
		if (!enabled() || base.empty()) return false;
		id = state_.id;
		const long long t = now();
		if (state_.retryAfter && t < state_.retryAfter) return false;
		state_.retryAfter = 0;
		const long long last = std::max(state_.lastSentAt, state_.lastBuiltAt);
		if (force || last == 0 || t - last >= day) {
			enqueueLocked(nextReportLocked(n));
			state_.lastBuiltAt = t;
			saveLocked();
		}
	}
	bool followedUp = false, sentAny = false;
	for (;;) {
		Pending head;
		std::string bytes;
		{
			std::lock_guard<std::recursive_mutex> g(m_);
			if (state_.queue.empty()) break;
			head = state_.queue.front();
			bytes = encoded(head, id, now());
		}
		bool have = true;
		int status = 0;
		const Answer answer = post(bytes, base, have, status);
		std::lock_guard<std::recursive_mutex> g(m_);
		// The switch went off (or the id changed) while it was going.
		if (state_.id != id || !enabled()) return sentAny;
		if (state_.queue.empty()) break;
		switch (answer) {
		case Answer::Ok:
			haveSnapshot_ = have ? 1 : 0;
			state_.queue.erase(state_.queue.begin());
			state_.hasConfirmed = true;
			state_.confirmed = head.full;
			state_.confirmedHash = head.hash;
			state_.lastSentAt = now();
			state_.lastSentKind = head.kind;
			if (head.kind == "snapshot") state_.needSnapshot = false;
			sentAny = true;
			log_.push_back("sent " + head.kind);
			if (!have) {
				// The server has nothing to apply changes to: a snapshot next.
				state_.needSnapshot = true;
				if (!state_.queue.empty()) state_.queue.front().makeSnapshot();
				else if (head.kind != "snapshot" && !followedUp) {
					followedUp = true;
					enqueueLocked(nextReportLocked(n));
				}
			}
			saveLocked();
			break;
		case Answer::TooMany:
			state_.retryAfter = now() + day;
			log_.push_back("kept: 429");
			saveLocked();
			return sentAny;
		case Answer::Refused:
			// The server won't take this one; tried three times, it goes.
			state_.queue.front().tries++;
			if (state_.queue.front().tries >= 3) state_.queue.erase(state_.queue.begin());
			log_.push_back(format("kept: %d", status));
			saveLocked();
			return sentAny;
		case Answer::Failed:
			state_.queue.front().tries++;
			log_.push_back(status ? format("kept: %d", status) : std::string("kept: no connection"));
			saveLocked();
			return sentAny;
		}
	}
	return sentAny;
}

// ---- Where it goes ------------------------------------------------------------------------------

namespace {

std::string envVar(const char* name) {
	const char* v = g_getenv(name);
	return v ? std::string(v) : std::string();
}

bool anyClVariable() {
	bool found = false;
	gchar** env = g_get_environ();
	for (gchar** p = env; p && *p; p++)
		if (strncmp(*p, "CL_", 3) == 0) found = true;
	g_strfreev(env);
	return found;
}

bool endsWith(const std::string& s, const std::string& end) { return s.size() >= end.size() && s.compare(s.size() - end.size(), end.size(), end) == 0; }

}  // namespace

std::string endpointUrl(bool testRun) {
	const std::string test = envVar("CL_USAGE_URL");
	if (!test.empty()) {
		gchar *scheme = nullptr, *host = nullptr, *path = nullptr;
		gint port = -1;
		if (!g_uri_split(test.c_str(), G_URI_FLAGS_NONE, &scheme, nullptr, &host, &port, &path, nullptr, nullptr, nullptr)) return std::string();
		const std::string sc = scheme ? lowerCase(scheme) : std::string(), hostText = host ? host : "";
		std::string p = path ? path : "";
		g_free(scheme);
		g_free(host);
		g_free(path);
		const std::string h = lowerCase(hostText);
		const bool http = sc == "http", https = sc == "https";
		const bool local = (h == "127.0.0.1" || h == "localhost" || h == "::1") && (http || https);
		const bool site = https && (h == "cedarlogic.netlify.app" || endsWith(h, "--cedarlogic.netlify.app"));
		if (!local && !site) return std::string();
		while (!p.empty() && p.back() == '/') p.pop_back();
		if (!endsWith(p, "/api/usage")) p += "/api/usage";
		std::string hostPart = hostText;
		if (hostPart.find(':') != std::string::npos) hostPart = "[" + hostPart + "]";
		const bool defaultPort = port < 0 || (https && port == 443) || (http && port == 80);
		return std::string(https ? "https://" : "http://") + hostPart + (defaultPort ? "" : format(":%d", port)) + p;
	}
	if (testRun || !envVar("CI").empty() || anyClVariable()) return std::string();
	return "https://cedarlogic.netlify.app/api/usage";
}

std::string policyFile() {
	const std::string other = envVar("CL_POLICY_FILE");
	return other.empty() ? std::string("/etc/cedarlogic/policy.ini") : other;
}

bool policySet(const char* name) {
	const std::string text = readText(policyFile());
	size_t at = 0;
	while (at < text.size()) {
		size_t end = text.find('\n', at);
		if (end == std::string::npos) end = text.size();
		std::string line = text.substr(at, end - at);
		at = end + 1;
		const size_t eq = line.find('=');
		if (eq == std::string::npos) continue;
		auto trim = [](std::string s) {
			while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
			size_t i = 0;
			while (i < s.size() && isspace((unsigned char)s[i])) i++;
			return s.substr(i);
		};
		if (lowerCase(trim(line.substr(0, eq))) != lowerCase(name)) continue;
		const std::string v = lowerCase(trim(line.substr(eq + 1)));
		if (v == "true" || v == "1" || v == "yes" || v == "on") return true;
	}
	return false;
}

// ---- The leak scan ---------------------------------------------------------------------------------

namespace {

// The words a report can hold whatever the computer: its keys, the setting names
// and words, the counters', the kinds. (A user called "show" would
// otherwise "leak" through showGrid.)
std::string vocabulary() {
	std::string words = "platform version build arch lang kind snapshot delta ping sentAt period from settings settingsHash counters "
	                    "sessions activeSeconds focusSeconds simViewSeconds classroomSessions syncOn actions true false x86_64 "
	                    "aarch64 linux made-when-you-turn-this-on yours other api/usage builtin-";
	for (const SettingDef& s : table()) {
		words += " ";
		words += s.name;
		for (const std::string& w : s.words) words += " " + w;
	}
	return lowerCase(words);
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
	if (from.empty()) return s;
	for (size_t at = 0; (at = s.find(from, at)) != std::string::npos; at += to.size()) s.replace(at, from.size(), to);
	return s;
}

}  // namespace

std::vector<std::string> leaks(const std::string& report, const std::vector<std::string>& needles) {
	const std::string text = lowerCase(report), words = vocabulary();
	std::vector<std::string> found;
	for (const std::string& raw : needles) {
		std::string n = raw;
		while (!n.empty() && isspace((unsigned char)n.back())) n.pop_back();
		while (!n.empty() && isspace((unsigned char)n.front())) n.erase(n.begin());
		n = lowerCase(n);
		if (n.size() < 4 || text.find(n) == std::string::npos || words.find(n) != std::string::npos) continue;
		if (std::find(found.begin(), found.end(), raw) == found.end()) found.push_back(raw);
	}
	std::sort(found.begin(), found.end());
	return found;
}

std::vector<std::string> machineNeedles() {
	std::vector<std::string> n;
	const std::string home = g_get_home_dir() ? g_get_home_dir() : "";
	if (!home.empty()) {
		n.push_back(home);
		n.push_back(replaceAll(home, "/", "\\/"));   // as some JSON writes it
		const size_t slash = home.find_last_of('/');
		if (slash != std::string::npos) n.push_back(home.substr(slash + 1));
	}
	if (g_get_user_name()) n.push_back(g_get_user_name());
	if (g_get_real_name() && strcmp(g_get_real_name(), "Unknown") != 0) {
		n.push_back(g_get_real_name());
		// Each part of it, too ("Levi", "Holliday").
		gchar** parts = g_strsplit(g_get_real_name(), " ", -1);
		for (gchar** p = parts; p && *p; p++) n.push_back(*p);
		g_strfreev(parts);
	}
	for (const char* v : { "USER", "LOGNAME", "HOSTNAME" }) n.push_back(envVar(v));
	if (g_get_host_name()) n.push_back(g_get_host_name());
	std::string etc = readText("/etc/hostname", 256);
	while (!etc.empty() && isspace((unsigned char)etc.back())) etc.pop_back();
	n.push_back(etc);
	struct utsname u;
	if (uname(&u) == 0) n.push_back(u.nodename);
	n.push_back(syncplatform::defaultDeviceName());   // the pretty hostname (/etc/machine-info)
	n.erase(std::remove_if(n.begin(), n.end(), [](const std::string& s) { return s.empty(); }), n.end());
	return n;
}

}  // namespace usage
