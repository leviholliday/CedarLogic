// The self-test's FakeServer: the server's rules (SYNC.md 3) in memory, a
// port of the design's ref/sim.py Server, answering real HTTP-shaped requests
// (JSON bodies, status codes, the Date header) so the engine's own protocol
// code is what talks to it.

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS   // snprintf/sscanf/localtime/getenv are used with care
#endif

#include "SyncTestServer.h"

#include <algorithm>
#include <cstdlib>

namespace clsync {
namespace test {

namespace {

json::Value err(const std::string& code) {
	json::Value o = json::Value::object();
	o.set("error", json::Value::string(code));
	return o;
}

std::string queryParam(const std::string& query, const std::string& key) {
	size_t at = 0;
	while (at <= query.size()) {
		size_t end = query.find('&', at);
		if (end == std::string::npos) end = query.size();
		const std::string part = query.substr(at, end - at);
		if (part.compare(0, key.size() + 1, key + "=") == 0) return part.substr(key.size() + 1);
		at = end + 1;
	}
	return std::string();
}

}  // namespace

FakeServer::FakeServer(Clock& c, Crypto& cr) : clock(c), crypto(cr) {}

std::string FakeServer::sha(const std::string& s) { return sha256Hex(crypto, s); }

json::Value FakeServer::entry(const std::string& id, const Rec& r) {
	json::Value e = json::Value::object();
	e.set("id", json::Value::string(id));
	e.set("ver", json::Value::integer(r.ver));
	e.set("seq", json::Value::integer(r.seq));
	e.set("size", json::Value::integer(r.size));
	e.set("updatedAt", json::Value::integer(r.at));
	e.set("deleted", json::Value::boolean(r.deleted));
	e.set("h", json::Value::string(r.h));
	return e;
}

json::Value FakeServer::status(const Space& sp) {
	json::Value o = json::Value::object();
	o.set("seq", json::Value::integer(sp.seq));
	o.set("purgedSeq", json::Value::integer(sp.purgedSeq));
	o.set("epoch", json::Value::string(sp.epoch));
	o.set("count", json::Value::integer(sp.count));
	o.set("bytes", json::Value::integer(sp.bytes));
	o.set("devices", json::Value::integer(sp.devices));
	return o;
}

FakeServer::Space FakeServer::newSpace() {
	Space sp;
	sp.epoch = randomHex(crypto, 16);
	sp.createdAt = sp.activeAt = clock.now();
	return sp;
}

int FakeServer::spaceFor(const std::string& sid, const std::string& token, Space*& sp, json::Value& out) {
	sp = nullptr;
	auto g = gone.find(sid);
	if (g != gone.end()) { out = err("space_" + g->second); return 410; }
	auto a = auth.find(sid);
	auto s = spaces.find(sid);
	if (a == auth.end() || s == spaces.end()) { out = err("no_space"); return 404; }
	if (sha(token) != a->second.authHash) { out = err("wrong_code"); return 401; }
	sp = &s->second;
	if (clock.now() - sp->activeAt > kDay) sp->activeAt = clock.now();
	return 200;
}

int FakeServer::putSpace(const std::string& sid, const std::string& token, const std::string& deleteHash, json::Value& out) {
	auto g = gone.find(sid);
	if (g != gone.end()) { out = err("space_" + g->second); return 410; }
	auto a = auth.find(sid);
	if (a != auth.end() && sha(token) != a->second.authHash) { out = err("wrong_code"); return 401; }
	if (a == auth.end()) auth[sid] = Auth{ sha(token), deleteHash, clock.now() };
	auto s = spaces.find(sid);
	if (s != spaces.end()) {
		out = status(s->second);
		out.set("created", json::Value::boolean(false));
		return 200;
	}
	spaces[sid] = newSpace();
	out = status(spaces[sid]);
	out.set("created", json::Value::boolean(true));
	return 201;
}

int FakeServer::changes(Space& sp, int64_t since, int64_t limit, json::Value& out) {
	std::vector<std::pair<int64_t, std::string>> order;
	for (const auto& kv : sp.recs)
		if (kv.second.seq > since) order.emplace_back(kv.second.seq, kv.first);
	std::sort(order.begin(), order.end());
	const bool more = (int64_t)order.size() > limit;
	if (more) order.resize((size_t)limit);
	json::Value es = json::Value::array();
	for (const auto& o : order) es.push(entry(o.second, sp.recs[o.second]));
	out = json::Value::object();
	out.set("seq", json::Value::integer(sp.seq));
	out.set("purgedSeq", json::Value::integer(sp.purgedSeq));
	out.set("epoch", json::Value::string(sp.epoch));
	out.set("entries", es);
	out.set("more", json::Value::boolean(more));
	out.set("next", json::Value::integer(more ? order.back().first : sp.seq));
	out.set("pollSeconds", json::Value::integer(600));
	return 200;
}

int FakeServer::fetch(Space& sp, const std::vector<std::string>& ids, json::Value& out) {
	if ((int64_t)ids.size() > limits.fetchIds) { out = err("bad_request"); return 400; }
	json::Value records = json::Value::array(), missing = json::Value::array();
	std::set<std::string> done;
	for (const std::string& id : ids) {
		if (!done.insert(id).second) continue;
		auto r = sp.recs.find(id);
		if (r == sp.recs.end()) {
			missing.push(json::Value::string(id));
			continue;
		}
		json::Value e = entry(id, r->second);
		e.set("data", json::Value::string(r->second.data));
		records.push(e);
	}
	out = json::Value::object();
	out.set("records", records);
	out.set("missing", missing);
	out.set("deferred", json::Value::array());
	return 200;
}

int FakeServer::write(Space& sp, const json::Value& writes, json::Value& out) {
	if (!writes.isArray() || writes.a.empty() || (int64_t)writes.a.size() > limits.writeItems) {
		out = err("bad_request");
		return 400;
	}
	const Limits& L = limits;
	json::Value results = json::Value::array();
	for (const json::Value& w : writes.a) {
		const std::string rid = w.str("id");
		const json::Value *bv = w.get("base"), *vv = w.get("ver");
		const bool deleted = w.flag("deleted");
		const bool device = w.flag("device") && !deleted;
		auto bad = [&](int st, const std::string& code) {
			json::Value r = json::Value::object();
			r.set("id", json::Value::string(rid));
			r.set("status", json::Value::integer(st));
			r.set("error", json::Value::string(code));
			results.push(r);
		};
		if (!isUuid(rid) || !bv || !vv || !bv->isInt() || !vv->isInt() || vv->i() <= bv->i()) { bad(400, "bad_request"); continue; }
		const int64_t base = bv->i(), ver = vv->i();
		Bytes env;
		if (!unb64u(w.str("data"), env) || env.size() < 30 || env[0] != 1 || env[1] > 1) { bad(400, "bad_request"); continue; }
		if ((int64_t)env.size() > (deleted || device ? L.maxTombstone : L.maxEnvelope)) { bad(413, "record_too_large"); continue; }
		const std::string h = envelopeHash(crypto, env);
		auto cur = sp.recs.find(rid);
		const bool has = cur != sp.recs.end();
		if (has && cur->second.ver == ver && cur->second.h == h) {   // the same write again: replay
			json::Value r = json::Value::object();
			r.set("id", json::Value::string(rid));
			r.set("status", json::Value::integer(200));
			r.set("entry", entry(rid, cur->second));
			results.push(r);
			continue;
		}
		auto gh = sp.ghosts.find(rid);
		const int64_t ghost = !has && gh != sp.ghosts.end() ? gh->second : 0;
		const int64_t curVer = has ? cur->second.ver : ghost;
		if (base != curVer) {
			json::Value r = json::Value::object();
			r.set("id", json::Value::string(rid));
			r.set("status", json::Value::integer(412));
			r.set("error", json::Value::string("conflict"));
			if (ghost) {
				json::Value g = json::Value::object();
				g.set("id", json::Value::string(rid));
				g.set("ver", json::Value::integer(ghost));
				g.set("seq", json::Value::integer(0));
				g.set("size", json::Value::integer(0));
				g.set("updatedAt", json::Value::integer(0));
				g.set("deleted", json::Value::boolean(true));
				g.set("h", json::Value::string(""));
				g.set("purged", json::Value::boolean(true));
				r.set("current", g);
			} else {
				r.set("current", has ? entry(rid, cur->second) : json::Value());
			}
			results.push(r);
			continue;
		}
		const char* was = !has || cur->second.deleted ? nullptr : (cur->second.dev ? "dev" : "rec");
		const char* now = deleted ? nullptr : (device ? "dev" : "rec");
		auto is = [](const char* a, const char* b) { return a && std::string(a) == b; };
		const int64_t sizeBefore = is(was, "rec") ? cur->second.size : 0;
		const int64_t count = sp.count + (is(now, "rec") ? 1 : 0) - (is(was, "rec") ? 1 : 0);
		const int64_t devices = sp.devices + (is(now, "dev") ? 1 : 0) - (is(was, "dev") ? 1 : 0);
		const int64_t total = sp.bytes - sizeBefore + (is(now, "rec") ? (int64_t)env.size() : 0);
		const int64_t entries = (int64_t)sp.recs.size() + (has ? 0 : 1);
		const bool grows = now && (!(was && std::string(was) == now) || (is(now, "rec") && (int64_t)env.size() > sizeBefore));
		if (grows && (count > L.maxRecords || total > L.maxBytes || entries > L.maxEntries || devices > L.maxDevices)) {
			json::Value r = json::Value::object();
			r.set("id", json::Value::string(rid));
			r.set("status", json::Value::integer(507));
			r.set("error", json::Value::string("space_full"));
			r.set("count", json::Value::integer(sp.count));
			r.set("bytes", json::Value::integer(sp.bytes));
			results.push(r);
			continue;
		}
		sp.seq += 1;
		sp.ghosts.erase(rid);
		Rec rec;
		rec.ver = ver;
		rec.seq = sp.seq;
		rec.size = (int64_t)env.size();
		rec.at = clock.now();
		rec.deleted = deleted;
		rec.dev = device;
		rec.data = w.str("data");
		rec.h = h;
		sp.recs[rid] = rec;
		sp.count = count;
		sp.bytes = total;
		sp.devices = devices;
		json::Value r = json::Value::object();
		r.set("id", json::Value::string(rid));
		r.set("status", json::Value::integer(has ? 200 : 201));
		r.set("entry", entry(rid, rec));
		results.push(r);
	}
	out = json::Value::object();
	out.set("results", results);
	out.set("seq", json::Value::integer(sp.seq));
	return 200;
}

int FakeServer::deleteSpace(const std::string& sid, const std::string& token, const std::string& deleteToken, json::Value& out) {
	Space* sp = nullptr;
	const int st = spaceFor(sid, token, sp, out);
	if (st != 200) return st;
	if (sha(deleteToken) != auth[sid].deleteHash) { out = err("wrong_delete_token"); return 403; }
	spaces.erase(sid);
	auth.erase(sid);
	gone[sid] = "deleted";
	out = json::Value::object();
	out.set("deleted", json::Value::boolean(true));
	return 200;
}

void FakeServer::cleanup() {
	const int64_t now = clock.now();
	const Limits& L = limits;
	for (auto it = spaces.begin(); it != spaces.end();) {
		Space& sp = it->second;
		if (sp.seq == 0 && now - sp.activeAt > L.emptyDays * kDay) {   // never used: no marker
			auth.erase(it->first);
			it = spaces.erase(it);
			continue;
		}
		if (now - sp.activeAt > L.inactiveDays * kDay) {
			auth.erase(it->first);
			gone[it->first] = "expired";
			it = spaces.erase(it);
			continue;
		}
		std::vector<std::pair<int64_t, std::string>> tombs;
		for (const auto& kv : sp.recs)
			if (kv.second.deleted) tombs.emplace_back(kv.second.at, kv.first);
		std::sort(tombs.begin(), tombs.end());
		const int64_t extra = std::max<int64_t>(0, (int64_t)tombs.size() - L.maxTombstones);
		for (size_t k = 0; k < tombs.size(); k++) {
			if ((int64_t)k < extra || now - tombs[k].first > L.tombstoneDays * kDay) {
				Rec& r = sp.recs[tombs[k].second];
				sp.purgedSeq = std::max(sp.purgedSeq, r.seq);
				sp.ghosts[tombs[k].second] = r.ver;
				sp.recs.erase(tombs[k].second);
			}
		}
		++it;
	}
}

HttpResponse FakeServer::handle(const HttpRequest& req) {
	requests++;
	HttpResponse resp;
	resp.sent = true;
	if (failNext > 0) {   // a request-level failure the test asked for: not acted on
		failNext--;
		json::Value e = err(failStatus == 429 ? "rate_limited" : "busy");
		e.set("retryAfter", json::Value::integer(failRetryAfter));
		resp.status = failStatus;
		resp.body = json::write(e);
		resp.headers["retry-after"] = std::to_string(failRetryAfter);
		resp.headers["date"] = formatHttpDate(clock.now());
		return resp;
	}
	json::Value out = json::Value::object();
	int st = 404;
	std::string url = req.url;
	const size_t api = url.find("/api/sync/v1");
	std::string path = api == std::string::npos ? url : url.substr(api + 12);
	std::string query;
	const size_t qm = path.find('?');
	if (qm != std::string::npos) {
		query = path.substr(qm + 1);
		path = path.substr(0, qm);
	}
	std::string token, deleteToken;
	for (const auto& h : req.headers) {
		std::string n = h.first;
		for (char& c : n) c = (char)tolower((unsigned char)c);
		if (n == "authorization" && h.second.compare(0, 7, "Bearer ") == 0) token = h.second.substr(7);
		if (n == "x-cedarlogic-delete") deleteToken = h.second;
	}
	json::Value body;
	if (!req.body.empty() && !json::parse(req.body, body)) body = json::Value();
	// /spaces/<sid>[/<op>]
	std::vector<std::string> parts;
	for (size_t at = 1; at <= path.size();) {
		size_t end = path.find('/', at);
		if (end == std::string::npos) end = path.size();
		parts.push_back(path.substr(at, end - at));
		at = end + 1;
	}
	if (parts.size() >= 2 && parts[0] == "spaces" && isHex(parts[1], 32)) {
		const std::string sid = parts[1];
		const std::string op = parts.size() > 2 ? parts[2] : "";
		lastOp = op.empty() ? req.method + " space" : op;
		Space* sp = nullptr;
		if (op.empty() && req.method == "PUT") {
			st = putSpace(sid, token, body.str("deleteHash"), out);
		} else if (op.empty() && req.method == "DELETE") {
			st = deleteSpace(sid, token, deleteToken, out);
		} else if ((st = spaceFor(sid, token, sp, out)) == 200) {
			if (op.empty() && req.method == "GET") {
				out = status(*sp);
			} else if (op == "changes" && req.method == "GET") {
				const std::string since = queryParam(query, "since"), limit = queryParam(query, "limit");
				int64_t lim = limit.empty() ? 1000 : atoll(limit.c_str());
				lim = std::min(lim, changesLimit);
				sinces.push_back(since.empty() ? 0 : atoll(since.c_str()));
				st = changes(*sp, since.empty() ? 0 : atoll(since.c_str()), std::max<int64_t>(1, lim), out);
			} else if (op == "fetch" && req.method == "POST") {
				std::vector<std::string> ids;
				if (const json::Value* a = body.get("ids"))
					for (const json::Value& x : a->a) ids.push_back(x.s);
				st = fetch(*sp, ids, out);
			} else if (op == "write" && req.method == "POST") {
				const json::Value* w = body.get("writes");
				st = write(*sp, w ? *w : json::Value(), out);
			} else {
				st = 404;
				out = err("not_found");
			}
		}
	} else {
		out = err("not_found");
	}
	resp.status = st;
	resp.body = json::write(out);
	resp.headers["date"] = formatHttpDate(clock.now());
	resp.headers["content-type"] = "application/json";
	return resp;
}

}  // namespace test
}  // namespace clsync
