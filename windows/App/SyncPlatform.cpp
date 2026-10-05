// What the sync engine needs from Windows (see SyncPlatform.h).

#include "SyncPlatform.h"
#include "App.h"
#include "Deflate.h"
#include "Http.h"

#include <bcrypt.h>
#include <shlobj.h>
#include <wincrypt.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifndef NT_SUCCESS
#define NT_SUCCESS(status) (((NTSTATUS)(status)) >= 0)
#endif
// STATUS_AUTH_TAG_MISMATCH: the tag of an AES-GCM message didn't match.
#define CL_STATUS_AUTH_TAG_MISMATCH ((NTSTATUS)0xC000A002L)

namespace syncplat {

namespace {

// ---- CNG -------------------------------------------------------------------------
// The providers are opened once and kept (a handle is thread-safe to use). The
// pseudo-handles BCRYPT_SHA256_ALG_HANDLE and BCRYPT_HMAC_SHA256_ALG_HANDLE
// would do on MSVC, but MinGW's import library lacks them.

struct Providers {
	BCRYPT_ALG_HANDLE sha = nullptr, hmac = nullptr, aes = nullptr;
	bool ok = false;
	Providers() {
		const bool a = NT_SUCCESS(BCryptOpenAlgorithmProvider(&sha, BCRYPT_SHA256_ALGORITHM, nullptr, 0));
		const bool b = NT_SUCCESS(BCryptOpenAlgorithmProvider(&hmac, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG));
		bool c = NT_SUCCESS(BCryptOpenAlgorithmProvider(&aes, BCRYPT_AES_ALGORITHM, nullptr, 0));
		if (c)
			c = NT_SUCCESS(BCryptSetProperty(aes, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0));
		ok = a && b && c;
	}
	// (Never closed: they live as long as the process.)
};

Providers& providers() {
	static Providers p;   // made once, thread-safely
	return p;
}

const uint8_t kNothing = 0;   // a pointer for an empty input (CNG wants one)

// SHA-256 or HMAC-SHA-256 of one buffer. False if CNG failed (it doesn't, short of running out of memory).
bool hashOnce(BCRYPT_ALG_HANDLE alg, const uint8_t* key, size_t keyLen, const uint8_t* p, size_t n, uint8_t out[32]) {
	if (alg == nullptr) return false;
	BCRYPT_HASH_HANDLE h = nullptr;
	if (!NT_SUCCESS(BCryptCreateHash(alg, &h, nullptr, 0, key ? (PUCHAR)(keyLen ? key : &kNothing) : nullptr, key ? (ULONG)keyLen : 0, 0)))
		return false;
	bool ok = true;
	// (Blocks of at most 1 GiB: the length is a ULONG.)
	while (ok && n > 0) {
		const ULONG step = (ULONG)std::min<size_t>(n, 1u << 30);
		ok = NT_SUCCESS(BCryptHashData(h, (PUCHAR)p, step, 0));
		p += step;
		n -= step;
	}
	ok = ok && NT_SUCCESS(BCryptFinishHash(h, out, 32, 0));
	BCryptDestroyHash(h);
	return ok;
}

class CngCrypto : public clsync::Crypto {
public:
	bool random(uint8_t* out, size_t n) override {
		// The system's preferred generator. A failure stops the operation: the
		// buffer is never used (SYNC.md 1.1).
		while (n > 0) {
			const ULONG step = (ULONG)std::min<size_t>(n, 1u << 20);
			if (!NT_SUCCESS(BCryptGenRandom(nullptr, out, step, BCRYPT_USE_SYSTEM_PREFERRED_RNG))) return false;
			out += step;
			n -= step;
		}
		return true;
	}
	void sha256(const uint8_t* p, size_t n, uint8_t out[32]) override {
		if (!hashOnce(providers().sha, nullptr, 0, p, n, out)) memset(out, 0, 32);   // (a wrong hash is a sync error, never a leak)
	}
	void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* p, size_t n, uint8_t out[32]) override {
		if (!hashOnce(providers().hmac, key ? key : &kNothing, keyLen, p, n, out)) memset(out, 0, 32);
	}
	bool aesGcmSeal(const uint8_t key[32], const uint8_t nonce[12], const clsync::Bytes& aad, const clsync::Bytes& plain,
	                clsync::Bytes& ctTag) override {
		if (!providers().ok) return false;
		BCRYPT_KEY_HANDLE k = nullptr;
		if (!NT_SUCCESS(BCryptGenerateSymmetricKey(providers().aes, &k, nullptr, 0, (PUCHAR)key, 32, 0))) return false;
		uint8_t tag[16] = {};
		uint8_t iv[12];
		memcpy(iv, nonce, 12);
		BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
		BCRYPT_INIT_AUTH_MODE_INFO(info);
		info.pbNonce = iv;
		info.cbNonce = 12;
		info.pbAuthData = aad.empty() ? nullptr : (PUCHAR)aad.data();
		info.cbAuthData = (ULONG)aad.size();
		info.pbTag = tag;
		info.cbTag = 16;
		ctTag.assign(plain.size() + 16, 0);
		ULONG got = 0;
		const NTSTATUS st = BCryptEncrypt(k, plain.empty() ? (PUCHAR)&kNothing : (PUCHAR)plain.data(), (ULONG)plain.size(), &info, nullptr, 0,
		                                  ctTag.data(), (ULONG)plain.size(), &got, 0);
		BCryptDestroyKey(k);
		if (!NT_SUCCESS(st) || got != plain.size()) {
			ctTag.clear();
			return false;
		}
		memcpy(ctTag.data() + plain.size(), tag, 16);
		return true;
	}
	bool aesGcmOpen(const uint8_t key[32], const uint8_t nonce[12], const clsync::Bytes& aad, const clsync::Bytes& ctTag,
	                clsync::Bytes& plain) override {
		plain.clear();
		if (!providers().ok || ctTag.size() < 16) return false;
		const size_t n = ctTag.size() - 16;
		BCRYPT_KEY_HANDLE k = nullptr;
		if (!NT_SUCCESS(BCryptGenerateSymmetricKey(providers().aes, &k, nullptr, 0, (PUCHAR)key, 32, 0))) return false;
		uint8_t tag[16], iv[12];
		memcpy(tag, ctTag.data() + n, 16);
		memcpy(iv, nonce, 12);
		BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
		BCRYPT_INIT_AUTH_MODE_INFO(info);
		info.pbNonce = iv;
		info.cbNonce = 12;
		info.pbAuthData = aad.empty() ? nullptr : (PUCHAR)aad.data();
		info.cbAuthData = (ULONG)aad.size();
		info.pbTag = tag;
		info.cbTag = 16;
		clsync::Bytes out(n + 1, 0);   // (+1: a buffer even for an empty message)
		ULONG got = 0;
		const NTSTATUS st = BCryptDecrypt(k, n ? (PUCHAR)ctTag.data() : (PUCHAR)&kNothing, (ULONG)n, &info, nullptr, 0, out.data(), (ULONG)n,
		                                  &got, 0);
		BCryptDestroyKey(k);
		// CL_STATUS_AUTH_TAG_MISMATCH: it was changed, or it isn't for this key and id.
		if (!NT_SUCCESS(st) || st == CL_STATUS_AUTH_TAG_MISMATCH || got != n) return false;
		out.resize(n);
		plain.swap(out);
		return true;
	}
	bool deflateRaw(const clsync::Bytes& in, clsync::Bytes& out) override {
		const std::string packed = deflate::compress(std::string(in.begin(), in.end()));
		out.assign(packed.begin(), packed.end());
		return true;
	}
	bool inflateRaw(const clsync::Bytes& in, size_t maxOut, clsync::Bytes& out) override {
		out.clear();
		std::string plain;
		if (!deflate::decompress(std::string(in.begin(), in.end()), plain, maxOut)) return false;
		out.assign(plain.begin(), plain.end());
		return true;
	}
};

// ---- Files ---------------------------------------------------------------------------

std::string knownFolder(const KNOWNFOLDERID& id) {
	PWSTR p = nullptr;
	std::string dir;
	if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p)) && p) dir = U(p);
	if (p) CoTaskMemFree(p);
	return dir;
}

bool makeDir(const std::string& dir) {
	const int made = SHCreateDirectoryExW(nullptr, W(dir).c_str(), nullptr);
	return made == ERROR_SUCCESS || made == ERROR_ALREADY_EXISTS || made == ERROR_FILE_EXISTS;
}

bool readAll(const std::string& path, std::string& out) {
	HANDLE h = CreateFileW(W(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	out.clear();
	char buf[4096];
	DWORD got = 0;
	bool ok = true;
	while (ReadFile(h, buf, sizeof buf, &got, nullptr) && got > 0) {
		out.append(buf, got);
		if (out.size() > (1u << 20)) { ok = false; break; }   // a code is a few dozen bytes
	}
	CloseHandle(h);
	return ok;
}

// Written beside it and moved over it, so a code is never half there.
bool writeAtomic(const std::string& path, const std::string& bytes) {
	const std::wstring target = W(path), tmp = target + L".tmp";
	HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	DWORD wrote = 0;
	bool ok = WriteFile(h, bytes.data(), (DWORD)bytes.size(), &wrote, nullptr) && wrote == bytes.size() && FlushFileBuffers(h);
	CloseHandle(h);
	ok = ok && MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
	if (!ok) DeleteFileW(tmp.c_str());
	return ok;
}

// A sync code's own domain, so another program's DPAPI blob (same user) can't
// be mistaken for it, and ours for theirs.
const char kEntropy[] = "cedarlogic-sync-v1";

bool sameOrInside(const std::string& path, const std::string& folder) {
	if (folder.empty()) return false;
	std::string p = lowerCase(path), f = lowerCase(folder);
	while (!f.empty() && f.back() == '\\') f.pop_back();
	return p == f || p.compare(0, f.size() + 1, f + "\\") == 0;
}

std::string removeTrailing(std::string s) {
	while (!s.empty() && (s.back() == '\\' || s.back() == '/')) s.pop_back();
	return s;
}

}  // namespace

clsync::Crypto& crypto() {
	static CngCrypto c;
	return c;
}

clsync::HttpResponse httpCall(const clsync::HttpRequest& r) {
	http::Request q;
	q.method = r.method;
	q.url = r.url;
	q.headers = r.headers;
	q.body = r.body;
	http::Response a = http::send(q);
	clsync::HttpResponse out;
	out.sent = a.sent;
	out.status = a.status;
	out.headers = std::move(a.headers);
	out.body = std::move(a.body);
	return out;
}

std::string syncDir() {
	std::string local = knownFolder(FOLDERID_LocalAppData);
	if (local.empty()) {
		wchar_t tmp[MAX_PATH + 1];
		const DWORD n = GetTempPathW(MAX_PATH + 1, tmp);
		local = removeTrailing(U(std::wstring(tmp, n > 0 && n <= MAX_PATH ? n : 0)));
	}
	return removeTrailing(local) + "\\CedarLogic\\Sync";
}

std::string serverBase() {
	const std::string standard = "https://cedarlogic.netlify.app/api/sync/v1";
	char buf[1024];
	const DWORD n = GetEnvironmentVariableA("CL_SYNC_URL", buf, sizeof buf);
	if (n == 0 || n >= sizeof buf) return standard;
	std::string url(buf, n);
	while (!url.empty() && url.back() == '/') url.pop_back();
	return http::urlAllowed(url) ? url : standard;
}

std::string deviceName() {
	wchar_t buf[256] = L"";
	DWORD n = 256;
	std::string name;
	if (GetComputerNameExW(ComputerNamePhysicalDnsHostname, buf, &n) && n > 0) name = U(std::wstring(buf, n));
	if (name.empty()) name = "Windows PC";
	// At most 64 characters (not bytes, and never half of one).
	size_t scalars = 0, at = 0;
	while (at < name.size() && scalars < 64) {
		const unsigned char c = (unsigned char)name[at];
		at += c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
		scalars++;
	}
	return name.substr(0, std::min(at, name.size()));
}

std::string clientName() { return strf("windows/%s+%.7s", CL_VERSION, CL_GIT_COMMIT); }

// ---- The code at rest ---------------------------------------------------------------

std::string SecretStore::load() const {
	std::string blob;
	if (!readAll(path(), blob) || blob.empty()) return std::string();
	DATA_BLOB in = { (DWORD)blob.size(), (BYTE*)blob.data() }, entropy = { (DWORD)(sizeof kEntropy - 1), (BYTE*)kEntropy }, out = {};
	if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return std::string();
	std::string code((const char*)out.pbData, out.cbData);
	SecureZeroMemory(out.pbData, out.cbData);
	LocalFree(out.pbData);
	return code;
}

bool SecretStore::save(const std::string& code) const {
	if (!makeDir(dir)) return false;
	DATA_BLOB in = { (DWORD)code.size(), (BYTE*)code.data() }, entropy = { (DWORD)(sizeof kEntropy - 1), (BYTE*)kEntropy }, out = {};
	if (!CryptProtectData(&in, L"CedarLogic sync code", &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return false;
	const bool ok = writeAtomic(path(), std::string((const char*)out.pbData, out.cbData));
	LocalFree(out.pbData);
	return ok;
}

void SecretStore::forget() const { DeleteFileW(W(path()).c_str()); }

// ---- The lock -----------------------------------------------------------------------

bool FileLock::tryLock(const std::string& path) {
	if (handle) return true;
	const size_t slash = path.find_last_of("\\/");
	if (slash != std::string::npos) makeDir(path.substr(0, slash));
	// Opened for no one else to share: a second process can't open it, let
	// alone lock it. (The lock itself is the same promise, stated.)
	HANDLE h = CreateFileW(W(path).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	OVERLAPPED ov = {};
	if (!LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov)) {
		CloseHandle(h);
		return false;
	}
	handle = h;
	return true;
}

void FileLock::unlock() {
	if (handle == nullptr) return;
	OVERLAPPED ov = {};
	UnlockFileEx((HANDLE)handle, 0, 1, 0, &ov);
	CloseHandle((HANDLE)handle);
	handle = nullptr;
}

PlatformHost::PlatformHost(std::string dir) : secrets(dir.empty() ? syncDir() : dir) {}

// ---- --sync-test ----------------------------------------------------------------------

namespace {

std::string hex(const uint8_t* p, size_t n) {
	static const char* d = "0123456789abcdef";
	std::string s;
	for (size_t i = 0; i < n; i++) { s += d[p[i] >> 4]; s += d[p[i] & 15]; }
	return s;
}

clsync::Bytes bytes(const std::string& s) { return clsync::Bytes(s.begin(), s.end()); }

// The engine's self-test wants a Host for HTTP (for the mock server); the
// rest of what a Host does is the app's windows, which there are none of.
class TestHost : public PlatformHost {
public:
	using PlatformHost::PlatformHost;
	void onMain(const std::function<void()>& fn) override { fn(); }
	void flushOpen(std::function<void()> done) override { done(); }
	clsync::WindowState windowState(const std::string&) override { return clsync::WindowState(); }
	void circuitReplaced(const std::string&, const std::string&) override {}
	void closeCircuit(const std::string&) override {}
	void libraryChanged() override {}
	void statusChanged(const clsync::Status&) override {}
	void notice(const std::string&) override {}
	void askMassDelete(int, std::function<void(bool)> answer) override { answer(true); }
	void askIncomingDeletes(int, const std::string&, std::function<void(bool)> answer) override { answer(true); }
};

std::string uniqueTempDir() {
	wchar_t tmp[MAX_PATH + 1];
	const DWORD n = GetTempPathW(MAX_PATH + 1, tmp);
	std::string base = removeTrailing(U(std::wstring(tmp, n > 0 && n <= MAX_PATH ? n : 0)));
	return base + strf("\\cl-sync-test-%lu-%lu", (unsigned long)GetCurrentProcessId(), (unsigned long)GetTickCount());
}

}  // namespace

bool runSelfTest(std::string& report, const std::string& server) {
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		report += strf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
		if (!ok) failures++;
	};
	clsync::Crypto& c = crypto();

	// The cryptography, against answers from the standards (the design's vectors,
	// in the engine's test below, say the same of what is built on it).
	uint8_t d[32];
	c.sha256((const uint8_t*)"abc", 3, d);
	check(hex(d, 32) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "CNG SHA-256 of \"abc\"");
	const uint8_t k20[20] = { 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b };
	c.hmacSha256(k20, 20, (const uint8_t*)"Hi There", 8, d);
	check(hex(d, 32) == "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", "CNG HMAC-SHA-256 (RFC 4231, case 1)");
	uint8_t zeros32[32] = {}, zeros12[12] = {};
	clsync::Bytes ct, back;
	check(c.aesGcmSeal(zeros32, zeros12, clsync::Bytes(), clsync::Bytes(16, 0), ct) && ct.size() == 32 &&
	          hex(ct.data(), 32) == "cea7403d4d606b6e074ec5d3baf39d18d0d1c8a799996bf0265b98b5d48ab919",
	      "CNG AES-256-GCM seal (the GCM paper's test case 14)");
	check(c.aesGcmOpen(zeros32, zeros12, clsync::Bytes(), ct, back) && back == clsync::Bytes(16, 0), "CNG AES-256-GCM open, back to the plaintext");
	clsync::Bytes tampered = ct;
	tampered[3] ^= 1;
	check(!c.aesGcmOpen(zeros32, zeros12, clsync::Bytes(), tampered, back) && back.empty(), "a changed ciphertext doesn't open");
	tampered = ct;
	tampered[31] ^= 0x80;
	check(!c.aesGcmOpen(zeros32, zeros12, clsync::Bytes(), tampered, back), "a changed tag doesn't open");
	check(!c.aesGcmOpen(zeros32, zeros12, bytes("other id"), ct, back), "other associated data doesn't open");
	check(c.aesGcmSeal(zeros32, zeros12, clsync::Bytes(), clsync::Bytes(), ct) && ct.size() == 16 &&
	          hex(ct.data(), 16) == "530f8afbc74536b9a963b4f1c4cb738b",
	      "CNG AES-256-GCM of nothing (test case 13)");
	check(c.aesGcmOpen(zeros32, zeros12, clsync::Bytes(), ct, back) && back.empty(), "...opens to nothing");
	uint8_t r1[16], r2[16];
	check(c.random(r1, 16) && c.random(r2, 16) && memcmp(r1, r2, 16) != 0, "BCryptGenRandom gives different bytes each time");
	const std::string text = std::string("(cedarlogic (version 3) ") + std::string(5000, 'x') + ")";
	clsync::Bytes packed, unpacked;
	check(c.deflateRaw(bytes(text), packed) && packed.size() < text.size() / 4 && c.inflateRaw(packed, 20000000, unpacked) && unpacked == bytes(text),
	      "deflate and inflate give the text back");
	check(!c.inflateRaw(packed, 100, unpacked), "inflate stops past its limit");

	// The URL rules: https, or this computer for testing.
	check(http::urlAllowed("https://cedarlogic.netlify.app/api/sync/v1/health") && http::urlAllowed("http://localhost:8787/api/sync/v1") &&
	          http::urlAllowed("http://127.0.0.1:8787/x"),
	      "https and this computer's http are allowed");
	check(!http::urlAllowed("http://cedarlogic.netlify.app/api") && !http::urlAllowed("http://localhost.example.com/") &&
	          !http::urlAllowed("ftp://localhost/") && !http::urlAllowed("not a url"),
	      "plain http elsewhere, and other schemes, are refused");
	{
		http::Request evil;
		evil.method = "GET";
		evil.url = "https://cedarlogic.netlify.app/api/sync/v1/health";
		evil.headers = { { "x-test", "a\r\nAuthorization: Bearer stolen" } };
		const http::Response r = http::send(evil);
		check(!r.sent && r.status == 0, "a header with a line break in it isn't sent");
		http::Request plain;
		plain.method = "GET";
		plain.url = "http://example.invalid/";
		const http::Response p = http::send(plain);
		check(!p.sent && p.status == 0, "a request to plain http elsewhere isn't sent");
	}

	// Where sync keeps its files, and the code at rest.
	const std::string dir = syncDir(), roaming = knownFolder(FOLDERID_RoamingAppData), local = knownFolder(FOLDERID_LocalAppData);
	check(sameOrInside(dir, local) && !sameOrInside(dir, roaming) && dir.size() > 5 && dir.compare(dir.size() - 5, 5, "\\Sync") == 0,
	      "sync's files are in %LOCALAPPDATA%\\CedarLogic\\Sync, not the roaming folder: " + dir);
	const std::string temp = uniqueTempDir();
	{
		SecretStore store(temp + "\\secret-test");
		const std::string code = "000G40R40M30E209185GR38E1YZ4";
		check(store.load().empty(), "no code stored: none loaded");
		check(store.save(code), "the code is saved");
		std::string blob;
		const bool read = readAll(store.path(), blob);
		check(read && !blob.empty() && blob.find(code) == std::string::npos && blob.find("000G40R4") == std::string::npos,
		      "the file doesn't hold the code in the clear (DPAPI)");
		check(store.load() == code, "the code is read back");
		check(store.save("HRFNM31VKN3Y59P1Y3CV7T2JEGD3") && store.load() == "HRFNM31VKN3Y59P1Y3CV7T2JEGD3", "a new code replaces it");
		store.forget();
		check(store.load().empty() && !fileExists(store.path()), "forgotten: the file is gone");
	}
	{
		FileLock one, two;
		const std::string path = temp + "\\lock-test\\lock";
		check(one.tryLock(path), "a lock is taken");
		check(!two.tryLock(path), "a second one can't have it");
		one.unlock();
		check(two.tryLock(path), "once it's let go, it can");
	}

	// The engine: the design's vectors and scenarios (and the mock server's, when named).
	std::string base = server;
	if (base.empty()) {
		char buf[1024];
		const DWORD n = GetEnvironmentVariableA("CL_SYNC_URL", buf, sizeof buf);
		if (n > 0 && n < sizeof buf) base = std::string(buf, n);
	}
	if (!base.empty() && !http::urlAllowed(base)) {
		check(false, "the server to test against must be https or on this computer: " + base);
		base.clear();
	}
	const std::string engineTemp = temp + "\\engine";
	makeDir(engineTemp);
	TestHost host(temp + "\\secret-host");
	std::string engineReport;
	const bool engineOk = clsync::selfTest(c, engineTemp, engineReport, &host, base);
	report += engineReport;
	if (!engineOk) failures++;
	if (!base.empty()) report += "(the single-device scenarios also ran against " + base + ")\n";
	std::error_code ec;
	std::filesystem::remove_all(std::filesystem::path(W(temp)), ec);
	report += failures ? strf("%d sync checks failed\n", failures) : std::string("sync checks: all passed\n");
	return failures == 0;
}

}  // namespace syncplat
