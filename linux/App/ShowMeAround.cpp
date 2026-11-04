// Help > Show Me Around (see ShowMeAround.h).

#include "ShowMeAround.h"
#include "Anim.h"
#include "Brand.h"
#include "Canvas.h"
#include "MenuBar.h"
#include "Rail.h"
#include "Shortcuts.h"
#include "Splash.h"
#include "Toolbar.h"
#include "Welcome.h"
#include "Window.h"

#include <algorithm>
#include <cmath>
#include <map>

using namespace brand;

namespace showme {

namespace {

// ---- Its places and words -----------------------------------------------------------------

enum Side { Right, Left, Below, Above, Inside };

struct StopInfo {
	const char* title;
	std::vector<Side> sides;   // where the card would rather go, before trying the others
};

const StopInfo kStops[SpotCount] = {
	{ "Parts", { Right } },
	{ "Your circuit", { Inside } },
	{ "The toolbar", { Below } },
	{ "The Actions panel", { Left } },
	{ "Run it", { Below } },
	{ "Tabs", { Below } },
	{ "Help and Settings", { Below } },
};

// The keys a command has now (Settings > Shortcuts changes them), "" for none.
std::string keyOf(const char* id) {
	const shortcuts::Action* a = shortcuts::find(id);
	return a ? shortcuts::label(shortcuts::keys(*a)) : std::string();
}
std::string withKey(const char* id) {
	const std::string k = keyOf(id);
	return k.empty() ? std::string() : " (" + k + ")";
}

// ---- The layers --------------------------------------------------------------------------

// Each layer fills its overlay, which is measured instead (it's there and
// laid out before the layer is first shown).
struct Layers {
	GtkWidget* title = nullptr;
	GtkWidget* content = nullptr;
	GtkWidget* titleOver = nullptr;
	GtkWidget* contentOver = nullptr;
	GtkWidget* over(GtkWidget* layer) const { return layer == title ? titleOver : contentOver; }
};
std::map<CircuitWindow*, Layers> g_layers;

bool windowAlive(CircuitWindow* w) {
	return w && std::find(circuitWindows().begin(), circuitWindows().end(), w) != circuitWindows().end();
}

RectF unite(const RectF& a, const RectF& b) {
	return rectF(std::min(a.left, b.left), std::min(a.top, b.top), std::max(a.right, b.right), std::max(a.bottom, b.bottom));
}
RectF inflate(const RectF& r, float d) { return rectF(r.left - d, r.top - d, r.right + d, r.bottom + d); }
RectF offsetRect(const RectF& r, float dx, float dy) { return rectF(r.left + dx, r.top + dy, r.right + dx, r.bottom + dy); }
RectF lerpRect(const RectF& a, const RectF& b, float t) {
	return rectF(a.left + (b.left - a.left) * t, a.top + (b.top - a.top) * t, a.right + (b.right - a.right) * t, a.bottom + (b.bottom - a.bottom) * t);
}
bool sameRect(const RectF& a, const RectF& b) {
	return std::fabs(a.left - b.left) < 0.5f && std::fabs(a.top - b.top) < 0.5f && std::fabs(a.right - b.right) < 0.5f &&
	       std::fabs(a.bottom - b.bottom) < 0.5f;
}

// A widget on screen: mapped, and not inside a revealer that's put away.
bool showing(GtkWidget* x) {
	if (x == nullptr || !gtk_widget_get_mapped(x) || gtk_widget_get_allocated_width(x) <= 1 || gtk_widget_get_allocated_height(x) <= 1) return false;
	for (GtkWidget* p = gtk_widget_get_parent(x); p; p = gtk_widget_get_parent(p))
		if (GTK_IS_REVEALER(p) && !gtk_revealer_get_child_revealed(GTK_REVEALER(p))) return false;
	return true;
}

// A widget's rectangle in its toplevel's points.
bool rectIn(GtkWidget* top, GtkWidget* x, RectF& out) {
	int ox = 0, oy = 0;
	if (!gtk_widget_translate_coordinates(x, top, 0, 0, &ox, &oy)) return false;
	out = rectF((float)ox, (float)oy, (float)(ox + gtk_widget_get_allocated_width(x)), (float)(oy + gtk_widget_get_allocated_height(x)));
	return true;
}

// The card's pointer: on which edge, and how far along it.
enum Edge { NoEdge, Top, Bottom, Leading, Trailing };
struct Caret { Edge edge = NoEdge; float at = 0; };

struct Hit { RectF r; int id; };
enum { kSkip = 1, kBack, kNext, kLesson };

const float kPad = 6, kGap = 14, kMargin = 12, kShadow = 26, kCardRadius = 14;

// ---- The walk ----------------------------------------------------------------------------

struct Walk {
	CircuitWindow* win = nullptr;
	bool on = false, previewing = false;
	int step = 0;
	RectF frames[SpotCount]{};
	bool has[SpotCount]{};
	RectF scene{};                 // both layers, in the window's points
	// What's drawn: the hole and the card's top-left, moving from -> to.
	RectF holeFrom{}, holeTo{};
	PointF cardFrom{}, cardTo{};
	Caret caret;
	bool ringShown = true;
	double moveStart = -1;
	// With animations off, a cut inside a quick fade of the card: 1 fading
	// out (then the cut), 2 fading in.
	int fadePhase = 0;
	double fadeStart = 0;
	int pendingStep = -1;
	// The card, drawn once per stop (and again as the pointer lights a button).
	cairo_surface_t* card = nullptr;
	float cardW = 300, cardH = 150;
	int cardStep = -1, cardHot = -1, cardScale = 1, cardPos = 0, cardCount = 0;
	std::vector<Hit> hits;          // in the card's points
	int hot = -1;
	RectF lastDrawn{};
	bool drawnValid = false;
	guint tickId = 0, watchTimer = 0, settleTimer = 0;
	std::vector<guint> soonTimers;
	std::function<void(CircuitWindow*)> then;
};
Walk g;

// Starting by itself.
bool g_wanted = false, g_quiet = false, g_assumeKeys = false;
guint g_autoTimer = 0;
double g_readySince = -1;

bool calm() { return !anim::enabled() || (g.win && g.win->slowFrames()); }

const Layers* layersOf(CircuitWindow* w) {
	auto it = g_layers.find(w);
	return it == g_layers.end() ? nullptr : &it->second;
}

bool available(int s) {
	if (s == Menus) return true;
	if (s == ActionsSpot) return prefs().actionsOn && g.has[s];
	return g.has[s];
}

std::vector<int> shownSteps() {
	std::vector<int> v;
	for (int s = 0; s < SpotCount; s++) if (available(s)) v.push_back(s);
	return v;
}

// Where each place is now, in the walk's window.
bool measure() {
	CircuitWindow* w = g.win;
	const Layers* L = layersOf(w);
	if (!w || !L) return false;
	GtkWidget* top = GTK_WIDGET(w->window());
	RectF was[SpotCount];
	bool had[SpotCount];
	std::copy(std::begin(g.frames), std::end(g.frames), was);
	std::copy(std::begin(g.has), std::end(g.has), had);
	const RectF sceneWas = g.scene;
	RectF a{}, b{};
	const bool ta = L->titleOver && showing(L->titleOver) && rectIn(top, L->titleOver, a);
	const bool tb = rectIn(top, L->contentOver, b);
	g.scene = ta && tb ? unite(a, b) : tb ? b : a;
	for (int s = 0; s < SpotCount; s++) g.has[s] = false;
	auto put = [&](int s, GtkWidget* x) {
		RectF r{};
		if (showing(x) && rectIn(top, x, r)) { g.frames[s] = r; g.has[s] = true; }
	};
	put(Palette, w->paletteWidgetForTour());
	if (Canvas* c = w->currentCanvas()) put(CanvasSpot, c->widget());
	if (Toolbar* t = w->toolbarWidget()) {
		RectF bar{};
		if (showing(t->widget()) && rectIn(top, t->widget(), bar)) {
			RectF r{};
			if (t->toolsRect(false, r)) { g.frames[ToolbarSpot] = offsetRect(r, bar.left, bar.top); g.has[ToolbarSpot] = true; }
			if (t->toolsRect(true, r)) { g.frames[Run] = offsetRect(r, bar.left, bar.top); g.has[Run] = true; }
		}
	}
	if (prefs().actionsOn && w->rail()) put(ActionsSpot, w->rail()->dockWidget());
	put(Tabs, w->tabStripForTour());
	if (MenuRow* m = w->menuRowWidget()) {
		if (m->showing()) {
			GList* kids = gtk_container_get_children(GTK_CONTAINER(m->bar()));
			bool any = false;
			RectF u{};
			for (GList* k = kids; k; k = k->next) {
				RectF r{};
				if (!showing(GTK_WIDGET(k->data)) || !rectIn(top, GTK_WIDGET(k->data), r)) continue;
				u = any ? unite(u, r) : r;
				any = true;
			}
			g_list_free(kids);
			if (any) { g.frames[Menus] = u; g.has[Menus] = true; }
		}
	}
	// Clipped to the window.
	for (int s = 0; s < SpotCount; s++) {
		if (!g.has[s]) continue;
		RectF& r = g.frames[s];
		r = rectF(std::max(r.left, g.scene.left), std::max(r.top, g.scene.top), std::min(r.right, g.scene.right), std::min(r.bottom, g.scene.bottom));
		if (rectWidth(r) <= 0.5f || rectHeight(r) <= 0.5f) g.has[s] = false;
	}
	bool changed = !sameRect(sceneWas, g.scene);
	for (int s = 0; s < SpotCount; s++) changed = changed || had[s] != g.has[s] || (g.has[s] && !sameRect(was[s], g.frames[s]));
	return changed;
}

// The lit place, a little larger than it is, and at least a finger's width
// (the folded panel's tab is 6 points). With the menus not showing, above
// the window, out of sight.
RectF holeRect(int s) {
	const RectF& S = g.scene;
	if (!g.has[s]) {
		const float cx = (S.left + S.right) / 2;
		return rectF(cx - 60, S.top - 40, cx + 60, S.top - 20);
	}
	RectF r = inflate(g.frames[s], kPad);
	if (rectWidth(r) < 28) { const float d = (28 - rectWidth(r)) / 2; r.left -= d; r.right += d; }
	const RectF in = inflate(S, -2);
	const RectF c = rectF(std::max(r.left, in.left), std::max(r.top, in.top), std::min(r.right, in.right), std::min(r.bottom, in.bottom));
	return rectWidth(c) > 0 && rectHeight(c) > 0 ? c : r;
}

float cardWidth() { return std::min(300.0f, std::max(200.0f, rectWidth(g.scene) - 2 * kMargin)); }

// Beside the place, on the side it prefers if it fits there, else any side
// that does, else inside a big place, else at the window's foot.
void place(int s, const RectF& h, PointF& origin, Caret& caret) {
	const RectF& S = g.scene;
	const float m = kMargin, gap = kGap, bw = g.cardW, bh = g.cardH;
	auto clampX = [&](float x) { return std::min(std::max(x, S.left + m), std::max(S.left + m, S.right - bw - m)); };
	auto clampY = [&](float y) { return std::min(std::max(y, S.top + m), std::max(S.top + m, S.bottom - bh - m)); };
	const float midX = (h.left + h.right) / 2, midY = (h.top + h.bottom) / 2;
	caret = Caret{};
	if (s == Menus && !g.has[s]) {
		origin = pointF(clampX((S.left + S.right) / 2 - bw / 2), std::min(S.top + 64, std::max(S.top + m, S.bottom - bh - m)));
		caret = { Top, (S.left + S.right) / 2 - origin.x };
		return;
	}
	std::vector<Side> sides = kStops[s].sides;
	for (Side x : { Below, Right, Left, Above, Inside }) sides.push_back(x);
	for (Side side : sides) {
		switch (side) {
		case Right:
			if (S.right - h.right - gap - m >= bw) { origin = pointF(h.right + gap, clampY(midY - bh / 2)); caret = { Leading, midY - origin.y }; return; }
			break;
		case Left:
			if (h.left - S.left - gap - m >= bw) { origin = pointF(h.left - gap - bw, clampY(midY - bh / 2)); caret = { Trailing, midY - origin.y }; return; }
			break;
		case Below:
			if (S.bottom - h.bottom - gap - m >= bh) { origin = pointF(clampX(midX - bw / 2), h.bottom + gap); caret = { Top, midX - origin.x }; return; }
			break;
		case Above:
			if (h.top - S.top - gap - m >= bh) { origin = pointF(clampX(midX - bw / 2), h.top - gap - bh); caret = { Bottom, midX - origin.x }; return; }
			break;
		case Inside:
			if (rectWidth(h) >= bw + 2 * m && rectHeight(h) >= bh + 2 * m) { origin = pointF(clampX(midX - bw / 2), clampY(midY - bh / 2)); return; }
			break;
		}
	}
	origin = pointF(clampX((S.left + S.right) / 2 - bw / 2), clampY(S.bottom - bh - m));
}

// ---- The card --------------------------------------------------------------------------

const char* const kMenuNames[] = { "File", "Edit", "View", "Simulation", "Window", "Help" };

// Draws it at (0, 0), `w` wide; returns its height. With cr null, only measures.
float paintCard(cairo_t* cr, float w, std::vector<Hit>* hits) {
	const int s = g.step;
	const std::vector<int> v = shownSteps();
	const int count = std::max(1, (int)v.size());
	int pos = 1;
	for (size_t i = 0; i < v.size(); i++) if (v[i] == s) pos = (int)i + 1;
	const bool last = v.empty() || s >= v.back(), first = v.empty() || s <= v.front();
	const float x = 16, iw = w - 32;
	float y = 16;
	if (cr) {
		drawIcon(cr, "find-location-symbolic", rectF(x - 1, y - 2, x + 13, y + 12), 10, kNeon);
		text(cr, format("SHOW ME AROUND  ·  %d OF %d", pos, count), x + 18, y, 10, Bold, kNeon, 0, TextAlign::Leading, 1.1f);
		if (!last) {
			const RectF skip = rectF(w - 16 - 40, y - 3, w - 16, y + 14);
			text(cr, "Skip", skip.left, y - 1, 11.5f, Medium, g.cardHot == kSkip ? alpha(kPrimary, 0.75f) : kFaint, 40, TextAlign::Trailing);
			if (hits) hits->push_back({ inflate(skip, 3), kSkip });
		}
	}
	y += 14 + 9;
	if (pos == 1) {
		y += text(cr, format("A quick look around CedarLogic, %d stops. Skip any time.", count), x, y, 12, SemiBold, alpha(kPrimary, 0.85f), iw) + 9;
	}
	y += text(cr, title(s), x, y, 16, Bold, kPrimary, iw) + 9;
	y += text(cr, body(s), x, y, 12.5f, Normal, kSecondary, iw) + 9;
	if (s == Menus) {
		// The window's menus, small: File (where Settings is) and Help, lit.
		const float h = 22;
		if (cr) {
			cairo_save(cr);
			roundedPath(cr, rectF(x, y, x + iw, y + h), 6);
			cairo_clip(cr);
			fillRound(cr, rectF(x, y, x + iw, y + h), 6, colorF(1, 1, 1, 0.07f));
			float mx = x + 6;
			for (const char* m : kMenuNames) {
				const bool lit = std::string(m) == "File" || std::string(m) == "Help";
				const float tw = textWidth(m, 10.5f, lit ? Bold : Normal);
				if (lit) fillRound(cr, rectF(mx, y + 3, mx + tw + 8, y + h - 3), 4, kNeon);
				text(cr, m, mx + 4, y + 4.5f, 10.5f, lit ? Bold : Normal, lit ? kInk : kSecondary);
				mx += tw + 8 + 2;
			}
			cairo_restore(cr);
		}
		y += h + 2 + 9;
	}
	// The dots, Back and Next.
	if (cr) {
		float dx = x;
		for (int i = 0; i < count; i++) {
			const bool here = i == pos - 1;
			const float dw = here ? 14 : 5;
			fillRound(cr, rectF(dx, y + 12.5f, dx + dw, y + 17.5f), 2.5f, here ? kNeon : colorF(1, 1, 1, 0.2f));
			dx += dw + 4;
		}
		const RectF nb = rectF(w - 16 - 74, y, w - 16, y + 30);
		button(cr, nb, last ? "Done" : "Next", true, g.cardHot == kNext);
		if (hits) hits->push_back({ nb, kNext });
		if (!first) {
			const RectF bb = rectF(nb.left - 8 - 64, y, nb.left - 8, y + 30);
			button(cr, bb, "Back", false, g.cardHot == kBack);
			if (hits) hits->push_back({ bb, kBack });
		}
	}
	y += 30 + 2;
	if (last) {
		y += 9;
		const std::string line = "Next, learn logic: the Guided Tour builds a circuit with you";
		const float lh = text(nullptr, line, 0, 0, 11.5f, Medium, kNeon, iw - 18);
		if (cr) {
			drawIcon(cr, "applications-science-symbolic", rectF(x - 1, y, x + 13, y + 14), 11, alpha(kNeon, 0.9f));
			text(cr, line, x + 18, y, 11.5f, Medium, alpha(kNeon, g.cardHot == kLesson ? 1.0f : 0.9f), iw - 18);
			if (hits) hits->push_back({ rectF(x - 2, y - 3, x + iw, y + lh + 3), kLesson });
		}
		y += lh;
	}
	return y + 16;
}

void dropCard() {
	if (g.card) cairo_surface_destroy(g.card);
	g.card = nullptr;
	g.cardStep = -1;
}

// The card as an image, with its shadow round it: only when what's on it
// changes (another stop, a button lit, its width).
void renderCard() {
	const Layers* L = layersOf(g.win);
	const int scale = L && L->content ? std::max(1, gtk_widget_get_scale_factor(L->content)) : 1;
	const float w = cardWidth();
	const std::vector<int> v = shownSteps();
	int pos = 0;
	for (size_t i = 0; i < v.size(); i++) if (v[i] == g.step) pos = (int)i + 1;
	if (g.card && g.cardStep == g.step && g.cardHot == g.hot && g.cardScale == scale && std::fabs(g.cardW - w) < 0.5f && g.cardPos == pos &&
	    g.cardCount == (int)v.size())
		return;
	dropCard();
	g.cardHot = g.hot;
	g.cardW = w;
	g.cardH = std::ceil(paintCard(nullptr, w, nullptr));
	const int pw = (int)std::ceil((w + 2 * kShadow) * scale), ph = (int)std::ceil((g.cardH + 2 * kShadow) * scale);
	g.card = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
	cairo_surface_set_device_scale(g.card, scale, scale);
	g.cardScale = scale;
	g.cardStep = g.step;
	g.cardPos = pos;
	g.cardCount = (int)v.size();
	cairo_t* cr = cairo_create(g.card);
	cairo_translate(cr, kShadow, kShadow);
	const RectF r = rectF(0, 0, w, g.cardH);
	// Its shadow, soft and a little below.
	for (int i = 0; i < 6; i++) {
		const float d = 2.0f + i * 3.0f;
		fillRound(cr, rectF(-d * 0.6f, 6 - d * 0.4f, w + d * 0.6f, g.cardH + 6 + d * 0.6f), kCardRadius + d, colorF(0, 0, 0, 0.07f));
	}
	cairo_save(cr);
	roundedPath(cr, r, kCardRadius);
	cairo_clip(cr);
	ground(cr, w, g.cardH, 0.1f, 0, 22);
	cairo_restore(cr);
	strokeRound(cr, rectF(0.5f, 0.5f, w - 0.5f, g.cardH - 0.5f), kCardRadius - 0.5f, colorF(1, 1, 1, 0.16f));
	g.hits.clear();
	paintCard(cr, w, &g.hits);
	cairo_destroy(cr);
	cairo_surface_flush(g.card);
}

// ---- Moving, and drawing ---------------------------------------------------------------------

double moveT() {
	if (g.moveStart < 0) return 1;
	const double t = (anim::now() - g.moveStart) / 0.42;
	return t >= 1 ? 1 : 1 - std::pow(1 - t, 3);
}
RectF currentHole() { return lerpRect(g.holeFrom, g.holeTo, (float)moveT()); }
PointF currentCard() {
	const float t = (float)moveT();
	return pointF(g.cardFrom.x + (g.cardTo.x - g.cardFrom.x) * t, g.cardFrom.y + (g.cardTo.y - g.cardFrom.y) * t);
}
float cardAlpha() {
	if (g.fadePhase == 0) return 1;
	const double t = anim::now() - g.fadeStart;
	return g.fadePhase == 1 ? (float)std::max(0.0, 1 - t / 0.1) : (float)std::min(1.0, t / 0.14);
}

// What the picture covers that can change: the hole with its ring and glow,
// the card with its shadow and pointer.
RectF liveBounds() {
	const PointF c = currentCard();
	return unite(inflate(currentHole(), 14), rectF(c.x - kShadow, c.y - kShadow, c.x + g.cardW + kShadow, c.y + g.cardH + kShadow));
}

// Redraws only the part of the layers that changed.
void invalidate(bool all = false) {
	const Layers* L = layersOf(g.win);
	if (!L) return;
	const RectF now = liveBounds();
	const RectF dirty = all || !g.drawnValid ? g.scene : unite(g.lastDrawn, now);
	g.lastDrawn = now;
	g.drawnValid = true;
	GtkWidget* top = GTK_WIDGET(g.win->window());
	for (GtkWidget* layer : { L->title, L->content }) {
		if (!layer || !gtk_widget_get_visible(layer)) continue;
		RectF lr{};
		if (!rectIn(top, L->over(layer), lr)) continue;
		const int x = (int)std::floor(dirty.left - lr.left), y = (int)std::floor(dirty.top - lr.top);
		const int x2 = (int)std::ceil(dirty.right - lr.left), y2 = (int)std::ceil(dirty.bottom - lr.top);
		const int cx = std::max(0, x), cy = std::max(0, y);
		const int cw = std::min(gtk_widget_get_allocated_width(layer), x2) - cx, ch = std::min(gtk_widget_get_allocated_height(layer), y2) - cy;
		if (cw > 0 && ch > 0) gtk_widget_queue_draw_area(layer, cx, cy, cw, ch);
	}
}

// Clicks on the dim go nowhere; in the hole they reach what's lit.
void updateInputShape() {
	const Layers* L = layersOf(g.win);
	if (!L) return;
	GtkWidget* top = GTK_WIDGET(g.win->window());
	const RectF h = g.holeTo;
	for (GtkWidget* layer : { L->title, L->content }) {
		if (!layer) continue;
		RectF lr{};
		if (!rectIn(top, L->over(layer), lr)) continue;
		const cairo_rectangle_int_t all = { 0, 0, std::max(1, (int)std::ceil(rectWidth(lr))), std::max(1, (int)std::ceil(rectHeight(lr))) };
		cairo_region_t* region = cairo_region_create_rectangle(&all);
		if (g.ringShown) {
			const cairo_rectangle_int_t hole = { (int)std::floor(h.left - lr.left), (int)std::floor(h.top - lr.top), (int)std::ceil(rectWidth(h)),
			                                     (int)std::ceil(rectHeight(h)) };
			cairo_region_subtract_rectangle(region, &hole);
		}
		gtk_widget_input_shape_combine_region(layer, region);
		cairo_region_destroy(region);
	}
}

gboolean tickCb(GtkWidget*, GdkFrameClock*, gpointer);

void ensureTick() {
	const Layers* L = layersOf(g.win);
	if (g.tickId || !L || !L->content) return;
	g.tickId = gtk_widget_add_tick_callback(L->content, tickCb, nullptr, nullptr);
}

// Points the spotlight and the card at the stop it's on (sliding there, or at once).
void aim(bool slide) {
	renderCard();
	const RectF hole = holeRect(g.step);
	PointF origin{};
	Caret c{};
	place(g.step, hole, origin, c);
	g.ringShown = g.step != Menus || g.has[Menus];
	if (slide && !calm() && g.drawnValid) {
		g.holeFrom = currentHole();
		g.cardFrom = currentCard();
		g.moveStart = anim::now();
	} else {
		g.holeFrom = hole;
		g.cardFrom = origin;
		g.moveStart = -1;
	}
	g.holeTo = hole;
	g.cardTo = origin;
	g.caret = c;
	updateInputShape();
	invalidate();
	if (g.moveStart >= 0) ensureTick();
}

gboolean tickCb(GtkWidget*, GdkFrameClock*, gpointer) {
	if (!g.on) { g.tickId = 0; return G_SOURCE_REMOVE; }
	bool more = false;
	guarded("Show Me Around", [&] {
		if (g.moveStart >= 0 && moveT() >= 1) g.moveStart = -1;
		if (g.fadePhase == 1 && anim::now() - g.fadeStart >= 0.1) {
			// The cut, in the middle of the fade.
			g.step = g.pendingStep;
			g.pendingStep = -1;
			g.fadePhase = 2;
			g.fadeStart = anim::now();
			if (g.win->rail()) g.win->rail()->refresh();
			measure();
			aim(false);
		} else if (g.fadePhase == 2 && anim::now() - g.fadeStart >= 0.14) {
			g.fadePhase = 0;
		}
		invalidate();
		more = g.moveStart >= 0 || g.fadePhase != 0;
	});
	if (!more) g.tickId = 0;
	return more ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

void paintLayer(GtkWidget* layer, cairo_t* cr) {
	if (!g.on || !g.win) return;
	const Layers* L = layersOf(g.win);
	if (!L || (layer != L->title && layer != L->content)) return;
	RectF lr{};
	if (!rectIn(GTK_WIDGET(g.win->window()), L->over(layer), lr)) return;
	cairo_save(cr);
	cairo_translate(cr, -lr.left, -lr.top);
	const RectF& S = g.scene;
	const RectF h = currentHole();
	// The dim, with its one rounded hole.
	cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
	cairo_rectangle(cr, S.left - 4, S.top - 4, rectWidth(S) + 8, rectHeight(S) + 8);
	roundedPath(cr, h, 10);
	cairo_set_source_rgba(cr, 0, 0, 0, prefs().dark ? 0.6 : 0.48);
	cairo_fill(cr);
	cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
	// The ring, with a little glow.
	if (g.ringShown) {
		strokeRound(cr, inflate(h, 2), 12, alpha(kNeon, 0.14f), 6);
		strokeRound(cr, inflate(h, 1), 11, alpha(kNeon, 0.25f), 3);
		strokeRound(cr, h, 10, alpha(kNeon, 0.85f), 2);
	}
	// The card, and its pointer.
	if (g.card) {
		const PointF c = currentCard();
		const float a = cardAlpha();
		cairo_set_source_surface(cr, g.card, c.x - kShadow, c.y - kShadow);
		cairo_paint_with_alpha(cr, a);
		if (g.caret.edge != NoEdge && a > 0.01f) {
			const float w = 16, d = 8;
			const float along = g.caret.edge == Top || g.caret.edge == Bottom ? g.cardW : g.cardH;
			const float at = std::min(std::max(g.caret.at, 20.0f), along - 20);
			switch (g.caret.edge) {
			case Top: cairo_move_to(cr, c.x + at - w / 2, c.y + 1); cairo_line_to(cr, c.x + at, c.y - d); cairo_line_to(cr, c.x + at + w / 2, c.y + 1); break;
			case Bottom: {
				const float y = c.y + g.cardH - 1;
				cairo_move_to(cr, c.x + at - w / 2, y); cairo_line_to(cr, c.x + at, y + d); cairo_line_to(cr, c.x + at + w / 2, y);
				break;
			}
			case Leading: cairo_move_to(cr, c.x + 1, c.y + at - w / 2); cairo_line_to(cr, c.x - d, c.y + at); cairo_line_to(cr, c.x + 1, c.y + at + w / 2); break;
			case Trailing: {
				const float x = c.x + g.cardW - 1;
				cairo_move_to(cr, x, c.y + at - w / 2); cairo_line_to(cr, x + d, c.y + at); cairo_line_to(cr, x, c.y + at + w / 2);
				break;
			}
			default: break;
			}
			cairo_close_path(cr);
			cairo_set_source_rgba(cr, kInk.r, kInk.g, kInk.b, a);
			cairo_fill(cr);
		}
	}
	cairo_restore(cr);
}

int hitAt(GtkWidget* layer, double lx, double ly) {
	RectF lr{};
	const Layers* L = layersOf(g.win);
	if (!g.on || !g.win || !L || !rectIn(GTK_WIDGET(g.win->window()), L->over(layer), lr)) return -1;
	const PointF c = currentCard();
	const float x = (float)lx + lr.left - c.x, y = (float)ly + lr.top - c.y;
	for (const Hit& h : g.hits) if (inRect(h.r, x, y)) return h.id;
	return -1;
}

void setHot(int id) {
	if (id == g.hot) return;
	g.hot = id;
	renderCard();
	invalidate();
}

void act(int id) {
	switch (id) {
	case kSkip: end(); break;
	case kBack: back(); break;
	case kNext: next(); break;
	case kLesson:
		if (!g.then) g.then = [](CircuitWindow* w) { welcome::startTourOn(w); };
		end();
		break;
	default: break;
	}
}

GtkWidget* makeLayer(GtkWidget* overlay) {
	GtkWidget* a = gtk_drawing_area_new();
	gtk_widget_set_name(a, "show-me-around");
	gtk_widget_set_no_show_all(a, TRUE);
	gtk_widget_set_halign(a, GTK_ALIGN_FILL);
	gtk_widget_set_valign(a, GTK_ALIGN_FILL);
	gtk_widget_add_events(a, GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_SCROLL_MASK);
	g_signal_connect(a, "draw", CL_CALLBACK(+[](GtkWidget* w, cairo_t* cr, gpointer) -> gboolean {
		guarded("Show Me Around", [&] { paintLayer(w, cr); });
		return TRUE;
	}), nullptr);
	g_signal_connect(a, "motion-notify-event", CL_CALLBACK(+[](GtkWidget* w, GdkEventMotion* e, gpointer) -> gboolean {
		guarded("Show Me Around", [&] { setHot(hitAt(w, e->x, e->y)); });
		return TRUE;
	}), nullptr);
	g_signal_connect(a, "leave-notify-event", CL_CALLBACK(+[](GtkWidget*, GdkEventCrossing*, gpointer) -> gboolean {
		guarded("Show Me Around", [] { setHot(-1); });
		return FALSE;
	}), nullptr);
	// The dim takes every click and the wheel, and does nothing with them.
	g_signal_connect(a, "button-press-event", CL_CALLBACK(+[](GtkWidget*, GdkEventButton*, gpointer) -> gboolean { return TRUE; }), nullptr);
	g_signal_connect(a, "scroll-event", CL_CALLBACK(+[](GtkWidget*, GdkEventScroll*, gpointer) -> gboolean { return TRUE; }), nullptr);
	g_signal_connect(a, "button-release-event", CL_CALLBACK(+[](GtkWidget* w, GdkEventButton* e, gpointer) -> gboolean {
		if (e->button == 1) {
			const int id = hitAt(w, e->x, e->y);
			guarded("Show Me Around", [&] { act(id); });
		}
		return TRUE;
	}), nullptr);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), a);
	return a;
}

void showLayers(bool on) {
	const Layers* L = layersOf(g.win);
	if (!L) return;
	for (GtkWidget* layer : { L->title, L->content }) {
		if (!layer) continue;
		if (on) {
			gtk_widget_set_opacity(layer, g.previewing || calm() ? 1 : 0);
			gtk_widget_show(layer);
			if (!g.previewing && !calm()) anim::fadeTo(layer, 1, 0.2);
		} else {
			gtk_widget_hide(layer);
		}
	}
}

void cancelTimers() {
	if (g.watchTimer) g_source_remove(g.watchTimer);
	if (g.settleTimer) g_source_remove(g.settleTimer);
	for (guint t : g.soonTimers) g_source_remove(t);
	g.soonTimers.clear();
	g.watchTimer = g.settleTimer = 0;
	const Layers* L = layersOf(g.win);
	if (g.tickId && L && L->content) gtk_widget_remove_tick_callback(L->content, g.tickId);
	g.tickId = 0;
}

// Once the layers are up (and after the panel opens for its stop), where
// things are settles: looked at again a few times, the spotlight following.
void remeasureSoon() {
	for (guint ms : { 50u, 220u, 400u }) {
		guint* slot = new guint(0);
		*slot = g_timeout_add_full(G_PRIORITY_DEFAULT, ms, [](gpointer p) -> gboolean {
			guint id = *static_cast<guint*>(p);
			g.soonTimers.erase(std::remove(g.soonTimers.begin(), g.soonTimers.end(), id), g.soonTimers.end());
			if (g.on && windowAlive(g.win)) guarded("Show Me Around", [] { if (measure()) aim(true); });
			return G_SOURCE_REMOVE;
		}, slot, [](gpointer p) { delete static_cast<guint*>(p); });
		g.soonTimers.push_back(*slot);
	}
}

// Four times a second while it's on: the places move with the window (a
// resize, the panel folding); the stop it's on gone (the panel turned off),
// on to the next.
gboolean watchCb(gpointer) {
	if (!g.on) { g.watchTimer = 0; return G_SOURCE_REMOVE; }
	bool keep = true;
	guarded("Show Me Around", [&] {
		if (!windowAlive(g.win)) { g.watchTimer = 0; keep = false; stop(); return; }
		const bool moved = measure();
		const std::vector<int> v = shownSteps();
		if (std::find(v.begin(), v.end(), g.step) == v.end() && g.fadePhase == 0) {
			int n = -1;
			for (int s : v) if (s > g.step) { n = s; break; }
			if (n < 0 && !v.empty()) n = v.back();
			if (n < 0) { g.watchTimer = 0; keep = false; stop(); return; }
			g.step = n;
			aim(true);
		} else if (moved) {
			aim(true);
		}
	});
	return keep ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

void go(int n) {
	if (n == g.step || n < 0) return;
	if (calm()) {
		g.pendingStep = n;
		g.fadePhase = 1;
		g.fadeStart = anim::now();
		ensureTick();
		return;
	}
	g.step = n;
	// The panel opens for its stop, and folds back after: where things are moves.
	if (g.win->rail()) g.win->rail()->refresh();
	measure();
	aim(true);
	remeasureSoon();
}

void begin(CircuitWindow* w) {
	g.settleTimer = 0;
	if (!windowAlive(w) || g.win != w) return;
	g.on = true;
	g.hot = -1;
	g.drawnValid = false;
	g.fadePhase = 0;
	showLayers(true);
	measure();
	const std::vector<int> v = shownSteps();
	g.step = v.empty() ? 0 : v.front();
	if (w->rail()) w->rail()->refresh();
	aim(false);
	invalidate(true);
	remeasureSoon();
	if (!g.previewing) g.watchTimer = g_timeout_add(250, watchCb, nullptr);
}

// Whether the desktop says which window has the keyboard (at the file's
// end: X11's headers define words this file uses).
bool wmTracksFocus(GdkDisplay* d);

gboolean autoTick(gpointer) {
	bool keep = true;
	guarded("Show Me Around", [&] {
		if (prefs().showMeAroundSeen || g.on || !g_wanted) { g_autoTimer = 0; keep = false; g_wanted = false; return; }
		CircuitWindow* w = readyWindow();
		if (w == nullptr) { g_readySince = -1; return; }
		// Ready for a moment (the window settled, the circuit's card played).
		const double now = anim::now();
		if (g_readySince < 0) { g_readySince = now; return; }
		if (now - g_readySince > 0.7) {
			g_autoTimer = 0;
			keep = false;
			g_wanted = false;
			start(w, 0.2);
		}
	});
	return keep ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

}  // namespace

// ---- The outside ------------------------------------------------------------------------

const char* spotName(int s) {
	static const char* names[] = { "palette", "canvas", "toolbar", "actions", "run", "tabs", "menus" };
	return s >= 0 && s < SpotCount ? names[s] : "";
}

std::string title(int s) { return s >= 0 && s < SpotCount ? kStops[s].title : ""; }

std::string body(int s) {
	switch (s) {
	case Palette: {
		const std::string k = keyOf("addGate");
		return "Drag any part from here onto the canvas, or press " + (k.empty() ? std::string("A") : k) + " and type its name.";
	}
	case CanvasSpot: return "Wire two parts by dragging from one pin to another, and click a switch to flip it.";
	case ToolbarSpot: return "New, undo, copy and paste, and zoom live up here; rest on a button for its name and shortcut.";
	case ActionsSpot:
		return prefs().actionsCollapsed
		           ? std::string("The buttons you use most, down the right edge. You keep it folded to a thin strip there: ") +
		                 (prefs().actionsHoverOpen ? "rest the pointer on it or click it" : "click it") + " to bring it out."
		           : "The buttons you use most, down the right edge. Click its left edge to fold it away, and right-click it to choose what’s on it.";
	case Run:
		return "Pause, step and set the speed here; the play button opens Simulation View" + withKey("simView") + " to watch signals move.";
	case Tabs: return "A circuit can have several pages: the + adds one, and a click on a tab goes to it.";
	case Menus:
		return "Help is in the menu bar at the top of the window, and Settings is under File" + withKey("preferences") +
		       ". Help ▸ Show Me Around plays this again.";
	default: return "";
	}
}

void addLayers(CircuitWindow* w, GtkWidget* titleOverlay, GtkWidget* contentOverlay) {
	Layers& L = g_layers[w];
	L.titleOver = titleOverlay;
	L.contentOver = contentOverlay;
	if (titleOverlay) L.title = makeLayer(titleOverlay);
	if (contentOverlay) L.content = makeLayer(contentOverlay);
	// Laid out (shown, or resized): its click shape follows.
	for (GtkWidget* layer : { L.title, L.content })
		if (layer)
			g_signal_connect(layer, "size-allocate", CL_CALLBACK(+[](GtkWidget*, GdkRectangle*, gpointer data) {
				if (g.on && g.win == static_cast<CircuitWindow*>(data)) guarded("Show Me Around", [] { updateInputShape(); });
			}), w);
	// The window's size changing: the places move.
	g_signal_connect(contentOverlay, "size-allocate", CL_CALLBACK(+[](GtkWidget*, GdkRectangle*, gpointer data) {
		CircuitWindow* cw = static_cast<CircuitWindow*>(data);
		if (g.on && g.win == cw)
			g_idle_add([](gpointer) -> gboolean {
				if (g.on && windowAlive(g.win)) guarded("Show Me Around", [] { if (measure()) aim(true); });
				return G_SOURCE_REMOVE;
			}, nullptr);
	}), w);
}

void windowClosed(CircuitWindow* w) {
	if (g.win == w) {
		// Its widgets are going with it: nothing of theirs is touched.
		cancelTimers();
		g.on = g.previewing = false;
		g.fadePhase = 0;
		g.pendingStep = -1;
		g.moveStart = -1;
		dropCard();
		g.win = nullptr;
	}
	g_layers.erase(w);
}

void start(CircuitWindow* w, double settle) {
	if (!windowAlive(w)) return;
	stop();
	stopWatching();
	if (welcome::tourActive()) welcome::endTourNow();
	g.win = w;
	g.previewing = false;
	gtk_window_present(w->window());
	// Focus mode and Simulation View hide the places it shows.
	if (w->focusMode()) w->toggleFocusMode();
	if (w->simView()) w->toggleSimView();
	if (settle > 0) {
		g.settleTimer = g_timeout_add((guint)(settle * 1000), [](gpointer data) -> gboolean {
			guarded("Show Me Around", [&] { begin(static_cast<CircuitWindow*>(data)); });
			return G_SOURCE_REMOVE;
		}, w);
	} else {
		begin(w);
	}
}

void preview(CircuitWindow* w, int spot) {
	stop();
	g.win = w;
	g.previewing = true;
	begin(w);
	measure();
	g.step = std::max(0, std::min((int)SpotCount - 1, spot));
	if (w->rail()) w->rail()->refresh();
	aim(false);
	invalidate(true);
}

void end() {
	if (!g.on) return;
	prefs().showMeAroundSeen = true;
	prefs().save();
	CircuitWindow* w = g.win;
	stop();
	if (!g.then) return;
	auto* after = new std::function<void(CircuitWindow*)>(std::move(g.then));
	g.then = nullptr;
	static CircuitWindow* s_for = nullptr;
	s_for = w;
	g_timeout_add_full(G_PRIORITY_DEFAULT, 250, [](gpointer p) -> gboolean {
		auto* f = static_cast<std::function<void(CircuitWindow*)>*>(p);
		CircuitWindow* target = windowAlive(s_for) ? s_for : (circuitWindows().empty() ? nullptr : circuitWindows().front());
		if (target) guarded("after Show Me Around", [&] { (*f)(target); });
		return G_SOURCE_REMOVE;
	}, after, [](gpointer p) { delete static_cast<std::function<void(CircuitWindow*)>*>(p); });
}

void stop() {
	CircuitWindow* w = g.win;
	cancelTimers();
	const bool was = g.on;
	g.on = false;
	g.previewing = false;
	g.fadePhase = 0;
	g.pendingStep = -1;
	g.moveStart = -1;
	g.hot = -1;
	if (w && windowAlive(w)) {
		showLayers(false);
		// The panel held open for its stop folds back.
		if (was && w->rail()) w->rail()->refresh();
	}
	dropCard();
	g.win = nullptr;
}

bool active() { return g.on; }
bool activeOn(const CircuitWindow* w) { return g.on && !g.previewing && g.win == w; }

bool key(CircuitWindow* w, const GdkEventKey* e) {
	if (!activeOn(w)) return false;
	const guint mods = e->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK | GDK_META_MASK);
	if (mods) return false;
	switch (e->keyval) {
	case GDK_KEY_Escape: end(); return true;
	case GDK_KEY_Right: case GDK_KEY_KP_Right: case GDK_KEY_Return: case GDK_KEY_KP_Enter: case GDK_KEY_space: next(); return true;
	case GDK_KEY_Left: case GDK_KEY_KP_Left: back(); return true;
	default: break;
	}
	// The function keys keep working (F9 shows or hides the panel, F10 the
	// menus); other plain keys don't reach the canvas meanwhile.
	if (e->keyval >= GDK_KEY_F1 && e->keyval <= GDK_KEY_F35) return false;
	return true;
}

bool holdsPanelOpen(const CircuitWindow* w) { return g.on && g.win == w && g.step == ActionsSpot && g.fadePhase != 1; }

void setThen(std::function<void(CircuitWindow*)> then) { g.then = std::move(then); }
bool hasThen() { return (bool)g.then; }

bool welcomeClosed(WelcomeExit exit) {
	if (prefs().showMeAroundSeen) return false;
	if (exit == WelcomeExit::GuidedTour) g.then = [](CircuitWindow* w) { welcome::startTourOn(w); };
	startWhenReady();
	return true;
}

void startWhenReady() {
	if (prefs().showMeAroundSeen || g.on) return;
	g_wanted = true;
	if (g_quiet || g_autoTimer) return;
	g_readySince = -1;
	g_autoTimer = g_timeout_add(300, autoTick, nullptr);
}

bool watching() { return g_wanted; }
void stopWatching() {
	if (g_autoTimer) g_source_remove(g_autoTimer);
	g_autoTimer = 0;
	g_wanted = false;
}

void setQuiet(bool q) {
	g_quiet = q;
	if (!q && g_wanted && !g_autoTimer) { g_wanted = false; startWhenReady(); }
}
bool quiet() { return g_quiet; }
void setAssumeKeys(bool a) { g_assumeKeys = a; }

CircuitWindow* readyWindow() {
	if (splashShowing() || welcome::window() || whatsnew::window() || welcome::tourActive()) return nullptr;
	// Nothing of ours over the circuit windows: a dialog, Settings, a sheet, the intro.
	bool over = false;
	GList* all = gtk_window_list_toplevels();
	for (GList* l = all; l && !over; l = l->next) {
		GtkWidget* t = GTK_WIDGET(l->data);
		if (gtk_widget_in_destruction(t) || !gtk_widget_get_visible(t) || gtk_window_get_window_type(GTK_WINDOW(t)) != GTK_WINDOW_TOPLEVEL) continue;
		bool circuit = false;
		for (CircuitWindow* c : circuitWindows()) circuit = circuit || GTK_WIDGET(c->window()) == t;
		over = !circuit;
	}
	g_list_free(all);
	if (over) return nullptr;
	CircuitWindow* first = nullptr;
	for (CircuitWindow* c : circuitWindows()) {
		GtkWidget* t = GTK_WIDGET(c->window());
		if (!gtk_widget_get_mapped(t) || c->isFloating()) continue;
		if (gtk_window_is_active(c->window())) return c;
		if (!first) first = c;
	}
	// No window is ever "active" without a window manager (a bare X display).
	if (first && (g_assumeKeys || !wmTracksFocus(gtk_widget_get_display(GTK_WIDGET(first->window()))))) return first;
	return nullptr;
}

void next() {
	if (!g.on) return;
	const int at = g.pendingStep >= 0 ? g.pendingStep : g.step;
	for (int s : shownSteps())
		if (s > at) { go(s); return; }
	end();
}

void back() {
	if (!g.on) return;
	const int at = g.pendingStep >= 0 ? g.pendingStep : g.step;
	int n = -1;
	for (int s : shownSteps()) if (s < at) n = s;
	if (n >= 0) go(n);
}

int step() { return g.pendingStep >= 0 ? g.pendingStep : g.step; }
std::vector<int> shown() { return shownSteps(); }
int position() {
	const std::vector<int> v = shownSteps();
	for (size_t i = 0; i < v.size(); i++) if (v[i] == step()) return (int)i + 1;
	return 0;
}

bool frame(int s, RectF& out) {
	if (s < 0 || s >= SpotCount || !g.has[s]) return false;
	out = g.frames[s];
	return true;
}

RectF cardRect() {
	const PointF c = currentCard();
	return rectF(c.x, c.y, c.x + g.cardW, c.y + g.cardH);
}

}  // namespace showme

// X11's headers last: they define None, Above, Below and more as macros.
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

namespace showme {
namespace {
bool wmTracksFocus(GdkDisplay* d) {
#ifdef GDK_WINDOWING_X11
	if (GDK_IS_X11_DISPLAY(d))
		return gdk_x11_screen_supports_net_wm_hint(gdk_display_get_default_screen(d), gdk_atom_intern_static_string("_NET_ACTIVE_WINDOW"));
#else
	(void)d;
#endif
	return true;
}
}  // namespace
}  // namespace showme
