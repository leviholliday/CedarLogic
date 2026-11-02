// The parts that introduce CedarLogic, in its own look whatever the theme
// (the Mac's BrandUI, as the Windows app has it): the first-run welcome, the
// guided tour that builds an AND circuit with you, and What's New. (The
// launch screen is Splash.cpp.)

#ifndef CL_LINUX_WELCOME_H
#define CL_LINUX_WELCOME_H

#include "App.h"

class CircuitWindow;

namespace welcome {
// Shown once, the first time the app has a window open (prefs().hasSeenWelcome).
// Offers the guided tour on its last page. False when it wasn't shown.
bool offer(CircuitWindow* window);
// The guided tour, on this circuit's window.
void startTour(CircuitWindow* window);
// Help > Guided Tour: the tour on a circuit of its own (this one if it's
// new and empty, else a new one).
void startTourOn(CircuitWindow* window);
// For --screenshot: the welcome on a page.
bool pageForScreenshot(int page);
GtkWidget* window();
}  // namespace welcome

// What's New: after an update, a walk through what's new since the old app,
// a page a theme, each with a way to try it (the Mac's WhatsNew.swift).
// Shown once per kVersion to someone who's seen the welcome; Help > What's
// New brings it back.
namespace whatsnew {
extern const char* const kVersion;
bool offer(CircuitWindow* window);
void show(CircuitWindow* window, int page = 0);
GtkWidget* window();
}  // namespace whatsnew

// Older names, for main.cpp.
inline void offerWelcome(GtkApplication*, CircuitWindow* window) { welcome::offer(window); }
inline void startTour(CircuitWindow* window) { welcome::startTourOn(window); }

// Whether a truth table is open (the tour waits for one): TruthTableWindow.cpp.
bool truthTableOpen();

#endif  // CL_LINUX_WELCOME_H
