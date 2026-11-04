// The intro, as the Mac app's (mac/App/Intro.swift, whose header has the
// whole timeline): the very first launch, and the first launch after an
// update with a What's New tour, open with the circuit drawing itself before
// the launch screen. The same intro both times, with the sound both times;
// Help > Play the Intro plays it again (sound and all), and a setting plays
// it at every launch, silently (the sound is for the first launch and the
// first after an update only). About 6 seconds:
//
//   0.00-0.45  the dark canvas fades in from nothing, the two input pins
//              easing in with it; the grid comes up after, softer, strongest
//              in the middle, with a few out-of-focus motes drifting over it
//              and a faint light under the gate. All of it slowly pushes in
//              (the circuit 4%, the grid behind it 2%) until the bloom.
//   0.40-1.96  two wires trace in from their pins, a bright head running
//              along them and slowing into each end; the letters of the icon
//              snap into place as the wires reach them (the C, the D -- an
//              AND gate -- and the L, which the output runs down to the LED);
//              a seven-segment display draws in beside it.
//   1.99-2.58  a clock pulse runs through, the wires high with a soft wide
//              glow under their line, the D lights (and the light under it);
//              just before it lands the canvas draws in a breath (the light
//              under the gate and the grid dim a touch), and as it lands the
//              LED lights with a brief lift of light over everything (the
//              sound's bell lands here, started on the picture's own clock)
//              and the display spells the version, a digit at a time.
//   2.58-3.05  a beat, everything lit, so it registers.
//   3.05-3.64  the LED blooms into the icon itself, which flies into the
//              launch screen's panel (Splash.cpp) as that fades in over it,
//              part-way into its own appearance ...
//   3.52-4.42  ... and its sweep of light, at full length, as on any launch;
//   4.42-5.52  it holds, finished (the bar full, "CedarLogic 0.4"), long
//              enough to read;
//   5.52-6.07  then it dissolves into the window (and the welcome, or What's
//              New, on top), a crossfade.
//
// Any key or click skips it. With the desktop's animations off
// (gtk-enable-animations) it's just the launch screen's still panel and a
// 0.4 s crossfade.
//
// It plays on its own clock (Pacer, below), not the wall's: the clock starts
// with the first frame that's actually up and moves on by the time between
// frames, at most a thirtieth of a second a frame, so a late frame slows it
// for that frame instead of jumping it ahead, and none of it is ever
// skipped. On a slow machine (a Raspberry Pi drawing a large window) it
// draws less (Tier::Lite: no push-in, motes or softening glows) rather than
// give up, and the sound is started for the pace the picture keeps, so the
// bell still lands on the LED. Only what changes is drawn again: the
// canvas, the panel's ground and shadow and the title's letters are drawn
// once and kept, and while only the panel moves only its box is redrawn.
//
// Here it's one window, where the circuit window will come in, that draws
// the intro and then the launch screen's panel (Splash.cpp plays it). Help >
// Play the Intro puts the same window over the front circuit window.
//
// `cedarlogic --render-intro <dir> [first|update|every] [--behind <png>]`
// draws it at 60 fps to PNGs (frame-0000.png on, 1280 x 800 at 2x, on
// black), with no display: Cairo image surfaces only. --behind is the
// picture of the window it dissolves into (else the canvas it opens on).
// CL_INTRO=first|update|every plays that one on a normal launch, for
// development; CL_NO_INTRO=1 (or CL_NO_SPLASH) never. CL_INTRO_FREEZE=<seconds>
// holds it; CL_INTRO_LOG=1 says on stderr which intro played and whether
// with the sound. `cedarlogic --intro-check`: the rules (which launch plays
// which, the sound, the setting, Help > Play the Intro) with settings of its
// own, no window.

#ifndef CL_LINUX_INTRO_H
#define CL_LINUX_INTRO_H

#include "Chrome.h"
#include <chrono>
#include <string>
#include <vector>

namespace intro {

// What it's played for: the first launch, the first after an update, or
// every launch (Settings > General, "Play the intro every time CedarLogic
// opens").
enum class Trigger { None, First, Update, Every };
const char* name(Trigger t);
// The sound: the first launch's and an update's only; every other launch is
// silent. (Help > Play the Intro plays First, sound and all.)
inline bool hasSound(Trigger t) { return t == Trigger::First || t == Trigger::Update; }

// The timeline, in seconds (the header has it all; the Mac's own numbers).
constexpr double kFadeIn = 0.45;
constexpr double kLedAt = 2.58;
// The bell's onset in FirstLaunch.wav (the same cut as the Mac's .m4a: the
// swell starts at 3.96 s and peaks by 4.06).
constexpr double kSoundHit = 3.96;
constexpr double kBloomAt = kLedAt + 0.47;
constexpr double kCardIn = kBloomAt + 0.27;
constexpr double kCardFade = 0.32;
constexpr double kSplashAt = 0.65;       // the panel's own clock as it comes in
constexpr double kSweepStart = 0.85, kSweepTime = 0.9;   // the panel's sweep (Splash.cpp)
constexpr double kSweepEnd = kSweepStart + kSweepTime;   // the panel's bar is full here
// The panel holds, finished, to be read, then dissolves.
constexpr double kPanelHold = 1.1;
constexpr double kDissolveAt = kCardIn + kSweepEnd - kSplashAt + kPanelHold;
constexpr double kFade = 0.55;
constexpr double kTotal = kDissolveAt + kFade;
constexpr double kCalmHold = 0.5, kCalmFade = 0.4;
// From here on only the canvas is left under the panel (the circuit, the
// display, the icon and the bloom have all gone): the window changes only
// where the panel is, until the dissolve.
constexpr double kGroundOnly = kCardIn + kCardFade;
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
// This run had a `--` option: a test or a picture (the Actions rail's
// one-time line waits for a real launch too).
bool dashRun();
// Which intro the saved settings say this launch is owed (None as a rule).
Trigger owed(const Prefs& p);
Trigger owed();
// What a launch plays, by the saved settings: the intro owed, or, with "Play
// the intro every time CedarLogic opens" on, Every.
Trigger forLaunch(const Prefs& p);
// CL_INTRO's value: first (also anything else), update or every.
Trigger parse(const char* s);
// The intro to play at this launch, if any.
Trigger pending();
// CL_INTRO_LOG: a line on stderr saying which intro played and how.
void log(const std::string& what);
void markPlayed(Trigger t);
// CL_INTRO_FREEZE, or a very large number.
double freezeAt();

// The sound, from part-way in so its bell lands on the LED (with `calm`,
// just before the bell, as the window comes in), faded up. `prepare` cuts it
// in memory while the launch's work goes on; `play` writes it out and starts
// it, `late` seconds further in (however long the picture has already been
// playing, so the bell still lands on the LED); `stop` ends it (skipped).
void prepareSound(bool calm);
void playSound(double late = 0);
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

// How much of it is drawn. Full: all of it, as the Mac's. Lite, for a machine
// whose first frames come slowly (a Raspberry Pi drawing at a large window's
// size): no push-in (held where it was), no motes, and none of the glows
// that only soften (the light under the gate, the lift of light at the hit,
// the wires' widest glow); the tracing of the C, the D and the L, the LED,
// the 0.4, the bloom, the panel held and the dissolve are all there.
enum class Tier { Full, Lite };
const char* name(Tier t);

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
	// From picture time `at`, `tier` (what Lite leaves out fades over 0.3 s).
	void setTier(Tier tier, double at);
	Tier tier() const;
	// Makes the images it keeps (for what `cr` draws on), so the first frame
	// doesn't have to.
	void prepare(cairo_t* cr);

private:
	struct Impl;
	Impl* d;
};

// ---- Its clock -------------------------------------------------------------

// The most the picture moves on in one frame: a frame that comes late (the
// machine busy, a slow draw) slows the picture for that frame rather than
// jumping it ahead, so nothing is ever skipped. (At 30 frames a second or
// better it keeps time.)
constexpr double kMaxStep = 1.0 / 30;

// The intro's own clock and how much it draws, worked out from the frames as
// they're shown (Splash.cpp feeds it the window's frame clock): the picture
// starts at 0 with the first frame that's up and moves on by the time
// between frames, at most kMaxStep a frame. From the first frames it picks
// the tier (Lite if they come more than ~21 ms apart or take more than 16 ms
// to draw), and it may still drop to Lite until shortly before the LED. It
// says when to start the sound and from where in its cut, so the bell lands
// on the LED on the picture's clock: from the first frames' pace, it starts
// it so the LED is due just as the bell (near the start of its cut on a
// fast machine; later in the picture, from the cut's start, on a slow one);
// from then until the LED the picture keeps to where it should be for that:
// never ahead (it waits), and catching up (still at most kMaxStep a frame)
// when it falls behind.
class Pacer {
public:
	// A frame was shown at `now` seconds (the frame clock's), having taken
	// `drawMs` to draw. Returns how far the picture moved on (0 for the first).
	double frame(double now, double drawMs);
	// Time went by without a frame shown (nothing to measure): the next
	// frame's step counts from `now`.
	void idle(double now);
	double t() const { return t_; }
	int frames() const { return frames_; }
	Tier tier() const { return tier_; }
	double tierAt() const { return tierAt_; }
	// The sound is owed (its cut lands its bell kLedAt into the cut).
	void soundOwed() { soundOwed_ = true; }
	// Whether to start the sound now (at `now`), and where in its cut; -1 not yet.
	double soundStart(double now) const;
	// It was started at `now`, `offset` seconds into its cut.
	void soundStarted(double now, double offset);
	bool soundStarted() const { return soundAt_ >= 0; }
	// Where the sound is, in the picture's time, at `now` (-1 if not started).
	double soundTime(double now) const { return soundAt_ < 0 ? -1 : soundOffset_ + (now - soundAt_); }
	// The intro was skipped or cut short: no more lining up with the sound.
	void stopSound() { soundOwed_ = false; soundAt_ = -1; }
	// (For the check: the median of the last frames' gaps and draw times.)
	double recentGap() const;
	double recentDraw() const;

private:
	double t_ = 0, last_ = -1;
	int frames_ = 0;
	Tier tier_ = Tier::Full;
	double tierAt_ = 0;
	bool decided_ = false;
	std::vector<double> gaps_, draws_, paces_;   // the last few frames'
	bool soundOwed_ = false;
	double soundAt_ = -1, soundOffset_ = 0, soundPace_ = 1;
	void judge();
};

// ---- Measuring it (--intro-bench, Splash.h) ----

// The parts of a frame, timed apart: each part's time leaves out the parts
// drawn inside it (a glow inside the circuit is the glows', not the wires').
enum Part { kClear, kBackdrop, kGrid, kGlows, kMotes, kWires, kDisplay, kIcon, kBloom, kPanel, kGroups, kParts };
const char* partName(int part);
// While the bench runs, each part's time so far in ms; null otherwise (and
// then a Timed costs a test of a pointer).
struct Prof {
	double ms[kParts] = {};
	int cur = -1;
	std::chrono::steady_clock::time_point mark;
	void flush() {
		const auto n = std::chrono::steady_clock::now();
		if (cur >= 0) ms[cur] += std::chrono::duration<double, std::milli>(n - mark).count();
		mark = n;
	}
};
extern Prof* gProf;
// The time from here to the end of the scope is `part`'s.
struct Timed {
	int prev = -1;
	explicit Timed(int part) {
		if (!gProf) return;
		gProf->flush();
		prev = gProf->cur;
		gProf->cur = part;
	}
	~Timed() {
		if (!gProf) return;
		gProf->flush();
		gProf->cur = prev;
	}
	Timed(const Timed&) = delete;
	Timed& operator=(const Timed&) = delete;
};

// --render-intro: the frames, then 0 (or 1 if a frame couldn't be written).
int render(const std::string& dir, Trigger trigger, const std::string& behind);

// --intro-check: which launch plays which intro and with what sound, the
// setting, and Help > Play the Intro (in the actions and in `menubar`), with
// settings of its own. Returns the exit code.
int check(GMenuModel* menubar);

}  // namespace intro

#endif  // CL_LINUX_INTRO_H
