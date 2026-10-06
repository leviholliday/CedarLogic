// The classroom core's self-test (CLASSROOM.md 7.1 vectors, 7.2 scenarios)
// with OpenSSL's libcrypto (SHA-256, HMAC, AES-GCM, PBKDF2, P-256) and zlib for
// the hooks and libcurl for HTTP -- the hooks the Linux app will use, so the
// core is checked natively on a Mac or Linux machine without any app.
// mac/Tools/classroom-selftest.sh builds and runs it.
//
//   classroom_selftest [tempDir] [--vectors file.json] [--only <name>] [--c-api]
//                      [--server http://localhost:8788/api/classroom/v1 --live http://localhost:8788/api/live/v1]
//
// --vectors checks another copy of the vectors file (the website's) instead of
// the embedded one, vectors only. --c-api runs it all through the C interface
// (cl_classroom_self_test) with C hooks over the same libraries, as the Mac
// app's Swift hooks would.

#include "CedarClassroom.h"
#include "ClassroomTest.h"

#include <curl/curl.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/obj_mac.h>
#include <openssl/rand.h>
#include <zlib.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace {

struct OpenSslCrypto : clsync::Crypto {
	bool random(uint8_t* out, size_t n) override { return RAND_bytes(out, (int)n) == 1; }
	void sha256(const uint8_t* p, size_t n, uint8_t out[32]) override {
		unsigned int len = 32;
		EVP_Digest(p, n, out, &len, EVP_sha256(), nullptr);
	}
	void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* p, size_t n, uint8_t out[32]) override {
		unsigned int len = 32;
		static const uint8_t none = 0;
		HMAC(EVP_sha256(), keyLen ? key : &none, (int)keyLen, n ? p : &none, n, out, &len);
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
		out.assign(n + 1, 0);
		bool ok = c && EVP_DecryptInit_ex(c, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
		          EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) == 1 &&
		          EVP_DecryptInit_ex(c, nullptr, nullptr, key, nonce) == 1 &&
		          EVP_DecryptUpdate(c, nullptr, &len, aad.data(), (int)aad.size()) == 1 &&
		          EVP_DecryptUpdate(c, out.data(), &len, ctTag.data(), (int)n) == 1 &&
		          EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_TAG, 16, (void*)(ctTag.data() + n)) == 1 &&
		          EVP_DecryptFinal_ex(c, out.data() + len, &fin) == 1;
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
	// Streamed, counted as it goes: it stops as soon as the output passes maxOut (the deflate bomb).
	bool inflateRaw(const clsync::Bytes& in, size_t maxOut, clsync::Bytes& out) override {
		z_stream z{};
		if (inflateInit2(&z, -15) != Z_OK) return false;
		out.clear();
		uint8_t chunk[65536];
		z.next_in = (Bytef*)in.data();
		z.avail_in = (uInt)in.size();
		int r = Z_OK;
		while (r != Z_STREAM_END) {
			z.next_out = chunk;
			z.avail_out = sizeof chunk;
			r = inflate(&z, Z_NO_FLUSH);
			const size_t got = sizeof chunk - z.avail_out;
			if (out.size() + got > maxOut) { r = Z_DATA_ERROR; break; }
			out.insert(out.end(), chunk, chunk + got);
			if (r != Z_OK && r != Z_STREAM_END) break;
			if (r == Z_OK && got == 0 && z.avail_in == 0) { r = Z_DATA_ERROR; break; }   // cut short
		}
		inflateEnd(&z);
		if (r != Z_STREAM_END) { out.clear(); return false; }
		return true;
	}
};

// P-256 on OpenSSL's EC_POINT arithmetic (the uncompressed X9.63 form throughout).
struct OpenSslCurve : clclass::Curve {
	EC_GROUP* g = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
	~OpenSslCurve() override { EC_GROUP_free(g); }
	bool scalar(const uint8_t d[32], BIGNUM* out) {
		BN_bin2bn(d, 32, out);
		return !BN_is_zero(out) && BN_cmp(out, EC_GROUP_get0_order(g)) < 0;
	}
	bool p256Public(const uint8_t d[32], uint8_t pub[65]) override {
		BN_CTX* ctx = BN_CTX_new();
		BIGNUM* k = BN_new();
		EC_POINT* p = EC_POINT_new(g);
		bool ok = scalar(d, k) && EC_POINT_mul(g, p, k, nullptr, nullptr, ctx) == 1 &&
		          EC_POINT_point2oct(g, p, POINT_CONVERSION_UNCOMPRESSED, pub, 65, ctx) == 65;
		EC_POINT_free(p);
		BN_clear_free(k);
		BN_CTX_free(ctx);
		return ok;
	}
	bool p256Generate(uint8_t d[32], uint8_t pub[65]) override {
		for (int i = 0; i < 8; i++)
			if (RAND_bytes(d, 32) == 1 && p256Public(d, pub)) return true;
		return false;
	}
	bool p256Ecdh(const uint8_t d[32], const uint8_t peer[65], uint8_t x[32]) override {
		if (peer[0] != 4) return false;
		BN_CTX* ctx = BN_CTX_new();
		BIGNUM *k = BN_new(), *bx = BN_new();
		EC_POINT *q = EC_POINT_new(g), *s = EC_POINT_new(g);
		bool ok = scalar(d, k) && EC_POINT_oct2point(g, q, peer, 65, ctx) == 1 && EC_POINT_is_on_curve(g, q, ctx) == 1 &&
		          EC_POINT_mul(g, s, nullptr, q, k, ctx) == 1 && !EC_POINT_is_at_infinity(g, s) &&
		          EC_POINT_get_affine_coordinates(g, s, bx, nullptr, ctx) == 1 && BN_bn2binpad(bx, x, 32) == 32;
		EC_POINT_free(q);
		EC_POINT_free(s);
		BN_clear_free(k);
		BN_free(bx);
		BN_CTX_free(ctx);
		return ok;
	}
	bool pbkdf2Sha256(clsync::Crypto&, const uint8_t* pw, size_t pwLen, const uint8_t* salt, size_t saltLen, uint32_t rounds,
	                  uint8_t out[32]) override {
		return PKCS5_PBKDF2_HMAC((const char*)pw, (int)pwLen, salt, (int)saltLen, (int)rounds, EVP_sha256(), 32, out) == 1;
	}
};

size_t collect(char* p, size_t size, size_t n, void* user) {
	static_cast<std::string*>(user)->append(p, size * n);
	return size * n;
}

clsync::HttpResponse curlHttp(const clsync::HttpRequest& r) {
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
		std::istringstream lines(head);
		std::string line;
		while (std::getline(lines, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
			const size_t colon = line.find(':');
			if (colon == std::string::npos) continue;
			std::string name = line.substr(0, colon), value = line.substr(colon + 1);
			for (char& ch : name) ch = (char)tolower((unsigned char)ch);
			while (!value.empty() && value[0] == ' ') value.erase(0, 1);
			out.headers[name] = value;
		}
	}
	curl_slist_free_all(hs);
	curl_easy_cleanup(c);
	return out;
}

// HTTP only; the rest is never called by the over-HTTP tests.
struct CurlHost : clclass::Host {
	clsync::HttpResponse http(const clsync::HttpRequest& r) override { return curlHttp(r); }
	void onMain(const std::function<void()>& fn) override { fn(); }
	std::string loadFile(const std::string&) override { return std::string(); }
	bool saveFile(const std::string&, const std::string&) override { return true; }
	void removeTree(const std::string&) override {}
	bool tryLock(const std::string&) override { return true; }
	void unlock() override {}
	void classesChanged() override {}
	void liveChanged(const std::string&, const clclass::Live&) override {}
	void answersChanged(const std::string&, const clclass::AnswerCounts&) override {}
	void statusChanged(const std::string&, const clclass::Status&) override {}
	void notice(const std::string&) override {}
	std::vector<std::pair<std::string, std::string>> syncSideRecords() override { return {}; }
	void syncPutSide(const std::string&, const std::string&) override {}
	void syncDeleteSide(const std::string&) override {}
};

// ---- the same hooks as C functions (CedarClassroom.h), for --c-api ----

OpenSslCrypto* gCrypto = nullptr;
OpenSslCurve* gCurve = nullptr;

bool cRandom(void*, uint8_t* out, size_t n) { return gCrypto->random(out, n); }
void cSha(void*, const uint8_t* p, size_t n, uint8_t out[32]) { gCrypto->sha256(p, n, out); }
void cHmac(void*, const uint8_t* k, size_t kl, const uint8_t* p, size_t n, uint8_t out[32]) { gCrypto->hmacSha256(k, kl, p, n, out); }
bool cSeal(void*, const uint8_t key[32], const uint8_t nonce[12], const uint8_t* aad, size_t aadLen, const uint8_t* plain, size_t n,
           uint8_t* out) {
	clsync::Bytes ct;
	if (!gCrypto->aesGcmSeal(key, nonce, clsync::Bytes(aad, aad + aadLen), clsync::Bytes(plain, plain + n), ct)) return false;
	memcpy(out, ct.data(), ct.size());
	return true;
}
bool cOpen(void*, const uint8_t key[32], const uint8_t nonce[12], const uint8_t* aad, size_t aadLen, const uint8_t* ct, size_t n,
           uint8_t* out) {
	clsync::Bytes plain;
	if (!gCrypto->aesGcmOpen(key, nonce, clsync::Bytes(aad, aad + aadLen), clsync::Bytes(ct, ct + n), plain)) return false;
	if (!plain.empty()) memcpy(out, plain.data(), plain.size());
	return true;
}
uint8_t* copyOut(const clsync::Bytes& b, size_t* outLen) {
	uint8_t* p = (uint8_t*)malloc(b.size() + 1);
	if (!b.empty()) memcpy(p, b.data(), b.size());
	*outLen = b.size();
	return p;
}
uint8_t* cDeflate(void*, const uint8_t* in, size_t n, size_t* outLen) {
	clsync::Bytes out;
	return gCrypto->deflateRaw(clsync::Bytes(in, in + n), out) ? copyOut(out, outLen) : nullptr;
}
uint8_t* cInflate(void*, const uint8_t* in, size_t n, size_t maxOut, size_t* outLen) {
	clsync::Bytes out;
	return gCrypto->inflateRaw(clsync::Bytes(in, in + n), maxOut, out) ? copyOut(out, outLen) : nullptr;
}
void cHttp(void*, const char* method, const char* url, const char* headers, const uint8_t* body, size_t bodyLen, int* status,
           bool* sent, char** respHeaders, uint8_t** respBody, size_t* respLen) {
	clsync::HttpRequest r;
	r.method = method;
	r.url = url;
	std::istringstream lines(headers);
	std::string line;
	while (std::getline(lines, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		const size_t colon = line.find(": ");
		if (colon != std::string::npos) r.headers.emplace_back(line.substr(0, colon), line.substr(colon + 2));
	}
	r.body.assign((const char*)body, bodyLen);
	const clsync::HttpResponse resp = curlHttp(r);
	*status = resp.status;
	*sent = resp.sent;
	std::string h;
	for (const auto& kv : resp.headers) h += kv.first + ": " + kv.second + "\r\n";
	*respHeaders = strdup(h.c_str());
	*respBody = (uint8_t*)malloc(resp.body.size() + 1);
	memcpy(*respBody, resp.body.data(), resp.body.size());
	*respLen = resp.body.size();
}
bool cGenerate(void*, uint8_t d[32], uint8_t pub[65]) { return gCurve->p256Generate(d, pub); }
bool cPublic(void*, const uint8_t d[32], uint8_t pub[65]) { return gCurve->p256Public(d, pub); }
bool cEcdh(void*, const uint8_t d[32], const uint8_t peer[65], uint8_t x[32]) { return gCurve->p256Ecdh(d, peer, x); }
bool cPbkdf2(void*, const uint8_t* pw, size_t pwLen, const uint8_t* salt, size_t saltLen, uint32_t rounds, uint8_t out[32]) {
	return gCurve->pbkdf2Sha256(*gCrypto, pw, pwLen, salt, saltLen, rounds, out);
}

}  // namespace

int main(int argc, char** argv) {
	std::string temp, server, live, vectors;
	bool cApi = false;
	if (const char* u = getenv("CL_CLASSROOM_URL")) server = u;
	if (const char* u = getenv("CL_LIVE_URL")) live = u;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--server") == 0 && i + 1 < argc) server = argv[++i];
		else if (strcmp(argv[i], "--live") == 0 && i + 1 < argc) live = argv[++i];
		else if (strcmp(argv[i], "--vectors") == 0 && i + 1 < argc) vectors = argv[++i];
		else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc) setenv("CL_CLASSROOM_TEST_ONLY", argv[++i], 1);
		else if (strcmp(argv[i], "--c-api") == 0) cApi = true;
		else temp = argv[i];
	}
	if (temp.empty()) {
		const char* t = getenv("TMPDIR");
		temp = std::string(t ? t : "/tmp") + "/cl-classroom-selftest";
	}
	curl_global_init(CURL_GLOBAL_DEFAULT);
	OpenSslCrypto crypto;
	OpenSslCurve curve;
	CurlHost host;
	std::string report;
	bool ok;
	if (!vectors.empty()) {
		std::ifstream f(vectors, std::ios::binary);
		std::stringstream s;
		s << f.rdbuf();
		clsync::test::Report r;
		if (const char* o = getenv("CL_CLASSROOM_TEST_ONLY")) r.only = o;
		clclass::test::vectorTests(crypto, curve, r, s.str());
		report = r.text + std::to_string(r.passed) + " passed, " + std::to_string(r.failed) + " failed\n";
		ok = r.failed == 0;
	} else if (cApi) {
		gCrypto = &crypto;
		gCurve = &curve;
		CLSyncHooks s;
		memset(&s, 0, sizeof s);
		s.random = cRandom;
		s.sha256 = cSha;
		s.hmac_sha256 = cHmac;
		s.aes_gcm_seal = cSeal;
		s.aes_gcm_open = cOpen;
		s.deflate_raw = cDeflate;
		s.inflate_raw = cInflate;
		s.http = cHttp;
		CLClassroomHooks h;
		memset(&h, 0, sizeof h);
		h.sync = &s;
		h.p256_generate = cGenerate;
		h.p256_public = cPublic;
		h.p256_ecdh = cEcdh;
		h.pbkdf2_sha256 = cPbkdf2;
		char* text = nullptr;
		ok = cl_classroom_self_test(&h, temp.c_str(), server.empty() ? nullptr : server.c_str(), live.empty() ? nullptr : live.c_str(),
		                            &text);
		report = text ? text : "no report\n";
		free(text);
	} else {
		ok = clclass::selfTest(crypto, curve, temp, report, server.empty() ? nullptr : &host, server, live);
	}
	fputs(report.c_str(), stdout);
	curl_global_cleanup();
	return ok ? 0 : 1;
}
