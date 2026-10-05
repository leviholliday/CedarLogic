// The app's side of sync (see SyncApp.h): the engine and what it asks of the
// windows, and what the rest of the app tells it.

#include "SyncApp.h"
#include "Alert.h"
#include "Dialogs.h"
#include "Library.h"
#include "ShareLink.h"
#include "SyncPlatform.h"
#include "Window.h"

#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <mutex>

namespace syncapp {

namespace {

const wchar_t* kHostClass = L"CedarLogicSyncHost";
const UINT kCall = WM_APP + 41;   // the engine thread wants something run here
const UINT kTask = WM_APP + 42;   // something to do here, a moment from now

// A call from the engine thread to the UI thread, and the engine's wait for it.
struct Call {
	std::function<void()> fn;
	HANDLE done;
	std::mutex lock;
	bool started = false, abandoned = false;
	explicit Call(std::function<void()> f) : fn(std::move(f)), done(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
	~Call() { if (done) CloseHandle(done); }
};

class AppHost;

struct State {
	HWND window = nullptr;
	DWORD uiThread = 0;
	std::shared_ptr<AppHost> host;   // (shared: made complete after this, where it's defined)
	std::unique_ptr<clsync::Engine> engine;
	std::atomic<bool> closing{false};
	std::mutex lock;                              // the two queues
	std::deque<std::shared_ptr<Call>> calls;
	std::deque<std::function<void()>> tasks;
	// UI thread only:
	clsync::Status last;
	std::string notice;
	double noticeAt = -1e9;
	std::map<int, std::function<void(Event)>> listeners;
	int nextListener = 1;
	bool demoOn = false;
	bool active = true;
	double lastUserActive = -1e9;
};

State& S() {
	static State s;
	return s;
}

bool onUiThread() { return GetCurrentThreadId() == S().uiThread; }


void fire(Event e) {
	std::map<int, std::function<void(Event)>> copy = S().listeners;   // (a listener may remove itself)
	for (const auto& l : copy)
		if (S().listeners.count(l.first)) guarded("sync", [&] { l.second(e); });
}

HWND frontParentImpl() {
	HWND fg = GetForegroundWindow();
	for (CircuitWindow* w : circuitWindows()) {
		if (w->window() == fg || GetLastActivePopup(w->window()) == fg) return fg;
	}
	if (circuitWindows().empty()) return nullptr;
	CircuitWindow* w = circuitWindows().back();
	HWND popup = GetLastActivePopup(w->window());
	return popup ? popup : w->window();
}

CircuitWindow* windowForFolder(const std::string& folderId) {
	for (CircuitWindow* w : circuitWindows()) {
		library::Item it;
		if (library::itemFor(w->filePath(), it) && it.id == folderId) return w;
	}
	return nullptr;
}

// The engine's words say "Recently Deleted", which Windows has no list of: its
// circuits are kept in the library's Trash folder.
std::string forWindows(std::string text) {
	const std::string from = "Recently Deleted", to = "the Trash folder";
	for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) text.replace(at, from.size(), to);
	return text;
}

class AppHost : public syncplat::PlatformHost {
public:
	// Runs fn on the UI thread and waits for it, as the engine asks. Dropped
	// (so the engine stops what it was doing) once the app is closing.
	void onMain(const std::function<void()>& fn) override {
		State& s = S();
		if (s.closing) return;
		if (onUiThread()) {
			fn();
			return;
		}
		auto call = std::make_shared<Call>(fn);
		{
			std::lock_guard<std::mutex> guard(s.lock);
			s.calls.push_back(call);
		}
		PostMessageW(s.window, kCall, 0, 0);
		for (;;) {
			if (WaitForSingleObject(call->done, 50) == WAIT_OBJECT_0) return;
			if (s.closing) {
				std::lock_guard<std::mutex> guard(call->lock);
				if (!call->started) {
					call->abandoned = true;   // never run
					return;
				}
			}
		}
	}

	// Every circuit open in a window with unsaved changes is saved now (the
	// windows save themselves every few seconds; this just doesn't wait).
	void flushOpen(std::function<void()> done) override {
		for (CircuitWindow* w : std::vector<CircuitWindow*>(circuitWindows())) {
			library::Item it;
			// (One in the middle of a drag isn't saved half done: it stays unsaved, and the engine waits for it.)
			if (library::itemFor(w->filePath(), it) && w->isDirty() && !w->busyEditing()) w->saveQuietly(false);
		}
		done();
	}

	clsync::WindowState windowState(const std::string& folderId) override {
		clsync::WindowState st;
		if (CircuitWindow* w = windowForFolder(folderId)) {
			st.open = true;
			// Unsaved work, or something open over the window (a dialog, Your
			// Circuits, a list): that code is waiting on this circuit as it is, so
			// a change from elsewhere waits for it.
			st.dirty = w->isDirty() || !IsWindowEnabled(w->window());
			st.lastInputAt = w->lastInputMs();
		}
		return st;
	}

	void circuitReplaced(const std::string& folderId, const std::string& fromDevice) override {
		if (CircuitWindow* w = windowForFolder(folderId)) w->reloadFromSync(fromDevice);
	}

	void closeCircuit(const std::string& folderId) override {
		if (CircuitWindow* w = windowForFolder(folderId)) w->closeForSync();
	}

	void libraryChanged() override {
		for (CircuitWindow* w : std::vector<CircuitWindow*>(circuitWindows())) w->libraryChanged();
		fire(Event::Library);
	}

	void statusChanged(const clsync::Status& st) override {
		S().last = st;
		fire(Event::Status);
	}

	void notice(const std::string& text) override {
		State& s = S();
		s.notice = forWindows(text);
		s.noticeAt = nowSeconds();
		// On the window in front, as a note, as Saved and Copied are.
		CircuitWindow* target = nullptr;
		HWND fg = GetForegroundWindow();
		for (CircuitWindow* w : circuitWindows()) if (w->window() == fg) target = w;
		if (target == nullptr && !circuitWindows().empty()) target = circuitWindows().back();
		if (target) target->note(s.notice);
		fire(Event::Status);
	}

	// A question is answered whenever the person decides: asked a moment from
	// now, not inside the engine's call (which would hold the engine thread).
	void askMassDelete(int count, std::function<void(bool)> answer) override {
		later([count, answer] { syncapp::askMassDelete(count, answer); });
	}
	void askIncomingDeletes(int count, const std::string& from, std::function<void(bool)> answer) override {
		later([count, from, answer] { syncapp::askIncomingDeletes(count, from, answer); });
	}

	static void later(std::function<void()> task) { defer(std::move(task)); }
};

LRESULT CALLBACK hostProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
	State& s = S();
	if (msg == kCall) {
		std::deque<std::shared_ptr<Call>> take;
		{
			std::lock_guard<std::mutex> guard(s.lock);
			take.swap(s.calls);
		}
		for (const std::shared_ptr<Call>& c : take) {
			bool run;
			{
				std::lock_guard<std::mutex> guard(c->lock);
				run = !c->abandoned && !s.closing;
				if (run) c->started = true;
			}
			if (run) guarded("sync", [&] { c->fn(); });
			SetEvent(c->done);
		}
		return 0;
	}
	if (msg == kTask) {
		std::deque<std::function<void()>> take;
		{
			std::lock_guard<std::mutex> guard(s.lock);
			take.swap(s.tasks);
		}
		for (const std::function<void()>& t : take) guarded("sync", [&] { t(); });
		return 0;
	}
	return DefWindowProcW(h, msg, wp, lp);
}

std::string envOr(const char* name, const char* fallback) {
	char buf[512];
	const DWORD n = GetEnvironmentVariableA(name, buf, sizeof buf);
	return n > 0 && n < sizeof buf ? std::string(buf, n) : std::string(fallback);
}

// A sample state for the pictures.
const int64_t kDay = 24 * 3600 * 1000LL;

int64_t nowMs() {
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	ULARGE_INTEGER u;
	u.LowPart = ft.dwLowDateTime;
	u.HighPart = ft.dwHighDateTime;
	return (int64_t)((u.QuadPart - 116444736000000000ULL) / 10000);
}

const char kDemoCode[] = "000G40R40M30E209185GR38E1YZ4";   // the design's first test code

}  // namespace

// ---- Life ---------------------------------------------------------------------------

void defer(std::function<void()> task) {
	State& s = S();
	if (s.window == nullptr) {   // (no engine: nothing to wait for)
		task();
		return;
	}
	{
		std::lock_guard<std::mutex> guard(s.lock);
		s.tasks.push_back(std::move(task));
	}
	PostMessageW(s.window, kTask, 0, 0);
}

void init() {
	State& s = S();
	if (s.engine) return;
	s.uiThread = GetCurrentThreadId();
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof wc;
	wc.lpfnWndProc = hostProc;
	wc.hInstance = appInstance();
	wc.lpszClassName = kHostClass;
	RegisterClassExW(&wc);
	s.window = CreateWindowExW(0, kHostClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, appInstance(), nullptr);
	if (s.window == nullptr) return;
	// The library's folder exists (a library with no circuit in it yet is
	// still a library, not an unreadable one).
	SHCreateDirectoryExW(nullptr, W(library::root()).c_str(), nullptr);
	s.host = std::make_shared<AppHost>();
	clsync::Config c;
	c.libraryRoot = library::root();
	c.syncDir = syncplat::syncDir();
	c.serverBase = syncplat::serverBase();
	c.appKey = envOr("CL_FEEDBACK_KEY", CL_FEEDBACK_KEY);   // the apps' own key, as Send Feedback sends
	c.client = syncplat::clientName();
	c.defaultDeviceName = syncplat::deviceName();
	// What the digest leaves out of a gate is what the gate library says is its default.
	c.gateDefault = [](const std::string& lib, bool gui, const std::string& name) -> std::string {
		char buf[4096];
		buf[0] = 0;
		if (!cl_sync_core_gate_default(nullptr, lib.c_str(), gui, name.c_str(), buf, sizeof buf)) return std::string("\x01");
		buf[sizeof buf - 1] = 0;
		return std::string(buf);
	};
	s.engine.reset(new clsync::Engine(c, syncplat::crypto(), *s.host));
	// A circuit saved, made, renamed or deleted here: sync looks soon.
	library::setChangedHook([] { libraryChanged(); });
}

void start() {
	if (S().engine) S().engine->start();
}

void shutdown() {
	State& s = S();
	s.closing = true;
	s.engine.reset();   // stops the thread (a call it is waiting on is dropped)
	s.host.reset();
	if (s.window) DestroyWindow(s.window);
	s.window = nullptr;
}

void stateChanged() { fire(Event::Status); }

clsync::Engine* engine() { return S().engine.get(); }

HWND frontWindow() { return frontParentImpl(); }

void demo(bool on) { S().demoOn = on; }
bool isDemo() { return S().demoOn; }

// ---- State ---------------------------------------------------------------------------

bool enabled() {
	if (S().demoOn) return true;
	return S().engine && S().engine->enabled();
}

clsync::Status status() {
	clsync::Status st;
	if (S().demoOn) {
		st.kind = clsync::Status::Synced;
		st.text = "Synced just now";
		st.lastSyncAt = nowMs();
		st.circuits = 42;
		return st;
	}
	if (S().engine) st = S().engine->status();
	return st;
}

std::string statusLine(const clsync::Status& s) {
	if (s.kind == clsync::Status::Off) return std::string();
	std::string t = s.text;
	if ((s.kind == clsync::Status::Synced || s.kind == clsync::Status::Full) && s.circuits > 0)
		t += strf(" · %d circuit%s", s.circuits, s.circuits == 1 ? "" : "s");
	return t;
}

std::string code() {
	if (S().demoOn) return kDemoCode;
	return S().engine ? S().engine->code() : std::string();
}

std::string deviceName() {
	if (S().demoOn) return "Levi’s Surface Laptop";
	return S().engine ? S().engine->deviceName() : syncplat::deviceName();
}

void setDeviceName(const std::string& name) {
	if (S().demoOn || !S().engine) return;
	std::string n = name;
	const size_t a = n.find_first_not_of(" \t"), b = n.find_last_not_of(" \t");
	n = a == std::string::npos ? std::string() : n.substr(a, b - a + 1);
	if (n.empty() || n == S().engine->deviceName()) return;
	S().engine->setDeviceName(n);
}

std::vector<std::pair<std::string, int64_t>> devices() {
	std::vector<std::pair<std::string, int64_t>> out;
	if (S().demoOn) {
		const int64_t now = nowMs();
		return { { deviceName(), now }, { "Safari on iPhone", now - 2 * kDay }, { "Raspberry Pi", now - 90 * kDay } };
	}
	if (S().engine) out = S().engine->devices();
	// This PC first, even before its device record has been sent.
	const std::string me = deviceName();
	const int64_t at = status().lastSyncAt;
	auto mine = std::find_if(out.begin(), out.end(), [&](const std::pair<std::string, int64_t>& d) { return d.first == me; });
	if (mine == out.end()) out.push_back({ me, at ? at : nowMs() });
	std::stable_sort(out.begin(), out.end(), [&](const std::pair<std::string, int64_t>& a, const std::pair<std::string, int64_t>& b) {
		if ((a.first == me) != (b.first == me)) return a.first == me;
		return a.second > b.second;
	});
	return out;
}

std::string recentNotice() {
	State& s = S();
	return nowSeconds() - s.noticeAt < 60 ? s.notice : std::string();
}

int listen(std::function<void(Event)> fn) {
	State& s = S();
	const int id = s.nextListener++;
	s.listeners[id] = std::move(fn);
	return id;
}

void unlisten(int id) { S().listeners.erase(id); }

// ---- Triggers --------------------------------------------------------------------------

void appActivated(bool active) {
	State& s = S();
	if (!s.engine || s.active == active) return;
	s.active = active;
	if (active) s.engine->appActivated();
	else s.engine->appDeactivated();   // a flush: what's waiting goes now
}

void userActive() {
	State& s = S();
	if (!s.engine) return;
	const double t = nowSeconds();
	if (t - s.lastUserActive < 5) return;
	s.lastUserActive = t;
	s.engine->userActive();
}

void libraryChanged() {
	if (S().engine && !S().closing) S().engine->noteLibraryChanged();
}

void syncNow() {
	if (S().engine) S().engine->syncNow();
}

void quitting() {
	State& s = S();
	if (!s.engine || s.closing || !s.engine->enabled()) return;
	// The engine's thread sends what's left and says when (within 5 seconds);
	// the messages go on meanwhile, so the screen doesn't freeze.
	struct Wait {
		HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		~Wait() { CloseHandle(event); }
	};
	auto wait = std::make_shared<Wait>();
	s.engine->quitting([wait] { SetEvent(wait->event); });
	const ULONGLONG end = GetTickCount64() + 5300;
	for (;;) {
		const ULONGLONG now = GetTickCount64();
		if (now >= end) break;
		const DWORD r = MsgWaitForMultipleObjects(1, &wait->event, FALSE, (DWORD)(end - now), QS_ALLINPUT);
		if (r == WAIT_OBJECT_0 || r == WAIT_FAILED) break;
		MSG m;
		while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
			if (m.message == WM_QUIT) {
				PostQuitMessage((int)m.wParam);
				return;
			}
			TranslateMessage(&m);
			DispatchMessageW(&m);
		}
	}
}

// ---- Links -------------------------------------------------------------------------------

bool isSyncLink(const std::string& arg) {
	if (!sharelink::isLink(arg)) return false;
	size_t at = arg.find(':') + 1;
	while (at < arg.size() && at < arg.find(':') + 3 && arg[at] == '/') at++;
	size_t end = arg.find_first_of("#?/", at);
	if (end == std::string::npos) end = arg.size();
	return lowerCase(arg.substr(at, end - at)) == "sync";
}

void openLink(HWND parent, const std::string& link) {
	if (!S().engine && !S().demoOn) {
		showMessage(parent, Tone::Info, "Sync isn't running", "Start CedarLogic normally to link this PC.");
		return;
	}
	// No code in it (cedarlogic://sync): just the place to type one.
	if (link.find("k=") == std::string::npos) {
		setPreferencesPage(5);
		showPreferencesDialog(parent);
		return;
	}
	std::string code, why;
	if (!clsync::parseCode(syncplat::crypto(), link, code, why)) {
		showMessage(parent, Tone::Warning, "That sync link has a problem", clsync::whyText(why, link));
		return;
	}
	if (enabled()) {
		if (S().engine && S().engine->code() == code) {
			showMessage(parent, Tone::Info, "This PC already syncs with this code.", "");
			return;
		}
		if (!askConfirm(parent, "This PC syncs with another code",
		                "Switch to this one? Circuits here stay; they’ll be added to the other synced circuits.", "Switch", "Cancel"))
			return;
		S().engine->turnOff(false);
	}
	haveCode(parent, link, true);
}

// ---- The questions ------------------------------------------------------------------------

void askMassDelete(int count, std::function<void(bool)> answer) {
	Alert a;
	a.title = "Sync";
	a.heading = strf("%d synced circuits aren’t on this PC any more", count);
	a.text = "Delete them from your other devices too, or bring them back here from the synced copy?";
	a.badge = 2;
	a.buttons = { { "Bring Them Back", 0, 1 }, { "Delete Them Everywhere", 1, 2, true } };
	a.escape = 0;
	a.enter = 0;
	const int r = runAlert(frontWindow(), a);
	answer(r == 1);
}

void askIncomingDeletes(int count, const std::string& from, std::function<void(bool)> answer) {
	const clsync::Status st = status();
	Alert a;
	a.title = "Sync";
	const std::string who = from.empty() ? std::string("Another device") : from;
	a.heading = st.circuits >= count && st.circuits > 0 ? strf("%s deleted %d of your %d synced circuits", who.c_str(), count, st.circuits)
	                                                    : strf("%s deleted %d of your synced circuits", who.c_str(), count);
	a.text = "Move them out of Your Circuits on this PC (they’re kept in its Trash folder), or keep them? "
	         "Kept circuits are sent back to your other devices.";
	a.badge = 2;
	a.buttons = { { "Keep Them", 0, 1 }, { "Move Them to the Trash Folder", 1, 0 } };
	a.escape = 0;
	a.enter = 0;
	const int r = runAlert(frontWindow(), a);
	answer(r == 1);
}

}  // namespace syncapp
