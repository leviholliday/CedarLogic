// The intro, as the Mac app's (mac/App/Intro.swift, whose header has the
// whole timeline): the very first launch, and the first launch after an
// update with a What's New tour, open with the circuit drawing itself before
// the launch screen. The same intro both times (only the first launch has
// the sound). About 5 seconds:
//
//   0.00-0.45  the dark canvas fades in from nothing, its grid and the two
//              input pins easing in with it.
//   0.40-1.96  two wires trace in from their pins, a bright head running
//              along them and slowing into each end; the letters of the icon
//              snap into place as the wires reach them (the C, the D -- an
//              AND gate -- and the L, which the output runs down to the LED);
//              a seven-segment display draws in beside it.
//   1.99-2.58  a clock pulse runs through, the D lights, and as it lands the
//              LED lights (the sound's bell chord lands here) and the display
//              spells the version, a digit at a time.
//   2.58-3.05  a beat, everything lit, so it registers.
//   3.05-3.64  the LED blooms into the icon itself, which flies into the
//              launch screen's panel (Splash.cpp) as that fades in over it,
//              part-way into its own appearance ...
//   3.52-4.42  ... and its sweep of light, at full length, as on any launch;
//   4.42-4.97  then it dissolves into the window (and the welcome, or What's
//              New, on top), a crossfade.
//
// Any key or click skips it. With the desktop's animations off
// (gtk-enable-animations) it's just the launch screen's still panel and a
// 0.4 s crossfade, and the same if the first frames come too slowly (an old
// Raspberry Pi): the intro gives way to the still panel rather than stutter.
//
// Here it's one window, where the circuit window will come in, that draws
// the intro and then the launch screen's panel (Splash.cpp plays it).
//
// `cedarlogic --render-intro <dir> [first|update] [--behind <png>]` draws it
// at 60 fps to PNGs (frame-0000.png on, 1280 x 800 at 2x, on black), with no
// display: Cairo image surfaces only. --behind is the picture of the window
// it dissolves into (else the canvas it opens on).
// CL_INTRO=first|update plays it again on a normal launch, for development;
// CL_NO_INTRO=1 (or CL_NO_SPLASH) never. CL_INTRO_FREEZE=<seconds> holds it.

#ifndef CL_LINUX_INTRO_H
#define CL_LINUX_INTRO_H

#include "Chrome.h"
#include <string>
#include <vector>

namespace intro {

enum class Trigger { None, First, Update };
const char* name(Trigger t);

// The timeline, in seconds (the header has it all; the Mac's own numbers).
constexpr double kFadeIn = 0.45;
constexpr double kLedAt = 2.58;
constexpr double kSoundHit = 4.0;   // the bell chord, into FirstLaunch.wav
constexpr double kBloomAt = kLedAt + 0.47;
constexpr double kCardIn = kBloomAt + 0.27;
constexpr double kCardFade = 0.32;
constexpr double kSplashAt = 0.65;       // the panel's own clock as it comes in
constexpr double kSweepStart = 0.85, kSweepTime = 0.9;   // the panel's sweep (Splash.cpp)
constexpr double kDissolveAt = kCardIn + kSweepStart + kSweepTime - kSplashAt;
constexpr double kFade = 0.55;
constexpr double kTotal = kDissolveAt + kFade;
constexpr double kCalmHold = 0.5, kCalmFade = 0.4;
constexpr double kSkipFade = 0.3;

// The intro window's fade in (as the Mac's window server fades it in).
double fadeInShown(double t);
// Ease in and out (the Mac's .easeInEaseOut, near enough).
double easeInOut(double u);

// The version the picture shows (What's New's).
const char* version();

// main() tells it the arguments: a run with a `--` option is a test or a
// picture, and never plays it.
void noteArgs(int argc, char** argv);
// Which intro the saved settings say this launch is owed (None as a rule).
Trigger owed();
// The intro to play at this launch, if any.
Trigger pending();
void markPlayed(Trigger t);
// CL_INTRO_FREEZE, or a very large number.
double freezeAt();

// The first launch's sound, from part-way in so its bell lands on the LED
// (with `calm`, just before the bell, as the window comes in), faded up.
// `prepare` writes the cut copy while the launch's work goes on; `play`
// starts it; `stop` ends it (skipped).
void prepareSound(bool calm);
void playSound();
void stopSound();

// Where the launch screen's panel (560 x 372) goes in an intro of w x h
// points: centred, a little above the middle.
PointF cardOrigin(float w, float h);

// How the canvas draws a wire (the settings' thickness, dots and low colour).
struct WireLook {
	double width = 1;
	double dot = 0.18 / 0.05;
	bool dots = true;
	Color low = colorF(0.62f, 0.67f, 0.76f);
	static WireLook current();
};

// The intro's picture, laid out for one size; `draw` paints it at a moment.
class Scene {
public:
	Scene(float w, float h, const WireLook& look, bool live);
	~Scene();
	Scene(const Scene&) = delete;
	Scene& operator=(const Scene&) = delete;
	// Everything at t seconds, its whole at `opacity` (over what's there).
	void draw(cairo_t* cr, double t, double opacity);
	// Just the canvas, its grid in (behind the still panel).
	void drawGround(cairo_t* cr, double opacity);

private:
	struct Impl;
	Impl* d;
};

// --render-intro: the frames, then 0 (or 1 if a frame couldn't be written).
int render(const std::string& dir, Trigger trigger, const std::string& behind);

}  // namespace intro

#endif  // CL_LINUX_INTRO_H
