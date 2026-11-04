// The launch screen, as the Mac app's (Splash.swift) and the Windows app's,
// moment for moment: a glass panel in the icon's own colours in the middle
// of the screen. The icon rises out of a blur, its circuit traces draw in,
// "CedarLogic" writes itself in a letter at a time, a line says what's being
// done and a thin neon bar along the bottom fills as it's done; a band of
// light passes over the glass. Then it dissolves into the window. About two
// and a half seconds; the very first time, slower, with its own sound (the
// Mac's FirstLaunch), fading into the welcome. On the first launch and the
// first after an update (and, with the setting, every launch) the intro
// plays first (Intro.h) and hands over to it.

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

// Help > Play the Intro: the first launch's intro, sound and all, over
// `over` (the front circuit window), dissolving back into it. Nothing is
// hidden or held, and the once-only keys are left as they are. Not while
// the launch screen (or another intro) is up.
void splashPlayIntroNow(GtkWindow* over);
// The launch screen or an intro is up (splashActive is the launch's only).
bool splashShowing();

// The panel as it plays after the intro: at `t` on its own clock, its
// top-left at (x, y), the whole at `opacity`; `calm`, the still one.
void drawPanelAfterIntro(cairo_t* cr, float x, float y, double t, double opacity, bool calm);

// --splash-frame: the panel at t seconds, as a PNG.
bool renderSplashFrame(double t, bool first, const std::string& file);

#endif  // CL_LINUX_SPLASH_H
