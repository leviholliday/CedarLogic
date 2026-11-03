// The launch screen, as the Mac app's (Splash.swift) and the Windows app's,
// moment for moment: a glass panel in the icon's own colours in the middle
// of the screen. The icon rises out of a blur, its circuit traces draw in,
// "CedarLogic" writes itself in a letter at a time, a line says what's being
// done and a thin neon bar along the bottom fills as it's done; a band of
// light passes over the glass. Then it dissolves into the window. About two
// and a half seconds; the very first time, slower, with its own sound (the
// Mac's FirstLaunch), fading into the welcome. On the first launch and the
// first after an update, the intro plays first (Intro.h) and hands over to it.

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

// While the intro plays (Intro.h) its window is where the circuit window
// will come in: the first circuit window is put there before it's shown.
void splashPlaceUnder(GtkWindow* window);

// The panel as it plays after the intro: at `t` on its own clock, its
// top-left at (x, y), the whole at `opacity`; `calm`, the still one.
void drawPanelAfterIntro(cairo_t* cr, float x, float y, double t, double opacity, bool calm);

// --splash-frame: the panel at t seconds, as a PNG.
bool renderSplashFrame(double t, bool first, const std::string& file);

#endif  // CL_LINUX_SPLASH_H
