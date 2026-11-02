// Pairing's bytes (SYNC.md 11.11): the pairing code and link, pairId and
// pairKey, the read token and its hash, the hello and answer envelopes with
// fixed nonces, and what must not open. The D loop against the FakeServer is
// in SyncTestEngine.cpp.

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "SyncTest.h"

#include <cstring>

namespace clsync {
namespace test {

namespace {

Bytes counting(uint8_t from, size_t n) {
	Bytes b;
	for (size_t i = 0; i < n; i++) b.push_back((uint8_t)(from + i));
	return b;
}

}  // namespace

void pairVectorTests(Crypto& cr, Report& r) {
	const Bytes p = counting(0xa0, 16);
	const std::string code = encodeCode(cr, p.data());
	r.line(code == "M2GT58X4MPKAFA59NANTSBDENX83", "pair: pairing code");
	const std::string link = pairLink(code);
	r.line(link == "https://cedarlogic.netlify.app/sync/#p=M2GT58X4MPKAFA59NANTSBDENX83", "pair: link");
	const PairKeys k = pairKeys(cr, p.data());
	r.line(k.pairId == "2caadcde5f2aee160164bd13927d3637", "pair: pairId");
	r.line(hex(k.key) == "34ff905499658d1bd3842749d074ba1401b0816262af06abbbbc0735fedca39b", "pair: pairKey");

	const std::string token = readTokenOf(counting(0x40, 32));
	r.line(token == "QEFCQ0RFRkdISUpLTE1OT1BRUlNUVVZXWFlaW1xdXl8", "pair: read token");
	r.line(readHashOf(cr, token) == "f45dc82a7523d14974cea050e3028e8bf06a8ee855a9d63034ebb4fb9ecc2305", "pair: readHash");

	// The links: read back; what isn't one.
	{
		uint8_t back[16];
		r.line(parsePairLink(cr, link, back) && memcmp(back, p.data(), 16) == 0, "pair: link parses");
		r.line(parsePairLink(cr, "#p=m2gt-58x4-mpka-fa59-nant-sbde-nx83", back) && memcmp(back, p.data(), 16) == 0,
		       "pair: bare #p=, lower case, dashes");
		r.line(!parsePairLink(cr, "https://cedarlogic.netlify.app/sync/#k=M2GT58X4MPKAFA59NANTSBDENX83", back),
		       "pair: a #k= link is a sync code");
		r.line(!parsePairLink(cr, "M2GT58X4MPKAFA59NANTSBDENX83", back), "pair: a bare code is a sync code");
		r.line(!parsePairLink(cr, "#p=M2GT58X4MPKAFA59NANTSBDENX84", back), "pair: a typo fails the checksum");
		r.line(!parsePairLink(cr, "#p=M2GT58X4MPKAFA59NANTSBDENX8", back), "pair: 27 symbols");
		std::string c, why;
		r.line(!parseCode(cr, link, c, why), "pair: a pairing link isn't a sync code");
	}

	// The messages.
	const std::string device = "Sam\xE2\x80\x99s MacBook \"Air\"";
	const std::string hello = pairHelloJson(device);
	r.line(hello == "{\"v\":1,\"kind\":\"hello\",\"device\":\"Sam\xE2\x80\x99s MacBook \\\"Air\\\"\"}", "pair: hello JSON");
	const std::string answer = pairAnswerJson("000G40R40M30E209185GR38E1YZ4", "Chrome on Android");
	r.line(answer == "{\"v\":1,\"kind\":\"answer\",\"code\":\"000G40R40M30E209185GR38E1YZ4\",\"device\":\"Chrome on Android\"}",
	       "pair: answer JSON");
	r.line(pairHelloJson(std::string("a\\b\x01\x1f\n\t\x7f", 8)) ==
	           "{\"v\":1,\"kind\":\"hello\",\"device\":\"a\\\\b\\u0001\\u001f\\n\\t\x7f\"}",
	       "pair: escaped as JSON.stringify does");
	std::string helloEnv, answerEnv;
	r.line(sealPairForTest(cr, k, "hello", hello, counting(0x10, 12), helloEnv) &&
	           helloEnv == "ARAREhMUFRYXGBkaG0vOHTp3q9Sc5tbSN42jE2irZhIUJNlUK6Zu-bHHIWnfkTRS3yJFkRLq1uekWAlpN6N7_r-5atwh5KPKqcA_y6zkuvoLcn7mCdI",
	       "pair: hello envelope");
	r.line(sealPairForTest(cr, k, "answer", answer, counting(0x20, 12), answerEnv) &&
	           answerEnv == "ASAhIiMkJSYnKCkqK6XNj59CphJxJWWwjDJhjQBq0bFaTI6puMWrGLeOz0YQNUnuMszZCoWCMoG6nDWoj8d39E7TYfEgxBo0Q7mVazyKBMzOBRwf_CPwNfxOdNwvS5Ufp4EiL2LC6cxrjJODs2JjYJzi6r2CQ04",
	       "pair: answer envelope");
	PairMessage m;
	r.line(openPair(cr, k, "hello", helloEnv, m) && m.device == device && m.code.empty(), "pair: hello opens");
	r.line(openPair(cr, k, "answer", answerEnv, m) && m.device == "Chrome on Android" && m.code == "000G40R40M30E209185GR38E1YZ4",
	       "pair: answer opens");
	std::string fresh;
	r.line(sealPair(cr, k, "answer", answer, fresh) && fresh != answerEnv && openPair(cr, k, "answer", fresh, m), "pair: a fresh nonce");

	// Must not open.
	bool anyOpened = false;
	for (const std::string* env : { &helloEnv, &answerEnv }) {
		Bytes b;
		unb64u(*env, b);
		const std::string kind = env == &helloEnv ? "hello" : "answer";
		for (size_t i = 0; i < b.size(); i++) {
			Bytes c = b;
			c[i] ^= 0x01;
			if (openPair(cr, k, kind, b64u(c), m)) anyOpened = true;
		}
	}
	r.line(!anyOpened, "pair: no byte can change");
	r.line(!openPair(cr, k, "answer", helloEnv, m), "pair: the hello doesn't open as an answer");
	{
		const PairKeys other = pairKeys(cr, counting(0xb0, 16).data());
		PairKeys wrongId = k;
		wrongId.pairId = other.pairId;
		r.line(!openPair(cr, wrongId, "answer", answerEnv, m), "pair: the answer doesn't open under another pairId");
		r.line(!openPair(cr, other, "answer", answerEnv, m), "pair: nor with another P");
	}
	{
		Bytes b;
		unb64u(answerEnv, b);
		r.line(!openPair(cr, k, "answer", b64u(Bytes(b.begin(), b.begin() + 28)), m), "pair: shorter than 29 bytes");
		r.line(!openPair(cr, k, "answer", answerEnv + "=", m) && !openPair(cr, k, "answer", "", m), "pair: not base64url");
	}
	// Sealed right but wrong inside: damaged.
	auto damaged = [&](const std::string& kind, const std::string& plain) {
		std::string env;
		return sealPairForTest(cr, k, kind, plain, counting(0x30, 12), env) && !openPair(cr, k, kind, env, m);
	};
	r.line(damaged("answer", "[1]") && damaged("answer", "not json") && damaged("hello", "\"x\""), "pair: JSON that isn't an object");
	r.line(damaged("hello", "{\"v\":2,\"kind\":\"hello\",\"device\":\"x\"}") && damaged("hello", "{\"kind\":\"hello\",\"device\":\"x\"}") &&
	           damaged("hello", "{\"v\":\"1\",\"kind\":\"hello\",\"device\":\"x\"}"),
	       "pair: v isn't 1");
	r.line(damaged("hello", "{\"v\":1,\"kind\":\"answer\",\"device\":\"x\"}"), "pair: the wrong kind inside");
	r.line(damaged("hello", "{\"v\":1,\"kind\":\"hello\",\"device\":3}") && damaged("hello", "{\"v\":1,\"kind\":\"hello\"}"),
	       "pair: device not a string");
	r.line(damaged("answer", "{\"v\":1,\"kind\":\"answer\",\"code\":\"000G40R40M30E209185GR38E1YZ5\",\"device\":\"x\"}") &&
	           damaged("answer", "{\"v\":1,\"kind\":\"answer\",\"device\":\"x\"}"),
	       "pair: a code that doesn't parse");
	{
		std::string env;
		r.line(sealPairForTest(cr, k, "answer", "{\"v\":1.0,\"kind\":\"answer\",\"code\":\"000g-40r4-0m30-e209-185g-r38e-1yz4\",\"device\":\"\"}",
		                       counting(0x30, 12), env) &&
		           openPair(cr, k, "answer", env, m) && m.code == "000G40R40M30E209185GR38E1YZ4",
		       "pair: a code as typed comes out canonical");
	}
	{
		std::string env;
		r.line(!sealPairForTest(cr, k, "hello", pairHelloJson(std::string(2100, 'x')), counting(0x30, 12), env),
		       "pair: no envelope over 2048 bytes");
	}

	// The device name as shown.
	r.line(pairDeviceText("") == "another device" && pairDeviceText("\x01\x1f") == "another device", "pair: empty -> another device");
	r.line(pairDeviceText("Sam\xE2\x80\x99s\x07 phone\n\xC2\x85") == "Sam\xE2\x80\x99s phone", "pair: control characters dropped");
	{
		std::string longName;
		for (int i = 0; i < 70; i++) longName += "\xE2\x80\x99";
		std::string want;
		for (int i = 0; i < 64; i++) want += "\xE2\x80\x99";
		r.line(pairDeviceText(longName) == want, "pair: cut to 64 characters");
	}
}

}  // namespace test
}  // namespace clsync
