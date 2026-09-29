// The launch screen (see Splash.h): the icon's own colours -- near-black
// green, neon green, brushed silver -- the same brand the Mac app's launch
// screen uses (mac/App/Splash.swift), simplified to something Cairo draws
// happily without a compositor: no true window transparency, just a dark
// panel the whole window fills, with the glow and motion drawn on top of it.

#include "Splash.h"

#include <algorithm>
#include <cmath>

namespace {

const int kMinVisibleMs = 1100;   // long enough to actually see it animate
struct RGB { double r, g, b; };
const RGB kInk{ 0.035, 0.063, 0.047 }, kInkDeep{ 0.012, 0.024, 0.018 };
const RGB kNeon{ 0.22, 1.0, 0.42 }, kNeonDeep{ 0.05, 0.78, 0.26 };
const RGB kSilver{ 0.93, 0.95, 0.94 };

double now_ms() { return g_get_monotonic_time() / 1000.0; }

struct SplashState {
	double shownAt = 0;
	std::string status = "Starting up…";
	guint tick = 0;
	guint minTimer = 0;
};

// Ease-out cubic, the same curve the Mac splash's bar glides on.
double easeOut(double t) { return 1.0 - std::pow(1.0 - std::clamp(t, 0.0, 1.0), 3.0); }

void roundedRect(cairo_t* cr, double x, double y, double w, double h, double r) {
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -G_PI_2, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI_2);
	cairo_arc(cr, x + r, y + h - r, r, G_PI_2, G_PI);
	cairo_arc(cr, x + r, y + r, r, G_PI, 3 * G_PI_2);
	cairo_close_path(cr);
}

// A soft glow: the same shape stroked several times, wider and fainter each
// time, under a crisp final pass -- a cheap stand-in for a real blur.
void glowStroke(cairo_t* cr, double baseWidth, const RGB& c, double alpha) {
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	for (int pass = 3; pass >= 1; pass--) {
		cairo_set_line_width(cr, baseWidth + pass * 3.5);
		cairo_set_source_rgba(cr, c.r, c.g, c.b, alpha * 0.10);
		cairo_stroke_preserve(cr);
	}
	cairo_set_line_width(cr, baseWidth);
	cairo_set_source_rgba(cr, c.r, c.g, c.b, alpha);
	cairo_stroke(cr);
}

// The brand mark: a switch feeding an AND gate feeding a lit bulb, the same
// motif the welcome screen's hero uses, in the launch screen's neon green.
void drawMark(cairo_t* cr, double cx, double cy, double scale, double glow) {
	const double x0 = cx - 95 * scale, x1 = cx - 30 * scale, x3 = cx + 95 * scale;
	const double r = 22 * scale;

	// A soft halo behind the whole mark.
	cairo_pattern_t* halo = cairo_pattern_create_radial(cx, cy, 0, cx, cy, 130 * scale);
	cairo_pattern_add_color_stop_rgba(halo, 0, kNeon.r, kNeon.g, kNeon.b, 0.20 * glow);
	cairo_pattern_add_color_stop_rgba(halo, 1, kNeon.r, kNeon.g, kNeon.b, 0);
	cairo_set_source(cr, halo);
	cairo_arc(cr, cx, cy, 130 * scale, 0, 2 * G_PI);
	cairo_fill(cr);
	cairo_pattern_destroy(halo);

	// Switch.
	cairo_new_path(cr);
	cairo_rectangle(cr, x0 - 16 * scale, cy - 16 * scale, 32 * scale, 32 * scale);
	glowStroke(cr, 2.2 * scale, kNeon, 0.9);
	// Wire in.
	cairo_new_path(cr);
	cairo_move_to(cr, x0 + 16 * scale, cy);
	cairo_line_to(cr, x1 - 22 * scale, cy);
	glowStroke(cr, 2.2 * scale, kNeon, 0.9);
	// AND gate.
	cairo_new_path(cr);
	cairo_move_to(cr, x1 - 22 * scale, cy - 24 * scale);
	cairo_line_to(cr, x1, cy - 24 * scale);
	cairo_curve_to(cr, x1 + 38 * scale, cy - 24 * scale, x1 + 38 * scale, cy + 24 * scale, x1, cy + 24 * scale);
	cairo_line_to(cr, x1 - 22 * scale, cy + 24 * scale);
	cairo_close_path(cr);
	glowStroke(cr, 2.2 * scale, kNeon, 0.9);
	// Wire out.
	cairo_new_path(cr);
	cairo_move_to(cr, x1 + 38 * scale, cy);
	cairo_line_to(cr, x3 - r - 4 * scale, cy);
	glowStroke(cr, 2.2 * scale, kNeon, 0.9);
	// The lit bulb.
	cairo_new_path(cr);
	cairo_arc(cr, x3 - r, cy, r, 0, 2 * G_PI);
	cairo_set_source_rgba(cr, kNeon.r, kNeon.g, kNeon.b, 0.30 * glow);
	cairo_fill_preserve(cr);
	glowStroke(cr, 2.2 * scale, kNeon, 0.95);
}

// "CedarLogic" in brushed silver with a faint green glow behind it.
void drawTitle(cairo_t* cr, GtkWidget* w, double cx, double y) {
	cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, 30);
	cairo_text_extents_t ext;
	cairo_text_extents(cr, "CedarLogic", &ext);
	const double x = cx - ext.width / 2 - ext.x_bearing;
	cairo_set_source_rgba(cr, kNeon.r, kNeon.g, kNeon.b, 0.30);
	for (double d = 1.5; d <= 3.0; d += 1.5) {
		cairo_move_to(cr, x, y + d);
		cairo_show_text(cr, "CedarLogic");
		cairo_move_to(cr, x, y - d);
		cairo_show_text(cr, "CedarLogic");
	}
	cairo_set_source_rgb(cr, kSilver.r, kSilver.g, kSilver.b);
	cairo_move_to(cr, x, y);
	cairo_show_text(cr, "CedarLogic");
	(void)w;
}

gboolean onDraw(GtkWidget* w, cairo_t* cr, gpointer data) {
	SplashState* st = static_cast<SplashState*>(data);
	const double width = gtk_widget_get_allocated_width(w), height = gtk_widget_get_allocated_height(w);
	const double t = (now_ms() - st->shownAt) / kMinVisibleMs;
	const double p = easeOut(std::min(1.0, t));
	// A slow, gentle pulse in the glow, like the Mac launch screen's.
	const double glow = 0.85 + 0.15 * std::sin(t * 9.0);

	cairo_pattern_t* bg = cairo_pattern_create_linear(0, 0, 0, height);
	cairo_pattern_add_color_stop_rgb(bg, 0, kInk.r, kInk.g, kInk.b);
	cairo_pattern_add_color_stop_rgb(bg, 1, kInkDeep.r, kInkDeep.g, kInkDeep.b);
	cairo_set_source(cr, bg);
	cairo_paint(cr);
	cairo_pattern_destroy(bg);

	// A faint neon edge, like the Mac panel's border gradient.
	roundedRect(cr, 1, 1, width - 2, height - 2, 18);
	cairo_set_source_rgba(cr, kNeon.r, kNeon.g, kNeon.b, 0.20);
	cairo_set_line_width(cr, 1.4);
	cairo_stroke(cr);

	drawMark(cr, width / 2, height * 0.36, 0.62, glow);
	drawTitle(cr, w, width / 2, height * 0.62);

	// The status line.
	cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, 12);
	cairo_text_extents_t se;
	cairo_text_extents(cr, st->status.c_str(), &se);
	cairo_set_source_rgba(cr, 0.62, 0.74, 0.66, 0.85);
	cairo_move_to(cr, width / 2 - se.width / 2 - se.x_bearing, height * 0.72);
	cairo_show_text(cr, st->status.c_str());

	// The thin neon progress bar along the bottom, with a bright head, as on
	// the Mac launch screen.
	const double barX = 40, barW = width - 80, barY = height - 34, barH = 4;
	roundedRect(cr, barX, barY, barW, barH, barH / 2);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
	cairo_fill(cr);
	if (p > 0.01) {
		const double fillW = std::max(barH, barW * p);
		cairo_pattern_t* bar = cairo_pattern_create_linear(barX, 0, barX + fillW, 0);
		cairo_pattern_add_color_stop_rgb(bar, 0, kNeonDeep.r, kNeonDeep.g, kNeonDeep.b);
		cairo_pattern_add_color_stop_rgb(bar, 1, kNeon.r, kNeon.g, kNeon.b);
		roundedRect(cr, barX, barY, fillW, barH, barH / 2);
		cairo_set_source(cr, bar);
		cairo_fill(cr);
		cairo_pattern_destroy(bar);
		// The glowing head.
		cairo_set_source_rgba(cr, kNeon.r, kNeon.g, kNeon.b, 0.9 * glow);
		cairo_arc(cr, barX + fillW, barY + barH / 2, 3.2, 0, 2 * G_PI);
		cairo_fill(cr);
	}
	return TRUE;
}

}  // namespace

GtkWidget* showSplash() {
	GtkWidget* w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_decorated(GTK_WINDOW(w), FALSE);
	gtk_window_set_position(GTK_WINDOW(w), GTK_WIN_POS_CENTER);
	gtk_window_set_default_size(GTK_WINDOW(w), 460, 300);
	gtk_window_set_resizable(GTK_WINDOW(w), FALSE);
	gtk_window_set_keep_above(GTK_WINDOW(w), TRUE);
	gtk_window_set_skip_taskbar_hint(GTK_WINDOW(w), TRUE);
	gtk_window_set_type_hint(GTK_WINDOW(w), GDK_WINDOW_TYPE_HINT_SPLASHSCREEN);
	gtk_widget_set_name(w, "splash");
	gtk_widget_set_app_paintable(w, TRUE);

	GtkWidget* area = gtk_drawing_area_new();
	gtk_container_add(GTK_CONTAINER(w), area);

	SplashState* st = new SplashState();
	st->shownAt = now_ms();
	g_object_set_data_full(G_OBJECT(w), "state", st, +[](gpointer p) { delete static_cast<SplashState*>(p); });
	g_signal_connect(area, "draw", G_CALLBACK(onDraw), st);

	gtk_widget_show_all(w);
	// So it actually paints before whatever startup work runs next on this
	// same thread (loading the gate library, building the menus).
	while (gtk_events_pending()) gtk_main_iteration();

	// Redraws through the animation: the glow's pulse and the bar filling.
	st->tick = g_timeout_add(30, +[](gpointer data) -> gboolean {
		gtk_widget_queue_draw(GTK_WIDGET(data));
		return G_SOURCE_CONTINUE;
	}, area);
	return w;
}

void splashSetStatus(GtkWidget* splash, const char* status) {
	if (splash == nullptr) return;
	SplashState* st = static_cast<SplashState*>(g_object_get_data(G_OBJECT(splash), "state"));
	if (st == nullptr) return;
	st->status = status;
	gtk_widget_queue_draw(splash);
}

namespace {
struct HideRequest { GtkWidget* splash; GSourceFunc onHidden; gpointer data; };
}  // namespace

void hideSplashSoon(GtkWidget* splash, GSourceFunc onHidden, gpointer data) {
	if (splash == nullptr) { if (onHidden) onHidden(data); return; }
	SplashState* st = static_cast<SplashState*>(g_object_get_data(G_OBJECT(splash), "state"));
	const double elapsed = st ? now_ms() - st->shownAt : kMinVisibleMs;
	const int wait = (int)std::max(0.0, kMinVisibleMs - elapsed);
	g_timeout_add(wait, +[](gpointer p) -> gboolean {
		HideRequest* r = static_cast<HideRequest*>(p);
		SplashState* s = static_cast<SplashState*>(g_object_get_data(G_OBJECT(r->splash), "state"));
		if (s && s->tick) { g_source_remove(s->tick); s->tick = 0; }
		gtk_widget_destroy(r->splash);
		if (r->onHidden) r->onHidden(r->data);
		delete r;
		return G_SOURCE_REMOVE;
	}, new HideRequest{ splash, onHidden, data });
}
