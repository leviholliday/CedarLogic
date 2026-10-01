// Updates for the test build (see Updater.cpp).

#ifndef CL_WINDOWS_UPDATER_H
#define CL_WINDOWS_UPDATER_H

#include "App.h"

namespace updater {

// At launch: tidy up after a finished update, and check again in a while.
void start();
// Help > Check for Updates: says so when there's nothing new.
void checkNow(HWND parent);

}  // namespace updater

#endif  // CL_WINDOWS_UPDATER_H
