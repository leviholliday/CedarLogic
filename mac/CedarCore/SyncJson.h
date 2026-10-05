// A small strict JSON reader and writer for the sync engine (SYNC.md 2.2, 6.1).
//
// Reading: RFC 8259 only (no comments, no NaN, no trailing commas), every
// escape including surrogate pairs; an unpaired surrogate escape, a raw control
// character or invalid UTF-8 refuses the whole text. Numbers are doubles;
// isInt() is the payload rule "a whole number from 0 to 2^53-1". Nesting
// deeper than 32 levels is refused.
//
// Writing: '"', '\' and U+0000-U+001F are escaped (the short forms where JSON
// has them, else \u00XX), everything else is raw UTF-8 -- what Python's
// json.dumps(ensure_ascii=False, separators=(",", ":")) writes.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace clsync {
namespace json {

struct Value {
	enum Type { Null, Bool, Number, String, Array, Object } type = Null;
	bool b = false;
	double n = 0;
	std::string s;
	std::vector<Value> a;
	std::vector<std::pair<std::string, Value>> o;   // in order; with a repeated key the last one counts

	Value() = default;
	static Value boolean(bool v) { Value x; x.type = Bool; x.b = v; return x; }
	static Value number(double v) { Value x; x.type = Number; x.n = v; return x; }
	static Value integer(int64_t v) { Value x; x.type = Number; x.n = (double)v; return x; }
	static Value string(std::string v) { Value x; x.type = String; x.s = std::move(v); return x; }
	static Value array() { Value x; x.type = Array; return x; }
	static Value object() { Value x; x.type = Object; return x; }

	bool isNull() const { return type == Null; }
	bool isString() const { return type == String; }
	bool isObject() const { return type == Object; }
	bool isArray() const { return type == Array; }
	bool isNumber() const { return type == Number; }
	bool isBool() const { return type == Bool; }
	// A whole number from 0 to 2^53-1 (5, 5.0 and 5e0 are all 5).
	bool isInt() const;
	int64_t i() const;   // 0 for anything that isn't a finite number inside int64

	// Objects: the member (the last of repeated keys), or nullptr.
	const Value* get(const std::string& key) const;
	Value* get(const std::string& key);
	// Objects: sets (replacing) or appends a member; returns it.
	Value& set(const std::string& key, Value v);
	void erase(const std::string& key);
	// Arrays.
	Value& push(Value v) { a.push_back(std::move(v)); return a.back(); }

	// Reads with a fallback (objects).
	std::string str(const std::string& key, const std::string& fallback = std::string()) const;
	int64_t integer(const std::string& key, int64_t fallback = 0) const;
	bool flag(const std::string& key, bool fallback = false) const;
};

// Parses the whole text (whitespace around it allowed). False if it isn't
// strict JSON; `out` is then unspecified.
bool parse(const std::string& text, Value& out);

// Compact JSON text.
std::string write(const Value& v);
// A JSON string literal, quotes included.
std::string quote(const std::string& s);

}  // namespace json
}  // namespace clsync
