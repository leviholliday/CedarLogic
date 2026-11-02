// clclass::selfTest: the vectors of CLASSROOM.md 7.1 (ClassroomVectors.h,
// from tests/classroom/vectors.json), the scenarios of 7.2 on the FakeServer
// (ClassroomTestScenarios.cpp), the threaded engine (ClassroomTestEngine.cpp),
// the live connection and held polls (ClassroomTestLive.cpp) and, with a
// serverBase, a class's round trip and a live round against a real server (the
// Worker under wrangler dev, or the mock server) over the platform's own HTTP
// and WebSockets.

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS   // getenv in a test
#endif

#include "ClassroomTest.h"

#include <cstdlib>

namespace clclass {
namespace test {

// One class through a real server (the mock server of CLASSROOM.md 10 S, or a deploy
// preview): create, a second device, join, post, hand in, view, live, delete.
void serverTests(Crypto& cr, Curve& curve, const std::string& tempDir, Report& r, Host& host, const std::string& serverBase,
                 const std::string& liveBase) {
	(void)tempDir;
	auto client = [&](std::map<std::string, std::string>& files) {
		Config cfg;
		cfg.serverBase = serverBase;
		cfg.liveBase = liveBase;
		cfg.client = "selftest/1";
		if (const char* k = getenv("CL_APP_KEY")) cfg.appKey = k;
		ClientHooks h;
		h.http = [&host](const HttpRequest& q) { return host.http(q); };
		h.now = [] { return clsync::SystemClock().now(); };
		h.load = [&files](const std::string& n) { auto it = files.find(n); return it == files.end() ? std::string() : it->second; };
		h.save = [&files](const std::string& n, const std::string& t) {
			files[n] = t;
			return true;
		};
		h.removeTree = [&files](const std::string& n) {
			for (auto it = files.begin(); it != files.end();)
				it = it->first.compare(0, n.size(), n) == 0 ? files.erase(it) : std::next(it);
		};
		return std::unique_ptr<Client>(new Client(cfg, cr, curve, h));
	};
	std::map<std::string, std::string> fT, fT2, fS;
	auto T = client(fT), T2 = client(fT2), S = client(fS);
	const Result c = T->createClass("Self-test class");
	r.line(c.ok, "server: create", c.message);
	if (!c.ok) return;
	const std::string cid = c.value;
	Teaching* t = T->teachingOf(cid);
	const Result a = T2->addTeacherKey(t->teacherKey);
	r.line(a.ok && T2->teachingOf(cid), "server: a second device adds the class with the key", a.message);
	const Result j = S->join(t->rec.joinCode, "Sam Lee");
	r.line(j.ok, "server: join", j.message);
	Assignment as;
	as.title = "Lab";
	as.cdl = "(cedarlogic\n  (version 3)\n)\n";
	as.keyText = "LED = A";
	const Result p = T->postAssignment(cid, as, true);
	r.line(p.ok, "server: post", p.message);
	S->pulse(cid);
	r.line(S->assignments(cid).size() == 1, "server: the student sees it through the pulse and the cached path");
	const Result h = S->handIn(cid, p.value, as.cdl);
	r.line(h.ok && h.value != "pending", "server: hand in", h.message);
	T->refreshSubmissions(cid, p.value);
	const auto subs = T->submissions(cid, p.value);
	r.line(subs.size() == 1 && subs[0].name == "Sam Lee" && subs[0].cdl == as.cdl, "server: the teacher opens the hand-in");
	const Result g = T->goLive(cid, as.cdl);
	S->pulse(cid);
	r.line(g.ok && S->live(cid).on, "server: live", g.message);
	T->endLive(cid);
	// 3.17 class passes (a server without them, the old mock, answers 404 not_found: skipped).
	const Result pc = S->makeClassPass(cid);
	if (!(pc.status == 404 && pc.error == "not_found")) {
		std::map<std::string, std::string> fG;
		auto G = client(fG);
		const Result pv = G->previewClassPass(pc.value);
		const Result use = G->useClassPass(pc.value);
		const Result all = T->listPasses(cid);
		r.line(pc.ok && pv.ok && pv.value == "Self-test class\nSam Lee" && use.ok && G->membershipOf(cid) &&
		           all.ok && all.value.find("\tSam Lee\t") != std::string::npos,
		       "server: a class pass brings the student back on a fresh device; the teacher sees it", pc.message + use.message + all.message);
		const Result cancel = T->cancelPass(cid, all.value.substr(0, 64));
		std::map<std::string, std::string> fG2;
		auto G2 = client(fG2);
		const Result after = G2->previewClassPass(pc.value);
		r.line(cancel.ok && !after.ok && after.error == "pass_gone", "server: the teacher cancels the pass; it stops working", after.message);
	}
	const Result d = T->deleteClass(cid);
	const Result after = S->refreshMember(cid);
	r.line(d.ok && !after.ok && after.status == 410, "server: delete; the student gets 410", d.message);
}

}  // namespace test

bool selfTest(clsync::Crypto& cr, Curve& curve, const std::string& tempDir, std::string& report, Host* httpOnly,
              const std::string& serverBase, const std::string& liveBase) {
	test::Report r;
	if (const char* only = getenv("CL_CLASSROOM_TEST_ONLY")) r.only = only;
	test::vectorTests(cr, curve, r, test::embeddedVectors());
	test::scenarioTests(cr, curve, tempDir, r);
	test::engineTests(cr, curve, tempDir, r);
	test::liveTests(cr, curve, tempDir, r);
	test::engineLiveTests(cr, curve, tempDir, r);
	if (httpOnly && !serverBase.empty()) {
		std::string live = liveBase;
		const size_t at = serverBase.find("/api/classroom/v1");
		if (live.empty() && at != std::string::npos) live = serverBase.substr(0, at) + "/api/live/v1";
		test::serverTests(cr, curve, tempDir, r, *httpOnly, serverBase, live);
		test::liveServerTests(cr, curve, r, *httpOnly, serverBase, live);
	}
	report = r.text + std::to_string(r.passed) + " passed, " + std::to_string(r.failed) + " failed\n";
	return r.failed == 0;
}

}  // namespace clclass
