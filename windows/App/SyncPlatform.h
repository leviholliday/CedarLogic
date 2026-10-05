// What the sync engine (mac/CedarCore/Sync.h, SYNC.md) needs from Windows:
// the cryptography (CNG -- BCrypt -- which comes with Windows), HTTPS (WinHTTP,
// Http.h), the secret kept at rest (DPAPI), the lock that keeps two copies of
// the app from syncing one library, and where sync keeps its files. The app's
// side of sync (windows, dialogs, Settings) is SyncApp.h.
//
// Sync's own files are in %LOCALAPPDATA%\CedarLogic\Sync, not beside the
// library in the roaming %APPDATA%: a lab PC's roaming profile must not carry
// another PC's sync state or code. The code itself is encrypted with DPAPI
// for the signed-in user (secret.dpapi), so another account can't read it.

#ifndef CL_WINDOWS_SYNC_PLATFORM_H
#define CL_WINDOWS_SYNC_PLATFORM_H

#include "Sync.h"

#include <string>

namespace syncplat {

// CNG: BCryptGenRandom, SHA-256 and HMAC, AES-256-GCM, and deflate from
// Deflate.h. Thread-safe.
clsync::Crypto& crypto();

// One HTTPS request (or one to this computer, for testing).
clsync::HttpResponse httpCall(const clsync::HttpRequest& r);

// %LOCALAPPDATA%\CedarLogic\Sync
std::string syncDir();
// https://cedarlogic.netlify.app/api/sync/v1, or CL_SYNC_URL when that is an
// https address or one on this computer.
std::string serverBase();
// The computer's name (the physical DNS host name), at most 64 characters.
std::string deviceName();
// "windows/0.1.0+1a2b3c4", for x-cedarlogic-client.
std::string clientName();

// The code, kept for this user only (DPAPI) in `dir`\secret.dpapi.
class SecretStore {
public:
	explicit SecretStore(std::string dir) : dir(std::move(dir)) {}
	std::string path() const { return dir + "\\secret.dpapi"; }
	std::string load() const;              // "" if there isn't one, or it can't be read
	bool save(const std::string& code) const;
	void forget() const;
private:
	std::string dir;
};

// A file held exclusively while the engine runs.
class FileLock {
public:
	~FileLock() { unlock(); }
	bool tryLock(const std::string& path);   // false if another process has it
	void unlock();
private:
	void* handle = nullptr;
};

// The parts of a Host that need no window: HTTP, the secret and the lock.
class PlatformHost : public clsync::Host {
public:
	explicit PlatformHost(std::string dir = std::string());
	clsync::HttpResponse http(const clsync::HttpRequest& r) override { return httpCall(r); }
	std::string loadSecret() override { return secrets.load(); }
	bool saveSecret(const std::string& code) override { return secrets.save(code); }
	void forgetSecret() override { secrets.forget(); }
	bool tryLock(const std::string& path) override { return lock.tryLock(path); }
	void unlock() override { lock.unlock(); }
protected:
	SecretStore secrets;
	FileLock lock;
};

// --sync-test [--server <url>]: the cryptography against known answers, the
// secret, the lock and the URL rules, then the engine's own self-test (the
// design's vectors and scenarios; against the mock server too when one is
// named, or CL_SYNC_URL is). A PASS or FAIL line each in `report`; false if
// any failed.
bool runSelfTest(std::string& report, const std::string& server);

}  // namespace syncplat

#endif  // CL_WINDOWS_SYNC_PLATFORM_H
