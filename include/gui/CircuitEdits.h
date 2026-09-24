// Edits that shape a page -- making connections, rerouting wires (Straighten)
// and Tidy Up -- shared by the wx canvas and the native Mac front end, so both
// do exactly the same thing. Nothing here touches a window: each function
// works on a page's gates and wires (GUICanvas::getGateList/getWireList, which
// the wx canvas and the native page model both provide) and the circuit.

#ifndef CL_CIRCUITEDITS_H
#define CL_CIRCUITEDITS_H

#include <set>
#include <string>
#include <vector>
#include "logic_values.h"
#include "command/cmdTidy.h"

class GUICircuit;
class GUICanvas;
class guiWire;
class klsCommand;

namespace edits {

// The command that joins a gate's pin to an existing wire, or null when they
// are already joined or the bus widths don't match. Not yet run.
klsCommand* gateWireConnection(GUICircuit* circuit, GUICanvas* page, IDType gateId,
                               const std::string& hotspot, IDType wireId);

// The command that joins two gate pins: a new wire when neither has one,
// otherwise the free pin joins the other's wire. Null when there's nothing
// to do (the same pin twice, already joined, mismatched buses). Not yet run.
klsCommand* gateConnection(GUICircuit* circuit, GUICanvas* page, IDType gate1Id,
                           const std::string& hotspot1, IDType gate2Id, const std::string& hotspot2);

// Route these wires together around every gate on the page and its other
// wires (route/GridRouter.h), and apply the result. A wire the router can't
// place gets the old one-wire straighten; one whose gates stayed put keeps its
// shape when the new route would be far longer. Returns each wire's before and
// after shape. `movedGates`: gates that just moved (Tidy Up).
std::vector<WireReshape> rerouteWires(GUICanvas* page, const std::vector<unsigned long>& ids,
                                      const std::set<unsigned long>* movedGates = nullptr);

// Reroute one wire on its own, sliding its trunk off other wires.
void straightenWireAvoiding(GUICanvas* page, guiWire* wire);

// A Tidy Up, worked out and applied: gates moved (without dragging their
// wires) and their wires rerouted. Record it with cmdTidy (whose first Do is a
// no-op), or put it back with revertTidy.
struct TidyPlan {
	std::vector<cmdTidy::GateMove> moves;
	std::vector<WireReshape> wires;
	std::vector<float> ghostRects;   // l, b, r, t per moved gate's old spot
	std::vector<float> ghostLines;   // x0, y0, x1, y1 per old wire segment
	bool empty() const { return moves.empty() && wires.empty(); }
};

// mode 0 keeps the layout's shape, 1 rearranges by signal flow. Tidies the
// selected gates, or the whole page when nothing is selected.
TidyPlan applyTidy(GUICanvas* page, int mode);

// Undo an applied plan that wasn't recorded: gates back first, so each wire's
// old shape is trimmed to where its pins really are.
void revertTidy(GUICircuit* circuit, const TidyPlan& plan);

}  // namespace edits

#endif  // CL_CIRCUITEDITS_H
