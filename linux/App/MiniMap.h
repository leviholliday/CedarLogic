// The whole page, small, under the palette, with the canvas's visible area
// outlined in red: click or drag to jump there. The circuit's picture is
// cached and only redrawn when the page, its contents, the window's size or
// the theme changes; panning and zooming only move the outline (the Mac
// app's MiniMap, in GTK).

#ifndef CL_LINUX_MINIMAP_H
#define CL_LINUX_MINIMAP_H

#include "App.h"

class CircuitWindow;

class MiniMap {
public:
	explicit MiniMap(CircuitWindow* window);
	~MiniMap();

	GtkWidget* widget() const { return area; }
	// Cheap: just queues a redraw. The picture itself is only rebuilt when
	// draw() finds its cache key has changed.
	void queueDraw();

private:
	CircuitWindow* win;
	GtkWidget* area;
	cairo_surface_t* cache = nullptr;
	std::string cacheKey;
	bool dragging = false;

	// The box the thumbnail covers (the page's contents, unioned with what's
	// currently visible, so panning past the edge never leaves the outline
	// off the picture) and its scale.
	bool fit(double w, double h, double& left, double& bottom, double& right, double& top, double& upp) const;
	void draw(cairo_t* cr);
	void goTo(double vx, double vy);

	static gboolean drawCb(GtkWidget*, cairo_t*, gpointer);
	static gboolean pressCb(GtkWidget*, GdkEventButton*, gpointer);
	static gboolean releaseCb(GtkWidget*, GdkEventButton*, gpointer);
	static gboolean motionCb(GtkWidget*, GdkEventMotion*, gpointer);
};

#endif  // CL_LINUX_MINIMAP_H
