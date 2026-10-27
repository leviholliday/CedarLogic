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
	// How many canvases exist (--session-check: all gone with their window).
	static int liveCount;

	GtkWidget* widget() const { return area; }
	uint64_t pageKey() const { return key; }
	// The page's index in the document now (-1 once it's gone).
	int page() const;

	void redraw();
	// The same, for something drawn over the circuit (a fade, the opening
	// card, a note): what's under it is as it was, so a drag's static layer
	// (below) is kept.
	void redrawOver();
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

	// The camera, for putting it back after the circuit was reloaded in place.
	struct View { double originX, originY, upp; bool needsFit; };
	View view() const { return View{ originX, originY, upp, needsFit }; }
	void setView(const View& v);

	// Draw mode turned on or off, or another tool picked (the window's).
	void drawingChanged();

	// Called when the window's circuit changed under it (undo, a new page).
	void noteFit() { needsFit = true; }
	// Let go of the off-screen images (a tab that isn't in front).
	void dropBuffers();

private:
	// --bench-drag (Bench.cpp) drives the real handlers and draw() directly;
	// --session-check (SessionCheck.cpp) looks inside to check what it did.
	friend struct CanvasBench;
	friend struct SessionDriver;
	enum class Drag { None, Edit, Pan, Ink };

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
	// of their own while the camera stays put. On Wayland the window itself
	// is an image in memory, and the canvas draws straight onto it instead
	// (canDrawDirect) unless a glass panel needs the frame.
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
	// The camera of the frame before: the grid goes into its layer only once
	// the camera has stayed put for two frames (while it moves -- a pan, a
	// zoom -- the layer would be thrown away every frame, so the grid is
	// drawn straight in instead, one full-size copy fewer).
	GridKey lastGridKey{};
	bool lastGridKeySet = false;

	// What draw() did last (see SMOOTHNESS.md). `frameWhole`: `frame` holds
	// the whole canvas as it's on screen, so a redraw of part of it may draw
	// just that part. `frameFresh`: `frame` was drawn this time, so the glass
	// panels may blur it.
	bool frameWhole = false, frameFresh = false;
	// Drawing straight onto the window, without `frame`: when the window is
	// an image in memory (Wayland) and no glass panel needs `frame`.
	// CEDARLOGIC_CANVAS_FRAME=1 turns it off.
	bool canDrawDirect(cairo_t* cr, int scale) const;
	// For --bench-drag: a scale factor to draw at instead of the screen's,
	// and which way to draw (0 as above, 1 always through `frame`, 2 always
	// straight), and a cheaper antialiasing while dragging (also
	// CEDARLOGIC_FAST_DRAG=1; off by default).
	int scaleOverride = 0, pathOverride = 0;
	bool fastDrag = false;
	// While dragging, the minimap follows at most ten times a second (its
	// picture of the whole page is drawn again whenever the view or the
	// page's bounds move); it catches up when the drag ends.
	gint64 lastMiniMap = 0;
	// --bench-drag: each draw's time, in ms, while set.
	std::vector<double>* drawTimes = nullptr;

	// Draw only what moves (SMOOTHNESS.md): while parts are dragged, the page
	// without them -- background, grid, every other wire and gate, the
	// drawing -- is kept in `staticLayer`, made on the drag's first moving
	// frame (cl_document_draw_layer), and each frame copies it and draws the
	// moving parts on top. It's made again whenever its key changes (the
	// camera, the size, the theme and look, the grid, what's moving, an edit)
	// or anything else asks for a redraw (redraw(), not redrawOver(): the
	// simulation changing a wire, a menu command...). Made again three
	// frames running (a running clock, a zoom), it isn't worth it: the rest
	// of that drag is drawn whole, as it always was. CEDARLOGIC_DRAW_ALL=1
	// (and --bench-drag's "whole page" rows) turn it off.
	struct LayerKey {
		GridKey grid;
		double accent, wireScale;
		unsigned stamp;
		unsigned long long moving;
		bool operator==(const LayerKey& o) const {
			return grid == o.grid && accent == o.accent && wireScale == o.wireScale && stamp == o.stamp && moving == o.moving;
		}
	};
	cairo_surface_t* staticLayer = nullptr;
	LayerKey layerKey{};
	bool layerValid = false, layersOff = false, rebuiltLastFrame = false, drawAll = false;
	int rebuildsInARow = 0;
	unsigned layerGeneration = 0;   // one more each time the static layer is made
	// Counts kept for --bench-drag: frames drawn from the layer, and layers made.
	int layerFrames = 0, layerBuilds = 0;
	void dropLayer();

	// And the moving parts that only slide (cl_document_moving_rigid: the
	// gates and the wires that go whole with them -- all of it for a select
	// all): drawn once into `movingImage`, when the static layer is made, and
	// copied each frame, moved by whole pixels; the wires that reshape are
	// drawn live under it. It's made again with the static layer (the same
	// key, and redraw()), and not used while a new selection's halo fades in
	// (those frames draw the moving parts as before), for fewer than 8
	// selected gates (cheap to draw, and then exact), when it would be too
	// big (zoomed far in), or with CEDARLOGIC_DRAG_IMAGE=0. Off by at most
	// half a pixel until the drop draws it exactly.
	cairo_surface_t* movingImage = nullptr;
	double imageX = 0, imageY = 0, imageDX = 0, imageDY = 0;   // its corner in device pixels; the move's offset then
	unsigned imageGeneration = 0;
	int imageScale = 0;
	bool imagesOff = false, imagesDisabled = false;
	// Counts kept for --bench-drag: frames drawn with the image, and images made.
	int imageFrames = 0, imageBuilds = 0;
	void dropMovingImage();
	bool drawMovingImage(cairo_t* cr, int scale, const CLDrawOptions& o);
	bool drawLayered(cairo_t* cr, int scale, const CLDrawOptions& o, const GridKey& grid);
	void drawBackground(cairo_t* cr, int scale, const Palette& pal, const GridKey& key, double fade);
	void drawSceneTop(cairo_t* cr, int scale, bool sim, bool dark, bool projector);

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

	// Draw mode (Ink.h): a stroke or an eraser drag in progress, the pointer's
	// shape and the drawing bar over the bottom of the canvas.
	bool inkErasing = false;
	bool tabletEraser = false;   // a tablet pen's eraser end is near
	double eraserRadius() const { return 10 * upp; }   // 10 points on screen
	bool erasesWith(GdkEvent* e);
	double pressureOf(GdkEvent* e) const;
	void inkSample(GdkEvent* e, double vx, double vy);
	bool inkPress(GdkEventButton* e);
	void inkMotion(GdkEventMotion* e);
	void inkRelease(GdkEventButton* e);
	void inkHover(double vx, double vy);
	void inkHoverAtPointer();
	void endInkStroke(bool keep);
	bool inkKey(GdkEventKey* e);
	void drawInkBar(cairo_t* cr, float w, float h);
	bool inkBarAction(const char* action);

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
