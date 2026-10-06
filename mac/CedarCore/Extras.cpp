// What the CedarLogic interface adds on top of the basics: drawing with the
// Appearance settings, Simulation View's signal flow, pages that close with
// an undo, gates that follow the pointer until dropped, connect-nearby (C),
// the memory editor, and what the guided tour watches. Each piece follows the
// wx code it's named after (GUICanvas, cmdDeleteTab, RamPopupDialog, Welcome).

#include "DocumentImpl.h"
#include "CGScene.h"
#include "Settings.h"
#include "guiGate.h"
#include "guiWire.h"
#include "render/RenderStyle.h"
#include "render/SkiaProbe.h"   // measuredTextWidth (Core Text, in CGScene.cpp)
#include "command/cmdCreateWire.h"
#include "command/cmdDeleteSelection.h"
#include "command/cmdMoveSelection.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <queue>
#include <sstream>

namespace {

// The camera cl_document_draw uses: device pixels, world y up.
struct Camera {
	CGContextRef ctx;
	cl::render::Transform t;
	Camera(CGContextRef ctx, double backingScale, double originX, double originY, double unitsPerPoint) : ctx(ctx) {
		CGContextSaveGState(ctx);
		CGContextScaleCTM(ctx, 1.0 / backingScale, 1.0 / backingScale);
		const float scale = (float)(backingScale / unitsPerPoint);
		t.a = scale; t.b = 0; t.c = 0; t.d = -scale;
		t.e = (float)(-originX * scale); t.f = (float)(originY * scale);
	}
	~Camera() { CGContextRestoreGState(ctx); }
};

bool firstPinLit(guiGate* g) {
	for (auto& hs : g->getHotspotList()) {
		if (!g->isConnected(hs.first)) continue;
		const std::vector<StateType>& st = g->getConnection(hs.first)->getState();
		return !st.empty() && st[0] == ONE;
	}
	return false;
}

}  // namespace

extern "C" {

// ---- Drawing ----------------------------------------------------------------

void cl_document_draw_ex(CLDocument* doc, int page, CGContextRef ctx, double backingScale,
                         double originX, double originY, double unitsPerPoint,
                         const CLDrawOptions* o) {
	if (o == nullptr) return;
	using cl::render::RenderStyle;
	RenderStyle s = o->thumbnail ? RenderStyle::thumbnail(o->dark) : RenderStyle::screen(o->dark);
	if (!o->thumbnail) {
		s.accentIndex = o->accent;
		s.wireScale = (float)o->wireScale;
		s.simView = o->simView;
		s.showSelection = o->showSelection && !o->simView;
		s.selectionFade = (float)std::min(1.0, std::max(0.0, o->selectionFade));
	}
	clDrawPage(doc, page, ctx, backingScale, originX, originY, unitsPerPoint, s);
}

void cl_accent_color(int index, bool dark, double* r, double* g, double* b) {
	cl::render::RenderStyle s;
	s.darkMode = dark;
	s.accentIndex = index;
	const cl::render::Color c = s.accent();
	if (r) *r = c.r;
	if (g) *g = c.g;
	if (b) *b = c.b;
}

void cl_set_wire_dots(bool atBends, double radius) {
	appConfig().appSettings.wireConnVisible = atBends;
	appConfig().appSettings.wireConnRadius = (float)radius;
}

void cl_set_low_wire_color(bool on, double r, double g, double b) {
	cl::mac::CGScene::remapLowWire = on;
	cl::mac::CGScene::lowWire = cl::render::Color((float)r, (float)g, (float)b, 1);
}

int cl_document_gate_count(const CLDocument* doc, int page) {
	GUICanvas* p = doc ? doc->page(page) : nullptr;
	return p ? (int)p->getGateList()->size() : 0;
}

// ---- Simulation View ------------------------------------------------------------
// GUICanvas::drawSignalFlowInto: dashes cover wherever (distance from the
// driving pin - phase) mod period is under the dash length, so as the phase
// grows they march away from the driver, splitting at branches.

void cl_simview_draw_flow(CLDocument* doc, int pageIndex, CGContextRef ctx, double backingScale,
                          double originX, double originY, double unitsPerPoint, double phasePoints,
                          double wireScale) {
	GUICanvas* page = doc ? doc->page(pageIndex) : nullptr;
	if (page == nullptr || ctx == nullptr || unitsPerPoint <= 0) return;
	using cl::render::Color;
	using cl::render::Point;
	using cl::render::Stroke;
	Camera cam(ctx, backingScale, originX, originY, unitsPerPoint);
	cl::mac::CGScene scene(ctx);
	scene.setViewport(cam.t);

	const float z = (float)unitsPerPoint;
	const float period = 16.0f * z, dashLen = 7.0f * z;
	const float phase = (float)std::fmod(phasePoints, 16.0) * z;
	cl::render::RenderStyle style = cl::render::RenderStyle::screen(true);
	style.simView = true;
	const Color on = style.simOn();
	const float px = (float)backingScale;
	Stroke glow(Color(on.r, on.g, on.b, 0.35f), 2.5f * px * (float)wireScale);
	glow.cap = cl::render::Cap::Round;
	Stroke core(Color(0.88f, 1.0f, 1.0f, 0.95f), 0.9f * px * (float)wireScale);
	core.cap = cl::render::Cap::Round;

	// Lit lights bloom, under the dashes.
	for (auto& ge : *page->getGateList()) {
		guiGateLED* led = dynamic_cast<guiGateLED*>(ge.second);
		if (led == nullptr || !firstPinLit(led)) continue;
		float gx, gy;
		led->getGLcoords(gx, gy);
		scene.fillCircle(Point(gx, gy), 2.8f, Color(on.r, on.g, on.b, 0.07f));
		scene.fillCircle(Point(gx, gy), 1.9f, Color(on.r, on.g, on.b, 0.12f));
		scene.fillCircle(Point(gx, gy), 1.25f, Color(on.r, on.g, on.b, 0.20f));
	}

	std::vector<Point> pts;
	int budget = 6000;   // dashes per frame, so a huge circuit can't stall drawing
	for (auto& we : *page->getWireList()) {
		guiWire* w = we.second;
		if (w == nullptr || budget <= 0 || we.first != w->getID()) continue;
		bool anyOn = false;
		for (StateType v : w->getState()) if (v == ONE) { anyOn = true; break; }
		if (!anyOn) continue;

		// The pin driving this wire: the one that's a gate output.
		wireConnection driver;
		bool haveDriver = false;
		for (const wireConnection& c : w->getConnections()) {
			guiGate* g = doc->circuit.getGate(c.gid);
			if (g != nullptr && !g->isConnectionInput(c.connection)) { driver = c; haveDriver = true; break; }
		}
		if (!haveDriver) continue;
		float px0, py0;
		doc->circuit.getGate(driver.gid)->getHotspotCoords(driver.connection, px0, py0);

		const std::map<long, wireSegment> segs = w->getSegmentMap();
		long start = -1;
		for (const auto& se : segs) {
			for (const wireConnection& c : se.second.connections)
				if (c.gid == driver.gid && c.connection == driver.connection) { start = se.first; break; }
			if (start >= 0) break;
		}
		if (start < 0) continue;

		auto axisOf = [](const wireSegment& sg, const GLPoint2f& p) { return sg.isHorizontal() ? p.x : p.y; };
		std::map<long, float> dist, entry;
		typedef std::pair<float, long> QItem;
		std::priority_queue<QItem, std::vector<QItem>, std::greater<QItem>> q;
		{
			const wireSegment& s0 = segs.at(start);
			const float a0 = axisOf(s0, s0.begin), a1 = axisOf(s0, s0.end);
			const float pinAxis = s0.isHorizontal() ? px0 : py0;
			entry[start] = std::min(std::max(pinAxis, std::min(a0, a1)), std::max(a0, a1));
			dist[start] = 0.0f;
			q.push(QItem(0.0f, start));
		}
		while (!q.empty()) {
			const QItem top = q.top(); q.pop();
			if (top.first > dist[top.second] + 1e-5f) continue;
			const wireSegment& sg = segs.at(top.second);
			for (const auto& ix : sg.intersects) {
				const float d = top.first + std::fabs(ix.first - entry[top.second]);
				const float otherAxis = sg.isHorizontal() ? sg.begin.y : sg.begin.x;
				for (long o : ix.second) {
					if (segs.find(o) == segs.end()) continue;
					auto it = dist.find(o);
					if (it != dist.end() && it->second <= d + 1e-5f) continue;
					dist[o] = d;
					entry[o] = otherAxis;
					q.push(QItem(d, o));
				}
			}
		}
		for (const auto& de : dist) {
			const wireSegment& sg = segs.at(de.first);
			const bool horiz = sg.isHorizontal();
			const float a0 = std::min(axisOf(sg, sg.begin), axisOf(sg, sg.end));
			const float a1 = std::max(axisOf(sg, sg.begin), axisOf(sg, sg.end));
			const float fixed = horiz ? sg.begin.y : sg.begin.x;
			const float e = entry[de.first], D = de.second;
			auto at = [&](float a) { return horiz ? Point(a, fixed) : Point(fixed, a); };
			for (int dir = -1; dir <= 1; dir += 2) {
				const float len = dir < 0 ? e - a0 : a1 - e;
				if (len <= 0.0f) continue;
				float u = std::fmod(phase - D, period);
				if (u > 0) u -= period;
				for (; u < len && budget > 0; u += period, budget--) {
					const float u0 = std::max(0.0f, u), u1 = std::min(len, u + dashLen);
					if (u1 <= u0) continue;
					pts.push_back(at(e + dir * u0));
					pts.push_back(at(e + dir * u1));
				}
			}
		}
	}
	if (!pts.empty()) {
		scene.lines(&pts[0], pts.size(), glow);
		scene.lines(&pts[0], pts.size(), core);
	}
}

int cl_simview_chips(CLDocument* doc, int pageIndex, CLSimChip* out, int max) {
	GUICanvas* page = doc ? doc->page(pageIndex) : nullptr;
	if (page == nullptr) return 0;
	struct Chip { float y, x; bool in, lit; };
	std::vector<Chip> ins, outs;
	for (auto& ge : *page->getGateList()) {
		guiGate* g = ge.second;
		const bool isIn = dynamic_cast<guiGateTOGGLE*>(g) != nullptr;
		const bool isOut = dynamic_cast<guiGateLED*>(g) != nullptr;
		if (!isIn && !isOut) continue;
		bool lit = firstPinLit(g);
		if (isIn && !lit) lit = g->getLogicParam("OUTPUT_NUM") == "1";
		float gx, gy;
		g->getGLcoords(gx, gy);
		(isIn ? ins : outs).push_back({ gy, gx, isIn, lit });
	}
	auto byPlace = [](const Chip& a, const Chip& b) { return a.y != b.y ? a.y > b.y : a.x < b.x; };
	std::sort(ins.begin(), ins.end(), byPlace);
	std::sort(outs.begin(), outs.end(), byPlace);
	int n = 0;
	for (auto* list : { &ins, &outs })
		for (const Chip& c : *list) {
			if (n < max && out) out[n] = CLSimChip{ c.in, c.lit };
			n++;
		}
	return n;
}

// ---- Simulation View in a classroom: projector mode, Predict, then reveal ----------

void cl_simview_draw_page(CLDocument* doc, int page, CGContextRef ctx, double backingScale,
                          double originX, double originY, double unitsPerPoint, const CLSimViewStyle* o) {
	if (o == nullptr) return;
	cl::render::RenderStyle s = cl::render::RenderStyle::screen(true);
	s.simView = true;
	s.showSelection = false;
	s.accentIndex = o->accent;
	s.wireScale = (float)o->wireScale;
	s.projector = o->projector;
	s.coverOutputs = o->predict;
	clDrawPage(doc, page, ctx, backingScale, originX, originY, unitsPerPoint, s);
}

}  // extern "C"

namespace {

// A light (an LED, not a junction, which is drawn by the same class) or a
// display (a register's value box).
bool isLight(guiGate* g) {
	return dynamic_cast<guiGateLED*>(g) != nullptr && g->getLibraryGateName().find("JUNC") == std::string::npos;
}
bool isDisplay(guiGate* g) { return dynamic_cast<guiGateREGISTER*>(g) != nullptr; }

// "minx,miny,maxx,maxy" from a gui parameter, in the gate's own units.
bool modelBox(guiGate* g, float& x0, float& y0, float& x1, float& y1) {
	std::string v = g->getGUIParam(isDisplay(g) ? "VALUE_BOX" : "LED_BOX");
	std::replace(v.begin(), v.end(), ',', ' ');
	std::istringstream iss(v);
	if (!(iss >> x0 >> y0 >> x1 >> y1)) return false;
	if (x0 > x1) std::swap(x0, x1);
	if (y0 > y1) std::swap(y0, y1);
	return true;
}

void toWorld(guiGate* g, float x, float y, float& wx, float& wy) {
	const GLdouble* m = g->getModelMatrix();
	wx = (float)(m[0] * x + m[4] * y + m[12]);
	wy = (float)(m[1] * x + m[5] * y + m[13]);
}

bool fromWorld(guiGate* g, float wx, float wy, float& x, float& y) {
	const GLdouble* m = g->getModelMatrix();
	const double det = m[0] * m[5] - m[4] * m[1];
	if (std::fabs(det) < 1e-9) return false;
	const double dx = wx - m[12], dy = wy - m[13];
	x = (float)(( m[5] * dx - m[4] * dy) / det);
	y = (float)((-m[1] * dx + m[0] * dy) / det);
	return true;
}

int displayDigits(guiGate* g) {
	const int bits = std::atoi(g->getLogicParam("INPUT_BITS").c_str());
	return std::max(1, std::min(8, (bits + 3) / 4));
}

CLSimLight lightInfo(guiGate* g) {
	CLSimLight l{};
	l.gate = (long)g->getID();
	l.value = -1;
	if (isDisplay(g)) {
		l.digits = displayDigits(g);
		if (g->getLogicParam("UNKNOWN_OUTPUTS") != "true" && !g->getLogicParam("CURRENT_VALUE").empty()) {
			const long long v = std::atoll(g->getLogicParam("CURRENT_VALUE").c_str());
			l.value = (int)(v & ((1LL << (4 * l.digits)) - 1));
		}
	} else {
		for (auto& hs : g->getHotspotList()) {
			if (!g->isConnected(hs.first)) continue;
			const std::vector<StateType>& st = g->getConnection(hs.first)->getState();
			if (!st.empty() && (st[0] == ONE || st[0] == ZERO)) l.value = st[0] == ONE ? 1 : 0;
			break;
		}
	}
	float x0, y0, x1, y1;
	if (modelBox(g, x0, y0, x1, y1)) {
		float ax, ay, bx, by;
		toWorld(g, x0, y0, ax, ay);
		toWorld(g, x1, y1, bx, by);
		l.left = std::min(ax, bx); l.right = std::max(ax, bx);
		l.bottom = std::min(ay, by); l.top = std::max(ay, by);
	} else {
		klsBBox b = g->getBBox();
		l.left = b.getLeft(); l.right = b.getRight(); l.bottom = b.getBottom(); l.top = b.getTop();
	}
	return l;
}

std::string hexText(int value, int digits) {
	std::string s;
	for (int i = digits - 1; i >= 0; i--) s += "0123456789ABCDEF"[(value >> (4 * i)) & 0xF];
	return s;
}

}  // namespace

extern "C" {

int cl_simview_lights(CLDocument* doc, int pageIndex, CLSimLight* out, int max) {
	GUICanvas* page = doc ? doc->page(pageIndex) : nullptr;
	if (page == nullptr) return 0;
	std::vector<std::pair<std::pair<float, float>, guiGate*>> found;
	for (auto& ge : *page->getGateList()) {
		guiGate* g = ge.second;
		if (g == nullptr || !(isLight(g) || isDisplay(g))) continue;
		float x, y;
		g->getGLcoords(x, y);
		found.push_back({ { -y, x }, g });   // top to bottom, then left to right
	}
	std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
		return a.first != b.first ? a.first < b.first : a.second->getID() < b.second->getID();
	});
	int n = 0;
	for (auto& f : found) {
		if (n < max && out) out[n] = lightInfo(f.second);
		n++;
	}
	return n;
}

long cl_simview_light_at(CLDocument* doc, int pageIndex, double x, double y, int* digit) {
	GUICanvas* page = doc ? doc->page(pageIndex) : nullptr;
	if (digit) *digit = 0;
	if (page == nullptr) return -1;
	for (auto& ge : *page->getGateList()) {
		guiGate* g = ge.second;
		if (g == nullptr || !(isLight(g) || isDisplay(g))) continue;
		klsBBox b = g->getBBox();
		b.extendTop(0.3f); b.extendBottom(0.3f); b.extendLeft(0.3f); b.extendRight(0.3f);
		if (!b.contains(GLPoint2f((float)x, (float)y))) continue;
		if (digit && isDisplay(g)) {
			float lx, ly, x0, y0, x1, y1;
			if (fromWorld(g, (float)x, (float)y, lx, ly) && modelBox(g, x0, y0, x1, y1) && x1 > x0) {
				const int n = displayDigits(g);
				*digit = std::max(0, std::min(n - 1, (int)std::floor((lx - x0) / ((x1 - x0) / n))));
			}
		}
		return (long)g->getID();
	}
	return -1;
}

int cl_simview_light_name(CLDocument* doc, int pageIndex, long gate, char* buf, int len) {
	GUICanvas* page = doc ? doc->page(pageIndex) : nullptr;
	std::string name;
	guiGate* light = page ? doc->circuit.getGate(gate) : nullptr;
	if (light != nullptr) {
		float px, py;
		light->getGLcoords(px, py);
		float best = 8.0f;
		for (auto& ge : *page->getGateList()) {
			if (dynamic_cast<guiLabel*>(ge.second) == nullptr) continue;
			const std::string text = ge.second->getGUIParam("LABEL_TEXT");
			if (text.empty() || text.size() > 16) continue;
			float lx, ly;
			ge.second->getGLcoords(lx, ly);
			const float d = std::hypot(lx - px, ly - py);
			if (d < best) { best = d; name = text; }
		}
	}
	if (buf && len > 0) {
		std::strncpy(buf, name.c_str(), (size_t)len - 1);
		buf[len - 1] = 0;
	}
	return (int)name.size();
}

void cl_simview_draw_predict(CLDocument* doc, int pageIndex, CGContextRef ctx, double backingScale,
                             double originX, double originY, double unitsPerPoint,
                             const CLPredictMark* marks, int count, bool projector) {
	GUICanvas* page = doc ? doc->page(pageIndex) : nullptr;
	if (page == nullptr || ctx == nullptr || unitsPerPoint <= 0 || marks == nullptr) return;
	using cl::render::Color;
	using cl::render::Point;
	using cl::render::Stroke;
	Camera cam(ctx, backingScale, originX, originY, unitsPerPoint);
	cl::mac::CGScene scene(ctx);
	scene.setViewport(cam.t);
	const float px = (float)backingScale * (projector ? 1.6f : 1.0f);   // a point, in device pixels

	auto ring = [&](float l, float b, float r, float t, float out, const Color& c, float width) {
		const Point box[] = { Point(l - out, b - out), Point(r + out, b - out), Point(r + out, t + out), Point(l - out, t + out) };
		scene.polyline(box, 4, Stroke(c, width), true);
	};
	// Text centred on (cx, cy): `h` is the text's pixel height (world units
	// here); Helvetica's capitals are 0.717 of it.
	auto centred = [&](const std::string& text, float cx, float cy, float h, const Color& c) {
		const float w = cl::render::measuredTextWidth(text.c_str(), h);
		scene.text(Point(cx - w / 2, cy + 0.717f * h / 2), text.c_str(), h, c);
	};

	for (int i = 0; i < count; i++) {
		const CLPredictMark& m = marks[i];
		guiGate* g = doc->circuit.getGate(m.gate);
		if (g == nullptr || !(isLight(g) || isDisplay(g))) continue;
		CLSimLight l = lightInfo(g);
		float L = (float)l.left, B = (float)l.bottom, R = (float)l.right, T = (float)l.top;
		if (projector && l.digits == 0) {   // the lit square is drawn bigger (guiGateLED)
			const float k = 1.3f, cx = (L + R) / 2, cy = (B + T) / 2;
			L = cx + (L - cx) * k; R = cx + (R - cx) * k; B = cy + (B - cy) * k; T = cy + (T - cy) * k;
		}
		const float cx = (L + R) / 2, cy = (B + T) / 2, H = T - B;
		const bool guessed = m.guess >= 0;
		const std::string guessText = !guessed ? "?" : l.digits == 0 ? std::string(1, m.guess ? '1' : '0')
		                                                             : hexText(m.guess, l.digits);
		if (m.mark == CL_PREDICT_COVERED) {
			// A card over the light: "?" until guessed, then the guess in amber.
			const float pad = 0.22f;
			scene.fillRect(Point(L - pad, B - pad), Point(R + pad, T + pad), Color(0.11f, 0.14f, 0.18f, 1));
			ring(L, B, R, T, pad, Color(0.50f, 0.60f, 0.70f, 1), 1.25f * px);
			const float h = l.digits == 0 ? std::max(H * 0.95f, 1.0f) : H * 0.62f;
			centred(guessText, cx, cy, std::min(h, (R - L) * 1.6f / std::max(1, (int)guessText.size())),
			        guessed ? Color(1.0f, 0.80f, 0.32f, 1) : Color(0.66f, 0.76f, 0.86f, 1));
			if (m.focused) ring(L, B, R, T, pad + 0.32f, Color(0.28f, 0.93f, 1.0f, 1), 2.0f * px);
			continue;
		}
		// Revealed: a ring that says how the guess did, and the guess above.
		const Color c = m.mark == CL_PREDICT_RIGHT ? Color(0.30f, 0.92f, 0.45f, 1)
		              : m.mark == CL_PREDICT_WRONG ? Color(1.0f, 0.36f, 0.36f, 1)
		                                           : Color(0.56f, 0.60f, 0.66f, 1);
		ring(L, B, R, T, 0.32f, c, 2.6f * px);
		if (m.focused) ring(L, B, R, T, 0.75f, Color(0.28f, 0.93f, 1.0f, 0.8f), 1.5f * px);
		if (!guessed) continue;
		const std::string tag = "Guess " + guessText;
		const float th = projector ? 1.15f : 0.9f;
		const float tw = cl::render::measuredTextWidth(tag.c_str(), th);
		const float ty = T + 0.32f + 0.35f + th * 0.6f;   // the tag's middle, above the ring
		scene.fillRect(Point(cx - tw / 2 - 0.3f, ty - th * 0.6f), Point(cx + tw / 2 + 0.3f, ty + th * 0.6f),
		               Color(0.05f, 0.07f, 0.09f, 0.92f));
		centred(tag, cx, ty, th, c);
	}
}

}  // extern "C"

// ---- Pages that close with an undo ------------------------------------------------
// cmdDeleteTab: what's on the page is deleted through the usual command (so
// the simulator hears of it) and the page itself is kept here, off the
// document, until an undo puts it back where it was.

namespace {

class ClosePageCommand : public klsCommand {
public:
	ClosePageCommand(CLDocument* doc, int index) : klsCommand(true, "Close Page"), doc(doc), index(index) {}

	bool Do() override {
		if (index < 0 || index >= (int)doc->pages.size() || doc->pages.size() < 2) return false;
		GUICanvas* p = doc->pages[index].get();
		std::vector<unsigned long> gates, wires;
		for (auto& g : *p->getGateList()) gates.push_back(g.first);
		for (auto& w : *p->getWireList()) wires.push_back(w.first);
		std::sort(gates.begin(), gates.end());
		std::sort(wires.begin(), wires.end());
		del.reset();
		if (!gates.empty() || !wires.empty()) {
			del.reset(new cmdDeleteSelection(&doc->circuit, p, gates, wires));
			del->Do();
		}
		held = std::move(doc->pages[index]);
		doc->pages.erase(doc->pages.begin() + index);
		doc->pageToShow = std::min(index, (int)doc->pages.size() - 1);
		return true;
	}

	bool Undo() override {
		if (!held) return false;
		doc->pages.insert(doc->pages.begin() + index, std::move(held));
		if (del) del->Undo();
		del.reset();
		doc->pageToShow = index;
		return true;
	}

private:
	CLDocument* doc;
	int index;
	std::unique_ptr<GUICanvas> held;          // while closed
	std::unique_ptr<cmdDeleteSelection> del;  // what was on it
};

void resetForPageChange(CLDocument* doc) {
	if (doc->tidy.active) cl_edit_tidy_end(doc, true);
	doc->gesture = EditGesture();
	doc->hoverPin = false;
}

}  // namespace

extern "C" {

bool cl_document_close_page(CLDocument* doc, int page) {
	if (doc == nullptr || page < 0 || page >= (int)doc->pages.size() || doc->pages.size() < 2) return false;
	resetForPageChange(doc);
	doc->edited = true;
	return doc->circuit.GetCommandProcessor()->Submit(new ClosePageCommand(doc, page));
}

bool cl_edit_undo_is_close_page(const CLDocument* doc) {
	return doc && const_cast<CLDocument*>(doc)->circuit.GetCommandProcessor()->GetUndoName() == "Close Page";
}

int cl_document_page_to_show(const CLDocument* doc) { return doc ? doc->pageToShow : -1; }

void cl_document_move_page(CLDocument* doc, int from, int to) {
	if (doc == nullptr) return;
	const int n = (int)doc->pages.size();
	if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;
	resetForPageChange(doc);
	std::unique_ptr<GUICanvas> p = std::move(doc->pages[from]);
	doc->pages.erase(doc->pages.begin() + from);
	doc->pages.insert(doc->pages.begin() + to, std::move(p));
	doc->edited = true;
}

unsigned long long cl_document_page_id(const CLDocument* doc, int page) {
	GUICanvas* p = doc ? doc->page(page) : nullptr;
	return (unsigned long long)(uintptr_t)p;
}

int cl_document_page_index(const CLDocument* doc, unsigned long long id) {
	if (doc == nullptr || id == 0) return -1;
	for (size_t i = 0; i < doc->pages.size(); i++)
		if ((unsigned long long)(uintptr_t)doc->pages[i].get() == id) return (int)i;
	return -1;
}

// ---- Memory (RamPopupDialog) -------------------------------------------------------

static guiGateRAM* ramGate(const CLDocument* doc, long gate) {
	if (doc == nullptr || gate < 0) return nullptr;
	return dynamic_cast<guiGateRAM*>(const_cast<CLDocument*>(doc)->circuit.getGate((unsigned long)gate));
}

bool cl_ram_info(const CLDocument* doc, long gate, int* addressBits, int* dataBits) {
	guiGateRAM* g = ramGate(doc, gate);
	if (g == nullptr) return false;
	int a = 0, d = 0;
	std::istringstream(g->getLogicParam("ADDRESS_BITS")) >> a;
	std::istringstream(g->getLogicParam("DATA_BITS")) >> d;
	if (addressBits) *addressBits = a;
	if (dataBits) *dataBits = d;
	return a > 0 && d > 0;
}

unsigned long cl_ram_value(CLDocument* doc, long gate, unsigned long address) {
	guiGateRAM* g = ramGate(doc, gate);
	return g ? g->getValueAt(address) : 0;
}

long cl_ram_last_read(CLDocument* doc, long gate) {
	guiGateRAM* g = ramGate(doc, gate);
	return g ? g->getLastRead() : -1;
}

long cl_ram_last_written(CLDocument* doc, long gate) {
	guiGateRAM* g = ramGate(doc, gate);
	return g ? g->getLastWritten() : -1;
}

void cl_ram_set(CLDocument* doc, long gate, unsigned long address, unsigned long value) {
	if (ramGate(doc, gate) == nullptr) return;
	std::ostringstream name;
	name << "Address:" << address;
	std::ostringstream v;
	v << value;
	doc->circuit.sendMessageToCore(klsMessage::Message(klsMessage::MT_SET_GATE_PARAM,
		new klsMessage::Message_SET_GATE_PARAM((unsigned long)gate, name.str(), v.str())));
	doc->circuit.sendMessageToCore(klsMessage::Message(klsMessage::MT_UPDATE_GATES));
	doc->edited = true;
}

void cl_ram_load_file(CLDocument* doc, long gate, const char* path) {
	if (ramGate(doc, gate) == nullptr || path == nullptr) return;
	doc->circuit.sendMessageToCore(klsMessage::Message(klsMessage::MT_SET_GATE_PARAM,
		new klsMessage::Message_SET_GATE_PARAM((unsigned long)gate, "READ_FILE", std::string(path))));
	doc->circuit.sendMessageToCore(klsMessage::Message(klsMessage::MT_UPDATE_GATES));
	doc->edited = true;
}

void cl_ram_save_file(CLDocument* doc, long gate, const char* path) {
	if (ramGate(doc, gate) == nullptr || path == nullptr) return;
	doc->circuit.sendMessageToCore(klsMessage::Message(klsMessage::MT_SET_GATE_PARAM,
		new klsMessage::Message_SET_GATE_PARAM((unsigned long)gate, "WRITE_FILE", std::string(path))));
}

// ---- The guided tour (Welcome.cpp's Circuit) ---------------------------------------

void cl_tour_status(CLDocument* doc, int pageIndex, CLTourStatus* out) {
	if (out == nullptr) return;
	*out = CLTourStatus();
	GUICanvas* page = doc ? doc->page(pageIndex) : nullptr;
	if (page == nullptr) return;
	std::vector<guiGate*> lights;
	guiGate* andGate = nullptr;
	for (auto& ge : *page->getGateList()) {
		guiGate* g = ge.second;
		if (g == nullptr) continue;
		if (dynamic_cast<guiGateTOGGLE*>(g)) {
			out->switches++;
			if (g->getLogicParam("OUTPUT_NUM") == "1") out->switchesOn++;
		}
		if (dynamic_cast<guiGateLED*>(g)) lights.push_back(g);
		const std::string n = g->getLibraryGateName();
		if (andGate == nullptr && n.find("AND") != std::string::npos && n.find("NAND") == std::string::npos) andGate = g;
	}
	out->lights = (int)lights.size();
	out->hasAnd = andGate != nullptr;
	if (andGate) {
		for (const auto& hs : andGate->getHotspotList())
			if (hs.first.rfind("IN", 0) == 0 && andGate->isConnected(hs.first)) out->andInputsWired++;
		if (andGate->isConnected("OUT")) {
			guiWire* w = andGate->getConnection("OUT");
			for (guiGate* l : lights)
				for (const auto& c : l->getConnections()) if (c.second == w) out->lightWired = true;
		}
	}
	for (guiGate* l : lights)
		for (const auto& c : l->getConnections())
			if (c.second && !c.second->getState().empty() && c.second->getState()[0] == ONE) out->lightOn = true;
}

}  // extern "C"
