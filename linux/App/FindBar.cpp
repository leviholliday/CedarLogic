// The find bar (see FindBar.h).

#include "FindBar.h"
#include "Canvas.h"
#include "Window.h"

#include <algorithm>

namespace {

GtkWidget* iconButton(const char* icon, const char* tip) {
	GtkWidget* b = gtk_button_new_from_icon_name(icon, GTK_ICON_SIZE_MENU);
	gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
	gtk_widget_set_tooltip_text(b, tip);
	gtk_widget_set_focus_on_click(b, FALSE);
	return b;
}

}  // namespace

FindBar::FindBar(CircuitWindow* window) : win(window) {
	root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_widget_set_name(root, "findbar");
	gtk_widget_set_margin_top(root, 12);
	gtk_widget_set_margin_bottom(root, 6);
	gtk_widget_set_margin_start(root, 6);
	gtk_widget_set_margin_end(root, 6);
	gtk_widget_set_size_request(root, 600, -1);
	revealer = gtk_revealer_new();
	gtk_revealer_set_transition_type(GTK_REVEALER(revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
	gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), 180);
	gtk_widget_set_halign(revealer, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(revealer, GTK_ALIGN_START);
	gtk_container_add(GTK_CONTAINER(revealer), root);
	GtkWidget* icon = gtk_image_new_from_icon_name("edit-find-symbolic", GTK_ICON_SIZE_MENU);
	gtk_style_context_add_class(gtk_widget_get_style_context(icon), "dim-label");
	gtk_box_pack_start(GTK_BOX(root), icon, FALSE, FALSE, 6);
	entry = gtk_entry_new();
	gtk_widget_set_name(entry, "findbar-entry");
	gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Labels, TO/FROM names, parts");
	gtk_entry_set_width_chars(GTK_ENTRY(entry), 26);
	gtk_entry_set_has_frame(GTK_ENTRY(entry), FALSE);
	gtk_box_pack_start(GTK_BOX(root), entry, FALSE, FALSE, 0);
	status = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(status), 0);
	gtk_label_set_ellipsize(GTK_LABEL(status), PANGO_ELLIPSIZE_END);
	gtk_style_context_add_class(gtk_widget_get_style_context(status), "dim-label");
	gtk_box_pack_start(GTK_BOX(root), status, TRUE, TRUE, 8);
	up = iconButton("go-up-symbolic", "Previous (Shift+Enter)");
	down = iconButton("go-down-symbolic", "Next (Enter)");
	GtkWidget* done = gtk_button_new_with_label("Done");
	gtk_widget_set_name(done, "findbar-done");
	gtk_widget_set_focus_on_click(done, FALSE);
	gtk_box_pack_start(GTK_BOX(root), up, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(root), down, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(root), done, FALSE, FALSE, 4);
	g_signal_connect(entry, "changed", G_CALLBACK(changedCb), this);
	g_signal_connect(entry, "key-press-event", G_CALLBACK(keyCb), this);
	g_signal_connect_swapped(up, "clicked", CL_CALLBACK(+[](FindBar* f) { f->step(-1); }), this);
	g_signal_connect_swapped(down, "clicked", CL_CALLBACK(+[](FindBar* f) { f->step(1); }), this);
	g_signal_connect_swapped(done, "clicked", CL_CALLBACK(+[](FindBar* f) { f->close(); }), this);
	GList* kids = gtk_container_get_children(GTK_CONTAINER(root));
	for (GList* k = kids; k; k = k->next) gtk_widget_show(GTK_WIDGET(k->data));
	g_list_free(kids);
}

void FindBar::open(const std::string& query) {
	if (!query.empty()) gtk_entry_set_text(GTK_ENTRY(entry), query.c_str());
	gtk_widget_show_all(revealer);
	gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), TRUE);
	gtk_widget_grab_focus(entry);
	gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
	run(gtk_entry_get_text_length(GTK_ENTRY(entry)) > 0);
}

void FindBar::close() {
	gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), FALSE);
	if (Canvas* c = win->currentCanvas()) gtk_widget_grab_focus(c->widget());
}

void FindBar::run(bool jump) {
	hits.clear();
	const std::string q = gtk_entry_get_text(GTK_ENTRY(entry));
	total = 0;
	index = 0;
	if (!q.empty()) {
		std::vector<CLFindResult> out(500);
		total = cl_find(win->document(), q.c_str(), out.data(), (int)out.size());
		for (int i = 0; i < std::min(total, (int)out.size()); i++)
			hits.push_back({ out[i].page, out[i].gate, out[i].x, out[i].y, out[i].text ? out[i].text : "", out[i].kind ? out[i].kind : "" });
	}
	if (jump && !hits.empty()) show(0);
	updateStatus();
}

void FindBar::step(int delta) {
	if (hits.empty()) { gtk_widget_error_bell(root); return; }
	const int n = (int)hits.size();
	index = ((index + delta) % n + n) % n;
	show(index);
	updateStatus();
}

void FindBar::show(int i) {
	if (i < 0 || i >= (int)hits.size()) return;
	const Hit& h = hits[i];
	win->showFoundGate(h.page, h.gate, h.x, h.y);
	gtk_widget_grab_focus(entry);
	gtk_editable_set_position(GTK_EDITABLE(entry), -1);
}

void FindBar::updateStatus() {
	std::string s;
	GtkStyleContext* sc = gtk_widget_get_style_context(status);
	gtk_style_context_remove_class(sc, "warning");
	if (total > 0 && index < (int)hits.size()) {
		s = format("%d of %d · %s", index + 1, total, hits[index].kind.c_str());
		if (cl_document_page_count(win->document()) > 1) s += " · " + win->tabName(hits[index].page);
	} else if (gtk_entry_get_text_length(GTK_ENTRY(entry)) > 0) {
		s = "Not found";
		gtk_style_context_add_class(sc, "warning");
	}
	gtk_label_set_text(GTK_LABEL(status), s.c_str());
	gtk_widget_set_sensitive(up, total > 0);
	gtk_widget_set_sensitive(down, total > 0);
}

void FindBar::changedCb(GtkEditable*, gpointer self) {
	FindBar* f = static_cast<FindBar*>(self);
	guarded("finding", [&] { f->run(true); });
}

gboolean FindBar::keyCb(GtkWidget*, GdkEventKey* e, gpointer self) {
	FindBar* f = static_cast<FindBar*>(self);
	const bool shift = (e->state & GDK_SHIFT_MASK) != 0;
	switch (e->keyval) {
	case GDK_KEY_Return:
	case GDK_KEY_KP_Enter: f->step(shift ? -1 : 1); return TRUE;
	case GDK_KEY_Escape: f->close(); return TRUE;
	case GDK_KEY_Up: f->step(-1); return TRUE;
	case GDK_KEY_Down: f->step(1); return TRUE;
	default: return FALSE;
	}
}
