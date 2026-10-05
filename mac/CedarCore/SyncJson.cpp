// Strict JSON for the sync engine (see SyncJson.h).

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS   // snprintf/sscanf/localtime/getenv are used with care
#endif

#include "SyncJson.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <locale>
#include <sstream>

namespace clsync {
namespace json {

namespace {

constexpr int kMaxDepth = 32;
constexpr double kMaxSafe = 9007199254740991.0;

void putUtf8(std::string& o, uint32_t cp) {
	if (cp < 0x80) o += (char)cp;
	else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 63)); }
	else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63)); }
	else {
		o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 63));
		o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63));
	}
}

// The length of the valid UTF-8 sequence starting at s[i] (a non-ASCII lead
// byte), or 0.
size_t utf8Run(const std::string& s, size_t i) {
	const unsigned char c = (unsigned char)s[i];
	int n;
	uint32_t cp;
	if (c >= 0xC2 && c <= 0xDF) { n = 1; cp = c & 31; }
	else if ((c >> 4) == 14) { n = 2; cp = c & 15; }
	else if (c >= 0xF0 && c <= 0xF4) { n = 3; cp = c & 7; }
	else return 0;
	for (int k = 1; k <= n; k++) {
		if (i + k >= s.size() || ((unsigned char)s[i + k] >> 6) != 2) return 0;
		cp = (cp << 6) | ((unsigned char)s[i + k] & 63);
	}
	if (n == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) return 0;
	if (n == 3 && (cp < 0x10000 || cp > 0x10FFFF)) return 0;
	return (size_t)n + 1;
}

struct Parser {
	const std::string& s;
	size_t i = 0;
	bool ok = true;

	explicit Parser(const std::string& t) : s(t) {}

	void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++; }
	bool lit(const char* w) {
		const size_t n = strlen(w);
		if (s.compare(i, n, w) == 0) { i += n; return true; }
		return false;
	}
	unsigned hex4() {
		if (i + 4 > s.size()) { ok = false; return 0; }
		unsigned v = 0;
		for (int k = 0; k < 4; k++) {
			const char c = s[i++];
			v <<= 4;
			if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
			else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
			else ok = false;
		}
		return v;
	}
	std::string str() {
		std::string o;
		i++;   // the opening quote
		while (ok && i < s.size()) {
			const unsigned char c = (unsigned char)s[i];
			if (c == '"') { i++; return o; }
			if (c < 0x20) { ok = false; break; }
			if (c >= 0x80) {
				const size_t n = utf8Run(s, i);
				if (n == 0) { ok = false; break; }
				o.append(s, i, n);
				i += n;
				continue;
			}
			i++;
			if (c != '\\') { o += (char)c; continue; }
			if (i >= s.size()) break;
			const char e = s[i++];
			switch (e) {
			case '"': o += '"'; break;
			case '\\': o += '\\'; break;
			case '/': o += '/'; break;
			case 'b': o += '\b'; break;
			case 'f': o += '\f'; break;
			case 'n': o += '\n'; break;
			case 'r': o += '\r'; break;
			case 't': o += '\t'; break;
			case 'u': {
				uint32_t cp = hex4();
				if (cp >= 0xD800 && cp <= 0xDBFF) {   // a pair, or refused
					if (!lit("\\u")) { ok = false; break; }
					const unsigned lo = hex4();
					if (lo < 0xDC00 || lo > 0xDFFF) { ok = false; break; }
					cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
				} else if (cp >= 0xDC00 && cp <= 0xDFFF) {
					ok = false;
					break;
				}
				putUtf8(o, cp);
				break;
			}
			default: ok = false;
			}
		}
		ok = false;
		return o;
	}
	bool digit() const { return i < s.size() && s[i] >= '0' && s[i] <= '9'; }
	Value val(int depth) {
		Value v;
		ws();
		if (depth > kMaxDepth || i >= s.size()) { ok = false; return v; }
		const char c = s[i];
		if (c == '{') {
			v.type = Value::Object;
			i++;
			ws();
			if (i < s.size() && s[i] == '}') { i++; return v; }
			while (ok) {
				ws();
				if (i >= s.size() || s[i] != '"') { ok = false; break; }
				std::string k = str();
				ws();
				if (!lit(":")) { ok = false; break; }
				Value m = val(depth + 1);
				if (!ok) break;
				v.o.emplace_back(std::move(k), std::move(m));
				ws();
				if (lit(",")) continue;
				if (lit("}")) break;
				ok = false;
			}
		} else if (c == '[') {
			v.type = Value::Array;
			i++;
			ws();
			if (i < s.size() && s[i] == ']') { i++; return v; }
			while (ok) {
				v.a.push_back(val(depth + 1));
				ws();
				if (lit(",")) continue;
				if (lit("]")) break;
				ok = false;
			}
		} else if (c == '"') {
			v.type = Value::String;
			v.s = str();
		} else if (lit("true")) {
			v = Value::boolean(true);
		} else if (lit("false")) {
			v = Value::boolean(false);
		} else if (lit("null")) {
		} else {
			const size_t st = i;
			if (s[i] == '-') i++;
			if (i < s.size() && s[i] == '0') i++;
			else if (i < s.size() && s[i] >= '1' && s[i] <= '9') while (digit()) i++;
			else { ok = false; return v; }
			if (i < s.size() && s[i] == '.') {
				i++;
				if (!digit()) ok = false;
				while (digit()) i++;
			}
			if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
				i++;
				if (i < s.size() && (s[i] == '+' || s[i] == '-')) i++;
				if (!digit()) ok = false;
				while (digit()) i++;
			}
			// The grammar is checked; the conversion must not depend on the locale.
			std::istringstream in(s.substr(st, i - st));
			in.imbue(std::locale::classic());
			double d = 0;
			in >> d;
			if (in.fail()) {
				// Out of range: +-HUGE (never an integer the payload rules accept).
				d = s[st] == '-' ? -HUGE_VAL : HUGE_VAL;
			}
			v = Value::number(d);
		}
		return v;
	}
};

void writeString(std::string& o, const std::string& s) {
	o += '"';
	for (unsigned char c : s) {
		switch (c) {
		case '"': o += "\\\""; break;
		case '\\': o += "\\\\"; break;
		case '\n': o += "\\n"; break;
		case '\r': o += "\\r"; break;
		case '\t': o += "\\t"; break;
		case '\b': o += "\\b"; break;
		case '\f': o += "\\f"; break;
		default:
			if (c < 0x20) {
				char buf[8];
				snprintf(buf, sizeof buf, "\\u%04x", c);
				o += buf;
			} else {
				o += (char)c;
			}
		}
	}
	o += '"';
}

void writeValue(std::string& o, const Value& v) {
	switch (v.type) {
	case Value::Null: o += "null"; break;
	case Value::Bool: o += v.b ? "true" : "false"; break;
	case Value::Number: {
		char buf[40];
		if (std::isfinite(v.n) && std::floor(v.n) == v.n && std::fabs(v.n) <= kMaxSafe) {
			snprintf(buf, sizeof buf, "%lld", (long long)v.n);
		} else if (std::isfinite(v.n)) {
			snprintf(buf, sizeof buf, "%.17g", v.n);
			for (char* p = buf; *p; p++) if (*p == ',') *p = '.';   // whatever the locale says
		} else {
			snprintf(buf, sizeof buf, "null");
		}
		o += buf;
		break;
	}
	case Value::String: writeString(o, v.s); break;
	case Value::Array:
		o += '[';
		for (size_t k = 0; k < v.a.size(); k++) {
			if (k) o += ',';
			writeValue(o, v.a[k]);
		}
		o += ']';
		break;
	case Value::Object:
		o += '{';
		for (size_t k = 0; k < v.o.size(); k++) {
			if (k) o += ',';
			writeString(o, v.o[k].first);
			o += ':';
			writeValue(o, v.o[k].second);
		}
		o += '}';
		break;
	}
}

}  // namespace

bool Value::isInt() const {
	return type == Number && std::isfinite(n) && std::floor(n) == n && n >= 0 && n <= kMaxSafe;
}

const Value* Value::get(const std::string& key) const {
	if (type != Object) return nullptr;
	for (size_t k = o.size(); k-- > 0;)
		if (o[k].first == key) return &o[k].second;
	return nullptr;
}

Value* Value::get(const std::string& key) {
	return const_cast<Value*>(static_cast<const Value*>(this)->get(key));
}

Value& Value::set(const std::string& key, Value v) {
	if (type != Object) { *this = object(); }
	if (Value* m = get(key)) { *m = std::move(v); return *m; }
	o.emplace_back(key, std::move(v));
	return o.back().second;
}

void Value::erase(const std::string& key) {
	for (size_t k = o.size(); k-- > 0;)
		if (o[k].first == key) o.erase(o.begin() + (long)k);
}

std::string Value::str(const std::string& key, const std::string& fallback) const {
	const Value* m = get(key);
	return m && m->isString() ? m->s : fallback;
}

int64_t Value::integer(const std::string& key, int64_t fallback) const {
	const Value* m = get(key);
	if (!m || m->type != Number || !std::isfinite(m->n)) return fallback;
	return (int64_t)m->n;
}

bool Value::flag(const std::string& key, bool fallback) const {
	const Value* m = get(key);
	return m && m->isBool() ? m->b : fallback;
}

bool parse(const std::string& text, Value& out) {
	Parser p(text);
	out = p.val(0);
	p.ws();
	return p.ok && p.i == text.size();
}

std::string write(const Value& v) {
	std::string o;
	writeValue(o, v);
	return o;
}

std::string quote(const std::string& s) {
	std::string o;
	writeString(o, s);
	return o;
}

}  // namespace json
}  // namespace clsync
