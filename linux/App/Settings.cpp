// Settings (see Settings.h).

#include "Settings.h"
#include "Anim.h"
#include "Canvas.h"
#include "Collections.h"
#include "Drawn.h"
#include "Shortcuts.h"
#include "TitleButtons.h"
#include "Toolbar.h"
#include "Window.h"

#include <algorithm>
#include <functional>
#include <memory>

namespace settings {

namespace {

enum Page { General, Appearance, CanvasPage, ToolbarPage, ShortcutsPage, PageCount };
const char* kTitles[] = { "General", "Appearance", "Canvas", "Toolbar", "Shortcuts" };
const char* kIcons[] = { "preferences-system-symbolic", "applications-graphics-symbolic", "input-mouse-symbolic",
                         "view-more-horizontal-symbolic", "input-keyboard-symbolic" };
const float kBarH = 78, kWidth = 640;

// Every window takes the change.
void apply() {
	prefs().applyWireDots();
	prefs().save();
	for (CircuitWindow* w : circuitWindows()) w->prefsChanged();
}

CircuitWindow* front() {
	for (CircuitWindow* w : circuitWindows()) if (gtk_window_is_active(w->window())) return w;
	return circuitWindows().empty() ? nullptr : circuitWindows().front();
}

// A callback kept with its widget (for GTK's C signals).
void on(GtkWidget* w, const char* signal, std::function<void()> f) {
	auto* fn = new std::function<void()>(std::move(f));
	g_object_set_data_full(G_OBJECT(w), signal, fn, [](gpointer p) { delete static_cast<std::function<void()>*>(p); });
	g_object_set_data(G_OBJECT(w), "cl-signal", (gpointer)signal);
	g_signal_connect(w, signal, CL_CALLBACK(+[](GtkWidget* self) {
		const char* sig = static_cast<const char*>(g_object_get_data(G_OBJECT(self), "cl-signal"));
		auto* fn = static_cast<std::function<void()>*>(g_object_get_data(G_OBJECT(self), sig));
		if (fn) guarded("a setting", [&] { (*fn)(); });
	}), nullptr);
}

GtkWidget* combo(const std::vector<std::string>& items, int active, std::function<void(int)> changed) {
	GtkWidget* c = gtk_combo_box_text_new();
	for (const std::string& i : items) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c), i.c_str());
	gtk_combo_box_set_active(GTK_COMBO_BOX(c), active);
	gtk_widget_set_halign(c, GTK_ALIGN_START);
	on(c, "changed", [c, changed] { changed(gtk_combo_box_get_active(GTK_COMBO_BOX(c))); });
	return c;
}

GtkWidget* check(const char* label, bool value, std::function<void(bool)> changed) {
	GtkWidget* c = gtk_check_button_new_with_label(label);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(c), value);
	gtk_widget_set_halign(c, GTK_ALIGN_START);
	on(c, "toggled", [c, changed] { changed(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(c))); });
	return c;
}

GtkWidget* slider(double lo, double hi, double step, double value, const char* fmt, std::function<void(double)> changed) {
	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget* s = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, lo, hi, step);
	gtk_scale_set_draw_value(GTK_SCALE(s), FALSE);
	gtk_range_set_value(GTK_RANGE(s), value);
	gtk_widget_set_size_request(s, 170, -1);
	GtkWidget* v = gtk_label_new(format(fmt, value).c_str());
	gtk_style_context_add_class(gtk_widget_get_style_context(v), "dim-label");
	gtk_box_pack_start(GTK_BOX(box), s, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), v, FALSE, FALSE, 0);
	std::string f = fmt;
	on(s, "value-changed", [s, v, f, changed] {
		const double x = gtk_range_get_value(GTK_RANGE(s));
		gtk_label_set_text(GTK_LABEL(v), format(f.c_str(), x).c_str());
		changed(x);
	});
	gtk_widget_set_halign(box, GTK_ALIGN_START);
	return box;
}

GtkWidget* button(const char* label, std::function<void()> clicked) {
	GtkWidget* b = gtk_button_new_with_label(label);
	gtk_widget_set_halign(b, GTK_ALIGN_START);
	on(b, "clicked", std::move(clicked));
	return b;
}

// One page: "Label:" on the left, the control and a line of help on the right.
struct Rows {
	GtkWidget* grid;
	int row = 0;
	Rows() {
		grid = gtk_grid_new();
		gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
		gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
		gtk_widget_set_margin_start(grid, 28);
		gtk_widget_set_margin_end(grid, 28);
		gtk_widget_set_margin_top(grid, 22);
		gtk_widget_set_margin_bottom(grid, 24);
	}
	GtkWidget* add(const char* label, GtkWidget* control, const std::string& hint = "") {
		GtkWidget* l = gtk_label_new(*label ? (std::string(label) + ":").c_str() : "");
		gtk_label_set_xalign(GTK_LABEL(l), 1);
		gtk_label_set_yalign(GTK_LABEL(l), 0);
		gtk_widget_set_size_request(l, 150, -1);
		gtk_widget_set_margin_top(l, 5);
		gtk_widget_set_valign(l, GTK_ALIGN_START);
		gtk_grid_attach(GTK_GRID(grid), l, 0, row, 1, 1);
		GtkWidget* v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
		gtk_box_pack_start(GTK_BOX(v), control, FALSE, FALSE, 0);
		GtkWidget* h = nullptr;
		if (!hint.empty()) {
			h = gtk_label_new(hint.c_str());
			gtk_style_context_add_class(gtk_widget_get_style_context(h), "hint");
			gtk_label_set_xalign(GTK_LABEL(h), 0);
			gtk_label_set_line_wrap(GTK_LABEL(h), TRUE);
			gtk_label_set_max_width_chars(GTK_LABEL(h), 58);
			gtk_widget_set_size_request(h, 360, -1);
			gtk_box_pack_start(GTK_BOX(v), h, FALSE, FALSE, 0);
		}
		gtk_grid_attach(GTK_GRID(grid), v, 1, row, 1, 1);
		row++;
		return h;
	}
};

// A drawn widget lives as long as its widget does.
template <class T> GtkWidget* owned(T* d) {
	g_signal_connect(d->widget(), "destroy", CL_CALLBACK(+[](GtkWidget*, gpointer p) { delete static_cast<T*>(p); }), d);
	return d->widget();
}

// ---- The app's colour: a row of swatches ----------------------------------------------

class Swatches : public Drawn {
public:
	Swatches() {
		create();
		gtk_widget_set_size_request(area, 7 * 60 + 8, 52);
		gtk_widget_set_halign(area, GTK_ALIGN_START);
	}

protected:
	void paint(cairo_t* cr, float, float h) override {
		static const int order[] = { 6, 0, 1, 2, 3, 4, 5 };
		static const char* names[] = { "Blue", "Purple", "Pink", "Orange", "Green", "Graphite", "CedarLogic" };
		const Chrome c = chrome();
		for (int i = 0; i < 7; i++) {
			const int a = order[i];
			double r, g, b;
			cl_accent_color(a, c.dark, &r, &g, &b);
			const Color col = colorF((float)r, (float)g, (float)b);
			const PointF mid = pointF(34 + i * 60, 16);
			const bool on = prefs().accent == a;
			const float hot = (float)fade.amount(i);
			if (on) strokeCircle(cr, mid, 15, withAlpha(c.barInk(), 0.85f), 2);
			fillCircle(cr, mid, 11 + hot, col);
			if (on) fillCircle(cr, mid, 4, colorF(1, 1, 1, 0.95f));
			drawText(cr, names[a], rectF(mid.x - 34, h - 16, mid.x + 34, h), 10.5f, withAlpha(c.barInk(), on ? 0.95f : 0.75f), TextAlign::Center, on);
		}
	}
	void mouseMove(float x, float) override { fade.setHot(std::min(6, std::max(0, (int)((x - 4) / 60)))); animate(); }
	void mouseLeave() override { fade.setHot(-1); animate(); }
	void mouseDown(int button, float x, float, bool, GdkEventButton*) override {
		static const int order[] = { 6, 0, 1, 2, 3, 4, 5 };
		if (button != 1) return;
		const int i = (int)((x - 4) / 60);
		if (i < 0 || i > 6) return;
		prefs().accent = order[i];
		prefs().save();
		applyTheme();
		apply();
		redraw();
	}
	bool animating() override { return fade.active(); }

private:
	anim::HoverFade fade;
};

// ---- A toolbar style, as a picture -------------------------------------------------

class StylePicture : public Drawn {
public:
	explicit StylePicture(int s) : style(s) {
		create();
		gtk_widget_set_size_request(area, (int)(520 * 0.82f), (int)(48 * 0.82f));
		gtk_widget_set_halign(area, GTK_ALIGN_START);
	}

protected:
	void paint(cairo_t* cr, float w, float h) override {
		const float k = 0.74f;
		cairo_save(cr);
		roundedPath(cr, rectF(0, 0, w, h), 8);
		cairo_clip(cr);
		CircuitWindow* f = front();
		if (f && f->toolbarWidget()) {
			cairo_scale(cr, k, k);
			f->toolbarWidget()->paintPicture(cr, w / k, h / k, style);
		} else {
			fillRect(cr, rectF(0, 0, w, h), withAlpha(chrome().barInk(), 0.1f));
		}
		cairo_restore(cr);
		strokeRound(cr, rectF(0.5f, 0.5f, w - 0.5f, h - 0.5f), 8, withAlpha(chrome().barInk(), 0.15f));
	}

private:
	int style;
};

// ---- The page bar (the window's top row) -------------------------------------------

struct SettingsWindow;
void select(SettingsWindow* s, int page, bool animated);

class PageBar : public Drawn {
public:
	PageBar(SettingsWindow* s) : owner(s) {
		create();
		gtk_widget_set_size_request(area, (int)kWidth, (int)kBarH);
		buttons.load();
		// Settings has no maximize.
		buttons.list.erase(std::remove_if(buttons.list.begin(), buttons.list.end(), [](const TitleButtons::Button& b) {
			return b.kind == TitleButtons::Max; }), buttons.list.end());
		hover.in = 0.12;
		hover.out = 0.18;
		pick.in = pick.out = 0.2;
	}
	int page = General;

protected:
	void paint(cairo_t* cr, float w, float h) override;
	void mouseMove(float x, float y) override {
		buttons.setHot(buttons.at(x, y));
		hover.setHot(itemAt(x, y));
		animate();
	}
	void mouseLeave() override { buttons.setHot(-1); hover.setHot(-1); animate(); }
	void mouseDown(int b, float x, float y, bool dbl, GdkEventButton* e) override;
	void mouseUp(int b, float x, float y) override {
		if (b != 1 || buttons.pressed < 0) return;
		const int i = buttons.pressed;
		buttons.pressed = -1;
		if (buttons.at(x, y) == i) buttons.activate(i, GTK_WINDOW(gtk_widget_get_toplevel(area)));
	}
	bool animating() override { return hover.active() || pick.active() || buttons.animating(); }

private:
	SettingsWindow* owner;
	TitleButtons buttons;
	anim::HoverFade hover, pick;
	RectF itemRect(int i, float w) const {
		const float iw = 74, total = iw * PageCount;
		const float x = (w - total) / 2 + i * iw;
		return rectF(x + 2, 26, x + iw - 2, kBarH - 6);
	}
	int itemAt(float x, float y) const {
		for (int i = 0; i < PageCount; i++) if (inRect(itemRect(i, width()), x, y)) return i;
		return -1;
	}
};

void PageBar::paint(cairo_t* cr, float w, float h) {
	const Chrome c = chrome();
	const Color ink = c.barInk();
	cairo_pattern_t* g = cairo_pattern_create_linear(0, 0, 0, h);
	const Color top = c.tabBarTop(), bottom = c.tabBar();
	cairo_pattern_add_color_stop_rgb(g, 0, top.r, top.g, top.b);
	cairo_pattern_add_color_stop_rgb(g, 1, bottom.r, bottom.g, bottom.b);
	cairo_rectangle(cr, 0, 0, w, h);
	cairo_set_source(cr, g);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	fillRect(cr, rectF(0, h - 1, w, h), withAlpha(ink, 0.12f));
	drawTextMid(cr, kTitles[page], rectF(0, 2, w, 24), 12.5f, withAlpha(ink, 0.85f), TextAlign::Center, true);
	pick.setHot(page);
	const Color accent = c.accent();
	for (int i = 0; i < PageCount; i++) {
		const RectF r = itemRect(i, w);
		const float on = (float)pick.amount(i), hot = (float)hover.amount(i);
		fillRound(cr, r, 9, withAlpha(on > 0.01f ? accent : ink, on > 0.01f ? 0.16f * on : 0.07f * hot));
		const Color fg = on > 0.5f ? accent : withAlpha(ink, 0.75f + 0.2f * hot);
		drawIcon(cr, kIcons[i], rectF(r.left, r.top + 4, r.right, r.top + 26), 20, fg);
		drawTextMid(cr, kTitles[i], rectF(r.left, r.top + 28, r.right, r.bottom - 2), 10.5f, fg, TextAlign::Center, on > 0.5f);
	}
	buttons.layout(w, 30);
	buttons.paint(cr, GTK_WINDOW(gtk_widget_get_toplevel(area)), ink);
	if (pick.active()) animate();
}

// ---- The window --------------------------------------------------------------------

struct SettingsWindow {
	GtkWidget* win = nullptr;
	GtkWidget* holder = nullptr;   // the page, sized as it eases
	GtkWidget* pageWidget = nullptr;
	PageBar* bar = nullptr;
	int page = General;
	anim::Tween height, opacity;
	guint tick = 0;
	// The Shortcuts page: the action being given new keys.
	const shortcuts::Action* recording = nullptr;
	GtkWidget* recordButton = nullptr;
	GtkWidget* shortcutNote = nullptr;
	GtkWidget* shortcutList = nullptr;
	std::string shortcutSearch;
};

SettingsWindow* g_settings = nullptr;

// ---- Pages -------------------------------------------------------------------------

GtkWidget* generalPage() {
	Rows r;
	Prefs& p = prefs();
	GtkWidget* name = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(name), p.studentName.c_str());
	gtk_entry_set_placeholder_text(GTK_ENTRY(name), "First and last name");
	gtk_widget_set_size_request(name, 240, -1);
	gtk_widget_set_halign(name, GTK_ALIGN_START);
	on(name, "changed", [name] { prefs().studentName = gtk_entry_get_text(GTK_ENTRY(name)); prefs().save(); });
	r.add("Your name", name, "Printed under your circuit when you export it as an image, and on the Lab Page template.");

	GtkWidget** openHint = new GtkWidget*(nullptr);
	auto openText = [] {
		return std::string(prefs().openReplaces
			? "New and opened circuits take the place of the one you're in, like classic CedarLogic. It's saved first, as everything is."
			: "New and opened circuits get a window of their own, so you can have several open side by side.");
	};
	*openHint = r.add("Opening a circuit", combo({ "Replace the current one", "Open in a new window" }, p.openReplaces ? 0 : 1, [openHint, openText](int i) {
		prefs().openReplaces = i == 0;
		prefs().save();
		if (*openHint) gtk_label_set_text(GTK_LABEL(*openHint), openText().c_str());
	}), openText());

	std::vector<std::string> names = { "A blank page" };
	std::vector<std::string> ids = { "" };
	for (const auto& t : templates::list()) { names.push_back(t.second); ids.push_back(t.first); }
	int at = 0;
	for (size_t i = 0; i < ids.size(); i++) if (ids[i] == p.newTemplate) at = (int)i;
	r.add("New circuits", combo(names, at, [ids](int i) {
		if (i >= 0 && i < (int)ids.size()) { prefs().newTemplate = ids[i]; prefs().save(); }
	}), "Ctrl+N starts a blank page, or every new circuit from the template you pick. (New from Template… still offers them all.)");

	// Double-clicking .cdl files.
	auto isUs = [] {
		gchar* out = nullptr;
		const gchar* argv[] = { "xdg-mime", "query", "default", "application/x-cedarlogic-circuit", nullptr };
		g_spawn_sync(nullptr, (gchar**)argv, nullptr, (GSpawnFlags)(G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL), nullptr, nullptr, &out,
		             nullptr, nullptr, nullptr);
		const bool us = out && g_strrstr(out, "cedarlogic");
		g_free(out);
		return us;
	};
	GtkWidget* files = gtk_button_new_with_label(isUs() ? "CedarLogic opens them" : "Open them with CedarLogic");
	gtk_widget_set_halign(files, GTK_ALIGN_START);
	gtk_widget_set_sensitive(files, !isUs());
	on(files, "clicked", [files, isUs] {
		const gchar* argv[] = { "xdg-mime", "default", "cedarlogic.desktop", "application/x-cedarlogic-circuit", nullptr };
		g_spawn_sync(nullptr, (gchar**)argv, nullptr, (GSpawnFlags)(G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL),
		             nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
		const bool us = isUs();
		gtk_button_set_label(GTK_BUTTON(files), us ? "CedarLogic opens them" : "Open them with CedarLogic");
		gtk_widget_set_sensitive(files, !us);
	});
	r.add("Opening files", files, "Double-clicking a .cdl file in your files opens it in CedarLogic.");
	r.add("Quitting", check("Ask before quitting", p.confirmQuit, [](bool on) { prefs().confirmQuit = on; prefs().save(); }),
	      "Ctrl+Q shows a question first: Enter quits, Escape doesn't.");
	r.add("Status bar", check("Show zoom, cursor position, and counts", p.showStatus, [](bool on) { prefs().showStatus = on; apply(); }),
	      "The readout in the bottom-right corner of the window. Notes (Saved, Copied…) still show there for a moment either way.");
	GtkWidget* upd = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_box_pack_start(GTK_BOX(upd), check("Check for new versions", p.checkUpdates, [](bool on) { prefs().checkUpdates = on; prefs().save(); }),
	                   FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(upd), button("Check Now", [] {
		if (CircuitWindow* w = front()) g_action_group_activate_action(G_ACTION_GROUP(w->application()), "check-updates", nullptr);
	}), FALSE, FALSE, 0);
	r.add("Updates", upd, "New test builds install from inside the app: it asks first, and keeps your circuits.");
	return r.grid;
}

GtkWidget* appearancePage() {
	Rows r;
	Prefs& p = prefs();
	r.add("Theme at launch", combo({ "Match System", "Light", "Dark", "Same as Last Time" }, p.themeMode, [](int i) {
		prefs().themeMode = i;
		if (i == 1) prefs().dark = false;
		else if (i == 2) prefs().dark = true;
		else if (i == 0) prefs().dark = systemPrefersDark();
		prefs().save();
		applyTheme();
	}), "Which theme the app opens in. The View menu and Ctrl+Shift+D switch it any time.");
	r.add("App colour", owned(new Swatches()),
	      "Selections, highlights and buttons. CedarLogic is the icon's green. Wire colours that show signal state never change.");
	r.add("Tabs", combo({ "Modern", "Classic" }, p.classicTabs ? 1 : 0, [](int i) { prefs().classicTabs = i == 1; apply(); }),
	      "Modern tabs drag to reorder or to split the view, and rename with a double-click. Classic are plain segments.");
	r.add("Canvas", check("Show the grid", p.showGrid, [](bool on) { prefs().showGrid = on; apply(); }),
	      "The background grid gates snap to. Printing never includes it.");
	r.add("Grid style", combo({ "Lines", "Dots" }, p.gridStyle, [](int i) { prefs().gridStyle = i; apply(); }),
	      "Dots are quieter; lines make alignment easier to see.");
	r.add("", check("Darker line every 5 squares", p.majorGrid, [](bool on) { prefs().majorGrid = on; apply(); }),
	      "Makes distances easy to judge at a glance.");
	r.add("Wire thickness", combo({ "Thin", "Normal", "Thick" }, p.wireThickness, [](int i) { prefs().wireThickness = i; apply(); }),
	      "On screen only. Printouts always use the standard weight.");
	r.add("Low wires", combo({ "Silver", "Slate blue", "Soft white", "Classic grey" }, p.lowWire, [](int i) { prefs().lowWire = i; apply(); }),
	      "The colour of wires carrying a 0 on the dark background, so they stand apart from the grid. Light mode keeps black.");
	r.add("", check("Show dots at wire bends", p.wireDots, [](bool on) { prefs().wireDots = on; apply(); }),
	      "Marks every corner of a wire. Junctions where wires join always get a dot.");
	r.add("Wire dot size", slider(0.08, 0.4, 0.01, p.wireDotSize, "%.2f", [](double v) { prefs().wireDotSize = v; apply(); }),
	      "Radius of the dots on wires, in grid units.");
	r.add("Gate size", slider(36, 96, 1, p.gateSize, "%.0f", [](double v) { prefs().gateSize = (int)v; apply(); }),
	      "How big the gates in the side panel are; the panel widens to fit them.");
	GtkWidget* side = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_box_pack_start(GTK_BOX(side), check("Show gate names", p.showGateNames, [](bool on) { prefs().showGateNames = on; apply(); }), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(side), check("Show category shortcuts", p.showCategoryKeys, [](bool on) { prefs().showCategoryKeys = on; apply(); }),
	                   FALSE, FALSE, 0);
	r.add("Side panel", side, "The names under the gates, and Shift+1…Shift+0 beside the categories that jump to them.");
	return r.grid;
}

GtkWidget* canvasPage() {
	Rows r;
	Prefs& p = prefs();
	r.add("Wires", check("Show a wire's value when you rest on it", p.wireValueTag, [](bool on) { prefs().wireValueTag = on; apply(); }),
	      "Pointing at a wire always lights up all of it. This also shows what it carries after a moment: 0, 1, Z (floating) or ! (a conflict).");
	r.add("Mouse wheel", combo({ "Zooms", "Moves around" }, p.mouseWheel, [](int i) { prefs().mouseWheel = i; apply(); }));
	r.add("", check("Reverse zoom direction", p.reverseWheel, [](bool on) { prefs().reverseWheel = on; apply(); }),
	      "Rolling the wheel away from you zooms in. Turn this on if it zooms out for you instead (some mice and settings reverse the wheel).");
	r.add("Touchpad scroll", combo({ "Zooms", "Moves around" }, p.touchpadScroll, [](int i) { prefs().touchpadScroll = i; apply(); }),
	      "Pinching always zooms.");
	r.add("", check("Reverse zoom direction", p.reverseTouchpad, [](bool on) { prefs().reverseTouchpad = on; apply(); }),
	      "Only matters when touchpad scrolling zooms. Ctrl+scroll always zooms; Shift+scroll always moves sideways.");
	r.add("Right-click", check("Rotates the gate", p.rightClickRotate, [](bool on) { prefs().rightClickRotate = on; apply(); }),
	      "Off: right-clicking a gate opens a menu with Rotate, Delete and more.");
	r.add("Duplicate", combo({ "Leaves the clipboard alone", "Copies to the clipboard too" }, p.duplicateUsesClipboard ? 1 : 0,
	                         [](int i) { prefs().duplicateUsesClipboard = i == 1; apply(); }),
	      "Second option: the copy stays on the clipboard, so paste makes more of it.");
	r.add("Tidy Up", combo({ "Keeps my layout", "Rearranges everything" }, p.tidyMode, [](int i) { prefs().tidyMode = i; apply(); }),
	      "Keeps my layout: lines gates up where they are. Rearranges everything: lays the circuit out by signal flow. The Edit menu always has the other one.");
	return r.grid;
}

GtkWidget* toolbarPage() {
	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_margin_start(box, 28);
	gtk_widget_set_margin_end(box, 28);
	gtk_widget_set_margin_top(box, 18);
	gtk_widget_set_margin_bottom(box, 22);
	struct Style { int id; const char* name; const char* blurb; };
	static const Style styles[] = {
		{ 3, "Seamless", "Blends into the canvas like one surface. Tools stay quiet until you point at them." },
		{ 0, "Classic", "Tools in tidy rounded groups, everything in reach." },
		{ 2, "Minimal", "Just the essentials and your file name; the rest is behind the ••• menu." },
	};
	GSList* group = nullptr;
	for (const Style& st : styles) {
		GtkWidget* item = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
		gtk_widget_set_margin_top(item, 6);
		gtk_widget_set_margin_bottom(item, 6);
		GtkWidget* radio = gtk_radio_button_new_with_label(group, st.name);
		group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(radio));
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio), prefs().toolbarStyle == st.id);
		gtk_style_context_add_class(gtk_widget_get_style_context(radio), "style-name");
		const int id = st.id;
		on(radio, "toggled", [radio, id] {
			if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radio))) return;
			prefs().toolbarStyle = id;
			apply();
		});
		GtkWidget* blurb = gtk_label_new(st.blurb);
		gtk_style_context_add_class(gtk_widget_get_style_context(blurb), "hint");
		gtk_label_set_xalign(GTK_LABEL(blurb), 0);
		gtk_widget_set_margin_start(blurb, 26);
		StylePicture* pic = new StylePicture(st.id);
		gtk_widget_set_margin_start(pic->widget(), 26);
		gtk_widget_set_margin_top(pic->widget(), 2);
		owned(pic);
		gtk_box_pack_start(GTK_BOX(item), radio, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(item), blurb, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(item), pic->widget(), FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(box), item, FALSE, FALSE, 0);
	}
	GtkWidget* heading = gtk_label_new("Show in the toolbar:");
	gtk_style_context_add_class(gtk_widget_get_style_context(heading), "heading");
	gtk_label_set_xalign(GTK_LABEL(heading), 0);
	gtk_widget_set_margin_top(heading, 14);
	gtk_box_pack_start(GTK_BOX(box), heading, FALSE, FALSE, 0);
	GtkWidget* grid = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(grid), 18);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
	gtk_widget_set_margin_top(grid, 10);
	const int groups[] = { TGFile, TGUndo, TGClipboard, TGZoom, TGSim, TGRun, TGLock, TGTab, TGFeedback };
	int i = 0;
	for (int g : groups) {
		GtkWidget* c = check(toolGroupName(g), (prefs().toolbarHidden & (1 << g)) == 0, [g](bool on) {
			if (on) prefs().toolbarHidden &= ~(1 << g);
			else prefs().toolbarHidden |= 1 << g;
			apply();
		});
		gtk_widget_set_size_request(c, 170, -1);
		gtk_grid_attach(GTK_GRID(grid), c, i % 3, i / 3, 1, 1);
		i++;
	}
	gtk_grid_attach(GTK_GRID(grid), check("Circuit name", prefs().showTitle, [](bool on) { prefs().showTitle = on; apply(); }), i % 3, i / 3, 1, 1);
	i++;
	gtk_grid_attach(GTK_GRID(grid), check("Dark mode switch", prefs().showThemeToggle, [](bool on) { prefs().showThemeToggle = on; apply(); }),
	                i % 3, i / 3, 1, 1);
	gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);
	GtkWidget* hint = gtk_label_new("Applies to every style. Hidden tools are still in the ••• menus and keep their shortcuts.");
	gtk_style_context_add_class(gtk_widget_get_style_context(hint), "hint");
	gtk_label_set_xalign(GTK_LABEL(hint), 0);
	gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
	gtk_widget_set_margin_top(hint, 10);
	gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 0);
	return box;
}

// ---- Shortcuts -----------------------------------------------------------------------

GtkWidget* keyCaps(const std::string& accel) {
	GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
	for (const std::string& cap : shortcuts::caps(accel)) {
		GtkWidget* l = gtk_label_new(cap.c_str());
		gtk_style_context_add_class(gtk_widget_get_style_context(l), "keycap");
		gtk_box_pack_start(GTK_BOX(row), l, FALSE, FALSE, 0);
	}
	return row;
}

void fillShortcuts(SettingsWindow* s);

void stopRecording(SettingsWindow* s) {
	s->recording = nullptr;
	s->recordButton = nullptr;
}

void shortcutRow(SettingsWindow* s, GtkWidget* list, const shortcuts::Action& a) {
	GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget* name = gtk_label_new(a.name);
	gtk_label_set_xalign(GTK_LABEL(name), 0);
	gtk_box_pack_start(GTK_BOX(row), name, TRUE, TRUE, 0);
	if (shortcuts::isCustom(a)) {
		const std::string def = a.keys;
		GtkWidget* back = gtk_button_new_from_icon_name("edit-undo-symbolic", GTK_ICON_SIZE_MENU);
		gtk_button_set_relief(GTK_BUTTON(back), GTK_RELIEF_NONE);
		const std::string first = def.substr(0, def.find('|'));
		gtk_widget_set_tooltip_text(back, ("Back to " + (first.empty() ? std::string("none") : shortcuts::label(first))).c_str());
		const shortcuts::Action* ap = &a;
		on(back, "clicked", [s, ap] {
			shortcuts::reset(*ap);
			if (CircuitWindow* w = front()) shortcuts::apply(w->application());
			fillShortcuts(s);
		});
		gtk_box_pack_start(GTK_BOX(row), back, FALSE, FALSE, 0);
	}
	GtkWidget* keys = gtk_button_new();
	gtk_widget_set_name(keys, "shortcut-keys");
	gtk_button_set_relief(GTK_BUTTON(keys), GTK_RELIEF_NONE);
	gtk_widget_set_size_request(keys, 130, -1);
	const std::string k = shortcuts::keys(a);
	if (s->recording == &a) {
		GtkWidget* l = gtk_label_new("Press keys…");
		gtk_style_context_add_class(gtk_widget_get_style_context(l), "recording");
		gtk_container_add(GTK_CONTAINER(keys), l);
		s->recordButton = keys;
	} else if (k.empty()) {
		GtkWidget* l = gtk_label_new("None");
		gtk_style_context_add_class(gtk_widget_get_style_context(l), "dim-label");
		gtk_container_add(GTK_CONTAINER(keys), l);
	} else {
		GtkWidget* caps = keyCaps(k);
		gtk_widget_set_halign(caps, GTK_ALIGN_END);
		gtk_container_add(GTK_CONTAINER(keys), caps);
	}
	const shortcuts::Action* ap = &a;
	on(keys, "clicked", [s, ap] {
		s->recording = s->recording == ap ? nullptr : ap;
		gtk_label_set_text(GTK_LABEL(s->shortcutNote), "Press the new keys. Escape keeps what was there; Backspace removes it.");
		fillShortcuts(s);
		gtk_widget_grab_focus(s->win);
	});
	gtk_box_pack_start(GTK_BOX(row), keys, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(list), row, FALSE, FALSE, 0);
}

void fillShortcuts(SettingsWindow* s) {
	GtkWidget* list = s->shortcutList;
	if (list == nullptr) return;
	GList* kids = gtk_container_get_children(GTK_CONTAINER(list));
	for (GList* k = kids; k; k = k->next) gtk_widget_destroy(GTK_WIDGET(k->data));
	g_list_free(kids);
	gchar* q = g_utf8_strdown(s->shortcutSearch.c_str(), -1);
	const std::string query = q ? q : "";
	g_free(q);
	std::string section;
	for (const shortcuts::Action& a : shortcuts::all()) {
		gchar* n = g_utf8_strdown((std::string(a.name) + " " + a.section).c_str(), -1);
		const bool match = query.empty() || (n && std::string(n).find(query) != std::string::npos);
		g_free(n);
		if (!match) continue;
		if (section != a.section) {
			section = a.section;
			gchar* up = g_utf8_strup(a.section, -1);
			GtkWidget* h = gtk_label_new(up);
			g_free(up);
			gtk_style_context_add_class(gtk_widget_get_style_context(h), "section");
			gtk_label_set_xalign(GTK_LABEL(h), 0);
			gtk_widget_set_margin_top(h, 10);
			gtk_box_pack_start(GTK_BOX(list), h, FALSE, FALSE, 0);
		}
		shortcutRow(s, list, a);
	}
	if (query.empty()) {
		GtkWidget* h = gtk_label_new("FIXED");
		gtk_style_context_add_class(gtk_widget_get_style_context(h), "section");
		gtk_label_set_xalign(GTK_LABEL(h), 0);
		gtk_widget_set_margin_top(h, 10);
		gtk_box_pack_start(GTK_BOX(list), h, FALSE, FALSE, 0);
		struct Fixed { const char* keys; const char* what; };
		static const Fixed fixed[] = {
			{ "space", "Tap: zoom to fit. Hold and drag: move around. In Simulation View: pause" },
			{ "Escape", "Cancel a drag, paste or connection; leave Simulation View" },
			{ "Delete", "Delete the selection" },
			{ "Up", "Arrows nudge the selection, or move around" },
			{ "<Shift>1", "Shift+1…Shift+0: jump to a gate category" },
			{ "<Primary>Tab", "Switch tabs: tap for the last one, hold for a picture of each" },
		};
		for (const Fixed& f : fixed) {
			GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
			GtkWidget* l = gtk_label_new(f.what);
			gtk_style_context_add_class(gtk_widget_get_style_context(l), "dim-label");
			gtk_label_set_xalign(GTK_LABEL(l), 0);
			gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
			gtk_box_pack_start(GTK_BOX(row), l, TRUE, TRUE, 0);
			GtkWidget* caps = keyCaps(f.keys);
			gtk_widget_set_margin_end(caps, 12);
			gtk_box_pack_start(GTK_BOX(row), caps, FALSE, FALSE, 0);
			gtk_box_pack_start(GTK_BOX(list), row, FALSE, FALSE, 0);
		}
	}
	gtk_widget_show_all(list);
}

GtkWidget* shortcutsPage(SettingsWindow* s) {
	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_widget_set_margin_start(box, 28);
	gtk_widget_set_margin_end(box, 28);
	gtk_widget_set_margin_top(box, 18);
	gtk_widget_set_margin_bottom(box, 20);
	GtkWidget* top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget* search = gtk_search_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(search), "Search shortcuts");
	gtk_widget_set_size_request(search, 220, -1);
	on(search, "search-changed", [s, search] { s->shortcutSearch = gtk_entry_get_text(GTK_ENTRY(search)); fillShortcuts(s); });
	gtk_box_pack_start(GTK_BOX(top), search, FALSE, FALSE, 0);
	GtkWidget* restore = button("Restore Defaults", [s] {
		shortcuts::resetAll();
		if (CircuitWindow* w = front()) shortcuts::apply(w->application());
		gtk_label_set_text(GTK_LABEL(s->shortcutNote), "Every shortcut is back to how it came.");
		fillShortcuts(s);
	});
	gtk_box_pack_end(GTK_BOX(top), restore, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), top, FALSE, FALSE, 0);
	s->shortcutNote = gtk_label_new("Click a shortcut, then press the new keys. Keys without Ctrl or Alt work while the canvas has the keyboard.");
	gtk_style_context_add_class(gtk_widget_get_style_context(s->shortcutNote), "hint");
	gtk_label_set_xalign(GTK_LABEL(s->shortcutNote), 0);
	gtk_label_set_line_wrap(GTK_LABEL(s->shortcutNote), TRUE);
	gtk_box_pack_start(GTK_BOX(box), s->shortcutNote, FALSE, FALSE, 0);
	GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_size_request(scroll, -1, 380);
	s->shortcutList = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	gtk_container_add(GTK_CONTAINER(scroll), s->shortcutList);
	gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
	fillShortcuts(s);
	return box;
}

GtkWidget* buildPage(SettingsWindow* s, int page) {
	s->shortcutList = nullptr;
	s->recording = nullptr;
	switch (page) {
	case Appearance: return appearancePage();
	case CanvasPage: return canvasPage();
	case ToolbarPage: return toolbarPage();
	case ShortcutsPage: return shortcutsPage(s);
	default: return generalPage();
	}
}

// The Mac's page switch: the old page goes, the window eases to the new
// one's height, then the new page fades in.
void select(SettingsWindow* s, int page, bool animated) {
	s->page = page;
	s->bar->page = page;
	s->bar->redraw();
	gtk_window_set_title(GTK_WINDOW(s->win), kTitles[page]);
	if (s->pageWidget) gtk_widget_destroy(s->pageWidget);
	GtkWidget* pg = buildPage(s, page);
	gtk_widget_show_all(pg);
	int minH = 0, natH = 0;
	gtk_widget_get_preferred_height_for_width(pg, (int)kWidth, &minH, &natH);
	s->pageWidget = pg;
	gtk_container_add(GTK_CONTAINER(s->holder), pg);
	const double now = s->height.value();
	if (!animated || now <= 0) {
		s->height.set(natH);
		s->opacity.set(1);
		gtk_widget_set_size_request(s->holder, (int)kWidth, natH);
		gtk_widget_set_opacity(pg, 1);
		return;
	}
	s->height.set(now);
	s->height.go(natH, 0.32, true);
	s->opacity.set(0);
	gtk_widget_set_opacity(pg, 0);
	if (s->tick == 0)
		s->tick = gtk_widget_add_tick_callback(s->win, [](GtkWidget*, GdkFrameClock*, gpointer data) -> gboolean {
			SettingsWindow* s = static_cast<SettingsWindow*>(data);
			gtk_widget_set_size_request(s->holder, (int)kWidth, (int)s->height.value());
			if (!s->height.active() && s->opacity.to == 0) s->opacity.go(1, 0.28);
			if (s->pageWidget) gtk_widget_set_opacity(s->pageWidget, s->opacity.value());
			if (s->height.active() || s->opacity.active() || s->opacity.to == 0) return G_SOURCE_CONTINUE;
			s->tick = 0;
			return G_SOURCE_REMOVE;
		}, s, nullptr);
}

void PageBar::mouseDown(int b, float x, float y, bool, GdkEventButton* e) {
	const int bi = buttons.at(x, y);
	if (bi >= 0) { if (b == 1) buttons.pressed = bi; return; }
	const int i = itemAt(x, y);
	if (i >= 0 && b == 1) {
		if (i != page) select(owner, i, true);
		return;
	}
	titleRowPress(GTK_WINDOW(gtk_widget_get_toplevel(area)), e, false);
}

}  // namespace

void show(CircuitWindow* from, int page) {
	if (g_settings) {
		if (page >= 0 && page != g_settings->page) select(g_settings, page, true);
		gtk_window_present(GTK_WINDOW(g_settings->win));
		return;
	}
	SettingsWindow* s = new SettingsWindow();
	g_settings = s;
	s->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_widget_set_name(s->win, "settings");
	gtk_window_set_resizable(GTK_WINDOW(s->win), FALSE);
	gtk_window_set_icon_name(GTK_WINDOW(s->win), "cedarlogic");
	if (from) gtk_window_set_transient_for(GTK_WINDOW(s->win), from->window());
	gtk_window_set_position(GTK_WINDOW(s->win), GTK_WIN_POS_CENTER_ON_PARENT);
	s->bar = new PageBar(s);
	gtk_window_set_titlebar(GTK_WINDOW(s->win), s->bar->widget());
	s->holder = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(s->win), s->holder);
	g_signal_connect(s->win, "key-press-event", CL_CALLBACK(+[](GtkWidget* w, GdkEventKey* e, gpointer data) -> gboolean {
		SettingsWindow* s = static_cast<SettingsWindow*>(data);
		if (s->recording) {
			// The keys for the action being changed.
			if (e->keyval == GDK_KEY_Escape) { s->recording = nullptr; fillShortcuts(s); return TRUE; }
			const shortcuts::Action* a = s->recording;
			if (e->keyval == GDK_KEY_BackSpace || e->keyval == GDK_KEY_Delete) {
				shortcuts::set(*a, "");
			} else {
				const std::string accel = shortcuts::fromEvent(e);
				if (accel.empty()) return TRUE;   // a modifier on its own: keep waiting
				// The fixed keys would never reach it (or a menu's would stop them working).
				const std::string kept = shortcuts::reserved(e);
				if (!kept.empty()) {
					gtk_label_set_text(GTK_LABEL(s->shortcutNote),
					                   (shortcuts::label(accel) + " is kept for " + kept + ". Press other keys, or Escape.").c_str());
					return TRUE;
				}
				const std::string loser = shortcuts::set(*a, accel);
				if (!loser.empty())
					if (const shortcuts::Action* l = shortcuts::find(loser)) {
						const std::string left = shortcuts::keys(*l);
						gtk_label_set_text(GTK_LABEL(s->shortcutNote),
						                   (shortcuts::label(accel) + " was “" + l->name + "”; " +
						                    (left.empty() ? std::string("that one has no shortcut now.") : "that one keeps " + shortcuts::label(left) + ".")).c_str());
					}
			}
			s->recording = nullptr;
			if (CircuitWindow* f = front()) shortcuts::apply(f->application());
			fillShortcuts(s);
			return TRUE;
		}
		// Escape closes, as every other window does; a field being typed in lets go first.
		if (e->keyval == GDK_KEY_Escape) {
			GtkWidget* focus = gtk_window_get_focus(GTK_WINDOW(w));
			if (focus && GTK_IS_ENTRY(focus)) { gtk_window_set_focus(GTK_WINDOW(w), nullptr); return TRUE; }
			gtk_widget_destroy(w);
			return TRUE;
		}
		// Ctrl+Tab and Ctrl+Shift+Tab go through the pages.
		if ((e->state & GDK_CONTROL_MASK) && (e->keyval == GDK_KEY_Tab || e->keyval == GDK_KEY_ISO_Left_Tab)) {
			const int d = e->keyval == GDK_KEY_ISO_Left_Tab || (e->state & GDK_SHIFT_MASK) ? PageCount - 1 : 1;
			select(s, (s->page + d) % PageCount, true);
			return TRUE;
		}
		return FALSE;
	}), s);
	g_signal_connect(s->win, "destroy", CL_CALLBACK(+[](GtkWidget*, gpointer data) {
		SettingsWindow* s = static_cast<SettingsWindow*>(data);
		if (g_settings == s) g_settings = nullptr;
		delete s->bar;
		delete s;
	}), s);
	select(s, page >= 0 ? page : General, false);
	gtk_widget_show_all(s->win);
	// The window fades in, as a sheet arrives.
	gtk_widget_set_opacity(s->win, 0);
	gtk_widget_add_tick_callback(s->win, [](GtkWidget* w, GdkFrameClock*, gpointer) -> gboolean {
		const double o = std::min(1.0, gtk_widget_get_opacity(w) + (anim::enabled() ? 0.12 : 1));
		gtk_widget_set_opacity(w, o);
		return o < 1 ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
	}, nullptr, nullptr);
}

GtkWidget* window() { return g_settings ? g_settings->win : nullptr; }

void themeChanged() {
	if (g_settings == nullptr) return;
	g_settings->bar->redraw();
	if (g_settings->pageWidget) gtk_widget_queue_draw(g_settings->pageWidget);
}

}  // namespace settings
