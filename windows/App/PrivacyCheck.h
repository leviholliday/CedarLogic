// The checks of crash reports and usage stats (docs/PRIVACY.md), the Windows
// twin of the Mac's mac/Tools/usage-check.sh and its launch-selftest
// `privacy` state:
//
//   --usage-check   the engine (UsageReport.h) against a stand-in for the
//                   site's /api/usage, mac/Tools/usage-check/server.py
//                   (CL_USAGE_URL=http://127.0.0.1:<port>): snapshot first, a
//                   delta with only what changed, a ping when nothing did,
//                   haveSnapshot false forcing a snapshot, reports kept (7 at
//                   most) while it's down, 429 waiting a day, nothing counted
//                   or sent when off or turned off by policy, DELETE on
//                   opt-out (and later, when it was offline), the 16 KB and
//                   200-action caps, JSON with no Origin header, the file only
//                   this user can open, and the leak scan of real reports
//                   (the profile folder, user and computer names, recent
//                   files, the student's name, the e-mail on feedback).
//                   USAGE_SAMPLES=<file>: one report of each kind written there.
//   --usage-live    snapshot, delta, ping and DELETE against a real server
//                   (CL_USAGE_URL: the site or one of its Netlify drafts) with
//                   a throwaway id, and haveSnapshot false after the DELETE.
//   --privacy-check the consent in real, visible windows with real input
//                   (SendInput): the welcome's "A small favour" page (no
//                   Continue until it's answered, No thanks clicked), the
//                   updaters' sheet in a real launch's order (never over What's
//                   New, before Show Me Around; Yes clicked), Escape, "Choose
//                   what to send" with its switches and Continue with These,
//                   "See exactly what's sent" opening and closing, asked once,
//                   and the settings saved in native.ini each time. With
//                   CL_EXPECT_POLICY=1 (the policy keys set by CI): nothing is
//                   offered, nothing counted, the sheet is never owed, and
//                   Settings > Privacy has both switches greyed out.
//
// Each in a home folder of its own (CEDARLOGIC_HOME): your settings are
// never touched. One PASS or FAIL line each; the exit code is 0 when all passed.

#ifndef CL_WINDOWS_PRIVACY_CHECK_H
#define CL_WINDOWS_PRIVACY_CHECK_H

#include <functional>
#include <string>

namespace privacycheck {

using Out = std::function<void(const std::string&)>;

// A home folder of its own (CEDARLOGIC_HOME), before the settings are read.
void prepare(const char* name);
void cleanUp();

int usageCheck(const Out& out);
int usageLive(const Out& out);
// With the gate library loaded and the window classes registered (from a timer).
int consentCheck(const Out& out);

}  // namespace privacycheck

#endif  // CL_WINDOWS_PRIVACY_CHECK_H
