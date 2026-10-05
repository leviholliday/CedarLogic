// Sync in the app (docs/SYNC.md): the engine from mac/CedarCore, started at
// launch with this machine's hooks (SyncPlatform) and the app's windows behind
// them -- saving open circuits first, holding a change that would land on
// unsaved work, reloading a window in place when a circuit changes, and the
// questions, notes and status the person sees. The Settings page and the
// sheets are in SyncUI.cpp.

#ifndef CL_LINUX_SYNCAPP_H
#define CL_LINUX_SYNCAPP_H

#include "App.h"
#include "Sync.h"

#include <functional>
#include <string>

namespace syncapp {

// Make the engine (once, at launch, after the gate library loaded), so Settings has something
// to show; begin() then starts syncing if a code is stored. The screenshot runs make it and
// don't begin, unless CL_SYNC_URL names a test server.
void start(GtkApplication* app);
void begin();
// At quit, every window closed and saved: sends what's unsent (5 seconds at most), then stops.
void quit();

// The engine, or null before start() / in a run that doesn't sync.
clsync::Engine* engine();
bool enabled();
// Whether it's syncing now or not: the status as the engine has it.
clsync::Status status();
// The last thing sync told the person (a conflict kept, a delete arrived...), for a minute.
std::string recentNotice();

// ---- Told by the app ------------------------------------------------------------------------
// Something in Your Circuits changed (a save, a rename, an import, a delete, a restored version).
void libraryChanged();
// The person did something in a window (keeps polling going while they work).
void userInput();
// A cedarlogic://sync#k=... link: Settings > Sync with that code, and its preview.
void handleLink(const std::string& link);

// ---- Told to the app ------------------------------------------------------------------------------
// Called on the main thread when the status changed, and when sync changed the library.
// Returns an id for remove().
guint addStatusListener(std::function<void()> fn);
guint addLibraryListener(std::function<void()> fn);
void removeListener(guint id);

// The gate library's default for a parameter (for the engine's structure digest).
std::string gateDefault(const std::string& lib, bool gui, const std::string& name);

}  // namespace syncapp

#endif  // CL_LINUX_SYNCAPP_H
