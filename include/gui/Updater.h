/*****************************************************************************
   Project: CEDAR Logic Simulator

   Updater: the one place the rest of the app talks to for updates, whatever
   does the work underneath -- Sparkle on macOS, WinSparkle on Windows, and our
   own AppImage updater on Linux.

   Every copy follows one of two feeds, picked in Settings > General >
   Testing group:
     normal tester  appcast.xml       releases published as normal
     beta tester    appcast-beta.xml  those plus beta builds, which come first
   Both live in the public releases repo; scripts/publish-update.sh writes them.
*****************************************************************************/

#pragma once

// The feed for the channel chosen in settings (appSettings.updateChannel).
const char* updateFeedUrl();

// "Check for Updates..." -- shows its result, including "you're up to date".
void Updater_CheckNow();

// The channel setting changed: point the running updater at the other feed
// and look there straight away.
void Updater_ChannelChanged();

// From MainApp::OnExit. Relaunches the app if an update asked to restart.
void Updater_OnExit();

#if !defined(__APPLE__) && !defined(_WIN32)
// Start the Linux updater's background checks (shortly after launch, then
// daily). Only the AppImage can replace itself; other builds get a link.
void LinuxUpdater_Initialize();
#endif
