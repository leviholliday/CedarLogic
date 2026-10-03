// Sheet: a window drawn whole with Cairo, as the Windows app's drawn windows
// (the truth table, Help, What's New...): a paint function, the places a
// click does something (hits, with the one under the pointer lit), scrolling
// and keys. GTK widgets (a text field) can sit over it in `overlay`. Runs
// modally over its owner until close().

#ifndef CL_LINUX_SHEET_H
#define CL_LINUX_SHEET_H

#include "Anim.h"
#include "Chrome.h"

#include <functional>
#include <string>
#include <vector>

class Sheet {
public:
	std::string title;
	int width = 760, height = 660, minWidth = 520, minHeight = 400;
	bool resizable = true;
	bool decorated = true;
	// No frame and see-through where the desktop composites (a card with its
	// own shadow); paint gets the whole window and should leave the edges clear.
	bool transparent = false;
	bool composited = false;   // set by run(): transparency is real
	std::function<void(Sheet&, cairo_t*, float w, float h)> paint;
	std::function<bool(Sheet&, guint keyval, guint state)> onKey;   // true when used
	std::function<void(Sheet&, float dy)> onScroll;                 // dy in points, down positive
	std::function<void(Sheet&)> onTick;
	// After the window is made, before it shows: widgets can go in `overlay`,
	// and `initialFocus` takes the keyboard instead of the drawing.
	std::function<void(Sheet&)> onOpen;
	GtkWidget* initialFocus = nullptr;
	// Closed, with the window and its widgets still there (to read them back).
	std::function<void(Sheet&)> onClose;                             // ~60 times a second while `animating`
	bool animating = false;

	struct Hit { RectF r; std::function<void()> act; };
	std::vector<Hit> hits;
	int hot = -1;
	// While painting: is the hit about to be added the one under the pointer?
	bool hotNext() const { return hot == (int)hits.size(); }
	void hit(const RectF& r, std::function<void()> act) { hits.push_back({ r, std::move(act) }); }

	GtkWidget* window = nullptr;
	GtkWidget* area = nullptr;
	GtkWidget* overlay = nullptr;   // widgets over the drawing (gtk_overlay_add_overlay)
	float pointerX = -1, pointerY = -1;

	void run(GtkWindow* owner);
	void close();
	void redraw() { if (area) gtk_widget_queue_draw(area); }
	bool closed() const { return done; }

private:
	GMainLoop* loop = nullptr;
	bool done = false;
	guint tickId = 0;
	int pressed = -1;
	int hitAt(float x, float y) const;
	static gboolean drawCb(GtkWidget*, cairo_t*, gpointer);
	static gboolean motionCb(GtkWidget*, GdkEventMotion*, gpointer);
	static gboolean leaveCb(GtkWidget*, GdkEventCrossing*, gpointer);
	static gboolean pressCb(GtkWidget*, GdkEventButton*, gpointer);
	static gboolean releaseCb(GtkWidget*, GdkEventButton*, gpointer);
	static gboolean scrollCb(GtkWidget*, GdkEventScroll*, gpointer);
	static gboolean keyCb(GtkWidget*, GdkEventKey*, gpointer);
	static gboolean deleteCb(GtkWidget*, GdkEvent*, gpointer);
	static void destroyCb(GtkWidget*, gpointer);
	static gboolean tickCb(GtkWidget*, GdkFrameClock*, gpointer);
};

#endif  // CL_LINUX_SHEET_H
