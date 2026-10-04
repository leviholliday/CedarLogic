// Check my circuit: a circuit's truth table against what an assignment asks
// for (a formula the app read, or a truth table the student pasted), matched
// by the names of the switches and lights. One implementation for every app;
// the wording here is what the student reads.

#include "TruthTableImpl.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <string>
#include <vector>

struct CLCheck {
	int verdict = 2;
	std::string summary;
	std::vector<std::pair<int, std::string>> notes;
	struct Out { std::string name; int column = -1; int wrong = 0; std::vector<char> expected, result; };
	std::vector<Out> outs;
	std::vector<bool> rowWrong;
	struct Name { std::string name; bool input = true; int column = -1; bool byHand = false; };
	std::vector<Name> names;
};

namespace {

enum { Info = 0, Warning = 1, Problem = 2 };

struct Expected {
	std::vector<std::string> ins;
	std::vector<std::string> outNames;
	std::vector<std::string> outValues;   // per minterm: '0', '1', '-'
};

std::vector<std::string> split(const std::string& s, char sep) {
	std::vector<std::string> out;
	std::string cur;
	for (char c : s) {
		if (c == sep) { out.push_back(cur); cur.clear(); }
		else if (c != '\r') cur += c;
	}
	out.push_back(cur);
	return out;
}

std::string trim(const std::string& s) {
	size_t a = 0, b = s.size();
	while (a < b && std::isspace((unsigned char)s[a])) a++;
	while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
	return s.substr(a, b - a);
}

// Names match whatever their case or spacing: "Carry In" is "carry_in".
std::string key(const std::string& s) {
	std::string k;
	for (unsigned char c : s)
		if (std::isalnum(c) || c >= 0x80) k += (char)std::tolower(c);
	return k;
}

std::string plural(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

std::string list(const std::vector<std::string>& xs) {
	std::string s;
	for (size_t i = 0; i < xs.size(); i++) s += (i == 0 ? "" : i + 1 == xs.size() ? " and " : ", ") + xs[i];
	return s;
}

std::map<std::string, std::string> readNames(const char* names) {
	std::map<std::string, std::string> m;
	if (!names) return m;
	for (const std::string& line : split(names, '\n')) {
		auto f = split(line, '\t');
		if (f.size() >= 2 && !key(f[0]).empty() && !trim(f[1]).empty()) m[key(f[0])] = trim(f[1]);
	}
	return m;
}

CLCheck* fail(CLCheck* c, const std::string& summary, const std::string& note = "") {
	c->verdict = 2;
	c->summary = summary;
	if (!note.empty()) c->notes.insert(c->notes.begin(), { Problem, note });
	return c;
}

// Matches `want` (asked-for names) with columns [from, to) of the table.
// Returns the asked-for names nothing could be found for.
std::vector<int> match(const CLTruthTable* tt, int from, int to, const std::vector<std::string>& want, bool inputs,
                       const std::map<std::string, std::string>& byHand, CLCheck* c, std::vector<int>& column,
                       std::vector<std::string>& paired) {
	std::vector<bool> used(tt->names.size(), false);
	column.assign(want.size(), -1);
	const size_t first = c->names.size();
	for (const std::string& w : want) c->names.push_back({ w, inputs, -1, false });
	auto find = [&](const std::string& k) {
		for (int col = from; col < to; col++)
			if (!used[col] && key(tt->names[col]) == k) return col;
		return -1;
	};
	// By hand, then by name.
	for (size_t i = 0; i < want.size(); i++) {
		auto h = byHand.find(key(want[i]));
		if (h == byHand.end()) continue;
		const int col = find(key(h->second));
		if (col >= 0) { column[i] = col; used[col] = true; c->names[first + i].byHand = true; }
	}
	for (size_t i = 0; i < want.size(); i++) {
		if (column[i] >= 0) continue;
		const int col = find(key(want[i]));
		if (col >= 0) { column[i] = col; used[col] = true; }
	}
	// What's left on both sides, when it's the same number, goes in order.
	std::vector<int> left, spare;
	for (size_t i = 0; i < want.size(); i++) if (column[i] < 0) left.push_back((int)i);
	for (int col = from; col < to; col++) if (!used[col]) spare.push_back(col);
	if (!left.empty() && left.size() == spare.size()) {
		for (size_t j = 0; j < left.size(); j++) {
			column[left[j]] = spare[j];
			used[spare[j]] = true;
			paired.push_back(want[left[j]] + " is " + tt->names[spare[j]]);
		}
		left.clear();
	}
	for (size_t i = 0; i < want.size(); i++) c->names[first + i].column = column[i];
	return left;
}

CLCheck* compare(const CLTruthTable* tt, const Expected& e, const std::map<std::string, std::string>& byHand, CLCheck* c) {
	const int n = (int)e.ins.size();
	if (e.outNames.empty()) return fail(c, "Nothing to check: there are no outputs in that.");
	const int cols = (int)tt->names.size();

	std::vector<int> inCol, outCol;
	std::vector<std::string> paired;
	auto missingIns = match(tt, 0, tt->inputs, e.ins, true, byHand, c, inCol, paired);
	auto missingOuts = match(tt, tt->inputs, cols, e.outNames, false, byHand, c, outCol, paired);

	if (tt->sequential)
		c->notes.push_back({ Info, "This page has clocks or flip-flops, so a light can depend on what happened before. "
		                           "The check uses the rows as they came out, each read after the row above it." });
	if (tt->unsettled > 0)
		c->notes.push_back({ Warning, plural(tt->unsettled, "row", "rows") + " never stopped changing (a clock or an "
		                              "oscillation), so " + (tt->unsettled == 1 ? "its lights are" : "their lights are") + " a snapshot." });

	if (!missingIns.empty()) {
		std::vector<std::string> names, switches;
		for (int i : missingIns) names.push_back(e.ins[i]);
		for (int col = 0; col < tt->inputs; col++) switches.push_back(tt->names[col]);
		const std::string what = names.size() == 1 ? "'s no switch called " + names[0] : " are no switches called " + list(names);
		return fail(c, "Can't check yet: there" + what + ".",
		            "There" + what + " (the switches are " + list(switches) + "). Pick which switch " +
		            (names.size() == 1 ? "it is" : "each one is") + " under Names, or change a switch's label.");
	}
	if (!paired.empty())
		c->notes.push_back({ Info, "Matched by position, as the names differ: " + list(paired) + "." });
	{
		std::vector<std::string> unused;
		for (int col = 0; col < tt->inputs; col++)
			if (std::find(inCol.begin(), inCol.end(), col) == inCol.end()) unused.push_back(tt->names[col]);
		if (!unused.empty())
			c->notes.push_back({ Info, (unused.size() == 1 ? "Switch " + unused[0] + " isn't" : "Switches " + list(unused) + " aren't") +
			                           " in what was asked for, so the lights shouldn't depend on " +
			                           (unused.size() == 1 ? "it" : "them") + ". Every row was checked." });
		unused.clear();
		for (int col = tt->inputs; col < cols; col++)
			if (std::find(outCol.begin(), outCol.end(), col) == outCol.end()) unused.push_back(tt->names[col]);
		if (!unused.empty())
			c->notes.push_back({ Info, (unused.size() == 1 ? "Light " + unused[0] + " isn't" : "Lights " + list(unused) + " aren't") +
			                           " in what was asked for, so " + (unused.size() == 1 ? "it wasn't" : "they weren't") + " checked." });
	}
	for (int k : missingOuts) {
		std::vector<std::string> lights;
		for (int col = tt->inputs; col < cols; col++) lights.push_back(tt->names[col]);
		c->notes.insert(c->notes.begin(), { Problem, "There's no light called " + e.outNames[k] + " (the lights are " + list(lights) +
		                                             "). Pick which light it is under Names, or change a light's label." });
	}

	// Row by row.
	const int rows = (int)tt->rows.size();
	c->rowWrong.assign(rows, false);
	int dontCares = 0, checked = 0;
	for (size_t k = 0; k < e.outNames.size(); k++) {
		CLCheck::Out o;
		o.name = e.outNames[k];
		o.column = outCol[k];
		std::map<char, int> odd;
		for (int r = 0; r < rows; r++) {
			const auto& row = tt->rows[r];
			int m = 0;
			for (int j = 0; j < n; j++) m = (m << 1) | (inCol[j] < (int)row.size() && row[inCol[j]] == '1' ? 1 : 0);
			const char want = m < (int)e.outValues[k].size() ? e.outValues[k][m] : '-';
			o.expected.push_back(want);
			if (o.column < 0) { o.result.push_back('-'); continue; }
			if (want != '0' && want != '1') { o.result.push_back('-'); dontCares++; continue; }
			checked++;
			const char got = o.column < (int)row.size() ? row[o.column] : '-';
			if (got == want) { o.result.push_back('='); continue; }
			o.result.push_back('x');
			o.wrong++;
			c->rowWrong[r] = true;
			if (got != '0' && got != '1') odd[got]++;
		}
		for (auto& [v, count] : odd) {
			const char* what = v == 'X' ? "X (unknown): a gate feeding it may be missing an input"
			                 : v == 'Z' ? "Z (floating): nothing is driving it"
			                 : v == '!' ? "! (a conflict): two outputs are wired together"
			                            : "nothing: it isn't connected";
			c->notes.push_back({ Warning, "On " + plural(count, "row", "rows") + " the light " +
			                              tt->names[o.column] + " shows " + what + "." });
		}
		c->outs.push_back(o);
	}

	// The verdict.
	std::vector<std::string> wrong;
	for (auto& o : c->outs) {
		if (o.wrong == 0) continue;
		wrong.push_back(o.name + (wrong.empty() ? " is wrong on " + std::to_string(o.wrong) + " of " + std::to_string(rows) + " rows"
		                                        : " on " + std::to_string(o.wrong)));
	}
	std::vector<std::string> missing;
	for (int k : missingOuts) missing.push_back(e.outNames[k]);
	if (!wrong.empty()) {
		c->verdict = 1;
		c->summary = "Doesn't match: " + list(wrong) + ".";
		if (!missing.empty()) c->summary += " " + list(missing) + " couldn't be checked.";
	} else if (!missing.empty()) {
		c->verdict = 2;
		const std::string what = missing.size() == 1 ? "'s no light called " : " are no lights called ";
		c->summary = missing.size() == e.outNames.size()
			? "Can't check yet: there" + what + list(missing) + "."
			: "The rest matches, but there" + what + list(missing) + ".";
	} else {
		c->verdict = 0;
		c->summary = checked == 0 ? "Matches, but every row was a don't-care, so nothing was really checked."
		           : "Matches: every row gives what was asked for" +
		             (dontCares > 0 ? " (" + plural(dontCares, "don't-care wasn't", "don't-cares weren't") + " checked)." : std::string("."));
	}
	return c;
}

bool readSpec(const char* text, Expected& e, std::string& error) {
	for (const std::string& line : split(text ? text : "", '\n')) {
		auto f = split(line, '\t');
		if (f.empty() || trim(f[0]).empty()) continue;
		if (f[0] == "in") {
			for (size_t i = 1; i < f.size(); i++) if (!trim(f[i]).empty()) e.ins.push_back(trim(f[i]));
		} else if (f[0] == "out" && f.size() >= 3) {
			e.outNames.push_back(trim(f[1]));
			e.outValues.push_back(trim(f[2]));
		}
	}
	if (e.ins.size() > 16) { error = "That has too many inputs."; return false; }
	for (size_t k = 0; k < e.outValues.size(); k++)
		if (e.outValues[k].size() != (size_t)1 << e.ins.size()) { error = e.outNames[k] + " doesn't have a value for every row."; return false; }
	return true;
}

bool isValue(char ch) { return ch == '0' || ch == '1' || ch == 'x' || ch == 'X' || ch == '-' || ch == 'd' || ch == 'D'; }

std::vector<std::string> tokens(const std::string& line) {
	std::vector<std::string> out;
	std::string cur;
	auto flush = [&] { if (!cur.empty()) out.push_back(cur); cur.clear(); };
	for (char ch : line) {
		if (ch == '|') { flush(); if (out.empty() || out.back() != "|") out.push_back("|"); }
		else if (std::isspace((unsigned char)ch) || ch == ',' || ch == ';') flush();
		else cur += ch;
	}
	flush();
	return out;
}

// A pasted table into what was asked for.
bool readTable(const CLTruthTable* tt, const char* text, const std::map<std::string, std::string>& byHand, Expected& e,
               std::string& error, std::vector<std::pair<int, std::string>>& notes) {
	std::vector<std::vector<std::string>> lines;
	for (const std::string& line : split(text ? text : "", '\n')) {
		auto t = tokens(line);
		if (!t.empty()) lines.push_back(t);
	}
	if (lines.empty()) { error = "Paste or type the truth table you were given."; return false; }
	std::vector<std::string> header;
	int bar = -1;
	size_t first = 0;
	bool hasHeader = false;
	for (auto& t : lines[0]) {
		if (t == "|") continue;
		for (char ch : t) if (!isValue(ch)) hasHeader = true;
	}
	if (hasHeader) {
		for (auto& t : lines[0]) {
			if (t == "|") { if (bar < 0) bar = (int)header.size(); }
			else header.push_back(t);
		}
		first = 1;
	} else {
		header = tt->names;
		bar = tt->inputs;
		notes.push_back({ Info, "Your table has no names on top, so its columns were taken to be the circuit's, in order." });
	}
	const int ncols = (int)header.size();
	int ni = bar;
	if (ni < 0) {
		// Leading names that are the circuit's switches are the inputs.
		auto isSwitch = [&](const std::string& name) {
			auto h = byHand.find(key(name));
			const std::string k = key(h != byHand.end() ? h->second : name);
			for (int col = 0; col < tt->inputs; col++) if (key(tt->names[col]) == k) return true;
			return false;
		};
		ni = 0;
		while (ni < ncols && isSwitch(header[ni])) ni++;
		if (ni == 0 || ni == ncols) ni = std::min(tt->inputs, ncols - 1);
	}
	if (ni <= 0 || ni >= ncols) { error = "Put a | between the inputs and the outputs in the top row, like A B | F."; return false; }
	if (ni > 12) { error = "That table has more inputs than can be checked (" + std::to_string(ni) + ")."; return false; }
	e.ins.assign(header.begin(), header.begin() + ni);
	e.outNames.assign(header.begin() + ni, header.end());
	const int size = 1 << ni;
	e.outValues.assign(ncols - ni, std::string(size, '?'));
	for (size_t li = first; li < lines.size(); li++) {
		std::string cells;
		for (auto& t : lines[li]) if (t != "|") cells += t;
		const int rowNo = (int)(li - first) + 1;
		if ((int)cells.size() != ncols) {
			error = "Row " + std::to_string(rowNo) + " has " + std::to_string(cells.size()) + " values; the top row has " +
			        std::to_string(ncols) + " names.";
			return false;
		}
		int m = 0;
		for (int j = 0; j < ni; j++) {
			if (cells[j] != '0' && cells[j] != '1') {
				error = "Row " + std::to_string(rowNo) + "'s inputs need to be 0 or 1.";
				return false;
			}
			m = (m << 1) | (cells[j] == '1');
		}
		for (int k = 0; k < ncols - ni; k++) {
			char v = cells[ni + k];
			if (!isValue(v)) {
				error = "Row " + std::to_string(rowNo) + " has \"" + std::string(1, v) + "\"; use 0, 1, or X for don't care.";
				return false;
			}
			v = (v == '0' || v == '1') ? v : '-';
			char& slot = e.outValues[k][m];
			if (slot != '?' && slot != v) {
				error = "Two rows give " + e.outNames[k] + " different values for the same inputs (row " + std::to_string(rowNo) + ").";
				return false;
			}
			slot = v;
		}
	}
	int missing = 0;
	for (char v : e.outValues[0]) if (v == '?') missing++;
	for (auto& vals : e.outValues)
		for (char& v : vals) if (v == '?') v = '-';
	if (missing > 0)
		notes.push_back({ Info, "Your table leaves out " + plural(missing, "row", "rows") + " of " + std::to_string(size) +
		                        ", so " + (missing == 1 ? "it wasn't" : "those weren't") + " checked." });
	return true;
}

}  // namespace

extern "C" {

CLCheck* cl_check_expected(const CLTruthTable* tt, const char* expected, const char* names) {
	auto* c = new CLCheck();
	if (!tt) return fail(c, "There's no truth table to check.");
	Expected e;
	std::string error;
	if (!readSpec(expected, e, error)) return fail(c, error);
	return compare(tt, e, readNames(names), c);
}

CLCheck* cl_check_table(const CLTruthTable* tt, const char* table, const char* names) {
	auto* c = new CLCheck();
	if (!tt) return fail(c, "There's no truth table to check.");
	Expected e;
	std::string error;
	const auto byHand = readNames(names);
	std::vector<std::pair<int, std::string>> notes;
	if (!readTable(tt, table, byHand, e, error, notes)) return fail(c, error);
	compare(tt, e, byHand, c);
	c->notes.insert(c->notes.end(), notes.begin(), notes.end());
	return c;
}

void cl_check_free(CLCheck* c) { delete c; }
int cl_check_verdict(const CLCheck* c) { return c ? c->verdict : 2; }
const char* cl_check_summary(const CLCheck* c) { return c ? c->summary.c_str() : ""; }
int cl_check_note_count(const CLCheck* c) { return c ? (int)c->notes.size() : 0; }
const char* cl_check_note(const CLCheck* c, int i) {
	return (c && i >= 0 && i < (int)c->notes.size()) ? c->notes[i].second.c_str() : "";
}
int cl_check_note_kind(const CLCheck* c, int i) { return (c && i >= 0 && i < (int)c->notes.size()) ? c->notes[i].first : 0; }
int cl_check_outputs(const CLCheck* c) { return c ? (int)c->outs.size() : 0; }
const char* cl_check_output_name(const CLCheck* c, int k) {
	return (c && k >= 0 && k < (int)c->outs.size()) ? c->outs[k].name.c_str() : "";
}
int cl_check_output_column(const CLCheck* c, int k) { return (c && k >= 0 && k < (int)c->outs.size()) ? c->outs[k].column : -1; }
int cl_check_output_wrong(const CLCheck* c, int k) { return (c && k >= 0 && k < (int)c->outs.size()) ? c->outs[k].wrong : 0; }
char cl_check_expected_cell(const CLCheck* c, int r, int k) {
	if (!c || k < 0 || k >= (int)c->outs.size() || r < 0 || r >= (int)c->outs[k].expected.size()) return ' ';
	return c->outs[k].expected[r];
}
char cl_check_result(const CLCheck* c, int r, int k) {
	if (!c || k < 0 || k >= (int)c->outs.size() || r < 0 || r >= (int)c->outs[k].result.size()) return '-';
	return c->outs[k].result[r];
}
bool cl_check_row_wrong(const CLCheck* c, int r) { return c && r >= 0 && r < (int)c->rowWrong.size() && c->rowWrong[r]; }
int cl_check_name_count(const CLCheck* c) { return c ? (int)c->names.size() : 0; }
const char* cl_check_name(const CLCheck* c, int i) { return (c && i >= 0 && i < (int)c->names.size()) ? c->names[i].name.c_str() : ""; }
bool cl_check_name_is_input(const CLCheck* c, int i) { return c && i >= 0 && i < (int)c->names.size() && c->names[i].input; }
int cl_check_name_column(const CLCheck* c, int i) { return (c && i >= 0 && i < (int)c->names.size()) ? c->names[i].column : -1; }
bool cl_check_name_by_hand(const CLCheck* c, int i) { return c && i >= 0 && i < (int)c->names.size() && c->names[i].byHand; }

}  // extern "C"
