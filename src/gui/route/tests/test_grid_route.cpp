// GridRouter tests: routes must be valid trees that reach every pin, keep out
// of gate bodies, never share a line with another wire, and not cross when a
// clean layout exists (the fan-out that used to stack every trunk on one line).
#include <doctest/doctest.h>
#include "route/GridRouter.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <sstream>
#include <string>

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

// Each connection is searched in a window around its ends first; a wall
// longer than that window must widen the search, not fail the wire or send
// it through the wall.
TEST_CASE("grid: a wall taller than the search window is still routed around") {
	GridInput in;
	in.margin = 30.0f;
	GridNet n; n.pins = { pin(0, 0, 1, 0), pin(10, 0, -1, 0) };
	in.nets.push_back(n);
	GridRect wall; wall.l = 4; wall.b = -20; wall.r = 6; wall.t = 20;
	in.obstacles.push_back(wall);
	GridOutput out = routeGrid(in);
	REQUIRE(out.ok[0]);
	CHECK(validTree(out.routes[0], 2));
	CHECK_FALSE(entersRect(out.routes[0], wall));
}

// A wide page of long wires (the shape Full rearrange gives a big circuit):
// every wire routes, cleanly, and the same input gives the same routes.
TEST_CASE("grid: a wide page of long wires routes every wire, the same way twice") {
	GridInput in;
	for (int col = 0; col < 8; col++) {
		GridRect g; g.l = col * 40.0f; g.b = 0; g.r = g.l + 3; g.t = 24;
		in.obstacles.push_back(g);
	}
	for (int i = 0; i < 40; i++) {
		const int from = i % 7, to = std::min(7, from + 1 + i % 4);
		GridNet n;
		n.pins = { pin(from * 40.0f + 3, 0.5f + (i % 24), 1, 0), pin(to * 40.0f, 23.5f - (i * 7 % 24), -1, 0) };
		in.nets.push_back(n);
	}
	const GridOutput a = routeGrid(in), b = routeGrid(in);
	for (size_t k = 0; k < in.nets.size(); k++) {
		REQUIRE(a.ok[k]);
		CHECK(validTree(a.routes[k], 2));
		for (const GridRect &g : in.obstacles) CHECK_FALSE(entersRect(a.routes[k], g));
		REQUIRE(a.routes[k].segments.size() == b.routes[k].segments.size());
		for (size_t s = 0; s < a.routes[k].segments.size(); s++) {
			CHECK(a.routes[k].segments[s].bx == b.routes[k].segments[s].bx);
			CHECK(a.routes[k].segments[s].by == b.routes[k].segments[s].by);
			CHECK(a.routes[k].segments[s].ex == b.routes[k].segments[s].ex);
			CHECK(a.routes[k].segments[s].ey == b.routes[k].segments[s].ey);
		}
	}
	CHECK(a.overlaps == b.overlaps);
}

TEST_CASE("grid: a pin with a neighbor's edge across its way out steps off it, not round the far side") {
	GridInput in;
	// The pin leaves its own gate to the right, and another gate's bottom edge
	// lies on that very line: walking on through it used to bring the wire out
	// on the far side, past the pin it was going to.
	in.obstacles = { { 0, 0, 4, 2 }, { 4.2f, 1, 9, 3 }, { 8, -4, 12, 0 } };
	GridNet net;
	net.pins = { { 4, 1, 1, 0 }, { 8, -2, -1, 0 } };
	in.nets = { net };
	GridOutput out = routeGrid(in);
	REQUIRE(out.ok.size() == 1);
	REQUIRE(out.ok[0]);
	CHECK(out.jammed[0]);
	CHECK_FALSE(out.forced[0]);
	CHECK(validTree(out.routes[0], 2));
	float len = 0.0f;
	for (const Segment &sg : out.routes[0].segments) len += std::fabs(sg.ex - sg.bx) + std::fabs(sg.ey - sg.by);
	CHECK(len <= 7.0f + E);   // straight there: 4 across, 3 down
}

TEST_CASE("grid: a pin pointing into the gate stacked on it runs straight through to that gate's pin on the same wire") {
	GridInput in;
	// Two gates stacked edge to edge, each with a pin in the middle of its top
	// (tri-state buffers and their enable pins): the lower pin points into the
	// upper gate, whose own pin on the same wire is straight above.
	in.obstacles = { { 0, 0, 4, 2 }, { 0, 2, 4, 4 } };
	GridNet net;
	net.pins = { { 2, 2, 0, 1 }, { 2, 4, 0, 1 } };
	in.nets = { net };
	GridOutput out = routeGrid(in);
	REQUIRE(out.ok.size() == 1);
	REQUIRE(out.ok[0]);
	CHECK_FALSE(out.jammed[0]);
	CHECK(validTree(out.routes[0], 2));
	for (const Segment &sg : out.routes[0].segments) {
		CHECK(sg.vertical);
		CHECK(sg.bx == doctest::Approx(2.0f));
	}
}

namespace {
// Bends: a horizontal and a vertical segment of one route meeting end to end.
int corners(const RouteResult &r) {
	int n = 0;
	for (const Segment &h : r.segments) for (const Segment &v : r.segments) {
		if (h.vertical || !v.vertical) continue;
		const bool atX = std::fabs(v.bx - h.bx) < E || std::fabs(v.bx - h.ex) < E;
		const bool atY = std::fabs(h.by - v.by) < E || std::fabs(h.by - v.ey) < E;
		if (atX && atY) n++;
	}
	return n;
}
}  // namespace

// The search steers each jog toward the middle of its connection, so a wire
// that has to rise past a part used to climb in two steps: up, across to the
// middle, up again. Polish slides the runs until the two jogs line up.
TEST_CASE("grid: a wire climbing past a part makes one jog, not a staircase") {
	GridInput in;
	in.obstacles = { { 7, -5, 9, -2 }, { 15, 5, 17, 8 } };
	GridNet n;
	n.pins = { pin(0, -5, 1, 0), pin(20, 3.5f, -1, 0) };
	in.nets = { n };
	GridOutput out = routeGrid(in);
	REQUIRE(out.ok[0]);
	CHECK(validTree(out.routes[0], 2));
	for (const GridRect &g : in.obstacles) CHECK_FALSE(entersRect(out.routes[0], g));
	CHECK(corners(out.routes[0]) == 2);
}

// A wire to two pins stacked on the far side: grown one pin at a time, the
// branch to the second pin came off a jog near the start and ran back along
// the first. Polish moves the junction to where the two pins are.
TEST_CASE("grid: a branch to a stacked pin comes off near that pin") {
	GridInput in;
	in.obstacles = { { 10, -4, 12, -1 }, { 7, -2, 9, 1 }, { 10, -6, 12, -3 } };
	GridNet n;
	n.pins = { pin(0, 1.5f, 1, 0), pin(20, 4, -1, 0), pin(20, 5.5f, -1, 0) };
	in.nets = { n };
	GridOutput out = routeGrid(in);
	REQUIRE(out.ok[0]);
	CHECK(validTree(out.routes[0], 3));
	for (const GridRect &g : in.obstacles) CHECK_FALSE(entersRect(out.routes[0], g));
	CHECK(corners(out.routes[0]) <= 2);
}

namespace {
float routeLength(const RouteResult &r) {
	float len = 0.0f;
	for (const Segment &s : r.segments) len += std::fabs(s.ex - s.bx) + std::fabs(s.ey - s.by);
	return len;
}
// A bus of `count` horizontal wires from x = -half to x = half, two units apart
// and centred on y = 0, routed first; then a wire from straight above the bus
// to straight below it.
GridInput busAndCrossingWire(int count, float half) {
	GridInput in;
	for (int i = 0; i < count; i++) {
		const float y = (i - (count - 1) / 2.0f) * 2.0f;
		GridNet b; b.pins = { pin(-half, y, 1, 0), pin(half, y, -1, 0) };
		in.nets.push_back(b);
	}
	GridNet w; w.pins = { pin(0, count + 4.0f, 0, -1), pin(0, -count - 4.0f, 0, 1) };
	in.nets.push_back(w);
	return in;
}

// "One bit cell" from lab6.cdl, page 2, as Straighten hands it over: tie
// points above and below the cell, buses across it. The six-pin wire joining
// the top tie points to the bottom ones (and the mux, the flip-flop, N.n) was
// routed last, every lane down the column already taken, and went right
// round the cell -- 110 units of wire where the person drew 78, straight down
// the middle crossing a few wires.
const char *BIT_CELL =
	"N 0 65 -16.5 -1 0 65 -18.5 0 1\n"
	"N 0 69 -17 -1 0 66 -18.5 0 1\n"
	"N 0 87 -36 -1 0 85.5 -36 1 0\n"
	"N 0 87.5 -38 -1 0 85.5 -37 1 0\n"
	"N 0 71 -23.5 0 1 62.5 -21.5 1 0 83 -21.5 -1 0\n"
	"N 0 70 -23.5 0 1 83 -22.5 -1 0 62.5 -22.5 1 0\n"
	"N 0 81 -41 1 0 67 -29.5 -1 0 66 -20.5 0 -1 66 -51 0 1\n"
	"N 0 73 -28.5 1 0 74.5 -28.5 -1 0\n"
	"N 0 63 -36 1 0 74.5 -31.5 -1 0 83.5 -36 -1 0\n"
	"N 0 77.5 -34.5 0 -1 83.5 -37 -1 0 63 -37 1 0\n"
	"N 1 73.5 -43 1 0 75 -43 -1 0\n"
	"N 1 73.5 -45 1 0 75 -45 -1 0\n"
	"N 1 73.5 -47 1 0 75 -47 -1 0\n"
	"N 1 73.5 -49 1 0 75 -48 -1 0\n"
	"N 0 66.5 -46 -1 0 63 -46 1 0\n"
	"N 0 81 -43 1 0 83.5 -46 -1 0\n"
	"N 4 81 -42 1 0 64 -20.5 0 -1 67 -31.5 -1 0 65 -51 0 1 80.5 -28.5 1 0 74.5 -24.5 -1 0\n"
	"N 0 65 -20.5 0 -1 67 -27.5 -1 0\n"
	"N 0 67 -25.5 -1 0 64 -51 0 1\n"
	"R 82.8118 -38.6394 86.1882 -37.3606\n"
	"R 72.546 -15.2788 85.454 -12.7212\n"
	"R 63.74 -53 64.26 -51\n"
	"R 60.5 -22.76 62.5 -22.24\n"
	"R 65.74 -53 66.26 -51\n"
	"R 61 -37.26 63 -36.74\n"
	"R 67.6863 -52.1394 70.3137 -50.8606\n"
	"R 82.9621 -35.1394 86.0379 -33.8606\n"
	"R 83.5 -36.26 85.5 -35.74\n"
	"R 74.5 -25.5 80.0512 -23.5\n"
	"R 83.5 -37.26 85.5 -36.74\n"
	"R 74.5 -34.5 80.5 -26.5\n"
	"R 63.6609 -54.6394 67.3391 -53.3606\n"
	"R 83 -21.76 85 -21.24\n"
	"R 83 -22.76 85 -22.24\n"
	"R 60.5 -21.76 62.5 -21.24\n"
	"R 83.5 -46.26 85.5 -45.74\n"
	"R 64.74 -53 65.26 -51\n"
	"R 65.74 -20.5 66.26 -18.5\n"
	"R 67 -32.5 73 -23.5\n"
	"R 58.7364 -19.6394 62.2636 -18.3606\n"
	"R 63.74 -20.5 64.26 -18.5\n"
	"R 67.6863 -19.6394 70.3137 -18.3606\n"
	"R 58.7364 -52.1394 62.2636 -50.8606\n"
	"R 64.74 -20.5 65.26 -18.5\n"
	"R 82.3494 -20.6394 85.6506 -19.3606\n"
	"R 82.3494 -24.1394 85.6506 -22.8606\n"
	"R 61 -46.26 63 -45.74\n"
	"R 65 -17.5 68 -15.5\n"
	"R 60.7242 -45.1394 63.2758 -43.8606\n"
	"R 61 -36.26 63 -35.74\n"
	"R 63.1609 -15.1394 66.8391 -13.8606\n"
	"R 75 -49 81 -40\n"
	"R 69 -18 72 -16\n"
	"R 83.6371 -45.1394 85.3629 -43.8606\n"
	"R 87 -37 90 -35\n"
	"R 87.5 -39 90.5 -37\n"
	"R 66.5 -50 73.5 -42\n"
	"P 64 -53 0 -1\n"
	"P 66 -53 0 -1\n"
	"P 61 -37 -1 0\n"
	"P 80.5 -31.5 1 0\n"
	"P 77.5 -26.5 0 1\n"
	"P 85 -21.5 1 0\n"
	"P 85 -22.5 1 0\n"
	"P 85.5 -46 1 0\n"
	"P 65 -53 0 -1\n"
	"P 64 -18.5 0 1\n"
	"P 61 -46 -1 0\n"
	"P 61 -36 -1 0\n"
	"P 81 -48 1 0\n"
	"P 75 -41 -1 0\n"
	"P 75 -42 -1 0\n"
	"P 75 -44 -1 0\n"
	"P 75 -46 -1 0\n";

GridInput parseInput(const char *text) {
	GridInput in;
	std::istringstream lines(text);
	std::string line;
	while (std::getline(lines, line)) {
		std::istringstream s(line);
		std::string t;
		s >> t;
		if (t == "N") {
			GridNet n;
			s >> n.root;
			GridPin p;
			while (s >> p.x >> p.y >> p.dx >> p.dy) n.pins.push_back(p);
			in.nets.push_back(n);
		} else if (t == "R") {
			GridRect r;
			s >> r.l >> r.b >> r.r >> r.t;
			in.obstacles.push_back(r);
		} else if (t == "P") {
			GridPin p;
			s >> p.x >> p.y >> p.dx >> p.dy;
			in.foreignPins.push_back(p);
		}
	}
	return in;
}
}  // namespace

TEST_CASE("grid: a wire from the top of a packed cell to the bottom goes down the middle, not round it") {
	GridInput in = parseInput(BIT_CELL);
	REQUIRE(in.nets.size() == 19);
	GridOutput out = routeGrid(in);
	size_t big = 0;
	for (size_t i = 0; i < in.nets.size(); i++) {
		REQUIRE(out.ok[i]);
		CHECK(validTree(out.routes[i], in.nets[i].pins.size()));
		if (in.nets[i].pins.size() > in.nets[big].pins.size()) big = i;
		for (size_t j = 0; j < i; j++) CHECK_FALSE(sharesLine(out.routes[i], out.routes[j]));
	}
	REQUIRE(in.nets[big].pins.size() == 6);
	CHECK(routeLength(out.routes[big]) < 85.0f);
	float total = 0.0f;
	for (const RouteResult &r : out.routes) total += routeLength(r);
	CHECK(total < 325.0f);   // 339.5 going round
}

// ...but not at any price: past a short bus with many wires, round the end
// is still the better picture.
TEST_CASE("grid: a wire past a short bus of many wires still goes round its end") {
	GridInput in = busAndCrossingWire(10, 4.0f);
	GridOutput out = routeGrid(in);
	for (size_t i = 0; i < in.nets.size(); i++) REQUIRE(out.ok[i]);
	int cross = 0;
	for (size_t i = 0; i + 1 < in.nets.size(); i++) cross += crossings(out.routes[i], out.routes.back());
	CHECK(cross == 0);
}
