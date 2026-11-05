// Crash reports, through Sentry (the Mac's mac/App/CrashReports.swift), into
// the same project as the Mac and Linux apps (CMake's CL_SENTRY_DSN, the
// Mac's address), tagged platform=windows. sentry-native is built by CI
// (.github/workflows/windows-native.yml) and linked in when CMake is given
// it (CL_SENTRY_DIR); without it crash reports are "not in this build".
//
// A report says where the app was when it crashed: the stack (addresses in
// the app's modules, so the developer can find the line), the Windows and
// CedarLogic versions. Never a circuit, a file or a name. sentry-native's
// in-process backend is used, not crashpad: crashpad's minidump would carry
// raw stack memory and the modules' full paths (C:\Users\<name>\...), which
// can't be cleaned before they go. Here the report is an ordinary event,
// and before it goes (scrub) every path loses its user name, the user and
// computer names go wherever they appear, and there's no trail of clicks
// or network requests, no session tracking and no user. It's written when
// the app crashes and sent the next time CedarLogic opens, if crash
// reports are still on.
//
// Asked about once, with usage stats, in one consent (Privacy.h); Settings >
// Privacy turns them on or off (off takes effect the next time CedarLogic
// opens); a lab turns them off for everyone with the policy
// HKLM\Software\Policies\CedarLogic\DisableCrashReporting (DWORD 1).

#ifndef CL_WINDOWS_CRASH_REPORTS_H
#define CL_WINDOWS_CRASH_REPORTS_H

#include <functional>
#include <string>

namespace crashreports {

// This build has sentry-native and an address to send to.
bool inBuild();
// The lab's policy says no.
bool policyOff();
// Crash reports can be turned on: in this build, and not turned off by policy.
bool available();
// What was chosen: -1 not asked yet, 0 no, 1 yes (prefs().crashReports).
int allowed();
// At launch, after the settings are read and before anything else can go
// wrong: Sentry starts if they're available and allowed. Never in a test
// or picture run (`testRun`).
void start(bool testRun);
bool started();
// Settings or the consent: on or off. On starts them now; off takes effect
// the next time CedarLogic opens (a report already being written still goes).
void set(bool on);
// At quit: what's waiting is written out.
void shutdown();

// The cleaning: a report's text (JSON) with every C:\Users\<name>\ (and
// /Users/<name>/, and as JSON escapes them) made <user>, and this user's
// and this PC's names taken out wherever they are.
std::string scrubbed(const std::string& text);

// --crash-check: the cleaning on a made-up report full of this PC's paths
// and names, that Sentry starts (and stops) in this build, the policy, and
// that a test run never starts it. 0 when all passed.
int selfTest(const std::function<void(const std::string&)>& out);

}  // namespace crashreports

#endif  // CL_WINDOWS_CRASH_REPORTS_H
