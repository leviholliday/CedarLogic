// A drawn window (see Sheet.h).

#include "Sheet.h"

int Sheet::hitAt(float x, float y) const {
	for (int i = (int)hits.size() - 1; i >= 0; i--) if (inRect(hits[i].r, x, y)) return i;
	return -1;
}

void Sheet::run(GtkWindow* owner) {
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), title.c_str());
	gtk_window_set_default_size(GTK_WINDOW(window), width, height);
	gtk_window_set_resizable(GTK_WINDOW(window), resizable);
	gtk_window_set_decorated(GTK_WINDOW(window), decorated);
	if (owner) {
		gtk_window_set_transient_for(GTK_WINDOW(window), owner);
		gtk_window_set_modal(GTK_WINDOW(window), TRUE);
		gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER_ON_PARENT);
	} else {
		gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
	}
	GdkGeometry g = {};
	g.min_width = minWidth;
	g.min_height = minHeight;
	gtk_window_set_geometry_hints(GTK_WINDOW(window), nullptr, &g, GDK_HINT_MIN_SIZE);
	if (transparent) {
		GdkScreen* screen = gtk_widget_get_screen(window);
		GdkVisual* rgba = gdk_screen_get_rgba_visual(screen);
		composited = rgba && gdk_screen_is_composited(screen);
		if (composited) {
			gtk_widget_set_visual(window, rgba);
			gtk_widget_set_app_paintable(window, TRUE);
		}
	}
	overlay = gtk_overlay_new();
	area = gtk_drawing_area_new();
	gtk_widget_set_can_focus(area, TRUE);
	gtk_widget_add_events(area, GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
	                                GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK | GDK_KEY_PRESS_MASK);
	gtk_container_add(GTK_CONTAINER(overlay), area);
	gtk_container_add(GTK_CONTAINER(window), overlay);
	g_signal_connect(area, "draw", G_CALLBACK(drawCb), this);
	g_signal_connect(area, "motion-notify-event", G_CALLBACK(motionCb), this);
	g_signal_connect(area, "leave-notify-event", G_CALLBACK(leaveCb), this);
	g_signal_connect(area, "button-press-event", G_CALLBACK(pressCb), this);
	g_signal_connect(area, "button-release-event", G_CALLBACK(releaseCb), this);
	g_signal_connect(area, "scroll-event", G_CALLBACK(scrollCb), this);
	g_signal_connect(window, "key-press-event", G_CALLBACK(keyCb), this);
	g_signal_connect(window, "delete-event", G_CALLBACK(deleteCb), this);
	tickId = gtk_widget_add_tick_callback(area, tickCb, this, nullptr);
	gtk_widget_show_all(window);
	gtk_widget_grab_focus(area);
	loop = g_main_loop_new(nullptr, FALSE);
	if (!done) g_main_loop_run(loop);
	g_main_loop_unref(loop);
	loop = nullptr;
	gtk_widget_remove_tick_callback(area, tickId);
	g_signal_handlers_disconnect_by_data(area, this);
	g_signal_handlers_disconnect_by_data(window, this);
	gtk_widget_destroy(window);
	window = area = overlay = nullptr;
	if (owner) gtk_window_present(owner);
}

void Sheet::close() {
	done = true;
	if (loop) g_main_loop_quit(loop);
}

gboolean Sheet::drawCb(GtkWidget* w, cairo_t* cr, gpointer self) {
	Sheet* s = static_cast<Sheet*>(self);
	guarded("drawing a window", [&] {
		if (s->transparent && s->composited) {
			cairo_save(cr);
			cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
			cairo_set_source_rgba(cr, 0, 0, 0, 0);
			cairo_paint(cr);
			cairo_restore(cr);
		}
		s->hits.clear();
		if (s->paint) s->paint(*s, cr, gtk_widget_get_allocated_width(w), gtk_widget_get_allocated_height(w));
	});
	return TRUE;
}

gboolean Sheet::motionCb(GtkWidget* w, GdkEventMotion* e, gpointer self) {
	Sheet* s = static_cast<Sheet*>(self);
	s->pointerX = (float)e->x;
	s->pointerY = (float)e->y;
	const int i = s->hitAt((float)e->x, (float)e->y);
	if (i != s->hot) {
		s->hot = i;
		GdkWindow* gw = gtk_widget_get_window(w);
		GdkCursor* c = i >= 0 ? gdk_cursor_new_from_name(gdk_window_get_display(gw), "pointer") : nullptr;
		gdk_window_set_cursor(gw, c);
		if (c) g_object_unref(c);
		s->redraw();
	}
	return TRUE;
}

gboolean Sheet::leaveCb(GtkWidget*, GdkEventCrossing*, gpointer self) {
	Sheet* s = static_cast<Sheet*>(self);
	s->pointerX = s->pointerY = -1;
	if (s->hot >= 0) { s->hot = -1; s->redraw(); }
	return FALSE;
}

gboolean Sheet::pressCb(GtkWidget* w, GdkEventButton* e, gpointer self) {
	Sheet* s = static_cast<Sheet*>(self);
	gtk_widget_grab_focus(w);
	if (e->type == GDK_BUTTON_PRESS && e->button == 1) s->pressed = s->hitAt((float)e->x, (float)e->y);
	return TRUE;
}

gboolean Sheet::releaseCb(GtkWidget*, GdkEventButton* e, gpointer self) {
	Sheet* s = static_cast<Sheet*>(self);
	if (e->button != 1) return TRUE;
	const int i = s->hitAt((float)e->x, (float)e->y);
	const int was = s->pressed;
	s->pressed = -1;
	if (i >= 0 && i == was) {
		auto act = s->hits[i].act;
		guarded("a click", [&] { if (act) act(); });
		s->redraw();
	}
	return TRUE;
}

gboolean Sheet::scrollCb(GtkWidget*, GdkEventScroll* e, gpointer self) {
	Sheet* s = static_cast<Sheet*>(self);
	if (!s->onScroll) return FALSE;
	double dy = 0;
	if (e->direction == GDK_SCROLL_SMOOTH) {
		double dx = 0;
		gdk_event_get_scroll_deltas((GdkEvent*)e, &dx, &dy);
		dy *= 40;
	} else if (e->direction == GDK_SCROLL_UP) dy = -56;
	else if (e->direction == GDK_SCROLL_DOWN) dy = 56;
	s->onScroll(*s, (float)dy);
	s->redraw();
	return TRUE;
}

gboolean Sheet::keyCb(GtkWidget*, GdkEventKey* e, gpointer self) {
	Sheet* s = static_cast<Sheet*>(self);
	if (s->onKey && guarded("a key", [&] { return s->onKey(*s, e->keyval, e->state); })) { s->redraw(); return TRUE; }
	if (e->keyval == GDK_KEY_Escape) { s->close(); return TRUE; }
	return FALSE;
}

gboolean Sheet::deleteCb(GtkWidget*, GdkEvent*, gpointer self) {
	static_cast<Sheet*>(self)->close();
	return TRUE;
}

gboolean Sheet::tickCb(GtkWidget*, GdkFrameClock*, gpointer self) {
	Sheet* s = static_cast<Sheet*>(self);
	if (s->animating) {
		if (s->onTick) guarded("an animation", [&] { s->onTick(*s); });
		s->redraw();
	}
	return G_SOURCE_CONTINUE;
}
