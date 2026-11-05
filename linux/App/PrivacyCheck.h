// The checks for crash reports' and usage stats' consent and the usage
// stats engine (docs/PRIVACY.md), as the Windows app's PrivacyCheck.h:
//
//   cedarlogic --usage-check    UsageReport.h against mac/Tools/usage-check/server.py
//                               (CL_USAGE_URL=http://127.0.0.1:<port>): the contract,
//                               snapshot, delta, ping, haveSnapshot false, kept while
//                               it's down (7 at most), 429, retries, DELETE, policy off,
//                               the caps, no Origin, the 0600 file, the leak scan
//   cedarlogic --usage-live     the same engine against the site's live draft
//                               (CL_USAGE_URL=https://usage-stats-test--cedarlogic.netlify.app),
//                               a throwaway id: snapshot, delta, ping, DELETE
//   cedarlogic --privacy-check  the consent in real windows on an X display (Xvfb), clicked
//                               with the real pointer (xdotool): the welcome's page (No
//                               thanks), the updaters' sheet after What's New and before
//                               Show Me Around (Yes), Escape, Choose what to send, See
//                               exactly what's sent, asked once, each answer in native.ini.
//                               With CL_EXPECT_POLICY=1 (and CL_POLICY_FILE saying both are
//                               off): nobody asked, nothing counted, Settings greyed out.
//
// Each prints PASS/FAIL lines and returns 0 when all passed.

#ifndef CL_LINUX_PRIVACY_CHECK_H
#define CL_LINUX_PRIVACY_CHECK_H

class CircuitWindow;

namespace privacycheck {
int usageCheck();
int usageLive();
int consentCheck(CircuitWindow* w);
}  // namespace privacycheck

#endif  // CL_LINUX_PRIVACY_CHECK_H
