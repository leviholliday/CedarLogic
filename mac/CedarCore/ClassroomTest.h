// The classroom self-test's insides (ClassroomTest*.cpp): the vectors, the
// FakeServer (a port of the server's rules, CLASSROOM.md 3) and the scenarios.
#pragma once

#include "ClassroomInternal.h"
#include "SyncTest.h"   // clsync::test::Report, TestCrypto

#include <map>
#include <set>
#include <string>
#include <vector>

namespace clclass {
namespace test {

using clsync::test::Report;

// The vectors of 7.1 from the JSON text of tests/classroom/vectors.json (embedded as
// ClassroomVectors.h, or another copy of the file).
void vectorTests(Crypto&, Curve&, Report&, const std::string& vectorsJson);
std::string embeddedVectors();

// A Curve that passes through to the platform's, but can be told to fail its key generation.
struct TestCurve : Curve {
	Curve& real;
	bool failGenerate = false;
	explicit TestCurve(Curve& r) : real(r) {}
	bool p256Generate(uint8_t d[32], uint8_t pub[65]) override { return !failGenerate && real.p256Generate(d, pub); }
	bool p256Public(const uint8_t d[32], uint8_t pub[65]) override { return real.p256Public(d, pub); }
	bool p256Ecdh(const uint8_t d[32], const uint8_t peer[65], uint8_t x[32]) override { return real.p256Ecdh(d, peer, x); }
	bool pbkdf2Sha256(Crypto& cr, const uint8_t* pw, size_t pwLen, const uint8_t* salt, size_t saltLen, uint32_t rounds,
	                  uint8_t out[32]) override {
		return real.pbkdf2Sha256(cr, pw, pwLen, salt, saltLen, rounds, out);
	}
};

// The server's rules (CLASSROOM.md 3.3-3.8) in memory: what the scenarios run against.
// Requests go to `base` (/api/classroom/v1) and `liveBase` (/api/live/v1).
class FakeServer {
public:
	explicit FakeServer(Crypto&);
	static const char* base() { return "https://fake.test/api/classroom/v1"; }
	static const char* liveBase() { return "https://fake.test/api/live/v1"; }
	HttpResponse handle(const HttpRequest&);

	// Controls (the mock server's /__mock/ ones).
	int64_t now = 1759600000000LL;
	std::string ip = "198.51.100.7";
	int maxStudents = 300;
	bool closed = false, paused = false;
	int offline = 0;                       // the next n requests get no answer
	int retryAfterNext = 0;                // the next write answers 429 with this Retry-After (s)
	std::vector<std::string> purges;       // tags purged, in order
	size_t requests = 0;
	std::vector<std::string> waits;        // every x-cedarlogic-wait asked: "<url> wait <s>"
	void cleanup();                        // the daily job (3.11)
	void flipSubmissionByte(const std::string& classId, const std::string& aid, const std::string& sid);
	void setSubmission(const std::string& classId, const std::string& aid, const std::string& sid, int64_t ver,
	                   const std::string& envB64);   // as the store stub would (scenarios 16, 23, 38)
	void setAnswer(const std::string& classId, const std::string& session, const std::string& sid, int64_t ver,
	               const std::string& envB64);
	void setName(const std::string& classId, const std::string& sid, int64_t ver, const std::string& envB64);
	bool swapJoin(const std::string& classId, const std::string& envB64);   // tamper {swapJoin}
	void stalePulse(const std::string& classId, int64_t seq, int64_t live);  // the CDN serves an old copy
	std::string dump() const;              // every stored key and value, as text
	std::string fetchKeyOf(const std::string& classId) const;
	std::string pepperHex() const;

	struct Rec { int64_t ver = 0, size = 0, at = 0; std::string h, env; };
	struct Sub { Rec rec; int attempts = 0; int64_t firstAt = 0; };
	struct Answer { int64_t ver = 0, at = 0; std::string h, env; };
	struct Member { std::string hash; int64_t joinedAt = 0, seenAt = 0; Rec name; };
	struct LiveSlot { Rec rec; std::string session; bool on = false, predict = false; };
	// v2 (3.16.2, 3.16.10): an item; releasedAt / releaseAt 0 = null.
	struct ItemRow { Rec rec; bool hidden = false; int64_t releasedAt = 0, releaseAt = 0; };
	struct Klass {
		std::string teacherHash, deleteHash;
		int64_t createdAt = 0, activeAt = 0, seq = 0;
		std::string fetchKey, joinIndex;
		Rec join;
		bool joinOpen = true;
		Rec teacher, info;
		std::map<std::string, Rec> assignments;
		std::map<std::string, ItemRow> items;
		std::map<std::string, int64_t> closesAt;          // -1: null
		LiveSlot live;
		std::map<std::string, Member> roster;
		std::map<std::string, std::map<std::string, Sub>> subs;
		std::map<std::string, std::map<std::string, Answer>> answers;
		int64_t bytes = 0;
		int64_t staleSeq = -1, staleLive = -1;
	};
	std::map<std::string, Klass> classes;

private:
	struct JoinEntry { std::string classId, joinHash, env; bool open = true; int64_t ver = 0, at = 0; };
	struct Move { std::string classId, studentId, env; int64_t createdAt = 0; };
	Crypto& cr;
	Bytes pepper;
	std::map<std::string, JoinEntry> joins;
	std::map<std::string, Move> moves;
	struct Pass { std::string classId, studentId, hash, env; int64_t createdAt = 0, usedAt = 0, uses = 0; };
	std::map<std::string, Pass> passes;                   // 3.17, by pid = SHA-256(passId)
	std::map<std::string, std::string> gone;              // classId -> deleted | expired
	std::map<std::string, int> counters;

	HttpResponse api(const HttpRequest&, const std::string& path);
	HttpResponse pulse(const HttpRequest&, const std::string& path);
	bool bump(const std::string& key, int limit);
	std::string newFetchKey();
	void releaseDue(Klass&);   // 3.16.10: every scheduled item whose releaseAt <= now
};

void scenarioTests(Crypto&, Curve&, const std::string& tempDir, Report&);
void engineTests(Crypto&, Curve&, const std::string& tempDir, Report&);
void serverTests(Crypto&, Curve&, const std::string& tempDir, Report&, Host& http, const std::string& serverBase,
                 const std::string& liveBase);
// The live connection and held polls (ClassroomTestLive.cpp): Clients and LiveLinks on a FakeHub,
// the threaded engine with sockets, and a live round against a real server through `host`.
void liveTests(Crypto&, Curve&, const std::string& tempDir, Report&);
void engineLiveTests(Crypto&, Curve&, const std::string& tempDir, Report&);
void liveServerTests(Crypto&, Curve&, Report&, Host& host, const std::string& serverBase, const std::string& liveBase);

}  // namespace test
}  // namespace clclass
