// The sync self-test's insides (SyncTest*.cpp): the report, the FakeServer
// (a port of the design's ref/sim.py Server) and the scenario runner.
#pragma once

#include "SyncInternal.h"

#include <string>
#include <tuple>
#include <vector>

namespace clsync {
namespace test {

struct Report {
	std::string text;
	int passed = 0, failed = 0;
	std::string only;   // run only checks whose label contains this (CL_SYNC_TEST_ONLY)
	void line(bool ok, const std::string& label, const std::string& why = std::string()) {
		if (ok) passed++;
		else failed++;
		text += (ok ? "PASS " : "FAIL ") + label + (ok || why.empty() ? "" : " -- " + why) + "\n";
	}
	bool wanted(const std::string& label) const { return only.empty() || label.find(only) != std::string::npos; }
};

// A Crypto that passes through to the platform's, but can be told to fail its RNG.
struct TestCrypto : Crypto {
	Crypto& real;
	bool failRandom = false;
	explicit TestCrypto(Crypto& r) : real(r) {}
	bool random(uint8_t* out, size_t n) override { return !failRandom && real.random(out, n); }
	void sha256(const uint8_t* p, size_t n, uint8_t out[32]) override { real.sha256(p, n, out); }
	void hmacSha256(const uint8_t* k, size_t kl, const uint8_t* p, size_t n, uint8_t out[32]) override {
		real.hmacSha256(k, kl, p, n, out);
	}
	bool aesGcmSeal(const uint8_t k[32], const uint8_t nonce[12], const Bytes& aad, const Bytes& plain, Bytes& ct) override {
		return real.aesGcmSeal(k, nonce, aad, plain, ct);
	}
	bool aesGcmOpen(const uint8_t k[32], const uint8_t nonce[12], const Bytes& aad, const Bytes& ct, Bytes& plain) override {
		return real.aesGcmOpen(k, nonce, aad, ct, plain);
	}
	bool deflateRaw(const Bytes& in, Bytes& out) override { return real.deflateRaw(in, out); }
	bool inflateRaw(const Bytes& in, size_t maxOut, Bytes& out) override { return real.inflateRaw(in, maxOut, out); }
};

// The gate defaults of the vectors (ref/gate_defaults.json).
GateDefaults vectorDefaults();
// The defaults of one gate type: (gui, name, value).
std::vector<std::tuple<bool, std::string, std::string>> gateDefaultList(const std::string& lib);
// A fixture's text (ref/fixtures/<file>), "" if unknown.
std::string fixture(const std::string& file);

void vectorTests(Crypto&, Report&);
void pairVectorTests(Crypto&, Report&);   // SYNC.md 11.11 (SyncTestPair.cpp)
void engineTests(Crypto&, const std::string& tempDir, Report&);
void scenarioTests(Crypto&, const std::string& tempDir, Report&, Host* httpOnly, const std::string& serverBase);

}  // namespace test
}  // namespace clsync
