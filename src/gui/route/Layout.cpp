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
const float BLOCK_GAP = 1.5f;        // Rearrange: room kept between separate circuits
const float PIN_OFFSET = 1.5f;        // Rearrange: how far an up/down pin sits off its wire
const int PICTURE_PARTS = 4;         // this many overlapping parts are a drawing
const float OVERLAP_MIN = 0.1f;      // overlap deeper than this counts
const float LIGHT_REACH = 1.5f;      // lights this close together belong to one display
const float LANE = 1.0f;             // Rearrange: room a passing wire keeps in a column
const float GROUP_GAP = 3.0f;        // Rearrange: a gap this wide between drawn groups splits them
const float GROUP_GAP_MAX = 10.0f;   // Rearrange: most room put back between groups
const float LEVEL_SHARE = 0.25f;     // Rearrange: groups level their wires only by this much of their size

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
	LeafLayout(std::vector<LayoutNode> nodes, float step) : N(std::move(nodes)), step(step), n(N.size()) {
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
	std::map<int, std::vector<std::pair<int, int>>> netPins;
	std::vector<std::set<int>> succ, pred;
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
	struct Edge { int a; float oa; int fa; int b; float ob; int fb; };
	std::vector<Edge> E;
	std::vector<std::vector<int>> inc;

	float snap(float v) const { return std::round(v / step) * step; }
	float snapUp(float v) const { return std::ceil(v / step - EPS) * step; }
	float W(int i) const { return N[i].r - N[i].l; }
	float H(int i) const { return N[i].t - N[i].b; }
	float CX(int i) const { return (N[i].l + N[i].r) / 2.0f; }
	float CY(int i) const { return (N[i].b + N[i].t) / 2.0f; }
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
	// broken at the edge that points back, walking left to right.
	void flow() {
		succ.assign(n, {}); pred.assign(n, {});
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
			for (int u : drivers) for (int v : sinks) if (u != v) { succ[u].insert(v); pred[v].insert(u); }
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
	// a source (a switch, ground) right before the first thing it feeds.
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
		for (size_t v = 0; v < n; v++) if (succ[v].empty() && !pred[v].empty()) layer[v] = maxL;
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
			std::sort(across.begin(), across.end(), [&](int a, int b) { return N[a].l < N[b].l; });
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
		for (auto &lay : layers) std::sort(lay.begin(), lay.end(), [&](int a, int b) { return y[a] > y[b]; });
		auto faces = [](const LayoutPin &p) { return p.dx != 0 ? 0 : p.dy > 0 ? 1 : p.dy < 0 ? -1 : 0; };

		// A wire that skips columns gets a placeholder in each column it
		// passes: a gate standing in its way counts as a crossing, and the
		// placeholder keeps a lane open for it.
		E.clear();
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
						y.push_back(y[u] + ou + (y[v] + ov - y[u] - ou) * f);
						cy0.push_back(y.back());
						h.push_back(0.0f);
						layers[l].push_back(d);
						E.push_back({ prev, po, pf, d, 0.0f, 0 });
						prev = d; po = 0.0f; pf = 0;
					}
					E.push_back({ prev, po, pf, v, ov, fv });
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
				std::sort(terms.begin(), terms.end(), [&](int a, int b) { return cy0[a] > cy0[b]; });
				for (size_t i = 0; i < terms.size(); i++) key[terms[i]] = slots[i];
			}
			std::stable_sort(lay.begin(), lay.end(), [&](int a, int b) {
				if (std::fabs(key[a] - key[b]) > EPS) return key[a] > key[b];
				if (terminal(a) && terminal(b)) return cy0[a] > cy0[b];
				return y[a] > y[b];   // a tie keeps the current order
			});
			stack(lay);
		};
		for (auto &lay : layers) stack(lay);
		for (int sweep = 0; sweep < 6; sweep++) {
			for (int l = 1; l <= maxL; l++) reorder(layers[l], -1);
			for (int l = maxL - 1; l >= 0; l--) reorder(layers[l], +1);
		}
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
		// every wire that has to cross the gap to the next.
		std::vector<float> colW(maxL + 1, 0.0f), colX(maxL + 1, 0.0f), gap(maxL + 1, 0.0f);
		for (size_t v = 0; v < n; v++) if (rep[v] == (int)v) colW[layer[v]] = std::max(colW[layer[v]], snapUp(stackW((int)v)));
		for (const auto &np : netPins) {
			int lo = maxL + 1, hi = -1;
			for (const auto &pp : np.second) { lo = std::min(lo, layer[pp.first]); hi = std::max(hi, layer[pp.first]); }
			for (int l = lo; l < hi; l++) gap[l] += 1.0f;
		}
		for (int l = 1; l <= maxL; l++) colX[l] = colX[l - 1] + colW[l - 1] + 1.5f + gap[l - 1];
		newL.assign(n, 0.0f);
		for (size_t v = 0; v < n; v++) {
			if (rep[v] != (int)v) continue;
			const int l = layer[v];
			const float w = stackW((int)v);
			float left;
			switch (alignOf(N[v])) {
			case LEFT:   left = colX[l]; break;
			case RIGHT:  left = colX[l] + colW[l] - w; break;
			default:     left = snap(colX[l] + (colW[l] - w) / 2.0f); break;
			}
			for (int m : members[v]) { newL[m] = left; left += W(m) + LAYER_GAP_Y; }
		}

		// Heights: each gate as level as it can get with what it's wired to,
		// each passing wire as straight, keeping the column's order and
		// spacing (pool-adjacent-violators).
		for (int round = 0; round < 4; round++) {
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
						for (int e : inc[v]) {
							const Edge &ed = E[e];
							const bool mine = ed.a == v;
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
		std::vector<double> sum;
		std::vector<int> cnt;
		for (size_t i = 0; i < m; i++) {
			sum.push_back(want[i] + S[i]); cnt.push_back(1);
			while (sum.size() >= 2 && sum[sum.size() - 2] / cnt[cnt.size() - 2] < sum.back() / cnt.back()) {
				sum[sum.size() - 2] += sum.back(); cnt[cnt.size() - 2] += cnt.back();
				sum.pop_back(); cnt.pop_back();
			}
		}
		size_t i = 0;
		for (size_t b = 0; b < sum.size(); b++)
			for (int k = 0; k < cnt[b]; k++, i++) y[lay[i]] = (float)(sum[b] / cnt[b]) - S[i];
	}
};

class Layouter {
public:
	explicit Layouter(const LayoutInput &in) : in(in), n(in.nodes.size()), ox(n, 0.0f), oy(n, 0.0f), movable(n, false) {
		for (size_t i = 0; i < n; i++) {
			movable[i] = in.nodes[i].movable;
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
	float L(int i) const { return in.nodes[i].l + ox[i]; }
	float R(int i) const { return in.nodes[i].r + ox[i]; }
	float B(int i) const { return in.nodes[i].b + oy[i]; }
	float T(int i) const { return in.nodes[i].t + oy[i]; }
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
			for (size_t k = 0; k < col.size(); k++)
				if (std::fabs(target - edge[k]) <= NUDGE + EPS) ox[col[k]] += snap(target - edge[k]);
		}

		// Level each gate with what it's wired to, drivers first, so wires that
		// miss by a step or three run straight.
		std::vector<int> byX = mv;
		std::sort(byX.begin(), byX.end(), [&](int a, int b) { return CX(a) < CX(b); });
		for (int j : byX) {
			const std::vector<int> &mine = stacks[find(j)];
			oy[j] += bestShift(j, std::set<int>(mine.begin(), mine.end()));
		}

		// No two gates overlap afterwards: push the lower one down.
		for (int pass = 0; pass < 8; pass++) {
			bool moved = false;
			std::vector<int> byTop = mv;
			std::sort(byTop.begin(), byTop.end(), [&](int a, int b) { return T(a) > T(b); });
			for (size_t a = 0; a < byTop.size(); a++)
				for (size_t c = a + 1; c < byTop.size(); c++) {
					const int i = byTop[a], j = byTop[c];
					// Real overlaps only: parts drawn edge to edge (a stack of
					// input labels) are meant to touch.
					const bool hits = L(i) < R(j) - EPS && L(j) < R(i) - EPS && B(i) < T(j) - EPS && B(j) < T(i) - EPS;
					if (hits) { oy[j] -= snapUp(T(j) - B(i)); moved = true; }
				}
			if (!moved) break;
		}
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
		for (size_t i = 0; i < n; i++) if (movable[i] && !inC[i]) blocks.push_back(blockOf({ (int)i }));
		std::sort(blocks.begin(), blocks.end(), [](const Block &a, const Block &b) { return a.top0 > b.top0; });
		for (size_t k = 1; k < blocks.size(); k++) {
			for (int pass = 0; pass < 16; pass++) {
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
		for (int axis = 0; axis < 2 && S.size() >= 4; axis++) {
			const bool alongX = axis == 0;
			std::vector<std::vector<int>> pieces = splitAlong(S, alongX);
			if (pieces.size() < 2) continue;
			std::vector<Box> boxes;
			for (const auto &piece : pieces) boxes.push_back(layoutGroup(piece));
			// Back together in drawn order, a lane of room per wire between.
			std::vector<int> placed = pieces[0];
			Box all = boxes[0];
			for (size_t k = 1; k < pieces.size(); k++) {
				const float gap = std::min(GROUP_GAP + (float)crossingNets(placed, pieces[k]), GROUP_GAP_MAX);
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
					dx = leveled ? lev : boxes[k - 1].l - boxes[k].l;
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
		layoutLeaf(S);
		return boxOf(S);
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
	// come back in order along the axis; one piece means no split.
	std::vector<std::vector<int>> splitAlong(const std::vector<int> &S, bool alongX) const {
		std::vector<int> order = S;
		auto lo = [&](int v) { return alongX ? L(v) : -T(v); };
		auto hi = [&](int v) { return alongX ? R(v) : -B(v); };
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
		for (int w : S) {
			LayoutNode nd = in.nodes[w];
			nd.l = L(w); nd.r = R(w); nd.b = B(w); nd.t = T(w);
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
		LeafLayout leaf(std::move(local), in.step);
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
