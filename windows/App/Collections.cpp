// Templates and My Parts (see Collections.h).

#include "Collections.h"
#include "Alert.h"
#include "Dialogs.h"
#include "Picker.h"
#include "Shortcuts.h"
#include "Window.h"

#include <shellapi.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>

namespace {

std::string readFile(const std::string& path) {
	std::ifstream in(W(path).c_str(), std::ios::binary);
	std::ostringstream s;
	s << in.rdbuf();
	return s.str();
}

bool writeFile(const std::string& path, const std::string& text) {
	HANDLE h = CreateFileW(W(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	DWORD wrote = 0;
	const bool ok = WriteFile(h, text.data(), (DWORD)text.size(), &wrote, nullptr) && wrote == text.size();
	CloseHandle(h);
	return ok;
}

std::string trim(const std::string& s) {
	const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
	return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// A new folder in `in`, made here (so no two saves ever share one; the
// number is this process's own); "" when it couldn't be.
std::string newFolder(const std::string& in) {
	static unsigned next = (unsigned)GetCurrentProcessId() * 2654435761u + (unsigned)GetTickCount();
	CreateDirectoryW(W(in).c_str(), nullptr);
	for (int tries = 0; tries <= 100; tries++) {
		const time_t t = time(nullptr);
		struct tm lt;
		localtime_s(&lt, &t);
		char buf[32];
		strftime(buf, sizeof buf, "%Y%m%d-%H%M%S", &lt);
		const std::string folder = in + "\\" + buf + strf("-%d", 1000 + (int)(next++ % 99000));
		if (CreateDirectoryW(W(folder).c_str(), nullptr)) return folder;
		if (GetLastError() != ERROR_ALREADY_EXISTS) break;
	}
	return std::string();
}

// Into the Recycle Bin, so a mistake can be taken back.
bool recycle(const std::string& folder) {
	std::wstring from = W(folder);
	from.push_back(L'\0');   // the list ends with two
	SHFILEOPSTRUCTW op = {};
	op.wFunc = FO_DELETE;
	op.pFrom = from.c_str();
	op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
	return SHFileOperationW(&op) == 0;
}

std::vector<std::string> folders(const std::string& root) {
	std::vector<std::string> out;
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW(W(root + "\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return out;
	do {
		const std::string id = U(fd.cFileName);
		if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !id.empty() && id[0] != '.') out.push_back(id);
	} while (FindNextFileW(h, &fd));
	FindClose(h);
	return out;
}

bool alive(CircuitWindow* w) { return std::find(circuitWindows().begin(), circuitWindows().end(), w) != circuitWindows().end(); }

}  // namespace

// ---- Templates -------------------------------------------------------------------

namespace templates {

namespace {

struct Template {
	std::string id, name, detail, text, folder;   // folder: "" for a built-in one
	std::string group = "Built In";               // the picker's heading for a built-in one
	// A flip-flop's or register's text with a running clock (`text` has one
	// that moves only on Step Clock) and with a pulse button in its place:
	// the picker's Clock line chooses. Empty: no choice.
	std::string runningText, pulseText;
	bool hasClockChoice() const { return !runningText.empty() && !pulseText.empty(); }
	const std::string& withClock(int clock) const {
		if (clock == 1 && !runningText.empty()) return runningText;
		if (clock == 2 && !pulseText.empty()) return pulseText;
		return text;
	}
};

std::string root() { return settingsDir() + "\\Templates"; }

std::vector<Template> yours() {
	std::vector<std::pair<Template, double>> found;
	for (const std::string& id : folders(root())) {
		Template t;
		t.id = id;
		t.folder = root() + "\\" + id;
		const std::string file = t.folder + "\\template.cdl";
		t.text = readFile(file);
		if (t.text.empty()) continue;
		t.name = trim(readFile(t.folder + "\\name.txt"));
		if (t.name.empty()) t.name = "Untitled Template";
		WIN32_FILE_ATTRIBUTE_DATA a;
		double when = 0;
		if (GetFileAttributesExW(W(file).c_str(), GetFileExInfoStandard, &a)) {
			ULARGE_INTEGER u;
			u.LowPart = a.ftLastWriteTime.dwLowDateTime;
			u.HighPart = a.ftLastWriteTime.dwHighDateTime;
			when = (double)(u.QuadPart - 116444736000000000ULL) / 1e7;
		}
		t.detail = "Yours";
		found.push_back({ t, when });
	}
	std::sort(found.begin(), found.end(), [](auto& a, auto& b) { return a.second > b.second; });
	std::vector<Template> out;
	for (auto& f : found) out.push_back(f.first);
	return out;
}

// Parts and wires built on a new circuit, by the engine, so the built-in
// templates are always in the current format.
struct Part { std::string gate; double x, y; std::string label; };
struct Wire { int from; std::string fromPin; int to; std::string toPin; };

std::string build(const std::vector<Part>& parts, const std::vector<Wire>& wires, const std::map<std::string, double>& big) {
	CLDocument* doc = cl_document_new();
	std::vector<CLBuildGate> gates;
	for (const Part& p : parts) gates.push_back({ p.gate.c_str(), p.x, p.y, p.label.empty() ? nullptr : p.label.c_str() });
	std::vector<CLBuildWire> links;
	for (const Wire& w : wires) links.push_back({ w.from, w.fromPin.c_str(), w.to, w.toPin.c_str() });
	cl_edit_build(doc, 0, gates.data(), (int)gates.size(), links.empty() ? nullptr : links.data(), (int)links.size(), "Template");
	cl_edit_select_none(doc, 0);
	std::string text = cl_document_save_text(doc);
	cl_document_close(doc);
	// Bigger text for the titles.
	for (const auto& b : big) {
		const std::string key = "(gparam \"LABEL_TEXT\" \"" + b.first + "\")";
		const size_t at = text.find(key);
		if (at == std::string::npos) continue;
		const std::string h = "(gparam \"TEXT_HEIGHT\" \"";
		const size_t hAt = text.find(h, at);
		if (hAt == std::string::npos || hAt - at > 200) continue;
		const size_t start = hAt + h.size(), end = text.find('"', start);
		if (end != std::string::npos) text.replace(start, end - start, strf("%g", b.second));
	}
	return text;
}

// A label whose left edge is at x (labels are placed by their middle).
Part label(const std::string& text, double x, double y, double height = 2) {
	return Part{ "AA_LABEL", x + text.size() * 0.3 * height, y, text };
}

std::string labPage() {
	const std::string name = trim(prefs().studentName);
	return build({ label("Lab 1: Title", -3.5, 12, 4),
	               label(name.empty() ? "Name: ______________________" : "Name: " + name, 0, 5),
	               label("Date: ____________", 44, 5), label("Course: ____________", -1.6, 1),
	               label("Inputs", -1.4, -8), label("Outputs", 60, -8) },
	             {}, { { "Lab 1: Title", 4 } });
}

// The Step Clock key the flip-flop and mistake templates name ("The K key
// steps the clock"), as it is now: another key, or the menu when it has none.
std::string withStepClockKey(std::string text) {
	const shortcuts::Action* a = shortcuts::find("stepClock");
	const shortcuts::Key k = a ? shortcuts::first(*a) : shortcuts::Key();
	if (k.valid() && shortcuts::label(k) == "K") return text;
	for (const char* from : { "The K key", "the K key" }) {
		const bool capital = from[0] == 'T';
		const std::string to = k.valid() ? std::string(capital ? "The " : "the ") + shortcuts::label(k) + " key"
		                                 : std::string("Simulate ▸ Step Clock");
		for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
			text.replace(at, strlen(from), to);
	}
	return text;
}

// The Mac's built-in templates, as files in res\templates (made from the Mac
// app's by windows/make-templates.py): templates.txt lists each one's id,
// group, name, line and whether it has a Clock choice. Read once.
const std::vector<Template>& shipped() {
	static std::vector<Template> list;
	static bool read = false;
	if (read) return list;
	read = true;
	const std::string dir = resourcesDir().empty() ? std::string() : resourcesDir() + "\\templates";
	std::istringstream lines(readFile(dir + "\\templates.txt"));
	std::string line;
	while (std::getline(lines, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.empty() || line[0] == '#') continue;
		std::vector<std::string> f;
		for (size_t at = 0;;) {
			const size_t tab = line.find('\t', at);
			f.push_back(line.substr(at, tab == std::string::npos ? std::string::npos : tab - at));
			if (tab == std::string::npos) break;
			at = tab + 1;
		}
		if (f.size() < 4) continue;
		Template t;
		t.id = f[0];
		t.group = f[1];
		t.name = f[2];
		t.detail = f[3];
		t.text = readFile(dir + "\\" + t.id + ".cdl");
		if (t.text.empty()) continue;
		if (f.size() > 4 && f[4] == "clock") {
			t.runningText = readFile(dir + "\\" + t.id + "-running.cdl");
			t.pulseText = readFile(dir + "\\" + t.id + "-pulse.cdl");
		}
		list.push_back(t);
	}
	return list;
}

// The Lab Page (built here: it carries your name), then the shipped ones.
std::vector<Template> builtIn() {
	std::vector<Template> out = {
		{ "builtin-lab", "Lab Page", "A title, your name, the date and the course, with room for inputs and outputs", labPage(), "" },
	};
	for (Template t : shipped()) {
		t.text = withStepClockKey(t.text);
		if (!t.runningText.empty()) t.runningText = withStepClockKey(t.runningText);
		if (!t.pulseText.empty()) t.pulseText = withStepClockKey(t.pulseText);
		out.push_back(t);
	}
	return out;
}

// The group of circuits with a mistake to find: offered in New from
// Template, but never as the start of every new circuit.
const char* const kMistakesGroup = "Classic Mistakes";

// The flip-flops' and registers' Clock line, as the Mac's picker has it
// (kept in prefs().templateClock): 0 manual (it moves only on Step Clock),
// 1 running, 2 a pulse button.
const char* const kClockChoices[] = { "Manual", "Running", "Pulse button" };

std::string stepClockName() {
	const shortcuts::Action* a = shortcuts::find("stepClock");
	const shortcuts::Key k = a ? shortcuts::first(*a) : shortcuts::Key();
	return k.valid() ? "the " + shortcuts::label(k) + " key" : "Simulate ▸ Step Clock";
}

std::string clockNote(int clock) {
	switch (clock) {
	case 1: return "It ticks by itself.";
	case 2: return "Click the Clock button on the circuit.";
	default: return "It moves only on Step Clock (" + stepClockName() + ").";
	}
}

}  // namespace

namespace {
std::string preselected;
}  // namespace

void preselect(const std::string& id) { preselected = id; }

void showPicker(CircuitWindow* from) {
	if (from == nullptr) return;
	const std::vector<Template> built = builtIn();
	std::vector<Template> mine = yours();
	auto all = [&] { std::vector<Template> a = built; a.insert(a.end(), mine.begin(), mine.end()); return a; };
	picker::Picker p;
	p.title = "New from Template";
	p.line = "Start a circuit from one of these. Save your own with File > Save as Template.";
	p.search = false;
	p.width = 940;
	p.height = 620;
	p.listWidth = 340;
	p.listOnLeft = true;
	p.leftButtons = { "Rename…", "Delete…" };
	p.rightButtons = { "Cancel", "Use Template" };
	p.rows = [&](const std::string&) {
		std::vector<picker::Row> out;
		picker::Row h;
		h.heading = true;
		// The built-in ones under their groups' headings, in the Mac's order.
		std::vector<std::string> groups;
		for (const Template& t : built)
			if (std::find(groups.begin(), groups.end(), t.group) == groups.end()) groups.push_back(t.group);
		for (const std::string& g : groups) {
			h.title = g;
			out.push_back(h);
			for (const Template& t : built) {
				if (t.group != g) continue;
				picker::Row r; r.id = t.id; r.title = t.name; r.subtitle = t.detail; r.tile = false; out.push_back(r);
			}
		}
		h.title = "Yours";
		h.subtitle = mine.empty() ? "None yet." : "";
		out.push_back(h);
		for (const Template& t : mine) { picker::Row r; r.id = t.id; r.title = t.name; r.subtitle = t.detail; r.tile = false; out.push_back(r); }
		return out;
	};
	CLDocument* shown = nullptr;
	std::string shownId;
	int clock = std::max(0, std::min(2, prefs().templateClock));
	int shownClock = clock;
	auto selectedTemplate = [&](Template& out) {
		const picker::Row* r = p.selected();
		if (r == nullptr) return false;
		for (const Template& t : all()) if (t.id == r->id) { out = t; return true; }
		return false;
	};
	p.onSelect = [&](picker::Picker&) {
		Template t;
		const bool has = selectedTemplate(t);
		if (has && t.id == shownId && clock == shownClock) return;
		if (shown) cl_document_close(shown);
		shown = nullptr;
		shownId = has ? t.id : std::string();
		shownClock = clock;
		if (has) {
			char err[256];
			const std::string& text = t.withClock(clock);
			shown = cl_document_open_text(text.c_str(), (long)text.size(), err, sizeof err);
		}
	};
	p.preview = [&](ID2D1RenderTarget* rt, const D2D1_RECT_F& box) {
		D2D1_MATRIX_3X2_F t;
		rt->GetTransform(&t);
		rt->SetTransform(D2D1::Matrix3x2F::Translation(box.left, box.top) * t);
		const bool drawn = shown && cl_document_draw_fitted(shown, 0, rt, box.right - box.left, box.bottom - box.top, 16, t._11,
		                                                    prefs().dark ? CL_STYLE_DARK : CL_STYLE_LIGHT);
		rt->SetTransform(t);
		if (!drawn) drawText(rt, "An empty page", box, 12, picker::Look{ prefs().dark }.ink(0.5f), TextAlign::Center);
	};
	// The Clock line (a flip-flop or a register selected): Manual, Running or
	// Pulse button, and what that means, as the Mac's picker has it.
	p.footerHeight = 34;
	std::vector<D2D1_RECT_F> clockSegments;
	p.footer = [&](ID2D1RenderTarget* rt, const D2D1_RECT_F& box) {
		clockSegments.clear();
		Template t;
		if (!selectedTemplate(t) || !t.hasClockChoice()) return;
		const picker::Look look{ prefs().dark };
		const float cy = (box.top + box.bottom) / 2;
		drawText(rt, "Clock", D2D1::RectF(box.left, cy - 10, box.left + 50, cy + 10), 13, look.ink(), TextAlign::Leading, true);
		float x = box.left + 50;
		const D2D1_RECT_F all = D2D1::RectF(x, cy - 13, x + 3 * 96, cy + 13);
		fillRound(rt, all, 7, look.ink(0.07f));
		for (int i = 0; i < 3; i++) {
			const D2D1_RECT_F r = D2D1::RectF(x + 2, cy - 11, x + 94, cy + 11);
			if (i == clock) {
				fillRound(rt, r, 6, look.sheet());
				strokeRound(rt, r, 6, look.ink(0.12f));
			}
			drawText(rt, kClockChoices[i], r, 12, look.ink(i == clock ? 1.0f : 0.75f), TextAlign::Center, i == clock);
			clockSegments.push_back(r);
			x += 96;
		}
		drawText(rt, clockNote(clock), D2D1::RectF(x + 12, cy - 10, box.right, cy + 10), 12, look.ink(0.55f));
	};
	p.onFooterClick = [&](picker::Picker& pk, float x, float y) {
		for (int i = 0; i < (int)clockSegments.size(); i++) {
			if (!inRect(clockSegments[i], x, y) || i == clock) continue;
			clock = i;
			prefs().templateClock = i;
			prefs().save();
			if (pk.onSelect) pk.onSelect(pk);   // the preview, with that clock
			pk.redraw();
		}
	};
	std::string useText, useName;
	p.onButton = [&](picker::Picker& pk, int b) -> bool {
		Template t;
		if (b == 100) return true;
		if (!selectedTemplate(t)) return false;
		if (b == 101) { useText = t.withClock(clock); useName = t.name; return true; }
		if (t.folder.empty()) {
			showMessage(pk.hwnd, Tone::Info, "That one's built in", "Only your own templates can be renamed or deleted.");
			return false;
		}
		if (b == 0) {
			std::string name = t.name;
			if (askText(pk.hwnd, "Rename Template", "The template's name:", name) && !name.empty()) {
				writeFile(t.folder + "\\name.txt", name);
				mine = yours();
				pk.reload();
			}
		} else if (b == 1) {
			if (askConfirm(pk.hwnd, "Delete “" + t.name + "”?", "It goes to the Recycle Bin. Circuits you made from it aren't touched.",
			               "Delete", "Cancel", true)) {
				recycle(t.folder);
				mine = yours();
				pk.reload();
			}
		}
		return false;
	};
	if (!preselected.empty()) {
		p.shown = p.rows("");
		for (int i = 0; i < (int)p.shown.size(); i++) if (p.shown[i].id == preselected) p.selection = i;
	}
	p.run(from->window());
	if (shown) cl_document_close(shown);
	if (useText.empty()) return;
	char err[512] = "";
	CLDocument* doc = cl_document_open_text(useText.c_str(), (long)useText.size(), err, sizeof err);
	if (doc == nullptr) { showMessage(alive(from) ? from->window() : nullptr, Tone::Error, "That template couldn't be opened", err); return; }
	CircuitWindow* w = nullptr;
	if (alive(from) && from->isPristine()) { from->replaceDocument(doc, ""); w = from; }
	else w = new CircuitWindow(doc, "");
	w->startAs(useName);
}

std::vector<std::pair<std::string, std::string>> list() {
	std::vector<std::pair<std::string, std::string>> out;
	for (const Template& t : builtIn())
		if (t.group != kMistakesGroup) out.push_back({ t.id, t.name });
	for (const Template& t : yours()) out.push_back({ t.id, t.name });
	return out;
}

bool startFrom(const std::string& id, CircuitWindow* from, bool replace) {
	std::vector<Template> all = builtIn();
	for (const Template& t : yours()) all.push_back(t);
	for (const Template& t : all) {
		if (t.id != id) continue;
		char err[512] = "";
		CLDocument* doc = cl_document_open_text(t.text.c_str(), (long)t.text.size(), err, sizeof err);
		if (doc == nullptr) return false;
		CircuitWindow* w = nullptr;
		if (alive(from) && (from->isPristine() || (replace && from->saveQuietly(false)))) { from->replaceDocument(doc, ""); w = from; }
		else w = new CircuitWindow(doc, "");
		w->startAs(t.name);
		return true;
	}
	return false;
}

void saveCurrent(CircuitWindow* window) {
	std::string name = window->titleText();
	if (!askText(window->window(), "Save as Template", "Name your template. Start a circuit from it with New from Template.", name) ||
	    name.empty())
		return;
	const std::string folder = newFolder(root());
	const bool wasDirty = window->isDirty();
	const std::string text = cl_document_save_text(window->document());
	if (wasDirty) window->saveQuietly(false);   // asking for the text marked it saved
	if (!folder.empty() && writeFile(folder + "\\name.txt", name) && writeFile(folder + "\\template.cdl", text))
		window->note("Saved “" + name + "” as a template.");
	else
		window->note("Couldn't save that template.");
}

}  // namespace templates

// ---- My Parts ---------------------------------------------------------------------

namespace parts {

namespace {

std::string root() { return settingsDir() + "\\Parts"; }

// A scratch circuit per part, holding just its gates, to draw from.
std::map<std::string, CLDocument*>& drawings() {
	static std::map<std::string, CLDocument*> m;
	return m;
}

void forget(const std::string& gateName) {
	auto it = drawings().find(gateName);
	if (it == drawings().end()) return;
	if (it->second) cl_document_close(it->second);
	drawings().erase(it);
}

}  // namespace

std::string Part::text() const { return readFile(folder + "\\part.txt"); }

std::vector<Part> all() {
	std::vector<Part> out;
	for (const std::string& id : folders(root())) {
		Part p;
		p.id = id;
		p.folder = root() + "\\" + id;
		if (!fileExists(p.folder + "\\part.txt")) continue;
		p.name = trim(readFile(p.folder + "\\name.txt"));
		if (p.name.empty()) p.name = "Untitled Part";
		out.push_back(p);
	}
	std::sort(out.begin(), out.end(), [](const Part& a, const Part& b) { return lowerCase(a.name) < lowerCase(b.name); });
	return out;
}

bool isPart(const std::string& gateName) { return gateName.compare(0, strlen(kPrefix), kPrefix) == 0; }

bool find(const std::string& gateName, Part& out) {
	if (!isPart(gateName)) return false;
	const std::string id = gateName.substr(strlen(kPrefix));
	for (const Part& p : all()) if (p.id == id) { out = p; return true; }
	return false;
}

void draw(const std::string& gateName, ID2D1RenderTarget* rt, double w, double h, double scale, bool dark) {
	auto it = drawings().find(gateName);
	if (it == drawings().end()) {
		CLDocument* doc = nullptr;
		Part p;
		if (find(gateName, p)) {
			doc = cl_document_new();
			const char* back = nullptr;
			const std::string text = p.text();
			if (!cl_edit_paste(doc, 0, text.c_str(), 0, 0, true, &back)) { cl_document_close(doc); doc = nullptr; }
			else cl_edit_select_none(doc, 0);
		}
		it = drawings().insert({ gateName, doc }).first;
	}
	if (it->second) cl_document_draw_fitted(it->second, 0, rt, w, h, 2, scale, dark ? CL_STYLE_DARK : CL_STYLE_LIGHT);
}

void saveSelection(CircuitWindow* window) {
	const std::string text = cl_edit_copy(window->document(), window->currentPage());
	if (text.empty()) { window->note("Select the gates for your part first."); return; }
	std::string name;
	if (!askText(window->window(), "Save as Part",
	             "Name your part. It'll be in the side panel under My Parts, and in Add a Gate (A).", name) || name.empty())
		return;
	const std::string folder = newFolder(root());
	if (!folder.empty() && writeFile(folder + "\\name.txt", name) && writeFile(folder + "\\part.txt", text)) {
		window->note("Saved “" + name + "” to My Parts.");
		for (CircuitWindow* w : circuitWindows()) w->partsChanged();
	} else {
		window->note("Couldn't save that part.");
	}
}

bool tileMenu(HWND owner, const std::string& gateName, POINT screen) {
	Part p;
	if (!find(gateName, p)) return false;
	HMENU m = CreatePopupMenu();
	AppendMenuW(m, MF_STRING, 1, L"&Rename…");
	AppendMenuW(m, MF_STRING, 2, L"&Delete…");
	const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, owner, nullptr);
	DestroyMenu(m);
	if (cmd == 1) {
		std::string name = p.name;
		if (!askText(owner, "Rename Part", "The part's name:", name) || name.empty()) return false;
		writeFile(p.folder + "\\name.txt", name);
	} else if (cmd == 2) {
		if (!askConfirm(owner, "Delete “" + p.name + "”?",
		                "It goes from My Parts to the Recycle Bin. Circuits that already use it keep their copy.", "Delete", "Cancel", true))
			return false;
		recycle(p.folder);
		forget(gateName);
	} else {
		return false;
	}
	for (CircuitWindow* w : circuitWindows()) w->partsChanged();
	return true;
}

}  // namespace parts
