// GridRouter tests: routes must be valid trees that reach every pin, keep out
// of gate bodies, never share a line with another wire, and not cross when a
// clean layout exists (the fan-out that used to stack every trunk on one line).
#include <doctest/doctest.h>
#include "route/GridRouter.h"
#include <cmath>
#include <functional>
#include <map>

using namespace cl::route;

namespace {

const float E = 1e-3f;

// Every pin placed exactly once, and every segment reachable from every other
// through recorded junctions (crossings are two-sided).
bool validTree(const RouteResult &r, size_t pinCount) {
	if (r.segments.empty()) return false;
	std::map<int, int> pinSeen;
	std::map<long, int> index;
	for (size_t i = 0; i < r.segments.size(); i++) {
		index[r.segments[i].id] = (int)i;
		for (int p : r.segments[i].pins) pinSeen[p]++;
	}
	if (pinSeen.size() != pinCount) return false;
	for (auto &p : pinSeen) if (p.second != 1) return false;
	std::vector<int> parent(r.segments.size());
	for (size_t i = 0; i < parent.size(); i++) parent[i] = (int)i;
	std::function<int(int)> find = [&](int x) { return parent[x] == x ? x : parent[x] = find(parent[x]); };
	for (size_t i = 0; i < r.segments.size(); i++)
		for (auto &c : r.segments[i].crossings) {
			auto it = index.find(c.second);
			if (it == index.end()) return false;
			// the other side must record it too
			bool back = false;
			for (auto &cb : r.segments[it->second].crossings) if (cb.second == r.segments[i].id) back = true;
			if (!back) return false;
			parent[find((int)i)] = find(it->second);
		}
	for (size_t i = 0; i < parent.size(); i++) if (find((int)i) != find(0)) return false;
	return true;
}

// A horizontal segment of one route crossing a vertical one of another, away
// from both ends.
int crossings(const RouteResult &a, const RouteResult &b) {
	int n = 0;
	for (const Segment &h : a.segments) for (const Segment &v : b.segments) {
		for (int flip = 0; flip < 2; flip++) {
			const Segment &H = flip ? v : h, &V = flip ? h : v;
			if (H.vertical || !V.vertical) continue;
			if (V.bx > H.bx + E && V.bx < H.ex - E && H.by > V.by + E && H.by < V.ey - E) n++;
		}
	}
	return n;
}

// Two routes lying along the same line for any length, or touching at all on it.
bool sharesLine(const RouteResult &a, const RouteResult &b) {
	for (const Segment &s : a.segments) for (const Segment &t : b.segments) {
		if (s.vertical != t.vertical) continue;
		if (s.vertical) {
			if (std::fabs(s.bx - t.bx) < E && std::min(s.ey, t.ey) >= std::max(s.by, t.by) - E) return true;
		} else {
			if (std::fabs(s.by - t.by) < E && std::min(s.ex, t.ex) >= std::max(s.bx, t.bx) - E) return true;
		}
	}
	return false;
}

bool entersRect(const RouteResult &r, const GridRect &b) {
	for (const Segment &s : r.segments) {
		// sample the segment; any point strictly inside the rect is a failure
		for (int i = 0; i <= 40; i++) {
			const float x = s.bx + (s.ex - s.bx) * i / 40.0f, y = s.by + (s.ey - s.by) * i / 40.0f;
			if (x > b.l + E && x < b.r - E && y > b.b + E && y < b.t - E) return true;
		}
	}
	return false;
}

GridPin pin(float x, float y, int dx, int dy) { GridPin p; p.x = x; p.y = y; p.dx = dx; p.dy = dy; return p; }

}  // namespace

TEST_CASE("grid: facing pins on one line route as a single straight wire") {
	GridInput in;
	GridNet n; n.pins = { pin(0, 0, 1, 0), pin(5, 0, -1, 0) };
	in.nets.push_back(n);
	GridOutput out = routeGrid(in);
	REQUIRE(out.ok[0]);
	REQUIRE(out.routes[0].segments.size() == 1);
	const Segment &s = out.routes[0].segments[0];
	CHECK_FALSE(s.vertical);
	CHECK(s.bx == doctest::Approx(0.0f));
	CHECK(s.ex == doctest::Approx(5.0f));
	CHECK(validTree(out.routes[0], 2));
}

TEST_CASE("grid: a gate in the way is routed around, not through") {
	GridInput in;
	GridNet n; n.pins = { pin(0, 0, 1, 0), pin(10, 0, -1, 0) };
	in.nets.push_back(n);
	GridRect gate; gate.l = 4; gate.b = -1; gate.r = 6; gate.t = 1;
	in.obstacles.push_back(gate);
	GridOutput out = routeGrid(in);
	REQUIRE(out.ok[0]);
	CHECK(validTree(out.routes[0], 2));
	CHECK_FALSE(entersRect(out.routes[0], gate));
}

TEST_CASE("grid: a wire that stays put is not overlapped") {
	GridInput in;
	GridNet n; n.pins = { pin(0, 0, 1, 0), pin(10, 0, -1, 0) };
	in.nets.push_back(n);
	GridFixedSeg f; f.bx = 3; f.by = 0; f.ex = 7; f.ey = 0;
	in.fixed.push_back(f);
	GridOutput out = routeGrid(in);
	REQUIRE(out.ok[0]);
	RouteResult fixed;
	Segment fs; fs.bx = 3; fs.by = 0; fs.ex = 7; fs.ey = 0; fs.vertical = false;
	fixed.segments.push_back(fs);
	CHECK_FALSE(sharesLine(out.routes[0], fixed));
}

// The shape from the counter screenshot: four outputs stacked on the right of
// one gate, each fanning out to two pins further right in the same top-to-
// bottom order. A clean, crossing-free layout exists; the old router stacked
// all four trunks on one vertical line.
TEST_CASE("grid: stacked fan-outs get their own lanes and don't cross") {
	GridInput in;
	GridRect counter; counter.l = -4; counter.b = -0.5f; counter.r = 0; counter.t = 3.5f;
	in.obstacles.push_back(counter);
	for (int i = 0; i < 4; i++) {
		GridNet n;
		const float y = 3.0f - i;            // outputs at 3, 2, 1, 0
		const float t = 10.5f - 3.5f * i;    // targets well spread vertically
		n.pins = { pin(0, y, 1, 0), pin(12, t, -1, 0), pin(12, t - 1.5f, -1, 0) };
		n.root = 0;
		in.nets.push_back(n);
		GridRect target; target.l = 12; target.b = t - 2.0f; target.r = 14; target.t = t + 0.5f;
		in.obstacles.push_back(target);
	}
	GridOutput out = routeGrid(in);
	CHECK(out.overlaps == 0);
	for (int i = 0; i < 4; i++) {
		REQUIRE(out.ok[i]);
		CHECK(validTree(out.routes[i], 3));
		CHECK_FALSE(entersRect(out.routes[i], counter));
	}
	int total = 0;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++) if (i != j) {
			total += crossings(out.routes[i], out.routes[j]);
			CHECK_FALSE(sharesLine(out.routes[i], out.routes[j]));
		}
	CHECK(total == 0);
}

TEST_CASE("grid: pins that face up and down leave the gate that way") {
	GridInput in;
	GridNet n; n.pins = { pin(0, 2, 0, 1), pin(4, -2, 0, -1) };
	in.nets.push_back(n);
	GridRect a; a.l = -1; a.b = 0; a.r = 1; a.t = 2; in.obstacles.push_back(a);
	GridRect b; b.l = 3; b.b = -2; b.r = 5; b.t = 0; in.obstacles.push_back(b);
	GridOutput out = routeGrid(in);
	REQUIRE(out.ok[0]);
	CHECK(validTree(out.routes[0], 2));
	CHECK_FALSE(entersRect(out.routes[0], a));
	CHECK_FALSE(entersRect(out.routes[0], b));
	// pin 0's segment is vertical (it leaves upward)
	for (const Segment &s : out.routes[0].segments)
		for (int p : s.pins) if (p == 0) CHECK(s.vertical);
}
