// Headless check of the templates that come from the website's examples
// (App/TemplatesExamples.swift), every page of each, in the engine, one
// Step Clock at a time -- the same as the website's
// scripts/test_sim_examples2.mjs: the J-K sets, resets, toggles and holds
// (the part, and master-slave from NAND gates); the D takes D only on the
// clock (the part, and two latches from gates); the T toggles (a J-K, and a
// D with an XOR); the 4-bit register loads and holds (the counting
// register, and four D flip-flops with clock enable); the 3-bit counter
// counts 0 to 7 (the counting register, and a ripple counter); the 4-bit
// adder adds (the part, and four 1-bit full adders); the decoder's and the
// multiplexer's truth tables (the parts, and gates); the comparator, parity
// generator, edge detector and SR latch with enable. Also each two-page
// template's page names.
//   examples_check <cl_gatedefs.xml> <dir with template-builtin-*.cdl>
// (`CedarLogic --render-ui <dir>` writes them.)
#include "CedarCore.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

static int failures = 0, checks = 0;
static void check(bool ok, const std::string& what) {
	checks++;
	if (!ok) failures++;
	printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
}

// One page of a template, opened fresh, with its switches found by the
// labels beside them (each switch's state kept here: a click flips it).
struct Page {
	CLDocument* doc = nullptr;
	int page = 0;
	std::string name;
	std::map<std::string, std::pair<double, double>> at;
	std::map<std::string, int> on;

	void steps(int n) { for (int i = 0; i < n; i++) cl_document_step(doc); }
	void set(const std::string& s, int v) {
		auto it = at.find(s);
		if (it == at.end()) { check(false, name + ": no switch " + s); return; }
		if (on[s] == v) return;
		if (!cl_document_click(doc, page, it->second.first, it->second.second)) check(false, name + ": switch " + s + " didn't click");
		on[s] = v;
		steps(12);
	}
	void step() { if (!cl_document_clock_step(doc, page)) check(false, name + ": no clock to step"); steps(4); }
	std::string lights(const std::vector<std::string>& names) {
		CLSimLight l[32];
		const int n = cl_simview_lights(doc, page, l, 32);
		std::map<std::string, int> by;
		for (int i = 0; i < n && i < 32; i++) {
			char b[32] = "";
			cl_simview_light_name(doc, page, l[i].gate, b, sizeof b);
			by[b] = l[i].value;
		}
		std::string out;
		for (const auto& k : names) { auto it = by.find(k); out += it == by.end() ? '?' : it->second == 1 ? '1' : it->second == 0 ? '0' : 'X'; }
		return out;
	}
};

static bool open(Page& p, const std::string& dir, const std::string& id, int page, const std::map<std::string, int>& start) {
	std::ifstream in(dir + "/template-builtin-" + id + ".cdl", std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	const std::string text = ss.str();
	char err[512] = "";
	p.doc = text.empty() ? nullptr : cl_document_open_text(text.data(), (long)text.size(), err, sizeof err);
	p.page = page;
	p.name = id + (page ? " page " + std::to_string(page + 1) : "");
	if (!p.doc) { check(false, p.name + ": opens (" + err + ")"); return false; }
	// Each switch: the toggle on the page nearest the label with its name.
	std::vector<CLFindResult> toggles(64);
	const int nt = std::min(64, cl_find(p.doc, "AA_TOGGLE", toggles.data(), 64));
	std::vector<std::pair<double, double>> mine;
	for (int i = 0; i < nt; i++) if (toggles[i].page == page && std::string(toggles[i].kind) == "Part") mine.push_back({ toggles[i].x, toggles[i].y });
	for (const auto& [s, v] : start) {
		CLFindResult r[16];
		const int n = std::min(16, cl_find(p.doc, s.c_str(), r, 16));
		double best = 1e9;
		for (int i = 0; i < n; i++) {
			if (r[i].page != page || std::string(r[i].kind) != "Label" || s != r[i].text) continue;
			for (const auto& t : mine) {
				const double d = std::hypot(t.first - r[i].x, t.second - r[i].y);
				if (d < best && d < 8) { best = d; p.at[s] = t; }
			}
		}
		p.on[s] = v;
	}
	p.steps(20);
	return true;
}

// Runs `body` on page `page` of template `id` (opened fresh), its switches
// starting as `start` says.
static void run(const std::string& dir, const std::string& id, int page, const std::map<std::string, int>& start,
                const std::function<void(Page&)>& body) {
	Page p;
	if (!open(p, dir, id, page, start)) return;
	printf("-- %s\n", p.name.c_str());
	body(p);
	cl_document_close(p.doc);
}

int main(int argc, char** argv) {
	if (argc < 3) { fprintf(stderr, "usage: examples_check <cl_gatedefs.xml> <render-ui dir>\n"); return 2; }
	if (!cl_library_load(argv[1])) { fprintf(stderr, "library failed\n"); return 1; }
	const std::string dir = argv[2];
	auto Q = [](Page& p) { return p.lights({ "Q", "Q'" }); };

	// Two pages each, named.
	for (const char* id : { "ff-d", "ff-jk", "ff-t", "reg-register", "count-3bit", "comb-adder-4bit", "comb-decoder-2to4", "comb-mux-4to1" }) {
		Page p;
		if (!open(p, dir, id, 0, {})) continue;
		const int n = cl_document_page_count(p.doc);
		const std::string a = n > 0 ? cl_document_page_name(p.doc, 0) : "", b = n > 1 ? cl_document_page_name(p.doc, 1) : "";
		check(n == 2 && a == "Using the part" && b.rfind("Built from ", 0) == 0, std::string(id) + ": two pages (" + a + ", " + b + ")");
		cl_document_close(p.doc);
	}

	auto dff = [&](Page& p) {
		check(Q(p) == "01", p.name + ": starts with Q = 0 (" + Q(p) + ")");
		p.set("D", 1); check(Q(p) == "01", p.name + ": D = 1, nothing until the clock");
		p.step(); check(Q(p) == "10", p.name + ": a step takes D: Q = 1 (" + Q(p) + ")");
		p.set("D", 0); check(Q(p) == "10", p.name + ": D = 0, Q holds between edges");
		p.step(); check(Q(p) == "01", p.name + ": a step takes D: Q = 0 (" + Q(p) + ")");
	};
	auto jk = [&](Page& p) {
		check(Q(p) == "01", p.name + ": starts with Q = 0 (" + Q(p) + ")");
		p.set("J", 1); p.step(); check(Q(p) == "10", p.name + ": J = 1, a step sets Q (" + Q(p) + ")");
		p.set("J", 0); p.set("K", 1); p.step(); check(Q(p) == "01", p.name + ": K = 1, a step resets Q (" + Q(p) + ")");
		p.set("J", 1); p.step(); check(Q(p) == "10", p.name + ": J = K = 1, a step toggles Q to 1 (" + Q(p) + ")");
		p.step(); check(Q(p) == "01", p.name + ": and back to 0 (" + Q(p) + ")");
		p.step(); p.set("J", 0); p.set("K", 0); p.step(); check(Q(p) == "10", p.name + ": J = K = 0, a step holds Q (" + Q(p) + ")");
	};
	auto tff = [&](Page& p) {
		check(Q(p) == "01", p.name + ": starts with Q = 0 (" + Q(p) + ")");
		p.step(); check(Q(p) == "01", p.name + ": T = 0, a step holds Q");
		p.set("T", 1); p.step(); check(Q(p) == "10", p.name + ": T = 1, a step flips Q to 1 (" + Q(p) + ")");
		p.step(); check(Q(p) == "01", p.name + ": and back to 0 (" + Q(p) + ")");
	};
	auto clr = [&](Page& p) { p.set("CLR'", 1); check(Q(p) == "01", p.name + ": CLR' starts on 0, Q = 0; turned on, Q stays 0 (" + Q(p) + ")"); };
	run(dir, "ff-d", 0, { { "D", 0 } }, dff);
	run(dir, "ff-d", 1, { { "D", 0 }, { "CLR'", 0 } }, [&](Page& p) { clr(p); dff(p); });
	run(dir, "ff-jk", 0, { { "J", 0 }, { "K", 0 } }, jk);
	run(dir, "ff-jk", 1, { { "J", 0 }, { "K", 0 }, { "CLR'", 0 } }, [&](Page& p) { clr(p); jk(p); });
	run(dir, "ff-t", 0, { { "T", 0 } }, tff);
	run(dir, "ff-t", 1, { { "T", 0 } }, tff);

	auto reg = [&](Page& p) {
		auto q = [&] { return p.lights({ "Q3", "Q2", "Q1", "Q0" }); };
		check(q() == "0000", p.name + ": starts at 0000 (" + q() + ")");
		p.step(); check(q() == "0101", p.name + ": Load on, a step stores 0101 (" + q() + ")");
		p.set("Load", 0); p.set("D3", 1); p.set("D0", 0); p.step(); check(q() == "0101", p.name + ": Load off, it holds (" + q() + ")");
		p.set("Load", 1); p.step(); check(q() == "1100", p.name + ": Load on again, a step stores 1100 (" + q() + ")");
	};
	// (The app's own register template, page 1: its switches as register_check has them.)
	run(dir, "reg-register", 0, { { "Load", 1 }, { "D3", 0 }, { "D2", 1 }, { "D1", 0 }, { "D0", 1 } }, reg);
	run(dir, "reg-register", 1, { { "Load", 1 }, { "D3", 0 }, { "D2", 1 }, { "D1", 0 }, { "D0", 1 } }, reg);

	auto count = [&](Page& p) {
		auto q = [&] { return p.lights({ "Q2", "Q1", "Q0" }); };
		std::string seen = q();
		for (int i = 0; i < 9; i++) { p.step(); seen += "," + q(); }
		check(seen == "000,001,010,011,100,101,110,111,000,001", p.name + ": counts 0 to 7 and round (" + seen + ")");
		p.set("Count", 0); p.step(); check(q() == "001", p.name + ": Count off, a step holds it (" + q() + ")");
	};
	run(dir, "count-3bit", 0, { { "Count", 1 } }, count);
	run(dir, "count-3bit", 1, { { "Count", 1 }, { "CLR'", 0 } }, [&](Page& p) { p.set("CLR'", 1); count(p); });

	auto adder = [&](Page& p) {
		std::string bad;
		const int cases[][3] = { { 0, 0, 0 }, { 3, 1, 0 }, { 5, 10, 0 }, { 7, 9, 1 }, { 15, 15, 1 }, { 8, 8, 0 }, { 6, 3, 1 } };
		for (const auto& c : cases) {
			for (int k = 0; k < 4; k++) { p.set("A" + std::to_string(k), (c[0] >> k) & 1); p.set("B" + std::to_string(k), (c[1] >> k) & 1); }
			p.set("Cin", c[2]);
			const std::string s = p.lights({ "Cout", "S3", "S2", "S1", "S0" });
			if (std::stoi(s, nullptr, 2) != c[0] + c[1] + c[2] || s.find_first_not_of("01") != std::string::npos)
				bad += " " + std::to_string(c[0]) + "+" + std::to_string(c[1]) + "+" + std::to_string(c[2]) + "=" + s;
		}
		check(bad.empty(), p.name + ": adds 0+0, 3+1, 5+10, 7+9+1, 15+15+1, 8+8, 6+3+1" + bad);
	};
	const std::map<std::string, int> ab = { { "A0", 1 }, { "A1", 1 }, { "A2", 0 }, { "A3", 0 }, { "B0", 1 }, { "B1", 0 }, { "B2", 1 }, { "B3", 0 }, { "Cin", 0 } };
	run(dir, "comb-adder-4bit", 0, ab, adder);
	run(dir, "comb-adder-4bit", 1, ab, adder);

	auto decoder = [&](Page& p) {
		std::string rows;
		for (int en = 0; en < 2; en++) for (int a = 0; a < 4; a++) {
			p.set("EN", en); p.set("A1", a >> 1); p.set("A0", a & 1);
			rows += (rows.empty() ? "" : " ") + p.lights({ "Y3", "Y2", "Y1", "Y0" });
		}
		check(rows == "0000 0000 0000 0000 0001 0010 0100 1000", p.name + ": truth table (" + rows + ")");
	};
	run(dir, "comb-decoder-2to4", 0, { { "EN", 1 }, { "A1", 0 }, { "A0", 0 } }, decoder);
	run(dir, "comb-decoder-2to4", 1, { { "EN", 1 }, { "A1", 0 }, { "A0", 0 } }, decoder);

	auto mux = [&](Page& p) {
		int bad = 0;
		for (int s = 0; s < 4; s++) for (int d : { 1, 2, 4, 8, 14, 6 }) {
			p.set("S1", s >> 1); p.set("S0", s & 1);
			for (int k = 0; k < 4; k++) p.set("I" + std::to_string(k), (d >> k) & 1);
			if (p.lights({ "Y" }) != std::string(1, '0' + ((d >> s) & 1))) bad++;
		}
		check(!bad, p.name + ": Y is the input S1 S0 picks (" + std::to_string(bad) + " wrong)");
	};
	const std::map<std::string, int> mx = { { "S1", 0 }, { "S0", 0 }, { "I0", 0 }, { "I1", 1 }, { "I2", 0 }, { "I3", 0 } };
	run(dir, "comb-mux-4to1", 0, mx, mux);
	run(dir, "comb-mux-4to1", 1, mx, mux);

	run(dir, "comb-comparator-2bit", 0, { { "A1", 1 }, { "B1", 0 }, { "A0", 1 }, { "B0", 0 } }, [&](Page& p) {
		std::string bad;
		for (int A = 0; A < 4; A++) for (int B = 0; B < 4; B++) {
			p.set("A1", A >> 1); p.set("A0", A & 1); p.set("B1", B >> 1); p.set("B0", B & 1);
			const std::string got = p.lights({ "A>B", "A=B", "A<B" });
			const std::string want = std::string(1, A > B ? '1' : '0') + (A == B ? '1' : '0') + (A < B ? '1' : '0');
			if (got != want) bad += " " + std::to_string(A) + "," + std::to_string(B) + ":" + got;
		}
		check(bad.empty(), p.name + ": A>B, A=B, A<B for all 16" + bad);
	});
	run(dir, "comb-parity", 0, { { "D3", 1 }, { "D2", 0 }, { "D1", 1 }, { "D0", 1 } }, [&](Page& p) {
		int bad = 0;
		for (int d = 0; d < 16; d++) {
			int ones = 0;
			for (int k = 0; k < 4; k++) { p.set("D" + std::to_string(k), (d >> k) & 1); ones += (d >> k) & 1; }
			if (p.lights({ "P" }) != std::string(1, '0' + ones % 2)) bad++;
		}
		check(!bad, p.name + ": P is 1 for an odd number of 1s (" + std::to_string(bad) + " wrong)");
	});
	run(dir, "ff-edge", 0, { { "X", 0 } }, [&](Page& p) {
		auto e = [&] { return p.lights({ "Q", "Edge" }); };
		p.step(); check(e() == "00", p.name + ": X = 0, Q and Edge 0 (" + e() + ")");
		p.set("X", 1); check(e() == "01", p.name + ": X goes to 1, Edge lights (" + e() + ")");
		p.step(); check(e() == "10", p.name + ": the next step, Q = 1 and Edge goes out (" + e() + ")");
		p.set("X", 0); p.step(); check(e() == "00", p.name + ": X back to 0, no edge (" + e() + ")");
	});
	run(dir, "ff-sr-enable", 0, { { "S", 0 }, { "EN", 1 }, { "R", 1 } }, [&](Page& p) {
		check(Q(p) == "01", p.name + ": starts reset (" + Q(p) + ")");
		p.set("R", 0); p.set("S", 1); check(Q(p) == "10", p.name + ": S = 1 with EN on, set (" + Q(p) + ")");
		p.set("S", 0); check(Q(p) == "10", p.name + ": S back to 0, it holds");
		p.set("EN", 0); p.set("R", 1); check(Q(p) == "10", p.name + ": EN off, R does nothing");
		p.set("EN", 1); check(Q(p) == "01", p.name + ": EN on, R resets it (" + Q(p) + ")");
	});

	printf("%d of %d checks passed\n", checks - failures, checks);
	return failures ? 1 : 0;
}
