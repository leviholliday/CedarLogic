// The circuit canvas: one per tab, each showing one page with its own camera.
// It draws the background and grid, asks the engine to draw the page on top,
// and turns clicks, drags, scrolls and keys into edits -- the Mac app's
// CanvasView and CLCanvas, in GTK.
//
// Camera: (originX, originY) is the world point at the view's top-left
// corner and `upp` how many world units one point covers (smaller = closer).
// World y points up; screen y points down.

#ifndef CL_LINUX_CANVAS_H
#define CL_LINUX_CANVAS_H

#include "Anim.h"
#include "App.h"
#include "Chrome.h"
#include <cstdint>
#include <string>
#include <vector>

class CircuitWindow;

class Canvas {
public:
	Canvas(CircuitWindow* window, uint64_t pageKey);
	~Canvas();

	GtkWidget* widget() const { return area; }
	uint64_t pageKey() const { return key; }
	// The page's index in the document now (-1 once it's gone).
	int page() const;

	void redraw();
	// World <-> view points.
	void worldPoint(double vx, double vy, double& wx, double& wy) const;
	double width() const;
	double height() const;
	double unitsPerPoint() const { return upp; }
	// The middle of the view, in the world.
	void center(double& wx, double& wy) const;
	// The top-left world point the camera is at (for the minimap).
	void origin(double& ox, double& oy) const { ox = originX; oy = originY; }
	// Jump the camera to centre on a world point, keeping the current zoom
	// (the minimap's click-to-go-there; no easing, unlike animateZoom).
	void panTo(double wx, double wy);
	// Where the pointer is over this canvas, in the world; false when it's
	// somewhere else.
	bool pointerWorld(double& wx, double& wy) const;

	// Camera. The animated ones ease over 140 ms, as the wx app's zoom does.
	void zoomToFit(bool animate);
	void zoomBy(double factor, double vx, double vy);   // >1 closer, keeping (vx, vy) still
	void animateZoom(double factor);                    // about the middle
	void zoomActual();
	void pan(double dx, double dy);                     // in points
	bool stepAnimation();                               // per clock tick; true while moving
	int zoomPercent() const;

	// Something outside the canvas ended what the pointer was doing.
	void cancelDrag();
	bool isDragging() const { return drag != Drag::None; }

	// Called when the window's circuit changed under it (undo, a new page).
	void noteFit() { needsFit = true; }
	// Let go of the off-screen images (a tab that isn't in front).
	void dropBuffers();

private:
	enum class Drag { None, Edit, Pan };

	CircuitWindow* win;
	GtkWidget* area;
	uint64_t key;
	double originX = -20, originY = 20, upp = 0.05;
	bool needsFit = true;
	Drag drag = Drag::None;
	double lastX = 0, lastY = 0;       // the pointer, in view points
	bool spaceDown = false, pannedWhileSpaceDown = false;
	bool pointerInside = false;
	// Settings > Canvas > Wires: what a wire carries, after resting on it.
	bool tagShown = false;
	guint tagTimer = 0;
	bool wireTagText(std::string& text, char& state) const;
	void updateWireTag();
	void hideWireTag();
	void drawWireTag(cairo_t* cr, float w, float h);

	// An eased zoom in progress.
	bool zooming = false;
	double fromX = 0, fromY = 0, fromUpp = 1, toX = 0, toY = 0, toUpp = 1;
	gint64 zoomStart = 0;
	void startZoom(double toOriginX, double toOriginY, double toUnitsPerPoint);

	// A touchpad pinch.
	double pinchLast = 1;

	// The view is drawn into an image in memory, on the processor, and put on
	// screen in one piece: on X11 that's one image sent to the display
	// instead of every shape. The background and grid are kept in a layer
	// of their own while the camera stays put.
	cairo_surface_t* frame = nullptr;
	cairo_surface_t* gridLayer = nullptr;
	struct GridKey {
		double ox, oy, upp, fade;
		int w, h, scale, style;
		bool dark, sim, major, show;
		bool operator==(const GridKey& o) const {
			return ox == o.ox && oy == o.oy && upp == o.upp && fade == o.fade && w == o.w && h == o.h &&
			       scale == o.scale && style == o.style && dark == o.dark && sim == o.sim && major == o.major && show == o.show;
		}
	};
	GridKey gridKey{};
	bool gridValid = false;

	void draw(cairo_t* cr);
	// Over the circuit: the banner and its buttons, the note.
	void drawOpeningCard(cairo_t* cr, float w, float h, double t);
	void drawOverlays(cairo_t* cr, float w, float h);
	void drawBanner(cairo_t* cr, float w);
	// The banner and the Simulation View bar come and go with a little motion.
	anim::Tween bannerFade, simBarIn;
	std::string bannerText;
	std::vector<std::pair<std::string, const char*>> bannerButtons;
	bool bannerLocked = false;
	guint motionTick = 0;
	void keepMoving();
	// Frosted glass: what's under r, blurred, for a panel over the canvas.
	void frosted(cairo_t* cr, const RectF& r, float radius);
	void drawToast(cairo_t* cr, float w, float h);
	void drawSimBar(cairo_t* cr, float w, float h);
	float sliderLeft = 0, sliderRight = 0, sliderTop = 0, sliderBottom = 0;   // Simulation View's speed
	bool sliderDragging = false;
	void setSpeedAt(double x);
	struct OverlayHit { float left, top, right, bottom; const char* action; };
	std::vector<OverlayHit> hits;
	int hotHit = -1;
	bool overlayPress(double x, double y);
	void drawScene(cairo_t* cr, int scale);
	void drawGrid(cairo_t* cr, const Palette& pal, double scale, double fade);
	void drawBox(cairo_t* cr, double l, double b, double r, double t, const RGBA& accent, double alpha);
	bool fitBoxOfPage(double& l, double& b, double& r, double& t) const;

	bool onPress(GdkEventButton* e);
	bool onRelease(GdkEventButton* e);
	bool onMotion(GdkEventMotion* e);
	bool onScroll(GdkEventScroll* e);
	bool onKeyPress(GdkEventKey* e);
	bool onKeyRelease(GdkEventKey* e);
	void onPinch(GdkEventTouchpadPinch* e);
	void onSizeChanged(int oldW, int oldH, int w, int h);
	void contextMenu(GdkEventButton* e);
	bool moving() const;
	int lastW = 0, lastH = 0;

	static gboolean drawCb(GtkWidget*, cairo_t*, gpointer);
	static gboolean pressCb(GtkWidget*, GdkEventButton*, gpointer);
	static gboolean releaseCb(GtkWidget*, GdkEventButton*, gpointer);
	static gboolean motionCb(GtkWidget*, GdkEventMotion*, gpointer);
	static gboolean scrollCb(GtkWidget*, GdkEventScroll*, gpointer);
	static gboolean keyPressCb(GtkWidget*, GdkEventKey*, gpointer);
	static gboolean keyReleaseCb(GtkWidget*, GdkEventKey*, gpointer);
	static gboolean crossingCb(GtkWidget*, GdkEventCrossing*, gpointer);
	static gboolean grabBrokenCb(GtkWidget*, GdkEventGrabBroken*, gpointer);
	static gboolean focusOutCb(GtkWidget*, GdkEventFocus*, gpointer);
	static gboolean eventCb(GtkWidget*, GdkEvent*, gpointer);
	static void sizeCb(GtkWidget*, GdkRectangle*, gpointer);
	static void dragReceivedCb(GtkWidget*, GdkDragContext*, gint, gint, GtkSelectionData*, guint, guint, gpointer);
	static gboolean dragMotionCb(GtkWidget*, GdkDragContext*, gint, gint, guint, gpointer);
};

#endif  // CL_LINUX_CANVAS_H
