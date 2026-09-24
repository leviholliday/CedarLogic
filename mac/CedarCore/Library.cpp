// The gate library for the native palette: categories and gates in the order
// the wx palette shows them (the library maps, which sort by name), and a gate
// type drawn into a tile, the way gateImage renders the wx palette's tiles.

#include "CedarCore.h"
#include "CGScene.h"
#include "GUICircuit.h"
#include "GateLibrary.h"
#include "guiGate.h"
#include "render/RenderStyle.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>

namespace {

const std::map<std::string, LibraryGate>* category(int index) {
	auto& libs = gateLibrary().libraries;
	if (index < 0 || index >= (int)libs.size()) return nullptr;
	auto it = libs.begin();
	std::advance(it, index);
	return &it->second;
}

// One scratch circuit to build sample gates in; each type is made once.
guiGate* sample(const std::string& name) {
	static GUICircuit scratch;
	static std::map<std::string, unsigned long> made;
	auto it = made.find(name);
	if (it != made.end()) return scratch.getGate(it->second);
	guiGate* g = scratch.createGate(name, -1, true);
	if (g == nullptr) return nullptr;
	g->setGLcoords(0, 0);
	made[name] = g->getID();
	return g;
}

std::string scratchText;

}  // namespace

extern "C" {

int cl_library_category_count(void) { return (int)gateLibrary().libraries.size(); }

const char* cl_library_category(int index) {
	auto& libs = gateLibrary().libraries;
	if (index < 0 || index >= (int)libs.size()) return "";
	auto it = libs.begin();
	std::advance(it, index);
	return it->first.c_str();
}

int cl_library_gate_count(int index) {
	const auto* c = category(index);
	return c ? (int)c->size() : 0;
}

const char* cl_library_gate(int index, int gate) {
	const auto* c = category(index);
	if (c == nullptr || gate < 0 || gate >= (int)c->size()) return "";
	auto it = c->begin();
	std::advance(it, gate);
	return it->first.c_str();
}

const char* cl_library_gate_caption(const char* name) {
	scratchText.clear();
	if (name == nullptr) return "";
	LibraryGate lg;
	if (gateLibrary().libParser.getGate(name, lg)) scratchText = lg.caption.empty() ? lg.gateName : lg.caption;
	return scratchText.c_str();
}

void cl_library_draw_gate(const char* name, CGContextRef ctx, double width, double height,
                          double backingScale, bool dark) {
	if (name == nullptr || ctx == nullptr || width <= 0 || height <= 0) return;
	guiGate* g = sample(name);
	if (g == nullptr) return;
	klsBBox box = g->getModelDrawBBox();
	if (box.empty()) return;
	// Fit the gate with a little padding, as gateImage does.
	const double pad = 0.5;
	const double w = box.getRight() - box.getLeft() + 2 * pad, h = box.getTop() - box.getBottom() + 2 * pad;
	const double scalePts = std::min(width / w, height / h);
	const double cx = (box.getLeft() + box.getRight()) / 2, cy = (box.getBottom() + box.getTop()) / 2;
	CGContextSaveGState(ctx);
	CGContextScaleCTM(ctx, 1.0 / backingScale, 1.0 / backingScale);
	const float s = (float)(scalePts * backingScale);
	cl::render::Transform t;
	t.a = s; t.d = -s;
	t.e = (float)(width * backingScale / 2 - cx * s);
	t.f = (float)(height * backingScale / 2 + cy * s);
	cl::mac::CGScene scene(ctx);
	scene.setViewport(t);
	g->drawToScene(scene, cl::render::RenderStyle::thumbnail(dark));
	CGContextRestoreGState(ctx);
}

}  // extern "C"
