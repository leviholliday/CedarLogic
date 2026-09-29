// A brief launch screen, shown while the app starts up (matters most on
// slower machines -- a Raspberry Pi -- where parsing the gate library and
// the first window taking shape isn't instant). Mirrors the Mac app's
// Splash.swift, much simplified.

#ifndef CL_LINUX_SPLASH_H
#define CL_LINUX_SPLASH_H

#include "App.h"

// Shows the splash and returns it; paints at once (pumps the loop once) so
// it's visible before any slow startup work runs on the same thread.
GtkWidget* showSplash();

// Keeps it on top for at least a short, fixed time from when it was shown
// (so a fast launch doesn't just flash it), then destroys it.
void hideSplashSoon(GtkWidget* splash);

#endif  // CL_LINUX_SPLASH_H
