// Usage stats in the app (see UsageStats.h).

#include "UsageStats.h"
#include "Actions.h"
#include "Shortcuts.h"
#include "SyncApp.h"
#include "Window.h"

#include <cstring>
#include <memory>
#include <thread>

namespace usagestats {

bool testRun = false;

namespace {

bool g_started = false;
guint g_timer = 0;
int g_ticks = 0;
gint64 g_lastInput = 0;   // the monotonic clock's microseconds at the last key, click or pointer move in one of our windows
std::function<void()> g_onChange;
const int kTick = 5;   // seconds between looks

void changed() {
	if (g_onChange) g_onChange();
}

// `f` back on the main thread.
void onMainThread(std::function<void()> f) {
	auto* p = new std::function<void()>(std::move(f));
	g_idle_add_full(G_PRIORITY_DEFAULT, [](gpointer d) -> gboolean {
		std::unique_ptr<std::function<void()>> fn(static_cast<std::function<void()>*>(d));
		if (*fn) guarded("usage stats", [&] { (*fn)(); });
		return G_SOURCE_REMOVE;
	}, p, nullptr);
}

void inBackground(std::function<void()> work) {
	std::thread([work] {
		guarded("usage stats", [&] { work(); });
		onMainThread([] { changed(); });
	}).detach();
}

// Any key, click, scroll or pointer move in one of the app's windows.
gboolean inputHook(GSignalInvocationHint*, guint n, const GValue* values, gpointer) {
	if (n >= 2) {
		const GdkEvent* e = static_cast<const GdkEvent*>(g_value_get_boxed(&values[1]));
		if (e && (e->type == GDK_KEY_PRESS || e->type == GDK_BUTTON_PRESS || e->type == GDK_MOTION_NOTIFY || e->type == GDK_SCROLL ||
		          e->type == GDK_TOUCH_BEGIN))
			g_lastInput = g_get_monotonic_time();
	}
	return TRUE;
}

// One of CedarLogic's windows has the keyboard (in front).
bool appInFront() {
	bool front = false;
	GList* all = gtk_window_list_toplevels();
	for (GList* l = all; l && !front; l = l->next) front = gtk_window_is_active(GTK_WINDOW(l->data));
	g_list_free(all);
	return front;
}

CircuitWindow* frontCircuit() {
	for (CircuitWindow* w : circuitWindows())
		if (gtk_window_is_active(w->window())) return w;
	return nullptr;
}

// Every 5 seconds: in front and touched in the last 5 minutes is time used;
// with a circuit window in front, in focus mode or Simulation View, that too.
gboolean sampleTick(gpointer) {
	guarded("usage stats", [] {
		g_ticks++;
		usage::Engine& e = engine();
		const double idle = (g_get_monotonic_time() - g_lastInput) / 1e6;
		if (e.enabled() && appInFront() && idle < 300) {
			int focus = 0, sim = 0;
			if (CircuitWindow* w = frontCircuit()) {
				if (w->focusMode()) focus = kTick;
				if (w->simView()) sim = kTick;
			}
			e.addTime(kTick, focus, sim);
		}
		if (g_ticks % 60 == 0) {
			if (e.enabled()) e.save();
			const usage::Now n = nowFromApp();
			inBackground([n] { engine().sendIfDue(n); });
		}
	});
	return G_SOURCE_CONTINUE;
}

std::string configDir() {
	gchar* dir = g_build_filename(g_get_user_config_dir(), "CedarLogic", nullptr);
	std::string s = dir;
	g_free(dir);
	return s;
}

}  // namespace

usage::Engine& engine() {
	// Made once and never freed: a send may still be going on its own
	// thread as the app quits.
	static usage::Engine* e = [] {
		usage::Engine* x = new usage::Engine(configDir());
		x->endpoint = [] { return usage::endpointUrl(testRun); };
		x->policyOff = [] { return usage::policySet("DisableUsageStats"); };
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

bool policyOff() { return usage::policySet("DisableUsageStats"); }

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
	g_lastInput = g_get_monotonic_time();
	g_signal_add_emission_hook(g_signal_lookup("event", GTK_TYPE_WIDGET), 0, inputHook, nullptr, nullptr);
	const usage::Now n = nowFromApp();
	inBackground([n] {
		engine().sendIfDue(n);
		engine().noteSession();
		// (Off: no file is made for it.)
		if (engine().enabled()) engine().save();
	});
	g_timer = g_timeout_add_seconds(kTick, sampleTick, nullptr);
}

void shutdown() {
	if (g_timer) g_source_remove(g_timer);
	g_timer = 0;
	if (on()) engine().save();
}

void countAction(const char* gaction) {
	if (gaction == nullptr || !g_started) return;
	usage::Engine& e = engine();
	if (!e.enabled()) return;
	const std::string full = std::string("win.") + gaction;
	std::string id;
	if (const actions::Item* a = actions::byAction(full.c_str())) id = a->id;
	else
		for (const shortcuts::Action& s : shortcuts::all())
			if (s.gaction && full == s.gaction) { id = s.id; break; }
	if (!id.empty()) e.count(id);
}

std::string preview() { return engine().preview(nowFromApp()); }

std::string lastSent() {
	const long long t = engine().lastSentAt();
	if (t <= 0) return std::string();
	GDateTime* d = g_date_time_new_from_unix_local((gint64)t);
	if (d == nullptr) return std::string();
	gchar* text = g_date_time_format(d, "%-d %b %Y, %H:%M");
	std::string s = text ? text : "";
	g_free(text);
	g_date_time_unref(d);
	const std::string kind = engine().lastSentKind();
	return s + " (" + (kind.empty() ? std::string("report") : kind) + ")";
}

int kept() { return engine().keptCount(); }

void deleteSent(std::function<void(bool done)> then) {
	std::thread([then] {
		bool done = false;
		guarded("usage stats", [&] { done = engine().deleteSent(); });
		onMainThread([then, done] {
			changed();
			if (then) then(done);
		});
	}).detach();
}

void onChange(std::function<void()> f) { g_onChange = std::move(f); }

}  // namespace usagestats
