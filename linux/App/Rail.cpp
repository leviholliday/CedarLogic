// The Actions rail (see Rail.h).

#include "Rail.h"
#include "Intro.h"
#include "Settings.h"
#include "ShowMeAround.h"
#include "Welcome.h"
#include "Window.h"

#include <algorithm>
#include <cmath>

// ---- The dock and the floater ---------------------------------------------------------

class RailView : public Drawn {
public:
	RailView(Rail* r, bool floating) : floater(floating), rail(r) { create(); }
	bool floater;

protected:
	void paint(cairo_t* cr, float w, float h) override;
	void mouseMove(float x, float y) override;
	void mouseLeave() override;
	void mouseDown(int button, float x, float y, bool, GdkEventButton* e) override;
	void mouseUp(int button, float x, float y) override;
	bool animating() override { return floater && rail->slide.active(); }

private:
	friend class Rail;
	Rail* rail;
	// Drawn as the tab (folded, or peeking): a click opens it.
	bool showsTab() const { return floater ? rail->floaterState == 1 : rail->folded(); }
};

// The labels (and the slid-out rail's shadow), over the canvas at the rail's
// left. It takes no clicks (the overlay passes them through).
class RailLabels : public Drawn {
public:
	explicit RailLabels(Rail* r) : rail(r) { create(); }
	// The band an item's label is drawn in, to redraw only that.
	void redrawLabel(int i) {
		if (i < 0 || area == nullptr) return;
		const int cy = (int)(actions::kTop + i * actions::kPitch + actions::kItemH / 2);
		gtk_widget_queue_draw_area(area, 0, cy - 20, gtk_widget_get_allocated_width(area), 40);
	}
	void redrawShadow() {
		if (area == nullptr) return;
		const int w = gtk_widget_get_allocated_width(area);
		gtk_widget_queue_draw_area(area, std::max(0, w - 14), 0, 14, gtk_widget_get_allocated_height(area));
	}

protected:
	void paint(cairo_t* cr, float w, float h) override;

private:
	Rail* rail;
};

namespace {
const float kLabelLayer = 320;
const float kEdgeZone = 5;   // the rail's left edge, which folds it (or, slid out, keeps it)

// Only `x` and on (to the right edge) takes the pointer: presses, drags and
// the wheel anywhere else go through to what's under it (the canvas); an
// empty `width` takes none at all. (A widget with a window of its own, as a
// drawing area is, catches every press over it otherwise -- even in an
// overlay that passes input through, which only passes over its own window,
// not the widget's.) -1 takes the whole of it again.
void takesPointerFrom(GtkWidget* w, int x, int width) {
	if (width < 0) {
		gtk_widget_input_shape_combine_region(w, nullptr);
		return;
	}
	cairo_region_t* r = cairo_region_create();
	if (width > 0) {
		const cairo_rectangle_int_t keep = { x, 0, width, 1 << 20 };
		cairo_region_union_rectangle(r, &keep);
	}
	gtk_widget_input_shape_combine_region(w, r);
	cairo_region_destroy(r);
}
}  // namespace

// ---- Rail ----------------------------------------------------------------------------------

Rail::Rail(CircuitWindow* window) : win(window) {
	GtkWidget* top = GTK_WIDGET(win->window());
	gtk_widget_add_events(top, GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK);
	// Every motion over the window, before whatever is under the pointer
	// gets it (the hot strip lies over the canvas, the notes, the tab).
	motion = gtk_event_controller_motion_new(top);
	gtk_event_controller_set_propagation_phase(motion, GTK_PHASE_CAPTURE);
	g_signal_connect(motion, "motion", G_CALLBACK(motionCb), this);
	g_signal_connect(top, "leave-notify-event", G_CALLBACK(leaveCb), this);
	presses = gtk_gesture_multi_press_new(top);
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(presses), 0);
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(presses), GTK_PHASE_CAPTURE);
	g_signal_connect(presses, "pressed", G_CALLBACK(pressedCb), this);
	g_signal_connect(presses, "released", G_CALLBACK(releasedCb), this);
	g_signal_connect(presses, "cancel", CL_CALLBACK(+[](GtkGesture*, GdkEventSequence*, gpointer self) { static_cast<Rail*>(self)->buttonDown = false; }), this);
	items = actions::rail();
}

Rail::~Rail() {
	if (menu) g_signal_handlers_disconnect_by_data(menu, this);
	if (motion) {
		g_signal_handlers_disconnect_by_data(motion, this);
		g_object_unref(motion);
	}
	if (presses) {
		g_signal_handlers_disconnect_by_data(presses, this);
		g_object_unref(presses);
	}
	if (GtkWidget* top = GTK_WIDGET(win->window())) g_signal_handlers_disconnect_by_data(top, this);
	if (slideImage) cairo_surface_destroy(slideImage);
	delete dock;
	delete floater;
	delete labels;
}

GtkWidget* Rail::wrap(GtkWidget* row) {
	overlay = gtk_overlay_new();
	gtk_container_add(GTK_CONTAINER(overlay), row);
	labels = new RailLabels(this);
	GtkWidget* l = labels->widget();
	gtk_widget_set_halign(l, GTK_ALIGN_END);
	gtk_widget_set_valign(l, GTK_ALIGN_FILL);
	gtk_widget_set_margin_end(l, (int)actions::kRailWidth);
	gtk_widget_set_size_request(l, (int)kLabelLayer, -1);
	gtk_widget_set_has_tooltip(l, FALSE);
	gtk_widget_set_no_show_all(l, TRUE);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), l);
	gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(overlay), l, TRUE);
	// It's 320 points over the canvas, the whole height, left of the rail:
	// every press there is the canvas's (on the Raspberry Pi and Zorin a band
	// of the canvas took no presses or drags at all until it did).
	takesPointerFrom(l, 0, 0);
	floater = new RailView(this, true);
	GtkWidget* f = floater->widget();
	gtk_widget_set_halign(f, GTK_ALIGN_END);
	gtk_widget_set_valign(f, GTK_ALIGN_FILL);
	gtk_widget_set_no_show_all(f, TRUE);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), f);
	// The overlay's own window round it passes everything through; the
	// floater's takes what its input shape says (takesPointerFrom).
	gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(overlay), f, TRUE);
	return overlay;
}

void Rail::addDock(GtkWidget* row) {
	dock = new RailView(this, false);
	gtk_widget_set_no_show_all(dock->widget(), TRUE);
	gtk_box_pack_start(GTK_BOX(row), dock->widget(), FALSE, FALSE, 0);
	refresh();
}

GtkWidget* Rail::dockWidget() const { return dock ? dock->widget() : nullptr; }
GtkWidget* Rail::floaterWidget() const { return floater ? floater->widget() : nullptr; }

void Rail::itemCenter(int i, double& x, double& y) const {
	x = actions::kRailWidth / 2;
	y = actions::kTop + i * actions::kPitch + actions::kItemH / 2;
}

int Rail::topInset() const {
	if (!fold.focus) return 0;
	GtkWidget* strip = win->tabStripForTour();
	return strip && gtk_widget_get_mapped(strip) ? gtk_widget_get_allocated_height(strip) : 0;
}

std::string Rail::describe() const {
	return format("strip %d, rail %d, out %d, waiting %d, dismissed %d, folded %d, floater %d", hover.overStrip, hover.overRail, hover.slidOut,
	              hover.pending, hover.dismissed, folded(), floaterState);
}

bool Rail::onScreen() const { return prefs().actionsOn; }
bool Rail::hoverEnabled() const { return prefs().actionsHoverOpen; }
// Show Me Around opens it for its stop (and it folds back after).
bool Rail::folded() const { return fold.folded(prefs().actionsCollapsed || pictureFold) && !showme::holdsPanelOpen(win); }
bool Rail::shownOnScreen() const { return onScreen() && (!folded() || hover.slidOut); }

void Rail::refresh() {
	items = actions::rail();
	if (hovered >= (int)items.size()) hovered = -1;
	if (pressed >= (int)items.size()) { pressed = -1; dragging = false; held = false; hover.dragging = false; }
	apply();
}

void Rail::apply() {
	if (dock == nullptr) return;
	const bool on = onScreen();
	const bool f = folded();
	gtk_widget_set_visible(dock->widget(), on);
	gtk_widget_set_visible(labels->widget(), on);
	const int want = (int)(f ? actions::kTabWidth : actions::kRailWidth);
	int cur = -1;
	gtk_widget_get_size_request(dock->widget(), &cur, nullptr);
	if (cur != want) gtk_widget_set_size_request(dock->widget(), want, -1);
	// Open, put away, or not to slide: nothing slid out or waiting.
	if (!on || !f || !hoverEnabled()) {
		if (hover.slidOut && !pictureSlid) { hover.close(); slide.set(0); }
		hover.pending = 0;
	}
	if (!f) edgeHot = edgeHot && on;
	// The tips: the tab, and the rail's edge.
	std::vector<RailView::Tip> tips;
	if (f) tips.push_back({ rectF(0, 0, actions::kTabWidth, 1e6f), "Show Actions" });
	else tips.push_back({ rectF(0, 0, kEdgeZone, 1e6f), "Fold Actions away" });
	dock->setTips(tips);
	tips.clear();
	tips.push_back({ rectF(0, 0, floaterState == 2 ? kEdgeZone : actions::kPeekWidth, 1e6f), floaterState == 2 ? "Keep Actions open" : "Show Actions" });
	floater->setTips(tips);
	updateFloater();
	redrawAll();
}

void Rail::redrawAll() {
	if (dock) dock->redraw();
	if (floater) floater->redraw();
	if (labels) labels->redraw();
}

void Rail::updateFloater() {
	if (floater == nullptr) return;
	int want = 0;
	if (onScreen() && folded()) {
		if (hover.slidOut || pictureSlid || slide.active() || slide.value() > 0.001) want = 2;
		else if (hover.peek() || picturePeek) want = 1;
	}
	if (want == floaterState) return;
	const int was = floaterState;
	floaterState = want;
	GtkWidget* f = floater->widget();
	if (want == 0) {
		gtk_widget_hide(f);
	} else {
		gtk_widget_set_size_request(f, (int)(want == 2 ? actions::kRailWidth : actions::kPeekWidth), -1);
		// Peeking, it's drawn a few points over the canvas, but only the tab's
		// own width (over the dock's tab) takes clicks: the hot strip is the
		// canvas's for presses, drags and the wheel, as the Mac's is. Slid out,
		// the rail is the rail.
		if (want == 1) takesPointerFrom(f, (int)(actions::kPeekWidth - actions::kTabWidth), (int)actions::kTabWidth);
		else takesPointerFrom(f, 0, -1);
		gtk_widget_show(f);
	}
	if (was == 2 || want == 2) labels->redrawShadow();
	if (want != 2 && hovered >= 0 && folded()) setHovered(-1);
	dock->redraw();
	floater->redraw();
	std::vector<RailView::Tip> tips;
	tips.push_back({ rectF(0, 0, want == 2 ? kEdgeZone : actions::kPeekWidth, 1e6f), want == 2 ? "Keep Actions open" : "Show Actions" });
	floater->setTips(tips);
}

void Rail::startSlide(bool out) {
	const bool smooth = anim::enabled() && !win->slowFrames();
	if (slideImage) { cairo_surface_destroy(slideImage); slideImage = nullptr; }
	slide.go(out ? 1 : 0, smooth ? 0.18 : 0);
	if (!out) setHovered(-1);
	updateFloater();
	if (floater) floater->animate();
	labels->redrawShadow();
	win->updateActions();   // View's Show or Hide Actions
}

void Rail::unfold() {
	bool c = prefs().actionsCollapsed;
	fold.unfold(c);
	if (hover.slidOut) { hover.close(); slide.set(0); }
	if (c != prefs().actionsCollapsed) { prefs().actionsCollapsed = c; actions::changed(); }
	else { apply(); win->updateActions(); }
}

void Rail::foldAway() {
	bool c = prefs().actionsCollapsed;
	fold.fold(c);
	setHovered(-1);
	edgeHot = false;
	if (c != prefs().actionsCollapsed) { prefs().actionsCollapsed = c; actions::changed(); }
	else { apply(); win->updateActions(); }
}

void Rail::setFocus(bool on) {
	fold.setFocus(on);
	if (hover.slidOut) { hover.close(); slide.set(0); }
	apply();
}

void Rail::toggle() {
	if (!onScreen()) {
		prefs().actionsCollapsed = false;
		prefs().actionsOn = true;
		actions::changed();
		return;
	}
	if (hover.slidOut) {
		hover.dismissed = hover.overStrip || hover.overRail;
		hover.close();
		startSlide(false);
	} else if (folded()) {
		unfold();
	} else {
		foldAway();
	}
}

void Rail::showOpen() {
	prefs().actionsOn = true;
	prefs().actionsCollapsed = false;
	if (fold.focus || fold.narrow) fold.peek = true;
	actions::changed();
}

bool Rail::escape() {
	if (pictureSlid || !hover.escape()) return false;
	startSlide(false);
	return true;
}

void Rail::tick() {
	const double now = anim::now();
	const bool f = folded() && onScreen();
	const int r = hover.tick(now, f, hoverEnabled());
	if (r == 1) startSlide(true);
	else if (r == -1) startSlide(false);
	// Held long enough: picked up, and any movement drags it.
	if (pressed >= 0 && !dragging && !held && now - pressAt >= actions::kHoldTime) {
		held = true;
		redrawAll();
	}
	// Focus mode's title row: kept clear.
	const int in = topInset();
	if (in != inset && floater && labels) {
		inset = in;
		gtk_widget_set_margin_top(floater->widget(), in);
		gtk_widget_set_margin_top(labels->widget(), in);
	}
	// A narrow window folds it.
	if (overlay) {
		const int w = gtk_widget_get_allocated_width(overlay);
		if (w > 1 && (w < actions::kNarrowWidth) != fold.narrow) {
			fold.setNarrow(w < actions::kNarrowWidth);
			apply();
			win->updateActions();
		}
	}
	// Slid back in: the floater goes (or peeks).
	if (floaterState == 2 && !slide.active() && !hover.slidOut && !pictureSlid) updateFloater();
	if (!slide.active() && slideImage) { cairo_surface_destroy(slideImage); slideImage = nullptr; }
}

// ---- The pointer at the window's edge --------------------------------------------------------

void Rail::motionCb(GtkEventControllerMotion*, double, double, gpointer self) {
	Rail* r = static_cast<Rail*>(self);
	if (r->overlay == nullptr || !gtk_widget_get_realized(r->overlay)) return;
	GdkEvent* ev = gtk_get_current_event();
	if (ev == nullptr) return;
	double x = 0, y = 0;
	GdkWindow* gw = ev->any.window;
	const bool coords = gw && gdk_event_get_coords(ev, &x, &y);
	// Up from the window the event came to, to the circuit window's own.
	GdkWindow* top = gtk_widget_get_window(GTK_WIDGET(r->win->window()));
	while (coords && gw && gw != top) {
		double px = 0, py = 0;
		gdk_window_coords_to_parent(gw, x, y, &px, &py);
		x = px;
		y = py;
		gw = gdk_window_get_effective_parent(gw);
	}
	GdkModifierType state = (GdkModifierType)0;
	gdk_event_get_state(ev, &state);
	gdk_event_free(ev);
	if (!coords || gw != top) return;
	// A button held (a drag on the canvas, a box being drawn): the edge
	// neither peeks nor slides the rail out over it, as the Mac's tracking
	// doesn't during a drag -- unless it's a drag on the rail itself.
	if (!(state & (GDK_BUTTON1_MASK | GDK_BUTTON2_MASK | GDK_BUTTON3_MASK))) r->buttonDown = false;
	if (r->buttonDown && r->pressed < 0) {
		// (Out already, it stays as it was until the button's let go.)
		if (!r->hover.slidOut) guarded("the rail's edge", [&] { r->pointerGone(); });
		return;
	}
	int ox = 0, oy = 0;
	if (!gtk_widget_translate_coordinates(r->overlay, GTK_WIDGET(r->win->window()), 0, 0, &ox, &oy)) return;
	guarded("the rail's edge", [&] { r->pointerAt(x - ox, y - oy); });
}

void Rail::pressedCb(GtkGestureMultiPress*, int, double, double, gpointer self) {
	Rail* r = static_cast<Rail*>(self);
	// A press on the rail or its tab is the rail's own (it opens, folds, runs an action).
	GdkEvent* ev = gtk_get_current_event();
	GtkWidget* on = ev ? gtk_get_event_widget(ev) : nullptr;
	if (ev) gdk_event_free(ev);
	if ((r->dock && on == r->dock->widget()) || (r->floater && on == r->floater->widget())) return;
	r->buttonDown = true;
	// About to slide out (or peeking) over a press on the canvas there: not now.
	if (!r->hover.slidOut) guarded("the rail's edge", [&] { r->pointerGone(); });
}

void Rail::releasedCb(GtkGestureMultiPress*, int, double, double, gpointer self) { static_cast<Rail*>(self)->buttonDown = false; }

gboolean Rail::leaveCb(GtkWidget* w, GdkEventCrossing* e, gpointer self) {
	Rail* r = static_cast<Rail*>(self);
	// Out of the window altogether (not into a part of it, or a menu's grab).
	if (e->window == gtk_widget_get_window(w) && e->detail != GDK_NOTIFY_INFERIOR && e->mode == GDK_CROSSING_NORMAL)
		guarded("the rail's edge", [&] { r->pointerGone(); });
	return FALSE;
}

void Rail::pointerAt(double x, double y) {
	if (pictureSlid || picturePeek) return;   // a picture's state stays as it was set
	const double W = gtk_widget_get_allocated_width(overlay), H = gtk_widget_get_allocated_height(overlay) - inset;
	y -= inset;
	const double now = anim::now();
	const bool f = folded() && onScreen();
	hover.setStrip(f && actions::inHotStrip(x, y, W, H), now, f, hoverEnabled());
	hover.setRail(f && hover.slidOut && actions::onSlidRail(x, y, W, H), now, f, hoverEnabled());
	if (hover.peek() != peekShown) {
		peekShown = hover.peek();
		updateFloater();
		dock->redraw();
	}
}

void Rail::pointerGone() {
	if (pictureSlid || picturePeek) return;
	const double now = anim::now();
	const bool f = folded() && onScreen();
	hover.setStrip(false, now, f, hoverEnabled());
	hover.setRail(false, now, f, hoverEnabled());
	if (peekShown) {
		peekShown = false;
		updateFloater();
		dock->redraw();
	}
}

// ---- Items, a press and a drag -------------------------------------------------------------

int Rail::itemAt(float x, float y) const {
	if (x < kEdgeZone || y < actions::kTop) return -1;
	const int i = (int)std::floor((y - actions::kTop) / actions::kPitch);
	return i >= 0 && i < (int)items.size() ? i : -1;
}

void Rail::setHovered(int i) {
	if (i == hovered) return;
	const int was = hovered;
	hovered = i;
	labels->redrawLabel(was);
	labels->redrawLabel(i);
	dock->redraw();
	floater->redraw();
}

int Rail::target() const {
	const int from = dragging ? pressed : pictureDrag;
	const double dy = dragging ? dragDy : pictureDy;
	const int n = (int)items.size();
	return std::min(std::max(from + (int)std::lround(dy / actions::kPitch), 0), std::max(n - 1, 0));
}

double Rail::shiftOf(int i) const {
	const int from = dragging ? pressed : pictureDrag;
	if (from < 0) return 0;
	const double dy = dragging ? dragDy : pictureDy;
	const int n = (int)items.size();
	if (i == from) return std::min(std::max(dy, -from * (double)actions::kPitch - 4), (n - 1 - from) * (double)actions::kPitch + 4);
	const int to = target();
	if (from < to && i > from && i <= to) return -actions::kPitch;
	if (to < from && i >= to && i < from) return actions::kPitch;
	return 0;
}

void RailView::mouseMove(float x, float y) {
	Rail* r = rail;
	if (r->pressed >= 0 && r->pressedOnFloater == floater) {
		const double dx = x - r->pressX, dy = y - r->pressY;
		if (!r->dragging && std::hypot(dx, dy) >= (r->held ? 1 : actions::kDragStart)) {
			r->dragging = true;
			r->hover.dragging = true;
			r->setHovered(-1);
		}
		if (r->dragging) {
			r->dragDy = dy;
			redraw();
		}
		return;
	}
	if (showsTab()) return;
	const bool edge = x < kEdgeZone;
	if (edge != r->edgeHot) { r->edgeHot = edge; redraw(); }
	r->setHovered(edge ? -1 : r->itemAt(x, y));
}

void RailView::mouseLeave() {
	Rail* r = rail;
	if (r->pressed >= 0) return;
	if (r->edgeHot) { r->edgeHot = false; redraw(); }
	r->setHovered(-1);
}

void RailView::mouseDown(int button, float x, float y, bool, GdkEventButton* e) {
	Rail* r = rail;
	if (button == 3) { r->showMenu(e); return; }
	if (button != 1) return;
	if (showsTab()) { r->unfold(); return; }
	if (x < kEdgeZone) {
		if (floater) r->unfold();
		else r->foldAway();
		return;
	}
	const int i = r->itemAt(x, y);
	if (i < 0) return;
	r->pressed = i;
	r->pressedOnFloater = floater;
	r->pressX = x;
	r->pressY = y;
	r->pressAt = anim::now();
	r->held = false;
	r->dragging = false;
	r->dragDy = 0;
}

void RailView::mouseUp(int button, float x, float y) {
	Rail* r = rail;
	if (button != 1 || r->pressed < 0) return;
	const int i = r->pressed;
	if (r->dragging) {
		const int to = r->target();
		const std::string id = r->items[(size_t)i]->id;
		r->pressed = -1;
		r->dragging = false;
		r->held = false;
		r->hover.dragging = false;
		actions::moveRailItem(id, to);
		// Dragged out of a slid-out rail: it goes back now, if the pointer has.
		r->hover.changed(anim::now(), r->folded() && r->onScreen(), r->hoverEnabled());
		r->redrawAll();
		return;
	}
	const bool wasHeld = r->held;
	r->pressed = -1;
	r->held = false;
	redraw();
	// Held to move but not moved: nothing runs.
	if (!wasHeld && r->itemAt(x, y) == i && i < (int)r->items.size()) actions::run(*r->items[(size_t)i], r->win);
}

// ---- The right-click menu ----------------------------------------------------------------------

namespace {
void railMenuItem(GtkWidget* menu, const char* label, void (*act)(Rail*), Rail* r) {
	GtkWidget* item = gtk_menu_item_new_with_mnemonic(label);
	g_object_set_data(G_OBJECT(item), "cl-act", (gpointer)act);
	g_signal_connect(item, "activate", CL_CALLBACK(+[](GtkMenuItem* it, gpointer rail) {
		auto act = reinterpret_cast<void (*)(Rail*)>(g_object_get_data(G_OBJECT(it), "cl-act"));
		guarded("the rail's menu", [&] { act(static_cast<Rail*>(rail)); });
	}), r);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}
}  // namespace

void Rail::showMenu(GdkEventButton* e) {
	if (menu) return;
	GtkWidget* m = gtk_menu_new();
	railMenuItem(m, "_Customize Actions…", [](Rail* r) { actions::showCustomize(r->win); }, this);
	gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());
	const char* foldLabel = folded() ? (hover.slidOut ? "_Keep Actions Open" : "_Show Actions") : "_Fold Actions Away";
	railMenuItem(m, foldLabel, [](Rail* r) { if (r->folded()) r->unfold(); else r->foldAway(); }, this);
	railMenuItem(m, "_Turn Off the Actions Panel", [](Rail*) {
		prefs().actionsOn = false;
		actions::changed();
	}, this);
	gtk_widget_show_all(m);
	menu = m;
	// Its menu open keeps a slid-out rail out; closed, it goes if the pointer has.
	hover.menuOpen = hover.slidOut;
	hover.pending = 0;
	gtk_menu_attach_to_widget(GTK_MENU(m), GTK_WIDGET(win->window()), nullptr);
	g_signal_connect(m, "deactivate", CL_CALLBACK(+[](GtkMenuShell* mm, gpointer self) {
		Rail* r = static_cast<Rail*>(self);
		g_signal_handlers_disconnect_by_data(mm, r);
		r->menu = nullptr;
		r->hover.menuOpen = false;
		r->hover.changed(anim::now(), r->folded() && r->onScreen(), r->hoverEnabled());
		g_idle_add([](gpointer w) -> gboolean { gtk_widget_destroy(GTK_WIDGET(w)); return G_SOURCE_REMOVE; }, mm);
	}), this);
	gtk_menu_popup_at_pointer(GTK_MENU(m), (GdkEvent*)e);
}

// ---- Drawing -----------------------------------------------------------------------------------

void Rail::paintTab(cairo_t* cr, float w, float h, bool lit) {
	const Chrome c = chrome();
	const Color accent = c.accent();
	const bool focus = fold.focus;
	fillRect(cr, rectF(0, 0, w, h), lit ? withAlpha(accent, 0.35f) : focus ? c.canvas() : c.tabBar());
	if (lit) fillRect(cr, rectF(0, 0, 1, h), withAlpha(accent, 0.7f));
	else if (!focus) fillRect(cr, rectF(0, 0, 1, h), c.sash());
	const float gw = lit ? 3 : 2, gh = lit ? 40 : 32;
	const float cx = (w + 1) / 2;
	fillRound(cr, rectF(cx - gw / 2, h / 2 - gh / 2, cx + gw / 2, h / 2 + gh / 2), gw / 2,
	          lit ? accent : withAlpha(c.barInk(), focus ? 0.22f : 0.45f));
}

void Rail::paintRail(cairo_t* cr, float w, float h, bool slid) {
	const Chrome c = chrome();
	const Color accent = c.accent();
	fillRect(cr, rectF(0, 0, w, h), c.canvas());
	const int n = (int)items.size();
	const int from = dragging ? pressed : pictureDrag;
	const bool draggingNow = from >= 0 && from < n;
	// Where the dragged one will go: a dashed space the others open up.
	if (draggingNow) {
		const float top = actions::kTop + target() * actions::kPitch;
		const RectF gap = rectF(6, top, 6 + actions::kItemW, top + actions::kItemH);
		fillRound(cr, gap, 6, withAlpha(accent, 0.07f));
		cairo_save(cr);
		const double dash[] = { 3, 3 };
		cairo_set_dash(cr, dash, 2, 0);
		strokeRound(cr, rectF(gap.left + 0.75f, gap.top + 0.75f, gap.right - 0.75f, gap.bottom - 0.75f), 6, withAlpha(accent, 0.6f), 1.5f);
		cairo_restore(cr);
	}
	auto drawItem = [&](int i) {
		const actions::Item& a = *items[(size_t)i];
		const bool en = actions::enabled(a, win);
		const bool on = actions::isOn(a, win);
		const bool lifted = (draggingNow && i == from) || (held && pressed == i);
		const bool lit = (hovered == i && en && !draggingNow) || lifted;
		const float top = actions::kTop + i * actions::kPitch + (float)shiftOf(i);
		const RectF r = rectF(6, top, 6 + actions::kItemW, top + actions::kItemH);
		cairo_save(cr);
		if (lifted) {
			const float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
			cairo_translate(cr, cx, cy);
			cairo_scale(cr, 1.08, 1.08);
			cairo_translate(cr, -cx, -cy);
			const float sa = c.dark ? 0.22f : 0.09f;
			fillRound(cr, rectF(r.left - 1, r.top + 1, r.right + 1, r.bottom + 4), 7, colorF(0, 0, 0, sa));
			fillRound(cr, rectF(r.left, r.top + 1, r.right, r.bottom + 2), 6, colorF(0, 0, 0, sa));
			fillRound(cr, r, 6, c.tabCard());
		}
		if (on) fillRound(cr, r, 6, withAlpha(accent, lit ? 0.24f : 0.18f));
		else if (lit) fillRound(cr, r, 6, withAlpha(accent, 0.10f));
		if (lit) strokeRound(cr, rectF(r.left + 0.75f, r.top + 0.75f, r.right - 0.75f, r.bottom - 0.75f), 6, withAlpha(accent, 0.9f), 1.5f);
		const Color ink = on ? accent : withAlpha(c.barInk(), en ? 0.92f : 0.3f);
		actions::drawIcon(cr, a, r, 16, ink);
		cairo_restore(cr);
	};
	for (int i = 0; i < n; i++) if (!(draggingNow && i == from)) drawItem(i);
	if (draggingNow) drawItem(from);
	// The edge toward the canvas: the side panel's sash, which folds it
	// (slid out, keeps it open).
	if (edgeHot) {
		fillRect(cr, rectF(0, 0, 2, h), withAlpha(accent, 0.6f));
		const RectF tab = rectF(1, h / 2 - 15, 11, h / 2 + 15);
		fillRound(cr, tab, 5, c.tabCard());
		strokeRound(cr, rectF(tab.left + 0.5f, tab.top + 0.5f, tab.right - 0.5f, tab.bottom - 0.5f), 5, withAlpha(accent, 0.5f));
		const float mx = (tab.left + tab.right) / 2, my = h / 2, d = slid ? -1.5f : 1.5f;
		cairo_save(cr);
		setColor(cr, accent);
		cairo_set_line_width(cr, 1.6);
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
		cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
		cairo_move_to(cr, mx - d, my - 5);
		cairo_line_to(cr, mx + d, my);
		cairo_line_to(cr, mx - d, my + 5);
		cairo_stroke(cr);
		cairo_restore(cr);
	} else {
		fillRect(cr, rectF(0, 0, 1, h), c.sash());
	}
}

void RailView::paint(cairo_t* cr, float w, float h) {
	Rail* r = rail;
	if (!floater) {
		if (r->folded()) r->paintTab(cr, w, h, r->peekShown || r->picturePeek);
		else r->paintRail(cr, w, h, false);
		return;
	}
	if (r->floaterState == 1) { r->paintTab(cr, w, h, true); return; }
	if (r->floaterState != 2) return;
	const double t = r->pictureSlid ? 1 : r->slide.value();
	const float dx = (float)((1 - t) * actions::kRailWidth);
	if (!r->slide.active()) {
		if (dx > 0.5f) cairo_translate(cr, dx, 0);
		r->paintRail(cr, w, h, true);
		return;
	}
	// Sliding: the rail drawn once, moved each frame.
	if (r->slideImage == nullptr) {
		const int scale = std::max(1, gtk_widget_get_scale_factor(area));
		GdkWindow* gw = gtk_widget_get_window(area);
		r->slideImage = gw ? gdk_window_create_similar_image_surface(gw, CAIRO_FORMAT_ARGB32, (int)std::ceil(w) * scale, (int)std::ceil(h) * scale, scale)
		                   : nullptr;
		if (r->slideImage) {
			cairo_t* c = cairo_create(r->slideImage);
			r->paintRail(c, w, h, true);
			cairo_destroy(c);
		}
	}
	if (r->slideImage) {
		cairo_set_source_surface(cr, r->slideImage, dx, 0);
		cairo_paint(cr);
	}
}

void RailLabels::paint(cairo_t* cr, float w, float h) {
	Rail* r = rail;
	const Chrome c = chrome();
	// Out over the canvas, a soft shadow on it.
	if (r->floaterState == 2 && !r->slide.active() && (r->hover.slidOut || r->pictureSlid)) {
		cairo_pattern_t* g = cairo_pattern_create_linear(w - 10, 0, w, 0);
		const double a = c.dark ? 0.4 : 0.14;
		cairo_pattern_add_color_stop_rgba(g, 0, 0, 0, 0, 0);
		cairo_pattern_add_color_stop_rgba(g, 1, 0, 0, 0, a);
		cairo_rectangle(cr, w - 10, 0, 10, h);
		cairo_set_source(cr, g);
		cairo_fill(cr);
		cairo_pattern_destroy(g);
	}
	const int dragFrom = r->dragging ? r->pressed : r->pictureDrag;
	const int i = r->pictureLabel >= 0 ? r->pictureLabel : r->hovered;
	if (i < 0 || i >= (int)r->items.size() || dragFrom >= 0) return;
	// Folded, the label goes with the rail only while it's out.
	if (r->folded() && r->floaterState != 2) return;
	const actions::Item& a = *r->items[(size_t)i];
	const bool en = actions::enabled(a, r->win);
	const std::string title = actions::plainTitle(a), key = actions::keyLabel(a);
	const float tw = textWidth(title, 12, true), kw = key.empty() ? 0 : textWidth(key, 11.5f);
	const float lw = 9 + tw + (key.empty() ? 0 : 8 + kw) + 9;
	const float cy = actions::kTop + i * actions::kPitch + actions::kItemH / 2;
	const RectF box = rectF(w - 6 - lw, cy - 13, w - 6, cy + 13);
	const float sa = c.dark ? 0.2f : 0.06f;
	fillRound(cr, rectF(box.left - 2, box.top, box.right + 2, box.bottom + 4), 8, colorF(0, 0, 0, sa));
	fillRound(cr, rectF(box.left - 1, box.top + 1, box.right + 1, box.bottom + 2), 7, colorF(0, 0, 0, sa));
	fillRound(cr, box, 6, c.tabCard());
	strokeRound(cr, rectF(box.left + 0.5f, box.top + 0.5f, box.right - 0.5f, box.bottom - 0.5f), 6, withAlpha(c.tabInk(), 0.12f));
	drawTextMid(cr, title, rectF(box.left + 9, box.top, box.left + 9 + tw + 1, box.bottom), 12, withAlpha(c.tabInk(), en ? 1.0f : 0.5f), TextAlign::Leading,
	            true);
	if (!key.empty())
		drawTextMid(cr, key, rectF(box.left + 9 + tw + 8, box.top, box.right - 8, box.bottom), 11.5f, withAlpha(c.tabInk(), 0.5f));
}

// ---- Pictures (--show rail:N) ----------------------------------------------------------------

void Rail::picture(int kind) {
	auto indexOf = [&](const char* id) {
		for (size_t i = 0; i < items.size(); i++) if (std::string(items[i]->id) == id) return (int)i;
		return -1;
	};
	items = actions::rail();
	switch (kind) {
	case 0: pictureLabel = indexOf("checkCircuit"); break;
	case 1: pictureFold = true; break;
	case 2: pictureFold = picturePeek = true; break;
	case 3: pictureFold = pictureSlid = true; pictureLabel = indexOf("tidy"); break;
	case 4: pictureDrag = 1; pictureDy = 52; break;
	case 5: win->toggleFocusMode(); break;
	case 6: win->toggleFocusMode(); pictureSlid = true; pictureLabel = indexOf("checkCircuit"); break;
	case 7: gtk_window_resize(win->window(), 860, 640); break;
	case 8: win->toggleSimView(); pictureLabel = indexOf("tidy"); break;
	case 9: prefs().actionsOn = false; actions::changed(); break;
	case 10: win->toggleFocusMode(); picturePeek = true; break;
	default: break;
	}
	if (pictureSlid) { hover.slidOut = true; slide.set(1); }
	apply();
	if (labels) labels->redraw();
}
