// Headless tests for the typed command serialization module. Two guarantees:
//   1. Golden format -- emit() produces the exact byte string the command
//      classes historically wrote by hand, so delegating to it changes nothing
//      on the clipboard/undo wire.
//   2. Round trip -- parse(emit(x)) == x for values that serialize exactly.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "cmdSerialize.h"

using namespace cmdser;

TEST_CASE("emit matches the historical hand-written formats (golden)") {
	CHECK(emit(CreateGate{5, "AND", 1.5f, -2.0f}) == "creategate 5 AND 1.5 -2");
	CHECK(emit(CreateGate{0, "BE_DECODER_3x8", 0.0f, 0.0f}) ==
	      "creategate 0 BE_DECODER_3x8 0 0");
	CHECK(emit(ConnectWire{3, 7, "IN_0"}) == "connectwire 3 7 IN_0");
	CHECK(emit(MoveGate{2, 1.0f, 2.0f, 3.0f, 4.0f}) == "movegate 2 1 2 3 4");
	CHECK(emit(MoveGate{9, -1.5f, 0.0f, 2.25f, -4.0f}) ==
	      "movegate 9 -1.5 0 2.25 -4");
	CHECK(emit(DisconnectWire{5, 9, "IN_0"}) == "disconnectwire 5 9 IN_0");
	CHECK(emit(CreateWire{{3, 4}, {3, 10, "OUT_0"}, {4, 11, "IN_1"}}) ==
	      "createwire 3 4 connectwire 3 10 OUT_0 connectwire 4 11 IN_1");
}

TEST_CASE("SetParams: sorted keys, per-entry tabs, space-bearing values") {
	// gid, "<#gui>,<#logic>", then "<name> <value>\t" per entry (GUI first). The
	// maps are std::map, so entries emit in sorted-key order (MIRROR < angle).
	SetParams sp{7, {{"angle", "90"}, {"MIRROR", "false"}}, {{"INPUT_BITS", "3"}}};
	CHECK(emit(sp) == "setparams 7 2,1 MIRROR false\tangle 90\tINPUT_BITS 3\t");

	SetParams back;
	REQUIRE(parse(emit(sp), back));
	CHECK(back.gid == 7);
	CHECK(back.guiParams == sp.guiParams);
	CHECK(back.logicParams == sp.logicParams);

	// A value with spaces (e.g. a label) survives: a param ends at its tab, not
	// the first space.
	SetParams label{3, {{"text", "R1 & C2 (load)"}}, {}};
	CHECK(emit(label) == "setparams 3 1,0 text R1 & C2 (load)\t");
	SetParams label2;
	REQUIRE(parse(emit(label), label2));
	CHECK(label2.guiParams["text"] == "R1 & C2 (load)");
}

TEST_CASE("MoveWire: nested per-segment records round-trip") {
	// A vertical segment with a connection and an intersection, then a plain
	// horizontal segment. Segments emit in order; the format ends with " done ".
	MoveWire mw;
	mw.wid = 5;
	MoveWireSeg s0;
	s0.id = 0; s0.vertical = true; s0.bx = 1; s0.by = 2; s0.ex = 1; s0.ey = 4;
	s0.connections = { { 10, "OUT_0" } };
	s0.intersects = { { 3.0f, 1 } };
	MoveWireSeg s1;
	s1.id = 1; s1.vertical = false; s1.bx = 1; s1.by = 4; s1.ex = 5; s1.ey = 4;
	mw.segments = { s0, s1 };

	CHECK(emit(mw) ==
	      "movewire 5 vsegment 0 1,2,1,4 connection 10 OUT_0 isect 3 1 hsegment 1 1,4,5,4  done ");

	MoveWire back;
	REQUIRE(parse(emit(mw), back));
	CHECK(back.wid == 5);
	REQUIRE(back.segments.size() == 2);
	CHECK(back.segments[0].id == 0);
	CHECK(back.segments[0].vertical == true);
	CHECK(back.segments[0].ey == doctest::Approx(4));
	REQUIRE(back.segments[0].connections.size() == 1);
	CHECK(back.segments[0].connections[0].first == 10);
	CHECK(back.segments[0].connections[0].second == "OUT_0");
	REQUIRE(back.segments[0].intersects.size() == 1);
	CHECK(back.segments[0].intersects[0].first == doctest::Approx(3));
	CHECK(back.segments[0].intersects[0].second == 1);
	CHECK(back.segments[1].id == 1);
	CHECK(back.segments[1].vertical == false);
}

TEST_CASE("parse consumes the leading keyword and fields") {
	CreateGate cg;
	REQUIRE(parse("creategate 12 OR 3.5 -1", cg));
	CHECK(cg.gid == 12);
	CHECK(cg.gateType == "OR");
	CHECK(cg.x == doctest::Approx(3.5));
	CHECK(cg.y == doctest::Approx(-1.0));

	ConnectWire cw;
	REQUIRE(parse("connectwire 100 200 OUT_3", cw));
	CHECK(cw.wireId == 100);
	CHECK(cw.gateId == 200);
	CHECK(cw.hotspot == "OUT_3");
}

TEST_CASE("a gate name with spaces round-trips") {
	CreateGate cg{9, "3x8 Decoder Chip", 22.5f, 26.5f};
	CreateGate back;
	REQUIRE(parse(emit(cg), back));
	CHECK(back.gid == 9);
	CHECK(back.gateType == "3x8 Decoder Chip");
	CHECK(back.x == doctest::Approx(22.5));
	CHECK(back.y == doctest::Approx(26.5));
}

TEST_CASE("keyword returns the leading token the dispatcher matches on") {
	CHECK(keyword("creategate 5 AND 1 2") == "creategate");
	CHECK(keyword("connectwire 3 7 IN_0") == "connectwire");
	CHECK(keyword("movegate 2 1 2 3 4") == "movegate");
	CHECK(keyword("") == "");
}

TEST_CASE("parse rejects a mismatched keyword") {
	CreateGate cg;
	CHECK_FALSE(parse("connectwire 1 2 IN_0", cg));
	ConnectWire cw;
	CHECK_FALSE(parse("movegate 1 2 3 4 5", cw));
}

TEST_CASE("round trip is stable for exactly-representable values") {
	CreateGate cg{42, "XOR", 2.5f, -8.0f};
	CreateGate cg2;
	REQUIRE(parse(emit(cg), cg2));
	CHECK(cg2.gid == cg.gid);
	CHECK(cg2.gateType == cg.gateType);
	CHECK(cg2.x == doctest::Approx(cg.x));
	CHECK(cg2.y == doctest::Approx(cg.y));

	ConnectWire cw{7, 13, "IN_2"};
	ConnectWire cw2;
	REQUIRE(parse(emit(cw), cw2));
	CHECK(cw2.wireId == cw.wireId);
	CHECK(cw2.gateId == cw.gateId);
	CHECK(cw2.hotspot == cw.hotspot);
}

TEST_CASE("createwire round-trips its id list and both connections") {
	// The id list is variable length and ends where the first connectwire begins.
	CreateWire cw{{7, 8, 9}, {7, 20, "OUT_0"}, {9, 21, "IN_3"}};
	CreateWire out;
	REQUIRE(parse(emit(cw), out));
	CHECK(out.wireIds == cw.wireIds);
	CHECK(out.conn1.wireId == cw.conn1.wireId);
	CHECK(out.conn1.gateId == cw.conn1.gateId);
	CHECK(out.conn1.hotspot == cw.conn1.hotspot);
	CHECK(out.conn2.wireId == cw.conn2.wireId);
	CHECK(out.conn2.gateId == cw.conn2.gateId);
	CHECK(out.conn2.hotspot == cw.conn2.hotspot);
}

TEST_CASE("SetParams: a value's tabs, newlines and backslashes come back exactly") {
	// A label with two lines cut the copied line in two, and the half left threw
	// out_of_range when read. Values now escape \ tab newline and CR.
	const std::string text = "two\nlines\twith a tab, C:\\path\\new \\ and \r";
	SetParams sp{3, {{"LABEL_TEXT", text}}, {{"JUNCTION_ID", "a\\b"}}};
	const std::string line = emit(sp);
	CHECK(line.find('\n') == std::string::npos);
	CHECK(line == "setparams 3 1,1 LABEL_TEXT two\\nlines\\twith a tab, C:\\\\path\\\\new \\\\ and \\r\t"
	              "JUNCTION_ID a\\\\b\t");
	SetParams back;
	REQUIRE(parse(line, back));
	CHECK(back.guiParams["LABEL_TEXT"] == text);
	CHECK(back.logicParams["JUNCTION_ID"] == "a\\b");

	// A backslash not starting an escape reads as itself (as before escapes).
	SetParams old;
	REQUIRE(parse("setparams 3 1,0 LABEL_TEXT C:\\x\t", old));
	CHECK(old.guiParams["LABEL_TEXT"] == "C:\\x");
}

TEST_CASE("damaged lines read without throwing, hanging or running past their end") {
	// Counts far beyond what the line holds: stop at its end.
	SetParams sp;
	REQUIRE(parse("setparams 3 4000000000,4000000000 A", sp));
	CHECK(sp.guiParams.size() == 1);
	CHECK(sp.guiParams["A"] == "");
	SetParams cut;
	REQUIRE(parse("setparams 3 1,0 LABEL_TEXT", cut));
	CHECK(cut.guiParams["LABEL_TEXT"] == "");

	// A movewire cut after "connection" or "isect" looped for ever.
	MoveWire mw;
	REQUIRE(parse("movewire 1 hsegment 0 0,0,1,0 connection", mw));
	CHECK(mw.segments.size() == 1);
	CHECK(mw.segments[0].connections.empty());
	MoveWire mw2;
	REQUIRE(parse("movewire 1 vsegment 0 0,0,0,1 isect 0.5", mw2));
	CHECK(mw2.segments.size() == 1);
	MoveWire mw3;
	REQUIRE(parse("movewire 1 hsegment", mw3));
	CHECK(mw3.segments.size() <= 1);
}
