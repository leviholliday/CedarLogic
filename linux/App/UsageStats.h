// Usage stats in the app (UsageReport.h is the engine; docs/PRIVACY.md says
// what's sent), as the Mac's mac/App/UsageStats.swift and the Windows app's
// windows/App/UsageStats.h: the switch, the counting -- launches, time
// CedarLogic is in front and being used (a key, a click or the pointer in the
// last 5 minutes), time in focus mode and Simulation View, the commands run
// -- and the send at launch, in the background, once a day at most. Nothing
// is counted while the switch is off or a lab's policy file
// (/etc/cedarlogic/policy.ini: DisableUsageStats=true) turns it off.
//
// Commands are counted where every one of them is dispatched (the window's
// GActions: the menus, the keys, the toolbar, the Actions panel), by the id
// the action registry gives it (actions::byAction, else the shortcut list's):
// never a menu's text, so a file name in Open Recent can't be counted.

#ifndef CL_LINUX_USAGE_STATS_H
#define CL_LINUX_USAGE_STATS_H

#include "UsageReport.h"

#include <functional>
#include <string>

namespace usagestats {

usage::Engine& engine();
// The app as it is now (its settings, sync), read on the main thread.
usage::Now nowFromApp();
// A test or picture run: it sends nowhere (set by main before anything else).
extern bool testRun;

bool policyOff();
// The switch, as Settings and the consent show it.
bool on();
// The switch (the consent, Settings > Privacy). Off tells the server to
// delete what it has, in the background.
void set(bool on);

// Once the windows are up, in real launches only: the day's report if one's
// due, this launch counted, a look every 5 seconds at what's in front, the
// file written every 5 minutes and at quit (shutdown).
void start();
void shutdown();

// A window's command (its GAction's name, "save"), counted by its registry id.
void countAction(const char* gaction);

// "See exactly what's sent".
std::string preview();
// "4 Nov 2026, 18:05 (snapshot)", or "" when nothing has gone yet.
std::string lastSent();
int kept();
// Settings > Privacy > Delete what's been sent, in the background; `then`
// on the main thread: true done, false the server couldn't be reached
// (it's asked again the next time CedarLogic opens).
void deleteSent(std::function<void(bool done)> then);
// Something changed (the switch, a send): Settings > Privacy reads it again.
void onChange(std::function<void()> f);

}  // namespace usagestats

#endif  // CL_LINUX_USAGE_STATS_H
