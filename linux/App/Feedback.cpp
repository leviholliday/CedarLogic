// Send Feedback (see Feedback.h). Top to bottom: the draft and its tags,
// what's sent about the computer, talking to the site (curl, on a thread of
// its own), and the window.

#include "Feedback.h"
#include "Brand.h"
#include "Canvas.h"
#include "Window.h"

#include <glib/gstdio.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <clocale>
#include <cmath>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

namespace feedback {

namespace {

const char* const kTags[] = { "Bug", "Idea", "Design", "Toolbar", "Canvas & wiring", "Simulation", "Files & saving", "Performance", "Other" };
const int kTagCount = 9;
const int kMaxImages = 3;

struct Priority { const char* id; const char* name; const char* hint; Color color; };
const Priority kPriorities[] = {
	{ "low", "Low", "Small thing, whenever", colorF(0.55f, 0.55f, 0.55f) },
	{ "normal", "Normal", "Worth fixing", colorF(0.23f, 0.52f, 0.95f) },
	{ "high", "High", "Gets in my way", colorF(0.95f, 0.55f, 0.12f) },
	{ "blocking", "Blocking", "I can't do my work", colorF(0.92f, 0.27f, 0.24f) },
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

std::string readAll(const std::string& file) {
	gchar* data = nullptr;
	gsize len = 0;
	if (!g_file_get_contents(file.c_str(), &data, &len, nullptr)) return std::string();
	std::string out(data, len);
	g_free(data);
	return out;
}

bool writeAll(const std::string& file, const std::string& data) {
	return g_file_set_contents(file.c_str(), data.data(), (gssize)data.size(), nullptr) != FALSE;
}

long long fileSize(const std::string& file) {
	GStatBuf st;
	return g_stat(file.c_str(), &st) == 0 ? (long long)st.st_size : 0;
}

std::string folder() {
	const std::string dir = std::string(g_get_user_cache_dir()) + "/CedarLogic Feedback";
	g_mkdir_with_parents(dir.c_str(), 0700);
	return dir;
}

std::string uniqueId() {
	static unsigned n = 0;
	return format("%08lx%04x", (unsigned long)(g_get_monotonic_time() / 1000), ++n & 0xFFFF);
}

struct Attachment {
	enum Kind { Screenshot, Image, Circuit } kind = Image;
	std::string file, name, type;
	GdkPixbuf* thumb = nullptr;
};

// The draft, kept in the settings between openings (and launches) until sent.
struct Draft {
	CircuitWindow* win = nullptr;
	std::string title, details, email;
	std::vector<std::string> chosen, declined;
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
			const std::string name = format("%s-%d.%s", base, n, ext);
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
		a.thumb = gdk_pixbuf_new_from_file_at_scale(file.c_str(), 264, 168, TRUE, nullptr);
		attachments.push_back(a);
	}
	void remove(size_t i) {
		if (i >= attachments.size()) return;
		g_remove(attachments[i].file.c_str());
		if (attachments[i].thumb) g_object_unref(attachments[i].thumb);
		attachments.erase(attachments.begin() + i);
	}
	void clearAttachments() { while (!attachments.empty()) remove(attachments.size() - 1); }
};

Draft& draft() {
	static Draft d;
	return d;
}

// ---- What's sent about the computer ------------------------------------------------

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
			if (c < 0x20) o += format("\\u%04x", c);
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

// A line of /etc/os-release, /proc/cpuinfo and the like: "key=value" or "key : value".
std::string fieldOf(const std::string& file, const std::string& key, char sep) {
	std::istringstream in(readAll(file));
	std::string line;
	while (std::getline(in, line)) {
		const size_t at = line.find(sep);
		if (at == std::string::npos || trimmed(line.substr(0, at)) != key) continue;
		std::string v = trimmed(line.substr(at + 1));
		if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
		return v;
	}
	return "";
}

std::string osName() {
	std::string name = fieldOf("/etc/os-release", "PRETTY_NAME", '=');
	if (name.empty()) name = "Linux";
	struct utsname u;
	if (uname(&u) == 0) name += std::string(" (") + u.release + ")";
	return name;
}

std::string archName() {
	struct utsname u;
	return uname(&u) == 0 ? u.machine : "unknown";
}

struct Device { std::string app, system, context, line; };

Device device(CircuitWindow* win) {
	Device d;
	const std::string os = osName();
	Json app;
	app.text("name", "CedarLogic for Linux").text("version", CL_VERSION).text("build", CL_GIT_COMMIT).text("commit", CL_GIT_COMMIT)
		.text("channel", "Native testing");
	d.app = app.str();
	std::string model = trimmed(readAll("/sys/devices/virtual/dmi/id/product_name"));
	const std::string maker = trimmed(readAll("/sys/devices/virtual/dmi/id/sys_vendor"));
	if (model.empty()) model = trimmed(readAll("/proc/device-tree/model"));   // a Raspberry Pi
	if (!model.empty() && model.back() == '\0') model.pop_back();
	if (!maker.empty() && model.find(maker) != 0) model = maker + " " + model;
	model = trimmed(model);
	const std::string chip = fieldOf("/proc/cpuinfo", "model name", ':');
	const long long memory = (long long)sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGE_SIZE);
	std::string list = "[";
	GdkDisplay* display = gdk_display_get_default();
	for (int i = 0; display && i < gdk_display_get_n_monitors(display); i++) {
		GdkMonitor* m = gdk_display_get_monitor(display, i);
		GdkRectangle g;
		gdk_monitor_get_geometry(m, &g);
		const int s = gdk_monitor_get_scale_factor(m);
		list += (i ? "," : "") + jsonQuote(format("%d×%d @%d%%", g.width * s, g.height * s, s * 100));
	}
	list += "]";
	const char* loc = setlocale(LC_MESSAGES, nullptr);
	const char* desktop = g_getenv("XDG_CURRENT_DESKTOP");
	const char* session = g_getenv("XDG_SESSION_TYPE");
	Json sys;
	sys.text("os", os).text("model", model.empty() ? "PC" : model).number("memoryGB", (long long)std::llround(memory / 1073741824.0))
		.number("cpus", sysconf(_SC_NPROCESSORS_ONLN)).raw("displays", list).text("locale", loc ? loc : "")
		.text("appearance", prefs().dark ? "Dark" : "Light").text("arch", archName())
		.text("desktop", std::string(desktop ? desktop : "") + (session ? std::string(" (") + session + ")" : ""));
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
		if (Canvas* c = win->currentCanvas()) ctx.text("zoom", format("%d%%", c->zoomPercent()));
	}
	d.context = ctx.str();
	d.line = format("version %s (%.7s), %s, %s", CL_VERSION, CL_GIT_COMMIT, os.substr(0, os.find(" (")).c_str(),
	                model.empty() ? "PC" : model.c_str());
	return d;
}

// ---- Talking to the site -------------------------------------------------------------

std::string envOr(const char* name, const char* fallback) {
	const char* v = g_getenv(name);
	return v && *v ? v : fallback;
}

struct Reply { int status = 0; std::string body; bool reached = false; };

// One request, through curl: the body from a file, the answer into another.
Reply request(const std::string& method, const std::string& pathAndQuery, const std::string& type, const std::string& token,
              const std::string& body) {
	Reply r;
	gchar* curl = g_find_program_in_path("curl");
	if (curl == nullptr) return r;
	const std::string base = folder() + "/" + uniqueId();
	const std::string in = base + ".body", out = base + ".reply";
	writeAll(in, body);
	std::vector<std::string> args = { curl, "-sS", "-X", method, "--max-time", "120", "--connect-timeout", "20",
	                                  "-H", "content-type: " + type, "-H", "x-cedarlogic-key: " + envOr("CL_FEEDBACK_KEY", CL_FEEDBACK_KEY),
	                                  "-A", "CedarLogic", "-o", out, "-w", "%{http_code}" };
	if (!token.empty()) { args.push_back("-H"); args.push_back("x-upload-token: " + token); }
	if (!body.empty()) { args.push_back("--data-binary"); args.push_back("@" + in); }
	args.push_back(envOr("CL_FEEDBACK_URL", CL_FEEDBACK_URL) + pathAndQuery);
	g_free(curl);
	std::vector<gchar*> argv;
	for (std::string& a : args) argv.push_back(&a[0]);
	argv.push_back(nullptr);
	gchar* stdoutText = nullptr;
	gint exit = 0;
	if (g_spawn_sync(nullptr, argv.data(), nullptr, G_SPAWN_STDERR_TO_DEV_NULL, nullptr, nullptr, &stdoutText, nullptr, &exit, nullptr)) {
		const int code = stdoutText ? atoi(stdoutText) : 0;
		if (code > 0) {
			r.reached = true;
			r.status = code;
			r.body = readAll(out);
		}
	}
	g_free(stdoutText);
	g_remove(in.c_str());
	g_remove(out.c_str());
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
		else o += format("%%%02X", c);
	}
	return o;
}

struct Sending {
	std::mutex lock;
	std::string note = "Sending…";
	std::atomic<int> state{ 0 };   // 0 going, 1 sent, 2 failed
	std::string failure;
	void set(const std::string& n) {
		std::lock_guard<std::mutex> g(lock);
		note = n;
	}
};

// Tried again a few times: a flaky connection shouldn't lose a report.
Reply perform(const std::string& method, const std::string& path, const std::string& type, const std::string& token, const std::string& body,
              std::string& failure) {
	Reply r;
	for (int attempt = 0; attempt < 3; attempt++) {
		r = request(method, path, type, token, body);
		if (r.reached && r.status >= 200 && r.status < 300) return r;
		if (r.reached) {
			const std::string why = jsonField(r.body, "error");
			failure = why.empty() ? format("The feedback server said no (%d).", r.status) : why;
			if (r.status < 500 && r.status != 429) return r;
		} else {
			failure = "Couldn't reach the feedback server. Check the internet connection (and that curl is installed) and try again.";
		}
		g_usleep((gulong)(700000 * std::pow(2, attempt)));
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
			const std::string q = "/api/feedback/upload?id=" + percent(id) + "&file=" + percent(a.name) + format("&index=%lld&total=%lld", i, pieces);
			const Reply r = perform("PUT", q, "application/octet-stream", token, part, failure);
			if (r.status < 200 || r.status >= 300) { fail(failure); return; }
			done += (long long)part.size();
			s->set(format("Sending %s… %.1f of %.1f MB", a.name.c_str(), done / 1e6, total / 1e6));
		}
	}
	const Reply r = perform("POST", "/api/feedback/complete?id=" + percent(id), "application/json", token, "", failure);
	if (r.status < 200 || r.status >= 300) { fail(failure); return; }
	s->state = 1;
}

// ---- The window ----------------------------------------------------------------------

struct Look {
	bool dark;
	Color ink, dim, card, line, accent;
};
Look look() {
	Look l;
	l.dark = prefs().dark;
	l.ink = l.dark ? colorF(0.93f, 0.93f, 0.93f) : colorF(0.1f, 0.1f, 0.1f);
	l.dim = l.dark ? colorF(0.62f, 0.62f, 0.62f) : colorF(0.42f, 0.42f, 0.42f);
	l.card = l.dark ? colorF(1, 1, 1, 0.045f) : colorF(1, 1, 1);
	l.line = l.dark ? colorF(1, 1, 1, 0.08f) : colorF(0, 0, 0, 0.09f);
	l.accent = chrome().accent();
	return l;
}

const float kChipH = 26, kChipGap = 7;

std::vector<RectF> chipRects(float width) {
	std::vector<RectF> out;
	float x = 0, y = 0;
	for (const char* t : kTags) {
		const float w = textWidth(t, 12, true) + 22 + (draft().isSuggested(t) ? 13 : 0);
		if (x > 0 && x + w > width) { x = 0; y += kChipH + kChipGap; }
		out.push_back(rectF(x, y, x + w, y + kChipH));
		x += w + kChipGap;
	}
	return out;
}

void paintTags(cairo_t* cr, float width) {
	const Look l = look();
	const std::vector<RectF> r = chipRects(width);
	for (int i = 0; i < kTagCount; i++) {
		const bool on = draft().isOn(kTags[i]), suggested = draft().isSuggested(kTags[i]);
		const RectF c = rectF(r[i].left + 0.5f, r[i].top + 0.5f, r[i].right - 0.5f, r[i].bottom - 0.5f);
		fillRound(cr, c, kChipH / 2, on ? withAlpha(l.accent, l.dark ? 0.2f : 0.12f) : l.card);
		const Color edge = on ? withAlpha(l.accent, 0.55f) : (suggested ? withAlpha(l.ink, 0.35f) : l.line);
		cairo_save(cr);
		if (suggested && !on) {
			const double dashes[] = { 3, 2 };
			cairo_set_dash(cr, dashes, 2, 0);
		}
		roundedPath(cr, c, kChipH / 2);
		setColor(cr, edge);
		cairo_set_line_width(cr, 1);
		cairo_stroke(cr);
		cairo_restore(cr);
		float x = r[i].left + 11;
		if (suggested) {
			drawTextMid(cr, "✦", rectF(x, r[i].top, x + 12, r[i].bottom), 9, l.accent);
			x += 13;
		}
		drawTextMid(cr, kTags[i], rectF(x, r[i].top, r[i].right, r[i].bottom), 12, on ? l.accent : withAlpha(l.ink, 0.75f), TextAlign::Leading, on);
	}
}

RectF pillRect(int i, float width) {
	const float gap = 8, w = (width - 3 * gap) / 4;
	return rectF(i * (w + gap), 0, i * (w + gap) + w, 32);
}

void paintPriority(cairo_t* cr, float width) {
	const Look l = look();
	for (int i = 0; i < 4; i++) {
		const Priority& p = kPriorities[i];
		const bool on = draft().priority == i;
		const RectF r = pillRect(i, width);
		fillRound(cr, r, 9, on ? withAlpha(p.color, l.dark ? 0.22f : 0.14f) : l.card);
		strokeRound(cr, rectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f), 9, on ? withAlpha(p.color, 0.7f) : l.line,
		            on ? 1.5f : 1);
		const float tw = textWidth(p.name, 12.5f, on) + 14;
		const float x = (r.left + r.right - tw) / 2;
		fillCircle(cr, pointF(x + 4, 16), 4, p.color);
		drawTextMid(cr, p.name, rectF(x + 14, 0, r.right, 32), 12.5f, on ? l.ink : withAlpha(l.ink, 0.7f), TextAlign::Leading, on);
	}
	drawTextMid(cr, kPriorities[draft().priority].hint, rectF(0, 38, width, 54), 11.5f, l.dim);
}

const float kTileW = 116, kTileH = 72, kThumbW = 112;

RectF tileRect(int i) { return rectF(i * (kTileW + 10), 6, i * (kTileW + 10) + kTileW, 6 + kTileH); }
RectF thumbRect(size_t i) {
	const float x0 = 2 * (kTileW + 10) + 8;
	return rectF(x0 + i * (kThumbW + 12), 6, x0 + i * (kThumbW + 12) + kThumbW, 6 + kTileH);
}
RectF closeRect(size_t i) {
	const RectF t = thumbRect(i);
	return rectF(t.right - 12, t.top - 6, t.right + 6, t.top + 12);
}

void paintAttachments(cairo_t* cr, float) {
	const Look l = look();
	const bool more = draft().imageCount() < kMaxImages;
	const char* icons[] = { "camera-photo-symbolic", "image-x-generic-symbolic" };
	const char* labels[] = { "Screenshot", "Add image…" };
	for (int i = 0; i < 2; i++) {
		const RectF r = tileRect(i);
		const float a = more ? 1 : 0.45f;
		fillRound(cr, r, 11, l.card);
		strokeRound(cr, rectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f), 11, l.line);
		drawIcon(cr, icons[i], rectF(r.left, r.top + 12, r.right, r.top + 38), 19, withAlpha(l.accent, a));
		drawTextMid(cr, labels[i], rectF(r.left, r.top + 44, r.right, r.bottom - 6), 12, withAlpha(l.ink, a), TextAlign::Center);
	}
	for (size_t i = 0; i < draft().attachments.size(); i++) {
		const Attachment& a = draft().attachments[i];
		const RectF r = thumbRect(i);
		fillRound(cr, r, 9, colorF(0, 0, 0, 0.3f));
		if (a.thumb) {
			const float sw = gdk_pixbuf_get_width(a.thumb), sh = gdk_pixbuf_get_height(a.thumb);
			const float k = std::max(kThumbW / sw, kTileH / sh);
			cairo_save(cr);
			roundedPath(cr, r, 9);
			cairo_clip(cr);
			cairo_translate(cr, (r.left + r.right - sw * k) / 2, (r.top + r.bottom - sh * k) / 2);
			cairo_scale(cr, k, k);
			gdk_cairo_set_source_pixbuf(cr, a.thumb, 0, 0);
			cairo_paint(cr);
			cairo_restore(cr);
		}
		strokeRound(cr, rectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f), 9, l.line);
		const RectF c = closeRect(i);
		fillCircle(cr, pointF((c.left + c.right) / 2, (c.top + c.bottom) / 2), 8.5f, colorF(0, 0, 0, 0.65f));
		drawIcon(cr, Icon::Dismiss, c, 8, colorF(1, 1, 1));
	}
}

// The band across the top, in the icon's colours.
void paintBand(cairo_t* cr, float w, float h) {
	cairo_pattern_t* g = cairo_pattern_create_linear(0, 0, w, h);
	cairo_pattern_add_color_stop_rgb(g, 0, 0.035, 0.063, 0.047);
	cairo_pattern_add_color_stop_rgb(g, 1, 0.06, 0.12, 0.08);
	roundedPath(cr, rectF(0, 0, w, h), 10);
	cairo_set_source(cr, g);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	for (int i = 0; i < 4; i++) {
		const float y = 16 + i * 15.0f, x1 = w * 0.66f + i * 14, x2 = w * 0.70f + i * 14;
		drawLine(cr, pointF(w * 0.55f, y), pointF(x1, y), brand::alpha(brand::kNeon, 0.10f), 1.2f);
		drawLine(cr, pointF(x1, y), pointF(x2, y + 10), brand::alpha(brand::kNeon, 0.18f), 1.2f);
		drawLine(cr, pointF(x2, y + 10), pointF(w, y + 10), brand::alpha(brand::kNeon, 0.18f), 1.2f);
	}
	drawTextMid(cr, "Send Feedback", rectF(20, 16, w, 44), 20, colorF(1, 1, 1), TextAlign::Leading, true);
	const float bx = 20 + textWidth("Send Feedback", 20, true) + 10;
	fillRound(cr, rectF(bx, 23, bx + 40, 39), 8, brand::kNeon);
	drawTextMid(cr, "BETA", rectF(bx, 23, bx + 40, 39), 9.5f, brand::kInkDeep, TextAlign::Center, true);
	drawWrapped(cr, "What's working, what's broken, what you'd love to see. It goes straight to the developer.", rectF(20, 50, w - 16, h), 12,
	            brand::kDim);
}

// ---- The dialog's parts ----

struct Form {
	GtkWidget* dialog = nullptr;
	GtkWidget* title = nullptr;
	GtkWidget* details = nullptr;
	GtkWidget* tagsHeading = nullptr;
	GtkWidget* tags = nullptr;
	GtkWidget* priority = nullptr;
	GtkWidget* shots = nullptr;
	GtkWidget* circuit = nullptr;
	GtkWidget* name = nullptr;
	GtkWidget* email = nullptr;
	GtkWidget* contact = nullptr;
	GtkWidget* problem = nullptr;
	GtkWidget* send = nullptr;
	GtkWidget* close = nullptr;
	Device dev;
	std::shared_ptr<Sending> sending;
	guint timer = 0;
	bool sent = false;
};

std::string textOf(GtkWidget* view) {
	GtkTextBuffer* b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
	GtkTextIter s, e;
	gtk_text_buffer_get_bounds(b, &s, &e);
	gchar* t = gtk_text_buffer_get_text(b, &s, &e, FALSE);
	std::string out = t ? t : "";
	g_free(t);
	return out;
}

void collect(Form* f) {
	Draft& d = draft();
	d.title = gtk_entry_get_text(GTK_ENTRY(f->title));
	d.details = textOf(f->details);
	d.email = gtk_entry_get_text(GTK_ENTRY(f->email));
	d.contactOK = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(f->contact));
	d.includeCircuit = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(f->circuit));
	prefs().studentName = trimmed(gtk_entry_get_text(GTK_ENTRY(f->name)));
	d.save();
}

void wordsChanged(Form* f) {
	Draft& d = draft();
	d.title = gtk_entry_get_text(GTK_ENTRY(f->title));
	d.details = textOf(f->details);
	gtk_label_set_text(GTK_LABEL(f->tagsHeading), d.suggested().empty() ? "TAGS" : "TAGS  ·  ✦ suggested from what you wrote");
	gtk_widget_queue_draw(f->tags);
	gtk_label_set_text(GTK_LABEL(f->problem), "");
}

void emailChanged(Form* f) { gtk_widget_set_sensitive(f->contact, !trimmed(gtk_entry_get_text(GTK_ENTRY(f->email))).empty()); }

GtkWidget* picture(int height, void (*paint)(cairo_t*, float)) {
	GtkWidget* a = gtk_drawing_area_new();
	gtk_widget_set_size_request(a, -1, height);
	gtk_widget_add_events(a, GDK_BUTTON_PRESS_MASK);
	g_signal_connect(a, "draw", G_CALLBACK(+[](GtkWidget* w, cairo_t* cr, gpointer p) -> gboolean {
		guarded("Send Feedback", [&] { reinterpret_cast<void (*)(cairo_t*, float)>(p)(cr, (float)gtk_widget_get_allocated_width(w)); });
		return TRUE;
	}), (gpointer)paint);
	return a;
}

GtkWidget* heading(const char* text) {
	GtkWidget* l = gtk_label_new(text);
	gtk_label_set_xalign(GTK_LABEL(l), 0);
	gtk_widget_set_margin_top(l, 8);
	PangoAttrList* a = pango_attr_list_new();
	pango_attr_list_insert(a, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
	pango_attr_list_insert(a, pango_attr_scale_new(0.82));
	pango_attr_list_insert(a, pango_attr_letter_spacing_new(1 * PANGO_SCALE));
	gtk_label_set_attributes(GTK_LABEL(l), a);
	pango_attr_list_unref(a);
	gtk_style_context_add_class(gtk_widget_get_style_context(l), "dim-label");
	return l;
}

void attachScreenshot(Form* f) {
	Draft& d = draft();
	if (d.imageCount() >= kMaxImages || d.win == nullptr) { gtk_widget_error_bell(f->dialog); return; }
	const std::string name = d.freeName("screenshot", "png");
	const std::string file = folder() + "/" + uniqueId() + "-" + name;
	// The circuit window, drawn as it looks now.
	GtkWidget* w = GTK_WIDGET(d.win->window());
	const int width = gtk_widget_get_allocated_width(w), height = gtk_widget_get_allocated_height(w);
	const int scale = std::max(1, gtk_widget_get_scale_factor(w));
	cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width * scale, height * scale);
	cairo_surface_set_device_scale(s, scale, scale);
	cairo_t* cr = cairo_create(s);
	gtk_widget_draw(w, cr);
	cairo_destroy(cr);
	const bool ok = cairo_surface_write_to_png(s, file.c_str()) == CAIRO_STATUS_SUCCESS;
	cairo_surface_destroy(s);
	if (ok) d.add(Attachment::Screenshot, file, name, "image/png");
	else gtk_widget_error_bell(f->dialog);
}

void attachImages(Form* f) {
	Draft& d = draft();
	GtkFileChooserNative* c = gtk_file_chooser_native_new("Pictures to Go With Your Feedback", GTK_WINDOW(f->dialog),
	                                                      GTK_FILE_CHOOSER_ACTION_OPEN, "_Add", "_Cancel");
	gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(c), TRUE);
	GtkFileFilter* filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, "Pictures (PNG, JPEG)");
	gtk_file_filter_add_mime_type(filter, "image/png");
	gtk_file_filter_add_mime_type(filter, "image/jpeg");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(c), filter);
	if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(c)) == GTK_RESPONSE_ACCEPT) {
		GSList* files = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(c));
		for (GSList* it = files; it; it = it->next) {
			const std::string f2 = static_cast<gchar*>(it->data);
			g_free(it->data);
			if (d.imageCount() >= kMaxImages) continue;
			const size_t dot = f2.find_last_of('.');
			std::string ext = dot == std::string::npos ? "" : f2.substr(dot + 1);
			for (char& ch : ext) ch = (char)tolower((unsigned char)ch);
			if (ext != "png" && ext != "jpg" && ext != "jpeg") continue;
			if (fileSize(f2) <= 0 || fileSize(f2) > 12000000) continue;
			const bool png = ext == "png";
			const std::string name = d.freeName("image", png ? "png" : "jpg");
			const std::string copy = folder() + "/" + uniqueId() + "-" + name;
			if (writeAll(copy, readAll(f2))) d.add(Attachment::Image, copy, name, png ? "image/png" : "image/jpeg");
		}
		g_slist_free(files);
	}
	g_object_unref(c);
}

void startSending(Form* f) {
	if (f->sending) return;
	Draft& d = draft();
	collect(f);
	if (trimmed(d.title).empty()) {
		gtk_label_set_text(GTK_LABEL(f->problem), "Write a short summary first.");
		gtk_widget_grab_focus(f->title);
		gtk_widget_error_bell(f->dialog);
		return;
	}
	std::vector<Attachment> files = d.attachments;
	for (Attachment& a : files) a.thumb = nullptr;   // the thread needs only the files
	if (d.includeCircuit) {
		const std::string file = folder() + "/" + uniqueId() + "-circuit.cdl";
		const char* text = cl_document_save_text(d.win->document());
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
		.flag("contactOK", d.contactOK && !mail.empty()).text("platform", "linux").raw("attachments", list)
		.raw("app", f->dev.app).raw("system", f->dev.system).raw("context", f->dev.context);
	f->sending = std::make_shared<Sending>();
	std::thread(upload, f->sending, body.str(), files).detach();
	gtk_widget_set_sensitive(f->send, FALSE);
	gtk_label_set_text(GTK_LABEL(f->problem), "Sending…");
}

gboolean timerCb(gpointer data) {
	Form* f = static_cast<Form*>(data);
	if (!f->sending) return G_SOURCE_CONTINUE;
	Draft& d = draft();
	if (f->sending->state == 0) {
		std::lock_guard<std::mutex> g(f->sending->lock);
		gtk_label_set_text(GTK_LABEL(f->problem), f->sending->note.c_str());
		return G_SOURCE_CONTINUE;
	}
	if (f->sending->state == 1) {
		// Sent: the draft is cleared for next time.
		d.clearAttachments();
		d.title.clear();
		d.details.clear();
		d.chosen.clear();
		d.declined.clear();
		d.priority = 1;
		d.includeCircuit = false;
		d.save();
		f->sent = true;
		gtk_dialog_response(GTK_DIALOG(f->dialog), GTK_RESPONSE_OK);
		return G_SOURCE_CONTINUE;
	}
	std::string why;
	{
		std::lock_guard<std::mutex> g(f->sending->lock);
		why = f->sending->failure;
	}
	f->sending.reset();
	gtk_widget_set_sensitive(f->send, TRUE);
	gtk_button_set_label(GTK_BUTTON(f->send), "Try Again");
	gtk_label_set_text(GTK_LABEL(f->problem), why.c_str());
	gtk_widget_error_bell(f->dialog);
	return G_SOURCE_CONTINUE;
}

}  // namespace

int probe() {
	g_setenv("CL_FEEDBACK_KEY", "probe-not-a-key", TRUE);
	const Reply r = request("POST", "/api/feedback", "application/json", "", "{}");
	return r.reached ? r.status : 0;
}

void show(CircuitWindow* win) {
	Draft& d = draft();
	d.win = win;
	d.load();
	Form f;
	f.dev = device(win);

	f.dialog = gtk_dialog_new();
	gtk_window_set_title(GTK_WINDOW(f.dialog), "Send Feedback");
	gtk_window_set_transient_for(GTK_WINDOW(f.dialog), win->window());
	gtk_window_set_modal(GTK_WINDOW(f.dialog), TRUE);
	gtk_window_set_default_size(GTK_WINDOW(f.dialog), 620, -1);
	f.close = gtk_dialog_add_button(GTK_DIALOG(f.dialog), "_Close", GTK_RESPONSE_CLOSE);
	f.send = gtk_dialog_add_button(GTK_DIALOG(f.dialog), "Send Feedback", 100);
	gtk_style_context_add_class(gtk_widget_get_style_context(f.send), "suggested-action");
	GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(f.dialog));
	GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);
	gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 640);
	gtk_box_pack_start(GTK_BOX(content), scroll, TRUE, TRUE, 0);
	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_container_set_border_width(GTK_CONTAINER(box), 18);
	gtk_container_add(GTK_CONTAINER(scroll), box);

	GtkWidget* band = gtk_drawing_area_new();
	gtk_widget_set_size_request(band, -1, 92);
	g_signal_connect(band, "draw", G_CALLBACK(+[](GtkWidget* w, cairo_t* cr, gpointer) -> gboolean {
		paintBand(cr, (float)gtk_widget_get_allocated_width(w), (float)gtk_widget_get_allocated_height(w));
		return TRUE;
	}), nullptr);
	gtk_box_pack_start(GTK_BOX(box), band, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(box), heading("WHAT'S ON YOUR MIND?"), FALSE, FALSE, 0);
	f.title = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(f.title), "A short summary, like “C doesn't connect the wire”");
	gtk_entry_set_text(GTK_ENTRY(f.title), d.title.c_str());
	gtk_box_pack_start(GTK_BOX(box), f.title, FALSE, FALSE, 0);
	f.details = gtk_text_view_new();
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(f.details), GTK_WRAP_WORD_CHAR);
	gtk_text_view_set_left_margin(GTK_TEXT_VIEW(f.details), 8);
	gtk_text_view_set_right_margin(GTK_TEXT_VIEW(f.details), 8);
	gtk_text_view_set_top_margin(GTK_TEXT_VIEW(f.details), 6);
	gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(f.details)), d.details.c_str(), -1);
	gtk_widget_set_tooltip_text(f.details, "What happened, what you expected, and how to make it happen again (if you know).");
	gtk_widget_set_size_request(f.details, -1, 96);
	GtkWidget* frame = gtk_frame_new(nullptr);
	gtk_container_add(GTK_CONTAINER(frame), f.details);
	gtk_box_pack_start(GTK_BOX(box), frame, FALSE, FALSE, 0);

	f.tagsHeading = heading("TAGS");
	gtk_box_pack_start(GTK_BOX(box), f.tagsHeading, FALSE, FALSE, 0);
	f.tags = picture(2 * (int)kChipH + (int)kChipGap + 2, paintTags);
	gtk_box_pack_start(GTK_BOX(box), f.tags, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), heading("HOW MUCH DOES IT MATTER?"), FALSE, FALSE, 0);
	f.priority = picture(56, paintPriority);
	gtk_box_pack_start(GTK_BOX(box), f.priority, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), heading("SHOW IT  ·  only the CedarLogic window is captured"), FALSE, FALSE, 0);
	f.shots = picture(84, paintAttachments);
	gtk_box_pack_start(GTK_BOX(box), f.shots, FALSE, FALSE, 0);
	f.circuit = gtk_check_button_new_with_label("Send this circuit too (makes a problem easy to repeat; it's only sent with this)");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(f.circuit), d.includeCircuit);
	gtk_box_pack_start(GTK_BOX(box), f.circuit, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(box), heading("ABOUT YOU"), FALSE, FALSE, 0);
	GtkWidget* grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
	f.name = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(f.name), prefs().studentName.c_str());
	gtk_widget_set_hexpand(f.name, TRUE);
	f.email = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(f.email), d.email.c_str());
	gtk_entry_set_placeholder_text(GTK_ENTRY(f.email), "If you'd like a reply");
	gtk_entry_set_input_purpose(GTK_ENTRY(f.email), GTK_INPUT_PURPOSE_EMAIL);
	const char* labels[] = { "Name", "Email" };
	GtkWidget* fields[] = { f.name, f.email };
	for (int i = 0; i < 2; i++) {
		GtkWidget* l = gtk_label_new(labels[i]);
		gtk_label_set_xalign(GTK_LABEL(l), 1);
		gtk_grid_attach(GTK_GRID(grid), l, 0, i, 1, 1);
		gtk_grid_attach(GTK_GRID(grid), fields[i], 1, i, 1, 1);
	}
	gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);
	f.contact = gtk_check_button_new_with_label("You can email me about this");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(f.contact), d.contactOK);
	gtk_box_pack_start(GTK_BOX(box), f.contact, FALSE, FALSE, 0);
	GtkWidget* also = gtk_label_new(("Also sent: " + f.dev.line).c_str());
	gtk_label_set_xalign(GTK_LABEL(also), 0);
	gtk_label_set_line_wrap(GTK_LABEL(also), TRUE);
	gtk_style_context_add_class(gtk_widget_get_style_context(also), "dim-label");
	gtk_widget_set_margin_top(also, 6);
	gtk_box_pack_start(GTK_BOX(box), also, FALSE, FALSE, 0);
	f.problem = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(f.problem), 0);
	gtk_label_set_line_wrap(GTK_LABEL(f.problem), TRUE);
	gtk_box_pack_start(GTK_BOX(box), f.problem, FALSE, FALSE, 0);

	g_signal_connect_swapped(f.title, "changed", G_CALLBACK(wordsChanged), &f);
	g_signal_connect_swapped(gtk_text_view_get_buffer(GTK_TEXT_VIEW(f.details)), "changed", G_CALLBACK(wordsChanged), &f);
	g_signal_connect_swapped(f.email, "changed", G_CALLBACK(emailChanged), &f);
	g_signal_connect(f.tags, "button-press-event", G_CALLBACK(+[](GtkWidget* w, GdkEventButton* e, gpointer data) -> gboolean {
		Form* form = static_cast<Form*>(data);
		if (form->sending || e->button != 1) return TRUE;
		const std::vector<RectF> r = chipRects((float)gtk_widget_get_allocated_width(w));
		for (int i = 0; i < kTagCount; i++) if (inRect(r[i], (float)e->x, (float)e->y)) draft().toggle(kTags[i]);
		gtk_widget_queue_draw(w);
		return TRUE;
	}), &f);
	g_signal_connect(f.priority, "button-press-event", G_CALLBACK(+[](GtkWidget* w, GdkEventButton* e, gpointer data) -> gboolean {
		Form* form = static_cast<Form*>(data);
		if (form->sending || e->button != 1) return TRUE;
		for (int i = 0; i < 4; i++) if (inRect(pillRect(i, (float)gtk_widget_get_allocated_width(w)), (float)e->x, (float)e->y)) draft().priority = i;
		gtk_widget_queue_draw(w);
		return TRUE;
	}), &f);
	g_signal_connect(f.shots, "button-press-event", G_CALLBACK(+[](GtkWidget* w, GdkEventButton* e, gpointer data) -> gboolean {
		Form* form = static_cast<Form*>(data);
		if (form->sending || e->button != 1 || e->type != GDK_BUTTON_PRESS) return TRUE;
		Draft& dd = draft();
		const float x = (float)e->x, y = (float)e->y;
		guarded("Send Feedback", [&] {
			for (size_t i = 0; i < dd.attachments.size(); i++)
				if (inRect(closeRect(i), x, y)) { dd.remove(i); return; }
			for (size_t i = 0; i < dd.attachments.size(); i++)
				if (inRect(thumbRect(i), x, y)) {
					gchar* uri = g_filename_to_uri(dd.attachments[i].file.c_str(), nullptr, nullptr);
					if (uri) { openExternally(GTK_WINDOW(form->dialog), uri); g_free(uri); }
					return;
				}
			if (inRect(tileRect(0), x, y)) attachScreenshot(form);
			else if (inRect(tileRect(1), x, y)) attachImages(form);
		});
		gtk_widget_queue_draw(w);
		return TRUE;
	}), &f);

	wordsChanged(&f);
	emailChanged(&f);
	f.timer = g_timeout_add(120, timerCb, &f);
	gtk_widget_show_all(f.dialog);
	gtk_widget_grab_focus(f.title);
	for (;;) {
		const int r = gtk_dialog_run(GTK_DIALOG(f.dialog));
		if (r == 100) { guarded("Send Feedback", [&] { startSending(&f); }); continue; }
		if (r == GTK_RESPONSE_OK) break;
		// Closing while it sends: it carries on sending in the background.
		break;
	}
	g_source_remove(f.timer);
	if (!f.sent && !f.sending) collect(&f);   // kept as a draft for next time
	gtk_widget_destroy(f.dialog);
	if (f.sent) win->note("Feedback sent. Thank you!");
	else if (f.sending) win->note("Still sending your feedback in the background.");
}

}  // namespace feedback
