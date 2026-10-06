// The classroom core as a command-line device for the cross-check between the
// two clients (the website's scripts/test_classroom_interop.mjs): one process
// holds any number of devices (clclass::Client, each with its own files in
// memory) on the OpenSSL / zlib / libcurl hooks of classroom_openssl.h, talks
// to a classroom server over HTTP, and is driven one command per line.
// mac/Tools/classroom-interop.sh builds it (and runs the cross-check).
//
//   classroom_interop --server http://localhost:8788/api/classroom/v1 [--live http://localhost:8788/api/live/v1]
//
// In, one JSON object per line: {"dev": "S1", "cmd": "join", "code": "...", "name": "Sam Lee"}.
// Out, one JSON object per line: {"ok": true, "message": "", "value": "...", "status": 201, "error": "", ...}
// plus what the command reads (classes, assignments, students, submissions, live, answers).
// A device is made the first time it is named. Commands (the class as "classId"):
//   teacher: createClass {name}  addTeacherKey {text}  previewTeacherKey {text}  refreshTeacher  renameClass {name}
//            setJoinOpen {open}  newJoinCode  postAssignment {id?, title, instructions, cdl, keyText, keyNames,
//            canCheck, dueAt?, closeAfterDue?}  deleteAssignment {aid}  refreshStudents  removeStudents {sids,
//            deleteHandIns}  refreshSubmissions {aid}  goLive {cdl}  push {cdl, prompt?, lights?, reveal?}
//            endLive  takeOverLive  refreshAnswers  forgetClass  deleteClass
//   student: previewJoinCode {code}  join {code, name}  rename {name}  pulse  refreshMember  handIn {aid, cdl}
//            sendAnswer {lights: {L: 0|1}}  makeMoveCode  previewMoveCode {code}  importMoveCode {code}
//            leaveClass  forgetMembership
//   reading: classes  assignments  students  submissions {aid}  live  answers  member  notices
//   quit
//
// Check My Circuit and a circuit's lights are stand-ins (there is no simulator here), the same
// as the web core's tests: a circuit "matches", and every light is 1, when it has a switch that is on.

#include "ClassroomInternal.h"
#include "classroom_openssl.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <string>

namespace {

using clsync::json::Value;
using namespace clclass;

const char* kOn = "\"OUTPUT_NUM\" \"1\"";

struct Device {
	std::map<std::string, std::string> files;
	std::vector<std::string> notices;
	std::unique_ptr<Client> client;
};

Value str(const std::string& s) { return Value::string(s); }
Value num(int64_t n) { return Value::integer(n); }
Value flag(bool b) { return Value::boolean(b); }

Value resultJson(const Result& r) {
	Value o = Value::object();
	o.set("ok", flag(r.ok));
	o.set("message", str(r.message));
	o.set("value", str(r.value));
	o.set("status", num(r.status));
	o.set("error", str(r.error));
	return o;
}

Value classesJson(const Client& c) {
	Value a = Value::array();
	for (const ClassInfo& i : c.classes()) {
		Value o = Value::object();
		o.set("classId", str(i.classId));
		o.set("name", str(i.name));
		o.set("teaching", flag(i.teaching));
		o.set("teacherKey", str(i.teacherKey));
		o.set("joinCode", str(i.joinCode));
		o.set("joinOpen", flag(i.joinOpen));
		o.set("studentName", str(i.studentName));
		o.set("live", flag(i.live));
		o.set("warning", str(i.warning));
		a.push(o);
	}
	return a;
}

Value assignmentsJson(const Client& c, const std::string& cid) {
	Value a = Value::array();
	for (const Assignment& x : c.assignments(cid)) {
		Value o = Value::object();
		o.set("id", str(x.id));
		o.set("title", str(x.title));
		o.set("instructions", str(x.instructions));
		o.set("cdl", str(x.cdl));
		o.set("dueAt", num(x.dueAt));
		o.set("closeAfterDue", flag(x.closeAfterDue));
		o.set("ver", num(x.ver));
		o.set("keyText", str(x.keyText));
		o.set("keyNames", str(x.keyNames));
		o.set("keySealed", flag(x.keySealed));
		o.set("handedInAt", num(x.handedInAt));
		o.set("attempts", num(x.attempts));
		o.set("pending", flag(x.pending));
		o.set("closed", flag(x.closed));
		o.set("unreadable", flag(x.unreadable));
		o.set("problem", str(x.problem));
		a.push(o);
	}
	return a;
}

Value studentsJson(const Client& c, const std::string& cid) {
	Value a = Value::array();
	for (const Student& s : c.students(cid)) {
		Value o = Value::object();
		o.set("studentId", str(s.studentId));
		o.set("name", str(s.name));
		o.set("joinedAt", num(s.joinedAt));
		o.set("unreadable", flag(s.unreadable));
		a.push(o);
	}
	return a;
}

Value submissionsJson(const Client& c, const std::string& cid, const std::string& aid) {
	Value a = Value::array();
	for (const Submission& s : c.submissions(cid, aid)) {
		Value o = Value::object();
		o.set("studentId", str(s.studentId));
		o.set("name", str(s.name));
		o.set("cdl", str(s.cdl));
		o.set("handedInAt", num(s.handedInAt));
		o.set("attempts", num(s.attempts));
		o.set("checkVerdict", num(s.checkVerdict));
		o.set("checkSummary", str(s.checkSummary));
		o.set("unreadable", flag(s.unreadable));
		o.set("problem", str(s.problem));
		o.set("left", flag(s.left));
		o.set("ver", num(s.ver));
		a.push(o);
	}
	return a;
}

Value liveJson(const Live& l) {
	Value o = Value::object();
	o.set("on", flag(l.on));
	o.set("ended", flag(l.ended));
	o.set("reveal", flag(l.reveal));
	o.set("hasPredict", flag(l.hasPredict));
	o.set("session", str(l.session));
	o.set("cdl", str(l.cdl));
	o.set("prompt", str(l.prompt));
	Value lights = Value::array();
	for (const std::string& s : l.lights) lights.push(str(s));
	o.set("lights", lights);
	o.set("ver", num(l.ver));
	o.set("step", num(l.step));
	o.set("myRight", num(l.myRight));
	o.set("myTotal", num(l.myTotal));
	o.set("takeOver", flag(l.takeOver));
	return o;
}

Value answersJson(const AnswerCounts& n) {
	Value o = Value::object();
	o.set("answered", num(n.answered));
	o.set("students", num(n.students));
	o.set("right", num(n.right));
	o.set("wrong", num(n.wrong));
	Value per = Value::object();
	for (const auto& kv : n.perLight) {
		Value pair = Value::array();
		pair.push(num(kv.second.first));
		pair.push(num(kv.second.second));
		per.set(kv.first, pair);
	}
	o.set("perLight", per);
	return o;
}

std::vector<std::string> strings(const Value* v) {
	std::vector<std::string> out;
	if (v && v->isArray())
		for (const Value& x : v->a) out.push_back(x.s);
	return out;
}

}  // namespace

int main(int argc, char** argv) {
	Config base;
	base.client = "interop/1";
	bool liveGiven = false;
	if (const char* u = getenv("CL_CLASSROOM_URL")) base.serverBase = u;
	if (const char* u = getenv("CL_LIVE_URL")) { base.liveBase = u; liveGiven = true; }
	if (const char* k = getenv("CL_APP_KEY")) base.appKey = k;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--server") == 0 && i + 1 < argc) base.serverBase = argv[++i];
		else if (strcmp(argv[i], "--live") == 0 && i + 1 < argc) { base.liveBase = argv[++i]; liveGiven = true; }
	}
	const size_t at = base.serverBase.find("/api/classroom/v1");
	if (!liveGiven && at != std::string::npos) base.liveBase = base.serverBase.substr(0, at) + "/api/live/v1";
	curl_global_init(CURL_GLOBAL_DEFAULT);
	OpenSslCrypto crypto;
	OpenSslCurve curve;
	std::map<std::string, Device> devices;   // std::map: a device's address never moves

	auto deviceNamed = [&](const std::string& name) -> Device& {
		Device& d = devices[name];
		if (d.client) return d;
		Config cfg = base;
		cfg.deviceName = name;
		Device* dp = &d;
		ClientHooks h;
		h.http = [](const clsync::HttpRequest& q) { return curlHttp(q); };
		h.now = [] { return clsync::SystemClock().now(); };
		h.load = [dp](const std::string& n) {
			auto it = dp->files.find(n);
			return it == dp->files.end() ? std::string() : it->second;
		};
		h.save = [dp](const std::string& n, const std::string& t) {
			dp->files[n] = t;
			return true;
		};
		h.removeTree = [dp](const std::string& n) {
			for (auto it = dp->files.begin(); it != dp->files.end();)
				it = it->first.compare(0, n.size(), n) == 0 ? dp->files.erase(it) : std::next(it);
		};
		h.notice = [dp](const std::string& t) { dp->notices.push_back(t); };
		h.check = [](const std::string& cdl, const std::string&, const std::string&, int& verdict, std::string& summary) {
			const bool on = cdl.find(kOn) != std::string::npos;
			verdict = on ? 0 : 1;
			summary = on ? "Matches" : "1 row wrong";
			return true;
		};
		h.lights = [](const std::string& cdl, const std::vector<std::string>& lights, std::map<std::string, int>& values) {
			for (const std::string& l : lights) values[l] = cdl.find(kOn) != std::string::npos ? 1 : 0;
			return true;
		};
		d.client.reset(new Client(cfg, crypto, curve, h));
		d.client->load();
		return d;
	};

	std::string line;
	while (std::getline(std::cin, line)) {
		if (line.empty()) continue;
		Value in;
		Value out = Value::object();
		if (!clsync::json::parse(line, in) || !in.isObject()) {
			out.set("ok", flag(false));
			out.set("message", str("not JSON"));
			std::cout << clsync::json::write(out) << std::endl;
			continue;
		}
		const std::string cmd = in.str("cmd"), cid = in.str("classId");
		if (cmd == "quit") break;
		Device& d = deviceNamed(in.str("dev", "default"));
		Client& c = *d.client;
		bool known = true;
		Result r = Result::good();
		if (cmd == "createClass") r = c.createClass(in.str("name"));
		else if (cmd == "previewTeacherKey") r = c.previewTeacherKey(in.str("text"));
		else if (cmd == "addTeacherKey") r = c.addTeacherKey(in.str("text"));
		else if (cmd == "refreshTeacher") r = c.refreshTeacher(cid);
		else if (cmd == "renameClass") r = c.renameClass(cid, in.str("name"));
		else if (cmd == "setJoinOpen") r = c.setJoinOpen(cid, in.flag("open"));
		else if (cmd == "newJoinCode") r = c.newJoinCode(cid);
		else if (cmd == "postAssignment") {
			Assignment a;
			a.id = in.str("id");
			a.title = in.str("title");
			a.instructions = in.str("instructions");
			a.cdl = in.str("cdl");
			a.keyText = in.str("keyText");
			a.keyNames = in.str("keyNames");
			const Value* due = in.get("dueAt");
			a.dueAt = due && due->isNumber() ? due->i() : -1;
			a.closeAfterDue = in.flag("closeAfterDue");
			r = c.postAssignment(cid, a, in.flag("canCheck"));
		} else if (cmd == "deleteAssignment") r = c.deleteAssignment(cid, in.str("aid"));
		else if (cmd == "refreshStudents") r = c.refreshStudents(cid);
		else if (cmd == "removeStudents") r = c.removeStudents(cid, strings(in.get("sids")), in.flag("deleteHandIns"));
		else if (cmd == "refreshSubmissions") r = c.refreshSubmissions(cid, in.str("aid"));
		else if (cmd == "goLive") r = c.goLive(cid, in.str("cdl"));
		else if (cmd == "push") {
			const Value* p = in.get("prompt");
			const std::string prompt = p ? p->s : std::string();
			const std::vector<std::string> lights = strings(in.get("lights"));
			r = c.push(cid, in.str("cdl"), p ? &prompt : nullptr, in.get("lights") ? &lights : nullptr, in.flag("reveal"));
		} else if (cmd == "endLive") r = c.endLive(cid);
		else if (cmd == "takeOverLive") r = c.takeOverLive(cid);
		else if (cmd == "refreshAnswers") r = c.refreshAnswers(cid);
		else if (cmd == "forgetClass") c.forgetClass(cid);
		else if (cmd == "deleteClass") r = c.deleteClass(cid);
		else if (cmd == "previewJoinCode") r = c.previewJoinCode(in.str("code"));
		else if (cmd == "join") r = c.join(in.str("code"), in.str("name"));
		else if (cmd == "rename") r = c.rename(cid, in.str("name"));
		else if (cmd == "pulse") r = c.pulse(cid);
		else if (cmd == "refreshMember") r = c.refreshMember(cid);
		else if (cmd == "handIn") r = c.handIn(cid, in.str("aid"), in.str("cdl"));
		else if (cmd == "sendAnswer") {
			std::map<std::string, int> lights;
			if (const Value* l = in.get("lights"))
				for (const auto& kv : l->o) lights[kv.first] = (int)kv.second.i();
			r = c.sendAnswer(cid, lights);
		} else if (cmd == "makeMoveCode") r = c.makeMoveCode(cid);
		else if (cmd == "previewMoveCode") r = c.previewMoveCode(in.str("code"));
		else if (cmd == "importMoveCode") r = c.importMoveCode(in.str("code"));
		else if (cmd == "leaveClass") r = c.leaveClass(cid);
		else if (cmd == "forgetMembership") c.forgetMembership(cid);
		else if (cmd == "classes") out.set("classes", classesJson(c));
		else if (cmd == "assignments") out.set("assignments", assignmentsJson(c, cid));
		else if (cmd == "students") out.set("students", studentsJson(c, cid));
		else if (cmd == "submissions") out.set("submissions", submissionsJson(c, cid, in.str("aid")));
		else if (cmd == "live") out.set("live", liveJson(c.live(cid)));
		else if (cmd == "answers") out.set("answers", answersJson(c.answers(cid)));
		else if (cmd == "member") {
			Membership* m = c.membershipOf(cid);
			out.set("member", flag(m != nullptr));
			out.set("studentId", str(m ? m->studentId : std::string()));
			out.set("name", str(m ? m->name : std::string()));
			out.set("className", str(m ? m->className : std::string()));
		} else if (cmd == "notices") {
			Value a = Value::array();
			for (const std::string& n : d.notices) a.push(str(n));
			out.set("notices", a);
			d.notices.clear();
		} else known = false;
		if (!known) r = Result::bad("unknown command: " + cmd);
		const Value rj = resultJson(r);
		for (const auto& kv : rj.o)
			if (!out.get(kv.first)) out.set(kv.first, kv.second);
		std::cout << clsync::json::write(out) << std::endl;
	}
	curl_global_cleanup();
	return 0;
}
