// Updates for the AppImage: nothing like Sparkle exists for Linux, so this is
// a small one of our own. An AppImage is a single file, and the running one
// can be replaced in place (the process keeps the old file open until it
// exits), then relaunched.
//
// This testing build has no version numbers yet -- CI republishes the same
// rolling "linux-native-testing" release on every push to linux/native -- so
// "newer" means "a different commit than the one this copy was built from",
// read from the tag's current commit (GitHub's API), not a semver compare.
// Once real numbered releases exist, this can move to the appcast format the
// rest of CedarLogic already uses (include/gui/UpdateInfo.h) for one shared
// update mechanism; for a rolling test channel this is simpler and matches
// what CI actually publishes.

#ifndef CL_LINUX_UPDATER_H
#define CL_LINUX_UPDATER_H

#include "App.h"

// Start background checks: once a few seconds after launch, then daily.
// Offers each new commit once per run; never nags on a failed fetch.
void Updater_Initialize(GtkApplication* app);

// "Check for Updates" -- reports its result either way, including "you're up
// to date" and "couldn't reach the update feed".
void Updater_CheckNow(GtkApplication* app);

// Run as an AppImage: add it to the applications menu with its icon.
void Updater_IntegrateAppImage();

#endif  // CL_LINUX_UPDATER_H
