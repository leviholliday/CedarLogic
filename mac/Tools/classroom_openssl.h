// The classroom core's hooks on OpenSSL's libcrypto (SHA-256, HMAC, AES-GCM,
// PBKDF2, P-256), zlib and libcurl -- the hooks the Linux app will use -- for
// the tools that run the core without an app: classroom_selftest.cpp and
// classroom_interop.cpp. One translation unit each (everything is in an
// unnamed namespace).
#pragma once

#include "Classroom.h"

#include <curl/curl.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/obj_mac.h>
#include <openssl/rand.h>
#include <zlib.h>

#include <cctype>
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

}  // namespace
