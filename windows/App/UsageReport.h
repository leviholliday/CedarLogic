// Anonymous usage stats (docs/PRIVACY.md), the part that needs no window:
// the Mac's mac/App/UsageReport.swift, for Windows. --usage-check
// (PrivacyCheck.h) runs it against mac/Tools/usage-check/server.py, a
// stand-in for cedarlogic.netlify.app's /api/usage:
//
//   POST   /api/usage          a report (JSON, below) -> {"ok": true, "haveSnapshot": bool}
//   DELETE /api/usage?id=<id>  everything for that id -> {"ok": true}
//
// A report is built only from the allowlist here (table()) and the counters
// (Counters): never free text, never a name, a path, a code or a time more
// exact than the minute (UTC). The first report after the switch goes on is
// a "snapshot" (every setting that isn't its default); after that a "delta"
// (only the settings that changed since the last report the server
// confirmed, and the counters since then), or a "ping" when nothing changed
// and nothing was counted. haveSnapshot false makes the next one a snapshot
// again. A report that couldn't be sent is kept (7 at most) and tried again
// at the next chance; a 429 waits 24 hours.
//
// What's counted, the random id and the reports still to send live in a file
// of their own, %APPDATA%\CedarLogic\Usage\usage.json, that only this
// Windows user can open (its folder too), written every 5 minutes and at
// quit. The switch itself is in native.ini (prefs().usageStats). The id is
// made when the switch goes on and forgotten (the server told to delete
// it) when it goes off.
//
// Thread-safe: the app sends from a thread of its own (UsageStats.h); the
// network is never waited for with the lock held.

#ifndef CL_WINDOWS_USAGE_REPORT_H
#define CL_WINDOWS_USAGE_REPORT_H

#include "App.h"

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

struct Prefs;

namespace usage {

// ---- Values ----------------------------------------------------------------------

// A setting's value as it's sent: a bool, a number or one of a few fixed words.
struct Value {
	enum Kind { Bool, Int, Double, String } kind = Bool;
	bool b = false;
	long long i = 0;
	double d = 0;
	std::string s;
	static Value boolean(bool v) { Value x; x.kind = Bool; x.b = v; return x; }
	static Value integer(long long v) { Value x; x.kind = Int; x.i = v; return x; }
	static Value number(double v) { Value x; x.kind = Double; x.d = v; return x; }
	static Value word(const std::string& v) { Value x; x.kind = String; x.s = v; return x; }
	bool operator==(const Value& o) const;
	bool operator!=(const Value& o) const { return !(*this == o); }
};
using Settings = std::map<std::string, Value>;

// JSON written the same way every time: keys sorted, numbers as JavaScript
// writes them (1, not 1.0; 0.18, not 0.17999999999999999), no spaces. The
// settings hash is SHA-256 over this form of the settings, so the server can
// make the same one (JSON.stringify of the object with its keys sorted).
struct Json {
	enum Kind { Null, Bool, Int, Double, String, Object } kind = Null;
	bool b = false;
	long long i = 0;
	double d = 0;
	std::string s;
	std::map<std::string, Json> o;
	static Json boolean(bool v) { Json x; x.kind = Bool; x.b = v; return x; }
	static Json integer(long long v) { Json x; x.kind = Int; x.i = v; return x; }
	static Json number(double v) { Json x; x.kind = Double; x.d = v; return x; }
	static Json string(const std::string& v) { Json x; x.kind = String; x.s = v; return x; }
	static Json object() { Json x; x.kind = Object; return x; }
	Json& set(const std::string& k, Json v) { kind = Object; return o[k] = std::move(v); }
	std::string text(bool pretty = false, const std::string& indent = std::string()) const;
};
std::string jsNumber(double d);
std::string quoted(const std::string& s);
Json toJson(const Value& v);
Json toJson(const Settings& s);

// ---- The allowlist ------------------------------------------------------------------

// One setting that may be sent: the name it's sent under (the Mac's, where
// the setting is the same, so the site's admin page lines the platforms up),
// its default, for a word the only words it can be, and how it's read.
struct SettingDef {
	const char* name;
	Value def;
	std::vector<std::string> words;
	std::function<Value(const Prefs&)> read;
};
// Every setting a report can carry. Nothing else in the settings is read:
// not your name, your circuits, folders, sync codes, window places, recent
// files or shortcuts. (The names avoid the words the server drops as
// possibly personal -- name, key, code, id, path... -- so "gateLabels",
// not "showGateNames".)
const std::vector<SettingDef>& table();
// A setting's value now, made safe: a number clamped (and a fraction rounded
// to 2 places), a word from its list or "other".
Value valueOf(const SettingDef& s, const Prefs& p);
// Every allowlisted setting that differs from its default.
Settings nonDefault(const Prefs& p);
const Value* defaultValue(const std::string& name);
// SHA-256 (hex) of the canonical JSON of the full non-default settings.
std::string hash(const Settings& s);
std::string sha256Hex(const std::string& bytes);

// ---- What's counted -------------------------------------------------------------------

struct Counters {
	int sessions = 0, activeSeconds = 0, focusSeconds = 0, simViewSeconds = 0, classroomSessions = 0;
	std::map<std::string, int> actions;
	static const int maxActions = 200;
	bool isZero() const;
	// An action's id, as the registry names it (letters, digits and _):
	// anything else is never counted.
	static bool isActionId(const std::string& s);
	void count(const std::string& action);
};

// A report waiting to go (or kept after a failure).
struct Pending {
	std::string kind, from, to;
	bool hasSettings = false;   // what it sends (snapshot: all of `full`; delta: what changed; ping: none)
	Settings settings;
	Settings full;              // every non-default setting when it was made: what the server has once it's confirmed
	std::string hash;
	bool hasCounters = false;
	Counters counters;
	bool syncOn = false;
	int tries = 0;
	void makeSnapshot();
};

// The local file: the id, counters since the last report, what the server
// last confirmed, and reports still to send.
struct State {
	std::string id;
	Counters counters;
	std::string periodFrom;
	bool hasConfirmed = false;
	Settings confirmed;
	std::string confirmedHash;
	bool needSnapshot = true;
	long long lastSentAt = 0, lastBuiltAt = 0, retryAfter = 0;   // Unix seconds; 0: never
	std::string lastSentKind;
	std::vector<Pending> queue;
	std::vector<std::string> pendingDeletes;   // ids the server still has to be told to forget
};

// What a report says about the app (no more).
struct AppInfo {
	std::string platform = "windows";
	std::string version;
	long long build = 0;
	std::string os, arch, lang;
	static AppInfo current();
	Json json() const;
};

// The app as it is now, for a report: its settings (nonDefault, read on the
// window thread) and whether sync is on.
struct Now {
	Settings settings;
	bool syncOn = false;
};

// ---- The engine ----------------------------------------------------------------------

class Engine {
public:
	static const int maxKept = 7;
	static const int maxBytes = 16 * 1024;
	static const long long day = 86400;

	explicit Engine(const std::string& directory);

	std::function<long long()> now;            // Unix seconds (the checks' own clock)
	std::function<std::string()> endpoint;     // .../api/usage; "": nowhere (endpointUrl)
	std::function<bool()> policyOff;           // DisableUsageStats
	AppInfo app = AppInfo::current();

	// On and off. setSwitch: as native.ini has it, at launch. set: the switch
	// changed (the consent, Settings > Privacy): on makes a fresh id (and the
	// next report a snapshot); off forgets it, and what was counted, and
	// queues the server's DELETE (flushDeletes sends it).
	void setSwitch(bool on);
	bool switchedOn() const;
	void set(bool on);
	// Counting and sending: the switch is on, no policy says no, there's an id.
	bool enabled() const;
	std::string id() const;
	static bool isUsageId(const std::string& s);
	// Random, from nothing about this PC: 32 hex digits.
	static std::string newId();

	// Settings > Privacy > Delete What's Been Sent: the server forgets this
	// id; a new one (and a snapshot) follows if the switch stays on. False:
	// the server couldn't be reached (it's asked again at the next chance).
	bool deleteSent();
	// DELETE for each id the server still has to forget.
	void flushDeletes();

	void noteSession();
	void noteClassroom();
	void count(const std::string& action);
	void addTime(int active, int focus, int simView);
	Counters counters() const;
	long long lastSentAt() const;
	std::string lastSentKind() const;
	int keptCount() const;
	std::vector<std::string> pendingDeletes() const;
	int lastHaveSnapshot() const { return haveSnapshot_; }   // -1: none yet
	std::vector<std::string> log() const;

	static std::string date(long long t);     // "2026-11-04" (UTC)
	static std::string minute(long long t);   // "2026-11-04T18:05Z"

	// The report the next send would make from what's counted now (it's
	// not queued; the counters stay).
	Pending nextReport(const Now& n) const;
	Json body(const Pending& p, const std::string& id, long long sentAt) const;
	// The bytes that go: at most 16 KB (the least-used actions go first if it's more).
	std::string encoded(const Pending& p, const std::string& id, long long sentAt) const;
	// "See exactly what's sent": the next report, pretty-printed, as it would
	// go now (before the switch is on, with a stand-in id).
	std::string preview(const Now& n) const;

	// At launch, and now and then after: a report if one's due (24 h since
	// the last one went or was made), then whatever's kept, oldest first.
	// Blocks on the network: the app calls it from a thread of its own.
	bool sendIfDue(const Now& n, bool force = false);

	void load();
	// Written whole (a temporary, then moved over it); only this user can open it.
	void save();
	std::string file() const { return file_; }
	std::string directory() const { return dir_; }

private:
	enum class Answer { Ok, TooMany, Refused, Failed };
	Answer post(const std::string& body, const std::string& api, bool& haveSnapshot, int& status);
	void enqueueLocked(const Pending& p);
	Pending nextReportLocked(const Now& n) const;
	void resetLocked();
	void saveLocked();

	mutable std::recursive_mutex m_;
	std::string dir_, file_;
	State state_;
	bool switched_ = false;
	std::atomic<bool> sending_{ false };
	std::atomic<int> haveSnapshot_{ -1 };
	std::vector<std::string> log_;
};

// https://cedarlogic.netlify.app/api/usage. CL_USAGE_URL may point it at a
// stand-in on this PC (127.0.0.1, localhost) or, over HTTPS, the site or one
// of its Netlify drafts (<name>--cedarlogic.netlify.app), and nowhere else;
// with or without the /api/usage. A test or picture run, CI, a run with
// CEDARLOGIC_HOME or any CL_ variable sends nowhere unless it's pointed at one.
std::string endpointUrl(bool testRun);

// HKLM\Software\Policies\CedarLogic (or HKCU's), a DWORD of 1: a lab's switch.
bool policySet(const wchar_t* name);

// ---- The leak scan -------------------------------------------------------------------

// Every string from `needles` (4 characters or more, any case) found in
// `report` that isn't simply one of the words a report is made of.
std::vector<std::string> leaks(const std::string& report, const std::vector<std::string>& needles);
// The strings that would mean a leak on this PC: the profile folder (as it
// is, with / and as JSON writes it) and its name, the user name, the
// computer's names.
std::vector<std::string> machineNeedles();

}  // namespace usage

#endif  // CL_WINDOWS_USAGE_REPORT_H
