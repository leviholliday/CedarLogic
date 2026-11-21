// GridRouter -- routes many wires together on the 0.5 grid.
//
// TrunkRouter shapes one wire at a time from its own pins, so it cannot see the
// gates in the way or the wire next to it: straightening a page used to stack
// every trunk on the same line. GridRouter takes every net to be routed, the
// gate bodies and the wires that stay put, and lays them all out at once:
//
//   * each net is grown as a tree from its driver, one pin at a time, by an A*
//     search over grid nodes where gate bodies are walls and each step costs
//     length plus a price for bends, for crossing another wire, for running
//     right beside one, for hugging a gate and for passing a foreign pin;
//   * each search looks in a window around its ends first (a few steps past
//     the box they make) and widens only when nothing there gets through, so
//     one wire on a big page doesn't explore the whole sheet;
//   * two wires on the same line (which reads as one wire) are all but
//     forbidden, and the nets are ripped up and rerouted a few times with the
//     price of contested nodes rising each pass (negotiated congestion), so
//     early nets move aside for later ones instead of the order deciding. On
//     a big page, where a full pass is costly, later passes reroute only the
//     nets still sharing a line, and stop once that stops helping;
//   * a net that still comes out much longer than the shortest tree joining
//     its pins (walled in by nets routed before it, it went round a block) is
//     routed again first, crossings cheap, the nets in its way put back round
//     it, and kept when the lot come out shorter, counting a crossing as a
//     few units of wire;
//   * finished wires are then polished: each straight run of a tree is tried
//     slid sideways to the lines where that ends a jog or a crossing, and kept
//     there when the wire comes out shorter, with fewer bends or less crowded
//     -- never with a crossing more (staircases become one bend, a branch
//     comes off where it is needed);
//   * the work is capped in search steps, not seconds (Tidy Up runs on the UI
//     thread, and the result mustn't depend on the machine): a wire past the
//     cap is left to the caller's plain route;
//   * the result is written as the same Segment topology TrunkRouter returns,
//     so guiWire adopts it with no new plumbing.
//
// Pure and deterministic: no wx, no engine, unit-tested in test_route.

#ifndef CL_ROUTE_GRIDROUTER_H
#define CL_ROUTE_GRIDROUTER_H

#include "route/WireRoute.h"
#include <vector>

namespace cl {
namespace route {

// A pin to reach, with the way it leaves its gate (dx, dy each -1, 0 or 1).
// (0, 0) means unknown: the wire may leave in any direction.
struct GridPin {
	float x = 0.0f, y = 0.0f;
	int dx = 0, dy = 0;
};

// One wire to route. Pin order is the caller's connection order; `root` is the
// pin the tree grows from (the driver, when there is one).
struct GridNet {
	std::vector<GridPin> pins;
	int root = 0;
};

// A gate's footprint, world units, y up. Its edges count as inside.
struct GridRect {
	float l = 0.0f, b = 0.0f, r = 0.0f, t = 0.0f;
};

// A segment of a wire that is not being rerouted: routed wires avoid lying on
// it and pay to cross it.
struct GridFixedSeg {
	float bx = 0.0f, by = 0.0f, ex = 0.0f, ey = 0.0f;
};

struct GridInput {
	std::vector<GridNet> nets;
	std::vector<GridRect> obstacles;
	std::vector<GridFixedSeg> fixed;
	// Pins of gates no routed net uses (free pins, or pins of fixed wires):
	// routed wires keep a step away from their tips.
	std::vector<GridPin> foreignPins;
	float step = 0.5f;
	float margin = 6.0f;   // room around the pins for detours
};

struct GridOutput {
	// Per net: whether it routed. A net that didn't keeps whatever shape the
	// caller had; routes[i] is empty then.
	std::vector<bool> ok;
	std::vector<RouteResult> routes;
	// Per net: true when another gate's body sits right across the way out of
	// one of its pins (parts jammed together); its route starts with a step
	// along or into that body.
	std::vector<bool> jammed;
	// Per net: true when it only routed with gate bodies passable -- the route
	// may run through one. The caller may prefer the shape the wire was drawn
	// with.
	std::vector<bool> forced;
	// Nodes two routed wires still share on the same line (0 when clean).
	int overlaps = 0;
};

GridOutput routeGrid(const GridInput &in);

}  // namespace route
}  // namespace cl

#endif  // CL_ROUTE_GRIDROUTER_H
