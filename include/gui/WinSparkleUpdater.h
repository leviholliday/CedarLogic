/*****************************************************************************
   Project: CEDAR Logic Simulator
   WinSparkleUpdater: Windows auto-update support via WinSparkle library
*****************************************************************************/

#ifndef WINSPARKLEUPDATER_H
#define WINSPARKLEUPDATER_H

#ifdef _WIN32

// Initialize WinSparkle (call once at app startup)
void WinSparkleUpdater_Initialize();

// Check for updates (shows UI)
void WinSparkleUpdater_CheckForUpdates();

// The testing group changed: restart WinSparkle on the other feed (the URL can
// only be set before win_sparkle_init) and check it quietly.
void WinSparkleUpdater_ChannelChanged();

// Cleanup WinSparkle (call before app exit)
void WinSparkleUpdater_Cleanup();

#endif // _WIN32

#endif // WINSPARKLEUPDATER_H
