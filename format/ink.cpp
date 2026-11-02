#include "ink.hpp"

#include <algorithm>
#include <cmath>
#include <locale>
#include <sstream>

namespace cl {
namespace ink {

const char kAlpha[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

namespace {

// A product rounded to a double before anything is added to it. Compilers
// may fuse a * b + c into one rounding (clang does on ARM, GCC wherever it
// can), and the spec's numbers are JavaScript's and Python's, which never do:
// -0.005 * 100 + 0.5 is 0 there, and just below it fused.
double rounded(double v) {
	volatile double r = v;
	return r;
}

// floor(v * k + 0.5), as the reference computes it.
double halfUp(double v, double k) { return std::floor(rounded(v * k) + 0.5); }

int digitOf(char c) {
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '-') return 62;
	if (c == '_') return 63;
	return -1;
}

bool digitAt(const std::string &v, size_t i) { return i < v.size() && v[i] >= '0' && v[i] <= '9'; }

// [+-]?(\d+(\.\d*)?|\.\d+)([eE][+-]?\d+)?, the whole text.
bool isNumber(const std::string &v) {
	size_t i = 0;
	const size_t n = v.size();
	if (i < n && (v[i] == '+' || v[i] == '-')) i++;
	size_t d = 0;
	while (digitAt(v, i)) { i++; d++; }
	if (i < n && v[i] == '.') {
		i++;
		while (digitAt(v, i)) { i++; d++; }
	}
	if (d == 0) return false;
	if (i < n && (v[i] == 'e' || v[i] == 'E')) {
		i++;
		if (i < n && (v[i] == '+' || v[i] == '-')) i++;
		size_t e = 0;
		while (digitAt(v, i)) { i++; e++; }
		if (!e) return false;
	}
	return i == n;
}

// A number text isNumber() accepted, as a double (an overflow is infinite).
double toDouble(const std::string &v) {
	std::istringstream in(v[0] == '+' ? v.substr(1) : v);
	in.imbue(std::locale::classic());
	double d = 0;
	in >> d;
	if (in.fail()) {
		const size_t e = v.find_first_of("eE");
		const bool tiny = e != std::string::npos && v.size() > e + 1 && v[e + 1] == '-';
		return tiny ? 0.0 : (v[0] == '-' ? -HUGE_VAL : HUGE_VAL);
	}
	return d;
}

std::string trimSpace(const std::string &s) {
	const size_t a = s.find_first_not_of(" \t\r\n");
	if (a == std::string::npos) return "";
	const size_t b = s.find_last_not_of(" \t\r\n");
	return s.substr(a, b - a + 1);
}

double segDist(double px, double py, double ax, double ay, double bx, double by) {
	const double dx = bx - ax, dy = by - ay;
	const double L = rounded(dx * dx) + rounded(dy * dy);
	if (L == 0) return std::hypot(px - ax, py - ay);
	double t = (rounded((px - ax) * dx) + rounded((py - ay) * dy)) / L;
	t = t < 0 ? 0.0 : t > 1 ? 1.0 : t;
	return std::hypot(px - (ax + rounded(t * dx)), py - (ay + rounded(t * dy)));
}

int clampWidth(double centi) {
	if (!(centi >= kWidthMinCenti)) return kWidthMinCenti;   // NaN too
	if (centi > kWidthMaxCenti) return kWidthMaxCenti;
	return (int)centi;
}

// UTF-8 -> code points, leniently: a malformed sequence, an overlong one or
// an encoded surrogate becomes U+FFFD (one per bad byte, or per sequence).
std::vector<char32_t> decodeUtf8(const std::string &s) {
	std::vector<char32_t> out;
	out.reserve(s.size());
	size_t i = 0;
	const size_t n = s.size();
	while (i < n) {
		const unsigned char c = (unsigned char)s[i];
		if (c < 0x80) { out.push_back(c); i++; continue; }
		int len = 0;
		char32_t cp = 0, min = 0;
		if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; min = 0x80; }
		else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; min = 0x800; }
		else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; min = 0x10000; }
		else { out.push_back(0xFFFD); i++; continue; }
		if (i + len > n) { out.push_back(0xFFFD); i++; continue; }
		bool ok = true;
		for (int k = 1; k < len; k++) {
			const unsigned char d = (unsigned char)s[i + k];
			if ((d & 0xC0) != 0x80) { ok = false; break; }
			cp = (cp << 6) | (d & 0x3F);
		}
		if (!ok || cp < min || cp > 0x10FFFF) { out.push_back(0xFFFD); i++; continue; }
		if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;   // a lone surrogate
		out.push_back(cp);
		i += len;
	}
	return out;
}

void appendUtf8(std::string &o, char32_t cp) {
	if (cp < 0x80) o += (char)cp;
	else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
	else if (cp < 0x10000) {
		o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F));
	} else {
		o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F));
		o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F));
	}
}

// Item k of a list as text, or null when missing or a list.
const std::string *atomAt(const SNode &n, size_t k) {
	if (!n.isList() || k >= n.items.size() || n.items[k].isList()) return nullptr;
	return &n.items[k].text;
}

// "1", "01", "001"...: version 1. Anything else isn't.
bool isVersionOne(const std::string *v) {
	if (v == nullptr || v->empty()) return false;
	for (char c : *v) if (c < '0' || c > '9') return false;
	const size_t nz = v->find_first_not_of('0');
	return nz != std::string::npos && v->substr(nz) == "1";
}

} // namespace

std::int32_t quantize(double world) { return (std::int32_t)halfUp(world, 100); }

std::string encodeVlq(long long v) {
	unsigned long long n = v < 0 ? ((unsigned long long)(-v) << 1) | 1 : (unsigned long long)v << 1;
	std::string out;
	while (true) {
		unsigned d = (unsigned)(n & 31);
		n >>= 5;
		if (n) d |= 32;
		out += kAlpha[d];
		if (!n) return out;
	}
}

std::vector<long long> decodeVlqAll(const std::string &s) {
	std::vector<long long> vals;
	unsigned long long n = 0;
	int shift = 0, chars = 0;
	for (char c : s) {
		const int d = digitOf(c);
		if (d < 0) throw InkError("bad-char");
		if (++chars > kMaxVlqChars) throw InkError("too-long");
		n |= (unsigned long long)(d & 31) << shift;
		shift += 5;
		if (d & 32) continue;
		const long long v = (n & 1) ? -(long long)(n >> 1) : (long long)(n >> 1);
		if (v > kMaxAbs || v < -kMaxAbs) throw InkError("out-of-range");
		vals.push_back(v);
		n = 0; shift = 0; chars = 0;
	}
	if (chars) throw InkError("cut-short");
	return vals;
}

std::string encodePoints(const std::vector<std::int32_t> &xy) {
	std::string out;
	long long px = 0, py = 0;
	for (size_t i = 0; i + 1 < xy.size(); i += 2) {
		out += encodeVlq(xy[i] - px);
		out += encodeVlq(xy[i + 1] - py);
		px = xy[i];
		py = xy[i + 1];
	}
	return out;
}

std::vector<std::int32_t> decodePoints(const std::string &s, size_t limit) {
	const std::vector<long long> vals = decodeVlqAll(s);
	if (vals.empty()) throw InkError("empty");
	if (vals.size() % 2) throw InkError("odd");
	if (vals.size() / 2 > limit) throw InkError("too-many-points");
	std::vector<std::int32_t> out;
	out.reserve(vals.size());
	long long x = 0, y = 0;
	for (size_t i = 0; i < vals.size(); i += 2) {
		x += vals[i];
		y += vals[i + 1];
		if (x > kMaxAbs || x < -kMaxAbs || y > kMaxAbs || y < -kMaxAbs) throw InkError("out-of-range");
		out.push_back((std::int32_t)x);
		out.push_back((std::int32_t)y);
	}
	return out;
}

std::string encodePressure(const std::vector<double> &ps) {
	std::string out;
	for (double p : ps) {
		double v = halfUp(p, 63);
		if (!(v >= 0)) v = 0;
		if (v > 63) v = 63;
		out += kAlpha[(int)v];
	}
	return out;
}

bool pressureOk(const std::string &s, size_t n) {
	if (s.size() != n) return false;
	for (char c : s) if (digitOf(c) < 0) return false;
	return true;
}

double pressureAt(const std::string &s, size_t i) {
	if (i >= s.size()) return 1;
	const int d = digitOf(s[i]);
	return d < 0 ? 1.0 : d / 63.0;
}

std::vector<size_t> simplify(const std::vector<double> &xy, double eps) {
	const size_t n = xy.size() / 2;
	std::vector<size_t> out;
	if (n <= 2) {
		for (size_t i = 0; i < n; i++) out.push_back(i);
		return out;
	}
	std::vector<bool> keep(n, false);
	keep[0] = keep[n - 1] = true;
	std::vector<std::pair<size_t, size_t>> stack{ { 0, n - 1 } };
	while (!stack.empty()) {
		const std::pair<size_t, size_t> ab = stack.back();
		stack.pop_back();
		const size_t a = ab.first, b = ab.second;
		double best = -1;
		size_t bi = 0;
		bool found = false;
		for (size_t i = a + 1; i < b; i++) {
			const double d = segDist(xy[2 * i], xy[2 * i + 1], xy[2 * a], xy[2 * a + 1], xy[2 * b], xy[2 * b + 1]);
			if (d > best) { best = d; bi = i; found = true; }
		}
		if (found && best > eps) {
			keep[bi] = true;
			stack.push_back({ a, bi });
			stack.push_back({ bi, b });
		}
	}
	for (size_t i = 0; i < n; i++) if (keep[i]) out.push_back(i);
	return out;
}

InkStroke makeStroke(const std::string &tool, const std::string &color, double widthWorld,
                     const std::vector<double> &xy, const std::vector<double> &pressure, double eps) {
	InkStroke st;
	st.tool = tool;
	st.color = color;
	st.widthCenti = clampWidth(halfUp(widthWorld, 100));
	const bool withPressure = !pressure.empty();
	std::vector<double> pr;
	for (size_t i : simplify(xy, eps)) {
		const std::int32_t x = quantize(xy[2 * i]), y = quantize(xy[2 * i + 1]);
		const size_t m = st.xy.size();
		if (m >= 2 && st.xy[m - 2] == x && st.xy[m - 1] == y) continue;
		st.xy.push_back(x);
		st.xy.push_back(y);
		if (withPressure) pr.push_back(i < pressure.size() ? pressure[i] : 1.0);
	}
	if (withPressure) st.pressure = encodePressure(pr);
	return st;
}

bool isToken(const std::string &s) {
	if (s.empty() || s.size() > 32 || s[0] < 'a' || s[0] > 'z') return false;
	for (char c : s)
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
	return true;
}

std::string normalizeNotes(const std::string &utf8, size_t cap) {
	std::vector<char32_t> cps = decodeUtf8(utf8);
	size_t start = 0;
	while (start < cps.size() && cps[start] == 0xFEFF) start++;
	std::string out;
	out.reserve(utf8.size());
	size_t count = 0;
	for (size_t i = start; i < cps.size() && count < cap; i++) {
		char32_t c = cps[i];
		if (c == '\r') {
			if (i + 1 < cps.size() && cps[i + 1] == '\n') i++;
			c = '\n';
		}
		if ((c <= 0x08) || c == 0x0B || c == 0x0C || (c >= 0x0E && c <= 0x1F) || c == 0x7F) continue;
		appendUtf8(out, c);
		count++;
	}
	return out;
}

bool notesWorthWriting(const std::string &s) {
	for (char c : s) if (c != ' ' && c != '\t' && c != '\n') return true;
	return false;
}

size_t scalarCount(const std::string &utf8) {
	size_t n = 0;
	for (unsigned char c : utf8) if ((c & 0xC0) != 0x80) n++;
	return n;
}

std::string firstScalars(const std::string &utf8, size_t n) {
	size_t seen = 0;
	for (size_t i = 0; i < utf8.size(); i++) {
		if (((unsigned char)utf8[i] & 0xC0) != 0x80) {
			if (seen == n) return utf8.substr(0, i);
			seen++;
		}
	}
	return utf8;
}

std::string widthText(int centi) {
	const double v = centi / 100.0;
	std::ostringstream o;
	o.imbue(std::locale::classic());
	if (v == std::floor(v)) o << (long long)v;
	else { o.precision(10); o << v; }
	return o.str();
}

InkStroke readStroke(const SNode &x) {
	const std::vector<SNode> &items = x.items;
	if (items.size() < 5) throw InkError("short");
	for (size_t k = 1; k < 5; k++)
		if (items[k].isList()) throw InkError("list-in-value");
	const std::string wtext = trimSpace(items[3].text);
	if (!isNumber(wtext)) throw InkError("bad-width");
	const double w = toDouble(wtext);
	if (!std::isfinite(w) || w <= 0) throw InkError("bad-width");
	InkStroke st;
	st.tool = isToken(items[1].text) ? items[1].text : "pen";
	st.color = isToken(items[2].text) ? items[2].text : "ink";
	st.widthCenti = clampWidth(halfUp(w, 100));
	st.xy = decodePoints(items[4].text);
	if (items.size() >= 6 && !items[5].isList() && pressureOk(items[5].text, st.points()))
		st.pressure = items[5].text;
	return st;
}

SNode strokeNode(const InkStroke &s) {
	SNode n = SNode::list();
	n.add(SNode::sym("stroke"));
	n.add(SNode::sym(isToken(s.tool) ? s.tool : "pen"));
	n.add(SNode::sym(isToken(s.color) ? s.color : "ink"));
	n.add(SNode::sym(widthText(s.widthCenti)));
	n.add(SNode::str(encodePoints(s.xy)));
	if (!s.pressure.empty()) n.add(SNode::str(s.pressure));
	return n;
}

void readPageInk(const SNode &page, PageInk &ink, int &dropped, size_t &docPoints) {
	std::vector<const SNode *> drawings;
	for (const SNode &e : page.items)
		if (e.isList() && e.head() == "drawing") drawings.push_back(&e);
	if (drawings.empty()) return;
	bool foreign = false;
	for (const SNode *d : drawings)
		if (!isVersionOne(atomAt(*d, 1))) foreign = true;
	if (foreign) {
		// Kept whole and written back (3.5); none of it is drawn or edited.
		for (const SNode *d : drawings) ink.foreign.push_back(*d);
		return;
	}
	size_t pagePoints = 0;
	for (const SNode *d : drawings) {
		for (const SNode &s : d->items) {
			if (!s.isList() || s.head() != "stroke") continue;
			InkStroke st;
			try {
				st = readStroke(s);
			} catch (const InkError &) {
				dropped++;
				continue;
			}
			const size_t n = st.points();
			if (ink.strokes.size() >= kReadPageStrokes || pagePoints + n > kReadPagePoints ||
			    docPoints + n > kReadDocPoints) {
				dropped++;
				continue;
			}
			pagePoints += n;
			docPoints += n;
			ink.strokes.push_back(std::move(st));
		}
	}
}

std::vector<SNode> pageInkNodes(const PageInk &ink) {
	if (!ink.foreign.empty()) return ink.foreign;
	if (ink.strokes.empty()) return {};
	SNode d = SNode::list();
	d.add(SNode::sym("drawing"));
	d.add(SNode::sym(std::to_string(kDrawingVersion)));
	for (const InkStroke &s : ink.strokes) d.add(strokeNode(s));
	return { d };
}

} // namespace ink
} // namespace cl
