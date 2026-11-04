// Help > Show Me Around: a short walk round the real window (the Mac's
// mac/App/ShowMeAround.swift). Everything is dimmed but one place at a time
// -- the side panel of parts, the canvas, the toolbar, the Actions panel, the
// run controls, the tabs, then the menus in the window's top row for Help
// and Settings -- with a bubble beside it saying what it's for in a
// sentence. Next, Back and Skip in the bubble; Right (or Enter, or Space)
// and Left step, Escape stops. The spotlight slides from place to place;
// with Windows' "Animation effects" off it cuts, the bubble fading out and
// in. A place that isn't in the window just now is passed over (the Actions
// panel turned off, a toolbar style without those tools). At the panel's
// stop a folded panel opens, and folds back after.
//
// It starts by itself until it has been seen once (finished or skipped:
// prefs().showMeAroundSeen): after the first-run welcome closes, whichever
// way, after What's New for an update's first launch, and on any launch
// before it's been seen -- never over the launch screen, the intro, the
// welcome, What's New, the logic lesson or a dialog: only once a circuit
// window is up, in front, and taking input. Help > Show Me Around plays it
// again. "Learn Logic: Guided Tour" (Welcome.h) is the other kind, a lesson
// that builds a circuit with you: chosen from the welcome, it starts after
// this; and this walk's last stop offers it.
//
// The overlay is a layered popup over the circuit window's client area,
// drawn with Direct2D: a dim with one rounded hole, a ring round it, and a
// card with a small pointer. The hole's pixels are fully transparent, so
// clicks there reach what's lit; clicks on the dim go nowhere. Keys go to
// the circuit window, where the message loop hands them here first (key()).

#ifndef CL_WINDOWS_SHOW_ME_AROUND_H
#define CL_WINDOWS_SHOW_ME_AROUND_H

#include "App.h"
#include <functional>
#include <string>
#include <vector>

class CircuitWindow;

namespace showme {

// The places it stops at, in order (CircuitWindow::tourSpot measures them).
enum Spot { Palette, Canvas, ToolbarSpot, Actions, Run, Tabs, MenuBar, SpotCount };

// Its stops: their place, title and the bubble's sentence (with the keys as
// Settings > Shortcuts has them now).
int stepCount();
Spot stepSpot(int i);
std::string stepTitle(int i);
std::string stepBody(int i);

// Help > Show Me Around, or its first time by itself: on this window, out
// of focus mode and Simulation View first. `settle`: seconds before the
// first stop (the window settling).
void start(CircuitWindow* window, double settle = 0.35);
// Done, Skip or Escape: it's been seen (it won't start by itself again),
// and whatever was to come after it (the lesson) comes now.
void end();
// Put away without counting as seen (its window closing).
void stop();
bool active();
CircuitWindow* target();
// Which stop is showing; the stops this window has, in order; "2 of 7".
int step();
std::vector<int> shown();
int position();
bool isFirst();
bool isLast();
void next();
void back();

bool seen();
// What comes after it, given the window it walked round (the lesson).
void setThen(std::function<void(CircuitWindow*)> then);
bool hasThen();

// How the first-run welcome was closed. The walk comes first, whichever
// way; the lesson, if that was the choice, after it. False: it's been seen
// already, so the lesson (if chosen) should start now.
enum class WelcomeExit { GuidedTour, Template, Blank, Shortcuts, Skip };
bool welcomeClosed(WelcomeExit exit);
// Until it's been seen: watches for the moment a circuit window is up and
// taking input, with nothing over it, and starts there (a window appearing,
// the welcome or What's New closing all call this; one watch at a time).
void startWhenReady();
bool watching();
void stopWatching();
// The circuit window it could start on now, if any.
CircuitWindow* readyWindow();

// The Actions panel is held open for its stop.
bool holdsPanelOpen(const CircuitWindow* w);
// A key on its way to a window: true when it's taken (Escape, the arrows,
// Enter and Space; other keys without Ctrl or Alt don't reach the canvas
// meanwhile).
bool key(const MSG& m);
// A circuit window is going: so does the walk, if it was on it.
void windowClosed(CircuitWindow* w);

// Test and picture runs never start it by themselves; --rail-check lets it
// start on a window that isn't in front (CI's windows may not be).
extern bool quiet;
extern bool assumeFront;
// For pictures and checks: on this window at once, at stop `step` (the
// first stop it has from there), nothing sliding.
void preview(CircuitWindow* window, int step);
// The window with the walk over it, as a PNG.
bool saveScreenshot(const std::string& file);
// For checks: the overlay is on screen; the lit place, in screen pixels.
bool overlayShown();
RECT holeOnScreen();

}  // namespace showme

#endif  // CL_WINDOWS_SHOW_ME_AROUND_H
