// The sync engine's C interface (include/CedarSync.h) over clsync::Engine:
// the C hooks become Crypto and Host, the C++ callbacks become tokens.

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS   // snprintf/sscanf/localtime/getenv are used with care
#endif

#include "CedarSync.h"
#include "SyncInternal.h"

#include <cstdlib>
#include <cstring>

namespace {

using namespace clsync;

struct HookCrypto : Crypto {
	CLSyncHooks h;
	explicit HookCrypto(const CLSyncHooks& hooks) : h(hooks) {}
	bool random(uint8_t* out, size_t n) override { return h.random && h.random(h.ctx, out, n); }
	void sha256(const uint8_t* p, size_t n, uint8_t out[32]) override {
		if (h.sha256) h.sha256(h.ctx, p, n, out);
		else memset(out, 0, 32);
	}
	void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* p, size_t n, uint8_t out[32]) override {
		if (h.hmac_sha256) h.hmac_sha256(h.ctx, key, keyLen, p, n, out);
		else memset(out, 0, 32);
	}
	bool aesGcmSeal(const uint8_t key[32], const uint8_t nonce[12], const Bytes& aad, const Bytes& plain, Bytes& ctTag) override {
		if (!h.aes_gcm_seal) return false;
		ctTag.assign(plain.size() + 16, 0);
		static const uint8_t none = 0;
		return h.aes_gcm_seal(h.ctx, key, nonce, aad.empty() ? &none : aad.data(), aad.size(), plain.empty() ? &none : plain.data(),
		                      plain.size(), ctTag.data());
	}
	bool aesGcmOpen(const uint8_t key[32], const uint8_t nonce[12], const Bytes& aad, const Bytes& ctTag, Bytes& plain) override {
		if (!h.aes_gcm_open || ctTag.size() < 16) return false;
		plain.assign(ctTag.size() - 16 + 1, 0);   // +1: never a null pointer for an empty plaintext
		static const uint8_t none = 0;
		const bool ok = h.aes_gcm_open(h.ctx, key, nonce, aad.empty() ? &none : aad.data(), aad.size(), ctTag.data(), ctTag.size(),
		                               plain.data());
		plain.resize(ok ? ctTag.size() - 16 : 0);
		return ok;
	}
	bool deflateRaw(const Bytes& in, Bytes& out) override {
		if (!h.deflate_raw) return false;
		size_t len = 0;
		uint8_t* p = h.deflate_raw(h.ctx, in.data(), in.size(), &len);
		if (!p) return false;
		out.assign(p, p + len);
		free(p);
		return true;
	}
	bool inflateRaw(const Bytes& in, size_t maxOut, Bytes& out) override {
		if (!h.inflate_raw) return false;
		size_t len = 0;
		uint8_t* p = h.inflate_raw(h.ctx, in.data(), in.size(), maxOut, &len);
		if (!p) return false;
		const bool ok = len <= maxOut;
		if (ok) out.assign(p, p + len);
		free(p);
		return ok;
	}
};

void runFunction(void* arg) { (*static_cast<const std::function<void()>*>(arg))(); }

struct HookHost : Host {
	CLSyncHooks h;
	explicit HookHost(const CLSyncHooks& hooks) : h(hooks) {}

	HttpResponse http(const HttpRequest& r) override {
		HttpResponse out;
		if (!h.http) return out;
		std::string headers;
		for (const auto& kv : r.headers) headers += kv.first + ": " + kv.second + "\r\n";
		int status = 0;
		bool sent = false;
		char* respHeaders = nullptr;
		uint8_t* respBody = nullptr;
		size_t respLen = 0;
		h.http(h.ctx, r.method.c_str(), r.url.c_str(), headers.c_str(), (const uint8_t*)r.body.data(), r.body.size(), &status, &sent,
		       &respHeaders, &respBody, &respLen);
		out.status = status;
		out.sent = sent;
		if (respHeaders) {
			const std::string all = respHeaders;
			size_t at = 0;
			while (at < all.size()) {
				size_t end = all.find('\n', at);
				if (end == std::string::npos) end = all.size();
				std::string line = all.substr(at, end - at);
				if (!line.empty() && line.back() == '\r') line.pop_back();
				const size_t colon = line.find(':');
				if (colon != std::string::npos) {
					std::string name = line.substr(0, colon);
					for (char& c : name)
						if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
					std::string value = line.substr(colon + 1);
					while (!value.empty() && (value[0] == ' ' || value[0] == '\t')) value.erase(0, 1);
					out.headers[name] = value;
				}
				at = end + 1;
			}
			free(respHeaders);
		}
		if (respBody) {
			out.body.assign((const char*)respBody, respLen);
			free(respBody);
		}
		return out;
	}
	void onMain(const std::function<void()>& fn) override {
		if (h.on_main) h.on_main(h.ctx, runFunction, (void*)&fn);
		else fn();
	}
	std::string loadSecret() override {
		if (!h.load_secret) return std::string();
		char* s = h.load_secret(h.ctx);
		if (!s) return std::string();
		std::string out = s;
		free(s);
		return out;
	}
	bool saveSecret(const std::string& code) override { return h.save_secret && h.save_secret(h.ctx, code.c_str()); }
	void forgetSecret() override {
		if (h.forget_secret) h.forget_secret(h.ctx);
	}
	bool tryLock(const std::string& path) override { return !h.try_lock || h.try_lock(h.ctx, path.c_str()); }
	void unlock() override {
		if (h.unlock) h.unlock(h.ctx);
	}
	void flushOpen(std::function<void()> done) override {
		if (!h.flush_open) {
			done();
			return;
		}
		h.flush_open(h.ctx, new std::function<void()>(std::move(done)));
	}
	WindowState windowState(const std::string& folderId) override {
		WindowState w;
		if (h.window_state) h.window_state(h.ctx, folderId.c_str(), &w.open, &w.dirty, &w.lastInputAt);
		return w;
	}
	void circuitReplaced(const std::string& folderId, const std::string& fromDevice) override {
		if (h.circuit_replaced) h.circuit_replaced(h.ctx, folderId.c_str(), fromDevice.c_str());
	}
	void closeCircuit(const std::string& folderId) override {
		if (h.close_circuit) h.close_circuit(h.ctx, folderId.c_str());
	}
	void libraryChanged() override {
		if (h.library_changed) h.library_changed(h.ctx);
	}
	void statusChanged(const Status&) override {
		if (h.status_changed) h.status_changed(h.ctx);
	}
	void notice(const std::string& text) override {
		if (h.notice) h.notice(h.ctx, text.c_str());
	}
	void sideChanged() override {
		if (h.side_changed) h.side_changed(h.ctx);
	}
	void askMassDelete(int count, std::function<void(bool)> answer) override {
		if (!h.ask_mass_delete) {
			answer(false);   // nobody to ask: keep them (Bring Them Back)
			return;
		}
		h.ask_mass_delete(h.ctx, count, new std::function<void(bool)>(std::move(answer)));
	}
	void askIncomingDeletes(int count, const std::string& fromDevices, std::function<void(bool)> answer) override {
		if (!h.ask_incoming_deletes) {
			answer(false);   // nobody to ask: keep them
			return;
		}
		h.ask_incoming_deletes(h.ctx, count, fromDevices.c_str(), new std::function<void(bool)>(std::move(answer)));
	}
};

std::string orEmpty(const char* s) { return s ? std::string(s) : std::string(); }

// An override of the server for testing (§3.2): https, or http only for this computer.
std::string serverFromEnvironment() {
	const char* u = getenv("CL_SYNC_URL");
	const std::string url = u ? u : "";
	if (url.compare(0, 8, "https://") == 0) return url;
	// http only for this computer: "localhost" or "127.0.0.1", then a port (digits) or the path --
	// not "http://localhost:80@elsewhere/", whose host is elsewhere.
	for (const char* host : { "http://localhost", "http://127.0.0.1" }) {
		const size_t n = strlen(host);
		if (url.compare(0, n, host) != 0) continue;
		size_t i = n;
		if (i < url.size() && url[i] == ':') {
			i++;
			const size_t digits = i;
			while (i < url.size() && url[i] >= '0' && url[i] <= '9') i++;
			if (i == digits) return std::string();
		}
		if (i == url.size() || url[i] == '/') return url;
	}
	return std::string();
}

}  // namespace

struct CLSyncEngine {
	HookCrypto crypto;
	HookHost host;
	std::unique_ptr<Engine> engine;
	std::string code, deviceName, statusText, device, sideRid;
	Status snapshot;
	std::vector<std::pair<std::string, int64_t>> devices;
	CLSyncEngine(const CLSyncHooks& h) : crypto(h), host(h) {}
};

extern "C" {

CLSyncEngine* cl_sync_create(const CLSyncHooks* hooks, const char* libraryRoot, const char* syncDir, const char* appKey,
                             const char* client, const char* defaultDeviceName) {
	if (!hooks) return nullptr;
	CLSyncEngine* e = new CLSyncEngine(*hooks);
	Config c;
	c.libraryRoot = orEmpty(libraryRoot);
	c.syncDir = orEmpty(syncDir);
	c.appKey = orEmpty(appKey);
	c.client = orEmpty(client);
	c.defaultDeviceName = orEmpty(defaultDeviceName);
	c.sideKinds = { "classroom", "membership" };   // the classroom's (CLASSROOM.md 3.16.7)
	const std::string server = serverFromEnvironment();
	if (!server.empty()) c.serverBase = server;
	const CLSyncHooks h = *hooks;
	if (h.gate_default) {
		c.gateDefault = [h](const std::string& lib, bool gui, const std::string& name) -> std::string {
			char buf[4096];
			buf[0] = 0;
			if (!h.gate_default(h.ctx, lib.c_str(), gui, name.c_str(), buf, sizeof buf)) return std::string("\x01");
			buf[sizeof buf - 1] = 0;
			return std::string(buf);
		};
	}
	e->engine.reset(new Engine(c, e->crypto, e->host));
	return e;
}

void cl_sync_destroy(CLSyncEngine* e) {
	if (!e) return;
	const int before = detachedEngines();
	e->engine.reset();   // stops and joins the thread (up to 3 s)
	// A thread that was still inside a hook can't be stopped: it keeps using the hook objects
	// (and the app's ctx), so they are left in place rather than freed under it.
	if (detachedEngines() != before) return;
	delete e;
}

void cl_sync_start(CLSyncEngine* e) {
	if (e) e->engine->start();
}

bool cl_sync_enabled(CLSyncEngine* e) { return e && e->engine->enabled(); }

const char* cl_sync_code(CLSyncEngine* e) {
	if (!e) return "";
	e->code = e->engine->code();
	return e->code.c_str();
}

const char* cl_sync_device_name(CLSyncEngine* e) {
	if (!e) return "";
	e->deviceName = e->engine->deviceName();
	return e->deviceName.c_str();
}

void cl_sync_set_device_name(CLSyncEngine* e, const char* name) {
	if (e && name) e->engine->setDeviceName(name);
}

int cl_sync_device_count(CLSyncEngine* e) {
	if (!e) return 0;
	e->devices = e->engine->devices();
	return (int)e->devices.size();
}

const char* cl_sync_device(CLSyncEngine* e, int i, int64_t* lastSyncAt) {
	if (!e || i < 0 || i >= (int)e->devices.size()) return "";
	if (lastSyncAt) *lastSyncAt = e->devices[(size_t)i].second;
	e->device = e->devices[(size_t)i].first;
	return e->device.c_str();
}

int cl_sync_status_kind(CLSyncEngine* e) {
	if (!e) return CL_SYNC_OFF;
	e->snapshot = e->engine->status();
	return (int)e->snapshot.kind;
}

const char* cl_sync_status_text(CLSyncEngine* e) {
	if (!e) return "";
	e->snapshot = e->engine->status();
	e->statusText = e->snapshot.text;
	return e->statusText.c_str();
}

int64_t cl_sync_last_sync(CLSyncEngine* e) { return e ? e->engine->status().lastSyncAt : 0; }

int cl_sync_circuit_count(CLSyncEngine* e) { return e ? e->engine->status().circuits : 0; }

int cl_sync_problem_count(CLSyncEngine* e) {
	if (!e) return 0;
	e->snapshot = e->engine->status();
	return (int)e->snapshot.problems.size();
}

const char* cl_sync_problem(CLSyncEngine* e, int i, const char** folderId) {
	if (!e || i < 0 || i >= (int)e->snapshot.problems.size()) {
		if (folderId) *folderId = "";
		return "";
	}
	if (folderId) *folderId = e->snapshot.problems[(size_t)i].first.c_str();
	return e->snapshot.problems[(size_t)i].second.c_str();
}

void cl_sync_turn_on(CLSyncEngine* e, CLSyncDone done, void* ctx) {
	if (!e) return;
	e->engine->turnOn([done, ctx](bool ok, std::string message) {
		if (done) done(ctx, ok, message.c_str());
	});
}

void cl_sync_preview(CLSyncEngine* e, const char* code, CLSyncPreviewDone done, void* ctx) {
	if (!e) return;
	e->engine->preview(orEmpty(code), [done, ctx](bool ok, std::string message, Preview pv) {
		std::string devices;
		for (const std::string& d : pv.devices) devices += (devices.empty() ? "" : "\n") + d;
		if (done) done(ctx, ok, message.c_str(), pv.circuits, devices.c_str());
	});
}

void cl_sync_link(CLSyncEngine* e, const char* code, CLSyncDone done, void* ctx) {
	if (!e) return;
	e->engine->link(orEmpty(code), [done, ctx](bool ok, std::string message) {
		if (done) done(ctx, ok, message.c_str());
	});
}

void cl_sync_turn_off(CLSyncEngine* e, bool removeSyncedCircuits) {
	if (e) e->engine->turnOff(removeSyncedCircuits);
}

void cl_sync_delete_synced_copy(CLSyncEngine* e, CLSyncDone done, void* ctx) {
	if (!e) return;
	e->engine->deleteSyncedCopy([done, ctx](bool ok, std::string message) {
		if (done) done(ctx, ok, message.c_str());
	});
}

void cl_sync_start_over(CLSyncEngine* e, CLSyncDone done, void* ctx) {
	if (!e) return;
	e->engine->startOver([done, ctx](bool ok, std::string message) {
		if (done) done(ctx, ok, message.c_str());
	});
}

void cl_sync_answer(void* token, bool yes) {
	auto* fn = static_cast<std::function<void(bool)>*>(token);
	if (!fn) return;
	(*fn)(yes);
	delete fn;
}

void cl_sync_flush_done(void* token) {
	auto* fn = static_cast<std::function<void()>*>(token);
	if (!fn) return;
	(*fn)();
	delete fn;
}

char* cl_sync_side_records(CLSyncEngine* e, const char* kind) {
	json::Value out = json::Value::array();
	if (e)
		for (const auto& r : e->engine->sideRecords(orEmpty(kind))) {
			json::Value pair = json::Value::array();
			pair.push(json::Value::string(r.first));
			pair.push(json::Value::string(r.second));
			out.push(pair);
		}
	const std::string text = json::write(out);
	char* p = (char*)malloc(text.size() + 1);
	if (p) memcpy(p, text.c_str(), text.size() + 1);
	return p;
}
const char* cl_sync_put_side(CLSyncEngine* e, const char* payloadJson, const char* rid) {
	if (!e) return "";
	json::Value v;
	const std::string text = orEmpty(payloadJson);
	e->sideRid = json::parse(text, v) && v.isObject() ? e->engine->putSideRecord(v.str("kind"), text, orEmpty(rid)) : std::string();
	return e->sideRid.c_str();
}
void cl_sync_delete_side(CLSyncEngine* e, const char* rid) {
	if (e) e->engine->deleteSideRecord(orEmpty(rid));
}

void cl_sync_now(CLSyncEngine* e) {
	if (e) e->engine->syncNow();
}
void cl_sync_note_library_changed(CLSyncEngine* e) {
	if (e) e->engine->noteLibraryChanged();
}
void cl_sync_app_activated(CLSyncEngine* e) {
	if (e) e->engine->appActivated();
}
void cl_sync_app_deactivated(CLSyncEngine* e) {
	if (e) e->engine->appDeactivated();
}
void cl_sync_user_active(CLSyncEngine* e) {
	if (e) e->engine->userActive();
}

void cl_sync_quitting(CLSyncEngine* e, CLSyncQuitDone done, void* ctx) {
	if (!e) {
		if (done) done(ctx);
		return;
	}
	e->engine->quitting([done, ctx] {
		if (done) done(ctx);
	});
}

void cl_sync_pair_start(CLSyncEngine* e, CLSyncPairShow show, CLSyncPairDone done, void* ctx) {
	if (!e) return;
	static_assert((int)CL_SYNC_PAIR_CODE == (int)Engine::PairCode && (int)CL_SYNC_PAIR_EXPIRED == (int)Engine::PairExpired &&
	                  (int)CL_SYNC_PAIR_FAILED == (int)Engine::PairFailed,
	              "pair results");
	e->engine->pairStart(
		[show, ctx](const std::string& link) {
			if (show) show(ctx, link.c_str());
		},
		[done, ctx](int result, const std::string& text, const std::string& from) {
			if (done) done(ctx, result, text.c_str(), from.c_str());
		});
}

void cl_sync_pair_cancel(CLSyncEngine* e) {
	if (e) e->engine->pairCancel();
}

bool cl_sync_parse_code(const CLSyncHooks* hooks, const char* text, char code[29], char why[16]) {
	if (code) code[0] = 0;
	if (why) why[0] = 0;
	if (!hooks || !text) {
		if (why) strcpy(why, "length");
		return false;
	}
	HookCrypto crypto(*hooks);
	std::string c, w;
	if (!parseCode(crypto, text, c, w)) {
		if (why) {
			strncpy(why, w.c_str(), 15);
			why[15] = 0;
		}
		return false;
	}
	if (code) {
		memcpy(code, c.c_str(), 28);
		code[28] = 0;
	}
	return true;
}

const char* cl_sync_why_text(const char* why, const char* text) {
	static thread_local std::string out;
	out = whyText(orEmpty(why), orEmpty(text));
	return out.c_str();
}

void cl_sync_group_code(const char* code, char out[35]) {
	if (!out) return;
	const std::string g = groupCode(orEmpty(code).substr(0, 28));
	strncpy(out, g.c_str(), 34);
	out[34] = 0;
}

const char* cl_sync_web_link(const char* code) {
	static thread_local std::string out;
	out = webLink(orEmpty(code));
	return out.c_str();
}

const char* cl_sync_app_link(const char* code) {
	static thread_local std::string out;
	out = appLink(orEmpty(code));
	return out.c_str();
}

int cl_sync_qr(const char* text, uint8_t* out) {
	int size = 0;
	const std::vector<bool> m = qr(orEmpty(text), size);
	if (!out || size <= 0) return 0;
	for (size_t i = 0; i < m.size(); i++) out[i] = m[i] ? 1 : 0;
	return size;
}

bool cl_sync_self_test(const CLSyncHooks* hooks, const char* tempDir, const char* serverBase, char** report) {
	if (report) *report = nullptr;
	if (!hooks) return false;
	HookCrypto crypto(*hooks);
	HookHost host(*hooks);
	std::string text;
	const std::string base = orEmpty(serverBase);
	const bool ok = selfTest(crypto, orEmpty(tempDir), text, base.empty() || !hooks->http ? nullptr : &host, base);
	if (report) {
		*report = (char*)malloc(text.size() + 1);
		if (*report) memcpy(*report, text.c_str(), text.size() + 1);
	}
	return ok;
}

}  // extern "C"
