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

TEST_CASE("layout keep: a part sitting on its own driver goes sideways, level, not down") {
	LayoutInput in;
	// a switch, and the label it feeds drawn on top of it: the label's input pin
	// (x = 1) is left of the switch's output pin (x = 3)
	LayoutNode sw = box(0, 0, 3, 2);     sw.pins = { pin(3, 1, 1, 0, true) };
	LayoutNode lab = box(1, 0, 4, 2);    lab.pins = { pin(1, 1, -1, 0, false) };
	in.nodes = { sw, lab };
	auto off = layoutGates(in);
	REQUIRE(off.size() == 2);
	CHECK_FALSE(overlap(in.nodes[0], off[0], in.nodes[1], off[1]));
	// level with the driver (the wire runs straight), and with room for the wire
	CHECK(1.0f + off[1].second == doctest::Approx(1.0f + off[0].second));
	CHECK(1.0f + off[1].first >= 3.0f + off[0].first + 2.0f - 1e-3f);
	for (auto &o : off) { CHECK(onGrid(o.first)); CHECK(onGrid(o.second)); }
}

TEST_CASE("layout keep: two overlapping parts that are not wired together still separate downwards") {
	LayoutInput in;
	in.nodes = { box(0, 0, 3, 2), box(1, 0, 4, 2) };
	auto off = layoutGates(in);
	CHECK_FALSE(overlap(in.nodes[0], off[0], in.nodes[1], off[1]));
	CHECK(off[1].first == doctest::Approx(off[0].first));
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

TEST_CASE("layout rearrange: parts drawn turned round flow right to left") {
	LayoutInput in;
	in.mode = TidyMode::Rearrange;
	// switch -> inverter -> label, every part turned round (outputs on the
	// left, inputs on the right), drawn jumbled up
	LayoutNode out = box(8, 0, 2, 2);   out.pins = { pin(10, 1, 1, 1, false) };
	LayoutNode gate = box(0, 3, 2, 2);  gate.pins = { pin(2, 4, 1, 0, false), pin(0, 4, -1, 1, true) };
	LayoutNode src = box(4, -3, 2, 2);  src.pins = { pin(4, -2, -1, 0, true) };
	in.nodes = { out, gate, src };
	auto off = layoutGates(in);
	auto px = [&](size_t v, size_t p) { return in.nodes[v].pins[p].x + off[v].first; };
	auto py = [&](size_t v, size_t p) { return in.nodes[v].pins[p].y + off[v].second; };
	// each wire leaves its driver leftward straight into the pin it feeds
	CHECK(px(2, 0) > px(1, 0));
	CHECK(px(1, 1) > px(0, 0));
	CHECK(py(2, 0) == doctest::Approx(py(1, 0)));
	CHECK(py(1, 1) == doctest::Approx(py(0, 0)));
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

TEST_CASE("layout rearrange: a caption drawn beside a part goes with it; a far title stays") {
	LayoutInput in;
	in.mode = TidyMode::Rearrange;
	// the backwards chain from above: the source on the right ends up on the left
	LayoutNode out = box(0, 0, 2, 2);  out.pins = { pin(0, 1, -1, 1, false) };
	LayoutNode gate = box(4, 3, 2, 2); gate.pins = { pin(4, 4, -1, 0, false), pin(6, 4, 1, 1, true) };
	LayoutNode src = box(8, -3, 2, 2); src.pins = { pin(10, -2, 1, 0, true) };
	LayoutNode capSrc = box(8, -0.5f, 2, 1);     // just above the source
	LayoutNode capOut = box(-3, 0.5f, 2.5f, 1);   // just left of the output
	LayoutNode title = box(-5, 12, 8, 2);         // far from everything
	in.nodes = { out, gate, src, capSrc, capOut, title };
	auto off = layoutGates(in);
	REQUIRE(off.size() == 6);
	CHECK(off[2].first != 0.0f);   // the source did move
	CHECK(off[3] == off[2]);
	CHECK(off[4] == off[0]);
	CHECK(off[5].first == 0.0f); CHECK(off[5].second == 0.0f);
	for (size_t i = 0; i < 5; i++)
		for (size_t j = i + 1; j < 5; j++) CHECK_FALSE(overlap(in.nodes[i], off[i], in.nodes[j], off[j]));
}

TEST_CASE("layout rearrange: parts are spaced for their captions") {
	LayoutInput in;
	in.mode = TidyMode::Rearrange;
	// two one-pin sources, each with a caption on top, feeding a gate's two
	// inputs a step apart: stacked edge to edge, B's caption would sit on A
	LayoutNode a = box(0, 4, 2, 1);    a.pins = { pin(2, 4.5f, 1, 0, true) };
	LayoutNode b = box(0, 1, 2, 1);    b.pins = { pin(2, 1.5f, 1, 1, true) };
	LayoutNode capA = box(0, 5.25f, 2, 1);
	LayoutNode capB = box(0, 2.25f, 2, 1);
	LayoutNode g = box(6, 0, 2, 3);    g.pins = { pin(6, 2, -1, 0, false), pin(6, 1, -1, 1, false), pin(8, 1.5f, 1, 2, true) };
	LayoutNode sink = box(11, 0.5f, 2, 2); sink.pins = { pin(11, 1.5f, -1, 2, false) };
	in.nodes = { a, b, capA, capB, g, sink };
	auto off = layoutGates(in);
	REQUIRE(off.size() == 6);
	CHECK(off[2] == off[0]);
	CHECK(off[3] == off[1]);
	for (size_t i = 0; i < in.nodes.size(); i++)
		for (size_t j = i + 1; j < in.nodes.size(); j++) CHECK_FALSE(overlap(in.nodes[i], off[i], in.nodes[j], off[j]));
	for (auto &o : off) { CHECK(onGrid(o.first)); CHECK(onGrid(o.second)); }
}

TEST_CASE("layout rearrange: a part with a caption still lines up with its column") {
	LayoutInput in;
	in.mode = TidyMode::Rearrange;
	// one source feeding two gates in a column, each gate feeding a sink; the
	// lower gate has a caption on its right
	LayoutNode src = box(0, 0, 2, 2);    src.pins = { pin(2, 1, 1, 0, true) };
	LayoutNode g1 = box(5, 2, 2, 2);     g1.pins = { pin(5, 3, -1, 0, false), pin(7, 3, 1, 1, true) };
	LayoutNode g2 = box(5, -2, 2, 2);    g2.pins = { pin(5, -1, -1, 0, false), pin(7, -1, 1, 2, true) };
	LayoutNode s1 = box(10, 2, 2, 2);    s1.pins = { pin(10, 3, -1, 1, false) };
	LayoutNode s2 = box(10, -2, 2, 2);   s2.pins = { pin(10, -1, -1, 2, false) };
	LayoutNode cap = box(7.25f, -2, 1.5f, 1);
	in.nodes = { src, g1, g2, s1, s2, cap };
	auto off = layoutGates(in);
	REQUIRE(off.size() == 6);
	CHECK(off[5] == off[2]);
	CHECK(in.nodes[1].l + off[1].first == doctest::Approx(in.nodes[2].l + off[2].first));
	for (size_t i = 0; i < in.nodes.size(); i++)
		for (size_t j = i + 1; j < in.nodes.size(); j++) CHECK_FALSE(overlap(in.nodes[i], off[i], in.nodes[j], off[j]));
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

TEST_CASE("layout rearrange: an output fed from far back sits right after its driver, a light stays last") {
	// a chain src -> g1 -> g2 -> g3 -> g4 -> end, and a label tapped off g1
	LayoutInput in;
	in.mode = TidyMode::Rearrange;
	LayoutNode src = box(0, 0, 2, 2); src.pins = { pin(2, 1, 1, 0, true) };
	in.nodes.push_back(src);
	for (int i = 0; i < 4; i++) {
		const float X = 4.0f + 4.0f * i;
		LayoutNode g = box(X, 0, 2, 2); g.pins = { pin(X, 1, -1, i, false), pin(X + 2, 1, 1, i + 1, true) };
		in.nodes.push_back(g);
	}
	LayoutNode end = box(20, 0, 2, 2); end.pins = { pin(20, 1, -1, 4, false) };
	LayoutNode tap = box(20, -3, 2, 2); tap.pins = { pin(20, -2, -1, 1, false) };   // drawn at the far end
	LayoutNode led = box(20, -6, 2, 2); led.indicator = true; led.pins = { pin(20, -5, -1, 1, false) };   // a light, also off g1
	in.nodes.push_back(end); in.nodes.push_back(tap); in.nodes.push_back(led);
	auto off = layoutGates(in);
	// the light still goes in the last column, with the end of the chain
	CHECK(left(in, off, 7) == doctest::Approx(left(in, off, 5)));
	// the tap shares g2's column, not the last one
	CHECK(left(in, off, 6) < left(in, off, 3));
	CHECK(left(in, off, 6) > left(in, off, 1));
	CHECK(left(in, off, 5) > left(in, off, 4));
	for (size_t i = 0; i < in.nodes.size(); i++)
		for (size_t j = i + 1; j < in.nodes.size(); j++) CHECK_FALSE(overlap(in.nodes[i], off[i], in.nodes[j], off[j]));
}

TEST_CASE("layout rearrange: a tie point hanging off a wire doesn't hold its gate where it was drawn") {
	// switch -> gate D -> gate G, G drawn well below D; G's output runs on to an
	// inverter and a light, and to a tie point hanging under the wire (its
	// pin on its top). G comes up level with D.
	LayoutInput in;
	in.mode = TidyMode::Rearrange;
	LayoutNode sw = box(0, -12, 2, 2);    sw.pins = { pin(2, -11, 1, 0, true) };
	LayoutNode d = box(4, 0, 2, 2);       d.pins = { pin(4, 1, -1, 0, false), pin(6, 1, 1, 1, true) };
	LayoutNode g = box(8, -12, 2, 2);     g.pins = { pin(8, -11, -1, 1, false), pin(10, -11, 1, 2, true) };
	LayoutNode inv = box(12, -12, 2, 2);  inv.pins = { pin(12, -11, -1, 2, false), pin(14, -11, 1, 3, true) };
	LayoutNode led = box(16, -12, 2, 2);  led.pins = { pin(16, -11, -1, 3, false) };
	LayoutNode tie = box(12, -15, 1, 1);  tie.pins = { { 12.5f, -14, 0, 1, 2, false } };
	in.nodes = { sw, d, g, inv, led, tie };
	auto off = layoutGates(in);
	CHECK(1 + off[1].second == doctest::Approx(-11 + off[2].second));
	for (size_t i = 0; i < in.nodes.size(); i++)
		for (size_t j = i + 1; j < in.nodes.size(); j++) CHECK_FALSE(overlap(in.nodes[i], off[i], in.nodes[j], off[j]));
}

TEST_CASE("layout rearrange: groups drawn side by side that come out long stack instead") {
	// two chains of six parts, each drawn folded into two rows of three,
	// the chains side by side and joined by one wire: laid out flat in
	// one row they would be far wider than drawn
	LayoutInput in;
	in.mode = TidyMode::Rearrange;
	for (int g = 0; g < 2; g++) {
		const float X = 14.0f * g;
		const int n0 = 10 * g;
		for (int i = 0; i < 6; i++) {
			const float x = X + 4.0f * (i % 3), yb = i < 3 ? 3.0f : 0.0f;
			LayoutNode p = box(x, yb, 2, 2);
			if (i > 0) p.pins.push_back(pin(x, yb + 1, -1, n0 + i - 1, false));
			if (i < 5) p.pins.push_back(pin(x + 2, yb + 1, 1, n0 + i, true));
			if (g == 0 && i == 5) p.pins.push_back(pin(x + 2, yb + 1, 1, 9, true));
			if (g == 1 && i == 0) p.pins.push_back(pin(x, yb + 1.5f, -1, 9, false));
			in.nodes.push_back(p);
		}
	}
	auto off = layoutGates(in);
	float l = 1e9f, r = -1e9f, b = 1e9f, t = -1e9f;
	for (size_t i = 0; i < in.nodes.size(); i++) {
		l = std::min(l, in.nodes[i].l + off[i].first); r = std::max(r, in.nodes[i].r + off[i].first);
		b = std::min(b, in.nodes[i].b + off[i].second); t = std::max(t, in.nodes[i].t + off[i].second);
	}
	CHECK((r - l) / (t - b) < 8.0f);
	// the first group (drawn on the left) on top, wholly above the second
	float lowA = 1e9f, highB = -1e9f;
	for (int i = 0; i < 6; i++) {
		lowA = std::min(lowA, in.nodes[i].b + off[i].second);
		highB = std::max(highB, top(in, off, 6 + i));
	}
	CHECK(lowA > highB);
	// left edges lined up, and only a small gap between the rows
	float leftA = 1e9f, leftB = 1e9f;
	for (int i = 0; i < 6; i++) { leftA = std::min(leftA, left(in, off, i)); leftB = std::min(leftB, left(in, off, 6 + i)); }
	CHECK(leftA == doctest::Approx(leftB));
	CHECK(lowA - highB < 3.5f);
	// each still reads left to right
	for (int i = 0; i + 1 < 6; i++) { CHECK(left(in, off, i) < left(in, off, i + 1)); CHECK(left(in, off, 6 + i) < left(in, off, 7 + i)); }
	for (size_t i = 0; i < in.nodes.size(); i++)
		for (size_t j = i + 1; j < in.nodes.size(); j++) CHECK_FALSE(overlap(in.nodes[i], off[i], in.nodes[j], off[j]));
}

TEST_CASE("layout rearrange: a tie point wired to a pin underneath a part sits under it") {
	// switch -> g1 -> g2 -> light, and a tie point drawn far below wired by
	// the pin on its top to a pin on g2's bottom: nothing level to line up
	// with, but it still comes up under g2 instead of staying where drawn
	LayoutInput in;
	in.mode = TidyMode::Rearrange;
	LayoutNode sw = box(0, 0, 2, 2);      sw.pins = { pin(2, 1, 1, 0, true) };
	LayoutNode g1 = box(4, 0, 2, 2);      g1.pins = { pin(4, 1, -1, 0, false), pin(6, 1, 1, 1, true) };
	LayoutNode g2 = box(8, 0, 2, 2);      g2.pins = { pin(8, 1, -1, 1, false), pin(10, 1, 1, 2, true), { 9, 0, 0, -1, 3, true } };
	LayoutNode led = box(12, 0, 2, 2);    led.pins = { pin(12, 1, -1, 2, false) };
	LayoutNode tie = box(12, -20, 1, 1);  tie.pins = { { 12.5f, -19, 0, 1, 3, false } };
	in.nodes = { sw, g1, g2, led, tie };
	auto off = layoutGates(in);
	const float g2bottom = 0 + off[2].second, tieTop = -19 + off[4].second;
	CHECK(tieTop < g2bottom);
	CHECK(g2bottom - tieTop <= 3.0f);
	for (size_t i = 0; i < in.nodes.size(); i++)
		for (size_t j = i + 1; j < in.nodes.size(); j++) CHECK_FALSE(overlap(in.nodes[i], off[i], in.nodes[j], off[j]));
}

TEST_CASE("layout rearrange: wires running straight across need no lane of their own") {
	// a part with six outputs wired straight across to a part with six inputs
	LayoutInput in;
	in.mode = TidyMode::Rearrange;
	LayoutNode a = box(0, 0, 2, 7), b = box(20, 0, 2, 7);
	for (int i = 0; i < 6; i++) { a.pins.push_back(pin(2, 1.0f + i, 1, i, true)); b.pins.push_back(pin(20, 1.0f + i, -1, i, false)); }
	LayoutNode c = box(0, -6, 2, 2); c.pins = { pin(2, -5, 1, 9, true) };
	LayoutNode d = box(20, -6, 2, 2); d.pins = { pin(20, -5, -1, 9, false) };
	in.nodes = { a, b, c, d };
	auto off = layoutGates(in);
	for (int i = 0; i < 6; i++) CHECK(1.0f + i + off[0].second == doctest::Approx(1.0f + i + off[1].second));
	// the old rule kept a lane per wire: 1.5 + 7 between the columns
	CHECK(left(in, off, 1) - (in.nodes[0].r + off[0].first) < 7.0f);
}

TEST_CASE("layout rearrange: the same input gives the same answer") {
	LayoutInput in = keypadInverters();
	auto a = layoutGates(in), b = layoutGates(in);
	CHECK(a == b);
}

TEST_CASE("layout keep: a label left behind the pin it feeds goes in front of it") {
	LayoutInput in;
	// a display with its input pin on the left (x = 10), and the label that
	// drives it sitting below, its output pin (x = 12) past the display's pin:
	// the wire would have to leave right, turn back and come round.
	LayoutNode disp = box(10, 0, 4, 6);    disp.pins = { pin(10, 3, -1, 0, false), pin(10, 5, -1, 1, false) };
	LayoutNode lab = box(8, -3, 4, 2);     lab.pins = { pin(12, -2, 1, 0, true) };
	LayoutNode other = box(0, 4, 4, 2);    other.pins = { pin(4, 5, 1, 1, true) };
	in.nodes = { disp, lab, other };
	auto off = layoutGates(in);
	REQUIRE(off.size() == 3);
	// the display stays; the label ends up left of the display's pin with room, level with it
	CHECK(off[0].first == doctest::Approx(0.0f));
	CHECK(12.0f + off[1].first <= 10.0f + off[0].first - 2.0f + 1e-3f);
	CHECK(-2.0f + off[1].second == doctest::Approx(3.0f + off[0].second));
	CHECK_FALSE(overlap(in.nodes[0], off[0], in.nodes[1], off[1]));
	for (auto &o : off) { CHECK(onGrid(o.first)); CHECK(onGrid(o.second)); }
}

TEST_CASE("layout keep: a label already in front of its pin, however close, is left alone") {
	LayoutInput in;
	LayoutNode disp = box(10, 0, 4, 6);    disp.pins = { pin(10, 3, -1, 0, false) };
	LayoutNode lab = box(5, -3, 4, 2);     lab.pins = { pin(9, -2, 1, 0, true) };   // 1 ahead, 5 off level
	in.nodes = { disp, lab };
	auto off = layoutGates(in);
	CHECK(off[1].first == doctest::Approx(0.0f));
	CHECK(off[1].second == doctest::Approx(0.0f));
}

TEST_CASE("layout keep: labels stacked beside a part in the wrong order trade places") {
	LayoutInput in;
	// a counter with outputs at y = 5 (net 0) and y = 3 (net 1); two labels
	// stacked to its right, the top one wired to the lower output
	LayoutNode ctr = box(0, 0, 6, 8);     ctr.pins = { pin(6, 5, 1, 0, true), pin(6, 3, 1, 1, true) };
	LayoutNode top = box(8, 4, 4, 2);     top.pins = { pin(8, 5, -1, 1, false) };
	LayoutNode bot = box(8, 2, 4, 2);     bot.pins = { pin(8, 3, -1, 0, false) };
	in.nodes = { ctr, top, bot };
	auto off = layoutGates(in);
	REQUIRE(off.size() == 3);
	// each label ends up level with its own output, in the other's old spot
	CHECK(5.0f + off[1].second == doctest::Approx(3.0f + off[0].second));
	CHECK(3.0f + off[2].second == doctest::Approx(5.0f + off[0].second));
	CHECK(off[1].first == doctest::Approx(off[2].first));
	CHECK_FALSE(overlap(in.nodes[1], off[1], in.nodes[2], off[2]));
}

TEST_CASE("layout keep: labels already in the order of the pins stay where they are") {
	LayoutInput in;
	LayoutNode ctr = box(0, 0, 6, 8);     ctr.pins = { pin(6, 5, 1, 0, true), pin(6, 3, 1, 1, true) };
	LayoutNode top = box(8, 4, 4, 2);     top.pins = { pin(8, 5, -1, 0, false) };
	LayoutNode bot = box(8, 2, 4, 2);     bot.pins = { pin(8, 3, -1, 1, false) };
	in.nodes = { ctr, top, bot };
	auto off = layoutGates(in);
	for (auto &o : off) { CHECK(o.first == doctest::Approx(0.0f)); CHECK(o.second == doctest::Approx(0.0f)); }
}

namespace {
LayoutPin upPin(float x, float y, int dy, int net, bool out) {
	LayoutPin p; p.x = x; p.y = y; p.dy = dy; p.net = net; p.output = out; return p;
}
}  // namespace

TEST_CASE("layout keep: tie points drawn in a row stay in their row") {
	LayoutInput in;
	// three tie points side by side, and a caption just under them whose
	// middle lines up with the last: lining the "stack" up used to put the
	// middle one on top of the last
	LayoutNode a = box(63.7f, -53, 0.6f, 2); a.pins = { upPin(64, -51, 1, 0, false) };
	LayoutNode b = box(64.7f, -53, 0.6f, 2); b.pins = { upPin(65, -51, 1, 1, false) };
	LayoutNode c = box(65.7f, -53, 0.6f, 2); c.pins = { upPin(66, -51, 1, 2, false) };
	LayoutNode cap = box(64.2f, -54.6f, 3.6f, 1.2f);
	in.nodes = { a, b, c, cap };
	auto off = layoutGates(in);
	for (size_t i = 0; i < 3; i++) {
		CHECK(off[i].second == doctest::Approx(0.0f));
		for (size_t j = i + 1; j < 4; j++) CHECK_FALSE(overlap(in.nodes[i], off[i], in.nodes[j], off[j]));
	}
}

TEST_CASE("layout keep: a part drawn turned round, dropped on what feeds it, goes back beside it, level") {
	LayoutInput in;
	// a decoder with outputs on its left, and an OR drawn turned round (inputs
	// on its right) dropped over its lower corner, 6 below level
	LayoutNode dec = box(75, -49, 6, 9);
	dec.pins = { pin(75, -43, -1, 0, true), pin(75, -45, -1, 1, true), pin(75, -47, -1, 2, true), pin(75, -49, -1, 3, true) };
	LayoutNode orr = box(72.5f, -56, 7, 8);
	orr.pins = { pin(79.5f, -49, 1, 0, false), pin(79.5f, -51, 1, 1, false), pin(79.5f, -53, 1, 2, false), pin(79.5f, -55, 1, 3, false),
	             pin(72.5f, -52, -1, 4, true) };
	in.nodes = { dec, orr };
	auto off = layoutGates(in);
	CHECK_FALSE(overlap(in.nodes[0], off[0], in.nodes[1], off[1]));
	// left of the decoder with room for the wires, each input level with its driver
	CHECK(79.5f + off[1].first <= 75.0f + off[0].first - 2.0f + 1e-3f);
	CHECK(-49.0f + off[1].second == doctest::Approx(-43.0f + off[0].second));
}

TEST_CASE("layout keep: a keypad dropped onto an adder's inputs moves off them; the adder stays") {
	LayoutInput in;
	LayoutNode add = box(88, 2, 16, 8);
	for (int k = 0; k < 4; k++) add.pins.push_back(upPin(94.0f - k, 10, 1, k, false));
	for (int k = 0; k < 4; k++) add.pins.push_back(upPin(101.0f - k, 10, 1, 4 + k, false));
	LayoutNode k1 = box(80, 23, 9, 12), k2 = box(86, 5, 9, 12);
	for (int k = 0; k < 4; k++) k1.pins.push_back(pin(89, 26.0f + 2 * k, 1, k, true));
	for (int k = 0; k < 4; k++) k2.pins.push_back(pin(95, 8.0f + 2 * k, 1, 4 + k, true));
	in.nodes = { add, k1, k2 };
	auto off = layoutGates(in);
	CHECK(off[0].first == doctest::Approx(0.0f));
	CHECK(off[0].second == doctest::Approx(0.0f));
	for (size_t i = 0; i < 3; i++)
		for (size_t j = i + 1; j < 3; j++) CHECK_FALSE(overlap(in.nodes[i], off[i], in.nodes[j], off[j]));
	// no adder input left under the keypad without room for its wire
	const float l = 86 + off[2].first, r = 95 + off[2].first, b = 5 + off[2].second;
	for (const LayoutPin &p : add.pins) CHECK((p.x <= l - 1.5f + 1e-3f || p.x >= r + 1.5f - 1e-3f || b >= p.y + 3.0f - 1e-3f));
}

TEST_CASE("layout keep: a gate dropped onto the next part's inputs keeps its place and gets a channel") {
	LayoutInput in;
	LayoutNode gate = box(6, -11, 6, 4);
	gate.pins = { pin(6, -8, -1, 0, false), pin(6, -10, -1, 1, false), pin(12, -9, 1, 2, true) };
	LayoutNode add = box(-1, -18, 16, 8);
	for (int k = 0; k < 4; k++) add.pins.push_back(upPin(7.0f + k, -10, 1, 3 + k, false));
	in.nodes = { gate, add };
	auto off = layoutGates(in);
	CHECK(off[0].first == doctest::Approx(0.0f));
	CHECK(off[0].second == doctest::Approx(0.0f));
	CHECK(off[1].first == doctest::Approx(0.0f));
	// room under the gate for the four wires to reach their pins
	CHECK(-11.0f - (-10.0f + off[1].second) >= 1.0f + 4 * 0.5f - 1e-3f);
}

TEST_CASE("layout keep: a keypad dropped on the adder it feeds goes back above it, not beside it") {
	LayoutInput in;
	// as above: the second keypad lands on the adder's inputs. Moved off them
	// to its side, its lower wires would come in under the pins they feed and
	// loop back up; there's room above the adder, under the first keypad.
	LayoutNode add = box(88, 2, 16, 8);
	for (int k = 0; k < 4; k++) add.pins.push_back(upPin(94.0f - k, 10, 1, k, false));
	for (int k = 0; k < 4; k++) add.pins.push_back(upPin(101.0f - k, 10, 1, 4 + k, false));
	LayoutNode k1 = box(80, 23, 9, 12), k2 = box(86, 5, 9, 12);
	for (int k = 0; k < 4; k++) k1.pins.push_back(pin(89, 26.0f + 2 * k, 1, k, true));
	for (int k = 0; k < 4; k++) k2.pins.push_back(pin(95, 8.0f + 2 * k, 1, 4 + k, true));
	in.nodes = { add, k1, k2 };
	auto off = layoutGates(in);
	for (size_t i = 0; i < 3; i++)
		for (size_t j = i + 1; j < 3; j++) CHECK_FALSE(overlap(in.nodes[i], off[i], in.nodes[j], off[j]));
	// every pin of the keypad above the adder's inputs, and left of them
	for (const LayoutPin &p : k2.pins) {
		CHECK(p.y + off[2].second >= 10.0f + 1.0f - 1e-3f);
		CHECK(p.x + off[2].first <= 91.0f - 1.0f + 1e-3f);
	}
	CHECK(off[1].first == doctest::Approx(0.0f));
	CHECK(off[1].second == doctest::Approx(0.0f));
}

namespace {
// A gate turned round (inputs on its right) just left of an AND gate fed from
// above: the wires into both have to run down the gap between them.
LayoutInput channelCircuit(float gap, bool second) {
	LayoutInput in;
	LayoutNode top = box(0, 10, 16, 8);
	for (int k = 0; k < 4; k++) top.pins.push_back(upPin(6.5f + k, 10, -1, k == 3 ? 4 : k, true));
	LayoutNode org = box(0, second ? -4 : 0, 6, second ? 10 : 6);
	org.pins = { pin(6, 5, 1, 3, false), pin(6, 3, 1, 2, false), pin(6, 1, 1, 4, false), pin(0, 3, -1, 5, true) };
	LayoutNode g1 = box(6 + gap, 2, 6, 4);
	g1.pins = { pin(6 + gap, 5, -1, 0, false), pin(6 + gap, 3, -1, 1, false), pin(12 + gap, 4, 1, 3, true) };
	LayoutNode lab = box(-14, 2, 2, 2); lab.pins = { pin(-12, 3, 1, 5, false) };
	in.nodes = { top, org, g1, lab };
	if (second) {
		LayoutNode g2 = box(6 + gap, -3, 6, 4);
		g2.pins = { pin(6 + gap, 0, -1, 6, false), pin(6 + gap, -2, -1, 7, false) };
		LayoutNode src = box(-14, -4, 2, 4); src.pins = { pin(-12, -1, 1, 6, true), pin(-12, -3, 1, 7, true) };
		in.nodes.push_back(g2); in.nodes.push_back(src);
	}
	return in;
}
}  // namespace

TEST_CASE("layout keep: a gap between parts side by side opens up for the wires that turn in it") {
	LayoutInput in = channelCircuit(2, false);
	auto off = layoutGates(in);
	// five wires turn between the two gates: a lane and a quarter each, and one to spare
	const float gap = (in.nodes[2].l + off[2].first) - (in.nodes[1].r + off[1].first);
	CHECK(gap >= 6 * 1.25f - 0.5f - 1e-3f);
	// only sideways, and nothing lands on anything
	for (size_t i = 0; i < in.nodes.size(); i++) {
		CHECK(off[i].second == doctest::Approx(0.0f));
		for (size_t j = i + 1; j < in.nodes.size(); j++) CHECK_FALSE(overlap(in.nodes[i], off[i], in.nodes[j], off[j]));
	}
}

TEST_CASE("layout keep: a gap a step short of its wires is left as drawn") {
	LayoutInput in = channelCircuit(7.0f, false);
	auto off = layoutGates(in);
	for (auto &o : off) { CHECK(o.first == doctest::Approx(0.0f)); CHECK(o.second == doctest::Approx(0.0f)); }
}

TEST_CASE("layout keep: gates lined up in a column step aside together") {
	// the gate on the left is the bigger part here, so the column moves
	LayoutInput in = channelCircuit(2, true);
	auto off = layoutGates(in);
	CHECK(off[2].first > 0.0f);
	CHECK(off[4].first == doctest::Approx(off[2].first));
	CHECK(off[2].second == doctest::Approx(0.0f));
	CHECK(off[4].second == doctest::Approx(0.0f));
	for (size_t i = 0; i < in.nodes.size(); i++)
		for (size_t j = i + 1; j < in.nodes.size(); j++) CHECK_FALSE(overlap(in.nodes[i], off[i], in.nodes[j], off[j]));
}

TEST_CASE("layout keep: a page packed evenly with gates is not blown up to make room for wires") {
	// 20 columns half a unit apart, 15 rows a unit apart; each gate sends three
	// wires out of its right side to the gate one row down in the next column.
	// Every gap side by side is short of its wires: nowhere is locally jammed,
	// so the page keeps its size, and the call returns.
	const int C = 20, R = 15;
	const float w = 4, h = 3;
	LayoutInput in;
	for (int c = 0; c < C; c++)
		for (int r = 0; r < R; r++) in.nodes.push_back(box(c * (w + 0.5f), -r * (h + 1.0f) - h, w, h));
	auto id = [&](int c, int r) { return c * R + r; };
	int net = 0;
	for (int c = 0; c + 1 < C; c++)
		for (int r = 0; r < R; r++)
			for (int k = 0; k < 3; k++, net++) {
				LayoutNode &a = in.nodes[id(c, r)], &b = in.nodes[id(c + 1, (r + 1) % R)];
				a.pins.push_back(pin(a.r, a.b + 0.5f + k, 1, net, true));
				b.pins.push_back(pin(b.l, b.b + 0.5f + k, -1, net, false));
			}
	auto off = layoutGates(in);
	REQUIRE(off.size() == in.nodes.size());
	float most = 0.0f;
	for (auto &o : off) most = std::max(most, std::fabs(o.first) + std::fabs(o.second));
	CHECK(most <= 5.0f);
	int hits = 0;
	for (size_t i = 0; i < in.nodes.size(); i++)
		for (size_t j = i + 1; j < in.nodes.size(); j++) hits += overlap(in.nodes[i], off[i], in.nodes[j], off[j]);
	CHECK(hits == 0);
}
