// Builds the shared test cases for Check My Circuit on clocked circuits
// (docs/CHECK-SEQUENTIAL.md) and works out what each should give:
//   make_check_cases <cl_gatedefs.xml> <out cases.json>
//
// Every circuit is built through CedarCore (cl_edit_build, as the Mac app's
// templates are) and saved as the app saves it. Each case's result comes from
// the document's rules run on the real engine: this file holds a reference
// checker written straight from CHECK-SEQUENTIAL.md, which runs a twin of the
// circuit with every clock part swapped for a switch the checker turns on and
// off (what a manual clock is, electrically). Each case also says what it
// should give, worked out by hand (the verdict, the error, the first wrong
// step and what that step expected and got); the tool stops with an error
// when the engine and the hand-worked answer disagree. The combinational
// cases run today's checker (cl_truth_table, cl_check_expected, cl_check_table).

#include "DocumentImpl.h"
#include "guiGate.h"
#include "guiWire.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

// ---- Text helpers (as Check.cpp's) ------------------------------------------

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

std::string lower(std::string s) {
	for (char& c : s) c = (char)std::tolower((unsigned char)c);
	return s;
}

std::string plural(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

std::string list(const std::vector<std::string>& xs) {
	std::string s;
	for (size_t i = 0; i < xs.size(); i++) s += (i == 0 ? "" : i + 1 == xs.size() ? " and " : ", ") + xs[i];
	return s;
}

std::string joined(const std::vector<std::string>& xs, const char* sep) {
	std::string s;
	for (size_t i = 0; i < xs.size(); i++) s += (i ? sep : "") + xs[i];
	return s;
}

bool isValue(char ch) { return ch == '0' || ch == '1' || ch == 'x' || ch == 'X' || ch == '-' || ch == 'd' || ch == 'D'; }
bool isDontCare(char ch) { return ch == 'x' || ch == 'X' || ch == '-' || ch == 'd' || ch == 'D'; }
bool allDigits(const std::string& s) {
	if (s.empty()) return false;
	for (char c : s) if (c < '0' || c > '9') return false;
	return true;
}
bool binary(const std::string& s) { return !s.empty() && s.find_first_not_of("01") == std::string::npos; }

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

// Split on spaces, tabs and commas.
std::vector<std::string> words(const std::string& s) {
	std::vector<std::string> out;
	std::string cur;
	for (char ch : s) {
		if (std::isspace((unsigned char)ch) || ch == ',') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
		else cur += ch;
	}
	if (!cur.empty()) out.push_back(cur);
	return out;
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
	size_t at = 0;
	while ((at = s.find(from, at)) != std::string::npos) { s.replace(at, from.size(), to); at += to.size(); }
	return s;
}

// ---- The answer key (CHECK-SEQUENTIAL.md §2, §3) ------------------------------

enum Kind { KEmpty = 0, KFormula = 1, KTable = 2, KCount = 3, KStates = 4, KTiming = 5 };
const char* kindName(int k) {
	static const char* n[] = { "empty", "formula", "table", "count", "states", "timing" };
	return n[k];
}

// A start:, reset: or set: line: names and their values.
struct Assign { std::vector<std::string> names; std::string bits; };

struct Key {
	int kind = KEmpty;
	bool options = false;                     // it has start:, reset:, set: or clock: lines
	std::string error;                        // a bad_key message ("" when it reads)
	bool hasStart = false, hasReset = false, hasSet = false, hasClock = false;
	Assign start, reset, set;
	std::string clock;
	// A count
	std::vector<std::string> countNames;      // the prefix, most significant first
	std::vector<std::string> countTokens;
	bool cyclic = false;
	// A timing table: the header's names after the pulse column, and where its first | is
	std::vector<std::string> names;
	int bar = -1;
	// A state table (next: as written, in the state's order); a timing table's outputs
	std::vector<std::string> ins, outs, state, next;
	struct Row { int number = 0; int pulse = 0; std::string in, st, nx, out; };
	std::vector<Row> rows;
};

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

// §2: which kind of key a text is.
int detect(const std::string& text, bool* options = nullptr, std::vector<std::string>* bodyOut = nullptr,
           std::vector<std::string>* optOut = nullptr) {
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
	if (body.empty()) return KEmpty;
	std::vector<std::string> n, v;
	bool cyc;
	if (body.size() == 1 && readCount(body[0], n, v, cyc)) return KCount;
	if (body.size() >= 2) {
		bool eq = false;
		for (auto& l : body) if (l.find('=') != std::string::npos) eq = true;
		if (!eq) {
			std::vector<std::vector<std::string>> lines;
			for (auto& l : body) lines.push_back(tokens(l));
			if (looksTiming(lines)) return KTiming;
			if (looksStates(lines)) return KStates;
			if (looksTable(body)) return KTable;
		}
	}
	return KFormula;
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

Key readKey(const std::string& text) {
	Key k;
	std::vector<std::string> body, opts;
	k.kind = detect(text, &k.options, &body, &opts);
	if (k.kind == KEmpty && !opts.empty()) {
		k.error = "Add what to check under the start:, reset:, set: or clock: lines: a count, a state table or a timing table.";
		return k;
	}
	if (k.kind == KEmpty) return k;
	if (!opts.empty() && (k.kind == KFormula || k.kind == KTable)) {
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
		if (w == "start" && k.kind == KCount) { k.error = "A count starts at its first number, so it doesn't take a start: line."; return k; }
		Assign& a = w == "start" ? k.start : w == "reset" ? k.reset : k.set;
		if (!readAssign(w, r, a, w != "start" || k.kind == KTiming, k.error)) return k;
	}
	if (k.kind == KCount) readCount(body[0], k.countNames, k.countTokens, k.cyclic);
	else if (k.kind == KTiming) readTiming(body, k);
	else if (k.kind == KStates) readStates(body, k);
	return k;
}

// ---- The circuit: its switches and lights, and a twin to run (§4, §5) -------------

struct Port { long gate; std::string name; bool input; bool labeled; std::string value; };

std::pair<float, float> where(guiGate* g) { float x, y; g->getGLcoords(x, y); return { x, y }; }

// The page's switches then lights, each top to bottom then left to right,
// named as cl_truth_table names them when nothing is selected.
std::vector<Port> portsOf(CLDocument* doc, int pageIndex) {
	auto* gates = doc->page(pageIndex)->getGateList();
	std::vector<guiGate*> ins, outs, labels;
	for (auto& g : *gates) {
		if (dynamic_cast<guiGateTOGGLE*>(g.second)) ins.push_back(g.second);
		else if (dynamic_cast<guiGateLED*>(g.second)) outs.push_back(g.second);
		else if (dynamic_cast<guiLabel*>(g.second) && !g.second->getGUIParam("LABEL_TEXT").empty()) labels.push_back(g.second);
	}
	auto byPlace = [](guiGate* a, guiGate* b) {
		const auto pa = where(a), pb = where(b);
		return pa.second != pb.second ? pa.second > pb.second : pa.first < pb.first;
	};
	std::sort(ins.begin(), ins.end(), byPlace);
	std::sort(outs.begin(), outs.end(), byPlace);
	std::sort(labels.begin(), labels.end(), [](guiGate* a, guiGate* b) { return a->getID() < b->getID(); });
	std::vector<bool> used(labels.size(), false);
	auto nameFor = [&](guiGate* port, const std::string& fallback, bool& labeled) {
		const auto p = where(port);
		int best = -1;
		float bestD = 8.0f;
		for (size_t i = 0; i < labels.size(); i++) {
			if (used[i]) continue;
			const std::string text = labels[i]->getGUIParam("LABEL_TEXT");
			if (text.size() > 16) continue;
			const auto q = where(labels[i]);
			const float d = std::hypot(q.first - p.first, q.second - p.second);
			if (d < bestD) { bestD = d; best = (int)i; }
		}
		labeled = best >= 0;
		if (best < 0) return fallback;
		used[best] = true;
		return labels[best]->getGUIParam("LABEL_TEXT");
	};
	std::vector<Port> ports;
	for (size_t i = 0; i < ins.size(); i++) {
		Port p{ (long)ins[i]->getID(), "", true, false, ins[i]->getLogicParam("OUTPUT_NUM") == "1" ? "1" : "0" };
		p.name = nameFor(ins[i], std::string(1, (char)('A' + i)), p.labeled);
		ports.push_back(p);
	}
	for (size_t i = 0; i < outs.size(); i++) {
		Port p{ (long)outs[i]->getID(), "", false, false, "" };
		p.name = nameFor(outs[i], outs.size() == 1 ? "Y" : "Y" + std::to_string(i + 1), p.labeled);
		ports.push_back(p);
	}
	return ports;
}

struct ClockPart { long gate; bool manual; };

std::vector<ClockPart> clocksOf(CLDocument* doc) {
	std::vector<ClockPart> out;
	for (size_t p = 0; p < doc->pages.size(); p++)
		for (auto& g : *doc->page((int)p)->getGateList())
			if (g.second->getLogicType() == "CLOCK") out.push_back({ (long)g.first, g.second->getLogicParam("MANUAL") == "true" });
	std::sort(out.begin(), out.end(), [](const ClockPart& a, const ClockPart& b) { return a.gate < b.gate; });
	return out;
}

char readLight(guiGate* g) {
	for (auto& hs : g->getHotspotList()) {
		if (!g->isConnected(hs.first)) continue;
		const std::vector<StateType>& st = g->getConnection(hs.first)->getState();
		if (st.empty()) return 'X';
		switch (st[0]) {
			case ONE: return '1';
			case ZERO: return '0';
			case HI_Z: return 'Z';
			case CONFLICT: return '!';
			default: return 'X';
		}
	}
	return '-';
}

// The primitives of §5, on a twin of the circuit: the same parts and wires,
// with each clock part a switch (with the same id) the checker turns on and off.
struct Sim {
	std::string twin;
	std::vector<long> clockGates;          // what pulses: the clock parts, or the clock switch
	std::vector<long> heldGates;           // clock parts held at 0 (a clock: line names a switch)
	std::map<long, std::string> base;      // every switch's power-on value
	CLDocument* doc = nullptr;
	bool settled = true;

	~Sim() { if (doc) cl_document_close(doc); }

	guiGate* gate(long id) {
		for (size_t p = 0; p < doc->pages.size(); p++) {
			auto* gl = doc->page((int)p)->getGateList();
			auto it = gl->find((unsigned long)id);
			if (it != gl->end()) return it->second;
		}
		return nullptr;
	}
	void param(long id, const std::string& name, const std::string& value) {
		guiGate* g = gate(id);
		if (!g) return;
		g->setLogicParam(name, value);
		doc->circuit.sendMessageToCore(klsMessage::Message(klsMessage::MT_SET_GATE_PARAM,
			new klsMessage::Message_SET_GATE_PARAM(g->getID(), name, value)));
	}
	bool settle() { if (!doc->sim->settle(1000)) settled = false; return settled; }
	bool powerOn() {
		if (doc) cl_document_close(doc);
		char err[256];
		cl_set_settle_on_open(false);
		doc = cl_document_open_text(twin.data(), (long)twin.size(), err, sizeof err);
		if (!doc) { fprintf(stderr, "twin: %s\n", err); exit(1); }
		settled = true;
		for (size_t p = 0; p < doc->pages.size(); p++)
			for (auto& g : *doc->page((int)p)->getGateList())
				if (g.second->getLogicType() == "REGISTER") param((long)g.first, "CURRENT_VALUE", "0");
		for (auto& b : base) param(b.first, "OUTPUT_NUM", b.second);
		for (long id : clockGates) param(id, "OUTPUT_NUM", "0");
		for (long id : heldGates) param(id, "OUTPUT_NUM", "0");
		return settle();
	}
	void set(long id, char v) { param(id, "OUTPUT_NUM", std::string(1, v)); }
	bool pulse() {
		for (long id : clockGates) param(id, "OUTPUT_NUM", "1");
		if (!settle()) return false;
		for (long id : clockGates) param(id, "OUTPUT_NUM", "0");
		return settle();
	}
	char read(long id) { guiGate* g = gate(id); return g ? readLight(g) : '-'; }
};

// ---- The checker (§6 to §10) -----------------------------------------------------

enum { Info = 0, Warning = 1, Problem = 2 };

struct Step {
	std::string kind = "pulse";   // start, set or pulse
	int pulse = 0, row = 0;
	std::string state, in, exp, got;
	bool wrong = false;
};

struct Result {
	int kind = KEmpty, verdict = 2;
	std::string error, summary;
	std::vector<std::pair<int, std::string>> notes;
	std::vector<Port> ports;
	struct Name { std::string name; bool input; int port; bool byHand; };
	std::vector<Name> names;
	struct Signal { std::string name, role; int port; };
	std::vector<Signal> signals;
	std::vector<Step> steps;
	int firstWrong = -1;
	// today's kinds
	struct Out { std::string name; int column, wrong; };
	std::vector<Out> outs;
	std::vector<int> rowWrong;
	std::vector<std::string> ttNames, ttRows;
	int ttInputs = 0;
};

struct Checker {
	CLDocument* real;
	Sim& sim;
	Key k;
	std::map<std::string, std::string> byHand;   // key(asked-for name) -> a switch's or light's name
	Result res;

	std::vector<int> switches, lights;   // port indices
	int clockSwitch = -1;
	std::vector<int> inPorts;            // the key's inputs, in the key's order
	std::vector<std::pair<int, char>> setPorts, resetPorts;
	std::vector<int> control;            // switches the start search may turn on or off
	std::vector<std::string> paired;
	bool neverSettled = false;

	Checker(CLDocument* real, Sim& sim) : real(real), sim(sim) {}

	const Port& port(int i) { return res.ports[i]; }
	// An error: verdict 2, and only the notes that say what to fix.
	Result& fail(const std::string& error, const std::string& summary) {
		res.verdict = 2; res.error = error; res.summary = summary;
		res.notes.erase(std::remove_if(res.notes.begin(), res.notes.end(), [](auto& n) { return n.first != Problem; }), res.notes.end());
		return res;
	}

	int findPort(const std::string& want, const std::vector<int>& pool, bool& hand) {
		hand = false;
		auto h = byHand.find(key(want));
		if (h != byHand.end())
			for (int p : pool) if (key(port(p).name) == key(h->second)) { hand = true; return p; }
		for (int p : pool) if (key(port(p).name) == key(want)) return p;
		return -1;
	}

	// By hand, by name, then what's left in order when both sides have the same number.
	std::vector<int> match(const std::vector<std::string>& want, const std::vector<int>& pool, bool input, std::vector<std::string>& missing) {
		std::set<int> taken;
		std::vector<int> col(want.size(), -1);
		std::vector<bool> hand(want.size(), false);
		for (size_t i = 0; i < want.size(); i++) {
			auto h = byHand.find(key(want[i]));
			if (h == byHand.end()) continue;
			for (int p : pool) if (!taken.count(p) && key(port(p).name) == key(h->second)) { col[i] = p; hand[i] = true; taken.insert(p); break; }
		}
		for (size_t i = 0; i < want.size(); i++) {
			if (col[i] >= 0) continue;
			for (int p : pool) if (!taken.count(p) && key(port(p).name) == key(want[i])) { col[i] = p; taken.insert(p); break; }
		}
		std::vector<int> left, spare;
		for (size_t i = 0; i < want.size(); i++) if (col[i] < 0) left.push_back((int)i);
		for (int p : pool) if (!taken.count(p)) spare.push_back(p);
		if (!left.empty() && left.size() == spare.size()) {
			for (size_t j = 0; j < left.size(); j++) {
				col[left[j]] = spare[j];
				paired.push_back(want[left[j]] + " is " + port(spare[j]).name);
			}
		}
		for (size_t i = 0; i < want.size(); i++) {
			res.names.push_back({ want[i], input, col[i], hand[i] });
			if (col[i] < 0) missing.push_back(want[i]);
		}
		return col;
	}

	Result& noSwitch(const std::vector<std::string>& names) {
		std::vector<std::string> all;
		for (int s : switches) all.push_back(port(s).name);
		const std::string what = names.size() == 1 ? "'s no switch called " + names[0] : " are no switches called " + list(names);
		res.notes = { { Problem, "There" + what + " (" + (all.empty() ? std::string("this page has no switches") : "the switches are " + list(all)) +
		                         "). Pick which switch " + (names.size() == 1 ? "it is" : "each one is") + " under Names, or change a switch's label." } };
		return fail("no_switch", "Can't check yet: there" + what + ".");
	}
	Result& noLight(const std::vector<std::string>& names) {
		std::vector<std::string> all;
		for (int l : lights) all.push_back(port(l).name);
		res.notes.clear();
		for (const std::string& n : names)
			res.notes.push_back({ Problem, "There's no light called " + n + " (" + (all.empty() ? std::string("this page has no lights") : "the lights are " + list(all)) +
			                               "). Pick which light it is under Names, or change a light's label." });
		const std::string what = names.size() == 1 ? "'s no light called " : " are no lights called ";
		return fail("no_light", "Can't check yet: there" + what + list(names) + ".");
	}

	std::string readPorts(const std::vector<int>& which) { std::string s; for (int p : which) s += sim.read(port(p).gate); return s; }

	std::string setsText(const std::vector<std::pair<int, char>>& sets) {
		std::vector<std::string> parts;
		for (auto& s : sets) parts.push_back(port(s.first).name + " to " + s.second);
		return "To start, the check set " + list(parts) + ", gave one clock pulse and set " + (sets.size() == 1 ? "it" : "them") + " back.";
	}
	// Set some switches, settle, a pulse, set them back, settle.
	bool withPulse(const std::vector<std::pair<int, char>>& sets) {
		for (auto& s : sets) sim.set(port(s.first).gate, s.second);
		if (!sim.settle() || !sim.pulse()) return false;
		for (auto& s : sets) sim.set(port(s.first).gate, sim.base[port(s.first).gate][0]);
		return sim.settle();
	}

	// §7: power on; then the reset: line; then the other switches, a few at a
	// time, each try from power-on. `how` says what got there ("" at power-on).
	struct Reached { bool ok = false; std::string how; };
	Reached reach(const std::vector<int>& which, const std::function<bool(const std::string&)>& good) {
		Reached r;
		if (!sim.powerOn()) { neverSettled = true; return r; }
		if (good(readPorts(which))) { r.ok = true; return r; }
		auto attempt = [&](const std::vector<std::pair<int, char>>& sets) {
			if (!sim.powerOn() || !withPulse(sets)) { neverSettled = true; return false; }
			return good(readPorts(which));
		};
		if (k.hasReset) {
			if (attempt(resetPorts)) { r.ok = true; r.how = setsText(resetPorts); return r; }
			if (neverSettled) return r;
		}
		const int c = std::min<int>(8, (int)control.size());
		for (int size = 1; size <= std::min(4, c); size++) {
			std::vector<int> idx(size);
			for (int i = 0; i < size; i++) idx[i] = i;
			for (;;) {
				std::vector<std::pair<int, char>> sets;
				for (int i : idx) sets.push_back({ control[i], sim.base[port(control[i]).gate] == "1" ? '0' : '1' });
				if (attempt(sets)) { r.ok = true; r.how = setsText(sets); return r; }
				if (neverSettled) return r;
				int i = size - 1;
				while (i >= 0 && idx[i] == c - size + i) i--;
				if (i < 0) break;
				idx[i]++;
				for (int j = i + 1; j < size; j++) idx[j] = idx[j - 1] + 1;
			}
		}
		return r;
	}

	Result& unreachable(const std::string& summary, const std::string& got) {
		const bool any = k.hasReset || !control.empty();
		res.notes = { { Problem, "At the start the lights show " + got + (any
			? ". Turning the other switches on or off, with a clock pulse, didn't get them there."
			: ", and there's no other switch to reset them with.") +
			" Add a reset (a switch on the flip-flops' CLR' or PRE'), or name it with a line like reset: CLR' = 0." } };
		return fail("start_unreachable", summary);
	}

	Result& unsettled(const std::string& where) {
		res.verdict = 1;
		res.error = "never_settles";
		res.summary = "Doesn't settle: " + where + " the circuit was still changing after 1000 steps, so a loop of gates may be flipping back and forth.";
		res.firstWrong = (int)res.steps.size() - 1;
		return res;
	}

	void oddNotes(const std::vector<std::string>& names, const std::vector<int>& at) {
		for (size_t s = 0; s < names.size(); s++) {
			std::map<char, int> odd;
			for (auto& st : res.steps)
				if (at[s] < (int)st.got.size()) { const char v = st.got[at[s]]; if (v != '0' && v != '1' && v != '~') odd[v]++; }
			for (auto& [v, count] : odd) {
				const char* what = v == 'X' ? "X (unknown): a gate feeding it may be missing an input"
				                 : v == 'Z' ? "Z (floating): nothing is driving it"
				                 : v == '!' ? "! (a conflict): two outputs are wired together"
				                            : "nothing: it isn't connected";
				res.notes.push_back({ Warning, "On " + plural(count, "step", "steps") + " the light " + names[s] + " shows " + what + "." });
			}
		}
	}

	// The notes every kind adds once its names are matched.
	void matchNotes(const std::vector<int>& usedLights) {
		if (!paired.empty()) res.notes.push_back({ Info, "Matched by position, as the names differ: " + list(paired) + "." });
		std::vector<std::string> names, values;
		for (int s : control) {
			if (std::any_of(resetPorts.begin(), resetPorts.end(), [&](auto& x) { return x.first == s; })) continue;
			names.push_back(port(s).name);
			values.push_back(sim.base[port(s).gate]);
		}
		if (!names.empty())
			res.notes.push_back({ Info, (names.size() == 1 ? "Switch " + names[0] + " isn't" : "Switches " + list(names) + " aren't") +
			                            " in what was asked for, so " + (names.size() == 1 ? "it stayed as it is" : "they stayed as they are") +
			                            " on the page (" + list(values) + ")." });
		names.clear();
		for (int l : lights) if (std::find(usedLights.begin(), usedLights.end(), l) == usedLights.end()) names.push_back(port(l).name);
		if (!names.empty())
			res.notes.push_back({ Info, (names.size() == 1 ? "Light " + names[0] + " isn't" : "Lights " + list(names) + " aren't") +
			                            " in what was asked for, so " + (names.size() == 1 ? "it wasn't" : "they weren't") + " checked." });
	}

	static std::string number(const std::string& bits) {
		if (!binary(bits)) return bits;
		long v = 0;
		for (char c : bits) v = v * 2 + (c == '1');
		return std::to_string(v);
	}
	static std::string bitsOf(long v, int n) { std::string s; for (int i = n - 1; i >= 0; i--) s += ((v >> i) & 1) ? '1' : '0'; return s; }

	Result& run(const std::string& text, int pageIndex, const std::map<std::string, std::string>& hand);
	Result& runCount();
	Result& runTiming();
	Result& runStates();
};

std::string dontCares(int n) { return " (" + plural(n, "don't-care wasn't", "don't-cares weren't") + " checked)."; }

Result& Checker::run(const std::string& text, int pageIndex, const std::map<std::string, std::string>& hand) {
	for (auto& h : hand) byHand[key(h.first)] = h.second;
	k = readKey(text);
	res.kind = k.kind;
	if (k.kind == KEmpty && !k.options)
		return fail("empty", "Type or paste what the assignment asks for: a formula, a truth table, a count, a state table or a timing table.");
	if (!k.error.empty()) return fail("bad_key", k.error);

	// The page's switches and lights, and the clock.
	res.ports = portsOf(real, pageIndex);
	for (size_t i = 0; i < res.ports.size(); i++) (res.ports[i].input ? switches : lights).push_back((int)i);
	const auto clocks = clocksOf(real);
	bool hand1;
	if (k.hasClock) {
		clockSwitch = findPort(k.clock, switches, hand1);
		res.names.push_back({ k.clock, true, clockSwitch, hand1 });
		if (clockSwitch < 0) return noSwitch({ k.clock });
	} else if (clocks.empty()) {
		for (int s : switches) if (key(port(s).name) == "clk" || key(port(s).name) == "clock") { clockSwitch = s; break; }
		if (clockSwitch < 0) {
			res.notes = { { Problem, "Add a clock (from Input and Output) to the flip-flops' clock inputs, or say which switch is the clock with a line like clock: CLK." } };
			return fail("no_clock", "Can't check yet: there's no clock.");
		}
	}
	for (auto& p : res.ports) if (p.input) sim.base[p.gate] = p.value;
	if (clockSwitch >= 0) {
		sim.clockGates = { port(clockSwitch).gate };
		sim.base[port(clockSwitch).gate] = "0";
		for (auto& c : clocks) sim.heldGates.push_back(c.gate);
	} else {
		for (auto& c : clocks) sim.clockGates.push_back(c.gate);
	}

	// The key's inputs.
	std::vector<std::string> inNames;
	if (k.kind == KTiming) {
		int ni = k.bar;
		if (ni < 0) {
			ni = 0;
			while (ni < (int)k.names.size() && findPort(k.names[ni], switches, hand1) >= 0) ni++;
			if (ni == (int)k.names.size()) return fail("bad_key", "Put a | between the switches and the lights in the top row, like Pulse | X | Z.");
		}
		inNames.assign(k.names.begin(), k.names.begin() + ni);
		k.outs.assign(k.names.begin() + ni, k.names.end());
		for (auto& r : k.rows) { r.in = r.out.substr(0, ni); r.out = r.out.substr(ni); }
	} else if (k.kind == KStates) {
		inNames = k.ins;
	}
	for (const std::string& n : inNames) {
		bool isClock;
		if (clockSwitch >= 0) {
			auto h = byHand.find(key(n));
			isClock = key(n) == key(port(clockSwitch).name) || (h != byHand.end() && key(h->second) == key(port(clockSwitch).name));
		} else {
			isClock = key(n) == "clk" || key(n) == "clock";
		}
		if (isClock) {
			res.notes = { { Problem, "Each row is one clock pulse, and the check turns the clock on and off itself. Leave " + n + " out of the table." } };
			return fail("clock_in_key", "Can't check yet: " + n + " is the clock, so it can't be a column too.");
		}
	}
	std::vector<int> pool;
	for (int s : switches) if (s != clockSwitch) pool.push_back(s);
	std::vector<std::string> missing;
	inPorts = match(inNames, pool, true, missing);
	if (!missing.empty()) return noSwitch(missing);
	// set: and reset: name switches, by name only.
	auto named = [&](const Assign& a, std::vector<std::pair<int, char>>& out) {
		for (size_t i = 0; i < a.names.size(); i++) {
			bool h;
			const int p = findPort(a.names[i], pool, h);
			res.names.push_back({ a.names[i], true, p, h });
			if (p < 0) missing.push_back(a.names[i]); else out.push_back({ p, a.bits[i] });
		}
	};
	named(k.set, setPorts);
	named(k.reset, resetPorts);
	if (!missing.empty()) return noSwitch(missing);
	for (auto& s : setPorts)
		if (std::find(inPorts.begin(), inPorts.end(), s.first) != inPorts.end())
			return fail("bad_key", port(s.first).name + " is in the table and in set:; leave it out of one.");
	for (auto& s : setPorts) sim.base[port(s.first).gate] = std::string(1, s.second);
	for (int s : pool) {
		const bool used = std::find(inPorts.begin(), inPorts.end(), s) != inPorts.end() ||
		                  std::any_of(setPorts.begin(), setPorts.end(), [&](auto& x) { return x.first == s; });
		if (!used) control.push_back(s);
	}

	// Notes on the clock (each kind adds the names').
	if (clockSwitch < 0 && clocks.size() == 1 && !clocks[0].manual)
		res.notes.push_back({ Info, "The clock runs by itself on your page; for the check it was stepped one pulse at a time." });
	if (clockSwitch < 0 && clocks.size() > 1)
		res.notes.push_back({ Info, "This circuit has " + std::to_string(clocks.size()) + " clocks; for the check they all pulsed together, one pulse at a time." });
	if (clockSwitch >= 0 && !k.hasClock)
		res.notes.push_back({ Info, "There's no clock, so the switch " + port(clockSwitch).name + " was used as one: each pulse turned it on and off." });
	if (clockSwitch >= 0 && k.hasClock && !clocks.empty())
		res.notes.push_back({ Info, "The clock: line makes " + port(clockSwitch).name + " the clock, so the clock parts on the page were held at 0." });

	if (k.kind == KCount) return runCount();
	if (k.kind == KTiming) return runTiming();
	return runStates();
}

Result& Checker::runCount() {
	// Which lights make the number (§8.1).
	std::vector<int> watch;
	const bool byDefault = k.countNames.empty();
	if (byDefault) {
		for (int l : lights) {
			const std::string n = trim(port(l).name);
			std::string base;
			if (n.size() > 1 && n.back() == '\'') base = n.substr(0, n.size() - 1);
			else if (n.size() > 3 && n.compare(n.size() - 3, 3, "\xE2\x80\x99") == 0) base = n.substr(0, n.size() - 3);
			bool complement = false;
			if (!base.empty())
				for (int o : lights) if (o != l && lower(trim(port(o).name)) == lower(trim(base))) complement = true;
			if (!complement) watch.push_back(l);
		}
		if (watch.empty()) {
			res.notes = { { Problem, "Put a light on each flip-flop's output and label them, like Q2, Q1 and Q0." } };
			return fail("no_lights", "Can't check yet: there are no lights on this page to read the count from.");
		}
		// Lights labeled one name and different numbers (Q0, Q1, Q2) go by number, highest first.
		bool numbered = watch.size() >= 2;
		std::string prefix;
		std::set<long> seen;
		std::vector<std::pair<long, int>> order;
		for (int l : watch) {
			const std::string n = trim(port(l).name);
			size_t d = n.size();
			while (d > 0 && std::isdigit((unsigned char)n[d - 1])) d--;
			bool letters = true;
			for (size_t i = 0; i < d; i++) if (!std::isalpha((unsigned char)n[i]) && n[i] != '_') letters = false;
			if (!port(l).labeled || d == n.size() || n.size() - d > 6 || !letters) { numbered = false; break; }
			const std::string p = lower(n.substr(0, d));
			const long v = std::stol(n.substr(d));
			if (order.empty()) prefix = p;
			if (p != prefix || seen.count(v)) { numbered = false; break; }
			seen.insert(v);
			order.push_back({ v, l });
		}
		if (numbered) {
			std::stable_sort(order.begin(), order.end(), [](auto& a, auto& b) { return a.first > b.first; });
			watch.clear();
			for (auto& o : order) watch.push_back(o.second);
		}
		for (int l : watch) res.names.push_back({ port(l).name, false, l, false });
	} else {
		std::vector<std::string> missing;
		watch = match(k.countNames, lights, false, missing);
		if (!missing.empty()) return noLight(missing);
	}
	const int n = (int)watch.size();
	if (n > 16) return fail("bad_key", "That's " + std::to_string(n) + " lights; a count can use up to 16.");
	// The numbers: binary when every one is 0s and 1s of the same length, 2 or more; else decimal.
	bool inBinary = true;
	for (auto& t : k.countTokens) if (t.size() < 2 || !binary(t) || t.size() != k.countTokens[0].size()) inBinary = false;
	if (inBinary && (int)k.countTokens[0].size() != n)
		return fail("bad_key", "Those numbers have " + std::to_string(k.countTokens[0].size()) + " bits, but the count has " +
		                       plural(n, "light", "lights") + ".");
	std::vector<long> values;
	const long max = (1L << n) - 1;
	for (auto& t : k.countTokens) {
		long v = 0;
		if (inBinary) for (char c : t) v = v * 2 + (c == '1');
		else v = t.size() > 9 ? max + 1 : std::stol(t);
		if (v > max)
			return fail("bad_key", t + " doesn't fit in " + plural(n, "light", "lights") + ": " + (n == 1 ? "it counts" : "they count") +
			                       " up to " + std::to_string(max) + ".");
		values.push_back(v);
	}
	if (k.cyclic && values.size() >= 2 && values.back() == values.front()) values.pop_back();
	const int L = (int)values.size();
	for (int i = 0; i < n; i++) res.signals.push_back({ byDefault ? port(watch[i]).name : k.countNames[i], "output", watch[i] });

	matchNotes(watch);
	if (byDefault) {
		std::vector<std::string> ns;
		for (int l : watch) ns.push_back(port(l).name);
		res.notes.push_back({ Info, n == 1 ? "The count is read from the light " + ns[0] + "."
		                                   : "The count is read from the lights " + joined(ns, " ") + ", most significant first." });
	}

	// The start (§7).
	auto good = [&](const std::string& bits) {
		if (!binary(bits)) return false;
		const long x = std::stol(number(bits));
		return k.cyclic ? std::find(values.begin(), values.end(), x) != values.end() : x == values[0];
	};
	Reached r = reach(watch, good);
	if (neverSettled) {
		Step s; s.kind = "start"; s.exp = bitsOf(values[0], n); s.got = std::string(n, '~'); s.wrong = true;
		res.steps.push_back(s);
		return unsettled("at the start");
	}
	if (!r.ok) {
		sim.powerOn();
		return unreachable(k.cyclic ? "Can't check yet: the lights can't be set to a number in the count to start."
		                            : "Can't check yet: the lights can't be set to " + std::to_string(values[0]) + " to start.",
		                   number(readPorts(watch)));
	}
	if (!r.how.empty()) res.notes.push_back({ Info, r.how });
	const std::string first = readPorts(watch);
	const int at = k.cyclic ? (int)(std::find(values.begin(), values.end(), std::stol(number(first))) - values.begin()) : 0;
	if (at != 0) res.notes.push_back({ Info, "The lights started at " + number(first) + ", so the check started there in the count." });
	Step s0; s0.kind = "start"; s0.exp = bitsOf(values[at], n); s0.got = first;
	res.steps.push_back(s0);
	const int pulses = k.cyclic ? 2 * L : L - 1;
	for (int p = 1; p <= pulses; p++) {
		Step s;
		s.pulse = p;
		s.exp = bitsOf(values[k.cyclic ? (at + p) % L : p], n);
		if (!sim.pulse()) {
			s.got = std::string(n, '~'); s.wrong = true;
			res.steps.push_back(s);
			return unsettled("at clock pulse " + std::to_string(p));
		}
		s.got = readPorts(watch);
		s.wrong = s.got != s.exp;
		res.steps.push_back(s);
	}
	std::vector<std::string> ns;
	std::vector<int> idx;
	for (int i = 0; i < n; i++) { ns.push_back(port(watch[i]).name); idx.push_back(i); }
	oddNotes(ns, idx);
	for (size_t i = 0; i < res.steps.size(); i++) if (res.steps[i].wrong) { res.firstWrong = (int)i; break; }
	if (res.firstWrong >= 0) {
		const Step& w = res.steps[res.firstWrong];
		const std::string got = number(w.got);
		res.verdict = 1;
		res.summary = "Doesn't match: after clock pulse " + std::to_string(w.pulse) + (n == 1 ? " the light shows " : " the lights show ") + got + ", not " + number(w.exp) +
		              (!binary(w.got) && n > 1 ? " (" + w.exp + ")" : std::string("")) + ".";
		return res;
	}
	std::vector<std::string> seq;
	for (int i = 0; i < L; i++) seq.push_back(std::to_string(values[i]));
	if (L > 8) { seq.erase(seq.begin() + 6, seq.end() - 1); seq.insert(seq.end() - 1, "\xE2\x80\xA6"); }
	res.verdict = 0;
	res.summary = std::string(n == 1 ? "Matches: the light counts " : "Matches: the lights count ") + joined(seq, ", ") +
	              (k.cyclic ? " and repeat (checked twice round)." : ".");
	return res;
}

Result& Checker::runTiming() {
	std::vector<std::string> missing;
	const std::vector<int> watch = match(k.outs, lights, false, missing);
	if (!missing.empty()) return noLight(missing);
	// start: names lights, by name only.
	std::vector<int> startPorts;
	for (auto& nm : k.start.names) {
		bool h;
		const int p = findPort(nm, lights, h);
		res.names.push_back({ nm, false, p, h });
		if (p < 0) missing.push_back(nm); else startPorts.push_back(p);
	}
	if (!missing.empty()) return noLight(missing);
	for (size_t i = 0; i < inPorts.size(); i++) res.signals.push_back({ k.names[i], "input", inPorts[i] });
	for (size_t i = 0; i < watch.size(); i++) res.signals.push_back({ k.outs[i], "output", watch[i] });
	std::vector<int> used = watch;
	used.insert(used.end(), startPorts.begin(), startPorts.end());
	matchNotes(used);

	const size_t no = watch.size();
	auto settleFailAtStart = [&]() -> Result& {
		Step s; s.kind = "start"; s.exp = std::string(no, '-'); s.got = std::string(no, '~'); s.wrong = true;
		res.steps.push_back(s);
		return unsettled("at the start");
	};
	if (k.hasStart) {
		Reached r = reach(startPorts, [&](const std::string& s) { return s == k.start.bits; });
		if (neverSettled) return settleFailAtStart();
		if (!r.ok) {
			sim.powerOn();
			std::vector<std::string> ns;
			for (int p : startPorts) ns.push_back(port(p).name);
			return unreachable("Can't check yet: the circuit can't be set to " + joined(ns, " ") + " = " + k.start.bits + " to start.",
			                   readPorts(startPorts));
		}
		if (!r.how.empty()) res.notes.push_back({ Info, r.how });
	} else {
		if (!sim.powerOn()) return settleFailAtStart();
		if (k.hasReset) {
			if (!withPulse(resetPorts)) return settleFailAtStart();
			res.notes.push_back({ Info, setsText(resetPorts) });
		}
	}
	std::string inputs;
	for (int p : inPorts) inputs += sim.base[port(p).gate];
	if (k.rows[0].pulse != 0) {
		Step s; s.kind = "start"; s.exp = std::string(no, '-'); s.got = readPorts(watch);
		res.steps.push_back(s);
	}
	int dc = 0, checked = 0;
	for (auto& row : k.rows) {
		Step s;
		s.kind = row.pulse == 0 ? "start" : "pulse";
		s.pulse = row.pulse;
		s.row = row.number;
		for (size_t i = 0; i < inPorts.size(); i++) {
			if (row.in[i] != '-') inputs[i] = row.in[i];
			sim.set(port(inPorts[i]).gate, inputs[i]);
		}
		s.in = inputs;
		s.exp = row.out;
		if (!sim.settle() || (row.pulse != 0 && !sim.pulse())) {
			s.got = std::string(no, '~'); s.wrong = true;
			res.steps.push_back(s);
			return unsettled(row.pulse == 0 ? "at the start" : "at clock pulse " + std::to_string(row.pulse));
		}
		s.got = readPorts(watch);
		for (size_t i = 0; i < no; i++) {
			if (s.exp[i] == '-') { dc++; continue; }
			checked++;
			if (s.got[i] != s.exp[i]) s.wrong = true;
		}
		res.steps.push_back(s);
	}
	std::vector<std::string> lightNames;
	std::vector<int> idx;
	for (size_t i = 0; i < no; i++) { lightNames.push_back(port(watch[i]).name); idx.push_back((int)i); }
	oddNotes(lightNames, idx);
	for (size_t i = 0; i < res.steps.size(); i++) if (res.steps[i].wrong) { res.firstWrong = (int)i; break; }
	if (res.firstWrong >= 0) {
		const Step& w = res.steps[res.firstWrong];
		std::vector<std::string> parts;
		for (size_t i = 0; i < no; i++)
			if (w.exp[i] != '-' && w.got[i] != w.exp[i]) parts.push_back(k.outs[i] + " is " + w.got[i] + " instead of " + w.exp[i]);
		res.verdict = 1;
		res.summary = "Doesn't match: " + (w.pulse == 0 ? std::string("at the start") : "after clock pulse " + std::to_string(w.pulse)) + ", " +
		              list(parts) + ".";
		return res;
	}
	res.verdict = 0;
	res.summary = checked == 0 ? "Matches, but every value was a don't-care, so nothing was really checked."
	                           : "Matches: every clock pulse gives what was asked for" + (dc > 0 ? dontCares(dc) : std::string("."));
	return res;
}

Result& Checker::runStates() {
	std::vector<std::string> want = k.state, missing;
	want.insert(want.end(), k.outs.begin(), k.outs.end());
	const std::vector<int> got = match(want, lights, false, missing);
	if (!missing.empty()) return noLight(missing);
	const int ns = (int)k.state.size(), no = (int)k.outs.size();
	const std::vector<int> statePorts(got.begin(), got.begin() + ns), outPorts(got.begin() + ns, got.end());
	// start: the state's bits, in the state's order.
	std::string startBits;
	if (k.hasStart) {
		if (k.start.names.empty()) {
			if ((int)k.start.bits.size() != ns)
				return fail("bad_key", "start: needs " + plural(ns, "value", "values") + ", one for each of " + list(k.state) + ".");
			startBits = k.start.bits;
		} else {
			startBits = std::string(ns, '?');
			for (size_t i = 0; i < k.start.names.size(); i++) {
				int at = -1;
				for (int j = 0; j < ns; j++) if (key(k.state[j]) == key(k.start.names[i])) at = j;
				if (at < 0 || startBits[at] != '?' || (int)k.start.names.size() != ns)
					return fail("bad_key", "start: gives the state, so it names " + list(k.state) + ".");
				startBits[at] = k.start.bits[i];
			}
		}
	}
	for (size_t i = 0; i < inPorts.size(); i++) res.signals.push_back({ k.ins[i], "input", inPorts[i] });
	for (int j = 0; j < ns; j++) res.signals.push_back({ k.state[j], "state", statePorts[j] });
	for (int j = 0; j < ns; j++) res.signals.push_back({ k.next[j], "next", statePorts[j] });
	for (int j = 0; j < no; j++) res.signals.push_back({ k.outs[j], "output", outPorts[j] });
	matchNotes(got);

	auto stateText = [&](const std::string& bits) { return joined(k.state, " ") + " = " + bits; };
	auto inText = [&](const std::string& bits) { return k.ins.empty() ? std::string("") : " with " + joined(k.ins, " ") + " = " + bits; };
	auto unsettledStep = [&](const std::string& kind) { Step s; s.kind = kind; s.state = std::string(ns, '~'); s.wrong = true; res.steps.push_back(s); };

	// The start, and a restart: the same again. 0 there, 1 an error, 2 never settled.
	auto start = [&](bool first) -> int {
		if (k.hasStart) {
			Reached r = reach(statePorts, [&](const std::string& s) { return s == startBits; });
			if (neverSettled) return 2;
			if (!r.ok) {
				sim.powerOn();
				unreachable("Can't check yet: the circuit can't be set to " + stateText(startBits) + " to start.", readPorts(statePorts));
				return 1;
			}
			if (first && !r.how.empty()) res.notes.push_back({ Info, r.how });
		} else {
			if (!sim.powerOn()) return 2;
			if (k.hasReset) {
				if (!withPulse(resetPorts)) return 2;
				if (first) res.notes.push_back({ Info, setsText(resetPorts) });
			}
			const std::string st = readPorts(statePorts);
			if (!binary(st)) {
				res.notes = { { Problem, "Flip-flops made from gates start unknown. Add a start: line, like start: " +
				                         stateText(std::string(ns, '0')) + ", and a reset the check can use (a switch, or a reset: line)." } };
				fail("start_unknown", ns == 1 ? "Can't check yet: at the start " + k.state[0] + " is " + st + ", not 0 or 1."
				                              : "Can't check yet: at the start the state " + joined(k.state, " ") + " is " + st + ", not 0s and 1s.");
				return 1;
			}
		}
		Step s;
		s.kind = "start";
		s.state = readPorts(statePorts);
		res.steps.push_back(s);
		return 0;
	};
	int st = start(true);
	if (st == 1) return res;
	if (st == 2) { unsettledStep("start"); return unsettled("at the start"); }
	const std::string s0 = res.steps.back().state;
	std::string cur = s0;

	const int R = (int)k.rows.size();
	std::vector<bool> done(R, false);
	std::vector<std::string> seen(R);     // the next state each row gave
	std::set<std::string> cannotSet;
	int pulses = 0, dc = 0;
	bool stopped = false, settleFail = false;

	// A row: its inputs, settle, the outputs read; a pulse; the next state read.
	auto execute = [&](int q) -> bool {
		const Key::Row& row = k.rows[q];
		Step s;
		for (int i = (int)res.steps.size() - 1; i >= 0; i--) if (res.steps[i].kind != "pulse") { s.pulse = (int)res.steps.size() - i; break; }
		s.row = row.number;
		s.state = cur;
		s.in = row.in;
		s.exp = row.nx + row.out;
		for (size_t i = 0; i < inPorts.size(); i++) sim.set(port(inPorts[i]).gate, row.in[i]);
		pulses++;
		if (!sim.settle()) settleFail = true;
		const std::string outs = settleFail ? std::string(no, '~') : readPorts(outPorts);
		if (!settleFail && !sim.pulse()) settleFail = true;
		const std::string next = settleFail ? std::string(ns, '~') : readPorts(statePorts);
		s.got = next + outs;
		if (settleFail) { s.wrong = true; res.steps.push_back(s); return false; }
		if (!done[q]) for (char c : s.exp) if (c == '-') dc++;
		for (size_t i = 0; i < s.exp.size(); i++) if (s.exp[i] != '-' && s.exp[i] != s.got[i]) s.wrong = true;
		res.steps.push_back(s);
		done[q] = true;
		seen[q] = next;
		cur = next;
		return !s.wrong;
	};
	auto rowsLeftIn = [&](const std::string& state) {
		for (int q = 0; q < R; q++) if (!done[q] && k.rows[q].st == state) return true;
		return false;
	};
	bool justRestarted = false;
	for (;;) {
		if (std::find(done.begin(), done.end(), false) == done.end()) break;
		if (pulses >= 1000) { stopped = true; break; }
		// 1. A row for the state it's in.
		int here = -1;
		for (int q = 0; q < R; q++) if (!done[q] && k.rows[q].st == cur) { here = q; break; }
		if (here >= 0) {
			justRestarted = false;
			if (!execute(here)) break;
			continue;
		}
		// 2. The nearest state with rows left, along rows already run.
		std::map<std::string, std::pair<std::string, int>> prev;
		std::vector<std::string> queue = { cur };
		prev[cur] = { "", -1 };
		std::string goal;
		for (size_t qi = 0; qi < queue.size() && goal.empty(); qi++) {
			const std::string s = queue[qi];
			for (int q = 0; q < R && goal.empty(); q++) {
				if (!done[q] || k.rows[q].st != s || prev.count(seen[q])) continue;
				prev[seen[q]] = { s, q };
				if (rowsLeftIn(seen[q])) goal = seen[q];
				queue.push_back(seen[q]);
			}
		}
		if (!goal.empty()) {
			std::vector<int> path;
			for (std::string s = goal; prev[s].second >= 0; s = prev[s].first) path.push_back(prev[s].second);
			std::reverse(path.begin(), path.end());
			bool ok = true;
			for (int q : path) if (pulses >= 1000 || !(ok = execute(q))) break;
			if (!ok) break;
			justRestarted = false;
			continue;
		}
		// 3. Back to the start.
		if (cur != s0 && !justRestarted) {
			st = start(false);
			if (st == 2) { settleFail = true; unsettledStep("start"); break; }
			cur = res.steps.back().state;
			justRestarted = true;
			continue;
		}
		// 4. Set a state with switches: the first state with rows left that hasn't failed.
		bool set = false;
		for (int q = 0; q < R && !set; q++) {
			const std::string target = k.rows[q].st;
			if (done[q] || cannotSet.count(target)) continue;
			Reached r = reach(statePorts, [&](const std::string& s) { return s == target; });
			if (neverSettled) break;
			if (!r.ok) { cannotSet.insert(target); continue; }
			Step s;
			s.kind = "set";
			s.state = readPorts(statePorts);
			res.steps.push_back(s);
			cur = s.state;
			justRestarted = false;
			set = true;
		}
		if (neverSettled) { settleFail = true; unsettledStep("set"); break; }
		if (!set) break;
	}
	if (settleFail) {
		const Step& w = res.steps.back();
		return unsettled(w.kind == "pulse" ? "in row " + std::to_string(w.row) : "at the start");
	}
	std::vector<std::string> sigNames;
	std::vector<int> idx;
	for (int j = 0; j < ns; j++) { sigNames.push_back(port(statePorts[j]).name); idx.push_back(j); }
	for (int j = 0; j < no; j++) { sigNames.push_back(port(outPorts[j]).name); idx.push_back(ns + j); }
	oddNotes(sigNames, idx);
	for (size_t i = 0; i < res.steps.size(); i++) if (res.steps[i].wrong) { res.firstWrong = (int)i; break; }
	if (res.firstWrong >= 0) {
		const Step& w = res.steps[res.firstWrong];
		std::vector<std::string> parts;
		bool nextWrong = false;
		for (int j = 0; j < ns; j++) if (w.exp[j] != '-' && w.got[j] != w.exp[j]) nextWrong = true;
		if (nextWrong) parts.push_back("the next state is " + w.got.substr(0, ns) + " instead of " + w.exp.substr(0, ns));
		for (int j = 0; j < no; j++)
			if (w.exp[ns + j] != '-' && w.got[ns + j] != w.exp[ns + j]) parts.push_back(k.outs[j] + " is " + w.got[ns + j] + " instead of " + w.exp[ns + j]);
		res.verdict = 1;
		res.summary = "Doesn't match: in state " + stateText(w.state) + inText(w.in) + " (row " + std::to_string(w.row) + "), " + list(parts) + ".";
		return res;
	}
	// Rows never reached.
	std::vector<std::string> states;
	int left = 0;
	for (int q = 0; q < R; q++)
		if (!done[q]) { left++; if (std::find(states.begin(), states.end(), k.rows[q].st) == states.end()) states.push_back(k.rows[q].st); }
	if (left > 0 && !stopped)
		res.notes.push_back({ Warning, (states.size() == 1 ? "State " + states[0] + " was" : "States " + list(states) + " were") +
		                               " never reached: clocking from the start doesn't get there, and no switch sets " +
		                               (states.size() == 1 ? "it." : "them.") });
	if (stopped) res.notes.push_back({ Warning, "The check stopped after 1000 clock pulses." });
	res.verdict = 0;
	if (R == 0) res.summary = "Matches, but every value was a don't-care, so nothing was really checked.";
	else if (left > 0)
		res.summary = "Matches on every row it reached, but " + plural(left, "row", "rows") + " (" + (states.size() == 1 ? "state " : "states ") +
		              list(states) + ") " + (left == 1 ? "wasn't reached, so it wasn't checked." : "weren't reached, so they weren't checked.");
	else res.summary = "Matches: every row of the state table does what was asked for" + (dc > 0 ? dontCares(dc) : std::string("."));
	return res;
}

}  // namespace

namespace {

// ---- Building the circuits -----------------------------------------------------

// A part, with a label beside it when it has a name (a switch's on its left,
// a light's on its right, close enough for the truth table's naming).
struct Part { std::string gate; double x, y; std::string name; bool on; };
struct Link { int a; std::string pa; int b; std::string pb; };

struct Circuit {
	std::string name;                     // what cases.json calls it
	std::vector<Part> parts;
	std::vector<Link> links;
	std::set<int> manual;                 // clock parts set to Only on Step Clock
	std::map<int, std::string> halfCycle; // clock parts with another half-period
	int add(const std::string& gate, double x, double y, const std::string& name = "", bool on = false) {
		parts.push_back({ gate, x, y, name, on });
		return (int)parts.size() - 1;
	}
	void wire(int a, const std::string& pa, int b, const std::string& pb) { links.push_back({ a, pa, b, pb }); }
};

// The circuit as the app saves it, or its twin (clock parts as switches).
std::string build(const Circuit& c, bool twin) {
	CLDocument* doc = cl_document_new();
	std::vector<CLBuildGate> gates;
	std::vector<std::string> keep;
	keep.reserve(c.parts.size() * 2 + 1);
	for (const Part& p : c.parts) {
		keep.push_back(twin && p.gate == "BB_CLOCK" ? "AA_TOGGLE" : p.gate);
		gates.push_back({ keep.back().c_str(), p.x, p.y, nullptr });
	}
	for (const Part& p : c.parts) {
		if (p.name.empty()) continue;
		const double w = 0.6 * p.name.size();
		const double x = p.gate == "AA_TOGGLE" ? p.x - 2.4 - w : p.x + 2 + w;   // labels are placed by their middle
		keep.push_back(p.name);
		gates.push_back({ "AA_LABEL", x, p.y, keep.back().c_str() });
	}
	std::vector<CLBuildWire> wires;
	for (const Link& l : c.links) {
		auto pin = [&](int i, const std::string& name) { return twin && c.parts[i].gate == "BB_CLOCK" && name == "CLK" ? "OUT_0" : name.c_str(); };
		wires.push_back({ l.a, pin(l.a, l.pa), l.b, pin(l.b, l.pb) });
	}
	const int made = cl_edit_build(doc, 0, gates.data(), (int)gates.size(), wires.data(), (int)wires.size(), "Case");
	if (made != (int)gates.size()) { fprintf(stderr, "built %d of %zu parts\n", made, gates.size()); exit(1); }
	cl_edit_select_none(doc, 0);
	for (size_t i = 0; i < c.parts.size(); i++)
		if (c.parts[i].on && !cl_document_click(doc, 0, c.parts[i].x, c.parts[i].y)) { fprintf(stderr, "couldn't turn on part %zu\n", i); exit(1); }
	if (!twin)
		for (auto& h : c.halfCycle) cl_gate_set_setting(doc, h.first + 1, "HALF_CYCLE", h.second.c_str());
	std::string text = cl_document_save_text(doc);
	cl_document_close(doc);
	// Parts are made with ids 1, 2, 3... in order; the setting Group 1 adds
	// (Only on Step Clock) goes in as the app will save it.
	if (!twin)
		for (int i : c.manual) {
			const std::string head = "(gate \"BB_CLOCK\"\n      (uuid \"" + std::to_string(i + 1) + "\")";
			size_t at = text.find(head);
			if (at == std::string::npos || (at = text.find("(lparam \"HALF_CYCLE\"", at)) == std::string::npos) { fprintf(stderr, "no clock %d\n", i); exit(1); }
			at = text.find(')', at) + 1;
			text.insert(at, "\n      (lparam \"MANUAL\" \"true\")");
		}
	// Every wire asked for is there.
	size_t wiresSaved = 0;
	for (size_t at = 0; (at = text.find("(wire\n", at)) != std::string::npos; at++) wiresSaved++;
	if (wiresSaved == 0 && !c.links.empty()) { fprintf(stderr, "no wires saved\n"); exit(1); }
	return text;
}

// A 2-bit up counter: D flip-flops, Q0 toggling, Q1 = Q1 XOR Q0.
Circuit counter2(const std::string& ff = "AE_DFF_LOW", bool presets = false, bool xLight = false) {
	Circuit c;
	c.name = std::string(ff == "AE_DFF_LOW" ? "counter2" : "counter2-falling-edge") + (presets ? "-pre-clr" : "") + (xLight ? "-open-and" : "");
	const int clk = c.add("BB_CLOCK", 0, -16);
	const int f0 = c.add(ff, 24, 8), f1 = c.add(ff, 24, -8);
	const int x = c.add("AI_XOR2", 14, -6);
	c.wire(clk, "CLK", f0, "clock"); c.wire(clk, "CLK", f1, "clock");
	c.wire(f0, "OUTINV_0", f0, "IN_0");
	c.wire(f0, "OUT_0", x, "IN_0"); c.wire(f1, "OUT_0", x, "IN_1"); c.wire(x, "OUT", f1, "IN_0");
	const int q1 = c.add("GA_LED", 60, 6, "Q1"), q0 = c.add("GA_LED", 60, 0, "Q0");
	if (xLight) {   // Q1's light through an AND gate with an input left open
		const int a = c.add("AA_AND2", 46, 6);
		c.wire(f1, "OUT_0", a, "IN_0"); c.wire(a, "OUT", q1, "N_in0");
	} else {
		c.wire(f1, "OUT_0", q1, "N_in0");
	}
	c.wire(f0, "OUT_0", q0, "N_in0");
	if (presets) {
		const int pre = c.add("AA_TOGGLE", 0, 18, "PRE'", true), clr = c.add("AA_TOGGLE", 0, -26, "CLR'", true);
		c.wire(pre, "OUT_0", f0, "set"); c.wire(pre, "OUT_0", f1, "set");
		c.wire(clr, "OUT_0", f0, "clear"); c.wire(clr, "OUT_0", f1, "clear");
	}
	return c;
}

// A 3-bit up counter from J-K flip-flops used as T flip-flops; Q0's light on top.
Circuit counter3jk(bool wrong) {
	Circuit c;
	c.name = wrong ? "counter3-jk-wrong-wire" : "counter3-jk";
	const int clk = c.add("BB_CLOCK", 0, -24);
	const int vdd = c.add("EE_VDD", 8, 24);
	const int f0 = c.add("BE_JKFF_LOW", 24, 14), f1 = c.add("BE_JKFF_LOW", 24, 0), f2 = c.add("BE_JKFF_LOW", 24, -14);
	for (int f : { f0, f1, f2 }) c.wire(clk, "CLK", f, "clock");
	c.wire(vdd, "OUT_0", f0, "J"); c.wire(vdd, "OUT_0", f0, "K");
	c.wire(f0, "Q", f1, "J"); c.wire(f0, "Q", f1, "K");
	if (wrong) {   // T2 from Q1 alone: the AND gate is left out
		c.wire(f1, "Q", f2, "J"); c.wire(f1, "Q", f2, "K");
	} else {
		const int a = c.add("AA_AND2", 14, -10);
		c.wire(f0, "Q", a, "IN_0"); c.wire(f1, "Q", a, "IN_1");
		c.wire(a, "OUT", f2, "J"); c.wire(a, "OUT", f2, "K");
	}
	const int q0 = c.add("GA_LED", 60, 10, "Q0"), q1 = c.add("GA_LED", 60, 5, "Q1"), q2 = c.add("GA_LED", 60, 0, "Q2");
	c.wire(f0, "Q", q0, "N_in0"); c.wire(f1, "Q", q1, "N_in0"); c.wire(f2, "Q", q2, "N_in0");
	return c;
}

// 0, 3, 5, 6 from D flip-flops: D2 = Q0, D1 = Q1', D0 = Q2' (wrong: D0 = Q2).
Circuit count0356(bool wrong, bool presets) {
	Circuit c;
	c.name = std::string(wrong ? "count0356-wrong-wire" : "count0356") + (presets ? "-pre-clr" : "");
	const int clk = c.add("BB_CLOCK", 0, -20);
	const int f2 = c.add("AE_DFF_LOW", 24, 12), f1 = c.add("AE_DFF_LOW", 24, 0), f0 = c.add("AE_DFF_LOW", 24, -12);
	for (int f : { f0, f1, f2 }) c.wire(clk, "CLK", f, "clock");
	c.wire(f0, "OUT_0", f2, "IN_0");
	c.wire(f1, "OUTINV_0", f1, "IN_0");
	c.wire(f2, wrong ? "OUT_0" : "OUTINV_0", f0, "IN_0");
	const int q2 = c.add("GA_LED", 60, 10, "Q2"), q1 = c.add("GA_LED", 60, 5, "Q1"), q0 = c.add("GA_LED", 60, 0, "Q0");
	c.wire(f2, "OUT_0", q2, "N_in0"); c.wire(f1, "OUT_0", q1, "N_in0"); c.wire(f0, "OUT_0", q0, "N_in0");
	if (presets) {
		const int pre = c.add("AA_TOGGLE", 0, 20, "PRE'", true), clr = c.add("AA_TOGGLE", 0, -30, "CLR'", true);
		for (int f : { f0, f1, f2 }) { c.wire(pre, "OUT_0", f, "set"); c.wire(clr, "OUT_0", f, "clear"); }
	}
	return c;
}

// A 2-bit up/down counter: Q0 toggles, Q1 = XNOR(Q1, Q0, UP); UP starts on.
Circuit updown() {
	Circuit c;
	c.name = "updown2";
	const int up = c.add("AA_TOGGLE", 0, 8, "UP", true);
	const int clk = c.add("BB_CLOCK", 0, -16);
	const int f0 = c.add("AE_DFF_LOW", 24, 8), f1 = c.add("AE_DFF_LOW", 24, -8);
	const int x = c.add("AO_XNOR3", 14, -6);
	c.wire(clk, "CLK", f0, "clock"); c.wire(clk, "CLK", f1, "clock");
	c.wire(f0, "OUTINV_0", f0, "IN_0");
	c.wire(f1, "OUT_0", x, "IN_0"); c.wire(f0, "OUT_0", x, "IN_1"); c.wire(up, "OUT_0", x, "IN_2");
	c.wire(x, "OUT", f1, "IN_0");
	const int q1 = c.add("GA_LED", 60, 6, "Q1"), q0 = c.add("GA_LED", 60, 0, "Q0");
	c.wire(f1, "OUT_0", q1, "N_in0"); c.wire(f0, "OUT_0", q0, "N_in0");
	return c;
}

// The library's 4-bit counting register, as the 4-Bit Counter template has it, with a light per bit.
Circuit register4() {
	Circuit c;
	c.name = "register4";
	const int clk = c.add("BB_CLOCK", 0, -16);
	const int reg = c.add("AA_REGISTER4", 24, 0);
	const int vdd = c.add("EE_VDD", 10, 14), gnd = c.add("FF_GND", 10, -14);
	c.wire(clk, "CLK", reg, "clock");
	c.wire(vdd, "OUT_0", reg, "count_enable"); c.wire(vdd, "OUT_0", reg, "count_up");
	c.wire(gnd, "OUT_0", reg, "load"); c.wire(gnd, "OUT_0", reg, "clear");
	const char* names[4] = { "Q3", "Q2", "Q1", "Q0" };
	for (int i = 0; i < 4; i++) {
		const int q = c.add("GA_LED", 60, 12 - 5 * i, names[i]);
		c.wire(reg, "OUT_" + std::to_string(3 - i), q, "N_in0");
	}
	return c;
}

// A Moore machine that lights Z after two 1s in a row: A = 00, B = 01, C = 10;
// Q1+ = X(Q1 + Q0), Q0+ = X(Q1 + Q0)'; Z = Q1. Wrong: Q0+ = X Q1'.
Circuit moore11(bool wrong) {
	Circuit c;
	c.name = wrong ? "moore11-wrong-wire" : "moore11";
	const int x = c.add("AA_TOGGLE", 0, 10, "X");
	const int clk = c.add("BB_CLOCK", 0, -20);
	const int f1 = c.add("AE_DFF_LOW", 36, 8), f0 = c.add("AE_DFF_LOW", 36, -10);
	const int o = c.add("AE_OR2", 14, 14), a1 = c.add("AA_AND2", 24, 10), a0 = c.add("AA_AND2", 24, -8);
	c.wire(clk, "CLK", f1, "clock"); c.wire(clk, "CLK", f0, "clock");
	c.wire(f1, "OUT_0", o, "IN_0"); c.wire(f0, "OUT_0", o, "IN_1");
	c.wire(x, "OUT_0", a1, "IN_0"); c.wire(o, "OUT", a1, "IN_1"); c.wire(a1, "OUT", f1, "IN_0");
	c.wire(x, "OUT_0", a0, "IN_0");
	if (wrong) {
		c.wire(f1, "OUTINV_0", a0, "IN_1");
	} else {
		const int n = c.add("BE_NOR2", 14, -6);
		c.wire(f1, "OUT_0", n, "IN_0"); c.wire(f0, "OUT_0", n, "IN_1"); c.wire(n, "OUT", a0, "IN_1");
	}
	c.wire(a0, "OUT", f0, "IN_0");
	const int q1 = c.add("GA_LED", 60, 10, "Q1"), q0 = c.add("GA_LED", 60, 5, "Q0"), z = c.add("GA_LED", 60, -2, "Z");
	c.wire(f1, "OUT_0", q1, "N_in0"); c.wire(f0, "OUT_0", q0, "N_in0"); c.wire(f1, "OUT_0", z, "N_in0");
	return c;
}

// A Mealy machine for two 1s in a row: Q+ = X, Z = X Q (wrong: Z = X Q').
Circuit mealy11(bool wrong) {
	Circuit c;
	c.name = wrong ? "mealy11-wrong-wire" : "mealy11";
	const int x = c.add("AA_TOGGLE", 0, 8, "X");
	const int clk = c.add("BB_CLOCK", 0, -12);
	const int f = c.add("AE_DFF_LOW", 24, 4);
	const int a = c.add("AA_AND2", 42, -4);
	c.wire(clk, "CLK", f, "clock"); c.wire(x, "OUT_0", f, "IN_0");
	c.wire(x, "OUT_0", a, "IN_0"); c.wire(f, wrong ? "OUTINV_0" : "OUT_0", a, "IN_1");
	const int q = c.add("GA_LED", 60, 6, "Q"), z = c.add("GA_LED", 60, -2, "Z");
	c.wire(f, "OUT_0", q, "N_in0"); c.wire(a, "OUT", z, "N_in0");
	return c;
}

// The J-K Flip-Flop template's layout: PRE', J, K and CLR' switches (all on),
// a clock, lights on Q and Q'. tied: one switch T drives J and K (a T flip-flop).
Circuit jk(const std::string& ff, bool manual, bool tied = false, bool jkOn = true) {
	Circuit c;
	c.name = std::string(tied ? "t-ff" : "jk-ff") + (ff == "BE_JKFF_LOW_NT" ? "-falling-edge" : "") + (manual ? "-manual-clock" : "") + (!tied && !jkOn ? "-jk-off" : "");
	const int f = c.add(ff, 24, 0);
	const int clk = c.add("BB_CLOCK", 4, 0);
	const int pre = c.add("AA_TOGGLE", 0, 14, "PRE'", true), clr = c.add("AA_TOGGLE", 0, -14, "CLR'", true);
	c.wire(clk, "CLK", f, "clock"); c.wire(pre, "OUT_0", f, "set"); c.wire(clr, "OUT_0", f, "clear");
	if (tied) {
		const int t = c.add("AA_TOGGLE", 0, 8, "T");
		c.wire(t, "OUT_0", f, "J"); c.wire(t, "OUT_0", f, "K");
	} else {
		const int j = c.add("AA_TOGGLE", 0, 8, "J", jkOn), k = c.add("AA_TOGGLE", 0, -8, "K", jkOn);
		c.wire(j, "OUT_0", f, "J"); c.wire(k, "OUT_0", f, "K");
	}
	const int q = c.add("GA_LED", 60, 3, "Q"), nq = c.add("GA_LED", 60, -3, "Q'");
	c.wire(f, "Q", q, "N_in0"); c.wire(f, "nQ", nq, "N_in0");
	if (manual) c.manual.insert(clk);
	return c;
}

// Three D flip-flops in a row (wrong: the last takes the first's Q).
Circuit shift3(bool wrong) {
	Circuit c;
	c.name = wrong ? "shift3-wrong-wire" : "shift3";
	const int din = c.add("AA_TOGGLE", 0, 8, "Din");
	const int clk = c.add("BB_CLOCK", 0, -16);
	const int a = c.add("AE_DFF_LOW", 20, 8), b = c.add("AE_DFF_LOW", 34, 0), d = c.add("AE_DFF_LOW", 48, -8);
	for (int f : { a, b, d }) c.wire(clk, "CLK", f, "clock");
	c.wire(din, "OUT_0", a, "IN_0"); c.wire(a, "OUT_0", b, "IN_0"); c.wire(wrong ? a : b, "OUT_0", d, "IN_0");
	const int q0 = c.add("GA_LED", 70, 10, "Q0"), q1 = c.add("GA_LED", 70, 5, "Q1"), q2 = c.add("GA_LED", 70, 0, "Q2");
	c.wire(a, "OUT_0", q0, "N_in0"); c.wire(b, "OUT_0", q1, "N_in0"); c.wire(d, "OUT_0", q2, "N_in0");
	return c;
}

// A D flip-flop clocked by a switch: "Load", or "CLK"; stray: a clock part wired to nothing.
Circuit dSwitchClock(const std::string& clockName, bool stray = false) {
	Circuit c;
	c.name = "d-ff-switch-" + lower(clockName) + (stray ? "-stray-clock" : "");
	if (stray) c.add("BB_CLOCK", 0, -20);
	const int d = c.add("AA_TOGGLE", 0, 6, "D"), k = c.add("AA_TOGGLE", 0, -6, clockName);
	const int f = c.add("AE_DFF_LOW", 24, 0);
	c.wire(d, "OUT_0", f, "IN_0"); c.wire(k, "OUT_0", f, "clock");
	const int q = c.add("GA_LED", 60, 2, "Q");
	c.wire(f, "OUT_0", q, "N_in0");
	return c;
}

// A toggling D flip-flop with no light.
Circuit noLights() {
	Circuit c;
	c.name = "no-lights";
	const int clk = c.add("BB_CLOCK", 0, -10);
	const int f = c.add("AE_DFF_LOW", 20, 0);
	c.wire(clk, "CLK", f, "clock"); c.wire(f, "OUTINV_0", f, "IN_0");
	return c;
}

// Two clocks (half-periods 5 and 7), each toggling one J-K flip-flop.
Circuit twoClocks() {
	Circuit c;
	c.name = "two-clocks";
	const int ca = c.add("BB_CLOCK", 0, 10), cb = c.add("BB_CLOCK", 0, -10);
	const int vdd = c.add("EE_VDD", 10, 22);
	const int f0 = c.add("BE_JKFF_LOW", 24, 10), f1 = c.add("BE_JKFF_LOW", 24, -10);
	c.wire(ca, "CLK", f0, "clock"); c.wire(cb, "CLK", f1, "clock");
	for (int f : { f0, f1 }) { c.wire(vdd, "OUT_0", f, "J"); c.wire(vdd, "OUT_0", f, "K"); }
	const int q1 = c.add("GA_LED", 60, 5, "Q1"), q0 = c.add("GA_LED", 60, 0, "Q0");
	c.wire(f1, "Q", q1, "N_in0"); c.wire(f0, "Q", q0, "N_in0");
	c.halfCycle[cb] = "7";
	return c;
}

// A toggling D flip-flop whose Q lets a NAND and two inverters ring.
Circuit oscillator() {
	Circuit c;
	c.name = "ring-after-q";
	const int clk = c.add("BB_CLOCK", 0, -10);
	const int f = c.add("AE_DFF_LOW", 20, 0);
	const int n = c.add("BA_NAND2", 36, -8), i1 = c.add("AA_INVERTER", 44, -14), i2 = c.add("AA_INVERTER", 52, -14);
	c.wire(clk, "CLK", f, "clock"); c.wire(f, "OUTINV_0", f, "IN_0");
	c.wire(f, "OUT_0", n, "IN_0"); c.wire(n, "OUT", i1, "IN_0"); c.wire(i1, "OUT_0", i2, "IN_0"); c.wire(i2, "OUT_0", n, "IN_1");
	const int q = c.add("GA_LED", 66, 4, "Q"), r = c.add("GA_LED", 66, -4, "Ring");
	c.wire(f, "OUT_0", q, "N_in0"); c.wire(n, "OUT", r, "N_in0");
	return c;
}

// The Gated D Latch template with the clock as its enable.
Circuit clockedLatch() {
	Circuit c;
	c.name = "clocked-d-latch";
	const int d = c.add("AA_TOGGLE", 0, 7, "D");
	const int clk = c.add("BB_CLOCK", 0, -1);
	const int s = c.add("BA_NAND2", 15, 6), inv = c.add("AA_INVERTER", 10, -9), r = c.add("BA_NAND2", 19, -6);
	const int q = c.add("BA_NAND2", 30, 4), nq = c.add("BA_NAND2", 30, -4);
	c.wire(d, "OUT_0", s, "IN_0"); c.wire(clk, "CLK", s, "IN_1");
	c.wire(d, "OUT_0", inv, "IN_0"); c.wire(clk, "CLK", r, "IN_0"); c.wire(inv, "OUT_0", r, "IN_1");
	c.wire(s, "OUT", q, "IN_0"); c.wire(nq, "OUT", q, "IN_1");
	c.wire(q, "OUT", nq, "IN_0"); c.wire(r, "OUT", nq, "IN_1");
	const int lq = c.add("GA_LED", 60, 4, "Q"), lnq = c.add("GA_LED", 60, -4, "Q'");
	c.wire(q, "OUT", lq, "N_in0"); c.wire(nq, "OUT", lnq, "N_in0");
	return c;
}

// Combinational ones, for today's kinds.
Circuit fullAdder() {
	Circuit c;
	c.name = "full-adder";
	const int a = c.add("AA_TOGGLE", 0, 10, "A"), b = c.add("AA_TOGGLE", 0, 4, "B"), cin = c.add("AA_TOGGLE", 0, -2, "Cin");
	const int x1 = c.add("AI_XOR2", 16, 8), x2 = c.add("AI_XOR2", 30, 6), a1 = c.add("AA_AND2", 16, -6), a2 = c.add("AA_AND2", 30, -4);
	const int o = c.add("AE_OR2", 42, -6);
	c.wire(a, "OUT_0", x1, "IN_0"); c.wire(b, "OUT_0", x1, "IN_1");
	c.wire(x1, "OUT", x2, "IN_0"); c.wire(cin, "OUT_0", x2, "IN_1");
	c.wire(a, "OUT_0", a1, "IN_0"); c.wire(b, "OUT_0", a1, "IN_1");
	c.wire(cin, "OUT_0", a2, "IN_0"); c.wire(x1, "OUT", a2, "IN_1");
	c.wire(a1, "OUT", o, "IN_0"); c.wire(a2, "OUT", o, "IN_1");
	const int s = c.add("GA_LED", 60, 8, "S"), co = c.add("GA_LED", 60, 0, "Cout");
	c.wire(x2, "OUT", s, "N_in0"); c.wire(o, "OUT", co, "N_in0");
	return c;
}

Circuit halfAdder(bool wrong) {
	Circuit c;
	c.name = wrong ? "half-adder-or" : "half-adder";
	const int a = c.add("AA_TOGGLE", 0, 6, "A"), b = c.add("AA_TOGGLE", 0, 0, "B");
	const int x = c.add(wrong ? "AE_OR2" : "AI_XOR2", 20, 6), n = c.add("AA_AND2", 20, -4);
	c.wire(a, "OUT_0", x, "IN_0"); c.wire(b, "OUT_0", x, "IN_1");
	c.wire(a, "OUT_0", n, "IN_0"); c.wire(b, "OUT_0", n, "IN_1");
	const int s = c.add("GA_LED", 60, 6, "S"), cy = c.add("GA_LED", 60, 0, "C");
	c.wire(x, "OUT", s, "N_in0"); c.wire(n, "OUT", cy, "N_in0");
	return c;
}

Circuit xor3() {
	Circuit c;
	c.name = "xor3";
	const int a = c.add("AA_TOGGLE", 0, 10, "A"), b = c.add("AA_TOGGLE", 0, 4, "B"), d = c.add("AA_TOGGLE", 0, -2, "C");
	const int x = c.add("AI_XOR3", 20, 4);
	c.wire(a, "OUT_0", x, "IN_0"); c.wire(b, "OUT_0", x, "IN_1"); c.wire(d, "OUT_0", x, "IN_2");
	const int f = c.add("GA_LED", 60, 4, "F");
	c.wire(x, "OUT", f, "N_in0");
	return c;
}

// ---- Today's kinds -------------------------------------------------------------

Result oldCheck(const std::string& cdl, int kind, const std::string& keyText, const std::string& parsed,
                const std::map<std::string, std::string>& hand) {
	Result r;
	r.kind = kind;
	cl_set_settle_on_open(true);
	char err[256];
	CLDocument* doc = cl_document_open_text(cdl.data(), (long)cdl.size(), err, sizeof err);
	if (!doc) { fprintf(stderr, "open: %s\n", err); exit(1); }
	CLTruthTable* tt = cl_truth_table(doc, 0, err, sizeof err);
	if (!tt) { fprintf(stderr, "truth table: %s\n", err); exit(1); }
	std::string names;
	for (auto& h : hand) names += h.first + "\t" + h.second + "\n";
	CLCheck* c = kind == KFormula ? cl_check_expected(tt, parsed.c_str(), names.c_str()) : cl_check_table(tt, keyText.c_str(), names.c_str());
	r.verdict = cl_check_verdict(c);
	r.summary = cl_check_summary(c);
	for (int i = 0; i < cl_check_note_count(c); i++) r.notes.push_back({ cl_check_note_kind(c, i), cl_check_note(c, i) });
	for (int k = 0; k < cl_check_outputs(c); k++) r.outs.push_back({ cl_check_output_name(c, k), cl_check_output_column(c, k), cl_check_output_wrong(c, k) });
	for (int row = 0; row < cl_tt_rows(tt); row++) if (cl_check_row_wrong(c, row)) r.rowWrong.push_back(row);
	for (int i = 0; i < cl_check_name_count(c); i++)
		r.names.push_back({ cl_check_name(c, i), cl_check_name_is_input(c, i), cl_check_name_column(c, i), cl_check_name_by_hand(c, i) });
	r.ttInputs = cl_tt_inputs(tt);
	for (int col = 0; col < cl_tt_columns(tt); col++) r.ttNames.push_back(cl_tt_name(tt, col));
	for (int row = 0; row < cl_tt_rows(tt); row++) {
		std::string s;
		for (int col = 0; col < cl_tt_columns(tt); col++) s += cl_tt_cell(tt, row, col);
		r.ttRows.push_back(s);
	}
	cl_check_free(c);
	cl_tt_free(tt);
	cl_document_close(doc);
	return r;
}

// ---- The cases -------------------------------------------------------------------

// What a case should give, worked out by hand: the verdict and error; for a
// wrong one, its first wrong step (the pulse for a count or a timing table,
// the row for a state table) and what that step expected and got; how many
// steps, when that's worth pinning down.
struct Want {
	int verdict;
	std::string error;
	int at = -1;
	std::string exp, got;
	int steps = -1;
};

struct Case {
	std::string id, title;
	Circuit circuit;
	std::string key;
	Want want;
	std::map<std::string, std::string> names;
	std::string parsed;    // a formula, as the app's formula reader gives it to cl_check_expected
};

std::vector<Case> cases() {
	std::vector<Case> v;
	auto add = [&](const std::string& id, const std::string& title, const Circuit& c, const std::string& key, const Want& w) {
		v.push_back({ id, title, c, key, w, {}, "" });
	};
	// Counts.
	add("count-2bit-up", "A 2-bit up counter from D flip-flops counts 0 to 3 and repeats",
	    counter2(), "0, 1, 2, 3, repeat", { 0, "", -1, "", "", 9 });
	add("count-2bit-binary-names", "The same, with the lights named first and the numbers in binary",
	    counter2(), "Q1 Q0: 00 01 10 11 ...", { 0, "", -1, "", "", 9 });
	add("count-2bit-arrows-finite", "A count without repeat checks just the pulses written",
	    counter2(), "0 -> 1 -> 2 -> 3 -> 0", { 0, "", -1, "", "", 5 });
	add("count-3bit-jk", "A 3-bit up counter from J-K flip-flops, Q0's light on top: read as Q2 Q1 Q0 by their numbers",
	    counter3jk(false), "0 1 2 3 4 5 6 7 ...", { 0, "", -1, "", "", 17 });
	add("count-3bit-jk-wrong-wire", "One wire wrong: Q2 toggles on Q1 alone, so 2 goes to 7",
	    counter3jk(true), "0 1 2 3 4 5 6 7 ...", { 1, "", 3, "011", "111" });
	add("count-0356", "A counter for 0, 3, 5, 6 from D flip-flops with no gates (D2 = Q0, D1 = Q1', D0 = Q2')",
	    count0356(false, false), "0, 3, 5, 6, repeat", { 0, "", -1, "", "", 9 });
	add("count-0356-wrong-wire", "D0 wired to Q2 instead of Q2': 0 goes to 2",
	    count0356(true, false), "0, 3, 5, 6, repeat", { 1, "", 1, "011", "010" });
	add("count-0356-start-midway", "A repeating count may start anywhere in it: 3, 5, 6, 0 is the same cycle",
	    count0356(false, false), "3, 5, 6, 0, repeat", { 0, "", -1, "", "", 9 });
	add("count-updown-set-down", "An up/down counter counting down, its UP switch held at 0 by a set: line",
	    updown(), "set: UP = 0\n0, 3, 2, 1, repeat", { 0, "", -1, "", "", 9 });
	add("count-updown-switch-as-is", "The same counter with UP left as it is on the page (on): it counts up",
	    updown(), "0 1 2 3 ...", { 0, "", -1, "", "", 9 });
	add("count-register-16", "The library's counting register, all 16 values (the summary shortens the list)",
	    register4(), "0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 repeat", { 0, "", -1, "", "", 33 });
	add("count-falling-edge", "Falling-edge D flip-flops: a pulse is a whole clock cycle, so it counts the same",
	    counter2("AE_DFF_LOW_NT"), "0, 1, 2, 3, repeat", { 0, "", -1, "", "", 9 });
	add("count-jk-toggle-q", "A J-K flip-flop with J = K = 1 toggles; Q' is left out of the number as Q's complement",
	    jk("BE_JKFF_LOW", false), "0, 1, repeat", { 0, "", -1, "", "", 5 });
	add("count-two-clocks", "Two clocks with different half-periods pulse together in a check",
	    twoClocks(), "0, 3, repeat", { 0, "", -1, "", "", 5 });
	add("count-start-with-pre", "A count that starts at 3: the check finds that PRE' sets it there",
	    counter2("AE_DFF_LOW", true), "3, 0, 1, 2", { 0, "", -1, "", "", 4 });
	add("count-unknown-light", "Q1's light goes through an AND gate with an open input: X whenever Q1 is 1",
	    counter2("AE_DFF_LOW", false, true), "Q1 Q0: 0 1 2 3 ...", { 1, "", 2, "10", "X0" });
	add("count-oscillates", "Once Q is 1 a NAND and two inverters ring: the check stops at that pulse",
	    oscillator(), "Q: 0, 1, repeat", { 1, "never_settles", 1, "1", "~" });
	add("count-start-unreachable", "A count that must start at 2 on a counter with no reset",
	    counter2(), "2, 3, 0, 1", { 2, "start_unreachable" });
	// Timing tables.
	add("timing-updown", "The up/down counter with UP changing between pulses",
	    updown(), "Pulse | UP | Q1 Q0\n0 | 1 | 0 0\n1 | 1 | 0 1\n2 | 1 | 1 0\n3 | 0 | 0 1\n4 | 0 | 0 0\n5 | 0 | 1 1\n6 | 1 | 0 0",
	    { 0, "", -1, "", "", 7 });
	add("timing-moore-detector", "A Moore detector for two 1s in a row, input by input",
	    moore11(false), "Pulse | X | Z\n0 | 0 | 0\n1 | 1 | 0\n2 | 1 | 1\n3 | 1 | 1\n4 | 0 | 0\n5 | 1 | 0\n6 | 0 | 0",
	    { 0, "", -1, "", "", 7 });
	add("timing-jk-characteristic", "A J-K flip-flop with a manual clock through set, hold, reset and toggle",
	    jk("BE_JKFF_LOW", true, false, false), "Pulse | J K | Q\n1 | 1 0 | 1\n2 | 0 0 | 1\n3 | 0 1 | 0\n4 | 1 1 | 1\n5 | 1 1 | 0\n6 | 0 0 | 0",
	    { 0, "", -1, "", "", 7 });
	add("timing-t-flip-flop", "A T flip-flop (J and K tied): toggles while T is 1, holds while it's 0",
	    jk("BE_JKFF_LOW", false, true), "Step T | Q\n1 1 | 1\n2 1 | 0\n3 0 | 0\n4 1 | 1", { 0, "", -1, "", "", 5 });
	add("timing-jk-falling-edge", "A falling-edge J-K flip-flop toggling from a reset: line, with a row 0 for the start",
	    jk("BE_JKFF_LOW_NT", false), "reset: CLR' = 0\nPulse | Q\n0 | 0\n1 | 1\n2 | 0\n3 | 1", { 0, "", -1, "", "", 4 });
	add("timing-jk-falling-edge-power-on", "Without a reset, a falling-edge J-K flip-flop with J = K = 1 starts at 1: "
	    "its inverted clock input takes the clock settling to 0 at power-on as an edge (the engine does this whenever the circuit opens)",
	    jk("BE_JKFF_LOW_NT", false), "Pulse | Q\n0 | 0\n1 | 1\n2 | 0", { 1, "", 0, "0", "1", 3 });
	add("timing-shift-register", "A 3-bit shift register: Din moves along one place a pulse",
	    shift3(false), "Pulse | Din | Q0 Q1 Q2\n1 | 1 | 1 0 0\n2 | 0 | 0 1 0\n3 | 1 | 1 0 1\n4 | 1 | 1 1 0\n5 | 0 | 0 1 1",
	    { 0, "", -1, "", "", 6 });
	add("timing-shift-register-wrong", "The last stage wired to the first: Q2 is wrong after pulse 2",
	    shift3(true), "Pulse | Din | Q0 Q1 Q2\n1 | 1 | 1 0 0\n2 | 0 | 0 1 0\n3 | 1 | 1 0 1\n4 | 1 | 1 1 0\n5 | 0 | 0 1 1",
	    { 1, "", 2, "010", "011" });
	add("timing-clock-switch", "No clock part, but a switch named CLK: it is the clock",
	    dSwitchClock("CLK"), "Pulse | D | Q\n1 | 1 | 1\n2 | 0 | 0\n3 | 1 | 1", { 0, "", -1, "", "", 4 });
	add("timing-clock-line", "A clock: line makes the switch Load the clock",
	    dSwitchClock("Load"), "clock: Load\nPulse | D | Q\n1 | 1 | 1\n2 | 0 | 0", { 0, "", -1, "", "", 3 });
	add("timing-dont-cares", "Don't-cares in a timing table aren't checked",
	    moore11(false), "Pulse | X | Q1 Q0 Z\n1 | 1 | 0 1 -\n2 | 1 | 1 0 1\n3 | 0 | - - 0", { 0, "", -1, "", "", 4 });
	// State tables.
	add("states-moore-detector", "The Moore detector's state table, its unused state left as don't-cares",
	    moore11(false), "Q1 Q0 X | Q1+ Q0+ | Z\n0 0 0 | 0 0 | 0\n0 0 1 | 0 1 | 0\n0 1 0 | 0 0 | 0\n0 1 1 | 1 0 | 0\n1 0 0 | 0 0 | 1\n1 0 1 | 1 0 | 1\n1 1 - | - - | -",
	    { 0, "", -1, "", "", 10 });
	add("states-moore-wrong-wire", "Q0+ wired to X Q1': from 01 with X = 1 it goes to 11",
	    moore11(true), "Q1 Q0 X | Q1+ Q0+ | Z\n0 0 0 | 0 0 | 0\n0 0 1 | 0 1 | 0\n0 1 0 | 0 0 | 0\n0 1 1 | 1 0 | 0\n1 0 0 | 0 0 | 1\n1 0 1 | 1 0 | 1\n1 1 - | - - | -",
	    { 1, "", 4, "100", "110", 6 });
	add("states-mealy-detector", "A Mealy detector: Z is read before each pulse, with the row's input on",
	    mealy11(false), "Q X | Q+ | Z\n0 0 | 0 | 0\n0 1 | 1 | 0\n1 0 | 0 | 0\n1 1 | 1 | 1", { 0, "", -1, "", "", 6 });
	add("states-mealy-wrong-wire", "Z wired to X Q': wrong in state 0 with X = 1",
	    mealy11(true), "Q X | Q+ | Z\n0 0 | 0 | 0\n0 1 | 1 | 0\n1 0 | 0 | 0\n1 1 | 1 | 1", { 1, "", 2, "10", "11", 3 });
	add("states-t-flip-flop", "A T flip-flop's characteristic table, written with (t) and (t+1)",
	    jk("BE_JKFF_LOW", false, true), "Q(t) T | Q(t+1)\n0 0 | 0\n0 1 | 1\n1 0 | 1\n1 1 | 0", { 0, "", -1, "", "", 5 });
	add("states-all-states-set-by-pre", "Every state of the 0, 3, 5, 6 counter: 111 is set by PRE', and 100, 010, 001 follow from it",
	    count0356(false, true),
	    "Q2 Q1 Q0 | Q2+ Q1+ Q0+\n0 0 0 | 0 1 1\n0 0 1 | 1 1 1\n0 1 0 | 0 0 1\n0 1 1 | 1 0 1\n1 0 0 | 0 1 0\n1 0 1 | 1 1 0\n1 1 0 | 0 0 0\n1 1 1 | 1 0 0",
	    { 0, "", -1, "", "", 10 });
	add("states-not-self-correcting", "Asked to go from every unused state to 000, it goes from 111 to 100",
	    count0356(false, true),
	    "Q2 Q1 Q0 | Q2+ Q1+ Q0+\n0 0 0 | 0 1 1\n0 0 1 | 0 0 0\n0 1 0 | 0 0 0\n0 1 1 | 1 0 1\n1 0 0 | 0 0 0\n1 0 1 | 1 1 0\n1 1 0 | 0 0 0\n1 1 1 | 0 0 0",
	    { 1, "", 8, "000", "100", 7 });
	add("states-unreached", "Without PRE' or CLR', the unused states can't be reached: their rows aren't checked",
	    count0356(false, false),
	    "Q2 Q1 Q0 | Q2+ Q1+ Q0+\n0 0 0 | 0 1 1\n0 0 1 | 1 1 1\n0 1 0 | 0 0 1\n0 1 1 | 1 0 1\n1 0 0 | 0 1 0\n1 0 1 | 1 1 0\n1 1 0 | 0 0 0\n1 1 1 | 1 0 0",
	    { 0, "", -1, "", "", 5 });
	add("states-start-line", "A start: line the check reaches with PRE', into the counter's unused states",
	    count0356(false, true), "start: Q2 Q1 Q0 = 111\nQ2 Q1 Q0 | Q2+ Q1+ Q0+\n1 1 1 | 1 0 0\n1 0 0 | 0 1 0\n0 1 0 | 0 0 1\n0 0 1 | 1 1 1",
	    { 0, "", -1, "", "", 5 });
	add("states-latch-unknown", "A gated D latch clocked by the clock starts unknown",
	    clockedLatch(), "Q D | Q+\n0 0 | 0\n0 1 | 1\n1 0 | 0\n1 1 | 1", { 2, "start_unknown" });
	add("states-latch-start-unreachable", "The same with start: Q = 0: there's no switch to reset it with",
	    clockedLatch(), "start: Q = 0\nQ D | Q+\n0 0 | 0\n0 1 | 1\n1 0 | 0\n1 1 | 1", { 2, "start_unreachable" });
	add("states-latch-reset-line", "A reset: line that loads D = 0 with one pulse gives the latch a start",
	    clockedLatch(), "reset: D = 0\nQ D | Q+\n0 0 | 0\n0 1 | 1\n1 0 | 0\n1 1 | 1", { 0, "", -1, "", "", 6 });
	// Errors.
	add("error-no-clock", "A flip-flop clocked by a switch named Load, and no clock: line",
	    dSwitchClock("Load"), "Pulse | D | Q\n1 | 1 | 1\n2 | 0 | 0", { 2, "no_clock" });
	add("error-clock-in-key", "The clock switch as a column of the timing table",
	    dSwitchClock("CLK"), "Pulse | CLK D | Q\n1 | 1 1 | 1", { 2, "clock_in_key" });
	add("error-no-light", "A count naming a light the page doesn't have",
	    counter2(), "Q2 Q1 Q0: 0 1 2 3 ...", { 2, "no_light" });
	add("error-no-switch", "A timing table with an input the page doesn't have",
	    moore11(false), "Pulse | X Y | Z\n1 | 1 0 | 0", { 2, "no_switch" });
	add("error-number-too-big", "A count with a number too big for its lights",
	    counter2(), "0, 1, 2, 9, repeat", { 2, "bad_key" });
	add("error-row-length", "A state table row with a value missing",
	    moore11(false), "Q1 Q0 X | Q1+ Q0+ | Z\n0 0 0 | 0 0 | 0\n0 0 1 | 0 1", { 2, "bad_key" });
	add("error-pulse-numbers", "A timing table that skips a pulse",
	    moore11(false), "Pulse | X | Z\n1 | 1 | 0\n3 | 1 | 1", { 2, "bad_key" });
	add("error-start-with-count", "A start: line with a count",
	    counter2(), "start: Q1 Q0 = 00\n0 1 2 3 ...", { 2, "bad_key" });
	add("error-options-with-formula", "A set: line with a formula",
	    halfAdder(false), "set: A = 1\nS = A ^ B", { 2, "bad_key" });
	add("error-only-options", "Only a start: line",
	    counter2(), "start: Q1 Q0 = 00", { 2, "bad_key" });
	add("error-empty", "Nothing typed",
	    counter2(), "", { 2, "empty" });
	add("error-no-lights", "A count on a page with no lights",
	    noLights(), "0, 1, repeat", { 2, "no_lights" });
	add("error-no-switches-two", "Two inputs the page doesn't have",
	    moore11(false), "Pulse | X Y W | Z\n1 | 1 0 0 | 0", { 2, "no_switch" });
	add("error-two-start-lines", "Two start: lines",
	    moore11(false), "start: Q1 Q0 = 00\nstart: Q1 Q0 = 01\nQ1 Q0 X | Q1+ Q0+ | Z\n0 0 0 | 0 0 | 0", { 2, "bad_key" });
	add("error-start-syntax", "A start: line with a value that isn't 0 or 1",
	    moore11(false), "start: Q1 Q0 = 0x\nQ1 Q0 X | Q1+ Q0+ | Z\n0 0 0 | 0 0 | 0", { 2, "bad_key" });
	add("error-reset-syntax", "A reset: line without names",
	    counter2(), "reset: 0\n0 1 2 3 ...", { 2, "bad_key" });
	add("error-set-values", "A set: line with more values than names",
	    updown(), "set: UP = 01\n0 1 2 3 ...", { 2, "bad_key" });
	add("error-clock-syntax", "A clock: line with a value",
	    dSwitchClock("CLK"), "clock: CLK = 1\nPulse | D | Q\n1 | 1 | 1", { 2, "bad_key" });
	add("error-timing-no-names", "A timing table with only its pulse column",
	    moore11(false), "Pulse\n1\n2", { 2, "bad_key" });
	add("error-timing-no-pulse-number", "A timing row that doesn't start with its pulse",
	    moore11(false), "Pulse | X | Z\nA | 1 | 0", { 2, "bad_key" });
	add("error-timing-bad-value", "A timing row with a 2 in it",
	    moore11(false), "Pulse | X | Z\n1 | 1 | 2", { 2, "bad_key" });
	add("error-timing-no-bar", "A timing table without | whose names are all switches",
	    dSwitchClock("Load"), "clock: Load\nPulse D\n1 1\n2 0", { 2, "bad_key" });
	add("error-set-and-input", "A switch both in the table and in set:",
	    updown(), "set: UP = 1\nPulse | UP | Q1 Q0\n1 | 1 | 0 1", { 2, "bad_key" });
	add("error-binary-width", "Binary values longer than the count's lights",
	    counter2(), "000 001 010 011 ...", { 2, "bad_key" });
	add("error-states-twice", "A state table naming a column twice",
	    moore11(false), "Q1 Q1 X | Q1+ | Z\n0 0 0 | 0 | 0", { 2, "bad_key" });
	add("error-states-no-present", "A next state with no present state",
	    moore11(false), "Q1 X | Q2+ | Z\n0 0 | 0 | 0", { 2, "bad_key" });
	add("error-states-conflict", "Two rows (one spread by a don't-care) disagree",
	    moore11(false), "Q1 Q0 X | Q1+ Q0+ | Z\n0 0 - | 0 0 | 0\n0 0 1 | 0 1 | 0", { 2, "bad_key" });
	add("error-states-too-many-rows", "A row with eleven don't-cares on the left",
	    moore11(false), "Q A B C D E F G H I J | Q+\n- - - - - - - - - - - | 0", { 2, "bad_key" });
	add("error-states-start-names", "A start: line naming only part of the state",
	    moore11(false), "start: Q1 = 0\nQ1 Q0 X | Q1+ Q0+ | Z\n0 0 0 | 0 0 | 0", { 2, "bad_key" });
	add("error-states-start-length", "A start: line with too few bits",
	    moore11(false), "start: 0\nQ1 Q0 X | Q1+ Q0+ | Z\n0 0 0 | 0 0 | 0", { 2, "bad_key" });
	add("count-start-unreachable-repeat", "A repeating count that doesn't include where the counter powers on",
	    counter2(), "1, 2, repeat", { 2, "start_unreachable" });
	add("error-no-lights-two", "A count naming two lights the page doesn't have",
	    counter2(), "Q3 Q2 Q1 Q0: 0 1 2 3 ...", { 2, "no_light" });
	add("error-set-syntax", "A set: line without a value",
	    updown(), "set: UP\n0 1 2 3 ...", { 2, "bad_key" });
	add("count-start-unreachable-tried", "A count from 1 on a counter whose PRE' and CLR' only give 3 and 0",
	    counter2("AE_DFF_LOW", true), "1, 2, 3", { 2, "start_unreachable" });
	add("timing-all-dont-cares", "A timing table of nothing but don't-cares",
	    moore11(false), "Pulse | X | Z\n1 | 1 | -\n2 | 0 | -", { 0, "", -1, "", "", 3 });
	add("timing-clock-line-stray-clock", "A clock: line on a page that also has a clock part, wired to nothing",
	    dSwitchClock("Load", true), "clock: Load\nPulse | D | Q\n1 | 1 | 1\n2 | 0 | 0", { 0, "", -1, "", "", 3 });
	add("states-dont-care", "A state table with a don't-care output",
	    mealy11(false), "Q X | Q+ | Z\n0 0 | 0 | -\n0 1 | 1 | 0\n1 0 | 0 | 0\n1 1 | 1 | 1", { 0, "", -1, "", "", 6 });
	// Today's kinds, unchanged.
	{
		Case c{ "today-formula", "Today's formulas: a full adder", fullAdder(), "S = A ^ B ^ Cin\nCout = AB + Cin(A ^ B)", { 0, "" }, {},
		        "in\tA\tB\tCin\nout\tS\t01101001\nout\tCout\t00010111" };
		v.push_back(c);
		Case m{ "today-minterms", "Today's minterm list", xor3(), "F(A,B,C) = \xCE\xA3m(1,2,4,7)", { 0, "" }, {}, "in\tA\tB\tC\nout\tF\t01101001" };
		v.push_back(m);
		Case p{ "today-formula-by-position", "Today's matching by position: G is the light C", halfAdder(false), "S = A ^ B\nG = AB", { 0, "" }, {},
		        "in\tA\tB\nout\tS\t0110\nout\tG\t0001" };
		v.push_back(p);
	}
	add("today-table", "Today's pasted truth table: a half adder",
	    halfAdder(false), "A B | S C\n0 0 | 0 0\n0 1 | 1 0\n1 0 | 1 0\n1 1 | 0 1", { 0, "" });
	add("today-table-wrong", "Today's truth table on a half adder with OR for S",
	    halfAdder(true), "A B | S C\n0 0 | 0 0\n0 1 | 1 0\n1 0 | 1 0\n1 1 | 0 1", { 1, "" });
	add("today-table-dont-care", "Today's truth table with a don't-care",
	    halfAdder(false), "A B | S C\n0 0 | 0 X\n0 1 | 1 0\n1 0 | 1 0\n1 1 | 0 1", { 0, "" });
	{
		Case h{ "today-table-by-hand", "Today's names by hand: the table's Sum is the light S", halfAdder(false),
		        "A B | Sum C\n0 0 | 0 0\n0 1 | 1 0\n1 0 | 1 0\n1 1 | 0 1", { 0, "" }, { { "Sum", "S" } }, "" };
		v.push_back(h);
		Case t{ "states-names-by-hand", "Sequential names by hand: the table's In is the switch X", mealy11(false),
		        "Q In | Q+ | Z\n0 0 | 0 | 0\n0 1 | 1 | 0\n1 0 | 0 | 0\n1 1 | 1 | 1", { 0, "", -1, "", "", 6 }, { { "In", "X" } }, "" };
		v.push_back(t);
	}
	return v;
}

// Texts and the kind each is read as (§2).
struct DetectCase { std::string text; int kind; bool options; };
std::vector<DetectCase> detectCases() {
	return {
		{ "", KEmpty, false }, { "  \n\t\n", KEmpty, false },
		{ "S = A ^ B", KFormula, false }, { "F(A,B,C) = \xCE\xA3m(1,2,4,7)", KFormula, false }, { "A'B + AC", KFormula, false },
		{ "AB\nC", KFormula, false }, { "1", KFormula, false }, { "5, repeat", KFormula, false }, { "0, 3, five, 6", KFormula, false },
		{ "S = A ^ B\nCout = AB", KFormula, false },
		{ "A B | F\n0 0 | 0\n0 1 | 1\n1 0 | 1\n1 1 | 0", KTable, false }, { "0 0 0\n0 1 1\n1 0 1\n1 1 0", KTable, false },
		{ "CLK D | Q\n0 0 | 0\n0 1 | 0\n1 0 | 0\n1 1 | 1", KTable, false }, { "Clock | Q\n0 | 0\n1 | 1", KTable, false },
		{ "0, 3, 5, 6, repeat", KCount, false }, { "0 3 5 6 ...", KCount, false }, { "0 3 5 6\xE2\x80\xA6", KCount, false },
		{ "Q2 Q1 Q0: 000 011 101 110 (repeat)", KCount, false }, { "0 -> 1 -> 2 -> 3 -> 0", KCount, false },
		{ "0 \xE2\x86\x92 1 \xE2\x86\x92 2, and repeat", KCount, false }, { "0 1", KCount, false },
		{ "Q1 Q0 X | Q1+ Q0+ | Z\n0 0 0 | 0 1 | 0", KStates, false }, { "Q X | Q | Z\n0 0 | 0 | 0\n0 1 | 1 | 0", KStates, false },
		{ "Q(t) T | Q(t+1)\n0 0 | 0\n0 1 | 1", KStates, false }, { "A B X | A* B*\n0 0 1 | 0 1", KStates, false },
		{ "Pulse | X | Z\n1 | 1 | 0", KTiming, false }, { "edge X Z\n0 0 0\n1 1 0", KTiming, false }, { "# | J K | Q\n1 | 1 0 | 1", KTiming, false },
		{ "Clock | X | Z\n1 | 1 | 0\n2 | 0 | 1\n3 | 1 | 1", KTiming, false },
		{ "start: Q1 Q0 = 00\nQ1 Q0 X | Q1+ Q0+\n0 0 0 | 0 1", KStates, true }, { "set: EN = 1\n0 1 2 3 repeat", KCount, true },
		{ "Clock: CLK\nPulse | D | Q\n1 | 1 | 1", KTiming, true }, { "start: 00", KEmpty, true }, { "set: A = 1\nS = A ^ B", KFormula, true },
	};
}

// ---- JSON ------------------------------------------------------------------------

std::string q(const std::string& s) {
	std::string o = "\"";
	for (unsigned char c : s) {
		if (c == '"') o += "\\\"";
		else if (c == '\\') o += "\\\\";
		else if (c == '\n') o += "\\n";
		else if (c == '\t') o += "\\t";
		else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
		else o += (char)c;
	}
	return o + "\"";
}

std::string resultJson(const Result& r, bool sequential) {
	std::string o = "{\n      \"kind\": " + q(kindName(r.kind)) + ",\n      \"verdict\": " + std::to_string(r.verdict) +
	                ",\n      \"error\": " + q(r.error) + ",\n      \"summary\": " + q(r.summary) + ",\n      \"notes\": [";
	for (size_t i = 0; i < r.notes.size(); i++) o += std::string(i ? "," : "") + "\n        [" + std::to_string(r.notes[i].first) + ", " + q(r.notes[i].second) + "]";
	o += r.notes.empty() ? "]" : "\n      ]";
	o += ",\n      \"names\": [";
	for (size_t i = 0; i < r.names.size(); i++)
		o += std::string(i ? ", " : "") + "[" + q(r.names[i].name) + ", " + (r.names[i].input ? "true" : "false") + ", " +
		     std::to_string(r.names[i].port) + ", " + (r.names[i].byHand ? "true" : "false") + "]";
	o += "]";
	if (sequential) {
		o += ",\n      \"ports\": [";
		for (size_t i = 0; i < r.ports.size(); i++) o += std::string(i ? ", " : "") + "[" + q(r.ports[i].name) + ", " + (r.ports[i].input ? "true" : "false") + "]";
		o += "],\n      \"signals\": [";
		for (size_t i = 0; i < r.signals.size(); i++)
			o += std::string(i ? ", " : "") + "[" + q(r.signals[i].name) + ", " + q(r.signals[i].role) + ", " + std::to_string(r.signals[i].port) + "]";
		o += "],\n      \"firstWrong\": " + std::to_string(r.firstWrong) + ",\n      \"steps\": [";
		for (size_t i = 0; i < r.steps.size(); i++) {
			const Step& s = r.steps[i];
			o += std::string(i ? "," : "") + "\n        {\"kind\": " + q(s.kind) + ", \"pulse\": " + std::to_string(s.pulse) + ", \"row\": " + std::to_string(s.row) +
			     ", \"state\": " + q(s.state) + ", \"in\": " + q(s.in) + ", \"exp\": " + q(s.exp) + ", \"got\": " + q(s.got) +
			     ", \"wrong\": " + (s.wrong ? "true" : "false") + "}";
		}
		o += r.steps.empty() ? "]" : "\n      ]";
	} else if (!r.ttNames.empty()) {
		o += ",\n      \"table\": {\"names\": [";
		for (size_t i = 0; i < r.ttNames.size(); i++) o += std::string(i ? ", " : "") + q(r.ttNames[i]);
		o += "], \"inputs\": " + std::to_string(r.ttInputs) + ", \"rows\": [";
		for (size_t i = 0; i < r.ttRows.size(); i++) o += std::string(i ? ", " : "") + q(r.ttRows[i]);
		o += "]},\n      \"outputs\": [";
		for (size_t i = 0; i < r.outs.size(); i++)
			o += std::string(i ? ", " : "") + "[" + q(r.outs[i].name) + ", " + std::to_string(r.outs[i].column) + ", " + std::to_string(r.outs[i].wrong) + "]";
		o += "],\n      \"rowWrong\": [";
		for (size_t i = 0; i < r.rowWrong.size(); i++) o += std::string(i ? ", " : "") + std::to_string(r.rowWrong[i]);
		o += "]";
	}
	return o + "\n    }";
}

}  // namespace

int main(int argc, char** argv) {
	if (argc < 3) { fprintf(stderr, "usage: make_check_cases <cl_gatedefs.xml> <out cases.json>\n"); return 2; }
	if (!cl_library_load(argv[1])) { fprintf(stderr, "couldn't load the gate library\n"); return 1; }
	int bad = 0;

	std::string json = "{\n  \"format\": 1,\n  \"about\": " +
		q("Shared test cases for Check My Circuit (docs/CHECK-SEQUENTIAL.md), made by mac/Tools/make_check_cases.cpp: "
		  "each circuit as the Mac app saves it, the answer key typed into the Check box, and what the check gives. "
		  "Checked against hand-worked answers and the real engine.") + ",\n  \"detect\": [";
	const auto dcs = detectCases();
	for (size_t i = 0; i < dcs.size(); i++) {
		bool opts = false;
		const int k = detect(dcs[i].text, &opts);
		if (k != dcs[i].kind || opts != dcs[i].options) {
			fprintf(stderr, "DETECT %s: got %s%s, wanted %s%s\n", q(dcs[i].text).c_str(), kindName(k), opts ? " (options)" : "",
			        kindName(dcs[i].kind), dcs[i].options ? " (options)" : "");
			bad++;
		}
		json += std::string(i ? "," : "") + "\n    {\"text\": " + q(dcs[i].text) + ", \"kind\": " + q(kindName(dcs[i].kind)) +
		        ", \"options\": " + (dcs[i].options ? "true" : "false") + "}";
	}
	json += "\n  ],\n  \"cases\": [";
	std::map<std::string, std::string> circuits;   // name -> text

	const auto all = cases();
	std::set<std::string> ids;
	for (size_t ci = 0; ci < all.size(); ci++) {
		const Case& c = all[ci];
		if (!ids.insert(c.id).second) { fprintf(stderr, "two cases called %s\n", c.id.c_str()); return 1; }
		const std::string cdl = build(c.circuit, false);
		auto known = circuits.find(c.circuit.name);
		if (known != circuits.end() && known->second != cdl) { fprintf(stderr, "two circuits called %s\n", c.circuit.name.c_str()); return 1; }
		circuits[c.circuit.name] = cdl;
		bool opts = false;
		const int kind = detect(c.key, &opts);
		Result r;
		const bool sequential = !((kind == KFormula || kind == KTable) && !opts);
		if (sequential) {
			Sim sim;
			sim.twin = build(c.circuit, true);
			cl_set_settle_on_open(false);
			char err[256];
			CLDocument* real = cl_document_open_text(cdl.data(), (long)cdl.size(), err, sizeof err);
			if (!real) { fprintf(stderr, "%s: %s\n", c.id.c_str(), err); return 1; }
			Checker ch(real, sim);
			r = ch.run(c.key, 0, c.names);
			cl_document_close(real);
		} else {
			r = oldCheck(cdl, kind, c.key, c.parsed, c.names);
		}
		// The engine's answer against the hand-worked one.
		std::vector<std::string> why;
		if (r.verdict != c.want.verdict) why.push_back("verdict " + std::to_string(r.verdict));
		if (r.error != c.want.error) why.push_back("error " + r.error);
		if (c.want.at >= 0) {
			if (r.firstWrong < 0) why.push_back("no wrong step");
			else {
				const Step& w = r.steps[r.firstWrong];
				const int at = kind == KStates ? w.row : w.pulse;
				if (at != c.want.at) why.push_back("first wrong at " + std::to_string(at));
				if (!c.want.exp.empty() && w.exp != c.want.exp) why.push_back("expected " + w.exp);
				if (!c.want.got.empty() && w.got != c.want.got) why.push_back("got " + w.got);
			}
		}
		if (c.want.steps >= 0 && (int)r.steps.size() != c.want.steps) why.push_back(std::to_string(r.steps.size()) + " steps");
		printf("%-36s %-7s v%d %-17s %s\n", c.id.c_str(), kindName(r.kind), r.verdict, r.error.c_str(), r.summary.c_str());
		for (auto& n : r.notes) printf("%40s [%d] %s\n", "", n.first, n.second.c_str());
		if (!why.empty()) {
			fprintf(stderr, "MISMATCH %s: %s\n", c.id.c_str(), joined(why, "; ").c_str());
			for (auto& s : r.steps) fprintf(stderr, "    %s p%d r%d st=%s in=%s exp=%s got=%s%s\n", s.kind.c_str(), s.pulse, s.row, s.state.c_str(),
			                                s.in.c_str(), s.exp.c_str(), s.got.c_str(), s.wrong ? " WRONG" : "");
			bad++;
		}
		json += std::string(ci ? "," : "") + "\n    {\n    \"id\": " + q(c.id) + ",\n    \"title\": " + q(c.title) + ",\n    \"page\": 0,\n    \"key\": " + q(c.key);
		if (!c.names.empty()) {
			json += ",\n    \"names\": {";
			bool first = true;
			for (auto& n : c.names) { json += std::string(first ? "" : ", ") + q(n.first) + ": " + q(n.second); first = false; }
			json += "}";
		}
		if (!c.parsed.empty()) json += ",\n    \"parsed\": " + q(c.parsed);
		json += ",\n    \"circuit\": " + q(c.circuit.name) + ",\n    \"expect\": " + resultJson(r, sequential) + "\n    }";
	}
	json += "\n  ],\n  \"circuits\": {";
	bool firstCircuit = true;
	for (auto& c : circuits) { json += std::string(firstCircuit ? "" : ",") + "\n    " + q(c.first) + ": " + q(c.second); firstCircuit = false; }
	json += "\n  }\n}\n";
	if (bad) { fprintf(stderr, "%d disagreements: cases.json not written\n", bad); return 1; }
	FILE* f = fopen(argv[2], "w");
	if (!f) { fprintf(stderr, "can't write %s\n", argv[2]); return 1; }
	fputs(json.c_str(), f);
	fclose(f);
	printf("%zu cases on %zu circuits, and %zu detections, written to %s\n", all.size(), circuits.size(), dcs.size(), argv[2]);
	return 0;
}
