// Checks for Build from Formula (App/Formula.cpp), without the app: the
// formula reader on known answers, and circuits planned in every shape and
// gate style, built in the real engine, whose truth tables must match the
// formula. The Linux twin of mac/Tools/formula-check.
//   formula_check <cl_gatedefs.xml>
#include "CedarCore.h"
#include "Formula.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void fail(const std::string& what) {
	failures++;
	printf("FAIL: %s\n", what.c_str());
}

// Each minterm's value: 1, 0, or -1 for a don't-care.
std::vector<int> values(int n, const std::vector<int>& ones, const std::vector<int>& dc = {}) {
	std::vector<int> v((size_t)1 << n, 0);
	for (int m : ones) v[m] = 1;
	for (int m : dc) v[m] = -1;
	return v;
}

void expectTruth(const std::string& text, const std::string& name, const std::vector<int>& want) {
	formula::Parsed p;
	std::string error;
	if (!formula::parse(text, p, error)) { fail("\"" + text + "\" didn't read: " + error); return; }
	for (const formula::Function& f : p.functions)
		if (f.name == name) {
			if (f.values != want) fail("\"" + text + "\": " + name + " has the wrong table");
			return;
		}
	fail("\"" + text + "\": no output " + name);
}

// A full adder whose carry is wrong when all three are on.
CLTruthTable* wrongAdder(char floatingCarry = 0) {
	CLTruthTable* tt = cl_tt_new(3, false, 0);
	for (const char* n : { "A", "B", "Cin", "S", "Cout" }) cl_tt_add_name(tt, n);
	for (int m = 0; m < 8; m++) {
		const int ones = __builtin_popcount(m);
		std::string row;
		for (int i = 2; i >= 0; i--) row += ((m >> i) & 1) ? '1' : '0';
		row += ones % 2 ? '1' : '0';
		row += floatingCarry && m < 4 ? floatingCarry : (ones >= 2 && m != 7) ? '1' : '0';
		cl_tt_add_row(tt, row.c_str());
	}
	return tt;
}

void expectCheck(const std::string& what, bool table, const std::string& text, int verdict, const std::string& summary,
                 const char* names = "", char floating = 0) {
	CLTruthTable* tt = wrongAdder(floating);
	std::string source = text;
	if (!table) {
		formula::Parsed p;
		std::string error;
		if (!formula::parse(text, p, error)) { fail(what + ": didn't read: " + error); cl_tt_free(tt); return; }
		source = formula::checkSpec(p);
	}
	CLCheck* c = table ? cl_check_table(tt, source.c_str(), names) : cl_check_expected(tt, source.c_str(), names);
	if (cl_check_verdict(c) != verdict || (!summary.empty() && summary != cl_check_summary(c)))
		fail(what + ": " + std::to_string(cl_check_verdict(c)) + " " + cl_check_summary(c));
	cl_check_free(c);
	cl_tt_free(tt);
}

const char* kShapes[] = { "as written", "sum of products", "product of sums" };
const char* kStyles[] = { "any gates", "NAND only", "NOR only" };

}  // namespace

int main(int argc, char** argv) {
	// ---- Reading formulas ----
	expectTruth("F = A'B + AC", "F", values(3, { 2, 3, 5, 7 }));
	expectTruth("F = (A + B)'C", "F", values(3, { 1 }));
	expectTruth("F = ~(A*B)", "F", values(2, { 0, 1, 2 }));
	expectTruth("F = (AB)'", "F", values(2, { 0, 1, 2 }));
	expectTruth("S = A ^ B ^ Cin; Cout = AB + Cin(A ^ B)", "Cout", values(3, { 3, 5, 6, 7 }));
	expectTruth("F(A,B,C) = Σm(1,3,5) + d(7)", "F", values(3, { 1, 3, 5 }, { 7 }));
	for (const char* bad : { "F = A +", "F = (A", "F = A )", "= A", "F = ", "F(A,B) = C", "F = Σm(1,9)", "F(A) = Σm(1,2)" }) {
		formula::Parsed p;
		std::string error;
		if (formula::parse(bad, p, error)) fail(std::string("should refuse \"") + bad + "\"");
	}

	// ---- Check my circuit, on tables made by hand ----
	const std::string adder = "S = A ^ B ^ Cin; Cout = AB + Cin(A ^ B)";
	expectCheck("adder", false, adder, 1, "Doesn't match: Cout is wrong on 1 of 8 rows.");
	expectCheck("don't-care", false, "S(A,B,Cin) = Σm(1,2,4,7); Cout(A,B,Cin) = Σm(3,5,6) + d(7)", 0,
	            "Matches: every row gives what was asked for (1 don't-care wasn't checked).");
	expectCheck("by position", false, "Sum = X ^ Y ^ Z", 2, "Can't check yet: there's no light called Sum.");
	expectCheck("by hand", false, "Sum = X ^ Y ^ Z", 0, "", "Sum\tS");
	expectCheck("missing switches", false, "S = A ^ B ^ Ci + D", 2, "Can't check yet: there are no switches called Ci and D.");
	expectCheck("floating", false, adder, 1, "Doesn't match: Cout is wrong on 5 of 8 rows.", "", 'Z');
	expectCheck("table", true, "A B Cin | S Cout\n000 00\n001 10\n010 10\n011 01\n100 10\n101 01\n110 01\n111 11", 1,
	            "Doesn't match: Cout is wrong on 1 of 8 rows.");
	expectCheck("bad table", true, "A B | F\n0 0 | 2", 2, "Row 1 has \"2\"; use 0, 1, or X for don't care.");

	// ---- Built in the engine: its truth table must be the formula's ----
	if (!cl_library_load(argc > 1 ? argv[1] : "res/cl_gatedefs.xml")) {
		printf("FAIL: gate library didn't load\n");
		return 2;
	}
	const char* formulas[] = {
		"F = A'B + AC",
		"S = A ^ B ^ Cin; Cout = AB + Cin(A ^ B)",
		"F(A,B,C,D) = Σm(1,3,7,11,15) + d(0,2,5)",
		"F = (A + B)(A' + C)",
		"F = A",
		"F = A'",
		"F = 1",
		"F = A B C D E",
		"Y = A'B'C' + ABC + A B' D + C D' E",
		"F = (A ⊕ B)'",
		"G = A + B + C + D + E + F1",
		// Negated ANDs and ORs as written: a NAND and a NOR in every style.
		"F = (AB)'",
		"F = ~(A & B & C)",
		"F = (A'B')'",
		"F = (A B C D E)'",
		"F = (A + B + C)'",
		"F = (AB)' + (CD)'",
		"F = ((A + B)(C + D))'",
	};
	int built = 0;
	for (const char* text : formulas) {
		formula::Parsed parsed;
		std::string error;
		if (!formula::parse(text, parsed, error)) { fail(std::string("\"") + text + "\" didn't read: " + error); continue; }
		for (int shape = 0; shape < 3; shape++)
			for (int style = 0; style < 3; style++)
				for (int two = 0; two < 2; two++) {
					const formula::Plan plan = formula::plan(parsed, (formula::Shape)shape, (formula::Style)style, two != 0);
					const std::string what = std::string("\"") + text + "\" " + kShapes[shape] + ", " + kStyles[style] + (two ? ", 2-input" : "");
					// Every gate input is wired.
					for (size_t g = 0; g < plan.parts.size(); g++) {
						const std::string& gate = plan.parts[g].gate;
						if (gate == "AA_TOGGLE" || gate == "AA_LABEL" || gate == "EE_VDD" || gate == "FF_GND") continue;
						const int want = gate == "GA_LED" || gate == "AA_INVERTER" ? 1 : gate.back() - '0';
						int got = 0;
						for (const formula::Plan::Wire& w : plan.wires) if (w.to == (int)g) got++;
						if (got != want) fail(what + ": " + gate + " has " + std::to_string(got) + " of " + std::to_string(want) + " inputs wired");
					}
					CLDocument* doc = cl_document_new();
					if (doc == nullptr) { fail("no document"); continue; }
					std::vector<CLBuildGate> gates;
					for (const formula::Plan::Part& p : plan.parts)
						gates.push_back({ p.gate.c_str(), p.x, p.y, p.label.empty() ? nullptr : p.label.c_str() });
					std::vector<CLBuildWire> wires;
					for (const formula::Plan::Wire& w : plan.wires) wires.push_back({ w.from, w.fromPin.c_str(), w.to, w.toPin.c_str() });
					const int made = cl_edit_build(doc, 0, gates.data(), (int)gates.size(), wires.data(), (int)wires.size(), "Build");
					if (made != (int)plan.parts.size())
						fail(what + ": made " + std::to_string(made) + " of " + std::to_string(plan.parts.size()));
					if (parsed.variables.empty()) {   // no switches, no table
						cl_document_close(doc);
						built++;
						continue;
					}
					char err[256] = "";
					CLTruthTable* tt = cl_truth_table(doc, 0, err, sizeof err);
					if (tt == nullptr) { fail(what + ": no truth table: " + err); cl_document_close(doc); continue; }
					const int ins = cl_tt_inputs(tt), cols = cl_tt_columns(tt);
					for (int c = 0; c < ins && c < (int)parsed.variables.size(); c++)
						if (parsed.variables[c] != cl_tt_name(tt, c)) fail(what + ": input " + std::to_string(c) + " is " + cl_tt_name(tt, c));
					for (const formula::Function& f : parsed.functions) {
						int col = -1;
						for (int c = ins; c < cols; c++) if (f.name == cl_tt_name(tt, c)) col = c;
						if (col < 0) { fail(what + ": no column " + f.name); continue; }
						for (int r = 0; r < cl_tt_rows(tt) && r < (int)f.values.size(); r++) {
							if (f.values[r] < 0) continue;
							const char cell = cl_tt_cell(tt, r, col);
							if (cell != (f.values[r] ? '1' : '0'))
								fail(what + ": " + f.name + " row " + std::to_string(r) + " is " + cell + ", want " + (f.values[r] ? "1" : "0"));
						}
					}
					// Check my circuit: the formula itself matches.
					{
						CLCheck* c = cl_check_expected(tt, formula::checkSpec(parsed).c_str(), "");
						if (cl_check_verdict(c) != 0) fail(what + ": check says " + cl_check_summary(c));
						cl_check_free(c);
					}
					cl_tt_free(tt);
					cl_document_close(doc);
					built++;
				}
	}
	printf("built %d circuits; %s\n", built, failures == 0 ? "all checks passed" : (std::to_string(failures) + " failed").c_str());
	return failures == 0 ? 0 : 1;
}
