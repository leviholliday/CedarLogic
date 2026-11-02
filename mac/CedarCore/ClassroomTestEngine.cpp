// The threaded engine (ClassroomEngine.cpp) end to end on the FakeServer: two
// engines, a teacher's and a student's, each on its own thread, through the
// Host interface -- create, join, post, the class page's polling, hand in,
// view, live, and stopping.

#include "ClassroomTest.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace clclass {
namespace test {

namespace {

// A Host over the FakeServer (shared, so behind a lock), files in memory, the UI thread being
// whichever thread asks (the self-test has no run loop).
struct TestHost : Host {
	FakeServer& server;
	std::mutex& serverLock;
	std::mutex mu;
	std::condition_variable cv;
	std::mutex filesMu;
	std::map<std::string, std::string> files;
	std::vector<std::string> notices;
	std::map<std::string, Live> lives;
	int changed = 0;
	bool ownLock = true;
	bool* lockFree = &ownLock;   // the Classroom folder's lock (shared by hosts of one folder)
	TestHost(FakeServer& s, std::mutex& l) : server(s), serverLock(l) {}
	std::map<std::string, std::string> copyFiles() {
		std::lock_guard<std::mutex> lock(filesMu);
		return files;
	}
	HttpResponse http(const HttpRequest& r) override {
		std::lock_guard<std::mutex> lock(serverLock);
		return server.handle(r);
	}
	void onMain(const std::function<void()>& fn) override {
		std::lock_guard<std::mutex> lock(mu);   // one "UI thread" at a time
		fn();
	}
	std::string loadFile(const std::string& n) override {
		std::lock_guard<std::mutex> lock(filesMu);
		auto it = files.find(n);
		return it == files.end() ? std::string() : it->second;
	}
	bool saveFile(const std::string& n, const std::string& t) override {
		std::lock_guard<std::mutex> lock(filesMu);
		files[n] = t;
		return true;
	}
	void removeTree(const std::string& n) override {
		std::lock_guard<std::mutex> lock(filesMu);
		for (auto it = files.begin(); it != files.end();) it = it->first.compare(0, n.size(), n) == 0 ? files.erase(it) : std::next(it);
	}
	bool tryLock(const std::string&) override {
		std::lock_guard<std::mutex> lock(filesMu);
		const bool was = *lockFree;
		*lockFree = false;
		return was;
	}
	void unlock() override {
		std::lock_guard<std::mutex> lock(filesMu);
		*lockFree = true;
	}
	void classesChanged() override {
		changed++;   // (under mu: onMain holds it)
		cv.notify_all();
	}
	void liveChanged(const std::string& classId, const Live& l) override {
		lives[classId] = l;
		cv.notify_all();
	}
	void answersChanged(const std::string&, const AnswerCounts&) override {}
	void statusChanged(const std::string&, const Status&) override {}
	void notice(const std::string& t) override { notices.push_back(t); }
	std::vector<std::pair<std::string, std::string>> syncSideRecords() override { return {}; }
	void syncPutSide(const std::string&, const std::string&) override {}
	void syncDeleteSide(const std::string&) override {}
};

// Waits (up to `seconds`) for a done callback's answer.
struct Waiter {
	std::mutex mu;
	std::condition_variable cv;
	bool done = false, ok = false;
	std::string message, value;
	void set(bool k, const std::string& m, const std::string& v = std::string()) {
		std::lock_guard<std::mutex> lock(mu);
		done = true;
		ok = k;
		message = m;
		value = v;
		cv.notify_all();
	}
	bool wait(double seconds = 20) {
		std::unique_lock<std::mutex> lock(mu);
		return cv.wait_for(lock, std::chrono::milliseconds((int)(seconds * 1000)), [this] { return done; });
	}
};

template <typename F>
bool until(F f, double seconds = 10) {
	const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds((int)(seconds * 1000));
	while (std::chrono::steady_clock::now() < end) {
		if (f()) return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	return f();
}

}  // namespace

void engineTests(Crypto& cr, Curve& curve, const std::string& tempDir, Report& r) {
	(void)tempDir;
	if (!r.wanted("engine")) return;
	FakeServer server(cr);
	server.now = clsync::SystemClock().now();   // the engine's clock is the real one
	std::mutex serverLock;
	TestHost th(server, serverLock), sh(server, serverLock);
	Config cfg;
	cfg.serverBase = FakeServer::base();
	cfg.liveBase = FakeServer::liveBase();
	cfg.client = "test/engine";
	std::string cid, code, aid;
	{
		Engine teacher(cfg, cr, curve, th), student(cfg, cr, curve, sh);
		teacher.start();
		student.start();
		Waiter w1;
		teacher.createClass("Engine class", [&](bool ok, std::string m, std::string id) { w1.set(ok, m, id); });
		r.line(w1.wait() && w1.ok && teacher.classes().size() == 1 && teacher.classes()[0].name == "Engine class", "engine: create (done on the UI side)",
		       w1.message);
		cid = w1.value;
		code = teacher.classes().empty() ? std::string() : teacher.classes()[0].joinCode;
		Waiter w2;
		student.previewJoinCode(code, [&](bool ok, std::string m, std::string name, bool open) { w2.set(ok && open, m, name); });
		Waiter w3;
		student.join(code, "Sam", [&](bool ok, std::string m, std::string id) { w3.set(ok, m, id); });
		r.line(w2.wait() && w2.ok && w2.value == "Engine class" && w3.wait() && w3.ok && w3.value == cid, "engine: preview and join", w3.message);
		Assignment a;
		a.title = "Engine lab";
		a.cdl = "(cedarlogic)";
		Waiter w4;
		teacher.postAssignment(cid, a, true, [&](bool ok, const std::string& m) { w4.set(ok, m); });
		r.line(w4.wait() && w4.ok && teacher.assignments(cid).size() == 1, "engine: post", w4.message);
		aid = teacher.assignments(cid).empty() ? std::string() : teacher.assignments(cid)[0].id;
		student.pageOpen(cid, true);   // the class page: the pulse at once
		r.line(until([&] { return student.assignments(cid).size() == 1; }), "engine: the open class page polls and lists the assignment");
		Waiter w5;
		student.handIn(cid, aid, "(cedarlogic 1)", [&](bool ok, const std::string& m) { w5.set(ok, m); });
		Waiter w6;
		w5.wait();
		teacher.refreshSubmissions(cid, aid, [&](bool ok, const std::string& m) { w6.set(ok, m); });
		r.line(w5.ok && w6.wait() && teacher.submissions(cid, aid).size() == 1 && teacher.submissions(cid, aid)[0].name == "Sam",
		       "engine: hand in; the teacher views it", w5.message);
		Waiter w7;
		teacher.goLive(cid, "(cedarlogic live)", [&](bool ok, const std::string& m) { w7.set(ok, m); });
		w7.wait();
		student.follow(cid, true);
		r.line(w7.ok && until([&] {
			       std::lock_guard<std::mutex> lock(sh.mu);
			       return sh.lives.count(cid) && sh.lives[cid].on && sh.lives[cid].cdl == "(cedarlogic live)";
		       }),
		       "engine: live: the student's engine reports the pushed circuit");
		int changed = 0;
		{
			std::lock_guard<std::mutex> lock(th.mu);
			changed = th.changed;
		}
		r.line(sh.copyFiles().count("memberships.json") && th.copyFiles().count("teaching.json") && changed > 0,
		       "engine: state files written, the UI told");
		// A second copy of the app on the same folder doesn't get the lock, so it doesn't poll.
		TestHost th2(server, serverLock);
		th2.files = th.copyFiles();
		th2.lockFree = th.lockFree;
		Engine again(cfg, cr, curve, th2);
		again.start();
		r.line(again.classes().size() == 1 && !*th.lockFree, "engine: a second copy loads the classes without the lock");
		student.stop();
		Waiter w8;
		student.handIn(cid, aid, "(cedarlogic 2)", [&](bool ok, const std::string& m) { w8.set(ok, m); });
		r.line(!w8.wait(0.5), "engine: nothing runs after stop()");
	}
	r.line(detachedEngines() == 0 && th.ownLock, "engine: every thread finished on destruction, the lock given back");
}

}  // namespace test
}  // namespace clclass
