// Headless check of the Registers templates (File ▸ New from Template) in
// the engine: each one does what the website's docs/REGISTER-EXAMPLES.md
// says it does, one Step Clock at a time -- the 4-bit register loads, holds
// and clears; the shift register shifts left and right, holds, loads and
// clears; serial-in parallel-out shifts and sets or clears at once; the ring
// counter goes round; the Johnson counter counts its eight states -- and
// each running-clock choice runs.
//   register_check <cl_gatedefs.xml> <dir with template-builtin-reg-*.cdl>
// (`CedarLogic --render-ui <dir>` writes them.)
#include "CedarCore.h"
#include <cstdio>
#include <cstring>
#include <fstream>
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

static CLDocument* open(const std::string& dir, const std::string& id) {
	std::ifstream in(dir + "/template-builtin-reg-" + id + ".cdl", std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	const std::string text = ss.str();
	char err[512] = "";
	CLDocument* doc = text.empty() ? nullptr : cl_document_open_text(text.data(), (long)text.size(), err, sizeof err);
	if (!doc) printf("FAIL open %s: %s\n", id.c_str(), err);
	return doc;
}

static void steps(CLDocument* doc, int n) { for (int i = 0; i < n; i++) cl_document_step(doc); }

// Q3 Q2 Q1 Q0 as "0101", read from the lights named by the labels beside them.
static std::string q(CLDocument* doc) {
	CLSimLight lights[16];
	const int n = cl_simview_lights(doc, 0, lights, 16);
	std::map<std::string, int> byName;
	for (int i = 0; i < n && i < 16; i++) {
		char name[32];
		cl_simview_light_name(doc, 0, lights[i].gate, name, sizeof name);
		byName[name] = lights[i].value;
	}
	std::string out;
	for (const char* k : { "Q3", "Q2", "Q1", "Q0" }) {
		auto it = byName.find(k);
		out += it == byName.end() ? '?' : it->second == 1 ? '1' : it->second == 0 ? '0' : 'X';
	}
	return out;
}

static void flip(CLDocument* doc, double x, double y, const char* what) {
	if (!cl_document_click(doc, 0, x, y)) printf("FAIL no switch (%s) at %g,%g\n", what, x, y);
	steps(doc, 12);
}

static std::string clock(CLDocument* doc) {
	cl_document_clock_step(doc, 0);
	steps(doc, 4);
	return q(doc);
}

// Each Step Clock gives the next of `want`.
static void sequence(CLDocument* doc, const std::vector<std::string>& want, const std::string& what) {
	std::string got;
	bool ok = true;
	for (const std::string& w : want) {
		const std::string s = clock(doc);
		got += (got.empty() ? "" : " ") + s;
		if (s != w) ok = false;
	}
	check(ok, what + " (" + got + ")");
}

// The running-clock choice: no manual clock, and the lights move by
// themselves (after `x, y` is flipped, when given: a register that only
// loads has nothing new to show until D or Load changes).
static void runs(const std::string& dir, const std::string& id, double x = 0, double y = 0, const char* what = nullptr, int least = 3) {
	CLDocument* doc = open(dir, id + "-running");
	if (!doc) { check(false, id + " (running clock): opens"); return; }
	check(cl_document_manual_clock_count(doc, 0) == 0, id + " (running clock): no manual clock");
	if (what) flip(doc, x, y, what);
	std::string prev = q(doc);
	int changes = 0;
	for (int i = 0; i < 600; i++) {
		cl_document_step(doc);
		const std::string now = q(doc);
		if (now != prev) { changes++; prev = now; }
	}
	check(changes >= least, id + " (running clock): the lights change by themselves (" + std::to_string(changes) + " times in 600 steps)");
	cl_document_close(doc);
}

int main(int argc, char** argv) {
	if (argc < 3) { fprintf(stderr, "usage: register_check <cl_gatedefs.xml> <render-ui dir>\n"); return 2; }
	if (!cl_library_load(argv[1])) { fprintf(stderr, "library failed\n"); return 1; }
	const std::string dir = argv[2];

	// 4-Bit Register (Load and Hold).
	if (CLDocument* doc = open(dir, "register")) {
		check(cl_document_manual_clock_count(doc, 0) == 1, "register: one clock, on Step Clock");
		check(q(doc) == "0000", "register: starts at 0000 (" + q(doc) + ")");
		check(clock(doc) == "0101", "register: Load on, a step stores D3-D0 = 0101");
		flip(doc, 14, -9, "Load");
		flip(doc, 14, -13, "D3");
		check(clock(doc) == "0101", "register: Load off, D changed to 1101, a step holds 0101");
		check(clock(doc) == "0101", "register: and another");
		flip(doc, 14, -9, "Load");
		check(clock(doc) == "1101", "register: Load on again, a step stores 1101");
		flip(doc, 19, -36, "Clear");
		check(clock(doc) == "0000", "register: Clear on, a step clears it (Clear wins over Load)");
		cl_document_close(doc);
	}
	runs(dir, "register", 14, -13, "D3", 1);   // it loads the new D3 by itself, then holds

	// 4-Bit Shift Register.
	if (CLDocument* doc = open(dir, "shift")) {
		sequence(doc, { "0001", "0011" }, "shift: Shift, Left and Serial In on: 0001, 0011");
		flip(doc, 46, -15, "Serial In");
		sequence(doc, { "0110" }, "shift: Serial In off: 0110");
		flip(doc, 46, -20, "Shift");
		sequence(doc, { "0110", "0110" }, "shift: Shift off: it holds");
		flip(doc, 46, -20, "Shift");
		flip(doc, 46, -30, "Left");
		sequence(doc, { "0011" }, "shift: Shift on, Left off: shifts right, Serial In (0) in at Q3: 0011");
		flip(doc, 46, -15, "Serial In");
		sequence(doc, { "1001", "1100" }, "shift: Serial In on: 1001, 1100");
		flip(doc, 46, -25, "Load");
		sequence(doc, { "0101" }, "shift: Load on: a step loads D3-D0 (0101), ahead of Shift");
		flip(doc, 19, -20, "Clear");
		sequence(doc, { "0000" }, "shift: Clear on: a step clears it, ahead of Load");
		cl_document_close(doc);
	}
	runs(dir, "shift");

	// Serial-In, Parallel-Out.
	if (CLDocument* doc = open(dir, "sipo")) {
		sequence(doc, { "0001", "0011" }, "sipo: Serial In on: 0001, 0011");
		flip(doc, 15, -18, "Serial In");
		sequence(doc, { "0110", "1100", "1000" }, "sipo: Serial In off: 0110, 1100, 1000");
		flip(doc, 15, -10, "PRE'");
		check(q(doc) == "1111", "sipo: PRE' to 0 sets all four at once, no clock (" + q(doc) + ")");
		flip(doc, 15, -10, "PRE'");
		flip(doc, 11, -29, "CLR'");
		check(q(doc) == "0000", "sipo: CLR' to 0 clears them at once (" + q(doc) + ")");
		cl_document_close(doc);
	}
	runs(dir, "sipo");

	// Ring Counter.
	if (CLDocument* doc = open(dir, "ring")) {
		sequence(doc, { "0001" }, "ring: Load and D0 on: a step loads 0001");
		flip(doc, 46, -25, "Load");
		sequence(doc, { "0010", "0100", "1000", "0001", "0010" }, "ring: Load off: the 1 goes round");
		flip(doc, 46, -20, "Shift");
		sequence(doc, { "0010", "0010" }, "ring: Shift off holds");
		flip(doc, 46, -20, "Shift");
		flip(doc, 22, -9, "D2");
		flip(doc, 46, -25, "Load");
		sequence(doc, { "0101" }, "ring: Load 0101");
		flip(doc, 46, -25, "Load");
		sequence(doc, { "1010", "0101", "1010" }, "ring: two 1s go round: 1010, 0101");
		cl_document_close(doc);
	}
	runs(dir, "ring", 46, -25, "Load");

	// Johnson Counter.
	if (CLDocument* doc = open(dir, "johnson")) {
		sequence(doc, { "0001", "0011", "0111", "1111", "1110", "1100", "1000", "0000", "0001" },
		         "johnson: 0001, 0011, 0111, 1111, 1110, 1100, 1000, 0000, and round again");
		sequence(doc, { "0011", "0111" }, "johnson: on again");
		flip(doc, 11, -29, "CLR'");
		check(q(doc) == "0000", "johnson: CLR' to 0 starts it over (" + q(doc) + ")");
		flip(doc, 11, -29, "CLR'");
		sequence(doc, { "0001" }, "johnson: CLR' back on: 0001");
		cl_document_close(doc);
	}
	runs(dir, "johnson");

	printf("%d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
