// Usage stats in the app (UsageReport.h is the engine; docs/PRIVACY.md says
// what's sent), as the Mac's mac/App/UsageStats.swift: the switch, the
// counting -- launches, time CedarLogic is in front and the PC isn't idle,
// time in focus mode and Simulation View, the commands run -- and the send
// at launch, in the background, once a day at most. Nothing is counted
// while the switch is off or a lab's policy (DisableUsageStats) turns it off.
//
// Commands are counted where every one of them is dispatched,
// CircuitWindow::run (the menus, the keys, the toolbar, the Actions panel),
// by the id the action registry gives it (actions::forCommand, else
// shortcuts::forCommand): never a menu's text, so a file name in Open
// Recent or the Window menu can't be counted. One command counts once
// however many runs it passes through (Counting).

#ifndef CL_WINDOWS_USAGE_STATS_H
#define CL_WINDOWS_USAGE_STATS_H

#include "UsageReport.h"

#include <functional>
#include <string>

namespace usagestats {

usage::Engine& engine();
// The app as it is now (its settings, sync), read on the window thread.
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

// A command, counted once even when one run calls another.
struct Counting {
	explicit Counting(int command);
	~Counting();
};

// "See exactly what's sent".
std::string preview();
// "4 Nov 2026, 18:05 (snapshot)", or "" when nothing has gone yet.
std::string lastSent();
int kept();
// Settings > Privacy > Delete What's Been Sent, in the background; `then`
// on the window thread: true done, false the server couldn't be reached
// (it's asked again the next time CedarLogic opens).
void deleteSent(std::function<void(bool done)> then);
// Something changed (the switch, a send): Settings > Privacy reads it again.
void onChange(std::function<void()> f);

}  // namespace usagestats

#endif  // CL_WINDOWS_USAGE_STATS_H
