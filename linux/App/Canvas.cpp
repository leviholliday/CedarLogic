// The circuit canvas (see Canvas.h).

#include "Canvas.h"
#include "Window.h"

#include <algorithm>
#include <cmath>

namespace {

const double kMinUpp = 0.004;   // very close
const double kMaxUpp = 1.0;     // very far
const double kZoomTime = 0.14;  // seconds, as the wx app's eased zoom
const char* kGateTarget = "application/x-cedarlogic-gate";

double clampUpp(double u) { return std::min(std::max(u, kMinUpp), kMaxUpp); }

int modifiersOf(guint state) {
	int m = 0;
	if (state & GDK_SHIFT_MASK) m |= CL_MOD_SHIFT;
	if (state & GDK_MOD1_MASK) m |= CL_MOD_OPTION;
	return m;
}

// Shift and nothing else counts as a bare key (Shift+S is Tidy Up).
bool bareKey(guint state) {
	return (state & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK | GDK_META_MASK)) == 0;
}

// The pointer's shape over the canvas (nothing happens before it's on screen).
void setCursor(GtkWidget* w, const char* name) {
	GdkWindow* gw = gtk_widget_get_window(w);
	if (gw == nullptr) return;
	GdkCursor* c = name ? gdk_cursor_new_from_name(gdk_window_get_display(gw), name) : nullptr;
	gdk_window_set_cursor(gw, c);
	if (c) g_object_unref(c);
}

}  // namespace

Canvas::Canvas(CircuitWindow* window, uint64_t pageKey) : win(window), key(pageKey) {
	area = gtk_drawing_area_new();
	g_object_ref_sink(area);
	g_object_set_data(G_OBJECT(area), "cl-window", window);
	gtk_widget_set_can_focus(area, TRUE);
	gtk_widget_set_hexpand(area, TRUE);
	gtk_widget_set_vexpand(area, TRUE);
	gtk_widget_add_events(area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK |
	                            GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK | GDK_KEY_PRESS_MASK |
	                            GDK_KEY_RELEASE_MASK | GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK |
	                            GDK_FOCUS_CHANGE_MASK | GDK_TOUCHPAD_GESTURE_MASK);
	g_signal_connect(area, "draw", G_CALLBACK(drawCb), this);
	g_signal_connect(area, "button-press-event", G_CALLBACK(pressCb), this);
	g_signal_connect(area, "button-release-event", G_CALLBACK(releaseCb), this);
	g_signal_connect(area, "motion-notify-event", G_CALLBACK(motionCb), this);
	g_signal_connect(area, "scroll-event", G_CALLBACK(scrollCb), this);
	g_signal_connect(area, "key-press-event", G_CALLBACK(keyPressCb), this);
	g_signal_connect(area, "key-release-event", G_CALLBACK(keyReleaseCb), this);
	g_signal_connect(area, "enter-notify-event", G_CALLBACK(crossingCb), this);
	g_signal_connect(area, "leave-notify-event", G_CALLBACK(crossingCb), this);
	g_signal_connect(area, "grab-broken-event", G_CALLBACK(grabBrokenCb), this);
	g_signal_connect(area, "focus-out-event", G_CALLBACK(focusOutCb), this);
	g_signal_connect(area, "event", G_CALLBACK(eventCb), this);
	g_signal_connect(area, "size-allocate", G_CALLBACK(sizeCb), this);

	// Gates dragged in from the palette.
	GtkTargetEntry target = { (gchar*)kGateTarget, GTK_TARGET_SAME_APP, 1 };
	gtk_drag_dest_set(area, GTK_DEST_DEFAULT_ALL, &target, 1, GDK_ACTION_COPY);
	g_signal_connect(area, "drag-data-received", G_CALLBACK(dragReceivedCb), this);
	g_signal_connect(area, "drag-motion", G_CALLBACK(dragMotionCb), this);
}

Canvas::~Canvas() {
	g_signal_handlers_disconnect_by_data(area, this);
	g_object_unref(area);
	dropBuffers();
}

void Canvas::dropBuffers() {
	if (frame) cairo_surface_destroy(frame);
	if (gridLayer) cairo_surface_destroy(gridLayer);
	frame = gridLayer = nullptr;
	gridValid = false;
}

int Canvas::page() const { return cl_document_page_index(win->document(), key); }

void Canvas::redraw() {
	gtk_widget_queue_draw(area);
	win->redrawMiniMap();
}

double Canvas::width() const { return gtk_widget_get_allocated_width(area); }
double Canvas::height() const { return gtk_widget_get_allocated_height(area); }

void Canvas::worldPoint(double vx, double vy, double& wx, double& wy) const {
	wx = originX + vx * upp;
	wy = originY - vy * upp;
}

void Canvas::center(double& wx, double& wy) const { worldPoint(width() / 2, height() / 2, wx, wy); }

bool Canvas::pointerWorld(double& wx, double& wy) const {
	GdkWindow* w = gtk_widget_get_window(area);
	if (w == nullptr) return false;
	GdkDisplay* display = gdk_window_get_display(w);
	GdkSeat* seat = display ? gdk_display_get_default_seat(display) : nullptr;
	GdkDevice* pointer = seat ? gdk_seat_get_pointer(seat) : nullptr;
	if (pointer == nullptr) return false;
	double x = 0, y = 0;
	gdk_window_get_device_position_double(w, pointer, &x, &y, nullptr);
	if (x < 0 || y < 0 || x >= width() || y >= height()) return false;
	worldPoint(x, y, wx, wy);
	return true;
}

// ---- Drawing -------------------------------------------------------------------

gboolean Canvas::drawCb(GtkWidget*, cairo_t* cr, gpointer self) {
	guarded("drawing", [&] { static_cast<Canvas*>(self)->draw(cr); });
	return TRUE;
}

void Canvas::draw(cairo_t* cr) {
	const double w = width(), h = height();
	if (needsFit && w > 1 && h > 1) zoomToFit(false);
	const int scale = std::max(1, gtk_widget_get_scale_factor(area));
	const int pw = (int)std::ceil(w * scale), ph = (int)std::ceil(h * scale);
	if (pw <= 0 || ph <= 0) return;
	if (frame == nullptr || cairo_image_surface_get_width(frame) != pw || cairo_image_surface_get_height(frame) != ph) {
		dropBuffers();
		frame = cairo_image_surface_create(CAIRO_FORMAT_RGB24, pw, ph);
		gridLayer = cairo_image_surface_create(CAIRO_FORMAT_RGB24, pw, ph);
		cairo_surface_set_device_scale(frame, scale, scale);
		cairo_surface_set_device_scale(gridLayer, scale, scale);
	}
	if (cairo_surface_status(frame) != CAIRO_STATUS_SUCCESS || cairo_surface_status(gridLayer) != CAIRO_STATUS_SUCCESS) {
		// Out of memory for the image: draw straight to the window instead.
		dropBuffers();
		drawScene(cr, scale);
		return;
	}
	cairo_t* fc = cairo_create(frame);
	drawScene(fc, scale);
	cairo_destroy(fc);
	cairo_surface_flush(frame);
	cairo_save(cr);
	cairo_set_source_surface(cr, frame, 0, 0);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_paint(cr);
	cairo_restore(cr);
}

void Canvas::drawScene(cairo_t* cr, int scale) {
	const bool sim = win->simView();
	const bool dark = prefs().dark || sim;
	const Palette pal{ dark, sim };

	// The background and grid: from the layer while nothing about them changed.
	const double fade = win->appearProgress();
	const GridKey key{ originX, originY, upp, fade, (int)width(), (int)height(), scale, prefs().gridStyle,
	                   dark, sim, prefs().majorGrid, prefs().showGrid };
	if (gridLayer) {
		if (!gridValid || !(key == gridKey)) {
			cairo_t* gc = cairo_create(gridLayer);
			const RGBA bg = pal.canvas();
			cairo_set_source_rgb(gc, bg.r, bg.g, bg.b);
			cairo_paint(gc);
			if (prefs().showGrid) drawGrid(gc, pal, scale, fade);
			cairo_destroy(gc);
			gridKey = key;
			gridValid = true;
		}
		cairo_set_source_surface(cr, gridLayer, 0, 0);
		cairo_paint(cr);
	} else {
		const RGBA bg = pal.canvas();
		cairo_set_source_rgb(cr, bg.r, bg.g, bg.b);
		cairo_paint(cr);
		if (prefs().showGrid) drawGrid(cr, pal, scale, fade);
	}

	CLDocument* doc = win->document();
	const int p = page();
	if (doc == nullptr || p < 0) return;
	CLDrawOptions o;
	o.dark = dark;
	o.accent = prefs().accent;
	o.wireScale = prefs().wireScale();
	o.simView = sim;
	o.thumbnail = false;
	o.showSelection = true;
	o.selectionFade = win->selectionFade();
	cl_document_draw_ex(doc, p, cr, scale, originX, originY, upp, &o);
	if (sim) {
		cl_simview_draw_flow(doc, p, cr, scale, originX, originY, upp, win->flowPhase(), prefs().wireScale());
		return;
	}
	const RGBA a = accentColor(dark);
	cl_edit_draw_overlay(doc, p, cr, scale, originX, originY, upp, a.r, a.g, a.b);
	double l, b, r, t, alpha;
	if (cl_edit_box(doc, &l, &b, &r, &t)) drawBox(cr, l, b, r, t, a, 1);
	else if (win->dragFadeBox(l, b, r, t, alpha) && alpha > 0) drawBox(cr, l, b, r, t, a, alpha);
}

void Canvas::drawBox(cairo_t* cr, double l, double b, double r, double t, const RGBA& accent, double alpha) {
	const double x = (l - originX) / upp, y = (originY - t) / upp;
	const double w = (r - l) / upp, h = (t - b) / upp;
	cairo_save(cr);
	cairo_rectangle(cr, x, y, w, h);
	cairo_set_source_rgba(cr, accent.r, accent.g, accent.b, 0.25 * alpha);
	cairo_fill(cr);
	cairo_rectangle(cr, x + 0.5, y + 0.5, std::max(0.0, w - 1), std::max(0.0, h - 1));
	cairo_set_source_rgba(cr, accent.r, accent.g, accent.b, alpha);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);
	cairo_restore(cr);
}

// GUICanvas::drawGridInto: a line (or dot) every grid unit, spread out so
// they're never closer than 13 pixels; every fifth one darker.
void Canvas::drawGrid(cairo_t* cr, const Palette& pal, double scale, double fade) {
	const double w = width(), h = height();
	const double unitsPerPixel = upp / scale;
	const int space = std::max(1, (int)(13 * unitsPerPixel));
	const double hair = 1 / scale;
	const long x0 = (long)std::floor(originX / space), x1 = (long)std::ceil((originX + w * upp) / space);
	const long y0 = (long)std::floor((originY - h * upp) / space), y1 = (long)std::ceil(originY / space);
	if (x1 < x0 || y1 < y0 || x1 - x0 >= 4000 || y1 - y0 >= 4000) return;
	const bool majorOn = prefs().majorGrid;
	auto major = [&](long i) { return majorOn && ((i % 5) + 5) % 5 == 0; };
	auto sx = [&](long i) { return ((double)i * space - originX) / upp; };
	auto sy = [&](long i) { return (originY - (double)i * space) / upp; };

	cairo_save(cr);
	if (prefs().gridStyle == 1) {
		if ((x1 - x0 + 1) * (y1 - y0 + 1) >= 60000) { cairo_restore(cr); return; }
		const double r = 1.1 / scale, rMajor = 1.7 / scale;
		const RGBA dot = pal.grid(0.08 * 3 * fade), dotMajor = pal.grid(0.08 * 5 * fade);
		for (int pass = 0; pass < 2; pass++) {
			const bool isMajor = pass == 1;
			const RGBA& c = isMajor ? dotMajor : dot;
			cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a);
			cairo_new_path(cr);
			for (long ix = x0; ix <= x1; ix++) {
				for (long iy = y0; iy <= y1; iy++) {
					if ((major(ix) && major(iy)) != isMajor) continue;
					const double rr = isMajor ? rMajor : r;
					cairo_new_sub_path(cr);
					cairo_arc(cr, sx(ix), sy(iy), rr, 0, 2 * M_PI);
				}
			}
			cairo_fill(cr);
		}
		cairo_restore(cr);
		return;
	}
	cairo_set_line_width(cr, hair);
	for (int pass = 0; pass < 2; pass++) {
		const bool isMajor = pass == 1;
		// On the dark canvas the darker lines stay quieter, so low wires
		// don't read as grid.
		const RGBA c = pal.grid((isMajor ? 0.08 * (pal.dark && !pal.simView ? 1.6 : 2.5) : 0.08) * fade);
		cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a);
		cairo_new_path(cr);
		for (long i = x0; i <= x1; i++) {
			if (major(i) != isMajor) continue;
			const double x = std::round(sx(i) * scale) / scale + hair / 2;
			cairo_move_to(cr, x, 0);
			cairo_line_to(cr, x, h);
		}
		for (long i = y0; i <= y1; i++) {
			if (major(i) != isMajor) continue;
			const double y = std::round(sy(i) * scale) / scale + hair / 2;
			cairo_move_to(cr, 0, y);
			cairo_line_to(cr, w, y);
		}
		cairo_stroke(cr);
	}
	cairo_restore(cr);
}

// ---- Camera --------------------------------------------------------------------

bool Canvas::fitBoxOfPage(double& l, double& b, double& r, double& t) const {
	const int p = page();
	if (p < 0 || !cl_document_page_bounds(win->document(), p, &l, &b, &r, &t)) {
		l = -20; b = -15; r = 20; t = 15;
		return false;
	}
	return true;
}

void Canvas::zoomToFit(bool animate) {
	const double w = width(), h = height();
	if (w <= 1 || h <= 1) { needsFit = true; return; }
	needsFit = false;
	double l, b, r, t;
	fitBoxOfPage(l, b, r, t);
	const double pad = 3;
	const double u = clampUpp(std::max((r - l + 2 * pad) / w, (t - b + 2 * pad) / h));
	const double ox = (l + r) / 2 - w * u / 2, oy = (b + t) / 2 + h * u / 2;
	if (animate) {
		startZoom(ox, oy, u);
	} else {
		zooming = false;
		upp = u; originX = ox; originY = oy;
		redraw();
		win->statusDirty = true;
	}
}

void Canvas::zoomBy(double factor, double vx, double vy) {
	if (!(factor > 0) || !std::isfinite(factor)) return;
	zooming = false;
	double wx, wy;
	worldPoint(vx, vy, wx, wy);
	upp = clampUpp(upp / factor);
	originX = wx - vx * upp;
	originY = wy + vy * upp;
	redraw();
	win->statusDirty = true;
}

void Canvas::animateZoom(double factor) {
	// From where an eased zoom in progress is heading, so quick presses add up.
	const double bx = zooming ? toX : originX, by = zooming ? toY : originY, bu = zooming ? toUpp : upp;
	const double cx = width() / 2, cy = height() / 2;
	const double wx = bx + cx * bu, wy = by - cy * bu;
	const double u = clampUpp(bu / factor);
	startZoom(wx - cx * u, wy + cy * u, u);
}

void Canvas::zoomActual() { animateZoom((zooming ? toUpp : upp) / 0.1); }

int Canvas::zoomPercent() const { return upp > 0 ? (int)std::lround(100 * 0.1 / upp) : 100; }

void Canvas::startZoom(double ox, double oy, double u) {
	fromX = originX; fromY = originY; fromUpp = upp;
	toX = ox; toY = oy; toUpp = u;
	zoomStart = g_get_monotonic_time();
	zooming = true;
	stepAnimation();
}

bool Canvas::stepAnimation() {
	if (!zooming) return false;
	const double t = std::min(1.0, (g_get_monotonic_time() - zoomStart) / 1e6 / kZoomTime);
	const double e = 1 - std::pow(1 - t, 3);
	// The scale eases geometrically; the origin follows.
	upp = fromUpp * std::pow(toUpp / fromUpp, e);
	originX = fromX + (toX - fromX) * e;
	originY = fromY + (toY - fromY) * e;
	if (t >= 1) { zooming = false; originX = toX; originY = toY; upp = toUpp; }
	redraw();
	win->statusDirty = true;
	return zooming;
}

void Canvas::pan(double dx, double dy) {
	zooming = false;
	originX -= dx * upp;
	originY += dy * upp;
	redraw();
	win->statusDirty = true;
}

void Canvas::panTo(double wx, double wy) {
	zooming = false;
	originX = wx - width() / 2 * upp;
	originY = wy + height() / 2 * upp;
	redraw();
	win->statusDirty = true;
}

void Canvas::sizeCb(GtkWidget*, GdkRectangle* a, gpointer self) {
	Canvas* c = static_cast<Canvas*>(self);
	c->onSizeChanged(c->lastW, c->lastH, a->width, a->height);
	c->lastW = a->width;
	c->lastH = a->height;
}

void Canvas::onSizeChanged(int oldW, int oldH, int w, int h) {
	// Keep the middle of the view where it was as the window resizes.
	if (oldW > 1 && oldH > 1 && !needsFit) {
		originX -= (w - oldW) / 2.0 * upp;
		originY += (h - oldH) / 2.0 * upp;
	}
}

// ---- Pointer -------------------------------------------------------------------

gboolean Canvas::pressCb(GtkWidget*, GdkEventButton* e, gpointer self) {
	return guarded("a click", [&] { return static_cast<Canvas*>(self)->onPress(e); });
}
gboolean Canvas::releaseCb(GtkWidget*, GdkEventButton* e, gpointer self) {
	return guarded("a click", [&] { return static_cast<Canvas*>(self)->onRelease(e); });
}
gboolean Canvas::motionCb(GtkWidget*, GdkEventMotion* e, gpointer self) {
	return guarded("moving the pointer", [&] { return static_cast<Canvas*>(self)->onMotion(e); });
}
gboolean Canvas::scrollCb(GtkWidget*, GdkEventScroll* e, gpointer self) {
	return guarded("scrolling", [&] { return static_cast<Canvas*>(self)->onScroll(e); });
}
gboolean Canvas::keyPressCb(GtkWidget*, GdkEventKey* e, gpointer self) {
	return guarded("a key", [&] { return static_cast<Canvas*>(self)->onKeyPress(e); });
}
gboolean Canvas::keyReleaseCb(GtkWidget*, GdkEventKey* e, gpointer self) {
	return guarded("a key", [&] { return static_cast<Canvas*>(self)->onKeyRelease(e); });
}

gboolean Canvas::crossingCb(GtkWidget*, GdkEventCrossing* e, gpointer self) {
	static_cast<Canvas*>(self)->pointerInside = e->type == GDK_ENTER_NOTIFY;
	return FALSE;
}

gboolean Canvas::grabBrokenCb(GtkWidget*, GdkEventGrabBroken*, gpointer self) {
	// Something took the pointer mid-drag (a menu, another window): finish
	// the gesture where it is, so nothing is left half-done.
	guarded("a drag", [&] { static_cast<Canvas*>(self)->cancelDrag(); });
	return FALSE;
}

gboolean Canvas::focusOutCb(GtkWidget*, GdkEventFocus*, gpointer self) {
	Canvas* c = static_cast<Canvas*>(self);
	c->spaceDown = false;
	return FALSE;
}

gboolean Canvas::eventCb(GtkWidget*, GdkEvent* e, gpointer self) {
	if (e->type == GDK_TOUCHPAD_PINCH) {
		guarded("a pinch", [&] { static_cast<Canvas*>(self)->onPinch(&e->touchpad_pinch); });
		return TRUE;
	}
	return FALSE;
}

void Canvas::cancelDrag() {
	if (drag == Drag::Edit) {
		double wx, wy;
		worldPoint(lastX, lastY, wx, wy);
		cl_edit_release(win->document(), wx, wy);
		win->edited();
	}
	drag = Drag::None;
	setCursor(area, nullptr);
	redraw();
}

bool Canvas::moving() const { return drag == Drag::Edit || win->isFloating(); }

bool Canvas::onPress(GdkEventButton* e) {
	CLDocument* doc = win->document();
	const int p = page();
	if (doc == nullptr || p < 0) return FALSE;
	gtk_widget_grab_focus(area);
	zooming = false;
	lastX = e->x;
	lastY = e->y;
	double wx, wy;
	worldPoint(e->x, e->y, wx, wy);

	if (e->button == 2) {   // the middle button moves around
		drag = Drag::Pan;
		setCursor(area, "grabbing");
		return TRUE;
	}
	if (e->button == 3) {
		if (e->type != GDK_BUTTON_PRESS) return TRUE;
		if (win->simView()) return TRUE;
		if (win->locked()) { win->lockNudge(); return TRUE; }
		const int target = cl_edit_context(doc, p, wx, wy, upp);
		if (prefs().rightClickRotate && target == CL_CONTEXT_GATE) {
			win->rotate();
			return TRUE;
		}
		win->selectionChanged();
		redraw();
		win->showContextMenu(target, wx, wy, e);
		return TRUE;
	}
	if (e->button != 1) return FALSE;

	if (e->type == GDK_2BUTTON_PRESS) {
		// The second press of a double-click already began a gesture: end
		// it in place, then open the gate's settings, or fit the page.
		if (win->isFloating()) return TRUE;
		if (drag == Drag::Edit) cl_edit_release(doc, wx, wy);
		drag = Drag::None;
		if (win->simView() || win->locked()) return TRUE;
		if (cl_edit_single_gate(doc, p) >= 0) win->showSettings();
		else zoomToFit(true);
		win->selectionChanged();
		redraw();
		return TRUE;
	}
	if (e->type != GDK_BUTTON_PRESS) return TRUE;

	if (win->hasPendingGate() && win->placePendingGate(wx, wy)) {
		redraw();
		return TRUE;   // it's on the pointer now; the next click drops it
	}
	if (spaceDown || (e->state & GDK_CONTROL_MASK)) {
		drag = Drag::Pan;
		pannedWhileSpaceDown = true;
		setCursor(area, "grabbing");
		return TRUE;
	}
	// Simulation View and Lock: parts still take clicks (switches, keypads);
	// anywhere else, a drag moves around.
	if (win->simView() || win->locked()) {
		if (cl_document_click(doc, p, wx, wy)) {
			win->redraw();
		} else {
			drag = Drag::Pan;
			setCursor(area, "grabbing");
			if (win->locked() && !win->simView()) win->lockNudge();
		}
		return TRUE;
	}
	cl_edit_press(doc, p, wx, wy, modifiersOf(e->state), upp);
	drag = Drag::Edit;
	redraw();
	win->selectionChanged();
	return TRUE;
}

bool Canvas::onMotion(GdkEventMotion* e) {
	CLDocument* doc = win->document();
	const int p = page();
	if (doc == nullptr || p < 0) return FALSE;
	const double dx = e->x - lastX, dy = e->y - lastY;
	lastX = e->x;
	lastY = e->y;
	double wx, wy;
	worldPoint(e->x, e->y, wx, wy);
	win->pointerMoved(wx, wy);
	switch (drag) {
	case Drag::Pan:
		pan(dx, dy);
		break;
	case Drag::Edit:
		cl_edit_drag(doc, wx, wy);
		redraw();
		break;
	case Drag::None:
		if (win->hasPendingGate() && win->placePendingGate(wx, wy)) { redraw(); break; }
		if (win->simView()) break;
		if (cl_edit_hover(doc, p, wx, wy, upp)) redraw();
		break;
	}
	return TRUE;
}

bool Canvas::onRelease(GdkEventButton* e) {
	CLDocument* doc = win->document();
	lastX = e->x;
	lastY = e->y;
	if (drag == Drag::Edit && e->button == 1 && doc) {
		double l, b, r, t;
		if (cl_edit_box(doc, &l, &b, &r, &t)) win->fadeOutDragBox(l, b, r, t);
		double wx, wy;
		worldPoint(e->x, e->y, wx, wy);
		cl_edit_release(doc, wx, wy);
		drag = Drag::None;
		win->edited();
	} else if (drag == Drag::Pan && (e->button == 1 || e->button == 2)) {
		drag = Drag::None;
	}
	setCursor(area, spaceDown ? "grab" : nullptr);
	redraw();
	return TRUE;
}

// Per device (Preferences > Canvas): a wheel mouse zooms and a touchpad moves
// around, by default. Ctrl+scroll always zooms; Shift+scroll moves sideways.
bool Canvas::onScroll(GdkEventScroll* e) {
	zooming = false;
	const Prefs& pr = prefs();
	GdkDevice* src = gdk_event_get_source_device((GdkEvent*)e);
	const bool touchpad = src && gdk_device_get_source(src) == GDK_SOURCE_TOUCHPAD;
	const bool ctrl = e->state & GDK_CONTROL_MASK, shift = e->state & GDK_SHIFT_MASK;
	double dx = 0, dy = 0;
	switch (e->direction) {
	case GDK_SCROLL_UP: dy = -1; break;
	case GDK_SCROLL_DOWN: dy = 1; break;
	case GDK_SCROLL_LEFT: dx = -1; break;
	case GDK_SCROLL_RIGHT: dx = 1; break;
	case GDK_SCROLL_SMOOTH: gdk_event_get_scroll_deltas((GdkEvent*)e, &dx, &dy); break;
	}
	if (dx == 0 && dy == 0) return TRUE;
	const bool vertical = std::fabs(dy) >= std::fabs(dx);
	const bool zooms = touchpad ? pr.touchpadScroll == 0 : pr.mouseWheel == 0;
	const double inSign = (touchpad ? pr.reverseTouchpad : pr.reverseWheel) ? -1 : 1;
	// "In" is scrolling up (away), one wheel notch a ZOOM_STEP (0.75) as in wx.
	if (ctrl || (zooms && !shift && vertical)) {
		const double steps = -dy * (ctrl ? 1 : inSign) * (touchpad ? 0.35 : 1);
		zoomBy(std::pow(1 / 0.75, steps), e->x, e->y);
	} else if (touchpad) {
		pan(-dx * 30, -dy * 30);
	} else if (shift || !vertical) {
		pan(-(shift ? dy : dx) * 30, 0);
	} else {
		pan(0, -dy * 30);
	}
	return TRUE;
}

void Canvas::onPinch(GdkEventTouchpadPinch* e) {
	if (e->phase == GDK_TOUCHPAD_GESTURE_PHASE_BEGIN) { pinchLast = 1; return; }
	if (e->phase != GDK_TOUCHPAD_GESTURE_PHASE_UPDATE || !(e->scale > 0)) return;
	const double factor = e->scale / pinchLast;
	pinchLast = e->scale;
	zoomBy(factor, e->x, e->y);
}

// ---- Keys ----------------------------------------------------------------------

bool Canvas::onKeyPress(GdkEventKey* e) {
	CLDocument* doc = win->document();
	if (doc == nullptr) return FALSE;
	const guint k = e->keyval;
	const bool shift = e->state & GDK_SHIFT_MASK;
	const bool bare = bareKey(e->state);
	const bool arrow = k == GDK_KEY_Left || k == GDK_KEY_Right || k == GDK_KEY_Up || k == GDK_KEY_Down ||
	                   k == GDK_KEY_KP_Left || k == GDK_KEY_KP_Right || k == GDK_KEY_KP_Up || k == GDK_KEY_KP_Down;
	const bool isLeft = k == GDK_KEY_Left || k == GDK_KEY_KP_Left, isRight = k == GDK_KEY_Right || k == GDK_KEY_KP_Right;
	const bool isUp = k == GDK_KEY_Up || k == GDK_KEY_KP_Up;
	const bool isReturn = k == GDK_KEY_Return || k == GDK_KEY_KP_Enter;
	const bool isDelete = k == GDK_KEY_Delete || k == GDK_KEY_BackSpace || k == GDK_KEY_KP_Delete;
	const guint lower = gdk_keyval_to_lower(k);

	// Simulation View: Escape leaves, Space runs and pauses, arrows move around.
	if (win->simView()) {
		if (!bare) return FALSE;
		if (k == GDK_KEY_Escape) win->toggleSimView();
		else if (k == GDK_KEY_space) { if (!spaceDown) { spaceDown = true; win->toggleRunning(); } }
		else if (arrow) pan(isLeft ? 40 : isRight ? -40 : 0, isUp ? 40 : (isLeft || isRight) ? 0 : -40);
		else if (k == GDK_KEY_equal || k == GDK_KEY_plus || k == GDK_KEY_KP_Add) animateZoom(1 / 0.75);
		else if (k == GDK_KEY_minus || k == GDK_KEY_KP_Subtract) animateZoom(0.75);
		else if (lower == GDK_KEY_t && !shift) win->makeTruthTable();
		else if (k == GDK_KEY_question) win->showShortcuts();
		return TRUE;
	}

	// Tidy Up on show: Return keeps it, Escape puts it back, Tab tries the
	// other mode. Anything else keeps it and carries on.
	if (win->tidyActive() && bare) {
		if (isReturn) { win->endTidy(true); return TRUE; }
		if (k == GDK_KEY_Escape) { win->endTidy(false); return TRUE; }
		if (k == GDK_KEY_Tab || k == GDK_KEY_ISO_Left_Tab) { win->switchTidyMode(); return TRUE; }
		if (k != GDK_KEY_Shift_L && k != GDK_KEY_Shift_R) win->endTidy(true);
	}

	if (k == GDK_KEY_Escape) {
		if (win->hasPendingGate()) { win->clearPendingGate(); return TRUE; }
		// Mid-move: first just the connections C made, then the move.
		if (moving() && cl_edit_take_back_connects(doc) > 0) {
			win->note("Took back the connections.");
			win->redraw();
			return TRUE;
		}
		if (win->isFloating()) { win->cancelFloating(); return TRUE; }
		if (drag == Drag::Edit || cl_edit_is_connecting(doc)) {
			cl_edit_cancel(doc);
			drag = Drag::None;
			win->edited();
			return TRUE;
		}
		win->selectNone();
		return TRUE;
	}

	if (!bare) return FALSE;   // Ctrl and Alt combinations are the menus'

	// Arrows nudge the selection, or move around when nothing's selected.
	if (arrow) {
		if (win->hasSelection() && win->canEdit()) {
			const double s = shift ? 2.5 : 0.5;
			win->nudge(isLeft ? -s : isRight ? s : 0, isUp ? s : (isLeft || isRight) ? 0 : -s);
		} else {
			const double s = shift ? 200 : 40;
			pan(isLeft ? s : isRight ? -s : 0, isUp ? s : (isLeft || isRight) ? 0 : -s);
		}
		return TRUE;
	}

	if (k == GDK_KEY_space) {
		if (!spaceDown) {
			spaceDown = true;
			pannedWhileSpaceDown = false;
			if (drag == Drag::None) setCursor(area, "grab");
		}
		return TRUE;
	}

	if (isDelete) {
		if (!win->canEdit()) { win->lockNudge(); return TRUE; }
		win->deleteSelection();
		return TRUE;
	}

	// Shift+1...9, 0: that palette category (the tenth is 0).
	if (shift) {
		guint plain = 0;
		GdkKeymap* keymap = gdk_keymap_get_for_display(gtk_widget_get_display(area));
		if (gdk_keymap_translate_keyboard_state(keymap, e->hardware_keycode, (GdkModifierType)0, e->group,
		                                        &plain, nullptr, nullptr, nullptr) &&
		    plain >= GDK_KEY_0 && plain <= GDK_KEY_9) {
			const int n = plain == GDK_KEY_0 ? 10 : (int)(plain - GDK_KEY_0);
			win->showPaletteCategory(n - 1);
			return TRUE;
		}
	}

	if (k == GDK_KEY_question || (lower == GDK_KEY_slash && shift)) { win->showShortcuts(); return TRUE; }

	// The single-letter keys, as in the wx app.
	switch (lower) {
	case GDK_KEY_c:
		if (shift) break;
		// C while something is moving (dragged, or floating on the pointer):
		// connect it to the pins it's next to and keep moving. The
		// connections are kept at the drop; Escape takes back just them.
		if (moving()) {
			if (win->canEdit() && pointerInside) {
				const int p = page();
				const int n = cl_edit_connect_while_moving(doc, p, upp);
				if (n > 0) win->note(format("Connected %d pin%s. Escape takes it back.", n, n == 1 ? "" : "s"));
				else if (n == 0) win->note("Nothing close enough to connect.");
				win->redraw();
			}
			return TRUE;
		}
		win->copy();
		return TRUE;
	case GDK_KEY_v: if (shift) break; if (win->canEdit()) win->paste(); else win->lockNudge(); return TRUE;
	case GDK_KEY_x: if (shift) break; if (win->canEdit()) win->cut(); else win->lockNudge(); return TRUE;
	case GDK_KEY_d: if (shift) break; if (win->canEdit()) win->duplicate(); else win->lockNudge(); return TRUE;
	case GDK_KEY_a: if (shift) break; if (win->canEdit()) win->quickAdd(); else win->lockNudge(); return TRUE;
	case GDK_KEY_r: if (shift) break; if (win->canEdit()) win->rotate(); else win->lockNudge(); return TRUE;
	case GDK_KEY_s:
		if (!win->canEdit()) { win->lockNudge(); return TRUE; }
		if (shift) win->tidy(); else win->straighten();
		return TRUE;
	case GDK_KEY_t: if (shift) break; win->makeTruthTable(); return TRUE;
	default: break;
	}
	return FALSE;
}

bool Canvas::onKeyRelease(GdkEventKey* e) {
	if (e->keyval != GDK_KEY_space) return FALSE;
	const bool was = spaceDown;
	spaceDown = false;
	if (drag == Drag::None) setCursor(area, nullptr);
	if (!was || win->simView()) return TRUE;   // there Space pauses (on the way down)
	// A tap zooms to fit (a hold-and-drag moved around).
	if (!pannedWhileSpaceDown) zoomToFit(true);
	return TRUE;
}

// ---- Gates dropped from the palette ----------------------------------------------

gboolean Canvas::dragMotionCb(GtkWidget*, GdkDragContext* ctx, gint, gint, guint time, gpointer self) {
	Canvas* c = static_cast<Canvas*>(self);
	if (!c->win->canEdit()) { gdk_drag_status(ctx, (GdkDragAction)0, time); return TRUE; }
	gdk_drag_status(ctx, GDK_ACTION_COPY, time);
	return TRUE;
}

void Canvas::dragReceivedCb(GtkWidget*, GdkDragContext* ctx, gint x, gint y, GtkSelectionData* data,
                            guint, guint time, gpointer self) {
	Canvas* c = static_cast<Canvas*>(self);
	const guchar* raw = gtk_selection_data_get_data(data);
	const gint len = gtk_selection_data_get_length(data);
	bool ok = false;
	if (raw && len > 0 && c->win->canEdit()) guarded("dropping a gate", [&] {
		const std::string name((const char*)raw, (size_t)len);
		double wx, wy;
		c->worldPoint(x, y, wx, wy);
		const int p = c->page();
		if (p >= 0 && cl_edit_add_gate(c->win->document(), p, name.c_str(), wx, wy)) {
			ok = true;
			c->win->edited();
			gtk_widget_grab_focus(c->area);
		}
	});
	gtk_drag_finish(ctx, ok, FALSE, time);
}
