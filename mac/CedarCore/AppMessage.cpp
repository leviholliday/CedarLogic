// A message from CedarLogic: AppMessage.h's rules, and include/CedarMessage.h's C interface.

#include "AppMessage.h"
#include "CedarMessage.h"
#include "SyncJson.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace clmessage {

namespace {

bool isDate(const std::string& s) {
	if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
	for (size_t i : { 0, 1, 2, 3, 5, 6, 8, 9 })
		if (s[i] < '0' || s[i] > '9') return false;
	const int month = (s[5] - '0') * 10 + (s[6] - '0'), day = (s[8] - '0') * 10 + (s[9] - '0');
	return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

bool isId(const std::string& s) {
	if (s.empty() || s.size() > 40) return false;
	return std::all_of(s.begin(), s.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
}

// Characters, not bytes (UTF-8): what the limits count.
size_t length(const std::string& s) {
	return (size_t)std::count_if(s.begin(), s.end(), [](char c) { return ((unsigned char)c & 0xC0) != 0x80; });
}

// The site and a path of plain URL characters: nothing that could end the address early or start
// another one (no spaces, quotes, angle brackets, backslashes, control characters).
bool isOurUrl(const std::string& u) {
	const size_t n = strlen(kSite);
	if (u.size() > 200 || u.compare(0, n, kSite) != 0) return false;
	for (char c : u.substr(n)) {
		const unsigned char b = (unsigned char)c;
		if (b <= 0x20 || b >= 0x7F || strchr("\"'<>\\`{}|^", c)) return false;
	}
	return true;
}

bool isVersion(const std::string& v) {
	if (v.empty() || v.size() > 20 || v[0] < '0' || v[0] > '9') return false;
	return std::all_of(v.begin(), v.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '-'; });
}

// The kept answers: id -> "" (done with) or the day it waits till.
std::vector<std::pair<std::string, std::string>> answers(const std::string& shown) {
	std::vector<std::pair<std::string, std::string>> out;
	size_t at = 0;
	while (at <= shown.size()) {
		const size_t comma = std::min(shown.find(',', at), shown.size());
		const std::string item = shown.substr(at, comma - at);
		const size_t mark = item.find('@');
		const std::string id = item.substr(0, mark), till = mark == std::string::npos ? "" : item.substr(mark + 1);
		if (isId(id) && (till.empty() || isDate(till))) out.emplace_back(id, till);
		at = comma + 1;
	}
	return out;
}

// Days since 1970-01-01, and back (Howard Hinnant's civil-date algorithms).
long daysFromCivil(int y, int m, int d) {
	y -= m <= 2;
	const long era = (y >= 0 ? y : y - 399) / 400;
	const unsigned yoe = (unsigned)(y - era * 400);
	const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (long)doe - 719468;
}
void civilFromDays(long z, int& y, int& m, int& d) {
	z += 719468;
	const long era = (z >= 0 ? z : z - 146096) / 146097;
	const unsigned doe = (unsigned)(z - era * 146097);
	const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	const unsigned mp = (5 * doy + 2) / 153;
	d = (int)(doy - (153 * mp + 2) / 5 + 1);
	m = (int)(mp < 10 ? mp + 3 : mp - 9);
	y = (int)(yoe + era * 400 + (m <= 2));
}

}  // namespace

std::string addDays(const std::string& date, int days) {
	if (!isDate(date)) return {};
	int y = atoi(date.substr(0, 4).c_str()), m = atoi(date.substr(5, 2).c_str()), d = atoi(date.substr(8, 2).c_str());
	civilFromDays(daysFromCivil(y, m, d) + days, y, m, d);
	if (y < 0 || y > 9999) return {};
	char out[16];
	snprintf(out, sizeof out, "%04d-%02d-%02d", y, m, d);
	return out;
}

bool pick(const std::string& json, const std::string& today, const std::string& platform, const std::string& shown, Message& out) {
	if (json.size() > kMaxFile || !isDate(today)) return false;
	clsync::json::Value root;
	if (!clsync::json::parse(json, root) || !root.isObject()) return false;
	const clsync::json::Value* list = root.get("messages");
	if (!list || !list->isArray()) return false;
	const auto kept = answers(shown);
	for (const clsync::json::Value& m : list->a) {
		if (!m.isObject()) continue;
		Message c{ m.str("id"), m.str("eyebrow"), m.str("title"), m.str("text"), m.str("note"), m.str("button", "Open"), m.str("url"), 3 };
		const std::string from = m.str("from"), until = m.str("until");
		if (!isId(c.id) || !isDate(from) || (!until.empty() && !isDate(until))) continue;
		if (c.title.empty() || length(c.title) > 80 || c.text.empty() || length(c.text) > 600 || c.button.empty() || length(c.button) > 30) continue;
		if (length(c.eyebrow) > 40 || length(c.note) > 90) continue;
		if (const clsync::json::Value* l = m.get("later")) {
			if (!l->isInt() || l->i() < 1 || l->i() > 60) continue;
			c.later = (int)l->i();
		}
		if (!isOurUrl(c.url)) continue;
		if (const clsync::json::Value* p = m.get("platforms")) {
			if (!p->isArray() || std::none_of(p->a.begin(), p->a.end(), [&](const clsync::json::Value& x) { return x.isString() && x.s == platform; }))
				continue;
		}
		// (Dates as YYYY-MM-DD compare as text.)
		if (today < from || (!until.empty() && today > until)) continue;
		const auto k = std::find_if(kept.begin(), kept.end(), [&](const std::pair<std::string, std::string>& a) { return a.first == c.id; });
		if (k != kept.end() && (k->second.empty() || today < k->second)) continue;
		out = std::move(c);
		return true;
	}
	return false;
}

std::string remember(const std::string& shown, const std::string& id, const std::string& today, int laterDays) {
	std::string out;
	for (const auto& a : answers(shown)) {
		if (a.first == id) continue;
		out += (out.empty() ? "" : ",") + a.first + (a.second.empty() ? "" : "@" + a.second);
	}
	if (!isId(id)) return out;
	const std::string till = laterDays > 0 ? addDays(today, laterDays) : std::string();
	return out + (out.empty() ? "" : ",") + id + (till.empty() ? "" : "@" + till);
}

std::string link(const Message& m, const std::string& platform, const std::string& version) {
	std::string u = m.url;
	u += u.find('?') == std::string::npos ? '?' : '&';
	u += "from=" + (platform == "mac" || platform == "windows" || platform == "linux" ? platform : std::string("unknown"));
	if (isVersion(version)) u += "&v=" + version;
	return u;
}

}  // namespace clmessage

namespace {
char* dup(const std::string& s) {
	char* p = (char*)malloc(s.size() + 1);
	if (p) memcpy(p, s.c_str(), s.size() + 1);
	return p;
}
}  // namespace

extern "C" {

CLAppMessage* cl_message_pick(const char* json, size_t length, const char* today, const char* platform, const char* version,
                              const char* shown) {
	if (!json || !today || !platform) return nullptr;
	clmessage::Message m;
	if (!clmessage::pick(std::string(json, length), today, platform, shown ? shown : "", m)) return nullptr;
	CLAppMessage* out = (CLAppMessage*)calloc(1, sizeof(CLAppMessage));
	if (!out) return nullptr;
	out->id = dup(m.id);
	out->eyebrow = dup(m.eyebrow);
	out->title = dup(m.title);
	out->text = dup(m.text);
	out->note = dup(m.note);
	out->button = dup(m.button);
	out->link = dup(clmessage::link(m, platform, version ? version : ""));
	out->later = m.later;
	if (!out->id || !out->eyebrow || !out->title || !out->text || !out->note || !out->button || !out->link) {
		cl_message_free(out);
		return nullptr;
	}
	return out;
}

void cl_message_free(CLAppMessage* m) {
	if (!m) return;
	for (char* p : { m->id, m->eyebrow, m->title, m->text, m->note, m->button, m->link }) free(p);
	free(m);
}

char* cl_message_remember(const char* shown, const char* id, const char* today, int laterDays) {
	return dup(clmessage::remember(shown ? shown : "", id ? id : "", today ? today : "", laterDays));
}

}  // extern "C"
