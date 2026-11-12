// A message from CedarLogic (mac/CedarCore/AppMessage.h): which one is due, and what's refused.
// Headless, in CI under the sanitizers with the rest of mac/Tools/headless; also checks the real
// message.json when given its path (the releases repo's).
//   message_check [message.json]
#include "AppMessage.h"
#include "CedarMessage.h"
#include "SyncJson.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <vector>

static int fails = 0;
#define CHECK(c, what) do { if (c) printf("  ok   %s\n", what); else { printf("  FAIL %s\n", what); fails++; } } while (0)

using clmessage::Message;
using clmessage::pick;

static std::string one(const std::string& fields) { return R"({"messages":[{)" + fields + "}]}"; }
static const std::string kGood = R"("id":"one-year-2027","from":"2027-11-05","title":"It's been a year.","text":"Thank you.","button":"Fill Out the Form","url":"https://cedarlogic.netlify.app/one-year/")";
static bool due(const std::string& json, const std::string& today = "2027-11-05", const std::string& platform = "mac", const std::string& shown = "") {
	Message m;
	return pick(json, today, platform, shown, m);
}

int main(int argc, char** argv) {
	printf("the dates\n");
	CHECK(!due(one(kGood), "2027-11-04"), "not the day before");
	CHECK(due(one(kGood), "2027-11-05"), "on the day");
	CHECK(due(one(kGood), "2028-03-01"), "after it, with no end");
	CHECK(due(one(kGood + R"(,"until":"2027-12-31")"), "2027-12-31") && !due(one(kGood + R"(,"until":"2027-12-31")"), "2028-01-01"), "until: the last day, not after");
	CHECK(!due(one(kGood), "2027-11-5") && !due(one(kGood), "") && !due(one(kGood), "2027/11/05"), "today must be YYYY-MM-DD");
	CHECK(!due(one(R"("id":"a","from":"2027-13-01","title":"t","text":"x","url":"https://cedarlogic.netlify.app/")"), "2028-01-01"), "an odd from date skips it");

	printf("answered, and where\n");
	CHECK(!due(one(kGood), "2027-11-06", "mac", "one-year-2027"), "done with: not again");
	CHECK(due(one(kGood), "2027-11-06", "mac", "something-else"), "another id answered doesn't count");
	CHECK(!due(one(kGood), "2027-11-07", "mac", "a,one-year-2027@2027-11-08,b") && due(one(kGood), "2027-11-08", "mac", "a,one-year-2027@2027-11-08,b"),
	      "Remind Me Later: not before its day, then again");
	CHECK(due(one(kGood), "2027-11-06", "mac", "one-year-2027@junk,,@,ONE"), "a kept line that's damaged counts for nothing");
	CHECK(due(one(kGood + R"(,"platforms":["linux","windows"])"), "2027-11-05", "linux") && !due(one(kGood + R"(,"platforms":["linux","windows"])")), "platforms");
	CHECK(!due(one(kGood + R"(,"platforms":"mac")")), "platforms must be a list");

	printf("the first one due wins\n");
	{
		const std::string two = R"({"messages":[{"id":"later","from":"2030-01-01","title":"t","text":"x","url":"https://cedarlogic.netlify.app/a/"},)"
		                        R"({"id":"bad","from":"2020-01-01","title":"t","text":"x","url":"https://evil.example/"},)"
		                        R"({)" + kGood + "}]}";
		Message m;
		CHECK(pick(two, "2027-11-05", "mac", "", m) && m.id == "one-year-2027", "skips one not due and one that's wrong");
		CHECK(m.title == "It's been a year." && m.button == "Fill Out the Form", "keeps its words");
	}

	printf("only our site\n");
	for (const char* u : { "https://evil.example/one-year/", "http://cedarlogic.netlify.app/one-year/", "https://cedarlogic.netlify.app.evil.example/",
	                       "https://cedarlogic.netlify.app@evil.example/", "https://cedarlogic.netlify.app", "https://cedarlogic.netlify.app/a b",
	                       "https://cedarlogic.netlify.app/\"onclick", "https://cedarlogic.netlify.app/\\evil", "javascript:alert(1)", "" }) {
		const std::string label = std::string("refused: ") + (*u ? u : "(empty)");
		CHECK(!due(one(R"("id":"x","from":"2020-01-01","title":"t","text":"x","url":)" + clsync::json::quote(u))), label.c_str());
	}

	printf("sizes and shapes\n");
	CHECK(!due(one(R"("id":"x","from":"2020-01-01","title":")" + std::string(81, 't') + R"(","text":"x","url":"https://cedarlogic.netlify.app/")")), "a title over 80");
	CHECK(due(one(R"("id":"x","from":"2020-01-01","title":")" + std::string(40, 't') + "\xC3\xA9" + R"(","text":"x","url":"https://cedarlogic.netlify.app/")")), "UTF-8 counts characters");
	CHECK(!due(one(R"("id":"x","from":"2020-01-01","title":"t","text":")" + std::string(601, 'x') + R"(","url":"https://cedarlogic.netlify.app/")")), "a text over 600");
	CHECK(!due(one(R"("id":"Has Caps","from":"2020-01-01","title":"t","text":"x","url":"https://cedarlogic.netlify.app/")")), "an id that isn't a-z, 0-9, -");
	CHECK(!due(one(R"("id":"x","from":"2020-01-01","text":"x","url":"https://cedarlogic.netlify.app/")")), "no title");
	{
		Message m;
		CHECK(pick(one(R"("id":"x","from":"2020-01-01","title":"t","text":"x","url":"https://cedarlogic.netlify.app/")"), "2027-01-01", "mac", "", m) && m.button == "Open" && m.later == 3 && m.eyebrow.empty(),
		      "no button: Open; later: 3 days; no eyebrow");
		CHECK(pick(one(kGood + R"(,"eyebrow":"One year","note":"Two minutes","later":7)"), "2027-11-05", "mac", "", m) && m.eyebrow == "One year" && m.note == "Two minutes" && m.later == 7,
		      "eyebrow, note, later");
		CHECK(!due(one(kGood + R"(,"later":0)")) && !due(one(kGood + R"(,"later":61)")) && !due(one(kGood + R"(,"later":"3")")), "later from 1 to 60 days");
		CHECK(!due(one(kGood + R"(,"eyebrow":")" + std::string(41, 'e') + "\"")) && !due(one(kGood + R"(,"note":")" + std::string(91, 'n') + "\"")), "an eyebrow over 40, a note over 90");
	}
	std::string big = one(kGood);
	big.insert(big.size() - 1, std::string(",\"pad\":\"") + std::string(17000, ' ') + "\"");
	CHECK(!due(big), "a file over 16 KB");
	for (const char* bad : { "", "null", "[]", "{}", "{\"messages\":{}}", "{\"messages\":[1,\"x\",null]}", "{\"messages\":[]", "\xff\xfe" })
		CHECK(!due(bad), (std::string("nothing from: ") + (bad[0] == '\xff' ? "(not UTF-8)" : *bad ? bad : "(empty)")).c_str());

	printf("the link\n");
	{
		Message m;
		m.url = "https://cedarlogic.netlify.app/one-year/";
		CHECK(clmessage::link(m, "windows", "0.4.2") == "https://cedarlogic.netlify.app/one-year/?from=windows&v=0.4.2", "?from=&v=");
		CHECK(clmessage::link(m, "linux", "0.4 beta&x=1") == "https://cedarlogic.netlify.app/one-year/?from=linux", "an odd version is left out");
		m.url += "?a=1";
		CHECK(clmessage::link(m, "mac", "") == "https://cedarlogic.netlify.app/one-year/?a=1&from=mac", "& after a query");
	}

	printf("the days\n");
	CHECK(clmessage::addDays("2027-11-05", 3) == "2027-11-08", "+3");
	CHECK(clmessage::addDays("2027-12-30", 3) == "2028-01-02", "over the new year");
	CHECK(clmessage::addDays("2028-02-28", 1) == "2028-02-29" && clmessage::addDays("2027-02-28", 1) == "2027-03-01", "leap years");
	CHECK(clmessage::addDays("nope", 1).empty(), "not a date");

	printf("what's kept\n");
	CHECK(clmessage::remember("", "one-year-2027", "2027-11-05", 3) == "one-year-2027@2027-11-08", "Remind Me Later");
	CHECK(clmessage::remember("a,one-year-2027@2027-11-08", "one-year-2027", "2027-11-08", 0) == "a,one-year-2027", "then done with: replaced, others kept");
	CHECK(clmessage::remember("a,,b@x,c@2027-01-01", "Bad Id", "2027-11-05", 0) == "a,c@2027-01-01", "damage dropped; a bad id isn't kept");

	printf("the C interface\n");
	{
		const std::string j = one(kGood);
		CLAppMessage* m = cl_message_pick(j.data(), j.size(), "2027-11-05", "mac", "0.4.2", "other");
		CHECK(m && strcmp(m->id, "one-year-2027") == 0 && strcmp(m->link, "https://cedarlogic.netlify.app/one-year/?from=mac&v=0.4.2") == 0 && m->later == 3 &&
		      strcmp(m->eyebrow, "") == 0, "picks and links");
		char* kept = cl_message_remember("other", m ? m->id : "", "2027-11-05", m ? m->later : 0);
		CHECK(kept && strcmp(kept, "other,one-year-2027@2027-11-08") == 0, "remembers");
		CHECK(!cl_message_pick(j.data(), j.size(), "2027-11-07", "mac", "0.4.2", kept), "held back till then");
		free(kept);
		cl_message_free(m);
		CHECK(!cl_message_pick(j.data(), j.size(), "2027-11-05", "mac", "0.4.2", "one-year-2027"), "NULL once done with");
		CHECK(!cl_message_pick(nullptr, 0, "2027-11-05", "mac", nullptr, nullptr), "NULL for no file");
		cl_message_free(nullptr);
	}

	if (argc > 1) {
		printf("the real file: %s\n", argv[1]);
		std::ifstream in(argv[1], std::ios::binary);
		std::ostringstream s;
		s << in.rdbuf();
		// Every message in it is shown on its own first day (on the platforms it's for).
		clsync::json::Value root;
		const bool parsed = clsync::json::parse(s.str(), root) && root.get("messages") && root.get("messages")->isArray() && !root.get("messages")->a.empty();
		CHECK(parsed, "it's a list of messages");
		if (parsed)
			for (const clsync::json::Value& v : root.get("messages")->a) {
				const std::string id = v.str("id"), from = v.str("from");
				Message m;
				bool ok = false;
				for (const char* p : { "mac", "windows", "linux" }) ok = ok || pick(s.str(), from, p, "", m);
				CHECK(ok && m.id == id, ("\"" + id + "\" is shown from " + from + (ok ? ": \"" + m.title + "\", opening " + m.url : " (a rule is broken)")).c_str());
			}
	}

	printf(fails ? "message_check: %d FAILED\n" : "message_check: all passed\n", fails);
	return fails ? 1 : 0;
}
