// The parts that introduce CedarLogic, in its own look whatever the theme
// (the Mac's BrandUI): the launch screen, the first-run welcome, and the
// guided tour that points at the palette, the canvas, Run and the tabs.

#ifndef CL_WINDOWS_WELCOME_H
#define CL_WINDOWS_WELCOME_H

#include "App.h"
#include <functional>

class CircuitWindow;

namespace splash {
// Show the launch screen (it animates until hidden).
void show();
bool active();
void setStatus(const char* status);
// Hide it once it's been up long enough to see, then run `then`.
void hideSoon(std::function<void()> then);
}  // namespace splash

namespace welcome {
// Shown once, the first time the app has a window open (prefs().hasSeenWelcome).
// Offers the guided tour on its last page. False when it wasn't shown.
bool offer(CircuitWindow* window);
// Help > Guided Tour: the tour again, without the welcome pages.
void startTour(CircuitWindow* window);
}  // namespace welcome

#endif  // CL_WINDOWS_WELCOME_H
