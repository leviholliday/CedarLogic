// clclass::Engine: the Client on its own thread (CLASSROOM.md 6.2, 4.8). The
// UI thread posts operations and reads published copies of the lists; the
// engine thread runs one operation or one poll at a time, publishes, and
// reaches the UI only through Host::onMain. Polling: the pulse every `p`
// seconds while a session is on and the class page or live view is open, 60 s
// otherwise (120 s after 10 minutes without change), nothing in the background
// or after two hours without input; failures back off as Sync's.

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
	std::deque<std::function<void()>> ops;
	std::vector<std::string> pendingNotices;

	// Published for the UI thread (under mu).
	std::vector<ClassInfo> pubClasses;
	std::map<std::string, std::vector<Assignment>> pubAssignments;
	std::map<std::string, std::vector<Student>> pubStudents;
	std::map<std::string, std::vector<Submission>> pubSubmissions;   // "classId/aid"
	std::map<std::string, Live> pubLive;
	std::map<std::string, AnswerCounts> pubAnswers;
	std::map<std::string, Status> pubStatus;

	// Polling (engine thread, set through ops).
	std::set<std::string> pagesOpen, following;
	bool active = true;
	int64_t lastInput = 0;
	std::map<std::string, int64_t> nextPoll, lastChange, nextAnswers;
	std::map<std::string, int> failures;
	uint32_t seed = 2463534242u;

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
		k.check = [this](const std::string& cdl, const std::string& text, const std::string& names, int& verdict, std::string& summary) {
			return host.checkCircuit(cdl, text, names, verdict, summary);
		};
		k.lights = [this](const std::string& cdl, const std::vector<std::string>& names, std::map<std::string, int>& out) {
			return host.lightsOf(cdl, names, out);
		};
		client.reset(new Client(cfg, cr, curve, k));
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
			for (const ClassInfo& c : pubClasses) {
				pubAssignments[c.classId] = client->assignments(c.classId);
				pubLive[c.classId] = client->live(c.classId);
				pubStatus[c.classId] = client->status(c.classId);
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
		std::string id;
		while (client->liveChangedFlag(id)) lives.emplace_back(id, client->live(id));
		while (client->answersChangedFlag(id)) counts.emplace_back(id, client->answers(id));
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

	void post(std::function<void()> op) {
		{
			std::lock_guard<std::mutex> lock(mu);
			if (stopping) return;
			ops.push_back(std::move(op));
		}
		cv.notify_all();
	}

	// ---- polling (4.8) ----

	bool idle(int64_t now) const { return !active || now - lastInput > 2 * kHour; }

	bool wanted(const std::string& classId) {
		if (!locked) return false;
		if (client->teaches(classId)) return pagesOpen.count(classId) || client->predictOpen(classId);
		if (!client->member(classId)) return false;
		const Membership* m = client->membershipOf(classId);
		return pagesOpen.count(classId) || following.count(classId) || (m && !m->pending.empty());
	}

	int64_t interval(const std::string& classId, int64_t now) {
		if (client->teaches(classId)) return 60 * kSecond;
		if (client->liveOn(classId) && (following.count(classId) || pagesOpen.count(classId))) return client->pollSeconds() * kSecond;
		const int64_t since = lastChange.count(classId) ? now - lastChange[classId] : 0;
		return since > 10 * kMinute ? 120 * kSecond : 60 * kSecond;
	}

	void pollOne(const std::string& classId, int64_t now) {
		Result r;
		if (client->teaches(classId)) {
			if (client->predictOpen(classId) && now >= nextAnswers[classId]) {
				client->refreshAnswers(classId);
				nextAnswers[classId] = now + 3 * kSecond;
			}
			if (now < nextPoll[classId]) return;
			r = client->refreshTeacher(classId);
		} else {
			const int64_t seqBefore = client->membershipOf(classId) ? client->membershipOf(classId)->seq : 0;
			const int64_t liveBefore = client->membershipOf(classId) ? client->membershipOf(classId)->liveSeen : 0;
			r = client->pulse(classId);
			const Membership* m = client->membershipOf(classId);
			if (m && (m->seq != seqBefore || m->liveSeen != liveBefore)) lastChange[classId] = now;
		}
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

	void pollDue() {
		const int64_t now = clock.now();
		if (idle(now)) return;
		std::vector<std::string> ids;
		for (const ClassInfo& c : client->classes()) ids.push_back(c.classId);
		bool any = false;
		for (const std::string& id : ids) {
			if (stopping) return;
			if (!wanted(id)) continue;
			const bool answersDue = client->predictOpen(id) && now >= nextAnswers[id];
			if (now < nextPoll[id] && !answersDue) continue;
			pollOne(id, now);
			any = true;
		}
		if (any) publish();
	}

	int64_t nextWake() {
		const int64_t now = clock.now();
		if (idle(now)) return INT64_MAX;
		int64_t wake = INT64_MAX;
		for (const ClassInfo& c : client->classes()) {
			if (!wanted(c.classId)) continue;
			wake = std::min(wake, nextPoll[c.classId]);
			if (client->predictOpen(c.classId)) wake = std::min(wake, nextAnswers[c.classId]);
		}
		return wake;
	}

	void loop() {
		std::unique_lock<std::mutex> lock(mu);
		while (!stopping) {
			if (!ops.empty()) {
				std::function<void()> op = std::move(ops.front());
				ops.pop_front();
				lock.unlock();
				op();
				publish();
				lock.lock();
				continue;
			}
			lock.unlock();
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
	bool exited = true;
	{
		std::unique_lock<std::mutex> lock(d->mu);
		if (d->running) exited = d->cv.wait_for(lock, std::chrono::seconds(3), [this] { return d->exited; });
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
	if (d->locked) d->host.unlock();
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
	d->run([this, classId, lights] { return d->client->sendAnswer(classId, lights); }, plain(done));
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

}  // namespace clclass
