// The sync engine's insides, shared by Sync*.cpp and the self-test. Not for
// the apps: they use Sync.h (C++) or CedarSync.h (C). Plain C++17, no
// platform headers.
#pragma once

#include "Sync.h"
#include "SyncJson.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace clsync {

// ---- Constants (SYNC.md 1-3) ----------------------------------------------------------

constexpr int64_t kMaxSafeInt = 9007199254740991LL;
constexpr size_t kMaxEnvelope = 524288;          // a circuit's envelope, bytes
constexpr size_t kMaxSmallEnvelope = 4096;       // a tombstone's or device record's
constexpr size_t kMaxPlaintext = 20000000;       // what a record may inflate to
constexpr int64_t kMinTime = 1577836800000LL;    // 2020-01-01: earlier stamps read as this
constexpr int64_t kSecond = 1000, kMinute = 60 * kSecond, kHour = 60 * kMinute, kDay = 24 * kHour;
constexpr size_t kBatchItems = 50;               // write items per request
constexpr size_t kBatchChars = 3000000;          // envelope characters per write request
constexpr size_t kFetchIds = 50;
constexpr size_t kUiStep = 20;                   // circuits or records handled per step on the UI thread (§4.12)
extern const char* const kWebBase;               // https://cedarlogic.netlify.app/sync/
extern const char* const kAppBase;               // cedarlogic://sync

// ---- Bytes and text (SyncProtocol.cpp) ----------------------------------------------

std::string hex(const uint8_t* p, size_t n);
inline std::string hex(const Bytes& b) { return hex(b.data(), b.size()); }
inline std::string hexOf(const std::string& s) { return hex((const uint8_t*)s.data(), s.size()); }
bool unhex(const std::string& s, Bytes& out);
std::string b64u(const Bytes& b);
bool unb64u(const std::string& s, Bytes& out);
std::string sha256Hex(Crypto&, const std::string& s);
std::string sha256Hex(Crypto&, const Bytes& b);
bool validUtf8(const std::string& s);
void putUtf8(std::string& out, uint32_t cp);
// Unicode scalar values in valid UTF-8 text.
size_t scalarCount(const std::string& s);
bool isUuid(const std::string& s);             // a lowercase UUID v4
bool isHex(const std::string& s, size_t len);  // `len` lowercase hex digits
std::string newUuid(Crypto&);                  // "" if the RNG failed
std::string randomHex(Crypto&, size_t bytes);  // "" if the RNG failed

// SYNC.md 2.3.
std::string fileText(const std::string& bytes);
std::string dropBom(const std::string& s);
std::string trimAscii(const std::string& s);
std::string normalizeCdl(const std::string& s);
std::string normalizeName(const std::string& s);
// A name as written to and read from a payload: trimmed, at most 200 scalar values, "Untitled" if empty.
std::string payloadName(const std::string& s);
std::string nameHash(Crypto&, const std::string& name);
std::string cdlHash(Crypto&, const std::string& cdl);
std::string contentHash(Crypto&, const std::string& name, const std::string& cdl);
std::string envelopeHash(Crypto&, const Bytes& env);   // the server's `h`: 32 hex

// ---- Codes and keys (SyncProtocol.cpp) -------------------------------------------------

std::string encodeCode(Crypto&, const uint8_t secret[16]);
// The canonical 28 symbols, or false with why = "length" | "symbol" | "checksum".
bool normalizeCode(const std::string& text, std::string& code, std::string& why);
bool decodeCode(Crypto&, const std::string& text, uint8_t secret[16], std::string& why);
Bytes hkdf(Crypto&, const uint8_t* ikm, size_t ikmLen, const std::string& info, size_t len);   // salt cedarlogic-sync-v1
Bytes hkdfSalted(Crypto&, const std::string& salt, const uint8_t* ikm, size_t ikmLen, const std::string& info, size_t len);

struct Keys {
	std::string spaceId, authToken, authHash, deleteToken, deleteHash;
	Bytes recordKey;
	bool valid() const { return recordKey.size() == 32; }
};
Keys deriveKeys(Crypto&, const uint8_t secret[16]);
// From a canonical code; invalid Keys if it isn't one.
Keys keysForCode(Crypto&, const std::string& code);

// ---- Envelopes and payloads (SyncProtocol.cpp) ----------------------------------------

std::string aadText(const std::string& id, int64_t ver, int flags);
// The real seal: a fresh nonce. False if the RNG or the cipher failed (`why` says), or
// the envelope would pass `maxEnvelope` (why = "too big").
bool sealRecord(Crypto&, const Bytes& key, const std::string& id, int64_t ver, const std::string& payload,
                bool compress, size_t maxEnvelope, Bytes& env, std::string& why);
// Test vectors only: the nonce is given (SYNC.md 1.4).
bool sealForTest(Crypto&, const Bytes& key, const std::string& id, int64_t ver, const std::string& payload,
                 int flags, const Bytes& nonce, Bytes& env);
// False: damaged (malformed, wrong tag, inflate failed).
bool openRecord(Crypto&, const Bytes& key, const std::string& id, int64_t ver, const Bytes& env, std::string& payload);

struct Payload {
	std::string kind;             // circuit | deleted | device | a registered side kind (SYNC.md 2.5.1)
	std::string raw;              // a side record: the payload as it came
	std::string name, cdl;        // circuit (name trimmed and cut as payloadName)
	std::string device, deviceId, client;
	int64_t createdAt = -1;       // -1: absent
	int64_t modifiedAt = 0, deletedAt = 0, lastSyncAt = 0;
	bool hasBase = false;
	std::string baseName, baseCdl;
};
// "" (readable), "newer" or "invalid" (SYNC.md 2.2, 4.10). A kind in sideKinds (2.5.1) is read as
// it is (p.raw), never "newer"; without them any other kind is "newer", as before.
std::string readPayload(const std::string& bytes, Payload& p, const std::vector<std::string>* sideKinds = nullptr);
std::string circuitJson(const std::string& name, const std::string& cdl, int64_t modifiedAt, const std::string& device,
                        const std::string& deviceId, int64_t createdAt, const std::string* baseName,
                        const std::string* baseCdl);
std::string tombstoneJson(int64_t deletedAt, const std::string& device, const std::string& deviceId,
                          const std::string* baseName, const std::string* baseCdl);
std::string deviceJson(const std::string& device, const std::string& deviceId, const std::string& client,
                       int64_t lastSyncAt);
// A payload's time as used to pick the newer (SYNC.md 4.6).
int64_t effectiveTime(int64_t stamp, int64_t serverAt);

// HTTP dates (the Date header), no platform calls.
bool parseHttpDate(const std::string& s, int64_t& ms);
std::string formatHttpDate(int64_t ms);
// Civil time <-> days since 1970 (proleptic Gregorian).
int64_t daysFromCivil(int64_t y, unsigned m, unsigned d);
void civilFromDays(int64_t z, int64_t& y, unsigned& m, unsigned& d);

// ---- Pairing: adding a device by scanning (SyncPair.cpp, SYNC.md 11) ------------------

constexpr size_t kMaxPairEnvelope = 2048;        // a hello's or answer's envelope, bytes
extern const char* const kPairSalt;              // cedarlogic-pair-v1

struct PairKeys {
	std::string pairId;   // 32 lowercase hex
	Bytes key;            // AES-256-GCM
	bool valid() const { return key.size() == 32; }
};
PairKeys pairKeys(Crypto&, const uint8_t secret[16]);
// The QR code's text for a pairing code (the 28 symbols): https://cedarlogic.netlify.app/sync/#p=...
std::string pairLink(const std::string& pairingCode);
// Text holding "#p=" and a pairing code (the link, or a bare "#p=...") -> P. False for anything
// else, and always for text with "#k=" (a sync code, never a pairing link).
bool parsePairLink(Crypto&, const std::string& text, uint8_t secret[16]);
std::string readTokenOf(const Bytes& r);                     // base64url of R (43 chars)
std::string readHashOf(Crypto&, const std::string& readToken);   // lowercase hex SHA-256 of those 43 characters
// The plaintexts, keys in the order of 11.4, strings escaped as JSON.stringify does.
std::string pairHelloJson(const std::string& device);
std::string pairAnswerJson(const std::string& code, const std::string& device);
std::string pairAad(const std::string& kind, const std::string& pairId);
// kind "hello" or "answer"; env is base64url. False if the RNG or cipher failed or it's over 2048 bytes.
bool sealPair(Crypto&, const PairKeys&, const std::string& kind, const std::string& json, std::string& env);
// Test vectors only: the nonce is given.
bool sealPairForTest(Crypto&, const PairKeys&, const std::string& kind, const std::string& json, const Bytes& nonce,
                     std::string& env);
struct PairMessage {
	std::string device;   // as sent (pairDeviceText makes it showable)
	std::string code;     // an answer's sync code, canonical
};
// False: damaged (11.4).
bool openPair(Crypto&, const PairKeys&, const std::string& kind, const std::string& env, PairMessage& out);
// A device name from a message as shown: control characters dropped, at most 64 characters,
// "another device" if empty.
std::string pairDeviceText(const std::string& device);

// D's requests (11.5, 11.6). The usual headers (x-cedarlogic-client, x-cedarlogic-key) and,
// for the answer and DELETE, the read token as the bearer.
struct PairServer {
	std::string serverBase, appKey, client;
	std::function<HttpResponse(const HttpRequest&)> http;
};
struct PairSlot {
	PairKeys keys;
	std::string readToken, link;
};
// A fresh P and R, the hello sealed, PUT. 201 -> the slot; else the status (0: no answer) and,
// for a 409, make another. -1: the RNG or the cipher failed (nothing sent).
int pairPut(const PairServer&, Crypto&, const std::string& deviceName, PairSlot& slot);
// GET /pair/{id}/answer: the status; answerEnv the answer ("" while there's none).
int pairPoll(const PairServer&, const PairSlot&, std::string& answerEnv);
void pairDelete(const PairServer&, const std::string& pairId, const std::string& readToken);   // best effort
// How often D polls and how long a slot lives (ms): 3 s and 10 minutes; tests shorten them.
int64_t pairPollMs();
int64_t pairLifeMs();
void setPairTimingForTest(int64_t pollMs, int64_t lifeMs);   // 0, 0: back to the real ones

// ---- The structure digest (SyncStructure.cpp) ----------------------------------------

// lib, gui, name -> the default value, or "\x01" for none.
using GateDefaults = std::function<std::string(const std::string&, bool, const std::string&)>;
std::string structureText(const std::string& cdl, const GateDefaults& defaults);

// ---- Clocks -------------------------------------------------------------------------

struct Clock {
	virtual ~Clock() = default;
	virtual int64_t now() = 0;   // ms since 1970, this device's clock
};
struct SystemClock : Clock {
	int64_t now() override;
};

// ---- The local library as the algorithm sees it -------------------------------------

struct LocalHashes {
	std::string n, c, st, ch;   // nameHash, cdlHash, structureHash, contentHash
};

// What the algorithm needs of the circuits on this device: the apps' library
// folder (SyncLibrary.cpp) or, in the self-test, an in-memory "web" library.
struct Backend {
	virtual ~Backend() = default;
	// Every circuit's id; false if the library can't be read at all.
	virtual bool list(std::vector<std::string>& ids) = 0;
	virtual bool exists(const std::string& id) = 0;
	// The name and cdl as sync text (§2.3), the modification time (this device's clock, ms)
	// and createdAt (-1 if unknown).
	virtual bool read(const std::string& id, std::string& name, std::string& cdl, int64_t& mtime, int64_t& createdAt) = 0;
	virtual bool hashes(const std::string& id, LocalHashes& h) = 0;
	virtual int64_t modified(const std::string& id) = 0;
	// A new circuit; "" if it couldn't be made.
	virtual std::string create(const std::string& name, const std::string& cdl, int64_t mtime, const std::string& note) = 0;
	// The circuit becomes (name, cdl), written verbatim; its time `mtime`.
	virtual bool write(const std::string& id, const std::string& name, const std::string& cdl, int64_t mtime) = 0;
	// Apps: a version made by sync (arrival stamp), unless the newest version already has this structure.
	virtual bool keepVersion(const std::string& id, const std::string& cdl, const std::string& note) = 0;
	virtual bool keepsVersions() const = 0;
	// Into this device's trash (recoverable).
	virtual bool trash(const std::string& id) = 0;
	// The library's identity and generation (§2.5); the in-memory one keeps them in memory.
	virtual std::string libraryId() = 0;
	virtual int64_t libraryGen() = 0;
	virtual bool setLibraryGen(int64_t gen) = 0;
	// The hash cache, saved with the state.
	virtual json::Value cacheJson() { return json::Value::object(); }
	virtual void loadCache(const json::Value&) {}
	virtual void forget(const std::string& id) { (void)id; }
};

// The apps' library folder (SyncLibrary.cpp).
struct FileLibraryOptions {
	std::string root;
	Clock* clock = nullptr;
	Crypto* crypto = nullptr;
	std::function<std::string(const std::string& cdl)> structureHash;
	std::function<std::string()> deviceName;
};
std::unique_ptr<Backend> makeFileLibrary(const FileLibraryOptions&);
// "4 Oct 10:31" (local time), for version notes.
std::string versionNoteTime(int64_t ms);

// Files (UTF-8 paths), no platform calls.
namespace files {
bool read(const std::string& path, std::string& out);
bool writeAtomic(const std::string& path, const std::string& data);
bool exists(const std::string& path);
bool isDir(const std::string& path);
bool makeDirs(const std::string& path);
bool makePrivateDir(const std::string& path);   // owner only (0700 where that means something)
bool remove(const std::string& path);
bool removeAll(const std::string& path);
bool rename(const std::string& from, const std::string& to);
std::vector<std::string> listDir(const std::string& path);   // names
// Modification times: the file system's own ticks (for the hash cache), and ms since 1970.
bool mtimeRaw(const std::string& path, int64_t& ticks, int64_t& size);
int64_t mtimeMs(const std::string& path);   // 0 if missing
bool setMtimeMs(const std::string& path, int64_t ms);
std::string join(const std::string& a, const std::string& b);
}  // namespace files

// ---- Errors the algorithm throws --------------------------------------------------------

struct NetError : std::runtime_error { using std::runtime_error::runtime_error; };
struct SpaceGone : std::runtime_error { using std::runtime_error::runtime_error; };   // what(): "space_deleted" | "space_expired"
struct HttpError : std::runtime_error {
	int status;
	std::string code;
	HttpError(int s, std::string c, const std::string& message) : std::runtime_error(message), status(s), code(std::move(c)) {}
};
struct BusyError : std::runtime_error {
	int status;
	int64_t retryAfterMs;
	std::string code;
	BusyError(int s, int64_t r, std::string c, const std::string& message)
		: std::runtime_error(message), status(s), retryAfterMs(r), code(std::move(c)) {}
};
struct Stopped : std::runtime_error { Stopped() : std::runtime_error("stopped") {} };
struct LibraryError : std::runtime_error { using std::runtime_error::runtime_error; };

// Engines whose thread was still inside a host call when they were destroyed, and so were
// left to finish (leaked). The C interface keeps its hook objects alive when this went up.
int detachedEngines();

// ---- The sync state (§2.5, §4.1) --------------------------------------------------------

struct Sent {
	bool has = false;
	int64_t ver = 0;
	bool deleted = false;
	std::string n, c, st, ch;
};
struct RecState {
	std::string local;
	int64_t ver = 0;
	std::string n, c, st;
	Sent sent;
};
struct SeenRec {
	int64_t ver = 0;
	std::string h, local;
};
struct DeviceRecState {
	bool has = false;
	std::string id;
	int64_t ver = 0, at = 0;
	std::string name;   // the name it was written with (rename -> due)
};
struct State {
	std::string spaceId, epoch, libraryId, deviceId, deviceName;
	int64_t libraryGen = 0, offset = 0, cursor = 0, purgedSeq = 0, lastSyncAt = 0, lazySince = 0;
	bool joining = false;
	bool applying = false;    // a pull's apply step was under way (a crash there: the next pull joins)
	std::string gone;         // space_deleted | space_expired: stopped (§4.11)
	std::map<std::string, RecState> records;
	std::map<std::string, SeenRec> seen;
	std::vector<std::string> force, refetch;
	DeviceRecState device;
	std::map<std::string, std::string> tooBig;                          // rid -> contentHash
	std::map<std::string, std::pair<int64_t, std::string>> unreadable;  // rid -> (ver, newer|damaged)
	std::map<std::string, std::string> hints;                           // rid -> local (a re-join)
	std::map<std::string, std::pair<std::string, int64_t>> devices;     // rid -> (name, lastSyncAt)
	struct Side { int64_t ver = 0; std::string h, kind, json; };
	std::map<std::string, Side> side;                                    // side records (SYNC.md 2.5.1)
	bool sideKnown = false;                                              // the state was written by an engine with side records
	json::Value hashCache = json::Value::object();

	json::Value toJson() const;
	static bool fromJson(const json::Value& v, State& out);
};

// ---- The algorithm (Sync.cpp): one device, one cycle at a time --------------------------

// How the algorithm reaches the outside world. Everything optional has a quiet default.
struct CoreHooks {
	std::function<HttpResponse(const HttpRequest&)> http;
	// Run a step that touches the library or windows (the UI thread in the apps). Throws Stopped
	// if the step was dropped.
	std::function<void(const std::function<void()>&)> onMain;
	std::function<void(const std::string&)> notice;
	// Blocking questions (the engine waits for the person); throw Stopped to abandon the cycle.
	std::function<bool(int count)> askMassDelete;                                   // true = delete everywhere
	std::function<bool(int count, const std::string& devices)> askIncomingDeletes;  // true = move to trash
	// Window hooks (inside onMain).
	std::function<bool(const std::string& local, bool runtimeOnly)> held;
	std::function<void(const std::string& local, const std::string& device)> replaced;
	std::function<void(const std::string& local)> closing;
	std::function<void()> libraryChanged;
	std::function<void(const std::string& text, int done, int total)> progress;
	// Persisting (state.json, the secret); false = couldn't.
	std::function<bool(const State&)> saveState;
	// Inside onMain, after a pull that changed side records (2.5.1).
	std::function<void()> sideChanged;
};

struct CoreOptions {
	std::string serverBase = "https://cedarlogic.netlify.app/api/sync/v1";
	std::string appKey, client;
	bool web = false;          // self-test only: a CedarLogic Online-like client (copies, no versions)
	size_t batchItems = kBatchItems;
	int64_t changesLimit = 0;  // tests: ask for pages of this many entries (0: the server's default)
	GateDefaults gateDefaults;
	std::vector<std::string> sideKinds;   // SYNC.md 2.5.1: the kinds kept beside the circuits ("classroom", "membership")
};

class Core {
public:
	Core(CoreOptions, Crypto&, Clock&, Backend&, CoreHooks);

	// State: a fresh one (keeping this device's id and name), or a saved one.
	void reset();
	void adopt(const State& s);
	const State& state() const { return st; }
	State& mutableState() { return st; }

	bool enabled() const { return on; }
	const std::string& code() const { return code_; }
	const Keys& keys() const { return keys_; }
	void setCode(const std::string& canonical);   // syncing with this code from now on (state kept)
	void disable();

	// Turn On: a new code and PUT space (201). False and a sentence on failure.
	bool turnOn(std::string& message);
	// §4.8 step 1: 200 and the summary, or the HTTP status (0 offline, 400 not a code) and a sentence.
	int preview(const std::string& code, Preview& out, std::string& message);
	// §4.8 step 2: checks the space exists, then this code from now on, a fresh state, joining.
	bool link(const std::string& code, std::string& message, bool confirm = true);
	// Turn Off: synced circuits to the trash if asked, this device's record tombstoned (best effort).
	void turnOff(bool removeSynced);
	// DELETE the space with the delete token (200, or 410: already done), then off.
	int deleteSyncedCopy(std::string& message);

	// One cycle (§4.4). status(): synced, full, offline, gone, error, busy, stopped.
	void sync(bool flush);
	void syncPushOnly();   // the quitting path: no pull, no onMain, no questions
	void pull();
	void push(bool flush);

	const std::string& status() const { return status_; }
	const std::string& statusText() const { return statusText_; }
	const std::string& goneReason() const { return goneReason_; }
	int64_t retryAfterMs() const { return retryAfter_; }
	int64_t pollSeconds() const { return pollSeconds_; }
	const std::map<std::string, std::string>& problems() const { return problems_; }
	int circuitCount();
	std::vector<std::pair<std::string, int64_t>> deviceList() const;
	std::string deviceName() const { return st.deviceName; }
	void setDeviceName(const std::string& name) { st.deviceName = name; }
	bool heldLastPull() const { return heldLastPull_; }
	bool halted() const { return halted_; }   // a 401 or 403: no automatic retries
	void setBatchItems(size_t n) { opt.batchItems = n ? n : 1; }
	void setQuitting(bool q) { quitting_ = q; }

	size_t requests = 0;   // HTTP requests made (tests)
	std::string structureHash(const std::string& cdl);

private:
	struct Item {
		std::string id;
		int64_t base = 0, ver = 0;
		std::string data;
		bool deleted = false, device = false;
	};
	struct Incoming {
		std::string rid, local;
		int64_t ver;
		std::string device;
		int64_t seq = 0;   // the change-list entry, to hold the cursor before it
	};

	int rawApi(const Keys& k, const std::string& method, const std::string& path, const json::Value* body, json::Value& out,
	           const std::vector<std::pair<std::string, std::string>>& extra = {});
	int api(const std::string& method, const std::string& path, const json::Value* body, json::Value& out,
	        const std::vector<std::pair<std::string, std::string>>& extra = {});
	[[noreturn]] void throwFor(int status, const json::Value& out);
	void main(const std::function<void()>& fn);
	void note(const std::string& text);
	void save();
	int64_t now() { return clock_.now(); }
	int64_t serverNow() { return now() + st.offset; }

	bool hashesOf(const std::string& local, LocalHashes& h);
	std::string mappedRid(const std::string& local) const;
	void seenSet(const std::string& rid, int64_t ver, const std::string& h, const std::string* local);
	void addForce(const std::string& rid);
	void removeForce(const std::string& rid);
	bool inForce(const std::string& rid) const;
	bool inRefetch(const std::string& rid) const;
	bool newer(const Payload& p, int64_t updatedAt, int64_t hereMtime);
	bool held(const std::string& local, bool runtimeOnly);

	void libraryWentBack();
	void rewound(const std::string& epoch);
	void wentBack(const std::string& rid, int64_t ver);
	void damaged(const std::string& rid, int64_t ver, bool tombstone = false, const std::string& h = std::string());
	std::map<std::string, json::Value> fetchIds(std::vector<std::string> ids);
	bool openFetched(const std::string& rid, int64_t ver, const std::string& data, Payload& p, std::string& why);
	bool onRemoteUpdate(const std::string& rid, int64_t ver, int64_t updatedAt, const Payload& p);
	bool onRemoteDelete(const std::string& rid, int64_t ver, const Payload& p, std::vector<Incoming>& incoming);
	// Asks (if it is many), then trashes; the seqs of the entries that couldn't be handled now.
	std::vector<int64_t> incomingDeletes(std::vector<Incoming>& items);
	void buildJoinIndex();
	void onRemoteForgotten(const std::string& rid);
	std::string createLocal(const Payload& p);
	void setLocal(const std::string& local, const std::string& name, const std::string& cdl, const Payload& p);
	void keepLoser(const std::string& local, const std::string& name, const std::string& cdl, int64_t serverTime,
	               const std::string& device);
	std::string conflictNotice(const std::string& name, const std::string& device, const std::string& loserName,
	                           const std::string& loserDevice);
	void trashLocal(const std::string& local);
	std::string recNew(const std::string& rid, const std::string& local, int64_t ver, const std::string* name,
	                   const std::string* cdl);

	static json::Value itemJson(const Item& it);
	Item writeItem(const std::string& rid, RecState& s, const std::string& payload, bool deleted, bool compress,
	               const Sent* content, bool device, bool& tooBig);
	bool result(const Item& it, const json::Value& res, const std::map<std::string, std::string>& meta);
	bool sideResult(const Item& it, const json::Value& res);
public:
	// Side records (SYNC.md 2.5.1); any thread. The queue is memory only, sent by the next push.
	std::vector<std::pair<std::string, std::string>> sideRecords(const std::string& kind);
	std::string putSideRecord(const std::string& kind, const std::string& json, const std::string& rid);
	void deleteSideRecord(const std::string& rid);
private:
	struct SideQ { std::string kind, json; bool deleted = false; };
	std::map<std::string, SideQ> sideQueue_, sideSent_;
	std::mutex sideMu_;
	bool isSideKind(const std::string& k) const;

	CoreOptions opt;
	Crypto& crypto;
	Clock& clock_;
	Backend& lib;
	CoreHooks hooks;
	State st;
	bool on = false;
	std::string code_;
	Keys keys_;
	std::string status_ = "off", statusText_, goneReason_, fullText_, libraryFailed_;
	int64_t retryAfter_ = 0, pollSeconds_ = 600, lastRetryAfter_ = 0;
	bool full_ = false, quitting_ = false, saveFailed_ = false, libraryTouched_ = false, heldLastPull_ = false;
	bool noticedWentBack_ = false, halted_ = false;
	bool joinIndexBuilt_ = false;                                      // the join rule's candidates, once a pull
	std::vector<std::pair<std::string, LocalHashes>> joinCandidates_;
	std::map<std::string, std::string> problems_;      // folder -> sentence
	std::map<std::string, std::string> badRequest_;    // rid -> contentHash answered 400
	std::map<std::string, std::string> structureCache_;
	std::map<std::string, std::string> defaultsCache_;
};

// When to sync (§4.3): triggers, polling, backoff and Retry-After, as a pure
// function of time so the self-test can drive it with a fake clock.
class Scheduler {
public:
	void started(int64_t now);                    // the first cycle 2 s later
	void syncNow(int64_t now);                    // at once, a flush, past backoff (not past Retry-After)
	void libraryChanged(int64_t now);             // 5 s after the last change, within 60 s of the first
	void activated(int64_t now);                  // a cycle if the last attempt was over 60 s ago
	void deactivated(int64_t now, bool pending);  // to the background: a flush if anything waits
	void input(int64_t now);                      // keeps polling going for 10 minutes
	void setActive(bool a) { active = a; }
	// Whether a cycle is due now, and whether it is a flush. lazySince: the oldest unsent
	// runtime-only change (0 if none).
	bool due(int64_t now, bool& flush, int64_t lazySince);
	int64_t nextWake(int64_t now, int64_t lazySince) const;   // ms; INT64_MAX if nothing is planned
	void cycleStarted(int64_t now) { cycleStart = now; }
	void cycleDone(int64_t now, const std::string& status, int64_t retryAfterMs, int64_t pollSeconds, bool held);
	int64_t retryAt() const { return std::max(backoffUntil, retryAfterUntil); }
	int64_t lastAttemptAt() const { return lastAttempt; }

private:
	int64_t startAt = 0, lastAttempt = 0, firstChange = 0, lastChange = 0, cycleStart = 0;
	int64_t lastInput = 0, backoffUntil = 0, retryAfterUntil = 0, heldRetryAt = 0, pollMs = 600000;
	int failures = 0;
	bool nowPending = false, flushPending = false, retryPending = false, active = true, halted = false;
	uint32_t jitterSeed = 2463534242u;
};

// Status sentences (§5.1).
std::string statusSentence(const std::string& status, const std::string& detail, int64_t lastSyncAt, int64_t now);
std::string agoText(int64_t ms, int64_t now);

}  // namespace clsync
