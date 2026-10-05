// Sync in the app (see SyncApp.h).

#include "SyncApp.h"
#include "Alert.h"
#include "Library.h"
#include "SyncPlatform.h"
#include "SyncUI.h"
#include "Window.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

namespace syncapp {

namespace {

struct Listener {
	guint id;
	bool library;
	std::function<void()> fn;
};
std::vector<Listener> gListeners;
guint gNextListener = 1;

clsync::Engine* gEngine = nullptr;
std::string gNotice;
gint64 gNoticeAt = 0;
// The app is on its way out: nothing waits for the main loop any more.
std::atomic<bool> gShuttingDown{ false };
bool gActive = false;

void fire(bool library) {
	const std::vector<Listener> all = gListeners;   // a listener may add or remove listeners
	for (const Listener& l : all) {
		if (l.library != library) continue;
		const guint id = l.id;
		// (Removed by an earlier one: not called.)
		const bool still = std::any_of(gListeners.begin(), gListeners.end(), [id](const Listener& x) { return x.id == id; });
		if (still) guarded("sync", [&] { l.fn(); });
	}
}

CircuitWindow* frontWindow() {
	for (CircuitWindow* w : circuitWindows())
		if (gtk_window_is_active(w->window())) return w;
	return circuitWindows().empty() ? nullptr : circuitWindows().front();
}

// The windows showing a library circuit.
std::vector<CircuitWindow*> windowsFor(const std::string& folderId) {
	std::vector<CircuitWindow*> out;
	for (CircuitWindow* w : circuitWindows()) {
		library::Item it;
		if (!w->filePath().empty() && library::itemFor(w->filePath(), it) && it.id == folderId) out.push_back(w);
	}
	return out;
}

// What the engine calls into: the app's windows and this machine's files and network.
struct Call {
	std::mutex mu;
	std::condition_variable cv;
	bool running = false, done = false, cancelled = false;
	std::function<void()> fn;
};

struct Ask {
	bool local = false;
	int count = 0;
	std::string devices;
	std::function<void(bool)> answer;
};

class AppHost : public clsync::Host {
public:
	clsync::HttpResponse http(const clsync::HttpRequest& r) override { return syncplatform::http(r); }

	// The engine thread asks for the main loop and waits; if the app is quitting, the call is dropped
	// (and what it would have done never runs, as the engine expects).
	void onMain(const std::function<void()>& fn) override {
		auto call = std::make_shared<Call>();
		call->fn = fn;
		auto* holder = new std::shared_ptr<Call>(call);
		g_main_context_invoke_full(nullptr, G_PRIORITY_DEFAULT, [](gpointer p) -> gboolean {
			Call& c = **static_cast<std::shared_ptr<Call>*>(p);
			{
				std::lock_guard<std::mutex> g(c.mu);
				if (c.cancelled) return G_SOURCE_REMOVE;
				c.running = true;
			}
			guarded("sync", [&] { c.fn(); });
			std::lock_guard<std::mutex> g(c.mu);
			c.running = false;
			c.done = true;
			c.cv.notify_all();
			return G_SOURCE_REMOVE;
		}, holder, [](gpointer p) { delete static_cast<std::shared_ptr<Call>*>(p); });
		std::unique_lock<std::mutex> lock(call->mu);
		while (!call->done) {
			call->cv.wait_for(lock, std::chrono::milliseconds(100));
			if (!call->done && !call->running && gShuttingDown.load()) {
				call->cancelled = true;
				return;
			}
		}
	}

	std::string loadSecret() override { return syncplatform::loadSecret(syncplatform::syncFolder()); }
	bool saveSecret(const std::string& code) override { return syncplatform::saveSecret(syncplatform::syncFolder(), code); }
	void forgetSecret() override { syncplatform::forgetSecret(syncplatform::syncFolder()); }
	bool tryLock(const std::string& path) override { return syncplatform::tryLock(path); }
	void unlock() override { syncplatform::unlock(); }

	// §4.12 step 1: every open library circuit with unsaved changes is saved now (its own autosave
	// waits a couple of seconds). Not one in the middle of a drag: the apply step holds those.
	void flushOpen(std::function<void()> done) override {
		for (CircuitWindow* w : std::vector<CircuitWindow*>(circuitWindows())) {
			library::Item it;
			if (w->filePath().empty() || !w->isDirty() || w->busyEditing() || !library::itemFor(w->filePath(), it)) continue;
			guarded("saving", [&] { w->saveQuietly(false); });
		}
		done();
	}

	clsync::WindowState windowState(const std::string& folderId) override {
		clsync::WindowState s;
		for (CircuitWindow* w : windowsFor(folderId)) {
			s.open = true;
			s.dirty = s.dirty || w->isDirty() || w->busyEditing();
			s.lastInputAt = std::max(s.lastInputAt, w->lastInputMs());
		}
		return s;
	}

	void circuitReplaced(const std::string& folderId, const std::string& fromDevice) override {
		const std::string message = fromDevice.empty() ? std::string("Updated from your other device")
		                                               : "Updated from " + fromDevice;
		for (CircuitWindow* w : windowsFor(folderId)) w->reloadForSync(message);
	}

	// The circuit went (deleted on another device): its windows close without saving. The last
	// window stays, empty, rather than ending the app.
	void closeCircuit(const std::string& folderId) override {
		for (CircuitWindow* w : windowsFor(folderId)) {
			if (circuitWindows().size() > 1) w->discard();
			else w->replaceDocument(cl_document_new(), "");
		}
	}

	void libraryChanged() override {
		rebuildRecentMenus();
		for (CircuitWindow* w : std::vector<CircuitWindow*>(circuitWindows())) w->libraryChanged();
		fire(true);
	}

	void statusChanged(const clsync::Status&) override { fire(false); }

	void notice(const std::string& text) override {
		gNotice = text;
		gNoticeAt = g_get_monotonic_time();
		if (CircuitWindow* w = frontWindow()) w->note(text);
		fire(false);
	}

	void askMassDelete(int count, std::function<void(bool)> answer) override { later(true, count, std::string(), std::move(answer)); }
	void askIncomingDeletes(int count, const std::string& fromDevices, std::function<void(bool)> answer) override {
		later(false, count, fromDevices, std::move(answer));
	}

private:
	// The question is put to the person a moment later, not inside the engine's call: it may take
	// as long as they like, and the engine waits for the answer on its own.
	void later(bool local, int count, const std::string& devices, std::function<void(bool)> answer) {
		auto* ask = new Ask{ local, count, devices, std::move(answer) };
		g_idle_add([](gpointer p) -> gboolean {
			std::unique_ptr<Ask> a(static_cast<Ask*>(p));
			CircuitWindow* w = frontWindow();
			GtkWindow* parent = w ? w->window() : nullptr;
			Alert alert;
			alert.badge = 2;
			if (a->local) {
				alert.heading = std::to_string(a->count) + " synced circuits aren't on this computer any more";
				alert.text = "Delete them from your other devices too, or bring them back here from the synced copy?";
				alert.buttons = { { "Bring Them Back", 1, 1 }, { "Delete Them Everywhere", 2, 2, true } };
				alert.escape = 1;
				alert.enter = 1;
			} else {
				const std::string who = a->devices.empty() ? std::string("Another device") : a->devices;
				alert.heading = who + " deleted " + std::to_string(a->count) + " of your synced circuits";
				alert.text = "Move them to Recently Deleted here too, or keep them? Kept circuits are sent back to your other devices.";
				alert.buttons = { { "Keep Them", 1, 1 }, { "Move Them to Recently Deleted", 2, 2, true } };
				alert.escape = 1;
				alert.enter = 1;
			}
			const int answer = runAlert(parent, alert);
			// Local: yes = delete everywhere. Incoming: yes = move to the trash. Keep and bring back are 1.
			a->answer(answer == 2);
			return G_SOURCE_REMOVE;
		}, ask);
	}
};

AppHost* gHost = nullptr;

// The app, or a window of it, in front or not: the engine polls while it is.
gboolean activeCb(gpointer) {
	bool active = false;
	GList* all = gtk_window_list_toplevels();
	for (GList* l = all; l; l = l->next)
		if (gtk_window_is_active(GTK_WINDOW(l->data))) active = true;
	g_list_free(all);
	if (active != gActive) {
		gActive = active;
		if (gEngine) {
			if (active) gEngine->appActivated();
			else gEngine->appDeactivated();
		}
	}
	return G_SOURCE_CONTINUE;
}

}  // namespace

void start(GtkApplication*) {
	if (gEngine) return;
	library::root();   // (makes the folder)
	clsync::Config c;
	c.libraryRoot = syncplatform::libraryRoot();
	c.syncDir = syncplatform::syncFolder();
	c.serverBase = syncplatform::serverBase();
	c.appKey = syncplatform::appKey();
	c.client = syncplatform::clientName();
	c.defaultDeviceName = syncplatform::defaultDeviceName();
	c.gateDefault = gateDefault;
	gHost = new AppHost();
	gEngine = new clsync::Engine(c, syncplatform::crypto(), *gHost);
	g_timeout_add(500, activeCb, nullptr);
}

void begin() {
	if (gEngine) gEngine->start();
}

void quit() {
	if (!gEngine) return;
	struct Wait {
		std::mutex mu;
		GMainLoop* loop = nullptr;
		bool finished = false;
	};
	auto wait = std::make_shared<Wait>();
	if (gEngine->enabled()) {
		wait->loop = g_main_loop_new(nullptr, FALSE);
		// On the engine's thread (or a timer's, past five seconds): the loop is told to stop.
		gEngine->quitting([wait] {
			std::lock_guard<std::mutex> g(wait->mu);
			wait->finished = true;
			if (wait->loop) g_main_loop_quit(wait->loop);
		});
		struct Timeout {
			GMainLoop* loop;
			bool fired;
		} timeoutState{ wait->loop, false };
		const guint timeout = g_timeout_add(5500, [](gpointer p) -> gboolean {
			Timeout* t = static_cast<Timeout*>(p);
			t->fired = true;
			g_main_loop_quit(t->loop);
			return G_SOURCE_REMOVE;
		}, &timeoutState);
		bool already;
		{
			std::lock_guard<std::mutex> g(wait->mu);
			already = wait->finished;
		}
		if (!already) g_main_loop_run(wait->loop);
		if (!timeoutState.fired) g_source_remove(timeout);   // (not after the loop is gone)
		std::lock_guard<std::mutex> g(wait->mu);
		g_main_loop_unref(wait->loop);
		wait->loop = nullptr;
	}
	gShuttingDown = true;
	delete gEngine;   // stops it; waits a moment for its thread
	gEngine = nullptr;
}

clsync::Engine* engine() { return gEngine; }
bool enabled() { return gEngine && gEngine->enabled(); }
clsync::Status status() { return gEngine ? gEngine->status() : clsync::Status(); }

std::string recentNotice() {
	if (gNotice.empty() || (g_get_monotonic_time() - gNoticeAt) / 1e6 > 60) return std::string();
	return gNotice;
}

void libraryChanged() {
	if (gEngine) gEngine->noteLibraryChanged();
}

void userInput() {
	static gint64 last = 0;
	const gint64 now = g_get_monotonic_time();
	if (now - last < 5000000) return;
	last = now;
	if (gEngine) gEngine->userActive();
}

void handleLink(const std::string& link) { syncui::linkFromUrl(link); }

guint addStatusListener(std::function<void()> fn) {
	gListeners.push_back({ gNextListener, false, std::move(fn) });
	return gNextListener++;
}

guint addLibraryListener(std::function<void()> fn) {
	gListeners.push_back({ gNextListener, true, std::move(fn) });
	return gNextListener++;
}

void removeListener(guint id) {
	gListeners.erase(std::remove_if(gListeners.begin(), gListeners.end(), [id](const Listener& l) { return l.id == id; }), gListeners.end());
}

std::string gateDefault(const std::string& lib, bool gui, const std::string& name) {
	char buf[512];
	if (!cl_sync_core_gate_default(nullptr, lib.c_str(), gui, name.c_str(), buf, sizeof buf)) return std::string("\x01");
	return buf;
}

}  // namespace syncapp
