// The side panel's gates (see Palette.h).

#include "Palette.h"
#include "Canvas.h"
#include "Chrome.h"
#include "Collections.h"
#include "Drawn.h"
#include "Window.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

std::string lower(const std::string& s) {
	gchar* l = g_utf8_strdown(s.c_str(), -1);
	std::string out = l ? l : "";
	g_free(l);
	return out;
}

// Gate pictures, drawn once per size, theme and scale (the Mac's TileCache).
std::map<std::string, cairo_surface_t*>& tileCache() {
	static std::map<std::string, cairo_surface_t*> m;
	return m;
}

cairo_surface_t* tileImage(const std::string& name, float w, float h, int scale, bool dark) {
	const std::string key = name + format("|%d|%d|%d|%d", (int)w, (int)h, scale, dark ? 1 : 0);
	auto it = tileCache().find(key);
	if (it != tileCache().end()) return it->second;
	cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)std::ceil(w * scale), (int)std::ceil(h * scale));
	cairo_surface_set_device_scale(s, scale, scale);
	cairo_t* cr = cairo_create(s);
	if (parts::isPart(name)) parts::draw(name, cr, w, h, scale, dark);
	else cl_library_draw_gate(name.c_str(), cr, w, h, scale, dark);
	cairo_destroy(cr);
	tileCache()[key] = s;
	return s;
}

void forgetTiles() {
	for (auto& kv : tileCache()) cairo_surface_destroy(kv.second);
	tileCache().clear();
}

}  // namespace

// ---- The category menu ---------------------------------------------------------------

// The category's name in a soft rounded box, with a chevron; a click lists them all.
class CategoryButton : public Drawn {
public:
	explicit CategoryButton(GatePalette* p) : palette(p) {
		create();
		gtk_widget_set_size_request(area, -1, 30);
	}

protected:
	void paint(cairo_t* cr, float w, float h) override {
		const Chrome c = chrome();
		fillRect(cr, rectF(0, 0, w, h), c.canvas());
		const RectF r = rectF(0.5f, 0.5f, w - 0.5f, h - 0.5f);
		const float hot = (float)fade.value();
		fillRound(cr, r, 7, c.dark ? colorF(1, 1, 1, 0.06f + 0.04f * hot) : colorF(0, 0, 0, 0.045f + 0.025f * hot));
		strokeRound(cr, r, 7, c.hairline());
		const Color ink = c.barInk();
		drawTextMid(cr, palette->categoryTitle(), rectF(10, 0, w - 26, h), 12, ink, TextAlign::Center, true);
		drawIcon(cr, Icon::ChevronDown, rectF(w - 24, 0, w - 8, h), 9, withAlpha(ink, 0.6f));
	}
	void mouseMove(float, float) override { fade.go(1, 0.12); animate(); }
	void mouseLeave() override { fade.go(0, 0.18); animate(); }
	void mouseDown(int button, float, float, bool, GdkEventButton* e) override {
		if (button == 1) palette->chooseCategory(area, (GdkEvent*)e);
	}
	bool animating() override { return fade.active(); }

private:
	GatePalette* palette;
	anim::Tween fade;
};

// ---- The tiles ------------------------------------------------------------------------

class TileGrid : public Drawn {
public:
	explicit TileGrid(GatePalette* p) : palette(p) {
		create();
		gtk_widget_set_vexpand(area, TRUE);
		hover.in = 0.12;
		hover.out = 0.2;
	}
	~TileGrid() override { if (fadeTimer) g_source_remove(fadeTimer); }
	void reset() {
		scroll.snap(0);
		appear.set(0);
		appear.go(1, 0.15);
		hover.setHot(-1);
		animate();
	}

protected:
	void paint(cairo_t* cr, float w, float h) override;
	void mouseMove(float x, float y) override;
	void mouseLeave() override { if (pressed < 0) { hover.setHot(-1); animate(); } }
	void mouseDown(int button, float x, float y, bool doubleClick, GdkEventButton* e) override;
	void mouseUp(int button, float x, float y) override;
	void wheel(double dy, float, float) override;
	bool animating() override { return hover.active() || appear.active() || scroll.step() || barFade.active(); }

private:
	GatePalette* palette;
	anim::HoverFade hover;
	anim::Tween appear, barFade;
	anim::Spring scroll;          // points down the list
	int pressed = -1;
	bool dragging = false, placed = false;
	float pressX = 0, pressY = 0;
	Canvas* target = nullptr;
	double lastWheel = 0;
	guint fadeTimer = 0;          // the scroll bar fading once the wheel stops

	float tileW() const { return (float)prefs().gateSize; }
	float tileH() const { return tileW() * 0.8f + (palette->showingParts() || prefs().showGateNames ? 14 : 0) + 4; }
	int columns(float w) const { return std::max(1, (int)((w - 12 + 2) / (tileW() + 4 + 2))); }
	RectF tileRect(int i, float w) const {
		const int cols = columns(w);
		const float cellW = (w - 12 - (cols - 1) * 2) / cols;
		const float x = 6 + (i % cols) * (cellW + 2), y = 2 + (i / cols) * (tileH() + 2) - (float)scroll.x;
		return rectF(x, y, x + cellW, y + tileH());
	}
	float contentHeight(float w) const {
		const int n = (int)palette->shownGates().size();
		const int rows = (n + columns(w) - 1) / columns(w);
		return 2 + rows * (tileH() + 2) + 8;
	}
	void clampScroll() {
		const float maxS = std::max(0.0f, contentHeight(width()) - height());
		scroll.target = std::max(0.0, std::min<double>(scroll.target, maxS));
	}
	int tileAt(float x, float y) const {
		const int n = (int)palette->shownGates().size();
		for (int i = 0; i < n; i++) if (inRect(tileRect(i, width()), x, y)) return i;
		return -1;
	}
	void dragTo(float x, float y);
	void drop(float x, float y);
};

void TileGrid::paint(cairo_t* cr, float w, float h) {
	const Chrome c = chrome();
	const bool dark = c.dark;
	fillRect(cr, rectF(0, 0, w, h), c.canvas());
	const std::vector<GatePalette::Gate>& gates = palette->shownGates();
	if (gates.empty()) {
		const Color dim = withAlpha(c.barInk(), 0.55f);
		const std::string text = palette->showingParts()
		                             ? "Select some gates, then choose Edit ▸ Save as Part… to keep them here."
		                             : "No gates match.";
		if (palette->showingParts()) drawIcon(cr, "package-x-generic-symbolic", rectF(0, 24, w, 52), 22, dim);
		drawWrapped(cr, text, rectF(16, palette->showingParts() ? 60 : 24, w - 16, h), 11.5f, dim, false, TextAlign::Center);
		return;
	}
	const Color accent = c.accent();
	const int scale = std::max(1, gtk_widget_get_scale_factor(area));
	const float a = (float)appear.value();
	cairo_save(cr);
	if (a < 1) cairo_push_group(cr);
	const bool names = palette->showingParts() || prefs().showGateNames;
	const float artW = tileW(), artH = tileW() * 0.8f;
	std::vector<Tip> tips;
	for (int i = 0; i < (int)gates.size(); i++) {
		const RectF r = tileRect(i, w);
		if (r.bottom < 0 || r.top > h) continue;
		const float hot = (float)hover.amount(i);
		if (hot > 0.01f) {
			fillRound(cr, r, 6, withAlpha(accent, 0.10f * hot));
			strokeRound(cr, r, 6, withAlpha(accent, 0.9f * hot), 1.5f);
		}
		cairo_surface_t* img = tileImage(gates[i].name, artW, artH, scale, dark);
		const float ix = std::floor((r.left + r.right - artW) / 2), iy = r.top + 2;
		cairo_set_source_surface(cr, img, ix, iy);
		cairo_paint(cr);
		if (names)
			drawText(cr, gates[i].caption, rectF(r.left + 1, iy + artH, r.right - 1, iy + artH + 14), 9.5f, withAlpha(c.barInk(), 0.72f),
			         TextAlign::Center);
		tips.push_back({ r, parts::isPart(gates[i].name) ? gates[i].caption + " — right-click to rename or delete" : gates[i].caption });
	}
	if (a < 1) {
		cairo_pop_group_to_source(cr);
		cairo_paint_with_alpha(cr, a);
	}
	cairo_restore(cr);
	// A slim scroller while the list moves.
	const float total = contentHeight(w);
	const float bar = (float)barFade.value();
	if (total > h && bar > 0.01f) {
		const float th = std::max(24.0f, h * h / total), ty = (float)scroll.x / (total - h) * (h - th);
		fillRound(cr, rectF(w - 6, ty + 2, w - 2, ty + th - 2), 2, withAlpha(c.barInk(), 0.35f * bar));
	}
	setTips(tips);
}

void TileGrid::wheel(double dy, float, float) {
	scroll.target += dy * (std::fabs(dy) < 1 ? 30 : 48);
	clampScroll();
	barFade.go(1, 0.1);
	lastWheel = anim::now();
	// One timer, looking often enough that the bar still fades about 0.9 s
	// after the wheel stops (and removed with the grid, should its window
	// close first).
	if (fadeTimer == 0) fadeTimer = g_timeout_add(100, [](gpointer self) -> gboolean {
		TileGrid* t = static_cast<TileGrid*>(self);
		if (anim::now() - t->lastWheel <= 0.8) return G_SOURCE_CONTINUE;
		t->fadeTimer = 0;
		t->barFade.go(0, 0.4);
		t->animate();
		return G_SOURCE_REMOVE;
	}, this);
	animate();
}

void TileGrid::mouseMove(float x, float y) {
	if (pressed >= 0) {
		if (!dragging && (std::fabs(x - pressX) > 3 || std::fabs(y - pressY) > 3)) dragging = true;
		if (dragging) dragTo(x, y);
		return;
	}
	hover.setHot(tileAt(x, y));
	animate();
}

void TileGrid::mouseDown(int button, float x, float y, bool, GdkEventButton* e) {
	const int i = tileAt(x, y);
	if (i < 0) return;
	const std::string name = palette->shownGates()[i].name;
	if (button == 3) {
		if (parts::isPart(name)) parts::tileMenu(palette->window()->window(), name, (GdkEvent*)e);
		return;
	}
	if (button != 1) return;
	if (!palette->window()->canEdit()) { palette->window()->lockNudge(); return; }
	pressed = i;
	pressX = x;
	pressY = y;
	dragging = placed = false;
	target = nullptr;
}

// The side under the pointer takes the gate (on the way to the right side
// it crosses the left, and moves over with you); over the panel, the side
// next to it. It's placed at once, even while the pointer is still over the
// panel: the canvas only draws inside itself, so the gate slides out from
// under the panel as it's pulled across its edge.
void TileGrid::dragTo(float x, float y) {
	CircuitWindow* w = palette->window();
	double cx = 0, cy = 0;
	Canvas* c = w->canvasUnder(area, x, y, cx, cy);
	if (c == nullptr || pressed < 0) return;
	setCursorName("grabbing");
	if (c != target) {
		if (target && placed) w->cancelFloating();
		placed = false;
		target = c;
	}
	double wx, wy;
	c->worldPoint(cx, cy, wx, wy);
	w->pointerMoved(wx, wy);
	if (!placed) {
		placed = w->addGateFloatingOn(c, palette->shownGates()[pressed].name, wx, wy);
	} else if (w->isFloating()) {
		const int p = c->page();
		if (p >= 0 && cl_edit_hover(w->document(), p, wx, wy, c->unitsPerPoint())) c->redraw();
	}
}

void TileGrid::mouseUp(int button, float x, float y) {
	if (button != 1 || pressed < 0) return;
	const int i = pressed;
	pressed = -1;
	setCursorName(nullptr);
	CircuitWindow* w = palette->window();
	if (!dragging) {
		// A click: the gate follows the pointer until the next click.
		w->addGateOnNextMove(palette->shownGates()[i].name);
		return;
	}
	dragging = false;
	drop(x, y);
	hover.setHot(tileAt(x, y));
	animate();
}

void TileGrid::drop(float x, float y) {
	CircuitWindow* w = palette->window();
	Canvas* c = target;
	target = nullptr;
	if (!placed || c == nullptr || !w->isFloating()) return;   // C already put it down (and connected it)
	placed = false;
	double cx = 0, cy = 0;
	Canvas* under = w->canvasUnder(area, x, y, cx, cy);
	const int p = c->page();
	if (under == c && cx >= 0 && cy >= 0 && cx < c->width() && cy < c->height() && p >= 0) {
		double wx, wy;
		c->worldPoint(cx, cy, wx, wy);
		cl_edit_press(w->document(), p, wx, wy, 0, c->unitsPerPoint());
		cl_edit_release(w->document(), wx, wy);
		w->edited();
		gtk_widget_grab_focus(c->widget());
	} else {
		w->cancelFloating();   // let go off the canvas: never mind
	}
}

// ---- The panel ------------------------------------------------------------------------

GatePalette::GatePalette(CircuitWindow* window) : win(window) {
	root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_name(root, "palette");
	gtk_container_set_border_width(GTK_CONTAINER(root), 0);
	picker = new CategoryButton(this);
	GtkWidget* top = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_container_set_border_width(GTK_CONTAINER(top), 8);
	gtk_box_pack_start(GTK_BOX(top), picker->widget(), FALSE, FALSE, 0);
	search = gtk_search_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(search), "Find a gate");
	gtk_box_pack_start(GTK_BOX(top), search, FALSE, FALSE, 0);
	g_signal_connect(search, "search-changed", G_CALLBACK(searchChangedCb), this);
	gtk_box_pack_start(GTK_BOX(root), top, FALSE, FALSE, 0);
	tiles = new TileGrid(this);
	gtk_box_pack_start(GTK_BOX(root), tiles->widget(), TRUE, TRUE, 0);
	loadCategories();
	category = 0;
	fill();
}

GatePalette::~GatePalette() {
	g_signal_handlers_disconnect_by_data(search, this);
	delete picker;
	delete tiles;
}

void GatePalette::loadCategories() {
	categories.clear();
	for (int c = 0; c < cl_library_category_count(); c++) {
		Category cat;
		std::string name = cl_library_category(c);
		const size_t dash = name.find(" - ");
		cat.title = dash == std::string::npos ? name : name.substr(dash + 3);
		for (int i = 0; i < cl_library_gate_count(c); i++) {
			Gate g;
			g.name = cl_library_gate(c, i);
			g.caption = cl_library_gate_caption(g.name.c_str());
			if (g.caption.empty()) g.caption = g.name;
			cat.gates.push_back(g);
		}
		if (!cat.gates.empty()) categories.push_back(cat);
	}
	// Your own parts, last (Save as Part), always offered.
	Category mine;
	mine.title = "My Parts";
	for (const parts::Part& p : parts::all()) mine.gates.push_back({ p.gate(), p.name });
	categories.push_back(mine);
}

bool GatePalette::showingParts() const {
	return gtk_entry_get_text_length(GTK_ENTRY(search)) == 0 && category == (int)categories.size() - 1;
}

std::string GatePalette::categoryTitle() const {
	if (gtk_entry_get_text_length(GTK_ENTRY(search)) > 0) return "Search";
	return category >= 0 && category < (int)categories.size() ? categories[category].title : std::string();
}

void GatePalette::fill() {
	shown.clear();
	const std::string query = lower(gtk_entry_get_text(GTK_ENTRY(search)));
	if (!query.empty()) {
		// Searching looks through every category.
		for (const Category& c : categories)
			for (const Gate& g : c.gates)
				if (lower(g.caption).find(query) != std::string::npos || lower(g.name).find(query) != std::string::npos) shown.push_back(g);
	} else if (category >= 0 && category < (int)categories.size()) {
		shown = categories[category].gates;
	}
	if (tiles) tiles->reset();
	if (picker) picker->redraw();
}

struct CategoryChoice { GatePalette* palette; int index; };

void GatePalette::chooseCategory(GtkWidget* from, GdkEvent* e) {
	GtkWidget* menu = gtk_menu_new();
	for (int i = 0; i < (int)categories.size(); i++) {
		if (i == (int)categories.size() - 1) gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
		std::string label = categories[i].title;
		GtkWidget* item = gtk_check_menu_item_new();
		gtk_check_menu_item_set_draw_as_radio(GTK_CHECK_MENU_ITEM(item), TRUE);
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), i == category && gtk_entry_get_text_length(GTK_ENTRY(search)) == 0);
		GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
		GtkWidget* l = gtk_label_new(label.c_str());
		gtk_label_set_xalign(GTK_LABEL(l), 0);
		gtk_box_pack_start(GTK_BOX(row), l, TRUE, TRUE, 0);
		if (prefs().showCategoryKeys && i < 10 && i < (int)categories.size() - 1) {
			GtkWidget* k = gtk_label_new(format("⇧%d", (i + 1) % 10).c_str());
			gtk_style_context_add_class(gtk_widget_get_style_context(k), "dim-label");
			gtk_box_pack_end(GTK_BOX(row), k, FALSE, FALSE, 0);
		}
		gtk_container_add(GTK_CONTAINER(item), row);
		CategoryChoice* choice = new CategoryChoice{ this, i };
		g_object_set_data_full(G_OBJECT(item), "cl-choice", choice, [](gpointer p) { delete static_cast<CategoryChoice*>(p); });
		g_signal_connect(item, "activate", CL_CALLBACK(+[](GtkMenuItem* it, gpointer) {
			CategoryChoice* c = static_cast<CategoryChoice*>(g_object_get_data(G_OBJECT(it), "cl-choice"));
			if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(it))) return;
			const CategoryChoice copy = *c;
			copy.palette->showCategory(copy.index);
		}), nullptr);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	}
	gtk_widget_show_all(menu);
	gtk_menu_attach_to_widget(GTK_MENU(menu), from, nullptr);
	g_signal_connect(menu, "deactivate", CL_CALLBACK(+[](GtkMenuShell* m, gpointer) {
		g_idle_add([](gpointer m) -> gboolean { gtk_widget_destroy(GTK_WIDGET(m)); return G_SOURCE_REMOVE; }, m);
	}), nullptr);
	gtk_menu_popup_at_widget(GTK_MENU(menu), from, GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, e);
}

void GatePalette::showCategory(int index) {
	if (index < 0 || index >= (int)categories.size()) return;
	category = index;
	g_signal_handlers_block_by_func(search, (gpointer)searchChangedCb, this);
	gtk_entry_set_text(GTK_ENTRY(search), "");
	g_signal_handlers_unblock_by_func(search, (gpointer)searchChangedCb, this);
	fill();
}

void GatePalette::focusSearch() { gtk_widget_grab_focus(search); }

void GatePalette::themeChanged() {
	forgetTiles();
	if (tiles) tiles->redraw();
	if (picker) picker->redraw();
}

void GatePalette::partsChanged() {
	forgetTiles();
	const bool wasParts = category == (int)categories.size() - 1;
	loadCategories();
	if (wasParts) category = (int)categories.size() - 1;
	category = std::min(category, (int)categories.size() - 1);
	fill();
}

void GatePalette::searchChangedCb(GtkSearchEntry*, gpointer self) { static_cast<GatePalette*>(self)->fill(); }
