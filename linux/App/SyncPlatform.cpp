// What the sync engine needs from this machine (see SyncPlatform.h).

#include "SyncPlatform.h"

#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

#ifndef CL_VERSION
#define CL_VERSION "0"
#endif
#ifndef CL_GIT_COMMIT
#define CL_GIT_COMMIT "unknown"
#endif
#ifndef CL_FEEDBACK_KEY
#define CL_FEEDBACK_KEY ""
#endif

namespace syncplatform {

namespace {

// ---- Crypto ------------------------------------------------------------------------------

// Everything a converter makes from `in`, up to `limit` bytes; false if it refuses the data or
// would pass the limit.
bool convertAll(GConverter* conv, const clsync::Bytes& in, size_t limit, clsync::Bytes& out) {
	std::vector<uint8_t> buf(1 << 16);
	size_t used = 0;
	out.clear();
	for (;;) {
		gsize read = 0, written = 0;
		GError* err = nullptr;
		const GConverterResult r = g_converter_convert(conv, in.empty() ? (const void*)"" : (const void*)(in.data() + used),
		                                               in.size() - used, buf.data(), buf.size(), G_CONVERTER_INPUT_AT_END,
		                                               &read, &written, &err);
		if (err) {
			g_error_free(err);
			return false;
		}
		used += read;
		if (out.size() + written > limit) return false;
		out.insert(out.end(), buf.begin(), buf.begin() + written);
		if (r == G_CONVERTER_FINISHED) return true;
		if (read == 0 && written == 0) return false;   // wants more input than there is
	}
}

struct OpenSslCrypto : clsync::Crypto {
	bool random(uint8_t* out, size_t n) override { return RAND_bytes(out, (int)n) == 1; }
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
		GConverter* conv = G_CONVERTER(g_zlib_compressor_new(G_ZLIB_COMPRESSOR_FORMAT_RAW, 9));
		const bool ok = convertAll(conv, in, in.size() + in.size() / 8 + 4096, out);
		g_object_unref(conv);
		if (!ok) out.clear();
		return ok;
	}
	bool inflateRaw(const clsync::Bytes& in, size_t maxOut, clsync::Bytes& out) override {
		GConverter* conv = G_CONVERTER(g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_RAW));
		const bool ok = convertAll(conv, in, maxOut, out);
		g_object_unref(conv);
		if (!ok) out.clear();
		return ok;
	}
};

// ---- Files --------------------------------------------------------------------------------

std::string parentOf(const std::string& path) {
	const size_t slash = path.find_last_of('/');
	return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

std::string trimmed(const std::string& s) {
	const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
	return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

bool readAll(const std::string& path, std::string& out, size_t limit = 64u << 20) {
	gchar* data = nullptr;
	gsize len = 0;
	out.clear();
	if (!g_file_get_contents(path.c_str(), &data, &len, nullptr)) return false;
	if (len <= limit) out.assign(data, len);
	g_free(data);
	return len <= limit;
}

bool writeAll(int fd, const char* p, size_t n) {
	while (n > 0) {
		const ssize_t w = ::write(fd, p, n);
		if (w < 0) {
			if (errno == EINTR) continue;
			return false;
		}
		p += w;
		n -= (size_t)w;
	}
	return true;
}

// The folder, owner only; its parents (the app's data folder) as every folder is made.
bool makePrivateFolder(const std::string& dir) {
	if (dir.empty()) return false;
	const std::string parent = parentOf(dir);
	if (!parent.empty()) g_mkdir_with_parents(parent.c_str(), 0755);
	if (g_mkdir(dir.c_str(), 0700) != 0 && !g_file_test(dir.c_str(), G_FILE_TEST_IS_DIR)) return false;
	g_chmod(dir.c_str(), 0700);
	return true;
}

// ---- The curl program ----------------------------------------------------------------------

std::string curlPath() {
	if (const char* c = g_getenv("CL_SYNC_CURL")) {
		if (*c) return c;
	}
	gchar* p = g_find_program_in_path("curl");
	std::string out = p ? p : "";
	g_free(p);
	return out;
}

// A temp file made 0600 by the system (mkstemp), removed when this goes.
struct TempFile {
	std::string path;
	TempFile() = default;
	TempFile(const TempFile&) = delete;
	TempFile& operator=(const TempFile&) = delete;
	~TempFile() {
		if (!path.empty()) g_remove(path.c_str());
	}
	// Made, with `content` in it; false if that failed.
	bool make(const std::string& content) {
		gchar* name = nullptr;
		const int fd = g_file_open_tmp("cedarlogic-sync-XXXXXX", &name, nullptr);
		if (fd < 0) return false;
		path = name;
		g_free(name);
		const bool ok = content.empty() || writeAll(fd, content.data(), content.size());
		::close(fd);
		return ok;
	}
};

std::string oneLine(const std::string& s) {
	std::string out;
	for (char c : s) out += (c == '\r' || c == '\n') ? ' ' : c;
	return out;
}

// Response headers as curl -D writes them: every response (a 100 Continue, a redirect) in turn,
// each a status line and "name: value" lines; the last one counts.
void parseHeaders(const std::string& text, std::map<std::string, std::string>& out) {
	out.clear();
	size_t at = 0;
	while (at < text.size()) {
		size_t end = text.find('\n', at);
		if (end == std::string::npos) end = text.size();
		std::string line = text.substr(at, end - at);
		at = end + 1;
		while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
		if (line.compare(0, 5, "HTTP/") == 0) {
			out.clear();   // a new response begins
			continue;
		}
		const size_t colon = line.find(':');
		if (colon == std::string::npos || colon == 0) continue;
		std::string name = line.substr(0, colon);
		for (char& ch : name) ch = (char)g_ascii_tolower(ch);
		out[name] = trimmed(line.substr(colon + 1));
	}
}

}  // namespace

clsync::Crypto& crypto() {
	static OpenSslCrypto c;
	return c;
}

// ---- HTTP ---------------------------------------------------------------------------------------

bool isLocalHttp(const std::string& url) {
	if (url.compare(0, 7, "http://") != 0) return false;
	const size_t slash = url.find('/', 7);
	const std::string authority = url.substr(7, slash == std::string::npos ? std::string::npos : slash - 7);
	if (authority.find('@') != std::string::npos) return false;   // user:pass@host: the host isn't the one it seems
	for (const char* host : { "localhost", "127.0.0.1", "[::1]" }) {
		const size_t n = strlen(host);
		if (authority.compare(0, n, host) == 0 && (authority.size() == n || authority[n] == ':')) return true;
	}
	return false;
}

bool curlAvailable() { return !curlPath().empty(); }

clsync::HttpResponse http(const clsync::HttpRequest& r) {
	clsync::HttpResponse out;
	const bool local = isLocalHttp(r.url);
	if (r.url.compare(0, 8, "https://") != 0 && !local) return out;   // never plain http to anywhere else
	const std::string curl = curlPath();
	if (curl.empty()) return out;

	// What goes in files: the headers (the token among them) and the body.
	std::string headers = "Expect:\n";   // (no 100-continue wait before a big body)
	bool haveAgent = false;
	for (const auto& h : r.headers) {
		headers += oneLine(h.first) + ": " + oneLine(h.second) + "\n";
		if (g_ascii_strcasecmp(h.first.c_str(), "user-agent") == 0) haveAgent = true;
	}
	if (!haveAgent) headers += "User-Agent: CedarLogic\n";
	TempFile headersIn, bodyIn, headersOut, bodyOut;
	if (!headersIn.make(headers) || !headersOut.make("") || !bodyOut.make("")) return out;
	const bool hasBody = !r.body.empty();
	if (hasBody && !bodyIn.make(r.body)) return out;

	std::vector<std::string> args = { curl, "-q", "-sS", "--proto", local ? "=http,https" : "=https", "--proto-redir", "=https",
	                                  "--max-redirs", "0", "--max-time", "60", "--connect-timeout", "20", "-X", r.method,
	                                  "-H", "@" + headersIn.path, "-D", headersOut.path, "-o", bodyOut.path,
	                                  "-w", "%{http_code}" };
	if (local) {
		args.push_back("--noproxy");   // a test server on this machine isn't behind the proxy
		args.push_back("*");
	}
	if (hasBody) {
		args.push_back("--data-binary");
		args.push_back("@" + bodyIn.path);
	}
	args.push_back("--");
	args.push_back(r.url);
	std::vector<gchar*> argv;
	for (std::string& a : args) argv.push_back(&a[0]);
	argv.push_back(nullptr);

	gchar* stdoutText = nullptr;
	gint waitStatus = 0;
	GError* err = nullptr;
	const gboolean spawned = g_spawn_sync(nullptr, argv.data(), nullptr, (GSpawnFlags)G_SPAWN_STDERR_TO_DEV_NULL, nullptr, nullptr,
	                                      &stdoutText, nullptr, &waitStatus, &err);
	if (err) g_error_free(err);
	int exitCode = -1;
	if (spawned && WIFEXITED(waitStatus)) exitCode = WEXITSTATUS(waitStatus);
	// Nothing went out when curl couldn't resolve a name or connect (or set up TLS).
	out.sent = !(exitCode == 5 || exitCode == 6 || exitCode == 7 || exitCode == 35 || exitCode == 60 || exitCode == -1);
	if (exitCode == 0) {
		const int code = stdoutText ? atoi(stdoutText) : 0;
		if (code > 0) {
			out.status = code;
			std::string head;
			readAll(headersOut.path, head, 1 << 20);
			parseHeaders(head, out.headers);
			readAll(bodyOut.path, out.body);
		}
	}
	g_free(stdoutText);
	return out;
}

// ---- Where things are ----------------------------------------------------------------------------

std::string libraryRoot() { return std::string(g_get_user_data_dir()) + "/CedarLogic/Library"; }
std::string syncFolder() { return std::string(g_get_user_data_dir()) + "/CedarLogic/Sync"; }

std::string serverBase() {
	std::string url;
	if (const char* u = g_getenv("CL_SYNC_URL")) url = u;
	while (!url.empty() && url.back() == '/') url.pop_back();
	if (!url.empty() && ((url.compare(0, 8, "https://") == 0) || isLocalHttp(url))) return url;
	return "https://cedarlogic.netlify.app/api/sync/v1";
}

std::string appKey() {
	const char* v = g_getenv("CL_FEEDBACK_KEY");
	return v && *v ? v : CL_FEEDBACK_KEY;
}

std::string clientName() { return std::string("linux/") + CL_VERSION + "+" + std::string(CL_GIT_COMMIT).substr(0, 7); }

std::string defaultDeviceName() {
	std::string name;
	std::string info;
	if (readAll("/etc/machine-info", info, 1 << 16)) {
		size_t at = 0;
		while (at < info.size()) {
			size_t end = info.find('\n', at);
			if (end == std::string::npos) end = info.size();
			const std::string line = trimmed(info.substr(at, end - at));
			at = end + 1;
			if (line.compare(0, 16, "PRETTY_HOSTNAME=") != 0) continue;
			std::string v = trimmed(line.substr(16));
			if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front()) {
				v = v.substr(1, v.size() - 2);
				// (Backslash escapes inside the quotes.)
				std::string un;
				for (size_t i = 0; i < v.size(); i++) {
					if (v[i] == '\\' && i + 1 < v.size()) i++;
					un += v[i];
				}
				v = un;
			}
			name = v;
		}
	}
	if (trimmed(name).empty()) {
		const gchar* h = g_get_host_name();
		name = h ? h : "";
	}
	if (trimmed(name).empty()) name = "Linux computer";
	gchar* valid = g_utf8_make_valid(name.c_str(), -1);
	std::string out = valid ? valid : "";
	g_free(valid);
	if (g_utf8_strlen(out.c_str(), -1) > 64) out.resize((size_t)(g_utf8_offset_to_pointer(out.c_str(), 64) - out.c_str()));
	return out;
}

// ---- The secret and the lock ----------------------------------------------------------------------

std::string loadSecret(const std::string& dir) {
	std::string text;
	if (dir.empty() || !readAll(dir + "/secret", text, 4096)) return std::string();
	return trimmed(text);
}

bool saveSecret(const std::string& dir, const std::string& code) {
	if (!makePrivateFolder(dir)) return false;
	// A temp file made 0600 and moved over the old one: never readable by anyone else, not for an instant.
	const std::string tmp = dir + "/secret.tmp" + std::to_string((long)getpid());
	const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd < 0) return false;
	const std::string data = code + "\n";
	bool ok = writeAll(fd, data.data(), data.size()) && ::fsync(fd) == 0;
	ok = ::close(fd) == 0 && ok;
	ok = ok && g_rename(tmp.c_str(), (dir + "/secret").c_str()) == 0;
	if (!ok) g_remove(tmp.c_str());
	return ok;
}

void forgetSecret(const std::string& dir) {
	if (!dir.empty()) g_remove((dir + "/secret").c_str());
}

namespace {
std::mutex gLockMutex;
int gLockFd = -1;
}  // namespace

bool tryLock(const std::string& path) {
	std::lock_guard<std::mutex> g(gLockMutex);
	if (gLockFd >= 0) return true;
	const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
	if (fd < 0) return false;
	if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
		::close(fd);
		return false;
	}
	gLockFd = fd;
	return true;
}

void unlock() {
	std::lock_guard<std::mutex> g(gLockMutex);
	if (gLockFd < 0) return;
	::flock(gLockFd, LOCK_UN);
	::close(gLockFd);
	gLockFd = -1;
}

// ---- Tests and tools -------------------------------------------------------------------------------

bool selfTest(const std::string& tempDir, const std::string& serverBase, const std::string& only, std::string& report) {
	if (!only.empty()) g_setenv("CL_SYNC_TEST_ONLY", only.c_str(), TRUE);
	HeadlessHost host;
	return clsync::selfTest(crypto(), tempDir, report, serverBase.empty() ? nullptr : &host, serverBase);
}

}  // namespace syncplatform
