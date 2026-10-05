// The sync algorithm (SYNC.md 4): one device's cycle -- pull, apply, merge
// and conflicts, deletes, push, the device record -- and its state file. A
// port of the design's ref/sim.py Client, which wins any disagreement. The
// threads, triggers and backoff around it are in SyncEngine.cpp.

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS   // snprintf/sscanf/localtime/getenv are used with care
#endif

#include "SyncInternal.h"

#include <algorithm>
#include <cstdio>

namespace clsync {

// ---- the state file (§2.5) -----------------------------------------------------------------

namespace {

json::Value sentJson(const Sent& s) {
	if (!s.has) return json::Value();
	json::Value o = json::Value::object();
	o.set("ver", json::Value::integer(s.ver));
	if (s.deleted) {
		o.set("deleted", json::Value::boolean(true));
	} else {
		o.set("n", json::Value::string(s.n));
		o.set("c", json::Value::string(s.c));
		o.set("st", json::Value::string(s.st));
		if (!s.ch.empty()) o.set("ch", json::Value::string(s.ch));
	}
	return o;
}

Sent sentFrom(const json::Value* v) {
	Sent s;
	if (!v || !v->isObject()) return s;
	s.has = true;
	s.ver = v->integer("ver");
	s.deleted = v->flag("deleted");
	s.n = v->str("n");
	s.c = v->str("c");
	s.st = v->str("st");
	s.ch = v->str("ch");
	return s;
}

json::Value strings(const std::vector<std::string>& v) {
	json::Value a = json::Value::array();
	for (const std::string& s : v) a.push(json::Value::string(s));
	return a;
}

std::vector<std::string> stringsFrom(const json::Value* v) {
	std::vector<std::string> out;
	if (v && v->isArray())
		for (const json::Value& x : v->a)
			if (x.isString()) out.push_back(x.s);
	return out;
}

json::Value intOrNull(int64_t v) { return v ? json::Value::integer(v) : json::Value(); }

}  // namespace

json::Value State::toJson() const {
	json::Value o = json::Value::object();
	o.set("v", json::Value::integer(2));
	o.set("spaceId", json::Value::string(spaceId));
	o.set("epoch", json::Value::string(epoch));
	o.set("libraryId", json::Value::string(libraryId));
	o.set("libraryGen", json::Value::integer(libraryGen));
	o.set("deviceId", json::Value::string(deviceId));
	o.set("deviceName", json::Value::string(deviceName));
	o.set("offset", json::Value::integer(offset));
	o.set("cursor", json::Value::integer(cursor));
	o.set("purgedSeq", json::Value::integer(purgedSeq));
	o.set("lastSyncAt", json::Value::integer(lastSyncAt));
	o.set("joining", json::Value::boolean(joining));
	if (applying) o.set("applying", json::Value::boolean(true));
	if (!gone.empty()) o.set("gone", json::Value::string(gone));
	json::Value recs = json::Value::object();
	for (const auto& kv : records) {
		const RecState& r = kv.second;
		json::Value e = json::Value::object();
		e.set("folder", json::Value::string(r.local));
		e.set("ver", json::Value::integer(r.ver));
		e.set("n", json::Value::string(r.n));
		e.set("c", json::Value::string(r.c));
		e.set("st", json::Value::string(r.st));
		e.set("sent", sentJson(r.sent));
		recs.set(kv.first, std::move(e));
	}
	o.set("records", std::move(recs));
	json::Value sn = json::Value::object();
	for (const auto& kv : seen) {
		json::Value a = json::Value::array();
		a.push(json::Value::integer(kv.second.ver));
		a.push(json::Value::string(kv.second.h));
		a.push(kv.second.local.empty() ? json::Value() : json::Value::string(kv.second.local));
		sn.set(kv.first, std::move(a));
	}
	o.set("seen", std::move(sn));
	o.set("force", strings(force));
	o.set("refetch", strings(refetch));
	if (device.has) {
		json::Value d = json::Value::object();
		d.set("id", json::Value::string(device.id));
		d.set("ver", json::Value::integer(device.ver));
		d.set("at", json::Value::integer(device.at));
		d.set("name", json::Value::string(device.name));
		o.set("device", std::move(d));
	} else {
		o.set("device", json::Value());
	}
	o.set("lazySince", intOrNull(lazySince));
	o.set("hashCache", hashCache);
	json::Value tb = json::Value::object();
	for (const auto& kv : tooBig) tb.set(kv.first, json::Value::string(kv.second));
	o.set("tooBig", std::move(tb));
	json::Value un = json::Value::object();
	for (const auto& kv : unreadable) {
		json::Value a = json::Value::array();
		a.push(json::Value::integer(kv.second.first));
		a.push(json::Value::string(kv.second.second));
		un.set(kv.first, std::move(a));
	}
	o.set("unreadable", std::move(un));
	// Beyond §2.5 (readers ignore what they don't know): a re-join's hints and the device list.
	json::Value hi = json::Value::object();
	for (const auto& kv : hints) hi.set(kv.first, json::Value::string(kv.second));
	o.set("hints", std::move(hi));
	json::Value dv = json::Value::object();
	for (const auto& kv : devices) {
		json::Value a = json::Value::array();
		a.push(json::Value::string(kv.second.first));
		a.push(json::Value::integer(kv.second.second));
		dv.set(kv.first, std::move(a));
	}
	o.set("devices", std::move(dv));
	return o;
}

bool State::fromJson(const json::Value& v, State& s) {
	s = State();
	if (!v.isObject() || v.integer("v") != 2) return false;
	s.spaceId = v.str("spaceId");
	s.epoch = v.str("epoch");
	s.libraryId = v.str("libraryId");
	s.libraryGen = v.integer("libraryGen");
	s.deviceId = v.str("deviceId");
	s.deviceName = v.str("deviceName");
	s.offset = v.integer("offset");
	s.cursor = v.integer("cursor");
	s.purgedSeq = v.integer("purgedSeq");
	s.lastSyncAt = v.integer("lastSyncAt");
	s.joining = v.flag("joining");
	s.applying = v.flag("applying");
	s.gone = v.str("gone");
	if (const json::Value* r = v.get("records"))
		if (r->isObject())
			for (const auto& kv : r->o) {
				if (!isUuid(kv.first) || !kv.second.isObject()) continue;
				RecState rs;
				rs.local = kv.second.str("folder");
				rs.ver = kv.second.integer("ver");
				rs.n = kv.second.str("n");
				rs.c = kv.second.str("c");
				rs.st = kv.second.str("st");
				rs.sent = sentFrom(kv.second.get("sent"));
				s.records[kv.first] = rs;
			}
	if (const json::Value* r = v.get("seen"))
		if (r->isObject())
			for (const auto& kv : r->o) {
				if (!isUuid(kv.first) || !kv.second.isArray() || kv.second.a.size() < 2) continue;
				SeenRec sr;
				sr.ver = kv.second.a[0].i();
				sr.h = kv.second.a[1].s;
				if (kv.second.a.size() > 2 && kv.second.a[2].isString()) sr.local = kv.second.a[2].s;
				s.seen[kv.first] = sr;
			}
	s.force = stringsFrom(v.get("force"));
	s.refetch = stringsFrom(v.get("refetch"));
	if (const json::Value* d = v.get("device"))
		if (d->isObject() && isUuid(d->str("id"))) {
			s.device.has = true;
			s.device.id = d->str("id");
			s.device.ver = d->integer("ver");
			s.device.at = d->integer("at");
			s.device.name = d->str("name");
		}
	s.lazySince = v.integer("lazySince");
	if (const json::Value* h = v.get("hashCache"))
		if (h->isObject()) s.hashCache = *h;
	if (const json::Value* r = v.get("tooBig"))
		if (r->isObject())
			for (const auto& kv : r->o)
				if (kv.second.isString()) s.tooBig[kv.first] = kv.second.s;
	if (const json::Value* r = v.get("unreadable"))
		if (r->isObject())
			for (const auto& kv : r->o)
				if (kv.second.isArray() && kv.second.a.size() >= 2)
					s.unreadable[kv.first] = { kv.second.a[0].i(), kv.second.a[1].s };
	if (const json::Value* r = v.get("hints"))
		if (r->isObject())
			for (const auto& kv : r->o)
				if (kv.second.isString()) s.hints[kv.first] = kv.second.s;
	if (const json::Value* r = v.get("devices"))
		if (r->isObject())
			for (const auto& kv : r->o)
				if (kv.second.isArray() && kv.second.a.size() >= 2)
					s.devices[kv.first] = { kv.second.a[0].s, kv.second.a[1].i() };
	return true;
}

// ---- words (§5) ----------------------------------------------------------------------------

std::string agoText(int64_t ms, int64_t now) {
	const int64_t s = (now - ms) / 1000;
	if (s < 60) return "just now";
	if (s < 3600) return std::to_string(s / 60) + " min ago";
	if (s < 86400) return s / 3600 == 1 ? "an hour ago" : std::to_string(s / 3600) + " hours ago";
	if (s < 2 * 86400) return "yesterday";
	if (s < 60 * 86400) return std::to_string(s / 86400) + " days ago";
	if (s < 730 * 86400) return std::to_string(s / (30 * 86400)) + " months ago";
	return std::to_string(s / (365 * 86400)) + " years ago";
}

namespace {

const char* const kQ1 = "\xE2\x80\x9C";   // “
const char* const kQ2 = "\xE2\x80\x9D";   // ”

std::string q(const std::string& s) { return kQ1 + s + kQ2; }

std::string orDevice(const std::string& d) { return d.empty() ? "another device" : d; }

std::string listWords(const std::vector<std::string>& items) {
	std::string out;
	for (size_t i = 0; i < items.size(); i++) {
		if (i) out += i + 1 == items.size() ? " and " : ", ";
		out += items[i];
	}
	return out;
}

std::string defaultMessage(int status, const std::string& code) {
	if (code == "bad_request") return "That request wasn't understood.";
	if (code == "wrong_code") return "This code doesn't match the synced circuits.";
	if (code == "forbidden") return "Not from CedarLogic.";
	if (code == "wrong_delete_token") return "That code can't delete the synced copy.";
	if (code == "no_space") return "No circuits are synced with this code.";
	if (code == "not_found") return "Not here.";
	if (code == "method_not_allowed") return "That can't be done here.";
	if (code == "space_deleted") return "The synced copy was deleted from another device.";
	if (code == "space_expired") return "The synced copy was removed after a year without use.";
	if (code == "too_large") return "That's too much to send at once.";
	if (code == "rate_limited") return "Lots of syncing just now. Trying again in a minute.";
	if (code == "busy") return "Sync is busy. Trying again shortly.";
	if (code == "sync_busy") return "Sync can't take new devices today. Try again tomorrow.";
	if (code == "sync_paused") return "Sync is resting on the website for today. Your circuits are safe on this device.";
	if (status >= 500) return "Something went wrong on the website.";
	return "The website answered " + std::to_string(status) + ".";
}

const char* const kFullSentence =
	"Your synced circuits are full (1,000 circuits or 10 MB). New circuits stay on this device until you delete some.";
const char* const kTooBig = "Too big to sync (over 512 KB)";
const char* const kNewer = "Saved by a newer CedarLogic: changes here stay on this device";

}  // namespace

std::string statusSentence(const std::string& status, const std::string& detail, int64_t lastSyncAt, int64_t now) {
	if (status == "synced") return lastSyncAt ? "Synced " + agoText(lastSyncAt, now) : "Synced";
	if (status == "syncing") return detail.empty() ? "Syncing\xE2\x80\xA6" : detail;
	if (status == "offline") return "Offline. Changes will sync when you're back online.";
	if (status == "full") return detail.empty() ? kFullSentence : detail;
	if (status == "gone") {
		if (detail == "space_expired")
			return "The synced copy was removed after a year without use. Your circuits here are kept. Turn on sync again to "
			       "make a new code.";
		return "Sync was turned off from another device, and the synced copy was deleted. Your circuits here are kept. If "
		       "you started over with a new code, link this device again with it.";
	}
	if (status == "busy") return detail.empty() ? "Lots of syncing just now. Trying again in a minute." : detail;
	if (status == "locked") return "Syncing in another CedarLogic window.";
	if (status == "error") {
		std::string d = detail;
		while (!d.empty() && (d.back() == '.' || d.back() == ' ')) d.pop_back();
		return "Couldn't sync: " + d + ".";
	}
	return std::string();
}

// ---- the algorithm -------------------------------------------------------------------------------

Core::Core(CoreOptions o, Crypto& c, Clock& k, Backend& b, CoreHooks h)
	: opt(std::move(o)), crypto(c), clock_(k), lib(b), hooks(std::move(h)) {
	reset();
}

void Core::reset() {
	State fresh;
	fresh.deviceId = isHex(st.deviceId, 32) ? st.deviceId : randomHex(crypto, 16);
	fresh.deviceName = st.deviceName;
	st = fresh;
	problems_.clear();
	badRequest_.clear();
}

void Core::setCode(const std::string& canonical) {
	code_ = canonical;
	keys_ = keysForCode(crypto, canonical);
	on = keys_.valid();
	if (on) st.spaceId = keys_.spaceId;
}

void Core::disable() {
	on = false;
	code_.clear();
	keys_ = Keys();
	status_ = "off";
}

std::string Core::structureHash(const std::string& cdl) {
	const std::string key = cdlHash(crypto, cdl);
	auto it = structureCache_.find(key);
	if (it != structureCache_.end()) return it->second;
	GateDefaults defaults = [this](const std::string& libName, bool gui, const std::string& name) -> std::string {
		const std::string k = libName + "\x01" + (gui ? "g" : "l") + "\x01" + name;
		auto d = defaultsCache_.find(k);
		if (d != defaultsCache_.end()) return d->second;
		const std::string v = opt.gateDefaults ? opt.gateDefaults(libName, gui, name) : std::string("\x01");
		defaultsCache_[k] = v;
		return v;
	};
	const std::string h = sha256Hex(crypto, structureText(cdl, defaults));
	if (structureCache_.size() > 4000) structureCache_.clear();
	structureCache_[key] = h;
	return h;
}

void Core::main(const std::function<void()>& fn) {
	if (quitting_ || !hooks.onMain) fn();
	else hooks.onMain(fn);
}

void Core::note(const std::string& text) {
	if (hooks.notice) hooks.notice(text);
}

void Core::save() {
	st.hashCache = lib.cacheJson();
	if (hooks.saveState && !hooks.saveState(st)) saveFailed_ = true;
}

// ---- HTTP (§3.2-3.4) -------------------------------------------------------------------------------

int Core::rawApi(const Keys& k, const std::string& method, const std::string& path, const json::Value* body, json::Value& out,
                 const std::vector<std::pair<std::string, std::string>>& extra) {
	HttpRequest r;
	r.method = method;
	r.url = opt.serverBase + path;
	if (path != "/health") r.headers.emplace_back("authorization", "Bearer " + k.authToken);
	if (!opt.client.empty()) r.headers.emplace_back("x-cedarlogic-client", opt.client);
	if (!opt.appKey.empty()) r.headers.emplace_back("x-cedarlogic-key", opt.appKey);
	for (const auto& h : extra) r.headers.push_back(h);
	if (body) {
		r.headers.emplace_back("content-type", "application/json");
		r.body = json::write(*body);
	}
	requests++;
	if (!hooks.http) throw NetError("no network");
	const HttpResponse resp = hooks.http(r);
	if (resp.status == 0) throw NetError(resp.sent ? "the answer was lost" : "offline");
	lastRetryAfter_ = 0;
	for (const auto& h : resp.headers) {
		std::string name = h.first;
		for (char& c : name)
			if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
		int64_t ms = 0;
		if (name == "date" && parseHttpDate(h.second, ms)) st.offset = ms - now();
		if (name == "retry-after") {
			const long long sec = atoll(h.second.c_str());
			if (sec > 0) lastRetryAfter_ = sec * 1000;
		}
	}
	out = json::Value();
	if (resp.body.empty() || !json::parse(resp.body, out) || !out.isObject()) out = json::Value::object();
	if (!lastRetryAfter_ && out.get("retryAfter") && out.get("retryAfter")->isInt()) lastRetryAfter_ = out.integer("retryAfter") * 1000;
	return resp.status;
}

int Core::api(const std::string& method, const std::string& path, const json::Value* body, json::Value& out,
              const std::vector<std::pair<std::string, std::string>>& extra) {
	const std::string full = "/spaces/" + keys_.spaceId + path;
	int status = rawApi(keys_, method, full, body, out, extra);
	if (status == 404 && out.str("error") == "no_space" && on) {
		// The space is gone without a marker (never used for 7 days, or lost): make it
		// again with the same code; the new epoch makes every device send again (§4.11).
		json::Value b = json::Value::object(), o;
		b.set("deleteHash", json::Value::string(keys_.deleteHash));
		const int put = rawApi(keys_, "PUT", "/spaces/" + keys_.spaceId, &b, o);
		if (put == 410) throw SpaceGone(o.str("error", "space_deleted"));
		if (put != 200 && put != 201) throwFor(put, o);
		status = rawApi(keys_, method, full, body, out, extra);
	}
	if (status >= 200 && status < 300) return status;
	throwFor(status, out);
	return status;
}

void Core::throwFor(int status, const json::Value& out) {
	const std::string code = out.str("error");
	const std::string message = out.str("message", defaultMessage(status, code));
	if (status == 410) throw SpaceGone(code == "space_expired" ? "space_expired" : "space_deleted");
	if (status == 429 || status == 503) {
		int64_t wait = lastRetryAfter_;
		if (status == 429) wait = std::max<int64_t>(wait, 30 * kSecond);
		throw BusyError(status, wait, code, message);
	}
	throw HttpError(status, code, message);
}

// ---- turning on, the preview, linking (§4.8) --------------------------------------------------------

bool Core::turnOn(std::string& message) {
	for (int attempt = 0; attempt < 3; attempt++) {
		const std::string code = newCode(crypto);
		if (code.empty()) {
			message = "Couldn't make a sync code: this computer's random number generator didn't answer.";
			return false;
		}
		const Keys k = keysForCode(crypto, code);
		json::Value body = json::Value::object(), out;
		body.set("deleteHash", json::Value::string(k.deleteHash));
		int status;
		try {
			status = rawApi(k, "PUT", "/spaces/" + k.spaceId, &body, out);
		} catch (const NetError&) {
			message = "Couldn't reach CedarLogic's website. Check the connection and try again.";
			return false;
		}
		if (status == 200) continue;   // practically impossible: someone has this code; make another
		if (status != 201) {
			message = out.str("message", defaultMessage(status, out.str("error")));
			return false;
		}
		reset();
		setCode(code);
		st.joining = true;
		st.epoch = out.str("epoch");
		status_ = "synced";
		save();
		return true;
	}
	message = "Couldn't make a new sync code. Try again.";
	return false;
}

int Core::preview(const std::string& codeText, Preview& pv, std::string& message) {
	pv = Preview();
	std::string code, why;
	if (!parseCode(crypto, codeText, code, why)) {
		message = whyText(why, codeText);
		return 400;
	}
	const Keys k = keysForCode(crypto, code);
	json::Value out;
	try {
		const int status = rawApi(k, "GET", "/spaces/" + k.spaceId, nullptr, out);
		if (status == 404) {
			message = "No circuits are synced with this code. Check the code, or turn on sync on your other device first.";
			return status;
		}
		if (status == 410) {
			message = out.str("error") == "space_expired" ? "That synced copy was removed after a year without use."
			                                               : "That synced copy was deleted.";
			return status;
		}
		if (status == 401) {
			message = "This code doesn't match.";
			return status;
		}
		if (status != 200) {
			message = out.str("message", defaultMessage(status, out.str("error")));
			return status;
		}
		// Every entry, then every live record, decrypted in memory only.
		std::vector<std::string> ids;
		int64_t since = 0;
		for (int pages = 0; pages < 1000; pages++) {
			json::Value page;
			const int s = rawApi(k, "GET", "/spaces/" + k.spaceId + "/changes?since=" + std::to_string(since), nullptr, page);
			if (s != 200) {
				message = page.str("message", defaultMessage(s, page.str("error")));
				return s;
			}
			if (const json::Value* es = page.get("entries"))
				for (const json::Value& e : es->a)
					if (!e.flag("deleted") && isUuid(e.str("id"))) ids.push_back(e.str("id"));
			if (!page.flag("more")) break;
			since = page.integer("next");
		}
		std::vector<std::string> deviceNames, circuitDevices, names;
		std::vector<std::string> queue = ids;
		for (int rounds = 0; !queue.empty() && rounds < 1000; rounds++) {
			const size_t n = std::min(queue.size(), kFetchIds);
			json::Value body = json::Value::object(), list = json::Value::array(), fb;
			for (size_t i = 0; i < n; i++) list.push(json::Value::string(queue[i]));
			body.set("ids", list);
			const int s = rawApi(k, "POST", "/spaces/" + k.spaceId + "/fetch", &body, fb);
			if (s != 200) {
				message = fb.str("message", defaultMessage(s, fb.str("error")));
				return s;
			}
			size_t gotHere = 0;
			if (const json::Value* rs = fb.get("records"))
				for (const json::Value& r : rs->a) {
					gotHere++;
					Bytes env;
					std::string payload;
					Payload p;
					if (!unb64u(r.str("data"), env) || !openRecord(crypto, k.recordKey, r.str("id"), r.integer("ver"), env, payload))
						continue;
					if (!readPayload(payload, p).empty()) continue;
					if (p.kind == "circuit") {
						pv.circuits++;
						names.push_back(p.name);
						pv.newestEdit = std::max(pv.newestEdit, p.modifiedAt);
						if (!p.device.empty()) circuitDevices.push_back(p.device);
					} else if (p.kind == "device" && !p.device.empty()) {
						deviceNames.push_back(p.device);
					}
				}
			std::vector<std::string> next = stringsFrom(fb.get("deferred"));
			if (gotHere == 0 && next.size() >= n) break;   // nothing moves on
			next.insert(next.end(), queue.begin() + (long)n, queue.end());
			queue.swap(next);
		}
		std::sort(deviceNames.begin(), deviceNames.end());
		deviceNames.erase(std::unique(deviceNames.begin(), deviceNames.end()), deviceNames.end());
		std::sort(circuitDevices.begin(), circuitDevices.end());
		circuitDevices.erase(std::unique(circuitDevices.begin(), circuitDevices.end()), circuitDevices.end());
		pv.devices = deviceNames;
		for (const std::string& d : circuitDevices)
			if (std::find(pv.devices.begin(), pv.devices.end(), d) == pv.devices.end()) pv.devices.push_back(d);
		std::sort(names.begin(), names.end());
		if (names.size() > 5) names.resize(5);
		pv.names = names;
		// The confirmation (§5.1).
		std::vector<std::string> localIds;
		lib.list(localIds);
		if (pv.circuits == 0) {
			pv.sentence = "This code has no circuits yet. Only link with a code you made yourself \xE2\x80\x94 if someone sent it "
			              "to you, your circuits would go to them.";
		} else {
			std::string s = "This code has " + std::to_string(pv.circuits) + (pv.circuits == 1 ? " circuit" : " circuits");
			if (!pv.devices.empty()) s += " from " + listWords(pv.devices);
			if (pv.newestEdit) s += ", last changed " + agoText(pv.newestEdit, now() + st.offset);
			std::vector<std::string> quoted;
			for (const std::string& n : pv.names) quoted.push_back(q(n));
			s += ": " + [&] {
				std::string o;
				for (size_t i = 0; i < quoted.size(); i++) o += (i ? ", " : "") + quoted[i];
				return o;
			}();
			if ((int)pv.names.size() < pv.circuits) s += ", \xE2\x80\xA6";
			s += ".";
			if (!localIds.empty())
				s += " Linking adds your " + std::to_string(localIds.size()) +
				     (localIds.size() == 1 ? " circuit here to them." : " circuits here to them.") +
				     " Circuits that are already the same aren't doubled.";
			s += " Anyone with this code can see and change all of them. Only link with a code you made yourself.";
			pv.sentence = s;
		}
		return 200;
	} catch (const NetError&) {
		message = "Couldn't reach CedarLogic's website. Check the connection and try again.";
		return 0;
	}
}

bool Core::link(const std::string& codeText, std::string& message, bool confirm) {
	std::string code, why;
	if (!parseCode(crypto, codeText, code, why)) {
		message = whyText(why, codeText);
		return false;
	}
	const Keys k = keysForCode(crypto, code);
	json::Value out;
	int status;
	try {
		status = rawApi(k, "GET", "/spaces/" + k.spaceId, nullptr, out);
	} catch (const NetError&) {
		message = "Couldn't reach CedarLogic's website. Check the connection and try again.";
		return false;
	}
	if (status == 404) {
		message = "No circuits are synced with this code. Check the code, or turn on sync on your other device first.";
		return false;
	}
	if (status == 410) {
		message = out.str("error") == "space_expired" ? "That synced copy was removed after a year without use."
		                                               : "That synced copy was deleted.";
		return false;
	}
	if (status != 200) {
		message = status == 401 ? "This code doesn't match." : out.str("message", defaultMessage(status, out.str("error")));
		return false;
	}
	if (!confirm) return true;
	reset();
	setCode(code);
	st.joining = true;
	status_ = "synced";
	save();
	return true;
}

void Core::turnOff(bool removeSynced) {
	if (removeSynced) {
		main([&] {
			for (const auto& kv : st.records)
				if (!kv.second.local.empty() && lib.exists(kv.second.local)) trashLocal(kv.second.local);
			if (hooks.libraryChanged) hooks.libraryChanged();
		});
	}
	if (on && st.device.has) {
		try {   // best effort: this device leaves the list
			RecState ds;
			ds.ver = st.device.ver;
			bool big = false;
			Item it = writeItem(st.device.id, ds, tombstoneJson(serverNow(), st.deviceName, st.deviceId, nullptr, nullptr), true,
			                    false, nullptr, false, big);
			if (!it.id.empty()) {
				json::Value body = json::Value::object(), out;
				json::Value w = json::Value::array();
				w.push(itemJson(it));
				body.set("writes", w);
				rawApi(keys_, "POST", "/spaces/" + keys_.spaceId + "/write", &body, out);
			}
		} catch (const std::exception&) {
		}
	}
	disable();
	reset();
}

int Core::deleteSyncedCopy(std::string& message) {
	if (!on) return 200;
	json::Value out;
	int status;
	try {
		status = rawApi(keys_, "DELETE", "/spaces/" + keys_.spaceId, nullptr, out, { { "x-cedarlogic-delete", keys_.deleteToken } });
	} catch (const NetError&) {
		message = "Couldn't reach CedarLogic's website. Check the connection and try again.";
		return 0;
	}
	if (status == 200 || status == 410 || status == 404) {
		disable();
		reset();
		return 200;
	}
	message = out.str("message", defaultMessage(status, out.str("error")));
	return status;
}

// ---- one cycle (§4.4) --------------------------------------------------------------------------------

void Core::sync(bool flush) {
	if (!on) return;
	status_ = "syncing";
	statusText_.clear();
	retryAfter_ = 0;
	halted_ = false;
	full_ = false;
	libraryFailed_.clear();
	saveFailed_ = false;
	noticedWentBack_ = false;
	try {
		if (!opt.web) {
			std::string id;
			int64_t gen = 0;
			main([&] {
				id = lib.libraryId();
				gen = lib.libraryGen();
			});
			if (id.empty()) throw LibraryError("Your Circuits can't be written to");
			if (st.libraryId.empty()) {
				st.libraryId = id;
				st.libraryGen = gen;
			} else if (id != st.libraryId || gen != st.libraryGen) {
				libraryWentBack();
				st.libraryId = id;
				st.libraryGen = gen;
			}
		}
		if (st.applying) {   // a crash while applying: the join rule maps what was already written
			st.joining = true;
			st.applying = false;
		}
		pull();
		push(flush);
		if (!opt.web) {
			const int64_t gen = std::max<int64_t>(st.libraryGen, 0) + 1;
			bool ok = false;
			main([&] { ok = lib.setLibraryGen(gen); });
			if (ok) st.libraryGen = gen;
		}
		st.lastSyncAt = now();
		save();
		if (!libraryFailed_.empty()) {
			status_ = "error";
			statusText_ = libraryFailed_;
		} else if (saveFailed_) {
			status_ = "error";
			statusText_ = "the sync state couldn't be saved";
		} else {
			status_ = full_ ? "full" : "synced";
			if (full_) statusText_ = fullText_;
		}
	} catch (const NetError&) {
		status_ = "offline";
		save();
	} catch (const SpaceGone& e) {
		status_ = "gone";
		goneReason_ = e.what();
		st.gone = goneReason_;
		save();
		on = false;
	} catch (const BusyError& e) {
		status_ = "busy";
		statusText_ = e.status == 429 ? "Lots of syncing just now. Trying again in a minute." : e.what();
		retryAfter_ = e.retryAfterMs;
		save();
	} catch (const HttpError& e) {
		status_ = "error";
		statusText_ = e.what();
		halted_ = e.status == 401 || e.status == 403;   // the code doesn't match, or not from CedarLogic: no retries
		save();
	} catch (const LibraryError& e) {
		status_ = "error";
		statusText_ = e.what();
		save();
	} catch (const Stopped&) {
		status_ = "stopped";
		save();
	} catch (const std::exception& e) {
		status_ = "error";
		statusText_ = e.what();
		save();
	}
}

void Core::syncPushOnly() {
	if (!on) return;
	quitting_ = true;
	try {
		// Only a device that is in step may send on its way out. In the middle of a join, a
		// re-join or an apply, or after the library was replaced or restored, the next normal
		// cycle must decide first: a push now would send duplicates, or deletes for circuits
		// that are only missing from a restored folder.
		bool inStep = st.gone.empty() && !st.joining && !st.applying && st.hints.empty();
		if (inStep && !opt.web) {
			const std::string id = lib.libraryId();
			inStep = !id.empty() && id == st.libraryId && lib.libraryGen() == st.libraryGen;
		}
		if (inStep) push(true);
		save();
	} catch (const std::exception&) {
		save();
	}
	quitting_ = false;
}

void Core::libraryWentBack() {
	st.hints.clear();
	for (const auto& kv : st.seen)
		if (!kv.second.local.empty()) st.hints[kv.first] = kv.second.local;
	for (const auto& kv : st.records)
		if (!kv.second.local.empty()) st.hints[kv.first] = kv.second.local;
	st.refetch.clear();
	for (const auto& kv : st.hints) st.refetch.push_back(kv.first);
	st.records.clear();
	st.cursor = 0;
	st.joining = true;
	st.force.clear();
	note("Your library was restored from a backup. Syncing it again; nothing was deleted.");
	save();
}

void Core::rewound(const std::string& epoch) {
	if (!st.epoch.empty()) note("The synced copy was reset on the website. Sending your circuits again.");
	st.epoch = epoch;
	st.cursor = 0;
	st.purgedSeq = 0;
}

// ---- helpers -----------------------------------------------------------------------------------------

bool Core::hashesOf(const std::string& local, LocalHashes& h) { return lib.hashes(local, h); }

std::string Core::mappedRid(const std::string& local) const {
	for (const auto& kv : st.records)
		if (kv.second.local == local) return kv.first;
	return std::string();
}

void Core::seenSet(const std::string& rid, int64_t ver, const std::string& h, const std::string* local) {
	SeenRec& s = st.seen[rid];
	s.ver = ver;
	s.h = h;
	if (local) s.local = *local;
}

void Core::addForce(const std::string& rid) {
	if (!inForce(rid)) st.force.push_back(rid);
}

void Core::removeForce(const std::string& rid) { st.force.erase(std::remove(st.force.begin(), st.force.end(), rid), st.force.end()); }

bool Core::inForce(const std::string& rid) const { return std::find(st.force.begin(), st.force.end(), rid) != st.force.end(); }

bool Core::inRefetch(const std::string& rid) const {
	return std::find(st.refetch.begin(), st.refetch.end(), rid) != st.refetch.end();
}

bool Core::newer(const Payload& p, int64_t updatedAt, int64_t hereMtime) {
	const int64_t theirs = effectiveTime(p.modifiedAt, updatedAt), mine = hereMtime + st.offset;
	if (theirs != mine) return theirs > mine;
	return p.deviceId > st.deviceId;
}

bool Core::held(const std::string& local, bool runtimeOnly) {
	if (opt.web || quitting_ || !hooks.held) return false;
	return hooks.held(local, runtimeOnly);
}

std::string Core::recNew(const std::string& rid, const std::string& local, int64_t ver, const std::string* name,
                         const std::string* cdl) {
	if (joinIndexBuilt_)
		joinCandidates_.erase(std::remove_if(joinCandidates_.begin(), joinCandidates_.end(),
		                                     [&](const std::pair<std::string, LocalHashes>& c) { return c.first == local; }),
		                      joinCandidates_.end());
	RecState r;
	r.local = local;
	r.ver = ver;
	if (name && cdl) {
		r.n = nameHash(crypto, *name);
		r.c = cdlHash(crypto, *cdl);
		r.st = structureHash(*cdl);
	}
	st.records[rid] = r;
	return rid;
}

std::string Core::createLocal(const Payload& p) {
	const int64_t mtime = p.modifiedAt - st.offset;
	const std::string id =
		lib.create(p.name, p.cdl, mtime, "From " + orDevice(p.device) + " \xC2\xB7 edited " + versionNoteTime(mtime));
	if (id.empty()) throw LibraryError("Your Circuits can't be written to");
	libraryTouched_ = true;
	return id;
}

void Core::setLocal(const std::string& local, const std::string& name, const std::string& cdl, const Payload& p) {
	std::string hereName, hereCdl;
	int64_t mtime = 0, created = 0;
	if (!lib.read(local, hereName, hereCdl, mtime, created)) throw LibraryError("a circuit in Your Circuits can't be read");
	if (lib.keepsVersions() && structureHash(hereCdl) != structureHash(cdl)) {
		lib.keepVersion(local, hereCdl, "Before the change from " + orDevice(p.device));
		lib.keepVersion(local, cdl,
		                "From " + orDevice(p.device) + " \xC2\xB7 edited " + versionNoteTime(p.modifiedAt - st.offset));
	}
	if (hereName != name || hereCdl != cdl) {
		if (!lib.write(local, name, cdl, std::max(mtime, p.modifiedAt - st.offset)))
			throw LibraryError("Your Circuits can't be written to");
		libraryTouched_ = true;
		if (hooks.replaced) hooks.replaced(local, orDevice(p.device));
	}
}

void Core::keepLoser(const std::string& local, const std::string& name, const std::string& cdl, int64_t serverTime,
                     const std::string& device) {
	if (lib.keepsVersions()) {
		lib.keepVersion(local, cdl,
		                "From " + orDevice(device) + " \xC2\xB7 " + q(name) + " \xC2\xB7 edited " +
		                    versionNoteTime(serverTime - st.offset) + " \xC2\xB7 changed on both");
	} else {
		if (lib.create(name + " (from " + orDevice(device) + ")", cdl, serverTime - st.offset, std::string()).empty())
			throw LibraryError("Your Circuits can't be written to");
		libraryTouched_ = true;
	}
}

void Core::trashLocal(const std::string& local) {
	if (hooks.closing && !quitting_) hooks.closing(local);
	if (!lib.trash(local)) throw LibraryError("a circuit couldn't be moved to Recently Deleted");
	libraryTouched_ = true;
}

void Core::wentBack(const std::string& rid, int64_t ver) {
	auto it = st.records.find(rid);
	if (it != st.records.end() && !it->second.local.empty() && lib.exists(it->second.local)) {
		it->second.ver = ver;
		addForce(rid);
	}
	if (!noticedWentBack_) note("The website sent an older copy of a circuit. This device's copy was sent again.");
	noticedWentBack_ = true;
}

void Core::damaged(const std::string& rid, int64_t ver, bool tombstone, const std::string& h) {
	auto it = st.records.find(rid);
	if (it != st.records.end() && !it->second.local.empty() && lib.exists(it->second.local)) {
		it->second.ver = ver;
		addForce(rid);
		std::string name, cdl;
		int64_t m, c;
		lib.read(it->second.local, name, cdl, m, c);
		note(q(name) + " was damaged in the synced copy; this device's copy was sent again.");
	} else if (tombstone) {
		seenSet(rid, ver, h, nullptr);   // a damaged tombstone of a circuit not here: nothing to open, nothing to count
	} else {
		st.unreadable[rid] = { ver, "damaged" };
	}
}

bool Core::openFetched(const std::string& rid, int64_t ver, const std::string& data, Payload& p, std::string& why) {
	Bytes env;
	std::string payload;
	if (!unb64u(data, env) || !openRecord(crypto, keys_.recordKey, rid, ver, env, payload)) {
		why = "damaged";
		return false;
	}
	why = readPayload(payload, p);
	if (why == "invalid") why = "damaged";
	return why.empty();
}

std::map<std::string, json::Value> Core::fetchIds(std::vector<std::string> ids) {
	std::map<std::string, json::Value> got;
	int rounds = 0;
	while (!ids.empty()) {
		const size_t n = std::min(ids.size(), kFetchIds);
		json::Value body = json::Value::object(), list = json::Value::array(), out;
		for (size_t i = 0; i < n; i++) list.push(json::Value::string(ids[i]));
		body.set("ids", list);
		api("POST", "/fetch", &body, out);
		if (const json::Value* rs = out.get("records"))
			for (const json::Value& r : rs->a)
				if (isUuid(r.str("id"))) got[r.str("id")] = r;
		std::vector<std::string> next = stringsFrom(out.get("deferred"));
		next.insert(next.end(), ids.begin() + (long)n, ids.end());
		// A server that defers everything forever must not keep us here.
		if (next.size() >= ids.size() && ++rounds > 20) break;
		ids.swap(next);
	}
	return got;
}

// ---- pull (§4.5) ---------------------------------------------------------------------------------------

void Core::pull() {
	joinIndexBuilt_ = false;
	joinCandidates_.clear();
	int64_t since = st.cursor;
	std::vector<json::Value> entries;
	json::Value page;
	for (int restarts = 0;; restarts++) {
		if (restarts > 5) throw HttpError(500, "server_error", "the synced copy keeps changing");
		entries.clear();
		int64_t s = since;
		bool restart = false;
		for (int pages = 0;; pages++) {
			if (pages > 100000) throw HttpError(500, "server_error", "too many pages");
			api("GET", "/changes?since=" + std::to_string(s) + (opt.changesLimit > 0 ? "&limit=" + std::to_string(opt.changesLimit) : ""),
			    nullptr, page);
			const std::string epoch = page.str("epoch");
			if (!isHex(epoch, 32) || !page.get("seq") || !page.get("seq")->isInt() || !page.get("entries") ||
			    !page.get("entries")->isArray())
				throw HttpError(500, "server_error", "the website's answer wasn't understood");
			const int64_t seq = page.integer("seq"), purged = page.integer("purgedSeq");
			if (page.get("pollSeconds") && page.get("pollSeconds")->isInt())
				pollSeconds_ = std::max<int64_t>(60, page.integer("pollSeconds"));
			if ((!st.epoch.empty() && epoch != st.epoch) || seq < st.cursor) {
				rewound(epoch);
				since = 0;
				restart = true;
				break;
			}
			if (since > 0 && purged > since) {   // tombstones we never saw were purged
				since = 0;
				restart = true;
				break;
			}
			if (const json::Value* es = page.get("entries"))
				for (const json::Value& e : es->a)
					if (isUuid(e.str("id")) && e.get("ver") && e.get("ver")->isInt()) entries.push_back(e);
			if (!page.flag("more")) break;
			const int64_t next = page.integer("next");
			if (next <= s) throw HttpError(500, "server_error", "the change list doesn't move on");
			s = next;
		}
		if (!restart) break;
	}
	st.epoch = page.str("epoch");
	const bool full = since == 0;
	const int64_t pageSeq = page.integer("seq"), pagePurged = page.integer("purgedSeq");

	// What to fetch; what went back.
	std::vector<json::Value> need;
	std::vector<std::pair<std::string, int64_t>> back;
	for (const json::Value& e : entries) {
		const std::string id = e.str("id"), h = e.str("h");
		const int64_t ver = e.integer("ver");
		auto sv = st.seen.find(id);
		auto sr = st.records.find(id);
		const bool hasS = sr != st.records.end();
		if (sv != st.seen.end() && (ver < sv->second.ver || (ver == sv->second.ver && h != sv->second.h && hasS))) {
			back.emplace_back(id, ver);   // an older (or swapped) copy: ignored (H1)
			continue;
		}
		if (hasS && sr->second.ver == ver) continue;   // in step (often our own write)
		if (sv != st.seen.end() && sv->second.ver == ver && sv->second.h == h && !hasS && !inRefetch(id)) continue;
		auto u = st.unreadable.find(id);
		if (u != st.unreadable.end() && u->second.first == ver) continue;
		need.push_back(e);
	}
	std::sort(need.begin(), need.end(),
	          [](const json::Value& a, const json::Value& b) { return a.integer("seq") < b.integer("seq"); });
	std::vector<std::string> ids;
	for (const json::Value& e : need) ids.push_back(e.str("id"));
	if (!ids.empty() && hooks.progress && st.joining) hooks.progress("bringing", 0, (int)ids.size());
	std::map<std::string, json::Value> got = fetchIds(ids);

	// Decrypt here (the engine thread); apply on the UI thread.
	struct Opened {
		json::Value e;
		bool present = false;
		std::string why;
		Payload p;
	};
	std::vector<Opened> opened;
	for (const json::Value& e : need) {
		Opened o;
		o.e = e;
		auto r = got.find(e.str("id"));
		if (r != got.end() && r->second.integer("ver") == e.integer("ver")) {
			o.present = true;
			openFetched(e.str("id"), e.integer("ver"), r->second.str("data"), o.p, o.why);
		}
		opened.push_back(std::move(o));
	}

	std::vector<int64_t> held;
	std::vector<Incoming> incoming;
	if (!opened.empty() || !back.empty()) {
		st.applying = true;   // a crash before this pull ends: the next one re-joins (§4.5 cursor rule)
		save();
	}
	if (!back.empty())
		main([&] {
			for (const auto& b : back) wentBack(b.first, b.second);
		});
	// The join rule's candidates (every unmapped local circuit, hashed) in short steps first.
	if (st.joining && !opened.empty()) buildJoinIndex();
	// Applied in short steps on the UI thread, the state saved after each.
	const size_t kStep = kUiStep;
	for (size_t at = 0; at < opened.size(); at += kStep) {
		if (hooks.progress && st.joining && opened.size() > kStep) hooks.progress("bringing", (int)at, (int)opened.size());
		main([&] {
			libraryTouched_ = false;
			for (size_t k = at; k < std::min(opened.size(), at + kStep); k++) {
				Opened& o = opened[k];
				const std::string rid = o.e.str("id");
				const int64_t ver = o.e.integer("ver"), seq = o.e.integer("seq");
				if (!o.present) continue;   // changed or purged meanwhile: the next cycle
				if (o.why == "newer") {
					st.unreadable[rid] = { ver, "newer" };
					seenSet(rid, ver, o.e.str("h"), nullptr);
					continue;
				}
				if (o.why == "damaged") {
					damaged(rid, ver, o.e.flag("deleted"), o.e.str("h"));
					continue;
				}
				st.unreadable.erase(rid);
				bool ok = true;
				try {
					if (o.p.kind == "circuit") {
						ok = onRemoteUpdate(rid, ver, o.e.integer("updatedAt"), o.p);
					} else if (o.p.kind == "deleted") {
						const size_t before = incoming.size();
						ok = onRemoteDelete(rid, ver, o.p, incoming);
						for (size_t q2 = before; q2 < incoming.size(); q2++) incoming[q2].seq = seq;
					} else if (rid != st.device.id) {
						st.devices[rid] = { o.p.device, o.p.lastSyncAt };
					}
				} catch (const LibraryError& x) {
					libraryFailed_ = x.what();
					ok = false;
				}
				if (ok) {
					auto r = st.records.find(rid);
					seenSet(rid, ver, o.e.str("h"), r != st.records.end() ? &r->second.local : nullptr);
				} else {
					held.push_back(seq);
				}
			}
			if (libraryTouched_ && hooks.libraryChanged) hooks.libraryChanged();
		});
		save();
	}
	if (!incoming.empty())   // (one whose window has edits in it by the time the person answers stays, cursor held)
		for (int64_t sq : incomingDeletes(incoming))
			if (sq > 0) held.push_back(sq);
	main([&] {
		if (full) {
			std::set<std::string> present;
			for (const json::Value& e : entries) present.insert(e.str("id"));
			std::vector<std::string> gone;
			for (const auto& kv : st.records)
				if (kv.second.ver > 0 && !present.count(kv.first)) gone.push_back(kv.first);
			for (const std::string& rid : gone) onRemoteForgotten(rid);
			// What the whole list no longer has is forgotten too (a reset synced copy, a purge): a
			// damaged record that's gone, another device's record that's gone, and this device's own
			// device record, which goes out again with this cycle's push (from nothing, base 0).
			for (auto u = st.unreadable.begin(); u != st.unreadable.end();)
				u = present.count(u->first) ? std::next(u) : st.unreadable.erase(u);
			for (auto d = st.devices.begin(); d != st.devices.end();)
				d = present.count(d->first) ? std::next(d) : st.devices.erase(d);
			if (st.device.has && !present.count(st.device.id)) {
				st.device.ver = 0;
				st.device.at = 0;
			}
		}
	});
	st.cursor = held.empty() ? pageSeq : *std::min_element(held.begin(), held.end()) - 1;
	st.purgedSeq = pagePurged;
	if (held.empty()) {
		st.joining = false;
		st.refetch.clear();
		st.hints.clear();
	}
	heldLastPull_ = !held.empty();
	joinIndexBuilt_ = false;
	joinCandidates_.clear();
	st.applying = false;
	save();
}

// The join rule's candidates -- every local circuit not yet mapped, with its hashes -- built in short
// steps on the UI thread, so linking a device with a big library doesn't freeze its windows.
void Core::buildJoinIndex() {
	if (joinIndexBuilt_) return;
	std::vector<std::string> all;
	std::set<std::string> mappedSet;
	main([&] {
		lib.list(all);
		for (const auto& kv : st.records) mappedSet.insert(kv.second.local);
	});
	std::vector<std::pair<std::string, LocalHashes>> found;
	for (size_t at = 0; at < all.size(); at += kUiStep)
		main([&] {
			LocalHashes h;
			for (size_t k = at; k < std::min(all.size(), at + kUiStep); k++)
				if (!mappedSet.count(all[k]) && hashesOf(all[k], h)) found.emplace_back(all[k], h);
		});
	joinCandidates_ = std::move(found);
	joinIndexBuilt_ = true;
}

// ---- applying (§4.6) -------------------------------------------------------------------------------------

bool Core::onRemoteUpdate(const std::string& rid, int64_t ver, int64_t updatedAt, const Payload& p) {
	const std::string rN = nameHash(crypto, p.name), rC = cdlHash(crypto, p.cdl);
	auto it = st.records.find(rid);
	if (it != st.records.end()) {
		RecState& s = it->second;
		if (s.sent.has && !s.sent.deleted && s.sent.ver == ver && s.sent.n == rN && s.sent.c == rC) {
			s.ver = ver;   // our own write; its answer was lost
			s.n = rN;
			s.c = rC;
			s.st = structureHash(p.cdl);
			s.sent = Sent();
			return true;
		}
	}
	if (it == st.records.end()) {
		auto hint = st.hints.find(rid);
		std::string lid;
		if (hint != st.hints.end()) {
			lid = hint->second;
			st.hints.erase(hint);
		}
		if (!lid.empty() && lib.exists(lid) && mappedRid(lid).empty()) {
			recNew(rid, lid, 0, nullptr, nullptr);   // restored library: mapped, base unknown
			it = st.records.find(rid);
		} else {
			std::string match;
			if (st.joining) {   // the join rule (§4.8): the same content, else the same name and structure
				if (!joinIndexBuilt_) {
					joinIndexBuilt_ = true;
					joinCandidates_.clear();
					std::vector<std::string> all;
					lib.list(all);   // (no folder yet: nothing to match)
					std::set<std::string> mappedSet;
					for (const auto& kv : st.records) mappedSet.insert(kv.second.local);
					LocalHashes h;
					for (const std::string& x : all)
						if (!mappedSet.count(x) && hashesOf(x, h)) joinCandidates_.emplace_back(x, h);
				}
				const std::string ch = contentHash(crypto, p.name, p.cdl), rSt = structureHash(p.cdl);
				auto pick = [&](const std::function<bool(const LocalHashes&)>& same) {
					for (auto c = joinCandidates_.begin(); c != joinCandidates_.end(); ++c)
						if (same(c->second)) return c->first;   // (mapped ones leave the list: recNew)
					return std::string();
				};
				match = pick([&](const LocalHashes& h) { return h.ch == ch; });
				if (match.empty()) match = pick([&](const LocalHashes& h) { return h.n == rN && h.st == rSt; });
			}
			if (!match.empty()) {
				if (held(match, true)) return false;
				recNew(rid, match, ver, &p.name, &p.cdl);
				std::string name, cdl;
				int64_t mtime = 0, created = 0;
				lib.read(match, name, cdl, mtime, created);
				if ((name != p.name || cdl != p.cdl) && newer(p, updatedAt, mtime)) setLocal(match, p.name, p.cdl, p);
				return true;   // (if here is newer, it now counts as changed and is sent)
			}
			const std::string local = createLocal(p);
			recNew(rid, local, ver, &p.name, &p.cdl);
			if (st.seen.count(rid) && !st.joining && !inRefetch(rid))   // (Bring Them Back is quiet)
				note(q(p.name) + " was changed on " + orDevice(p.device) + " after it was deleted here, so it's back.");
			return true;
		}
	}
	RecState& s = it->second;
	if (s.local.empty() || !lib.exists(s.local)) {   // deleted here, changed there: edits win
		s.local = createLocal(p);
		s.ver = ver;
		s.n = rN;
		s.c = rC;
		s.st = structureHash(p.cdl);
		s.sent = Sent();
		note(q(p.name) + " was changed on " + orDevice(p.device) + ", so it was kept.");
		return true;
	}
	const std::string lid = s.local;
	std::string hereName, hereCdl;
	int64_t hereMtime = 0, created = 0;
	if (!lib.read(lid, hereName, hereCdl, hereMtime, created)) throw LibraryError("a circuit in Your Circuits can't be read");
	const std::string lN = nameHash(crypto, hereName), lC = cdlHash(crypto, hereCdl);
	const std::string rSt = structureHash(p.cdl), hereSt = structureHash(hereCdl);
	const bool runtimeOnly = lN == rN && hereSt == rSt;
	if ((lN != rN || lC != rC) && held(lid, runtimeOnly)) return false;   // an open window: next cycle
	std::string bN = s.n, bC = s.c, bSt = s.st;
	if (s.sent.has && !s.sent.deleted && p.hasBase && p.baseName == s.sent.n && p.baseCdl == s.sent.c) {
		bN = s.sent.n;   // built on our write whose answer was lost
		bC = s.sent.c;
		bSt = s.sent.st;
	}
	s.ver = ver;
	s.n = rN;
	s.c = rC;
	s.st = rSt;
	s.sent = Sent();
	if (lN == rN && lC == rC) return true;   // same already
	if (p.hasBase && p.baseName == lN && p.baseCdl == lC) {   // built on exactly what is here: fast-forward
		setLocal(lid, p.name, p.cdl, p);
		return true;
	}
	// The name and the circuit are merged separately.
	std::string name;
	if (lN == rN || lN == bN) name = p.name;
	else if (rN == bN) name = hereName;
	else name = newer(p, updatedAt, hereMtime) ? p.name : hereName;
	std::string cdl;
	if (lC == rC || lC == bC) {
		cdl = p.cdl;
	} else if (rC == bC) {
		cdl = hereCdl;
	} else if (hereSt == rSt) {   // only runtime state / the writer differ: the newer, quietly
		cdl = newer(p, updatedAt, hereMtime) ? p.cdl : hereCdl;
	} else if (!bSt.empty() && hereSt == bSt) {   // here only runtime state changed: theirs, quietly
		cdl = p.cdl;
	} else if (!bSt.empty() && rSt == bSt) {   // there only runtime state changed: ours, quietly
		cdl = hereCdl;
	} else if (newer(p, updatedAt, hereMtime)) {   // changed on both: the newer wins, nothing is lost
		keepLoser(lid, hereName, hereCdl, hereMtime + st.offset, st.deviceName);
		cdl = p.cdl;
		note(conflictNotice(name, p.device, hereName, st.deviceName));
	} else {
		keepLoser(lid, p.name, p.cdl, p.modifiedAt, p.device);
		cdl = hereCdl;
		note(conflictNotice(name, p.device, p.name, p.device));
	}
	setLocal(lid, name, cdl, p);
	return true;
}

std::string Core::conflictNotice(const std::string& name, const std::string& device, const std::string& loserName,
                                 const std::string& loserDevice) {
	std::string s = q(name) + " was changed here and on " + orDevice(device) + ". The newer one is open; ";
	if (lib.keepsVersions()) return s + "the other is in Version History.";
	return s + "the other is now " + q(loserName + " (from " + orDevice(loserDevice) + ")") + ".";
}

// ---- deletes (§4.7) -------------------------------------------------------------------------------------------

bool Core::onRemoteDelete(const std::string& rid, int64_t ver, const Payload& p, std::vector<Incoming>& incoming) {
	st.devices.erase(rid);
	auto it = st.records.find(rid);
	if (it == st.records.end()) {
		auto hint = st.hints.find(rid);
		if (hint == st.hints.end()) return true;
		const std::string lid = hint->second;
		st.hints.erase(hint);
		if (!lib.exists(lid) || !mappedRid(lid).empty()) return true;
		recNew(rid, lid, ver, nullptr, nullptr);   // restored library: the folder this record had
		LocalHashes h;
		if (p.hasBase && hashesOf(lid, h) && h.n == p.baseName && h.c == p.baseCdl) {
			trashLocal(lid);   // deleted after the backup was taken
			st.records.erase(rid);
		}
		return true;
	}
	RecState& s = it->second;
	if (s.sent.has && s.sent.deleted && s.sent.ver == ver) {   // our own delete; its answer was lost
		st.records.erase(it);
		return true;
	}
	if (s.local.empty() || !lib.exists(s.local)) {
		st.records.erase(it);
		return true;
	}
	LocalHashes h;
	if (!hashesOf(s.local, h)) throw LibraryError("a circuit in Your Circuits can't be read");
	if ((h.n == s.n && h.c == s.c) || (p.hasBase && h.n == p.baseName && h.c == p.baseCdl)) {
		if (held(s.local, false)) return false;
		incoming.push_back(Incoming{ rid, s.local, ver, orDevice(p.device) });
	} else {   // changed here: edits win
		s.ver = ver;
		std::string name, cdl;
		int64_t m, c;
		lib.read(s.local, name, cdl, m, c);
		note(q(name) + " was deleted on " + orDevice(p.device) + ", but you'd changed it here, so it was kept.");
	}
	return true;
}

std::vector<int64_t> Core::incomingDeletes(std::vector<Incoming>& items) {
	std::vector<int64_t> stays;
	int synced = 0;
	main([&] {
		for (const auto& kv : st.records)
			if (!kv.second.local.empty() && lib.exists(kv.second.local)) synced++;
	});
	std::vector<std::string> devs;
	for (const Incoming& i : items)
		if (std::find(devs.begin(), devs.end(), i.device) == devs.end()) devs.push_back(i.device);
	bool keep = false;
	if (items.size() > 5 && 2 * (int)items.size() > synced) {
		if (quitting_) return stays;
		keep = hooks.askIncomingDeletes ? !hooks.askIncomingDeletes((int)items.size(), listWords(devs)) : false;
	}
	main([&] {
		std::vector<std::string> names;
		for (const Incoming& i : items) {
			auto it = st.records.find(i.rid);
			if (it == st.records.end()) continue;
			if (keep) {   // Keep Them: they are sent back everywhere
				it->second.ver = i.ver;
				addForce(i.rid);
				continue;
			}
			// The person may have taken a while to answer: a window with edits in it by now is left alone.
			if (held(i.local, false)) {
				stays.push_back(i.seq);
				continue;
			}
			std::string name, cdl;
			int64_t m, c;
			lib.read(i.local, name, cdl, m, c);
			try {
				trashLocal(i.local);
			} catch (const LibraryError& x) {
				libraryFailed_ = x.what();
				stays.push_back(i.seq);
				continue;
			}
			st.records.erase(it);
			names.push_back(q(name));
		}
		if (!names.empty()) {
			std::string list;
			for (size_t k = 0; k < names.size(); k++) list += (k ? ", " : "") + names[k];
			note("Moved to Recently Deleted: " + list + " (deleted on " + listWords(devs) + ").");
		}
		if (hooks.libraryChanged) hooks.libraryChanged();
	});
	return stays;
}

void Core::onRemoteForgotten(const std::string& rid) {
	auto it = st.records.find(rid);
	if (it == st.records.end()) return;
	if (it->second.local.empty() || !lib.exists(it->second.local)) {
		st.records.erase(it);
		return;
	}
	it->second.ver = 0;   // sent again; the server's ghost makes the write ver higher (412 -> base)
	addForce(rid);
}

// ---- push (§4.9) --------------------------------------------------------------------------------------------------

json::Value Core::itemJson(const Item& it) {
	json::Value w = json::Value::object();
	w.set("id", json::Value::string(it.id));
	w.set("base", json::Value::integer(it.base));
	w.set("ver", json::Value::integer(it.ver));
	w.set("data", json::Value::string(it.data));
	w.set("deleted", json::Value::boolean(it.deleted));
	if (it.device) w.set("device", json::Value::boolean(true));
	return w;
}

Core::Item Core::writeItem(const std::string& rid, RecState& s, const std::string& payload, bool deleted, bool compress,
                           const Sent* content, bool device, bool& tooBig) {
	tooBig = false;
	auto sv = st.seen.find(rid);
	const int64_t ver = std::max(s.ver, sv != st.seen.end() ? sv->second.ver : 0) + 1;
	Bytes env;
	std::string why;
	if (!sealRecord(crypto, keys_.recordKey, rid, ver, payload, compress, deleted || device ? kMaxSmallEnvelope : kMaxEnvelope,
	                env, why)) {
		if (why == "too big") {
			tooBig = true;
			return Item();
		}
		throw HttpError(0, "crypto", "this computer couldn't encrypt a circuit");   // never sealed without a good RNG
	}
	s.sent = Sent();
	s.sent.has = true;
	s.sent.ver = ver;
	s.sent.deleted = deleted;
	if (content) {
		s.sent.n = content->n;
		s.sent.c = content->c;
		s.sent.st = content->st;
		s.sent.ch = content->ch;
	}
	Item it;
	it.id = rid;
	it.base = s.ver;
	it.ver = ver;
	it.data = b64u(env);
	it.deleted = deleted;
	it.device = device;
	return it;
}

void Core::push(bool flush) {
	full_ = false;
	const int64_t t = now();
	const bool restoring = !st.hints.empty() || st.joining;

	// The mass-delete guard (here).
	std::vector<std::string> goneHere;
	int synced = 0;
	bool readable = true;
	main([&] {
		std::vector<std::string> ids;
		readable = lib.list(ids) || st.records.empty();   // no library folder yet: nothing to send
		if (!readable) return;
		for (const auto& kv : st.records) {
			if (kv.second.ver > 0) synced++;
			if (kv.second.ver > 0 && (kv.second.local.empty() || !lib.exists(kv.second.local))) goneHere.push_back(kv.first);
		}
	});
	if (!readable) throw LibraryError("Your Circuits can't be read");
	bool noDeletes = false;
	if (!restoring && goneHere.size() > 5 && 2 * (int)goneHere.size() > synced) {
		bool everywhere = false;
		if (quitting_) noDeletes = true;
		else everywhere = hooks.askMassDelete ? hooks.askMassDelete((int)goneHere.size()) : true;
		if (!quitting_ && !everywhere) {   // Bring Them Back
			for (const std::string& rid : goneHere) {
				st.records.erase(rid);
				if (!inRefetch(rid)) st.refetch.push_back(rid);
			}
			st.cursor = 0;
			save();
		}
	}

	for (int attempt = 0; attempt < 3; attempt++) {
		std::vector<Item> items, changedItems, newItems;
		std::map<std::string, std::string> meta;
		struct Candidate {
			std::string rid, name, cdl;
			int64_t mtime = 0, createdAt = -1;
			LocalHashes h;
			bool isNew = false;
		};
		std::vector<Candidate> candidates;
		std::vector<std::string> deleted;
		std::vector<std::pair<int64_t, std::string>> order;
		std::map<std::string, std::string> ridOf;
		main([&] {
			problems_.clear();
			// Deleted here (not while joining or re-joining).
			for (auto it = st.records.begin(); it != st.records.end();) {
				const RecState& s = it->second;
				if (restoring || noDeletes || (!s.local.empty() && lib.exists(s.local))) { ++it; continue; }
				auto u = st.unreadable.find(it->first);
				if (s.ver == 0 || (u != st.unreadable.end() && u->second.second == "newer")) {
					it = st.records.erase(it);
					continue;
				}
				deleted.push_back(it->first);
				++it;
			}
			// New or changed here, oldest first.
			std::vector<std::string> ids;
			lib.list(ids);
			for (const std::string& id : ids) order.emplace_back(lib.modified(id), id);
			std::sort(order.begin(), order.end());
			for (const auto& kv : st.records) ridOf.emplace(kv.second.local, kv.first);
		});
		// Reading and hashing the circuits (the first time, every one of them) goes in short
		// steps, so the person's windows keep answering.
		for (size_t chunk = 0; chunk < order.size(); chunk += kUiStep) main([&] {
			for (size_t k = chunk; k < std::min(order.size(), chunk + kUiStep); k++) {
				const std::string& lid = order[k].second;
				auto known = ridOf.find(lid);
				std::string rid = known == ridOf.end() ? std::string() : known->second;
				if (rid.empty()) {
					// A join that hasn't finished (a window held a record back): a circuit that
					// may be one of the synced ones is not sent as a new one.
					if (st.joining) continue;
					rid = newUuid(crypto);
					if (rid.empty()) throw HttpError(0, "crypto", "this computer couldn't make a record id");
					recNew(rid, lid, 0, nullptr, nullptr);
				}
				RecState& s = st.records[rid];
				auto u = st.unreadable.find(rid);
				if (u != st.unreadable.end() && u->second.second == "newer") {
					problems_[lid] = kNewer;
					continue;
				}
				LocalHashes h;
				if (!hashesOf(lid, h)) continue;
				const bool forced = inForce(rid);
				if (s.ver > 0 && h.n == s.n && h.c == s.c && !forced) continue;   // in step
				auto tb = st.tooBig.find(rid);
				if (tb != st.tooBig.end() && tb->second == h.ch) {
					problems_[lid] = kTooBig;
					continue;
				}
				auto bad = badRequest_.find(rid);
				if (bad != badRequest_.end() && bad->second == h.ch) continue;
				if (full_ && s.ver == 0 && !st.seen.count(rid)) continue;   // no room for new ones
				if (s.ver > 0 && !forced && h.n == s.n && h.st == s.st && !flush) {   // only switches/registers: later
					if (!st.lazySince) st.lazySince = t;
					if (t - st.lazySince < 10 * kMinute) continue;
				}
				Candidate c;
				c.rid = rid;
				c.h = h;
				c.isNew = !(s.ver > 0 || st.seen.count(rid));
				if (!lib.read(lid, c.name, c.cdl, c.mtime, c.createdAt)) continue;
				candidates.push_back(std::move(c));
			}
		});
		// Seal here, on the engine thread.
		for (const std::string& rid : deleted) {
			RecState& s = st.records[rid];
			bool big = false;
			Item it = writeItem(rid, s, tombstoneJson(t + st.offset, st.deviceName, st.deviceId, &s.n, &s.c), true, false,
			                    nullptr, false, big);
			if (it.id.empty()) continue;
			items.push_back(it);
			meta[rid] = "deleted";
		}
		for (const Candidate& c : candidates) {
			RecState& s = st.records[c.rid];
			const bool hasBase = !s.n.empty();
			const std::string payload = circuitJson(c.name, c.cdl, c.mtime + st.offset, st.deviceName, st.deviceId, c.createdAt,
			                                        hasBase ? &s.n : nullptr, hasBase ? &s.c : nullptr);
			Sent content;
			content.n = c.h.n;
			content.c = c.h.c;
			content.st = c.h.st;
			content.ch = c.h.ch;
			bool big = false;
			Item it = writeItem(c.rid, s, payload, false, true, &content, false, big);
			if (big) {
				st.tooBig[c.rid] = c.h.ch;
				problems_[s.local] = kTooBig;
				continue;
			}
			(c.isNew ? newItems : changedItems).push_back(it);
			meta[c.rid] = "circuit";
		}
		items.insert(items.end(), changedItems.begin(), changedItems.end());
		items.insert(items.end(), newItems.begin(), newItems.end());
		int damagedCount = 0;
		for (const auto& u : st.unreadable)
			if (u.second.second == "damaged") damagedCount++;
		if (damagedCount)
			problems_[""] = damagedCount == 1 ? "1 synced circuit is damaged and can't be opened."
			                                  : std::to_string(damagedCount) + " synced circuits are damaged and can't be opened.";
		if (items.empty() && attempt > 0) break;
		// The device record: none yet, a day old, or renamed; the last item.
		if (attempt == 0 && !full_ && !quitting_ &&
		    (!st.device.has || t - st.device.at > kDay || st.device.name != st.deviceName)) {
			std::string drid = st.device.has ? st.device.id : newUuid(crypto);
			if (!drid.empty()) {
				RecState ds;
				ds.ver = st.device.has ? st.device.ver : 0;
				const int64_t at = st.device.has ? st.device.at : 0;
				st.device.has = true;
				st.device.id = drid;
				st.device.ver = ds.ver;
				st.device.at = at;
				bool big = false;
				Item it = writeItem(drid, ds, deviceJson(st.deviceName, st.deviceId, opt.client, t + st.offset), false, false,
				                    nullptr, true, big);
				if (!it.id.empty()) {
					items.push_back(it);
					meta[drid] = "device";
				}
			}
		}
		if (items.empty()) break;
		save();   // every `sent` before the requests go out
		if (hooks.progress && st.joining && !newItems.empty()) hooks.progress("sending", 0, (int)newItems.size());
		bool retry = false;
		size_t at = 0;
		while (at < items.size()) {
			size_t end = at, chars = 0;
			while (end < items.size() && end - at < opt.batchItems &&
			       (end == at || chars + items[end].data.size() <= kBatchChars)) {
				chars += items[end].data.size();
				end++;
			}
			json::Value body = json::Value::object(), out;
			json::Value w = json::Value::array();
			for (size_t k = at; k < end; k++) w.push(itemJson(items[k]));
			body.set("writes", w);
			api("POST", "/write", &body, out);
			const json::Value* results = out.get("results");
			for (size_t k = at; k < end; k++) {
				const json::Value* res = results && results->isArray() && k - at < results->a.size() ? &results->a[k - at] : nullptr;
				if (!res) continue;
				retry = result(items[k], *res, meta) || retry;
			}
			save();
			at = end;
		}
		if (!retry || quitting_) break;
	}
	// Nothing runtime-only left unsent: the lazy clock stops.
	if (st.lazySince) {
		bool pending = false;
		main([&] {
			for (const auto& kv : st.records) {
				const RecState& s = kv.second;
				LocalHashes h;
				if (s.ver > 0 && !s.local.empty() && lib.exists(s.local) && hashesOf(s.local, h) && (h.n != s.n || h.c != s.c))
					pending = true;
			}
		});
		if (!pending) st.lazySince = 0;
	}
}

bool Core::result(const Item& it, const json::Value& res, const std::map<std::string, std::string>& meta) {
	const std::string& rid = it.id;
	const int status = (int)res.integer("status");
	auto m = meta.find(rid);
	if (m != meta.end() && m->second == "device") {
		if (status == 200 || status == 201) {
			const json::Value* e = res.get("entry");
			st.device.has = true;
			st.device.id = rid;
			st.device.ver = e ? e->integer("ver") : it.ver;
			st.device.at = now();
			st.device.name = st.deviceName;
			if (e) seenSet(rid, e->integer("ver"), e->str("h"), nullptr);
		} else if (status == 412) {   // written over, or gone (a reset copy): on the base it has now, within the hour
			const json::Value* cur = res.get("current");
			st.device.ver = cur && cur->isObject() ? cur->integer("ver") : 0;
			st.device.at = now() - kDay + kHour;
		} else {   // again tomorrow (a full space delays it, quietly)
			st.device.at = now();
		}
		return false;
	}
	auto sit = st.records.find(rid);
	if (sit == st.records.end()) return false;
	RecState& s = sit->second;
	const Sent sent = s.sent;
	if (status == 200 || status == 201) {
		const json::Value* e = res.get("entry");
		const int64_t ver = e ? e->integer("ver") : it.ver;
		seenSet(rid, ver, e ? e->str("h") : std::string(), &s.local);
		if (it.deleted) {
			st.records.erase(sit);
		} else {
			s.ver = ver;
			s.n = sent.n;
			s.c = sent.c;
			s.st = sent.st;
			s.sent = Sent();
		}
		removeForce(rid);
		return false;
	}
	s.sent = Sent();
	if (status == 413) {
		st.tooBig[rid] = sent.ch;
		problems_[s.local] = kTooBig;
		return false;
	}
	if (status == 507) {
		full_ = true;
		fullText_ = res.str("error") == "site_full" ? "The website's sync storage is full just now. New circuits stay on this device."
		                                            : kFullSentence;
		return false;
	}
	if (status == 400) {
		badRequest_[rid] = sent.ch;
		return false;
	}
	if (status != 412 || quitting_) return false;
	// 412: resolve, then try again.
	const json::Value* cur = res.get("current");
	if (!cur || !cur->isObject() || cur->flag("purged")) {
		s.ver = cur && cur->isObject() ? cur->integer("ver") : 0;   // the server has no such record: send it again
		addForce(rid);
		return true;
	}
	auto sv = st.seen.find(rid);
	const int64_t cver = cur->integer("ver");
	if (sv != st.seen.end() && (cver < sv->second.ver || (cver == sv->second.ver && cur->str("h") != sv->second.h))) {
		main([&] { wentBack(rid, cver); });
		return true;
	}
	std::map<std::string, json::Value> got = fetchIds({ rid });
	auto r = got.find(rid);
	if (r == got.end()) return true;
	const int64_t rver = r->second.integer("ver");
	Payload p;
	std::string why;
	openFetched(rid, rver, r->second.str("data"), p, why);
	if (why == "newer") {
		st.unreadable[rid] = { rver, "newer" };
		return false;
	}
	if (why == "damaged") {
		main([&] { damaged(rid, rver); });
		return true;
	}
	if (p.kind == "deleted") {
		std::vector<Incoming> inc;
		main([&] { onRemoteDelete(rid, rver, p, inc); });
		if (!inc.empty()) incomingDeletes(inc);
	} else if (p.kind == "circuit") {
		main([&] {
			try {
				onRemoteUpdate(rid, rver, r->second.integer("updatedAt"), p);
			} catch (const LibraryError& x) {
				libraryFailed_ = x.what();
			}
			if (libraryTouched_ && hooks.libraryChanged) hooks.libraryChanged();
		});
	}
	seenSet(rid, rver, r->second.str("h"), nullptr);
	return true;
}

// ---- for the UI ---------------------------------------------------------------------------------------

int Core::circuitCount() {
	int n = 0;
	for (const auto& kv : st.records)
		if (kv.second.ver > 0) n++;
	return n;
}

std::vector<std::pair<std::string, int64_t>> Core::deviceList() const {
	std::vector<std::pair<std::string, int64_t>> out;
	for (const auto& kv : st.devices)
		if (kv.first != st.device.id) out.push_back(kv.second);
	std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
	return out;
}

}  // namespace clsync
