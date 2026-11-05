// Usage stats in the app (see UsageStats.h).

#include "UsageStats.h"
#include "Actions.h"
#include "Shortcuts.h"
#include "SyncApp.h"
#include "Window.h"

#include <memory>
#include <thread>

namespace usagestats {

bool testRun = false;

namespace {

int g_depth = 0;
bool g_started = false;
UINT_PTR g_timer = 0;
int g_ticks = 0;
HWND g_post = nullptr;
std::function<void()> g_onChange;
const UINT kRun = WM_APP + 40;
const int kTick = 5;   // seconds between looks

// What a thread of its own finished, back on the window thread.
LRESULT CALLBACK postProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
	if (msg == kRun) {
		std::unique_ptr<std::function<void()>> f(reinterpret_cast<std::function<void()>*>(lp));
		if (f && *f) guarded("usage stats", [&] { (*f)(); });
		return 0;
	}
	return DefWindowProcW(h, msg, wp, lp);
}

// (On the window thread, before any thread of its own posts to it.)
void ensurePoster() {
	if (g_post) return;
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof wc;
	wc.lpfnWndProc = postProc;
	wc.hInstance = appInstance();
	wc.lpszClassName = L"CedarLogicUsagePost";
	RegisterClassExW(&wc);
	g_post = CreateWindowExW(0, L"CedarLogicUsagePost", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, appInstance(), nullptr);
}

void onWindowThread(std::function<void()> f) {
	if (g_post == nullptr) return;
	auto* p = new std::function<void()>(std::move(f));
	if (!PostMessageW(g_post, kRun, 0, (LPARAM)p)) delete p;
}

void changed() {
	if (g_onChange) g_onChange();
}

void inBackground(std::function<void()> work) {
	ensurePoster();
	std::thread([work] {
		guarded("usage stats", [&] { work(); });
		onWindowThread([] { changed(); });
	}).detach();
}

bool appInFront() {
	DWORD pid = 0;
	GetWindowThreadProcessId(GetForegroundWindow(), &pid);
	return pid == GetCurrentProcessId();
}

double idleSeconds() {
	LASTINPUTINFO li = { sizeof li, 0 };
	if (!GetLastInputInfo(&li)) return 0;
	return (GetTickCount() - li.dwTime) / 1000.0;
}

CircuitWindow* frontCircuit() {
	const HWND fg = GetForegroundWindow();
	if (fg == nullptr) return nullptr;
	const HWND root = GetAncestor(fg, GA_ROOTOWNER);
	for (CircuitWindow* w : circuitWindows())
		if (w->window() == fg || w->window() == root) return w;
	return nullptr;
}

// Every 5 seconds: in front and touched in the last 5 minutes is time used;
// with a circuit window in front, in focus mode or Simulation View, that too.
void CALLBACK sampleTick(HWND, UINT, UINT_PTR, DWORD) {
	guarded("usage stats", [] {
		g_ticks++;
		usage::Engine& e = engine();
		if (e.enabled() && appInFront() && idleSeconds() < 300) {
			int focus = 0, sim = 0;
			if (CircuitWindow* w = frontCircuit()) {
				if (w->focusMode()) focus = kTick;
				if (w->simView()) sim = kTick;
			}
			e.addTime(kTick, focus, sim);
		}
		if (g_ticks % 60 == 0) {
			e.save();
			const usage::Now n = nowFromApp();
			inBackground([n] { engine().sendIfDue(n); });
		}
	});
}

}  // namespace

usage::Engine& engine() {
	// Made once and never freed: a send may still be going on its own
	// thread as the app quits.
	static usage::Engine* e = [] {
		usage::Engine* x = new usage::Engine(settingsDir() + "\\Usage");
		x->endpoint = [] { return usage::endpointUrl(testRun); };
		x->policyOff = [] { return usage::policySet(L"DisableUsageStats"); };
		x->setSwitch(prefs().usageStats);
		return x;
	}();
	return *e;
}

usage::Now nowFromApp() {
	usage::Now n;
	n.settings = usage::nonDefault(prefs());
	n.syncOn = syncapp::enabled();
	return n;
}

bool policyOff() { return usage::policySet(L"DisableUsageStats"); }

bool on() { return engine().switchedOn() && usage::Engine::isUsageId(engine().id()); }

void set(bool value) {
	prefs().usageStats = value;
	prefs().save();
	engine().set(value);
	// This launch counts, from the moment they said yes.
	if (value && g_started) engine().noteSession();
	const usage::Now n = nowFromApp();
	inBackground([value, n] {
		if (value) engine().sendIfDue(n);
		else engine().flushDeletes();
	});
	changed();
}

void start() {
	if (g_started) return;
	g_started = true;
	ensurePoster();
	const usage::Now n = nowFromApp();
	inBackground([n] {
		engine().sendIfDue(n);
		engine().noteSession();
		engine().save();
	});
	g_timer = SetTimer(nullptr, 0, kTick * 1000, sampleTick);
}

void shutdown() {
	if (g_timer) KillTimer(nullptr, g_timer);
	g_timer = 0;
	if (g_started || on()) engine().save();
}

Counting::Counting(int command) {
	if (g_depth++ != 0) return;
	usage::Engine& e = engine();
	if (!e.enabled()) return;
	std::string id;
	if (const actions::Def* d = actions::forCommand(command)) id = d->id;
	else if (const shortcuts::Action* a = shortcuts::forCommand(command)) id = a->id;
	if (!id.empty()) e.count(id);
}

Counting::~Counting() { g_depth--; }

std::string preview() { return engine().preview(nowFromApp()); }

std::string lastSent() {
	const long long t = engine().lastSentAt();
	if (t <= 0) return std::string();
	// Unix seconds -> this PC's time, as Windows writes a date.
	ULARGE_INTEGER u;
	u.QuadPart = (ULONGLONG)(t + 11644473600LL) * 10000000ULL;
	FILETIME ft = { u.LowPart, u.HighPart };
	SYSTEMTIME utc, local;
	if (!FileTimeToSystemTime(&ft, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) return std::string();
	wchar_t d[80] = L"", tm[40] = L"";
	GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &local, L"d MMM yyyy", d, 80, nullptr);
	GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &local, nullptr, tm, 40);
	const std::string kind = engine().lastSentKind();
	return U(d) + ", " + U(tm) + " (" + (kind.empty() ? std::string("report") : kind) + ")";
}

int kept() { return engine().keptCount(); }

void deleteSent(std::function<void(bool done)> then) {
	ensurePoster();
	std::thread([then] {
		bool done = false;
		guarded("usage stats", [&] { done = engine().deleteSent(); });
		onWindowThread([then, done] {
			changed();
			if (then) then(done);
		});
	}).detach();
}

void onChange(std::function<void()> f) { g_onChange = std::move(f); }

}  // namespace usagestats
