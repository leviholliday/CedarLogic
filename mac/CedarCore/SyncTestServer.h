// The self-test's FakeServer (SyncTestServer.cpp): a port of the design's
// ref/sim.py Server behind an HTTP-shaped handle().
#pragma once

#include "SyncInternal.h"

#include <map>
#include <mutex>
#include <set>
#include <string>

namespace clsync {
namespace test {

struct Limits {
	int64_t maxEnvelope = (int64_t)kMaxEnvelope;
	int64_t maxTombstone = (int64_t)kMaxSmallEnvelope;
	int64_t maxRecords = 1000;
	int64_t maxBytes = 10 * 1024 * 1024;
	int64_t maxEntries = 5000;
	int64_t maxTombstones = 2000;
	int64_t maxDevices = 20;
	int64_t tombstoneDays = 400, inactiveDays = 365, emptyDays = 7;
	int64_t fetchIds = 50, writeItems = 50;
};

class FakeServer {
public:
	struct Rec {
		int64_t ver = 0, seq = 0, size = 0, at = 0;
		bool deleted = false, dev = false;
		std::string data, h;
	};
	struct Space {
		std::string epoch;
		int64_t createdAt = 0, activeAt = 0, seq = 0, purgedSeq = 0, count = 0, bytes = 0, devices = 0;
		std::map<std::string, Rec> recs;
		std::map<std::string, int64_t> ghosts;
	};
	struct Auth {
		std::string authHash, deleteHash;
		int64_t createdAt = 0;
	};

	FakeServer(Clock&, Crypto&);
	HttpResponse handle(const HttpRequest&);
	void cleanup();
	Space newSpace();

	// What the endpoints do (also called directly by tests).
	int putSpace(const std::string& sid, const std::string& token, const std::string& deleteHash, json::Value& out);
	int deleteSpace(const std::string& sid, const std::string& token, const std::string& deleteToken, json::Value& out);
	int write(Space& sp, const json::Value& writes, json::Value& out);

	Clock& clock;
	Crypto& crypto;
	Limits limits;
	std::map<std::string, Auth> auth;
	std::map<std::string, Space> spaces;
	std::map<std::string, std::string> gone;   // sid -> deleted | expired
	int64_t changesLimit = 1000;               // page size cap (paging tests)
	int failNext = 0, failStatus = 429;        // request-level failures to answer next
	int64_t failRetryAfter = 120;
	size_t requests = 0;
	std::string lastOp;
	std::vector<int64_t> sinces;               // each changes request's `since`
	std::mutex mu;                             // held by threaded tests around handle() and their reads

private:
	std::string sha(const std::string& s);
	json::Value entry(const std::string& id, const Rec& r);
	json::Value status(const Space& sp);
	int spaceFor(const std::string& sid, const std::string& token, Space*& sp, json::Value& out);
	int changes(Space& sp, int64_t since, int64_t limit, json::Value& out);
	int fetch(Space& sp, const std::vector<std::string>& ids, json::Value& out);
};

}  // namespace test
}  // namespace clsync
