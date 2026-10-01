// Recovery copies of unsaved work (see Recovery.cpp).

#ifndef CL_WINDOWS_RECOVERY_H
#define CL_WINDOWS_RECOVERY_H

#include "App.h"
#include <string>

class CircuitWindow;

namespace recovery {

// A name for one window's copy, unique to this process.
std::string newBase();
// Write (or replace) a copy: the circuit's text, where it was saved (or "")
// and what it's called.
bool write(const std::string& base, const std::string& text, const std::string& path, const std::string& name);
// Remove a copy (a clean close, or a save that made it unneeded).
void remove(const std::string& base);
// At launch: offer back the copies a CedarLogic that's no longer running
// left behind. The first comes back into `reuse` when that's an untouched
// new window.
void offer(CircuitWindow* reuse);

}  // namespace recovery

#endif  // CL_WINDOWS_RECOVERY_H
