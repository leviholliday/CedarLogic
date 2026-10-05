// The apps' library folder as the sync engine sees it (SYNC.md 2.5, 4.2,
// 4.12): one folder per circuit (name.txt, circuit.cdl, versions/), deleted
// circuits in .Trash/, read through fileText, a hash cache keyed by the files'
// own modification ticks and sizes (with git's "racily clean" rule), atomic
// writes, versions made by sync stamped with the arrival time, and the
// library's .sync-library-id / .sync-library-gen. std::filesystem only.

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS   // snprintf/sscanf/localtime/getenv are used with care
#endif

#include "SyncInternal.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace clsync {

// ---- files ---------------------------------------------------------------------------

namespace files {

namespace {

fs::path P(const std::string& s) {
#if defined(__cpp_char8_t)
	return fs::path(reinterpret_cast<const char8_t*>(s.c_str()));
#else
	return fs::u8path(s);
#endif
}

std::string U(const fs::path& p) {
#if defined(__cpp_char8_t)
	const auto u = p.u8string();
	return std::string(u.begin(), u.end());
#else
	return p.u8string();
#endif
}

// file_time_type's epoch differs between standard libraries (1970, 1601, 2174):
// the offset to the system clock's, in whole seconds, measured once.
int64_t fileEpochOffsetUs() {
	static const int64_t offset = [] {
		using namespace std::chrono;
		const int64_t f = duration_cast<microseconds>(fs::file_time_type::clock::now().time_since_epoch()).count();
		const int64_t s = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
		const int64_t d = f - s;
		const int64_t sec = (d >= 0 ? d + 500000 : d - 500000) / 1000000;
		return sec * 1000000;
	}();
	return offset;
}

}  // namespace

std::string join(const std::string& a, const std::string& b) {
	if (a.empty()) return b;
	const char last = a.back();
	return (last == '/' || last == '\\') ? a + b : a + "/" + b;
}

bool read(const std::string& path, std::string& out) {
	out.clear();
	std::ifstream f(P(path), std::ios::binary);
	if (!f) return false;
	std::ostringstream ss;
	ss << f.rdbuf();
	if (f.bad()) return false;
	out = ss.str();
	return true;
}

bool writeAtomic(const std::string& path, const std::string& data) {
	const std::string tmp = path + ".sync-tmp";
	{
		std::ofstream f(P(tmp), std::ios::binary | std::ios::trunc);
		if (!f) return false;
		f.write(data.data(), (std::streamsize)data.size());
		f.flush();
		if (!f) {
			f.close();
			std::error_code ec;
			fs::remove(P(tmp), ec);
			return false;
		}
	}
	std::error_code ec;
	fs::rename(P(tmp), P(path), ec);
	if (ec) {
		// Some standard libraries won't rename over an existing file on Windows.
		std::error_code ec2;
		fs::remove(P(path), ec2);
		fs::rename(P(tmp), P(path), ec);
		if (ec) {
			fs::remove(P(tmp), ec2);
			return false;
		}
	}
	return true;
}

bool exists(const std::string& path) {
	std::error_code ec;
	return fs::exists(P(path), ec);
}

bool isDir(const std::string& path) {
	std::error_code ec;
	return fs::is_directory(P(path), ec);
}

bool makeDirs(const std::string& path) {
	std::error_code ec;
	fs::create_directories(P(path), ec);
	return isDir(path);
}

bool makePrivateDir(const std::string& path) {
	if (!makeDirs(path)) return false;
	std::error_code ec;
	fs::permissions(P(path), fs::perms::owner_all, fs::perm_options::replace, ec);   // 0700 (POSIX)
	return true;
}

bool remove(const std::string& path) {
	std::error_code ec;
	return fs::remove(P(path), ec);
}

bool removeAll(const std::string& path) {
	std::error_code ec;
	fs::remove_all(P(path), ec);
	return !ec;
}

bool rename(const std::string& from, const std::string& to) {
	std::error_code ec;
	fs::rename(P(from), P(to), ec);
	return !ec;
}

std::vector<std::string> listDir(const std::string& path) {
	std::vector<std::string> out;
	std::error_code ec;
	for (fs::directory_iterator it(P(path), ec), end; !ec && it != end; it.increment(ec)) out.push_back(U(it->path().filename()));
	std::sort(out.begin(), out.end());
	return out;
}

bool mtimeRaw(const std::string& path, int64_t& ticks, int64_t& size) {
	std::error_code ec;
	const auto t = fs::last_write_time(P(path), ec);
	if (ec) return false;
	const auto s = fs::file_size(P(path), ec);
	if (ec) return false;
	ticks = (int64_t)t.time_since_epoch().count();
	size = (int64_t)s;
	return true;
}

int64_t mtimeMs(const std::string& path) {
	using namespace std::chrono;
	std::error_code ec;
	const auto t = fs::last_write_time(P(path), ec);
	if (ec) return 0;
	const int64_t us = duration_cast<microseconds>(t.time_since_epoch()).count() - fileEpochOffsetUs();
	return us >= 0 ? us / 1000 : (us - 999) / 1000;
}

bool setMtimeMs(const std::string& path, int64_t ms) {
	using namespace std::chrono;
	const microseconds us(ms * 1000 + fileEpochOffsetUs());
	const fs::file_time_type t(duration_cast<fs::file_time_type::duration>(us));
	std::error_code ec;
	fs::last_write_time(P(path), t, ec);
	return !ec;
}

}  // namespace files

// ---- the library ---------------------------------------------------------------------------

namespace {

std::mutex gTimeMutex;

// Local time as the apps name folders and versions: yyyyMMdd-HHmmss.
std::string localStamp(int64_t ms) {
	std::lock_guard<std::mutex> lock(gTimeMutex);
	const std::time_t t = (std::time_t)(ms / 1000);
	const std::tm* lt = std::localtime(&t);
	if (!lt) return "19700101-000000";
	char buf[32];
	std::strftime(buf, sizeof buf, "%Y%m%d-%H%M%S", lt);
	return buf;
}

// "4 Oct 10:31", for version notes.
std::string noteTime(int64_t ms) {
	std::lock_guard<std::mutex> lock(gTimeMutex);
	const std::time_t t = (std::time_t)(ms / 1000);
	const std::tm* lt = std::localtime(&t);
	if (!lt) return std::string();
	static const char* const months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	char buf[48];
	snprintf(buf, sizeof buf, "%d %s %02d:%02d", lt->tm_mday, months[lt->tm_mon % 12], lt->tm_hour, lt->tm_min);
	return buf;
}

// A folder id's stamp as local time, ms; -1 if it isn't one.
int64_t stampTime(const std::string& id) {
	int y, mo, d, h, mi, s;
	if (id.size() < 15 || sscanf(id.c_str(), "%4d%2d%2d-%2d%2d%2d", &y, &mo, &d, &h, &mi, &s) != 6) return -1;
	std::lock_guard<std::mutex> lock(gTimeMutex);
	std::tm tm{};
	tm.tm_year = y - 1900;
	tm.tm_mon = mo - 1;
	tm.tm_mday = d;
	tm.tm_hour = h;
	tm.tm_min = mi;
	tm.tm_sec = s;
	tm.tm_isdst = -1;
	const std::time_t t = std::mktime(&tm);
	if (t == (std::time_t)-1) return -1;
	return (int64_t)t * 1000;
}

struct CacheEntry {
	int64_t cdlTicks = 0, cdlSize = -1, nameTicks = 0, nameSize = -1, checkedAt = 0, mtime = 0;
	LocalHashes h;
};

class FileLibrary : public Backend {
public:
	explicit FileLibrary(const FileLibraryOptions& o) : opt(o) {}

	bool list(std::vector<std::string>& ids) override {
		ids.clear();
		if (!files::isDir(opt.root)) return false;
		for (const std::string& n : files::listDir(opt.root)) {
			if (n.empty() || n[0] == '.') continue;
			const std::string folder = files::join(opt.root, n);
			if (files::isDir(folder) && files::exists(files::join(folder, "circuit.cdl"))) ids.push_back(n);
		}
		// The cache keeps only what is here.
		for (auto it = cache.begin(); it != cache.end();)
			it = std::binary_search(ids.begin(), ids.end(), it->first) ? std::next(it) : cache.erase(it);
		return true;
	}

	bool exists(const std::string& id) override {
		return validId(id) && files::exists(files::join(folderOf(id), "circuit.cdl"));
	}

	bool read(const std::string& id, std::string& name, std::string& cdl, int64_t& mtime, int64_t& createdAt) override {
		if (!validId(id)) return false;
		std::string raw;
		if (!files::read(cdlPath(id), raw)) return false;
		cdl = fileText(raw);
		std::string n;
		files::read(namePath(id), n);
		name = trimAscii(fileText(n));
		if (name.empty()) name = "Untitled";
		mtime = modified(id);
		createdAt = stampTime(id);
		return true;
	}

	int64_t modified(const std::string& id) override {
		return std::max(files::mtimeMs(cdlPath(id)), files::mtimeMs(namePath(id)));
	}

	bool hashes(const std::string& id, LocalHashes& out) override {
		if (!validId(id)) return false;
		CacheEntry k;
		if (!files::mtimeRaw(cdlPath(id), k.cdlTicks, k.cdlSize)) return false;
		if (!files::mtimeRaw(namePath(id), k.nameTicks, k.nameSize)) { k.nameTicks = 0; k.nameSize = -1; }
		auto it = cache.find(id);
		if (it != cache.end()) {
			const CacheEntry& c = it->second;
			// Not trusted when it was taken within 2 s of the change: a same-size
			// save in the same tick could follow ("racily clean").
			if (c.cdlTicks == k.cdlTicks && c.cdlSize == k.cdlSize && c.nameTicks == k.nameTicks &&
			    c.nameSize == k.nameSize && c.checkedAt - c.mtime >= 2000) {
				out = c.h;
				return true;
			}
		}
		std::string name, cdl;
		int64_t mtime = 0, created = 0;
		if (!read(id, name, cdl, mtime, created)) return false;
		Crypto& cr = *opt.crypto;
		k.h.n = nameHash(cr, name);
		k.h.c = cdlHash(cr, cdl);
		k.h.st = opt.structureHash(cdl);
		k.h.ch = contentHash(cr, name, cdl);
		k.mtime = mtime;
		k.checkedAt = opt.clock->now();
		cache[id] = k;
		out = k.h;
		return true;
	}

	std::string create(const std::string& name, const std::string& cdl, int64_t mtime, const std::string& note) override {
		if (!files::makeDirs(opt.root)) return std::string();
		std::string id;
		for (int attempt = 0; attempt < 20 && id.empty(); attempt++) {
			uint8_t r[4];
			if (!opt.crypto->random(r, sizeof r)) return std::string();
			const uint32_t n = ((uint32_t)r[0] << 24 | (uint32_t)r[1] << 16 | (uint32_t)r[2] << 8 | r[3]) % 90000 + 10000;
			const std::string candidate = localStamp(opt.clock->now()) + "-" + std::to_string(n);
			if (!files::exists(files::join(opt.root, candidate))) id = candidate;
		}
		if (id.empty()) return std::string();
		const std::string folder = folderOf(id);
		if (!files::makeDirs(files::join(folder, "versions"))) return std::string();
		if (!files::writeAtomic(namePath(id), name) || !files::writeAtomic(cdlPath(id), cdl)) {
			files::removeAll(folder);
			return std::string();
		}
		files::setMtimeMs(namePath(id), mtime);
		files::setMtimeMs(cdlPath(id), mtime);
		writeVersion(id, cdl, note);
		return id;
	}

	bool write(const std::string& id, const std::string& name, const std::string& cdl, int64_t mtime) override {
		if (!validId(id)) return false;
		std::string oldCdl, oldName;
		files::read(cdlPath(id), oldCdl);
		files::read(namePath(id), oldName);
		if (oldCdl != cdl) {
			if (!files::writeAtomic(cdlPath(id), cdl)) return false;
			files::setMtimeMs(cdlPath(id), mtime);
		}
		if (trimAscii(fileText(oldName)) != name) {
			if (!files::writeAtomic(namePath(id), name)) return false;
			files::setMtimeMs(namePath(id), mtime);
		}
		cache.erase(id);
		return true;
	}

	bool keepVersion(const std::string& id, const std::string& cdl, const std::string& note) override {
		if (!validId(id)) return false;
		const std::string dir = files::join(folderOf(id), "versions");
		files::makeDirs(dir);
		std::string newest;
		for (const std::string& n : files::listDir(dir))
			if (n.size() > 4 && n[0] != '.' && n.compare(n.size() - 4, 4, ".cdl") == 0 && n > newest) newest = n;
		if (!newest.empty()) {
			std::string text;
			if (files::read(files::join(dir, newest), text) && opt.structureHash(fileText(text)) == opt.structureHash(cdl))
				return true;   // the newest version already has this structure
		}
		return writeVersion(id, cdl, note);
	}

	bool keepsVersions() const override { return true; }

	bool trash(const std::string& id) override {
		if (!validId(id)) return false;
		const std::string bin = files::join(opt.root, ".Trash");
		files::makeDirs(bin);
		std::string dest = files::join(bin, id);
		for (int n = 2; files::exists(dest); n++) dest = files::join(bin, id + " " + std::to_string(n));
		cache.erase(id);
		return files::rename(folderOf(id), dest);
	}

	std::string libraryId() override {
		const std::string path = files::join(opt.root, ".sync-library-id");
		std::string text;
		if (files::read(path, text)) {
			text = trimAscii(text);
			if (isHex(text, 32)) return text;
		}
		const std::string id = randomHex(*opt.crypto, 16);
		if (id.empty() || !files::makeDirs(opt.root) || !files::writeAtomic(path, id + "\n")) return std::string();
		return id;
	}

	int64_t libraryGen() override {
		std::string text;
		if (!files::read(files::join(opt.root, ".sync-library-gen"), text)) return 0;
		text = trimAscii(text);
		if (text.empty() || text.size() > 15 || text.find_first_not_of("0123456789") != std::string::npos) return -1;
		return std::stoll(text);
	}

	bool setLibraryGen(int64_t gen) override {
		return files::makeDirs(opt.root) &&
		       files::writeAtomic(files::join(opt.root, ".sync-library-gen"), std::to_string(gen) + "\n");
	}

	json::Value cacheJson() override {
		json::Value o = json::Value::object();
		for (const auto& kv : cache) {
			const CacheEntry& c = kv.second;
			json::Value e = json::Value::object();
			e.set("cdlMtimeNs", json::Value::string(std::to_string(c.cdlTicks)));
			e.set("cdlSize", json::Value::integer(c.cdlSize));
			e.set("nameMtimeNs", json::Value::string(std::to_string(c.nameTicks)));
			e.set("nameSize", json::Value::integer(c.nameSize));
			e.set("mtime", json::Value::integer(c.mtime));
			e.set("checkedAt", json::Value::integer(c.checkedAt));
			e.set("n", json::Value::string(c.h.n));
			e.set("c", json::Value::string(c.h.c));
			e.set("st", json::Value::string(c.h.st));
			e.set("ch", json::Value::string(c.h.ch));
			o.set(kv.first, std::move(e));
		}
		return o;
	}

	void loadCache(const json::Value& v) override {
		cache.clear();
		if (!v.isObject()) return;
		for (const auto& kv : v.o) {
			const json::Value& e = kv.second;
			if (!e.isObject() || !validId(kv.first)) continue;
			CacheEntry c;
			try {
				c.cdlTicks = std::stoll(e.str("cdlMtimeNs", "0"));
				c.nameTicks = std::stoll(e.str("nameMtimeNs", "0"));
			} catch (const std::exception&) {
				continue;
			}
			c.cdlSize = e.integer("cdlSize", -1);
			c.nameSize = e.integer("nameSize", -1);
			c.mtime = e.integer("mtime");
			c.checkedAt = e.integer("checkedAt");
			c.h.n = e.str("n");
			c.h.c = e.str("c");
			c.h.st = e.str("st");
			c.h.ch = e.str("ch");
			if (!isHex(c.h.n, 64) || !isHex(c.h.c, 64) || !isHex(c.h.st, 64) || !isHex(c.h.ch, 64)) continue;
			cache[kv.first] = c;
		}
	}

	void forget(const std::string& id) override { cache.erase(id); }

	// A version made by sync: stamped with the arrival time (the next free
	// second if that one is taken), the file's time set to match, the note beside it.
	bool writeVersion(const std::string& id, const std::string& cdl, const std::string& note) {
		const std::string dir = files::join(folderOf(id), "versions");
		if (!files::makeDirs(dir)) return false;
		int64_t at = opt.clock->now() / 1000 * 1000;
		std::string stamp = localStamp(at);
		for (int k = 0; k < 3600 && files::exists(files::join(dir, stamp + ".cdl")); k++) {
			at += 1000;
			stamp = localStamp(at);
		}
		const std::string path = files::join(dir, stamp + ".cdl");
		if (!files::writeAtomic(path, cdl)) return false;
		files::setMtimeMs(path, at);
		if (!note.empty()) {
			const std::string notePath = files::join(dir, stamp + ".txt");
			files::writeAtomic(notePath, note + "\n");
			files::setMtimeMs(notePath, at);
		}
		// What .pending.cdl held is kept now (§4.12 step 3).
		files::remove(files::join(dir, ".pending.cdl"));
		return true;
	}

private:
	// Folder ids come from this library or the state file, never from the
	// server; still, nothing that could leave the library folder.
	static bool validId(const std::string& id) {
		return !id.empty() && id[0] != '.' && id.find('/') == std::string::npos && id.find('\\') == std::string::npos &&
		       id.find(':') == std::string::npos;
	}
	std::string folderOf(const std::string& id) const { return files::join(opt.root, id); }
	std::string cdlPath(const std::string& id) const { return files::join(folderOf(id), "circuit.cdl"); }
	std::string namePath(const std::string& id) const { return files::join(folderOf(id), "name.txt"); }

	FileLibraryOptions opt;
	std::map<std::string, CacheEntry> cache;
};

}  // namespace

std::unique_ptr<Backend> makeFileLibrary(const FileLibraryOptions& o) { return std::unique_ptr<Backend>(new FileLibrary(o)); }

std::string versionNoteTime(int64_t ms) { return noteTime(ms); }

}  // namespace clsync
