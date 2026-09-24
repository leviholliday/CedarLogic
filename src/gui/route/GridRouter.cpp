// GridRouter -- see include/gui/route/GridRouter.h for the approach.

#include "route/GridRouter.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <functional>
#include <queue>
#include <set>
#include <utility>

namespace cl {
namespace route {

namespace {

const float EPS = 1e-3f;

// Prices, in grid steps of wire length. Tuned for looks: a crossing is worth a
// long detour, a bend a short one, and lying on another wire's line (which
// reads as one wire) is a last resort.
const float BEND = 3.0f;
const float CROSS = 8.0f;
const float OVERLAP = 150.0f;       // same line as another wire
const float TOUCH = 150.0f;         // a corner or junction sitting on another wire
const float PARALLEL_NEAR = 3.0f;   // another wire one step to the side, same way
const float PARALLEL_FAR = 0.0f;    // two steps (one pin pitch) is normal spacing
const float HUG_GATE = 0.4f;        // right against a gate body
const float NEAR_PIN = 5.0f;        // brushing a pin that isn't ours
const float RELAXED_WALL = 60.0f;   // last-ditch: through a gate body
const float HISTORY_STEP = 2.0f;    // added to a contested node each pass
const float CENTER = 0.06f;         // tie-break: jogs halfway between the ends
const int PASSES = 6;
const size_t MAX_NODES = 1500000;
const size_t MAX_POPS = 3000000;

enum { PX = 0, NXD = 1, PY = 2, NYD = 3, NONE = 4 };

bool horizontalDir(int d) { return d == PX || d == NXD; }
int reverseDir(int d) {
	switch (d) { case PX: return NXD; case NXD: return PX; case PY: return NYD; case NYD: return PY; }
	return NONE;
}

// Grid lines: every multiple of `step` across [lo, hi], plus the exact
// coordinates in `exact` (pins, fixed wires), which win over a grid value
// within EPS so a pin's wire lands on the pin exactly.
std::vector<float> makeLines(float lo, float hi, float step, const std::vector<float> &exact) {
	std::vector<std::pair<float, int>> v;   // (value, 1 when exact)
	const long a = (long)std::floor(lo / step), b = (long)std::ceil(hi / step);
	for (long i = a; i <= b; i++) v.push_back({ (float)(i * step), 0 });
	for (float e : exact) if (e >= lo - EPS && e <= hi + EPS) v.push_back({ e, 1 });
	std::sort(v.begin(), v.end());
	std::vector<float> out;
	std::vector<int> isExact;
	for (const auto &p : v) {
		if (!out.empty() && std::fabs(p.first - out.back()) < EPS) {
			if (p.second && !isExact.back()) { out.back() = p.first; isExact.back() = 1; }
			continue;
		}
		out.push_back(p.first);
		isExact.push_back(p.second);
	}
	return out;
}

int findLine(const std::vector<float> &lines, float v) {
	auto it = std::lower_bound(lines.begin(), lines.end(), v - EPS);
	if (it == lines.end() || std::fabs(*it - v) >= EPS) return -1;
	return (int)(it - lines.begin());
}

int nearestLine(const std::vector<float> &lines, float v) {
	auto it = std::lower_bound(lines.begin(), lines.end(), v);
	if (it == lines.end()) return lines.empty() ? -1 : (int)lines.size() - 1;
	int i = (int)(it - lines.begin());
	if (i > 0 && std::fabs(lines[i - 1] - v) < std::fabs(lines[i] - v)) i--;
	return i;
}

class Router {
public:
	explicit Router(const GridInput &in) : in(in) {}
	GridOutput run();

private:
	struct PinInfo {
		int node = -1;
		int stub = -1;
		int dir = NONE;
		std::vector<int> walk;   // pin .. stub, the forced way out of the gate
	};
	struct NetState {
		std::set<long long> edges;
		std::vector<int> nodesH, nodesV;
		float cost = 0.0f;
		bool ok = false;
	};

	const GridInput &in;
	std::vector<float> xs, ys;
	int nx = 0, ny = 0, N = 0;
	std::vector<uint8_t> blocked, channel, hug;
	std::vector<int> owner;     // -1 free, net index, or -3 closed to every net
	std::vector<int> nearPin;   // -1 none, owning net, or -2 foreign
	std::vector<int> useH, useV, fixedH, fixedV;
	std::vector<float> hist;
	std::vector<std::vector<PinInfo>> pins;
	std::vector<NetState> nets;

	// Current net's tree.
	std::vector<uint8_t> inTree, treeAxis;
	std::vector<int> treeNodes;
	float tx0 = 0, tx1 = 0, ty0 = 0, ty1 = 0;
	float cx = 0, cy = 0;   // middle of the connection being searched

	// Search scratch, reset lazily by stamp.
	std::vector<float> dist;
	std::vector<int> parent, stamp;
	int curStamp = 0;

	int node(int ix, int iy) const { return iy * nx + ix; }
	int ixOf(int n) const { return n % nx; }
	int iyOf(int n) const { return n / nx; }
	int stepFrom(int n, int d) const {
		const int ix = ixOf(n), iy = iyOf(n);
		switch (d) {
		case PX:  return ix + 1 < nx ? n + 1 : -1;
		case NXD: return ix > 0 ? n - 1 : -1;
		case PY:  return iy + 1 < ny ? n + nx : -1;
		case NYD: return iy > 0 ? n - nx : -1;
		}
		return -1;
	}
	static long long edgeKey(int a, int b) {
		const int lo = std::min(a, b), hi = std::max(a, b);
		return (long long)lo * 2 + (hi - lo == 1 ? 0 : 1);
	}

	bool build();
	void markPin(int k, const GridPin &p, PinInfo &info);
	float moveCost(int k, int from, int arrived, int d, int to, bool goal, bool relaxed) const;
	bool passable(int k, int n, bool relaxed) const;
	bool goalNode(int n) const { return inTree[n] && !channel[n]; }
	void addToTree(int n);
	void addTreeEdge(NetState &ns, int a, int b);
	void resetTree();
	bool connect(int k, const PinInfo &p, bool relaxed, NetState &ns);
	bool routeNet(int k, bool relaxed);
	void commit(int k, int sign);
	RouteResult extract(int k, bool &ok) const;
};

bool Router::build() {
	float minx = FLT_MAX, maxx = -FLT_MAX, miny = FLT_MAX, maxy = -FLT_MAX;
	std::vector<float> ex, ey;
	for (const GridNet &net : in.nets)
		for (const GridPin &p : net.pins) {
			minx = std::min(minx, p.x); maxx = std::max(maxx, p.x);
			miny = std::min(miny, p.y); maxy = std::max(maxy, p.y);
			ex.push_back(p.x); ey.push_back(p.y);
		}
	if (minx > maxx) return false;
	for (const GridFixedSeg &s : in.fixed) {
		ex.push_back(s.bx); ex.push_back(s.ex);
		ey.push_back(s.by); ey.push_back(s.ey);
	}
	for (const GridPin &p : in.foreignPins) { ex.push_back(p.x); ey.push_back(p.y); }
	const float m = in.margin;
	xs = makeLines(minx - m, maxx + m, in.step, ex);
	ys = makeLines(miny - m, maxy + m, in.step, ey);
	nx = (int)xs.size(); ny = (int)ys.size();
	if ((size_t)nx * (size_t)ny > MAX_NODES || nx < 2 || ny < 2) return false;
	N = nx * ny;

	blocked.assign(N, 0); channel.assign(N, 0); hug.assign(N, 0);
	owner.assign(N, -1); nearPin.assign(N, -1);
	useH.assign(N, 0); useV.assign(N, 0); fixedH.assign(N, 0); fixedV.assign(N, 0);
	hist.assign(N, 0.0f);
	inTree.assign(N, 0); treeAxis.assign(N, 0);
	dist.assign((size_t)N * 5, 0.0f); parent.assign((size_t)N * 5, -1); stamp.assign((size_t)N * 5, 0);

	// Gate bodies, edges included.
	for (const GridRect &r : in.obstacles) {
		auto x0 = std::lower_bound(xs.begin(), xs.end(), r.l - EPS) - xs.begin();
		auto x1 = std::upper_bound(xs.begin(), xs.end(), r.r + EPS) - xs.begin();
		auto y0 = std::lower_bound(ys.begin(), ys.end(), r.b - EPS) - ys.begin();
		auto y1 = std::upper_bound(ys.begin(), ys.end(), r.t + EPS) - ys.begin();
		for (long iy = y0; iy < y1; iy++)
			for (long ix = x0; ix < x1; ix++) blocked[node((int)ix, (int)iy)] = 1;
	}
	for (int n = 0; n < N; n++) {
		if (!blocked[n]) continue;
		for (int d = 0; d < 4; d++) {
			const int m2 = stepFrom(n, d);
			if (m2 >= 0 && !blocked[m2]) hug[m2] = 1;
		}
	}

	// Wires that stay put.
	for (const GridFixedSeg &s : in.fixed) {
		const bool h = std::fabs(s.by - s.ey) < EPS, v = std::fabs(s.bx - s.ex) < EPS;
		if (h == v) continue;   // a dot, or not orthogonal
		if (h) {
			const int iy = findLine(ys, s.by);
			if (iy < 0) continue;
			const float lo = std::min(s.bx, s.ex), hi = std::max(s.bx, s.ex);
			for (int ix = 0; ix < nx; ix++)
				if (xs[ix] >= lo - EPS && xs[ix] <= hi + EPS) { useH[node(ix, iy)]++; fixedH[node(ix, iy)]++; }
		} else {
			const int ix = findLine(xs, s.bx);
			if (ix < 0) continue;
			const float lo = std::min(s.by, s.ey), hi = std::max(s.by, s.ey);
			for (int iy = 0; iy < ny; iy++)
				if (ys[iy] >= lo - EPS && ys[iy] <= hi + EPS) { useV[node(ix, iy)]++; fixedV[node(ix, iy)]++; }
		}
	}

	// Pins of the nets being routed.
	pins.assign(in.nets.size(), {});
	for (size_t k = 0; k < in.nets.size(); k++) {
		pins[k].resize(in.nets[k].pins.size());
		for (size_t i = 0; i < in.nets[k].pins.size(); i++) markPin((int)k, in.nets[k].pins[i], pins[k][i]);
	}
	// Pins nobody here routes: close the spot right outside the tip (a wire
	// through it would look attached) and charge for brushing past.
	for (const GridPin &p : in.foreignPins) {
		const int ix = nearestLine(xs, p.x), iy = nearestLine(ys, p.y);
		if (ix < 0 || iy < 0 || std::fabs(xs[ix] - p.x) > 0.3f || std::fabs(ys[iy] - p.y) > 0.3f) continue;
		const int tip = node(ix, iy);
		int d = NONE;
		if (p.dx > 0) d = PX; else if (p.dx < 0) d = NXD; else if (p.dy > 0) d = PY; else if (p.dy < 0) d = NYD;
		int around = tip;
		if (d != NONE) {
			int n = stepFrom(tip, d);
			while (n >= 0 && blocked[n]) n = stepFrom(n, d);
			if (n >= 0 && owner[n] == -1) { owner[n] = -3; around = n; }
		}
		for (int dd = 0; dd < 4; dd++) {
			const int m2 = stepFrom(around, dd);
			if (m2 >= 0 && owner[m2] == -1) nearPin[m2] = -2;
		}
	}
	return true;
}

void Router::markPin(int k, const GridPin &p, PinInfo &info) {
	const int ix = findLine(xs, p.x), iy = findLine(ys, p.y);
	if (ix < 0 || iy < 0) return;
	info.node = node(ix, iy);
	if (p.dx > 0) info.dir = PX; else if (p.dx < 0) info.dir = NXD;
	else if (p.dy > 0) info.dir = PY; else if (p.dy < 0) info.dir = NYD;
	info.walk.push_back(info.node);
	if (info.dir != NONE) {
		// Out through the gate body to the first open node, at least one step.
		int n = stepFrom(info.node, info.dir);
		while (n >= 0 && blocked[n]) { info.walk.push_back(n); n = stepFrom(n, info.dir); }
		if (n >= 0) info.walk.push_back(n);
		else { info.walk.resize(1); info.dir = NONE; }
	}
	info.stub = info.walk.back();
	for (size_t i = 0; i < info.walk.size(); i++) {
		const int n = info.walk[i];
		if (owner[n] == -1 || owner[n] == k) owner[n] = k;
		if (i + 1 < info.walk.size()) channel[n] = 1;
	}
	for (int d = 0; d < 4; d++) {
		const int m2 = stepFrom(info.stub, d);
		if (m2 >= 0 && owner[m2] == -1) nearPin[m2] = nearPin[m2] == -1 ? k : -2;
	}
}

bool Router::passable(int k, int n, bool relaxed) const {
	if (owner[n] == -3) return false;
	if (owner[n] >= 0) return owner[n] == k && !channel[n];
	if (blocked[n]) return relaxed;
	return true;
}

float Router::moveCost(int k, int from, int arrived, int d, int to, bool goal, bool relaxed) const {
	const bool h = horizontalDir(d);
	float c = (h ? std::fabs(xs[ixOf(to)] - xs[ixOf(from)]) : std::fabs(ys[iyOf(to)] - ys[iyOf(from)])) / in.step;
	if (arrived != NONE && arrived != d) {
		c += BEND;
		if (useH[from] + useV[from] > 0) c += TOUCH;   // our corner on their line
	}
	const int axisUse = h ? useH[to] : useV[to];
	const int crossUse = h ? useV[to] : useH[to];
	if (axisUse > 0) c += OVERLAP * axisUse;
	else if (crossUse > 0) c += goal ? TOUCH : CROSS;
	// Room beside the wire: others (and our own branches) one or two steps off.
	const int ix = ixOf(to), iy = iyOf(to);
	for (int off = 1; off <= 2; off++) {
		const float price = off == 1 ? PARALLEL_NEAR : PARALLEL_FAR;
		for (int sgn = -1; sgn <= 1; sgn += 2) {
			if (h) {
				const int j = iy + sgn * off;
				if (j < 0 || j >= ny) continue;
				const int m2 = node(ix, j);
				if (useH[m2] > 0 || (treeAxis[m2] & 1)) c += price;
			} else {
				const int j = ix + sgn * off;
				if (j < 0 || j >= nx) continue;
				const int m2 = node(j, iy);
				if (useV[m2] > 0 || (treeAxis[m2] & 2)) c += price;
			}
		}
	}
	// Among otherwise equal routes, run the jog down the middle: a vertical
	// run pays for its distance from the middle x, a horizontal one from the
	// middle y. Small enough never to outweigh a bend.
	c += CENTER * (h ? std::fabs(ys[iyOf(to)] - cy) : std::fabs(xs[ixOf(to)] - cx)) / in.step
	     * (h ? std::fabs(xs[ixOf(to)] - xs[ixOf(from)]) : std::fabs(ys[iyOf(to)] - ys[iyOf(from)])) / in.step;
	if (hug[to]) c += HUG_GATE;
	if (nearPin[to] != -1 && nearPin[to] != k) c += NEAR_PIN;
	if (relaxed && blocked[to] && owner[to] == -1) c += RELAXED_WALL;
	c += hist[to];
	return c;
}

void Router::addToTree(int n) {
	if (inTree[n]) return;
	inTree[n] = 1;
	treeNodes.push_back(n);
	const float x = xs[ixOf(n)], y = ys[iyOf(n)];
	if (treeNodes.size() == 1) { tx0 = tx1 = x; ty0 = ty1 = y; }
	else { tx0 = std::min(tx0, x); tx1 = std::max(tx1, x); ty0 = std::min(ty0, y); ty1 = std::max(ty1, y); }
}

void Router::addTreeEdge(NetState &ns, int a, int b) {
	ns.edges.insert(edgeKey(a, b));
	const uint8_t bit = (std::abs(a - b) == 1) ? 1 : 2;
	treeAxis[a] |= bit; treeAxis[b] |= bit;
	addToTree(a); addToTree(b);
}

void Router::resetTree() {
	for (int n : treeNodes) { inTree[n] = 0; treeAxis[n] = 0; }
	treeNodes.clear();
}

bool Router::connect(int k, const PinInfo &p, bool relaxed, NetState &ns) {
	if (p.node < 0) return false;
	if (inTree[p.node]) return true;
	// The forced way out of the gate first.
	for (size_t i = 0; i + 1 < p.walk.size(); i++) {
		if (inTree[p.walk[i + 1]]) {
			for (size_t j = 0; j <= i; j++) addTreeEdge(ns, p.walk[j], p.walk[j + 1]);
			return true;
		}
	}
	const int start = p.stub;
	if (!relaxed && owner[start] != k) return false;

	if (++curStamp == 0x7fffffff) { std::fill(stamp.begin(), stamp.end(), 0); curStamp = 1; }
	typedef std::pair<float, int> QE;
	std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
	auto heur = [&](int n) {
		const float x = xs[ixOf(n)], y = ys[iyOf(n)];
		const float dx = x < tx0 ? tx0 - x : (x > tx1 ? x - tx1 : 0.0f);
		const float dy = y < ty0 ? ty0 - y : (y > ty1 ? y - ty1 : 0.0f);
		return (dx + dy) / in.step;
	};
	{
		// Middle of this connection: between the start and the nearest point of
		// the tree's extent.
		const float sx = xs[ixOf(start)], sy = ys[iyOf(start)];
		cx = (sx + std::min(std::max(sx, tx0), tx1)) / 2.0f;
		cy = (sy + std::min(std::max(sy, ty0), ty1)) / 2.0f;
	}
	const int s0 = start * 5 + p.dir;
	stamp[s0] = curStamp; dist[s0] = 0.0f; parent[s0] = -1;
	pq.push({ heur(start), s0 });
	int goal = -1;
	size_t pops = 0;
	while (!pq.empty()) {
		const QE top = pq.top(); pq.pop();
		const int s = top.second;
		const int n = s / 5, arrived = s % 5;
		const float g = dist[s];
		if (top.first > g + heur(n) + 1e-4f) continue;   // stale
		if (goalNode(n) && n != start) { goal = s; break; }
		if (++pops > MAX_POPS) break;
		for (int d = 0; d < 4; d++) {
			if (arrived != NONE && d == reverseDir(arrived)) continue;
			const int m2 = stepFrom(n, d);
			if (m2 < 0) continue;
			const bool isGoal = goalNode(m2);
			if (!isGoal && !passable(k, m2, relaxed)) continue;
			const float ng = g + moveCost(k, n, arrived, d, m2, isGoal, relaxed);
			const int s2 = m2 * 5 + d;
			if (stamp[s2] == curStamp && dist[s2] <= ng) continue;
			stamp[s2] = curStamp; dist[s2] = ng; parent[s2] = s;
			pq.push({ ng + heur(m2), s2 });
		}
	}
	if (goal < 0) return false;

	ns.cost += dist[goal];
	for (size_t j = 0; j + 1 < p.walk.size(); j++) addTreeEdge(ns, p.walk[j], p.walk[j + 1]);
	std::vector<int> path;
	for (int s = goal; s >= 0; s = parent[s]) path.push_back(s / 5);
	for (size_t j = 0; j + 1 < path.size(); j++) addTreeEdge(ns, path[j], path[j + 1]);
	if (path.size() == 1) addToTree(path[0]);
	return true;
}

bool Router::routeNet(int k, bool relaxed) {
	NetState ns;
	resetTree();
	const std::vector<PinInfo> &ps = pins[k];
	const GridNet &net = in.nets[k];
	if (ps.size() < 2) return false;
	const int root = (net.root >= 0 && net.root < (int)ps.size()) ? net.root : 0;
	if (ps[root].node < 0) return false;
	for (size_t j = 0; j + 1 < ps[root].walk.size(); j++) addTreeEdge(ns, ps[root].walk[j], ps[root].walk[j + 1]);
	addToTree(ps[root].node);

	std::vector<int> left;
	for (int i = 0; i < (int)ps.size(); i++) if (i != root) left.push_back(i);
	while (!left.empty()) {
		// Nearest pin to the tree next, so branches join where they're closest.
		size_t best = 0; float bestD = FLT_MAX;
		for (size_t i = 0; i < left.size(); i++) {
			const PinInfo &p = ps[left[i]];
			if (p.node < 0) return false;
			const float px = xs[ixOf(p.node)], py = ys[iyOf(p.node)];
			for (int n : treeNodes) {
				const float d = std::fabs(xs[ixOf(n)] - px) + std::fabs(ys[iyOf(n)] - py);
				if (d < bestD) { bestD = d; best = i; }
			}
		}
		if (!connect(k, ps[left[best]], relaxed, ns)) { resetTree(); return false; }
		left.erase(left.begin() + best);
	}
	// Which nodes the net lies on, per axis.
	std::set<int> h, v;
	for (long long e : ns.edges) {
		const int a = (int)(e / 2);
		if (e % 2 == 0) { h.insert(a); h.insert(a + 1); }
		else { v.insert(a); v.insert(a + nx); }
	}
	ns.nodesH.assign(h.begin(), h.end());
	ns.nodesV.assign(v.begin(), v.end());
	ns.ok = true;
	nets[k] = std::move(ns);
	resetTree();
	return true;
}

void Router::commit(int k, int sign) {
	for (int n : nets[k].nodesH) useH[n] += sign;
	for (int n : nets[k].nodesV) useV[n] += sign;
}

RouteResult Router::extract(int k, bool &ok) const {
	RouteResult out;
	ok = false;
	// Maximal straight runs.
	std::vector<std::pair<int, int>> hEdges, vEdges;   // (row/col, index along)
	for (long long e : nets[k].edges) {
		const int a = (int)(e / 2);
		if (e % 2 == 0) hEdges.push_back({ iyOf(a), ixOf(a) });
		else vEdges.push_back({ ixOf(a), iyOf(a) });
	}
	std::sort(hEdges.begin(), hEdges.end());
	std::sort(vEdges.begin(), vEdges.end());
	long id = 0;
	std::vector<Segment> segs;
	for (size_t i = 0; i < hEdges.size();) {
		size_t j = i;
		while (j + 1 < hEdges.size() && hEdges[j + 1].first == hEdges[i].first && hEdges[j + 1].second == hEdges[j].second + 1) j++;
		Segment s;
		s.id = id++; s.vertical = false;
		s.bx = xs[hEdges[i].second]; s.ex = xs[hEdges[j].second + 1];
		s.by = s.ey = ys[hEdges[i].first];
		segs.push_back(s);
		i = j + 1;
	}
	for (size_t i = 0; i < vEdges.size();) {
		size_t j = i;
		while (j + 1 < vEdges.size() && vEdges[j + 1].first == vEdges[i].first && vEdges[j + 1].second == vEdges[j].second + 1) j++;
		Segment s;
		s.id = id++; s.vertical = true;
		s.bx = s.ex = xs[vEdges[i].first];
		s.by = ys[vEdges[i].second]; s.ey = ys[vEdges[j].second + 1];
		segs.push_back(s);
		i = j + 1;
	}
	// Junctions and bends: a horizontal and a vertical run of one tree that
	// share a point meet there.
	for (Segment &a : segs) {
		if (a.vertical) continue;
		for (Segment &b : segs) {
			if (!b.vertical) continue;
			if (b.bx < a.bx - EPS || b.bx > a.ex + EPS || a.by < b.by - EPS || a.by > b.ey + EPS) continue;
			a.crossings.push_back({ b.bx, b.id });
			b.crossings.push_back({ a.by, a.id });
		}
	}
	// Each pin onto a run through it, one along the way it leaves if possible.
	const std::vector<PinInfo> &ps = pins[k];
	for (size_t i = 0; i < ps.size(); i++) {
		const float px = in.nets[k].pins[i].x, py = in.nets[k].pins[i].y;
		const bool wantH = ps[i].dir == PX || ps[i].dir == NXD;
		const bool wantV = ps[i].dir == PY || ps[i].dir == NYD;
		Segment *pick = nullptr;
		for (Segment &s : segs) {
			const bool on = s.vertical
				? (std::fabs(s.bx - px) < EPS && py >= s.by - EPS && py <= s.ey + EPS)
				: (std::fabs(s.by - py) < EPS && px >= s.bx - EPS && px <= s.ex + EPS);
			if (!on) continue;
			if (pick == nullptr || (s.vertical ? wantV : wantH)) pick = &s;
			if (s.vertical ? wantV : wantH) break;
		}
		if (pick == nullptr) return out;
		pick->pins.push_back((int)i);
	}
	out.segments = std::move(segs);
	out.nextId = id;
	ok = !out.segments.empty();
	return out;
}

GridOutput Router::run() {
	GridOutput out;
	const size_t K = in.nets.size();
	out.ok.assign(K, false);
	out.routes.assign(K, RouteResult());
	if (K == 0 || !build()) return out;
	nets.assign(K, NetState());

	// Small, short nets first: they have the fewest ways around.
	std::vector<int> order(K);
	std::vector<float> span(K, 0.0f);
	for (size_t k = 0; k < K; k++) {
		order[k] = (int)k;
		float a = FLT_MAX, b = -FLT_MAX, c = FLT_MAX, d = -FLT_MAX;
		for (const GridPin &p : in.nets[k].pins) { a = std::min(a, p.x); b = std::max(b, p.x); c = std::min(c, p.y); d = std::max(d, p.y); }
		span[k] = (b - a) + (d - c);
	}
	std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
		if (in.nets[a].pins.size() != in.nets[b].pins.size()) return in.nets[a].pins.size() < in.nets[b].pins.size();
		return span[a] < span[b];
	});

	std::vector<NetState> best;
	int bestOverlaps = 0x7fffffff;
	float bestCost = FLT_MAX, lastCost = FLT_MAX;
	for (int pass = 0; pass < PASSES; pass++) {
		for (int k : order) {
			if (nets[k].ok) { commit(k, -1); nets[k] = NetState(); }
			if (routeNet(k, false) || routeNet(k, true)) commit(k, +1);
		}
		int overlaps = 0;
		float total = 0.0f;
		for (size_t k = 0; k < K; k++) total += nets[k].ok ? nets[k].cost : 0.0f;
		for (int n = 0; n < N; n++) {
			const bool clash = (useH[n] >= 2 && useH[n] > fixedH[n]) || (useV[n] >= 2 && useV[n] > fixedV[n]);
			if (clash) { overlaps++; hist[n] += HISTORY_STEP; }
		}
		if (overlaps < bestOverlaps || (overlaps == bestOverlaps && total < bestCost - 1e-3f)) {
			best = nets; bestOverlaps = overlaps; bestCost = total;
		}
		if (overlaps == 0 && pass >= 1 && total >= lastCost - 1e-3f) break;
		lastCost = total;
	}
	nets = best;
	out.overlaps = bestOverlaps == 0x7fffffff ? 0 : bestOverlaps;
	for (size_t k = 0; k < K; k++) {
		if (!nets[k].ok) continue;
		bool ok = false;
		RouteResult r = extract((int)k, ok);
		if (ok) { out.ok[k] = true; out.routes[k] = std::move(r); }
	}
	return out;
}

}  // namespace

GridOutput routeGrid(const GridInput &in) {
	Router r(in);
	return r.run();
}

}  // namespace route
}  // namespace cl
