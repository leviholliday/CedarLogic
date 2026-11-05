// The parts that introduce CedarLogic, in its own look whatever the theme
// (the Mac's BrandUI, as the Windows app has it): the first-run welcome,
// "Learn Logic: Guided Tour" (the lesson that builds an AND circuit with
// you), and What's New. (The launch screen is Splash.cpp; Help > Show Me
// Around, the walk round the window, is ShowMeAround.h.)

#ifndef CL_LINUX_WELCOME_H
#define CL_LINUX_WELCOME_H

#include "App.h"

class CircuitWindow;

namespace welcome {
// Shown once, the first time the app has a window open (prefs().hasSeenWelcome).
// Six pages: what CedarLogic is, making it yours, the Actions panel (its two
// switches), your name, six keys, and what to do first. However it closes,
// Show Me Around follows (until it's been seen), then the lesson if that
// was the choice. False when it wasn't shown.
bool offer(CircuitWindow* window);
// The lesson, on this circuit's window.
void startTour(CircuitWindow* window);
// Help > Learn Logic: Guided Tour: the lesson on a circuit of its own (this
// one if it's new and empty, else a new one).
void startTourOn(CircuitWindow* window);
bool tourActive();
void endTourNow();
// For --screenshot: the welcome on a page.
bool pageForScreenshot(int page);
GtkWidget* window();
// For --session-check: the welcome closed as by its last page's choices
// (0 the lesson, 1 a template, 2 a blank canvas, 3 the shortcuts), or by
// Skip (-1).
void closeForCheck(int choice);
}  // namespace welcome

// What's New: after an update, a walk through what's new since the old app,
// a page a theme, each with a way to try it (the Mac's WhatsNew.swift).
// Shown once per kVersion to someone who's seen the welcome; Help > What's
// New brings it back.
namespace whatsnew {
extern const char* const kVersion;
bool offer(CircuitWindow* window);
// `panelCard`: with the Actions panel's switches open over the page.
void show(CircuitWindow* window, int page = 0, bool panelCard = false);
GtkWidget* window();
}  // namespace whatsnew

// Older names, for main.cpp.
inline void offerWelcome(GtkApplication*, CircuitWindow* window) { welcome::offer(window); }
inline void startTour(CircuitWindow* window) { welcome::startTourOn(window); }

// Whether a truth table is open (the tour waits for one): TruthTableWindow.cpp.
bool truthTableOpen();

#endif  // CL_LINUX_WELCOME_H
