// Checks of sync on this machine, with the hooks the app itself uses
// (App/SyncPlatform.cpp: libcrypto, GIO's zlib, the curl program), no window needed.
//
//   sync_check [--temp <dir>] [--only <name>] [--server <url>]
//              [--curl-check <url>] [--roundtrip <url> [<a.cdl> <b.cdl>]] [--new-code]
//
//   (no options)   the engine's self-test: SYNC.md 7.1's vectors and 7.2's scenarios on its
//                  in-process server, and the engine's threads and C interface.
//   --server       the same, and the single-device scenarios again over HTTP against the
//                  website's mock server (cedarlogic-site: scripts/sync-mock-server.mjs).
//   --curl-check   one request through the curl program with a bearer token, and one that fails:
//                  the token is never on curl's command line, its header file is 0600, and every
//                  temp file is gone afterwards. Against any server (Tools/sync_mock.py will do).
//   --roundtrip    two libraries and two engines against a server: turn on, preview, link,
//                  rename, add, delete, delete the synced copy. Against any server.
//
//   --new-code     prints a fresh sync code (for a picture of Settings with sync turned on).
//
// Exit status 0 when everything passed. See Tools/sync-check.sh and the workflow.

#include "../App/SyncPlatform.h"
#include "Sync.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <sys/stat.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace {

int gFails = 0;

void ok(bool cond, const std::string& what) {
	printf("%s %s\n", cond ? "PASS" : "FAIL", what.c_str());
	fflush(stdout);
	if (!cond) gFails++;
}

std::string slurp(const std::string& path) {
	gchar* data = nullptr;
	gsize len = 0;
	if (!g_file_get_contents(path.c_str(), &data, &len, nullptr)) return std::string();
	std::string out(data, len);
	g_free(data);
	return out;
}

void spit(const std::string& path, const std::string& text) { g_file_set_contents(path.c_str(), text.data(), (gssize)text.size(), nullptr); }

std::vector<std::string> listDir(const std::string& dir) {
	std::vector<std::string> out;
	if (GDir* d = g_dir_open(dir.c_str(), 0, nullptr)) {
		while (const gchar* n = g_dir_read_name(d)) out.push_back(n);
		g_dir_close(d);
	}
	return out;
}

void removeTree(const std::string& path) {
	if (g_file_test(path.c_str(), G_FILE_TEST_IS_DIR) && !g_file_test(path.c_str(), G_FILE_TEST_IS_SYMLINK)) {
		for (const std::string& n : listDir(path)) removeTree(path + "/" + n);
		g_rmdir(path.c_str());
	} else {
		g_remove(path.c_str());
	}
}

bool waitFor(int ms, const std::function<bool()>& cond) {
	const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
	while (!cond()) {
		if (std::chrono::steady_clock::now() > end) return false;
		std::this_thread::sleep_for(std::chrono::milliseconds(25));
	}
	return true;
}

// ---- --curl-check -----------------------------------------------------------------------

int curlCheck(const std::string& url, const std::string& dir) {
	// Where temp files go is decided once, before anything asks: a folder of our own, to see it empty.
	const std::string tmp = dir + "/tmp";
	g_mkdir_with_parents(tmp.c_str(), 0700);
	g_setenv("TMPDIR", tmp.c_str(), TRUE);
	const std::string log = dir + "/curl.log", wrapper = dir + "/curl-wrapper.sh";
	gchar* real = g_find_program_in_path("curl");
	if (!real) {
		ok(false, "curl is installed");
		return 1;
	}
	// A wrapper that notes each argument and the mode of the header file, then runs the real curl.
	spit(wrapper, std::string("#!/bin/sh\nlog=\"$CL_SYNC_CURL_LOG\"\nprev=\"\"\nfor a in \"$@\"; do\n  printf 'ARG %s\\n' \"$a\" >> \"$log\"\n"
	                          "  if [ \"$prev\" = \"-H\" ]; then f=\"${a#@}\"; printf 'MODE %s\\n' \"$(ls -ld \"$f\" | cut -c1-10)\" >> \"$log\"; fi\n"
	                          "  prev=\"$a\"\ndone\nexec ") + real + " \"$@\"\n");
	g_free(real);
	g_chmod(wrapper.c_str(), 0700);
	g_setenv("CL_SYNC_CURL", wrapper.c_str(), TRUE);
	g_setenv("CL_SYNC_CURL_LOG", log.c_str(), TRUE);

	const std::string token = "SECRETTOKEN-0123456789abcdefghijklmnopqrstuv";
	clsync::HttpRequest r;
	r.method = "GET";
	r.url = url + "/health";
	r.headers = { { "Authorization", "Bearer " + token }, { "x-cedarlogic-key", "KEYSECRET-1234" } };
	const clsync::HttpResponse good = syncplatform::http(r);
	ok(good.status == 200 && good.body.find("\"ok\"") != std::string::npos, "a request through curl is answered (200 from /health)");
	ok(good.headers.count("date") == 1, "...and its Date header is read");
	std::string text = slurp(log);
	ok(text.find("SECRETTOKEN") == std::string::npos && text.find("KEYSECRET") == std::string::npos,
	   "the token and the key aren't on curl's command line");
	ok(text.find("ARG -H\nARG @") != std::string::npos, "the headers go in a file (-H @file)");
	ok(text.find("MODE -rw-------") != std::string::npos, "...a file only its owner can read");
	ok(listDir(tmp).empty(), "no temp file is left after a request");

	// A POST with a body, then one that can't connect (nothing listens on port 9).
	spit(log, "");
	r.method = "POST";
	r.url = url + "/spaces/00000000000000000000000000000000/fetch";
	r.body = "{\"ids\":[]}";
	const clsync::HttpResponse posted = syncplatform::http(r);
	ok(posted.status == 401 || posted.status == 404, "a POST with a body reaches the server (not a stray 0)");
	text = slurp(log);
	ok(text.find("SECRETTOKEN") == std::string::npos && text.find("--data-binary\nARG @") != std::string::npos,
	   "...its body goes in a file too, and no secret is on the command line");
	ok(listDir(tmp).empty(), "no temp file is left after a POST");
	r.method = "GET";
	r.url = "http://127.0.0.1:9/api/sync/v1/health";
	r.body.clear();
	const clsync::HttpResponse refused = syncplatform::http(r);
	ok(refused.status == 0 && !refused.sent, "a connection that is refused is status 0, nothing sent");
	ok(listDir(tmp).empty(), "no temp file is left after a failure");
	r.url = "http://example.com/api/sync/v1/health";
	const clsync::HttpResponse plain = syncplatform::http(r);
	ok(plain.status == 0 && !plain.sent, "plain http to another host isn't even tried");
	ok(!syncplatform::isLocalHttp("http://localhost:80@evil.example/x") && syncplatform::isLocalHttp("http://localhost:8787/api") &&
	   syncplatform::isLocalHttp("http://127.0.0.1/x") && syncplatform::isLocalHttp("http://[::1]:5/x") && !syncplatform::isLocalHttp("http://localhost.evil.example/x"),
	   "only localhost counts as local, user:pass@ tricks don't");
	r.url = url + "/health";
	r.method = "GET";
	g_unsetenv("CL_SYNC_CURL");
	return gFails ? 1 : 0;
}

// ---- --roundtrip ---------------------------------------------------------------------------

struct Device {
	std::string name, root, syncDir, tmp;
	std::unique_ptr<syncplatform::HeadlessHost> host;
	std::unique_ptr<clsync::Engine> engine;

	Device(const std::string& base, const std::string& nm, const std::string& serverBase) : name(nm) {
		root = base + "/" + nm + "/Library";
		syncDir = base + "/" + nm + "/Sync";
		g_mkdir_with_parents(root.c_str(), 0755);
		host.reset(new syncplatform::HeadlessHost(syncDir));
		clsync::Config c;
		c.libraryRoot = root;
		c.syncDir = syncDir;
		c.serverBase = serverBase;
		c.client = "linux/sync_check";
		c.defaultDeviceName = nm;
		c.gateDefault = [](const std::string&, bool, const std::string&) { return std::string("\x01"); };
		engine.reset(new clsync::Engine(c, syncplatform::crypto(), *host));
	}
	void addCircuit(const std::string& id, const std::string& circuitName, const std::string& cdl) {
		g_mkdir_with_parents((root + "/" + id + "/versions").c_str(), 0755);
		spit(root + "/" + id + "/name.txt", circuitName);
		spit(root + "/" + id + "/circuit.cdl", cdl);
	}
	// folder -> name, for the circuits in the library (not the trash).
	std::map<std::string, std::string> circuits() const {
		std::map<std::string, std::string> out;
		for (const std::string& n : listDir(root)) {
			if (n[0] == '.') continue;
			if (g_file_test((root + "/" + n + "/circuit.cdl").c_str(), G_FILE_TEST_EXISTS)) out[n] = slurp(root + "/" + n + "/name.txt");
		}
		return out;
	}
	std::set<std::string> names() const {
		std::set<std::string> out;
		for (const auto& kv : circuits()) out.insert(kv.second);
		return out;
	}
	std::string folderNamed(const std::string& circuitName) const {
		for (const auto& kv : circuits())
			if (kv.second == circuitName) return kv.first;
		return std::string();
	}
	int trashed() const { return (int)listDir(root + "/.Trash").size(); }
	// One cycle, now: true when it finished synced.
	bool syncNow() {
		const int64_t before = engine->status().lastSyncAt;
		engine->syncNow();
		return waitFor(30000, [&] {
			const clsync::Status s = engine->status();
			return s.kind == clsync::Status::Synced && s.lastSyncAt > before;
		});
	}
};

int roundTrip(const std::string& url, const std::string& dir, const std::string& cdlA, const std::string& cdlB) {
	const std::string a = slurp(cdlA), b = slurp(cdlB);
	if (a.empty() || b.empty()) {
		ok(false, "the two circuit files to sync are readable (" + cdlA + ", " + cdlB + ")");
		return 1;
	}
	Device A(dir, "Laptop", url), B(dir, "Pi", url);
	A.addCircuit("20261001-100000-11111", "Half adder", a);
	A.addCircuit("20261001-100001-22222", "Lab 6", b);
	A.engine->start();
	B.engine->start();

	std::promise<std::pair<bool, std::string>> on;
	A.engine->turnOn([&](bool good, std::string message) { on.set_value({ good, message }); });
	auto onResult = on.get_future();
	ok(onResult.wait_for(std::chrono::seconds(60)) == std::future_status::ready && onResult.get().first, "Turn On Sync makes a space on the server");
	const std::string code = A.engine->code();
	ok(code.size() == 28, "...and a 28-symbol code");
	ok(A.engine->enabled() && waitFor(30000, [&] { return A.engine->status().kind == clsync::Status::Synced; }), "the first device syncs");
	ok(A.engine->status().circuits == 2, "...its 2 circuits are on the server");
	ok(A.host->loadSecret() == code, "the code is stored (and only in the sync folder's secret file)");
	struct stat st;
	ok(g_stat((A.syncDir + "/secret").c_str(), (GStatBuf*)&st) == 0 && (st.st_mode & 0777) == 0600, "...a file only its owner can read");
	ok(g_stat(A.syncDir.c_str(), (GStatBuf*)&st) == 0 && (st.st_mode & 0777) == 0700, "...in a folder only its owner can enter");

	std::promise<std::tuple<bool, std::string, clsync::Preview>> pv;
	B.engine->preview(code, [&](bool good, std::string message, clsync::Preview p) { pv.set_value({ good, message, p }); });
	auto pvResult = pv.get_future();
	ok(pvResult.wait_for(std::chrono::seconds(60)) == std::future_status::ready, "the second device can preview the code");
	const auto preview = pvResult.get();
	ok(std::get<0>(preview) && std::get<2>(preview).circuits == 2, "...it holds 2 circuits");
	ok(!std::get<2>(preview).sentence.empty() && std::get<2>(preview).sentence.find("Laptop") != std::string::npos, "...from the Laptop");
	ok(B.circuits().empty() && B.host->loadSecret().empty(), "...and a preview stores nothing");

	std::promise<std::pair<bool, std::string>> ln;
	B.engine->link(code, [&](bool good, std::string message) { ln.set_value({ good, message }); });
	auto lnResult = ln.get_future();
	ok(lnResult.wait_for(std::chrono::seconds(60)) == std::future_status::ready && lnResult.get().first, "Link This Device");
	ok(waitFor(30000, [&] { return B.circuits().size() == 2 && B.engine->status().kind == clsync::Status::Synced; }), "the circuits arrive");
	ok(B.names() == std::set<std::string>({ "Half adder", "Lab 6" }), "...with their names");
	ok(slurp(B.root + "/" + B.folderNamed("Half adder") + "/circuit.cdl") == a && slurp(B.root + "/" + B.folderNamed("Lab 6") + "/circuit.cdl") == b,
	   "...and their circuits, byte for byte");

	// A renamed here, a new one there.
	spit(A.root + "/20261001-100000-11111/name.txt", "Half adder 2");
	A.addCircuit("20261001-100002-33333", "Third", a);
	ok(A.syncNow(), "an edit and a new circuit sync");
	ok(B.syncNow() && B.names() == std::set<std::string>({ "Half adder 2", "Lab 6", "Third" }), "...and the other device has both");

	// Deleted there (moved to the library's trash): it goes to the trash here.
	const std::string gone = B.folderNamed("Lab 6");
	g_mkdir_with_parents((B.root + "/.Trash").c_str(), 0755);
	g_rename((B.root + "/" + gone).c_str(), (B.root + "/.Trash/" + gone).c_str());
	ok(B.syncNow() && A.syncNow(), "a delete syncs");
	ok(A.names() == std::set<std::string>({ "Half adder 2", "Third" }) && A.trashed() == 1, "...into the other device's trash, not gone for good");

	// The synced copy deleted: the other device finds out.
	std::promise<std::pair<bool, std::string>> del;
	A.engine->deleteSyncedCopy([&](bool good, std::string message) { del.set_value({ good, message }); });
	auto delResult = del.get_future();
	ok(delResult.wait_for(std::chrono::seconds(60)) == std::future_status::ready && delResult.get().first, "Delete Synced Copy");
	ok(!A.engine->enabled() && A.host->loadSecret().empty(), "...turns sync off here, and forgets the code");
	B.engine->syncNow();
	ok(waitFor(30000, [&] { return B.engine->status().kind == clsync::Status::Gone; }), "the other device is told the copy was deleted");
	ok(B.names().size() == 2, "...and keeps its circuits");
	A.engine.reset();
	B.engine.reset();
	return gFails ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
	std::string temp, server, only, curlUrl, roundUrl, cdlA = "res/samples/practice.cdl", cdlB = "format/tests/fixtures/lab6.cdl";
	for (int i = 1; i < argc; i++) {
		const std::string a = argv[i];
		if (a == "--temp" && i + 1 < argc) temp = argv[++i];
		else if (a == "--only" && i + 1 < argc) only = argv[++i];
		else if (a == "--server" && i + 1 < argc) server = argv[++i];
		else if (a == "--new-code") {
			printf("%s\n", clsync::newCode(syncplatform::crypto()).c_str());
			return 0;
		}
		else if (a == "--curl-check" && i + 1 < argc) curlUrl = argv[++i];
		else if (a == "--roundtrip" && i + 1 < argc) {
			roundUrl = argv[++i];
			if (i + 2 < argc && argv[i + 1][0] != '-') { cdlA = argv[++i]; cdlB = argv[++i]; }
		} else {
			fprintf(stderr, "sync_check: unknown option %s\n", a.c_str());
			return 2;
		}
	}
	if (const char* u = g_getenv("CL_SYNC_URL"); u && server.empty() && curlUrl.empty() && roundUrl.empty()) server = u;
	gchar* made = nullptr;
	if (temp.empty()) {
		made = g_dir_make_tmp("cl-sync-check-XXXXXX", nullptr);
		if (!made) return 2;
		temp = made;
	}
	g_mkdir_with_parents(temp.c_str(), 0700);
	int status = 0;
	if (!curlUrl.empty()) {
		status |= curlCheck(curlUrl, temp);
	} else if (!roundUrl.empty()) {
		status |= roundTrip(roundUrl, temp, cdlA, cdlB);
	} else {
		std::string report;
		const bool good = syncplatform::selfTest(temp + "/selftest", server, only, report);
		fputs(report.c_str(), stdout);
		status |= good ? 0 : 1;
	}
	if (made) {
		removeTree(made);
		g_free(made);
	}
	printf("%s\n", status ? "FAILED" : "all passed");
	return status;
}
