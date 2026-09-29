// The welcome window and the guided tour (see Welcome.h).

#include "Welcome.h"
#include "Canvas.h"
#include "Window.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace {

// ---- The welcome window --------------------------------------------------------

// A small drawing of a switch, an AND gate and a light, wired up -- so the
// welcome page has something to look at without a whole animated hero view.
gboolean drawHero(GtkWidget* w, cairo_t* cr, gpointer) {
	const double width = gtk_widget_get_allocated_width(w), height = gtk_widget_get_allocated_height(w);
	const RGBA a = accentColor(prefs().dark);
	cairo_set_line_width(cr, 2);
	cairo_set_source_rgba(cr, a.r, a.g, a.b, 0.9);
	const double cy = height / 2, x0 = width / 2 - 90, x1 = width / 2 - 30, x2 = width / 2 + 30, x3 = width / 2 + 90;
	// Switch.
	cairo_rectangle(cr, x0 - 14, cy - 14, 28, 28);
	cairo_stroke(cr);
	// Wire in.
	cairo_move_to(cr, x0 + 14, cy);
	cairo_line_to(cr, x1 - 20, cy);
	cairo_stroke(cr);
	// AND gate (a flat side, a curved front).
	cairo_move_to(cr, x1 - 20, cy - 22);
	cairo_line_to(cr, x1, cy - 22);
	cairo_curve_to(cr, x1 + 34, cy - 22, x1 + 34, cy + 22, x1, cy + 22);
	cairo_line_to(cr, x1 - 20, cy + 22);
	cairo_close_path(cr);
	cairo_stroke(cr);
	// Wire out and light.
	cairo_move_to(cr, x1 + 34, cy);
	cairo_line_to(cr, x2 + 26, cy);
	cairo_stroke(cr);
	cairo_arc(cr, x3 - 4, cy, 16, 0, 2 * G_PI);
	cairo_set_source_rgba(cr, a.r, a.g, a.b, 0.22);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, a.r, a.g, a.b, 0.9);
	cairo_stroke(cr);
	return TRUE;
}

GtkWidget* heading(const char* eyebrow, const char* title, const char* line) {
	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	GtkWidget* e = gtk_label_new(nullptr);
	gchar* em = g_markup_printf_escaped("<span weight='bold' size='small' foreground='#4d90fe'>%s</span>", eyebrow);
	gtk_label_set_markup(GTK_LABEL(e), em);
	g_free(em);
	gtk_label_set_xalign(GTK_LABEL(e), 0);
	GtkWidget* t = gtk_label_new(nullptr);
	gchar* tm = g_markup_printf_escaped("<span weight='bold' size='xx-large'>%s</span>", title);
	gtk_label_set_markup(GTK_LABEL(t), tm);
	g_free(tm);
	gtk_label_set_xalign(GTK_LABEL(t), 0);
	GtkWidget* l = gtk_label_new(line);
	gtk_label_set_xalign(GTK_LABEL(l), 0);
	gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
	gtk_style_context_add_class(gtk_widget_get_style_context(l), "dim-label");
	gtk_box_pack_start(GTK_BOX(box), e, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), t, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), l, FALSE, FALSE, 4);
	return box;
}

GtkWidget* point(const char* title, const char* line) {
	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
	gtk_container_set_border_width(GTK_CONTAINER(box), 12);
	gtk_style_context_add_class(gtk_widget_get_style_context(box), "welcome-card");
	GtkWidget* t = gtk_label_new(nullptr);
	gchar* tm = g_markup_printf_escaped("<b>%s</b>", title);
	gtk_label_set_markup(GTK_LABEL(t), tm);
	g_free(tm);
	gtk_label_set_xalign(GTK_LABEL(t), 0);
	GtkWidget* l = gtk_label_new(line);
	gtk_label_set_xalign(GTK_LABEL(l), 0);
	gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
	gtk_style_context_add_class(gtk_widget_get_style_context(l), "dim-label");
	gtk_box_pack_start(GTK_BOX(box), t, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), l, FALSE, FALSE, 0);
	return box;
}

GtkWidget* keyRow(const char* key, const char* what) {
	GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	GtkWidget* k = gtk_label_new(key);
	gtk_widget_set_size_request(k, 78, -1);
	gtk_style_context_add_class(gtk_widget_get_style_context(k), "welcome-key");
	GtkWidget* w = gtk_label_new(what);
	gtk_label_set_xalign(GTK_LABEL(w), 0);
	gtk_box_pack_start(GTK_BOX(row), k, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(row), w, TRUE, TRUE, 0);
	return row;
}

struct Welcome {
	GtkWidget* win;
	GtkWidget* stack;
	GtkWidget* dots[3];
	GtkWidget* backBtn;
	GtkWidget* nextBtn;
	int page = 0;
	CircuitWindow* window;
};

void updateNav(Welcome* w) {
	const RGBA a = accentColor(prefs().dark);
	char hex[8];
	snprintf(hex, sizeof hex, "#%02x%02x%02x", (int)(a.r * 255), (int)(a.g * 255), (int)(a.b * 255));
	for (int i = 0; i < 3; i++) {
		gchar* m = g_markup_printf_escaped("<span foreground='%s'>●</span>", i == w->page ? hex : "#888888");
		gtk_label_set_markup(GTK_LABEL(w->dots[i]), m);
		g_free(m);
	}
	gtk_widget_set_sensitive(w->backBtn, w->page > 0);
	gtk_button_set_label(GTK_BUTTON(w->nextBtn), w->page == 2 ? "Just Start" : (w->page == 0 ? "Get Started" : "Continue"));
	gtk_stack_set_visible_child_name(GTK_STACK(w->stack), format("p%d", w->page).c_str());
}

void finish(Welcome* w, bool startTourNow) {
	prefs().hasSeenWelcome = true;
	prefs().save();
	CircuitWindow* window = w->window;
	gtk_widget_destroy(w->win);
	delete w;
	if (startTourNow) ::startTour(window);
}

void go(Welcome* w, int delta) {
	const int to = std::min(2, std::max(0, w->page + delta));
	if (to == w->page) { finish(w, false); return; }
	w->page = to;
	updateNav(w);
}

void build(Welcome* w) {
	GtkWidget* v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(w->win), v);

	w->stack = gtk_stack_new();
	gtk_stack_set_transition_type(GTK_STACK(w->stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
	gtk_stack_set_transition_duration(GTK_STACK(w->stack), 220);
	gtk_widget_set_vexpand(w->stack, TRUE);
	gtk_box_pack_start(GTK_BOX(v), w->stack, TRUE, TRUE, 0);

	// Page 0: what it is.
	{
		GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		GtkWidget* hero = gtk_drawing_area_new();
		gtk_widget_set_size_request(hero, -1, 150);
		g_signal_connect(hero, "draw", G_CALLBACK(drawHero), nullptr);
		gtk_box_pack_start(GTK_BOX(page), hero, FALSE, FALSE, 0);
		GtkWidget* body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
		gtk_container_set_border_width(GTK_CONTAINER(body), 40);
		gtk_box_pack_start(GTK_BOX(body), heading("CedarLogic",
			"Build it. Watch it think.",
			"Design digital logic circuits, run them live, and see exactly what every wire is doing."), FALSE, FALSE, 0);
		GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
		gtk_box_pack_start(GTK_BOX(row), point("It keeps itself", "Saves as you go; a copy is kept if it ever closes unexpectedly."), TRUE, TRUE, 0);
		gtk_box_pack_start(GTK_BOX(row), point("It shows its work", "Live wires, a truth table on one key, and an oscilloscope."), TRUE, TRUE, 0);
		gtk_box_pack_start(GTK_BOX(row), point("It stays out of the way", "Nearly everything has a key. You won't need the menus."), TRUE, TRUE, 0);
		gtk_box_pack_start(GTK_BOX(body), row, FALSE, FALSE, 8);
		gtk_box_pack_start(GTK_BOX(page), body, TRUE, TRUE, 0);
		gtk_stack_add_named(GTK_STACK(w->stack), page, "p0");
	}
	// Page 1: keys worth knowing.
	{
		GtkWidget* body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
		gtk_container_set_border_width(GTK_CONTAINER(body), 40);
		gtk_box_pack_start(GTK_BOX(body), heading("Six keys",
			"Worth knowing by heart.",
			"Click the canvas first, then just press the key -- no menu needed."), FALSE, FALSE, 0);
		GtkWidget* keys = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
		gtk_box_pack_start(GTK_BOX(keys), keyRow("A", "Add a gate by typing its name"), FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(keys), keyRow("R", "Rotate the selection"), FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(keys), keyRow("S", "Straighten the selected wires"), FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(keys), keyRow("Shift+S", "Tidy up the whole layout"), FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(keys), keyRow("T", "Truth table for this page"), FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(keys), keyRow("Space", "Run or pause the simulation"), FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(body), keys, FALSE, FALSE, 8);
		gtk_stack_add_named(GTK_STACK(w->stack), body, "p1");
	}
	// Page 2: ready.
	{
		GtkWidget* body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
		gtk_container_set_border_width(GTK_CONTAINER(body), 40);
		gtk_widget_set_valign(body, GTK_ALIGN_CENTER);
		gtk_box_pack_start(GTK_BOX(body), heading("Ready", "Let's build something.",
			"A short guided tour builds a working circuit with you, one step at a time -- or just start."), FALSE, FALSE, 0);
		GtkWidget* tourBtn = gtk_button_new_with_label("Take the Guided Tour");
		gtk_style_context_add_class(gtk_widget_get_style_context(tourBtn), "suggested-action");
		g_signal_connect(tourBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) { finish(static_cast<Welcome*>(data), true); }), w);
		gtk_box_pack_start(GTK_BOX(body), tourBtn, FALSE, FALSE, 8);
		gtk_stack_add_named(GTK_STACK(w->stack), body, "p2");
	}

	gtk_widget_show_all(w->stack);

	GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_container_set_border_width(GTK_CONTAINER(bar), 20);
	GtkWidget* dotsBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	for (int i = 0; i < 3; i++) {
		w->dots[i] = gtk_label_new("●");
		gtk_style_context_add_class(gtk_widget_get_style_context(w->dots[i]), i == 0 ? "dim-label" : "dim-label");
		gtk_box_pack_start(GTK_BOX(dotsBox), w->dots[i], FALSE, FALSE, 0);
	}
	gtk_box_pack_start(GTK_BOX(bar), dotsBox, FALSE, FALSE, 0);
	GtkWidget* spacer = gtk_label_new("");
	gtk_box_pack_start(GTK_BOX(bar), spacer, TRUE, TRUE, 0);
	GtkWidget* skip = gtk_button_new_with_label("Skip");
	g_signal_connect(skip, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) { finish(static_cast<Welcome*>(data), false); }), w);
	w->backBtn = gtk_button_new_with_label("Back");
	g_signal_connect(w->backBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) { go(static_cast<Welcome*>(data), -1); }), w);
	w->nextBtn = gtk_button_new_with_label("Get Started");
	gtk_style_context_add_class(gtk_widget_get_style_context(w->nextBtn), "suggested-action");
	g_signal_connect(w->nextBtn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) { go(static_cast<Welcome*>(data), 1); }), w);
	gtk_box_pack_start(GTK_BOX(bar), skip, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bar), w->backBtn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bar), w->nextBtn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(v), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(v), bar, FALSE, FALSE, 0);
	gtk_widget_show_all(bar);
	updateNav(w);
}

// ---- The guided tour --------------------------------------------------------
//
// A plain, undecorated window positioned by hand next to its anchor widget,
// not a GtkPopover: a popover's override-redirect arrow window depends on
// the window manager compositing it, and without one (as on this app's own
// test setup, and plausibly some minimal Linux desktops) it can fail to
// show at all, or leave the screen in a bad state. This is the same plain-
// window technique the splash screen and every other extra window in this
// app already use, which is known to work everywhere.

struct TourStep {
	GtkWidget* anchor;
	std::string title, body;
	// Where the bubble goes, relative to the anchor's own on-screen box.
	enum Side { Below, Right } side;
};

struct Tour {
	CircuitWindow* window;
	std::vector<TourStep> steps;
	size_t index = 0;
	GtkWidget* bubble = nullptr;
	gulong configureHandler = 0;
};

void showStep(Tour* t);

void closeBubble(Tour* t) {
	if (t->bubble) {
		if (t->configureHandler && t->window && GTK_IS_WIDGET(t->window->window()))
			g_signal_handler_disconnect(t->window->window(), t->configureHandler);
		gtk_widget_destroy(t->bubble);
	}
	t->bubble = nullptr;
	t->configureHandler = 0;
}

void endTour(Tour* t) {
	closeBubble(t);
	delete t;
}

void nextStep(Tour* t) {
	t->index++;
	if (t->index >= t->steps.size()) { endTour(t); return; }
	showStep(t);
}

// The anchor's box, in screen coordinates (its window's origin plus its
// allocation within that window).
bool anchorBox(GtkWidget* anchor, GdkRectangle& out) {
	if (anchor == nullptr || !gtk_widget_get_realized(anchor) || !gtk_widget_get_mapped(anchor)) return false;
	GdkWindow* gw = gtk_widget_get_window(anchor);
	if (gw == nullptr) return false;
	int ox = 0, oy = 0;
	gdk_window_get_origin(gw, &ox, &oy);
	GtkAllocation alloc;
	gtk_widget_get_allocation(anchor, &alloc);
	// gdk_window_get_origin gives the anchor's own GdkWindow origin, which
	// for a widget that owns no window of its own (most widgets, under
	// GTK3's default "no window" style) is really its toplevel's window --
	// so add the widget's allocation within that window to land on the
	// widget itself, not the corner of the whole toplevel.
	int wx = 0, wy = 0;
	gtk_widget_translate_coordinates(anchor, gtk_widget_get_toplevel(anchor), 0, 0, &wx, &wy);
	out.x = ox + wx;
	out.y = oy + wy;
	out.width = alloc.width;
	out.height = alloc.height;
	return true;
}

void repositionBubble(Tour* t) {
	if (t->bubble == nullptr || t->index >= t->steps.size()) return;
	GdkRectangle box;
	if (!anchorBox(t->steps[t->index].anchor, box)) return;
	int bw = 0, bh = 0;
	gtk_window_get_size(GTK_WINDOW(t->bubble), &bw, &bh);
	int x = box.x, y = box.y;
	switch (t->steps[t->index].side) {
	case TourStep::Below: x = box.x + 24; y = box.y + std::min(box.height, 90) + 10; break;
	case TourStep::Right: x = box.x + box.width + 10; y = box.y + 10; break;
	}
	gtk_window_move(GTK_WINDOW(t->bubble), x, y);
}

void showStep(Tour* t) {
	closeBubble(t);
	const TourStep& s = t->steps[t->index];
	GdkRectangle box;
	if (!anchorBox(s.anchor, box)) { nextStep(t); return; }

	t->bubble = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_decorated(GTK_WINDOW(t->bubble), FALSE);
	gtk_window_set_resizable(GTK_WINDOW(t->bubble), FALSE);
	gtk_window_set_type_hint(GTK_WINDOW(t->bubble), GDK_WINDOW_TYPE_HINT_UTILITY);
	gtk_window_set_transient_for(GTK_WINDOW(t->bubble), GTK_WINDOW(gtk_widget_get_toplevel(s.anchor)));
	gtk_window_set_keep_above(GTK_WINDOW(t->bubble), TRUE);
	gtk_window_set_skip_taskbar_hint(GTK_WINDOW(t->bubble), TRUE);
	gtk_widget_set_name(t->bubble, "tour-bubble");
	gtk_window_set_accept_focus(GTK_WINDOW(t->bubble), TRUE);

	GtkWidget* frame = gtk_frame_new(nullptr);
	GtkWidget* box2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_set_border_width(GTK_CONTAINER(box2), 14);
	gtk_widget_set_size_request(box2, 260, -1);
	GtkWidget* title = gtk_label_new(nullptr);
	gchar* m = g_markup_printf_escaped("<b>%s</b>", s.title.c_str());
	gtk_label_set_markup(GTK_LABEL(title), m);
	g_free(m);
	gtk_label_set_xalign(GTK_LABEL(title), 0);
	GtkWidget* body = gtk_label_new(s.body.c_str());
	gtk_label_set_xalign(GTK_LABEL(body), 0);
	gtk_label_set_line_wrap(GTK_LABEL(body), TRUE);
	GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget* count = gtk_label_new(format("%zu of %zu", t->index + 1, t->steps.size()).c_str());
	gtk_style_context_add_class(gtk_widget_get_style_context(count), "dim-label");
	GtkWidget* spacer = gtk_label_new("");
	GtkWidget* skip = gtk_button_new_with_label("Skip Tour");
	g_signal_connect(skip, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) { endTour(static_cast<Tour*>(data)); }), t);
	GtkWidget* next = gtk_button_new_with_label(t->index + 1 == t->steps.size() ? "Done" : "Next");
	gtk_style_context_add_class(gtk_widget_get_style_context(next), "suggested-action");
	g_signal_connect(next, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) { nextStep(static_cast<Tour*>(data)); }), t);
	gtk_box_pack_start(GTK_BOX(row), count, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(row), spacer, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(row), skip, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(row), next, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box2), title, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box2), body, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box2), row, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(frame), box2);
	gtk_container_add(GTK_CONTAINER(t->bubble), frame);

	// Escape closes it like Skip; a plain window needs its own key handler
	// (there's no popover-style implicit dismiss).
	g_signal_connect(t->bubble, "key-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventKey* e, gpointer data) -> gboolean {
		if (e->keyval == GDK_KEY_Escape) { endTour(static_cast<Tour*>(data)); return TRUE; }
		return FALSE;
	}), t);
	// If the main window moves or resizes, follow it.
	if (GTK_IS_WIDGET(t->window->window()))
		t->configureHandler = g_signal_connect(t->window->window(), "configure-event",
		                                       G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer data) -> gboolean {
			                                       repositionBubble(static_cast<Tour*>(data));
			                                       return FALSE;
		                                       }), t);

	gtk_widget_show_all(t->bubble);
	repositionBubble(t);
}

void loadCss() {
	static bool done = false;
	if (done) return;
	done = true;
	GtkCssProvider* css = gtk_css_provider_new();
	gtk_css_provider_load_from_data(css,
		".welcome-card { background-color: alpha(currentColor, 0.05); border-radius: 10px; }\n"
		".welcome-key { font-family: monospace; font-weight: bold; }\n",
		-1, nullptr);
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
	                                          GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(css);
}

}  // namespace

void offerWelcome(GtkApplication*, CircuitWindow* window) {
	if (prefs().hasSeenWelcome || window == nullptr) return;
	loadCss();
	Welcome* w = new Welcome();
	w->window = window;
	w->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(w->win), "Welcome to CedarLogic");
	gtk_window_set_transient_for(GTK_WINDOW(w->win), window->window());
	gtk_window_set_modal(GTK_WINDOW(w->win), TRUE);
	gtk_window_set_default_size(GTK_WINDOW(w->win), 640, 520);
	build(w);
	gtk_widget_show_all(w->win);
	updateNav(w);   // the dots need their markup after show_all resets it
}

void startTour(CircuitWindow* window) {
	if (window == nullptr) return;
	Tour* t = new Tour();
	t->window = window;
	t->steps = {
		{ window->paletteWidgetForTour(), "The gate palette",
		  "Click a gate here, then click on the canvas to place it -- or drag it there directly.",
		  TourStep::Right },
		{ window->currentCanvas() ? window->currentCanvas()->widget() : nullptr, "The canvas",
		  "Click a switch to flip it. Drag from a pin to wire two parts together. Right-click for more.",
		  TourStep::Below },
		{ window->runButtonForTour(), "Run and pause",
		  "The simulation runs live as you build. Pause it here, or just tap Space.",
		  TourStep::Below },
		{ window->tabStripForTour(), "Pages",
		  "A circuit can have more than one page -- add another with the + at the right.",
		  TourStep::Below },
	};
	showStep(t);
}
