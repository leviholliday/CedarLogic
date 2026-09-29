// The first-run welcome and the guided tour (the wx and Mac apps' Welcome),
// simplified: what CedarLogic is, the keys worth knowing, and a short tour
// that points at the palette, the canvas, the run button and the tabs.

#ifndef CL_LINUX_WELCOME_H
#define CL_LINUX_WELCOME_H

#include "App.h"

class CircuitWindow;

// Shown once, the first time the app has a window open (tracked in
// prefs().hasSeenWelcome). Offers the guided tour on its last page.
void offerWelcome(GtkApplication* app, CircuitWindow* window);

// Help > Guided Tour: runs it again on demand, without the welcome pages.
void startTour(CircuitWindow* window);

#endif  // CL_LINUX_WELCOME_H
