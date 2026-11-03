// Updates for the AppImage and the .deb: nothing like Sparkle exists for
// Linux, so this is a small one of our own. An AppImage is a single file, and
// the running one can be replaced in place (the process keeps the old file
// open until it exits), then relaunched; the .deb goes to apt through pkexec.
//
// Two testing groups, as on the Mac (Settings > General > Testing group):
//
//   Beta tester    the rolling "linux-native-testing" release in
//                  leviholliday/CedarLogic, which CI republishes on every push
//                  to linux/native. "Newer" is a different commit from this
//                  copy's (CL_GIT_COMMIT) with a newer signed build.
//   Normal tester  the newest full release v<version> (v0.4, v0.4.1...) in
//                  leviholliday/CedarLogic-Releases, the Mac's releases repo,
//                  that has this computer's file (its .deb, or its AppImage)
//                  and a signed list (packaging/sign-release.sh). Pre-releases
//                  (the Mac's betas, Classic) and drafts never count.
//
// Either way the same signed SHA256SUMS: for that release's own tag and the
// commit it's on, a build newer than this one (CL_BUILD_TIME), so switching
// from Beta to Normal never goes back to an older build. A Normal tester is
// also never offered an older version than CL_VERSION, nor the release its
// updater already installed (Prefs::installedRelease).
//
// The group a copy starts in (Prefs.cpp, startingChannel): the one chosen in
// Settings; else what linux/install.sh wrote in ~/.config/CedarLogic/
// install-channel (beta for --preview, normal for a release); else, for a
// copy that ran before there was a choice (every one of those came from the
// test build), Beta, and it says so once; else (a new install) Normal.

#ifndef CL_LINUX_UPDATER_H
#define CL_LINUX_UPDATER_H

#include "App.h"

// Start background checks: once a few seconds after launch, then daily.
// Offers each new commit once per run; never nags on a failed fetch.
void Updater_Initialize(GtkApplication* app);

// "Check for Updates" -- reports its result either way, including "you're up
// to date" and "couldn't reach the update feed".
void Updater_CheckNow(GtkApplication* app);

// Settings > General > Testing group changed: look again now, quietly.
void Updater_ChannelChanged(GtkApplication* app);

// The once-only word about the group a test build's copy was given, while
// it's still to be said (Settings shows it under the row too); else null.
const char* Updater_ChannelNotice();

// Run as an AppImage: add it to the applications menu with its icon.
void Updater_IntegrateAppImage();

// Updates are taken only when signed (update-signing/UpdateSignature.h):
// a build on the release is offered once its SHA256SUMS, signed with the
// update key, names the release's tag, the tag's commit and a newer build,
// and the download matches it.

// --update-self-test [--release <folder> --key <hex>]: the update check's
// own code against fake releases in a temporary folder (good, bad signature,
// stale manifest, wrong hash, downgrade...), and the .deb's root step with a
// stand-in for apt; or, given a folder the signing script signed, that one.
int Updater_SelfTest(int argc, char** argv);

// --channel-check --server http://127.0.0.1:<port> --root <folder>: the two
// testing groups through the updater's own check, curl and all, against a
// static stand-in for GitHub that serves <folder> (python3 -m http.server):
// Normal ignores pre-releases, Beta takes them, no downgrade on a switch,
// the group a copy starts in, bad signatures refused.
int Updater_ChannelCheck(int argc, char** argv);

// --install-update <deb> <manifest> <signature> <name>, as root through
// pkexec: the .deb's install, checked again on a copy only root can write.
int Updater_InstallUpdateAsRoot(int argc, char** argv);

#endif  // CL_LINUX_UPDATER_H
