// The vectors of CLASSROOM.md 7.1, read from tests/classroom/vectors.json
// (embedded as ClassroomVectors.h): codes, parsing, keys, P-256, every record
// opened and the byte-exact ones sealed again, the payloads that must be
// refused and the envelopes that must not open.

#include "ClassroomTest.h"

#include "ClassroomVectors.h"

#include <cstring>

namespace clclass {
namespace test {

using clsync::b64u;
using clsync::hex;
using clsync::sha256Hex;
using clsync::unb64u;
using clsync::unhex;

std::string embeddedVectors() {
	std::string out;
	for (const char* const* l = kVectorLines; *l; l++) out += *l;
	return out;
}

namespace {

Bytes H(const std::string& s) {
	Bytes b;
	unhex(s, b);
	return b;
}
Bytes B(const std::string& s) {
	Bytes b;
	unb64u(s, b);
	return b;
}
CodeKind kindOf(const std::string& k) { return k == "teacher" ? CodeKind::Teacher : k == "join" ? CodeKind::Join : CodeKind::Move; }

// A payload written again by this core's writers from what its reader made of it.
std::string rewrite(const json::Value& p) {
	const std::string kind = p.str("kind");
	if (kind == "teacher") {
		TeacherRec t;
		teacherFrom(p, t);
		return teacherJson(t);
	}
	if (kind == "join") return joinJson(p.str("name"), p.str("classKey"), p.str("pub"));
	if (kind == "info") return infoJson(p.str("name"), p.integer("modifiedAt"));
	if (kind == "assignment") {
		AssignmentRec a;
		assignmentFrom(p, a);
		return assignmentJson(a);
	}
	if (kind == "key") return keyJson(p.str("text"), p.str("names"));
	if (kind == "live") {
		LiveRec l;
		liveFrom(p, l);
		return liveJson(l);
	}
	if (kind == "name") return nameJson(p.str("name"), p.integer("joinedAt"), p.str("proof"));
	if (kind == "submission")
		return submissionJson(p.str("name"), p.str("cdl"), p.integer("handedInAt"), p.str("client"), (int)p.integer("attempt"),
		                      p.str("proof"));
	if (kind == "answer") {
		std::map<std::string, int> lights;
		if (const json::Value* l = p.get("lights"))
			for (const auto& kv : l->o) lights[kv.first] = (int)kv.second.n;
		return answerJson(p.str("session"), p.integer("ver"), lights, p.integer("at"), p.str("proof"));
	}
	if (kind == "move") {
		MoveRec m;
		moveFrom(p, m);
		return moveJson(m);
	}
	if (kind == "classroom")
		return classroomJson(p.str("classId"), p.str("teacherKey"), p.str("name"), p.integer("createdAt"), p.integer("modifiedAt"),
		                     p.str("device"), p.str("deviceId"));
	return std::string();
}

}  // namespace

void vectorTests(Crypto& cr, Curve& curve, Report& r, const std::string& text) {
	json::Value v;
	if (!json::parse(text, v) || !v.isObject()) {
		r.line(false, "vectors: the file reads as JSON");
		return;
	}
	r.line(v.integer("protocol") == 1 && v.integer("maxPlaintext") == (int64_t)kMaxPlaintext && v.str("salt") == kSalt &&
	           v.str("joinSalt") == kJoinSalt && v.integer("joinIterations") == kJoinRounds && v.str("webBase") == kWebBase &&
	           v.str("appBase") == kAppBase,
	       "constants: protocol, maxPlaintext, salts, rounds, links");
	{
		bool ok = true;
		if (const json::Value* e = v.get("envelopeOf"))
			for (const auto& kv : e->o) ok = ok && envelopeOf(kv.first) == kv.second.i();
		r.line(ok && v.get("envelopeOf") && v.get("envelopeOf")->o.size() == 11, "envelopeOf: every kind's envelope");
	}

	// 7.1.1 codes.
	if (const json::Value* codes = v.get("codes"))
		for (const auto& kv : codes->o)
			for (const json::Value& c : kv.second.a) {
				const CodeKind k = kindOf(kv.first);
				const std::string L = kv.first + " " + c.str("label");
				const Bytes secret = H(c.str("secretHex"));
				const std::string code = encodeCode(cr, k, secret.data());
				r.line(code == c.str("code"), "code " + L, code);
				uint8_t h[32];
				cr.sha256(secret.data(), secret.size(), h);
				char ck[8];
				snprintf(ck, sizeof ck, "%03x", (h[0] << 4) | (h[1] >> 4));
				r.line(ck == c.str("checksum12"), "checksum12 " + L);
				r.line(groupCode(code) == c.str("grouped"), "grouped " + L);
				r.line(webLink(k, code) == c.str("webLink") && appLink(k, code) == c.str("appLink"), "links " + L);
				Bytes back;
				std::string why;
				r.line(decodeCode(cr, k, c.str("grouped"), back, why) && back == secret, "decode " + L);
			}

	// 7.1.2 parsing.
	if (const json::Value* parse = v.get("parse"))
		for (const json::Value& p : parse->a) {
			std::string code, why;
			const CodeKind k = kindOf(p.str("kind"));
			const std::string got = parseCode(cr, k, p.str("input"), code, why) ? code : "error:" + why;
			r.line(got == p.str("expect"), "parse " + p.str("kind") + " " + p.str("label"), got);
			if (got.compare(0, 6, "error:") == 0)
				r.line(!whyText(k, why, p.str("input")).empty(), "why text " + p.str("kind") + " " + p.str("label"));
		}
	r.line(whyText(CodeKind::Join, "symbol", "U00G40R40MBY") == "\xE2\x80\x98U\xE2\x80\x99 can't be in a join code", "why text: symbol",
	       whyText(CodeKind::Join, "symbol", "U00G40R40MBY"));
	r.line(whyText(CodeKind::Join, "length", "000G40R40MB") == "A join code has 12 letters and digits; this has 11", "why text: length",
	       whyText(CodeKind::Join, "length", "000G40R40MB"));
	r.line(whyText(CodeKind::Join, "kind", "https://cedarlogic.netlify.app/sync/#k=000G") == "That's a sync code, not a join code.",
	       "why text: kind");
	r.line(whyText(CodeKind::Teacher, "kind", "#j=000G40R40MBY") == "That's a join code, not a teacher key.", "why text: kind (join)");
	r.line(whyText(CodeKind::Join, "symbol", "000G40\xC4\xB1" "40MBY") == "\xE2\x80\x98\xC4\xB1\xE2\x80\x99 can't be in a join code",
	       "why text: a non-ASCII character is shown as typed");
	{
		clsync::test::TestCrypto failing(cr);
		failing.failRandom = true;
		r.line(newCode(failing, CodeKind::Teacher).empty() && newCode(failing, CodeKind::Join).empty(), "no code when the RNG fails");
		std::string a = newCode(cr, CodeKind::Join), b = newCode(cr, CodeKind::Join), code, why;
		r.line(a.size() == 12 && a != b && parseCode(cr, CodeKind::Join, a, code, why), "new join codes parse and differ");
	}

	// 7.1.3 keys.
	const json::Value* keys = v.get("keys");
	if (keys) {
		if (const json::Value* t = keys->get("teacher"))
			for (const json::Value& k : t->a) {
				const std::string L = k.str("label");
				const Bytes secret = H(k.str("secretHex"));
				const TeacherKeys tk = teacherKeys(cr, secret);
				uint8_t prk[32];
				cr.hmacSha256((const uint8_t*)kSalt, strlen(kSalt), secret.data(), secret.size(), prk);
				r.line(hex(prk, 32) == k.str("prkHex"), "teacher PRK " + L);
				r.line(tk.classId == k.str("classId"), "classId " + L);
				r.line(tk.teacherToken == k.str("teacherToken") && tk.teacherHash == k.str("teacherHash"), "teacherToken " + L);
				r.line(tk.deleteToken == k.str("deleteToken") && tk.deleteHash == k.str("deleteHash"), "deleteToken " + L);
				r.line(hex(tk.backupKey) == k.str("backupKeyHex"), "backupKey " + L);
			}
		if (const json::Value* j = keys->get("join"))
			for (const json::Value& k : j->a) {
				const std::string L = k.str("label");
				const JoinKeys jk = joinKeys(cr, curve, H(k.str("secretHex")));
				r.line(hex(jk.stretched) == k.str("stretchedHex"), "join stretched (PBKDF2, 600,000 rounds) " + L);
				uint8_t prk[32];
				cr.hmacSha256((const uint8_t*)kSalt, strlen(kSalt), jk.stretched.data(), jk.stretched.size(), prk);
				r.line(hex(prk, 32) == k.str("prkHex"), "join PRK " + L);
				r.line(jk.joinId == k.str("joinId") && jk.joinToken == k.str("joinToken") && hex(jk.joinKey) == k.str("joinKeyHex"),
				       "joinId, joinToken, joinKey " + L);
				if (L == "counting") {
					// The core's own PBKDF2 on the HMAC hook (what a platform without a native one uses).
					uint8_t out[32];
					const Bytes s = H(k.str("secretHex"));
					pbkdf2Loop(cr, s.data(), s.size(), (const uint8_t*)kJoinSalt, strlen(kJoinSalt), kJoinRounds, out);
					r.line(hex(out, 32) == k.str("stretchedHex"), "join stretched by the core's own PBKDF2 loop");
				}
			}
		if (const json::Value* m = keys->get("move"))
			for (const json::Value& k : m->a) {
				const MoveKeys mk = moveKeys(cr, H(k.str("secretHex")));
				r.line(mk.moveId == k.str("moveId") && hex(mk.moveKey) == k.str("moveKeyHex"), "moveId, moveKey " + k.str("label"));
			}
		if (const json::Value* s = keys->get("server"))
			for (const json::Value& k : s->a) {
				const Bytes pepper = H(k.str("pepperHex"));
				r.line(hmacHex(cr, pepper, k.str("joinId")) == k.str("joinIndex") && hmacHex(cr, pepper, k.str("joinToken")) == k.str("joinHash"),
				       "the server's joinIndex and joinHash under the test pepper");
			}
	}
	if (const json::Value* t = v.get("tokens"))
		for (const json::Value& k : t->a) {
			const std::string token = b64u(H(k.str("bytesHex")));
			r.line(token == k.str("token") && sha256Hex(cr, token) == k.str("tokenHash"), "student token and tokenHash");
		}
	if (const json::Value* p = v.get("proof")) r.line(b64u(H(p->str("bytesHex"))) == p->str("proof"), "a student's proof");

	// 7.1.4 P-256.
	std::map<std::string, std::pair<Bytes, Bytes>> pairs;   // name -> (d, pub)
	if (const json::Value* p = v.get("p256")) {
		for (const char* name : { "teacher", "ephemeral", "other" }) {
			const json::Value* k = p->get(name);
			if (!k) continue;
			const Bytes d = H(k->str("dHex")), pub = H(k->str("pubHex"));
			uint8_t got[65];
			r.line(curve.p256Public(d.data(), got) && Bytes(got, got + 65) == pub, std::string("P-256 public key from d: ") + name);
			r.line(B(k->str("d")) == d && B(k->str("pub")) == pub, std::string("P-256 base64url forms: ") + name);
			pairs[name] = { d, pub };
		}
		uint8_t x1[32], x2[32];
		const auto& t = pairs["teacher"];
		const auto& e = pairs["ephemeral"];
		r.line(curve.p256Ecdh(e.first.data(), t.second.data(), x1) && hex(x1, 32) == p->str("sharedHex"), "ECDH ephemeral x teacher");
		r.line(curve.p256Ecdh(t.first.data(), e.second.data(), x2) && hex(x2, 32) == p->str("sharedHex"), "ECDH teacher x ephemeral");
		std::string info = "seal-key";
		info.append(e.second.begin(), e.second.end());
		info.append(t.second.begin(), t.second.end());
		r.line(clsync::hexOf(info) == p->str("sealInfoHex"), "seal-key info");
		r.line(hex(hkdf(cr, Bytes(x1, x1 + 32), info, 32)) == p->str("sealKeyHex"), "sealKey");
		if (const json::Value* bad = p->get("badPoints"))
			for (const json::Value& b : bad->a) {
				const Bytes pt = H(b.str("pubHex"));
				bool refused = !validPoint(curve, pt);
				if (pt.size() == 65) refused = refused && !curve.p256Ecdh(t.first.data(), pt.data(), x1);
				r.line(refused, "bad point refused: " + b.str("label"));
			}
		uint8_t zero[32] = {}, out[65];
		r.line(!curve.p256Public(zero, out), "P-256: a zero scalar is refused");
	}
	auto openKeyFor = [&](const json::Value& rec) {
		OpenKey k;
		if (!rec.str("keyHex").empty()) k.key = H(rec.str("keyHex"));
		const std::string who = rec.str("privateKey").empty() ? "teacher" : rec.str("privateKey");
		k.d = pairs[who].first;
		k.pub = pairs[who].second;
		return k;
	};

	// 7.1.5 records.
	if (const json::Value* recs = v.get("records"))
		for (const json::Value& rec : recs->a) {
			const std::string L = "record " + std::to_string(rec.integer("n")) + " (" + rec.str("kind") + ")";
			const std::string kind = rec.str("kind"), classId = rec.str("classId"), id = rec.str("id"), payload = rec.str("payload");
			const int64_t ver = rec.integer("ver");
			const int flags = (int)rec.integer("flags");
			const Bytes env = B(rec.str("data"));
			const Bytes nonce = H(rec.str("nonceHex"));
			r.line((int64_t)env.size() == rec.integer("envelopeBytes") && (int64_t)payload.size() == rec.integer("payloadBytes"),
			       L + ": sizes");
			r.line(clsync::envelopeHash(cr, env) == rec.str("h"), L + ": h");
			r.line(sha256Hex(cr, payload) == rec.str("payloadSha256"), L + ": payload SHA-256");
			if (!rec.str("envelopeHex").empty()) r.line(hex(env) == rec.str("envelopeHex"), L + ": envelopeHex is data");
			const Bytes deflated = H(rec.str("deflatedHex"));
			if (!deflated.empty()) {
				Bytes inflated;
				r.line(cr.inflateRaw(deflated, kMaxPlaintext, inflated) && std::string(inflated.begin(), inflated.end()) == payload,
				       L + ": the deflated bytes inflate to the payload");
			}
			if (kind == "classroom") {
				// A sync record (2.5): the sync engine's own envelope.
				const Bytes key = H(rec.str("keyHex"));
				std::string got;
				r.line(clsync::openRecord(cr, key, id, ver, env, got) && got == payload, L + ": opens as a sync record");
				Bytes again;
				r.line(clsync::sealForTest(cr, key, id, ver, payload, flags, nonce, again) && again == env, L + ": sealed again, byte-exact");
				clsync::Payload sp;
				r.line(clsync::readPayload(payload, sp) == "newer", L + ": an older sync engine reads it as newer (2.5)");
			} else {
				std::string got, why;
				const bool opened = open(cr, curve, env, kind, classId, id, ver, openKeyFor(rec), got, why);
				r.line(opened && got == payload, L + ": opens to the payload", why);
				Bytes again;
				if (envelopeOf(kind) == 1)
					sealForTest(cr, H(rec.str("keyHex")), kind, classId, id, ver, payload, flags, nonce, deflated, again);
				else
					sealToForTest(cr, curve, pairs["teacher"].second, pairs[rec.str("ephemeral")].first, kind, classId, id, ver, payload,
					              flags, nonce, deflated, again);
				r.line(again == env, L + (rec.flag("byteExact") ? ": sealed again, byte-exact" : ": sealed again from its deflate"));
			}
			json::Value p;
			const std::string verdict = readPayload(payload, kind, p);
			r.line(verdict.empty(), L + ": reads as its kind", verdict);
			r.line(verdict.empty() && rewrite(p) == payload, L + ": written again by the core's writer, byte for byte", rewrite(p));
			if (const json::Value* inner = rec.get("inner")) {
				const Bytes ienv = H(inner->str("envHex"));
				std::string got, why;
				const bool ok = open(cr, curve, ienv, "key", classId, inner->str("id"), inner->integer("ver"), openKeyFor(json::Value()), got,
				                     why);
				json::Value kp;
				r.line(ok && readPayload(got, "key", kp).empty() && kp.str("text") == "LED = A" && got == keyJson("LED = A", ""),
				       L + ": the inner sealed key opens", why);
				Bytes again;
				sealToForTest(cr, curve, pairs["teacher"].second, pairs[inner->str("ephemeral")].first, "key", classId, inner->str("id"),
				              inner->integer("ver"), got, 0, H(inner->str("nonce")), Bytes(), again);
				r.line(again == ienv, L + ": the inner key sealed again, byte-exact");
				// The assignment's key field carries that envelope.
				json::Value ap;
				readPayload(payload, "assignment", ap);
				const json::Value* k = ap.get("key");
				r.line(k && B(k->str("sealed")) == ienv, L + ": the assignment carries the sealed key");
			}
		}

	// 7.1.6 payloads that open but must be refused.
	if (const json::Value* refused = v.get("refused"))
		for (const json::Value& rec : refused->a) {
			const std::string L = "refused: " + rec.str("label");
			std::string got, why;
			const bool opened = open(cr, curve, B(rec.str("data")), rec.str("kind"), rec.str("classId"), rec.str("id"), rec.integer("ver"),
			                         openKeyFor(rec), got, why);
			r.line(opened && clsync::hexOf(got) == rec.str("payloadHex"), L + ": opens", why);
			json::Value p;
			const std::string verdict = readPayload(got, rec.str("kind"), p);
			r.line(verdict == rec.str("why"), L + ": " + rec.str("why"), verdict);
		}

	// 7.1.7 must not open.
	if (const json::Value* tamper = v.get("tamper"))
		for (const json::Value& rec : tamper->a) {
			std::string got, why;
			const bool opened = open(cr, curve, B(rec.str("data")), rec.str("kind"), rec.str("classId"), rec.str("id"), rec.integer("ver"),
			                         openKeyFor(rec), got, why);
			r.line(!opened, "must not open: " + rec.str("label"), why);
		}

	// Properties: two seals of one payload differ, and both open.
	{
		const Bytes key(32, 7);
		const std::string pl = infoJson("Digital Logic 101", 1);
		const std::string cid = "9c89e40e9ea981bbfeaf61e93c91be46";
		Bytes a, b;
		std::string why, got1, got2;
		seal(cr, key, "info", cid, "info", 1, pl, false, kMaxSmall, a, why);
		seal(cr, key, "info", cid, "info", 1, pl, false, kMaxSmall, b, why);
		OpenKey k;
		k.key = key;
		r.line(a != b && open(cr, curve, a, "info", cid, "info", 1, k, got1, why) && open(cr, curve, b, "info", cid, "info", 1, k, got2, why) &&
		           got1 == pl && got2 == pl,
		       "two symmetric seals of one payload differ, both open");
		const auto& t = pairs["teacher"];
		const std::string np = nameJson("Sam Lee", 1, "YGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn8");
		sealTo(cr, curve, t.second, "name", cid, "5f3a1c2e-8b4d-4a6f-9c1d-2e3f4a5b6c7d", 1, np, false, kMaxName, a, why);
		sealTo(cr, curve, t.second, "name", cid, "5f3a1c2e-8b4d-4a6f-9c1d-2e3f4a5b6c7d", 1, np, false, kMaxName, b, why);
		OpenKey tk;
		tk.d = t.first;
		tk.pub = t.second;
		r.line(a.size() == 95 + np.size() && a != b && Bytes(a.begin() + 2, a.begin() + 67) != Bytes(b.begin() + 2, b.begin() + 67) &&
		           open(cr, curve, a, "name", cid, "5f3a1c2e-8b4d-4a6f-9c1d-2e3f4a5b6c7d", 1, tk, got1, why) && got1 == np,
		       "two sealed envelopes differ (fresh ephemeral key and nonce), both open");
		// A big payload deflated and opened through the capped inflate.
		std::string big(200000, 'x');
		const std::string bigPayload = submissionJson("Sam", big, 1, "test", 1, "YGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn8");
		r.line(sealTo(cr, curve, t.second, "submission", cid, "a/b", 1, bigPayload, true, kMaxRecord, a, why) && a[1] == 1 &&
		           a.size() < 10000 && open(cr, curve, a, "submission", cid, "a/b", 1, tk, got1, why) && got1 == bigPayload,
		       "a deflated hand-in opens");
		clsync::test::TestCrypto failing(cr);
		failing.failRandom = true;
		r.line(!seal(failing, key, "info", cid, "info", 1, pl, false, kMaxSmall, a, why) && why == "rng" && a.empty(),
		       "no seal when the RNG fails");
		TestCurve noKeys(curve);
		noKeys.failGenerate = true;
		r.line(!sealTo(cr, noKeys, t.second, "name", cid, "x", 1, np, false, kMaxName, a, why) && a.empty(),
		       "no sealed envelope when the key pair can't be made");
		r.line(!sealTo(cr, curve, t.second, "submission", cid, "a/b", 1, std::string(600000, 'y'), false, kMaxRecord, a, why) &&
		           why == "too big",
		       "an envelope over its cap isn't made");
	}
	// The texts (2.2).
	r.line(cleanName("  Sam\tLee\x01  ", 64, "a student") == "SamLee" && cleanName(" \n ", 64, "a student") == "a student" &&
	           clsync::scalarCount(cleanName(std::string(70, 'a') + "\xC3\xA9", 64, "x")) == 64,
	       "names: trimmed, control characters dropped, cut to 64");
	int g = 0, s = 0;
	countParts("(gate \"A\")(gate \"B\")(seg \"0\" h)", g, s);
	r.line(g == 2 && s == 1, "parts counted in a v3 circuit");
}

}  // namespace test
}  // namespace clclass
