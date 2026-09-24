// CedarCore's document: a circuit read with the wx app's own loader
// (CircuitParse::applyLoaded) into the shared gate and wire model, one page
// model per page, with a LogicHost running its simulation.

#include "DocumentImpl.h"
#include "CGScene.h"
#include "CircuitParse.h"
#include "GUICanvas.h"
#include "GUICircuit.h"
#include "GateLibrary.h"
#include "LibraryParse.h"
#include "LogicHost.h"
#include "guiGate.h"
#include "guiWire.h"
#include "klsBBox.h"
#include "render/RenderStyle.h"
#include "command/cmdDeleteSelection.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool settleOnOpen = true;

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
	if (gateLibrary().libraries.empty()) { setError(error, errorLen, "The gate library isn't loaded."); return nullptr; }
	cl::LoadResult loaded;
	std::string why;
	if (!CircuitParse::readCircuitText(std::string(data, (size_t)length), loaded, why)) {
		setError(error, errorLen, why);
		return nullptr;
	}

	auto doc = std::make_unique<CLDocument>();
	// The loader grows the page list as the file names pages; start it with one.
	std::vector<GUICanvas*> canvases{ new GUICanvas(&doc->circuit) };
	CircuitParse parser(canvases);
	canvases = parser.applyLoaded(loaded);
	for (GUICanvas* c : canvases) doc->pages.emplace_back(c);
	for (const cl::MigrationNotice& n : parser.getLoadNotices()) {
		doc->notices.push_back(n.detail.empty() ? n.summary : n.summary + "\n" + n.detail);
		doc->noticeWarnings.push_back(n.severity == cl::Severity::Warning);
	}
	// Open settled, so the first frame shows real states.
	if (settleOnOpen) doc->sim->settle();
	return doc.release();
}

void cl_document_close(CLDocument* doc) { delete doc; }

void cl_set_settle_on_open(bool settle) { settleOnOpen = settle; }

CLDocument* cl_document_new(void) {
	CLDocument* doc = new CLDocument();
	doc->pages.emplace_back(new GUICanvas(&doc->circuit));
	return doc;
}

const char* cl_document_save_text(CLDocument* doc) {
	static std::string text;
	text.clear();
	if (doc == nullptr) return "";
	std::vector<GUICanvas*> pages;
	for (auto& p : doc->pages) pages.push_back(p.get());
	CircuitParse writer(pages);
	text = writer.textV3(pages);
	doc->edited = false;
	return text.c_str();
}

int cl_document_add_page(CLDocument* doc) {
	if (doc == nullptr) return -1;
	doc->pages.emplace_back(new GUICanvas(&doc->circuit));
	doc->edited = true;
	return (int)doc->pages.size() - 1;
}

void cl_document_rename_page(CLDocument* doc, int page, const char* name) {
	GUICanvas* p = doc ? doc->page(page) : nullptr;
	if (p == nullptr || name == nullptr) return;
	p->name = name;
	doc->edited = true;
}

void cl_document_delete_page(CLDocument* doc, int page) {
	GUICanvas* p = doc ? doc->page(page) : nullptr;
	if (p == nullptr || doc->pages.size() < 2) return;
	if (doc->tidy.active) cl_edit_tidy_end(doc, true);
	doc->gesture = EditGesture();
	// Delete what's on it through the usual command (it tells the simulator),
	// then drop the page and the history that might point at it.
	std::vector<unsigned long> gates, wires;
	for (auto& g : *p->getGateList()) gates.push_back(g.first);
	for (auto& w : *p->getWireList()) wires.push_back(w.first);
	if (!gates.empty() || !wires.empty()) {
		cmdDeleteSelection del(&doc->circuit, p, gates, wires);
		del.Do();
	}
	doc->circuit.GetCommandProcessor()->ClearCommands();
	doc->pages.erase(doc->pages.begin() + page);
	doc->edited = true;
}

int cl_document_page_count(const CLDocument* doc) { return doc ? (int)doc->pages.size() : 0; }

const char* cl_document_page_name(const CLDocument* doc, int page) {
	GUICanvas* p = doc ? doc->page(page) : nullptr;
	return p ? p->name.c_str() : "";
}

bool cl_document_page_bounds(const CLDocument* doc, int page,
                             double* left, double* bottom, double* right, double* top) {
	GUICanvas* p = doc ? doc->page(page) : nullptr;
	if (p == nullptr) return false;
	klsBBox box;
	bool any = false;
	auto add = [&](klsBBox b) {
		if (b.empty()) return;
		if (!any) box = b; else box.addBBox(b);
		any = true;
	};
	for (auto& g : *p->getGateList()) if (g.second) add(g.second->getBBox());
	for (auto& w : *p->getWireList()) if (w.second) add(w.second->getBBox());
	if (!any) return false;
	*left = box.getLeft(); *right = box.getRight();
	*bottom = box.getBottom(); *top = box.getTop();
	return true;
}

void cl_document_draw(CLDocument* doc, int page, CGContextRef ctx,
                      double backingScale, double originX, double originY,
                      double unitsPerPoint, bool dark) {
	GUICanvas* p = doc ? doc->page(page) : nullptr;
	if (p == nullptr || ctx == nullptr || unitsPerPoint <= 0) return;
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
	// In id order: the page's lists are hash maps, whose order depends on how
	// they were built, and where things overlap the order shows.
	std::vector<unsigned long> ids;
	for (auto& w : *p->getWireList()) if (w.second) ids.push_back(w.first);
	std::sort(ids.begin(), ids.end());
	for (unsigned long id : ids) (*p->getWireList())[id]->drawToScene(scene, style);
	ids.clear();
	for (auto& g : *p->getGateList()) if (g.second) ids.push_back(g.first);
	std::sort(ids.begin(), ids.end());
	for (unsigned long id : ids) (*p->getGateList())[id]->drawToScene(scene, style);
	CGContextRestoreGState(ctx);
}

int cl_document_tick(CLDocument* doc, double elapsedMs) {
	if (doc == nullptr || !doc->running) return 0;
	doc->carryMs += std::max(0.0, elapsedMs);
	int steps = (int)(doc->carryMs / doc->stepMs);
	if (steps <= 0) return 0;
	doc->carryMs -= steps * (double)doc->stepMs;
	// After a stall (the Mac slept, the window was hidden) don't grind through
	// minutes of backlog: drop it.
	const int kMaxCatchUp = 200;
	if (steps > kMaxCatchUp) { steps = kMaxCatchUp; doc->carryMs = 0; }
	doc->sim->step(steps);
	int flags = CL_TICK_CHANGED;
	if (doc->sim->takePauseRequest()) { doc->running = false; flags |= CL_TICK_PAUSED; }
	return flags;
}

void cl_document_set_running(CLDocument* doc, bool running) {
	if (doc == nullptr) return;
	doc->running = running;
	doc->carryMs = 0;
}

bool cl_document_is_running(const CLDocument* doc) { return doc && doc->running; }

void cl_document_step(CLDocument* doc) {
	if (doc == nullptr) return;
	doc->sim->step(1);
	doc->sim->takePauseRequest();
}

void cl_document_set_step_ms(CLDocument* doc, int ms) {
	if (doc) doc->stepMs = std::min(500, std::max(1, ms));
}

int cl_document_step_ms(const CLDocument* doc) { return doc ? doc->stepMs : 25; }

bool cl_document_click(CLDocument* doc, int page, double x, double y) {
	GUICanvas* p = doc ? doc->page(page) : nullptr;
	if (p == nullptr) return false;
	for (auto& g : *p->getGateList()) {
		guiGate* gate = g.second;
		if (gate == nullptr) continue;
		klsBBox box = gate->getBBox();
		if (!box.contains(GLPoint2f((float)x, (float)y))) continue;
		if (klsMessage::Message_SET_GATE_PARAM* msg = gate->checkClick((float)x, (float)y)) {
			doc->circuit.sendMessageToCore(klsMessage::Message(klsMessage::MT_SET_GATE_PARAM, msg));
			// Let the gate answer now (a keypad's new value, an LED) rather than
			// on the next step, so a paused circuit still responds.
			doc->circuit.sendMessageToCore(klsMessage::Message(klsMessage::MT_UPDATE_GATES));
			return true;
		}
	}
	return false;
}

int cl_document_notice_count(const CLDocument* doc) { return doc ? (int)doc->notices.size() : 0; }

const char* cl_document_notice(const CLDocument* doc, int index) {
	if (!doc || index < 0 || index >= (int)doc->notices.size()) return "";
	return doc->notices[index].c_str();
}

bool cl_document_notice_is_warning(const CLDocument* doc, int index) {
	return doc && index >= 0 && index < (int)doc->noticeWarnings.size() && doc->noticeWarnings[index];
}

}  // extern "C"
