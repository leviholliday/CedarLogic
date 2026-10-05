// The sync engine around the algorithm (SYNC.md 4.3, 4.12, 6.2): one thread,
// the triggers and backoff, the UI thread through Host::onMain, the questions,
// the state file and the secret, the quitting push, the status sentences.

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS   // snprintf/sscanf/localtime/getenv are used with care
#endif

#include "SyncInternal.h"

#include <atomic>
#include <climits>
#include <condition_variable>
#include <deque>
#include <thread>

namespace clsync {

// ---- when to sync (§4.3) -------------------------------------------------------------------------

void Scheduler::started(int64_t now) {
	jitterSeed ^= (uint32_t)now ^ (uint32_t)(now >> 32);   // devices don't all back off in step
	if (!jitterSeed) jitterSeed = 2463534242u;
	startAt = now + 2 * kSecond;
	lastInput = now;
}

void Scheduler::syncNow(int64_t) {
	nowPending = true;
	flushPending = true;
}

void Scheduler::libraryChanged(int64_t now) {
	if (!firstChange) firstChange = now;
	lastChange = now;
	lastInput = now;
}

void Scheduler::activated(int64_t now) {
	active = true;
	lastInput = now;
	if (now - lastAttempt > 60 * kSecond) nowPending = true;
}

void Scheduler::deactivated(int64_t, bool pending) {
	active = false;
	if (pending || firstChange) {
		nowPending = true;
		flushPending = true;
	}
}

void Scheduler::input(int64_t now) { lastInput = now; }

bool Scheduler::due(int64_t now, bool& flush, int64_t lazySince) {
	flush = false;
	if (now < retryAfterUntil) return false;
	if (nowPending) {
		flush = flushPending;
		return true;
	}
	if (halted) return false;   // only Sync Now (or a restart) tries again
	if (now < backoffUntil) return false;
	if (retryPending) return true;
	if (startAt && now >= startAt) return true;
	if (firstChange && now >= std::min(lastChange + 5 * kSecond, firstChange + 60 * kSecond)) return true;
	if (heldRetryAt && now >= heldRetryAt) return true;
	if (lazySince) {
		if (now - lazySince >= 10 * kMinute) return true;
		if (now - lastInput >= 2 * kMinute && lastAttempt < lastInput + 2 * kMinute) {   // 2 minutes without input
			flush = true;
			return true;
		}
	}
	if ((active || now - lastInput < 10 * kMinute) && lastAttempt && now >= lastAttempt + pollMs) return true;
	return false;
}

int64_t Scheduler::nextWake(int64_t now, int64_t lazySince) const {
	int64_t t = INT64_MAX;
	auto at = [&](int64_t x) {
		if (x > 0) t = std::min(t, x);
	};
	const int64_t gate = std::max(retryAfterUntil, now);
	if (nowPending) return std::max(gate, now);
	if (halted) return INT64_MAX;
	const int64_t floor = std::max(gate, backoffUntil);
	if (retryPending) at(floor);
	at(startAt);
	if (firstChange) at(std::min(lastChange + 5 * kSecond, firstChange + 60 * kSecond));
	at(heldRetryAt);
	if (lazySince) {
		at(lazySince + 10 * kMinute);
		at(lastInput + 2 * kMinute);
	}
	if (lastAttempt) at(lastAttempt + pollMs);
	if (t == INT64_MAX) return t;
	return std::max(t, floor);
}

void Scheduler::cycleDone(int64_t now, const std::string& status, int64_t retryAfterMs, int64_t pollSeconds, bool held) {
	lastAttempt = now;
	startAt = 0;
	nowPending = flushPending = false;
	pollMs = std::max<int64_t>(60, pollSeconds) * kSecond;
	heldRetryAt = held ? now + 5 * kSecond : 0;
	if (lastChange <= cycleStart) firstChange = lastChange = 0;
	halted = status == "halted";
	if (status == "synced" || status == "full") {
		failures = 0;
		backoffUntil = retryAfterUntil = 0;
		retryPending = false;
		return;
	}
	if (status == "gone" || status == "off" || status == "stopped" || status == "halted") {
		retryPending = false;
		return;
	}
	if (status == "locked") {   // another CedarLogic: look again in a minute
		backoffUntil = now + 60 * kSecond;
		retryPending = true;
		return;
	}
	// offline, error, busy: back off (30 s, 1, 2, 5, 10, then every 15 min, +-20 %).
	static const int64_t steps[] = { 30, 60, 120, 300, 600, 900 };
	const int64_t base = steps[std::min(failures, 5)] * kSecond;
	failures++;
	jitterSeed ^= jitterSeed << 13;
	jitterSeed ^= jitterSeed >> 17;
	jitterSeed ^= jitterSeed << 5;
	const int64_t jitter = (int64_t)(jitterSeed % 41) - 20;   // -20 .. +20 %
	backoffUntil = now + base + base * jitter / 100;
	if (status == "busy") retryAfterUntil = now + std::max<int64_t>(retryAfterMs, 0);
	retryPending = true;
}

// ---- the engine ---------------------------------------------------------------------------------------

namespace {

struct Waiter {
	std::mutex mu;
	std::condition_variable cv;
	bool done = false, value = false;
	void set(bool v = true) {
		std::lock_guard<std::mutex> lock(mu);
		value = v;
		done = true;
		cv.notify_all();
	}
	// Waits until set, `ms` pass (< 0: no limit), or `stop` turns true (checked every 100 ms).
	bool wait(int64_t ms, const std::function<bool()>& stop) {
		std::unique_lock<std::mutex> lock(mu);
		const auto start = std::chrono::steady_clock::now();
		while (!done && !stop()) {
			if (ms >= 0 && std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(ms)) break;
			cv.wait_for(lock, std::chrono::milliseconds(100));
		}
		return done;
	}
};

}  // namespace

namespace {
std::atomic<int> gDetached{ 0 };

// The pairing sentences (SYNC.md 11.6, 11.10).
const char* const kPairCantReach = "Can't reach the website. Check the connection, then try again.";
const char* const kPairExpired = "This QR code expired.";
const char* const kPairDamaged = "An answer came that couldn't be read.";
const char* const kPairSyncOn = "Sync is already on.";
const char* const kPairNoRandom = "Couldn't make a QR code: this computer's random number generator didn't answer.";

using SteadyTime = std::chrono::steady_clock::time_point;
SteadyTime steadyNow() { return std::chrono::steady_clock::now(); }
}  // namespace

int detachedEngines() { return gDetached.load(); }

struct Engine::Impl {
	Config cfg;
	Crypto& crypto;
	Host& host;
	SystemClock clock;
	std::unique_ptr<Backend> lib;
	std::unique_ptr<Core> core;

	std::mutex mu;
	std::condition_variable cv;
	std::thread th;
	bool threadRunning = false, threadExited = false, stopping = false, lockHeld = false, started = false;
	bool orphaned = false;   // the Engine is gone and the thread was let go: it must not touch the host again (under mu)
	std::atomic<bool> stopped{ false }, noMain{ false };
	std::atomic<int64_t> lazySincePub{ 0 };   // the engine's lazySince, for appDeactivated
	std::deque<std::function<void()>> ops;
	Scheduler sched;

	// Published for the UI thread (under mu).
	Status pub;
	bool pubEnabled = false;
	std::string pubCode, pubDeviceName, pubStatus = "off", pubDetail;
	std::vector<std::pair<std::string, int64_t>> pubDevices;
	std::vector<std::string> pendingNotices;

	// Pairing, D's side (SYNC.md 11.6), under mu. Every start and cancel moves pairGen on; a step
	// or a callback that finds it moved does nothing more.
	struct PairJob {
		bool active = false, put = false;   // put: the website has the slot
		PairSlot slot;
		SteadyTime started, next;
		std::function<void(const std::string&)> show;
		std::function<void(int, const std::string&, const std::string&)> done;
	};
	PairJob pair;
	uint64_t pairGen = 0;
	std::vector<std::pair<std::string, std::string>> pairDeletes;   // (pairId, read token) to DELETE

	Impl(Config c, Crypto& cr, Host& h) : cfg(std::move(c)), crypto(cr), host(h) {
		FileLibraryOptions o;
		o.root = cfg.libraryRoot;
		o.clock = &clock;
		o.crypto = &crypto;
		o.structureHash = [this](const std::string& cdl) { return core->structureHash(cdl); };
		lib = makeFileLibrary(o);
		CoreOptions co;
		co.serverBase = cfg.serverBase;
		co.appKey = cfg.appKey;
		co.client = cfg.client;
		co.gateDefaults = cfg.gateDefault;
		CoreHooks hk;
		hk.http = [this](const HttpRequest& r) { return host.http(r); };
		hk.onMain = [this](const std::function<void()>& fn) { runMain(fn); };
		hk.notice = [this](const std::string& t) {
			std::lock_guard<std::mutex> lock(mu);
			pendingNotices.push_back(t);
		};
		hk.askMassDelete = [this](int n) { return ask(n, std::string(), true); };
		hk.askIncomingDeletes = [this](int n, const std::string& devices) { return ask(n, devices, false); };
		hk.held = [this](const std::string& local, bool runtimeOnly) {
			const WindowState w = host.windowState(local);
			return w.open && (w.dirty || (runtimeOnly && clock.now() - w.lastInputAt < 60 * kSecond));
		};
		hk.replaced = [this](const std::string& local, const std::string& device) { host.circuitReplaced(local, device); };
		hk.closing = [this](const std::string& local) { host.closeCircuit(local); };
		hk.libraryChanged = [this] {
			if (!noMain) host.libraryChanged();
		};
		hk.progress = [this](const std::string& kind, int done, int total) {
			std::string text;
			if (kind == "bringing") text = "Bringing in " + std::to_string(done) + " of " + std::to_string(total) + " circuits\xE2\x80\xA6";
			else if (kind == "sending") text = total == 1 ? "Sending 1 circuit\xE2\x80\xA6" : "Sending " + std::to_string(total) + " circuits\xE2\x80\xA6";
			publish("syncing", text, done, total);
		};
		hk.saveState = [this](const State& s) { return saveState(s); };
		core.reset(new Core(co, crypto, clock, *lib, hk));
		core->setDeviceName(cfg.defaultDeviceName.empty() ? "CedarLogic" : cfg.defaultDeviceName);
	}

	std::string statePath() const { return files::join(cfg.syncDir, "state.json"); }

	bool saveState(const State& s) {
		if (!files::makePrivateDir(cfg.syncDir)) return false;
		return files::writeAtomic(statePath(), json::write(s.toJson()) + "\n");
	}

	// Runs fn on the UI thread; Stopped if that can't happen any more.
	void runMain(const std::function<void()>& fn) {
		if (noMain.load() || stopped.load()) throw Stopped();
		std::exception_ptr ex;
		bool ran = false;
		host.onMain([&] {
			if (stopped.load()) return;   // stop() came first (it runs on this thread too)
			ran = true;
			try {
				fn();
			} catch (...) {
				ex = std::current_exception();
			}
			deliverNotices();
		});
		if (ex) std::rethrow_exception(ex);
		if (!ran) throw Stopped();
	}

	// UI thread.
	void deliverNotices() {
		std::vector<std::string> n;
		{
			std::lock_guard<std::mutex> lock(mu);
			n.swap(pendingNotices);
		}
		for (const std::string& t : n) host.notice(t);
	}

	bool ask(int count, const std::string& devices, bool local) {
		auto w = std::make_shared<Waiter>();
		runMain([&] {
			if (local) host.askMassDelete(count, [w](bool yes) { w->set(yes); });
			else host.askIncomingDeletes(count, devices, [w](bool yes) { w->set(yes); });
		});
		// The person may take their time; stop() or quitting abandons the cycle (the cursor held).
		if (!w->wait(-1, [this] { return stopped.load() || noMain.load(); }) || noMain.load()) throw Stopped();
		return w->value;
	}

	// `onUi`: the caller is the UI thread already (load()), so the host is told directly --
	// asking onMain from there would wait for a thread that is waiting for us.
	void publish(const std::string& status, const std::string& detail, int done = 0, int total = 0, bool onUi = false) {
		{
			std::lock_guard<std::mutex> lock(mu);
			pubStatus = status;
			pubDetail = detail;
			pubEnabled = core->enabled();
			pubCode = core->code();
			pubDeviceName = core->deviceName();
			pubDevices = core->deviceList();
			pub.progressDone = done;
			pub.progressTotal = total;
			pub.lastSyncAt = core->state().lastSyncAt;
			pub.circuits = core->circuitCount();
			pub.problems.assign(core->problems().begin(), core->problems().end());
		}
		if (noMain.load() || stopped.load()) return;
		const Status s = statusNow();
		if (onUi) {
			host.statusChanged(s);
			return;
		}
		try {
			runMain([&] { host.statusChanged(s); });
		} catch (const Stopped&) {
		}
	}

	Status statusNow() {
		std::lock_guard<std::mutex> lock(mu);
		Status s = pub;
		const std::string st = pubStatus;
		s.kind = st == "synced" ? Status::Synced
		       : st == "syncing" ? Status::Syncing
		       : st == "offline" ? Status::Offline
		       : st == "error" ? Status::Error
		       : st == "full" ? Status::Full
		       : st == "gone" ? Status::Gone
		       : st == "busy" || st == "locked" ? Status::Busy
		                                          : Status::Off;
		if (!pubEnabled && s.kind != Status::Gone) s.kind = Status::Off;
		s.text = s.kind == Status::Off ? std::string() : statusSentence(st, pubDetail, s.lastSyncAt, clock.now());
		if (st == "error" || st == "offline") {
			const int64_t wait = sched.retryAt() - clock.now();
			if (st == "error" && wait > 0) {
				const int64_t min = (wait + 59999) / 60000;
				s.text += min <= 1 ? " Trying again in a minute." : " Trying again in " + std::to_string(min) + " minutes.";
			}
		}
		return s;
	}

	void publishCore() {
		const std::string st = core->status();
		std::string detail = core->statusText();
		if (st == "gone") detail = core->goneReason();
		publish(st == "stopped" ? std::string(core->enabled() ? "synced" : "off") : st, detail);
	}

	void enqueue(std::function<void()> op, bool front = false) {
		{
			std::lock_guard<std::mutex> lock(mu);
			if (front) ops.push_front(std::move(op));
			else ops.push_back(std::move(op));
		}
		cv.notify_all();
	}

	// UI thread: done callbacks.
	void finish(std::function<void(bool, std::string)> done, bool ok, const std::string& message) {
		if (!done) return;
		try {
			runMain([&] { done(ok, message); });
		} catch (const Stopped&) {
		}
	}

	bool takeLock() {
		if (lockHeld) return true;
		files::makePrivateDir(cfg.syncDir);
		lockHeld = host.tryLock(files::join(cfg.syncDir, "lock"));
		return lockHeld;
	}

	void runCycle(bool flush) {
		const int64_t t = clock.now();
		{
			std::lock_guard<std::mutex> lock(mu);
			sched.cycleStarted(t);
		}
		if (!takeLock()) {
			{
				std::lock_guard<std::mutex> lock(mu);
				sched.cycleDone(t, "locked", 0, core->pollSeconds(), false);
			}
			publish("locked", std::string());
			return;
		}
		publish("syncing", std::string());
		// §4.12 step 1: every open circuit saved first (asynchronously on the Mac), up to 10 s.
		try {
			auto w = std::make_shared<Waiter>();
			runMain([&] { host.flushOpen([w] { w->set(); }); });
			w->wait(10 * kSecond, [this] { return stopped.load(); });
		} catch (const Stopped&) {
			return;
		}
		core->sync(flush);
		lazySincePub = core->state().lazySince;
		{
			std::lock_guard<std::mutex> lock(mu);
			sched.cycleDone(clock.now(), core->halted() ? "halted" : core->status(), core->retryAfterMs(), core->pollSeconds(),
			                core->heldLastPull());
		}
		publishCore();
		if (!noMain.load()) {
			try {
				runMain([] {});
			} catch (const Stopped&) {
			}
		}
	}

	// ---- pairing (SYNC.md 11.6) ----

	PairServer pairServer() {
		PairServer s;
		s.serverBase = cfg.serverBase;
		s.appKey = cfg.appKey;
		s.client = cfg.client;
		s.http = [this](const HttpRequest& r) { return host.http(r); };
		return s;
	}

	// Under mu: the current pairing ends without done; its slot is deleted by the engine thread.
	void cancelPairLocked() {
		pairGen++;
		if (pair.active && pair.put) pairDeletes.emplace_back(pair.slot.keys.pairId, pair.slot.readToken);
		pair = PairJob();
	}

	// Engine thread.
	void runPairDeletes() {
		std::vector<std::pair<std::string, std::string>> dels;
		{
			std::lock_guard<std::mutex> lock(mu);
			dels.swap(pairDeletes);
		}
		if (dels.empty()) return;
		const PairServer srv = pairServer();
		for (const auto& d : dels) pairDelete(srv, d.first, d.second);
	}

	// Engine thread: done on the UI thread, unless this pairing was cancelled or replaced
	// (checked there too: cancel runs on the UI thread, so it comes wholly before or after).
	void endPair(uint64_t gen, int result, const std::string& text, const std::string& from) {
		std::function<void(int, const std::string&, const std::string&)> done;
		{
			std::lock_guard<std::mutex> lock(mu);
			if (pairGen != gen || !pair.active) return;
			done = std::move(pair.done);
			pair = PairJob();
		}
		try {
			runMain([&] {
				{
					std::lock_guard<std::mutex> lock(mu);
					if (pairGen != gen) return;
				}
				if (done) done(result, text, from);
			});
		} catch (const Stopped&) {
		}
	}

	// Engine thread: the PUT, or one poll.
	void pairStep() {
		uint64_t gen;
		PairJob j;
		{
			std::lock_guard<std::mutex> lock(mu);
			if (!pair.active) return;
			gen = pairGen;
			j.put = pair.put;
			j.slot = pair.slot;
			j.started = pair.started;
			j.show = pair.show;
		}
		const PairServer srv = pairServer();
		if (!j.put) {
			if (core->enabled()) {
				endPair(gen, Engine::PairFailed, kPairSyncOn, std::string());
				return;
			}
			PairSlot slot;
			int st = 0;
			SteadyTime started = steadyNow();
			for (int attempt = 0; attempt < 3; attempt++) {   // 409: someone has this P (practically never): another
				started = steadyNow();
				st = pairPut(srv, crypto, core->deviceName(), slot);
				if (st != 409) break;
				std::lock_guard<std::mutex> lock(mu);
				if (pairGen != gen) return;
			}
			if (st != 201) {
				endPair(gen, Engine::PairFailed, st == -1 ? kPairNoRandom : kPairCantReach, std::string());
				return;
			}
			{
				std::lock_guard<std::mutex> lock(mu);
				if (pairGen != gen || !pair.active) {   // cancelled while the PUT was out
					pairDeletes.emplace_back(slot.keys.pairId, slot.readToken);
					return;
				}
				pair.put = true;
				pair.slot = slot;
				pair.started = started;
				pair.next = std::min(steadyNow() + std::chrono::milliseconds(pairPollMs()),
				                     started + std::chrono::milliseconds(pairLifeMs()));
			}
			try {
				runMain([&] {
					{
						std::lock_guard<std::mutex> lock(mu);
						if (pairGen != gen) return;
					}
					if (j.show) j.show(slot.link);
				});
			} catch (const Stopped&) {
			}
			return;
		}
		if (core->enabled()) {   // linked another way meanwhile (a typed code): stop quietly
			std::lock_guard<std::mutex> lock(mu);
			if (pairGen == gen) cancelPairLocked();
			return;
		}
		const auto life = std::chrono::milliseconds(pairLifeMs());
		if (steadyNow() - j.started >= life) {
			endPair(gen, Engine::PairExpired, kPairExpired, std::string());
			return;
		}
		std::string env;
		const int st = pairPoll(srv, j.slot, env);
		{
			std::lock_guard<std::mutex> lock(mu);
			if (pairGen != gen || !pair.active) return;   // cancelled meanwhile: the cancel deletes the slot
		}
		if (st == 404) {
			endPair(gen, Engine::PairExpired, kPairExpired, std::string());
			return;
		}
		if (st == 200 && !env.empty()) {
			PairMessage m;
			const bool ok = openPair(crypto, j.slot.keys, "answer", env, m);
			pairDelete(srv, j.slot.keys.pairId, j.slot.readToken);   // best effort: it expires anyway
			if (ok) endPair(gen, Engine::PairCode, m.code, pairDeviceText(m.device));
			else endPair(gen, Engine::PairFailed, kPairDamaged, std::string());
			return;
		}
		// No answer yet, or an error: the next tick (errors silently, until the 10 minutes are up).
		std::lock_guard<std::mutex> lock(mu);
		if (pairGen == gen && pair.active)
			pair.next = std::min(steadyNow() + std::chrono::milliseconds(pairPollMs()), j.started + life);
	}

	void loop() {
		for (;;) {
			std::function<void()> op;
			bool cycle = false, flush = false, deletes = false, pairDue = false;
			{
				std::unique_lock<std::mutex> lock(mu);
				for (;;) {
					if (stopping) break;
					if (!ops.empty()) {
						op = std::move(ops.front());
						ops.pop_front();
						break;
					}
					if (!pairDeletes.empty()) {
						deletes = true;
						break;
					}
					if (pair.active && !noMain.load() && steadyNow() >= pair.next) {
						pairDue = true;
						break;
					}
					const int64_t now = clock.now();
					const int64_t lazy = core->state().lazySince;
					if (core->enabled() && !noMain.load() && sched.due(now, flush, lazy)) {
						cycle = true;
						break;
					}
					int64_t wake = core->enabled() ? sched.nextWake(now, lazy) : INT64_MAX;
					int64_t ms = wake == INT64_MAX ? 60000 : std::max<int64_t>(50, std::min<int64_t>(60000, wake - now));
					if (pair.active) {
						const int64_t p =
							std::chrono::duration_cast<std::chrono::milliseconds>(pair.next - steadyNow()).count();
						ms = std::max<int64_t>(1, std::min(ms, p));
					}
					cv.wait_for(lock, std::chrono::milliseconds(ms));
				}
				if (stopping) break;
			}
			try {
				if (op) op();
				else if (deletes) runPairDeletes();
				else if (pairDue) pairStep();
				else if (cycle) runCycle(flush);
			} catch (const std::exception&) {
			}
		}
		{
			// Stopped or destroyed with a QR code showing: its slot goes too (best effort; after
			// quitting() it already went, and nothing more is sent).
			std::lock_guard<std::mutex> lock(mu);
			cancelPairLocked();
			if (noMain.load() || orphaned) pairDeletes.clear();
		}
		runPairDeletes();
		std::lock_guard<std::mutex> lock(mu);
		if (lockHeld && !orphaned) host.unlock();
		lockHeld = false;
		threadExited = true;
		cv.notify_all();
	}

	void startThread() {
		std::lock_guard<std::mutex> lock(mu);
		if (threadRunning) return;
		threadRunning = true;
		th = std::thread([this] { loop(); });
	}

	// Load the secret and the state (UI thread, before the thread runs).
	void load() {
		State s;
		std::string text;
		json::Value v;
		const bool haveState = files::read(statePath(), text) && json::parse(text, v) && State::fromJson(v, s);
		const std::string secret = host.loadSecret();
		std::string code, why;
		const bool haveCode = !secret.empty() && parseCode(crypto, secret, code, why);
		if (haveState) {
			core->adopt(s);
			lib->loadCache(s.hashCache);
		}
		if (core->deviceName().empty()) core->setDeviceName(cfg.defaultDeviceName.empty() ? "CedarLogic" : cfg.defaultDeviceName);
		if (!haveCode) {
			core->reset();
			if (core->deviceName().empty()) core->setDeviceName(cfg.defaultDeviceName);
			publish("off", std::string(), 0, 0, true);
			return;
		}
		const Keys k = keysForCode(crypto, code);
		if (!haveState || s.spaceId != k.spaceId) {
			core->reset();   // no state for this code (lost, or another code's): join again
			core->mutableState().joining = true;
		}
		if (core->deviceName().empty()) core->setDeviceName(cfg.defaultDeviceName.empty() ? "CedarLogic" : cfg.defaultDeviceName);
		core->setCode(code);
		if (!core->state().gone.empty()) {   // stopped from another device: kept that way until the person acts
			const std::string reason = core->state().gone;
			core->disable();
			std::lock_guard<std::mutex> lock(mu);
			pubStatus = "gone";
			pubDetail = reason;
			pubEnabled = false;
			return;
		}
		std::lock_guard<std::mutex> lock(mu);
		pubEnabled = true;
		pubCode = core->code();
		pubDeviceName = core->deviceName();
		pubDevices = core->deviceList();
		pubStatus = "synced";
		pub.lastSyncAt = core->state().lastSyncAt;
	}

	void forgetEverything() {
		host.forgetSecret();
		files::remove(statePath());
		core->disable();
		core->reset();
	}
};

Engine::Engine(Config c, Crypto& cr, Host& h) : d(new Impl(std::move(c), cr, h)) {}

Engine::~Engine() {
	if (!d) return;
	stop();
	bool exited = true;
	{
		std::unique_lock<std::mutex> lock(d->mu);
		if (d->threadRunning) {
			exited = d->cv.wait_for(lock, std::chrono::seconds(3), [this] { return d->threadExited; });
			if (!exited) d->orphaned = true;   // (the thread's last step, under the same lock, sees it)
		}
	}
	if (d->threadRunning) {
		if (exited) {
			d->th.join();
		} else {
			// The thread is still inside a host call (an onMain this thread can't serve now, or
			// the network): it may not outlive what it points at, so it keeps its Impl.
			d->th.detach();
			d.release();
			gDetached++;
		}
	}
}

void Engine::start() {
	if (d->started) return;
	d->started = true;
	d->load();
	{
		std::lock_guard<std::mutex> lock(d->mu);
		d->sched.started(d->clock.now());
	}
	d->startThread();
}

void Engine::stop() {
	d->stopped = true;
	{
		std::lock_guard<std::mutex> lock(d->mu);
		d->stopping = true;
	}
	d->cv.notify_all();
}

bool Engine::enabled() const {
	std::lock_guard<std::mutex> lock(d->mu);
	return d->pubEnabled;
}

std::string Engine::code() const {
	std::lock_guard<std::mutex> lock(d->mu);
	return d->pubEnabled ? d->pubCode : std::string();
}

std::string Engine::deviceName() const {
	std::lock_guard<std::mutex> lock(d->mu);
	return d->pubDeviceName.empty() ? d->cfg.defaultDeviceName : d->pubDeviceName;
}

void Engine::setDeviceName(const std::string& name0) {
	std::string name = trimAscii(name0);
	if (scalarCount(name) > 64) {
		size_t count = 0, i = 0;
		for (; i < name.size(); i++)
			if (((unsigned char)name[i] & 0xC0) != 0x80 && count++ == 64) break;
		name = name.substr(0, i);
	}
	if (name.empty()) return;
	{
		std::lock_guard<std::mutex> lock(d->mu);
		d->pubDeviceName = name;
	}
	Impl* p = d.get();
	d->enqueue([p, name] {
		p->core->setDeviceName(name);
		if (p->core->enabled()) {
			std::lock_guard<std::mutex> lock(p->mu);
			p->sched.libraryChanged(p->clock.now());   // the device record is due: sync soon
		}
		if (p->core->enabled()) p->saveState(p->core->state());
	});
}

Status Engine::status() const { return d->statusNow(); }

std::vector<std::pair<std::string, int64_t>> Engine::devices() const {
	std::lock_guard<std::mutex> lock(d->mu);
	return d->pubDevices;
}

void Engine::turnOn(std::function<void(bool, std::string)> done) {
	Impl* p = d.get();
	d->startThread();
	d->enqueue([p, done] {
		if (!p->takeLock()) {
			p->finish(done, false, "Syncing in another CedarLogic window.");
			return;
		}
		std::string message;
		if (!p->core->turnOn(message)) {
			p->finish(done, false, message);
			return;
		}
		if (!p->host.saveSecret(p->core->code())) {
			p->core->disable();
			p->core->reset();
			p->finish(done, false, "The sync code couldn't be saved on this computer.");
			return;
		}
		{
			std::lock_guard<std::mutex> lock(p->mu);
			p->sched.syncNow(p->clock.now());
		}
		p->publish("synced", std::string());
		p->finish(done, true, std::string());
	});
}

void Engine::preview(const std::string& code, std::function<void(bool, std::string, Preview)> done) {
	Impl* p = d.get();
	d->startThread();
	d->enqueue([p, code, done] {
		Preview pv;
		std::string message;
		const int st = p->core->preview(code, pv, message);
		if (!done) return;
		try {
			p->runMain([&] { done(st == 200, st == 200 ? pv.sentence : message, pv); });
		} catch (const Stopped&) {
		}
	});
}

void Engine::link(const std::string& code, std::function<void(bool, std::string)> done) {
	Impl* p = d.get();
	d->startThread();
	d->enqueue([p, code, done] {
		std::string canonical, why;
		if (!parseCode(p->crypto, code, canonical, why)) {
			p->finish(done, false, whyText(why, code));
			return;
		}
		if (p->core->enabled() && p->core->code() == canonical) {
			p->finish(done, true, std::string());
			return;
		}
		if (!p->takeLock()) {
			p->finish(done, false, "Syncing in another CedarLogic window.");
			return;
		}
		std::string message;
		if (p->core->enabled()) {   // switching codes: this one leaves the other synced copy first
			if (!p->core->link(canonical, message, false)) {   // (but not for a code that doesn't work)
				p->finish(done, false, message);
				return;
			}
			const std::string name = p->core->deviceName();
			p->core->turnOff(false);
			p->core->setDeviceName(name);
		}
		if (!p->core->link(canonical, message)) {
			p->finish(done, false, message);
			return;
		}
		if (!p->host.saveSecret(canonical)) {
			p->core->disable();
			p->finish(done, false, "The sync code couldn't be saved on this computer.");
			return;
		}
		{
			std::lock_guard<std::mutex> lock(p->mu);
			p->sched.syncNow(p->clock.now());
		}
		p->publish("syncing", std::string());
		p->finish(done, true, std::string());
	});
}

void Engine::turnOff(bool removeSyncedCircuits) {
	Impl* p = d.get();
	{
		std::lock_guard<std::mutex> lock(d->mu);
		d->pubEnabled = false;
		d->pubStatus = "off";
	}
	d->startThread();
	d->enqueue([p, removeSyncedCircuits] {
		const std::string name = p->core->deviceName();
		try {
			p->core->turnOff(removeSyncedCircuits);
		} catch (const std::exception&) {
		}
		p->forgetEverything();
		p->core->setDeviceName(name);
		p->publish("off", std::string());
	});
}

void Engine::deleteSyncedCopy(std::function<void(bool, std::string)> done) {
	Impl* p = d.get();
	d->startThread();
	d->enqueue([p, done] {
		std::string message;
		const std::string name = p->core->deviceName();
		const int st = p->core->deleteSyncedCopy(message);
		if (st != 200) {
			p->finish(done, false, message);
			return;
		}
		p->forgetEverything();
		p->core->setDeviceName(name);
		p->publish("off", std::string());
		p->finish(done, true, std::string());
	});
}

void Engine::startOver(std::function<void(bool, std::string)> done) {
	Engine* self = this;
	deleteSyncedCopy([self, done](bool ok, std::string message) {
		if (!ok) {
			if (done) done(false, message);
			return;
		}
		self->turnOn(done);
	});
}

void Engine::syncNow() {
	std::lock_guard<std::mutex> lock(d->mu);
	d->sched.syncNow(d->clock.now());
	d->cv.notify_all();
}

void Engine::noteLibraryChanged() {
	std::lock_guard<std::mutex> lock(d->mu);
	d->sched.libraryChanged(d->clock.now());
	d->cv.notify_all();
}

void Engine::appActivated() {
	std::lock_guard<std::mutex> lock(d->mu);
	d->sched.activated(d->clock.now());
	d->cv.notify_all();
}

void Engine::appDeactivated() {
	std::lock_guard<std::mutex> lock(d->mu);
	d->sched.deactivated(d->clock.now(), d->lazySincePub.load() != 0);
	d->cv.notify_all();
}

void Engine::userActive() {
	std::lock_guard<std::mutex> lock(d->mu);
	d->sched.input(d->clock.now());
}

void Engine::quitting(std::function<void()> done) {
	struct Once {
		std::atomic<bool> fired{ false };
		std::function<void()> fn;
		void fire() {
			if (!fired.exchange(true) && fn) fn();
		}
	};
	auto once = std::make_shared<Once>();
	once->fn = std::move(done);
	d->noMain = true;   // from now on nothing waits for the UI thread
	{
		std::lock_guard<std::mutex> lock(d->mu);
		d->cancelPairLocked();   // a QR code showing: its slot is deleted below (best effort)
		if (!d->threadRunning || d->stopping) {
			once->fire();
			return;
		}
	}
	Impl* p = d.get();
	d->enqueue(
		[p, once] {
			if (p->core->enabled() && p->takeLock()) p->core->syncPushOnly();
			p->runPairDeletes();
			once->fire();
		},
		true);
	// However far it got, `done` comes within 5 s.
	std::thread([once] {
		std::this_thread::sleep_for(std::chrono::seconds(5));
		once->fire();
	}).detach();
}

void Engine::pairStart(std::function<void(const std::string&)> show,
                       std::function<void(int, const std::string&, const std::string&)> done) {
	d->startThread();
	{
		std::lock_guard<std::mutex> lock(d->mu);
		d->cancelPairLocked();   // a pairing already under way ends without done
		d->pair.active = true;
		d->pair.next = steadyNow();
		d->pair.show = std::move(show);
		d->pair.done = std::move(done);
	}
	d->cv.notify_all();
}

void Engine::pairCancel() {
	{
		std::lock_guard<std::mutex> lock(d->mu);
		d->cancelPairLocked();
	}
	d->cv.notify_all();
}

}  // namespace clsync
