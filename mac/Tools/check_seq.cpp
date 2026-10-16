// Runs Check My Circuit's shared test cases (tests/check-sequential/cases.json,
// docs/CHECK-SEQUENTIAL.md §13) through CedarCore, as the Mac app does, and
// compares every field of every result:
//   check_seq <cl_gatedefs.xml> <cases.json> [case id...]
//   check_seq <cl_gatedefs.xml> --templates <dir with template-builtin-ff-*.cdl>
//
// Each circuit is opened as the app opens a file. Today's kinds go through
// cl_truth_table and cl_check_expected / cl_check_table; everything else
// through cl_check_clocked. A clocked case is also run a second time on a
// circuit that has been running for a while (its clocks ticking, a switch
// flipped and back), which must give the same result and leave the circuit
// as it was. The `detect` texts go through cl_check_key_kind.
//
// With --templates, the flip-flop templates `CedarLogic --render-ui <dir>`
// writes are checked against their textbook tables (their clocks are manual,
// as the app makes them).

#include "CedarCore.h"
#include "SyncJson.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using clsync::json::Value;

namespace {

const char* kindName(int k) {
	static const char* n[] = { "empty", "formula", "table", "count", "states", "timing" };
	return k >= 0 && k <= 5 ? n[k] : "?";
}

Value str(const std::string& s) { return Value::string(s); }
Value num(long v) { return Value::integer(v); }

Value commonFields(CLCheck* c, int kind) {
	Value r = Value::object();
	r.set("kind", str(kindName(kind)));
	r.set("verdict", num(cl_check_verdict(c)));
	r.set("error", str(cl_check_error(c)));
	r.set("summary", str(cl_check_summary(c)));
	Value notes = Value::array();
	for (int i = 0; i < cl_check_note_count(c); i++) {
		Value n = Value::array();
		n.push(num(cl_check_note_kind(c, i)));
		n.push(str(cl_check_note(c, i)));
		notes.push(n);
	}
	r.set("notes", notes);
	Value names = Value::array();
	for (int i = 0; i < cl_check_name_count(c); i++) {
		Value n = Value::array();
		n.push(str(cl_check_name(c, i)));
		n.push(Value::boolean(cl_check_name_is_input(c, i)));
		n.push(num(cl_check_name_column(c, i)));
		n.push(Value::boolean(cl_check_name_by_hand(c, i)));
		names.push(n);
	}
	r.set("names", names);
	return r;
}

Value clockedResult(CLCheck* c) {
	Value r = commonFields(c, cl_check_kind(c));
	Value ports = Value::array();
	for (int i = 0; i < cl_check_port_count(c); i++) {
		Value p = Value::array();
		p.push(str(cl_check_port_name(c, i)));
		p.push(Value::boolean(cl_check_port_is_input(c, i)));
		ports.push(p);
	}
	r.set("ports", ports);
	static const char* roles[] = { "input", "state", "next", "output" };
	Value signals = Value::array();
	for (int i = 0; i < cl_check_signal_count(c); i++) {
		Value s = Value::array();
		s.push(str(cl_check_signal_name(c, i)));
		const int role = cl_check_signal_role(c, i);
		s.push(str(role >= 0 && role <= 3 ? roles[role] : "?"));
		s.push(num(cl_check_signal_port(c, i)));
		signals.push(s);
	}
	r.set("signals", signals);
	r.set("firstWrong", num(cl_check_first_wrong(c)));
	static const char* kinds[] = { "start", "set", "pulse" };
	Value steps = Value::array();
	for (int i = 0; i < cl_check_step_count(c); i++) {
		Value s = Value::object();
		const int k = cl_check_step_kind(c, i);
		s.set("kind", str(k >= 0 && k <= 2 ? kinds[k] : "?"));
		s.set("pulse", num(cl_check_step_pulse(c, i)));
		s.set("row", num(cl_check_step_row(c, i)));
		s.set("state", str(cl_check_step_text(c, i, CL_STEP_STATE_BITS)));
		s.set("in", str(cl_check_step_text(c, i, CL_STEP_INPUTS)));
		s.set("exp", str(cl_check_step_text(c, i, CL_STEP_EXPECTED)));
		s.set("got", str(cl_check_step_text(c, i, CL_STEP_GOT)));
		s.set("wrong", Value::boolean(cl_check_step_wrong(c, i)));
		steps.push(s);
	}
	r.set("steps", steps);
	return r;
}

Value todaysResult(CLCheck* c, CLTruthTable* tt, int kind) {
	Value r = commonFields(c, kind);
	Value table = Value::object();
	Value names = Value::array();
	for (int col = 0; col < cl_tt_columns(tt); col++) names.push(str(cl_tt_name(tt, col)));
	table.set("names", names);
	table.set("inputs", num(cl_tt_inputs(tt)));
	Value rows = Value::array();
	for (int row = 0; row < cl_tt_rows(tt); row++) {
		std::string s;
		for (int col = 0; col < cl_tt_columns(tt); col++) s += cl_tt_cell(tt, row, col);
		rows.push(str(s));
	}
	table.set("rows", rows);
	r.set("table", table);
	Value outs = Value::array();
	for (int k = 0; k < cl_check_outputs(c); k++) {
		Value o = Value::array();
		o.push(str(cl_check_output_name(c, k)));
		o.push(num(cl_check_output_column(c, k)));
		o.push(num(cl_check_output_wrong(c, k)));
		outs.push(o);
	}
	r.set("outputs", outs);
	Value wrong = Value::array();
	for (int row = 0; row < cl_tt_rows(tt); row++) if (cl_check_row_wrong(c, row)) wrong.push(num(row));
	r.set("rowWrong", wrong);
	return r;
}

// The first difference between what was expected and what came out, or "".
std::string diff(const Value& want, const Value& got, const std::string& at) {
	if (want.type != got.type) return at + ": a different type";
	switch (want.type) {
		case Value::Null: return "";
		case Value::Bool: return want.b == got.b ? "" : at + ": " + (got.b ? "true" : "false");
		case Value::Number: return want.n == got.n ? "" : at + ": " + std::to_string((long)got.n) + ", not " + std::to_string((long)want.n);
		case Value::String: return want.s == got.s ? "" : at + ":\n      got  " + clsync::json::quote(got.s) + "\n      want " + clsync::json::quote(want.s);
		case Value::Array:
			for (size_t i = 0; i < want.a.size() && i < got.a.size(); i++) {
				const std::string d = diff(want.a[i], got.a[i], at + "[" + std::to_string(i) + "]");
				if (!d.empty()) return d;
			}
			if (want.a.size() != got.a.size())
				return at + ": " + std::to_string(got.a.size()) + " items, not " + std::to_string(want.a.size()) +
				       (got.a.size() > want.a.size() ? " (extra: " + clsync::json::write(got.a[want.a.size()]) + ")"
				                                      : " (missing: " + clsync::json::write(want.a[got.a.size()]) + ")");
			return "";
		case Value::Object:
			for (auto& m : want.o) {
				const Value* g = got.get(m.first);
				if (!g) return at + "." + m.first + ": missing";
				const std::string d = diff(m.second, *g, at + "." + m.first);
				if (!d.empty()) return d;
			}
			for (auto& m : got.o) if (!want.get(m.first)) return at + "." + m.first + ": not expected";
			return "";
	}
	return "";
}

std::string names(const Value& c) {
	std::string out;
	if (const Value* n = c.get("names"))
		for (auto& m : n->o) out += m.first + "\t" + m.second.s + "\n";
	return out;
}

// The flip-flop templates against what a textbook says they do.
int templates(const std::string& dir) {
	struct T { const char* id; const char* key; int verdict; const char* summary; };
	const char* jk = "Q J K | Q+\n0 0 0 | 0\n0 0 1 | 0\n0 1 0 | 1\n0 1 1 | 1\n1 0 0 | 1\n1 0 1 | 0\n1 1 0 | 1\n1 1 1 | 0";
	const T all[] = {
		{ "d", "Pulse | D | Q\n1 | 1 | 1\n2 | 0 | 0\n3 | 1 | 1", 0, "Matches: every clock pulse gives what was asked for." },
		{ "d", "Pulse | D | Q\n1 | 1 | 1\n2 | 0 | 1", 1, "Doesn't match: after clock pulse 2, Q is 0 instead of 1." },
		{ "d-nt", "Pulse | D | Q\n0 | 0 | 0\n1 | 1 | 1\n2 | 0 | 0", 0, "Matches: every clock pulse gives what was asked for." },
		{ "d-ce", "Pulse | D CE | Q\n1 | 1 1 | 1\n2 | 0 0 | 1\n3 | 0 1 | 0", 0, "Matches: every clock pulse gives what was asked for." },
		{ "jk", jk, 0, "Matches: every row of the state table does what was asked for." },
		{ "jk-nt", jk, 0, "Matches: every row of the state table does what was asked for." },
		{ "t", "Q T | Q+\n0 0 | 0\n0 1 | 1\n1 0 | 1\n1 1 | 0", 0, "Matches: every row of the state table does what was asked for." },
		{ "t", "set: T = 1\n0, 1, repeat", 0, "Matches: the light counts 0, 1 and repeat (checked twice round)." },
		{ "t", "0, 1, repeat", 1, "Doesn't match: after clock pulse 1 the light shows 0, not 1." },
		{ "sr", "Pulse | S R | Q\n1 | 1 0 | 1", 2, "Can't check yet: there's no clock." },
	};
	int bad = 0, n = 0;
	for (const T& t : all) {
		n++;
		std::ifstream in(dir + "/template-builtin-ff-" + t.id + ".cdl", std::ios::binary);
		std::stringstream ss;
		ss << in.rdbuf();
		const std::string text = ss.str();
		char err[256];
		CLDocument* doc = in ? cl_document_open_text(text.data(), (long)text.size(), err, sizeof err) : nullptr;
		if (!doc) { printf("FAIL %s: no template\n", t.id); bad++; continue; }
		CLCheck* c = cl_check_clocked(doc, 0, t.key, "");
		const bool ok = cl_check_verdict(c) == t.verdict && std::string(cl_check_summary(c)) == t.summary;
		printf("%s %-6s %s\n", ok ? "ok  " : "FAIL", t.id, cl_check_summary(c));
		if (!ok) {
			bad++;
			for (int i = 0; i < cl_check_note_count(c); i++) printf("       [%d] %s\n", cl_check_note_kind(c, i), cl_check_note(c, i));
		}
		cl_check_free(c);
		cl_document_close(doc);
	}
	printf("%d of %d templates passed\n", n - bad, n);
	return bad ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
	if (argc < 3) { fprintf(stderr, "usage: check_seq <cl_gatedefs.xml> <cases.json> [case id...]\n"); return 2; }
	if (!cl_library_load(argv[1])) { fprintf(stderr, "couldn't load the gate library\n"); return 1; }
	if (argc >= 4 && std::string(argv[2]) == "--templates") return templates(argv[3]);
	std::ifstream in(argv[2], std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	Value all;
	if (!in || !clsync::json::parse(ss.str(), all)) { fprintf(stderr, "couldn't read %s\n", argv[2]); return 1; }
	std::set<std::string> only(argv + 3, argv + argc);
	int bad = 0, ran = 0;

	// The kinds of key, from the text alone.
	const Value* detect = all.get("detect");
	for (const Value& d : detect ? detect->a : std::vector<Value>()) {
		if (!only.empty()) break;
		bool options = false;
		const int kind = cl_check_key_kind(d.str("text").c_str(), &options);
		if (kindName(kind) != d.str("kind") || options != d.flag("options")) {
			printf("FAIL detect %s: %s%s, not %s%s\n", clsync::json::quote(d.str("text")).c_str(), kindName(kind), options ? " (options)" : "",
			       d.str("kind").c_str(), d.flag("options") ? " (options)" : "");
			bad++;
		}
		ran++;
	}

	const Value* circuits = all.get("circuits");
	const Value* cases = all.get("cases");
	for (const Value& c : cases ? cases->a : std::vector<Value>()) {
		const std::string id = c.str("id");
		if (!only.empty() && !only.count(id)) continue;
		const Value* text = circuits ? circuits->get(c.str("circuit")) : nullptr;
		const Value* expect = c.get("expect");
		if (!text || !expect) { printf("FAIL %s: no circuit or no expect\n", id.c_str()); bad++; continue; }
		const std::string keyText = c.str("key"), byHand = names(c);
		const int page = (int)c.integer("page");
		char err[256];
		bool options = false;
		const int kind = cl_check_key_kind(keyText.c_str(), &options);
		const bool clocked = !((kind == CL_KEY_FORMULA || kind == CL_KEY_TABLE) && !options);
		std::vector<std::string> problems;

		for (int run = 0; run < (clocked ? 2 : 1); run++) {
			CLDocument* doc = cl_document_open_text(text->s.data(), (long)text->s.size(), err, sizeof err);
			if (!doc) { problems.push_back(std::string("open: ") + err); break; }
			if (run == 1) {
				// A circuit that has been running: clocks ticking, the first switch flipped and back.
				for (int i = 0; i < 37; i++) cl_document_step(doc);
				cl_document_clock_step(doc, page);
			}
			const std::string before = cl_document_save_text(doc);
			Value got;
			if (clocked) {
				CLCheck* chk = cl_check_clocked(doc, page, keyText.c_str(), byHand.c_str());
				got = clockedResult(chk);
				cl_check_free(chk);
			} else {
				CLTruthTable* tt = cl_truth_table(doc, page, err, sizeof err);
				if (!tt) { problems.push_back(std::string("truth table: ") + err); cl_document_close(doc); break; }
				CLCheck* chk = kind == CL_KEY_FORMULA ? cl_check_expected(tt, c.str("parsed").c_str(), byHand.c_str())
				                                      : cl_check_table(tt, keyText.c_str(), byHand.c_str());
				got = todaysResult(chk, tt, kind);
				cl_check_free(chk);
				cl_tt_free(tt);
			}
			const std::string d = diff(*expect, got, run == 0 ? "expect" : "expect (on a running circuit)");
			if (!d.empty()) problems.push_back(d);
			if (clocked && cl_document_save_text(doc) != before) problems.push_back("the check changed the circuit");
			cl_document_close(doc);
		}
		ran++;
		if (problems.empty()) {
			printf("ok   %-38s %s\n", id.c_str(), expect->str("summary").c_str());
		} else {
			bad++;
			printf("FAIL %s\n", id.c_str());
			for (auto& p : problems) printf("    %s\n", p.c_str());
		}
	}
	printf("%d of %d passed\n", ran - bad, ran);
	return bad ? 1 : 0;
}
