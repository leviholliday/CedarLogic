/*****************************************************************************
   Project: CEDAR Logic Simulator
   Feedback: Help > Send Feedback. See Feedback.h.

   In here, top to bottom: talking to the site (curl, one process a request),
   what's sent (the form's draft, the tags it suggests, the facts about this
   computer), the picture of the window, and the window itself.
*****************************************************************************/

#include "Feedback.h"

#include "CedarLogic.h"
#include "EmbeddedRes.h"
#include "GUICanvas.h"
#include "MainApp.h"
#include "MainFrame.h"
#include "ModernToolbar.h"
#include "RenderMode.h"
#include "Settings.h"
#include "UiControls.h"
#include "UiKit.h"
#include "../version.h"

#ifdef __WXMSW__
#include "WinAppearance.h"
#include <wx/msw/registry.h>
#include <wx/msw/wrapwin.h>
#endif
#ifdef __APPLE__
#include "MacAppearance.h"
#include <sys/sysctl.h>
#endif
// The Linux build that links GTK itself (see CMakeLists) draws the window
// straight from GTK, which works under Wayland too; without it, the screen.
#if defined(__WXGTK__) && defined(__has_include)
#if __has_include(<gtk/gtk.h>)
#include <gtk/gtk.h>
#define CL_FEEDBACK_GTK 1
#endif
#endif
#ifndef _WIN32
#include <sys/utsname.h>
#include <unistd.h>
#endif

#include <wx/app.h>
#include <wx/config.h>
#include <wx/dcbuffer.h>
#include <wx/dcmemory.h>
#include <wx/dcscreen.h>
#include <wx/dialog.h>
#include <wx/display.h>
#include <wx/dnd.h>
#include <wx/ffile.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/graphics.h>
#include <wx/mstream.h>
#include <wx/process.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/textctrl.h>
#include <wx/thread.h>
#include <wx/timer.h>
#include <wx/uilocale.h>
#include <wx/utils.h>
#include <wx/wrapsizer.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

DECLARE_APP(MainApp)

namespace {

// ============================================================================
// Talking to the site
// ============================================================================

wxString envOr(const char* name, const char* fallback) {
	wxString v;
	if (wxGetEnv(name, &v) && !v.empty()) return v;
	return wxString::FromUTF8(fallback);
}
wxString serverBase() {
	wxString b = envOr("CL_FEEDBACK_URL", CEDARLOGIC_FEEDBACK_URL);
	while (b.EndsWith("/")) b.RemoveLast();
	return b;
}
wxString serverKey() { return envOr("CL_FEEDBACK_KEY", CEDARLOGIC_FEEDBACK_KEY); }

// Run `fn` after `ms`, once.
void later(int ms, std::function<void()> fn) {
	wxTimer* t = new wxTimer();
	t->Bind(wxEVT_TIMER, [t, fn](wxTimerEvent&) {
		t->Stop();
		fn();
		wxTheApp->CallAfter([t] { delete t; });
	});
	t->StartOnce(ms);
}

// ---- JSON: what's sent is built here; what comes back is small and flat ----

std::string jsonQuote(const std::string& utf8) {
	std::string out = "\"";
	for (unsigned char c : utf8) {
		switch (c) {
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); out += b; }
				else out += (char)c;
		}
	}
	return out + "\"";
}
std::string jsonQuote(const wxString& s) { return jsonQuote(std::string(s.ToUTF8())); }

class JsonObject {
public:
	JsonObject& text(const char* k, const wxString& v) { key(k); body += jsonQuote(v); return *this; }
	JsonObject& number(const char* k, long long v) { key(k); body += std::to_string(v); return *this; }
	JsonObject& flag(const char* k, bool v) { key(k); body += v ? "true" : "false"; return *this; }
	JsonObject& raw(const char* k, const std::string& json) { key(k); body += json; return *this; }
	JsonObject& list(const char* k, const std::vector<wxString>& v) {
		key(k);
		body += "[";
		for (size_t i = 0; i < v.size(); i++) body += (i ? "," : "") + jsonQuote(v[i]);
		body += "]";
		return *this;
	}
	std::string done() const { return "{" + body + "}"; }

private:
	void key(const char* k) {
		if (!body.empty()) body += ",";
		body += jsonQuote(std::string(k)) + ":";
	}
	std::string body;
};

bool jsonFind(const std::string& b, const char* key, size_t& at) {
	const std::string k = "\"" + std::string(key) + "\"";
	size_t p = b.find(k);
	if (p == std::string::npos) return false;
	p += k.size();
	while (p < b.size() && (b[p] == ' ' || b[p] == ':' || b[p] == '\n' || b[p] == '\t')) p++;
	at = p;
	return p < b.size();
}

void appendUtf8(std::string& out, unsigned v) {
	if (v < 0x80) out += (char)v;
	else if (v < 0x800) { out += (char)(0xC0 | (v >> 6)); out += (char)(0x80 | (v & 0x3F)); }
	else {
		out += (char)(0xE0 | (v >> 12));
		out += (char)(0x80 | ((v >> 6) & 0x3F));
		out += (char)(0x80 | (v & 0x3F));
	}
}

wxString jsonText(const std::string& b, const char* key) {
	size_t p;
	if (!jsonFind(b, key, p) || b[p] != '"') return "";
	std::string out;
	for (p++; p < b.size() && b[p] != '"'; p++) {
		if (b[p] != '\\' || p + 1 >= b.size()) { out += b[p]; continue; }
		const char e = b[++p];
		if (e == 'n') out += '\n';
		else if (e == 't') out += '\t';
		else if (e == 'u' && p + 4 < b.size()) {
			appendUtf8(out, (unsigned)strtoul(b.substr(p + 1, 4).c_str(), nullptr, 16));
			p += 4;
		} else out += e;
	}
	return wxString::FromUTF8(out);
}

long long jsonNumber(const std::string& b, const char* key, long long fallback) {
	size_t p;
	if (!jsonFind(b, key, p) || !isdigit((unsigned char)b[p])) return fallback;
	return strtoll(b.c_str() + p, nullptr, 10);
}

// ---- files ----

wxString scratchFolder() {
	const wxString d = wxStandardPaths::Get().GetTempDir() + wxFILE_SEP_PATH + "CedarLogic Feedback";
	if (!wxDirExists(d)) wxFileName::Mkdir(d, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
	return d;
}
wxString scratchFile(const wxString& suffix) {
	static unsigned n = 0;
	wxString p;
	do {
		p = scratchFolder() + wxFILE_SEP_PATH +
		    wxString::Format("%lu-%u-%d%s", (unsigned long)wxGetProcessId(), n++, rand() % 100000, suffix);
	} while (wxFileExists(p));
	return p;
}
bool writeFile(const wxString& path, const void* data, size_t size) {
	wxFFile f(path, "wb");
	return f.IsOpened() && f.Write(data, size) == size && f.Close();
}
bool readFile(const wxString& path, std::string& out) {
	wxFFile f(path, "rb");
	if (!f.IsOpened()) return false;
	const wxFileOffset n = f.Length();
	out.resize(n > 0 ? (size_t)n : 0);
	return n <= 0 || f.Read(&out[0], (size_t)n) == (size_t)n;
}
long long fileSize(const wxString& path) {
	const wxULongLong s = wxFileName::GetSize(path);
	return s == wxInvalidSize ? -1 : (long long)s.GetValue();
}

// A path curl can open. Windows' curl reads a name given with @ in the
// system code page, so an accented user name would lose it; the 8.3 name
// has none of that.
wxString forCurl(const wxString& path) {
#ifdef __WXMSW__
	const wxString s = wxFileName(path).GetShortPath();
	if (!s.empty()) return s;
#endif
	return path;
}

wxString curlProgram() {
#ifdef __WXMSW__
	// Windows 10 (1803) and later have it here -- by full path, so nothing
	// else named curl.exe gets run in its place.
	wxString root;
	if (!wxGetEnv("SystemRoot", &root) || root.empty()) root = "C:\\Windows";
	for (const char* dir : { "System32", "Sysnative" }) {
		const wxString p = root + "\\" + dir + "\\curl.exe";
		if (wxFileExists(p)) return p;
	}
	return "curl.exe";
#else
	for (const char* p : { "/usr/bin/curl", "/bin/curl", "/usr/local/bin/curl", "/opt/homebrew/bin/curl" })
		if (wxFileExists(p)) return p;
	return "curl";
#endif
}

// ---- one request ----

struct Reply {
	int status = 0;            // the HTTP status; 0 when there was no answer
	std::string body;
	wxString problem;          // why there was no answer
};

std::string drain(wxInputStream* s) {
	std::string out;
	char buf[4096];
	while (s && s->CanRead()) {
		s->Read(buf, sizeof buf);
		const size_t n = s->LastRead();
		if (n == 0) break;
		out.append(buf, n);
	}
	return out;
}

class CurlCall : public wxProcess {
public:
	CurlCall(std::function<void(const Reply&)> done, const wxString& bodyPath, const wxString& replyPath)
		: wxProcess(wxPROCESS_REDIRECT), done(std::move(done)), bodyPath(bodyPath), replyPath(replyPath) {}

	void OnTerminate(int, int status) override {
		Reply r;
		const std::string out = drain(GetInputStream());
		const std::string err = drain(GetErrorStream());
		r.status = atoi(out.c_str());
		readFile(replyPath, r.body);
		if (status != 0 || r.status == 0) {
			r.status = 0;
			wxString why = wxString::FromUTF8(err).Trim().Trim(false);
			r.problem = why.empty() ? wxString::Format("curl stopped (%d)", status) : why;
		}
		wxRemoveFile(bodyPath);
		wxRemoveFile(replyPath);
		auto cb = done;
		delete this;
		cb(r);
	}

private:
	std::function<void(const Reply&)> done;
	wxString bodyPath, replyPath;
};

// `bodyPath` (which this removes when done) goes as the body.
void httpCall(const wxString& method, const wxString& path, const std::vector<wxString>& headers,
              const wxString& bodyPath, std::function<void(const Reply&)> done) {
	const wxString replyPath = scratchFile(".reply");
	writeFile(replyPath, "", 0);
	std::vector<wxString> args = {
		curlProgram(), "-sS", "--connect-timeout", "20", "--max-time", "300",
		"-X", method, "-A", wxString("CedarLogic/") + VERSION_NUMBER(),
		"-o", forCurl(replyPath), "-w", "%{http_code}",
		"--data-binary", "@" + forCurl(bodyPath),
	};
	for (const wxString& h : headers) { args.push_back("-H"); args.push_back(h); }
	args.push_back(serverBase() + path);

	std::vector<std::wstring> wide;
	wide.reserve(args.size());
	for (const wxString& a : args) wide.push_back(a.ToStdWstring());
	std::vector<const wchar_t*> argv;
	for (const std::wstring& a : wide) argv.push_back(a.c_str());
	argv.push_back(nullptr);

	CurlCall* call = new CurlCall(done, bodyPath, replyPath);
	if (wxExecute(argv.data(), wxEXEC_ASYNC | wxEXEC_HIDE_CONSOLE, call) == 0) {
		delete call;
		wxRemoveFile(bodyPath);
		wxRemoveFile(replyPath);
		Reply r;
#ifdef __WXMSW__
		r.problem = "This version of Windows has no curl, which sending needs (Windows 10 and later have it).";
#else
		r.problem = "Sending needs curl. Install it (for example: sudo apt install curl) and try again.";
#endif
		done(r);
	}
}

// ---- a whole item: create, pieces, complete ----

struct Attachment {
	enum Kind { Screenshot, Image, Circuit } kind = Screenshot;
	wxString path;      // our own copy, in the scratch folder
	wxString name;      // as the site sees it: screenshot-1.png
	wxString type;      // image/png ...
	long long size = 0;
	wxBitmap thumb;
};

class Sender {
public:
	std::function<void(double, const wxString&)> onProgress;
	std::function<void(bool, const wxString&)> onDone;

	void start(const std::string& itemJson, const std::vector<Attachment>& attachments) {
		json = itemJson;
		files = attachments;
		id.clear(); token.clear();
		file = 0; offset = 0; sent = 0;
		total = 0;
		for (const Attachment& a : files) total += a.size;
		onProgress(0, "Sending...");
		request("POST", "/api/feedback", "application/json",
			[this] {
				const wxString p = scratchFile(".json");
				return writeFile(p, json.data(), json.size()) ? p : wxString();
			},
			[this](const Reply& r) {
				id = jsonText(r.body, "id");
				token = jsonText(r.body, "uploadToken");
				chunk = std::max(64LL * 1024, jsonNumber(r.body, "chunkSize", 3000000));
				if (id.empty()) { onDone(false, "The feedback site gave an answer this version doesn't understand."); return; }
				nextPiece();
			});
	}

private:
	void nextPiece() {
		while (file < files.size() && offset >= files[file].size) { file++; offset = 0; }
		if (file >= files.size()) {
			if (files.empty()) { onDone(true, ""); return; }   // nothing to add: already announced
			onProgress(1, "Finishing...");
			request("POST", "/api/feedback/complete?id=" + id, "application/json",
				[] {
					const wxString p = scratchFile(".json");
					return writeFile(p, "{}", 2) ? p : wxString();
				},
				[this](const Reply&) { onDone(true, ""); });
			return;
		}
		const Attachment& a = files[file];
		const long long pieces = std::max(1LL, (a.size + chunk - 1) / chunk);
		const long long index = offset / chunk;
		const long long length = std::min(chunk, a.size - offset);
		const wxString path = a.path;
		const long long from = offset;
		request("PUT", wxString::Format("/api/feedback/upload?id=%s&file=%s&index=%lld&total=%lld",
		                                id, a.name, index, pieces),
			"application/octet-stream",
			[path, from, length] {
				wxFFile in(path, "rb");
				if (!in.IsOpened() || !in.Seek(from)) return wxString();
				std::string bytes((size_t)length, '\0');
				if (in.Read(&bytes[0], (size_t)length) != (size_t)length) return wxString();
				const wxString p = scratchFile(".piece");
				return writeFile(p, bytes.data(), bytes.size()) ? p : wxString();
			},
			[this, length](const Reply&) {
				offset += length;
				sent += length;
				const Attachment& now = files[file];
				onProgress(total ? (double)sent / total : 1,
					wxString::Format("Sending %s... %.1f of %.1f MB",
						now.kind == Attachment::Circuit ? wxString("the circuit") : now.name,
						sent / 1e6, total / 1e6));
				nextPiece();
			});
	}

	// Tries three times when there was no answer or the site stumbled; a
	// plain "no" (4xx) is final.
	void request(const wxString& method, const wxString& path, const wxString& type,
	             std::function<wxString()> makeBody, std::function<void(const Reply&)> ok, int attempt = 1) {
		const wxString body = makeBody();
		if (body.empty()) { onDone(false, "Couldn't get what's being sent ready (is the disk full?)."); return; }
		std::vector<wxString> headers = { "content-type: " + type, "x-cedarlogic-key: " + serverKey() };
		if (!token.empty()) headers.push_back("x-upload-token: " + token);
		httpCall(method, path, headers, body,
			[=](const Reply& r) {
				if (r.status >= 200 && r.status < 300) { ok(r); return; }
				if ((r.status == 0 || r.status >= 500) && attempt < 3) {
					later(1500 * attempt, [=] { request(method, path, type, makeBody, ok, attempt + 1); });
					return;
				}
				if (r.status == 0)
					onDone(false, "Couldn't reach the feedback site. Check the internet connection and try again."
					              "\n(" + r.problem + ")");
				else {
					const wxString said = jsonText(r.body, "error");
					onDone(false, said.empty() ? wxString::Format("The feedback site said no (%d).", r.status) : said);
				}
			});
	}

	std::string json;
	std::vector<Attachment> files;
	wxString id, token;
	long long chunk = 3000000;
	size_t file = 0;
	long long offset = 0, sent = 0, total = 0;
};

// ============================================================================
// What's sent
// ============================================================================

const char* const TAGS[] = { "Bug", "Idea", "Design", "Toolbar", "Canvas & wiring", "Simulation",
                             "Files & saving", "Performance", "Other" };
const int TAG_COUNT = sizeof(TAGS) / sizeof(TAGS[0]);

struct PriorityInfo { const char* id; const char* name; const char* hint; unsigned char r, g, b; };
const PriorityInfo PRIORITIES[4] = {
	{ "low", "Low", "Small thing, whenever", 140, 144, 150 },
	{ "normal", "Normal", "Worth fixing", 59, 133, 242 },
	{ "high", "High", "Gets in my way", 242, 140, 31 },
	{ "blocking", "Blocking", "I can't do my work", 235, 69, 61 },
};

// Words that suggest a tag, matched at the start of a word.
struct Clue { const char* tag; std::vector<const char*> words; };
const std::vector<Clue>& clues() {
	static const std::vector<Clue> c = {
		{ "Bug", { "bug", "crash", "broke", "doesn't", "does not", "didn't", "isn't", "not working", "error", "wrong",
		           "glitch", "stuck", "freez", "fail", "won't", "can't", "cannot", "missing", "disappear", "weird",
		           "jump", "flicker", "should" } },
		{ "Idea", { "idea", "feature", "would be nice", "would love", "could you", "can you add", "please add", "wish",
		            "suggest", "it'd be", "it would be", "maybe add", "what if" } },
		{ "Design", { "look", "color", "colour", "font", "icon", "ugly", "design", "theme", "dark mode", "light mode",
		              "layout", "spacing", "align", "animation", "pretty" } },
		{ "Toolbar", { "toolbar", "button", "top bar", "title bar", "tooltip" } },
		{ "Canvas & wiring", { "wire", "wiring", "gate", "canvas", "connect", "pin", "drag", "zoom", "select", "paste",
		                       "copy", "rotate", "grid", "palette" } },
		{ "Simulation", { "simulat", "clock", "oscilloscope", "scope", "truth table", "timing", "signal", "step" } },
		{ "Files & saving", { "save", "saving", "open", "file", "export", "import", "version history", "library",
		                      "your circuits", "autosave", ".cdl", "print" } },
		{ "Performance", { "slow", "lag", "battery", "cpu", "hang", "stutter", "sluggish", "performance", "memory",
		                   "fan" } },
	};
	return c;
}
bool hasWord(const wxString& text, const wxString& word) {
	size_t from = 0;
	while (true) {
		const size_t at = text.find(word, from);
		if (at == wxString::npos) return false;
		if (at == 0 || !wxIsalpha((wxChar)text[at - 1])) return true;
		from = at + 1;
	}
}

enum class Phase { Writing, Sending, Sent, Failed };

struct Model {
	wxString title, details, email;
	std::set<wxString> chosen, declined;
	int priority = 1;
	bool contactOK = true;
	bool includeCircuit = false;
	std::vector<Attachment> attachments;
	Phase phase = Phase::Writing;
	double progress = 0;
	wxString note;                  // what sending is doing, or why it didn't
	wxString sentTo;                // the address a reply would go to, for the thank-you
	Sender sender;
	std::function<void()> changed;  // the window, while it's open

	static const size_t MAX_IMAGES = 3;

	void load() {
		wxConfigBase* c = wxConfigBase::Get();
		if (!c) return;
		c->Read("Feedback/Title", &title);
		c->Read("Feedback/Details", &details);
		c->Read("Feedback/Email", &email);
		c->Read("Feedback/Priority", &priority, 1);
		priority = std::max(0, std::min(3, priority));
		c->Read("Feedback/Contact", &contactOK, true);
		wxString s;
		c->Read("Feedback/Tags", &s);
		for (const wxString& t : wxSplit(s, '|')) if (!t.empty()) chosen.insert(t);
		c->Read("Feedback/Declined", &s);
		for (const wxString& t : wxSplit(s, '|')) if (!t.empty()) declined.insert(t);
	}
	void save() const {
		wxConfigBase* c = wxConfigBase::Get();
		if (!c) return;
		c->Write("Feedback/Title", title);
		c->Write("Feedback/Details", details);
		c->Write("Feedback/Email", email);
		c->Write("Feedback/Priority", priority);
		c->Write("Feedback/Contact", contactOK);
		wxArrayString a(false);
		for (const wxString& t : chosen) a.Add(t);
		c->Write("Feedback/Tags", wxJoin(a, '|'));
		a.Clear();
		for (const wxString& t : declined) a.Add(t);
		c->Write("Feedback/Declined", wxJoin(a, '|'));
	}
	void notify() { if (changed) changed(); }

	wxString name() const { return wxString::FromUTF8(appConfig().appSettings.studentName.c_str()); }
	void setName(const wxString& n) {
		appConfig().appSettings.studentName = std::string(n.Strip(wxString::both).ToUTF8());
		if (wxConfigBase* c = wxConfigBase::Get()) c->Write("StudentName", n.Strip(wxString::both));
	}

	// What the words suggest (and Simulation, while Simulation View is on).
	std::vector<wxString> suggested() const {
		wxString text = (title + " " + details).Lower();
		text.Replace(wxString::FromUTF8("\u2019"), "'");
		std::set<wxString> hit;
		for (const Clue& c : clues())
			for (const char* w : c.words)
				if (hasWord(text, w)) { hit.insert(c.tag); break; }
		if (renderMode().simView) hit.insert("Simulation");
		std::vector<wxString> out;
		for (const char* t : TAGS) if (hit.count(t)) out.push_back(t);
		return out;
	}
	bool isSuggested(const wxString& t) const {
		const auto s = suggested();
		return std::find(s.begin(), s.end(), t) != s.end();
	}
	// The tags it goes with: the ones picked, and the suggestions kept.
	std::vector<wxString> tags() const {
		const auto s = suggested();
		std::vector<wxString> out;
		for (const char* t : TAGS) {
			const bool sug = std::find(s.begin(), s.end(), wxString(t)) != s.end();
			if (chosen.count(t) || (sug && !declined.count(t))) out.push_back(t);
		}
		return out;
	}
	bool isOn(const wxString& t) const {
		const auto on = tags();
		return std::find(on.begin(), on.end(), t) != on.end();
	}
	void toggle(const wxString& t) {
		if (isSuggested(t)) {
			if (isOn(t)) { declined.insert(t); chosen.erase(t); }
			else declined.erase(t);
		} else if (chosen.count(t)) chosen.erase(t);
		else chosen.insert(t);
		save();
		notify();
	}

	size_t images() const {
		size_t n = 0;
		for (const Attachment& a : attachments) if (a.kind != Attachment::Circuit) n++;
		return n;
	}
	wxString freeName(const wxString& base, const wxString& ext) const {
		for (int n = 1;; n++) {
			const wxString name = wxString::Format("%s-%d.%s", base, n, ext);
			bool used = false;
			for (const Attachment& a : attachments) used |= a.name == name;
			if (!used) return name;
		}
	}
	void addImage(Attachment::Kind kind, const wxString& path, const wxString& name, const wxString& type,
	              const wxImage& picture) {
		Attachment a;
		a.kind = kind;
		a.path = path;
		a.name = name;
		a.type = type;
		a.size = fileSize(path);
		if (picture.IsOk()) a.thumb = wxBitmap(picture);
		attachments.push_back(a);
		notify();
	}
	void remove(size_t i) {
		if (i >= attachments.size()) return;
		wxRemoveFile(attachments[i].path);
		attachments.erase(attachments.begin() + i);
		notify();
	}

	bool canSend() const { return phase != Phase::Sending && !title.Strip(wxString::both).empty(); }
	void send(MainFrame* frame);
	void finished(bool ok, const wxString& why) {
		if (!ok) {
			phase = Phase::Failed;
			note = why;
			notify();
			return;
		}
		for (const Attachment& a : attachments) wxRemoveFile(a.path);
		attachments.clear();
		title.clear();
		details.clear();
		chosen.clear();
		declined.clear();
		priority = 1;
		includeCircuit = false;
		save();
		phase = Phase::Sent;
		note.clear();
		notify();
	}
};

Model& model() {
	static Model m;
	static bool loaded = false;
	if (!loaded) { loaded = true; m.load(); }
	return m;
}

// ---- about this computer ----

// One fact sent with the feedback, and how the form shows it.
struct Fact { const char* key; std::string json; wxString label, shown; };
struct Facts {
	std::vector<Fact> app, system, context;
	static void text(std::vector<Fact>& to, const char* k, const wxString& label, const wxString& v) {
		if (!v.empty()) to.push_back({ k, jsonQuote(v), label, v });
	}
	static void number(std::vector<Fact>& to, const char* k, const wxString& label, long long v) {
		to.push_back({ k, std::to_string(v), label, wxString::Format("%lld", v) });
	}
	static void flag(std::vector<Fact>& to, const char* k, const wxString& label, bool v) {
		to.push_back({ k, v ? "true" : "false", label, v ? "Yes" : "No" });
	}
	static std::string object(const std::vector<Fact>& f) {
		JsonObject o;
		for (const Fact& x : f) o.raw(x.key, x.json);
		return o.done();
	}
};

wxString firstLine(const wxString& path) {
	std::string s;
	if (!readFile(path, s)) return "";
	wxString t = wxString::FromUTF8(s.c_str());   // stops at a NUL, as /proc/device-tree has
	return t.BeforeFirst('\n').Strip(wxString::both);
}

#ifdef __APPLE__
wxString sysctlText(const char* name) {
	size_t size = 0;
	if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size == 0) return "";
	std::string buf(size, '\0');
	if (sysctlbyname(name, &buf[0], &size, nullptr, 0) != 0) return "";
	return wxString::FromUTF8(buf.c_str());
}
#endif

#ifdef __WXMSW__
wxString registryText(const wxString& key, const wxString& value) {
	wxRegKey k(wxRegKey::HKLM, key);
	wxString v;
	if (k.Exists() && k.HasValue(value)) k.QueryValue(value, v);
	return v.Strip(wxString::both);
}
#endif

// "Windows 11", "Ubuntu 24.04 LTS", "macOS 15.1": the short name.
wxString systemName() {
#if defined(__WXMSW__) || defined(__APPLE__)
	return wxGetOsDescription().BeforeFirst('(').Strip(wxString::both);
#else
	std::string s;
	if (readFile("/etc/os-release", s))
		for (const wxString& line : wxSplit(wxString::FromUTF8(s.c_str()), '\n'))
			if (line.StartsWith("PRETTY_NAME=")) {
				wxString v = line.AfterFirst('=');
				v.Replace("\"", "");
				if (!v.empty()) return v;
			}
	return "Linux";
#endif
}

wxString computerModel() {
#ifdef __WXMSW__
	const wxString bios = "HARDWARE\\DESCRIPTION\\System\\BIOS";
	wxString maker = registryText(bios, "SystemManufacturer"), product = registryText(bios, "SystemProductName");
	if (maker.Lower().Contains("to be filled") || maker.Lower().Contains("system manufacturer")) maker.clear();
	if (product.Lower().Contains("to be filled") || product.Lower().Contains("system product")) product.clear();
	return (maker.empty() || product.StartsWith(maker) ? product : maker + " " + product).Strip(wxString::both);
#elif defined(__APPLE__)
	return sysctlText("hw.model");
#else
	wxString pi = firstLine("/proc/device-tree/model");
	if (!pi.empty()) return pi;
	const wxString maker = firstLine("/sys/devices/virtual/dmi/id/sys_vendor");
	const wxString product = firstLine("/sys/devices/virtual/dmi/id/product_name");
	return (maker.empty() || product.StartsWith(maker) ? product : maker + " " + product).Strip(wxString::both);
#endif
}

wxString processorName() {
#ifdef __WXMSW__
	return registryText("HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "ProcessorNameString");
#elif defined(__APPLE__)
	return sysctlText("machdep.cpu.brand_string");
#else
	std::string s;
	if (readFile("/proc/cpuinfo", s))
		for (const wxString& line : wxSplit(wxString::FromUTF8(s.c_str()), '\n'))
			if (line.StartsWith("model name") || line.StartsWith("Model"))
				return line.AfterFirst(':').Strip(wxString::both);
	return "";
#endif
}

long long memoryGB() {
#ifdef __WXMSW__
	MEMORYSTATUSEX m;
	m.dwLength = sizeof m;
	if (!GlobalMemoryStatusEx(&m)) return 0;
	return (long long)std::llround(m.ullTotalPhys / 1073741824.0);
#elif defined(__APPLE__)
	unsigned long long bytes = 0;
	size_t size = sizeof bytes;
	if (sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) != 0) return 0;
	return (long long)std::llround(bytes / 1073741824.0);
#else
	const long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGESIZE);
	if (pages <= 0 || page <= 0) return 0;
	return (long long)std::llround((double)pages * page / 1073741824.0);
#endif
}

Facts gatherFacts(MainFrame* frame) {
	Facts f;
	const auto& s = appConfig().appSettings;
	Facts::text(f.app, "name", "App", "CedarLogic");
	Facts::text(f.app, "version", "Version", VERSION_NUMBER());
	Facts::text(f.app, "build", "Build", VERSION_NUMBER_STRING());
	Facts::text(f.app, "channel", "Testing group", s.updateChannel == 1 ? "Beta tester" : "Normal tester");
#ifdef __WXMSW__
	Facts::text(f.app, "bits", "Build type", wxString(sizeof(void*) == 4 ? "32-bit" : "64-bit") +
	            (wxIsPlatform64Bit() ? " on 64-bit Windows" : " on 32-bit Windows"));
#endif
	wxString appImage;
	if (wxGetEnv("APPIMAGE", &appImage)) Facts::text(f.app, "package", "Package", "AppImage");

	Facts::text(f.system, "os", "System", wxGetOsDescription());
#if !defined(__WXMSW__) && !defined(__APPLE__)
	Facts::text(f.system, "distribution", "Distribution", systemName());
	wxString v;
	if (wxGetEnv("XDG_CURRENT_DESKTOP", &v)) Facts::text(f.system, "desktop", "Desktop", v);
	if (wxGetEnv("XDG_SESSION_TYPE", &v)) Facts::text(f.system, "session", "Session", v);
#endif
	Facts::text(f.system, "model", "Computer", computerModel());
	Facts::text(f.system, "cpu", "Processor", processorName());
	Facts::text(f.system, "arch", "Architecture", wxGetCpuArchitectureName());
	if (const long long gb = memoryGB()) Facts::number(f.system, "memoryGB", "Memory (GB)", gb);
	Facts::number(f.system, "cpus", "Processor cores", wxThread::GetCPUCount());
	wxString displays;
	for (unsigned i = 0; i < wxDisplay::GetCount(); i++) {
		wxDisplay d(i);
		const wxRect g = d.GetGeometry();
		displays += wxString::Format("%s%dx%d @%gx", i ? ", " : "", g.width, g.height, d.GetScaleFactor());
	}
	Facts::text(f.system, "displays", "Displays", displays);
	Facts::text(f.system, "locale", "Language", wxUILocale::GetCurrent().GetName());

	Facts::text(f.context, "toolbar", "Toolbar", cl::tb::styleName(s.toolbarStyle));
	Facts::flag(f.context, "dark", "Dark mode", renderMode().darkMode);
	if (frame) {
		long long gates = 0;
		for (GUICanvas* c : frame->Canvases()) if (c) gates += (long long)c->getGateList()->size();
		Facts::number(f.context, "pages", "Pages", (long long)frame->Canvases().size());
		Facts::number(f.context, "gates", "Gates", gates);
		Facts::flag(f.context, "simulating", "Simulating", !frame->IsSimPaused());
		Facts::flag(f.context, "simulationView", "Simulation View", frame->IsSimView());
		Facts::flag(f.context, "splitView", "Split view", frame->IsSplit());
		Facts::flag(f.context, "locked", "Locked", frame->IsLockToolOn());
		Facts::text(f.context, "zoom", "Zoom", wxString::Format("%d%%", frame->GetZoomPercent()));
	}
	return f;
}

const char* platformId() {
#ifdef __WXMSW__
	return "windows";
#elif defined(__APPLE__)
	return "macos";
#else
	return "linux";
#endif
}

// "version 4.0.1, Windows 11, Dell XPS 13 9310": the form's footer.
wxString deviceLine() {
	wxString model = computerModel();
	if (model.empty()) model = wxGetCpuArchitectureName();
	return wxString::Format("version %s, %s, %s", VERSION_NUMBER(), systemName(), model);
}

void Model::send(MainFrame* frame) {
	if (!canSend()) return;
	std::vector<Attachment> files = attachments;
	if (includeCircuit && frame) {
		const wxString path = scratchFile("-circuit.cdl");
		if (frame->save(path.ToStdString(), 3)) {
			Attachment a;
			a.kind = Attachment::Circuit;
			a.path = path;
			a.name = "circuit.cdl";
			a.type = "text/plain";
			a.size = fileSize(path);
			if (a.size > 0) files.push_back(a);
		}
	}
	const Facts facts = gatherFacts(frame);
	const wxString mail = email.Strip(wxString::both);
	std::string list = "[";
	for (size_t i = 0; i < files.size(); i++) {
		JsonObject a;
		a.text("name", files[i].name).text("type", files[i].type).number("size", files[i].size);
		list += (i ? "," : "") + a.done();
	}
	list += "]";
	JsonObject o;
	o.text("title", title.Strip(wxString::both))
	 .text("details", details.Strip(wxString::both))
	 .list("tags", tags())
	 .list("autoTags", suggested())
	 .text("priority", PRIORITIES[priority].id)
	 .text("name", name())
	 .text("email", mail)
	 .flag("contactOK", contactOK && !mail.empty())
	 .text("platform", platformId())
	 .raw("attachments", list)
	 .raw("app", Facts::object(facts.app))
	 .raw("system", Facts::object(facts.system))
	 .raw("context", Facts::object(facts.context));
	sentTo = contactOK ? mail : wxString();
	phase = Phase::Sending;
	progress = 0;
	note = "Sending...";
	notify();

	sender.onProgress = [this](double p, const wxString& n) { progress = p; note = n; notify(); };
	sender.onDone = [this, files](bool ok, const wxString& why) {
		for (const Attachment& a : files) if (a.kind == Attachment::Circuit) wxRemoveFile(a.path);
		finished(ok, why);
	};
	sender.start(o.done(), files);
}

// ============================================================================
// The picture of the window
// ============================================================================

void collectCanvases(wxWindow* w, std::vector<GUICanvas*>& out) {
	for (wxWindow* child : w->GetChildren()) {
		if (GUICanvas* c = dynamic_cast<GUICanvas*>(child)) {
			if (c->IsShownOnScreen()) out.push_back(c);
			continue;
		}
		collectCanvases(child, out);
	}
}

// Paint each visible canvas into `base` from the circuit itself: a picture of
// a window rarely includes what OpenGL drew in it.
void paintCanvases(MainFrame* frame, wxImage& base, std::function<wxRect(GUICanvas*)> where) {
	std::vector<GUICanvas*> canvases;
	collectCanvases(frame, canvases);
	for (GUICanvas* c : canvases) {
		const wxRect r = where(c).Intersect(wxRect(0, 0, base.GetWidth(), base.GetHeight()));
		if (r.width < 8 || r.height < 8) continue;
		const wxImage shot = c->renderThumbnail(r.width, r.height, renderMode().darkMode);
		if (shot.IsOk()) base.Paste(shot, r.x, r.y);
	}
}

}  // namespace

static wxDialog* g_feedback = nullptr;   // the window, while it's open

// A picture of `win`. With `frame`, that window's canvases are painted in too
// (`win` is then the frame).
static bool grabWindow(wxTopLevelWindow* win, wxImage& out, MainFrame* frame) {
	if (!win || !win->IsShown()) return false;
#ifdef __WXMSW__
	wxBitmap bmp;
	int method = -1;
	if (!WinGrabWindow(win, bmp, &method)) return false;
	out = bmp.ConvertToImage();
	if (frame && method != 0) {
		// What was composed on screen wasn't to be had: the canvas is likely blank.
		const wxPoint origin = win->GetScreenPosition();
		paintCanvases(frame, out, [&](GUICanvas* c) {
			return wxRect(c->GetScreenPosition() - origin, c->GetClientSize());
		});
	}
	return true;
#elif defined(__APPLE__)
	std::vector<unsigned char> rgb;
	int w = 0, h = 0;
	if (!MacGrabWindow(win->GetHandle(), rgb, w, h)) return false;
	out.Create(w, h, false);
	std::copy(rgb.begin(), rgb.end(), out.GetData());
	if (frame) {
		const double scale = (double)w / std::max(1, win->GetSize().x);
		const wxPoint origin = win->GetScreenPosition();
		paintCanvases(frame, out, [&](GUICanvas* c) {
			const wxPoint at = c->GetScreenPosition() - origin;
			const wxSize sz = c->GetClientSize();
			return wxRect((int)std::lround(at.x * scale), (int)std::lround(at.y * scale),
			              (int)std::lround(sz.x * scale), (int)std::lround(sz.y * scale));
		});
	}
	return true;
#elif defined(CL_FEEDBACK_GTK)
	GtkWidget* top = static_cast<GtkWidget*>(win->GetHandle());
	if (!top || !gtk_widget_get_realized(top)) return false;
	const int w = gtk_widget_get_allocated_width(top), h = gtk_widget_get_allocated_height(top);
	const int sf = std::max(1, gtk_widget_get_scale_factor(top));
	if (w <= 0 || h <= 0) return false;
	cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w * sf, h * sf);
	cairo_surface_set_device_scale(surface, sf, sf);
	cairo_t* cr = cairo_create(surface);
	gtk_widget_draw(top, cr);
	cairo_destroy(cr);
	cairo_surface_flush(surface);
	const int W = w * sf, H = h * sf, stride = cairo_image_surface_get_stride(surface);
	const unsigned char* px = cairo_image_surface_get_data(surface);
	out.Create(W, H, false);
	unsigned char* to = out.GetData();
	const wxColour under = ui::paper();
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			const uint32_t p = *reinterpret_cast<const uint32_t*>(px + y * stride + 4 * x);
			const unsigned a = p >> 24;   // premultiplied: add what shows through
			to[0] = (unsigned char)std::min(255u, ((p >> 16) & 255) + under.Red() * (255 - a) / 255);
			to[1] = (unsigned char)std::min(255u, ((p >> 8) & 255) + under.Green() * (255 - a) / 255);
			to[2] = (unsigned char)std::min(255u, (p & 255) + under.Blue() * (255 - a) / 255);
			to += 3;
		}
	cairo_surface_destroy(surface);
	if (frame)
		paintCanvases(frame, out, [&](GUICanvas* c) {
			int x = 0, y = 0;
			gtk_widget_translate_coordinates(static_cast<GtkWidget*>(c->GetHandle()), top, 0, 0, &x, &y);
			const wxSize sz = c->GetClientSize();
			return wxRect(x * sf, y * sf, sz.x * sf, sz.y * sf);
		});
	return true;
#else
	// The screen, under the window: step the form aside while the app's
	// window is taken.
	const bool hide = frame && g_feedback && g_feedback->IsShown();
	if (hide) { g_feedback->Hide(); wxYield(); wxMilliSleep(150); }
	const wxRect r = win->GetScreenRect();
	wxBitmap bmp(r.width, r.height, 24);
	{
		wxScreenDC screen;
		wxMemoryDC dc(bmp);
		dc.Blit(0, 0, r.width, r.height, &screen, r.x, r.y);
	}
	if (hide) g_feedback->Show();
	out = bmp.ConvertToImage();
	if (frame) {
		const wxPoint origin = r.GetTopLeft();
		paintCanvases(frame, out, [&](GUICanvas* c) {
			return wxRect(c->GetScreenPosition() - origin, c->GetClientSize());
		});
	}
	return true;
#endif
}

bool CaptureAppWindow(MainFrame* frame, wxImage& out) {
	return grabWindow(frame, out, frame);
}

namespace {

// ============================================================================
// The window
// ============================================================================

// The app's brand: the icon's near-black green and its neon.
const wxColour BAND_INK(9, 16, 12), BAND_DEEP(15, 31, 20), NEON(56, 255, 107), NEON_DEEP(13, 199, 66);

// Type sizes are given as on a Mac (points of 1/72"); Windows and GTK count
// 1/96", so the same number would come out a third bigger there.
double pt(double mac) {
#ifdef __WXOSX__
	return mac;
#else
	return mac * 0.76;
#endif
}
wxFont font(double macPoints, bool bold = false) {
	return wxFont(wxFontInfo(pt(macPoints)).Bold(bold));
}

// How many bitmap pixels to a unit a graphics context draws in: Windows
// draws in physical pixels already; macOS and GTK in points.
double backing(const wxWindow* w) {
#ifdef __WXMSW__
	(void)w;
	return 1.0;
#else
	return w->GetContentScaleFactor();
#endif
}

wxColour lineColour() { return ui::withAlpha(ui::ink(), ui::isDark() ? 0.13 : 0.11); }
wxColour fieldColour() { return ui::isDark() ? wxColour(36, 40, 47) : *wxWHITE; }

// A control we draw: hover, press, keyboard and a click handler.
class Drawn : public wxControl {
public:
	Drawn(wxWindow* parent, const wxSize& dip)
		: wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxWANTS_CHARS) {
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		SetCursor(wxCursor(wxCURSOR_HAND));
		SetInitialSize(FromDIP(dip));
		Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
			wxAutoBufferedPaintDC dc(this);
			dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
			dc.Clear();
			std::unique_ptr<wxGraphicsContext> gc(ui::graphics(dc));
			if (gc) draw(gc.get(), GetClientSize());
		});
		Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) { hot = true; Refresh(); });
		Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { hot = false; down = false; Refresh(); });
		Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) { down = true; at = e.GetPosition(); Refresh(); });
		Bind(wxEVT_LEFT_DCLICK, [this](wxMouseEvent& e) { down = true; at = e.GetPosition(); Refresh(); });
		Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& e) {
			const bool was = down;
			down = false;
			Refresh();
			if (was && GetClientRect().Contains(e.GetPosition()) && IsEnabled()) clicked(e.GetPosition());
		});
		Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& e) {
			const int k = e.GetKeyCode();
			if ((k == WXK_SPACE || k == WXK_RETURN || k == WXK_NUMPAD_ENTER) && IsEnabled()) {
				clicked(wxPoint(-1, -1));
				return;
			}
			if (k == WXK_TAB) {
				Navigate(e.ShiftDown() ? wxNavigationKeyEvent::IsBackward : wxNavigationKeyEvent::IsForward);
				return;
			}
			e.Skip();
		});
		Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); });
		Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); });
	}
	bool AcceptsFocus() const override { return IsEnabled(); }
	bool AcceptsFocusFromKeyboard() const override { return IsEnabled(); }
	bool HasTransparentBackground() override { return true; }
	bool Enable(bool on = true) override {
		const bool r = wxControl::Enable(on);
		Refresh();
		return r;
	}

	std::function<void()> onClick;

protected:
	virtual void draw(wxGraphicsContext* gc, const wxSize& sz) = 0;
	virtual void clicked(const wxPoint&) { if (onClick) onClick(); }
	double dip(double v) const { return v * FromDIP(1000) / 1000.0; }
	void focusRing(wxGraphicsContext* gc, double x, double y, double w, double h, double r) {
		if (!HasFocus()) return;
		gc->SetBrush(*wxTRANSPARENT_BRUSH);
		gc->SetPen(wxPen(ui::withAlpha(ui::accent(), 0.8), (int)std::max(1.0, dip(2))));
		gc->DrawRoundedRectangle(x, y, w, h, r);
	}
	void centredText(wxGraphicsContext* gc, const wxString& s, double x, double y, double w, double h) {
		double tw, th;
		gc->GetTextExtent(s, &tw, &th);
		gc->DrawText(s, x + (w - tw) / 2, y + (h - th) / 2);
	}

	bool hot = false, down = false;
	wxPoint at;
};

// A width that fits `label` in `f`, plus padding, in DIPs.
int fitWidth(wxWindow* parent, const wxString& label, const wxFont& f, int pad) {
	int w = 0, h = 0;
	parent->GetTextExtent(label, &w, &h, nullptr, nullptr, &f);
	return parent->ToDIP(w) + pad;
}

// The two kinds of button: filled (Send) and quiet (Close).
class Button : public Drawn {
public:
	Button(wxWindow* parent, const wxString& label, bool primary)
		: Drawn(parent, wxSize(fitWidth(parent, label, font(13, true), 36), 34)), label(label), primary(primary) {}
	void SetLabelText(const wxString& s) {
		label = s;
		SetInitialSize(FromDIP(wxSize(fitWidth(GetParent(), s, font(13, true), 36), 34)));
		Refresh();
	}

private:
	void draw(wxGraphicsContext* gc, const wxSize& sz) override {
		const double r = dip(9);
		const bool on = IsEnabled();
		const wxColour ac = ui::accent();
		if (primary) {
			gc->SetPen(*wxTRANSPARENT_PEN);
			gc->SetBrush(wxBrush(ui::withAlpha(ac, !on ? 0.35 : down ? 0.8 : hot ? 1.0 : 0.92)));
		} else {
			gc->SetPen(wxPen(lineColour(), 1));
			gc->SetBrush(wxBrush(ui::withAlpha(ui::ink(), down ? 0.12 : hot ? 0.08 : 0.04)));
		}
		gc->DrawRoundedRectangle(0.5, 0.5, sz.x - 1, sz.y - 1, r);
		gc->SetFont(font(13, true), primary ? wxColour(255, 255, 255, on ? 255 : 170) : ui::ink());
		centredText(gc, label, 0, 0, sz.x, sz.y);
		focusRing(gc, 1, 1, sz.x - 2, sz.y - 2, r);
	}
	wxString label;
	bool primary;
};

// A tag: on, off, and whether it was suggested (a ✦) or a suggestion turned
// down (dashed).
class Chip : public Drawn {
public:
	Chip(wxWindow* parent, const wxString& tag)
		: Drawn(parent, wxSize(fitWidth(parent, tag, font(12), 26) + 12, 28)), tag(tag) {}
	void setState(bool isOn, bool isSuggested) {
		if (isOn == on && isSuggested == suggested) return;
		on = isOn;
		suggested = isSuggested;
		Refresh();
	}
	const wxString tag;

private:
	void draw(wxGraphicsContext* gc, const wxSize& sz) override {
		const wxColour ac = ui::accent(), in = ui::ink();
		const double h = sz.y - 1;
		const bool declined = suggested && !on;
		gc->SetBrush(wxBrush(on ? ui::withAlpha(ac, hot ? 0.28 : 0.2) : ui::withAlpha(in, hot ? 0.08 : 0.035)));
		wxGraphicsPenInfo pen(on ? ui::withAlpha(ac, 0.75) : ui::withAlpha(in, declined ? 0.3 : 0.14));
		if (declined) pen.Style(wxPENSTYLE_SHORT_DASH);
		gc->SetPen(gc->CreatePen(pen));
		gc->DrawRoundedRectangle(0.5, 0.5, sz.x - 1, h, h / 2);
		double x = dip(13);
		if (suggested) {
			gc->SetFont(font(10), declined ? ui::dim() : ac);
			double tw, th;
			const wxString star = wxString::FromUTF8("\u2726");
			gc->GetTextExtent(star, &tw, &th);
			gc->DrawText(star, x - dip(3), (sz.y - th) / 2);
			x += tw + dip(1);
		} else {
			x += dip(6);
		}
		gc->SetFont(font(12, on), declined ? ui::dim() : in);
		double tw, th;
		gc->GetTextExtent(tag, &tw, &th);
		gc->DrawText(tag, x, (sz.y - th) / 2);
		focusRing(gc, 1, 1, sz.x - 2, sz.y - 2, h / 2);
	}
	bool on = false, suggested = false;
};

// How much it matters: a dot in the level's colour and its name.
class PriorityPill : public Drawn {
public:
	PriorityPill(wxWindow* parent, int level) : Drawn(parent, wxSize(120, 32)), level(level) {}
	void setOn(bool v) { if (v != on) { on = v; Refresh(); } }

private:
	void draw(wxGraphicsContext* gc, const wxSize& sz) override {
		const PriorityInfo& p = PRIORITIES[level];
		const wxColour c(p.r, p.g, p.b), in = ui::ink();
		const double r = dip(9);
		gc->SetBrush(wxBrush(on ? ui::withAlpha(c, 0.16) : ui::withAlpha(in, hot ? 0.08 : 0.035)));
		gc->SetPen(wxPen(on ? ui::withAlpha(c, 0.9) : ui::withAlpha(in, 0.13), on ? (int)std::max(1.0, dip(1.5)) : 1));
		gc->DrawRoundedRectangle(0.5, 0.5, sz.x - 1, sz.y - 1, r);
		gc->SetFont(font(12.5, on), in);
		double tw, th;
		gc->GetTextExtent(p.name, &tw, &th);
		const double dot = dip(8), gap = dip(7);
		const double x = (sz.x - (dot + gap + tw)) / 2;
		gc->SetPen(*wxTRANSPARENT_PEN);
		gc->SetBrush(wxBrush(c));
		gc->DrawEllipse(x, (sz.y - dot) / 2, dot, dot);
		gc->DrawText(p.name, x + dot + gap, (sz.y - th) / 2);
		focusRing(gc, 1, 1, sz.x - 2, sz.y - 2, r);
	}
	int level;
	bool on = false;
};

// A big square button with an icon: Screenshot, Add image.
class Tile : public Drawn {
public:
	Tile(wxWindow* parent, const char* icon, const wxString& label)
		: Drawn(parent, wxSize(132, 84)), icon(icon), label(label) {}

private:
	void draw(wxGraphicsContext* gc, const wxSize& sz) override {
		const wxColour in = ui::ink();
		const bool on = IsEnabled();
		const double r = dip(12);
		gc->SetBrush(wxBrush(ui::withAlpha(in, !on ? 0.02 : down ? 0.1 : hot ? 0.07 : 0.035)));
		gc->SetPen(wxPen(ui::withAlpha(in, 0.13), 1));
		gc->DrawRoundedRectangle(0.5, 0.5, sz.x - 1, sz.y - 1, r);
		const double d = dip(24);
		const wxBitmap b = cl::tb::ToolIcon(icon, on ? ui::accent() : ui::dim(), (int)std::lround(d), backing(this));
		if (b.IsOk()) gc->DrawBitmap(b, (sz.x - d) / 2, sz.y * 0.38 - d / 2, d, d);
		gc->SetFont(font(12, false), on ? in : ui::dim());
		double tw, th;
		gc->GetTextExtent(label, &tw, &th);
		gc->DrawText(label, (sz.x - tw) / 2, sz.y * 0.72 - th / 2);
		focusRing(gc, 1, 1, sz.x - 2, sz.y - 2, r);
	}
	const char* icon;
	wxString label;
};

// An attachment, with an x to take it off.
class Thumb : public Drawn {
public:
	Thumb(wxWindow* parent, const Attachment& a) : Drawn(parent, wxSize(132, 84)), a(a) {
		SetToolTip(a.name + wxString::Format(" (%.1f MB)", a.size / 1e6));
	}
	std::function<void()> onRemove;

private:
	wxRect cross() const { return wxRect(GetClientSize().x - FromDIP(26), FromDIP(4), FromDIP(22), FromDIP(22)); }
	void clicked(const wxPoint& p) override {
		if ((p.x < 0 || cross().Contains(p)) && onRemove) onRemove();
	}
	void draw(wxGraphicsContext* gc, const wxSize& sz) override {
		const double r = dip(12);
		wxGraphicsPath clip = gc->CreatePath();
		clip.AddRoundedRectangle(0, 0, sz.x, sz.y, r);
		gc->PushState();
		gc->Clip(wxRegion(0, 0, sz.x, sz.y));
		gc->SetPen(*wxTRANSPARENT_PEN);
		gc->SetBrush(wxBrush(ui::withAlpha(ui::ink(), 0.06)));
		gc->FillPath(clip);
		if (a.thumb.IsOk()) {
			// Cover the tile, cropped to its shape.
			const double bw = a.thumb.GetWidth(), bh = a.thumb.GetHeight();
			const double s = std::max(sz.x / bw, sz.y / bh);
			gc->PushState();
			gc->Clip(wxRegion(0, 0, sz.x, sz.y));
			gc->DrawBitmap(a.thumb, (sz.x - bw * s) / 2, (sz.y - bh * s) / 2, bw * s, bh * s);
			gc->PopState();
		} else {
			gc->SetFont(font(11), ui::dim());
			centredText(gc, a.name, 0, 0, sz.x, sz.y);
		}
		gc->PopState();
		// The rounded corners: paint the page back over what's outside them.
		wxGraphicsPath corners = gc->CreatePath();
		corners.AddRectangle(-1, -1, sz.x + 2, sz.y + 2);
		corners.AddRoundedRectangle(0, 0, sz.x, sz.y, r);
		gc->SetBrush(wxBrush(GetParent()->GetBackgroundColour()));
		gc->FillPath(corners, wxODDEVEN_RULE);
		gc->SetBrush(*wxTRANSPARENT_BRUSH);
		gc->SetPen(wxPen(lineColour(), 1));
		gc->DrawRoundedRectangle(0.5, 0.5, sz.x - 1, sz.y - 1, r);
		// The x.
		const wxRect x = cross();
		gc->SetPen(*wxTRANSPARENT_PEN);
		gc->SetBrush(wxBrush(wxColour(0, 0, 0, hot ? 200 : 150)));
		gc->DrawEllipse(x.x, x.y, x.width, x.height);
		gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(*wxWHITE).Width(dip(1.6))));
		const double m = x.width * 0.33;
		gc->StrokeLine(x.x + m, x.y + m, x.GetRight() + 1 - m, x.GetBottom() + 1 - m);
		gc->StrokeLine(x.GetRight() + 1 - m, x.y + m, x.x + m, x.GetBottom() + 1 - m);
		focusRing(gc, 1, 1, sz.x - 2, sz.y - 2, r);
	}
	Attachment a;
};

// A checkbox of our own (the stock one ignores dark mode on Windows), with a
// line of explanation under it if wanted.
class Check : public Drawn {
public:
	Check(wxWindow* parent, const wxString& label, const wxString& sub, bool value)
		: Drawn(parent, wxSize(fitWidth(parent, sub.empty() ? label : sub, font(sub.empty() ? 12.5 : 11), 34),
		                       sub.empty() ? 22 : 36)),
		  label(label), sub(sub), on(value) {}
	bool GetValue() const { return on; }
	void SetValue(bool v) { if (v != on) { on = v; Refresh(); } }

private:
	void clicked(const wxPoint&) override {
		on = !on;
		Refresh();
		if (onClick) onClick();
	}
	void draw(wxGraphicsContext* gc, const wxSize& sz) override {
		const bool enabled = IsEnabled();
		const wxColour ac = ui::accent(), in = ui::ink();
		const double b = dip(16), top = sub.empty() ? (sz.y - b) / 2 : dip(2);
		gc->SetBrush(wxBrush(on ? ui::withAlpha(ac, enabled ? 1.0 : 0.4) : fieldColour()));
		gc->SetPen(wxPen(on ? ui::withAlpha(ac, enabled ? 1.0 : 0.4) : ui::withAlpha(in, hot ? 0.45 : 0.3), 1));
		gc->DrawRoundedRectangle(0.5, top + 0.5, b - 1, b - 1, dip(4));
		if (on) {
			gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(*wxWHITE).Width(dip(2)).Cap(wxCAP_ROUND).Join(wxJOIN_ROUND)));
			wxGraphicsPath tick = gc->CreatePath();
			tick.MoveToPoint(b * 0.26, top + b * 0.52);
			tick.AddLineToPoint(b * 0.43, top + b * 0.69);
			tick.AddLineToPoint(b * 0.75, top + b * 0.32);
			gc->StrokePath(tick);
		}
		gc->SetFont(font(12.5), enabled ? in : ui::dim());
		double tw, th;
		gc->GetTextExtent(label, &tw, &th);
		const double x = b + dip(9);
		gc->DrawText(label, x, sub.empty() ? (sz.y - th) / 2 : top + (b - th) / 2);
		if (!sub.empty()) {
			gc->SetFont(font(11), ui::dim());
			gc->DrawText(sub, x, top + b + dip(2));
		}
		focusRing(gc, 0, top - 1, b + 1, b + 1, dip(5));
	}
	wxString label, sub;
	bool on;
};

// The dark band across the top: the icon, the name, what this is for.
class Band : public wxPanel {
public:
	explicit Band(wxWindow* parent) : wxPanel(parent, wxID_ANY) {
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		SetInitialSize(FromDIP(wxSize(-1, 112)));
		const cl::res::Blob png = cl::res::find("icon.png");
		if (png.ok()) {
			wxMemoryInputStream in(png.data, png.size);
			wxImage img(in, wxBITMAP_TYPE_PNG);
			if (img.IsOk()) icon = img;
		}
		Bind(wxEVT_PAINT, [this](wxPaintEvent&) { paint(); });
		Bind(wxEVT_SIZE, [this](wxSizeEvent& e) { Refresh(); e.Skip(); });
	}

private:
	double dip(double v) const { return v * FromDIP(1000) / 1000.0; }
	void paint() {
		wxAutoBufferedPaintDC dc(this);
		std::unique_ptr<wxGraphicsContext> gc(ui::graphics(dc));
		if (!gc) return;
		const wxSize sz = GetClientSize();
		gc->SetPen(*wxTRANSPARENT_PEN);
		gc->SetBrush(gc->CreateLinearGradientBrush(0, 0, sz.x, sz.y, BAND_INK, BAND_DEEP));
		gc->DrawRectangle(0, 0, sz.x, sz.y);

		// A few traces, as on a circuit board, fading in to the right.
		gc->SetPen(gc->CreatePen(wxGraphicsPenInfo().LinearGradient(sz.x * 0.5, 0, sz.x, 0,
			wxColour(NEON.Red(), NEON.Green(), NEON.Blue(), 0), wxColour(NEON.Red(), NEON.Green(), NEON.Blue(), 60))
			.Width(dip(1.2))));
		for (int i = 0; i < 5; i++) {
			const double y = dip(20 + i * 15);
			wxGraphicsPath p = gc->CreatePath();
			p.MoveToPoint(sz.x * 0.52, y);
			p.AddLineToPoint(sz.x * 0.66 + dip(i * 14), y);
			p.AddLineToPoint(sz.x * 0.70 + dip(i * 14), y + dip(10));
			p.AddLineToPoint(sz.x, y + dip(10));
			gc->StrokePath(p);
		}
		gc->SetPen(*wxTRANSPARENT_PEN);
		gc->SetBrush(wxBrush(wxColour(NEON.Red(), NEON.Green(), NEON.Blue(), 70)));
		for (int i = 0; i < 5; i++) {
			const double x = sz.x * 0.70 + dip(i * 14), y = dip(30 + i * 15);
			gc->DrawEllipse(x - dip(2.5), y - dip(2.5), dip(5), dip(5));
		}

		const double left = dip(26), size = dip(46), y0 = (sz.y - size) / 2 + dip(4);
		if (icon.IsOk()) {
			const int px = (int)std::lround(size * backing(this));
			const wxBitmap b(icon.Scale(px, px, wxIMAGE_QUALITY_HIGH));
			gc->DrawBitmap(b, left, y0, size, size);
		}
		const double tx = left + size + dip(14);
		gc->SetFont(font(21, true), *wxWHITE);
		double tw, th;
		const wxString title = "Send Feedback";
		gc->GetTextExtent(title, &tw, &th);
		const double ty = y0 + dip(1);
		gc->DrawText(title, tx, ty);
		// BETA, in the neon.
		gc->SetFont(font(9.5, true), wxColour(3, 6, 5));
		double bw, bh;
		gc->GetTextExtent("BETA", &bw, &bh);
		const double px = tx + tw + dip(9), pw = bw + dip(12), ph = bh + dip(3);
		gc->SetBrush(wxBrush(NEON));
		gc->DrawRoundedRectangle(px, ty + (th - ph) / 2, pw, ph, ph / 2);
		gc->DrawText("BETA", px + dip(6), ty + (th - ph) / 2 + dip(1.5));
		gc->SetFont(font(12.5), wxColour(255, 255, 255, 170));
		gc->DrawText("What's working, what's broken, what you'd love to see. It goes straight to Levi.",
		             tx, ty + th + dip(4));
	}
	wxImage icon;
};

// The line at the foot of the form: "Also sent: ...", which opens the list.
class InfoLine : public Drawn {
public:
	explicit InfoLine(wxWindow* parent) : Drawn(parent, wxSize(200, 22)) {
		SetToolTip("What's sent along with it, so Levi knows where it happened");
	}
	void setText(const wxString& s) { text = s; Refresh(); }

private:
	void draw(wxGraphicsContext* gc, const wxSize& sz) override {
		const wxColour c = hot ? ui::ink() : ui::dim();
		const double d = dip(13), y = (sz.y - d) / 2;
		gc->SetBrush(*wxTRANSPARENT_BRUSH);
		gc->SetPen(wxPen(c, (int)std::max(1.0, dip(1.2))));
		gc->DrawEllipse(0.5, y, d, d);
		gc->SetPen(*wxTRANSPARENT_PEN);
		gc->SetBrush(wxBrush(c));
		gc->DrawRectangle(d / 2 - dip(0.6) + 0.5, y + d * 0.45, dip(1.2), d * 0.32);
		gc->DrawEllipse(d / 2 - dip(0.9) + 0.5, y + d * 0.2, dip(1.8), dip(1.8));
		gc->SetFont(font(11), c);
		const double x = d + dip(7), room = sz.x - x;
		wxString s = text;
		double tw, th;
		gc->GetTextExtent(s, &tw, &th);
		while (tw > room && s.length() > 4) {
			s = s.Left(s.length() - 2) + wxString::FromUTF8("\u2026");
			s.Replace(wxString::FromUTF8("\u2026\u2026"), wxString::FromUTF8("\u2026"));
			gc->GetTextExtent(s, &tw, &th);
		}
		gc->DrawText(s, x, (sz.y - th) / 2);
	}
	wxString text;
};

// Sending: a thin bar and what it's doing; or why it didn't go.
class Status : public wxPanel {
public:
	explicit Status(wxWindow* parent) : wxPanel(parent, wxID_ANY) {
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		SetInitialSize(FromDIP(wxSize(-1, 0)));
		Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
			wxAutoBufferedPaintDC dc(this);
			dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
			dc.Clear();
			std::unique_ptr<wxGraphicsContext> gc(ui::graphics(dc));
			if (!gc) return;
			const wxSize sz = GetClientSize();
			const double k = FromDIP(1000) / 1000.0;
			const Model& m = model();
			if (m.phase == Phase::Sending) {
				const double h = 4 * k, y = 8 * k, w = sz.x;
				gc->SetPen(*wxTRANSPARENT_PEN);
				gc->SetBrush(wxBrush(ui::withAlpha(ui::ink(), 0.1)));
				gc->DrawRoundedRectangle(0, y, w, h, h / 2);
				gc->SetBrush(wxBrush(ui::accent()));
				gc->DrawRoundedRectangle(0, y, std::max(h, w * m.progress), h, h / 2);
				gc->SetFont(font(11), ui::dim());
				gc->DrawText(m.note, 0, y + h + 5 * k);
			} else if (m.phase == Phase::Failed) {
				gc->SetFont(font(11.5), ui::isDark() ? wxColour(255, 120, 110) : wxColour(200, 40, 30));
				double y = 6 * k;
				for (const wxString& line : wxSplit(m.note, '\n')) {
					double tw, th;
					gc->GetTextExtent(line, &tw, &th);
					gc->DrawText(line, 0, y);
					y += th + 2 * k;
				}
			}
		});
	}
	void update() {
		const Phase p = model().phase;
		const int lines = p == Phase::Failed ? (int)wxSplit(model().note, '\n').size() : 0;
		const int h = p == Phase::Sending ? 34 : p == Phase::Failed ? 12 + 17 * lines : 0;
		if (GetMinSize().y != FromDIP(h)) {
			SetMinSize(FromDIP(wxSize(-1, h)));
			GetParent()->Layout();
		}
		Refresh();
	}
};

// After it's gone: a tick in a ring, and thanks.
class SentPanel : public wxPanel {
public:
	explicit SentPanel(wxWindow* parent) : wxPanel(parent, wxID_ANY) {
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		Bind(wxEVT_PAINT, [this](wxPaintEvent&) { paint(); });
		beat.Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
			step = std::min(1.0, step + 0.06);
			if (step >= 1.0) beat.Stop();
			Refresh();
		});
		another = new Button(this, "Send Another", false);
		done = new Button(this, "Done", true);
		Bind(wxEVT_SIZE, [this](wxSizeEvent& e) { place(); e.Skip(); });
	}
	void start() { step = 0; beat.Start(16); place(); }
	Button* another;
	Button* done;

private:
	double dip(double v) const { return v * FromDIP(1000) / 1000.0; }
	double mid() const { return GetClientSize().y * 0.36; }
	void place() {
		const wxSize sz = GetClientSize();
		const wxSize a = another->GetSize(), d = done->GetSize();
		const int gap = FromDIP(10), y = (int)(mid() + dip(150));
		const int x = (sz.x - (a.x + gap + d.x)) / 2;
		another->SetPosition(wxPoint(x, y));
		done->SetPosition(wxPoint(x + a.x + gap, y));
	}
	void paint() {
		wxAutoBufferedPaintDC dc(this);
		dc.SetBackground(wxBrush(GetBackgroundColour()));
		dc.Clear();
		std::unique_ptr<wxGraphicsContext> gc(ui::graphics(dc));
		if (!gc) return;
		const wxSize sz = GetClientSize();
		const double cx = sz.x / 2.0, cy = mid() - dip(20), r = dip(36);
		const double e = 1 - std::pow(1 - step, 3);
		const wxColour green = ui::isDark() ? NEON : NEON_DEEP;
		gc->SetBrush(wxBrush(ui::withAlpha(green, 0.12 * e)));
		gc->SetPen(*wxTRANSPARENT_PEN);
		gc->DrawEllipse(cx - r, cy - r, 2 * r, 2 * r);
		gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(green).Width(dip(3.5)).Cap(wxCAP_ROUND).Join(wxJOIN_ROUND)));
		wxGraphicsPath ring = gc->CreatePath();
		const double pi = 3.14159265358979;
		ring.AddArc(cx, cy, r, -pi / 2, -pi / 2 + 2 * pi * std::min(1.0, e * 1.25), true);
		gc->StrokePath(ring);
		const double t = std::max(0.0, std::min(1.0, (e - 0.45) / 0.55));
		if (t > 0) {
			const wxPoint2DDouble a(cx - r * 0.38, cy + r * 0.02), b(cx - r * 0.1, cy + r * 0.3),
			                      c(cx + r * 0.4, cy - r * 0.28);
			wxGraphicsPath tick = gc->CreatePath();
			tick.MoveToPoint(a);
			const double first = std::min(1.0, t * 2);
			tick.AddLineToPoint(a.m_x + (b.m_x - a.m_x) * first, a.m_y + (b.m_y - a.m_y) * first);
			if (t > 0.5) {
				const double second = (t - 0.5) * 2;
				tick.AddLineToPoint(b.m_x + (c.m_x - b.m_x) * second, b.m_y + (c.m_y - b.m_y) * second);
			}
			gc->StrokePath(tick);
		}
		const wxString name = model().name();
		const wxString first = name.BeforeFirst(' ');
		const wxString thanks = name.empty() ? wxString("Thank you!") : "Thank you, " + first + "!";
		gc->SetFont(font(24, true), ui::ink());
		double tw, th;
		gc->GetTextExtent(thanks, &tw, &th);
		gc->DrawText(thanks, cx - tw / 2, cy + r + dip(22));
		wxString line = "Your feedback is on its way to Levi.";
		if (!model().sentTo.empty()) line += " If he has a question, he'll write to " + model().sentTo + ".";
		gc->SetFont(font(13.5), ui::dim());
		double y = cy + r + dip(22) + th + dip(10);
		for (const wxString& l : ui::wrap(gc.get(), line, dip(400))) {
			double lw, lh;
			gc->GetTextExtent(l, &lw, &lh);
			gc->DrawText(l, cx - lw / 2, y);
			y += lh + dip(2);
		}
	}
	wxTimer beat;
	double step = 1;
};

class FeedbackWindow;
FeedbackWindow* g_window = nullptr;

class FeedbackWindow : public wxDialog {
public:
	explicit FeedbackWindow(MainFrame* frame)
		: wxDialog(frame, wxID_ANY, "Send Feedback", wxDefaultPosition, wxDefaultSize,
		           wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
		  frame(frame) {
		g_window = this;
		g_feedback = this;
		SetBackgroundColour(ui::paper());
		SetForegroundColour(ui::ink());
		wxBoxSizer* outer = new wxBoxSizer(wxVERTICAL);
		outer->Add(new Band(this), 0, wxEXPAND);

		body = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxBORDER_NONE);
		body->SetBackgroundColour(ui::paper());
		body->SetScrollRate(0, FromDIP(12));
		buildForm();
		outer->Add(body, 1, wxEXPAND);

		footer = new wxPanel(this);
		footer->SetBackgroundColour(ui::paper());
		buildFooter();
		outer->Add(footer, 0, wxEXPAND);

		sent = new SentPanel(this);
		sent->SetBackgroundColour(ui::paper());
		sent->another->onClick = [] { model().phase = Phase::Writing; model().notify(); };
		sent->done->onClick = [this] { Close(); };
		outer->Add(sent, 1, wxEXPAND);
		sent->Hide();
		SetSizer(outer);

		// As tall as the form, but never taller than the screen it's on.
		const wxRect screen = wxDisplay(wxDisplay::GetFromWindow(frame) == wxNOT_FOUND ? 0
		                                : wxDisplay::GetFromWindow(frame)).GetClientArea();
		SetClientSize(FromDIP(620), std::min(FromDIP(780), screen.height - FromDIP(60)));
		SetMinClientSize(FromDIP(wxSize(560, 460)));
		CentreOnParent();

		Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
			const int k = e.GetKeyCode();
			if (k == WXK_ESCAPE) { Close(); return; }
			if ((k == WXK_RETURN || k == WXK_NUMPAD_ENTER) && e.CmdDown()) { send(); return; }
			e.Skip();
		});
		Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) {
			model().changed = nullptr;
			if (model().phase == Phase::Sent) { model().phase = Phase::Writing; }
			g_window = nullptr;
			g_feedback = nullptr;
			Destroy();
		});

		model().changed = [this] { CallAfter([this] { if (g_window == this) sync(); }); };
		if (model().phase == Phase::Sent || model().phase == Phase::Failed) model().phase = Phase::Writing;
		sync();
		applyTheme();
		if (title->GetValue().empty()) title->SetFocus();
	}

	void sync() {
		Model& m = model();
		const bool isSent = m.phase == Phase::Sent;
		if (isSent != sent->IsShown()) {
			body->Show(!isSent);
			footer->Show(!isSent);
			sent->Show(isSent);
			Layout();
			if (isSent) { sent->start(); sent->done->SetFocus(); }
			else title->SetFocus();
		}
		for (Chip* c : chips) c->setState(m.isOn(c->tag), m.isSuggested(c->tag));
		tagNote->SetLabel(m.suggested().empty() ? wxString() : wxString::FromUTF8("\u2726 suggested from what you wrote"));
		for (size_t i = 0; i < pills.size(); i++) pills[i]->setOn((int)i == m.priority);
		priorityHint->SetLabel(PRIORITIES[m.priority].hint);
		screenshot->Enable(m.images() < Model::MAX_IMAGES && m.phase != Phase::Sending);
		addImage->Enable(m.images() < Model::MAX_IMAGES && m.phase != Phase::Sending);
		contact->Enable(!m.email.Strip(wxString::both).empty());
		circuit->SetValue(m.includeCircuit);
		const bool sending = m.phase == Phase::Sending;
		sendButton->Enable(m.canSend());
		sendButton->SetLabelText(sending ? wxString::Format("Sending %d%%", (int)std::lround(100 * m.progress))
		                                 : wxString("Send Feedback"));
		if (thumbsShown != attachmentKey()) rebuildThumbs();
		status->update();
		footer->Layout();
		body->Layout();
		body->FitInside();
	}

	void applyTheme() {
#ifdef __WXMSW__
		WinSetDarkTitlebar(this, true);
		WinSetCaptionColour(this, BAND_INK, *wxWHITE);
		WinThemeControls(this, ui::isDark());
#endif
	}

	MainFrame* frame;

private:
	wxStaticText* sectionLabel(wxWindow* parent, const wxString& s) {
		wxStaticText* t = new wxStaticText(parent, wxID_ANY, s.Upper());
		t->SetFont(font(10.5, true));
		t->SetForegroundColour(ui::dim());
		return t;
	}
	wxStaticText* note(wxWindow* parent, const wxString& s, double size = 11) {
		wxStaticText* t = new wxStaticText(parent, wxID_ANY, s);
		t->SetFont(font(size));
		t->SetForegroundColour(ui::withAlpha(ui::dim(), 0.85));
		return t;
	}
	// A section: its name (and a note at the right), then what's in it.
	wxBoxSizer* section(wxBoxSizer* column, const wxString& name, wxStaticText** noteOut = nullptr,
	                    const wxString& noteText = "") {
		wxBoxSizer* head = new wxBoxSizer(wxHORIZONTAL);
		head->Add(sectionLabel(body, name), 0, wxALIGN_BOTTOM);
		head->AddStretchSpacer();
		wxStaticText* n = note(body, noteText);
		head->Add(n, 0, wxALIGN_BOTTOM);
		if (noteOut) *noteOut = n;
		column->Add(head, 0, wxEXPAND | wxTOP, FromDIP(column->IsEmpty() ? 20 : 22));
		wxBoxSizer* content = new wxBoxSizer(wxVERTICAL);
		column->Add(content, 0, wxEXPAND | wxTOP, FromDIP(9));
		return content;
	}
	wxTextCtrl* field(wxWindow* parent, const wxString& value, const wxString& hint, bool multi) {
		wxTextCtrl* t = new wxTextCtrl(parent, wxID_ANY, value, wxDefaultPosition,
		                               multi ? FromDIP(wxSize(-1, 110)) : wxDefaultSize,
		                               multi ? (wxTE_MULTILINE | wxTE_RICH2) : 0);
		if (!multi) t->SetHint(hint);   // the several-line box has its hint above it
		t->SetFont(font(13));
		return t;
	}

	void buildForm() {
		Model& m = model();
		wxBoxSizer* column = new wxBoxSizer(wxVERTICAL);

		wxBoxSizer* s = section(column, "What's on your mind?");
		title = field(body, m.title, wxString::FromUTF8("A short summary, like \u201CC doesn't connect the wire\u201D"), false);
		title->SetMaxLength(200);
		title->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
			model().title = title->GetValue();
			model().save();
			sync();
		});
		s->Add(title, 0, wxEXPAND);
		details = field(body, m.details,
			"What happened, what you expected, and how to make it happen again (if you know).", true);
		details->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
			model().details = details->GetValue();
			model().save();
			sync();
		});
		// A box of several lines shows no hint inside it on any of the three
		// systems, so the hint goes just above it.
		s->Add(note(body, "What happened, what you expected, and how to make it happen again (if you know).", 11.5),
		       0, wxTOP, FromDIP(12));
		s->Add(details, 0, wxEXPAND | wxTOP, FromDIP(5));

		s = section(column, "Tags", &tagNote);
		wxWrapSizer* flow = new wxWrapSizer(wxHORIZONTAL);
		for (int i = 0; i < TAG_COUNT; i++) {
			Chip* c = new Chip(body, TAGS[i]);
			c->onClick = [c] { model().toggle(c->tag); };
			chips.push_back(c);
			flow->Add(c, 0, wxRIGHT | wxBOTTOM, FromDIP(7));
		}
		s->Add(flow, 0, wxEXPAND);

		s = section(column, "How much does it matter?");
		wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
		for (int i = 0; i < 4; i++) {
			PriorityPill* p = new PriorityPill(body, i);
			p->onClick = [i] { model().priority = i; model().save(); model().notify(); };
			pills.push_back(p);
			row->Add(p, 1, i ? wxLEFT : 0, FromDIP(8));
		}
		s->Add(row, 0, wxEXPAND);
		priorityHint = note(body, "", 11.5);
		s->Add(priorityHint, 0, wxTOP, FromDIP(7));

		s = section(column, "Show it", nullptr, "Only the CedarLogic window is captured");
		shots = new wxWrapSizer(wxHORIZONTAL);
		screenshot = new Tile(body, "camera", "Screenshot");
		screenshot->SetToolTip("A picture of the CedarLogic window as it is now");
		screenshot->onClick = [this] { takeScreenshot(); };
		addImage = new Tile(body, "image", "Add image...");
		addImage->SetToolTip("A picture from your computer (PNG or JPEG). You can drop one here too.");
		addImage->onClick = [this] { pickImages(); };
		shots->Add(screenshot, 0, wxRIGHT | wxBOTTOM, FromDIP(10));
		shots->Add(addImage, 0, wxRIGHT | wxBOTTOM, FromDIP(10));
		s->Add(shots, 0, wxEXPAND);
		circuit = new Check(body, "Send this circuit too", "Makes a problem easy to repeat. It's only sent with this.",
		                    m.includeCircuit);
		circuit->onClick = [this] { model().includeCircuit = circuit->GetValue(); };
		s->Add(circuit, 0, wxTOP, FromDIP(4));

		s = section(column, "About you");
		wxFlexGridSizer* grid = new wxFlexGridSizer(2, FromDIP(4), FromDIP(10));
		grid->AddGrowableCol(0, 1);
		grid->AddGrowableCol(1, 1);
		grid->Add(note(body, "Name", 11.5));
		grid->Add(note(body, "Email (if you'd like a reply)", 11.5));
		name = field(body, m.name(), "First and last name", false);
		name->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { model().setName(name->GetValue()); });
		email = field(body, m.email, "you@example.com", false);
		email->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
			model().email = email->GetValue();
			model().save();
			sync();
		});
		grid->Add(name, 0, wxEXPAND);
		grid->Add(email, 0, wxEXPAND);
		s->Add(grid, 0, wxEXPAND);
		contact = new Check(body, "Levi can email me about this", "", m.contactOK);
		contact->onClick = [this] { model().contactOK = contact->GetValue(); model().save(); };
		s->Add(contact, 0, wxTOP, FromDIP(10));

		wxBoxSizer* pad = new wxBoxSizer(wxVERTICAL);
		pad->Add(column, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(26));
		body->SetSizer(pad);

		// Pictures dropped anywhere on the form.
		class Drop : public wxFileDropTarget {
		public:
			explicit Drop(FeedbackWindow* w) : w(w) {}
			bool OnDropFiles(wxCoord, wxCoord, const wxArrayString& files) override {
				w->addFiles(files);
				return true;
			}
			FeedbackWindow* w;
		};
		body->SetDropTarget(new Drop(this));
	}

	void buildFooter() {
		wxBoxSizer* col = new wxBoxSizer(wxVERTICAL);
		wxPanel* rule = new wxPanel(footer, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
		rule->SetBackgroundColour(ui::isDark() ? wxColour(48, 52, 60) : wxColour(226, 228, 233));
		col->Add(rule, 0, wxEXPAND);
		wxBoxSizer* inner = new wxBoxSizer(wxVERTICAL);
		status = new Status(footer);
		inner->Add(status, 0, wxEXPAND);
		wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
		info = new InfoLine(footer);
		info->setText("Also sent: " + deviceLine());
		info->onClick = [this] { showFacts(); };
		row->Add(info, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
		Button* close = new Button(footer, "Close", false);
		close->onClick = [this] { Close(); };
		row->Add(close, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));
		sendButton = new Button(footer, "Send Feedback", true);
		sendButton->SetToolTip(
#ifdef __WXOSX__
			"Cmd+Return"
#else
			"Ctrl+Enter"
#endif
		);
		sendButton->onClick = [this] { send(); };
		row->Add(sendButton, 0, wxALIGN_CENTER_VERTICAL);
		inner->Add(row, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(14));
		col->Add(inner, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(26));
		footer->SetSizer(col);
	}

	void showFacts() {
		const Facts f = gatherFacts(frame);
		wxString text;
		auto group = [&](const wxString& title, const std::vector<Fact>& list) {
			text += title + "\n";
			for (const Fact& x : list) text += "    " + x.label + ": " + x.shown + "\n";
			text += "\n";
		};
		group("CedarLogic", f.app);
		group("This computer", f.system);
		group("The app right now", f.context);
		text += "Nothing else leaves your computer.";
		ui::Message(text, "Sent with your feedback", wxOK | wxCENTRE, this);
	}

	void send() {
		Model& m = model();
		if (m.title.Strip(wxString::both).empty()) {
			title->SetFocus();
			wxBell();
			return;
		}
		m.send(frame);
	}

	void takeScreenshot() {
		Model& m = model();
		if (m.images() >= Model::MAX_IMAGES) { wxBell(); return; }
		wxImage shot;
		if (!CaptureAppWindow(frame, shot) || !shot.IsOk()) {
			ui::Message("The window couldn't be captured here. Try Add image... with a screenshot of your own.",
			            "Screenshot", wxOK | wxICON_INFORMATION, this);
			return;
		}
		const wxString name = m.freeName("screenshot", "png");
		const wxString path = scratchFile("-" + name);
		if (!shot.SaveFile(path, wxBITMAP_TYPE_PNG)) { wxBell(); return; }
		m.addImage(Attachment::Screenshot, path, name, "image/png", thumbnail(shot));
	}

	static wxImage thumbnail(const wxImage& img) {
		if (!img.IsOk()) return wxImage();
		const double s = std::min(1.0, 400.0 / std::max(img.GetWidth(), img.GetHeight()));
		return img.Scale(std::max(1, (int)(img.GetWidth() * s)), std::max(1, (int)(img.GetHeight() * s)),
		                 wxIMAGE_QUALITY_HIGH);
	}

	void pickImages() {
		wxFileDialog d(this, "Add images", "", "", "Images (*.png;*.jpg;*.jpeg)|*.png;*.jpg;*.jpeg;*.PNG;*.JPG;*.JPEG",
		               wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);
		if (d.ShowModal() != wxID_OK) return;
		wxArrayString files;
		d.GetPaths(files);
		addFiles(files);
	}

	void addFiles(const wxArrayString& files) {
		Model& m = model();
		bool refused = false;
		for (const wxString& f : files) {
			if (m.images() >= Model::MAX_IMAGES) { refused = true; break; }
			const wxString ext = wxFileName(f).GetExt().Lower();
			const bool png = ext == "png";
			if (!png && ext != "jpg" && ext != "jpeg") { refused = true; continue; }
			const long long size = fileSize(f);
			if (size <= 0 || size > 12000000) { refused = true; continue; }
			const wxString name = m.freeName("image", png ? "png" : "jpg");
			const wxString copy = scratchFile("-" + name);
			if (!wxCopyFile(f, copy)) { refused = true; continue; }
			wxImage img;
			{
				wxLogNull quiet;   // a JPEG this build can't show still goes
				img.LoadFile(copy, png ? wxBITMAP_TYPE_PNG : wxBITMAP_TYPE_JPEG);
			}
			m.addImage(Attachment::Image, copy, name, png ? "image/png" : "image/jpeg", thumbnail(img));
		}
		if (refused) wxBell();
	}

	wxString attachmentKey() const {
		wxString k;
		for (const Attachment& a : model().attachments) k += a.path + "|";
		return k;
	}

	void rebuildThumbs() {
		for (Thumb* t : thumbs) { shots->Detach(t); t->Destroy(); }
		thumbs.clear();
		const auto& list = model().attachments;
		for (size_t i = 0; i < list.size(); i++) {
			Thumb* t = new Thumb(body, list[i]);
			const wxString path = list[i].path;
			t->onRemove = [path] {
				auto& all = model().attachments;
				for (size_t j = 0; j < all.size(); j++)
					if (all[j].path == path) { model().remove(j); break; }
			};
			thumbs.push_back(t);
			shots->Add(t, 0, wxRIGHT | wxBOTTOM, FromDIP(10));
		}
		thumbsShown = attachmentKey();
#ifdef __WXMSW__
		WinThemeControls(body, ui::isDark());
#endif
	}

	wxScrolledWindow* body;
	wxPanel* footer;
	SentPanel* sent;
	wxTextCtrl *title, *details, *name, *email;
	std::vector<Chip*> chips;
	std::vector<PriorityPill*> pills;
	std::vector<Thumb*> thumbs;
	wxString thumbsShown;
	wxStaticText *tagNote, *priorityHint;
	wxWrapSizer* shots;
	Tile *screenshot, *addImage;
	Check *circuit, *contact;
	InfoLine* info;
	Button* sendButton;
	Status* status;
};

}  // namespace

void ShowFeedback(MainFrame* frame) {
	if (g_window) {
		g_window->Show();
		g_window->Raise();
		return;
	}
	FeedbackWindow* w = new FeedbackWindow(frame);
	w->Show();
}

wxTopLevelWindow* FeedbackWindowForCapture(MainFrame* frame) {
	if (g_window) g_window->Destroy();
	g_window = nullptr;
	FeedbackWindow* w = new FeedbackWindow(frame);
	w->Show();
	return w;
}

void DismissFeedback() {
	if (g_window) g_window->Close();
}

namespace {
void finish(int code) {
	fflush(nullptr);
	std::_Exit(code);
}
}  // namespace

void StartFeedbackProbe() {
	// A key the site can't know: it has to answer 403, "Not from CedarLogic."
	const wxString body = scratchFile(".json");
	writeFile(body, "{}", 2);
	fprintf(stderr, "feedback probe: %s via %s\n", (const char*)serverBase().ToUTF8(),
	        (const char*)curlProgram().ToUTF8());
	httpCall("POST", "/api/feedback", { "content-type: application/json", "x-cedarlogic-key: probe" }, body,
		[](const Reply& r) {
			const wxString said = jsonText(r.body, "error");
			fprintf(stderr, "feedback probe: status %d, error \"%s\"%s%s\n", r.status, (const char*)said.ToUTF8(),
			        r.problem.empty() ? "" : ", problem: ", (const char*)r.problem.ToUTF8());
			finish(r.status == 403 && !said.empty() ? 0 : 1);
		});
	later(90000, [] { fprintf(stderr, "feedback probe: no answer in 90 s\n"); finish(2); });
}

static void selfTestNow(MainFrame* frame, const wxString& title) {
	Model& m = model();
	// Put the draft aside; this one is the test's.
	const wxString oldTitle = m.title, oldDetails = m.details;
	const auto oldChosen = m.chosen, oldDeclined = m.declined;
	const auto oldAttachments = m.attachments;
	m.attachments.clear();
	m.title = title;
	m.details = "Sent by --feedback-selftest to check sending works on this machine. It can be deleted.";
	m.chosen = { "Other" };
	m.declined.clear();
	m.priority = 0;
	m.includeCircuit = true;
	wxImage shot;
	const bool captured = CaptureAppWindow(frame, shot) && shot.IsOk();
	if (captured) {
		const wxString path = scratchFile("-screenshot-1.png");
		shot.SaveFile(path, wxBITMAP_TYPE_PNG);
		m.addImage(Attachment::Screenshot, path, "screenshot-1.png", "image/png", wxImage());
		wxString keep;
		if (wxGetEnv("CL_FEEDBACK_KEEP", &keep) && !keep.empty()) wxCopyFile(path, keep);
	}
	fprintf(stderr, "feedback selftest: screenshot %s (%lld bytes)\n", captured ? "taken" : "NOT taken",
	        captured ? m.attachments.back().size : 0LL);
	// CL_FEEDBACK_EXTRA=<a .png>: one more picture, to send one big enough
	// to go in several pieces.
	wxString extra;
	if (wxGetEnv("CL_FEEDBACK_EXTRA", &extra) && wxFileExists(extra)) {
		const wxString copy = scratchFile("-image-1.png");
		wxCopyFile(extra, copy);
		m.addImage(Attachment::Image, copy, "image-1.png", "image/png", wxImage());
	}
	Model* mp = &m;
	m.changed = [mp, oldTitle, oldDetails, oldChosen, oldDeclined, oldAttachments] {
		if (mp->phase == Phase::Sending) {
			fprintf(stderr, "feedback selftest: %3.0f%% %s\n", 100 * mp->progress, (const char*)mp->note.ToUTF8());
			return;
		}
		const bool ok = mp->phase == Phase::Sent;
		fprintf(stderr, "feedback selftest: %s%s\n", ok ? "SENT" : "FAILED: ", ok ? "" : (const char*)mp->note.ToUTF8());
		mp->title = oldTitle;
		mp->details = oldDetails;
		mp->chosen = oldChosen;
		mp->declined = oldDeclined;
		mp->attachments = oldAttachments;
		mp->phase = Phase::Writing;
		mp->includeCircuit = false;
		mp->save();
		finish(ok ? 0 : 1);
	};
	m.send(frame);
	later(300000, [] { fprintf(stderr, "feedback selftest: not done in 5 minutes\n"); finish(2); });
}

void StartFeedbackSelfTest(MainFrame* frame, const wxString& title) {
	// Once the window has had time to be drawn.
	later(2500, [frame, title] { selfTestNow(frame, title); });
}

bool RenderFeedback(MainFrame* frame, const wxString& dir) {
	wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
	auto settle = [] {
		for (int i = 0; i < 30; i++) { wxYield(); wxMilliSleep(25); }
	};
	bool ok = true;
	const bool wasDark = renderMode().darkMode;
	for (int dark = 0; dark < 2; dark++) {
		renderMode().darkMode = dark != 0;
		frame->ApplyTheme();
		wxTopLevelWindow* fb = FeedbackWindowForCapture(frame);
		settle();
		wxImage img;
		const bool got = grabWindow(fb, img, nullptr) && img.IsOk();
		const wxString name = dir + (dark ? "/feedback-dark.png" : "/feedback-light.png");
		fprintf(stderr, "render-feedback: %s %s\n", (const char*)name.ToUTF8(), got ? "captured" : "NOT captured");
		ok &= got && img.SaveFile(name, wxBITMAP_TYPE_PNG);
		if (dark) {
			wxImage shot;
			const bool took = CaptureAppWindow(frame, shot) && shot.IsOk();
			fprintf(stderr, "render-feedback: screenshot %s\n", took ? "taken" : "NOT taken");
			ok &= took && shot.SaveFile(dir + "/feedback-screenshot.png", wxBITMAP_TYPE_PNG);
		}
		DismissFeedback();
		settle();
	}
	renderMode().darkMode = wasDark;
	return ok;
}
