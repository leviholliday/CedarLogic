// Sync, the bytes (SYNC.md 1 and 2): codes, HKDF on the HMAC hook, base64url,
// hex, the record envelope, payloads, fileText and the hashes, HTTP dates.
// Checked byte for byte by the vectors of SYNC.md 7.1 (SyncTest.cpp).

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS   // snprintf/sscanf/localtime/getenv are used with care
#endif

#include "SyncInternal.h"

#include "QrCodeGen.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace clsync {

const char* const kWebBase = "https://cedarlogic.netlify.app/sync/";
const char* const kAppBase = "cedarlogic://sync";

namespace {

const char kAlpha[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
const char kSalt[] = "cedarlogic-sync-v1";

int alphaIndex(char c) {
	const char* p = c ? strchr(kAlpha, c) : nullptr;
	return p ? (int)(p - kAlpha) : -1;
}

int checksum12(Crypto& cr, const uint8_t secret[16]) {
	uint8_t h[32];
	cr.sha256(secret, 16, h);
	return (h[0] << 4) | (h[1] >> 4);
}

bool keyChar(unsigned char c) {
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' || c == ' ';
}

// Step 1 of SYNC.md 1.2: the code's part of a link, or the text itself.
std::string codePart(const std::string& text) {
	size_t at = text.find("#k=");
	if (at == std::string::npos) {
		std::string head = text.substr(0, 11);
		for (char& ch : head)
			if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
		if (head == "cedarlogic:") at = std::min(text.find("?k="), text.find("&k="));
	}
	if (at == std::string::npos) return text;
	size_t end = at + 3;
	while (end < text.size() && keyChar((unsigned char)text[end])) end++;
	return text.substr(at + 3, end - at - 3);
}

// One UTF-8 character starting at s[i] (or one byte if it isn't valid).
std::string charAt(const std::string& s, size_t i) {
	const unsigned char c = (unsigned char)s[i];
	size_t n = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
	if (i + n > s.size()) n = 1;
	return s.substr(i, n);
}

}  // namespace

// ---- bytes ------------------------------------------------------------------------

std::string hex(const uint8_t* p, size_t n) {
	static const char d[] = "0123456789abcdef";
	std::string s;
	s.reserve(n * 2);
	for (size_t i = 0; i < n; i++) {
		s += d[p[i] >> 4];
		s += d[p[i] & 15];
	}
	return s;
}

bool unhex(const std::string& s, Bytes& out) {
	out.clear();
	if (s.size() % 2) return false;
	auto v = [](char c) -> int {
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	};
	for (size_t i = 0; i < s.size(); i += 2) {
		const int a = v(s[i]), b = v(s[i + 1]);
		if (a < 0 || b < 0) return false;
		out.push_back((uint8_t)(a * 16 + b));
	}
	return true;
}

std::string b64u(const Bytes& b) {
	static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
	std::string s;
	s.reserve((b.size() * 4 + 2) / 3);
	size_t i = 0;
	for (; i + 2 < b.size(); i += 3) {
		const uint32_t v = ((uint32_t)b[i] << 16) | ((uint32_t)b[i + 1] << 8) | b[i + 2];
		s += t[v >> 18]; s += t[(v >> 12) & 63]; s += t[(v >> 6) & 63]; s += t[v & 63];
	}
	if (b.size() - i == 1) {
		const uint32_t v = (uint32_t)b[i] << 16;
		s += t[v >> 18]; s += t[(v >> 12) & 63];
	} else if (b.size() - i == 2) {
		const uint32_t v = ((uint32_t)b[i] << 16) | ((uint32_t)b[i + 1] << 8);
		s += t[v >> 18]; s += t[(v >> 12) & 63]; s += t[(v >> 6) & 63];
	}
	return s;
}

bool unb64u(const std::string& s, Bytes& out) {
	out.clear();
	if (s.size() % 4 == 1) return false;
	out.reserve(s.size() * 3 / 4);
	uint32_t acc = 0;
	int bits = 0;
	for (char ch : s) {
		int v;
		if (ch >= 'A' && ch <= 'Z') v = ch - 'A';
		else if (ch >= 'a' && ch <= 'z') v = ch - 'a' + 26;
		else if (ch >= '0' && ch <= '9') v = ch - '0' + 52;
		else if (ch == '-') v = 62;
		else if (ch == '_') v = 63;
		else return false;
		acc = (acc << 6) | (uint32_t)v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out.push_back((uint8_t)(acc >> bits));
		}
		acc &= 0xFFFF;
	}
	return true;
}

std::string sha256Hex(Crypto& cr, const std::string& s) {
	uint8_t h[32];
	cr.sha256((const uint8_t*)s.data(), s.size(), h);
	return hex(h, 32);
}

std::string sha256Hex(Crypto& cr, const Bytes& b) {
	uint8_t h[32];
	cr.sha256(b.data(), b.size(), h);
	return hex(h, 32);
}

bool validUtf8(const std::string& s) {
	size_t i = 0;
	while (i < s.size()) {
		const unsigned char c = (unsigned char)s[i];
		int n;
		uint32_t cp;
		if (c < 0x80) { i++; continue; }
		if (c >= 0xC2 && c <= 0xDF) { n = 1; cp = c & 31; }
		else if ((c >> 4) == 14) { n = 2; cp = c & 15; }
		else if (c >= 0xF0 && c <= 0xF4) { n = 3; cp = c & 7; }
		else return false;
		for (int k = 1; k <= n; k++) {
			if (i + k >= s.size() || ((unsigned char)s[i + k] >> 6) != 2) return false;
			cp = (cp << 6) | ((unsigned char)s[i + k] & 63);
		}
		if (n == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) return false;
		if (n == 3 && (cp < 0x10000 || cp > 0x10FFFF)) return false;
		i += (size_t)n + 1;
	}
	return true;
}

void putUtf8(std::string& o, uint32_t cp) {
	if (cp < 0x80) o += (char)cp;
	else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 63)); }
	else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63)); }
	else {
		o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 63));
		o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63));
	}
}

size_t scalarCount(const std::string& s) {
	size_t n = 0;
	for (unsigned char c : s)
		if ((c & 0xC0) != 0x80) n++;
	return n;
}

bool isHex(const std::string& s, size_t len) {
	if (s.size() != len) return false;
	for (char c : s)
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
	return true;
}

bool isUuid(const std::string& s) {
	if (s.size() != 36) return false;
	for (size_t i = 0; i < 36; i++) {
		const char c = s[i];
		if (i == 8 || i == 13 || i == 18 || i == 23) {
			if (c != '-') return false;
		} else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
			return false;
		}
	}
	return s[14] == '4' && (s[19] == '8' || s[19] == '9' || s[19] == 'a' || s[19] == 'b');
}

std::string randomHex(Crypto& cr, size_t bytes) {
	Bytes b(bytes);
	if (!cr.random(b.data(), b.size())) return std::string();
	return hex(b);
}

std::string newUuid(Crypto& cr) {
	uint8_t b[16];
	if (!cr.random(b, 16)) return std::string();
	b[6] = (uint8_t)((b[6] & 0x0F) | 0x40);
	b[8] = (uint8_t)((b[8] & 0x3F) | 0x80);
	const std::string h = hex(b, 16);
	return h.substr(0, 8) + "-" + h.substr(8, 4) + "-" + h.substr(12, 4) + "-" + h.substr(16, 4) + "-" + h.substr(20);
}

// ---- text and hashes (2.3) ----------------------------------------------------------

std::string fileText(const std::string& b) {
	static const uint16_t k80[32] = { 0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
	                                  0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
	                                  0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178 };
	if (validUtf8(b)) return dropBom(b);
	std::string t;
	t.reserve(b.size() + b.size() / 8);
	for (unsigned char c : b) putUtf8(t, c >= 0x80 && c <= 0x9F ? k80[c - 0x80] : c);
	return dropBom(t);
}

std::string dropBom(const std::string& s) { return s.compare(0, 3, "\xEF\xBB\xBF") == 0 ? s.substr(3) : s; }

std::string trimAscii(const std::string& s) {
	const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
	return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::string normalizeCdl(const std::string& s0) {
	const std::string s = dropBom(s0);
	if (s.find('\r') == std::string::npos) return s;
	std::string o;
	o.reserve(s.size());
	for (size_t i = 0; i < s.size(); i++)
		if (!(s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n')) o += s[i];
	return o;
}

std::string normalizeName(const std::string& s) { return trimAscii(dropBom(s)); }

std::string payloadName(const std::string& s) {
	std::string n = normalizeName(s);
	if (n.empty()) return "Untitled";
	if (scalarCount(n) <= 200) return n;
	size_t count = 0, i = 0;
	for (; i < n.size(); i++) {
		if (((unsigned char)n[i] & 0xC0) != 0x80) {
			if (count == 200) break;
			count++;
		}
	}
	return n.substr(0, i);
}

std::string nameHash(Crypto& cr, const std::string& name) { return sha256Hex(cr, normalizeName(name)); }
std::string cdlHash(Crypto& cr, const std::string& cdl) { return sha256Hex(cr, normalizeCdl(cdl)); }
std::string contentHash(Crypto& cr, const std::string& name, const std::string& cdl) {
	return sha256Hex(cr, normalizeName(name) + std::string(1, '\0') + normalizeCdl(cdl));
}
std::string envelopeHash(Crypto& cr, const Bytes& env) { return sha256Hex(cr, env).substr(0, 32); }

// ---- codes (1.2) ---------------------------------------------------------------------

std::string encodeCode(Crypto& cr, const uint8_t secret[16]) {
	uint8_t bits[18];
	memcpy(bits, secret, 16);
	const int ck = checksum12(cr, secret);
	bits[16] = (uint8_t)(ck >> 4);
	bits[17] = (uint8_t)((ck & 15) << 4);
	std::string s;
	for (int i = 0; i < 28; i++) {
		int v = 0;
		for (int b = 0; b < 5; b++) {
			const int bit = i * 5 + b;
			v = (v << 1) | ((bits[bit / 8] >> (7 - bit % 8)) & 1);
		}
		s += kAlpha[v];
	}
	return s;
}

bool normalizeCode(const std::string& text0, std::string& code, std::string& why) {
	const std::string text = codePart(text0);
	code.clear();
	for (size_t i = 0; i < text.size(); i++) {
		const unsigned char ch = (unsigned char)text[i];
		if (ch == '-' || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
		if (ch == 0xC2 && i + 1 < text.size() && (unsigned char)text[i + 1] == 0xA0) { i++; continue; }   // U+00A0
		if (ch >= 0x80) { why = "symbol"; return false; }   // every other non-ASCII character
		char c = (ch >= 'a' && ch <= 'z') ? (char)(ch - 32) : (char)ch;
		if (c == 'O') c = '0';
		if (c == 'I' || c == 'L') c = '1';
		if (alphaIndex(c) < 0) { why = "symbol"; return false; }
		code += c;
	}
	if (code.size() != 28) { why = "length"; return false; }
	return true;
}

bool decodeCode(Crypto& cr, const std::string& text, uint8_t secret[16], std::string& why) {
	std::string code;
	if (!normalizeCode(text, code, why)) return false;
	uint8_t bits[18] = {};
	for (int i = 0; i < 28; i++) {
		const int v = alphaIndex(code[(size_t)i]);
		for (int b = 0; b < 5; b++) {
			const int bit = i * 5 + b;
			if ((v >> (4 - b)) & 1) bits[bit / 8] |= (uint8_t)(0x80 >> (bit % 8));
		}
	}
	memcpy(secret, bits, 16);
	const int ck = (bits[16] << 4) | (bits[17] >> 4);
	if (ck != checksum12(cr, secret)) { why = "checksum"; return false; }
	return true;
}

std::string newCode(Crypto& cr) {
	uint8_t secret[16];
	if (!cr.random(secret, 16)) return std::string();
	return encodeCode(cr, secret);
}

bool parseCode(Crypto& cr, const std::string& text, std::string& code, std::string& why) {
	uint8_t secret[16];
	if (!decodeCode(cr, text, secret, why)) return false;
	return normalizeCode(text, code, why);
}

std::string whyText(const std::string& why, const std::string& text0) {
	if (why == "length") {
		std::string code, w;
		// Count what would be symbols, as the parser does.
		const std::string text = codePart(text0);
		size_t n = 0;
		for (size_t i = 0; i < text.size(); i++) {
			const unsigned char ch = (unsigned char)text[i];
			if (ch == '-' || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
			if (ch == 0xC2 && i + 1 < text.size() && (unsigned char)text[i + 1] == 0xA0) { i++; continue; }
			n++;
		}
		return "A sync code has 28 letters and digits; this has " + std::to_string(n) + ".";
	}
	if (why == "symbol") {
		const std::string text = codePart(text0);
		for (size_t i = 0; i < text.size(); i++) {
			const unsigned char ch = (unsigned char)text[i];
			if (ch == '-' || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
			if (ch == 0xC2 && i + 1 < text.size() && (unsigned char)text[i + 1] == 0xA0) { i++; continue; }
			char c = (ch >= 'a' && ch <= 'z') ? (char)(ch - 32) : (char)ch;
			if (c == 'O' || c == 'I' || c == 'L') continue;
			if (ch < 0x80 && alphaIndex(c) >= 0) continue;
			std::string bad = charAt(text, i);
			if (bad == " " || bad.empty()) bad = "?";
			return "\xE2\x80\x98" + bad + "\xE2\x80\x99 can't be in a sync code.";
		}
		return "That isn't a sync code.";
	}
	if (why == "checksum") return "That code has a typo in it. Check it against the other device.";
	return std::string();
}

std::string groupCode(const std::string& code) {
	std::string out;
	for (size_t i = 0; i < code.size(); i += 4) {
		if (i) out += '-';
		out += code.substr(i, 4);
	}
	return out;
}

std::vector<bool> qr(const std::string& text, int& size) {
	return qrcodegen::encodeBinary(std::vector<uint8_t>(text.begin(), text.end()), qrcodegen::Ecc::Medium, size);
}

std::string webLink(const std::string& code) { return std::string(kWebBase) + "#k=" + code; }
std::string appLink(const std::string& code) { return std::string(kAppBase) + "#k=" + code; }

// ---- keys (1.3) ------------------------------------------------------------------------

Bytes hkdf(Crypto& cr, const uint8_t* ikm, size_t ikmLen, const std::string& info, size_t len) {
	uint8_t prk[32], t[32];
	cr.hmacSha256((const uint8_t*)kSalt, strlen(kSalt), ikm, ikmLen, prk);
	Bytes out, block;
	for (uint8_t i = 1; out.size() < len; i++) {
		Bytes msg(block);
		msg.insert(msg.end(), info.begin(), info.end());
		msg.push_back(i);
		cr.hmacSha256(prk, 32, msg.data(), msg.size(), t);
		block.assign(t, t + 32);
		out.insert(out.end(), t, t + 32);
	}
	out.resize(len);
	return out;
}

Keys deriveKeys(Crypto& cr, const uint8_t secret[16]) {
	Keys k;
	k.spaceId = hex(hkdf(cr, secret, 16, "space-id", 16));
	k.authToken = b64u(hkdf(cr, secret, 16, "auth-token", 32));
	k.authHash = sha256Hex(cr, k.authToken);
	k.deleteToken = b64u(hkdf(cr, secret, 16, "delete-token", 32));
	k.deleteHash = sha256Hex(cr, k.deleteToken);
	k.recordKey = hkdf(cr, secret, 16, "record-key", 32);
	return k;
}

Keys keysForCode(Crypto& cr, const std::string& code) {
	uint8_t secret[16];
	std::string why;
	if (!decodeCode(cr, code, secret, why)) return Keys();
	Keys k = deriveKeys(cr, secret);
	memset(secret, 0, sizeof secret);
	return k;
}

// ---- envelopes (1.4) ---------------------------------------------------------------------

std::string aadText(const std::string& id, int64_t ver, int flags) {
	return "cedarlogic-sync/1|" + id + "|" + std::to_string(ver) + "|" + std::to_string(flags);
}

namespace {

bool sealWith(Crypto& cr, const Bytes& key, const std::string& id, int64_t ver, const std::string& payload, bool compress,
              const uint8_t nonce[12], Bytes& env, int& flags) {
	Bytes plain(payload.begin(), payload.end());
	flags = 0;
	if (compress) {
		Bytes z;
		if (cr.deflateRaw(plain, z)) {
			plain.swap(z);
			flags = 1;
		}
	}
	const std::string a = aadText(id, ver, flags);
	Bytes ct;
	if (key.size() != 32 || !cr.aesGcmSeal(key.data(), nonce, Bytes(a.begin(), a.end()), plain, ct)) return false;
	if (ct.size() != plain.size() + 16) return false;
	env.clear();
	env.reserve(14 + ct.size());
	env.push_back(1);
	env.push_back((uint8_t)flags);
	env.insert(env.end(), nonce, nonce + 12);
	env.insert(env.end(), ct.begin(), ct.end());
	return true;
}

}  // namespace

bool sealRecord(Crypto& cr, const Bytes& key, const std::string& id, int64_t ver, const std::string& payload, bool compress,
                size_t maxEnvelope, Bytes& env, std::string& why) {
	if (!isUuid(id) || ver < 1 || ver > kMaxSafeInt) { why = "bad record"; return false; }
	uint8_t nonce[12];
	if (!cr.random(nonce, 12)) { why = "rng"; return false; }   // never a zeroed nonce
	int flags = 0;
	if (!sealWith(cr, key, id, ver, payload, compress, nonce, env, flags)) { why = "cipher"; return false; }
	if (env.size() > maxEnvelope) { why = "too big"; env.clear(); return false; }
	return true;
}

bool sealForTest(Crypto& cr, const Bytes& key, const std::string& id, int64_t ver, const std::string& payload, int flags,
                 const Bytes& nonce, Bytes& env) {
	if (nonce.size() != 12) return false;
	int got = 0;
	return sealWith(cr, key, id, ver, payload, flags & 1, nonce.data(), env, got) && got == (flags & 1);
}

bool openRecord(Crypto& cr, const Bytes& key, const std::string& id, int64_t ver, const Bytes& env, std::string& payload) {
	payload.clear();
	if (env.size() < 30 || env[0] != 1 || (env[1] & ~1) || key.size() != 32) return false;
	const std::string a = aadText(id, ver, env[1]);
	Bytes plain;
	if (!cr.aesGcmOpen(key.data(), env.data() + 2, Bytes(a.begin(), a.end()), Bytes(env.begin() + 14, env.end()), plain))
		return false;
	if (env[1] & 1) {
		Bytes inflated;
		if (!cr.inflateRaw(plain, kMaxPlaintext, inflated) || inflated.size() > kMaxPlaintext) return false;
		plain.swap(inflated);
	}
	payload.assign(plain.begin(), plain.end());
	return true;
}

// ---- payloads (2.2) -----------------------------------------------------------------------

namespace {

bool noLoneSurrogates(const json::Value&) { return true; }   // the reader refuses them already

bool hex64(const json::Value* j) { return j && j->isString() && isHex(j->s, 64); }

void putBase(std::string& o, const std::string* baseName, const std::string* baseCdl) {
	if (!baseName || !baseCdl || !isHex(*baseName, 64) || !isHex(*baseCdl, 64)) return;   // unknown ("") is no base
	o += ",\"base\":{\"name\":" + json::quote(*baseName) + ",\"cdl\":" + json::quote(*baseCdl) + "}";
}

}  // namespace

std::string readPayload(const std::string& bytes, Payload& p) {
	p = Payload();
	if (!validUtf8(bytes)) return "invalid";
	json::Value v;
	if (!json::parse(bytes, v) || !v.isObject() || !noLoneSurrogates(v)) return "invalid";
	const json::Value* ver = v.get("v");
	if (!ver || !ver->isInt() || ver->n < 1) return "invalid";
	if (ver->n > 1) return "newer";
	const json::Value* k = v.get("kind");
	if (!k || !k->isString()) return "invalid";
	p.kind = k->s;
	if (p.kind != "circuit" && p.kind != "deleted" && p.kind != "device") return "newer";
	for (const char* f : { "device", "deviceId" })
		if (const json::Value* x = v.get(f))
			if (!x->isString()) return "invalid";
	p.device = v.str("device");
	p.deviceId = v.str("deviceId");
	if (const json::Value* b = v.get("base")) {
		if (!b->isNull()) {
			if (!(b->isObject() && hex64(b->get("name")) && hex64(b->get("cdl")))) return "invalid";
			p.hasBase = true;
			p.baseName = b->get("name")->s;
			p.baseCdl = b->get("cdl")->s;
		}
	}
	if (p.kind == "circuit") {
		const json::Value *n = v.get("name"), *c = v.get("cdl"), *m = v.get("modifiedAt");
		if (!n || !n->isString() || !c || !c->isString()) return "invalid";
		if (!m || !m->isInt()) return "invalid";
		if (const json::Value* cr = v.get("createdAt")) {
			if (!cr->isInt()) return "invalid";
			p.createdAt = cr->i();
		}
		p.name = payloadName(n->s);
		p.cdl = c->s;
		p.modifiedAt = m->i();
	} else if (p.kind == "deleted") {
		const json::Value* d = v.get("deletedAt");
		if (!d || !d->isInt()) return "invalid";
		p.deletedAt = d->i();
	} else {
		const json::Value* l = v.get("lastSyncAt");
		if (!l || !l->isInt()) return "invalid";
		p.lastSyncAt = l->i();
		p.client = v.str("client");
	}
	return std::string();
}

std::string circuitJson(const std::string& name, const std::string& cdl, int64_t modifiedAt, const std::string& device,
                        const std::string& deviceId, int64_t createdAt, const std::string* baseName, const std::string* baseCdl) {
	std::string o = "{\"v\":1,\"kind\":\"circuit\",\"name\":" + json::quote(payloadName(name)) + ",\"cdl\":" + json::quote(cdl);
	if (createdAt >= 0) o += ",\"createdAt\":" + std::to_string(createdAt);
	o += ",\"modifiedAt\":" + std::to_string(modifiedAt < 0 ? 0 : modifiedAt) + ",\"device\":" + json::quote(device) +
	     ",\"deviceId\":" + json::quote(deviceId);
	putBase(o, baseName, baseCdl);
	return o + "}";
}

std::string tombstoneJson(int64_t deletedAt, const std::string& device, const std::string& deviceId,
                          const std::string* baseName, const std::string* baseCdl) {
	std::string o = "{\"v\":1,\"kind\":\"deleted\",\"deletedAt\":" + std::to_string(deletedAt < 0 ? 0 : deletedAt) +
	                ",\"device\":" + json::quote(device) + ",\"deviceId\":" + json::quote(deviceId);
	putBase(o, baseName, baseCdl);
	return o + "}";
}

std::string deviceJson(const std::string& device, const std::string& deviceId, const std::string& client, int64_t lastSyncAt) {
	return "{\"v\":1,\"kind\":\"device\",\"device\":" + json::quote(device) + ",\"deviceId\":" + json::quote(deviceId) +
	       ",\"client\":" + json::quote(client) + ",\"lastSyncAt\":" + std::to_string(lastSyncAt < 0 ? 0 : lastSyncAt) + "}";
}

int64_t effectiveTime(int64_t stamp, int64_t serverAt) {
	return std::max(kMinTime, std::min(stamp, serverAt + 60 * kSecond));
}

// ---- dates ---------------------------------------------------------------------------------

// Howard Hinnant's civil-from-days and days-from-civil.
int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
	y -= m <= 2;
	const int64_t era = (y >= 0 ? y : y - 399) / 400;
	const unsigned yoe = (unsigned)(y - era * 400);
	const unsigned doy = (153 * (m + (m > 2 ? (unsigned)-3 : 9)) + 2) / 5 + d - 1;
	const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (int64_t)doe - 719468;
}

void civilFromDays(int64_t z, int64_t& y, unsigned& m, unsigned& d) {
	z += 719468;
	const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	const unsigned doe = (unsigned)(z - era * 146097);
	const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	y = (int64_t)yoe + era * 400;
	const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	const unsigned mp = (5 * doy + 2) / 153;
	d = doy - (153 * mp + 2) / 5 + 1;
	m = mp < 10 ? mp + 3 : mp - 9;
	y += m <= 2;
}

static const char* const kMonths[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
static const char* const kDays[] = { "Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed" };   // 1970-01-01 was a Thursday

bool parseHttpDate(const std::string& s, int64_t& ms) {
	// IMF-fixdate: "Sun, 06 Nov 1994 08:49:37 GMT" (the only form servers send now).
	const size_t comma = s.find(',');
	if (comma == std::string::npos) return false;
	char mon[4] = {};
	int d = 0, y = 0, hh = 0, mm = 0, ss = 0;
	char tz[4] = {};
	if (sscanf(s.c_str() + comma + 1, " %d %3s %d %d:%d:%d %3s", &d, mon, &y, &hh, &mm, &ss, tz) != 7) return false;
	int m = -1;
	for (int k = 0; k < 12; k++)
		if (strcmp(mon, kMonths[k]) == 0) m = k + 1;
	if (m < 0 || d < 1 || d > 31 || y < 1970 || hh > 23 || mm > 59 || ss > 60 || strcmp(tz, "GMT") != 0) return false;
	ms = ((daysFromCivil(y, (unsigned)m, (unsigned)d) * 24 + hh) * 60 + mm) * 60 * 1000 + (int64_t)ss * 1000;
	return true;
}

std::string formatHttpDate(int64_t ms) {
	int64_t secs = ms >= 0 ? ms / 1000 : (ms - 999) / 1000;
	int64_t days = secs >= 0 ? secs / 86400 : (secs - 86399) / 86400;
	int64_t rem = secs - days * 86400;
	int64_t y;
	unsigned m, d;
	civilFromDays(days, y, m, d);
	char buf[64];
	snprintf(buf, sizeof buf, "%s, %02u %s %04lld %02d:%02d:%02d GMT", kDays[((days % 7) + 7) % 7], d, kMonths[m - 1],
	         (long long)y, (int)(rem / 3600), (int)(rem / 60 % 60), (int)(rem % 60));
	return buf;
}

int64_t SystemClock::now() {
	return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
}

}  // namespace clsync
