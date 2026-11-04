// The intro (the Mac's Intro.swift): the circuit drawing itself over the
// circuit window. Two lengths of it.
//
// The full intro: the very first launch, and the first launch after an
// update with a What's New tour, with the sound both times; Help > Play the
// Intro plays it again (sound and all, over the front circuit window), and
// Settings > General > "When CedarLogic opens: Full intro" plays it at every
// other launch, silently. About 6 seconds (6.07); Intro.cpp has the timeline.
//
// The short intro: every other launch, unless Settings says otherwise
// ("When CedarLogic opens": Short intro, everyone's to begin with; Full
// intro; Classic launch screen; None). Just the drawing, silent, and no
// launch screen's panel: the same as the full intro up to the LED and "0.4"
// and the beat after them, then the bloom flashes out of the LED and the
// picture crossfades straight into the window. About 3.5 seconds (3.55).
// (The Mac's waits, held, if the window isn't ready; here the intro only
// starts once the launch's windows are made, so it never has to.)
//
// "Classic launch screen" is the launch screen as it was before there was an
// intro (Splash.cpp's panel alone, silent, about two and a half seconds);
// "None", no launch screen at all, and not even the card a circuit opens
// with: the window straight in. Whatever the choice, the first launch and
// the first after an update play the full intro, with the sound.
//
// Any key or click skips either; with Windows' animation effects off it's
// just the launch screen's still panel and a short crossfade.
//
// `CedarLogic --render-intro <dir> [first|update|every|short]` draws it,
// frame by frame at 60 fps, to PNGs. CL_INTRO=first|update|every|short plays
// that one on a normal launch, for development; CL_NO_INTRO=1 (or
// CL_NO_SPLASH) never.

#ifndef CL_WINDOWS_INTRO_H
#define CL_WINDOWS_INTRO_H

#include "App.h"
#include <wincodec.h>

#include <string>

namespace intro {

// What it's played for: the first launch, the first after an update, or
// any other launch (Settings: the full intro, Every, or the short one,
// Short). The sound is for the first two only.
enum class Trigger { None, First, Update, Every, Short };
inline bool hasSound(Trigger t) { return t == Trigger::First || t == Trigger::Update; }
// Just the drawing: no launch screen's panel, from the bloom straight into the window.
inline bool isShort(Trigger t) { return t == Trigger::Short; }

// Settings > General > "When CedarLogic opens" (Prefs::introAtLaunch): what
// an ordinary launch plays (the first launch and the first after an update
// play the full intro whatever it says). The short intro (everyone's to
// begin with); the full one; the launch screen as it was before there was
// an intro (Splash.cpp's panel on its own, silent); or none at all.
enum AtLaunch { AtShort = 0, AtFull = 1, AtClassic = 2, AtNone = 3 };
const char* atLaunchTitle(int choice);   // "Short intro", "Full intro", "Classic launch screen", "None"
const char* atLaunchKey(int choice);     // as saved: "short", "full", "classic", "none"
int atLaunchFromKey(const std::string& key);   // -1 if it isn't one
// What each choice does, for Settings (the Mac's words).
std::string atLaunchHint(int choice);

// The timeline, in seconds (the Mac's, to the hundredth). The canvas fades
// in over kFadeIn; the LED lights at kLedAt (the sound starts part-way in so
// its bell, kSoundHit into FirstLaunch, lands on it); it holds, lit, until
// it blooms; the launch screen's panel comes in over the bloom at kCardIn,
// over kCardFade, at kSplashAt on its own clock, plays the rest of its
// appearance and its sweep at full length, holds, finished, for kPanelHold
// to be read, and at kDissolveAt it all dissolves into the window over kFade.
const double kFadeIn = 0.45;
const double kLedAt = 2.58;
// The bell's onset in FirstLaunch.wav: its swell starts at 3.96 s and peaks
// by 4.06 (the WAV is the Mac's m4a, sample for sample: the same onset).
const double kSoundHit = 3.96;
const double kBloomAt = kLedAt + 0.47;
const double kCardIn = kBloomAt + 0.27;
const double kCardFade = 0.32;
const double kSplashAt = 0.65;
const double kSweepStart = 0.85, kSweepTime = 0.9;   // the launch screen's sweep (Splash.cpp)
const double kPanelHold = 1.1;
const double kDissolveAt = kCardIn + kSweepStart + kSweepTime - kSplashAt + kPanelHold;
const double kFade = 0.55;
const double kTotal = kDissolveAt + kFade;
// The short intro: the same up to the bloom, which crossfades straight into
// the window as it flashes (no panel): 3.55 s in all.
const double kShortDissolveAt = kBloomAt + 0.1;
const double kShortFade = 0.4;
const double kShortTotal = kShortDissolveAt + kShortFade;
// The timeline for a trigger: when it starts dissolving into the window, how
// long that takes, and how long it is in all.
inline double dissolveFor(Trigger t) { return isShort(t) ? kShortDissolveAt : kDissolveAt; }
inline double fadeFor(Trigger t) { return isShort(t) ? kShortFade : kFade; }
inline double lengthFor(Trigger t) { return dissolveFor(t) + fadeFor(t); }
// Animations off: the launch screen's still, briefly, then a crossfade.
const double kCalmHold = 0.5, kCalmFade = 0.4;
// Skipped: everything goes this quickly.
const double kSkipFade = 0.3;

// 0...1, eased in and out.
double inOut(double u);
// How far the intro has faded in, as a whole, t seconds in.
double fadeInShown(double t);

// The intro these settings say a launch is owed (the once-only keys): the
// first launch's, an update's, or none.
Trigger owed(const Prefs& p);
// What a launch plays by these settings: the intro owed (the full one, with
// the sound), else what "When CedarLogic opens" says: the short intro, the
// full one (both silent), or none (Classic launch screen, None).
Trigger forLaunch(const Prefs& p);
// The intro this launch plays, if any (worked out once a run): never in a
// test or picture run (any -- option), on CI, or with CL_NO_INTRO or
// CL_NO_SPLASH; CL_INTRO=first|update|every|short forces one.
Trigger pending();
// "None": this launch has no launch screen at all (and no opening card),
// the window straight in. Never for a launch owed the intro, a test or
// picture run, or one with CL_INTRO set. (straightInFor: by these settings.)
bool straightIn();
bool straightInFor(const Prefs& p);
void markPlayed(Trigger t);

// Plays `trigger`'s (full or short) over a circuit window (`over`, else the first one open), from
// now, the launch screen's icon at `cardIcon` (screen pixels). Returns its
// start (nowSeconds), or -1 if it couldn't play. `over` (Help > Play the
// Intro) is a window already on screen: it isn't moved.
double play(POINT cardIcon, Trigger trigger, CircuitWindow* over = nullptr);
// The sound, from part-way in (see kLedAt), faded up; with the animations
// off, from just before its bell. `clock`: when the picture's own clock
// started (nowSeconds); however long the sound takes to get going, it starts
// that much further into the file, so the bell still lands on the LED.
void playSound(bool calm, double clock = -1);
// A key or click: it goes, quickly, the launch screen and sound with it.
void skip();
bool active();

// --render-intro: every frame at 60 fps, 1280 x 800 points at 2x, on black
// (the short intro: 214 frames, 0 to 213; the full one 365),
// as frame-0000.png on in `dir`, `behind` (the circuit window, dark) put in
// under it as it dissolves. False, with `report` saying why, if it couldn't.
bool render(const std::string& dir, Trigger t, IWICBitmap* behind, std::string& report);
// How far into FirstLaunch the sound started last (-1: none yet), for the checks.
double lastSoundFrom();

}  // namespace intro

#endif  // CL_WINDOWS_INTRO_H
