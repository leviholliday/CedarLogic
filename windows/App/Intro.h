// The intro (the Mac's Intro.swift): the very first launch, and the first
// launch after an update with a What's New tour, open with the circuit
// drawing itself before the launch screen. The same intro both times (only
// the first launch has the sound). About 5 seconds; Intro.cpp has the
// timeline. Any key or click skips it; with Windows' animation effects off
// it's just the launch screen's still panel and a short crossfade.
//
// `CedarLogic --render-intro <dir> [first|update]` draws it, frame by frame
// at 60 fps, to PNGs. CL_INTRO=first|update plays it again on a normal
// launch, for development; CL_NO_INTRO=1 (or CL_NO_SPLASH) never.

#ifndef CL_WINDOWS_INTRO_H
#define CL_WINDOWS_INTRO_H

#include "App.h"
#include <wincodec.h>

#include <string>

namespace intro {

enum class Trigger { None, First, Update };

// The timeline, in seconds (the Mac's, to the hundredth). The canvas fades
// in over kFadeIn; the LED lights at kLedAt (the first launch's sound starts
// part-way in so its bell chord, kSoundHit into FirstLaunch, lands on it);
// it holds, lit, until it blooms; the launch screen's panel comes in over the
// bloom at kCardIn, over kCardFade, at kSplashAt on its own clock, plays the
// rest of its appearance and its sweep at full length, and at kDissolveAt it
// all dissolves into the window over kFade.
const double kFadeIn = 0.45;
const double kLedAt = 2.58;
const double kSoundHit = 4.0;
const double kBloomAt = kLedAt + 0.47;
const double kCardIn = kBloomAt + 0.27;
const double kCardFade = 0.32;
const double kSplashAt = 0.65;
const double kSweepStart = 0.85, kSweepTime = 0.9;   // the launch screen's sweep (Splash.cpp)
const double kDissolveAt = kCardIn + kSweepStart + kSweepTime - kSplashAt;
const double kFade = 0.55;
const double kTotal = kDissolveAt + kFade;
// Animations off: the launch screen's still, briefly, then a crossfade.
const double kCalmHold = 0.5, kCalmFade = 0.4;
// Skipped: everything goes this quickly.
const double kSkipFade = 0.3;

// 0...1, eased in and out.
double inOut(double u);
// How far the intro has faded in, as a whole, t seconds in.
double fadeInShown(double t);

// The intro this launch is owed, if any (worked out once a run): never in
// a test or picture run (any -- option), on CI, or with CL_NO_INTRO or
// CL_NO_SPLASH; CL_INTRO=first|update forces one.
Trigger pending();
void markPlayed(Trigger t);

// Plays it over the circuit window (the first one open), from now, the
// launch screen's icon at `cardIcon` (screen pixels). Returns its start
// (nowSeconds), or -1 if it couldn't play.
double play(POINT cardIcon);
// The first launch's sound, from part-way in (see kLedAt), faded up; with
// the animations off, from just before its bell.
void playSound(bool calm);
// A key or click: it goes, quickly, the launch screen and sound with it.
void skip();
bool active();

// --render-intro: every frame at 60 fps, 1280 x 800 points at 2x, on black,
// as frame-0000.png on in `dir`, `behind` (the circuit window, dark) put in
// under it as it dissolves. False, with `report` saying why, if it couldn't.
bool render(const std::string& dir, Trigger t, IWICBitmap* behind, std::string& report);

}  // namespace intro

#endif  // CL_WINDOWS_INTRO_H
