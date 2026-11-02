// Adding a device by scanning (SYNC.md 11): the pairing secret P and its link,
// the keys, the sealed hello and answer, and D's requests to the website. The
// polling loop itself runs on the engine thread (SyncEngine.cpp). Checked by
// the vectors of SYNC.md 11.11 (SyncTestPair.cpp).

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS   // snprintf/sscanf/localtime/getenv are used with care
#endif

#include "SyncInternal.h"

#include <atomic>
#include <cstring>

namespace clsync {

const char* const kPairSalt = "cedarlogic-pair-v1";

namespace {

std::atomic<int64_t> gPollMs{ 3 * kSecond }, gLifeMs{ 10 * kMinute };

bool linkChar(unsigned char c) {
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' || c == ' ';
}

// Decodes one UTF-8 sequence at s[i] (the text is valid UTF-8: the JSON reader checked it).
uint32_t nextScalar(const std::string& s, size_t& i) {
	const unsigned char c = (unsigned char)s[i];
	int n = c < 0x80 ? 0 : c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
	uint32_t cp = n == 0 ? c : n == 1 ? (c & 31u) : n == 2 ? (c & 15u) : (c & 7u);
	i++;
	for (; n > 0 && i < s.size(); n--, i++) cp = (cp << 6) | ((unsigned char)s[i] & 63u);
	return cp;
}

std::vector<std::pair<std::string, std::string>> headers(const PairServer& srv, const std::string& readToken, bool body) {
	std::vector<std::pair<std::string, std::string>> h;
	if (!readToken.empty()) h.emplace_back("authorization", "Bearer " + readToken);
	if (!srv.client.empty()) h.emplace_back("x-cedarlogic-client", srv.client);
	if (!srv.appKey.empty()) h.emplace_back("x-cedarlogic-key", srv.appKey);
	if (body) h.emplace_back("content-type", "application/json");
	return h;
}

HttpResponse send(const PairServer& srv, const std::string& method, const std::string& path, const std::string& readToken,
                  const std::string& body) {
	HttpRequest r;
	r.method = method;
	r.url = srv.serverBase + path;
	r.headers = headers(srv, readToken, !body.empty());
	r.body = body;
	if (!srv.http) return HttpResponse();
	return srv.http(r);
}

bool sealWithNonce(Crypto& cr, const PairKeys& k, const std::string& kind, const std::string& json, const uint8_t nonce[12],
                   std::string& env) {
	env.clear();
	if (!k.valid() || (kind != "hello" && kind != "answer")) return false;
	const std::string aad = pairAad(kind, k.pairId);
	Bytes ct;
	if (!cr.aesGcmSeal(k.key.data(), nonce, Bytes(aad.begin(), aad.end()), Bytes(json.begin(), json.end()), ct)) return false;
	Bytes out;
	out.reserve(13 + ct.size());
	out.push_back(1);
	out.insert(out.end(), nonce, nonce + 12);
	out.insert(out.end(), ct.begin(), ct.end());
	if (out.size() > kMaxPairEnvelope) return false;
	env = b64u(out);
	return true;
}

}  // namespace

int64_t pairPollMs() { return gPollMs.load(); }
int64_t pairLifeMs() { return gLifeMs.load(); }

void setPairTimingForTest(int64_t pollMs, int64_t lifeMs) {
	gPollMs = pollMs > 0 ? pollMs : 3 * kSecond;
	gLifeMs = lifeMs > 0 ? lifeMs : 10 * kMinute;
}

// ---- P, the link, the keys (11.2, 11.3) -------------------------------------------------------

PairKeys pairKeys(Crypto& cr, const uint8_t secret[16]) {
	PairKeys k;
	k.pairId = hex(hkdfSalted(cr, kPairSalt, secret, 16, "pair-id", 16));
	k.key = hkdfSalted(cr, kPairSalt, secret, 16, "pair-key", 32);
	return k;
}

std::string pairLink(const std::string& pairingCode) { return std::string(kWebBase) + "#p=" + pairingCode; }

bool parsePairLink(Crypto& cr, const std::string& text, uint8_t secret[16]) {
	if (text.find("#k=") != std::string::npos) return false;
	const size_t at = text.find("#p=");
	if (at == std::string::npos) return false;
	size_t end = at + 3;
	while (end < text.size() && linkChar((unsigned char)text[end])) end++;
	std::string why;
	return decodeCode(cr, text.substr(at + 3, end - at - 3), secret, why);
}

std::string readTokenOf(const Bytes& r) { return b64u(r); }

std::string readHashOf(Crypto& cr, const std::string& readToken) { return sha256Hex(cr, readToken); }

// ---- the two messages (11.4) ---------------------------------------------------------------------

std::string pairHelloJson(const std::string& device) {
	return "{\"v\":1,\"kind\":\"hello\",\"device\":" + json::quote(device) + "}";
}

std::string pairAnswerJson(const std::string& code, const std::string& device) {
	return "{\"v\":1,\"kind\":\"answer\",\"code\":" + json::quote(code) + ",\"device\":" + json::quote(device) + "}";
}

std::string pairAad(const std::string& kind, const std::string& pairId) { return std::string(kPairSalt) + "|" + kind + "|" + pairId; }

bool sealPair(Crypto& cr, const PairKeys& k, const std::string& kind, const std::string& json, std::string& env) {
	uint8_t nonce[12];
	env.clear();
	if (!cr.random(nonce, 12)) return false;   // never a zeroed nonce
	return sealWithNonce(cr, k, kind, json, nonce, env);
}

bool sealPairForTest(Crypto& cr, const PairKeys& k, const std::string& kind, const std::string& json, const Bytes& nonce,
                     std::string& env) {
	if (nonce.size() != 12) return false;
	return sealWithNonce(cr, k, kind, json, nonce.data(), env);
}

bool openPair(Crypto& cr, const PairKeys& k, const std::string& kind, const std::string& env, PairMessage& out) {
	out = PairMessage();
	Bytes b;
	if (!k.valid() || !unb64u(env, b) || b.size() < 29 || b[0] != 1) return false;
	const std::string aad = pairAad(kind, k.pairId);
	Bytes plain;
	if (!cr.aesGcmOpen(k.key.data(), b.data() + 1, Bytes(aad.begin(), aad.end()), Bytes(b.begin() + 13, b.end()), plain))
		return false;
	json::Value v;
	if (!json::parse(std::string(plain.begin(), plain.end()), v) || !v.isObject()) return false;
	const json::Value* ver = v.get("v");
	const json::Value* kd = v.get("kind");
	const json::Value* dev = v.get("device");
	if (!ver || !ver->isNumber() || ver->n != 1) return false;
	if (!kd || !kd->isString() || kd->s != kind) return false;
	if (!dev || !dev->isString()) return false;
	if (kind == "answer") {
		const json::Value* code = v.get("code");
		std::string canonical, why;
		if (!code || !code->isString() || !parseCode(cr, code->s, canonical, why)) return false;
		out.code = canonical;
	}
	out.device = dev->s;
	return true;
}

std::string pairDeviceText(const std::string& device) {
	std::string out;
	size_t count = 0;
	for (size_t i = 0; i < device.size() && count < 64;) {
		const uint32_t cp = nextScalar(device, i);
		if (cp < 0x20 || (cp >= 0x7F && cp <= 0x9F)) continue;   // control characters
		putUtf8(out, cp);
		count++;
	}
	return out.empty() ? std::string("another device") : out;
}

// ---- D's requests (11.5, 11.6) ------------------------------------------------------------------

int pairPut(const PairServer& srv, Crypto& cr, const std::string& deviceName, PairSlot& slot) {
	slot = PairSlot();
	uint8_t p[16];
	Bytes r(32);
	if (!cr.random(p, 16) || !cr.random(r.data(), r.size())) return -1;
	PairSlot s;
	s.keys = pairKeys(cr, p);
	s.readToken = readTokenOf(r);
	s.link = pairLink(encodeCode(cr, p));
	std::string hello;
	if (!sealPair(cr, s.keys, "hello", pairHelloJson(deviceName), hello)) return -1;
	json::Value body = json::Value::object();
	body.set("hello", json::Value::string(hello));
	body.set("readHash", json::Value::string(readHashOf(cr, s.readToken)));
	const HttpResponse resp = send(srv, "PUT", "/pair/" + s.keys.pairId, std::string(), json::write(body));
	if (resp.status == 201) slot = s;
	return resp.status;
}

int pairPoll(const PairServer& srv, const PairSlot& slot, std::string& answerEnv) {
	answerEnv.clear();
	const HttpResponse resp = send(srv, "GET", "/pair/" + slot.keys.pairId + "/answer", slot.readToken, std::string());
	if (resp.status != 200) return resp.status;
	json::Value v;
	if (!json::parse(resp.body, v) || !v.isObject()) return 0;   // a garbled answer from the website: try again
	const json::Value* a = v.get("answer");
	if (!a || a->isNull()) return 200;
	answerEnv = a->isString() && !a->s.empty() ? a->s : std::string("?");   // anything else: damaged
	return 200;
}

void pairDelete(const PairServer& srv, const std::string& pairId, const std::string& readToken) {
	if (pairId.empty()) return;
	send(srv, "DELETE", "/pair/" + pairId, readToken, std::string());
}

}  // namespace clsync
