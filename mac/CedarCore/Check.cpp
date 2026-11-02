// Check my circuit: a circuit's truth table against what an assignment asks
// for (a formula the app read, or a truth table the student pasted), matched
// by the names of the switches and lights. One implementation for every app;
// the wording here is what the student reads. Every kind of answer key is
// read here too (docs/CHECK-SEQUENTIAL.md §2 and §3); counts, state tables and
// timing tables are run by CheckClocked.cpp.

#include "CheckImpl.h"
#include "TruthTableImpl.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace clcheck;

namespace {

struct Expected {
	std::vector<std::string> ins;
	std::vector<std::string> outNames;
	std::vector<std::string> outValues;   // per minterm: '0', '1', '-'
};

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

// ---- Reading the answer key (docs/CHECK-SEQUENTIAL.md §2, §3) ----------------

namespace clcheck {

namespace {

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
	size_t at = 0;
	while ((at = s.find(from, at)) != std::string::npos) { s.replace(at, from.size(), to); at += to.size(); }
	return s;
}

bool optionLine(const std::string& line, std::string& word, std::string& rest) {
	size_t i = 0;
	while (i < line.size() && std::isalpha((unsigned char)line[i])) i++;
	const std::string w = lower(line.substr(0, i));
	if (w != "start" && w != "reset" && w != "set" && w != "clock") return false;
	size_t j = i;
	while (j < line.size() && (line[j] == ' ' || line[j] == '\t')) j++;
	if (j >= line.size() || line[j] != ':') return false;
	word = w;
	rest = trim(line.substr(j + 1));
	return true;
}

// "Q2 Q1 Q0: 0, 3, 5, 6, repeat" (the prefix and the repeat are optional).
bool readCount(const std::string& line, std::vector<std::string>& names, std::vector<std::string>& values, bool& cyclic) {
	names.clear(); values.clear(); cyclic = false;
	std::string rest = line;
	const size_t colon = line.find(':');
	if (colon != std::string::npos) {
		names = words(line.substr(0, colon));
		if (names.empty()) return false;
		rest = line.substr(colon + 1);
	}
	for (const char* arrow : { "->", "\xE2\x86\x92", "=>", "\xE2\x87\x92" }) rest = replaceAll(rest, arrow, " ");
	rest = replaceAll(rest, "\xE2\x80\xA6", " ... ");
	rest = replaceAll(rest, "...", " ... ");
	std::vector<std::string> t;
	for (const std::string& w : tokens(rest)) {
		if (w == "|") return false;
		std::string v = w;
		if (v.size() > 2 && v.front() == '(' && v.back() == ')') v = v.substr(1, v.size() - 2);
		t.push_back(v);
	}
	auto marker = [](const std::string& s) { const std::string l = lower(s); return l == "..." || l == "repeat" || l == "repeats"; };
	if (!t.empty() && marker(t.back())) {
		cyclic = true;
		const bool word = lower(t.back()) != "...";
		t.pop_back();
		if (word && !t.empty() && (lower(t.back()) == "and" || lower(t.back()) == "then")) t.pop_back();
	}
	if (t.size() < 2) return false;
	for (const std::string& v : t) if (!allDigits(v)) return false;
	values = t;
	return true;
}

const std::set<std::string> stepWords = { "edge", "step", "cycle", "pulse", "tick", "#" };

bool looksTiming(const std::vector<std::vector<std::string>>& lines) {
	std::vector<std::string> head;
	for (auto& t : lines[0]) if (t != "|") head.push_back(t);
	if (head.empty()) return false;
	if (stepWords.count(lower(head[0]))) return true;
	// Or the first column counts 0 (or 1), 1, 2... up to 2 at least.
	if (lines.size() < 3) return false;
	long last = -1;
	for (size_t i = 1; i < lines.size(); i++) {
		std::string f;
		for (auto& t : lines[i]) if (t != "|") { f = t; break; }
		if (!allDigits(f) || f.size() > 6) return false;
		const long v = std::stol(f);
		if (i == 1 ? (v != 0 && v != 1) : v != last + 1) return false;
		last = v;
	}
	return last >= 2;
}

// "Q1+", "Q1*" and "Q1(t+1)" are Q1's next state; "Q1(t)" is Q1.
std::string nextBase(const std::string& name, bool& marked) {
	marked = false;
	const std::string l = lower(name);
	if (l.size() > 5 && l.compare(l.size() - 5, 5, "(t+1)") == 0) { marked = true; return name.substr(0, name.size() - 5); }
	if (name.size() > 1 && (name.back() == '+' || name.back() == '*')) { marked = true; return name.substr(0, name.size() - 1); }
	if (l.size() > 3 && l.compare(l.size() - 3, 3, "(t)") == 0) return name.substr(0, name.size() - 3);
	return name;
}

bool looksStates(const std::vector<std::vector<std::string>>& lines) {
	bool header = false;
	for (auto& t : lines[0]) {
		if (t == "|") continue;
		for (char ch : t) if (!isValue(ch)) header = true;
	}
	if (!header) return false;
	int bar = -1;
	std::vector<std::string> names;
	for (auto& t : lines[0]) {
		if (t == "|") { if (bar < 0 && !names.empty()) bar = (int)names.size(); continue; }
		bool marked;
		names.push_back(nextBase(t, marked));
		if (marked) return true;
	}
	if (bar < 0) return false;
	for (size_t i = bar; i < names.size(); i++)
		for (int j = 0; j < bar; j++) if (key(names[i]) == key(names[j])) return true;
	return false;
}

bool looksTable(const std::vector<std::string>& body) {
	for (size_t i = 1; i < body.size(); i++)
		for (char ch : body[i])
			if (!isValue(ch) && ch != '|' && ch != ' ' && ch != '\t' && ch != ',' && ch != ';') return false;
	return true;
}

// §2: which kind of key a text is, and its body and option lines.
int detect(const std::string& text, bool* options, std::vector<std::string>* bodyOut, std::vector<std::string>* optOut) {
	std::vector<std::string> body, opts;
	for (const std::string& raw : split(text, '\n')) {
		const std::string line = trim(raw);
		if (line.empty()) continue;
		std::string w, r;
		if (optionLine(line, w, r)) opts.push_back(line); else body.push_back(line);
	}
	if (options) *options = !opts.empty();
	if (bodyOut) *bodyOut = body;
	if (optOut) *optOut = opts;
	if (body.empty()) return CL_KEY_EMPTY;
	std::vector<std::string> n, v;
	bool cyc;
	if (body.size() == 1 && readCount(body[0], n, v, cyc)) return CL_KEY_COUNT;
	if (body.size() >= 2) {
		bool eq = false;
		for (auto& l : body) if (l.find('=') != std::string::npos) eq = true;
		if (!eq) {
			std::vector<std::vector<std::string>> lines;
			for (auto& l : body) lines.push_back(tokens(l));
			if (looksTiming(lines)) return CL_KEY_TIMING;
			if (looksStates(lines)) return CL_KEY_STATES;
			if (looksTable(body)) return CL_KEY_TABLE;
		}
	}
	return CL_KEY_FORMULA;
}

bool readAssign(const std::string& word, const std::string& rest, Assign& a, bool namesNeeded, std::string& error) {
	const std::string how = word == "start" ? "Write start: like start: Q1 Q0 = 01." : word == "reset" ? "Write reset: like reset: CLR' = 0."
	                                                                               : "Write set: like set: EN = 1.";
	a = Assign();
	const size_t eqs = std::count(rest.begin(), rest.end(), '=');
	auto bitsOf = [](const std::string& s, std::string& out) {
		out.clear();
		for (char ch : s) {
			if (ch == '0' || ch == '1') out += ch;
			else if (!std::isspace((unsigned char)ch) && ch != ',') return false;
		}
		return !out.empty();
	};
	if (eqs == 0) {
		if (namesNeeded || !bitsOf(rest, a.bits)) { error = how; return false; }
		return true;
	}
	if (eqs == 1) {
		const size_t at = rest.find('=');
		a.names = words(rest.substr(0, at));
		if (a.names.empty() || !bitsOf(rest.substr(at + 1), a.bits)) { error = how; return false; }
		if (a.names.size() != a.bits.size()) {
			error = word + ": has " + plural((int)a.names.size(), "name", "names") + " but " + plural((int)a.bits.size(), "value", "values") + ".";
			return false;
		}
		return true;
	}
	std::string chunk;
	std::vector<std::string> chunks;
	for (char ch : rest) { if (ch == ',' || ch == ';') { chunks.push_back(chunk); chunk.clear(); } else chunk += ch; }
	chunks.push_back(chunk);
	for (const std::string& c : chunks) {
		if (trim(c).empty()) continue;
		const size_t at = c.find('=');
		std::string b;
		if (at == std::string::npos || c.find('=', at + 1) != std::string::npos || trim(c.substr(0, at)).empty() ||
		    !bitsOf(c.substr(at + 1), b) || b.size() != 1) { error = how; return false; }
		a.names.push_back(trim(c.substr(0, at)));
		a.bits += b;
	}
	return true;
}

std::string rowLength(int row, int got, int want) {
	return "Row " + std::to_string(row) + " has " + std::to_string(got) + " values; the top row has " + std::to_string(want) + " names.";
}
std::string badCell(int row, char v) { return "Row " + std::to_string(row) + " has \"" + std::string(1, v) + "\"; use 0, 1, or X for don't care."; }

bool readTiming(const std::vector<std::string>& body, Key& k) {
	std::vector<std::vector<std::string>> lines;
	for (auto& l : body) lines.push_back(tokens(l));
	bool skipped = false;
	for (auto& t : lines[0]) {
		if (t == "|") { if (!k.names.empty() && k.bar < 0) k.bar = (int)k.names.size(); continue; }
		if (!skipped) { skipped = true; continue; }    // the pulse column
		k.names.push_back(t);
	}
	const int n = (int)k.names.size();
	if (n == 0) { k.error = "Put the names of the switches and lights in the top row, like Pulse | X | Z."; return false; }
	if (k.bar == n) k.bar = -1;
	if (lines.size() - 1 > 1000) { k.error = "That's " + std::to_string(lines.size() - 1) + " rows; up to 1000 can be checked."; return false; }
	for (size_t li = 1; li < lines.size(); li++) {
		const int r = (int)li;
		std::string first, cells;
		bool got = false;
		for (auto& t : lines[li]) {
			if (t == "|") continue;
			if (!got) { first = t; got = true; } else cells += t;
		}
		if (!allDigits(first) || first.size() > 6) { k.error = "Row " + std::to_string(r) + " needs its clock pulse number first."; return false; }
		const int v = std::stoi(first);
		const int want = li == 1 ? (v == 0 ? 0 : 1) : k.rows.back().pulse + 1;
		if (v != want) {
			k.error = "Number the rows by clock pulse: 0 for the start (if you like), then 1, 2, 3 and so on. Row " + std::to_string(r) +
			          " has " + std::to_string(v) + ".";
			return false;
		}
		if ((int)cells.size() != n) { k.error = rowLength(r, (int)cells.size(), n); return false; }
		Key::Row row;
		row.number = r;
		row.pulse = v;
		for (char ch : cells) {
			if (!isValue(ch)) { k.error = badCell(r, ch); return false; }
			row.out += isDontCare(ch) ? '-' : ch;   // inputs and outputs are split once the switches are known
		}
		k.rows.push_back(row);
	}
	return true;
}

bool readStates(const std::vector<std::string>& body, Key& k) {
	std::vector<std::vector<std::string>> lines;
	for (auto& l : body) lines.push_back(tokens(l));
	std::vector<std::string> names, bases;
	std::vector<bool> marked;
	int bar = -1;
	for (auto& t : lines[0]) {
		if (t == "|") { if (bar < 0 && !names.empty()) bar = (int)names.size(); continue; }
		bool m;
		names.push_back(t);
		bases.push_back(nextBase(t, m));
		marked.push_back(m);
	}
	const int n = (int)names.size();
	std::vector<bool> isNext(n, false);
	bool anyMarked = false;
	for (int i = 0; i < n; i++) if (marked[i]) { isNext[i] = true; anyMarked = true; }
	if (!anyMarked && bar >= 0)
		for (int i = bar; i < n; i++)
			for (int j = 0; j < bar; j++) if (key(bases[i]) == key(bases[j])) isNext[i] = true;
	int right = bar >= 0 ? bar : n;
	for (int i = 0; i < n; i++) if (isNext[i]) { right = std::min(right, i); break; }
	for (int i = 0; i < right; i++) {
		for (int j = 0; j < i; j++)
			if (key(bases[i]) == key(bases[j])) { k.error = bases[i] + " is in the top row twice."; return false; }
	}
	std::vector<int> nextOf(n, -1);      // a left column -> its next-state column
	for (int i = right; i < n; i++) {
		if (!isNext[i]) continue;
		int found = -1;
		for (int j = 0; j < right; j++) if (key(bases[j]) == key(bases[i])) found = j;
		if (found < 0) { k.error = names[i] + " has no " + bases[i] + " column for the present state."; return false; }
		if (nextOf[found] >= 0) { k.error = names[i] + " is in the top row twice."; return false; }
		nextOf[found] = i;
	}
	std::vector<int> stCols, inCols, outCols, nxCols;
	for (int j = 0; j < right; j++) {
		if (nextOf[j] >= 0) { stCols.push_back(j); nxCols.push_back(nextOf[j]); k.state.push_back(bases[j]); k.next.push_back(names[nextOf[j]]); }
		else { inCols.push_back(j); k.ins.push_back(bases[j]); }
	}
	for (int i = right; i < n; i++) if (!isNext[i]) { outCols.push_back(i); k.outs.push_back(names[i]); }
	// Rows; a don't-care on the left stands for both values.
	struct Raw { int number; std::string st, in, nx, out; };
	std::vector<Raw> raws;
	for (size_t li = 1; li < lines.size(); li++) {
		const int r = (int)li;
		std::string cells;
		for (auto& t : lines[li]) if (t != "|") cells += t;
		if ((int)cells.size() != n) { k.error = rowLength(r, (int)cells.size(), n); return false; }
		for (char ch : cells) if (!isValue(ch)) { k.error = badCell(r, ch); return false; }
		auto pick = [&](const std::vector<int>& cols) { std::string s; for (int c : cols) s += isDontCare(cells[c]) ? '-' : cells[c]; return s; };
		raws.push_back({ r, pick(stCols), pick(inCols), pick(nxCols), pick(outCols) });
	}
	std::vector<Key::Row> rows;
	std::map<std::string, size_t> at;
	for (const Raw& raw : raws) {
		const std::string left = raw.st + raw.in;
		std::vector<int> dashes;
		for (size_t i = 0; i < left.size(); i++) if (left[i] == '-') dashes.push_back((int)i);
		if (dashes.size() > 10 || rows.size() + ((size_t)1 << dashes.size()) > 1024) {
			k.error = "That table stands for more than 1024 rows; up to 1024 can be checked.";
			return false;
		}
		for (int m = 0; m < (1 << dashes.size()); m++) {
			std::string l = left;
			for (size_t d = 0; d < dashes.size(); d++) l[dashes[d]] = ((m >> (dashes.size() - 1 - d)) & 1) ? '1' : '0';
			Key::Row row;
			row.number = raw.number;
			row.st = l.substr(0, raw.st.size());
			row.in = l.substr(raw.st.size());
			row.nx = raw.nx;
			row.out = raw.out;
			auto it = at.find(l);
			if (it == at.end()) { at[l] = rows.size(); rows.push_back(row); continue; }
			Key::Row& old = rows[it->second];
			auto merge = [&](std::string& a, const std::string& b, const std::vector<std::string>& colNames) {
				for (size_t i = 0; i < a.size(); i++) {
					if (b[i] == '-') continue;
					if (a[i] != '-' && a[i] != b[i]) {
						k.error = "Two rows give " + colNames[i] + " different values for the same state and inputs (row " +
						          std::to_string(raw.number) + ").";
						return false;
					}
					a[i] = b[i];
				}
				return true;
			};
			if (!merge(old.nx, row.nx, k.next) || !merge(old.out, row.out, k.outs)) return false;
		}
	}
	for (auto& r : rows)
		if (r.nx.find_first_not_of('-') != std::string::npos || r.out.find_first_not_of('-') != std::string::npos) k.rows.push_back(r);
	return true;
}

}  // namespace

int detectKey(const std::string& text, bool* options) { return detect(text, options, nullptr, nullptr); }

Key readKey(const std::string& text) {
	Key k;
	std::vector<std::string> body, opts;
	k.kind = detect(text, &k.options, &body, &opts);
	if (k.kind == CL_KEY_EMPTY && !opts.empty()) {
		k.error = "Add what to check under the start:, reset:, set: or clock: lines: a count, a state table or a timing table.";
		return k;
	}
	if (k.kind == CL_KEY_EMPTY) return k;
	if (!opts.empty() && (k.kind == CL_KEY_FORMULA || k.kind == CL_KEY_TABLE)) {
		k.error = "start:, reset:, set: and clock: lines go with a count, a state table or a timing table.";
		return k;
	}
	for (const std::string& line : opts) {
		std::string w, r;
		optionLine(line, w, r);
		bool& has = w == "start" ? k.hasStart : w == "reset" ? k.hasReset : w == "set" ? k.hasSet : k.hasClock;
		if (has) { k.error = "There are two " + w + ": lines."; return k; }
		has = true;
		if (w == "clock") {
			if (r.empty() || r.find('=') != std::string::npos) { k.error = "Write clock: like clock: CLK."; return k; }
			k.clock = r;
			continue;
		}
		if (w == "start" && k.kind == CL_KEY_COUNT) { k.error = "A count starts at its first number, so it doesn't take a start: line."; return k; }
		Assign& a = w == "start" ? k.start : w == "reset" ? k.reset : k.set;
		if (!readAssign(w, r, a, w != "start" || k.kind == CL_KEY_TIMING, k.error)) return k;
	}
	if (k.kind == CL_KEY_COUNT) readCount(body[0], k.countNames, k.countTokens, k.cyclic);
	else if (k.kind == CL_KEY_TIMING) readTiming(body, k);
	else if (k.kind == CL_KEY_STATES) readStates(body, k);
	return k;
}


}  // namespace clcheck

extern "C" {

CLCheck* cl_check_expected(const CLTruthTable* tt, const char* expected, const char* names) {
	auto* c = new CLCheck();
	c->kind = CL_KEY_FORMULA;
	if (!tt) return fail(c, "There's no truth table to check.");
	Expected e;
	std::string error;
	if (!readSpec(expected, e, error)) return fail(c, error);
	return compare(tt, e, readNames(names), c);
}

CLCheck* cl_check_table(const CLTruthTable* tt, const char* table, const char* names) {
	auto* c = new CLCheck();
	c->kind = CL_KEY_TABLE;
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

int cl_check_key_kind(const char* text, bool* options) {
	bool opts = false;
	const int kind = detectKey(text ? text : "", &opts);
	if (options) *options = opts;
	return kind;
}

int cl_check_kind(const CLCheck* c) { return c ? c->kind : CL_KEY_EMPTY; }
const char* cl_check_error(const CLCheck* c) { return c ? c->error.c_str() : ""; }
int cl_check_port_count(const CLCheck* c) { return c ? (int)c->ports.size() : 0; }
const char* cl_check_port_name(const CLCheck* c, int i) { return (c && i >= 0 && i < (int)c->ports.size()) ? c->ports[i].name.c_str() : ""; }
bool cl_check_port_is_input(const CLCheck* c, int i) { return c && i >= 0 && i < (int)c->ports.size() && c->ports[i].input; }
int cl_check_signal_count(const CLCheck* c) { return c ? (int)c->signals.size() : 0; }
const char* cl_check_signal_name(const CLCheck* c, int i) { return (c && i >= 0 && i < (int)c->signals.size()) ? c->signals[i].name.c_str() : ""; }
int cl_check_signal_role(const CLCheck* c, int i) { return (c && i >= 0 && i < (int)c->signals.size()) ? c->signals[i].role : CL_SIGNAL_OUTPUT; }
int cl_check_signal_port(const CLCheck* c, int i) { return (c && i >= 0 && i < (int)c->signals.size()) ? c->signals[i].port : -1; }
int cl_check_step_count(const CLCheck* c) { return c ? (int)c->steps.size() : 0; }
int cl_check_first_wrong(const CLCheck* c) { return c ? c->firstWrong : -1; }
int cl_check_step_kind(const CLCheck* c, int i) { return (c && i >= 0 && i < (int)c->steps.size()) ? c->steps[i].kind : CL_STEP_PULSE; }
int cl_check_step_pulse(const CLCheck* c, int i) { return (c && i >= 0 && i < (int)c->steps.size()) ? c->steps[i].pulse : 0; }
int cl_check_step_row(const CLCheck* c, int i) { return (c && i >= 0 && i < (int)c->steps.size()) ? c->steps[i].row : 0; }
bool cl_check_step_wrong(const CLCheck* c, int i) { return c && i >= 0 && i < (int)c->steps.size() && c->steps[i].wrong; }
const char* cl_check_step_text(const CLCheck* c, int i, int what) {
	if (!c || i < 0 || i >= (int)c->steps.size()) return "";
	const CLCheck::Step& s = c->steps[i];
	switch (what) {
		case CL_STEP_STATE_BITS: return s.state.c_str();
		case CL_STEP_INPUTS: return s.in.c_str();
		case CL_STEP_EXPECTED: return s.exp.c_str();
		case CL_STEP_GOT: return s.got.c_str();
		default: return "";
	}
}

}  // extern "C"
