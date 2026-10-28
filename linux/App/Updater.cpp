// The AppImage and .deb updater (see Updater.h).
//
//   1. ask GitHub for the commit the "linux-native-testing" tag points at now
//   2. compare it with CL_GIT_COMMIT, baked in at build time
//   3. different -> fetch the release's SHA256SUMS and SHA256SUMS.sig and
//      check them (update-signing/UpdateSignature.h): signed with the preview
//      key built in here, for this tag and the tag's commit, a build newer
//      than this one (CL_BUILD_TIME), with one file for this CPU and kind.
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

#include "UpdateSignature.h"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>
#include <thread>

#ifndef CL_GIT_COMMIT
#define CL_GIT_COMMIT "unknown"
#endif
// The commit's committer time (Unix seconds): the build number the signed
// manifest's must be above. 0 when it isn't known (a build outside git).
#ifndef CL_BUILD_TIME
#define CL_BUILD_TIME 0
#endif

namespace {

const char* kOwnerRepo = "leviholliday/CedarLogic";
const char* kTag = "linux-native-testing";

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
// wget). The URL is ours and quoted; one with a quote in it is refused, and
// so is anything but https. Neither reads the user's ~/.curlrc or ~/.wgetrc,
// and curl won't follow a redirect off https either. (wget has no switch for
// that; what it brings is still checked against the signed manifest.)
bool fetchable(const std::string& url) {
	return url.compare(0, 8, "https://") == 0 && url.find('\'') == std::string::npos;
}
const char* const kCurl = "curl -q -fsSL --proto =https --proto-redir =https";
const char* const kWget = "wget --no-config -q";

std::string fetch(const std::string& url) {
	if (!fetchable(url)) return {};
	const std::string cmds[] = {
		std::string(kCurl) + " --max-time 15 -H 'User-Agent: CedarLogic' '" + url + "' 2>/dev/null",
		std::string(kWget) + " -T 15 -U CedarLogic -O - '" + url + "' 2>/dev/null",
	};
	for (const std::string& cmd : cmds) {
		FILE* p = popen(cmd.c_str(), "r");
		if (!p) continue;
		std::string body;
		char chunk[8192];
		size_t got;
		while (body.size() < (1u << 20) && (got = fread(chunk, 1, sizeof chunk, p)) > 0) body.append(chunk, got);
		if (pclose(p) == 0 && !body.empty()) return body;
	}
	return {};
}

bool download(const std::string& url, const std::string& dest) {
	if (!fetchable(url) || dest.find('\'') != std::string::npos) return false;
	const std::string cmds[] = {
		std::string(kCurl) + " --max-time 300 -o '" + dest + "' '" + url + "' 2>/dev/null",
		std::string(kWget) + " -T 120 -O '" + dest + "' '" + url + "' 2>/dev/null",
	};
	for (const std::string& cmd : cmds) {
		if (std::system(cmd.c_str()) == 0) return true;
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

// The commit this repo's testing tag points at right now, or "" on failure.
std::string latestCommit() {
	const std::string body = fetch(format("https://api.github.com/repos/%s/commits/%s", kOwnerRepo, kTag));
	std::string sha;
	// The very first "sha" field in the response is the commit's own (every
	// nested one -- the tree, its parents -- comes later in GitHub's JSON).
	if (body.empty() || !jsonString(body, "sha", sha)) return {};
	return sha;
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

std::string releaseFileUrl(const std::string& name) {
	return format("https://github.com/%s/releases/download/%s/%s", kOwnerRepo, kTag, name.c_str());
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
};

// The release's manifest and signature through `get` (the release's
// download URLs, or a folder in the self-test), and the decision on them.
updatesig::Decision findVerified(const std::function<bool(const std::string& name, std::string& out)>& get, const unsigned char key[32],
                                 const std::string& releaseCommit, const std::string& runningCommit, long long runningBuild, Kind kind,
                                 Asset* out) {
	std::string manifest, signature;
	bool haveManifest = false, haveSignature = false;
	if (releaseCommit != runningCommit) {
		haveManifest = get(updatesig::kManifestName, manifest);
		haveSignature = haveManifest && get(updatesig::kSignatureName, signature);
	}
	const updatesig::Decision d = updatesig::decide(haveManifest, manifest, haveSignature, signature, key, kTag, releaseCommit, runningCommit,
	                                                runningBuild, [kind](const std::string& n) { return isOurs(n, kind); });
	if (out && d.result == updatesig::Result::Offer) {
		out->url = releaseFileUrl(d.file.name);
		out->file = d.file;
		out->manifest = manifest;
		out->signature = signature;
	}
	return d;
}

bool fetchReleaseFile(const std::string& name, std::string& out) {
	if (!updatesig::safeName(name)) return false;
	out = fetch(releaseFileUrl(name));
	return !out.empty();
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
	if (!updatesig::safeName(name) || !isOurs(name, Kind::Deb)) { why = "\"" + name + "\" isn't this computer's .deb"; return kNotVerified; }
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
	if (m.tag != kTag) { why = "the manifest is for " + m.tag; return done(kNotVerified); }
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

// The .deb: downloaded and checked, then installed through pkexec (which
// asks for the password in the desktop's own dialog) by this program's own
// root step, which checks it all again on a copy only root can change.
void installDeb(GtkApplication* app, const Asset& asset) {
	GtkWindow* parent = gtk_application_get_active_window(app);
	const std::string page = format("https://github.com/%s/releases/tag/%s", kOwnerRepo, kTag);
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
			openExternally(parent, format("https://github.com/%s/releases/tag/%s", kOwnerRepo, kTag));
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

	if (askConfirm(parent, "The update is installed", "Restart CedarLogic now to use it? Your circuits are saved.", "Restart Now", "Later")) {
		if (quitApp(app)) relaunch(current);
	}
}

// Everything a background check produces, polled for on the GTK thread (only
// std::atomic<bool> done is touched by both sides at once).
struct CheckResult {
	GtkApplication* app;
	bool interactive;
	std::thread worker;
	std::atomic<bool> done{ false };
	bool fetched = false;
	std::string sha;
	updatesig::Decision decision;
	Asset asset;
};

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
	const bool newer = r->fetched && r->decision.result == updatesig::Result::Offer;
	if (!newer) {
		if (r->interactive) {
			if (!r->fetched)
				showMessage(parent, GTK_MESSAGE_WARNING, "Couldn't check for updates",
				            "CedarLogic couldn't reach the update feed. Check your connection and try again.");
			else if (r->decision.result == updatesig::Result::NotVerified)
				showMessage(parent, GTK_MESSAGE_INFO, "No verified update yet",
				            "There's a newer test build, but it hasn't been signed yet, so CedarLogic won't install it. Try again later.");
			else
				showMessage(parent, GTK_MESSAGE_INFO, "You're up to date", "This is the latest version.");
		}
		delete r;
		return G_SOURCE_REMOVE;
	}
	if (!r->interactive) {
		if (g_offeredThisRun == r->sha) { delete r; return G_SOURCE_REMOVE; }
		g_offeredThisRun = r->sha;
	}
	if (askConfirm(parent, "A new version of CedarLogic is available", "Download and install it now? It takes a moment, and CedarLogic restarts after.",
	               "Install", "Not Now"))
		install(r->app, r->asset);
	delete r;
	return G_SOURCE_REMOVE;
}

void check(GtkApplication* app, bool interactive) {
	// A copy that can't update itself (built from source) isn't told about
	// every new test build at launch; Check for Updates still says.
	if (!interactive && ((runningAppImage().empty() && !installedFromDeb()) || strcmp(CL_GIT_COMMIT, "unknown") == 0)) return;
	if (g_checking.exchange(true)) return;
	CheckResult* r = new CheckResult{ app, interactive };
	r->worker = std::thread([r] {
		const std::string sha = latestCommit();
		r->fetched = !sha.empty();
		if (r->fetched) {
			r->sha = sha;
			r->decision = findVerified(fetchReleaseFile, updatesig::kPublicKey, sha, CL_GIT_COMMIT, (long long)CL_BUILD_TIME,
			                           installedFromDeb() ? Kind::Deb : Kind::AppImage, &r->asset);
			logUpdate(r->decision.why);
		}
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

updatesig::SelfTestHooks hooksFor(Kind kind, const std::string& assetName) {
	updatesig::SelfTestHooks h;
	h.tag = kTag;
	h.assetName = assetName;
	h.makeDir = [](const std::string& d) { return g_mkdir_with_parents(d.c_str(), 0700) == 0; };
	h.writeFile = [](const std::string& p, const std::string& b) { return g_file_set_contents(p.c_str(), b.data(), (gssize)b.size(), nullptr) == TRUE; };
	h.sha256Hex = sha256Bytes;
	h.join = joinPath;
	h.find = [kind](const std::string& dir, const unsigned char key[32], const std::string& releaseCommit, const std::string& runningCommit,
	                long long runningBuild) {
		auto fromDir = [&dir](const std::string& name, std::string& out) { return readWhole(joinPath(dir, name), out); };
		return findVerified(fromDir, key, releaseCommit, runningCommit, runningBuild, kind, nullptr);
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

}  // namespace

int Updater_SelfTest(int argc, char** argv) {
	std::string release, key;
	for (int i = 1; i + 1 < argc; i++) {
		if (strcmp(argv[i], "--release") == 0) release = argv[++i];
		else if (strcmp(argv[i], "--key") == 0) key = argv[++i];
	}
	std::string report;
	bool ok = true;
	if (!release.empty()) {
		// A folder the signing script signed (with a key made for the test).
		report += "Signed folder " + release + "\n";
		ok = updatesig::checkSignedFolder(release, key, hooksFor(Kind::Deb, debName()), readWhole, report);
	} else {
		gchar* dir = g_dir_make_tmp("cl-update-test-XXXXXX", nullptr);
		if (!dir) { fprintf(stderr, "no temporary folder\n"); return 2; }
		report += "AppImage updates\n";
		ok = updatesig::selfTest(std::string(dir) + "/appimage", hooksFor(Kind::AppImage, "CedarLogic-Native-0.4.0-" + cpuArch() + ".AppImage"),
		                         report) && ok;
		report += ".deb updates\n";
		ok = updatesig::selfTest(std::string(dir) + "/deb", hooksFor(Kind::Deb, debName()), report) && ok;
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
	g_timer = g_timeout_add(8000, +[](gpointer app) -> gboolean {
		if (prefs().checkUpdates) check(GTK_APPLICATION(app), false);
		g_timeout_add_seconds(24 * 60 * 60, +[](gpointer app) -> gboolean {
			if (prefs().checkUpdates) check(GTK_APPLICATION(app), false);
			return G_SOURCE_CONTINUE;
		}, app);
		return G_SOURCE_REMOVE;
	}, app);
}

void Updater_CheckNow(GtkApplication* app) { check(app, true); }

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
		const std::string cmd = "(update-desktop-database '" + apps + "'; update-mime-database '" + data + "/mime'; "
		                        "gtk-update-icon-cache -q -t '" + data + "/icons/hicolor') >/dev/null 2>&1 &";
		if (data.find('\'') == std::string::npos) (void)std::system(cmd.c_str());
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
