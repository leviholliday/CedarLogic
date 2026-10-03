// The launch screen (see Splash.h). Drawn with Cairo into a window with real
// transparency where the desktop composites (so the panel's corners are
// round and its shadow soft); on one that doesn't, the panel fills the
// window. Blurs are drawn as fades and soft glows.

#include "Splash.h"
#include "Brand.h"

#include <algorithm>
#include <cmath>
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
	double dissolveAt = 2.25, fadeTime = 0.42, readyAt = -1;
	struct Step { double at; std::string text; double progress; };
	std::vector<Step> steps;

	void plan(int gates, int families, const std::string& opening) {
		fadeTime = first ? 1.1 : 0.42;
		if (calm) dissolveAt = 1.1;
		const double spacing = calm ? 0.12 : first ? 0.72 : 0.32;
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
		dissolveAt = first ? std::max(4.9, std::min(7.0, readyAt + 0.3)) : std::min(6.0, std::max(calm ? 1.1 : 2.3, readyAt + 0.3));
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

const char* kTitle = "CedarLogic";
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
	bool presented = false;       // the windows it held are in
	bool composited = false;
	GSourceFunc then = nullptr;
	gpointer thenData = nullptr;
	std::string opening;
	guint tick = 0;
	double elapsed() const { return start < 0 ? 0 : now() - start; }
};

Splash* g_splash = nullptr;

void libraryCounts(int& gates, int& families) {
	gates = 0;
	families = cl_library_category_count();
	for (int c = 0; c < families; c++) gates += cl_library_gate_count(c);
}

void destroy(Splash* s) {
	if (s->tick) g_source_remove(s->tick);
	if (s->win) gtk_widget_destroy(s->win);
	if (g_splash == s) g_splash = nullptr;
	delete s;
}

gboolean drawCb(GtkWidget* w, cairo_t* cr, gpointer data) {
	Splash* s = static_cast<Splash*>(data);
	guarded("the launch screen", [&] {
		cairo_save(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_set_source_rgba(cr, 0, 0, 0, 0);
		cairo_paint(cr);
		cairo_restore(cr);
		const double t = s->elapsed();
		const double appear = s->start < 0 ? 0 : std::min(1.0, t / 0.2);
		if (s->composited) {
			drawAll(cr, s->tl, t, appear, true);
		} else {
			// No transparency here: the panel fills the window.
			const float ww = gtk_widget_get_allocated_width(w), wh = gtk_widget_get_allocated_height(w);
			fillRect(cr, rectF(0, 0, ww, wh), kGroundBottom);
			cairo_save(cr);
			cairo_translate(cr, -kMargin + (ww - kW) / 2, -kMargin + (wh - kH) / 2);
			drawAll(cr, s->tl, t, std::max(appear, 0.001), false);
			cairo_restore(cr);
		}
	});
	return TRUE;
}

gboolean tickCb(gpointer data) {
	Splash* s = static_cast<Splash*>(data);
	gtk_widget_queue_draw(s->area);
	if (s->start < 0) return G_SOURCE_CONTINUE;
	const double t = s->elapsed();
	// The windows come in as it starts to dissolve, under it.
	if (!s->presented && t >= s->tl.dissolveAt) {
		s->presented = true;
		GSourceFunc f = s->then;
		s->then = nullptr;
		if (f) guarded("opening the windows", [&] { f(s->thenData); });
		if (g_splash == s && s->win) gtk_window_present(GTK_WINDOW(s->win));
	}
	if (t >= s->tl.dissolveAt + s->tl.fadeTime + 0.05) {
		s->tick = 0;
		destroy(s);
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

}  // namespace

GtkWidget* showSplash() {
	if (g_splash) return g_splash->win;
	Splash* s = new Splash();
	s->tl.first = !prefs().hasSeenWelcome && !prefs().firstLaunchPlayed;
	s->tl.calm = reduceMotion();
	s->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_decorated(GTK_WINDOW(s->win), FALSE);
	gtk_window_set_resizable(GTK_WINDOW(s->win), FALSE);
	gtk_window_set_skip_taskbar_hint(GTK_WINDOW(s->win), TRUE);
	gtk_window_set_type_hint(GTK_WINDOW(s->win), GDK_WINDOW_TYPE_HINT_SPLASHSCREEN);
	gtk_window_set_position(GTK_WINDOW(s->win), GTK_WIN_POS_CENTER);
	gtk_window_set_keep_above(GTK_WINDOW(s->win), TRUE);
	gtk_window_set_accept_focus(GTK_WINDOW(s->win), FALSE);
	gtk_window_set_title(GTK_WINDOW(s->win), "CedarLogic");
	GdkScreen* screen = gtk_widget_get_screen(s->win);
	GdkVisual* rgba = gdk_screen_get_rgba_visual(screen);
	s->composited = rgba && gdk_screen_is_composited(screen);
	if (s->composited) {
		gtk_widget_set_visual(s->win, rgba);
		gtk_widget_set_app_paintable(s->win, TRUE);
		gtk_window_set_default_size(GTK_WINDOW(s->win), (int)(kW + 2 * kMargin), (int)(kH + 2 * kMargin));
	} else {
		gtk_window_set_default_size(GTK_WINDOW(s->win), (int)kW, (int)kH);
	}
	s->area = gtk_drawing_area_new();
	gtk_container_add(GTK_CONTAINER(s->win), s->area);
	g_signal_connect(s->area, "draw", G_CALLBACK(drawCb), s);
	g_splash = s;
	gtk_widget_show_all(s->win);
	s->tick = g_timeout_add(16, tickCb, s);
	// Shown (clear) at once, before the slow work starts on this thread.
	while (gtk_events_pending()) gtk_main_iteration_do(FALSE);
	return s->win;
}

bool splashActive() { return g_splash != nullptr; }

// The work happens before it plays (it lists what was done, a line at a
// time, once it begins), so what's said meanwhile isn't shown.
void splashSetStatus(GtkWidget*, const char*) {}

void splashSetOpening(const std::string& name) {
	if (g_splash) g_splash->opening = name;
}

void hideSplashSoon(GtkWidget* splash, GSourceFunc onHidden, gpointer data) {
	Splash* s = g_splash;
	if (splash == nullptr || s == nullptr || s->win != splash) {
		if (onHidden) onHidden(data);
		return;
	}
	s->then = onHidden;
	s->thenData = data;
	int gates = 0, families = 0;
	libraryCounts(gates, families);
	s->tl.plan(gates, families, s->opening);
	s->start = now();
	if (s->tl.first) {
		// The first launch's sound: once ever.
		prefs().firstLaunchPlayed = true;
		prefs().save();
		if (prefs().playLaunchSound) brand::playChime();
	}
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
