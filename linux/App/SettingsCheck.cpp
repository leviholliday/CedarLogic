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
#include "MenuBar.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
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

// ---- Contrast as drawn: lit, pressed, chosen and ticked ----------------------------------
// The text read off the window as GTK draws it (not from the style's
// colours alone: a dropdown's row draws its text through a node of its own,
// which a rule can miss), with the control forced into each state a pointer
// or a click puts it in. Drawn at three times the size, so a letter's strokes are
// solid and their colour is the text's own.

struct Shot {
	cairo_surface_t* s = nullptr;
	int w = 0, h = 0;
	static constexpr int kScale = 3;
	~Shot() { if (s) cairo_surface_destroy(s); }
};

// `top` drawn whole, on what its own background paints over white.
void drawShot(GtkWidget* top, Shot& shot) {
	if (shot.s) cairo_surface_destroy(shot.s);
	shot.w = std::max(1, gtk_widget_get_allocated_width(top));
	shot.h = std::max(1, gtk_widget_get_allocated_height(top));
	shot.s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, shot.w * Shot::kScale, shot.h * Shot::kScale);
	cairo_surface_set_device_scale(shot.s, Shot::kScale, Shot::kScale);
	cairo_t* cr = cairo_create(shot.s);
	const Rgb under = paintOver(top, Rgb{ 1, 1, 1 });
	cairo_set_source_rgb(cr, under.r, under.g, under.b);
	cairo_paint(cr);
	gtk_widget_draw(top, cr);
	cairo_destroy(cr);
	cairo_surface_flush(shot.s);
}

// The text in `text`'s box in the picture of `top`: its ground (the colour
// most of the box is) and its ink (the pixel that stands out most from it).
bool readDrawn(GtkWidget* top, const Shot& shot, GtkWidget* text, Reading& out) {
	int x = 0, y = 0;
	if (!gtk_widget_translate_coordinates(text, top, 0, 0, &x, &y)) return false;
	const int w = gtk_widget_get_allocated_width(text), h = gtk_widget_get_allocated_height(text);
	// Only what shows of it: clipped to every part it's in (a row scrolled
	// out of its list, or a menu's row scrolled out of its menu, isn't on
	// screen; what's drawn there is something else).
	int cx0 = x, cy0 = y, cx1 = x + w, cy1 = y + h;
	for (GtkWidget* a = gtk_widget_get_parent(text); a && a != top; a = gtk_widget_get_parent(a)) {
		int ax = 0, ay = 0;
		if (!gtk_widget_translate_coordinates(a, top, 0, 0, &ax, &ay)) return false;
		cx0 = std::max(cx0, ax);
		cy0 = std::max(cy0, ay);
		cx1 = std::min(cx1, ax + gtk_widget_get_allocated_width(a));
		cy1 = std::min(cy1, ay + gtk_widget_get_allocated_height(a));
	}
	// ...and to every window it's drawn in (a menu's rows scroll inside one,
	// under its scroll arrows; a list's inside its viewport's).
	if (GdkWindow* topWin = gtk_widget_get_window(top)) {
		int tx = 0, ty = 0;
		gdk_window_get_origin(topWin, &tx, &ty);
		for (GdkWindow* gw = gtk_widget_get_window(text); gw && gw != topWin; gw = gdk_window_get_parent(gw)) {
			int gx = 0, gy = 0;
			gdk_window_get_origin(gw, &gx, &gy);
			gx -= tx;
			gy -= ty;
			cx0 = std::max(cx0, gx);
			cy0 = std::max(cy0, gy);
			cx1 = std::min(cx1, gx + gdk_window_get_width(gw));
			cy1 = std::min(cy1, gy + gdk_window_get_height(gw));
		}
	}
	if (cx1 <= cx0 || cy1 <= cy0 || (cx1 - cx0) * (cy1 - cy0) < 0.6 * w * h) return false;
	const int k = Shot::kScale;
	const int x0 = std::max(0, cx0 * k), y0 = std::max(0, cy0 * k), x1 = std::min(shot.w * k, cx1 * k), y1 = std::min(shot.h * k, cy1 * k);
	if (x1 - x0 < 2 || y1 - y0 < 2) return false;
	const unsigned char* data = cairo_image_surface_get_data(shot.s);
	const int stride = cairo_image_surface_get_stride(shot.s);
	auto at = [&](int px, int py) {
		const uint32_t v = *reinterpret_cast<const uint32_t*>(data + py * stride + px * 4);
		return Rgb{ ((v >> 16) & 0xff) / 255.0, ((v >> 8) & 0xff) / 255.0, (v & 0xff) / 255.0 };
	};
	std::map<int, int> counts;
	std::map<int, Rgb> sums;
	for (int py = y0; py < y1; py++)
		for (int px = x0; px < x1; px++) {
			const Rgb c = at(px, py);
			const int key = ((int)(c.r * 31) << 10) | ((int)(c.g * 31) << 5) | (int)(c.b * 31);
			counts[key]++;
			Rgb& sum = sums[key];
			sum.r += c.r; sum.g += c.g; sum.b += c.b;
		}
	int best = -1, most = 0;
	for (const auto& kv : counts) if (kv.second > most) { most = kv.second; best = kv.first; }
	if (best < 0) return false;
	const Rgb sum = sums[best];
	const Rgb ground{ sum.r / most, sum.g / most, sum.b / most };
	Rgb ink = ground;
	double top_ = 1;
	for (int py = y0; py < y1; py++)
		for (int px = x0; px < x1; px++) {
			const Rgb c = at(px, py);
			const double r = ratio(c, ground);
			if (r > top_) { top_ = r; ink = c; }
		}
	out = Reading{ top_, ink, ground };
	return true;
}

// Every piece of text inside `w` (itself included), as drawn.
void textsIn(GtkWidget* w, std::vector<GtkWidget*>& out) {
	walk(w, [&](GtkWidget* x) {
		if (!(GTK_IS_LABEL(x) || GTK_IS_CELL_VIEW(x)) || !showing(x)) return;
		if (GTK_IS_LABEL(x) && gtk_label_get_text(GTK_LABEL(x))[0] == 0) return;
		out.push_back(x);
	});
}

std::string textOrKind(GtkWidget* w) {
	const std::string t = textOf(w);
	return t.empty() ? std::string(G_OBJECT_TYPE_NAME(w)) : "\"" + t + "\"";
}

struct StateTally { int n = 0, bad = 0; double worst = 100; };

// One state on every control of a kind in `top` at once, the window drawn,
// each control's text read; then the state taken off again.
void readInState(GtkWidget* top, const std::vector<GtkWidget*>& controls, GtkStateFlags state, const char* stateName, const std::string& where,
                 StateTally& tally) {
	if (controls.empty()) return;
	for (GtkWidget* c : controls) gtk_widget_set_state_flags(c, state, FALSE);
	pump(60);
	Shot shot;
	drawShot(top, shot);
	for (GtkWidget* c : controls) {
		std::vector<GtkWidget*> texts;
		textsIn(c, texts);
		for (GtkWidget* t : texts) {
			Reading r;
			if (!gtk_widget_is_sensitive(t) || !readDrawn(top, shot, t, r)) continue;
			tally.n++;
			tally.worst = std::min(tally.worst, r.ratio);
			if (r.ratio < 4.5) {
				tally.bad++;
				printf("  hard to read on %s, %s: %s %s in a %s: %s on %s, %.2f:1\n", where.c_str(), stateName, G_OBJECT_TYPE_NAME(t), textOrKind(t).c_str(),
				       G_OBJECT_TYPE_NAME(c), hex(r.fg).c_str(), hex(r.bg).c_str(), r.ratio);
			}
		}
	}
	for (GtkWidget* c : controls) gtk_widget_unset_state_flags(c, state);
	pump(30);
}

// The buttons, ticks, dots, dropdowns and rows of `root`, each lit
// (:hover), pressed (:active), ticked or open (:checked) and chosen
// (:selected) as it can be, read as drawn.
void checkStates(GtkWidget* root, const std::string& where) {
	std::vector<GtkWidget*> push, toggles, ticks, combos, rows;
	walk(root, [&](GtkWidget* w) {
		if (!showing(w) || !gtk_widget_is_sensitive(w)) return;
		if (GTK_IS_CHECK_BUTTON(w)) ticks.push_back(w);
		else if (GTK_IS_TOGGLE_BUTTON(w)) {
			// A dropdown's own button is the dropdown's, below.
			bool inCombo = false;
			for (GtkWidget* p = gtk_widget_get_parent(w); p && !inCombo; p = gtk_widget_get_parent(p)) inCombo = GTK_IS_COMBO_BOX(p);
			(inCombo ? combos : toggles).push_back(w);
		}
		else if (GTK_IS_BUTTON(w)) push.push_back(w);
		else if (GTK_IS_LIST_BOX_ROW(w)) rows.push_back(w);
	});
	struct Pass { const std::vector<GtkWidget*>* list; GtkStateFlags state; const char* name; };
	const Pass passes[] = {
		{ &push, GTK_STATE_FLAG_PRELIGHT, "a button lit (:hover)" },
		{ &push, (GtkStateFlags)(GTK_STATE_FLAG_PRELIGHT | GTK_STATE_FLAG_ACTIVE), "a button pressed (:active)" },
		{ &toggles, GTK_STATE_FLAG_PRELIGHT, "a toggle lit (:hover)" },
		{ &toggles, GTK_STATE_FLAG_CHECKED, "a toggle on (:checked)" },
		{ &toggles, (GtkStateFlags)(GTK_STATE_FLAG_ACTIVE | GTK_STATE_FLAG_PRELIGHT), "a toggle pressed (:active)" },
		{ &ticks, GTK_STATE_FLAG_PRELIGHT, "a box or dot lit (:hover)" },
		{ &ticks, (GtkStateFlags)(GTK_STATE_FLAG_CHECKED | GTK_STATE_FLAG_PRELIGHT), "a box or dot ticked and lit (:checked)" },
		{ &ticks, GTK_STATE_FLAG_ACTIVE, "a box or dot pressed (:active)" },
		{ &combos, GTK_STATE_FLAG_PRELIGHT, "a dropdown lit (:hover)" },
		{ &combos, GTK_STATE_FLAG_CHECKED, "a dropdown open (:checked)" },
		{ &rows, GTK_STATE_FLAG_PRELIGHT, "a row lit (:hover)" },
		{ &rows, GTK_STATE_FLAG_SELECTED, "a row chosen (:selected)" },
		{ &rows, (GtkStateFlags)(GTK_STATE_FLAG_SELECTED | GTK_STATE_FLAG_PRELIGHT), "a row chosen and lit (:selected:hover)" },
	};
	StateTally tally;
	GtkWidget* top = gtk_widget_get_toplevel(root);
	for (const Pass& p : passes) readInState(top, *p.list, p.state, p.name, where, tally);
	expect(tally.n > 0 && tally.bad == 0,
	       format("%s: lit, pressed, ticked, open and chosen, all %d readings of %zu buttons, %zu toggles, %zu boxes and dots, %zu dropdowns and "
	              "%zu rows read at 4.5:1 or better (the lowest %.2f:1)",
	              where.c_str(), tally.n, push.size(), toggles.size(), ticks.size(), combos.size(), rows.size(), tally.n ? tally.worst : 0.0));
}

// The menu showing now (a dropdown's list, a menu bar's menu): its window.
GtkWidget* openMenuWindow(GtkWidget* after = nullptr) {
	GtkWidget* found = nullptr;
	GList* all = gtk_window_list_toplevels();
	for (GList* l = all; l; l = l->next) {
		GtkWidget* pop = GTK_WIDGET(l->data);
		GtkWidget* child = GTK_IS_BIN(pop) ? gtk_bin_get_child(GTK_BIN(pop)) : nullptr;
		if (pop == after || !gtk_widget_get_mapped(pop) || !child || !GTK_IS_MENU(child)) continue;
		found = pop;
	}
	g_list_free(all);
	return found;
}

// Every row of an open menu (in `pop`, its window), as it is and lit in
// turn (the pointer on it), read as drawn; `deeper`: a row with a submenu
// opens it and its rows are read too.
void readMenu(GtkWidget* pop, const std::string& where, StateTally& plain, StateTally& lit, bool deeper) {
	GtkWidget* menu = gtk_bin_get_child(GTK_BIN(pop));
	std::vector<GtkWidget*> items;
	GList* kids = gtk_container_get_children(GTK_CONTAINER(menu));
	for (GList* k = kids; k; k = k->next) {
		GtkWidget* it = GTK_WIDGET(k->data);
		if (GTK_IS_MENU_ITEM(it) && !GTK_IS_SEPARATOR_MENU_ITEM(it) && gtk_widget_get_visible(it)) items.push_back(it);
	}
	g_list_free(kids);
	gtk_menu_shell_deselect(GTK_MENU_SHELL(menu));
	pump(60);
	auto readItem = [&](GtkWidget* it, const Shot& shot, StateTally& t, const char* how) {
		std::vector<GtkWidget*> texts;
		textsIn(it, texts);
		walk(it, [&](GtkWidget* x) { if (GTK_IS_ACCEL_LABEL(x) && std::find(texts.begin(), texts.end(), x) == texts.end()) texts.push_back(x); });
		for (GtkWidget* tx : texts) {
			Reading r;
			if (!readDrawn(pop, shot, tx, r)) continue;
			t.n++;
			t.worst = std::min(t.worst, r.ratio);
			if (r.ratio < 4.5) {
				t.bad++;
				printf("  hard to read in %s, %s: %s: %s on %s, %.2f:1\n", where.c_str(), how, textOrKind(tx).c_str(), hex(r.fg).c_str(), hex(r.bg).c_str(), r.ratio);
			}
		}
	};
	{
		Shot shot;
		drawShot(pop, shot);
		for (GtkWidget* it : items) if (gtk_widget_is_sensitive(it)) readItem(it, shot, plain, "a row");
	}
	for (GtkWidget* it : items) {
		if (!gtk_widget_is_sensitive(it)) continue;
		gtk_menu_shell_select_item(GTK_MENU_SHELL(menu), it);
		pump(50);
		Shot shot;
		drawShot(pop, shot);
		readItem(it, shot, lit, "a row lit (the pointer on it)");
		// The rows showing now (a long menu scrolls to the lit one), as they are.
		for (GtkWidget* other : items) if (other != it && gtk_widget_is_sensitive(other)) readItem(other, shot, plain, "a row");
		GtkWidget* sub = gtk_menu_item_get_submenu(GTK_MENU_ITEM(it));
		if (deeper && sub) {
			g_signal_emit_by_name(it, "activate-item");   // opens its submenu now
			pump(250);
			GtkWidget* subPop = gtk_widget_get_toplevel(sub);
			if (subPop && gtk_widget_get_mapped(subPop)) {
				const char* label = gtk_menu_item_get_label(GTK_MENU_ITEM(it));
				readMenu(subPop, where + " > " + (label ? label : "a submenu"), plain, lit, false);
				// The parent row stays lit with its submenu open: read again.
				Shot again;
				drawShot(pop, again);
				readItem(it, again, lit, "a row with its submenu open");
			}
			gtk_menu_shell_deselect(GTK_MENU_SHELL(sub));
			gtk_menu_popdown(GTK_MENU(sub));
			pump(80);
		}
	}
	gtk_menu_shell_deselect(GTK_MENU_SHELL(menu));
}

// A dropdown on `root`, opened, its rows read as they are and lit in turn.
void checkDropdownList(GtkWidget* combo, const std::string& where) {
	gtk_combo_box_popup(GTK_COMBO_BOX(combo));
	pump(400);
	GtkWidget* pop = openMenuWindow();
	if (!pop) { expect(false, where + ": a dropdown's list opens"); return; }
	StateTally plain, lit;
	readMenu(pop, where + "'s dropdown", plain, lit, false);
	gtk_combo_box_popdown(GTK_COMBO_BOX(combo));
	pump(200);
	expect(plain.n > 0 && plain.bad == 0 && lit.n > 0 && lit.bad == 0,
	       format("%s: a dropdown's list, all %d rows as they are and %d lit read at 4.5:1 or better (the lowest %.2f:1, lit %.2f:1)", where.c_str(),
	              plain.n, lit.n, plain.n ? plain.worst : 0.0, lit.n ? lit.worst : 0.0));
}

// ---- Fitting ---------------------------------------------------------------------------

bool isControlLike(GtkWidget* w) { return GTK_IS_COMBO_BOX(w) || GTK_IS_BUTTON(w) || GTK_IS_SCALE(w); }

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
	const GdkRectangle area = workArea(win);
	// Never more than the room it was given (usableArea), whatever its parts ask for.
	const GdkRectangle room = usableArea(win);
	int w = 0, h = 0;
	gtk_window_get_size(GTK_WINDOW(win), &w, &h);
	expect(w > 0 && w <= room.width && h <= room.height,
	       format("%s: the window, %dx%d with its own title bar, is no bigger than the %dx%d it has room for", where.c_str(), w, h, room.width, room.height));
	if (onWayland()) {
		// The desktop places it (an app can't, nor read where), and may put a
		// title bar of its own over it and a panel it doesn't tell GTK of:
		// all of that has to fit.
		const int titleBar = 36, panel = 44;
		expect(w <= area.width && h + titleBar + panel <= area.height,
		       format("%s: on Wayland, the window, %dx%d, with room for a title bar the desktop draws (%d) and a panel (%d), fits the %dx%d screen",
		              where.c_str(), w, h, titleBar, panel, area.width, area.height));
		return;
	}
	const GdkRectangle f = frameOf(win);
	expect(f.width > 0 && inside(f, area), where + ": the window, " + rect(f) + ", is on the screen's " + rect(area));
}

// When the page scrolls, no text or control runs under the scroll bar GTK
// lays over its right edge.
void checkClearOfBar(GtkWidget* sc, const std::string& where) {
	GtkWidget* sb = gtk_scrolled_window_get_vscrollbar(GTK_SCROLLED_WINDOW(sc));
	GtkAdjustment* v = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sc));
	if (!sb || !gtk_widget_get_mapped(sb) || gtk_adjustment_get_upper(v) <= gtk_adjustment_get_page_size(v) + 1) return;
	int bar = gtk_widget_get_allocated_width(sb);
	gtk_widget_get_preferred_width(sb, nullptr, &bar);
	const int clear = gtk_widget_get_allocated_width(sc) - bar;
	int n = 0, under = 0;
	walk(sc, [&](GtkWidget* w) {
		if (!(GTK_IS_LABEL(w) || GTK_IS_ENTRY(w) || isControlLike(w)) || !showing(w)) return;
		if (GTK_IS_LABEL(w) && gtk_label_get_text(GTK_LABEL(w))[0] == 0) return;
		int x = 0, y = 0;
		if (!gtk_widget_translate_coordinates(w, sc, gtk_widget_get_allocated_width(w), 0, &x, &y)) return;
		n++;
		if (x > clear) {
			under++;
			printf("  under the scroll bar on %s: %s \"%s\" ends at %d, the bar starts at %d\n", where.c_str(), G_OBJECT_TYPE_NAME(w),
			       GTK_IS_LABEL(w) ? gtk_label_get_text(GTK_LABEL(w)) : "", x, clear);
		}
	});
	expect(under == 0, format("%s: it scrolls, and none of its %d pieces runs under the %d-point scroll bar", where.c_str(), n, bar));
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

// --menu-check: every menu's text, as it is and lit, as drawn (see Settings.h).
int menuCheck(CircuitWindow* w, const std::string& dir, const std::string& tag) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	gDir = dir;
	gTag = tag.empty() ? std::string("screen") : tag;
	if (!dir.empty()) g_mkdir_with_parents(dir.c_str(), 0755);
	printf("menu-check: %s, the %s theme, the app %s, the accent's text %s\n", gTag.c_str(), themeName().c_str(), chrome().dark ? "dark" : "light",
	       chrome().onAccent().r > 0.5f ? "white" : "dark");
	GtkWidget* top = GTK_WIDGET(w->window());
	gtk_window_present(w->window());
	pump(400);
	MenuRow* row = w->menuRowWidget();
	GtkWidget* bar = row ? row->bar() : nullptr;
	expect(bar && showing(bar), "the window has its menu bar");
	if (!bar) return 1;
	std::vector<GtkWidget*> titles;
	{
		GList* kids = gtk_container_get_children(GTK_CONTAINER(bar));
		for (GList* k = kids; k; k = k->next) if (GTK_IS_MENU_ITEM(k->data) && gtk_widget_get_visible(GTK_WIDGET(k->data))) titles.push_back(GTK_WIDGET(k->data));
		g_list_free(kids);
	}
	// The titles: as they are, and lit with the pointer on them (no menu open).
	{
		StateTally plain, lit;
		Shot shot;
		drawShot(top, shot);
		for (GtkWidget* t : titles) {
			std::vector<GtkWidget*> texts;
			textsIn(t, texts);
			for (GtkWidget* x : texts) {
				Reading r;
				if (!readDrawn(top, shot, x, r)) continue;
				plain.n++;
				plain.worst = std::min(plain.worst, r.ratio);
				if (r.ratio < 4.5) { plain.bad++; printf("  hard to read: the title %s: %s on %s, %.2f:1\n", textOrKind(x).c_str(), hex(r.fg).c_str(), hex(r.bg).c_str(), r.ratio); }
			}
		}
		readInState(top, titles, GTK_STATE_FLAG_PRELIGHT, "a title lit (:hover)", "the menu bar", lit);
		expect(plain.n == (int)titles.size() && plain.bad == 0 && lit.n == (int)titles.size() && lit.bad == 0,
		       format("the menu bar's %zu titles read at 4.5:1 or better, as they are and lit (the lowest %.2f:1, lit %.2f:1)", titles.size(),
		              plain.n ? plain.worst : 0.0, lit.n ? lit.worst : 0.0));
	}
	// Each menu open (as Alt and its letter opens it), its title lit over it,
	// and every row as it is and lit, and every submenu's.
	int opened = 0;
	for (size_t i = 0; i < titles.size(); i++) {
		const char* name = gtk_menu_item_get_label(GTK_MENU_ITEM(titles[i]));
		const std::string where = std::string("the ") + (name ? name : "?") + " menu";
		if (!row->openMenu((int)i)) continue;
		pump(450);
		GtkWidget* pop = openMenuWindow();
		if (!pop) { expect(false, where + " opens"); continue; }
		opened++;
		shot(format("menu-%zu", i));
		StateTally title, plain, lit;
		{
			Shot shot2;
			drawShot(top, shot2);
			std::vector<GtkWidget*> texts;
			textsIn(titles[i], texts);
			for (GtkWidget* x : texts) {
				Reading r;
				if (!readDrawn(top, shot2, x, r)) continue;
				title.n++;
				title.worst = std::min(title.worst, r.ratio);
				if (r.ratio < 4.5) { title.bad++; printf("  hard to read: %s's title, open: %s on %s, %.2f:1\n", where.c_str(), hex(r.fg).c_str(), hex(r.bg).c_str(), r.ratio); }
			}
		}
		readMenu(pop, where, plain, lit, true);
		expect(title.n > 0 && title.bad == 0, format("%s: its title, lit while it's open, reads at %.2f:1", where.c_str(), title.n ? title.worst : 0.0));
		expect(plain.n > 0 && plain.bad == 0 && lit.n > 0 && lit.bad == 0,
		       format("%s: all %d pieces of text as they are and %d lit (the pointer on the row) read at 4.5:1 or better (the lowest %.2f:1, lit %.2f:1)",
		              where.c_str(), plain.n, lit.n, plain.n ? plain.worst : 0.0, lit.n ? lit.worst : 0.0));
		gtk_menu_shell_cancel(GTK_MENU_SHELL(bar));
		pump(250);
	}
	expect(opened == (int)titles.size() && opened >= 6, format("every menu opened (%d of %zu)", opened, titles.size()));
	// The canvas's right-click menus, as the app makes them: on the canvas, and on a part.
	for (int target : { -1, (int)CL_CONTEXT_GATE }) {
		const std::string where = target < 0 ? "the canvas's right-click menu" : "a part's right-click menu";
		GtkWidget* before = openMenuWindow();
		// (A part's menu with parts selected, so its rows are live.)
		if (target >= 0) w->runAction("win.select-all");
		w->showContextMenu(target, 0, 0, nullptr);
		pump(400);
		GtkWidget* pop = openMenuWindow(before);
		if (!pop) { expect(false, where + " opens"); continue; }
		if (target < 0) shot("menu-context");
		StateTally plain, lit;
		readMenu(pop, where, plain, lit, false);
		expect(plain.n > 0 && plain.bad == 0 && lit.n > 0 && lit.bad == 0,
		       format("%s: all %d pieces of text as they are and %d lit read at 4.5:1 or better (the lowest %.2f:1, lit %.2f:1)", where.c_str(), plain.n,
		              lit.n, plain.n ? plain.worst : 0.0, lit.n ? lit.worst : 0.0));
		gtk_menu_shell_deactivate(GTK_MENU_SHELL(gtk_bin_get_child(GTK_BIN(pop))));
		pump(250);
	}
	// A menu with every kind of row the app's menus have (ticks, dots, a
	// shortcut, a submenu, one greyed), and a dropdown's list, in a window
	// of their own; and a menu drawn as a popover.
	{
		GtkWidget* win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
		gtk_window_set_default_size(GTK_WINDOW(win), 360, 240);
		GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
		GtkWidget* combo = gtk_combo_box_text_new();
		for (const char* t : { "Short intro", "Full intro", "Classic launch screen", "None" }) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), t);
		gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
		gtk_widget_set_halign(combo, GTK_ALIGN_START);
		GtkWidget* anchor = gtk_button_new_with_label("Anchor");
		gtk_widget_set_halign(anchor, GTK_ALIGN_START);
		gtk_box_pack_start(GTK_BOX(box), combo, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(box), anchor, FALSE, FALSE, 0);
		gtk_container_add(GTK_CONTAINER(win), box);
		gtk_widget_show_all(win);
		pump(400);
		checkDropdownList(combo, "a dropdown in a window of its own");

		GtkWidget* menu = gtk_menu_new();
		GtkAccelGroup* accel = gtk_accel_group_new();
		gtk_menu_set_accel_group(GTK_MENU(menu), accel);
		GtkWidget* plainItem = gtk_menu_item_new_with_mnemonic("_Rotate");
		GtkWidget* keyed = gtk_menu_item_new_with_mnemonic("_Copy");
		gtk_widget_add_accelerator(keyed, "activate", accel, GDK_KEY_c, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
		GtkWidget* tick = gtk_check_menu_item_new_with_mnemonic("_Show the Grid");
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(tick), TRUE);
		GtkWidget* dot = gtk_check_menu_item_new_with_mnemonic("_Gates and Wires");
		gtk_check_menu_item_set_draw_as_radio(GTK_CHECK_MENU_ITEM(dot), TRUE);
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(dot), TRUE);
		GtkWidget* parent = gtk_menu_item_new_with_mnemonic("Co_nnect to");
		GtkWidget* sub = gtk_menu_new();
		for (const char* t : { "Page 1", "Page 2" }) {
			GtkWidget* it = gtk_check_menu_item_new_with_label(t);
			gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(it), t[5] == '1');
			gtk_menu_shell_append(GTK_MENU_SHELL(sub), it);
		}
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent), sub);
		GtkWidget* off = gtk_menu_item_new_with_mnemonic("_Unlock Part");
		gtk_widget_set_sensitive(off, FALSE);
		for (GtkWidget* it : { plainItem, keyed, tick, dot }) gtk_menu_shell_append(GTK_MENU_SHELL(menu), it);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), parent);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), off);
		gtk_menu_attach_to_widget(GTK_MENU(menu), win, nullptr);
		gtk_widget_show_all(menu);
		gtk_menu_popup_at_widget(GTK_MENU(menu), anchor, GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, nullptr);
		pump(400);
		if (GtkWidget* pop = openMenuWindow()) {
			shot("menu-kinds");
			StateTally plain, lit;
			readMenu(pop, "a menu of every kind of row", plain, lit, true);
			expect(plain.n > 0 && plain.bad == 0 && lit.n > 0 && lit.bad == 0,
			       format("a menu of every kind of row (ticks, dots, a shortcut, a submenu): all %d pieces of text as they are and %d lit read at "
			              "4.5:1 or better (the lowest %.2f:1, lit %.2f:1)",
			              plain.n, lit.n, plain.n ? plain.worst : 0.0, lit.n ? lit.worst : 0.0));
			gtk_menu_popdown(GTK_MENU(menu));
			pump(200);
		} else {
			expect(false, "a menu of every kind of row opens");
		}
		g_object_unref(accel);
		gtk_widget_destroy(menu);
		gtk_widget_destroy(win);
		pump(200);
	}
	// A menu drawn as a popover (a model's menu, from a button), its rows as they are and lit.
	{
		GMenu* model = g_menu_new();
		g_menu_append(model, "Zoom to Fit", "win.zoom-fit");
		g_menu_append(model, "Select All", "win.select-all");
		g_menu_append(model, "Learn Logic Online…", "app.learn-online");
		GtkWidget* relative = row->widget();
		GtkWidget* pover = gtk_popover_new_from_model(relative, G_MENU_MODEL(model));
		gtk_popover_set_transitions_enabled(GTK_POPOVER(pover), FALSE);
		gtk_popover_popup(GTK_POPOVER(pover));
		pump(500);
		std::vector<GtkWidget*> buttons;
		walk(pover, [&](GtkWidget* x) { if (G_TYPE_CHECK_INSTANCE_TYPE(x, g_type_from_name("GtkModelButton")) && showing(x)) buttons.push_back(x); });
		StateTally plain, lit;
		if (!buttons.empty()) {
			Shot s0;
			drawShot(pover, s0);
			for (GtkWidget* b : buttons) {
				std::vector<GtkWidget*> texts;
				textsIn(b, texts);
				for (GtkWidget* x : texts) {
					Reading r;
					if (!readDrawn(pover, s0, x, r)) continue;
					plain.n++;
					plain.worst = std::min(plain.worst, r.ratio);
					if (r.ratio < 4.5) { plain.bad++; printf("  hard to read in a popover: %s: %s on %s, %.2f:1\n", textOrKind(x).c_str(), hex(r.fg).c_str(), hex(r.bg).c_str(), r.ratio); }
				}
			}
			readInState(pover, buttons, GTK_STATE_FLAG_PRELIGHT, "a row lit (:hover)", "a popover", lit);
		}
		expect(buttons.size() == 3 && plain.n > 0 && plain.bad == 0 && lit.n > 0 && lit.bad == 0,
		       format("a menu as a popover: its %zu rows read at 4.5:1 or better, as they are and lit (the lowest %.2f:1, lit %.2f:1)", buttons.size(),
		              plain.n ? plain.worst : 0.0, lit.n ? lit.worst : 0.0));
		gtk_widget_destroy(pover);
		g_object_unref(model);
		pump(200);
	}
	printf("%s\n", gFailed == 0 ? "menu-check: all passed" : format("menu-check: %d failed", gFailed).c_str());
	return gFailed == 0 ? 0 : 1;
}

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
		// A page that scrolls: its end too, so the picture shows the last setting.
		if (GtkWidget* sc = pageScroller()) {
			GtkAdjustment* v = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sc));
			if (gtk_adjustment_get_upper(v) > gtk_adjustment_get_page_size(v) + 1) {
				gtk_adjustment_set_value(v, gtk_adjustment_get_upper(v) - gtk_adjustment_get_page_size(v));
				pump(250);
				shot(format("settings-%d-end", page));
				gtk_adjustment_set_value(v, 0);
				pump(100);
			}
		}
		checkFits(win, where);
		const int ww = gtk_widget_get_allocated_width(win);
		if (width < 0) width = ww;
		sameWidth = sameWidth && ww == width;
		GtkWidget* sc = pageScroller();
		GtkWidget* pg = currentPage();
		if (sc && pg) {
			int minW = 0, natW = 0;
			gtk_widget_get_preferred_width(pg, &minW, &natW);
			// What the page has across: the scroller less its scroll bar, where
			// the theme's isn't an overlay (PiXflat's takes 30 points).
			GtkWidget* view = gtk_bin_get_child(GTK_BIN(sc));
			const int room = gtk_widget_get_allocated_width(view ? view : sc);
			expect(minW <= room, format("%s: its content needs %d of the %d points it has across", where.c_str(), minW, room));
			int minH = 0, natH = 0;
			gtk_widget_get_preferred_height_for_width(pg, room, &minH, &natH);
			GtkAdjustment* v = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sc));
			expect(gtk_adjustment_get_upper(v) + 1 >= minH,
			       format("%s: all %d points of it can be reached (%d shown, scrolling to %.0f)", where.c_str(), minH,
			              gtk_widget_get_allocated_height(sc), gtk_adjustment_get_upper(v)));
		}
		if (sc && pg) checkClearOfBar(sc, where);
		checkText(win, where);
		checkStates(win, where);
		// A dropdown's list open (the first on the page, and "When CedarLogic opens").
		{
			std::vector<GtkWidget*> combos;
			walk(win, [&](GtkWidget* x) {
				if (GTK_IS_COMBO_BOX(x) && showing(x) && gtk_widget_is_sensitive(x) &&
				    (combos.empty() || strcmp(gtk_widget_get_name(x), "launch-choice") == 0))
					combos.push_back(x);
			});
			// (Not on Wayland: a menu there opens only from a real click, which this has none of.)
			if (onWayland()) combos.clear();
			for (GtkWidget* c : combos) {
				if (GtkWidget* sc2 = scrollerOf(c)) {
					// In view, as a pointer would need it.
					GtkAdjustment* v = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sc2));
					int cx = 0, cy = 0;
					if (GtkWidget* content = gtk_bin_get_child(GTK_BIN(sc2))) gtk_widget_translate_coordinates(c, content, 0, 0, &cx, &cy);
					gtk_adjustment_set_value(v, std::max(0.0, std::min(gtk_adjustment_get_upper(v) - gtk_adjustment_get_page_size(v),
					                                                   cy - gtk_adjustment_get_page_size(v) / 3)));
					pump(80);
				}
				checkDropdownList(c, where + (strcmp(gtk_widget_get_name(c), "launch-choice") == 0 ? " (When CedarLogic opens)" : ""));
			}
			if (GtkWidget* sc2 = pageScroller()) gtk_adjustment_set_value(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sc2)), 0);
		}
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
		checkStates(cw, "Customize Actions");
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
