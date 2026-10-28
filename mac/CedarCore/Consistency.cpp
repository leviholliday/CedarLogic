// cl_document_check: what must always be true of a document's gates and wires,
// checked in one place. The fuzzers and core_check call it after every step;
// docs/CORE-HARDENING.md explains each rule and the bugs that broke it.
//
// A wire's connections are kept in three places: the wire's list of the pins it
// holds (the truth), each gate's record of which wire is on each pin, and the
// segment of the wire's shape that ends at the pin. A save writes the segment
// records and a reopen rebuilds the other two from them, so a pin the wire holds
// but no segment records is a connection lost on the next save.

#include "DocumentImpl.h"
#include "guiGate.h"
#include "guiWire.h"

#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <utility>

namespace {

bool fail(char* why, int whyLen, const std::string& text) {
	if (why != nullptr && whyLen > 0) snprintf(why, (size_t)whyLen, "%s", text.c_str());
	return false;
}

std::string pinName(unsigned long gid, const std::string& pin) {
	return "gate " + std::to_string(gid) + " pin " + pin;
}

bool finite(const GLPoint2f& p) { return std::isfinite(p.x) && std::isfinite(p.y); }

// The pin lies on the segment (axis-aligned, with a little slack).
bool reaches(const wireSegment& s, float px, float py) {
	const float e = 0.05f;
	return px >= std::fmin(s.begin.x, s.end.x) - e && px <= std::fmax(s.begin.x, s.end.x) + e &&
	       py >= std::fmin(s.begin.y, s.end.y) - e && py <= std::fmax(s.begin.y, s.end.y) + e;
}

}  // namespace

bool cl_document_check(const CLDocument* constDoc, char* why, int whyLen) {
	if (why != nullptr && whyLen > 0) why[0] = 0;
	if (constDoc == nullptr) return true;
	CLDocument* doc = const_cast<CLDocument*>(constDoc);   // the engine's getters aren't const
	// A part or paste following the pointer isn't placed yet: its wires stretch
	// as it goes and are settled when it drops.
	if (doc->gesture.mode == EditGesture::Moving) return true;

	std::set<unsigned long> gatesSeen, wiresSeen;
	for (size_t p = 0; p < doc->pages.size(); p++) {
		GUICanvas* page = doc->pages[p].get();
		const std::string where = " on page " + std::to_string(p);
		for (auto& g : *page->getGateList()) {
			if (g.second == nullptr) continue;
			if (!gatesSeen.insert(g.first).second) return fail(why, whyLen, "gate " + std::to_string(g.first) + " is on two pages");
			if (doc->circuit.getGate(g.first) != g.second) return fail(why, whyLen, "gate " + std::to_string(g.first) + where + " is not the circuit's gate of that id");
			float x = 0, y = 0;
			g.second->getGLcoords(x, y);
			if (!std::isfinite(x) || !std::isfinite(y)) return fail(why, whyLen, "gate " + std::to_string(g.first) + " is at a position that isn't a number");
			// Each pin the gate says is wired is held by that wire, on this page.
			for (auto& hs : g.second->getHotspotList()) {
				if (!g.second->isConnected(hs.first)) continue;
				guiWire* w = g.second->getConnection(hs.first);
				if (w == nullptr) return fail(why, whyLen, pinName(g.first, hs.first) + " is wired to nothing");
				if (page->getWireList()->find(w->getID()) == page->getWireList()->end())
					return fail(why, whyLen, pinName(g.first, hs.first) + " is wired to wire " + std::to_string(w->getID()) + ", which is not on its page");
				bool held = false;
				for (const wireConnection& c : w->getConnections()) held = held || (c.gid == g.first && c.connection == hs.first);
				if (!held) return fail(why, whyLen, pinName(g.first, hs.first) + " says it is on wire " + std::to_string(w->getID()) + ", which doesn't hold it");
			}
		}
		for (auto& wi : *page->getWireList()) {
			guiWire* w = wi.second;
			if (w == nullptr) continue;
			const std::string name = "wire " + std::to_string(wi.first);
			if (!wiresSeen.insert(wi.first).second) return fail(why, whyLen, name + " is on two pages");
			if (doc->circuit.getWire(wi.first) != w) return fail(why, whyLen, name + where + " is not the circuit's wire of that id");
			const std::vector<wireConnection> held = w->getConnections();
			const std::map<long, wireSegment> shape = w->getSegmentMap();
			if (held.empty()) continue;   // nothing to record (a save leaves it out)
			if (shape.empty()) return fail(why, whyLen, name + " holds pins but has no shape");

			std::set<std::pair<unsigned long, std::string>> heldSet;
			for (const wireConnection& c : held) {
				if (!heldSet.insert({ c.gid, c.connection }).second) return fail(why, whyLen, name + " holds " + pinName(c.gid, c.connection) + " twice");
				auto g = page->getGateList()->find(c.gid);
				if (g == page->getGateList()->end() || g->second == nullptr)
					return fail(why, whyLen, name + where + " holds " + pinName(c.gid, c.connection) + ", which is not on its page");
				if (!g->second->isConnected(c.connection) || g->second->getConnection(c.connection) != w)
					return fail(why, whyLen, name + " holds " + pinName(c.gid, c.connection) + ", but the gate says that pin is not on it");
			}
			// Each held pin is recorded on exactly one segment, which reaches it;
			// each record is of a held pin.
			std::set<std::pair<unsigned long, std::string>> recorded;
			for (const auto& s : shape) {
				if (!finite(s.second.begin) || !finite(s.second.end)) return fail(why, whyLen, name + " segment " + std::to_string(s.first) + " has an end that isn't a number");
				for (const wireConnection& c : s.second.connections) {
					const std::pair<unsigned long, std::string> k(c.gid, c.connection);
					if (!heldSet.count(k)) return fail(why, whyLen, name + " segment " + std::to_string(s.first) + " records " + pinName(c.gid, c.connection) + ", which the wire doesn't hold");
					if (!recorded.insert(k).second) return fail(why, whyLen, name + " records " + pinName(c.gid, c.connection) + " twice");
					float px = 0, py = 0;
					page->getGateList()->find(c.gid)->second->getHotspotCoords(c.connection, px, py);
					if (!reaches(s.second, px, py)) return fail(why, whyLen, name + " segment " + std::to_string(s.first) + " records " + pinName(c.gid, c.connection) + " but doesn't reach it");
				}
			}
			for (const auto& k : heldSet)
				if (!recorded.count(k)) return fail(why, whyLen, name + " holds " + pinName(k.first, k.second) + " but no segment records it (a save would lose it)");
		}
	}
	return true;
}
