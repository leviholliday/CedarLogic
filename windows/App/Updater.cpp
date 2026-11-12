// Updates, the way the Linux app's AppImage updates, from one of two places
// (Settings > General > Testing group, as the Mac's):
//
//   beta testers    the rolling test build, the "windows-native-testing"
//                   pre-release in leviholliday/CedarLogic (as before there
//                   was a choice)
//   normal testers  the newest versioned release (v0.4, v0.4.1...) in
//                   leviholliday/CedarLogic-Releases, the repo the Mac's
//                   releases are in, that has this CPU's zip
//                   (CedarLogic-<version>-Windows-<x64|ARM64>.zip). Drafts and
//                   pre-releases (the Mac betas) are passed over.
//
// Either way, the same steps:
//
//   1. ask GitHub for the commit the release's tag points at
//   2. compare it with CL_GIT_COMMIT, baked in at build time
//   3. different -> fetch the release's SHA256SUMS and SHA256SUMS.sig and
//      check them (update-signing/UpdateSignature.h): signed with the preview
//      key built in here, for this tag and the tag's commit, a build newer
//      than this one (CL_BUILD_TIME), with this CPU's zip in it. Anything
//      less: no update is offered, and why goes to update.log in the
//      settings folder. A versioned release's SHA256SUMS is
//      packaging/sign-release.sh's: the same format and key, the release's
//      own tag and the Releases repo's commit for it, and as its build the
//      build time of the Windows files in it (their commit's, as the test
//      build's), so a normal release older than the test build someone has
//      is never "newer": switching from beta to normal stays put until a
//      normal release passes it
//   4. verified -> ask; yes -> download that zip, check its size and SHA-256
//      against the manifest before anything else touches it, and unpack it
//      (with the tar that comes with Windows 10 and 11)
//   4. move the running CedarLogic.exe aside (Windows lets a running program
//      be renamed, not replaced), put the new one and its res folder in its
//      place, and offer to restart -- through the app's normal Quit, so
//      unsaved work is asked about first. The old exe is removed at the next
//      launch.
//
// The same for a copy unzipped anywhere and one the installer put in
// %LOCALAPPDATA%\Programs\CedarLogic: only the exe and res beside it change
// (the installer's uninstaller and its notes stay as they are; Settings >
// Apps learns the new version at the next launch, in Integration.cpp).
//
// The network calls run on a thread of their own; only the main thread
// touches windows.

#include "Updater.h"
#include "Alert.h"
#include "Window.h"

#include "UpdateSignature.h"

#include <bcrypt.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <ctime>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#ifndef CL_GIT_COMMIT
#define CL_GIT_COMMIT "unknown"
#endif
// The commit's committer time (Unix seconds): the build number the signed
// manifest's must be above. 0 when it isn't known (a build outside git).
#ifndef CL_BUILD_TIME
#define CL_BUILD_TIME 0
#endif
#ifndef CL_VERSION
#define CL_VERSION "0"
#endif

namespace updater {

namespace {

const char* kOwnerRepo = "leviholliday/CedarLogic";
const char* kTag = "windows-native-testing";
const char* kReleasesRepo = "leviholliday/CedarLogic-Releases";

#if defined(_M_ARM64) || defined(__aarch64__)
const char* kArch = "ARM64";
#else
const char* kArch = "x64";
#endif

std::string exePath() {
	wchar_t buf[MAX_PATH * 4];
	const DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof buf / sizeof buf[0]));
	return U(std::wstring(buf, n));
}

// A GET over HTTPS, following redirects (release downloads go through
// GitHub's storage). Into `out`, or straight into `file` when one is given.
bool httpGet(const std::string& url, std::string* out, const std::string& file, size_t limit) {
	const std::wstring wurl = W(url);
	URL_COMPONENTS u = {};
	u.dwStructSize = sizeof u;
	wchar_t host[256] = L"", path[2048] = L"";
	u.lpszHostName = host;
	u.dwHostNameLength = 255;
	u.lpszUrlPath = path;
	u.dwUrlPathLength = 2047;
	if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &u)) return false;
	HINTERNET session = WinHttpOpen(L"CedarLogic", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
	                                WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session) return false;
	WinHttpSetTimeouts(session, 15000, 15000, 30000, 60000);
	bool ok = false;
	HINTERNET connect = WinHttpConnect(session, host, u.nPort, 0);
	HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", path, nullptr, WINHTTP_NO_REFERER,
	                                                 WINHTTP_DEFAULT_ACCEPT_TYPES,
	                                                 u.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
	                            : nullptr;
	HANDLE f = INVALID_HANDLE_VALUE;
	if (request && WinHttpSendRequest(request, L"Accept: application/vnd.github+json\r\n", (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
	    WinHttpReceiveResponse(request, nullptr)) {
		DWORD status = 0, size = sizeof status;
		WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
		                    &status, &size, WINHTTP_NO_HEADER_INDEX);
		if (status == 200) {
			if (!file.empty())
				f = CreateFileW(W(file).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			ok = file.empty() || f != INVALID_HANDLE_VALUE;
			size_t total = 0;
			std::vector<char> chunk(64 * 1024);
			for (DWORD got = 0; ok;) {
				if (!WinHttpReadData(request, chunk.data(), (DWORD)chunk.size(), &got)) { ok = false; break; }
				if (got == 0) break;
				total += got;
				if (total > limit) { ok = false; break; }
				if (out) out->append(chunk.data(), got);
				DWORD wrote = 0;
				if (f != INVALID_HANDLE_VALUE && (!WriteFile(f, chunk.data(), got, &wrote, nullptr) || wrote != got)) ok = false;
			}
		}
	}
	if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
	if (!ok && !file.empty()) DeleteFileW(W(file).c_str());
	if (request) WinHttpCloseHandle(request);
	if (connect) WinHttpCloseHandle(connect);
	WinHttpCloseHandle(session);
	return ok;
}

// The value of one "key":"value" string field in a JSON document, from
// `from` on (the Linux updater's: enough for GitHub's answers).
bool jsonString(const std::string& json, const std::string& key, std::string& out, size_t from = 0,
                size_t* endOut = nullptr, size_t* startOut = nullptr) {
	const std::string needle = "\"" + key + "\":\"";
	size_t at = json.find(needle, from);
	if (at == std::string::npos) return false;
	at += needle.size();
	if (startOut) *startOut = at;
	std::string raw;
	for (size_t i = at; i < json.size(); i++) {
		if (json[i] == '"') { out.swap(raw); if (endOut) *endOut = i + 1; return true; }
		if (json[i] == '\\' && i + 1 < json.size()) raw += json[++i];
		else raw += json[i];
	}
	return false;
}

// ---- Verifying ------------------------------------------------------------------

// Why a check found no update, or a download was refused: a line in
// update.log in the settings folder (the last 64 KB kept), and the debugger.
void logUpdate(const std::string& what) {
	time_t now = time(nullptr);
	char when[32] = "";
	struct tm t;
	if (gmtime_s(&t, &now) == 0) strftime(when, sizeof when, "%Y-%m-%d %H:%M:%SZ", &t);
	const std::string line = std::string(when) + " " + what + "\r\n";
	OutputDebugStringW(W("CedarLogic update: " + line).c_str());
	const std::wstring path = W(settingsDir() + "\\update.log");
	WIN32_FILE_ATTRIBUTE_DATA a;
	if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a) && (a.nFileSizeHigh || a.nFileSizeLow > 64 * 1024))
		DeleteFileW(path.c_str());
	HANDLE f = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f == INVALID_HANDLE_VALUE) return;
	DWORD wrote = 0;
	WriteFile(f, line.data(), (DWORD)line.size(), &wrote, nullptr);
	CloseHandle(f);
}

// SHA-256 with Windows' own BCrypt, over bytes handed to `feed`.
bool sha256With(const std::function<bool(BCRYPT_HASH_HANDLE)>& feed, std::string& hex) {
	BCRYPT_ALG_HANDLE alg = nullptr;
	BCRYPT_HASH_HANDLE hash = nullptr;
	unsigned char digest[32];
	bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
	          BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0 && feed(hash) &&
	          BCryptFinishHash(hash, digest, sizeof digest, 0) == 0;
	if (hash) BCryptDestroyHash(hash);
	if (alg) BCryptCloseAlgorithmProvider(alg, 0);
	if (ok) hex = updatesig::toHex(digest, sizeof digest);
	return ok;
}

std::string sha256Bytes(const std::string& bytes) {
	std::string hex;
	sha256With([&](BCRYPT_HASH_HANDLE h) {
		return bytes.empty() || BCryptHashData(h, (PUCHAR)bytes.data(), (ULONG)bytes.size(), 0) == 0;
	}, hex);
	return hex;
}

// A file's SHA-256 and size, read through once.
bool sha256File(const std::string& path, std::string& hex, long long& size) {
	HANDLE f = CreateFileW(W(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f == INVALID_HANDLE_VALUE) return false;
	size = 0;
	const bool ok = sha256With([&](BCRYPT_HASH_HANDLE h) {
		std::vector<unsigned char> chunk(64 * 1024);
		DWORD got = 0;
		while (ReadFile(f, chunk.data(), (DWORD)chunk.size(), &got, nullptr)) {
			if (got == 0) return true;
			size += got;
			if (BCryptHashData(h, chunk.data(), got, 0) != 0) return false;
		}
		return false;
	}, hex);
	CloseHandle(f);
	return ok;
}

// A downloaded file against the signed manifest's entry for it.
bool verifyFile(const std::string& path, const updatesig::File& expected, std::string& why) {
	std::string hex;
	long long size = 0;
	if (!sha256File(path, hex, size)) {
		why = "couldn't read " + path;
		return false;
	}
	return updatesig::checkDownload(expected, size, hex, why);
}

// ---- Where updates come from ------------------------------------------------------

// GitHub, or for --channel-check a stand-in on this computer (the same paths).
struct Source {
	std::string api = "https://api.github.com";
	std::string web = "https://github.com";
	const unsigned char* key = updatesig::kPublicKey;
};

// A release to update from, and the file in it this copy takes.
struct Target {
	std::string repo, tag;
	std::string version;     // a versioned release's ("0.4.1"); "" for the test build
	std::string assetName;   // this CPU's zip
};

std::string betaAsset() { return std::string("CedarLogic-Windows-native-") + kArch + ".zip"; }
Target betaTarget() { return { kOwnerRepo, kTag, "", betaAsset() }; }

std::string releaseFileUrl(const Source& src, const Target& t, const std::string& name) {
	return src.web + "/" + t.repo + "/releases/download/" + t.tag + "/" + name;
}

struct Asset {
	std::string url;
	updatesig::File file;
	std::string installed;   // a versioned release's "<tag> <build>" (Prefs::installedRelease), else ""
};

// The release's manifest and signature through `get` (the release's
// download URLs, or a folder in the self-test), and the decision on them.
updatesig::Decision findVerified(const std::function<bool(const std::string& name, std::string& out)>& get, const unsigned char key[32],
                                 const std::string& tag, const std::string& assetName, const std::string& releaseCommit,
                                 const std::string& runningCommit, long long runningBuild) {
	std::string manifest, signature;
	bool haveManifest = false, haveSignature = false;
	if (releaseCommit != runningCommit) {
		haveManifest = get(updatesig::kManifestName, manifest);
		haveSignature = haveManifest && get(updatesig::kSignatureName, signature);
	}
	return updatesig::decide(haveManifest, manifest, haveSignature, signature, key, tag, releaseCommit, runningCommit, runningBuild,
	                         [&assetName](const std::string& name) { return name == assetName; });
}

// ---- GitHub's list of releases -------------------------------------------------------
// A small JSON reader, enough for the releases list (nested objects, arrays,
// escapes); anything it can't read is no list at all.

struct Json {
	enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
	bool boolean = false;
	std::string text;
	std::vector<Json> items;
	std::vector<std::pair<std::string, Json>> fields;
	const Json* get(const char* key) const {
		for (const auto& f : fields)
			if (f.first == key) return &f.second;
		return nullptr;
	}
	bool isTrue(const char* key) const {
		const Json* v = get(key);
		return v && v->kind == Bool && v->boolean;
	}
	std::string str(const char* key) const {
		const Json* v = get(key);
		return v && v->kind == String ? v->text : std::string();
	}
};

struct JsonReader {
	const std::string& t;
	size_t i = 0;
	int depth = 0;
	void space() {
		while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) i++;
	}
	bool word(const char* w) {
		const size_t n = strlen(w);
		if (t.compare(i, n, w) != 0) return false;
		i += n;
		return true;
	}
	static void utf8(std::string& out, unsigned cp) {
		if (cp < 0x80) out += (char)cp;
		else if (cp < 0x800) {
			out += (char)(0xC0 | (cp >> 6));
			out += (char)(0x80 | (cp & 0x3F));
		} else {
			out += (char)(0xE0 | (cp >> 12));
			out += (char)(0x80 | ((cp >> 6) & 0x3F));
			out += (char)(0x80 | (cp & 0x3F));
		}
	}
	bool string(std::string& out) {
		if (i >= t.size() || t[i] != '"') return false;
		i++;
		while (i < t.size()) {
			const char c = t[i++];
			if (c == '"') return true;
			if ((unsigned char)c < 0x20) return false;
			if (c != '\\') {
				out += c;
				continue;
			}
			if (i >= t.size()) return false;
			const char e = t[i++];
			switch (e) {
			case '"': case '\\': case '/': out += e; break;
			case 'b': out += '\b'; break;
			case 'f': out += '\f'; break;
			case 'n': out += '\n'; break;
			case 'r': out += '\r'; break;
			case 't': out += '\t'; break;
			case 'u': {
				if (i + 4 > t.size()) return false;
				unsigned cp = 0;
				for (int k = 0; k < 4; k++) {
					const char h = t[i++];
					cp <<= 4;
					if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
					else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
					else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
					else return false;
				}
				utf8(out, cp);   // (a surrogate half stays as it is: the names read here are plain)
				break;
			}
			default: return false;
			}
		}
		return false;
	}
	bool value(Json& v) {
		space();
		if (i >= t.size() || depth >= 64) return false;
		depth++;
		bool ok = true;
		const char c = t[i];
		if (c == '{' || c == '[') {
			const bool object = c == '{';
			const char close = object ? '}' : ']';
			v.kind = object ? Json::Object : Json::Array;
			i++;
			space();
			if (i < t.size() && t[i] == close) i++;
			else
				for (;;) {
					space();
					std::string key;
					Json item;
					if (object) {
						if (!string(key)) { ok = false; break; }
						space();
						if (i >= t.size() || t[i] != ':') { ok = false; break; }
						i++;
					}
					if (!value(item)) { ok = false; break; }
					if (object) v.fields.emplace_back(std::move(key), std::move(item));
					else v.items.push_back(std::move(item));
					space();
					if (i < t.size() && t[i] == ',') { i++; continue; }
					if (i < t.size() && t[i] == close) { i++; break; }
					ok = false;
					break;
				}
		} else if (c == '"') {
			v.kind = Json::String;
			ok = string(v.text);
		} else if (word("true")) {
			v.kind = Json::Bool;
			v.boolean = true;
		} else if (word("false")) v.kind = Json::Bool;
		else if (word("null")) v.kind = Json::Null;
		else if (c == '-' || (c >= '0' && c <= '9')) {
			v.kind = Json::Number;
			while (i < t.size() && (isdigit((unsigned char)t[i]) || t[i] == '-' || t[i] == '+' || t[i] == '.' || t[i] == 'e' || t[i] == 'E'))
				v.text += t[i++];
		} else ok = false;
		depth--;
		return ok;
	}
};

bool parseJson(const std::string& text, Json& out) {
	JsonReader r{ text };
	if (!r.value(out)) return false;
	r.space();
	return r.i == text.size();
}

// "v0.4.1" -> {0, 4, 1}: a versioned release's tag ("v" and one to four
// numbers); anything else isn't one.
bool versionOf(const std::string& tag, std::vector<long>& parts) {
	parts.clear();
	if (tag.size() < 2 || tag.size() > 40 || tag[0] != 'v') return false;
	size_t i = 1;
	for (;;) {
		size_t j = i;
		while (j < tag.size() && isdigit((unsigned char)tag[j])) j++;
		if (j == i || j - i > 6 || parts.size() >= 4) return false;
		parts.push_back(atol(tag.substr(i, j - i).c_str()));
		if (j == tag.size()) return true;
		if (tag[j] != '.') return false;
		i = j + 1;
	}
}

// 0.4 and 0.4.0 are the same version.
int compareVersions(std::vector<long> a, std::vector<long> b) {
	while (a.size() < b.size()) a.push_back(0);
	while (b.size() < a.size()) b.push_back(0);
	return a < b ? -1 : a > b ? 1 : 0;
}

// The name docs/RELEASE-0.4.md's step 2 gives the test build's zip on a
// versioned release.
std::string normalAsset(const std::string& version) { return "CedarLogic-" + version + "-Windows-" + kArch + ".zip"; }

enum class Pick { Found, None, Unreadable };

// The newest full release (not a draft, not a pre-release) with a versioned
// tag, this CPU's zip and a signed list (SHA256SUMS and its .sig), from
// GitHub's list of the Releases repo's releases: newest by version, not by
// date. (The Linux app's rule, with its files.)
Pick pickNormal(const std::string& json, Target& out) {
	Json list;
	if (!parseJson(json, list) || list.kind != Json::Array) return Pick::Unreadable;
	std::vector<long> best, parts;
	bool found = false;
	for (const Json& r : list.items) {
		if (r.kind != Json::Object) continue;
		const Json* draft = r.get("draft");
		const Json* pre = r.get("prerelease");
		// Only one that says it's neither.
		if (!draft || draft->kind != Json::Bool || draft->boolean || !pre || pre->kind != Json::Bool || pre->boolean) continue;
		const std::string tag = r.str("tag_name");
		if (!versionOf(tag, parts)) continue;
		const std::string asset = normalAsset(tag.substr(1));
		bool has = false, manifest = false, signature = false;
		if (const Json* assets = r.get("assets"))
			for (const Json& a : assets->items) {
				if (a.kind != Json::Object) continue;
				const std::string name = a.str("name");
				has = has || name == asset;
				manifest = manifest || name == updatesig::kManifestName;
				signature = signature || name == updatesig::kSignatureName;
			}
		if (!has || !manifest || !signature) continue;
		if (!found || compareVersions(parts, best) > 0) {
			best = parts;
			out = { kReleasesRepo, tag, tag.substr(1), asset };
			found = true;
		}
	}
	return found ? Pick::Found : Pick::None;
}

// ---- One check ---------------------------------------------------------------------

struct Found {
	bool asked = false;       // GitHub answered (else: no connection)
	bool noRelease = false;   // normal: no versioned release for Windows yet
	Target target;
	std::string commit;       // the commit the release's tag is on
	updatesig::Decision decision;
	Asset asset;              // Offer: what to download
	std::string why;          // for the log
	std::string key;          // what was found, for offering it once: the test build's commit, or "<tag> <build>"
};

// This copy, as the check sees it.
struct Running {
	std::string commit;      // CL_GIT_COMMIT
	long long build = 0;     // CL_BUILD_TIME
	std::string version;     // CL_VERSION
	std::string installed;   // Prefs::installedRelease
};

// What there is for this testing group, checked: the code a real check and
// --channel-check both run.
Found lookFor(const Source& src, bool beta, const Running& me) {
	Found f;
	std::string body;
	if (beta) f.target = betaTarget();
	else {
		if (!httpGet(src.api + "/repos/" + kReleasesRepo + "/releases?per_page=100", &body, "", 16u << 20)) {
			f.why = "couldn't list the releases";
			return f;
		}
		const Pick pick = pickNormal(body, f.target);
		if (pick == Pick::Unreadable) {
			f.why = "the list of releases couldn't be read";
			return f;
		}
		if (pick == Pick::None) {
			f.asked = true;
			f.noRelease = true;
			f.decision.result = updatesig::Result::UpToDate;
			f.why = "no full release in " + std::string(kReleasesRepo) + " has a signed Windows zip for " + kArch + " yet";
			return f;
		}
	}
	body.clear();
	if (httpGet(src.api + "/repos/" + f.target.repo + "/commits/" + f.target.tag, &body, "", 4u << 20))
		jsonString(body, "sha", f.commit);   // the first "sha" is the commit's own
	if (f.commit.empty()) {
		f.why = "couldn't read the commit of " + f.target.tag;
		return f;
	}
	f.asked = true;
	const Target t = f.target;
	auto get = [&src, &t](const std::string& name, std::string& out) {
		if (!updatesig::safeName(name)) return false;
		out.clear();
		return httpGet(releaseFileUrl(src, t, name), &out, "", 64 * 1024) && !out.empty();
	};
	f.decision = findVerified(get, src.key, t.tag, t.assetName, f.commit, me.commit, me.build);
	f.key = beta ? f.commit : t.tag + " " + std::to_string(f.decision.manifest.build);
	if (f.decision.result == updatesig::Result::Offer && !beta) {
		// Never back to an older version than this one (a beta tester's newer
		// test build is refused by its build already), nor the release this
		// updater installed.
		std::vector<long> mine, theirs;
		if (versionOf("v" + me.version, mine) && versionOf(t.tag, theirs) && compareVersions(theirs, mine) < 0) {
			f.decision.result = updatesig::Result::UpToDate;
			f.decision.why = t.tag + " is an older version than this one (" + me.version + "): not going back";
		} else if (f.key == me.installed) {
			f.decision.result = updatesig::Result::UpToDate;
			f.decision.why = t.tag + " (build " + std::to_string(f.decision.manifest.build) + ") is the release installed";
		}
	}
	if (f.decision.result == updatesig::Result::Offer) {
		f.asset.url = releaseFileUrl(src, t, f.decision.file.name);
		f.asset.file = f.decision.file;
		if (!beta) f.asset.installed = f.key;
	}
	f.why = t.tag + ": " + f.decision.why;
	return f;
}

struct Check {
	std::thread worker;
	std::atomic<bool> done{ false };
	bool interactive = false;
	bool beta = false;   // the testing group it looked for
	Running running;     // (filled on the main thread: the worker doesn't touch prefs)
	HWND parent = nullptr;
	Found found;
};

std::unique_ptr<Check> g_check;
std::string g_offered;   // a background check offers each build once
std::string g_status;    // how the last check went, for Settings

void runCheck(Check* c) {
	c->found = lookFor(Source(), c->beta, c->running);
	logUpdate(std::string(c->beta ? "beta: " : "normal: ") + c->found.why);
	c->done.store(true);
}

// Run something on a thread while a small window says so, the app still
// drawing behind it. The window it's over is left as it was found: one a
// dialog had disabled stays disabled (enabled, it could be closed under
// that dialog, whose code is still running).
template <class F>
bool withProgress(HWND parent, const char* text, F&& work) {
	const UINT dpi = dpiOf(parent);
	HWND w = CreateWindowExW(WS_EX_DLGMODALFRAME, L"#32770", L"Updating CedarLogic", WS_POPUP | WS_CAPTION, 0, 0,
	                         scaled(360, dpi), scaled(110, dpi), parent, nullptr, appInstance(), nullptr);
	CreateWindowExW(0, L"STATIC", W(text).c_str(), WS_CHILD | WS_VISIBLE, scaled(16, dpi), scaled(14, dpi), scaled(320, dpi),
	                scaled(20, dpi), w, nullptr, appInstance(), nullptr);
	HWND bar = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE | PBS_MARQUEE, scaled(16, dpi), scaled(42, dpi),
	                           scaled(320, dpi), scaled(14, dpi), w, nullptr, appInstance(), nullptr);
	SendMessageW(bar, PBM_SETMARQUEE, TRUE, 30);
	setFontTree(w, uiFont(dpi));
	setDarkTitleBar(w, prefs().dark);
	RECT pr = {}, wr = {};
	if (parent == nullptr || !GetWindowRect(parent, &pr)) SystemParametersInfoW(SPI_GETWORKAREA, 0, &pr, 0);
	GetWindowRect(w, &wr);
	SetWindowPos(w, nullptr, (pr.left + pr.right - (wr.right - wr.left)) / 2, (pr.top + pr.bottom - (wr.bottom - wr.top)) / 2, 0, 0,
	             SWP_NOSIZE | SWP_NOZORDER);
	ShowWindow(w, SW_SHOW);
	const bool parentWasEnabled = parent && IsWindowEnabled(parent);
	if (parentWasEnabled) EnableWindow(parent, FALSE);
	std::atomic<bool> done{ false };
	bool ok = false;
	std::thread worker([&] { ok = work(); done.store(true); });
	while (!done.load()) {
		MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
		MSG m;
		while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
			if (m.message == WM_QUIT) { PostQuitMessage((int)m.wParam); break; }
			TranslateMessage(&m);
			DispatchMessageW(&m);
		}
	}
	worker.join();
	if (parentWasEnabled) EnableWindow(parent, TRUE);
	DestroyWindow(w);
	if (parentWasEnabled) SetForegroundWindow(parent);
	return ok;
}

// Run a program (by its full path: never one found in the current folder)
// without a window, and wait for it. True when it finished with 0.
bool runHidden(const std::wstring& program, const std::wstring& arguments) {
	STARTUPINFOW si = { sizeof si };
	PROCESS_INFORMATION pi = {};
	std::wstring cmd = L"\"" + program + L"\" " + arguments;
	if (!CreateProcessW(program.c_str(), &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
		return false;
	WaitForSingleObject(pi.hProcess, 120000);
	DWORD code = 1;
	GetExitCodeProcess(pi.hProcess, &code);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return code == 0;
}

bool copyTree(const std::wstring& from, const std::wstring& to) {
	CreateDirectoryW(to.c_str(), nullptr);
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((from + L"\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return false;
	bool ok = true;
	do {
		const std::wstring name = fd.cFileName;
		if (name == L"." || name == L"..") continue;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ok = copyTree(from + L"\\" + name, to + L"\\" + name) && ok;
		else ok = CopyFileW((from + L"\\" + name).c_str(), (to + L"\\" + name).c_str(), FALSE) && ok;
	} while (FindNextFileW(h, &fd));
	FindClose(h);
	return ok;
}

void removeTree(const std::wstring& dir) {
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			const std::wstring name = fd.cFileName;
			if (name == L"." || name == L"..") continue;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) removeTree(dir + L"\\" + name);
			else DeleteFileW((dir + L"\\" + name).c_str());
		} while (FindNextFileW(h, &fd));
		FindClose(h);
	}
	RemoveDirectoryW(dir.c_str());
}

// The unpacked folder that has CedarLogic.exe: the zip's CedarLogic folder,
// else the zip's top, else whichever folder a level down has it.
std::wstring unpacked(const std::wstring& work) {
	auto has = [](const std::wstring& dir) { return GetFileAttributesW((dir + L"\\CedarLogic.exe").c_str()) != INVALID_FILE_ATTRIBUTES; };
	if (has(work + L"\\CedarLogic")) return work + L"\\CedarLogic";
	if (has(work)) return work;
	std::wstring found = work + L"\\CedarLogic";
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((work + L"\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return found;
	do {
		const std::wstring name = fd.cFileName;
		if (name != L"." && name != L".." && (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && has(work + L"\\" + name)) {
			found = work + L"\\" + name;
			break;
		}
	} while (FindNextFileW(h, &fd));
	FindClose(h);
	return found;
}

// The app in the middle of something: a dialog up over a window (its code
// is waiting on that dialog, so the window mustn't close or be asked over),
// the mouse held down, a menu open, a window being moved or sized. Any
// window's dialog counts, not only the one asked over: a question asked
// inside another window's dialog loop holds that loop up, and the dialog,
// finished meanwhile, gives its window back to be closed under its code.
// `window`, the one asked over, must take clicks; when it's a dialog itself
// (Settings' Check Now), the circuit window under it is that dialog's.
bool inTheMiddle(HWND window) {
	if (window && !IsWindowEnabled(window)) return true;
	const HWND under = window ? GetAncestor(window, GA_ROOTOWNER) : nullptr;
	for (CircuitWindow* w : circuitWindows())
		if (w->window() != under && !IsWindowEnabled(w->window())) return true;
	GUITHREADINFO gui = { sizeof gui };
	return GetGUIThreadInfo(GetCurrentThreadId(), &gui) &&
	       (gui.hwndCapture || (gui.flags & (GUI_INMENUMODE | GUI_POPUPMENUMODE | GUI_INMOVESIZE)));
}

// "Restart Now": once nothing is in the middle of something (quitting
// can't happen under a dialog, whose code is waiting on it), the windows
// close as Quit closes them and the new exe starts.
std::wstring g_restartExe;

void CALLBACK restartTimer(HWND, UINT, UINT_PTR id, DWORD) {
	if (inTheMiddle(nullptr)) return;   // once it's over
	KillTimer(nullptr, id);
	if (!quitApp()) return;
	STARTUPINFOW si = { sizeof si };
	PROCESS_INFORMATION pi = {};
	std::wstring cmd = L"\"" + g_restartExe + L"\"";
	if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
	}
}

void install(HWND parent, const Asset& asset) {
	const std::string exe = exePath();
	const std::string dir = dirName(exe);
	wchar_t tmp[MAX_PATH];
	GetTempPathW(MAX_PATH, tmp);
	const std::wstring work = std::wstring(tmp) + L"CedarLogic-update-" + std::to_wstring(GetCurrentProcessId());
	const std::wstring zip = work + L".zip";
	removeTree(work);
	CreateDirectoryW(work.c_str(), nullptr);
	// Windows' own tar (Windows 10 1803 and later), from System32.
	wchar_t sys[MAX_PATH] = L"";
	const UINT sysLength = GetSystemDirectoryW(sys, MAX_PATH);
	const std::wstring tar = sysLength > 0 && sysLength < MAX_PATH ? std::wstring(sys) + L"\\tar.exe" : std::wstring();
	if (tar.empty() || GetFileAttributesW(tar.c_str()) == INVALID_FILE_ATTRIBUTES) {
		removeTree(work);
		showMessage(parent, Tone::Warning, "This copy of Windows can't unpack the update",
		            "Updating needs Windows 10 version 1803 or later. Download the new build from the release page instead.");
		return;
	}
	// Nothing is unpacked, let alone run, before the zip is the one the
	// signed manifest names: its size and SHA-256.
	bool verified = false;
	std::string why;
	const bool got = withProgress(parent, "Downloading the update…", [&] {
		if (!httpGet(asset.url, nullptr, U(zip), 512u << 20)) return false;
		if (!verifyFile(U(zip), asset.file, why)) return false;
		verified = true;
		return runHidden(tar, L"-xf \"" + zip + L"\" -C \"" + work + L"\"");
	});
	DeleteFileW(zip.c_str());
	if (!verified && !why.empty()) {
		removeTree(work);
		logUpdate("the download didn't verify, not installing it: " + why);
		showMessage(parent, Tone::Warning, "The update didn't pass its check",
		            "The downloaded file isn't the one the signed list of this build names, so it wasn't installed. (" + why + ")");
		return;
	}
	const std::wstring fresh = unpacked(work);
	if (!got || GetFileAttributesW((fresh + L"\\CedarLogic.exe").c_str()) == INVALID_FILE_ATTRIBUTES) {
		removeTree(work);
		showMessage(parent, Tone::Warning, "The update couldn't be downloaded", "Check your connection and try again.");
		return;
	}
	// The running exe moves aside; the new one takes its place.
	const std::wstring wexe = W(exe), old = wexe + L".old";
	DeleteFileW(old.c_str());
	if (!MoveFileExW(wexe.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
		removeTree(work);
		showMessage(parent, Tone::Warning, "The update was downloaded but couldn't be put in place",
		            "Check that you can change the files in " + dir + ".");
		return;
	}
	if (!CopyFileW((fresh + L"\\CedarLogic.exe").c_str(), wexe.c_str(), FALSE)) {
		MoveFileExW(old.c_str(), wexe.c_str(), MOVEFILE_REPLACE_EXISTING);   // put it back
		removeTree(work);
		showMessage(parent, Tone::Warning, "The update couldn't be put in place", "Check that you can change the files in " + dir + ".");
		return;
	}
	// The res folder beside it (the gates, help and samples): copied whole
	// to res.new, then swapped in, so a file that can't be replaced (in use,
	// read-only, a full disk) leaves the old exe and res, not a mix.
	const std::wstring res = W(dir) + L"\\res", resNew = res + L".new", resOld = res + L".old";
	removeTree(resNew);
	removeTree(resOld);
	const bool hadRes = GetFileAttributesW(res.c_str()) != INVALID_FILE_ATTRIBUTES;
	const bool staged = copyTree(fresh + L"\\res", resNew);
	const bool movedOld = staged && (!hadRes || MoveFileExW(res.c_str(), resOld.c_str(), 0));
	if (!(movedOld && MoveFileExW(resNew.c_str(), res.c_str(), 0))) {
		if (movedOld && hadRes) MoveFileExW(resOld.c_str(), res.c_str(), 0);
		removeTree(resNew);
		MoveFileExW(old.c_str(), wexe.c_str(), MOVEFILE_REPLACE_EXISTING);   // the old exe back
		removeTree(work);
		showMessage(parent, Tone::Warning, "The update couldn't be put in place",
		            "Some of CedarLogic's files in " + dir + "\\res couldn't be replaced. Close anything using them and try again.");
		return;
	}
	removeTree(resOld);
	removeTree(work);
	// A versioned release, once installed: never offered again.
	if (!asset.installed.empty()) {
		prefs().installedRelease = asset.installed;
		prefs().save();
	}
	if (askConfirm(parent, "The update is installed", "Restart CedarLogic now to use it? Your circuits are saved.", "Restart Now", "Later")) {
		// Asked from Settings' Check Now: Settings closes first (its loop
		// ends before the restart's turn comes).
		bool fromWindow = false;
		for (CircuitWindow* w : circuitWindows()) fromWindow = fromWindow || w->window() == parent;
		wchar_t cls[16] = L"";
		if (parent && IsWindow(parent) && !fromWindow && GetClassNameW(parent, cls, 16) && lstrcmpW(cls, L"#32770") == 0)
			PostMessageW(parent, WM_COMMAND, IDCANCEL, 0);
		g_restartExe = wexe;
		SetTimer(nullptr, 0, 100, restartTimer);
	}
}

// The window a check's answer goes over: the one Check Now was asked from
// while it's there, else a circuit window with no dialog up.
// ---- A message from CedarLogic (CedarMessage.h) ---------------------------------
// After a background check finds CedarLogic up to date: message.json from the releases repo, next
// to the releases, and the engine says whether one is due today: the form, Remind Me Later (again in
// a few days) or No Thanks, kept in Prefs::shownMessages; it waits for any dialog or menu to close first.

struct MessageFetch {
	std::thread worker;
	std::atomic<bool> done{ false };
	std::string body;
};
std::unique_ptr<MessageFetch> g_message;

void showMessageIfDue(HWND parent, const std::string& body) {
	SYSTEMTIME now;
	GetLocalTime(&now);
	const std::string today = strf("%04d-%02d-%02d", now.wYear, now.wMonth, now.wDay);
	CLAppMessage* m = cl_message_pick(body.data(), body.size(), today.c_str(), "windows", CL_VERSION, prefs().shownMessages.c_str());
	if (!m) return;
	// The Mac's three answers, on the app's own card: the form, Remind Me Later (Escape, or closing
	// it), and No Thanks, apart on the left.
	Alert a;
	a.title = *m->eyebrow ? m->eyebrow : m->title;
	a.heading = m->title;
	a.text = std::string(m->text) + (*m->note ? std::string("\n\n") + m->note : std::string());
	a.buttons = { { m->button, 1, 1 }, { "Remind Me Later", 2 }, { "No Thanks", 3, 0, true } };
	a.enter = 1;
	a.escape = 2;
	const int answer = runAlert(parent, a);
	if (char* kept = cl_message_remember(prefs().shownMessages.c_str(), m->id, today.c_str(), answer == 2 ? m->later : 0)) {
		prefs().shownMessages = kept;
		free(kept);
		prefs().save();
	}
	const std::string link = m->link;
	cl_message_free(m);
	if (answer == 1) openExternally(parent, link);
}

void CALLBACK messageTimer(HWND, UINT, UINT_PTR id, DWORD) {
	if (!g_message) { KillTimer(nullptr, id); return; }
	if (!g_message->done.load()) return;
	HWND parent = nullptr;
	for (CircuitWindow* w : circuitWindows())
		if (IsWindowEnabled(w->window())) { parent = w->window(); break; }
	if (inTheMiddle(parent)) return;
	KillTimer(nullptr, id);
	g_message->worker.join();
	const std::string body = std::move(g_message->body);
	g_message.reset();
	if (!body.empty() && parent) guarded("a message from CedarLogic", [parent, &body] { showMessageIfDue(parent, body); });
}

void checkMessage() {
	if (g_message) return;
	g_message.reset(new MessageFetch());
	MessageFetch* f = g_message.get();
	f->worker = std::thread([f] {
		std::string body;
		if (httpGet(CL_MESSAGE_URL, &body, std::string(), 16 * 1024)) f->body = std::move(body);
		f->done.store(true);
	});
	SetTimer(nullptr, 0, 200, messageTimer);
}

HWND answerParent(const Check* c) {
	if (c->parent && IsWindow(c->parent)) return c->parent;
	for (CircuitWindow* w : circuitWindows())
		if (IsWindowEnabled(w->window())) return w->window();
	return circuitWindows().empty() ? nullptr : circuitWindows().front()->window();
}

void begin(HWND parent, bool interactive);

// Said once to a copy that came from the test builds (Prefs::loadFrom).
const char* kNotice = "This copy came from the test builds, so it's a beta tester and goes on getting them. Choose Normal tester to get "
                      "only versions that are settled.";

std::string shortCommit(const std::string& c) { return c.substr(0, 7); }

// How a check went, in a sentence (Settings shows the last one).
std::string statusOf(const Found& f, bool beta) {
	const std::string group = beta ? "Beta tester" : "Normal tester";
	if (!f.asked) return "Last check: couldn't reach GitHub.";
	if (f.noRelease) return "Last check: there's no normal release for Windows yet. Beta testers get the test builds now.";
	const std::string what = beta ? "test build " + shortCommit(f.commit) : "CedarLogic " + f.target.version;
	switch (f.decision.result) {
	case updatesig::Result::Offer: return "Last check (" + group + "): " + what + " is ready to install.";
	case updatesig::Result::UpToDate:
		if (!beta && f.decision.manifest.build > 0 && f.decision.manifest.build < (long long)CL_BUILD_TIME)
			return "Last check (" + group + "): this copy is newer than " + what + ", the newest normal release. It updates once a newer one is out.";
		return "Last check (" + group + "): up to date.";
	default: return "Last check (" + group + "): " + what + " isn't signed yet, so it isn't offered.";
	}
}

void finish(HWND parent) {
	Check* c = g_check.get();
	const bool interactive = c->interactive;
	const bool beta = c->beta;
	const Found found = c->found;
	const HWND asker = c->parent;
	g_check->worker.join();
	g_check.reset();
	// The testing group changed while it looked: look again, for the new one.
	if (beta != prefs().betaTester) {
		begin(interactive ? asker : nullptr, interactive);
		return;
	}
	g_status = statusOf(found, beta);
	// A copy from the test builds, made a beta tester: said once, here too
	// (and in Settings > General, and update.log).
	if (prefs().testingGroupNotice) {
		noticeSeen();
		for (CircuitWindow* w : circuitWindows()) {
			w->note("Updates: you're a beta tester, as before (Settings > General)");
			break;
		}
	}
	const Asset asset = found.asset;
	const updatesig::Result result = found.decision.result;
	if (!found.asked) {
		if (interactive) showMessage(parent, Tone::Warning, "Couldn't check for updates", "Check your connection and try again.");
		return;
	}
	if (found.noRelease) {
		if (interactive)
			showMessage(parent, Tone::Info, "No normal release for Windows yet",
			            "Normal testers get new versions once they're settled, and there isn't one for Windows yet. Beta testers get the "
			            "test builds now: choose Beta tester in Settings > General to get them.");
		return;
	}
	if (result == updatesig::Result::UpToDate) {
		if (interactive) {
			if (!beta && found.decision.manifest.build > 0 && found.decision.manifest.build < (long long)CL_BUILD_TIME)
				showMessage(parent, Tone::Info, "CedarLogic is up to date",
				            "This copy is newer than CedarLogic " + found.target.version +
				                ", the newest normal release, so it stays as it is until a newer one is out.");
			else showMessage(parent, Tone::Info, "CedarLogic is up to date", "You have the newest version.");
		} else {
			checkMessage();   // up to date: a message from CedarLogic, if one is due
		}
		return;
	}
	// A new build that isn't signed yet, or one that doesn't verify
	// (update.log says which): never offered.
	if (result != updatesig::Result::Offer || asset.url.empty()) {
		if (interactive)
			showMessage(parent, Tone::Info, "No verified update yet",
			            beta ? "There's a newer test build, but it hasn't been signed yet, so CedarLogic won't install it. Try again later."
			                 : "There's a newer version, but it hasn't been signed yet, so CedarLogic won't install it. Try again later.");
		return;
	}
	if (!interactive && g_offered == found.key) return;
	g_offered = found.key;
	const std::string text =
	    beta ? strf("Build %s is out (you have %s). Download and install it now?", shortCommit(found.commit).c_str(),
	                shortCommit(CL_GIT_COMMIT).c_str())
	         : strf("CedarLogic %s is out (you have build %s). Download and install it now?", found.target.version.c_str(),
	                shortCommit(CL_GIT_COMMIT).c_str());
	if (askConfirm(parent, "A new version of CedarLogic is ready", text, "Install", "Not Now")) install(parent, asset);
}

void CALLBACK pollTimer(HWND, UINT, UINT_PTR id, DWORD) {
	if (!g_check) { KillTimer(nullptr, id); return; }
	if (!g_check->done.load()) return;
	// This timer is the thread's, so it fires inside a dialog's loop too
	// (gate settings, Settings, Export...). The answer waits till that's
	// over, and tries again 200 ms on: Settings' Check Now is answered over
	// Settings, inside its loop; any other dialog, in any window, waits.
	const HWND parent = answerParent(g_check.get());
	if (inTheMiddle(parent)) return;
	KillTimer(nullptr, id);
	guarded("checking for updates", [parent] { finish(parent); });
}

void begin(HWND parent, bool interactive) {
	// One at a time: Check for Updates while one runs makes that one say
	// how it went (finish() reads these on this thread; the worker doesn't).
	if (g_check) {
		if (interactive) {
			g_check->interactive = true;
			g_check->parent = parent;
		}
		return;
	}
	g_check.reset(new Check());
	g_check->interactive = interactive;
	g_check->parent = parent;
	g_check->beta = prefs().betaTester;   // (read here: the worker doesn't touch prefs)
	g_check->running = { CL_GIT_COMMIT, (long long)CL_BUILD_TIME, CL_VERSION, prefs().installedRelease };
	Check* c = g_check.get();
	c->worker = std::thread([c] { runCheck(c); });
	SetTimer(nullptr, 0, 200, pollTimer);
}

// Unless Settings > General says not to (Check Now still asks).
void CALLBACK firstCheck(HWND, UINT, UINT_PTR id, DWORD) {
	KillTimer(nullptr, id);
	if (prefs().checkUpdates) begin(nullptr, false);
}

void CALLBACK laterCheck(HWND, UINT, UINT_PTR, DWORD) {
	if (prefs().checkUpdates) begin(nullptr, false);
}

// ---- Self-test -------------------------------------------------------------------

std::string joinPath(const std::string& dir, const std::string& name) { return dir + "\\" + name; }

bool readWhole(const std::string& path, std::string& bytes) {
	HANDLE f = CreateFileW(W(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f == INVALID_HANDLE_VALUE) return false;
	bytes.clear();
	char chunk[65536];
	DWORD got = 0;
	bool ok = true;
	while ((ok = ReadFile(f, chunk, sizeof chunk, &got, nullptr) != 0) && got > 0) bytes.append(chunk, got);
	CloseHandle(f);
	return ok;
}

bool writeWhole(const std::string& path, const std::string& bytes) {
	HANDLE f = CreateFileW(W(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f == INVALID_HANDLE_VALUE) return false;
	DWORD wrote = 0;
	const bool ok = bytes.empty() || (WriteFile(f, bytes.data(), (DWORD)bytes.size(), &wrote, nullptr) && wrote == bytes.size());
	CloseHandle(f);
	return ok;
}

updatesig::SelfTestHooks selfTestHooks() {
	updatesig::SelfTestHooks h;
	h.tag = kTag;
	h.assetName = betaAsset();
	h.makeDir = [](const std::string& d) { return CreateDirectoryW(W(d).c_str(), nullptr) != 0; };
	h.writeFile = writeWhole;
	h.sha256Hex = sha256Bytes;
	h.join = joinPath;
	h.find = [](const std::string& dir, const unsigned char key[32], const std::string& releaseCommit, const std::string& runningCommit,
	            long long runningBuild) {
		auto fromDir = [&dir](const std::string& name, std::string& out) { return readWhole(joinPath(dir, name), out); };
		return findVerified(fromDir, key, kTag, betaAsset(), releaseCommit, runningCommit, runningBuild);
	};
	h.verifyFile = verifyFile;
	return h;
}

// ---- --channel-check ---------------------------------------------------------------
// The testing groups against a stand-in for GitHub (windows/Tools/
// update-channel-server.py serves a folder at GitHub's paths): this writes
// fake releases into that folder, signed with a key made for the test, and
// puts each case through lookFor(), the code a real check runs, over HTTP.

const unsigned char kTestSeed[32] = { 7, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5, 8, 9, 7, 9, 3, 2, 3, 8, 4, 6, 2, 6, 4, 3, 3, 8, 3, 2, 7, 9, 5 };
const unsigned char kOtherSeed[32] = { 9, 9, 9, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 1 };

struct FakeRelease {
	std::string repo, tag;
	bool prerelease = false, draft = false;
	bool windows = true;   // has the Windows zips (else only a Mac file)
	long long build = 0;   // the signed manifest's build; 0: not signed
	enum Signing { Good, OtherKey, Edited, OtherTag } signing = Good;
};

FakeRelease normalRelease(const std::string& tag, long long build) { return { kReleasesRepo, tag, false, false, true, build }; }
FakeRelease testRelease(long long build) { return { kOwnerRepo, kTag, true, false, true, build }; }

bool makeDirs(const std::string& path) {
	for (size_t at = path.find('\\', 3); ; at = path.find('\\', at + 1)) {
		CreateDirectoryW(W(path.substr(0, at)).c_str(), nullptr);
		if (at == std::string::npos) break;
	}
	return GetFileAttributesW(W(path).c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::string jsonQuote(const std::string& s) {
	std::string out = "\"";
	for (char c : s) {
		if (c == '"' || c == '\\') out += '\\';
		out += c;
	}
	return out + "\"";
}

struct Fixture {
	std::string root;
	bool ok = true;
	unsigned char publicKey[32] = {};
	void put(const std::string& rel, const std::string& bytes) {
		std::string path = root;
		for (char c : "\\" + rel) path += c == '/' ? '\\' : c;
		ok = makeDirs(dirName(path)) && writeWhole(path, bytes) && ok;
	}
	static std::string commitOf(const FakeRelease& r) {
		// A commit per release, as GitHub would say (40 hex digits).
		const std::string h = sha256Bytes(r.repo + " " + r.tag + " " + std::to_string(r.build));
		return h.substr(0, 40);
	}
	static std::vector<std::string> names(const FakeRelease& r) {
		std::vector<std::string> n;
		if (r.repo == kOwnerRepo)
			n = { "CedarLogic-Windows-Setup-ARM64.exe", "CedarLogic-Windows-Setup-x64.exe", "CedarLogic-Windows-native-ARM64.zip",
			      "CedarLogic-Windows-native-x64.zip" };
		else {
			const std::string v = r.tag.substr(1);
			n.push_back("CedarLogic-" + v + "-Mac.dmg");
			if (r.windows) {
				n.push_back("CedarLogic-" + v + "-Windows-ARM64.zip");
				n.push_back("CedarLogic-" + v + "-Windows-x64.zip");
				n.push_back("CedarLogic-" + v + "-Windows-Setup-x64.exe");
			}
		}
		return n;
	}
	static std::string bytesOf(const FakeRelease& r, const std::string& name) { return "fake " + name + " of " + r.tag + "\n"; }

	// Everything GitHub would serve for these releases, in place of what was there.
	void write(const std::vector<FakeRelease>& releases, const std::string& listOverride = std::string()) {
		removeTree(W(root));
		ok = makeDirs(root);
		std::string list = "[";
		int id = 100;
		for (const FakeRelease& r : releases) {
			const std::string commit = commitOf(r);
			put("repos/" + r.repo + "/commits/" + r.tag,
			    "{\"sha\":\"" + commit + "\",\"node_id\":\"C_x\",\"commit\":{\"author\":{\"name\":\"someone\"},\"tree\":{\"sha\":\"" +
			        std::string(40, 'e') + "\"}}}");
			std::vector<std::string> files = names(r);
			updatesig::Manifest m;
			m.tag = r.signing == FakeRelease::OtherTag ? "v0.1" : r.tag;
			m.commit = commit;
			m.build = r.build;
			for (const std::string& n : files) {
				const std::string b = bytesOf(r, n);
				put(r.repo + "/releases/download/" + r.tag + "/" + n, b);
				m.files.push_back({ n, (long long)b.size(), sha256Bytes(b) });
			}
			if (r.build > 0) {
				std::string text = updatesig::formatManifest(m);
				unsigned char pub[32];
				const std::string sig = updatesig::testSign(text, r.signing == FakeRelease::OtherKey ? kOtherSeed : kTestSeed, pub);
				if (r.signing == FakeRelease::Edited) {
					m.build += 100000;
					text = updatesig::formatManifest(m);
				}
				put(r.repo + "/releases/download/" + r.tag + "/SHA256SUMS", text);
				put(r.repo + "/releases/download/" + r.tag + "/SHA256SUMS.sig", sig);
				files.push_back("SHA256SUMS");
				files.push_back("SHA256SUMS.sig");
			}
			if (r.repo != kReleasesRepo) continue;
			// As GitHub lists it: other fields around, objects inside, notes
			// that look like JSON.
			if (list.size() > 1) list += ",";
			list += strf("{\"url\":\"https://api.github.com/repos/x/releases/%d\",\"id\":%d,\"author\":{\"login\":\"someone\",\"id\":1,"
			             "\"type\":\"User\",\"site_admin\":false},\"node_id\":\"RE_x\",\"tag_name\":%s,\"target_commitish\":\"main\","
			             "\"name\":\"CedarLogic %s \\u00e9\",\"draft\":%s,\"immutable\":false,\"prerelease\":%s,"
			             "\"created_at\":\"2026-10-31T00:00:00Z\",\"assets\":[",
			             id, id, jsonQuote(r.tag).c_str(), r.tag.c_str(), r.draft ? "true" : "false", r.prerelease ? "true" : "false");
			id++;
			for (size_t k = 0; k < files.size(); k++)
				list += std::string(k ? "," : "") + "{\"id\":" + std::to_string(k + 1) + ",\"name\":" + jsonQuote(files[k]) +
				        ",\"label\":null,\"uploader\":{\"login\":\"someone\"},\"size\":12,\"download_count\":0}";
			list += "],\"tarball_url\":null,\"body\":\"Notes: \\\"prerelease\\\":false, \\\"tag_name\\\":\\\"v9.9\\\" [x] {y}\\nline two\"}";
		}
		list += "]";
		put("repos/" + std::string(kReleasesRepo) + "/releases", listOverride.empty() ? list : listOverride);
	}
};

}  // namespace

bool runChannelCheck(std::string& report, const std::string& server, const std::string& root) {
	bool all = true;
	auto expect = [&](bool ok, const std::string& name, const std::string& detail = std::string()) {
		report += std::string(ok ? "  ok    " : "  FAIL  ") + name + (ok || detail.empty() ? "" : ": " + detail) + "\n";
		all = all && ok;
	};
	if (server.empty() || root.empty()) {
		report += "--channel-check needs --server <url> and --root <the folder it serves>\n";
		return false;
	}
	Fixture fx;
	fx.root = root;
	while (fx.root.size() > 3 && (fx.root.back() == '\\' || fx.root.back() == '/')) fx.root.pop_back();
	updatesig::testSign("", kTestSeed, fx.publicKey);
	Source src;
	src.api = src.web = server;
	src.key = fx.publicKey;
	// This copy: never on a release's commit, version 0.4.0 (CL_VERSION's
	// form), at `build`.
	auto me = [](long long build, const std::string& version = "0.4.0", const std::string& installed = "") {
		return Running{ std::string(40, '1'), build, version, installed };
	};
	const char* names[] = { "Offer", "UpToDate", "NotVerified" };
	auto resultName = [&](const Found& f) { return std::string(names[(int)f.decision.result]) + " (" + f.why + ")"; };

	// One case: what the check found, and for an offer, the file downloaded
	// and checked as an install would.
	auto check = [&](const std::string& name, bool beta, const Running& running, updatesig::Result want, const std::string& wantTag) {
		const Found f = lookFor(src, beta, running);
		bool ok = f.asked && !f.noRelease && f.decision.result == want && (wantTag.empty() || f.target.tag == wantTag);
		std::string detail = resultName(f) + ", from " + f.target.tag;
		if (ok && want == updatesig::Result::Offer) {
			const std::string wantName = beta ? betaAsset() : normalAsset(wantTag.substr(1));
			const std::string file = fx.root + "\\download.zip";
			std::string why;
			ok = f.asset.file.name == wantName && httpGet(f.asset.url, nullptr, file, 1 << 20) && verifyFile(file, f.asset.file, why);
			if (!ok) detail = "the offered " + f.asset.file.name + " (wanted " + wantName + ") didn't download and verify: " + why;
			DeleteFileW(W(file).c_str());
		}
		expect(ok && fx.ok, name, fx.ok ? detail : "couldn't write the fake releases");
		return f;
	};

	report += "Versions\n";
	std::vector<long> a, b;
	expect(versionOf("v0.4.1", a) && a == std::vector<long>{ 0, 4, 1 }, "v0.4.1 is a version");
	expect(!versionOf(kTag, a) && !versionOf("v4.0.0-beta.1", a) && !versionOf("v0.4.", a) && !versionOf("0.4", a) && !versionOf("v", a),
	       "test-build and other tags aren't");
	expect(versionOf("v0.4", a) && versionOf("v0.4.0", b) && compareVersions(a, b) == 0, "0.4 is 0.4.0");
	expect(versionOf("v0.4.10", a) && versionOf("v0.4.9", b) && compareVersions(a, b) > 0, "0.4.10 is after 0.4.9");

	// What the Releases repo has today (Mac betas as pre-releases, a classic
	// wx pre-release, Mac-only full releases), with Windows releases added.
	const std::vector<FakeRelease> today = {
		{ kReleasesRepo, "v0.3.19", true, false, false, 0 },
		{ kReleasesRepo, "v0.3.4", false, false, false, 0 },
		{ kReleasesRepo, "v4.0.2", true, false, false, 0 },
	};
	std::vector<FakeRelease> releases = today;
	releases.push_back(testRelease(3100));
	report += "Normal testers, before there's a Windows release\n";
	fx.write(releases);
	{
		const Found f = lookFor(src, false, me(2000));
		expect(f.asked && f.noRelease && f.decision.result == updatesig::Result::UpToDate, "no Windows release yet: nothing offered, and says so",
		       resultName(f));
	}

	report += "Normal ignores pre-releases, beta takes them\n";
	FakeRelease pre = normalRelease("v0.5", 3000);
	pre.prerelease = true;
	FakeRelease draft = normalRelease("v0.6", 3500);
	draft.draft = true;
	FakeRelease wxPre = normalRelease("v4.0.3", 5000);
	wxPre.prerelease = true;
	FakeRelease macOnly = normalRelease("v0.4.11", 0);
	macOnly.windows = false;
	for (const FakeRelease& r : { pre, draft, wxPre, macOnly, normalRelease("v0.4.9", 2400), normalRelease("v0.4.10", 2600),
	                              normalRelease("v0.4", 2200) })
		releases.push_back(r);
	fx.write(releases);
	check("normal: the newest full release with Windows files (v0.4.10), not the pre-releases, the draft or a Mac-only one", false, me(2000),
	      updatesig::Result::Offer, "v0.4.10");
	check("beta: the test build, a pre-release", true, me(2000), updatesig::Result::Offer, kTag);
	check("normal: the release this copy is (same build): up to date", false, me(2600), updatesig::Result::UpToDate, "v0.4.10");
	check("beta: the test build this copy is: up to date", true, me(3100), updatesig::Result::UpToDate, kTag);

	report += "Switching from beta to normal doesn't go back\n";
	{
		const Found f = check("normal, on the test build (newer than v0.4.10): stays", false, me(3100), updatesig::Result::UpToDate, "v0.4.10");
		expect(f.decision.manifest.build == 2600, "it saw v0.4.10's build and kept this one", resultName(f));
	}
	releases.push_back(normalRelease("v0.5.0", 3200));
	fx.write(releases);
	check("normal, on the test build: takes v0.5.0 once it's newer", false, me(3100), updatesig::Result::Offer, "v0.5.0");
	{
		const Found f = lookFor(src, false, me(3100));
		expect(f.asset.installed == "v0.5.0 3200", "what an install of it remembers is \"v0.5.0 3200\"", f.asset.installed);
	}
	check("normal: never an older version than this one (0.5.1)", false, me(1000, "0.5.1"), updatesig::Result::UpToDate, "v0.5.0");
	check("normal: never the release this updater installed", false, me(1000, "0.4.0", "v0.5.0 3200"), updatesig::Result::UpToDate, "v0.5.0");
	check("normal: a re-signed release with a newer build isn't the one installed", false, me(1000, "0.4.0", "v0.5.0 3000"),
	      updatesig::Result::Offer, "v0.5.0");

	report += "Bad signatures are refused\n";
	auto withNewest = [&](FakeRelease r) {
		std::vector<FakeRelease> v = releases;
		v.push_back(r);
		fx.write(v);
	};
	FakeRelease bad = normalRelease("v0.5.1", 4000);
	bad.signing = FakeRelease::OtherKey;
	withNewest(bad);
	check("normal: signed with another key", false, me(3100), updatesig::Result::NotVerified, "v0.5.1");
	bad.signing = FakeRelease::Edited;
	withNewest(bad);
	check("normal: the manifest changed after signing", false, me(3100), updatesig::Result::NotVerified, "v0.5.1");
	bad.signing = FakeRelease::OtherTag;
	withNewest(bad);
	check("normal: another release's signed manifest", false, me(3100), updatesig::Result::NotVerified, "v0.5.1");
	withNewest(normalRelease("v0.5.1", 0));
	check("normal: a newer release not signed yet is passed over (v0.5.0 is the newest signed)", false, me(3100), updatesig::Result::Offer,
	      "v0.5.0");
	{
		std::vector<FakeRelease> v = today;
		FakeRelease t = testRelease(4000);
		t.signing = FakeRelease::OtherKey;
		v.push_back(t);
		fx.write(v);
		check("beta: signed with another key", true, me(3100), updatesig::Result::NotVerified, kTag);
	}
	{
		fx.write(releases);
		const Found f = lookFor(src, false, me(3100));
		const Target t = f.target;
		fx.put(t.repo + "/releases/download/" + t.tag + "/" + t.assetName, "not the signed file\n");
		const std::string file = fx.root + "\\download.zip";
		std::string why;
		const bool got = f.decision.result == updatesig::Result::Offer && httpGet(f.asset.url, nullptr, file, 1 << 20);
		expect(got && !verifyFile(file, f.asset.file, why), "normal: a download that isn't the signed file is refused", why);
		DeleteFileW(W(file).c_str());
	}

	report += "When GitHub can't be read\n";
	fx.write(releases, "<html>rate limited</html>");
	expect(!lookFor(src, false, me(2000)).asked, "a list that isn't JSON: no answer (not \"up to date\")");
	{
		Source nowhere = src;
		nowhere.api = nowhere.web = "http://127.0.0.1:9";
		expect(!lookFor(nowhere, false, me(2000)).asked && !lookFor(nowhere, true, me(2000)).asked, "no server: no answer");
	}

	report += "The testing group for each kind of install\n";
	{
		const std::string dir = fx.root + "\\prefs";
		makeDirs(dir);
		auto loaded = [&](const std::string& name, const std::string* text, const std::string& marker = std::string()) {
			const std::string path = dir + "\\" + name + ".ini";
			if (text) writeWhole(path, *text);
			Prefs p;
			p.loadFrom(path, &marker);
			return p;
		};
		const Prefs fresh = loaded("none", nullptr);
		expect(!fresh.betaTester && !fresh.testingGroupNotice, "a new install (no settings): normal tester");
		const std::string old = "[CedarLogic]\nthemeMode=1\naccent=6\nbrandAccentSet=1\ncheckUpdates=true\nhasSeenWelcome=true\n";
		Prefs moved = loaded("old", &old);
		expect(moved.betaTester && moved.testingGroupNotice, "settings from a test build (no testing group): beta tester, said once");
		std::string saved = moved.text();
		const Prefs again = loaded("old-saved", &saved);
		expect(again.betaTester && again.testingGroupNotice, "saved before it was said: still beta, still to say");
		moved.testingGroupNotice = false;
		saved = moved.text();
		const Prefs said = loaded("old-said", &saved);
		expect(said.betaTester && !said.testingGroupNotice, "once said: beta, not said again");
		saved = fresh.text();
		const Prefs next = loaded("new-saved", &saved);
		expect(!next.betaTester && !next.testingGroupNotice, "a new install's next launch: still normal");
		const std::string chose = old + "testingGroup=normal\ntestingGroupNotice=true\n";
		const Prefs normal = loaded("chose-normal", &chose);
		expect(!normal.betaTester && !normal.testingGroupNotice, "chose Normal tester: stays normal");
		// What installed it said (install-channel), before the guesses.
		expect(loaded("marker-new-beta", nullptr, "beta").betaTester, "a new install marked beta (the test build's installer): beta");
		const Prefs markedNormal = loaded("marker-old-normal", &old, "normal");
		expect(!markedNormal.betaTester && !markedNormal.testingGroupNotice,
		       "settings from a test build, then a release installed over it (marked normal): normal");
		const Prefs markedBeta = loaded("marker-old-beta", &old, "beta");
		expect(markedBeta.betaTester && !markedBeta.testingGroupNotice, "marked beta: beta, nothing to say");
		expect(!loaded("marker-chosen", &chose, "beta").betaTester, "a group chosen in Settings beats the marker");
		expect(!loaded("marker-junk", nullptr, "nightly").betaTester, "a marker that isn't beta or normal: ignored");
		std::string inst = fresh.text();
		Prefs withRelease = fresh;
		withRelease.installedRelease = "v0.5.0 3200";
		inst = withRelease.text();
		expect(loaded("installed", &inst).installedRelease == "v0.5.0 3200", "the release installed is kept");
	}

	removeTree(W(fx.root));
	CreateDirectoryW(W(fx.root).c_str(), nullptr);
	report += strf("channel check (%s): %s\n", kArch, all ? "all passed" : "FAILED");
	return all;
}

namespace {

}  // namespace

bool runSelfTest(std::string& report, const std::string& release, const std::string& key) {
	bool ok;
	if (!release.empty()) {
		report += "Signed folder " + release + "\n";
		ok = updatesig::checkSignedFolder(release, key, selfTestHooks(), readWhole, report);
	} else {
		wchar_t tmp[MAX_PATH];
		GetTempPathW(MAX_PATH, tmp);
		const std::wstring dir = std::wstring(tmp) + L"CedarLogic-update-test-" + std::to_wstring(GetCurrentProcessId());
		removeTree(dir);
		CreateDirectoryW(dir.c_str(), nullptr);
		report += "Zip updates\n";
		ok = updatesig::selfTest(U(dir), selfTestHooks(), report);
		removeTree(dir);
	}
	report += strf("update self-test (this build: %s, build %lld): %s\n", CL_GIT_COMMIT, (long long)CL_BUILD_TIME,
	               ok ? "all passed" : "FAILED");
	return ok;
}

void start() {
	// The exe a finished update moved aside, and the res folder it replaced.
	DeleteFileW((W(exePath()) + L".old").c_str());
	removeTree(W(dirName(exePath())) + L"\\res.old");
	// A copy from the test builds, made a beta tester by Prefs::loadFrom:
	// Settings says so, and the first check's answer (finish()).
	if (prefs().testingGroupNotice) {
		g_status = kNotice;
		logUpdate(std::string("testing group: ") + kNotice);
	}
	// Builds made outside CI (no commit known) don't update themselves.
	if (std::string(CL_GIT_COMMIT) == "unknown") return;
	SetTimer(nullptr, 0, 8000, firstCheck);
	SetTimer(nullptr, 0, 4 * 60 * 60 * 1000, laterCheck);   // and every few hours
}

void checkNow(HWND parent) { begin(parent, true); }

void groupChanged() {
	g_offered.clear();
	g_status.clear();
	// A check on its way looks again when it's done (finish()); else, now.
	if (!g_check && prefs().checkUpdates && std::string(CL_GIT_COMMIT) != "unknown") begin(nullptr, false);
}

std::string status() { return g_status; }

void noticeSeen() {
	if (!prefs().testingGroupNotice) return;
	prefs().testingGroupNotice = false;
	prefs().save();
}

void shutdown() {
	// (A message being fetched, the same.)
	if (g_message) {
		g_message->worker.detach();
		(void)g_message.release();
	}
	if (!g_check) return;
	// A check still on its way can't be waited for (a slow network takes a
	// minute), and a std::thread left joinable when the statics go aborts
	// the process. Let it run until the process ends, and keep what it
	// writes to alive till then.
	g_check->worker.detach();
	(void)g_check.release();
}

}  // namespace updater
