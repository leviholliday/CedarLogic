// Signed preview updates (see UpdateSignature.h).

#include "UpdateSignature.h"

#include "monocypher/monocypher-ed25519.h"
#include "monocypher/monocypher.h"

#include <cstring>
#include <set>

namespace updatesig {

// 4184daba2830c1db416be7869a7d873788170ea1c00104abcbfd018d7fe3cf40
const unsigned char kPublicKey[32] = {
	0x41, 0x84, 0xda, 0xba, 0x28, 0x30, 0xc1, 0xdb, 0x41, 0x6b, 0xe7, 0x86, 0x9a, 0x7d, 0x87, 0x37,
	0x88, 0x17, 0x0e, 0xa1, 0xc0, 0x01, 0x04, 0xab, 0xcb, 0xfd, 0x01, 0x8d, 0x7f, 0xe3, 0xcf, 0x40,
};

const char* const kManifestName = "SHA256SUMS";
const char* const kSignatureName = "SHA256SUMS.sig";

namespace {

const char* const kHeader = "CedarLogic preview manifest 1";
const size_t kMaxManifest = 64 * 1024;
const size_t kMaxFiles = 256;

// A decimal count with no sign, no leading zero and no more than 15 digits
// (so it fits a long long with room to spare).
bool parseCount(const std::string& s, long long& out) {
	if (s.empty() || s.size() > 15 || (s.size() > 1 && s[0] == '0')) return false;
	long long v = 0;
	for (char c : s) {
		if (c < '0' || c > '9') return false;
		v = v * 10 + (c - '0');
	}
	out = v;
	return true;
}

std::vector<std::string> splitSpaces(const std::string& s) {
	std::vector<std::string> out;
	size_t at = 0;
	while (true) {
		const size_t sp = s.find(' ', at);
		out.push_back(s.substr(at, sp == std::string::npos ? std::string::npos : sp - at));
		if (sp == std::string::npos) break;
		at = sp + 1;
	}
	return out;
}

std::string shortSha(const std::string& s) { return s.size() > 12 ? s.substr(0, 12) : s; }

}  // namespace

bool isHex(const std::string& s, size_t length) {
	if (s.size() != length) return false;
	for (char c : s)
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
	return true;
}

bool safeName(const std::string& name) {
	if (name.empty() || name.size() > 128 || name[0] == '.') return false;
	for (char c : name) {
		const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
		                c == '+' || c == '-';
		if (!ok) return false;
	}
	return true;
}

std::string toHex(const unsigned char* bytes, size_t n) {
	static const char* digits = "0123456789abcdef";
	std::string out;
	out.reserve(n * 2);
	for (size_t i = 0; i < n; i++) {
		out += digits[bytes[i] >> 4];
		out += digits[bytes[i] & 15];
	}
	return out;
}

bool fromHex(const std::string& hex, std::string& bytes) {
	if (hex.size() % 2) return false;
	auto nibble = [](char c) -> int {
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	};
	std::string out;
	for (size_t i = 0; i < hex.size(); i += 2) {
		const int hi = nibble(hex[i]), lo = nibble(hex[i + 1]);
		if (hi < 0 || lo < 0) return false;
		out += (char)(hi * 16 + lo);
	}
	bytes.swap(out);
	return true;
}

bool verifySignature(const std::string& message, const std::string& signature, const unsigned char publicKey[32]) {
	if (signature.size() != 64) return false;
	return crypto_ed25519_check(reinterpret_cast<const uint8_t*>(signature.data()), publicKey,
	                            reinterpret_cast<const uint8_t*>(message.data()), message.size()) == 0;
}

bool parseManifest(const std::string& text, Manifest& out, std::string& why) {
	Manifest m;
	if (text.empty() || text.size() > kMaxManifest) { why = "the manifest is empty or too long"; return false; }
	if (text.back() != '\n') { why = "the manifest doesn't end with a newline"; return false; }
	if (text.find('\r') != std::string::npos || text.find('\0') != std::string::npos) {
		why = "the manifest has a carriage return or a NUL in it";
		return false;
	}
	std::vector<std::string> lines;
	for (size_t at = 0; at < text.size();) {
		const size_t nl = text.find('\n', at);
		lines.push_back(text.substr(at, nl - at));
		at = nl + 1;
	}
	if (lines.size() < 5) { why = "the manifest is missing lines"; return false; }
	if (lines[0] != kHeader) { why = "the manifest's first line isn't \"" + std::string(kHeader) + "\""; return false; }
	auto field = [&](size_t i, const char* key, std::string& value) {
		const std::string prefix = std::string(key) + " ";
		if (lines[i].compare(0, prefix.size(), prefix) != 0) return false;
		value = lines[i].substr(prefix.size());
		return true;
	};
	std::string build;
	if (!field(1, "tag", m.tag) || !safeName(m.tag)) { why = "line 2 isn't a tag"; return false; }
	if (!field(2, "commit", m.commit) || !isHex(m.commit, 40)) { why = "line 3 isn't a 40-digit commit"; return false; }
	if (!field(3, "build", build) || !parseCount(build, m.build) || m.build <= 0) { why = "line 4 isn't a build number"; return false; }
	std::set<std::string> names;
	for (size_t i = 4; i < lines.size(); i++) {
		const std::vector<std::string> parts = splitSpaces(lines[i]);
		File f;
		if (parts.size() != 4 || parts[0] != "file") { why = "line " + std::to_string(i + 1) + " isn't a file line"; return false; }
		f.name = parts[1];
		f.sha256 = parts[3];
		if (!safeName(f.name)) { why = "line " + std::to_string(i + 1) + " has a name that isn't a plain file name"; return false; }
		if (!parseCount(parts[2], f.size) || f.size <= 0) { why = "line " + std::to_string(i + 1) + " has a bad size"; return false; }
		if (!isHex(f.sha256, 64)) { why = "line " + std::to_string(i + 1) + " has a bad sha256"; return false; }
		if (!names.insert(f.name).second) { why = f.name + " is listed twice"; return false; }
		m.files.push_back(f);
	}
	if (m.files.size() > kMaxFiles) { why = "the manifest lists too many files"; return false; }
	out = m;
	return true;
}

std::string formatManifest(const Manifest& m) {
	std::string s = std::string(kHeader) + "\n";
	s += "tag " + m.tag + "\n";
	s += "commit " + m.commit + "\n";
	s += "build " + std::to_string(m.build) + "\n";
	for (const File& f : m.files) s += "file " + f.name + " " + std::to_string(f.size) + " " + f.sha256 + "\n";
	return s;
}

Decision decide(bool haveManifest, const std::string& manifestText, bool haveSignature, const std::string& signature,
                const unsigned char publicKey[32], const std::string& tag, const std::string& releaseCommit,
                const std::string& runningCommit, long long runningBuild, const std::function<bool(const std::string&)>& ours) {
	Decision d;
	d.result = Result::NotVerified;
	if (!isHex(releaseCommit, 40)) { d.why = "the release's commit isn't known"; return d; }
	if (releaseCommit == runningCommit) {
		d.result = Result::UpToDate;
		d.why = "the release is this build (" + shortSha(runningCommit) + ")";
		return d;
	}
	if (!haveManifest) { d.why = "the release has no " + std::string(kManifestName) + " (published, not signed yet)"; return d; }
	if (!haveSignature) { d.why = "the release has no " + std::string(kSignatureName) + " (published, not signed yet)"; return d; }
	if (manifestText.size() > kMaxManifest) { d.why = "the manifest is too long"; return d; }
	if (!verifySignature(manifestText, signature, publicKey)) {
		d.why = signature.size() == 64 ? "the manifest's signature doesn't verify" : "the signature isn't 64 bytes";
		return d;
	}
	Manifest m;
	std::string why;
	if (!parseManifest(manifestText, m, why)) { d.why = "a signed manifest that can't be read: " + why; return d; }
	if (m.tag != tag) { d.why = "the manifest is for " + m.tag + ", not " + tag; return d; }
	if (m.commit != releaseCommit) {
		d.why = "the manifest is for commit " + shortSha(m.commit) + ", but the release is on " + shortSha(releaseCommit) +
		        " (left from an earlier build, or not signed yet)";
		return d;
	}
	d.manifest = m;
	if (m.build <= runningBuild) {
		d.result = Result::UpToDate;
		d.why = "the release's build (" + std::to_string(m.build) + ") isn't newer than this one (" + std::to_string(runningBuild) +
		        "): not going back";
		return d;
	}
	int matches = 0;
	for (const File& f : m.files)
		if (ours(f.name)) {
			d.file = f;
			matches++;
		}
	if (matches != 1) {
		d.why = matches == 0 ? "the manifest has no file for this computer" : "the manifest has more than one file for this computer";
		d.file = File();
		return d;
	}
	d.result = Result::Offer;
	d.why = "verified: " + d.file.name + " for commit " + shortSha(m.commit) + ", build " + std::to_string(m.build);
	return d;
}

bool checkDownload(const File& expected, long long size, const std::string& sha256, std::string& why) {
	if (size != expected.size) {
		why = expected.name + " is " + std::to_string(size) + " bytes, the manifest says " + std::to_string(expected.size);
		return false;
	}
	if (!isHex(sha256, 64) || sha256 != expected.sha256) {
		why = expected.name + "'s sha256 is " + shortSha(sha256) + "..., the manifest says " + shortSha(expected.sha256) + "...";
		return false;
	}
	why.clear();
	return true;
}

std::string testSign(const std::string& message, const unsigned char seed[32], unsigned char publicKeyOut[32]) {
	uint8_t s[32], secret[64], sig[64];
	memcpy(s, seed, 32);   // (key_pair wipes the seed it's given)
	crypto_ed25519_key_pair(secret, publicKeyOut, s);
	crypto_ed25519_sign(sig, secret, reinterpret_cast<const uint8_t*>(message.data()), message.size());
	crypto_wipe(secret, sizeof secret);
	return std::string(reinterpret_cast<const char*>(sig), 64);
}

// ---- Self-test ----------------------------------------------------------------

bool selfTest(const std::string& dir, const SelfTestHooks& h, std::string& report) {
	bool all = true;
	auto expect = [&](const std::string& label, bool ok, const std::string& detail) {
		report += std::string(ok ? "  ok   " : "  FAIL ") + label + (detail.empty() ? "" : " -- " + detail) + "\n";
		all = all && ok;
	};

	// The platform's SHA-256, against known answers (FIPS 180-2).
	expect("SHA-256 of \"abc\"", h.sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "");
	expect("SHA-256 of nothing", h.sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "");

	unsigned char seed[32], otherSeed[32], pub[32], otherPub[32];
	for (int i = 0; i < 32; i++) {
		seed[i] = (unsigned char)(0x5a ^ (i * 7));
		otherSeed[i] = (unsigned char)(0xa5 ^ (i * 11));
	}
	testSign("", seed, pub);
	testSign("", otherSeed, otherPub);

	const std::string running(40, '1');   // this copy
	const long long runningBuild = 1000;
	const std::string fresh(40, '2');     // the new build
	const std::string earlier(40, '3');   // a build after this one, before the new one
	const std::string older(40, '4');     // one before this copy
	std::string newBytes = "CedarLogic self-test: the new build's file\n", oldBytes = "CedarLogic self-test: an earlier file!\n";
	while (newBytes.size() < 4096) newBytes += "new ";
	while (oldBytes.size() < newBytes.size()) oldBytes += "old ";
	oldBytes.resize(newBytes.size());   // the same size: only the hash tells them apart

	auto manifestFor = [&](const std::string& tag, const std::string& commit, long long build, const std::string& name,
	                       const std::string& bytes) {
		Manifest m;
		m.tag = tag;
		m.commit = commit;
		m.build = build;
		m.files.push_back({ "README-other.txt", 5, h.sha256Hex("other") });
		m.files.push_back({ name, (long long)bytes.size(), h.sha256Hex(bytes) });
		return formatManifest(m);
	};
	int n = 0;
	// One fake release: its own folder with what's given (empty: left out).
	auto release = [&](const std::string& manifest, const std::string& signature, const std::string& asset) {
		const std::string d = h.join(dir, "release-" + std::to_string(++n));
		h.makeDir(d);
		if (!manifest.empty()) h.writeFile(h.join(d, kManifestName), manifest);
		if (!signature.empty()) h.writeFile(h.join(d, kSignatureName), signature);
		h.writeFile(h.join(d, h.assetName), asset);
		return d;
	};
	auto sign = [&](const std::string& m) {
		unsigned char p[32];
		return testSign(m, seed, p);
	};
	const std::string good = manifestFor(h.tag, fresh, 2000, h.assetName, newBytes);
	const std::string goodSig = sign(good);

	{
		const std::string d = release(good, goodSig, newBytes);
		const Decision r = h.find(d, pub, fresh, running, runningBuild);
		std::string why;
		const bool fileOk = r.result == Result::Offer && h.verifyFile(h.join(d, h.assetName), r.file, why);
		expect("good manifest: offered, and the download checks out", r.result == Result::Offer && r.file.name == h.assetName && fileOk,
		       r.why + (why.empty() ? "" : "; " + why));
	}
	{
		std::string bad = goodSig;
		bad[10] ^= 0x01;
		const Decision r = h.find(release(good, bad, newBytes), pub, fresh, running, runningBuild);
		expect("bad signature (one bit flipped): refused", r.result == Result::NotVerified, r.why);
	}
	{
		std::string bad = goodSig;
		bad[40] ^= 0x80;   // in S
		const Decision r = h.find(release(good, bad, newBytes), pub, fresh, running, runningBuild);
		expect("bad signature (S changed): refused", r.result == Result::NotVerified, r.why);
	}
	{
		const Decision r = h.find(release(good, goodSig, newBytes), otherPub, fresh, running, runningBuild);
		expect("signed by another key: refused", r.result == Result::NotVerified, r.why);
	}
	{
		std::string edited = good;
		edited[edited.size() - 2] = edited[edited.size() - 2] == '0' ? '1' : '0';   // the last hash digit
		const Decision r = h.find(release(edited, goodSig, newBytes), pub, fresh, running, runningBuild);
		expect("manifest edited after signing: refused", r.result == Result::NotVerified, r.why);
	}
	{
		const Decision r = h.find(release(good, "", newBytes), pub, fresh, running, runningBuild);
		expect("published, not signed (no .sig): refused", r.result == Result::NotVerified, r.why);
	}
	{
		const Decision r = h.find(release("", "", newBytes), pub, fresh, running, runningBuild);
		expect("no manifest at all: refused", r.result == Result::NotVerified, r.why);
	}
	{
		const Decision r = h.find(release(good, goodSig.substr(0, 63), newBytes), pub, fresh, running, runningBuild);
		expect("truncated signature (63 bytes): refused", r.result == Result::NotVerified, r.why);
	}
	{
		const Decision r = h.find(release(good, goodSig + "x", newBytes), pub, fresh, running, runningBuild);
		expect("signature with a byte added: refused", r.result == Result::NotVerified, r.why);
	}
	{
		// The last build's manifest, still on the release after CI put up a
		// new build and moved the tag.
		const std::string stale = manifestFor(h.tag, earlier, 1500, h.assetName, oldBytes);
		const Decision r = h.find(release(stale, sign(stale), newBytes), pub, fresh, running, runningBuild);
		expect("stale manifest, tag moved to a new commit: refused", r.result == Result::NotVerified, r.why);
	}
	{
		// CI uploads the new files first and moves the tag last: meanwhile
		// the old manifest matches the tag, but not the new files.
		const std::string stale = manifestFor(h.tag, earlier, 1500, h.assetName, oldBytes);
		const std::string d = release(stale, sign(stale), newBytes);
		const Decision r = h.find(d, pub, earlier, running, runningBuild);
		std::string why;
		const bool fileOk = r.result == Result::Offer && h.verifyFile(h.join(d, h.assetName), r.file, why);
		expect("stale manifest, tag not moved yet, new files: the download is refused", r.result == Result::Offer && !fileOk,
		       why.empty() ? r.why : why);
	}
	{
		const std::string d = release(good, goodSig, oldBytes);   // the same size, other bytes
		const Decision r = h.find(d, pub, fresh, running, runningBuild);
		std::string why;
		const bool fileOk = r.result == Result::Offer && h.verifyFile(h.join(d, h.assetName), r.file, why);
		expect("wrong hash (same size): the download is refused", r.result == Result::Offer && !fileOk, why);
	}
	{
		const std::string d = release(good, goodSig, newBytes + "!");
		const Decision r = h.find(d, pub, fresh, running, runningBuild);
		std::string why;
		const bool fileOk = r.result == Result::Offer && h.verifyFile(h.join(d, h.assetName), r.file, why);
		expect("wrong size: the download is refused", r.result == Result::Offer && !fileOk, why);
	}
	{
		const std::string down = manifestFor(h.tag, older, 900, h.assetName, oldBytes);
		const Decision r = h.find(release(down, sign(down), oldBytes), pub, older, running, runningBuild);
		expect("downgrade (an older build, signed, tag moved back): not offered", r.result != Result::Offer, r.why);
	}
	{
		const std::string same = manifestFor(h.tag, older, runningBuild, h.assetName, oldBytes);
		const Decision r = h.find(release(same, sign(same), oldBytes), pub, older, running, runningBuild);
		expect("a build no newer than this one: not offered", r.result != Result::Offer, r.why);
	}
	{
		const std::string other = manifestFor("someone-elses-testing", fresh, 2000, h.assetName, newBytes);
		const Decision r = h.find(release(other, sign(other), newBytes), pub, fresh, running, runningBuild);
		expect("another release's manifest: refused", r.result == Result::NotVerified, r.why);
	}
	{
		const std::string none = manifestFor(h.tag, fresh, 2000, "CedarLogic-for-a-toaster.bin", newBytes);
		const Decision r = h.find(release(none, sign(none), newBytes), pub, fresh, running, runningBuild);
		expect("no file for this computer: refused", r.result == Result::NotVerified, r.why);
	}
	{
		std::string evil = good;
		const size_t at = evil.find("file " + h.assetName);
		evil.replace(at, 5 + h.assetName.size(), "file ../" + h.assetName);
		const Decision r = h.find(release(evil, sign(evil), newBytes), pub, fresh, running, runningBuild);
		expect("a path in a signed manifest: refused", r.result == Result::NotVerified, r.why);
	}
	{
		std::string crlf;
		for (char c : good) {
			if (c == '\n') crlf += '\r';
			crlf += c;
		}
		const Decision r = h.find(release(crlf, sign(crlf), newBytes), pub, fresh, running, runningBuild);
		expect("a signed manifest with CRLF line ends: refused", r.result == Result::NotVerified, r.why);
	}
	{
		const Decision r = h.find(release("", "", newBytes), pub, running, running, runningBuild);
		expect("the release is this build: up to date", r.result == Result::UpToDate, r.why);
	}
	return all;
}

bool checkSignedFolder(const std::string& dir, const std::string& publicKeyHex, const SelfTestHooks& h,
                       const std::function<bool(const std::string& path, std::string& bytes)>& readFile, std::string& report) {
	std::string key, manifest, sig;
	if (!isHex(publicKeyHex, 64) || !fromHex(publicKeyHex, key)) {
		report += "  FAIL the public key isn't 64 hex digits\n";
		return false;
	}
	if (!readFile(h.join(dir, kManifestName), manifest) || !readFile(h.join(dir, kSignatureName), sig)) {
		report += "  FAIL no SHA256SUMS or SHA256SUMS.sig in " + dir + "\n";
		return false;
	}
	unsigned char pub[32];
	memcpy(pub, key.data(), 32);
	Manifest m;
	std::string why;
	if (!parseManifest(manifest, m, why)) {
		report += "  FAIL " + why + "\n";
		return false;
	}
	const bool sigOk = verifySignature(manifest, sig, pub);
	report += std::string(sigOk ? "  ok   " : "  FAIL ") + "the signing script's signature verifies here\n";
	// And through the updater's own path, as if this copy were build 1 on
	// another commit (the folder needn't have a file for this computer).
	const Decision d = h.find(dir, pub, m.commit, std::string(40, '0'), 1);
	const bool findOk = d.result == Result::Offer || (d.result == Result::NotVerified && d.why == "the manifest has no file for this computer");
	report += std::string(findOk ? "  ok   " : "  FAIL ") + "the updater's check: " + d.why + "\n";
	bool all = sigOk && findOk;
	for (const File& f : m.files) {
		std::string fwhy;
		const bool ok = h.verifyFile(h.join(dir, f.name), f, fwhy);
		report += std::string(ok ? "  ok   " : "  FAIL ") + f.name + (fwhy.empty() ? "" : " -- " + fwhy) + "\n";
		all = all && ok;
	}
	return all;
}

}  // namespace updatesig
