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
// A class pass (3.17): three independent HKDF outputs of its 16 bytes; the server keeps SHA-256 of passId (pid) and of passToken.
struct PassKeys {
	std::string passId, pid, passToken, tokenHash;
	Bytes passKey;
	bool valid() const { return passKey.size() == 32; }
};
PassKeys passKeys(Crypto&, const Bytes& secret);
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
std::string passJson(const MoveRec&);   // 3.17: the move record's fields, kind "pass"
bool moveFrom(const json::Value&, MoveRec&);
std::string classroomJson(const std::string& classId, const std::string& teacherKey, const std::string& name, int64_t createdAt,
                          int64_t modifiedAt, const std::string& device, const std::string& deviceId);

// v2 (3.16.2): a shared circuit ("share") or a class example ("example").
struct ItemRec {
	std::string type, title, topic, note, cdl;
	int64_t createdAt = 0, modifiedAt = 0;
};
std::string itemJson(const ItemRec&);
bool itemFrom(const json::Value&, ItemRec&);
// v2 (3.16.7): the sync side record of a membership (a move record plus joinedAt).
std::string membershipJson(const MoveRec&, int64_t joinedAt);

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
	std::function<bool()> syncOn;                                         // (none: on)
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
// An item as the status lists it (3.16.2).
struct ItemMark { int64_t ver = 0; std::string h; bool hidden = false; int64_t releasedAt = 0, at = 0, releaseAt = 0; };

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
	std::map<std::string, ItemMark> items;   // v2 (3.16.2)
	int64_t warnAt = 0;               // v2 (3.16.4)
	int students = -1;                // the roster's size, as the live connection last said (-1: not heard)
};

struct Pending { std::string cdl; int attempt = 0; int64_t at = 0; };
struct Membership {
	std::string classId, studentId, token, proof, classKey, pub, fetchKey, name, className;
	int64_t joinedAt = 0, seq = 0, infoVer = 0, expiresAt = 0, liveSeen = 0, nameVer = 1;
	int64_t pulseLive = -1;                                       // the pulse's `live` last seen (-1: not known yet)
	std::string pulseEtag;                                        // the pulse's ETag, for If-None-Match (3.3)
	std::map<std::string, int64_t> seenAssignments;               // aid -> ver
	std::map<std::string, int64_t> seenSubmissions;               // aid -> the ver of the last hand-in
	std::map<std::string, int64_t> handedInAt;                    // aid -> ms
	std::map<std::string, std::string> handedInHash;              // aid -> cdlHash of what was handed in
	std::map<std::string, int> attempts;                           // aid -> hand-ins so far
	std::map<std::string, Pending> pending;
	int wrongToken = 0;                                           // 401s in a row (4.11)
	// v2 (3.16)
	std::map<std::string, ItemMark> items;                        // the released items the status lists
	std::map<std::string, int64_t> seenItems;                     // iid -> the highest ver opened
	std::map<std::string, std::string> news;                      // iid -> "new" | "updated", until opened
	int64_t warnAt = 0;
	std::string sideRid;                                          // the membership's sync side record (3.16.7)
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
	Result refreshTeacher(const std::string& classId, bool mayRecreate = true);   // the status (4.1)
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
	Result refreshAnswers(const std::string& classId, int64_t waitSeconds = 0);   // waitSeconds: a held poll (3.3)

	// Student.
	Result previewJoinCode(const std::string& text);   // value: "<class name>\n<1|0>"
	Result join(const std::string& text, const std::string& name);
	Result rename(const std::string& classId, const std::string& name);
	Result handIn(const std::string& classId, const std::string& aid, const std::string& cdl);
	Result sendAnswer(const std::string& classId, const std::map<std::string, int>& lights);
	Result makeMoveCode(const std::string& classId);
	Result previewMoveCode(const std::string& text);   // value: "<class name>\n<student name>"
	Result importMoveCode(const std::string& text);
	Result makeClassPass(const std::string& classId);
	Result previewClassPass(const std::string& text);   // value: "<class name>\n<student name>"
	Result useClassPass(const std::string& text);
	Result listPasses(const std::string& classId);
	Result cancelPass(const std::string& classId, const std::string& pid);
	Result leaveClass(const std::string& classId);
	void forgetMembership(const std::string& classId);
	Result pulse(const std::string& classId, int64_t waitSeconds = 0);   // one poll (4.8); sends pending hand-ins
	Result refreshMember(const std::string& classId);  // the status

	// The same two polls in halves, for a poll the server holds (x-cedarlogic-wait, 3.3) while the
	// engine thread goes on: the request (an empty url: nothing to ask), then its answer.
	HttpRequest pulseRequest(const std::string& classId, int64_t waitSeconds);
	Result pulseReply(const std::string& classId, const HttpResponse&);
	HttpRequest answersRequest(const std::string& classId, int64_t waitSeconds);
	Result answersReply(const std::string& classId, const HttpResponse&);
	int64_t holdSeconds() const { return holdSeconds_; }   // the server's limits.holdSeconds (0: it doesn't hold)

	// The live connection (3.14).
	std::string socketUrl(const std::string& classId) const;            // ws(s)://.../classes/{classId}/socket
	std::vector<std::pair<std::string, std::string>> socketHeaders() const;
	bool helloFor(const std::string& classId, std::string& text) const;  // false: the class isn't on this device
	Result socketMessage(const std::string& classId, const json::Value& msg);   // a message from the server
	// Send My Guess in parts (4.6): sealed, then sent over the socket or with PUT; the socket's
	// "ok" or "error" taken like the PUT's answer.
	Result sealAnswer(const std::string& classId, const std::map<std::string, int>& lights, json::Value& body);
	Result putAnswer(const std::string& classId, const json::Value& body, const std::map<std::string, int>& lights);
	Result answerReply(const std::string& classId, const json::Value& reply, const std::map<std::string, int>& lights);

	// Sync's side records (2.5).
	void sideChanged();

	// v2 (3.16): items and hand-in history.
	Result postItem(const std::string& classId, const Item& draft, bool hidden, int64_t releaseAt = kReleaseKeep);   // value: the iid
	Result setItemHidden(const std::string& classId, const std::string& iid, bool hidden, int64_t releaseAt = kReleaseKeep);
	Result deleteItem(const std::string& classId, const std::string& iid);
	std::vector<Item> items(const std::string& classId) const;
	void itemOpened(const std::string& classId, const std::string& iid);
	Result loadHistory(const std::string& classId, const std::string& aid, const std::string& sid);
	std::vector<Submission> history(const std::string& classId, const std::string& aid, const std::string& sid) const;
	struct News { std::string classId, className; std::vector<ItemNews> items; };
	bool newsFlag(News& out);                          // items news since the last call

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
	bool handinFlag(std::string& classId, std::string& aid);   // a hand-in the live connection told of

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
	HttpRequest request(const std::string& method, const std::string& url, const std::vector<std::pair<std::string, std::string>>& headers,
	                    const json::Value* body, const std::string& ifNoneMatch = std::string());
	Api answer(const HttpResponse&);
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
	// tombstone: the membership's side record goes too (left, removed, deleted); else this device only.
	void forgetMembershipLocal(const std::string& classId, const std::string& notice, bool tombstone = true);
	Result gone(const std::string& classId, const Api& a, bool teaching);
	Result openPass(const std::string& text, MoveRec& r);   // 3.17: GET /pass/{passId}, opened
	Result fetchAssignment(const std::string& classId, const std::string& fetchKey, const Bytes& classKey,
	                       const std::string& aid, int64_t ver, const std::string& h, bool teacher);
	Result fetchLive(Membership& m, int64_t ver);
	Result applyLiveRecord(Membership& m, int64_t ver, const std::string& session, const std::string& envB64);
	Result applyPulse(const std::string& classId, const json::Value& p, const json::Value* record);
	Result removedFromClass(const std::string& classId, int status, const std::string& error);
	void readLimits(const json::Value& status);
	void applyAnswers(Teaching& t, const json::Value* list, bool full);
	void recount(Teaching& t);
	Result sendPending(Membership& m);
	Result handInNow(Membership& m, const std::string& aid, const std::string& cdl, int attempt, bool queueOffline);
	bool memberStatus(Membership& m, const json::Value& s);
	std::string cachePath(const std::string& classId, const std::string& name) const;
	void saveCache(const std::string& classId, const std::string& name, const std::string& text);
	std::string loadCache(const std::string& classId, const std::string& name) const;
	Result fetchItem(const std::string& classId, const std::string& fetchKey, const Bytes& classKey, const std::string& iid, int64_t ver);
	void forgetItem(const std::string& classId, const std::string& iid);
	void putMemberSide(Membership& m);
	void putTeacherSide(Teaching& t);
	bool syncOn() const { return !hooks_.syncOn || hooks_.syncOn(); }

	Config cfg_;
	Crypto& cr_;
	Curve& curve_;
	ClientHooks hooks_;
	std::map<std::string, Teaching> teaching_;
	std::map<std::string, Membership> members_;
	struct PassSeen { std::string text; MoveRec rec; int64_t at = 0; };
	PassSeen passSeen_;                                            // 3.17: the pass a preview just opened
	std::vector<std::string> removedSide_;                         // sync record ids not to add back (2.5)
	std::map<std::string, JoinKeys> joinCache_;                    // code -> keys
	std::map<std::string, Assignment> asgCache_;                   // "classId/aid" -> the opened assignment
	std::map<std::string, Student> roster_;                        // "classId/sid"
	std::map<std::string, int64_t> nameVer_;                       // "classId/sid" -> the name record's ver opened
	std::map<std::string, int64_t> closesAt_;                      // "classId/aid" -> ms, or -1
	std::map<std::string, Submission> subs_;                       // "classId/aid/sid"
	std::map<std::string, std::string> subsEtag_;                  // "classId/aid"
	std::map<std::string, Live> lives_;                            // student view
	std::map<std::string, Item> itemCache_;                        // "classId/iid" -> the opened item (v2)
	std::map<std::string, std::vector<Submission>> history_;       // "classId/aid/sid" -> earlier hand-ins (v2)
	std::vector<News> news_;
public:
	const std::map<std::string, std::vector<Submission>>& allHistory() const { return history_; }
private:
	std::map<std::string, std::map<std::string, int>> myGuess_;    // classId -> the guesses sent for the live ver
	std::map<std::string, AnswerCounts> counts_;
	struct Guess { int64_t ver = 0; std::map<std::string, int> lights; };
	std::map<std::string, std::map<std::string, Guess>> answers_;   // classId -> sid -> the answer counted (4.4)
	std::set<std::pair<std::string, std::string>> handins_;         // (classId, aid)
	std::map<std::string, std::string> answersEtag_;
	std::map<std::string, Status> status_;
	std::set<std::string> liveChanged_, answersChanged_;
	std::string deviceId_;
	int64_t offset_ = 0, pollSeconds_ = 10, retryAfter_ = 0, holdSeconds_ = 25;
};

// ---- The live connection (3.14): one class's WebSocket, as the web core's LiveSocket --------

// The platform's socket, as the engine hands it to a LiveLink (Host's socket hooks, or a test's).
struct SocketHooks {
	std::function<int()> newId;
	std::function<bool(int id, const std::string& url, const std::vector<std::pair<std::string, std::string>>& headers)> open;
	std::function<void(int id, const std::string& text)> send;
	std::function<void(int id, int code)> close;
};

// Says hello with the device's token, hands every message to the Client (socketMessage), answers
// requests by id, pings every 45 s and closes a socket whose last ping got no pong, and reconnects
// (1, 2, 5, 10, 30, 60 s, +-20 %). After three tries that never got a hello (a school network that
// blocks WebSockets) it is in Fallback: the engine holds polls, and the socket is tried again every
// five minutes. A bye (or a closing code) for a removed student, a deleted class or a wrong key
// ends it. Everything runs on the engine thread; time comes in as `now` (ms).
class LiveLink {
public:
	enum State { Idle, Connecting, Open, Fallback, Closed };
	LiveLink(Client&, const std::string& classId, SocketHooks&, uint32_t seed);
	void start(int64_t now);
	void stop();                                         // the page closed: closed, no reconnecting
	void abandon();                                      // the engine ends: the socket closed, nothing reported
	void opened(int id, int64_t now);
	void text(int id, const std::string& text, int64_t now);
	void closed(int id, int code, int64_t now);
	void tick(int64_t now);                              // reconnect, hello and ping timers, requests waiting
	int64_t nextTimer() const;                           // INT64_MAX: nothing to wait for
	bool owns(int id) const { return id != 0 && id == id_; }
	State state() const { return state_; }
	bool isOpen() const { return state_ == Open && id_ != 0; }
	bool unsupported() const { return unsupported_; }   // the platform has no WebSockets
	bool stateChanged();                                 // since the last call (the engine reschedules its polls)
	// A message with an id ("answer"): `reply` gets the server's "ok" or "error" for it, or nullptr
	// when none comes within 8 s or the socket goes first. False (and no reply) if not open.
	bool request(json::Value msg, std::function<void(const json::Value*)> reply, int64_t now);
	static const char* stateName(State);

private:
	void connect(int64_t now);
	void closeOwn(int64_t now);                          // the client gives up on the socket (no hello, no pong)
	void finish(int code, int64_t now);                  // the socket is gone: reconnect, fall back or end
	void retry(int64_t now, int64_t atLeastMs);
	void setState(State);
	void failPending();
	Client& client_;
	std::string classId_;
	SocketHooks& hooks_;
	uint32_t seed_;
	State state_ = Idle;
	bool changed_ = false, stopped_ = true, fallback_ = false, greeted_ = false, ended_ = false, alive_ = true;
	bool unsupported_ = false;
	int id_ = 0, failures_ = 0, byeStatus_ = 0;
	int64_t retryAt_ = 0, helloBy_ = 0, pingAt_ = 0, byeRetryAfterMs_ = 0, nextRequest_ = 1;
	std::string hello_;
	struct Waiting { std::function<void(const json::Value*)> reply; int64_t until = 0; };
	std::map<int64_t, Waiting> pending_;
};

}  // namespace clclass
