// The preview updates' signature check and manifest reader, on their own
// (UpdateSignature.h): RFC 8032's Ed25519 test vectors, then the ways a
// signature must fail -- another key, a flipped bit in the message, R or S,
// a truncated or lengthened signature, S + L in place of S -- then the
// manifest format's edge cases.
//
//   verify_check               run the checks; exit 0 when all pass
//   verify_check --print-key   print the built-in public key in hex (CI compares
//                              it with the signing script's and install.sh's)

#include "UpdateSignature.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace updatesig;

namespace {

int failures = 0;

void expect(const std::string& label, bool ok) {
	printf("%s %s\n", ok ? "  ok  " : "  FAIL", label.c_str());
	if (!ok) failures++;
}

std::string bytes(const char* hex) {
	std::string out;
	if (!fromHex(hex, out)) {
		fprintf(stderr, "bad hex in a test vector\n");
		failures++;
	}
	return out;
}

struct Vector {
	const char* name;
	const char* secret;
	const char* publicKey;
	const char* message;
	const char* signature;
};

// RFC 8032, section 7.1.
const Vector kVectors[] = {
	{ "TEST 1", "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
	  "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
	  "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b" },
	{ "TEST 2", "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
	  "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
	  "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00" },
	{ "TEST 3", "c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
	  "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
	  "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a" },
	{ "TEST SHA(abc)", "833fe62409237b9d62ec77587520911e9a759cec1d19755b7da901b96dca3d42",
	  "ec172b93ad5e563bf4932c70e1245034c35467ef2efd4d64ebf819683467e2bf",
	  "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
	  "dc2a4459e7369633a52b1bf277839a00201009a3efbf3ecb69bea2186c26b58909351fc9ac90b3ecfdfbc7c66431e0303dca179c138ac17ad9bef1177331a704" },
};

// S + L, where L is the order of the base point: the same signature to a
// lax verifier, refused by a strict one (RFC 8032 5.1.7 requires S < L).
std::string addL(const std::string& sig) {
	static const unsigned char L[32] = { 0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
	                                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10 };
	std::string out = sig;
	unsigned carry = 0;
	for (int i = 0; i < 32; i++) {
		const unsigned v = (unsigned char)out[32 + i] + L[i] + carry;
		out[32 + i] = (char)(v & 0xff);
		carry = v >> 8;
	}
	return out;
}

const char* kGood =
	"CedarLogic preview manifest 1\n"
	"tag linux-native-testing\n"
	"commit 0123456789abcdef0123456789abcdef01234567\n"
	"build 1761567890\n"
	"file cedarlogic_amd64.deb 1234 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
	"file CedarLogic-Native-0.4.0-x86_64.AppImage 99 bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n";

bool parses(const std::string& text) {
	Manifest m;
	std::string why;
	return parseManifest(text, m, why);
}

std::string replaced(std::string s, const std::string& from, const std::string& to) {
	const size_t at = s.find(from);
	if (at != std::string::npos) s.replace(at, from.size(), to);
	return s;
}

}  // namespace

int main(int argc, char** argv) {
	if (argc > 1 && strcmp(argv[1], "--print-key") == 0) {
		printf("%s\n", toHex(kPublicKey, 32).c_str());
		return 0;
	}

	printf("Ed25519 (RFC 8032 test vectors)\n");
	for (const Vector& v : kVectors) {
		const std::string pub = bytes(v.publicKey), msg = bytes(v.message), sig = bytes(v.signature), seed = bytes(v.secret);
		const unsigned char* key = reinterpret_cast<const unsigned char*>(pub.data());
		expect(std::string(v.name) + ": verifies", verifySignature(msg, sig, key));

		// The same key and message signed here: the same signature (Ed25519 is deterministic).
		unsigned char derived[32];
		const std::string mine = testSign(msg, reinterpret_cast<const unsigned char*>(seed.data()), derived);
		expect(std::string(v.name) + ": signing gives the same key and signature", mine == sig && memcmp(derived, key, 32) == 0);

		std::string otherKey = pub;
		otherKey[0] ^= 0x01;
		expect(std::string(v.name) + ": another key is refused",
		       !verifySignature(msg, sig, reinterpret_cast<const unsigned char*>(otherKey.data())));
		const std::string wrongKey = bytes(kVectors[(&v - kVectors + 1) % 4].publicKey);
		expect(std::string(v.name) + ": the next vector's key is refused",
		       !verifySignature(msg, sig, reinterpret_cast<const unsigned char*>(wrongKey.data())));

		std::string m2 = msg.empty() ? std::string("x") : msg;
		if (!msg.empty()) m2[0] ^= 0x01;
		expect(std::string(v.name) + ": a changed message is refused", !verifySignature(m2, sig, key));
		expect(std::string(v.name) + ": a byte added to the message is refused", !verifySignature(msg + '\0', sig, key));

		std::string r = sig, s = sig;
		r[3] ^= 0x10;
		s[60] ^= 0x01;
		expect(std::string(v.name) + ": a flipped bit in R is refused", !verifySignature(msg, r, key));
		expect(std::string(v.name) + ": a flipped bit in S is refused", !verifySignature(msg, s, key));
		expect(std::string(v.name) + ": a truncated signature (63 bytes) is refused", !verifySignature(msg, sig.substr(0, 63), key));
		expect(std::string(v.name) + ": a signature with a byte added is refused", !verifySignature(msg, sig + '\0', key));
		expect(std::string(v.name) + ": an empty signature is refused", !verifySignature(msg, "", key));
		expect(std::string(v.name) + ": S + L (malleated) is refused", !verifySignature(msg, addL(sig), key));
	}
	{
		const std::string zero(64, '\0');
		expect("all-zero signature is refused", !verifySignature("", zero, kPublicKey));
	}

	printf("Manifest\n");
	{
		Manifest m;
		std::string why;
		const bool ok = parseManifest(kGood, m, why);
		expect("a good manifest reads", ok && m.tag == "linux-native-testing" && m.build == 1761567890 && m.files.size() == 2 &&
		                                    m.files[0].name == "cedarlogic_amd64.deb" && m.files[0].size == 1234);
		expect("and writes back the same", ok && formatManifest(m) == kGood);
	}
	const std::string good = kGood;
	expect("no final newline: refused", !parses(good.substr(0, good.size() - 1)));
	expect("CRLF: refused", !parses(replaced(good, "\n", "\r\n")));
	expect("another first line: refused", !parses(replaced(good, "manifest 1", "manifest 2")));
	expect("lines out of order: refused", !parses(replaced(replaced(good, "tag linux-native-testing\n", ""), "build 1761567890\n",
	                                                       "build 1761567890\ntag linux-native-testing\n")));
	expect("a short commit: refused", !parses(replaced(good, "commit 0123456789abcdef0123456789abcdef01234567", "commit 0123456")));
	expect("an uppercase commit: refused", !parses(replaced(good, "commit 0123456789abcdef", "commit 0123456789ABCDEF")));
	expect("build 0: refused", !parses(replaced(good, "build 1761567890", "build 0")));
	expect("a negative build: refused", !parses(replaced(good, "build 1761567890", "build -5")));
	expect("a build with a leading zero: refused", !parses(replaced(good, "build 1761567890", "build 01761567890")));
	expect("a huge build: refused", !parses(replaced(good, "build 1761567890", "build 99999999999999999999")));
	expect("no files: refused", !parses(good.substr(0, good.find("file "))));
	expect("a path for a name: refused", !parses(replaced(good, "file cedarlogic_amd64.deb", "file ../cedarlogic_amd64.deb")));
	expect("a dot file: refused", !parses(replaced(good, "file cedarlogic_amd64.deb", "file .cedarlogic_amd64.deb")));
	expect("a backslash in a name: refused", !parses(replaced(good, "file cedarlogic_amd64.deb", "file a\\cedarlogic_amd64.deb")));
	expect("two spaces: refused", !parses(replaced(good, "file cedarlogic_amd64.deb 1234", "file cedarlogic_amd64.deb  1234")));
	expect("a short hash: refused", !parses(replaced(good, " aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n", " aaaa\n")));
	expect("size 0: refused", !parses(replaced(good, "deb 1234 ", "deb 0 ")));
	expect("a name listed twice: refused",
	       !parses(good + "file cedarlogic_amd64.deb 1 cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\n"));
	expect("a blank line: refused", !parses(good + "\n"));
	expect("something after the files: refused", !parses(good + "end\n"));
	expect("a NUL: refused", !parses(replaced(good, "tag linux", std::string("tag lin\0ux", 10))));

	printf("Decision\n");
	{
		unsigned char seed[32] = { 7 }, pub[32];
		const std::string sig = testSign(good, seed, pub);
		const std::string commit = "0123456789abcdef0123456789abcdef01234567";
		auto deb = [](const std::string& n) { return n == "cedarlogic_amd64.deb"; };
		Decision d = decide(true, good, true, sig, pub, "linux-native-testing", commit, std::string(40, 'f'), 100, deb);
		expect("offered when everything matches", d.result == Result::Offer && d.file.name == "cedarlogic_amd64.deb");
		d = decide(true, good, true, sig, kPublicKey, "linux-native-testing", commit, std::string(40, 'f'), 100, deb);
		expect("not with the real key (the test key signed it)", d.result == Result::NotVerified);
		d = decide(true, good, true, sig, pub, "windows-native-testing", commit, std::string(40, 'f'), 100, deb);
		expect("not for another tag", d.result == Result::NotVerified);
		d = decide(true, good, true, sig, pub, "linux-native-testing", std::string(40, 'e'), std::string(40, 'f'), 100, deb);
		expect("not when the tag is on another commit", d.result == Result::NotVerified);
		d = decide(true, good, true, sig, pub, "linux-native-testing", commit, std::string(40, 'f'), 1761567890, deb);
		expect("not when it's no newer", d.result == Result::UpToDate);
		d = decide(true, good, true, sig, pub, "linux-native-testing", commit, commit, 100, deb);
		expect("up to date on the same commit", d.result == Result::UpToDate);
		d = decide(true, good, true, sig, pub, "linux-native-testing", "", std::string(40, 'f'), 100, deb);
		expect("not when the release's commit is unknown", d.result == Result::NotVerified);
		auto any = [](const std::string&) { return true; };
		d = decide(true, good, true, sig, pub, "linux-native-testing", commit, std::string(40, 'f'), 100, any);
		expect("not when two files could be this computer's", d.result == Result::NotVerified);

		File f{ "x.deb", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" };
		std::string why;
		expect("a download that matches", checkDownload(f, 3, f.sha256, why));
		expect("a download one byte longer", !checkDownload(f, 4, f.sha256, why));
		expect("a download with another hash",
		       !checkDownload(f, 3, "ca7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", why));
		expect("a download with no hash", !checkDownload(f, 3, "", why));
	}

	printf("%s\n", failures ? "verify_check: FAILED" : "verify_check: all passed");
	return failures ? 1 : 0;
}
