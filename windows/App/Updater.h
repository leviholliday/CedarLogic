// Updates: beta testers get the test build, normal testers the newest
// versioned release (see Updater.cpp).

#ifndef CL_WINDOWS_UPDATER_H
#define CL_WINDOWS_UPDATER_H

#include "App.h"

namespace updater {

// At launch: tidy up after a finished update, and check again in a while.
void start();
// Help > Check for Updates: says so when there's nothing new.
void checkNow(HWND parent);
// Quitting: let go of a check that's still running.
void shutdown();

// Settings > General > Testing group (prefs().betaTester) changed: look
// again, in the background, for the new group.
void groupChanged();
// How the last check went, in a sentence, for Settings ("" before one).
// Until then, for a copy from the test builds made a beta tester, why.
std::string status();
// That reason has been shown: not again.
void noticeSeen();

// Updates are taken only when signed (update-signing/UpdateSignature.h):
// a build on the release is offered once its SHA256SUMS, signed with the
// preview key, names this tag, the tag's commit and a newer build, and the
// zip downloaded matches it.
//
// --update-self-test [--release <folder> --key <hex>]: the update check's
// own code against fake releases in a temporary folder (good, bad signature,
// stale manifest, wrong hash, downgrade...); or, given a folder the signing
// script signed, that one. True when everything did what it should.
bool runSelfTest(std::string& report, const std::string& release, const std::string& key);

// --channel-check --server <url> --root <folder>: the testing groups through
// the real check's code, over HTTP, against windows/Tools/
// update-channel-server.py serving <folder> at <url> (as GitHub's API and
// downloads): normal testers pass over pre-releases, beta testers take them,
// no going back when switching, the group a copy from the test builds gets,
// and bad signatures refused.
bool runChannelCheck(std::string& report, const std::string& server, const std::string& root);

}  // namespace updater

#endif  // CL_WINDOWS_UPDATER_H
