// The CedarLogic sync engine (SYNC.md). Plain C++17; the platform comes in
// through Crypto and Host.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace clsync {

using Bytes = std::vector<uint8_t>;

// ---- What the platform provides -------------------------------------------------

// Thread-safe; called from the engine thread and from selfTest.
struct Crypto {
	virtual ~Crypto() = default;
	virtual bool random(uint8_t* out, size_t n) = 0;                       // a CSPRNG; false = stop, never use the buffer
	virtual void sha256(const uint8_t* p, size_t n, uint8_t out[32]) = 0;
	virtual void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* p, size_t n, uint8_t out[32]) = 0;
	// ctTag = ciphertext ‖ 16-byte tag. open() returns false on a tag mismatch.
	virtual bool aesGcmSeal(const uint8_t key[32], const uint8_t nonce[12], const Bytes& aad, const Bytes& plain, Bytes& ctTag) = 0;
	virtual bool aesGcmOpen(const uint8_t key[32], const uint8_t nonce[12], const Bytes& aad, const Bytes& ctTag, Bytes& plain) = 0;
	// Raw deflate (RFC 1951). deflateRaw may return false: the record goes uncompressed.
	virtual bool deflateRaw(const Bytes& in, Bytes& out) { (void)in; (void)out; return false; }
	virtual bool inflateRaw(const Bytes& in, size_t maxOut, Bytes& out) = 0;   // false past maxOut
};

struct HttpRequest {
	std::string method;                                      // GET PUT POST DELETE
	std::string url;                                         // absolute
	std::vector<std::pair<std::string, std::string>> headers;
	std::string body;                                        // JSON or empty
};
struct HttpResponse {
	bool sent = false;      // the whole request went out (the server may have acted)
	int status = 0;         // 0: no answer (offline, timeout, TLS failure)
	std::map<std::string, std::string> headers;   // names lower-case (date, retry-after)
	std::string body;
};

struct Status {
	enum Kind { Off, Synced, Syncing, Offline, Error, Full, Gone, Busy } kind = Off;
	std::string text;        // the §5.1 sentence, ready to show
	int64_t lastSyncAt = 0;  // ms
	int circuits = 0;
	int progressDone = 0, progressTotal = 0;   // "Bringing in 12 of 37…"
	std::vector<std::pair<std::string, std::string>> problems;   // (folder id, "Too big to sync (over 512 KB)")
};

struct Preview {            // what a code holds (§4.8), for the Link confirmation
	int circuits = 0;
	std::vector<std::string> devices;   // device names
	std::vector<std::string> names;     // up to 5 circuit names
	int64_t newestEdit = 0;             // ms, 0 if none
	std::string sentence;               // the §5.1 confirmation text, ready to show
};

struct WindowState { bool open = false, dirty = false; int64_t lastInputAt = 0; };

struct Host {
	virtual ~Host() = default;
	// Engine thread. Blocking; ~20 s connect, 60 s total; HTTPS only (http only for a localhost CL_SYNC_URL).
	virtual HttpResponse http(const HttpRequest& r) = 0;
	// Engine thread: run fn on the UI thread and wait for it. Never called after stop() returned, nor in quitting().
	virtual void onMain(const std::function<void()>& fn) = 0;
	// The secret (the 28-symbol code) at rest: §2.5. load returns "" if none.
	virtual std::string loadSecret() = 0;
	virtual bool saveSecret(const std::string& code) = 0;
	virtual void forgetSecret() = 0;
	// A lock on the sync folder held while the engine runs; false if another process has it.
	virtual bool tryLock(const std::string& lockPath) = 0;
	virtual void unlock() = 0;
	// UI thread (inside onMain):
	virtual void flushOpen(std::function<void()> done) = 0;                    // save every open library circuit with unsaved
	                                                                           // changes; call done when saved (any thread)
	virtual WindowState windowState(const std::string& folderId) = 0;          // §4.12 apply step
	virtual void circuitReplaced(const std::string& folderId, const std::string& fromDevice) = 0;   // reload its windows
	virtual void closeCircuit(const std::string& folderId) = 0;               // close its (clean) windows
	virtual void libraryChanged() = 0;                                         // refresh Your Circuits
	virtual void statusChanged(const Status& s) = 0;
	virtual void notice(const std::string& text) = 0;                          // a one-line note
	// Questions; answer on the UI thread whenever the person decides (the cycle waits, the cursor held).
	virtual void askMassDelete(int count, std::function<void(bool deleteEverywhere)> answer) = 0;
	virtual void askIncomingDeletes(int count, const std::string& fromDevices,
	                                std::function<void(bool moveToTrash)> answer) = 0;
	// UI thread: a pull changed side records (SYNC.md 2.5.1); read them with Engine::sideRecords.
	virtual void sideChanged() {}
};

struct Config {
	std::string libraryRoot;       // the apps' library folder
	std::string syncDir;           // §2.5 per-machine folder (state.json, lock)
	std::string serverBase = "https://cedarlogic.netlify.app/api/sync/v1";
	std::string appKey;            // x-cedarlogic-key
	std::string client;            // x-cedarlogic-client, e.g. "linux/0.4.0+812"
	std::string defaultDeviceName; // §5.1
	std::function<std::string(const std::string& lib, bool gui, const std::string& name)> gateDefault;
	                               // the gate library's default for a param, or "\x01" for none (§2.4)
	std::vector<std::string> sideKinds;   // SYNC.md 2.5.1: kinds kept beside the circuits ("classroom", "membership")
};

// ---- Codes and text (any thread) -----------------------------------------------------

std::string newCode(Crypto&);                                  // 28 symbols ("" if the RNG failed)
// Text typed, pasted or scanned (or a link) -> the canonical code; false and why ("length", "symbol", "checksum").
bool parseCode(Crypto&, const std::string& text, std::string& code, std::string& why);
std::string whyText(const std::string& why, const std::string& text);   // the §1.2 sentences
std::string groupCode(const std::string& code);                // XXXX-XXXX-…
std::string webLink(const std::string& code);                  // https://cedarlogic.netlify.app/sync/#k=…
std::string appLink(const std::string& code);                  // cedarlogic://sync#k=…
// The QR code of webLink(code): size x size modules, row-major, true = dark (quiet zone not included).
std::vector<bool> qr(const std::string& text, int& size);

// ---- The engine (create, call and destroy on the UI thread) -------------------------

class Engine {
public:
	Engine(Config, Crypto&, Host&);
	~Engine();                                                 // stop(), joins the thread

	// Starts syncing if a secret is stored. Call once at launch.
	void start();
	void stop();                                               // returns at once; the thread finishes its step and exits

	bool enabled() const;
	std::string code() const;                                  // "" when off
	std::string deviceName() const;
	void setDeviceName(const std::string&);
	Status status() const;
	std::vector<std::pair<std::string, int64_t>> devices() const;   // (name, lastSyncAt) from the device records

	// Each finishes on the UI thread through `done` (ok, or a sentence for the person).
	void turnOn(std::function<void(bool, std::string)> done);                                   // new code, PUT space, first cycle
	void preview(const std::string& code, std::function<void(bool, std::string, Preview)> done); // §4.8 step 1: nothing stored
	void link(const std::string& code, std::function<void(bool, std::string)> done);            // step 2, after the confirmation
	void turnOff(bool removeSyncedCircuits);
	void deleteSyncedCopy(std::function<void(bool, std::string)> done);
	void startOver(std::function<void(bool, std::string)> done);                               // delete + turnOn

	// Triggers (§4.3).
	void syncNow();                     // a flush
	void noteLibraryChanged();          // after any save/rename/import/delete/restore in the library
	void appActivated();                // app or a window came to the front
	void appDeactivated();              // to the background: a flush
	void userActive();                  // input happened (keeps polling going for 10 min)
	// §4.12 Quitting: call after the host saved every open circuit. A push-only cycle on the engine
	// thread that never calls onMain; `done` runs on the engine thread within 5 s.
	void quitting(std::function<void()> done);

	// Pairing (SYNC.md 11.6): this device joins by showing a QR code that a device that syncs
	// scans. Sync must be off. On the engine thread: a slot on the website, then a poll every 3 s
	// for up to 10 minutes. `show` gets the QR code's text (the pairing link) once the website has
	// the slot; `done` once, with PairCode (text = the sync code, for preview() and link();
	// from = the sending device's name, ready to show), PairExpired or PairFailed (text = the
	// sentence). Both on the UI thread. pairCancel(), a new pairStart(), turning sync on, stop(),
	// quitting() or destroying the engine end it: done isn't called then, and the slot is deleted
	// (best effort).
	enum PairResult { PairCode = 0, PairExpired = 1, PairFailed = 2 };
	void pairStart(std::function<void(const std::string& link)> show,
	               std::function<void(int result, const std::string& text, const std::string& from)> done);
	void pairCancel();

	// Side records (SYNC.md 2.5.1), any thread: (rid, payload JSON) of a kind, as last pulled or
	// written; a write (rid "" = a new id, returned) and a tombstone are sent by the next push.
	std::vector<std::pair<std::string, std::string>> sideRecords(const std::string& kind);
	std::string putSideRecord(const std::string& kind, const std::string& json, const std::string& rid);
	void deleteSideRecord(const std::string& rid);

private:
	struct Impl;
	std::unique_ptr<Impl> d;
};

// ---- Tests ---------------------------------------------------------------------------

// The vectors of SYNC.md §7.1 and the scenarios of §7.2 on an in-process
// FakeServer and temporary library folders (under tempDir). One PASS/FAIL line
// each in `report`; false if any failed. If serverBase is set (a mock server,
// §10 S), the scenarios that need only one device also run against it over http().
bool selfTest(Crypto&, const std::string& tempDir, std::string& report, Host* httpOnly = nullptr,
              const std::string& serverBase = "");

}  // namespace clsync
