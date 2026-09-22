/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   GUICanvas: Contains rendering and input functions for a page
*****************************************************************************/

#include "GUICanvas.h"
#include "PaletteDrag.h"
#include "RenderMode.h"
#include "Settings.h"
#include "GateLibrary.h"
#include "MainApp.h"
#include "paramDialog.h"
#include "QuickAddDialog.h"
#include "klsClipboard.h"
#include "guiWire.h"
#include "render/Scene.h"
#include "render/RenderStyle.h"
#ifdef WITH_SKIA
#include "render/SkiaProbe.h"
#endif


#include <wx/dnd.h>
#include <cstring>

// Included to use the min() and max() templates:
#include <algorithm>
#include <iostream>
using namespace std;

// Enable access to objects in the main application
DECLARE_APP(MainApp)

unsigned int renderTime = 0;
unsigned int renderNum = 0;

// TODO: this should probably get it's own file
class DnDText : public wxTextDropTarget {
public:
	DnDText(GUICanvas* canvas) { m_canvas = canvas; }

	bool OnDropText(wxCoord x, wxCoord y, const wxString& text) wxOVERRIDE {
		string gateName = text.ToStdString();

		// Make sure the gate exists
		if (gateLibrary().gateNameToLibrary.count(gateName) == 0) {
			return false;
		}

		wxPoint m(x, y);
		m_canvas->addGate(gateName, m_canvas->mapToCanvas(m));
		return true;
	};

private:
	GUICanvas* m_canvas;
};

// GUICanvas constructor - defaults grid size to 1 unit square
GUICanvas::GUICanvas(wxWindow *parent, GUICircuit* gCircuit, wxWindowID id,
    const wxPoint& pos, const wxSize& size, long style, const wxString& name)
    : klsGLCanvas(parent, name, id, pos, size, style|wxSUNKEN_BORDER ) {

	this->gCircuit = gCircuit;
	isWithinPaste = false;
	currentDragState = DRAG_NONE;
	
	hotspotHighlight = "";
	
	drawWireHover = false;
	
	setHorizGrid(0.5);
	setVertGrid(0.5);
	
	// Add mouse object to collision checker
	mouse = new klsCollisionObject( COLL_MOUSEBOX );
	snapMouse = new klsCollisionObject( COLL_MOUSEBOX );
	collisionChecker.addObject( mouse );
	
	// Add drag selection box to collision checker
	dragselectbox = new klsCollisionObject( COLL_SELBOX );
	collisionChecker.addObject( dragselectbox );

	overlayFadeTimer = new wxTimer(this);
	Bind(wxEVT_TIMER, &GUICanvas::OnOverlayFadeTimer, this, overlayFadeTimer->GetId());

	SetDropTarget(new DnDText(this));

#ifdef __WXOSX__
	// Suppress macOS bonk sound for keys handled in OnKeyDown
	Bind(wxEVT_CHAR, [](wxKeyEvent& evt) {
		int key = evt.GetKeyCode();
		if (key == 'a' || key == 'A' || key == 'r' || key == 'R' ||
			key == 'c' || key == 'C' ||
			key == WXK_SPACE || key == '+' || key == '=' || key == '-') {
			// Swallow — already handled in OnKeyDown
		} else {
			evt.Skip();
		}
	});
#endif
}

GUICanvas::~GUICanvas() {
	overlayFadeTimer->Stop();
	delete overlayFadeTimer;
	delete snapMouse;
	delete mouse;
	delete dragselectbox;
}

// Clears the circuit by selecting all gates and wires and then running a delete command
void GUICanvas::clearCircuit() {
	selectedGates.clear();
	selectedWires.clear();
	preMove.clear();
	preMoveWire.clear();

	collisionChecker.clear();
	gateList.clear();
	wireList.clear();

	// Add mouse object to collision checker
	collisionChecker.addObject( mouse );
	
	// Add drag selection box to collision checker
	collisionChecker.addObject( dragselectbox );
	
	hotspotHighlight = "";
	potentialConnectionHotspots.clear();
	drawWireHover = false;
	isWithinPaste = false;
	saveMove = false;
}

// Build the palette preview gate. The circuit is the only thing that knows how
// to assemble a gate from the library, so we ask it for one and then take it
// straight back out: this gate is scenery until the drop turns it into a real,
// undoable creation.
std::unique_ptr<guiGate> GUICanvas::takeNewDragGate(const string &gateName) {
	guiGate *built = gCircuit->createGate(gateName, -1);
	if (built == nullptr) return nullptr;
	return gCircuit->releaseGate(built->getID());
}

// Inserts an existing gate onto the canvas at a particular x,y position
void GUICanvas::insertGate(unsigned long id, guiGate* gt, float x, float y) {
	if (gt == NULL) return;
	gt->setGLcoords(x, y);
	gateList[id] = gt;
	
	// Add the gate to the collision checker:
	collisionChecker.addObject( gt );
}

// Inserts an existing wire onto the canvas
void GUICanvas::insertWire(guiWire* wire) {

	if (wire == nullptr) return;

	// Only the wire itself goes in, under its head id. The other bus-line ids
	// used to get a nullptr entry apiece, which meant every walk of this list
	// had to step over holes and every index into it could return nothing.
	// Nothing on a canvas allocates ids, so the placeholders bought nothing.
	wireList[wire->getID()] = wire;

	// Add the wire to the collision checker:
	collisionChecker.addObject( wire );
}

// If the gate exists on this page, then remove it from the page
void GUICanvas::removeGate(unsigned long gid) {
	unordered_map < unsigned long, guiGate* >::iterator thisGate = gateList.find(gid);
	if (thisGate != gateList.end()) {
		// Clear a hotspot we're holding if we need to
		if (hotspotGate == gid) hotspotHighlight = "";
		
		// Take the gate out of the collision checker:
		collisionChecker.removeObject( thisGate->second );
		collisionChecker.update();

		gateList.erase(thisGate);
	}
}

// If the wire exists on this page, then remove it from the page
void GUICanvas::removeWire(unsigned long wireId) {

	if (wireList.find(wireId) == wireList.end()) return;

	guiWire *wire = wireList.at(wireId);
	collisionChecker.removeObject(wire);
	collisionChecker.update();

	// Release ID's owned by the wire.
	for (int busLineId : wire->getIDs()) {
		auto thisWire = wireList.find(busLineId);
		if (thisWire != wireList.end()) {
			wireList.erase(thisWire);
		}
	}
}

// Tag a command with this canvas, then submit it, so undo/redo can return to
// the page the edit happened on.
void GUICanvas::submitCommand(klsCommand *cmd) {
	cmd->setCanvas(this);
	gCircuit->GetCommandProcessor()->Submit((wxCommand *)cmd);
}

// Render the page
// Render the whole page into the engine-neutral Scene (Workstream G). Fits the
// circuit's world bounding box into a deviceW x deviceH viewport (y-flipped for a
// top-left device origin), then emits every wire and gate.
void GUICanvas::renderToScene(cl::render::Scene& scene,
                              const cl::render::RenderStyle& style,
                              int deviceW, int deviceH) {
	using namespace cl::render;

	klsBBox world;
	for (auto it = gateList.begin(); it != gateList.end(); ++it)
		if (it->second) world.addBBox(it->second->getBBox());
	for (auto it = wireList.begin(); it != wireList.end(); ++it)
		if (it->second) world.addBBox(it->second->getBBox());

	float minX, minY, maxX, maxY;
	if (world.empty()) { minX = minY = -50; maxX = maxY = 50; }
	else { minX = world.getLeft(); maxX = world.getRight();
	       minY = world.getBottom(); maxY = world.getTop(); }

	float worldW = std::max(1e-3f, maxX - minX);
	float worldH = std::max(1e-3f, maxY - minY);
	float scale = 0.92f * std::min((float)deviceW / worldW,
	                               (float)deviceH / worldH);
	float offX = ((float)deviceW - worldW * scale) * 0.5f;
	float offY = ((float)deviceH - worldH * scale) * 0.5f;

	// world -> device: x' = (x - minX)*scale + offX ; y' = (maxY - y)*scale + offY
	Transform t;
	t.a = scale;  t.c = 0;      t.e = -minX * scale + offX;
	t.b = 0;      t.d = -scale; t.f =  maxY * scale + offY;

	// Visible world rect = the full device rectangle back-projected, so the grid
	// fills the image the way GL fills the visible viewport.
	const float gMinX = minX - offX / scale;
	const float gMaxX = minX + ((float)deviceW - offX) / scale;
	const float gMinY = maxY - ((float)deviceH - offY) / scale;
	const float gMaxY = maxY + offY / scale;
	drawSceneContents(scene, style, t, scale, gMinX, gMinY, gMaxX, gMaxY);
}

// The on-screen style with the user's Appearance choices applied.
static cl::render::RenderStyle liveStyle(bool dark) {
	cl::render::RenderStyle s = cl::render::RenderStyle::screen(dark);
	const auto& a = appConfig().appSettings;
	s.showGrid = a.gridlineVisible;
	s.accentIndex = a.accentColor;
	static const float wireScales[3] = {0.7f, 1.0f, 1.6f};
	s.wireScale = wireScales[(a.wireThickness >= 0 && a.wireThickness < 3) ? a.wireThickness : 1];
	return s;
}

wxImage GUICanvas::renderThumbnail(int w, int h, bool dark) {
	wxImage img(w, h);
#ifdef WITH_SKIA
	using namespace cl::render;
	RenderStyle style = liveStyle(dark);
	const unsigned int bg = dark ? 0xFF131519u : 0xFFFFFFFFu;

	wxSize sz = GetClientSize();
	GLdouble px, py; getPan(px, py);
	const double vz = getZoom();
	if (sz.GetWidth() < 50 || sz.GetHeight() < 50 || vz <= 0) {
		skiaRenderToRGB(w, h, [&](Scene& s) { renderToScene(s, style, w, h); }, img.GetData(), bg);
		return img;
	}
	// Same center and zoom as the live view, scaled so the whole visible area
	// fits the thumbnail (a little extra world shows if the aspects differ).
	const double viewW = sz.GetWidth() * vz, viewH = sz.GetHeight() * vz;
	const float scale = (float)std::min(w / viewW, h / viewH);
	const double cx = px + viewW * 0.5, cy = py - viewH * 0.5;
	const float minX = (float)(cx - w / scale * 0.5), maxX = (float)(cx + w / scale * 0.5);
	const float minY = (float)(cy - h / scale * 0.5), maxY = (float)(cy + h / scale * 0.5);
	Transform t;
	t.a = scale; t.c = 0; t.e = -minX * scale;
	t.b = 0; t.d = -scale; t.f = maxY * scale;
	skiaRenderToRGB(w, h, [&](Scene& s) {
		drawSceneContents(s, style, t, scale, minX, minY, maxX, maxY);
	}, img.GetData(), bg);
#endif
	return img;
}

// Draw the grid + wires + gates into `scene` under an already-computed viewport
// transform. Shared by the bbox-fit export path (renderToScene) and the live
// camera path (renderLiveToScene). `scale` is device px per world unit; the
// g{Min,Max}{X,Y} bounds are the visible world rectangle for the grid.
// The background grid, matching klsGLCanvas: the base world spacing snaps to an
// integer (>=1), then grows so on-screen lines stay at least
// MIN_GRID_SCREEN_SPACING px apart when zoomed out. Every MAJOR_GRID_EVERY-th
// line is drawn brighter -- a CAD-ruler convention (Illustrator, Figma, KiCad
// all do this) that makes it possible to judge distance and alignment at a
// glance instead of counting hairlines. Assumes the viewport is already set;
// it is camera-dependent so the live path draws it fresh every frame.
void GUICanvas::drawGridInto(cl::render::Scene& scene,
                             const cl::render::RenderStyle& style, float scale,
                             float gMinX, float gMinY, float gMaxX, float gMaxY) {
	using namespace cl::render;
	if (!style.showGrid) return;
	const float viewZoom = 1.0f / scale;   // GL viewZoom = world units / pixel
	const long spaceX = std::max(std::max((long)(horizSpacing + 0.5f), 1L),
	                             (long)(MIN_GRID_SCREEN_SPACING * viewZoom));
	const long spaceY = std::max(std::max((long)(vertSpacing + 0.5f), 1L),
	                             (long)(MIN_GRID_SCREEN_SPACING * viewZoom));
	const long MAJOR_GRID_EVERY = 5;
	const bool majorOn = appConfig().appSettings.majorGridVisible;
	auto isMajor = [MAJOR_GRID_EVERY, majorOn](long idx) {
		return majorOn && ((idx % MAJOR_GRID_EVERY) + MAJOR_GRID_EVERY) % MAJOR_GRID_EVERY == 0;
	};

	Stroke minor, major;
	minor.color = style.gridColor((float)GRID_INTENSITY);
	minor.width = 1.0f;
	major.color = style.gridColor((float)GRID_INTENSITY * 2.5f);
	major.width = 1.0f;

	std::vector<Point> minorLines, majorLines;
	const long xStartIdx = (long)std::floor(gMinX / spaceX);
	for (long idx = xStartIdx; (float)(idx * spaceX) <= gMaxX; idx++) {
		const float x = (float)(idx * spaceX);
		std::vector<Point>& target = isMajor(idx) ? majorLines : minorLines;
		target.push_back(Point(x, gMinY)); target.push_back(Point(x, gMaxY));
	}
	const long yStartIdx = (long)std::floor(gMinY / spaceY);
	for (long idx = yStartIdx; (float)(idx * spaceY) <= gMaxY; idx++) {
		const float y = (float)(idx * spaceY);
		std::vector<Point>& target = isMajor(idx) ? majorLines : minorLines;
		target.push_back(Point(gMinX, y)); target.push_back(Point(gMaxX, y));
	}
	if (appConfig().appSettings.gridStyle == 1) {
		// Dots at the intersections instead of lines; a dot on two darker
		// lines is drawn larger and stronger.
		const float r = 1.1f * viewZoom, rMajor = 1.7f * viewZoom;
		const Color dot = style.gridColor((float)GRID_INTENSITY * 3.0f);
		const Color dotMajor = style.gridColor((float)GRID_INTENSITY * 5.0f);
		for (long ix = xStartIdx; (float)(ix * spaceX) <= gMaxX; ix++)
			for (long iy = yStartIdx; (float)(iy * spaceY) <= gMaxY; iy++) {
				const bool m = isMajor(ix) && isMajor(iy);
				scene.fillCircle(Point((float)(ix * spaceX), (float)(iy * spaceY)), m ? rMajor : r, m ? dotMajor : dot);
			}
		return;
	}
	if (!minorLines.empty()) scene.lines(&minorLines[0], minorLines.size(), minor);
	if (!majorLines.empty()) scene.lines(&majorLines[0], majorLines.size(), major);
}

// The circuit itself: wires then gates. Assumes the viewport/matrix is already
// set by the caller (the live path records this into an SkPicture, so it must
// NOT set the viewport here).
void GUICanvas::drawCircuitInto(cl::render::Scene& scene,
                                const cl::render::RenderStyle& style) {
	for (auto it = wireList.begin(); it != wireList.end(); ++it)
		if (it->second) it->second->drawToScene(scene, style);
	for (auto it = gateList.begin(); it != gateList.end(); ++it)
		if (it->second) it->second->drawToScene(scene, style);
}

void GUICanvas::drawSceneContents(cl::render::Scene& scene,
                                  const cl::render::RenderStyle& style,
                                  const cl::render::Transform& t, float scale,
                                  float gMinX, float gMinY,
                                  float gMaxX, float gMaxY) {
	scene.setViewport(t);
	drawGridInto(scene, style, scale, gMinX, gMinY, gMaxX, gMaxY);
	drawCircuitInto(scene, style);
}

#ifdef WITH_SKIA
// A cheap signature of everything that affects the rendered circuit: gate
// positions + selection, and wire selection + signal state (which drives every
// state colour and per-type fill). The live path re-records its SkPicture only
// when this changes, so a pure pan (nothing here changes) replays the cache.
unsigned long long GUICanvas::renderContentKey() {
	unsigned long long sig = 1469598103934665603ULL;
	auto mix = [&sig](unsigned long long v) { sig = (sig ^ v) * 1099511628211ULL; };
	// Global settings that change the drawing but aren't per-object state: wire
	// connection dots are gated on wireConnVisible and sized by wireConnRadius.
	mix(appConfig().appSettings.wireConnVisible ? 1u : 0u);
	{ float r = (float)appConfig().appSettings.wireConnRadius; unsigned u;
	  std::memcpy(&u, &r, sizeof u); mix(u); }
	// Fold each gate's/wire's full appearance -- transform, params, selection and
	// signal state -- so any edit (move, rotate, reshape, toggle, param change)
	// invalidates the retained SkPicture. Anything omitted here replays stale.
	for (auto it = gateList.begin(); it != gateList.end(); ++it) {
		if (!it->second) continue;
		mix(it->first);
		mix(it->second->appearanceHash());
	}
	for (auto it = wireList.begin(); it != wireList.end(); ++it) {
		if (!it->second) continue;
		mix(it->first);
		mix(it->second->appearanceHash());
	}
	return sig;
}
#endif

// Render the page at the LIVE camera (pan/zoom), not the bbox fit -- this is the
// on-screen path (G3). The camera is the canvas's own pan/zoom:
//   world x in [panX, panX + w*viewZoom], y in [panY - h*viewZoom, panY], mapped
//   to physical pixels. So device px per world unit = contentScale / viewZoom,
//   and world y is flipped for the top-left device origin.
void GUICanvas::renderLiveToScene(cl::render::Scene& scene,
                                  const cl::render::RenderStyle& style) {
	using namespace cl::render;
	wxSize sz = GetClientSize();
	const double sf = GetContentScaleFactor();
	GLdouble px, py; getPan(px, py);
	double vz = getZoom();
	if (vz <= 0) vz = 1.0;
	const float scale = (float)(sf / vz);
	Transform t;
	t.a = scale;  t.c = 0; t.e = (float)(-px * scale);
	t.b = 0; t.d = -scale; t.f = (float)( py * scale);
	const float gMinX = (float)px;
	const float gMaxX = (float)(px + sz.GetWidth()  * vz);
	const float gMinY = (float)(py - sz.GetHeight() * vz);
	const float gMaxY = (float)py;
	drawSceneContents(scene, style, t, scale, gMinX, gMinY, gMaxX, gMaxY);
}

// G3: paint the live frame through Skia's Ganesh backend into the window FBO.
// The grid is drawn live (camera-dependent); the circuit is retained in an
// SkPicture and replayed under the camera, so a pan is a cheap replay.
bool GUICanvas::renderSkiaLive() {
#ifdef WITH_SKIA
	using namespace cl::render;
	wxSize sz = GetClientSize();
	const double sf = GetContentScaleFactor();
	const int w = (int)(sz.GetWidth() * sf), h = (int)(sz.GetHeight() * sf);
	if (w <= 0 || h <= 0) return false;
	GLdouble px, py; getPan(px, py);
	double vz = getZoom();
	if (vz <= 0) vz = 1.0;
	const float scale = (float)(sf / vz);
	Transform t;
	t.a = scale;  t.c = 0; t.e = (float)(-px * scale);
	t.b = 0; t.d = -scale; t.f = (float)( py * scale);
	const float gMinX = (float)px;
	const float gMaxX = (float)(px + sz.GetWidth()  * vz);
	const float gMinY = (float)(py - sz.GetHeight() * vz);
	const float gMaxY = (float)py;

	GUICanvas* self = this;
	// Includes View > Display Gridlines and the Appearance preferences.
	RenderStyle style = liveStyle(renderMode().darkMode);
	// Selection halo fade-in (see markSelectionChanged): 0 right after a
	// selection change, ramping to 1 over SELECTION_FADE_MS.
	{
		const long selMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - selectionChangedAt).count();
		style.selectionFade = std::min(1.0f, std::max(0.0f, (float)selMs / SELECTION_FADE_MS));
	}
	// Key the cached circuit picture on the circuit CONTENT only. The interactive
	// overlays (hover bulb, drag box, wire hover, ...) are drawn live on top each
	// frame via drawOverlay below, so they follow the mouse WITHOUT invalidating
	// the picture -- otherwise every mouse move re-recorded the whole scene, which
	// is what let fast mouse movement starve the sim (see perf notes).
	// The theme is folded in too: it rebakes wire/gate stroke colors into the
	// cached picture, so toggling dark mode must invalidate it like any other
	// appearance change. selectionFade is folded in ONLY while its own fade is
	// still running (quantized, so it's stable -- and so the cache hits again
	// -- once the fade settles at 1.0).
	unsigned long long sceneKey = renderContentKey() ^ (style.darkMode ? 0x9E3779B97F4A7C15ULL : 0ULL);
	// Accent (selection halos) and wire thickness are baked into the picture too.
	sceneKey ^= (unsigned long long)(style.accentIndex + 1) * 0xC2B2AE3D27D4EB4FULL;
	sceneKey ^= (unsigned long long)(style.wireScale * 16.0f) * 0x165667B19E3779F9ULL;
	if (style.selectionFade < 1.0f)
		sceneKey ^= ((unsigned long long)(style.selectionFade * 64.0f) * 0x2545F4914F6CDD1DULL);
	auto drawGrid = [self, style, t, scale, gMinX, gMinY, gMaxX, gMaxY](Scene& s) {
		s.setViewport(t);
		self->drawGridInto(s, style, scale, gMinX, gMinY, gMaxX, gMaxY);
	};
	auto drawScene = [self, style](Scene& s) {
		self->drawCircuitInto(s, style);
	};
	// Logical pixel -> device pixel, no camera. Y is flipped (like the world
	// transform `t` above) even though logical-pixel space has no real "up" --
	// SkiaScene::text() unconditionally un-flips Y around the glyph origin to
	// compensate for the world convention, so a NON-flipped viewport here
	// left text rendering upside down; matching the sign is what text() needs
	// to draw right-side up, not an actual coordinate-space requirement.
	const float logicalW = (float)sz.GetWidth(), logicalH = (float)sz.GetHeight();
	Transform screenT;
	screenT.a = (float)sf; screenT.b = 0; screenT.c = 0; screenT.d = -(float)sf;
	screenT.e = 0; screenT.f = logicalH * (float)sf;
	auto drawOverlay = [self, t, style, screenT, logicalW, logicalH](Scene& s) {
		s.setViewport(t);
		self->drawOverlaysInto(s);
		self->drawEmptyHintInto(s, style, screenT, logicalW, logicalH);
	};
	const cl::render::Color bg = style.background();
	const unsigned int clearARGB =
		0xFF000000u | ((unsigned int)(bg.r * 255.0f + 0.5f) << 16) |
		((unsigned int)(bg.g * 255.0f + 0.5f) << 8) | (unsigned int)(bg.b * 255.0f + 0.5f);
	return skiaRenderWindowScene(w, h, 0, sceneKey, t, drawGrid, drawScene, drawOverlay, clearARGB);
#else
	return false;
#endif
}

void GUICanvas::markSelectionChanged() {
	selectionChangedAt = std::chrono::steady_clock::now();
	if (!overlayFadeTimer->IsRunning()) overlayFadeTimer->Start(OVERLAY_FADE_TIMER_RATE_MS);
}

// Drives repaints for the drag-select and selection-halo fades; idle (and the
// timer stopped) once both have settled. Nothing else keeps the canvas
// repainting between mouse/sim events, so without this the fades would just
// freeze at whatever alpha the next unrelated repaint happened to catch them at.
void GUICanvas::OnOverlayFadeTimer(wxTimerEvent& WXUNUSED(event)) {
	const auto now = std::chrono::steady_clock::now();
	if (dragSelectFading) {
		const long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - dragSelectFadeStart).count();
		if (ms >= DRAGSELECT_FADE_MS) dragSelectFading = false;
	}
	const long selMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - selectionChangedAt).count();
	Refresh();
	if (!dragSelectFading && selMs >= SELECTION_FADE_MS) overlayFadeTimer->Stop();
}

#ifdef WITH_SKIA
void GUICanvas::drawOverlaysInto(cl::render::Scene& scene) {
	using cl::render::Point;
	using cl::render::Color;
	using cl::render::Stroke;
	const float r = HOTSPOT_SCREEN_RADIUS * (float)getZoom();
	const Color accent = liveStyle(renderMode().darkMode).accent();

	auto box = [&scene](float x, float y, float rad, const Color& c) {
		Point pts[4] = { Point(x - rad, y + rad), Point(x + rad, y + rad),
		                 Point(x + rad, y - rad), Point(x - rad, y - rad) };
		scene.polyline(pts, 4, Stroke(c, 1.0f), true);
	};

	// Hovered gate pin -- the red bulb you drag a wire out from.
	if (guiGate *hovered = hotspotHighlight.size() > 0 ? getGate(hotspotGate) : nullptr) {
		float x, y;
		hovered->getHotspotCoords(hotspotHighlight, x, y);
		box(x, y, r, Color(1.0f, 0.0f, 0.0f));
	}

	// Wire hover -- a red X at the mouse. Only while the hovered wire still
	// exists, so deleting it clears the X on the delete's own repaint instead of
	// leaving it stuck until the next mouse move.
	if (drawWireHover && getWire(wireHoverID) != nullptr) {
		GLPoint2f m = getMouseCoords();
		Point xs[4] = { Point(m.x - r, m.y + r), Point(m.x + r, m.y - r),
		                Point(m.x + r, m.y + r), Point(m.x - r, m.y - r) };
		scene.lines(xs, 4, Stroke(Color(1.0f, 0.0f, 0.0f), 1.0f));
	}

	if (currentDragState == DRAG_SELECT) {
		GLPoint2f s = getDragStartCoords(), e = getMouseCoords();
		Point pts[4] = { Point(s.x, s.y), Point(s.x, e.y), Point(e.x, e.y), Point(e.x, s.y) };
		scene.polyline(pts, 4, Stroke(accent, 1.0f), true);
		scene.fillRect(Point(s.x, s.y), Point(e.x, e.y), Color(accent.r, accent.g, accent.b, 0.25f));
	} else if (currentDragState == DRAG_CONNECT) {
		// Anchor the preview line at the source pin, not the click point.
		GLPoint2f s = getDragStartCoords();
		if (currentConnectionSource.isGate) {
			if (guiGate *src = getGate(currentConnectionSource.objectID))
				src->getHotspotCoords(currentConnectionSource.connection, s.x, s.y);
		}
		GLPoint2f e = getMouseCoords();
		Point ln[2] = { Point(s.x, s.y), Point(e.x, e.y) };
		scene.lines(ln, 2, Stroke(Color(0.0f, 0.78f, 0.0f, 1.0f), 1.0f));
	} else if (currentDragState == DRAG_NEWGATE && newDragGate != nullptr) {
		newDragGate->drawToScene(scene, liveStyle(renderMode().darkMode));
	}

	// The just-released drag-select box, fading out (see OnMouseUp) -- drawn
	// independently of currentDragState, which is already back to DRAG_NONE by
	// the time this runs.
	if (dragSelectFading) {
		const long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - dragSelectFadeStart).count();
		const float fade = std::max(0.0f, 1.0f - (float)ms / DRAGSELECT_FADE_MS);
		const Point lo(dragSelectFadeBox.getLeft(), dragSelectFadeBox.getBottom());
		const Point hi(dragSelectFadeBox.getRight(), dragSelectFadeBox.getTop());
		Point fadePts[4] = { lo, Point(lo.x, hi.y), hi, Point(hi.x, lo.y) };
		scene.polyline(fadePts, 4, Stroke(Color(accent.r, accent.g, accent.b, fade), 1.0f), true);
		scene.fillRect(lo, hi, Color(accent.r, accent.g, accent.b, 0.25f * fade));
	}

	// Potential connection hotspots -- where a dragged wire could snap.
	for (size_t i = 0; i < potentialConnectionHotspots.size(); i++)
		box(potentialConnectionHotspots[i].x, potentialConnectionHotspots[i].y, r, accent);

	// Collision overlaps -- translucent boxes where two gates overlap.
	for (std::map<klsCollisionObjectType, CollisionGroup>::iterator ov = collisionChecker.overlaps.begin();
	     ov != collisionChecker.overlaps.end(); ++ov) {
		if (ov->first != COLL_GATE) continue;
		for (CollisionGroup::iterator obj = ov->second.begin(); obj != ov->second.end(); ++obj) {
			if ((*obj)->getType() != COLL_GATE) continue;
			CollisionGroup hits = (*obj)->getOverlaps();
			for (CollisionGroup::iterator h = hits.begin(); h != hits.end(); ++h) {
				if ((*h)->getType() != COLL_GATE) continue;
				klsBBox hb = (*obj)->getBBox().intersect((*h)->getBBox());
				if (!hb.empty())
					scene.fillRect(Point(hb.getLeft(), hb.getBottom()),
					               Point(hb.getRight(), hb.getTop()),
					               Color(0.4f, 0.1f, 0.0f, 0.3f));
			}
		}
	}
}

void GUICanvas::drawEmptyHintInto(cl::render::Scene& scene, const cl::render::RenderStyle& style,
                                  const cl::render::Transform& screenT, float logicalW, float logicalH) {
	using namespace cl::render;
	if (!gateList.empty() || !wireList.empty()) return;
	scene.setViewport(screenT);
	const char* msg = "Drag a gate here to start";
	const float textPx = 16.0f;
	const float textW = measuredTextWidth(msg, textPx);
	const Color c = style.darkMode ? Color(1.0f, 1.0f, 1.0f, 0.22f) : Color(0.0f, 0.0f, 0.0f, 0.22f);
	// text()'s origin is the top of the capitals, in the Y-UP space screenT
	// establishes -- convert from the top-down logical position we actually
	// want (vertical center, nudged up half a cap-height so the glyphs
	// themselves sit centered rather than their top edge).
	const float topDownY = logicalH * 0.5f - textPx * 0.5f;
	scene.text(Point(logicalW * 0.5f - textW * 0.5f, logicalH - topDownY), msg, textPx, c);
}
#endif

void GUICanvas::mouseLeftDown(wxMouseEvent& event) {
	if (connectSticky && currentDragState == DRAG_CONNECT) {
		// Finishing click of a sticky click-to-connect. Whatever's hovered is
		// the target -- OnMouseMove runs unthrottled while DRAG_CONNECT is
		// active, so hotspotHighlight/drawWireHover are already current for
		// this cursor position. A click on empty space just cancels.
		tryFinishConnection();
		connectSticky = false;
		currentDragState = DRAG_NONE;
		SetCursor(wxCursor(wxCURSOR_ARROW));
		Refresh();
		return;
	}
	GLPoint2f m = getMouseCoords();
	bool handled = false;
	dragPressTime = std::chrono::steady_clock::now(); // for the click-vs-drag time dead zone
	// If placing a new gate (snap-to-cursor), let OnMouseUp finalize it
	if (currentDragState == DRAG_NEWGATE) return;
	// If I am in a paste operation then mouse-up is all I am concerned with
	if (isWithinPaste) return;
	
	// Update the mouse collision object
	klsBBox mBox;
	float delta = MOUSE_HOVER_DELTA * getZoom();
	mBox.addPoint( m );
	mBox.extendTop( delta );
	mBox.extendBottom( delta );
	mBox.extendLeft( delta );
	mBox.extendRight( delta );
	mouse->setBBox( mBox );
	
	// Do a collision detection on all first-level objects.
	// The map collisionChecker.overlaps now contains
	// all of the objects involved in any collisions.
	collisionChecker.update();

	// Loop through all objects hit by the mouse
	//	Favor wires over gates
	CollisionGroup hitThings = mouse->getOverlaps();
	CollisionGroup::iterator hit = hitThings.begin();
	while( hit != hitThings.end() && !handled ) {
		//*************************************
		//Edit by Joshua Lansford 3/16/07
		//It has been requested by students that a ctrl
		//click will select multiple gates just like
		//a shift click does.
		//thus I will replace "event.ShiftDown()"
		//everywere it appears in this file with
		//"(event.ShiftDown()||event.ControlDown())"
		//************************************
		
		if ((*hit)->getType() == COLL_WIRE) {
			guiWire* hitWire = ((guiWire*)(*hit));
			bool wasSelected = hitWire->isSelected();
			hitWire->unselect();
			if ( hitWire->hover( m.x, m.y, WIRE_HOVER_SCREEN_DELTA * getZoom() )) {
				hitWire->select();
				if ((event.ShiftDown()||event.ControlDown()) && wasSelected) hitWire->unselect();
				if (!((event.ShiftDown()||event.ControlDown()))) {
					unselectAllWires();
					unselectAllGates();
					hitWire->select();
				}
				if (event.ControlDown() && !(this->isLocked())) {
					currentConnectionSource.isGate = false;
					currentConnectionSource.objectID = hitWire->getID();
					currentDragState = DRAG_CONNECT;
				}
				else if (!((event.ShiftDown()||event.ControlDown()))) {
					wireHoverID = hitWire->getID();
					if (hitWire->startSegDrag(snapMouse) && !(this->isLocked())) currentDragState = DRAG_WIRESEG;
					// Leave the wire selected after a plain click (not just a
					// drag) so a single click + Delete removes it -- previously
					// this unselected unconditionally, so only a shift-click or
					// a full drag-select box could leave a wire deletable.
				}
				handled = true;	
			} else if (wasSelected && hitThings.size() > 1) hitWire->select(); // probably dragging a selection
		}
		hit++;
	}

	// do we have a highlighted hotspot (which means we're on it now)
	if (hotspotHighlight.size() > 0 && currentDragState == DRAG_NONE && !(this->isLocked())) {
		// Start dragging a new wire:
		//gateList[hotspotGate]->select();
		unselectAllGates();
		unselectAllWires();
		handled = true; // Don't worry about checking other events in this proc
		currentDragState = DRAG_CONNECT;
		currentConnectionSource.isGate = true;
		currentConnectionSource.objectID = hotspotGate;
		currentConnectionSource.connection = hotspotHighlight;
	}

	// Now check gate collisions
	hit = hitThings.begin();
	while( hit != hitThings.end() && !handled ) {
		if ((*hit)->getType() == COLL_GATE) {
			guiGate* hitGate = ((guiGate*)(*hit));
			// The gate is in hitThings via its collision box, which is grown to
			// enclose its hotspot pins -- so clicking the empty space beside a pin
			// lands in that box. Only treat it as a selection if the click is on the
			// gate BODY, so pins stay for wire-connecting, not selecting.
			if (!hitGate->getSelectionBBox().overlaps(mouse->getBBox())) { hit++; continue; }
			bool wasSelected = hitGate->isSelected();
			if ((event.ShiftDown()||event.ControlDown()) && wasSelected) hitGate->unselect(); // Remove gate from selection
			else if ((event.ShiftDown()||event.ControlDown()) && !wasSelected) hitGate->select(); // Add gate to selection
			else if (!((event.ShiftDown()||event.ControlDown())) && !wasSelected) { // Begin new selection group
				unselectAllGates();
				unselectAllWires();
				hitGate->select();
			}
			if (!((event.ShiftDown()||event.ControlDown())) && !(this->isLocked())) currentDragState = DRAG_SELECTION; // Start dragging
			handled = true;
		}
		hit++;
	}

	// If I am not in a selection group and I haven't handled a selection then unselect everything
	if (!handled && !((event.ShiftDown()||event.ControlDown()))) {
		unselectAllGates();
		unselectAllWires();
	}
	// A gate or wire was just clicked/selected above -- fade its halo in
	// rather than snap it to full opacity (see RenderStyle::selectionFade).
	if (handled) markSelectionChanged();

	if (!handled && event.ControlDown()) {
		// Cmd(/Ctrl)+drag on empty background pans instead of rubber-band
		// selecting -- GeoGebra-style navigation. Not gated on isLocked(): like
		// the existing middle-button pan, this is navigation, not an edit.
		// Piggyback on that same middle-button drag-pan machinery
		// (klsGLCanvas::wxOnMouseEvent) rather than re-deriving the pan math;
		// DRAG_PAN just tells OnMouseMove/OnMouseUp to stand aside while it runs.
		currentDragState = DRAG_PAN;
		beginDrag(BUTTON_MIDDLE);
		SetCursor(wxCursor(wxCURSOR_HAND));
	} else if (!handled) { // Otherwise initialize drag select
		currentDragState = DRAG_SELECT;
	}
	
	// Show the updates
	Refresh();

	// clean up the selected gates vector and saved premove state
	selectedGates.clear();
	preMove.clear();
	saveMove = false;
	unordered_map < unsigned long, guiGate* >::iterator thisGate = gateList.begin();
	while (thisGate != gateList.end()) {
		if ((thisGate->second)->isSelected()) {
			// Push back the gate's id, xy pos, angle, and select flag
			preMove.push_back(GateState((thisGate->first), 0, 0, (thisGate->second)->isSelected()));
			(thisGate->second)->getGLcoords(preMove[preMove.size()-1].x, preMove[preMove.size()-1].y);
			selectedGates.push_back((thisGate->first));
		}
		thisGate++;
	}
	// clean up the selected wires vector
	selectedWires.clear();
	preMoveWire.clear();
	unordered_map < unsigned long, guiWire* >::iterator thisWire = wireList.begin();
	while (thisWire != wireList.end()) {
		if ((thisWire->second)->isSelected()) {
			// Push back the wire's id
			preMoveWire.push_back(WireState((thisWire->first), (thisWire->second)->getCenter(), (thisWire->second)->getSegmentMap()));
			selectedWires.push_back((thisWire->first));
		}
		thisWire++;
	}
}

void GUICanvas::mouseRightDown(wxMouseEvent& event) {
	GLPoint2f m = getMouseCoords();
	vector < unsigned long >::iterator sGate;

	if (isWithinPaste || (currentDragState != DRAG_NONE)) return; // Left mouse up is the next event we are looking for

	// Update the mouse collision object
	klsBBox mBox;
	float delta = MOUSE_HOVER_DELTA * getZoom();
	mBox.addPoint( m );
	mBox.extendTop( delta );
	mBox.extendBottom( delta );
	mBox.extendLeft( delta );
	mBox.extendRight( delta );
	mouse->setBBox( mBox );
	
	// Do a collision detection on all first-level objects.
	// The map collisionChecker.overlaps now contains
	// all of the objects involved in any collisions.
	collisionChecker.update();

	// Go ahead and remove all selection since this is the right mouse button
	unselectAllGates();
	unselectAllWires();

	// If locked then we have nothing else to do
	if ( this->isLocked() ) return;

	// do we have a highlighted hotspot (which means we're on it now)
	if (hotspotHighlight.size() > 0) {
		// If the hotspot is connected then we disconnect it and generate a command
		guiGate *hotGate = getGate(hotspotGate);
		if (hotGate != nullptr && hotGate->isConnected(hotspotHighlight)) {
			// disconnect this wire
			guiWire *hotWire = hotGate->getConnection(hotspotHighlight);
			if (hotWire->numConnections() > 2)
				submitCommand( new cmdDisconnectWire( gCircuit, hotWire->getID(), hotspotGate, hotspotHighlight ) );
			else submitCommand( new cmdDeleteWire( gCircuit, this, hotWire->getID() ) );
		}
		currentDragState = DRAG_NONE;
		Refresh();
		return;
	}

	if (currentDragState != DRAG_NONE) { Refresh(); return; }

	// Not on a hotspot -- favor a wire body under the cursor (same hover()
	// test mouseLeftDown uses; a plain bbox check is too loose for an
	// L-shaped wire, which would then catch clicks on its empty corner).
	CollisionGroup hitThings = mouse->getOverlaps();
	CollisionGroup::iterator hit = hitThings.begin();
	guiWire* menuWire = nullptr;
	while (hit != hitThings.end()) {
		if ((*hit)->getType() == COLL_WIRE) {
			guiWire* w = (guiWire*)(*hit);
			if (w->hover(m.x, m.y, WIRE_HOVER_SCREEN_DELTA * getZoom())) { menuWire = w; break; }
		}
		hit++;
	}

	if (menuWire != nullptr) {
		menuWire->select();
		Refresh();
		enum { ID_CTX_DELETE_WIRE = 7000 };
		wxMenu menu;
		menu.Append(ID_CTX_DELETE_WIRE, "Delete Wire");
		const int chosen = GetPopupMenuSelectionFromUser(menu, event.GetPosition());
		if (chosen == ID_CTX_DELETE_WIRE) submitCommand( new cmdDeleteWire( gCircuit, this, menuWire->getID() ) );
		else unselectAllWires();
		Refresh();
		return;
	}

	// Otherwise check for a gate under the cursor.
	guiGate* menuGate = nullptr;
	hit = hitThings.begin();
	while (hit != hitThings.end()) {
		if ((*hit)->getType() == COLL_GATE) { menuGate = (guiGate*)(*hit); break; }
		hit++;
	}
	if (menuGate == nullptr) { Refresh(); return; }

	if (appConfig().appSettings.rightClickRotate) {
		// BEGIN WORKAROUND
		//	Gates that have connections cannot be rotated without sacrificing wire sanity
		map < string, GLPoint2f > gateHotspots = menuGate->getHotspotList();
		map < string, GLPoint2f >::iterator ghsWalk = gateHotspots.begin();
		bool gateConnected = false;
		while ( ghsWalk !=  gateHotspots.end() ) {
			if ( menuGate->isConnected( ghsWalk->first ) ) {
				gateConnected = true;
				break;
			}
			ghsWalk++;
		}
		// END WORKAROUND
		if (!gateConnected) {
			map < string, string > newParams(*(menuGate->getAllGUIParams()));
			istringstream issAngle(newParams["angle"]);
			GLfloat angle;
			issAngle >> angle;
			angle += 90.0;
			if (angle >= 360.0) angle -= 360.0;
			ostringstream ossAngle;
			ossAngle << angle;
			newParams["angle"] = ossAngle.str();
			submitCommand( new cmdSetParams(gCircuit, menuGate->getID(), paramSet(&newParams, NULL) ) );
		}
	} else {
		menuGate->select();
		Refresh();
		enum { ID_CTX_ROTATE_GATE = 7001, ID_CTX_DELETE_GATE = 7002 };
		wxMenu menu;
		menu.Append(ID_CTX_ROTATE_GATE, "Rotate");
		menu.Append(ID_CTX_DELETE_GATE, "Delete");
		const int chosen = GetPopupMenuSelectionFromUser(menu, event.GetPosition());
		if (chosen == ID_CTX_ROTATE_GATE) {
			map < string, GLPoint2f > gateHotspots = menuGate->getHotspotList();
			map < string, GLPoint2f >::iterator ghsWalk = gateHotspots.begin();
			bool gateConnected = false;
			while ( ghsWalk !=  gateHotspots.end() ) {
				if ( menuGate->isConnected( ghsWalk->first ) ) { gateConnected = true; break; }
				ghsWalk++;
			}
			if (!gateConnected) {
				map < string, string > newParams(*(menuGate->getAllGUIParams()));
				istringstream issAngle(newParams["angle"]);
				GLfloat angle;
				issAngle >> angle;
				angle += 90.0;
				if (angle >= 360.0) angle -= 360.0;
				ostringstream ossAngle;
				ossAngle << angle;
				newParams["angle"] = ossAngle.str();
				submitCommand( new cmdSetParams(gCircuit, menuGate->getID(), paramSet(&newParams, NULL) ) );
			}
		} else if (chosen == ID_CTX_DELETE_GATE) {
			vector<unsigned long> gates{menuGate->getID()}, wires;
			submitCommand( new cmdDeleteSelection(gCircuit, this, gates, wires) );
		} else {
			unselectAllGates();
		}
	}
	Refresh();
}

void GUICanvas::OnMouseMove( GLdouble glX, GLdouble glY, bool ShiftDown, bool CtrlDown ) {
	// Cmd(/Ctrl)+drag pan: the actual panning already happened in
	// klsGLCanvas::wxOnMouseEvent's BUTTON_MIDDLE handling (which is why this
	// got called at all, mid-pan, instead of being skipped like a "pure"
	// middle-drag is) -- nothing here should also try to hover/select/move.
	if (currentDragState == DRAG_PAN) return;

	// Handle gate dragging from palette (especially needed for macOS where OnMouseEnter
	// may not fire correctly when mouse is captured)
	if (paletteDrag().newGateToDrag.size() > 0 && currentDragState == DRAG_NONE && !(this->isLocked())) {
		GLPoint2f m = getMouseCoords();
		newDragGate = takeNewDragGate(paletteDrag().newGateToDrag);
		if (newDragGate != nullptr) {
			newDragGate->setGLcoords(m.x, m.y);
			currentDragState = DRAG_NEWGATE;
			paletteDrag().newGateToDrag = "";
			beginDrag( BUTTON_LEFT );
			unselectAllGates();
			newDragGate->select();
			collisionChecker.addObject( newDragGate.get() );
		} else {
			paletteDrag().newGateToDrag = "";
		}
	}

	// Keep a flag for whether things have changed.  If nothing changes, then no render is necessary.
	bool shouldRender = false;

	GLPoint2f m = getMouseCoords();
	GLPoint2f dStart = getDragStartCoords(BUTTON_LEFT);
	GLPoint2f diff( m.x - dStart.x, m.y - dStart.y ); // What is the difference between start and now

	GLPoint2f mSnap = getSnappedPoint( m ); // Work with a snapped mouse coord
	GLPoint2f dStartSnap = getSnappedPoint( dStart );
	GLPoint2f diffSnap( mSnap.x - dStartSnap.x, mSnap.y - dStartSnap.y );

	// Click-vs-drag dead zone: treat this as a click (no move) while the pointer
	// is still within a small pixel radius of the press point, OR within a short
	// time of it -- so neither a jittery nor a quick-but-traveling selection click
	// nudges the gate into the next grid cell.
	{
		float dead = DRAG_START_SCREEN_DELTA * getZoom();
		float ax = diff.x < 0 ? -diff.x : diff.x;
		float ay = diff.y < 0 ? -diff.y : diff.y;
		long long heldMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - dragPressTime).count();
		if ((ax < dead && ay < dead) || heldMs < DRAG_START_TIME_MS) {
			diffSnap.x = 0.0f; diffSnap.y = 0.0f;
		}
	}

	// Update the mouse as a collision object:
	klsBBox mBox;
	float delta = MOUSE_HOVER_DELTA * getZoom();
	mBox.addPoint( m );
	mBox.extendTop( delta );
	mBox.extendBottom( delta );
	mBox.extendLeft( delta );
	mBox.extendRight( delta );
	mouse->setBBox( mBox );

	klsBBox smBox;
	smBox.addPoint( mSnap );
	snapMouse->setBBox( smBox );

	// Update the drag select box coordinates:
	klsBBox dBox;
	dBox.addPoint( dStart );
	dBox.addPoint( m );
	dragselectbox->setBBox( dBox );
	
	if ( this->isLocked() ) return;

	// Hover-work throttle. OnMouseMove fires once per raw motion event -- hundreds
	// per second during a fast sweep -- but the collision pass + hover highlight
	// only need to refresh ~60x/sec. When not in a drag, skip the heavy work if it
	// ran <15ms ago; this is what stops fast mouse movement from saturating the
	// GUI thread and starving the sim's timers (the clock/oscope would visibly
	// lag). Clicks stay accurate: mouseLeftDown runs its own collision update.
	// Drags are never throttled -- they must track the cursor exactly.
	if ( currentDragState == DRAG_NONE ) {
		auto now = std::chrono::steady_clock::now();
		if ( std::chrono::duration_cast<std::chrono::milliseconds>(now - lastHoverTime).count() < 15 )
			return;
		lastHoverTime = now;
	}

	// Do a collision detection on all first-level objects.
	// The map collisionChecker.overlaps now contains
	// all of the objects involved in any collisions.
	collisionChecker.update();

	// Update a newly-dragged gate's position
	if (currentDragState == DRAG_NEWGATE) {
		shouldRender = true;
		newDragGate->setGLcoords(mSnap.x, mSnap.y);
	}
	
	// If the hotspot hover is on, make it clear
	if (hotspotHighlight.size() > 0) shouldRender = true;
	hotspotHighlight = "";

	// If necessary, save the move being done.
	if (preMove.size() > 0 && currentDragState != DRAG_CONNECT && (diffSnap.x != 0 || diffSnap.y != 0)) saveMove = true;

	if (currentDragState == DRAG_SELECTION) {
		// Move all gates that are selected in the preMove vector:
		for (unsigned int i = 0; i < preMoveWire.size(); i++) {
			if (guiWire *w = getWire(preMoveWire[i].id)) w->move(preMoveWire[i].point, diffSnap);
		}
		for (unsigned int i = 0; i < preMove.size(); i++) {
			if (guiGate *g = getGate(preMove[i].id)) g->setGLcoords(preMove[i].x + diffSnap.x, preMove[i].y + diffSnap.y);
		}
	} else if (currentDragState == DRAG_WIRESEG) {
		if (guiWire *w = getWire(wireHoverID)) w->updateSegDrag(snapMouse);
	}

	// Generate a new list of potential connections
	potentialConnectionHotspots.clear();
	// Reset wire hover flag
	if (drawWireHover) shouldRender = true;
	drawWireHover = false;

	CollisionGroup hitThings = mouse->getOverlaps();
	CollisionGroup::iterator hit = hitThings.begin();
	while( hit != hitThings.end()) {
		if ((*hit)->getType() == COLL_GATE) {
			guiGate* hitGate = ((guiGate*)(*hit));
			
			// Update the hotspot hover variables:
			if( hotspotHighlight.size() == 0 ) {
				if (currentDragState != DRAG_NEWGATE || hitGate->getID() != newDragGate->getID()) hotspotHighlight = hitGate->checkHotspots( m.x, m.y, HOTSPOT_SCREEN_DELTA * getZoom() );
				if( hotspotHighlight.size() > 0 ) {
					if (currentDragState != DRAG_NEWGATE || hitGate->getID() != newDragGate->getID()) hotspotGate = hitGate->getID();
					shouldRender = true;
				}
			}
			
		}
		if ((*hit)->getType() == COLL_WIRE && !drawWireHover && currentDragState != DRAG_WIRESEG) { // Check for wire hover
			guiWire* hitWire = ((guiWire*)(*hit));
			drawWireHover = hitWire->hover( m.x, m.y, WIRE_HOVER_SCREEN_DELTA * getZoom() );
			wireHoverID = hitWire->getID();
			if (drawWireHover) shouldRender = true;
		}
		hit++;
	}

	if (currentDragState == DRAG_SELECT) { // Check for items within drag select box
		unselectAllGates();
		unselectAllWires();
		// Other items may have been selected before if we're using shift/control
		for (unsigned int i = 0; i < preMove.size(); i++) {
			if (guiGate *g = getGate(preMove[i].id)) g->select();
		}
		for (unsigned int i = 0; i < preMoveWire.size(); i++) {
			if (guiWire *w = getWire(preMoveWire[i].id)) w->select();
		}
		// Now check the collision box for dragselects
		CollisionGroup selThings = dragselectbox->getOverlaps();
		hit = selThings.begin();
		while( hit != selThings.end()) {
			if ((*hit)->getType() == COLL_GATE) {
				guiGate* hitGate = ((guiGate*)(*hit));
				if (dBox.overlaps((*hit)->getBBox())) hitGate->select();
			}
			if ((*hit)->getType() == COLL_WIRE) {
				guiWire* hitWire = ((guiWire*)(*hit));
				if (dBox.overlaps((*hit)->getBBox())) hitWire->select();
			}
			hit++;
		}
	}
	
	// Check potential hotspot connections (on gate/gate collisions)
	CollisionGroup ovrList = collisionChecker.overlaps[COLL_GATE];
	CollisionGroup::iterator obj = ovrList.begin();
	while( obj != ovrList.end() ) {
		CollisionGroup hitThings = (*obj)->getOverlaps();
		CollisionGroup::iterator hit = hitThings.begin();
		while( hit != hitThings.end() ) {
			// Only check gate collisions
			if ((*hit)->getType() != COLL_GATE) { hit++; continue; };
			// obj and hit are two overlapping gates
			//  get overlapping hotspots of obj in another group
			CollisionGroup hotspotOverlaps = (*obj)->checkSubsToSubs(*hit);
			CollisionGroup::iterator hotspotCollide = hotspotOverlaps.begin();
			while (hotspotCollide != hotspotOverlaps.end()) {
				// hotspotCollide is in obj; hsWalk is in hit
				CollisionGroup hshits = (*hotspotCollide)->getOverlaps();
				CollisionGroup::iterator hsWalk = hshits.begin();
				while (hsWalk != hshits.end()) {
					if ( !(((guiGate*)(*obj))->isConnected(((gateHotspot*)(*hotspotCollide))->name)) && !(((guiGate*)(*hit))->isConnected(((gateHotspot*)(*hsWalk))->name)))
						potentialConnectionHotspots.push_back( ((gateHotspot*)(*hotspotCollide))->getLocation() );
					hsWalk++;
				}
				hotspotCollide++;
			}
			hit++;
		}
		obj++;
	}

	if (currentDragState == DRAG_SELECTION || currentDragState == DRAG_SELECT || currentDragState == DRAG_CONNECT || currentDragState == DRAG_WIRESEG) shouldRender = true;
	
	// Only render if necessary
	if (shouldRender) {
		Refresh();
	}
	
	// clean up the selected gates vector
	selectedGates.clear();
	unordered_map < unsigned long, guiGate* >::iterator thisGate = gateList.begin();
	while (thisGate != gateList.end()) {
		if ((thisGate->second)->isSelected()) selectedGates.push_back((thisGate->first));
		thisGate++;
	}
	// clean up the selected wires vector
	selectedWires.clear();
	unordered_map < unsigned long, guiWire* >::iterator thisWire = wireList.begin();
	while (thisWire != wireList.end()) {
		if ((thisWire->second)->isSelected()) selectedWires.push_back((thisWire->first));
		thisWire++;
	}
}

void GUICanvas::OnMouseUp(wxMouseEvent& event) {
	if (currentDragState == DRAG_PAN) {
		// Nothing fires a real MiddleUp for this synthetic middle-drag (it was
		// never a real middle-button press), so end it explicitly here --
		// otherwise isDragging(BUTTON_MIDDLE) would stay stuck true and the
		// canvas would keep "panning" on every subsequent mouse move.
		endDrag(BUTTON_MIDDLE);
		currentDragState = DRAG_NONE;
		SetCursor(wxCursor(wxCURSOR_ARROW));
		return;
	}
	GLPoint2f m = getMouseCoords();
	SetCursor(wxCursor(wxCURSOR_ARROW));
	unordered_map < unsigned long, guiGate* >::iterator thisGate;
	cmdMoveSelection* movecommand = NULL;
	cmdCreateGate* creategatecommand = NULL;

	// Update the drag select box coordinates for wire source detection:
	klsBBox dBox;
	float delta = HOTSPOT_SCREEN_DELTA * getZoom();
	dBox.addPoint( getDragStartCoords( BUTTON_LEFT ) );
	dBox.extendTop( delta );
	dBox.extendBottom( delta );
	dBox.extendLeft( delta );
	dBox.extendRight( delta );
	dragselectbox->setBBox( dBox );

	// A palette gate that 'C' created mid-drag: record its creation at the
	// final drop position instead of creation + a separate move, so a single
	// undo deletes it.
	if (pendingCreateGate != nullptr && currentDragState == DRAG_SELECTION) {
		if (guiGate *g = preMove.size() > 0 ? getGate(preMove[0].id) : nullptr) {
			float gX, gY;
			g->getGLcoords(gX, gY);
			g->updateConnectionMerges();
			pendingCreateGate->setPosition(gX, gY);
		}
		storeCommand(pendingCreateGate);
	}
	// If moving a selection then save the move as a command
	else if (saveMove && currentDragState == DRAG_SELECTION) {
		float gX, gY;
		guiGate *anchor = preMove.size() > 0 ? getGate(preMove[0].id) : nullptr;
		if (anchor != nullptr) {
			anchor->getGLcoords(gX, gY);
			movecommand = new cmdMoveSelection( gCircuit, preMove, preMoveWire, preMove[0].x, preMove[0].y, gX, gY );
			for (unsigned int i = 0; i < preMove.size(); i++) {
				if (guiGate *g = getGate(preMove[i].id)) g->updateConnectionMerges();
			}
			if (!isWithinPaste) submitCommand( movecommand );
			if (!isWithinPaste) movecommand->Undo();
		}
		if (preMove.size() > 1) preMove.clear();
	}

	// Check for single selection out of group
	if (guiGate *anchor = preMove.size() > 0 ? getGate(preMove[0].id) : nullptr) {
		float gX, gY;
		anchor->getGLcoords(gX, gY);
		if (gX == preMove[0].x && gY == preMove[0].y && !((event.ShiftDown()||event.ControlDown()))) { // no move
			CollisionGroup hitThings = mouse->getOverlaps();
			CollisionGroup::iterator hit = hitThings.begin();
			while( hit != hitThings.end() ) {
				if ((*hit)->getType() == COLL_GATE) {
					guiGate* hitGate = ((guiGate*)(*hit));
					unselectAllGates();
					unselectAllWires();
					preMove.clear();
					preMove.push_back(GateState(hitGate->getID(), 0, 0, true));
					hitGate->select();
					saveMove = false;
					break;
				}
				hit++;
			}		
		}
	}

	if (currentDragState == DRAG_WIRESEG) {
		if (guiWire *w = getWire(wireHoverID)) {
			w->endSegDrag();
			w->select();
			submitCommand( new cmdWireSegDrag( gCircuit, this, wireHoverID ) );
		}
	}

	// If dragging a new gate then 
	if (currentDragState == DRAG_NEWGATE) {
		int newGID = gCircuit->getNextAvailableGateID();
		float nx, ny;
		newDragGate->getGLcoords(nx, ny);
		creategatecommand = new cmdCreateGate( this, gCircuit, newGID, newDragGate->getLibraryGateName(), nx, ny );
		gCircuit->GetCommandProcessor()->Submit( (wxCommand*)creategatecommand );
		collisionChecker.removeObject( newDragGate.get() );
		// Only now do a collision detection on all first-level objects since the new gate is in.
		// The map collisionChecker.overlaps now contains
		// all of the objects involved in any collisions.
		collisionChecker.update();
		if (guiGate *created = gCircuit->getGate(newGID)) {
			cmdSetParams setgateparams( gCircuit, newGID, paramSet(created->getAllGUIParams(), created->getAllLogicParams()));
			setgateparams.Do();
			created->select();
			selectedGates.push_back(newGID);
		}
		newDragGate.reset();
	}
	else {
		// Do a collision detection on all first-level objects.
		// The map collisionChecker.overlaps now contains
		// all of the objects involved in any collisions.
		collisionChecker.update();
		
		if ((currentDragState == DRAG_NONE || currentDragState == DRAG_SELECTION) && preMove.size() == 1) {
			// Loop through all objects hit by the mouse
			//	Favor wires over gates
//			CollisionGroup hitThings = mouse->getOverlaps();
//			CollisionGroup::iterator hit = hitThings.begin();
//			while( hit != hitThings.end() && !handled ) {
//				if ((*hit)->getType() == COLL_GATE) {
//					guiGate* hitGate = ((guiGate*)(*hit));
					// Check that gate still exists (may have been deleted by undo)
					if (guiGate* hitGate = getGate(preMove[0].id)) {
						if (!((event.ShiftDown()||event.ControlDown())) && ((event.LeftUp() && currentDragState == DRAG_SELECTION) || event.LeftDClick())) {
							// Check for toggle switch
							float x, y;
							hitGate->getGLcoords(x,y);
							bool handled = false;
							if (!saveMove) {
								klsMessage::Message_SET_GATE_PARAM* clickHandleGate = hitGate->checkClick( m.x, m.y );
								if (clickHandleGate != NULL) {
									gCircuit->sendMessageToCore(klsMessage::Message(klsMessage::MT_SET_GATE_PARAM, clickHandleGate));
									handled = true;
								}
							}
							if (event.LeftDClick() && !handled) {
								hitGate->doParamsDialog( gCircuit, gCircuit->GetCommandProcessor() );
								currentDragState = DRAG_NONE;
								// setparams command will handle oscope update
								handled = true;
							}
						}
					}
//				}
//				hit++;
//			}
		}

		// If we are dragging something...
		if (currentDragState == DRAG_CONNECT && !connectSticky) {
			GLPoint2f connStart = getDragStartCoords(BUTTON_LEFT);
			bool barelyMoved = fabs(m.x - connStart.x) < DRAG_START_SCREEN_DELTA * getZoom() &&
			                    fabs(m.y - connStart.y) < DRAG_START_SCREEN_DELTA * getZoom();
			// hotspotHighlight is still the SOURCE pin right after a plain click
			// (OnMouseMove last ran while hovering it to click it, and nothing
			// moved since to overwrite it) -- exclude that pin from "there's a
			// target" or a stationary click on a pin could never enter sticky mode.
			bool hoveringSelf = currentConnectionSource.isGate &&
			                    hotspotGate == currentConnectionSource.objectID &&
			                    hotspotHighlight == currentConnectionSource.connection;
			bool hasTarget = (!hotspotHighlight.empty() && !hoveringSelf) || drawWireHover;
			if (event.LeftUp() && currentConnectionSource.isGate && !hasTarget && barelyMoved) {
				// Pressed a pin and released again without dragging anywhere --
				// rather than cancelling, start a sticky connect: the preview
				// line keeps following the mouse with the button up, and the
				// next click (mouseLeftDown, top) finishes or cancels it.
				connectSticky = true;
				SetCursor(wxCursor(wxCURSOR_CROSS));
				Refresh();
				return;
			}
			tryFinishConnection();
		}

		collisionChecker.update();
	}

	if ((currentDragState == DRAG_NEWGATE || currentDragState == DRAG_SELECTION) && (potentialConnectionHotspots.size() > 0)) {
		// Check potential hotspot connections (on gate/gate collisions)
		CollisionGroup ovrList = collisionChecker.overlaps[COLL_GATE];
		CollisionGroup::iterator obj = ovrList.begin();
		while( obj != ovrList.end() ) {
			CollisionGroup hitThings = (*obj)->getOverlaps();
			CollisionGroup::iterator hit = hitThings.begin();
			while( hit != hitThings.end() ) {
				// Only check gate collisions
				if ((*hit)->getType() != COLL_GATE) { hit++; continue; };
				// obj and hit are two overlapping gates
				//  get overlapping hotspots of obj in another group
				CollisionGroup hotspotOverlaps = (*obj)->checkSubsToSubs(*hit);
				CollisionGroup::iterator hotspotCollide = hotspotOverlaps.begin();
				while (hotspotCollide != hotspotOverlaps.end()) {
					// hotspotCollide is in obj; hsWalk is in hit
					CollisionGroup hshits = (*hotspotCollide)->getOverlaps();
					CollisionGroup::iterator hsWalk = hshits.begin();
					while (hsWalk != hshits.end()) {
						if (!(((guiGate*)(*obj))->isConnected(((gateHotspot*)(*hotspotCollide))->name)) && !(((guiGate*)(*hit))->isConnected(((gateHotspot*)(*hsWalk))->name)) &&
							(((guiGate*)(*obj))->isSelected() || ((guiGate*)(*hit))->isSelected())) {

							cmdCreateWire* createwire = (cmdCreateWire *)createGateConnectionCommand(
								((guiGate*)(*obj))->getID(), ((gateHotspot*)(*hotspotCollide))->name,
								((guiGate*)(*hit))->getID(), ((gateHotspot*)(*hsWalk))->name);

							if (createwire != nullptr) {
								createwire->Do();
								//collisionChecker.update();
								if (currentDragState == DRAG_SELECTION && pendingCreateGate != nullptr) {
									pendingCreateGate->getConnections()->push_back(std::unique_ptr<klsCommand>(createwire));
								}
								else if (currentDragState == DRAG_SELECTION) {
									if (movecommand == NULL) {
										movecommand = new cmdMoveSelection(gCircuit, preMove, preMoveWire, 0, 0, 0, 0);
										if (!isWithinPaste) submitCommand(movecommand);
									}
									movecommand->getConnections()->push_back(std::unique_ptr<klsCommand>(createwire));
								}
								else if (currentDragState == DRAG_NEWGATE) creategatecommand->getConnections()->push_back(std::unique_ptr<klsCommand>(createwire));
								else delete createwire;
							}
						}
						hsWalk++;
					}
					hotspotCollide++;
				}
				hit++;
			}
			obj++;
		}
	}

	// Forgiving connect on drop: pins that are close (not only touching)
	// connect too, by the same rule as 'C'. Only after a real move or a new
	// gate -- a plain click on a gate shouldn't wire it to its neighbors.
	if (!isLocked() && (currentDragState == DRAG_NEWGATE || (currentDragState == DRAG_SELECTION && saveMove))) {
		collisionChecker.update();
		for (auto &c : findNearbyConnections()) {
			cmdCreateWire* createwire = (cmdCreateWire *)createGateConnectionCommand(c.srcGate, c.srcHS, c.dstGate, c.dstHS);
			if (createwire == nullptr) continue;
			createwire->Do();
			if (currentDragState == DRAG_NEWGATE && creategatecommand != nullptr) {
				creategatecommand->getConnections()->push_back(std::unique_ptr<klsCommand>(createwire));
			} else if (pendingCreateGate != nullptr) {
				pendingCreateGate->getConnections()->push_back(std::unique_ptr<klsCommand>(createwire));
			} else {
				if (movecommand == NULL) {
					movecommand = new cmdMoveSelection(gCircuit, preMove, preMoveWire, 0, 0, 0, 0);
					if (!isWithinPaste) submitCommand(movecommand);
				}
				movecommand->getConnections()->push_back(std::unique_ptr<klsCommand>(createwire));
			}
		}
	}

	// Drop a paste block with the proper move coords
	if (isWithinPaste) {
		pasteCommand->addCommand( movecommand );
		submitCommand( pasteCommand );
		isWithinPaste = false;
		autoScrollEnable(); // Re-enable auto scrolling
	}
	
	if (currentDragState == DRAG_SELECT) {
		// Release the rubber-band box with a fade instead of it just vanishing
		// -- same box drawOverlaysInto was drawing live, one last time at
		// falling alpha. Skip the flash for a box too small to have been a
		// real drag (a plain click that never moved).
		GLPoint2f fs = getDragStartCoords(), fe = getMouseCoords();
		klsBBox fadeBox;
		fadeBox.addPoint(fs); fadeBox.addPoint(fe);
		if (!fadeBox.empty() &&
		    (fadeBox.getRight() - fadeBox.getLeft() > DRAG_START_SCREEN_DELTA * getZoom() ||
		     fadeBox.getTop() - fadeBox.getBottom() > DRAG_START_SCREEN_DELTA * getZoom())) {
			dragSelectFadeBox = fadeBox;
			dragSelectFadeStart = std::chrono::steady_clock::now();
			dragSelectFading = true;
			if (!overlayFadeTimer->IsRunning()) overlayFadeTimer->Start(OVERLAY_FADE_TIMER_RATE_MS);
		}
		// The box may well have just picked something up -- fade its halo in too.
		markSelectionChanged();
	}

	// Mid-drag 'C' connections go on the undo stack last, above the move or
	// creation, so the first undo removes just the connection.
	for (klsCommand *cmd : pendingConnects) storeCommand(cmd);
	pendingConnects.clear();
	pendingCreateGate = nullptr;

	currentDragState = DRAG_NONE;
	connectSticky = false;

	Update();
}

void GUICanvas::storeCommand(klsCommand *cmd) {
	cmd->setCanvas(this);
	gCircuit->GetCommandProcessor()->Store((wxCommand *)cmd);
}

void GUICanvas::discardPendingDragCommands() {
	for (auto it = pendingConnects.rbegin(); it != pendingConnects.rend(); ++it) {
		(*it)->Undo();
		delete *it;
	}
	pendingConnects.clear();
	if (pendingCreateGate != nullptr) {
		pendingCreateGate->Undo();
		delete pendingCreateGate;
		pendingCreateGate = nullptr;
		preMove.clear();
		preMoveWire.clear();
	}
	collisionChecker.update();
}

// Add a gate from a drag and drop operation.
//
// The way this was originally done relied on mouse events switching from the
// selector pane to the main canvas, which doesn't happen when using gtk.
//
// This is a hacky solution, but it avoids needing to refactor the OnMouseUp
// event.
void GUICanvas::addGate(string gate, GLPoint2f m) {
	newDragGate = takeNewDragGate(gate);
	if (newDragGate == nullptr) return;

	newDragGate->setGLcoords(m.x, m.y);
	currentDragState = DRAG_NEWGATE;

	unselectAllGates();
	newDragGate->select();
	collisionChecker.addObject( newDragGate.get() );

	wxMouseEvent ev = wxMouseEvent(wxEVT_LEFT_UP);
	OnMouseUp(ev);
}

void GUICanvas::OnMouseEnter(wxMouseEvent& event) {
	GLPoint2f m = getMouseCoords();

	// Do a collision detection on all first-level objects.
	// The map collisionChecker.overlaps now contains
	// all of the objects involved in any collisions.
	//collisionChecker.update();

	paletteDrag().showDragImage = false;
	if (event.LeftIsDown() && paletteDrag().newGateToDrag.size() > 0 && currentDragState == DRAG_NONE && !(this->isLocked())) {
		newDragGate = takeNewDragGate(paletteDrag().newGateToDrag);
		if (newDragGate == nullptr) { paletteDrag().newGateToDrag = ""; return; }
		newDragGate->setGLcoords(m.x, m.y);
		currentDragState = DRAG_NEWGATE;
		paletteDrag().newGateToDrag = "";
		beginDrag( BUTTON_LEFT );
		unselectAllGates();
		newDragGate->select();
		collisionChecker.addObject( newDragGate.get() );
	}
	// Don't clear newGateToDrag here — OnMouseMove handles it for
	// both palette drags and quick-add placement.
}


// Cancel any in-progress drag and restore pre-drag state. Invoked by Escape and,
// via the base cancelDrag() hook, on a lost mouse capture (so an OS capture steal
// mid new-gate/paste/move doesn't leave currentDragState + newDragGate orphaned).
void GUICanvas::cancelDrag() {
	unselectAllGates();
	unselectAllWires();
	if (currentDragState == DRAG_NEWGATE && newDragGate != nullptr) {
		collisionChecker.removeObject( newDragGate.get() );
		newDragGate.reset();
		collisionChecker.update();
		paletteDrag().newGateToDrag = "";
	} else if (isWithinPaste) {
		// Cancel paste operation: undo all pasted gates/wires
		pasteCommand->Undo();
		delete pasteCommand;
		pasteCommand = nullptr;
		isWithinPaste = false;
		preMove.clear();
		preMoveWire.clear();
		collisionChecker.update();
	} else {
		// Unwind any mid-drag 'C' connections (and a gate 'C' created from
		// the palette) before putting the moved gates back.
		discardPendingDragCommands();
		if (preMove.size() > 0) {
			saveMove = false;
			for (unsigned int i = 0; i < preMove.size(); i++) {
				guiGate *g = getGate(preMove[i].id);
				if (g == nullptr) continue;
				g->setGLcoords(preMove[i].x, preMove[i].y);
				if (preMove[i].selected) g->select();
			}
			preMove.clear();
		}
		if (preMoveWire.size() > 0) {
			for (unsigned int i = 0; i < preMoveWire.size(); i++) {
				guiWire *w = getWire(preMoveWire[i].id);
				if (w == nullptr) continue;
				w->setSegmentMap(preMoveWire[i].oldWireTree);
				w->select();
			}
		}
	}
	currentDragState = DRAG_NONE;
	connectSticky = false;
	SetCursor(wxCursor(wxCURSOR_ARROW));
	endDrag(BUTTON_LEFT);
	Refresh();
}

void GUICanvas::OnKeyDown(wxKeyEvent& event) {
	switch (event.GetKeyCode()) {
	case WXK_DELETE:
	case WXK_BACK:  // macOS "delete" key (backspace)
		if (currentDragState == DRAG_NONE && !(this->isLocked())) deleteSelection();
		break;
	case WXK_ESCAPE:
		// Mid-drag with 'C' connections pending: take back just those and
		// keep dragging. A second Escape then cancels the drag itself.
		if (currentDragState == DRAG_SELECTION && !pendingConnects.empty()) {
			for (auto it = pendingConnects.rbegin(); it != pendingConnects.rend(); ++it) {
				(*it)->Undo();
				delete *it;
			}
			pendingConnects.clear();
			collisionChecker.update();
			Refresh();
		} else {
			cancelDrag();
		}
		break;
	case WXK_LEFT:
	case WXK_NUMPAD_LEFT:
		translatePan(-PAN_STEP * getZoom(), 0.0);
		break;
	case WXK_RIGHT:
	case WXK_NUMPAD_RIGHT:
		translatePan(+PAN_STEP * getZoom(), 0.0);
		break;
	case WXK_UP:
	case WXK_NUMPAD_UP:
		translatePan(0.0, PAN_STEP * getZoom());
		break;
	case WXK_DOWN:
	case WXK_NUMPAD_DOWN:
		translatePan(0.0, -PAN_STEP * getZoom());
		break;
	case 43: // + key (Shift+=)
	case 61: // = key (for zoom in without shift on Mac)
	case WXK_NUMPAD_ADD:
		zoomIn();
		break;
	case 45: // - key on top row
	case WXK_NUMPAD_SUBTRACT:
		zoomOut();
		break;
	case WXK_SPACE:
		setZoomAll();
		break;
	case 'A':
	case 'a':
		if (!event.ControlDown() && !event.AltDown() && !event.CmdDown() && currentDragState == DRAG_NONE && !this->isLocked()) {
#ifdef __WXOSX__
			QuickAddDialog* dlg = new QuickAddDialog(wxTheApp->GetTopWindow());
			dlg->Bind(wxEVT_WINDOW_MODAL_DIALOG_CLOSED, [dlg](wxWindowModalDialogEvent& evt) {
				if (evt.GetReturnCode() == wxID_OK && !dlg->getSelectedGate().empty()) {
					paletteDrag().newGateToDrag = dlg->getSelectedGate();
				}
				dlg->Destroy();
			});
			dlg->ShowWindowModal();
#else
			QuickAddDialog dlg(wxGetTopLevelParent(this));
			if (dlg.ShowModal() == wxID_OK && !dlg.getSelectedGate().empty()) {
				paletteDrag().newGateToDrag = dlg.getSelectedGate();
				CallAfter([this]() { SetFocus(); });
			}
#endif
		}
		break;
	case 'R':
	case 'r':
		if (!event.ControlDown() && !event.AltDown() && !event.CmdDown() && !this->isLocked()) {
			rotateSelection();
			Refresh();
		}
		break;
	case 'C':
	case 'c':
		// Connect a gate to whatever's unambiguously nearby -- works while
		// still holding the mouse down mid-drag (Ctrl/Cmd+C, copy, never
		// reaches here: MainFrame's CHAR_HOOK claims it first). A gate still
		// being dragged in from the palette isn't a real gate yet, so create
		// it in place first (without letting go), then connect.
		if (!event.ControlDown() && !event.AltDown() && !event.CmdDown() && !this->isLocked()) {
			if (currentDragState == DRAG_NEWGATE) commitNewDragGate();
			connectNearbyHotspots();
		}
		break;
	}
}

void GUICanvas::deleteSelection() {
	// whatever is in the selected vectors goes
	if (selectedWires.size() > 0 || selectedGates.size() > 0) submitCommand( new cmdDeleteSelection( gCircuit, this, selectedGates, selectedWires ) );
	selectedWires.clear();
	selectedGates.clear();
	preMove.clear();
	saveMove = false;

	// Do a collision detection on all first-level objects.
	// The map collisionChecker.overlaps now contains
	// all of the objects involved in any collisions.
	Update();
}

void GUICanvas::unselectAllGates() {
	unordered_map < unsigned long, guiGate* >::iterator thisGate = gateList.begin();
	while (thisGate != gateList.end()) {
		(thisGate->second)->unselect();
		thisGate++;
	}
}

void GUICanvas::unselectAllWires() {
	unordered_map < unsigned long, guiWire* >::iterator thisWire = wireList.begin();
	while (thisWire != wireList.end()) {
		(thisWire->second)->unselect();
		thisWire++;
	}
}	

void GUICanvas::copyBlockToClipboard () {
	klsClipboard myClipboard;
	// Ship the selected gates and wires out to the clipboard
	myClipboard.copyBlock( gCircuit, this, selectedGates, selectedWires );
}

void GUICanvas::cutSelectionToClipboard () {
	// Copy first -- deleteSelection() clears the selection vectors it reads from.
	copyBlockToClipboard();
	deleteSelection();
}

void GUICanvas::pasteBlockFromClipboard () {
	if (this->isLocked()) return;
	
	klsClipboard myClipboard;
	pasteCommand = myClipboard.pasteBlock( gCircuit, this );
	if (pasteCommand == NULL) return;
	currentDragState = DRAG_SELECTION; // drag until dropped
	isWithinPaste = true;
	saveMove = true;
	
	// clean up the selected gates vector
	selectedGates.clear();
	preMove.clear();
	unordered_map< unsigned long, guiGate* >::iterator thisGate = gateList.begin();
	unsigned long snapToGateID = 0;
	GLPoint2f gatecoord;
	// paste only to snapped point
	GLPoint2f mc = getSnappedPoint(getMouseCoords());
	GLPoint2f minPoint;
	bool ref = false;
	// Find top-left-most point
	while (thisGate != gateList.end()) {
		GLPoint2f temp;
		if ((thisGate->second)->isSelected()) {
			(thisGate->second)->getGLcoords(temp.x, temp.y);
			if (temp.x < minPoint.x || !ref) minPoint.x = temp.x;
			if (temp.y > minPoint.y || !ref) minPoint.y = temp.y;
			ref = true;
		}
		thisGate++;
	}
	ref = false;
	// Try to drag by the top-left-most gate
	double minMagnitude = 0.0;
	thisGate = gateList.begin();
	while (thisGate != gateList.end()) {
		GLPoint2f temp;
		if ((thisGate->second)->isSelected()) {
			if (ref) {
				(thisGate->second)->getGLcoords(temp.x, temp.y);
				float diffx = gatecoord.x - minPoint.x, diffy = gatecoord.y - minPoint.y;
				double newMag = (diffx * diffx) + (diffy * diffy);
				if (newMag < minMagnitude) {
					minMagnitude = newMag;
					gatecoord = temp;
					snapToGateID = (thisGate->first);
				}
			} else {
				(thisGate->second)->getGLcoords(gatecoord.x, gatecoord.y);
				float diffx = gatecoord.x - minPoint.x, diffy = gatecoord.y - minPoint.y;
				minMagnitude = (diffx * diffx) + (diffy * diffy);
				snapToGateID = (thisGate->first);
				ref = true;
			}
		}
		thisGate++;
	}

	// What is the difference between that gate and the mouse coords
	GLPoint2f diff( mc.x-gatecoord.x, mc.y-gatecoord.y );
	// Shift all the gates and track their differences by command
	thisGate = gateList.begin();
	while (thisGate != gateList.end()) {
		if ((thisGate->second)->isSelected()) {
			preMove.push_back(GateState((thisGate->first), 0, 0, (thisGate->second)->isSelected()));
//			(thisGate->second)->translateGLcoords(diff.x, diff.y);
			(thisGate->second)->getGLcoords(preMove[preMove.size()-1].x, preMove[preMove.size()-1].y);
			preMove[preMove.size()-1].x += diff.x; preMove[preMove.size()-1].y += diff.y;
			cmdMoveGate* mgcmd = new cmdMoveGate(gCircuit, (thisGate->first), preMove[preMove.size()-1].x-diff.x, preMove[preMove.size()-1].y-diff.y, preMove[preMove.size()-1].x, preMove[preMove.size()-1].y, true);
			mgcmd->Do();
			pasteCommand->addCommand( mgcmd );
			selectedGates.push_back((thisGate->first));
		}
		thisGate++;
	}
	// clean up the selected wires vector
	selectedWires.clear();
	preMoveWire.clear();
	unordered_map< unsigned long, guiWire* >::iterator thisWire = wireList.begin();
	while (thisWire != wireList.end()) {
		if ((thisWire->second)->isSelected()) {
			// Push back the wire's id and set up a premove state
			cmdMoveWire* movewire = new cmdMoveWire(gCircuit, (thisWire->first), (thisWire->second)->getSegmentMap(), diff);
			movewire->Do();
			pasteCommand->addCommand(movewire);
			preMoveWire.push_back(WireState((thisWire->first), (thisWire->second)->getCenter(), (thisWire->second)->getSegmentMap()));
			selectedWires.push_back((thisWire->first));
		}
		thisWire++;
	} 

	autoScrollDisable();
	beginDrag(BUTTON_LEFT);
	
	Update();
}


// Zoom the canvas to fit all items within it:
void GUICanvas::setZoomAll( void ) {
// TODO: BUG this function sometimes hangs the program.
	klsBBox zoomBox;

	// Add all the gates into the zoom all box:
	unordered_map< unsigned long, guiGate* >::iterator gateWalk = gateList.begin();
	while( gateWalk != gateList.end() ) {
		zoomBox.addBBox( (gateWalk->second)->getBBox() );
		gateWalk++;
	}

	// Add all the wires into the zoom all box:
	unordered_map< unsigned long, guiWire* >::iterator wireWalk = wireList.begin();
	while( wireWalk != wireList.end() ) {
		zoomBox.addBBox((wireWalk->second)->getBBox());
		wireWalk++;
	}
	
	// Make sure to not have a dumb zoom factor on an empty canvas:
	if( gateList.empty() ) {
		zoomBox.addPoint(GLPoint2f(0, 0));
	}
	
	// Put some margin around the zoom box:
	zoomBox.extendTop( ZOOM_ALL_MARGIN );
	zoomBox.extendBottom( ZOOM_ALL_MARGIN );
	zoomBox.extendLeft( ZOOM_ALL_MARGIN );
	zoomBox.extendRight( ZOOM_ALL_MARGIN );

	// Zoom to the zoom-all box:
	setViewport( zoomBox.getTopLeft(), zoomBox.getBottomRight() );
}


// print page contents
void GUICanvas::printLists() {
	wxGetApp().logfile << "printing page lists" << endl << flush;
	unordered_map< unsigned long, guiGate* >::iterator thisGate = gateList.begin();
	while (thisGate != gateList.end()) {
		float x, y;
		(thisGate->second)->getGLcoords(x, y);
		wxGetApp().logfile << " gate " << thisGate->first << " type " << (thisGate->second)->getLibraryGateName() << " at " << x << "," << y << endl << flush;
		thisGate++;
	}
	unordered_map< unsigned long, guiWire* >::iterator thisWire = wireList.begin();
	while (thisWire != wireList.end()) {
		wxGetApp().logfile << " wire " << thisWire->first << endl << flush;
		thisWire++;
	}
}	

// Update the collision checker and refresh
void GUICanvas::Update() {
	if (minimap == NULL){
		return;
	}

	minimap->setLists( &gateList, &wireList );
	minimap->setCanvas(this);
	updateMiniMap();
	Refresh();
	wxWindow::Update();
}

//Julian: Moved implementation of zoom fuctions out of header.

void GUICanvas::zoomIn() {
	//Only zoom when not dragging
	if (currentDragState == DRAG_NONE) {
		animateZoomTo(getZoom() * ZOOM_STEP);
	}
}

void GUICanvas::zoomOut() {
	//Only zoom when not dragging
	if (currentDragState == DRAG_NONE) {
		animateZoomTo(getZoom() / ZOOM_STEP);
	}
}

bool GUICanvas::tryFinishConnection() {
	// Only a gate-sourced DRAG_CONNECT ever finishes here (matching the
	// pre-existing press-drag-release behavior this was extracted from).
	if (!currentConnectionSource.isGate) return false;

	wxCommand *command = nullptr;
	if (drawWireHover) {
		command = createGateWireConnectionCommand(
			currentConnectionSource.objectID,
			currentConnectionSource.connection, wireHoverID);
	} else if (hotspotHighlight.size() > 0) {
		command = createGateConnectionCommand(
			currentConnectionSource.objectID,
			currentConnectionSource.connection,
			hotspotGate, hotspotHighlight);
	}

	if (command == nullptr) return false;
	submitCommand((klsCommand *)command);
	return true;
}

klsCommand * GUICanvas::createGateWireConnectionCommand(IDType gateId, const string &hotspot, IDType wireId) {

	guiGate *gate = getGate(gateId);
	guiWire *wire = getWire(wireId);
	if (gate == nullptr || wire == nullptr) return nullptr;

	// Make sure not already connected.
	if (gate->isConnected(hotspot) &&
		gate->getConnection(hotspot) == wire) {
		return nullptr;
	}

	cmdConnectWire *command = new cmdConnectWire(gCircuit, wireId, gateId, hotspot);

	if (command->validateBusLines()) {
		return command;
	}
	else {
		delete command;
		return nullptr;
	}
}

klsCommand * GUICanvas::createGateConnectionCommand(IDType gate1Id, const string &hotspot1, IDType gate2Id, const string &hotspot2) {

	guiGate *gate1 = getGate(gate1Id);
	guiGate *gate2 = getGate(gate2Id);
	if (gate1 == nullptr || gate2 == nullptr) return nullptr;

	// Don't connect a hotspot to itself.
	if (gate1 == gate2 && hotspot1 == hotspot2) {
		return nullptr;
	}

	// Make sure not already connected.
	if (gate1->isConnected(hotspot1) &&
		gate2->isConnected(hotspot2) &&
		gate1->getConnection(hotspot1) == gate2->getConnection(hotspot2)) {
		return nullptr;
	}

	// Neither connected, so create wire.
	if (!gate1->isConnected(hotspot1) &&
		!gate2->isConnected(hotspot2)) {


		gateHotspot *hs1 = gate1->getHotspot(hotspot1);
		if (hs1 == nullptr) return nullptr;   // no such pin on this gate

		vector<IDType> wireIds(hs1->getBusLines());

		// Get the correct number of new, unique wire ids.
		for (int i = 0; i < (int)wireIds.size(); i++) {
			wireIds[i] = gCircuit->getNextAvailableWireID();
		}

		cmdConnectWire *connectwire =
			new cmdConnectWire(gCircuit, wireIds[0], gate1Id, hotspot1);

		cmdConnectWire *connectwire2 =
			new cmdConnectWire(gCircuit, wireIds[0], gate2Id, hotspot2);

		cmdCreateWire *createWire =
			new cmdCreateWire(this, gCircuit, wireIds, connectwire, connectwire2);

		if (createWire->validateBusLines()) {
			return createWire;
		}
		else {
			delete createWire;
			return nullptr;
		}
	}
	else {
		
		// One of the gates is connected.
		if (gate1->isConnected(hotspot1)) {
			return createGateWireConnectionCommand(gate2Id,
				hotspot2, gate1->getConnection(hotspot1)->getID());
		}
		else if (gate2->isConnected(hotspot2)) {
			return createGateWireConnectionCommand(gate1Id,
				hotspot1, gate2->getConnection(hotspot2)->getID());
		}
		return nullptr;
	}
}

// One step clockwise on screen. The world is y-up, so the model matrix turns
// counter-clockwise for a rising angle -- stepping the stored angle DOWN is what
// reads as clockwise to the user. The angle values keep their meaning, so this
// changes only the order repeated presses walk through them; saved circuits and
// the file format are untouched.
static float rotatedClockwise(const std::string& current) {
	istringstream iss(current);
	float angle = 0.0f;
	iss >> angle;
	return fmod(angle - 90.0f + 360.0f, 360.0f);
}

void GUICanvas::rotateSelection() {

	// If we're placing a gate from quick add menu, rotate that gate
	if (currentDragState == DRAG_NEWGATE && newDragGate != nullptr) {
		ostringstream oss;
		oss << rotatedClockwise(newDragGate->getGUIParam("angle"));
		newDragGate->setGUIParam("angle", oss.str());
		return;
	}

	// If we're in paste mode, rotate all gates being pasted that have no wire connections
	if (isWithinPaste) {
		for (unsigned int i = 0; i < preMove.size(); i++) {
			guiGate* gate = getGate(preMove[i].id);
			if (gate == nullptr) continue;

			map< string, GLPoint2f > hotspots = gate->getHotspotList();
			bool hasConnections = false;
			for (auto& hotspot : hotspots) {
				if (gate->isConnected(hotspot.first)) {
					hasConnections = true;
					break;
				}
			}
			if (hasConnections) continue;

			ostringstream oss;
			oss << rotatedClockwise(gate->getGUIParam("angle"));
			gate->setGUIParam("angle", oss.str());
		}
		return;
	}

	// Otherwise rotate all selected gates that aren't connected to wires
	unordered_map< unsigned long, guiGate* >::iterator gateWalk = gateList.begin();
	while (gateWalk != gateList.end()) {
		if (gateWalk->second->isSelected()) {
			// Check if gate has any connections - if so, skip it
			map< string, GLPoint2f > hotspots = gateWalk->second->getHotspotList();
			bool hasConnections = false;
			for (auto& hotspot : hotspots) {
				if (gateWalk->second->isConnected(hotspot.first)) {
					hasConnections = true;
					break;
				}
			}

			// Only rotate if gate has no connections
			if (!hasConnections) {
				ostringstream oss;
				oss << rotatedClockwise(gateWalk->second->getGUIParam("angle"));
				gateWalk->second->setGUIParam("angle", oss.str());
			}
		}
		gateWalk++;
	}
}

bool GUICanvas::commitNewDragGate() {
	if (currentDragState != DRAG_NEWGATE || newDragGate == nullptr) return false;

	// Create the real gate at its current (ghost) position -- the same thing
	// OnMouseUp does on a real drop.
	int newGID = gCircuit->getNextAvailableGateID();
	float nx, ny;
	newDragGate->getGLcoords(nx, ny);
	// Applied now but only put on the undo stack at drop (OnMouseUp), with
	// its final position -- or undone if the drag is cancelled.
	cmdCreateGate* creategatecommand = new cmdCreateGate(this, gCircuit, newGID, newDragGate->getLibraryGateName(), nx, ny);
	creategatecommand->Do();
	collisionChecker.removeObject(newDragGate.get());
	collisionChecker.update();

	guiGate *created = gCircuit->getGate(newGID);
	newDragGate.reset();
	if (created == nullptr) { delete creategatecommand; currentDragState = DRAG_NONE; return false; }
	pendingCreateGate = creategatecommand;

	cmdSetParams setgateparams(gCircuit, newGID, paramSet(created->getAllGUIParams(), created->getAllLogicParams()));
	setgateparams.Do();
	unselectAllGates();
	unselectAllWires();
	created->select();

	// Switch from "follow the cursor" (DRAG_NEWGATE) to the normal
	// delta-from-press-point move model (DRAG_SELECTION) without letting go
	// of the mouse: back-solve a baseline so preMove[0]+diffSnap lands on the
	// gate's just-created position right now, so only movement from here
	// forward (not the whole palette drag so far) gets added as the user
	// keeps dragging.
	GLPoint2f m = getMouseCoords();
	GLPoint2f dStart = getDragStartCoords(BUTTON_LEFT);
	GLPoint2f mSnap = getSnappedPoint(m);
	GLPoint2f dStartSnap = getSnappedPoint(dStart);
	GLPoint2f diffSnap(mSnap.x - dStartSnap.x, mSnap.y - dStartSnap.y);

	preMove.clear();
	preMove.push_back(GateState(newGID, nx - diffSnap.x, ny - diffSnap.y, true));
	preMoveWire.clear();
	saveMove = true;

	currentDragState = DRAG_SELECTION;
	return true;
}

vector<GUICanvas::NearbyConnection> GUICanvas::findNearbyConnections() {
	vector<NearbyConnection> candidates;

	float radius = HOTSPOT_CONNECT_SCREEN_RADIUS * getZoom();

	for (auto &srcEntry : gateList) {
		guiGate* src = srcEntry.second;
		if (!src->isSelected()) continue;

		for (auto &srcHS : src->getHotspotList()) {
			if (src->isConnected(srcHS.first)) continue;
			GLPoint2f sp = srcHS.second;

			guiGate* bestGate = nullptr;
			string bestHSName;
			float bestDist = -1.0f, secondDist = -1.0f;

			for (auto &dstEntry : gateList) {
				guiGate* dst = dstEntry.second;
				// Only connect to a stationary gate -- if it's also selected
				// (and so moving with src) their relative position won't change.
				if (dst == src || dst->isSelected()) continue;

				for (auto &dstHS : dst->getHotspotList()) {
					if (dst->isConnected(dstHS.first)) continue;
					float dx = dstHS.second.x - sp.x, dy = dstHS.second.y - sp.y;
					float d = sqrtf(dx * dx + dy * dy);
					if (d > radius) continue;

					if (bestDist < 0.0f || d < bestDist) {
						secondDist = bestDist;
						bestDist = d;
						bestGate = dst;
						bestHSName = dstHS.first;
					} else if (secondDist < 0.0f || d < secondDist) {
						secondDist = d;
					}
				}
			}

			if (bestGate == nullptr) continue;
			// Ambiguous -- a second candidate is within a hair of as close as
			// the best one (a near-tie), so don't guess which pin was meant.
			if (secondDist >= 0.0f && secondDist < bestDist * HOTSPOT_CONNECT_AMBIGUITY_RATIO) continue;

			candidates.push_back({src->getID(), srcHS.first, bestGate->getID(), bestHSName, bestDist});
		}
	}

	// Same tie rule from the target's side: if several dragged pins picked
	// the same target pin (an output sitting between two inputs), only a
	// clearly-closest one gets it; on a near-tie, none do.
	vector<NearbyConnection> result;
	for (size_t i = 0; i < candidates.size(); i++) {
		bool keep = true;
		for (size_t j = 0; j < candidates.size() && keep; j++) {
			if (i == j) continue;
			if (candidates[j].dstGate != candidates[i].dstGate || candidates[j].dstHS != candidates[i].dstHS) continue;
			// Another pin wants this target: drop i unless it's clearly closer.
			if (candidates[j].dist * HOTSPOT_CONNECT_AMBIGUITY_RATIO > candidates[i].dist &&
			    candidates[i].dist * HOTSPOT_CONNECT_AMBIGUITY_RATIO > candidates[j].dist) keep = false; // near-tie
			else if (candidates[j].dist < candidates[i].dist) keep = false;                            // j is clearly closer
		}
		if (keep) result.push_back(candidates[i]);
	}
	return result;
}

int GUICanvas::connectNearbyHotspots() {
	vector<NearbyConnection> candidates = findNearbyConnections();
	if (candidates.empty()) return 0;

	int madeCount = 0;

	if (currentDragState == DRAG_SELECTION && preMove.size() > 0) {
		// Mid-drag (button still down): apply the connection now, but hold it
		// off the undo stack until the drop so it lands ABOVE the move --
		// undo then removes the connection first, then the move.
		for (auto &c : candidates) {
			klsCommand *cmd = createGateConnectionCommand(c.srcGate, c.srcHS, c.dstGate, c.dstHS);
			if (cmd == nullptr) continue;
			cmd->setCanvas(this);
			cmd->Do();
			pendingConnects.push_back(cmd);
			madeCount++;
		}
	} else {
		// Not mid-drag -- nothing to bundle into, so each connection is its
		// own undo step.
		for (auto &c : candidates) {
			if (klsCommand *cmd = createGateConnectionCommand(c.srcGate, c.srcHS, c.dstGate, c.dstHS)) {
				submitCommand(cmd);
				madeCount++;
			}
		}
	}

	if (madeCount > 0) {
		collisionChecker.update();
		Refresh();
	}
	return madeCount;
}
