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

// ---- Full rearrange: the rules from the owner's example circuits ----------

namespace {

// A keypad-like source with four right-facing pins, each wired to a label
// straight across and, through an inverter, to a second label below it.
LayoutInput keypadInverters() {
	LayoutInput in;
	in.mode = TidyMode::Rearrange;
	LayoutNode kp = box(0, -6, 9, 12);
	for (int i = 0; i < 4; i++) kp.pins.push_back(pin(9, 3 - 2 * i, 1, i, true));
	in.nodes.push_back(kp);
	for (int i = 0; i < 4; i++) {
		const float y = 3 - 2.0f * i;
		LayoutNode inv = box(30, y - 2, 6, 2); inv.pins = { pin(30, y - 1, -1, i, false), pin(36, y - 1, 1, 4 + i, true) };
		LayoutNode plain = box(42, y - 1, 2, 2); plain.pins = { pin(42, y, -1, i, false) };
		LayoutNode bar = box(42, y - 3, 2, 2); bar.pins = { pin(42, y - 2, -1, 4 + i, false) };
		in.nodes.push_back(inv); in.nodes.push_back(plain); in.nodes.push_back(bar);
	}
	return in;
}

float top(const LayoutInput &in, const std::vector<std::pair<float, float>> &off, size_t i) { return in.nodes[i].t + off[i].second; }
float left(const LayoutInput &in, const std::vector<std::pair<float, float>> &off, size_t i) { return in.nodes[i].l + off[i].first; }

}  // namespace

TEST_CASE("layout rearrange: output labels keep the order they were drawn in") {
	LayoutInput in = keypadInverters();
	auto off = layoutGates(in);
	// labels are nodes 2,3, 5,6, 8,9, 11,12: A A' B B' C C' D D', top to bottom
	const size_t labels[8] = { 2, 3, 5, 6, 8, 9, 11, 12 };
	for (int i = 0; i + 1 < 8; i++) CHECK(top(in, off, labels[i]) > top(in, off, labels[i + 1]));
	// and they stay in one column
	for (int i = 1; i < 8; i++) CHECK(left(in, off, labels[i]) == doctest::Approx(left(in, off, labels[0])));
	for (size_t i = 0; i < in.nodes.size(); i++)
		for (size_t j = i + 1; j < in.nodes.size(); j++) CHECK_FALSE(overlap(in.nodes[i], off[i], in.nodes[j], off[j]));
}

TEST_CASE("layout rearrange: a wire passing the inverter column runs straight to its label") {
	LayoutInput in = keypadInverters();
	auto off = layoutGates(in);
	for (int i = 0; i < 4; i++) {
		const size_t plain = 2 + 3 * i;
		// the plain label sits on a lane clear of every inverter, so its wire
		// can run straight across that column instead of round an inverter
		const float labelY = in.nodes[plain].pins[0].y + off[plain].second;
		for (int k = 0; k < 4; k++) {
			const size_t inv = 1 + 3 * k;
			const float invTop = top(in, off, inv), invBottom = in.nodes[inv].b + off[inv].second;
			CHECK((labelY > invTop + 0.5f || labelY < invBottom - 0.5f));
		}
		// and just above its own inverter, the way it was drawn
		CHECK(labelY > top(in, off, 1 + 3 * i));
		if (i) CHECK(labelY < in.nodes[1 + 3 * (i - 1)].b + off[1 + 3 * (i - 1)].second);
	}
}

TEST_CASE("layout rearrange: repeated groups drawn apart stay apart, in order") {
	// three identical slices: source -> gate -> sink, each 30 apart, chained
	// by one wire from a slice's gate to the next slice's gate
	LayoutInput in;
	in.mode = TidyMode::Rearrange;
	for (int s = 0; s < 3; s++) {
		const float X = 30.0f * s;
		const int n0 = 10 * s;
		LayoutNode src = box(X, 4, 2, 2);  src.pins = { pin(X + 2, 5, 1, n0, true) };
		LayoutNode g = box(X + 5, 0, 4, 4); g.pins = { pin(X + 5, 1, -1, n0, false), pin(X + 5, 3, -1, s ? n0 - 5 : -1, false),
		                                               pin(X + 9, 2, 1, n0 + 1, true), pin(X + 9, 1, 1, s < 2 ? n0 + 5 : -1, true) };
		LayoutNode sink = box(X + 12, -4, 2, 2); sink.pins = { pin(X + 12, -3, -1, n0 + 1, false) };
		in.nodes.push_back(src); in.nodes.push_back(g); in.nodes.push_back(sink);
	}
	auto off = layoutGates(in);
	for (int s = 0; s + 1 < 3; s++) {
		float r = -1e9f, l = 1e9f;
		for (int k = 0; k < 3; k++) {
			r = std::max(r, in.nodes[3 * s + k].r + off[3 * s + k].first);
			l = std::min(l, in.nodes[3 * (s + 1) + k].l + off[3 * (s + 1) + k].first);
		}
		CHECK(r < l);   // slice s wholly left of slice s + 1
	}
	// each slice reads left to right and is laid out the same way
	for (int s = 0; s < 3; s++) {
		CHECK(left(in, off, 3 * s) < left(in, off, 3 * s + 1));
		CHECK(left(in, off, 3 * s + 1) < left(in, off, 3 * s + 2));
		if (s) CHECK(left(in, off, 3 * s + 1) - left(in, off, 3 * s) == doctest::Approx(left(in, off, 1) - left(in, off, 0)));
	}
	for (size_t i = 0; i < in.nodes.size(); i++)
		for (size_t j = i + 1; j < in.nodes.size(); j++) CHECK_FALSE(overlap(in.nodes[i], off[i], in.nodes[j], off[j]));
}

TEST_CASE("layout rearrange: a group wired top to bottom keeps flowing down") {
	// two adders stacked: the first's bottom pins feed the second's top pins,
	// a keypad beside them feeds the first from the side
	LayoutInput in;
	in.mode = TidyMode::Rearrange;
	LayoutNode kp = box(-14, 2, 9, 12);
	for (int i = 0; i < 4; i++) kp.pins.push_back({ -5, 11.0f - 2 * i, 1, 0, i, true });
	LayoutNode a1 = box(-8, 0, 16, 8), a2 = box(-8, -14, 16, 8);
	for (int i = 0; i < 4; i++) {
		a1.pins.push_back({ -5.0f + i, 8, 0, 1, i, false });        // top inputs
		a1.pins.push_back({ -5.0f + i, 0, 0, -1, 4 + i, true });    // bottom outputs, same spacing
		a2.pins.push_back({ -5.0f + i, -6, 0, 1, 4 + i, false });
	}
	in.nodes = { kp, a1, a2 };
	auto off = layoutGates(in);
	// still top to bottom: keypad above, then adder 1, then adder 2
	CHECK(in.nodes[1].b + off[1].second > top(in, off, 2));
	// the keypad's pins stay left of the inputs they feed, so its wires turn once
	CHECK(in.nodes[0].pins[0].x + off[0].first < in.nodes[1].pins[0].x + off[1].first);
	// adder 2 straight under adder 1, its inputs right below adder 1's outputs
	CHECK(left(in, off, 1) == doctest::Approx(left(in, off, 2)));
	for (size_t i = 0; i < 3; i++)
		for (size_t j = i + 1; j < 3; j++) CHECK_FALSE(overlap(in.nodes[i], off[i], in.nodes[j], off[j]));
}

TEST_CASE("layout rearrange: the same input gives the same answer") {
	LayoutInput in = keypadInverters();
	auto a = layoutGates(in), b = layoutGates(in);
	CHECK(a == b);
}
