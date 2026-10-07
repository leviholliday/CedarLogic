// Drawing on the circuit and student notes: every vector in
// tests/fixtures/drawing (copied from the website's scripts/fixtures/drawing,
// made by drawref.py, the reference the spec defers to) against format/.

#include <doctest/doctest.h>

#include "circuit_file_io.hpp"
#include "ink.hpp"
#include "migrate.hpp"
#include "mini_json.hpp"
#include "sexpr.hpp"

#include <cmath>
#include <functional>
#include <fstream>
#include <sstream>
#include <string>

using namespace cl;

namespace {

const std::string kDir = std::string(FIXTURES_DIR) + "/drawing/";

std::string slurp(const std::string &path) {
	std::ifstream in(path, std::ios::binary);
	REQUIRE_MESSAGE(in.good(), "missing fixture " << path);
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

minijson::Value vectors(const std::string &name) { return minijson::parse(slurp(kDir + name)); }

std::vector<std::int32_t> centiOf(const minijson::Value &pairs) {
	std::vector<std::int32_t> out;
	for (const auto &p : pairs.arr) {
		out.push_back((std::int32_t)p[0].num);
		out.push_back((std::int32_t)p[1].num);
	}
	return out;
}

std::string kindOf(const std::function<void()> &f) {
	try {
		f();
	} catch (const ink::InkError &e) {
		return e.kind;
	}
	return "";
}

// A string atom as the writer writes it.
std::string quoted(const std::string &s) {
	std::string w = writeSexpr(SNode::str(s));
	return w.substr(0, w.size() - 1);
}

} // namespace

TEST_CASE("ink: VLQ vectors") {
	const minijson::Value v = vectors("ink-vlq.json");
	CHECK(v["alphabet"].str == ink::kAlpha);
	for (const auto &c : v["good"].arr) {
		CHECK(ink::encodeVlq((long long)c["value"].num) == c["text"].str);
		const std::vector<long long> d = ink::decodeVlqAll(c["text"].str);
		REQUIRE(d.size() == 1);
		CHECK(d[0] == (long long)c["value"].num);
	}
	for (const auto &c : v["lenient"].arr) {
		const std::vector<long long> d = ink::decodeVlqAll(c["text"].str);
		REQUIRE(d.size() == c["values"].size());
		for (size_t i = 0; i < d.size(); i++) CHECK(d[i] == (long long)c["values"][i].num);
	}
	for (const auto &c : v["bad"].arr)
		CHECK_MESSAGE(kindOf([&] { ink::decodeVlqAll(c["text"].str); }) == c["error"].str, c["text"].str);
}

TEST_CASE("ink: points and pressure vectors") {
	const minijson::Value v = vectors("ink-points.json");
	for (const auto &c : v["cases"].arr) {
		std::vector<std::int32_t> centi;
		for (const auto &p : c["world"].arr) {
			centi.push_back(ink::quantize(p[0].num));
			centi.push_back(ink::quantize(p[1].num));
		}
		CHECK(centi == centiOf(c["centi"]));
		CHECK(ink::encodePoints(centi) == c["text"].str);
		CHECK(ink::decodePoints(c["text"].str) == centiOf(c["centi"]));
	}
	for (const auto &c : v["bad"].arr)
		CHECK_MESSAGE(kindOf([&] { ink::decodePoints(c["text"].str); }) == c["error"].str, c["error"].str);
	for (const auto &c : v["pressure"].arr) {
		std::vector<double> ps;
		for (const auto &p : c["pressure"].arr) ps.push_back(p.num);
		CHECK(ink::encodePressure(ps) == c["text"].str);
		CHECK(ink::pressureOk(c["text"].str, ps.size()));
	}
}

TEST_CASE("ink: the simplifier and making a stroke") {
	for (const auto &c : vectors("ink-simplify.json").arr) {
		std::vector<double> xy;
		for (const auto &p : c["points"].arr) { xy.push_back(p[0].num); xy.push_back(p[1].num); }
		const std::vector<size_t> kept = ink::simplify(xy, c["eps"].num);
		std::vector<size_t> want;
		for (const auto &k : c["kept"].arr) want.push_back((size_t)k.num);
		CHECK_MESSAGE(kept == want, c["name"].str);
		const InkStroke st = ink::makeStroke("pen", "ink", 0.25, xy, {}, c["eps"].num);
		CHECK_MESSAGE(ink::encodePoints(st.xy) == c["stroke"]["text"].str, c["name"].str);
		CHECK(st.xy == centiOf(c["stroke"]["centi"]));
		CHECK(st.widthCenti == 25);
	}
}

TEST_CASE("ink: reading and rewriting (stroke ...)") {
	for (const auto &c : vectors("ink-strokes.json").arr) {
		const SNode node = parseSexpr(c["text"].str);
		if (c.has("error")) {
			CHECK_MESSAGE(kindOf([&] { ink::readStroke(node); }) == c["error"].str, c["name"].str);
			continue;
		}
		const InkStroke st = ink::readStroke(node);
		const minijson::Value &r = c["result"];
		CHECK_MESSAGE(st.tool == r["tool"].str, c["name"].str);
		CHECK_MESSAGE(st.color == r["color"].str, c["name"].str);
		CHECK_MESSAGE(st.widthCenti == (int)std::lround(r["width"].num * 100), c["name"].str);
		CHECK_MESSAGE(st.xy == centiOf(r["centi"]), c["name"].str);
		CHECK_MESSAGE(st.pressure == (r["pressure"].isNull() ? std::string() : r["pressure"].str), c["name"].str);
		const std::string w = writeSexpr(ink::strokeNode(st));
		CHECK_MESSAGE(w.substr(0, w.size() - 1) == c["rewritten"].str, c["name"].str);
	}
}

TEST_CASE("ink: notes normalize, and are written only when worth it") {
	for (const auto &c : vectors("notes.json").arr) {
		if (c.has("input_repeat")) {
			const std::string big((size_t)c["input_repeat"][1].num, c["input_repeat"][0].str[0]);
			CHECK(ink::scalarCount(ink::normalizeNotes(big)) == (size_t)c["normalized_length"].num);
			continue;
		}
		const std::string norm = ink::normalizeNotes(c["input"].str);
		CHECK_MESSAGE(norm == c["normalized"].str, c["name"].str);
		CHECK_MESSAGE(ink::notesWorthWriting(norm) == c["written"].b, c["name"].str);
		if (c.has("node")) {
			CHECK_MESSAGE("(notes " + quoted(norm) + ")" == c["node"].str, c["name"].str);
			// And it reads back the same from a file.
			const CircuitFile cf = readCircuitFile("(cedarlogic (version 3) (generator \"\") " + c["node"].str + ")");
			CHECK(cf.notes == norm);
		}
	}
	CHECK(ink::normalizeNotes(std::string(30000, 'y'), ink::kWriteNotesChars).size() == 20000);
}

TEST_CASE("ink: every sample reads as expected and writes the same bytes again") {
	const minijson::Value exp = vectors("expected.json");
	for (const auto &entry : exp.obj) {
		const std::string &name = entry.first;
		const minijson::Value &e = entry.second;
		const std::string text = slurp(kDir + "cdl/" + name + ".cdl");
		INFO(name);
		if (e.has("refusedByEveryApp")) {
			CHECK(text.find("<version>") != std::string::npos);
			continue;
		}
		CHECK(text.find("<version>") == std::string::npos);
		const LoadResult lr = loadCircuit(text);
		const CircuitFile &cf = lr.file;
		CHECK(cf.notes == e["notes"].str);
		CHECK(cf.inkHidden == e["hidden"].b);
		std::vector<int> pagesWithInk;
		for (const Page &pg : cf.pages) if (!pg.ink.empty()) pagesWithInk.push_back(pg.index);
		REQUIRE(pagesWithInk.size() == e["pages"].size());
		for (size_t k = 0; k < e["pages"].size(); k++) {
			const minijson::Value &ep = e["pages"][k];
			const Page *pg = nullptr;
			for (const Page &p : cf.pages) if (p.index == (int)ep["index"].num) pg = &p;
			REQUIRE(pg != nullptr);
			CHECK(pg->ink.readOnly() == ep["readOnly"].b);
			REQUIRE(pg->ink.foreign.size() == ep["foreign"].size());
			for (size_t f = 0; f < pg->ink.foreign.size(); f++)
				CHECK(pg->ink.foreign[f] == parseSexpr(ep["foreign"][f].str));
			REQUIRE(pg->ink.strokes.size() == ep["strokes"].size());
			for (size_t s = 0; s < pg->ink.strokes.size(); s++) {
				const InkStroke &st = pg->ink.strokes[s];
				const minijson::Value &es = ep["strokes"][s];
				CHECK(st.tool == es["tool"].str);
				CHECK(st.color == es["color"].str);
				CHECK(st.widthCenti == (int)std::lround(es["width"].num * 100));
				CHECK(st.xy == centiOf(es["centi"]));
				CHECK(st.pressure == (es["pressure"].isNull() ? std::string() : es["pressure"].str));
			}
		}
		// The reference's notice is "drawing: N left out"; ours says it in words.
		int dropped = 0;
		for (const auto &n : e["notices"].arr) dropped += std::atoi(n.str.c_str() + std::string("drawing: ").size());
		CHECK(cf.inkDropped == dropped);
		bool warned = false;
		for (const MigrationNotice &n : lr.notices)
			if (n.summary.find("couldn't be read and w") != std::string::npos) warned = true;
		CHECK(warned == (dropped > 0));
		const std::string rewritten = writeCircuitFile(cf);
		if (e["rewriteSame"].b) CHECK(rewritten == text);
		else CHECK(rewritten == slurp(kDir + "cdl/" + e["rewrite"].str));
	}
}

TEST_CASE("ink: a newer drawing makes a read-only page, said once") {
	const LoadResult lr = loadCircuit(slurp(kDir + "cdl/newer-drawing.cdl"));
	int notes = 0;
	for (const MigrationNotice &n : lr.notices)
		if (n.summary.find("newer CedarLogic") != std::string::npos) { notes++; CHECK(n.severity == Severity::Info); }
	CHECK(notes == 1);
}

TEST_CASE("ink: no string can put <version> in a file's bytes") {
	for (const std::string &s : { std::string("<version>9.0</version>"), std::string("x < y"), std::string("<<version>"),
	                              std::string("<\\version>"), std::string("<\"q"), std::string("a<\xC3\xA9") }) {
		const std::string w = writeSexpr(SNode::str(s));
		CHECK(w.find("<version") == std::string::npos);
		CHECK(w.find("</version") == std::string::npos);
		CHECK(parseSexpr(w).text == s);
	}
	CHECK(quoted("x < y") == "\"x <\\ y\"");
	CHECK(quoted("<version>9.0</version>") == "\"<\\version>9.0<\\/version>\"");
	// A label with one, through the whole format.
	CircuitFile cf;
	cf.generator = "test";
	Page pg;
	GateInstance g;
	g.uuid = "1";
	g.libName = "AA_LABEL";
	g.params = { { "LABEL_TEXT", "<version>9.0</version>", true } };
	pg.gates.push_back(g);
	cf.pages.push_back(pg);
	cf.notes = "see <version>99.0</version>";
	const std::string t = writeCircuitFile(cf);
	CHECK(t.find("<version>") == std::string::npos);
	const CircuitFile back = readCircuitFile(t);
	CHECK(back == cf);
}

TEST_CASE("ink: a drawing and notes round-trip through the model") {
	CircuitFile cf;
	cf.generator = "test";
	Page p0, p1;
	p0.index = 0;
	p1.index = 1;
	std::vector<double> xy = { 0, 0, 0.5, 0.52, 1.0, 0.98, 1.5, 1.6, 2.2, 1.7 };
	p0.ink.strokes.push_back(ink::makeStroke("pen", "red", 0.25, xy, { 0.2, 0.4, 0.6, 0.8, 1 }, 0.01));
	p0.ink.strokes.push_back(ink::makeStroke("highlighter", "yellow", 1.2, { 3, -3 }, {}, 0.01));
	cf.pages = { p0, p1 };
	cf.notes = "Two lines\nand a tab\there";
	cf.inkHidden = true;
	const std::string t = writeCircuitFile(cf);
	CHECK(t.find("(show-drawing no)") != std::string::npos);
	const CircuitFile back = readCircuitFile(t);
	CHECK(back == cf);
	CHECK(writeCircuitFile(back) == t);
	// Hidden with nothing to hide: not written.
	cf.pages[0].ink.strokes.clear();
	CHECK(writeCircuitFile(cf).find("show-drawing") == std::string::npos);
	// Only whitespace in the notes: not written.
	cf.notes = " \n\t";
	CHECK(writeCircuitFile(cf).find("(notes") == std::string::npos);
}
