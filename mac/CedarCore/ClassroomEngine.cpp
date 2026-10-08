// clclass::Engine: the Client on its own thread (CLASSROOM.md 6.2, 4.8, 3.14). The
// UI thread posts operations and reads published copies of the lists; the
// engine thread runs one operation or one poll at a time, publishes, and
// reaches the UI only through Host::onMain. Each open class page (or live view)
// keeps the class's live connection (LiveLink, ClassroomLive.cpp) through the
// Host's socket hooks; while it is open it brings every change and the engine
// polls only every 5 minutes, in case. Without it (connecting, three tries
// without a hello, or no WebSockets on the platform) a student's pulse during a
// lecture and a teacher's predict answers are polls the server holds (up to
// holdSeconds, on a thread of their own, asked again at once), else the pulse
// every `p` seconds; outside a session 60 s (120 s after 10 minutes without
// change), nothing in the background or after two hours without input;
// failures back off as Sync's.

#include "ClassroomInternal.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace clclass {

namespace {

int64_t backoffMs(int failures, uint32_t& seed) {
	static const int64_t steps[] = { 30, 60, 120, 300, 600, 900 };
	const int64_t base = steps[std::max(0, std::min(failures, 6) - 1)] * kSecond;
	seed = seed * 1664525u + 1013904223u;
	const double jitter = 0.8 + 0.4 * ((seed >> 8) & 0xFFFF) / 65535.0;   // +-20 %
	return (int64_t)(base * jitter);
}

std::atomic<int> gDetached{ 0 };
std::atomic<int> gSocketIds{ 0 };   // one numbering for the process: engines may share a host's sockets

}  // namespace

int detachedEngines() { return gDetached.load(); }

struct Engine::Impl {
	Config cfg;
	Crypto& cr;
	Curve& curve;
	Host& host;
	clsync::SystemClock clock;
	std::unique_ptr<Client> client;

	std::mutex mu;
	std::condition_variable cv;
	std::thread th;
	bool running = false, exited = false, stopping = false, started = false, locked = false;
	std::atomic<bool> stopped{ false };
	struct Op { std::function<void()> fn; bool publish = true; };
	std::deque<Op> ops;
	std::vector<std::string> pendingNotices;
	int background = 0;                 // threads out in a held poll (under mu)

	// Published for the UI thread (under mu).
	std::vector<ClassInfo> pubClasses;
	std::map<std::string, std::vector<Assignment>> pubAssignments;
	std::map<std::string, std::vector<Student>> pubStudents;
	std::map<std::string, std::vector<Submission>> pubSubmissions;   // "classId/aid"
	std::map<std::string, Live> pubLive;
	std::map<std::string, AnswerCounts> pubAnswers;
	std::map<std::string, Status> pubStatus;
	std::map<std::string, std::string> pubLinks;                      // classId -> the live connection's state
	std::map<std::string, std::vector<Item>> pubItems;                // v2
	std::map<std::string, std::vector<Submission>> pubHistory;        // "classId/aid/sid"

	// Polling (engine thread, set through ops).
	std::set<std::string> pagesOpen, following;
	bool active = true;
	int64_t lastInput = 0;
	std::map<std::string, int64_t> nextPoll, lastChange, nextAnswers;
	std::map<std::string, int> failures;
	std::map<std::string, bool> held;   // the last poll was held by the server: ask again at once
	std::set<std::string> busy;         // "classId/pulse", "classId/answers": a held poll is out
	uint32_t seed = 2463534242u;

	// The live connections (3.14), engine thread. `links` is declared after what it points at.
	struct Events : SocketEvents {
		Impl* d;
		explicit Events(Impl* i) : d(i) {}
		void socketOpened(int id) override { d->post({ [this, id] { d->socketEvent(id, 0, std::string(), 0); }, true }); }
		void socketText(int id, const std::string& text) override {
			d->post({ [this, id, text] { d->socketEvent(id, 1, text, 0); }, text != "pong" });
		}
		void socketClosed(int id, int code) override { d->post({ [this, id, code] { d->socketEvent(id, 2, std::string(), code); }, true }); }
	} events{ this };
	SocketHooks sockets;
	std::map<std::string, std::unique_ptr<LiveLink>> links;

	Impl(Config c, Crypto& crypto, Curve& cv_, Host& h) : cfg(std::move(c)), cr(crypto), curve(cv_), host(h) {
		ClientHooks k;
		k.http = [this](const HttpRequest& r) { return host.http(r); };
		k.now = [this] { return clock.now(); };
		k.load = [this](const std::string& n) { return host.loadFile(n); };
		k.save = [this](const std::string& n, const std::string& t) { return host.saveFile(n, t); };
		k.removeTree = [this](const std::string& n) { host.removeTree(n); };
		k.notice = [this](const std::string& t) {
			std::lock_guard<std::mutex> lock(mu);
			pendingNotices.push_back(t);
		};
		k.sideRecords = [this] {
			std::vector<std::pair<std::string, std::string>> out;
			main([&] { out = host.syncSideRecords(); });
			return out;
		};
		k.putSide = [this](const std::string& rid, const std::string& j) { main([&] { host.syncPutSide(rid, j); }); };
		k.deleteSide = [this](const std::string& rid) { main([&] { host.syncDeleteSide(rid); }); };
		k.syncOn = [this] {
			bool on = false;
			main([&] { on = host.syncOn(); });
			return on;
		};
		k.check = [this](const std::string& cdl, const std::string& text, const std::string& names, int& verdict, std::string& summary) {
			return host.checkCircuit(cdl, text, names, verdict, summary);
		};
		k.lights = [this](const std::string& cdl, const std::vector<std::string>& names, std::map<std::string, int>& out) {
			return host.lightsOf(cdl, names, out);
		};
		client.reset(new Client(cfg, cr, curve, k));
		sockets.newId = [] {
			int id = ++gSocketIds;
			if (id <= 0) id = gSocketIds = 1;   // (after two billion sockets)
			return id;
		};
		sockets.open = [this](int id, const std::string& url, const std::vector<std::pair<std::string, std::string>>& headers) {
			return host.socketOpen(id, url, headers, events);
		};
		sockets.send = [this](int id, const std::string& text) { host.socketSend(id, text); };
		sockets.close = [this](int id, int code) { host.socketClose(id, code); };
	}

	// Runs fn on the UI thread (and waits); nothing once stop() came.
	void main(const std::function<void()>& fn) {
		if (stopped.load()) return;
		host.onMain([&] {
			if (!stopped.load()) fn();
		});
	}

	// Engine thread (or the UI thread in start(), before the thread runs).
	void publish(bool onUi = false) {
		std::map<std::string, Status> before;
		{
			std::lock_guard<std::mutex> lock(mu);
			before = pubStatus;
			pubClasses = client->classes();
			pubAssignments.clear();
			pubStudents.clear();
			pubSubmissions.clear();
			pubLive.clear();
			pubAnswers.clear();
			pubStatus.clear();
			pubLinks.clear();
			pubItems.clear();
			pubHistory = client->allHistory();
			for (const auto& l : links) pubLinks[l.first] = LiveLink::stateName(l.second->state());
			for (const ClassInfo& c : pubClasses) {
				pubAssignments[c.classId] = client->assignments(c.classId);
				pubLive[c.classId] = client->live(c.classId);
				pubStatus[c.classId] = client->status(c.classId);
				pubItems[c.classId] = client->items(c.classId);
				if (c.teaching) {
					pubStudents[c.classId] = client->students(c.classId);
					pubAnswers[c.classId] = client->answers(c.classId);
					for (const Assignment& a : pubAssignments[c.classId])
						pubSubmissions[c.classId + "/" + a.id] = client->submissions(c.classId, a.id);
				}
			}
		}
		std::vector<std::pair<std::string, Live>> lives;
		std::vector<std::pair<std::string, AnswerCounts>> counts;
		std::vector<std::pair<std::string, std::string>> handins;
		std::string id, aid;
		while (client->liveChangedFlag(id)) lives.emplace_back(id, client->live(id));
		while (client->answersChangedFlag(id)) counts.emplace_back(id, client->answers(id));
		while (client->handinFlag(id, aid)) handins.emplace_back(id, aid);
		std::vector<Client::News> news;
		Client::News one;
		while (client->newsFlag(one)) news.push_back(one);
		std::vector<std::pair<std::string, Status>> statuses;
		{
			std::lock_guard<std::mutex> lock(mu);
			for (const auto& kv : pubStatus) {
				auto b = before.find(kv.first);
				if (b == before.end() || b->second.kind != kv.second.kind || b->second.text != kv.second.text) statuses.push_back(kv);
			}
		}
		auto tell = [&] {
			host.classesChanged();
			for (const auto& l : lives) host.liveChanged(l.first, l.second);
			for (const auto& c : counts) host.answersChanged(c.first, c.second);
			for (const auto& s : statuses) host.statusChanged(s.first, s.second);
			for (const auto& h : handins) host.submissionsChanged(h.first, h.second);
			for (const auto& n : news) host.itemsChanged(n.classId, n.items, n.className);
			std::vector<std::string> n;
			{
				std::lock_guard<std::mutex> lock(mu);
				n.swap(pendingNotices);
			}
			for (const std::string& t : n) host.notice(t);
		};
		if (onUi) tell();
		else main(tell);
	}

	void post(Op op) {
		{
			std::lock_guard<std::mutex> lock(mu);
			if (stopping) return;
			ops.push_back(std::move(op));
		}
		cv.notify_all();
	}
	void post(std::function<void()> fn) { post(Op{ std::move(fn), true }); }

	// ---- the live connections (3.14) ----

	bool linkOpen(const std::string& classId) const {
		auto it = links.find(classId);
		return it != links.end() && it->second->isOpen();
	}

	// One live connection per class whose page (or live view) is open, as the web's Poller keeps.
	void syncLinks(int64_t now) {
		if (!locked) return;
		std::set<std::string> want;
		for (const std::set<std::string>* s : { &pagesOpen, &following })
			for (const std::string& id : *s)
				if (client->teaches(id) || client->member(id)) want.insert(id);
		for (auto it = links.begin(); it != links.end();) {
			if (want.count(it->first)) {
				++it;
				continue;
			}
			it->second->stop();
			it = links.erase(it);
		}
		for (const std::string& id : want) {
			if (links.count(id)) continue;
			seed = seed * 1664525u + 1013904223u;
			links[id].reset(new LiveLink(*client, id, sockets, seed));
			links[id]->start(now);
		}
		afterLinks(now);
	}

	// A connection opened, fell back or ended: the polls follow (4.8). Open: only a safety poll
	// now and then; fallback or ended: polling takes over at once.
	void afterLinks(int64_t now) {
		for (auto& kv : links) {
			if (!kv.second->stateChanged()) continue;
			const LiveLink::State st = kv.second->state();
			if (busy.count(kv.first + "/pulse")) continue;   // a held poll is out: its answer schedules the next
			if (st == LiveLink::Open) nextPoll[kv.first] = now + kSafety;
			else if (st == LiveLink::Fallback || st == LiveLink::Closed) nextPoll[kv.first] = std::min<int64_t>(nextPoll[kv.first], now);
		}
	}

	void socketEvent(int id, int kind, const std::string& text, int code) {
		const int64_t now = clock.now();
		for (auto& kv : links) {
			LiveLink& l = *kv.second;
			if (!l.owns(id)) continue;
			if (kind == 0) l.opened(id, now);
			else if (kind == 1) l.text(id, text, now);
			else l.closed(id, code, now);
			break;
		}
		afterLinks(now);
	}

	void tickLinks(int64_t now) {
		for (auto& kv : links) kv.second->tick(now);
		afterLinks(now);
	}

	// ---- polling (4.8) ----

	static constexpr int64_t kSafety = 5 * kMinute;   // a poll now and then while the live connection is open

	bool idle(int64_t now) const { return !active || now - lastInput > 2 * kHour; }

	bool wanted(const std::string& classId) {
		if (!locked) return false;
		if (client->teaches(classId)) return pagesOpen.count(classId) || client->predictOpen(classId);
		if (!client->member(classId)) return false;
		const Membership* m = client->membershipOf(classId);
		(void)m;
		return true;   // v2: a joined class is polled while the app is in use (Your teacher shared …, 3.16.2)
	}

	// How long the server should hold a poll: not while the live connection is open (it brings
	// everything), else the server's holdSeconds (3.3).
	int64_t holdFor(const std::string& classId) const { return linkOpen(classId) ? 0 : client->holdSeconds(); }

	bool answersDue(const std::string& classId, int64_t now) {
		return client->predictOpen(classId) && !linkOpen(classId) && !busy.count(classId + "/answers") && now >= nextAnswers[classId];
	}

	int64_t interval(const std::string& classId, int64_t now) {
		if (linkOpen(classId)) return kSafety;
		if (client->teaches(classId)) return 60 * kSecond;
		if (client->liveOn(classId) && (following.count(classId) || pagesOpen.count(classId)))
			return held[classId] ? kSecond : client->pollSeconds() * kSecond;   // held: again at once
		const int64_t since = lastChange.count(classId) ? now - lastChange[classId] : 0;
		return since > 10 * kMinute ? 120 * kSecond : 60 * kSecond;
	}

	static bool waits(const HttpRequest& q) {
		for (const auto& h : q.headers)
			if (h.first == "x-cedarlogic-wait") return true;
		return false;
	}

	// A poll the server holds, on a thread of its own so the engine thread goes on (a hand-in or an
	// answer mustn't wait 25 s behind it); its answer comes back as an operation.
	void holdPoll(const HttpRequest& q, std::function<void(const HttpResponse&)> apply) {
		{
			std::lock_guard<std::mutex> lock(mu);
			if (stopping) return;
			background++;
		}
		std::thread([this, q, apply] {
			const HttpResponse h = host.http(q);
			post([apply, h] { apply(h); });
			std::lock_guard<std::mutex> lock(mu);
			background--;
			cv.notify_all();
		}).detach();
	}

	void schedule(const std::string& classId, const Result& r, int64_t now) {
		const bool failed = !r.ok && (r.status == 0 || r.status == 429 || r.status >= 500);
		if (failed) failures[classId]++;
		else failures[classId] = 0;
		int64_t next = now + interval(classId, now);
		if (failed) {
			int64_t wait = backoffMs(failures[classId], seed);
			if (client->liveOn(classId)) wait = std::min<int64_t>(wait, 60 * kSecond);   // a lecture mustn't go quiet
			next = now + wait;
		}
		const int64_t retryAt = client->retryAfterMs();
		if (retryAt > next) next = retryAt;
		nextPoll[classId] = next;
	}

	void pollAnswers(const std::string& classId, int64_t now) {
		const int64_t wait = holdFor(classId);
		const HttpRequest q = client->answersRequest(classId, wait);
		if (q.url.empty()) return;
		if (!waits(q)) {
			client->answersReply(classId, host.http(q));
			nextAnswers[classId] = now + 3 * kSecond;
			return;
		}
		busy.insert(classId + "/answers");
		holdPoll(q, [this, classId, now](const HttpResponse& h) {
			busy.erase(classId + "/answers");
			const AnswerCounts before = client->answers(classId);
			client->answersReply(classId, h);
			const int64_t at = clock.now();
			const bool news = client->answers(classId).answered != before.answered;
			// Held for real (or answered with news): ask again at once; a server that didn't hold: 3 s.
			const bool wasHeld = (h.status == 200 || h.status == 304) && (at - now >= 2 * kSecond || news);
			nextAnswers[classId] = at + (wasHeld ? kSecond : 3 * kSecond);
		});
	}

	void pollOne(const std::string& classId, int64_t now) {
		Result r;
		if (client->teaches(classId)) {
			if (answersDue(classId, now)) pollAnswers(classId, now);
			if (now < nextPoll[classId]) return;
			r = client->refreshTeacher(classId);
			schedule(classId, r, now);
			return;
		}
		if (busy.count(classId + "/pulse")) return;
		const Membership* before = client->membershipOf(classId);
		const int64_t seqBefore = before ? before->seq : 0, liveBefore = before ? before->liveSeen : 0;
		const int64_t wait = client->liveOn(classId) ? holdFor(classId) : 0;   // held during a lecture (3.10)
		const HttpRequest q = client->pulseRequest(classId, wait);
		const bool asked = waits(q);
		const bool holding = holdFor(classId) > 0;   // polls are held now (no open connection)
		auto apply = [this, classId, now, seqBefore, liveBefore, holding](const HttpResponse& h) {
			const int64_t at = clock.now();
			const Result res = client->pulseReply(classId, h);
			const Membership* m = client->membershipOf(classId);
			const bool news = m && (m->seq != seqBefore || m->liveSeen != liveBefore);
			if (news) lastChange[classId] = at;
			// Held for real, or answered with news (a session that just began): ask again at once,
			// held. A server that doesn't hold answers at once: the usual interval.
			held[classId] = holding && (h.status == 200 || h.status == 304) && (at - now >= 2 * kSecond || news);
			schedule(classId, res, at);
		};
		if (!asked) {
			apply(host.http(q));
			return;
		}
		busy.insert(classId + "/pulse");
		nextPoll[classId] = INT64_MAX;
		holdPoll(q, [this, classId, apply](const HttpResponse& h) {
			busy.erase(classId + "/pulse");
			if (client->member(classId)) apply(h);   // (published after, as every operation)
		});
	}

	void pollDue() {
		const int64_t now = clock.now();
		if (idle(now)) return;
		std::vector<std::string> ids;
		for (const ClassInfo& c : client->classes()) ids.push_back(c.classId);
		bool any = false;
		for (const std::string& id : ids) {
			if (stopping) return;
			if (!wanted(id)) continue;
			if (now < nextPoll[id] && !answersDue(id, now)) continue;
			pollOne(id, now);
			any = true;
		}
		if (any) publish();
	}

	int64_t nextWake() {
		const int64_t now = clock.now();
		int64_t wake = INT64_MAX;
		for (const auto& kv : links) wake = std::min(wake, kv.second->nextTimer());
		if (idle(now)) return wake;
		for (const ClassInfo& c : client->classes()) {
			if (!wanted(c.classId)) continue;
			wake = std::min(wake, nextPoll[c.classId]);
			if (client->predictOpen(c.classId) && !linkOpen(c.classId) && !busy.count(c.classId + "/answers"))
				wake = std::min(wake, nextAnswers[c.classId]);
		}
		return wake;
	}

	void loop() {
		std::unique_lock<std::mutex> lock(mu);
		while (!stopping) {
			if (!ops.empty()) {
				Op op = std::move(ops.front());
				ops.pop_front();
				lock.unlock();
				op.fn();
				if (op.publish) publish();
				lock.lock();
				continue;
			}
			lock.unlock();
			const int64_t now = clock.now();
			syncLinks(now);
			tickLinks(now);
			pollDue();
			const int64_t wake = nextWake();
			lock.lock();
			if (stopping || !ops.empty()) continue;
			if (wake == INT64_MAX) cv.wait(lock);
			else {
				const int64_t ms = std::max<int64_t>(50, wake - clock.now());
				cv.wait_for(lock, std::chrono::milliseconds(std::min<int64_t>(ms, 60 * kSecond)));
			}
		}
		exited = true;
		cv.notify_all();
	}

	// An operation whose result goes to `done` on the UI thread.
	void run(std::function<Result()> op, std::function<void(const Result&)> done) {
		post([this, op, done] {
			const Result r = op();
			publish();
			if (done) main([&] { done(r); });
		});
	}
};

Engine::Engine(Config c, Crypto& cr, Curve& curve, Host& h) : d(new Impl(std::move(c), cr, curve, h)) {}

Engine::~Engine() {
	if (!d) return;
	stop();
	bool exited = true, quiet = true;
	{
		std::unique_lock<std::mutex> lock(d->mu);
		if (d->running) exited = d->cv.wait_for(lock, std::chrono::seconds(3), [this] { return d->exited; });
		// A held poll's thread: a moment to come back (it answers at once on a test server).
		quiet = d->cv.wait_for(lock, std::chrono::seconds(1), [this] { return d->background == 0; });
	}
	if (d->running) {
		if (exited) {
			d->th.join();
		} else {
			// The thread is still inside a host call (the network, or an onMain this thread can't
			// serve now): it keeps its Impl rather than outliving what it points at.
			d->th.detach();
			d.release();
			gDetached++;
			return;
		}
	}
	// The live connections end here (their pending answers with them: nothing is reported now).
	for (auto& kv : d->links) kv.second->abandon();
	if (d->locked) d->host.unlock();
	if (!quiet) {
		// A held poll is still out (up to holdSeconds): its thread keeps the Impl.
		d.release();
		gDetached++;
	}
}

void Engine::start() {
	if (d->started) return;
	d->started = true;
	d->locked = d->host.tryLock(d->cfg.dir.empty() ? std::string("lock") : d->cfg.dir + "/lock");
	d->lastInput = d->clock.now();
	d->client->load();
	d->publish(true);
	d->running = true;
	d->th = std::thread([this] { d->loop(); });
	d->post([this] { d->client->sideChanged(); });
}

void Engine::stop() {
	{
		std::lock_guard<std::mutex> lock(d->mu);
		d->stopping = true;
		d->ops.clear();
	}
	d->stopped = true;
	d->cv.notify_all();
}

std::vector<ClassInfo> Engine::classes() const {
	std::lock_guard<std::mutex> lock(d->mu);
	return d->pubClasses;
}

Status Engine::status(const std::string& classId) const {
	std::lock_guard<std::mutex> lock(d->mu);
	auto it = d->pubStatus.find(classId);
	return it == d->pubStatus.end() ? Status() : it->second;
}

namespace {
std::function<void(const Result&)> plain(Done done) {
	return [done](const Result& r) {
		if (done) done(r.ok, r.message);
	};
}
std::function<void(const Result&)> valued(std::function<void(bool, std::string, std::string)> done) {
	return [done](const Result& r) {
		if (done) done(r.ok, r.message, r.value);
	};
}
}  // namespace

void Engine::createClass(const std::string& name, std::function<void(bool, std::string, std::string)> done) {
	d->run([this, name] { return d->client->createClass(name); }, valued(done));
}
void Engine::previewTeacherKey(const std::string& text, std::function<void(bool, std::string, std::string)> done) {
	d->run([this, text] { return d->client->previewTeacherKey(text); }, valued(done));
}
void Engine::addTeacherKey(const std::string& text, Done done) {
	d->run([this, text] { return d->client->addTeacherKey(text); }, plain(done));
}
void Engine::renameClass(const std::string& classId, const std::string& name, Done done) {
	d->run([this, classId, name] { return d->client->renameClass(classId, name); }, plain(done));
}
void Engine::setJoinOpen(const std::string& classId, bool open, Done done) {
	d->run([this, classId, open] { return d->client->setJoinOpen(classId, open); }, plain(done));
}
void Engine::newJoinCode(const std::string& classId, Done done) {
	d->run([this, classId] { return d->client->newJoinCode(classId); }, plain(done));
}
void Engine::forgetClass(const std::string& classId) {
	d->post([this, classId] { d->client->forgetClass(classId); });
}
void Engine::deleteClass(const std::string& classId, Done done) {
	d->run([this, classId] { return d->client->deleteClass(classId); }, plain(done));
}
std::vector<Assignment> Engine::assignments(const std::string& classId) const {
	std::lock_guard<std::mutex> lock(d->mu);
	auto it = d->pubAssignments.find(classId);
	return it == d->pubAssignments.end() ? std::vector<Assignment>() : it->second;
}
void Engine::postAssignment(const std::string& classId, const Assignment& draft, bool studentsCanCheck, Done done) {
	d->run([this, classId, draft, studentsCanCheck] { return d->client->postAssignment(classId, draft, studentsCanCheck); }, plain(done));
}
void Engine::deleteAssignment(const std::string& classId, const std::string& aid, Done done) {
	d->run([this, classId, aid] { return d->client->deleteAssignment(classId, aid); }, plain(done));
}
std::vector<Student> Engine::students(const std::string& classId) const {
	std::lock_guard<std::mutex> lock(d->mu);
	auto it = d->pubStudents.find(classId);
	return it == d->pubStudents.end() ? std::vector<Student>() : it->second;
}
void Engine::refreshStudents(const std::string& classId, Done done) {
	d->run([this, classId] { return d->client->refreshStudents(classId); }, plain(done));
}
void Engine::removeStudents(const std::string& classId, const std::vector<std::string>& sids, bool deleteHandIns, Done done) {
	d->run([this, classId, sids, deleteHandIns] { return d->client->removeStudents(classId, sids, deleteHandIns); }, plain(done));
}
std::vector<Submission> Engine::submissions(const std::string& classId, const std::string& aid) const {
	std::lock_guard<std::mutex> lock(d->mu);
	auto it = d->pubSubmissions.find(classId + "/" + aid);
	return it == d->pubSubmissions.end() ? std::vector<Submission>() : it->second;
}
void Engine::refreshSubmissions(const std::string& classId, const std::string& aid, Done done) {
	d->run([this, classId, aid] { return d->client->refreshSubmissions(classId, aid); }, plain(done));
}
void Engine::goLive(const std::string& classId, const std::string& cdl, Done done) {
	d->run([this, classId, cdl] { return d->client->goLive(classId, cdl); }, plain(done));
}
void Engine::push(const std::string& classId, const std::string& cdl, const std::string* prompt, const std::vector<std::string>* lights,
                  bool reveal, Done done) {
	const bool hasPrompt = prompt != nullptr;
	const std::string p = prompt ? *prompt : std::string();
	const std::vector<std::string> l = lights ? *lights : std::vector<std::string>();
	d->run([this, classId, cdl, hasPrompt, p, l, reveal] { return d->client->push(classId, cdl, hasPrompt ? &p : nullptr, &l, reveal); },
	       plain(done));
}
void Engine::endLive(const std::string& classId, Done done) {
	d->run([this, classId] { return d->client->endLive(classId); }, plain(done));
}
void Engine::takeOverLive(const std::string& classId, Done done) {
	d->run([this, classId] { return d->client->takeOverLive(classId); }, plain(done));
}
Live Engine::live(const std::string& classId) const {
	std::lock_guard<std::mutex> lock(d->mu);
	auto it = d->pubLive.find(classId);
	return it == d->pubLive.end() ? Live() : it->second;
}
AnswerCounts Engine::answers(const std::string& classId) const {
	std::lock_guard<std::mutex> lock(d->mu);
	auto it = d->pubAnswers.find(classId);
	return it == d->pubAnswers.end() ? AnswerCounts() : it->second;
}

std::vector<Item> Engine::items(const std::string& classId) const {
	std::lock_guard<std::mutex> lock(d->mu);
	auto it = d->pubItems.find(classId);
	return it == d->pubItems.end() ? std::vector<Item>() : it->second;
}
void Engine::postItem(const std::string& classId, const Item& draft, bool hidden, std::function<void(bool, std::string, std::string)> done) {
	d->run([this, classId, draft, hidden] { return d->client->postItem(classId, draft, hidden); }, valued(done));
}
void Engine::setItemHidden(const std::string& classId, const std::string& iid, bool hidden, Done done) {
	d->run([this, classId, iid, hidden] { return d->client->setItemHidden(classId, iid, hidden); }, plain(done));
}
void Engine::deleteItem(const std::string& classId, const std::string& iid, Done done) {
	d->run([this, classId, iid] { return d->client->deleteItem(classId, iid); }, plain(done));
}
void Engine::itemOpened(const std::string& classId, const std::string& iid) {
	d->post([this, classId, iid] { d->client->itemOpened(classId, iid); });
}
void Engine::loadHistory(const std::string& classId, const std::string& aid, const std::string& sid, Done done) {
	d->run([this, classId, aid, sid] { return d->client->loadHistory(classId, aid, sid); }, plain(done));
}
std::vector<Submission> Engine::history(const std::string& classId, const std::string& aid, const std::string& sid) const {
	std::lock_guard<std::mutex> lock(d->mu);
	auto it = d->pubHistory.find(classId + "/" + aid + "/" + sid);
	return it == d->pubHistory.end() ? std::vector<Submission>() : it->second;
}

void Engine::previewJoinCode(const std::string& text, std::function<void(bool, std::string, std::string, bool)> done) {
	d->run([this, text] { return d->client->previewJoinCode(text); },
	       [done](const Result& r) {
		       if (!done) return;
		       const size_t nl = r.value.rfind('\n');
		       done(r.ok, r.message, nl == std::string::npos ? r.value : r.value.substr(0, nl),
		            nl != std::string::npos && r.value.substr(nl + 1) == "1");
	       });
}
void Engine::join(const std::string& text, const std::string& name, std::function<void(bool, std::string, std::string)> done) {
	d->run([this, text, name] { return d->client->join(text, name); }, valued(done));
}
void Engine::rename(const std::string& classId, const std::string& name, Done done) {
	d->run([this, classId, name] { return d->client->rename(classId, name); }, plain(done));
}
void Engine::handIn(const std::string& classId, const std::string& aid, const std::string& cdl, Done done) {
	d->run([this, classId, aid, cdl] { return d->client->handIn(classId, aid, cdl); }, plain(done));
}
void Engine::follow(const std::string& classId, bool following) {
	d->post([this, classId, following] {
		if (following) {
			d->following.insert(classId);
			d->nextPoll[classId] = 0;   // at once
		} else {
			d->following.erase(classId);
		}
	});
}
void Engine::sendAnswer(const std::string& classId, const std::map<std::string, int>& lights, Done done) {
	d->post([this, classId, lights, done] {
		auto tell = [this, done](const Result& r) {
			if (done) d->main([&] { done(r.ok, r.message); });
		};
		json::Value body;
		const Result sealed = d->client->sealAnswer(classId, lights, body);
		if (!sealed.ok || sealed.value == "none") {
			tell(sealed.ok ? Result::good() : sealed);
			return;
		}
		// Over the live connection while it is open (a twentieth of a request, 3.10); the PUT if
		// no "ok" or "error" comes within 8 s.
		auto it = d->links.find(classId);
		if (it != d->links.end() && it->second->isOpen()) {
			json::Value msg = body;
			msg.set("t", json::Value::string("answer"));
			const bool sent = it->second->request(
				msg,
				[this, classId, body, lights, tell](const json::Value* reply) {
					tell(reply ? d->client->answerReply(classId, *reply, lights) : d->client->putAnswer(classId, body, lights));
				},
				d->clock.now());
			if (sent) return;
		}
		tell(d->client->putAnswer(classId, body, lights));
	});
}
void Engine::makeMoveCode(const std::string& classId, std::function<void(bool, std::string, std::string)> done) {
	d->run([this, classId] { return d->client->makeMoveCode(classId); }, valued(done));
}
void Engine::previewMoveCode(const std::string& text, std::function<void(bool, std::string, std::string, std::string)> done) {
	d->run([this, text] { return d->client->previewMoveCode(text); },
	       [done](const Result& r) {
		       if (!done) return;
		       const size_t nl = r.value.find('\n');
		       done(r.ok, r.message, nl == std::string::npos ? r.value : r.value.substr(0, nl),
		            nl == std::string::npos ? std::string() : r.value.substr(nl + 1));
	       });
}
void Engine::importMoveCode(const std::string& text, Done done) {
	d->run([this, text] { return d->client->importMoveCode(text); }, plain(done));
}
void Engine::makeClassPass(const std::string& classId, std::function<void(bool, std::string, std::string)> done) {
	d->run([this, classId] { return d->client->makeClassPass(classId); }, valued(done));
}
void Engine::previewClassPass(const std::string& text, std::function<void(bool, std::string, std::string)> done) {
	d->run([this, text] { return d->client->previewClassPass(text); }, valued(done));
}
void Engine::useClassPass(const std::string& text, std::function<void(bool, std::string, std::string)> done) {
	d->run([this, text] { return d->client->useClassPass(text); }, valued(done));
}
void Engine::listPasses(const std::string& classId, std::function<void(bool, std::string, std::string)> done) {
	d->run([this, classId] { return d->client->listPasses(classId); }, valued(done));
}
void Engine::cancelPass(const std::string& classId, const std::string& pid, Done done) {
	d->run([this, classId, pid] { return d->client->cancelPass(classId, pid); }, plain(done));
}
void Engine::leaveClass(const std::string& classId, Done done) {
	d->run([this, classId] { return d->client->leaveClass(classId); }, plain(done));
}
void Engine::forgetMembership(const std::string& classId) {
	d->post([this, classId] { d->client->forgetMembership(classId); });
}

void Engine::pageOpen(const std::string& classId, bool open) {
	d->post([this, classId, open] {
		if (open) {
			d->pagesOpen.insert(classId);
			d->nextPoll[classId] = 0;
			d->lastChange[classId] = d->clock.now();
		} else {
			d->pagesOpen.erase(classId);
		}
	});
}
void Engine::appActivated() {
	d->post([this] {
		d->active = true;
		d->lastInput = d->clock.now();
		for (const std::string& id : d->pagesOpen) d->nextPoll[id] = 0;
		for (const std::string& id : d->following) d->nextPoll[id] = 0;
	});
}
void Engine::appDeactivated() {
	d->post([this] { d->active = false; });
}
void Engine::userActive() {
	d->post([this] {
		const bool wasIdle = d->idle(d->clock.now());
		d->lastInput = d->clock.now();
		if (wasIdle)
			for (const std::string& id : d->pagesOpen) d->nextPoll[id] = 0;
	});
}
void Engine::syncSideChanged() {
	d->post([this] { d->client->sideChanged(); });
}

void Engine::socketOpened(int id) { d->events.socketOpened(id); }
void Engine::socketText(int id, const std::string& text) { d->events.socketText(id, text); }
void Engine::socketClosed(int id, int code) { d->events.socketClosed(id, code); }

std::string Engine::liveConnection(const std::string& classId) const {
	std::lock_guard<std::mutex> lock(d->mu);
	auto it = d->pubLinks.find(classId);
	return it == d->pubLinks.end() ? std::string() : it->second;
}

}  // namespace clclass
