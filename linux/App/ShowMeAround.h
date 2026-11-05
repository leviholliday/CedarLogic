// Help > Show Me Around (the Mac's mac/App/ShowMeAround.swift): a short walk
// round the real window. Everything is dimmed but one place at a time -- the
// side panel of parts, the canvas, the toolbar, the Actions panel, the run
// controls, the tabs, then the window's menus for Help and Settings -- with
// a card beside it saying what it's for in a sentence. Next, Back and Skip
// on the card; Right (or Enter, or Space) and Left step, Escape stops. The
// spotlight slides from place to place; with the desktop's animations off
// (or frames too slow, as on a Raspberry Pi with a big circuit) it cuts,
// the card fading out and in. A place that isn't in the window just now is
// passed over: the Actions panel turned off, the side panel hidden, a
// toolbar too narrow for its tools. At the Actions panel's stop a folded
// panel opens, and folds back after.
//
// It starts by itself until it has been seen once (finished or skipped:
// prefs().showMeAroundSeen): after the first-run welcome closes, whichever
// way; after What's New, for an update's first launch; and on any launch
// before then -- never over the launch screen, the intro, the welcome,
// What's New, a dialog or the lesson, only once a circuit window is up and
// has the keyboard. "Learn Logic: Guided Tour" (Welcome.h) is the other
// kind, a lesson that builds a circuit with you: chosen from the welcome it
// starts after this, and this walk's last stop offers it.
//
// Drawn as flat shapes (a dim with one rounded hole, a ring round it, a card
// with a pointer), in two layers, one over the title bar (the menus and the
// toolbar) and one over the rest of the window, each drawing its share of
// the same picture. It's cheap where it has to be: the card is drawn once
// per stop into an image, and while the spotlight moves only the part of
// the layers that changes is redrawn, with nothing made each frame. Clicks
// on the dim go nowhere; in the lit place they reach what's there.

#ifndef CL_LINUX_SHOWMEAROUND_H
#define CL_LINUX_SHOWMEAROUND_H

#include "Chrome.h"
#include <functional>
#include <string>
#include <vector>

class CircuitWindow;

namespace showme {

// The places it stops at, in order.
enum Spot { Palette, CanvasSpot, ToolbarSpot, ActionsSpot, Run, Tabs, Menus, SpotCount };
const char* spotName(int spot);

// The two layers for a circuit window: over its title rows and over its
// content (each a GtkOverlay the window made). Once, as the window is built.
void addLayers(CircuitWindow* w, GtkWidget* titleOverlay, GtkWidget* contentOverlay);
// The window is going: a walk on it stops (not seen: it starts again next time).
void windowClosed(CircuitWindow* w);

// Help > Show Me Around, or its first time by itself: on this window, out of
// focus mode and Simulation View first; the lesson stops if it's on.
// `settle`: seconds before the first stop.
void start(CircuitWindow* w, double settle = 0.35);
// Done, Skip or Escape: seen (it won't start by itself again), and whatever
// was to come after it (the lesson) comes now.
void end();
// Put away without counting as seen (its window closed).
void stop();
bool active();
bool activeOn(const CircuitWindow* w);
// A key in the circuit window while it's on: true when it took it.
bool key(CircuitWindow* w, const GdkEventKey* e);
// The Actions panel is held open for its stop.
bool holdsPanelOpen(const CircuitWindow* w);

// What comes after it (the welcome's Learn Logic choice), on its window.
void setThen(std::function<void(CircuitWindow*)> then);
bool hasThen();

// How the first-run welcome was closed.
enum class WelcomeExit { GuidedTour, Template, Blank, Shortcuts, Skip };
// The welcome closed: the walk comes first, whichever way (the lesson, if
// that was the choice, after it). False: it has been seen, so the lesson
// (if chosen) should start now.
bool welcomeClosed(WelcomeExit exit);
// Until it has been seen: watches for the moment a circuit window is up with
// the keyboard and nothing over it, and starts there. One watch at a time.
void startWhenReady();
bool watching();
void stopWatching();
// Picture and check runs never start it by themselves (main.cpp).
void setQuiet(bool quiet);
bool quiet();
// For the checks on a display with no window manager (no window is ever
// "active" there): a circuit window counts as having the keyboard.
void setAssumeKeys(bool assume);
// The circuit window it could start on now, or null (for the checks too).
CircuitWindow* readyWindow();

// Stepping, for the card's buttons and the checks.
void next();
void back();
int step();                     // the Spot it's on
std::vector<int> shown();       // the stops this window has now, in order
int position();                 // 1-based among them
std::string title(int spot);
std::string body(int spot);
// The place's rectangle in its window (points from the window's top-left),
// as last measured; false when it isn't showing.
bool frame(int spot, RectF& out);
// The card's rectangle as drawn now, in the same points (for the checks).
RectF cardRect();
// For --session-check: the middle of the card's Next (0), Back (1), Skip (2)
// or lesson (3) button, in the window's points (false: not on the card now);
// and whether the card and the spotlight have stopped moving.
bool buttonCenter(int which, double& x, double& y);
bool settled();

// --show walk:N (CI's pictures): at stop N on this window, nothing watched,
// no keys taken.
void preview(CircuitWindow* w, int spot);

}  // namespace showme

#endif  // CL_LINUX_SHOWMEAROUND_H
