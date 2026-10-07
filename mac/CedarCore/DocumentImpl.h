// The document behind the C interface, shared by the files that implement it
// (Document.cpp, Editor.cpp). Not part of the interface Swift sees.

#ifndef CL_MAC_DOCUMENTIMPL_H
#define CL_MAC_DOCUMENTIMPL_H

#include "CedarCore.h"
#include "CanvasTypes.h"
#include "CircuitEdits.h"
#include "GUICanvas.h"
#include "GUICircuit.h"
#include "LogicHost.h"
#include "klsBBox.h"
#include "render/RenderStyle.h"
#include "render/Scene.h"
#include "circuit_file.hpp"

#include <memory>
#include <string>
#include <vector>

// A drag in progress on the canvas (see Editor.cpp).
struct EditGesture {
	enum Mode { None, Pressed, Moving, BoxSelect, Connect, WireSeg } mode = None;
	// Connect: from this gate's pin; sticky once a click (no drag) started it,
	// so the line follows the pointer until the next click.
	unsigned long srcGate = 0;
	std::string srcPin;
	bool sticky = false;
	// Moving: a paste or new gate following the pointer (no button down)
	// until a click drops it.
	bool floating = false;
	float unitsPerPoint = 0.05f;
	GLPoint2f current;
	// WireSeg: the wire whose segment is being dragged.
	unsigned long wire = 0;
	bool moved = false;
	float hoverDelta = 0.25f;
	int page = 0;
	GLPoint2f start;
	unsigned long gate = 0;       // pressed on this gate (when onGate)
	bool onGate = false;
	bool toggledOff = false;      // a shift-click that deselected: no drag, no click
	float dragSlop = 0.3f;        // world units the pointer may wander before it's a drag
	std::vector<GateState> preMove;
	std::vector<WireState> preMoveWire;
	GLPoint2f lastDelta;
	klsBBox box;
	// Moving: connections C made mid-move (wx pendingConnects). Applied at
	// once, but held off the undo stack until the drop, where they go above
	// the move; Escape takes back just these. Owned (see Editor.cpp).
	std::vector<klsCommand*> pendingConnects;
};

// Drawing on the circuit (Ink.cpp): the stroke being drawn, the eraser's
// drag and what the pointer would draw.
struct InkGesture {
	bool active = false;
	int page = -1;
	int tool = 0;               // CL_INK_PEN / CL_INK_HIGHLIGHTER
	std::string color = "ink";
	double width = 0.25;        // world units
	double unitsPerPoint = 0.05;
	bool pressure = false;      // this stroke keeps pressure (a pen's tablet samples)
	double lastPressure = 1;
	std::vector<double> xy;     // the part being captured: world samples
	std::vector<double> pr;     // their pressure (when `pressure`)
	std::vector<cl::InkStroke> parts;   // parts already split off (2,000 samples each)
	bool full = false;          // a cap was reached: no more samples taken
};
struct InkErase {
	bool active = false;
	int page = -1;
	bool haveLast = false;
	double lastX = 0, lastY = 0;
	std::vector<cl::InkStroke> before;   // the page's strokes when the drag began
	std::vector<bool> gone;              // parallel to `before`
};
struct InkHover {
	bool on = false;
	int page = -1;
	double x = 0, y = 0;
	int tool = 0;
	std::string color = "ink";
	double width = 0.25;
};

struct CLDocument {
	GUICircuit circuit;
	std::unique_ptr<LogicHost> sim;
	std::vector<std::unique_ptr<GUICanvas>> pages;   // destroyed before the circuit
	std::vector<std::string> notices;
	std::vector<bool> noticeWarnings;
	bool running = true;
	int stepMs = 25;
	double carryMs = 0;

	CLDocument() : sim(new LogicHost(circuit)) {
		registerLogicHost(&circuit, sim.get());
		sim->afterStep = [this] { recordScope(); };
	}
	~CLDocument() {
		// History first: a closed page is kept by its command (Extras.cpp),
		// and goes with it while the circuit is still whole.
		circuit.GetCommandProcessor()->ClearCommands();
		pages.clear();
		registerLogicHost(&circuit, nullptr);
	}

	EditGesture gesture;
	// A Tidy Up on show: applied, not yet recorded (see Editor.cpp).
	struct TidyPreview {
		bool active = false;
		int mode = 0;
		int page = 0;
		edits::TidyPlan plan;
	} tidy;
	// The oscilloscope's recording: one sample per step for every signal a TO
	// label names (its JUNCTION_ID), from the wire on the label's pin -- what
	// the wx oscope samples. All traces have the same length; samples[i][0]
	// is step firstStep.
	struct Scope {
		std::vector<std::string> names;                 // sorted
		std::vector<std::vector<unsigned char>> samples; // parallel to names
		unsigned long long firstStep = 0;
		size_t length = 0;
		unsigned long gateVersion = (unsigned long)-1;
	} scope;
	void recordScope();

	// What's under the pointer, for the overlay: a pin (red box) or nothing.
	bool hoverPin = false;
	GLPoint2f hoverPinAt;
	int hoverPage = -1;   // the page the pin box belongs to
	unsigned long hoverWire = 0;   // the wire under the pointer, lit up whole
	int hoverWirePage = -1;
	bool edited = false;          // changed since opened or last saved
	bool lockedHeld = false;      // the last move, delete or rotate left locked parts put
	int pageToShow = -1;          // set when an undo or redo adds or removes a page
	InkGesture ink;
	InkErase inkErase;
	InkHover inkHover;
	int inkHistoryPage = -1;      // set when an undo or redo changes a page's drawing
	std::string notesScratch;     // cl_notes's text

	GUICanvas* page(int i) const {
		return (i >= 0 && i < (int)pages.size()) ? pages[i].get() : nullptr;
	}
};


// How the drawing on a page is coloured (Ink.cpp): the screen's light or
// dark colours, the colour-print ones, or black and white.
enum { kInkLookLight = 0, kInkLookDark = 1, kInkLookPrint = 2, kInkLookBW = 3 };

// Draw a page's wires and gates in a style (Document.cpp). The camera is
// cl_document_draw's. `inkMode` is CL_INK_* (whether the drawing goes on
// too, in its layers), `inkLook` its colours.
extern "C" void clDrawPage(CLDocument* doc, int page, CGContextRef ctx, double backingScale,
                           double originX, double originY, double unitsPerPoint,
                           const cl::render::RenderStyle& style, int inkMode, int inkLook);

// One layer of a page's drawing into a scene whose viewport is the page's:
// the highlighter strokes (under the parts) or the pen strokes (over them).
// `pixelsPerUnit` is device pixels per world unit (Ink.cpp).
void clDrawInkLayer(cl::render::Scene& scene, const cl::PageInk& ink, bool highlighter, int look,
                    float pixelsPerUnit, bool projector);
// Whether a page's drawing is drawn for an inkMode.
bool clInkDrawn(const CLDocument* doc, int inkMode);

// cl_document_open_text, settled or not whatever cl_set_settle_on_open says
// (Document.cpp): Check My Circuit opens its copies before a single step.
extern "C" CLDocument* clOpenText(const char* data, long length, char* error, int errorLen, bool settle);

// The circuit as cl_document_save_text writes it, without marking the
// document saved (Check My Circuit's copies).
std::string clSaveText(CLDocument* doc);

#endif  // CL_MAC_DOCUMENTIMPL_H
