// The launch screen, as the Mac app's (Splash.swift) and the Windows app's,
// moment for moment: a glass panel in the icon's own colours in the middle
// of the screen. The icon rises out of a blur, its circuit traces draw in,
// "CedarLogic" writes itself in a letter at a time, a line says what's being
// done and a thin neon bar along the bottom fills as it's done; a band of
// light passes over the glass. Then it dissolves into the window. About two
// and a half seconds; the very first time, slower, with its own sound (the
// Mac's FirstLaunch), fading into the welcome.

#ifndef CL_LINUX_SPLASH_H
#define CL_LINUX_SPLASH_H

#include "App.h"
#include <string>

// Shows the splash (unseen until hideSplashSoon plays it) and returns it.
GtkWidget* showSplash();
bool splashActive();
// Up, and the windows it hides not brought in yet (they come in as it dissolves).
bool splashHoldsWindows();

// Said while starting; the panel lists what was done once it plays.
void splashSetStatus(GtkWidget* splash, const char* status);
// The circuit being opened, for its "Opening ..." line.
void splashSetOpening(const std::string& name);

// The launch's work is done: play it, and call `onHidden` (the windows come
// in) as it starts to dissolve, then it goes. A null splash calls onHidden
// at once.
void hideSplashSoon(GtkWidget* splash, GSourceFunc onHidden = nullptr, gpointer data = nullptr);

// --splash-frame: the panel at t seconds, as a PNG.
bool renderSplashFrame(double t, bool first, const std::string& file);

#endif  // CL_LINUX_SPLASH_H
