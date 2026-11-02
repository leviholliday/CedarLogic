// The minimap (see MiniMap.h).

#include "MiniMap.h"
#include "Canvas.h"
#include "Window.h"

#include <algorithm>
#include <cmath>

MiniMap::MiniMap(CircuitWindow* window) : win(window) {
	area = gtk_drawing_area_new();
	gtk_widget_set_size_request(area, -1, 120);
	gtk_widget_add_events(area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
	gtk_widget_set_tooltip_text(area, "The whole page. Click or drag to go there.");
	g_signal_connect(area, "draw", G_CALLBACK(drawCb), this);
	g_signal_connect(area, "button-press-event", G_CALLBACK(pressCb), this);
	g_signal_connect(area, "button-release-event", G_CALLBACK(releaseCb), this);
	g_signal_connect(area, "motion-notify-event", G_CALLBACK(motionCb), this);
}

MiniMap::~MiniMap() {
	g_signal_handlers_disconnect_by_data(area, this);
	if (cache) cairo_surface_destroy(cache);
}

void MiniMap::queueDraw() { gtk_widget_queue_draw(area); }

bool MiniMap::fit(double w, double h, double& left, double& bottom, double& right, double& top, double& upp) const {
	Canvas* c = win->currentCanvas();
	if (c == nullptr || w < 4 || h < 4) return false;
	const int page = c->page();
	double ox, oy;
	c->origin(ox, oy);
	const double cw = c->width(), ch = c->height(), cupp = c->unitsPerPoint();
	// The visible area (world space), unioned with the page's own bounds so
	// panning past the drawn content never runs the outline off the picture.
	double l = ox, t = oy, r = ox + cw * cupp, b = oy - ch * cupp;
	double pl, pb, pr, pt;
	if (page >= 0 && cl_document_page_bounds(win->document(), page, &pl, &pb, &pr, &pt)) {
		l = std::min(l, pl); b = std::min(b, pb); r = std::max(r, pr); t = std::max(t, pt);
	}
	l -= 2; b -= 2; r += 2; t += 2;
	upp = std::max((r - l) / w, (t - b) / h);
	if (!(upp > 0)) return false;
	const double cx = (l + r) / 2, cy = (b + t) / 2;
	left = cx - w / 2 * upp;
	top = cy + h / 2 * upp;
	bottom = top - h * upp;
	right = left + w * upp;
	return true;
}

void MiniMap::draw(cairo_t* cr) {
	const double w = gtk_widget_get_allocated_width(area), h = gtk_widget_get_allocated_height(area);
	const bool dark = prefs().dark;
	const Palette pal{ dark, false };
	const RGBA bg = pal.canvas();
	cairo_set_source_rgb(cr, bg.r, bg.g, bg.b);
	cairo_paint(cr);

	Canvas* c = win->currentCanvas();
	double left = 0, bottom = 0, right = 0, top = 0, upp = 1;
	if (c == nullptr || !fit(w, h, left, bottom, right, top, upp)) return;
	const int page = c->page();
	const int scale = std::max(1, gtk_widget_get_scale_factor(area));

	const std::string key = format("%d|%d|%d|%d|%d|%.4f|%.4f|%u", page, dark ? 1 : 0, (int)w, (int)h, scale, left, top,
	                               win->editStamp());
	if (cache == nullptr || key != cacheKey) {
		if (cache) cairo_surface_destroy(cache);
		cache = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, std::max(1, (int)(w * scale)), std::max(1, (int)(h * scale)));
		cairo_t* cc = cairo_create(cache);
		cairo_surface_set_device_scale(cache, scale, scale);
		cairo_set_source_rgb(cc, bg.r, bg.g, bg.b);
		cairo_paint(cc);
		if (page >= 0) {
			CLDrawOptions o{};
			o.dark = dark;
			o.accent = prefs().accent;
			o.wireScale = 1.0;
			o.simView = false;
			o.thumbnail = true;
			o.showSelection = false;
			o.selectionFade = 1;
			cl_document_draw_ex(win->document(), page, cc, scale, left, top, upp, &o);
		}
		cairo_destroy(cc);
		cacheKey = key;
	}
	cairo_set_source_surface(cr, cache, 0, 0);
	cairo_paint(cr);

	// The visible area, outlined.
	double ox, oy;
	c->origin(ox, oy);
	const double cw = c->width(), ch = c->height(), cupp = c->unitsPerPoint();
	const double x = (ox - left) / upp, y = (top - oy) / upp;
	const double vw = std::max(3.0, cw * cupp / upp), vh = std::max(3.0, ch * cupp / upp);
	const RGBA accent = accentColor(dark);
	cairo_set_source_rgba(cr, accent.r, accent.g, accent.b, 0.9);
	cairo_set_line_width(cr, 1);
	cairo_rectangle(cr, x + 0.5, y + 0.5, vw, vh);
	cairo_stroke(cr);
}

void MiniMap::goTo(double vx, double vy) {
	Canvas* c = win->currentCanvas();
	double left = 0, bottom = 0, right = 0, top = 0, upp = 1;
	const double w = gtk_widget_get_allocated_width(area), h = gtk_widget_get_allocated_height(area);
	if (c == nullptr || !fit(w, h, left, bottom, right, top, upp)) return;
	c->panTo(left + vx * upp, top - vy * upp);
}

gboolean MiniMap::drawCb(GtkWidget*, cairo_t* cr, gpointer self) {
	guarded("drawing the minimap", [&] { static_cast<MiniMap*>(self)->draw(cr); });
	return TRUE;
}

gboolean MiniMap::pressCb(GtkWidget* w, GdkEventButton* e, gpointer self) {
	MiniMap* m = static_cast<MiniMap*>(self);
	if (e->button != 1) return FALSE;
	m->dragging = true;
	guarded("jumping to a spot", [&] { m->goTo(e->x, e->y); });
	return TRUE;
}

gboolean MiniMap::releaseCb(GtkWidget*, GdkEventButton* e, gpointer self) {
	if (e->button == 1) static_cast<MiniMap*>(self)->dragging = false;
	return TRUE;
}

gboolean MiniMap::motionCb(GtkWidget*, GdkEventMotion* e, gpointer self) {
	MiniMap* m = static_cast<MiniMap*>(self);
	if (!m->dragging) return FALSE;
	guarded("jumping to a spot", [&] { m->goTo(e->x, e->y); });
	return TRUE;
}
