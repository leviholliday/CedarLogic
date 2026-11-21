// Layout for Tidy Up -- see include/gui/route/Layout.h.

#include "route/Layout.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <functional>
#include <map>
#include <set>

namespace cl {
namespace route {

namespace {

const float EPS = 1e-3f;
const float LAYER_GAP_Y = 1.0f;      // Rearrange: room between gates in a column
const float ALIGN_REACH = 1.5f;      // Keep Layout: furthest a gate moves to line a wire up
const float NUDGE = 1.0f;            // Keep Layout: furthest a gate moves to line up a stack
const float STACK_GAP = 3.0f;        // Keep Layout: most room between gates in one stack
const float LEAD = 2.0f;             // Keep Layout: room between a driver's pin and the pin it feeds when two parts that overlap are wired together
const float SEAT_REACH = 8.0f;       // Keep Layout: a stray label or switch is put beside the pin it's wired to only from this close
const int SEAT_ROWS = 8;             // ...and at most this many steps off level, or
const int SEAT_COLS = 6;             // this many further out, when the level spot is taken
const float PIN_CLEAR = 1.0f;        // Keep Layout: room a wire needs in front of its pin
const float PIN_MARGIN = 1.5f;       // ...beside a part's edge, too
const float RESETTLE_REACH = 12.0f;  // Keep Layout: furthest a part dropped onto another's pins moves to clear them...
const float RESETTLE_CLEAR = 3.0f;   // ...and the room it leaves in front of every pin there
const int CHANNEL_MIN = 3;           // Keep Layout: a gap between two parts that this many wires turn in wants a lane for each...
const float LANE_SIDE = 1.25f;       // ...with this much room for each between parts side by side
const float LANE_ROW = 0.5f;         // ...and this much between parts one above another
const int CHANNEL_SLACK = 1;         // ...is opened when it is more than this many grid steps short
const long CHANNEL_WORK = 4000000;   // ...and the whole pass stops after this many part-to-part checks
const float CHANNEL_DENSE = 0.25f;   // ...and isn't made along an axis where more than this share of gaps are short
const int CHANNEL_DENSE_MIN = 4;     // ...(and more than this many)
const float CHANNEL_SPREAD = 0.1f;   // ...and moves no part further in all than this share of the page
const float CHANNEL_REACH = 8.0f;    // ...or this, if more
const float BLOCK_GAP = 1.5f;        // Rearrange: room kept between separate circuits
const float PIN_OFFSET = 1.5f;        // Rearrange: how far an up/down pin sits off its wire
const int PICTURE_PARTS = 4;         // this many overlapping parts are a drawing
const float OVERLAP_MIN = 0.1f;      // overlap deeper than this counts
const float LIGHT_REACH = 1.5f;      // lights this close together belong to one display
const float LANE = 1.0f;             // Rearrange: room a passing wire keeps in a column
const float GROUP_GAP = 3.0f;        // Rearrange: a gap this wide between drawn groups splits them
const float GROUP_GAP_MAX = 10.0f;   // Rearrange: most room put back between groups
const float LEVEL_SHARE = 0.25f;     // Rearrange: groups level their wires only by this much of their size
const float CAPTION_REACH = 2.0f;    // Rearrange: unwired text this close to a part goes with it...
const float CAPTION_MAX_H = 2.0f;    // ...unless it is taller (a title)...
const float CAPTION_MAX_W = 8.0f;    // ...or wider than this (a sentence)
const int SINK_STRETCH = 1;          // Rearrange: an output goes to the last column only from this few columns before it
const float WIDE = 2.0f;             // Rearrange: groups side by side wider than this many times their height may stack instead
const int LEVEL_ROUNDS = 12;         // Rearrange: passes leveling each part with what it's wired to
const double GIVE_WAY = 0.01;        // Rearrange: how hard a part hanging off a wire holds its place in a column
const float GREW = 1.5f;             // ...and this many times wider for their height than they were drawn

enum Align { LEFT, RIGHT, CENTER };

// Gates line up by the side their wires leave from: outputs-only gates (a
// toggle, a keypad) by their right edge, inputs-only ones (an output label) by
// their left, the rest by their middle.
Align alignOf(const LayoutNode &n) {
	bool l = false, r = false;
	for (const LayoutPin &p : n.pins) { if (p.dx < 0) l = true; if (p.dx > 0) r = true; }
	if (l && !r) return LEFT;
	if (r && !l) return RIGHT;
	return CENTER;
}

// One group laid out in columns by signal flow: inputs first, each gate one
// column right of what drives it, outputs last; sources as far right as
// their sinks allow; gates ordered within a column to cut crossings, with
// terminals (labels, keypads, displays) kept in the order they were drawn
// unless swapping uncrosses wires; wires that pass a column keep a lane of
// their own through it; then each gate as level as it can get with what it
// is wired to. Works on its own copy of the nodes, all of them movable.
class LeafLayout {
public:
	// Caption room: how much of a node's box, on each side, is its captions
	// rather than the part itself. The part reads, orders and lines up in
	// its column as if they weren't there; they only take up room.
	struct Margin { float l = 0.0f, r = 0.0f, b = 0.0f, t = 0.0f; };

	LeafLayout(std::vector<LayoutNode> nodes, float step, std::vector<Margin> margins)
		: N(std::move(nodes)), step(step), n(N.size()), margin(std::move(margins)) {
		margin.resize(n);
		for (size_t i = 0; i < n; i++)
			for (size_t p = 0; p < N[i].pins.size(); p++)
				if (N[i].pins[p].net >= 0) netPins[N[i].pins[p].net].push_back({ (int)i, (int)p });
	}

	// How far to move each node.
	std::vector<std::pair<float, float>> run() {
		std::vector<std::pair<float, float>> off(n, { 0.0f, 0.0f });
		if (n < 2) return off;
		flow();
		layering();
		stacks();
		place();
		// Same top-left corner as before.
		float l0 = FLT_MAX, t0 = -FLT_MAX, l1 = FLT_MAX, t1 = -FLT_MAX;
		for (size_t v = 0; v < n; v++) {
			l0 = std::min(l0, N[v].l); t0 = std::max(t0, N[v].t);
			l1 = std::min(l1, newL[v]); t1 = std::max(t1, y[v] + H((int)v) / 2.0f);
		}
		const float sx = l0 - l1, sy = t0 - t1;
		for (size_t v = 0; v < n; v++) off[v] = { snap(newL[v] + sx - N[v].l), snap(y[v] + sy - cy0[v]) };
		return off;
	}

private:
	std::vector<LayoutNode> N;
	float step;
	size_t n;
	std::vector<Margin> margin;
	std::map<int, std::vector<std::pair<int, int>>> netPins;
	std::vector<std::set<int>> succ, pred;
	std::vector<char> fed;            // driven by something: an output, not a source
	std::vector<int> layer;
	int maxL = 0;
	std::vector<std::vector<int>> layers;
	std::vector<float> cy0, y, h;     // centers and heights, placeholders after the real nodes
	std::vector<float> newL;
	// Parts that cut across the flow (a keypad whose pins leave sideways in a
	// group that runs top to bottom) stand one behind another along the
	// flow, not beside each other with their pins facing a neighbor: the
	// first of them in a column stands for the stack while it is placed.
	std::vector<int> rep;                       // the part standing for v (v itself, mostly)
	std::vector<std::vector<int>> members;      // rep -> its stack in order along x
	// a in an earlier column than b; fa/fb: how each end's pin leaves its
	// part, 0 sideways (or a placeholder), +1 up, -1 down.
	struct Edge { int a; float oa; int fa; int b; float ob; int fb; int net; };
	std::vector<Edge> E;
	std::vector<std::vector<int>> inc;
	std::map<int, std::pair<int, int>> chain;  // placeholder -> the parts its wire runs between
	std::vector<int> dlayer;                   // placeholder's column, by index past n

	float snap(float v) const { return std::round(v / step) * step; }
	float snapUp(float v) const { return std::ceil(v / step - EPS) * step; }
	float W(int i) const { return N[i].r - N[i].l; }
	float H(int i) const { return N[i].t - N[i].b; }
	float CX(int i) const { return (N[i].l + margin[i].l + N[i].r - margin[i].r) / 2.0f; }   // the part's own middle
	float CY(int i) const { return (N[i].b + N[i].t) / 2.0f; }                               // the middle of its room
	float bodyCY(int i) const { return (N[i].b + margin[i].b + N[i].t - margin[i].t) / 2.0f; }
	bool isDummy(int v) const { return v >= (int)n; }
	// A part wired on one side only: a label, a keypad, a display, a switch.
	bool terminal(int v) const {
		if (isDummy(v)) return false;
		bool l = false, r = false, other = false;
		for (const LayoutPin &p : N[v].pins) {
			if (p.net < 0) continue;
			if (p.dx < 0) l = true; else if (p.dx > 0) r = true; else other = true;
		}
		return !other && (l != r);
	}
	// A part with a single wired pin, and that pin on its top or bottom (a
	// tie point hanging under a wire, an LED over one): it wants to sit a
	// step off the wire rather than level with it.
	bool hangs(int v) const {
		if (isDummy(v)) return false;
		int k = 0, vert = 0;
		for (const LayoutPin &p : N[v].pins) if (p.net >= 0) { k++; if (p.dx == 0 && p.dy != 0) vert++; }
		return k == 1 && vert == 1;
	}
	// Room between two neighbors in a column: one-pin parts wired from the
	// side (input and output labels) sit edge to edge, as people draw them,
	// so their wires can meet a gate's closely spaced inputs straight on; a
	// passing wire keeps a lane.
	float gapBetween(int a, int b) const {
		if (isDummy(a) || isDummy(b)) return LANE;
		auto single = [&](int v) {
			const std::vector<LayoutPin> &ps = N[v].pins;
			return ps.size() == 1 && ps[0].dx != 0;
		};
		return (single(a) && single(b)) ? 0.0f : LAYER_GAP_Y;
	}

	// Signal flow: each driver points at what it drives; feedback loops are
	// broken at the edge that points back, walking left to right. Parts are
	// never turned, so the columns follow the way the pins face: a wire from
	// a pin facing left to one facing right (a gate drawn turned round, its
	// output on its left) runs right to left, and the part it feeds goes in
	// an earlier column -- otherwise every such wire loops round both parts.
	void flow() {
		succ.assign(n, {}); pred.assign(n, {}); fed.assign(n, 0);
		for (const auto &np : netPins) {
			std::set<int> drivers, sinks;
			for (const auto &pp : np.second) (N[pp.first].pins[pp.second].output ? drivers : sinks).insert(pp.first);
			if (drivers.size() > 1) {
				// Several pins claim to drive (some parts mark every pin as an
				// output): believe the ones that face right, as outputs do.
				std::set<int> right;
				for (const auto &pp : np.second)
					if (N[pp.first].pins[pp.second].output && N[pp.first].pins[pp.second].dx > 0) right.insert(pp.first);
				if (!right.empty() && right.size() < drivers.size()) {
					for (int d : drivers) if (!right.count(d)) sinks.insert(d);
					drivers = right;
				}
			}
			if (drivers.empty() && sinks.size() >= 2) {
				// No declared driver: read it left to right.
				int left = *sinks.begin();
				for (int s : sinks) if (CX(s) < CX(left)) left = s;
				drivers.insert(left); sinks.erase(left);
			}
			// Which way each part's pins on this net face: +1 right, -1 left.
			auto facing = [&](int v) {
				int f = 0;
				for (const auto &pp : np.second)
					if (pp.first == v) { const int dx = N[v].pins[pp.second].dx; f += (dx > 0) - (dx < 0); }
				return (f > 0) - (f < 0);
			};
			for (int u0 : drivers)
				for (int v0 : sinks) {
					if (u0 == v0) continue;
					fed[v0] = 1;
					int u = u0, v = v0;
					// The driver's pin facing left and the sink's facing right
					// (or one of them so, the other up or down): right to left.
					if (facing(u) - facing(v) < 0) std::swap(u, v);
					succ[u].insert(v); pred[v].insert(u);
				}
		}
		std::vector<int> order(n);
		for (size_t i = 0; i < n; i++) order[i] = (int)i;
		std::sort(order.begin(), order.end(), [&](int a, int b) { return CX(a) < CX(b); });
		std::vector<int> color(n, 0);
		std::vector<std::pair<int, int>> back;
		std::function<void(int)> dfs = [&](int u) {
			color[u] = 1;
			std::vector<int> next(succ[u].begin(), succ[u].end());
			std::sort(next.begin(), next.end(), [&](int a, int b) { return CX(a) < CX(b); });
			for (int v : next) {
				if (color[v] == 1) back.push_back({ u, v });
				else if (color[v] == 0) dfs(v);
			}
			color[u] = 2;
		};
		for (int u : order) if (color[u] == 0 && pred[u].empty()) dfs(u);
		for (int u : order) if (color[u] == 0) dfs(u);
		for (const auto &e : back) { succ[e.first].erase(e.second); pred[e.second].erase(e.first); }
	}

	// Columns: the longest path from an input; outputs all in the last one;
	// a source (a switch, ground) right before the first thing it feeds, or
	// right after it when it is drawn turned round, feeding leftward.
	void layering() {
		layer.assign(n, 0);
		std::vector<int> indeg(n, 0), queue;
		for (size_t v = 0; v < n; v++) { indeg[v] = (int)pred[v].size(); if (indeg[v] == 0) queue.push_back((int)v); }
		for (size_t qi = 0; qi < queue.size(); qi++) {
			const int u = queue[qi];
			for (int v : succ[u]) {
				layer[v] = std::max(layer[v], layer[u] + 1);
				if (--indeg[v] == 0) queue.push_back(v);
			}
		}
		maxL = 0;
		for (size_t v = 0; v < n; v++) maxL = std::max(maxL, layer[v]);
		// Lights go in the last column, so they line up as a row of
		// outputs; so does any other output whose driver is near the end
		// anyway. One fed from far back (a label, a tie point) sits right
		// after its driver instead of at the end of a long wire.
		for (size_t v = 0; v < n; v++) {
			if (!succ[v].empty() || pred[v].empty() || !fed[v]) continue;
			int last = 0;
			for (int u : pred[v]) last = std::max(last, layer[u]);
			layer[v] = (!hangs((int)v) && (N[v].indicator || last + 1 >= maxL - SINK_STRETCH)) ? maxL : last + 1;
		}
		for (size_t v = 0; v < n; v++) {
			if (!pred[v].empty() || succ[v].empty()) continue;
			int first = maxL;
			for (int s : succ[v]) first = std::min(first, layer[s]);
			layer[v] = std::max(0, first - 1);
		}
		// No empty columns.
		std::vector<int> used(maxL + 1, 0);
		for (size_t v = 0; v < n; v++) used[layer[v]] = 1;
		std::vector<int> packed(maxL + 1, 0);
		int next = 0;
		for (int l = 0; l <= maxL; l++) { packed[l] = next; if (used[l]) next++; }
		for (size_t v = 0; v < n; v++) layer[v] = packed[layer[v]];
		maxL = next - 1;
		layers.assign(maxL + 1, {});
		for (size_t v = 0; v < n; v++) layers[layer[v]].push_back((int)v);
	}

	// Finds the parts in each column whose wired pins all leave up or down
	// -- across the flow -- and stacks them along x behind the first.
	void stacks() {
		rep.assign(n, 0);
		members.assign(n, {});
		for (size_t v = 0; v < n; v++) { rep[v] = (int)v; members[v] = { (int)v }; }
		for (auto &lay : layers) {
			std::vector<int> across;
			for (int v : lay) {
				int side = 0, vert = 0;
				for (const LayoutPin &p : N[v].pins) {
					if (p.net < 0) continue;
					if (p.dx != 0) side++; else if (p.dy != 0) vert++;
				}
				if (side == 0 && vert >= 2) across.push_back(v);
			}
			if (across.size() < 2) continue;
			std::sort(across.begin(), across.end(), [&](int a, int b) { return N[a].l + margin[a].l < N[b].l + margin[b].l; });
			const int lead = across[0];
			members[lead] = across;
			for (size_t i = 1; i < across.size(); i++) {
				rep[across[i]] = lead;
				members[across[i]].clear();
				lay.erase(std::find(lay.begin(), lay.end(), across[i]));
			}
		}
	}
	// A stack's width along x, with a gap between its parts.
	float stackW(int v) const {
		float w = 0.0f;
		for (size_t i = 0; i < members[v].size(); i++) w += W(members[v][i]) + (i ? LAYER_GAP_Y : 0.0f);
		return w;
	}

	// Stacks a column top to bottom around its mean height, in its order.
	void stack(std::vector<int> &lay) {
		if (lay.empty()) return;
		float mean = 0.0f, span = 0.0f;
		for (size_t i = 0; i < lay.size(); i++) {
			mean += y[lay[i]]; span += h[lay[i]];
			if (i > 0) span += gapBetween(lay[i - 1], lay[i]);
		}
		mean /= lay.size();
		float top = mean + span / 2.0f;
		for (size_t i = 0; i < lay.size(); i++) {
			if (i > 0) top -= gapBetween(lay[i - 1], lay[i]);
			y[lay[i]] = top - h[lay[i]] / 2.0f;
			top -= h[lay[i]];
		}
	}

	// For v's wires on one side (-1 left, +1 right): v's own port offset and
	// where each far end is.
	void portsOn(int v, int side, std::vector<std::pair<float, float>> &out) const {
		out.clear();
		for (int e : inc[v]) {
			const Edge &ed = E[e];
			if (side < 0 && ed.b == v) out.push_back({ ed.ob, y[ed.a] + ed.oa });
			if (side > 0 && ed.a == v) out.push_back({ ed.oa, y[ed.b] + ed.ob });
		}
	}

	void place() {
		cy0.assign(n, 0.0f); y.assign(n, 0.0f); h.assign(n, 0.0f);
		for (size_t v = 0; v < n; v++) { cy0[v] = CY((int)v); y[v] = cy0[v]; h[v] = H((int)v); }
		for (size_t v = 0; v < n; v++) for (int m : members[v]) h[v] = std::max(h[v], H(m));
		for (auto &lay : layers) std::sort(lay.begin(), lay.end(), [&](int a, int b) { return bodyCY(a) > bodyCY(b); });
		auto faces = [](const LayoutPin &p) { return p.dx != 0 ? 0 : p.dy > 0 ? 1 : p.dy < 0 ? -1 : 0; };

		// A wire that skips columns gets a placeholder in each column it
		// passes: a gate standing in its way counts as a crossing, and the
		// placeholder keeps a lane open for it.
		E.clear(); chain.clear(); dlayer.clear();
		for (const auto &np : netPins)
			for (const auto &pa : np.second)
				for (const auto &pb : np.second) {
					const int u0 = pa.first, v0 = pb.first;
					if (u0 == v0 || !succ[u0].count(v0) || layer[u0] >= layer[v0]) continue;
					const int u = rep[u0], v = rep[v0];
					if (u == v) continue;
					// A stacked part sits level with the part standing for it.
					const float ou = N[u0].pins[pa.second].y - cy0[u0];
					const float ov = N[v0].pins[pb.second].y - cy0[v0];
					const int fu = faces(N[u0].pins[pa.second]), fv = faces(N[v0].pins[pb.second]);
					int prev = u; float po = ou; int pf = fu;
					for (int l = layer[u] + 1; l < layer[v]; l++) {
						const float f = (float)(l - layer[u]) / (float)(layer[v] - layer[u]);
						const int d = (int)y.size();
						chain[d] = { u, v };
						y.push_back(y[u] + ou + (y[v] + ov - y[u] - ou) * f);
						cy0.push_back(y.back());
						h.push_back(0.0f);
						layers[l].push_back(d);
						dlayer.push_back(l);
						E.push_back({ prev, po, pf, d, 0.0f, 0, np.first });
						prev = d; po = 0.0f; pf = 0;
					}
					E.push_back({ prev, po, pf, v, ov, fv, np.first });
				}
		const size_t total = y.size();
		inc.assign(total, {});
		for (size_t e = 0; e < E.size(); e++) { inc[E[e].a].push_back((int)e); inc[E[e].b].push_back((int)e); }

		// Order within each column: by the average height of what each gate
		// is wired to on one side, sweeping right then left. Terminals keep
		// the order they were drawn in among themselves (an output list reads
		// in the order its author chose), taking the slots the sort gives them.
		std::vector<std::pair<float, float>> tmp, tmp2;
		auto reorder = [&](std::vector<int> &lay, int side) {
			std::map<int, float> key;
			for (int v : lay) {
				portsOn(v, side, tmp);
				float sum = 0.0f;
				for (const auto &pt : tmp) sum += pt.second - pt.first;
				key[v] = tmp.empty() ? y[v] : sum / tmp.size();
			}
			std::vector<int> terms;
			for (int v : lay) if (terminal(v)) terms.push_back(v);
			if (terms.size() >= 2) {
				std::vector<float> slots;
				for (int v : terms) slots.push_back(key[v]);
				std::sort(slots.begin(), slots.end(), [](float a, float b) { return a > b; });
				std::sort(terms.begin(), terms.end(), [&](int a, int b) { return bodyCY(a) > bodyCY(b); });
				for (size_t i = 0; i < terms.size(); i++) key[terms[i]] = slots[i];
			}
			std::stable_sort(lay.begin(), lay.end(), [&](int a, int b) {
				if (std::fabs(key[a] - key[b]) > EPS) return key[a] > key[b];
				if (terminal(a) && terminal(b)) return bodyCY(a) > bodyCY(b);
				return y[a] > y[b];   // a tie keeps the current order
			});
			stack(lay);
		};
		// With `above` over `below`, a wire of above's reaching lower than one of
		// below's on the same side is a crossing.
		auto crossings = [&](int above, int below) {
			int c = 0;
			for (int side = -1; side <= 1; side += 2) {
				portsOn(above, side, tmp);
				portsOn(below, side, tmp2);
				for (const auto &pa : tmp) for (const auto &pb : tmp2) if (pa.second < pb.second - EPS) c++;
			}
			return c;
		};
		for (auto &lay : layers) stack(lay);
		for (int sweep = 0; sweep < 6; sweep++) {
			for (int l = 1; l <= maxL; l++) reorder(layers[l], -1);
			for (int l = maxL - 1; l >= 0; l--) reorder(layers[l], +1);
		}
		for (int sweep = 0; sweep < 4; sweep++) {
			bool any = false;
			for (auto &lay : layers) {
				for (size_t pass = 0; pass < lay.size(); pass++) {
					bool swapped = false;
					for (size_t i = 0; i + 1 < lay.size(); i++) {
						if (crossings(lay[i + 1], lay[i]) < crossings(lay[i], lay[i + 1])) {
							std::swap(lay[i], lay[i + 1]);
							swapped = true;
						}
					}
					if (!swapped) break;
					any = true;
				}
				stack(lay);
			}
			if (!any) break;
		}

		// Column x: each as wide as its widest gate, with a lane of room for
		// every wire that has to cross the gap to the next. Parts line up by
		// their own bodies; the column widens on either side for captions
		// that stick out past the others.
		auto ml = [&](int v) { return members[v].size() == 1 ? margin[v].l : 0.0f; };
		auto mr = [&](int v) { return members[v].size() == 1 ? margin[v].r : 0.0f; };
		auto bodyW = [&](int v) { return stackW(v) - ml(v) - mr(v); };
		std::vector<float> colB(maxL + 1, 0.0f), extL(maxL + 1, 0.0f), extR(maxL + 1, 0.0f);
		for (size_t v = 0; v < n; v++) if (rep[v] == (int)v) colB[layer[v]] = std::max(colB[layer[v]], snapUp(bodyW((int)v)));
		auto bodyOff = [&](int v) {
			const float slack = colB[layer[v]] - bodyW(v);
			switch (alignOf(N[v])) {
			case LEFT:  return 0.0f;
			case RIGHT: return slack;
			default:    return slack / 2.0f;
			}
		};
		for (size_t v = 0; v < n; v++) {
			if (rep[v] != (int)v) continue;
			const int l = layer[v];
			extL[l] = std::max(extL[l], snapUp(ml((int)v) - bodyOff((int)v)));
			extR[l] = std::max(extR[l], snapUp(bodyOff((int)v) + bodyW((int)v) + mr((int)v) - colB[l]));
		}
		std::vector<float> colW(maxL + 1, 0.0f), colX(maxL + 1, 0.0f), gap(maxL + 1, 0.0f);
		for (int l = 0; l <= maxL; l++) colW[l] = extL[l] + colB[l] + extR[l];
		for (const auto &np : netPins) {
			int lo = maxL + 1, hi = -1;
			for (const auto &pp : np.second) { lo = std::min(lo, layer[pp.first]); hi = std::max(hi, layer[pp.first]); }
			for (int l = lo; l < hi; l++) gap[l] += 1.0f;
		}
		for (int l = 1; l <= maxL; l++) colX[l] = colX[l - 1] + colW[l - 1] + 1.5f + gap[l - 1];
		auto placeX = [&]() {
			newL.assign(n, 0.0f);
			for (size_t v = 0; v < n; v++) {
				if (rep[v] != (int)v) continue;
				const int l = layer[v];
				const float base = colX[l] + extL[l], w = bodyW((int)v);
				float left;
				switch (alignOf(N[v])) {
				case LEFT:   left = base; break;
				case RIGHT:  left = base + colB[l] - w; break;
				default:     left = snap(base + (colB[l] - w) / 2.0f); break;
				}
				left -= ml((int)v);
				for (int m : members[v]) { newL[m] = left; left += W(m) + LAYER_GAP_Y; }
			}
		};
		placeX();

		// Heights: each gate as level as it can get with what it's wired to,
		// each passing wire as straight, keeping the column's order and
		// spacing (pool-adjacent-violators).
		for (int round = 0; round < LEVEL_ROUNDS; round++) {
			for (int pass = 0; pass < 2; pass++) {
				for (int li = 0; li <= maxL; li++) {
					const int l = pass == 0 ? li : maxL - li;
					std::vector<int> &lay = layers[l];
					if (lay.empty()) continue;
					std::vector<float> want;
					for (int v : lay) {
						// Each wire's near end (through the lane it takes across
						// any column between) says where this part wants to be:
						// level with a side pin; a little above or below one that
						// faces up or down (an LED over its wire, a switch beside
						// a top input).
						std::vector<float> cand;
						bool anyUp = false, anyDown = false, anySide = false;
						// A part hanging off one wire follows the part it's wired to
						// rather than pulling it off its others: each would wait for the
						// other, and the pair would stay wherever it was drawn.
						bool others = false;
						auto far = [&](int e) {
							const int o = E[e].a == v ? E[e].b : E[e].a;
							if (!isDummy(o)) return o;
							const auto &c = chain[o];
							return c.first == v ? c.second : c.first;
						};
						for (int e : inc[v]) if (!hangs(far(e))) others = true;
						for (int e : inc[v]) {
							const Edge &ed = E[e];
							const bool mine = ed.a == v;
							if (others && !isDummy(v) && hangs(far(e))) continue;
							const float po = mine ? ed.oa : ed.ob, qo = mine ? ed.ob : ed.oa;
							const int pf = mine ? ed.fa : ed.fb, qf = mine ? ed.fb : ed.fa;
							const float qy = y[mine ? ed.b : ed.a] + qo;
							float py;
							if (pf == 0 && qf == 0) py = qy;
							else if (pf == 0) py = qy + (qf > 0 ? PIN_OFFSET : -PIN_OFFSET);
							else if (qf == 0) py = qy + (pf < 0 ? PIN_OFFSET : -PIN_OFFSET);
							else continue;
							if (pf > 0) anyUp = true; else if (pf < 0) anyDown = true; else anySide = true;
							cand.push_back(py - po);
						}
						if (cand.empty() && !inc[v].empty()) {
							// Wired only pin to pin from top or bottom (one tie
							// point hanging off another): sit a step off the other
							// part's pin rather than stay wherever it was drawn.
							for (int e : inc[v]) {
								const Edge &ed = E[e];
								const bool mine = ed.a == v;
								const float po = mine ? ed.oa : ed.ob, qo = mine ? ed.ob : ed.oa;
								const int pf = mine ? ed.fa : ed.fb;
								cand.push_back(y[mine ? ed.b : ed.a] + qo + (pf < 0 ? PIN_OFFSET : -PIN_OFFSET) - po);
							}
							std::sort(cand.begin(), cand.end());
							want.push_back(cand[cand.size() / 2]);
							continue;
						}
						if (cand.empty()) { want.push_back(y[v]); continue; }
						std::sort(cand.begin(), cand.end());
						// Pins that all leave upward (a keypad's, in a group that
						// runs top to bottom) want every wire to turn toward its
						// target, so the part sits below all of them; downward
						// ones above all. Side pins take the middle.
						if (anyUp && !anyDown && !anySide) want.push_back(cand.front());
						else if (anyDown && !anyUp && !anySide) want.push_back(cand.back());
						else if (cand.size() % 2 == 0) want.push_back((cand[cand.size() / 2 - 1] + cand[cand.size() / 2]) / 2.0f);
						else want.push_back(cand[cand.size() / 2]);
					}
					fitColumn(lay, want);
				}
			}
		}
		{
			// Room between columns: a lane for each wire that turns there,
			// half of one for a wire running straight across (the router
			// still lays it beside the others).
			auto colOf = [&](int v) { return isDummy(v) ? dlayer[v - n] : layer[v]; };
			std::vector<std::set<int>> turning(maxL + 1);
			for (const Edge &ed : E) {
				const float ya = y[ed.a] + ed.oa, yb = y[ed.b] + ed.ob;
				if (std::fabs(ya - yb) > EPS || ed.fa != 0 || ed.fb != 0) turning[std::min(colOf(ed.a), colOf(ed.b))].insert(ed.net);
			}
			for (const auto &np : netPins) {
				// a net with pins in one column runs up or down beside it
				std::map<int, int> per;
				for (const auto &pp : np.second) per[layer[pp.first]]++;
				for (const auto &pc : per) if (pc.second > 1 && pc.first < maxL) turning[pc.first].insert(np.first);
			}
			for (int l = 0; l <= maxL; l++) gap[l] = (gap[l] + (float)turning[l].size()) / 2.0f;
			for (int l = 1; l <= maxL; l++) colX[l] = colX[l - 1] + colW[l - 1] + 1.5f + gap[l - 1];
			placeX();
		}
		// Placeholders have done their job; stacked parts sit level with their lead.
		for (auto &lay : layers)
			lay.erase(std::remove_if(lay.begin(), lay.end(), [&](int v) { return isDummy(v); }), lay.end());
		y.resize(n);
		for (size_t v = 0; v < n; v++) if (rep[v] != (int)v) y[v] = y[rep[v]];
	}

	// Closest heights to `want` that keep `lay`'s top-to-bottom order with
	// the gaps between neighbors: shift each target by the room above it so
	// the constraint becomes "never increasing", then pool adjacent violators.
	void fitColumn(const std::vector<int> &lay, const std::vector<float> &want) {
		const size_t m = lay.size();
		std::vector<float> S(m, 0.0f);
		for (size_t i = 1; i < m; i++) S[i] = S[i - 1] + h[lay[i - 1]] / 2.0f + h[lay[i]] / 2.0f + gapBetween(lay[i - 1], lay[i]);
		// A part hanging off a wire gives way to its neighbors rather than
		// pushing them (and through them everything they're wired to) off
		// their wires.
		std::vector<double> sum, wt;
		std::vector<int> cnt;
		for (size_t i = 0; i < m; i++) {
			const double w = hangs(lay[i]) ? GIVE_WAY : 1.0;
			sum.push_back(w * (want[i] + S[i])); wt.push_back(w); cnt.push_back(1);
			while (sum.size() >= 2 && sum[sum.size() - 2] / wt[wt.size() - 2] < sum.back() / wt.back()) {
				sum[sum.size() - 2] += sum.back(); wt[wt.size() - 2] += wt.back(); cnt[cnt.size() - 2] += cnt.back();
				sum.pop_back(); wt.pop_back(); cnt.pop_back();
			}
		}
		size_t i = 0;
		for (size_t b = 0; b < sum.size(); b++)
			for (int k = 0; k < cnt[b]; k++, i++) y[lay[i]] = (float)(sum[b] / wt[b]) - S[i];
	}
};

class Layouter {
public:
	explicit Layouter(const LayoutInput &in) : in(in), n(in.nodes.size()), ox(n, 0.0f), oy(n, 0.0f), movable(n, false),
	                                            el(n), eb(n), er(n), et(n) {
		for (size_t i = 0; i < n; i++) {
			movable[i] = in.nodes[i].movable;
			el[i] = in.nodes[i].l; eb[i] = in.nodes[i].b; er[i] = in.nodes[i].r; et[i] = in.nodes[i].t;
			for (size_t p = 0; p < in.nodes[i].pins.size(); p++)
				if (in.nodes[i].pins[p].net >= 0) netPins[in.nodes[i].pins[p].net].push_back({ (int)i, (int)p });
		}
		holdPictures();
	}

	std::vector<std::pair<float, float>> run() {
		if (in.mode == TidyMode::Rearrange) rearrange(); else keepLayout();
		std::vector<std::pair<float, float>> out(n);
		for (size_t i = 0; i < n; i++)
			out[i] = movable[i] ? std::make_pair(snap(ox[i]), snap(oy[i])) : std::make_pair(0.0f, 0.0f);
		return out;
	}

private:
	const LayoutInput &in;
	size_t n;
	std::vector<float> ox, oy;
	std::vector<char> movable;
	std::map<int, std::vector<std::pair<int, int>>> netPins;   // net -> (node, pin)
	// Each part's footprint before any move. The same as the node's own,
	// except in Rearrange for a part with captions: theirs is added to it.
	std::vector<float> el, eb, er, et;
	std::set<std::pair<int, int>> drawnOver;   // Keep Layout: pairs that overlapped as drawn

	// A drawing, not a circuit to lay out, stays exactly as drawn: parts drawn
	// overlapping in a cluster of PICTURE_PARTS or more, or that many lights
	// packed within a step or two of each other (the segments of a display).
	void holdPictures() {
		std::vector<int> parent(n);
		for (size_t i = 0; i < n; i++) parent[i] = (int)i;
		std::function<int(int)> find = [&](int x) { return parent[x] == x ? x : parent[x] = find(parent[x]); };
		for (size_t i = 0; i < n; i++)
			for (size_t j = i + 1; j < n; j++) {
				const LayoutNode &a = in.nodes[i], &b = in.nodes[j];
				const float reach = (a.indicator && b.indicator) ? -LIGHT_REACH : OVERLAP_MIN;
				if (a.l < b.r - reach && b.l < a.r - reach && a.b < b.t - reach && b.b < a.t - reach)
					parent[find((int)i)] = find((int)j);
			}
		std::map<int, int> size;
		for (size_t i = 0; i < n; i++) size[find((int)i)]++;
		for (size_t i = 0; i < n; i++) if (size[find((int)i)] >= PICTURE_PARTS) movable[i] = 0;
	}

	float snap(float v) const { return std::round(v / in.step) * in.step; }
	float snapUp(float v) const { return std::ceil(v / in.step - EPS) * in.step; }
	float L(int i) const { return el[i] + ox[i]; }
	float R(int i) const { return er[i] + ox[i]; }
	float B(int i) const { return eb[i] + oy[i]; }
	float T(int i) const { return et[i] + oy[i]; }
	float W(int i) const { return in.nodes[i].r - in.nodes[i].l; }
	float H(int i) const { return in.nodes[i].t - in.nodes[i].b; }
	float CX(int i) const { return (L(i) + R(i)) / 2.0f; }
	float CY(int i) const { return (B(i) + T(i)) / 2.0f; }
	bool vOverlap(int i, int j, float gap) const { return B(i) < T(j) + gap - EPS && B(j) < T(i) + gap - EPS; }

	// ---------------------------------------------------------------- Keep Layout

	void keepLayout() {
		std::vector<int> mv;
		for (size_t i = 0; i < n; i++) if (movable[i]) mv.push_back((int)i);
		if (mv.empty()) return;

		// Stacks: gates sitting nearly one above another, close together, that
		// share an edge or a middle within a step or two. Chained, so a whole
		// column of inverters is one stack.
		std::vector<int> parent(n);
		for (size_t i = 0; i < n; i++) parent[i] = (int)i;
		std::function<int(int)> find = [&](int x) { return parent[x] == x ? x : parent[x] = find(parent[x]); };
		for (size_t a = 0; a < mv.size(); a++)
			for (size_t c = a + 1; c < mv.size(); c++) {
				const int i = mv[a], j = mv[c];
				if (vOverlap(i, j, 0.0f)) continue;
				const float gapY = std::max(B(i) - T(j), B(j) - T(i));
				if (gapY > STACK_GAP) continue;
				const bool lined = std::fabs(CX(i) - CX(j)) <= NUDGE || std::fabs(L(i) - L(j)) <= NUDGE ||
				                   std::fabs(R(i) - R(j)) <= NUDGE;
				if (lined) parent[find(i)] = find(j);
			}
		std::map<int, std::vector<int>> stacks;
		for (int i : mv) stacks[find(i)].push_back(i);

		// Line each stack up on the edge most of its gates share -- only gates
		// already within a nudge of it move.
		for (auto &st : stacks) {
			std::vector<int> &col = st.second;
			if (col.size() < 2) continue;
			int count[3] = { 0, 0, 0 };
			for (int j : col) count[alignOf(in.nodes[j])]++;
			Align a = CENTER;
			if (count[LEFT] > count[RIGHT] && count[LEFT] > count[CENTER]) a = LEFT;
			else if (count[RIGHT] > count[LEFT] && count[RIGHT] > count[CENTER]) a = RIGHT;
			std::vector<float> edge;
			for (int j : col) edge.push_back(a == LEFT ? L(j) : a == RIGHT ? R(j) : CX(j));
			std::vector<float> sorted = edge;
			std::sort(sorted.begin(), sorted.end());
			const float target = sorted[sorted.size() / 2];
			// A part that would land on a neighbor stays: parts drawn side by side
			// in a row (tie points) chain into a stack through what's under them.
			for (size_t k = 0; k < col.size(); k++)
				if (std::fabs(target - edge[k]) <= NUDGE + EPS) {
					const float s = snap(target - edge[k]);
					if (!newHit(col[k], s, 0.0f)) ox[col[k]] += s;
				}
		}

		orderLeaves(mv);

		// Level each gate with what it's wired to, drivers first, so wires that
		// miss by a step or three run straight.
		std::vector<int> byX = mv;
		std::sort(byX.begin(), byX.end(), [&](int a, int b) { return CX(a) < CX(b); });
		for (int j : byX) {
			const std::vector<int> &mine = stacks[find(j)];
			oy[j] += bestShift(j, std::set<int>(mine.begin(), mine.end()));
		}

		// Parts drawn on top of each other, before any are pushed apart.
		for (int i : mv)
			for (size_t k = 0; k < n; k++)
				if ((int)k != i && L(i) < R((int)k) - EPS && L((int)k) < R(i) - EPS && B(i) < T((int)k) - EPS && B((int)k) < T(i) - EPS)
					drawnOver.insert({ std::min(i, (int)k), std::max(i, (int)k) });
		separate(mv);
		if (seatLeaves(mv)) separate(mv);
		// Again now nothing overlaps: parts pushed clear of each other land
		// stacked in whatever order they were dropped.
		orderLeaves(mv);
		openChannels();
	}

	// ------------------------------------------------ Keep Layout: channels
	// Measured along an axis: 0 runs down the page, 1 runs right. `lo`/`hi`
	// are a part's near and far edge along it, `pl`/`ph` its span across it.
	float lo(int i, int ax) const { return ax ? L(i) : -T(i); }
	float hi(int i, int ax) const { return ax ? R(i) : -B(i); }
	float pl(int i, int ax) const { return ax ? B(i) : L(i); }
	float ph(int i, int ax) const { return ax ? T(i) : R(i); }
	void shift(int i, int ax, float d) { if (ax) ox[i] += d; else oy[i] -= d; }
	// A pin's place along the axis, across it, and which way along it it faces.
	void pinAt(int v, const LayoutPin &p, int ax, float &u, float &w, int &f) const {
		const float x = p.x + ox[v], y = p.y + oy[v];
		if (ax) { u = x; w = y; f = p.dx; } else { u = -y; w = x; f = -p.dy; }
	}

	// A pin whose wire runs straight on, level, to a pin facing it: it needs
	// no lane of its own in the gap it crosses.
	bool runsStraight(int v, const LayoutPin &p, int ax) const {
		float u, w; int f;
		pinAt(v, p, ax, u, w, f);
		auto it = netPins.find(p.net);
		if (f == 0 || it == netPins.end()) return false;
		for (const auto &qq : it->second) {
			const LayoutPin &q = in.nodes[qq.first].pins[qq.second];
			if (&q == &p) continue;
			float qu, qw; int qf;
			pinAt(qq.first, q, ax, qu, qw, qf);
			if (qf == -f && std::fabs(qw - w) < EPS && (qu - u) * f > -EPS) return true;
		}
		return false;
	}

	// The room the gap from `a` to the part `b` after it wants: a lane for
	// each wire that has to turn in it -- leaving a pin of one facing the
	// other, and not running straight across -- and one more beside them;
	// 0 when fewer than CHANNEL_MIN wires turn there, or either side is a
	// part with one wire (a label, a ground): that wire is one of them.
	float channelNeed(int a, int b, int ax) const {
		int pin = -1;
		if (wires(a, pin) < 2 || wires(b, pin) < 2) return 0.0f;
		const float l = std::max(pl(a, ax), pl(b, ax)) - PIN_MARGIN, h = std::min(ph(a, ax), ph(b, ax)) + PIN_MARGIN;
		std::set<int> nets;
		for (int side = 0; side < 2; side++) {
			const int v = side ? b : a;
			for (const LayoutPin &p : in.nodes[v].pins) {
				if (p.net < 0) continue;
				float u, w; int f;
				pinAt(v, p, ax, u, w, f);
				if (f != (side ? -1 : 1) || w <= l || w >= h || runsStraight(v, p, ax)) continue;
				nets.insert(p.net);
			}
		}
		return (int)nets.size() >= CHANNEL_MIN ? (float)(nets.size() + 1) * (ax ? LANE_SIDE : LANE_ROW) : 0.0f;
	}

	bool acrossOverlap(int a, int b, int ax) const { return pl(a, ax) < ph(b, ax) - EPS && pl(b, ax) < ph(a, ax) - EPS; }

	// Parts lined up in a column, close together: they move sideways as one.
	// Worked out for every part at once, again only after something moves.
	std::vector<int> colOf;                    // part -> its column
	std::vector<std::vector<int>> colParts;    // column -> its parts
	bool colsStale = true;
	long chanWork = 0;                         // checks made so far by openChannels
	void findColumns() {
		std::vector<int> parent(n);
		for (size_t i = 0; i < n; i++) parent[i] = (int)i;
		std::function<int(int)> find = [&](int x) { return parent[x] == x ? x : parent[x] = find(parent[x]); };
		for (size_t a = 0; a < n; a++)
			for (size_t c = a + 1; c < n; c++) {
				const int i = (int)a, j = (int)c;
				if (vOverlap(i, j, 0.0f) || std::max(B(i) - T(j), B(j) - T(i)) > STACK_GAP) continue;
				if (std::fabs(CX(i) - CX(j)) <= EPS || std::fabs(L(i) - L(j)) <= EPS || std::fabs(R(i) - R(j)) <= EPS)
					parent[find(i)] = find(j);
			}
		std::map<int, int> id;
		colOf.assign(n, 0);
		colParts.clear();
		for (size_t i = 0; i < n; i++) {
			auto it = id.emplace(find((int)i), (int)colParts.size()).first;
			if (it->second == (int)colParts.size()) colParts.emplace_back();
			colOf[i] = it->second;
			colParts[it->second].push_back((int)i);
		}
		chanWork += (long)(n * n / 2);
		colsStale = false;
	}
	const std::vector<int> &columnOf(int v) {
		if (colsStale) findColumns();
		return colParts[colOf[v]];
	}

	// Moves `start` by `d` along the axis (dir +1 onwards, -1 back) and works
	// out what has to go with it: parts it would close a gap on (only as far
	// as the gap's own wires need, never onto them), parts lined up with it
	// in a column when it goes sideways, and labels on a level wire from it.
	// False if a part that can't move would have to.
	bool cascade(int start, int ax, int dir, float d, std::map<int, float> &out) {
		out.clear();
		out[start] = d;
		std::vector<int> todo = { start };
		auto raise = [&](int k, float by) {
			float &cur = out[k];
			if (by > cur + EPS) { cur = by; todo.push_back(k); }
		};
		for (size_t guard = 0; !todo.empty() && guard < 64 * n; guard++) {
			const int v = todo.back();
			todo.pop_back();
			const float dv = out[v];
			chanWork += (long)n;
			if (chanWork > CHANNEL_WORK) return false;
			if (ax) for (int k : columnOf(v)) raise(k, dv);
			int pin = -1;
			for (const LayoutPin &p : in.nodes[v].pins) {
				if (p.net < 0) continue;
				for (const auto &qq : netPins.at(p.net)) {
					const int k = qq.first;
					if (k == v || wires(k, pin) != 1) continue;
					float u, w, qu, qw; int f, qf;
					pinAt(v, p, ax, u, w, f);
					pinAt(k, in.nodes[k].pins[qq.second], ax, qu, qw, qf);
					if (f == 0 && qf == 0 && std::fabs(qu - u) < EPS) raise(k, dv);
				}
			}
			for (size_t kk = 0; kk < n; kk++) {
				const int k = (int)kk;
				if (k == v || !acrossOverlap(v, k, ax)) continue;
				const bool ahead = dir > 0 ? lo(k, ax) >= hi(v, ax) - EPS : hi(k, ax) <= lo(v, ax) + EPS;
				if (!ahead) continue;
				const float before = dir > 0 ? lo(k, ax) - hi(v, ax) : lo(v, ax) - hi(k, ax);
				const float keep = std::max(0.0f, std::min(before, std::max(STACK_GAP, dir > 0 ? channelNeed(v, k, ax) : channelNeed(k, v, ax))));
				const float after = before - dv + (out.count(k) ? out[k] : 0.0f);
				if (keep - after > EPS) raise(k, (out.count(k) ? out[k] : 0.0f) + snapUp(keep - after));
			}
		}
		if (!todo.empty()) return false;
		for (const auto &m : out) if (!movable[m.first]) return false;
		return true;
	}

	// Along one axis: how many parts have a neighbour ahead (`nb`), and how
	// many of those gaps are short of their wires (`sh`).
	void neighbours(int ax, int &nb, int &sh) {
		nb = sh = 0;
		for (size_t ia = 0; ia < n; ia++) {
			int best = -1; float bg = FLT_MAX;
			for (size_t ib = 0; ib < n; ib++) {
				const int a = (int)ia, b = (int)ib;
				if (a == b || (!movable[a] && !movable[b]) || !acrossOverlap(a, b, ax)) continue;
				const float gap = lo(b, ax) - hi(a, ax);
				if (gap < -EPS || gap >= bg) continue;
				bg = gap; best = b;
			}
			chanWork += (long)n;
			if (best < 0) continue;
			nb++;
			const float need = channelNeed((int)ia, best, ax);
			if (need > 0.0f && bg < need - CHANNEL_SLACK * in.step - EPS) sh++;
		}
	}
	// Gaps too narrow for the wires that turn in them open up: the parts on
	// one side (the side that moves less) step away, with whatever they
	// would close in on, never past each other. Never more than CHANNEL_WORK
	// checks in all: past that it stops, and keeps the gaps it has opened.
	// A page where many of the gaps are short is packed evenly, not jammed in
	// a few places: that axis is left as drawn rather than spread out. Nor
	// does any part go further than CHANNEL_SPREAD of the page (or
	// CHANNEL_REACH) in all from here.
	void openChannels() {
		chanWork = 0;
		colsStale = true;
		bool dense[2];
		float reach[2];
		for (int ax = 0; ax < 2; ax++) {
			int nb, sh;
			neighbours(ax, nb, sh);
			dense[ax] = sh > std::max(CHANNEL_DENSE_MIN, (int)(nb * CHANNEL_DENSE));
			float l = FLT_MAX, h = -FLT_MAX;
			for (size_t i = 0; i < n; i++) { l = std::min(l, lo((int)i, ax)); h = std::max(h, hi((int)i, ax)); }
			reach[ax] = std::max(CHANNEL_REACH, CHANNEL_SPREAD * (h - l));
		}
		std::vector<float> went[2] = { std::vector<float>(n, 0.0f), std::vector<float>(n, 0.0f) };
		for (int pass = 0; pass < 8; pass++) {
			bool any = false;
			for (int ax = 0; ax < 2; ax++)
				for (size_t ia = 0; ia < n && !dense[ax]; ia++)
					for (size_t ib = 0; ib < n; ib++) {
						const int a = (int)ia, b = (int)ib;
						if (++chanWork > CHANNEL_WORK) return;
						if (a == b || (!movable[a] && !movable[b]) || !acrossOverlap(a, b, ax)) continue;
						const float gap = lo(b, ax) - hi(a, ax);
						if (gap < -EPS) continue;
						const float need = channelNeed(a, b, ax);
						if (need <= 0.0f || gap >= need - CHANNEL_SLACK * in.step - EPS) continue;
						bool blocked = false;
						chanWork += (long)n;
						for (size_t k = 0; k < n && !blocked; k++) {
							const int j = (int)k;
							if (j == a || j == b) continue;
							blocked = lo(j, ax) < lo(b, ax) && hi(j, ax) > hi(a, ax) &&
							          pl(j, ax) < std::min(ph(a, ax), ph(b, ax)) && ph(j, ax) > std::max(pl(a, ax), pl(b, ax));
						}
						if (blocked) continue;
						const float d = snapUp(need - gap);
						std::map<int, float> fw, bw;
						const bool okF = cascade(b, ax, 1, d, fw), okB = cascade(a, ax, -1, d, bw);
						if (chanWork > CHANNEL_WORK) return;
						if (!okF && !okB) continue;
						auto cost = [&](const std::map<int, float> &m) {
							float c = 0.0f;
							for (const auto &e : m) c += W(e.first) * H(e.first) * e.second;
							return c;
						};
						// Parts wired on one side only (a keypad, a display) step back
						// first: they have no wires behind them to crowd.
						auto twoWay = [&](const std::map<int, float> &m) {
							for (const auto &e : m) if (!oneWay(e.first)) return true;
							return false;
						};
						bool forward = okF && (!okB || cost(fw) <= cost(bw) + EPS);
						if (okF && okB && twoWay(fw) != twoWay(bw)) forward = !twoWay(fw);
						const std::map<int, float> &m = forward ? fw : bw;
						bool far = false;
						for (const auto &e : m) far = far || std::fabs(went[ax][e.first] + (forward ? e.second : -e.second)) > reach[ax] + EPS;
						if (far) continue;
						for (const auto &e : m) {
							shift(e.first, ax, forward ? e.second : -e.second);
							went[ax][e.first] += forward ? e.second : -e.second;
						}
						colsStale = true;
						any = true;
					}
			if (!any) break;
		}
	}

	// No two gates overlap afterwards: push the lower one down.
	void separate(const std::vector<int> &mv) {
		for (int pass = 0; pass < 16; pass++) {
			bool moved = false;
			std::vector<int> byTop = mv;
			std::sort(byTop.begin(), byTop.end(), [&](int a, int b) { return T(a) > T(b); });
			for (size_t a = 0; a < byTop.size(); a++)
				for (size_t c = a + 1; c < byTop.size(); c++) {
					const int i = byTop[a], j = byTop[c];
					// Real overlaps only: parts drawn edge to edge (a stack of
					// input labels) are meant to touch.
					const bool hits = L(i) < R(j) - EPS && L(j) < R(i) - EPS && B(i) < T(j) - EPS && B(j) < T(i) - EPS;
					if (!hits) continue;
					// Two parts wired straight to each other (a switch on top of its
					// label, say): the one that is fed goes to the right of its driver,
					// level, so the wire runs straight. Moving it down instead leaves
					// the wire running backwards, and the router has to loop it round.
					int src = -1, dst = -1;
					float need = 0.0f;
					if (backwardLink(i, j, src, dst, need) || backwardLink(j, i, src, dst, need)) {
						const float dx = need > 0 ? snapUp(need) : -snapUp(-need);
						// Level, too, if that lands it on nothing new: a gate fed
						// several wires by a part it was dropped on goes back beside it.
						const float dy = levelWith(src, dst);
						ox[dst] += dx;
						if (dy != 0.0f && !newHit(dst, 0.0f, dy)) oy[dst] += dy;
						moved = true; continue;
					}
					// Two parts drawn on top of each other where, pushed down, j would
					// sit tight under i with several pins of one facing the other and
					// no room for their wires (a keypad dropped onto the adder it
					// feeds). The smaller goes to the nearest spot where it covers no
					// one's pins, if it only feeds or is only fed (a keypad, a
					// display); one in a chain stays in its column, and the gap gets
					// a lane for each of those wires instead. (Parts with one wire
					// are seated in front of their pin later.)
					float drop = snapUp(T(j) - B(i));
					int pin = -1;
					const bool drawn = drawnOver.count({ std::min(i, j), std::max(i, j) }) > 0;
					const int blocked = drawn && wires(i, pin) >= 2 && wires(j, pin) >= 2 ? facingPins(i, j, -drop) : 0;
					if (blocked >= 2) {
						const int s = W(i) * H(i) <= W(j) * H(j) ? i : j;
						if (oneWay(s)) { if (resettle(s)) { moved = true; continue; } }
						else drop = snapUp(T(j) - B(i) + PIN_CLEAR + blocked * in.step);
					}
					oy[j] -= drop; moved = true;
				}
			if (!moved) break;
		}
	}

	// How many of a part's pins are wired; `pin` is the last of them.
	int wires(int v, int &pin) const {
		int count = 0;
		for (size_t k = 0; k < in.nodes[v].pins.size(); k++)
			if (in.nodes[v].pins[k].net >= 0) { count++; pin = (int)k; }
		return count;
	}

	// Labels (or switches, lights) stacked in a column beside the part they
	// are each wired to, in a different order from its pins: their wires have
	// to cross in the gap between, where there's room for one to and not two,
	// and the second is sent right round the column. They trade places -- the
	// same spots, in the order of the pins.
	void orderLeaves(const std::vector<int> &mv) {
		struct Leaf { int v; float pinX, pinY; };   // its pin's x, the y of the pin it's wired to
		std::map<std::pair<int, int>, std::vector<Leaf>> sides;   // (part, side of it) -> leaves there
		for (int v : mv) {
			int pi = -1;
			if (wires(v, pi) != 1) continue;
			const LayoutPin &p = in.nodes[v].pins[pi];
			if (p.dx == 0) continue;
			const auto &np = netPins[p.net];
			if (np.size() != 2) continue;
			const std::pair<int, int> &o = np[0].first == v ? np[1] : np[0];
			if (o.first == v) continue;
			const LayoutPin &q = in.nodes[o.first].pins[o.second];
			if (q.dx != -p.dx || ((q.x + ox[o.first]) - (p.x + ox[v])) * p.dx <= EPS) continue;
			sides[{ o.first, q.dx }].push_back({ v, p.x + ox[v], q.y + oy[o.first] });
		}
		for (auto &sd : sides) {
			std::vector<Leaf> &all = sd.second;
			std::sort(all.begin(), all.end(), [&](const Leaf &a, const Leaf &b) { return T(a.v) > T(b.v); });
			for (size_t i = 0; i < all.size();) {
				// A run: same size, pins in one column, one right under the next.
				size_t j = i;
				while (j + 1 < all.size()) {
					const int a = all[j].v, b = all[j + 1].v;
					const float gap = B(a) - T(b);
					if (std::fabs(W(a) - W(b)) > EPS || std::fabs(H(a) - H(b)) > EPS || std::fabs(all[j].pinX - all[j + 1].pinX) > EPS ||
					    gap < -EPS || gap > STACK_GAP + EPS) break;
					j++;
				}
				if (j > i) {
					std::vector<Leaf> want(all.begin() + i, all.begin() + j + 1);
					std::stable_sort(want.begin(), want.end(), [](const Leaf &a, const Leaf &b) { return a.pinY > b.pinY + EPS; });
					std::vector<std::pair<float, float>> spot;
					for (size_t k = i; k <= j; k++) spot.push_back({ L(all[k].v), B(all[k].v) });
					for (size_t k = 0; k < want.size(); k++) {
						const int v = want[k].v;
						const float dx = spot[k].first - L(v), dy = spot[k].second - B(v);
						ox[v] += dx; oy[v] += dy;
					}
				}
				i = j + 1;
			}
		}
	}

	// A part with one wire (a label, a switch, a light) left on the wrong side
	// of the pin it's wired to -- its wire would have to leave it, turn back
	// and loop round to come in from the front. It goes to the nearest free
	// spot in front of that pin instead, level with it when there's room.
	bool seatLeaves(const std::vector<int> &mv) {
		bool any = false;
		for (int v : mv) {
			int pi = -1;
			if (wires(v, pi) != 1) continue;
			const LayoutPin &p = in.nodes[v].pins[pi];
			if (p.dx == 0) continue;
			const float px = p.x + ox[v], py = p.y + oy[v];
			// The pins facing this one on its wire: fine if any is ahead of it.
			int best = -1, bestPin = -1;
			float bestD = FLT_MAX, bestGap = 0.0f;
			bool ahead = false;
			for (const auto &qq : netPins[p.net]) {
				if (qq.first == v) continue;
				const LayoutPin &q = in.nodes[qq.first].pins[qq.second];
				if (q.dx != -p.dx) continue;
				const float qx = q.x + ox[qq.first], qy = q.y + oy[qq.first];
				const float gap = (qx - px) * p.dx;
				// Ahead with room for the wire to turn, or touching and level.
				if (gap >= 2 * in.step - EPS || (gap > -EPS && std::fabs(qy - py) < EPS)) { ahead = true; break; }
				const float d = std::fabs(qx - px) + std::fabs(qy - py);
				if (d < bestD) { bestD = d; best = qq.first; bestPin = qq.second; bestGap = gap; }
			}
			if (ahead || best < 0 || bestD > SEAT_REACH) continue;
			// Two such parts wired to each other: the one that is fed moves.
			int other = -1;
			if (p.output && movable[best] && wires(best, other) == 1) continue;
			const LayoutPin &q = in.nodes[best].pins[bestPin];
			const float sx = -p.dx * snapUp(LEAD - bestGap);
			const float sy = snap((q.y + oy[best]) - py);
			bool placed = false;
			for (int cost = 0; cost <= SEAT_ROWS + 2 * SEAT_COLS && !placed; cost++)
				for (int col = 0; col <= SEAT_COLS && 2 * col <= cost && !placed; col++) {
					const int row = cost - 2 * col;
					if (row > SEAT_ROWS) continue;
					for (int sgn = 1; sgn >= (row == 0 ? 1 : -1) && !placed; sgn -= 2) {
						const float dx = sx - p.dx * col * in.step, dy = sy + sgn * row * in.step;
						const float l = L(v) + dx, r = R(v) + dx, b = B(v) + dy, t = T(v) + dy;
						bool free = true;
						for (size_t j = 0; j < n && free; j++) {
							if ((int)j == v) continue;
							if (l < R((int)j) - EPS && L((int)j) < r - EPS && b < T((int)j) - EPS && B((int)j) < t - EPS) free = false;
						}
						if (!free) continue;
						ox[v] += dx; oy[v] += dy;
						placed = any = true;
					}
				}
		}
		return any;
	}


	// True when `a` drives a pin of `b` that sits left of (or too close to the
	// right of) the driving pin: `need` is how far `b` has to go right for the
	// wire to have LEAD between the two pins. Parts drawn turned round
	// (outputs on the left) the same way, leftwards: `need` is then negative.
	bool backwardLink(int a, int b, int &src, int &dst, float &need) const {
		for (const LayoutPin &pa : in.nodes[a].pins) {
			if (!pa.output || pa.dx == 0 || pa.net < 0) continue;
			for (const LayoutPin &pb : in.nodes[b].pins) {
				if (pb.output || pb.dx != -pa.dx || pb.net != pa.net) continue;
				const float gap = ((pb.x + ox[b]) - (pa.x + ox[a])) * pa.dx;
				if (gap < LEAD - EPS) { src = a; dst = b; need = (LEAD - gap) * pa.dx; return true; }
			}
		}
		return false;
	}

	// The move up or down that lines `dst`'s inputs up with the pins of `src`
	// driving them (the middle one of their offsets) when it takes two wires or
	// more from it.
	float levelWith(int src, int dst) const {
		std::vector<float> d;
		for (const LayoutPin &pa : in.nodes[src].pins) {
			if (!pa.output || pa.dx == 0 || pa.net < 0) continue;
			for (const LayoutPin &pb : in.nodes[dst].pins)
				if (!pb.output && pb.dx == -pa.dx && pb.net == pa.net) d.push_back((pa.y + oy[src]) - (pb.y + oy[dst]));
		}
		// One wire (a label beside its pin) only goes sideways: it stays in its
		// column of labels.
		if (d.size() < 2) return 0.0f;
		std::sort(d.begin(), d.end());
		return snap(d[d.size() / 2]);
	}

	// Would `v` moved by (dx, dy) overlap a part it doesn't overlap now?
	bool newHit(int v, float dx, float dy) const {
		for (size_t k = 0; k < n; k++) {
			if ((int)k == v) continue;
			const int j = (int)k;
			const bool now = L(v) < R(j) - EPS && L(j) < R(v) - EPS && B(v) < T(j) - EPS && B(j) < T(v) - EPS;
			const bool then = L(v) + dx < R(j) - EPS && L(j) < R(v) + dx - EPS && B(v) + dy < T(j) - EPS && B(j) < T(v) + dy - EPS;
			if (then && !now) return true;
		}
		return false;
	}

	// A wired pin of `p` (moved by pdx, pdy) facing straight into box
	// l..r, b..t from closer than PIN_CLEAR: no room for its wire.
	int pinFaces(int p, float pdx, float pdy, float l, float b, float r, float t, float clear = PIN_CLEAR) const {
		int count = 0;
		for (const LayoutPin &q : in.nodes[p].pins) {
			if (q.net < 0) continue;
			const float x = q.x + ox[p] + pdx, y = q.y + oy[p] + pdy;
			if (q.dy != 0) {
				if (x <= l - PIN_MARGIN || x >= r + PIN_MARGIN) continue;
				const float d = q.dy > 0 ? b - y : y - t;
				if (d > -EPS && d < clear - EPS) count++;
			} else if (q.dx != 0) {
				if (y <= b - PIN_MARGIN || y >= t + PIN_MARGIN) continue;
				const float d = q.dx > 0 ? l - x : x - r;
				if (d > -EPS && d < clear - EPS) count++;
			}
		}
		return count;
	}

	// All of a part's wired pins drive, or all are driven.
	bool oneWay(int v) const {
		bool o = false, i = false;
		for (const LayoutPin &q : in.nodes[v].pins) if (q.net >= 0) (q.output ? o : i) = true;
		return !(o && i);
	}

	// With j moved down by `dy`, how many pins of i and j face the other part?
	int facingPins(int i, int j, float dy) const {
		return pinFaces(j, 0.0f, dy, L(i), B(i), R(i), T(i)) + pinFaces(i, 0.0f, 0.0f, L(j), B(j) + dy, R(j), T(j) + dy);
	}

	// Would every wire of `v`, moved by (dx, dy), leave both its ends forwards
	// -- each pin ahead of the other, by PIN_CLEAR, along the way that one
	// faces? Not so, the wire has to go out and turn back round one of them.
	bool wiresFace(int v, float dx, float dy) const {
		for (const LayoutPin &p : in.nodes[v].pins) {
			if (p.net < 0) continue;
			const float px = p.x + ox[v] + dx, py = p.y + oy[v] + dy;
			for (const auto &qq : netPins.at(p.net)) {
				if (qq.first == v) continue;
				const LayoutPin &q = in.nodes[qq.first].pins[qq.second];
				const float qx = q.x + ox[qq.first], qy = q.y + oy[qq.first];
				if ((qx - px) * p.dx + (qy - py) * p.dy < PIN_CLEAR - EPS && (p.dx || p.dy)) return false;
				if ((px - qx) * q.dx + (py - qy) * q.dy < PIN_CLEAR - EPS && (q.dx || q.dy)) return false;
			}
		}
		return true;
	}

	// The nearest spot (within RESETTLE_REACH) where `v` overlaps nothing, no
	// pin of its faces a part, and it covers no part's pin; false if none.
	// One where its wires also leave both ends forwards first (a keypad
	// dropped on the adder it feeds goes back above it, not beside it with
	// its wires coming in under the pins they feed), if there is one.
	bool resettle(int v) { return resettle(v, true) || resettle(v, false); }
	bool resettle(int v, bool facing) {
		const int reach = (int)std::lround(RESETTLE_REACH / in.step);
		for (int cost = 1; cost <= 2 * reach; cost++)
			for (int ax = 0; ax <= cost && ax <= reach; ax++) {
				const int ay = cost - ax;
				if (ay > reach) continue;
				for (int sx = 1; sx >= (ax == 0 ? 1 : -1); sx -= 2)
					for (int sy = 1; sy >= (ay == 0 ? 1 : -1); sy -= 2) {
						const float dx = sx * ax * in.step, dy = sy * ay * in.step;
						const float l = L(v) + dx, r = R(v) + dx, b = B(v) + dy, t = T(v) + dy;
						bool ok = true;
						for (size_t k = 0; k < n && ok; k++) {
							const int j = (int)k;
							if (j == v) continue;
							if (l < R(j) - EPS && L(j) < r - EPS && b < T(j) - EPS && B(j) < t - EPS) ok = false;
							else if (pinFaces(j, 0.0f, 0.0f, l, b, r, t, RESETTLE_CLEAR) || pinFaces(v, dx, dy, L(j), B(j), R(j), T(j), RESETTLE_CLEAR)) ok = false;
						}
						if (!ok || (facing && !wiresFace(v, dx, dy))) continue;
						ox[v] += dx; oy[v] += dy;
						return true;
					}
			}
		return false;
	}

	// The move (within ALIGN_REACH, in grid steps) that lines up the most of
	// this gate's pins with the pins they're wired to across the way; 0 unless
	// something beats staying put.
	float bestShift(int j, const std::set<int> &sameCol) const {
		std::map<int, std::set<int>> votes;   // shift in steps -> pins it straightens
		const LayoutNode &node = in.nodes[j];
		for (size_t pi = 0; pi < node.pins.size(); pi++) {
			const LayoutPin &p = node.pins[pi];
			if (p.dx == 0 || p.net < 0) continue;
			auto it = netPins.find(p.net);
			if (it == netPins.end()) continue;
			const float px = p.x + ox[j], py = p.y + oy[j];
			for (const auto &qq : it->second) {
				if (qq.first == j || sameCol.count(qq.first)) continue;
				const LayoutPin &q = in.nodes[qq.first].pins[qq.second];
				if (q.dx == 0 || q.dx == p.dx) continue;           // must face each other
				const float qx = q.x + ox[qq.first], qy = q.y + oy[qq.first];
				if ((p.dx > 0 && qx <= px) || (p.dx < 0 && qx >= px)) continue;
				const float s = qy - py;
				if (std::fabs(s) > ALIGN_REACH + EPS) continue;
				votes[(int)std::lround(s / in.step)].insert((int)pi);
			}
		}
		int bestKey = 0;
		size_t bestVotes = votes.count(0) ? votes[0].size() : 0;
		for (const auto &v : votes) {
			if (v.second.size() > bestVotes ||
			    (v.second.size() == bestVotes && std::abs(v.first) < std::abs(bestKey))) {
				bestVotes = v.second.size(); bestKey = v.first;
			}
		}
		return bestKey * in.step;
	}

	// ------------------------------------------------------------------ Rearrange
	//
	// A circuit is laid out as a tree of groups. The groups are the ones the
	// user drew: a clear gap across the whole circuit with few wires over it
	// (the three slices of a multi-digit adder, say) splits it, and each side
	// is split again the other way until nothing splits. Each leaf group is
	// laid out in layers along its own flow -- left to right, or top to
	// bottom when its wires mostly leave gates by the top and bottom -- and
	// the groups go back together in the order they were drawn, side by
	// side with room for the wires between them. So an already tidy circuit
	// keeps its shape, and repeated structure stays repeated.

	void rearrange() {
		// Gates wired to at least one other movable gate take part; the rest
		// (titles, lone parts) stay where they are.
		std::vector<int> C;
		std::vector<char> inC(n, 0);
		for (size_t i = 0; i < n; i++) {
			if (!movable[i]) continue;
			bool linked = false;
			for (const LayoutPin &p : in.nodes[i].pins) {
				if (p.net < 0) continue;
				for (const auto &qq : netPins[p.net])
					if (qq.first != (int)i && movable[qq.first]) linked = true;
			}
			if (linked) { C.push_back((int)i); inC[i] = 1; }
		}
		if (C.size() < 2) return;

		// Captions go with the part they were drawn beside, and that part
		// keeps room for them: whole grid steps, so parts placed on the grid
		// edge to edge still clear each other's captions.
		std::vector<int> anchor = attachCaptions(inC);
		for (size_t c = 0; c < n; c++) {
			const int a = anchor[c];
			if (a < 0) continue;
			const LayoutNode &an = in.nodes[a], &cn = in.nodes[c];
			el[a] = std::min(el[a], an.l - snapUp(std::max(0.0f, an.l - cn.l)));
			eb[a] = std::min(eb[a], an.b - snapUp(std::max(0.0f, an.b - cn.b)));
			er[a] = std::max(er[a], an.r + snapUp(std::max(0.0f, cn.r - an.r)));
			et[a] = std::max(et[a], an.t + snapUp(std::max(0.0f, cn.t - an.t)));
		}

		// Separate circuits (nothing wired between them) are laid out one at a
		// time, each where it was.
		std::vector<int> parent(n);
		for (size_t i = 0; i < n; i++) parent[i] = (int)i;
		std::function<int(int)> find = [&](int x) { return parent[x] == x ? x : parent[x] = find(parent[x]); };
		for (const auto &np : netPins) {
			int first = -1;
			for (const auto &pp : np.second) {
				if (!inC[pp.first]) continue;
				if (first < 0) first = pp.first; else parent[find(pp.first)] = find(first);
			}
		}
		std::map<int, std::vector<int>> comps;
		for (int v : C) comps[find(v)].push_back(v);
		std::vector<float> l0(n), b0(n), r0(n), t0(n);
		for (size_t i = 0; i < n; i++) { l0[i] = L((int)i); b0[i] = B((int)i); r0[i] = R((int)i); t0[i] = T((int)i); }
		for (auto &cp : comps) {
			// Where the circuit was: same top-left corner afterwards.
			float cl = FLT_MAX, ct = -FLT_MAX;
			for (int v : cp.second) { cl = std::min(cl, L(v)); ct = std::max(ct, T(v)); }
			const Box box = layoutGroup(cp.second);
			const float dx = snap(cl - box.l), dy = snap(ct - box.t);
			for (int v : cp.second) { ox[v] += dx; oy[v] += dy; }
		}

		// Blocks that now run into each other (a circuit grew) move out of
		// the way, top block first: one drawn beside another moves right,
		// otherwise down; lone movable gates count as blocks of one.
		struct Block { std::vector<int> nodes; float l, b, r, t, l0, r0, top0; };
		std::vector<Block> blocks;
		auto blockOf = [&](const std::vector<int> &nodes) {
			Block bk; bk.nodes = nodes;
			bk.l = FLT_MAX; bk.b = FLT_MAX; bk.r = -FLT_MAX; bk.t = -FLT_MAX;
			bk.l0 = FLT_MAX; bk.r0 = -FLT_MAX; bk.top0 = -FLT_MAX;
			for (int v : nodes) {
				bk.l = std::min(bk.l, L(v)); bk.r = std::max(bk.r, R(v));
				bk.b = std::min(bk.b, B(v)); bk.t = std::max(bk.t, T(v));
				bk.l0 = std::min(bk.l0, l0[v]); bk.r0 = std::max(bk.r0, r0[v]);
				bk.top0 = std::max(bk.top0, t0[v]);
			}
			return bk;
		};
		for (auto &cp : comps) blocks.push_back(blockOf(cp.second));
		for (size_t i = 0; i < n; i++) if (movable[i] && !inC[i] && anchor[i] < 0) blocks.push_back(blockOf({ (int)i }));
		std::sort(blocks.begin(), blocks.end(), [](const Block &a, const Block &b) { return a.top0 > b.top0; });
		for (size_t k = 1; k < blocks.size(); k++) {
			// Each move clears at least one block for good (moves only go right
			// or down), so k + 1 passes always settle it: a column of labels
			// the circuit grew over steps down past the ones already moved, one
			// a pass -- a fixed cap of 16 left the rest of a long list piled on
			// one line.
			for (size_t pass = 0; pass <= k; pass++) {
				float drop = 0.0f, shove = 0.0f;
				for (size_t j = 0; j < k; j++) {
					const Block &a = blocks[j], &b = blocks[k];
					if (!(a.l < b.r + BLOCK_GAP && b.l < a.r + BLOCK_GAP && a.b < b.t + BLOCK_GAP && b.b < a.t + BLOCK_GAP)) continue;
					if (b.l0 >= a.r0 - EPS) shove = std::max(shove, a.r + BLOCK_GAP - b.l);
					else drop = std::max(drop, b.t - (a.b - BLOCK_GAP));
				}
				if (shove <= EPS && drop <= EPS) break;
				const float dx = snapUp(shove), dy = snapUp(drop);
				for (int v : blocks[k].nodes) { ox[v] += dx; oy[v] -= dy; }
				blocks[k].l += dx; blocks[k].r += dx; blocks[k].b -= dy; blocks[k].t -= dy;
			}
		}
		for (size_t c = 0; c < n; c++)
			if (anchor[c] >= 0) { ox[c] = ox[anchor[c]]; oy[c] = oy[anchor[c]]; }
	}

	// Rearrange: for each part, the part it is a caption of, or -1. A caption
	// is a movable part with no wires (text, mostly) drawn within
	// CAPTION_REACH of a wired part; it belongs to the nearest wired part,
	// edge to edge, when that part is one being laid out. One drawn by a part
	// that stays (or by nothing) is left alone, as are big ones (a title).
	std::vector<int> attachCaptions(const std::vector<char> &inC) const {
		std::vector<int> anchor(n, -1);
		std::vector<char> wired(n, 0);
		for (const auto &np : netPins) {
			bool two = false;
			for (const auto &pp : np.second) if (pp.first != np.second[0].first) two = true;
			if (two) for (const auto &pp : np.second) wired[pp.first] = 1;
		}
		for (size_t c = 0; c < n; c++) {
			if (!movable[c] || inC[c] || wired[c]) continue;
			const LayoutNode &cn = in.nodes[c];
			if (cn.t - cn.b > CAPTION_MAX_H + EPS || cn.r - cn.l > CAPTION_MAX_W + EPS) continue;
			int best = -1;
			float bestD = FLT_MAX;
			for (size_t j = 0; j < n; j++) {
				if (!wired[j]) continue;
				const LayoutNode &a = in.nodes[j];
				const float gx = std::max(0.0f, std::max(a.l - cn.r, cn.l - a.r));
				const float gy = std::max(0.0f, std::max(a.b - cn.t, cn.b - a.t));
				const float d = std::sqrt(gx * gx + gy * gy);
				if (d < bestD - EPS) { bestD = d; best = (int)j; }
			}
			if (best >= 0 && bestD <= CAPTION_REACH + EPS && inC[best]) anchor[c] = best;
		}
		return anchor;
	}

	struct Box { float l, b, r, t; };

	Box boxOf(const std::vector<int> &S) const {
		Box b = { FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX };
		for (int v : S) { b.l = std::min(b.l, L(v)); b.b = std::min(b.b, B(v)); b.r = std::max(b.r, R(v)); b.t = std::max(b.t, T(v)); }
		return b;
	}

	// Nets with a pin in both sets.
	int crossingNets(const std::vector<int> &A, const std::vector<int> &Bs) const {
		std::vector<char> inA(n, 0), inB(n, 0);
		for (int v : A) inA[v] = 1;
		for (int v : Bs) inB[v] = 1;
		int c = 0;
		for (const auto &np : netPins) {
			bool a = false, b = false;
			for (const auto &pp : np.second) { if (inA[pp.first]) a = true; if (inB[pp.first]) b = true; }
			if (a && b) c++;
		}
		return c;
	}

	// Nets wired between two or more of these gates.
	int innerNets(const std::vector<int> &S) const {
		std::vector<char> inS(n, 0);
		for (int v : S) inS[v] = 1;
		int c = 0;
		for (const auto &np : netPins) {
			int k = 0;
			for (const auto &pp : np.second) if (inS[pp.first]) k++;
			if (k >= 2) c++;
		}
		return c;
	}

	// Lays the group out, moving its gates, and returns where it ended up.
	Box layoutGroup(const std::vector<int> &S) {
		const Box drawn = boxOf(S);
		for (int axis = 0; axis < 2 && S.size() >= 4; axis++) {
			const bool alongX = axis == 0;
			std::vector<std::vector<int>> pieces = splitAlong(S, alongX);
			if (pieces.size() < 2) continue;
			std::vector<Box> boxes;
			for (const auto &piece : pieces) boxes.push_back(layoutGroup(piece));
			if (alongX) {
				// Groups drawn side by side that come out long and thin (each
				// grew wider than it was drawn) go in rows instead, top to
				// bottom in drawn order, so the whole is about as wide for its
				// height as it was drawn.
				const std::vector<size_t> breaks = rowBreaks(boxes, drawn);
				if (breaks.size() > 1) {
					std::vector<std::vector<int>> rows;
					std::vector<Box> rowBoxes;
					for (size_t r = 0; r < breaks.size(); r++) {
						const size_t e = r + 1 < breaks.size() ? breaks[r + 1] : pieces.size();
						std::vector<std::vector<int>> rp(pieces.begin() + breaks[r], pieces.begin() + e);
						std::vector<Box> rb(boxes.begin() + breaks[r], boxes.begin() + e);
						rowBoxes.push_back(join(rp, rb, true));
						rows.push_back({});
						for (const auto &pc : rp) rows.back().insert(rows.back().end(), pc.begin(), pc.end());
					}
					return join(rows, rowBoxes, false, true);
				}
			}
			return join(pieces, boxes, alongX);
		}
		layoutLeaf(S);
		return boxOf(S);
	}

	// Where each row of groups starts, for groups laid out side by side in
	// one row: one row unless that is over WIDE times as wide as it is tall
	// and grew GREW times wider for its height than it was drawn; else
	// the number of rows that best matches the drawn shape (at least square),
	// each row taking the next groups in order.
	std::vector<size_t> rowBreaks(const std::vector<Box> &boxes, const Box &drawn) const {
		const size_t m = boxes.size();
		auto shape = [&](const std::vector<size_t> &br, float &w, float &h) {
			w = 0.0f; h = 0.0f;
			for (size_t r = 0; r < br.size(); r++) {
				const size_t e = r + 1 < br.size() ? br[r + 1] : m;
				float rw = 0.0f, rh = 0.0f;
				for (size_t k = br[r]; k < e; k++) {
					rw += boxes[k].r - boxes[k].l + (k > br[r] ? GROUP_GAP : 0.0f);
					rh = std::max(rh, boxes[k].t - boxes[k].b);
				}
				w = std::max(w, rw); h += rh + (r ? GROUP_GAP : 0.0f);
			}
		};
		std::vector<size_t> best = { 0 };
		float w, h;
		shape(best, w, h);
		const float dw = drawn.r - drawn.l, dh = drawn.t - drawn.b;
		const float drawnShape = dh > EPS ? dw / dh : FLT_MAX;
		if (h <= EPS || w / h <= std::max(WIDE, GREW * drawnShape)) return best;
		const float target = std::max(1.0f, drawnShape);
		float bestErr = std::fabs(std::log(w / h / target));
		float total = 0.0f;
		for (const Box &bx : boxes) total += bx.r - bx.l;
		for (size_t k = 2; k <= m; k++) {
			// Fill each row up to an even share of the total width.
			std::vector<size_t> br = { 0 };
			float rw = 0.0f;
			for (size_t i = 0; i < m; i++) {
				const float bw = boxes[i].r - boxes[i].l;
				if (i > 0 && br.size() < k && (rw + bw / 2.0f > total / k || m - i == k - br.size())) { br.push_back(i); rw = 0.0f; }
				rw += bw;
			}
			if (br.size() != k) continue;
			shape(br, w, h);
			const float err = std::fabs(std::log(w / h / target));
			if (err < bestErr - EPS) { bestErr = err; best = br; }
		}
		return best;
	}

	// Puts laid-out pieces back together in order, side by side (alongX) or
	// one under another, a lane of room per wire between, leveling the wires
	// between neighbors when that costs little. Rows of groups (stacked only
	// because side by side was too wide) go under each other with their left
	// edges lined up and the usual gap between groups.
	Box join(const std::vector<std::vector<int>> &pieces, std::vector<Box> boxes, bool alongX, bool rows = false) {
		std::vector<int> placed = pieces[0];
		Box all = boxes[0];
		for (size_t k = 1; k < pieces.size(); k++) {
			const float gap = rows ? GROUP_GAP : std::min(GROUP_GAP + (float)crossingNets(placed, pieces[k]), GROUP_GAP_MAX);
			float dx, dy;
			const float minSpan = alongX ? std::min(all.t - all.b, boxes[k].t - boxes[k].b)
			                             : std::min(all.r - all.l, boxes[k].r - boxes[k].l);
			float lev;
			const bool leveled = level(placed, pieces[k], !alongX, lev) && std::fabs(lev) <= LEVEL_SHARE * minSpan;
			if (alongX) {
				dx = all.r + gap - boxes[k].l;
				dy = leveled ? lev : boxes[k - 1].t - boxes[k].t;
			} else {
				dy = all.b - gap - boxes[k].t;
				dx = (leveled && !rows) ? lev : boxes[k - 1].l - boxes[k].l;
			}
			dx = snap(dx); dy = snap(dy);
			for (int v : pieces[k]) { ox[v] += dx; oy[v] += dy; }
			boxes[k].l += dx; boxes[k].r += dx; boxes[k].b += dy; boxes[k].t += dy;
			all.l = std::min(all.l, boxes[k].l); all.r = std::max(all.r, boxes[k].r);
			all.b = std::min(all.b, boxes[k].b); all.t = std::max(all.t, boxes[k].t);
			placed.insert(placed.end(), pieces[k].begin(), pieces[k].end());
		}
		return all;
	}

	// The shift (x when inX, else y) that best levels the wires between the
	// two sets: the median over their pin pairs. False when nothing joins them.
	bool level(const std::vector<int> &A, const std::vector<int> &Bs, bool inX, float &out) const {
		std::vector<char> inA(n, 0), inB(n, 0);
		for (int v : A) inA[v] = 1;
		for (int v : Bs) inB[v] = 1;
		std::vector<float> d;
		for (const auto &np : netPins)
			for (const auto &pa : np.second) {
				if (!inA[pa.first]) continue;
				const LayoutPin &p = in.nodes[pa.first].pins[pa.second];
				for (const auto &pb : np.second) {
					if (!inB[pb.first]) continue;
					const LayoutPin &q = in.nodes[pb.first].pins[pb.second];
					d.push_back(inX ? (p.x + ox[pa.first]) - (q.x + ox[pb.first]) : (p.y + oy[pa.first]) - (q.y + oy[pb.first]));
				}
			}
		if (d.empty()) return false;
		std::sort(d.begin(), d.end());
		out = d[d.size() / 2];
		return true;
	}

	// Splits S at the gaps the user left along one axis: a gap of GROUP_GAP
	// or more that every gate clears, with at least two gates on each side
	// and few wires across it next to the wiring within each side. Pieces
	// come back in order along the axis; one piece means no split. The gaps
	// are between the parts themselves: a caption in one doesn't close it.
	std::vector<std::vector<int>> splitAlong(const std::vector<int> &S, bool alongX) const {
		std::vector<int> order = S;
		auto lo = [&](int v) { return alongX ? in.nodes[v].l + ox[v] : -(in.nodes[v].t + oy[v]); };
		auto hi = [&](int v) { return alongX ? in.nodes[v].r + ox[v] : -(in.nodes[v].b + oy[v]); };
		std::sort(order.begin(), order.end(), [&](int a, int b) { return lo(a) < lo(b); });
		std::vector<std::vector<int>> pieces;
		std::vector<int> cur;
		float reach = -FLT_MAX;
		for (size_t k = 0; k < order.size(); k++) {
			const int v = order[k];
			if (k > 0 && lo(v) >= reach + GROUP_GAP - EPS) {
				std::vector<int> before, after;
				for (size_t j = 0; j < k; j++) before.push_back(order[j]);
				for (size_t j = k; j < order.size(); j++) after.push_back(order[j]);
				if (before.size() >= 2 && after.size() >= 2) {
					const int limit = std::max(1, std::min(innerNets(before), innerNets(after)) / 4);
					if (crossingNets(before, after) <= limit) { pieces.push_back(cur); cur.clear(); }
				}
			}
			cur.push_back(v);
			reach = std::max(reach, hi(v));
		}
		pieces.push_back(cur);
		return pieces;
	}

	// Lays out one group in layers. A group whose wires mostly leave gates by
	// the top and bottom (adders stacked over each other) flows top to
	// bottom: it is laid out turned on its side and turned back.
	void layoutLeaf(const std::vector<int> &S) {
		int h = 0, v = 0;
		std::vector<char> inS(n, 0);
		for (int w : S) inS[w] = 1;
		for (const auto &np : netPins) {
			int k = 0;
			for (const auto &pp : np.second) if (inS[pp.first]) k++;
			if (k < 2) continue;
			for (const auto &pp : np.second) {
				if (!inS[pp.first]) continue;
				const LayoutPin &p = in.nodes[pp.first].pins[pp.second];
				if (p.dx != 0) h += k - 1; else if (p.dy != 0) v += k - 1;
			}
		}
		const bool down = v > h;
		std::vector<LayoutNode> local;
		std::vector<LeafLayout::Margin> margins;   // caption room round each part, in the group's own frame
		for (int w : S) {
			LayoutNode nd = in.nodes[w];
			nd.l = L(w); nd.r = R(w); nd.b = B(w); nd.t = T(w);
			const LayoutNode &body = in.nodes[w];
			LeafLayout::Margin m;
			if (down) { m.l = et[w] - body.t; m.r = body.b - eb[w]; m.b = body.l - el[w]; m.t = er[w] - body.r; }   // x' = -y, y' = x
			else { m.l = body.l - el[w]; m.r = er[w] - body.r; m.b = body.b - eb[w]; m.t = et[w] - body.t; }
			margins.push_back(m);
			for (LayoutPin &p : nd.pins) {
				p.x += ox[w]; p.y += oy[w];
				// Wires out of the group don't shape it.
				if (p.net >= 0) {
					bool inside = false;
					for (const auto &pp : netPins[p.net]) if (inS[pp.first] && pp.first != w) inside = true;
					if (!inside) p.net = -1;
				}
			}
			if (down) {
				// x' = -y, y' = x: down becomes right.
				const LayoutNode src = nd;
				nd.l = -src.t; nd.r = -src.b; nd.b = src.l; nd.t = src.r;
				for (LayoutPin &p : nd.pins) {
					const float x = p.x, y = p.y;
					const int dx = p.dx, dy = p.dy;
					p.x = -y; p.y = x; p.dx = -dy; p.dy = dx;
				}
			}
			local.push_back(nd);
		}
		LeafLayout leaf(std::move(local), in.step, std::move(margins));
		const std::vector<std::pair<float, float>> off = leaf.run();
		for (size_t i = 0; i < S.size(); i++) {
			if (down) { ox[S[i]] += off[i].second; oy[S[i]] -= off[i].first; }
			else { ox[S[i]] += off[i].first; oy[S[i]] += off[i].second; }
		}
	}
};

}  // namespace

std::vector<std::pair<float, float>> layoutGates(const LayoutInput &in) {
	Layouter l(in);
	return l.run();
}

}  // namespace route
}  // namespace cl
