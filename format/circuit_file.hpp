#pragma once

#include "sexpr.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace cl {

// A plain, GUI- and core-independent description of a saved circuit: the seam
// the v3 .cdl format sits on. Reading a file produces one of these; writing
// consumes one. Applying it to the live GUI/core (or building one from them) is
// a separate pass, so the format can be loaded, validated, and tested headless.

struct XY {
	double x = 0, y = 0;
	bool operator==(const XY &o) const { return x == o.x && y == o.y; }
};

// A parameter is a name/value pair; values are strings at the file edge and
// parse into typed values (see the gate paramSchema) when applied. `gui`
// distinguishes a GUI param (legacy <gparam>) from a logic param (<lparam>).
struct Param {
	std::string name;
	std::string value;
	bool gui = false;
	bool operator==(const Param &o) const {
		return name == o.name && value == o.value && gui == o.gui;
	}
};

struct GateInstance {
	std::string uuid;
	std::string libName;   // library gate name, e.g. "AA_AND2"
	XY at;
	double angle = 0;
	std::vector<Param> params;
	bool operator==(const GateInstance &o) const {
		return uuid == o.uuid && libName == o.libName && at == o.at &&
		       angle == o.angle && params == o.params;
	}
};

// One endpoint on a wire segment: a gate (by uuid) and the pin (hotspot) on it.
struct WireConn {
	std::string gateUuid;
	std::string pin;
	bool operator==(const WireConn &o) const { return gateUuid == o.gateUuid && pin == o.pin; }
};

// A point where one segment meets another of the same wire — the branch topology.
// `at` is the coordinate along the segment (x for a horizontal segment, y for a
// vertical one); `segment` is the id of the segment met there.
struct Intersection {
	double at = 0;
	std::string segment;
	bool operator==(const Intersection &o) const { return at == o.at && segment == o.segment; }
};

// One straight run of a wire. Wires are a tree of these, joined at intersections;
// the whole structure is routing the user drew, so it is preserved verbatim.
struct WireSegment {
	std::string id;
	bool vertical = false;                    // authored orientation, not derived from a/b
	XY begin, end;                            // legacy invariant: begin <= end
	std::vector<WireConn> connects;           // gate pins landing on this segment
	std::vector<Intersection> intersections;  // joins to other segments of this wire
	bool operator==(const WireSegment &o) const {
		return id == o.id && vertical == o.vertical && begin == o.begin && end == o.end &&
		       connects == o.connects && intersections == o.intersections;
	}
};

// A wire: one or more IDs (a bus carries several lines) and its segment tree.
struct WireInstance {
	std::vector<std::string> ids;
	std::vector<WireSegment> segments;
	bool operator==(const WireInstance &o) const {
		return ids == o.ids && segments == o.segments;
	}
};

// A mark drawn on a page (the website's docs/DRAWING-NOTES.md 2): pinned to
// the page in world centi-units (world x 100, y up), not to any part. Stored
// quantized, so a save and a load give back exactly what was drawn.
struct InkStroke {
	std::string tool = "pen", color = "ink";   // tokens; unknown ones are kept
	int widthCenti = 25;                       // the full width, 2..2000
	std::vector<std::int32_t> xy;              // absolute x0,y0,x1,y1,... (a dot is one point)
	std::string pressure;                      // "" or one ALPHA char per point
	bool operator==(const InkStroke &o) const {
		return tool == o.tool && color == o.color && widthCenti == o.widthCenti && xy == o.xy &&
		       pressure == o.pressure;
	}
	size_t points() const { return xy.size() / 2; }
};

// A page's drawing. `foreign` holds drawing nodes of a version this reader
// doesn't know, kept whole and written back; such a page is read-only.
struct PageInk {
	std::vector<InkStroke> strokes;
	std::vector<SNode> foreign;
	bool empty() const { return strokes.empty() && foreign.empty(); }
	bool readOnly() const { return !foreign.empty(); }
	bool operator==(const PageInk &o) const { return strokes == o.strokes && foreign == o.foreign; }
};

struct Page {
	int index = 0;
	// What the user named this tab; empty means it is still "Page N".
	std::string name;
	std::vector<GateInstance> gates;
	std::vector<WireInstance> wires;
	PageInk ink;
	bool operator==(const Page &o) const {
		return index == o.index && name == o.name && gates == o.gates && wires == o.wires && ink == o.ink;
	}
};

struct CircuitFile {
	int formatVersion = 3;
	std::string generator;
	std::vector<Page> pages;
	// The student's notes for the whole circuit ("" for none), and whether
	// the drawing is hidden. Only v3 holds them.
	std::string notes;
	bool inkHidden = false;
	// Read only: strokes left out because they couldn't be read.
	int inkDropped = 0;
	bool operator==(const CircuitFile &o) const {
		return formatVersion == o.formatVersion && generator == o.generator &&
		       pages == o.pages && notes == o.notes && inkHidden == o.inkHidden;
	}
};

} // namespace cl
