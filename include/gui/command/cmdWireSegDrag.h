
#pragma once
#include "klsCommand.h"
#include <map>
#include "../wireSegment.h"

// cmdWireSegDrag - Set's a wire's tree after dragging a segment
class cmdWireSegDrag : public klsCommand {
public:
	cmdWireSegDrag(GUICircuit* gCircuit, GUICanvas* gCanvas, IDType wireID);
	// Any before/after reshape (e.g. Straighten Route), not just a drag.
	cmdWireSegDrag(GUICircuit* gCircuit, GUICanvas* gCanvas, IDType wireID,
		const std::map<long, wireSegment>& before, const std::map<long, wireSegment>& after);

	bool Do();

	bool Undo();

private:
	std::map<long, wireSegment> oldSegMap;
	std::map<long, wireSegment> newSegMap;
	unsigned long wireID;
};