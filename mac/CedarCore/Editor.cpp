// Editing a page in the native Mac front end. Each edit is made with the same
// command classes the wx canvas uses (src/gui/command), submitted to the
// circuit's undo stack, so an edit -- and its undo -- does exactly what it does
// in the wx app. The gesture logic follows GUICanvas::mouseLeftDown /
// OnMouseMove / OnMouseUp for the parts it covers.

#include "DocumentImpl.h"
#include "GateLibrary.h"
#include "guiGate.h"
#include "guiWire.h"
#include "klsClipboard.h"
#include "CGScene.h"
#include "CircuitEdits.h"
#include "render/Scene.h"
#include "command/cmdCreateGate.h"
#include "command/cmdDeleteWire.h"
#include "command/cmdDisconnectWire.h"
#include "command/cmdTidy.h"
#include "command/cmdWireSegDrag.h"
#include "command/cmdDeleteSelection.h"
#include "command/cmdMoveGate.h"
#include "command/cmdMoveSelection.h"
#include "command/cmdMoveWire.h"
#include "command/cmdPasteBlock.h"
#include "command/cmdSetParams.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <sstream>

namespace {

const float kGrid = 0.5f;                 // gates snap to half a grid square
const float kHoverPoints = 5.0f;          // how near a wire counts (WIRE_HOVER_SCREEN_DELTA)
const float kDragPoints = 6.0f;           // how far before a press is a drag (DRAG_START_SCREEN_DELTA)
const float kPinPoints = 5.0f;            // how near a pin counts (HOTSPOT_SCREEN_DELTA)
const float kPinBoxPoints = 3.0f;         // the pin highlight's half-size (HOTSPOT_SCREEN_RADIUS)

GLPoint2f snap(GLPoint2f p) {
	return GLPoint2f(kGrid * std::floor(p.x / kGrid + 0.5f), kGrid * std::floor(p.y / kGrid + 0.5f));
}

// Tag a command with its page, then run it and put it on the undo stack (what
// GUICanvas::submitCommand does).
void submit(CLDocument* doc, GUICanvas* page, klsCommand* cmd) {
	cmd->setCanvas(page);
	doc->circuit.GetCommandProcessor()->Submit(cmd);
	doc->edited = true;
}

// The gate whose body is under a point; the smallest one when several overlap
// (a gate drawn inside another).
guiGate* gateAt(GUICanvas* page, float x, float y) {
	guiGate* best = nullptr;
	float bestArea = FLT_MAX;
	for (auto& g : *page->getGateList()) {
		if (!g.second) continue;
		klsBBox b = g.second->getSelectionBBox();
		if (b.empty()) b = g.second->getBBox();
		if (!b.contains(GLPoint2f(x, y))) continue;
		const float area = (b.getRight() - b.getLeft()) * (b.getTop() - b.getBottom());
		if (area < bestArea) { bestArea = area; best = g.second; }
	}
	return best;
}

guiWire* wireAt(GUICanvas* page, float x, float y, float delta) {
	for (auto& w : *page->getWireList())
		if (w.second && w.second->hover(x, y, delta)) return w.second;
	return nullptr;
}

void selectedIds(GUICanvas* page, std::vector<unsigned long>& gates, std::vector<unsigned long>& wires) {
	for (auto& g : *page->getGateList()) if (g.second && g.second->isSelected()) gates.push_back(g.first);
	for (auto& w : *page->getWireList()) if (w.second && w.second->isSelected()) wires.push_back(w.first);
	std::sort(gates.begin(), gates.end());
	std::sort(wires.begin(), wires.end());
}

// Move the selection by `delta` as one undo step: shift the wires, then the
// gates, then record it. GUICanvas::nudgeSelection and the drop in OnMouseUp
// do the same, including the Undo after Submit -- the move already happened,
// and Submit runs Do again.
void commitMove(CLDocument* doc, GUICanvas* page, std::vector<GateState>& moved,
                std::vector<WireState>& movedWires, GLPoint2f delta) {
	if (moved.empty()) return;
	cmdMoveSelection* mc = new cmdMoveSelection(&doc->circuit, moved, movedWires,
		moved[0].x, moved[0].y, moved[0].x + delta.x, moved[0].y + delta.y);
	for (auto& gs : moved) if (guiGate* g = doc->circuit.getGate(gs.id)) g->updateConnectionMerges();
	submit(doc, page, mc);
	mc->Undo();
	page->collisionUpdate();
}

void snapshotSelection(CLDocument* doc, GUICanvas* page, std::vector<GateState>& gates, std::vector<WireState>& wires) {
	for (auto& g : *page->getGateList()) {
		if (!g.second || !g.second->isSelected()) continue;
		float x, y;
		g.second->getGLcoords(x, y);
		gates.push_back(GateState(g.first, x, y, true));
	}
	for (auto& w : *page->getWireList())
		if (w.second && w.second->isSelected())
			wires.push_back(WireState(w.first, w.second->getCenter(), w.second->getSegmentMap()));
	(void)doc;
}

std::string scratch;   // backs returned strings until the next call

// A pin within `delta` of a point: the gate and pin name, or false.
bool pinAt(GUICanvas* page, float x, float y, float delta, unsigned long& gate, std::string& pin) {
	for (auto& g : *page->getGateList()) {
		if (!g.second) continue;
		klsBBox b = g.second->getBBox();
		b.extendTop(delta); b.extendBottom(delta); b.extendLeft(delta); b.extendRight(delta);
		if (!b.contains(GLPoint2f(x, y))) continue;
		const std::string hs = g.second->checkHotspots(x, y, delta);
		if (!hs.empty()) { gate = g.first; pin = hs; return true; }
	}
	return false;
}

// A pinned-down point for the wire code's segment hit tests (the wx canvas's
// snapMouse).
struct PointObject : klsCollisionObject {
	explicit PointObject(GLPoint2f p) : klsCollisionObject(COLL_MOUSEBOX) {
		klsBBox b; b.addPoint(p); setBBox(b);
	}
};

// Any edit keeps a Tidy Up on show, as it does in the wx app.
void settleTidy(CLDocument* doc) {
	if (doc && doc->tidy.active) cl_edit_tidy_end(doc, true);
}

// Finish a connection at a point: to a pin there, or to a wire. Returns
// whether one was made.
bool finishConnection(CLDocument* doc, GUICanvas* page, float x, float y) {
	EditGesture& g = doc->gesture;
	klsCommand* cmd = nullptr;
	unsigned long gate = 0;
	std::string pin;
	if (pinAt(page, x, y, g.hoverDelta, gate, pin) && !(gate == g.srcGate && pin == g.srcPin)) {
		cmd = edits::gateConnection(&doc->circuit, page, g.srcGate, g.srcPin, gate, pin);
	} else if (guiWire* w = wireAt(page, x, y, g.hoverDelta)) {
		cmd = edits::gateWireConnection(&doc->circuit, page, g.srcGate, g.srcPin, w->getID());
	}
	if (cmd == nullptr) return false;
	submit(doc, page, cmd);
	page->collisionUpdate();
	return true;
}

}  // namespace

extern "C" {

int cl_edit_press(CLDocument* doc, int pageIndex, double x, double y, int modifiers, double unitsPerPoint) {
	GUICanvas* page = doc ? doc->page(pageIndex) : nullptr;
	if (page == nullptr) return CL_PRESS_NOTHING;
	EditGesture& g = doc->gesture;
	// The click that ends a click-started connection: connect to what's
	// there, or cancel on nothing.
	if (g.mode == EditGesture::Connect && g.sticky) {
		if (GUICanvas* p = doc->page(g.page)) finishConnection(doc, p, (float)x, (float)y);
		g = EditGesture();
		return CL_PRESS_PART;
	}
	settleTidy(doc);
	g = EditGesture();
	g.page = pageIndex;
	g.start = GLPoint2f((float)x, (float)y);
	g.dragSlop = (float)(kDragPoints * unitsPerPoint);
	g.hoverDelta = (float)(kPinPoints * unitsPerPoint);
	g.current = g.start;
	const bool shift = modifiers & CL_MOD_SHIFT;

	// A pin: start a wire from it.
	{
		unsigned long gate = 0;
		std::string pin;
		if (!shift && pinAt(page, (float)x, (float)y, g.hoverDelta, gate, pin)) {
			page->unselectAllGates();
			page->unselectAllWires();
			g.mode = EditGesture::Connect;
			g.srcGate = gate;
			g.srcPin = pin;
			return CL_PRESS_PART;
		}
	}

	if (guiGate* gate = gateAt(page, (float)x, (float)y)) {
		g.onGate = true;
		g.gate = gate->getID();
		if (shift) {
			if (gate->isSelected()) { gate->unselect(); g.toggledOff = true; }
			else gate->select();
		} else if (!gate->isSelected()) {
			page->unselectAllGates();
			page->unselectAllWires();
			gate->select();
		}
		g.mode = g.toggledOff ? EditGesture::None : EditGesture::Pressed;
		return CL_PRESS_PART;
	}
	if (guiWire* wire = wireAt(page, (float)x, (float)y, (float)(kHoverPoints * unitsPerPoint))) {
		if (shift) {
			if (wire->isSelected()) { wire->unselect(); g.toggledOff = true; }
			else wire->select();
			g.mode = g.toggledOff ? EditGesture::None : EditGesture::Pressed;
			return CL_PRESS_PART;
		}
		// Part of a bigger selection: a drag moves all of it. Otherwise the
		// press picks the wire, and a drag reshapes the segment under it.
		int selected = 0;
		for (auto& e : *page->getGateList()) if (e.second && e.second->isSelected()) selected++;
		for (auto& e : *page->getWireList()) if (e.second && e.second->isSelected()) selected++;
		if (wire->isSelected() && selected > 1) {
			g.mode = EditGesture::Pressed;
			return CL_PRESS_PART;
		}
		page->unselectAllGates();
		page->unselectAllWires();
		wire->select();
		PointObject at(snap(g.start));
		if (wire->startSegDrag(&at)) {
			g.mode = EditGesture::WireSeg;
			g.wire = wire->getID();
		} else {
			g.mode = EditGesture::Pressed;
		}
		return CL_PRESS_PART;
	}
	if (!shift) {
		page->unselectAllGates();
		page->unselectAllWires();
	}
	g.mode = EditGesture::BoxSelect;
	g.box.reset();
	return CL_PRESS_BOX;
}

void cl_edit_drag(CLDocument* doc, double x, double y) {
	if (doc == nullptr) return;
	EditGesture& g = doc->gesture;
	GUICanvas* page = doc->page(g.page);
	if (page == nullptr) return;
	const GLPoint2f m((float)x, (float)y);
	g.current = m;

	if (g.mode == EditGesture::Connect) return;   // the overlay draws the line
	if (g.mode == EditGesture::WireSeg) {
		if (!g.moved && std::fabs(m.x - g.start.x) < g.dragSlop && std::fabs(m.y - g.start.y) < g.dragSlop) return;
		if (guiWire* w = doc->circuit.getWire(g.wire)) {
			PointObject at(snap(m));
			w->updateSegDrag(&at);
			g.moved = true;
		}
		return;
	}

	if (g.mode == EditGesture::Pressed) {
		if (std::fabs(m.x - g.start.x) < g.dragSlop && std::fabs(m.y - g.start.y) < g.dragSlop) return;
		g.preMove.clear();
		g.preMoveWire.clear();
		snapshotSelection(doc, page, g.preMove, g.preMoveWire);
		// A drag that started on a lone wire waits for stage 3 (segment drags).
		g.mode = g.preMove.empty() ? EditGesture::None : EditGesture::Moving;
	}
	if (g.mode == EditGesture::Moving) {
		// Both ends snapped, so the selection moves in whole grid steps and
		// keeps its offset from the pointer.
		const GLPoint2f a = snap(g.start), b = snap(m);
		const GLPoint2f d(b.x - a.x, b.y - a.y);
		for (auto& ws : g.preMoveWire) if (guiWire* w = doc->circuit.getWire(ws.id)) w->move(ws.point, d);
		for (auto& gs : g.preMove) if (guiGate* gt = doc->circuit.getGate(gs.id)) gt->setGLcoords(gs.x + d.x, gs.y + d.y);
		g.lastDelta = d;
		return;
	}
	if (g.mode == EditGesture::BoxSelect) {
		g.box.reset();
		g.box.addPoint(g.start);
		g.box.addPoint(m);
		// Touching is enough (as the wx canvas does it): anything the box
		// overlaps is selected.
		for (auto& e : *page->getGateList()) {
			if (!e.second) continue;
			klsBBox b = e.second->getSelectionBBox();
			if (b.empty()) b = e.second->getBBox();
			if (g.box.overlaps(b)) e.second->select(); else e.second->unselect();
		}
		for (auto& e : *page->getWireList()) {
			if (!e.second) continue;
			if (g.box.overlaps(e.second->getBBox())) e.second->select(); else e.second->unselect();
		}
	}
}

void cl_edit_release(CLDocument* doc, double x, double y) {
	if (doc == nullptr) return;
	EditGesture& g = doc->gesture;
	GUICanvas* page = doc->page(g.page);
	if (page != nullptr) {
		if (g.mode == EditGesture::Connect) {
			const bool barelyMoved = std::fabs((float)x - g.start.x) < g.dragSlop &&
			                         std::fabs((float)y - g.start.y) < g.dragSlop;
			if (finishConnection(doc, page, (float)x, (float)y) || !barelyMoved) { g = EditGesture(); return; }
			// Pressed and let go on the pin: the line follows the pointer
			// until the next click.
			g.sticky = true;
			g.current = GLPoint2f((float)x, (float)y);
			return;
		}
		if (g.mode == EditGesture::WireSeg) {
			if (guiWire* w = doc->circuit.getWire(g.wire)) {
				if (g.moved) {
					w->endSegDrag();
					w->select();
					submit(doc, page, new cmdWireSegDrag(&doc->circuit, page, g.wire));
				} else {
					w->setSegmentMap(w->getOldSegmentMap());   // a click, not a reshape
				}
			}
			page->collisionUpdate();
			g = EditGesture();
			return;
		}
		if (g.mode == EditGesture::Moving) {
			if (g.lastDelta.x != 0 || g.lastDelta.y != 0) {
				commitMove(doc, page, g.preMove, g.preMoveWire, g.lastDelta);
			}
		} else if (g.mode == EditGesture::Pressed && g.onGate) {
			// Pressed and let go in place: a switch or keypad gets the click.
			if (guiGate* gate = doc->circuit.getGate(g.gate)) {
				if (klsMessage::Message_SET_GATE_PARAM* msg = gate->checkClick((float)x, (float)y)) {
					doc->circuit.sendMessageToCore(klsMessage::Message(klsMessage::MT_SET_GATE_PARAM, msg));
					doc->circuit.sendMessageToCore(klsMessage::Message(klsMessage::MT_UPDATE_GATES));
				}
			}
		}
		page->collisionUpdate();
	}
	g = EditGesture();
}

void cl_edit_cancel(CLDocument* doc) {
	if (doc == nullptr) return;
	EditGesture& g = doc->gesture;
	if (g.mode == EditGesture::WireSeg)
		if (guiWire* w = doc->circuit.getWire(g.wire)) w->setSegmentMap(w->getOldSegmentMap());
	if (g.mode == EditGesture::Moving) {
		// Put everything back where it was: gates first, then each wire's old
		// shape, so it's trimmed to where its pins really are.
		for (auto& gs : g.preMove) if (guiGate* gt = doc->circuit.getGate(gs.id)) gt->setGLcoords(gs.x, gs.y, true);
		for (auto& ws : g.preMoveWire) if (guiWire* w = doc->circuit.getWire(ws.id)) w->setSegmentMap(ws.oldWireTree);
	}
	if (GUICanvas* page = doc->page(g.page)) page->collisionUpdate();
	g = EditGesture();
}

bool cl_edit_box(const CLDocument* doc, double* left, double* bottom, double* right, double* top) {
	if (doc == nullptr || doc->gesture.mode != EditGesture::BoxSelect) return false;
	klsBBox b = doc->gesture.box;
	if (b.empty()) return false;
	*left = b.getLeft(); *right = b.getRight(); *bottom = b.getBottom(); *top = b.getTop();
	return true;
}

void cl_edit_select_all(CLDocument* doc, int pageIndex) {
	GUICanvas* page = doc ? doc->page(pageIndex) : nullptr;
	if (page == nullptr) return;
	for (auto& g : *page->getGateList()) if (g.second) g.second->select();
	for (auto& w : *page->getWireList()) if (w.second) w.second->select();
}

void cl_edit_select_none(CLDocument* doc, int pageIndex) {
	GUICanvas* page = doc ? doc->page(pageIndex) : nullptr;
	if (page == nullptr) return;
	page->unselectAllGates();
	page->unselectAllWires();
}

int cl_edit_selected_gate_count(const CLDocument* doc, int pageIndex) {
	GUICanvas* page = doc ? doc->page(pageIndex) : nullptr;
	if (page == nullptr) return 0;
	int n = 0;
	for (auto& g : *page->getGateList()) if (g.second && g.second->isSelected()) n++;
	return n;
}

int cl_edit_selected_wire_count(const CLDocument* doc, int pageIndex) {
	GUICanvas* page = doc ? doc->page(pageIndex) : nullptr;
	if (page == nullptr) return 0;
	int n = 0;
	for (auto& w : *page->getWireList()) if (w.second && w.second->isSelected()) n++;
	return n;
}

void cl_edit_delete(CLDocument* doc, int pageIndex) {
	settleTidy(doc);
	GUICanvas* page = doc ? doc->page(pageIndex) : nullptr;
	if (page == nullptr) return;
	std::vector<unsigned long> gates, wires;
	selectedIds(page, gates, wires);
	if (gates.empty() && wires.empty()) return;
	submit(doc, page, new cmdDeleteSelection(&doc->circuit, page, gates, wires));
	page->collisionUpdate();
}

void cl_edit_rotate(CLDocument* doc, int pageIndex) {
	settleTidy(doc);
	GUICanvas* page = doc ? doc->page(pageIndex) : nullptr;
	if (page == nullptr) return;
	std::vector<klsCommand*> steps;
	for (auto& e : *page->getGateList()) {
		guiGate* gate = e.second;
		if (!gate || !gate->isSelected()) continue;
		bool wired = false;
		for (auto& hs : gate->getHotspotList()) if (gate->isConnected(hs.first)) { wired = true; break; }
		if (wired) continue;
		std::istringstream iss(gate->getGUIParam("angle"));
		float angle = 0;
		iss >> angle;
		std::ostringstream oss;
		oss << std::fmod(angle - 90.0f + 360.0f, 360.0f);
		ParameterMap gui;
		gui["angle"] = oss.str();
		klsCommand* cmd = new cmdSetParams(&doc->circuit, e.first, paramSet(&gui, nullptr));
		cmd->setCanvas(page);
		cmd->Do();
		steps.push_back(cmd);
	}
	if (steps.empty()) return;
	// Applied above; the block's first Do is a no-op, so this only records it.
	submit(doc, page, new cmdPasteBlock(steps, "Rotate"));
	page->collisionUpdate();
}

void cl_edit_nudge(CLDocument* doc, int pageIndex, double dx, double dy) {
	settleTidy(doc);
	GUICanvas* page = doc ? doc->page(pageIndex) : nullptr;
	if (page == nullptr) return;
	std::vector<GateState> moved;
	std::vector<WireState> movedWires;
	snapshotSelection(doc, page, moved, movedWires);
	if (moved.empty()) return;
	const GLPoint2f d((float)dx, (float)dy);
	for (auto& ws : movedWires) if (guiWire* w = doc->circuit.getWire(ws.id)) w->move(ws.point, d);
	for (auto& gs : moved) if (guiGate* g = doc->circuit.getGate(gs.id)) g->setGLcoords(gs.x + d.x, gs.y + d.y);
	commitMove(doc, page, moved, movedWires, d);
}

bool cl_edit_add_gate(CLDocument* doc, int pageIndex, const char* libGateName, double x, double y) {
	settleTidy(doc);
	GUICanvas* page = doc ? doc->page(pageIndex) : nullptr;
	if (page == nullptr || libGateName == nullptr) return false;
	const GLPoint2f at = snap(GLPoint2f((float)x, (float)y));
	const unsigned long id = doc->circuit.getNextAvailableGateID();
	submit(doc, page, new cmdCreateGate(page, &doc->circuit, id, libGateName, at.x, at.y));
	guiGate* created = doc->circuit.getGate(id);
	if (created == nullptr) return false;
	// Send the library's settings to the simulator, as a drop on the wx canvas
	// does (not an undo step of its own).
	cmdSetParams params(&doc->circuit, id, paramSet(created->getAllGUIParams(), created->getAllLogicParams()));
	params.Do();
	page->unselectAllGates();
	page->unselectAllWires();
	created->select();
	page->collisionUpdate();
	return true;
}

const char* cl_edit_copy(CLDocument* doc, int pageIndex) {
	scratch.clear();
	GUICanvas* page = doc ? doc->page(pageIndex) : nullptr;
	if (page == nullptr) return "";
	std::vector<unsigned long> gates, wires;
	selectedIds(page, gates, wires);
	if (gates.empty()) return "";
	klsClipboard cb;
	scratch = cb.serializeBlock(&doc->circuit, page, gates, wires);
	return scratch.c_str();
}

bool cl_edit_paste(CLDocument* doc, int pageIndex, const char* text, double x, double y, bool shift,
                   const char** clipboardOut) {
	GUICanvas* page = doc ? doc->page(pageIndex) : nullptr;
	if (clipboardOut) *clipboardOut = "";
	if (page == nullptr || text == nullptr) return false;
	settleTidy(doc);
	klsClipboard::shiftHeld = shift;
	klsClipboard::rewrittenText.clear();
	klsClipboard cb;
	cmdPasteBlock* block = cb.pasteText(&doc->circuit, page, text, true);
	klsClipboard::shiftHeld = false;
	if (block == nullptr) return false;

	// The pasted things are selected. Put the top-left gate on the point, as
	// the wx canvas's paste does once it's dropped.
	std::vector<GateState> gates;
	std::vector<WireState> wires;
	snapshotSelection(doc, page, gates, wires);
	float minX = FLT_MAX, maxY = -FLT_MAX;
	for (auto& gs : gates) { minX = std::min(minX, gs.x); maxY = std::max(maxY, gs.y); }
	const GLPoint2f target = snap(GLPoint2f((float)x, (float)y));
	const GLPoint2f d = gates.empty() ? GLPoint2f(0, 0) : GLPoint2f(target.x - minX, target.y - maxY);
	for (auto& gs : gates) {
		cmdMoveGate* mg = new cmdMoveGate(&doc->circuit, gs.id, gs.x, gs.y, gs.x + d.x, gs.y + d.y, true);
		mg->Do();
		block->addCommand(mg);
	}
	for (auto& ws : wires) {
		cmdMoveWire* mw = new cmdMoveWire(&doc->circuit, ws.id, ws.oldWireTree, d);
		mw->Do();
		block->addCommand(mw);
	}
	submit(doc, page, block);
	page->collisionUpdate();
	scratch = klsClipboard::rewrittenText;
	if (clipboardOut) *clipboardOut = scratch.c_str();
	return true;
}

bool cl_edit_undo(CLDocument* doc) {
	if (doc == nullptr) return false;
	settleTidy(doc);
	const bool ok = doc->circuit.GetCommandProcessor()->Undo();
	if (ok) doc->edited = true;
	for (auto& p : doc->pages) p->collisionUpdate();
	return ok;
}

bool cl_edit_redo(CLDocument* doc) {
	if (doc == nullptr) return false;
	settleTidy(doc);
	const bool ok = doc->circuit.GetCommandProcessor()->Redo();
	if (ok) doc->edited = true;
	for (auto& p : doc->pages) p->collisionUpdate();
	return ok;
}

bool cl_edit_can_undo(const CLDocument* doc) {
	return doc && const_cast<GUICircuit&>(doc->circuit).GetCommandProcessor()->CanUndo();
}

bool cl_edit_can_redo(const CLDocument* doc) {
	return doc && const_cast<GUICircuit&>(doc->circuit).GetCommandProcessor()->CanRedo();
}

const char* cl_edit_undo_name(const CLDocument* doc) {
	scratch = doc ? const_cast<GUICircuit&>(doc->circuit).GetCommandProcessor()->GetUndoName() : "";
	return scratch.c_str();
}

const char* cl_edit_redo_name(const CLDocument* doc) {
	scratch = doc ? const_cast<GUICircuit&>(doc->circuit).GetCommandProcessor()->GetRedoName() : "";
	return scratch.c_str();
}

bool cl_document_is_edited(const CLDocument* doc) { return doc && doc->edited; }

// ---- Gate settings ---------------------------------------------------------

long cl_edit_single_gate(const CLDocument* doc, int pageIndex) {
	GUICanvas* page = doc ? doc->page(pageIndex) : nullptr;
	if (page == nullptr) return -1;
	long found = -1;
	for (auto& g : *page->getGateList()) {
		if (!g.second || !g.second->isSelected()) continue;
		if (found != -1) return -1;
		found = (long)g.first;
	}
	return found;
}

static const LibraryGate* libraryGateOf(const CLDocument* doc, long gate) {
	guiGate* g = doc ? doc->circuit.getGate((unsigned long)gate) : nullptr;
	if (g == nullptr) return nullptr;
	auto lib = gateLibrary().libraries.find(g->getLibraryName());
	if (lib == gateLibrary().libraries.end()) return nullptr;
	auto def = lib->second.find(g->getLibraryGateName());
	return def == lib->second.end() ? nullptr : &def->second;
}

const char* cl_gate_caption(const CLDocument* doc, long gate) {
	const LibraryGate* lg = libraryGateOf(doc, gate);
	scratch = lg ? (lg->caption.empty() ? lg->gateName : lg->caption) : "";
	return scratch.c_str();
}

int cl_gate_setting_count(const CLDocument* doc, long gate) {
	const LibraryGate* lg = libraryGateOf(doc, gate);
	return lg ? (int)lg->dlgParams.size() : 0;
}

bool cl_gate_setting(const CLDocument* doc, long gate, int index, CLGateSetting* out) {
	static std::string label, name, type, value;
	const LibraryGate* lg = libraryGateOf(doc, gate);
	if (lg == nullptr || out == nullptr || index < 0 || index >= (int)lg->dlgParams.size()) return false;
	const lgDlgParam& p = lg->dlgParams[index];
	guiGate* g = doc->circuit.getGate((unsigned long)gate);
	label = p.textLabel; name = p.name; type = p.type;
	value = p.isGui ? g->getGUIParam(p.name) : g->getLogicParam(p.name);
	out->label = label.c_str();
	out->name = name.c_str();
	out->type = type.c_str();
	out->value = value.c_str();
	out->min = p.Rmin;
	out->max = p.Rmax;
	return true;
}

bool cl_gate_set_setting(CLDocument* doc, long gate, const char* name, const char* value) {
	const LibraryGate* lg = libraryGateOf(doc, gate);
	if (lg == nullptr || name == nullptr || value == nullptr) return false;
	for (const lgDlgParam& p : lg->dlgParams) {
		if (p.name != name) continue;
		settleTidy(doc);
		ParameterMap m;
		m[name] = value;
		klsCommand* cmd = p.isGui
			? new cmdSetParams(&doc->circuit, (unsigned long)gate, paramSet(&m, nullptr))
			: new cmdSetParams(&doc->circuit, (unsigned long)gate, paramSet(nullptr, &m));
		// The page the gate is on, so undo can say where it happened.
		GUICanvas* page = nullptr;
		for (auto& pg : doc->pages) if (pg->getGateList()->count((unsigned long)gate)) page = pg.get();
		submit(doc, page, cmd);
		return true;
	}
	return false;
}

// ---- Wires -------------------------------------------------------------------

bool cl_edit_hover(CLDocument* doc, int pageIndex, double x, double y, double unitsPerPoint) {
	GUICanvas* page = doc ? doc->page(pageIndex) : nullptr;
	if (page == nullptr) return false;
	EditGesture& g = doc->gesture;
	bool redraw = false;
	if (g.mode == EditGesture::Connect && g.sticky) {
		g.current = GLPoint2f((float)x, (float)y);
		redraw = true;
	}
	unsigned long gate = 0;
	std::string pin;
	const bool onPin = pinAt(page, (float)x, (float)y, (float)(kPinPoints * unitsPerPoint), gate, pin);
	GLPoint2f at;
	if (onPin) doc->circuit.getGate(gate)->getHotspotCoords(pin, at.x, at.y);
	if (onPin != doc->hoverPin || (onPin && (at.x != doc->hoverPinAt.x || at.y != doc->hoverPinAt.y))) redraw = true;
	doc->hoverPin = onPin;
	doc->hoverPinAt = at;
	return redraw;
}

bool cl_edit_is_connecting(const CLDocument* doc) {
	return doc && doc->gesture.mode == EditGesture::Connect && doc->gesture.sticky;
}

int cl_edit_context(CLDocument* doc, int pageIndex, double x, double y, double unitsPerPoint) {
	GUICanvas* page = doc ? doc->page(pageIndex) : nullptr;
	if (page == nullptr) return CL_CONTEXT_NOTHING;
	settleTidy(doc);
	doc->gesture = EditGesture();
	unsigned long gate = 0;
	std::string pin;
	if (pinAt(page, (float)x, (float)y, (float)(kPinPoints * unitsPerPoint), gate, pin)) {
		guiGate* g = doc->circuit.getGate(gate);
		if (g->isConnected(pin)) return CL_CONTEXT_PIN;
	}
	auto pick = [&](bool selected, auto select) {
		// Right-clicking something already in the selection keeps the
		// selection, so its menu acts on all of it.
		if (!selected) { page->unselectAllGates(); page->unselectAllWires(); select(); }
	};
	if (guiWire* w = wireAt(page, (float)x, (float)y, (float)(kHoverPoints * unitsPerPoint))) {
		pick(w->isSelected(), [&] { w->select(); });
		return CL_CONTEXT_WIRE;
	}
	if (guiGate* g = gateAt(page, (float)x, (float)y)) {
		pick(g->isSelected(), [&] { g->select(); });
		return CL_CONTEXT_GATE;
	}
	return CL_CONTEXT_NOTHING;
}

void cl_edit_disconnect_pin(CLDocument* doc, int pageIndex, double x, double y, double unitsPerPoint) {
	GUICanvas* page = doc ? doc->page(pageIndex) : nullptr;
	if (page == nullptr) return;
	unsigned long gate = 0;
	std::string pin;
	if (!pinAt(page, (float)x, (float)y, (float)(kPinPoints * unitsPerPoint), gate, pin)) return;
	guiGate* g = doc->circuit.getGate(gate);
	if (!g->isConnected(pin)) return;
	guiWire* w = g->getConnection(pin);
	// As the wx canvas does it: off the pin, or the whole wire when this was
	// one of its only two ends.
	if (w->numConnections() > 2) submit(doc, page, new cmdDisconnectWire(&doc->circuit, w->getID(), gate, pin));
	else submit(doc, page, new cmdDeleteWire(&doc->circuit, page, w->getID()));
	page->collisionUpdate();
}

void cl_edit_straighten(CLDocument* doc, int pageIndex) {
	GUICanvas* page = doc ? doc->page(pageIndex) : nullptr;
	if (page == nullptr) return;
	settleTidy(doc);
	std::vector<unsigned long> ids;
	for (auto& w : *page->getWireList()) if (w.second && w.second->isSelected()) ids.push_back(w.first);
	if (ids.empty()) {
		std::set<unsigned long> fromGates;
		for (auto& g : *page->getGateList()) {
			if (!g.second || !g.second->isSelected()) continue;
			for (const auto& c : g.second->getConnections()) if (c.second) fromGates.insert(c.second->getID());
		}
		ids.assign(fromGates.begin(), fromGates.end());
	}
	std::sort(ids.begin(), ids.end());
	std::vector<klsCommand*> steps;
	for (WireReshape& w : edits::rerouteWires(page, ids))
		steps.push_back(new cmdWireSegDrag(&doc->circuit, page, w.id, w.before, w.after));
	if (steps.size() == 1) submit(doc, page, steps[0]);
	else if (!steps.empty()) submit(doc, page, new cmdPasteBlock(steps, "Straighten Wires"));
	page->collisionUpdate();
}

bool cl_edit_tidy_begin(CLDocument* doc, int pageIndex, int mode) {
	GUICanvas* page = doc ? doc->page(pageIndex) : nullptr;
	if (page == nullptr) return false;
	if (doc->tidy.active) cl_edit_tidy_end(doc, false);
	doc->gesture = EditGesture();
	edits::TidyPlan plan = edits::applyTidy(page, mode);
	page->collisionUpdate();
	if (plan.empty()) return false;
	doc->tidy.active = true;
	doc->tidy.mode = mode;
	doc->tidy.page = pageIndex;
	doc->tidy.plan = std::move(plan);
	return true;
}

void cl_edit_tidy_end(CLDocument* doc, bool keep) {
	if (doc == nullptr || !doc->tidy.active) return;
	GUICanvas* page = doc->page(doc->tidy.page);
	edits::TidyPlan plan = std::move(doc->tidy.plan);
	doc->tidy = CLDocument::TidyPreview();
	if (keep) submit(doc, page, new cmdTidy(&doc->circuit, page, plan.moves, plan.wires));
	else edits::revertTidy(&doc->circuit, plan);
	if (page) page->collisionUpdate();
}

bool cl_edit_tidy_active(const CLDocument* doc) { return doc && doc->tidy.active; }
int cl_edit_tidy_mode(const CLDocument* doc) { return doc ? doc->tidy.mode : 0; }

void cl_edit_draw_overlay(CLDocument* doc, int pageIndex, CGContextRef ctx, double backingScale,
                          double originX, double originY, double unitsPerPoint,
                          double ar, double ag, double ab) {
	if (doc == nullptr || ctx == nullptr || unitsPerPoint <= 0) return;
	using cl::render::Color;
	using cl::render::Point;
	using cl::render::Stroke;
	CGContextSaveGState(ctx);
	CGContextScaleCTM(ctx, 1.0 / backingScale, 1.0 / backingScale);
	const float scale = (float)(backingScale / unitsPerPoint);
	cl::render::Transform t;
	t.a = scale; t.d = -scale;
	t.e = (float)(-originX * scale); t.f = (float)(originY * scale);
	cl::mac::CGScene scene(ctx);
	scene.setViewport(t);
	const float px = (float)backingScale;   // one point, in the device pixels strokes use

	// Tidy Up: where things were, faintly.
	if (doc->tidy.active && doc->tidy.page == pageIndex) {
		const Color ghost((float)ar, (float)ag, (float)ab, 0.35f);
		const std::vector<float>& r = doc->tidy.plan.ghostRects;
		for (size_t i = 0; i + 3 < r.size(); i += 4) {
			Point pts[4] = { Point(r[i], r[i + 1]), Point(r[i], r[i + 3]), Point(r[i + 2], r[i + 3]), Point(r[i + 2], r[i + 1]) };
			scene.polyline(pts, 4, Stroke(ghost, px), true);
		}
		std::vector<Point> segs;
		const std::vector<float>& l = doc->tidy.plan.ghostLines;
		for (size_t i = 0; i + 3 < l.size(); i += 4) { segs.push_back(Point(l[i], l[i + 1])); segs.push_back(Point(l[i + 2], l[i + 3])); }
		if (!segs.empty()) scene.lines(&segs[0], segs.size(), Stroke(Color((float)ar, (float)ag, (float)ab, 0.22f), px));
	}

	// A connection being made: a line from its pin to the pointer.
	const EditGesture& g = doc->gesture;
	if (g.mode == EditGesture::Connect && g.page == pageIndex) {
		if (guiGate* src = doc->circuit.getGate(g.srcGate)) {
			float sx, sy;
			src->getHotspotCoords(g.srcPin, sx, sy);
			Point ln[2] = { Point(sx, sy), Point(g.current.x, g.current.y) };
			scene.lines(ln, 2, Stroke(Color(0.0f, 0.72f, 0.0f, 1.0f), 1.5f * px));
		}
	}

	// The pin under the pointer: the red box you drag a wire out of.
	if (doc->hoverPin) {
		const float r = (float)(kPinBoxPoints * unitsPerPoint);
		const GLPoint2f c = doc->hoverPinAt;
		Point pts[4] = { Point(c.x - r, c.y + r), Point(c.x + r, c.y + r), Point(c.x + r, c.y - r), Point(c.x - r, c.y - r) };
		scene.polyline(pts, 4, Stroke(Color(1.0f, 0.0f, 0.0f, 1.0f), px), true);
	}
	CGContextRestoreGState(ctx);
}

}  // extern "C"
