// Headless check that each Classic Mistakes template really shows its
// problem in the engine, on the files `CedarLogic --render-ui <dir>` writes:
//   - J-K latch from gates: Reset holds Q and Q' still, even with EN on;
//     steady while J and K differ, but with J = K = 1 and EN on Q flickers
//     for good (the truth table calls it unsettled);
//   - gated clock: Step Clock counts on every press, not every other one,
//     and a running clock does the same;
//   - three-input AND: the open input makes the light unknown (X) exactly
//     when A and B are both on;
//   - two gates, one light: the wire is in conflict (!) when A and B differ.
//   mistakes_check <cl_gatedefs.xml> <dir with template-builtin-mistake-*.cdl>
#include "CedarCore.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int failures = 0, checks = 0;

static void check(bool ok, const std::string& what) {
	checks++;
	if (!ok) failures++;
	printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
}

static bool readFile(const std::string& path, std::string& out) {
	std::ifstream in(path, std::ios::binary);
	if (!in) return false;
	std::ostringstream ss;
	ss << in.rdbuf();
	out = ss.str();
	return true;
}

static CLDocument* openTemplate(const std::string& dir, const std::string& id) {
	std::string text;
	if (!readFile(dir + "/template-builtin-mistake-" + id + ".cdl", text)) { check(false, id + ": template file"); return nullptr; }
	char err[512] = "";
	CLDocument* doc = cl_document_open_text(text.data(), (long)text.size(), err, sizeof err);
	if (!doc) check(false, id + ": opens (" + err + ")");
	return doc;
}

static void steps(CLDocument* doc, int n) { for (int i = 0; i < n; i++) cl_document_step(doc); }

// The page's lights (top to bottom), as the Simulation View bar sees them.
static bool light(CLDocument* doc, size_t i) {
	CLSimChip buf[32];
	const int n = cl_simview_chips(doc, 0, buf, 32);
	size_t seen = 0;
	for (int k = 0; k < n && k < 32; k++) {
		if (buf[k].isInput) continue;
		if (seen++ == i) return buf[k].lit;
	}
	return false;
}

static void click(CLDocument* doc, double x, double y) {
	char where[64];
	snprintf(where, sizeof where, "%g,%g", x, y);
	if (!cl_document_click(doc, 0, x, y)) check(false, std::string("a switch at ") + where);
	steps(doc, 12);
}

// The truth table's rows as text (the inputs' then the outputs' cells, no
// spaces), and how many rows never settled.
struct Table { std::vector<std::string> rows; int unsettled = 0; };
static Table table(CLDocument* doc) {
	Table t;
	char err[256] = "";
	CLTruthTable* tt = cl_truth_table(doc, 0, err, sizeof err);
	if (!tt) { check(false, std::string("truth table (") + err + ")"); return t; }
	for (int r = 0; r < cl_tt_rows(tt); r++) {
		std::string s;
		for (int c = 0; c < cl_tt_columns(tt); c++) s += cl_tt_cell(tt, r, c);
		t.rows.push_back(s);
	}
	t.unsettled = cl_tt_unsettled(tt);
	cl_tt_free(tt);
	return t;
}

// The counter's value, from the saved text.
static int counterValue(CLDocument* doc) {
	const std::string text = cl_document_save_text(doc);
	const size_t gate = text.find("(gate \"AA_REGISTER4\"");
	if (gate == std::string::npos) return -1;
	const std::string tag = "(lparam \"CURRENT_VALUE\" \"";
	const size_t at = text.find(tag, gate);
	if (at == std::string::npos) return 0;
	return atoi(text.c_str() + at + tag.size());
}

static void latch(const std::string& dir) {
	CLDocument* doc = openTemplate(dir, "latch");
	if (!doc) return;
	steps(doc, 40);
	check(!light(doc, 0) && light(doc, 1), "latch: starts reset (Q = 0, Q' = 1)");

	// Reset holds both sides: EN on (J = K = 1) changes nothing yet.
	click(doc, 0, 0);
	bool held = true;
	for (int i = 0; i < 100; i++) { cl_document_step(doc); if (light(doc, 0) || !light(doc, 1)) held = false; }
	check(held, "latch: Reset on, EN on: Q = 0 and Q' = 1, steady");
	click(doc, 0, 0);    // EN off again
	click(doc, 0, 15);   // Reset off
	steps(doc, 40);
	check(!light(doc, 0) && light(doc, 1), "latch: Reset off, EN off: Q holds 0");

	// J = K = 1, EN on: Q never settles.
	click(doc, 0, 0);
	steps(doc, 20);
	int changes = 0, lateChanges = 0;
	bool prevQ = light(doc, 0);
	for (int i = 0; i < 300; i++) {
		cl_document_step(doc);
		const bool q = light(doc, 0);
		if (q != prevQ) { changes++; if (i >= 150) lateChanges++; }
		prevQ = q;
	}
	check(changes >= 40 && lateChanges >= 20,
	      "latch: J = K = 1 with EN on, Q keeps changing (" + std::to_string(changes) + " changes in 300 steps, " +
	      std::to_string(lateChanges) + " in the last 150)");
	const Table t = table(doc);
	check(t.unsettled > 0, "latch: the truth table finds rows that never settle (" + std::to_string(t.unsettled) + ")");

	// EN off and Reset on: back to a known state; then it works when J and K differ.
	click(doc, 0, 0);
	click(doc, 0, 15);
	steps(doc, 30);
	check(!light(doc, 0) && light(doc, 1), "latch: EN off and Reset on: back to Q = 0");
	click(doc, 0, 15);   // Reset off
	click(doc, 0, 8);    // K off
	click(doc, 0, 0);    // EN on: J = 1, K = 0, a plain set
	steps(doc, 60);
	bool steady = light(doc, 0) && !light(doc, 1);
	for (int i = 0; i < 100; i++) { cl_document_step(doc); if (!light(doc, 0) || light(doc, 1)) steady = false; }
	check(steady, "latch: with J = 1, K = 0 and EN on it simply sets (Q = 1, steady)");
	click(doc, 0, 8);    // K on
	click(doc, 0, -8);   // J off: a plain reset
	steps(doc, 60);
	steady = !light(doc, 0) && light(doc, 1);
	for (int i = 0; i < 100; i++) { cl_document_step(doc); if (light(doc, 0) || !light(doc, 1)) steady = false; }
	check(steady, "latch: with J = 0, K = 1 and EN on it resets (Q = 0, steady)");
	cl_document_close(doc);
}

static void gatedClock(const std::string& dir) {
	CLDocument* doc = openTemplate(dir, "gated-clock");
	if (!doc) return;
	check(cl_document_manual_clock_count(doc, 0) == 1, "gated clock: one manual clock");
	steps(doc, 30);
	check(counterValue(doc) == 0 && !light(doc, 0), "gated clock: starts at 0 with Enable off, and holds still");
	// 8 presses: Enable alternates, yet the count goes up on every one.
	bool counted = true, alternates = true;
	bool prevEnable = light(doc, 0);
	for (int i = 1; i <= 8; i++) {
		if (!cl_document_clock_step(doc, 0)) counted = false;
		if (counterValue(doc) != i) counted = false;
		if (light(doc, 0) == prevEnable) alternates = false;
		prevEnable = light(doc, 0);
	}
	check(alternates, "gated clock: Enable alternates 1, 0, 1, 0 over 8 presses");
	check(counted, "gated clock: and the count is 8 after 8 presses (an ideal enable would give 4)");

	// A running clock: the count goes up once per cycle, as often as Enable
	// changes, instead of half as often.
	cl_edit_press(doc, 0, 0, 0, 0, 0.05);
	cl_edit_release(doc, 0, 0);
	const long clock = cl_edit_single_gate(doc, 0);
	cl_edit_select_none(doc, 0);
	check(cl_gate_set_setting(doc, clock, "MANUAL", "false"), "gated clock: the clock can be set to run");
	steps(doc, 20);
	int enableChanges = 0, increments = 0;
	bool prev = light(doc, 0);
	int last = counterValue(doc);
	for (int i = 0; i < 600; i++) {
		cl_document_step(doc);
		const bool e = light(doc, 0);
		if (e != prev) { enableChanges++; prev = e; }
		const int v = counterValue(doc);
		if (v != last) { increments++; last = v; }
	}
	check(enableChanges >= 50 && increments >= enableChanges - 2,
	      "gated clock: running, " + std::to_string(increments) + " counts for " + std::to_string(enableChanges) +
	      " Enable changes (an ideal enable would count half as often)");
	cl_document_close(doc);
}

static void floating(const std::string& dir) {
	CLDocument* doc = openTemplate(dir, "floating");
	if (!doc) return;
	steps(doc, 20);
	check(!light(doc, 0), "floating: the light isn't lit");
	const Table t = table(doc);
	const std::vector<std::string> want = { "000", "010", "100", "11X" };
	check(t.rows == want, "floating: the light is 0 unless A and B are both on, then X (unknown)");
	cl_document_close(doc);
}

static void twoOutputs(const std::string& dir) {
	CLDocument* doc = openTemplate(dir, "two-outputs");
	if (!doc) return;
	steps(doc, 20);
	const Table t = table(doc);
	const std::vector<std::string> want = { "000", "01!", "10!", "111" };
	check(t.rows == want, "two outputs: the light is in conflict (!) when A and B differ, fine when they match");
	cl_document_close(doc);
}

int main(int argc, char** argv) {
	if (argc < 3) { fprintf(stderr, "usage: mistakes_check <cl_gatedefs.xml> <render-ui dir>\n"); return 2; }
	if (!cl_library_load(argv[1])) { fprintf(stderr, "couldn't load the library\n"); return 1; }
	const std::string dir = argv[2];
	latch(dir);
	gatedClock(dir);
	floating(dir);
	twoOutputs(dir);
	printf("%d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
