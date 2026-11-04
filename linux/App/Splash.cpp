// The launch screen (see Splash.h). Drawn with Cairo into a window with real
// transparency where the desktop composites (so the panel's corners are
// round and its shadow soft); on one that doesn't, the panel fills the
// window. Blurs are drawn as fades and soft glows.
//
// When the intro is owed (Intro.h), the window is the size of the circuit
// window and where it will come in: the intro plays in it, then this panel
// over it, part-way into its own appearance, which holds a moment to be read
// and dissolves into the window. Help > Play the Intro puts the same window
// over the front circuit window (splashPlayIntroNow), dissolving back into it.

#include "Splash.h"
#include "Brand.h"
#include "Intro.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

// ---- The look ------------------------------------------------------------------

const float kW = 560, kH = 372;     // the panel, in points
const float kMargin = 44;           // room around it for the shadow
const float kRadius = 30;

const Color kNeon = colorF(0.22f, 1.0f, 0.42f), kNeonDeep = colorF(0.05f, 0.78f, 0.26f);
const Color kDim = colorF(0.62f, 0.74f, 0.66f);
const Color kGroundTop = colorF(0.042f, 0.078f, 0.056f), kGroundBottom = colorF(0.014f, 0.029f, 0.021f);

Color alpha(Color c, float a) { c.a = a; return c; }
double ease(double x) { const double c = std::min(1.0, std::max(0.0, x)); return 1 - std::pow(1 - c, 3); }
double span(double t, double a, double b) { return ease((t - a) / (b - a)); }
double smooth(double x) { const double c = std::min(1.0, std::max(0.0, x)); return c * c * (3 - 2 * c); }
double now() { return g_get_monotonic_time() / 1e6; }
void stop(cairo_pattern_t* p, double at, const Color& c) { cairo_pattern_add_color_stop_rgba(p, at, c.r, c.g, c.b, c.a); }

bool reduceMotion() {
	gboolean animations = TRUE;
	if (GtkSettings* s = gtk_settings_get_default()) g_object_get(s, "gtk-enable-animations", &animations, nullptr);
	return !animations;
}

// ---- The timeline --------------------------------------------------------------

struct Timeline {
	bool first = false;     // the very first launch: slower, with the sound
	bool calm = false;      // the desktop's animations are off
	bool afterIntro = false;   // played after the intro (Intro.h)
	double dissolveAt = 2.25, fadeTime = 0.42, readyAt = -1;
	std::string title = "CedarLogic";
	struct Step { double at; std::string text; double progress; };
	std::vector<Step> steps;

	// After the intro: "Ready", the version in its title, the rest of its
	// appearance from intro::kSplashAt on its own clock (the bar filling as it
	// goes), its sweep at full length, a hold, finished, to be read, and the
	// dissolve (as the Mac's Splash.configureAfterIntro). With `calm`, the
	// still, briefly.
	void planAfterIntro(bool calm_) {
		afterIntro = true;
		first = false;
		calm = calm_;
		title = std::string("CedarLogic ") + intro::version();
		steps = { { -100, "Ready", 1 } };
		readyAt = 0;
		dissolveAt = calm ? intro::kCalmHold : intro::kSplashAt + intro::kDissolveAt - intro::kCardIn;
		fadeTime = calm ? intro::kCalmFade : intro::kFade;
	}

	// `head`: the seconds the launch's work already took with the panel up
	// (still clear). The Mac counts its timeline from the launch, so slow
	// work shortens what's left of it; here the work blocks the drawing, so
	// the panel plays after it, quicker by what the work took: the steps
	// close up (to 0.2 s apart) and it dissolves at 1.8 s at the earliest,
	// once the icon, traces and name are all in.
	void plan(int gates, int families, const std::string& opening, double head = 0) {
		fadeTime = first ? 1.1 : 0.42;
		if (calm) dissolveAt = 1.1;
		const double target = std::max(1.8, 2.3 - std::max(0.0, head));
		const double spacing = calm ? 0.12 : first ? 0.72 : std::min(0.32, std::max(0.2, (target - 0.6) / 6));
		double at = 0;
		auto step = [&](const std::string& text, double progress) {
			at += spacing;
			steps.push_back({ at, text, progress });
		};
		step("Loading the gate library", 0.12);
		step(format("%d gates in %d families", gates, families), 0.34);
		step("Drawing the palette", 0.5);
		step("Starting the simulator", 0.66);
		step(opening.empty() ? std::string("Opening a new circuit") : "Opening “" + opening + "”", 0.86);
		step("Ready", 1);
		readyAt = at + 0.3;
		dissolveAt = first ? std::max(4.9, std::min(7.0, readyAt + 0.3)) : std::min(6.0, std::max(calm ? 1.1 : target, readyAt + 0.3));
	}

	void status(double t, std::string& now, std::string& before, double& mix) const {
		now = "Starting up";
		before.clear();
		double changedAt = -1;
		for (const Step& s : steps) {
			if (s.at > t) break;
			before = now;
			now = s.text;
			changedAt = s.at;
		}
		mix = changedAt < 0 ? 1 : std::min(1.0, (t - changedAt) / 0.2);
	}

	double progress(double t) const {
		// After the intro: gliding home from where it'd be as the panel comes
		// in, full as the sweep ends, then held there while the panel is read.
		if (afterIntro) {
			if (calm) return 1;
			const double from = intro::kSplashAt, end = intro::kSweepEnd;
			return 0.3 + 0.7 * smooth((t - from) / (end - from));
		}
		const double pace = calm ? 0.9 : first ? 3.9 : 2.1;
		auto early = [&](double x) { return 0.92 * smooth(x / pace); };
		if (readyAt < 0 || t <= readyAt) return early(t);
		const double from = early(readyAt);
		const double end = std::max(readyAt + 0.25, dissolveAt - 0.05);
		return from + (1 - from) * smooth((t - readyAt) / (end - readyAt));
	}
};

// ---- Drawing -------------------------------------------------------------------

void polyline(cairo_t* cr, const std::vector<PointF>& pts, double progress) {
	double total = 0;
	for (size_t i = 1; i < pts.size(); i++) total += std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
	double left = total * std::min(1.0, std::max(0.0, progress));
	cairo_new_path(cr);
	cairo_move_to(cr, pts[0].x, pts[0].y);
	for (size_t i = 1; i < pts.size() && left > 0; i++) {
		const double len = std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
		if (len <= left) { cairo_line_to(cr, pts[i].x, pts[i].y); left -= len; continue; }
		const double k = left / len;
		cairo_line_to(cr, pts[i - 1].x + (pts[i].x - pts[i - 1].x) * k, pts[i - 1].y + (pts[i].y - pts[i - 1].y) * k);
		left = 0;
	}
}

const float kTitleSize = 36, kLetterGap = 0.5f;

// The panel itself, 560 x 372 points at the origin, at time t.
void drawPanel(cairo_t* cr, const Timeline& tl, double t) {
	const bool calm = tl.calm;
	const double rise = calm ? span(t, 0, 0.3) : span(t, 0.08, 0.85);
	const double glow = span(t, 0.3, 1.1) * (0.85 + 0.15 * std::sin(t * 2.6));
	cairo_save(cr);
	roundedPath(cr, rectF(0, 0, kW, kH), kRadius);
	cairo_clip(cr);

	// The icon's ground: near-black green.
	{
		cairo_pattern_t* g = cairo_pattern_create_linear(0, 0, 0, kH);
		stop(g, 0, kGroundTop);
		stop(g, 1, kGroundBottom);
		cairo_set_source(cr, g);
		cairo_paint(cr);
		cairo_pattern_destroy(g);
	}
	// Its grid, faintly, strongest in the middle.
	{
		cairo_push_group(cr);
		cairo_set_line_width(cr, 0.5);
		setColor(cr, alpha(kNeon, (float)(0.07 * span(t, 0.1, 0.9))));
		const float step = 28;
		for (float x = std::fmod(kW, step) / 2; x < kW; x += step) { cairo_move_to(cr, x, 0); cairo_line_to(cr, x, kH); }
		for (float y = std::fmod(kH, step) / 2; y < kH; y += step) { cairo_move_to(cr, 0, y); cairo_line_to(cr, kW, y); }
		cairo_stroke(cr);
		cairo_pop_group_to_source(cr);
		cairo_pattern_t* mask = cairo_pattern_create_radial(kW / 2, kH / 2, 0, kW / 2, kH / 2, 320);
		cairo_pattern_add_color_stop_rgba(mask, 0, 0, 0, 0, 1);
		cairo_pattern_add_color_stop_rgba(mask, 40.0 / 320, 0, 0, 0, 1);
		cairo_pattern_add_color_stop_rgba(mask, 1, 0, 0, 0, 0);
		cairo_mask(cr, mask);
		cairo_pattern_destroy(mask);
	}
	// A green bloom behind the icon.
	{
		cairo_pattern_t* b = cairo_pattern_create_radial(kW * 0.5, kH * 0.36, 0, kW * 0.5, kH * 0.36, 210);
		stop(b, 0, alpha(kNeon, (float)(0.30 * glow)));
		stop(b, 4.0 / 210, alpha(kNeon, (float)(0.30 * glow)));
		stop(b, 1, alpha(kNeon, 0));
		cairo_set_source(cr, b);
		cairo_paint(cr);
		cairo_pattern_destroy(b);
	}
	// The traces from the icon, drawn in, each ending in a dot.
	{
		const double progress = calm ? 1 : span(t, 0.35, 1.15);
		const float cy = 44 + 64, left = kW / 2 - 64 - 6, right = kW / 2 + 64 + 6;
		const std::vector<std::vector<PointF>> lines = {
			{ { 26, cy - 12 }, { left - 40, cy - 12 }, { left - 22, cy - 4 }, { left, cy - 4 } },
			{ { 26, cy + 12 }, { left - 40, cy + 12 }, { left - 22, cy + 4 }, { left, cy + 4 } },
			{ { right, cy + 14 }, { kW - 26, cy + 14 } },
		};
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
		cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
		for (size_t i = 0; i < lines.size(); i++) {
			if (progress > 0.001) {
				polyline(cr, lines[i], progress);
				setColor(cr, alpha(kNeon, (float)(0.18 * glow)));
				cairo_set_line_width(cr, 7);
				cairo_stroke(cr);
				polyline(cr, lines[i], progress);
				setColor(cr, alpha(kNeon, 0.85f));
				cairo_set_line_width(cr, 1.6);
				cairo_stroke(cr);
			}
			const PointF end = i < 2 ? lines[i].front() : lines[i].back();
			if (progress > 0.96) {
				fillCircle(cr, end, 8, alpha(kNeon, (float)(0.18 * glow)));
				fillCircle(cr, end, 4, kNeon);
			}
		}
	}
	// The icon: rises, grows and sharpens, a green glow under it.
	{
		const float s = (float)(0.9 + 0.1 * rise), dy = calm ? 0 : (float)(26 * (1 - rise));
		cairo_save(cr);
		cairo_translate(cr, kW / 2, 44 + dy + 64);
		cairo_scale(cr, s, s);
		cairo_translate(cr, -64, -64);
		// The blur, as copies spread out and faint while it sharpens.
		const float blur = calm ? 0 : (float)(16 * (1 - rise));
		cairo_push_group(cr);
		if (blur > 0.5f) {
			for (int i = 0; i < 6; i++) {
				const double a = i * G_PI / 3;
				cairo_save(cr);
				cairo_translate(cr, blur * 0.5 * std::cos(a), blur * 0.5 * std::sin(a));
				brand::icon(cr, 0, 0, 128, 0);
				cairo_restore(cr);
			}
			cairo_pop_group_to_source(cr);
			cairo_paint_with_alpha(cr, rise / 6 * 1.4);
			cairo_push_group(cr);
		}
		brand::icon(cr, 0, 0, 128, (float)(0.9 * glow));
		cairo_pop_group_to_source(cr);
		cairo_paint_with_alpha(cr, blur > 0.5f ? rise * rise : rise);
		cairo_restore(cr);
	}
	// "CedarLogic", a letter at a time, in brushed silver with a green glow.
	{
		std::vector<float> widths;
		float titleW = 0;
		const char* kTitle = tl.title.c_str();
		for (const char* c = kTitle; *c; c++) {
			widths.push_back(brand::textWidth(std::string(1, *c), kTitleSize, brand::SemiBold));
			titleW += widths.back() + kLetterGap;
		}
		titleW -= kLetterGap;
		float x = kW / 2 - titleW / 2;
		const float top = 44 + 128 + 20;
		for (size_t i = 0; kTitle[i]; i++) {
			const double p = calm ? span(t, 0.2, 0.5) : span(t, 0.55 + i * 0.045, 0.9 + i * 0.045);
			const float dy = calm ? 0 : (float)(9 * (1 - p));
			if (p > 0.002) {
				const std::string ch(1, kTitle[i]);
				// The glow under it.
				for (int k = 3; k >= 1; k--)
					brand::text(cr, ch, x - k * 0.6f, top + dy, kTitleSize, brand::SemiBold, alpha(kNeon, (float)(0.08 * p)));
				cairo_push_group(cr);
				brand::text(cr, ch, x, top + dy, kTitleSize, brand::SemiBold, colorF(1, 1, 1));
				cairo_pattern_t* mask = cairo_pop_group(cr);
				cairo_pattern_t* silver = cairo_pattern_create_linear(0, top + dy + 6, 0, top + dy + kTitleSize * 1.15);
				cairo_pattern_add_color_stop_rgba(silver, 0, 0.97, 0.97, 0.97, p);
				cairo_pattern_add_color_stop_rgba(silver, 0.5, 0.74, 0.74, 0.74, p);
				cairo_pattern_add_color_stop_rgba(silver, 1, 0.9, 0.9, 0.9, p);
				cairo_set_source(cr, silver);
				cairo_mask(cr, mask);
				cairo_pattern_destroy(silver);
				cairo_pattern_destroy(mask);
			}
			x += widths[i] + kLetterGap;
		}
	}
	// What it is, and where.
	{
		const float a = (float)(0.75 * span(t, 1.05, 1.45));
		const std::string what = "LOGIC SIMULATOR  ·  FOR LINUX";
		const float ww = brand::textWidth(what, 10, brand::SemiBold, 2.4f);
		brand::text(cr, what, kW / 2 - ww / 2, 44 + 128 + 20 + 50 + 8, 10, brand::SemiBold, alpha(kDim, a), 0, TextAlign::Leading, 2.4f);
		std::string nowText, before;
		double mix = 1;
		tl.status(t, nowText, before, mix);
		const float statusTop = kH - 24 - 3 - 14 - 16;
		const float shown = (float)span(t, 0.5, 0.9);
		auto line = [&](const std::string& s, float la) {
			if (s.empty() || la <= 0.01f) return;
			const float lw = brand::textWidth(s, 11.5f, brand::Medium);
			brand::text(cr, s, kW / 2 - lw / 2, statusTop, 11.5f, brand::Medium, alpha(kDim, la * shown * 0.75f));
		};
		line(before, (float)(1 - mix));
		line(nowText, (float)mix);
	}
	// The thin neon bar along the bottom, with a bright head.
	{
		const double fill = tl.progress(t);
		const float bx = 30, bw = kW - 60, by = kH - 24 - 3, bh = 3;
		fillRound(cr, rectF(bx, by, bx + bw, by + bh), bh / 2, colorF(1, 1, 1, 0.08f));
		const float w = (float)(bw * fill);
		if (w > 0.5f) {
			fillRound(cr, rectF(bx - 3, by - 3, bx + w + 3, by + bh + 3), 4.5f, alpha(kNeon, 0.16f));
			cairo_pattern_t* g = cairo_pattern_create_linear(bx, 0, bx + std::max(w, 1.0f), 0);
			stop(g, 0, kNeonDeep);
			stop(g, 1, kNeon);
			roundedPath(cr, rectF(bx, by, bx + w, by + bh), bh / 2);
			cairo_set_source(cr, g);
			cairo_fill(cr);
			cairo_pattern_destroy(g);
			if (fill > 0.01 && fill < 0.999) {
				fillCircle(cr, pointF(bx + w, by + bh / 2), 6, alpha(kNeon, 0.25f));
				fillCircle(cr, pointF(bx + w, by + bh / 2), 2.5f, colorF(1, 1, 1));
			}
		}
	}
	// A soft band of light passing across the glass.
	if (!calm) {
		const double phase = span(t, 0.85, 1.75);
		if (phase > 0 && phase < 1) {
			const float bw = 150, bh = kH * 1.6f;
			const float x = (float)(-200 + (kW + 400) * phase), y = -kH * 0.3f;
			cairo_save(cr);
			cairo_translate(cr, x + bw / 2, y + bh / 2);
			cairo_rotate(cr, 18 * G_PI / 180);
			cairo_translate(cr, -(x + bw / 2), -(y + bh / 2));
			cairo_pattern_t* g = cairo_pattern_create_linear(x, 0, x + bw, 0);
			cairo_pattern_add_color_stop_rgba(g, 0, 1, 1, 1, 0);
			cairo_pattern_add_color_stop_rgba(g, 0.5, 1, 1, 1, 0.13);
			cairo_pattern_add_color_stop_rgba(g, 1, 1, 1, 1, 0);
			cairo_rectangle(cr, x, y, bw, bh);
			cairo_set_source(cr, g);
			cairo_fill(cr);
			cairo_pattern_destroy(g);
			cairo_restore(cr);
		}
	}
	cairo_restore(cr);
	// The glass's edge: brighter at the top.
	{
		cairo_pattern_t* g = cairo_pattern_create_linear(0, 0, 0, kH);
		cairo_pattern_add_color_stop_rgba(g, 0, 1, 1, 1, 0.22);
		stop(g, 0.5, alpha(kNeon, 0.12f));
		cairo_pattern_add_color_stop_rgba(g, 1, 1, 1, 1, 0.05);
		roundedPath(cr, rectF(0.5f, 0.5f, kW - 0.5f, kH - 0.5f), kRadius - 0.5f);
		cairo_set_source(cr, g);
		cairo_set_line_width(cr, 1);
		cairo_stroke(cr);
		cairo_pattern_destroy(g);
	}
}

// Everything, onto a clear surface (kW + 2 margins wide): the shadow, then
// the panel, coming in over `appear` and dissolving at the end.
void drawAll(cairo_t* cr, const Timeline& tl, double t, double appear, bool shadow) {
	const double out = span(t, tl.dissolveAt, tl.dissolveAt + tl.fadeTime);
	const double opacity = appear * (1 - out);
	if (opacity <= 0.002) return;
	const float s = tl.calm ? 1 : (float)(1 + 0.035 * out);
	cairo_save(cr);
	cairo_translate(cr, kMargin + kW / 2, kMargin + kH / 2);
	cairo_scale(cr, s, s);
	cairo_translate(cr, -kW / 2, -kH / 2);
	cairo_push_group(cr);
	if (shadow) brand::glow(cr, rectF(0, 12, kW, kH + 12), kRadius, colorF(0, 0, 0, 0.42f), 26);
	drawPanel(cr, tl, t);
	cairo_pop_group_to_source(cr);
	cairo_paint_with_alpha(cr, opacity);
	cairo_restore(cr);
}

// ---- The window ----------------------------------------------------------------

struct Splash {
	GtkWidget* win = nullptr;
	GtkWidget* area = nullptr;
	Timeline tl;
	double start = -1;            // when it began playing (-1: not yet)
	double shownAt = 0;           // when it went up (clear), before the launch's work
	bool presented = false;       // the windows it held are in
	bool composited = false;
	GSourceFunc then = nullptr;
	gpointer thenData = nullptr;
	std::string opening;
	guint tick = 0;

	// The intro (Intro.h), when it's owed: `scene` while it plays in this
	// window (else, with the desktop's animations off, just the still panel).
	intro::Trigger intro = intro::Trigger::None;
	intro::Scene* scene = nullptr;
	intro::WireLook look;
	float sceneW = 0, sceneH = 0;
	bool maximized = false;       // the circuit window comes in maximized
	double freeze = 1e9;
	// Skipped, or given way to the still panel: when it dissolves, and how long.
	double cutAt = -1, cutFade = 0;
	bool still = false;           // gave way: the frames came too slowly
	double stillAt = 0;
	double lastTick = -1;
	int frames = 0, slowDraws = 0, slowGaps = 0;
	// The sound, started on the picture's clock once its first frame is up.
	bool soundDue = false;
	int drawn = 0;                // frames drawn since it began
	// Help > Play the Intro: over this window (a weak pointer), which it
	// dissolves back into; nothing is held, nothing marked played.
	bool replay = false;
	GtkWidget* over = nullptr;

	double elapsed() const { return start < 0 ? 0 : std::min(now() - start, freeze); }
	// When it dissolves into the windows it held, and over how long.
	double dissolveAt() const { return cutAt >= 0 ? cutAt : scene ? intro::kDissolveAt : tl.dissolveAt; }
	double fadeTime() const {
		if (cutAt >= 0) return cutFade;
		if (scene) return composited ? intro::kFade : 0;   // (where nothing's see-through: at once)
		return tl.fadeTime;
	}
};

Splash* g_splash = nullptr;

void libraryCounts(int& gates, int& families) {
	gates = 0;
	families = cl_library_category_count();
	for (int c = 0; c < families; c++) gates += cl_library_gate_count(c);
}

void destroy(Splash* s) {
	if (s->tick && s->area) gtk_widget_remove_tick_callback(s->area, s->tick);
	s->tick = 0;
	if (s->win) gtk_widget_destroy(s->win);
	if (g_splash == s) g_splash = nullptr;
	// Played from the menu: the window it was over has the keys again.
	if (s->over) {
		g_object_remove_weak_pointer(G_OBJECT(s->over), (gpointer*)&s->over);
		if (gtk_widget_get_visible(s->over)) gtk_window_present(GTK_WINDOW(s->over));
	}
	delete s->scene;
	delete s;
}

// The intro's picture at t (the window's, and --intro-bench's): the intro,
// then the panel over it, part-way into its appearance, dissolving at
// `dissolveAt` over `fade`.
void paintIntroFrame(cairo_t* cr, intro::Scene* scene, float w, float h, double t, double dissolveAt, double fade, bool composited) {
	const double shown = t < dissolveAt ? 0 : fade > 0 ? intro::easeInOut((t - dissolveAt) / fade) : 1;
	cairo_save(cr);
	if (composited) {
		roundedPath(cr, rectF(0, 0, w, h), 10);
		cairo_clip(cr);
	}
	const PointF card = intro::cardOrigin(w, h);
	// Fading in as a whole from nothing (where the desktop composites).
	scene->draw(cr, t, (composited ? intro::fadeInShown(t) : 1) * (1 - shown));
	if (t >= intro::kCardIn) {
		intro::Timed timed(intro::kPanel);
		drawPanelAfterIntro(cr, card.x, card.y, t - intro::kCardIn + intro::kSplashAt,
		                    intro::easeInOut((t - intro::kCardIn) / intro::kCardFade) * (1 - shown), false);
	}
	cairo_restore(cr);
}

// The intro's window, at t: the intro, then the panel over it, part-way into
// its appearance; or the still panel over the canvas, when it gave way.
void drawIntro(Splash* s, cairo_t* cr, float w, float h) {
	if (s->start < 0) {
		if (!s->composited) fillRect(cr, rectF(0, 0, w, h), colorF(0.075f, 0.082f, 0.098f));
		return;
	}
	if (s->sceneW != w || s->sceneH != h) {
		// The desktop gave the window another size: laid out again for it.
		delete s->scene;
		s->scene = new intro::Scene(w, h, s->look, true);
		s->sceneW = w;
		s->sceneH = h;
	}
	const double t = s->elapsed();
	const double fade = s->fadeTime();
	if (!s->still) {
		paintIntroFrame(cr, s->scene, w, h, t, s->dissolveAt(), fade, s->composited);
		return;
	}
	const double shown = t < s->dissolveAt() ? 0 : fade > 0 ? intro::easeInOut((t - s->dissolveAt()) / fade) : 1;
	cairo_save(cr);
	if (s->composited) {
		roundedPath(cr, rectF(0, 0, w, h), 10);
		cairo_clip(cr);
	}
	const PointF card = intro::cardOrigin(w, h);
	s->scene->drawGround(cr, 1 - shown);
	drawPanelAfterIntro(cr, card.x, card.y, t - s->stillAt, 1 - shown, true);
	cairo_restore(cr);
}

gboolean drawCb(GtkWidget* w, cairo_t* cr, gpointer data) {
	Splash* s = static_cast<Splash*>(data);
	guarded("the launch screen", [&] {
		cairo_save(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_set_source_rgba(cr, 0, 0, 0, 0);
		cairo_paint(cr);
		cairo_restore(cr);
		const float ww = gtk_widget_get_allocated_width(w), wh = gtk_widget_get_allocated_height(w);
		if (s->scene) {
			const gint64 began = g_get_monotonic_time();
			drawIntro(s, cr, ww, wh);
			if (s->start >= 0) s->drawn++;
			// A frame that takes too long, three in a row, and it gives way
			// (tickCb): a Raspberry Pi shouldn't play it as a slideshow.
			if (s->start >= 0 && !s->still && s->cutAt < 0 && ++s->frames > 3) {
				const double ms = (g_get_monotonic_time() - began) / 1000.0;
				s->slowDraws = ms > 45 ? s->slowDraws + 1 : 0;
			}
			return;
		}
		const double t = s->elapsed();
		const double appear = s->start < 0 ? 0 : std::min(1.0, t / 0.2);
		if (s->composited) {
			drawAll(cr, s->tl, t, appear, true);
		} else {
			// No transparency here: the panel fills the window.
			fillRect(cr, rectF(0, 0, ww, wh), kGroundBottom);
			cairo_save(cr);
			cairo_translate(cr, -kMargin + (ww - kW) / 2, -kMargin + (wh - kH) / 2);
			drawAll(cr, s->tl, t, std::max(appear, 0.001), false);
			cairo_restore(cr);
		}
	});
	return TRUE;
}

// The intro gives way to the still panel (as with the animations off): held
// a moment, then the crossfade.
void giveWay(Splash* s, double t, const char* why) {
	s->still = true;
	s->stillAt = t;
	s->cutAt = t + intro::kCalmHold;
	s->cutFade = s->composited ? intro::kCalmFade : 0;
	g_printerr("CedarLogic: the intro gave way to the still launch screen (%s)\n", why);
}

// Once a frame, in step with the screen (the frame clock), not on a timer
// of its own that drifts against it.
gboolean tickCb(GtkWidget*, GdkFrameClock*, gpointer data) {
	Splash* s = static_cast<Splash*>(data);
	gtk_widget_queue_draw(s->area);
	if (s->start < 0) return G_SOURCE_CONTINUE;
	const double t = s->elapsed();
	// The sound, once the picture's first frame is up: however long that
	// took, it starts that much further in, so the bell lands on the LED
	// (a little after the flash, never before it).
	if (s->soundDue && s->drawn > 0) {
		s->soundDue = false;
		if (s->cutAt < 0) intro::playSound(now() - s->start);
	}
	if (s->scene && !s->still && s->cutAt < 0) {
		// Frames far apart (the drawing, or the desktop, can't keep up):
		// twice, before the bloom, and it gives way.
		const double n = now();
		if (s->lastTick >= 0 && t > 0.3 && t < intro::kBloomAt && n - s->lastTick > 0.15) s->slowGaps++;
		s->lastTick = n;
		if (s->slowGaps >= 2) giveWay(s, t, "frames far apart");
		else if (s->slowDraws >= 3 && t < intro::kBloomAt) giveWay(s, t, "frames slow to draw");
	}
	// The windows come in as it starts to dissolve, under it.
	if (!s->presented && t >= s->dissolveAt()) {
		s->presented = true;
		GSourceFunc f = s->then;
		s->then = nullptr;
		if (f) guarded("opening the windows", [&] { f(s->thenData); });
		if (g_splash == s && s->win) {
			// Over them as it goes, but the keys are theirs now.
			if (s->scene) gtk_window_set_accept_focus(GTK_WINDOW(s->win), FALSE);
			gtk_window_present(GTK_WINDOW(s->win));
		}
	}
	if (t >= s->dissolveAt() + s->fadeTime() + (s->fadeTime() > 0 ? 0.05 : 0)) {
		s->tick = 0;
		// Gone after this frame (not from inside its own frame callback).
		g_idle_add([](gpointer p) -> gboolean {
			if (g_splash == p) destroy(static_cast<Splash*>(p));
			return G_SOURCE_REMOVE;
		}, s);
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

// Any key or click while the intro plays: it goes, quickly.
gboolean skipCb(GtkWidget*, GdkEvent*, gpointer data) {
	Splash* s = static_cast<Splash*>(data);
	if (!s->scene || s->start < 0 || s->presented) return TRUE;
	const double t = s->elapsed();
	if (t >= s->dissolveAt()) return TRUE;
	s->cutAt = t;
	s->cutFade = s->composited ? intro::kSkipFade : 0;
	s->soundDue = false;
	intro::stopSound();
	return TRUE;
}

bool isX11() {
	GdkDisplay* d = gdk_display_get_default();
	return d && strcmp(G_OBJECT_TYPE_NAME(d), "GdkX11Display") == 0;
}

// The monitor the pointer is on (where the windows will come in).
GdkMonitor* pointerMonitor() {
	GdkDisplay* d = gdk_display_get_default();
	if (!d) return nullptr;
	int x = 0, y = 0;
	if (GdkSeat* seat = gdk_display_get_default_seat(d))
		if (GdkDevice* p = gdk_seat_get_pointer(seat)) gdk_device_get_position(p, nullptr, &x, &y);
	GdkMonitor* m = gdk_display_get_monitor_at_point(d, x, y);
	if (!m) m = gdk_display_get_primary_monitor(d);
	if (!m && gdk_display_get_n_monitors(d) > 0) m = gdk_display_get_monitor(d, 0);
	return m;
}

// The window, unseen until it plays: the launch screen (`trigger` None), or
// the intro in a window where the circuit window will come in, its size --
// or, played from the menu, over `over`, its size.
Splash* makeSplash(intro::Trigger trigger, GtkWindow* over) {
	Splash* s = new Splash();
	s->tl.first = !over && !prefs().hasSeenWelcome && !prefs().firstLaunchPlayed;
	s->tl.calm = reduceMotion();
	s->intro = trigger;
	s->replay = over != nullptr;
	if (s->intro != intro::Trigger::None) {
		s->tl.planAfterIntro(s->tl.calm);
		// The first launch's sound and an update's, cut to land on the LED,
		// ready by the time it plays; every other launch's is silent.
		if (intro::hasSound(s->intro) && prefs().playLaunchSound) intro::prepareSound(s->tl.calm);
		intro::log(std::string(intro::name(s->intro)) + (over ? ", from the menu" : "") +
		           (intro::hasSound(s->intro) ? prefs().playLaunchSound ? ", with the sound" : ", the sound turned off" : ", silent") +
		           (s->tl.calm ? ", the still panel (animations off)" : ""));
	}
	const bool introWindow = s->intro != intro::Trigger::None && !s->tl.calm;
	s->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_decorated(GTK_WINDOW(s->win), FALSE);
	gtk_window_set_resizable(GTK_WINDOW(s->win), FALSE);
	gtk_window_set_skip_taskbar_hint(GTK_WINDOW(s->win), TRUE);
	gtk_window_set_type_hint(GTK_WINDOW(s->win), GDK_WINDOW_TYPE_HINT_SPLASHSCREEN);
	gtk_window_set_position(GTK_WINDOW(s->win), over ? GTK_WIN_POS_CENTER_ON_PARENT : GTK_WIN_POS_CENTER);
	if (over) gtk_window_set_transient_for(GTK_WINDOW(s->win), over);
	gtk_window_set_keep_above(GTK_WINDOW(s->win), TRUE);
	// The intro takes the keys that skip it; the launch screen can't be skipped.
	gtk_window_set_accept_focus(GTK_WINDOW(s->win), introWindow);
	gtk_window_set_title(GTK_WINDOW(s->win), "CedarLogic");
	GdkScreen* screen = gtk_widget_get_screen(s->win);
	GdkVisual* rgba = gdk_screen_get_rgba_visual(screen);
	s->composited = rgba && gdk_screen_is_composited(screen);
	if (s->composited) {
		gtk_widget_set_visual(s->win, rgba);
		gtk_widget_set_app_paintable(s->win, TRUE);
	}
	if (introWindow) {
		// Where the circuit window will come in, its size (Window.cpp's), on
		// the screen with the pointer on it; or over the window it plays over.
		int w = prefs().windowWidth, h = prefs().windowHeight;
		s->maximized = over ? gtk_window_is_maximized(over) : prefs().windowMaximized;
		GdkRectangle area = { 0, 0, 1280, 800 };
		GdkMonitor* m = nullptr;
		if (over && gtk_widget_get_window(GTK_WIDGET(over)))
			m = gdk_display_get_monitor_at_window(gdk_display_get_default(), gtk_widget_get_window(GTK_WIDGET(over)));
		if (!m) m = pointerMonitor();
		if (m) gdk_monitor_get_workarea(m, &area);
		if (over) gtk_window_get_size(over, &w, &h);
		if (s->maximized) { w = area.width; h = area.height; }
		w = std::max(480, std::min(w, area.width));
		h = std::max(320, std::min(h, area.height));
		gtk_window_set_default_size(GTK_WINDOW(s->win), w, h);
		if (s->maximized) {
			gtk_window_set_position(GTK_WINDOW(s->win), GTK_WIN_POS_NONE);
			gtk_window_move(GTK_WINDOW(s->win), area.x, area.y);
		} else if (over && isX11()) {
			int x = 0, y = 0;
			gtk_window_get_position(over, &x, &y);
			gtk_window_set_position(GTK_WINDOW(s->win), GTK_WIN_POS_NONE);
			gtk_window_move(GTK_WINDOW(s->win), x, y);
		}
		s->look = intro::WireLook::current();
		s->scene = new intro::Scene((float)w, (float)h, s->look, true);
		s->sceneW = (float)w;
		s->sceneH = (float)h;
		s->freeze = intro::freezeAt();
	} else if (s->composited) {
		gtk_window_set_default_size(GTK_WINDOW(s->win), (int)(kW + 2 * kMargin), (int)(kH + 2 * kMargin));
	} else {
		gtk_window_set_default_size(GTK_WINDOW(s->win), (int)kW, (int)kH);
	}
	// Before GTK draws it the window is its CSS background (the desktop
	// theme's, light grey or white): a pale box flashed up where the panel
	// was about to be. Clear where it composites, the panel's ground (or the
	// intro's canvas) where it doesn't.
	{
		GtkCssProvider* css = gtk_css_provider_new();
		const Color under = introWindow ? colorF(0.075f, 0.082f, 0.098f) : kGroundBottom;
		const std::string ground = s->composited ? "transparent"
		                                         : format("rgb(%d,%d,%d)", (int)(under.r * 255), (int)(under.g * 255), (int)(under.b * 255));
		gtk_css_provider_load_from_data(css, ("window { background: " + ground + "; box-shadow: none; border: none; }").c_str(), -1, nullptr);
		gtk_style_context_add_provider(gtk_widget_get_style_context(s->win), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
		g_object_unref(css);
	}
	s->area = gtk_drawing_area_new();
	gtk_container_add(GTK_CONTAINER(s->win), s->area);
	g_signal_connect(s->area, "draw", G_CALLBACK(drawCb), s);
	if (introWindow) {
		gtk_widget_add_events(s->area, GDK_BUTTON_PRESS_MASK);
		g_signal_connect(s->area, "button-press-event", G_CALLBACK(skipCb), s);
		g_signal_connect(s->win, "key-press-event", G_CALLBACK(skipCb), s);
	}
	if (over) {
		s->over = GTK_WIDGET(over);
		g_object_add_weak_pointer(G_OBJECT(s->over), (gpointer*)&s->over);
	}
	g_splash = s;
	gtk_widget_show_all(s->win);
	s->tick = gtk_widget_add_tick_callback(s->area, tickCb, s, nullptr);
	s->shownAt = now();
	return s;
}

// It plays: the intro (its sound once its first frame is up) or, with the
// animations off, the still panel and the sound from just before its bell.
void beginIntro(Splash* s) {
	s->start = now();
	if (intro::hasSound(s->intro) && prefs().playLaunchSound) {
		if (s->scene) s->soundDue = true;
		else intro::playSound();
	}
	// To the front, for the keys that skip it.
	if (s->scene) gtk_window_present(GTK_WINDOW(s->win));
}

}  // namespace

GtkWidget* showSplash() {
	if (g_splash) return g_splash->win;
	Splash* s = makeSplash(intro::pending(), nullptr);
	// Shown (clear) at once, before the slow work starts on this thread.
	while (gtk_events_pending()) gtk_main_iteration_do(FALSE);
	return s->win;
}

bool splashActive() { return g_splash != nullptr && !g_splash->replay; }
bool splashHoldsWindows() { return g_splash != nullptr && !g_splash->replay && !g_splash->presented; }
bool splashShowing() { return g_splash != nullptr; }

void splashPlayIntroNow(GtkWindow* over) {
	if (g_splash || !over || !gtk_widget_get_visible(GTK_WIDGET(over))) return;
	// The first launch's, sound and all; nothing is hidden or held, and the
	// once-only keys are left as they are.
	Splash* s = makeSplash(intro::Trigger::First, over);
	beginIntro(s);
}

// The work happens before it plays (it lists what was done, a line at a
// time, once it begins), so what's said meanwhile isn't shown.
void splashSetStatus(GtkWidget*, const char*) {}

void splashSetOpening(const std::string& name) {
	if (g_splash) g_splash->opening = name;
}

void splashPlaceUnder(GtkWindow* w) {
	Splash* s = g_splash;
	// Only X11 lets an app say where its windows go; elsewhere the desktop
	// places both (centred, as a rule).
	if (s == nullptr || s->replay || s->scene == nullptr || w == nullptr || s->maximized || !s->win || !isX11()) return;
	int x = 0, y = 0;
	gtk_window_get_position(GTK_WINDOW(s->win), &x, &y);
	gtk_window_move(w, x, y);
}

void hideSplashSoon(GtkWidget* splash, GSourceFunc onHidden, gpointer data) {
	Splash* s = g_splash;
	if (splash == nullptr || s == nullptr || s->win != splash || s->replay) {
		if (onHidden) onHidden(data);
		return;
	}
	s->then = onHidden;
	s->thenData = data;
	if (s->intro != intro::Trigger::None) {
		// Played once each: kept now, so a launch that stops part-way doesn't play it again.
		intro::markPlayed(s->intro);
		if (s->intro == intro::Trigger::First) prefs().firstLaunchPlayed = true;
		prefs().save();
		beginIntro(s);
		return;
	}
	s->start = now();
	int gates = 0, families = 0;
	libraryCounts(gates, families);
	s->tl.plan(gates, families, s->opening, s->start - s->shownAt);
	if (s->tl.first) {
		// The first launch's sound: once ever.
		prefs().firstLaunchPlayed = true;
		prefs().save();
		if (prefs().playLaunchSound) brand::playChime();
	}
}

void drawPanelAfterIntro(cairo_t* cr, float x, float y, double t, double opacity, bool calm) {
	static Timeline played, still;
	if (!played.afterIntro) {
		played.planAfterIntro(false);
		still.planAfterIntro(true);
	}
	cairo_save(cr);
	cairo_translate(cr, x - kMargin, y - kMargin);
	drawAll(cr, calm ? still : played, t, opacity, true);
	cairo_restore(cr);
}

bool renderSplashFrame(double t, bool first, const std::string& file) {
	Timeline tl;
	tl.first = first;
	int gates = 0, families = 0;
	libraryCounts(gates, families);
	tl.plan(gates, families, "practice");
	const int scale = 2;
	cairo_surface_t* surface =
		cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)((kW + 2 * kMargin) * scale), (int)((kH + 2 * kMargin) * scale));
	cairo_t* cr = cairo_create(surface);
	cairo_scale(cr, scale, scale);
	drawAll(cr, tl, t, 1, true);
	cairo_destroy(cr);
	const bool ok = cairo_surface_write_to_png(surface, file.c_str()) == CAIRO_STATUS_SUCCESS;
	cairo_surface_destroy(surface);
	return ok;
}

// ---- --intro-bench -------------------------------------------------------------

namespace {

double msSince(std::chrono::steady_clock::time_point a) {
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
}

// One run of the intro, every frame at 60 fps, drawn as the window draws it
// (cleared, then the frame); each frame's total and its parts.
struct Run {
	std::vector<double> total;
	std::vector<std::array<double, intro::kParts>> parts;
};

Run benchRun(cairo_surface_t* surface, intro::Scene* scene, int w, int h) {
	Run r;
	const int frames = (int)std::lround(intro::kTotal * 60) + 1;
	for (int f = 0; f < frames; f++) {
		const double t = f / 60.0;
		intro::Prof prof;
		intro::gProf = &prof;
		const auto began = std::chrono::steady_clock::now();
		prof.mark = began;
		cairo_t* cr = cairo_create(surface);
		{
			intro::Timed timed(intro::kClear);
			cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
			cairo_set_source_rgba(cr, 0, 0, 0, 0);
			cairo_paint(cr);
			cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
		}
		paintIntroFrame(cr, scene, (float)w, (float)h, t, intro::kDissolveAt, intro::kFade, true);
		cairo_destroy(cr);
		cairo_surface_flush(surface);
		prof.flush();
		intro::gProf = nullptr;
		r.total.push_back(msSince(began));
		std::array<double, intro::kParts> p;
		for (int i = 0; i < intro::kParts; i++) p[i] = prof.ms[i];
		r.parts.push_back(p);
	}
	return r;
}

double meanOf(const std::vector<double>& v, size_t from = 0, size_t to = (size_t)-1) {
	to = std::min(to, v.size());
	if (from >= to) return 0;
	double s = 0;
	for (size_t i = from; i < to; i++) s += v[i];
	return s / (to - from);
}

double quantile(std::vector<double> v, double q) {
	if (v.empty()) return 0;
	std::sort(v.begin(), v.end());
	return v[std::min(v.size() - 1, (size_t)std::max(0.0, std::ceil(v.size() * q) - 1))];
}

}  // namespace

int introBench(const std::vector<std::string>& args) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	std::vector<std::pair<int, int>> sizes;
	for (const std::string& a : args) {
		int w = 0, h = 0;
		if (sscanf(a.c_str(), "%dx%d", &w, &h) == 2 && w >= 480 && h >= 320 && w <= 8192 && h <= 8192) sizes.push_back({ w, h });
	}
	if (sizes.empty()) sizes = { { 1280, 800 }, { 1920, 1080 } };
	const int frames = (int)std::lround(intro::kTotal * 60) + 1;
	printf("intro-bench: %d frames at 60 fps (%.2f s), at scale 1, drawn as the intro's window draws them where the desktop composites\n",
	       frames, intro::kTotal);
	for (const auto& size : sizes) {
		const int w = size.first, h = size.second;
		cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
		const auto built = std::chrono::steady_clock::now();
		intro::Scene* scene = new intro::Scene((float)w, (float)h, intro::WireLook::current(), true);
		const double buildMs = msSince(built);
		// Once cold (the images it keeps made, the fonts loaded), then again: the numbers.
		const Run cold = benchRun(surface, scene, w, h);
		const Run run = benchRun(surface, scene, w, h);
		const size_t panelAt = (size_t)std::ceil(intro::kCardIn * 60);
		printf("\n%dx%d, full:\n", w, h);
		printf("  a frame: mean %.2f ms, median %.2f, p95 %.2f, worst %.2f (at %.2f s)\n", meanOf(run.total), quantile(run.total, 0.5),
		       quantile(run.total, 0.95), *std::max_element(run.total.begin(), run.total.end()),
		       (std::max_element(run.total.begin(), run.total.end()) - run.total.begin()) / 60.0);
		struct Phase { const char* name; double a, b; };
		const Phase phases[] = { { "fade in", 0, intro::kFadeIn }, { "circuit", intro::kFadeIn, intro::kBloomAt },
		                         { "bloom", intro::kBloomAt, intro::kCardIn + intro::kCardFade },
		                         { "panel", intro::kCardIn + intro::kCardFade, intro::kDissolveAt }, { "dissolve", intro::kDissolveAt, 99 } };
		printf("  by phase (mean ms a frame):");
		for (const Phase& p : phases)
			printf("  %s %.2f-%.2f s: %.2f", p.name, p.a, std::min(p.b, intro::kTotal),
			       meanOf(run.total, (size_t)std::ceil(p.a * 60), (size_t)std::ceil(p.b * 60)));
		printf("\n  by part (mean ms a frame, worst):");
		double partsSum = 0;
		for (int i = 0; i < intro::kParts; i++) {
			std::vector<double> v;
			for (const auto& p : run.parts) v.push_back(p[i]);
			partsSum += meanOf(v);
			printf("  %s %.2f (%.2f)", intro::partName(i), meanOf(v), *std::max_element(v.begin(), v.end()));
		}
		printf("  other %.2f\n", meanOf(run.total) - partsSum);
		printf("  cold: the scene laid out %.2f ms, its first frame %.2f ms, the panel's first %.2f ms; the whole first run %.0f ms\n", buildMs,
		       cold.total[0], panelAt < cold.total.size() ? cold.total[panelAt] : 0, meanOf(cold.total) * cold.total.size());
		printf("  at 60 fps: %d of %d frames over 16.7 ms, %d over 33.3 ms\n",
		       (int)std::count_if(run.total.begin(), run.total.end(), [](double x) { return x > 16.7; }), frames,
		       (int)std::count_if(run.total.begin(), run.total.end(), [](double x) { return x > 33.3; }));
		delete scene;
		cairo_surface_destroy(surface);
	}
	return 0;
}
