// The intro (see Intro.h): the Mac's IntroScene (mac/App/Intro.swift), drawn
// with Cairo. The same layout in the icon's 512 units, the same timings and
// curves, the same colours; Core Animation's layers become a paint a frame,
// and its strokeEnd a part of a path measured along its length.
//
// Cheap enough for a Raspberry Pi: the canvas, grid and vignette are drawn
// once into two images and painted each frame; everything else is a few
// dozen strokes and fills, and gradients for the glows. Only while a part of
// it is see-through (fading in, the circuit going, the dissolve) is it drawn
// into a group of its own, clipped to where that part is.

#include "Intro.h"
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

// A soft green glow, white in the middle (wire heads, the LED, the icon).
void glow(cairo_t* cr, PointF c, double radius, double opacity) {
	if (opacity <= 0.002 || radius <= 0.1) return;
	cairo_pattern_t* g = cairo_pattern_create_radial(c.x, c.y, 0, c.x, c.y, radius);
	cairo_pattern_add_color_stop_rgba(g, 0, 1, 1, 1, opacity);
	cairo_pattern_add_color_stop_rgba(g, 0.12, 0.6, 1, 0.72, 0.85 * opacity);
	cairo_pattern_add_color_stop_rgba(g, 0.4, 0.22, 1, 0.42, 0.25 * opacity);
	cairo_pattern_add_color_stop_rgba(g, 1, 0.22, 1, 0.42, 0);
	cairo_set_source(cr, g);
	cairo_arc(cr, c.x, c.y, radius, 0, 2 * G_PI);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
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

// ---- When ----------------------------------------------------------------------------

const char* name(Trigger t) { return t == Trigger::Update ? "update" : t == Trigger::First ? "first" : "none"; }

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
Trigger owed() {
	const Prefs& p = prefs();
	if (!p.hasSeenWelcome && !p.firstLaunchPlayed && !p.introFirstLaunchPlayed) return Trigger::First;
	const bool whatsNew = p.hasSeenWelcome && p.seenWhatsNew != whatsnew::kVersion;
	if (whatsNew && p.introUpdatePlayed != whatsnew::kVersion) return Trigger::Update;
	return Trigger::None;
}

// Never in the tests, the picture runs, CI or any run with a `--` option.
static bool allowed() {
	for (const char* v : { "CL_NO_INTRO", "CL_NO_SPLASH", "CI" })
		if (g_getenv(v)) return false;
	return !gDashArg;
}

Trigger pending() {
	if (!allowed()) return Trigger::None;
	if (const char* forced = g_getenv("CL_INTRO")) return strcmp(forced, "update") == 0 ? Trigger::Update : Trigger::First;
	return owed();
}

void markPlayed(Trigger t) {
	if (t == Trigger::First) prefs().introFirstLaunchPlayed = true;
	if (t == Trigger::Update) prefs().introUpdatePlayed = whatsnew::kVersion;
}

double freezeAt() {
	const char* f = g_getenv("CL_INTRO_FREEZE");
	return f ? g_ascii_strtod(f, nullptr) : 1e9;
}

PointF cardOrigin(float w, float h) { return { w / 2 - 280, h / 2 - 36 - 186 }; }

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
std::string gCut;   // the cut copy, in the runtime folder
GPid gPlayer = 0;

uint32_t le32(const unsigned char* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
uint16_t le16(const unsigned char* p) { return (uint16_t)(p[0] | p[1] << 8); }
void put32(std::string& s, uint32_t v) { for (int i = 0; i < 4; i++) s.push_back((char)(v >> (8 * i) & 0xff)); }
void put16(std::string& s, uint16_t v) { for (int i = 0; i < 2; i++) s.push_back((char)(v >> (8 * i) & 0xff)); }
}  // namespace

// FirstLaunch.wav from `from` seconds in, at 0.75, faded up over 0.3 s (the
// Mac's player does it with its volume; a player program here has none).
void prepareSound(bool calm) {
	if (!gCut.empty()) return;
	const std::string file = resourcesDir() + "/FirstLaunch.wav";
	gchar* raw = nullptr;
	gsize n = 0;
	if (!g_file_get_contents(file.c_str(), &raw, &n, nullptr)) return;
	const unsigned char* b = reinterpret_cast<const unsigned char*>(raw);
	uint16_t kind = 0, channels = 0, align = 0, bits = 0;
	uint32_t rate = 0, byteRate = 0;
	const unsigned char* data = nullptr;
	uint32_t dataSize = 0;
	if (n > 12 && memcmp(b, "RIFF", 4) == 0 && memcmp(b + 8, "WAVE", 4) == 0) {
		for (gsize at = 12; at + 8 <= n;) {
			const uint32_t size = le32(b + at + 4);
			if (at + 8 + size > n) break;
			if (memcmp(b + at, "fmt ", 4) == 0 && size >= 16) {
				kind = le16(b + at + 8); channels = le16(b + at + 10); rate = le32(b + at + 12);
				byteRate = le32(b + at + 16); align = le16(b + at + 20); bits = le16(b + at + 22);
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
		std::string out;
		const uint32_t len = (uint32_t)(dataSize - skip);
		out.reserve(44 + len);
		out += "RIFF"; put32(out, 36 + len); out += "WAVE";
		out += "fmt "; put32(out, 16); put16(out, 1); put16(out, channels); put32(out, rate); put32(out, byteRate); put16(out, align); put16(out, 16);
		out += "data"; put32(out, len);
		const size_t frames = len / align, fadeFrames = std::max<size_t>(1, (size_t)(0.3 * rate));
		const unsigned char* src = data + skip;
		for (size_t f = 0; f < frames; f++) {
			const double gain = 0.75 * std::min(1.0, (double)f / fadeFrames);
			for (int c = 0; c < channels; c++) {
				const int16_t v = (int16_t)le16(src + f * align + c * 2);
				put16(out, (uint16_t)(int16_t)std::lround(v * gain));
			}
		}
		const std::string path = std::string(g_get_user_runtime_dir()) + format("/cedarlogic-intro-%d.wav", (int)getpid());
		if (g_file_set_contents(path.c_str(), out.data(), (gssize)out.size(), nullptr)) gCut = path;
	}
	g_free(raw);
}

void playSound() {
	if (gCut.empty() || gPlayer) return;
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

	// The canvas, its vignette, and the grid: drawn once.
	cairo_surface_t* bare = nullptr;   // the canvas and vignette
	cairo_surface_t* grid = nullptr;   // the same with the grid on it, at full strength
	double cachedScale = 0;

	Impl(float w_, float h_, const WireLook& l, bool live_) : w(w_), h(h_), look(l), live(live_) {
		// The circuit and its display span about 580 units across.
		k = std::min(w * 0.55 / 580, h * 0.84 / 512);
		ox = w / 2 - 300 * k;
		oy = h / 2 - 247 * k;
		build();
	}
	~Impl() {
		if (bare) cairo_surface_destroy(bare);
		if (grid) cairo_surface_destroy(grid);
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

	void ensureGround(cairo_t* cr) {
		double sx = 1, sy = 1;
		cairo_surface_get_device_scale(cairo_get_target(cr), &sx, &sy);
		if (bare && cachedScale == sx) return;
		if (bare) cairo_surface_destroy(bare);
		if (grid) cairo_surface_destroy(grid);
		cachedScale = sx;
		const int iw = (int)std::ceil(w), ih = (int)std::ceil(h);
		for (int withGrid = 0; withGrid < 2; withGrid++) {
			cairo_surface_t* s = cairo_surface_create_similar(cairo_get_target(cr), CAIRO_CONTENT_COLOR, iw, ih);
			cairo_t* c = cairo_create(s);
			set(c, kCanvas);
			cairo_paint(c);
			if (withGrid) {
				// The canvas's grid, minor and major lines, as the app draws it.
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
			}
			// Darker towards the edges, so the eye goes to the middle.
			cairo_pattern_t* v = cairo_pattern_create_radial(0.5, 0.5, 0, 0.5, 0.5, 0.5);
			cairo_pattern_add_color_stop_rgba(v, 0, 0, 0, 0, 0);
			cairo_pattern_add_color_stop_rgba(v, 0.45, 0, 0, 0, 0.02);
			cairo_pattern_add_color_stop_rgba(v, 0.8, 0, 0, 0, 0.16);
			cairo_pattern_add_color_stop_rgba(v, 1, 0, 0, 0, 0.3);
			cairo_pattern_set_extend(v, CAIRO_EXTEND_PAD);
			cairo_matrix_t m;
			cairo_matrix_init_scale(&m, 1.0 / w, 1.0 / h);
			cairo_pattern_set_matrix(v, &m);
			cairo_set_source(c, v);
			cairo_paint(c);
			cairo_pattern_destroy(v);
			cairo_destroy(c);
			(withGrid ? grid : bare) = s;
		}
	}

	void paintGround(cairo_t* cr, double gridShown) {
		cairo_set_source_surface(cr, bare, 0, 0);
		cairo_paint(cr);
		// The grid over the bare canvas at `gridShown`: the same as drawing
		// its lines that faint (over is linear in the alpha).
		if (gridShown > 0.002) {
			cairo_set_source_surface(cr, grid, 0, 0);
			cairo_paint_with_alpha(cr, gridShown);
		}
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
		// ... high, in green with Simulation View's glow ...
		if (q > 0) {
			const double widths[3] = { look.width + 7, look.width + 3, look.width };
			const double alphas[3] = { 0.10, 0.22, 1 };
			for (int i = 0; i < 3; i++) {
				p.trace(cr, 0, q);
				stroke(cr, kNeon, alphas[i], widths[i], false);
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
		glow(cr, p.point(f), 15 * sc, shown);
	}

	void drawCircuit(cairo_t* cr, double t) {
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
		glow(cr, ledC, 75 * k * g, lit);
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

	void drawIcon(cairo_t* cr, double t) {
		// Over the circuit, its own art where the circuit was, then flying to
		// where the launch screen's own icon is, and handing over (landed just
		// as the panel is half in, so they don't double up).
		const double landed = kCardIn + kCardFade;
		const double o = smooth(t, kBloomAt + 0.05, kBloomAt + 0.26) * (1 - smooth(t, landed - 0.14, landed));
		if (o <= 0.002) return;
		const double fly = smooth(t, kBloomAt + 0.14, kBloomAt + 0.44);
		const PointF big = P(0, 0);
		const double bigSide = 512 * k, small = cardIconSize;
		const double x = big.x + (cardIcon.x - small / 2 - big.x) * fly, y = big.y + (cardIcon.y - small / 2 - big.y) * fly;
		const double side = bigSide + (small - bigSide) * fly;
		const PointF centre = { (float)(x + side / 2), (float)(y + side / 2) };
		glow(cr, centre, side * 1.9 / 2, o * (0.55 + 0.25 * (1 - fly)));
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
	void drawBloom(cairo_t* cr, double t) {
		const double o = 0.9 * smooth(t, kBloomAt - 0.02, kBloomAt + 0.08) * (1 - smooth(t, kBloomAt + 0.14, kBloomAt + 0.56));
		if (o <= 0.002) return;
		const double bs = 0.04 + 0.96 * ease(t, kBloomAt, kBloomAt + 0.44);
		const double r = std::max(w, h) * 1.6 / 2 * bs;
		cairo_pattern_t* g = cairo_pattern_create_radial(ledC.x, ledC.y, 0, ledC.x, ledC.y, r);
		cairo_pattern_add_color_stop_rgba(g, 0, 0.9, 1, 0.93, o);
		cairo_pattern_add_color_stop_rgba(g, 0.05, 0.45, 1, 0.6, 0.7 * o);
		cairo_pattern_add_color_stop_rgba(g, 0.22, 0.22, 1, 0.42, 0.16 * o);
		cairo_pattern_add_color_stop_rgba(g, 0.5, 0.22, 1, 0.42, 0.04 * o);
		cairo_pattern_add_color_stop_rgba(g, 1, 0.22, 1, 0.42, 0);
		cairo_set_source(cr, g);
		// (Out past its edge it's clear: only the canvas it covers is filled.)
		const double l = std::max(0.0, ledC.x - r), tp = std::max(0.0, ledC.y - r);
		cairo_rectangle(cr, l, tp, std::min((double)w, ledC.x + r) - l, std::min((double)h, ledC.y + r) - tp);
		cairo_fill(cr);
		cairo_pattern_destroy(g);
	}

	// A part drawn at `opacity` as a whole (in a group, clipped to `box`, only
	// while it's see-through).
	template <class F>
	void layer(cairo_t* cr, double opacity, const RectF& box, F&& f) {
		if (opacity <= 0.002) return;
		if (opacity >= 0.998) { f(); return; }
		cairo_save(cr);
		cairo_rectangle(cr, box.left, box.top, box.right - box.left, box.bottom - box.top);
		cairo_clip(cr);
		cairo_push_group(cr);
		f();
		cairo_pop_group_to_source(cr);
		cairo_paint_with_alpha(cr, opacity);
		cairo_restore(cr);
	}

	void draw(cairo_t* cr, double t, double opacity) {
		ensureGround(cr);
		layer(cr, opacity, rectF(0, 0, w, h), [&] {
			// The canvas itself fades in with the whole; its grid comes up a little after.
			paintGround(cr, smooth(t, 0.1, 0.75) * 0.85);
			// The circuit goes as the icon arrives over it; the display (and the
			// canvas) as the launch screen's panel comes in.
			layer(cr, 1 - smooth(t, kBloomAt + 0.12, kBloomAt + 0.32), circuitBox, [&] { drawCircuit(cr, t); });
			layer(cr, smooth(t, 1.86, 2.0) * (1 - smooth(t, kCardIn, kCardIn + kCardFade)), displayBox, [&] { drawDisplay(cr, t); });
			drawIcon(cr, t);
			drawBloom(cr, t);
		});
	}
};

Scene::Scene(float w, float h, const WireLook& look, bool live) : d(new Impl(w, h, look, live)) {}
Scene::~Scene() { delete d; }
void Scene::draw(cairo_t* cr, double t, double opacity) { d->draw(cr, t, opacity); }
void Scene::drawGround(cairo_t* cr, double opacity) {
	d->ensureGround(cr);
	d->layer(cr, opacity, rectF(0, 0, d->w, d->h), [&] { d->paintGround(cr, 0.85); });
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

}  // namespace intro
