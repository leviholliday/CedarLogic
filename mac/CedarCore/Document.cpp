// CedarCore's document: a circuit read from a .cdl and built into the same
// gate and wire objects the wx app uses, grouped by page. Mirrors
// CircuitParse::applyCircuitFile, minus the simulator and the wx canvases.

#include "CedarCore.h"
#include "CGScene.h"
#include "GUICircuit.h"
#include "GateLibrary.h"
#include "LibraryParse.h"
#include "guiGate.h"
#include "guiWire.h"
#include "klsBBox.h"
#include "migrate.hpp"
#include "render/RenderStyle.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

struct CLDocument {
	GUICircuit circuit;
	struct Page {
		std::string name;
		std::vector<unsigned long> gates;
		std::vector<unsigned long> wires;   // head bus-line id of each wire
	};
	std::vector<Page> pages;
};

namespace {

bool readFile(const char* path, std::string& out) {
	std::ifstream in(path, std::ios::binary);
	if (!in) return false;
	std::ostringstream ss;
	ss << in.rdbuf();
	out = ss.str();
	return true;
}

void setError(char* error, int len, const std::string& msg) {
	if (error == nullptr || len <= 0) return;
	std::strncpy(error, msg.c_str(), (size_t)len - 1);
	error[len - 1] = 0;
}

std::vector<IDType> idsOf(const cl::WireInstance& w) {
	std::vector<IDType> ids;
	for (const std::string& id : w.ids) ids.push_back(strtoull(id.c_str(), nullptr, 10));
	return ids;
}

// The wire's saved route, as CircuitParse::applyWireShape lays it in. A
// connection naming a gate that isn't on this page is dropped: the routing
// code would dereference it.
void applyWireShape(GUICircuit& circuit, const cl::WireInstance& w) {
	const std::vector<IDType> ids = idsOf(w);
	if (ids.empty()) return;
	guiWire* wire = circuit.getWire(ids.front());
	if (wire == nullptr) return;
	std::map<long, wireSegment> shape;
	for (const cl::WireSegment& ms : w.segments) {
		wireSegment seg;
		seg.verticalSeg = ms.vertical;
		seg.id = strtol(ms.id.c_str(), nullptr, 10);
		seg.begin = GLPoint2f((float)ms.begin.x, (float)ms.begin.y);
		seg.end = GLPoint2f((float)ms.end.x, (float)ms.end.y);
		seg.calcBBox();
		for (const cl::WireConn& c : ms.connects) {
			wireConnection wc;
			wc.gid = strtoul(c.gateUuid.c_str(), nullptr, 10);
			if (circuit.getGate(wc.gid) == nullptr) continue;
			wc.connection = c.pin;
			seg.connections.push_back(wc);
		}
		for (const cl::Intersection& x : ms.intersections)
			seg.intersects[(GLfloat)x.at].push_back(strtol(x.segment.c_str(), nullptr, 10));
		shape[seg.id] = seg;
	}
	if (shape.empty()) return;   // keep the route the connections gave it
	wire->setIDs(ids);
	wire->setSegmentMap(shape);
}

}  // namespace

extern "C" {

bool cl_library_load(const char* xmlPath) {
	std::string xml;
	if (!readFile(xmlPath, xml)) return false;
	LibraryParse lib(xml);
	gateLibrary().libParser = lib;
	return !gateLibrary().libraries.empty();
}

CLDocument* cl_document_open(const char* path, char* error, int errorLen) {
	std::string text;
	if (!readFile(path, text)) { setError(error, errorLen, "The file couldn't be read."); return nullptr; }
	return cl_document_open_text(text.data(), (long)text.size(), error, errorLen);
}

CLDocument* cl_document_open_text(const char* data, long length, char* error, int errorLen) {
	if (data == nullptr || length < 0) { setError(error, errorLen, "The file is empty."); return nullptr; }
	const std::string text(data, (size_t)length);
	cl::LoadResult loaded;
	try {
		loaded = cl::loadCircuit(text);
	} catch (const std::exception& e) {
		setError(error, errorLen, std::string("This circuit file couldn't be read: ") + e.what());
		return nullptr;
	} catch (...) {
		setError(error, errorLen, "This circuit file is damaged or incomplete.");
		return nullptr;
	}
	if (gateLibrary().libraries.empty()) { setError(error, errorLen, "The gate library isn't loaded."); return nullptr; }

	auto doc = std::make_unique<CLDocument>();
	for (const cl::Page& pg : loaded.file.pages) {
		if (pg.index < 0 || pg.index > 255) continue;   // as CircuitParse refuses
		while ((int)doc->pages.size() <= pg.index) doc->pages.push_back(CLDocument::Page());
		CLDocument::Page& page = doc->pages[pg.index];
		page.name = pg.name;

		// Each gate's pin -> wire ids, gathered from the wires.
		std::unordered_map<std::string, std::vector<std::pair<std::string, std::vector<IDType>>>> pins;
		for (const cl::WireInstance& w : pg.wires) {
			const std::vector<IDType> ids = idsOf(w);
			if (ids.empty()) continue;
			std::set<std::pair<std::string, std::string>> seen;
			for (const cl::WireSegment& s : w.segments)
				for (const cl::WireConn& c : s.connects)
					if (seen.insert({ c.gateUuid, c.pin }).second) pins[c.gateUuid].push_back({ c.pin, ids });
		}

		for (const cl::GateInstance& g : pg.gates) {
			const long id = strtol(g.uuid.c_str(), nullptr, 10);
			guiGate* gate = doc->circuit.createGate(g.libName, id, true);
			if (gate == nullptr) continue;
			gate->setGLcoords((float)g.at.x, (float)g.at.y);
			// The library's own drawing params win over ones older files baked
			// in (see CircuitParse::parseGateToSend).
			LibraryGate lib;
			gateLibrary().libParser.getGate(g.libName, lib);
			for (const cl::Param& p : g.params) {
				if (!p.gui) gate->setLogicParam(p.name, p.value);
				else if (!lib.ownsGUIParam(p.name)) gate->setGUIParam(p.name, p.value);
			}
			std::ostringstream angle;
			angle << g.angle;
			gate->setGUIParam("angle", angle.str());
			for (const auto& pin : pins[g.uuid]) doc->circuit.setWireConnection(pin.second, id, pin.first, true);
			page.gates.push_back((unsigned long)id);
		}

		for (const cl::WireInstance& w : pg.wires) {
			applyWireShape(doc->circuit, w);
			const std::vector<IDType> ids = idsOf(w);
			if (!ids.empty() && doc->circuit.getWire(ids.front()) != nullptr) page.wires.push_back(ids.front());
		}
	}
	if (doc->pages.empty()) doc->pages.push_back(CLDocument::Page());
	return doc.release();
}

void cl_document_close(CLDocument* doc) { delete doc; }

int cl_document_page_count(const CLDocument* doc) { return doc ? (int)doc->pages.size() : 0; }

const char* cl_document_page_name(const CLDocument* doc, int page) {
	if (!doc || page < 0 || page >= (int)doc->pages.size()) return "";
	return doc->pages[page].name.c_str();
}

bool cl_document_page_bounds(const CLDocument* doc, int page,
                             double* left, double* bottom, double* right, double* top) {
	if (!doc || page < 0 || page >= (int)doc->pages.size()) return false;
	const CLDocument::Page& p = doc->pages[page];
	klsBBox box;
	bool any = false;
	for (unsigned long id : p.gates)
		if (guiGate* g = doc->circuit.getGate(id)) {
			klsBBox b = g->getBBox();
			if (b.empty()) continue;
			if (!any) box = b; else box.addBBox(b);
			any = true;
		}
	for (unsigned long id : p.wires)
		if (guiWire* w = doc->circuit.getWire(id)) {
			klsBBox b = w->getBBox();
			if (b.empty()) continue;
			if (!any) box = b; else box.addBBox(b);
			any = true;
		}
	if (!any) return false;
	*left = box.getLeft(); *right = box.getRight();
	*bottom = box.getBottom(); *top = box.getTop();
	return true;
}

void cl_document_draw(CLDocument* doc, int page, CGContextRef ctx,
                      double backingScale, double originX, double originY,
                      double unitsPerPoint, bool dark) {
	if (!doc || !ctx || page < 0 || page >= (int)doc->pages.size() || unitsPerPoint <= 0) return;
	const CLDocument::Page& p = doc->pages[page];
	// Work in physical pixels, as the wx app's device space does, so stroke
	// widths (device pixels) match it exactly.
	CGContextSaveGState(ctx);
	CGContextScaleCTM(ctx, 1.0 / backingScale, 1.0 / backingScale);
	const float scale = (float)(backingScale / unitsPerPoint);
	cl::render::Transform t;
	t.a = scale; t.b = 0; t.c = 0; t.d = -scale;
	t.e = (float)(-originX * scale); t.f = (float)(originY * scale);
	cl::mac::CGScene scene(ctx);
	scene.setViewport(t);
	const cl::render::RenderStyle style = cl::render::RenderStyle::screen(dark);
	for (unsigned long id : p.wires)
		if (guiWire* w = doc->circuit.getWire(id)) w->drawToScene(scene, style);
	for (unsigned long id : p.gates)
		if (guiGate* g = doc->circuit.getGate(id)) g->drawToScene(scene, style);
	CGContextRestoreGState(ctx);
}

}  // extern "C"
