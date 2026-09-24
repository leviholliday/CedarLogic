// Tidy Up layout tests: no two moved gates end up overlapping, Keep Layout
// lines columns up and levels near-miss wires, Rearrange orders by signal flow.
#include <doctest/doctest.h>
#include "route/Layout.h"
#include <cmath>

using namespace cl::route;

namespace {

LayoutNode box(float l, float b, float w, float h) {
	LayoutNode n; n.l = l; n.b = b; n.r = l + w; n.t = b + h; return n;
}
LayoutPin pin(float x, float y, int dx, int net, bool out) {
	LayoutPin p; p.x = x; p.y = y; p.dx = dx; p.net = net; p.output = out; return p;
}
bool overlap(const LayoutNode &a, std::pair<float, float> da, const LayoutNode &b, std::pair<float, float> db) {
	return a.l + da.first < b.r + db.first - 1e-3f && b.l + db.first < a.r + da.first - 1e-3f &&
	       a.b + da.second < b.t + db.second - 1e-3f && b.b + db.second < a.t + da.second - 1e-3f;
}
bool onGrid(float v) { return std::fabs(v * 2.0f - std::round(v * 2.0f)) < 1e-3f; }

}  // namespace

TEST_CASE("layout keep: a column of staggered gates lines up and a near-miss wire goes straight") {
	LayoutInput in;
	// driver on the left, output pin at y = 2
	LayoutNode drv = box(0, 1, 2, 2); drv.pins = { pin(2, 2, 1, 0, true) };
	// two gates in a rough column, x staggered by 0.5; the first's input is
	// 0.5 below the driver's output
	LayoutNode g1 = box(6, 0.5f, 2, 2); g1.pins = { pin(6, 1.5f, -1, 0, false) };
	LayoutNode g2 = box(6.5f, -3, 2, 2); g2.pins = { pin(6.5f, -2, -1, 1, false) };
	in.nodes = { drv, g1, g2 };
	auto off = layoutGates(in);
	REQUIRE(off.size() == 3);
	// the column lines up on its left edges (inputs-only gates)
	CHECK(in.nodes[1].l + off[1].first == doctest::Approx(in.nodes[2].l + off[2].first));
	// g1's input now level with the driver's output
	CHECK(1.5f + off[1].second == doctest::Approx(2.0f + off[0].second));
	for (auto &o : off) { CHECK(onGrid(o.first)); CHECK(onGrid(o.second)); }
}

TEST_CASE("layout keep: gates overlapping in a column are spread apart") {
	LayoutInput in;
	in.nodes = { box(0, 0, 2, 2), box(0.2f, 1, 2, 2), box(0, 1.5f, 2, 2) };
	auto off = layoutGates(in);
	for (size_t i = 0; i < 3; i++)
		for (size_t j = i + 1; j < 3; j++) CHECK_FALSE(overlap(in.nodes[i], off[i], in.nodes[j], off[j]));
}

TEST_CASE("layout rearrange: a chain placed backwards comes out left to right") {
	LayoutInput in;
	in.mode = TidyMode::Rearrange;
	// input -> gate -> output, but drawn output-first from the left
	LayoutNode out = box(0, 0, 2, 2);  out.pins = { pin(0, 1, -1, 1, false) };
	LayoutNode gate = box(4, 3, 2, 2); gate.pins = { pin(4, 4, -1, 0, false), pin(6, 4, 1, 1, true) };
	LayoutNode src = box(8, -3, 2, 2); src.pins = { pin(10, -2, 1, 0, true) };
	in.nodes = { out, gate, src };
	auto off = layoutGates(in);
	const float xo = in.nodes[0].l + off[0].first, xg = in.nodes[1].l + off[1].first, xs = in.nodes[2].l + off[2].first;
	CHECK(xs < xg);
	CHECK(xg < xo);
	// straight wires: each pin level with the one it's wired to
	CHECK(-2 + off[2].second == doctest::Approx(4 + off[1].second));
	CHECK(4 + off[1].second == doctest::Approx(1 + off[0].second));
	for (size_t i = 0; i < 3; i++)
		for (size_t j = i + 1; j < 3; j++) CHECK_FALSE(overlap(in.nodes[i], off[i], in.nodes[j], off[j]));
}

TEST_CASE("layout rearrange: a fixed gate and an unwired label stay put") {
	LayoutInput in;
	in.mode = TidyMode::Rearrange;
	LayoutNode a = box(0, 0, 2, 2); a.pins = { pin(2, 1, 1, 0, true) };
	LayoutNode b = box(10, 5, 2, 2); b.pins = { pin(10, 6, -1, 0, false) };
	LayoutNode title = box(-5, 10, 6, 1);
	LayoutNode fixed = box(20, 0, 2, 2); fixed.movable = false; fixed.pins = { pin(20, 1, -1, 0, false) };
	in.nodes = { a, b, title, fixed };
	auto off = layoutGates(in);
	CHECK(off[2].first == 0.0f); CHECK(off[2].second == 0.0f);
	CHECK(off[3].first == 0.0f); CHECK(off[3].second == 0.0f);
}

TEST_CASE("layout: a display made of packed lights is left exactly as drawn") {
	for (int mode = 0; mode < 2; mode++) {
		LayoutInput in;
		in.mode = mode ? TidyMode::Rearrange : TidyMode::KeepLayout;
		// a row of five lights, 2 apart, fed by one label
		LayoutNode label = box(-3, 0, 2, 1); label.pins = { pin(-1, 0.5f, 1, 0, true) };
		in.nodes.push_back(label);
		for (int i = 0; i < 5; i++) {
			LayoutNode led = box(2.0f * i, 0, 1.5f, 1.5f);
			led.indicator = true;
			led.pins = { pin(2.0f * i, 0.5f, -1, 0, false) };
			in.nodes.push_back(led);
		}
		auto off = layoutGates(in);
		for (int i = 1; i <= 5; i++) { CHECK(off[i].first == 0.0f); CHECK(off[i].second == 0.0f); }
	}
}
