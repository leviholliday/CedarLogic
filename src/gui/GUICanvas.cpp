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
#include "MainFrame.h"
#include "paramDialog.h"
#include "QuickAddDialog.h"
#include "klsClipboard.h"
#include "guiWire.h"
#include "route/GridRouter.h"
#include "route/Layout.h"
#include <map>
#include <set>
#include "render/Scene.h"
#include "render/RenderStyle.h"
#ifdef WITH_SKIA
#include "render/SkiaProbe.h"
#endif


#include <wx/dnd.h>
#include <cstring>

// Included to use the min() and max() templates:
#include <algorithm>
#include <queue>
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
	// A Space released while another window had focus never reaches OnKeyUp.
	Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) {
		if (spaceHeld && currentDragState != DRAG_PAN) SetCursor(wxCursor(wxCURSOR_ARROW));
		spaceHeld = false;
		e.Skip();
	});

	SetDropTarget(new DnDText(this));

#ifdef __WXOSX__
	// Suppress macOS bonk sound for keys handled in OnKeyDown
	Bind(wxEVT_CHAR, [this](wxKeyEvent& evt) {
		// Every bare key OnKeyDown's switch handles; add new ones here too.
		static const wxString handled = "aAcCdDrRsStTvVxX +=-";
		int key = evt.GetKeyCode();
		const bool previewKey = tidy.active && (key == WXK_RETURN || key == WXK_TAB || key == WXK_ESCAPE);
		if (!evt.CmdDown() && !evt.AltDown() && ((key < 128 && handled.Find((wxChar)key) != wxNOT_FOUND) || previewKey)) {
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
	s.simView = renderMode().simView;
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
	const float fade = appearProgress();
	minor.color = style.gridColor((float)GRID_INTENSITY * fade);
	minor.width = 1.0f;
	major.color = style.gridColor((float)GRID_INTENSITY * 2.5f * fade);
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
		const Color dot = style.gridColor((float)GRID_INTENSITY * 3.0f * fade);
		const Color dotMajor = style.gridColor((float)GRID_INTENSITY * 5.0f * fade);
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
	if (style.simView) sceneKey ^= 0x94D049BB133111EBULL;
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
		if (renderMode().simView) self->drawSimBarInto(s, screenT, logicalW, logicalH);
		if (self->tidy.active) self->drawTidyBannerInto(s, screenT, logicalW, logicalH);
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
void GUICanvas::playAppearAnimation() {
	closing = false;   // a reopened tab must not come back still dimmed
	appearing = true;
	appearStart = std::chrono::steady_clock::now();
	if (!overlayFadeTimer->IsRunning()) overlayFadeTimer->Start(OVERLAY_FADE_TIMER_RATE_MS);
	Refresh();
}

void GUICanvas::playCloseAnimation() {
	closing = true;
	appearing = false;
	closeStart = std::chrono::steady_clock::now();
	if (!overlayFadeTimer->IsRunning()) overlayFadeTimer->Start(OVERLAY_FADE_TIMER_RATE_MS);
	Refresh();
}

void GUICanvas::cancelCloseAnimation() {
	if (!closing) return;
	closing = false;   // the fade timer stops itself on its next tick
	Refresh();
}

int GUICanvas::closeAnimationMs() { return CLOSE_ANIM_MS; }

// 0 while the tab is staying, heading to 1 as it goes.
float GUICanvas::closeProgress() const {
	if (!closing) return 0.0f;
	const long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - closeStart).count();
	const float t = std::min(1.0f, std::max(0.0f, (float)ms / CLOSE_ANIM_MS));
	return t * t;   // ease-in: slow to let go, then gone
}

float GUICanvas::appearProgress() const {
	if (!appearing) return 1.0f;
	const long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - appearStart).count();
	const float t = std::min(1.0f, std::max(0.0f, (float)ms / APPEAR_ANIM_MS));
	return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);   // ease-out cubic
}

void GUICanvas::OnOverlayFadeTimer(wxTimerEvent& WXUNUSED(event)) {
	const auto now = std::chrono::steady_clock::now();
	// Simulation View animates (dashes, the LIVE light) while running -- but
	// only on a canvas you can see. A tab left behind would otherwise keep
	// repainting 60 times a second until the mode ended; drawing it again
	// (drawOverlaysInto) restarts this timer when it comes back.
	const bool flowing = renderMode().simView && !simPaused() && IsShownOnScreen();
	if (flowing) {
		const double dt = std::min(0.05, std::chrono::duration<double>(now - flowLastTick).count());
		// Dash speed follows the simulation speed slider: 40 px/s at 25 ms per
		// step, faster as steps get shorter (square-root curve so the ends of
		// the 1-500 ms range stay watchable).
		MainFrame* mf = wxGetApp().mainframe;
		const double ms = std::max(1, mf ? mf->GetStepMs() : 25);
		const double pxPerSec = std::min(240.0, std::max(8.0, 40.0 * std::sqrt(25.0 / ms)));
		flowPhasePx += dt * pxPerSec;
	}
	flowLastTick = now;
	if (appearing && std::chrono::duration_cast<std::chrono::milliseconds>(now - appearStart).count() >= APPEAR_ANIM_MS)
		appearing = false;
	if (dragSelectFading) {
		const long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - dragSelectFadeStart).count();
		if (ms >= DRAGSELECT_FADE_MS) dragSelectFading = false;
	}
	const long selMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - selectionChangedAt).count();
	Refresh();
	if (!dragSelectFading && !appearing && !closing && !flowing && selMs >= SELECTION_FADE_MS) overlayFadeTimer->Stop();
}

#ifdef WITH_SKIA
void GUICanvas::drawOverlaysInto(cl::render::Scene& scene) {
	using cl::render::Point;
	using cl::render::Color;
	using cl::render::Stroke;
	const float r = HOTSPOT_SCREEN_RADIUS * (float)getZoom();
	const Color accent = liveStyle(renderMode().darkMode).accent();

	// A tab on its way out dims towards the window background, so it reads as
	// leaving rather than blinking off. Over the whole visible area, so it
	// covers the circuit as well as the grid.
	if (closing) {
		const float p = closeProgress();
		wxSize sz = GetClientSize();
		GLdouble px, py; getPan(px, py);
		const double vz = getZoom() > 0 ? getZoom() : 1.0;
		const Color bg = liveStyle(renderMode().darkMode).background();
		scene.fillRect(Point((float)px, (float)(py - sz.GetHeight() * vz)),
		               Point((float)(px + sz.GetWidth() * vz), (float)py),
		               Color(bg.r, bg.g, bg.b, p));
	}

	auto box = [&scene](float x, float y, float rad, const Color& c) {
		Point pts[4] = { Point(x - rad, y + rad), Point(x + rad, y + rad),
		                 Point(x + rad, y - rad), Point(x - rad, y - rad) };
		scene.polyline(pts, 4, Stroke(c, 1.0f), true);
	};

	drawSignalFlowInto(scene);

	// Tidy Up preview: where things were, faintly, under where they're going.
	if (tidy.active) {
		const Color ghost(accent.r, accent.g, accent.b, 0.35f);
		for (size_t i = 0; i + 3 < tidy.ghostRects.size(); i += 4) {
			const float l = tidy.ghostRects[i], b = tidy.ghostRects[i + 1];
			const float rr = tidy.ghostRects[i + 2], t = tidy.ghostRects[i + 3];
			Point pts[4] = { Point(l, b), Point(l, t), Point(rr, t), Point(rr, b) };
			scene.polyline(pts, 4, Stroke(ghost, 1.0f), true);
		}
		std::vector<Point> segs;
		for (size_t i = 0; i + 3 < tidy.ghostLines.size(); i += 4) {
			segs.push_back(Point(tidy.ghostLines[i], tidy.ghostLines[i + 1]));
			segs.push_back(Point(tidy.ghostLines[i + 2], tidy.ghostLines[i + 3]));
		}
		if (!segs.empty()) scene.lines(&segs[0], segs.size(), Stroke(Color(accent.r, accent.g, accent.b, 0.22f), 1.0f));
	}
	if (renderMode().simView && !simPaused() && !overlayFadeTimer->IsRunning()) {
		flowLastTick = std::chrono::steady_clock::now();
		overlayFadeTimer->Start(OVERLAY_FADE_TIMER_RATE_MS);
	}

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

void GUICanvas::drawSignalFlowInto(cl::render::Scene& scene) {
	using cl::render::Point;
	using cl::render::Color;
	using cl::render::Stroke;
	flowDrewSomething = false;
	if (!renderMode().simView) return;

	const float z = (float)getZoom();            // world units per screen px
	const float period = 16.0f * z;              // dash + gap
	const float dashLen = 7.0f * z;
	const float phase = (float)std::fmod(flowPhasePx, 16.0) * z;
	const cl::render::RenderStyle style = liveStyle(true);
	const Color on = style.simOn();
	Stroke glow(Color(on.r, on.g, on.b, 0.35f), 5.0f * style.wireScale);
	glow.cap = cl::render::Cap::Round;
	Stroke core(Color(0.88f, 1.0f, 1.0f, 0.95f), 1.8f * style.wireScale);
	core.cap = cl::render::Cap::Round;

	GLPoint2f tl, br;
	getViewport(tl, br);
	klsBBox view;
	view.addPoint(tl);
	view.addPoint(br);
	view.extendTop(4 * z); view.extendBottom(4 * z); view.extendLeft(4 * z); view.extendRight(4 * z);

	// Lit lights bloom. Drawn first so the dashes sit on top of any overlap.
	for (auto& ge : gateList) {
		guiGateLED* led = dynamic_cast<guiGateLED*>(ge.second);
		if (led == nullptr || !led->getBBox().overlaps(view)) continue;
		bool lit = false;
		for (auto& hs : led->getHotspotList()) {
			if (!led->isConnected(hs.first)) continue;
			const std::vector<StateType>& st = led->getConnection(hs.first)->getState();
			lit = !st.empty() && st[0] == ONE;
			break;
		}
		if (!lit) continue;
		float gx, gy;
		led->getGLcoords(gx, gy);
		scene.fillCircle(Point(gx, gy), 2.8f, Color(on.r, on.g, on.b, 0.07f));
		scene.fillCircle(Point(gx, gy), 1.9f, Color(on.r, on.g, on.b, 0.12f));
		scene.fillCircle(Point(gx, gy), 1.25f, Color(on.r, on.g, on.b, 0.20f));
	}

	std::vector<Point> glowPts, corePts;
	int budget = 6000;   // dashes per frame, so a huge circuit can't stall painting

	for (auto& we : wireList) {
		guiWire* w = we.second;
		if (w == nullptr || budget <= 0) continue;
		const std::vector<StateType>& st = w->getState();
		bool anyOn = false;
		for (StateType v : st) if (v == ONE) { anyOn = true; break; }
		if (!anyOn) continue;
		if (!w->getBBox().overlaps(view)) continue;

		// The pin driving this wire: the one that's a gate output.
		wireConnection driver;
		bool haveDriver = false;
		for (const wireConnection& c : w->getConnections()) {
			guiGate* g = getGate(c.gid);
			if (g != nullptr && !g->isConnectionInput(c.connection)) { driver = c; haveDriver = true; break; }
		}
		if (!haveDriver) continue;
		float px, py;
		getGate(driver.gid)->getHotspotCoords(driver.connection, px, py);

		const std::map<long, wireSegment> segs = w->getSegmentMap();
		long start = -1;
		for (const auto& se : segs) {
			for (const wireConnection& c : se.second.connections)
				if (c.gid == driver.gid && c.connection == driver.connection) { start = se.first; break; }
			if (start >= 0) break;
		}
		if (start < 0) continue;

		// Distance along the wire from the driver to where each segment is
		// first reached (shortest path through the junctions).
		auto axisOf = [](const wireSegment& sg, const GLPoint2f& p) { return sg.isHorizontal() ? p.x : p.y; };
		std::map<long, float> dist;
		std::map<long, float> entry;   // entry position along the segment's own axis
		typedef std::pair<float, long> QItem;
		std::priority_queue<QItem, std::vector<QItem>, std::greater<QItem>> q;
		{
			const wireSegment& s0 = segs.at(start);
			const float a0 = axisOf(s0, s0.begin), a1 = axisOf(s0, s0.end);
			const float pinAxis = s0.isHorizontal() ? px : py;
			entry[start] = std::min(std::max(pinAxis, std::min(a0, a1)), std::max(a0, a1));
			dist[start] = 0.0f;
			q.push(QItem(0.0f, start));
		}
		while (!q.empty()) {
			const QItem top = q.top(); q.pop();
			if (top.first > dist[top.second] + 1e-5f) continue;
			const wireSegment& sg = segs.at(top.second);
			for (const auto& ix : sg.intersects) {
				const float d = top.first + std::fabs(ix.first - entry[top.second]);
				const float otherAxis = sg.isHorizontal() ? sg.begin.y : sg.begin.x;
				for (long o : ix.second) {
					if (segs.find(o) == segs.end()) continue;
					auto it = dist.find(o);
					if (it != dist.end() && it->second <= d + 1e-5f) continue;
					dist[o] = d;
					entry[o] = otherAxis;
					q.push(QItem(d, o));
				}
			}
		}

		// Dashes cover wherever (distance - phase) mod period < dashLen, so as
		// phase grows they march away from the driver, splitting at branches.
		for (const auto& de : dist) {
			const wireSegment& sg = segs.at(de.first);
			const bool horiz = sg.isHorizontal();
			const float a0 = std::min(axisOf(sg, sg.begin), axisOf(sg, sg.end));
			const float a1 = std::max(axisOf(sg, sg.begin), axisOf(sg, sg.end));
			const float fixed = horiz ? sg.begin.y : sg.begin.x;
			const float e = entry[de.first], D = de.second;
			auto at = [&](float a) { return horiz ? Point(a, fixed) : Point(fixed, a); };
			for (int dir = -1; dir <= 1; dir += 2) {
				const float len = dir < 0 ? e - a0 : a1 - e;
				if (len <= 0.0f) continue;
				// First dash start at or before u = 0.
				float u = std::fmod(phase - D, period);
				if (u > 0) u -= period;
				for (; u < len && budget > 0; u += period, budget--) {
					const float u0 = std::max(0.0f, u), u1 = std::min(len, u + dashLen);
					if (u1 <= u0) continue;
					const Point p0 = at(e + dir * u0), p1 = at(e + dir * u1);
					glowPts.push_back(p0); glowPts.push_back(p1);
					corePts.push_back(p0); corePts.push_back(p1);
				}
			}
		}
		flowDrewSomething = true;
	}
	if (!glowPts.empty()) scene.lines(&glowPts[0], glowPts.size(), glow);
	if (!corePts.empty()) scene.lines(&corePts[0], corePts.size(), core);
}

bool GUICanvas::simPaused() const {
	MainFrame* mf = wxGetApp().mainframe;
	return mf != nullptr && mf->IsSimPaused();
}

// The speed slider runs fast on the right: the step time (the toolbar's
// 1-500 ms slider) mapped logarithmically, so the useful fast end isn't
// crammed into a few pixels.
static const float SIM_MS_MIN = 1.0f, SIM_MS_MAX = 500.0f;
static float simSpeedFraction(int ms) {
	const float t = std::log(std::max(SIM_MS_MIN, (float)ms) / SIM_MS_MIN) / std::log(SIM_MS_MAX / SIM_MS_MIN);
	return 1.0f - std::min(1.0f, std::max(0.0f, t));
}

void GUICanvas::simSetSpeedFromX(float x) {
	MainFrame* mf = wxGetApp().mainframe;
	if (mf == nullptr || simSpeedRect.w <= 0) return;
	const float f = std::min(1.0f, std::max(0.0f, (x - simSpeedRect.x) / simSpeedRect.w));
	mf->SetStepMs((int)std::lround(SIM_MS_MIN * std::pow(SIM_MS_MAX / SIM_MS_MIN, 1.0f - f)));
}

void GUICanvas::simBarClick(float x, float y) {
	MainFrame* mf = wxGetApp().mainframe;
	if (mf == nullptr) return;
	if (simPlayRect.contains(x, y))      mf->SetSimPaused(!mf->IsSimPaused());
	else if (simStepRect.contains(x, y)) mf->StepSimOnce();
	else if (simDoneRect.contains(x, y)) mf->SetSimView(false);
	else if (simSpeedRect.contains(x, y)) {
		simSpeedDragging = true;
		simSetSpeedFromX(x);
	}
	Refresh();
}

void GUICanvas::drawSimBarInto(cl::render::Scene& scene, const cl::render::Transform& screenT,
                               float W, float H) {
	using cl::render::Point;
	using cl::render::Color;
	using cl::render::Stroke;
	using cl::render::measuredTextWidth;
	scene.setViewport(screenT);
	// Everything below is laid out top-down in logical px; flip for the scene.
	auto P = [H](float x, float y) { return Point(x, H - y); };
	auto roundRect = [&](float x, float y, float w, float h, float r) {
		// Corners in order around the rectangle (top-right, bottom-right,
		// bottom-left, top-left), each a quarter circle in 15-degree steps.
		std::vector<Point> pts;
		const float cx[4] = { x + w - r, x + w - r, x + r, x + r };
		const float cy[4] = { y + r, y + h - r, y + h - r, y + r };
		for (int c = 0; c < 4; c++)
			for (int i = 0; i <= 6; i++) {
				const float a = (-90.0f + 90.0f * c + 15.0f * i) * 3.14159265f / 180.0f;
				pts.push_back(P(cx[c] + r * std::cos(a), cy[c] + r * std::sin(a)));
			}
		return pts;
	};
	auto fillRound = [&](float x, float y, float w, float h, float r, const Color& c) {
		std::vector<Point> pts = roundRect(x, y, w, h, r);
		scene.fillPolygon(&pts[0], pts.size(), c);
	};
	auto strokeRound = [&](float x, float y, float w, float h, float r, const Color& c) {
		std::vector<Point> pts = roundRect(x, y, w, h, r);
		scene.polyline(&pts[0], pts.size(), Stroke(c, 1.0f), true);
	};
	auto text = [&](float x, float y, const char* str, float px, const Color& c) {
		scene.text(P(x, y), str, px, c);
	};

	MainFrame* mf = wxGetApp().mainframe;
	const bool paused = simPaused();
	const cl::render::RenderStyle style = liveStyle(true);
	const Color on = style.simOn();
	const Color ink(0.86f, 0.93f, 1.0f, 1.0f);
	const Color dim(0.48f, 0.58f, 0.68f, 1.0f);
	const Color faint(1.0f, 1.0f, 1.0f, 0.07f);
	const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();

	const float barH = 52.0f, margin = 14.0f;
	const float barW = std::min(W - 2 * margin, 1040.0f);
	const float bx = (W - barW) * 0.5f, by = H - margin - barH;
	simBarRect = { bx, by, barW, barH };
	const float midY = by + barH * 0.5f;

	// Glass-dark panel with a hairline cyan edge and a faint top highlight.
	fillRound(bx, by + 3, barW, barH, 14, Color(0, 0, 0, 0.35f));   // soft shadow
	fillRound(bx, by, barW, barH, 14, Color(0.055f, 0.070f, 0.090f, 0.94f));
	strokeRound(bx, by, barW, barH, 14, Color(on.r, on.g, on.b, 0.22f));
	{
		const Point hl[2] = { P(bx + 16, by + 1.5f), P(bx + barW - 16, by + 1.5f) };
		scene.lines(hl, 2, Stroke(Color(1, 1, 1, 0.06f), 1.0f));
	}

	float x = bx + 18;

	// Status: a breathing light while live, a steady amber one when paused.
	{
		const Color live(0.36f, 1.0f, 0.62f, 1.0f), amber(1.0f, 0.72f, 0.25f, 1.0f);
		const Color c = paused ? amber : live;
		const float breathe = paused ? 1.0f : (float)(0.6 + 0.4 * std::sin(secs * 3.2));
		scene.fillCircle(P(x + 5, midY), 9.0f, Color(c.r, c.g, c.b, 0.12f * breathe));
		scene.fillCircle(P(x + 5, midY), 4.0f, Color(c.r, c.g, c.b, 0.55f + 0.45f * breathe));
		text(x + 18, midY - 12, "SIMULATION", 9.0f, dim);
		text(x + 18, midY + 1, paused ? "PAUSED" : "LIVE", 13.0f, paused ? amber : ink);
		x += 18 + 86;
	}
	auto divider = [&]() {
		const Point d[2] = { P(x, by + 12), P(x, by + barH - 12) };
		scene.lines(d, 2, Stroke(Color(1, 1, 1, 0.08f), 1.0f));
		x += 16;
	};
	divider();

	// Play/pause and step.
	const float btn = 32.0f;
	simPlayRect = { x, midY - btn / 2, btn, btn };
	fillRound(x, midY - btn / 2, btn, btn, 9, paused ? Color(on.r, on.g, on.b, 0.18f) : faint);
	strokeRound(x, midY - btn / 2, btn, btn, 9, Color(1, 1, 1, 0.08f));
	{
		const float cx = x + btn / 2, cy = midY;
		if (paused) {
			const Point tri[3] = { P(cx - 4, cy - 7), P(cx + 7, cy), P(cx - 4, cy + 7) };
			scene.fillPolygon(tri, 3, on);
		} else {
			scene.fillRect(P(cx - 6, cy - 7), P(cx - 2, cy + 7), ink);
			scene.fillRect(P(cx + 2, cy - 7), P(cx + 6, cy + 7), ink);
		}
	}
	x += btn + 8;
	simStepRect = { x, midY - btn / 2, btn, btn };
	fillRound(x, midY - btn / 2, btn, btn, 9, faint);
	strokeRound(x, midY - btn / 2, btn, btn, 9, Color(1, 1, 1, 0.08f));
	{
		const float cx = x + btn / 2, cy = midY;
		const Point tri[3] = { P(cx - 6, cy - 6), P(cx + 3, cy), P(cx - 6, cy + 6) };
		scene.fillPolygon(tri, 3, ink);
		scene.fillRect(P(cx + 4, cy - 6), P(cx + 6.5f, cy + 6), ink);
	}
	x += btn + 16;
	divider();

	// Speed slider (fast on the right) with its step time.
	{
		const float trackW = 150.0f;
		text(x, midY - 14, "SPEED", 9.0f, dim);
		const float ty = midY + 6;
		simSpeedRect = { x - 6, ty - 12, trackW + 12, 24 };
		const int ms = mf ? mf->GetStepMs() : 25;
		const float f = simSpeedFraction(ms);
		const float kx = x + f * trackW;
		fillRound(x, ty - 2, trackW, 4, 2, Color(1, 1, 1, 0.12f));
		if (kx - x > 4) fillRound(x, ty - 2, kx - x, 4, 2, Color(on.r, on.g, on.b, 0.75f));
		scene.fillCircle(P(kx, ty), 10.0f, Color(on.r, on.g, on.b, 0.14f));
		scene.fillCircle(P(kx, ty), 6.0f, Color(0.92f, 1.0f, 1.0f, 1.0f));
		char buf[32];
		std::snprintf(buf, sizeof buf, "%d ms / step", ms);
		text(x + trackW + 14, midY - 5, buf, 11.0f, ink);
		x += trackW + 14 + measuredTextWidth(buf, 11.0f) + 16;
	}
	divider();

	// Done, pinned right.
	const float doneW = 76.0f, doneH = 30.0f;
	const float dx = bx + barW - 16 - doneW;
	simDoneRect = { dx, midY - doneH / 2, doneW, doneH };
	fillRound(dx, midY - doneH / 2, doneW, doneH, 9, faint);
	strokeRound(dx, midY - doneH / 2, doneW, doneH, 9, Color(1, 1, 1, 0.10f));
	text(dx + 13, midY - 5, "Done", 12.0f, ink);
	text(dx + 13 + measuredTextWidth("Done", 12.0f) + 7, midY - 3, "esc", 9.0f, dim);

	// Live readout of every switch (IN) and light (OUT) as small chips,
	// ordered top-to-bottom, left-to-right like the drawing, in whatever
	// room is left between the controls and Done.
	struct Chip { float y, x; bool lit; };
	std::vector<Chip> ins, outs;
	for (auto& ge : gateList) {
		guiGate* g = ge.second;
		const bool isIn = dynamic_cast<guiGateTOGGLE*>(g) != nullptr;
		const bool isOut = dynamic_cast<guiGateLED*>(g) != nullptr;
		if (!isIn && !isOut) continue;
		bool lit = false;
		for (auto& hs : g->getHotspotList()) {
			if (!g->isConnected(hs.first)) continue;
			const std::vector<StateType>& st = g->getConnection(hs.first)->getState();
			lit = !st.empty() && st[0] == ONE;
			break;
		}
		if (isIn && !lit) lit = g->getLogicParam("OUTPUT_NUM") == "1";
		float gx, gy;
		g->getGLcoords(gx, gy);
		(isIn ? ins : outs).push_back({ gy, gx, lit });
	}
	auto byPlace = [](const Chip& a, const Chip& b) { return a.y != b.y ? a.y > b.y : a.x < b.x; };
	std::sort(ins.begin(), ins.end(), byPlace);
	std::sort(outs.begin(), outs.end(), byPlace);
	const float chip = 10.0f, gap = 5.0f;
	float room = dx - 16 - x;
	auto chipRow = [&](const char* label, const std::vector<Chip>& chips) {
		if (chips.empty() || room < 60) return;
		const float lw = measuredTextWidth(label, 9.0f) + 8;
		int fit = (int)((room - lw) / (chip + gap));
		if (fit <= 0) return;
		const int n = std::min<int>(fit, (int)chips.size());
		text(x, midY - 4, label, 9.0f, dim);
		float cx = x + lw;
		for (int i = 0; i < n; i++) {
			const bool lit = chips[i].lit;
			if (lit) fillRound(cx - 3, midY - chip / 2 - 3, chip + 6, chip + 6, 5, Color(on.r, on.g, on.b, 0.16f));
			fillRound(cx, midY - chip / 2, chip, chip, 3, lit ? on : Color(1, 1, 1, 0.10f));
			cx += chip + gap;
		}
		const float used = (cx - x) + 14;
		x += used;
		room -= used;
	};
	chipRow("IN", ins);
	chipRow("OUT", outs);
}

void GUICanvas::drawEmptyHintInto(cl::render::Scene& scene, const cl::render::RenderStyle& style,
                                  const cl::render::Transform& screenT, float logicalW, float logicalH) {
	using namespace cl::render;
	if (!gateList.empty() || !wireList.empty()) return;
	scene.setViewport(screenT);
	const char* msg = "Drag a gate here to start";
	const float textPx = 16.0f;
	const float textW = measuredTextWidth(msg, textPx);
	const float p = appearProgress();
	const float a = 0.22f * p;
	const Color c = style.darkMode ? Color(1.0f, 1.0f, 1.0f, a) : Color(0.0f, 0.0f, 0.0f, a);
	// text()'s origin is the top of the capitals, in the Y-UP space screenT
	// establishes -- convert from the top-down logical position we actually
	// want (vertical center, nudged up half a cap-height so the glyphs
	// themselves sit centered rather than their top edge).
	const float topDownY = logicalH * 0.5f - textPx * 0.5f + (1.0f - p) * 14.0f;   // drifts up into place
	scene.text(Point(logicalW * 0.5f - textW * 0.5f, logicalH - topDownY), msg, textPx, c);
}
#endif

void GUICanvas::mouseLeftDown(wxMouseEvent& event) {
	if (tidy.active) finishTidy(true);   // clicking on keeps the preview
	// In a split, clicking a pane is how you say which side you are working
	// in -- the menus, toolbar and keyboard all follow the focused canvas.
	if (MainFrame* mf = wxGetApp().mainframe)
		if (mf->IsSplit()) mf->FocusCanvas(this);

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
	if (renderMode().simView) {
		// No editing here: the bar's controls, clickable parts (switches,
		// keypads), and otherwise drag to pan.
		const wxPoint sp = event.GetPosition();
		if (simBarRect.contains((float)sp.x, (float)sp.y)) { simBarClick((float)sp.x, (float)sp.y); return; }
		const GLPoint2f wm = getMouseCoords();
		for (auto& ge : gateList) {
			if (klsMessage::Message_SET_GATE_PARAM* msg = ge.second->checkClick(wm.x, wm.y)) {
				gCircuit->sendMessageToCore(klsMessage::Message(klsMessage::MT_SET_GATE_PARAM, msg));
				return;
			}
		}
		currentDragState = DRAG_PAN;
		beginDrag(BUTTON_MIDDLE);
		SetCursor(wxCursor(wxCURSOR_HAND));
		return;
	}
	if (spaceHeld && currentDragState == DRAG_NONE && !isWithinPaste) {
		// Same pan machinery as Cmd+drag (DRAG_PAN, ended in OnMouseUp).
		spacePanned = true;
		currentDragState = DRAG_PAN;
		beginDrag(BUTTON_MIDDLE);
		SetCursor(wxCursor(wxCURSOR_HAND));
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
	if (tidy.active) finishTidy(true);
	GLPoint2f m = getMouseCoords();
	vector < unsigned long >::iterator sGate;

	if (renderMode().simView) return;
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

	// Remember the selection -- right-clicking a wire that's part of it acts
	// on the whole selection -- then clear it as right-click always has.
	std::vector<unsigned long> selGates, selWires;
	for (auto& g : gateList) if (g.second->isSelected()) selGates.push_back(g.first);
	for (auto& w : wireList) if (w.second->isSelected()) selWires.push_back(w.first);
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
		// Part of a selection with other things in it: the menu acts on all of it.
		const bool inSelection = std::find(selWires.begin(), selWires.end(), menuWire->getID()) != selWires.end()
		                         && selWires.size() + selGates.size() > 1;
		std::vector<unsigned long> targets = inSelection ? selWires : std::vector<unsigned long>{ static_cast<unsigned long>(menuWire->getID()) };
		if (inSelection) {
			for (unsigned long id : selGates) if (guiGate* g = getGate(id)) g->select();
			for (unsigned long id : selWires) if (guiWire* w = getWire(id)) w->select();
		} else {
			menuWire->select();
		}
		Refresh();
		enum { ID_CTX_DELETE_WIRE = 7000, ID_CTX_STRAIGHTEN = 7003 };
		wxMenu menu;
		menu.Append(ID_CTX_STRAIGHTEN, targets.size() > 1
			? wxString::Format("Straighten %zu Wires", targets.size()) : wxString("Straighten Route"));
		menu.AppendSeparator();
		menu.Append(ID_CTX_DELETE_WIRE, inSelection ? "Delete Selection" : "Delete Wire");
		const int chosen = GetPopupMenuSelectionFromUser(menu, event.GetPosition());
		if (chosen == ID_CTX_DELETE_WIRE) {
			if (inSelection) submitCommand( new cmdDeleteSelection( gCircuit, this, selGates, selWires ) );
			else submitCommand( new cmdDeleteWire( gCircuit, this, menuWire->getID() ) );
		}
		else if (chosen == ID_CTX_STRAIGHTEN) {
			straightenWires(targets);
		}
		else if (!inSelection) unselectAllWires();
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
	if (renderMode().simView) {
		if (simSpeedDragging) {
			simSetSpeedFromX((float)ScreenToClient(wxGetMousePosition()).x);
			Refresh();
		}
		return;   // no hover highlights or editing in Simulation View
	}

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
	if (renderMode().simView) {
		simSpeedDragging = false;
		currentDragState = DRAG_NONE;
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
								else if (currentDragState == DRAG_NEWGATE && creategatecommand != nullptr) creategatecommand->getConnections()->push_back(std::unique_ptr<klsCommand>(createwire));
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
	if (tidy.active) {
		switch (event.GetKeyCode()) {
		case WXK_RETURN: case WXK_NUMPAD_ENTER: finishTidy(true); return;
		case WXK_ESCAPE: finishTidy(false); return;
		case WXK_TAB: { const int other = 1 - tidy.mode; finishTidy(false); startTidy(other); return; }
		default: {
			// A modifier on its own doesn't count. (Not case labels: off the Mac,
			// WXK_RAW_CONTROL is WXK_CONTROL, and a switch can't list it twice.)
			const int k = event.GetKeyCode();
			if (k == WXK_SHIFT || k == WXK_CONTROL || k == WXK_ALT || k == WXK_RAW_CONTROL) { event.Skip(); return; }
			finishTidy(true);   // anything else keeps it, then carries on
			break;
		}
		}
	}
	if (renderMode().simView) {
		MainFrame* mf = wxGetApp().mainframe;
		switch (event.GetKeyCode()) {
		case WXK_ESCAPE: if (mf) mf->SetSimView(false); break;
		case WXK_SPACE:  if (mf) mf->SetSimPaused(!mf->IsSimPaused()); break;
		case WXK_RIGHT: case WXK_NUMPAD_RIGHT: translatePan(+PAN_STEP * getZoom(), 0.0); break;
		case 43: case 61: case WXK_NUMPAD_ADD: zoomIn(); break;
		case 45: case WXK_NUMPAD_SUBTRACT:     zoomOut(); break;
		case WXK_LEFT: case WXK_NUMPAD_LEFT:   translatePan(-PAN_STEP * getZoom(), 0.0); break;
		case WXK_UP: case WXK_NUMPAD_UP:       translatePan(0.0, PAN_STEP * getZoom()); break;
		case WXK_DOWN: case WXK_NUMPAD_DOWN:   translatePan(0.0, -PAN_STEP * getZoom()); break;
		default: break;
		}
		return;
	}
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
	case WXK_RIGHT:
	case WXK_NUMPAD_RIGHT:
	case WXK_UP:
	case WXK_NUMPAD_UP:
	case WXK_DOWN:
	case WXK_NUMPAD_DOWN: {
		const int k = event.GetKeyCode();
		const float sx = (k == WXK_LEFT || k == WXK_NUMPAD_LEFT) ? -1.0f
		               : (k == WXK_RIGHT || k == WXK_NUMPAD_RIGHT) ? 1.0f : 0.0f;
		const float sy = (k == WXK_UP || k == WXK_NUMPAD_UP) ? 1.0f
		               : (k == WXK_DOWN || k == WXK_NUMPAD_DOWN) ? -1.0f : 0.0f;
		// With something selected, arrows nudge it a grid square (Shift: 5).
		// Otherwise they pan, as before.
		const float step = horizSpacing * (event.ShiftDown() ? 5.0f : 1.0f);
		if (!nudgeSelection(sx * step, sy * step))
			translatePan(sx * PAN_STEP * getZoom(), sy * PAN_STEP * getZoom());
		break;
	}
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
		// Tap: zoom to fit (on release, in OnKeyUp). Hold + drag: pan.
		if (!spaceHeld) {
			spaceHeld = true;
			spacePanned = false;
			if (currentDragState == DRAG_NONE) SetCursor(wxCursor(wxCURSOR_HAND));
		}
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
		// Mid-drag this connects a gate to whatever is unambiguously nearby,
		// which is the whole point of having it on a bare key -- you press it
		// without letting go of the mouse. With nothing being dragged there is
		// nothing to connect, so it copies instead, the way C reads everywhere
		// else. (Cmd+C never reaches here: MainFrame's CHAR_HOOK claims it.)
		if (!event.ControlDown() && !event.AltDown() && !event.CmdDown()) {
			if (currentDragState == DRAG_NONE) {
				copyBlockToClipboard();
			} else if (!this->isLocked()) {
				if (currentDragState == DRAG_NEWGATE) commitNewDragGate();
				connectNearbyHotspots();
			}
		}
		break;
	case 'D':
	case 'd':
		if (bareKey(event) && !this->isLocked()) duplicateSelection();
		break;
	case 'V':
	case 'v':
		if (bareKey(event) && !this->isLocked()) pasteBlockFromClipboard();
		break;
	case 'X':
	case 'x':
		if (bareKey(event) && !this->isLocked()) cutSelectionToClipboard();
		break;
	case 'S':
	case 's':
		// Straighten whatever wires are selected.
		if (bareKey(event) && !this->isLocked() && event.ShiftDown()) {
			startTidy(appConfig().appSettings.tidyMode);
		} else if (bareKey(event) && !this->isLocked()) {
			std::vector<unsigned long> ids = selectedWireIds();
			if (ids.empty()) ids = wiresOfSelectedGates();
			if (!ids.empty()) straightenWires(ids);
		}
		break;
	case 'T':
	case 't':
		if (bareKey(event)) {
			wxCommandEvent evt(wxEVT_MENU, View_TruthTable);
			if (MainFrame* mf = wxGetApp().mainframe) mf->ProcessWindowEvent(evt);
		}
		break;
	}
}

// A letter pressed on its own: no Cmd, Ctrl or Option. Shift is allowed --
// it makes no difference to these, and capitals arrive with it held.
bool GUICanvas::bareKey(const wxKeyEvent& event) const {
	return !event.ControlDown() && !event.AltDown() && !event.CmdDown() &&
	       currentDragState == DRAG_NONE && !isWithinPaste;
}

void GUICanvas::OnKeyUp(wxKeyEvent& event) {
	if (event.GetKeyCode() == WXK_SPACE && spaceHeld) {
		spaceHeld = false;
		if (currentDragState != DRAG_PAN) SetCursor(wxCursor(wxCURSOR_ARROW));
		if (!spacePanned && currentDragState == DRAG_NONE) setZoomAll();
		return;
	}
	event.Skip();
}

bool GUICanvas::nudgeSelection(float dx, float dy) {
	if (currentDragState != DRAG_NONE || isLocked() || isWithinPaste) return false;
	vector<GateState> moved;
	vector<WireState> movedWires;
	for (auto& g : gateList) {
		if (!g.second->isSelected()) continue;
		float x, y;
		g.second->getGLcoords(x, y);
		moved.push_back(GateState(g.first, x, y, true));
	}
	if (moved.empty()) return false;
	for (auto& w : wireList)
		if (w.second->isSelected())
			movedWires.push_back(WireState(w.first, w.second->getCenter(), w.second->getSegmentMap()));

	// Same steps as a mouse drag (OnMouseMove) and its drop (OnMouseUp).
	const GLPoint2f delta(dx, dy);
	for (auto& ws : movedWires) if (guiWire* w = getWire(ws.id)) w->move(ws.point, delta);
	for (auto& gs : moved) if (guiGate* g = getGate(gs.id)) g->setGLcoords(gs.x + dx, gs.y + dy);
	cmdMoveSelection* mc = new cmdMoveSelection(gCircuit, moved, movedWires,
		moved[0].x, moved[0].y, moved[0].x + dx, moved[0].y + dy);
	for (auto& gs : moved) if (guiGate* g = getGate(gs.id)) g->updateConnectionMerges();
	submitCommand(mc);
	mc->Undo();   // see OnMouseUp: the gates already moved, so cancel Submit's extra Do()
	collisionChecker.update();
	Refresh();
	return true;
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
	startPaste( myClipboard.pasteBlock( gCircuit, this ) );
}

void GUICanvas::duplicateSelection() {
	if (this->isLocked() || currentDragState != DRAG_NONE || isWithinPaste) return;
	// The selection vectors can lag the selection flags; read the flags.
	vector<unsigned long> gates, wires;
	for (auto& g : gateList) if (g.second->isSelected()) gates.push_back(g.first);
	for (auto& w : wireList) if (w.second->isSelected()) wires.push_back(w.first);
	if (gates.empty()) return;
	selectedGates = gates;
	selectedWires = wires;
	if (appConfig().appSettings.duplicateUsesClipboard) {
		copyBlockToClipboard();
		pasteBlockFromClipboard();
		return;
	}
	klsClipboard cb;
	const string text = cb.serializeBlock( gCircuit, this, gates, wires );
	if (!text.empty()) startPaste( cb.pasteText( gCircuit, this, text, false ) );
}

void GUICanvas::selectAll() {
	if (currentDragState != DRAG_NONE || isWithinPaste) return;
	selectedGates.clear();
	selectedWires.clear();
	for (auto& g : gateList) if (g.second) { g.second->select(); selectedGates.push_back(g.first); }
	for (auto& w : wireList) if (w.second) { w.second->select(); selectedWires.push_back(w.first); }
	markSelectionChanged();
	Refresh();
}

// The pasted gates follow the mouse until the next click drops them.
void GUICanvas::startPaste( cmdPasteBlock* cmd ) {
	pasteCommand = cmd;
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
	// Drag by the gate nearest the top-left corner. gateList is unordered, so
	// ties go to the lower id -- otherwise the anchor (and where the block
	// lands under the mouse) could change from one paste to the next.
	double minMagnitude = 0.0;
	thisGate = gateList.begin();
	while (thisGate != gateList.end()) {
		if ((thisGate->second)->isSelected()) {
			GLPoint2f temp;
			(thisGate->second)->getGLcoords(temp.x, temp.y);
			float diffx = temp.x - minPoint.x, diffy = temp.y - minPoint.y;
			double mag = (diffx * diffx) + (diffy * diffy);
			if (!ref || mag < minMagnitude || (mag == minMagnitude && thisGate->first < snapToGateID)) {
				minMagnitude = mag;
				gatecoord = temp;
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

// Straighten every wire in `ids` as one undo step. Shared by the right-click
// menu and the S key.
void GUICanvas::straightenWires(const std::vector<unsigned long>& ids) {
	std::vector<klsCommand*> steps = routeWiresTogether(ids);
	if (steps.size() == 1) submitCommand(steps[0]);
	else if (!steps.empty()) submitCommand( new cmdPasteBlock( steps, "Straighten Wires" ) );
	collisionChecker.update();
	Refresh();
}

namespace {
// Which way a pin leaves its gate: out through the side of the body it sits on,
// along the axis the gate declares for it.
void pinExit(guiGate* g, const klsBBox& body, const std::string& hs, float x, float y, int& dx, int& dy) {
	const float e = 1e-3f;
	dx = dy = 0;
	klsBBox b = body;
	const bool vert = g->isVerticalHotspot(hs);
	const bool L = x <= b.getLeft() + e, R = x >= b.getRight() - e;
	const bool T = y >= b.getTop() - e, B = y <= b.getBottom() + e;
	if (vert) { if (T && !B) dy = 1; else if (B && !T) dy = -1; }
	else      { if (L && !R) dx = -1; else if (R && !L) dx = 1; }
	if (dx != 0 || dy != 0) return;
	if (L && !R) dx = -1; else if (R && !L) dx = 1;
	else if (T && !B) dy = 1; else if (B && !T) dy = -1;
	else if (vert) dy = y >= (b.getTop() + b.getBottom()) / 2 ? 1 : -1;
	else dx = x >= (b.getLeft() + b.getRight()) / 2 ? 1 : -1;
}

// A gate's footprint: its body plus the pins sticking out of it.
klsBBox gateBody(guiGate* g) {
	klsBBox body = g->getSelectionBBox();
	if (body.empty()) body = g->getBBox();
	return body;
}
void gateFootprint(guiGate* g, const klsBBox& body, float& l, float& b, float& r, float& t) {
	klsBBox bb = body;
	l = bb.getLeft(); r = bb.getRight(); b = bb.getBottom(); t = bb.getTop();
	for (const auto& hs : g->getHotspotList()) {
		l = std::min(l, hs.second.x); r = std::max(r, hs.second.x);
		b = std::min(b, hs.second.y); t = std::max(t, hs.second.y);
	}
}
}

// Lay out `ids` together with the page-wide router (route/GridRouter.h): every
// gate is in the way and every other wire stays put. A wire it can't route gets
// the old one-wire straighten. Returns one before/after step per wire, already
// applied.
std::vector<klsCommand*> GUICanvas::routeWiresTogether(const std::vector<unsigned long>& ids) {
	std::vector<klsCommand*> steps;
	for (WireReshape& w : rerouteWires(ids))
		steps.push_back( new cmdWireSegDrag( gCircuit, this, w.id, w.before, w.after ) );
	return steps;
}

std::vector<WireReshape> GUICanvas::rerouteWires(const std::vector<unsigned long>& ids,
                                                 const std::set<unsigned long>* movedGates) {
	using namespace cl::route;
	std::vector<WireReshape> steps;
	std::vector<guiWire*> wires;
	std::vector<unsigned long> wireIds;
	std::set<unsigned long> routing;
	for (unsigned long id : ids) {
		guiWire* w = getWire(id);
		if (w == nullptr || w->getConnections().size() < 2 || !routing.insert(id).second) continue;
		bool gatesOk = true;
		for (const wireConnection& c : w->getConnections()) if (getGate(c.gid) == nullptr) gatesOk = false;
		if (!gatesOk) continue;
		wires.push_back(w);
		wireIds.push_back(id);
	}
	if (wires.empty()) return steps;

	GridInput in;
	// Gate footprints: the body plus the pins sticking out of it.
	std::map<unsigned long, klsBBox> bodies;
	for (auto& ge : gateList) {
		guiGate* g = ge.second;
		if (g == nullptr) continue;
		klsBBox body = gateBody(g);
		if (body.empty()) continue;
		bodies[ge.first] = body;
		GridRect r;
		gateFootprint(g, body, r.l, r.b, r.r, r.t);
		in.obstacles.push_back(r);
	}
	std::set<std::pair<unsigned long, std::string>> usedPins;
	for (guiWire* w : wires) {
		GridNet net;
		net.root = -1;
		const std::vector<wireConnection> conns = w->getConnections();
		for (size_t i = 0; i < conns.size(); i++) {
			guiGate* g = getGate(conns[i].gid);
			GridPin p;
			g->getHotspotCoords(conns[i].connection, p.x, p.y);
			auto body = bodies.find(conns[i].gid);
			if (body != bodies.end()) pinExit(g, body->second, conns[i].connection, p.x, p.y, p.dx, p.dy);
			net.pins.push_back(p);
			usedPins.insert({ conns[i].gid, conns[i].connection });
			if (net.root < 0 && !g->isConnectionInput(conns[i].connection)) net.root = (int)i;
		}
		if (net.root < 0) net.root = 0;
		in.nets.push_back(net);
	}
	for (auto& ge : gateList) {
		guiGate* g = ge.second;
		auto body = bodies.find(ge.first);
		if (g == nullptr || body == bodies.end()) continue;
		for (const auto& hs : g->getHotspotList()) {
			if (usedPins.count({ ge.first, hs.first })) continue;
			GridPin p;
			p.x = hs.second.x; p.y = hs.second.y;
			pinExit(g, body->second, hs.first, p.x, p.y, p.dx, p.dy);
			in.foreignPins.push_back(p);
		}
	}
	for (auto& we : wireList) {
		if (we.second == nullptr || routing.count(we.first)) continue;
		for (const auto& seg : we.second->getSegmentMap()) {
			GridFixedSeg f;
			f.bx = seg.second.begin.x; f.by = seg.second.begin.y;
			f.ex = seg.second.end.x;   f.ey = seg.second.end.y;
			in.fixed.push_back(f);
		}
	}

	const GridOutput out = routeGrid(in);
	for (size_t k = 0; k < wires.size(); k++) {
		guiWire* w = wires[k];
		const auto before = w->getSegmentMap();
		bool routed = false;
		if (out.ok[k]) {
			w->adoptRoute(out.routes[k]);
			routed = !w->getSegmentMap().empty();
			if (!routed) w->setSegmentMap(before);
		}
		if (!routed) straightenWireAvoiding(w);
		// Never trade a wire for a much longer one: a shape drawn to run along
		// packed parts (a 7-segment display) would otherwise be sent the long
		// way round. Only when its gates stayed put -- if they moved, the old
		// shape no longer fits anyway.
		bool gatesMoved = false;
		if (movedGates != nullptr)
			for (const wireConnection& c : w->getConnections()) if (movedGates->count(c.gid)) gatesMoved = true;
		if (!gatesMoved) {
			auto length = [](const std::map<long, wireSegment>& m) {
				float len = 0.0f;
				for (const auto& seg : m) len += std::fabs(seg.second.end.x - seg.second.begin.x) + std::fabs(seg.second.end.y - seg.second.begin.y);
				return len;
			};
			const float was = length(before), now = length(w->getSegmentMap());
			if (now > was * 1.5f + 3.0f) w->setSegmentMap(before);
		}
		WireReshape r;
		r.id = wireIds[k];
		r.before = before;
		r.after = w->getSegmentMap();
		steps.push_back(std::move(r));
	}
	return steps;
}

void GUICanvas::straightenAll() {
	std::vector<unsigned long> ids;
	for (const auto& w : wireList) if (w.second != nullptr) ids.push_back(w.first);
	std::sort(ids.begin(), ids.end());
	straightenWires(ids);
}

// Every wire attached to a selected gate.
std::vector<unsigned long> GUICanvas::wiresOfSelectedGates() const {
	std::set<unsigned long> ids;
	for (const auto& g : gateList) {
		if (g.second == nullptr || !g.second->isSelected()) continue;
		for (const auto& c : g.second->getConnections()) if (c.second != nullptr) ids.insert(c.second->getID());
	}
	return std::vector<unsigned long>(ids.begin(), ids.end());
}

// Every wire currently selected.
std::vector<unsigned long> GUICanvas::selectedWireIds() const {
	std::vector<unsigned long> ids;
	for (const auto& w : wireList)
		if (w.second != nullptr && w.second->isSelected()) ids.push_back(w.first);
	return ids;
}

void GUICanvas::straightenWireAvoiding(guiWire* wire) {
	// Every other wire's segments, gathered once.
	std::vector<wireSegment> others;
	for (auto& we : wireList)
		if (we.second != wire)
			for (const auto& s : we.second->getSegmentMap()) others.push_back(s.second);

	// How crowded this wire is: length it runs alongside other wires, weighted
	// by closeness -- fully on top counts in full, fading to nothing at
	// CLEARANCE apart -- so wires end up with real space between them, not
	// just technically not touching. Crossings don't count; those are readable.
	const float CLEARANCE = 2.0f;   // world units (4 grid squares)
	auto overlap = [&]() {
		float total = 0.0f;
		for (const auto& me : wire->getSegmentMap()) {
			const wireSegment& a = me.second;
			const bool ah = a.isHorizontal();
			for (const wireSegment& b : others) {
				if (b.isHorizontal() != ah) continue;
				const float gap = ah ? std::fabs(a.begin.y - b.begin.y) : std::fabs(a.begin.x - b.begin.x);
				if (gap >= CLEARANCE) continue;
				const float along = ah
					? std::min(a.end.x, b.end.x) - std::max(a.begin.x, b.begin.x)
					: std::min(a.end.y, b.end.y) - std::max(a.begin.y, b.begin.y);
				if (along > 0.0f) total += along * (1.0f - gap / CLEARANCE);
			}
		}
		return total;
	};

	wire->straightenRoute();
	float best = overlap();
	float pos, lo, hi;
	// Only a two-pin wire has one trunk to slide; a wire with more pins is
	// left on its fresh route.
	if (best <= 1e-3f || wire->getConnections().size() != 2 || !wire->trunkRange(pos, lo, hi)) return;

	// Nearest grid positions first, alternating sides, strictly between the
	// outermost pins so every branch keeps a real length.
	float bestPos = pos;
	const float step = 0.5f;
	for (int k = 1; k * step < (hi - lo); k++) {
		for (int side = 1; side >= -1; side -= 2) {
			const float p = pos + side * k * step;
			if (p <= lo + 1e-3f || p >= hi - 1e-3f) continue;
			wire->routeWithTrunkAt(p);
			const float o = overlap();
			if (o < best - 1e-3f) { best = o; bestPos = p; }
			if (best <= 1e-3f) return;
		}
	}
	wire->routeWithTrunkAt(bestPos);
}

void GUICanvas::startTidy(int mode, bool preview) {
	if (tidy.active) finishTidy(false);
	if (isLocked() || currentDragState != DRAG_NONE || isWithinPaste) return;
	using namespace cl::route;

	// The selection, or the whole page when nothing is selected. Every gate
	// goes in (the rest as fixed neighbors to line up with).
	std::vector<unsigned long> ids;
	bool anySelected = false;
	for (auto& ge : gateList) {
		if (ge.second == nullptr) continue;
		ids.push_back(ge.first);
		if (ge.second->isSelected()) anySelected = true;
	}
	std::sort(ids.begin(), ids.end());
	if (ids.empty()) return;

	LayoutInput in;
	in.mode = mode == 1 ? TidyMode::Rearrange : TidyMode::KeepLayout;
	std::map<guiWire*, int> netOf;
	std::vector<unsigned long> nodeGate;
	for (unsigned long id : ids) {
		guiGate* g = getGate(id);
		const klsBBox body = gateBody(g);
		if (klsBBox(body).empty()) continue;
		LayoutNode node;
		gateFootprint(g, body, node.l, node.b, node.r, node.t);
		node.movable = !anySelected || g->isSelected();
		node.indicator = g->getLibraryGateName().find("LED") != std::string::npos;
		for (const auto& hs : g->getHotspotList()) {
			LayoutPin p;
			p.x = hs.second.x; p.y = hs.second.y;
			pinExit(g, body, hs.first, p.x, p.y, p.dx, p.dy);
			if (guiWire* w = g->getConnection(hs.first)) {
				auto it = netOf.find(w);
				if (it == netOf.end()) it = netOf.insert({ w, (int)netOf.size() }).first;
				p.net = it->second;
			}
			p.output = !g->isConnectionInput(hs.first);
			node.pins.push_back(p);
		}
		in.nodes.push_back(node);
		nodeGate.push_back(id);
	}
	const std::vector<std::pair<float, float>> offsets = layoutGates(in);

	TidyState t;
	t.mode = mode;
	std::set<unsigned long> wireIds;
	for (size_t i = 0; i < nodeGate.size(); i++) {
		guiGate* g = getGate(nodeGate[i]);
		if (!in.nodes[i].movable) continue;
		for (const auto& c : g->getConnections()) if (c.second != nullptr) wireIds.insert(c.second->getID());
		if (offsets[i].first == 0.0f && offsets[i].second == 0.0f) continue;
		cmdTidy::GateMove m;
		m.id = nodeGate[i];
		g->getGLcoords(m.fromX, m.fromY);
		m.toX = m.fromX + offsets[i].first;
		m.toY = m.fromY + offsets[i].second;
		const LayoutNode& n = in.nodes[i];
		t.ghostRects.insert(t.ghostRects.end(), { n.l, n.b, n.r, n.t });
		// Move without dragging the wires along; they're rerouted next.
		g->setGLcoords(m.toX, m.toY, true);
		t.moves.push_back(m);
	}
	std::set<unsigned long> moved;
	for (const cmdTidy::GateMove& m : t.moves) moved.insert(m.id);
	t.wires = rerouteWires(std::vector<unsigned long>(wireIds.begin(), wireIds.end()), &moved);
	for (const WireReshape& w : t.wires)
		for (const auto& seg : w.before)
			t.ghostLines.insert(t.ghostLines.end(),
				{ seg.second.begin.x, seg.second.begin.y, seg.second.end.x, seg.second.end.y });
	collisionChecker.update();
	if (t.moves.empty() && t.wires.empty()) { Refresh(); return; }

	if (!preview) {
		submitCommand( new cmdTidy( gCircuit, this, t.moves, t.wires ) );
		Refresh();
		return;
	}
	t.active = true;
	tidy = std::move(t);
	SetFocus();
	Refresh();
}

void GUICanvas::finishTidy(bool keep) {
	if (!tidy.active) return;
	TidyState t = std::move(tidy);
	tidy = TidyState();
	if (keep) {
		submitCommand( new cmdTidy( gCircuit, this, t.moves, t.wires ) );
	} else {
		// Gates back first, so each wire's old shape is trimmed to where its
		// pins really are.
		for (const cmdTidy::GateMove& m : t.moves)
			if (guiGate* g = getGate(m.id)) g->setGLcoords(m.fromX, m.fromY, true);
		for (const WireReshape& w : t.wires)
			if (guiWire* wire = getWire(w.id)) wire->setSegmentMap(w.before);
		collisionChecker.update();
	}
	Refresh();
}

void GUICanvas::drawTidyBannerInto(cl::render::Scene& scene, const cl::render::Transform& screenT,
                                   float W, float H) {
	using cl::render::Point;
	using cl::render::Color;
	using cl::render::measuredTextWidth;
	scene.setViewport(screenT);
	auto P = [H](float x, float y) { return Point(x, H - y); };
	const char* lead = "Tidy Up preview";
	const char* rest = tidy.mode == 1
		? "Return keeps it  \u00b7  Esc puts it back  \u00b7  Tab: keep my layout instead"
		: "Return keeps it  \u00b7  Esc puts it back  \u00b7  Tab: full rearrange instead";
	const float px = 13.0f, pad = 14.0f, gapW = 12.0f;
	const float wLead = measuredTextWidth(lead, px), wRest = measuredTextWidth(rest, px);
	const float w = pad + wLead + gapW + wRest + pad, h = 32.0f;
	const float x = std::max(8.0f, (W - w) / 2.0f), y = 12.0f;
	// A dark pill in both themes, like a HUD over the canvas.
	std::vector<Point> pts;
	const float r = h / 2.0f;
	const float cx[2] = { x + w - r, x + r };
	for (int c = 0; c < 2; c++)
		for (int i = 0; i <= 12; i++) {
			const float a = (-90.0f + 180.0f * c + 15.0f * i) * 3.14159265f / 180.0f;
			pts.push_back(P(cx[c] + r * std::cos(a), y + r + r * std::sin(a)));
		}
	scene.fillPolygon(&pts[0], pts.size(), Color(0.10f, 0.11f, 0.13f, 0.94f));
	const float ty = y + (h - px) / 2.0f;
	scene.text(P(x + pad, ty), lead, px, Color(1.0f, 1.0f, 1.0f, 1.0f));
	scene.text(P(x + pad + wLead + gapW, ty), rest, px, Color(1.0f, 1.0f, 1.0f, 0.68f));
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
