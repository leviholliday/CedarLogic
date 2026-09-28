// The gate palette (see Palette.h).

#include "Palette.h"
#include "Window.h"

#include <algorithm>
#include <cstring>

namespace {

const char* kGateTarget = "application/x-cedarlogic-gate";
const int kTileW = 78, kTileH = 50;

std::string lower(const std::string& s) {
	gchar* l = g_utf8_strdown(s.c_str(), -1);
	std::string out = l ? l : "";
	g_free(l);
	return out;
}

}  // namespace

GatePalette::GatePalette(CircuitWindow* window) : win(window) {
	root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_name(root, "palette");
	gtk_container_set_border_width(GTK_CONTAINER(root), 6);

	search = gtk_search_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(search), "Find a gate");
	gtk_box_pack_start(GTK_BOX(root), search, FALSE, FALSE, 0);
	g_signal_connect(search, "search-changed", G_CALLBACK(searchChangedCb), this);

	combo = gtk_combo_box_text_new();
	gtk_box_pack_start(GTK_BOX(root), combo, FALSE, FALSE, 0);

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
		if (cat.gates.empty()) continue;
		const int n = (int)categories.size() + 1;
		const std::string label = n <= 10 ? format("%s   (Shift+%d)", cat.title.c_str(), n % 10) : cat.title;
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), label.c_str());
		categories.push_back(cat);
	}

	GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand(scroll, TRUE);
	flow = gtk_flow_box_new();
	gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
	gtk_flow_box_set_activate_on_single_click(GTK_FLOW_BOX(flow), TRUE);
	gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(flow), TRUE);
	gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 8);
	gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow), 2);
	gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow), 2);
	gtk_widget_set_valign(flow, GTK_ALIGN_START);
	g_signal_connect(flow, "child-activated", G_CALLBACK(activatedCb), this);
	gtk_container_add(GTK_CONTAINER(scroll), flow);
	gtk_box_pack_start(GTK_BOX(root), scroll, TRUE, TRUE, 0);

	g_signal_connect(combo, "changed", G_CALLBACK(comboChangedCb), this);
	if (!categories.empty()) gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
}

void GatePalette::showCategory(int index) {
	if (index < 0 || index >= (int)categories.size()) return;
	gtk_entry_set_text(GTK_ENTRY(search), "");
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), index);
}

void GatePalette::focusSearch() { gtk_widget_grab_focus(search); }

void GatePalette::themeChanged() { gtk_widget_queue_draw(flow); }

void GatePalette::clearTiles() {
	GList* kids = gtk_container_get_children(GTK_CONTAINER(flow));
	for (GList* k = kids; k; k = k->next) gtk_widget_destroy(GTK_WIDGET(k->data));
	g_list_free(kids);
	for (std::string* s : tileNames) delete s;
	tileNames.clear();
}

void GatePalette::fill() {
	clearTiles();
	const std::string query = lower(gtk_entry_get_text(GTK_ENTRY(search)));
	if (!query.empty()) {
		// Searching looks through every category.
		for (const Category& c : categories)
			for (const Gate& g : c.gates)
				if (lower(g.caption).find(query) != std::string::npos || lower(g.name).find(query) != std::string::npos)
					gtk_container_add(GTK_CONTAINER(flow), tile(g));
	} else {
		const int i = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
		if (i >= 0 && i < (int)categories.size())
			for (const Gate& g : categories[i].gates) gtk_container_add(GTK_CONTAINER(flow), tile(g));
	}
	gtk_widget_show_all(flow);
}

GtkWidget* GatePalette::tile(const Gate& g) {
	std::string* name = new std::string(g.name);
	tileNames.push_back(name);

	GtkWidget* box = gtk_event_box_new();
	gtk_widget_set_tooltip_text(box, g.caption.c_str());
	GtkWidget* v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget* art = gtk_drawing_area_new();
	gtk_widget_set_size_request(art, kTileW, kTileH);
	g_signal_connect(art, "draw", G_CALLBACK(drawTileCb), name);
	gtk_box_pack_start(GTK_BOX(v), art, FALSE, FALSE, 0);
	if (prefs().showGateNames) {
		GtkWidget* label = gtk_label_new(g.caption.c_str());
		gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
		gtk_label_set_max_width_chars(GTK_LABEL(label), 10);
		gtk_widget_set_size_request(label, kTileW, -1);
		GtkStyleContext* sc = gtk_widget_get_style_context(label);
		gtk_style_context_add_class(sc, "dim-label");
		PangoAttrList* attrs = pango_attr_list_new();
		pango_attr_list_insert(attrs, pango_attr_scale_new(0.8));
		gtk_label_set_attributes(GTK_LABEL(label), attrs);
		pango_attr_list_unref(attrs);
		gtk_box_pack_start(GTK_BOX(v), label, FALSE, FALSE, 0);
	}
	gtk_container_add(GTK_CONTAINER(box), v);
	g_object_set_data(G_OBJECT(box), "gate", name);

	GtkTargetEntry target = { (gchar*)kGateTarget, GTK_TARGET_SAME_APP, 1 };
	gtk_drag_source_set(box, GDK_BUTTON1_MASK, &target, 1, GDK_ACTION_COPY);
	g_signal_connect(box, "drag-data-get", G_CALLBACK(dragDataGetCb), name);
	g_signal_connect(box, "drag-begin", G_CALLBACK(dragBeginCb), name);
	return box;
}

void GatePalette::comboChangedCb(GtkComboBox*, gpointer self) {
	GatePalette* p = static_cast<GatePalette*>(self);
	if (gtk_entry_get_text_length(GTK_ENTRY(p->search)) > 0) return;
	p->fill();
}

void GatePalette::searchChangedCb(GtkSearchEntry*, gpointer self) { static_cast<GatePalette*>(self)->fill(); }

void GatePalette::activatedCb(GtkFlowBox*, GtkFlowBoxChild* child, gpointer self) {
	GatePalette* p = static_cast<GatePalette*>(self);
	GtkWidget* box = gtk_bin_get_child(GTK_BIN(child));
	std::string* name = box ? static_cast<std::string*>(g_object_get_data(G_OBJECT(box), "gate")) : nullptr;
	if (name) p->win->addGateOnNextMove(*name);
}

gboolean GatePalette::drawTileCb(GtkWidget* w, cairo_t* cr, gpointer data) {
	const std::string* name = static_cast<const std::string*>(data);
	const double scale = std::max(1, gtk_widget_get_scale_factor(w));
	cl_library_draw_gate(name->c_str(), cr, gtk_widget_get_allocated_width(w),
	                     gtk_widget_get_allocated_height(w), scale, prefs().dark);
	return FALSE;
}

void GatePalette::dragDataGetCb(GtkWidget*, GdkDragContext*, GtkSelectionData* data, guint, guint, gpointer d) {
	const std::string* name = static_cast<const std::string*>(d);
	gtk_selection_data_set(data, gdk_atom_intern(kGateTarget, FALSE), 8,
	                       (const guchar*)name->data(), (gint)name->size());
}

void GatePalette::dragBeginCb(GtkWidget*, GdkDragContext* ctx, gpointer d) {
	// The gate itself under the pointer while it's dragged.
	const std::string* name = static_cast<const std::string*>(d);
	cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kTileW, kTileH);
	cairo_t* cr = cairo_create(s);
	cl_library_draw_gate(name->c_str(), cr, kTileW, kTileH, 1, prefs().dark);
	cairo_destroy(cr);
	cairo_surface_set_device_offset(s, -kTileW / 2.0, -kTileH / 2.0);
	gtk_drag_set_icon_surface(ctx, s);
	cairo_surface_destroy(s);
}
