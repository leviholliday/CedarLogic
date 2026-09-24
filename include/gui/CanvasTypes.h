// Snapshots of where gates and wires were, so a move can be undone. Shared by
// the wx canvas and the native page model (and the move commands).

#ifndef CL_CANVASTYPES_H
#define CL_CANVASTYPES_H

#include <map>
#include "gl_defs.h"
#include "wireSegment.h"

// Struct GateState
//		stores the position and id of a gate so we know where we moved from
struct GateState {
	GateState( unsigned int nID, float nX, float nY, bool nSel ) : id(nID), x(nX), y(nY), selected(nSel) {}
	unsigned int id;
	float x;
	float y;
	bool selected;
};

// Struct WireState
//		stores the relative position (to itself) of a wire so we know where we moved from
struct WireState {
	WireState( unsigned int nID, GLPoint2f nPoint, std::map < long, wireSegment > nTree ) : 
		id(nID), point(nPoint), oldWireTree(nTree) {}
	unsigned int id;
	GLPoint2f point;
	std::map < long, wireSegment > oldWireTree;
};

#endif  // CL_CANVASTYPES_H
