// Edits that shape a page, shared by the wx canvas and the native Mac front
// end -- see include/gui/CircuitEdits.h. Moved here from GUICanvas.cpp as they
// were, with the page passed in instead of being `this`.

#include "CircuitEdits.h"
#include "GUICanvas.h"
#include "GUICircuit.h"
#include "guiGate.h"
#include "guiWire.h"
#include "route/GridRouter.h"
#include "route/Layout.h"
#include "command/cmdConnectWire.h"
#include "command/cmdCreateWire.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>

namespace edits {

namespace {

// Page-scoped lookups, as GUICanvas::getGate/getWire do them: a wire by its
// head id, and nothing that isn't on this page.
guiGate* pageGate(GUICanvas* page, unsigned long id) {
	auto it = page->getGateList()->find(id);
	return it != page->getGateList()->end() ? it->second : nullptr;
}

guiWire* pageWire(GUICanvas* page, unsigned long id) {
	auto it = page->getWireList()->find(id);
	return it != page->getWireList()->end() ? it->second : nullptr;
}

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

}  // namespace

klsCommand* gateWireConnection(GUICircuit* gCircuit, GUICanvas* page, IDType gateId,
                               const std::string& hotspot, IDType wireId) {

	guiGate *gate = pageGate(page, gateId);
	guiWire *wire = pageWire(page, wireId);
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

klsCommand* gateConnection(GUICircuit* gCircuit, GUICanvas* page, IDType gate1Id,
                           const std::string& hotspot1, IDType gate2Id, const std::string& hotspot2) {

	guiGate *gate1 = pageGate(page, gate1Id);
	guiGate *gate2 = pageGate(page, gate2Id);
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

		std::vector<IDType> wireIds(hs1->getBusLines());

		// Get the correct number of new, unique wire ids.
		for (int i = 0; i < (int)wireIds.size(); i++) {
			wireIds[i] = gCircuit->getNextAvailableWireID();
		}

		cmdConnectWire *connectwire =
			new cmdConnectWire(gCircuit, wireIds[0], gate1Id, hotspot1);

		cmdConnectWire *connectwire2 =
			new cmdConnectWire(gCircuit, wireIds[0], gate2Id, hotspot2);

		cmdCreateWire *createWire =
			new cmdCreateWire(page, gCircuit, wireIds, connectwire, connectwire2);

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
			return gateWireConnection(gCircuit, page, gate2Id,
				hotspot2, gate1->getConnection(hotspot1)->getID());
		}
		else if (gate2->isConnected(hotspot2)) {
			return gateWireConnection(gCircuit, page, gate1Id,
				hotspot1, gate2->getConnection(hotspot2)->getID());
		}
		return nullptr;
	}
}


std::vector<WireReshape> rerouteWires(GUICanvas* page, const std::vector<unsigned long>& ids,
                                      const std::set<unsigned long>* movedGates) {
	using namespace cl::route;
	std::vector<WireReshape> steps;
	std::vector<guiWire*> wires;
	std::vector<unsigned long> wireIds;
	std::set<unsigned long> routing;
	for (unsigned long id : ids) {
		guiWire* w = pageWire(page, id);
		if (w == nullptr || w->getConnections().size() < 2 || !routing.insert(id).second) continue;
		bool gatesOk = true;
		for (const wireConnection& c : w->getConnections()) if (pageGate(page, c.gid) == nullptr) gatesOk = false;
		if (!gatesOk) continue;
		wires.push_back(w);
		wireIds.push_back(id);
	}
	if (wires.empty()) return steps;

	GridInput in;
	// Gate footprints: the body plus the pins sticking out of it.
	std::map<unsigned long, klsBBox> bodies;
	for (auto& ge : *page->getGateList()) {
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
			guiGate* g = pageGate(page, conns[i].gid);
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
	for (auto& ge : *page->getGateList()) {
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
	for (auto& we : *page->getWireList()) {
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
		if (!routed) straightenWireAvoiding(page, w);
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


void straightenWireAvoiding(GUICanvas* page, guiWire* wire) {
	// Every other wire's segments, gathered once.
	std::vector<wireSegment> others;
	for (auto& we : *page->getWireList())
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

TidyPlan applyTidy(GUICanvas* page, int mode) {
	using namespace cl::route;
	TidyPlan plan;

	// The selection, or the whole page when nothing is selected. Every gate
	// goes in (the rest as fixed neighbors to line up with).
	std::vector<unsigned long> ids;
	bool anySelected = false;
	for (auto& ge : *page->getGateList()) {
		if (ge.second == nullptr) continue;
		ids.push_back(ge.first);
		if (ge.second->isSelected()) anySelected = true;
	}
	std::sort(ids.begin(), ids.end());
	if (ids.empty()) return plan;

	LayoutInput in;
	in.mode = mode == 1 ? TidyMode::Rearrange : TidyMode::KeepLayout;
	std::map<guiWire*, int> netOf;
	std::vector<unsigned long> nodeGate;
	for (unsigned long id : ids) {
		guiGate* g = pageGate(page, id);
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

	std::set<unsigned long> wireIds;
	for (size_t i = 0; i < nodeGate.size(); i++) {
		guiGate* g = pageGate(page, nodeGate[i]);
		if (!in.nodes[i].movable) continue;
		for (const auto& c : g->getConnections()) if (c.second != nullptr) wireIds.insert(c.second->getID());
		if (offsets[i].first == 0.0f && offsets[i].second == 0.0f) continue;
		cmdTidy::GateMove m;
		m.id = nodeGate[i];
		g->getGLcoords(m.fromX, m.fromY);
		m.toX = m.fromX + offsets[i].first;
		m.toY = m.fromY + offsets[i].second;
		const LayoutNode& n = in.nodes[i];
		plan.ghostRects.insert(plan.ghostRects.end(), { n.l, n.b, n.r, n.t });
		// Move without dragging the wires along; they're rerouted next.
		g->setGLcoords(m.toX, m.toY, true);
		plan.moves.push_back(m);
	}
	std::set<unsigned long> moved;
	for (const cmdTidy::GateMove& m : plan.moves) moved.insert(m.id);
	plan.wires = rerouteWires(page, std::vector<unsigned long>(wireIds.begin(), wireIds.end()), &moved);
	for (const WireReshape& w : plan.wires)
		for (const auto& seg : w.before)
			plan.ghostLines.insert(plan.ghostLines.end(),
				{ seg.second.begin.x, seg.second.begin.y, seg.second.end.x, seg.second.end.y });
	return plan;
}

void revertTidy(GUICircuit* circuit, const TidyPlan& plan) {
	for (const cmdTidy::GateMove& m : plan.moves)
		if (guiGate* g = circuit->getGate(m.id)) g->setGLcoords(m.fromX, m.fromY, true);
	for (const WireReshape& w : plan.wires)
		if (guiWire* wire = circuit->getWire(w.id)) wire->setSegmentMap(w.before);
}

}  // namespace edits
