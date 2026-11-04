// --settings-check (Settings.h): every Settings page, and the Customize
// Actions window, on the screen this run has -- each pictured (the whole
// screen, so a window running off it shows), and checked: it fits the
// screen, its content fits its width, its text can be read (a contrast of
// 4.5:1 against what's behind it, as WCAG asks of body text), and the mouse
// wheel over a dropdown, number box, slider or switch scrolls the page
// rather than changing the setting.

#include "Settings.h"
#include "Actions.h"
#include "Chrome.h"
#include "Window.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace settings {

namespace {

int gFailed = 0;
std::string gDir, gTag;

void expect(bool ok, const std::string& what) {
	printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
	if (!ok) gFailed++;
}

// The app runs (draws, animates, lays out) for `ms`.
void pump(int ms) {
	const gint64 until = g_get_monotonic_time() + (gint64)ms * 1000;
	while (g_get_monotonic_time() < until) {
		while (g_main_context_pending(nullptr)) g_main_context_iteration(nullptr, FALSE);
		g_usleep(4000);
	}
}

// The whole screen, as a PNG: what a person would see.
void shot(const std::string& name) {
	if (gDir.empty()) return;
	GdkWindow* root = gdk_get_default_root_window();
	const int w = gdk_window_get_width(root), h = gdk_window_get_height(root);
	GdkPixbuf* pb = gdk_pixbuf_get_from_window(root, 0, 0, w, h);
	const std::string file = gDir + "/" + gTag + "-" + name + ".png";
	const bool ok = pb && gdk_pixbuf_save(pb, file.c_str(), "png", nullptr, nullptr);
	if (pb) g_object_unref(pb);
	printf("%s %s\n", ok ? "pictured" : "couldn't picture", file.c_str());
}

void walk(GtkWidget* w, const std::function<void(GtkWidget*)>& f) {
	f(w);
	if (!GTK_IS_CONTAINER(w)) return;
	GList* kids = gtk_container_get_children(GTK_CONTAINER(w));
	for (GList* k = kids; k; k = k->next) walk(GTK_WIDGET(k->data), f);
	g_list_free(kids);
}

std::string themeName() {
	gchar* t = nullptr;
	g_object_get(gtk_settings_get_default(), "gtk-theme-name", &t, nullptr);
	std::string s = t ? t : "?";
	g_free(t);
	if (const char* env = g_getenv("GTK_THEME")) s += std::string(" (GTK_THEME=") + env + ")";
	return s;
}

// ---- Contrast ------------------------------------------------------------------------

struct Rgb { double r = 0, g = 0, b = 0; };

double channel(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
double luminance(const Rgb& c) { return 0.2126 * channel(c.r) + 0.7152 * channel(c.g) + 0.0722 * channel(c.b); }
double ratio(const Rgb& a, const Rgb& b) {
	const double x = luminance(a), y = luminance(b);
	return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
}
Rgb over(const Rgb& under, double r, double g, double b, double a) {
	return Rgb{ under.r + (r - under.r) * a, under.g + (g - under.g) * a, under.b + (b - under.b) * a };
}
std::string hex(const Rgb& c) {
	return format("#%02x%02x%02x", (int)std::lround(c.r * 255), (int)std::lround(c.g * 255), (int)std::lround(c.b * 255));
}

// What a widget paints behind itself, over what's under it: its background
// colour, then its background image (a gradient's average colour; GTK themes
// paint buttons with them).
Rgb paintOver(GtkWidget* w, Rgb under) {
	GtkStyleContext* ctx = gtk_widget_get_style_context(w);
	const GtkStateFlags state = gtk_style_context_get_state(ctx);
	GdkRGBA* bg = nullptr;
	gtk_style_context_get(ctx, state, GTK_STYLE_PROPERTY_BACKGROUND_COLOR, &bg, nullptr);
	if (bg) {
		under = over(under, bg->red, bg->green, bg->blue, bg->alpha);
		gdk_rgba_free(bg);
	}
	cairo_pattern_t* image = nullptr;
	gtk_style_context_get(ctx, state, GTK_STYLE_PROPERTY_BACKGROUND_IMAGE, &image, nullptr);
	if (image) {
		int n = 0;
		if (cairo_pattern_get_color_stop_count(image, &n) == CAIRO_STATUS_SUCCESS && n > 0) {
			double r = 0, g = 0, b = 0, a = 0;
			for (int i = 0; i < n; i++) {
				double off, sr, sg, sb, sa;
				cairo_pattern_get_color_stop_rgba(image, i, &off, &sr, &sg, &sb, &sa);
				r += sr / n; g += sg / n; b += sb / n; a += sa / n;
			}
			under = over(under, r, g, b, a);
		}
		cairo_pattern_destroy(image);
	}
	return under;
}

double opacityOf(GtkWidget* w) {
	GtkStyleContext* ctx = gtk_widget_get_style_context(w);
	double o = 1;
	gtk_style_context_get(ctx, gtk_style_context_get_state(ctx), "opacity", &o, nullptr);
	return o * gtk_widget_get_opacity(w);
}

struct Reading { double ratio; Rgb fg, bg; };

// A text widget's ink against everything painted under it, from the window
// down. (Not the window's own fade: the check waits for it.)
Reading read(GtkWidget* w) {
	std::vector<GtkWidget*> chain;
	for (GtkWidget* x = w; x; x = gtk_widget_get_parent(x)) chain.push_back(x);
	Rgb bg{ 1, 1, 1 };
	double opacity = 1;
	for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
		bg = paintOver(*it, bg);
		if (!gtk_widget_is_toplevel(*it)) opacity *= opacityOf(*it);
	}
	GtkStyleContext* ctx = gtk_widget_get_style_context(w);
	GdkRGBA fg;
	gtk_style_context_get_color(ctx, gtk_style_context_get_state(ctx), &fg);
	const Rgb ink = over(bg, fg.red, fg.green, fg.blue, fg.alpha * opacity);
	return Reading{ ratio(ink, bg), ink, bg };
}

std::string textOf(GtkWidget* w) {
	std::string t;
	if (GTK_IS_LABEL(w)) t = gtk_label_get_text(GTK_LABEL(w));
	else if (GTK_IS_ENTRY(w)) t = gtk_entry_get_text(GTK_ENTRY(w));
	else if (GTK_IS_CELL_VIEW(w)) {
		// A dropdown's chosen item.
		for (GtkWidget* x = w; x; x = gtk_widget_get_parent(x))
			if (GTK_IS_COMBO_BOX_TEXT(x)) {
				gchar* s = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(x));
				t = s ? s : "";
				g_free(s);
				break;
			}
	}
	if (t.size() > 48) t = t.substr(0, 45) + "...";
	return t;
}

bool showing(GtkWidget* w) {
	return gtk_widget_get_mapped(w) && gtk_widget_get_child_visible(w) && gtk_widget_get_allocated_width(w) > 1;
}

// Every piece of text in `root` that can be read (greyed-out text, as WCAG
// says, needn't be).
void checkText(GtkWidget* root, const std::string& where) {
	int n = 0, bad = 0;
	double worst = 100;
	walk(root, [&](GtkWidget* w) {
		if (!(GTK_IS_LABEL(w) || GTK_IS_ENTRY(w) || GTK_IS_CELL_VIEW(w))) return;
		if (!showing(w) || !gtk_widget_is_sensitive(w)) return;
		const std::string t = textOf(w);
		if (t.empty()) return;
		const Reading r = read(w);
		n++;
		worst = std::min(worst, r.ratio);
		if (r.ratio < 4.5) {
			bad++;
			printf("  hard to read on %s: %s \"%s\": %s on %s, %.2f:1\n", where.c_str(), G_OBJECT_TYPE_NAME(w), t.c_str(), hex(r.fg).c_str(),
			       hex(r.bg).c_str(), r.ratio);
		}
	});
	expect(n > 0 && bad == 0, format("%s: all %d pieces of text read at 4.5:1 or better (the lowest %.2f:1)", where.c_str(), n, n ? worst : 0.0));
}

// ---- Fitting ---------------------------------------------------------------------------

GdkRectangle workArea(GtkWidget* win) {
	GdkDisplay* d = gdk_display_get_default();
	GdkMonitor* mon = gtk_widget_get_window(win) ? gdk_display_get_monitor_at_window(d, gtk_widget_get_window(win)) : nullptr;
	if (!mon) mon = gdk_display_get_primary_monitor(d);
	if (!mon) mon = gdk_display_get_monitor(d, 0);
	GdkRectangle r{ 0, 0, 0, 0 };
	if (mon) gdk_monitor_get_workarea(mon, &r);
	return r;
}

GdkRectangle frameOf(GtkWidget* win) {
	GdkRectangle r{ 0, 0, 0, 0 };
	if (GdkWindow* gw = gtk_widget_get_window(win)) gdk_window_get_frame_extents(gw, &r);
	return r;
}

bool inside(const GdkRectangle& r, const GdkRectangle& area) {
	return r.x >= area.x && r.y >= area.y && r.x + r.width <= area.x + area.width && r.y + r.height <= area.y + area.height;
}

std::string rect(const GdkRectangle& r) { return format("%dx%d at (%d,%d)", r.width, r.height, r.x, r.y); }

void checkFits(GtkWidget* win, const std::string& where) {
	const GdkRectangle area = workArea(win), f = frameOf(win);
	expect(f.width > 0 && inside(f, area), where + ": the window, " + rect(f) + ", is on the screen's " + rect(area));
}

// ---- The wheel -------------------------------------------------------------------------

// The window a pointer at (x, y) in `w` is over, as X picks it: the topmost
// visible child under it, all the way down.
GdkWindow* windowAt(GdkWindow* w, int x, int y, int* ox, int* oy) {
	for (GList* l = gdk_window_peek_children(w); l; l = l->next) {
		GdkWindow* c = GDK_WINDOW(l->data);
		if (!gdk_window_is_visible(c)) continue;
		int cx = 0, cy = 0;
		gdk_window_get_position(c, &cx, &cy);
		if (x >= cx && y >= cy && x < cx + gdk_window_get_width(c) && y < cy + gdk_window_get_height(c))
			return windowAt(c, x - cx, y - cy, ox, oy);
	}
	*ox = x;
	*oy = y;
	return w;
}

double valueOf(GtkWidget* w) {
	if (GTK_IS_COMBO_BOX(w)) return gtk_combo_box_get_active(GTK_COMBO_BOX(w));
	if (GTK_IS_SPIN_BUTTON(w)) return gtk_spin_button_get_value(GTK_SPIN_BUTTON(w));
	if (GTK_IS_RANGE(w)) return gtk_range_get_value(GTK_RANGE(w));
	if (GTK_IS_SWITCH(w)) return gtk_switch_get_active(GTK_SWITCH(w));
	return 0;
}

bool isControl(GtkWidget* w) { return GTK_IS_COMBO_BOX(w) || GTK_IS_SPIN_BUTTON(w) || GTK_IS_SCALE(w) || GTK_IS_SWITCH(w); }

const char* kindOf(GtkWidget* w) {
	return GTK_IS_COMBO_BOX(w) ? "dropdown" : GTK_IS_SPIN_BUTTON(w) ? "number box" : GTK_IS_SCALE(w) ? "slider" : "switch";
}

// One turn of the wheel (or a touchpad's smooth scroll) over `w`'s middle,
// delivered as the display server would: to the window under the pointer.
bool wheel(GtkWidget* w, GdkScrollDirection dir, bool smooth) {
	GtkAllocation a;
	gtk_widget_get_allocation(w, &a);
	GdkWindow* base = gtk_widget_get_window(w);
	if (!base) return false;
	int x = 0, y = 0;
	if (!gtk_widget_get_has_window(w)) { x = a.x; y = a.y; }
	int lx = 0, ly = 0;
	GdkWindow* hit = windowAt(base, x + a.width / 2, y + a.height / 2, &lx, &ly);
	gpointer owner = nullptr;
	gdk_window_get_user_data(hit, &owner);
	if (!owner || !(GTK_WIDGET(owner) == w || gtk_widget_is_ancestor(GTK_WIDGET(owner), w))) return false;
	GdkEvent* e = gdk_event_new(GDK_SCROLL);
	e->scroll.window = GDK_WINDOW(g_object_ref(hit));
	e->scroll.send_event = TRUE;
	e->scroll.time = GDK_CURRENT_TIME;
	e->scroll.x = lx;
	e->scroll.y = ly;
	int rx = 0, ry = 0;
	gdk_window_get_root_coords(hit, lx, ly, &rx, &ry);
	e->scroll.x_root = rx;
	e->scroll.y_root = ry;
	if (smooth) {
		e->scroll.direction = GDK_SCROLL_SMOOTH;
		e->scroll.delta_y = dir == GDK_SCROLL_DOWN ? 1 : -1;
	} else {
		e->scroll.direction = dir;
	}
	GdkDevice* pointer = gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_display_get_default()));
	gdk_event_set_device(e, pointer);
	gdk_event_set_source_device(e, pointer);
	gtk_main_do_event(e);
	gdk_event_free(e);
	pump(40);
	return true;
}

GtkWidget* scrollerOf(GtkWidget* w) {
	for (GtkWidget* x = gtk_widget_get_parent(w); x; x = gtk_widget_get_parent(x))
		if (GTK_IS_SCROLLED_WINDOW(x)) return x;
	return nullptr;
}

// The wheel over each control in `root`: the setting stays as it was, and
// where there's more page to scroll to, the page scrolls instead.
void checkWheel(GtkWidget* root, const std::string& where) {
	std::vector<GtkWidget*> controls;
	walk(root, [&](GtkWidget* w) { if (isControl(w) && showing(w) && gtk_widget_is_sensitive(w)) controls.push_back(w); });
	if (controls.empty()) { printf("  (no dropdowns, number boxes, sliders or switches on %s)\n", where.c_str()); return; }
	int tried = 0, kept = 0, scrolled = 0, couldScroll = 0;
	for (GtkWidget* c : controls) {
		// A control's change can rebuild its page (one that wasn't kept): gone, then.
		if (!GTK_IS_WIDGET(c) || !showing(c)) continue;
		GtkWidget* sc = scrollerOf(c);
		GtkAdjustment* v = sc ? gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sc)) : nullptr;
		// In view first, as a pointer would have to be.
		if (v) {
			GtkWidget* content = gtk_bin_get_child(GTK_BIN(sc));
			if (content && GTK_IS_VIEWPORT(content) && gtk_bin_get_child(GTK_BIN(content))) content = gtk_bin_get_child(GTK_BIN(content));
			int cx = 0, cy = 0;
			if (content) gtk_widget_translate_coordinates(c, content, 0, 0, &cx, &cy);
			const double top = gtk_adjustment_get_lower(v), bottom = gtk_adjustment_get_upper(v) - gtk_adjustment_get_page_size(v);
			gtk_adjustment_set_value(v, std::max(top, std::min(bottom, cy - gtk_adjustment_get_page_size(v) / 3)));
			pump(60);
		}
		const double before = valueOf(c);
		const double at = v ? gtk_adjustment_get_value(v) : 0;
		const bool roomBelow = v && at < gtk_adjustment_get_upper(v) - gtk_adjustment_get_page_size(v) - 1;
		bool reached = wheel(c, GDK_SCROLL_DOWN, false);
		const bool moved = v && gtk_adjustment_get_value(v) > at + 0.5;
		reached = reached && GTK_IS_WIDGET(c) && wheel(c, GDK_SCROLL_UP, false);
		reached = reached && GTK_IS_WIDGET(c) && wheel(c, GDK_SCROLL_DOWN, true);
		reached = reached && GTK_IS_WIDGET(c) && wheel(c, GDK_SCROLL_UP, true);
		if (!reached) {
			printf("  (the pointer couldn't reach a %s on %s)\n", GTK_IS_WIDGET(c) ? kindOf(c) : "control", where.c_str());
			continue;
		}
		tried++;
		const double after = valueOf(c);
		if (after == before) kept++;
		else printf("  the wheel changed a %s on %s: %g to %g\n", kindOf(c), where.c_str(), before, after);
		if (roomBelow) {
			couldScroll++;
			if (moved) scrolled++;
			else printf("  the wheel over a %s on %s didn't scroll the page\n", kindOf(c), where.c_str());
		}
		if (v) gtk_adjustment_set_value(v, 0);
	}
	expect(tried > 0 && kept == tried, format("%s: the wheel over %d of %d controls left every one as it was (%d kept)", where.c_str(), tried,
	                                          (int)controls.size(), kept));
	if (couldScroll > 0) expect(scrolled == couldScroll, format("%s: and scrolled the page instead (%d of %d)", where.c_str(), scrolled, couldScroll));
}

// A window of every kind of control, in a scroller, as the dialogs have
// them: the wheel over each.
void checkWheelEverywhere() {
	GtkWidget* win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(win), 320, 240);
	GtkWidget* sc = gtk_scrolled_window_new(nullptr, nullptr);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	GtkWidget* combo = gtk_combo_box_text_new();
	for (const char* s : { "One", "Two", "Three" }) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), s);
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 1);
	GtkWidget* spin = gtk_spin_button_new_with_range(0, 10, 1);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), 5);
	GtkWidget* scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 10, 1);
	gtk_range_set_value(GTK_RANGE(scale), 5);
	GtkWidget* sw = gtk_switch_new();
	gtk_widget_set_halign(sw, GTK_ALIGN_START);
	for (GtkWidget* w : { combo, spin, scale, sw }) gtk_box_pack_start(GTK_BOX(box), w, FALSE, FALSE, 0);
	GtkWidget* tall = gtk_drawing_area_new();
	gtk_widget_set_size_request(tall, -1, 900);
	gtk_box_pack_start(GTK_BOX(box), tall, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(sc), box);
	gtk_container_add(GTK_CONTAINER(win), sc);
	gtk_widget_show_all(win);
	pump(300);
	checkWheel(win, "a window of every kind of control");
	gtk_widget_destroy(win);
}

}  // namespace

int check(CircuitWindow* w, const std::string& dir, const std::string& tag) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	gDir = dir;
	gTag = tag.empty() ? std::string("screen") : tag;
	if (!dir.empty()) g_mkdir_with_parents(dir.c_str(), 0755);
	const GdkRectangle screen = workArea(GTK_WIDGET(w->window()));
	printf("settings-check: %s, a %dx%d work area, the %s theme, the app %s\n", gTag.c_str(), screen.width, screen.height, themeName().c_str(),
	       chrome().dark ? "dark" : "light");
	show(w, 0);
	pump(800);
	GtkWidget* win = window();
	expect(win != nullptr, "Settings opens");
	if (!win) return 1;
	int width = -1;
	bool sameWidth = true;
	for (int page = 0; page < pageCount(); page++) {
		selectNow(page);
		pump(450);
		const std::string where = std::string("Settings > ") + pageTitle(page);
		shot(format("settings-%d", page));
		checkFits(win, where);
		const int ww = gtk_widget_get_allocated_width(win);
		if (width < 0) width = ww;
		sameWidth = sameWidth && ww == width;
		GtkWidget* sc = pageScroller();
		GtkWidget* pg = currentPage();
		if (sc && pg) {
			int minW = 0, natW = 0;
			gtk_widget_get_preferred_width(pg, &minW, &natW);
			const int room = gtk_widget_get_allocated_width(sc);
			expect(minW <= room, format("%s: its content needs %d of the %d points it has across", where.c_str(), minW, room));
			int minH = 0, natH = 0;
			gtk_widget_get_preferred_height_for_width(pg, room, &minH, &natH);
			GtkAdjustment* v = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sc));
			expect(gtk_adjustment_get_upper(v) + 1 >= minH,
			       format("%s: all %d points of it can be reached (%d shown, scrolling to %.0f)", where.c_str(), minH,
			              gtk_widget_get_allocated_height(sc), gtk_adjustment_get_upper(v)));
		}
		checkText(win, where);
		checkWheel(win, where);
		if (window() != win) { expect(false, where + ": Settings is still open after the wheel"); return 1; }
	}
	expect(sameWidth, format("every page is the same width (%d)", width));
	// Customize Actions (from the rail, or Settings > Actions).
	gtk_widget_destroy(win);
	pump(200);
	actions::showCustomize(w, "");
	pump(600);
	if (GtkWidget* cw = actions::customizeWindow()) {
		shot("customize");
		checkFits(cw, "Customize Actions");
		checkText(cw, "Customize Actions");
		checkWheel(cw, "Customize Actions");
		if (actions::customizeWindow()) gtk_widget_destroy(actions::customizeWindow());
		pump(100);
	} else {
		expect(false, "Customize Actions opens");
	}
	checkWheelEverywhere();
	printf("%s\n", gFailed == 0 ? "settings-check: all passed" : format("settings-check: %d failed", gFailed).c_str());
	return gFailed == 0 ? 0 : 1;
}

}  // namespace settings
