#pragma once
#include "klsCommand.h"
#include <map>
#include <vector>
#include "../wireSegment.h"

// One wire's shape before and after a rearrangement.
struct WireReshape {
	unsigned long id = 0;
	std::map<long, wireSegment> before, after;
};

// cmdTidy - Tidy Up: gates moved and their wires rerouted, as one undo step.
// The canvas has already applied it (it was previewed), so the first Do is a
// no-op. Gates move without dragging their wires along; each wire's saved
// shape is set after its gates are in place, so the shape is trimmed to the
// pins where they actually are.
class cmdTidy : public klsCommand {
public:
	struct GateMove {
		unsigned long id = 0;
		float fromX = 0.0f, fromY = 0.0f, toX = 0.0f, toY = 0.0f;
	};

	cmdTidy(GUICircuit* gCircuit, GUICanvas* gCanvas, const std::vector<GateMove>& moves,
		const std::vector<WireReshape>& wires);

	bool Do();

	bool Undo();

private:
	std::vector<GateMove> moves;
	std::vector<WireReshape> wires;
	bool applied = true;
};
