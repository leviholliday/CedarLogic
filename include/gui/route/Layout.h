// Gate layout for Tidy Up (Shift+S): where to move each gate so the circuit
// reads left to right in clean columns, before GridRouter redraws the wires.
//
// Two modes:
//   * KeepLayout -- the user's arrangement, straightened: gates that sit in
//     roughly the same column are lined up, a gate whose pin is a step or
//     three off the pin it connects to is nudged level with it (so the wire
//     runs straight), gates that overlap in a column are spread apart, and
//     columns that are too close for their wires get room.
//   * Rearrange -- layered by signal flow: inputs in the first column, each
//     gate one column right of what drives it, outputs in the last; gates
//     ordered within a column to cut crossings, then placed level with what
//     they connect to where the column has room.
//
// Pure: no wx, no engine. Unit-tested in test_route.

#ifndef CL_ROUTE_LAYOUT_H
#define CL_ROUTE_LAYOUT_H

#include <utility>
#include <vector>

namespace cl {
namespace route {

struct LayoutPin {
	float x = 0.0f, y = 0.0f;   // world position now
	int dx = 0, dy = 0;         // way it leaves the gate
	int net = -1;               // pins with the same net are wired together; -1 = free
	bool output = false;        // drives its net
};

struct LayoutNode {
	float l = 0.0f, b = 0.0f, r = 0.0f, t = 0.0f;   // footprint now, y up
	std::vector<LayoutPin> pins;
	bool movable = true;        // false: stays put, but still counts as a neighbor
	bool indicator = false;     // a light (LED): packed with others it's a display
};

enum class TidyMode { KeepLayout = 0, Rearrange = 1 };

struct LayoutInput {
	std::vector<LayoutNode> nodes;
	TidyMode mode = TidyMode::KeepLayout;
	float step = 0.5f;
};

// How far to move each node, in whole grid steps; (0, 0) for fixed ones.
std::vector<std::pair<float, float>> layoutGates(const LayoutInput &in);

}  // namespace route
}  // namespace cl

#endif  // CL_ROUTE_LAYOUT_H
