// The circuit canvas: one per tab, each showing one page with its own camera.
// It draws the background and grid, asks the engine to draw the page on top,
// and turns clicks, drags, scrolls and keys into edits -- the Mac app's
// CanvasView and CLCanvas, the Linux app's Canvas, as a Win32 child window
// drawn with Direct2D.
//
// Camera: (originX, originY) is the world point at the view's top-left
// corner and `upp` how many world units one point covers (smaller = closer).
// World y points up; screen y points down. Points are 1/96 inch.

#ifndef CL_WINDOWS_CANVAS_H
#define CL_WINDOWS_CANVAS_H

#include "App.h"
#include <cstdint>
#include <vector>

class CircuitWindow;

class Canvas {
public:
	Canvas(CircuitWindow* window, HWND parent, uint64_t pageKey);
	~Canvas();

	HWND widget() const { return hwnd; }
	uint64_t pageKey() const { return key; }
	// The page's index in the document now (-1 once it's gone).
	int page() const;

	void redraw();
	// World <-> view points.
	void worldPoint(double vx, double vy, double& wx, double& wy) const;
	double width() const;    // points
	double height() const;
	double unitsPerPoint() const { return upp; }
	// The middle of the view, in the world.
	void center(double& wx, double& wy) const;
	// The top-left world point the camera is at (for the minimap).
	void origin(double& ox, double& oy) const { ox = originX; oy = originY; }
	// Jump the camera to centre on a world point, keeping the current zoom.
	void panTo(double wx, double wy);
	// Glide to centre a world point, keeping the zoom (Find's results).
	void centerOn(double wx, double wy);
	// Where the pointer is over this canvas, in the world; false when it's
	// somewhere else.
	bool pointerWorld(double& wx, double& wy) const;
	// A screen pixel to view points.
	void screenToView(POINT screen, double& vx, double& vy) const;

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
	// Let go of the drawing surface (a tab that isn't in front).
	void dropBuffers() { surface.release(); }
	void show(bool visible);
	void focus() { SetFocus(hwnd); }

	// Draw the canvas into any Direct2D target sized w x h points (the
	// window's own paint, and --screenshot).
	void drawInto(ID2D1RenderTarget* rt, double scale);
	// --wire-tag: the pointer resting on a wire (one carrying a 1 if there
	// is one), its value showing, for the screenshot. False: no wire.
	bool showWireTagNow();

private:
	enum class Drag { None, Edit, Pan, Slider };

	CircuitWindow* win;
	HWND hwnd = nullptr;
	uint64_t key;
	WindowSurface surface;
	double originX = -20, originY = 20, upp = 0.05;
	bool needsFit = true;
	Drag drag = Drag::None;
	double lastX = 0, lastY = 0;       // the pointer, in view points
	bool spaceDown = false, pannedWhileSpaceDown = false;
	bool pointerInside = false;
	double lastW = 0, lastH = 0;

	// An eased zoom in progress.
	bool zooming = false;
	double fromX = 0, fromY = 0, fromUpp = 1, toX = 0, toY = 0, toUpp = 1;
	double zoomStart = 0;
	void startZoom(double toOriginX, double toOriginY, double toUnitsPerPoint);

	double scale() const;               // pixels per point
	void viewPoint(LPARAM lp, double& vx, double& vy) const;
	void paint();
	void drawGrid(ID2D1RenderTarget* rt, const Palette& pal, double scale, double fade);
	void drawBox(ID2D1RenderTarget* rt, double l, double b, double r, double t, const RGBA& accent, double alpha);
	bool fitBoxOfPage(double& l, double& b, double& r, double& t) const;
	void setCursor(LPCWSTR which);

	void onPress(int button, double vx, double vy, bool doubleClick, WPARAM keys);
	void onRelease(int button, double vx, double vy);
	void onMotion(double vx, double vy);
	void onWheel(int delta, bool horizontal, WPARAM keys, POINT screen);
	bool onKeyDown(UINT vk, LPARAM lp);
	bool onKeyUp(UINT vk);
	bool onChar(wchar_t c);
	void onSize(double w, double h);
	bool moving() const;
	LPCWSTR cursorNow = nullptr;

	// What's drawn over the circuit (the Mac's overlays): the banner for
	// Tidy Up and Lock, Simulation View's control bar. Their
	// buttons, as drawn last (view points).
	struct OverlayHit { D2D1_RECT_F rect; int command; };
	std::vector<OverlayHit> hits;
	D2D1_RECT_F sliderTrack{};
	int hotHit = -1;
	void drawOverlays(ID2D1RenderTarget* rt, float w, float h);
	void drawSimBar(ID2D1RenderTarget* rt, float w, float h);
	void drawBanner(ID2D1RenderTarget* rt, float w);
	void drawOpeningCard(ID2D1RenderTarget* rt, float w, float h, double t);
	bool overlayPress(double vx, double vy);
	void setSpeedAt(double vx);

	// What the wire under the pointer carries, in a chip beside it once the
	// pointer has rested there a moment (Settings > Canvas), as the Mac's.
	bool tagShown = false, tagWaiting = false;
	bool wireTagText(std::string& text, char& state) const;
	void updateWireTag();
	void hideWireTag();
	void drawWireTag(ID2D1RenderTarget* rt, float w, float h);

	LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
	static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
	friend void registerCanvasClass();
};

void registerCanvasClass();

#endif  // CL_WINDOWS_CANVAS_H
