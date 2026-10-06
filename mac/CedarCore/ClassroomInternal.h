// The classroom core's insides, shared by Classroom*.cpp and the self-test.
// Not for the apps: they use Classroom.h (C++) or CedarClassroom.h (C). Plain
// C++17, no platform headers.
#pragma once

#include "Classroom.h"
#include "SyncInternal.h"   // hex, b64u, hkdfSalted, json, files, HttpRequest

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace clclass {

namespace json = clsync::json;
using clsync::Crypto;
using clsync::HttpRequest;
using clsync::HttpResponse;

// ---- Constants (CLASSROOM.md 1-3) ------------------------------------------------------

constexpr size_t kMaxPlaintext = 4000000;     // what a classroom record may inflate to (1.5)
constexpr size_t kMaxRecord = 524288;         // assignment, submission, live
constexpr size_t kMaxSmall = 4096;            // teacher, join, info, move
constexpr size_t kMaxName = 640;
constexpr size_t kMaxAnswer = 2048;
constexpr size_t kMaxKey = 65536;             // a sealed key inside an assignment
constexpr int kMaxGates = 20000, kMaxSegments = 50000;   // a received circuit (4.9)
constexpr uint32_t kJoinRounds = 600000;
constexpr int64_t kSecond = 1000, kMinute = 60 * kSecond, kHour = 60 * kMinute, kDay = 24 * kHour;
extern const char* const kWebBase;            // https://cedarlogic.netlify.app/classroom/
extern const char* const kAppBase;            // cedarlogic://classroom
extern const char* const kSalt;               // cedarlogic-classroom-v1
extern const char* const kJoinSalt;           // cedarlogic-classroom-join-v1

// ---- Codes (1.2) ------------------------------------------------------------------------

size_t codeBytes(CodeKind k);                 // 16 or 6
size_t codeSymbols(CodeKind k);               // 28 or 12
std::string encodeCode(Crypto&, CodeKind, const uint8_t* secret);
// Steps 1-5 of 1.2: the canonical symbols, or false with why = kind | symbol | length.
bool normalizeCode(CodeKind, const std::string& text, std::string& code, std::string& why);
// All of 1.2 (the checksum too): the secret bytes.
bool decodeCode(Crypto&, CodeKind, const std::string& text, Bytes& secret, std::string& why);

// ---- Keys (1.3) ---------------------------------------------------------------------------

Bytes hkdf(Crypto&, const Bytes& ikm, const std::string& info, size_t len);   // salt cedarlogic-classroom-v1
bool pbkdf2Loop(Crypto&, const uint8_t* pw, size_t pwLen, const uint8_t* salt, size_t saltLen, uint32_t rounds, uint8_t out[32]);

struct TeacherKeys {
	std::string classId, teacherToken, teacherHash, deleteToken, deleteHash;
	Bytes backupKey;
	bool valid() const { return backupKey.size() == 32; }
};
TeacherKeys teacherKeys(Crypto&, const Bytes& secret);
struct JoinKeys {
	Bytes stretched;
	std::string joinId, joinToken;
	Bytes joinKey;
	bool valid() const { return joinKey.size() == 32; }
};
JoinKeys joinKeys(Crypto&, Curve&, const Bytes& secret);   // the 600,000 rounds: slow
struct MoveKeys {
	std::string moveId;
	Bytes moveKey;
	bool valid() const { return moveKey.size() == 32; }
};
MoveKeys moveKeys(Crypto&, const Bytes& secret);
std::string hmacHex(Crypto&, const Bytes& key, const std::string& text);   // the server's join index and hash

// ---- Envelopes (1.5) ------------------------------------------------------------------

int envelopeOf(const std::string& kind);      // 1, 2, or 0 for a kind with none
std::string aadText(const std::string& kind, const std::string& classId, const std::string& id, int64_t ver, int flags);
// The real seals: a fresh nonce (and a fresh ephemeral key). False and why ("rng", "cipher",
// "point", "too big") if anything failed or the envelope would pass maxEnvelope.
bool seal(Crypto&, const Bytes& key, const std::string& kind, const std::string& classId, const std::string& id, int64_t ver,
          const std::string& payload, bool compress, size_t maxEnvelope, Bytes& env, std::string& why);
bool sealTo(Crypto&, Curve&, const Bytes& pub, const std::string& kind, const std::string& classId, const std::string& id,
            int64_t ver, const std::string& payload, bool compress, size_t maxEnvelope, Bytes& env, std::string& why);
// Test vectors only: the nonce (and the ephemeral scalar) are given; `deflated`, if not empty, is
// used as the deflated plaintext (so a vector made with another deflate seals the same bytes).
bool sealForTest(Crypto&, const Bytes& key, const std::string& kind, const std::string& classId, const std::string& id,
                 int64_t ver, const std::string& payload, int flags, const Bytes& nonce, const Bytes& deflated, Bytes& env);
bool sealToForTest(Crypto&, Curve&, const Bytes& pub, const Bytes& ephemeralD, const std::string& kind,
                   const std::string& classId, const std::string& id, int64_t ver, const std::string& payload, int flags,
                   const Bytes& nonce, const Bytes& deflated, Bytes& env);
// The key that opens a record: the symmetric key for 0x01, the private scalar and its public
// key for 0x02.
struct OpenKey {
	Bytes key;      // 32
	Bytes d, pub;   // 32, 65
};
// False: damaged; why = version | format | short | point | tag | inflate.
bool open(Crypto&, Curve&, const Bytes& env, const std::string& kind, const std::string& classId, const std::string& id,
          int64_t ver, const OpenKey& k, std::string& payload, std::string& why);
bool validPoint(Curve&, const Bytes& pub);    // 65 bytes, 0x04, on the curve

// ---- Payloads (2.2) -----------------------------------------------------------------------

// "" (readable, `out` the object), "newer" or "invalid", in 2.2's order.
std::string readPayload(const std::string& bytes, const std::string& expectedKind, json::Value& out);
// What a person's or a class's name becomes when written and read: ASCII whitespace trimmed,
// control characters dropped, at most `max` scalar values, `empty` if nothing is left.
std::string cleanName(const std::string& s, size_t max, const std::string& empty);
std::string cutText(const std::string& s, size_t max);   // trimmed and cut (titles, instructions, prompts)

struct TeacherRec {
	std::string name, d, pub, classKey, joinCode;   // d, pub, classKey base64url
	bool joinOpen = true;
	int64_t createdAt = 0, modifiedAt = 0;
};
std::string teacherJson(const TeacherRec&);
bool teacherFrom(const json::Value&, TeacherRec&);
std::string joinJson(const std::string& name, const std::string& classKey, const std::string& pub);
std::string infoJson(const std::string& name, int64_t modifiedAt);
struct AssignmentRec {
	std::string title, instructions, cdl;
	int64_t dueAt = -1;           // -1: null
	bool closeAfterDue = false;
	int keyMode = 0;              // 0 none, 1 text, 2 sealed
	std::string keyText, keyNames, keySealed;
	int64_t createdAt = 0, modifiedAt = 0;
};
std::string assignmentJson(const AssignmentRec&);
bool assignmentFrom(const json::Value&, AssignmentRec&);
std::string keyJson(const std::string& text, const std::string& names);
struct LiveRec {
	std::string session, cdl, prompt;
	int step = 0;
	bool hasPredict = false, reveal = false, ended = false;
	std::vector<std::string> lights;
	int64_t at = 0;
};
std::string liveJson(const LiveRec&);
bool liveFrom(const json::Value&, LiveRec&);
std::string nameJson(const std::string& name, int64_t joinedAt, const std::string& proof);
std::string submissionJson(const std::string& name, const std::string& cdl, int64_t handedInAt, const std::string& client,
                           int attempt, const std::string& proof);
std::string answerJson(const std::string& session, int64_t ver, const std::map<std::string, int>& lights, int64_t at,
                       const std::string& proof);
struct MoveRec {
	std::string classId, studentId, token, proof, classKey, pub, name, className;
};
std::string moveJson(const MoveRec&);
bool moveFrom(const json::Value&, MoveRec&);
std::string classroomJson(const std::string& classId, const std::string& teacherKey, const std::string& name, int64_t createdAt,
                          int64_t modifiedAt, const std::string& device, const std::string& deviceId);

// Parts in a circuit's text, counted before anything is built from it (4.9).
void countParts(const std::string& cdl, int& gates, int& segments);
bool tooBig(const std::string& cdl);

// ---- The client: one device's classes, one request at a time ------------------------------

// What the client needs of the world. Everything optional has a quiet default.
struct ClientHooks {
	std::function<HttpResponse(const HttpRequest&)> http;
	std::function<int64_t()> now;                                         // ms since 1970, this device's clock
	std::function<std::string(const std::string&)> load;
	std::function<bool(const std::string&, const std::string&)> save;
	std::function<void(const std::string&)> removeTree;
	std::function<void(const std::string&)> notice;
	std::function<std::vector<std::pair<std::string, std::string>>()> sideRecords;
	std::function<void(const std::string&, const std::string&)> putSide;
	std::function<void(const std::string&)> deleteSide;
	std::function<bool(const std::string&, const std::string&, const std::string&, int&, std::string&)> check;
	std::function<bool(const std::string&, const std::vector<std::string>&, std::map<std::string, int>&)> lights;
};

struct Result {
	bool ok = false;
	std::string message;     // a sentence for the person (or "" when ok)
	std::string value;       // a classId, a class name, a code, ...
	int status = 0;          // the last HTTP status (0: no answer)
	std::string error;       // the server's error code, if any
	static Result good(const std::string& v = std::string()) { Result r; r.ok = true; r.value = v; return r; }
	static Result bad(const std::string& m, int s = 0, const std::string& e = std::string()) {
		Result r; r.message = m; r.status = s; r.error = e; return r;
	}
};

struct Checked { std::string h; int verdict = -1; std::string summary; };

struct Teaching {
	std::string classId, teacherKey;
	TeacherRec rec;
	int64_t ver = 0;                  // the teacher record
	std::string h;
	int64_t joinVer = 0;              // the join record this device last checked (4.1)
	std::string joinH;
	int64_t infoVer = 0, seq = 0, liveVer = 0, expiresAt = 0, expiresWarnedAt = 0;
	std::string fetchKey, etag, joinProblem;
	std::map<std::string, std::pair<int64_t, std::string>> assignments;   // aid -> (ver, h) seen
	std::map<std::string, std::string> proofs;                            // sid -> the pinned proof (2.2)
	std::map<std::string, Checked> checked;                               // "aid/sid"
	std::map<std::string, std::pair<int64_t, std::string>> subMarks;      // "aid/sid" -> (ver, h) high-water marks
	// the live session this device runs (4.4)
	LiveRec live;
	bool liveOn = false, takeOver = false;
	bool otherLive = false;           // the status says another device's session is on
	std::string liveCdl;              // the last pushed circuit, for scoring a reveal
	int64_t predictSince = 0;         // the live ver that asked the current question
	LiveRec pendingRec;               // a write refused with 412 (Take Over sends it again)
	bool pendingOn = true;
	std::string sideRid;              // the sync side record that carries this class (2.5)
};

struct Pending { std::string cdl; int attempt = 0; int64_t at = 0; };
struct Membership {
	std::string classId, studentId, token, proof, classKey, pub, fetchKey, name, className;
	int64_t joinedAt = 0, seq = 0, infoVer = 0, expiresAt = 0, liveSeen = 0, nameVer = 1;
	std::map<std::string, int64_t> seenAssignments;               // aid -> ver
	std::map<std::string, int64_t> seenSubmissions;               // aid -> the ver of the last hand-in
	std::map<std::string, int64_t> handedInAt;                    // aid -> ms
	std::map<std::string, std::string> handedInHash;              // aid -> cdlHash of what was handed in
	std::map<std::string, int> attempts;                           // aid -> hand-ins so far
	std::map<std::string, Pending> pending;
	int wrongToken = 0;                                           // 401s in a row (4.11)
};

// Engines whose thread was still inside a host call when they were destroyed, and so were
// left to finish (leaked), as Sync's.
int detachedEngines();

class Client {
public:
	Client(Config, Crypto&, Curve&, ClientHooks);

	void load();   // teaching.json, memberships.json and the caches
	void save();

	// Teacher.
	Result createClass(const std::string& name);
	Result previewTeacherKey(const std::string& text);
	Result addTeacherKey(const std::string& text, bool quiet = false);
	Result renameClass(const std::string& classId, const std::string& name);
	Result setJoinOpen(const std::string& classId, bool open);
	Result newJoinCode(const std::string& classId);
	void forgetClass(const std::string& classId);
	Result deleteClass(const std::string& classId);
	Result refreshTeacher(const std::string& classId);                   // the status (4.1)
	Result postAssignment(const std::string& classId, const Assignment& draft, bool studentsCanCheck);
	Result deleteAssignment(const std::string& classId, const std::string& aid);
	Result refreshStudents(const std::string& classId);
	Result removeStudents(const std::string& classId, const std::vector<std::string>& sids, bool deleteHandIns);
	Result refreshSubmissions(const std::string& classId, const std::string& aid);
	Result goLive(const std::string& classId, const std::string& cdl);
	Result push(const std::string& classId, const std::string& cdl, const std::string* prompt,
	            const std::vector<std::string>* lights, bool reveal);
	Result endLive(const std::string& classId);
	Result takeOverLive(const std::string& classId);
	Result refreshAnswers(const std::string& classId);

	// Student.
	Result previewJoinCode(const std::string& text);   // value: "<class name>\n<1|0>"
	Result join(const std::string& text, const std::string& name);
	Result rename(const std::string& classId, const std::string& name);
	Result handIn(const std::string& classId, const std::string& aid, const std::string& cdl);
	Result sendAnswer(const std::string& classId, const std::map<std::string, int>& lights);
	Result makeMoveCode(const std::string& classId);
	Result previewMoveCode(const std::string& text);   // value: "<class name>\n<student name>"
	Result importMoveCode(const std::string& text);
	Result leaveClass(const std::string& classId);
	void forgetMembership(const std::string& classId);
	Result pulse(const std::string& classId);          // one poll (4.8); sends pending hand-ins
	Result refreshMember(const std::string& classId);  // the status

	// Sync's side records (2.5).
	void sideChanged();

	// Views.
	std::vector<ClassInfo> classes() const;
	std::vector<Assignment> assignments(const std::string& classId) const;
	std::vector<Student> students(const std::string& classId) const;
	std::vector<Submission> submissions(const std::string& classId, const std::string& aid) const;
	Live live(const std::string& classId) const;
	AnswerCounts answers(const std::string& classId) const;
	Status status(const std::string& classId) const;
	bool teaches(const std::string& classId) const { return teaching_.count(classId) > 0; }
	bool member(const std::string& classId) const { return members_.count(classId) > 0; }
	bool liveOn(const std::string& classId) const;     // a session is on (student: the pulse says so)
	bool predictOpen(const std::string& classId) const;
	int64_t pollSeconds() const { return pollSeconds_; }
	int64_t retryAfterMs() const { return retryAfter_; }
	bool liveChangedFlag(std::string& classId);        // a new live version since the last call
	bool answersChangedFlag(std::string& classId);

	// Tests.
	Teaching* teachingOf(const std::string& classId);
	Membership* membershipOf(const std::string& classId);
	size_t requests = 0;
	std::vector<std::string> log;      // quiet log lines (4.3, 4.9)

private:
	struct Api {
		int status = 0;
		bool sent = false;
		json::Value body;
		std::string error, etag;
		int64_t retryAfterMs = 0;
	};
	Api call(const std::string& method, const std::string& url, const std::vector<std::pair<std::string, std::string>>& headers,
	         const json::Value* body, const std::string& ifNoneMatch = std::string());
	Api teacherCall(Teaching& t, const std::string& method, const std::string& path, const json::Value* body,
	                const std::string& ifNoneMatch = std::string());
	Api studentCall(Membership& m, const std::string& method, const std::string& path, const json::Value* body,
	                const std::string& ifNoneMatch = std::string());
	Result fail(const Api& a, const std::string& context);
	int64_t now() { return hooks_.now ? hooks_.now() : 0; }
	int64_t serverNow() { return now() + offset_; }
	void note(const std::string& t);

	const JoinKeys& joinKeysFor(const std::string& code);       // cached: the stretching is slow
	bool keysOf(const Teaching& t, TeacherKeys& k) const;
	OpenKey teacherOpenKey(const Teaching& t) const;
	Bytes classKeyOf(const Teaching& t) const;
	Result putClass(Teaching& t, bool fresh);                    // create or re-create (3.3)
	bool sealTeacher(Teaching& t, int64_t ver, Bytes& env);
	bool sealJoin(Teaching& t, const JoinKeys& jk, int64_t ver, Bytes& env);
	bool sealInfo(Teaching& t, int64_t ver, Bytes& env);
	bool openTeacherStatus(Teaching& t, const json::Value& status, std::string& problem);
	void checkJoinRecord(Teaching& t, const json::Value& join);
	Result liveWrite(Teaching& t, const LiveRec& rec, bool on, int64_t base);
	void forgetClassLocal(const std::string& classId, bool tombstone);
	void forgetMembershipLocal(const std::string& classId, const std::string& notice);
	Result gone(const std::string& classId, const Api& a, bool teaching);
	Result fetchAssignment(const std::string& classId, const std::string& fetchKey, const Bytes& classKey,
	                       const std::string& aid, int64_t ver, const std::string& h, bool teacher);
	Result fetchLive(Membership& m, int64_t ver);
	Result sendPending(Membership& m);
	Result handInNow(Membership& m, const std::string& aid, const std::string& cdl, int attempt, bool queueOffline);
	bool memberStatus(Membership& m, const json::Value& s);
	std::string cachePath(const std::string& classId, const std::string& name) const;
	void saveCache(const std::string& classId, const std::string& name, const std::string& text);
	std::string loadCache(const std::string& classId, const std::string& name) const;

	Config cfg_;
	Crypto& cr_;
	Curve& curve_;
	ClientHooks hooks_;
	std::map<std::string, Teaching> teaching_;
	std::map<std::string, Membership> members_;
	std::vector<std::string> removedSide_;                         // sync record ids not to add back (2.5)
	std::map<std::string, JoinKeys> joinCache_;                    // code -> keys
	std::map<std::string, Assignment> asgCache_;                   // "classId/aid" -> the opened assignment
	std::map<std::string, Student> roster_;                        // "classId/sid"
	std::map<std::string, int64_t> nameVer_;                       // "classId/sid" -> the name record's ver opened
	std::map<std::string, int64_t> closesAt_;                      // "classId/aid" -> ms, or -1
	std::map<std::string, Submission> subs_;                       // "classId/aid/sid"
	std::map<std::string, std::string> subsEtag_;                  // "classId/aid"
	std::map<std::string, Live> lives_;                            // student view
	std::map<std::string, std::map<std::string, int>> myGuess_;    // classId -> the guesses sent for the live ver
	std::map<std::string, AnswerCounts> counts_;
	std::map<std::string, std::string> answersEtag_;
	std::map<std::string, Status> status_;
	std::set<std::string> liveChanged_, answersChanged_;
	std::string deviceId_;
	int64_t offset_ = 0, pollSeconds_ = 3, retryAfter_ = 0;
};

}  // namespace clclass
