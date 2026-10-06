// The protocol scenarios of CLASSROOM.md 7.2, numbered as there, with clients
// ("devices": T1 and T2 a teacher's, S1, S2 ... students') against the
// FakeServer. Each device has its own files (in memory) and, for a teacher
// with Sync on, a shared store of sync side records.

#include "ClassroomTest.h"

#include <algorithm>
#include <functional>
#include <memory>

namespace clclass {
namespace test {

using clsync::b64u;
using clsync::hex;
using clsync::sha256Hex;
using clsync::unb64u;

namespace {

const char* const kCdlOff =
	"(cedarlogic\n  (version 3)\n  (generator \"CedarLogic 0.3.5\")\n  (page 0\n    (gate \"AA_TOGGLE\"\n      (uuid \"1\")\n      (at 10 20)\n"
	"      (angle 0)\n      (lparam \"OUTPUT_NUM\" \"0\"))\n    (gate \"GA_LED\"\n      (uuid \"2\")\n      (at 18 20)\n      (angle 0))\n"
	"    (wire\n      (ids \"3\")\n      (seg \"0\" h\n        (pts 11 20 17 20)\n        (connect \"1\" \"OUT_0\")\n"
	"        (connect \"2\" \"N_in0\")))))\n";

std::string cdlOn() {
	std::string s = kCdlOff;
	const std::string from = "\"OUTPUT_NUM\" \"0\"";
	s.replace(s.find(from), from.size(), "\"OUTPUT_NUM\" \"1\"");
	return s;
}

// Sync, as far as the classroom sees it: the records of kind "classroom" one teacher's devices share.
struct SideStore {
	std::map<std::string, std::string> recs;
	int n = 0;
};

struct Device {
	std::map<std::string, std::string> files;
	std::vector<std::string> notices;
	std::unique_ptr<Client> c;
	std::function<void(const HttpRequest&, HttpResponse&)> tamper;   // changes the FakeServer's answers (a server that's wrong)
	Device(Crypto& cr, Curve& curve, FakeServer& s, SideStore* side, const std::string& client = "test/1") {
		Config cfg;
		cfg.serverBase = FakeServer::base();
		cfg.liveBase = FakeServer::liveBase();
		cfg.client = client;
		cfg.deviceName = "Test Mac";
		ClientHooks h;
		h.http = [this, &s](const HttpRequest& r) {
			HttpResponse x = s.handle(r);
			if (tamper) tamper(r, x);
			return x;
		};
		h.now = [&s] { return s.now; };
		h.load = [this](const std::string& n) { auto it = files.find(n); return it == files.end() ? std::string() : it->second; };
		h.save = [this](const std::string& n, const std::string& t) {
			files[n] = t;
			return true;
		};
		h.removeTree = [this](const std::string& n) {
			for (auto it = files.begin(); it != files.end();)
				it = (it->first == n || it->first.compare(0, n.size() + 1, n + "/") == 0) ? files.erase(it) : std::next(it);
		};
		h.notice = [this](const std::string& t) { notices.push_back(t); };
		if (side) {
			h.sideRecords = [side] { return std::vector<std::pair<std::string, std::string>>(side->recs.begin(), side->recs.end()); };
			h.putSide = [side](const std::string& rid, const std::string& j) {
				side->recs[rid.empty() ? "rid-" + std::to_string(++side->n) : rid] = j;
			};
			h.deleteSide = [side](const std::string& rid) { side->recs.erase(rid); };
		}
		// Check My Circuit and the lights, as the app would: the switch's state is the light's.
		h.check = [](const std::string& cdl, const std::string& key, const std::string&, int& verdict, std::string& summary) {
			const bool on = cdl.find("\"OUTPUT_NUM\" \"1\"") != std::string::npos;
			verdict = key == "LED = A" && on ? 0 : 1;
			summary = verdict == 0 ? "Matches" : "1 wrong row";
			return true;
		};
		h.lights = [](const std::string& cdl, const std::vector<std::string>& names, std::map<std::string, int>& out) {
			for (const std::string& n : names) out[n] = cdl.find("\"OUTPUT_NUM\" \"1\"") != std::string::npos ? 1 : 0;
			return true;
		};
		c.reset(new Client(cfg, cr, curve, h));
		c->load();
	}
	Client* operator->() { return c.get(); }
	bool noticed(const std::string& part) const {
		for (const std::string& n : notices)
			if (n.find(part) != std::string::npos) return true;
		return false;
	}
};

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

}  // namespace

void scenarioTests(Crypto& cr, Curve& curve, const std::string& tempDir, Report& r) {
	(void)tempDir;
	FakeServer s(cr);
	SideStore side;
	Device T1(cr, curve, s, &side), T2(cr, curve, s, nullptr), S1(cr, curve, s, nullptr), S2(cr, curve, s, nullptr);
	const std::string on = cdlOn();
	auto line = [&](bool ok, const std::string& label, const std::string& why = std::string()) {
		if (r.wanted(label)) r.line(ok, label, why);
	};

	// 1. Create.
	Result res = T1->createClass("  Digital Logic 101 ");
	const std::string cid = res.value;
	Teaching* t1 = T1->teachingOf(cid);
	line(res.ok && t1 && t1->rec.name == "Digital Logic 101" && t1->teacherKey.size() == 28 && t1->rec.joinCode.size() == 12 &&
	         t1->rec.d.size() == 43 && t1->rec.classKey.size() == 43 && s.classes.count(cid),
	     "s01 create: 201, the key, d, the class key and the join code kept", res.message);
	{
		Bytes secret;
		std::string why;
		decodeCode(cr, CodeKind::Teacher, t1 ? t1->teacherKey : "", secret, why);
		OpenKey k;
		k.key = teacherKeys(cr, secret).backupKey;
		std::string payload;
		Bytes env;
		unb64u(s.classes[cid].teacher.env, env);
		line(t1 && open(cr, curve, env, "teacher", cid, "teacher", 1, k, payload, why) && payload == teacherJson(t1->rec),
		     "s01 create: the website's teacher record opens with backupKey to the same record");
		line(side.recs.size() == 1 && has(side.recs.begin()->second, t1 ? t1->teacherKey : "x") &&
		         has(side.recs.begin()->second, "\"kind\":\"classroom\""),
		     "s01 create: a sync side record is written (Sync on)");
		line(T1.files.count("teaching.json") && has(T1.files["teaching.json"], t1 ? t1->teacherKey : "x"), "s01 create: teaching.json");
	}
	const std::string teacherKey = t1 ? t1->teacherKey : std::string();
	const std::string joinCode = t1 ? t1->rec.joinCode : std::string();

	// 2. A second device with the teacher key.
	{
		std::string spaced;
		for (size_t i = 0; i < teacherKey.size(); i++) {
			if (i && i % 4 == 0) spaced += ' ';
			spaced += (char)tolower((unsigned char)teacherKey[i]);
		}
		const std::string before = s.dump();
		const Result p = T2->previewTeacherKey(" " + spaced + " ");
		const Result a = T2->addTeacherKey(webLink(CodeKind::Teacher, teacherKey));
		Teaching* t2 = T2->teachingOf(cid);
		line(p.ok && p.value == "Digital Logic 101", "s02 another device: the preview names the class", p.message);
		line(a.ok && t2 && t2->rec.d == t1->rec.d && t2->rec.classKey == t1->rec.classKey && t2->rec.joinCode == joinCode &&
		         t2->joinProblem.empty(),
		     "s02 another device: T2 has everything T1 has", a.message);
		line(s.dump() == before, "s02 another device: nothing was written to the website");
		Device T3(cr, curve, s, &side);
		T3->sideChanged();
		line(T3->teachingOf(cid) != nullptr && T3.notices.empty(), "s02 a classroom sync record adds the class quietly");
	}

	// 3. Wrong keys.
	{
		std::string wrong = teacherKey;
		wrong[5] = wrong[5] == 'Z' ? 'Y' : 'Z';
		const Result a = T2->previewTeacherKey(wrong);
		line(!a.ok && a.error == "checksum" && has(a.message, "typo"), "s03 a wrong teacher key: checksum", a.message);
		const std::string other = newCode(cr, CodeKind::Teacher);
		const Result b = T2->addTeacherKey(other);
		line(!b.ok && b.status == 404 && b.message == "This class isn't on the website." && T2->classes().size() == 1,
		     "s03 another class's key: not on the website, nothing stored", b.message);
	}

	// 4. S1 joins.
	std::string grouped = groupCode(joinCode);
	for (char& ch : grouped) ch = (char)tolower((unsigned char)ch);
	res = S1->previewJoinCode(grouped);
	line(res.ok && res.value == "Digital Logic 101\n1", "s04 join: the preview names the class", res.message);
	res = S1->join(grouped, "Sam Lee");
	Membership* m1 = S1->membershipOf(cid);
	line(res.ok && m1 && m1->fetchKey == s.fetchKeyOf(cid) && S1->assignments(cid).empty(), "s04 join: 201, no assignments yet", res.message);
	T1->refreshStudents(cid);
	{
		const auto st = T1->students(cid);
		line(st.size() == 1 && st[0].name == "Sam Lee" && t1->proofs.count(m1 ? m1->studentId : "") && t1->proofs[m1->studentId] == m1->proof,
		     "s04 join: T1 opens the sealed name to \"Sam Lee\" and pins the proof");
	}
	const std::string sid1 = m1 ? m1->studentId : std::string();

	// 5. Wrong code, closed, full.
	{
		const Result a = S2->join(newCode(cr, CodeKind::Join), "Alex");
		line(!a.ok && a.status == 404 && a.message == "No class has this code. Check it with your teacher.", "s05 a wrong join code: 404",
		     a.message);
		T1->setJoinOpen(cid, false);
		const Result p = S2->previewJoinCode(joinCode);
		const Result b = S2->join(joinCode, "Alex");
		line(p.ok && p.value == "Digital Logic 101\n0" && !b.ok && b.error == "join_closed" &&
		         b.message == "Joining \xE2\x80\x9C" "Digital Logic 101\xE2\x80\x9D is closed. Ask your teacher to open it.",
		     "s05 a closed class: join_closed", b.message);
		T1->setJoinOpen(cid, true);
		s.maxStudents = 1;
		const Result c = S2->join(joinCode, "Alex");
		line(!c.ok && c.error == "class_full" && c.message == "This class is full.", "s05 a full class: class_full", c.message);
		s.maxStudents = 300;
	}

	// 6. A new join code.
	{
		const std::string oldFetch = s.fetchKeyOf(cid);
		const Result n = T1->newJoinCode(cid);
		const Result old = S2->join(joinCode, "Alex");
		line(n.ok && n.value != joinCode && !old.ok && old.status == 404, "s06 new code: the old one answers no_class", old.message);
		const Result j = S2->join(n.value, "Alex Kim");
		line(j.ok && S2->membershipOf(cid), "s06 new code: joins", j.message);
		line(s.fetchKeyOf(cid) != oldFetch && m1->fetchKey == oldFetch, "s06 new code: the class's fetchKey changed");
		const size_t before = s.requests;
		S1->pulse(cid);
		line(m1->fetchKey == s.fetchKeyOf(cid) && s.requests - before >= 2, "s06 new code: S1's pulse is refused, S1 reads its status and keeps working");
		HttpRequest q;
		q.method = "GET";
		q.url = std::string(FakeServer::liveBase()) + "/" + cid + "/" + oldFetch;
		line(s.handle(q).status == 404, "s06 new code: the old path stays refused");
	}
	const std::string sid2 = S2->membershipOf(cid) ? S2->membershipOf(cid)->studentId : std::string();

	// 7. An assignment with a readable key.
	Assignment a1;
	a1.title = "Lab 3: a switch and a light";
	a1.instructions = "Wire the switch to the light.";
	a1.cdl = kCdlOff;
	a1.keyText = "LED = A";
	res = T1->postAssignment(cid, a1, true);
	const std::string aid1 = res.value;
	{
		const int64_t seqBefore = m1->seq;
		S1->pulse(cid);
		const auto list = S1->assignments(cid);
		line(res.ok && s.classes[cid].seq > seqBefore && list.size() == 1 && list[0].title == a1.title && list[0].keyText == "LED = A" &&
		         list[0].cdl == kCdlOff && list[0].attempts == 0,
		     "s07 post: the pulse moves, S1 sees it with its key, attempt 0", res.message);
		line(S1.files.count("cache/" + cid + "/assignments/" + aid1 + ".json") > 0, "s07 post: kept in S1's cache");
	}

	// 8. "Only I check".
	Assignment a2;
	a2.title = "Lab 4";
	a2.cdl = kCdlOff;
	a2.keyText = "LED = A";
	res = T1->postAssignment(cid, a2, false);
	const std::string aid2 = res.value;
	{
		S1->pulse(cid);
		Assignment got;
		for (const Assignment& x : S1->assignments(cid))
			if (x.id == aid2) got = x;
		line(res.ok && got.title == "Lab 4" && got.keySealed && got.keyText.empty() && !has(S1.files["cache/" + cid + "/assignments/" + aid2 + ".json"], "LED = A"),
		     "s08 only I check: S1 sees the title, holds no key text");
		T2->refreshTeacher(cid);
		Assignment t2got;
		for (const Assignment& x : T2->assignments(cid))
			if (x.id == aid2) t2got = x;
		line(t2got.keySealed && t2got.keyText == "LED = A", "s08 only I check: T2 opens the sealed key from the assignment");
	}

	// 9. Two teacher devices edit the same assignment.
	{
		Assignment e1 = a1;
		e1.id = aid1;
		e1.title = "Lab 3 (from T1)";
		Assignment e2 = a1;
		e2.id = aid1;
		e2.title = "Lab 3 (from T2)";
		T2->refreshTeacher(cid);
		const Result x = T1->postAssignment(cid, e1, true);
		const Result y = T2->postAssignment(cid, e2, true);
		std::string title;
		for (const Assignment& a : T2->assignments(cid))
			if (a.id == aid1) title = a.title;
		line(x.ok && !y.ok && y.status == 412 && y.message == "This class was changed on another device." && title == "Lab 3 (from T1)",
		     "s09 two editors: T2 gets 412 and shows T1's edit", y.message);
	}

	// 10. Hand in twice.
	{
		S1->pulse(cid);
		const Result h1 = S1->handIn(cid, aid1, kCdlOff);
		const int64_t first = s.now;
		s.now += kMinute;
		const Result h2 = S1->handIn(cid, aid1, on);
		T1->refreshSubmissions(cid, aid1);
		const auto subs = T1->submissions(cid, aid1);
		line(h1.ok && h2.ok && h1.message == "Handed in." && subs.size() == 1 && subs[0].attempts == 2 && subs[0].cdl == on &&
		         subs[0].name == "Sam Lee" && subs[0].checkVerdict == 0 && subs[0].checkSummary == "Matches",
		     "s10 hand in twice: attempt 2, the latest content, T1's own check", h2.message);
		line(s.classes[cid].subs[aid1][sid1].firstAt == first, "s10 hand in twice: firstAt kept");
		const auto mine = S1->assignments(cid);
		bool ok = false;
		for (const Assignment& a : mine) ok = ok || (a.id == aid1 && a.attempts == 2 && a.handedInAt > 0);
		line(ok, "s10 hand in twice: S1's row shows attempt 2");
	}

	// 24 and 11. Move to another device; hand in from it with a stale base.
	Device S1b(cr, curve, s, nullptr);
	{
		const Result mc = S1->makeMoveCode(cid);
		const Result pv = S1b->previewMoveCode(groupCode(mc.value));
		const Result im = S1b->importMoveCode(webLink(CodeKind::Move, mc.value));
		Membership* mb = S1b->membershipOf(cid);
		line(mc.ok && pv.ok && pv.value == "Digital Logic 101\nSam Lee" && im.ok && mb && mb->studentId == sid1 && mb->token == m1->token &&
		         mb->proof == m1->proof && mb->fetchKey == s.fetchKeyOf(cid),
		     "s24 move: the other device is the same student", im.message);
		const Result again = S1b->importMoveCode(mc.value);
		const Result pv2 = S2->previewMoveCode(mc.value);
		line(!pv2.ok && pv2.status == 404 && pv2.message == "That move code has expired. Make a new one on the other device." && !again.ok &&
		         again.error == "move_gone",
		     "s24 move: the slot is gone after the import; importing again gets move_gone", pv2.message);
		const Result h = S1b->handIn(cid, aid1, on);   // its base is 0; the server's ver is 2
		line(h.ok && s.classes[cid].subs[aid1][sid1].rec.ver == 3, "s11 a stale base: 412, then the current ver as base, once", h.message);
		const Result h2 = S1->handIn(cid, aid1, on);   // S1's base (2) is stale now too
		line(h2.ok && s.classes[cid].subs[aid1][sid1].rec.ver == 4, "s11 the first device's next hand-in also goes through");
	}

	// 25. A move code expires.
	{
		const Result mc = S1->makeMoveCode(cid);
		s.now += 11 * kMinute;
		const Result pv = S2->previewMoveCode(mc.value);
		const Result mc2 = S1->makeMoveCode(cid);
		const Result pv2 = S2->previewMoveCode(mc2.value);
		line(!pv.ok && pv.error == "move_gone" && pv2.ok, "s25 an expired move code: move_gone; a new one works", pv.message);
	}

	// 12. Hand-ins closed after the due date.
	{
		Assignment a3;
		a3.title = "Quiz";
		a3.cdl = kCdlOff;
		a3.dueAt = s.now + kHour;
		a3.closeAfterDue = true;
		const Result p = T1->postAssignment(cid, a3, true);
		S1->pulse(cid);
		s.now += 2 * kHour;
		const Result h = S1->handIn(cid, p.value, on);
		bool closed = false;
		for (const Assignment& a : S1->assignments(cid)) closed = closed || (a.id == p.value && a.closed);
		line(!h.ok && h.error == "assignment_closed" && has(h.message, "Hand-ins for this assignment are closed.") && has(h.message, "It was due") &&
		         closed,
		     "s12 closed: 409, the sentence with the due date, the row says closed", h.message);
	}

	// 13. Offline, then online.
	{
		s.offline = 1;
		const Result h = S1->handIn(cid, aid2, on);
		bool pending = false;
		for (const Assignment& a : S1->assignments(cid)) pending = pending || (a.id == aid2 && a.pending);
		line(h.ok && h.value == "pending" && h.message == "Will hand in when you're online." && pending && !s.classes[cid].subs[aid2].count(sid1),
		     "s13 offline: kept in pending", h.message);
		S1->pulse(cid);
		pending = false;
		for (const Assignment& a : S1->assignments(cid)) pending = pending || (a.id == aid2 && a.pending);
		line(!pending && s.classes[cid].subs[aid2].count(sid1), "s13 online: sent at the next pulse");
	}

	// 14. Too big.
	{
		Bytes junk(800000);
		cr.random(junk.data(), junk.size());
		const std::string big = "(cedarlogic " + b64u(junk) + ")";
		const auto before = s.classes[cid].subs[aid1][sid1].rec.ver;
		const Result h = S1->handIn(cid, aid1, big);
		line(!h.ok && h.message == "Too big to send (over 512 KB)." && s.classes[cid].subs[aid1][sid1].rec.ver == before,
		     "s14 over 512 KiB: the sentence, nothing stored", h.message);
	}

	// 15. A damaged hand-in.
	{
		const Result h = S2->handIn(cid, aid1, kCdlOff);
		s.flipSubmissionByte(cid, aid1, sid2);
		T1->refreshSubmissions(cid, aid1);
		Submission got;
		for (const Submission& x : T1->submissions(cid, aid1))
			if (x.studentId == sid2) got = x;
		const size_t before = s.requests;
		T1->refreshSubmissions(cid, aid1);
		line(h.ok && got.unreadable && got.problem == "Couldn't be read" && got.name == "Alex Kim" && got.handedInAt > 0,
		     "s15 damaged: \"Couldn't be read\" with the name and the time",
		     std::to_string(h.ok) + got.problem + "|" + got.name + "|" + std::to_string(got.handedInAt));
		line(s.requests - before == 2, "s15 damaged: not fetched again until h changes (the roster and a 304)");
	}

	// 16. A payload of a newer CedarLogic.
	{
		Membership* m2 = S2->membershipOf(cid);
		const std::string pl = "{\"v\":2,\"kind\":\"submission\",\"name\":\"Alex Kim\",\"cdl\":\"\",\"handedInAt\":1,\"attempt\":9,\"proof\":\"" +
		                       m2->proof + "\"}";
		const int64_t ver = s.classes[cid].subs[aid1][sid2].rec.ver + 1;
		Bytes env;
		std::string why;
		Bytes pub;
		unb64u(m2->pub, pub);
		sealTo(cr, curve, pub, "submission", cid, aid1 + "/" + sid2, ver, pl, false, kMaxRecord, env, why);
		s.setSubmission(cid, aid1, sid2, ver, b64u(env));
		T1->refreshSubmissions(cid, aid1);
		Submission got;
		for (const Submission& x : T1->submissions(cid, aid1))
			if (x.studentId == sid2) got = x;
		line(got.unreadable && got.problem == "Needs a newer CedarLogic" && got.cdl.empty(), "s16 v 2: \"Needs a newer CedarLogic\"");
	}

	// 17. Live: three pushes, S1 follows.
	{
		bool inOrder = true;
		Result g = T1->goLive(cid, kCdlOff);
		std::string changed;
		for (int i = 0; i < 3; i++) {
			if (i) g = T1->push(cid, i % 2 ? on : kCdlOff, nullptr, nullptr, false);
			S1->pulse(cid);
			const Live l = S1->live(cid);
			const bool flagged = S1->liveChangedFlag(changed) && changed == cid;
			inOrder = inOrder && g.ok && flagged && l.on && l.ver == s.classes[cid].live.rec.ver && l.step == i + 1 &&
			          l.cdl == (i % 2 ? on : std::string(kCdlOff));
		}
		const size_t before = S1->requests;
		S1->pulse(cid);
		line(inOrder && S1->requests - before == 1 && !S1->liveChangedFlag(changed), "s17 live: each version once, in order");
	}

	// 18 and 19. Predict, answers, reveal.
	{
		const std::string prompt = "What will the light show when the switch is on?";
		const std::vector<std::string> lights = { "LED" };
		T1->push(cid, kCdlOff, &prompt, &lights, false);
		S1->pulse(cid);
		S2->pulse(cid);
		const Result a = S1->sendAnswer(cid, { { "LED", 1 } });
		const Result b = S2->sendAnswer(cid, { { "LED", 0 } });
		T1->refreshAnswers(cid);
		AnswerCounts c = T1->answers(cid);
		line(a.ok && b.ok && c.answered == 2 && c.perLight["LED"] == std::make_pair(1, 1) && c.students == 2,
		     "s18 predict: 2 answered, LED 1 (1) 0 (1)");
		T1->push(cid, on, &prompt, &lights, true);
		T1->refreshAnswers(cid);   // nothing new answered: the endpoint alone would say 304
		c = T1->answers(cid);
		line(c.answered == 2 && c.right == 1 && c.wrong == 1, "s18 reveal with no new answers: scored at once");
		const Result late = S2->sendAnswer(cid, { { "LED", 0 } });   // to the version just replaced
		S1->pulse(cid);
		S2->pulse(cid);
		T1->refreshAnswers(cid);
		c = T1->answers(cid);
		const Live l1 = S1->live(cid), l2 = S2->live(cid);
		line(c.right == 1 && c.wrong == 1 && l1.reveal && l1.myRight == 1 && l1.myTotal == 1 && l2.myRight == 0 && l2.myTotal == 1,
		     "s18 reveal: right and wrong against the pushed lights; each student's own score");
		line(late.ok && late.value != "dropped", "s19 an answer to the version just replaced is accepted");
		T1->endLive(cid);
		const Result after = S1->sendAnswer(cid, { { "LED", 1 } });
		line(after.ok && after.value == "dropped", "s19 an answer after End Live: not_live, dropped quietly");
	}

	// 21. End Live.
	{
		S1->pulse(cid);
		const Live l = S1->live(cid);
		HttpRequest q;
		q.method = "GET";
		q.url = std::string(FakeServer::liveBase()) + "/" + cid + "/" + s.fetchKeyOf(cid);
		json::Value p;
		json::parse(s.handle(q).body, p);
		line(!l.on && l.ended && p.integer("live", -1) == 0 && !S1->liveOn(cid), "s21 end live: the pulse's live is 0, the view ended");
	}

	// 20. A second teacher device takes over.
	{
		T1->goLive(cid, kCdlOff);
		T2->refreshTeacher(cid);
		const Result x = T2->push(cid, on, nullptr, nullptr, false);
		const Live offer = T2->live(cid);
		const Result y = T2->takeOverLive(cid);
		const Result z = T1->push(cid, kCdlOff, nullptr, nullptr, false);
		line(!x.ok && x.status == 412 && offer.takeOver && y.ok && T2->live(cid).on && !z.ok && z.status == 412 && T1->live(cid).takeOver &&
		         has(z.message, "Take over here?"),
		     "s20 T2 pushes while T1 is live: Take Over, then T1's next push gets the offer", x.message);
		T2->endLive(cid);
	}

	// 22. A stale pulse.
	{
		S1->pulse(cid);
		const int64_t seq = s.classes[cid].seq;
		s.stalePulse(cid, seq - 3, 0);
		const size_t before = S1->requests, notices = S1.notices.size();
		const size_t logs = S1->log.size();
		S1->pulse(cid);
		line(S1->requests - before == 1 && S1.notices.size() == notices && S1->log.size() == logs + 1 && m1->seq == seq,
		     "s22 a stale pulse: ignored quietly, nothing refetched");
	}

	// 23. An older ver listed for an opened hand-in.
	{
		T1->refreshSubmissions(cid, aid1);
		Submission before;
		for (const Submission& x : T1->submissions(cid, aid1))
			if (x.studentId == sid1) before = x;
		const auto cur = s.classes[cid].subs[aid1][sid1].rec;
		s.setSubmission(cid, aid1, sid1, cur.ver - 1, cur.env);
		const size_t logs = T1->log.size();
		T1->refreshSubmissions(cid, aid1);
		Submission after;
		for (const Submission& x : T1->submissions(cid, aid1))
			if (x.studentId == sid1) after = x;
		line(before.ver == cur.ver && after.ver == cur.ver && after.cdl == before.cdl && T1->log.size() > logs,
		     "s23 an older ver listed: ignored with a log line, the opened copy stays",
		     std::to_string(before.ver) + " " + std::to_string(cur.ver) + " " + std::to_string(after.ver) + " " + std::to_string(T1->log.size() - logs));
		s.setSubmission(cid, aid1, sid1, cur.ver, cur.env);
	}

	// 26. A student leaves.
	Device S3(cr, curve, s, nullptr);
	{
		const std::string code = T1->teachingOf(cid)->rec.joinCode;
		S3->join(code, "Jo");
		S3->pulse(cid);
		S3->handIn(cid, aid1, on);
		const std::string sid3 = S3->membershipOf(cid)->studentId;
		T1->refreshSubmissions(cid, aid1);
		const Result l = S3->leaveClass(cid);
		T1->refreshSubmissions(cid, aid1);
		Submission got;
		for (const Submission& x : T1->submissions(cid, aid1))
			if (x.studentId == sid3) got = x;
		line(l.ok && !S3->membershipOf(cid) && !s.classes[cid].roster.count(sid3) && got.left && got.name == "Jo" && got.cdl == on,
		     "s26 leave: roster entry gone, the hand-in stays with the name inside, marked left");
	}

	// 27. The teacher removes students.
	{
		Device S4(cr, curve, s, nullptr);
		const std::string code = T1->teachingOf(cid)->rec.joinCode;
		S4->join(code, "Kai");
		S4->pulse(cid);
		S4->handIn(cid, aid1, on);
		const std::string sid4 = S4->membershipOf(cid)->studentId;
		const std::string oldFetch = s.fetchKeyOf(cid);
		const Result a = T1->removeStudents(cid, { sid2 }, false);
		const Result b = T1->removeStudents(cid, { sid4 }, true);
		T1->refreshSubmissions(cid, aid1);
		bool s2listed = false, s4listed = false;
		for (const Submission& x : T1->submissions(cid, aid1)) {
			s2listed = s2listed || (x.studentId == sid2 && x.left);
			s4listed = s4listed || x.studentId == sid4;
		}
		line(a.ok && b.ok && s2listed && !s4listed && !s.classes[cid].subs[aid1].count(sid4) && s.fetchKeyOf(cid) != oldFetch,
		     "s27 remove: S2's hand-ins stay, S3's go with the box ticked, the fetchKey changed");
		const Result r2 = S2->refreshMember(cid);
		line(!r2.ok && r2.error == "not_a_member" && !S2->membershipOf(cid) && S2.noticed("You were removed from \xE2\x80\x9C" "Digital Logic 101"),
		     "s27 remove: S2's next request is refused, membership forgotten, one notice", r2.message);
		HttpRequest q;
		q.method = "GET";
		q.url = std::string(FakeServer::liveBase()) + "/" + cid + "/" + oldFetch + "/assignment/" + aid1 + "/1";
		json::Value p;
		json::parse(s.handle(q).body, p);
		line(p.str("error") == "wrong_fetch_key", "s27 remove: the old record paths answer wrong_fetch_key");
	}

	// 38. Records forged with the class's public key but another proof.
	{
		S1->pulse(cid);
		Bytes pub, env;
		unb64u(m1->pub, pub);
		std::string why;
		const std::string forgedProof = b64u(Bytes(32, 0x55));
		const int64_t ver = s.classes[cid].subs[aid1][sid1].rec.ver + 1;
		sealTo(cr, curve, pub, "submission", cid, aid1 + "/" + sid1, ver, submissionJson("Sam Lee", on, s.now, "x", 9, forgedProof), true,
		       kMaxRecord, env, why);
		const auto real = s.classes[cid].subs[aid1][sid1].rec;
		s.setSubmission(cid, aid1, sid1, ver, b64u(env));
		Bytes nenv;
		sealTo(cr, curve, pub, "name", cid, sid1, 5, nameJson("Mallory", 1, forgedProof), false, kMaxName, nenv, why);
		s.setName(cid, sid1, 5, b64u(nenv));
		T1->refreshSubmissions(cid, aid1);
		Submission got;
		for (const Submission& x : T1->submissions(cid, aid1))
			if (x.studentId == sid1) got = x;
		std::string name;
		for (const Student& x : T1->students(cid))
			if (x.studentId == sid1) name = x.name;
		line(got.unreadable && got.problem == "Couldn't be verified" && got.cdl.empty() && got.checkVerdict == -1 && name == "Sam Lee",
		     "s38 forged: \"Couldn't be verified\", the pinned name kept");
		// A forged answer isn't counted.
		const std::string prompt = "Light?";
		const std::vector<std::string> lights = { "LED" };
		T1->refreshTeacher(cid);
		T1->goLive(cid, kCdlOff);
		T1->push(cid, kCdlOff, &prompt, &lights, false);
		const Live tl = T1->live(cid);
		Bytes aenv;
		sealTo(cr, curve, pub, "answer", cid, tl.session + "/" + sid1, tl.ver,
		       answerJson(tl.session, tl.ver, { { "LED", 1 } }, s.now, forgedProof), false, kMaxAnswer, aenv, why);
		s.setAnswer(cid, tl.session, sid1, tl.ver, b64u(aenv));
		T1->refreshAnswers(cid);
		line(T1->answers(cid).answered == 0, "s38 forged: the answer isn't counted");
		T1->endLive(cid);
		s.setSubmission(cid, aid1, sid1, real.ver, real.env);   // the real one back
	}

	// 43. Too big to build.
	{
		Device S5(cr, curve, s, nullptr);
		S5->join(T1->teachingOf(cid)->rec.joinCode, "Big");
		S5->pulse(cid);
		std::string huge = "(cedarlogic\n  (version 3)\n  (page 0\n";
		for (int i = 0; i < 25000; i++) huge += "    (gate \"AA_TOGGLE\" (uuid \"" + std::to_string(i) + "\"))\n";
		huge += "))\n";
		const Result h = S5->handIn(cid, aid2, huge);
		const std::string sid5 = S5->membershipOf(cid)->studentId;
		// And a deflate bomb, sealed properly to the class.
		Device S6(cr, curve, s, nullptr);
		S6->join(T1->teachingOf(cid)->rec.joinCode, "Bomb");
		Membership* m6 = S6->membershipOf(cid);
		const std::string bombText(4000001, ' ');
		Bytes bomb;
		cr.deflateRaw(Bytes(bombText.begin(), bombText.end()), bomb);
		Bytes pub, env, nonce(12, 9), eph(32, 0);
		unb64u(m6->pub, pub);
		uint8_t ed[32], epub[65];
		curve.p256Generate(ed, epub);
		eph.assign(ed, ed + 32);
		sealToForTest(cr, curve, pub, eph, "submission", cid, aid2 + "/" + m6->studentId, 1, bombText, 1, nonce, bomb, env);
		s.setSubmission(cid, aid2, m6->studentId, 1, b64u(env));
		T1->refreshSubmissions(cid, aid2);
		std::string p5, p6;
		bool rest = false;
		for (const Submission& x : T1->submissions(cid, aid2)) {
			if (x.studentId == sid5) p5 = x.problem;
			else if (x.studentId == m6->studentId) p6 = x.problem;
			else rest = rest || (x.studentId == sid1 && !x.unreadable);
		}
		line(h.ok && p5 == "Couldn't be read (too big)" && p6 == "Couldn't be read (too big)" && rest,
		     "s43 25,000 gates / a deflate bomb: \"Couldn't be read (too big)\"; the rest of the table fills", p5 + " | " + p6);
	}

	// 32. The request breaker trips: writes pause, reading goes on.
	{
		s.paused = true;
		const Result h = S1->handIn(cid, aid2, kCdlOff);
		const Result p = T1->postAssignment(cid, a1, true);
		const Result st = S1->refreshMember(cid);
		const Result pl = S1->pulse(cid);
		line(h.ok && h.value == "pending" && !p.ok && p.error == "classroom_paused" &&
		         p.message == "Classrooms are resting on the website for today. Your work is safe on this device." && st.ok && pl.ok,
		     "s32 breaker: writes get classroom_paused; the status and the pulse are served", p.message);
		s.paused = false;
		S1.c->load();   // (as after a restart: the pending hand-in is in memberships.json)
		s.now += 2 * kMinute;
		S1->pulse(cid);
		line(S1->membershipOf(cid) && S1->membershipOf(cid)->pending.empty(), "s32 breaker: the pending hand-in goes once it's over");
		m1 = S1->membershipOf(cid);
	}

	// 33. Retry-After 120 on a hand-in.
	{
		s.retryAfterNext = 120;
		const int64_t ver = s.classes[cid].subs[aid1][sid1].rec.ver;
		const Result h = S1->handIn(cid, aid1, kCdlOff);
		const size_t before = s.requests;
		s.now += 60 * kSecond;
		S1->pulse(cid);
		const bool waited = s.classes[cid].subs[aid1][sid1].rec.ver == ver && s.requests - before == 1;
		s.now += 61 * kSecond;
		S1->pulse(cid);
		line(h.ok && h.value == "pending" && waited && s.classes[cid].subs[aid1][sid1].rec.ver > ver,
		     "s33 Retry-After: no hand-in for 120 s, then it goes");
	}

	// 34. A join record under another key, or with a point off the curve.
	{
		Device T9(cr, curve, s, nullptr);
		const std::string cid9 = T9->createClass("Swap").value;
		Teaching* t9 = T9->teachingOf(cid9);
		Bytes secret;
		std::string why;
		decodeCode(cr, CodeKind::Join, t9->rec.joinCode, secret, why);
		const JoinKeys jk = joinKeys(cr, curve, secret);
		Bytes env;
		seal(cr, Bytes(32, 3), "join", cid9, jk.joinId, 2, joinJson("Swap", t9->rec.classKey, t9->rec.pub), false, kMaxSmall, env, why);
		s.swapJoin(cid9, b64u(env));
		const Result a = S2->previewJoinCode(t9->rec.joinCode);
		Bytes pub;
		unb64u(t9->rec.pub, pub);
		pub[64] ^= 1;   // off the curve
		seal(cr, jk.joinKey, "join", cid9, jk.joinId, 3, joinJson("Swap", t9->rec.classKey, b64u(pub)), false, kMaxSmall, env, why);
		s.swapJoin(cid9, b64u(env));
		const Result b = S2->join(t9->rec.joinCode, "Alex");
		line(!a.ok && a.message == "This class's record on the website couldn't be read." && !b.ok && !S2->membershipOf(cid9) &&
		         !s.classes[cid9].roster.size(),
		     "s34 a join record under another key, or a pub off the curve: couldn't be read, nothing joined", a.message + " | " + b.message);

		// 37. The join record swapped for one with another public key, under the same join key.
		uint8_t d[32], other[65];
		curve.p256Generate(d, other);
		seal(cr, jk.joinKey, "join", cid9, jk.joinId, 4, joinJson("Swap", t9->rec.classKey, b64u(Bytes(other, other + 65))), false, kMaxSmall,
		     env, why);
		s.swapJoin(cid9, b64u(env));
		T9->refreshTeacher(cid9);
		std::string warning;
		for (const ClassInfo& c : T9->classes())
			if (c.classId == cid9) warning = c.warning;
		line(warning == "The join record on the website isn't the one your devices wrote. Change the join code.",
		     "s37 a swapped join record: T1 finds another key and says so", warning);
		const Result n = T9->newJoinCode(cid9);
		std::string after = "-";
		for (const ClassInfo& c : T9->classes())
			if (c.classId == cid9) after = c.warning;
		T9->refreshTeacher(cid9);
		for (const ClassInfo& c : T9->classes())
			if (c.classId == cid9) after += c.warning;
		line(n.ok && after == "", "s37 after Change Join Code the warning goes", n.message + "|" + after);
	}

	// 39. What the store holds.
	{
		const std::string dump = s.dump();
		Teaching* t = T1->teachingOf(cid);
		Bytes secret, pub;
		std::string why;
		decodeCode(cr, CodeKind::Join, t->rec.joinCode, secret, why);
		const JoinKeys jk = joinKeys(cr, curve, secret);
		decodeCode(cr, CodeKind::Join, joinCode, secret, why);
		const JoinKeys old = joinKeys(cr, curve, secret);
		unb64u(t->rec.pub, pub);
		const bool clean = !has(dump, jk.joinId) && !has(dump, old.joinId) && !has(dump, sha256Hex(cr, jk.joinToken)) &&
		                   !has(dump, jk.joinToken) && !has(dump, t->rec.pub) && !has(dump, hex(pub)) && !has(dump, t->rec.classKey);
		Bytes pepper;
		clsync::unhex(s.pepperHex(), pepper);
		const std::string index = hmacHex(cr, pepper, jk.joinId);
		line(clean && has(dump, "j/" + index), "s39 the store: no joinId, no hash of a join token, no public key; j/ is the HMAC");
	}

	// 40. A posted code: 60 joins from one address into one class, then one removal.
	{
		const std::string code = T1->teachingOf(cid)->rec.joinCode;
		Bytes secret;
		std::string why;
		decodeCode(cr, CodeKind::Join, code, secret, why);
		const JoinKeys jk = joinKeys(cr, curve, secret);
		s.ip = "203.0.113.9";
		const int64_t since = s.now;
		int created = 0, lastStatus = 0;
		std::vector<std::string> flood;
		for (int i = 0; i < 61; i++) {
			const std::string sid = clsync::newUuid(cr);
			HttpRequest q;
			q.method = "POST";
			q.url = std::string(FakeServer::base()) + "/classes/" + cid + "/students";
			q.headers = { { "Authorization", "Bearer " + jk.joinToken } };
			q.body = "{\"studentId\":\"" + sid + "\",\"tokenHash\":\"" + std::string(64, 'a') + "\",\"name\":{\"ver\":1,\"env\":\"" + b64u(Bytes(200, 2)) + "\"}}";
			lastStatus = s.handle(q).status;
			if (lastStatus == 201) {
				created++;
				flood.push_back(sid);
			}
		}
		std::vector<std::string> recent;
		for (const auto& kv : s.classes[cid].roster)
			if (kv.second.joinedAt >= since) recent.push_back(kv.first);
		const size_t before = s.requests;
		const Result rm = T1->removeStudents(cid, recent, false);
		bool gone = true;
		for (const std::string& f : flood) gone = gone && !s.classes[cid].roster.count(f);
		line(created == 60 && lastStatus == 429 && rm.ok && gone && s.classes[cid].roster.count(sid1),
		     "s40 the 61st join from one address is refused; one request removes the 40 (here 60)");
		(void)before;
		s.ip = "198.51.100.7";
	}

	// 35. Many students join and hand in at once.
	{
		Device T(cr, curve, s, nullptr);
		const std::string c35 = T->createClass("Big lecture").value;
		const std::string code = T->teachingOf(c35)->rec.joinCode;
		Assignment a;
		a.title = "Everyone";
		a.cdl = kCdlOff;
		const std::string aid = T->postAssignment(c35, a, true).value;
		int ok = 0;
		for (int i = 0; i < 30; i++) {
			s.ip = "192.0.2." + std::to_string(i % 4);
			Device st(cr, curve, s, nullptr);
			if (st->join(code, "Student " + std::to_string(i)).ok && st->handIn(c35, aid, on).ok) ok++;
		}
		s.ip = "198.51.100.7";
		T->refreshSubmissions(c35, aid);
		line(ok == 30 && s.classes[c35].subs[aid].size() == 30 && T->submissions(c35, aid).size() == 30,
		     "s35 30 students join and hand in: every write lands, none lost in the index");
	}

	// 36. Classrooms closed on the website.
	{
		s.closed = true;
		Device T(cr, curve, s, nullptr);
		const Result c = T->createClass("Closed");
		const Result st = S1->refreshMember(cid);
		const Result p = S1->pulse(cid);
		line(!c.ok && c.error == "busy" && c.message == "The classroom service is busy. Trying again shortly." && st.ok && p.ok,
		     "s36 CLASSROOM_CLOSED: create gets busy, reads and the pulse work", c.message);
		s.closed = false;
	}

	// 44. Remove from This Device.
	{
		Device T3(cr, curve, s, &side);
		T3->sideChanged();
		const bool had = T3->teachingOf(cid) != nullptr;
		T3->forgetClass(cid);
		T3->sideChanged();
		Device S1c(cr, curve, s, nullptr);
		const Result mc = S1->makeMoveCode(cid);
		S1c->importMoveCode(mc.value);
		S1c->forgetMembership(cid);
		line(had && !T3->teachingOf(cid) && side.recs.size() >= 1 && T1->teachingOf(cid) && s.classes.count(cid),
		     "s44 teacher: gone from that computer only; the sync record intact and not re-added there");
		line(!S1c->membershipOf(cid) && s.classes[cid].roster.count(sid1) && S1->membershipOf(cid), "s44 student: still in the class on the website");
	}

	// 42, 28. Delete an assignment, then the class.
	{
		const Result da = T1->deleteAssignment(cid, aid2);
		S1->pulse(cid);
		bool listed = false;
		for (const Assignment& a : S1->assignments(cid)) listed = listed || a.id == aid2;
		line(da.ok && !listed, "s42 delete an assignment: gone from S1's list");
		const Result dc = T1->deleteClass(cid);
		const Result t2 = T2->refreshTeacher(cid);
		const Result st = S1->refreshMember(cid);
		line(dc.ok && !T1->teachingOf(cid) && !t2.ok && t2.status == 410 && !T2->teachingOf(cid) && T2.noticed("was deleted by the teacher.") &&
		         !st.ok && !S1->membershipOf(cid) && S1.noticed("\xE2\x80\x9C" "Digital Logic 101\xE2\x80\x9D was deleted by the teacher."),
		     "s28 delete: 410 for everyone; T2 and S1 forget it with the notice", st.message);
		line(side.recs.empty(), "s28 delete: the sync record is tombstoned");
		const auto a = std::find(s.purges.begin(), s.purges.end(), "asg-" + aid2);
		const auto c = std::find(a, s.purges.end(), "class-" + cid);
		line(a != s.purges.end() && c != s.purges.end(), "s42 the purge log: asg-<aid>, then class-<classId>");
		HttpRequest q;
		q.method = "GET";
		q.url = std::string(FakeServer::base()) + "/classes/" + cid;
		line(s.handle(q).status == 410, "s28 delete: the id answers 410 to anyone");
	}

	// The state files bring a device back as it was.
	{
		Device T(cr, curve, s, nullptr);
		const std::string c = T->createClass("Files").value;
		Assignment a;
		a.title = "Saved";
		a.cdl = kCdlOff;
		T->postAssignment(c, a, true);
		Device S(cr, curve, s, nullptr);
		S->join(T->teachingOf(c)->rec.joinCode, "Sam");
		S->pulse(c);
		Device T2b(cr, curve, s, nullptr), S2b(cr, curve, s, nullptr);
		T2b.files = T.files;
		S2b.files = S.files;
		T2b->load();
		S2b->load();
		const auto ta = T2b->assignments(c), sa = S2b->assignments(c);
		line(T2b->teachingOf(c) && T2b->teachingOf(c)->rec.d == T->teachingOf(c)->rec.d && ta.size() == 1 && S2b->membershipOf(c) &&
		         S2b->membershipOf(c)->token == S->membershipOf(c)->token && sa.size() == 1 && sa[0].title == "Saved",
		     "files: teaching.json, memberships.json and the cache load back");
		const size_t before = s.requests;
		S2b->pulse(c);
		line(s.requests - before == 1, "files: nothing refetched after a restart");
	}

	// 30. A never-used class is removed after 7 days and re-created quietly.
	{
		Device T(cr, curve, s, nullptr);
		const std::string c30 = T->createClass("Never used").value;
		const std::string code = T->teachingOf(c30)->rec.joinCode;
		const int64_t start = s.now;
		s.now += 8 * kDay;
		s.cleanup();
		const bool removed = !s.classes.count(c30);
		const Result r1 = T->refreshTeacher(c30);
		Device S(cr, curve, s, nullptr);
		const Result j = S->previewJoinCode(code);
		line(removed && r1.ok && s.classes.count(c30) && j.ok && T.notices.empty(), "s30 never used: removed, then re-created quietly; the same code works",
		     r1.message);
		s.now = start;
	}

	// 29. Expiry: the warning from 60 days before, then class_expired.
	{
		Device T(cr, curve, s, nullptr);
		Device S(cr, curve, s, nullptr);
		const std::string c29 = T->createClass("Old class").value;
		S->join(T->teachingOf(c29)->rec.joinCode, "Sam");
		T->refreshTeacher(c29);
		const int64_t start = s.now;
		s.now = start + 345 * kDay;
		std::string warning;
		for (const ClassInfo& c : T->classes())
			if (c.classId == c29) warning = c.warning;
		s.now = start + 401 * kDay;
		s.cleanup();
		const Result r1 = T->refreshTeacher(c29);
		const Result r2 = S->refreshMember(c29);
		line(has(warning, "will be removed from the website on") && has(warning, "unless someone opens it"), "s29 expiry: the warning line", warning);
		line(!r1.ok && r1.error == "class_expired" && has(r1.message, "was removed after 400 days without use.") && !T->teachingOf(c29) &&
		         !S->membershipOf(c29) && S.noticed("was removed after 400 days without use."),
		     "s29 expiry: class_expired for everyone", r1.message);
		s.now = start;
	}

	line(true, "s31 an old sync client reads a classroom record as newer (checked with the vectors: record 12)");

	// Answers no honest server gives (one that's broken, or not ours): nothing breaks, nothing escapes.
	{
		Device T(cr, curve, s, nullptr), S(cr, curve, s, nullptr);
		const std::string c = T->createClass("Odd answers").value;
		S->join(T->teachingOf(c)->rec.joinCode, "Sam");
		Assignment a;
		a.title = "Lab";
		a.cdl = kCdlOff;
		const std::string aid = T->postAssignment(c, a, true).value;
		// Assignment ids that aren't UUIDs: never fetched, never a file name.
		std::vector<std::string> urls;
		auto odd = [&urls, c](const HttpRequest& q, HttpResponse& x) {
			urls.push_back(q.url);
			const std::string tail = "/classes/" + c;
			if (q.method != "GET" || x.status != 200 || q.url.size() < tail.size() || q.url.compare(q.url.size() - tail.size(), tail.size(), tail) != 0) return;
			const std::string from = "\"assignments\":{";
			const size_t at = x.body.find(from);
			if (at != std::string::npos)
				x.body.insert(at + from.size(), "\"../../x\":{\"ver\":1,\"h\":\"00\",\"closesAt\":null},\"a/b\":{\"ver\":1,\"h\":\"00\",\"closesAt\":null},");
		};
		S.tamper = odd;
		T.tamper = odd;
		S->refreshMember(c);
		T->refreshTeacher(c);
		bool fetched = false, filed = false;
		for (const std::string& u : urls) fetched = fetched || has(u, "/assignment/../") || has(u, "/assignment/a/b");
		for (const auto& f : S.files) filed = filed || has(f.first, "..") || has(f.first, "assignments/a/");
		for (const auto& f : T.files) filed = filed || has(f.first, "..") || has(f.first, "assignments/a/");
		const Membership* m = S->membershipOf(c);
		line(!fetched && !filed && m && m->seenAssignments.size() == 1 && m->seenAssignments.count(aid) && S->assignments(c).size() == 1 &&
		         T->teachingOf(c)->assignments.size() == 1,
		     "odd answers: assignment ids that aren't UUIDs are skipped");
		// A server that keeps answering 404 no_class: the class is made again once per refresh, no more.
		size_t puts = 0;
		T.tamper = [&puts, c](const HttpRequest& q, HttpResponse& x) {
			if (q.method == "PUT" && has(q.url, "/classes/" + c) && !has(q.url, "/classes/" + c + "/")) puts++;
			if (q.method == "GET" && has(q.url, "/classes/" + c)) {
				x.status = 404;
				x.body = "{\"error\":\"no_class\",\"message\":\"\"}";
			}
		};
		const Result r1 = T->refreshTeacher(c);
		line(!r1.ok && puts == 1, "odd answers: 404 no_class again and again: re-created once, then an error", std::to_string(puts));
		// A Retry-After far too big: kept to a day, no overflow.
		T.tamper = [](const HttpRequest&, HttpResponse& x) {
			x.status = 429;
			x.headers["retry-after"] = "99999999999999999999";
			x.body = "{\"error\":\"rate_limited\",\"retryAfter\":9223372036854775807}";
		};
		T->refreshTeacher(c);
		line(T->retryAfterMs() > s.now && T->retryAfterMs() <= s.now + kDay, "odd answers: a huge Retry-After is kept to a day",
		     std::to_string(T->retryAfterMs() - s.now));
		T.tamper = nullptr;
		S.tamper = nullptr;
	}
}

}  // namespace test
}  // namespace clclass
