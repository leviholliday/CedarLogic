// The AppImage updater (see Updater.h).
//
//   1. ask GitHub for the commit the "linux-native-testing" tag points at now
//   2. compare it with CL_GIT_COMMIT, baked in at build time
//   3. different -> ask; yes -> download this CPU's AppImage next to the
//      running one, check its size against what GitHub reported, make it
//      executable and rename it over the old file
//   4. offer to restart, through the app's normal Quit (so unsaved work is
//      asked about first) -- then relaunch the new file

#include "Updater.h"
#include "Alert.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <thread>

#ifndef CL_GIT_COMMIT
#define CL_GIT_COMMIT "unknown"
#endif

namespace {

const char* kOwnerRepo = "leviholliday/CedarLogic";
const char* kTag = "linux-native-testing";

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
// wget). The URL is ours and quoted; one with a quote in it is refused.
std::string fetch(const std::string& url) {
	if (url.find('\'') != std::string::npos) return {};
	const std::string cmds[] = {
		"curl -fsSL --max-time 15 -H 'User-Agent: CedarLogic' '" + url + "' 2>/dev/null",
		"wget -q -T 15 -U CedarLogic -O - '" + url + "' 2>/dev/null",
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
	if (url.find('\'') != std::string::npos || dest.find('\'') != std::string::npos) return false;
	const std::string cmds[] = {
		"curl -fsSL --max-time 300 -o '" + dest + "' '" + url + "' 2>/dev/null",
		"wget -q -T 120 -O '" + dest + "' '" + url + "' 2>/dev/null",
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

long long jsonNumber(const std::string& json, const std::string& key, size_t beforePos) {
	const std::string needle = "\"" + key + "\":";
	// GitHub lists an asset's "size" before its "browser_download_url", so
	// the nearest one *before* the url's own start is this asset's, not the
	// next one's.
	size_t at = json.rfind(needle, beforePos);
	if (at == std::string::npos) return 0;
	return strtoll(json.c_str() + at + needle.size(), nullptr, 10);
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

// This CPU's AppImage in the release, or a download url of "" if the release
// or the asset can't be found.
struct Asset { std::string url; long long size = 0; };
Asset findAsset(const std::string& suffix) {
	const std::string body = fetch(format("https://api.github.com/repos/%s/releases/tags/%s", kOwnerRepo, kTag));
	Asset out;
	size_t at = 0, start = 0, end = 0;
	std::string url;
	while (jsonString(body, "browser_download_url", url, at, &end, &start)) {
		at = end;
		if (url.size() >= suffix.size() && url.compare(url.size() - suffix.size(), suffix.size(), suffix) == 0) {
			out.url = url;
			out.size = jsonNumber(body, "size", start);
			return out;
		}
	}
	return out;
}

std::atomic<bool> g_checking{false};
std::string g_offeredThisRun;   // a background check offers each commit once
guint g_timer = 0;

// The download, with a small pulsing dialog while a background thread works
// (network calls off the GTK thread; only the poll touches widgets).
bool installWithProgress(GtkWindow* parent, const std::string& url, const std::string& dest) {
	// A plain window, not a GtkDialog: there are no buttons for the user to
	// press while this runs.
	GtkWidget* d = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(d), "Updating CedarLogic");
	gtk_window_set_modal(GTK_WINDOW(d), TRUE);
	gtk_window_set_transient_for(GTK_WINDOW(d), parent);
	gtk_window_set_destroy_with_parent(GTK_WINDOW(d), TRUE);
	gtk_window_set_resizable(GTK_WINDOW(d), FALSE);
	gtk_window_set_deletable(GTK_WINDOW(d), FALSE);
	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_set_border_width(GTK_CONTAINER(box), 16);
	gtk_container_add(GTK_CONTAINER(d), box);
	GtkWidget* label = gtk_label_new("Downloading the update…");
	GtkWidget* bar = gtk_progress_bar_new();
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 4);
	gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 4);
	gtk_widget_set_size_request(d, 320, -1);
	gtk_widget_show_all(d);

	std::atomic<bool> done{false};
	bool ok = false;
	std::thread worker([&] { ok = download(url, dest); done.store(true); });
	while (!done.load()) {
		gtk_progress_bar_pulse(GTK_PROGRESS_BAR(bar));
		while (gtk_events_pending()) gtk_main_iteration();
		g_usleep(80000);
	}
	worker.join();
	gtk_widget_destroy(d);
	return ok;
}

// The .deb: downloaded, then installed by apt through pkexec (which asks for
// the password in the desktop's own dialog).
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
	struct stat st;
	if (!installWithProgress(parent, asset.url, file) || stat(file.c_str(), &st) != 0 || (asset.size > 0 && st.st_size != asset.size)) {
		std::remove(file.c_str());
		g_free(pkexec);
		showMessage(parent, GTK_MESSAGE_WARNING, "The update couldn't be downloaded", "Check your connection and try again.");
		return;
	}
	// apt reads the file as its own user: make it readable.
	chmod(dir.c_str(), 0755);
	chmod(file.c_str(), 0644);
	gchar* argv[] = { pkexec, (gchar*)"apt-get", (gchar*)"install", (gchar*)"-y", (gchar*)"--allow-downgrades",
	                  const_cast<gchar*>(file.c_str()), nullptr };
	gint status = 1;
	GError* e = nullptr;
	const bool ran = g_spawn_sync(nullptr, argv, nullptr, G_SPAWN_STDOUT_TO_DEV_NULL, nullptr, nullptr, nullptr, nullptr, &status, &e);
	if (e) g_error_free(e);
	g_free(pkexec);
	std::remove(file.c_str());
	if (!ran || !g_spawn_check_exit_status(status, nullptr)) {
		if (askConfirm(parent, "The update wasn't installed",
		               "It needs your password to install. Open the download page to install it by hand?", "Open Page", "Not Now"))
			openExternally(parent, page);
		return;
	}
	if (askConfirm(parent, "The update is installed", "Restart CedarLogic now to use it? Your circuits are saved.", "Restart Now", "Later")) {
		if (quitApp(app)) {
			gchar* again[] = { (gchar*)"/usr/bin/cedarlogic", nullptr };
			g_spawn_async(nullptr, again, nullptr, (GSpawnFlags)0, nullptr, nullptr, nullptr, nullptr);
		}
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

	const bool ok = installWithProgress(parent, asset.url, temp);
	struct stat st;
	const bool sizeOk = ok && stat(temp.c_str(), &st) == 0 && (asset.size <= 0 || st.st_size == asset.size);
	if (!sizeOk) {
		std::remove(temp.c_str());
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
		if (quitApp(app)) {
			gchar* argv[] = { const_cast<gchar*>(current.c_str()), nullptr };
			GError* e = nullptr;
			if (!g_spawn_async(nullptr, argv, nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr, &e) && e)
				g_error_free(e);   // the file is already updated; a manual relaunch still gets it
		}
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
	Asset asset;
};

gboolean pollCheck(gpointer data) {
	CheckResult* r = static_cast<CheckResult*>(data);
	if (!r->done.load()) return G_SOURCE_CONTINUE;
	r->worker.join();
	g_checking.store(false);
	GtkWindow* parent = gtk_application_get_active_window(r->app);
	const bool newer = r->fetched && !r->asset.url.empty();
	if (!newer) {
		if (r->interactive) {
			if (!r->fetched)
				showMessage(parent, GTK_MESSAGE_WARNING, "Couldn't check for updates",
				            "CedarLogic couldn't reach the update feed. Check your connection and try again.");
			else
				showMessage(parent, GTK_MESSAGE_INFO, "You're up to date", "This is the latest test build.");
		}
		delete r;
		return G_SOURCE_REMOVE;
	}
	if (!r->interactive) {
		if (g_offeredThisRun == r->sha) { delete r; return G_SOURCE_REMOVE; }
		g_offeredThisRun = r->sha;
	}
	if (askConfirm(parent, "A new test build is available", "Download and install it now? It takes a moment, and CedarLogic restarts after.",
	               "Install", "Not Now"))
		install(r->app, r->asset);
	delete r;
	return G_SOURCE_REMOVE;
}

void check(GtkApplication* app, bool interactive) {
	if (g_checking.exchange(true)) return;
	CheckResult* r = new CheckResult{ app, interactive };
	r->worker = std::thread([r] {
		const std::string sha = latestCommit();
		r->fetched = !sha.empty();
		if (r->fetched && sha.compare(0, strlen(CL_GIT_COMMIT), CL_GIT_COMMIT) != 0) {
			r->sha = sha;
			r->asset = findAsset(installedFromDeb() ? (cpuArch() == "aarch64" ? "_arm64.deb" : "_amd64.deb") : "-" + cpuArch() + ".AppImage");
		}
		r->done.store(true);
	});
	g_timeout_add(150, pollCheck, r);
}

}  // namespace

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
// (that has its own entry).
void Updater_IntegrateAppImage() {
	const std::string image = runningAppImage();
	const char* appdir = std::getenv("APPDIR");
	if (image.empty() || appdir == nullptr) return;
	if (g_file_test("/usr/share/applications/cedarlogic.desktop", G_FILE_TEST_EXISTS)) return;
	const std::string data = g_get_user_data_dir();
	const std::string apps = data + "/applications", icons = data + "/icons/hicolor/256x256/apps", mime = data + "/mime/packages";
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
	std::string quoted;
	for (char c : image) {
		if (c == '"' || c == '\\' || c == '`' || c == '$') quoted += '\\';
		quoted += c;
	}
	const std::string entry = "[Desktop Entry]\nName=CedarLogic\nGenericName=Logic Simulator\nComment=Build and simulate digital logic circuits\n"
	                          "Type=Application\nExec=\"" + quoted + "\" %F\nIcon=cedarlogic\nTerminal=false\n"
	                          "MimeType=application/x-cedarlogic-circuit;\nStartupWMClass=CedarLogic\n"
	                          "Categories=Education;Development;Electronics;\nKeywords=logic;simulator;circuit;gates;\n";
	const std::string file = apps + "/cedarlogic.desktop";
	gchar* was = nullptr;
	const bool same = g_file_get_contents(file.c_str(), &was, nullptr, nullptr) && entry == was;
	g_free(was);
	if (same) return;
	g_file_set_contents(file.c_str(), entry.c_str(), -1, nullptr);
	// Tell the desktop (quietly; whichever of these it has).
	const std::string cmd = "(update-desktop-database '" + apps + "'; update-mime-database '" + data + "/mime'; "
	                        "gtk-update-icon-cache -q -t '" + data + "/icons/hicolor') >/dev/null 2>&1 &";
	if (apps.find('\'') == std::string::npos) (void)std::system(cmd.c_str());
}
