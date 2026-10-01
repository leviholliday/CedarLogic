// Send Feedback (see Feedback.h). Top to bottom: the draft and its tags,
// what's sent about the PC, talking to the site (WinHTTP, on a thread of
// its own), and the window.

#include "Feedback.h"
#include "Canvas.h"
#include "Chrome.h"
#include "Dialogs.h"
#include "Images.h"
#include "Window.h"

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
	enum Kind { Screenshot, Image, Circuit } kind = Image;
	std::string file, name, type;
	IWICBitmap* thumb = nullptr;
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
		for (const Attachment& a : attachments) if (a.kind != Attachment::Circuit) n++;
		return n;
	}
	std::string freeName(const char* base, const char* ext) const {
		for (int n = 1;; n++) {
			const std::string name = strf("%s-%d.%s", base, n, ext);
			bool used = false;
			for (const Attachment& a : attachments) used = used || a.name == name;
			if (!used) return name;
		}
	}
	void add(Attachment::Kind kind, const std::string& file, const std::string& name, const std::string& type) {
		Attachment a;
		a.kind = kind;
		a.file = file;
		a.name = name;
		a.type = type;
		a.thumb = images::load(file, 264, 168);
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

struct Device { std::string app, system, context, line; };

Device device(CircuitWindow* win) {
	Device d;
	const std::string os = osName();
	Json app;
	app.text("name", "CedarLogic for Windows").text("version", CL_VERSION).text("build", CL_GIT_COMMIT).text("commit", CL_GIT_COMMIT)
		.text("channel", "Native testing");
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

struct Reply { int status = 0; std::string body; bool reached = false; };

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
	if (req && WinHttpSendRequest(req, headers.c_str(), (DWORD)-1L, body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
	                              (DWORD)body.size(), (DWORD)body.size(), 0) &&
	    WinHttpReceiveResponse(req, nullptr)) {
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

std::string percent(const std::string& s) {
	std::string o;
	for (unsigned char c : s) {
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
		else o += strf("%%%02X", c);
	}
	return o;
}

// A send in progress, shared with its thread (it outlives the window).
struct Sending {
	std::mutex lock;
	std::string note = "Sending…";
	double fraction = 0;
	std::atomic<int> state{ 0 };   // 0 going, 1 sent, 2 failed
	std::string failure;
	void set(const std::string& n, double f) {
		std::lock_guard<std::mutex> g(lock);
		note = n;
		fraction = f;
	}
};

// Tried again a few times: a flaky connection shouldn't lose a report.
Reply perform(const std::string& method, const std::string& path, const std::string& type, const std::string& token,
              const std::string& body, std::string& failure) {
	Reply r;
	for (int attempt = 0; attempt < 3; attempt++) {
		r = request(method, path, type, token, body);
		if (r.reached && r.status >= 200 && r.status < 300) return r;
		if (r.reached) {
			const std::string why = jsonField(r.body, "error");
			failure = why.empty() ? strf("The feedback server said no (%d).", r.status) : why;
			if (r.status < 500 && r.status != 429) return r;
		} else {
			failure = "Couldn't reach the feedback server. Check the internet connection and try again.";
		}
		Sleep((DWORD)(700 * std::pow(2, attempt)));
	}
	r.status = 0;
	return r;
}

void upload(std::shared_ptr<Sending> s, std::string body, std::vector<Attachment> files) {
	auto fail = [&](const std::string& why) {
		{
			std::lock_guard<std::mutex> g(s->lock);
			s->failure = why;
		}
		s->state = 2;
	};
	std::string failure;
	const Reply created = perform("POST", "/api/feedback", "application/json", "", body, failure);
	if (created.status < 200 || created.status >= 300) { fail(failure); return; }
	const std::string id = jsonField(created.body, "id"), token = jsonField(created.body, "uploadToken");
	const long long chunk = std::max(65536LL, atoll(jsonField(created.body, "chunkSize").c_str()));
	if (id.empty() || token.empty()) { fail("The feedback server's answer didn't make sense."); return; }
	long long total = 1, done = 0;
	for (const Attachment& a : files) total += fileSize(a.file);
	for (const Attachment& a : files) {
		const std::string data = readAll(a.file);
		const long long pieces = std::max(1LL, (long long)((data.size() + chunk - 1) / chunk));
		for (long long i = 0; i < pieces; i++) {
			const std::string part = data.substr((size_t)(i * chunk), (size_t)chunk);
			const std::string q = "/api/feedback/upload?id=" + percent(id) + "&file=" + percent(a.name) + strf("&index=%lld&total=%lld", i, pieces);
			const Reply r = perform("PUT", q, "application/octet-stream", token, part, failure);
			if (r.status < 200 || r.status >= 300) { fail(failure); return; }
			done += (long long)part.size();
			s->set(strf("Sending %s… %.1f of %.1f MB", a.name.c_str(), done / 1e6, total / 1e6), (double)done / total);
		}
	}
	const Reply r = perform("POST", "/api/feedback/complete?id=" + percent(id), "application/json", token, "", failure);
	if (r.status < 200 || r.status >= 300) { fail(failure); return; }
	s->state = 1;
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

const float kTileW = 116, kTileH = 72, kThumbW = 112;

D2D1_RECT_F tileRect(int i) { return D2D1::RectF(i * (kTileW + 10), 6, i * (kTileW + 10) + kTileW, 6 + kTileH); }
D2D1_RECT_F thumbRect(size_t i) {
	const float x0 = 2 * (kTileW + 10) + 8;
	return D2D1::RectF(x0 + i * (kThumbW + 12), 6, x0 + i * (kThumbW + 12) + kThumbW, 6 + kTileH);
}
D2D1_RECT_F closeRect(size_t i) {
	const D2D1_RECT_F t = thumbRect(i);
	return D2D1::RectF(t.right - 12, t.top - 6, t.right + 6, t.top + 12);
}

void paintAttachments(ID2D1RenderTarget* rt, float) {
	const Look l = look();
	const bool more = draft().imageCount() < kMaxImages;
	const wchar_t glyphs[] = { 0xE722, 0xEB9F };   // camera, photo
	const char* labels[] = { "Screenshot", "Add image…" };
	for (int i = 0; i < 2; i++) {
		const D2D1_RECT_F r = tileRect(i);
		const float a = more ? 1 : 0.45f;
		fillRound(rt, r, 11, l.card);
		strokeRound(rt, D2D1::RectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f), 11, l.line);
		drawIcon(rt, glyphs[i], D2D1::RectF(r.left, r.top + 12, r.right, r.top + 38), 19, withAlpha(l.accent, a));
		drawText(rt, labels[i], D2D1::RectF(r.left, r.top + 44, r.right, r.bottom), 12, withAlpha(l.ink, a), TextAlign::Center);
	}
	for (size_t i = 0; i < draft().attachments.size(); i++) {
		const Attachment& a = draft().attachments[i];
		const D2D1_RECT_F r = thumbRect(i);
		fillRound(rt, r, 9, D2D1::ColorF(0, 0, 0, 0.3f));
		if (a.thumb) {
			ID2D1Bitmap* bmp = nullptr;
			if (SUCCEEDED(rt->CreateBitmapFromWicBitmap(a.thumb, &bmp))) {
				const D2D1_SIZE_F s = bmp->GetSize();
				// Filled, centred, clipped to the rounded card.
				const float k = std::max(kThumbW / s.width, kTileH / s.height);
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
		strokeRound(rt, D2D1::RectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f), 9, l.line);
		const D2D1_RECT_F c = closeRect(i);
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

int probe() {
	SetEnvironmentVariableA("CL_FEEDBACK_KEY", "probe-not-a-key");
	const Reply r = request("POST", "/api/feedback", "application/json", "", "{}");
	return r.reached ? r.status : 0;
}

void show(CircuitWindow* win) {
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
	title.tip = "A short summary, like “C doesn't connect the wire”";
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
	shots.height = 84;
	shots.paint = [](ID2D1RenderTarget* rt, float w, float) { paintAttachments(rt, w); };
	const int shotsField = f.add(shots);
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

	std::shared_ptr<Sending> sending;
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
	f.onInit = [&](Form& form) {
		refreshTagsHeading(form);
		form.enable(contactField, !trimmed(d.email).empty());
	};
	f.onChange = [&](Form& form, int field) {
		if (field == titleField || field == detailsField) {
			d.title = form.text(titleField);
			d.details = form.text(detailsField);
			refreshTagsHeading(form);
			form.refresh(tagsField);
		}
		if (field == emailField) form.enable(contactField, !trimmed(form.text(emailField)).empty());
		form.setProblem("");
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
				if (inRect(closeRect(i), x, y)) { d.remove(i); form.refresh(field); return; }
			for (size_t i = 0; i < d.attachments.size(); i++)
				if (inRect(thumbRect(i), x, y)) { openExternally(form.dialog, d.attachments[i].file); return; }
			if (inRect(tileRect(0), x, y)) attachScreenshot(form);
			else if (inRect(tileRect(1), x, y)) attachImages(form);
		}
		form.refresh(field);
	};
	f.onButton = [&](Form& form, int) {
		if (sending) return false;
		collect(form);
		if (trimmed(d.title).empty()) {
			form.setProblem("Write a short summary first.");
			SetFocus(form.fields[titleField].hwnd);
			MessageBeep(MB_ICONWARNING);
			return false;
		}
		std::vector<Attachment> files = d.attachments;
		for (Attachment& a : files) a.thumb = nullptr;   // the thread needs only the files
		if (d.includeCircuit) {
			const std::string file = folder() + "\\" + uniqueId() + "-circuit.cdl";
			const char* text = cl_document_save_text(win->document());
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
		for (size_t i = 0; i < files.size(); i++)
			list += (i ? "," : "") + Json().text("name", files[i].name).text("type", files[i].type).number("size", fileSize(files[i].file)).str();
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
		std::thread(upload, sending, body.str(), files).detach();
		for (HWND b : form.buttonWindows) EnableWindow(b, FALSE);
		form.setProblem("Sending…");
		return false;
	};
	f.onTimer = [&](Form& form) {
		if (!sending) return;
		if (sending->state == 0) {
			std::lock_guard<std::mutex> g(sending->lock);
			form.setProblem(sending->note);
			return;
		}
		if (sending->state == 1) {
			// Sent: the draft is cleared for next time.
			d.clearAttachments();
			d.title.clear();
			d.details.clear();
			d.chosen.clear();
			d.declined.clear();
			d.priority = 1;
			d.includeCircuit = false;
			d.save();
			win->note("Feedback sent. Thank you!");
			EndDialog(form.dialog, IDOK);
			return;
		}
		std::string why;
		{
			std::lock_guard<std::mutex> g(sending->lock);
			why = sending->failure;
		}
		sending.reset();
		for (HWND b : form.buttonWindows) EnableWindow(b, TRUE);
		if (HWND send = GetDlgItem(form.dialog, 100)) SetWindowTextW(send, L"Try Again");
		form.setProblem(why);
		MessageBeep(MB_ICONWARNING);
	};
	const int r = f.run(win->window());
	if (r != IDOK && !sending) {
		// Kept as a draft for next time.
		d.title = f.fields[titleField].value;
		d.details = f.fields[detailsField].value;
		d.email = f.fields[emailField].value;
		d.contactOK = !f.fields[contactField].value.empty();
		d.includeCircuit = !f.fields[circuitField].value.empty();
		prefs().studentName = trimmed(f.fields[nameField].value);
		d.save();
	}
}

}  // namespace feedback
