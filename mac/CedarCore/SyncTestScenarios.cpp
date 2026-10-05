// The protocol scenarios of SYNC.md 7.2 for the C++ engine: ports of the
// design's ref/sim.py scenarios (names match), on the FakeServer with app
// clients on real temporary library folders and "web" clients (copies, v1
// XML, no versions) in memory -- plus the client-only rows (22, 24) and, with
// a mock server, a set of them again over HTTP.

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS   // snprintf/sscanf/localtime/getenv are used with care
#endif

#include "SyncTest.h"
#include "SyncTestServer.h"

#include "circuit_file_io.hpp"
#include "legacy_cdl.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace clsync {
namespace test {

namespace {

struct Fail : std::runtime_error {
	explicit Fail(const std::string& m) : std::runtime_error(m) {}
};

#define CHECK(cond)                                                                       \
	do {                                                                                  \
		if (!(cond)) throw Fail(std::string(#cond) + " (line " + std::to_string(__LINE__) + ")"); \
	} while (0)
#define CHECK_MSG(cond, msg)                                                                                  \
	do {                                                                                                      \
		if (!(cond)) throw Fail(std::string(#cond) + " (line " + std::to_string(__LINE__) + "): " + (msg)); \
	} while (0)

struct FakeClock : Clock {
	int64_t t = 1759500000000LL;
	int64_t now() override { return t; }
	int64_t tick(int64_t ms = 1000) { return t += ms; }
};

struct SkewClock : Clock {
	FakeClock& base;
	int64_t skew;
	SkewClock(FakeClock& b, int64_t s) : base(b), skew(s) {}
	int64_t now() override { return base.t + skew; }
};

std::string CDL() { return fixture("vector-v3.cdl"); }

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
	for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
	return s;
}

// A different circuit: the LED moved to x = n.
std::string cdlWith(int n) { return replaceAll(CDL(), "(at 18 20)", "(at " + std::to_string(n) + " 20)"); }
std::string cdlSwitch(bool on, const std::string& base = CDL()) {
	return replaceAll(base, "(lparam \"OUTPUT_NUM\" \"0\")", std::string("(lparam \"OUTPUT_NUM\" \"") + (on ? "1" : "0") + "\")");
}

std::string num(double v) {
	char buf[64];
	if (std::floor(v) == v && std::fabs(v) < 1e15) snprintf(buf, sizeof buf, "%lld", (long long)v);
	else snprintf(buf, sizeof buf, "%.17g", v);
	return buf;
}

// v3 text -> v1 XML in the shape CedarLogic Online writes (the design's ref/xmlcdl.py):
// every default param written out, the angle as a gparam. Anything else is returned as is.
std::string toXml(const std::string& v3) {
	const std::string t = normalizeCdl(v3);
	if (cl::detectFormat(t) != cl::SourceFormat::SexprV3) return v3;
	cl::CircuitFile cf;
	try {
		cf = cl::readCircuitFile(t);
	} catch (const std::exception&) {
		return v3;
	}
	std::vector<std::string> out = { "<circuit>", "<CurrentPage>0</CurrentPage>" };
	for (size_t pi = 0; pi < cf.pages.size(); pi++) {
		const cl::Page& pg = cf.pages[pi];
		const std::string idx = std::to_string(pg.index);
		out.push_back("<page " + idx + ">");
		out.push_back("<PageViewport>0,10,10,0</PageViewport>");
		for (const cl::GateInstance& g : pg.gates) {
			std::vector<std::pair<std::string, std::string>> gp, lp;
			for (const auto& d : gateDefaultList(g.libName)) (std::get<0>(d) ? gp : lp).emplace_back(std::get<1>(d), std::get<2>(d));
			for (const cl::Param& p : g.params) {
				auto& list = p.gui ? gp : lp;
				bool found = false;
				for (auto& kv : list)
					if (kv.first == p.name) { kv.second = p.value; found = true; }
				if (!found) list.emplace_back(p.name, p.value);
			}
			out.push_back("<gate>");
			out.push_back("<ID>" + g.uuid + "</ID>");
			out.push_back("<type>" + g.libName + "</type>");
			out.push_back("<position>" + num(g.at.x) + "," + num(g.at.y) + "</position>");
			for (const auto& kv : gp) out.push_back("<gparam>" + kv.first + " " + replaceAll(kv.second, "<", "\x07") + "</gparam>");
			char angle[64];
			snprintf(angle, sizeof angle, "<gparam>angle %.1f</gparam>", g.angle);
			out.push_back(angle);
			for (const auto& kv : lp) out.push_back("<lparam>" + kv.first + " " + replaceAll(kv.second, "<", "\x07") + "</lparam>");
			out.back() += "</gate>";
		}
		for (const cl::WireInstance& w : pg.wires) {
			std::string ids;
			for (const std::string& id : w.ids) ids += (ids.empty() ? "" : " ") + id;
			out.push_back("<wire>");
			out.push_back("<ID>" + ids + "</ID>");
			out.push_back("<shape>");
			for (const cl::WireSegment& s : w.segments) {
				const std::string tag = s.vertical ? "vsegment" : "hsegment";
				out.push_back("<" + tag + ">");
				out.push_back("<ID>" + s.id + "</ID>");
				out.push_back("<points>" + num(s.begin.x) + "," + num(s.begin.y) + "," + num(s.end.x) + "," + num(s.end.y) + "</points>");
				for (const cl::WireConn& c : s.connects) {
					out.push_back("<connection>");
					out.push_back("<GID>" + c.gateUuid + "</GID>");
					out.push_back("<name>" + c.pin + "</name></connection>");
				}
				out.back() += "</" + tag + ">";
			}
			out.back() += "</shape></wire>";
		}
		out.push_back(pi + 1 == cf.pages.size() ? "</page " + idx + "></circuit>" : "</page " + idx + ">");
	}
	if (out.back().size() < 10 || out.back().compare(out.back().size() - 10, 10, "</circuit>") != 0) out.push_back("</circuit>");
	std::string text;
	for (const std::string& l : out) text += l + "\n";
	return text;
}

// ---- the network between one client and the server ----------------------------------------------

struct Net {
	FakeServer* server = nullptr;
	Host* real = nullptr;
	bool down = false;
	int loseAnswers = 0;
	int failAfter = -1;   // this many more requests get through, then the connection drops
	std::function<void(const std::string&)> before;
	size_t calls = 0;
	std::vector<std::string> log;
	std::vector<int64_t> sinces;   // each changes request's `since`
	// The mock server: its clock pinned to the test's (x-mock-now), its rate limits keyed by
	// this world's own address (x-mock-ip).
	FakeClock* mockClock = nullptr;
	std::string mockIp;

	static std::string opName(const HttpRequest& r) {
		const size_t at = r.url.find("/spaces/");
		if (at == std::string::npos) return "other";
		const std::string rest = r.url.substr(at + 8 + 32);
		if (rest.empty()) return r.method == "PUT" ? "put_space" : r.method == "DELETE" ? "delete_space" : "get_space";
		if (rest.compare(0, 8, "/changes") == 0) return "changes";
		if (rest.compare(0, 6, "/fetch") == 0) return "fetch";
		if (rest.compare(0, 6, "/write") == 0) return "write";
		return "other";
	}
	HttpResponse call(const HttpRequest& r) {
		calls++;
		const std::string name = opName(r);
		log.push_back(name);
		if (name == "changes") {
			const size_t at = r.url.find("since=");
			sinces.push_back(at == std::string::npos ? 0 : atoll(r.url.c_str() + at + 6));
		}
		if (failAfter >= 0) {
			if (failAfter == 0) {
				down = true;
				failAfter = -1;
			} else {
				failAfter--;
			}
		}
		if (down) return HttpResponse();
		if (before) before(name);
		HttpResponse resp;
		if (server) {
			resp = server->handle(r);
		} else {
			HttpRequest m = r;
			if (mockClock) m.headers.emplace_back("x-mock-now", std::to_string(mockClock->t));
			if (!mockIp.empty()) m.headers.emplace_back("x-mock-ip", mockIp);
			resp = real->http(m);
		}
		if (loseAnswers > 0) {
			loseAnswers--;
			HttpResponse lost;
			lost.sent = true;
			return lost;
		}
		return resp;
	}
};

// ---- a "web" library: CedarLogic Online in memory ---------------------------------------------------

class MemLibrary : public Backend {
public:
	struct C {
		std::string name, cdl;
		int64_t mtime = 0;
	};
	std::map<std::string, C> lib, trashed;
	std::vector<std::string> order;   // creation order
	std::string prefix;
	int n = 0;
	Crypto& cr;
	std::function<std::string(const std::string&)> structure;
	int64_t gen = 0;

	MemLibrary(Crypto& c, const std::string& p) : prefix(p), cr(c) {}
	bool list(std::vector<std::string>& ids) override {
		ids.clear();
		for (const std::string& id : order)
			if (lib.count(id)) ids.push_back(id);
		return true;
	}
	bool exists(const std::string& id) override { return lib.count(id) > 0; }
	bool read(const std::string& id, std::string& name, std::string& cdl, int64_t& mtime, int64_t& createdAt) override {
		auto it = lib.find(id);
		if (it == lib.end()) return false;
		name = it->second.name;
		cdl = it->second.cdl;
		mtime = it->second.mtime;
		createdAt = -1;
		return true;
	}
	bool hashes(const std::string& id, LocalHashes& h) override {
		auto it = lib.find(id);
		if (it == lib.end()) return false;
		h.n = nameHash(cr, it->second.name);
		h.c = cdlHash(cr, it->second.cdl);
		h.st = structure(it->second.cdl);
		h.ch = contentHash(cr, it->second.name, it->second.cdl);
		return true;
	}
	int64_t modified(const std::string& id) override { return lib.count(id) ? lib[id].mtime : 0; }
	std::string create(const std::string& name, const std::string& cdl, int64_t mtime, const std::string&) override {
		const std::string id = prefix + "-" + std::to_string(++n);
		lib[id] = C{ name, cdl, mtime };
		order.push_back(id);
		return id;
	}
	bool write(const std::string& id, const std::string& name, const std::string& cdl, int64_t mtime) override {
		auto it = lib.find(id);
		if (it == lib.end()) return false;
		it->second = C{ name, cdl, mtime };
		return true;
	}
	bool keepVersion(const std::string&, const std::string&, const std::string&) override { return true; }
	bool keepsVersions() const override { return false; }
	bool trash(const std::string& id) override {
		auto it = lib.find(id);
		if (it == lib.end()) return false;
		trashed[id] = it->second;
		lib.erase(it);
		return true;
	}
	std::string libraryId() override { return "0123456789abcdef0123456789abcdef"; }
	int64_t libraryGen() override { return gen; }
	bool setLibraryGen(int64_t g) override {
		gen = g;
		return true;
	}
};

// ---- one device ------------------------------------------------------------------------------------------

struct Client {
	std::string kind, device;
	FakeClock& clock;
	SkewClock myClock;
	Crypto& cr;
	Net net;
	std::string root;   // apps: the library folder
	std::unique_ptr<Backend> lib;
	MemLibrary* mem = nullptr;
	std::unique_ptr<Core> core;
	std::string savedState, code;
	std::vector<std::string> notices;
	std::vector<std::pair<std::string, int>> asked;
	std::set<std::string> dirty, recent;
	std::vector<std::pair<std::string, std::string>> replacedLog;
	bool answerLocalDeletes = true, answerIncomingDeletes = true;
	bool reloadEachSync = true;
	int made = 0;
	int64_t changesLimit = 0;
	std::string serverBase = "http://fake/api/sync/v1";

	Client(const std::string& k, const std::string& d, FakeClock& c, int64_t skew, Crypto& crypto, const std::string& dir)
		: kind(k), device(d), clock(c), myClock(c, skew), cr(crypto) {
		if (kind == "app") {
			root = files::join(dir, "Library");
			files::removeAll(dir);
			files::makeDirs(root);
		}
		makeLibrary();
		makeCore();
		core->setDeviceName(device);
	}

	void makeLibrary() {
		if (kind == "app") {
			FileLibraryOptions o;
			o.root = root;
			o.clock = &myClock;
			o.crypto = &cr;
			o.structureHash = [this](const std::string& cdl) { return core->structureHash(cdl); };
			lib = makeFileLibrary(o);
		} else {
			auto m = std::unique_ptr<MemLibrary>(new MemLibrary(cr, device));
			m->structure = [this](const std::string& cdl) { return core->structureHash(cdl); };
			mem = m.get();
			lib = std::move(m);
		}
	}

	void makeCore() {
		CoreOptions o;
		o.serverBase = serverBase;
		o.client = kind + "/selftest";
		o.web = kind == "web";
		o.gateDefaults = vectorDefaults();
		o.changesLimit = changesLimit;
		CoreHooks h;
		h.http = [this](const HttpRequest& r) { return net.call(r); };
		h.notice = [this](const std::string& t) { notices.push_back(t); };
		h.askMassDelete = [this](int n) {
			asked.emplace_back("local-deletes", n);
			return answerLocalDeletes;
		};
		h.askIncomingDeletes = [this](int n, const std::string&) {
			asked.emplace_back("incoming-deletes", n);
			return answerIncomingDeletes;
		};
		h.held = [this](const std::string& local, bool runtimeOnly) {
			return dirty.count(local) > 0 || (runtimeOnly && recent.count(local) > 0);
		};
		h.replaced = [this](const std::string& local, const std::string& from) { replacedLog.emplace_back(local, from); };
		h.saveState = [this](const State& s) {
			savedState = json::write(s.toJson());
			return true;
		};
		std::string name = core ? core->deviceName() : device;
		core.reset(new Core(o, cr, myClock, *lib, h));
		core->setDeviceName(name);
	}

	// As after a restart: the state and the hash cache from what was saved.
	void restart() {
		json::Value v;
		State s;
		if (savedState.empty() || !json::parse(savedState, v) || !State::fromJson(v, s)) return;
		const bool wasOn = core->enabled();
		const size_t req = core->requests;
		makeCore();
		core->adopt(s);
		lib->loadCache(s.hashCache);
		if (wasOn) core->setCode(code);
		core->requests = req;
	}

	int64_t now() { return myClock.now(); }
	std::string saveText(const std::string& cdl) { return kind == "web" ? toXml(cdl) : cdl; }

	// -- the person, here --
	std::string create(const std::string& name, const std::string& cdl0) {
		clock.tick();
		const std::string cdl = saveText(cdl0);
		if (mem) return mem->create(name, cdl, now(), "");
		const std::string id = "P" + device + "-" + std::to_string(++made);
		const std::string folder = files::join(root, id);
		files::makeDirs(files::join(folder, "versions"));
		files::writeAtomic(files::join(folder, "name.txt"), name);
		files::writeAtomic(files::join(folder, "circuit.cdl"), cdl);
		files::setMtimeMs(files::join(folder, "name.txt"), now());
		files::setMtimeMs(files::join(folder, "circuit.cdl"), now());
		return id;
	}
	void edit(const std::string& lid, const std::string& cdl0) {
		clock.tick();
		const std::string cdl = saveText(cdl0);
		if (mem) {
			mem->lib[lid].cdl = cdl;
			mem->lib[lid].mtime = now();
			return;
		}
		const std::string p = files::join(files::join(root, lid), "circuit.cdl");
		files::writeAtomic(p, cdl);
		files::setMtimeMs(p, now());
	}
	void rename(const std::string& lid, const std::string& name) {
		clock.tick();
		if (mem) {
			mem->lib[lid].name = name;
			mem->lib[lid].mtime = now();
			return;
		}
		const std::string p = files::join(files::join(root, lid), "name.txt");
		files::writeAtomic(p, name);
		files::setMtimeMs(p, now());
	}
	void remove(const std::string& lid) { lib->trash(lid); }
	void vanish(const std::string& lid) {
		if (mem) mem->lib.erase(lid);
		else files::removeAll(files::join(root, lid));
	}

	std::vector<std::string> ids() {
		std::vector<std::string> out;
		lib->list(out);
		return out;
	}
	std::string nameOf(const std::string& lid) {
		std::string n, c;
		int64_t m, cr2;
		lib->read(lid, n, c, m, cr2);
		return n;
	}
	std::string cdlOf(const std::string& lid) {
		std::string n, c;
		int64_t m, cr2;
		lib->read(lid, n, c, m, cr2);
		return c;
	}
	std::vector<std::string> names() {
		std::vector<std::string> out;
		for (const std::string& id : ids()) out.push_back(nameOf(id));
		std::sort(out.begin(), out.end());
		return out;
	}
	std::vector<std::string> byName(const std::string& name) {
		std::vector<std::string> out;
		for (const std::string& id : ids())
			if (nameOf(id) == name) out.push_back(id);
		return out;
	}
	std::vector<std::pair<std::string, std::string>> structures() {
		std::vector<std::pair<std::string, std::string>> out;
		for (const std::string& id : ids()) out.emplace_back(nameOf(id), core->structureHash(cdlOf(id)));
		std::sort(out.begin(), out.end());
		return out;
	}
	// Apps: versions/<stamp>.cdl and their notes, oldest first.
	std::vector<std::pair<std::string, std::string>> versions(const std::string& lid) {
		std::vector<std::pair<std::string, std::string>> out;
		if (mem) return out;
		const std::string dir = files::join(files::join(root, lid), "versions");
		for (const std::string& n : files::listDir(dir)) {
			if (n.size() < 5 || n[0] == '.' || n.compare(n.size() - 4, 4, ".cdl") != 0) continue;
			std::string cdl, note;
			files::read(files::join(dir, n), cdl);
			files::read(files::join(dir, n.substr(0, n.size() - 4) + ".txt"), note);
			out.emplace_back(cdl, note);
		}
		return out;
	}
	bool hasVersion(const std::string& lid, const std::string& cdl) {
		for (const auto& v : versions(lid))
			if (v.first == cdl) return true;
		return false;
	}
	bool anyVersionNote(const std::string& lid, const std::string& text) {
		for (const auto& v : versions(lid))
			if (v.second.find(text) != std::string::npos) return true;
		return false;
	}
	std::vector<std::string> trashNames() {
		std::vector<std::string> out;
		if (mem) {
			for (const auto& kv : mem->trashed) out.push_back(kv.second.name);
		} else {
			const std::string bin = files::join(root, ".Trash");
			for (const std::string& n : files::listDir(bin)) {
				std::string name;
				files::read(files::join(files::join(bin, n), "name.txt"), name);
				out.push_back(trimAscii(name));
			}
		}
		std::sort(out.begin(), out.end());
		return out;
	}
	bool anyNotice(const std::string& text) {
		for (const std::string& n : notices)
			if (n.find(text) != std::string::npos) return true;
		return false;
	}

	// -- sync --
	std::string turnOn() {
		std::string message;
		if (!core->turnOn(message)) throw Fail("turnOn: " + message);
		code = core->code();
		return code;
	}
	int link(const std::string& text, Preview& pv, bool confirm = true) {
		std::string message;
		const int st = core->preview(text, pv, message);
		if (st != 200 || !confirm) return st;
		if (!core->link(text, message, true)) throw Fail("link: " + message);
		std::string why;
		parseCode(cr, text, code, why);
		return st;
	}
	void sync(bool flush = true) {
		if (reloadEachSync) restart();
		core->sync(flush);
	}
	std::string status() { return core->status(); }
	bool enabled() { return core->enabled(); }
	const State& state() { return core->state(); }
	std::string mapped(const std::string& lid) {
		for (const auto& kv : core->state().records)
			if (kv.second.local == lid) return kv.first;
		return std::string();
	}
	std::string problem(const std::string& lid) {
		auto it = core->problems().find(lid);
		return it == core->problems().end() ? std::string() : it->second;
	}
};

// What the scenarios see of the server and do to it: the FakeServer's insides, or the mock
// server's /__mock/ controls over HTTP.
struct RecView {
	int64_t ver = 0, seq = 0;
	bool deleted = false, dev = false;
	std::string h, data;
};
struct SpaceView {
	bool exists = false, authExists = false;
	int64_t seq = 0, purgedSeq = 0;
	std::map<std::string, RecView> recs;
};

struct World {
	FakeClock clock;
	Crypto& cr;
	std::unique_ptr<FakeServer> server;
	Host* real = nullptr;
	std::string realBase, mockIp;
	bool limitsChanged = false;
	std::vector<std::unique_ptr<Client>> cls;

	World(Crypto& c, const std::string& dir, std::vector<std::string> kinds, Limits limits = Limits(),
	      std::vector<int64_t> skews = {}, Host* r = nullptr, const std::string& rb = "")
		: cr(c), real(r), realBase(rb) {
		if (!real) {
			server.reset(new FakeServer(clock, cr));
			server->limits = limits;
		} else {
			uint8_t b[3];
			cr.random(b, 3);
			mockIp = "10." + std::to_string(b[0]) + "." + std::to_string(b[1]) + "." + std::to_string(b[2]);
			setLimits(limits);
		}
		for (size_t i = 0; i < kinds.size(); i++) {
			std::string up = kinds[i];
			for (char& ch : up) ch = (char)toupper((unsigned char)ch);
			const std::string name = up + std::to_string(i);
			cls.emplace_back(new Client(kinds[i], name, clock, i < skews.size() ? skews[i] : 0, cr, files::join(dir, name)));
			Client& cl = *cls.back();
			cl.net.server = server.get();
			cl.net.real = real;
			if (real) {
				cl.net.mockClock = &clock;
				cl.net.mockIp = mockIp;
				cl.serverBase = realBase;
				cl.makeCore();
			}
		}
	}
	~World() {
		if (real && limitsChanged) {
			json::Value b = json::Value::object();
			b.set("reset", json::Value::boolean(true));
			control("limits", b);
		}
	}
	Client& operator[](size_t i) { return *cls[i]; }

	// The mock server's controls (POST /__mock/<op>, JSON).
	json::Value control(const std::string& op, const json::Value& body, const std::string& method = "POST",
	                    const std::string& query = "") {
		HttpRequest r;
		r.method = method;
		r.url = realBase.substr(0, realBase.find("/api/sync/v1")) + "/__mock/" + op + query;
		if (method == "POST") {
			r.headers.emplace_back("content-type", "application/json");
			r.body = json::write(body);
		}
		const HttpResponse resp = real->http(r);
		json::Value out;
		if (resp.status != 200 || !json::parse(resp.body, out)) throw Fail("mock control " + op + ": " + std::to_string(resp.status));
		return out;
	}

	void setLimits(const Limits& L) {
		if (!real) {
			server->limits = L;
			return;
		}
		const Limits d;
		json::Value b = json::Value::object();
		if (L.maxEnvelope != d.maxEnvelope) b.set("maxRecordBytes", json::Value::integer(L.maxEnvelope));
		if (L.maxRecords != d.maxRecords) b.set("maxRecords", json::Value::integer(L.maxRecords));
		if (b.o.empty()) return;
		control("limits", b);
		limitsChanged = true;
	}

	SpaceView view(const std::string& sid) {
		SpaceView v;
		if (server) {
			v.authExists = server->auth.count(sid) > 0;
			auto s = server->spaces.find(sid);
			if (s == server->spaces.end()) return v;
			v.exists = true;
			v.seq = s->second.seq;
			v.purgedSeq = s->second.purgedSeq;
			for (const auto& kv : s->second.recs)
				v.recs[kv.first] = RecView{ kv.second.ver, kv.second.seq, kv.second.deleted, kv.second.dev, kv.second.h, kv.second.data };
			return v;
		}
		const json::Value d = control("dump", json::Value(), "GET", "?space=" + sid + "&data=1");
		const json::Value* auth = d.get("auth");
		v.authExists = auth && auth->get(sid);
		const json::Value* spaces = d.get("spaces");
		const json::Value* doc = spaces ? spaces->get(sid) : nullptr;
		if (!doc || !doc->isObject()) return v;
		v.exists = true;
		v.seq = doc->integer("seq");
		v.purgedSeq = doc->integer("purgedSeq");
		if (const json::Value* recs = doc->get("recs"))
			for (const auto& kv : recs->o)
				v.recs[kv.first] = RecView{ kv.second.integer("ver"), kv.second.integer("seq"), kv.second.flag("del"),
				                            kv.second.flag("dev"), kv.second.str("h"), kv.second.str("data") };
		return v;
	}
	SpaceView view(Client& c) { return view(c.core->keys().spaceId); }
	std::map<std::string, RecView> recs(Client& c) { return view(c).recs; }
	int64_t seq(Client& c) { return view(c).seq; }

	// Live circuit records on the server (not device records, not tombstones).
	int circuitsOnServer(Client& c, Crypto&) {
		int n = 0;
		for (const auto& kv : recs(c)) {
			if (kv.second.deleted) continue;
			Bytes env;
			std::string payload;
			Payload p;
			if (unb64u(kv.second.data, env) && openRecord(cr, c.core->keys().recordKey, kv.first, kv.second.ver, env, payload) &&
			    readPayload(payload, p).empty() && p.kind == "circuit")
				n++;
		}
		return n;
	}

	void tamper(Client& c, const std::string& op, const std::string& id = "") {
		const std::string sid = c.core->keys().spaceId;
		if (server) {
			if (!server->tamper(sid, op, id)) throw Fail("tamper " + op);
			return;
		}
		json::Value b = json::Value::object();
		b.set("space", json::Value::string(sid));
		b.set("op", json::Value::string(op));
		if (!id.empty()) b.set("id", json::Value::string(id));
		control("tamper", b);
	}

	// The daily cleanup, now (the test's clock).
	void cleanup() {
		if (server) {
			server->cleanup();
			return;
		}
		json::Value b = json::Value::object();
		b.set("set", json::Value::integer(clock.t));
		control("clock", b);
		control("cleanup", json::Value::object());
	}

	// The next API request is answered with `status` (and Retry-After), not acted on.
	void failNext(int status, int64_t retryAfter) {
		if (server) {
			server->failNext = 1;
			server->failStatus = status;
			server->failRetryAfter = retryAfter;
			return;
		}
		json::Value b = json::Value::object();
		b.set("status", json::Value::integer(status));
		b.set("retryAfter", json::Value::integer(retryAfter));
		b.set("count", json::Value::integer(1));
		control("fail", b);
	}

	// One API request with these keys (none of the client's own logic).
	int raw(const Keys& k, const std::string& method, const std::string& path, const json::Value* body, json::Value& out,
	        const std::vector<std::pair<std::string, std::string>>& extra = {}) {
		HttpRequest r;
		r.method = method;
		r.url = (server ? std::string("http://fake/api/sync/v1") : realBase) + "/spaces/" + k.spaceId + path;
		r.headers.emplace_back("authorization", "Bearer " + k.authToken);
		for (const auto& h : extra) r.headers.push_back(h);
		if (body) {
			r.headers.emplace_back("content-type", "application/json");
			r.body = json::write(*body);
		}
		HttpResponse resp;
		if (server) {
			resp = server->handle(r);
		} else {
			r.headers.emplace_back("x-mock-now", std::to_string(clock.t));
			r.headers.emplace_back("x-mock-ip", mockIp);
			resp = real->http(r);
		}
		if (!json::parse(resp.body, out)) out = json::Value::object();
		return resp.status;
	}
	int rawWrite(Client& c, const json::Value& writes, json::Value& out) {
		json::Value b = json::Value::object();
		b.set("writes", writes);
		return raw(c.core->keys(), "POST", "/write", &b, out);
	}
};

bool sameEverywhere(std::vector<Client*> cls) {
	const auto first = cls[0]->structures();
	for (Client* c : cls)
		if (c->structures() != first) return false;
	return true;
}

std::vector<std::string> V(std::initializer_list<const char*> xs) {
	std::vector<std::string> out;
	for (const char* x : xs) out.push_back(x);
	return out;
}

std::string lowerSpaced(const std::string& code) {
	std::string g = groupCode(code);
	for (char& c : g) {
		if (c == '-') c = ' ';
		else c = (char)tolower((unsigned char)c);
	}
	return g;
}

const int64_t DAY = kDay, MINUTE = kMinute;

// ---- the scenarios ----------------------------------------------------------------------------------------------

struct Ctx {
	Crypto& cr;
	std::string dir;
	Host* real = nullptr;
	std::string realBase;
};

void s01_first_device_then_link(Ctx& x) {
	World w(x.cr, x.dir, { "app", "web" }, Limits(), {}, x.real, x.realBase);
	Client &a = w[0], &b = w[1];
	a.create("Adder", cdlWith(20));
	const std::string code = a.turnOn();
	a.sync();
	Preview pv;
	CHECK(b.link(lowerSpaced(code), pv) == 200);
	CHECK(pv.circuits == 1 && pv.devices == V({ "APP0" }) && pv.names == V({ "Adder" }));
	CHECK(pv.sentence.find("1 circuit from APP0") != std::string::npos);
	b.sync();
	CHECK(b.names() == V({ "Adder" }) && sameEverywhere({ &a, &b }));
	CHECK(b.cdlOf(b.byName("Adder")[0]) == a.cdlOf(a.byName("Adder")[0]));   // received text kept byte for byte
}

void s02_link_with_unknown_code_creates_nothing(Ctx& x) {
	World w(x.cr, x.dir, { "app" }, Limits(), {}, x.real, x.realBase);
	Client& a = w[0];
	const std::string code = newCode(x.cr);
	Preview pv;
	CHECK(a.link(code, pv) == 404 && !a.enabled());
	const SpaceView v = w.view(keysForCode(x.cr, code).spaceId);
	CHECK(!v.exists && !v.authExists);
	std::string message;
	CHECK(!a.core->link(code, message) && message.find("No circuits are synced with this code") == 0);
}

void s03_edit_offline_on_two_devices_app_app(Ctx& x) {
	World w(x.cr, x.dir, { "app", "app" }, Limits(), {}, x.real, x.realBase);
	Client &a = w[0], &b = w[1];
	const std::string lid = a.create("Counter", cdlWith(20));
	Preview pv;
	const std::string code = a.turnOn();
	a.sync();
	b.link(code, pv);
	b.sync();
	const std::string lb = b.byName("Counter")[0];
	a.net.down = b.net.down = true;
	a.edit(lid, cdlWith(30));   // earlier
	b.edit(lb, cdlWith(40));    // later: wins
	a.sync();
	b.sync();
	CHECK(a.status() == "offline" && b.status() == "offline");
	a.net.down = b.net.down = false;
	a.sync();
	b.sync();
	a.sync();
	CHECK(sameEverywhere({ &a, &b }) && a.names() == V({ "Counter" }));
	CHECK(a.cdlOf(lid) == cdlWith(40));
	CHECK(a.hasVersion(lid, cdlWith(30)));   // nothing lost
	CHECK(b.anyVersionNote(lb, "changed on both") && b.hasVersion(lb, cdlWith(30)));   // B saw both changes
}

void s04_edit_offline_web_loses_gets_a_copy(Ctx& x) {
	World w(x.cr, x.dir, { "app", "web" }, Limits(), {}, x.real, x.realBase);
	Client &a = w[0], &wb = w[1];
	const std::string lid = a.create("Counter", cdlWith(20));
	Preview pv;
	const std::string code = a.turnOn();
	a.sync();
	wb.link(code, pv);
	wb.sync();
	const std::string lw = wb.byName("Counter")[0];
	wb.edit(lw, cdlWith(30));   // earlier, on the web (saved as XML)
	a.edit(lid, cdlWith(40));   // later, in the app: wins
	a.sync();
	wb.sync();
	a.sync();
	CHECK_MSG(wb.names() == V({ "Counter", "Counter (from WEB1)" }), wb.names().size() > 1 ? wb.names()[1] : "");
	CHECK(sameEverywhere({ &a, &wb }));
	CHECK(wb.cdlOf(lw) == cdlWith(40));
	const std::string copy = wb.byName("Counter (from WEB1)")[0];
	CHECK(a.core->structureHash(wb.cdlOf(copy)) == a.core->structureHash(cdlWith(30)));
	CHECK(wb.anyNotice("Counter (from WEB1)"));
}

void s05_edit_offline_web_wins_app_keeps_version(Ctx& x) {
	World w(x.cr, x.dir, { "app", "web" }, Limits(), {}, x.real, x.realBase);
	Client &a = w[0], &wb = w[1];
	const std::string lid = a.create("Counter", cdlWith(20));
	Preview pv;
	const std::string code = a.turnOn();
	a.sync();
	wb.link(code, pv);
	wb.sync();
	const std::string lw = wb.byName("Counter")[0];
	a.edit(lid, cdlWith(30));    // earlier, app
	wb.edit(lw, cdlWith(40));    // later, web: wins
	a.sync();
	wb.sync();
	a.sync();
	CHECK(wb.names() == V({ "Counter", "Counter (from APP0)" }));
	CHECK(a.core->structureHash(a.cdlOf(lid)) == a.core->structureHash(cdlWith(40)) && sameEverywhere({ &a, &wb }));
}

void s06_delete_here_edit_there_edit_wins_both_orders(Ctx& x) {
	for (const char* order : { "delete first", "edit first" }) {
		World w(x.cr, files::join(x.dir, order[0] == 'd' ? "d" : "e"), { "app", "app" }, Limits(), {}, x.real, x.realBase);
		Client &a = w[0], &b = w[1];
		const std::string lid = a.create("ALU", cdlWith(20));
		Preview pv;
		const std::string code = a.turnOn();
		a.sync();
		b.link(code, pv);
		b.sync();
		const std::string lb = b.byName("ALU")[0];
		a.remove(lid);
		b.edit(lb, cdlWith(50));
		if (order[0] == 'd') {
			a.sync();
			b.sync();
			a.sync();
		} else {
			b.sync();
			a.sync();
			b.sync();
		}
		CHECK_MSG(a.names() == V({ "ALU" }) && b.names() == V({ "ALU" }), order);
		CHECK(sameEverywhere({ &a, &b }) && a.cdlOf(a.ids()[0]) == cdlWith(50));
		CHECK_MSG(a.anyNotice("ALU"), order);   // the deleting device is told
	}
}

void s07_delete_reaches_other_devices_into_trash(Ctx& x) {
	World w(x.cr, x.dir, { "app", "web" }, Limits(), {}, x.real, x.realBase);
	Client &a = w[0], &b = w[1];
	const std::string lid = a.create("Mux", cdlWith(20));
	Preview pv;
	const std::string code = a.turnOn();
	a.sync();
	b.link(code, pv);
	b.sync();
	a.remove(lid);
	a.sync();
	b.sync();
	CHECK(b.names().empty() && b.trashNames() == V({ "Mux" }));
	CHECK(b.anyNotice("Recently Deleted"));
	bool found = false;
	for (const auto& kv : w.recs(a)) {
		if (!kv.second.deleted) continue;
		Bytes env;
		std::string payload;
		Payload p;
		CHECK(unb64u(kv.second.data, env) && openRecord(x.cr, a.core->keys().recordKey, kv.first, kv.second.ver, env, payload));
		CHECK(readPayload(payload, p).empty() && p.kind == "deleted");
		json::Value v;
		CHECK(json::parse(payload, v) && !v.get("name") && !v.get("cdl"));
		found = true;
	}
	CHECK(found);
}

void s08_rename_one_side_and_both_sides(Ctx& x) {
	World w(x.cr, x.dir, { "app", "app" }, Limits(), {}, x.real, x.realBase);
	Client &a = w[0], &b = w[1];
	const std::string lid = a.create("Untitled", cdlWith(20));
	Preview pv;
	const std::string code = a.turnOn();
	a.sync();
	b.link(code, pv);
	b.sync();
	const std::string lb = b.byName("Untitled")[0];
	const size_t versionsBefore = b.versions(lb).size();
	a.rename(lid, "Traffic light");
	a.sync();
	b.sync();
	CHECK(b.names() == V({ "Traffic light" }) && b.versions(lb).size() == versionsBefore);
	a.rename(lid, "Lights A");
	b.rename(lb, "Lights B");   // B later: wins, quietly
	a.sync();
	b.sync();
	a.sync();
	CHECK(a.names() == V({ "Lights B" }) && b.names() == V({ "Lights B" }) && a.versions(lid).empty());
}

void s09_switches_only_on_both_app_and_web_is_not_a_conflict(Ctx& x) {
	World w(x.cr, x.dir, { "app", "web" }, Limits(), {}, x.real, x.realBase);
	Client &a = w[0], &wb = w[1];
	const std::string lid = a.create("Lamp", CDL());
	Preview pv;
	const std::string code = a.turnOn();
	a.sync();
	wb.link(code, pv);
	wb.sync();
	const std::string lw = wb.byName("Lamp")[0];
	a.edit(lid, cdlSwitch(true));
	wb.edit(lw, replaceAll(cdlSwitch(true), "0.3.5", "web"));   // the web saves XML
	CHECK(a.cdlOf(lid) != wb.cdlOf(lw));
	a.sync();
	wb.sync();
	a.sync();
	CHECK(wb.names() == V({ "Lamp" }) && sameEverywhere({ &a, &wb }) && a.versions(lid).empty());
	CHECK(a.cdlOf(lid) == wb.cdlOf(lw));   // the newer text, byte for byte
}

void s10_join_with_overlapping_libraries(Ctx& x) {
	for (const char* kb : { "app", "web" }) {
		World w(x.cr, files::join(x.dir, kb), { "app", kb }, Limits(), {}, x.real, x.realBase);
		Client &a = w[0], &b = w[1];
		a.create("X", cdlWith(20));
		a.create("Y", cdlWith(21));
		b.create("X", cdlWith(20));
		b.create("Z", cdlWith(22));
		b.create("Y", cdlSwitch(true, cdlWith(21)));   // the same circuit, a switch flipped
		b.create("Y", cdlWith(99));                    // the same name, another circuit
		Preview pv;
		const std::string code = a.turnOn();
		a.sync();
		b.link(code, pv);
		b.sync();
		a.sync();
		CHECK_MSG(a.names() == V({ "X", "Y", "Y", "Z" }) && b.names() == V({ "X", "Y", "Y", "Z" }), kb);
		CHECK(w.circuitsOnServer(a, x.cr) == 4);
		CHECK(sameEverywhere({ &a, &b }));
	}
}

void s11_quota_exceeded(Ctx& x) {
	Limits L;
	L.maxRecords = 3;
	World w(x.cr, x.dir, { "app", "web" }, L, {}, x.real, x.realBase);
	Client &a = w[0], &b = w[1];
	for (int i = 0; i < 4; i++) a.create("C" + std::to_string(i), cdlWith(20 + i));
	Preview pv;
	const std::string code = a.turnOn();
	a.sync();
	CHECK(a.status() == "full" && w.circuitsOnServer(a, x.cr) == 3);   // circuits first; the device record waits
	CHECK(a.core->statusText().find("full") != std::string::npos);
	b.link(code, pv);
	b.sync();
	CHECK(b.ids().size() == 3);
	b.remove(b.byName(b.names()[0])[0]);
	b.sync();
	a.sync();
	CHECK(a.status() == "synced" && a.ids().size() == 3 && w.circuitsOnServer(a, x.cr) == 3);
	b.sync();
	CHECK(sameEverywhere({ &a, &b }) && b.ids().size() == 3);
}

void s12_too_large_record_is_skipped_not_retried(Ctx& x) {
	Limits L;
	L.maxEnvelope = 400;
	World w(x.cr, x.dir, { "app" }, L, {}, x.real, x.realBase);
	Client& a = w[0];
	std::string big;
	for (int i = 0; i < 50; i++) big += CDL();
	const std::string bigId = a.create("Big", big);
	a.create("Small", "x");
	a.turnOn();
	a.sync();
	const size_t calls = a.net.calls;
	a.sync();
	CHECK(w.circuitsOnServer(a, x.cr) == 1);
	CHECK_MSG(a.net.calls - calls == 1, std::to_string(a.net.calls - calls));   // just the pull
	CHECK(a.problem(bigId).find("Too big") == 0);
	a.edit(bigId, "y");
	a.sync();
	CHECK(w.circuitsOnServer(a, x.cr) == 2);
}

void s13_412_retry_when_another_device_pushes_between_pull_and_push(Ctx& x) {
	World w(x.cr, x.dir, { "app", "app" }, Limits(), {}, x.real, x.realBase);
	Client &a = w[0], &b = w[1];
	const std::string lid = a.create("FSM", cdlWith(20));
	Preview pv;
	const std::string code = a.turnOn();
	a.sync();
	b.link(code, pv);
	b.sync();
	const std::string lb = b.byName("FSM")[0];
	a.edit(lid, cdlWith(30));
	a.core->pull();   // A has pulled ...
	b.edit(lb, cdlWith(40));
	b.sync();         // ... B pushes first (a later edit) ...
	a.core->push(true);   // ... A's write gets 412, resolves, retries
	b.sync();
	a.sync();
	CHECK(sameEverywhere({ &a, &b }) && a.cdlOf(lid) == cdlWith(40));
	CHECK(a.hasVersion(lid, cdlWith(30)));
}

void s14_answer_lost_mid_push_no_duplicates(Ctx& x) {
	World w(x.cr, x.dir, { "app", "web" }, Limits(), {}, x.real, x.realBase);
	Client &a = w[0], &b = w[1];
	const std::string lid = a.create("Decoder", cdlWith(20));
	a.turnOn();
	a.core->pull();
	a.net.loseAnswers = 1;
	try {
		a.core->push(true);
	} catch (const NetError&) {
	}
	a.edit(lid, cdlWith(25));   // and it changes before the next sync
	a.sync();
	CHECK(a.status() == "synced" && a.names() == V({ "Decoder" }));
	CHECK(w.circuitsOnServer(a, x.cr) == 1);
	Preview pv;
	b.link(a.code, pv);
	b.sync();
	CHECK(b.names() == V({ "Decoder" }) && sameEverywhere({ &a, &b }) && a.versions(lid).empty());
}

void s15_replayed_write_is_idempotent(Ctx& x) {
	World w(x.cr, x.dir, { "app" }, Limits(), {}, x.real, x.realBase);
	Client& a = w[0];
	a.create("R", "r");
	a.turnOn();
	a.sync();
	std::string rid;
	for (const auto& kv : a.state().records) rid = kv.first;
	const RecView rec = w.recs(a)[rid];
	json::Value writes = json::Value::array(), item = json::Value::object(), out;
	item.set("id", json::Value::string(rid));
	item.set("base", json::Value::integer(0));
	item.set("ver", json::Value::integer(1));
	item.set("data", json::Value::string(rec.data));
	writes.push(item);
	CHECK(w.rawWrite(a, writes, out) == 200);
	const json::Value& r0 = out.get("results")->a[0];
	CHECK(r0.integer("status") == 200 && r0.get("entry")->integer("ver") == 1);
}

void s16_long_offline_device_after_tombstones_are_purged(Ctx& x) {
	for (bool edited : { false, true }) {
		World w(x.cr, files::join(x.dir, edited ? "edited" : "unchanged"), { "app", "app" }, Limits(), {}, x.real, x.realBase);
		Client &a = w[0], &b = w[1];
		const std::string keep = a.create("Keep", cdlWith(20));
		const std::string gone = a.create("Gone", cdlWith(21));
		Preview pv;
		const std::string code = a.turnOn();
		a.sync();
		b.link(code, pv);
		b.sync();
		b.net.down = true;
		a.remove(gone);
		a.sync();
		if (edited) b.edit(b.byName("Gone")[0], cdlWith(60));
		a.edit(keep, cdlWith(70));
		a.sync();
		w.clock.tick(401 * DAY);
		a.sync();
		w.cleanup();
		CHECK(w.view(a).purgedSeq > 0);
		b.net.down = false;
		b.sync();
		a.sync();
		// A forgotten record is never trashed: B sends "Gone" again (§4.7).
		CHECK_MSG(b.names() == V({ "Gone", "Keep" }) && a.names() == V({ "Gone", "Keep" }), edited ? "edited" : "unchanged");
		CHECK(sameEverywhere({ &a, &b }));
	}
}

void s17_cloud_copy_deleted_from_another_device(Ctx& x) {
	World w(x.cr, x.dir, { "app", "web" }, Limits(), {}, x.real, x.realBase);
	Client &a = w[0], &b = w[1];
	a.create("A", "a");
	Preview pv;
	const std::string code = a.turnOn();
	a.sync();
	b.link(code, pv);
	b.sync();
	json::Value out;
	CHECK(w.raw(a.core->keys(), "DELETE", "", nullptr, out) == 403);   // the bearer token alone
	std::string message;
	CHECK(b.core->deleteSyncedCopy(message) == 200);
	a.sync();
	CHECK(a.status() == "gone" && !a.enabled() && a.names() == V({ "A" }));
	const Keys k = keysForCode(x.cr, code);
	json::Value body = json::Value::object();
	body.set("deleteHash", json::Value::string(k.deleteHash));
	CHECK(w.raw(k, "PUT", "", &body, out) == 410);   // the same code can't bring it back
}

void s18_turn_off_keeps_or_removes(Ctx& x) {
	World w(x.cr, x.dir, { "app" }, Limits(), {}, x.real, x.realBase);
	Client& a = w[0];
	a.create("Mine", "m");
	a.turnOn();
	a.sync();
	a.create("After", "n");
	a.core->turnOff(false);
	CHECK(a.names() == V({ "After", "Mine" }) && !a.enabled());
	a.turnOn();
	a.sync();
	a.core->turnOff(true);
	CHECK(a.names().empty() && a.trashNames() == V({ "After", "Mine" }));
}

void s19_damaged_record_is_not_applied_and_is_repaired(Ctx& x) {
	World w(x.cr, x.dir, { "app", "app" }, Limits(), {}, x.real, x.realBase);
	Client &a = w[0], &b = w[1];
	a.create("T", cdlWith(20));
	Preview pv;
	const std::string code = a.turnOn();
	a.sync();
	const std::string rid = a.state().records.begin()->first;
	w.tamper(a, "damage", rid);
	b.link(code, pv);
	b.sync();
	CHECK(b.names().empty() && b.state().unreadable.size() == 1 && b.state().unreadable.count(rid) &&
	      b.state().unreadable.at(rid) == std::make_pair((int64_t)2, std::string("damaged")) && b.status() == "synced");
	const size_t calls = b.net.calls;
	b.sync();
	CHECK(b.net.calls - calls == 1);   // not fetched again
	a.sync();                          // A has the circuit: its copy goes over the damaged one
	CHECK(w.recs(a)[rid].ver == 3);
	CHECK(a.anyNotice("damaged"));
	b.sync();
	CHECK(b.names() == V({ "T" }) && sameEverywhere({ &a, &b }));
}

void s20_three_devices_converge(Ctx& x) {
	World w(x.cr, x.dir, { "app", "app", "web" }, Limits(), {}, x.real, x.realBase);
	Client &a = w[0], &b = w[1], &c = w[2];
	const std::string xid = a.create("Shared", cdlWith(20));
	Preview pv;
	const std::string code = a.turnOn();
	a.sync();
	b.link(code, pv);
	b.sync();
	c.link(code, pv);
	c.sync();
	a.edit(xid, cdlWith(30));
	b.edit(b.byName("Shared")[0], cdlWith(31));
	c.create("Web only", "w");
	c.edit(c.byName("Shared")[0], cdlWith(32));
	for (int k = 0; k < 2; k++) {
		a.sync();
		b.sync();
		c.sync();
	}
	CHECK(sameEverywhere({ &a, &b, &c }));
}

void s21_connection_drops_mid_push(Ctx& x) {
	World w(x.cr, x.dir, { "app", "web" }, Limits(), {}, x.real, x.realBase);
	Client &a = w[0], &b = w[1];
	for (int i = 0; i < 3; i++) a.create("N" + std::to_string(i), cdlWith(40 + i));
	a.turnOn();
	a.core->setBatchItems(1);
	a.net.failAfter = 2;   // the change list, one write, then nothing
	a.reloadEachSync = false;
	a.sync();
	CHECK(a.status() == "offline" && w.circuitsOnServer(a, x.cr) == 1);
	a.net.down = false;
	a.sync();
	CHECK(a.status() == "synced" && w.circuitsOnServer(a, x.cr) == 3);
	Preview pv;
	b.link(a.code, pv);
	b.sync();
	CHECK(b.names() == V({ "N0", "N1", "N2" }) && sameEverywhere({ &a, &b }));
}

// 22 (clients): a 429 with Retry-After: 120.
void s22_retry_after_is_respected(Ctx& x) {
	World w(x.cr, x.dir, { "app" }, Limits(), {}, x.real, x.realBase);
	Client& a = w[0];
	a.create("R", cdlWith(20));
	a.turnOn();
	Scheduler sch;
	sch.started(a.now() - 10 * kSecond);
	w.failNext(429, 120);
	const int64_t t0 = a.now();
	a.sync();
	CHECK(a.status() == "busy" && a.core->retryAfterMs() >= 120 * kSecond);
	sch.cycleDone(t0, a.status(), a.core->retryAfterMs(), a.core->pollSeconds(), false);
	sch.syncNow(a.now());   // the person presses Sync Now: still not before Retry-After
	const size_t before = a.net.calls;
	int64_t firstAt = 0;
	for (int s = 1; s <= 130; s++) {
		w.clock.tick(kSecond);
		bool flush = false;
		if (sch.due(a.now(), flush, false)) {
			if (!firstAt) firstAt = a.now();
			a.sync(flush);
			sch.cycleDone(a.now(), a.status(), a.core->retryAfterMs(), a.core->pollSeconds(), false);
			break;
		}
	}
	CHECK_MSG(firstAt && firstAt - t0 >= 120 * kSecond, std::to_string(firstAt - t0));
	CHECK(a.net.calls > before && a.status() == "synced" && w.circuitsOnServer(a, x.cr) == 1);
	// And the backoff after a failure: 30 s, then longer.
	Scheduler s2;
	s2.started(0);
	s2.cycleDone(1000000, "offline", 0, 600, false);
	bool f = false;
	CHECK(!s2.due(1000000 + 20 * kSecond, f, false) && s2.due(1000000 + 40 * kSecond, f, false));
}

void s23_open_window_with_unsaved_edits_is_not_overwritten(Ctx& x) {
	World w(x.cr, x.dir, { "app", "app" }, Limits(), {}, x.real, x.realBase);
	Client &a = w[0], &b = w[1];
	const std::string la = a.create("Open", cdlWith(20));
	Preview pv;
	const std::string code = a.turnOn();
	a.sync();
	b.link(code, pv);
	b.sync();
	b.edit(b.byName("Open")[0], cdlWith(30));
	b.sync();
	a.dirty.insert(la);   // A's window has edits not yet saved
	const int64_t cursor = a.state().cursor;
	a.sync();
	CHECK(a.cdlOf(la) == cdlWith(20) && a.state().cursor < w.seq(a));
	CHECK(a.state().cursor >= cursor);
	a.edit(la, cdlWith(35));
	a.dirty.clear();   // the window saves (later than B's edit)
	a.sync();
	b.sync();
	a.sync();
	CHECK(sameEverywhere({ &a, &b }) && a.cdlOf(la) == cdlWith(35));
	CHECK(a.hasVersion(la, cdlWith(30)));   // B's edit kept as a version
}

// 24 (clients): an open window with no unsaved edits reloads; a runtime-only change waits for input to stop.
void s24_clean_window_reloads_runtime_change_waits_for_input(Ctx& x) {
	World w(x.cr, x.dir, { "app", "app" }, Limits(), {}, x.real, x.realBase);
	Client &a = w[0], &b = w[1];
	const std::string la = a.create("Open", CDL());
	Preview pv;
	const std::string code = a.turnOn();
	a.sync();
	b.link(code, pv);
	b.sync();
	const std::string lb = b.byName("Open")[0];
	b.edit(lb, cdlWith(30));
	b.sync();
	a.sync();
	CHECK(a.cdlOf(la) == cdlWith(30));
	CHECK(!a.replacedLog.empty() && a.replacedLog.back() == std::make_pair(la, std::string("APP1")));
	// A runtime-only change to a window with input in the last minute waits.
	b.edit(lb, cdlSwitch(true, cdlWith(30)));
	b.sync(true);
	a.recent.insert(la);
	a.sync();
	CHECK(a.cdlOf(la) == cdlWith(30) && a.state().cursor < w.seq(a));
	a.recent.clear();
	a.sync();
	CHECK(a.cdlOf(la) == cdlSwitch(true, cdlWith(30)) && a.state().cursor == w.seq(a));
}

void s25_mass_delete_here_asks_first(Ctx& x) {
	World w(x.cr, x.dir, { "app", "app" }, Limits(), {}, x.real, x.realBase);
	Client &a = w[0], &b = w[1];
	std::vector<std::string> made;
	for (int i = 0; i < 12; i++) made.push_back(a.create("M" + std::to_string(i), cdlWith(20 + i)));
	Preview pv;
	const std::string code = a.turnOn();
	a.sync();
	b.link(code, pv);
	b.sync();
	for (int i = 0; i < 10; i++) a.vanish(made[(size_t)i]);   // vanished (a folder moved away)
	a.answerLocalDeletes = false;                              // Bring Them Back
	a.sync();
	CHECK(a.asked == (std::vector<std::pair<std::string, int>>{ { "local-deletes", 10 } }) && w.circuitsOnServer(a, x.cr) == 12);
	a.sync();
	CHECK(a.ids().size() == 12 && sameEverywhere({ &a, &b }));
}

void copyTree(const std::string& from, const std::string& to) {
	files::makeDirs(to);
	for (const std::string& n : files::listDir(from)) {
		const std::string f = files::join(from, n), t = files::join(to, n);
		if (files::isDir(f)) {
			copyTree(f, t);
		} else {
			std::string data;
			files::read(f, data);
			files::writeAtomic(t, data);
			files::setMtimeMs(t, files::mtimeMs(f));
		}
	}
}

void s26_library_restored_from_a_backup(Ctx& x) {
	World w(x.cr, x.dir, { "app", "app" }, Limits(), {}, x.real, x.realBase);
	Client &a = w[0], &b = w[1];
	const std::string p = a.create("P", cdlWith(20));
	const std::string q = a.create("Q", cdlWith(21));
	Preview pv;
	const std::string code = a.turnOn();
	a.sync();
	b.link(code, pv);
	b.sync();
	const std::string backup = files::join(x.dir, "backup");
	copyTree(a.root, backup);   // Time Machine
	a.edit(p, cdlWith(30));
	a.create("R", cdlWith(22));
	a.remove(q);
	a.sync();
	b.sync();
	files::removeAll(a.root);   // ... puts the old folder back
	copyTree(backup, a.root);
	a.sync();
	b.sync();
	CHECK(w.circuitsOnServer(a, x.cr) == 2);   // P and R; no deletes sent, no duplicates
	CHECK_MSG(a.names() == V({ "P", "R" }) && b.names() == V({ "P", "R" }), std::to_string(a.names().size()));
	CHECK(a.cdlOf(p) == cdlWith(30) && sameEverywhere({ &a, &b }));
	CHECK(a.trashNames() == V({ "Q", "Q" }) || a.trashNames() == V({ "Q" }));
	CHECK(a.anyNotice("restored from a backup"));
}

void s30_newer_payload_is_left_alone(Ctx& x) {
	World w(x.cr, x.dir, { "app", "app" }, Limits(), {}, x.real, x.realBase);
	Client &a = w[0], &b = w[1];
	const std::string la = a.create("V2", cdlWith(20));
	Preview pv;
	const std::string code = a.turnOn();
	a.sync();
	b.link(code, pv);
	b.sync();
	const std::string rid = a.mapped(la);
	std::string p2 = circuitJson("V2", cdlWith(25), 1, "future", std::string(32, 'f'), -1, nullptr, nullptr);
	p2 = replaceAll(p2, "{\"v\":1,", "{\"v\":2,");
	Bytes env;
	std::string why;
	CHECK(sealRecord(x.cr, a.core->keys().recordKey, rid, 2, p2, false, kMaxEnvelope, env, why));
	json::Value writes = json::Value::array(), item = json::Value::object(), out;
	item.set("id", json::Value::string(rid));
	item.set("base", json::Value::integer(1));
	item.set("ver", json::Value::integer(2));
	item.set("data", json::Value::string(b64u(env)));
	writes.push(item);
	CHECK(w.rawWrite(a, writes, out) == 200);
	const std::string lb = b.byName("V2")[0];
	b.sync();
	CHECK(b.state().unreadable.count(rid) && b.state().unreadable.at(rid) == std::make_pair((int64_t)2, std::string("newer")));
	CHECK(b.cdlOf(lb) == cdlWith(20));
	b.edit(lb, cdlWith(26));
	const size_t calls = b.net.calls;
	b.sync();
	CHECK(b.net.calls - calls == 1 && w.recs(a)[rid].ver == 2);   // no write over it: one request (the pull)
	CHECK(b.problem(lb).find("newer") != std::string::npos);
}

void s31_join_rule_only_when_joining(Ctx& x) {
	World w(x.cr, x.dir, { "app", "app" }, Limits(), {}, x.real, x.realBase);
	Client &a = w[0], &b = w[1];
	Preview pv;
	const std::string code = a.turnOn();
	a.sync();
	b.link(code, pv);
	b.sync();
	a.create("Untitled", "");
	b.create("Untitled", "");   // made separately on each device, after linking
	a.sync();
	b.sync();
	a.sync();
	CHECK(a.names() == V({ "Untitled", "Untitled" }) && b.names() == V({ "Untitled", "Untitled" }));
}

void s32_lost_answer_then_another_device_builds_on_it(Ctx& x) {
	for (const char* kind : { "web", "app" }) {
		World w(x.cr, files::join(x.dir, kind), { kind, "app" }, Limits(), {}, x.real, x.realBase);
		Client &wc = w[0], &a = w[1];
		const std::string lw = wc.create("Counter", cdlWith(20));
		Preview pv;
		const std::string code = wc.turnOn();
		wc.sync();
		a.link(code, pv);
		a.sync();
		wc.edit(lw, cdlWith(30));
		wc.core->pull();
		wc.net.loseAnswers = 1;   // the write lands; the answer is lost (or a pagehide push)
		try {
			wc.core->push(true);
		} catch (const NetError&) {
		}
		const std::string la = a.byName("Counter")[0];
		a.sync();
		CHECK(a.core->structureHash(a.cdlOf(la)) == a.core->structureHash(cdlWith(30)));
		a.edit(la, cdlWith(40));
		a.sync();   // built on W's write
		wc.sync();
		a.sync();
		CHECK_MSG(wc.names() == V({ "Counter" }) && a.names() == V({ "Counter" }), kind);
		CHECK(sameEverywhere({ &wc, &a }) && !a.anyVersionNote(la, "changed on both"));
		CHECK(!wc.anyNotice("changed here and") && !a.anyNotice("changed here and"));
	}
}

void s33_rename_on_one_device_edit_on_the_other(Ctx& x) {
	const std::vector<std::pair<std::string, std::string>> kinds = { { "app", "app" }, { "web", "web" }, { "app", "web" } };
	for (const auto& k : kinds) {
		World w(x.cr, files::join(x.dir, k.first + "-" + k.second), { k.first, k.second }, Limits(), {}, x.real, x.realBase);
		Client &a = w[0], &b = w[1];
		const std::string la = a.create("Untitled", cdlWith(20));
		Preview pv;
		const std::string code = a.turnOn();
		a.sync();
		b.link(code, pv);
		b.sync();
		const std::string lb = b.byName("Untitled")[0];
		b.rename(lb, "Traffic light");   // earlier
		a.edit(la, cdlWith(30));         // later, structural
		b.sync();
		a.sync();
		b.sync();
		CHECK_MSG(a.names() == V({ "Traffic light" }) && b.names() == V({ "Traffic light" }), k.first + "/" + k.second);
		CHECK(a.core->structureHash(a.cdlOf(la)) == a.core->structureHash(cdlWith(30)) && sameEverywhere({ &a, &b }));
	}
}

void s34_clock_skew_does_not_pick_the_wrong_winner(Ctx& x) {
	World w(x.cr, x.dir, { "app", "web" }, Limits(), { 0, -DAY }, x.real, x.realBase);   // B's clock is a day slow
	Client &a = w[0], &b = w[1];
	const std::string la = a.create("C", cdlWith(20));
	Preview pv;
	const std::string code = a.turnOn();
	a.sync();
	b.link(code, pv);
	b.sync();
	b.sync();   // B has heard the server's Date
	const std::string lb = b.byName("C")[0];
	a.edit(la, cdlWith(30));   // earlier
	b.edit(lb, cdlWith(40));   // later, by a slow clock
	a.sync();
	b.sync();
	a.sync();
	CHECK_MSG(a.core->structureHash(a.cdlOf(la)) == a.core->structureHash(cdlWith(40)), "the later edit wins");
	CHECK(sameEverywhere({ &a, &b }));
}

void s35_server_rolls_a_record_back(Ctx& x) {
	World w(x.cr, x.dir, { "web", "web" }, Limits(), {}, x.real, x.realBase);
	Client &w1 = w[0], &w2 = w[1];
	const std::string lid = w1.create("Report", cdlWith(20));
	Preview pv;
	const std::string code = w1.turnOn();
	w1.sync();
	w2.link(code, pv);
	w2.sync();
	const std::string rid = w1.mapped(lid);
	w1.edit(lid, cdlWith(99));
	w1.sync();
	w2.sync();
	w.tamper(w1, "rollback", rid);   // the server serves ver 1 again as the current one
	w2.sync();
	w1.sync();
	w2.sync();
	for (Client* c : { &w1, &w2 }) {
		const std::string id = c->byName("Report")[0];
		CHECK_MSG(c->core->structureHash(c->cdlOf(id)) == c->core->structureHash(cdlWith(99)), c->device);
	}
	CHECK(w.recs(w1)[rid].ver >= 3);   // the newer copy was sent again, at a higher ver
	CHECK(w2.anyNotice("older copy"));
}

void s36_forged_deletes_trash_nothing(Ctx& x) {
	World w(x.cr, x.dir, { "app", "web" }, Limits(), {}, x.real, x.realBase);
	Client &a = w[0], &wb = w[1];
	for (int i = 0; i < 20; i++) a.create("C" + std::to_string(i), cdlWith(30 + i));
	Preview pv;
	const std::string code = a.turnOn();
	a.sync();
	wb.link(code, pv);
	wb.sync();
	w.tamper(a, "forgeTombstones");   // tombstone flags with no sealed tombstone
	a.sync();
	wb.sync();
	CHECK(a.ids().size() == 20 && wb.ids().size() == 20 && a.trashNames().empty() && wb.trashNames().empty());
	// ... and a forged purge: purgedSeq raised, every record gone
	w.tamper(a, "forgePurge");
	a.sync();
	wb.sync();
	CHECK(a.ids().size() == 20 && wb.ids().size() == 20 && a.trashNames().empty() && wb.trashNames().empty());
	CHECK(w.circuitsOnServer(a, x.cr) == 20);   // sent again
}

void s37_many_deletes_from_another_device_ask_first(Ctx& x) {
	World w(x.cr, x.dir, { "app", "app" }, Limits(), {}, x.real, x.realBase);
	Client &a = w[0], &b = w[1];
	std::vector<std::string> made;
	for (int i = 0; i < 12; i++) made.push_back(a.create("D" + std::to_string(i), cdlWith(20 + i)));
	Preview pv;
	const std::string code = a.turnOn();
	a.sync();
	b.link(code, pv);
	b.sync();
	for (int i = 0; i < 10; i++) a.remove(made[(size_t)i]);
	a.sync();   // A meant it (Delete Them Everywhere)
	CHECK(a.asked == (std::vector<std::pair<std::string, int>>{ { "local-deletes", 10 } }));
	b.answerIncomingDeletes = false;   // B: Keep Them
	b.sync();
	CHECK(b.asked == (std::vector<std::pair<std::string, int>>{ { "incoming-deletes", 10 } }) && b.ids().size() == 12 &&
	      b.trashNames().empty());
	a.sync();
	CHECK(a.ids().size() == 12 && sameEverywhere({ &a, &b }));   // they came back everywhere
}

void s38_space_reset_is_noticed(Ctx& x) {
	World w(x.cr, x.dir, { "app", "app" }, Limits(), {}, x.real, x.realBase);
	Client &a = w[0], &b = w[1];
	for (int i = 0; i < 5; i++) a.create("C" + std::to_string(i), cdlWith(30 + i));
	const std::string code = a.turnOn();
	a.sync();
	a.sync();
	w.tamper(a, "resetSpace");   // restored / recreated empty
	a.sync();
	CHECK(w.circuitsOnServer(a, x.cr) == 5 && a.anyNotice("reset"));
	Preview pv;
	b.link(code, pv);
	b.sync();
	CHECK(b.names() == a.names() && sameEverywhere({ &a, &b }));
}

void s39_someone_elses_code_shows_whose_it_is(Ctx& x) {
	World w(x.cr, x.dir, { "app", "app" }, Limits(), {}, x.real, x.realBase);
	Client &bob = w[0], &me = w[1];
	bob.core->setDeviceName("Bob\xE2\x80\x99s laptop");
	for (int i = 0; i < 3; i++) bob.create("Lab " + std::to_string(i), cdlWith(30 + i));
	const std::string code = bob.turnOn();
	bob.sync();
	me.create("My secret design", cdlWith(80));
	const std::map<std::string, RecView> before = w.recs(bob);
	Preview pv;
	CHECK(me.link(code, pv, false) == 200);   // the preview, before anything is sent
	CHECK(pv.circuits == 3 && pv.devices == V({ "Bob\xE2\x80\x99s laptop" }) && !me.enabled());
	CHECK(pv.sentence.find("3 circuits from Bob\xE2\x80\x99s laptop") == 0 + std::string("This code has ").size());
	const std::map<std::string, RecView> after = w.recs(bob);
	CHECK(after.size() == before.size() && w.circuitsOnServer(bob, x.cr) == 3);
	for (const auto& kv : before) CHECK(after.at(kv.first).h == kv.second.h && after.at(kv.first).seq == kv.second.seq);
}

void s40_empty_space_stays_while_devices_use_it_and_a_lost_space_is_made_again(Ctx& x) {
	World w(x.cr, x.dir, { "web", "web" }, Limits(), {}, x.real, x.realBase);
	Client &wa = w[0], &p = w[1];
	Preview pv;
	const std::string code = wa.turnOn();
	wa.sync();
	p.link(code, pv);
	p.sync();
	for (int k = 0; k < 8; k++) {
		w.clock.tick(DAY);
		wa.sync();
		p.sync();
		w.cleanup();
	}
	CHECK(w.view(wa).exists && wa.enabled() && p.enabled());
	wa.create("First", cdlWith(20));
	wa.sync();
	p.sync();
	CHECK(p.names() == V({ "First" }));
	w.tamper(wa, "loseSpace");   // lost without a marker
	wa.sync();
	p.sync();
	CHECK(wa.status() == "synced" && p.status() == "synced" && w.circuitsOnServer(wa, x.cr) == 1 && p.names() == V({ "First" }));
}

void s41_switch_flips_are_sent_lazily(Ctx& x) {
	World w(x.cr, x.dir, { "app", "app" }, Limits(), {}, x.real, x.realBase);
	Client &a = w[0], &b = w[1];
	const std::string la = a.create("Lamp", CDL());
	Preview pv;
	const std::string code = a.turnOn();
	a.sync();
	b.link(code, pv);
	b.sync();
	const std::string rid = a.mapped(la);
	const int64_t ver = w.recs(a)[rid].ver;
	a.edit(la, cdlSwitch(true));
	a.sync(false);
	CHECK(w.recs(a)[rid].ver == ver);   // only a switch changed: not yet
	w.clock.tick(11 * MINUTE);
	a.sync(false);
	CHECK(w.recs(a)[rid].ver == ver + 1);   // sent after 10 minutes
	a.edit(la, cdlWith(25));
	a.sync(false);
	CHECK(w.recs(a)[rid].ver == ver + 2);   // a real change goes at once
}

void s42_purge_while_paging_restarts_as_a_full_pull(Ctx& x) {
	World w(x.cr, x.dir, { "app", "app" }, Limits(), {}, x.real, x.realBase);
	Client &a = w[0], &b = w[1];
	std::vector<std::string> keep;
	for (int i = 0; i < 3; i++) keep.push_back(a.create("K" + std::to_string(i), cdlWith(20 + i)));
	const std::string gone = a.create("Gone", cdlWith(40));
	Preview pv;
	const std::string code = a.turnOn();
	a.sync();
	b.link(code, pv);
	b.sync();
	b.net.down = true;
	for (size_t i = 0; i < keep.size(); i++) a.edit(keep[i], cdlWith(60 + (int)i));
	a.remove(gone);
	a.sync();
	w.clock.tick(401 * DAY);
	a.sync();
	b.net.down = false;
	b.changesLimit = 1;   // pages of one entry
	bool purged = false;
	b.net.sinces.clear();
	b.net.before = [&](const std::string& name) {
		if (name == "changes" && !purged && b.net.sinces.size() == 2) {
			w.cleanup();   // the daily cleanup runs between two pages
			purged = true;
		}
	};
	b.sync();
	const std::vector<int64_t>& sinces = b.net.sinces;
	CHECK(purged && !sinces.empty() && sinces[0] > 0);
	CHECK(std::find(sinces.begin() + 1, sinces.end(), 0) != sinces.end());   // restarted as a full pull
	b.net.before = nullptr;
	b.changesLimit = 0;
	a.sync();
	b.sync();
	CHECK(sameEverywhere({ &a, &b }) && b.state().cursor == w.seq(a));
}

void s43_switch_flip_here_real_edit_there_is_not_a_conflict(Ctx& x) {
	World w(x.cr, x.dir, { "app", "web" }, Limits(), {}, x.real, x.realBase);
	Client &a = w[0], &wb = w[1];
	const std::string la = a.create("Lamp", CDL());
	Preview pv;
	const std::string code = a.turnOn();
	a.sync();
	wb.link(code, pv);
	wb.sync();
	const std::string lw = wb.byName("Lamp")[0];
	wb.edit(lw, cdlSwitch(true));   // earlier: a switch flipped online
	a.edit(la, cdlWith(30));        // later: a real edit in the app
	wb.sync();
	a.sync();
	wb.sync();
	CHECK(a.names() == V({ "Lamp" }) && wb.names() == V({ "Lamp" }) && sameEverywhere({ &a, &wb }));
	CHECK(a.core->structureHash(a.cdlOf(la)) == a.core->structureHash(cdlWith(30)));
	CHECK(!a.anyVersionNote(la, "changed on both"));
}

// The state file keeps everything across a restart (every scenario reloads it before each
// cycle; this checks the fields directly).
void state_file_round_trip(Ctx& x) {
	State s;
	s.spaceId = "9d8d8b91b2ab7698e8b8e2b01513d9eb";
	s.epoch = std::string(32, 'e');
	s.libraryId = std::string(32, 'a');
	s.libraryGen = 41;
	s.deviceId = std::string(32, 'd');
	s.deviceName = "Levi\xE2\x80\x99s MacBook Air";
	s.offset = -1250;
	s.cursor = 57;
	s.purgedSeq = 3;
	s.lastSyncAt = 1759575600123LL;
	s.joining = true;
	RecState r;
	r.local = "20261004-101010-48213";
	r.ver = 3;
	r.n = std::string(64, '1');
	r.c = std::string(64, '2');
	r.st = std::string(64, '3');
	r.sent.has = true;
	r.sent.ver = 4;
	r.sent.n = r.n;
	r.sent.c = r.c;
	r.sent.st = r.st;
	s.records["3b241101-e2bb-4255-8caf-4136c566a962"] = r;
	s.seen["3b241101-e2bb-4255-8caf-4136c566a962"] = SeenRec{ 3, std::string(32, 'h'), r.local };
	s.force.push_back("3b241101-e2bb-4255-8caf-4136c566a962");
	s.device.has = true;
	s.device.id = "d072d9d7-722d-472b-aa6b-e355ad92c15c";
	s.device.ver = 4;
	s.device.at = 1759575960000LL;
	s.lazySince = 5;
	s.tooBig["x"] = std::string(64, '4');
	s.unreadable["y"] = { 4, "newer" };
	json::Value v;
	State back;
	CHECK(json::parse(json::write(s.toJson()), v) && State::fromJson(v, back));
	CHECK(json::write(back.toJson()) == json::write(s.toJson()));
	CHECK(back.records.at("3b241101-e2bb-4255-8caf-4136c566a962").sent.ver == 4 && back.offset == -1250 && back.joining);
	(void)x;
}

}  // namespace

void scenarioTests(Crypto& cr, const std::string& tempDir, Report& report, Host* httpOnly, const std::string& serverBase) {
	struct Row {
		const char* name;
		void (*fn)(Ctx&);
		bool overHttp;   // also run against a mock server (through its /__mock/ controls)
	};
	static const Row rows[] = {
		{ "s01_first_device_then_link", s01_first_device_then_link, true },
		{ "s02_link_with_unknown_code_creates_nothing", s02_link_with_unknown_code_creates_nothing, true },
		{ "s03_edit_offline_on_two_devices_app_app", s03_edit_offline_on_two_devices_app_app, true },
		{ "s04_edit_offline_web_loses_gets_a_copy", s04_edit_offline_web_loses_gets_a_copy, true },
		{ "s05_edit_offline_web_wins_app_keeps_version", s05_edit_offline_web_wins_app_keeps_version, true },
		{ "s06_delete_here_edit_there_edit_wins_both_orders", s06_delete_here_edit_there_edit_wins_both_orders, true },
		{ "s07_delete_reaches_other_devices_into_trash", s07_delete_reaches_other_devices_into_trash, true },
		{ "s08_rename_one_side_and_both_sides", s08_rename_one_side_and_both_sides, true },
		{ "s09_switches_only_on_both_app_and_web_is_not_a_conflict", s09_switches_only_on_both_app_and_web_is_not_a_conflict, true },
		{ "s10_join_with_overlapping_libraries", s10_join_with_overlapping_libraries, true },
		{ "s11_quota_exceeded", s11_quota_exceeded, true },
		{ "s12_too_large_record_is_skipped_not_retried", s12_too_large_record_is_skipped_not_retried, true },
		{ "s13_412_retry_when_another_device_pushes_between_pull_and_push", s13_412_retry_when_another_device_pushes_between_pull_and_push, true },
		{ "s14_answer_lost_mid_push_no_duplicates", s14_answer_lost_mid_push_no_duplicates, true },
		{ "s15_replayed_write_is_idempotent", s15_replayed_write_is_idempotent, true },
		{ "s16_long_offline_device_after_tombstones_are_purged", s16_long_offline_device_after_tombstones_are_purged, true },
		{ "s17_cloud_copy_deleted_from_another_device", s17_cloud_copy_deleted_from_another_device, true },
		{ "s18_turn_off_keeps_or_removes", s18_turn_off_keeps_or_removes, true },
		{ "s19_damaged_record_is_not_applied_and_is_repaired", s19_damaged_record_is_not_applied_and_is_repaired, true },
		{ "s20_three_devices_converge", s20_three_devices_converge, true },
		{ "s21_connection_drops_mid_push", s21_connection_drops_mid_push, true },
		{ "s22_retry_after_is_respected", s22_retry_after_is_respected, true },
		{ "s23_open_window_with_unsaved_edits_is_not_overwritten", s23_open_window_with_unsaved_edits_is_not_overwritten, true },
		{ "s24_clean_window_reloads_runtime_change_waits_for_input", s24_clean_window_reloads_runtime_change_waits_for_input, true },
		{ "s25_mass_delete_here_asks_first", s25_mass_delete_here_asks_first, true },
		{ "s26_library_restored_from_a_backup", s26_library_restored_from_a_backup, true },
		{ "s30_newer_payload_is_left_alone", s30_newer_payload_is_left_alone, true },
		{ "s31_join_rule_only_when_joining", s31_join_rule_only_when_joining, true },
		{ "s32_lost_answer_then_another_device_builds_on_it", s32_lost_answer_then_another_device_builds_on_it, true },
		{ "s33_rename_on_one_device_edit_on_the_other", s33_rename_on_one_device_edit_on_the_other, true },
		{ "s34_clock_skew_does_not_pick_the_wrong_winner", s34_clock_skew_does_not_pick_the_wrong_winner, true },
		{ "s35_server_rolls_a_record_back", s35_server_rolls_a_record_back, true },
		{ "s36_forged_deletes_trash_nothing", s36_forged_deletes_trash_nothing, true },
		{ "s37_many_deletes_from_another_device_ask_first", s37_many_deletes_from_another_device_ask_first, true },
		{ "s38_space_reset_is_noticed", s38_space_reset_is_noticed, true },
		{ "s39_someone_elses_code_shows_whose_it_is", s39_someone_elses_code_shows_whose_it_is, true },
		{ "s40_empty_space_stays_while_devices_use_it_and_a_lost_space_is_made_again",
		  s40_empty_space_stays_while_devices_use_it_and_a_lost_space_is_made_again, true },
		{ "s41_switch_flips_are_sent_lazily", s41_switch_flips_are_sent_lazily, true },
		{ "s42_purge_while_paging_restarts_as_a_full_pull", s42_purge_while_paging_restarts_as_a_full_pull, true },
		{ "s43_switch_flip_here_real_edit_there_is_not_a_conflict", s43_switch_flip_here_real_edit_there_is_not_a_conflict, true },
		{ "state_file_round_trip", state_file_round_trip, false },
	};
	const std::string base = files::join(tempDir, "scenarios");
	for (int pass = 0; pass < 2; pass++) {
		if (pass == 1 && (!httpOnly || serverBase.empty())) break;
		for (const Row& row : rows) {
			if (pass == 1 && !row.overHttp) continue;
			const std::string label = std::string(pass ? "mock server: " : "") + row.name;
			if (!report.wanted(label)) continue;
			Ctx x{ cr, files::join(base, row.name), pass ? httpOnly : nullptr, pass ? serverBase : std::string() };
			files::removeAll(x.dir);
			try {
				row.fn(x);
				report.line(true, label);
				files::removeAll(x.dir);
			} catch (const std::exception& e) {
				report.line(false, label, e.what());
			}
		}
	}
	engineTests(cr, files::join(tempDir, "engine"), report);
	if (report.failed == 0) files::removeAll(base);
}

}  // namespace test
}  // namespace clsync
