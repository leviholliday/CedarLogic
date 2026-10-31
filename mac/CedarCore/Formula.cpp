// Boolean algebra and formula -> circuit (see Formula.h).

#include "Formula.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <functional>
#include <limits>
#include <set>
#include <utility>

namespace formula {

namespace {

int bits(int x) {
	int n = 0;
	for (unsigned u = (unsigned)x; u; u &= u - 1) n++;
	return n;
}

std::string fmt(const char* f, ...) {
	char buf[512];
	va_list ap;
	va_start(ap, f);
	vsnprintf(buf, sizeof buf, f, ap);
	va_end(ap);
	return buf;
}

}  // namespace

int Implicant::literals(int n) const { return n - bits(mask & ((1 << n) - 1)); }

// ---- Minimizing ----------------------------------------------------------------------

namespace {

// Every prime implicant of the ones and don't-cares.
std::vector<Implicant> primes(const std::vector<int>& terms) {
	std::set<Implicant> current, all;
	for (int t : terms) current.insert(Implicant{ t, 0 });
	while (!current.empty()) {
		std::set<Implicant> next, used;
		std::map<int, std::vector<Implicant>> byMask;
		for (const Implicant& i : current) byMask[i.mask].push_back(i);
		for (auto& kv : byMask) {
			const std::vector<Implicant>& group = kv.second;
			for (size_t a = 0; a + 1 < group.size(); a++)
				for (size_t b = a + 1; b < group.size(); b++) {
					const int d = group[a].value ^ group[b].value;
					if (bits(d) != 1) continue;
					next.insert(Implicant{ group[a].value & ~d, group[a].mask | d });
					used.insert(group[a]);
					used.insert(group[b]);
				}
		}
		for (const Implicant& i : current) if (!used.count(i)) all.insert(i);
		current = next;
	}
	// Always the same order, so equally good answers don't swap about.
	std::vector<Implicant> out(all.begin(), all.end());
	std::sort(out.begin(), out.end(), [](const Implicant& a, const Implicant& b) {
		const int da = bits(a.mask), db = bits(b.mask);
		if (da != db) return da > db;   // fewest letters first
		return a.value != b.value ? a.value < b.value : a.mask < b.mask;
	});
	return out;
}

// Terms in reading order: A before A', and before terms without A.
std::vector<Implicant> readingOrder(std::vector<Implicant> imps, int n) {
	auto key = [n](const Implicant& p) {
		std::vector<int> k;
		for (int i = 0; i < n; i++) {
			const int bit = 1 << (n - 1 - i);
			k.push_back((p.mask & bit) ? 2 : ((p.value & bit) ? 0 : 1));
		}
		return k;
	};
	std::sort(imps.begin(), imps.end(), [&](const Implicant& a, const Implicant& b) { return key(a) < key(b); });
	return imps;
}

}  // namespace

// The fewest prime implicants that cover every one (then the fewest letters):
// the essential ones first, then a search, which gives up and keeps its best
// so far on a very large table.
std::vector<Implicant> minimize(int n, const std::vector<int>& ones, const std::vector<int>& dontCares) {
	if (ones.empty()) return {};
	std::vector<int> terms = ones;
	terms.insert(terms.end(), dontCares.begin(), dontCares.end());
	const std::vector<Implicant> all = primes(terms);
	std::vector<Implicant> chosen;
	std::set<int> remaining(ones.begin(), ones.end());
	std::vector<Implicant> candidates;
	for (const Implicant& p : all)
		for (int m : ones) if (p.covers(m)) { candidates.push_back(p); break; }

	// Essential primes: the only one covering some one.
	for (bool changed = true; changed;) {
		changed = false;
		for (int m : remaining) {
			const Implicant* only = nullptr;
			int count = 0;
			for (const Implicant& p : candidates) if (p.covers(m)) { only = &p; count++; }
			if (count == 1) {
				const Implicant p = *only;
				chosen.push_back(p);
				for (auto it = remaining.begin(); it != remaining.end();) it = p.covers(*it) ? remaining.erase(it) : std::next(it);
				candidates.erase(std::remove(candidates.begin(), candidates.end(), p), candidates.end());
				changed = true;
				break;
			}
		}
	}
	{
		std::vector<Implicant> keep;
		for (const Implicant& p : candidates)
			for (int m : remaining) if (p.covers(m)) { keep.push_back(p); break; }
		candidates = keep;
	}
	if (remaining.empty()) return readingOrder(chosen, n);

	auto cost = [n](const std::vector<Implicant>& s) {
		int lits = 0;
		for (const Implicant& p : s) lits += p.literals(n);
		return std::make_pair((int)s.size(), lits);
	};
	auto countIn = [](const Implicant& p, const std::set<int>& left) {
		int c = 0;
		for (int m : left) if (p.covers(m)) c++;
		return c;
	};

	// A first answer, greedily: most still-needed ones covered, fewest letters.
	std::vector<Implicant> best;
	{
		std::set<int> left = remaining;
		while (!left.empty()) {
			const Implicant* pick = nullptr;
			for (const Implicant& p : candidates) {
				if (pick == nullptr) { pick = &p; continue; }
				const int ca = countIn(*pick, left), cb = countIn(p, left);
				if (cb > ca || (cb == ca && p.literals(n) < pick->literals(n))) pick = &p;
			}
			if (pick == nullptr || countIn(*pick, left) == 0) break;
			best.push_back(*pick);
			const Implicant p = *pick;
			for (auto it = left.begin(); it != left.end();) it = p.covers(*it) ? left.erase(it) : std::next(it);
		}
	}
	int nodes = 0;
	std::function<void(const std::set<int>&, std::vector<Implicant>&)> search = [&](const std::set<int>& left, std::vector<Implicant>& picked) {
		if (++nodes > 40000) return;
		if (left.empty()) {
			if (cost(picked) < cost(best)) best = picked;
			return;
		}
		if (picked.size() + 1 > best.size()) return;
		// Branch on the one with the fewest ways to cover it.
		int m = -1, fewest = std::numeric_limits<int>::max();
		for (int x : left) {
			int ways = 0;
			for (const Implicant& p : candidates) if (p.covers(x)) ways++;
			if (ways < fewest) { fewest = ways; m = x; }
		}
		for (const Implicant& p : candidates) {
			if (!p.covers(m)) continue;
			std::set<int> rest;
			for (int x : left) if (!p.covers(x)) rest.insert(x);
			picked.push_back(p);
			search(rest, picked);
			picked.pop_back();
		}
	};
	std::vector<Implicant> picked;
	search(remaining, picked);
	chosen.insert(chosen.end(), best.begin(), best.end());
	return readingOrder(chosen, n);
}

TwoLevel simplest(bool sop, int n, const std::vector<int>& values) {
	std::vector<int> ones, zeros, dc;
	for (int i = 0; i < (int)values.size(); i++) (values[i] == 1 ? ones : values[i] == 0 ? zeros : dc).push_back(i);
	TwoLevel t;
	t.sumOfProducts = sop;
	if (zeros.empty()) { t.constant = (!ones.empty() || !dc.empty()) ? 1 : 0; return t; }
	if (ones.empty()) { t.constant = 0; return t; }
	// A product of sums is the simplest sum of products of the zeros, inside out.
	const std::vector<Implicant> imps = minimize(n, sop ? ones : zeros, dc);
	t.implicants = imps;
	for (const Implicant& p : imps) {
		std::vector<Literal> term;
		for (int i = 0; i < n; i++) {
			const int bit = 1 << (n - 1 - i);
			if (p.mask & bit) continue;
			const bool set = (p.value & bit) != 0;
			term.push_back(Literal{ i, sop ? !set : set });
		}
		t.terms.push_back(term);
	}
	return t;
}

std::string TwoLevel::text(const std::vector<std::string>& names) const {
	if (constant >= 0) return constant ? "1" : "0";
	bool tight = true;
	for (const std::string& s : names) tight = tight && s.size() == 1;
	std::string out;
	auto lit = [&](const Literal& l) { return names[l.variable] + (l.negated ? "'" : ""); };
	if (sumOfProducts) {
		for (size_t t = 0; t < terms.size(); t++) {
			if (t > 0) out += " + ";
			for (size_t i = 0; i < terms[t].size(); i++) {
				if (i > 0 && !tight) out += "\xC2\xB7";
				out += lit(terms[t][i]);
			}
		}
	} else {
		const bool bare = terms.size() == 1;
		for (const std::vector<Literal>& clause : terms) {
			const bool paren = !bare && clause.size() > 1;
			if (paren) out += "(";
			for (size_t i = 0; i < clause.size(); i++) {
				if (i > 0) out += " + ";
				out += lit(clause[i]);
			}
			if (paren) out += ")";
		}
	}
	return out;
}

// ---- Karnaugh maps ----------------------------------------------------------------

bool KMapLayout::make(int n, KMapLayout& out) {
	if (n < 2 || n > 4) return false;
	out.n = n;
	out.rowVars = n / 2;
	out.colVars = n - out.rowVars;
	auto gray = [](int count) { return count == 1 ? std::vector<int>{ 0, 1 } : std::vector<int>{ 0, 1, 3, 2 }; };
	out.rowCodes = gray(out.rowVars);
	out.colCodes = gray(out.colVars);
	return true;
}

namespace {
std::vector<std::pair<int, int>> split(const std::vector<int>& idx) {
	std::vector<std::pair<int, int>> out;
	if (idx.empty()) return out;
	int start = idx[0], prev = idx[0];
	for (size_t k = 1; k < idx.size(); k++) {
		if (idx[k] == prev + 1) { prev = idx[k]; continue; }
		out.push_back({ start, prev });
		start = prev = idx[k];
	}
	out.push_back({ start, prev });
	return out;
}
}  // namespace

void KMapLayout::runs(const Implicant& p, std::vector<std::pair<int, int>>& rows, std::vector<std::pair<int, int>>& cols) const {
	const int colMask = (1 << colVars) - 1;
	std::vector<int> r, c;
	for (int i = 0; i < (int)rowCodes.size(); i++)
		if ((rowCodes[i] & ~(p.mask >> colVars)) == (p.value >> colVars)) r.push_back(i);
	for (int i = 0; i < (int)colCodes.size(); i++)
		if ((colCodes[i] & ~(p.mask & colMask)) == (p.value & colMask)) c.push_back(i);
	rows = split(r);
	cols = split(c);
}

// ---- Reading formulas -----------------------------------------------------------------

bool Expr::eval(int m, int n) const {
	switch (kind) {
	case Var: return ((m >> (n - 1 - var)) & 1) == 1;
	case Const: return value;
	case Not: return !kids[0].eval(m, n);
	case And: for (const Expr& e : kids) if (!e.eval(m, n)) return false; return true;
	case Or: for (const Expr& e : kids) if (e.eval(m, n)) return true; return false;
	case Xor: { bool x = false; for (const Expr& e : kids) x = x != e.eval(m, n); return x; }
	}
	return false;
}

namespace {

typedef std::u32string Text;

Text decode(const std::string& s) {
	Text out;
	for (size_t i = 0; i < s.size();) {
		unsigned char c = (unsigned char)s[i];
		char32_t cp;
		int extra;
		if (c < 0x80) { cp = c; extra = 0; }
		else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
		else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
		else { cp = c & 0x07; extra = 3; }
		i++;
		for (int k = 0; k < extra && i < s.size(); k++, i++) cp = (cp << 6) | ((unsigned char)s[i] & 0x3F);
		out.push_back(cp);
	}
	return out;
}

std::string encode(const Text& t) {
	std::string out;
	for (char32_t c : t) {
		if (c < 0x80) out += (char)c;
		else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
		else if (c < 0x10000) { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
		else { out += (char)(0xF0 | (c >> 18)); out += (char)(0x80 | ((c >> 12) & 0x3F)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
	}
	return out;
}

bool isLetter(char32_t c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
bool isUpper(char32_t c) { return c >= 'A' && c <= 'Z'; }
bool isLower(char32_t c) { return c >= 'a' && c <= 'z'; }
bool isDigit(char32_t c) { return c >= '0' && c <= '9'; }
bool isSpace(char32_t c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == 0xA0; }

Text trim(const Text& t) {
	size_t a = 0, b = t.size();
	while (a < b && isSpace(t[a])) a++;
	while (b > a && isSpace(t[b - 1])) b--;
	return t.substr(a, b - a);
}

std::string upper(std::string s) {
	for (char& c : s) c = (char)toupper((unsigned char)c);
	return s;
}

// Natural order: X2 before X10, case aside.
bool naturalLess(const std::string& a, const std::string& b) {
	size_t i = 0, j = 0;
	while (i < a.size() && j < b.size()) {
		if (isdigit((unsigned char)a[i]) && isdigit((unsigned char)b[j])) {
			size_t ie = i, je = j;
			while (ie < a.size() && isdigit((unsigned char)a[ie])) ie++;
			while (je < b.size() && isdigit((unsigned char)b[je])) je++;
			const long x = strtol(a.substr(i, ie - i).c_str(), nullptr, 10), y = strtol(b.substr(j, je - j).c_str(), nullptr, 10);
			if (x != y) return x < y;
			i = ie;
			j = je;
			continue;
		}
		const int ca = tolower((unsigned char)a[i]), cb = tolower((unsigned char)b[j]);
		if (ca != cb) return ca < cb;
		i++;
		j++;
	}
	return a.size() - i < b.size() - j;
}

// "Σm(1,3,5)", "sum m(1,3) + d(7)", "m(1,2)", "ΠM(0,2)", "M(0,4)".
struct MintermList { std::vector<int> ones, dc; bool maxterms = false; };

bool mintermList(const Text& source, MintermList& out) {
	Text t;
	for (char32_t c : source) if (c != ' ') t.push_back(c);
	size_t i = 0;
	bool product = false;
	const std::string s = encode(t);
	const char* prefixes[] = { "\xCE\xA3", "\xE2\x88\x91", "sum", "SUM", "Sum", "\xCE\xA0", "\xE2\x88\x8F", "prod", "PROD", "Prod" };
	std::string rest = s;
	for (int k = 0; k < 10; k++) {
		const std::string p = prefixes[k];
		if (rest.compare(0, p.size(), p) == 0) { rest = rest.substr(p.size()); product = k >= 5; break; }
	}
	(void)i;
	if (rest.empty() || (rest[0] != 'm' && rest[0] != 'M') || rest.size() < 3 || rest[1] != '(') return false;
	const bool capital = rest[0] == 'M';
	auto numbers = [](const std::string& list, std::vector<int>& into) {
		size_t at = 0;
		while (at < list.size()) {
			const size_t comma = list.find(',', at);
			const std::string n = list.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
			if (!n.empty()) into.push_back(atoi(n.c_str()));
			if (comma == std::string::npos) break;
			at = comma + 1;
		}
	};
	auto onlyDigits = [](const std::string& x) {
		for (char c : x) if (!isdigit((unsigned char)c) && c != ',') return false;
		return true;
	};
	const size_t close = rest.find(')');
	if (close == std::string::npos) return false;
	const std::string inside = rest.substr(2, close - 2);
	if (!onlyDigits(inside)) return false;
	std::string after = rest.substr(close + 1);
	MintermList m;
	numbers(inside, m.ones);
	if (!after.empty()) {
		if (after.compare(0, 3, "+d(") != 0 || after.back() != ')') return false;
		const std::string d = after.substr(3, after.size() - 4);
		if (!onlyDigits(d)) return false;
		numbers(d, m.dc);
	}
	m.maxterms = capital || product;
	out = m;
	return true;
}

// A recursive-descent reader: OR, then XOR, then AND (written or side by
// side), then NOT and the prime.
struct Parser {
	Text text;
	size_t i = 0;
	std::vector<std::string> variables;
	bool fixed = false;
	std::string error;

	enum Kind { Name, Zero, One, LParen, RParen, OrT, XorT, AndT, NotT, Prime, End };
	struct Token { Kind kind; std::string name; };

	std::string rest() const { return encode(text.substr(std::min(i, text.size()), 12)); }

	// The next token and where it ends, without taking it.
	std::pair<Token, size_t> scan() const {
		size_t j = i;
		while (j < text.size() && isSpace(text[j])) j++;
		if (j >= text.size()) return { { End, "" }, j };
		const char32_t c = text[j];
		switch (c) {
		case '(': case '[': return { { LParen, "" }, j + 1 };
		case ')': case ']': return { { RParen, "" }, j + 1 };
		case '+': case '|': case 0x2228: return { { OrT, "" }, j + 1 };
		case 0x2295: case '^': return { { XorT, "" }, j + 1 };
		case '*': case 0xB7: case '&': case 0x2227: case 0x2022: case '.': case 0x22C5: return { { AndT, "" }, j + 1 };
		case '~': case '!': case 0xAC: case '-': return { { NotT, "" }, j + 1 };
		case '\'': case 0x2019: case '`': return { { Prime, "" }, j + 1 };
		case '0': return { { Zero, "" }, j + 1 };
		case '1': return { { One, "" }, j + 1 };
		default: break;
		}
		if (!isLetter(c)) return { { Name, encode(Text(1, c)) }, j + 1 };
		// A word: an operator if it's one, else a name. Capitals start a new
		// name (AB is A and B), small letters and digits continue it (Cin, X1, sel).
		size_t k = j;
		while (k < text.size() && (isLetter(text[k]) || isDigit(text[k]) || text[k] == '_')) k++;
		const std::string word = upper(encode(text.substr(j, k - j)));
		if (word == "OR") return { { OrT, "" }, k };
		if (word == "XOR") return { { XorT, "" }, k };
		if (word == "AND") return { { AndT, "" }, k };
		if (word == "NOT") return { { NotT, "" }, k };
		size_t e = j + 1;
		while (e < k && (!isUpper(text[e]) || isLower(text[j]))) e++;
		return { { Name, encode(text.substr(j, e - j)) }, e };
	}
	Kind peek() const { return scan().first.kind; }
	Token take() { auto r = scan(); i = r.second; return r.first; }
	bool fail(const std::string& message) { if (error.empty()) error = message; return false; }

	bool parseAll(Expr& out) {
		if (!parseOr(out)) return false;
		const Kind t = peek();
		if (t != End) return fail(t == RParen ? "There's a ) without a (." : "Something's off near \"" + rest() + "\".");
		return true;
	}
	bool parseOr(Expr& out) {
		std::vector<Expr> parts(1);
		if (!parseXor(parts[0])) return false;
		while (peek() == OrT) { take(); parts.emplace_back(); if (!parseXor(parts.back())) return false; }
		if (parts.size() == 1) out = parts[0];
		else { out = Expr(); out.kind = Expr::Or; out.kids = parts; }
		return true;
	}
	bool parseXor(Expr& out) {
		std::vector<Expr> parts(1);
		if (!parseAnd(parts[0])) return false;
		while (peek() == XorT) { take(); parts.emplace_back(); if (!parseAnd(parts.back())) return false; }
		if (parts.size() == 1) out = parts[0];
		else { out = Expr(); out.kind = Expr::Xor; out.kids = parts; }
		return true;
	}
	bool parseAnd(Expr& out) {
		std::vector<Expr> parts(1);
		if (!parseUnary(parts[0])) return false;
		for (;;) {
			const Kind t = peek();
			if (t == AndT) { take(); parts.emplace_back(); if (!parseUnary(parts.back())) return false; continue; }
			// Side by side: AB, A(B + C), (A + B)(C + D).
			if (t == Name || t == Zero || t == One || t == LParen || t == NotT) {
				parts.emplace_back();
				if (!parseUnary(parts.back())) return false;
				continue;
			}
			break;
		}
		if (parts.size() == 1) out = parts[0];
		else { out = Expr(); out.kind = Expr::And; out.kids = parts; }
		return true;
	}
	bool parseUnary(Expr& out) {
		if (peek() == NotT) {
			take();
			Expr inner;
			if (!parseUnary(inner)) return false;
			out = Expr();
			out.kind = Expr::Not;
			out.kids = { inner };
			return true;
		}
		if (!parsePrimary(out)) return false;
		while (peek() == Prime) {
			take();
			Expr n;
			n.kind = Expr::Not;
			n.kids = { out };
			out = n;
		}
		return true;
	}
	bool parsePrimary(Expr& out) {
		const Token t = take();
		out = Expr();
		switch (t.kind) {
		case Zero: out.kind = Expr::Const; out.value = false; return true;
		case One: out.kind = Expr::Const; out.value = true; return true;
		case LParen:
			if (!parseOr(out)) return false;
			if (take().kind != RParen) return fail("A ( isn't closed with a ).");
			return true;
		case Name: {
			if (t.name.empty() || !isLetter((unsigned char)t.name[0])) return fail("\"" + t.name + "\" isn't something a formula can have.");
			out.kind = Expr::Var;
			for (size_t k = 0; k < variables.size(); k++) if (variables[k] == t.name) { out.var = (int)k; return true; }
			if (fixed) return fail(t.name + " isn't one of the variables listed in the ( ).");
			variables.push_back(t.name);
			out.var = (int)variables.size() - 1;
			return true;
		}
		case End: return fail("The formula stops too soon; something's missing at the end.");
		case RParen: return fail("There's a ) where something should be.");
		default: return fail("Something's missing before \"" + rest() + "\".");
		}
	}
};

void renumber(Expr& e, const std::vector<int>& map) {
	if (e.kind == Expr::Var) e.var = map[e.var];
	for (Expr& k : e.kids) renumber(k, map);
}

}  // namespace

bool parse(const std::string& source, Parsed& out, std::string& error) {
	// Lines (or ; between them).
	std::vector<Text> lines;
	{
		Text all = decode(source), cur;
		for (char32_t c : all) {
			if (c == ';' || c == '\n') { if (!trim(cur).empty()) lines.push_back(trim(cur)); cur.clear(); }
			else cur.push_back(c);
		}
		if (!trim(cur).empty()) lines.push_back(trim(cur));
	}
	if (lines.empty()) { error = "Type a formula, like F = A'B + AC."; return false; }

	struct Line { std::string name; bool hasDeclared = false; std::vector<std::string> declared; Text rhs; };
	std::vector<Line> parsed;
	const char* defaultNames[] = { "F", "G", "H", "K", "P", "Q" };
	for (size_t i = 0; i < lines.size(); i++) {
		Line l;
		l.name = i < 6 ? defaultNames[i] : fmt("F%d", (int)i + 1);
		l.rhs = lines[i];
		const size_t eq = lines[i].find('=');
		if (eq != Text::npos) {
			Text lhs = trim(lines[i].substr(0, eq));
			l.rhs = trim(lines[i].substr(eq + 1));
			const size_t open = lhs.find('(');
			if (open != Text::npos && !lhs.empty() && lhs.back() == ')') {
				const Text inside = lhs.substr(open + 1, lhs.size() - open - 2);
				l.hasDeclared = true;
				Text cur;
				for (char32_t c : inside) {
					if (c == ',') { if (!trim(cur).empty()) l.declared.push_back(encode(trim(cur))); cur.clear(); }
					else cur.push_back(c);
				}
				if (!trim(cur).empty()) l.declared.push_back(encode(trim(cur)));
				lhs = trim(lhs.substr(0, open));
			}
			bool ok = !lhs.empty();
			for (char32_t c : lhs) ok = ok && (isLetter(c) || isDigit(c) || c == '_');
			if (!ok) { error = "\"" + encode(lhs) + "\" can't be an output's name. Use letters and digits, like F or Y1."; return false; }
			l.name = encode(lhs);
		}
		if (l.rhs.empty()) { error = l.name + " = what? There's nothing after the =."; return false; }
		parsed.push_back(l);
	}

	// The variables: a declared list wins; otherwise every name used, in order.
	std::vector<std::string> variables;
	bool anyDeclared = false;
	for (const Line& l : parsed) if (l.hasDeclared) { anyDeclared = true; variables = l.declared; break; }
	std::vector<Expr> exprs(parsed.size());
	std::vector<bool> isExpr(parsed.size(), false);
	std::vector<MintermList> lists(parsed.size());
	for (size_t i = 0; i < parsed.size(); i++) {
		const Line& l = parsed[i];
		if (mintermList(l.rhs, lists[i])) {
			if (!l.hasDeclared && variables.empty()) {
				error = "For a list of minterms, name the variables too: " + l.name + "(A,B,C) = " + encode(l.rhs);
				return false;
			}
		} else {
			Parser p;
			p.text = l.rhs;
			p.variables = variables;
			p.fixed = anyDeclared;
			if (!p.parseAll(exprs[i])) { error = p.error; return false; }
			variables = p.variables;
			isExpr[i] = true;
		}
	}
	if (!anyDeclared) {
		// Undeclared: alphabetical, so "C + AB" still has A first.
		std::vector<std::string> order = variables;
		std::sort(order.begin(), order.end(), naturalLess);
		std::vector<int> map(variables.size());
		for (size_t k = 0; k < variables.size(); k++)
			map[k] = (int)(std::find(order.begin(), order.end(), variables[k]) - order.begin());
		for (size_t i = 0; i < parsed.size(); i++) if (isExpr[i]) renumber(exprs[i], map);
		variables = order;
	}
	if (variables.empty() && std::find(isExpr.begin(), isExpr.end(), true) == isExpr.end()) {
		error = "There are no variables in that.";
		return false;
	}
	if ((int)variables.size() > kMaxVariables) {
		error = fmt("That's %d variables. Up to %d can be built at once.", (int)variables.size(), kMaxVariables);
		return false;
	}
	const int n = (int)variables.size(), size = 1 << n;
	out = Parsed();
	out.variables = variables;
	std::set<std::string> names;
	for (size_t i = 0; i < parsed.size(); i++) {
		Function f;
		f.name = parsed[i].name;
		if (isExpr[i]) {
			f.hasExpr = true;
			f.expr = exprs[i];
			for (int m = 0; m < size; m++) f.values.push_back(exprs[i].eval(m, n) ? 1 : 0);
		} else {
			const MintermList& list = lists[i];
			for (int m : list.ones) if (m >= size) { error = fmt("%d is too big for %d variables (the last minterm is %d).", m, n, size - 1); return false; }
			for (int m : list.dc) if (m >= size) { error = fmt("%d is too big for %d variables (the last minterm is %d).", m, n, size - 1); return false; }
			f.values.assign((size_t)size, list.maxterms ? 1 : 0);
			for (int m : list.ones) f.values[m] = list.maxterms ? 0 : 1;
			for (int m : list.dc) f.values[m] = -1;
		}
		if (!names.insert(f.name).second) { error = "Two lines have the same name. Give each output its own, like F and G."; return false; }
		if (std::find(variables.begin(), variables.end(), f.name) != variables.end()) {
			error = f.name + " is both an output and a variable.";
			return false;
		}
		out.functions.push_back(f);
	}
	return true;
}

// ---- Formula -> circuit -------------------------------------------------------------

namespace {

// The gates a formula needs, in the style asked for: each gate is made once
// (the same inputs give the same wire), and NOT of a NOT is the original.
class Synth {
public:
	enum Kind { AND_, OR_, NAND_, NOR_, XOR_, XNOR_, NOT_ };
	struct Gate {
		Kind kind;
		std::vector<int> inputs;
		std::string libraryName() const {
			const int k = (int)inputs.size();
			switch (kind) {
			case AND_: return fmt("AA_AND%d", k);
			case OR_: return fmt("AE_OR%d", k);
			case NAND_: return fmt("BA_NAND%d", k);
			case NOR_: return fmt("BE_NOR%d", k);
			case XOR_: return "AI_XOR2";
			case XNOR_: return "AO_XNOR2";
			default: return "AA_INVERTER";
			}
		}
	};
	struct Operand { bool isConstant; bool value; int net; };
	static std::string caption(Kind k) {
		static const char* names[] = { "AND", "OR", "NAND", "NOR", "XOR", "XNOR", "NOT" };
		return names[k];
	}
	static std::string outputPin(const Gate* g) { return g == nullptr || g->kind == NOT_ ? "OUT_0" : "OUT"; }

	int nVars;
	Style style;
	int maxIn;
	std::vector<Gate> gates;

	Synth(int n, Style s, int m) : nVars(n), style(s), maxIn(m) {}

	int NOT(int x) {
		auto it = inverse.find(x);
		if (it != inverse.end()) return it->second;
		const int y = style == AnyGates ? make(NOT_, { x }) : style == NandOnly ? make(NAND_, { x, x }) : make(NOR_, { x, x });
		inverse[x] = y;
		inverse[y] = x;
		return y;
	}
	int AND(std::vector<int> xs) {
		xs = unique(xs);
		if (xs.size() == 1) return xs[0];
		switch (style) {
		case AnyGates: return make(AND_, fanIn(xs, [this](std::vector<int> v) { return AND(v); }));
		case NandOnly: return NOT(NAND(xs));
		default: { std::vector<int> n; for (int x : xs) n.push_back(NOT(x)); return NOR(n); }
		}
	}
	int OR(std::vector<int> xs) {
		xs = unique(xs);
		if (xs.size() == 1) return xs[0];
		switch (style) {
		case AnyGates: return make(OR_, fanIn(xs, [this](std::vector<int> v) { return OR(v); }));
		case NandOnly: { std::vector<int> n; for (int x : xs) n.push_back(NOT(x)); return NAND(n); }
		default: return NOT(NOR(xs));
		}
	}
	int NAND(std::vector<int> xs) {
		xs = unique(xs);
		if (xs.size() == 1) return NOT(xs[0]);
		if (style == NorOnly) return NOT(AND(xs));
		const int net = make(NAND_, fanIn(xs, [this](std::vector<int> v) { return AND(v); }));
		if ((int)xs.size() <= maxIn) {
			auto it = made.find(key(AND_, sorted(xs)));
			if (it != made.end()) { inverse[net] = it->second; inverse[it->second] = net; }
		}
		return net;
	}
	int NOR(std::vector<int> xs) {
		xs = unique(xs);
		if (xs.size() == 1) return NOT(xs[0]);
		if (style == NandOnly) return NOT(OR(xs));
		return make(NOR_, fanIn(xs, [this](std::vector<int> v) { return OR(v); }));
	}
	int XOR(int a, int b) {
		switch (style) {
		case AnyGates: return make(XOR_, { a, b });
		case NandOnly: { const int t = NAND({ a, b }); return NAND({ NAND({ a, t }), NAND({ b, t }) }); }
		default: return NOT(XNOR(a, b));
		}
	}
	int XNOR(int a, int b) {
		switch (style) {
		case AnyGates: return make(XNOR_, { a, b });
		case NandOnly: return NOT(XOR(a, b));
		default: { const int t = NOR({ a, b }); return NOR({ NOR({ a, t }), NOR({ b, t }) }); }
		}
	}

	// The formula (or its NOT), folding away constants: A + 1 is 1.
	Operand compile(const Expr& e, bool neg) {
		switch (e.kind) {
		case Expr::Var: return { false, false, neg ? NOT(e.var) : e.var };
		case Expr::Const: return { true, e.value != neg, 0 };
		case Expr::Not: return compile(e.kids[0], !neg);
		case Expr::And:
		case Expr::Or: {
			const bool isAnd = e.kind == Expr::And;
			std::vector<int> nets;
			for (const Expr& x : e.kids) {
				const Operand o = compile(x, false);
				if (o.isConstant) {
					// AND with 0 is 0, OR with 1 is 1; the other constant drops out.
					if (o.value != isAnd) return { true, isAnd == neg, 0 };
				} else {
					nets.push_back(o.net);
				}
			}
			nets = unique(nets);
			if (nets.empty()) return { true, isAnd != neg, 0 };
			if (nets.size() == 1) return { false, false, neg ? NOT(nets[0]) : nets[0] };
			return { false, false, isAnd ? (neg ? NAND(nets) : AND(nets)) : (neg ? NOR(nets) : OR(nets)) };
		}
		case Expr::Xor: {
			bool flip = neg;
			std::vector<int> nets;
			for (const Expr& x : e.kids) {
				const Operand o = compile(x, false);
				if (o.isConstant) { if (o.value) flip = !flip; continue; }
				// A xor A is 0: a net twice cancels.
				auto it = std::find(nets.begin(), nets.end(), o.net);
				if (it != nets.end()) nets.erase(it);
				else nets.push_back(o.net);
			}
			if (nets.empty()) return { true, flip, 0 };
			if (nets.size() == 1) return { false, false, flip ? NOT(nets[0]) : nets[0] };
			int acc = nets[0];
			for (size_t k = 1; k < nets.size(); k++) {
				const bool last = k == nets.size() - 1;
				acc = last && flip ? XNOR(acc, nets[k]) : XOR(acc, nets[k]);
			}
			return { false, false, acc };
		}
		}
		return { true, false, 0 };
	}

private:
	std::map<std::string, int> made;
	std::map<int, int> inverse;

	static std::vector<int> sorted(std::vector<int> v) { std::sort(v.begin(), v.end()); return v; }
	static std::string key(Kind k, const std::vector<int>& in) {
		std::string s = fmt("%d:", (int)k);
		for (int x : in) s += fmt("%d,", x);
		return s;
	}
	int make(Kind kind, const std::vector<int>& inputs) {
		const std::vector<int> ordered = kind == NOT_ ? inputs : sorted(inputs);
		const std::string k = key(kind, ordered);
		auto it = made.find(k);
		if (it != made.end()) return it->second;
		gates.push_back(Gate{ kind, ordered });
		const int net = nVars + (int)gates.size() - 1;
		made[k] = net;
		return net;
	}
	// Too many inputs for one gate: combine them in groups first.
	std::vector<int> fanIn(std::vector<int> xs, const std::function<int(std::vector<int>)>& combine) {
		while ((int)xs.size() > maxIn) {
			std::vector<int> next;
			for (size_t at = 0; at < xs.size(); at += maxIn) {
				std::vector<int> group(xs.begin() + at, xs.begin() + std::min(xs.size(), at + maxIn));
				next.push_back(group.size() == 1 ? group[0] : combine(group));
			}
			xs = next;
		}
		return xs;
	}
	static std::vector<int> unique(const std::vector<int>& xs) {
		std::vector<int> out;
		for (int x : xs) if (std::find(out.begin(), out.end(), x) == out.end()) out.push_back(x);
		return out;
	}
};

Expr twoLevelExpr(const TwoLevel& t) {
	Expr e;
	if (t.constant >= 0) { e.kind = Expr::Const; e.value = t.constant == 1; return e; }
	auto lit = [](const Literal& l) {
		Expr v;
		v.kind = Expr::Var;
		v.var = l.variable;
		if (!l.negated) return v;
		Expr n;
		n.kind = Expr::Not;
		n.kids = { v };
		return n;
	};
	std::vector<Expr> groups;
	for (const std::vector<Literal>& term : t.terms) {
		if (term.size() == 1) { groups.push_back(lit(term[0])); continue; }
		Expr g;
		g.kind = t.sumOfProducts ? Expr::And : Expr::Or;
		for (const Literal& l : term) g.kids.push_back(lit(l));
		groups.push_back(g);
	}
	if (groups.size() == 1) return groups[0];
	e.kind = t.sumOfProducts ? Expr::Or : Expr::And;
	e.kids = groups;
	return e;
}

}  // namespace

std::string Plan::summary() const {
	if (logicGates == 0) return "No gates needed: just switches and lights.";
	std::string s = fmt("%d gate%s: ", logicGates, logicGates == 1 ? "" : "s");
	const char* order[] = { "AND", "OR", "NOT", "NAND", "NOR", "XOR", "XNOR" };
	bool first = true;
	for (const char* k : order) {
		auto it = gateCounts.find(k);
		if (it == gateCounts.end()) continue;
		if (!first) s += ", ";
		s += fmt("%d %s", it->second, k);
		first = false;
	}
	return s;
}

Plan plan(const Parsed& formulas, Shape shape, Style style, bool twoInputOnly) {
	const int n = (int)formulas.variables.size();
	Synth s(n, style, twoInputOnly ? 2 : 4);
	struct Output { std::string name; Synth::Operand op; };
	std::vector<Output> outputs;
	for (const Function& f : formulas.functions) {
		Expr e;
		if (shape == AsWritten && f.hasExpr) e = f.expr;
		else e = twoLevelExpr(simplest(shape != ProductOfSums, n, f.values));
		outputs.push_back({ f.name, s.compile(e, false) });
	}

	Plan out;
	const double column = 16, gap = 2;
	auto height = [](size_t inputs) { return inputs <= 1 ? 3.0 : inputs * 2.0 + 1; };
	const int total = n + (int)s.gates.size();
	// Only what the outputs use.
	std::vector<bool> needed((size_t)total, false);
	std::function<void(int)> mark = [&](int net) {
		if (net < n || needed[net]) return;
		needed[net] = true;
		for (int i : s.gates[net - n].inputs) mark(i);
	};
	for (const Output& o : outputs) if (!o.op.isConstant) mark(o.op.net);

	// Columns: a switch is in column 0; a gate one past its latest input.
	std::vector<int> level((size_t)total, 0);
	for (int g = 0; g < (int)s.gates.size(); g++) {
		if (!needed[n + g]) continue;
		int m = 0;
		for (int i : s.gates[g].inputs) m = std::max(m, level[i]);
		level[n + g] = 1 + m;
	}
	int lastLevel = 1;
	for (int l : level) lastLevel = std::max(lastLevel, l);

	// Down each column (y goes up, so down is negative): switches in order,
	// then each column's gates near the middle of what feeds them.
	std::vector<double> y((size_t)total, 0);
	for (int v = 0; v < n; v++) y[v] = -v * 6.0;
	for (int l = 1; l <= lastLevel; l++) {
		std::vector<int> nets;
		for (int net = n; net < total; net++) if (needed[net] && level[net] == l) nets.push_back(net);
		if (nets.empty()) continue;
		auto want = [&](int net) {
			const std::vector<int>& ins = s.gates[net - n].inputs;
			double sum = 0;
			for (int i : ins) sum += y[i];
			return sum / std::max<size_t>(1, ins.size());
		};
		std::stable_sort(nets.begin(), nets.end(), [&](int a, int b) { return want(a) > want(b); });
		double floor = std::numeric_limits<double>::infinity();
		for (int net : nets) {
			const double h = height(s.gates[net - n].inputs.size());
			double at = want(net);
			if (at + h / 2 + gap > floor) at = floor - gap - h / 2;
			y[net] = at;
			floor = at - h / 2;
		}
	}

	std::map<int, int> index;   // net -> part
	auto add = [&](const std::string& gate, double x, double yy, const std::string& label = "") {
		out.parts.push_back({ gate, x, yy, label });
		return (int)out.parts.size() - 1;
	};
	// Every variable gets its switch, used or not, so the truth table has a
	// column for each.
	for (int v = 0; v < n; v++) {
		index[v] = add("AA_TOGGLE", 0, y[v]);
		const std::string& name = formulas.variables[v];
		add("AA_LABEL", -3.5 - name.size() * 0.6, y[v], name);
	}
	for (int net = n; net < total; net++) {
		if (!needed[net]) continue;
		const Synth::Gate& g = s.gates[net - n];
		index[net] = add(g.libraryName(), level[net] * column, y[net]);
		out.logicGates++;
		out.gateCounts[Synth::caption(g.kind)]++;
	}
	// Each gate's inputs: the highest source on the top pin, and so on down.
	for (int net = n; net < total; net++) {
		if (!needed[net]) continue;
		const Synth::Gate& g = s.gates[net - n];
		std::vector<int> sources = g.inputs;
		std::stable_sort(sources.begin(), sources.end(), [&](int a, int b) { return y[a] > y[b]; });
		for (size_t pin = 0; pin < sources.size(); pin++) {
			const int src = sources[pin];
			out.wires.push_back({ index[src], Synth::outputPin(src < n ? nullptr : &s.gates[src - n]), index[net],
			                      g.kind == Synth::NOT_ ? "IN_0" : fmt("IN_%d", (int)pin) });
		}
	}
	// The lights, in a column past the last gate, beside what drives them,
	// top to bottom by where their wires come from, so they don't cross.
	const double lightX = (lastLevel + 1) * column - 6;
	auto wanted = [&](size_t k) { return outputs[k].op.isConstant ? -(double)k * 6 : y[outputs[k].op.net]; };
	std::vector<size_t> order;
	for (size_t k = 0; k < outputs.size(); k++) order.push_back(k);
	std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return wanted(a) > wanted(b); });
	double floor = std::numeric_limits<double>::infinity();
	for (size_t k : order) {
		const Output& o = outputs[k];
		double want = wanted(k);
		if (want + 3 > floor) want = floor - 3;
		floor = want - 3;
		const int light = add("GA_LED", lightX, want);
		add("AA_LABEL", lightX + 3 + o.name.size() * 0.6, want, o.name);
		if (!o.op.isConstant) {
			const int x = o.op.net;
			out.wires.push_back({ index[x], Synth::outputPin(x < n ? nullptr : &s.gates[x - n]), light, "N_in0" });
		} else {
			const int power = add(o.op.value ? "EE_VDD" : "FF_GND", lightX - 5, want + (o.op.value ? 2 : -2));
			out.wires.push_back({ power, "OUT_0", light, "N_in0" });
		}
	}
	return out;
}

std::string checkSpec(const Parsed& p) {
	std::string s = "in";
	for (const std::string& v : p.variables) s += "\t" + v;
	for (const Function& f : p.functions) {
		s += "\nout\t" + f.name + "\t";
		for (int v : f.values) s += v == 1 ? '1' : v == 0 ? '0' : '-';
	}
	return s;
}

}  // namespace formula
