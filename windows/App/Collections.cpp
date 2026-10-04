// Templates and My Parts (see Collections.h).

#include "Collections.h"
#include "Dialogs.h"
#include "Picker.h"
#include "Window.h"

#include <shellapi.h>

#include <algorithm>
#include <cstdlib>
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

std::string counter() {
	std::vector<Part> p = { { "BB_CLOCK", 0, 0, "" }, { "AA_REGISTER4", 16, 0, "" }, { "EE_VDD", 10, 11, "" },
	                        { "FF_GND", 10, -11, "" }, { "GE_LED_DISPLAY_4BIT", 32, 0, "" } };
	p.push_back(label("4-Bit Counter", 0, 18, 3));
	p.push_back(label("Clock", -3, -4));
	p.push_back(label("Count", 29, -5));
	std::vector<Wire> w = { { 0, "CLK", 1, "clock" }, { 2, "OUT_0", 1, "count_enable" }, { 2, "OUT_0", 1, "count_up" },
	                        { 3, "OUT_0", 1, "load" }, { 3, "OUT_0", 1, "clear" } };
	for (int i = 0; i < 4; i++) w.push_back({ 1, strf("OUT_%d", i), 4, strf("IN_%d", i) });
	return build(p, w, { { "4-Bit Counter", 3 } });
}

std::string sevenSegment() {
	std::vector<Part> p;
	std::vector<Wire> w;
	// Switches D (top, most significant) down to A.
	const char* names[] = { "D", "C", "B", "A" };
	for (int i = 0; i < 4; i++) {
		const double y = 6.0 - i * 4;
		p.push_back({ "AA_TOGGLE", 0, y, "" });
		p.push_back(label(names[i], -4, y));
	}
	const int display = (int)p.size();
	p.push_back({ "GE_LED_DISPLAY_4BIT", 14, 16, "" });
	for (int i = 0; i < 4; i++) w.push_back({ i * 2, "OUT_0", display, strf("IN_%d", 3 - i) });
	// The segments, laid out as the digit: a on top, g in the middle.
	struct Seg { const char* n; double x, y; };
	const Seg segs[] = { { "a", 70, 12 }, { "b", 75, 7 }, { "c", 75, -3 }, { "d", 70, -8 },
	                     { "e", 65, -3 }, { "f", 65, 7 }, { "g", 70, 2 } };
	for (const Seg& s : segs) {
		p.push_back({ "GA_LED", s.x, s.y, "" });
		p.push_back(label(s.n, s.x + 1.6, s.y + 1.6));
	}
	p.push_back(label("7-Segment Decoder", 0, 26, 3));
	p.push_back(label("Hex value", 10, 9));
	p.push_back(label("Build your decoder here", 26, -12));
	p.push_back(label("Segments", 64, 17));
	return build(p, w, { { "7-Segment Decoder", 3 } });
}

std::vector<Template> builtIn() {
	return {
		{ "builtin-lab", "Lab Page", "A title, your name, the date and the course, with room for inputs and outputs", labPage(), "" },
		{ "builtin-counter", "4-Bit Counter", "A clock driving a counting register, shown on a hex display", counter(), "" },
		{ "builtin-7seg", "7-Segment Decoder Starter", "Four switches and seven segment lights: build the decoder between them",
		  sevenSegment(), "" },
	};
}

}  // namespace

void showPicker(CircuitWindow* from) {
	if (from == nullptr) return;
	const std::vector<Template> built = builtIn();
	std::vector<Template> mine = yours();
	auto all = [&] { std::vector<Template> a = built; a.insert(a.end(), mine.begin(), mine.end()); return a; };
	picker::Picker p;
	p.title = "New from Template";
	p.line = "Start a circuit from one of these. Save your own with Save as Template, in the ••• menu's File.";
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
		h.title = "Built In";
		out.push_back(h);
		for (const Template& t : built) { picker::Row r; r.id = t.id; r.title = t.name; r.subtitle = t.detail; r.tile = false; out.push_back(r); }
		h.title = "Yours";
		h.subtitle = mine.empty() ? "None yet." : "";
		out.push_back(h);
		for (const Template& t : mine) { picker::Row r; r.id = t.id; r.title = t.name; r.subtitle = t.detail; r.tile = false; out.push_back(r); }
		return out;
	};
	CLDocument* shown = nullptr;
	std::string shownId;
	auto selectedTemplate = [&](Template& out) {
		const picker::Row* r = p.selected();
		if (r == nullptr) return false;
		for (const Template& t : all()) if (t.id == r->id) { out = t; return true; }
		return false;
	};
	p.onSelect = [&](picker::Picker&) {
		Template t;
		const bool has = selectedTemplate(t);
		if (has && t.id == shownId) return;
		if (shown) cl_document_close(shown);
		shown = nullptr;
		shownId = has ? t.id : std::string();
		if (has) {
			char err[256];
			shown = cl_document_open_text(t.text.c_str(), (long)t.text.size(), err, sizeof err);
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
	std::string useText, useName;
	p.onButton = [&](picker::Picker& pk, int b) -> bool {
		Template t;
		if (b == 100) return true;
		if (!selectedTemplate(t)) return false;
		if (b == 101) { useText = t.text; useName = t.name; return true; }
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
			if (askYesNo(pk.hwnd, "Delete “" + t.name + "”?", "It goes to the Recycle Bin. Circuits you made from it aren't touched.")) {
				recycle(t.folder);
				mine = yours();
				pk.reload();
			}
		}
		return false;
	};
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
	for (const Template& t : builtIn()) out.push_back({ t.id, t.name });
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
		if (!askYesNo(owner, "Delete “" + p.name + "”?",
		              "It goes from My Parts to the Recycle Bin. Circuits that already use it keep their copy."))
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
