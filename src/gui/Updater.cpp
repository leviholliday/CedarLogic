/*****************************************************************************
   Project: CEDAR Logic Simulator

   Updater: see Updater.h
*****************************************************************************/

#include "Updater.h"

#include "CedarLogic.h"   // feed URLs
#include "Settings.h"
#include "UpdateInfo.h"
#include "UiControls.h"
#include "../version.h"

#ifdef __APPLE__
#include "SparkleUpdater.h"
#endif
#ifdef _WIN32
#include "WinSparkleUpdater.h"
#endif

#include <wx/app.h>
#include <wx/utils.h>

#if !defined(__APPLE__) && !defined(_WIN32)
#include "MainApp.h"
#include "MainFrame.h"
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/progdlg.h>
#include <wx/timer.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

DECLARE_APP(MainApp)
#endif

const char* updateFeedUrl() {
	return appConfig().appSettings.updateChannel == 1 ? CEDARLOGIC_APPCAST_BETA_URL
	                                                  : CEDARLOGIC_APPCAST_URL;
}

#if !defined(__APPLE__) && !defined(_WIN32)
// ---- Linux ------------------------------------------------------------------
//
// Nothing like Sparkle exists for Linux, so this is a small one of our own for
// the AppImage: an AppImage is a single file, and the running one can be
// replaced in place (the process keeps the old file open until it exits).
//
//   1. read the feed for this channel, pick the newest "linux" item for this CPU
//   2. download it next to the running AppImage
//   3. check its SHA-256 against the feed (fetched over HTTPS from GitHub)
//   4. make it executable and rename it over the old file
//   5. offer to restart
namespace {

bool g_relaunch = false;
bool g_checking = false;
wxTimer* g_timer = nullptr;
std::string g_offeredThisRun;   // a background check offers each version once

std::string cpuArch() {
	struct utsname u;
	if (uname(&u) != 0) return "x86_64";
	std::string m = u.machine;
	if (m == "arm64") return "aarch64";
	return m;
}

std::string runningAppImage() {
	const char* p = std::getenv("APPIMAGE");
	return p ? std::string(p) : std::string();
}

wxString channelName() {
	return appConfig().appSettings.updateChannel == 1 ? "beta" : "normal";
}

// Run `work` on a thread while a progress window pulses. The thread touches no
// wx objects.
template <class F>
void runWithProgress(const wxString& title, const wxString& text, F work) {
	std::atomic<bool> done(false);
	std::thread t([&] { work(); done.store(true); });
	wxWindow* parent = wxGetApp().mainframe;
	wxProgressDialog dlg(title, text, 100, parent, wxPD_APP_MODAL | wxPD_AUTO_HIDE);
	while (!done.load()) {
		dlg.Pulse();
		wxMilliSleep(50);
		wxTheApp->Yield(true);
	}
	t.join();
}

void install(const cl::update::FeedItem& item) {
	wxWindow* parent = wxGetApp().mainframe;
	const std::string current = runningAppImage();
	if (current.empty()) {
		// A build run from source or a tarball: nothing to replace in place.
		if (ui::Message("This copy of CedarLogic isn't the AppImage, so it can't update "
		                "itself. Open the download page?",
		                "Update available", wxYES_NO | wxICON_INFORMATION, parent) == wxYES)
			wxLaunchDefaultBrowser(CEDARLOGIC_RELEASES_URL);
		return;
	}
	if (item.sha256.size() != 64) {
		ui::Message("The update feed has no checksum for this download, so it can't be "
		            "verified. Nothing was changed.",
		            "Update not installed", wxOK | wxICON_WARNING, parent);
		return;
	}

	const std::string dir = std::string(wxFileName(wxString::FromUTF8(current)).GetPath().ToUTF8());
	const std::string temp = dir + "/.CedarLogic-update-" + std::to_string(getpid());
	bool ok = false;
	std::string gotHash;
	runWithProgress("Updating CedarLogic", "Downloading the update...", [&] {
		ok = cl::update::downloadFile(item.url, temp);
		if (ok) gotHash = cl::update::sha256File(temp);
	});

	if (!ok) {
		std::remove(temp.c_str());
		ui::Message("The update couldn't be downloaded. Check your connection and try "
		            "again. (CedarLogic needs curl or wget, and permission to write to " +
		            wxString::FromUTF8(dir) + ".)",
		            "Update failed", wxOK | wxICON_WARNING, parent);
		return;
	}
	if (gotHash != item.sha256) {
		std::remove(temp.c_str());
		ui::Message("The download didn't match the update feed's checksum, so it was "
		            "thrown away. Nothing was changed.",
		            "Update failed", wxOK | wxICON_WARNING, parent);
		return;
	}
	if (chmod(temp.c_str(), 0755) != 0 || std::rename(temp.c_str(), current.c_str()) != 0) {
		std::remove(temp.c_str());
		ui::Message("The update was downloaded but couldn't replace " +
		            wxString::FromUTF8(current) + ". Check that you can write to that folder.",
		            "Update failed", wxOK | wxICON_WARNING, parent);
		return;
	}

	if (ui::Message("CedarLogic " + wxString::FromUTF8(item.shortVersion) +
	                " is installed. Restart now to use it?",
	                "Update installed", wxYES_NO | wxICON_INFORMATION, parent) == wxYES) {
		g_relaunch = true;
		// Close the normal way, so unsaved work is asked about first. If the
		// user cancels that, don't relaunch.
		if (MainFrame* f = wxGetApp().mainframe) {
			if (!f->Close()) g_relaunch = false;
		}
	}
}

void check(bool interactive) {
	if (g_checking) return;
	g_checking = true;
	const std::string url = updateFeedUrl();
	const std::string arch = cpuArch();

	std::thread([url, arch, interactive] {
		std::string xml = cl::update::fetchAppcast(url);
		cl::update::FeedItem item;
		bool found = !xml.empty() && cl::update::appcastLatestItem(xml, "linux", arch, item);
		bool fetched = !xml.empty();
		wxTheApp->CallAfter([=] {
			g_checking = false;
			wxWindow* parent = wxGetApp().mainframe;
			const cl::update::Version current = cl::update::parseVersion(VERSION_NUMBER());
			const bool newer = found && cl::update::newerThan(item.version, current);
			if (!newer) {
				if (!interactive) return;
				if (!fetched)
					ui::Message("CedarLogic couldn't reach the update feed. Check your "
					            "connection and try again.",
					            "Couldn't check for updates", wxOK | wxICON_WARNING, parent);
				else
					ui::Message("You have the latest version (" + wxString(VERSION_NUMBER()) +
					            ", " + channelName() + " tester).",
					            "You're up to date", wxOK | wxICON_INFORMATION, parent);
				return;
			}
			std::string shown = item.shortVersion.empty() ? "a new version" : item.shortVersion;
			if (!interactive) {
				if (g_offeredThisRun == shown) return;
				g_offeredThisRun = shown;
			}
			if (ui::Message("CedarLogic " + wxString::FromUTF8(shown) + " is available (you have " +
			                wxString(VERSION_NUMBER()) + "). Download and install it now?",
			                "Update available", wxYES_NO | wxICON_INFORMATION, parent) == wxYES)
				install(item);
		});
	}).detach();
}

}  // namespace

void LinuxUpdater_Initialize() {
	if (g_timer) return;
	g_timer = new wxTimer();
	g_timer->Bind(wxEVT_TIMER, [](wxTimerEvent&) {
		check(false);
		g_timer->Start(24 * 60 * 60 * 1000);   // then once a day
	});
	g_timer->StartOnce(8000);   // not during startup
}
#endif

void Updater_CheckNow() {
#ifdef __APPLE__
	SparkleUpdater_CheckForUpdates();
#elif defined(_WIN32)
	WinSparkleUpdater_CheckForUpdates();
#else
	check(true);
#endif
}

void Updater_ChannelChanged() {
	if (cl::update::checksDisabled()) return;
#ifdef __APPLE__
	SparkleUpdater_ChannelChanged();
#elif defined(_WIN32)
	WinSparkleUpdater_ChannelChanged();
#else
	g_offeredThisRun.clear();
	check(false);
#endif
}

void Updater_OnExit() {
#if !defined(__APPLE__) && !defined(_WIN32)
	delete g_timer;
	g_timer = nullptr;
	if (g_relaunch) {
		const std::string app = runningAppImage();
		if (!app.empty()) wxExecute(wxString::FromUTF8(app), wxEXEC_ASYNC);
	}
#endif
}
