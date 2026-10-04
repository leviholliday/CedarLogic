// Templates and My Parts (see Collections.h).

#include "Collections.h"
#include "Alert.h"
#include "Dialogs.h"
#include "Picker.h"
#include "Window.h"

#include <glib/gstdio.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <map>

namespace {

std::string readFile(const std::string& path) {
	gchar* data = nullptr;
	gsize len = 0;
	if (!g_file_get_contents(path.c_str(), &data, &len, nullptr)) return std::string();
	std::string out(data, len);
	g_free(data);
	return out;
}

bool writeFile(const std::string& path, const std::string& text) {
	return g_file_set_contents(path.c_str(), text.data(), (gssize)text.size(), nullptr) != FALSE;
}

bool fileExists(const std::string& path) { return g_file_test(path.c_str(), G_FILE_TEST_EXISTS) != FALSE; }

std::string trim(const std::string& s) {
	const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
	return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::string lowerCase(const std::string& s) {
	gchar* l = g_utf8_strdown(s.c_str(), -1);
	std::string out = l ? l : "";
	g_free(l);
	return out;
}

std::string dataDir(const char* name) {
	const std::string dir = std::string(g_get_user_data_dir()) + "/CedarLogic/" + name;
	g_mkdir_with_parents(dir.c_str(), 0755);
	return dir;
}

std::string newId() {
	const time_t t = time(nullptr);
	struct tm lt;
	localtime_r(&t, &lt);
	char buf[32];
	strftime(buf, sizeof buf, "%Y%m%d-%H%M%S", &lt);
	return std::string(buf) + format("-%d", 1000 + g_random_int_range(0, 99000));
}

// Into the desktop's trash, so a mistake can be taken back; where there's
// no trash, into a hidden .Trash beside it (as Your Circuits does).
bool recycle(const std::string& folder) {
	GFile* f = g_file_new_for_path(folder.c_str());
	const bool ok = g_file_trash(f, nullptr, nullptr) != FALSE;
	g_object_unref(f);
	if (ok) return true;
	gchar* parent = g_path_get_dirname(folder.c_str());
	gchar* name = g_path_get_basename(folder.c_str());
	const std::string trash = std::string(parent) + "/.Trash";
	g_free(parent);
	g_mkdir_with_parents(trash.c_str(), 0755);
	std::string dest = trash + "/" + name;
	for (int n = 2; fileExists(dest); n++) dest = trash + "/" + name + format(" %d", n);
	g_free(name);
	return ::g_rename(folder.c_str(), dest.c_str()) == 0;
}

std::vector<std::string> folders(const std::string& root) {
	std::vector<std::string> out;
	GDir* d = g_dir_open(root.c_str(), 0, nullptr);
	if (d == nullptr) return out;
	while (const gchar* n = g_dir_read_name(d)) {
		const std::string id = n;
		if (!id.empty() && id[0] != '.' && g_file_test((root + "/" + id).c_str(), G_FILE_TEST_IS_DIR)) out.push_back(id);
	}
	g_dir_close(d);
	return out;
}

double modified(const std::string& path) {
	GStatBuf st;
	return g_stat(path.c_str(), &st) == 0 ? (double)st.st_mtime : 0;
}

bool alive(CircuitWindow* w) { return std::find(circuitWindows().begin(), circuitWindows().end(), w) != circuitWindows().end(); }

}  // namespace

// ---- Templates -------------------------------------------------------------------

namespace templates {

namespace {

struct Template {
	std::string id, name, detail, text, folder;   // folder: "" for a built-in one
};

std::string root() { return dataDir("Templates"); }

std::vector<Template> yours() {
	std::vector<std::pair<Template, double>> found;
	for (const std::string& id : folders(root())) {
		Template t;
		t.id = id;
		t.folder = root() + "/" + id;
		const std::string file = t.folder + "/template.cdl";
		t.text = readFile(file);
		if (t.text.empty()) continue;
		t.name = trim(readFile(t.folder + "/name.txt"));
		if (t.name.empty()) t.name = "Untitled Template";
		const double when = modified(file);
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
		if (end != std::string::npos) text.replace(start, end - start, format("%g", b.second));
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
	for (int i = 0; i < 4; i++) w.push_back({ 1, format("OUT_%d", i), 4, format("IN_%d", i) });
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
	for (int i = 0; i < 4; i++) w.push_back({ i * 2, "OUT_0", display, format("IN_%d", 3 - i) });
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
	p.preview = [&](cairo_t* cr, const RectF& box) {
		double sx = 1, sy = 1;
		cairo_user_to_device_distance(cr, &sx, &sy);
		cairo_save(cr);
		cairo_translate(cr, box.left, box.top);
		const bool drawn = shown && cl_document_draw_fitted(shown, 0, cr, box.right - box.left, box.bottom - box.top, 16, sx,
		                                                    prefs().dark ? CL_STYLE_DARK : CL_STYLE_LIGHT);
		cairo_restore(cr);
		if (!drawn) drawText(cr, "An empty page", box, 12, picker::Look{ prefs().dark }.ink(0.5f), TextAlign::Center);
	};
	std::string useText, useName;
	p.onButton = [&](picker::Picker& pk, int b) -> bool {
		Template t;
		if (b == 100) return true;
		if (!selectedTemplate(t)) return false;
		if (b == 101) { useText = t.text; useName = t.name; return true; }
		if (t.folder.empty()) {
			showMessage(GTK_WINDOW(pk.window), GTK_MESSAGE_INFO, "That one's built in", "Only your own templates can be renamed or deleted.");
			return false;
		}
		if (b == 0) {
			std::string name = t.name;
			if (askText(GTK_WINDOW(pk.window), "Rename Template", "The template's name:", name) && !name.empty()) {
				writeFile(t.folder + "/name.txt", name);
				mine = yours();
				pk.reload();
			}
		} else if (b == 1) {
			if (askConfirm(GTK_WINDOW(pk.window), "Delete “" + t.name + "”?", "It goes to the trash. Circuits you made from it aren't touched.", "Delete", "Cancel", true)) {
				if (!recycle(t.folder))
					showMessage(GTK_WINDOW(pk.window), GTK_MESSAGE_WARNING, "Couldn't delete “" + t.name + "”", "Its folder couldn't be moved.");
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
	if (doc == nullptr) { showMessage(alive(from) ? from->window() : nullptr, GTK_MESSAGE_ERROR, "That template couldn't be opened", err); return; }
	CircuitWindow* w = nullptr;
	if (alive(from) && from->isPristine()) { from->replaceDocument(doc, ""); w = from; }
	else w = new CircuitWindow(from->application(), doc, "");
	w->startAs(useName);
}

std::vector<std::pair<std::string, std::string>> list() {
	std::vector<std::pair<std::string, std::string>> out;
	for (const Template& t : builtIn()) out.push_back({ t.id, t.name });
	for (const Template& t : yours()) out.push_back({ t.id, t.name });
	return out;
}

bool startFrom(const std::string& id, CircuitWindow* from, GtkApplication* app, bool replace) {
	std::vector<Template> all = builtIn();
	for (const Template& t : yours()) all.push_back(t);
	for (const Template& t : all) {
		if (t.id != id) continue;
		char err[512] = "";
		CLDocument* doc = cl_document_open_text(t.text.c_str(), (long)t.text.size(), err, sizeof err);
		if (doc == nullptr) return false;
		CircuitWindow* w = nullptr;
		if (alive(from) && (from->isPristine() || (replace && from->saveQuietly(false)))) { from->replaceDocument(doc, ""); w = from; }
		else w = new CircuitWindow(app, doc, "");
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
	const std::string folder = root() + "/" + newId();
	g_mkdir_with_parents(folder.c_str(), 0755);
	const bool wasDirty = window->isDirty();
	const std::string text = cl_document_save_text(window->document());
	if (wasDirty) window->saveQuietly(false);   // asking for the text marked it saved
	if (writeFile(folder + "/name.txt", name) && writeFile(folder + "/template.cdl", text))
		window->note("Saved “" + name + "” as a template.");
	else
		window->note("Couldn't save that template.");
}

}  // namespace templates

// ---- My Parts ---------------------------------------------------------------------

namespace parts {

namespace {

std::string root() { return dataDir("Parts"); }

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

std::string Part::text() const { return readFile(folder + "/part.txt"); }

std::vector<Part> all() {
	std::vector<Part> out;
	for (const std::string& id : folders(root())) {
		Part p;
		p.id = id;
		p.folder = root() + "/" + id;
		if (!fileExists(p.folder + "/part.txt")) continue;
		p.name = trim(readFile(p.folder + "/name.txt"));
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

void draw(const std::string& gateName, cairo_t* rt, double w, double h, double scale, bool dark) {
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
	const std::string folder = root() + "/" + newId();
	g_mkdir_with_parents(folder.c_str(), 0755);
	if (writeFile(folder + "/name.txt", name) && writeFile(folder + "/part.txt", text)) {
		window->note("Saved “" + name + "” to My Parts.");
		for (CircuitWindow* w : circuitWindows()) w->partsChanged();
	} else {
		window->note("Couldn't save that part.");
	}
}

namespace {

struct MenuData { GtkWindow* owner; std::string gate; };

void renameCb(GtkMenuItem*, gpointer data) {
	const MenuData m0 = *static_cast<MenuData*>(data);
	const MenuData* m = &m0;
	Part p;
	if (!find(m->gate, p)) return;
	std::string name = p.name;
	if (!askText(m->owner, "Rename Part", "The part's name:", name) || name.empty()) return;
	writeFile(p.folder + "/name.txt", name);
	for (CircuitWindow* w : circuitWindows()) w->partsChanged();
}

void deleteCb(GtkMenuItem*, gpointer data) {
	const MenuData m0 = *static_cast<MenuData*>(data);   // the menu (and data) go while asking
	const MenuData* m = &m0;
	Part p;
	if (!find(m->gate, p)) return;
	if (!askConfirm(m->owner, "Delete \u201C" + p.name + "\u201D?",
	              "It goes from My Parts to the trash. Circuits that already use it keep their copy.", "Delete", "Cancel", true))
		return;
	if (!recycle(p.folder)) {
		showMessage(m->owner, GTK_MESSAGE_WARNING, "Couldn't delete \u201C" + p.name + "\u201D", "Its folder couldn't be moved.");
		return;
	}
	forget(m->gate);
	for (CircuitWindow* w : circuitWindows()) w->partsChanged();
}

}  // namespace

void tileMenu(GtkWindow* owner, const std::string& gateName, GdkEvent* e) {
	Part p;
	if (!find(gateName, p)) return;
	MenuData* data = new MenuData{ owner, gateName };
	GtkWidget* menu = gtk_menu_new();
	g_object_set_data_full(G_OBJECT(menu), "data", data, [](gpointer d) { delete static_cast<MenuData*>(d); });
	GtkWidget* r = gtk_menu_item_new_with_mnemonic("_Rename…");
	GtkWidget* d = gtk_menu_item_new_with_mnemonic("_Delete…");
	g_signal_connect(r, "activate", G_CALLBACK(renameCb), data);
	g_signal_connect(d, "activate", G_CALLBACK(deleteCb), data);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), r);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), d);
	gtk_widget_show_all(menu);
	g_signal_connect(menu, "deactivate", CL_CALLBACK(+[](GtkMenuShell* m, gpointer) {
		g_idle_add([](gpointer w) -> gboolean { gtk_widget_destroy(GTK_WIDGET(w)); return FALSE; }, m);
	}), nullptr);
	gtk_menu_popup_at_pointer(GTK_MENU(menu), e);
}

}  // namespace parts
