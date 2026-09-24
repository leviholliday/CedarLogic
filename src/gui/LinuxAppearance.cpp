/*****************************************************************************
   Project: CEDAR Logic Simulator
   LinuxAppearance: GTK-specific window behaviour -- matching GTK's own widgets
   to the app's light/dark theme, and knowing when a window is really up.
*****************************************************************************/

#ifdef __WXGTK__

#include "LinuxAppearance.h"

#include <wx/window.h>
#include <gtk/gtk.h>

void GtkSetPreferDarkTheme(bool dark) {
	GtkSettings* settings = gtk_settings_get_default();
	if (settings == nullptr) return;
	// Per application, not system-wide: it only changes what this process draws.
	g_object_set(settings, "gtk-application-prefer-dark-theme", dark ? TRUE : FALSE, nullptr);
}

bool GtkIsMappedOnScreen(wxWindow* window) {
	if (window == nullptr) return false;
	GtkWidget* widget = static_cast<GtkWidget*>(window->GetHandle());
	if (widget == nullptr) return false;
	GdkWindow* gdk = gtk_widget_get_window(widget);
	if (gdk == nullptr) return false;
	// WITHDRAWN clears when the X server reports the window mapped, which is
	// after the window manager has taken it -- unlike gtk_widget_get_mapped,
	// which flips as soon as GTK asks.
	return (gdk_window_get_state(gdk) & GDK_WINDOW_STATE_WITHDRAWN) == 0;
}

#endif // __WXGTK__
