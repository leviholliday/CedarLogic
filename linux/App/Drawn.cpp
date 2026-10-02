// Drawn widgets (see Drawn.h).

#include "Drawn.h"

Drawn::~Drawn() {
	if (area) {
		g_signal_handlers_disconnect_by_data(area, this);
		g_object_unref(area);
	}
}

void Drawn::create() {
	area = gtk_drawing_area_new();
	g_object_ref_sink(area);
	gtk_widget_add_events(area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK | GDK_ENTER_NOTIFY_MASK |
	                            GDK_LEAVE_NOTIFY_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
	gtk_widget_set_has_tooltip(area, TRUE);
	g_signal_connect(area, "draw", G_CALLBACK(drawCb), this);
	g_signal_connect(area, "motion-notify-event", G_CALLBACK(motionCb), this);
	g_signal_connect(area, "enter-notify-event", G_CALLBACK(crossingCb), this);
	g_signal_connect(area, "leave-notify-event", G_CALLBACK(crossingCb), this);
	g_signal_connect(area, "button-press-event", G_CALLBACK(pressCb), this);
	g_signal_connect(area, "button-release-event", G_CALLBACK(releaseCb), this);
	g_signal_connect(area, "scroll-event", G_CALLBACK(scrollCb), this);
	g_signal_connect(area, "query-tooltip", G_CALLBACK(tooltipCb), this);
	g_signal_connect(area, "size-allocate", G_CALLBACK(sizeCb), this);
}

float Drawn::width() const { return area ? (float)gtk_widget_get_allocated_width(area) : 0; }
float Drawn::height() const { return area ? (float)gtk_widget_get_allocated_height(area) : 0; }

gboolean Drawn::drawCb(GtkWidget* w, cairo_t* cr, gpointer self) {
	Drawn* d = static_cast<Drawn*>(self);
	guarded("drawing", [&] { d->paint(cr, (float)gtk_widget_get_allocated_width(w), (float)gtk_widget_get_allocated_height(w)); });
	return TRUE;
}

gboolean Drawn::motionCb(GtkWidget*, GdkEventMotion* e, gpointer self) {
	Drawn* d = static_cast<Drawn*>(self);
	d->pointerIn = true;
	guarded("the pointer", [&] { d->mouseMove((float)e->x, (float)e->y); });
	return FALSE;
}

gboolean Drawn::crossingCb(GtkWidget*, GdkEventCrossing* e, gpointer self) {
	Drawn* d = static_cast<Drawn*>(self);
	if (e->type == GDK_LEAVE_NOTIFY && e->mode == GDK_CROSSING_NORMAL) {
		d->pointerIn = false;
		guarded("the pointer", [&] { d->mouseLeave(); });
	} else if (e->type == GDK_ENTER_NOTIFY) {
		d->pointerIn = true;
	}
	return FALSE;
}

gboolean Drawn::pressCb(GtkWidget*, GdkEventButton* e, gpointer self) {
	Drawn* d = static_cast<Drawn*>(self);
	// A double click arrives as a second press, then this.
	if (e->type == GDK_3BUTTON_PRESS) return TRUE;
	guarded("a click", [&] { d->mouseDown((int)e->button, (float)e->x, (float)e->y, e->type == GDK_2BUTTON_PRESS, e); });
	return TRUE;
}

gboolean Drawn::releaseCb(GtkWidget*, GdkEventButton* e, gpointer self) {
	Drawn* d = static_cast<Drawn*>(self);
	guarded("a click", [&] { d->mouseUp((int)e->button, (float)e->x, (float)e->y); });
	return TRUE;
}

gboolean Drawn::scrollCb(GtkWidget*, GdkEventScroll* e, gpointer self) {
	Drawn* d = static_cast<Drawn*>(self);
	double dy = 0;
	if (e->direction == GDK_SCROLL_UP) dy = -1;
	else if (e->direction == GDK_SCROLL_DOWN) dy = 1;
	else if (e->direction == GDK_SCROLL_SMOOTH) { double dx; gdk_event_get_scroll_deltas((GdkEvent*)e, &dx, &dy); }
	guarded("scrolling", [&] { d->wheel(dy, (float)e->x, (float)e->y); });
	return TRUE;
}

gboolean Drawn::tooltipCb(GtkWidget*, gint x, gint y, gboolean keyboard, GtkTooltip* tip, gpointer self) {
	Drawn* d = static_cast<Drawn*>(self);
	if (keyboard) return FALSE;
	for (const Tip& t : d->tipList) {
		if (!inRect(t.rect, (float)x, (float)y) || t.text.empty()) continue;
		gtk_tooltip_set_text(tip, t.text.c_str());
		// So moving to the next button brings up its own.
		GdkRectangle r = { (int)t.rect.left, (int)t.rect.top, (int)(t.rect.right - t.rect.left), (int)(t.rect.bottom - t.rect.top) };
		gtk_tooltip_set_tip_area(tip, &r);
		return TRUE;
	}
	return FALSE;
}

void Drawn::sizeCb(GtkWidget*, GdkRectangle*, gpointer self) {
	Drawn* d = static_cast<Drawn*>(self);
	guarded("sizing", [&] { d->sizeChanged(); });
}
