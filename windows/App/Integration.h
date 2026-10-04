// CedarLogic as a normal Windows app (see Integration.cpp): in the Start
// menu, and opening .cdl files when they're double-clicked (and the website's
// cedarlogic:// links, ShareLink.h). The installer
// (windows/installer/CedarLogic.iss) does this for an installed copy; a copy
// run from the zip asks once, and does it for itself.

#ifndef CL_WINDOWS_INTEGRATION_H
#define CL_WINDOWS_INTEGRATION_H

#include "App.h"
#include <string>

class CircuitWindow;

namespace integration {

// This copy was put here by the installer (Settings > Apps removes it).
bool installed();
// At launch, for a copy run from the zip: one added to Start that has since
// moved follows itself there, and the question is asked once, when nothing
// else is in the way (not over the welcome, the tour or a dialog). A copy with
// Start and .cdl files that came before cedarlogic:// links (updated in place)
// registers them too. `now` (CI's --dialog start-menu): asked at once,
// whatever was answered before. An installed copy updated from inside the app
// tells Settings > Apps its new version.
void start(bool now = false);
// The ••• menu's Help > Add to Start Menu / Remove from Start Menu: there
// for a copy run from the zip, while no installed CedarLogic is about.
bool offered();
std::string menuLabel();
void menuCommand(CircuitWindow* window);
// Settings > General's Start menu switch (shown while offered()): whether
// this copy is in Start and opens .cdl files, and putting it there or
// taking it out at once, without the question. False when Windows wouldn't
// let it.
bool inStartMenu();
bool setInStartMenu(bool on);
// --start-menu-test (CI): adds this copy, checks Start and what opens .cdl
// files, removes it and checks it's gone, then puts back what was there. A
// PASS or FAIL line each, in `report`; false if any failed.
bool selfTest(std::string& report);

}  // namespace integration

#endif  // CL_WINDOWS_INTEGRATION_H
