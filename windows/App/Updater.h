// Updates for the test build (see Updater.cpp).

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

}  // namespace updater

#endif  // CL_WINDOWS_UPDATER_H
