#pragma once

// Just enough JSON to read the drawing fixtures (tests/fixtures/drawing/*.json)
// in the tests. A lone UTF-16 surrogate in a \u escape is kept as its 3-byte
// (CESU) form, so the notes normalizer can be shown turning it into U+FFFD.

#include <cstdlib>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace minijson {

struct Value {
	enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
	bool b = false;
	double num = 0;
	std::string str;
	std::vector<Value> arr;
	std::vector<std::pair<std::string, Value>> obj;   // in file order

	bool has(const std::string &k) const {
		for (auto &p : obj) if (p.first == k) return true;
		return false;
	}
	const Value &operator[](const std::string &k) const {
		for (auto &p : obj) if (p.first == k) return p.second;
		static const Value none;
		return none;
	}
	const Value &operator[](size_t i) const { return arr.at(i); }
	size_t size() const { return kind == Array ? arr.size() : obj.size(); }
	bool isNull() const { return kind == Null; }
};

namespace detail {
inline void utf8(std::string &o, unsigned cp) {
	if (cp < 0x80) o += (char)cp;
	else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
	else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
	else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
}
struct P {
	const std::string &s;
	size_t i = 0;
	void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) i++; }
	unsigned hex4() {
		unsigned v = (unsigned)std::strtoul(s.substr(i, 4).c_str(), nullptr, 16);
		i += 4;
		return v;
	}
	std::string string() {
		i++;
		std::string o;
		while (i < s.size() && s[i] != '"') {
			char c = s[i++];
			if (c != '\\') { o += c; continue; }
			char e = s[i++];
			switch (e) {
			case 'n': o += '\n'; break;
			case 't': o += '\t'; break;
			case 'r': o += '\r'; break;
			case 'b': o += '\b'; break;
			case 'f': o += '\f'; break;
			case 'u': {
				unsigned cp = hex4();
				if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
					size_t save = i;
					i += 2;
					unsigned lo = hex4();
					if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
					else i = save;
				}
				utf8(o, cp);
				break;
			}
			default: o += e;
			}
		}
		i++;
		return o;
	}
	Value value() {
		ws();
		Value v;
		if (i >= s.size()) throw std::runtime_error("json: end");
		char c = s[i];
		if (c == '{') {
			v.kind = Value::Object;
			i++;
			ws();
			if (s[i] == '}') { i++; return v; }
			while (true) {
				ws();
				std::string k = string();
				ws();
				i++;   // :
				v.obj.emplace_back(k, value());
				ws();
				if (s[i++] == '}') return v;
			}
		}
		if (c == '[') {
			v.kind = Value::Array;
			i++;
			ws();
			if (s[i] == ']') { i++; return v; }
			while (true) {
				v.arr.push_back(value());
				ws();
				if (s[i++] == ']') return v;
			}
		}
		if (c == '"') { v.kind = Value::String; v.str = string(); return v; }
		if (s.compare(i, 4, "true") == 0) { v.kind = Value::Bool; v.b = true; i += 4; return v; }
		if (s.compare(i, 5, "false") == 0) { v.kind = Value::Bool; i += 5; return v; }
		if (s.compare(i, 4, "null") == 0) { i += 4; return v; }
		char *end = nullptr;
		v.kind = Value::Number;
		v.num = std::strtod(s.c_str() + i, &end);
		i = (size_t)(end - s.c_str());
		return v;
	}
};
} // namespace detail

inline Value parse(const std::string &text) {
	detail::P p{ text };
	return p.value();
}

} // namespace minijson
