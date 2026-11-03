// The menu row (see MenuBar.h).

#include "MenuBar.h"
#include "Toolbar.h"
#include "Window.h"

MenuRow::MenuRow(CircuitWindow* window) : win(window) {
	create();
	gtk_widget_set_size_request(area, -1, (int)rowHeight());
	overlay = gtk_overlay_new();
	g_object_ref_sink(overlay);
	gtk_container_add(GTK_CONTAINER(overlay), area);
	menubar = appMenubar() ? gtk_menu_bar_new_from_model(appMenubar()) : gtk_menu_bar_new();
	gtk_widget_set_name(menubar, "menubar");
	gtk_style_context_add_class(gtk_widget_get_style_context(menubar), "cl-bar");
	gtk_widget_set_halign(menubar, GTK_ALIGN_START);
	gtk_widget_set_valign(menubar, GTK_ALIGN_CENTER);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), menubar);
	// A menu opening: its items' wording and state follow this window
	// (Edit's Lock in Place says Unlock Part when every selected part is locked).
	g_signal_connect(menubar, "button-press-event", CL_CALLBACK(+[](GtkWidget*, GdkEventButton*, gpointer self) -> gboolean {
		static_cast<MenuRow*>(self)->win->updateActions();
		return FALSE;
	}), this);
	refresh();
}

bool MenuRow::showing() const { return menubar && gtk_widget_is_visible(menubar) && gtk_widget_get_mapped(menubar) && !win->focusMode(); }

void MenuRow::refresh() {
	const bool dark = win->toolbarWidget() ? win->toolbarWidget()->barIsDark() : chrome().dark;
	GtkStyleContext* sc = gtk_widget_get_style_context(menubar);
	gtk_style_context_remove_class(sc, dark ? "on-light" : "on-dark");
	gtk_style_context_add_class(sc, dark ? "on-dark" : "on-light");
	onDark = dark;
	buttons.load();
	gtk_widget_set_margin_start(menubar, (int)(buttons.leftRoom() > 0 ? buttons.leftRoom() - 2 : 6));
	redraw();
}

bool MenuRow::openMenu(int index) {
	GList* kids = gtk_container_get_children(GTK_CONTAINER(menubar));
	GtkWidget* item = static_cast<GtkWidget*>(g_list_nth_data(kids, (guint)index));
	g_list_free(kids);
	if (item == nullptr) return false;
	win->updateActions();
	gtk_widget_mnemonic_activate(item, FALSE);   // as Alt and its letter
	return true;
}

void MenuRow::paint(cairo_t* cr, float w, float h) {
	Toolbar* t = win->toolbarWidget();
	const Color bg = t ? t->barColor() : chrome().bar();
	const bool dark = t ? t->barIsDark() : chrome().dark;
	if (dark != onDark) refresh();
	fillRect(cr, rectF(0, 0, w, h), bg);
	buttons.layout(w, h);
	buttons.paint(cr, win->window(), Chrome{ dark }.barInk());
}

void MenuRow::sizeChanged() { refresh(); }

void MenuRow::mouseMove(float x, float y) {
	buttons.setHot(buttons.at(x, y));
	animate();
}

void MenuRow::mouseLeave() {
	buttons.setHot(-1);
	animate();
}

void MenuRow::mouseDown(int button, float x, float y, bool, GdkEventButton* e) {
	const int b = buttons.at(x, y);
	if (b >= 0) {
		if (button == 1) { buttons.pressed = b; redraw(); }
		return;
	}
	titleRowPress(win->window(), e);
}

void MenuRow::mouseUp(int button, float x, float y) {
	if (button != 1 || buttons.pressed < 0) return;
	const int b = buttons.pressed;
	buttons.pressed = -1;
	if (buttons.at(x, y) == b) buttons.activate(b, win->window());
	redraw();
}
