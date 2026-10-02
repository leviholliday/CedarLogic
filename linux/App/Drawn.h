// Drawn: a widget that draws itself with Cairo, in points, and hands its
// pointer to the subclass in points too -- what the toolbar, the tab strip
// and the side panel are made of (the Windows app's Drawn, in GTK). Hover
// tracking and tooltips per rectangle come with it.

#ifndef CL_LINUX_DRAWN_H
#define CL_LINUX_DRAWN_H

#include "Chrome.h"
#include <string>
#include <vector>

class Drawn {
public:
	virtual ~Drawn();
	GtkWidget* widget() const { return area; }
	void redraw() { if (area) gtk_widget_queue_draw(area); }
	float width() const;    // points
	float height() const;

protected:
	// Make the widget (call from the subclass's constructor).
	void create();

	virtual void paint(cairo_t* cr, float w, float h) = 0;
	virtual void mouseMove(float, float) {}
	virtual void mouseLeave() {}
	// button 1 left, 2 middle, 3 right; the event for menus and window drags.
	virtual void mouseDown(int /*button*/, float, float, bool /*doubleClick*/, GdkEventButton* /*e*/) {}
	virtual void mouseUp(int /*button*/, float, float) {}
	virtual void wheel(double /*dy*/, float, float) {}
	virtual void sizeChanged() {}

	// Tooltips: replace the set, each a rectangle in points.
	struct Tip { RectF rect; std::string text; };
	void setTips(const std::vector<Tip>& tips) { tipList = tips; }

	GtkWidget* area = nullptr;
	bool pointerIn = false;

private:
	std::vector<Tip> tipList;
	static gboolean drawCb(GtkWidget*, cairo_t*, gpointer);
	static gboolean motionCb(GtkWidget*, GdkEventMotion*, gpointer);
	static gboolean crossingCb(GtkWidget*, GdkEventCrossing*, gpointer);
	static gboolean pressCb(GtkWidget*, GdkEventButton*, gpointer);
	static gboolean releaseCb(GtkWidget*, GdkEventButton*, gpointer);
	static gboolean scrollCb(GtkWidget*, GdkEventScroll*, gpointer);
	static gboolean tooltipCb(GtkWidget*, gint, gint, gboolean, GtkTooltip*, gpointer);
	static void sizeCb(GtkWidget*, GdkRectangle*, gpointer);
};

#endif  // CL_LINUX_DRAWN_H
