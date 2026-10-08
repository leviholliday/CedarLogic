// The CedarLogic classroom core (CLASSROOM.md). Plain C++17; crypto, P-256,
// HTTP, files and the UI thread come in through the hooks.
//
// This is CLASSROOM.md 6.2, with a few small additions marked "(added)":
// Host::checkCircuit and Host::lightsOf (Check My Circuit and the lights of a
// pushed circuit come from the app, which has the simulator and the formula
// reader), Config::deviceName, and read-only fields the UI needs (a record's
// problem line, a class's warning line, the student's own score); and, for
// revision 3's server (CLASSROOM.md 3.14, 13), the live connection: three
// socket hooks on Host, Engine::socketOpened/Text/Closed for what comes back,
// and Host::submissionsChanged.
#pragma once
#include "Sync.h"          // clsync::Crypto, HttpRequest, HttpResponse, Bytes
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace clclass {

using clsync::Bytes;
using Done = std::function<void(bool ok, const std::string& message)>;   // on the UI thread

// ---- What the platform provides ----------------------------------------------------

// P-256 (thread-safe), beside clsync::Crypto's random / sha256 / hmac / AES-GCM / deflate.
struct Curve {
	virtual ~Curve() = default;
	virtual bool p256Generate(uint8_t d[32], uint8_t pub[65]) = 0;                       // a fresh key pair; false = stop
	virtual bool p256Public(const uint8_t d[32], uint8_t pub[65]) = 0;                    // false if d is 0 or >= n
	virtual bool p256Ecdh(const uint8_t d[32], const uint8_t peer[65], uint8_t x[32]) = 0;   // false: peer isn't 04|X|Y on the curve
	// PBKDF2-HMAC-SHA256 for the join code (1.3). The default runs the rounds on clsync::Crypto's hmacSha256
	// hook (about a second); a platform with a native PBKDF2 (CommonCrypto, OpenSSL, CNG) overrides it.
	virtual bool pbkdf2Sha256(clsync::Crypto&, const uint8_t* pw, size_t pwLen, const uint8_t* salt, size_t saltLen,
	                          uint32_t rounds, uint8_t out[32]);
};

struct Assignment {
	std::string id, title, instructions, cdl;
	int64_t dueAt = -1;                 // -1: none
	bool closeAfterDue = false;
	int64_t ver = 0;
	std::string keyText, keyNames;      // the key as typed (students can check), or the teacher's own copy
	bool keySealed = false;             // the key is sealed to the teacher (students see only that there is one)
	// a student's view:
	int64_t handedInAt = 0; int attempts = 0; bool changedSince = false; bool pending = false; bool closed = false;
	bool unreadable = false;            // damaged, or needs a newer CedarLogic (message says which)
	std::string problem;
};
struct Submission {
	std::string studentId, name, cdl;
	int64_t handedInAt = 0; int attempts = 0;
	int checkVerdict = -1;              // -1 not checked, 0 matches, 1 wrong rows, 2 couldn't check
	std::string checkSummary;
	bool unreadable = false;
	// (added) "Couldn't be read", "Couldn't be read (too big)", "Needs a newer CedarLogic" or
	// "Couldn't be verified"; left: the student left or was removed, "(left the class)".
	std::string problem;
	bool left = false;
	int64_t ver = 0;
};
// v2 (3.16.2): a shared circuit (type "share") or a class example ("example").
struct Item {
	std::string id, type, title, topic, note, cdl;
	int64_t ver = 0, createdAt = 0, releasedAt = 0;   // releasedAt: when students first saw it (0: hidden)
	bool hidden = false;                              // teacher: kept from students until released
	std::string news;                                 // student: "new", "updated" or "" (opened, or there when joining)
	bool unreadable = false;
	std::string problem;
};
struct ItemNews { std::string id, what, type, title; };   // what: "new" | "updated"
struct Student { std::string studentId, name; int64_t joinedAt = 0, seenAt = 0; bool unreadable = false; };
struct Live {
	bool on = false, ended = false, reveal = false, hasPredict = false;
	std::string session, cdl, prompt;
	std::vector<std::string> lights;
	int64_t ver = 0; int step = 0;
	int myRight = -1, myTotal = 0;      // (added) a student's own guesses once revealed (-1: no score)
	bool takeOver = false;              // (added) teacher: another device is live; takeOverLive() takes over
};
struct AnswerCounts { int answered = 0, students = 0, right = 0, wrong = 0; std::map<std::string, std::pair<int, int>> perLight; };   // light -> (ones, zeros)
struct ClassInfo {
	std::string classId, name;
	bool teaching = false;              // else a membership
	std::string teacherKey, joinCode;   // teaching
	bool joinOpen = true;
	std::string studentName;            // membership
	int64_t expiresAt = 0;
	int64_t warnAt = 0;                 // v2 (3.16.4): the status's warnAt (0: an older server)
	int news = 0;                       // student: items not opened yet (3.16.2)
	bool live = false;
	// (added) a line for the class page: the expiry warning (4.1), or "The join record on the
	// website isn't the one your devices wrote. Change the join code."
	std::string warning;
};
struct Status { enum Kind { Idle, Working, Offline, Error, Gone } kind = Idle; std::string text; };

// (added) What a platform's WebSocket hands back (3.14), from any thread, for the socket `id` the
// core opened: until the core closes that id itself, or after the platform reported it closed.
struct SocketEvents {
	virtual ~SocketEvents() = default;
	virtual void socketOpened(int id) = 0;                             // the upgrade succeeded
	virtual void socketText(int id, const std::string& text) = 0;      // a text frame
	virtual void socketClosed(int id, int code) = 0;                   // gone, for whatever reason (1006: the network)
};

struct Host {
	virtual ~Host() = default;
	// Engine thread. Blocking, as Sync's; HTTPS only (http only for localhost overrides).
	virtual clsync::HttpResponse http(const clsync::HttpRequest&) = 0;
	virtual void onMain(const std::function<void()>&) = 0;
	// The Classroom folder (2.4): "teaching.json", "memberships.json", "cache/<classId>/<name>". "" = none. Atomic, 0600.
	virtual std::string loadFile(const std::string& name) = 0;
	virtual bool saveFile(const std::string& name, const std::string& text) = 0;
	virtual void removeTree(const std::string& name) = 0;
	virtual bool tryLock(const std::string& lockPath) = 0;
	virtual void unlock() = 0;
	// UI thread:
	virtual void classesChanged() = 0;                                          // lists, assignments, roster, hand-ins
	virtual void liveChanged(const std::string& classId, const Live&) = 0;      // a new version for a student following
	virtual void answersChanged(const std::string& classId, const AnswerCounts&) = 0;
	virtual void statusChanged(const std::string& classId, const Status&) = 0;
	virtual void notice(const std::string& text) = 0;
	// Sync's side records (2.5); a host without Sync returns nothing and ignores writes.
	virtual std::vector<std::pair<std::string, std::string>> syncSideRecords() = 0;   // (rid, payload JSON) of kind "classroom"
	virtual void syncPutSide(const std::string& ridOrEmpty, const std::string& payloadJson) = 0;
	virtual void syncDeleteSide(const std::string& rid) = 0;
	// (added) Engine thread. Check My Circuit on a hand-in (4.3): the verdict (0 matches, 1 wrong
	// rows, 2 couldn't check) and the Check sheet's one-line summary. false: not checked.
	virtual bool checkCircuit(const std::string& cdl, const std::string& keyText, const std::string& keyNames,
	                          int& verdict, std::string& summary) {
		(void)cdl; (void)keyText; (void)keyNames; (void)verdict; (void)summary;
		return false;
	}
	// (added) Engine thread. The named lights of a circuit once it settles (0 or 1), for scoring
	// predict answers after a reveal (4.4, 4.6). false: can't tell (no right/wrong is shown).
	virtual bool lightsOf(const std::string& cdl, const std::vector<std::string>& lights, std::map<std::string, int>& values) {
		(void)cdl; (void)lights; (void)values;
		return false;
	}
	// (added) The live connection (3.14): a WebSocket (wss://, or ws:// for a localhost override) with
	// these request headers, text frames only. Engine thread; no blocking. socketOpen returns false
	// on a platform without WebSockets (the engine then holds polls, 3.10); otherwise everything that
	// happens to the socket comes back through `events` until socketClose(id) returns or the platform
	// reports socketClosed(id). "ping" is an ordinary text frame (the server answers "pong").
	virtual bool socketOpen(int id, const std::string& url, const std::vector<std::pair<std::string, std::string>>& headers,
	                        SocketEvents& events) {
		(void)id; (void)url; (void)headers; (void)events;
		return false;
	}
	virtual void socketSend(int id, const std::string& text) { (void)id; (void)text; }
	virtual void socketClose(int id, int code) { (void)id; (void)code; }
	// (added) UI thread. A student handed in to that assignment (the teacher's live connection says
	// so): a submissions view that is open refreshes (refreshSubmissions).
	virtual void submissionsChanged(const std::string& classId, const std::string& aid) { (void)classId; (void)aid; }
	// v2 (3.16.2), UI thread: a student's class has new or updated items ("Your teacher shared …").
	virtual void itemsChanged(const std::string& classId, const std::vector<ItemNews>& news, const std::string& className) {
		(void)classId; (void)news; (void)className;
	}
	// v2 (3.16.7): whether Sync is on (side records are read and written only then).
	virtual bool syncOn() { return true; }
};

// The classroom service (3.2, 3.13): the Worker's own origin. A placeholder that can never resolve
// until the owner deploys and sets the real one in ClassroomProtocol.cpp (as the web core's
// SERVICE); CL_CLASSROOM_SERVICE, or CL_CLASSROOM_URL and CL_LIVE_URL, point the apps at another
// (cl_classroom_create).
extern const char* const kService;          // https://cedarlogic-classroom.leviholliday7.workers.dev

struct Config {
	std::string dir;                    // the Classroom folder
	std::string serverBase = std::string(kService) + "/api/classroom/v1";
	std::string liveBase = std::string(kService) + "/api/live/v1";
	std::string appKey, client;         // x-cedarlogic-key, x-cedarlogic-client
	std::string deviceName;             // (added) the sync side record's `device` (2.5)
};

// ---- Codes and text (any thread) --------------------------------------------------------

enum class CodeKind { Teacher, Join, Move };
std::string newCode(clsync::Crypto&, CodeKind);                              // "" if the RNG failed
bool parseCode(clsync::Crypto&, CodeKind, const std::string& text, std::string& code, std::string& why);   // why: length | symbol | checksum | kind
std::string whyText(CodeKind, const std::string& why, const std::string& text);
std::string groupCode(const std::string& code);
std::string webLink(CodeKind, const std::string& code);
std::string appLink(CodeKind, const std::string& code);
// (QR modules: clsync::qr of the web link)

// ---- The engine (create, call and destroy on the UI thread) -------------------------

class Engine {
public:
	Engine(Config, clsync::Crypto&, Curve&, Host&);
	~Engine();
	void start();                                        // loads the files, starts polling for open pages
	void stop();

	std::vector<ClassInfo> classes() const;
	Status status(const std::string& classId) const;

	// Teacher (4.1-4.4). `done` on the UI thread.
	void createClass(const std::string& name, std::function<void(bool, std::string message, std::string classId)> done);
	void previewTeacherKey(const std::string& text, std::function<void(bool, std::string message, std::string className)> done);
	void addTeacherKey(const std::string& text, Done done);
	void renameClass(const std::string& classId, const std::string& name, Done);
	void setJoinOpen(const std::string& classId, bool open, Done);
	void newJoinCode(const std::string& classId, Done);
	void forgetClass(const std::string& classId);                                   // Remove from This Device: this device only
	void deleteClass(const std::string& classId, Done);                             // the website too
	std::vector<Assignment> assignments(const std::string& classId) const;
	void postAssignment(const std::string& classId, const Assignment& draft, bool studentsCanCheck, Done);   // draft.id "" = new
	void deleteAssignment(const std::string& classId, const std::string& aid, Done);
	std::vector<Student> students(const std::string& classId) const;
	void refreshStudents(const std::string& classId, Done);
	void removeStudents(const std::string& classId, const std::vector<std::string>& sids, bool deleteHandIns, Done);   // one or many
	std::vector<Submission> submissions(const std::string& classId, const std::string& aid) const;
	void refreshSubmissions(const std::string& classId, const std::string& aid, Done);  // index, fetch, open, check
	void goLive(const std::string& classId, const std::string& cdl, Done);
	void push(const std::string& classId, const std::string& cdl, const std::string* prompt,
	          const std::vector<std::string>* lights, bool reveal, Done);
	void endLive(const std::string& classId, Done);
	void takeOverLive(const std::string& classId, Done);                             // after a 412
	Live live(const std::string& classId) const;
	AnswerCounts answers(const std::string& classId) const;
	// v2 (3.16.2, 3.16.3): shared circuits and class examples; a hand-in's earlier attempts.
	std::vector<Item> items(const std::string& classId) const;      // teacher: every one; student: the released ones
	void postItem(const std::string& classId, const Item& draft, bool hidden, std::function<void(bool, std::string message, std::string iid)> done);
	void setItemHidden(const std::string& classId, const std::string& iid, bool hidden, Done);
	void deleteItem(const std::string& classId, const std::string& iid, Done);
	void itemOpened(const std::string& classId, const std::string& iid);   // student: not news any more
	void loadHistory(const std::string& classId, const std::string& aid, const std::string& sid, Done);
	std::vector<Submission> history(const std::string& classId, const std::string& aid, const std::string& sid) const;   // oldest first

	// Student (4.5-4.7).
	void previewJoinCode(const std::string& text, std::function<void(bool, std::string message, std::string className, bool open)> done);
	void join(const std::string& text, const std::string& name, std::function<void(bool, std::string message, std::string classId)> done);
	void rename(const std::string& classId, const std::string& name, Done);
	void handIn(const std::string& classId, const std::string& aid, const std::string& cdl, Done);   // queues when offline
	void follow(const std::string& classId, bool following);                        // the live view is open
	void sendAnswer(const std::string& classId, const std::map<std::string, int>& lights, Done);
	void makeMoveCode(const std::string& classId, std::function<void(bool, std::string message, std::string code)> done);
	void previewMoveCode(const std::string& text, std::function<void(bool, std::string message, std::string className, std::string studentName)> done);
	void importMoveCode(const std::string& text, Done);
	void leaveClass(const std::string& classId, Done);
	void forgetMembership(const std::string& classId);                              // Remove from This Device: nothing on the website

	// Triggers (4.8).
	void pageOpen(const std::string& classId, bool open);                            // the class page is showing
	void appActivated();
	void appDeactivated();
	void userActive();
	void syncSideChanged();                                                          // Sync applied a classroom record

	// (added) The live connection (3.14): what the platform's socket says, from any thread
	// (Host::socketOpen's `events` lead here too).
	void socketOpened(int id);
	void socketText(int id, const std::string& text);
	void socketClosed(int id, int code);
	// (added) A class's live connection: "" (none: the page isn't open), "connecting", "open",
	// "fallback" (three tries got no hello: held polls, the socket again every 5 minutes) or
	// "closed" (ended by the server: removed, deleted, a wrong key).
	std::string liveConnection(const std::string& classId) const;

private:
	struct Impl;
	std::unique_ptr<Impl> d;
};

// The vectors of 7.1 and the scenarios of 7.2 on an in-process FakeServer (and, with serverBase, the mock server).
bool selfTest(clsync::Crypto&, Curve&, const std::string& tempDir, std::string& report, Host* httpOnly = nullptr,
              const std::string& serverBase = "", const std::string& liveBase = "");

}  // namespace clclass
