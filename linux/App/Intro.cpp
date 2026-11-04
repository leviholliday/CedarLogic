// The intro (see Intro.h): the Mac's IntroScene (mac/App/Intro.swift), drawn
// with Cairo. The same layout in the icon's 512 units, the same timings and
// curves, the same colours; Core Animation's layers become a paint a frame,
// and its strokeEnd a part of a path measured along its length.
//
// Cheap enough for a Raspberry Pi: the canvas and its vignette, and the grid
// (its fall-off towards the edges and the vignette over it baked in), are
// drawn once into images and painted each frame, the grid scaled a little
// for the slow push-in (and, once that's over, the two as one image); the
// glows, the bloom, the light under the gate, the motes and the lift of
// light at the hit are small images drawn once and painted stretched, as the
// Mac's layers are (in Lite, without the push-in, the large ones are drawn
// once at the window's size); everything else is a few dozen strokes and
// fills. Only while a part of it is see-through (fading in, the circuit
// going, the dissolve) is it drawn into a group of its own, clipped to where
// that part is. --intro-bench (Splash.h) times each part.

#include "Intro.h"
#include "Actions.h"
#include "Brand.h"
#include "Splash.h"
#include "Welcome.h"

#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <glib/gstdio.h>
#include <unistd.h>

namespace intro {

namespace {

// ---- The look (the Mac's IntroScene colours) -------------------------------------

const Color kNeon = colorF(0.22f, 1.0f, 0.42f);
const Color kHead = colorF(0.82f, 1.0f, 0.86f);
const Color kSilver = colorF(0.86f, 0.9f, 0.88f);
// The Graphite theme's canvas and grid (Theme.swift): the app's dark look.
const Color kCanvas = colorF(0.075f, 0.082f, 0.098f);
const Color kGridMinor = colorF(0.16f, 0.17f, 0.20f);
const Color kGridMajor = colorF(0.23f, 0.25f, 0.29f);
const Color kLedCore = colorF(0.85f, 1.0f, 0.9f);

void set(cairo_t* cr, const Color& c, double opacity = 1) { cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a * opacity); }

// ---- Time (the Mac's curves) --------------------------------------------------------

double clamp01(double x) { return std::min(1.0, std::max(0.0, x)); }
// 0...1 between a and b, soft at both ends.
double smooth(double t, double a, double b) { const double c = clamp01((t - a) / (b - a)); return c * c * (3 - 2 * c); }
// 0...1 between a and b, fast out of the gate.
double ease(double t, double a, double b) { const double c = clamp01((t - a) / (b - a)); return 1 - std::pow(1 - c, 3); }
// A signal's run along a wire: away gently, then slowing all the way into the far end.
double run(double t, double a, double b) {
	const double c = clamp01((t - a) / (b - a));
	const double lift = c * c * (3 - 2 * c);
	const double land = 1 - std::pow(1 - c, 3.2);
	return lift * 0.25 + land * 0.75;
}
// A snap into place: from small up past full size and back.
double pop(double t, double at, double from = 0.6) {
	const double u = clamp01((t - at) / 0.3);
	if (u <= 0) return from;
	const double s = 1 - std::pow(1 - u, 3);
	return from + (1 - from) * s + 0.22 * std::sin(u * G_PI) * (1 - u);
}

// ---- Paths measured along their length -------------------------------------------

// One run of points (closed: its last point is its first again).
struct Run {
	std::vector<PointF> pts;
	std::vector<double> at;   // running length at each point
	bool closed = false;
};
// A shape of one or more runs, measured end to end (as strokeEnd measures them).
struct Shape {
	std::vector<Run> runs;
	double length = 0;
	void add(std::vector<PointF> pts, bool closed) {
		if (pts.size() < 2) return;
		if (closed) pts.push_back(pts.front());
		Run r;
		r.closed = closed;
		r.pts = std::move(pts);
		r.at.push_back(length);
		for (size_t i = 1; i < r.pts.size(); i++) r.at.push_back(r.at.back() + std::hypot(r.pts[i].x - r.pts[i - 1].x, r.pts[i].y - r.pts[i - 1].y));
		length = r.at.back();
		runs.push_back(std::move(r));
	}
	// The part from `from` to `to` (shares of the length) as the current path
	// (or added to it).
	void trace(cairo_t* cr, double from, double to, bool add = false) const {
		if (!add) cairo_new_path(cr);
		if (length <= 0 || to <= from) return;
		const double a = from * length, b = to * length;
		for (const Run& r : runs) {
			if (r.at.back() <= a || r.at.front() >= b) continue;
			bool started = false;
			for (size_t i = 1; i < r.pts.size(); i++) {
				const double s0 = r.at[i - 1], s1 = r.at[i];
				if (s1 <= a || s0 >= b) continue;
				const double span = s1 - s0;
				auto lerp = [&](double s) {
					const double u = span > 0 ? (s - s0) / span : 0;
					return PointF{ (float)(r.pts[i - 1].x + (r.pts[i].x - r.pts[i - 1].x) * u), (float)(r.pts[i - 1].y + (r.pts[i].y - r.pts[i - 1].y) * u) };
				};
				if (!started) {
					const PointF p = lerp(std::max(a, s0));
					cairo_move_to(cr, p.x, p.y);
					started = true;
				}
				const PointF q = lerp(std::min(b, s1));
				cairo_line_to(cr, q.x, q.y);
			}
			// Whole: closed properly, so its corner is joined.
			if (started && r.closed && a <= r.at.front() && b >= r.at.back()) cairo_close_path(cr);
		}
	}
	void whole(cairo_t* cr, bool add = false) const { trace(cr, 0, 1, add); }
	PointF point(double f) const {
		const double s = clamp01(f) * length;
		for (const Run& r : runs)
			for (size_t i = 1; i < r.pts.size(); i++) {
				if (r.at[i] < s) continue;
				const double span = r.at[i] - r.at[i - 1];
				const double u = span > 0 ? (s - r.at[i - 1]) / span : 0;
				return { (float)(r.pts[i - 1].x + (r.pts[i].x - r.pts[i - 1].x) * u), (float)(r.pts[i - 1].y + (r.pts[i].y - r.pts[i - 1].y) * u) };
			}
		return runs.empty() ? PointF{} : runs.back().pts.back();
	}
	RectF bounds() const {
		RectF b = rectF(1e9f, 1e9f, -1e9f, -1e9f);
		for (const Run& r : runs)
			for (const PointF& p : r.pts) {
				b.left = std::min(b.left, p.x); b.top = std::min(b.top, p.y);
				b.right = std::max(b.right, p.x); b.bottom = std::max(b.bottom, p.y);
			}
		return b;
	}
};

// An arc as points (angles in degrees, y down; going the way the angles go).
void arc(std::vector<PointF>& out, double cx, double cy, double r, double from, double to) {
	const int n = std::max(8, (int)(std::fabs(to - from) / 3));
	for (int i = 0; i <= n; i++) {
		const double a = (from + (to - from) * i / n) * G_PI / 180;
		out.push_back({ (float)(cx + r * std::cos(a)), (float)(cy + r * std::sin(a)) });
	}
}

// A radial image `side` pixels square: its stops from the middle out to
// the edge, at full strength (painted at any opacity, as all its stops'
// alpha go with the opacity).
struct Stop { double at, r, g, b, a; };
cairo_surface_t* radialImage(int side, std::initializer_list<Stop> stops, cairo_extend_t extend = CAIRO_EXTEND_PAD) {
	cairo_surface_t* img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, side, side);
	cairo_t* c = cairo_create(img);
	cairo_pattern_t* g = cairo_pattern_create_radial(side / 2.0, side / 2.0, 0, side / 2.0, side / 2.0, side / 2.0);
	for (const Stop& st : stops) cairo_pattern_add_color_stop_rgba(g, st.at, st.r, st.g, st.b, st.a);
	cairo_pattern_set_extend(g, extend);
	cairo_set_source(c, g);
	cairo_paint(c);
	cairo_pattern_destroy(g);
	cairo_destroy(c);
	return img;
}

// `img` (clear at its edges) stretched over the circle at c of `radius`, at
// `opacity`: only the square it covers is touched. `fast`: each pixel from
// the nearest of the image's, not a blend of four (several times cheaper on
// an ARM CPU, and a soft glow from a large enough image looks the same).
void paintRound(cairo_t* cr, cairo_surface_t* img, PointF c, double radius, double opacity, bool fast = false) {
	const int n = cairo_image_surface_get_width(img);
	cairo_save(cr);
	cairo_translate(cr, c.x - radius, c.y - radius);
	cairo_scale(cr, 2 * radius / n, 2 * radius / n);
	cairo_set_source_surface(cr, img, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), fast ? CAIRO_FILTER_FAST : CAIRO_FILTER_BILINEAR);
	cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_NONE);
	cairo_paint_with_alpha(cr, std::min(1.0, opacity));
	cairo_restore(cr);
}

// A soft green glow, white in the middle (wire heads, the LED, the icon): an
// image drawn once and painted stretched, as the Mac's layers are (a
// gradient worked out for every pixel of it, every frame, is too much for a
// Raspberry Pi at the LED's and the icon's size).
cairo_surface_t* glowImage(int side) {
	return radialImage(side, { { 0, 1, 1, 1, 1 }, { 0.12, 0.6, 1, 0.72, 0.85 }, { 0.4, 0.22, 1, 0.42, 0.25 }, { 1, 0.22, 1, 0.42, 0 } },
	                   CAIRO_EXTEND_NONE);
}
void glow(cairo_t* cr, PointF c, double radius, double opacity, bool fast = false) {
	if (opacity <= 0.002 || radius <= 0.1) return;
	Timed timed(kGlows);
	static cairo_surface_t* small = glowImage(64);
	static cairo_surface_t* large = glowImage(384);
	static cairo_surface_t* larger = nullptr;   // (for `fast`: as large as the glow gets)
	double dx = 2 * radius, dy = 0;
	cairo_user_to_device_distance(cr, &dx, &dy);
	const double across = std::hypot(dx, dy);
	if (fast && across > 384) {
		if (!larger) larger = glowImage(768);
		paintRound(cr, larger, c, radius, opacity, true);
		return;
	}
	paintRound(cr, across <= 96 ? small : large, c, radius, opacity, fast && across > 96);
}

void circle(cairo_t* cr, PointF c, double r) {
	cairo_new_sub_path(cr);
	cairo_arc(cr, c.x, c.y, r, 0, 2 * G_PI);
	cairo_close_path(cr);
}

// The icon the bloom becomes: the Mac's LaunchIcon (its art lines up with
// the circuit), else the app's own.
cairo_surface_t* launchIcon() {
	static cairo_surface_t* icon = [] {
		const std::string dir = resourcesDir();
		for (const std::string& f : { dir + "/LaunchIcon.png", dir + "/../mac/App/LaunchIcon.png" }) {
			cairo_surface_t* s = cairo_image_surface_create_from_png(f.c_str());
			if (cairo_surface_status(s) == CAIRO_STATUS_SUCCESS) return s;
			cairo_surface_destroy(s);
		}
		return (cairo_surface_t*)nullptr;
	}();
	return icon;
}

// The launch screen draws the app's icon 128 points square, its tile a
// little inside that: the flying icon lands on the tile.
double panelTile() { return brand::iconTile(); }

bool gDashArg = false;

}  // namespace

// ---- Its clock ----------------------------------------------------------------------

namespace {
const size_t kRecent = 8;   // frames looked at
const double kSlowGap = 0.021, kSlowDraw = 16;   // Lite past these (s, ms)
// The tier is settled after this many frames (or this far in), and may still
// drop to Lite until this far in.
const int kJudgeFrames = 8;
const double kJudgeBy = 0.5, kLastDrop = kLedAt - 0.3;

void keep(std::vector<double>& v, double x) {
	v.push_back(x);
	if (v.size() > kRecent) v.erase(v.begin());
}
double median(std::vector<double> v) {
	if (v.empty()) return 0;
	std::sort(v.begin(), v.end());
	return v.size() % 2 ? v[v.size() / 2] : (v[v.size() / 2 - 1] + v[v.size() / 2]) / 2;
}
}  // namespace

double Pacer::recentGap() const { return gaps_.empty() ? tierGap_ : median(gaps_); }
double Pacer::recentDraw() const { return draws_.empty() ? tierDraw_ : median(draws_); }

void Pacer::idle(double now) { if (last_ >= 0) last_ = now; }

double Pacer::frame(double now, double drawMs) {
	frames_++;
	if (last_ < 0) {
		// The first frame up: the picture starts here, at 0.
		last_ = now;
		return 0;
	}
	const double gap = std::max(0.0, now - last_);
	last_ = now;
	double step = std::min(gap, kMaxStep);
	keep(gaps_, gap);
	// (The first frames drawn make the images the scene keeps: not counted.)
	if (frames_ > 2) keep(draws_, drawMs);
	keep(paces_, gap > 0 ? step / gap : 1);
	// From the sound's start until the LED, where the picture should be for
	// the LED to land with the bell (at the pace the sound was started for):
	// never ahead of that (it waits), and when behind, catching up by as much
	// as half a frame more a frame (never more than kMaxStep).
	if (soundAt_ >= 0 && t_ < kLedAt) {
		const double due = kLedAt - (kLedAt - soundTime(now)) * soundPace_;
		const double lag = due - t_;
		if (lag <= step) step = std::max(0.0, lag);
		else step = std::min({ kMaxStep, lag, std::max(step, 1.5 * gap) });
	}
	t_ += step;
	judge();
	return step;
}

void Pacer::judge() {
	if (tier_ == Tier::Lite || gaps_.size() < 5) return;
	const bool slow = median(gaps_) > kSlowGap || (draws_.size() >= 4 && median(draws_) > kSlowDraw);
	if (!decided_ && (frames_ >= kJudgeFrames || t_ >= kJudgeBy)) decided_ = true;
	if (decided_ && slow && t_ < kLastDrop) {
		tier_ = Tier::Lite;
		tierAt_ = t_;
		tierGap_ = median(gaps_);
		tierDraw_ = median(draws_);
		// The frames to come are another tier's: measured afresh.
		gaps_.clear();
		draws_.clear();
		paces_.clear();
	}
}

double Pacer::soundStart(double now) const {
	(void)now;
	if (!soundOwed_ || soundAt_ >= 0) return -1;
	// Not before the tier is settled and the pace measured.
	if (!decided_ || paces_.size() < 4) return -1;
	if (t_ >= kLedAt - 0.05) return kLedAt - 0.05;   // (late: the bell at once)
	// The picture's pace (picture seconds a second): keeping time, or on a
	// slow machine a little under what it's been, so that the picture, if
	// anything, is early for the bell and waits for it (it can always slow,
	// but can only catch up where its frames come quicker than kMaxStep).
	const double measured = median(paces_);
	const double pace = measured >= 0.97 ? 1 : std::max(0.05, measured * 0.9);
	const double left = (kLedAt - t_) / pace;   // seconds until the LED is due
	if (left > kLedAt) return -1;                 // more than the cut has before its bell
	return kLedAt - left;
}

void Pacer::soundStarted(double now, double offset) {
	soundAt_ = now;
	soundOffset_ = offset;
	soundOwed_ = false;
	// The pace it was started for: the picture's time left to the LED over the sound's.
	const double left = kLedAt - offset;
	soundPace_ = left > 0.01 ? std::min(1.0, std::max(0.05, (kLedAt - t_) / left)) : 1;
}

// ---- Measuring ----------------------------------------------------------------------

Prof* gProf = nullptr;

const char* partName(int part) {
	static const char* names[kParts] = { "clear", "backdrop", "grid", "under", "glows", "motes", "wires", "display", "icon", "bloom", "panel", "groups" };
	return part >= 0 && part < kParts ? names[part] : "?";
}

// ---- When ----------------------------------------------------------------------------

const char* name(Trigger t) {
	switch (t) {
	case Trigger::First: return "first";
	case Trigger::Update: return "update";
	case Trigger::Every: return "every";
	default: return "none";
	}
}

const char* name(Tier t) { return t == Tier::Lite ? "lite" : "full"; }

double easeInOut(double u) {
	const double c = clamp01(u);
	return c < 0.5 ? 2 * c * c : 1 - std::pow(-2 * c + 2, 2) / 2;
}
double fadeInShown(double t) { return easeInOut(t / kFadeIn); }

const char* version() { return whatsnew::kVersion; }

bool dashRun() { return gDashArg; }

void noteArgs(int argc, char** argv) {
	for (int i = 1; i < argc; i++)
		if (argv[i] && strncmp(argv[i], "--", 2) == 0) gDashArg = true;
}

// The same test as the launch screen's first-launch look and What's New, so
// it can't disagree with them.
Trigger owed(const Prefs& p) {
	if (!p.hasSeenWelcome && !p.firstLaunchPlayed && !p.introFirstLaunchPlayed) return Trigger::First;
	const bool whatsNew = p.hasSeenWelcome && p.seenWhatsNew != whatsnew::kVersion;
	if (whatsNew && p.introUpdatePlayed != whatsnew::kVersion) return Trigger::Update;
	return Trigger::None;
}
Trigger owed() { return owed(prefs()); }

Trigger forLaunch(const Prefs& p) {
	const Trigger t = owed(p);
	if (t != Trigger::None) return t;
	return p.introEveryLaunch ? Trigger::Every : Trigger::None;
}

void log(const std::string& what) {
	if (g_getenv("CL_INTRO_LOG")) g_printerr("CedarLogic: intro: %s\n", what.c_str());
}

// Never in the tests, the picture runs, CI or any run with a `--` option.
static bool allowed() {
	for (const char* v : { "CL_NO_INTRO", "CL_NO_SPLASH", "CI" })
		if (g_getenv(v)) return false;
	return !gDashArg;
}

Trigger pending() {
	if (!allowed()) return Trigger::None;
	if (const char* forced = g_getenv("CL_INTRO")) return parse(forced);
	return forLaunch(prefs());
}

// (Every launch's isn't owed, so there's nothing to keep.)
void markPlayed(Trigger t) {
	if (t == Trigger::First) prefs().introFirstLaunchPlayed = true;
	if (t == Trigger::Update) prefs().introUpdatePlayed = whatsnew::kVersion;
}

double freezeAt() {
	const char* f = g_getenv("CL_INTRO_FREEZE");
	return f ? g_ascii_strtod(f, nullptr) : 1e9;
}

// (On whole points, so the images the panel keeps land on whole pixels.)
PointF cardOrigin(float w, float h) { return { std::floor(w / 2 - 280), std::floor(h / 2 - 36 - 186) }; }

WireLook WireLook::current() {
	const Prefs& p = prefs();
	WireLook l;
	l.width = p.wireScale();
	l.dot = p.wireDotSize / 0.05;
	l.dots = p.wireDots;
	static const float colors[][3] = { { 0.62f, 0.67f, 0.76f }, { 0.47f, 0.58f, 0.84f }, { 0.86f, 0.87f, 0.90f } };
	l.low = p.lowWire >= 0 && p.lowWire < 3 ? colorF(colors[p.lowWire][0], colors[p.lowWire][1], colors[p.lowWire][2]) : colorF(0.35f, 0.35f, 0.35f);
	return l;
}

// ---- The sound -----------------------------------------------------------------------

namespace {
// The cut, in memory: FirstLaunch.wav's samples from where it starts.
std::string gPcm;
uint16_t gChannels = 0, gAlign = 0;
uint32_t gRate = 0;
std::string gCut;   // the file the player has, in the runtime folder
GPid gPlayer = 0;

uint32_t le32(const unsigned char* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
uint16_t le16(const unsigned char* p) { return (uint16_t)(p[0] | p[1] << 8); }
void put32(std::string& s, uint32_t v) { for (int i = 0; i < 4; i++) s.push_back((char)(v >> (8 * i) & 0xff)); }
void put16(std::string& s, uint16_t v) { for (int i = 0; i < 2; i++) s.push_back((char)(v >> (8 * i) & 0xff)); }
}  // namespace

// FirstLaunch.wav from `from` seconds in, kept in memory until it plays.
void prepareSound(bool calm) {
	if (!gPcm.empty()) return;
	const std::string file = resourcesDir() + "/FirstLaunch.wav";
	gchar* raw = nullptr;
	gsize n = 0;
	if (!g_file_get_contents(file.c_str(), &raw, &n, nullptr)) return;
	const unsigned char* b = reinterpret_cast<const unsigned char*>(raw);
	uint16_t kind = 0, channels = 0, align = 0, bits = 0;
	uint32_t rate = 0;
	const unsigned char* data = nullptr;
	uint32_t dataSize = 0;
	if (n > 12 && memcmp(b, "RIFF", 4) == 0 && memcmp(b + 8, "WAVE", 4) == 0) {
		for (gsize at = 12; at + 8 <= n;) {
			const uint32_t size = le32(b + at + 4);
			if (at + 8 + size > n) break;
			if (memcmp(b + at, "fmt ", 4) == 0 && size >= 16) {
				kind = le16(b + at + 8); channels = le16(b + at + 10); rate = le32(b + at + 12);
				align = le16(b + at + 20); bits = le16(b + at + 22);
			} else if (memcmp(b + at, "data", 4) == 0) {
				data = b + at + 8;
				dataSize = size;
			}
			at += 8 + size + (size & 1);
		}
	}
	const double from = calm ? kSoundHit - 0.15 - kCalmHold : kSoundHit - kLedAt;
	const uint64_t skip = (uint64_t)(from * rate) * align;
	if (kind == 1 && bits == 16 && channels > 0 && align == channels * 2 && data && skip < dataSize) {
		gPcm.assign(reinterpret_cast<const char*>(data + skip), dataSize - skip);
		gChannels = channels;
		gAlign = align;
		gRate = rate;
	}
	g_free(raw);
}

// The cut from `late` seconds further in, at 0.75, faded up over 0.3 s from
// there (the Mac's player does it with its volume; a player program here
// has none), written out and handed to the first player there is. However
// long the writing takes counts too.
void playSound(double late) {
	if (gPcm.empty() || gPlayer) return;
	const gint64 began = g_get_monotonic_time();
	const std::string path = std::string(g_get_user_runtime_dir()) + format("/cedarlogic-intro-%d.wav", (int)getpid());
	auto write = [&](double skipSeconds) {
		const size_t total = gPcm.size() / gAlign;
		const size_t skip = std::min(total, (size_t)(std::max(0.0, skipSeconds) * gRate));
		const size_t frames = total - skip;
		const uint32_t len = (uint32_t)(frames * gAlign);
		std::string out;
		out.reserve(44 + len);
		out += "RIFF"; put32(out, 36 + len); out += "WAVE";
		out += "fmt "; put32(out, 16); put16(out, 1); put16(out, gChannels); put32(out, gRate); put32(out, gRate * gAlign);
		put16(out, gAlign); put16(out, 16);
		out += "data"; put32(out, len);
		const size_t fadeFrames = std::max<size_t>(1, (size_t)(0.3 * gRate));
		const unsigned char* src = reinterpret_cast<const unsigned char*>(gPcm.data()) + skip * gAlign;
		for (size_t f = 0; f < frames; f++) {
			const double gain = 0.75 * std::min(1.0, (double)f / fadeFrames);
			for (int c = 0; c < gChannels; c++) {
				const int16_t v = (int16_t)le16(src + f * gAlign + c * 2);
				put16(out, (uint16_t)(int16_t)std::lround(v * gain));
			}
		}
		return g_file_set_contents(path.c_str(), out.data(), (gssize)out.size(), nullptr);
	};
	const bool written = write(late + (g_get_monotonic_time() - began) / 1e6);
	gPcm.clear();
	if (!written) return;
	gCut = path;
	// PipeWire, then PulseAudio, then plain ALSA (as the launch chime).
	for (const char* player : { "pw-play", "paplay", "aplay" }) {
		gchar* found = g_find_program_in_path(player);
		if (found == nullptr) continue;
		const gchar* argv[] = { found, player[0] == 'a' ? "-q" : gCut.c_str(), player[0] == 'a' ? gCut.c_str() : nullptr, nullptr };
		GPid pid = 0;
		if (g_spawn_async(nullptr, (gchar**)argv, nullptr,
		                  (GSpawnFlags)(G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL), nullptr,
		                  nullptr, &pid, nullptr)) {
			gPlayer = pid;
			g_child_watch_add(pid, [](GPid p, gint, gpointer) {
				g_spawn_close_pid(p);
				if (gPlayer == p) gPlayer = 0;
				if (!gCut.empty()) g_remove(gCut.c_str());
				gCut.clear();
			}, nullptr);
		}
		g_free(found);
		break;
	}
	if (!gPlayer && !gCut.empty()) { g_remove(gCut.c_str()); gCut.clear(); }
}

// Skipped: the sound goes with the picture.
void stopSound() {
	gPcm.clear();
	if (gPlayer) kill(gPlayer, SIGTERM);
}

// ---- The picture ---------------------------------------------------------------------

struct Scene::Impl {
	float w, h;
	WireLook look;
	bool live;
	// The icon's art is 512 units square; the circuit is laid out in those
	// units and drawn at k points a unit, centred on (300, 247).
	double k, ox, oy;
	PointF P(double x, double y) const { return { (float)(ox + k * x), (float)(oy + k * y) }; }

	struct Wire {
		Shape path;
		double draw0, draw1, pulse0, pulse1;
	};
	std::vector<Wire> wires;
	std::vector<PointF> pins;
	struct Letter {
		Shape outline;
		PointF centre;
		double popAt, trace0, trace1;
	};
	std::vector<Letter> letters;
	Shape inside;   // the AND gate's inside, lit when it fires
	PointF ledC;
	double lr;
	Shape housing;
	RectF displayBox;
	struct Digit {
		std::vector<Shape> off, on;
		double at;
	};
	std::vector<Digit> digits;
	PointF cardIcon;
	double cardIconSize;
	RectF circuitBox;

	// The canvas and its vignette, and the grid: drawn once.
	cairo_surface_t* bare = nullptr;   // the canvas and vignette
	cairo_surface_t* grid = nullptr;   // the grid alone, falling away to the edges, the vignette over it
	double cachedScale = 0;
	// The small images painted stretched (the Mac's layer contents): the
	// light under the gate, a mote, the lift of light at the hit.
	cairo_surface_t* underImage = nullptr;
	cairo_surface_t* moteImage = nullptr;
	cairo_surface_t* liftImage = nullptr;
	RectF underBox;   // where the light under the gate goes
	// Soft out-of-focus lights drifting slowly over the grid (y down here).
	struct Mote {
		PointF from;
		double dx, dy;     // points a second
		double side, alpha, phase;
	};
	std::vector<Mote> motes;

	Impl(float w_, float h_, const WireLook& l, bool live_) : w(w_), h(h_), look(l), live(live_) {
		// The circuit and its display span about 580 units across.
		k = std::min(w * 0.55 / 580, h * 0.84 / 512);
		ox = w / 2 - 300 * k;
		oy = h / 2 - 247 * k;
		build();
	}
	~Impl() {
		for (cairo_surface_t* x : { bare, grid, underImage, moteImage, liftImage, settled, underFull, liftFull })
			if (x) cairo_surface_destroy(x);
	}

	Shape shapeOf(const std::vector<std::vector<PointF>>& runs, bool closed) const {
		Shape s;
		for (const std::vector<PointF>& r : runs) {
			std::vector<PointF> pts;
			for (const PointF& p : r) pts.push_back(P(p.x, p.y));
			s.add(pts, closed);
		}
		return s;
	}

	void build() {
		// The wires, routed as the canvas routes them, square bends: two inputs
		// through the C into the D (an AND gate), and its output down the L to the LED.
		auto wire = [&](std::vector<PointF> pts, double d0, double d1, double p0, double p1) {
			Wire wr;
			wr.path = shapeOf({ pts }, false);
			wr.draw0 = d0; wr.draw1 = d1; wr.pulse0 = p0; wr.pulse1 = p1;
			wires.push_back(wr);
		};
		wire({ { 20, 217.5f }, { 124, 217.5f }, { 124, 206 }, { 196, 206 } }, 0.40, 1.29, 1.99, 2.29);
		wire({ { 20, 253.5f }, { 132, 253.5f }, { 132, 288 }, { 196, 288 } }, 0.45, 1.31, 1.99, 2.29);
		wire({ { 351, 247 }, { 376, 247 }, { 376, 316 }, { 486, 316 } }, 1.43, 1.96, 2.31, kLedAt);
		pins = { P(14, 217.5), P(14, 253.5) };

		// The letters, as outlines of the icon's: the C (its stroke's outline,
		// in the order Core Graphics makes it: the inside, then back round the outside) ...
		std::vector<PointF> c;
		arc(c, 156, 247, 76, 55, 305);
		arc(c, 156, 247, 100, 305, 55);
		// ... the D, which is an AND gate (its outside, then its inside) ...
		std::vector<std::vector<PointF>> d;
		for (const auto& xr : { std::make_pair(196.0, 123.0), std::make_pair(220.0, 99.0) }) {
			std::vector<PointF> r = { { (float)xr.first, (float)(247 - xr.second) } };
			arc(r, 228, 247, xr.second, -90, 90);
			r.push_back({ (float)xr.first, (float)(247 + xr.second) });
			d.push_back(r);
		}
		// ... and the L, leaning as the icon's does.
		const std::vector<PointF> l = { { 395, 172 }, { 419, 172 }, { 377, 325 }, { 470, 325 }, { 455, 350 }, { 345, 350 } };
		auto letter = [&](const std::vector<std::vector<PointF>>& runs, double popAt, double t0, double t1) {
			Letter lt;
			lt.outline = shapeOf(runs, true);
			const RectF b = lt.outline.bounds();
			lt.centre = { (b.left + b.right) / 2, (b.top + b.bottom) / 2 };
			lt.popAt = popAt; lt.trace0 = t0; lt.trace1 = t1;
			letters.push_back(lt);
		};
		letter({ c }, 0.64, 0.54, 1.12);
		letter(d, 1.26, 1.19, 1.58);
		letter({ l }, 1.55, 1.48, 1.89);
		inside = shapeOf({ d[1] }, true);

		ledC = P(494, 316);
		lr = 8 * k;

		buildDisplay();

		// Where the launch screen's icon will be: the bloomed icon lands on its tile.
		const PointF card = cardOrigin(w, h);
		cardIcon = { w / 2, card.y + 44 + 64 };
		cardIconSize = 128 * panelTile();
		// The circuit and its glows (the LED's at its widest), for its group.
		circuitBox = rectF(P(-20, 0).x, P(0, 60).y, P(700, 0).x, P(0, 520).y);

		// A faint light under the gate, as if the circuit lit the canvas.
		const double glowSide = std::max(w, h) * 0.95;
		const PointF uc = P(250, 250);
		underBox = rectF((float)(uc.x - glowSide / 2), (float)(uc.y - glowSide * 0.36), (float)(uc.x + glowSide / 2), (float)(uc.y + glowSide * 0.36));
		// A few motes: soft, out of focus, drifting very slowly; the Mac's
		// (the same numbers in the same order, its y up turned down).
		uint64_t seed = 0x9E3779B97F4A7C15ull;
		auto rnd = [&seed] {
			seed = seed * 6364136223846793005ull + 1442695040888963407ull;
			return (double)((seed >> 33) % 10000) / 10000;
		};
		for (int i = 0; i < 9; i++) {
			Mote m;
			m.side = 22 + 60 * rnd();
			const double x = w * (0.12 + 0.76 * rnd()), y = h * (0.15 + 0.7 * rnd());
			m.from = { (float)x, (float)(h - y) };
			m.dx = (rnd() - 0.5) * 14;
			m.dy = -(4 + rnd() * 9);
			m.alpha = 0.05 + 0.09 * rnd();
			m.phase = rnd() * 6;
			motes.push_back(m);
		}
	}

	// A seven-segment display beside the LED that spells the version as the
	// LED lights, a digit at a time.
	void buildDisplay() {
		const std::string chars = version();
		int digitsCount = 0;
		for (char ch : chars) digitsCount += g_ascii_isdigit(ch) ? 1 : 0;
		digitsCount = std::max(1, digitsCount);
		const double cellW = 24, gap = 16, hh = 46, x0 = 528, y0 = 293;
		const double boxW = digitsCount * cellW + (digitsCount - 1) * gap + 22;
		{
			// A rounded box, 7 units round.
			const double bx = x0 - 11, by = y0 - 12, bw = boxW, bh = hh + 24, r = 7;
			std::vector<PointF> box;
			arc(box, bx + r, by + bh - r, r, 90, 180);
			arc(box, bx + r, by + r, r, 180, 270);
			arc(box, bx + bw - r, by + r, r, 270, 360);
			arc(box, bx + bw - r, by + bh - r, r, 0, 90);
			housing = shapeOf({ box }, true);
			displayBox = rectF(P(bx, 0).x - 12, P(0, by).y - 12, P(bx + bw, 0).x + 12, P(0, by + bh).y + 12);
		}
		// The segments of one digit (a to g), as hexagonal bars, leaning a
		// little about the baseline, as a display's digits do.
		const double t = 4.6;
		const double base = y0 + hh;
		auto lean = [&](double x, double y) { return P(x - 0.1 * (y - base), y); };
		auto bar = [&](double ax, double ay, double bx, double by) {
			std::vector<std::pair<double, double>> q;
			if (ay == by) q = { { ax + t / 2, ay }, { ax + t, ay - t / 2 }, { bx - t, by - t / 2 }, { bx - t / 2, by }, { bx - t, by + t / 2 }, { ax + t, ay + t / 2 } };
			else q = { { ax, ay + t / 2 }, { ax + t / 2, ay + t }, { bx + t / 2, by - t }, { bx, by - t / 2 }, { bx - t / 2, by - t }, { ax - t / 2, ay + t } };
			Shape s;
			std::vector<PointF> pts;
			for (const auto& p : q) pts.push_back(lean(p.first, p.second));
			s.add(pts, true);
			return s;
		};
		auto segments = [&](double x) {
			const double l = x, r = x + cellW, top = y0, mid = y0 + hh / 2, bottom = y0 + hh;
			return std::vector<Shape>{ bar(l, top, r, top), bar(r, top, r, mid), bar(r, mid, r, bottom), bar(l, bottom, r, bottom),
			                           bar(l, mid, l, bottom), bar(l, top, l, mid), bar(l, mid, r, mid) };
		};
		const std::vector<std::vector<int>> lit = { { 0, 1, 2, 3, 4, 5 }, { 1, 2 }, { 0, 1, 6, 4, 3 }, { 0, 1, 6, 2, 3 }, { 5, 6, 1, 2 },
		                                            { 0, 5, 6, 2, 3 }, { 0, 5, 4, 3, 2, 6 }, { 0, 1, 2 }, { 0, 1, 2, 3, 4, 5, 6 }, { 0, 1, 2, 3, 5, 6 } };
		double x = x0;
		int order = 0;
		for (char ch : chars) {
			Digit dg;
			if (g_ascii_isdigit(ch)) {
				const std::vector<Shape> segs = segments(x);
				const std::vector<int>& on = lit[ch - '0'];
				for (int i = 0; i < 7; i++) (std::find(on.begin(), on.end(), i) != on.end() ? dg.on : dg.off).push_back(segs[i]);
				x += cellW + gap;
			} else if (ch == '.') {
				std::vector<PointF> dot;
				const double cx = x - gap / 2, cy = base;
				for (int i = 0; i < 24; i++) dot.push_back(lean(cx + 3 * std::cos(i * G_PI / 12), cy + 3 * std::sin(i * G_PI / 12)));
				Shape s;
				s.add(dot, true);
				dg.on.push_back(s);
			} else {
				continue;
			}
			dg.at = kLedAt + 0.1 * order++;
			digits.push_back(dg);
		}
	}

	// ---- The canvas, once ----

	// Darker towards the edges, so the eye goes to the middle: the Mac's
	// vignette, an ellipse filling the window.
	cairo_pattern_t* vignette() const {
		cairo_pattern_t* v = cairo_pattern_create_radial(0.5, 0.5, 0, 0.5, 0.5, 0.5);
		cairo_pattern_add_color_stop_rgba(v, 0, 0, 0, 0, 0);
		cairo_pattern_add_color_stop_rgba(v, 0.4, 0, 0, 0, 0.03);
		cairo_pattern_add_color_stop_rgba(v, 0.75, 0, 0, 0, 0.22);
		cairo_pattern_add_color_stop_rgba(v, 1, 0, 0, 0, 0.42);
		cairo_pattern_set_extend(v, CAIRO_EXTEND_PAD);
		cairo_matrix_t m;
		cairo_matrix_init_scale(&m, 1.0 / w, 1.0 / h);
		cairo_pattern_set_matrix(v, &m);
		return v;
	}
	// How dark the vignette is at (x, y), for the motes under it.
	double vignetteAt(double x, double y) const {
		const double r = std::hypot((x - w / 2) / (w / 2), (y - h / 2) / (h / 2));
		const double at[] = { 0, 0.4, 0.75, 1 }, a[] = { 0, 0.03, 0.22, 0.42 };
		for (int i = 1; i < 4; i++)
			if (r <= at[i]) return a[i - 1] + (a[i] - a[i - 1]) * (r - at[i - 1]) / (at[i] - at[i - 1]);
		return a[3];
	}

	void ensureGround(cairo_t* cr) {
		double sx = 1, sy = 1;
		cairo_surface_get_device_scale(cairo_get_target(cr), &sx, &sy);
		if (!underImage) {
			underImage = radialImage(256, { { 0, 0.1, 0.85, 0.34, 0.11 }, { 0.3, 0.08, 0.7, 0.3, 0.06 }, { 0.65, 0.05, 0.5, 0.25, 0.012 },
			                                { 1, 0.05, 0.5, 0.25, 0 } });
			moteImage = radialImage(64, { { 0, 0.75, 1, 0.82, 1 }, { 0.35, 0.6, 1, 0.72, 0.75 }, { 0.7, 0.35, 0.9, 0.5, 0.25 },
			                              { 1, 0.3, 0.9, 0.5, 0 } });
			liftImage = radialImage(256, { { 0, 0.8, 1, 0.85, 0.16 }, { 0.12, 0.45, 1, 0.6, 0.07 }, { 0.45, 0.3, 0.9, 0.5, 0.025 },
			                               { 1, 0.3, 0.9, 0.5, 0.012 } });
		}
		if (bare && cachedScale == sx) return;
		if (bare) cairo_surface_destroy(bare);
		if (grid) cairo_surface_destroy(grid);
		cachedScale = sx;
		const int iw = (int)std::ceil(w), ih = (int)std::ceil(h);
		// The canvas, the vignette over it.
		bare = cairo_surface_create_similar(cairo_get_target(cr), CAIRO_CONTENT_COLOR, iw, ih);
		{
			cairo_t* c = cairo_create(bare);
			set(c, kCanvas);
			cairo_paint(c);
			cairo_pattern_t* v = vignette();
			cairo_set_source(c, v);
			cairo_paint(c);
			cairo_pattern_destroy(v);
			cairo_destroy(c);
		}
		// The canvas's grid, minor and major lines, as the app draws it, on
		// nothing: strongest in the middle, falling away into the dark at the
		// edges (so it reads as a surface going back, not a flat backdrop),
		// and darkened by the vignette as it is over the canvas.
		grid = cairo_surface_create_similar(cairo_get_target(cr), CAIRO_CONTENT_COLOR_ALPHA, iw, ih);
		{
			cairo_t* c = cairo_create(grid);
			cairo_push_group(c);
			const double step = 20, cx = w / 2, cy = h / 2;
			for (int major = 0; major < 2; major++) {
				cairo_new_path(c);
				for (int i = -(int)(cx / step) - 1; i <= (int)(cx / step) + 1; i++) {
					if ((i % 5 == 0) != (major == 1)) continue;
					cairo_move_to(c, cx + i * step, 0);
					cairo_line_to(c, cx + i * step, h);
				}
				for (int i = -(int)(cy / step) - 1; i <= (int)(cy / step) + 1; i++) {
					if ((i % 5 == 0) != (major == 1)) continue;
					cairo_move_to(c, 0, cy + i * step);
					cairo_line_to(c, w, cy + i * step);
				}
				set(c, major ? kGridMajor : kGridMinor);
				cairo_set_line_width(c, major ? 0.75 : 0.5);
				cairo_stroke(c);
			}
			cairo_pop_group_to_source(c);
			// (The Mac's mask: an ellipse 1.3 by 1.4 times the window.)
			cairo_pattern_t* mask = cairo_pattern_create_radial(0, 0, 0, 0, 0, 1);
			cairo_pattern_add_color_stop_rgba(mask, 0, 0, 0, 0, 1);
			cairo_pattern_add_color_stop_rgba(mask, 0.35, 0, 0, 0, 0.8);
			cairo_pattern_add_color_stop_rgba(mask, 0.75, 0, 0, 0, 0.3);
			cairo_pattern_add_color_stop_rgba(mask, 1, 0, 0, 0, 0.12);
			cairo_pattern_set_extend(mask, CAIRO_EXTEND_PAD);
			cairo_matrix_t m;
			cairo_matrix_init_scale(&m, 1 / (0.65 * w), 1 / (0.7 * h));
			cairo_matrix_translate(&m, -cx, -cy);
			cairo_pattern_set_matrix(mask, &m);
			cairo_mask(c, mask);
			cairo_pattern_destroy(mask);
			// The vignette over it, darkening it where it's drawn (atop keeps its alpha).
			cairo_set_operator(c, CAIRO_OPERATOR_ATOP);
			cairo_pattern_t* v = vignette();
			cairo_set_source(c, v);
			cairo_paint(c);
			cairo_pattern_destroy(v);
			cairo_destroy(c);
		}
	}

	// An image painted stretched over the box (x, y, bw, bh) at `opacity`.
	void paintStretched(cairo_t* cr, cairo_surface_t* img, double x, double y, double bw, double bh, double opacity) const {
		if (!img || opacity <= 0.002) return;
		const int iw = cairo_image_surface_get_width(img), ih = cairo_image_surface_get_height(img);
		cairo_save(cr);
		cairo_rectangle(cr, x, y, bw, bh);
		cairo_clip(cr);
		cairo_translate(cr, x, y);
		cairo_scale(cr, bw / iw, bh / ih);
		cairo_set_source_surface(cr, img, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
		cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
		cairo_paint_with_alpha(cr, opacity);
		cairo_restore(cr);
	}

	// Scaled by `s` about the middle (the push-in).
	void about(cairo_t* cr, double s) const {
		cairo_translate(cr, w / 2, h / 2);
		cairo_scale(cr, s, s);
		cairo_translate(cr, -w / 2, -h / 2);
	}
	PointF onStage(PointF q, double near) const {
		return { (float)(w / 2 + (q.x - w / 2) * near), (float)(h / 2 + (q.y - h / 2) * near) };
	}

	// The canvas and what's behind the circuit, pushed in by `far`: the grid
	// at `gridShown`, the light under the gate at `under`, the motes at
	// `moteShown` where they've drifted to by t.
	void paintGround(cairo_t* cr, double gridShown, double far = 1, double under = 0, double moteShown = 0, double t = 0) {
		{
			Timed timed(kBackdrop);
			cairo_set_source_surface(cr, bare, 0, 0);
			cairo_paint(cr);
		}
		cairo_save(cr);
		if (far != 1) about(cr, far);
		if (gridShown > 0.002) {
			Timed timed(kGrid);
			cairo_set_source_surface(cr, grid, 0, 0);
			if (far != 1) cairo_pattern_set_filter(cairo_get_source(cr), live ? CAIRO_FILTER_BILINEAR : CAIRO_FILTER_GOOD);
			cairo_paint_with_alpha(cr, gridShown);
		}
		if (under > 0.002) {
			Timed timed(kUnder);
			paintUnder(cr, far, under);
		}
		Timed timed(kMotes);
		if (moteShown > 0.002)
			for (const Mote& m : motes) {
				const double x = m.from.x + m.dx * t, y = m.from.y + m.dy * t;
				const double o = m.alpha * moteShown * (0.75 + 0.25 * std::sin(t * 1.3 + m.phase)) * (1 - vignetteAt(x, y));
				paintStretched(cr, moteImage, x - m.side / 2, y - m.side / 2, m.side, m.side, o);
			}
		cairo_restore(cr);
	}

	// ---- A moment ----

	void stroke(cairo_t* cr, const Color& c, double opacity, double width, bool round) {
		set(cr, c, opacity);
		cairo_set_line_width(cr, width);
		cairo_set_line_cap(cr, round ? CAIRO_LINE_CAP_ROUND : CAIRO_LINE_CAP_BUTT);
		cairo_set_line_join(cr, round ? CAIRO_LINE_JOIN_ROUND : CAIRO_LINE_JOIN_MITER);
		cairo_stroke(cr);
	}

	// A wire at t: laid in behind its head, which slows into the far end and
	// settles there as its trail draws in after it; then the same again,
	// brighter, for the clock pulse, the wire high behind it.
	void drawWire(cairo_t* cr, const Wire& wr, double t) {
		const double dd = run(t, wr.draw0, wr.draw1), q = run(t, wr.pulse0, wr.pulse1);
		const Shape& p = wr.path;
		const PointF ends[2] = { p.runs[0].pts.front(), p.runs[0].pts.back() };
		// At rest, as the canvas draws it, a dot where it meets each pin ...
		if (dd > 0) {
			p.trace(cr, 0, dd);
			stroke(cr, look.low, 1, look.width, false);
		}
		if (look.dots)
			for (int i = 0; i < 2; i++) {
				const double at = i == 0 ? wr.draw0 : wr.draw1 - 0.02;
				const double o = smooth(t, at, at + 0.08);
				if (o <= 0.002) continue;
				circle(cr, ends[i], look.dot);
				set(cr, look.low, o);
				cairo_fill(cr);
			}
		// ... high, in green with Simulation View's glow, and a wider, softer
		// bloom under that ...
		if (q > 0) {
			const double widths[4] = { look.width + 16, look.width + 7, look.width + 3, look.width };
			const double alphas[4] = { 0.045 * extras, 0.10 * extras, 0.22, 1 };
			for (int i = 0; i < 4; i++) {
				if (alphas[i] <= 0.002) continue;
				p.trace(cr, 0, q);
				stroke(cr, kNeon, alphas[i], widths[i], i == 0);
			}
		}
		if (look.dots)
			for (int i = 0; i < 2; i++) {
				const double at = i == 0 ? wr.pulse0 : wr.pulse1 - 0.02;
				const double o = smooth(t, at, at + 0.08);
				if (o <= 0.002) continue;
				circle(cr, ends[i], look.dot);
				set(cr, kNeon, o);
				cairo_fill(cr);
			}
		// ... and the head's trail and glow, on top: whichever run is going
		// (or has just landed), the pulse, else the drawing in.
		const bool pulsing = t >= wr.pulse0;
		const double f = pulsing ? q : dd, start = pulsing ? wr.pulse0 : wr.draw0, end = pulsing ? wr.pulse1 : wr.draw1;
		const double settle = ease(t, end, end + 0.22);
		const double shown = smooth(t, start, start + 0.05) * (1 - settle);
		if (shown <= 0.002) return;
		// A trail about 90 points long, or the wire, whichever is shorter.
		const double trailFrac = std::min(1.0, 90 / p.length) * (1 - settle);
		const double from = std::max(0.0, f - trailFrac * 0.6);
		if (f - from > 1e-4) {
			p.trace(cr, from, f);
			stroke(cr, kNeon, 0.28 * shown, look.width + 5, true);
		}
		const int n = 6;
		for (int i = 0; i < n; i++) {
			// The steps overlap at the head: brightest there, fading back.
			const double part = (double)(n - i) / n;
			const double s = std::max(0.0, f - trailFrac * part);
			if (f - s <= 1e-4) continue;
			p.trace(cr, s, f);
			stroke(cr, kHead, (0.22 + 0.13 * i) * shown * (pulsing ? 1 : 0.8), look.width + 0.6, true);
		}
		// The head: a soft light that narrows as it lands.
		const double sc = (pulsing ? 1.15 : 0.9) * (1 - 0.45 * settle);
		glow(cr, p.point(f), 15 * sc, shown, tier == Tier::Lite);
	}

	void drawCircuit(cairo_t* cr, double t) {
		Timed timed(kWires);
		// The gate fires: its inside glows green.
		if (t >= 1.26) {
			inside.whole(cr);
			set(cr, kNeon, 0.08 + 0.32 * ease(t, 2.29, 2.45));
			cairo_fill(cr);
		}
		for (const Wire& wr : wires) drawWire(cr, wr, t);

		// The input pins: small rings, as in the icon, easing in with the
		// canvas just before the wires leave them.
		for (size_t i = 0; i < pins.size(); i++) {
			const double at = 0.16 + 0.07 * i;
			const double o = smooth(t, at, at + 0.2);
			if (o <= 0.002) continue;
			const double s = pop(t, at, 0.2);
			cairo_save(cr);
			cairo_translate(cr, pins[i].x, pins[i].y);
			cairo_scale(cr, s, s);
			circle(cr, PointF{}, 6 * k);
			set(cr, kCanvas, o);
			cairo_fill_preserve(cr);
			stroke(cr, kNeon, o, std::max(1.5, 1.4 * k), false);
			cairo_restore(cr);
		}

		for (const Letter& l : letters) {
			const double tr = smooth(t, l.trace0, l.trace1);
			const double shown = smooth(t, l.popAt - 0.06, l.popAt + 0.04);
			if (shown <= 0.002) continue;
			const double sc = pop(t, l.popAt, 0.82);
			cairo_save(cr);
			cairo_translate(cr, l.centre.x, l.centre.y);
			cairo_scale(cr, sc, sc);
			cairo_translate(cr, -l.centre.x, -l.centre.y);
			// The clock pulse catches the outlines as it passes, and they stay
			// lit until the bloom.
			const double glowA = 0.22 + 0.4 * smooth(t, 2.16, 2.4) * (1 - smooth(t, kBloomAt, kBloomAt + 0.2));
			if (tr > 0) {
				l.outline.trace(cr, 0, tr);
				set(cr, kNeon, glowA * shown);
				cairo_set_line_width(cr, 7 * k);
				cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
				cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
				cairo_stroke(cr);
			}
			// Filled faintly once outlined: silver glass.
			const double fill = 0.13 * smooth(t, l.trace1 - 0.15, l.trace1 + 0.1);
			if (fill > 0.002) {
				l.outline.whole(cr);
				cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
				set(cr, kSilver, fill * shown);
				cairo_fill(cr);
				cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
			}
			if (tr > 0) {
				l.outline.trace(cr, 0, tr);
				set(cr, kSilver, shown);
				cairo_set_line_width(cr, std::max(1.4, 1.5 * k));
				cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
				cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
				cairo_stroke(cr);
			}
			cairo_restore(cr);
		}

		// The LED: in when the output reaches it, lit as the pulse lands with
		// a flash that settles, held lit, then swelling into the bloom.
		const double lit = ease(t, kLedAt, kLedAt + 0.06);
		const double fu = clamp01((t - kLedAt) / 0.45);
		const double flash = fu > 0 ? std::pow(1 - fu, 2) * std::min(1.0, fu / 0.08) : 0;
		const double g = 0.6 + 0.8 * lit + 0.55 * flash + 0.4 * smooth(t, kBloomAt - 0.22, kBloomAt);
		glow(cr, ledC, 75 * k * g, lit, tier == Tier::Lite);
		const double ringO = smooth(t, 1.88, 1.98);
		if (ringO > 0.002) {
			const double s = pop(t, 1.91, 0.3);
			cairo_save(cr);
			cairo_translate(cr, ledC.x, ledC.y);
			cairo_scale(cr, s, s);
			circle(cr, PointF{}, lr);
			set(cr, kCanvas, ringO);
			cairo_fill_preserve(cr);
			stroke(cr, kNeon, ringO, std::max(1.5, 1.6 * k), false);
			cairo_restore(cr);
		}
		if (lit > 0.002) {
			const double s = pop(t, kLedAt, 0.7);
			circle(cr, ledC, (lr - 2.5 * k) * s);
			set(cr, kLedCore, lit);
			cairo_fill(cr);
		}
	}

	void drawDisplay(cairo_t* cr, double t) {
		Timed timed(kDisplay);
		housing.whole(cr);
		set(cr, kSilver, 0.06);
		cairo_fill(cr);
		const double drawn = smooth(t, 1.84, 2.13);
		if (drawn > 0) {
			housing.trace(cr, 0, drawn);
			stroke(cr, kSilver, 1, std::max(1.4, 1.5 * k), false);
		}
		// The version, a digit at a time, each flaring as it lights.
		for (const Digit& dg : digits) {
			cairo_new_path(cr);
			for (const Shape& s : dg.off) s.whole(cr, true);
			set(cr, kNeon, 0.07);
			cairo_fill(cr);
			const double on = ease(t, dg.at, dg.at + 0.06);
			if (on <= 0.002) continue;
			const double glowO = on * (0.6 + 0.4 * (1 - smooth(t, dg.at + 0.06, dg.at + 0.36)));
			cairo_new_path(cr);
			for (const Shape& s : dg.on) s.whole(cr, true);
			set(cr, kNeon, 0.3 * glowO);
			cairo_set_line_width(cr, 5);
			cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
			cairo_stroke(cr);
			cairo_new_path(cr);
			for (const Shape& s : dg.on) s.whole(cr, true);
			set(cr, kNeon, on);
			cairo_fill(cr);
		}
	}

	void drawIcon(cairo_t* cr, double t, double near) {
		// Over the circuit, its own art where the circuit was, then flying to
		// where the launch screen's own icon is, and handing over (landed just
		// as the panel is half in, so they don't double up).
		const double landed = kCardIn + kCardFade;
		const double o = smooth(t, kBloomAt + 0.05, kBloomAt + 0.26) * (1 - smooth(t, landed - 0.14, landed));
		if (o <= 0.002) return;
		Timed timed(kIcon);
		const double fly = smooth(t, kBloomAt + 0.14, kBloomAt + 0.44);
		// (From where the circuit is, pushed in.)
		const double bigSide = 512 * k * near, small = cardIconSize;
		const PointF art = onStage(P(256, 256), near);
		const PointF big = { (float)(art.x - bigSide / 2), (float)(art.y - bigSide / 2) };
		const double x = big.x + (cardIcon.x - small / 2 - big.x) * fly, y = big.y + (cardIcon.y - small / 2 - big.y) * fly;
		const double side = bigSide + (small - bigSide) * fly;
		const PointF centre = { (float)(x + side / 2), (float)(y + side / 2) };
		// (Its glow is one of the extras Lite leaves out.)
		glow(cr, centre, side * 1.9 / 2, o * (0.55 + 0.25 * (1 - fly)) * extras, tier == Tier::Lite);
		cairo_surface_t* icon = launchIcon();
		cairo_save(cr);
		if (icon) {
			const int iw = cairo_image_surface_get_width(icon);
			cairo_translate(cr, x, y);
			cairo_scale(cr, side / iw, side / iw);
			cairo_set_source_surface(cr, icon, 0, 0);
			cairo_pattern_set_filter(cairo_get_source(cr), live ? CAIRO_FILTER_BILINEAR : CAIRO_FILTER_GOOD);
			cairo_paint_with_alpha(cr, o);
		} else {
			// The app's own icon has a margin round its tile.
			const double pad = side * (1 / panelTile() - 1) / 2;
			cairo_push_group(cr);
			brand::icon(cr, (float)(x - pad), (float)(y - pad), (float)(side + 2 * pad));
			cairo_pop_group_to_source(cr);
			cairo_paint_with_alpha(cr, o);
		}
		cairo_restore(cr);
	}

	// The bloom: a flash out of the LED that fills the canvas.
	void drawBloom(cairo_t* cr, double t, double near) {
		const double o = 0.9 * smooth(t, kBloomAt - 0.02, kBloomAt + 0.08) * (1 - smooth(t, kBloomAt + 0.14, kBloomAt + 0.56));
		if (o <= 0.002) return;
		Timed timed(kBloom);
		const double bs = 0.04 + 0.96 * ease(t, kBloomAt, kBloomAt + 0.44);
		const double r = std::max(w, h) * 1.6 / 2 * bs;
		// (Drawn once and painted stretched, as the glows are; out past its
		// edge it's clear, and only the canvas it covers is touched.)
		auto bloomImage = [](int side) {
			return radialImage(side, { { 0, 0.9, 1, 0.93, 1 }, { 0.05, 0.45, 1, 0.6, 0.7 }, { 0.22, 0.22, 1, 0.42, 0.16 }, { 0.5, 0.22, 1, 0.42, 0.04 },
			                           { 1, 0.22, 1, 0.42, 0 } },
			                   CAIRO_EXTEND_NONE);
		};
		static cairo_surface_t* image = bloomImage(512);
		static cairo_surface_t* larger = nullptr;   // (Lite's, painted without blending: larger)
		const bool lite = tier == Tier::Lite;
		if (lite && !larger) larger = bloomImage(1024);
		paintRound(cr, lite ? larger : image, onStage(ledC, near), r, o, lite);
	}

	// A part drawn at `opacity` as a whole (in a group, clipped to `box`, only
	// while it's see-through).
	template <class F>
	void layer(cairo_t* cr, double opacity, const RectF& box, F&& f) {
		if (opacity <= 0.002) return;
		if (opacity >= 0.998) { f(); return; }
		cairo_save(cr);
		{
			Timed timed(kGroups);
			cairo_rectangle(cr, box.left, box.top, box.right - box.left, box.bottom - box.top);
			cairo_clip(cr);
			cairo_push_group(cr);
		}
		f();
		Timed timed(kGroups);
		cairo_pop_group_to_source(cr);
		cairo_paint_with_alpha(cr, opacity);
		cairo_restore(cr);
	}

	// The canvas and its grid at `far`, as one image, once it no longer
	// changes (the push-in over or held, the grid all the way up, the light
	// under the gate and the motes gone or left out): one copy a frame
	// instead of two, the grid's scaled.
	cairo_surface_t* settled = nullptr;
	double settledFar = -1, settledScale = 0;
	void paintSettled(cairo_t* cr, double far, double opacity) {
		Timed timed(kBackdrop);
		if (!settled || settledFar != far || settledScale != cachedScale) {
			if (settled) cairo_surface_destroy(settled);
			settled = cairo_surface_create_similar(cairo_get_target(cr), CAIRO_CONTENT_COLOR, (int)std::ceil(w), (int)std::ceil(h));
			settledFar = far;
			settledScale = cachedScale;
			cairo_t* c = cairo_create(settled);
			cairo_set_source_surface(c, bare, 0, 0);
			cairo_paint(c);
			if (far != 1) about(c, far);
			cairo_set_source_surface(c, grid, 0, 0);
			cairo_pattern_set_filter(cairo_get_source(c), CAIRO_FILTER_GOOD);
			cairo_paint_with_alpha(c, 0.8);
			cairo_destroy(c);
		}
		cairo_set_source_surface(cr, settled, 0, 0);
		if (opacity >= 0.998) cairo_paint(cr);
		else cairo_paint_with_alpha(cr, opacity);
	}

	// How much is drawn (Intro.h's Tier): from `liteAt` on, what Lite leaves
	// out fades away, and the push-in eases back out from where it was.
	Tier tier = Tier::Full;
	double liteAt = 0;
	double extras = 1;   // what Lite leaves out, as it fades (1: all of it)

	// Without the push-in, the light under the gate and the lift of light at
	// the hit don't move: each drawn once at the window's size and painted
	// as it is (not stretched from a small image, every pixel, every frame).
	cairo_surface_t* underFull = nullptr;
	cairo_surface_t* liftFull = nullptr;
	double fullScale = 0;
	template <class F>
	void paintFull(cairo_t* cr, cairo_surface_t*& img, double opacity, F&& draw) {
		if (opacity <= 0.002) return;
		if (fullScale != cachedScale) {
			for (cairo_surface_t** x : { &underFull, &liftFull })
				if (*x) { cairo_surface_destroy(*x); *x = nullptr; }
			fullScale = cachedScale;
		}
		if (!img) {
			img = cairo_surface_create_similar(cairo_get_target(cr), CAIRO_CONTENT_COLOR_ALPHA, (int)std::ceil(w), (int)std::ceil(h));
			cairo_t* c = cairo_create(img);
			draw(c);
			cairo_destroy(c);
		}
		cairo_set_source_surface(cr, img, 0, 0);
		cairo_paint_with_alpha(cr, std::min(1.0, opacity));
	}
	void paintUnder(cairo_t* cr, double far, double opacity) {
		auto stretched = [&](cairo_t* c, double o) {
			paintStretched(c, underImage, underBox.left, underBox.top, underBox.right - underBox.left, underBox.bottom - underBox.top, o);
		};
		if (far == 1) paintFull(cr, underFull, opacity, [&](cairo_t* c) { stretched(c, 1); });
		else stretched(cr, opacity);
	}
	void paintLift(cairo_t* cr, double near, double opacity) {
		const double side = std::max(w, h) * 1.6;
		const PointF c = onStage(ledC, near);
		auto stretched = [&](cairo_t* cc, double o) { paintStretched(cc, liftImage, c.x - side / 2, c.y - side / 2, side, side, o); };
		if (near == 1) paintFull(cr, liftFull, opacity, [&](cairo_t* cc) { stretched(cc, 1); });
		else stretched(cr, opacity);
	}

	void draw(cairo_t* cr, double t, double opacity) {
		ensureGround(cr);
		const bool lite = tier == Tier::Lite;
		extras = lite ? 1 - smooth(t, liteAt, liteAt + 0.3) : 1;
		// The camera: a slow push-in until the bloom, the circuit 4%, the
		// backdrop behind it 2% (so they part a little: depth). (None in Lite:
		// from where it had got to, it eases back out.)
		const double push = lite ? smooth(std::min(t, liteAt), 0, kBloomAt + 0.35) * extras : smooth(t, 0, kBloomAt + 0.35);
		const bool pushed = lite ? extras <= 0 : t >= kBloomAt + 0.35;   // where it ends up
		// Once everything but the canvas has gone (the panel is over it), the
		// canvas alone: no group needed to fade it.
		if (t >= kGroundOnly && pushed) {
			if (opacity > 0.002) paintSettled(cr, 1 + 0.02 * push, opacity);
			return;
		}
		layer(cr, opacity, rectF(0, 0, w, h), [&] {
			const double near = 1 + 0.04 * push, far = 1 + 0.02 * push;
			// A breath in just before the hit: the light under the gate and the
			// grid dim a touch for about a tenth of a second, so the LED pops.
			const double breath = smooth(t, kLedAt - 0.14, kLedAt - 0.03) * (1 - ease(t, kLedAt, kLedAt + 0.08));
			// The canvas itself fades in with the whole; its grid comes up after
			// the wires have set off, softly.
			const double gridShown = smooth(t, 0.3, 1.5) * 0.8 * (1 - 0.2 * breath);
			// The light under the gate: faint as the canvas comes up, brighter as
			// the pulse fires the gate and lands in the LED, gone with the circuit.
			const double under = (0.35 * smooth(t, 0.3, 1.6) + 0.4 * smooth(t, 2.1, 2.5) + 0.25 * smooth(t, kLedAt, kLedAt + 0.3)) *
			                     (1 - 0.35 * breath) * (1 - smooth(t, kBloomAt + 0.1, kBloomAt + 0.4));
			// The motes drift, each breathing a little.
			const double moteShown = smooth(t, 0.4, 1.6) * (1 - smooth(t, kBloomAt + 0.05, kBloomAt + 0.35)) * extras;
			if (pushed && t >= 1.5 && breath <= 0 && under <= 0.002 && moteShown <= 0.002) paintSettled(cr, far, 1);
			else paintGround(cr, gridShown, far, under, moteShown, t);
			// The circuit goes as the icon arrives over it; the display (and the
			// canvas) as the launch screen's panel comes in.
			cairo_save(cr);
			about(cr, near);
			layer(cr, 1 - smooth(t, kBloomAt + 0.12, kBloomAt + 0.32), circuitBox, [&] { drawCircuit(cr, t); });
			layer(cr, smooth(t, 1.86, 2.0) * (1 - smooth(t, kCardIn, kCardIn + kCardFade)), displayBox, [&] { drawDisplay(cr, t); });
			cairo_restore(cr);
			// The hit: a lift of light out of the LED over everything, for
			// about 150 ms.
			const double hu = (t - kLedAt) / 0.18;
			if (hu > 0 && hu < 1) {
				Timed timed(kGlows);
				paintLift(cr, near, std::min(1.0, hu / 0.14) * std::pow(1 - hu, 2));
			}
			drawIcon(cr, t, near);
			drawBloom(cr, t, near);
		});
	}
};

Scene::Scene(float w, float h, const WireLook& look, bool live) : d(new Impl(w, h, look, live)) {}
Scene::~Scene() { delete d; }
void Scene::setTier(Tier tier, double at) {
	d->tier = tier;
	d->liteAt = std::max(0.0, at);
}
Tier Scene::tier() const { return d->tier; }
void Scene::prepare(cairo_t* cr) { d->ensureGround(cr); }
void Scene::draw(cairo_t* cr, double t, double opacity) { d->draw(cr, t, opacity); }
void Scene::drawGround(cairo_t* cr, double opacity) {
	d->ensureGround(cr);
	d->layer(cr, opacity, rectF(0, 0, d->w, d->h), [&] { d->paintGround(cr, 0.8); });
}

// ---- Rendering it --------------------------------------------------------------------

// 1280 x 800 at 2x, 60 frames a second, on black: the intro fading in from
// nothing (over the desktop, on screen; a video can't show see-through), the
// launch screen's panel over it from cardIn, and both dissolving into the
// window (`behind`, scaled to cover; else the canvas a new circuit opens on).
int render(const std::string& dir, Trigger trigger, const std::string& behind) {
	g_mkdir_with_parents(dir.c_str(), 0755);
	prefs().load();
	const float W = 1280, H = 800;
	const int scale = 2;
	Scene scene(W, H, WireLook::current(), false);
	cairo_surface_t* back = nullptr;
	if (!behind.empty()) {
		back = cairo_image_surface_create_from_png(behind.c_str());
		if (cairo_surface_status(back) != CAIRO_STATUS_SUCCESS) {
			fprintf(stderr, "render-intro: couldn't read %s; the canvas behind instead\n", behind.c_str());
			cairo_surface_destroy(back);
			back = nullptr;
		}
	}
	const PointF card = cardOrigin(W, H);
	const int frames = (int)std::lround(kTotal * 60);
	int failed = 0;
	for (int f = 0; f <= frames; f++) {
		const double t = f / 60.0;
		cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)W * scale, (int)H * scale);
		cairo_surface_set_device_scale(surface, scale, scale);
		cairo_t* cr = cairo_create(surface);
		cairo_set_source_rgb(cr, 0, 0, 0);
		cairo_paint(cr);
		const double shown = easeInOut((t - kDissolveAt) / kFade);
		// The window under it all, put in as the dissolve begins.
		if (t >= kDissolveAt) {
			if (back) {
				const int bw = cairo_image_surface_get_width(back), bh = cairo_image_surface_get_height(back);
				const double s = std::max(W / bw, H / bh);
				cairo_save(cr);
				cairo_translate(cr, (W - bw * s) / 2, (H - bh * s) / 2);
				cairo_scale(cr, s, s);
				cairo_set_source_surface(cr, back, 0, 0);
				cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
				cairo_paint(cr);
				cairo_restore(cr);
			} else {
				scene.drawGround(cr, 1);
			}
		}
		// The intro, fading in at first as a whole, and out into the window.
		scene.draw(cr, t, (1 - shown) * fadeInShown(t));
		// The launch screen's panel, at its own time.
		if (t >= kCardIn) drawPanelAfterIntro(cr, card.x, card.y, t - kCardIn + kSplashAt, easeInOut((t - kCardIn) / kCardFade) * (1 - shown), false);
		cairo_destroy(cr);
		const std::string file = dir + format("/frame-%04d.png", f);
		if (cairo_surface_write_to_png(surface, file.c_str()) != CAIRO_STATUS_SUCCESS) failed++;
		cairo_surface_destroy(surface);
	}
	if (back) cairo_surface_destroy(back);
	printf("render-intro: %d frames (%s) in %s%s\n", frames + 1, name(trigger == Trigger::None ? Trigger::First : trigger), dir.c_str(),
	       failed ? format(", %d not written", failed).c_str() : "");
	return failed ? 1 : 0;
}

// ---- --intro-check -------------------------------------------------------------------

namespace {

int gFailed = 0;
void expect(bool ok, const std::string& what) {
	printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
	if (!ok) gFailed++;
}

// The settings as a launch reads them, from `text` (nullptr: no file at all).
void launchWith(const char* text) {
	gchar* f = g_build_filename(g_get_user_config_dir(), "CedarLogic", "native.ini", nullptr);
	gchar* dir = g_path_get_dirname(f);
	g_mkdir_with_parents(dir, 0700);
	if (text) g_file_set_contents(f, text, -1, nullptr);
	else g_remove(f);
	g_free(dir);
	g_free(f);
	prefs() = Prefs();
	prefs().load();
}

// An item for `action` anywhere under `model`, and its label.
bool menuHas(GMenuModel* model, const char* action, std::string& label, std::string& in, const std::string& path = "") {
	for (int i = 0; model && i < g_menu_model_get_n_items(model); i++) {
		gchar* a = nullptr;
		gchar* l = nullptr;
		g_menu_model_get_item_attribute(model, i, G_MENU_ATTRIBUTE_ACTION, "s", &a);
		g_menu_model_get_item_attribute(model, i, G_MENU_ATTRIBUTE_LABEL, "s", &l);
		const std::string here = l ? l : "";
		const bool hit = a && strcmp(a, action) == 0;
		if (hit) { label = here; in = path; }
		g_free(a);
		g_free(l);
		if (hit) return true;
		for (const char* link : { G_MENU_LINK_SUBMENU, G_MENU_LINK_SECTION }) {
			GMenuModel* sub = g_menu_model_get_item_link(model, i, link);
			const bool found = sub && menuHas(sub, action, label, in, strcmp(link, G_MENU_LINK_SUBMENU) == 0 ? here : path);
			if (sub) g_object_unref(sub);
			if (found) return true;
		}
	}
	return false;
}

// The intro's clock with frames coming as `gap(f)` says (seconds after frame
// f) and taking `draw(f)` ms to draw, the sound owed: what it did.
struct Played {
	double firstStep = -1, maxStep = 0, wall = 0;
	bool backwards = false;
	int frames = 0;
	Tier tier = Tier::Full;
	double tierAt = 0;
	bool sound = false;
	double soundFrom = -1;
	// Where the sound was (in the picture's time, the bell at kLedAt) as the
	// picture reached the LED: 0, the bell right on it.
	double bellAtLed = -99;
};
template <class Gap, class Draw>
Played play(Gap gap, Draw draw) {
	Pacer p;
	p.soundOwed();
	Played r;
	double now = 1000;
	for (int f = 0; f < 20000 && p.t() < kTotal; f++) {
		const double before = p.t();
		const double step = p.frame(now, draw(f));
		if (f == 0) r.firstStep = step;
		r.maxStep = std::max(r.maxStep, step);
		if (p.t() < before) r.backwards = true;
		const double from = p.soundStart(now);
		if (from >= 0) {
			p.soundStarted(now, from);
			r.sound = true;
			r.soundFrom = from;
		}
		if (before < kLedAt && p.t() >= kLedAt && p.soundStarted()) r.bellAtLed = p.soundTime(now) - kLedAt;
		r.frames++;
		now += gap(f);
	}
	r.wall = now - 1000;
	r.tier = p.tier();
	r.tierAt = p.tierAt();
	return r;
}

void expectPlayed(const char* how, const Played& r, double gap, Tier tier, bool bellChecked = true) {
	expect(r.firstStep == 0 && r.maxStep <= kMaxStep + 1e-9 && !r.backwards,
	       format("%s: the clock starts at 0 with the first frame and never moves on more than %.1f ms a frame (most %.1f ms)", how,
	              kMaxStep * 1000, r.maxStep * 1000));
	expect(r.tier == tier, format("%s: drawn %s (from %.2f s), %.2f s to play it, %d frames", how, name(r.tier), r.tierAt, r.wall, r.frames));
	if (bellChecked)
		expect(r.sound && r.bellAtLed >= -1e-6 && r.bellAtLed <= std::max(gap, kMaxStep) + 0.005,
		       format("%s: the bell lands on the LED (the sound from %.2f s into its cut, %.0f ms off at the LED)", how, r.soundFrom, r.bellAtLed * 1000));
}

}  // namespace

Trigger parse(const char* s) {
	if (s == nullptr) return Trigger::None;
	return strcmp(s, "update") == 0 ? Trigger::Update : strcmp(s, "every") == 0 ? Trigger::Every : Trigger::First;
}

int check(GMenuModel* menubar) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	const std::string v = whatsnew::kVersion;

	// ---- The timeline ----
	expect(std::fabs(kTotal - 6.07) < 0.005 && std::fabs(kDissolveAt - 5.52) < 0.005,
	       format("about 6 s in all (%.2f), dissolving at %.2f", kTotal, kDissolveAt));
	expect(std::fabs(kCardIn + kSweepEnd - kSplashAt - 4.42) < 0.005 && std::fabs(kDissolveAt - (kCardIn + kSweepEnd - kSplashAt) - 1.1) < 1e-9,
	       "the panel's sweep ends at 4.42 s and it holds, finished, 1.1 s");
	expect((int)std::lround(kTotal * 60) + 1 == 365, "365 frames at 60 fps");

	// ---- Its clock, with frames coming quickly, slowly, and late ----
	{
		const double f60 = 1.0 / 60;
		const Played fast = play([&](int) { return f60; }, [](int) { return 5.0; });
		expectPlayed("60 fps", fast, f60, Tier::Full);
		expect(std::fabs(fast.wall - kTotal) < 0.1, format("60 fps: it keeps time (%.2f s for %.2f s)", fast.wall, kTotal));
		const Played thirty = play([](int) { return 1.0 / 30; }, [](int) { return 12.0; });
		expectPlayed("30 fps", thirty, 1.0 / 30, Tier::Lite);
		expect(std::fabs(thirty.wall - kTotal) < 0.15, format("30 fps: it still keeps time (%.2f s)", thirty.wall));
		// A stall of 0.6 s in the middle (the machine busy): one frame's step, not a jump.
		const Played stall = play([&](int f) { return f == 100 ? 0.6 : f60; }, [](int) { return 5.0; });
		expectPlayed("60 fps with a 0.6 s stall", stall, f60, Tier::Full, false);
		expect(stall.wall > kTotal + 0.2, format("the stall makes it longer (%.2f s), not skip ahead", stall.wall));
		// Slow to draw at 60 Hz (20 ms a frame: every other refresh).
		const Played heavy = play([](int) { return 2.0 / 60; }, [](int) { return 20.0; });
		expectPlayed("20 ms draws", heavy, 2.0 / 60, Tier::Lite);
		// A Raspberry Pi drawing a large window: 40 ms, and 100 ms, a frame.
		const Played pi = play([](int) { return 0.04; }, [](int) { return 35.0; });
		expectPlayed("40 ms frames", pi, 0.04, Tier::Lite);
		const Played slow = play([](int) { return 0.1; }, [](int) { return 90.0; });
		expectPlayed("100 ms frames", slow, 0.1, Tier::Lite);
		expect(slow.tierAt < 0.5, format("100 ms frames: Lite from the first frames (%.2f s)", slow.tierAt));
		// Uneven frames, 8 to 30 ms apart.
		uint64_t seed = 7;
		auto jitter = [&](int) {
			seed = seed * 6364136223846793005ull + 1442695040888963407ull;
			return 0.008 + 0.022 * ((seed >> 33) % 1000) / 1000.0;
		};
		const Played uneven = play(jitter, [](int) { return 6.0; });
		expect(uneven.firstStep == 0 && uneven.maxStep <= kMaxStep + 1e-9 && !uneven.backwards,
		       format("uneven frames: never more than %.1f ms a frame (most %.1f ms)", kMaxStep * 1000, uneven.maxStep * 1000));
		// Quick, then slow from 1 s in: still never a jump, and Lite from then.
		double clock = 0;
		const Played later = play([&](int) { const double g = clock < 1 ? f60 : 0.05; clock += g; return g; }, [&](int) { return clock < 1 ? 5.0 : 45.0; });
		expectPlayed("quick, then slow from 1 s", later, 0.05, Tier::Lite, false);
	}

	// ---- Which launch plays which, and the sound ----
	launchWith(nullptr);
	expect(prefs().introEveryLaunch, "\"Play the intro every time CedarLogic opens\" is on for a new install");
	expect(forLaunch(prefs()) == Trigger::First && hasSound(Trigger::First), "the first launch plays the first intro, with the sound");
	const std::string seen = "[CedarLogic]\nhasSeenWelcome=true\nfirstLaunchPlayed=true\nintroFirstLaunchPlayed=true\n";
	launchWith((seen + "seenWhatsNew=0.3\n").c_str());
	expect(forLaunch(prefs()) == Trigger::Update, format("the first launch after an update plays the update intro (%s)", name(forLaunch(prefs()))));
	expect(hasSound(forLaunch(prefs())), "and it has the sound");
	prefs().introEveryLaunch = true;
	expect(forLaunch(prefs()) == Trigger::Update && hasSound(forLaunch(prefs())), "an update's intro has the sound, setting or not");
	launchWith((seen + "seenWhatsNew=" + v + "\nintroUpdatePlayed=" + v + "\n").c_str());
	expect(prefs().introEveryLaunch && forLaunch(prefs()) == Trigger::Every && !hasSound(forLaunch(prefs())),
	       format("with nothing saved for it, it's on: a later launch plays the intro, silently (%s)", name(forLaunch(prefs()))));
	prefs().introEveryLaunch = false;
	prefs().save();
	launchWith((seen + "seenWhatsNew=" + v + "\nintroUpdatePlayed=" + v + "\nintroEveryLaunch=false\n").c_str());
	expect(!prefs().introEveryLaunch && forLaunch(prefs()) == Trigger::None, "turned off in Settings, it stays off: a later launch plays no intro");
	prefs().introEveryLaunch = true;
	prefs().save();
	launchWith((seen + "seenWhatsNew=" + v + "\nintroUpdatePlayed=" + v + "\nintroEveryLaunch=true\n").c_str());
	expect(prefs().introEveryLaunch, "the setting is kept");
	expect(forLaunch(prefs()) == Trigger::Every, format("with it on, a later launch plays the intro (%s)", name(forLaunch(prefs()))));
	expect(!hasSound(forLaunch(prefs())), "an every-launch intro has no sound");
	markPlayed(Trigger::Every);
	expect(prefs().introFirstLaunchPlayed && prefs().introUpdatePlayed == v, "playing it leaves the once-only keys as they were");
	expect(hasSound(Trigger::First) && hasSound(Trigger::Update) && !hasSound(Trigger::Every) && !hasSound(Trigger::None),
	       "sound for the first launch and updates only");
	expect(parse("first") == Trigger::First && parse("update") == Trigger::Update && parse("every") == Trigger::Every,
	       "CL_INTRO=first|update|every names each");
	launchWith((seen + "seenWhatsNew=0.3\n").c_str());
	markPlayed(Trigger::Update);
	expect(forLaunch(prefs()) == Trigger::None, "the update intro plays once");

	// ---- Help > Play the Intro ----
	const actions::Item* a = actions::find("playIntro");
	expect(a && strcmp(a->menu, "Help") == 0 && strcmp(a->title, "Play the Intro") == 0 && strcmp(a->action, "app.play-intro") == 0,
	       "Play the Intro is among the actions, in Help");
	std::string label, in;
	const bool there = menuHas(menubar, "app.play-intro", label, in);
	expect(there && label == "Play the _Intro" && in == "_Help", format("Help > Play the Intro is in the menu bar (\"%s\" in \"%s\")", label.c_str(), in.c_str()));

	printf("%s\n", gFailed == 0 ? "intro-check: all passed" : format("intro-check: %d failed", gFailed).c_str());
	return gFailed == 0 ? 0 : 1;
}


}  // namespace intro
