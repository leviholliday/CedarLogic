// Regression checks for the engine's hardening (docs/CORE-HARDENING.md): each
// bug the fuzzers found gets a case here, small enough to read. Headless: it
// builds with the rest of mac/Tools/headless and runs in CI under the sanitizers.
//   core_check <cl_gatedefs.xml> [fixtures-dir]
#include "DocumentImpl.h"
#include "guiGate.h"
#include "guiWire.h"
#include "route/WireRoute.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(c, what) do { if (c) printf("  ok   %s\n", what); else { printf("  FAIL %s\n", what); fails++; } } while (0)

static std::string fixtures;

static long newest(CLDocument* doc, int page) {
	long best = -1;
	for (auto& g : *doc->page(page)->getGateList())
		if (g.second && (long)g.first > best) best = (long)g.first;
	return best;
}

static std::string setting(CLDocument* doc, long gate, const char* name) {
	for (int i = 0; i < cl_gate_setting_count(doc, gate); i++) {
		CLGateSetting s;
		if (cl_gate_setting(doc, gate, i, &s) && strcmp(s.name, name) == 0) return s.value;
	}
	return "(no such setting)";
}

static CLDocument* reopen(CLDocument* doc) {
	const std::string text = cl_document_save_text(doc);
	char err[256] = "";
	return cl_document_open_text(text.data(), (long)text.size(), err, sizeof err);
}

static std::string readFile(const std::string& path) {
	std::ifstream in(path, std::ios::binary);
	std::ostringstream s;
	s << in.rdbuf();
	return s.str();
}

// ---- Text typed into a part comes back exactly ----------------------------------

static void textRoundTrip() {
	printf("text typed into labels, links and pages saves and reopens exactly\n");
	const char* const texts[] = {
		"back\\slash", "\\", "\\\\", "ends with \\", "\\\"", "\"quoted\"", "<version>", "a < b > c",
		"&amp; & &lt;", "two\nlines", "tab\there", "caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80", "(paren)", ")(",
		"C:\\Users\\student\\lab.cdl", "trailing space ", " leading space", "",
	};
	std::string longText(20000, 'x');
	longText += "\\end";
	std::vector<std::string> all(std::begin(texts), std::end(texts));
	all.push_back(longText);
	int bad = 0;
	for (const std::string& t : all) {
		CLDocument* doc = cl_document_new();
		cl_edit_add_gate(doc, 0, "AA_LABEL", 0, 0);
		const long label = newest(doc, 0);
		cl_edit_add_gate(doc, 0, "DE_TO", 10, 0);
		const long to = newest(doc, 0);
		cl_gate_set_setting(doc, label, "LABEL_TEXT", t.c_str());
		cl_gate_set_setting(doc, to, "JUNCTION_ID", t.c_str());
		cl_document_rename_page(doc, 0, t.c_str());
		const std::string first = cl_document_save_text(doc);
		CLDocument* back = reopen(doc);
		bool ok = back != nullptr;
		if (ok) {
			ok = setting(back, label, "LABEL_TEXT") == t && setting(back, to, "JUNCTION_ID") == t &&
			     std::string(cl_document_save_text(back)) == first;
			// Copy and paste carries it too (the clipboard has a text format of its own).
			cl_edit_select_all(back, 0);
			const std::string copied = cl_edit_copy(back, 0);
			const char* why = nullptr;
			cl_edit_paste(back, 0, copied.c_str(), 30, 30, false, &why);
			const long pasted = newest(back, 0);
			ok = ok && pasted > to && (setting(back, pasted, "LABEL_TEXT") == t || setting(back, pasted, "JUNCTION_ID") == t);
			cl_document_close(back);
		}
		if (!ok) { printf("    lost: \"%.40s\"\n", t.c_str()); bad++; }
		cl_document_close(doc);
	}
	CHECK(bad == 0, "every text came back exactly, through save and through copy/paste");
}

// ---- Helpers for the cases below ------------------------------------------------

static bool consistent(CLDocument* doc, const char* when) {
	char why[512] = "";
	if (cl_document_check(doc, why, sizeof why)) return true;
	printf("    inconsistent %s: %s\n", when, why);
	return false;
}

static CLDocument* openText(const std::string& text, std::string* error = nullptr) {
	char err[512] = "";
	CLDocument* doc = cl_document_open_text(text.data(), (long)text.size(), err, sizeof err);
	if (error) *error = err;
	return doc;
}

// Saved, reopened and saved again: the same text.
static bool saveIsStable(CLDocument* doc) {
	const std::string a = cl_document_save_text(doc);
	CLDocument* back = openText(a);
	if (back == nullptr) return false;
	const std::string b = cl_document_save_text(back);
	cl_document_close(back);
	if (a != b) printf("    saves differ (%zu vs %zu bytes)\n", a.size(), b.size());
	return a == b;
}

static int noticeCount(CLDocument* doc) { return (int)doc->notices.size(); }

static std::string page0(const std::string& body) {
	return "(cedarlogic\n  (version 3)\n  (generator \"core_check\")\n  (page 0\n" + body + "))\n";
}

static std::string gate(const char* type, const char* id, const char* x, const char* y, const std::string& extra = "") {
	return std::string("    (gate \"") + type + "\" (uuid \"" + id + "\") (at " + x + " " + y + ") (angle 0)" + extra + ")\n";
}

// ---- Files that are damaged or crafted --------------------------------------------

static void hostileFiles() {
	printf("damaged and crafted files open safely or are refused\n");
	std::string err;

	CLDocument* doc = openText(page0(gate("AA_TOGGLE", "1", "nan", "0")), &err);
	CHECK(doc == nullptr && err.find("out of range") != std::string::npos, "a position of nan is refused with a message");
	if (doc) cl_document_close(doc);
	doc = openText(page0(gate("AA_TOGGLE", "1", "inf", "0")));
	CHECK(doc == nullptr, "an infinite position is refused");
	if (doc) cl_document_close(doc);

	doc = openText(page0(gate("AA_TOGGLE", "1", "1e30", "-4e12")));
	CHECK(doc != nullptr, "a part far outside the page opens");
	if (doc) {
		double x = 0, y = 0;
		cl_gate_position(doc, 1, &x, &y);
		CHECK(std::fabs(x) <= WORLD_LIMIT && std::fabs(y) <= WORLD_LIMIT && noticeCount(doc) > 0, "... brought to the edge, with a notice");
		CHECK(saveIsStable(doc), "... and saves stably");
		cl_document_close(doc);
	}

	// A memory's READ_FILE / WRITE_FILE from a file: never acted on.
	const std::string target = fixtures.empty() ? "/tmp/cl-core-check-written.cdm" : fixtures + "/../cl-core-check-written.cdm";
	remove(target.c_str());
	doc = openText(page0(gate("AE_RAM_8x8", "1", "0", "0", " (lparam \"WRITE_FILE\" \"" + target + "\") (lparam \"READ_FILE\" \"/etc/hosts\")")));
	FILE* written = fopen(target.c_str(), "r");
	CHECK(doc != nullptr && written == nullptr, "a file's WRITE_FILE setting writes nothing");
	if (written) { fclose(written); remove(target.c_str()); }
	if (doc) {
		CHECK(std::string(cl_document_save_text(doc)).find("hosts") == std::string::npos, "... and READ_FILE reads nothing");
		cl_document_close(doc);
	}

	doc = openText(page0(gate("AA_AND2", "1", "0", "0", " (lparam \"INPUT_BITS\" \"999999999\")") +
	                     gate("AA_AND2", "2", "10", "0", " (lparam \"INPUT_BITS\" \"-1\")")));
	CHECK(doc != nullptr, "a part with a width of 999999999 (and one of -1) opens");
	if (doc) {
		CHECK(doc->circuit.getGate(1)->getLogicParam("INPUT_BITS") == "32" && doc->circuit.getGate(2)->getLogicParam("INPUT_BITS") == "0",
		      "... held to 32 and 0");
		cl_document_set_running(doc, true);
		for (int i = 0; i < 5; i++) cl_document_tick(doc, 50);
		CHECK(consistent(doc, "after running") && saveIsStable(doc), "... runs, and saves stably");
		cl_document_close(doc);
	}

	doc = openText(page0(gate("AA_TOGGLE", "1", "0", "0") + gate("GA_LED", "1", "10", "0") + gate("GA_LED", "007", "20", "0") +
	                     gate("GA_LED", "99999999999999999999", "30", "0")));
	CHECK(doc != nullptr && doc->circuit.getGate(7) != nullptr && doc->page(0)->getGateList()->size() == 2 && noticeCount(doc) >= 2,
	      "a second gate with a used id, and an id too large, are left out with notices");
	if (doc) { CHECK(consistent(doc, "after open") && saveIsStable(doc), "... and the rest saves stably"); cl_document_close(doc); }

	// Wires: to a gate that isn't there, to a pin the gate doesn't have, a pin
	// claimed by two wires, a crossing with nothing, a segment far off its pin,
	// a segment id far out of range.
	const std::string parts = gate("AA_TOGGLE", "1", "0", "0") + gate("AA_INVERTER", "2", "10", "0") + gate("GA_LED", "3", "20", "0");
	const char* wires[] = {
		"    (wire (ids \"5\") (seg \"0\" h (pts 1.5 0 7 0) (connect \"1\" \"OUT_0\") (connect \"9\" \"IN_0\")))\n",
		"    (wire (ids \"5\") (seg \"0\" h (pts 1.5 0 7 0) (connect \"1\" \"OUT_0\") (connect \"2\" \"NOPE\")))\n",
		"    (wire (ids \"5\") (seg \"0\" h (pts 1.5 0 7 0) (connect \"1\" \"OUT_0\") (connect \"2\" \"IN_0\")))\n"
		"    (wire (ids \"6\") (seg \"0\" h (pts 1.5 0 7 0) (connect \"1\" \"OUT_0\") (connect \"3\" \"N_in0\")))\n",
		"    (wire (ids \"5\") (seg \"0\" h (pts 1.5 0 7 0) (connect \"1\" \"OUT_0\") (connect \"2\" \"IN_0\") (cross 3 \"44\")))\n",
		"    (wire (ids \"5\") (seg \"0\" h (pts 100 50 200 50) (connect \"1\" \"OUT_0\") (connect \"2\" \"IN_0\")))\n",
		"    (wire (ids \"5\") (seg \"9223372036854775807\" h (pts 1.5 0 7 0) (connect \"1\" \"OUT_0\") (connect \"2\" \"IN_0\")))\n",
		"    (wire (ids \"5\") (seg \"0\" h (pts 1.5 0 7 0) (connect \"1\" \"OUT_0\")) (seg \"1\" h (pts 1.5 0 7 0) (connect \"2\" \"IN_0\")))\n",
		"    (wire (ids \"5 5\") (seg \"0\" h (pts 1.5 0 7 0) (connect \"1\" \"OUT_0\") (connect \"2\" \"IN_0\")))\n",
	};
	int bad = 0;
	for (const char* w : wires) {
		doc = openText(page0(parts + w));
		if (doc == nullptr) continue;   // refused: fine
		if (!consistent(doc, "after open") || !saveIsStable(doc)) { printf("    with: %s", w); bad++; }
		cl_document_close(doc);
	}
	CHECK(bad == 0, "damaged wires open consistent (or are refused), and save stably");

	// Deep nesting, a NUL byte, angles that aren't usable, an enormous file.
	doc = openText("(cedarlogic (version 3) (generator \"x\") " + std::string(100000, '(') + std::string(100000, ')') + ")");
	CHECK(doc == nullptr, "100,000 levels of nesting are refused (no stack overflow)");
	std::string withNul = page0(gate("AA_LABEL", "1", "0", "0", " (gparam \"LABEL_TEXT\" \"ab\")"));
	withNul.insert(withNul.find("\"ab\"") + 2, 1, '\0');
	doc = openText(withNul);
	CHECK(doc != nullptr && saveIsStable(doc), "a NUL byte in a label is dropped, and the save reads back");
	if (doc) cl_document_close(doc);
	doc = openText(page0(gate("AA_TOGGLE", "1", "0", "0") +
	                     "    (gate \"AA_TOGGLE\" (uuid \"2\") (at 5 5) (angle 1e-320))\n"
	                     "    (gate \"AA_TOGGLE\" (uuid \"3\") (at 9 9) (angle -2147483647.5))\n"));
	CHECK(doc != nullptr && saveIsStable(doc), "tiny and huge angles open, and save stably");
	if (doc) cl_document_close(doc);
	std::string big;
	for (int i = 1; i <= 3000; i++) big += gate("GA_LED", std::to_string(i).c_str(), std::to_string(i % 100).c_str(), std::to_string(i / 100).c_str());
	doc = openText(page0(big));
	CHECK(doc != nullptr && doc->page(0)->getGateList()->size() == 3000, "3,000 parts open");
	if (doc) {
		cl_edit_select_all(doc, 0);
		cl_edit_delete(doc, 0);
		cl_edit_undo(doc);
		CHECK(doc->page(0)->getGateList()->size() == 3000, "... and delete all, then undo, brings them back");
		cl_document_close(doc);
	}
}

// ---- Clipboard text from anywhere ---------------------------------------------------

static void hostileClipboard() {
	printf("damaged clipboard text pastes safely\n");
	const char* texts[] = {
		"movewire 1 hsegment 0 0,0,1,0 connection\n",
		"setparams 1 4000000000,4000000000 A\n",
		"createwire connectwire 1 1 OUT_0 connectwire 1 2 IN_0\n",
		"creategate 1 AA_TOGGLE 0 0\nconnectwire 77 1 OUT_0\nsetparams 55 1,0 angle nan\t\n",
		"creategate 1 AA_TOGGLE 0 0\ncreategate 1 AA_TOGGLE 4 0\ncreategate 2 GA_LED 10 0\n"
		"createwire 5 connectwire 5 1 OUT_0 connectwire 5 2 N_in0\ncreatewire 5 connectwire 5 1 OUT_0 connectwire 5 2 N_in0\n",
		"creategate 1 AA_TOGGLE 1e30 nan\ncreategate 2 GA_LED 10 0\n"
		"createwire 5 connectwire 5 1 OUT_0 connectwire 5 2 N_in0\nmovewire 5 hsegment 9223372036854775807 0,0,1,0 connection 1 OUT_0 isect 3 44  done \n",
		"creategate 1 AE_RAM_8x8 0 0\nsetparams 1 0,3 DATA_BITS 4000000000\tREAD_FILE x\tWRITE_FILE /tmp/cl-core-check-paste.cdm\t\n",
	};
	int bad = 0;
	for (const char* t : texts) {
		CLDocument* doc = cl_document_new();
		cl_edit_add_gate(doc, 0, "AA_TOGGLE", -20, 0);
		const char* back = nullptr;
		cl_edit_paste(doc, 0, t, 5, 5, false, &back);
		bool ok = consistent(doc, "after the paste");
		while (cl_edit_can_undo(doc)) cl_edit_undo(doc);
		while (cl_edit_can_redo(doc)) cl_edit_redo(doc);
		ok = ok && consistent(doc, "after undo all, redo all") && saveIsStable(doc);
		if (!ok) { printf("    pasting: %s", t); bad++; }
		cl_document_close(doc);
	}
	FILE* written = fopen("/tmp/cl-core-check-paste.cdm", "r");
	CHECK(bad == 0 && written == nullptr, "each pastes (or not) with the circuit consistent, undoes and saves stably, and writes no file");
	if (written) fclose(written);

	// A lone pasted TO/FROM counts its name on, as text: past what an int holds too.
	CLDocument* doc = cl_document_new();
	cl_edit_add_gate(doc, 0, "DE_TO", 0, 0);
	const long to = newest(doc, 0);
	cl_gate_set_setting(doc, to, "JUNCTION_ID", "bus99999999999999999999");
	cl_edit_select_all(doc, 0);
	std::string text = cl_edit_copy(doc, 0);
	const char* next = nullptr;
	cl_edit_paste(doc, 0, text.c_str(), 10, 10, false, &next);
	CHECK(setting(doc, newest(doc, 0), "JUNCTION_ID") == "bus100000000000000000000", "a pasted link named bus99999999999999999999 becomes bus100000000000000000000");
	cl_document_close(doc);
}

// ---- The simulation at its edges ----------------------------------------------------

static void simulationEdges() {
	printf("the simulation at its edges\n");
	// An inverter feeding itself, with no delay: oscillates for ever, so each
	// step must stop on its own.
	std::string loop = page0(gate("AA_INVERTER", "1", "0", "0", " (lparam \"DEFAULT_DELAY\" \"0\")") +
	                         "    (wire (ids \"5\") (seg \"0\" h (pts -3 0 3 0) (connect \"1\" \"IN_0\") (connect \"1\" \"OUT_0\")))\n");
	CLDocument* doc = openText(loop);
	CHECK(doc != nullptr, "a zero-delay inverter loop opens (its settle stops)");
	if (doc) {
		cl_document_set_running(doc, true);
		for (int i = 0; i < 200; i++) cl_document_tick(doc, 50);
		cl_document_step(doc);
		CHECK(consistent(doc, "after running"), "... and runs 10 s of ticks");
		cl_document_close(doc);
	}

	// A counter whose MAX_COUNT is the largest number (maxCount + 1 is 0, and
	// counting up divided by it), an encoder with no inputs (log 0), a decoder
	// asked for 32 inputs (four billion outputs).
	auto wire = [](const char* id, const char* g1, const char* p1, const char* g2, const char* p2) {
		// Any shape: one that misses its pins is redrawn from them on open.
		return std::string("    (wire (ids \"") + id + "\") (seg \"0\" h (pts 0 0 1 0) (connect \"" + g1 + "\" \"" + p1 +
		       "\") (connect \"" + g2 + "\" \"" + p2 + "\")))\n";
	};
	doc = openText(page0(gate("AA_REGISTER4", "1", "0", "0", " (lparam \"MAX_COUNT\" \"18446744073709551615\") (lparam \"CURRENT_VALUE\" \"18446744073709551614\")") +
	                     gate("BB_CLOCK", "2", "-20", "0", " (lparam \"HALF_CYCLE\" \"1\")") +
	                     gate("AA_TOGGLE", "3", "-20", "6", " (lparam \"OUTPUT_NUM\" \"1\")") +
	                     gate("AA_TOGGLE", "4", "-20", "12", " (lparam \"OUTPUT_NUM\" \"1\")") +
	                     gate("CB_PRI_ENCODER_4x2", "8", "20", "0", " (lparam \"INPUT_BITS\" \"0\")") +
	                     gate("BA_DECODER_2x4", "9", "40", "0", " (lparam \"INPUT_BITS\" \"32\")") +
	                     wire("5", "2", "CLK", "1", "clock") + wire("6", "3", "OUT_0", "1", "count_enable") + wire("7", "4", "OUT_0", "1", "count_up")));
	CHECK(doc != nullptr, "a counter to the largest number, an encoder with no inputs and a 32-input decoder open");
	if (doc) {
		CHECK(doc->circuit.getGate(1)->isConnected("clock") && doc->circuit.getGate(1)->isConnected("count_up"), "... the counter wired up");
		cl_document_set_running(doc, true);
		for (int i = 0; i < 100; i++) cl_document_tick(doc, 50);
		CHECK(consistent(doc, "after running"), "... and count and run for 5 s");
		cl_document_close(doc);
	}

	// Pages with nothing on them.
	doc = cl_document_new();
	cl_document_add_page(doc);
	cl_document_add_page(doc);
	cl_document_set_running(doc, true);
	for (int i = 0; i < 20; i++) cl_document_tick(doc, 50);
	cl_document_step(doc);
	CHECK(cl_document_page_count(doc) == 3 && saveIsStable(doc), "three empty pages run and save stably");
	cl_document_close(doc);

	// A RAM read at every address: reading makes no entries (the memory had one
	// per address read, all saved with the circuit).
	doc = openText(page0(gate("AE_RAM_8x8", "1", "0", "0")));
	CHECK(doc != nullptr, "a RAM opens");
	if (doc) {
		guiGate* ram = doc->circuit.getGate(1);
		const size_t before = std::string(cl_document_save_text(doc)).size();
		for (int a = 0; a < 256; a++) {
			doc->circuit.sendMessageToCore(klsMessage::Message(klsMessage::MT_SET_GATE_PARAM,
				new klsMessage::Message_SET_GATE_PARAM(1, "lastRead", std::to_string(a))));
			cl_document_tick(doc, 20);
		}
		(void)ram;
		CHECK(std::string(cl_document_save_text(doc)).size() == before, "... and reading it leaves the save as it was");
		cl_document_close(doc);
	}

	// A Pauseulator's pause is a request, not saved with the circuit.
	doc = openText(page0(gate("AA_TOGGLE", "1", "0", "0", " (lparam \"OUTPUT_NUM\" \"1\")") + gate("Pauseulator", "2", "10", "0") +
	                     "    (wire (ids \"5\") (seg \"0\" h (pts 1.5 0 5 0) (connect \"1\" \"OUT_0\") (connect \"2\" \"signal\")))\n"));
	CHECK(doc != nullptr, "a Pauseulator on a switch opens");
	if (doc) {
		cl_document_set_running(doc, true);
		for (int i = 0; i < 10; i++) cl_document_tick(doc, 50);
		CHECK(std::string(cl_document_save_text(doc)).find("PAUSE_SIM") == std::string::npos && saveIsStable(doc),
		      "... and its PAUSE_SIM isn't saved");
		cl_document_close(doc);
	}

	// Undo and redo, over and over, after a busy session.
	doc = cl_document_new();
	for (int i = 0; i < 30; i++) cl_edit_add_gate(doc, 0, i % 2 ? "AA_AND2" : "AA_TOGGLE", (i % 6) * 8.0, (i / 6) * 8.0);
	cl_edit_select_all(doc, 0);
	std::string all = cl_edit_copy(doc, 0);
	const char* back = nullptr;
	cl_edit_paste(doc, 0, all.c_str(), 60, 0, false, &back);
	cl_edit_select_all(doc, 0);
	cl_edit_rotate(doc, 0);
	cl_edit_delete(doc, 0);
	bool ok = true;
	for (int round = 0; round < 50 && ok; round++) {
		while (cl_edit_can_undo(doc)) cl_edit_undo(doc);
		ok = consistent(doc, "after undo all");
		while (cl_edit_can_redo(doc)) cl_edit_redo(doc);
		ok = ok && consistent(doc, "after redo all");
	}
	CHECK(ok && saveIsStable(doc), "50 rounds of undo all, redo all");
	cl_document_close(doc);
}

// ---- Wires ------------------------------------------------------------------------

static void wireCases() {
	printf("wires\n");
	// A RAM's DATA_IN_n and DATA_OUT_n share a spot, and a wire holds both. Taking
	// that spot off a wire with one other pin left a wire holding one pin, whose
	// shape still recorded the two taken off.
	CLDocument* doc = cl_document_new();
	cl_edit_add_gate(doc, 0, "AE_RAM_8x8", 0, 0);
	const long ram = newest(doc, 0);
	cl_edit_add_gate(doc, 0, "AA_TOGGLE", -20, 0);
	const long sw = newest(doc, 0);
	guiGate* r = doc->circuit.getGate(ram);
	guiGate* t = doc->circuit.getGate(sw);
	float rx = 0, ry = 0, tx = 0, ty = 0;
	r->getHotspotCoords("DATA_IN_0", rx, ry);
	t->getHotspotCoords("OUT_0", tx, ty);
	cl_edit_press(doc, 0, tx, ty, 0, 0.05);
	cl_edit_drag(doc, (tx + rx) / 2, (ty + ry) / 2);
	cl_edit_drag(doc, rx, ry);
	cl_edit_release(doc, rx, ry);
	const bool joined = r->isConnected("DATA_IN_0") && r->isConnected("DATA_OUT_0");
	CHECK(joined, "a wire to a RAM's data spot holds DATA_IN_0 and DATA_OUT_0");
	cl_edit_disconnect_pin(doc, 0, rx, ry, 0.05);
	CHECK(!r->isConnected("DATA_IN_0") && !r->isConnected("DATA_OUT_0") && !t->isConnected("OUT_0") && consistent(doc, "after Disconnect"),
	      "Disconnect on that spot takes the whole wire (one pin would be left)");
	cl_edit_undo(doc);
	CHECK(r->isConnected("DATA_IN_0") && consistent(doc, "after Undo"), "... and Undo puts it back");
	cl_document_close(doc);
}

// ---- Smaller things the fuzzers found ------------------------------------------------

static void smallerCases() {
	printf("smaller things\n");
	// A version or page number past an int: refused or left out, not wrapped.
	CLDocument* doc = openText("(cedarlogic (version 4294967299) (generator \"x\") (page 0))");
	CHECK(doc == nullptr, "a version of 4294967299 isn't read as 3");
	if (doc) cl_document_close(doc);
	doc = openText("(cedarlogic (version 3) (generator \"x\") (page 4294967296" + gate("AA_TOGGLE", "1", "0", "0") + "))");
	CHECK(doc != nullptr && doc->page(0)->getGateList()->empty(), "a page 4294967296 isn't read as page 0");
	if (doc) cl_document_close(doc);

	// A register as wide as a bus may be shows 8 digits, not a crash.
	doc = openText(page0(gate("AA_REGISTER4", "1", "0", "0", " (lparam \"INPUT_BITS\" \"64\") (lparam \"CURRENT_VALUE\" \"-7\")")));
	CHECK(doc != nullptr && consistent(doc, "after open"), "a register 64 bits wide opens");
	if (doc) cl_document_close(doc);

	// The same crossing kept under two positions (float noise): kept once.
	const std::string parts = gate("AA_TOGGLE", "1", "0", "0") + gate("AA_INVERTER", "2", "10", "10") + gate("GA_LED", "3", "20", "-10");
	doc = openText(page0(parts + "    (wire (ids \"5\")\n"
		"      (seg \"0\" v (pts 4 -10 4 10) (cross -10 \"2\") (cross 0 \"1\") (cross 0.0000004 \"1\") (cross 10 \"3\"))\n"
		"      (seg \"1\" h (pts 1.5 0 4 0) (connect \"1\" \"OUT_0\") (cross 4 \"0\"))\n"
		"      (seg \"2\" h (pts 4 -10 19.24 -10) (connect \"3\" \"N_in0\") (cross 4 \"0\"))\n"
		"      (seg \"3\" h (pts 4 10 7 10) (connect \"2\" \"IN_0\") (cross 4 \"0\")))\n"));
	CHECK(doc != nullptr && consistent(doc, "after open") && saveIsStable(doc), "a crossing recorded twice opens once, and saves stably");
	if (doc) cl_document_close(doc);

	// Routing a straight trunk with the next id 0 gave a branch the trunk's id.
	cl::route::RouteInput in;
	in.pins = { { 0, 0, false }, { 10, 5, false }, { 0, 10, false } };
	in.snapTrunk = true;
	in.nextId = 0;
	cl::route::RouteResult out = cl::route::TrunkRouter().route(in);
	bool unique = true;
	for (size_t i = 0; i < out.segments.size(); i++)
		for (size_t j = i + 1; j < out.segments.size(); j++) unique = unique && out.segments[i].id != out.segments[j].id;
	CHECK(unique && out.segments.size() == 4, "a three-pin trunk route gives every segment its own id");

	// A rotated part's pins sit a few millionths off the grid: saves still agree.
	doc = cl_document_new();
	cl_edit_add_gate(doc, 0, "AA_AND2", 0, 0);
	guiGate* a = doc->circuit.getGate(newest(doc, 0));
	cl_edit_add_gate(doc, 0, "GA_LED", 12, 7);
	guiGate* led = doc->circuit.getGate(newest(doc, 0));
	cl_edit_select_none(doc, 0);
	a->select();
	for (int i = 0; i < 3; i++) cl_edit_rotate(doc, 0);
	float ax = 0, ay = 0, lx = 0, ly = 0;
	a->getHotspotCoords("OUT", ax, ay);
	led->getHotspotCoords("N_in0", lx, ly);
	cl_edit_press(doc, 0, ax, ay, 0, 0.05);
	cl_edit_drag(doc, lx, ly);
	cl_edit_release(doc, lx, ly);
	CHECK(a->isConnected("OUT") && consistent(doc, "after wiring") && saveIsStable(doc), "a wire on a rotated part saves the same after reopening");
	cl_document_close(doc);

	// A part at a position finer than a save writes, wired: the part was saved
	// rounded one way and the wire's end at its pin the other (2.0001, then 2).
	doc = openText(page0(gate("AA_TOGGLE", "1", "0.00005", "0.00015") + gate("GA_LED", "2", "20.33335", "7.77775") +
	                     "    (wire (ids \"5\") (seg \"0\" h (pts 0 0 1 0) (connect \"1\" \"OUT_0\") (connect \"2\" \"N_in0\")))\n"));
	CHECK(doc != nullptr && saveIsStable(doc), "a wired part at 0.00005 saves the same after reopening");
	if (doc) cl_document_close(doc);

	// An adder or a register with no bits (or one) from a file, given inputs
	// and a clock: read past an empty bus (the adder's sign bit at index -1,
	// the register's shift into the top of no bits).
	const char* widths[] = { "0", "1" };
	for (const char* bits : widths) {
		const std::string width = std::string(" (lparam \"INPUT_BITS\" \"") + bits + "\")";
		const std::string on = " (lparam \"OUTPUT_NUM\" \"1\")";
		auto link = [](const char* id, const char* g1, const char* p1, const char* g2, const char* p2) {
			return std::string("    (wire (ids \"") + id + "\") (seg \"0\" h (pts 0 0 1 0) (connect \"" + g1 + "\" \"" + p1 +
			       "\") (connect \"" + g2 + "\" \"" + p2 + "\")))\n";
		};
		doc = openText(page0(gate("AE_FULLADDER_4BIT", "10", "0", "0", width) + gate("AA_REGISTER4", "20", "0", "20", width) +
		                     gate("BA_SHIFT_REGISTER_4", "30", "0", "40", width) + gate("BB_CLOCK", "1", "-20", "30", " (lparam \"HALF_CYCLE\" \"1\")") +
		                     gate("AA_TOGGLE", "2", "-20", "0", on) + gate("AA_TOGGLE", "3", "-20", "10", on) +
		                     gate("AA_TOGGLE", "4", "-20", "20", on) + gate("AA_TOGGLE", "6", "-20", "40", on) +
		                     link("50", "2", "OUT_0", "10", "carry_in") + link("51", "3", "OUT_0", "30", "shift_enable") +
		                     link("52", "4", "OUT_0", "20", "count_enable") + link("53", "6", "OUT_0", "30", "carry_in") +
		                     "    (wire (ids \"54\") (seg \"0\" h (pts 0 0 1 0) (connect \"1\" \"CLK\") (connect \"20\" \"clock\") (connect \"30\" \"clock\")))\n"));
		CHECK(doc != nullptr, (std::string("an adder and registers ") + bits + " bits wide open").c_str());
		if (!doc) continue;
		cl_document_set_running(doc, true);
		for (int i = 0; i < 60; i++) cl_document_tick(doc, 50);
		cl_document_step(doc);
		// (Not saveIsStable: opening runs a clocked counter on, by design.)
		CHECK(consistent(doc, "after running"), "... and add, count and shift for 3 s");
		cl_document_close(doc);
	}
}

// A part drag that broke a wire (fuzz_edit seed 147, a 2000-op session,
// saved just before with CL_FUZZ_SAVEBEFORE): the circuit, and the same drag.
// (Seed 104, a wire in two pieces, needs the session's own history to happen
// again; the fuzz runs in CI cover it.)
static void dragCases() {
	printf("a part drag never leaves a wire broken\n");
	struct Case { const char* file; int page; bool all; double x0, y0, x1, y1; int steps; const char* what; };
	const Case cases[] = {
		{ "drag-leaves-a-dot.cdl", 4, false, 22.50, 28.85, 21.10, 17.50, 5, "a wire on a part's pins leaves no run of no length" },
	};
	for (const Case& c : cases) {
		const std::string text = readFile(fixtures + "/../../../tests/fixtures/core/" + c.file);
		CLDocument* doc = openText(text);
		if (doc == nullptr) { CHECK(false, c.file); continue; }
		if (c.all) cl_edit_select_all(doc, c.page); else cl_edit_select_none(doc, c.page);
		cl_edit_press(doc, c.page, c.x0, c.y0, 0, 0.05);
		for (int i = 1; i <= c.steps; i++)
			cl_edit_drag(doc, c.x0 + (c.x1 - c.x0) * i / c.steps, c.y0 + (c.y1 - c.y0) * i / c.steps);
		cl_edit_release(doc, c.x1, c.y1);
		CHECK(consistent(doc, "after the drag") && saveIsStable(doc), c.what);
		cl_edit_undo(doc);
		CHECK(consistent(doc, "after Undo"), "... and Undo puts it back whole");
		cl_document_close(doc);
	}
}

// Straighten Wires on many wires at once, then Undo and Redo: Redo gave a
// run's end at -28.5 where the step itself had left -28.4989 (fuzz_edit wide
// seed 21, op 292; the circuit's first page, cut to the two wires it needs).
static void straightenCases() {
	printf("Redo of Straighten Wires gives what the step gave\n");
	const std::string text = readFile(fixtures + "/../../../tests/fixtures/core/straighten-redo.cdl");
	CLDocument* doc = openText(text);
	if (doc == nullptr) { CHECK(false, "straighten-redo.cdl opens"); return; }
	cl_edit_select_all(doc, 0);
	cl_edit_straighten(doc, 0);
	const std::string done = cl_document_save_text(doc);
	cl_edit_undo(doc);
	cl_edit_redo(doc);
	const std::string redone = cl_document_save_text(doc);
	CHECK(done == redone && consistent(doc, "after Redo"), "two wires straightened at once: Redo gives the same shapes");
	cl_document_close(doc);
}

int main(int argc, char** argv) {
	if (argc < 2 || !cl_library_load(argv[1])) { fprintf(stderr, "usage: core_check gatedefs [fixtures-dir]\n"); return 2; }
	if (argc > 2) fixtures = argv[2];
	cl_set_settle_on_open(true);

	textRoundTrip();
	hostileFiles();
	hostileClipboard();
	simulationEdges();
	wireCases();
	smallerCases();
	if (!fixtures.empty()) dragCases();
	if (!fixtures.empty()) straightenCases();

	printf(fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails ? 1 : 0;
}
