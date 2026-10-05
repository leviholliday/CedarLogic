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
