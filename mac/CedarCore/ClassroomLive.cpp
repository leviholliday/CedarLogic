// The live connection of one class (CLASSROOM.md 3.14), as the web core's
// LiveSocket: hello, the server's messages handed to the Client, answers by
// id, pings, reconnecting with backoff, and the fallback to held polls after
// three tries that never got a hello. The socket itself is the platform's
// (SocketHooks: Host::socketOpen/Send/Close); nothing here blocks or keeps
// time of its own -- the engine thread calls in with `now`.

#include "ClassroomInternal.h"

#include <algorithm>
#include <climits>

namespace clclass {

namespace {

const int64_t kBackoff[] = { 1, 2, 5, 10, 30, 60 };   // seconds, +-20 % (4.8)
const int64_t kFallbackRetry = 300 * kSecond;          // the socket again every five minutes once polling took over
const int64_t kHelloWait = 15 * kSecond;               // no hello by then: a failed try
const int64_t kPing = 45 * kSecond;
const int64_t kRequestWait = 8 * kSecond;              // an answer's "ok" or "error"; then the HTTP way (4.6)

// The bye's error for a closing code that came without one (4000 + an HTTP status).
std::string errorOfCode(int status, bool teacher) {
	switch (status) {
	case 401: return teacher ? "wrong_key" : "wrong_token";
	case 403: return "not_a_member";
	case 404: return "no_class";
	case 410: return "class_deleted";
	default: return std::string();
	}
}

}  // namespace

LiveLink::LiveLink(Client& c, const std::string& classId, SocketHooks& h, uint32_t seed)
	: client_(c), classId_(classId), hooks_(h), seed_(seed ? seed : 2463534242u) {}

const char* LiveLink::stateName(State s) {
	switch (s) {
	case Connecting: return "connecting";
	case Open: return "open";
	case Fallback: return "fallback";
	case Closed: return "closed";
	default: return "";
	}
}

void LiveLink::setState(State s) {
	if (state_ == s) return;
	state_ = s;
	changed_ = true;
}

bool LiveLink::stateChanged() {
	const bool c = changed_;
	changed_ = false;
	return c;
}

void LiveLink::start(int64_t now) {
	if (!stopped_) return;
	stopped_ = false;
	connect(now);
}

void LiveLink::stop() {
	stopped_ = true;
	retryAt_ = helloBy_ = pingAt_ = 0;
	if (id_ && hooks_.close) hooks_.close(id_, 1000);
	id_ = 0;
	failPending();
	setState(Closed);
}

void LiveLink::abandon() {
	stopped_ = true;
	if (id_ && hooks_.close) hooks_.close(id_, 1000);
	id_ = 0;
	pending_.clear();
}

void LiveLink::connect(int64_t now) {
	if (stopped_) return;
	std::string hello;
	if (!client_.helloFor(classId_, hello)) {   // the class is gone from this device
		stop();
		return;
	}
	if (!fallback_) setState(Connecting);
	id_ = hooks_.newId ? hooks_.newId() : 0;
	greeted_ = ended_ = false;
	byeStatus_ = 0;
	byeRetryAfterMs_ = 0;
	hello_ = hello;
	if (!id_ || !hooks_.open || !hooks_.open(id_, client_.socketUrl(classId_), client_.socketHeaders())) {
		// No WebSockets on this platform: the held polls do it all, from now on.
		id_ = 0;
		unsupported_ = true;
		fallback_ = true;
		setState(Fallback);
		return;
	}
	helloBy_ = now + kHelloWait;
}

void LiveLink::opened(int id, int64_t now) {
	(void)now;
	if (!owns(id) || !hooks_.send) return;
	hooks_.send(id, hello_);
}

void LiveLink::text(int id, const std::string& text, int64_t now) {
	if (!owns(id)) return;
	if (text == "pong") {
		alive_ = true;
		return;
	}
	json::Value msg;
	if (!json::parse(text, msg) || !msg.isObject() || msg.str("t").empty()) {
		client_.log.push_back("live connection of " + classId_ + ": a message that isn't one, ignored");
		return;
	}
	const std::string t = msg.str("t");
	if (t == "hello") {
		helloBy_ = 0;
		greeted_ = true;
		failures_ = 0;
		fallback_ = false;
		alive_ = true;
		pingAt_ = now + kPing;
		setState(Open);
	}
	if ((t == "ok" || t == "error") && msg.get("id") && msg.get("id")->isInt()) {
		auto it = pending_.find(msg.integer("id"));
		if (it != pending_.end()) {
			const std::function<void(const json::Value*)> reply = it->second.reply;
			pending_.erase(it);
			if (reply) reply(&msg);
			return;
		}
	}
	if (t == "bye") {
		byeStatus_ = (int)msg.integer("status");
		byeRetryAfterMs_ = std::min<int64_t>(std::max<int64_t>(0, msg.integer("retryAfter")), 3600) * kSecond;
		if (byeStatus_ == 401 || byeStatus_ == 403 || byeStatus_ == 404 || byeStatus_ == 410) ended_ = true;
	}
	client_.socketMessage(classId_, msg);
	if (t == "bye" && owns(id)) {
		// The server closes next; local workerd doesn't always finish that handshake, so the
		// client closes its side itself (3.14).
		if (hooks_.close) hooks_.close(id_, 1000);
		finish(4000 + byeStatus_, now);
	}
}

void LiveLink::closed(int id, int code, int64_t now) {
	if (!owns(id)) return;
	finish(code, now);
}

void LiveLink::closeOwn(int64_t now) {
	if (id_ && hooks_.close) hooks_.close(id_, 1000);
	finish(1006, now);
}

void LiveLink::finish(int code, int64_t now) {
	id_ = 0;
	helloBy_ = pingAt_ = 0;
	failPending();
	const int status = code >= 4000 && code < 5000 ? code - 4000 : 0;
	if (status == 401 || status == 403 || status == 404 || status == 410) {
		if (!byeStatus_) {
			// A closing code without its bye: taken as the bye would have been.
			json::Value bye = json::Value::object();
			bye.set("t", json::Value::string("bye"));
			bye.set("status", json::Value::integer(status));
			bye.set("error", json::Value::string(errorOfCode(status, client_.teaches(classId_))));
			byeStatus_ = status;
			client_.socketMessage(classId_, bye);
		}
		ended_ = true;
	}
	if (ended_) {
		// Removed, deleted, a wrong key: nothing to reconnect to (the page opening again starts over).
		stopped_ = true;
		retryAt_ = 0;
		setState(Closed);
		return;
	}
	if (stopped_) return;
	int64_t atLeast = 0;
	if (status == 400 || status == 413) {
		client_.log.push_back("live connection of " + classId_ + " closed " + std::to_string(code) + ": a bug");
		failures_++;
	} else if (!greeted_) {
		failures_++;
	} else {
		failures_ = std::max(1, failures_);   // a drop after a good connection: again soon (an eviction, a deploy)
	}
	if (status == 429) atLeast = byeRetryAfterMs_ ? byeRetryAfterMs_ : 60 * kSecond;
	retry(now, atLeast);
}

void LiveLink::retry(int64_t now, int64_t atLeastMs) {
	if (stopped_) return;
	if (failures_ >= 3) fallback_ = true;
	setState(fallback_ ? Fallback : Connecting);
	const int64_t base = fallback_ ? kFallbackRetry : kBackoff[std::min(std::max(failures_, 1) - 1, 5)] * kSecond;
	seed_ = seed_ * 1664525u + 1013904223u;
	const double jitter = 0.8 + 0.4 * ((seed_ >> 8) & 0xFFFF) / 65535.0;
	retryAt_ = now + std::max<int64_t>((int64_t)(base * jitter), atLeastMs);
}

void LiveLink::tick(int64_t now) {
	if (retryAt_ && now >= retryAt_) {
		retryAt_ = 0;
		connect(now);
	}
	if (id_ && !greeted_ && helloBy_ && now >= helloBy_) closeOwn(now);   // never said hello: a failed try
	if (isOpen() && pingAt_ && now >= pingAt_) {
		if (!alive_) {
			closeOwn(now);   // the last ping got no pong
		} else {
			alive_ = false;
			if (hooks_.send) hooks_.send(id_, "ping");
			pingAt_ = now + kPing;
		}
	}
	std::vector<std::function<void(const json::Value*)>> late;
	for (auto it = pending_.begin(); it != pending_.end();) {
		if (now >= it->second.until) {
			late.push_back(it->second.reply);
			it = pending_.erase(it);
		} else {
			++it;
		}
	}
	for (const auto& reply : late)
		if (reply) reply(nullptr);
}

int64_t LiveLink::nextTimer() const {
	int64_t t = INT64_MAX;
	if (retryAt_) t = std::min(t, retryAt_);
	if (id_ && !greeted_ && helloBy_) t = std::min(t, helloBy_);
	if (isOpen() && pingAt_) t = std::min(t, pingAt_);
	for (const auto& kv : pending_) t = std::min(t, kv.second.until);
	return t;
}

bool LiveLink::request(json::Value msg, std::function<void(const json::Value*)> reply, int64_t now) {
	if (!isOpen() || !hooks_.send) return false;
	const int64_t id = nextRequest_++;
	msg.set("id", json::Value::integer(id));
	pending_[id] = { std::move(reply), now + kRequestWait };
	hooks_.send(id_, json::write(msg));
	return true;
}

void LiveLink::failPending() {
	std::map<int64_t, Waiting> was;
	was.swap(pending_);
	for (auto& kv : was)
		if (kv.second.reply) kv.second.reply(nullptr);
}

}  // namespace clclass
