// What the sync engine needs from this machine (SYNC.md 6.4, Linux): OpenSSL's
// libcrypto for the hashes and AES-GCM, GIO's zlib for deflate, the curl
// program for HTTP (the secrets never on its command line), a 0600 file for
// the sync code and a lock on the sync folder. GLib and OpenSSL only -- no
// GTK -- so Tools/sync_check.cpp runs the engine's self-test with the very
// hooks the app uses, on a machine with no display.

#ifndef CL_LINUX_SYNCPLATFORM_H
#define CL_LINUX_SYNCPLATFORM_H

#include "Sync.h"

#include <string>

namespace syncplatform {

// The engine's Crypto, on libcrypto (hashes, HMAC, AES-256-GCM, RAND_bytes) and
// GIO's raw zlib. One shared instance; every call is thread-safe.
clsync::Crypto& crypto();

// ---- HTTP --------------------------------------------------------------------------
// Through the curl program, as Send Feedback does, but with the request's
// headers in a 0600 temp file (`-H @file`) and its body in another, so a
// token is never in the command line other users can read in /proc. HTTPS
// only; plain http only to localhost, 127.0.0.1 or ::1 (a test server, CL_SYNC_URL).
// Every temp file is gone when it returns, on every path. CL_SYNC_CURL names
// another curl (the tests put a wrapper there that logs the arguments).
clsync::HttpResponse http(const clsync::HttpRequest& r);
bool curlAvailable();
// True for http://localhost..., http://127.0.0.1... and http://[::1]...
bool isLocalHttp(const std::string& url);

// ---- Where things are ---------------------------------------------------------------
std::string libraryRoot();      // ~/.local/share/CedarLogic/Library, as the app's Library
std::string syncFolder();       // ~/.local/share/CedarLogic/Sync (per machine, 0700)
// CL_SYNC_URL if it is https (or http to localhost), else the website's.
std::string serverBase();
std::string appKey();           // x-cedarlogic-key, the same key Send Feedback sends
std::string clientName();       // "linux/0.4.0+abc1234", for x-cedarlogic-client
// The pretty hostname (/etc/machine-info), else the host's name, at most 64 characters.
std::string defaultDeviceName();

// ---- The secret at rest, and the lock (SYNC.md 2.5) ----------------------------------
// The 28-symbol code in `dir`/secret, mode 0600 (the folder 0700).
std::string loadSecret(const std::string& dir);
bool saveSecret(const std::string& dir, const std::string& code);
void forgetSecret(const std::string& dir);
// flock(LOCK_EX | LOCK_NB) on the file; false when another process holds it.
bool tryLock(const std::string& path);
void unlock();

// ---- For tests and tools ---------------------------------------------------------------
// A Host with the real HTTP, secret and lock and no windows: every UI call
// does nothing and onMain runs where it is called.
struct HeadlessHost : clsync::Host {
	explicit HeadlessHost(std::string dir = std::string()) : secretDir(std::move(dir)) {}
	std::string secretDir;
	clsync::Status last;
	int statusCount = 0;
	clsync::HttpResponse http(const clsync::HttpRequest& r) override { return syncplatform::http(r); }
	void onMain(const std::function<void()>& fn) override { fn(); }
	std::string loadSecret() override { return syncplatform::loadSecret(secretDir); }
	bool saveSecret(const std::string& code) override { return syncplatform::saveSecret(secretDir, code); }
	void forgetSecret() override { syncplatform::forgetSecret(secretDir); }
	bool tryLock(const std::string& path) override { return syncplatform::tryLock(path); }
	void unlock() override { syncplatform::unlock(); }
	void flushOpen(std::function<void()> done) override { done(); }
	clsync::WindowState windowState(const std::string&) override { return clsync::WindowState(); }
	void circuitReplaced(const std::string&, const std::string&) override {}
	void closeCircuit(const std::string&) override {}
	void libraryChanged() override {}
	void statusChanged(const clsync::Status& s) override { last = s; statusCount++; }
	void notice(const std::string&) override {}
	void askMassDelete(int, std::function<void(bool)> answer) override { answer(true); }
	void askIncomingDeletes(int, const std::string&, std::function<void(bool)> answer) override { answer(true); }
};

// The engine's own self-test (SYNC.md 7.1 vectors, 7.2 scenarios) with these
// hooks; with `serverBase` also the single-device scenarios over HTTP against a
// mock server. `only` limits it to the checks whose name contains it.
bool selfTest(const std::string& tempDir, const std::string& serverBase, const std::string& only, std::string& report);

}  // namespace syncplatform

#endif  // CL_LINUX_SYNCPLATFORM_H
