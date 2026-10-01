// The parts that introduce CedarLogic, in its own look whatever the theme
// (the Mac's BrandUI): the launch screen, the first-run welcome, and the
// guided tour that points at the palette, the canvas, Run and the tabs.

#ifndef CL_WINDOWS_WELCOME_H
#define CL_WINDOWS_WELCOME_H

#include "App.h"
#include <functional>
#include <string>

class CircuitWindow;

namespace splash {
// The launch screen (Splash.cpp): made at once, unseen; it plays once the
// launch's work is done (hideSoon), then dissolves into the windows.
void show();
bool active();
void setStatus(const char* status);
// The circuit being opened, for its "Opening ..." line.
void setOpening(const std::string& name);
// The work is done: play it, then run `then` (the windows come in) as it
// dissolves. Null `then`: close it at once (something went wrong).
void hideSoon(std::function<void()> then);
// --splash-frame: the panel at t seconds, as a PNG.
bool renderFrame(double t, bool first, const std::string& file);
}  // namespace splash

namespace welcome {
// Shown once, the first time the app has a window open (prefs().hasSeenWelcome).
// Offers the guided tour on its last page. False when it wasn't shown.
bool offer(CircuitWindow* window);
// The guided tour, on this circuit's window.
void startTour(CircuitWindow* window);
// Help > Guided Tour: the tour on a circuit of its own (this one if it's
// new and empty, else a new one).
void startTourOn(CircuitWindow* window);
// For --screenshot: the welcome on a page, and the tour's card.
bool pageForScreenshot(int page);
HWND tourWindow();
}  // namespace welcome

// What's New: after an update, a walk through what's new since the old
// Windows app, a page a theme, each with a way to try it (the Mac's
// WhatsNew.swift). Shown once per kVersion to someone who's seen the
// welcome; Help > What's New brings it back.
namespace whatsnew {
extern const char* const kVersion;
// If it hasn't been shown for this version. False when it wasn't shown.
bool offer(CircuitWindow* window);
void show(CircuitWindow* window, int page = 0);
}  // namespace whatsnew

#endif  // CL_WINDOWS_WELCOME_H
