// CedarCore's document: a circuit read with the wx app's own loader
// (CircuitParse::applyLoaded) into the shared gate and wire model, one page
// model per page, with a LogicHost running its simulation.

#include "DocumentImpl.h"
#include <map>
#include "CGScene.h"
#include "CircuitParse.h"
#include "GUICanvas.h"
#include "GUICircuit.h"
#include "GateLibrary.h"
#include "LibraryParse.h"
#include "LogicHost.h"
#include "Settings.h"
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

// A part locked in place: a small padlock on a disc at its top-right corner,
// the same size on screen at any zoom (smaller only when zoomed far out, so
// it never swamps the part). `upp` is world units per point, `px` device
// pixels per point.
void drawLockBadge(cl::render::Scene& scene, guiGate* g, const cl::render::RenderStyle& style, float upp, float px) {
	using cl::render::Color;
	using cl::render::Point;
	using cl::render::Stroke;
	klsBBox b = g->getSelectionBBox();
	if (b.empty()) b = g->getBBox();
	if (b.empty()) return;
	const float r = std::min(7.0f * upp, 0.6f);   // the disc
	const float u = r / 7.0f;                      // a point, at that size
	const Point c(b.getRight() - 1.5f * u, b.getTop() - 1.5f * u);
	const bool dark = style.darkMode;
	const Color disc = dark ? Color(0.20f, 0.22f, 0.26f, 1) : Color(1, 1, 1, 1);
	const Color rim = dark ? Color(1, 1, 1, 0.22f) : Color(0, 0, 0, 0.22f);
	const Color ink = dark ? Color(0.86f, 0.87f, 0.90f, 1) : Color(0.32f, 0.34f, 0.38f, 1);
	const float w = std::max(0.5f, (u / upp) * px);   // a point's stroke, scaled with the badge
	scene.fillCircle(c, r, disc);
	scene.strokeCircle(c, r, Stroke(rim, w));
	// The body, and the shackle over it.
	scene.fillRect(Point(c.x - 3.4f * u, c.y - 3.6f * u), Point(c.x + 3.4f * u, c.y + 0.6f * u), ink);
	const float sr = 2.1f * u, top = c.y + 1.6f * u;
	Stroke shackle(ink, 1.4f * w);
	scene.arc(Point(c.x, top), sr, -90.0f, 180.0f, shackle);
	Point legs[4] = { Point(c.x - sr, top), Point(c.x - sr, c.y + 0.4f * u),
	                  Point(c.x + sr, top), Point(c.x + sr, c.y + 0.4f * u) };
	scene.lines(legs, 4, shackle);
}

void setError(char* error, int len, const std::string& msg) {
	if (error == nullptr || len <= 0) return;
	std::strncpy(error, msg.c_str(), (size_t)len - 1);
	error[len - 1] = 0;
}

// The page's clocks set to move only on Step Clock. A file from before the
// setting has no MANUAL at all: a running clock.
std::vector<unsigned long> manualClocks(const CLDocument* doc, int pageIndex) {
	std::vector<unsigned long> out;
	GUICanvas* p = doc ? doc->page(pageIndex) : nullptr;
	if (p == nullptr) return out;
	for (auto& g : *p->getGateList()) {
		if (isManualClock(g.second)) out.push_back(g.first);
	}
	return out;
}

}  // namespace

bool isManualClock(guiGate* gate) {
	if (gate == nullptr || gate->getLogicType() != "CLOCK") return false;
	// Looked up, not indexed: getLogicParam would add an empty MANUAL,
	// which the next save would write.
	auto* params = gate->getAllLogicParams();
	auto it = params->find("MANUAL");
	return it != params->end() && it->second == "true";
}

extern "C" {

bool cl_library_load(const char* xmlPath) {
	// The wx app's defaults for what the drawing reads from its settings
	// (MainApp::loadSettings); the app changes them with cl_set_wire_dots.
	static bool defaults = false;
	if (!defaults) {
		defaults = true;
		cl_set_wire_dots(true, 0.18);
		appConfig().appSettings.gridlineVisible = true;
		appConfig().appSettings.majorGridVisible = true;
	}
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
	return clOpenText(data, length, error, errorLen, settleOnOpen);
}

CLDocument* clOpenText(const char* data, long length, char* error, int errorLen, bool settle) {
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
	// The gates were made before their pages joined the document, so their
	// links went to the engine as the shared group's (docs/PAGE-LINKS.md).
	// Numbered from the first page's group (0) so a gate added later agrees.
	std::map<int, int> groups;
	bool any = false;
	for (auto& p : doc->pages) {
		auto it = groups.find(p->linkGroup);
		const int g = it != groups.end() ? it->second : (int)groups.size();
		groups.emplace(p->linkGroup, g);
		p->linkGroup = g;
		any = any || g != 0;
	}
	if (any) doc->sim->relinkJunctions();
	for (const cl::MigrationNotice& n : parser.getLoadNotices()) {
		doc->notices.push_back(n.detail.empty() ? n.summary : n.summary + "\n" + n.detail);
		doc->noticeWarnings.push_back(n.severity == cl::Severity::Warning);
	}
	// Open settled, so the first frame shows real states.
	if (settle) doc->sim->settle();
	return doc.release();
}

void cl_document_close(CLDocument* doc) { delete doc; }

void cl_set_settle_on_open(bool settle) { settleOnOpen = settle; }

CLDocument* cl_document_new(void) {
	CLDocument* doc = new CLDocument();
	doc->pages.emplace_back(new GUICanvas(&doc->circuit));
	return doc;
}

extern "C++" std::string clSaveText(CLDocument* doc) {
	std::vector<GUICanvas*> pages;
	for (auto& p : doc->pages) pages.push_back(p.get());
	CircuitParse writer(pages);
	return writer.textV3(pages);
}

const char* cl_document_save_text(CLDocument* doc) {
	static std::string text;
	text.clear();
	if (doc == nullptr) return "";
	text = clSaveText(doc);
	doc->edited = false;
	return text.c_str();
}

int cl_document_export_legacy(CLDocument* doc, const char* path, int format, char* error, int errorLen) {
	if (doc == nullptr || path == nullptr) return -1;
	std::vector<GUICanvas*> pages;
	for (auto& p : doc->pages) pages.push_back(p.get());
	CircuitParse writer(pages);
	const bool ok = format == 1 ? writer.saveCircuitLegacy(path, pages) : writer.saveCircuit(path, pages);
	std::string why = writer.getLastError();
	// The older formats have no place for a drawing or notes (the website's
	// DRAWING-NOTES.md 4.12): written, with that said.
	const bool lost = cl_ink_any(doc) || !doc->circuit.circuitNotes.empty();
	const std::string loss = "Warning: The drawing and notes aren't kept in this older format.";
	if (ok) {
		if (!lost) return 0;
		setError(error, errorLen, loss);
		return 1;
	}
	const bool warning = why.rfind("Warning:", 0) == 0;
	if (warning && lost) why += "\n" + loss;
	setError(error, errorLen, why);
	// The v1.x writer still writes the file when all it has is a warning.
	return warning ? 1 : -1;
}

int cl_document_add_page(CLDocument* doc) {
	if (doc == nullptr) return -1;
	const int group = clNewPageLinkGroup(doc);
	doc->pages.emplace_back(new GUICanvas(&doc->circuit));
	doc->pages.back()->linkGroup = group;
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

void clDrawPage(CLDocument* doc, int page, CGContextRef ctx,
               double backingScale, double originX, double originY,
               double unitsPerPoint, const cl::render::RenderStyle& style, int inkMode, int inkLook) {
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
	// The drawing (the website's DRAWING-NOTES.md 4.8): highlighter strokes
	// under the parts, like a real highlighter, and pen strokes over them.
	const bool ink = clInkDrawn(doc, inkMode) && !p->ink.strokes.empty();
	if (ink) clDrawInkLayer(scene, p->ink, true, inkLook, scale, style.projector);
	// In id order: the page's lists are hash maps, whose order depends on how
	// they were built, and where things overlap the order shows.
	std::vector<unsigned long> ids;
	for (auto& w : *p->getWireList()) if (w.second) ids.push_back(w.first);
	std::sort(ids.begin(), ids.end());
	// Live dark screen only: paper, thumbnails and Simulation View keep theirs.
	scene.wiresPhase = style.darkMode && style.colorOutput && style.showLiveState && !style.simView;
	for (unsigned long id : ids) (*p->getWireList())[id]->drawToScene(scene, style);
	scene.wiresPhase = false;
	ids.clear();
	for (auto& g : *p->getGateList()) if (g.second) ids.push_back(g.first);
	std::sort(ids.begin(), ids.end());
	for (unsigned long id : ids) (*p->getGateList())[id]->drawToScene(scene, style);
	if (ink) clDrawInkLayer(scene, p->ink, false, inkLook, scale, style.projector);
	// Lock badges: on the canvas being edited only (the styles that show the
	// selection), never in Simulation View, a picture or a thumbnail.
	if (style.showSelection && style.colorOutput && !style.simView)
		for (unsigned long id : ids) {
			guiGate* g = (*p->getGateList())[id];
			if (g->isLocked()) drawLockBadge(scene, g, style, (float)unitsPerPoint, (float)backingScale);
		}
	CGContextRestoreGState(ctx);
}

void cl_document_draw(CLDocument* doc, int page, CGContextRef ctx,
                      double backingScale, double originX, double originY,
                      double unitsPerPoint, bool dark) {
	clDrawPage(doc, page, ctx, backingScale, originX, originY, unitsPerPoint,
	           cl::render::RenderStyle::screen(dark), CL_INK_FOLLOW, dark ? kInkLookDark : kInkLookLight);
}

bool cl_document_draw_fitted(CLDocument* doc, int page, CGContextRef ctx,
                             double width, double height, double margin,
                             double backingScale, int style) {
	return cl_document_draw_fitted_ink(doc, page, ctx, width, height, margin, backingScale, style, false);
}

bool cl_document_draw_fitted_ink(CLDocument* doc, int page, CGContextRef ctx,
                                 double width, double height, double margin,
                                 double backingScale, int style, bool ink) {
	double l, b, r, t;
	if (width <= 2 * margin || height <= 2 * margin) return false;
	bool any = cl_document_page_bounds(doc, page, &l, &b, &r, &t);
	// With the drawing, the picture holds all of it too.
	double il, ib, ir, it;
	if (ink && cl_ink_bounds(doc, page, &il, &ib, &ir, &it)) {
		if (any) { l = std::min(l, il); b = std::min(b, ib); r = std::max(r, ir); t = std::max(t, it); }
		else { l = il; b = ib; r = ir; t = it; any = true; }
	}
	if (!any) return false;
	const double w = std::max(r - l, 1.0), h = std::max(t - b, 1.0);
	const double upp = std::max(w / (width - 2 * margin), h / (height - 2 * margin));
	// Centered: the world point at the top-left corner of the area.
	const double originX = (l + r) / 2 - upp * width / 2;
	const double originY = (b + t) / 2 + upp * height / 2;
	cl::render::RenderStyle rs = style == CL_STYLE_PRINT
		? cl::render::RenderStyle::print()
		: cl::render::RenderStyle::screen(style == CL_STYLE_DARK);
	rs.showSelection = false;
	const int look = style == CL_STYLE_PRINT ? kInkLookBW : style == CL_STYLE_DARK ? kInkLookDark : kInkLookPrint;
	clDrawPage(doc, page, ctx, backingScale, originX, originY, upp, rs, ink ? CL_INK_ALWAYS : CL_INK_NEVER, look);
	return true;
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

int cl_document_manual_clock_count(const CLDocument* doc, int page) {
	return (int)manualClocks(doc, page).size();
}

bool cl_document_clock_step(CLDocument* doc, int page) {
	const std::vector<unsigned long> clocks = manualClocks(doc, page);
	if (clocks.empty()) return false;
	// The level goes straight to the engine: it isn't a setting, and isn't saved.
	for (const char* level : { "1", "0" }) {
		for (unsigned long id : clocks)
			doc->circuit.sendMessageToCore(klsMessage::Message(klsMessage::MT_SET_GATE_PARAM,
				new klsMessage::Message_SET_GATE_PARAM((int)id, "MANUAL_LEVEL", level)));
		doc->sim->settle(1000, true);
	}
	doc->sim->takePauseRequest();
	return true;
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
		// A clock on "Only on Step Clock": the click is a Step Clock.
		if (isManualClock(gate)) return cl_document_clock_step(doc, page);
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
