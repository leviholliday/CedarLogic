// The classroom core's C interface (include/CedarClassroom.h) over
// clclass::Engine: the C hooks become Crypto, Curve and Host; lists are read
// through count/item getters over a copy taken by the count call.

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS   // getenv, strncpy are used with care
#endif

#include "CedarClassroom.h"
#include "ClassroomInternal.h"

#include <cstdlib>
#include <cstring>

namespace {

using namespace clclass;
using clsync::Bytes;

struct HookCrypto : clsync::Crypto {
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
		plain.assign(ctTag.size() - 16 + 1, 0);
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

struct HookCurve : Curve {
	CLClassroomHooks h;
	explicit HookCurve(const CLClassroomHooks& hooks) : h(hooks) {}
	bool p256Generate(uint8_t d[32], uint8_t pub[65]) override { return h.p256_generate && h.p256_generate(h.ctx, d, pub); }
	bool p256Public(const uint8_t d[32], uint8_t pub[65]) override { return h.p256_public && h.p256_public(h.ctx, d, pub); }
	bool p256Ecdh(const uint8_t d[32], const uint8_t peer[65], uint8_t x[32]) override {
		return h.p256_ecdh && peer[0] == 4 && h.p256_ecdh(h.ctx, d, peer, x);
	}
	bool pbkdf2Sha256(clsync::Crypto& cr, const uint8_t* pw, size_t pwLen, const uint8_t* salt, size_t saltLen, uint32_t rounds,
	                  uint8_t out[32]) override {
		if (h.pbkdf2_sha256) return h.pbkdf2_sha256(h.ctx, pw, pwLen, salt, saltLen, rounds, out);
		return Curve::pbkdf2Sha256(cr, pw, pwLen, salt, saltLen, rounds, out);
	}
};

void runFunction(void* arg) { (*static_cast<const std::function<void()>*>(arg))(); }

std::string orEmpty(const char* s) { return s ? std::string(s) : std::string(); }

clsync::HttpResponse hookHttp(const CLSyncHooks& h, const clsync::HttpRequest& r) {
	clsync::HttpResponse out;
	if (!h.http) return out;
	std::string headers;
	for (const auto& kv : r.headers) headers += kv.first + ": " + kv.second + "\r\n";
	int status = 0;
	bool sent = false;
	char* respHeaders = nullptr;
	uint8_t* respBody = nullptr;
	size_t respLen = 0;
	h.http(h.ctx, r.method.c_str(), r.url.c_str(), headers.c_str(), (const uint8_t*)r.body.data(), r.body.size(), &status, &sent, &respHeaders,
	       &respBody, &respLen);
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

struct HookHost : Host {
	CLClassroomHooks h;
	CLSyncHooks s;
	explicit HookHost(const CLClassroomHooks& hooks) : h(hooks) {
		memset(&s, 0, sizeof s);
		if (hooks.sync) s = *hooks.sync;
	}
	clsync::HttpResponse http(const clsync::HttpRequest& r) override { return hookHttp(s, r); }
	void onMain(const std::function<void()>& fn) override {
		if (s.on_main) s.on_main(s.ctx, runFunction, (void*)&fn);
		else fn();
	}
	std::string loadFile(const std::string& name) override {
		if (!h.load_file) return std::string();
		char* t = h.load_file(h.ctx, name.c_str());
		if (!t) return std::string();
		std::string out = t;
		free(t);
		return out;
	}
	bool saveFile(const std::string& name, const std::string& text) override { return h.save_file && h.save_file(h.ctx, name.c_str(), text.c_str()); }
	void removeTree(const std::string& name) override {
		if (h.remove_tree) h.remove_tree(h.ctx, name.c_str());
	}
	bool tryLock(const std::string& path) override { return !h.try_lock || h.try_lock(h.ctx, path.c_str()); }
	void unlock() override {
		if (h.unlock) h.unlock(h.ctx);
	}
	void classesChanged() override {
		if (h.classes_changed) h.classes_changed(h.ctx);
	}
	void liveChanged(const std::string& classId, const Live&) override {
		if (h.live_changed) h.live_changed(h.ctx, classId.c_str());
	}
	void answersChanged(const std::string& classId, const AnswerCounts&) override {
		if (h.answers_changed) h.answers_changed(h.ctx, classId.c_str());
	}
	void statusChanged(const std::string& classId, const Status&) override {
		if (h.status_changed) h.status_changed(h.ctx, classId.c_str());
	}
	void notice(const std::string& text) override {
		if (h.notice) h.notice(h.ctx, text.c_str());
	}
	std::vector<std::pair<std::string, std::string>> syncSideRecords() override {
		std::vector<std::pair<std::string, std::string>> out;
		if (!h.sync_side_count || !h.sync_side) return out;
		const int n = h.sync_side_count(h.ctx);
		for (int i = 0; i < n; i++) {
			const char* rid = nullptr;
			const char* json = h.sync_side(h.ctx, i, &rid);
			if (json && rid) out.emplace_back(rid, json);
		}
		return out;
	}
	void syncPutSide(const std::string& rid, const std::string& json) override {
		if (h.sync_put_side) h.sync_put_side(h.ctx, rid.c_str(), json.c_str());
	}
	void syncDeleteSide(const std::string& rid) override {
		if (h.sync_delete_side) h.sync_delete_side(h.ctx, rid.c_str());
	}
	bool checkCircuit(const std::string& cdl, const std::string& keyText, const std::string& keyNames, int& verdict,
	                  std::string& summary) override {
		if (!h.check_circuit) return false;
		char* text = nullptr;
		const bool ok = h.check_circuit(h.ctx, cdl.c_str(), keyText.c_str(), keyNames.c_str(), &verdict, &text);
		summary = text ? text : "";
		free(text);
		return ok;
	}
	bool lightsOf(const std::string& cdl, const std::vector<std::string>& lights, std::map<std::string, int>& values) override {
		if (!h.lights_of || lights.empty()) return false;
		std::vector<const char*> names;
		for (const std::string& l : lights) names.push_back(l.c_str());
		std::vector<int> v(lights.size(), 0);
		if (!h.lights_of(h.ctx, cdl.c_str(), names.data(), (int)names.size(), v.data())) return false;
		for (size_t i = 0; i < lights.size(); i++) values[lights[i]] = v[i] ? 1 : 0;
		return true;
	}
	bool socketOpen(int id, const std::string& url, const std::vector<std::pair<std::string, std::string>>& headers,
	                SocketEvents& events) override {
		if (!h.socket_open) return false;
		std::string lines;
		for (const auto& kv : headers) lines += kv.first + ": " + kv.second + "\r\n";
		return h.socket_open(h.ctx, id, url.c_str(), lines.c_str(), &events);
	}
	void socketSend(int id, const std::string& text) override {
		if (h.socket_send) h.socket_send(h.ctx, id, text.c_str());
	}
	void socketClose(int id, int code) override {
		if (h.socket_close) h.socket_close(h.ctx, id, code);
	}
	void submissionsChanged(const std::string& classId, const std::string& aid) override {
		if (h.submissions_changed) h.submissions_changed(h.ctx, classId.c_str(), aid.c_str());
	}
};

// An override of a server for testing (3.2): https, or http only for this computer.
std::string urlFromEnvironment(const char* var) {
	const char* u = getenv(var);
	const std::string url = u ? u : "";
	if (url.compare(0, 8, "https://") == 0) return url;
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

CodeKind kindOf(int k) { return k == 1 ? CodeKind::Join : k == 2 ? CodeKind::Move : CodeKind::Teacher; }

}  // namespace

struct CLClassroom {
	HookCrypto crypto;
	HookCurve curve;
	HookHost host;
	Config cfg;
	std::unique_ptr<Engine> engine;
	std::vector<ClassInfo> classes;
	std::vector<Assignment> assignments;
	std::vector<Student> students;
	std::vector<Submission> submissions;
	Live live;
	AnswerCounts answers;
	std::string text;   // a string returned by a getter that has no list behind it
	CLClassroom(const CLClassroomHooks& h, const CLSyncHooks& s) : crypto(s), curve(h), host(h) {}
};

namespace {

const CLSyncHooks kNoSync = {};

std::function<void(bool, std::string, std::string)> doneOf(CLClassroomDone done, void* ctx) {
	return [done, ctx](bool ok, std::string message, std::string result) {
		if (done) done(ctx, ok, message.c_str(), result.c_str());
	};
}
Done plainOf(CLClassroomDone done, void* ctx) {
	return [done, ctx](bool ok, const std::string& message) {
		if (done) done(ctx, ok, message.c_str(), "");
	};
}
bool in(int i, size_t n) { return i >= 0 && (size_t)i < n; }

}  // namespace

extern "C" {

CLClassroom* cl_classroom_create(const CLClassroomHooks* hooks, const char* dir, const char* appKey, const char* client) {
	if (!hooks) return nullptr;
	CLClassroom* c = new CLClassroom(*hooks, hooks->sync ? *hooks->sync : kNoSync);
	c->cfg.dir = orEmpty(dir);
	c->cfg.appKey = orEmpty(appKey);
	c->cfg.client = orEmpty(client);
	std::string service = urlFromEnvironment("CL_CLASSROOM_SERVICE");   // the service's origin (3.13)
	while (!service.empty() && service.back() == '/') service.pop_back();
	if (!service.empty()) {
		c->cfg.serverBase = service + "/api/classroom/v1";
		c->cfg.liveBase = service + "/api/live/v1";
	}
	const std::string server = urlFromEnvironment("CL_CLASSROOM_URL"), live = urlFromEnvironment("CL_LIVE_URL");
	if (!server.empty()) c->cfg.serverBase = server;
	if (!live.empty()) c->cfg.liveBase = live;
	return c;
}

void cl_classroom_set_device_name(CLClassroom* c, const char* name) {
	if (c && !c->engine) c->cfg.deviceName = orEmpty(name);
}

void cl_classroom_destroy(CLClassroom* c) {
	if (!c) return;
	const int before = detachedEngines();
	c->engine.reset();
	// A thread left running keeps using the hooks it was given: keep them too.
	if (detachedEngines() != before) return;
	delete c;
}

void cl_classroom_start(CLClassroom* c) {
	if (!c || c->engine) return;
	c->engine.reset(new Engine(c->cfg, c->crypto, c->curve, c->host));
	c->engine->start();
}

int cl_classroom_class_count(CLClassroom* c) {
	if (!c || !c->engine) return 0;
	c->classes = c->engine->classes();
	return (int)c->classes.size();
}
#define CLASS_FIELD(name, type, expr, fallback) \
	type cl_classroom_class_##name(CLClassroom* c, int i) { return c && in(i, c->classes.size()) ? expr : fallback; }
CLASS_FIELD(id, const char*, c->classes[(size_t)i].classId.c_str(), "")
CLASS_FIELD(name, const char*, c->classes[(size_t)i].name.c_str(), "")
CLASS_FIELD(teaching, bool, c->classes[(size_t)i].teaching, false)
CLASS_FIELD(teacher_key, const char*, c->classes[(size_t)i].teacherKey.c_str(), "")
CLASS_FIELD(join_code, const char*, c->classes[(size_t)i].joinCode.c_str(), "")
CLASS_FIELD(join_open, bool, c->classes[(size_t)i].joinOpen, false)
CLASS_FIELD(student_name, const char*, c->classes[(size_t)i].studentName.c_str(), "")
CLASS_FIELD(expires_at, int64_t, c->classes[(size_t)i].expiresAt, 0)
CLASS_FIELD(live, bool, c->classes[(size_t)i].live, false)
CLASS_FIELD(warning, const char*, c->classes[(size_t)i].warning.c_str(), "")
#undef CLASS_FIELD

int cl_classroom_status_kind(CLClassroom* c, const char* classId) {
	return c && c->engine ? (int)c->engine->status(orEmpty(classId)).kind : 0;
}
const char* cl_classroom_status_text(CLClassroom* c, const char* classId) {
	if (!c || !c->engine) return "";
	c->text = c->engine->status(orEmpty(classId)).text;
	return c->text.c_str();
}

void cl_classroom_create_class(CLClassroom* c, const char* name, CLClassroomDone done, void* ctx) {
	if (c && c->engine) c->engine->createClass(orEmpty(name), doneOf(done, ctx));
}
void cl_classroom_preview_teacher_key(CLClassroom* c, const char* text, CLClassroomDone done, void* ctx) {
	if (c && c->engine) c->engine->previewTeacherKey(orEmpty(text), doneOf(done, ctx));
}
void cl_classroom_add_teacher_key(CLClassroom* c, const char* text, CLClassroomDone done, void* ctx) {
	if (c && c->engine) c->engine->addTeacherKey(orEmpty(text), plainOf(done, ctx));
}
void cl_classroom_rename_class(CLClassroom* c, const char* classId, const char* name, CLClassroomDone done, void* ctx) {
	if (c && c->engine) c->engine->renameClass(orEmpty(classId), orEmpty(name), plainOf(done, ctx));
}
void cl_classroom_set_join_open(CLClassroom* c, const char* classId, bool open, CLClassroomDone done, void* ctx) {
	if (c && c->engine) c->engine->setJoinOpen(orEmpty(classId), open, plainOf(done, ctx));
}
void cl_classroom_new_join_code(CLClassroom* c, const char* classId, CLClassroomDone done, void* ctx) {
	if (c && c->engine) c->engine->newJoinCode(orEmpty(classId), plainOf(done, ctx));
}
void cl_classroom_forget_class(CLClassroom* c, const char* classId) {
	if (c && c->engine) c->engine->forgetClass(orEmpty(classId));
}
void cl_classroom_delete_class(CLClassroom* c, const char* classId, CLClassroomDone done, void* ctx) {
	if (c && c->engine) c->engine->deleteClass(orEmpty(classId), plainOf(done, ctx));
}

int cl_classroom_assignment_count(CLClassroom* c, const char* classId) {
	if (!c || !c->engine) return 0;
	c->assignments = c->engine->assignments(orEmpty(classId));
	return (int)c->assignments.size();
}
#define ASG_FIELD(name, type, expr, fallback) \
	type cl_classroom_assignment_##name(CLClassroom* c, int i) { return c && in(i, c->assignments.size()) ? expr : fallback; }
ASG_FIELD(id, const char*, c->assignments[(size_t)i].id.c_str(), "")
ASG_FIELD(title, const char*, c->assignments[(size_t)i].title.c_str(), "")
ASG_FIELD(instructions, const char*, c->assignments[(size_t)i].instructions.c_str(), "")
ASG_FIELD(cdl, const char*, c->assignments[(size_t)i].cdl.c_str(), "")
ASG_FIELD(due_at, int64_t, c->assignments[(size_t)i].dueAt, -1)
ASG_FIELD(close_after_due, bool, c->assignments[(size_t)i].closeAfterDue, false)
ASG_FIELD(ver, int64_t, c->assignments[(size_t)i].ver, 0)
ASG_FIELD(key_text, const char*, c->assignments[(size_t)i].keyText.c_str(), "")
ASG_FIELD(key_names, const char*, c->assignments[(size_t)i].keyNames.c_str(), "")
ASG_FIELD(key_sealed, bool, c->assignments[(size_t)i].keySealed, false)
ASG_FIELD(handed_in_at, int64_t, c->assignments[(size_t)i].handedInAt, 0)
ASG_FIELD(attempts, int, c->assignments[(size_t)i].attempts, 0)
ASG_FIELD(changed_since, bool, c->assignments[(size_t)i].changedSince, false)
ASG_FIELD(pending, bool, c->assignments[(size_t)i].pending, false)
ASG_FIELD(closed, bool, c->assignments[(size_t)i].closed, false)
ASG_FIELD(unreadable, bool, c->assignments[(size_t)i].unreadable, false)
ASG_FIELD(problem, const char*, c->assignments[(size_t)i].problem.c_str(), "")
#undef ASG_FIELD

void cl_classroom_post_assignment(CLClassroom* c, const char* classId, const char* aidOrNull, const char* title, const char* instructions,
                                  int64_t dueAt, bool closeAfterDue, const char* cdl, const char* keyText, const char* keyNames,
                                  bool studentsCanCheck, CLClassroomDone done, void* ctx) {
	if (!c || !c->engine) return;
	Assignment a;
	a.id = orEmpty(aidOrNull);
	a.title = orEmpty(title);
	a.instructions = orEmpty(instructions);
	a.dueAt = dueAt;
	a.closeAfterDue = closeAfterDue;
	a.cdl = orEmpty(cdl);
	a.keyText = orEmpty(keyText);
	a.keyNames = orEmpty(keyNames);
	c->engine->postAssignment(orEmpty(classId), a, studentsCanCheck, plainOf(done, ctx));
}
void cl_classroom_delete_assignment(CLClassroom* c, const char* classId, const char* aid, CLClassroomDone done, void* ctx) {
	if (c && c->engine) c->engine->deleteAssignment(orEmpty(classId), orEmpty(aid), plainOf(done, ctx));
}

int cl_classroom_student_count(CLClassroom* c, const char* classId) {
	if (!c || !c->engine) return 0;
	c->students = c->engine->students(orEmpty(classId));
	return (int)c->students.size();
}
#define STU_FIELD(name, type, expr, fallback) \
	type cl_classroom_student_##name(CLClassroom* c, int i) { return c && in(i, c->students.size()) ? expr : fallback; }
STU_FIELD(id, const char*, c->students[(size_t)i].studentId.c_str(), "")
STU_FIELD(name, const char*, c->students[(size_t)i].name.c_str(), "")
STU_FIELD(joined_at, int64_t, c->students[(size_t)i].joinedAt, 0)
STU_FIELD(seen_at, int64_t, c->students[(size_t)i].seenAt, 0)
STU_FIELD(unreadable, bool, c->students[(size_t)i].unreadable, false)
#undef STU_FIELD

void cl_classroom_refresh_students(CLClassroom* c, const char* classId, CLClassroomDone done, void* ctx) {
	if (c && c->engine) c->engine->refreshStudents(orEmpty(classId), plainOf(done, ctx));
}
void cl_classroom_remove_students(CLClassroom* c, const char* classId, const char* sids, bool deleteHandIns, CLClassroomDone done, void* ctx) {
	if (!c || !c->engine) return;
	std::vector<std::string> ids;
	const std::string all = orEmpty(sids);
	size_t at = 0;
	while (at <= all.size()) {
		size_t end = all.find('\n', at);
		if (end == std::string::npos) end = all.size();
		if (end > at) ids.push_back(all.substr(at, end - at));
		at = end + 1;
	}
	c->engine->removeStudents(orEmpty(classId), ids, deleteHandIns, plainOf(done, ctx));
}
void cl_classroom_remove_student(CLClassroom* c, const char* classId, const char* sid, bool deleteHandIns, CLClassroomDone done, void* ctx) {
	if (c && c->engine) c->engine->removeStudents(orEmpty(classId), { orEmpty(sid) }, deleteHandIns, plainOf(done, ctx));
}

int cl_classroom_submission_count(CLClassroom* c, const char* classId, const char* aid) {
	if (!c || !c->engine) return 0;
	c->submissions = c->engine->submissions(orEmpty(classId), orEmpty(aid));
	return (int)c->submissions.size();
}
#define SUB_FIELD(name, type, expr, fallback) \
	type cl_classroom_submission_##name(CLClassroom* c, int i) { return c && in(i, c->submissions.size()) ? expr : fallback; }
SUB_FIELD(student_id, const char*, c->submissions[(size_t)i].studentId.c_str(), "")
SUB_FIELD(name, const char*, c->submissions[(size_t)i].name.c_str(), "")
SUB_FIELD(cdl, const char*, c->submissions[(size_t)i].cdl.c_str(), "")
SUB_FIELD(handed_in_at, int64_t, c->submissions[(size_t)i].handedInAt, 0)
SUB_FIELD(attempts, int, c->submissions[(size_t)i].attempts, 0)
SUB_FIELD(check_verdict, int, c->submissions[(size_t)i].checkVerdict, -1)
SUB_FIELD(check_summary, const char*, c->submissions[(size_t)i].checkSummary.c_str(), "")
SUB_FIELD(unreadable, bool, c->submissions[(size_t)i].unreadable, false)
SUB_FIELD(problem, const char*, c->submissions[(size_t)i].problem.c_str(), "")
SUB_FIELD(left, bool, c->submissions[(size_t)i].left, false)
#undef SUB_FIELD

void cl_classroom_refresh_submissions(CLClassroom* c, const char* classId, const char* aid, CLClassroomDone done, void* ctx) {
	if (c && c->engine) c->engine->refreshSubmissions(orEmpty(classId), orEmpty(aid), plainOf(done, ctx));
}

void cl_classroom_go_live(CLClassroom* c, const char* classId, const char* cdl, CLClassroomDone done, void* ctx) {
	if (c && c->engine) c->engine->goLive(orEmpty(classId), orEmpty(cdl), plainOf(done, ctx));
}
void cl_classroom_push(CLClassroom* c, const char* classId, const char* cdl, const char* promptOrNull, const char* const* lights, int lightCount,
                       bool reveal, CLClassroomDone done, void* ctx) {
	if (!c || !c->engine) return;
	const std::string prompt = orEmpty(promptOrNull);
	std::vector<std::string> names;
	for (int i = 0; lights && i < lightCount; i++) names.push_back(orEmpty(lights[i]));
	c->engine->push(orEmpty(classId), orEmpty(cdl), promptOrNull ? &prompt : nullptr, &names, reveal, plainOf(done, ctx));
}
void cl_classroom_end_live(CLClassroom* c, const char* classId, CLClassroomDone done, void* ctx) {
	if (c && c->engine) c->engine->endLive(orEmpty(classId), plainOf(done, ctx));
}
void cl_classroom_take_over_live(CLClassroom* c, const char* classId, CLClassroomDone done, void* ctx) {
	if (c && c->engine) c->engine->takeOverLive(orEmpty(classId), plainOf(done, ctx));
}

#define LIVE_FIELD(name, type, expr, fallback)                                   \
	type cl_classroom_live_##name(CLClassroom* c, const char* classId) {          \
		if (!c || !c->engine) return fallback;                                    \
		c->live = c->engine->live(orEmpty(classId));                             \
		return expr;                                                              \
	}
LIVE_FIELD(on, bool, c->live.on, false)
LIVE_FIELD(ended, bool, c->live.ended, false)
LIVE_FIELD(reveal, bool, c->live.reveal, false)
LIVE_FIELD(take_over, bool, c->live.takeOver, false)
LIVE_FIELD(session, const char*, c->live.session.c_str(), "")
LIVE_FIELD(cdl, const char*, c->live.cdl.c_str(), "")
LIVE_FIELD(ver, int64_t, c->live.ver, 0)
LIVE_FIELD(step, int, c->live.step, 0)
LIVE_FIELD(has_predict, bool, c->live.hasPredict, false)
LIVE_FIELD(prompt, const char*, c->live.prompt.c_str(), "")
LIVE_FIELD(light_count, int, (int)c->live.lights.size(), 0)
LIVE_FIELD(my_right, int, c->live.myRight, -1)
LIVE_FIELD(my_total, int, c->live.myTotal, 0)
#undef LIVE_FIELD
const char* cl_classroom_live_light(CLClassroom* c, const char* classId, int i) {
	if (!c || !c->engine) return "";
	c->live = c->engine->live(orEmpty(classId));
	return in(i, c->live.lights.size()) ? c->live.lights[(size_t)i].c_str() : "";
}

#define ANSWERS_FIELD(name, expr)                                                \
	int cl_classroom_answers_##name(CLClassroom* c, const char* classId) {       \
		if (!c || !c->engine) return 0;                                           \
		c->answers = c->engine->answers(orEmpty(classId));                       \
		return expr;                                                              \
	}
ANSWERS_FIELD(answered, c->answers.answered)
ANSWERS_FIELD(students, c->answers.students)
ANSWERS_FIELD(right, c->answers.right)
ANSWERS_FIELD(wrong, c->answers.wrong)
ANSWERS_FIELD(light_count, (int)c->answers.perLight.size())
#undef ANSWERS_FIELD
const char* cl_classroom_answers_light(CLClassroom* c, const char* classId, int i, int* ones, int* zeros) {
	if (ones) *ones = 0;
	if (zeros) *zeros = 0;
	if (!c || !c->engine) return "";
	c->answers = c->engine->answers(orEmpty(classId));
	int at = 0;
	for (const auto& kv : c->answers.perLight) {
		if (at++ != i) continue;
		if (ones) *ones = kv.second.first;
		if (zeros) *zeros = kv.second.second;
		c->text = kv.first;
		return c->text.c_str();
	}
	return "";
}

void cl_classroom_preview_join_code(CLClassroom* c, const char* text, CLClassroomDone done, void* ctx) {
	if (!c || !c->engine) return;
	c->engine->previewJoinCode(orEmpty(text), [done, ctx](bool ok, std::string message, std::string name, bool open) {
		const std::string result = ok ? name + "\n" + (open ? "1" : "0") : std::string();
		if (done) done(ctx, ok, message.c_str(), result.c_str());
	});
}
void cl_classroom_join(CLClassroom* c, const char* text, const char* name, CLClassroomDone done, void* ctx) {
	if (c && c->engine) c->engine->join(orEmpty(text), orEmpty(name), doneOf(done, ctx));
}
void cl_classroom_rename(CLClassroom* c, const char* classId, const char* name, CLClassroomDone done, void* ctx) {
	if (c && c->engine) c->engine->rename(orEmpty(classId), orEmpty(name), plainOf(done, ctx));
}
void cl_classroom_hand_in(CLClassroom* c, const char* classId, const char* aid, const char* cdl, CLClassroomDone done, void* ctx) {
	if (c && c->engine) c->engine->handIn(orEmpty(classId), orEmpty(aid), orEmpty(cdl), plainOf(done, ctx));
}
void cl_classroom_follow(CLClassroom* c, const char* classId, bool following) {
	if (c && c->engine) c->engine->follow(orEmpty(classId), following);
}
void cl_classroom_send_answer(CLClassroom* c, const char* classId, const char* const* lights, const int* values, int n, CLClassroomDone done,
                              void* ctx) {
	if (!c || !c->engine) return;
	std::map<std::string, int> guesses;
	for (int i = 0; lights && values && i < n; i++) guesses[orEmpty(lights[i])] = values[i] ? 1 : 0;
	c->engine->sendAnswer(orEmpty(classId), guesses, plainOf(done, ctx));
}
void cl_classroom_make_move_code(CLClassroom* c, const char* classId, CLClassroomDone done, void* ctx) {
	if (c && c->engine) c->engine->makeMoveCode(orEmpty(classId), doneOf(done, ctx));
}
void cl_classroom_preview_move_code(CLClassroom* c, const char* text, CLClassroomDone done, void* ctx) {
	if (!c || !c->engine) return;
	c->engine->previewMoveCode(orEmpty(text), [done, ctx](bool ok, std::string message, std::string className, std::string student) {
		const std::string result = ok ? className + "\n" + student : std::string();
		if (done) done(ctx, ok, message.c_str(), result.c_str());
	});
}
void cl_classroom_import_move_code(CLClassroom* c, const char* text, CLClassroomDone done, void* ctx) {
	if (c && c->engine) c->engine->importMoveCode(orEmpty(text), plainOf(done, ctx));
}
void cl_classroom_leave_class(CLClassroom* c, const char* classId, CLClassroomDone done, void* ctx) {
	if (c && c->engine) c->engine->leaveClass(orEmpty(classId), plainOf(done, ctx));
}
void cl_classroom_forget_membership(CLClassroom* c, const char* classId) {
	if (c && c->engine) c->engine->forgetMembership(orEmpty(classId));
}

const char* cl_classroom_live_connection(CLClassroom* c, const char* classId) {
	if (!c || !c->engine) return "";
	c->text = c->engine->liveConnection(orEmpty(classId));
	return c->text.c_str();
}

void cl_classroom_socket_opened(void* events, int id) {
	if (events) static_cast<SocketEvents*>(events)->socketOpened(id);
}
void cl_classroom_socket_text(void* events, int id, const char* text) {
	if (events) static_cast<SocketEvents*>(events)->socketText(id, orEmpty(text));
}
void cl_classroom_socket_closed(void* events, int id, int code) {
	if (events) static_cast<SocketEvents*>(events)->socketClosed(id, code);
}

void cl_classroom_page_open(CLClassroom* c, const char* classId, bool open) {
	if (c && c->engine) c->engine->pageOpen(orEmpty(classId), open);
}
void cl_classroom_app_activated(CLClassroom* c) {
	if (c && c->engine) c->engine->appActivated();
}
void cl_classroom_app_deactivated(CLClassroom* c) {
	if (c && c->engine) c->engine->appDeactivated();
}
void cl_classroom_user_active(CLClassroom* c) {
	if (c && c->engine) c->engine->userActive();
}
void cl_classroom_sync_side_changed(CLClassroom* c) {
	if (c && c->engine) c->engine->syncSideChanged();
}

bool cl_classroom_parse_code(const CLSyncHooks* hooks, int kind, const char* text, char code[29], char why[16]) {
	HookCrypto cr(hooks ? *hooks : kNoSync);
	std::string out, w;
	const bool ok = parseCode(cr, kindOf(kind), orEmpty(text), out, w);
	if (code) {
		strncpy(code, ok ? out.c_str() : "", 28);
		code[28] = 0;
	}
	if (why) {
		strncpy(why, ok ? "" : w.c_str(), 15);
		why[15] = 0;
	}
	return ok;
}

const char* cl_classroom_why_text(int kind, const char* why, const char* text) {
	thread_local std::string s;
	s = whyText(kindOf(kind), orEmpty(why), orEmpty(text));
	return s.c_str();
}

void cl_classroom_group_code(const char* code, char out[35]) {
	const std::string g = groupCode(orEmpty(code));
	strncpy(out, g.c_str(), 34);
	out[34] = 0;
}

const char* cl_classroom_web_link(int kind, const char* code) {
	thread_local std::string s;
	s = webLink(kindOf(kind), orEmpty(code));
	return s.c_str();
}

const char* cl_classroom_app_link(int kind, const char* code) {
	thread_local std::string s;
	s = appLink(kindOf(kind), orEmpty(code));
	return s.c_str();
}

bool cl_classroom_self_test(const CLClassroomHooks* hooks, const char* tempDir, const char* serverBase, const char* liveBase, char** report) {
	if (!hooks) return false;
	HookCrypto cr(hooks->sync ? *hooks->sync : kNoSync);
	HookCurve curve(*hooks);
	HookHost host(*hooks);
	std::string text;
	const bool ok = selfTest(cr, curve, orEmpty(tempDir), text, serverBase ? &host : nullptr, orEmpty(serverBase), orEmpty(liveBase));
	if (report) *report = strdup(text.c_str());
	return ok;
}

}  // extern "C"
