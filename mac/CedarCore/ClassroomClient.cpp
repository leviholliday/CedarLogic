// The classroom client (CLASSROOM.md 4): one device's classes, as a teacher
// and as a student, one request at a time. Engine (ClassroomEngine.cpp) runs
// it on its own thread; the self-test drives it directly against the
// FakeServer. Everything a person reads is a sentence of 3.4 or 4.

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "ClassroomInternal.h"

#include <algorithm>
#include <cstring>

namespace clclass {

using clsync::b64u;
using clsync::hex;
using clsync::isHex;
using clsync::isUuid;
using clsync::sha256Hex;
using clsync::unb64u;

namespace {

const char* const kOffline = "Can't reach the website.";
const char* const kRateLimited = "Lots going on just now. Trying again in a minute.";
const char* const kServerError = "Something went wrong on the website.";
const char* const kNoRandom = "This computer's random number generator didn't answer. Nothing was sent.";
const char* const kChanged = "This class was changed on another device.";
const char* const kTooBig = "Too big to send (over 512 KB).";
const char* const kTooBigAssignment =
	"Too big to send (over 512 KB). A starter circuit is usually a few KB; check for a huge RAM image.";
const char* const kCantRead = "Couldn't be read";
const char* const kCantReadBig = "Couldn't be read (too big)";
const char* const kCantReadAssignment = "Couldn't be read (the teacher may need to post it again)";
const char* const kNewer = "Needs a newer CedarLogic";
const char* const kUnverified = "Couldn't be verified";
const char* const kRecordUnreadable = "This class's record on the website couldn't be read.";
const char* const kJoinUnreadable = "This class's record on the website couldn't be read.";
const char* const kJoinNotOurs = "The join record on the website isn't the one your devices wrote. Change the join code.";
const char* const kLiveElsewhere = "You're live from another device. Take over here? The other device stops pushing.";
const char* const kLiveUnreadable = "Couldn't read what the teacher sent. Trying again.";

// The sentences of 3.4, by error code (the client's own words, never the server's).
std::string sentence(const std::string& code, int status, bool joining) {
	if (code == "bad_request") return "That request wasn't understood.";
	if (code == "wrong_key") return "This teacher key doesn't match this class.";
	if (code == "wrong_token") return "This device isn't known to the class any more. Join again with the class code.";
	if (code == "forbidden") return "Not from CedarLogic.";
	if (code == "wrong_delete_token") return "That key can't delete the class.";
	if (code == "join_closed") return "Joining this class is closed. Ask your teacher to open it.";
	if (code == "not_a_member") return "You're not in this class any more.";
	if (code == "no_class") return joining ? "No class has this code. Check it with your teacher." : "This class isn't on the website.";
	if (code == "no_assignment") return "That assignment was removed.";
	if (code == "move_gone") return "That move code has expired. Make a new one on the other device.";
	if (code == "not_found") return "Not here.";
	if (code == "method_not_allowed") return "That can't be done here.";
	if (code == "student_exists" || code == "join_exists" || code == "move_exists") return "Try again.";
	if (code == "assignment_closed") return "Hand-ins for this assignment are closed.";
	if (code == "not_live") return "The teacher isn't showing this question any more.";
	if (code == "class_deleted") return "This class was deleted by the teacher.";
	if (code == "class_expired") return "This class was removed after 400 days without use.";
	if (code == "conflict") return "This was changed on another device.";
	if (code == "too_large") return "That's too much to send at once.";
	if (code == "record_too_large") return kTooBig;
	if (code == "rate_limited" || status == 429) return kRateLimited;
	if (code == "busy") return "The classroom service is busy. Trying again shortly.";
	if (code == "classroom_busy") return "Classrooms can't be made today. Try again tomorrow.";
	if (code == "classroom_paused") return "Classrooms are resting on the website for today. Your work is safe on this device.";
	if (code == "class_full") return "This class is full.";
	if (code == "too_many_assignments")
		return "This class has as many assignments as it can hold (100). Delete one to post another.";
	if (code == "student_full") return "You've handed in as much as this class allows. Ask your teacher.";
	if (code == "site_full") return "The website's classroom storage is full just now.";
	if (status == 0) return kOffline;
	return kServerError;
}

std::string quoted(const std::string& name) { return "\xE2\x80\x9C" + name + "\xE2\x80\x9D"; }   // “name”

Bytes fromB64(const std::string& s) {
	Bytes b;
	unb64u(s, b);
	return b;
}

json::Value rec(int64_t ver, const Bytes& env) {
	json::Value o = json::Value::object();
	o.set("ver", json::Value::integer(ver));
	o.set("env", json::Value::string(b64u(env)));
	return o;
}

json::Value baseRec(int64_t base, int64_t ver, const Bytes& env) {
	json::Value o = json::Value::object();
	o.set("base", json::Value::integer(base));
	o.set("ver", json::Value::integer(ver));
	o.set("env", json::Value::string(b64u(env)));
	return o;
}

json::Value assignmentToJson(const Assignment& a) {
	json::Value o = json::Value::object();
	o.set("id", json::Value::string(a.id));
	o.set("title", json::Value::string(a.title));
	o.set("instructions", json::Value::string(a.instructions));
	o.set("cdl", json::Value::string(a.cdl));
	o.set("dueAt", json::Value::integer(a.dueAt < 0 ? -1 : a.dueAt));
	o.set("closeAfterDue", json::Value::boolean(a.closeAfterDue));
	o.set("ver", json::Value::integer(a.ver));
	o.set("keyText", json::Value::string(a.keyText));
	o.set("keyNames", json::Value::string(a.keyNames));
	o.set("keySealed", json::Value::boolean(a.keySealed));
	o.set("unreadable", json::Value::boolean(a.unreadable));
	o.set("problem", json::Value::string(a.problem));
	return o;
}

Assignment assignmentOfJson(const json::Value& o) {
	Assignment a;
	a.id = o.str("id");
	a.title = o.str("title");
	a.instructions = o.str("instructions");
	a.cdl = o.str("cdl");
	const json::Value* due = o.get("dueAt");
	a.dueAt = due && due->isNumber() && due->n >= 0 ? (int64_t)due->n : -1;
	a.closeAfterDue = o.flag("closeAfterDue");
	a.ver = o.integer("ver");
	a.keyText = o.str("keyText");
	a.keyNames = o.str("keyNames");
	a.keySealed = o.flag("keySealed");
	a.unreadable = o.flag("unreadable");
	a.problem = o.str("problem");
	return a;
}

json::Value submissionToJson(const Submission& s, const std::string& h) {
	json::Value o = json::Value::object();
	o.set("studentId", json::Value::string(s.studentId));
	o.set("name", json::Value::string(s.name));
	o.set("cdl", json::Value::string(s.cdl));
	o.set("handedInAt", json::Value::integer(s.handedInAt));
	o.set("attempts", json::Value::integer(s.attempts));
	o.set("ver", json::Value::integer(s.ver));
	o.set("h", json::Value::string(h));
	o.set("unreadable", json::Value::boolean(s.unreadable));
	o.set("problem", json::Value::string(s.problem));
	return o;
}

Submission submissionOfJson(const json::Value& o) {
	Submission s;
	s.studentId = o.str("studentId");
	s.name = o.str("name");
	s.cdl = o.str("cdl");
	s.handedInAt = o.integer("handedInAt");
	s.attempts = (int)o.integer("attempts");
	s.ver = o.integer("ver");
	s.unreadable = o.flag("unreadable");
	s.problem = o.str("problem");
	return s;
}

std::string mapKey(const std::string& a, const std::string& b) { return a + "/" + b; }

// "14 Jan 2028" for a time in ms (UTC; the apps show their own local dates).
std::string dateText(int64_t ms) {
	static const char* const months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	int64_t y = 0;
	unsigned m = 1, d = 1;
	clsync::civilFromDays(ms / kDay, y, m, d);
	return std::to_string(d) + " " + months[(m - 1) % 12] + " " + std::to_string(y);
}

}  // namespace

Client::Client(Config c, Crypto& cr, Curve& curve, ClientHooks h) : cfg_(std::move(c)), cr_(cr), curve_(curve), hooks_(std::move(h)) {}

void Client::note(const std::string& t) {
	if (hooks_.notice) hooks_.notice(t);
}

Teaching* Client::teachingOf(const std::string& classId) {
	auto it = teaching_.find(classId);
	return it == teaching_.end() ? nullptr : &it->second;
}

Membership* Client::membershipOf(const std::string& classId) {
	auto it = members_.find(classId);
	return it == members_.end() ? nullptr : &it->second;
}

// ---- HTTP (3.2) ---------------------------------------------------------------------------

Client::Api Client::call(const std::string& method, const std::string& url,
                         const std::vector<std::pair<std::string, std::string>>& headers, const json::Value* body,
                         const std::string& ifNoneMatch) {
	Api a;
	HttpRequest r;
	r.method = method;
	r.url = url;
	if (!cfg_.client.empty()) r.headers.emplace_back("x-cedarlogic-client", cfg_.client);
	if (!cfg_.appKey.empty()) r.headers.emplace_back("x-cedarlogic-key", cfg_.appKey);
	for (const auto& h : headers) r.headers.push_back(h);
	if (!ifNoneMatch.empty()) r.headers.emplace_back("If-None-Match", ifNoneMatch);
	if (body) {
		r.headers.emplace_back("content-type", "application/json");
		r.body = json::write(*body);
	}
	requests++;
	if (!hooks_.http) return a;
	const HttpResponse h = hooks_.http(r);
	a.status = h.status;
	a.sent = h.sent;
	auto header = [&](const char* name) {
		auto it = h.headers.find(name);
		return it == h.headers.end() ? std::string() : it->second;
	};
	int64_t serverMs = 0;
	if (clsync::parseHttpDate(header("date"), serverMs)) offset_ = serverMs - now();   // Sync 4.6's clock offset
	a.etag = header("etag");
	const std::string ra = header("retry-after");
	if (!ra.empty()) a.retryAfterMs = std::max<int64_t>(0, atoll(ra.c_str())) * kSecond;
	if (!h.body.empty() && json::parse(h.body, a.body) && a.body.isObject()) a.error = a.body.str("error");
	if (a.status == 429 || a.status == 503) {
		if (a.retryAfterMs <= 0) a.retryAfterMs = a.body.integer("retryAfter", a.status == 429 ? 60 : 30) * kSecond;
		if (a.status == 429) a.retryAfterMs = std::max<int64_t>(a.retryAfterMs, 30 * kSecond);
		retryAfter_ = std::max(retryAfter_, now() + a.retryAfterMs);
	}
	return a;
}

Client::Api Client::teacherCall(Teaching& t, const std::string& method, const std::string& path, const json::Value* body,
                                const std::string& ifNoneMatch) {
	TeacherKeys k;
	keysOf(t, k);
	return call(method, cfg_.serverBase + path, { { "Authorization", "Bearer " + k.teacherToken } }, body, ifNoneMatch);
}

Client::Api Client::studentCall(Membership& m, const std::string& method, const std::string& path, const json::Value* body,
                                const std::string& ifNoneMatch) {
	return call(method, cfg_.serverBase + path, { { "Authorization", "Bearer " + m.token }, { "x-cedarlogic-student", m.studentId } },
	            body, ifNoneMatch);
}

Result Client::fail(const Api& a, const std::string& context) {
	(void)context;
	return Result::bad(sentence(a.error, a.status, false), a.status, a.error);
}

// ---- Keys --------------------------------------------------------------------------------

const JoinKeys& Client::joinKeysFor(const std::string& code) {
	auto it = joinCache_.find(code);
	if (it != joinCache_.end()) return it->second;
	Bytes secret;
	std::string why;
	JoinKeys k;
	if (decodeCode(cr_, CodeKind::Join, code, secret, why)) k = joinKeys(cr_, curve_, secret);   // the slow step, once per code
	if (joinCache_.size() > 16) joinCache_.clear();
	return joinCache_[code] = k;
}

bool Client::keysOf(const Teaching& t, TeacherKeys& k) const {
	Bytes secret;
	std::string why;
	if (!decodeCode(cr_, CodeKind::Teacher, t.teacherKey, secret, why)) return false;
	k = teacherKeys(cr_, secret);
	return k.valid();
}

OpenKey Client::teacherOpenKey(const Teaching& t) const {
	OpenKey k;
	k.d = fromB64(t.rec.d);
	k.pub = fromB64(t.rec.pub);
	return k;
}

Bytes Client::classKeyOf(const Teaching& t) const { return fromB64(t.rec.classKey); }

bool Client::sealTeacher(Teaching& t, int64_t ver, Bytes& env) {
	TeacherKeys k;
	std::string why;
	return keysOf(t, k) && seal(cr_, k.backupKey, "teacher", t.classId, "teacher", ver, teacherJson(t.rec), false, kMaxSmall, env, why);
}

bool Client::sealJoin(Teaching& t, const JoinKeys& jk, int64_t ver, Bytes& env) {
	std::string why;
	return jk.valid() && seal(cr_, jk.joinKey, "join", t.classId, jk.joinId, ver, joinJson(t.rec.name, t.rec.classKey, t.rec.pub), false,
	                          kMaxSmall, env, why);
}

bool Client::sealInfo(Teaching& t, int64_t ver, Bytes& env) {
	std::string why;
	return seal(cr_, classKeyOf(t), "info", t.classId, "info", ver, infoJson(t.rec.name, t.rec.modifiedAt), false, kMaxSmall, env, why);
}

// ---- Teacher: the class (4.1) --------------------------------------------------------------

Result Client::putClass(Teaching& t, bool fresh) {
	TeacherKeys k;
	if (!keysOf(t, k)) return Result::bad(kRecordUnreadable);
	const JoinKeys& jk = joinKeysFor(t.rec.joinCode);
	Bytes teacherEnv, joinEnv, infoEnv;
	if (!sealTeacher(t, 1, teacherEnv) || !sealJoin(t, jk, 1, joinEnv) || !sealInfo(t, 1, infoEnv)) return Result::bad(kNoRandom);
	json::Value body = json::Value::object();
	body.set("deleteHash", json::Value::string(k.deleteHash));
	body.set("teacher", rec(1, teacherEnv));
	json::Value join = rec(1, joinEnv);
	join.set("joinId", json::Value::string(jk.joinId));
	join.set("joinToken", json::Value::string(jk.joinToken));
	join.set("open", json::Value::boolean(t.rec.joinOpen));
	body.set("join", join);
	body.set("info", rec(1, infoEnv));
	const Api a = teacherCall(t, "PUT", "/classes/" + t.classId, &body);
	if (a.status != 201 && a.status != 200) return fail(a, "create");
	if (fresh && a.status != 201) return Result::bad("Try again.", a.status, "exists");
	t.ver = 1;
	t.h = clsync::envelopeHash(cr_, teacherEnv);
	t.joinVer = 1;
	t.joinH = clsync::envelopeHash(cr_, joinEnv);
	t.infoVer = 1;
	t.seq = 0;
	t.etag.clear();
	t.liveVer = std::max<int64_t>(t.liveVer, a.body.get("live") ? a.body.get("live")->integer("ver") : 0);
	t.fetchKey = a.body.str("fetchKey", t.fetchKey);
	t.expiresAt = a.body.integer("expiresAt", t.expiresAt);
	Result r = Result::good(t.classId);
	r.status = a.status;
	return r;
}

Result Client::createClass(const std::string& name0) {
	const std::string name = cleanName(name0, 100, "Untitled class");
	for (int attempt = 0; attempt < 3; attempt++) {
		uint8_t secret[16], join[6], classKey[32], d[32], pub[65];
		if (!cr_.random(secret, 16) || !cr_.random(join, 6) || !cr_.random(classKey, 32) || !curve_.p256Generate(d, pub))
			return Result::bad(kNoRandom);
		Teaching t;
		t.teacherKey = encodeCode(cr_, CodeKind::Teacher, secret);
		const TeacherKeys k = teacherKeys(cr_, Bytes(secret, secret + 16));
		t.classId = k.classId;
		t.rec.name = name;
		t.rec.d = b64u(Bytes(d, d + 32));
		t.rec.pub = b64u(Bytes(pub, pub + 65));
		t.rec.classKey = b64u(Bytes(classKey, classKey + 32));
		t.rec.joinCode = encodeCode(cr_, CodeKind::Join, join);
		t.rec.joinOpen = true;
		t.rec.createdAt = t.rec.modifiedAt = serverNow();
		memset(secret, 0, sizeof secret);
		memset(d, 0, sizeof d);
		memset(classKey, 0, sizeof classKey);
		Result r = putClass(t, true);
		if (!r.ok && (r.error == "exists" || r.error == "join_exists")) continue;   // practically impossible: new secrets
		if (!r.ok) return r;
		teaching_[t.classId] = t;   // only after the website took it (4.1)
		save();
		if (hooks_.putSide)
			hooks_.putSide("", classroomJson(t.classId, t.teacherKey, t.rec.name, t.rec.createdAt, t.rec.modifiedAt, cfg_.deviceName,
			                                 deviceId_));
		return Result::good(t.classId);
	}
	return Result::bad("Try again.");
}

// The teacher record inside a status, opened with the backup key (4.1).
bool Client::openTeacherStatus(Teaching& t, const json::Value& status, std::string& problem) {
	const json::Value* tr = status.get("teacher");
	if (!tr) { problem = kRecordUnreadable; return false; }
	TeacherKeys k;
	if (!keysOf(t, k)) { problem = kRecordUnreadable; return false; }
	OpenKey ok;
	ok.key = k.backupKey;
	std::string payload, why;
	const Bytes env = fromB64(tr->str("env"));
	json::Value p;
	TeacherRec next;
	if (!open(cr_, curve_, env, "teacher", t.classId, "teacher", tr->integer("ver"), ok, payload, why) ||
	    !readPayload(payload, "teacher", p).empty() || !teacherFrom(p, next)) {
		problem = kRecordUnreadable;
		return false;
	}
	// The key pair and the class key never change; a record holding others isn't this class's.
	uint8_t pub[65];
	const Bytes d = fromB64(next.d);
	if (d.size() != 32 || !curve_.p256Public(d.data(), pub) || b64u(Bytes(pub, pub + 65)) != next.pub || fromB64(next.classKey).size() != 32) {
		problem = kRecordUnreadable;
		return false;
	}
	if (!t.rec.d.empty() && (next.d != t.rec.d || next.classKey != t.rec.classKey)) {
		problem = kRecordUnreadable;
		return false;
	}
	t.rec = next;
	t.ver = tr->integer("ver");
	t.h = clsync::envelopeHash(cr_, env);
	return true;
}

Result Client::previewTeacherKey(const std::string& text) {
	std::string code, why;
	if (!parseCode(cr_, CodeKind::Teacher, text, code, why)) return Result::bad(whyText(CodeKind::Teacher, why, text), 0, why);
	Teaching t;
	t.teacherKey = code;
	TeacherKeys k;
	keysOf(t, k);
	t.classId = k.classId;
	const Api a = teacherCall(t, "GET", "/classes/" + t.classId, nullptr);
	if (a.status != 200) return fail(a, "teacher key");
	std::string problem;
	if (!openTeacherStatus(t, a.body, problem)) return Result::bad(problem, 200, "damaged");
	Result r = Result::good(t.rec.name);
	if (teaching_.count(t.classId)) r.message = quoted(t.rec.name) + " is already on this device.";
	return r;
}

Result Client::addTeacherKey(const std::string& text, bool quiet) {
	std::string code, why;
	if (!parseCode(cr_, CodeKind::Teacher, text, code, why)) return Result::bad(whyText(CodeKind::Teacher, why, text), 0, why);
	Teaching t;
	t.teacherKey = code;
	TeacherKeys k;
	keysOf(t, k);
	t.classId = k.classId;
	if (teaching_.count(t.classId)) {
		Result r = Result::good(t.classId);
		r.message = quoted(teaching_[t.classId].rec.name) + " is already on this device.";
		return r;
	}
	const Api a = teacherCall(t, "GET", "/classes/" + t.classId, nullptr);
	if (a.status != 200) return fail(a, "teacher key");
	std::string problem;
	if (!openTeacherStatus(t, a.body, problem)) return Result::bad(problem, 200, "damaged");
	(void)quiet;   // the same either way; the caller decides whether to say anything
	teaching_[t.classId] = t;
	// The rest comes with the first status read (assignments, the join record's check).
	refreshTeacher(t.classId);
	save();
	return Result::good(t.classId);
}

void Client::checkJoinRecord(Teaching& t, const json::Value& join) {
	const int64_t ver = join.integer("ver");
	const std::string h = join.str("h");
	if (ver == t.joinVer && h == t.joinH && !h.empty()) return;
	const JoinKeys& jk = joinKeysFor(t.rec.joinCode);
	if (!jk.valid()) return;
	const Api a = call("GET", cfg_.serverBase + "/join/" + jk.joinId, { { "Authorization", "Bearer " + jk.joinToken } }, nullptr);
	if (a.status == 0 || a.status == 429 || a.status >= 500) return;   // try again at the next status
	bool ours = false;
	if (a.status == 200) {
		OpenKey k;
		k.key = jk.joinKey;
		std::string payload, why;
		json::Value p;
		const Bytes env = fromB64(a.body.str("env"));
		ours = open(cr_, curve_, env, "join", t.classId, jk.joinId, a.body.integer("ver"), k, payload, why) &&
		       readPayload(payload, "join", p).empty() && p.str("pub") == t.rec.pub && p.str("classKey") == t.rec.classKey;
		if (ours && clsync::envelopeHash(cr_, env) != h) ours = false;   // what the status names must be what we checked
	}
	if (ours) {
		t.joinVer = ver;
		t.joinH = h;
		t.joinProblem.clear();
	} else {
		t.joinProblem = kJoinNotOurs;
		log.push_back("join record of " + t.classId + " isn't ours");
	}
}

Result Client::refreshTeacher(const std::string& classId) {
	Teaching* tp = teachingOf(classId);
	if (!tp) return Result::bad("That class isn't on this device.");
	Teaching& t = *tp;
	const Api a = teacherCall(t, "GET", "/classes/" + classId, nullptr, t.etag);
	if (a.status == 304) return Result::good();
	if (a.status == 404 && a.error == "no_class") {
		// A never-used class the website removed after 7 days: re-created quietly (4.1).
		Result r = putClass(t, false);
		if (r.ok) {
			save();
			return refreshTeacher(classId);
		}
		return r;
	}
	if (a.status == 410) return gone(classId, a, true);
	if (a.status != 200) {
		Status s;
		s.kind = a.status == 0 ? Status::Offline : Status::Error;
		s.text = sentence(a.error, a.status, false);
		status_[classId] = s;
		return fail(a, "status");
	}
	status_[classId] = Status();
	const json::Value& b = a.body;
	const int64_t seq = b.integer("seq");
	if (seq < t.seq) {
		log.push_back("an older status of " + classId + " (seq " + std::to_string(seq) + ") ignored");
		return Result::good();
	}
	std::string problem;
	if (const json::Value* tr = b.get("teacher")) {
		const int64_t ver = tr->integer("ver");
		if (ver > t.ver) {
			Teaching next = t;
			if (openTeacherStatus(next, b, problem)) t = next;
			else log.push_back("teacher record of " + classId + " couldn't be read: " + problem);
		} else if (ver < t.ver) {
			log.push_back("an older teacher record of " + classId + " ignored");
		}
	}
	t.seq = seq;
	t.etag = a.etag;
	t.fetchKey = b.str("fetchKey", t.fetchKey);
	t.expiresAt = b.integer("expiresAt", t.expiresAt);
	if (const json::Value* i = b.get("info")) t.infoVer = std::max(t.infoVer, i->integer("ver"));
	if (const json::Value* j = b.get("join")) {
		t.rec.joinOpen = j->flag("open", t.rec.joinOpen);
		checkJoinRecord(t, *j);
	}
	if (const json::Value* l = b.get("live")) {
		const int64_t ver = l->integer("ver");
		if (ver > t.liveVer) {
			t.liveVer = ver;
			// Another device went live, pushed or ended: this one isn't the one pushing now.
			if (l->str("session") != t.live.session || !l->flag("on")) t.liveOn = false;
		}
		t.otherLive = l->flag("on") && !t.liveOn && ver >= t.liveVer;
	}
	// Assignments: the index is the list (2.1); new versions are fetched like a student's.
	std::set<std::string> listed;
	if (const json::Value* as = b.get("assignments"))
		for (const auto& kv : as->o) {
			listed.insert(kv.first);
			const int64_t ver = kv.second.integer("ver");
			const json::Value* c = kv.second.get("closesAt");
			closesAt_[mapKey(classId, kv.first)] = c && c->isInt() ? c->i() : -1;
			auto seen = t.assignments.find(kv.first);
			if (seen != t.assignments.end() && seen->second.first >= ver) continue;
			fetchAssignment(classId, t.fetchKey, classKeyOf(t), kv.first, ver, kv.second.str("h"), true);
		}
	for (auto it = t.assignments.begin(); it != t.assignments.end();) {
		if (listed.count(it->first)) { ++it; continue; }
		asgCache_.erase(mapKey(classId, it->first));
		if (hooks_.removeTree) hooks_.removeTree(cachePath(classId, "assignments/" + it->first + ".json"));
		it = t.assignments.erase(it);
	}
	save();
	return Result::good();
}

Result Client::gone(const std::string& classId, const Api& a, bool teachingSide) {
	const bool expired = a.error == "class_expired";
	std::string name;
	if (teachingSide) {
		if (Teaching* t = teachingOf(classId)) name = t->rec.name;
		forgetClassLocal(classId, false);
	} else {
		if (Membership* m = membershipOf(classId)) name = m->className;
	}
	const std::string text = expired ? quoted(name) + " was removed after 400 days without use."
	                                 : quoted(name) + " was deleted by the teacher.";
	if (!teachingSide) forgetMembershipLocal(classId, text);
	else note(text);
	Result r = Result::bad(text, 410, a.error.empty() ? "class_deleted" : a.error);
	return r;
}

Result Client::renameClass(const std::string& classId, const std::string& name0) {
	Teaching* tp = teachingOf(classId);
	if (!tp) return Result::bad("That class isn't on this device.");
	Teaching& t = *tp;
	Teaching next = t;
	next.rec.name = cleanName(name0, 100, "Untitled class");
	next.rec.modifiedAt = serverNow();
	Bytes infoEnv, joinEnv, teacherEnv;
	const JoinKeys& jk = joinKeysFor(next.rec.joinCode);
	if (!sealInfo(next, t.infoVer + 1, infoEnv) || !sealJoin(next, jk, t.joinVer + 1, joinEnv) || !sealTeacher(next, t.ver + 1, teacherEnv))
		return Result::bad(kNoRandom);
	json::Value info = baseRec(t.infoVer, t.infoVer + 1, infoEnv);
	Api a = teacherCall(t, "PUT", "/classes/" + classId + "/info", &info);
	if (a.status == 412) {
		refreshTeacher(classId);
		return Result::bad(kChanged, 412, "conflict");
	}
	if (a.status != 200 && a.status != 201) return fail(a, "rename");
	t.infoVer++;
	json::Value body = json::Value::object();
	body.set("joinId", json::Value::string(jk.joinId));
	body.set("joinToken", json::Value::string(jk.joinToken));
	body.set("open", json::Value::boolean(next.rec.joinOpen));
	body.set("ver", json::Value::integer(t.joinVer + 1));
	body.set("env", json::Value::string(b64u(joinEnv)));
	body.set("teacher", baseRec(t.ver, t.ver + 1, teacherEnv));
	a = teacherCall(t, "PUT", "/classes/" + classId + "/join", &body);
	if (a.status == 412) {
		refreshTeacher(classId);
		return Result::bad(kChanged, 412, "conflict");
	}
	if (a.status != 200) return fail(a, "rename");
	t.rec = next.rec;
	t.ver++;
	t.h = clsync::envelopeHash(cr_, teacherEnv);
	t.joinVer++;
	t.joinH = clsync::envelopeHash(cr_, joinEnv);
	t.fetchKey = a.body.str("fetchKey", t.fetchKey);
	if (hooks_.putSide && hooks_.sideRecords)
		for (const auto& sr : hooks_.sideRecords()) {
			json::Value p;
			if (readPayload(sr.second, "classroom", p).empty() && p.str("classId") == classId)
				hooks_.putSide(sr.first, classroomJson(classId, t.teacherKey, t.rec.name, t.rec.createdAt, t.rec.modifiedAt, cfg_.deviceName,
				                                       deviceId_));
		}
	save();
	return Result::good();
}

Result Client::setJoinOpen(const std::string& classId, bool open) {
	Teaching* tp = teachingOf(classId);
	if (!tp) return Result::bad("That class isn't on this device.");
	Teaching& t = *tp;
	{
		// The join record and the teacher record that remembers it, written together (3.3).
		Teaching next = t;
		next.rec.joinOpen = open;
		next.rec.modifiedAt = serverNow();
		const JoinKeys& jk = joinKeysFor(next.rec.joinCode);
		Bytes joinEnv, teacherEnv;
		if (!sealJoin(next, jk, t.joinVer + 1, joinEnv) || !sealTeacher(next, t.ver + 1, teacherEnv)) return Result::bad(kNoRandom);
		json::Value body = json::Value::object();
		body.set("joinId", json::Value::string(jk.joinId));
		body.set("joinToken", json::Value::string(jk.joinToken));
		body.set("open", json::Value::boolean(open));
		body.set("ver", json::Value::integer(t.joinVer + 1));
		body.set("env", json::Value::string(b64u(joinEnv)));
		body.set("teacher", baseRec(t.ver, t.ver + 1, teacherEnv));
		const Api a = teacherCall(t, "PUT", "/classes/" + classId + "/join", &body);
		if (a.status == 412) {
			refreshTeacher(classId);
			return Result::bad(kChanged, 412, "conflict");
		}
		if (a.status != 200) return fail(a, "join");
		t.rec = next.rec;
		t.ver++;
		t.h = clsync::envelopeHash(cr_, teacherEnv);
		t.joinVer++;
		t.joinH = clsync::envelopeHash(cr_, joinEnv);
		t.fetchKey = a.body.str("fetchKey", t.fetchKey);
		save();
		return Result::good();
	}
}

Result Client::newJoinCode(const std::string& classId) {
	Teaching* tp = teachingOf(classId);
	if (!tp) return Result::bad("That class isn't on this device.");
	Teaching& t = *tp;
	for (int attempt = 0; attempt < 2; attempt++) {
		Teaching next = t;
		next.rec.joinCode = newCode(cr_, CodeKind::Join);
		if (next.rec.joinCode.empty()) return Result::bad(kNoRandom);
		next.rec.modifiedAt = serverNow();
		const JoinKeys& jk = joinKeysFor(next.rec.joinCode);
		Bytes joinEnv, teacherEnv;
		if (!sealJoin(next, jk, t.joinVer + 1, joinEnv) || !sealTeacher(next, t.ver + 1, teacherEnv)) return Result::bad(kNoRandom);
		json::Value body = json::Value::object();
		body.set("joinId", json::Value::string(jk.joinId));
		body.set("joinToken", json::Value::string(jk.joinToken));
		body.set("open", json::Value::boolean(next.rec.joinOpen));
		body.set("ver", json::Value::integer(t.joinVer + 1));
		body.set("env", json::Value::string(b64u(joinEnv)));
		body.set("teacher", baseRec(t.ver, t.ver + 1, teacherEnv));
		const Api a = teacherCall(t, "PUT", "/classes/" + classId + "/join", &body);
		if (a.status == 409 && a.error == "join_exists") continue;   // another class has it: another code
		if (a.status == 412) {
			refreshTeacher(classId);
			return Result::bad(kChanged, 412, "conflict");
		}
		if (a.status != 200) return fail(a, "join");
		t.rec = next.rec;
		t.ver++;
		t.h = clsync::envelopeHash(cr_, teacherEnv);
		t.joinVer++;
		t.joinH = clsync::envelopeHash(cr_, joinEnv);
		t.joinProblem.clear();
		t.fetchKey = a.body.str("fetchKey", t.fetchKey);
		save();
		return Result::good(t.rec.joinCode);
	}
	return Result::bad("Try again.");
}

void Client::forgetClassLocal(const std::string& classId, bool tombstone) {
	if (hooks_.sideRecords)
		for (const auto& sr : hooks_.sideRecords()) {
			json::Value p;
			if (!readPayload(sr.second, "classroom", p).empty() || p.str("classId") != classId) continue;
			if (tombstone && hooks_.deleteSide) hooks_.deleteSide(sr.first);
			else if (!tombstone && std::find(removedSide_.begin(), removedSide_.end(), sr.first) == removedSide_.end())
				removedSide_.push_back(sr.first);
		}
	teaching_.erase(classId);
	for (auto it = asgCache_.begin(); it != asgCache_.end();) it = it->first.compare(0, classId.size() + 1, classId + "/") == 0 ? asgCache_.erase(it) : std::next(it);
	for (auto it = subs_.begin(); it != subs_.end();) it = it->first.compare(0, classId.size() + 1, classId + "/") == 0 ? subs_.erase(it) : std::next(it);
	for (auto it = roster_.begin(); it != roster_.end();) it = it->first.compare(0, classId.size() + 1, classId + "/") == 0 ? roster_.erase(it) : std::next(it);
	counts_.erase(classId);
	if (hooks_.removeTree && !members_.count(classId)) hooks_.removeTree("cache/" + classId);
	save();
}

void Client::forgetClass(const std::string& classId) { forgetClassLocal(classId, false); }

Result Client::deleteClass(const std::string& classId) {
	Teaching* tp = teachingOf(classId);
	if (!tp) return Result::bad("That class isn't on this device.");
	TeacherKeys k;
	keysOf(*tp, k);
	const Api a = call("DELETE", cfg_.serverBase + "/classes/" + classId,
	                   { { "Authorization", "Bearer " + k.teacherToken }, { "x-cedarlogic-delete", k.deleteToken } }, nullptr);
	if (a.status != 200 && a.status != 410) return fail(a, "delete");
	forgetClassLocal(classId, true);
	return Result::good();
}

// ---- Teacher: assignments (4.2) --------------------------------------------------------------

Result Client::postAssignment(const std::string& classId, const Assignment& draft, bool studentsCanCheck) {
	Teaching* tp = teachingOf(classId);
	if (!tp) return Result::bad("That class isn't on this device.");
	Teaching& t = *tp;
	const std::string aid = draft.id.empty() ? clsync::newUuid(cr_) : draft.id;
	if (!isUuid(aid)) return Result::bad(aid.empty() ? kNoRandom : "That request wasn't understood.");
	const auto mark = t.assignments.find(aid);
	const int64_t base = mark == t.assignments.end() ? 0 : mark->second.first;
	const int64_t ver = base + 1;
	AssignmentRec a;
	a.title = cutText(draft.title, 200);
	if (a.title.empty()) a.title = "Untitled assignment";
	a.instructions = cutText(draft.instructions, 20000);
	a.cdl = draft.cdl;
	a.dueAt = draft.dueAt;
	a.closeAfterDue = draft.closeAfterDue && draft.dueAt >= 0;
	const auto cached = asgCache_.find(mapKey(classId, aid));
	a.createdAt = serverNow();
	a.modifiedAt = a.createdAt;
	if (!draft.keyText.empty()) {
		a.keyNames = draft.keyNames;
		if (studentsCanCheck) {
			a.keyMode = 1;
			a.keyText = draft.keyText;
		} else {
			Bytes env;
			std::string why;
			if (!sealTo(cr_, curve_, fromB64(t.rec.pub), "key", classId, aid, ver, keyJson(draft.keyText, draft.keyNames), false, kMaxKey, env,
			            why))
				return Result::bad(why == "too big" ? kTooBigAssignment : kNoRandom);
			a.keyMode = 2;
			a.keySealed = b64u(env);
			a.keyNames.clear();
		}
	}
	(void)cached;
	Bytes env;
	std::string why;
	if (!seal(cr_, classKeyOf(t), "assignment", classId, aid, ver, assignmentJson(a), true, kMaxRecord, env, why))
		return Result::bad(why == "too big" ? kTooBigAssignment : kNoRandom);
	json::Value body = baseRec(base, ver, env);
	body.set("closesAt", a.closeAfterDue ? json::Value::integer(a.dueAt) : json::Value());
	const Api r = teacherCall(t, "PUT", "/classes/" + classId + "/assignments/" + aid, &body);
	if (r.status == 412) {
		refreshTeacher(classId);
		return Result::bad(kChanged, 412, "conflict");
	}
	if (r.status == 413) return Result::bad(kTooBigAssignment, 413, r.error);
	if (r.status != 200 && r.status != 201) return fail(r, "post");
	t.assignments[aid] = { ver, clsync::envelopeHash(cr_, env) };
	closesAt_[mapKey(classId, aid)] = a.closeAfterDue ? a.dueAt : -1;
	Assignment kept = draft;   // the teacher's own copy, key text included (4.9: re-posted from here if damaged)
	kept.id = aid;
	kept.title = a.title;
	kept.instructions = a.instructions;
	kept.closeAfterDue = a.closeAfterDue;
	kept.ver = ver;
	kept.keySealed = a.keyMode == 2;
	kept.unreadable = false;
	kept.problem.clear();
	asgCache_[mapKey(classId, aid)] = kept;
	saveCache(classId, "assignments/" + aid + ".json", json::write(assignmentToJson(kept)));
	save();
	return Result::good(aid);
}

Result Client::deleteAssignment(const std::string& classId, const std::string& aid) {
	Teaching* tp = teachingOf(classId);
	if (!tp) return Result::bad("That class isn't on this device.");
	const Api a = teacherCall(*tp, "DELETE", "/classes/" + classId + "/assignments/" + aid, nullptr);
	if (a.status != 200 && a.status != 404) return fail(a, "delete assignment");
	tp->assignments.erase(aid);
	asgCache_.erase(mapKey(classId, aid));
	for (auto it = subs_.begin(); it != subs_.end();)
		it = it->first.compare(0, classId.size() + aid.size() + 2, classId + "/" + aid + "/") == 0 ? subs_.erase(it) : std::next(it);
	if (hooks_.removeTree) {
		hooks_.removeTree(cachePath(classId, "assignments/" + aid + ".json"));
		hooks_.removeTree(cachePath(classId, "submissions/" + aid));
	}
	save();
	return Result::good();
}

// An assignment record of exactly `ver`, from the cached path (3.3), opened and kept.
Result Client::fetchAssignment(const std::string& classId, const std::string& fetchKey, const Bytes& classKey, const std::string& aid,
                               int64_t ver, const std::string& h, bool teacher) {
	(void)h;
	const Api a = call("GET", cfg_.liveBase + "/" + classId + "/" + fetchKey + "/assignment/" + aid + "/" + std::to_string(ver), {}, nullptr);
	if (a.status != 200) return Result::bad(sentence(a.error, a.status, false), a.status, a.error);
	Assignment out;
	out.id = aid;
	out.ver = ver;
	OpenKey k;
	k.key = classKey;
	std::string payload, why;
	json::Value p;
	const Bytes env = fromB64(a.body.str("env"));
	std::string verdict = "invalid";
	if (open(cr_, curve_, env, "assignment", classId, aid, ver, k, payload, why)) verdict = readPayload(payload, "assignment", p);
	AssignmentRec r;
	if (verdict.empty()) {
		assignmentFrom(p, r);
		out.title = r.title;
		out.instructions = r.instructions;
		out.cdl = r.cdl;
		out.dueAt = r.dueAt;
		out.closeAfterDue = r.closeAfterDue;
		if (r.keyMode == 1) {
			out.keyText = r.keyText;
			out.keyNames = r.keyNames;
		} else if (r.keyMode == 2) {
			out.keySealed = true;
			if (teacher) {
				// Only the teacher's devices open the sealed key (4.2).
				Teaching* t = teachingOf(classId);
				std::string kp;
				json::Value kj;
				if (t && open(cr_, curve_, fromB64(r.keySealed), "key", classId, aid, ver, teacherOpenKey(*t), kp, why) &&
				    readPayload(kp, "key", kj).empty()) {
					out.keyText = kj.str("text");
					out.keyNames = kj.str("names");
				}
			}
		}
		if (tooBig(out.cdl)) {
			out.cdl.clear();
			out.unreadable = true;
			out.problem = kCantReadBig;
		}
	} else {
		out.unreadable = true;
		out.problem = verdict == "newer" ? kNewer : kCantReadAssignment;
		out.title = asgCache_.count(mapKey(classId, aid)) ? asgCache_[mapKey(classId, aid)].title : std::string();
	}
	asgCache_[mapKey(classId, aid)] = out;
	saveCache(classId, "assignments/" + aid + ".json", json::write(assignmentToJson(out)));
	if (teacher) {
		if (Teaching* t = teachingOf(classId)) t->assignments[aid] = { ver, clsync::envelopeHash(cr_, env) };
	} else if (Membership* m = membershipOf(classId)) {
		m->seenAssignments[aid] = ver;
	}
	return Result::good();
}

// ---- Teacher: students and hand-ins (4.3) -------------------------------------------------

Result Client::refreshStudents(const std::string& classId) {
	Teaching* tp = teachingOf(classId);
	if (!tp) return Result::bad("That class isn't on this device.");
	Teaching& t = *tp;
	const Api a = teacherCall(t, "GET", "/classes/" + classId + "/students", nullptr);
	if (a.status == 410) return gone(classId, a, true);
	if (a.status != 200) return fail(a, "students");
	std::set<std::string> listed;
	if (const json::Value* list = a.body.get("students"))
		for (const json::Value& s : list->a) {
			const std::string sid = s.str("studentId");
			if (!isUuid(sid)) continue;
			listed.insert(sid);
			const std::string key = mapKey(classId, sid);
			Student& st = roster_[key];
			st.studentId = sid;
			st.joinedAt = s.integer("joinedAt");
			st.seenAt = s.integer("seenAt");
			const json::Value* n = s.get("name");
			const int64_t nver = n ? n->integer("ver") : 0;
			if (nameVer_.count(key) && nameVer_[key] >= nver) continue;   // opened already (cached by ver)
			std::string payload, why;
			json::Value p;
			const bool opened = n && open(cr_, curve_, fromB64(n->str("env")), "name", classId, sid, nver, teacherOpenKey(t), payload, why) &&
			                    readPayload(payload, "name", p).empty();
			nameVer_[key] = nver;
			if (!opened) {
				if (st.name.empty()) st.unreadable = true;
				continue;
			}
			const std::string proof = p.str("proof");
			auto pin = t.proofs.find(sid);
			if (pin == t.proofs.end()) {
				t.proofs[sid] = proof;   // the first proof opened for a student is pinned (2.2)
			} else if (pin->second != proof) {
				log.push_back("a name record for " + sid + " couldn't be verified");
				continue;   // keeps the pinned name
			}
			st.name = cleanName(p.str("name"), 64, "a student");
			st.unreadable = false;
		}
	for (auto it = roster_.begin(); it != roster_.end();) {
		if (it->first.compare(0, classId.size() + 1, classId + "/") == 0 && !listed.count(it->second.studentId)) it = roster_.erase(it);
		else ++it;
	}
	save();
	return Result::good();
}

Result Client::removeStudents(const std::string& classId, const std::vector<std::string>& sids, bool deleteHandIns) {
	Teaching* tp = teachingOf(classId);
	if (!tp) return Result::bad("That class isn't on this device.");
	if (sids.empty()) return Result::good();
	Api a;
	if (sids.size() == 1) {
		a = teacherCall(*tp, "DELETE", "/classes/" + classId + "/students/" + sids[0] + (deleteHandIns ? "?submissions=delete" : ""), nullptr);
	} else {
		json::Value body = json::Value::object();
		json::Value ids = json::Value::array();
		for (const std::string& s : sids) ids.push(json::Value::string(s));
		body.set("ids", ids);
		body.set("submissions", json::Value::string(deleteHandIns ? "delete" : "keep"));
		a = teacherCall(*tp, "POST", "/classes/" + classId + "/students/remove", &body);
	}
	if (a.status != 200) return fail(a, "remove");
	for (const std::string& sid : sids) {
		roster_.erase(mapKey(classId, sid));
		for (auto it = subs_.begin(); it != subs_.end();) {
			const std::string& k = it->first;
			const bool theirs = k.compare(0, classId.size() + 1, classId + "/") == 0 && k.size() > sid.size() &&
			                    k.compare(k.size() - sid.size(), sid.size(), sid) == 0;
			if (theirs && deleteHandIns) {
				it = subs_.erase(it);
				continue;
			}
			if (theirs) it->second.left = true;
			++it;
		}
	}
	refreshTeacher(classId);   // the new fetchKey
	save();
	return Result::good();
}

Result Client::refreshSubmissions(const std::string& classId, const std::string& aid) {
	Teaching* tp = teachingOf(classId);
	if (!tp) return Result::bad("That class isn't on this device.");
	refreshStudents(classId);
	tp = teachingOf(classId);
	if (!tp) return Result::bad("That class isn't on this device.");
	Teaching& t = *tp;
	const std::string base = "/classes/" + classId + "/assignments/" + aid + "/submissions";
	const std::string ek = mapKey(classId, aid);
	const Api idx = teacherCall(t, "GET", base, nullptr, subsEtag_[ek]);
	if (idx.status == 304) return Result::good();
	if (idx.status == 404 && idx.error == "no_assignment") return Result::bad(sentence(idx.error, 404, false), 404, idx.error);
	if (idx.status != 200) return fail(idx, "submissions");
	subsEtag_[ek] = idx.etag;
	std::vector<std::string> want;
	std::set<std::string> listed;
	if (const json::Value* subs = idx.body.get("subs"))
		for (const auto& kv : subs->o) {
			const std::string sid = kv.first;
			if (!isUuid(sid)) continue;
			listed.insert(sid);
			const int64_t ver = kv.second.integer("ver");
			const std::string h = kv.second.str("h");
			auto mark = t.subMarks.find(mapKey(aid, sid));
			if (mark != t.subMarks.end()) {
				if (ver < mark->second.first || (ver == mark->second.first && h != mark->second.second)) {
					log.push_back("an older hand-in listed for " + sid + " ignored");   // 4.3: a stale cache or a server that went back
					continue;
				}
				if (ver == mark->second.first) continue;   // have it
			}
			want.push_back(sid);
		}
	// Hand-ins gone from the index (deleted with their student) go here too.
	for (auto it = subs_.begin(); it != subs_.end();) {
		const std::string prefix = classId + "/" + aid + "/";
		if (it->first.compare(0, prefix.size(), prefix) == 0 && !listed.count(it->second.studentId)) it = subs_.erase(it);
		else ++it;
	}
	const Assignment* asg = asgCache_.count(ek) ? &asgCache_[ek] : nullptr;
	for (int rounds = 0; !want.empty() && rounds < 50; rounds++) {
		json::Value body = json::Value::object();
		json::Value ids = json::Value::array();
		const size_t n = std::min<size_t>(want.size(), 50);
		for (size_t i = 0; i < n; i++) ids.push(json::Value::string(want[i]));
		want.erase(want.begin(), want.begin() + (long)n);
		body.set("ids", ids);
		const Api f = teacherCall(t, "POST", base + "/fetch", &body);
		if (f.status != 200) return fail(f, "fetch");
		if (const json::Value* d = f.body.get("deferred"))   // what the server had no time for: asked again
			for (const json::Value& x : d->a)
				if (isUuid(x.s)) want.push_back(x.s);
		if (const json::Value* recs = f.body.get("records"))
			for (const json::Value& r : recs->a) {
				const std::string sid = r.str("studentId");
				if (!isUuid(sid)) continue;
				const int64_t ver = r.integer("ver");
				const Bytes env = fromB64(r.str("env"));
				const std::string h = clsync::envelopeHash(cr_, env);
				Submission s;
				s.studentId = sid;
				s.ver = ver;
				s.attempts = (int)r.integer("attempts");
				s.handedInAt = r.integer("at");
				std::string payload, why;
				json::Value p;
				std::string verdict = "invalid";
				if (open(cr_, curve_, env, "submission", classId, aid + "/" + sid, ver, teacherOpenKey(t), payload, why))
					verdict = readPayload(payload, "submission", p);
				const auto rs = roster_.find(mapKey(classId, sid));
				s.name = rs != roster_.end() ? rs->second.name : std::string();
				s.left = rs == roster_.end();
				if (!verdict.empty()) {
					s.unreadable = true;
					s.problem = verdict == "newer" ? kNewer : (why == "inflate" ? kCantReadBig : kCantRead);
				} else {
					const std::string proof = p.str("proof");
					auto pin = t.proofs.find(sid);
					if (pin == t.proofs.end()) t.proofs[sid] = proof;
					if (pin != t.proofs.end() && pin->second != proof) {
						s.unreadable = true;
						s.problem = kUnverified;   // never opened as content, checked or counted (2.2)
					} else {
						if (s.name.empty()) s.name = cleanName(p.str("name"), 64, "a student");
						s.handedInAt = p.integer("handedInAt");
						s.cdl = p.str("cdl");
						if (tooBig(s.cdl)) {
							s.cdl.clear();
							s.unreadable = true;
							s.problem = kCantReadBig;
						}
					}
				}
				// Check My Circuit, on this device, cached per (aid, sid, h) (4.3).
				if (!s.unreadable && asg && !asg->keyText.empty()) {
					Checked& c = t.checked[mapKey(aid, sid)];
					if (c.h != h) {
						c = Checked();
						c.h = h;
						int verdictCode = -1;
						std::string summary;
						if (hooks_.check && hooks_.check(s.cdl, asg->keyText, asg->keyNames, verdictCode, summary)) {
							c.verdict = verdictCode;
							c.summary = summary;
						}
					}
					s.checkVerdict = c.verdict;
					s.checkSummary = c.summary;
				}
				t.subMarks[mapKey(aid, sid)] = { ver, h };
				subs_[classId + "/" + aid + "/" + sid] = s;
				saveCache(classId, "submissions/" + aid + "/" + sid + ".json", json::write(submissionToJson(s, h)));
			}
	}
	save();
	return Result::good();
}

// ---- Teacher: the live view (4.4) ----------------------------------------------------------

Result Client::liveWrite(Teaching& t, const LiveRec& lr, bool on, int64_t base) {
	const int64_t ver = std::max(t.liveVer, base) + 1;
	Bytes env;
	std::string why;
	if (!seal(cr_, classKeyOf(t), "live", t.classId, lr.session, ver, liveJson(lr), !lr.ended, kMaxRecord, env, why))
		return Result::bad(why == "too big" ? kTooBig : kNoRandom);
	json::Value body = baseRec(base, ver, env);
	body.set("session", json::Value::string(lr.session));
	body.set("on", json::Value::boolean(on));
	body.set("predict", json::Value::boolean(lr.hasPredict));
	const Api a = teacherCall(t, "PUT", "/classes/" + t.classId + "/live", &body);
	if (a.status == 412) {
		// Another device is live (4.4): offer to take over with the current version as the base.
		if (const json::Value* c = a.body.get("current")) t.liveVer = std::max(t.liveVer, c->integer("ver"));
		t.takeOver = true;
		t.liveOn = false;   // this device isn't the one pushing now
		t.pendingRec = lr;
		t.pendingOn = on;
		return Result::bad(kLiveElsewhere, 412, "conflict");
	}
	if (a.status == 413) return Result::bad(kTooBig, 413, a.error);
	if (a.status != 200) return fail(a, "live");
	const bool newQuestion = lr.hasPredict && (!t.live.hasPredict || t.live.prompt != lr.prompt || t.live.session != lr.session ||
	                                           t.live.lights != lr.lights || !t.liveOn);
	// A reveal (or a new circuit while revealed) scores the same answers again: the answers
	// endpoint would answer 304, so its etag goes and the next refresh counts afresh.
	const bool rescore = lr.hasPredict && (lr.reveal != t.live.reveal || (lr.reveal && lr.cdl != t.liveCdl));
	t.liveVer = ver;
	t.live = lr;
	t.liveOn = on;
	t.takeOver = false;
	t.otherLive = false;
	if (!lr.ended) t.liveCdl = lr.cdl;
	if (newQuestion) {
		t.predictSince = ver;
		counts_.erase(t.classId);
		answersEtag_.erase(t.classId);
	} else if (rescore) {
		answersEtag_.erase(t.classId);
	}
	if (!lr.hasPredict) counts_.erase(t.classId);
	save();
	return Result::good();
}

Result Client::goLive(const std::string& classId, const std::string& cdl) {
	Teaching* tp = teachingOf(classId);
	if (!tp) return Result::bad("That class isn't on this device.");
	LiveRec lr;
	lr.session = clsync::newUuid(cr_);
	if (lr.session.empty()) return Result::bad(kNoRandom);
	lr.step = 1;
	lr.cdl = cdl;
	lr.at = serverNow();
	if (tp->otherLive) {
		// Another device is live: ask before taking over (4.4).
		tp->takeOver = true;
		tp->pendingRec = lr;
		tp->pendingOn = true;
		return Result::bad(kLiveElsewhere, 412, "conflict");
	}
	return liveWrite(*tp, lr, true, tp->liveVer);
}

Result Client::push(const std::string& classId, const std::string& cdl, const std::string* prompt, const std::vector<std::string>* lights,
                    bool reveal) {
	Teaching* tp = teachingOf(classId);
	if (!tp) return Result::bad("That class isn't on this device.");
	if (!tp->liveOn) return goLive(classId, cdl);
	LiveRec lr;
	lr.session = tp->live.session;
	lr.step = tp->live.step + 1;
	lr.cdl = cdl;
	lr.at = serverNow();
	if (prompt) {
		lr.hasPredict = true;
		lr.prompt = cutText(*prompt, 500);
		if (lights)
			for (const std::string& l : *lights)
				if (lr.lights.size() < 32) lr.lights.push_back(l);
		lr.reveal = reveal;
	}
	return liveWrite(*tp, lr, true, tp->liveVer);
}

Result Client::endLive(const std::string& classId) {
	Teaching* tp = teachingOf(classId);
	if (!tp) return Result::bad("That class isn't on this device.");
	LiveRec lr;
	lr.session = tp->live.session.empty() ? clsync::newUuid(cr_) : tp->live.session;
	lr.ended = true;
	lr.at = serverNow();
	return liveWrite(*tp, lr, false, tp->liveVer);
}

Result Client::takeOverLive(const std::string& classId) {
	Teaching* tp = teachingOf(classId);
	if (!tp) return Result::bad("That class isn't on this device.");
	if (!tp->takeOver) return Result::good();
	LiveRec lr = tp->pendingRec;
	lr.at = serverNow();
	return liveWrite(*tp, lr, tp->pendingOn, tp->liveVer);
}

Result Client::refreshAnswers(const std::string& classId) {
	Teaching* tp = teachingOf(classId);
	if (!tp) return Result::bad("That class isn't on this device.");
	Teaching& t = *tp;
	if (!t.liveOn || !t.live.hasPredict) return Result::good();
	const Api a = teacherCall(t, "GET", "/classes/" + classId + "/live/answers?session=" + t.live.session, nullptr, answersEtag_[classId]);
	if (a.status == 304) return Result::good();
	if (a.status != 200) return fail(a, "answers");
	answersEtag_[classId] = a.etag;
	AnswerCounts c;
	for (const std::string& l : t.live.lights) c.perLight[l] = { 0, 0 };
	for (const auto& kv : roster_)
		if (kv.first.compare(0, classId.size() + 1, classId + "/") == 0) c.students++;
	std::map<std::string, int> truth;
	const bool scored = t.live.reveal && hooks_.lights && hooks_.lights(t.liveCdl, t.live.lights, truth);
	if (const json::Value* list = a.body.get("answers"))
		for (const json::Value& x : list->a) {
			const std::string sid = x.str("studentId");
			const int64_t ver = x.integer("ver");
			if (!isUuid(sid) || ver < t.predictSince) continue;   // an answer to an earlier question
			std::string payload, why;
			json::Value p;
			if (!open(cr_, curve_, fromB64(x.str("env")), "answer", classId, t.live.session + "/" + sid, ver, teacherOpenKey(t), payload, why) ||
			    !readPayload(payload, "answer", p).empty() || p.str("session") != t.live.session || p.integer("ver") != ver)
				continue;
			const std::string proof = p.str("proof");
			auto pin = t.proofs.find(sid);
			if (pin == t.proofs.end() || pin->second != proof) continue;   // not counted (4.4)
			c.answered++;
			bool right = true;
			const json::Value* lights = p.get("lights");
			for (const std::string& l : t.live.lights) {
				const json::Value* g = lights ? lights->get(l) : nullptr;
				if (!g) {
					right = false;
					continue;
				}
				if (g->n == 1) c.perLight[l].first++;
				else c.perLight[l].second++;
				if (scored && (!truth.count(l) || truth[l] != (int)g->n)) right = false;
			}
			if (scored) (right ? c.right : c.wrong)++;
		}
	counts_[classId] = c;
	answersChanged_.insert(classId);
	return Result::good();
}

// ---- Student: joining (4.5) ------------------------------------------------------------------

Result Client::previewJoinCode(const std::string& text) {
	std::string code, why;
	if (!parseCode(cr_, CodeKind::Join, text, code, why)) return Result::bad(whyText(CodeKind::Join, why, text), 0, why);
	const JoinKeys& jk = joinKeysFor(code);
	if (!jk.valid()) return Result::bad(kNoRandom);
	const Api a = call("GET", cfg_.serverBase + "/join/" + jk.joinId, { { "Authorization", "Bearer " + jk.joinToken } }, nullptr);
	if (a.status != 200) return Result::bad(sentence(a.error, a.status, true), a.status, a.error);
	const std::string classId = a.body.str("classId");
	OpenKey k;
	k.key = jk.joinKey;
	std::string payload;
	json::Value p;
	if (!isHex(classId, 32) || !open(cr_, curve_, fromB64(a.body.str("env")), "join", classId, jk.joinId, a.body.integer("ver"), k, payload, why) ||
	    !readPayload(payload, "join", p).empty() || !validPoint(curve_, fromB64(p.str("pub"))))
		return Result::bad(kJoinUnreadable, 200, "damaged");
	Result r = Result::good(cleanName(p.str("name"), 100, "Untitled class") + "\n" + (a.body.flag("open", true) ? "1" : "0"));
	if (members_.count(classId)) r.message = "You're already in this class on this device.";
	return r;
}

Result Client::join(const std::string& text, const std::string& name0) {
	std::string code, why;
	if (!parseCode(cr_, CodeKind::Join, text, code, why)) return Result::bad(whyText(CodeKind::Join, why, text), 0, why);
	const JoinKeys& jk = joinKeysFor(code);
	if (!jk.valid()) return Result::bad(kNoRandom);
	const Api look = call("GET", cfg_.serverBase + "/join/" + jk.joinId, { { "Authorization", "Bearer " + jk.joinToken } }, nullptr);
	if (look.status != 200) return Result::bad(sentence(look.error, look.status, true), look.status, look.error);
	const std::string classId = look.body.str("classId");
	OpenKey k;
	k.key = jk.joinKey;
	std::string payload;
	json::Value p;
	if (!isHex(classId, 32) ||
	    !open(cr_, curve_, fromB64(look.body.str("env")), "join", classId, jk.joinId, look.body.integer("ver"), k, payload, why) ||
	    !readPayload(payload, "join", p).empty() || !validPoint(curve_, fromB64(p.str("pub"))))
		return Result::bad(kJoinUnreadable, 200, "damaged");
	const std::string className = cleanName(p.str("name"), 100, "Untitled class");
	if (members_.count(classId)) {
		Result r = Result::good(classId);
		r.message = "You're already in this class on this device.";
		return r;
	}
	if (!look.body.flag("open", true))
		return Result::bad("Joining " + quoted(className) + " is closed. Ask your teacher to open it.", 403, "join_closed");
	for (int attempt = 0; attempt < 2; attempt++) {
		Membership m;
		m.classId = classId;
		m.className = className;
		m.classKey = p.str("classKey");
		m.pub = p.str("pub");
		m.name = cleanName(name0, 64, "a student");
		m.studentId = clsync::newUuid(cr_);
		uint8_t tok[32], proof[32];
		if (m.studentId.empty() || !cr_.random(tok, 32) || !cr_.random(proof, 32)) return Result::bad(kNoRandom);
		m.token = b64u(Bytes(tok, tok + 32));
		m.proof = b64u(Bytes(proof, proof + 32));
		memset(tok, 0, sizeof tok);
		m.joinedAt = serverNow();
		Bytes env;
		if (!sealTo(cr_, curve_, fromB64(m.pub), "name", classId, m.studentId, 1, nameJson(m.name, m.joinedAt, m.proof), false, kMaxName, env,
		            why))
			return Result::bad(why == "too big" ? kTooBig : kNoRandom);
		json::Value body = json::Value::object();
		body.set("studentId", json::Value::string(m.studentId));
		body.set("tokenHash", json::Value::string(sha256Hex(cr_, m.token)));
		body.set("name", rec(1, env));
		const Api a = call("POST", cfg_.serverBase + "/classes/" + classId + "/students", { { "Authorization", "Bearer " + jk.joinToken } }, &body);
		if (a.status == 409 && a.error == "student_exists") {
			// A lost answer to our own join, or (practically never) someone else's id (3.3).
			const Api s = studentCall(m, "GET", "/classes/" + classId, nullptr);
			if (s.status != 200) continue;
		} else if (a.status == 403 && a.error == "join_closed") {
			return Result::bad("Joining " + quoted(className) + " is closed. Ask your teacher to open it.", 403, a.error);
		} else if (a.status != 201) {
			return Result::bad(sentence(a.error, a.status, true), a.status, a.error);
		}
		m.joinedAt = a.body.integer("joinedAt", m.joinedAt);
		m.fetchKey = a.body.str("fetchKey");
		members_[classId] = m;
		save();
		refreshMember(classId);
		return Result::good(classId);
	}
	return Result::bad("Try again.");
}

Result Client::rename(const std::string& classId, const std::string& name0) {
	Membership* mp = membershipOf(classId);
	if (!mp) return Result::bad("That class isn't on this device.");
	Membership& m = *mp;
	const std::string name = cleanName(name0, 64, "a student");
	Bytes env;
	std::string why;
	if (!sealTo(cr_, curve_, fromB64(m.pub), "name", classId, m.studentId, m.nameVer + 1, nameJson(name, m.joinedAt, m.proof), false, kMaxName,
	            env, why))
		return Result::bad(kNoRandom);
	json::Value body = json::Value::object();
	body.set("name", rec(m.nameVer + 1, env));
	const Api a = studentCall(m, "PUT", "/classes/" + classId + "/students/" + m.studentId, &body);
	if (a.status == 410) return gone(classId, a, false);
	if (a.status == 403 && a.error == "not_a_member") {
		const std::string text = "You were removed from " + quoted(m.className) + ".";
		forgetMembershipLocal(classId, text);
		return Result::bad(text, 403, a.error);
	}
	if (a.status != 200) return fail(a, "rename");
	m.nameVer++;
	m.name = name;
	save();
	return Result::good();
}

// ---- Student: the class page (4.5, 4.8) -----------------------------------------------------

bool Client::memberStatus(Membership& m, const json::Value& b) {
	const int64_t seq = b.integer("seq");
	if (seq < m.seq) {
		log.push_back("an older status of " + m.classId + " ignored");
		return false;
	}
	const std::string classId = m.classId;
	const std::string fk = b.str("fetchKey");
	if (isHex(fk, 32)) m.fetchKey = fk;
	m.expiresAt = b.integer("expiresAt", m.expiresAt);
	const Bytes classKey = fromB64(m.classKey);
	bool wrongKey = false;
	if (const json::Value* i = b.get("info")) {
		const int64_t ver = i->integer("ver");
		if (ver > m.infoVer) {
			const Api a = call("GET", cfg_.liveBase + "/" + classId + "/" + m.fetchKey + "/info/" + std::to_string(ver), {}, nullptr);
			if (a.status == 200) {
				OpenKey k;
				k.key = classKey;
				std::string payload, why;
				json::Value p;
				if (open(cr_, curve_, fromB64(a.body.str("env")), "info", classId, "info", ver, k, payload, why) &&
				    readPayload(payload, "info", p).empty())
					m.className = cleanName(p.str("name"), 100, "Untitled class");
				m.infoVer = ver;
			} else if (a.error == "wrong_fetch_key") {
				wrongKey = true;
			}
		}
	}
	std::set<std::string> listed;
	if (const json::Value* as = b.get("assignments"))
		for (const auto& kv : as->o) {
			listed.insert(kv.first);
			const json::Value* c = kv.second.get("closesAt");
			closesAt_[mapKey(classId, kv.first)] = c && c->isInt() ? c->i() : -1;
			const int64_t ver = kv.second.integer("ver");
			auto seen = m.seenAssignments.find(kv.first);
			if (seen != m.seenAssignments.end() && seen->second >= ver) continue;   // never back, never twice (4.9)
			const Result r = fetchAssignment(classId, m.fetchKey, classKey, kv.first, ver, kv.second.str("h"), false);
			if (r.error == "wrong_fetch_key") wrongKey = true;
		}
	for (auto it = m.seenAssignments.begin(); it != m.seenAssignments.end();) {
		if (listed.count(it->first)) { ++it; continue; }
		asgCache_.erase(mapKey(classId, it->first));
		if (hooks_.removeTree) hooks_.removeTree(cachePath(classId, "assignments/" + it->first + ".json"));
		it = m.seenAssignments.erase(it);
	}
	if (const json::Value* l = b.get("live")) {
		const int64_t ver = l->integer("ver");
		if (ver > m.liveSeen) {
			const Result r = fetchLive(m, ver);
			if (r.error == "wrong_fetch_key") wrongKey = true;
		}
	}
	if (!wrongKey) m.seq = seq;
	return !wrongKey;
}

Result Client::refreshMember(const std::string& classId) {
	Membership* mp = membershipOf(classId);
	if (!mp) return Result::bad("That class isn't on this device.");
	Membership& m = *mp;
	const Api a = studentCall(m, "GET", "/classes/" + classId, nullptr);
	if (a.status == 403 && a.error == "not_a_member") {
		const std::string text = "You were removed from " + quoted(m.className) + ".";
		forgetMembershipLocal(classId, text);
		return Result::bad(text, 403, a.error);
	}
	if (a.status == 410) return gone(classId, a, false);
	if (a.status == 401) {
		Status s;
		s.kind = Status::Error;
		s.text = sentence("wrong_token", 401, false);
		if (++m.wrongToken >= 2) status_[classId] = s;   // once: a retry (4.11); then the sentence
		return Result::bad(s.text, 401, a.error);
	}
	if (a.status != 200) {
		Status s;
		s.kind = a.status == 0 ? Status::Offline : Status::Error;
		s.text = sentence(a.error, a.status, false);
		status_[classId] = s;
		return fail(a, "status");
	}
	m.wrongToken = 0;
	status_[classId] = Status();
	if (!memberStatus(m, a.body)) {
		// A record path refused: the fetchKey moved on between the two reads. Once more.
		const Api again = studentCall(m, "GET", "/classes/" + classId, nullptr);
		if (again.status == 200) memberStatus(m, again.body);
	}
	save();
	return Result::good();
}

Result Client::fetchLive(Membership& m, int64_t ver) {
	const Api a = call("GET", cfg_.liveBase + "/" + m.classId + "/" + m.fetchKey + "/live/" + std::to_string(ver), {}, nullptr);
	if (a.status != 200) return Result::bad(sentence(a.error, a.status, false), a.status, a.error);
	m.liveSeen = ver;   // fetched once, never again (4.9)
	OpenKey k;
	k.key = fromB64(m.classKey);
	std::string payload, why;
	json::Value p;
	LiveRec lr;
	const std::string session = a.body.str("session");
	if (!isUuid(session) || !open(cr_, curve_, fromB64(a.body.str("env")), "live", m.classId, session, ver, k, payload, why) ||
	    !readPayload(payload, "live", p).empty() || !liveFrom(p, lr) || lr.session != session || (!lr.ended && tooBig(lr.cdl))) {
		note(kLiveUnreadable);   // the view keeps the last good one
		return Result::bad(kLiveUnreadable, 200, "damaged");
	}
	Live& l = lives_[m.classId];
	const bool newSession = l.session != lr.session;
	l.ver = ver;
	l.session = lr.session;
	l.ended = lr.ended;
	l.on = !lr.ended;
	if (!lr.ended) {
		l.cdl = lr.cdl;
		l.step = lr.step;
		l.hasPredict = lr.hasPredict;
		l.prompt = lr.prompt;
		l.lights = lr.lights;
		l.reveal = lr.reveal;
		saveCache(m.classId, "live.cdl", lr.cdl);
	}
	if (newSession) myGuess_.erase(m.classId);
	l.myRight = -1;
	l.myTotal = 0;
	if (l.on && l.reveal && l.hasPredict && myGuess_.count(m.classId)) {
		std::map<std::string, int> truth;
		if (hooks_.lights && hooks_.lights(l.cdl, l.lights, truth)) {
			const auto& mine = myGuess_[m.classId];
			l.myRight = 0;
			for (const std::string& name : l.lights) {
				auto g = mine.find(name);
				if (g == mine.end()) continue;
				l.myTotal++;
				if (truth.count(name) && truth[name] == g->second) l.myRight++;
			}
		}
	}
	liveChanged_.insert(m.classId);
	return Result::good();
}

Result Client::pulse(const std::string& classId) {
	Membership* mp = membershipOf(classId);
	if (!mp) return Result::bad("That class isn't on this device.");
	Membership& m = *mp;
	const Api a = call("GET", cfg_.liveBase + "/" + classId + "/" + m.fetchKey, {}, nullptr);
	if (a.status == 404) {
		// An old fetchKey (a code change or a removal) or no class: the authenticated status says which.
		Result r = refreshMember(classId);
		if (!r.ok || !membershipOf(classId)) return r;
		return sendPending(*membershipOf(classId));
	}
	if (a.status != 200) {
		Status s;
		s.kind = a.status == 0 ? Status::Offline : Status::Error;
		s.text = sentence(a.error, a.status, false);
		status_[classId] = s;
		return fail(a, "pulse");
	}
	status_[classId] = Status();
	const int64_t seq = a.body.integer("seq"), live = a.body.integer("live");
	if (const int64_t p = a.body.integer("p")) pollSeconds_ = std::max<int64_t>(1, std::min<int64_t>(p, 600));
	if (seq < m.seq || (live != 0 && live < m.liveSeen)) {
		log.push_back("an older pulse of " + classId + " ignored");   // a stale CDN copy: normal (4.9)
	} else if (seq > m.seq) {
		refreshMember(classId);
		if (!membershipOf(classId)) return Result::good();
	} else if (live > m.liveSeen) {
		const Result r = fetchLive(m, live);
		if (r.error == "wrong_fetch_key") refreshMember(classId);
	} else if (live == 0 && lives_.count(classId) && lives_[classId].on) {
		// The session ended without our seeing the ended record yet: the status has it.
		refreshMember(classId);
	}
	if (Membership* again = membershipOf(classId)) return sendPending(*again);
	return Result::good();
}

// ---- Student: handing in (4.5) -------------------------------------------------------------

Result Client::handInNow(Membership& m, const std::string& aid, const std::string& cdl, int attempt, bool queueOffline) {
	const std::string classId = m.classId;
	for (int tries = 0; tries < 2; tries++) {
		const int64_t base = m.seenSubmissions.count(aid) ? m.seenSubmissions[aid] : 0;
		const int64_t ver = base + 1;
		const int64_t at = serverNow();
		Bytes env;
		std::string why;
		if (!sealTo(cr_, curve_, fromB64(m.pub), "submission", classId, aid + "/" + m.studentId, ver,
		            submissionJson(m.name, cdl, at, cfg_.client, attempt, m.proof), true, kMaxRecord, env, why))
			return Result::bad(why == "too big" ? kTooBig : kNoRandom, 0, why == "too big" ? "record_too_large" : "rng");
		json::Value body = baseRec(base, ver, env);
		const Api a = studentCall(m, "PUT", "/classes/" + classId + "/assignments/" + aid + "/submissions/" + m.studentId, &body);
		if (a.status == 200 || a.status == 201) {
			m.seenSubmissions[aid] = ver;
			m.attempts[aid] = attempt;
			m.handedInAt[aid] = at;
			m.handedInHash[aid] = clsync::cdlHash(cr_, cdl);
			m.pending.erase(aid);
			save();
			Result r = Result::good();
			r.message = "Handed in.";
			return r;
		}
		if (a.status == 412) {
			// Handed in from another device: its version is the base now. Once (4.5).
			if (const json::Value* c = a.body.get("current")) m.seenSubmissions[aid] = c->integer("ver");
			if (const json::Value* c = a.body.get("current")) attempt = std::max(attempt, (int)c->integer("attempts") + 1);
			continue;
		}
		if (a.status == 0 || a.status == 429 || a.status == 503 || a.status >= 500) {
			if (!queueOffline) return fail(a, "hand in");
			Pending& p = m.pending[aid];   // one per assignment, the newest replacing the older (4.8)
			p.cdl = cdl;
			p.attempt = attempt;
			p.at = at;
			save();
			Result r = Result::good("pending");
			r.message = a.status == 0 ? "Will hand in when you're online." : sentence(a.error, a.status, false);
			r.status = a.status;
			r.error = a.error;
			return r;
		}
		m.pending.erase(aid);
		save();
		if (a.status == 403 && a.error == "not_a_member") {
			const std::string text = "You were removed from " + quoted(m.className) + ".";
			forgetMembershipLocal(classId, text);
			return Result::bad(text, 403, a.error);
		}
		if (a.status == 410) return gone(classId, a, false);
		if (a.status == 409 && a.error == "assignment_closed") {
			const int64_t due = closesAt_.count(mapKey(classId, aid)) ? closesAt_[mapKey(classId, aid)] : -1;
			std::string text = sentence(a.error, 409, false);
			if (due >= 0) text += " It was due " + dateText(due) + ".";
			return Result::bad(text, 409, a.error);
		}
		return fail(a, "hand in");
	}
	return Result::bad(sentence("conflict", 412, false), 412, "conflict");
}

Result Client::handIn(const std::string& classId, const std::string& aid, const std::string& cdl) {
	Membership* mp = membershipOf(classId);
	if (!mp) return Result::bad("That class isn't on this device.");
	const int attempt = (mp->attempts.count(aid) ? mp->attempts[aid] : 0) + 1;
	if (now() < retryAfter_) {
		Pending& p = mp->pending[aid];
		p.cdl = cdl;
		p.attempt = attempt;
		p.at = serverNow();
		save();
		Result r = Result::good("pending");
		r.message = kRateLimited;
		return r;
	}
	return handInNow(*mp, aid, cdl, attempt, true);
}

Result Client::sendPending(Membership& m) {
	if (m.pending.empty() || now() < retryAfter_) return Result::good();
	std::vector<std::pair<int64_t, std::string>> order;
	for (const auto& kv : m.pending) order.emplace_back(kv.second.at, kv.first);
	std::sort(order.begin(), order.end());   // oldest first
	const std::string classId = m.classId;
	for (const auto& o : order) {
		Membership* mp = membershipOf(classId);
		if (!mp || !mp->pending.count(o.second)) continue;
		const Pending p = mp->pending[o.second];
		const Result r = handInNow(*mp, o.second, p.cdl, p.attempt, true);
		if (r.value == "pending") break;   // still offline or waiting
	}
	return Result::good();
}

Result Client::sendAnswer(const std::string& classId, const std::map<std::string, int>& lights) {
	Membership* mp = membershipOf(classId);
	if (!mp) return Result::bad("That class isn't on this device.");
	Membership& m = *mp;
	const auto lv = lives_.find(classId);
	if (lv == lives_.end() || !lv->second.on || !lv->second.hasPredict) return Result::good();   // dropped quietly
	const Live& l = lv->second;
	Bytes env;
	std::string why;
	if (!sealTo(cr_, curve_, fromB64(m.pub), "answer", classId, l.session + "/" + m.studentId, l.ver,
	            answerJson(l.session, l.ver, lights, serverNow(), m.proof), false, kMaxAnswer, env, why))
		return Result::bad(why == "too big" ? kTooBig : kNoRandom);
	json::Value body = json::Value::object();
	body.set("session", json::Value::string(l.session));
	body.set("ver", json::Value::integer(l.ver));
	body.set("env", json::Value::string(b64u(env)));
	const Api a = studentCall(m, "PUT", "/classes/" + classId + "/live/answers/" + m.studentId, &body);
	if (a.status == 409 && a.error == "not_live") return Result::good("dropped");   // the teacher moved on: quietly
	if (a.status != 200) return fail(a, "answer");
	myGuess_[classId] = lights;
	return Result::good();
}

// ---- Student: another device, leaving (4.7) --------------------------------------------------

Result Client::makeMoveCode(const std::string& classId) {
	Membership* mp = membershipOf(classId);
	if (!mp) return Result::bad("That class isn't on this device.");
	Membership& m = *mp;
	for (int attempt = 0; attempt < 2; attempt++) {
		uint8_t secret[16];
		if (!cr_.random(secret, 16)) return Result::bad(kNoRandom);
		const std::string code = encodeCode(cr_, CodeKind::Move, secret);
		const MoveKeys mk = moveKeys(cr_, Bytes(secret, secret + 16));
		memset(secret, 0, sizeof secret);
		MoveRec r;
		r.classId = classId;
		r.studentId = m.studentId;
		r.token = m.token;
		r.proof = m.proof;
		r.classKey = m.classKey;
		r.pub = m.pub;
		r.name = m.name;
		r.className = m.className;
		Bytes env;
		std::string why;
		if (!seal(cr_, mk.moveKey, "move", classId, mk.moveId, 1, moveJson(r), false, kMaxSmall, env, why)) return Result::bad(kNoRandom);
		json::Value body = json::Value::object();
		body.set("classId", json::Value::string(classId));
		body.set("studentId", json::Value::string(m.studentId));
		body.set("env", json::Value::string(b64u(env)));
		const Api a = studentCall(m, "PUT", "/move/" + mk.moveId, &body);
		if (a.status == 409 && a.error == "move_exists") continue;
		if (a.status != 201 && a.status != 200) return fail(a, "move");
		return Result::good(code);
	}
	return Result::bad("Try again.");
}

namespace {
struct Moved {
	MoveRec rec;
	MoveKeys keys;
};
}  // namespace

Result Client::previewMoveCode(const std::string& text) {
	std::string code, why;
	if (!parseCode(cr_, CodeKind::Move, text, code, why)) return Result::bad(whyText(CodeKind::Move, why, text), 0, why);
	Bytes secret;
	decodeCode(cr_, CodeKind::Move, code, secret, why);
	const MoveKeys mk = moveKeys(cr_, secret);
	const Api a = call("GET", cfg_.serverBase + "/move/" + mk.moveId, {}, nullptr);
	if (a.status != 200) return Result::bad(sentence(a.error, a.status, false), a.status, a.error);
	const std::string classId = a.body.str("classId");
	OpenKey k;
	k.key = mk.moveKey;
	std::string payload;
	json::Value p;
	MoveRec r;
	if (!isHex(classId, 32) || !open(cr_, curve_, fromB64(a.body.str("env")), "move", classId, mk.moveId, 1, k, payload, why) ||
	    !readPayload(payload, "move", p).empty() || !moveFrom(p, r) || r.classId != classId || !validPoint(curve_, fromB64(r.pub)))
		return Result::bad(kRecordUnreadable, 200, "damaged");
	Result out = Result::good(r.className + "\n" + r.name);
	if (members_.count(classId)) out.message = "You're already in this class on this device.";
	return out;
}

Result Client::importMoveCode(const std::string& text) {
	std::string code, why;
	if (!parseCode(cr_, CodeKind::Move, text, code, why)) return Result::bad(whyText(CodeKind::Move, why, text), 0, why);
	Bytes secret;
	decodeCode(cr_, CodeKind::Move, code, secret, why);
	const MoveKeys mk = moveKeys(cr_, secret);
	const Api a = call("GET", cfg_.serverBase + "/move/" + mk.moveId, {}, nullptr);
	if (a.status != 200) return Result::bad(sentence(a.error, a.status, false), a.status, a.error);
	const std::string classId = a.body.str("classId");
	OpenKey k;
	k.key = mk.moveKey;
	std::string payload;
	json::Value p;
	MoveRec r;
	if (!isHex(classId, 32) || !open(cr_, curve_, fromB64(a.body.str("env")), "move", classId, mk.moveId, 1, k, payload, why) ||
	    !readPayload(payload, "move", p).empty() || !moveFrom(p, r) || r.classId != classId || !validPoint(curve_, fromB64(r.pub)))
		return Result::bad(kRecordUnreadable, 200, "damaged");
	if (members_.count(classId)) {
		Result out = Result::good(classId);
		out.message = "You're already in this class on this device.";
		return out;
	}
	Membership m;
	m.classId = classId;
	m.studentId = r.studentId;
	m.token = r.token;
	m.proof = r.proof;
	m.classKey = r.classKey;
	m.pub = r.pub;
	m.name = r.name;
	m.className = r.className;
	m.joinedAt = serverNow();
	members_[classId] = m;
	save();
	// The slot has done its job (best effort), then the class's status for its fetchKey.
	studentCall(members_[classId], "DELETE", "/move/" + mk.moveId, nullptr);
	refreshMember(classId);
	return Result::good(classId);
}

void Client::forgetMembershipLocal(const std::string& classId, const std::string& text) {
	members_.erase(classId);
	lives_.erase(classId);
	myGuess_.erase(classId);
	for (auto it = asgCache_.begin(); it != asgCache_.end();)
		it = (!teaching_.count(classId) && it->first.compare(0, classId.size() + 1, classId + "/") == 0) ? asgCache_.erase(it) : std::next(it);
	if (hooks_.removeTree && !teaching_.count(classId)) hooks_.removeTree("cache/" + classId);
	save();
	if (!text.empty()) note(text);
}

void Client::forgetMembership(const std::string& classId) { forgetMembershipLocal(classId, std::string()); }

Result Client::leaveClass(const std::string& classId) {
	Membership* mp = membershipOf(classId);
	if (!mp) return Result::bad("That class isn't on this device.");
	const Api a = studentCall(*mp, "DELETE", "/classes/" + classId + "/students/" + mp->studentId, nullptr);
	if (a.status != 200 && a.status != 403 && a.status != 410) return fail(a, "leave");
	forgetMembershipLocal(classId, std::string());
	return Result::good();
}

// ---- Sync's side records (2.5) --------------------------------------------------------------

void Client::sideChanged() {
	if (!hooks_.sideRecords) return;
	std::set<std::string> present;
	for (const auto& sr : hooks_.sideRecords()) {
		json::Value p;
		if (!readPayload(sr.second, "classroom", p).empty()) continue;
		const std::string classId = p.str("classId");
		present.insert(classId);
		if (Teaching* t = teachingOf(classId)) {
			t->sideRid = sr.first;
			continue;
		}
		if (std::find(removedSide_.begin(), removedSide_.end(), sr.first) != removedSide_.end()) continue;   // removed here
		const Result r = addTeacherKey(p.str("teacherKey"), true);   // "Add a class with a teacher key", quietly (4.1)
		if (Teaching* t = teachingOf(classId)) t->sideRid = sr.first;
		(void)r;
	}
	// A class whose side record went away was deleted on another device: forget it here.
	std::vector<std::string> goneIds;
	for (const auto& kv : teaching_)
		if (!kv.second.sideRid.empty() && !present.count(kv.first)) goneIds.push_back(kv.first);
	for (const std::string& id : goneIds) {
		const std::string name = teaching_[id].rec.name;
		forgetClassLocal(id, false);
		note(quoted(name) + " was deleted by the teacher.");
	}
	save();
}

// ---- Views ---------------------------------------------------------------------------------

std::vector<ClassInfo> Client::classes() const {
	std::vector<ClassInfo> out;
	for (const auto& kv : teaching_) {
		const Teaching& t = kv.second;
		ClassInfo c;
		c.classId = t.classId;
		c.name = t.rec.name;
		c.teaching = true;
		c.teacherKey = t.teacherKey;
		c.joinCode = t.rec.joinCode;
		c.joinOpen = t.rec.joinOpen;
		c.expiresAt = t.expiresAt;
		c.live = t.liveOn;
		if (!t.joinProblem.empty()) c.warning = t.joinProblem;
		else if (t.expiresAt > 0 && t.expiresAt - (hooks_.now ? hooks_.now() + offset_ : 0) < 60 * kDay)
			c.warning = "This class will be removed from the website on " + dateText(t.expiresAt) +
			            " unless someone opens it. Opening it, or a student handing in, keeps it.";
		out.push_back(c);
	}
	for (const auto& kv : members_) {
		const Membership& m = kv.second;
		if (teaching_.count(m.classId)) continue;
		ClassInfo c;
		c.classId = m.classId;
		c.name = m.className;
		c.studentName = m.name;
		c.expiresAt = m.expiresAt;
		auto l = lives_.find(m.classId);
		c.live = l != lives_.end() && l->second.on;
		out.push_back(c);
	}
	std::stable_sort(out.begin(), out.end(), [](const ClassInfo& a, const ClassInfo& b) { return a.name < b.name; });
	return out;
}

std::vector<Assignment> Client::assignments(const std::string& classId) const {
	std::vector<Assignment> out;
	const auto t = teaching_.find(classId);
	const auto m = members_.find(classId);
	const int64_t nowServer = (hooks_.now ? hooks_.now() : 0) + offset_;
	for (const auto& kv : asgCache_) {
		if (kv.first.compare(0, classId.size() + 1, classId + "/") != 0) continue;
		Assignment a = kv.second;
		const auto c = closesAt_.find(kv.first);
		a.closed = c != closesAt_.end() && c->second >= 0 && nowServer > c->second;
		if (t == teaching_.end() && m != members_.end()) {
			const Membership& ms = m->second;
			if (a.keySealed) {
				a.keyText.clear();   // a student never holds a sealed key's text
				a.keyNames.clear();
			}
			auto h = ms.handedInAt.find(a.id);
			if (h != ms.handedInAt.end()) a.handedInAt = h->second;
			auto n = ms.attempts.find(a.id);
			if (n != ms.attempts.end()) a.attempts = n->second;
			a.pending = ms.pending.count(a.id) > 0;
		}
		out.push_back(a);
	}
	std::sort(out.begin(), out.end(), [](const Assignment& a, const Assignment& b) { return a.id < b.id; });
	return out;
}

std::vector<Student> Client::students(const std::string& classId) const {
	std::vector<Student> out;
	for (const auto& kv : roster_)
		if (kv.first.compare(0, classId.size() + 1, classId + "/") == 0) out.push_back(kv.second);
	std::sort(out.begin(), out.end(), [](const Student& a, const Student& b) { return a.joinedAt < b.joinedAt; });
	return out;
}

std::vector<Submission> Client::submissions(const std::string& classId, const std::string& aid) const {
	std::vector<Submission> out;
	const std::string prefix = classId + "/" + aid + "/";
	for (const auto& kv : subs_)
		if (kv.first.compare(0, prefix.size(), prefix) == 0) {
			Submission s = kv.second;
			const auto rs = roster_.find(mapKey(classId, s.studentId));
			s.left = rs == roster_.end();
			if (!s.left && !rs->second.name.empty()) s.name = rs->second.name;
			out.push_back(s);
		}
	std::sort(out.begin(), out.end(), [](const Submission& a, const Submission& b) { return a.name < b.name; });
	return out;
}

Live Client::live(const std::string& classId) const {
	const auto t = teaching_.find(classId);
	if (t != teaching_.end()) {
		Live l;
		l.on = t->second.liveOn;
		l.session = t->second.live.session;
		l.ver = t->second.liveVer;
		l.step = t->second.live.step;
		l.cdl = t->second.liveCdl;
		l.hasPredict = t->second.live.hasPredict;
		l.prompt = t->second.live.prompt;
		l.lights = t->second.live.lights;
		l.reveal = t->second.live.reveal;
		l.ended = t->second.live.ended;
		l.takeOver = t->second.takeOver;
		return l;
	}
	const auto l = lives_.find(classId);
	return l == lives_.end() ? Live() : l->second;
}

AnswerCounts Client::answers(const std::string& classId) const {
	const auto c = counts_.find(classId);
	return c == counts_.end() ? AnswerCounts() : c->second;
}

Status Client::status(const std::string& classId) const {
	const auto s = status_.find(classId);
	return s == status_.end() ? Status() : s->second;
}

bool Client::liveOn(const std::string& classId) const {
	const auto t = teaching_.find(classId);
	if (t != teaching_.end()) return t->second.liveOn;
	const auto l = lives_.find(classId);
	return l != lives_.end() && l->second.on;
}

bool Client::predictOpen(const std::string& classId) const {
	const auto t = teaching_.find(classId);
	return t != teaching_.end() && t->second.liveOn && t->second.live.hasPredict;
}

bool Client::liveChangedFlag(std::string& classId) {
	if (liveChanged_.empty()) return false;
	classId = *liveChanged_.begin();
	liveChanged_.erase(liveChanged_.begin());
	return true;
}

bool Client::answersChangedFlag(std::string& classId) {
	if (answersChanged_.empty()) return false;
	classId = *answersChanged_.begin();
	answersChanged_.erase(answersChanged_.begin());
	return true;
}

// ---- Files (2.4) -------------------------------------------------------------------------------

std::string Client::cachePath(const std::string& classId, const std::string& name) const { return "cache/" + classId + "/" + name; }

void Client::saveCache(const std::string& classId, const std::string& name, const std::string& text) {
	if (hooks_.save) hooks_.save(cachePath(classId, name), text);
}

std::string Client::loadCache(const std::string& classId, const std::string& name) const {
	return hooks_.load ? hooks_.load(cachePath(classId, name)) : std::string();
}

namespace {

json::Value marksJson(const std::map<std::string, std::pair<int64_t, std::string>>& marks) {
	json::Value o = json::Value::object();
	for (const auto& kv : marks) {
		json::Value e = json::Value::object();
		e.set("ver", json::Value::integer(kv.second.first));
		e.set("h", json::Value::string(kv.second.second));
		o.set(kv.first, e);
	}
	return o;
}

void marksFrom(const json::Value* o, std::map<std::string, std::pair<int64_t, std::string>>& marks) {
	if (!o) return;
	for (const auto& kv : o->o) marks[kv.first] = { kv.second.integer("ver"), kv.second.str("h") };
}

json::Value intsJson(const std::map<std::string, int64_t>& m) {
	json::Value o = json::Value::object();
	for (const auto& kv : m) o.set(kv.first, json::Value::integer(kv.second));
	return o;
}

void intsFrom(const json::Value* o, std::map<std::string, int64_t>& m) {
	if (o)
		for (const auto& kv : o->o) m[kv.first] = kv.second.i();
}

json::Value liveRecJson(const LiveRec& l) {
	json::Value o;
	json::parse(liveJson(l), o);
	if (l.ended) return o;
	o.set("cdl", json::Value::string(""));   // the circuit lives in liveCdl
	return o;
}

}  // namespace

void Client::save() {
	if (!hooks_.save) return;
	json::Value t = json::Value::object();
	t.set("v", json::Value::integer(1));
	t.set("deviceId", json::Value::string(deviceId_));
	json::Value classes = json::Value::object();
	for (const auto& kv : teaching_) {
		const Teaching& x = kv.second;
		json::Value c = json::Value::object();
		c.set("teacherKey", json::Value::string(x.teacherKey));
		json::Value rec;
		json::parse(teacherJson(x.rec), rec);
		c.set("record", rec);
		c.set("ver", json::Value::integer(x.ver));
		c.set("h", json::Value::string(x.h));
		json::Value j = json::Value::object();
		j.set("ver", json::Value::integer(x.joinVer));
		j.set("h", json::Value::string(x.joinH));
		c.set("join", j);
		c.set("seq", json::Value::integer(x.seq));
		c.set("assignments", marksJson(x.assignments));
		c.set("liveVer", json::Value::integer(x.liveVer));
		json::Value proofs = json::Value::object();
		for (const auto& p : x.proofs) proofs.set(p.first, json::Value::string(p.second));
		c.set("proofs", proofs);
		json::Value checked = json::Value::object();
		for (const auto& ck : x.checked) {
			json::Value e = json::Value::object();
			e.set("h", json::Value::string(ck.second.h));
			e.set("verdict", json::Value::number(ck.second.verdict));
			e.set("summary", json::Value::string(ck.second.summary));
			checked.set(ck.first, e);
		}
		c.set("checked", checked);
		c.set("expiresWarnedAt", json::Value::integer(x.expiresWarnedAt));
		// beyond 2.4's example: what this device needs to carry on
		c.set("infoVer", json::Value::integer(x.infoVer));
		c.set("fetchKey", json::Value::string(x.fetchKey));
		c.set("expiresAt", json::Value::integer(x.expiresAt));
		c.set("subMarks", marksJson(x.subMarks));
		c.set("joinProblem", json::Value::string(x.joinProblem));
		c.set("sideRid", json::Value::string(x.sideRid));
		c.set("live", liveRecJson(x.live));
		c.set("liveOn", json::Value::boolean(x.liveOn));
		c.set("predictSince", json::Value::integer(x.predictSince));
		classes.set(kv.first, c);
	}
	t.set("classes", classes);
	json::Value removed = json::Value::array();
	for (const std::string& r : removedSide_) removed.push(json::Value::string(r));
	t.set("removed", removed);
	hooks_.save("teaching.json", json::write(t) + "\n");

	json::Value m = json::Value::object();
	m.set("v", json::Value::integer(1));
	json::Value mc = json::Value::object();
	for (const auto& kv : members_) {
		const Membership& x = kv.second;
		json::Value c = json::Value::object();
		c.set("studentId", json::Value::string(x.studentId));
		c.set("token", json::Value::string(x.token));
		c.set("proof", json::Value::string(x.proof));
		c.set("classKey", json::Value::string(x.classKey));
		c.set("pub", json::Value::string(x.pub));
		c.set("fetchKey", json::Value::string(x.fetchKey));
		c.set("name", json::Value::string(x.name));
		c.set("className", json::Value::string(x.className));
		c.set("joinedAt", json::Value::integer(x.joinedAt));
		c.set("seq", json::Value::integer(x.seq));
		json::Value seen = json::Value::object();
		seen.set("live", json::Value::integer(x.liveSeen));
		seen.set("assignments", intsJson(x.seenAssignments));
		seen.set("submissions", intsJson(x.seenSubmissions));
		c.set("seen", seen);
		json::Value pending = json::Value::object();
		for (const auto& p : x.pending) {
			json::Value e = json::Value::object();
			e.set("cdl", json::Value::string(p.second.cdl));
			e.set("attempt", json::Value::integer(p.second.attempt));
			e.set("at", json::Value::integer(p.second.at));
			pending.set(p.first, e);
		}
		c.set("pending", pending);
		c.set("infoVer", json::Value::integer(x.infoVer));
		c.set("expiresAt", json::Value::integer(x.expiresAt));
		c.set("nameVer", json::Value::integer(x.nameVer));
		c.set("handedInAt", intsJson(x.handedInAt));
		std::map<std::string, int64_t> attempts(x.attempts.begin(), x.attempts.end());
		c.set("attempts", intsJson(attempts));
		json::Value hashes = json::Value::object();
		for (const auto& h : x.handedInHash) hashes.set(h.first, json::Value::string(h.second));
		c.set("handedInHash", hashes);
		mc.set(kv.first, c);
	}
	m.set("classes", mc);
	hooks_.save("memberships.json", json::write(m) + "\n");
}

void Client::load() {
	teaching_.clear();
	members_.clear();
	json::Value t, m;
	if (hooks_.load && json::parse(hooks_.load("teaching.json"), t) && t.isObject()) {
		deviceId_ = t.str("deviceId");
		if (const json::Value* cs = t.get("classes"))
			for (const auto& kv : cs->o) {
				const json::Value& c = kv.second;
				Teaching x;
				x.classId = kv.first;
				x.teacherKey = c.str("teacherKey");
				const json::Value* rec = c.get("record");
				json::Value p;
				if (!rec || !readPayload(json::write(*rec), "teacher", p).empty() || !teacherFrom(p, x.rec) || !isHex(x.classId, 32)) continue;
				x.ver = c.integer("ver");
				x.h = c.str("h");
				if (const json::Value* j = c.get("join")) {
					x.joinVer = j->integer("ver");
					x.joinH = j->str("h");
				}
				x.seq = c.integer("seq");
				marksFrom(c.get("assignments"), x.assignments);
				x.liveVer = c.integer("liveVer");
				if (const json::Value* ps = c.get("proofs"))
					for (const auto& pp : ps->o) x.proofs[pp.first] = pp.second.s;
				if (const json::Value* ck = c.get("checked"))
					for (const auto& e : ck->o) {
						Checked k;
						k.h = e.second.str("h");
						const json::Value* v = e.second.get("verdict");
						k.verdict = v && v->isNumber() ? (int)v->n : -1;
						k.summary = e.second.str("summary");
						x.checked[e.first] = k;
					}
				x.expiresWarnedAt = c.integer("expiresWarnedAt");
				x.infoVer = c.integer("infoVer");
				x.fetchKey = c.str("fetchKey");
				x.expiresAt = c.integer("expiresAt");
				marksFrom(c.get("subMarks"), x.subMarks);
				x.joinProblem = c.str("joinProblem");
				x.sideRid = c.str("sideRid");
				if (const json::Value* l = c.get("live")) liveFrom(*l, x.live);
				x.liveOn = c.flag("liveOn");
				x.liveCdl = loadCache(x.classId, "live.cdl");
				x.predictSince = c.integer("predictSince");
				teaching_[x.classId] = x;
			}
		if (const json::Value* r = t.get("removed"))
			for (const json::Value& x : r->a) removedSide_.push_back(x.s);
	}
	if (deviceId_.empty()) deviceId_ = clsync::randomHex(cr_, 16);
	if (hooks_.load && json::parse(hooks_.load("memberships.json"), m) && m.isObject())
		if (const json::Value* cs = m.get("classes"))
			for (const auto& kv : cs->o) {
				const json::Value& c = kv.second;
				Membership x;
				x.classId = kv.first;
				x.studentId = c.str("studentId");
				x.token = c.str("token");
				x.proof = c.str("proof");
				x.classKey = c.str("classKey");
				x.pub = c.str("pub");
				x.fetchKey = c.str("fetchKey");
				x.name = c.str("name");
				x.className = c.str("className");
				x.joinedAt = c.integer("joinedAt");
				x.seq = c.integer("seq");
				if (const json::Value* seen = c.get("seen")) {
					x.liveSeen = seen->integer("live");
					intsFrom(seen->get("assignments"), x.seenAssignments);
					intsFrom(seen->get("submissions"), x.seenSubmissions);
				}
				if (const json::Value* ps = c.get("pending"))
					for (const auto& p : ps->o) {
						Pending pe;
						pe.cdl = p.second.str("cdl");
						pe.attempt = (int)p.second.integer("attempt");
						pe.at = p.second.integer("at");
						x.pending[p.first] = pe;
					}
				x.infoVer = c.integer("infoVer");
				x.expiresAt = c.integer("expiresAt");
				x.nameVer = c.integer("nameVer", 1);
				intsFrom(c.get("handedInAt"), x.handedInAt);
				std::map<std::string, int64_t> attempts;
				intsFrom(c.get("attempts"), attempts);
				for (const auto& a : attempts) x.attempts[a.first] = (int)a.second;
				if (const json::Value* hs = c.get("handedInHash"))
					for (const auto& h : hs->o) x.handedInHash[h.first] = h.second.s;
				if (!isHex(x.classId, 32) || !isUuid(x.studentId)) continue;
				members_[x.classId] = x;
			}
	// The caches: assignments (both sides), the teacher's opened hand-ins, the last live circuit.
	auto loadAssignments = [&](const std::string& classId, const std::vector<std::string>& aids) {
		for (const std::string& aid : aids) {
			json::Value o;
			if (json::parse(loadCache(classId, "assignments/" + aid + ".json"), o) && o.isObject())
				asgCache_[mapKey(classId, aid)] = assignmentOfJson(o);
		}
	};
	for (const auto& kv : teaching_) {
		std::vector<std::string> aids;
		for (const auto& a : kv.second.assignments) aids.push_back(a.first);
		loadAssignments(kv.first, aids);
		for (const auto& sm : kv.second.subMarks) {
			const size_t slash = sm.first.find('/');
			if (slash == std::string::npos) continue;
			json::Value o;
			if (json::parse(loadCache(kv.first, "submissions/" + sm.first + ".json"), o) && o.isObject())
				subs_[kv.first + "/" + sm.first] = submissionOfJson(o);
		}
	}
	for (const auto& kv : members_) {
		std::vector<std::string> aids;
		for (const auto& a : kv.second.seenAssignments) aids.push_back(a.first);
		loadAssignments(kv.first, aids);
		const std::string cdl = loadCache(kv.first, "live.cdl");
		if (!cdl.empty()) {
			Live& l = lives_[kv.first];
			l.cdl = cdl;
			l.ver = kv.second.liveSeen;
		}
	}
}

}  // namespace clclass
