// The live connection (CLASSROOM.md 3.14) and the polls the server holds (3.3):
//
// - liveTests: Clients and their LiveLinks against a FakeHub -- the class
//   object's sockets, a port of cloudflare/classroom/src/class.mjs's, over the
//   FakeServer -- on the FakeServer's clock: hello, pushes with the record
//   inside (no pulse, no record fetch, no status read), the seq-by-one rule,
//   answers by id and their 8 s HTTP fallback, the teacher's batched answers,
//   pings, closing codes, backoff, and the fallback after three tries
//   (scenarios 47 and 48 in miniature).
// - engineLiveTests: the threaded Engine with the same FakeHub behind its
//   Host's socket hooks.
// - liveServerTests: the Engine against a real server (the Worker under
//   wrangler dev) through the platform's own HTTP and, if it has them,
//   WebSockets: a teacher and students on sockets, one student whose network
//   blocks them (three tries, then held polls), a push, answers, a removal and
//   a deletion.

#include "ClassroomTest.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <condition_variable>
#include <algorithm>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace clclass {
namespace test {

namespace {

const char* const kOff =
	"(cedarlogic\n  (version 3)\n  (page 0\n    (gate \"AA_TOGGLE\"\n      (uuid \"1\")\n      (at 10 20)\n      (angle 0)\n"
	"      (lparam \"OUTPUT_NUM\" \"0\"))\n    (gate \"GA_LED\"\n      (uuid \"2\")\n      (at 18 20)\n      (angle 0))))\n";

std::string headerOf(const HttpRequest& r, const std::string& name) {
	for (const auto& h : r.headers) {
		std::string n = h.first;
		for (char& c : n) c = (char)tolower((unsigned char)c);
		if (n == name) return h.second;
	}
	return std::string();
}

// The class object's sockets (class.mjs, 3.14) over the FakeServer: hello checked with the
// token, every write the clients make seen as the server would see it (a diff of the class
// before and after) and told to the sockets: pulses, pushes with the record inside, answers
// batched to the teachers, the roster's count, hand-ins, byes. One lock: the engines' threads.
class FakeHub {
public:
	FakeServer& s;
	std::recursive_mutex mu;
	bool blockAll = false;      // a network that blocks WebSockets: every upgrade fails
	bool silent = false;        // upgrades, then never answers (no hello)
	bool mutePong = false;
	bool dropAnswers = false;   // answers over the socket vanish (no "ok", nothing kept)
	int holdMs = 0;             // hold a poll that says x-cedarlogic-wait this long at most (real time; 0: never)
	int opens = 0;
	uint64_t changes = 0;       // a class's seq, live slot or answers moved
	std::condition_variable_any changed;
	struct Conn {
		std::string classId, role, sid, token;
		SocketEvents* ev = nullptr;
	};
	std::map<int, Conn> conns;
	explicit FakeHub(FakeServer& srv) : s(srv) {}

	bool open(int id, const std::string& url, SocketEvents& ev) {
		std::lock_guard<std::recursive_mutex> lock(mu);
		opens++;
		const size_t at = url.find("/classes/");
		const std::string cid = at == std::string::npos ? std::string() : url.substr(at + 9, 32);
		if (blockAll || (url.compare(0, 5, "ws://") != 0 && url.compare(0, 6, "wss://") != 0)) {
			ev.socketClosed(id, 1006);
			return true;
		}
		if (!s.classes.count(cid)) {   // the upgrade is refused (404 no_class): never a socket
			ev.socketClosed(id, 1006);
			return true;
		}
		conns[id] = { cid, "", "", "", &ev };
		ev.socketOpened(id);
		return true;
	}
	void close(int id) {
		std::lock_guard<std::recursive_mutex> lock(mu);
		conns.erase(id);
	}
	// The server closes a socket with this code (and no bye).
	void closeWith(int id, int code) {
		std::lock_guard<std::recursive_mutex> lock(mu);
		auto it = conns.find(id);
		if (it == conns.end()) return;
		SocketEvents* ev = it->second.ev;
		conns.erase(it);
		ev->socketClosed(id, code);
	}
	int connOf(const std::string& role, const std::string& sid = std::string()) {
		std::lock_guard<std::recursive_mutex> lock(mu);
		for (const auto& kv : conns)
			if (kv.second.role == role && (sid.empty() || kv.second.sid == sid)) return kv.first;
		return 0;
	}
	HttpResponse http(const HttpRequest& r) {
		std::unique_lock<std::recursive_mutex> lock(mu);
		HttpResponse resp = through(r);
		const std::string w = headerOf(r, "x-cedarlogic-wait");
		if (resp.status != 304 || w.empty() || holdMs <= 0) return resp;
		// Held until the class changes or the time is up (3.3), as the class object does.
		const uint64_t gen = changes;
		const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::min(atoi(w.c_str()) * 1000, holdMs));
		while (changes == gen && changed.wait_until(lock, end) != std::cv_status::timeout) {}
		return changes == gen ? resp : through(r);
	}
	void send(int id, const std::string& text) {
		std::lock_guard<std::recursive_mutex> lock(mu);
		auto it = conns.find(id);
		if (it == conns.end()) return;
		if (text == "ping") {
			if (!mutePong) it->second.ev->socketText(id, "pong");
			return;
		}
		json::Value m;
		if (!json::parse(text, m) || !m.isObject() || m.str("t").empty()) {
			closeWith(id, 4400);
			return;
		}
		Conn& c = it->second;
		const std::string t = m.str("t");
		if (c.role.empty()) {
			if (t != "hello") {
				closeWith(id, 4401);
				return;
			}
			if (silent) return;
			hello(id, m);
			return;
		}
		if (t == "answer" && c.role == "S") {
			if (dropAnswers) return;
			auto k = s.classes.find(c.classId);
			if (k == s.classes.end() || !k->second.roster.count(c.sid)) {
				bye(id, 403, "not_a_member");
				return;
			}
			json::Value body = json::Value::object();
			body.set("session", json::Value::string(m.str("session")));
			body.set("ver", json::Value::integer(m.integer("ver")));
			body.set("env", json::Value::string(m.str("env")));
			HttpRequest q;
			q.method = "PUT";
			q.url = std::string(FakeServer::base()) + "/classes/" + c.classId + "/live/answers/" + c.sid;
			q.headers = { { "Authorization", "Bearer " + c.token }, { "x-cedarlogic-student", c.sid } };
			q.body = json::write(body);
			const std::string sid = c.sid;
			const HttpResponse resp = through(q);
			if (!conns.count(id)) return;
			json::Value out = json::Value::object(), rb;
			json::parse(resp.body, rb);
			out.set("id", m.get("id") ? *m.get("id") : json::Value());
			if (resp.status == 200) {
				out.set("t", json::Value::string("ok"));
				out.set("at", json::Value::integer(rb.integer("at")));
			} else {
				out.set("t", json::Value::string("error"));
				out.set("status", json::Value::integer(resp.status));
				out.set("error", json::Value::string(rb.str("error")));
				out.set("message", json::Value::string(rb.str("error")));
			}
			to(id, out);
			return;
		}
		if (t == "answers" && c.role == "T") {
			auto k = s.classes.find(c.classId);
			if (k != s.classes.end() && !k->second.live.session.empty()) to(id, answersMsg(k->second, k->second.live.session, true, {}));
			return;
		}
		json::Value e = json::Value::object();
		e.set("t", json::Value::string("error"));
		e.set("id", m.get("id") ? *m.get("id") : json::Value());
		e.set("status", json::Value::integer(400));
		e.set("error", json::Value::string("bad_request"));
		to(id, e);
	}

private:
	struct Snap {
		bool exists = false;
		int64_t seq = 0, liveVer = 0;
		std::set<std::string> roster;
		std::map<std::string, std::string> answers;                     // sid -> h, the live session's
		std::map<std::string, std::map<std::string, int64_t>> subs;     // aid -> sid -> ver
	};
	Snap snap(const std::string& cid) {
		Snap x;
		auto it = s.classes.find(cid);
		if (it == s.classes.end()) return x;
		const FakeServer::Klass& k = it->second;
		x.exists = true;
		x.seq = k.seq;
		x.liveVer = k.live.rec.ver;
		for (const auto& r : k.roster) x.roster.insert(r.first);
		auto a = k.answers.find(k.live.session);
		if (a != k.answers.end())
			for (const auto& kv : a->second) x.answers[kv.first] = kv.second.h + "/" + std::to_string(kv.second.ver);
		for (const auto& as : k.subs)
			for (const auto& sb : as.second) x.subs[as.first][sb.first] = sb.second.rec.ver;
		return x;
	}
	void to(int id, const json::Value& m) {
		auto it = conns.find(id);
		if (it != conns.end()) it->second.ev->socketText(id, json::write(m));
	}
	void bye(int id, int status, const std::string& error, int64_t retryAfter = 0) {
		json::Value b = json::Value::object();
		b.set("t", json::Value::string("bye"));
		b.set("status", json::Value::integer(status));
		b.set("error", json::Value::string(error));
		b.set("message", json::Value::string(error));
		if (retryAfter) b.set("retryAfter", json::Value::integer(retryAfter));
		to(id, b);
		closeWith(id, 4000 + status);
	}
	json::Value pulse(const FakeServer::Klass& k) {
		json::Value p = json::Value::object();
		p.set("t", json::Value::string("pulse"));
		p.set("seq", json::Value::integer(k.seq));
		p.set("live", json::Value::integer(k.live.on ? k.live.rec.ver : 0));
		p.set("p", json::Value::integer(10));
		p.set("fetchKey", json::Value::string(k.fetchKey));
		return p;
	}
	json::Value liveMsg(const FakeServer::Klass& k) {
		json::Value m = pulse(k);
		m.set("t", json::Value::string("live"));
		m.set("ver", json::Value::integer(k.live.rec.ver));
		m.set("session", json::Value::string(k.live.session));
		m.set("on", json::Value::boolean(k.live.on));
		m.set("predict", json::Value::boolean(k.live.predict));
		m.set("env", json::Value::string(k.live.rec.env));
		return m;
	}
	json::Value answersMsg(const FakeServer::Klass& k, const std::string& session, bool full, const std::set<std::string>& only) {
		json::Value m = json::Value::object(), list = json::Value::array();
		m.set("t", json::Value::string("answers"));
		m.set("session", json::Value::string(session));
		m.set("full", json::Value::boolean(full));
		auto a = k.answers.find(session);
		if (a != k.answers.end())
			for (const auto& kv : a->second) {
				if (!full && !only.count(kv.first)) continue;
				json::Value e = json::Value::object();
				e.set("studentId", json::Value::string(kv.first));
				e.set("ver", json::Value::integer(kv.second.ver));
				e.set("h", json::Value::string(kv.second.h));
				e.set("env", json::Value::string(kv.second.env));
				list.push(e);
			}
		m.set("answers", list);
		return m;
	}
	void hello(int id, const json::Value& m) {
		Conn& c = conns[id];
		const std::string cid = c.classId;
		const std::string token = m.str("token"), sid = m.str("sid");
		HttpRequest q;
		q.method = "GET";
		q.url = std::string(FakeServer::base()) + "/classes/" + cid;
		q.headers = { { "Authorization", "Bearer " + token } };
		if (!sid.empty()) q.headers.emplace_back("x-cedarlogic-student", sid);
		const HttpResponse resp = s.handle(q);
		if (resp.status != 200) {
			json::Value b;
			json::parse(resp.body, b);
			bye(id, resp.status, b.str("error"));
			return;
		}
		c.role = sid.empty() ? "T" : "S";
		c.sid = sid;
		c.token = token;
		const FakeServer::Klass& k = s.classes[cid];
		json::Value h = pulse(k);
		h.set("t", json::Value::string("hello"));
		h.set("role", json::Value::string(sid.empty() ? "teacher" : "student"));
		if (sid.empty()) h.set("students", json::Value::integer((int64_t)k.roster.size()));
		to(id, h);
		if (!sid.empty() && k.live.rec.ver > m.integer("live") && !k.live.rec.env.empty()) to(id, liveMsg(k));
		if (sid.empty() && !k.live.session.empty()) to(id, answersMsg(k, k.live.session, true, {}));
	}
	HttpResponse through(const HttpRequest& r) {
		std::set<std::string> cids;
		for (const auto& kv : conns) cids.insert(kv.second.classId);
		for (const auto& kv : s.classes) cids.insert(kv.first);
		std::map<std::string, Snap> before;
		for (const std::string& cid : cids) before[cid] = snap(cid);
		const HttpResponse resp = s.handle(r);
		for (const std::string& cid : cids) {
			const Snap& b = before[cid];
			const Snap a = snap(cid);
			std::vector<int> ids;
			for (const auto& kv : conns)
				if (kv.second.classId == cid && !kv.second.role.empty()) ids.push_back(kv.first);
			if (a.seq != b.seq || a.answers != b.answers || a.exists != b.exists) {
				changes++;
				changed.notify_all();
			}
			if (b.exists && !a.exists) {   // deleted: every socket told (410), then closed
				for (int id : ids) bye(id, 410, "class_deleted");
				continue;
			}
			if (!a.exists) continue;
			const FakeServer::Klass& k = s.classes[cid];
			for (int id : ids)
				if (conns.count(id) && conns[id].role == "S" && b.roster.count(conns[id].sid) && !a.roster.count(conns[id].sid))
					bye(id, 403, "not_a_member");
			auto each = [&](const std::string& role, const json::Value& m) {
				for (int id : ids)
					if (conns.count(id) && conns[id].role == role) to(id, m);
			};
			if (a.roster.size() != b.roster.size()) {
				json::Value st = json::Value::object();
				st.set("t", json::Value::string("students"));
				st.set("students", json::Value::integer((int64_t)a.roster.size()));
				each("T", st);
			}
			if (a.seq != b.seq) {
				if (a.liveVer != b.liveVer) {
					each("S", liveMsg(k));
					each("T", pulse(k));
				} else {
					each("S", pulse(k));
					each("T", pulse(k));
				}
			}
			std::set<std::string> changed;
			for (const auto& kv : a.answers) {
				auto o = b.answers.find(kv.first);
				if (o == b.answers.end() || o->second != kv.second) changed.insert(kv.first);
			}
			if (!changed.empty()) each("T", answersMsg(k, k.live.session, false, changed));
			for (const auto& as : a.subs) {
				auto o = b.subs.find(as.first);
				if (o != b.subs.end() && o->second == as.second) continue;
				json::Value h = json::Value::object();
				h.set("t", json::Value::string("handin"));
				h.set("aid", json::Value::string(as.first));
				each("T", h);
			}
		}
		return resp;
	}
};

// One device for the Client-level tests: a Client, its LiveLink, its events queued until pumped.
struct Queue {
	struct E {
		struct LDev* d;
		int kind, id;
		std::string text;
		int code;
	};
	std::deque<E> q;
};

int gSocketIds = 0;

struct LDev : SocketEvents {
	FakeServer& s;
	FakeHub& hub;
	Queue& queue;
	std::map<std::string, std::string> files;
	std::vector<std::string> notices, urls, sent;
	std::unique_ptr<Client> c;
	SocketHooks hooks;
	std::unique_ptr<LiveLink> link;
	bool noSockets = false;
	LDev(Crypto& cr, Curve& curve, FakeServer& srv, FakeHub& h, Queue& q) : s(srv), hub(h), queue(q) {
		Config cfg;
		cfg.serverBase = FakeServer::base();
		cfg.liveBase = FakeServer::liveBase();
		cfg.client = "test/live";
		ClientHooks k;
		k.http = [this](const HttpRequest& r) {
			urls.push_back(r.method + " " + r.url);
			return hub.http(r);
		};
		k.now = [this] { return s.now; };
		k.load = [this](const std::string& n) { auto it = files.find(n); return it == files.end() ? std::string() : it->second; };
		k.save = [this](const std::string& n, const std::string& t) {
			files[n] = t;
			return true;
		};
		k.removeTree = [this](const std::string& n) {
			for (auto it = files.begin(); it != files.end();)
				it = (it->first == n || it->first.compare(0, n.size() + 1, n + "/") == 0) ? files.erase(it) : std::next(it);
		};
		k.notice = [this](const std::string& t) { notices.push_back(t); };
		k.lights = [](const std::string& cdl, const std::vector<std::string>& names, std::map<std::string, int>& out) {
			const int on = cdl.find("\"OUTPUT_NUM\" \"1\"") != std::string::npos ? 1 : 0;
			for (const std::string& n : names) out[n] = on;
			return true;
		};
		c.reset(new Client(cfg, cr, curve, k));
		hooks.newId = [] { return ++gSocketIds; };
		hooks.open = [this](int id, const std::string& url, const std::vector<std::pair<std::string, std::string>>&) {
			return !noSockets && hub.open(id, url, *this);
		};
		hooks.send = [this](int id, const std::string& text) {
			sent.push_back(text);
			hub.send(id, text);
		};
		hooks.close = [this](int id, int) { hub.close(id); };
	}
	~LDev() override {
		// Gone from the test: its socket closed at the hub, its events not delivered.
		if (link) link->abandon();
		for (auto it = queue.q.begin(); it != queue.q.end();) it = it->d == this ? queue.q.erase(it) : std::next(it);
	}
	void socketOpened(int id) override { queue.q.push_back({ this, 0, id, std::string(), 0 }); }
	void socketText(int id, const std::string& text) override { queue.q.push_back({ this, 1, id, text, 0 }); }
	void socketClosed(int id, int code) override { queue.q.push_back({ this, 2, id, std::string(), code }); }
	void startLink(const std::string& cid) {
		link.reset(new LiveLink(*c, cid, hooks, 12345u + (uint32_t)gSocketIds));
		link->start(s.now);
	}
	size_t gets(const std::string& what) const {   // the client's own requests whose "METHOD url" contains `what`
		size_t n = 0;
		for (const std::string& u : urls)
			if (u.find(what) != std::string::npos) n++;
		return n;
	}
};

void pump(Queue& queue, int64_t now) {
	for (int guard = 0; guard < 10000 && !queue.q.empty(); guard++) {
		const Queue::E e = queue.q.front();
		queue.q.pop_front();
		if (!e.d->link) continue;
		if (e.kind == 0) e.d->link->opened(e.id, now);
		else if (e.kind == 1) e.d->link->text(e.id, e.text, now);
		else e.d->link->closed(e.id, e.code, now);
	}
}

// Send My Guess the way the engine does it: over the socket, the PUT if no answer comes.
struct AnswerJob {
	Result out;
	bool done = false, viaSocket = false;
	json::Value body;
};
void sendGuess(LDev& d, const std::string& cid, const std::map<std::string, int>& lights, AnswerJob& job) {
	const Result sealed = d.c->sealAnswer(cid, lights, job.body);
	if (!sealed.ok || sealed.value == "none") {
		job.out = sealed;
		job.done = true;
		return;
	}
	json::Value msg = job.body;
	msg.set("t", json::Value::string("answer"));
	LDev* dp = &d;
	const std::map<std::string, int> l = lights;
	job.viaSocket = d.link && d.link->request(
		msg,
		[dp, cid, l, &job](const json::Value* reply) {
			job.out = reply ? dp->c->answerReply(cid, *reply, l) : dp->c->putAnswer(cid, job.body, l);
			job.done = true;
		},
		d.s.now);
	if (!job.viaSocket) {
		job.out = d.c->putAnswer(cid, job.body, lights);
		job.done = true;
	}
}

}  // namespace

void liveTests(Crypto& cr, Curve& curve, const std::string& tempDir, Report& r) {
	(void)tempDir;
	if (!r.wanted("live") && !r.wanted("s47") && !r.wanted("s48")) return;
	FakeServer s(cr);
	FakeHub hub(s);
	Queue q;
	auto line = [&](bool ok, const std::string& label, const std::string& why = std::string()) { r.line(ok, label, why); };
	LDev T(cr, curve, s, hub, q), S1(cr, curve, s, hub, q), S2(cr, curve, s, hub, q), S3(cr, curve, s, hub, q);

	const Result made = T.c->createClass("Live class");
	const std::string cid = made.value;
	const std::string code = T.c->teachingOf(cid) ? T.c->teachingOf(cid)->rec.joinCode : std::string();
	const bool joined = S1.c->join(code, "Ann").ok && S2.c->join(code, "Ben").ok;
	T.c->refreshStudents(cid);   // the proofs pinned (2.2)
	line(made.ok && joined, "live: a class, two students");

	// Hello.
	T.startLink(cid);
	S1.startLink(cid);
	S2.startLink(cid);
	line(T.link->state() == LiveLink::Connecting && S1.link->state() == LiveLink::Connecting, "live: connecting until the hello");
	pump(q, s.now);
	std::string hello;
	S1.c->helloFor(cid, hello);
	json::Value h;
	json::parse(hello, h);
	line(T.link->isOpen() && S1.link->isOpen() && S2.link->isOpen() && hub.connOf("T") && hub.connOf("S", S1.c->membershipOf(cid)->studentId),
	     "live: hello: the teacher's and the students' connections are open");
	line(h.str("token") == S1.c->membershipOf(cid)->token && h.str("sid") == S1.c->membershipOf(cid)->studentId && h.get("live") &&
	         T.sent.size() == 1 && T.sent[0].find("\"token\"") != std::string::npos && T.sent[0].find("\"sid\"") == std::string::npos,
	     "live: a student's hello has its token, sid and live; a teacher's its token only");
	line(S1.c->socketUrl(cid) == std::string("wss://fake.test/api/classroom/v1/classes/") + cid + "/socket",
	     "live: the socket's address is the API's, wss://");

	// s47: a push reaches the students with the record inside: no pulse, no record fetch, no status read.
	size_t before1 = S1.urls.size(), before2 = S2.urls.size();
	const Result g = T.c->goLive(cid, kOff);
	pump(q, s.now);
	Live l1 = S1.c->live(cid);
	line(g.ok && l1.on && l1.cdl == kOff && l1.step == 1 && S2.c->live(cid).on && S1.urls.size() == before1 && S2.urls.size() == before2,
	     "s47 Go Live reaches both students over the socket, record inside: no request at all");
	const std::string on = std::string(kOff).replace(std::string(kOff).find("\"0\""), 3, "\"1\"");
	before1 = S1.urls.size();
	T.c->push(cid, on, nullptr, nullptr, false);
	pump(q, s.now);
	l1 = S1.c->live(cid);
	line(l1.on && l1.step == 2 && l1.cdl == on && S1.urls.size() == before1 && S1.c->membershipOf(cid)->seq == s.classes[cid].seq,
	     "s47 a second push: the next step, still no request; seq kept by one");

	// Any other teacher write: the pulse, and the status read for what it changed.
	before1 = S1.urls.size();
	const size_t statusBefore = S1.gets("GET " + std::string(FakeServer::base()) + "/classes/" + cid);
	Assignment a;
	a.title = "Lab 1";
	a.cdl = kOff;
	const Result posted = T.c->postAssignment(cid, a, true);
	pump(q, s.now);
	line(posted.ok && S1.c->assignments(cid).size() == 1 && S1.gets("GET " + std::string(FakeServer::base()) + "/classes/" + cid) == statusBefore + 1 &&
	         S1.urls.size() > before1,
	     "live: an assignment posted: the pulse moves seq alone, the student reads its status and the record");

	// Predict: answers over the socket, batched to the teacher.
	const std::string prompt = "What will the LED show?";
	const std::vector<std::string> lights = { "LED" };
	T.c->push(cid, kOff, &prompt, &lights, false);
	pump(q, s.now);
	before1 = S1.urls.size();
	const size_t tBefore = T.urls.size();
	AnswerJob j1;
	sendGuess(S1, cid, { { "LED", 1 } }, j1);
	pump(q, s.now);
	AnswerCounts c = T.c->answers(cid);
	line(j1.viaSocket && j1.done && j1.out.ok && S1.urls.size() == before1 && S1.sent.back().find("\"t\":\"answer\"") != std::string::npos &&
	         S1.sent.back().find("\"id\":") != std::string::npos,
	     "s47 an answer goes over the socket with an id; its \"ok\" comes back: no PUT");
	line(c.answered == 1 && c.perLight["LED"] == std::make_pair(1, 0) && T.urls.size() == tBefore,
	     "s47 the teacher's count comes over its socket: 1 answered, no answers request");
	// No "ok" within 8 s: the PUT.
	hub.dropAnswers = true;
	AnswerJob j2;
	sendGuess(S2, cid, { { "LED", 0 } }, j2);
	pump(q, s.now);
	const bool waiting = !j2.done && S2.link->nextTimer() <= s.now + 8 * kSecond;
	s.now += 8 * kSecond;
	S2.link->tick(s.now);
	pump(q, s.now);
	hub.dropAnswers = false;
	c = T.c->answers(cid);
	line(waiting && j2.done && j2.out.ok && S2.gets("PUT") == 1 && c.answered == 2 && c.perLight["LED"] == std::make_pair(1, 1),
	     "live: no answer within 8 s: sent again with PUT; the teacher counts it from its socket");
	// A reveal scores the answers already counted, without asking for them again.
	const size_t tBefore2 = T.urls.size();
	T.c->push(cid, on, &prompt, &lights, true);
	c = T.c->answers(cid);
	line(c.right == 1 && c.wrong == 1 && T.gets("/live/answers") == 0 && T.urls.size() == tBefore2 + 1,
	     "live: a reveal scores the answers kept: 1 right, 1 wrong, no answers request");
	pump(q, s.now);
	// The answers' error from the socket: an answer to a session gone is dropped quietly.
	{
		json::Value err = json::Value::object();
		err.set("t", json::Value::string("error"));
		err.set("status", json::Value::integer(409));
		err.set("error", json::Value::string("not_live"));
		const Result dropped = S1.c->answerReply(cid, err, { { "LED", 1 } });
		line(dropped.ok && dropped.value == "dropped", "live: a socket's 409 not_live: dropped quietly, as the PUT's");
	}

	// A teacher's hello brings the session's whole answers list.
	T.link->stop();
	T.c->answers(cid);
	T.startLink(cid);
	pump(q, s.now);
	c = T.c->answers(cid);
	line(T.link->isOpen() && c.answered == 2 && T.sent.back().find("\"session\"") != std::string::npos,
	     "live: a teacher's hello names its session and gets the whole answers list (full)");

	// Hand-ins and the roster's count reach the teacher's socket.
	const std::string aid = T.c->assignments(cid).empty() ? std::string() : T.c->assignments(cid)[0].id;
	S1.c->handIn(cid, aid, on);
	pump(q, s.now);
	std::string hc, ha;
	const bool handin = T.c->handinFlag(hc, ha) && hc == cid && ha == aid;
	const bool joined3 = S3.c->join(code, "Cy").ok;
	pump(q, s.now);
	line(handin && joined3 && T.c->teachingOf(cid)->students == 3, "live: the teacher hears of a hand-in and of a new student (3)");

	// Pings every 45 s; a ping without a pong closes the socket, and it comes back.
	size_t pings = 0;
	for (int i = 0; i < 2; i++) {
		s.now += 45 * kSecond;
		S1.link->tick(s.now);
		pump(q, s.now);
		pings += S1.sent.back() == "ping";
	}
	hub.mutePong = true;
	s.now += 45 * kSecond;
	S1.link->tick(s.now);
	pump(q, s.now);
	s.now += 45 * kSecond;
	S1.link->tick(s.now);
	const bool dropped = !S1.link->isOpen() && S1.link->state() == LiveLink::Connecting;
	hub.mutePong = false;
	s.now += 1300;   // 1 s +-20 %
	S1.link->tick(s.now);
	pump(q, s.now);
	line(pings == 2 && dropped && S1.link->isOpen(), "live: a ping every 45 s; one with no pong closes it; back after 1 s");

	// Closing codes.
	{
		const int id = hub.connOf("S", S1.c->membershipOf(cid)->studentId);
		hub.closeWith(id, 4408);   // no hello in time (as the server sees it)
		pump(q, s.now);
		const bool again = S1.link->state() == LiveLink::Connecting && S1.link->nextTimer() <= s.now + 1200;
		s.now += 1300;
		S1.link->tick(s.now);
		pump(q, s.now);
		line(again && S1.link->isOpen(), "live: 4408: reconnected after 1 s");
		// 4429 with its bye: not before retryAfter.
		const int id2 = hub.connOf("S", S1.c->membershipOf(cid)->studentId);
		json::Value bye = json::Value::object();
		bye.set("t", json::Value::string("bye"));
		bye.set("status", json::Value::integer(429));
		bye.set("error", json::Value::string("rate_limited"));
		bye.set("retryAfter", json::Value::integer(60));
		hub.conns[id2].ev->socketText(id2, json::write(bye));
		hub.closeWith(id2, 4429);
		pump(q, s.now);
		s.now += 30 * kSecond;
		S1.link->tick(s.now);
		pump(q, s.now);
		const bool waited = !S1.link->isOpen();
		s.now += 31 * kSecond;
		S1.link->tick(s.now);
		pump(q, s.now);
		line(waited && S1.link->isOpen(), "live: 4429: back only after retryAfter (60 s)");
		// A stale pulse on the socket is ignored.
		const int64_t seq = S1.c->membershipOf(cid)->seq;
		json::Value old = json::Value::object();
		old.set("t", json::Value::string("pulse"));
		old.set("seq", json::Value::integer(seq - 1));
		old.set("live", json::Value::integer(1));
		const size_t n = S1.urls.size();
		const int id3 = hub.connOf("S", S1.c->membershipOf(cid)->studentId);
		hub.conns[id3].ev->socketText(id3, json::write(old));
		pump(q, s.now);
		line(S1.c->membershipOf(cid)->seq == seq && S1.urls.size() == n && S1.c->live(cid).on, "live: a pulse lower than seen is ignored");
	}

	// A removal: the student's socket hears bye 403 and ends with its membership.
	{
		const std::string sid2 = S2.c->membershipOf(cid)->studentId;
		T.c->removeStudents(cid, { sid2 }, false);
		pump(q, s.now);
		const bool forgot = !S2.c->membershipOf(cid) && !S2.notices.empty() && S2.notices.back().find("You were removed from") == 0;
		s.now += 10 * kMinute;
		S2.link->tick(s.now);
		pump(q, s.now);
		line(forgot && S2.link->state() == LiveLink::Closed && !hub.connOf("S", sid2) && S2.link->nextTimer() == INT64_MAX,
		     "s47 removed: bye 403, the membership forgotten, no reconnecting");
		line(S1.c->membershipOf(cid) && S1.c->membershipOf(cid)->fetchKey == s.classes[cid].fetchKey,
		     "live: the others take the new fetchKey from the socket's pulse");
	}

	// 4410 without a bye: the class is forgotten as after the bye.
	{
		const int id = hub.connOf("S", S3.c->membershipOf(cid) ? S3.c->membershipOf(cid)->studentId : std::string());
		const bool none = id == 0;   // S3 has no link yet
		S3.startLink(cid);
		pump(q, s.now);
		const int id3 = hub.connOf("S", S3.c->membershipOf(cid)->studentId);
		hub.closeWith(id3, 4410);
		pump(q, s.now);
		line(none && id3 && !S3.c->membershipOf(cid) && S3.link->state() == LiveLink::Closed && !S3.notices.empty() &&
		         S3.notices.back().find("was deleted by the teacher") != std::string::npos,
		     "live: closing code 4410 with no bye: the class forgotten, \"deleted by the teacher\"");
	}

	// A wrong token: bye 401, no reconnecting.
	{
		LDev X(cr, curve, s, hub, q);
		X.c->join(code, "Dee");
		X.c->membershipOf(cid)->token = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
		X.startLink(cid);
		pump(q, s.now);
		s.now += 2 * kMinute;
		X.link->tick(s.now);
		line(X.link->state() == LiveLink::Closed && X.link->nextTimer() == INT64_MAX, "live: a hello that doesn't check out: 4401, ended");
	}

	// s48: a network that blocks WebSockets: three tries, then held polls; the socket every 5 minutes.
	{
		LDev Y(cr, curve, s, hub, q);
		Y.c->join(code, "Eve");
		Y.c->pulse(cid);   // the pulse's ETag known
		hub.blockAll = true;
		Y.startLink(cid);
		pump(q, s.now);
		const bool first = Y.link->state() == LiveLink::Connecting;
		for (int i = 0; i < 3; i++) {
			s.now += 2500;
			Y.link->tick(s.now);
			pump(q, s.now);
		}
		const int opens = hub.opens;
		const bool fell = Y.link->state() == LiveLink::Fallback && Y.link->nextTimer() >= s.now + 240 * kSecond;
		line(first && fell, "s48 three tries without a hello: the fallback; the socket again in 5 minutes (+-20 %)");
		// Held polls: the push answers them; the seq-by-one rule spares the status.
		s.waits.clear();
		T.c->push(cid, kOff, nullptr, nullptr, false);
		pump(q, s.now);
		const size_t n = Y.urls.size();
		Y.c->pulse(cid, Y.c->holdSeconds());
		const bool held = !s.waits.empty() && s.waits.back().find("wait 25") != std::string::npos &&
		                  s.waits.back().find("no If-None-Match") == std::string::npos;
		line(held && Y.c->live(cid).on && Y.c->live(cid).cdl == kOff && Y.urls.size() - n == 2 && Y.gets("/live/") >= 1,
		     "s48 a held pulse (x-cedarlogic-wait 25 with If-None-Match) brings the push: the pulse and the record, no status");
		const size_t m = Y.urls.size();
		Y.c->pulse(cid, Y.c->holdSeconds());
		line(Y.urls.size() - m == 1 && s.classes[cid].seq == Y.c->membershipOf(cid)->seq, "s48 nothing new: 304, one request");
		// End Live over held polls: ended without a status read either.
		const size_t e = Y.urls.size();
		T.c->endLive(cid);
		Y.c->pulse(cid, Y.c->holdSeconds());
		line(Y.c->live(cid).ended && !Y.c->live(cid).on && Y.urls.size() - e == 1, "s48 End Live over a held pulse: ended, no status read");
		hub.blockAll = false;
		s.now += 6 * kMinute;
		Y.link->tick(s.now);
		pump(q, s.now);
		line(hub.opens == opens + 1 && Y.link->isOpen(), "s48 five minutes on the socket is tried again, and opens");
		// A platform without WebSockets: the fallback at once, never tried again.
		LDev Z(cr, curve, s, hub, q);
		Z.c->join(code, "Fay");
		Z.noSockets = true;
		Z.startLink(cid);
		line(Z.link->state() == LiveLink::Fallback && Z.link->unsupported() && Z.link->nextTimer() == INT64_MAX,
		     "live: no WebSockets on the platform: held polls from the start");
		// No hello in 15 s: a failed try.
		hub.silent = true;
		LDev W(cr, curve, s, hub, q);
		W.c->join(code, "Gus");
		W.startLink(cid);
		pump(q, s.now);
		s.now += 15 * kSecond;
		W.link->tick(s.now);
		hub.silent = false;
		line(W.link->state() == LiveLink::Connecting && !W.link->isOpen() && W.link->nextTimer() > s.now, "live: no hello within 15 s: a failed try");
	}

	// Deleted: every socket hears bye 410.
	T.c->deleteClass(cid);
	pump(q, s.now);
	line(!S1.c->membershipOf(cid) && S1.link->state() == LiveLink::Closed, "live: the class deleted: bye 410, the class forgotten");
	// Where the service is (3.2, 13): the placeholder until the deploy; ws:// beside an http:// base;
	// revision 2's addresses answer 410 moved.
	{
		Config d;
		ClientHooks k;
		k.http = [](const HttpRequest&) {
			HttpResponse x;
			x.sent = true;
			x.status = 410;
			x.body = "{\"error\":\"moved\",\"message\":\"CedarLogic Classroom has moved. Update CedarLogic.\"}";
			return x;
		};
		Client old(d, cr, curve, k);
		const Result moved = old.createClass("Old");
		Config local;
		local.serverBase = "http://localhost:8788/api/classroom/v1";
		Client near(local, cr, curve, ClientHooks());
		line(d.serverBase == "https://cedarlogic-classroom.leviholliday7.workers.dev/api/classroom/v1" && d.liveBase == "https://cedarlogic-classroom.leviholliday7.workers.dev/api/live/v1" &&
		         near.socketUrl(cid) == "ws://localhost:8788/api/classroom/v1/classes/" + cid + "/socket",
		     "live: the service's address; ws:// for a local http:// server");
		line(!moved.ok && moved.message == "CedarLogic Classroom has moved. Update CedarLogic.", "live: 410 moved: \"Update CedarLogic.\"", moved.message);
	}
}

// ---- the threaded engine with sockets --------------------------------------------------------------

namespace {

// A Host for one engine: the transport (HTTP and sockets) given, everything else in memory,
// the UI thread being whichever thread asks. Counts its own requests by kind.
struct NetHost : Host {
	std::function<HttpResponse(const HttpRequest&)> transport;
	std::function<bool(int, const std::string&, const std::vector<std::pair<std::string, std::string>>&, SocketEvents&)> openFn;
	std::function<void(int, const std::string&)> sendFn;
	std::function<void(int, int)> closeFn;
	bool blockSockets = false;   // a network that blocks WebSockets: every upgrade fails at once
	std::string liveBase;
	std::mutex mu, filesMu, countMu;
	std::map<std::string, std::string> files;
	std::vector<std::string> notices;
	std::map<std::string, Live> lives;
	std::map<std::string, AnswerCounts> counts;
	std::map<std::string, int> kinds;   // pulse, held, record, status, answerPut, answersGet
	int handins = 0;
	int count(const std::string& k) {
		std::lock_guard<std::mutex> lock(countMu);
		return kinds[k];
	}
	HttpResponse http(const HttpRequest& r) override {
		{
			std::lock_guard<std::mutex> lock(countMu);
			const bool live = r.url.compare(0, liveBase.size(), liveBase) == 0;
			if (live && r.url.find("/live/") == std::string::npos && r.url.find("/assignment/") == std::string::npos &&
			    r.url.find("/info/") == std::string::npos)
				kinds["pulse"]++;
			if (live && r.url.find("/live/") != std::string::npos) kinds["record"]++;
			if (!headerOf(r, "x-cedarlogic-wait").empty()) kinds["held"]++;
			if (r.method == "PUT" && r.url.find("/live/answers/") != std::string::npos) kinds["answerPut"]++;
			if (r.method == "GET" && r.url.find("/live/answers?") != std::string::npos) kinds["answersGet"]++;
			const size_t at = r.url.find("/classes/");
			if (r.method == "GET" && !live && at != std::string::npos && r.url.size() == at + 9 + 32) kinds["status"]++;
		}
		return transport(r);
	}
	void onMain(const std::function<void()>& fn) override {
		std::lock_guard<std::mutex> lock(mu);
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
	bool tryLock(const std::string&) override { return true; }
	void unlock() override {}
	void classesChanged() override {}
	void liveChanged(const std::string& classId, const Live& l) override { lives[classId] = l; }   // (under mu)
	void answersChanged(const std::string& classId, const AnswerCounts& c) override { counts[classId] = c; }
	void statusChanged(const std::string&, const Status&) override {}
	void notice(const std::string& t) override { notices.push_back(t); }
	std::vector<std::pair<std::string, std::string>> syncSideRecords() override { return {}; }
	void syncPutSide(const std::string&, const std::string&) override {}
	void syncDeleteSide(const std::string&) override {}
	void submissionsChanged(const std::string&, const std::string&) override { handins++; }
	bool socketOpen(int id, const std::string& url, const std::vector<std::pair<std::string, std::string>>& headers,
	                SocketEvents& events) override {
		if (!openFn) return false;
		if (blockSockets) {
			events.socketClosed(id, 1006);
			return true;
		}
		return openFn(id, url, headers, events);
	}
	void socketSend(int id, const std::string& text) override {
		if (sendFn) sendFn(id, text);
	}
	void socketClose(int id, int code) override {
		if (closeFn) closeFn(id, code);
	}
	Live liveOf(const std::string& cid) {
		std::lock_guard<std::mutex> lock(mu);
		return lives.count(cid) ? lives[cid] : Live();
	}
	AnswerCounts countsOf(const std::string& cid) {
		std::lock_guard<std::mutex> lock(mu);
		return counts.count(cid) ? counts[cid] : AnswerCounts();
	}
	std::string lastNotice() {
		std::lock_guard<std::mutex> lock(mu);
		return notices.empty() ? std::string() : notices.back();
	}
};

struct Wait {
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
	bool wait(double seconds = 30) {
		std::unique_lock<std::mutex> lock(mu);
		return cv.wait_for(lock, std::chrono::milliseconds((int)(seconds * 1000)), [this] { return done; }) && ok;
	}
	Done fn() {
		return [this](bool k, const std::string& m) { set(k, m); };
	}
	std::function<void(bool, std::string, std::string)> fn3() {
		return [this](bool k, std::string m, std::string v) { set(k, m, v); };
	}
};

template <typename F>
bool until(F f, double seconds) {
	const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds((int)(seconds * 1000));
	while (std::chrono::steady_clock::now() < end) {
		if (f()) return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	return f();
}

double since(std::chrono::steady_clock::time_point t0) {
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

// One class through engines on a server, live: the teacher and two students on sockets (if the
// transport has them) and a student whose network blocks them. `prefix` names the checks.
void liveRound(Crypto& cr, Curve& curve, Report& r, const std::string& prefix, const Config& base,
               const std::function<void(NetHost&)>& wire, bool socketsExpected, double holdLimit) {
	NetHost th, s1h, s2h, s3h;
	for (NetHost* h : { &th, &s1h, &s2h, &s3h }) {
		h->liveBase = base.liveBase;
		wire(*h);
	}
	s2h.blockSockets = true;
	Config cfg = base;
	cfg.client = "selftest/live";
	auto line = [&](bool ok, const std::string& label, const std::string& why = std::string()) { r.line(ok, prefix + label, why); };
	{
		Engine T(cfg, cr, curve, th), S1(cfg, cr, curve, s1h), S2(cfg, cr, curve, s2h), S3(cfg, cr, curve, s3h);
		for (Engine* e : { &T, &S1, &S2, &S3 }) e->start();
		Wait made;
		T.createClass("Live round", made.fn3());
		if (!made.wait()) {
			line(false, "create", made.message);
			return;
		}
		const std::string cid = made.value;
		const std::string code = T.classes().empty() ? std::string() : T.classes()[0].joinCode;
		Wait j1, j2, j3;
		S1.join(code, "Ann", j1.fn3());
		S2.join(code, "Ben", j2.fn3());
		S3.join(code, "Cy", j3.fn3());
		Wait roster;
		const bool joined = j1.wait() && j2.wait() && j3.wait();
		T.refreshStudents(cid, roster.fn());
		line(joined && roster.wait() && T.students(cid).size() == 3, "create; three students join");
		for (Engine* e : { &T, &S1, &S2, &S3 }) e->pageOpen(cid, true);
		const std::string open = socketsExpected ? "open" : "fallback";
		const bool opened = until([&] { return T.liveConnection(cid) == open && S1.liveConnection(cid) == open && S3.liveConnection(cid) == open; }, 20);
		const auto tBlocked = std::chrono::steady_clock::now();
		const bool fell = until([&] { return S2.liveConnection(cid) == "fallback"; }, 20);
		line(opened, std::string("the class pages' live connections: ") + open, T.liveConnection(cid) + "/" + S1.liveConnection(cid));
		line(fell, "a network that blocks WebSockets: three tries, then the fallback",
		     S2.liveConnection(cid) + std::to_string(since(tBlocked)) + " s");

		// Go Live: on sockets the record comes inside (no pulse, record or status). The blocked
		// student, outside a session, polls every 60 s: the app coming to the front asks at once.
		const int s1Pulse = s1h.count("pulse"), s1Record = s1h.count("record"), s1Status = s1h.count("status");
		Wait live;
		const auto t0 = std::chrono::steady_clock::now();
		T.goLive(cid, kOff, live.fn());
		const bool reached1 = live.wait() && until([&] { return s1h.liveOf(cid).on && s1h.liveOf(cid).cdl == kOff; }, 10);
		const double at1 = since(t0);
		if (socketsExpected)
			line(reached1 && s1h.count("pulse") == s1Pulse && s1h.count("record") == s1Record && s1h.count("status") == s1Status,
			     "s47 Go Live reaches a socket's student with the record inside: no pulse, record or status request",
			     std::to_string(at1) + " s");
		S2.appActivated();
		if (!socketsExpected) {   // no student has a socket here
			S1.appActivated();
			S3.appActivated();
		}
		const bool reached2 = until([&] {
			return s2h.liveOf(cid).cdl == kOff && s1h.liveOf(cid).cdl == kOff && s3h.liveOf(cid).cdl == kOff;
		}, 10);
		line(reached2, "a student without a socket sees the session when its app comes to the front");

		// During the session the blocked student's polls are held: the next push answers one at once,
		// and the seq-by-one rule spares the status.
		std::this_thread::sleep_for(std::chrono::milliseconds(2500));   // its held poll is out by now
		const int s2Status = s2h.count("status"), s2Held = s2h.count("held");
		const std::string prompt = "LED?";
		const std::vector<std::string> lights = { "LED" };
		Wait pushed;
		const auto t1 = std::chrono::steady_clock::now();
		T.push(cid, kOff, &prompt, &lights, false, pushed.fn());
		const bool asked = pushed.wait() && until([&] { return s2h.liveOf(cid).hasPredict; }, holdLimit);
		const double at2 = since(t1);
		line(asked && at2 < 3 && s2Held > 0 && s2h.count("status") == s2Status,
		     "s48 the held pulse brings the push at once, no status read", std::to_string(at2) + " s, " + std::to_string(s2Held) + " held");
		const bool askedAll = until([&] { return s1h.liveOf(cid).hasPredict && s3h.liveOf(cid).hasPredict; }, 10);

		// Predict: S1 and S3 answer over their sockets, S2 with PUT; the teacher counts all three.
		Wait a1, a2, a3;
		S1.sendAnswer(cid, { { "LED", 1 } }, a1.fn());
		S2.sendAnswer(cid, { { "LED", 0 } }, a2.fn());
		S3.sendAnswer(cid, { { "LED", 1 } }, a3.fn());
		const bool sent = a1.wait() && a2.wait() && a3.wait();
		const bool counted = until([&] { if (getenv("CL_LIVE_DEBUG")) { static int n = 0; if (++n % 50 == 0) fprintf(stderr, "DEBUG answered=%d gets=%d held=%d\n", T.answers(cid).answered, th.count("answersGet"), th.count("held")); } return T.answers(cid).answered == 3; }, holdLimit);
		const AnswerCounts c = T.answers(cid);
		line(askedAll && sent && counted && c.perLight.count("LED") && c.perLight.at("LED") == std::make_pair(2, 1) &&
		         s2h.count("answerPut") == 1,
		     "predict: three answers counted (LED 1 (2) 0 (1)); the blocked student's by PUT",
		     std::to_string(c.answered) + " answered");
		if (socketsExpected)
			line(s1h.count("answerPut") == 0 && s3h.count("answerPut") == 0 && th.count("answersGet") == 0,
			     "s47 the socket students' answers went over their sockets, and reached the teacher's: no PUT, no answers request");

		// A removal ends S1's membership (on its socket: bye 403).
		Wait removed;
		const std::vector<Student> st = T.students(cid);
		std::string sid1;
		for (const Student& x : st)
			if (x.name == "Ann") sid1 = x.studentId;
		T.removeStudents(cid, std::vector<std::string>{ sid1 }, false, removed.fn());
		const bool gone1 = removed.wait() && until([&] { return S1.classes().empty(); }, socketsExpected ? 10 : 30);
		line(gone1 && s1h.lastNotice().find("You were removed from") == 0, "a removed student's device forgets the class at once",
		     s1h.lastNotice());

		// The class deleted: S3's socket hears 410; the blocked S2 learns it from its next poll.
		Wait deleted;
		T.deleteClass(cid, deleted.fn());
		const bool gone3 = deleted.wait() && until([&] { return S3.classes().empty(); }, socketsExpected ? 10 : 30);
		line(gone3 && s3h.lastNotice().find("was deleted by the teacher") != std::string::npos,
		     socketsExpected ? "the class deleted: a socket's student forgets it at once" : "the class deleted: a student forgets it",
		     s3h.lastNotice());
		const bool gone2 = until([&] { return S2.classes().empty(); }, holdLimit + 5);
		line(gone2, "the class deleted: the fallback student forgets it by its held poll");
	}
}

}  // namespace

void engineLiveTests(Crypto& cr, Curve& curve, const std::string& tempDir, Report& r) {
	(void)tempDir;
	if (!r.wanted("engine live")) return;
	FakeServer s(cr);
	s.now = clsync::SystemClock().now();
	FakeHub hub(s);
	hub.holdMs = 4000;
	Config cfg;
	cfg.serverBase = FakeServer::base();
	cfg.liveBase = FakeServer::liveBase();
	liveRound(cr, curve, r, "engine live: ", cfg,
	          [&hub](NetHost& h) {
		          h.transport = [&hub](const HttpRequest& q) { return hub.http(q); };
		          h.openFn = [&hub](int id, const std::string& url, const std::vector<std::pair<std::string, std::string>>&,
		                            SocketEvents& ev) { return hub.open(id, url, ev); };
		          h.sendFn = [&hub](int id, const std::string& text) { hub.send(id, text); };
		          h.closeFn = [&hub](int id, int) { hub.close(id); };
	          },
	          true, 6);
}

void liveServerTests(Crypto& cr, Curve& curve, Report& r, Host& host, const std::string& serverBase, const std::string& liveBase) {
	if (!r.wanted("server live")) return;
	// Does this platform have WebSockets? Ask for one to nowhere useful and see.
	struct Probe : SocketEvents {
		void socketOpened(int) override {}
		void socketText(int, const std::string&) override {}
		void socketClosed(int, int) override {}
	} probe;
	const bool sockets = host.socketOpen(999999, "ws://127.0.0.1:9/", {}, probe);
	if (sockets) host.socketClose(999999, 1000);
	Config cfg;
	cfg.serverBase = serverBase;
	cfg.liveBase = liveBase;
	if (const char* k = getenv("CL_APP_KEY")) cfg.appKey = k;
	liveRound(cr, curve, r, std::string("server live (") + (sockets ? "sockets" : "no sockets: held polls") + "): ", cfg,
	          [&host](NetHost& h) {
		          h.transport = [&host](const HttpRequest& q) { return host.http(q); };
		          h.openFn = [&host](int id, const std::string& url, const std::vector<std::pair<std::string, std::string>>& hd,
		                             SocketEvents& ev) { return host.socketOpen(id, url, hd, ev); };
		          h.sendFn = [&host](int id, const std::string& text) { host.socketSend(id, text); };
		          h.closeFn = [&host](int id, int code) { host.socketClose(id, code); };
	          },
	          sockets, 30);
}

}  // namespace test
}  // namespace clclass
