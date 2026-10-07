// Headless check of Step Clock (clocks with "Only on Step Clock" on) through
// the C interface, on the flip-flop templates `CedarLogic --render-ui <dir>`
// writes: manual clocks hold still, each Step Clock is one full cycle, a
// running clock still runs (and the templates' running choice has one), the
// setting survives a save, a clock with it off saves without it, and undo
// turns it back off. The picker's pulse-button choice (-pulse): a
// Single-Pulse Generator labeled Clock in place of the clock, and each
// click on it clocks the flip-flop once.
//   clock_check <cl_gatedefs.xml> <dir with template-builtin-ff-*.cdl>
#include "CedarCore.h"
#include <chrono>
#include <cstdio>
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

static CLDocument* openText(const std::string& text) {
	char err[512] = "";
	CLDocument* doc = cl_document_open_text(text.data(), (long)text.size(), err, sizeof err);
	if (!doc) printf("FAIL open: %s\n", err);
	return doc;
}

// The page's switches (top to bottom) and lights (Q above Q'), as the
// Simulation View bar sees them.
struct Chips { std::vector<bool> in, out; };
static Chips chips(CLDocument* doc) {
	CLSimChip buf[32];
	const int n = cl_simview_chips(doc, 0, buf, 32);
	Chips c;
	for (int i = 0; i < n && i < 32; i++) (buf[i].isInput ? c.in : c.out).push_back(buf[i].lit);
	return c;
}
static bool q(CLDocument* doc) { Chips c = chips(doc); return c.out.size() > 0 && c.out[0]; }
static bool nq(CLDocument* doc) { Chips c = chips(doc); return c.out.size() > 1 && c.out[1]; }

static void steps(CLDocument* doc, int n) { for (int i = 0; i < n; i++) cl_document_step(doc); }

// Flip the switch at (6, y) (the templates' data switches) and let it settle.
static void flip(CLDocument* doc, double y) {
	if (!cl_document_click(doc, 0, 6, y)) printf("FAIL no switch at 6,%g\n", y);
	steps(doc, 12);
}

// The clock (at x = 4 in the templates), selected; its gate id.
static long clockGate(CLDocument* doc, double clockY) {
	cl_edit_press(doc, 0, 4, clockY, 0, 0.05);
	cl_edit_release(doc, 4, clockY);
	return cl_edit_single_gate(doc, 0);
}

static std::string setting(CLDocument* doc, long gate, const char* name) {
	const int n = cl_gate_setting_count(doc, gate);
	for (int i = 0; i < n; i++) {
		CLGateSetting s;
		if (cl_gate_setting(doc, gate, i, &s) && strcmp(s.name, name) == 0)
			return std::string(s.label) + "|" + s.type + "|" + s.value;
	}
	return "";
}

// Q holds over 200 steps (with the inputs set so a moving clock would change it).
static void holdsStill(CLDocument* doc, const std::string& name) {
	const bool q0 = q(doc), nq0 = nq(doc);
	bool still = true;
	for (int i = 0; i < 200; i++) {
		cl_document_step(doc);
		if (q(doc) != q0 || nq(doc) != nq0) still = false;
	}
	check(still, name + ": Q holds over 200 steps with the clock on manual");
}

// J = K = 1 (or T = 1): every Step Clock toggles Q, and Q' is its opposite.
static void toggles(CLDocument* doc, const std::string& name) {
	bool ok = true;
	bool prev = q(doc);
	for (int i = 0; i < 8; i++) {
		if (!cl_document_clock_step(doc, 0)) { ok = false; break; }
		const bool now = q(doc);
		if (now == prev || nq(doc) == now) ok = false;
		prev = now;
	}
	check(ok, name + ": each Step Clock toggles Q (8 presses)");
}

// D is captured on each Step Clock, and only then.
static void captures(CLDocument* doc, const std::string& name, double dY) {
	bool ok = true;
	cl_document_clock_step(doc, 0);   // start with Q = D
	bool d = chips(doc).in.size() > 1 && chips(doc).in[1];
	ok = q(doc) == d;
	for (int i = 0; i < 6; i++) {
		flip(doc, dY);
		d = !d;
		steps(doc, 20);
		if (q(doc) == d) ok = false;   // not before the step (Q still holds the old D)
		cl_document_clock_step(doc, 0);
		if (q(doc) != d || nq(doc) == d) ok = false;
	}
	check(ok, name + ": Q takes D on each Step Clock, and not before");
}

static void runsAgain(CLDocument* doc, long gate, const std::string& name) {
	check(cl_gate_set_setting(doc, gate, "MANUAL", "false"), name + ": the setting can be turned off");
	check(cl_document_manual_clock_count(doc, 0) == 0, name + ": then no manual clock on the page");
	check(!cl_document_clock_step(doc, 0), name + ": and Step Clock says there's none");
	int changes = 0;
	bool prev = q(doc);
	for (int i = 0; i < 200; i++) {
		cl_document_step(doc);
		if (q(doc) != prev) { changes++; prev = q(doc); }
	}
	check(changes >= 10, name + ": a running clock runs (Q changed " + std::to_string(changes) + " times in 200 steps)");
	check(cl_gate_set_setting(doc, gate, "MANUAL", "true"), name + ": and back to manual");
	steps(doc, 5);
	const bool q0 = q(doc);
	bool still = true;
	for (int i = 0; i < 100; i++) { cl_document_step(doc); if (q(doc) != q0) still = false; }
	check(still && cl_document_manual_clock_count(doc, 0) == 1, name + ": switched back mid-run, it holds still again");
}


// The pulse-button choice: no clock part, Q holds until the button (at
// x = 6, its middle) is clicked, then each click is one clock.
static bool clickPulse(CLDocument* doc, double clockY) {
	const bool ok = cl_document_click(doc, 0, 6, clockY);
	steps(doc, 20);
	return ok;
}

static void pulseChoice(const std::string& dir, const char* id, const char* kind, double clockY) {
	const std::string name = std::string("ff-") + id + "-pulse";
	std::string text;
	if (!readFile(dir + "/template-builtin-ff-" + id + "-pulse.cdl", text)) { check(false, name + ": template file"); return; }
	check(text.find("CC_PULSE") != std::string::npos && text.find("BB_CLOCK") == std::string::npos,
	      name + ": a pulse button, no clock part");
	check(text.find("Click the Clock button") != std::string::npos && text.find("K key") == std::string::npos,
	      name + ": the hint says to click Clock, no K key");
	CLDocument* doc = openText(text);
	if (!doc) { failures++; return; }
	check(cl_document_manual_clock_count(doc, 0) == 0 && !cl_document_clock_step(doc, 0), name + ": nothing for Step Clock");
	if (strcmp(kind, "jk") == 0 || strcmp(kind, "t") == 0) {
		flip(doc, 8);
		if (strcmp(kind, "jk") == 0) flip(doc, -8);
		const bool q0 = q(doc);
		bool still = true;
		for (int i = 0; i < 200; i++) { cl_document_step(doc); if (q(doc) != q0) still = false; }
		check(still, name + ": Q holds until the button is clicked");
		bool ok = true, prev = q(doc);
		for (int i = 0; i < 8; i++) {
			if (!clickPulse(doc, clockY)) { ok = false; break; }
			if (q(doc) == prev || nq(doc) == q(doc)) ok = false;
			prev = q(doc);
		}
		check(ok, name + ": each click on Clock toggles Q (8 clicks)");
	} else {
		if (strcmp(kind, "dce") == 0) {
			flip(doc, 8);                  // D = 1 with CE = 0
			const bool q0 = q(doc);
			clickPulse(doc, clockY);
			check(q(doc) == q0, name + ": with CE = 0, a click leaves Q alone");
			flip(doc, -8);                 // CE = 1
			flip(doc, 8);                  // D back to 0
		}
		bool d = false, ok = true;
		clickPulse(doc, clockY);           // start with Q = D = 0
		if (q(doc)) ok = false;
		for (int i = 0; i < 6; i++) {
			flip(doc, 8);
			d = !d;
			steps(doc, 20);
			if (q(doc) == d) ok = false;   // not taken before the click
			if (!clickPulse(doc, clockY)) ok = false;
			if (q(doc) != d || nq(doc) == d) ok = false;
		}
		check(ok, name + ": Q takes D on each click on Clock, and not before");
	}
	cl_document_close(doc);
}

int main(int argc, char** argv) {
	if (argc < 3) { fprintf(stderr, "usage: clock_check <cl_gatedefs.xml> <render-ui dir>\n"); return 2; }
	if (!cl_library_load(argv[1])) { fprintf(stderr, "couldn't load the library\n"); return 1; }
	const std::string dir = argv[2];

	struct T { const char* id; const char* kind; double clockY; };
	const T templates[] = {
		{ "jk", "jk", 0 }, { "jk-nt", "jk", 0 }, { "t", "t", 0 },
		{ "d", "d", -1 }, { "d-nt", "d", -1 }, { "d-ce", "dce", 0 },
	};
	std::string jkText;
	for (const T& t : templates) {
		const std::string name = std::string("ff-") + t.id;
		std::string text;
		if (!readFile(dir + "/template-builtin-ff-" + t.id + ".cdl", text)) { check(false, name + ": template file"); continue; }
		if (strcmp(t.id, "jk") == 0) jkText = text;
		check(text.find("(lparam \"MANUAL\" \"true\")") != std::string::npos, name + ": the clock is saved as manual");
		check(text.find("\"Clock\"") != std::string::npos && text.find("The K key steps the clock. Turn PRE' or CLR' off") != std::string::npos,
		      name + ": the labels name the K key");
		CLDocument* doc = openText(text);
		if (!doc) { failures++; continue; }
		check(cl_document_manual_clock_count(doc, 0) == 1, name + ": one manual clock on the page");
		const long gate = clockGate(doc, t.clockY);
		check(setting(doc, gate, "MANUAL") == "Only on Step Clock|BOOL|true", name + ": the clock's settings show Only on Step Clock, on");
		cl_edit_select_none(doc, 0);

		if (strcmp(t.kind, "jk") == 0) {
			flip(doc, 8); flip(doc, -8);   // J = K = 1
			holdsStill(doc, name);
			toggles(doc, name);
			runsAgain(doc, gate, name);
		} else if (strcmp(t.kind, "t") == 0) {
			flip(doc, 8);                  // T = 1
			holdsStill(doc, name);
			toggles(doc, name);
			runsAgain(doc, gate, name);
		} else if (strcmp(t.kind, "d") == 0) {
			flip(doc, 8);                  // D = 1, not yet taken
			holdsStill(doc, name);
			check(!q(doc), name + ": D = 1 isn't taken without a Step Clock");
			captures(doc, name, 8);
		} else {
			flip(doc, 8);                  // D = 1 with CE = 0: nothing taken
			const bool q0 = q(doc);
			cl_document_clock_step(doc, 0);
			cl_document_clock_step(doc, 0);
			check(q(doc) == q0, name + ": with CE = 0, Step Clock leaves Q alone");
			flip(doc, -8);                 // CE = 1
			captures(doc, name, 8);
		}
		cl_document_close(doc);

		// The picker's running choice: the same circuit with a running clock.
		std::string running;
		if (!readFile(dir + "/template-builtin-ff-" + t.id + "-running.cdl", running)) { check(false, name + "-running: template file"); continue; }
		check(running.find("MANUAL") == std::string::npos, name + "-running: no MANUAL saved");
		check(running.find("The clock runs by itself.") != std::string::npos, name + "-running: the hint says it runs");
		doc = openText(running);
		if (!doc) { failures++; continue; }
		check(cl_document_manual_clock_count(doc, 0) == 0, name + "-running: no manual clock");
		if (strcmp(t.kind, "jk") == 0 || strcmp(t.kind, "t") == 0) {
			flip(doc, 8);
			if (strcmp(t.kind, "jk") == 0) flip(doc, -8);
			int changes = 0;
			bool prev = q(doc);
			for (int i = 0; i < 200; i++) { cl_document_step(doc); if (q(doc) != prev) { changes++; prev = q(doc); } }
			check(changes >= 10, name + "-running: Q toggles by itself (" + std::to_string(changes) + " changes in 200 steps)");
		}
		cl_document_close(doc);
		pulseChoice(dir, t.id, t.kind, t.clockY);
	}

	// A new clock: no MANUAL in its file (off isn't saved, so a running
	// clock's file and sync's digest match older versions). Turning it on
	// saves it; undo turns it off again, redo back on.
	{
		CLDocument* doc = cl_document_new();
		CLBuildGate gates[] = { { "BB_CLOCK", 0, 0, nullptr } };
		check(cl_edit_build(doc, 0, gates, 1, nullptr, 0, "Test") == 1, "new clock: built");
		const long gate = cl_edit_single_gate(doc, 0);
		check(setting(doc, gate, "MANUAL") == "Only on Step Clock|BOOL|false", "new clock: Only on Step Clock shows off");
		std::string text = cl_document_save_text(doc);
		check(text.find("MANUAL") == std::string::npos && text.find("HALF_CYCLE") != std::string::npos,
		      "new clock: saved without MANUAL");
		check(cl_document_manual_clock_count(doc, 0) == 0, "new clock: not manual");
		check(cl_gate_set_setting(doc, gate, "MANUAL", "true"), "new clock: turned on");
		text = cl_document_save_text(doc);
		check(cl_document_manual_clock_count(doc, 0) == 1 && text.find("(lparam \"MANUAL\" \"true\")") != std::string::npos,
		      "new clock: on, and saved as true");
		check(cl_edit_undo(doc), "new clock: undo");
		text = cl_document_save_text(doc);
		check(cl_document_manual_clock_count(doc, 0) == 0 && text.find("MANUAL") == std::string::npos,
		      "new clock: after undo, off and saved without MANUAL");
		check(!cl_document_clock_step(doc, 0), "new clock: after undo, Step Clock has nothing to step");
		check(cl_edit_redo(doc), "new clock: redo");
		text = cl_document_save_text(doc);
		check(cl_document_manual_clock_count(doc, 0) == 1 && text.find("(lparam \"MANUAL\" \"true\")") != std::string::npos,
		      "new clock: after redo, on again");
		cl_document_close(doc);
	}

	// A file that says MANUAL "false" (by hand, or another app): saved
	// back without it.
	{
		CLDocument* doc = cl_document_new();
		CLBuildGate gates[] = { { "BB_CLOCK", 0, 0, nullptr } };
		cl_edit_build(doc, 0, gates, 1, nullptr, 0, "Test");
		std::string text = cl_document_save_text(doc);
		cl_document_close(doc);
		const size_t at = text.find("(lparam \"HALF_CYCLE\"");
		if (at != std::string::npos) text.insert(at, "(lparam \"MANUAL\" \"false\") ");
		check(text.find("MANUAL") != std::string::npos, "MANUAL false file: made");
		doc = openText(text);
		if (doc) {
			check(cl_document_manual_clock_count(doc, 0) == 0, "MANUAL false file: a running clock");
			check(std::string(cl_document_save_text(doc)).find("MANUAL") == std::string::npos,
			      "MANUAL false file: saved without it");
			cl_document_close(doc);
		}
	}

	// Save and reopen: the setting stays, either way.
	if (!jkText.empty()) {
		CLDocument* doc = openText(jkText);
		const std::string saved = cl_document_save_text(doc);
		CLDocument* again = openText(saved);
		check(again && cl_document_manual_clock_count(again, 0) == 1, "save and reopen: still a manual clock");
		if (again) {
			flip(again, 8); flip(again, -8);
			holdsStill(again, "save and reopen");
			cl_document_close(again);
		}
		const long gate = clockGate(doc, 0);
		cl_gate_set_setting(doc, gate, "MANUAL", "false");
		const std::string off = cl_document_save_text(doc);
		check(off.find("MANUAL") == std::string::npos, "save with the setting off: not written");
		again = openText(off);
		check(again && cl_document_manual_clock_count(again, 0) == 0, "save and reopen with it off: a running clock");
		if (again) cl_document_close(again);
		cl_document_close(doc);

		// A file from before the setting (no MANUAL at all): a running clock.
		std::string old = jkText;
		const std::string tag = "(lparam \"MANUAL\" \"true\")";
		const size_t at = old.find(tag);
		if (at != std::string::npos) old.erase(at, tag.size());
		doc = openText(old);
		if (doc) {
			check(cl_document_manual_clock_count(doc, 0) == 0, "a file without MANUAL: no manual clock");
			flip(doc, 8); flip(doc, -8);
			int changes = 0;
			bool prev = q(doc);
			for (int i = 0; i < 200; i++) { cl_document_step(doc); if (q(doc) != prev) { changes++; prev = q(doc); } }
			check(changes >= 10, "a file without MANUAL: its clock runs as before");
			cl_document_close(doc);
		}

		// Step Clock with a running clock on the page too: the manual one still
		// makes its cycle, and the settle cap keeps it quick.
		doc = openText(jkText);
		if (doc) {
			// A running clock lighting a light, off to the side (below, so Q
			// and Q' stay the first two lights).
			CLBuildGate gates[] = { { "BB_CLOCK", 60, -30, nullptr }, { "GA_LED", 72, -30, nullptr } };
			CLBuildWire wires[] = { { 0, "CLK", 1, "N_in0" } };
			check(cl_edit_build(doc, 0, gates, 2, wires, 1, "Test") == 2, "while running: a running clock added");
			cl_edit_select_none(doc, 0);
			check(cl_document_manual_clock_count(doc, 0) == 1, "while running: still one manual clock");
			cl_document_set_running(doc, true);
			flip(doc, 8); flip(doc, -8);
			auto t0 = std::chrono::steady_clock::now();
			toggles(doc, "while running");
			const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
			check(ms < 2000, "while running: 8 Step Clocks took " + std::to_string((int)ms) + " ms");
			int changes = 0;
			bool prev = chips(doc).out.size() > 2 && chips(doc).out[2];
			for (int i = 0; i < 40; i++) {
				cl_document_step(doc);
				const bool now = chips(doc).out.size() > 2 && chips(doc).out[2];
				if (now != prev) { changes++; prev = now; }
			}
			check(changes >= 4, "while running: the running clock's light still blinks (" + std::to_string(changes) + " changes in 40 steps)");
			cl_document_close(doc);
		}
	}

	printf("%d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
