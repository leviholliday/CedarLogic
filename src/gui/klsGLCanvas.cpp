/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   klsGLCanvas: Generic implementation of OpenGL canvas
*****************************************************************************/

#include "klsGLCanvas.h"
#include <cstdlib>
#include "Settings.h"
#include "MainApp.h"
#include "paramDialog.h"
#include "render/RendererHealth.h"
#include "wx/msgdlg.h"
#ifdef __APPLE__
#include "MacScrollDevice.h"
#endif
#include <cstdio>
#ifdef __WXGTK__
#include "DcRasterPaint.h"
#endif

// Included to use the min() and max() templates:
#include <algorithm>
using namespace std;

// Activate this #define statement if you want to
// show the mouse handling arrows:
//#define CANVAS_DEBUG_TESTS_ON


DECLARE_APP(MainApp)

BEGIN_EVENT_TABLE(klsGLCanvas, wxGLCanvas)
    EVT_PAINT(klsGLCanvas::wxOnPaint)
    EVT_SIZE(klsGLCanvas::wxOnSize)
    EVT_ERASE_BACKGROUND(klsGLCanvas::wxOnEraseBackground)

	EVT_MOUSEWHEEL(klsGLCanvas::wxOnMouseWheel)
	EVT_MAGNIFY(klsGLCanvas::wxOnMagnify)
    EVT_MOUSE_EVENTS(klsGLCanvas::wxOnMouseEvent)
    EVT_MOUSE_CAPTURE_LOST(klsGLCanvas::wxOnCaptureLost)

    EVT_KEY_DOWN(klsGLCanvas::wxKeyDown)
    EVT_KEY_UP(klsGLCanvas::wxKeyUp)

   	EVT_TIMER(SCROLL_TIMER_ID, klsGLCanvas::OnScrollTimer)
   	EVT_TIMER(ZOOM_ANIM_TIMER_ID, klsGLCanvas::OnZoomAnimTimer)
END_EVENT_TABLE()


klsGLCanvas::klsGLCanvas(wxWindow *parent, const wxString& name, wxWindowID id,
						const wxPoint& pos, const wxSize& size, long style ) :
						wxGLCanvas(parent, glCanvasAttributes(), id, pos, size, style|wxFULL_REPAINT_ON_RESIZE|wxWANTS_CHARS, name) {

	// Zoom and OpenGL coordinate of upper-left corner of this canvas:
	viewZoom = DEFAULT_ZOOM;
	panX = panY = 0.0;

	autoScrollEnable();
	
	// The mouse is in the window:
	mouseOutOfWindow = false;

	// Mouse wheel rotation tracking variable:
	wheelRotation = 0.0;

	// Set the mouse coords memory:
	setMouseCoords( GLPoint2f(0.0,0.0) );
	setMouseScreenCoords( wxPoint( 0, 0 ) );

	setIsDragging( false, BUTTON_LEFT );
	setDragStartCoords( GLPoint2f(0.0,0.0), BUTTON_LEFT );
	setDragEndCoords( GLPoint2f(0.0,0.0), BUTTON_LEFT );

	setIsDragging( false, BUTTON_MIDDLE );
	setDragStartCoords( GLPoint2f(0.0,0.0), BUTTON_MIDDLE );
	setDragEndCoords( GLPoint2f(0.0,0.0), BUTTON_MIDDLE );

	setIsDragging( false, BUTTON_RIGHT );
	setDragStartCoords( GLPoint2f(0.0,0.0), BUTTON_RIGHT );
	setDragEndCoords( GLPoint2f(0.0,0.0), BUTTON_RIGHT );

	// Set up scrolling timer:
	scrollTimer = new wxTimer(this, SCROLL_TIMER_ID);
	scrollTimer->Stop();

	zoomAnimTimer = new wxTimer(this, ZOOM_ANIM_TIMER_ID);

	setHorizGrid( 1 );
	setHorizGridColor( 0, 0, (GLfloat) GRID_INTENSITY, (GLfloat) GRID_INTENSITY );
	disableHorizGrid();

	setVertGrid( 1 );
	setVertGridColor( 0, 0, (GLfloat) GRID_INTENSITY, (GLfloat) GRID_INTENSITY );
	disableVertGrid();

	deferPaint = false;
	panning = false;
	lastPanPaintMs = 0;

	minimap = NULL;
	
	canvasLocked = false;
}


klsGLCanvas::~klsGLCanvas() {
	scrollTimer->Stop();
	delete scrollTimer;
	zoomAnimTimer->Stop();
	delete zoomAnimTimer;
	return;
}

void klsGLCanvas::updateMiniMap() {
	GLPoint2f p1, p2;
	getViewport( p1, p2 );
	if (minimap != NULL) minimap->update(p1, p2);
}

// Set the viewport (Set the left/top and right/bottom coordinates).
// NOTE: It will enforce a 1:1 aspect ratio, but it will make the best
// attempt to fit the zoom box as close as possible. Basically, it will
// fit the longest side to the window, and center the rest.
void klsGLCanvas::setViewport( GLPoint2f topLeft, GLPoint2f bottomRight ) {
	wxSize sz = GetClientSize();
	double sAspect = (double) sz.GetHeight() / (double) sz.GetWidth();

	double newWidth = bottomRight.x - topLeft.x;
	double newHeight = topLeft.y - bottomRight.y;
	double aspect = newHeight / newWidth;
	
	bool useWidth = aspect < sAspect; // Use the width as the limiting factor.
	
	double newZoom = 1.0;
	GLPoint2f newPan;
	
	if( useWidth ) {
		// The box width determines the new zoom factor:
		newZoom = newWidth / sz.GetWidth();

		// The x coordinate is the edge of the box:
		newPan.x = topLeft.x;

		// The y coordinate must center the box:
		newPan.y = topLeft.y + 0.5 * (sz.GetHeight() * newZoom - newHeight); // y + (1/2 of the leftover margins)
	} else {
		// The box height determines the new zoom factor:
		newZoom = newHeight / sz.GetHeight();

		// The y coordinate is the edge of the box:
		newPan.y = topLeft.y;

		// The x coordinate must center the box:
		newPan.x = topLeft.x - 0.5 * (sz.GetWidth() * newZoom - newWidth); // x - (1/2 of the leftover margins)
	}

	// Set the new viewport:
	setZoom( newZoom );
	setPan( newPan.x, newPan.y );
}


void klsGLCanvas::getViewport( GLPoint2f& p1, GLPoint2f& p2 ) {
	wxSize sz = GetClientSize();
	p1.x = panX;
	p1.y = panY;
	p2.x = panX + (sz.GetWidth()*viewZoom);
	p2.y = panY - (sz.GetHeight()*viewZoom);
}

GLPoint2f klsGLCanvas::mapToCanvas(wxPoint m) {
	int w, h;
	GetClientSize(&w, &h);

	float glX = panX + (m.x * viewZoom);
	float glY = panY - (m.y * viewZoom);

	return GLPoint2f(glX, glY);
}


void klsGLCanvas::wxOnPaint(wxPaintEvent& event) {
	wxPaintDC dc(this);
	wxGetApp().SetCurrentCanvas(this);
	// With a context current, ask the driver what it is. Costs nothing after the
	// first answer, and it is the one fact a "drawing is slow" report never
	// carries. See render/RendererHealth.h.
	cl::render::noteGLImplementation(
		(const char*)glGetString(GL_VENDOR),
		(const char*)glGetString(GL_RENDERER),
		(const char*)glGetString(GL_VERSION));
	// No GL state to set up here: Skia owns the pipeline and sets clear colour,
	// blending, and pixel store per draw. (The old fixed-function setup would
	// also be invalid under the core profile macOS now asks for.)
#ifdef __WXGTK__
	// Catches the frame if Skia has fallen back to the processor; see
	// DcRasterPaint.h for why GTK shows it without OpenGL.
	DcRasterPaint cpuFrame;
#endif
	if (!renderSkiaLive()) {
		// Neither the graphics driver nor the processor fallback could produce a
		// frame. Clear anyway: an untouched back buffer swaps in as black, and a
		// black canvas reads as a broken app rather than a renderer that gave up.
		glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	// Outside the branch on purpose. The usual outcome now is that the fallback
	// drew the frame, so nothing looks wrong except the speed, and the reason for
	// the speed is exactly what wants saying.
	announceRendererFailure();

#ifdef __WXGTK__
	if (cpuFrame.captured()) {
		cpuFrame.paint(dc, GetContentScaleFactor());
		return;
	}
#endif
	// Show the new buffer:
	glFlush();
	SwapBuffers();
}

// Tell the user, once per run, that the graphics driver could not be used and
// the processor is drawing instead. Without this the program is just mysteriously
// slow. Deferred with CallAfter, because a modal dialog opened from inside a
// paint handler repaints the window it came from.
void klsGLCanvas::announceRendererFailure() {
	static bool announced = false;
	if (announced || !cl::render::rendererFailed()) return;
	announced = true;
	const wxString msg = cl::render::rendererFailureMessage();
	if (msg.empty()) return;   // expected on this driver; see rendererFailureMessage
	CallAfter([msg] {
		wxMessageBox(msg, "Rendering issue", wxOK | wxICON_WARNING);
	});
}


void klsGLCanvas::wxOnEraseBackground(wxEraseEvent& WXUNUSED(event))
{
  // We're using double-buffering, so do nothing, to avoid flashing.
}


// The OS took the mouse capture away mid-drag (alt-tab, a popup, etc.). End any
// active drags so state doesn't get stuck; endDrag(BUTTON_MIDDLE) also flushes
// the final pan frame the throttle may have skipped.
void klsGLCanvas::wxOnCaptureLost(wxMouseCaptureLostEvent& WXUNUSED(event))
{
	if (isDragging(BUTTON_LEFT))   endDrag(BUTTON_LEFT);
	if (isDragging(BUTTON_MIDDLE)) endDrag(BUTTON_MIDDLE);
	if (isDragging(BUTTON_RIGHT))  endDrag(BUTTON_RIGHT);
	// Base flags are cleared; let the subclass unwind its own drag state (a
	// half-placed new gate, an in-progress paste or move) which endDrag doesn't
	// touch -- otherwise it stays desynced from the now-cleared flags.
	cancelDrag();
}


void klsGLCanvas::wxOnSize(wxSizeEvent& event)
{
	wxGetApp().SetCurrentCanvas(this);
	Refresh();
}


void klsGLCanvas::getPan( GLdouble &x, GLdouble &y ) {
	x = this->panX;
	y = this->panY;
}


void klsGLCanvas::setPan( GLdouble newX, GLdouble newY ) {
	// Clamp the panning ranges:
	newX = max(newX, MIN_PAN);
	newX = min(newX, MAX_PAN);

	newY = max(newY, MIN_PAN);
	newY = min(newY, MAX_PAN);

	// Set the new pan values:
	panX = newX;
	panY = newY;

	// Reset the mouse coordinates to the new pan settings, and call OnMouseMove()
	// because the mouse's gl coords have changed. During a middle-drag pan we skip
	// OnMouseMove -- its collision/hover work is irrelevant to panning and would
	// run every mouse-move (twice, with the caller's own call), stuttering the
	// drag; setMouseCoords still runs so the next drag delta is computed correctly.
	setMouseCoords();
	if (!panning) {
		GLPoint2f m = getMouseCoords();
		OnMouseMove(m.x, m.y, isShiftDown, isControlDown);
	}
	updateMiniMap();

	// During a compound camera move (zoom = setZoom + setCenter, each of which
	// calls setPan) skip the repaint until the final state, so we don't flash the
	// intermediate frame.
	if (deferPaint) return;

	// While panning, throttle the synchronous repaint to ~frame rate. A real
	// mouse fires moves far faster than we can paint; without this each move
	// forces a full synchronous paint and they back up, so the view lags the
	// cursor. Intermediate moves still update panX/panY above, so no motion is
	// lost -- the next painted frame just uses the latest position. endDrag forces
	// a final paint so the last move always lands.
	if (panning) {
		wxLongLong now = wxGetLocalTimeMillis();
		if ((now - lastPanPaintMs).GetValue() < 12) return;
		lastPanPaintMs = now;
	}

	Refresh(); // Obviously it needs refreshed after a pan.
	// Force an immediate repaint rather than a deferred WM_PAINT. On Windows
	// WM_PAINT is the lowest-priority message, so during a drag it gets starved
	// by the flood of mouse-move events the canvas receives (the minimap stays
	// smooth only because it does not get those events). Painting synchronously
	// keeps interactive pan/zoom smooth.
	wxWindow::Update();
}


//Julian: Added to assist in zoom to mouse
void klsGLCanvas::setCenter(GLdouble newX, GLdouble newY)
{
	GLPoint2f topLeft;
	GLPoint2f bottomRight;
	GLPoint2f center;

	getViewport(topLeft, bottomRight);
	center = getCenter();

	setPan(newX - (center.x - topLeft.x), newY - (center.y - topLeft.y));
}

void klsGLCanvas::translatePan( GLdouble relX, GLdouble relY ) {
	GLdouble x, y;
	getPan(x, y);
	setPan( x + relX, y + relY );
}


void klsGLCanvas::OnScrollTimer(wxTimerEvent& event) {
	wxSize sz = GetClientSize();
	bool mouseOnBorder = false;
	bool isDuringDrag = isDragging(BUTTON_LEFT);

//TODO: Make this work so that you can auto-scroll even on the edge of a
// maximized window!
	wxPoint mPos = getMouseScreenCoords();
	if( (mPos.x == 0) || (mPos.x == sz.GetWidth()) ) {
		mouseOnBorder = true;
	}

	if( (mPos.y == 0) || (mPos.y == sz.GetHeight()) ) {
		mouseOnBorder = true;
	}

	//TODO: Find a way to disable auto-scroll when a new gate is being dragged around.
	if( isDuringDrag && (mouseOnBorder || mouseOutOfWindow) ) {
		GLdouble transX = 0.0;
		GLdouble transY = 0.0;
		if( mPos.x <= 0 ) {
			transX = -SCROLL_STEP * getZoom();
		} else if( mPos.x >= sz.GetWidth() ){
			transX = +SCROLL_STEP * getZoom();
		}
		
		if( mPos.y <= 0 ) {
			transY = +SCROLL_STEP * getZoom();
		} else if( mPos.y >= sz.GetHeight() ){
			transY = -SCROLL_STEP * getZoom();
		}

		// Call this once, to avoid the mouse callback being run twice:
		translatePan(transX, transY);
	}
}


void klsGLCanvas::setZoom( GLdouble newZoom ) {
	// Clamp the newZoom factor within the allowed zoom
	// sizes:
	newZoom = max(newZoom, MIN_ZOOM);
	newZoom = min(newZoom, MAX_ZOOM);

	GLPoint2f center = getCenter();
	GLPoint2f topLeft;
	GLPoint2f bottomRight;
	getViewport(topLeft, bottomRight);
	
	GLPoint2f oldDist = center - topLeft;
	GLPoint2f newDist = oldDist;

	oldDist.x *= newZoom / viewZoom;
	oldDist.y *= newZoom / viewZoom;

	viewZoom = newZoom;

	translatePan(newDist.x - oldDist.x, newDist.y - oldDist.y);
}


void klsGLCanvas::wxOnMouseEvent(wxMouseEvent& event) {
	isShiftDown = event.ShiftDown();
	isControlDown = event.ControlDown();
	
	// Always set the mouse coords to the current event:
	setMouseScreenCoords( event.GetPosition() );
	setMouseCoords();

	// Check all of the button events:
	if (event.LeftDown() ) {
		mouseOutOfWindow = false; // Assume that we clicked inside the window!
		beginDrag(BUTTON_LEFT);
		OnMouseDown(event); // Call the event handler.
	} else if( event.LeftUp() || event.LeftDClick()) {
		endDrag(BUTTON_LEFT);
		OnMouseUp( event );
	} else if( event.RightDown() || event.RightDClick() ) {
		beginDrag( BUTTON_RIGHT );
		OnMouseDown( event ); // Call the event handler.
	} else if( event.RightUp() ) {
		endDrag( BUTTON_RIGHT );
		OnMouseUp( event );
	} else if( event.MiddleDown() || event.MiddleDClick() ) {
		beginDrag( BUTTON_MIDDLE );
		OnMouseDown( event ); // Call the event handler.
	} else if( event.MiddleUp() ) {
		endDrag( BUTTON_MIDDLE );   // forces the final pan repaint
		OnMouseUp( event );
	} else {
		// It's not a button event, so check the others:
		if( event.Entering() ) {
			mouseOutOfWindow = false;
			scrollTimer->Stop();
			OnMouseEnter( event );
		} else if( event.Leaving() && !isDragging( BUTTON_MIDDLE ) ) { // Don't allow auto-scroll during pan-scrolling.
			// Flag the scroll event by telling it that the
			// mouse has left the window:
			mouseOutOfWindow = true;

			// Start the scroll timer:
			if( isAutoScrollOn() && isDragging( BUTTON_LEFT ) ) {
				scrollTimer->Start(SCROLL_TIMER_RATE);
			}

			// Call the event handler:
			OnMouseLeave( event );
		} else {
			// Handle the drag-pan event here if needed:
			if( isDragging( BUTTON_MIDDLE ) ) {
				GLPoint2f mouseDelta( getMouseCoords().x - getDragStartCoords( BUTTON_MIDDLE ).x,
										getMouseCoords().y - getDragStartCoords( BUTTON_MIDDLE ).y );

				// A pan only needs to move the camera + repaint. Flag it so setPan
				// skips the hover/collision OnMouseMove, and skip our own call too
				// -- that heavy per-move work is what makes the drag stutter.
				panning = true;
				translatePan( -mouseDelta.x, -mouseDelta.y );
				panning = false;
				// ...unless a left drag (gate move / rubber-band / new-gate) is also
				// in progress: it still needs OnMouseMove to track the cursor.
				if( isDragging( BUTTON_LEFT ) ) {
					GLPoint2f m = getMouseCoords();
					OnMouseMove(m.x, m.y, event.ShiftDown(), event.ControlDown());
				}
			} else {
				// It's nothing else, so it must be a mouse motion event:
				GLPoint2f m = getMouseCoords();
				OnMouseMove(m.x, m.y, event.ShiftDown(), event.ControlDown());
			}
		}
		
	}

// Refresh the canvas to show the mouse drag highlights if needed:
#ifdef CANVAS_DEBUG_TESTS_ON
	Refresh();
#endif

}


void klsGLCanvas::wxOnMouseWheel(wxMouseEvent& event) {
	const auto& settings = appConfig().appSettings;
#ifdef __APPLE__
	const bool trackpad = MacCurrentScrollIsPrecise();
#else
	const bool trackpad = false;   // can't tell; everything uses the mouse setting
#endif
#ifdef __WXOSX__
	const bool zoomModifier = event.CmdDown();   // the physical Cmd key
#else
	const bool zoomModifier = event.ControlDown();   // the physical Ctrl key
#endif
	const bool plainVertical = !zoomModifier && !event.ShiftDown() &&
	                           event.GetWheelAxis() == wxMOUSE_WHEEL_VERTICAL;
	const bool zooms = trackpad ? settings.trackpadScrollAction == 0
	                            : settings.mouseWheelAction == 0;
	// Which way is "in": physical up/away. macOS "natural scrolling" flips the
	// reported direction, so undo that, then apply the user's own flip.
	const bool reverse = trackpad ? settings.reverseTrackpadZoom : settings.reverseWheelZoom;
	const double inSign = (event.IsWheelInverted() ? -1.0 : 1.0) * (reverse ? -1.0 : 1.0);

	// A trackpad sends a stream of small deltas; zoom by them continuously
	// rather than in whole wheel-notch steps, which would lurch.
	if (trackpad && zooms && plainVertical) {
		const double notches = (double)event.GetWheelRotation() / event.GetWheelDelta();
		zoomToMouseByFactor(pow(ZOOM_STEP, 0.35 * notches * inSign));
		updateMiniMap();
		event.Skip();
		return;
	}

	// Accumulate mouse wheel events until they amount
	// to one "line", and then take them line at a time:
	wheelRotation += event.GetWheelRotation();
	int rotationLines = (int)wheelRotation / event.GetWheelDelta();
	wheelRotation -= rotationLines * event.GetWheelDelta();

	if (rotationLines != 0) {
		GLdouble panAmount = PAN_STEP * getZoom() * rotationLines;

		// Per-device choice (see Preferences > Canvas): by default a mouse
		// wheel zooms (a mouse has no pinch) and a trackpad moves around like
		// every other Mac app. Cmd/Ctrl+scroll always zooms.
		if (zoomModifier) {
			OnMouseWheel(rotationLines / abs(rotationLines));
		} else if (zooms && plainVertical) {
			OnMouseWheel((rotationLines > 0 ? 1 : -1) * (inSign > 0 ? 1 : -1));
		} else if (event.GetWheelAxis() == wxMOUSE_WHEEL_HORIZONTAL || event.ShiftDown()) {
			// Horizontal swipe, or Shift+scroll for a mouse with no horizontal
			// wheel -- both pan horizontally.
			translatePan(panAmount, 0.0);
		} else {
			translatePan(0.0, panAmount);
		}
	}

	// Update the drag-pan event here if needed:
	if( isDragging( BUTTON_MIDDLE ) ) {
		GLPoint2f mouseDelta( getMouseCoords().x - getDragStartCoords( BUTTON_MIDDLE ).x,
								getMouseCoords().y - getDragStartCoords( BUTTON_MIDDLE ).y );

		translatePan( -mouseDelta.x, -mouseDelta.y );
	}

	updateMiniMap();
	event.Skip(); // Send the event on to wxOnMouseEvent, so that the gl coordinates get updated.
}

// Trackpad pinch (macOS only -- wx never generates this from a plain wheel or
// a two-finger pan, so it can't fight with wxOnMouseWheel's pan default).
// GetMagnification() is the fractional size change since the last event (0.02
// for a 2% pinch-out, negative for pinch-in), already scaled for a natural
// per-frame feel.
//
// getZoom()/setZoom() are in this codebase's own "viewZoom" units, where
// SMALLER means more zoomed in (see zoomToMouse: ZOOM_STEP is 0.75, and
// zooming in multiplies BY it) -- the opposite of the everyday sense of
// "zoom factor." So a pinch-out (positive magnification, fingers spreading,
// which should zoom IN) has to shrink getZoom(), i.e. divide by (1+magnification)
// rather than multiply by it. Multiplying, as an earlier version of this did,
// zoomed out on pinch-out -- backwards from every other Mac app.
void klsGLCanvas::wxOnMagnify(wxMouseEvent& event) {
	setMouseScreenCoords(event.GetPosition());
	setMouseCoords();
	double magFactor = 1.0 + event.GetMagnification();
	// Guard against a degenerate/inverted zoom from an extreme or malformed
	// event; a real pinch's per-event magnification is nowhere near this.
	if (magFactor < 0.1) magFactor = 0.1;
	if (magFactor > 10.0) magFactor = 10.0;
	zoomToMouseByFactor(1.0 / magFactor);
	updateMiniMap();
}


// Start a drag event right away, using the current mouse coordinates.
// This captures the mouse using CaptureMouse() and sets the "Drag Start Coords"
// to the current mouse coordinates.
// (This is usually called by this class right before OnMouseDown(), but
// can be called by the subclasses. For example, right after an OnMouseEnter()
// in which a gate is being dragged. Or, maybe also for a Paste from clipboard event.)
void klsGLCanvas::beginDrag( mouseButton whichButton ) {
	// If we are already in a drag event for this button, ignore any additional
	// ones that come along. This allows a beginDrag() called from
	// an event handler to not CaptureMouse() too many times.
	if( isDragging( whichButton ) ) return;
	
	// Set the keyboard focus to this window. This allows the user to re-set
	// the keyboard focus to this window by clicking on it.
	SetFocus();

	// Bind all mouse events to this window:
	if (!HasCapture()) {
		CaptureMouse();
	}
	
	// Set the dragging start coordinates:
	setDragStartCoords( getMouseCoords(), whichButton );
	
	// Set the flag to tell us that the button is dragging:
	setIsDragging( true, whichButton );
}


// Force the drag event to end, by unclaiming the mouse (If all other buttons haven't
// claimed a drag event too) and setting the "Drag End Coords".
void klsGLCanvas::endDrag( mouseButton whichButton ) {
	// Set the dragging start coordinates:
	setDragEndCoords( getMouseCoords(), whichButton );

	// Set the flag to tell us that the button is finished dragging:
	setIsDragging( false, whichButton );

	// Release the mouse capture, but only once no other button is still mid-drag:
	// the capture is shared, so a concurrent middle-pan + left gate-drag would
	// otherwise lose tracking when the first button is released.
	// (wxWidgets asserts on over-release; this is also called for double-click/ESC
	// where ReleaseMouse may run without a matching CaptureMouse -- HasCapture
	// guards that.)
	if (HasCapture() && !isDragging(BUTTON_LEFT) && !isDragging(BUTTON_MIDDLE)
	                 && !isDragging(BUTTON_RIGHT)) {
		ReleaseMouse();
	}

	// A middle-drag pan throttles its repaints, so its last frame may have been
	// skipped; paint the final position now, however the pan ended (MiddleUp, ESC,
	// or lost capture -- see wxOnCaptureLost).
	if (whichButton == BUTTON_MIDDLE) {
		Refresh();
		wxWindow::Update();
	}
}


void klsGLCanvas::wxKeyDown(wxKeyEvent& event) {
	wxGetApp().SetCurrentCanvas(this);
	// Give the subclassed handler first dibs on the event:
	OnKeyDown( event );

	// If the subclassed handler took the event, then don't handle it:
	if( event.GetSkipped() ) return;

	// GUICanvas::OnKeyDown already acted on these (pan/nudge, zoom). They used
	// to be acted on again here, so every arrow press panned twice and every
	// +/- zoomed twice. Just claim them, so macOS doesn't beep at them.
	bool handled = true;
	switch (event.GetKeyCode()) {
	case WXK_LEFT:
	case WXK_NUMPAD_LEFT:
	case WXK_RIGHT:
	case WXK_NUMPAD_RIGHT:
	case WXK_UP:
	case WXK_NUMPAD_UP:
	case WXK_DOWN:
	case WXK_NUMPAD_DOWN:
	case 43: // + key (Shift+=)
	case 61: // = key (for zoom in without shift on Mac)
	case WXK_NUMPAD_ADD:
	case 45: // - key on top row (Works for both '-' and '_')
	case WXK_NUMPAD_SUBTRACT:
		break;
	default:
		handled = false;
		break;
	}

	if (!handled) event.Skip();
	updateMiniMap();
}

void klsGLCanvas::wxKeyUp(wxKeyEvent& event) {
	OnKeyUp( event );
}

//Julian: Moved implementation from header
void klsGLCanvas::OnMouseWheel(long numOfLines) {
	zoomToMouse(numOfLines);
}

GLPoint2f klsGLCanvas::getSnappedPoint(GLPoint2f c) {
	GLfloat x = horizSpacing * floor(c.x / horizSpacing + 0.5);
	GLfloat y = vertSpacing * floor(c.y / vertSpacing + 0.5);
	return GLPoint2f(x, y);
}

void klsGLCanvas::setHorizGrid(GLfloat hSpacing) {
	horizOn = true;
	if (hSpacing != 0.0) horizSpacing = hSpacing;
}

void klsGLCanvas::setHorizGridColor(GLfloat a, GLfloat b, GLfloat c, GLfloat d) {
	hColor[0] = a;
	hColor[1] = b;
	hColor[2] = c;
	hColor[3] = d;
}

void klsGLCanvas::disableHorizGrid() {
	horizOn = false;
}

void klsGLCanvas::setVertGrid(GLfloat vSpacing) {
	vertOn = true;
	if (vSpacing != 0.0) vertSpacing = vSpacing;
}

void klsGLCanvas::setVertGridColor(GLfloat a, GLfloat b, GLfloat c, GLfloat d) {
	vColor[0] = a;
	vColor[1] = b;
	vColor[2] = c;
	vColor[3] = d;
}

void klsGLCanvas::disableVertGrid() {
	vertOn = false;
}

void klsGLCanvas::setMouseCoords() {
	setMouseCoords(mapToCanvas(getMouseScreenCoords()));
}

//Julian: Added to allow for zoom to mouse
void klsGLCanvas::zoomToMouse(long numLines)
{
	zoomToMouseByFactor(numLines > 0 ? pow(ZOOM_STEP, numLines) : 1.0 / pow(ZOOM_STEP, -numLines));
}

// Shared by the stepped wheel zoom above and the continuous pinch gesture
// (wxOnMagnify): re-centers the camera on `factor * getZoom()` so whatever
// world point is under the cursor/gesture stays under it, instead of zooming
// around the viewport center.
void klsGLCanvas::zoomToMouseByFactor(double factor)
{
	GLPoint2f center = getCenter();
	GLPoint2f mouse = getMouseCoords();

	GLPoint2f centerToMouse = mouse - center;
	centerToMouse.x /= getZoom();
	centerToMouse.y /= getZoom();

	// setZoom and setCenter both call setPan, which normally repaints
	// synchronously -- defer so the zoom paints once at the final camera state
	// instead of flashing the intermediate (center-fixed) frame before the
	// mouse-fixed correction.
	deferPaint = true;
	setZoom(getZoom() * factor);

	centerToMouse.x *= getZoom();
	centerToMouse.y *= getZoom();

	setCenter(mouse.x - centerToMouse.x, mouse.y - centerToMouse.y);
	deferPaint = false;

	Refresh();
	wxWindow::Update();
}

void klsGLCanvas::animateZoomTo(GLdouble targetZoom) {
	targetZoom = max(targetZoom, (GLdouble)MIN_ZOOM);
	targetZoom = min(targetZoom, (GLdouble)MAX_ZOOM);
	// Restart from wherever the last animation (or a plain setZoom) left off,
	// not from the in-flight animation's own target -- so mashing the zoom
	// button repeatedly accelerates smoothly instead of queuing up stale steps.
	zoomAnimStartZoom = viewZoom;
	zoomAnimTargetZoom = targetZoom;
	zoomAnimStartTime = std::chrono::steady_clock::now();
	if (!zoomAnimTimer->IsRunning()) zoomAnimTimer->Start(ZOOM_ANIM_RATE_MS);
}

void klsGLCanvas::OnZoomAnimTimer(wxTimerEvent& WXUNUSED(event)) {
	const long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - zoomAnimStartTime).count();
	const double t = (double)elapsedMs / ZOOM_ANIM_DURATION_MS;
	if (t >= 1.0) {
		setZoom(zoomAnimTargetZoom);
		zoomAnimTimer->Stop();
		return;
	}
	// Ease-out cubic: starts at full speed and settles gently, which reads as
	// a responsive snap rather than a sluggish drift (an ease-in, or linear,
	// both feel laggy for a one-shot button press).
	const double eased = 1.0 - pow(1.0 - t, 3.0);
	setZoom(zoomAnimStartZoom + (zoomAnimTargetZoom - zoomAnimStartZoom) * eased);
}

GLPoint2f klsGLCanvas::getCenter() {
	GLPoint2f topLeft, bottomRight;
	getViewport(topLeft, bottomRight);

	GLPoint2f center = bottomRight + topLeft;
	center.x /= 2;
	center.y /= 2;

	return center;
}
