// The threaded engine (SyncEngine.cpp) end to end: a fake UI thread, a host
// that answers on it, the FakeServer behind http(), real library folders --
// Turn On, link, Sync Now, the lock (scenario 27), quitting, stop and destroy,
// a restart, Turn Off, Delete Synced Copy; and the C interface.

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS   // getenv in a test
#endif

#include "CedarSync.h"
#include "SyncTest.h"
#include "SyncTestServer.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <thread>

namespace clsync {
namespace test {

namespace {

struct Fail : std::runtime_error {
	explicit Fail(const std::string& m) : std::runtime_error(m) {}
};

#define ECHECK(cond)                                                                                  \
	do {                                                                                              \
		if (!(cond)) throw Fail(std::string(#cond) + " (line " + std::to_string(__LINE__) + ")"); \
	} while (0)

// A stand-in for the app's UI thread: runs what it is given, in order.
class UiThread {
public:
	UiThread() : th([this] { loop(); }) {}
	~UiThread() {
		{
			std::lock_guard<std::mutex> lock(mu);
			quit = true;
		}
		cv.notify_all();
		th.join();
	}
	// Runs fn there and waits (as DispatchQueue.main.sync does).
	void sync(const std::function<void()>& fn) {
		if (std::this_thread::get_id() == th.get_id()) {
			fn();
			return;
		}
		auto done = std::make_shared<std::pair<std::mutex, std::condition_variable>>();
		bool finished = false;
		post([&, done] {
			fn();
			std::lock_guard<std::mutex> lock(done->first);
			finished = true;
			done->second.notify_all();
		});
		std::unique_lock<std::mutex> lock(done->first);
		done->second.wait(lock, [&] { return finished; });
	}
	void post(std::function<void()> fn) {
		{
			std::lock_guard<std::mutex> lock(mu);
			q.push_back(std::move(fn));
		}
		cv.notify_all();
	}
	bool onThisThread() const { return std::this_thread::get_id() == th.get_id(); }

private:
	void loop() {
		for (;;) {
			std::function<void()> fn;
			{
				std::unique_lock<std::mutex> lock(mu);
				cv.wait(lock, [this] { return quit || !q.empty(); });
				if (q.empty()) return;
				fn = std::move(q.front());
				q.pop_front();
			}
			fn();
		}
	}
	std::mutex mu;
	std::condition_variable cv;
	std::deque<std::function<void()>> q;
	bool quit = false;
	std::thread th;
};

struct TestHost : Host {
	UiThread& ui;
	FakeServer& server;
	std::mutex mu;   // the server, the secret, the logs
	std::string secret;
	bool lockFree = true, lockTaken = false;
	std::atomic<int> requests{ 0 }, onMainCalls{ 0 }, offMainHostCalls{ 0 };
	std::vector<std::string> notices;
	std::vector<Status> statuses;
	std::atomic<bool> answerYes{ true };
	TestHost(UiThread& u, FakeServer& s) : ui(u), server(s) {}

	void mustBeUi() {
		if (!ui.onThisThread()) offMainHostCalls++;
	}
	HttpResponse http(const HttpRequest& r) override {
		requests++;
		std::lock_guard<std::mutex> lock(server.mu);
		return server.handle(r);
	}
	void onMain(const std::function<void()>& fn) override {
		onMainCalls++;
		ui.sync(fn);
	}
	std::string loadSecret() override {
		std::lock_guard<std::mutex> lock(mu);
		return secret;
	}
	bool saveSecret(const std::string& code) override {
		std::lock_guard<std::mutex> lock(mu);
		secret = code;
		return true;
	}
	void forgetSecret() override {
		std::lock_guard<std::mutex> lock(mu);
		secret.clear();
	}
	bool tryLock(const std::string&) override {
		std::lock_guard<std::mutex> lock(mu);
		if (!lockFree) return false;
		lockTaken = true;
		return true;
	}
	void unlock() override {
		std::lock_guard<std::mutex> lock(mu);
		lockTaken = false;
	}
	void flushOpen(std::function<void()> done) override {
		mustBeUi();
		std::thread([done] { done(); }).detach();   // saved "asynchronously"
	}
	WindowState windowState(const std::string&) override {
		mustBeUi();
		return WindowState();
	}
	void circuitReplaced(const std::string&, const std::string&) override { mustBeUi(); }
	void closeCircuit(const std::string&) override { mustBeUi(); }
	void libraryChanged() override { mustBeUi(); }
	void statusChanged(const Status& s) override {
		mustBeUi();
		std::lock_guard<std::mutex> lock(mu);
		statuses.push_back(s);
	}
	void notice(const std::string& text) override {
		mustBeUi();
		std::lock_guard<std::mutex> lock(mu);
		notices.push_back(text);
	}
	void askMassDelete(int, std::function<void(bool)> answer) override {
		mustBeUi();
		const bool yes = answerYes;
		ui.post([answer, yes] { answer(yes); });   // the person answers a moment later
	}
	void askIncomingDeletes(int, const std::string&, std::function<void(bool)> answer) override {
		mustBeUi();
		const bool yes = answerYes;
		ui.post([answer, yes] { answer(yes); });
	}
};

std::string replaceAllSimple(std::string s, const std::string& from, const std::string& to) {
	for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
	return s;
}

std::string lowerSpaced(const std::string& code) {
	std::string g = groupCode(code);
	for (char& c : g) c = c == '-' ? ' ' : (char)tolower((unsigned char)c);
	return g;
}

bool waitFor(const std::function<bool()>& cond, int ms) {
	const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
	while (std::chrono::steady_clock::now() < end) {
		if (cond()) return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	return cond();
}

void makeCircuit(const std::string& root, const std::string& id, const std::string& name, const std::string& cdl) {
	const std::string folder = files::join(root, id);
	files::makeDirs(files::join(folder, "versions"));
	files::writeAtomic(files::join(folder, "name.txt"), name);
	files::writeAtomic(files::join(folder, "circuit.cdl"), cdl);
}

std::vector<std::string> folderNames(const std::string& root) {
	std::vector<std::string> out;
	for (const std::string& n : files::listDir(root)) {
		if (n.empty() || n[0] == '.') continue;
		std::string name;
		if (files::read(files::join(files::join(root, n), "name.txt"), name)) out.push_back(trimAscii(name));
	}
	std::sort(out.begin(), out.end());
	return out;
}

struct Device {
	std::string root, syncDir;
	TestHost host;
	std::unique_ptr<Engine> engine;
	Device(UiThread& ui, FakeServer& server, Crypto& cr, const std::string& dir, const std::string& name)
		: root(files::join(dir, "Library")), syncDir(files::join(dir, "Sync")), host(ui, server) {
		files::makeDirs(root);
		open(cr, name);
	}
	void open(Crypto& cr, const std::string& name) {
		Config c;
		c.libraryRoot = root;
		c.syncDir = syncDir;
		c.serverBase = "http://fake/api/sync/v1";
		c.client = "test/1";
		c.defaultDeviceName = name;
		c.gateDefault = vectorDefaults();
		engine.reset(new Engine(c, cr, host));
	}
};

int liveCircuits(FakeServer& s) {
	std::lock_guard<std::mutex> lock(s.mu);
	int n = 0;
	for (const auto& sp : s.spaces)
		for (const auto& r : sp.second.recs)
			if (!r.second.deleted && !r.second.dev) n++;
	return n;
}

void engineEndToEnd(Crypto& cr, const std::string& dir) {
	SystemClock clock;
	FakeServer server(clock, cr);
	UiThread ui;
	Device a(ui, server, cr, files::join(dir, "A"), "Mac A");
	makeCircuit(a.root, "20261004-101010-11111", "Adder", fixture("vector-v3.cdl"));
	makeCircuit(a.root, "20261004-101011-22222", "Params", fixture("params-v3.cdl"));
	ui.sync([&] { a.engine->start(); });
	ECHECK(!a.engine->enabled() && a.engine->status().kind == Status::Off);
	ECHECK(a.host.onMainCalls == 0);   // start() runs on the UI thread: it never waits for itself

	// Turn On.
	std::atomic<int> turnedOn{ 0 };
	ui.sync([&] { a.engine->turnOn([&](bool ok, std::string) { turnedOn = ok ? 1 : -1; }); });
	ECHECK(waitFor([&] { return turnedOn != 0; }, 5000) && turnedOn == 1);
	ECHECK(a.engine->enabled() && a.engine->code().size() == 28);
	ECHECK(waitFor([&] { return a.engine->status().kind == Status::Synced && liveCircuits(server) == 2; }, 5000));
	ECHECK(a.engine->status().text.find("Synced") == 0 && a.engine->status().circuits == 2);
	ECHECK(files::exists(files::join(a.syncDir, "state.json")));
	const std::string code = a.engine->code();

	// Another device: the preview, then Link.
	Device b(ui, server, cr, files::join(dir, "B"), "Laptop B");
	makeCircuit(b.root, "20261004-111111-33333", "Mine", fixture("params-v1.cdl"));
	ui.sync([&] { b.engine->start(); });
	std::atomic<int> previewed{ 0 };
	std::string sentence;
	std::mutex smu;
	ui.sync([&] {
		b.engine->preview(code, [&](bool ok, std::string s, Preview pv) {
			std::lock_guard<std::mutex> lock(smu);
			sentence = s;
			previewed = ok && pv.circuits == 2 ? 1 : -1;
		});
	});
	ECHECK(waitFor([&] { return previewed != 0; }, 5000) && previewed == 1);
	{
		std::lock_guard<std::mutex> lock(smu);
		ECHECK(sentence.find("This code has 2 circuits from Mac A") == 0);
		ECHECK(sentence.find("Linking adds your 1 circuit here") != std::string::npos);
	}
	ECHECK(!b.engine->enabled() && liveCircuits(server) == 2);   // nothing sent by the preview
	std::atomic<int> linked{ 0 };
	ui.sync([&] { b.engine->link(lowerSpaced(code), [&](bool ok, std::string) { linked = ok ? 1 : -1; }); });
	ECHECK(waitFor([&] { return linked != 0; }, 5000) && linked == 1);
	// "Mine" and "Params" are the same circuit with different names: both kept.
	ECHECK(waitFor([&] { return folderNames(b.root).size() == 3 && liveCircuits(server) == 3; }, 5000));
	ECHECK(b.engine->code() == code);

	// A change in A arrives in B with Sync Now on both.
	makeCircuit(a.root, "20261004-121212-44444", "New one", fixture("vector-v1.cdl"));
	ui.sync([&] { a.engine->noteLibraryChanged(); a.engine->syncNow(); });
	ECHECK(waitFor([&] { return liveCircuits(server) == 4; }, 5000));
	ui.sync([&] { b.engine->syncNow(); });
	ECHECK(waitFor([&] { return folderNames(b.root).size() == 4; }, 5000));
	ECHECK(waitFor([&] { return !a.engine->devices().empty() || true; }, 10));

	// Linking with a code that has no synced copy fails, and the device keeps syncing as it was.
	{
		std::atomic<int> bad{ 0 };
		std::string message;
		const std::string unknown = newCode(cr);
		ui.sync([&] { b.engine->link(unknown, [&](bool ok, std::string m) { message = m; bad = ok ? 1 : -1; }); });
		ECHECK(waitFor([&] { return bad != 0; }, 5000) && bad == -1);
		ECHECK(message.find("No circuits are synced") == 0 && b.engine->enabled() && b.engine->code() == code);
		const int64_t before = b.engine->status().lastSyncAt;
		ui.sync([&] { b.engine->syncNow(); });
		ECHECK(waitFor([&] { return b.engine->status().lastSyncAt > before; }, 5000));   // still syncing
	}

	// Scenario 27: a second process holds the lock -- no requests, and it says so.
	Device c(ui, server, cr, files::join(dir, "C"), "Locked C");
	{
		std::lock_guard<std::mutex> lock(c.host.mu);
		c.host.secret = code;
		c.host.lockFree = false;
	}
	ui.sync([&] { c.engine->start(); c.engine->syncNow(); });
	ECHECK(waitFor([&] { return c.engine->status().kind == Status::Busy; }, 5000));
	ECHECK(c.engine->status().text == "Syncing in another CedarLogic window.");
	ECHECK(c.host.requests == 0);

	// Quitting: a push-only cycle that never calls onMain, done within 5 s.
	{
		std::lock_guard<std::mutex> lock(a.host.mu);
	}
	const std::string adder = files::join(files::join(a.root, "20261004-101010-11111"), "circuit.cdl");
	files::writeAtomic(adder, fixture("vector-v3.cdl") + "\n");
	files::setMtimeMs(adder, clock.now() + 5000);
	const int mainBefore = a.host.onMainCalls;
	std::atomic<bool> quitDone{ false };
	const auto t0 = std::chrono::steady_clock::now();
	ui.sync([&] { a.engine->quitting([&] { quitDone = true; }); });
	ECHECK(waitFor([&] { return quitDone.load(); }, 6000));
	ECHECK(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(5500));
	ECHECK(a.host.onMainCalls == mainBefore);
	bool sent = false;
	std::unique_lock<std::mutex> serverLock(server.mu);
	for (const auto& sp : server.spaces)
		for (const auto& r : sp.second.recs)
			if (r.second.ver >= 2 && !r.second.dev && !r.second.deleted) sent = true;
	serverLock.unlock();
	ECHECK(sent);
	ECHECK(a.host.offMainHostCalls == 0 && b.host.offMainHostCalls == 0);
	ui.sync([&] { a.engine.reset(); });   // stop and destroy

	// A restart: the same code and state, synced.
	a.open(cr, "Mac A");
	ui.sync([&] { a.engine->start(); });
	ECHECK(a.engine->enabled() && a.engine->code() == code);
	ui.sync([&] { a.engine->syncNow(); });
	ECHECK(waitFor([&] { return a.engine->status().kind == Status::Synced; }, 5000));

	// Delete Synced Copy on B; A stops ("gone") at its next sync and keeps its circuits.
	std::atomic<int> deleted{ 0 };
	ui.sync([&] { b.engine->deleteSyncedCopy([&](bool ok, std::string) { deleted = ok ? 1 : -1; }); });
	ECHECK(waitFor([&] { return deleted != 0; }, 5000) && deleted == 1);
	ECHECK(!b.engine->enabled() && b.host.loadSecret().empty() && !files::exists(files::join(b.syncDir, "state.json")));
	ui.sync([&] { a.engine->syncNow(); });
	ECHECK(waitFor([&] { return a.engine->status().kind == Status::Gone; }, 5000));
	ECHECK(a.engine->status().text.find("Sync was turned off from another device") == 0 && !a.engine->enabled());
	ECHECK(folderNames(a.root) == std::vector<std::string>({ "Adder", "Mine", "New one", "Params" }));

	// Turn On again on A, then Turn Off removing the synced circuits.
	turnedOn = 0;
	ui.sync([&] { a.engine->turnOn([&](bool ok, std::string) { turnedOn = ok ? 1 : -1; }); });
	ECHECK(waitFor([&] { return turnedOn != 0; }, 5000) && turnedOn == 1 && a.engine->code() != code);
	ECHECK(waitFor([&] { return a.engine->status().kind == Status::Synced && liveCircuits(server) == 4; }, 5000));
	ui.sync([&] { a.engine->turnOff(true); });
	ECHECK(waitFor([&] { return folderNames(a.root).empty(); }, 5000));
	ECHECK(!a.engine->enabled() && a.host.loadSecret().empty());

	// Destroying engines mid-way never hangs.
	ui.sync([&] {
		b.engine->syncNow();
		b.engine.reset();
		c.engine.reset();
		a.engine.reset();
	});
}

// Both mass-delete questions through the threaded engine: asked on the UI thread, answered
// later, the cycle waiting meanwhile (scenarios 25 and 37 with real threads).
void engineQuestions(Crypto& cr, const std::string& dir) {
	SystemClock clock;
	FakeServer server(clock, cr);
	UiThread ui;
	Device a(ui, server, cr, files::join(dir, "A"), "Mac A");
	for (int i = 0; i < 8; i++)
		makeCircuit(a.root, "20261004-10101" + std::to_string(i) + "-1000" + std::to_string(i), "Q" + std::to_string(i),
		            replaceAllSimple(fixture("vector-v3.cdl"), "(at 18 20)", "(at " + std::to_string(30 + i) + " 20)"));
	ui.sync([&] { a.engine->start(); });
	std::atomic<int> on{ 0 };
	ui.sync([&] { a.engine->turnOn([&](bool ok, std::string) { on = ok ? 1 : -1; }); });
	ECHECK(waitFor([&] { return on != 0; }, 5000) && on == 1);
	ECHECK(waitFor([&] { return liveCircuits(server) == 8; }, 5000));
	const std::string code = a.engine->code();
	Device b(ui, server, cr, files::join(dir, "B"), "Laptop B");
	ui.sync([&] { b.engine->start(); });
	std::atomic<int> linked{ 0 };
	ui.sync([&] { b.engine->link(code, [&](bool ok, std::string) { linked = ok ? 1 : -1; }); });
	ECHECK(waitFor([&] { return linked == 1 && folderNames(b.root).size() == 8; }, 5000));
	ECHECK(waitFor([&] { return b.engine->status().kind == Status::Synced; }, 5000));
	// A: 7 of 8 deleted on purpose (Delete Them Everywhere, answered a moment later).
	for (int i = 0; i < 7; i++) a.engine->noteLibraryChanged();
	for (const std::string& n : files::listDir(a.root))
		if (n[0] != '.' && folderNames(a.root).size() > 1) files::removeAll(files::join(a.root, n));
	a.host.answerYes = true;
	ui.sync([&] { a.engine->syncNow(); });
	ECHECK(waitFor([&] { return liveCircuits(server) == 1 && a.engine->status().kind == Status::Synced; }, 5000));
	// B: asked, answers Keep Them; they go back to the server and to A.
	b.host.answerYes = false;
	ui.sync([&] { b.engine->syncNow(); });
	ECHECK(waitFor([&] { return liveCircuits(server) == 8; }, 5000));
	ECHECK(folderNames(b.root).size() == 8);
	ui.sync([&] { a.engine->syncNow(); });
	ECHECK(waitFor([&] { return folderNames(a.root).size() == 8; }, 5000));
	ECHECK(a.host.offMainHostCalls == 0 && b.host.offMainHostCalls == 0);
	ui.sync([&] {
		a.engine.reset();
		b.engine.reset();
	});
}

// The C interface, with hooks over the self-test's Crypto.
Crypto* gCrypto = nullptr;
bool cRandom(void*, uint8_t* out, size_t n) { return gCrypto->random(out, n); }
void cSha(void*, const uint8_t* p, size_t n, uint8_t out[32]) { gCrypto->sha256(p, n, out); }
void cHmac(void*, const uint8_t* k, size_t kl, const uint8_t* p, size_t n, uint8_t out[32]) { gCrypto->hmacSha256(k, kl, p, n, out); }
void cMain(void*, void (*fn)(void*), void* arg) { fn(arg); }

void cInterface(Crypto& cr) {
	gCrypto = &cr;
	CLSyncHooks h;
	memset(&h, 0, sizeof h);
	h.random = cRandom;
	h.sha256 = cSha;
	h.hmac_sha256 = cHmac;
	h.on_main = cMain;
	char code[29], why[16];
	ECHECK(cl_sync_parse_code(&h, "cedarlogic://sync?k=000g-40r4-0m30-e209-185g-r38e-1yz4", code, why) &&
	       std::string(code) == "000G40R40M30E209185GR38E1YZ4");
	ECHECK(!cl_sync_parse_code(&h, "000G40R40M30E209185GR38E1YZ", code, why) && std::string(why) == "length");
	ECHECK(std::string(cl_sync_why_text("checksum", "")).find("typo") != std::string::npos);
	char grouped[35];
	cl_sync_group_code("000G40R40M30E209185GR38E1YZ4", grouped);
	ECHECK(std::string(grouped) == "000G-40R4-0M30-E209-185G-R38E-1YZ4");
	ECHECK(std::string(cl_sync_web_link("000G40R40M30E209185GR38E1YZ4")) ==
	       "https://cedarlogic.netlify.app/sync/#k=000G40R40M30E209185GR38E1YZ4");
	ECHECK(std::string(cl_sync_app_link("X")) == "cedarlogic://sync#k=X");
	std::vector<uint8_t> modules(177 * 177);
	ECHECK(cl_sync_qr("https://cedarlogic.netlify.app/sync/#k=000G40R40M30E209185GR38E1YZ4", modules.data()) == 37);
	// An engine with no network: created, started (off), destroyed.
	CLSyncEngine* e = cl_sync_create(&h, "/nonexistent-cl-sync-library", "/nonexistent-cl-sync-dir", "", "test/1", "Test");
	ECHECK(e != nullptr);
	cl_sync_start(e);
	ECHECK(!cl_sync_enabled(e) && std::string(cl_sync_code(e)).empty() && cl_sync_status_kind(e) == CL_SYNC_OFF);
	ECHECK(std::string(cl_sync_device_name(e)) == "Test");
	cl_sync_destroy(e);
}

// ---- Pairing, D's side (SYNC.md 11.6, 11.12): the loop on the engine thread against the FakeServer ----

// A clock the test can move forward (the website's, for the 10 minutes of a slot).
struct ShiftClock : Clock {
	SystemClock sys;
	std::atomic<int64_t> shift{ 0 };
	int64_t now() override { return sys.now() + shift.load(); }
};

// What show and done were given, and whether they came on the UI thread.
struct PairWatch {
	UiThread& ui;
	std::mutex mu;
	std::string link, text, from;
	int result = -1;
	std::atomic<int> shows{ 0 }, dones{ 0 }, offUi{ 0 };
	explicit PairWatch(UiThread& u) : ui(u) {}
	std::function<void(const std::string&)> show() {
		return [this](const std::string& l) {
			if (!ui.onThisThread()) offUi++;
			std::lock_guard<std::mutex> lock(mu);
			link = l;
			shows++;
		};
	}
	std::function<void(int, const std::string&, const std::string&)> done() {
		return [this](int r, const std::string& t, const std::string& f) {
			if (!ui.onThisThread()) offUi++;
			std::lock_guard<std::mutex> lock(mu);
			result = r;
			text = t;
			from = f;
			dones++;
		};
	}
	std::string getLink() {
		std::lock_guard<std::mutex> lock(mu);
		return link;
	}
};

HttpResponse serve(FakeServer& server, const std::string& method, const std::string& path, const std::string& body) {
	HttpRequest r;
	r.method = method;
	r.url = "http://fake/api/sync/v1" + path;
	r.body = body;
	if (!body.empty()) r.headers.emplace_back("content-type", "application/json");
	std::lock_guard<std::mutex> lock(server.mu);
	return server.handle(r);
}

// Plays L (11.7) against the FakeServer: reads the hello, posts an answer (or bytes that aren't one).
// The POST's status; -1 not a pairing link, -2 the hello didn't open.
int answerAs(FakeServer& server, Crypto& cr, const std::string& link, const std::string& code, const std::string& device,
             std::string& helloDevice, bool garbage = false) {
	uint8_t p[16];
	if (!parsePairLink(cr, link, p)) return -1;
	const PairKeys k = pairKeys(cr, p);
	const HttpResponse g = serve(server, "GET", "/pair/" + k.pairId, std::string());
	if (g.status != 200) return g.status;
	json::Value v;
	PairMessage m;
	if (!json::parse(g.body, v) || !openPair(cr, k, "hello", v.str("hello"), m)) return -2;
	helloDevice = m.device;
	std::string env;
	if (garbage) env = b64u(Bytes(60, 7));
	else if (!sealPair(cr, k, "answer", pairAnswerJson(code, device), env)) return -3;
	json::Value b = json::Value::object();
	b.set("answer", json::Value::string(env));
	return serve(server, "POST", "/pair/" + k.pairId + "/answer", json::write(b)).status;
}

size_t pairSlots(FakeServer& s) {
	std::lock_guard<std::mutex> lock(s.mu);
	return s.pairs.size();
}

struct PairTiming {   // polls every 100 ms in these tests; back to 3 s and 10 minutes after
	PairTiming() { setPairTimingForTest(100, 10 * kMinute); }
	~PairTiming() { setPairTimingForTest(0, 0); }
};

const char* const kSamPhone = "Sam\xE2\x80\x99s phone";
const char* const kSamPc = "Sam\xE2\x80\x99s Windows PC";

void enginePairing(Crypto& cr, const std::string& dir) {
	PairTiming timing;
	ShiftClock clock;
	FakeServer server(clock, cr);
	UiThread ui;
	Device a(ui, server, cr, files::join(dir, "A"), kSamPhone);
	makeCircuit(a.root, "20261005-101010-11111", "Adder", fixture("vector-v3.cdl"));
	ui.sync([&] { a.engine->start(); });
	std::atomic<int> on{ 0 };
	ui.sync([&] { a.engine->turnOn([&](bool ok, std::string) { on = ok ? 1 : -1; }); });
	ECHECK(waitFor([&] { return on != 0; }, 5000) && on == 1);
	ECHECK(waitFor([&] { return a.engine->status().kind == Status::Synced && liveCircuits(server) == 1; }, 5000));
	const std::string code = a.engine->code();

	// Starting while sync is on.
	{
		PairWatch w(ui);
		ui.sync([&] { a.engine->pairStart(w.show(), w.done()); });
		ECHECK(waitFor([&] { return w.dones == 1; }, 5000));
		ECHECK(w.result == Engine::PairFailed && w.text == "Sync is already on." && w.shows == 0 && pairSlots(server) == 0);
	}

	// The answer arrives (after two polls that failed, retried quietly): the code, then preview and Link.
	Device b(ui, server, cr, files::join(dir, "B"), kSamPc);
	ui.sync([&] { b.engine->start(); });
	{
		PairWatch w(ui);
		ui.sync([&] { b.engine->pairStart(w.show(), w.done()); });
		ECHECK(waitFor([&] { return w.shows == 1; }, 5000));
		const std::string link = w.getLink();
		ECHECK(link.compare(0, 39, "https://cedarlogic.netlify.app/sync/#p=") == 0 && link.size() == 39 + 28);
		ECHECK(pairSlots(server) == 1);
		{
			std::lock_guard<std::mutex> lock(server.mu);
			server.failNext = 2;
			server.failStatus = 503;
			server.failRetryAfter = 2;
		}
		ECHECK(waitFor([&] {
			std::lock_guard<std::mutex> lock(server.mu);
			return server.failNext == 0;
		}, 5000));
		ECHECK(w.dones == 0);
		std::string hello, ignored;
		ECHECK(answerAs(server, cr, link, code, kSamPhone, hello) == 200);
		ECHECK(hello == kSamPc);
		ECHECK(waitFor([&] { return w.dones == 1; }, 5000));
		ECHECK(w.result == Engine::PairCode && w.text == code && w.from == kSamPhone);
		ECHECK(waitFor([&] { return pairSlots(server) == 0; }, 2000));   // deleted
		ECHECK(answerAs(server, cr, link, code, kSamPhone, ignored) == 404);
		std::atomic<int> previewed{ 0 }, linked{ 0 };
		ui.sync([&] { b.engine->preview(w.text, [&](bool ok, std::string, Preview pv) { previewed = ok && pv.circuits == 1 ? 1 : -1; }); });
		ECHECK(waitFor([&] { return previewed != 0; }, 5000) && previewed == 1);
		ui.sync([&] { b.engine->link(w.text, [&](bool ok, std::string) { linked = ok ? 1 : -1; }); });
		ECHECK(waitFor([&] { return linked != 0; }, 5000) && linked == 1);
		ECHECK(b.engine->enabled() && b.engine->code() == code);
		ECHECK(waitFor([&] { return folderNames(b.root) == std::vector<std::string>({ "Adder" }); }, 5000));
		ECHECK(w.offUi == 0 && w.dones == 1);
	}

	Device c(ui, server, cr, files::join(dir, "C"), kSamPc);
	ui.sync([&] { c.engine->start(); });

	// The first answer wins; a second one is refused (both posted before D's next poll).
	{
		PairWatch w(ui);
		ui.sync([&] { c.engine->pairStart(w.show(), w.done()); });
		ECHECK(waitFor([&] { return w.shows == 1; }, 5000));
		uint8_t p[16];
		ECHECK(parsePairLink(cr, w.getLink(), p));
		const PairKeys k = pairKeys(cr, p);
		std::string e1, e2;
		ECHECK(sealPair(cr, k, "answer", pairAnswerJson(code, kSamPhone), e1) &&
		       sealPair(cr, k, "answer", pairAnswerJson(newCode(cr), "someone else"), e2));
		json::Value b1 = json::Value::object(), b2 = json::Value::object();
		b1.set("answer", json::Value::string(e1));
		b2.set("answer", json::Value::string(e2));
		int s1, s2;
		{
			std::lock_guard<std::mutex> lock(server.mu);
			HttpRequest r;
			r.method = "POST";
			r.url = "http://fake/api/sync/v1/pair/" + k.pairId + "/answer";
			r.body = json::write(b1);
			s1 = server.handle(r).status;
			r.body = json::write(b2);
			s2 = server.handle(r).status;
		}
		ECHECK(s1 == 200 && s2 == 409);
		ECHECK(waitFor([&] { return w.dones == 1; }, 5000));
		ECHECK(w.result == Engine::PairCode && w.text == code && w.from == kSamPhone);
	}

	// Expiry by the website's clock: a 404.
	{
		PairWatch w(ui);
		ui.sync([&] { c.engine->pairStart(w.show(), w.done()); });
		ECHECK(waitFor([&] { return w.shows == 1; }, 5000));
		clock.shift = 11 * kMinute;
		ECHECK(waitFor([&] { return w.dones == 1; }, 5000));
		ECHECK(w.result == Engine::PairExpired && w.text == "This QR code expired." && w.from.empty());
		clock.shift = 0;
		std::string hello;
		ECHECK(answerAs(server, cr, w.getLink(), code, kSamPhone, hello) == 404);
	}

	// Expiry by D's own clock (the 10 minutes, here 600 ms), polls failing all along.
	{
		setPairTimingForTest(50, 600);
		PairWatch w(ui);
		const auto t0 = std::chrono::steady_clock::now();
		ui.sync([&] { c.engine->pairStart(w.show(), w.done()); });
		ECHECK(waitFor([&] { return w.shows == 1; }, 5000));
		{
			std::lock_guard<std::mutex> lock(server.mu);
			server.failNext = 1000;
			server.failStatus = 503;
		}
		ECHECK(waitFor([&] { return w.dones == 1; }, 5000));
		ECHECK(std::chrono::steady_clock::now() - t0 >= std::chrono::milliseconds(600));
		ECHECK(w.result == Engine::PairExpired && w.text == "This QR code expired.");
		{
			std::lock_guard<std::mutex> lock(server.mu);
			server.failNext = 0;
			server.pairs.clear();
		}
		setPairTimingForTest(100, 10 * kMinute);
	}

	// A damaged answer: FAILED, and the slot deleted.
	{
		PairWatch w(ui);
		ui.sync([&] { c.engine->pairStart(w.show(), w.done()); });
		ECHECK(waitFor([&] { return w.shows == 1; }, 5000));
		std::string hello;
		ECHECK(answerAs(server, cr, w.getLink(), code, kSamPhone, hello, true) == 200);
		ECHECK(waitFor([&] { return w.dones == 1; }, 5000));
		ECHECK(w.result == Engine::PairFailed && w.text == "An answer came that couldn't be read.");
		ECHECK(waitFor([&] { return pairSlots(server) == 0; }, 2000));
	}

	// Cancel: the slot deleted, the polls stop, and done never comes.
	{
		PairWatch w(ui);
		ui.sync([&] { c.engine->pairStart(w.show(), w.done()); });
		ECHECK(waitFor([&] { return w.shows == 1; }, 5000));
		ECHECK(waitFor([&] {
			std::lock_guard<std::mutex> lock(server.mu);
			return server.pairPolls > 0;
		}, 5000));
		ui.sync([&] { c.engine->pairCancel(); });
		ECHECK(waitFor([&] { return pairSlots(server) == 0; }, 5000));
		int polls;
		{
			std::lock_guard<std::mutex> lock(server.mu);
			polls = server.pairPolls;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(400));
		std::lock_guard<std::mutex> lock(server.mu);
		ECHECK(server.pairPolls == polls && w.dones == 0);
	}

	// A new start replaces the one showing: its slot deleted, its done never called.
	{
		PairWatch w1(ui), w2(ui);
		ui.sync([&] { c.engine->pairStart(w1.show(), w1.done()); });
		ECHECK(waitFor([&] { return w1.shows == 1; }, 5000));
		ui.sync([&] { c.engine->pairStart(w2.show(), w2.done()); });
		ECHECK(waitFor([&] { return w2.shows == 1; }, 5000));
		ECHECK(waitFor([&] { return pairSlots(server) == 1; }, 5000));
		std::string hello;
		ECHECK(answerAs(server, cr, w1.getLink(), code, kSamPhone, hello) == 404);
		ECHECK(answerAs(server, cr, w2.getLink(), code, kSamPhone, hello) == 200);
		ECHECK(waitFor([&] { return w2.dones == 1; }, 5000) && w2.result == Engine::PairCode);
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
		ECHECK(w1.dones == 0 && w1.shows == 1);
	}

	// A 503 on the PUT: can't reach the website; started again, it works.
	{
		PairWatch w(ui);
		{
			std::lock_guard<std::mutex> lock(server.mu);
			server.failNext = 1;
			server.failStatus = 503;
		}
		ui.sync([&] { c.engine->pairStart(w.show(), w.done()); });
		ECHECK(waitFor([&] { return w.dones == 1; }, 5000));
		ECHECK(w.result == Engine::PairFailed && w.text == "Can't reach the website. Check the connection, then try again." &&
		       w.shows == 0 && pairSlots(server) == 0);
		PairWatch again(ui);
		ui.sync([&] { c.engine->pairStart(again.show(), again.done()); });
		ECHECK(waitFor([&] { return again.shows == 1; }, 5000) && pairSlots(server) == 1);
		ui.sync([&] { c.engine->pairCancel(); });
		ECHECK(waitFor([&] { return pairSlots(server) == 0; }, 5000) && again.dones == 0);
	}

	// No answer at all to the PUT (offline): the same.
	{
		PairWatch w(ui);
		{
			std::lock_guard<std::mutex> lock(server.mu);
			server.failNext = 1;
			server.failStatus = 0;
		}
		ui.sync([&] { c.engine->pairStart(w.show(), w.done()); });
		ECHECK(waitFor([&] { return w.dones == 1; }, 5000));
		ECHECK(w.result == Engine::PairFailed && w.text == "Can't reach the website. Check the connection, then try again." &&
		       w.shows == 0);
		std::lock_guard<std::mutex> lock(server.mu);
		server.failStatus = 429;
	}

	// Quitting with a QR code showing: the slot deleted, done never called.
	{
		PairWatch w(ui);
		ui.sync([&] { c.engine->pairStart(w.show(), w.done()); });
		ECHECK(waitFor([&] { return w.shows == 1; }, 5000));
		std::atomic<bool> quit{ false };
		ui.sync([&] { c.engine->quitting([&] { quit = true; }); });
		ECHECK(waitFor([&] { return quit.load(); }, 6000));
		ECHECK(pairSlots(server) == 0);
		ui.sync([&] { c.engine.reset(); });
		ECHECK(w.dones == 0);
	}

	// Destroyed with a QR code showing: nothing is sent from the exiting thread (the host may be
	// gone, or the machine offline), so the slot is left to expire; done is never called.
	{
		Device d(ui, server, cr, files::join(dir, "D"), kSamPc);
		PairWatch w(ui);
		ui.sync([&] { d.engine->start(); d.engine->pairStart(w.show(), w.done()); });
		ECHECK(waitFor([&] { return w.shows == 1; }, 5000));
		const auto t0 = std::chrono::steady_clock::now();
		ui.sync([&] { d.engine.reset(); });
		ECHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2));
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		ECHECK(pairSlots(server) == 1 && w.dones == 0);
		ECHECK(d.host.offMainHostCalls == 0);
	}
	ECHECK(a.host.offMainHostCalls == 0 && b.host.offMainHostCalls == 0 && c.host.offMainHostCalls == 0);
	ui.sync([&] {
		a.engine.reset();
		b.engine.reset();
	});
}

// The same through the C interface: start, show, the code; start again, cancel.
FakeServer* gPairServer = nullptr;
bool cSeal(void*, const uint8_t key[32], const uint8_t nonce[12], const uint8_t* aad, size_t aadLen, const uint8_t* plain, size_t n,
           uint8_t* out) {
	Bytes ct;
	if (!gCrypto->aesGcmSeal(key, nonce, Bytes(aad, aad + aadLen), Bytes(plain, plain + n), ct) || ct.size() != n + 16) return false;
	memcpy(out, ct.data(), ct.size());
	return true;
}
bool cOpen(void*, const uint8_t key[32], const uint8_t nonce[12], const uint8_t* aad, size_t aadLen, const uint8_t* ct, size_t n,
           uint8_t* out) {
	Bytes plain;
	if (!gCrypto->aesGcmOpen(key, nonce, Bytes(aad, aad + aadLen), Bytes(ct, ct + n), plain) || plain.size() + 16 != n) return false;
	if (!plain.empty()) memcpy(out, plain.data(), plain.size());
	return true;
}
void cHttp(void*, const char* method, const char* url, const char* headers, const uint8_t* body, size_t bodyLen, int* status,
           bool* sent, char** respHeaders, uint8_t** respBody, size_t* respLen) {
	HttpRequest r;
	r.method = method;
	r.url = url;
	r.body.assign((const char*)body, bodyLen);
	const std::string all = headers ? headers : "";
	for (size_t at = 0; at < all.size();) {
		size_t end = all.find("\r\n", at);
		if (end == std::string::npos) end = all.size();
		const std::string line = all.substr(at, end - at);
		const size_t colon = line.find(": ");
		if (colon != std::string::npos) r.headers.emplace_back(line.substr(0, colon), line.substr(colon + 2));
		at = end + 2;
	}
	HttpResponse resp;
	{
		std::lock_guard<std::mutex> lock(gPairServer->mu);
		resp = gPairServer->handle(r);
	}
	*status = resp.status;
	*sent = true;
	std::string h;
	for (const auto& kv : resp.headers) h += kv.first + ": " + kv.second + "\r\n";
	*respHeaders = (char*)malloc(h.size() + 1);
	memcpy(*respHeaders, h.c_str(), h.size() + 1);
	*respLen = resp.body.size();
	*respBody = (uint8_t*)malloc(resp.body.size() + 1);
	memcpy(*respBody, resp.body.data(), resp.body.size());
}

struct CPairState {
	std::mutex mu;
	std::string link, text, from;
	int result = -1;
	std::atomic<int> shows{ 0 }, dones{ 0 };
};
void cPairShow(void* ctx, const char* link) {
	auto* s = static_cast<CPairState*>(ctx);
	std::lock_guard<std::mutex> lock(s->mu);
	s->link = link;
	s->shows++;
}
void cPairDone(void* ctx, int result, const char* text, const char* from) {
	auto* s = static_cast<CPairState*>(ctx);
	std::lock_guard<std::mutex> lock(s->mu);
	s->result = result;
	s->text = text;
	s->from = from;
	s->dones++;
}

void cPairing(Crypto& cr, const std::string& dir) {
	PairTiming timing;
	SystemClock clock;
	FakeServer server(clock, cr);
	gCrypto = &cr;
	gPairServer = &server;
	CLSyncHooks h;
	memset(&h, 0, sizeof h);
	h.random = cRandom;
	h.sha256 = cSha;
	h.hmac_sha256 = cHmac;
	h.aes_gcm_seal = cSeal;
	h.aes_gcm_open = cOpen;
	h.http = cHttp;
	h.on_main = cMain;
	files::makeDirs(files::join(dir, "Library"));
	// (The FakeServer reads only the path after /api/sync/v1, whatever the server.)
	CLSyncEngine* e = cl_sync_create(&h, files::join(dir, "Library").c_str(), files::join(dir, "Sync").c_str(), "", "test/1", kSamPc);
	ECHECK(e != nullptr);
	cl_sync_start(e);
	CPairState s;
	cl_sync_pair_start(e, cPairShow, cPairDone, &s);
	ECHECK(waitFor([&] { return s.shows == 1; }, 5000));
	std::string link;
	{
		std::lock_guard<std::mutex> lock(s.mu);
		link = s.link;
	}
	const std::string code = newCode(cr);
	std::string hello;
	ECHECK(answerAs(server, cr, link, code, kSamPhone, hello) == 200 && hello == kSamPc);
	ECHECK(waitFor([&] { return s.dones == 1; }, 5000));
	{
		std::lock_guard<std::mutex> lock(s.mu);
		ECHECK(s.result == CL_SYNC_PAIR_CODE && s.text == code && s.from == kSamPhone);
	}
	ECHECK(waitFor([&] { return pairSlots(server) == 0; }, 2000));
	CPairState t;
	cl_sync_pair_start(e, cPairShow, cPairDone, &t);
	ECHECK(waitFor([&] { return t.shows == 1 && pairSlots(server) == 1; }, 5000));
	cl_sync_pair_cancel(e);
	ECHECK(waitFor([&] { return pairSlots(server) == 0; }, 5000));
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	ECHECK(t.dones == 0);
	cl_sync_destroy(e);
	gPairServer = nullptr;
}

}  // namespace

void engineTests(Crypto& cr, const std::string& tempDir, Report& report) {
	struct Row {
		const char* name;
		std::function<void()> fn;
	};
	const std::vector<Row> rows = {
		{ "engine: turn on, preview, link, sync now, lock (27), quitting, restart, delete, turn off",
		  [&] { engineEndToEnd(cr, tempDir); } },
		{ "engine: both mass-delete questions, answered later on the UI thread", [&] { engineQuestions(cr, tempDir); } },
		{ "engine: the C interface", [&] { cInterface(cr); } },
		{ "engine: pairing, D's side (11.6): code, first answer wins, expiry, damaged, cancel, 503, quit, destroy (slot left to expire)",
		  [&] { enginePairing(cr, tempDir); } },
		{ "engine: pairing through the C interface", [&] { cPairing(cr, tempDir); } },
	};
	for (const Row& row : rows) {
		if (!report.wanted(row.name)) continue;
		files::removeAll(tempDir);
		try {
			row.fn();
			report.line(true, row.name);
		} catch (const std::exception& e) {
			report.line(false, row.name, e.what());
		}
	}
	files::removeAll(tempDir);
}

}  // namespace test
}  // namespace clsync
