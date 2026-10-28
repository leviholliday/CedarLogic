// Signed preview updates for the native Windows and Linux apps: one copy of
// this code in both (update-signing/ is outside the engine's folders, so the
// Mac engine stays the same on every branch).
//
// CI publishes each test build to a rolling GitHub release, but it can't sign
// it: the signing key is in the owner's Keychain, never on GitHub. After CI
// has published, scripts/sign-preview.sh downloads the release's files,
// writes a manifest of them and signs it with that key (Ed25519), and uploads
// both to the release:
//
//   SHA256SUMS      the manifest, plain text (below)
//   SHA256SUMS.sig  64 bytes: the raw Ed25519 signature of SHA256SUMS
//
// The apps take an update only when the signature checks out against the
// public key built into them (kPublicKey), the manifest is for this release
// tag and for the commit the tag is on now, its build is newer than the
// running one, and the file downloaded has the size and SHA-256 the manifest
// gives it. Anything else -- no manifest yet (CI has published but no one
// has signed), a bad signature, an old manifest left over, a downgrade --
// means no update is offered, and the reason is logged.
//
// The manifest, line by line, each ending in "\n" (no "\r", no blank lines,
// nothing else; the order is fixed):
//
//   CedarLogic preview manifest 1
//   tag linux-native-testing
//   commit <the 40 lowercase hex digits of the commit the release was built from>
//   build <the commit's committer time, in Unix seconds>
//   file <name> <size in bytes> <sha256, 64 lowercase hex digits>
//   file ...                       (one or more; names unique)
//
// "build" only ever grows from one published commit to the next (committer
// times do), and each app has its own built in (CL_BUILD_TIME, from the same
// commit), so a signed manifest for an older build -- replayed, or left on a
// tag moved back -- is refused as not newer. Names are plain file names
// ([A-Za-z0-9._+-], not starting with "."), never paths.
//
// The signature check is Monocypher 4.0.2's (update-signing/monocypher,
// BSD-2-Clause or CC0): RFC 8032 Ed25519, with S < L checked. SHA-256 of the
// downloaded file is each platform's own (GLib on Linux, BCrypt on Windows).

#ifndef CL_UPDATE_SIGNATURE_H
#define CL_UPDATE_SIGNATURE_H

#include <functional>
#include <string>
#include <vector>

namespace updatesig {

// The public half of the preview signing key (the private half is only in
// the owner's Keychain). scripts/sign-preview.sh and linux/install.sh have
// the same key; CI checks that all three agree.
extern const unsigned char kPublicKey[32];

// The manifest's file names.
extern const char* const kManifestName;    // "SHA256SUMS"
extern const char* const kSignatureName;   // "SHA256SUMS.sig"

// True only when `signature` is exactly 64 bytes and is a valid Ed25519
// signature of `message` by `publicKey`.
bool verifySignature(const std::string& message, const std::string& signature, const unsigned char publicKey[32]);

struct File {
	std::string name;
	long long size = 0;
	std::string sha256;   // 64 lowercase hex digits
};

struct Manifest {
	std::string tag, commit;
	long long build = 0;
	std::vector<File> files;
};

// Strict: anything not exactly in the format above is refused, with why.
bool parseManifest(const std::string& text, Manifest& out, std::string& why);
// The manifest's text, as the signing script writes it.
std::string formatManifest(const Manifest& m);

bool safeName(const std::string& name);
bool isHex(const std::string& s, size_t length);

enum class Result {
	Offer,        // a verified, newer build: `file` is the one to download
	UpToDate,     // the release is this build (or not newer than it)
	NotVerified,  // there's something else on the release, but it doesn't verify
};

struct Decision {
	Result result = Result::NotVerified;
	std::string why;      // for the log
	Manifest manifest;
	File file;
};

// The whole decision, given what the release has. `releaseCommit` is the
// commit the release's tag points at now (GitHub's answer), `runningCommit`
// and `runningBuild` this copy's own (CL_GIT_COMMIT, CL_BUILD_TIME).
// `ours` picks this copy's file from the manifest's names (by CPU and kind);
// exactly one must match.
Decision decide(bool haveManifest, const std::string& manifestText, bool haveSignature, const std::string& signature,
                const unsigned char publicKey[32], const std::string& tag, const std::string& releaseCommit,
                const std::string& runningCommit, long long runningBuild, const std::function<bool(const std::string&)>& ours);

// A downloaded file against the manifest's entry for it.
bool checkDownload(const File& expected, long long size, const std::string& sha256, std::string& why);

// ---- Self-test ----------------------------------------------------------------
// Fake releases in a folder, signed with a throwaway test key, put through
// the platform updater's own code (what `find` and `verifyFile` wrap): a
// good one, a bad signature, another key, an edited manifest, a missing or
// truncated signature, a stale manifest (from before the tag moved, and with
// the new files under an old tag), a wrong hash, a wrong size, a downgrade, a
// replay of this build, another platform's manifest, no file for this CPU.

struct SelfTestHooks {
	std::string tag;         // the platform's release tag
	std::string assetName;   // the file this copy would download
	std::function<bool(const std::string& dir)> makeDir;
	std::function<bool(const std::string& path, const std::string& bytes)> writeFile;
	// The platform's SHA-256 of some bytes, in hex (checked against known
	// answers first, then used to write the fake manifests).
	std::function<std::string(const std::string& bytes)> sha256Hex;
	std::function<std::string(const std::string& dir, const std::string& name)> join;
	// The updater's own check, reading the release's files from `dir`.
	std::function<Decision(const std::string& dir, const unsigned char publicKey[32], const std::string& releaseCommit,
	                       const std::string& runningCommit, long long runningBuild)> find;
	// The updater's own check of a downloaded file (its SHA-256 and size).
	std::function<bool(const std::string& path, const File& expected, std::string& why)> verifyFile;
};

// Adds a line per case to `report`; true when every case did what it should.
bool selfTest(const std::string& dir, const SelfTestHooks& hooks, std::string& report);

// A release folder signed by someone else (the signing script, with a key
// made for the test): every file in its manifest is checked against
// `publicKeyHex`. For CI, to show the script's openssl signatures verify here.
bool checkSignedFolder(const std::string& dir, const std::string& publicKeyHex, const SelfTestHooks& hooks,
                       const std::function<bool(const std::string& path, std::string& bytes)>& readFile, std::string& report);

// For tests: Ed25519 signing with a 32-byte seed (Monocypher's), and hex.
std::string testSign(const std::string& message, const unsigned char seed[32], unsigned char publicKeyOut[32]);
std::string toHex(const unsigned char* bytes, size_t n);
bool fromHex(const std::string& hex, std::string& bytes);

}  // namespace updatesig

#endif  // CL_UPDATE_SIGNATURE_H
