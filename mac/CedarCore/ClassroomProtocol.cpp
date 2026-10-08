// Classroom, the bytes (CLASSROOM.md 1 and 2): the three codes, the keys
// (HKDF on the HMAC hook, PBKDF2 for the join code), both envelopes, the
// payload reader and writers. Checked byte for byte by the vectors of 7.1
// (ClassroomTest.cpp). The sync engine's building blocks do the rest.

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "ClassroomInternal.h"

#include <cstring>

namespace clclass {

using clsync::b64u;
using clsync::hex;
using clsync::isHex;
using clsync::isUuid;
using clsync::sha256Hex;
using clsync::unb64u;

const char* const kWebBase = "https://cedarlogic.netlify.app/classroom/";
const char* const kService = "https://cedarlogic-classroom.leviholliday7.workers.dev";   // set at deploy (Classroom.h)
const char* const kAppBase = "cedarlogic://classroom";
const char* const kSalt = "cedarlogic-classroom-v1";
const char* const kJoinSalt = "cedarlogic-classroom-join-v1";

namespace {

const char kAlpha[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

int alphaIndex(char c) {
	const char* p = c ? strchr(kAlpha, c) : nullptr;
	return p ? (int)(p - kAlpha) : -1;
}

char kindLetter(CodeKind k) { return k == CodeKind::Teacher ? 't' : k == CodeKind::Join ? 'j' : 'm'; }
const char* kindWhat(CodeKind k) { return k == CodeKind::Teacher ? "teacher key" : k == CodeKind::Join ? "join code" : "move code"; }
const char* letterWhat(char c) {
	switch (c) {
	case 't': return "teacher key";
	case 'j': return "join code";
	case 'm': return "move code";
	default: return "sync code";
	}
}

int checksum12(Crypto& cr, const uint8_t* secret, size_t n) {
	uint8_t h[32];
	cr.sha256(secret, n, h);
	return (h[0] << 4) | (h[1] >> 4);
}

bool valueChar(unsigned char c) {
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' || c == ' ';
}

// Step 1 of 1.2: the letter of a link's code ('\0' if there is no link) and its value.
std::string codePart(const std::string& text, char& letter) {
	letter = 0;
	size_t at = std::string::npos;
	for (size_t i = 0; i + 2 < text.size(); i++)
		if (text[i] == '#' && strchr("tjmk", text[i + 1]) && text[i + 1] && text[i + 2] == '=') { at = i; break; }
	if (at == std::string::npos) {
		std::string head = text.substr(0, 11);
		for (char& ch : head)
			if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
		if (head == "cedarlogic:")
			for (size_t i = 0; i + 2 < text.size(); i++)
				if ((text[i] == '?' || text[i] == '&') && strchr("tjmk", text[i + 1]) && text[i + 1] && text[i + 2] == '=') {
					at = i;
					break;
				}
	}
	if (at == std::string::npos) return text;
	letter = text[at + 1];
	size_t end = at + 3;
	while (end < text.size() && valueChar((unsigned char)text[end])) end++;
	return text.substr(at + 3, end - at - 3);
}

// One Unicode scalar value at s[i]: its bytes (one byte if it isn't valid UTF-8).
size_t scalarLen(const std::string& s, size_t i) {
	const unsigned char c = (unsigned char)s[i];
	size_t n = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
	if (i + n > s.size()) n = 1;
	return n;
}

bool skipped(const std::string& s, size_t i, size_t n) {
	const unsigned char ch = (unsigned char)s[i];
	if (n == 1) return ch == '-' || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
	return n == 2 && ch == 0xC2 && (unsigned char)s[i + 1] == 0xA0;   // U+00A0
}

// Steps 2-5, also telling which character was wrong (for the sentence).
bool normalizeWith(CodeKind k, const std::string& text0, std::string& code, std::string& why, std::string& bad, char& letter,
                   size_t& count) {
	code.clear();
	const std::string text = codePart(text0, letter);
	if (letter && letter != kindLetter(k)) { why = "kind"; return false; }
	for (size_t i = 0; i < text.size();) {
		const size_t n = scalarLen(text, i);
		if (skipped(text, i, n)) { i += n; continue; }
		const unsigned char ch = (unsigned char)text[i];
		char c = 0;
		if (n == 1 && ch < 0x80) {
			c = (ch >= 'a' && ch <= 'z') ? (char)(ch - 32) : (char)ch;   // ASCII only
			if (c == 'O') c = '0';
			if (c == 'I' || c == 'L') c = '1';
		}
		if (!c || alphaIndex(c) < 0) {
			why = "symbol";
			bad = text.substr(i, n);
			return false;
		}
		code += c;
		i += n;
	}
	count = code.size();
	if (code.size() != codeSymbols(k)) { why = "length"; return false; }
	return true;
}

}  // namespace

// ---- codes (1.2) ------------------------------------------------------------------------

size_t codeBytes(CodeKind k) { return k == CodeKind::Join ? 6 : 16; }
size_t codeSymbols(CodeKind k) { return k == CodeKind::Join ? 12 : 28; }

std::string encodeCode(Crypto& cr, CodeKind k, const uint8_t* secret) {
	const size_t n = codeBytes(k);
	uint8_t bits[18] = {};
	memcpy(bits, secret, n);
	const int ck = checksum12(cr, secret, n);
	bits[n] = (uint8_t)(ck >> 4);
	bits[n + 1] = (uint8_t)((ck & 15) << 4);
	std::string s;
	for (size_t i = 0; i < codeSymbols(k); i++) {
		int v = 0;
		for (int b = 0; b < 5; b++) {
			const size_t bit = i * 5 + (size_t)b;
			v = (v << 1) | ((bits[bit / 8] >> (7 - bit % 8)) & 1);
		}
		s += kAlpha[v];
	}
	return s;
}

bool normalizeCode(CodeKind k, const std::string& text, std::string& code, std::string& why) {
	std::string bad;
	char letter = 0;
	size_t count = 0;
	return normalizeWith(k, text, code, why, bad, letter, count);
}

bool decodeCode(Crypto& cr, CodeKind k, const std::string& text, Bytes& secret, std::string& why) {
	std::string code;
	if (!normalizeCode(k, text, code, why)) return false;
	const size_t n = codeBytes(k);
	uint8_t bits[18] = {};
	for (size_t i = 0; i < code.size(); i++) {
		const int v = alphaIndex(code[i]);
		for (int b = 0; b < 5; b++) {
			const size_t bit = i * 5 + (size_t)b;
			if ((v >> (4 - b)) & 1) bits[bit / 8] |= (uint8_t)(0x80 >> (bit % 8));
		}
	}
	secret.assign(bits, bits + n);
	const int ck = (bits[n] << 4) | (bits[n + 1] >> 4);
	if (ck != checksum12(cr, secret.data(), n)) { why = "checksum"; secret.clear(); return false; }
	return true;
}

std::string newCode(Crypto& cr, CodeKind k) {
	uint8_t secret[16];
	if (!cr.random(secret, codeBytes(k))) return std::string();
	const std::string s = encodeCode(cr, k, secret);
	memset(secret, 0, sizeof secret);
	return s;
}

bool parseCode(Crypto& cr, CodeKind k, const std::string& text, std::string& code, std::string& why) {
	Bytes secret;
	if (!decodeCode(cr, k, text, secret, why)) return false;
	return normalizeCode(k, text, code, why);
}

// The sentences of 1.2, exactly as scripts/classroom-vectors.mjs words them.
std::string whyText(CodeKind k, const std::string& why, const std::string& text) {
	std::string code, w, bad;
	char letter = 0;
	size_t count = 0;
	normalizeWith(k, text, code, w, bad, letter, count);
	const std::string what = kindWhat(k);
	if (why == "kind") return std::string("That's a ") + letterWhat(letter) + ", not a " + what + ".";
	if (why == "symbol") return "\xE2\x80\x98" + (bad.empty() ? std::string("?") : bad) + "\xE2\x80\x99 can't be in a " + what;
	if (why == "length")
		return "A " + what + " has " + std::to_string(codeSymbols(k)) + " letters and digits; this has " + std::to_string(count);
	if (why == "checksum") {
		if (k == CodeKind::Teacher) return "That key has a typo in it. Check it against your saved copy.";
		if (k == CodeKind::Join) return "That code has a typo in it. Check it with your teacher.";
		return "That code has a typo in it. Check it on the other device.";
	}
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

std::string webLink(CodeKind k, const std::string& code) { return std::string(kWebBase) + "#" + kindLetter(k) + "=" + code; }
std::string appLink(CodeKind k, const std::string& code) { return std::string(kAppBase) + "#" + kindLetter(k) + "=" + code; }

// ---- keys (1.3) -----------------------------------------------------------------------------

Bytes hkdf(Crypto& cr, const Bytes& ikm, const std::string& info, size_t len) {
	return clsync::hkdfSalted(cr, kSalt, ikm.data(), ikm.size(), info, len);
}

bool pbkdf2Loop(Crypto& cr, const uint8_t* pw, size_t pwLen, const uint8_t* salt, size_t saltLen, uint32_t rounds, uint8_t out[32]) {
	if (rounds < 1) return false;
	Bytes first(salt, salt + saltLen);
	first.insert(first.end(), { 0, 0, 0, 1 });   // block 1: 32 bytes is all we need
	uint8_t u[32], next[32];
	cr.hmacSha256(pw, pwLen, first.data(), first.size(), u);
	memcpy(out, u, 32);
	for (uint32_t i = 1; i < rounds; i++) {
		cr.hmacSha256(pw, pwLen, u, 32, next);
		memcpy(u, next, 32);
		for (int j = 0; j < 32; j++) out[j] ^= u[j];
	}
	memset(u, 0, 32);
	memset(next, 0, 32);
	return true;
}

bool Curve::pbkdf2Sha256(Crypto& cr, const uint8_t* pw, size_t pwLen, const uint8_t* salt, size_t saltLen, uint32_t rounds,
                         uint8_t out[32]) {
	return pbkdf2Loop(cr, pw, pwLen, salt, saltLen, rounds, out);
}

TeacherKeys teacherKeys(Crypto& cr, const Bytes& secret) {
	TeacherKeys k;
	if (secret.size() != 16) return k;
	k.classId = hex(hkdf(cr, secret, "class-id", 16));
	k.teacherToken = b64u(hkdf(cr, secret, "teacher-token", 32));
	k.teacherHash = sha256Hex(cr, k.teacherToken);
	k.deleteToken = b64u(hkdf(cr, secret, "delete-token", 32));
	k.deleteHash = sha256Hex(cr, k.deleteToken);
	k.backupKey = hkdf(cr, secret, "backup-key", 32);
	return k;
}

JoinKeys joinKeys(Crypto& cr, Curve& curve, const Bytes& secret) {
	JoinKeys k;
	if (secret.size() != 6) return k;
	uint8_t s[32];
	if (!curve.pbkdf2Sha256(cr, secret.data(), secret.size(), (const uint8_t*)kJoinSalt, strlen(kJoinSalt), kJoinRounds, s)) return k;
	k.stretched.assign(s, s + 32);
	memset(s, 0, sizeof s);
	k.joinId = hex(hkdf(cr, k.stretched, "join-id", 16));
	k.joinToken = b64u(hkdf(cr, k.stretched, "join-token", 32));
	k.joinKey = hkdf(cr, k.stretched, "join-key", 32);
	return k;
}

MoveKeys moveKeys(Crypto& cr, const Bytes& secret) {
	MoveKeys k;
	if (secret.size() != 16) return k;
	k.moveId = hex(hkdf(cr, secret, "move-id", 16));
	k.moveKey = hkdf(cr, secret, "move-key", 32);
	return k;
}

std::string hmacHex(Crypto& cr, const Bytes& key, const std::string& text) {
	uint8_t out[32];
	cr.hmacSha256(key.data(), key.size(), (const uint8_t*)text.data(), text.size(), out);
	return hex(out, 32);
}

// ---- envelopes (1.5) ------------------------------------------------------------------------

int envelopeOf(const std::string& kind) {
	for (const char* k : { "teacher", "join", "info", "assignment", "live", "move", "classroom", "item", "membership" })
		if (kind == k) return 1;
	for (const char* k : { "name", "submission", "answer", "key" })
		if (kind == k) return 2;
	return 0;
}

std::string aadText(const std::string& kind, const std::string& classId, const std::string& id, int64_t ver, int flags) {
	return "cedarlogic-classroom/1|" + kind + "|" + classId + "|" + id + "|" + std::to_string(ver) + "|" + std::to_string(flags);
}

namespace {

// The plaintext: deflated when asked (and when the deflate worked).
Bytes plainOf(Crypto& cr, const std::string& payload, bool compress, const Bytes& deflated, int& flags) {
	Bytes plain(payload.begin(), payload.end());
	flags = 0;
	if (!deflated.empty()) {
		flags = 1;
		return deflated;
	}
	if (compress) {
		Bytes z;
		if (cr.deflateRaw(plain, z)) {
			plain.swap(z);
			flags = 1;
		}
	}
	return plain;
}

bool gcm(Crypto& cr, const Bytes& key, const uint8_t nonce[12], const std::string& aad, const Bytes& plain, Bytes& ct) {
	return key.size() == 32 && cr.aesGcmSeal(key.data(), nonce, Bytes(aad.begin(), aad.end()), plain, ct) &&
	       ct.size() == plain.size() + 16;
}

bool sealSym(Crypto& cr, const Bytes& key, const std::string& kind, const std::string& classId, const std::string& id, int64_t ver,
             const std::string& payload, bool compress, const Bytes& deflated, const uint8_t nonce[12], Bytes& env) {
	int flags = 0;
	const Bytes plain = plainOf(cr, payload, compress, deflated, flags);
	Bytes ct;
	if (!gcm(cr, key, nonce, aadText(kind, classId, id, ver, flags), plain, ct)) return false;
	env.clear();
	env.push_back(1);
	env.push_back((uint8_t)flags);
	env.insert(env.end(), nonce, nonce + 12);
	env.insert(env.end(), ct.begin(), ct.end());
	return true;
}

Bytes sealKeyOf(Crypto& cr, const uint8_t x[32], const uint8_t* e, const uint8_t* r) {
	std::string info = "seal-key";
	info.append((const char*)e, 65);
	info.append((const char*)r, 65);
	return clsync::hkdfSalted(cr, kSalt, x, 32, info, 32);
}

bool sealPub(Crypto& cr, Curve& curve, const Bytes& pub, const uint8_t ed[32], const std::string& kind, const std::string& classId,
             const std::string& id, int64_t ver, const std::string& payload, bool compress, const Bytes& deflated,
             const uint8_t nonce[12], Bytes& env, std::string& why) {
	if (pub.size() != 65 || pub[0] != 4) { why = "point"; return false; }
	uint8_t E[65], x[32];
	if (!curve.p256Public(ed, E)) { why = "point"; return false; }
	if (!curve.p256Ecdh(ed, pub.data(), x)) { why = "point"; return false; }
	Bytes key = sealKeyOf(cr, x, E, pub.data());
	memset(x, 0, sizeof x);
	int flags = 0;
	const Bytes plain = plainOf(cr, payload, compress, deflated, flags);
	Bytes ct;
	const bool ok = gcm(cr, key, nonce, aadText(kind, classId, id, ver, flags), plain, ct);
	std::fill(key.begin(), key.end(), 0);
	if (!ok) { why = "cipher"; return false; }
	env.clear();
	env.push_back(2);
	env.push_back((uint8_t)flags);
	env.insert(env.end(), E, E + 65);
	env.insert(env.end(), nonce, nonce + 12);
	env.insert(env.end(), ct.begin(), ct.end());
	return true;
}

}  // namespace

bool seal(Crypto& cr, const Bytes& key, const std::string& kind, const std::string& classId, const std::string& id, int64_t ver,
          const std::string& payload, bool compress, size_t maxEnvelope, Bytes& env, std::string& why) {
	env.clear();
	if (envelopeOf(kind) != 1 || ver < 1 || ver > clsync::kMaxSafeInt) { why = "bad record"; return false; }
	uint8_t nonce[12];
	if (!cr.random(nonce, 12)) { why = "rng"; return false; }   // never a zeroed nonce
	if (!sealSym(cr, key, kind, classId, id, ver, payload, compress, Bytes(), nonce, env)) { why = "cipher"; return false; }
	if (env.size() > maxEnvelope) { why = "too big"; env.clear(); return false; }
	return true;
}

bool sealTo(Crypto& cr, Curve& curve, const Bytes& pub, const std::string& kind, const std::string& classId, const std::string& id,
            int64_t ver, const std::string& payload, bool compress, size_t maxEnvelope, Bytes& env, std::string& why) {
	env.clear();
	if (envelopeOf(kind) != 2 || ver < 1 || ver > clsync::kMaxSafeInt) { why = "bad record"; return false; }
	uint8_t nonce[12], ed[32], E[65];
	if (!cr.random(nonce, 12)) { why = "rng"; return false; }
	if (!curve.p256Generate(ed, E)) { why = "rng"; return false; }   // a fresh ephemeral key, forgotten below
	const bool ok = sealPub(cr, curve, pub, ed, kind, classId, id, ver, payload, compress, Bytes(), nonce, env, why);
	memset(ed, 0, sizeof ed);
	if (!ok) { env.clear(); return false; }
	if (env.size() > maxEnvelope) { why = "too big"; env.clear(); return false; }
	return true;
}

bool sealForTest(Crypto& cr, const Bytes& key, const std::string& kind, const std::string& classId, const std::string& id,
                 int64_t ver, const std::string& payload, int flags, const Bytes& nonce, const Bytes& deflated, Bytes& env) {
	if (nonce.size() != 12) return false;
	return sealSym(cr, key, kind, classId, id, ver, payload, flags & 1, (flags & 1) ? deflated : Bytes(), nonce.data(), env);
}

bool sealToForTest(Crypto& cr, Curve& curve, const Bytes& pub, const Bytes& ephemeralD, const std::string& kind,
                   const std::string& classId, const std::string& id, int64_t ver, const std::string& payload, int flags,
                   const Bytes& nonce, const Bytes& deflated, Bytes& env) {
	if (nonce.size() != 12 || ephemeralD.size() != 32) return false;
	std::string why;
	return sealPub(cr, curve, pub, ephemeralD.data(), kind, classId, id, ver, payload, flags & 1, (flags & 1) ? deflated : Bytes(),
	               nonce.data(), env, why);
}

bool validPoint(Curve& curve, const Bytes& pub) {
	if (pub.size() != 65 || pub[0] != 4) return false;
	// The point is on the curve if an ECDH with it works (every platform's import checks).
	uint8_t one[32] = {}, x[32];
	one[31] = 1;
	return curve.p256Ecdh(one, pub.data(), x);
}

bool open(Crypto& cr, Curve& curve, const Bytes& env, const std::string& kind, const std::string& classId, const std::string& id,
          int64_t ver, const OpenKey& k, std::string& payload, std::string& why) {
	payload.clear();
	const int want = envelopeOf(kind);
	if (env.empty() || want == 0 || env[0] != want) { why = "version"; return false; }   // first, always
	if (env.size() < 2 || (env[1] & ~1)) { why = "format"; return false; }
	const int flags = env[1];
	const std::string aad = aadText(kind, classId, id, ver, flags);
	Bytes plain;
	if (want == 1) {
		if (env.size() < 30) { why = "short"; return false; }
		if (k.key.size() != 32 ||
		    !cr.aesGcmOpen(k.key.data(), env.data() + 2, Bytes(aad.begin(), aad.end()), Bytes(env.begin() + 14, env.end()), plain)) {
			why = "tag";
			return false;
		}
	} else {
		if (env.size() < 95 || env[2] != 4) { why = "short"; return false; }
		if (k.d.size() != 32 || k.pub.size() != 65) { why = "point"; return false; }
		uint8_t x[32];
		if (!curve.p256Ecdh(k.d.data(), env.data() + 2, x)) { why = "point"; return false; }
		Bytes key = sealKeyOf(cr, x, env.data() + 2, k.pub.data());
		memset(x, 0, sizeof x);
		const bool ok =
			cr.aesGcmOpen(key.data(), env.data() + 67, Bytes(aad.begin(), aad.end()), Bytes(env.begin() + 79, env.end()), plain);
		std::fill(key.begin(), key.end(), 0);
		if (!ok) { why = "tag"; return false; }
	}
	if (flags & 1) {
		Bytes inflated;
		if (!cr.inflateRaw(plain, kMaxPlaintext, inflated) || inflated.size() > kMaxPlaintext) { why = "inflate"; return false; }
		plain.swap(inflated);
	}
	payload.assign(plain.begin(), plain.end());
	return true;
}

// ---- payloads (2.2) -----------------------------------------------------------------------

namespace {

bool isStr(const json::Value* v) { return v && v->isString(); }
bool isInt(const json::Value* v) { return v && v->isInt(); }
bool isBool(const json::Value* v) { return v && v->isBool(); }
bool isB64(const json::Value* v, size_t n) {
	if (!isStr(v) || v->s.size() != (n * 4 + 2) / 3) return false;
	for (char c : v->s)
		if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
	return true;
}
bool isCode(const json::Value* v, size_t n) {
	if (!isStr(v) || v->s.size() != n) return false;
	for (char c : v->s)
		if (alphaIndex(c) < 0) return false;
	return true;
}
bool isUuidV(const json::Value* v) { return isStr(v) && isUuid(v->s); }
bool optStr(const json::Value* v) { return !v || v->isString(); }

bool fields(const json::Value& p, const std::string& kind) {
	auto g = [&](const char* k) { return p.get(k); };
	if (kind == "teacher")
		return isStr(g("name")) && isB64(g("d"), 32) && isB64(g("pub"), 65) && isB64(g("classKey"), 32) && isCode(g("joinCode"), 12) &&
		       isBool(g("joinOpen")) && isInt(g("createdAt")) && isInt(g("modifiedAt"));
	if (kind == "join") return isStr(g("name")) && isB64(g("classKey"), 32) && isB64(g("pub"), 65);
	if (kind == "info") return isStr(g("name")) && isInt(g("modifiedAt"));
	if (kind == "assignment") {
		const json::Value* due = g("dueAt");
		if (!(isStr(g("title")) && isStr(g("instructions")) && due && (due->isNull() || due->isInt()) && isBool(g("closeAfterDue")) &&
		      isStr(g("cdl")) && isInt(g("createdAt")) && isInt(g("modifiedAt"))))
			return false;
		const json::Value* key = g("key");
		if (!key) return false;
		if (key->isNull()) return true;
		return key->isObject() && (isStr(key->get("text")) != isStr(key->get("sealed"))) && optStr(key->get("names"));
	}
	if (kind == "live") {
		const json::Value* ended = g("ended");
		if (!(isUuidV(g("session")) && isInt(g("at")) && (!ended || ended->isBool()))) return false;
		if (ended && ended->b) return true;
		const json::Value* pr = g("predict");
		if (!(isStr(g("cdl")) && isInt(g("step")) && isBool(g("reveal")) && pr)) return false;
		if (pr->isNull()) return true;
		if (!(pr->isObject() && isStr(pr->get("prompt")))) return false;
		const json::Value* l = pr->get("lights");
		if (!l || !l->isArray()) return false;
		for (const json::Value& x : l->a)
			if (!x.isString()) return false;
		return true;
	}
	if (kind == "name") return isStr(g("name")) && isInt(g("joinedAt")) && isB64(g("proof"), 32);
	if (kind == "submission")
		return isStr(g("name")) && isStr(g("cdl")) && isInt(g("handedInAt")) && isInt(g("attempt")) && isB64(g("proof"), 32);
	if (kind == "answer") {
		const json::Value* l = g("lights");
		if (!(isStr(g("session")) && isInt(g("ver")) && l && l->isObject() && isInt(g("at")) && isB64(g("proof"), 32))) return false;
		for (const auto& kv : l->o)
			if (!(kv.second.isNumber() && (kv.second.n == 0 || kv.second.n == 1))) return false;
		return true;
	}
	if (kind == "key") return isStr(g("text")) && optStr(g("names"));
	if (kind == "move") {
		const json::Value* c = g("classId");
		return isStr(c) && isHex(c->s, 32) && isUuidV(g("studentId")) && isB64(g("token"), 32) && isB64(g("proof"), 32) &&
		       isB64(g("classKey"), 32) && isB64(g("pub"), 65) && isStr(g("name")) && isStr(g("className"));
	}
	if (kind == "classroom") {
		const json::Value* c = g("classId");
		return isStr(c) && isHex(c->s, 32) && isCode(g("teacherKey"), 28) && isStr(g("name")) && isInt(g("createdAt")) &&
		       isInt(g("modifiedAt"));
	}
	if (kind == "item")   // v2 (3.16.2)
		return isStr(g("type")) && isStr(g("title")) && isStr(g("topic")) && isStr(g("note")) && isStr(g("cdl")) && isInt(g("createdAt")) &&
		       isInt(g("modifiedAt"));
	if (kind == "membership") {   // v2 (3.16.7): a move record plus joinedAt
		const json::Value* c = g("classId");
		return isStr(c) && isHex(c->s, 32) && isUuidV(g("studentId")) && isB64(g("token"), 32) && isB64(g("proof"), 32) &&
		       isB64(g("classKey"), 32) && isB64(g("pub"), 65) && isStr(g("name")) && isStr(g("className")) && isInt(g("joinedAt"));
	}
	return false;
}

const char* const kKnown[] = { "teacher", "join", "info", "assignment", "live", "name", "submission", "answer", "key", "move", "classroom", "item", "membership" };

}  // namespace

std::string readPayload(const std::string& bytes, const std::string& expectedKind, json::Value& out) {
	out = json::Value();
	json::Value p;
	// UTF-8, no unpaired surrogate escape (the reader refuses both), JSON, an object.
	if (!clsync::validUtf8(bytes) || !json::parse(bytes, p) || !p.isObject()) return "invalid";
	const json::Value* v = p.get("v");
	if (!v || !v->isInt() || v->n < 1) return "invalid";
	if (v->n > 1) return "newer";
	const json::Value* k = p.get("kind");
	if (!k || !k->isString()) return "invalid";
	bool known = false;
	for (const char* x : kKnown) known = known || k->s == x;
	if (!known) return "newer";
	if (!expectedKind.empty() && k->s != expectedKind) return "invalid";
	if (!fields(p, k->s)) return "invalid";
	out = std::move(p);
	return std::string();
}

std::string cleanName(const std::string& s, size_t max, const std::string& empty) {
	std::string kept;
	for (size_t i = 0; i < s.size();) {
		const size_t n = scalarLen(s, i);
		const unsigned char c = (unsigned char)s[i];
		const bool valid = n > 1 ? clsync::validUtf8(s.substr(i, n)) : c < 0x80;
		if (valid && !(n == 1 && (c < 0x20 || c == 0x7F))) kept.append(s, i, n);
		i += n;
	}
	std::string out = cutText(kept, max);
	return out.empty() ? empty : out;
}

std::string cutText(const std::string& s0, size_t max) {
	const std::string s = clsync::trimAscii(s0);
	std::string out;
	size_t count = 0;
	for (size_t i = 0; i < s.size() && count < max; count++) {
		const size_t n = scalarLen(s, i);
		out.append(s, i, n);
		i += n;
	}
	return clsync::trimAscii(out);
}

namespace {
std::string q(const std::string& s) { return json::quote(s); }
std::string num(int64_t n) { return std::to_string(n < 0 ? 0 : n); }
std::string boolText(bool b) { return b ? "true" : "false"; }
}  // namespace

std::string teacherJson(const TeacherRec& t) {
	return "{\"v\":1,\"kind\":\"teacher\",\"name\":" + q(t.name) + ",\"d\":" + q(t.d) + ",\"pub\":" + q(t.pub) +
	       ",\"classKey\":" + q(t.classKey) + ",\"joinCode\":" + q(t.joinCode) + ",\"joinOpen\":" + boolText(t.joinOpen) +
	       ",\"createdAt\":" + num(t.createdAt) + ",\"modifiedAt\":" + num(t.modifiedAt) + "}";
}

bool teacherFrom(const json::Value& p, TeacherRec& t) {
	if (p.str("kind") != "teacher") return false;
	t.name = cleanName(p.str("name"), 100, "Untitled class");
	t.d = p.str("d");
	t.pub = p.str("pub");
	t.classKey = p.str("classKey");
	t.joinCode = p.str("joinCode");
	t.joinOpen = p.flag("joinOpen", true);
	t.createdAt = p.integer("createdAt");
	t.modifiedAt = p.integer("modifiedAt");
	return true;
}

std::string joinJson(const std::string& name, const std::string& classKey, const std::string& pub) {
	return "{\"v\":1,\"kind\":\"join\",\"name\":" + q(name) + ",\"classKey\":" + q(classKey) + ",\"pub\":" + q(pub) + "}";
}

std::string infoJson(const std::string& name, int64_t modifiedAt) {
	return "{\"v\":1,\"kind\":\"info\",\"name\":" + q(name) + ",\"modifiedAt\":" + num(modifiedAt) + "}";
}

std::string assignmentJson(const AssignmentRec& a) {
	std::string key = "null";
	if (a.keyMode == 1) {
		key = "{\"text\":" + q(a.keyText);
		if (!a.keyNames.empty()) key += ",\"names\":" + q(a.keyNames);
		key += "}";
	} else if (a.keyMode == 2) {
		key = "{\"sealed\":" + q(a.keySealed) + "}";
	}
	return "{\"v\":1,\"kind\":\"assignment\",\"title\":" + q(a.title) + ",\"instructions\":" + q(a.instructions) +
	       ",\"dueAt\":" + (a.dueAt < 0 ? std::string("null") : num(a.dueAt)) + ",\"closeAfterDue\":" + boolText(a.closeAfterDue) +
	       ",\"cdl\":" + q(a.cdl) + ",\"key\":" + key + ",\"createdAt\":" + num(a.createdAt) + ",\"modifiedAt\":" + num(a.modifiedAt) +
	       "}";
}

bool assignmentFrom(const json::Value& p, AssignmentRec& a) {
	a = AssignmentRec();
	a.title = cutText(p.str("title"), 200);
	a.instructions = cutText(p.str("instructions"), 20000);
	a.cdl = p.str("cdl");
	const json::Value* due = p.get("dueAt");
	a.dueAt = due && due->isInt() ? due->i() : -1;
	a.closeAfterDue = p.flag("closeAfterDue");
	if (const json::Value* k = p.get("key")) {
		if (k->isObject()) {
			if (const json::Value* t = k->get("text"); t && t->isString()) {
				a.keyMode = 1;
				a.keyText = t->s;
			} else {
				a.keyMode = 2;
				a.keySealed = k->str("sealed");
			}
			a.keyNames = k->str("names");
		}
	}
	a.createdAt = p.integer("createdAt");
	a.modifiedAt = p.integer("modifiedAt");
	return true;
}

std::string keyJson(const std::string& text, const std::string& names) {
	std::string o = "{\"v\":1,\"kind\":\"key\",\"text\":" + q(text);
	if (!names.empty()) o += ",\"names\":" + q(names);
	return o + "}";
}

std::string liveJson(const LiveRec& l) {
	if (l.ended) return "{\"v\":1,\"kind\":\"live\",\"session\":" + q(l.session) + ",\"ended\":true,\"at\":" + num(l.at) + "}";
	std::string predict = "null";
	if (l.hasPredict) {
		predict = "{\"prompt\":" + q(l.prompt) + ",\"lights\":[";
		for (size_t i = 0; i < l.lights.size(); i++) predict += (i ? "," : "") + q(l.lights[i]);
		predict += "]}";
	}
	return "{\"v\":1,\"kind\":\"live\",\"session\":" + q(l.session) + ",\"step\":" + num(l.step) + ",\"cdl\":" + q(l.cdl) +
	       ",\"predict\":" + predict + ",\"reveal\":" + boolText(l.reveal) + ",\"at\":" + num(l.at) + "}";
}

bool liveFrom(const json::Value& p, LiveRec& l) {
	l = LiveRec();
	l.session = p.str("session");
	l.at = p.integer("at");
	l.ended = p.flag("ended");
	if (l.ended) return true;
	l.step = (int)p.integer("step");
	l.cdl = p.str("cdl");
	l.reveal = p.flag("reveal");
	if (const json::Value* pr = p.get("predict"); pr && pr->isObject()) {
		l.hasPredict = true;
		l.prompt = cutText(pr->str("prompt"), 500);
		if (const json::Value* ls = pr->get("lights"))
			for (const json::Value& x : ls->a)
				if (l.lights.size() < 32) l.lights.push_back(x.s);
	}
	return true;
}

std::string nameJson(const std::string& name, int64_t joinedAt, const std::string& proof) {
	return "{\"v\":1,\"kind\":\"name\",\"name\":" + q(name) + ",\"joinedAt\":" + num(joinedAt) + ",\"proof\":" + q(proof) + "}";
}

std::string submissionJson(const std::string& name, const std::string& cdl, int64_t handedInAt, const std::string& client,
                           int attempt, const std::string& proof) {
	return "{\"v\":1,\"kind\":\"submission\",\"name\":" + q(name) + ",\"cdl\":" + q(cdl) + ",\"handedInAt\":" + num(handedInAt) +
	       ",\"client\":" + q(client) + ",\"attempt\":" + num(attempt) + ",\"proof\":" + q(proof) + "}";
}

std::string answerJson(const std::string& session, int64_t ver, const std::map<std::string, int>& lights, int64_t at,
                       const std::string& proof) {
	std::string l = "{";
	bool first = true;
	for (const auto& kv : lights) {
		l += (first ? "" : ",") + q(kv.first) + ":" + (kv.second ? "1" : "0");
		first = false;
	}
	l += "}";
	return "{\"v\":1,\"kind\":\"answer\",\"session\":" + q(session) + ",\"ver\":" + num(ver) + ",\"lights\":" + l + ",\"at\":" + num(at) +
	       ",\"proof\":" + q(proof) + "}";
}

std::string moveJson(const MoveRec& m) {
	return "{\"v\":1,\"kind\":\"move\",\"classId\":" + q(m.classId) + ",\"studentId\":" + q(m.studentId) + ",\"token\":" + q(m.token) +
	       ",\"proof\":" + q(m.proof) + ",\"classKey\":" + q(m.classKey) + ",\"pub\":" + q(m.pub) + ",\"name\":" + q(m.name) +
	       ",\"className\":" + q(m.className) + "}";
}

bool moveFrom(const json::Value& p, MoveRec& m) {
	m.classId = p.str("classId");
	m.studentId = p.str("studentId");
	m.token = p.str("token");
	m.proof = p.str("proof");
	m.classKey = p.str("classKey");
	m.pub = p.str("pub");
	m.name = cleanName(p.str("name"), 64, "a student");
	m.className = cleanName(p.str("className"), 100, "Untitled class");
	return true;
}

std::string classroomJson(const std::string& classId, const std::string& teacherKey, const std::string& name, int64_t createdAt,
                          int64_t modifiedAt, const std::string& device, const std::string& deviceId) {
	return "{\"v\":1,\"kind\":\"classroom\",\"classId\":" + q(classId) + ",\"teacherKey\":" + q(teacherKey) + ",\"name\":" + q(name) +
	       ",\"createdAt\":" + num(createdAt) + ",\"modifiedAt\":" + num(modifiedAt) + ",\"device\":" + q(device) +
	       ",\"deviceId\":" + q(deviceId) + "}";
}

std::string itemJson(const ItemRec& a) {
	return "{\"v\":1,\"kind\":\"item\",\"type\":" + q(a.type == "share" ? "share" : "example") + ",\"title\":" + q(cleanName(a.title, 200, "Untitled")) +
	       ",\"topic\":" + q(cutText(a.topic, 100)) + ",\"note\":" + q(cutText(a.note, 20000)) + ",\"cdl\":" + q(a.cdl) +
	       ",\"createdAt\":" + num(a.createdAt) + ",\"modifiedAt\":" + num(a.modifiedAt) + "}";
}

bool itemFrom(const json::Value& p, ItemRec& a) {
	a.type = p.str("type") == "share" ? "share" : "example";
	a.title = cleanName(p.str("title"), 200, "Untitled");
	a.topic = cutText(p.str("topic"), 100);
	a.note = cutText(p.str("note"), 20000);
	a.cdl = p.str("cdl");
	a.createdAt = p.integer("createdAt");
	a.modifiedAt = p.integer("modifiedAt");
	return true;
}

std::string membershipJson(const MoveRec& m, int64_t joinedAt) {
	return "{\"v\":1,\"kind\":\"membership\",\"classId\":" + q(m.classId) + ",\"studentId\":" + q(m.studentId) + ",\"token\":" + q(m.token) +
	       ",\"proof\":" + q(m.proof) + ",\"classKey\":" + q(m.classKey) + ",\"pub\":" + q(m.pub) + ",\"name\":" + q(m.name) +
	       ",\"className\":" + q(m.className) + ",\"joinedAt\":" + num(joinedAt) + "}";
}

// ---- the parts budget (4.9) ------------------------------------------------------------------

void countParts(const std::string& cdl, int& gates, int& segments) {
	gates = segments = 0;
	auto count = [&](const char* what) {
		int n = 0;
		const size_t len = strlen(what);
		for (size_t at = cdl.find(what); at != std::string::npos; at = cdl.find(what, at + len)) n++;
		return n;
	};
	// v3 (s-expressions) and v1/v2 (XML) both: whichever the text is.
	gates = count("(gate ") + count("<gate>");
	segments = count("(seg ") + count("<hsegment>") + count("<vsegment>");
}

bool tooBig(const std::string& cdl) {
	int g = 0, s = 0;
	countParts(cdl, g, s);
	return g > kMaxGates || s > kMaxSegments;
}

}  // namespace clclass
