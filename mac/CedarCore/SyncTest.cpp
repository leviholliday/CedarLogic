// clsync::selfTest: the vectors of SYNC.md 7.1 (from the design's
// ref/vectors.h, copied in as SyncVectors.h, and ref/fixtures as
// SyncFixtures.h), then the protocol scenarios of 7.2 (SyncTestScenarios.cpp).

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS   // getenv in a test
#endif

#include "SyncTest.h"

#include "SyncFixtures.h"
#include "SyncVectors.h"

#include <cstdlib>
#include <cstring>

namespace clsync {
namespace test {

namespace {

Bytes unhexOrEmpty(const char* s) {
	Bytes b;
	if (s) unhex(s, b);
	return b;
}

}  // namespace

GateDefaults vectorDefaults() {
	static const std::map<std::string, std::string>* table = [] {
		auto* m = new std::map<std::string, std::string>();
		for (const GateDefault& d : kGateDefaults)
			(*m)[std::string(d.lib) + "\x01" + (d.gui ? "g" : "l") + "\x01" + d.name] = d.value;
		return m;
	}();
	return [](const std::string& lib, bool gui, const std::string& name) -> std::string {
		auto it = table->find(lib + "\x01" + (gui ? "g" : "l") + "\x01" + name);
		return it == table->end() ? std::string("\x01") : it->second;
	};
}

std::vector<std::tuple<bool, std::string, std::string>> gateDefaultList(const std::string& lib) {
	std::vector<std::tuple<bool, std::string, std::string>> out;
	for (const GateDefault& d : kGateDefaults)
		if (lib == d.lib) out.emplace_back(d.gui, d.name, d.value);
	return out;
}

std::string fixture(const std::string& file) {
	for (const SyncFixture& f : kSyncFixtures) {
		if (file != f.file) continue;
		std::string out;
		for (const char* const* l = f.lines; *l; l++) out += *l;
		return out;
	}
	return std::string();
}

void vectorTests(Crypto& cr, Report& r) {
	// 7.1.1 and 7.1.3: codes and keys.
	for (const CodeVec& v : kCodeVecs) {
		const std::string L = v.label;
		Bytes secret;
		unhex(v.secretHex, secret);
		const std::string code = encodeCode(cr, secret.data());
		r.line(code == v.code, "code " + L);
		r.line(groupCode(code) == v.grouped, "grouped " + L);
		r.line(webLink(code) == v.webLink && appLink(code) == v.appLink, "links " + L);
		uint8_t back[16];
		std::string why, canon;
		r.line(decodeCode(cr, v.grouped, back, why) && memcmp(back, secret.data(), 16) == 0, "decode " + L);
		r.line(parseCode(cr, v.webLink, canon, why) && canon == v.code, "parse link " + L);
		const Keys k = deriveKeys(cr, secret.data());
		r.line(k.spaceId == v.spaceId, "spaceId " + L);
		r.line(k.authToken == v.authToken, "authToken " + L);
		r.line(k.authHash == v.authHash, "authHash " + L);
		r.line(k.deleteToken == v.deleteToken, "deleteToken " + L);
		r.line(k.deleteHash == v.deleteHash, "deleteHash " + L);
		r.line(hex(k.recordKey) == v.recordKeyHex, "recordKey " + L);
	}
	// 7.1.2: parsing what people type.
	for (const ParseVec& p : kParseVecs) {
		std::string code, why, got;
		got = parseCode(cr, p.input, code, why) ? code : "error:" + why;
		r.line(got == p.expect, std::string("parse ") + p.label, got);
		if (got.compare(0, 6, "error:") == 0) r.line(!whyText(why, p.input).empty(), std::string("why text ") + p.label);
	}
	r.line(whyText("length", "000G40R40M30E209185GR38E1YZ") == "A sync code has 28 letters and digits; this has 27.",
	       "why text: length", whyText("length", "000G40R40M30E209185GR38E1YZ"));
	r.line(whyText("symbol", "U00G40R40M30E209185GR38E1YZ4") == "\xE2\x80\x98U\xE2\x80\x99 can't be in a sync code.",
	       "why text: symbol", whyText("symbol", "U00G40R40M30E209185GR38E1YZ4"));
	{
		Crypto* c = &cr;
		TestCrypto failing(*c);
		failing.failRandom = true;
		r.line(newCode(failing).empty(), "no code when the RNG fails");
	}
	// 7.1.4: text and hashes.
	r.line(nameHash(cr, kHash_name) == kHash_nameHash, "nameHash");
	r.line(cdlHash(cr, kHash_cdl) == kHash_cdlHash, "cdlHash");
	std::string crlf = "\xEF\xBB\xBF";
	for (const char* q = kHash_cdl; *q; q++) {
		if (*q == '\n') crlf += '\r';
		crlf += *q;
	}
	r.line(cdlHash(cr, crlf) == kHash_cdlHash, "cdlHash with BOM and CRLF");
	r.line(contentHash(cr, kHash_name, kHash_cdl) == kHash_contentHash, "contentHash");
	r.line(contentHash(cr, kHash_name, crlf) == kHash_contentHash, "contentHash CRLF");
	r.line(contentHash(cr, std::string("  ") + kHash_name + "\n", kHash_cdl) == kHash_contentHash, "contentHash padded name");
	r.line(contentHash(cr, kHash_name, kHash_cdlSwitchOn) == kHash_contentHashSwitchOn, "contentHash switch on");
	for (const TextVec& t : kTextVecs) {
		Bytes b;
		unhex(t.bytesHex, b);
		r.line(hexOf(fileText(std::string(b.begin(), b.end()))) == t.textUtf8Hex, std::string("text: ") + t.label);
	}
	// 7.1.5: structure.
	const GateDefaults defaults = vectorDefaults();
	for (const StructureVec& s : kStructureVecs) {
		const std::string cdl = s.cdl ? std::string(s.cdl) : fixture(s.file);
		const std::string t = structureText(cdl, defaults);
		r.line(!cdl.empty() && sha256Hex(cr, t) == s.structureHash, std::string("structureHash: ") + s.label);
		if (s.structure) r.line(t == s.structure, std::string("structure text: ") + s.label, t);
	}
	// 7.1.6: records.
	const RecordVec& r1 = kRecordVecs[0];
	{
		Bytes env;
		r.line(sealForTest(cr, unhexOrEmpty(r1.recordKeyHex), r1.recordId, r1.ver, r1.payload, 0, unhexOrEmpty(r1.nonceHex),
		                   env) && b64u(env) == r1.data,
		       "record 1 byte-exact");
		r.line(aadText(r1.recordId, r1.ver, r1.flags) == r1.aad, "record 1 AAD");
		// The engine's own payload writer gives record 1's payload byte for byte.
		Payload p;
		readPayload(r1.payload, p);
		r.line(circuitJson(p.name, p.cdl, p.modifiedAt, p.device, p.deviceId, p.createdAt, nullptr, nullptr) == r1.payload,
		       "record 1 payload as the engine writes it");
	}
	for (const RecordVec& rv : kRecordVecs) {
		const std::string L = std::string("record ") + std::string(rv.label).substr(0, 2);
		Bytes e;
		std::string payload;
		const bool opened = unb64u(rv.data, e) && openRecord(cr, unhexOrEmpty(rv.recordKeyHex), rv.recordId, rv.ver, e, payload);
		r.line(opened && payload == rv.payload, L + " opens");
		r.line(envelopeHash(cr, e) == rv.envelopeH, L + " h");
		r.line(e.size() > 1 && e[1] == rv.flags, L + " flags");
		Payload p;
		r.line(readPayload(payload, p).empty() && p.kind == rv.kind, L + " readable");
		if (rv.nameUtf8Hex) r.line(hexOf(p.name) == rv.nameUtf8Hex, L + " name");
		if (rv.cdlUtf8Hex) r.line(hexOf(p.cdl) == rv.cdlUtf8Hex, L + " cdl");
	}
	// 7.1.7: open, but refused.
	for (const RefusedVec& t : kRefusedVecs) {
		Bytes e;
		std::string payload;
		r.line(unb64u(t.data, e) && openRecord(cr, unhexOrEmpty(r1.recordKeyHex), t.recordId, t.ver, e, payload) &&
		           hexOf(payload) == t.payloadHex,
		       std::string("refused opens: ") + t.label);
		Payload p;
		const std::string why = readPayload(payload, p);
		r.line(why == t.why, std::string("refused: ") + t.label, why);
	}
	// 7.1.8: must not open.
	for (const TamperVec& t : kTamperVecs) {
		Bytes e;
		std::string payload;
		const bool opened = unb64u(t.data, e) && openRecord(cr, unhexOrEmpty(t.recordKeyHex), t.recordId, t.ver, e, payload);
		r.line(!opened, std::string("tamper: ") + t.label);
	}
	// Fresh nonces; no seal without the RNG; the size cap.
	{
		const Bytes key = unhexOrEmpty(r1.recordKeyHex);
		Bytes e1, e2, e3;
		std::string why;
		const bool a = sealRecord(cr, key, r1.recordId, 1, "x", false, kMaxEnvelope, e1, why);
		const bool b = sealRecord(cr, key, r1.recordId, 1, "x", false, kMaxEnvelope, e2, why);
		r.line(a && b && e1 != e2, "two seals of the same payload differ");
		TestCrypto failing(cr);
		failing.failRandom = true;
		r.line(!sealRecord(failing, key, r1.recordId, 1, "x", false, kMaxEnvelope, e3, why) && why == "rng" && e3.empty(),
		       "no seal when the RNG fails");
		std::string big(kMaxEnvelope, 'a');
		r.line(!sealRecord(cr, key, r1.recordId, 1, big, false, kMaxEnvelope, e3, why) && why == "too big", "seal size cap");
		std::string payload;
		Bytes z;
		if (sealRecord(cr, key, r1.recordId, 2, r1.payload, true, kMaxEnvelope, z, why)) {
			r.line(z[1] == 1 && openRecord(cr, key, r1.recordId, 2, z, payload) && payload == r1.payload,
			       "deflated seal opens");
		} else {
			r.line(false, "deflated seal opens", why);
		}
	}
	// JSON edge cases the payload rules depend on.
	{
		json::Value v;
		r.line(json::parse("{\"a\":5e0,\"b\":5.0,\"c\":[true,false,null],\"d\":\"\\ud83d\\ude00\\/\\u2028\"}", v) &&
		           v.get("a")->isInt() && v.get("b")->isInt() && v.get("d")->s == "\xF0\x9F\x98\x80/\xE2\x80\xA8",
		       "json: escapes and numbers");
		r.line(!json::parse("{\"a\":\"\\ud83d\"}", v), "json: unpaired surrogate refused");
		r.line(!json::parse("{\"a\":\"\\udc00x\"}", v), "json: lone low surrogate refused");
		r.line(!json::parse("{\"a\":1,}", v) && !json::parse("[01]", v) && !json::parse("NaN", v) && !json::parse("\"a\tb\"", v),
		       "json: strictness");
		std::string deep;
		for (int k = 0; k < 40; k++) deep += "[";
		for (int k = 0; k < 40; k++) deep += "]";
		r.line(!json::parse(deep, v), "json: depth limit");
		r.line(json::quote(std::string("a\"\\\n\x01\xE2\x80\xA8", 8)) == "\"a\\\"\\\\\\n\\u0001\xE2\x80\xA8\"", "json: writer escapes");
		r.line(json::parse("{\"x\":1.5}", v) && !v.get("x")->isInt() && json::parse("{\"x\":9007199254740992}", v) &&
		           !v.get("x")->isInt() && json::parse("{\"x\":-1}", v) && !v.get("x")->isInt(),
		       "json: the integer rule");
	}
	// HTTP dates.
	{
		int64_t ms = 0;
		r.line(parseHttpDate("Sun, 06 Nov 1994 08:49:37 GMT", ms) && ms == 784111777000LL &&
		           formatHttpDate(784111777000LL) == "Sun, 06 Nov 1994 08:49:37 GMT",
		       "http dates");
	}
	// QR codes (SYNC.md 1.2): the https link is 67 bytes -> version 5 (37 modules) at level M.
	{
		int size = 0;
		const std::vector<bool> m = qr(webLink(kCodeVecs[0].code), size);
		bool finders = size == 37 && (int)m.size() == size * size;
		if (finders) {
			// The three finder patterns' outer rings and centres.
			auto at = [&](int x, int y) { return (bool)m[(size_t)(y * size + x)]; };
			for (int k = 0; k < 7; k++)
				finders = finders && at(k, 0) && at(0, k) && at(size - 1 - k, 0) && at(k, size - 1) && at(6, k);
			finders = finders && at(3, 3) && at(size - 4, 3) && at(3, size - 4) && !at(1, 1) && !at(7, 7);
		}
		r.line(finders, "qr: version 5 for the web link", "size " + std::to_string(size));
	}
}

}  // namespace test

bool selfTest(Crypto& crypto, const std::string& tempDir, std::string& report, Host* httpOnly, const std::string& serverBase) {
	test::Report r;
	if (const char* only = getenv("CL_SYNC_TEST_ONLY")) r.only = only;
	if (r.only.empty() || r.only == "vectors") test::vectorTests(crypto, r);
	if (r.only.empty() || r.only == "vectors" || r.only == "pair") test::pairVectorTests(crypto, r);
	if (r.only != "vectors") test::scenarioTests(crypto, tempDir, r, httpOnly, serverBase);
	r.text += std::to_string(r.passed) + " passed, " + std::to_string(r.failed) + " failed\n";
	report = r.text;
	return r.failed == 0;
}

}  // namespace clsync
