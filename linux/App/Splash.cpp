// The launch screen (see Splash.h).

#include "Splash.h"

#include <algorithm>

namespace {
const int kMinVisibleMs = 500;
}

GtkWidget* showSplash() {
	GtkWidget* w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_decorated(GTK_WINDOW(w), FALSE);
	gtk_window_set_position(GTK_WINDOW(w), GTK_WIN_POS_CENTER);
	gtk_window_set_default_size(GTK_WINDOW(w), 340, 220);
	gtk_window_set_resizable(GTK_WINDOW(w), FALSE);
	gtk_window_set_keep_above(GTK_WINDOW(w), TRUE);
	gtk_window_set_skip_taskbar_hint(GTK_WINDOW(w), TRUE);
	gtk_window_set_type_hint(GTK_WINDOW(w), GDK_WINDOW_TYPE_HINT_SPLASHSCREEN);
	gtk_widget_set_name(w, "splash");

	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_container_set_border_width(GTK_CONTAINER(box), 32);
	gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
	gtk_container_add(GTK_CONTAINER(w), box);

	GtkWidget* icon = gtk_image_new_from_icon_name("cedarlogic", GTK_ICON_SIZE_DIALOG);
	gtk_image_set_pixel_size(GTK_IMAGE(icon), 96);
	gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);

	GtkWidget* title = gtk_label_new(nullptr);
	gtk_label_set_markup(GTK_LABEL(title), "<span size='xx-large' weight='bold'>CedarLogic</span>");
	gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 4);

	GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_halign(row, GTK_ALIGN_CENTER);
	GtkWidget* spinner = gtk_spinner_new();
	gtk_spinner_start(GTK_SPINNER(spinner));
	GtkWidget* status = gtk_label_new("Starting…");
	gtk_style_context_add_class(gtk_widget_get_style_context(status), "dim-label");
	gtk_box_pack_start(GTK_BOX(row), spinner, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(row), status, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 8);

	gtk_widget_show_all(w);
	// So it actually paints before whatever startup work runs next on this
	// same thread (loading the gate library, building the menus).
	while (gtk_events_pending()) gtk_main_iteration();
	g_object_set_data(G_OBJECT(w), "shown-at", GINT_TO_POINTER((int)(g_get_monotonic_time() / 1000)));
	return w;
}

void hideSplashSoon(GtkWidget* splash) {
	if (splash == nullptr) return;
	const int shownAt = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(splash), "shown-at"));
	const int elapsed = (int)(g_get_monotonic_time() / 1000) - shownAt;
	const int wait = std::max(0, kMinVisibleMs - elapsed);
	g_timeout_add(wait, +[](gpointer w) -> gboolean {
		gtk_widget_destroy(GTK_WIDGET(w));
		return G_SOURCE_REMOVE;
	}, splash);
}
