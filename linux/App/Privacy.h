// One consent for crash reports and usage stats (UsageStats.h), asked once
// (the Mac's mac/App/Privacy.swift, as the Windows app has it): on the
// welcome's "A small favour" page for a new install, or as a sheet over the
// circuit window for everyone else (after What's New, before Show Me
// Around, never over the launch screen or the intro). Settings > Privacy
// changes it later, shows what's sent and deletes it. A lab's policy file,
// /etc/cedarlogic/policy.ini (DisableCrashReporting=true,
// DisableUsageStats=true), switches either off for everyone and greys it
// out. docs/PRIVACY.md has the same words as "See exactly what's sent".
//
// Crash reports aren't in the Linux app yet (no crash reporter is built
// in): the question is the same, and the answer is kept (crashReports in
// native.ini) for the version that has one, but nothing is sent.
//
// The welcome's page and the sheet are drawn in Welcome.cpp (the brand's
// look and its panels); the rest is here.

#ifndef CL_LINUX_PRIVACY_H
#define CL_LINUX_PRIVACY_H

#include "App.h"

#include <string>
#include <vector>

class CircuitWindow;

namespace privacy {

enum class Choice { None, Yes, No, Custom };

// The words (the Mac's PrivacyCopy).
extern const char* const kEyebrow;     // "Help improve CedarLogic" (shown in capitals)
extern const char* const kTitle;       // "A small favour"
extern const char* const kNote;
extern const char* const kSignature;   // "Levi"
extern const char* const kYes;
extern const char* const kNo;
extern const char* const kLater;
extern const char* const kCrashLine;
extern const char* const kUsageLine;
extern const char* const kIdLine;
const std::vector<std::string>& neverSent();

// Asked (prefs().privacyAsked), and what was chosen (prefs().privacyChoice).
bool asked();
Choice choice();
const char* choiceKey(Choice c);   // "yes", "no", "custom", ""

bool crashInBuild();   // a crash reporter in this build (not yet on Linux)
bool crashOffered();   // in this build, not turned off by policy
bool crashPolicyOff();
bool usageOffered();   // not turned off by policy
bool usagePolicyOff();
bool anythingToAsk();
// Crash reports: on (1), off (0) or never chosen (-1); usage stats' switch.
int crashAllowed();
bool usageOn();

// The answer, from the welcome, the sheet or Settings. "No thanks" leaves
// crash reports on for someone who'd said yes to them before.
void record(Choice c, bool crash, bool usage);
// Yes, No thanks, or the two switches as they're set.
void answer(Choice c, bool crash = false, bool usage = false);

// The sheet is owed: someone past the welcome who hasn't been asked.
bool owed();

// At launch (and as the welcome closes): watches for the moment a circuit
// window is in front and taking input, with no launch screen, intro,
// welcome, What's New, lesson or dialog over it (showme::readyWindow), and
// asks there. Never in test and picture runs (`quiet`), unless a check says so.
void askWhenReady();
bool watching();
void stopWatching();
// Show Me Around waits for this: the question comes first.
bool holdsShowMeAround();
extern bool quiet;

// The sheet (Welcome.cpp): over the circuit window, the brand's look, 640
// points wide (less on a small screen, where it scrolls) and as tall as the
// taller of its two ways (Yes / No thanks, or the two switches). Escape is
// No thanks. Answered, it closes and Show Me Around follows (the first time).
void presentSheet(CircuitWindow* over);
bool sheetOpen();
void closeSheet();
// The welcome's "A small favour" page or the sheet, whichever is up: its
// window, and a control's middle on the screen (for --privacy-check's real
// clicks; it's scrolled into view first); false when it isn't showing.
enum Control { CtlYes, CtlNo, CtlChoose, CtlSeeSent, CtlCrashSwitch, CtlUsageSwitch, CtlContinue };
GtkWidget* askWindow();
bool controlPoint(Control c, int& screenX, int& screenY);
// The two switches showing ("Choose what to send"), and their state, as drawn.
bool choosing();
bool switchState(bool crash);
// For pictures: the switches showing (or not) on the question that's up.
void setChoosing(bool on);

// "See exactly what's sent": the next usage report exactly as it would go
// now, pretty-printed, what crash reports would carry, and in plain words
// what's never sent. Over `owner`; Done (or Escape) closes it.
void showWhatsSent(GtkWindow* owner);
// Its window, while it's up (for checks and pictures).
GtkWidget* whatsSentWindow();

// Settings > Privacy's lines.
std::string crashHint();
std::string usageHint();
std::string lastSentLine();

}  // namespace privacy

#endif  // CL_LINUX_PRIVACY_H
