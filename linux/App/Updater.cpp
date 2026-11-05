// The AppImage and .deb updater (see Updater.h).
//
//   1. which release: a Beta tester's is the rolling "linux-native-testing"
//      in leviholliday/CedarLogic; a Normal tester's, the newest full
//      (not pre-) release v<version> in leviholliday/CedarLogic-Releases
//      that has this computer's file (its .deb, or its AppImage) and a
//      signed list. Ask GitHub for the commit that release's tag is on.
//   2. Beta: compare it with CL_GIT_COMMIT, baked in at build time
//   3. different -> fetch the release's SHA256SUMS and SHA256SUMS.sig and
//      check them (update-signing/UpdateSignature.h): signed with the update
//      key built in here, for this release's tag and the tag's commit, a
//      build newer than this one (CL_BUILD_TIME), with one file for this CPU
//      and kind. A Normal tester's release must also be no older a version
//      than this one (CL_VERSION), and not the one this updater installed.
//      Anything less: no update is offered, and why goes to stderr.
//   4. verified -> ask; yes -> download that file next to the running one,
//      check its size and SHA-256 against the manifest, make it executable
//      and rename it over the old file (the AppImage); or download the .deb
//      and hand it to this program's own --install-update through pkexec,
//      which copies it somewhere only root can write, checks it all again
//      there, and only then runs apt on that copy
//   5. offer to restart, through the app's normal Quit (so unsaved work is
//      asked about first) -- then relaunch the new file

#include "Updater.h"
#include "Alert.h"
#include "Window.h"

#include "UpdateSignature.h"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <functional>
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string>
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

namespace {

const char* kOwnerRepo = "leviholliday/CedarLogic";
const char* kTag = "linux-native-testing";
// The versioned releases, every platform's (the Mac's DMGs and Sparkle zips,
// Windows' installers, Linux's .debs and AppImages), as v0.4, v0.4.1...
const char* kReleasesRepo = "leviholliday/CedarLogic-Releases";

// Where the checks go: GitHub, or --channel-check's stand-in server.
struct Endpoints {
	std::string api = "https://api.github.com";
	std::string download = "https://github.com";
};
// --channel-check's stand-in (http://127.0.0.1:<port>): the only place plain
// http is fetched from. Empty otherwise.
std::string g_testServer;

void logUpdate(const std::string& what) { fprintf(stderr, "CedarLogic update: %s\n", what.c_str()); }

std::string cpuArch() {
	struct utsname u;
	if (uname(&u) != 0) return "x86_64";
	std::string m = u.machine;
	return m == "arm64" ? "aarch64" : m;
}

// The AppImage runtime always sets this to the file's own path; empty means
// this copy was run some other way (built from source, extracted).
std::string runningAppImage() {
	const char* p = std::getenv("APPIMAGE");
	return p ? std::string(p) : std::string();
}

// Started with --appimage-extract-and-run (no FUSE here): the runtime
// unpacked the AppImage into a temporary appimage_extracted_ folder rather
// than mounting it, and it needs the same every time it starts. Only APPDIR
// says so (or the variable that asks for it, when that's how it started).
bool extractedAndRun() {
	if (runningAppImage().empty()) return false;
	const char* e = std::getenv("APPIMAGE_EXTRACT_AND_RUN");
	const char* d = std::getenv("APPDIR");
	return (e && *e) || (d && strstr(d, "/appimage_extracted_"));
}

// Installed from the .deb: the app is /usr/bin/cedarlogic, which dpkg owns.
bool installedFromDeb() {
	if (!runningAppImage().empty()) return false;
	char exe[4096];
	const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
	if (n <= 0) return false;
	exe[n] = 0;
	return strcmp(exe, "/usr/bin/cedarlogic") == 0 && g_file_test("/var/lib/dpkg/info/cedarlogic.list", G_FILE_TEST_EXISTS);
}

// curl, or wget where curl is missing (a stock Ubuntu desktop has only
// wget), each run straight from an argument list: no shell ever reads the
// URL or the file name, so nothing in them can be taken as a command.
// Only https is fetched (and the --channel-check stand-in's plain http).
// Neither reads the user's ~/.curlrc or ~/.wgetrc, and curl won't follow a
// redirect off https either. (wget has no switch for that; what it brings
// is still checked against the signed manifest.)
bool testServerUrl(const std::string& url) {
	return !g_testServer.empty() && url.compare(0, g_testServer.size() + 1, g_testServer + "/") == 0;
}
bool fetchable(const std::string& url) { return url.compare(0, 8, "https://") == 0 || testServerUrl(url); }

std::vector<std::string> curlArgs(const std::string& url) {
	if (testServerUrl(url)) return { "curl", "-q", "-fsSL", "--proto", "=http", "--proto-redir", "=http" };   // the stand-in only
	return { "curl", "-q", "-fsSL", "--proto", "=https", "--proto-redir", "=https" };
}
std::vector<std::string> wgetArgs() { return { "wget", "--no-config", "-q" }; }

// Runs `args` (found on PATH, no shell), its errors thrown away. With `out`,
// what it writes is read into it, at most `limit` bytes: past that it's
// stopped and `over` set. True when it ran and exited 0.
bool runProgram(const std::vector<std::string>& args, std::string* out = nullptr, size_t limit = 0, bool* over = nullptr) {
	std::vector<gchar*> argv;
	for (const std::string& a : args) argv.push_back(const_cast<gchar*>(a.c_str()));
	argv.push_back(nullptr);
	if (over) *over = false;
	GPid pid = 0;
	gint fd = -1;
	GError* e = nullptr;
	const GSpawnFlags flags = (GSpawnFlags)(G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDERR_TO_DEV_NULL |
	                                        (out ? 0 : G_SPAWN_STDOUT_TO_DEV_NULL));
	if (!g_spawn_async_with_pipes(nullptr, argv.data(), nullptr, flags, nullptr, nullptr, &pid, nullptr, out ? &fd : nullptr, nullptr, &e)) {
		if (e) g_error_free(e);
		return false;   // not installed
	}
	bool tooMuch = false;
	if (out) {
		out->clear();
		char chunk[8192];
		while (true) {
			const ssize_t got = read(fd, chunk, sizeof chunk);
			if (got < 0 && errno == EINTR) continue;
			if (got <= 0) break;
			out->append(chunk, (size_t)got);
			if (out->size() > limit) {
				tooMuch = true;
				kill(pid, SIGTERM);
				break;
			}
		}
		close(fd);
	}
	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
	g_spawn_close_pid(pid);
	if (over) *over = tooMuch;
	return !tooMuch && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// The body, at most `limit` bytes (more: none at all, as a failure).
std::string fetch(const std::string& url, size_t limit = 1u << 20) {
	if (!fetchable(url)) return {};
	std::vector<std::string> curl = curlArgs(url);
	for (const char* a : { "--max-time", "15", "--max-filesize" }) curl.push_back(a);
	curl.push_back(format("%zu", limit));
	for (const std::string& a : { std::string("-H"), std::string("User-Agent: CedarLogic"), std::string("--"), url }) curl.push_back(a);
	std::vector<std::string> wget = wgetArgs();
	for (const std::string& a : { std::string("-T"), std::string("15"), std::string("-U"), std::string("CedarLogic"), std::string("-O"),
	                              std::string("-"), std::string("--"), url })
		wget.push_back(a);
	for (const std::vector<std::string>* args : { &curl, &wget }) {
		std::string body;
		bool over = false;
		if (runProgram(*args, &body, limit, &over) && !body.empty()) return body;
		if (over) return {};
	}
	return {};
}

bool download(const std::string& url, const std::string& dest) {
	if (!fetchable(url) || dest.empty()) return false;
	std::vector<std::string> curl = curlArgs(url);
	for (const std::string& a : { std::string("--max-time"), std::string("300"), std::string("-o"), dest, std::string("--"), url }) curl.push_back(a);
	std::vector<std::string> wget = wgetArgs();
	for (const std::string& a : { std::string("-T"), std::string("120"), std::string("-O"), dest, std::string("--"), url }) wget.push_back(a);
	for (const std::vector<std::string>* args : { &curl, &wget }) {
		if (runProgram(*args)) return true;
		std::remove(dest.c_str());
	}
	return false;
}

// The value of one "key":"value" string field in a JSON document, unescaping
// \" \\ \/ \n \t. Good enough for GitHub's API responses without pulling in a
// JSON library. Starts from `from`, so a caller walks repeated keys (every
// asset has its own "browser_download_url") by passing the previous match's
// end back in; `endOut`, if given, is set to just past the closing quote.
bool jsonString(const std::string& json, const std::string& key, std::string& out, size_t from = 0,
                size_t* endOut = nullptr, size_t* startOut = nullptr) {
	const std::string needle = "\"" + key + "\":\"";
	size_t at = json.find(needle, from);
	if (at == std::string::npos) return false;
	at += needle.size();
	if (startOut) *startOut = at;
	std::string raw;
	for (size_t i = at; i < json.size(); i++) {
		if (json[i] == '"') {
			out.swap(raw);
			if (endOut) *endOut = i + 1;
			return true;
		}
		if (json[i] == '\\' && i + 1 < json.size()) {
			i++;
			switch (json[i]) {
			case 'n': raw += '\n'; break;
			case 't': raw += '\t'; break;
			default: raw += json[i]; break;   // \" \\ \/ and anything else literally
			}
		} else {
			raw += json[i];
		}
	}
	return false;   // no closing quote: malformed
}

// The commit a repo's tag points at right now, or "" on failure.
std::string latestCommit(const Endpoints& ep, const std::string& repo, const std::string& tag) {
	if (!updatesig::safeName(tag)) return {};
	const std::string body = fetch(ep.api + "/repos/" + repo + "/commits/" + tag);
	std::string sha;
	// The very first "sha" field in the response is the commit's own (every
	// nested one -- the tree, its parents -- comes later in GitHub's JSON).
	if (body.empty() || !jsonString(body, "sha", sha)) return {};
	return sha;
}

// ---- The versioned releases --------------------------------------------------------

// "v0.4.1" (or "0.4.1") -> {0, 4, 1}: digits and dots only, one to four
// numbers. Anything else ("v4.0.0-beta.1", "linux-native-testing") isn't a
// version.
bool parseVersion(const std::string& text, std::vector<long>& out) {
	std::string s = text;
	if (!s.empty() && s[0] == 'v') s.erase(0, 1);
	out.clear();
	size_t at = 0;
	while (true) {
		size_t end = at;
		while (end < s.size() && s[end] >= '0' && s[end] <= '9') end++;
		if (end == at || end - at > 6) return false;
		out.push_back(std::stol(s.substr(at, end - at)));
		if (end == s.size()) break;
		if (s[end] != '.') return false;
		at = end + 1;
	}
	return out.size() <= 4;
}

// <0, 0, >0, with missing numbers as 0 (0.4 == 0.4.0).
int compareVersions(const std::vector<long>& a, const std::vector<long>& b) {
	for (size_t i = 0; i < std::max(a.size(), b.size()); i++) {
		const long x = i < a.size() ? a[i] : 0, y = i < b.size() ? b[i] : 0;
		if (x != y) return x < y ? -1 : 1;
	}
	return 0;
}

bool isReleaseTag(const std::string& tag) {
	std::vector<long> v;
	return tag.size() > 1 && tag[0] == 'v' && parseVersion(tag, v);
}

// One release in GitHub's list of them.
struct ListedRelease {
	std::string tag;
	bool draft = true, prerelease = true;   // (unknown: treated as not a full release)
	std::vector<std::string> urls;          // its files' browser_download_url
};

// The JSON string whose opening quote is at `at`, unescaped as jsonString
// does; `end` is set to just past its closing quote.
bool readStringAt(const std::string& json, size_t at, std::string& out, size_t& end) {
	if (at >= json.size() || json[at] != '"') return false;
	std::string raw;
	for (size_t i = at + 1; i < json.size(); i++) {
		if (json[i] == '"') {
			out.swap(raw);
			end = i + 1;
			return true;
		}
		if (json[i] == '\\' && i + 1 < json.size()) {
			i++;
			raw += json[i] == 'n' ? '\n' : json[i] == 't' ? '\t' : json[i];
		} else {
			raw += json[i];
		}
	}
	return false;
}

// The position just past `"key":` (and any spaces) at or after `from`.
size_t findKey(const std::string& json, const char* key, size_t from) {
	const std::string needle = std::string("\"") + key + "\":";
	size_t at = json.find(needle, from);
	if (at == std::string::npos) return std::string::npos;
	at += needle.size();
	while (at < json.size() && json[at] == ' ') at++;
	return at;
}

// GitHub's list of releases, as tag, draft, prerelease and file URLs. Its
// JSON has each release's tag_name, then draft and prerelease, then its
// assets (each with a browser_download_url), in that order; a quote inside a
// string is \", so the text of a release's notes can't pass for a key.
// (Whatever the order, a URL is only ever taken for the tag it names.)
std::vector<ListedRelease> parseReleases(const std::string& json) {
	std::vector<ListedRelease> out;
	const char* keys[] = { "tag_name", "draft", "prerelease", "browser_download_url" };
	size_t next[4];
	for (int i = 0; i < 4; i++) next[i] = findKey(json, keys[i], 0);
	while (true) {
		int k = -1;
		for (int i = 0; i < 4; i++)
			if (next[i] != std::string::npos && (k < 0 || next[i] < next[k])) k = i;
		if (k < 0) break;
		const size_t at = next[k];
		size_t end = at + 1;
		if (k == 0 || k == 3) {
			std::string value;
			if (readStringAt(json, at, value, end)) {
				if (k == 0) {
					out.push_back(ListedRelease());
					out.back().tag = value;
				} else if (!out.empty()) {
					out.back().urls.push_back(value);
				}
			}
		} else if (!out.empty()) {
			const bool t = json.compare(at, 4, "true") == 0, f = json.compare(at, 5, "false") == 0;
			if (t || f) (k == 1 ? out.back().draft : out.back().prerelease) = t;
		}
		next[k] = findKey(json, keys[k], std::max(end, at + 1));
	}
	return out;
}

// ---- Verifying ------------------------------------------------------------------

enum class Kind { AppImage, Deb };

std::string debName() { return cpuArch() == "aarch64" ? "cedarlogic_arm64.deb" : "cedarlogic_amd64.deb"; }

// This copy's file among the manifest's names: the .deb for this CPU, or
// this CPU's AppImage (whose name has the version in it).
bool isOurs(const std::string& name, Kind kind) {
	if (kind == Kind::Deb) return name == debName();
	const std::string prefix = "CedarLogic-Native-", suffix = "-" + cpuArch() + ".AppImage";
	return name.size() > prefix.size() + suffix.size() && name.compare(0, prefix.size(), prefix) == 0 &&
	       name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool startsEnds(const std::string& s, const std::string& prefix, const std::string& suffix) {
	return s.size() > prefix.size() + suffix.size() && s.compare(0, prefix.size(), prefix) == 0 &&
	       s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// This copy's file among a versioned release's: the .deb for this CPU as
// docs/RELEASE-0.4.md renames it (cedarlogic_0.4_amd64.deb), or as the test
// build names it; this CPU's AppImage, CedarLogic-Native-<v>-<arch>.AppImage
// or CedarLogic-<v>-<arch>.AppImage. (linux/install.sh takes the same names.)
bool isReleaseFile(const std::string& name, Kind kind) {
	if (kind == Kind::Deb) {
		const std::string arch = cpuArch() == "aarch64" ? "arm64" : "amd64";
		return name == debName() || (startsEnds(name, "cedarlogic_", "_" + arch + ".deb") && name[11] >= '0' && name[11] <= '9');
	}
	const std::string suffix = "-" + cpuArch() + ".AppImage";
	for (const char* prefix : { "CedarLogic-Native-", "CedarLogic-" }) {
		const size_t n = std::strlen(prefix);
		if (startsEnds(name, prefix, suffix) && name[n] >= '0' && name[n] <= '9') return true;
	}
	return false;
}

// A file on a release, as GitHub serves it.
std::string releaseFileUrl(const Endpoints& ep, const std::string& repo, const std::string& tag, const std::string& name) {
	return ep.download + "/" + repo + "/releases/download/" + tag + "/" + name;
}
std::string releasePage(const std::string& repo, const std::string& tag) {
	return format("https://github.com/%s/releases/tag/%s", repo.c_str(), tag.c_str());
}

// A file's SHA-256 (GLib's) and size, read through once.
bool sha256File(const std::string& path, std::string& hex, long long& size) {
	FILE* f = fopen(path.c_str(), "rb");
	if (!f) return false;
	GChecksum* sum = g_checksum_new(G_CHECKSUM_SHA256);
	unsigned char chunk[65536];
	size_t got;
	size = 0;
	while ((got = fread(chunk, 1, sizeof chunk, f)) > 0) {
		g_checksum_update(sum, chunk, got);
		size += (long long)got;
	}
	const bool ok = !ferror(f);
	fclose(f);
	hex = g_checksum_get_string(sum);
	g_checksum_free(sum);
	return ok;
}

std::string sha256Bytes(const std::string& bytes) {
	gchar* s = g_compute_checksum_for_data(G_CHECKSUM_SHA256, reinterpret_cast<const guchar*>(bytes.data()), bytes.size());
	std::string out = s;
	g_free(s);
	return out;
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

// What an update check found: the file to download and the signed manifest
// it came from (the .deb's root step checks that again itself).
struct Asset {
	std::string url;
	updatesig::File file;
	std::string manifest, signature;
	std::string page;        // the release's page, for installing it by hand
	std::string installed;   // a versioned release's "<tag> <build>" (Prefs::installedRelease), else ""
};

// A release's manifest and signature through `get` (the release's download
// URLs, or a folder in the self-test), and the decision on them: signed, for
// `tag` and the commit it's on, newer than this build, one file `ours`.
updatesig::Decision findVerified(const std::function<bool(const std::string& name, std::string& out)>& get, const unsigned char key[32],
                                 const std::string& tag, const std::string& releaseCommit, const std::string& runningCommit,
                                 long long runningBuild, const std::function<bool(const std::string&)>& ours, std::string* manifestOut,
                                 std::string* signatureOut) {
	std::string manifest, signature;
	bool haveManifest = false, haveSignature = false;
	if (releaseCommit != runningCommit) {
		haveManifest = get(updatesig::kManifestName, manifest);
		haveSignature = haveManifest && get(updatesig::kSignatureName, signature);
	}
	const updatesig::Decision d =
		updatesig::decide(haveManifest, manifest, haveSignature, signature, key, tag, releaseCommit, runningCommit, runningBuild, ours);
	if (manifestOut) *manifestOut = manifest;
	if (signatureOut) *signatureOut = signature;
	return d;
}

// The rolling test build's (the self-test's hooks).
updatesig::Decision findVerified(const std::function<bool(const std::string& name, std::string& out)>& get, const unsigned char key[32],
                                 const std::string& releaseCommit, const std::string& runningCommit, long long runningBuild, Kind kind) {
	return findVerified(get, key, kTag, releaseCommit, runningCommit, runningBuild, [kind](const std::string& n) { return isOurs(n, kind); },
	                    nullptr, nullptr);
}

// ---- One check, for a testing group ------------------------------------------------

enum Channel { kNormal = 0, kBeta = 1 };

// This copy, as the check sees it (CL_GIT_COMMIT and the rest; the
// channel check passes stand-ins).
struct Running {
	std::string commit;
	long long build = 0;
	std::string version;            // CL_VERSION
	std::string installedRelease;   // Prefs::installedRelease
};

struct Outcome {
	bool fetched = false;     // the feed answered
	bool noRelease = false;   // Normal: no full release has this computer's file yet
	std::string key;          // what was offered: the test build's commit, or "<tag> <build>"
	updatesig::Decision decision;
	Asset asset;
};

Outcome checkChannel(int channel, Kind kind, const Running& running, const unsigned char key[32], const Endpoints& ep) {
	Outcome o;
	std::string repo, tag, commit;
	std::function<bool(const std::string&)> ours;
	if (channel == kBeta) {
		repo = kOwnerRepo;
		tag = kTag;
		ours = [kind](const std::string& n) { return isOurs(n, kind); };
	} else {
		// The newest full release (not a draft, not a pre-release: the Mac's
		// betas and the Classic builds are those) whose tag is a version, with
		// this computer's file and a signed list. Newest by version, not by date.
		const std::string list = fetch(ep.api + "/repos/" + kReleasesRepo + "/releases?per_page=50", 8u << 20);
		if (list.empty() || list[0] != '[') {
			o.decision.why = "couldn't read the list of releases";
			return o;
		}
		o.fetched = true;
		ours = [kind](const std::string& n) { return isReleaseFile(n, kind); };
		std::vector<long> best;
		for (const ListedRelease& r : parseReleases(list)) {
			std::vector<long> v;
			if (r.draft || r.prerelease || !isReleaseTag(r.tag) || !parseVersion(r.tag, v)) continue;
			bool file = false, manifest = false, signature = false;
			for (const std::string& url : r.urls) {
				const std::string name = url.substr(url.rfind('/') + 1);
				if (!updatesig::safeName(name) || url != releaseFileUrl(ep, kReleasesRepo, r.tag, name)) continue;
				file = file || ours(name);
				manifest = manifest || name == updatesig::kManifestName;
				signature = signature || name == updatesig::kSignatureName;
			}
			if (file && manifest && signature && (tag.empty() || compareVersions(v, best) > 0)) {
				tag = r.tag;
				best = v;
			}
		}
		if (tag.empty()) {
			o.noRelease = true;
			o.decision.result = updatesig::Result::UpToDate;
			o.decision.why = std::string("no full release in ") + kReleasesRepo + " has a signed " + (kind == Kind::Deb ? ".deb" : "AppImage") +
			                 " for this computer yet";
			return o;
		}
		repo = kReleasesRepo;
	}
	commit = latestCommit(ep, repo, tag);
	o.fetched = !commit.empty();
	if (!o.fetched) {
		o.decision.why = "couldn't read the commit " + tag + " is on";
		return o;
	}
	auto get = [&](const std::string& name, std::string& out) {
		if (!updatesig::safeName(name)) return false;
		out = fetch(releaseFileUrl(ep, repo, tag, name));
		return !out.empty();
	};
	std::string manifest, signature;
	o.decision = findVerified(get, key, tag, commit, running.commit, running.build, ours, &manifest, &signature);
	o.key = channel == kBeta ? commit : tag + " " + std::to_string(o.decision.manifest.build);
	if (o.decision.result == updatesig::Result::Offer && channel == kNormal) {
		// Never back to an older version (a Beta tester's newer test build is
		// refused by its build already), nor the one this updater installed.
		std::vector<long> mine, theirs;
		if (parseVersion(running.version, mine) && parseVersion(tag, theirs) && compareVersions(theirs, mine) < 0) {
			o.decision.result = updatesig::Result::UpToDate;
			o.decision.why = tag + " is an older version than this one (" + running.version + "): not going back";
		} else if (o.key == running.installedRelease) {
			o.decision.result = updatesig::Result::UpToDate;
			o.decision.why = tag + " (build " + std::to_string(o.decision.manifest.build) + ") is the release installed";
		}
	}
	if (o.decision.result == updatesig::Result::Offer) {
		o.asset.url = releaseFileUrl(ep, repo, tag, o.decision.file.name);
		o.asset.file = o.decision.file;
		o.asset.manifest = manifest;
		o.asset.signature = signature;
		o.asset.page = releasePage(repo, tag);
		if (channel == kNormal) o.asset.installed = o.key;
	}
	return o;
}

// ---- The .deb's root step ---------------------------------------------------------
// pkexec /usr/bin/cedarlogic --install-update <deb> <manifest> <signature> <name>
//
// Run as root, by the installed (root-owned) program itself. The files it's
// given are the user's, which anything running as that user could change
// at any moment; so each is copied into a new folder only root can write
// (mkdtemp makes it 0700, root's), and everything is checked on those
// copies: the signature with the built-in key, the tag, a build newer than
// this installed one, the name being this CPU's .deb, its size and SHA-256.
// Only then does apt get the copy (the folder opened to 0755 for apt's own
// user to read it, still writable only by root).

const int kNotVerified = 3;   // (pkexec itself uses 126 and 127)

// A regular file, not a link, at most `limit` bytes, into `bytes`.
bool readRegular(const std::string& path, size_t limit, std::string& bytes, std::string& why) {
	const int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) { why = "can't open " + path + ": " + strerror(errno); return false; }
	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 || (unsigned long long)st.st_size > limit) {
		close(fd);
		why = path + " isn't a regular file of a sensible size";
		return false;
	}
	bytes.clear();
	bytes.reserve((size_t)st.st_size);
	char chunk[65536];
	ssize_t got;
	while ((got = read(fd, chunk, sizeof chunk)) > 0) {
		bytes.append(chunk, (size_t)got);
		if (bytes.size() > limit) { close(fd); why = path + " is too big"; return false; }
	}
	close(fd);
	if (got < 0) { why = "can't read " + path; return false; }
	return true;
}

bool writeNew(const std::string& path, const std::string& bytes) {
	const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
	if (fd < 0) return false;
	size_t at = 0;
	while (at < bytes.size()) {
		const ssize_t n = write(fd, bytes.data() + at, bytes.size() - at);
		if (n <= 0) { close(fd); return false; }
		at += (size_t)n;
	}
	return close(fd) == 0;
}

// The checks and the hand-over, with the key, this build, where the private
// folder goes and what runs apt given (the self-test passes its own).
int installVerifiedDeb(const std::string& deb, const std::string& manifestPath, const std::string& signaturePath, const std::string& name,
                       const unsigned char key[32], long long runningBuild, const std::string& tempBase,
                       const std::function<int(const std::string& copy)>& runApt, std::string& why) {
	// The test build's name, or a versioned release's (which one is checked
	// again once the signed manifest says which release this is).
	if (!updatesig::safeName(name) || !(isOurs(name, Kind::Deb) || isReleaseFile(name, Kind::Deb))) {
		why = "\"" + name + "\" isn't this computer's .deb";
		return kNotVerified;
	}
	std::string folder = tempBase + "/cedarlogic-update-XXXXXX";
	if (!g_mkdtemp_full(&folder[0], 0700)) { why = "can't make a private folder in " + tempBase; return kNotVerified; }
	const std::string copy = folder + "/" + name;
	std::string manifest, signature, bytes;
	auto done = [&](int code) {
		g_remove(copy.c_str());
		g_rmdir(folder.c_str());
		return code;
	};
	if (!readRegular(manifestPath, 64 * 1024, manifest, why) || !readRegular(signaturePath, 64, signature, why) ||
	    !readRegular(deb, 512u << 20, bytes, why))
		return done(kNotVerified);
	if (!writeNew(copy, bytes)) { why = "can't write " + copy; return done(kNotVerified); }
	bytes.clear();
	if (!updatesig::verifySignature(manifest, signature, key)) { why = "the manifest's signature doesn't verify"; return done(kNotVerified); }
	updatesig::Manifest m;
	if (!updatesig::parseManifest(manifest, m, why)) return done(kNotVerified);
	// The rolling test build (a Beta tester's), or a versioned release (a
	// Normal tester's): each with its own names. Either way the build must
	// be newer than the installed one, so an old signed manifest of either
	// can't take it back.
	const bool testing = m.tag == kTag;
	if (!testing && !isReleaseTag(m.tag)) { why = "the manifest is for " + m.tag; return done(kNotVerified); }
	if (!(testing ? isOurs(name, Kind::Deb) : isReleaseFile(name, Kind::Deb))) {
		why = "\"" + name + "\" isn't this computer's .deb in " + m.tag;
		return done(kNotVerified);
	}
	if (m.build <= runningBuild) {
		why = "build " + std::to_string(m.build) + " isn't newer than the installed one (" + std::to_string(runningBuild) + ")";
		return done(kNotVerified);
	}
	const updatesig::File* entry = nullptr;
	for (const updatesig::File& f : m.files)
		if (f.name == name) entry = &f;
	if (!entry) { why = "the manifest doesn't list " + name; return done(kNotVerified); }
	if (!verifyFile(copy, *entry, why)) return done(kNotVerified);
	// apt reads it as its own user: into the folder, which stays root's.
	if (chmod(folder.c_str(), 0755) != 0) { why = "can't open the private folder to apt"; return done(kNotVerified); }
	return done(runApt(copy));
}

int aptInstall(const std::string& copy) {
	// apt waits a minute for another install (unattended-upgrades, the
	// software updater) rather than failing at once. --allow-downgrades
	// stays as before: the signed build number above already refuses
	// anything older than this.
	gchar* argv[] = { (gchar*)"/usr/bin/apt-get", (gchar*)"install", (gchar*)"-y", (gchar*)"--allow-downgrades",
	                  (gchar*)"-o", (gchar*)"DPkg::Lock::Timeout=60", const_cast<gchar*>(copy.c_str()), nullptr };
	gint status = 1;
	GError* e = nullptr;
	// apt's errors go to this process's stderr, which pkexec's caller reads.
	if (!g_spawn_sync(nullptr, argv, nullptr, G_SPAWN_STDOUT_TO_DEV_NULL, nullptr, nullptr, nullptr, nullptr, &status, &e)) {
		fprintf(stderr, "E: CedarLogic: couldn't run apt-get: %s\n", e ? e->message : "?");
		if (e) g_error_free(e);
		return 1;
	}
	return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

// The updated copy, started as this one quits. It waits for this process to
// end (main.cpp), so it becomes the running CedarLogic rather than handing
// its launch back to this one.
void relaunch(const std::string& file) {
	gchar** env = g_environ_setenv(g_get_environ(), "CEDARLOGIC_RESTART_AFTER", format("%ld", (long)getpid()).c_str(), TRUE);
	// Without FUSE, started the way this one was (else it never comes back).
	if (extractedAndRun()) env = g_environ_setenv(env, "APPIMAGE_EXTRACT_AND_RUN", "1", TRUE);
	gchar* argv[] = { const_cast<gchar*>(file.c_str()), nullptr };
	// A failure leaves the file updated; a manual relaunch still gets it.
	g_spawn_async(nullptr, argv, env, (GSpawnFlags)0, nullptr, nullptr, nullptr, nullptr);
	g_strfreev(env);
}

std::atomic<bool> g_checking{false};
std::string g_offeredThisRun;   // a background check offers each commit once
guint g_timer = 0;

// Slow work (the download, apt) on a background thread, with a small pulsing
// window meanwhile, so every window keeps drawing; only this loop touches
// widgets.
bool runWithProgress(GtkWindow* parent, const char* text, const std::function<bool()>& work) {
	// A plain window, not a GtkDialog: there are no buttons for the user to
	// press while this runs.
	GtkWidget* d = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(d), "Updating CedarLogic");
	gtk_window_set_modal(GTK_WINDOW(d), TRUE);
	gtk_window_set_transient_for(GTK_WINDOW(d), parent);
	gtk_window_set_resizable(GTK_WINDOW(d), FALSE);
	gtk_window_set_deletable(GTK_WINDOW(d), FALSE);
	// Not deletable is only a hint some desktops ignore: closing it anyway
	// does nothing, and should it go some other way, the loop stops using it.
	bool gone = false;
	g_signal_connect(d, "delete-event", G_CALLBACK(gtk_true), nullptr);
	g_signal_connect(d, "destroy", CL_CALLBACK(+[](GtkWidget*, gpointer gone) { *static_cast<bool*>(gone) = true; }), &gone);
	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_set_border_width(GTK_CONTAINER(box), 16);
	gtk_container_add(GTK_CONTAINER(d), box);
	GtkWidget* label = gtk_label_new(text);
	GtkWidget* bar = gtk_progress_bar_new();
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 4);
	gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 4);
	gtk_widget_set_size_request(d, 320, -1);
	gtk_widget_show_all(d);

	std::atomic<bool> done{false};
	bool ok = false;
	std::thread worker([&] { ok = work(); done.store(true); });
	while (!done.load()) {
		if (!gone) gtk_progress_bar_pulse(GTK_PROGRESS_BAR(bar));
		while (gtk_events_pending()) gtk_main_iteration();
		g_usleep(80000);
	}
	worker.join();
	if (!gone) gtk_widget_destroy(d);
	return ok;
}

// Downloaded and checked against the signed manifest; `why` says what failed.
enum class Got { Ok, NoDownload, NotVerified };
Got downloadVerified(GtkWindow* parent, const Asset& asset, const std::string& dest, std::string& why) {
	Got got = Got::NoDownload;
	runWithProgress(parent, "Downloading the update…", [&] {
		if (!download(asset.url, dest)) return false;
		got = verifyFile(dest, asset.file, why) ? Got::Ok : Got::NotVerified;
		return true;
	});
	if (got == Got::NotVerified) logUpdate("the download didn't verify, not installing it: " + why);
	return got;
}

void showNotVerified(GtkWindow* parent, const std::string& why) {
	showMessage(parent, GTK_MESSAGE_WARNING, "The update didn't pass its check",
	            "The downloaded file isn't the one the signed list of this build names, so it wasn't installed. (" + why + ")");
}

// What apt said last: its error ("E: ..."), or else its last line.
std::string lastAptLine(const char* out) {
	std::string last, error;
	gchar** lines = g_strsplit(out ? out : "", "\n", -1);
	for (gchar** l = lines; *l; l++) {
		const std::string line = g_strstrip(*l);
		if (line.empty()) continue;
		last = line;
		if (line.rfind("E:", 0) == 0) error = line;
	}
	g_strfreev(lines);
	return error.empty() ? last : error;
}

// A versioned release, once installed: never offered again (its signed
// build can be the time it was signed, later than the build in it).
void rememberInstalled(const Asset& asset) {
	if (asset.installed.empty()) return;
	prefs().installedRelease = asset.installed;
	prefs().save();
}

// The .deb: downloaded and checked, then installed through pkexec (which
// asks for the password in the desktop's own dialog) by this program's own
// root step, which checks it all again on a copy only root can change.
void installDeb(GtkApplication* app, const Asset& asset) {
	GtkWindow* parent = gtk_application_get_active_window(app);
	const std::string& page = asset.page;
	gchar* pkexec = g_find_program_in_path("pkexec");
	if (pkexec == nullptr) {
		if (askConfirm(parent, "An update is available", "Open the download page to get the new installer?", "Open Page", "Not Now")) openExternally(parent, page);
		return;
	}
	const std::string dir = std::string(g_get_user_cache_dir()) + "/CedarLogic";
	g_mkdir_with_parents(dir.c_str(), 0755);
	const std::string file = dir + "/cedarlogic-update.deb";
	const std::string manifestFile = dir + "/cedarlogic-update.SHA256SUMS", signatureFile = dir + "/cedarlogic-update.SHA256SUMS.sig";
	auto clean = [&] {
		std::remove(file.c_str());
		std::remove(manifestFile.c_str());
		std::remove(signatureFile.c_str());
	};
	std::string why;
	const Got got = downloadVerified(parent, asset, file, why);
	if (got != Got::Ok || !g_file_set_contents(manifestFile.c_str(), asset.manifest.data(), (gssize)asset.manifest.size(), nullptr) ||
	    !g_file_set_contents(signatureFile.c_str(), asset.signature.data(), (gssize)asset.signature.size(), nullptr)) {
		clean();
		g_free(pkexec);
		if (got == Got::NotVerified) showNotVerified(parent, why);
		else showMessage(parent, GTK_MESSAGE_WARNING, "The update couldn't be downloaded", "Check your connection and try again.");
		return;
	}
	// Readable by anyone (a home folder over NFS can hide it from root).
	chmod(dir.c_str(), 0755);
	chmod(file.c_str(), 0644);
	chmod(manifestFile.c_str(), 0644);
	chmod(signatureFile.c_str(), 0644);
	gchar* argv[] = { pkexec, (gchar*)"/usr/bin/cedarlogic", (gchar*)"--install-update", const_cast<gchar*>(file.c_str()),
	                  const_cast<gchar*>(manifestFile.c_str()), const_cast<gchar*>(signatureFile.c_str()),
	                  const_cast<gchar*>(asset.file.name.c_str()), nullptr };
	gint status = 1;
	bool ran = false;
	why.clear();
	runWithProgress(parent, "Installing the update…", [&] {
		gchar* err = nullptr;
		GError* e = nullptr;
		ran = g_spawn_sync(nullptr, argv, nullptr, G_SPAWN_STDOUT_TO_DEV_NULL, nullptr, nullptr, nullptr, &err, &status, &e);
		why = e ? e->message : lastAptLine(err);
		if (e) g_error_free(e);
		g_free(err);
		return ran;
	});
	g_free(pkexec);
	clean();
	const int code = !ran ? -1 : WIFEXITED(status) ? WEXITSTATUS(status) : -1;
	if (code != 0) {
		// pkexec: 126, the password was cancelled; 127, it couldn't be asked
		// for or wasn't accepted. 3, the root step's own check refused it.
		// Anything else is apt's own trouble.
		if (code == kNotVerified) logUpdate("the root step refused the update: " + why);
		const std::string text = code == 126 ? "Installing it needs your password, and that was cancelled."
		                       : code == 127 ? "Installing it needs an administrator's password, and the system couldn't ask for it or didn't accept it."
		                       : code == kNotVerified ? "It didn't pass its check" + (why.empty() ? std::string(".") : ": " + why)
		                       : "apt couldn't install it" + (why.empty() ? std::string(".") : ": " + why);
		if (askConfirm(parent, "The update wasn't installed", text + " Open the download page to install it by hand?", "Open Page", "Not Now"))
			openExternally(parent, page);
		return;
	}
	rememberInstalled(asset);
	if (askConfirm(parent, "The update is installed", "Restart CedarLogic now to use it? Your circuits are saved.", "Restart Now", "Later")) {
		if (quitApp(app)) relaunch("/usr/bin/cedarlogic");
	}
}

void install(GtkApplication* app, const Asset& asset) {
	if (installedFromDeb()) { installDeb(app, asset); return; }
	GtkWindow* parent = gtk_application_get_active_window(app);
	const std::string current = runningAppImage();
	if (current.empty()) {
		if (askConfirm(parent, "An update is available",
		               "This copy of CedarLogic isn't the AppImage or the installed app, so it can't update itself. Open the download page?",
		               "Open Page", "Not Now"))
			openExternally(parent, asset.page);
		return;
	}
	const std::string dir = [&] {
		gchar* d = g_path_get_dirname(current.c_str());
		std::string s = d;
		g_free(d);
		return s;
	}();
	const std::string temp = format("%s/.CedarLogic-update-%d", dir.c_str(), (int)getpid());

	std::string why;
	const Got got = downloadVerified(parent, asset, temp, why);
	if (got != Got::Ok) {
		std::remove(temp.c_str());
		if (got == Got::NotVerified) { showNotVerified(parent, why); return; }
		showMessage(parent, GTK_MESSAGE_WARNING, "The update couldn't be downloaded",
		            "Check your connection and try again. (CedarLogic needs curl or wget, and permission to "
		            "write to " + dir + ".)");
		return;
	}
	if (chmod(temp.c_str(), 0755) != 0 || std::rename(temp.c_str(), current.c_str()) != 0) {
		std::remove(temp.c_str());
		showMessage(parent, GTK_MESSAGE_WARNING, "The update was downloaded but couldn't replace the running copy",
		            "Check that you can write to " + dir + ".");
		return;
	}

	rememberInstalled(asset);
	if (askConfirm(parent, "The update is installed", "Restart CedarLogic now to use it? Your circuits are saved.", "Restart Now", "Later")) {
		if (quitApp(app)) relaunch(current);
	}
}

// Everything a background check produces, polled for on the GTK thread (only
// std::atomic<bool> done is touched by both sides at once).
struct CheckResult {
	GtkApplication* app;
	bool interactive;
	int channel;
	std::thread worker;
	std::atomic<bool> done{ false };
	Outcome outcome;
};

// Said once, the first time the updater speaks after a copy from the test
// builds became a Beta tester (Prefs.cpp's startingChannel).
const char* const kChannelNotice =
	"This copy came from the test builds, so it's a Beta tester and keeps getting them. "
	"For only the finished versions, choose Normal tester in Settings › General › Testing group.";

// The once-only notice, if it's due: as a note in the window (the
// updater's status), and gone from the settings after.
std::string takeChannelNotice() {
	if (!prefs().channelNotice) return {};
	prefs().channelNotice = false;
	prefs().save();
	logUpdate(std::string("testing group: Beta tester (") + kChannelNotice + ")");
	return kChannelNotice;
}

gboolean pollCheck(gpointer data) {
	CheckResult* r = static_cast<CheckResult*>(data);
	if (!r->done.load()) return G_SOURCE_CONTINUE;
	// Not over a dialog: restarting would close the windows under it.
	if (!r->interactive && gtk_grab_get_current() != nullptr) return G_SOURCE_CONTINUE;
	r->worker.join();
	// Until the question and any install are over: no second check meanwhile
	// (the daily one, Check for Updates).
	struct Done { ~Done() { g_checking.store(false); } } done;
	GtkWindow* parent = gtk_application_get_active_window(r->app);
	const Outcome& o = r->outcome;
	const bool beta = r->channel == kBeta;
	const bool newer = o.fetched && o.decision.result == updatesig::Result::Offer;
	// The note about the testing group goes with whatever is said now.
	const std::string notice = takeChannelNotice();
	const std::string after = notice.empty() ? std::string() : "\n\n" + notice;
	if (!newer) {
		if (r->interactive) {
			if (!o.fetched)
				showMessage(parent, GTK_MESSAGE_WARNING, "Couldn't check for updates",
				            "CedarLogic couldn't reach the update feed. Check your connection and try again." + after);
			else if (o.decision.result == updatesig::Result::NotVerified)
				showMessage(parent, GTK_MESSAGE_INFO, "No verified update yet",
				            std::string(beta ? "There's a newer test build" : "There's a newer version") +
				                ", but it hasn't been signed yet, so CedarLogic won't install it. Try again later." + after);
			else if (o.noRelease)
				showMessage(parent, GTK_MESSAGE_INFO, "You're up to date",
				            "No finished version of CedarLogic for Linux is out yet. To get the test builds now, choose Beta tester in "
				            "Settings › General › Testing group." + after);
			else
				showMessage(parent, GTK_MESSAGE_INFO, "You're up to date", "This is the latest version." + after);
		} else if (!notice.empty()) {
			if (!circuitWindows().empty()) circuitWindows().front()->note("Updates: Beta tester (from the test builds). Settings › General to change it");
		}
		delete r;
		return G_SOURCE_REMOVE;
	}
	if (!r->interactive) {
		if (g_offeredThisRun == o.key) { delete r; return G_SOURCE_REMOVE; }
		g_offeredThisRun = o.key;
	}
	if (askConfirm(parent, "A new version of CedarLogic is available",
	               std::string(beta ? "A new test build is ready. " : "") + "Download and install it now? It takes a moment, and CedarLogic restarts after." +
	                   after,
	               "Install", "Not Now"))
		install(r->app, o.asset);
	delete r;
	return G_SOURCE_REMOVE;
}

Running thisCopy() {
	Running r;
	r.commit = CL_GIT_COMMIT;
	r.build = (long long)CL_BUILD_TIME;
	r.version = CL_VERSION;
	r.installedRelease = prefs().installedRelease;
	return r;
}

void check(GtkApplication* app, bool interactive) {
	// A copy that can't update itself (built from source) isn't told about
	// every new test build at launch; Check for Updates still says.
	if (!interactive && ((runningAppImage().empty() && !installedFromDeb()) || strcmp(CL_GIT_COMMIT, "unknown") == 0)) return;
	if (g_checking.exchange(true)) return;
	CheckResult* r = new CheckResult{ app, interactive, prefs().updateChannel == kBeta ? kBeta : kNormal };
	const Running running = thisCopy();
	const Kind kind = installedFromDeb() ? Kind::Deb : Kind::AppImage;
	r->worker = std::thread([r, running, kind] {
		r->outcome = checkChannel(r->channel, kind, running, updatesig::kPublicKey, Endpoints());
		logUpdate(std::string(r->channel == kBeta ? "Beta" : "Normal") + ": " + r->outcome.decision.why);
		r->done.store(true);
	});
	g_timeout_add(150, pollCheck, r);
}

// ---- Self-test -------------------------------------------------------------------

std::string joinPath(const std::string& dir, const std::string& name) { return dir + "/" + name; }

bool readWhole(const std::string& path, std::string& bytes) {
	gchar* data = nullptr;
	gsize len = 0;
	if (!g_file_get_contents(path.c_str(), &data, &len, nullptr)) return false;
	bytes.assign(data, len);
	g_free(data);
	return true;
}

// The hooks for a release: the rolling test build's (kTag), or a versioned
// release's (v0.4.1: a Normal tester's, with its names).
updatesig::SelfTestHooks hooksFor(Kind kind, const std::string& assetName, const std::string& tag = kTag) {
	updatesig::SelfTestHooks h;
	h.tag = tag;
	h.assetName = assetName;
	h.makeDir = [](const std::string& d) { return g_mkdir_with_parents(d.c_str(), 0700) == 0; };
	h.writeFile = [](const std::string& p, const std::string& b) { return g_file_set_contents(p.c_str(), b.data(), (gssize)b.size(), nullptr) == TRUE; };
	h.sha256Hex = sha256Bytes;
	h.join = joinPath;
	h.find = [kind, tag](const std::string& dir, const unsigned char key[32], const std::string& releaseCommit, const std::string& runningCommit,
	                     long long runningBuild) {
		auto fromDir = [&dir](const std::string& name, std::string& out) { return readWhole(joinPath(dir, name), out); };
		if (tag == kTag) return findVerified(fromDir, key, releaseCommit, runningCommit, runningBuild, kind);
		return findVerified(fromDir, key, tag, releaseCommit, runningCommit, runningBuild, [kind](const std::string& n) { return isReleaseFile(n, kind); },
		                    nullptr, nullptr);
	};
	h.verifyFile = verifyFile;
	return h;
}

// The .deb's root step with a test key and a stand-in for apt.
bool rootStepTest(const std::string& dir, std::string& report) {
	bool all = true;
	auto expect = [&](const std::string& label, bool ok, const std::string& detail) {
		report += std::string(ok ? "  ok   " : "  FAIL ") + label + (detail.empty() ? "" : " -- " + detail) + "\n";
		all = all && ok;
	};
	unsigned char seed[32], pub[32];
	for (int i = 0; i < 32; i++) seed[i] = (unsigned char)(i * 3 + 1);
	const std::string name = debName();
	std::string deb = "!<arch>\nthe new build's .deb, for the self-test\n";
	while (deb.size() < 2048) deb += "deb ";
	updatesig::Manifest m;
	m.tag = kTag;
	m.commit = std::string(40, '2');
	m.build = 2000;
	m.files.push_back({ name, (long long)deb.size(), sha256Bytes(deb) });
	const std::string manifest = updatesig::formatManifest(m);
	const std::string signature = updatesig::testSign(manifest, seed, pub);
	const std::string base = joinPath(dir, "root");
	g_mkdir_with_parents(base.c_str(), 0700);
	int n = 0;
	struct Ran { bool called = false; std::string copy, bytes; mode_t folderMode = 0; };
	auto run = [&](const std::string& debBytes, const std::string& man, const std::string& sig, const std::string& asName, long long running,
	               bool linkTheDeb, std::function<void(const std::string& debPath)> meanwhile, Ran& ran, std::string& why) {
		const std::string d = joinPath(dir, "user-" + std::to_string(++n));
		g_mkdir_with_parents(d.c_str(), 0700);
		const std::string debPath = joinPath(d, "cedarlogic-update.deb"), manPath = joinPath(d, "m"), sigPath = joinPath(d, "s");
		g_file_set_contents(manPath.c_str(), man.data(), (gssize)man.size(), nullptr);
		g_file_set_contents(sigPath.c_str(), sig.data(), (gssize)sig.size(), nullptr);
		if (linkTheDeb) {
			const std::string real = joinPath(d, "real.deb");
			g_file_set_contents(real.c_str(), debBytes.data(), (gssize)debBytes.size(), nullptr);
			if (symlink(real.c_str(), debPath.c_str()) != 0) return -1;
		} else {
			g_file_set_contents(debPath.c_str(), debBytes.data(), (gssize)debBytes.size(), nullptr);
		}
		auto apt = [&](const std::string& copy) {
			ran.called = true;
			ran.copy = copy;
			readWhole(copy, ran.bytes);
			gchar* folder = g_path_get_dirname(copy.c_str());
			struct stat st;
			if (stat(folder, &st) == 0) ran.folderMode = st.st_mode & 07777;
			g_free(folder);
			// What a swap of the user's file now would do: nothing to the copy apt has.
			if (meanwhile) meanwhile(debPath);
			std::string again;
			readWhole(copy, again);
			return again == ran.bytes ? 0 : 99;
		};
		return installVerifiedDeb(debPath, manPath, sigPath, asName, pub, running, base, apt, why);
	};
	{
		Ran ran;
		std::string why;
		const int code = run(deb, manifest, signature, name, 1000, false,
		                     [](const std::string& p) { g_file_set_contents(p.c_str(), "evil", 4, nullptr); }, ran, why);
		const bool inBase = ran.copy.compare(0, base.size() + 1, base + "/") == 0;
		expect("root step: a good .deb goes to apt as a private copy", code == 0 && ran.called && ran.bytes == deb && inBase && ran.folderMode == 0755,
		       why);
		gchar* folder = ran.copy.empty() ? nullptr : g_path_get_dirname(ran.copy.c_str());
		expect("root step: the private copy is gone afterwards", folder && !g_file_test(folder, G_FILE_TEST_EXISTS), "");
		g_free(folder);
	}
	{
		Ran ran;
		std::string why, swapped = deb;
		swapped[20] ^= 1;   // changed after the app checked it, before the root step
		const int code = run(swapped, manifest, signature, name, 1000, false, nullptr, ran, why);
		expect("root step: a .deb changed after the app's check is refused", code == kNotVerified && !ran.called, why);
	}
	{
		Ran ran;
		std::string why, bad = signature;
		bad[5] ^= 1;
		const int code = run(deb, manifest, bad, name, 1000, false, nullptr, ran, why);
		expect("root step: a bad signature is refused", code == kNotVerified && !ran.called, why);
	}
	{
		Ran ran;
		std::string why;
		const int code = run(deb, manifest, signature, name, 2000, false, nullptr, ran, why);
		expect("root step: a build no newer than the installed one is refused", code == kNotVerified && !ran.called, why);
	}
	{
		Ran ran;
		std::string why;
		const int code = run(deb, manifest, signature, "../" + name, 1000, false, nullptr, ran, why);
		expect("root step: a name that isn't this computer's .deb is refused", code == kNotVerified && !ran.called, why);
	}
	{
		Ran ran;
		std::string why;
		const int code = run(deb, manifest, signature, name, 1000, true, nullptr, ran, why);
		expect("root step: a symlink for the .deb is refused", code == kNotVerified && !ran.called, why);
	}
	// A Normal tester's: a versioned release's manifest and name.
	const std::string relName = std::string("cedarlogic_0.4.1_") + (cpuArch() == "aarch64" ? "arm64" : "amd64") + ".deb";
	auto releaseManifest = [&](const std::string& tag, const std::string& fileName, long long build) {
		updatesig::Manifest r;
		r.tag = tag;
		r.commit = std::string(40, '5');
		r.build = build;
		r.files.push_back({ fileName, (long long)deb.size(), sha256Bytes(deb) });
		return updatesig::formatManifest(r);
	};
	{
		Ran ran;
		std::string why;
		const std::string rm = releaseManifest("v0.4.1", relName, 2000);
		const int code = run(deb, rm, updatesig::testSign(rm, seed, pub), relName, 1000, false, nullptr, ran, why);
		expect("root step: a versioned release's .deb (v0.4.1) goes to apt", code == 0 && ran.called && ran.bytes == deb, why);
	}
	{
		Ran ran;
		std::string why;
		const std::string rm = releaseManifest("v0.4.1", relName, 900);
		const int code = run(deb, rm, updatesig::testSign(rm, seed, pub), relName, 1000, false, nullptr, ran, why);
		expect("root step: a versioned release no newer than the installed build is refused", code == kNotVerified && !ran.called, why);
	}
	{
		Ran ran;
		std::string why;
		const std::string rm = releaseManifest("v0.4.1", name, 2000);   // the test build's name in a release: not its names
		const std::string tm = releaseManifest(kTag, relName, 2000);    // a release's name under the test build's tag
		const int a = run(deb, tm, updatesig::testSign(tm, seed, pub), relName, 1000, false, nullptr, ran, why);
		std::string why2;
		Ran ran2;
		const std::string om = releaseManifest("someone-else", relName, 2000);
		const int b = run(deb, om, updatesig::testSign(om, seed, pub), relName, 1000, false, nullptr, ran2, why2);
		expect("root step: a release's name under the test build's tag, or another tag, is refused",
		       a == kNotVerified && !ran.called && b == kNotVerified && !ran2.called, why + "; " + why2);
		(void)rm;
	}
	return all;
}

void removeTree(const std::string& path) {
	if (g_file_test(path.c_str(), G_FILE_TEST_IS_DIR) && !g_file_test(path.c_str(), G_FILE_TEST_IS_SYMLINK)) {
		if (GDir* d = g_dir_open(path.c_str(), 0, nullptr)) {
			while (const gchar* n = g_dir_read_name(d)) removeTree(path + "/" + n);
			g_dir_close(d);
		}
		g_rmdir(path.c_str());
	} else {
		g_remove(path.c_str());
	}
}

// ---- --channel-check: the testing groups, against a stand-in server ------------
// The check writes GitHub's answers (the list of releases, a tag's commit,
// each release's files and signed list, signed with a test key) into `root`,
// which a plain static server (python3 -m http.server) serves at `server`;
// each case then runs the updater's own check, curl and all, through it.

struct ChannelCheck {
	std::string root, server;
	unsigned char seed[32], pub[32];
	int fails = 0, cases = 0;
	Endpoints ep;

	ChannelCheck(const std::string& r, const std::string& s) : root(r), server(s) {
		for (int i = 0; i < 32; i++) seed[i] = (unsigned char)(0x33 ^ (i * 5));
		updatesig::testSign("", seed, pub);
		ep.api = server + "/api";
		ep.download = server + "/dl";
	}
	void expect(const std::string& label, bool ok, const std::string& detail) {
		cases++;
		if (!ok) fails++;
		printf("%s %s%s\n", ok ? "  ok  " : "  FAIL", label.c_str(), detail.empty() ? "" : (" -- " + detail).c_str());
	}
	void write(const std::string& path, const std::string& bytes) {
		const std::string full = root + "/" + path;
		gchar* dir = g_path_get_dirname(full.c_str());
		g_mkdir_with_parents(dir, 0755);
		g_free(dir);
		g_file_set_contents(full.c_str(), bytes.data(), (gssize)bytes.size(), nullptr);
	}
	static std::string fileBytes(const std::string& name) { return "CedarLogic channel check: " + name + "\n"; }

	// One release on the stand-in: its files, its signed list (for
	// `signedTag`, normally its own), the commit its tag is on.
	struct Rel {
		std::string tag;
		bool prerelease = false;
		std::vector<std::string> names;
		long long build = 0;
		bool sign = true;
		std::string signedTag;      // "" -> tag
		bool badSignature = false;
	};
	std::string commitFor(const std::string& tag) { return sha256Bytes("commit " + tag).substr(0, 40); }
	void publish(const std::string& repo, const Rel& r) {
		const std::string base = "dl/" + repo + "/releases/download/" + r.tag + "/";
		updatesig::Manifest m;
		m.tag = r.signedTag.empty() ? r.tag : r.signedTag;
		m.commit = commitFor(r.tag);
		m.build = r.build;
		for (const std::string& n : r.names) {
			const std::string b = fileBytes(r.tag + "/" + n);
			write(base + n, b);
			m.files.push_back({ n, (long long)b.size(), sha256Bytes(b) });
		}
		if (r.sign) {
			const std::string text = updatesig::formatManifest(m);
			std::string sig = updatesig::testSign(text, seed, pub);
			if (r.badSignature) sig[9] ^= 0x04;
			write(base + updatesig::kManifestName, text);
			write(base + updatesig::kSignatureName, sig);
		}
		write("api/repos/" + repo + "/commits/" + r.tag, "{\"sha\":\"" + m.commit + "\",\"node_id\":\"x\",\"commit\":{\"tree\":{\"sha\":\"" +
		                                                     std::string(40, 'f') + "\"}}}");
	}
	// GitHub's list of releases, newest first as it is, with the keys in its order.
	void list(const std::vector<Rel>& rels) {
		std::string json = "[";
		int id = 100;
		for (const Rel& r : rels) {
			if (json.size() > 1) json += ",";
			++id;
			json += "{\"url\":\"https://api.github.com/repos/" + std::string(kReleasesRepo) + "/releases/" + std::to_string(id) +
			        "\",\"id\":" + std::to_string(id) + ",\"author\":{\"login\":\"someone\"},\"tag_name\":\"" + r.tag +
			        "\",\"target_commitish\":\"main\",\"name\":\"CedarLogic " + r.tag + "\",\"draft\":false,\"prerelease\":" +
			        (r.prerelease ? "true" : "false") + ",\"assets\":[";
			std::vector<std::string> names = r.names;
			if (r.sign) { names.push_back(updatesig::kManifestName); names.push_back(updatesig::kSignatureName); }
			for (size_t i = 0; i < names.size(); i++) {
				json += std::string(i ? "," : "") + "{\"name\":\"" + names[i] + "\",\"digest\":null,\"browser_download_url\":\"" +
				        releaseFileUrl(ep, kReleasesRepo, r.tag, names[i]) + "\"}";
			}
			// Release notes that look like keys, escaped as GitHub escapes them.
			json += "],\"body\":\"Fixes. \\\"tag_name\\\":\\\"v9.9\\\", \\\"prerelease\\\":false, \\\"browser_download_url\\\":\\\"" +
			        releaseFileUrl(ep, kReleasesRepo, "v9.9", "cedarlogic_9.9_amd64.deb") + "\\\"\"}";
		}
		write("api/repos/" + std::string(kReleasesRepo) + "/releases", json + "]");
	}
	std::string debFor(const std::string& v) { return "cedarlogic_" + v + "_" + (cpuArch() == "aarch64" ? "arm64" : "amd64") + ".deb"; }
	std::string appImageFor(const std::string& v) { return "CedarLogic-Native-" + v + "-" + cpuArch() + ".AppImage"; }
	std::vector<std::string> allFiles(const std::string& v) {
		return { "CedarLogic-" + v + "-Mac.dmg", "CedarLogic-" + v + "-Windows-Setup-x64.exe", "cedarlogic_" + v + "_amd64.deb", "cedarlogic_" + v + "_arm64.deb",
		         "CedarLogic-Native-" + v + "-x86_64.AppImage", "CedarLogic-Native-" + v + "-aarch64.AppImage" };
	}

	Outcome run(int channel, Kind kind, long long build, const std::string& version = "0.4.0", const std::string& installed = "") {
		Running me;
		me.commit = std::string(40, '1');
		me.build = build;
		me.version = version;
		me.installedRelease = installed;
		return checkChannel(channel, kind, me, pub, ep);
	}
	// The offered file downloaded through the stand-in and checked against the signed list.
	bool downloads(const Outcome& o, std::string& why) {
		const std::string dest = root + "/downloaded";
		std::remove(dest.c_str());
		if (!download(o.asset.url, dest)) { why = "couldn't download " + o.asset.url; return false; }
		return verifyFile(dest, o.asset.file, why);
	}

	void testBuild(long long build, bool badSignature = false) {
		Rel t;
		t.tag = kTag;
		t.names = { "cedarlogic_amd64.deb", "cedarlogic_arm64.deb", "CedarLogic-Native-0.4.0-x86_64.AppImage", "CedarLogic-Native-0.4.0-aarch64.AppImage",
		            "install.sh" };
		t.build = build;
		t.badSignature = badSignature;
		publish(kOwnerRepo, t);
	}

	int all() {
		const char* kinds[] = { ".deb", "AppImage" };
		// The usual state: the test build ahead of everything; v0.5.0 a
		// pre-release (a beta), newest by date; v0.4.2 a Mac-only fix; v0.4.1
		// and v0.4 full releases with Linux files; Classic 4.0.2, a
		// pre-release with a .deb of its own and no signed list.
		auto usual = [&](bool badRelease, const std::string& v041Tag) {
			testBuild(3000);
			Rel pre{ "v0.5.0", true, allFiles("0.5.0"), 2800 };
			Rel mac{ "v0.4.2", false, { "CedarLogic-0.4.2-Mac.dmg" }, 2600 };
			Rel r41{ "v0.4.1", false, allFiles("0.4.1"), 2000 };
			r41.badSignature = badRelease;
			r41.signedTag = v041Tag;
			Rel r4{ "v0.4", false, allFiles("0.4"), 1500 };
			Rel classic{ "v4.0.2", true, { "cedarlogic_4.0.2_amd64.deb", "cedarlogic_4.0.2_arm64.deb" }, 0, false };
			for (const Rel* r : { &pre, &mac, &r41, &r4 }) publish(kReleasesRepo, *r);
			write("api/repos/" + std::string(kReleasesRepo) + "/commits/v4.0.2", "{\"sha\":\"" + commitFor("v4.0.2") + "\"}");
			list({ pre, mac, classic, r4, r41 });   // (v0.4 listed before v0.4.1: newest by version, not by order)
		};
		usual(false, "");
		for (int k = 0; k < 2; k++) {
			const Kind kind = k == 0 ? Kind::Deb : Kind::AppImage;
			const std::string label = std::string(kinds[k]) + ": ";
			const std::string want = kind == Kind::Deb ? debFor("0.4.1") : appImageFor("0.4.1");
			Outcome n = run(kNormal, kind, 1000);
			std::string why;
			const bool got = n.decision.result == updatesig::Result::Offer && downloads(n, why);
			expect(label + "Normal takes the newest full release, v0.4.1 (not the v0.5.0 pre-release, not Mac-only v0.4.2)",
			       got && n.decision.file.name == want && n.asset.installed == "v0.4.1 2000" &&
			           n.asset.url == releaseFileUrl(ep, kReleasesRepo, "v0.4.1", want),
			       n.decision.why + (why.empty() ? "" : "; " + why));
			Outcome b = run(kBeta, kind, 1000);
			why.clear();
			const bool gotB = b.decision.result == updatesig::Result::Offer && downloads(b, why);
			expect(label + "Beta takes the test build", gotB && b.asset.installed.empty() && b.decision.manifest.build == 3000 &&
			                                                b.asset.url.find("/" + std::string(kTag) + "/") != std::string::npos,
			       b.decision.why + (why.empty() ? "" : "; " + why));
			// A Beta tester on a test build newer than the release switches to Normal.
			Outcome down = run(kNormal, kind, 2500);
			expect(label + "Beta to Normal on a newer test build: no downgrade", down.fetched && down.decision.result == updatesig::Result::UpToDate,
			       down.decision.why);
			Outcome older = run(kNormal, kind, 1000, "0.4.2");
			expect(label + "Normal never offers an older version than this one", older.decision.result == updatesig::Result::UpToDate,
			       older.decision.why);
			Outcome same = run(kNormal, kind, 1000, "0.4.1", "v0.4.1 2000");
			expect(label + "Normal doesn't offer the release this updater installed", same.decision.result == updatesig::Result::UpToDate,
			       same.decision.why);
			Outcome up = run(kBeta, kind, 3000);
			expect(label + "Beta on the test build itself: up to date", up.decision.result != updatesig::Result::Offer, up.decision.why);
		}
		// A bad signature, on the release and on the test build.
		usual(true, "");
		testBuild(3000, true);
		for (int k = 0; k < 2; k++) {
			const Kind kind = k == 0 ? Kind::Deb : Kind::AppImage;
			Outcome n = run(kNormal, kind, 1000), b = run(kBeta, kind, 1000);
			expect(std::string(kinds[k]) + ": a bad signature is refused (Normal, and not an older release instead)",
			       n.decision.result == updatesig::Result::NotVerified, n.decision.why);
			expect(std::string(kinds[k]) + ": a bad signature is refused (Beta)", b.decision.result == updatesig::Result::NotVerified, b.decision.why);
		}
		// A release's signed list for another tag (an older release's, or the test build's).
		for (const char* other : { "v0.4", kTag }) {
			usual(false, other);
			Outcome n = run(kNormal, Kind::Deb, 1000);
			expect(std::string("a signed list for ") + other + " on v0.4.1 is refused", n.decision.result == updatesig::Result::NotVerified, n.decision.why);
		}
		// Only pre-releases (and a Mac-only one): Normal has nothing, Beta still has the test build.
		{
			testBuild(3000);
			Rel pre{ "v0.5.0", true, allFiles("0.5.0"), 2800 };
			Rel mac{ "v0.4.2", false, { "CedarLogic-0.4.2-Mac.dmg" }, 2600 };
			publish(kReleasesRepo, pre);
			publish(kReleasesRepo, mac);
			list({ pre, mac });
			Outcome n = run(kNormal, Kind::Deb, 1000), a = run(kNormal, Kind::AppImage, 1000), b = run(kBeta, Kind::Deb, 1000);
			expect("Normal ignores pre-releases: nothing to offer", n.fetched && n.noRelease && n.decision.result == updatesig::Result::UpToDate &&
			                                                            a.noRelease,
			       n.decision.why);
			expect("Beta takes the test build all the same", b.decision.result == updatesig::Result::Offer, b.decision.why);
		}
		// A release with Linux files but no signed list yet: not offered.
		{
			Rel r{ "v0.4.1", false, allFiles("0.4.1"), 2000, false };
			publish(kReleasesRepo, r);
			list({ r });
			Outcome n = run(kNormal, Kind::Deb, 1000);
			expect("a release not signed yet isn't offered", n.decision.result != updatesig::Result::Offer, n.decision.why);
		}
		// The stand-in gone: "couldn't reach", not "up to date".
		{
			Endpoints real = ep;
			ep.api = server + "/nothing-here";
			Outcome n = run(kNormal, Kind::Deb, 1000), b = run(kBeta, Kind::Deb, 1000);
			ep = real;
			expect("no answer: a failed check, for both groups", !n.fetched && !b.fetched, n.decision.why + "; " + b.decision.why);
		}
		// Which group a copy starts in (Prefs.cpp).
		{
			const ChannelStart fresh = startingChannel(false, false, 0, false, "");
			expect("a new install starts as a Normal tester", fresh.channel == kNormal && !fresh.notice, "");
			const ChannelStart old = startingChannel(true, false, 0, false, "");
			expect("a copy that ran before (a test build's) becomes a Beta tester, and says so once", old.channel == kBeta && old.notice, "");
			const ChannelStart said = startingChannel(true, true, kBeta, false, "");
			expect("...and once it's said, stays a Beta tester without saying it again", said.channel == kBeta && !said.notice, "");
			const ChannelStart chose = startingChannel(true, true, kNormal, false, "beta");
			expect("a group chosen in Settings wins over install.sh's marker", chose.channel == kNormal && !chose.notice, "");
			const ChannelStart preview = startingChannel(false, false, 0, false, "beta"), release = startingChannel(true, false, 0, false, "normal");
			expect("install.sh --preview starts a Beta tester; its release install a Normal one", preview.channel == kBeta && !preview.notice &&
			                                                                                           release.channel == kNormal && !release.notice,
			       "");
		}
		// GitHub's list, read: tags, flags and files per release, notes ignored.
		{
			const std::vector<ListedRelease> rs = parseReleases(
				"[{\"tag_name\": \"v1\",\"draft\":false,\"prerelease\": true,\"assets\":[{\"browser_download_url\":\"https://x/a\"}],\"body\":\"\\\"tag_name\\\":\\\"v2\\\"\"},"
				"{\"tag_name\":\"v3\",\"draft\":true,\"prerelease\":false,\"assets\":[]}]");
			expect("GitHub's list of releases reads as it should",
			       rs.size() == 2 && rs[0].tag == "v1" && !rs[0].draft && rs[0].prerelease && rs[0].urls.size() == 1 && rs[0].urls[0] == "https://x/a" &&
			           rs[1].tag == "v3" && rs[1].draft && rs[1].urls.empty(),
			       format("%d releases", (int)rs.size()));
			std::vector<long> a, b, c;
			expect("versions: v0.4 == 0.4.0 < v0.4.1 < v0.10; v4.0.0-beta.1 isn't one",
			       parseVersion("v0.4", a) && parseVersion("0.4.0", b) && compareVersions(a, b) == 0 && parseVersion("v0.4.1", c) &&
			           compareVersions(b, c) < 0 && parseVersion("v0.10", a) && compareVersions(c, a) < 0 && !isReleaseTag("v4.0.0-beta.1") &&
			           !isReleaseTag(kTag) && !isReleaseTag("0.4"),
			       "");
			expect("this computer's names in a release", isReleaseFile(debFor("0.4"), Kind::Deb) && isReleaseFile(debName(), Kind::Deb) &&
			                                                 !isReleaseFile("cedarlogic_x_amd64.deb", Kind::Deb) &&
			                                                 isReleaseFile(appImageFor("0.4.0"), Kind::AppImage) &&
			                                                 isReleaseFile("CedarLogic-0.4-" + cpuArch() + ".AppImage", Kind::AppImage) &&
			                                                 !isReleaseFile("CedarLogic-0.4-Mac.dmg", Kind::AppImage),
			       "");
		}
		printf(fails == 0 ? "channel-check: all %d passed\n" : "channel-check: %d of %d failed\n", fails == 0 ? cases : fails, cases);
		return fails == 0 ? 0 : 1;
	}
};

}  // namespace

int Updater_ChannelCheck(int argc, char** argv) {
	std::string server, root;
	for (int i = 1; i + 1 < argc; i++) {
		if (strcmp(argv[i], "--server") == 0) server = argv[++i];
		else if (strcmp(argv[i], "--root") == 0) root = argv[++i];
	}
	if (server.compare(0, 17, "http://127.0.0.1:") != 0 || root.empty()) {
		fprintf(stderr, "usage: cedarlogic --channel-check --server http://127.0.0.1:<port> --root <the folder it serves>\n");
		return 2;
	}
	while (!server.empty() && server.back() == '/') server.pop_back();
	g_testServer = server;
	ChannelCheck c(root, server);
	return c.all();
}

int Updater_SelfTest(int argc, char** argv) {
	std::string release, key, tag = kTag;
	for (int i = 1; i + 1 < argc; i++) {
		if (strcmp(argv[i], "--release") == 0) release = argv[++i];
		else if (strcmp(argv[i], "--key") == 0) key = argv[++i];
		else if (strcmp(argv[i], "--tag") == 0) tag = argv[++i];
	}
	std::string report;
	bool ok = true;
	if (!release.empty()) {
		// A folder the signing script signed (with a key made for the test).
		report += "Signed folder " + release + "\n";
		report += "(for " + tag + ")\n";
		ok = updatesig::checkSignedFolder(release, key, hooksFor(Kind::Deb, debName(), tag), readWhole, report);
	} else {
		gchar* dir = g_dir_make_tmp("cl-update-test-XXXXXX", nullptr);
		if (!dir) { fprintf(stderr, "no temporary folder\n"); return 2; }
		report += "AppImage updates\n";
		ok = updatesig::selfTest(std::string(dir) + "/appimage", hooksFor(Kind::AppImage, "CedarLogic-Native-0.4.0-" + cpuArch() + ".AppImage"),
		                         report) && ok;
		report += ".deb updates\n";
		ok = updatesig::selfTest(std::string(dir) + "/deb", hooksFor(Kind::Deb, debName()), report) && ok;
		// A Normal tester's: the same cases for a versioned release.
		const std::string relDeb = std::string("cedarlogic_0.4.1_") + (cpuArch() == "aarch64" ? "arm64" : "amd64") + ".deb";
		report += "versioned release (v0.4.1) .deb updates\n";
		ok = updatesig::selfTest(std::string(dir) + "/release-deb", hooksFor(Kind::Deb, relDeb, "v0.4.1"), report) && ok;
		report += "versioned release (v0.4.1) AppImage updates\n";
		ok = updatesig::selfTest(std::string(dir) + "/release-appimage",
		                         hooksFor(Kind::AppImage, "CedarLogic-Native-0.4.1-" + cpuArch() + ".AppImage", "v0.4.1"), report) && ok;
		ok = rootStepTest(std::string(dir) + "/rootstep", report) && ok;
		removeTree(dir);
		g_free(dir);
	}
	fputs(report.c_str(), stdout);
	printf("update self-test (this build: %s, build %lld): %s\n", CL_GIT_COMMIT, (long long)CL_BUILD_TIME, ok ? "all passed" : "FAILED");
	return ok ? 0 : 1;
}

int Updater_InstallUpdateAsRoot(int argc, char** argv) {
	if (argc != 6) {
		fprintf(stderr, "E: CedarLogic: usage: cedarlogic --install-update <deb> <manifest> <signature> <name>\n");
		return kNotVerified;
	}
	if (geteuid() != 0) {
		fprintf(stderr, "E: CedarLogic: --install-update runs as root (through pkexec)\n");
		return kNotVerified;
	}
	std::string why;
	const int code = installVerifiedDeb(argv[2], argv[3], argv[4], argv[5], updatesig::kPublicKey, (long long)CL_BUILD_TIME, "/var/tmp",
	                                    aptInstall, why);
	if (code == kNotVerified) fprintf(stderr, "E: CedarLogic: the update didn't verify, not installing it: %s\n", why.c_str());
	return code;
}

void Updater_Initialize(GtkApplication* app) {
	if (g_timer) return;
	// The group decided at first run (Prefs.cpp's startingChannel) is kept.
	prefs().save();
	g_timer = g_timeout_add(8000, +[](gpointer app) -> gboolean {
		const bool canUpdate = (!runningAppImage().empty() || installedFromDeb()) && strcmp(CL_GIT_COMMIT, "unknown") != 0;
		if (prefs().checkUpdates && canUpdate) check(GTK_APPLICATION(app), false);
		else if (prefs().channelNotice && !circuitWindows().empty()) {
			takeChannelNotice();
			circuitWindows().front()->note("Updates: Beta tester (from the test builds). Settings › General to change it");
		}
		g_timeout_add_seconds(24 * 60 * 60, +[](gpointer app) -> gboolean {
			if (prefs().checkUpdates) check(GTK_APPLICATION(app), false);
			return G_SOURCE_CONTINUE;
		}, app);
		return G_SOURCE_REMOVE;
	}, app);
}

void Updater_CheckNow(GtkApplication* app) { check(app, true); }

void Updater_ChannelChanged(GtkApplication* app) {
	// Chosen, so nothing more to say about the group it was given.
	if (prefs().channelNotice) {
		prefs().channelNotice = false;
		prefs().save();
	}
	// As the Mac's: look again now, quietly (it asks only if there's something).
	if (prefs().checkUpdates) check(app, false);
}

const char* Updater_ChannelNotice() { return prefs().channelNotice ? kChannelNotice : nullptr; }

// Run as an AppImage: put it in the applications menu, with its icon, and
// make .cdl files open in it -- as an installed app would be. Written each
// launch, so a moved file is followed; skipped when the .deb is installed
// (that has its own entry), and taken away again then.
void Updater_IntegrateAppImage() {
	const std::string data = g_get_user_data_dir();
	const std::string apps = data + "/applications", icons = data + "/icons/hicolor/256x256/apps", mime = data + "/mime/packages";
	const std::string file = apps + "/cedarlogic.desktop";
	// Tell the desktop (quietly; whichever of these it has).
	auto refresh = [&] {
		const std::string mimeDir = data + "/mime", iconDir = data + "/icons/hicolor";
		const std::vector<std::vector<std::string>> commands = {
			{ "update-desktop-database", apps }, { "update-mime-database", mimeDir }, { "gtk-update-icon-cache", "-q", "-t", iconDir } };
		for (const auto& c : commands) {
			std::vector<gchar*> argv;
			for (const std::string& a : c) argv.push_back(const_cast<gchar*>(a.c_str()));
			argv.push_back(nullptr);
			// In the background, each from its argument list (no shell); a missing one is skipped.
			g_spawn_async(nullptr, argv.data(), nullptr, (GSpawnFlags)(G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL),
			              nullptr, nullptr, nullptr, nullptr);
		}
	};
	if (g_file_test("/usr/share/applications/cedarlogic.desktop", G_FILE_TEST_EXISTS)) {
		// The installed app's entry is hidden by one of the same name here
		// (the user's own wins), so one an AppImage wrote goes, with its icon
		// and file type: the menu and .cdl files then open the installed app,
		// not an AppImage that may be gone.
		gchar* was = nullptr;
		if (g_file_get_contents(file.c_str(), &was, nullptr, nullptr) && (strstr(was, "X-CedarLogic-AppImage=") || strstr(was, ".AppImage"))) {
			g_remove(file.c_str());
			g_remove((mime + "/cedarlogic-mime.xml").c_str());
			g_remove((icons + "/cedarlogic.png").c_str());
			refresh();
		}
		g_free(was);
		return;
	}
	const std::string image = runningAppImage();
	const char* appdir = std::getenv("APPDIR");
	if (image.empty() || appdir == nullptr) return;
	// The path as one quoted Exec argument (the Desktop Entry spec): \ before " ` $ and \, % as %%,
	// then every \ doubled for the key file's own escaping. A control character (a newline would
	// start a line of its own in the entry) can't be written at all, so no entry then.
	std::string quoted;
	for (char c : image) {
		if ((unsigned char)c < 0x20 || c == 0x7f) return;
		if (c == '"' || c == '`' || c == '$') quoted += "\\\\";
		else if (c == '\\') quoted += "\\\\\\";
		else if (c == '%') quoted += '%';
		quoted += c;
	}
	g_mkdir_with_parents(apps.c_str(), 0755);
	g_mkdir_with_parents(icons.c_str(), 0755);
	g_mkdir_with_parents(mime.c_str(), 0755);
	auto copy = [](const std::string& from, const std::string& to) {
		gchar* bytes = nullptr;
		gsize len = 0;
		if (!g_file_get_contents(from.c_str(), &bytes, &len, nullptr)) return;
		g_file_set_contents(to.c_str(), bytes, (gssize)len, nullptr);
		g_free(bytes);
	};
	copy(std::string(appdir) + "/usr/share/icons/hicolor/256x256/apps/cedarlogic.png", icons + "/cedarlogic.png");
	copy(std::string(appdir) + "/usr/share/mime/packages/cedarlogic-mime.xml", mime + "/cedarlogic-mime.xml");
	// Without FUSE, the menu and .cdl files start it the way this launch was
	// started, or each of them would fail silently.
	const std::string exec = std::string(extractedAndRun() ? "env APPIMAGE_EXTRACT_AND_RUN=1 " : "") + "\"" + quoted + "\" %U";
	const std::string entry = "[Desktop Entry]\nName=CedarLogic\nGenericName=Logic Simulator\nComment=Build and simulate digital logic circuits\n"
	                          "Type=Application\nExec=" + exec + "\nIcon=cedarlogic\nTerminal=false\n"
	                          "MimeType=application/x-cedarlogic-circuit;x-scheme-handler/cedarlogic;\nStartupWMClass=CedarLogic\n"
	                          "Categories=Education;Development;Electronics;\nKeywords=logic;simulator;circuit;gates;\n"
	                          "X-CedarLogic-AppImage=true\n";
	gchar* was = nullptr;
	const bool same = g_file_get_contents(file.c_str(), &was, nullptr, nullptr) && entry == was;
	g_free(was);
	if (same) return;
	g_file_set_contents(file.c_str(), entry.c_str(), -1, nullptr);
	refresh();
}
