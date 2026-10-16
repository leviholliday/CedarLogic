// The FakeServer: CLASSROOM.md 3.3-3.8 in memory, for the scenarios of 7.2.
// It keeps what the real one keeps (hashes and HMACs of tokens, opaque
// envelopes, counters) and answers with the same paths, bodies, status codes
// and error codes. Limits are the design's, a few made small by the tests.

#include "ClassroomTest.h"

#include <algorithm>
#include <cstring>

namespace clclass {
namespace test {

using clsync::b64u;
using clsync::hex;
using clsync::isHex;
using clsync::isUuid;
using clsync::sha256Hex;
using clsync::unb64u;

namespace {

HttpResponse reply(int64_t now, int status, const json::Value& body, const std::string& etag = std::string()) {
	HttpResponse r;
	r.sent = true;
	r.status = status;
	r.headers["date"] = clsync::formatHttpDate(now);
	if (!etag.empty()) r.headers["etag"] = etag;
	if (status != 304) r.body = json::write(body);
	return r;
}

HttpResponse error(int64_t now, int status, const std::string& code, int retryAfter = 0) {
	json::Value b = json::Value::object();
	b.set("error", json::Value::string(code));
	b.set("message", json::Value::string(code));   // the clients use their own sentences (3.4)
	if (retryAfter) b.set("retryAfter", json::Value::integer(retryAfter));
	HttpResponse r = reply(now, status, b);
	if (retryAfter) r.headers["retry-after"] = std::to_string(retryAfter);
	return r;
}

std::string header(const HttpRequest& r, const std::string& name) {
	for (const auto& h : r.headers) {
		std::string n = h.first;
		for (char& c : n) c = (char)tolower((unsigned char)c);
		if (n == name) return h.second;
	}
	return std::string();
}

std::string bearer(const HttpRequest& r) {
	const std::string a = header(r, "authorization");
	return a.compare(0, 7, "Bearer ") == 0 ? a.substr(7) : std::string();
}

std::vector<std::string> split(const std::string& path) {
	std::vector<std::string> out;
	size_t at = 1;
	while (at <= path.size()) {
		size_t end = path.find('/', at);
		if (end == std::string::npos) end = path.size();
		out.push_back(path.substr(at, end - at));
		at = end + 1;
	}
	return out;
}

int64_t envSize(const std::string& b) {
	Bytes x;
	return unb64u(b, x) ? (int64_t)x.size() : -1;
}

}  // namespace

FakeServer::FakeServer(Crypto& c) : cr(c) {
	pepper.resize(32);
	for (int i = 0; i < 32; i++) pepper[(size_t)i] = (uint8_t)(0xe0 + i);   // the test pepper of 7.1.3
}

std::string FakeServer::pepperHex() const { return hex(pepper); }

std::string FakeServer::newFetchKey() {
	return clsync::randomHex(cr, 16);
}

bool FakeServer::bump(const std::string& key, int limit) { return ++counters[key] <= limit; }

std::string FakeServer::fetchKeyOf(const std::string& classId) const {
	auto it = classes.find(classId);
	return it == classes.end() ? std::string() : it->second.fetchKey;
}

HttpResponse FakeServer::handle(const HttpRequest& r) {
	requests++;
	const std::string wait = header(r, "x-cedarlogic-wait");
	if (!wait.empty()) waits.push_back(r.url + " wait " + wait + (header(r, "if-none-match").empty() ? " (no If-None-Match)" : ""));
	if (offline > 0) {
		offline--;
		return HttpResponse();   // no answer
	}
	const std::string b = base(), lb = liveBase();
	if (r.url.compare(0, b.size(), b) == 0) return api(r, r.url.substr(b.size()));
	if (r.url.compare(0, lb.size(), lb) == 0) return pulse(r, r.url.substr(lb.size()));
	return error(now, 404, "not_found");
}

// ---- /api/classroom/v1 ----------------------------------------------------------------

HttpResponse FakeServer::api(const HttpRequest& r, const std::string& full) {
	std::string path = full, query;
	const size_t qm = full.find('?');
	if (qm != std::string::npos) {
		path = full.substr(0, qm);
		query = full.substr(qm + 1);
	}
	const std::vector<std::string> p = split(path);
	const std::string& m = r.method;
	json::Value body;
	if (!r.body.empty() && (!json::parse(r.body, body) || !body.isObject())) return error(now, 400, "bad_request");
	const bool write = m == "PUT" || m == "POST" || m == "DELETE";
	if (write && closed) return error(now, 503, "busy", 30);
	if (write && paused && !(m == "DELETE" && p.size() == 2 && p[0] == "classes")) return error(now, 503, "classroom_paused", 60);
	if (write && retryAfterNext) {
		const int ra = retryAfterNext;
		retryAfterNext = 0;
		return error(now, 429, "rate_limited", ra);
	}
	const std::string token = bearer(r);
	const std::string ipHour = ip + "/" + std::to_string(now / kHour);
	auto failedLookup = [&]() { return !bump("fail/" + ipHour, 60); };

	if (p.size() == 1 && p[0] == "health" && m == "GET") {
		json::Value o = json::Value::object();
		o.set("ok", json::Value::boolean(true));
		o.set("protocol", json::Value::integer(1));
		return reply(now, 200, o);
	}

	// ---- joining by code ----
	if (p.size() == 2 && p[0] == "join" && m == "GET") {
		if (!isHex(p[1], 32)) return error(now, 404, "not_found");
		const auto j = joins.find(hmacHex(cr, pepper, p[1]));
		if (j == joins.end()) return failedLookup() ? error(now, 429, "rate_limited", 3600) : error(now, 404, "no_class");
		if (gone.count(j->second.classId)) return error(now, 410, gone[j->second.classId] == "expired" ? "class_expired" : "class_deleted");
		if (hmacHex(cr, pepper, token) != j->second.joinHash) return failedLookup() ? error(now, 429, "rate_limited", 3600) : error(now, 401, "wrong_token");
		json::Value o = json::Value::object();
		o.set("classId", json::Value::string(j->second.classId));
		o.set("ver", json::Value::integer(j->second.ver));
		o.set("env", json::Value::string(j->second.env));
		o.set("open", json::Value::boolean(j->second.open));
		return reply(now, 200, o);
	}

	// ---- the move slot ----
	if (p.size() == 2 && p[0] == "move") {
		const std::string& moveId = p[1];
		if (!isHex(moveId, 32)) return error(now, 404, "not_found");
		auto mv = moves.find(moveId);
		const bool live = mv != moves.end() && now < mv->second.createdAt + 600000;
		if (m == "GET") {
			if (!live) return error(now, 404, "move_gone");
			json::Value o = json::Value::object();
			o.set("classId", json::Value::string(mv->second.classId));
			o.set("env", json::Value::string(mv->second.env));
			o.set("expiresAt", json::Value::integer(mv->second.createdAt + 600000));
			return reply(now, 200, o);
		}
		const std::string classId = m == "PUT" ? body.str("classId") : (mv != moves.end() ? mv->second.classId : std::string());
		auto c = classes.find(classId);
		const std::string sid = header(r, "x-cedarlogic-student");
		const bool student = c != classes.end() && c->second.roster.count(sid) && c->second.roster[sid].hash == sha256Hex(cr, token);
		if (m == "PUT") {
			if (!student || body.str("studentId") != sid) return error(now, 401, "wrong_token");
			if (live) return error(now, 409, "move_exists");
			if (!bump("move/" + sid + "/" + std::to_string(now / kHour), 10)) return error(now, 429, "rate_limited", 3600);
			moves[moveId] = { classId, sid, body.str("env"), now };
			json::Value o = json::Value::object();
			o.set("expiresAt", json::Value::integer(now + 600000));
			return reply(now, 201, o);
		}
		if (m == "DELETE") {
			if (mv == moves.end()) return reply(now, 200, json::Value::object());
			if (live && !(student && mv->second.studentId == sid)) return error(now, 401, "wrong_token");
			moves.erase(moveId);
			return reply(now, 200, json::Value::object());
		}
		return error(now, 405, "method_not_allowed");
	}

	// ---- class passes (3.17): the redemption ----
	if (p.size() == 2 && p[0] == "pass") {
		if (!isHex(p[1], 32)) return error(now, 404, "not_found");
		if (m != "GET") return error(now, 405, "method_not_allowed");
		auto ps = passes.find(sha256Hex(cr, p[1]));
		if (ps == passes.end()) return failedLookup() ? error(now, 429, "rate_limited", 3600) : error(now, 404, "pass_gone");
		const std::string cid = ps->second.classId;
		if (gone.count(cid)) {
			passes.erase(ps);
			return error(now, 410, gone[cid] == "expired" ? "class_expired" : "class_deleted");
		}
		if (sha256Hex(cr, token) != ps->second.hash) return failedLookup() ? error(now, 429, "rate_limited", 3600) : error(now, 401, "wrong_token");
		auto c = classes.find(cid);
		if (c == classes.end() || !c->second.roster.count(ps->second.studentId)) {
			passes.erase(ps);
			return error(now, 404, "pass_gone");
		}
		ps->second.usedAt = now;
		ps->second.uses++;
		json::Value o = json::Value::object();
		o.set("classId", json::Value::string(cid));
		o.set("studentId", json::Value::string(ps->second.studentId));
		o.set("env", json::Value::string(ps->second.env));
		o.set("createdAt", json::Value::integer(ps->second.createdAt));
		return reply(now, 200, o);
	}

	if (p.size() < 2 || p[0] != "classes" || !isHex(p[1], 32)) return error(now, 404, "not_found");
	const std::string classId = p[1];

	// ---- class passes (3.17): make, list, cancel ----
	if (p.size() >= 3 && p[2] == "passes") {
		if (gone.count(classId)) return error(now, 410, gone[classId] == "expired" ? "class_expired" : "class_deleted");
		auto c = classes.find(classId);
		if (c == classes.end()) return error(now, 404, "no_class");
		const std::string sid = header(r, "x-cedarlogic-student");
		const bool teacher = sid.empty() && c->second.teacherHash == sha256Hex(cr, token);
		if (!sid.empty() && !c->second.roster.count(sid)) return error(now, 403, "not_a_member");
		const bool student = !sid.empty() && c->second.roster[sid].hash == sha256Hex(cr, token);
		if (!teacher && !student) return error(now, 401, sid.empty() ? "wrong_key" : "wrong_token");
		if (p.size() == 3 && m == "POST") {
			if (!student || body.str("studentId") != sid) return error(now, 403, "forbidden");
			const std::string passId = body.str("passId"), th = body.str("tokenHash");
			if (!isHex(passId, 32) || !isHex(th, 64) || body.str("env").empty()) return error(now, 400, "bad_request");
			const std::string pid = sha256Hex(cr, passId);
			if (passes.count(pid)) return error(now, 409, "pass_exists");
			int mine = 0;
			for (const auto& x : passes) mine += x.second.classId == classId && x.second.studentId == sid;
			if (mine >= 5) return error(now, 409, "too_many_passes");
			passes[pid] = { classId, sid, th, body.str("env"), now, 0, 0 };
			json::Value o = json::Value::object();
			o.set("pid", json::Value::string(pid));
			o.set("createdAt", json::Value::integer(now));
			return reply(now, 201, o);
		}
		if (p.size() == 3 && m == "GET") {
			json::Value list = json::Value::array();
			for (const auto& x : passes) {
				if (x.second.classId != classId || (student && x.second.studentId != sid)) continue;
				json::Value e = json::Value::object();
				e.set("pid", json::Value::string(x.first));
				e.set("studentId", json::Value::string(x.second.studentId));
				e.set("createdAt", json::Value::integer(x.second.createdAt));
				e.set("usedAt", x.second.usedAt ? json::Value::integer(x.second.usedAt) : json::Value());
				e.set("uses", json::Value::integer(x.second.uses));
				list.push(e);
			}
			json::Value o = json::Value::object();
			o.set("passes", list);
			return reply(now, 200, o);
		}
		if (p.size() == 4 && m == "DELETE") {
			auto ps = passes.find(p[3]);
			json::Value o = json::Value::object();
			if (ps == passes.end() || ps->second.classId != classId) { o.set("cancelled", json::Value::boolean(false)); return reply(now, 200, o); }
			if (student && ps->second.studentId != sid) return error(now, 403, "forbidden");
			passes.erase(ps);
			o.set("cancelled", json::Value::boolean(true));
			return reply(now, 200, o);
		}
		return error(now, 405, "method_not_allowed");
	}

	// ---- create, re-create or confirm (3.3) ----
	if (p.size() == 2 && m == "PUT") {
		if (gone.count(classId)) return error(now, 410, gone[classId] == "expired" ? "class_expired" : "class_deleted");
		auto it = classes.find(classId);
		if (it != classes.end() && it->second.teacherHash != sha256Hex(cr, token)) return error(now, 401, "wrong_key");
		const bool created = it == classes.end();
		if (created) {
			const json::Value *t = body.get("teacher"), *j = body.get("join"), *i = body.get("info");
			if (!t || !j || !i || !isHex(body.str("deleteHash"), 64) || !isHex(j->str("joinId"), 32) || j->str("joinToken").size() != 43)
				return error(now, 400, "bad_request");
			for (const json::Value* e : { t, j, i }) {
				Bytes env;
				if (!unb64u(e->str("env"), env) || env.size() < 30 || env.size() > 4096 || env[0] != 1 || env[1] > 1)
					return error(now, 400, "bad_request");
			}
			if (!bump("create/" + ip + "/" + std::to_string(now / kDay), 10)) return error(now, 503, "classroom_busy");
			const std::string index = hmacHex(cr, pepper, j->str("joinId"));
			if (joins.count(index)) return error(now, 409, "join_exists");
			Klass k;
			k.teacherHash = sha256Hex(cr, token);
			k.deleteHash = body.str("deleteHash");
			k.createdAt = k.activeAt = now;
			k.fetchKey = newFetchKey();
			k.joinIndex = index;
			Bytes je, te, ie;
			unb64u(j->str("env"), je);
			unb64u(t->str("env"), te);
			unb64u(i->str("env"), ie);
			k.join = { j->integer("ver"), (int64_t)je.size(), now, clsync::envelopeHash(cr, je), j->str("env") };
			k.joinOpen = j->flag("open", true);
			k.teacher = { t->integer("ver"), (int64_t)te.size(), now, clsync::envelopeHash(cr, te), t->str("env") };
			k.info = { i->integer("ver"), (int64_t)ie.size(), now, clsync::envelopeHash(cr, ie), i->str("env") };
			joins[index] = { classId, hmacHex(cr, pepper, j->str("joinToken")), j->str("env"), k.joinOpen, k.join.ver, now };
			classes[classId] = k;
			it = classes.find(classId);
		}
		HttpRequest status = r;
		status.method = "GET";
		HttpResponse s = api(status, "/classes/" + classId);
		json::Value o;
		json::parse(s.body, o);
		o.set("created", json::Value::boolean(created));
		return reply(now, created ? 201 : 200, o);
	}

	if (gone.count(classId)) return error(now, 410, gone[classId] == "expired" ? "class_expired" : "class_deleted");

	// ---- joining (3.3) ----
	if (p.size() == 3 && p[2] == "students" && m == "POST") {
		auto it = classes.find(classId);
		if (it == classes.end()) return error(now, 404, "no_class");
		Klass& k = it->second;
		const auto j = joins.find(k.joinIndex);
		if (j == joins.end() || j->second.joinHash != hmacHex(cr, pepper, token)) return failedLookup() ? error(now, 429, "rate_limited", 3600) : error(now, 401, "wrong_token");
		if (!k.joinOpen) return error(now, 403, "join_closed");
		if (!bump("joins/" + ip + "/" + classId + "/" + std::to_string(now / kDay), 60)) return error(now, 429, "rate_limited", 3600);
		if ((int)k.roster.size() >= maxStudents) return error(now, 507, "class_full");
		const std::string sid = body.str("studentId");
		const json::Value* name = body.get("name");
		if (!isUuid(sid) || !isHex(body.str("tokenHash"), 64) || !name) return error(now, 400, "bad_request");
		if (k.roster.count(sid)) return error(now, 409, "student_exists");
		const int64_t size = envSize(name->str("env"));
		if (size > 640) return error(now, 413, "record_too_large");
		Member mb;
		mb.hash = body.str("tokenHash");
		mb.joinedAt = mb.seenAt = now;
		mb.name = { name->integer("ver"), size, now, "", name->str("env") };
		k.roster[sid] = mb;
		json::Value o = json::Value::object();
		o.set("joinedAt", json::Value::integer(now));
		o.set("fetchKey", json::Value::string(k.fetchKey));
		return reply(now, 201, o);
	}

	// ---- who is asking (3.8) ----
	auto it = classes.find(classId);
	if (it == classes.end()) return failedLookup() ? error(now, 429, "rate_limited", 3600) : error(now, 404, "no_class");
	Klass& k = it->second;
	const std::string sidHeader = header(r, "x-cedarlogic-student");
	bool teacher = false, student = false;
	if (!sidHeader.empty()) {
		auto rs = k.roster.find(sidHeader);
		if (rs == k.roster.end()) return error(now, 403, "not_a_member");
		if (rs->second.hash != sha256Hex(cr, token)) return failedLookup() ? error(now, 429, "rate_limited", 3600) : error(now, 401, "wrong_token");
		student = true;
		if (now - rs->second.seenAt > kDay) rs->second.seenAt = now;
	} else {
		if (k.teacherHash != sha256Hex(cr, token)) return failedLookup() ? error(now, 429, "rate_limited", 3600) : error(now, 401, "wrong_key");
		teacher = true;
	}
	if (now - k.activeAt > kDay) k.activeAt = now;   // a class students still use is active
	releaseDue(k);

	auto recJson = [](const Rec& x, bool env) {
		json::Value o = json::Value::object();
		o.set("ver", json::Value::integer(x.ver));
		o.set("size", json::Value::integer(x.size));
		o.set("at", json::Value::integer(x.at));
		o.set("h", json::Value::string(x.h));
		if (env) o.set("env", json::Value::string(x.env));
		return o;
	};
	auto store = [&](const std::string& env) {
		Rec x;
		Bytes b;
		unb64u(env, b);
		x.size = (int64_t)b.size();
		x.at = now;
		x.h = clsync::envelopeHash(cr, b);
		x.env = env;
		return x;
	};
	auto conflict = [&](const Rec& cur) {
		json::Value o = json::Value::object();
		o.set("error", json::Value::string("conflict"));
		o.set("message", json::Value::string("conflict"));
		o.set("current", recJson(cur, false));
		return reply(now, 412, o);
	};
	auto itemEntry = [&](const ItemRow& x) {
		json::Value e = recJson(x.rec, false);
		e.set("hidden", json::Value::boolean(x.hidden));
		e.set("releasedAt", x.releasedAt ? json::Value::integer(x.releasedAt) : json::Value());
		e.set("releaseAt", x.releaseAt ? json::Value::integer(x.releaseAt) : json::Value());
		return e;
	};
	auto purge = [&](const std::string& tag) { purges.push_back(tag); };
	auto newKey = [&]() {
		k.fetchKey = newFetchKey();
		k.seq++;
		purge("class-" + classId);
	};

	// ---- the status ----
	if (p.size() == 2 && m == "GET") {
		const std::string etag = "\"" + std::to_string(k.seq) + "\"";
		if (header(r, "if-none-match") == etag) return reply(now, 304, json::Value(), etag);
		json::Value o = json::Value::object();
		o.set("seq", json::Value::integer(k.seq));
		o.set("fetchKey", json::Value::string(k.fetchKey));
		o.set("expiresAt", json::Value::integer(k.activeAt + 400 * kDay));
		json::Value info = json::Value::object();
		info.set("ver", json::Value::integer(k.info.ver));
		info.set("h", json::Value::string(k.info.h));
		o.set("info", info);
		json::Value as = json::Value::object();
		for (const auto& a : k.assignments) {
			json::Value e = recJson(a.second, false);
			const int64_t c = k.closesAt[a.first];
			e.set("closesAt", c >= 0 ? json::Value::integer(c) : json::Value());
			as.set(a.first, e);
		}
		o.set("assignments", as);
		json::Value is = json::Value::object();   // v2: the teacher every item, a student the released ones
		for (const auto& kv : k.items)
			if (teacher || !kv.second.hidden) is.set(kv.first, itemEntry(kv.second));
		o.set("items", is);
		json::Value lv = json::Value::object();
		lv.set("ver", json::Value::integer(k.live.rec.ver));
		lv.set("session", json::Value::string(k.live.session));
		lv.set("on", json::Value::boolean(k.live.on));
		lv.set("predict", json::Value::boolean(k.live.predict));
		lv.set("at", json::Value::integer(k.live.rec.at));
		o.set("live", lv);
		json::Value limits = json::Value::object();   // revision 3's (3.3): the clients read these two
		limits.set("pulseSeconds", json::Value::integer(10));
		limits.set("holdSeconds", json::Value::integer(25));
		o.set("limits", limits);
		if (teacher) {
			o.set("createdAt", json::Value::integer(k.createdAt));
			o.set("activeAt", json::Value::integer(k.activeAt));
			json::Value j = json::Value::object();
			j.set("ver", json::Value::integer(k.join.ver));
			j.set("h", json::Value::string(k.join.h));
			j.set("open", json::Value::boolean(k.joinOpen));
			j.set("at", json::Value::integer(k.join.at));
			o.set("join", j);
			o.set("teacher", recJson(k.teacher, true));
			o.set("students", json::Value::integer((int64_t)k.roster.size()));
			o.set("bytes", json::Value::integer(k.bytes));
		}
		return reply(now, 200, o, etag);
	}

	if (p.size() == 2 && m == "DELETE") {
		if (!teacher) return error(now, 401, "wrong_key");
		if (sha256Hex(cr, header(r, "x-cedarlogic-delete")) != k.deleteHash) return error(now, 403, "wrong_delete_token");
		gone[classId] = "deleted";
		joins.erase(k.joinIndex);
		purge("class-" + classId);
		classes.erase(classId);
		json::Value o = json::Value::object();
		o.set("deleted", json::Value::boolean(true));
		return reply(now, 200, o);
	}

	// ---- the teacher's own records ----
	if (p.size() == 3 && (p[2] == "teacher" || p[2] == "info") && m == "PUT") {
		if (!teacher) return error(now, 401, "wrong_key");
		Rec& cur = p[2] == "teacher" ? k.teacher : k.info;
		if (body.integer("base", -1) != cur.ver) return conflict(cur);
		cur = store(body.str("env"));
		cur.ver = body.integer("ver");
		k.seq++;
		return reply(now, 200, recJson(cur, false));
	}
	if (p.size() == 3 && p[2] == "join" && m == "PUT") {
		if (!teacher) return error(now, 401, "wrong_key");
		const json::Value* t = body.get("teacher");
		if (!t || !isHex(body.str("joinId"), 32)) return error(now, 400, "bad_request");
		if (t->integer("base", -1) != k.teacher.ver) return conflict(k.teacher);
		const std::string index = hmacHex(cr, pepper, body.str("joinId"));
		const bool changed = index != k.joinIndex;
		if (changed && joins.count(index)) return error(now, 409, "join_exists");
		Rec je = store(body.str("env"));
		je.ver = body.integer("ver");
		joins[index] = { classId, hmacHex(cr, pepper, body.str("joinToken")), body.str("env"), body.flag("open", true), je.ver, now };
		k.teacher = store(t->str("env"));
		k.teacher.ver = t->integer("ver");
		k.join = je;
		k.joinOpen = body.flag("open", true);
		k.seq++;
		if (changed) {
			joins.erase(k.joinIndex);
			k.joinIndex = index;
			newKey();   // the old code may be the leak (3.3)
		}
		purge("class-" + classId);
		json::Value o = json::Value::object();
		o.set("fetchKey", json::Value::string(k.fetchKey));
		o.set("seq", json::Value::integer(k.seq));
		return reply(now, 200, o);
	}

	// ---- items (3.16.2), scheduled release (3.16.10) ----
	if (p.size() == 4 && p[2] == "items" && (m == "PUT" || m == "DELETE")) {
		const std::string iid = p[3];
		if (!isUuid(iid)) return error(now, 404, "not_found");
		if (!teacher) return error(now, 401, "wrong_key");
		auto a = k.items.find(iid);
		if (m == "DELETE") {
			if (a != k.items.end()) {
				k.items.erase(a);
				k.seq++;
			}
			json::Value o = json::Value::object();
			o.set("deleted", json::Value::boolean(true));
			return reply(now, 200, o);
		}
		const bool fresh = a == k.items.end();
		const int64_t cur = fresh ? 0 : a->second.rec.ver;
		Bytes env;
		unb64u(body.str("env"), env);
		if (!fresh && body.integer("ver") == cur && clsync::envelopeHash(cr, env) == a->second.rec.h)
			return reply(now, 200, itemEntry(a->second));   // a replay
		if (body.integer("base", -1) != cur) {
			json::Value o = json::Value::object();
			o.set("error", json::Value::string("conflict"));
			o.set("message", json::Value::string("conflict"));
			o.set("current", fresh ? json::Value() : itemEntry(a->second));
			return reply(now, 412, o);
		}
		const json::Value* h = body.get("hidden");
		if (h && !h->isBool()) return error(now, 400, "bad_request");
		bool hidden = h && h->b;
		const json::Value* ra = body.get("releaseAt");
		if (ra && !ra->isNull() && !(ra->isInt() && ra->i() > 0)) return error(now, 400, "bad_request");
		if (ra && ra->isInt() && ra->i() > now + 400 * kDay) return error(now, 400, "bad_request");
		if (env.size() > 524288) return error(now, 413, "record_too_large");
		if (fresh && k.items.size() >= 300) return error(now, 507, "too_many_items");
		ItemRow x = fresh ? ItemRow() : a->second;
		const bool wasHidden = fresh || x.hidden;
		x.rec = store(body.str("env"));
		x.rec.ver = body.integer("ver");
		if (ra && ra->isInt() && ra->i() > now) {   // scheduled: hidden until then
			hidden = true;
			x.releaseAt = ra->i();
		} else if (ra && ra->isInt()) {             // a time already past: now
			hidden = false;
			x.releaseAt = 0;
		} else if (ra) {                            // null: cancel, hidden as sent
			x.releaseAt = 0;
		} else if (!hidden) {                       // absent + visible (an older client's Release)
			x.releaseAt = 0;
		}                                           // absent + hidden: the schedule stays
		x.hidden = hidden;
		if (hidden) x.releasedAt = 0;
		else if (wasHidden) x.releasedAt = now;
		k.items[iid] = x;
		k.seq++;
		json::Value o = itemEntry(x);
		o.set("seq", json::Value::integer(k.seq));
		return reply(now, fresh ? 201 : 200, o);
	}

	// ---- assignments ----
	if (p.size() >= 4 && p[2] == "assignments") {
		const std::string aid = p[3];
		if (!isUuid(aid)) return error(now, 404, "not_found");
		auto a = k.assignments.find(aid);
		if (p.size() == 4 && m == "PUT") {
			if (!teacher) return error(now, 401, "wrong_key");
			const int64_t cur = a == k.assignments.end() ? 0 : a->second.ver;
			Bytes env;
			unb64u(body.str("env"), env);
			if (a != k.assignments.end() && body.integer("ver") == cur && clsync::envelopeHash(cr, env) == a->second.h)
				return reply(now, 200, recJson(a->second, false));   // a replay
			if (body.integer("base", -1) != cur) return conflict(a == k.assignments.end() ? Rec() : a->second);
			if (env.size() > 524288) return error(now, 413, "record_too_large");
			if (a == k.assignments.end() && k.assignments.size() >= 100) return error(now, 507, "too_many_assignments");
			Rec x = store(body.str("env"));
			x.ver = body.integer("ver");
			const json::Value* c = body.get("closesAt");
			k.closesAt[aid] = c && c->isInt() ? c->i() : -1;
			const bool fresh = a == k.assignments.end();
			k.assignments[aid] = x;
			k.seq++;
			return reply(now, fresh ? 201 : 200, recJson(x, false));
		}
		if (p.size() == 4 && m == "DELETE") {
			if (!teacher) return error(now, 401, "wrong_key");
			k.assignments.erase(aid);
			k.closesAt.erase(aid);
			k.subs.erase(aid);
			k.seq++;
			purge("asg-" + aid);
			return reply(now, 200, json::Value::object());
		}
		if (a == k.assignments.end()) return error(now, 404, "no_assignment");
		auto& subs = k.subs[aid];
		if (p.size() == 5 && p[4] == "submissions" && m == "GET") {
			if (!teacher) return error(now, 401, "wrong_key");
			json::Value o = json::Value::object(), s = json::Value::object();
			std::string lines;
			for (const auto& kv : subs) {
				json::Value e = recJson(kv.second.rec, false);
				e.set("attempts", json::Value::integer(kv.second.attempts));
				e.set("firstAt", json::Value::integer(kv.second.firstAt));
				s.set(kv.first, e);
				lines += kv.first + ":" + std::to_string(kv.second.rec.ver) + ":" + kv.second.rec.h + "\n";
			}
			o.set("subs", s);
			const std::string etag = "\"" + sha256Hex(cr, lines).substr(0, 32) + "\"";
			if (header(r, "if-none-match") == etag) return reply(now, 304, json::Value(), etag);
			return reply(now, 200, o, etag);
		}
		if (p.size() == 6 && p[4] == "submissions" && p[5] == "fetch" && m == "POST") {
			if (!teacher) return error(now, 401, "wrong_key");
			json::Value o = json::Value::object(), recs = json::Value::array(), missing = json::Value::array();
			if (const json::Value* ids = body.get("ids"))
				for (const json::Value& id : ids->a) {
					auto s = subs.find(id.s);
					if (s == subs.end()) {
						missing.push(json::Value::string(id.s));
						continue;
					}
					json::Value e = recJson(s->second.rec, true);
					e.set("studentId", json::Value::string(id.s));
					e.set("attempts", json::Value::integer(s->second.attempts));
					recs.push(e);
				}
			o.set("records", recs);
			o.set("missing", missing);
			o.set("deferred", json::Value::array());
			return reply(now, 200, o);
		}
		if (p.size() == 6 && p[4] == "submissions") {
			const std::string sid = p[5];
			if (m == "GET") {
				if (!teacher && sid != sidHeader) return error(now, 403, "forbidden");
				auto s = subs.find(sid);
				if (s == subs.end()) return error(now, 404, "not_found");
				return reply(now, 200, recJson(s->second.rec, true));
			}
			if (m == "PUT") {
				if (!student || sid != sidHeader) return error(now, 401, "wrong_token");
				const int64_t c = k.closesAt[aid];
				if (c >= 0 && now > c) return error(now, 409, "assignment_closed");
				Bytes env;
				if (!unb64u(body.str("env"), env) || env.empty() || env[0] != 2) return error(now, 400, "bad_request");
				if (env.size() > 524288) return error(now, 413, "record_too_large");
				if (!bump("handins/" + sid + "/" + std::to_string(now / kHour), 60)) return error(now, 429, "rate_limited", 3600);
				auto s = subs.find(sid);
				const int64_t cur = s == subs.end() ? 0 : s->second.rec.ver;
				if (s != subs.end() && body.integer("ver") == cur && clsync::envelopeHash(cr, env) == s->second.rec.h)
					return reply(now, 200, recJson(s->second.rec, false));
				if (body.integer("base", -1) != cur) {
					json::Value o = json::Value::object();
					o.set("error", json::Value::string("conflict"));
					json::Value cj = recJson(s == subs.end() ? Rec() : s->second.rec, false);
					cj.set("attempts", json::Value::integer(s == subs.end() ? 0 : s->second.attempts));
					o.set("current", cj);
					return reply(now, 412, o);
				}
				const bool first = s == subs.end();
				Sub& sub = subs[sid];
				sub.rec = store(body.str("env"));
				sub.rec.ver = body.integer("ver");
				sub.attempts++;
				if (first) sub.firstAt = now;
				json::Value o = recJson(sub.rec, false);
				o.set("attempts", json::Value::integer(sub.attempts));
				return reply(now, first ? 201 : 200, o);
			}
		}
		return error(now, 405, "method_not_allowed");
	}

	// ---- students ----
	if (p.size() == 3 && p[2] == "students" && m == "GET") {
		if (!teacher) return error(now, 401, "wrong_key");
		json::Value o = json::Value::object(), list = json::Value::array();
		for (const auto& kv : k.roster) {
			json::Value s = json::Value::object();
			s.set("studentId", json::Value::string(kv.first));
			s.set("joinedAt", json::Value::integer(kv.second.joinedAt));
			s.set("seenAt", json::Value::integer(kv.second.seenAt));
			json::Value n = json::Value::object();
			n.set("ver", json::Value::integer(kv.second.name.ver));
			n.set("env", json::Value::string(kv.second.name.env));
			s.set("name", n);
			list.push(s);
		}
		o.set("students", list);
		return reply(now, 200, o);
	}
	auto removeOne = [&](const std::string& sid, bool deleteSubs) {
		k.roster.erase(sid);
		for (auto& session : k.answers) session.second.erase(sid);
		if (deleteSubs)
			for (auto& a : k.subs) a.second.erase(sid);
	};
	if (p.size() == 4 && p[2] == "students" && p[3] == "remove" && m == "POST") {
		if (!teacher) return error(now, 401, "wrong_key");
		const bool del = body.str("submissions") == "delete";
		if (const json::Value* ids = body.get("ids"))
			for (const json::Value& id : ids->a) removeOne(id.s, del);
		newKey();
		json::Value o = json::Value::object();
		o.set("removed", json::Value::boolean(true));
		return reply(now, 200, o);
	}
	if (p.size() == 4 && p[2] == "students") {
		const std::string sid = p[3];
		if (m == "DELETE") {
			if (student && sid != sidHeader) return error(now, 403, "forbidden");
			removeOne(sid, teacher && query == "submissions=delete");
			newKey();
			json::Value o = json::Value::object();
			o.set(teacher ? "removed" : "left", json::Value::boolean(true));
			return reply(now, 200, o);
		}
		if (m == "PUT") {
			if (!student || sid != sidHeader) return error(now, 401, "wrong_token");
			if (!bump("rename/" + sid + "/" + std::to_string(now / kHour), 5)) return error(now, 429, "rate_limited", 3600);
			const json::Value* name = body.get("name");
			if (!name) return error(now, 400, "bad_request");
			const int64_t size = envSize(name->str("env"));
			if (size > 640) return error(now, 413, "record_too_large");
			k.roster[sid].name = { name->integer("ver"), size, now, "", name->str("env") };
			return reply(now, 200, json::Value::object());
		}
	}

	// ---- the live session ----
	if (p.size() == 3 && p[2] == "live" && m == "PUT") {
		if (!teacher) return error(now, 401, "wrong_key");
		if (body.integer("base", -1) != k.live.rec.ver) {
			json::Value o = json::Value::object();
			o.set("error", json::Value::string("conflict"));
			json::Value c = recJson(k.live.rec, false);
			c.set("session", json::Value::string(k.live.session));
			o.set("current", c);
			return reply(now, 412, o);
		}
		Rec x = store(body.str("env"));
		x.ver = body.integer("ver");
		if (x.size > 524288) return error(now, 413, "record_too_large");
		if (body.str("session") != k.live.session) k.answers.clear();   // answers of other sessions go (the cleanup's job)
		k.live.rec = x;
		k.live.session = body.str("session");
		k.live.on = body.flag("on");
		k.live.predict = body.flag("predict");
		k.seq++;
		return reply(now, 200, recJson(x, false));
	}
	if (p.size() == 4 && p[2] == "live" && p[3] == "answers" && m == "GET") {
		if (!teacher) return error(now, 401, "wrong_key");
		const std::string session = query.compare(0, 8, "session=") == 0 ? query.substr(8) : std::string();
		json::Value o = json::Value::object(), list = json::Value::array();
		std::string lines;
		for (const auto& kv : k.answers[session]) {
			json::Value a = json::Value::object();
			a.set("studentId", json::Value::string(kv.first));
			a.set("ver", json::Value::integer(kv.second.ver));
			a.set("h", json::Value::string(kv.second.h));
			a.set("env", json::Value::string(kv.second.env));
			list.push(a);
			lines += kv.first + ":" + kv.second.h + "\n";
		}
		const std::string etag = "\"" + sha256Hex(cr, lines).substr(0, 32) + "\"";
		if (header(r, "if-none-match") == etag) return reply(now, 304, json::Value(), etag);
		o.set("session", json::Value::string(session));
		o.set("answers", list);
		return reply(now, 200, o, etag);
	}
	if (p.size() == 5 && p[2] == "live" && p[3] == "answers" && m == "PUT") {
		const std::string sid = p[4];
		if (!student || sid != sidHeader) return error(now, 401, "wrong_token");
		if (!k.live.on || body.str("session") != k.live.session || body.integer("ver") > k.live.rec.ver) return error(now, 409, "not_live");
		if (envSize(body.str("env")) > 2048) return error(now, 413, "record_too_large");
		if (!bump("answers/" + sid + "/" + std::to_string(now / kMinute), 10)) return error(now, 429, "rate_limited", 60);
		Bytes env;
		unb64u(body.str("env"), env);
		k.answers[k.live.session][sid] = { body.integer("ver"), now, clsync::envelopeHash(cr, env), body.str("env") };
		json::Value o = json::Value::object();
		o.set("at", json::Value::integer(now));
		return reply(now, 200, o);
	}
	return error(now, 404, "not_found");
}

// ---- /api/live/v1: the pulse and the cached records ---------------------------------------

HttpResponse FakeServer::pulse(const HttpRequest& r, const std::string& path) {
	const std::vector<std::string> p = split(path.substr(0, path.find('?')));
	if (p.size() < 2 || !isHex(p[0], 32)) return error(now, 404, "not_found");
	auto it = classes.find(p[0]);
	if (it == classes.end() || gone.count(p[0])) return error(now, 404, "no_class");
	Klass& k = it->second;
	if (p[1] != k.fetchKey) {
		bump("fail/" + ip + "/" + std::to_string(now / kHour), 60);
		return error(now, 404, "wrong_fetch_key");
	}
	releaseDue(k);
	json::Value o = json::Value::object();
	if (p.size() == 2) {
		int64_t seq = k.seq, live = k.live.on ? k.live.rec.ver : 0;
		if (k.staleSeq >= 0) {   // one stale copy from the CDN
			seq = k.staleSeq;
			live = k.staleLive;
			k.staleSeq = k.staleLive = -1;
		}
		o.set("seq", json::Value::integer(seq));
		o.set("live", json::Value::integer(live));
		o.set("p", json::Value::integer(10));
		const std::string etag = "\"" + std::to_string(seq) + "." + std::to_string(live) + "\"";
		// (Not held here: a held poll that nothing changes answers 304 at the end, as now.)
		if (header(r, "if-none-match") == etag) return reply(now, 304, json::Value(), etag);
		return reply(now, 200, o, etag);
	}
	auto version = [&](const Rec& x, const std::string& v) {
		if (x.ver == 0 || std::to_string(x.ver) != v) return error(now, 404, "no_version");
		json::Value out = json::Value::object();
		out.set("ver", json::Value::integer(x.ver));
		out.set("env", json::Value::string(x.env));
		return reply(now, 200, out);
	};
	if (p.size() == 4 && p[2] == "live") {
		HttpResponse resp = version(k.live.rec, p[3]);
		if (resp.status == 200) {
			json::Value out;
			json::parse(resp.body, out);
			out.set("session", json::Value::string(k.live.session));
			resp.body = json::write(out);
		}
		return resp;
	}
	if (p.size() == 4 && p[2] == "info") return version(k.info, p[3]);
	if (p.size() == 5 && p[2] == "assignment") {
		auto a = k.assignments.find(p[3]);
		if (a == k.assignments.end()) return error(now, 404, "no_version");
		return version(a->second, p[4]);
	}
	if (p.size() == 5 && p[2] == "item") {   // hidden ones too: unlisted, not secret (3.16.2)
		auto a = k.items.find(p[3]);
		if (a == k.items.end()) return error(now, 404, "no_version");
		return version(a->second.rec, p[4]);
	}
	return error(now, 404, "not_found");
}

void FakeServer::releaseDue(Klass& k) {
	bool any = false;
	for (auto& kv : k.items) {
		ItemRow& x = kv.second;
		if (!x.hidden || !x.releaseAt || x.releaseAt > now) continue;
		x.hidden = false;
		x.releasedAt = x.releaseAt;   // the time it was meant for
		x.releaseAt = 0;
		any = true;
	}
	if (any) k.seq++;
}

// ---- controls -------------------------------------------------------------------------------

void FakeServer::cleanup() {
	for (auto it = classes.begin(); it != classes.end();) {
		Klass& k = it->second;
		if (k.roster.empty() && k.assignments.empty() && k.items.empty() && now - k.activeAt > 7 * kDay) {
			joins.erase(k.joinIndex);   // never used: no marker (3.11)
			it = classes.erase(it);
			continue;
		}
		if (now - k.activeAt > 400 * kDay) {
			gone[it->first] = "expired";
			joins.erase(k.joinIndex);
			purges.push_back("class-" + it->first);
			it = classes.erase(it);
			continue;
		}
		++it;
	}
	for (auto it = moves.begin(); it != moves.end();) it = now - it->second.createdAt > kHour ? moves.erase(it) : std::next(it);
}

void FakeServer::flipSubmissionByte(const std::string& classId, const std::string& aid, const std::string& sid) {
	Sub& s = classes[classId].subs[aid][sid];
	Bytes env;
	unb64u(s.rec.env, env);
	if (env.size() > 100) env[100] ^= 1;
	s.rec.env = b64u(env);
	s.rec.h = clsync::envelopeHash(cr, env);
}

void FakeServer::setSubmission(const std::string& classId, const std::string& aid, const std::string& sid, int64_t ver,
                               const std::string& envB64) {
	Sub& s = classes[classId].subs[aid][sid];
	Bytes env;
	unb64u(envB64, env);
	s.rec = { ver, (int64_t)env.size(), now, clsync::envelopeHash(cr, env), envB64 };
	s.attempts++;
}

void FakeServer::setAnswer(const std::string& classId, const std::string& session, const std::string& sid, int64_t ver,
                           const std::string& envB64) {
	Bytes env;
	unb64u(envB64, env);
	classes[classId].answers[session][sid] = { ver, now, clsync::envelopeHash(cr, env), envB64 };
}

void FakeServer::setName(const std::string& classId, const std::string& sid, int64_t ver, const std::string& envB64) {
	classes[classId].roster[sid].name = { ver, envSize(envB64), now, "", envB64 };
}

bool FakeServer::swapJoin(const std::string& classId, const std::string& envB64) {
	auto it = classes.find(classId);
	if (it == classes.end()) return false;
	Klass& k = it->second;
	JoinEntry& j = joins[k.joinIndex];
	j.env = envB64;
	j.ver = k.join.ver + 1;
	Bytes env;
	unb64u(envB64, env);
	k.join.ver = j.ver;
	k.join.h = clsync::envelopeHash(cr, env);
	k.join.env = envB64;
	k.seq++;
	return true;
}

void FakeServer::stalePulse(const std::string& classId, int64_t seq, int64_t live) {
	classes[classId].staleSeq = seq;
	classes[classId].staleLive = live;
}

std::string FakeServer::dump() const {
	std::string out;
	for (const auto& kv : classes) {
		const Klass& k = kv.second;
		out += "a/" + kv.first + " " + k.teacherHash + " " + k.deleteHash + "\n";
		out += "c/" + kv.first + " " + k.fetchKey + " " + k.joinIndex + " " + k.join.h + " " + k.teacher.env + " " + k.info.env + "\n";
		for (const auto& a : k.assignments) out += "as/" + kv.first + "/" + a.first + " " + a.second.env + "\n";
		for (const auto& m : k.roster) out += "r/" + kv.first + "/" + m.first + " " + m.second.hash + " " + m.second.name.env + "\n";
		for (const auto& a : k.subs)
			for (const auto& s : a.second) out += "s/" + kv.first + "/" + a.first + "/" + s.first + " " + s.second.rec.env + "\n";
		out += "lv/" + kv.first + " " + k.live.rec.env + "\n";
	}
	for (const auto& j : joins) out += "j/" + j.first + " " + j.second.classId + " " + j.second.joinHash + " " + j.second.env + "\n";
	for (const auto& m : moves) out += "mv/" + m.first + " " + m.second.classId + " " + m.second.studentId + " " + m.second.env + "\n";
	for (const auto& g : gone) out += "gone/" + g.first + " " + g.second + "\n";
	return out;
}

}  // namespace test
}  // namespace clclass
