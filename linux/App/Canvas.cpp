// The circuit canvas (see Canvas.h).

#include "Canvas.h"
#include "Brand.h"
#include "Chrome.h"
#include "Shortcuts.h"
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

	// Gates dragged in from the palette; .cdl files from the file manager.
	GtkTargetEntry targets[] = { { (gchar*)kGateTarget, GTK_TARGET_SAME_APP, 1 }, { (gchar*)"text/uri-list", 0, 2 } };
	gtk_drag_dest_set(area, GTK_DEST_DEFAULT_ALL, targets, 2, GDK_ACTION_COPY);
	g_signal_connect(area, "drag-data-received", G_CALLBACK(dragReceivedCb), this);
	g_signal_connect(area, "drag-motion", G_CALLBACK(dragMotionCb), this);
}

Canvas::~Canvas() {
	if (tagTimer) g_source_remove(tagTimer);
	if (motionTick) gtk_widget_remove_tick_callback(area, motionTick);
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
	drawOverlays(cr, (float)w, (float)h);
}

// ---- Overlays ------------------------------------------------------------------

void Canvas::drawOverlays(cairo_t* cr, float w, float h) {
	hits.clear();
	sliderRight = sliderLeft = 0;
	// In a split, the bar for Simulation View sits under the first side; the
	// banner is the side's you're working in.
	// It slides up from the bottom, as the Mac's does.
	simBarIn.go(win->simView() && win->paneOf(this) == 0 ? 1 : 0, 0.34);
	if (simBarIn.active()) keepMoving();
	const float simIn = (float)simBarIn.value();
	if (simIn > 0.01f) {
		cairo_save(cr);
		cairo_translate(cr, 0, (1 - simIn) * 80);
		drawSimBar(cr, w, h);
		cairo_restore(cr);
		if (simIn < 1) hits.clear();
	}
	if (win->currentCanvas() == this) drawBanner(cr, w);
	drawWireTag(cr, w, h);
	double t;
	if (win->openingCard(t)) drawOpeningCard(cr, w, h, t);
}

// A circuit opening (the Mac's OpeningCard): over the canvas, a small glass
// card with its name and what's in it, a sweep of light and a quick line
// filling; then the card lifts away and the circuit fades up. 0.88 s.
void Canvas::drawOpeningCard(cairo_t* cr, float w, float h, double t) {
	auto ease = [](double x) { const double c = std::min(1.0, std::max(0.0, x)); return 1 - std::pow(1 - c, 3); };
	auto smooth = [](double x) { const double c = std::min(1.0, std::max(0.0, x)); return c * c * c * (c * (c * 6 - 15) + 10); };
	const bool dark = prefs().dark;
	const double inP = t < 0 ? 0 : ease(t / 0.24), line = smooth((t - 0.1) / 0.45), out = smooth((t - 0.58) / 0.3);
	const double sweep = smooth((t - 0.14) / 0.5);
	// The canvas, held back until the card lifts.
	const Palette pal{ dark, false };
	fillRect(cr, rectF(0, 0, w, h), withAlpha(fromRGBA(pal.canvas()), (float)(1 - out)));
	const float opacity = (float)(inP * (1 - out));
	if (opacity <= 0.003f) return;
	const std::string title = win->titleText(), detail = win->openingDetail();
	const float textW = std::max({ textWidth(title, 15, true), textWidth(detail, 11.5f), 170.0f });
	const float cw = std::max(300.0f, 20 + 46 + 14 + textW + 20), ch = 16 + 46 + 16 + 4;
	const RectF card = rectF((w - cw) / 2, (h - ch) / 2, (w + cw) / 2, (h + ch) / 2);
	const float s = (float)((0.94 + 0.06 * inP) * (1 + 0.04 * out));
	cairo_save(cr);
	cairo_translate(cr, w / 2, h / 2);
	cairo_scale(cr, s, s);
	cairo_translate(cr, -w / 2, -h / 2);
	cairo_push_group(cr);
	brand::glow(cr, rectF(card.left, card.top + 10, card.right, card.bottom + 10), 20, colorF(0, 0, 0, dark ? 0.45f : 0.18f), 22);
	fillRound(cr, card, 20, dark ? colorF(0.17f, 0.18f, 0.21f, 0.97f) : colorF(0.985f, 0.985f, 0.99f, 0.97f));
	strokeRound(cr, rectF(card.left + 0.4f, card.top + 0.4f, card.right - 0.4f, card.bottom - 0.4f), 19.6f,
	            dark ? colorF(1, 1, 1, 0.14f) : colorF(0, 0, 0, 0.08f), 0.8f);
	const float ix = card.left + 20, iy = card.top + 16;
	brand::icon(cr, ix, iy, 46, 0.35f);
	const float tx = ix + 46 + 14;
	drawText(cr, title, rectF(tx, iy, card.right - 16, iy + 20), 15, dark ? colorF(1, 1, 1) : colorF(0, 0, 0, 0.85f), TextAlign::Leading, true);
	drawText(cr, detail, rectF(tx, iy + 23, card.right - 16, iy + 39), 11.5f, dark ? colorF(1, 1, 1, 0.55f) : colorF(0, 0, 0, 0.5f));
	const RectF track = rectF(tx, iy + 44, tx + 170, iy + 46.5f);
	fillRound(cr, track, 1.25f, dark ? colorF(1, 1, 1, 0.08f) : colorF(0, 0, 0, 0.08f));
	if (line > 0.01) {
		const RectF fill = rectF(track.left, track.top, track.left + (float)(170 * line), track.bottom);
		fillRound(cr, rectF(fill.left - 2, fill.top - 2, fill.right + 2, fill.bottom + 2), 3, withAlpha(brand::kNeon, 0.2f));
		fillRound(cr, fill, 1.25f, brand::kNeon);
	}
	// The sweep of light across the card.
	if (sweep > 0 && sweep < 1) {
		cairo_save(cr);
		roundedPath(cr, card, 20);
		cairo_clip(cr);
		const float bx = (float)(card.left - 120 + (cw + 240) * sweep);
		cairo_translate(cr, bx + 45, (card.top + card.bottom) / 2);
		cairo_rotate(cr, 18 * G_PI / 180);
		cairo_translate(cr, -(bx + 45), -(card.top + card.bottom) / 2);
		cairo_pattern_t* g = cairo_pattern_create_linear(bx, 0, bx + 90, 0);
		cairo_pattern_add_color_stop_rgba(g, 0, 1, 1, 1, 0);
		cairo_pattern_add_color_stop_rgba(g, 0.5, 1, 1, 1, dark ? 0.12 : 0.35);
		cairo_pattern_add_color_stop_rgba(g, 1, 1, 1, 1, 0);
		cairo_rectangle(cr, bx, card.top - ch / 2, 90, ch * 2);
		cairo_set_source(cr, g);
		cairo_fill(cr);
		cairo_pattern_destroy(g);
		cairo_restore(cr);
	}
	cairo_pop_group_to_source(cr);
	cairo_paint_with_alpha(cr, opacity);
	cairo_restore(cr);
}

// Simulation View's control bar (the Mac's SimBar): a dark glass panel along
// the bottom with a breathing LIVE light, pause and step, the speed, a chip
// for every switch and light, and Done.
void Canvas::drawSimBar(cairo_t* cr, float w, float h) {
	const Color on = colorF(0.28f, 0.93f, 1.0f), ink = colorF(0.86f, 0.93f, 1.0f);
	const Color dim = colorF(0.48f, 0.58f, 0.68f), live = colorF(0.36f, 1.0f, 0.62f);
	const Color amber = colorF(1.0f, 0.72f, 0.25f);
	const float barW = std::min(1040.0f, w - 28), barH = 52;
	if (barW < 360) return;
	const float x0 = (w - barW) / 2, y0 = h - 14 - barH, cy = y0 + barH / 2;
	const RectF bar = rectF(x0, y0, x0 + barW, y0 + barH);
	fillRound(cr, rectF(bar.left, bar.top + 3, bar.right, bar.bottom + 3), 14, colorF(0, 0, 0, 0.30f));
	// Dark glass over the live circuit.
	frosted(cr, bar, 14);
	fillRound(cr, bar, 14, colorF(0.055f, 0.070f, 0.090f, 0.78f));
	strokeRound(cr, bar, 14, withAlpha(on, 0.22f));
	fillRect(cr, rectF(bar.left + 14, bar.top + 1, bar.right - 14, bar.top + 2), colorF(1, 1, 1, 0.06f));

	const bool paused = !win->running();
	const double t = g_get_monotonic_time() / 1e6;
	const float breathe = paused ? 1.0f : (float)(0.6 + 0.4 * std::sin(t * 3.2));
	const Color light = paused ? amber : live;
	float x = x0 + 18;
	fillCircle(cr, pointF(x + 9, cy), 9, withAlpha(light, 0.12f * breathe));
	fillCircle(cr, pointF(x + 9, cy), 4, withAlpha(light, 0.55f + 0.45f * breathe));
	x += 28;
	drawText(cr, "SIMULATION", rectF(x, cy - 16, x + 80, cy - 3), 9, dim);
	drawText(cr, paused ? "PAUSED" : "LIVE", rectF(x, cy - 3, x + 80, cy + 15), 13, paused ? amber : ink, TextAlign::Leading, true);
	x += 70 + 14;
	auto divider = [&] { fillRect(cr, rectF(x, cy - 14, x + 1, cy + 14), colorF(1, 1, 1, 0.08f)); x += 1 + 14; };
	divider();
	auto button = [&](const char* glyph, bool lit, Color color, const char* action) {
		const RectF r = rectF(x, cy - 16, x + 32, cy + 16);
		const bool hot = hotHit == (int)hits.size();
		fillRound(cr, r, 9, lit ? withAlpha(on, 0.18f) : colorF(1, 1, 1, hot ? 0.12f : 0.07f));
		strokeRound(cr, r, 9, colorF(1, 1, 1, 0.08f));
		drawIcon(cr, glyph, r, 14, color);
		hits.push_back({ r.left, r.top, r.right, r.bottom, action });
		x += 32 + 8;
	};
	button(paused ? Icon::Play : Icon::Pause, paused, paused ? on : ink, "win.running");
	button(Icon::Step, false, ink, "win.step");
	x += 6;
	divider();
	// Speed, fast on the right.
	drawText(cr, "SPEED", rectF(x, cy - 19, x + 60, cy - 6), 9, dim);
	const float trackW = 150, ty = cy + 5;
	sliderLeft = x; sliderRight = x + trackW; sliderTop = ty - 10; sliderBottom = ty + 10;
	const float f = (float)speedFraction(cl_document_step_ms(win->document()));
	fillRound(cr, rectF(x, ty - 2, x + trackW, ty + 2), 2, colorF(1, 1, 1, 0.12f));
	fillRound(cr, rectF(x, ty - 2, x + std::max(4.0f, trackW * f), ty + 2), 2, withAlpha(on, 0.75f));
	fillCircle(cr, pointF(x + trackW * f, ty), 10, withAlpha(on, 0.14f));
	fillCircle(cr, pointF(x + trackW * f, ty), 6, colorF(0.92f, 1, 1));
	x += trackW + 12;
	drawText(cr, format("%d ms / step", cl_document_step_ms(win->document())), rectF(x, ty - 8, x + 84, ty + 8), 11, ink);
	x += 84 + 14;
	divider();

	// The Done button at the right end; the chips in what's left.
	const float doneW = 70;
	const RectF done = rectF(x0 + barW - 18 - doneW, cy - 15, x0 + barW - 18, cy + 15);
	const bool doneHot = hotHit == (int)hits.size();
	fillRound(cr, done, 9, colorF(1, 1, 1, doneHot ? 0.12f : 0.07f));
	strokeRound(cr, done, 9, colorF(1, 1, 1, 0.10f));
	drawText(cr, "Done", rectF(done.left + 13, cy - 8, done.left + 46, cy + 10), 12, ink);
	drawText(cr, "esc", rectF(done.left + 44, cy - 6, done.right, cy + 8), 9, dim);
	hits.push_back({ done.left, done.top, done.right, done.bottom, "win.sim-view" });

	CLSimChip chips[64];
	const int n = std::min(64, cl_simview_chips(win->document(), page(), chips, 64));
	std::vector<CLSimChip> ins, outs;
	for (int i = 0; i < n; i++) (chips[i].isInput ? ins : outs).push_back(chips[i]);
	const float room = done.left - 14 - x;
	auto rowWidth = [](size_t count) { return count ? 22.0f + 15.0f * count : 0.0f; };
	size_t cap = 24;
	while (cap > 0 && rowWidth(std::min(cap, ins.size())) + rowWidth(std::min(cap, outs.size())) + 14 > room) cap /= 2;
	if (cap == 0) return;
	auto row = [&](const char* label, const std::vector<CLSimChip>& list) {
		if (list.empty()) return;
		drawText(cr, label, rectF(x, cy - 7, x + 24, cy + 8), 9, dim);
		x += 22;
		for (size_t i = 0; i < std::min(cap, list.size()); i++) {
			const RectF c = rectF(x, cy - 5, x + 10, cy + 5);
			if (list[i].lit) fillRound(cr, rectF(c.left - 3, c.top - 3, c.right + 3, c.bottom + 3), 5, withAlpha(on, 0.16f));
			fillRound(cr, c, 3, list[i].lit ? on : colorF(1, 1, 1, 0.10f));
			x += 15;
		}
		x += 14;
	};
	row("IN", ins);
	row("OUT", outs);
}

void Canvas::setSpeedAt(double x) {
	const float f = (float)((x - sliderLeft) / std::max(1.0f, sliderRight - sliderLeft));
	win->setStepMs(speedFromFraction(f));
	redraw();
}

// A pill over the top of the canvas: Tidy Up's preview, Simulation View,
// Lock, with their buttons.
void Canvas::drawBanner(cairo_t* cr, float w) {
	std::string text;
	std::vector<CircuitWindow::BannerButton> buttons;
	const bool has = win->bannerFor(text, buttons);
	if (has) {
		bannerText = text;
		bannerButtons.clear();
		for (const auto& b : buttons) bannerButtons.push_back({ b.label, b.action });
		bannerLocked = win->locked();
	}
	// It drops in and fades, and fades away.
	bannerFade.go(has ? 1 : 0, has ? 0.22 : 0.16);
	if (bannerFade.active()) keepMoving();
	const float a = (float)bannerFade.value();
	if (a < 0.01f || bannerText.empty()) return;
	const bool sim = win->simView();
	const Chrome c{ prefs().dark || sim };
	const Color ink = c.barInk();
	std::vector<float> bw;
	float total = 16 + textWidth(bannerText, 12) + 12;
	for (const auto& b : bannerButtons) { bw.push_back(textWidth(b.first, 12, true) + 22); total += bw.back() + 6; }
	total += 6;
	if (bannerLocked) total += 20;
	const float x0 = std::max(8.0f, (w - total) / 2), y0 = 12 - 10 * (1 - a), hgt = 38;
	const RectF pill = rectF(x0, y0, x0 + total, y0 + hgt);
	cairo_save(cr);
	cairo_push_group(cr);
	fillRound(cr, rectF(pill.left, pill.top + 2, pill.right, pill.bottom + 3), hgt / 2, colorF(0, 0, 0, c.dark ? 0.35f : 0.10f));
	// Glass: the circuit under it, blurred, and a wash of the bar's colour.
	frosted(cr, pill, hgt / 2);
	fillRound(cr, pill, hgt / 2, c.dark ? rgb255(40, 43, 50, 0.78f) : colorF(1, 1, 1, 0.74f));
	strokeRound(cr, pill, hgt / 2, withAlpha(ink, c.dark ? 0.14f : 0.10f));
	fillRect(cr, rectF(pill.left + hgt / 2, pill.top + 1, pill.right - hgt / 2, pill.top + 2), colorF(1, 1, 1, c.dark ? 0.08f : 0.7f));
	float x = x0 + 16;
	const float ty = y0 + hgt / 2 - 8;
	if (bannerLocked) {
		drawIcon(cr, Icon::Lock, rectF(x - 2, y0, x + 16, y0 + hgt), 13, withAlpha(ink, 0.8f));
		x += 20;
	}
	drawText(cr, bannerText, rectF(x, ty, x + textWidth(bannerText, 12) + 2, ty + 18), 12, ink);
	x += textWidth(bannerText, 12) + 12;
	for (size_t i = 0; i < bannerButtons.size(); i++) {
		const RectF r = rectF(x, y0 + 6, x + bw[i], y0 + hgt - 6);
		const bool hot = hotHit == (int)hits.size();
		const bool primary = i == 0;
		fillRound(cr, r, 7, primary ? withAlpha(c.accent(), hot ? 1.0f : 0.9f) : withAlpha(ink, hot ? 0.14f : 0.08f));
		drawText(cr, bannerButtons[i].first, rectF(r.left, ty, r.right, ty + 18), 12, primary ? c.onAccent() : ink, TextAlign::Center, primary);
		if (has) hits.push_back({ r.left, r.top, r.right, r.bottom, bannerButtons[i].second });
		x += bw[i] + 6;
	}
	cairo_pop_group_to_source(cr);
	cairo_paint_with_alpha(cr, a);
	cairo_restore(cr);
}

// Redrawn every frame while something on it is moving.
void Canvas::keepMoving() {
	if (motionTick) return;
	motionTick = gtk_widget_add_tick_callback(area, [](GtkWidget* w, GdkFrameClock*, gpointer self) -> gboolean {
		Canvas* c = static_cast<Canvas*>(self);
		gtk_widget_queue_draw(w);
		if (c->bannerFade.active() || c->simBarIn.active()) return G_SOURCE_CONTINUE;
		c->motionTick = 0;
		return G_SOURCE_REMOVE;
	}, this, nullptr);
}

// Frosted glass: the frame under r shrunk small and drawn back large (a
// blur, done the cheap way), inside the panel's rounded shape.
void Canvas::frosted(cairo_t* cr, const RectF& r, float radius) {
	if (frame == nullptr) return;
	const float rw = r.right - r.left, rh = r.bottom - r.top;
	if (rw < 2 || rh < 2) return;
	const int sw = std::max(2, (int)(rw / 10)), sh = std::max(2, (int)(rh / 10));
	cairo_surface_t* small = cairo_image_surface_create(CAIRO_FORMAT_RGB24, sw, sh);
	cairo_t* sc = cairo_create(small);
	cairo_scale(sc, sw / rw, sh / rh);
	cairo_set_source_surface(sc, frame, -r.left, -r.top);
	cairo_pattern_set_filter(cairo_get_source(sc), CAIRO_FILTER_GOOD);
	cairo_paint(sc);
	cairo_destroy(sc);
	cairo_save(cr);
	roundedPath(cr, r, radius);
	cairo_clip(cr);
	cairo_translate(cr, r.left, r.top);
	cairo_scale(cr, rw / sw, rh / sh);
	cairo_set_source_surface(cr, small, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
	cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
	cairo_paint(cr);
	cairo_restore(cr);
	cairo_surface_destroy(small);
}

// A note (Saved, Copied...) over the bottom of the canvas, as a dark pill
// that fades.
void Canvas::drawToast(cairo_t* cr, float w, float h) {
	std::string text;
	double alpha = 0;
	if (!win->toast(text, alpha)) return;
	const float a = (float)alpha;
	const float tw = std::min(w - 40, textWidth(text, 12) + 32), th = 32;
	const float bottom = h - 16 - (win->simView() ? 52 + 14 : 0);
	const RectF r = rectF((w - tw) / 2, bottom - th, (w + tw) / 2, bottom);
	const bool dark = prefs().dark || win->simView();
	fillRound(cr, rectF(r.left, r.top + 2, r.right, r.bottom + 2), th / 2, colorF(0, 0, 0, 0.18f * a));
	fillRound(cr, r, th / 2, dark ? colorF(0.24f, 0.26f, 0.30f, 0.96f * a) : colorF(0.13f, 0.14f, 0.16f, 0.92f * a));
	drawText(cr, text, rectF(r.left + 14, r.top + 7, r.right - 14, r.bottom), 12, colorF(1, 1, 1, a), TextAlign::Center);
}

bool Canvas::overlayPress(double x, double y) {
	if (sliderRight > sliderLeft && x >= sliderLeft && x < sliderRight && y >= sliderTop && y < sliderBottom) {
		sliderDragging = true;
		setSpeedAt(x);
		return true;
	}
	for (const OverlayHit& h : hits) {
		if (x < h.left || x >= h.right || y < h.top || y >= h.bottom) continue;
		win->runAction(h.action);
		redraw();
		return true;
	}
	return false;
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

// ---- What a wire carries -------------------------------------------------------------

bool Canvas::wireTagText(std::string& text, char& state) const {
	const int p = page();
	if (p < 0) return false;
	char buf[72] = "";
	const int bits = cl_edit_hover_wire_state(win->document(), p, buf, sizeof buf);
	if (bits <= 0) return false;
	const std::string s = buf;
	state = s.find('!') != std::string::npos ? '!' : s.find('X') != std::string::npos ? 'X' : s.find('Z') != std::string::npos ? 'Z'
	        : bits == 1 ? (s.empty() ? '0' : s[0]) : 'b';
	if (bits == 1) {
		switch (state) {
		case '1': text = "1"; break;
		case '0': text = "0"; break;
		case 'Z': text = "Z \u00B7 floating (nothing drives it)"; break;
		case '!': text = "! \u00B7 conflict (outputs disagree)"; break;
		default: text = "X \u00B7 unknown"; break;
		}
	} else if (state == 'b') {
		text = s + " = " + std::to_string(std::stoll(s, nullptr, 2));
	} else {
		text = s;
	}
	return true;
}

void Canvas::hideWireTag() {
	if (tagTimer) { g_source_remove(tagTimer); tagTimer = 0; }
	if (tagShown) { tagShown = false; redraw(); }
}

void Canvas::updateWireTag() {
	std::string text;
	char state = 0;
	if (!prefs().wireValueTag || !wireTagText(text, state)) { hideWireTag(); return; }
	if (tagShown) { redraw(); return; }
	if (tagTimer) return;
	tagTimer = g_timeout_add(win->simView() ? 600 : 1100, [](gpointer self) -> gboolean {
		Canvas* c = static_cast<Canvas*>(self);
		c->tagTimer = 0;
		std::string t;
		char st;
		if (c->pointerInside && c->wireTagText(t, st)) { c->tagShown = true; c->redraw(); }
		return G_SOURCE_REMOVE;
	}, this);
}

// A small chip beside the pointer: green for 1, grey for 0, blue for
// floating, red for a conflict, orange for unknown.
void Canvas::drawWireTag(cairo_t* cr, float w, float h) {
	std::string text;
	char state = 0;
	if (!tagShown || !wireTagText(text, state)) return;
	Color c = state == '1' ? colorF(0.13f, 0.68f, 0.3f) : state == '0' ? colorF(0.42f, 0.42f, 0.42f) : state == 'Z' ? colorF(0.2f, 0.47f, 0.96f)
	        : state == '!' ? colorF(0.92f, 0.26f, 0.24f) : state == 'X' ? colorF(0.96f, 0.58f, 0.13f) : colorF(0.25f, 0.25f, 0.25f);
	const float tw = faceWidth(text, "Monospace", 12, true);
	RectF r = rectF((float)lastX + 14, (float)lastY - 30, (float)lastX + 14 + tw + 14, (float)lastY - 30 + 20);
	if (r.right > w - 4) { const float rw = r.right - r.left; r.left = (float)lastX - 14 - rw; r.right = r.left + rw; }
	if (r.top < 4) { r.top = (float)lastY + 14; r.bottom = r.top + 20; }
	(void)h;
	fillRound(cr, rectF(r.left, r.top + 1, r.right, r.bottom + 2), 10, colorF(0, 0, 0, 0.25f));
	fillRound(cr, r, 10, c);
	drawFace(cr, text, r.left + 7, r.top + 2, "Monospace", 12, colorF(1, 1, 1), true);
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
	if (e->type == GDK_LEAVE_NOTIFY) static_cast<Canvas*>(self)->hideWireTag();
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
	hideWireTag();
	win->activatePane(win->paneOf(this));
	zooming = false;
	lastX = e->x;
	lastY = e->y;
	// The banner's buttons first.
	if (e->button == 1 && e->type == GDK_BUTTON_PRESS && overlayPress(e->x, e->y)) return TRUE;
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
	if (sliderDragging) { setSpeedAt(e->x); return TRUE; }
	// Hover over the banner's buttons.
	int h = -1;
	for (size_t i = 0; i < hits.size(); i++)
		if (e->x >= hits[i].left && e->x < hits[i].right && e->y >= hits[i].top && e->y < hits[i].bottom) h = (int)i;
	if (h != hotHit) { hotHit = h; redraw(); }
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
		if (win->simView()) {
			if (prefs().wireValueTag && cl_edit_hover_wire(doc, p, wx, wy, upp)) redraw();
			updateWireTag();
			break;
		}
		if (cl_edit_hover(doc, p, wx, wy, upp)) redraw();
		updateWireTag();
		break;
	}
	return TRUE;
}

bool Canvas::onRelease(GdkEventButton* e) {
	if (sliderDragging) { sliderDragging = false; return TRUE; }
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
		else if (shortcuts::canvasAction(e) == "truthTable") win->makeTruthTable();
		else if (shortcuts::canvasAction(e) == "checkCircuit") win->checkCircuit();
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

	// The single-letter keys, as in the wx app (Settings > Shortcuts changes them).
	const std::string act = shortcuts::canvasAction(e);
	(void)lower;
	if (act == "quickCopy") {
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
	}
	if (act.empty()) return FALSE;
	if (act == "truthTable") { win->makeTruthTable(); return TRUE; }
	if (act == "checkCircuit") { win->checkCircuit(); return TRUE; }
	if (!win->canEdit()) { win->lockNudge(); return TRUE; }
	if (act == "quickPaste") win->paste();
	else if (act == "quickCut") win->cut();
	else if (act == "quickDuplicate") win->duplicate();
	else if (act == "addGate") win->quickAdd();
	else if (act == "rotate") win->rotate();
	else if (act == "straighten") win->straighten();
	else if (act == "tidy") win->tidy();
	else return FALSE;
	return TRUE;
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

gboolean Canvas::dragMotionCb(GtkWidget* widget, GdkDragContext* ctx, gint, gint, guint time, gpointer self) {
	Canvas* c = static_cast<Canvas*>(self);
	// Files open whatever the circuit's state; a gate needs one that can change.
	const GdkAtom target = gtk_drag_dest_find_target(widget, ctx, nullptr);
	const bool files = target == gdk_atom_intern_static_string("text/uri-list");
	if (target == GDK_NONE || (!files && !c->win->canEdit())) { gdk_drag_status(ctx, (GdkDragAction)0, time); return TRUE; }
	gdk_drag_status(ctx, GDK_ACTION_COPY, time);
	return TRUE;
}

void Canvas::dragReceivedCb(GtkWidget*, GdkDragContext* ctx, gint x, gint y, GtkSelectionData* data,
                            guint info, guint time, gpointer self) {
	Canvas* c = static_cast<Canvas*>(self);
	if (info == 2) {
		c->win->openDroppedFiles(data);
		gtk_drag_finish(ctx, TRUE, FALSE, time);
		return;
	}
	const guchar* raw = gtk_selection_data_get_data(data);
	const gint len = gtk_selection_data_get_length(data);
	bool ok = false;
	if (raw && len > 0 && c->win->canEdit()) guarded("dropping a gate", [&] {
		const std::string name((const char*)raw, (size_t)len);
		double wx, wy;
		c->worldPoint(x, y, wx, wy);
		// Same as clicking the tile: the gate lands floating, still following
		// the pointer until a click drops it -- so C-to-connect, Escape, and
		// every other in-flight key work exactly as they do after a click.
		if (c->win->addGateFloating(name, wx, wy)) {
			ok = true;
			gtk_widget_grab_focus(c->area);
		}
	});
	gtk_drag_finish(ctx, ok, FALSE, time);
}
