// The classroom core's self-test (CLASSROOM.md 7.1 vectors, 7.2 scenarios)
// with OpenSSL's libcrypto (SHA-256, HMAC, AES-GCM, PBKDF2, P-256) and zlib for
// the hooks and libcurl for HTTP (classroom_openssl.h) -- the hooks the Linux app will use, so the
// core is checked natively on a Mac or Linux machine without any app.
// mac/Tools/classroom-selftest.sh builds and runs it.
//
//   classroom_selftest [tempDir] [--vectors file.json] [--only <name>] [--c-api]
//                      [--server http://localhost:8788/api/classroom/v1 --live http://localhost:8788/api/live/v1]
//                      [--relay mac/Tools/classroom-ws-relay.mjs]
//
// --relay gives the over-HTTP tests WebSockets too (CLASSROOM.md 3.14): node runs
// that script and the core's socket hooks go to it over a pipe, so the live
// connection meets the real Worker. Without it the tests' devices have no
// sockets and live on held polls.
//
// --vectors checks another copy of the vectors file (the website's) instead of
// the embedded one, vectors only. --c-api runs it all through the C interface
// (cl_classroom_self_test) with C hooks over the same libraries, as the Mac
// app's Swift hooks would.

#include "CedarClassroom.h"
#include "ClassroomTest.h"
#include "classroom_openssl.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include <sys/wait.h>
#include <unistd.h>

namespace {

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

// WebSockets through node (classroom-ws-relay.mjs), one line per event each way. Every report is
// made under the lock and only for a socket still open, so after socketClose returns nothing more
// about it reaches the core (the promise of Host::socketOpen).
struct Relay {
	FILE* to = nullptr;
	FILE* from = nullptr;
	pid_t pid = -1;
	std::thread reader;
	std::mutex mu, wmu;
	std::map<int, void*> events;   // id -> the core's events (for cl_classroom_socket_*)
	bool start(const std::string& script) {
		int in[2], out[2];
		if (pipe(in) != 0 || pipe(out) != 0) return false;
		pid = fork();
		if (pid < 0) return false;
		if (pid == 0) {
			dup2(in[0], 0);
			dup2(out[1], 1);
			::close(in[0]);
			::close(in[1]);
			::close(out[0]);
			::close(out[1]);
			execlp("node", "node", script.c_str(), (char*)nullptr);
			_exit(127);
		}
		::close(in[0]);
		::close(out[1]);
		to = fdopen(in[1], "w");
		from = fdopen(out[0], "r");
		reader = std::thread([this] { read(); });
		return true;
	}
	void stop() {
		if (to) fclose(to);   // node sees the end of its input and exits
		to = nullptr;
		if (reader.joinable()) reader.join();
		if (from) fclose(from);
		if (pid > 0) waitpid(pid, nullptr, 0);
	}
	void write(const std::string& line) {
		std::lock_guard<std::mutex> lock(wmu);
		if (!to) return;
		fputs((line + "\n").c_str(), to);
		fflush(to);
	}
	void read() {
		char* buf = nullptr;
		size_t cap = 0;
		ssize_t n;
		while ((n = getline(&buf, &cap, from)) > 0) {
			std::string line(buf, (size_t)n);
			while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
			const size_t sp1 = line.find(' ');
			if (sp1 == std::string::npos) continue;
			const size_t sp2 = line.find(' ', sp1 + 1);
			const int id = atoi(line.substr(sp1 + 1, sp2 == std::string::npos ? std::string::npos : sp2 - sp1 - 1).c_str());
			const std::string arg = sp2 == std::string::npos ? std::string() : line.substr(sp2 + 1);
			std::lock_guard<std::mutex> lock(mu);
			auto it = events.find(id);
			if (it == events.end()) continue;
			if (line[0] == 'O') {
				cl_classroom_socket_opened(it->second, id);
			} else if (line[0] == 'T') {
				clsync::json::Value v;
				if (clsync::json::parse(arg, v) && v.isString()) cl_classroom_socket_text(it->second, id, v.s.c_str());
			} else if (line[0] == 'C') {
				void* ev = it->second;
				events.erase(it);
				cl_classroom_socket_closed(ev, id, atoi(arg.c_str()));
			}
		}
		free(buf);
	}
	bool open(int id, const std::string& url, const std::string& headerLines, void* ev) {
		clsync::json::Value o = clsync::json::Value::object(), h = clsync::json::Value::object();
		std::istringstream lines(headerLines);
		std::string l;
		while (std::getline(lines, l)) {
			if (!l.empty() && l.back() == '\r') l.pop_back();
			const size_t colon = l.find(": ");
			if (colon != std::string::npos) h.set(l.substr(0, colon), clsync::json::Value::string(l.substr(colon + 2)));
		}
		o.set("url", clsync::json::Value::string(url));
		o.set("headers", h);
		{
			std::lock_guard<std::mutex> lock(mu);
			events[id] = ev;
		}
		write("O " + std::to_string(id) + " " + clsync::json::write(o));
		return true;
	}
	void send(int id, const std::string& text) { write("S " + std::to_string(id) + " " + clsync::json::quote(text)); }
	void close(int id) {
		{
			std::lock_guard<std::mutex> lock(mu);
			events.erase(id);
		}
		write("X " + std::to_string(id) + " 1000");
	}
};
Relay* gRelay = nullptr;

// HTTP with curl, WebSockets through the relay.
struct RelayHost : CurlHost {
	bool socketOpen(int id, const std::string& url, const std::vector<std::pair<std::string, std::string>>& headers,
	                clclass::SocketEvents& events) override {
		std::string lines;
		for (const auto& kv : headers) lines += kv.first + ": " + kv.second + "\r\n";
		return gRelay->open(id, url, lines, &events);
	}
	void socketSend(int id, const std::string& text) override { gRelay->send(id, text); }
	void socketClose(int id, int) override { gRelay->close(id); }
};

bool cSocketOpen(void*, int id, const char* url, const char* headers, void* events) {
	return gRelay && gRelay->open(id, url, headers ? headers : "", events);
}
void cSocketSend(void*, int id, const char* text) {
	if (gRelay) gRelay->send(id, text);
}
void cSocketClose(void*, int id, int) {
	if (gRelay) gRelay->close(id);
}

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
	std::string temp, server, live, vectors, relay;
	bool cApi = false;
	if (const char* u = getenv("CL_CLASSROOM_URL")) server = u;
	if (const char* u = getenv("CL_LIVE_URL")) live = u;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--server") == 0 && i + 1 < argc) server = argv[++i];
		else if (strcmp(argv[i], "--live") == 0 && i + 1 < argc) live = argv[++i];
		else if (strcmp(argv[i], "--vectors") == 0 && i + 1 < argc) vectors = argv[++i];
		else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc) setenv("CL_CLASSROOM_TEST_ONLY", argv[++i], 1);
		else if (strcmp(argv[i], "--c-api") == 0) cApi = true;
		else if (strcmp(argv[i], "--relay") == 0 && i + 1 < argc) relay = argv[++i];
		else temp = argv[i];
	}
	if (temp.empty()) {
		const char* t = getenv("TMPDIR");
		temp = std::string(t ? t : "/tmp") + "/cl-classroom-selftest";
	}
	curl_global_init(CURL_GLOBAL_DEFAULT);
	OpenSslCrypto crypto;
	OpenSslCurve curve;
	CurlHost curlOnly;
	RelayHost relayHost;
	Relay relayProcess;
	if (!relay.empty()) {
		if (!relayProcess.start(relay)) {
			fprintf(stderr, "couldn't start node %s\n", relay.c_str());
			return 2;
		}
		gRelay = &relayProcess;
	}
	clclass::Host& host = gRelay ? (clclass::Host&)relayHost : (clclass::Host&)curlOnly;
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
		if (gRelay) {
			h.socket_open = cSocketOpen;
			h.socket_send = cSocketSend;
			h.socket_close = cSocketClose;
		}
		char* text = nullptr;
		ok = cl_classroom_self_test(&h, temp.c_str(), server.empty() ? nullptr : server.c_str(), live.empty() ? nullptr : live.c_str(),
		                            &text);
		report = text ? text : "no report\n";
		free(text);
	} else {
		ok = clclass::selfTest(crypto, curve, temp, report, server.empty() ? nullptr : &host, server, live);
	}
	fputs(report.c_str(), stdout);
	if (gRelay) relayProcess.stop();
	curl_global_cleanup();
	return ok ? 0 : 1;
}
