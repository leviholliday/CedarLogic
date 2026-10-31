// Send Feedback (see Feedback.h). Top to bottom: the draft and its tags,
// what's sent about the PC, talking to the site (WinHTTP, on a thread of
// its own), and the window.

#include "Feedback.h"
#include "Alert.h"
#include "Brand.h"
#include "Canvas.h"
#include "Chrome.h"
#include "Dialogs.h"
#include "Fade.h"
#include "FeedbackCapture.h"
#include "Images.h"
#include "SyncApp.h"
#include "Window.h"

#include <bcrypt.h>
#include <dwmapi.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

namespace feedback {

namespace {

const char* const kTags[] = { "Bug", "Idea", "Design", "Toolbar", "Canvas & wiring", "Simulation", "Files & saving", "Performance", "Other" };
const int kTagCount = 9;
const int kMaxImages = 3;

struct Priority { const char* id; const char* name; const char* hint; D2D1_COLOR_F color; };
const Priority kPriorities[] = {
	{ "low", "Low", "Small thing, whenever", D2D1::ColorF(0.55f, 0.55f, 0.55f) },
	{ "normal", "Normal", "Worth fixing", D2D1::ColorF(0.23f, 0.52f, 0.95f) },
	{ "high", "High", "Gets in my way", D2D1::ColorF(0.95f, 0.55f, 0.12f) },
	{ "blocking", "Blocking", "I can't do my work", D2D1::ColorF(0.92f, 0.27f, 0.24f) },
};

// Words that suggest each tag (the Mac's list).
struct Clue { const char* tag; std::vector<const char*> words; };
const std::vector<Clue>& clues() {
	static const std::vector<Clue> c = {
		{ "Bug", { "bug", "crash", "broke", "doesn't", "does not", "didn't", "isn't", "not working", "error", "wrong", "glitch", "stuck",
		           "freez", "fail", "won't", "can't", "cannot", "missing", "disappear", "weird", "jump", "flicker", "should" } },
		{ "Idea", { "idea", "feature", "would be nice", "would love", "could you", "can you add", "please add", "wish", "suggest", "it'd be",
		            "it would be", "maybe add", "what if" } },
		{ "Design", { "look", "color", "colour", "font", "icon", "ugly", "design", "theme", "dark mode", "light mode", "layout", "spacing",
		              "align", "animation", "pretty" } },
		{ "Toolbar", { "toolbar", "button", "top bar", "title bar", "tooltip" } },
		{ "Canvas & wiring", { "wire", "wiring", "gate", "canvas", "connect", "pin", "drag", "zoom", "select", "paste", "copy", "rotate",
		                       "grid", "palette" } },
		{ "Simulation", { "simulat", "clock", "oscilloscope", "scope", "truth table", "timing", "signal", "step" } },
		{ "Files & saving", { "save", "saving", "open", "file", "export", "import", "version history", "library", "your circuits",
		                      "autosave", ".cdl", "print" } },
		{ "Performance", { "slow", "lag", "battery", "cpu", "hang", "stutter", "sluggish", "performance", "memory", "fan" } },
	};
	return c;
}

// The word at the start of a word ("freez" finds "freezes", "pin" not "spinning").
bool has(const std::string& text, const std::string& word) {
	for (size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + 1))
		if (at == 0 || !isalpha((unsigned char)text[at - 1])) return true;
	return false;
}

std::string trimmed(const std::string& s) {
	const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
	return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::vector<std::string> split(const std::string& s, char by) {
	std::vector<std::string> out;
	std::string part;
	std::istringstream in(s);
	while (std::getline(in, part, by)) if (!part.empty()) out.push_back(part);
	return out;
}

struct Attachment {
	enum Kind { Screenshot, Image, Circuit, Recording } kind = Image;
	std::string file, name, type;
	IWICBitmap* thumb = nullptr;
	double seconds = 0;   // a recording's length
};

// Whole files, by UTF-8 name.
std::string readAll(const std::string& file) {
	std::string out;
	HANDLE h = CreateFileW(W(file).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE) return out;
	char buf[65536];
	DWORD got = 0;
	while (ReadFile(h, buf, sizeof buf, &got, nullptr) && got > 0) out.append(buf, got);
	CloseHandle(h);
	return out;
}

bool writeAll(const std::string& file, const std::string& data) {
	HANDLE h = CreateFileW(W(file).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	DWORD wrote = 0;
	const bool ok = WriteFile(h, data.data(), (DWORD)data.size(), &wrote, nullptr) && wrote == data.size();
	CloseHandle(h);
	return ok;
}

long long fileSize(const std::string& file) {
	WIN32_FILE_ATTRIBUTE_DATA a;
	if (!GetFileAttributesExW(W(file).c_str(), GetFileExInfoStandard, &a)) return 0;
	return ((long long)a.nFileSizeHigh << 32) | a.nFileSizeLow;
}

std::string folder() {
	wchar_t tmp[MAX_PATH] = L"";
	GetTempPathW(MAX_PATH, tmp);
	std::wstring dir = std::wstring(tmp) + L"CedarLogic Feedback";
	CreateDirectoryW(dir.c_str(), nullptr);
	char out[MAX_PATH * 3] = "";
	WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, out, sizeof out, nullptr, nullptr);
	return out;
}

std::string uniqueId() {
	static unsigned n = 0;
	return strf("%08lx%04x", (unsigned long)GetTickCount(), ++n & 0xFFFF);
}

// The draft, kept in the prefs between openings (and launches) until sent.
struct Draft {
	CircuitWindow* win = nullptr;
	std::string title, details, email;
	std::vector<std::string> chosen;    // tags picked
	std::vector<std::string> declined;  // suggestions turned off
	int priority = 1;
	bool contactOK = true, includeCircuit = false;
	std::vector<Attachment> attachments;

	void load() {
		const Prefs& p = prefs();
		title = p.feedbackTitle;
		details = p.feedbackDetails;
		email = p.feedbackEmail;
		chosen = split(p.feedbackTags, ',');
		priority = p.feedbackPriority;
		contactOK = p.feedbackContact;
	}
	void save() const {
		Prefs& p = prefs();
		p.feedbackTitle = title;
		p.feedbackDetails = details;
		p.feedbackEmail = email;
		std::string tags;
		for (const std::string& t : chosen) tags += (tags.empty() ? "" : ",") + t;
		p.feedbackTags = tags;
		p.feedbackPriority = priority;
		p.feedbackContact = contactOK;
		p.save();
	}
	bool picked(const std::string& t) const { return std::find(chosen.begin(), chosen.end(), t) != chosen.end(); }
	bool wasDeclined(const std::string& t) const { return std::find(declined.begin(), declined.end(), t) != declined.end(); }

	// What the words suggest (and Simulation, if Simulation View is on).
	std::vector<std::string> suggested() const {
		std::string text = title + " " + details;
		for (char& c : text) c = (char)tolower((unsigned char)c);
		std::vector<std::string> out;
		for (const Clue& c : clues())
			for (const char* w : c.words) if (has(text, w)) { out.push_back(c.tag); break; }
		if (win && win->simView() && std::find(out.begin(), out.end(), "Simulation") == out.end()) out.push_back("Simulation");
		return out;
	}
	bool isSuggested(const std::string& t) const {
		const std::vector<std::string> s = suggested();
		return std::find(s.begin(), s.end(), t) != s.end();
	}
	// The tags it goes with: the ones picked, and the suggestions kept.
	bool isOn(const std::string& t) const { return picked(t) || (isSuggested(t) && !wasDeclined(t)); }
	std::vector<std::string> tags() const {
		std::vector<std::string> out;
		for (const char* t : kTags) if (isOn(t)) out.push_back(t);
		return out;
	}
	void toggle(const std::string& t) {
		auto drop = [](std::vector<std::string>& v, const std::string& x) { v.erase(std::remove(v.begin(), v.end(), x), v.end()); };
		if (isSuggested(t)) {
			if (isOn(t)) { declined.push_back(t); drop(chosen, t); }
			else drop(declined, t);
		} else if (picked(t)) {
			drop(chosen, t);
		} else {
			chosen.push_back(t);
		}
	}
	int imageCount() const {
		int n = 0;
		for (const Attachment& a : attachments) if (a.kind == Attachment::Screenshot || a.kind == Attachment::Image) n++;
		return n;
	}
	bool hasRecording() const {
		for (const Attachment& a : attachments) if (a.kind == Attachment::Recording) return true;
		return false;
	}
	std::string freeName(const char* base, const char* ext) const {
		for (int n = 1;; n++) {
			const std::string name = strf("%s-%d.%s", base, n, ext);
			bool used = false;
			for (const Attachment& a : attachments) used = used || a.name == name;
			if (!used) return name;
		}
	}
	void add(Attachment::Kind kind, const std::string& file, const std::string& name, const std::string& type,
	         const std::string& thumbFrom = "", double seconds = 0) {
		Attachment a;
		a.kind = kind;
		a.file = file;
		a.name = name;
		a.type = type;
		a.seconds = seconds;
		a.thumb = images::load(thumbFrom.empty() ? file : thumbFrom, 264, 168);
		attachments.push_back(a);
	}
	void remove(size_t i) {
		if (i >= attachments.size()) return;
		DeleteFileW(W(attachments[i].file).c_str());
		if (attachments[i].thumb) attachments[i].thumb->Release();
		attachments.erase(attachments.begin() + i);
	}
	void clearAttachments() { while (!attachments.empty()) remove(attachments.size() - 1); }
};

Draft& draft() {
	static Draft d;
	return d;
}

// ---- What's sent about the PC ---------------------------------------------------------

std::string jsonQuote(const std::string& s) {
	std::string o = "\"";
	for (unsigned char c : s) {
		switch (c) {
		case '"': o += "\\\""; break;
		case '\\': o += "\\\\"; break;
		case '\n': o += "\\n"; break;
		case '\r': break;
		case '\t': o += "\\t"; break;
		default:
			if (c < 0x20) o += strf("\\u%04x", c);
			else o += (char)c;
		}
	}
	return o + "\"";
}

struct Json {
	std::string body = "{";
	Json& raw(const char* k, const std::string& v) {
		if (body.size() > 1) body += ",";
		body += jsonQuote(k) + ":" + v;
		return *this;
	}
	Json& text(const char* k, const std::string& v) { return raw(k, jsonQuote(v)); }
	Json& number(const char* k, long long v) { return raw(k, std::to_string(v)); }
	Json& flag(const char* k, bool v) { return raw(k, v ? "true" : "false"); }
	std::string str() const { return body + "}"; }
};

std::string osName() {
	typedef LONG(WINAPI * RtlGetVersionFn)(OSVERSIONINFOW*);
	OSVERSIONINFOW v = { sizeof v };
	if (auto f = (RtlGetVersionFn)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")) f(&v);
	const bool eleven = v.dwMajorVersion == 10 && v.dwBuildNumber >= 22000;
	return strf("Windows %s (build %lu)", eleven ? "11" : v.dwMajorVersion == 10 ? "10" : strf("%lu.%lu", v.dwMajorVersion, v.dwMinorVersion).c_str(),
	            v.dwBuildNumber);
}

std::string registryText(const wchar_t* key, const wchar_t* value) {
	wchar_t buf[256] = L"";
	DWORD size = sizeof buf;
	if (RegGetValueW(HKEY_LOCAL_MACHINE, key, value, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return "";
	char out[512] = "";
	WideCharToMultiByte(CP_UTF8, 0, buf, -1, out, sizeof out, nullptr, nullptr);
	return trimmed(out);
}

BOOL CALLBACK monitorProc(HMONITOR m, HDC, LPRECT, LPARAM data) {
	auto* out = reinterpret_cast<std::vector<std::string>*>(data);
	MONITORINFO mi = { sizeof mi };
	GetMonitorInfoW(m, &mi);
	UINT dx = 96, dy = 96;
	typedef HRESULT(WINAPI * GetDpiForMonitorFn)(HMONITOR, int, UINT*, UINT*);
	static auto getDpi = (GetDpiForMonitorFn)(void*)GetProcAddress(LoadLibraryW(L"shcore.dll"), "GetDpiForMonitor");
	if (getDpi) getDpi(m, 0, &dx, &dy);
	out->push_back(strf("%ld×%ld @%d%%", mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, (int)(dx * 100 / 96)));
	return TRUE;
}

// The server's install ids: 8 to 64 of A-Z a-z 0-9 _ -.
bool isInstallId(const std::string& s) {
	if (s.size() < 8 || s.size() > 64) return false;
	for (char c : s)
		if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) return false;
	return true;
}

// This install's id, as the Mac's FeedbackServer.installId: made once (32
// hex digits, random) and kept in the settings. It names nobody; it only lets
// the feedback server count one PC's reports (10 a day), so students behind
// one school network aren't one sender. One that isn't the right shape
// (edited by hand) is made again.
std::string installId() {
	std::string& id = prefs().installId;
	if (isInstallId(id)) return id;
	unsigned char bytes[16] = {};
	if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, bytes, sizeof bytes, BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
		LARGE_INTEGER t;
		QueryPerformanceCounter(&t);
		for (int i = 0; i < 16; i++) bytes[i] = (unsigned char)((t.QuadPart >> (i % 8 * 8)) ^ (GetCurrentProcessId() * (i + 7)) ^ GetTickCount());
	}
	id.clear();
	for (unsigned char b : bytes) id += strf("%02x", b);
	prefs().save();
	return id;
}

struct Device { std::string app, system, context, line; };

Device device(CircuitWindow* win) {
	Device d;
	const std::string os = osName();
	Json app;
	app.text("name", "CedarLogic for Windows").text("version", CL_VERSION).text("build", CL_GIT_COMMIT).text("commit", CL_GIT_COMMIT)
		.text("channel", "Native testing")
		// (Also sent on its own as installId: createReport.)
		.text("installId", installId());
	d.app = app.str();
	MEMORYSTATUSEX mem = { sizeof mem };
	GlobalMemoryStatusEx(&mem);
	SYSTEM_INFO si;
	GetNativeSystemInfo(&si);
	std::vector<std::string> displays;
	EnumDisplayMonitors(nullptr, nullptr, monitorProc, (LPARAM)&displays);
	std::string list = "[";
	for (size_t i = 0; i < displays.size(); i++) list += (i ? "," : "") + jsonQuote(displays[i]);
	list += "]";
	wchar_t locale[LOCALE_NAME_MAX_LENGTH] = L"";
	GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
	char loc[128] = "";
	WideCharToMultiByte(CP_UTF8, 0, locale, -1, loc, sizeof loc, nullptr, nullptr);
	std::string model = registryText(L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"SystemProductName");
	const std::string maker = registryText(L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"SystemManufacturer");
	if (!maker.empty() && model.find(maker) != 0) model = maker + " " + model;
	const std::string chip = registryText(L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString");
	Json sys;
	sys.text("os", os).text("model", model.empty() ? "PC" : model).number("memoryGB", (long long)std::llround(mem.ullTotalPhys / 1073741824.0))
		.number("cpus", si.dwNumberOfProcessors).raw("displays", list).text("locale", loc).text("appearance", prefs().dark ? "Dark" : "Light")
		.text("arch", si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? "arm64" : "x64");
	if (!chip.empty()) sys.text("chip", chip);
	d.system = sys.str();
	Json ctx;
	ctx.flag("dark", prefs().dark);
	if (win) {
		CLDocument* doc = win->document();
		const int pages = cl_document_page_count(doc);
		long long gates = 0;
		for (int i = 0; i < pages; i++) gates += cl_document_gate_count(doc, i);
		ctx.number("pages", pages).number("gates", gates).flag("simulating", win->running()).flag("simulationView", win->simView())
			.flag("locked", win->locked());
		if (Canvas* c = win->currentCanvas()) ctx.text("zoom", strf("%d%%", c->zoomPercent()));
	}
	d.context = ctx.str();
	d.line = strf("version %s (%.7s), %s, %s", CL_VERSION, CL_GIT_COMMIT, os.substr(0, os.find(" (")).c_str(), model.empty() ? "PC" : model.c_str());
	return d;
}

// ---- Talking to the site -----------------------------------------------------------

std::string envOr(const char* name, const char* fallback) {
	char buf[512];
	const DWORD n = GetEnvironmentVariableA(name, buf, sizeof buf);
	return n > 0 && n < sizeof buf ? std::string(buf, n) : std::string(fallback);
}

// `sent`: the request went out whole (so the server may have acted on it);
// `reached`: an answer came back.
struct Reply { int status = 0; std::string body; bool sent = false, reached = false; };

Reply request(const std::string& method, const std::string& pathAndQuery, const std::string& type, const std::string& token,
              const std::string& body) {
	Reply r;
	const std::wstring url = W(envOr("CL_FEEDBACK_URL", CL_FEEDBACK_URL) + pathAndQuery);
	URL_COMPONENTS u = { sizeof u };
	wchar_t host[256] = L"", path[2048] = L"";
	u.lpszHostName = host;
	u.dwHostNameLength = 256;
	u.lpszUrlPath = path;
	u.dwUrlPathLength = 2048;
	u.dwSchemeLength = (DWORD)-1;
	u.dwExtraInfoLength = (DWORD)-1;
	if (!WinHttpCrackUrl(url.c_str(), 0, 0, &u)) return r;
	std::wstring full = path;
	if (u.lpszExtraInfo && u.dwExtraInfoLength) full.append(u.lpszExtraInfo, u.dwExtraInfoLength);
	HINTERNET session = WinHttpOpen(L"CedarLogic", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (session == nullptr) return r;
	WinHttpSetTimeouts(session, 20000, 20000, 60000, 60000);
	HINTERNET connect = WinHttpConnect(session, host, u.nPort, 0);
	HINTERNET req = connect ? WinHttpOpenRequest(connect, W(method).c_str(), full.c_str(), nullptr, WINHTTP_NO_REFERER,
	                                             WINHTTP_DEFAULT_ACCEPT_TYPES, u.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
	                        : nullptr;
	std::wstring headers = W("content-type: " + type + "\r\nx-cedarlogic-key: " + envOr("CL_FEEDBACK_KEY", CL_FEEDBACK_KEY) + "\r\n");
	if (!token.empty()) headers += W("x-upload-token: " + token + "\r\n");
	r.sent = req && WinHttpSendRequest(req, headers.c_str(), (DWORD)-1L, body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
	                                   (DWORD)body.size(), (DWORD)body.size(), 0);
	if (r.sent && WinHttpReceiveResponse(req, nullptr)) {
		r.reached = true;
		DWORD code = 0, size = sizeof code;
		WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &size,
		                    WINHTTP_NO_HEADER_INDEX);
		r.status = (int)code;
		std::vector<char> chunk(16384);
		for (;;) {
			DWORD got = 0;
			if (!WinHttpReadData(req, chunk.data(), (DWORD)chunk.size(), &got) || got == 0) break;
			r.body.append(chunk.data(), got);
			if (r.body.size() > 1000000) break;
		}
	}
	if (req) WinHttpCloseHandle(req);
	if (connect) WinHttpCloseHandle(connect);
	WinHttpCloseHandle(session);
	return r;
}

std::string jsonField(const std::string& json, const char* key) {
	const std::string needle = "\"" + std::string(key) + "\"";
	size_t at = json.find(needle);
	if (at == std::string::npos) return "";
	at = json.find(':', at + needle.size());
	if (at == std::string::npos) return "";
	while (++at < json.size() && json[at] == ' ') {}
	if (at >= json.size()) return "";
	if (json[at] != '"') {
		size_t end = at;
		while (end < json.size() && (isdigit((unsigned char)json[end]) || json[end] == '-')) end++;
		return json.substr(at, end - at);
	}
	std::string out;
	for (size_t i = at + 1; i < json.size(); i++) {
		if (json[i] == '"') return out;
		if (json[i] == '\\' && i + 1 < json.size()) out += json[++i];
		else out += json[i];
	}
	return out;
}

bool jsonTrue(const std::string& json, const char* key) {
	const std::string needle = "\"" + std::string(key) + "\"";
	size_t at = json.find(needle);
	if (at == std::string::npos) return false;
	at = json.find(':', at + needle.size());
	if (at == std::string::npos) return false;
	while (++at < json.size() && json[at] == ' ') {}
	return json.compare(at, 4, "true") == 0;
}

std::string percent(const std::string& s) {
	std::string o;
	for (unsigned char c : s) {
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
		else o += strf("%%%02X", c);
	}
	return o;
}

// ---- The server (the Mac's FeedbackServer.swift) ---------------------------------
// Every report carries this install's id, which is what the server counts
// reports by: 10 in any 24 hours, then 429 with code "sender_cap" until one
// ages out or the tester asks for more (once a day). An answer with a "code"
// is the server's considered answer, so it's never tried again; other 429s
// and 5xx (and a dropped connection) are, a few times.

// What went wrong, with a sentence to show as it is. `code` is the server's
// ("summary_required", "sender_cap", "more_used", "not_capped").
struct Failure {
	std::string message, code;
	int status = 0;
	bool canRequestMore = false;   // sender_cap: Request more would work
	long retryAfter = -1;          // seconds until one more report fits (sender_cap, more_used)
};

Failure failureOf(const Reply& r) {
	Failure f;
	f.status = r.status;
	f.message = jsonField(r.body, "error");
	if (f.message.empty()) f.message = strf("The feedback server said no (%d).", r.status);
	f.code = jsonField(r.body, "code");
	f.canRequestMore = jsonTrue(r.body, "canRequestMore");
	const std::string after = jsonField(r.body, "retryAfter");
	if (!after.empty()) f.retryAfter = atol(after.c_str());
	return f;
}

// Tried again a few times: a flaky connection shouldn't lose a report. Not a
// no with a code: asking again gets the same answer. A request that makes
// something (`repeatable` false: the report, Request more) is only sent again
// when the server can't have acted on it -- it never got all of it, or said
// it was too busy -- so a slow answer doesn't file it twice.
Reply perform(const std::string& method, const std::string& path, const std::string& type, const std::string& token,
              const std::string& body, Failure& failure, bool repeatable = true) {
	Reply r;
	for (int attempt = 0; attempt < 3; attempt++) {
		r = request(method, path, type, token, body);
		if (r.reached && r.status >= 200 && r.status < 300) return r;
		if (r.reached) {
			failure = failureOf(r);
			if (!failure.code.empty() || (r.status < 500 && r.status != 429)) return r;
			if (!repeatable && r.status != 429 && r.status != 503) break;
		} else {
			failure = Failure();
			failure.message = "Couldn't reach the feedback server. Check the internet connection and try again.";
			if (!repeatable && r.sent) break;
		}
		if (attempt < 2) Sleep((DWORD)(700 * std::pow(2, attempt)));
	}
	r.status = 0;
	return r;
}

struct Created { std::string id, token; long long chunk = 65536; };

// The report itself. installId is added here, so every report has it.
bool createReport(Json body, Created& c, Failure& f) {
	body.text("installId", installId());
	const Reply r = perform("POST", "/api/feedback", "application/json", "", body.str(), f, false);
	if (r.status < 200 || r.status >= 300) return false;
	c.id = jsonField(r.body, "id");
	c.token = jsonField(r.body, "uploadToken");
	c.chunk = std::max(65536LL, atoll(jsonField(r.body, "chunkSize").c_str()));
	if (c.id.empty() || c.token.empty()) {
		f = Failure();
		f.message = "The feedback server's answer didn't make sense.";
		return false;
	}
	return true;
}

bool completeReport(const Created& c, Failure& f) {
	const Reply r = perform("POST", "/api/feedback/complete?id=" + percent(c.id), "application/json", c.token, "", f);
	return r.status >= 200 && r.status < 300;
}

// After a sender_cap: asks for more, with a line saying why if there is one
// (and who's asking, in `extra`, for the developer's list). `granted`: how
// many more can be sent now (10).
bool requestMore(const std::string& message, Json extra, long& granted, Failure& f) {
	extra.text("installId", installId());
	const std::string m = trimmed(message);
	if (!m.empty()) extra.text("message", m.substr(0, 500));
	const Reply r = perform("POST", "/api/feedback/more", "application/json", "", extra.str(), f, false);
	if (r.status < 200 || r.status >= 300) return false;
	const long n = atol(jsonField(r.body, "granted").c_str());
	granted = n > 0 ? n : 10;
	return true;
}

// A send in progress, shared with its thread (it outlives the window).
struct Sending {
	std::mutex lock;
	std::string note = "Sending…";
	double fraction = 0;
	std::atomic<int> state{ 0 };   // 0 going, 1 sent, 2 failed
	Failure failure;
	void set(const std::string& n, double f) {
		std::lock_guard<std::mutex> g(lock);
		note = n;
		fraction = f;
	}
};

void upload(std::shared_ptr<Sending> s, Json body, std::vector<Attachment> files) {
	// The copy of the circuit made for this send goes when it's done (each
	// Send or Try Again makes its own).
	struct Tidy {
		const std::vector<Attachment>& files;
		~Tidy() {
			for (const Attachment& a : files) if (a.kind == Attachment::Circuit) DeleteFileW(W(a.file).c_str());
		}
	} tidy{ files };
	Failure failure;
	auto fail = [&] {
		{
			std::lock_guard<std::mutex> g(s->lock);
			s->failure = failure;
		}
		s->state = 2;
	};
	Created created;
	if (!createReport(body, created, failure)) { fail(); return; }
	long long total = 1, done = 0;
	for (const Attachment& a : files) total += fileSize(a.file);
	for (const Attachment& a : files) {
		const std::string data = readAll(a.file);
		const long long pieces = std::max(1LL, (long long)((data.size() + created.chunk - 1) / created.chunk));
		for (long long i = 0; i < pieces; i++) {
			const std::string part = data.substr((size_t)(i * created.chunk), (size_t)created.chunk);
			const std::string q = "/api/feedback/upload?id=" + percent(created.id) + "&file=" + percent(a.name) + strf("&index=%lld&total=%lld", i, pieces);
			const Reply r = perform("PUT", q, "application/octet-stream", created.token, part, failure);
			if (r.status < 200 || r.status >= 300) { fail(); return; }
			done += (long long)part.size();
			s->set(strf("Sending %s… %.1f of %.1f MB", a.name.c_str(), done / 1e6, total / 1e6), (double)done / total);
		}
	}
	if (!completeReport(created, failure)) { fail(); return; }
	s->state = 1;
}

// Request more, after a sender_cap: on a thread.
struct Asking {
	std::mutex lock;
	std::atomic<int> state{ 0 };   // 0 going, 1 given more, 2 not
	Failure failure;
	long granted = 10;
};

void askForMore(std::shared_ptr<Asking> a, std::string reason, Json extra) {
	Failure f;
	long n = 10;
	const bool ok = requestMore(reason, extra, n, f);
	std::lock_guard<std::mutex> g(a->lock);
	a->granted = n;
	a->failure = f;
	a->state = ok ? 1 : 2;
}

// The send going on, if any: it carries on when the window is closed, and
// is finished here then -- the draft cleared, or a note that it wasn't sent
// (Send Feedback shows why next time it opens).
std::shared_ptr<Sending>& inFlight() {
	static std::shared_ptr<Sending> s;
	return s;
}
UINT_PTR g_watch = 0;
int g_sample = 0;   // showSample's state: the form opens looking like it (1 sending, 2 failed, 4 over the cap, 5 no summary)
const int kSendButton = 100;   // Send Feedback (Form::buttons)
// A send that failed with the window closed: shown when it opens again.
bool g_hasLastFailure = false;
Failure g_lastFailure;

// Sent: the draft is cleared for next time.
void clearSentDraft() {
	Draft& d = draft();
	d.clearAttachments();
	d.title.clear();
	d.details.clear();
	d.chosen.clear();
	d.declined.clear();
	d.priority = 1;
	d.includeCircuit = false;
	d.save();
}

CircuitWindow* liveWindow(CircuitWindow* w) {
	const std::vector<CircuitWindow*>& all = circuitWindows();
	if (std::find(all.begin(), all.end(), w) != all.end()) return w;
	return all.empty() ? nullptr : all.front();
}

void CALLBACK watchSend(HWND, UINT, UINT_PTR, DWORD) {
	const std::shared_ptr<Sending> s = inFlight();
	if (s && s->state == 0) return;
	KillTimer(nullptr, g_watch);
	g_watch = 0;
	if (!s) return;
	inFlight().reset();
	CircuitWindow* w = liveWindow(draft().win);
	if (s->state == 1) {
		clearSentDraft();
		if (w) w->note("Feedback sent. Thank you!");
		return;
	}
	{
		std::lock_guard<std::mutex> g(s->lock);
		g_lastFailure = s->failure;
		g_hasLastFailure = true;
	}
	if (w) w->note("Your feedback wasn't sent. Open Send Feedback to try again.");
	MessageBeep(MB_ICONWARNING);
}

// ---- The window ------------------------------------------------------------------

struct Look {
	bool dark;
	D2D1_COLOR_F ink, dim, card, line, accent;
};
Look look() {
	Look l;
	l.dark = prefs().dark;
	l.ink = l.dark ? D2D1::ColorF(0.93f, 0.93f, 0.93f) : D2D1::ColorF(0.1f, 0.1f, 0.1f);
	l.dim = l.dark ? D2D1::ColorF(0.62f, 0.62f, 0.62f) : D2D1::ColorF(0.42f, 0.42f, 0.42f);
	l.card = l.dark ? D2D1::ColorF(1, 1, 1, 0.045f) : D2D1::ColorF(1, 1, 1);
	l.line = l.dark ? D2D1::ColorF(1, 1, 1, 0.08f) : D2D1::ColorF(0, 0, 0, 0.09f);
	l.accent = chrome().accent();
	return l;
}

const float kChipH = 26, kChipGap = 7;

// The tag chips, wrapped to the width.
std::vector<D2D1_RECT_F> chipRects(float width) {
	std::vector<D2D1_RECT_F> out;
	float x = 0, y = 0;
	for (const char* t : kTags) {
		const float w = textWidth(t, 12, true) + 22 + (draft().isSuggested(t) ? 13 : 0);
		if (x > 0 && x + w > width) { x = 0; y += kChipH + kChipGap; }
		out.push_back(D2D1::RectF(x, y, x + w, y + kChipH));
		x += w + kChipGap;
	}
	return out;
}

void paintTags(ID2D1RenderTarget* rt, float width) {
	const Look l = look();
	const std::vector<D2D1_RECT_F> r = chipRects(width);
	ID2D1Factory* f = nullptr;
	rt->GetFactory(&f);
	ID2D1StrokeStyle* dashed = nullptr;
	const float dashes[] = { 3, 2 };
	if (f) f->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN_ROUND,
	                                                         10, D2D1_DASH_STYLE_CUSTOM, 0), dashes, 2, &dashed);
	for (int i = 0; i < kTagCount; i++) {
		const bool on = draft().isOn(kTags[i]), suggested = draft().isSuggested(kTags[i]);
		const D2D1_RECT_F c = D2D1::RectF(r[i].left + 0.5f, r[i].top + 0.5f, r[i].right - 0.5f, r[i].bottom - 0.5f);
		fillRound(rt, c, kChipH / 2, on ? withAlpha(l.accent, l.dark ? 0.2f : 0.12f) : l.card);
		const D2D1_COLOR_F edge = on ? withAlpha(l.accent, 0.55f) : (suggested ? withAlpha(l.ink, 0.35f) : l.line);
		ID2D1SolidColorBrush* b = nullptr;
		if (SUCCEEDED(rt->CreateSolidColorBrush(edge, &b))) {
			rt->DrawRoundedRectangle(D2D1::RoundedRect(c, kChipH / 2, kChipH / 2), b, 1, suggested && !on ? dashed : nullptr);
			b->Release();
		}
		float x = r[i].left + 11;
		if (suggested) {
			drawText(rt, "✦", D2D1::RectF(x, r[i].top + 6, x + 12, r[i].bottom), 9, l.accent);
			x += 13;
		}
		drawText(rt, kTags[i], D2D1::RectF(x, r[i].top + 5, r[i].right, r[i].bottom), 12, on ? l.accent : withAlpha(l.ink, 0.75f),
		         TextAlign::Leading, on);
	}
	if (dashed) dashed->Release();
	if (f) f->Release();
}

D2D1_RECT_F pillRect(int i, float width) {
	const float gap = 8, w = (width - 3 * gap) / 4;
	return D2D1::RectF(i * (w + gap), 0, i * (w + gap) + w, 32);
}

void paintPriority(ID2D1RenderTarget* rt, float width) {
	const Look l = look();
	for (int i = 0; i < 4; i++) {
		const Priority& p = kPriorities[i];
		const bool on = draft().priority == i;
		const D2D1_RECT_F r = pillRect(i, width);
		fillRound(rt, r, 9, on ? withAlpha(p.color, l.dark ? 0.22f : 0.14f) : l.card);
		strokeRound(rt, D2D1::RectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f), 9, on ? withAlpha(p.color, 0.7f) : l.line,
		            on ? 1.5f : 1);
		const float tw = textWidth(p.name, 12.5f, on) + 14;
		const float x = (r.left + r.right - tw) / 2;
		fillCircle(rt, D2D1::Point2F(x + 4, 16), 4, p.color);
		drawText(rt, p.name, D2D1::RectF(x + 14, 8, r.right, 28), 12.5f, on ? l.ink : withAlpha(l.ink, 0.7f), TextAlign::Leading, on);
	}
	drawText(rt, kPriorities[draft().priority].hint, D2D1::RectF(0, 38, width, 54), 11.5f, l.dim);
}

const float kTileW = 116, kTileH = 72, kThumbW = 112, kThumbTop = 90, kShotsHeight = 168;
const int kTiles = 3;   // Screenshot, Record, Add image…

// The three tiles along the top, then what's attached on a row of its own.
D2D1_RECT_F tileRect(int i) { return D2D1::RectF(i * (kTileW + 10), 6, i * (kTileW + 10) + kTileW, 6 + kTileH); }
// The attachments' row, narrower if they wouldn't all fit (the last one's ×
// too, which stands 6 points past it).
D2D1_RECT_F thumbRect(size_t i, float width) {
	const float gap = 14;
	const float tw = std::max(40.0f, std::min(kThumbW, (width - 6 - (kMaxImages) * gap) / (kMaxImages + 1)));
	return D2D1::RectF(i * (tw + gap), kThumbTop, i * (tw + gap) + tw, kThumbTop + kTileH);
}
D2D1_RECT_F closeRect(size_t i, float width) {
	const D2D1_RECT_F t = thumbRect(i, width);
	return D2D1::RectF(t.right - 12, t.top - 6, t.right + 6, t.top + 12);
}

void paintAttachments(ID2D1RenderTarget* rt, float width) {
	const Look l = look();
	const Draft& d = draft();
	const bool more = d.imageCount() < kMaxImages, rec = capture::recording();
	const wchar_t glyphs[] = { 0xE722, 0xE7C8, 0xEB9F };   // camera, record, photo
	const std::string labels[] = { "Screenshot", d.hasRecording() ? "Record again" : "Record", "Add image\u2026" };
	const bool enabled[] = { more && !rec, !rec, more && !rec };
	for (int i = 0; i < kTiles; i++) {
		const D2D1_RECT_F r = tileRect(i);
		const float a = enabled[i] ? 1 : 0.45f;
		fillRound(rt, r, 11, l.card);
		strokeRound(rt, D2D1::RectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f), 11, l.line);
		const D2D1_COLOR_F tint = i == 1 ? D2D1::ColorF(0.91f, 0.25f, 0.21f) : l.accent;
		drawIcon(rt, glyphs[i], D2D1::RectF(r.left, r.top + 12, r.right, r.top + 38), 19, withAlpha(tint, a));
		drawText(rt, labels[i], D2D1::RectF(r.left, r.top + 44, r.right, r.bottom), 12, withAlpha(l.ink, a), TextAlign::Center);
	}
	if (d.attachments.empty()) {
		// Nothing yet: what the row is for.
		const D2D1_RECT_F e = D2D1::RectF(0, kThumbTop, width - 2, kThumbTop + kTileH);
		ID2D1Factory* f = nullptr;
		rt->GetFactory(&f);
		ID2D1StrokeStyle* dashed = nullptr;
		const float dashes[] = { 4, 3 };
		if (f) f->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN_ROUND,
		                                                         10, D2D1_DASH_STYLE_CUSTOM, 0), dashes, 2, &dashed);
		ID2D1SolidColorBrush* b = nullptr;
		if (SUCCEEDED(rt->CreateSolidColorBrush(withAlpha(l.ink, 0.18f), &b))) {
			rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(e.left + 0.5f, e.top + 0.5f, e.right - 0.5f, e.bottom - 0.5f), 10, 10), b, 1, dashed);
			b->Release();
		}
		if (dashed) dashed->Release();
		if (f) f->Release();
		drawText(rt, "Nothing attached yet. A picture or a minute's recording often says more than words.",
		         D2D1::RectF(e.left + 16, e.top + 18, e.right - 16, e.top + 36), 12, l.dim, TextAlign::Center);
		drawText(rt, "Up to three pictures and one recording. Only the CedarLogic window is captured, never the rest of your screen.",
		         D2D1::RectF(e.left + 16, e.top + 38, e.right - 16, e.bottom - 6), 11, withAlpha(l.dim, 0.85f), TextAlign::Center);
		return;
	}
	for (size_t i = 0; i < d.attachments.size(); i++) {
		const Attachment& a = d.attachments[i];
		const D2D1_RECT_F r = thumbRect(i, width);
		fillRound(rt, r, 9, D2D1::ColorF(0, 0, 0, 0.3f));
		if (a.thumb) {
			ID2D1Bitmap* bmp = nullptr;
			if (SUCCEEDED(rt->CreateBitmapFromWicBitmap(a.thumb, &bmp))) {
				const D2D1_SIZE_F s = bmp->GetSize();
				// Filled, centred, clipped to the rounded card.
				const float k = std::max((r.right - r.left) / s.width, kTileH / s.height);
				const float w = s.width * k, h = s.height * k;
				ID2D1Factory* f = nullptr;
				rt->GetFactory(&f);
				ID2D1RoundedRectangleGeometry* g = nullptr;
				if (f && SUCCEEDED(f->CreateRoundedRectangleGeometry(D2D1::RoundedRect(r, 9, 9), &g))) {
					rt->PushLayer(D2D1::LayerParameters(r, g), nullptr);
					rt->DrawBitmap(bmp, D2D1::RectF((r.left + r.right - w) / 2, (r.top + r.bottom - h) / 2, (r.left + r.right + w) / 2,
					                                (r.top + r.bottom + h) / 2));
					rt->PopLayer();
					g->Release();
				}
				if (f) f->Release();
				bmp->Release();
			}
		}
		if (a.kind == Attachment::Recording) {
			// A play button over it, and its length.
			const D2D1_POINT_2F c = D2D1::Point2F((r.left + r.right) / 2, (r.top + r.bottom) / 2);
			fillCircle(rt, c, 15, D2D1::ColorF(0, 0, 0, 0.55f));
			drawIcon(rt, 0xE768, D2D1::RectF(c.x - 15 + 2, c.y - 15, c.x + 15 + 2, c.y + 15), 13, D2D1::ColorF(1, 1, 1));
			const int secs = (int)std::lround(a.seconds);
			const std::string len = strf("%d:%02d", secs / 60, secs % 60);
			const float lw = textWidth(len, 10.5f, true) + 10;
			fillRound(rt, D2D1::RectF(r.right - lw - 5, r.bottom - 21, r.right - 5, r.bottom - 5), 5, D2D1::ColorF(0, 0, 0, 0.6f));
			drawText(rt, len, D2D1::RectF(r.right - lw - 5, r.bottom - 20, r.right - 5, r.bottom - 5), 10.5f, D2D1::ColorF(1, 1, 1),
			         TextAlign::Center, true);
		}
		strokeRound(rt, D2D1::RectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f), 9, l.line);
		const D2D1_RECT_F c = closeRect(i, width);
		fillCircle(rt, D2D1::Point2F((c.left + c.right) / 2, (c.top + c.bottom) / 2), 8.5f, D2D1::ColorF(0, 0, 0, 0.65f));
		drawIcon(rt, Icon::Dismiss, c, 8, D2D1::ColorF(1, 1, 1));
	}
}

// The band across the top, in the icon's colours.
void paintBand(ID2D1RenderTarget* rt, float w, float h) {
	const D2D1_COLOR_F ink = D2D1::ColorF(0.035f, 0.063f, 0.047f), deep = D2D1::ColorF(0.06f, 0.12f, 0.08f);
	const D2D1_COLOR_F neon = D2D1::ColorF(0.22f, 1.0f, 0.42f), dim = D2D1::ColorF(0.62f, 0.74f, 0.66f);
	D2D1_GRADIENT_STOP s[2] = { { 0, ink }, { 1, deep } };
	ID2D1GradientStopCollection* stops = nullptr;
	if (SUCCEEDED(rt->CreateGradientStopCollection(s, 2, &stops))) {
		ID2D1LinearGradientBrush* b = nullptr;
		if (SUCCEEDED(rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(w, h)), stops, &b))) {
			rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0, 0, w, h), 10, 10), b);
			b->Release();
		}
		stops->Release();
	}
	// A few traces, as on a circuit board, fading off to the right.
	ID2D1SolidColorBrush* t = nullptr;
	if (SUCCEEDED(rt->CreateSolidColorBrush(neon, &t))) {
		for (int i = 0; i < 4; i++) {
			const float y = 16 + i * 15.0f, x1 = w * 0.66f + i * 14, x2 = w * 0.70f + i * 14;
			t->SetOpacity(0.10f);
			rt->DrawLine(D2D1::Point2F(w * 0.55f, y), D2D1::Point2F(x1, y), t, 1.2f);
			t->SetOpacity(0.18f);
			rt->DrawLine(D2D1::Point2F(x1, y), D2D1::Point2F(x2, y + 10), t, 1.2f);
			rt->DrawLine(D2D1::Point2F(x2, y + 10), D2D1::Point2F(w, y + 10), t, 1.2f);
		}
		t->Release();
	}
	drawText(rt, "Send Feedback", D2D1::RectF(20, 16, w, 44), 20, D2D1::ColorF(1, 1, 1), TextAlign::Leading, true);
	const float bx = 20 + textWidth("Send Feedback", 20, true) + 10;
	fillRound(rt, D2D1::RectF(bx, 23, bx + 40, 39), 8, neon);
	drawText(rt, "BETA", D2D1::RectF(bx, 24, bx + 40, 39), 9.5f, D2D1::ColorF(0.012f, 0.024f, 0.018f), TextAlign::Center, true);
	drawText(rt, "What's working, what's broken, what you'd love to see. It goes straight to the developer.",
	         D2D1::RectF(20, 48, w - 16, h), 12, dim);
}

void attachScreenshot(Form& form) {
	Draft& d = draft();
	if (d.imageCount() >= kMaxImages || d.win == nullptr) { MessageBeep(MB_OK); return; }
	const std::string name = d.freeName("screenshot", "png");
	const std::string file = folder() + "\\" + uniqueId() + "-" + name;
	// The circuit window, as it looks now (this window stepped out of the way).
	ShowWindow(form.dialog, SW_HIDE);
	UpdateWindow(d.win->window());
	const bool ok = d.win->screenshot(file);
	ShowWindow(form.dialog, SW_SHOW);
	if (ok) d.add(Attachment::Screenshot, file, name, "image/png");
	else MessageBeep(MB_ICONWARNING);
}

int g_shotsField = -1;   // the attachments' picture field, for a recording that finishes later

// Record: this window steps away and the circuit window works as usual
// (Windows has it switched off behind this one, so it's switched on) until
// Stop, the ×, or a minute; then this window comes back with the recording
// on it. One recording at a time; Record Again replaces it.
void recordFromForm(Form& form) {
	Draft& d = draft();
	if (d.win == nullptr || capture::recording()) return;
	const HWND owner = d.win->window(), dialog = form.dialog;
	const std::string file = folder() + "\\" + uniqueId() + "-recording.mp4";
	Form* f = &form;
	ShowWindow(dialog, SW_HIDE);
	EnableWindow(owner, TRUE);
	SetForegroundWindow(owner);
	std::string why;
	const bool started = capture::startRecording(owner, file,
		[f, dialog, owner](const std::string& made, double seconds, const std::string& first, const std::string& problem) {
			if (!IsWindow(dialog)) return;
			EnableWindow(owner, FALSE);
			ShowWindow(dialog, SW_SHOW);
			SetForegroundWindow(dialog);
			Draft& dr = draft();
			if (!made.empty()) {
				for (size_t i = 0; i < dr.attachments.size(); i++)
					if (dr.attachments[i].kind == Attachment::Recording) { dr.remove(i); break; }
				dr.add(Attachment::Recording, made, "recording.mp4", "video/mp4", first, seconds);
			}
			if (!first.empty()) DeleteFileW(W(first).c_str());
			f->setProblem(problem);
			if (g_shotsField >= 0) f->refresh(g_shotsField);
		},
		why);
	if (!started) {
		EnableWindow(owner, FALSE);
		ShowWindow(dialog, SW_SHOW);
		SetForegroundWindow(dialog);
		form.setProblem(why);
		MessageBeep(MB_ICONWARNING);
	}
}

void attachImages(Form& form) {
	Draft& d = draft();
	const std::vector<std::string> files = chooseOpenFiles(form.dialog, "Pictures to Go With Your Feedback",
	                                                       { { "Pictures (*.png, *.jpg)", "*.png;*.jpg;*.jpeg" } }, true);
	for (const std::string& f : files) {
		if (d.imageCount() >= kMaxImages) break;
		std::string ext = f.substr(f.find_last_of('.') + 1);
		for (char& c : ext) c = (char)tolower((unsigned char)c);
		if (ext != "png" && ext != "jpg" && ext != "jpeg") continue;
		if (fileSize(f) <= 0 || fileSize(f) > 12000000) continue;
		const bool png = ext == "png";
		const std::string name = d.freeName("image", png ? "png" : "jpg");
		const std::string copy = folder() + "\\" + uniqueId() + "-" + name;
		if (CopyFileW(W(f).c_str(), W(copy).c_str(), FALSE)) d.add(Attachment::Image, copy, name, png ? "image/png" : "image/jpeg");
	}
}

}  // namespace

// --feedback-capture-test: Send Feedback opened, then, a step at a time
// from its timer, a screenshot, a three-second recording with the window
// zooming as it records (and a picture of the recording bar), the
// recording read back (its length and first frame), and a picture of the
// form with both on it. Each step says PASS or FAIL.
struct SelfTest {
	bool on = false;
	std::string dir;
	std::function<void(const std::string&)> say;
	int step = 0, failures = 0;
	double at = 0;   // when the next step is due
	void check(bool ok, const std::string& what) {
		say(std::string(ok ? "PASS " : "FAIL ") + what + "\n");
		if (!ok) failures++;
	}
};
SelfTest g_test;

const Attachment* attachmentOf(Attachment::Kind kind) {
	for (const Attachment& a : draft().attachments) if (a.kind == kind) return &a;
	return nullptr;
}

void selfTestStep(Form& form) {
	SelfTest& t = g_test;
	if (nowSeconds() < t.at) return;
	Draft& d = draft();
	switch (t.step++) {
	case 0: {
		attachScreenshot(form);
		const Attachment* a = attachmentOf(Attachment::Screenshot);
		t.check(a && fileSize(a->file) > 2000, "Screenshot: the window's picture is attached");
		t.check(a && a->thumb, "Screenshot: its thumbnail shows");
		if (a) CopyFileW(W(a->file).c_str(), W(t.dir + "\\feedback-screenshot.png").c_str(), FALSE);
		t.at = nowSeconds() + 0.5;
		break;
	}
	case 1:
		recordFromForm(form);
		t.check(capture::recording(), "Record: recording started");
		t.check(!IsWindowVisible(form.dialog), "Record: the form stepped out of the way");
		t.check(IsWindowEnabled(d.win->window()), "Record: the circuit window can be used while it records");
		t.at = nowSeconds() + 1.2;
		break;
	case 2: {
		// Something happening in the window while it records.
		d.win->run(CMD_ZOOM_IN);
		IWICBitmap* bar = capture::recordingBar() ? capture::window(capture::recordingBar()) : nullptr;
		t.check(bar != nullptr, "Record: the recording bar is up");
		if (bar) {
			capture::savePng(bar, t.dir + "\\recording-bar.png");
			bar->Release();
		}
		t.at = nowSeconds() + 1.8;
		break;
	}
	case 3: {
		capture::stopRecording(true);
		t.check(!capture::recording(), "Stop: the recording ended");
		t.check(IsWindowVisible(form.dialog), "Stop: the form came back");
		const Attachment* a = attachmentOf(Attachment::Recording);
		t.check(a && a->seconds >= 2.5, strf("Stop: the recording is attached (%.1f s)", a ? a->seconds : 0.0));
		if (a) {
			const long long size = fileSize(a->file);
			t.check(size > 2000 && size < 55000000, strf("The recording's file is a sensible size (%lld bytes)", size));
			const double secs = capture::probeVideo(a->file, t.dir + "\\feedback-recording-frame.png");
			t.check(secs >= 2.0, strf("The MP4 reads back (%.2f s long)", secs));
			t.check(fileSize(t.dir + "\\feedback-recording-frame.png") > 2000, "The MP4's first frame decodes");
			CopyFileW(W(a->file).c_str(), W(t.dir + "\\feedback-recording.mp4").c_str(), FALSE);
		}
		t.at = nowSeconds() + 0.6;
		break;
	}
	case 4: {
		IWICBitmap* shot = capture::window(form.dialog);
		if (shot) {
			capture::savePng(shot, t.dir + "\\feedback-attached.png");
			shot->Release();
		}
		// Thrown away (the ×): nothing kept, nothing attached.
		recordFromForm(form);
		t.at = nowSeconds() + 0.8;
		break;
	}
	case 5: {
		const size_t before = d.attachments.size();
		capture::stopRecording(false);
		t.check(d.attachments.size() == before && attachmentOf(Attachment::Recording) != nullptr,
		        "The \u00D7: a recording thrown away leaves the one before");
		d.clearAttachments();
		EndDialog(form.dialog, IDCANCEL);
		break;
	}
	default: break;
	}
}

int probe() {
	SetEnvironmentVariableA("CL_FEEDBACK_KEY", "probe-not-a-key");
	const Reply r = request("POST", "/api/feedback", "application/json", "", "{}");
	return r.reached ? r.status : 0;
}

// ---- Sent ----------------------------------------------------------------------
// The Mac's thank-you (FeedbackView's `sent`): a check that draws itself in
// a ring, "Thank you!" (with the first name they gave), where a reply would
// go, and Send Another or Done. True for Send Another.

namespace {

// A ring drawn from the top clockwise, `t` (0...1) of the way round, and the
// check in it once the ring's half done -- as the Mac's SentCheck, 88 points
// across, centred on (cx, cy).
void paintSentCheck(ID2D1RenderTarget* rt, float cx, float cy, double t, const D2D1_COLOR_F& color) {
	ID2D1Factory* f = nullptr;
	rt->GetFactory(&f);
	ID2D1SolidColorBrush* b = nullptr;
	ID2D1StrokeStyle* round = nullptr;
	if (f == nullptr || FAILED(rt->CreateSolidColorBrush(color, &b))) { if (f) f->Release(); return; }
	const D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
	                                                                    D2D1_LINE_JOIN_ROUND);
	f->CreateStrokeStyle(sp, nullptr, 0, &round);
	const float r = 42;   // inside the 88-point frame, the 4-point line's middle
	const double ring = std::min(1.0, std::max(0.0, t));
	if (ring > 0) {
		ID2D1PathGeometry* g = nullptr;
		ID2D1GeometrySink* s = nullptr;
		if (SUCCEEDED(f->CreatePathGeometry(&g)) && SUCCEEDED(g->Open(&s))) {
			auto at = [&](double a) { return D2D1::Point2F(cx + r * (float)std::sin(a), cy - r * (float)std::cos(a)); };
			const double end = ring * 2 * 3.14159265358979;
			s->BeginFigure(at(0), D2D1_FIGURE_BEGIN_HOLLOW);
			// In halves: one arc can't go all the way round.
			const double mid = std::min(end, 3.14159265358979);
			s->AddArc(D2D1::ArcSegment(at(mid), D2D1::SizeF(r, r), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
			if (end > mid)
				s->AddArc(D2D1::ArcSegment(at(end), D2D1::SizeF(r, r), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
			s->EndFigure(D2D1_FIGURE_END_OPEN);
			s->Close();
			rt->DrawGeometry(g, b, 4, round);
		}
		if (s) s->Release();
		if (g) g->Release();
	}
	// The check: (27,45) (39,57) (62,32) in the frame, drawn along its length.
	const double tick = std::max(0.0, std::min(1.0, (t - 0.5) * 2));
	if (tick > 0) {
		const D2D1_POINT_2F p[3] = { { cx - 44 + 27, cy - 44 + 45 }, { cx - 44 + 39, cy - 44 + 57 }, { cx - 44 + 62, cy - 44 + 32 } };
		const float l1 = std::hypot(p[1].x - p[0].x, p[1].y - p[0].y), l2 = std::hypot(p[2].x - p[1].x, p[2].y - p[1].y);
		float left = (float)tick * (l1 + l2);
		const float a = std::min(left, l1);
		rt->DrawLine(p[0], D2D1::Point2F(p[0].x + (p[1].x - p[0].x) * a / l1, p[0].y + (p[1].y - p[0].y) * a / l1), b, 5, round);
		left -= l1;
		if (left > 0)
			rt->DrawLine(p[1], D2D1::Point2F(p[1].x + (p[2].x - p[1].x) * left / l2, p[1].y + (p[2].y - p[1].y) * left / l2), b, 5, round);
	}
	if (round) round->Release();
	b->Release();
	f->Release();
}

bool showSent(CircuitWindow* win, const std::string& replyTo) {
	const double shown = nowSeconds();
	const bool calm = !fade::animations();
	std::string first = trimmed(prefs().studentName);
	first = first.substr(0, first.find(' '));
	const std::string heading = first.empty() ? "Thank you!" : "Thank you, " + first + "!";
	const std::string text = "Your feedback is on its way." + (replyTo.empty() ? std::string() : " If there's a question, the reply goes to " + replyTo + ".");

	Form f;
	f.title = "Send Feedback";
	f.width = 600;
	f.okText = "Done";
	f.cancelText = "";
	f.buttons = { "Send Another" };
	f.timerMs = 16;
	FormField card;
	card.kind = FormField::Picture;
	card.height = 300;
	card.paint = [&](ID2D1RenderTarget* rt, float w, float) {
		const FormLook look = formLook();
		// The ease the Mac's has (0.9 s, in and out).
		const double u = calm ? 1 : std::min(1.0, (nowSeconds() - shown) / 0.9);
		const double t = u < 0.5 ? 2 * u * u : 1 - std::pow(-2 * u + 2, 2) / 2;
		paintSentCheck(rt, w / 2, 76, t, D2D1::ColorF(0.05f, 0.78f, 0.26f));
		drawText(rt, heading, D2D1::RectF(16, 140, w - 16, 174), 24, look.ink, TextAlign::Center, true);
		// Wrapped (a reply address makes it two lines), as the Mac's 380 wide.
		brand::text(rt, text, (w - 380) / 2, 184, 13.5f, DWRITE_FONT_WEIGHT_NORMAL, look.dim, 380, DWRITE_TEXT_ALIGNMENT_CENTER);
	};
	const int cardField = f.add(card);
	f.onTimer = [&](Form& form) {
		if (nowSeconds() - shown < 1.0) form.refresh(cardField);
	};
	f.onButton = [](Form&, int) { return true; };
	return f.run(win->window()) == 100;
}

}  // namespace

void show(CircuitWindow* win) {
	// Recording for it already (Send Feedback chosen again from the menus or
	// the toolbar): that's Stop.
	if (capture::recording()) {
		capture::stopRecording(true);
		return;
	}
	Draft& d = draft();
	d.win = win;
	d.load();
	const Device dev = device(win);

	Form f;
	f.title = "Send Feedback";
	f.width = 600;
	f.okText = "";
	f.cancelText = "Close";
	f.buttons = { "Send Feedback" };
	f.timerMs = 120;

	FormField band;
	band.kind = FormField::Picture;
	band.height = 92;
	band.paint = [](ID2D1RenderTarget* rt, float w, float h) { paintBand(rt, w, h); };
	f.add(band);
	auto heading = [&](const char* text) {
		FormField n;
		n.kind = FormField::Note;
		n.label = text;
		return f.add(n);
	};
	heading("WHAT'S ON YOUR MIND?");
	FormField title;
	title.kind = FormField::Text;
	title.label = "Summary";
	title.value = d.title;
	const std::string titleTip = "A short summary, like “C doesn't connect the wire”";
	const std::string summaryHint = "Add a short summary to send it. A few words is plenty.";
	title.tip = titleTip;
	const int titleField = f.add(title);
	FormField details;
	details.kind = FormField::Text;
	details.label = "Details";
	details.lines = 5;
	details.value = d.details;
	details.tip = "What happened, what you expected, and how to make it happen again (if you know).";
	const int detailsField = f.add(details);
	const int tagsHeading = heading("TAGS");
	FormField tags;
	tags.kind = FormField::Picture;
	tags.height = 2 * (int)kChipH + (int)kChipGap + 2;
	tags.paint = [](ID2D1RenderTarget* rt, float w, float) { paintTags(rt, w); };
	const int tagsField = f.add(tags);
	heading("HOW MUCH DOES IT MATTER?");
	FormField priority;
	priority.kind = FormField::Picture;
	priority.height = 56;
	priority.paint = [](ID2D1RenderTarget* rt, float w, float) { paintPriority(rt, w); };
	const int priorityField = f.add(priority);
	heading("SHOW IT  ·  only the CedarLogic window is captured");
	FormField shots;
	shots.kind = FormField::Picture;
	shots.height = (int)kShotsHeight;
	shots.paint = [](ID2D1RenderTarget* rt, float w, float) { paintAttachments(rt, w); };
	const int shotsField = f.add(shots);
	g_shotsField = shotsField;
	FormField circuit;
	circuit.kind = FormField::Check;
	circuit.label = "Send this circuit too (makes a problem easy to repeat; it's only sent with this)";
	circuit.value = d.includeCircuit ? "1" : "";
	const int circuitField = f.add(circuit);
	heading("ABOUT YOU");
	FormField name;
	name.kind = FormField::Text;
	name.label = "Name";
	name.value = prefs().studentName;
	const int nameField = f.add(name);
	FormField email;
	email.kind = FormField::Text;
	email.label = "Email";
	email.value = d.email;
	email.tip = "If you'd like a reply";
	const int emailField = f.add(email);
	FormField contact;
	contact.kind = FormField::Check;
	contact.label = "You can email me about this";
	contact.value = d.contactOK ? "1" : "";
	const int contactField = f.add(contact);
	FormField also;
	also.kind = FormField::Note;
	also.label = "Also sent: " + dev.line;
	f.add(also);

	// Ten reports in a day already (the Mac's capped box): what the server
	// said in an orange box, and, once a day, a line saying why and Request
	// more. Then "You can send 10 more." in green. Put away until needed.
	enum Phase { Writing, Going, Failed, Capped };
	Phase phase = Writing;
	bool canMore = false;            // Capped: Request more would work
	std::string capText, notice;
	bool summaryNeeded = false;      // the server said the summary was empty
	const D2D1_COLOR_F orange = kPriorities[2].color;
	FormField capBox;
	capBox.kind = FormField::Picture;
	capBox.height = 58;
	capBox.hidden = true;
	capBox.paint = [&](ID2D1RenderTarget* rt, float w, float h) {
		const FormLook look = formLook();
		const D2D1_RECT_F r = D2D1::RectF(0.5f, 0.5f, w - 0.5f, h - 0.5f);
		fillRound(rt, r, 11, withAlpha(orange, prefs().dark ? 0.12f : 0.08f));
		strokeRound(rt, r, 11, withAlpha(orange, 0.35f));
		// An hourglass, drawn: two triangles point to point.
		ID2D1SolidColorBrush* b = nullptr;
		if (SUCCEEDED(rt->CreateSolidColorBrush(orange, &b))) {
			const float x = 18, y = 16;
			rt->DrawLine(D2D1::Point2F(x - 4, y - 5), D2D1::Point2F(x + 4, y - 5), b, 1.4f);
			rt->DrawLine(D2D1::Point2F(x - 4, y + 5), D2D1::Point2F(x + 4, y + 5), b, 1.4f);
			rt->DrawLine(D2D1::Point2F(x - 3.5f, y - 5), D2D1::Point2F(x + 3.5f, y + 5), b, 1.2f);
			rt->DrawLine(D2D1::Point2F(x + 3.5f, y - 5), D2D1::Point2F(x - 3.5f, y + 5), b, 1.2f);
			b->Release();
		}
		brand::text(rt, capText, 30, 9, 12.5f, DWRITE_FONT_WEIGHT_NORMAL, look.ink, w - 42);
	};
	const int capField = f.add(capBox);
	FormField reason;
	reason.kind = FormField::Text;
	reason.placeholder = "What's it about? (optional)";
	reason.hidden = true;
	const int reasonField = f.add(reason);
	FormField moreButton;
	moreButton.kind = FormField::Button;
	moreButton.label = "Request more";
	moreButton.beside = true;
	moreButton.hidden = true;
	const int moreField = f.add(moreButton);
	FormField noticeLine;
	noticeLine.kind = FormField::Picture;
	noticeLine.height = 20;
	noticeLine.hidden = true;
	noticeLine.paint = [&](ID2D1RenderTarget* rt, float w, float) {
		const D2D1_COLOR_F green = D2D1::ColorF(0.05f, 0.78f, 0.26f);
		fillRound(rt, D2D1::RectF(0, 4, 12, 16), 6, green);
		ID2D1SolidColorBrush* b = nullptr;
		if (SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &b))) {
			rt->DrawLine(D2D1::Point2F(3, 10), D2D1::Point2F(5.3f, 12.3f), b, 1.5f);
			rt->DrawLine(D2D1::Point2F(5.3f, 12.3f), D2D1::Point2F(9.2f, 7.4f), b, 1.5f);
			b->Release();
		}
		drawText(rt, notice, D2D1::RectF(18, 1, w, 19), 11.5f, green, TextAlign::Leading, true);
	};
	const int noticeField = f.add(noticeLine);

	std::shared_ptr<Sending>& sending = inFlight();
	std::shared_ptr<Asking> asking;   // Request more, going
	bool sentNow = false;     // sent while it was open
	std::string sentEmail;    // where a reply would go (if they said so)
	HWND sendTip = nullptr;   // "Add a short summary first", over Send while it's greyed
	// This window follows a send that's still going.
	if (g_watch) {
		KillTimer(nullptr, g_watch);
		g_watch = 0;
	}
	auto collect = [&](Form& form) {
		d.title = form.text(titleField);
		d.details = form.text(detailsField);
		d.email = form.text(emailField);
		d.contactOK = form.checked(contactField);
		d.includeCircuit = form.checked(circuitField);
		prefs().studentName = trimmed(form.text(nameField));
		d.save();
	};
	auto refreshTagsHeading = [&](Form& form) {
		form.setText(tagsHeading, d.suggested().empty() ? "TAGS" : "TAGS  ·  ✦ suggested from what you wrote");
	};
	auto titleBlank = [&](Form& form) { return trimmed(form.text(titleField)).empty(); };
	// The tooltip over Send. It's the dialog's (a greyed button gets no
	// pointer), over where the button is now.
	auto placeSendTip = [&](Form& form) {
		HWND send = GetDlgItem(form.dialog, kSendButton);
		if (sendTip == nullptr || send == nullptr) return;
		TOOLINFOW ti = { sizeof ti };
		ti.hwnd = form.dialog;
		ti.uId = 1;
		GetWindowRect(send, &ti.rect);
		MapWindowPoints(nullptr, form.dialog, (POINT*)&ti.rect, 2);
		SendMessageW(sendTip, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
	};
	// Send Feedback (or Try Again): greyed with no summary, while it goes,
	// and while Request more is the way on -- as the Mac's canSend.
	auto updateSend = [&](Form& form) {
		HWND send = GetDlgItem(form.dialog, kSendButton);
		if (send == nullptr) return;
		const bool blank = titleBlank(form);
		const bool can = phase != Going && !asking && !(phase == Capped && canMore) && !blank && !capture::recording();
		EnableWindow(send, can);
		SetWindowTextW(send, phase == Failed || (phase == Capped && !canMore) ? L"Try Again" : L"Send Feedback");
		InvalidateRect(send, nullptr, FALSE);
		if (sendTip) SendMessageW(sendTip, TTM_ACTIVATE, blank, 0);
		placeSendTip(form);
		// The hint under the summary: once there's something to send without
		// one, or the server asked (then in orange).
		const bool hint = blank && (summaryNeeded || !trimmed(form.text(detailsField)).empty() || !d.attachments.empty());
		form.setTip(titleField, hint ? summaryHint : titleTip);
		form.markTip(titleField, hint && summaryNeeded ? 1 : 0);
	};
	// The capped box, Request more and the notice: shown as the state says.
	auto layoutCap = [&](Form& form) {
		const bool capped = phase == Capped;
		form.fields[capField].hidden = !capped;
		form.fields[reasonField].hidden = form.fields[moreField].hidden = !(capped && canMore);
		form.fields[noticeField].hidden = notice.empty() || capped;
		form.hiddenChanged(0);
		form.refresh(capField);
		form.refresh(noticeField);
		// Down where it shows, in a form that scrolls.
		if ((capped || !notice.empty()) && form.scrollHeight > 0) form.scrollTo(form.scrollHeight);
		placeSendTip(form);
	};
	// A send (or a Try Again) that didn't go: the server's answer, as the Mac shows it.
	auto showFailure = [&](Form& form, const Failure& why) {
		form.setProgress(-1);
		notice.clear();
		if (why.code == "sender_cap") {
			phase = Capped;
			canMore = why.canRequestMore;
			capText = why.message;
			form.setProblem("");
		} else {
			phase = Failed;
			if (why.code == "summary_required") summaryNeeded = true;
			form.setProblem(why.message);
		}
		layoutCap(form);
		updateSend(form);
		MessageBeep(MB_ICONWARNING);
	};
	f.onInit = [&](Form& form) {
		refreshTagsHeading(form);
		form.enable(contactField, !trimmed(d.email).empty());
		sendTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, form.dialog,
		                          nullptr, appInstance(), nullptr);
		if (sendTip) {
			TOOLINFOW ti = { sizeof ti };
			ti.uFlags = TTF_SUBCLASS;
			ti.hwnd = form.dialog;
			ti.uId = 1;
			ti.lpszText = const_cast<wchar_t*>(L"Add a short summary first");
			SendMessageW(sendTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
		}
		if (sending) {
			phase = Going;
			std::lock_guard<std::mutex> g(sending->lock);
			form.setProgress(sending->fraction, sending->note);
		} else if (g_hasLastFailure) {
			g_hasLastFailure = false;
			showFailure(form, g_lastFailure);
		}
		if (g_sample == 1) {
			phase = Going;
			form.setProgress(0.42, "Sending screenshot.png… 0.4 of 1.0 MB");
		} else if (g_sample == 2) {
			Failure x;
			x.message = "Couldn't reach the feedback server. Check the internet connection and try again.";
			showFailure(form, x);
		} else if (g_sample == 4) {
			Failure x;
			x.code = "sender_cap";
			x.canRequestMore = true;
			x.message = "You've sent 10 reports in the last day. Tap 'Request more' if you have another problem.";
			showFailure(form, x);
		} else if (g_sample == 5) {
			form.setText(titleField, "");
			form.setText(detailsField, "The C key doesn't connect the wire when the pins are close.");
		}
		updateSend(form);
	};
	f.onChange = [&](Form& form, int field) {
		if (field == moreField) {
			// Request more: 10 more for a day, with the line saying why.
			if (phase != Capped || !canMore || asking) return;
			collect(form);
			Json extra;
			Json app;
			app.text("version", CL_VERSION).text("build", CL_GIT_COMMIT);
			extra.text("name", prefs().studentName).text("platform", "windows").raw("app", app.str());
			const std::string mail = trimmed(d.email);
			if (d.contactOK && !mail.empty()) extra.text("email", mail);
			asking = std::make_shared<Asking>();
			std::thread(askForMore, asking, form.text(reasonField), extra).detach();
			EnableWindow(form.fields[moreField].hwnd, FALSE);
			EnableWindow(form.fields[reasonField].hwnd, FALSE);
			form.setProblem("");
			form.setProgress(0, "Asking for more…");
			updateSend(form);
			return;
		}
		if (field == titleField || field == detailsField) {
			d.title = form.text(titleField);
			d.details = form.text(detailsField);
			refreshTagsHeading(form);
			form.refresh(tagsField);
		}
		if (field == emailField) form.enable(contactField, !trimmed(form.text(emailField)).empty());
		// The server asked for a summary and now there is one.
		if (field == titleField && summaryNeeded && !titleBlank(form)) {
			summaryNeeded = false;
			if (phase == Failed) { phase = Writing; form.setProblem(""); }
		}
		if (field != reasonField && phase == Failed) form.setProblem("");
		updateSend(form);
	};
	f.onClick = [&](Form& form, int field, float x, float y) {
		if (sending) return;
		RECT rc;
		GetClientRect(form.fields[field].hwnd, &rc);
		const float w = rc.right / (dpiOf(form.dialog) / 96.0f);
		if (field == tagsField) {
			const std::vector<D2D1_RECT_F> r = chipRects(w);
			for (int i = 0; i < kTagCount; i++) if (inRect(r[i], x, y)) d.toggle(kTags[i]);
		} else if (field == priorityField) {
			for (int i = 0; i < 4; i++) if (inRect(pillRect(i, w), x, y)) d.priority = i;
		} else if (field == shotsField) {
			for (size_t i = 0; i < d.attachments.size(); i++)
				if (inRect(closeRect(i, w), x, y)) { d.remove(i); form.refresh(field); updateSend(form); return; }
			for (size_t i = 0; i < d.attachments.size(); i++)
				if (inRect(thumbRect(i, w), x, y)) { openExternally(form.dialog, d.attachments[i].file); return; }
			if (capture::recording()) return;
			if (inRect(tileRect(0), x, y)) attachScreenshot(form);
			else if (inRect(tileRect(1), x, y)) recordFromForm(form);
			else if (inRect(tileRect(2), x, y)) attachImages(form);
			updateSend(form);
		}
		form.refresh(field);
	};
	f.onButton = [&](Form& form, int) {
		if (sending || asking) return false;
		collect(form);
		// (Send is greyed without a summary; Enter could still get here.)
		if (trimmed(d.title).empty()) {
			updateSend(form);
			SetFocus(form.fields[titleField].hwnd);
			MessageBeep(MB_ICONWARNING);
			return false;
		}
		if (phase == Capped && canMore) return false;
		std::vector<Attachment> files = d.attachments;
		for (Attachment& a : files) a.thumb = nullptr;   // the thread needs only the files
		if (d.includeCircuit) {
			const std::string file = folder() + "\\" + uniqueId() + "-circuit.cdl";
			// Without the student's notes: they're theirs, not the bug's.
			const char* text = cl_document_save_text_ex(win->document(), CL_SAVE_NO_NOTES);
			if (text && writeAll(file, text)) {
				Attachment c;
				c.kind = Attachment::Circuit;
				c.file = file;
				c.name = "circuit.cdl";
				c.type = "text/plain";
				files.push_back(c);
			}
		}
		std::string list = "[";
		for (size_t i = 0; i < files.size(); i++) {
			Json item;
			item.text("name", files[i].name).text("type", files[i].type).number("size", fileSize(files[i].file));
			if (files[i].kind == Attachment::Recording) item.number("duration", std::max(1LL, (long long)std::lround(files[i].seconds)));
			list += (i ? "," : "") + item.str();
		}
		list += "]";
		std::string tagList = "[", autoList = "[";
		for (const std::string& t : d.tags()) tagList += (tagList.size() > 1 ? "," : "") + jsonQuote(t);
		for (const std::string& t : d.suggested()) autoList += (autoList.size() > 1 ? "," : "") + jsonQuote(t);
		const std::string mail = trimmed(d.email);
		Json body;
		body.text("title", trimmed(d.title)).text("details", trimmed(d.details)).raw("tags", tagList + "]").raw("autoTags", autoList + "]")
			.text("priority", kPriorities[d.priority].id).text("name", prefs().studentName).text("email", mail)
			.flag("contactOK", d.contactOK && !mail.empty()).text("platform", "windows").raw("attachments", list)
			.raw("app", dev.app).raw("system", dev.system).raw("context", dev.context);
		sending = std::make_shared<Sending>();
		std::thread(upload, sending, body, files).detach();
		// Close stays: it carries on without the window.
		phase = Going;
		notice.clear();
		layoutCap(form);
		form.setProblem("");
		form.setProgress(0, "Sending…");
		updateSend(form);
		sentEmail = d.contactOK ? mail : std::string();
		return false;
	};
	f.onTimer = [&](Form& form) {
		if (g_test.on) { selfTestStep(form); return; }
		placeSendTip(form);
		if (asking && asking->state != 0) {
			const std::shared_ptr<Asking> a = asking;
			asking.reset();
			form.setProgress(-1);
			EnableWindow(form.fields[moreField].hwnd, TRUE);
			EnableWindow(form.fields[reasonField].hwnd, TRUE);
			Failure why;
			long granted = 10;
			{
				std::lock_guard<std::mutex> g(a->lock);
				why = a->failure;
				granted = a->granted;
			}
			if (a->state == 1) {
				// Given more: Send works again.
				form.setText(reasonField, "");
				notice = strf("You can send %ld more.", granted);
				phase = Writing;
			} else if (why.code == "not_capped") {
				// Room after all (one aged out): say so and carry on.
				notice = why.message;
				phase = Writing;
			} else if (why.code == "more_used") {
				canMore = false;
				capText = why.message;
			} else {
				form.setProblem(why.message);
				MessageBeep(MB_ICONWARNING);
			}
			layoutCap(form);
			updateSend(form);
			return;
		}
		if (!sending) return;
		if (sending->state == 0) {
			std::lock_guard<std::mutex> g(sending->lock);
			form.setProgress(sending->fraction, sending->note);
			return;
		}
		if (sending->state == 1) {
			sending.reset();
			clearSentDraft();
			form.setProgress(1, "Sent");
			sentNow = true;
			// The form dissolves into the thank-you card (shown below, once
			// it's gone), as the Mac's turns into it.
			RECT dr;
			GetWindowRect(form.dialog, &dr);
			MapWindowPoints(nullptr, win->window(), (POINT*)&dr, 2);
			fade::crossfade(win->window(), &dr, 260);
			EndDialog(form.dialog, IDOK);
			return;
		}
		Failure why;
		{
			std::lock_guard<std::mutex> g(sending->lock);
			why = sending->failure;
		}
		sending.reset();
		showFailure(form, why);
	};

	const int r = f.run(win->window());
	// (A recording can't outlive the form it attaches to.)
	if (capture::recording()) capture::stopRecording(false);
	g_shotsField = -1;
	if (r != IDOK && !sending) {
		// Kept as a draft for next time.
		d.title = f.fields[titleField].value;
		d.details = f.fields[detailsField].value;
		d.email = f.fields[emailField].value;
		d.contactOK = !f.fields[contactField].value.empty();
		d.includeCircuit = !f.fields[circuitField].value.empty();
		prefs().studentName = trimmed(f.fields[nameField].value);
		d.save();
		syncapp::nameChanged();
	}
	// Closed while sending: finished without it.
	if (sending) g_watch = SetTimer(nullptr, 0, 400, watchSend);
	// Sent with the window up: the thank-you, and perhaps another.
	if (sentNow && showSent(win, sentEmail)) show(win);
}

// --feedback-check: the Mac's mac/Tools/feedback-check (main.swift), case
// for case, against its stand-in server (server.py) at CL_FEEDBACK_URL.
int serverCheck(std::function<void(const std::string&)> say) {
	int fails = 0;
	auto ok = [&](bool c, const std::string& m) {
		say(std::string(c ? "ok   " : "FAIL ") + m + "\n");
		if (!c) fails++;
	};
	const std::string kept = prefs().installId;
	auto hits = [] {
		const Reply r = request("GET", "/stats", "application/json", "", "");
		return atol(jsonField(r.body, "/api/feedback").c_str());
	};
	auto installs = [] {
		const Reply r = request("GET", "/stats", "application/json", "", "");
		std::vector<std::string> out;
		size_t at = r.body.find("\"installs\"");
		if (at == std::string::npos || (at = r.body.find('[', at)) == std::string::npos) return out;
		const size_t end = r.body.find(']', at);
		while ((at = r.body.find('"', at + 1)) != std::string::npos && at < end) {
			const size_t close = r.body.find('"', at + 1);
			if (close == std::string::npos) break;
			const std::string id = r.body.substr(at + 1, close - at - 1);
			if (!id.empty() && std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
			at = close;
		}
		std::sort(out.begin(), out.end());
		return out;
	};
	auto report = [](const std::string& title) {
		Json j;
		j.text("title", title).text("details", "").text("platform", "windows").raw("attachments", "[]");
		return j;
	};

	// The install id: made once, kept, and remade if it isn't the right shape.
	prefs().installId.clear();
	const std::string id = installId();
	ok(id.size() == 32 && isInstallId(id) && id.find('-') == std::string::npos, "the install id is 32 letters and digits (" + id + ")");
	ok(installId() == id, "...and the same the next time");
	prefs().installId = "not one!";
	const std::string again = installId();
	ok(again != "not one!" && isInstallId(again), "one that isn't the right shape is made again");
	ok(!isInstallId("short") && !isInstallId(std::string(65, 'a')) && isInstallId("abc_DEF-123"), "isInstallId takes 8 to 64 of A-Z a-z 0-9 _ -");

	// The summary.
	Created c;
	Failure blank;
	const bool blankWent = createReport(report("  "), c, blank);
	ok(!blankWent && blank.code == "summary_required" && blank.status == 400 && blank.message.rfind("Add a summary", 0) == 0,
	   "an empty summary: 400 summary_required, with the server's sentence");

	// Ten, then the cap: answered once, not tried again.
	int sent = 0;
	for (int i = 1; i <= 10; i++) {
		Failure f;
		if (createReport(report(strf("Report %d", i)), c, f) && completeReport(c, f)) sent++;
	}
	ok(sent == 10, strf("ten reports go (%d)", sent));
	const long before = hits();
	const double t0 = nowSeconds();
	Failure cap;
	createReport(report("Report 11"), c, cap);
	const double took = nowSeconds() - t0;
	const long after = hits();
	ok(cap.code == "sender_cap" && cap.status == 429 && cap.canRequestMore && cap.retryAfter == 82800, "the eleventh: 429 sender_cap, canRequestMore, retryAfter");
	ok(cap.message.find("Request more") != std::string::npos, "...with the server's sentence to show (" + cap.message + ")");
	ok(after - before == 1 && took < 1, strf("...asked once, not tried again (%ld requests)", after - before));

	// Request more: ten more, then not again.
	Json extra;
	extra.text("platform", "windows");
	long n = 0;
	Failure moreFail;
	const bool gotMore = requestMore("Another bug in the clock", extra, n, moreFail);
	ok(gotMore && n == 10, strf("Request more gives 10 more (%ld)", gotMore ? n : -1));
	Failure f11;
	ok(createReport(report("Report 11, after asking"), c, f11), "...and the next report goes");
	Failure twice;
	requestMore("", Json(), n, twice);
	ok(twice.code == "not_capped", "asking while there's room: not_capped (" + twice.message + ")");
	for (int i = 12; i <= 20; i++) {
		Failure f;
		createReport(report(strf("Report %d", i)), c, f);
	}
	Failure capped2;
	createReport(report("Report 21"), c, capped2);
	ok(capped2.code == "sender_cap" && !capped2.canRequestMore, "capped again: no Request more this time");
	Failure used;
	requestMore("", Json(), n, used);
	ok(used.code == "more_used" && used.status == 429, "asking again: more_used, not tried again");

	// A 429 without a code (too many at once) is still tried again.
	request("POST", "/api/feedback/plain429", "application/json", "", "{\"n\": 2}");
	prefs().installId = "othermac-0001";   // another sender, not capped
	const long h0 = hits();
	Failure plain;
	const bool retried = createReport(report("After two plain 429s"), c, plain);
	const long h1 = hits();
	ok(retried && h1 - h0 == 3, strf("a plain 429 is tried again, three tries in all (%ld)", h1 - h0));

	std::vector<std::string> want = { again, "othermac-0001" };
	std::sort(want.begin(), want.end());
	const std::vector<std::string> seen = installs();
	std::string list;
	for (const std::string& s : seen) list += (list.empty() ? "" : ", ") + s;
	ok(seen == want, "every report and Request more carried the install id (" + list + ")");

	prefs().installId = kept;
	prefs().save();
	say(fails == 0 ? "feedback-check: all passed\n" : strf("feedback-check: %d failed\n", fails));
	return fails;
}

void showSample(CircuitWindow* win, int state) {
	if (state == 3) {
		showSent(win, "student@example.edu");
		return;
	}
	g_sample = state;
	show(win);
	g_sample = 0;
}

bool recording() { return capture::recording(); }

void stopRecording() { capture::stopRecording(true); }

int captureSelfTest(CircuitWindow* win, const std::string& dir, std::function<void(const std::string&)> say) {
	g_test = SelfTest();
	g_test.on = true;
	g_test.dir = dir;
	g_test.say = std::move(say);
	g_test.at = nowSeconds() + 1.0;
	show(win);
	g_test.on = false;
	if (g_test.step < 6) g_test.check(false, strf("The test finished every step (it stopped at %d)", g_test.step));
	return g_test.failures;
}

}  // namespace feedback
