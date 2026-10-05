// The sync engine's self-test (SYNC.md 7.1 vectors, 7.2 scenarios) with
// OpenSSL's libcrypto and zlib for the Crypto hooks and libcurl for HTTP --
// the hooks the Linux app uses, so the engine is checked natively on a Mac or
// Linux machine without any app. mac/Tools/sync-selftest.sh builds and runs it.
//
//   sync_selftest [tempDir] [--server http://localhost:8787/api/sync/v1] [--only <name>]
//
// With --server (or CL_SYNC_URL), the single-device scenarios also run
// against the mock server (cedarlogic-site scripts/sync-mock-server.mjs).

#include "Sync.h"

#include <curl/curl.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <zlib.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

struct OpenSslCrypto : clsync::Crypto {
	bool failRandom = false;
	bool random(uint8_t* out, size_t n) override { return !failRandom && RAND_bytes(out, (int)n) == 1; }
	void sha256(const uint8_t* p, size_t n, uint8_t out[32]) override {
		unsigned int len = 32;
		EVP_Digest(p, n, out, &len, EVP_sha256(), nullptr);
	}
	void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* p, size_t n, uint8_t out[32]) override {
		unsigned int len = 32;
		HMAC(EVP_sha256(), key, (int)keyLen, p, n, out, &len);
	}
	bool aesGcmSeal(const uint8_t key[32], const uint8_t nonce[12], const clsync::Bytes& aad, const clsync::Bytes& plain,
	                clsync::Bytes& out) override {
		EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
		int len = 0, fin = 0;
		out.assign(plain.size() + 16, 0);
		bool ok = c && EVP_EncryptInit_ex(c, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
		          EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) == 1 &&
		          EVP_EncryptInit_ex(c, nullptr, nullptr, key, nonce) == 1 &&
		          EVP_EncryptUpdate(c, nullptr, &len, aad.data(), (int)aad.size()) == 1 &&
		          EVP_EncryptUpdate(c, out.data(), &len, plain.data(), (int)plain.size()) == 1 &&
		          EVP_EncryptFinal_ex(c, out.data() + len, &fin) == 1 &&
		          EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_GET_TAG, 16, out.data() + plain.size()) == 1;
		EVP_CIPHER_CTX_free(c);
		return ok;
	}
	bool aesGcmOpen(const uint8_t key[32], const uint8_t nonce[12], const clsync::Bytes& aad, const clsync::Bytes& ctTag,
	                clsync::Bytes& out) override {
		if (ctTag.size() < 16) return false;
		const size_t n = ctTag.size() - 16;
		EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
		int len = 0, fin = 0;
		out.assign(n + 1, 0);   // +1: never data() of an empty vector
		bool ok = c && EVP_DecryptInit_ex(c, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
		          EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) == 1 &&
		          EVP_DecryptInit_ex(c, nullptr, nullptr, key, nonce) == 1 &&
		          EVP_DecryptUpdate(c, nullptr, &len, aad.data(), (int)aad.size()) == 1 &&
		          EVP_DecryptUpdate(c, out.data(), &len, ctTag.data(), (int)n) == 1 &&
		          EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_TAG, 16, (void*)(ctTag.data() + n)) == 1 &&
		          EVP_DecryptFinal_ex(c, out.data() + len, &fin) == 1;   // 0: the tag didn't match
		EVP_CIPHER_CTX_free(c);
		out.resize(ok ? n : 0);
		return ok;
	}
	bool deflateRaw(const clsync::Bytes& in, clsync::Bytes& out) override {
		z_stream z{};
		if (deflateInit2(&z, 9, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) return false;
		out.assign(deflateBound(&z, (uLong)in.size()) + 16, 0);
		z.next_in = (Bytef*)in.data();
		z.avail_in = (uInt)in.size();
		z.next_out = out.data();
		z.avail_out = (uInt)out.size();
		const int r = deflate(&z, Z_FINISH);
		const size_t got = z.total_out;
		deflateEnd(&z);
		if (r != Z_STREAM_END) return false;
		out.resize(got);
		return true;
	}
	bool inflateRaw(const clsync::Bytes& in, size_t maxOut, clsync::Bytes& out) override {
		z_stream z{};
		if (inflateInit2(&z, -15) != Z_OK) return false;
		out.assign(maxOut + 1, 0);
		z.next_in = (Bytef*)in.data();
		z.avail_in = (uInt)in.size();
		z.next_out = out.data();
		z.avail_out = (uInt)out.size();
		const int r = inflate(&z, Z_FINISH);
		const size_t got = z.total_out;
		inflateEnd(&z);
		if (r != Z_STREAM_END || got > maxOut) { out.clear(); return false; }
		out.resize(got);
		return true;
	}
};

size_t collect(char* p, size_t size, size_t n, void* user) {
	static_cast<std::string*>(user)->append(p, size * n);
	return size * n;
}

// HTTP through libcurl; only http() is used by the self-test.
struct CurlHost : clsync::Host {
	clsync::HttpResponse http(const clsync::HttpRequest& r) override {
		clsync::HttpResponse out;
		CURL* c = curl_easy_init();
		if (!c) return out;
		std::string body, head;
		curl_slist* hs = nullptr;
		for (const auto& h : r.headers) hs = curl_slist_append(hs, (h.first + ": " + h.second).c_str());
		hs = curl_slist_append(hs, "Expect:");
		curl_easy_setopt(c, CURLOPT_URL, r.url.c_str());
		curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, r.method.c_str());
		curl_easy_setopt(c, CURLOPT_HTTPHEADER, hs);
		if (!r.body.empty() || r.method == "POST" || r.method == "PUT") {
			curl_easy_setopt(c, CURLOPT_POSTFIELDS, r.body.data());
			curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)r.body.size());
		}
		curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, collect);
		curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
		curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, collect);
		curl_easy_setopt(c, CURLOPT_HEADERDATA, &head);
		curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
		curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L);
		curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
		const CURLcode rc = curl_easy_perform(c);
		long status = 0;
		curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
		out.sent = rc == CURLE_OK || (rc != CURLE_COULDNT_CONNECT && rc != CURLE_COULDNT_RESOLVE_HOST);
		if (rc == CURLE_OK) {
			out.status = (int)status;
			out.body = body;
			size_t at = 0;
			while (at < head.size()) {
				size_t end = head.find("\r\n", at);
				if (end == std::string::npos) end = head.size();
				const std::string line = head.substr(at, end - at);
				const size_t colon = line.find(':');
				if (colon != std::string::npos) {
					std::string name = line.substr(0, colon), value = line.substr(colon + 1);
					for (char& ch : name) ch = (char)tolower((unsigned char)ch);
					while (!value.empty() && value[0] == ' ') value.erase(0, 1);
					out.headers[name] = value;
				}
				at = end + 2;
			}
		}
		curl_slist_free_all(hs);
		curl_easy_cleanup(c);
		return out;
	}
	void onMain(const std::function<void()>& fn) override { fn(); }
	std::string loadSecret() override { return std::string(); }
	bool saveSecret(const std::string&) override { return true; }
	void forgetSecret() override {}
	bool tryLock(const std::string&) override { return true; }
	void unlock() override {}
	void flushOpen(std::function<void()> done) override { done(); }
	clsync::WindowState windowState(const std::string&) override { return clsync::WindowState(); }
	void circuitReplaced(const std::string&, const std::string&) override {}
	void closeCircuit(const std::string&) override {}
	void libraryChanged() override {}
	void statusChanged(const clsync::Status&) override {}
	void notice(const std::string&) override {}
	void askMassDelete(int, std::function<void(bool)> answer) override { answer(true); }
	void askIncomingDeletes(int, const std::string&, std::function<void(bool)> answer) override { answer(true); }
};

}  // namespace

int main(int argc, char** argv) {
	std::string temp, server;
	if (const char* u = getenv("CL_SYNC_URL")) server = u;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--server") == 0 && i + 1 < argc) server = argv[++i];
		else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc) setenv("CL_SYNC_TEST_ONLY", argv[++i], 1);
		else temp = argv[i];
	}
	if (temp.empty()) {
		const char* t = getenv("TMPDIR");
		temp = std::string(t ? t : "/tmp") + "/cl-sync-selftest";
	}
	curl_global_init(CURL_GLOBAL_DEFAULT);
	OpenSslCrypto crypto;
	CurlHost host;
	std::string report;
	const bool ok = clsync::selfTest(crypto, temp, report, server.empty() ? nullptr : &host, server);
	fputs(report.c_str(), stdout);
	curl_global_cleanup();
	return ok ? 0 : 1;
}
