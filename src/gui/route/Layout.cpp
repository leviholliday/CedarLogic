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
	// Room between two gates stacked in a Rearrange column: one-pin parts wired
	// from the side (input and output labels) sit edge to edge, as people draw them, so their wires
	// can meet a gate's closely spaced inputs straight on.
	float stackGap(int a, int b) const {
		auto terminal = [&](int v) {
			const std::vector<LayoutPin> &ps = in.nodes[v].pins;
			return ps.size() == 1 && ps[0].dx != 0;
		};
		return (terminal(a) && terminal(b)) ? 0.0f : LAYER_GAP_Y;
	}
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
			std::vector<char> inComp(n, 0);
			for (int v : cp.second) inComp[v] = 1;
			layoutComponent(cp.second, inComp);
		}

		// Blocks that now run into each other (a circuit grew taller) move down,
		// top block first; lone movable gates count as blocks of one.
		struct Block { std::vector<int> nodes; float l, b, r, t, top0; };
		std::vector<Block> blocks;
		auto blockOf = [&](const std::vector<int> &nodes) {
			Block bk; bk.nodes = nodes;
			bk.l = FLT_MAX; bk.b = FLT_MAX; bk.r = -FLT_MAX; bk.t = -FLT_MAX; bk.top0 = -FLT_MAX;
			for (int v : nodes) {
				bk.l = std::min(bk.l, L(v)); bk.r = std::max(bk.r, R(v));
				bk.b = std::min(bk.b, B(v)); bk.t = std::max(bk.t, T(v));
				bk.top0 = std::max(bk.top0, t0[v]);
			}
			return bk;
		};
		for (auto &cp : comps) blocks.push_back(blockOf(cp.second));
		for (size_t i = 0; i < n; i++) if (movable[i] && !inC[i]) blocks.push_back(blockOf({ (int)i }));
		std::sort(blocks.begin(), blocks.end(), [](const Block &a, const Block &b) { return a.top0 > b.top0; });
		for (size_t k = 1; k < blocks.size(); k++) {
			float drop = 0.0f;
			for (size_t j = 0; j < k; j++) {
				const Block &a = blocks[j], &b = blocks[k];
				if (a.l < b.r + BLOCK_GAP && b.l < a.r + BLOCK_GAP && a.b < b.t - drop + BLOCK_GAP && b.b - drop < a.t + BLOCK_GAP)
					drop = std::max(drop, b.t - (a.b - BLOCK_GAP));
			}
			if (drop > EPS) {
				const float d = snapUp(drop);
				for (int v : blocks[k].nodes) oy[v] -= d;
				blocks[k].b -= d; blocks[k].t -= d;
			}
		}
	}

	void layoutComponent(const std::vector<int> &C, const std::vector<char> &inC) {
		// Signal flow: each driver points at what it drives.
		std::vector<std::set<int>> succ(n), pred(n);
		for (const auto &np : netPins) {
			std::set<int> drivers, sinks;
			for (const auto &pp : np.second) {
				if (!inC[pp.first]) continue;
				(in.nodes[pp.first].pins[pp.second].output ? drivers : sinks).insert(pp.first);
			}
			if (drivers.size() > 1) {
				// Several pins claim to drive (some parts mark every pin as an
				// output): believe the ones that face right, as outputs do.
				std::set<int> right;
				for (const auto &pp : np.second)
					if (inC[pp.first] && in.nodes[pp.first].pins[pp.second].output && in.nodes[pp.first].pins[pp.second].dx > 0)
						right.insert(pp.first);
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
		// Break feedback loops (latches, counters wired back) at the edge that
		// points back to something already on the path, walking left to right.
		{
			std::vector<int> order = C;
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

		// Columns: the longest path from an input; outputs all in the last one.
		std::vector<int> layer(n, 0);
		{
			std::vector<int> indeg(n, 0), queue;
			for (int v : C) { indeg[v] = (int)pred[v].size(); if (indeg[v] == 0) queue.push_back(v); }
			for (size_t qi = 0; qi < queue.size(); qi++) {
				const int u = queue[qi];
				for (int v : succ[u]) {
					layer[v] = std::max(layer[v], layer[u] + 1);
					if (--indeg[v] == 0) queue.push_back(v);
				}
			}
		}
		int maxL = 0;
		for (int v : C) maxL = std::max(maxL, layer[v]);
		for (int v : C) if (succ[v].empty() && !pred[v].empty()) layer[v] = maxL;
		std::vector<std::vector<int>> layers(maxL + 1);
		for (int v : C) layers[layer[v]].push_back(v);

		// Order within each column to cut crossings. A wire that skips columns
		// gets a placeholder in each column it passes, so a gate standing in its
		// way counts. Gates are sorted by the average height of what they're
		// wired to, sweeping right then left, then neighbors swap while that
		// uncrosses wires.
		std::vector<float> cy0(n, 0.0f);
		for (int v : C) cy0[v] = CY(v);
		std::vector<float> y(n, 0.0f), h(n, 0.0f);
		for (int v : C) { y[v] = CY(v); h[v] = H(v); }
		for (auto &lay : layers) std::sort(lay.begin(), lay.end(), [&](int a, int b) { return y[a] > y[b]; });
		struct Edge { int a; float oa; int b; float ob; };   // a in an earlier column than b
		std::vector<Edge> E;
		for (const auto &np : netPins)
			for (const auto &pa : np.second)
				for (const auto &pb : np.second) {
					const int u = pa.first, v = pb.first;
					if (u == v || !inC[u] || !inC[v] || !succ[u].count(v) || layer[u] >= layer[v]) continue;
					const float ou = in.nodes[u].pins[pa.second].y - cy0[u];
					const float ov = in.nodes[v].pins[pb.second].y - cy0[v];
					int prev = u; float po = ou;
					for (int l = layer[u] + 1; l < layer[v]; l++) {
						const float f = (float)(l - layer[u]) / (float)(layer[v] - layer[u]);
						const int d = (int)y.size();
						y.push_back(y[u] + ou + (y[v] + ov - y[u] - ou) * f);
						h.push_back(0.0f);
						layers[l].push_back(d);
						E.push_back({ prev, po, d, 0.0f });
						prev = d; po = 0.0f;
					}
					E.push_back({ prev, po, v, ov });
				}
		const size_t total = y.size();
		std::vector<std::vector<int>> inc(total);
		for (size_t e = 0; e < E.size(); e++) { inc[E[e].a].push_back((int)e); inc[E[e].b].push_back((int)e); }
		auto isDummy = [&](int v) { return v >= (int)n; };
		auto stack = [&](std::vector<int> &lay) {
			if (lay.empty()) return;
			float mean = 0.0f, span = 0.0f;
			for (size_t i = 0; i < lay.size(); i++) {
				mean += y[lay[i]]; span += h[lay[i]];
				if (i > 0) span += (isDummy(lay[i]) || isDummy(lay[i - 1])) ? 0.5f : stackGap(lay[i - 1], lay[i]);
			}
			mean /= lay.size();
			float top = mean + span / 2.0f;
			for (size_t i = 0; i < lay.size(); i++) {
				if (i > 0) top -= (isDummy(lay[i]) || isDummy(lay[i - 1])) ? 0.5f : stackGap(lay[i - 1], lay[i]);
				y[lay[i]] = top - h[lay[i]] / 2.0f;
				top -= h[lay[i]];
			}
		};
		// For v's wires on one side (-1 left, +1 right): where each far end is,
		// and v's own port offset.
		auto portsOn = [&](int v, int side, std::vector<std::pair<float, float>> &out) {
			out.clear();
			for (int e : inc[v]) {
				const Edge &ed = E[e];
				if (side < 0 && ed.b == v) out.push_back({ ed.ob, y[ed.a] + ed.oa });
				if (side > 0 && ed.a == v) out.push_back({ ed.oa, y[ed.b] + ed.ob });
			}
		};
		std::vector<std::pair<float, float>> tmp, tmp2;
		auto reorder = [&](std::vector<int> &lay, int side) {
			std::map<int, float> key;
			for (int v : lay) {
				portsOn(v, side, tmp);
				float sum = 0.0f;
				for (const auto &pt : tmp) sum += pt.second - pt.first;
				key[v] = tmp.empty() ? y[v] : sum / tmp.size();
			}
			std::stable_sort(lay.begin(), lay.end(), [&](int a, int b) {
				if (std::fabs(key[a] - key[b]) > EPS) return key[a] > key[b];
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
		// Placeholders have done their job.
		for (auto &lay : layers)
			lay.erase(std::remove_if(lay.begin(), lay.end(), [&](int v) { return isDummy(v); }), lay.end());
		y.resize(n);

		// Column x: each as wide as its widest gate, with a lane of room for
		// every wire that has to cross the gap to the next.
		std::vector<float> colW(maxL + 1, 0.0f), colX(maxL + 1, 0.0f), gap(maxL + 1, 0.0f);
		for (int v : C) colW[layer[v]] = std::max(colW[layer[v]], snapUp(W(v)));
		for (const auto &np : netPins) {
			int lo = maxL + 1, hi = -1;
			for (const auto &pp : np.second) if (inC[pp.first]) { lo = std::min(lo, layer[pp.first]); hi = std::max(hi, layer[pp.first]); }
			for (int l = lo; l < hi; l++) gap[l] += 1.0f;
		}
		for (int l = 1; l <= maxL; l++) colX[l] = colX[l - 1] + colW[l - 1] + 1.5f + gap[l - 1];
		std::vector<float> newL(n, 0.0f);
		for (int v : C) {
			const int l = layer[v];
			switch (alignOf(in.nodes[v])) {
			case LEFT:   newL[v] = colX[l]; break;
			case RIGHT:  newL[v] = colX[l] + colW[l] - W(v); break;
			default:     newL[v] = snap(colX[l] + (colW[l] - W(v)) / 2.0f); break;
			}
		}

		// Heights: each gate as level as it can get with what it's wired to,
		// keeping the column's order and spacing (pool-adjacent-violators).
		for (int round = 0; round < 4; round++) {
			for (int pass = 0; pass < 2; pass++) {
				for (int li = 0; li <= maxL; li++) {
					const int l = pass == 0 ? li : maxL - li;
					std::vector<int> &lay = layers[l];
					if (lay.empty()) continue;
					std::vector<float> want;
					for (int v : lay) {
						std::vector<float> cand;
						for (const LayoutPin &p : in.nodes[v].pins) {
							if (p.net < 0) continue;
							for (const auto &qq : netPins[p.net]) {
								if (!inC[qq.first] || layer[qq.first] == l) continue;
								const LayoutPin &q = in.nodes[qq.first].pins[qq.second];
								const float qy = q.y - cy0[qq.first] + y[qq.first];
								// Where this pin wants to be: level with a side pin; a
								// little above or below one that faces up or down
								// (an LED over its wire, a switch beside a top input).
								float py;
								if (p.dx != 0 && q.dx != 0) py = qy;
								else if (p.dx != 0 && q.dy != 0) py = qy + (q.dy > 0 ? PIN_OFFSET : -PIN_OFFSET);
								else if (p.dy != 0 && q.dx != 0) py = qy + (p.dy < 0 ? PIN_OFFSET : -PIN_OFFSET);
								else continue;
								cand.push_back(py - (p.y - cy0[v]));
							}
						}
						if (cand.empty()) { want.push_back(y[v]); continue; }
						std::sort(cand.begin(), cand.end());
						want.push_back(cand[cand.size() / 2]);
					}
					fitColumn(lay, want, y);
				}
			}
		}

		// Put the result where the circuit was: same top-left corner.
		float l0 = FLT_MAX, t0 = -FLT_MAX, l1 = FLT_MAX, t1 = -FLT_MAX;
		for (int v : C) {
			l0 = std::min(l0, L(v)); t0 = std::max(t0, T(v));
			l1 = std::min(l1, newL[v]); t1 = std::max(t1, y[v] + H(v) / 2.0f);
		}
		const float sx = l0 - l1, sy = t0 - t1;
		for (int v : C) {
			ox[v] = newL[v] + sx - in.nodes[v].l;
			oy[v] = snap(y[v] + sy - cy0[v]);
		}
	}

	// Closest heights to `want` that keep `lay`'s top-to-bottom order with
	// stackGap between neighbors: shift each target by the room above it so
	// the constraint becomes "never increasing", then pool adjacent violators.
	void fitColumn(const std::vector<int> &lay, const std::vector<float> &want, std::vector<float> &y) const {
		const size_t m = lay.size();
		std::vector<float> S(m, 0.0f);
		for (size_t i = 1; i < m; i++) S[i] = S[i - 1] + H(lay[i - 1]) / 2.0f + H(lay[i]) / 2.0f + stackGap(lay[i - 1], lay[i]);
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

}  // namespace

std::vector<std::pair<float, float>> layoutGates(const LayoutInput &in) {
	Layouter l(in);
	return l.run();
}

}  // namespace route
}  // namespace cl
