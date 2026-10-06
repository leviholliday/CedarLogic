// Check my circuit behind the C interface, shared by Check.cpp (today's
// truth table checks, and reading every kind of answer key) and
// CheckClocked.cpp (counts, state tables and timing tables, run clock pulse by
// clock pulse). Not part of the interface the apps see. The rules are
// docs/CHECK-SEQUENTIAL.md's.

#ifndef CL_MAC_CHECKIMPL_H
#define CL_MAC_CHECKIMPL_H

#include "CedarCore.h"

#include <cctype>
#include <map>
#include <string>
#include <utility>
#include <vector>

struct CLCheck {
	int kind = CL_KEY_EMPTY;
	int verdict = 2;
	std::string error;      // "" or a clocked check's error code
	std::string summary;
	std::vector<std::pair<int, std::string>> notes;
	struct Out { std::string name; int column = -1; int wrong = 0; std::vector<char> expected, result; };
	std::vector<Out> outs;
	std::vector<bool> rowWrong;
	// A clocked check's column is a port.
	struct Name { std::string name; bool input = true; int column = -1; bool byHand = false; };
	std::vector<Name> names;
	// A clocked check: the page's switches and lights, the step strip's
	// columns, and its steps.
	struct Port { std::string name; bool input = true; };
	std::vector<Port> ports;
	struct Signal { std::string name; int role = CL_SIGNAL_OUTPUT; int port = -1; };
	std::vector<Signal> signals;
	struct Step {
		int kind = CL_STEP_PULSE;
		int pulse = 0, row = 0;
		std::string state, in, exp, got;
		bool wrong = false;
	};
	std::vector<Step> steps;
	int firstWrong = -1;
};

namespace clcheck {

enum { Info = 0, Warning = 1, Problem = 2 };

inline std::vector<std::string> split(const std::string& s, char sep) {
	std::vector<std::string> out;
	std::string cur;
	for (char c : s) {
		if (c == sep) { out.push_back(cur); cur.clear(); }
		else if (c != '\r') cur += c;
	}
	out.push_back(cur);
	return out;
}

inline std::string trim(const std::string& s) {
	size_t a = 0, b = s.size();
	while (a < b && std::isspace((unsigned char)s[a])) a++;
	while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
	return s.substr(a, b - a);
}

// Names match whatever their case or spacing: "Carry In" is "carry_in".
inline std::string key(const std::string& s) {
	std::string k;
	for (unsigned char c : s)
		if (std::isalnum(c) || c >= 0x80) k += (char)std::tolower(c);
	return k;
}

inline std::string lower(std::string s) {
	for (char& c : s) c = (char)std::tolower((unsigned char)c);
	return s;
}

inline std::string plural(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

// "a, b and c".
inline std::string list(const std::vector<std::string>& xs) {
	std::string s;
	for (size_t i = 0; i < xs.size(); i++) s += (i == 0 ? "" : i + 1 == xs.size() ? " and " : ", ") + xs[i];
	return s;
}

inline std::string joined(const std::vector<std::string>& xs, const char* sep) {
	std::string s;
	for (size_t i = 0; i < xs.size(); i++) s += (i ? sep : "") + xs[i];
	return s;
}

inline bool isValue(char ch) { return ch == '0' || ch == '1' || ch == 'x' || ch == 'X' || ch == '-' || ch == 'd' || ch == 'D'; }
inline bool isDontCare(char ch) { return ch == 'x' || ch == 'X' || ch == '-' || ch == 'd' || ch == 'D'; }
inline bool allDigits(const std::string& s) {
	if (s.empty()) return false;
	for (char c : s) if (c < '0' || c > '9') return false;
	return true;
}
inline bool binary(const std::string& s) { return !s.empty() && s.find_first_not_of("01") == std::string::npos; }

// Split on spaces, tabs, commas and semicolons; a | is a token of its own
// (two in a row count once).
inline std::vector<std::string> tokens(const std::string& line) {
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
inline std::vector<std::string> words(const std::string& s) {
	std::vector<std::string> out;
	std::string cur;
	for (char ch : s) {
		if (std::isspace((unsigned char)ch) || ch == ',') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
		else cur += ch;
	}
	if (!cur.empty()) out.push_back(cur);
	return out;
}

// "Cin<TAB>C" lines: an asked-for name (by its key) and the name it is.
inline std::map<std::string, std::string> readNames(const char* names) {
	std::map<std::string, std::string> m;
	if (!names) return m;
	for (const std::string& line : split(names, '\n')) {
		auto f = split(line, '\t');
		if (f.size() >= 2 && !key(f[0]).empty() && !trim(f[1]).empty()) m[key(f[0])] = trim(f[1]);
	}
	return m;
}

// A start:, reset: or set: line: names and their values.
struct Assign { std::vector<std::string> names; std::string bits; };

// An answer key, read (§2, §3).
struct Key {
	int kind = CL_KEY_EMPTY;
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

// Which kind of key a text is (Check.cpp).
int detectKey(const std::string& text, bool* options);
// The whole key, or the error that stops it (Check.cpp).
Key readKey(const std::string& text);

}  // namespace clcheck

#endif  // CL_MAC_CHECKIMPL_H
