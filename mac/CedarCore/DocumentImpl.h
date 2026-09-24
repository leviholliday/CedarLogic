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

	CLDocument() : sim(new LogicHost(circuit)) { registerLogicHost(&circuit, sim.get()); }
	~CLDocument() {
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
	// What's under the pointer, for the overlay: a pin (red box) or nothing.
	bool hoverPin = false;
	GLPoint2f hoverPinAt;
	bool edited = false;          // changed since opened or last saved

	GUICanvas* page(int i) const {
		return (i >= 0 && i < (int)pages.size()) ? pages[i].get() : nullptr;
	}
};


#endif  // CL_MAC_DOCUMENTIMPL_H
