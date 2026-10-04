// The truth table window, as the Mac app's (TruthTableView.swift) and the
// Windows app's: the brand's band across the top with three tabs -- the
// table (click a column's name to rename it), a Karnaugh map for every light
// with the groups drawn on, and each light's simplest sum of products and
// product of sums, with NOT as a bar over the letter and a way to build them
// as gates. And Check: the lights against a formula or truth table the
// assignment gives (the core's cl_check_*, as the Mac app), wrong rows shown.
// Copy puts the table on the clipboard tab-separated (it pastes into a
// document or a spreadsheet); Export saves it as CSV.

#include "Brand.h"
#include "Dialogs.h"
#include "Formula.h"
#include "Sheet.h"
#include "Window.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

namespace {

using brand::kInk;
using brand::kInkDeep;
using brand::kNeon;
using brand::kNeonDeep;
const Color kBandDim = colorF(0.62f, 0.74f, 0.66f);
const Color kOrange = colorF(0.96f, 0.58f, 0.13f), kBlue = colorF(0.24f, 0.48f, 0.98f), kRed = colorF(0.92f, 0.26f, 0.24f);
const char* const kMono = "Monospace";
const char* const kSerif = "Serif";

// A formula as a textbook writes it: the letters in a serif italic, NOT as a
// bar over the letter. Returns the width drawn (or that would be, when cr is
// null).
float drawFormula(cairo_t* cr, const std::string& name, const formula::TwoLevel& t, const std::vector<std::string>& names, float x, float y,
                  float size, Color ink) {
	float at = x;
	auto plain = [&](const std::string& s) {
		if (cr) brand::text(cr, s, at, y, size, brand::Normal, ink);
		at += brand::textWidth(s, size, brand::Normal);
	};
	auto lit = [&](const formula::Literal& l) {
		const std::string s = names[l.variable];
		const float w = faceWidth(s, kSerif, size * 1.05f, false, true);
		if (cr) {
			drawFace(cr, s, at + 0.5f, y - size * 0.04f, kSerif, size * 1.05f, ink, false, true);
			if (l.negated) fillRect(cr, rectF(at + 1.5f, y + size * 0.02f, at + w, y + size * 0.02f + std::max(1.2f, size / 14)), ink);
		}
		at += w + 1;
	};
	plain(name + " = ");
	if (t.constant >= 0) { plain(t.constant ? "1" : "0"); return at - x; }
	bool tight = true;
	for (const std::string& s : names) tight = tight && s.size() == 1;
	if (t.sumOfProducts) {
		for (size_t k = 0; k < t.terms.size(); k++) {
			if (k > 0) plain(" + ");
			for (size_t i = 0; i < t.terms[k].size(); i++) {
				if (i > 0 && !tight) plain("·");
				lit(t.terms[k][i]);
			}
		}
	} else {
		const bool bare = t.terms.size() == 1;
		for (const std::vector<formula::Literal>& clause : t.terms) {
			const bool paren = !bare && clause.size() > 1;
			if (paren) plain("(");
			for (size_t i = 0; i < clause.size(); i++) {
				if (i > 0) plain(" + ");
				lit(clause[i]);
			}
			if (paren) plain(")");
		}
	}
	return at - x;
}

// A name a formula can use: letters, digits and _, starting with a letter.
// More than one capital would read as a product (LED is L·E·D), so those
// keep the first letter and any digits at the end (LED1 is L1).
std::string formulaName(const std::string& s, const std::string& fallback) {
	std::string kept;
	for (char c : s) if (isalnum((unsigned char)c) || c == '_') kept += c;
	if (kept.empty() || !isalpha((unsigned char)kept[0])) return fallback;
	bool restSmall = true;
	for (size_t i = 1; i < kept.size(); i++) if (isupper((unsigned char)kept[i])) restSmall = false;
	if (kept.size() == 1 || restSmall) return kept;
	size_t digits = kept.size();
	while (digits > 1 && isdigit((unsigned char)kept[digits - 1])) digits--;
	return kept.substr(0, 1) + kept.substr(digits);
}

// ---- The last check of each circuit ----
// Saved circuits' in ~/.config/CedarLogic/checks.ini (a group per file and
// page); unsaved ones' only while the app runs.
struct SavedCheck {
	int kind = 0;   // 0 a formula, 1 a truth table
	std::string text;
	std::map<std::string, std::string> names;   // asked-for name -> the circuit's
};
std::map<std::string, SavedCheck> gUnsavedChecks;

std::string checksFile() { return std::string(g_get_user_config_dir()) + "/CedarLogic/checks.ini"; }
std::string checkGroup(std::string key) {
	for (char& c : key) if (c == '[' || c == ']' || c == '\n' || c == '\r') c = '_';
	return key;
}

bool loadCheck(const std::string& key, SavedCheck& out) {
	if (key.empty()) return false;
	if (key.rfind("unsaved-", 0) == 0) {
		auto it = gUnsavedChecks.find(key);
		if (it == gUnsavedChecks.end()) return false;
		out = it->second;
		return true;
	}
	GKeyFile* k = g_key_file_new();
	bool ok = false;
	const std::string group = checkGroup(key);
	if (g_key_file_load_from_file(k, checksFile().c_str(), G_KEY_FILE_NONE, nullptr) && g_key_file_has_group(k, group.c_str())) {
		out.kind = g_key_file_get_integer(k, group.c_str(), "kind", nullptr);
		auto str = [&](const char* name) {
			gchar* v = g_key_file_get_string(k, group.c_str(), name, nullptr);
			std::string s = v ? v : "";
			g_free(v);
			return s;
		};
		out.text = str("text");
		const std::string names = str("names");
		size_t at = 0;
		while (at < names.size()) {
			size_t end = names.find('\n', at);
			if (end == std::string::npos) end = names.size();
			const std::string line = names.substr(at, end - at);
			const size_t tab = line.find('\t');
			if (tab != std::string::npos) out.names[line.substr(0, tab)] = line.substr(tab + 1);
			at = end + 1;
		}
		ok = true;
	}
	g_key_file_free(k);
	return ok;
}

void saveCheck(const std::string& key, const SavedCheck& c) {
	if (key.empty()) return;
	if (key.rfind("unsaved-", 0) == 0) { gUnsavedChecks[key] = c; return; }
	GKeyFile* k = g_key_file_new();
	g_key_file_load_from_file(k, checksFile().c_str(), G_KEY_FILE_KEEP_COMMENTS, nullptr);
	const std::string group = checkGroup(key);
	g_key_file_set_integer(k, group.c_str(), "kind", c.kind);
	g_key_file_set_string(k, group.c_str(), "text", c.text.c_str());
	std::string names;
	for (auto& [a, b] : c.names) names += a + "\t" + b + "\n";
	g_key_file_set_string(k, group.c_str(), "names", names.c_str());
	gchar* dir = g_path_get_dirname(checksFile().c_str());
	g_mkdir_with_parents(dir, 0700);
	g_free(dir);
	g_key_file_save_to_file(k, checksFile().c_str(), nullptr);
	g_key_file_free(k);
}

// Names match whatever their case or spacing, as the core matches them.
std::string nameKey(const std::string& s) {
	std::string k;
	for (unsigned char c : s) if (isalnum(c) || c >= 0x80) k += (char)tolower(c);
	return k;
}

const int kCheckTab = 3;
int gTab = 0;   // the tab last shown, for next time
int gOpen = 0;  // truth tables open now

struct TruthWindow {
	CircuitWindow* owner = nullptr;
	std::vector<std::string> names;
	int inputs = 0;
	std::vector<std::string> rows;   // one char per column
	bool sequential = false;
	int unsettled = 0;
	int tab = 0;
	bool groupsOfOnes = true;
	std::string buildText;   // set when "Build This as a Circuit" was chosen

	// Check: what the assignment gives, and how the circuit compares.
	std::string checkKey;
	SavedCheck want;
	CLCheck* check = nullptr;
	std::string checkError;   // the formula couldn't be read
	bool onlyWrong = false, scrollToWrong = false;
	GtkWidget* checkScroll = nullptr;
	GtkWidget* checkView = nullptr;
	GtkCssProvider* checkCss = nullptr;
	int placed[4] = { -1, -1, -1, -1 };
	bool cssDark = false, cssSet = false;

	~TruthWindow() { if (check) cl_check_free(check); }

	Sheet sheet;
	float scroll = 0, contentH = 0;
	RectF contentRect;
	std::string copied;
	guint copiedTimer = 0;

	int n() const { return inputs; }
	int outputs() const { return (int)names.size() - inputs; }
	std::vector<std::string> inputNames() const { return std::vector<std::string>(names.begin(), names.begin() + inputs); }
	std::vector<int> values(int k) const {
		std::vector<int> v;
		for (const std::string& row : rows) {
			const char c = row[inputs + k];
			v.push_back(c == '1' ? 1 : c == '0' ? 0 : -1);
		}
		return v;
	}
	int unknownCount() const {
		int u = 0;
		for (int k = 0; k < outputs(); k++) for (int v : values(k)) if (v < 0) u++;
		return u;
	}
	std::string tableText(char sep) const {
		// A CSV's column names quoted when they need it ("A, B", Sum "S").
		auto field = [sep](const std::string& f) {
			if (sep != ',' || f.find_first_of(",\"\r\n") == std::string::npos) return f;
			std::string q = "\"";
			for (char ch : f) q += ch == '"' ? std::string("\"\"") : std::string(1, ch);
			return q + "\"";
		};
		std::string s;
		for (size_t c = 0; c < names.size(); c++) s += (c ? std::string(1, sep) : "") + field(names[c]);
		s += "\n";
		for (const std::string& row : rows) {
			for (size_t c = 0; c < row.size(); c++) { if (c) s += sep; s += row[c]; }
			s += "\n";
		}
		return s;
	}
	std::string formulasForBuilding() const {
		// Every name once, inputs and outputs alike, and none a formula's word.
		std::vector<std::string> taken;
		auto unused = [&](const std::string& name, const std::string& fallback, const char* numbered) {
			auto usable = [&](const std::string& n) {
				std::string up = n;
				for (char& c : up) c = (char)toupper((unsigned char)c);
				return up != "AND" && up != "OR" && up != "NOT" && up != "XOR" && std::find(taken.begin(), taken.end(), n) == taken.end();
			};
			std::string out = usable(name) ? name : fallback;
			for (int k = 1; !usable(out); k++) out = format(numbered, k);
			taken.push_back(out);
			return out;
		};
		std::vector<std::string> ins;
		for (int i = 0; i < inputs; i++) {
			const std::string letter(1, (char)('A' + i));
			ins.push_back(unused(formulaName(names[i], letter), letter, "X%d"));
		}
		std::string out;
		for (int k = 0; k < outputs(); k++) {
			const std::string fallback = outputs() == 1 ? "F" : format("F%d", k + 1);
			const std::string o = unused(formulaName(names[inputs + k], fallback), fallback, "Y%d");
			std::string list;
			for (size_t i = 0; i < ins.size(); i++) list += (i ? "," : "") + ins[i];
			out += (k ? "\n" : "") + o + "(" + list + ") = " + formula::simplest(true, inputs, values(k)).text(ins);
		}
		return out;
	}

	void copy(const std::string& key, const std::string& text) {
		gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text.c_str(), -1);
		copied = key;
		if (copiedTimer) g_source_remove(copiedTimer);
		copiedTimer = g_timeout_add(1500, [](gpointer self) -> gboolean {
			TruthWindow* t = static_cast<TruthWindow*>(self);
			t->copied.clear();
			t->copiedTimer = 0;
			t->sheet.redraw();
			return G_SOURCE_REMOVE;
		}, this);
		sheet.redraw();
	}

	void exportCsv() {
		GtkFileChooserNative* c = gtk_file_chooser_native_new("Export as CSV", GTK_WINDOW(sheet.window), GTK_FILE_CHOOSER_ACTION_SAVE,
		                                                      "_Export", "_Cancel");
		gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(c), TRUE);
		gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(c), "Truth Table.csv");
		if (!prefs().lastFolder.empty()) gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(c), prefs().lastFolder.c_str());
		std::string file;
		if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(c)) == GTK_RESPONSE_ACCEPT)
			if (gchar* f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(c))) { file = f; g_free(f); }
		g_object_unref(c);
		if (file.empty()) return;
		const std::string text = tableText(',');
		if (!g_file_set_contents(file.c_str(), text.data(), (gssize)text.size(), nullptr))
			showMessage(GTK_WINDOW(sheet.window), GTK_MESSAGE_WARNING, "Couldn't save it there", "Try another folder.");
	}

	void run() {
		sheet.title = "Truth Table";
		sheet.width = 760;
		sheet.height = 660;
		sheet.minWidth = 620;
		sheet.minHeight = 480;
		sheet.paint = [this](Sheet& s, cairo_t* cr, float w, float h) { paint(s, cr, w, h); };
		sheet.onScroll = [this](Sheet&, float dy) {
			const float view = contentRect.bottom - contentRect.top;
			scroll = std::max(0.0f, std::min(scroll + dy, contentH - view));
		};
		sheet.onKey = [this](Sheet& s, guint k, guint state) -> bool {
			if (checkView && gtk_widget_has_focus(checkView)) return false;   // typing what's asked for
			if (k == GDK_KEY_Return || k == GDK_KEY_KP_Enter) { s.close(); return true; }
			if (k == GDK_KEY_Tab || k == GDK_KEY_ISO_Left_Tab) {
				tab = (tab + ((state & GDK_SHIFT_MASK) || k == GDK_KEY_ISO_Left_Tab ? 3 : 1)) % 4;
				scroll = 0;
				return true;
			}
			if ((state & GDK_CONTROL_MASK) && (k == GDK_KEY_c || k == GDK_KEY_C)) { copy("table", tableText('\t')); return true; }
			return false;
		};
		sheet.onOpen = [this](Sheet& sh) { openCheckEditor(sh); };
		sheet.onClose = [this](Sheet&) {
			if (checkView) g_signal_handlers_disconnect_by_data(gtk_text_view_get_buffer(GTK_TEXT_VIEW(checkView)), this);
		};
		recheck();
		sheet.run(owner->window());
		if (copiedTimer) g_source_remove(copiedTimer);
		if (checkCss) g_object_unref(checkCss);
		saveCheck(checkKey, want);
	}

	void paint(Sheet& s, cairo_t* cr, float w, float h) {
		const bool dark = prefs().dark;
		const Color accent = chrome().accent();
		const Color ink = dark ? colorF(0.93f, 0.93f, 0.93f) : colorF(0.1f, 0.1f, 0.1f);
		const Color dim = dark ? colorF(0.62f, 0.62f, 0.62f) : colorF(0.42f, 0.42f, 0.42f);
		const Color paper = dark ? colorF(0.075f, 0.085f, 0.1f) : colorF(0.965f, 0.97f, 0.975f);
		fillRect(cr, rectF(0, 0, w, h), paper);

		// The band.
		{
			cairo_pattern_t* g = cairo_pattern_create_linear(0, 0, w, 104);
			cairo_pattern_add_color_stop_rgb(g, 0, kInk.r, kInk.g, kInk.b);
			cairo_pattern_add_color_stop_rgb(g, 1, 0.06, 0.12, 0.08);
			cairo_rectangle(cr, 0, 0, w, 104);
			cairo_set_source(cr, g);
			cairo_fill(cr);
			cairo_pattern_destroy(g);
			for (int i = 0; i < 4; i++) {
				const float y = 14 + i * 15.0f;
				const Color trace = brand::alpha(kNeon, 0.18f);
				drawLine(cr, pointF(w * 0.62f, y), pointF(w * 0.70f + i * 12, y), trace, 1.2f);
				drawLine(cr, pointF(w * 0.70f + i * 12, y), pointF(w * 0.74f + i * 12, y + 9), trace, 1.2f);
				drawLine(cr, pointF(w * 0.74f + i * 12, y + 9), pointF(w, y + 9), trace, 1.2f);
			}
			drawTextMid(cr, "Truth Table", rectF(22, 22, 200, 50), 21, colorF(1, 1, 1), TextAlign::Leading, true);
			drawTextMid(cr, format("%d switch%s → %d light%s · %d rows", n(), n() == 1 ? "" : "es", outputs(), outputs() == 1 ? "" : "s",
			                       (int)rows.size()),
			            rectF(160, 27, w - 22, 50), 12.5f, kBandDim);
			const char* tabs[] = { "Truth Table", "Karnaugh Map", "Formulas", "Check" };
			float x = 22;
			for (int i = 0; i < 4; i++) {
				const float tw = textWidth(tabs[i], 12.5f, true) + 26;
				const RectF r = rectF(x, 62, x + tw, 90);
				const bool on = tab == i, isHot = s.hotNext();
				fillRound(cr, r, 14, on ? kNeon : colorF(1, 1, 1, isHot ? 0.16f : 0.09f));
				drawTextMid(cr, tabs[i], r, 12.5f, on ? kInkDeep : colorF(1, 1, 1, 0.88f), TextAlign::Center, true);
				s.hit(r, [this, i] { tab = i; scroll = 0; });
				x += tw + 8;
			}
		}

		float y = 104 + 14;
		if (sequential && tab != kCheckTab) {
			drawTextMid(cr, "This page has clocks or flip-flops, so outputs can depend on what happened before.", rectF(22, y, w - 22, y + 18),
			            12, dim);
			y += 22;
		}
		if (unsettled > 0 && tab != kCheckTab) {
			drawTextMid(cr, format("%d row%s never stopped changing (a clock or an oscillation), so those outputs are a snapshot.", unsettled,
			                       unsettled == 1 ? "" : "s"),
			            rectF(22, y, w - 22, y + 18), 12, kOrange);
			y += 22;
		}

		// The footer.
		const float fy = h - 18 - 30;
		auto pill = [&](const std::string& label, float x, std::function<void()> act) {
			const float pw = textWidth(label, 12.5f) + 26;
			const RectF r = rectF(x, fy, x + pw, fy + 30);
			fillRound(cr, r, 15, withAlpha(ink, s.hotNext() ? 0.10f : 0.06f));
			strokeRound(cr, r, 15, withAlpha(ink, 0.09f));
			drawTextMid(cr, label, r, 12.5f, ink, TextAlign::Center);
			s.hit(r, act);
			return pw;
		};
		float fx = 22;
		fx += pill("Copy Table", fx, [this] { copy("table", tableText('\t')); }) + 10;
		fx += pill("Export as CSV…", fx, [this] { exportCsv(); }) + 10;
		if (copied == "table") drawTextMid(cr, "Copied", rectF(fx, fy, fx + 80, fy + 30), 12, dim);
		{
			const RectF done = rectF(w - 22 - 90, fy, w - 22, fy + 30);
			fillRound(cr, done, 15, withAlpha(accent, s.hotNext() ? 1.0f : 0.92f));
			drawTextMid(cr, "Done", done, 13, chrome().onAccent(), TextAlign::Center, true);
			s.hit(done, [&s] { s.close(); });
		}

		// Check's editor stays put above what scrolls.
		if (tab == kCheckTab) y = drawCheckControls(s, cr, y, w, ink, dim, accent, dark);
		else showCheckEditor(false);

		// The tab's content, scrolling.
		contentRect = rectF(22, y, w - 22, fy - 14);
		{
			const float view = contentRect.bottom - contentRect.top;
			scroll = std::max(0.0f, std::min(scroll, std::max(0.0f, contentH - view)));
		}
		cairo_save(cr);
		cairo_rectangle(cr, contentRect.left - 4, contentRect.top, contentRect.right - contentRect.left + 8, contentRect.bottom - contentRect.top);
		cairo_clip(cr);
		cairo_translate(cr, 0, -scroll);
		const size_t fixedHits = s.hits.size();
		// Hover inside the content is tested against scrolled positions.
		switch (tab) {
		case 1: contentH = drawKMaps(s, cr, contentRect, ink, dim, accent, dark); break;
		case 2: contentH = drawFormulas(s, cr, contentRect, ink, dim, accent, dark); break;
		case kCheckTab: contentH = drawCheck(s, cr, contentRect, ink, dim, accent, dark); break;
		default: contentH = drawTable(s, cr, contentRect, ink, dim, accent, dark); break;
		}
		cairo_restore(cr);
		for (size_t i = fixedHits; i < s.hits.size(); i++) {
			s.hits[i].r.top -= scroll;
			s.hits[i].r.bottom -= scroll;
			if (s.hits[i].r.bottom < contentRect.top || s.hits[i].r.top > contentRect.bottom) s.hits[i].r = rectF(0, 0, 0, 0);
		}
	}

	// ---- Check ----

	void recheck() {
		if (check) { cl_check_free(check); check = nullptr; }
		checkError.clear();
		bool blank = true;
		for (char c : want.text) if (!g_ascii_isspace(c)) blank = false;
		if (!blank) {
			CLTruthTable* tt = cl_tt_new(inputs, sequential, unsettled);
			for (const std::string& name : names) cl_tt_add_name(tt, name.c_str());
			for (const std::string& row : rows) cl_tt_add_row(tt, row.c_str());
			std::string mapping;
			for (auto& [a, b] : want.names) mapping += a + "\t" + b + "\n";
			if (want.kind == 0) {
				formula::Parsed p;
				std::string error;
				if (formula::parse(want.text, p, error)) check = cl_check_expected(tt, formula::checkSpec(p).c_str(), mapping.c_str());
				else checkError = error;
			} else {
				check = cl_check_table(tt, want.text.c_str(), mapping.c_str());
			}
			cl_tt_free(tt);
		}
		scrollToWrong = true;
		sheet.redraw();
	}

	void openCheckEditor(Sheet& sh) {
		checkView = gtk_text_view_new();
		gtk_widget_set_name(checkView, "cl-check-text");
		gtk_text_view_set_monospace(GTK_TEXT_VIEW(checkView), TRUE);
		gtk_text_view_set_left_margin(GTK_TEXT_VIEW(checkView), 10);
		gtk_text_view_set_top_margin(GTK_TEXT_VIEW(checkView), 7);
		gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(checkView)), want.text.c_str(), -1);
		checkScroll = gtk_scrolled_window_new(nullptr, nullptr);
		gtk_widget_set_name(checkScroll, "cl-check-scroll");
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(checkScroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
		gtk_container_add(GTK_CONTAINER(checkScroll), checkView);
		gtk_widget_set_halign(checkScroll, GTK_ALIGN_START);
		gtk_widget_set_valign(checkScroll, GTK_ALIGN_START);
		gtk_widget_show(checkView);
		gtk_widget_set_no_show_all(checkScroll, TRUE);
		checkCss = gtk_css_provider_new();
		for (GtkWidget* wdg : { checkScroll, checkView })
			gtk_style_context_add_provider(gtk_widget_get_style_context(wdg), GTK_STYLE_PROVIDER(checkCss),
			                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		gtk_overlay_add_overlay(GTK_OVERLAY(sh.overlay), checkScroll);
		g_signal_connect(gtk_text_view_get_buffer(GTK_TEXT_VIEW(checkView)), "changed", CL_CALLBACK(+[](GtkTextBuffer* b, gpointer self) {
			TruthWindow* t = static_cast<TruthWindow*>(self);
			GtkTextIter s, e;
			gtk_text_buffer_get_bounds(b, &s, &e);
			gchar* text = gtk_text_buffer_get_text(b, &s, &e, FALSE);
			t->want.text = text ? text : "";
			g_free(text);
			t->recheck();
		}), this);
		if (tab == kCheckTab) sh.initialFocus = checkView;
	}

	void showCheckEditor(bool show, float x = 0, float y = 0, float w = 0, float h = 0) {
		if (!checkScroll) return;
		if (!show) {
			if (gtk_widget_get_visible(checkScroll)) gtk_widget_hide(checkScroll);
			return;
		}
		const int want4[4] = { (int)std::lround(x), (int)std::lround(y), (int)std::lround(w), (int)std::lround(h) };
		if (!std::equal(want4, want4 + 4, placed)) {
			std::copy(want4, want4 + 4, placed);
			gtk_widget_set_margin_start(checkScroll, want4[0]);
			gtk_widget_set_margin_top(checkScroll, want4[1]);
			gtk_widget_set_size_request(checkScroll, want4[2], want4[3]);
		}
		const bool dark = prefs().dark;
		if (!cssSet || cssDark != dark) {
			cssSet = true;
			cssDark = dark;
			const std::string css = std::string("#cl-check-scroll, #cl-check-text, #cl-check-text text { background-color: transparent; border: none; }"
			                                    "#cl-check-text text { color: ") + (dark ? "#eeeeee" : "#1a1a1a") + "; }";
			gtk_css_provider_load_from_data(checkCss, css.c_str(), -1, nullptr);
		}
		if (!gtk_widget_get_visible(checkScroll)) gtk_widget_show(checkScroll);
	}

	// What the assignment gives: a formula or a table, and the box to type it in.
	float drawCheckControls(Sheet& s, cairo_t* cr, float y, float w, Color ink, Color dim, Color accent, bool dark) {
		float x = 22;
		const char* intro = "The assignment gives";
		drawTextMid(cr, intro, rectF(x, y, x + 200, y + 28), 12.5f, dim);
		x += textWidth(intro, 12.5f) + 10;
		const char* segs[] = { "A formula", "A truth table" };
		const float segW[2] = { textWidth(segs[0], 12) + 24, textWidth(segs[1], 12) + 24 };
		fillRound(cr, rectF(x, y, x + segW[0] + segW[1] + 4, y + 28), 14, withAlpha(ink, 0.06f));
		for (int i = 0; i < 2; i++) {
			const RectF r = rectF(x + 2, y + 2, x + 2 + segW[i], y + 26);
			const bool on = want.kind == i;
			if (on) fillRound(cr, r, 12, accent);
			else if (s.hotNext()) fillRound(cr, r, 12, withAlpha(ink, 0.06f));
			drawTextMid(cr, segs[i], r, 12, on ? chrome().onAccent() : ink, TextAlign::Center);
			s.hit(r, [this, i] { if (want.kind != i) { want.kind = i; recheck(); } });
			x += segW[i];
		}
		if (want.kind == 1) {
			const char* fill = "Fill In This Circuit's Rows";
			const float fw = textWidth(fill, 11.5f, true);
			const RectF r = rectF(w - 22 - fw, y, w - 22, y + 28);
			drawTextMid(cr, fill, r, 11.5f, withAlpha(accent, s.hotNext() ? 0.75f : 1.0f), TextAlign::Trailing, true);
			s.hit(r, [this] {
				if (checkView) gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(checkView)), circuitTableText().c_str(), -1);
			});
		}
		y += 36;
		const float h = want.kind == 1 ? 104 : 62;
		const RectF box = rectF(22, y, w - 22, y + h);
		fillRound(cr, box, 10, dark ? colorF(1, 1, 1, 0.045f) : colorF(1, 1, 1, 1));
		strokeRound(cr, box, 10, dark ? colorF(1, 1, 1, 0.09f) : colorF(0, 0, 0, 0.08f));
		if (want.text.empty()) {
			const char* hint = want.kind == 0 ? "S = A ^ B ^ Cin\nCout = AB + Cin(A ^ B)        or   F(A,B,C) = Σm(1,3,5) + d(7)"
			                                  : "A  B  Cin | S  Cout\n0  0  0   | 0  0\n0  0  1   | 1  0\n…  (X for don't care)";
			float ly = y + 8;
			std::string text = hint;
			size_t at = 0;
			while (at <= text.size()) {
				size_t end = text.find('\n', at);
				if (end == std::string::npos) end = text.size();
				drawFace(cr, text.substr(at, end - at), box.left + 12, ly, kMono, 13, withAlpha(dim, 0.75f));
				ly += 19;
				at = end + 1;
			}
		}
		showCheckEditor(true, box.left + 1, box.top + 1, box.right - box.left - 2, h - 2);
		return box.bottom + 10;
	}

	// The verdict, its notes, the names, and the rows.
	float drawCheck(Sheet& s, cairo_t* cr, const RectF& box, Color ink, Color dim, Color accent, bool dark) {
		const Color on = dark ? kNeon : kNeonDeep;
		float y = box.top;
		bool blank = true;
		for (char c : want.text) if (!g_ascii_isspace(c)) blank = false;
		const int verdict = check ? cl_check_verdict(check) : 2;
		std::string summary = blank ? (want.kind == 0 ? "Type what the assignment asks for, like S = A ^ B ^ Cin."
		                                              : "Paste or type the truth table you were given.")
		                    : check ? cl_check_summary(check) : "Can't read that yet.";
		const Color color = blank ? dim : verdict == 0 ? on : verdict == 1 ? kRed : kOrange;
		{
			const RectF banner = rectF(box.left, y, box.right, y + 38);
			fillRound(cr, banner, 10, withAlpha(color, blank ? 0.06f : dark ? 0.16f : 0.11f));
			const PointF c = pointF(banner.left + 22, (banner.top + banner.bottom) / 2);
			fillCircle(cr, c, 9.5f, color);
			const char* glyph = blank ? "…" : verdict == 0 ? "✓" : verdict == 1 ? "✕" : "?";
			drawTextMid(cr, glyph, rectF(c.x - 9, c.y - 9, c.x + 9, c.y + 9), 11, colorF(1, 1, 1), TextAlign::Center, true);
			float right = banner.right - 12;
			if (verdict == 1 && check) {
				const char* label = "Only wrong rows";
				const float lw = textWidth(label, 11.5f) + 22;
				const RectF r = rectF(right - lw, banner.top + 8, right, banner.bottom - 8);
				const RectF tick = rectF(r.left, r.top + 3, r.left + 14, r.top + 17);
				fillRound(cr, tick, 4, onlyWrong ? accent : withAlpha(ink, s.hotNext() ? 0.14f : 0.08f));
				if (onlyWrong) drawTextMid(cr, "✓", tick, 10, chrome().onAccent(), TextAlign::Center, true);
				drawTextMid(cr, label, rectF(r.left + 20, r.top, r.right, r.bottom), 11.5f, dim);
				s.hit(r, [this] { onlyWrong = !onlyWrong; scroll = 0; });
				right = r.left - 10;
			}
			drawTextMid(cr, summary, rectF(banner.left + 42, banner.top, right, banner.bottom), 13.5f, blank ? dim : ink, TextAlign::Leading,
			            !blank);
			y = banner.bottom + 10;
		}
		// Notes: problems in red, warnings in orange, the rest quiet.
		auto note = [&](const std::string& text, int kind) {
			const Color c = kind == 2 ? kRed : kind == 1 ? kOrange : dim;
			const char* mark = kind == 2 ? "⊘" : kind == 1 ? "⚠" : "ⓘ";
			drawTextMid(cr, mark, rectF(box.left, y, box.left + 16, y + 16), 11.5f, c);
			const float h = drawWrapped(cr, text, rectF(box.left + 20, y, box.right, y + 200), 11.5f, c);
			y += std::max(16.0f, h) + 5;
		};
		if (!checkError.empty()) note(checkError, 2);
		for (int i = 0; check && i < cl_check_note_count(check); i++) note(cl_check_note(check, i), cl_check_note_kind(check, i));
		if (!check) return y - box.top;

		// Names, when one isn't simply the circuit's: click to choose.
		bool showNames = false;
		for (int i = 0; i < cl_check_name_count(check); i++) {
			const int col = cl_check_name_column(check, i);
			if (col < 0 || cl_check_name_by_hand(check, i) || nameKey(names[col]) != nameKey(cl_check_name(check, i))) showNames = true;
		}
		if (showNames) {
			y += 4;
			drawTextMid(cr, "Names", rectF(box.left, y, box.left + 50, y + 24), 11.5f, dim, TextAlign::Leading, true);
			float x = box.left + 52;
			for (int i = 0; i < cl_check_name_count(check); i++) {
				const std::string name = cl_check_name(check, i);
				const int col = cl_check_name_column(check, i);
				const bool input = cl_check_name_is_input(check, i);
				const std::string label = name + " → " + (col >= 0 ? names[col] : std::string("?"));
				const bool bold = cl_check_name_by_hand(check, i);
				const float cw = textWidth(label, 11.5f, bold) + 20;
				if (x + cw > box.right) { x = box.left + 52; y += 30; }
				const RectF r = rectF(x, y, x + cw, y + 24);
				fillRound(cr, r, 12, col < 0 ? withAlpha(kRed, 0.14f) : withAlpha(ink, s.hotNext() ? 0.11f : 0.06f));
				strokeRound(cr, r, 12, col < 0 ? withAlpha(kRed, 0.5f) : withAlpha(ink, 0.09f));
				drawTextMid(cr, label, r, 11.5f, col < 0 ? kRed : accent, TextAlign::Center, bold);
				s.hit(r, [this, name, input] { chooseName(name, input); });
				x += cw + 6;
			}
			y += 34;
		}

		// The rows: the circuit's inputs, then for each output what was asked for and what it gave.
		std::vector<int> outs;
		for (int k = 0; k < cl_check_outputs(check); k++) if (cl_check_output_column(check, k) >= 0) outs.push_back(k);
		if (outs.empty()) return y - box.top;
		const float cw = 50, headH = 38, rowH = 24;
		std::vector<int> shown;
		for (int r = 0; r < (int)rows.size(); r++) if (!onlyWrong || cl_check_row_wrong(check, r)) shown.push_back(r);
		const float tableW = cw * (inputs + 2 * outs.size()), x0 = box.left, y0 = y;
		const RectF card = rectF(x0, y0, x0 + tableW, y0 + headH + rowH * shown.size());
		fillRound(cr, card, 12, dark ? colorF(1, 1, 1, 0.045f) : colorF(1, 1, 1, 1));
		cairo_save(cr);
		roundedPath(cr, card, 12);
		cairo_clip(cr);
		fillRect(cr, rectF(x0, y0, card.right, y0 + headH), withAlpha(ink, 0.05f));
		for (int i = 0; i < inputs; i++)
			drawTextMid(cr, names[i], rectF(x0 + i * cw, y0, x0 + (i + 1) * cw, y0 + headH), 12, ink, TextAlign::Center, true);
		for (size_t j = 0; j < outs.size(); j++) {
			const float ox = x0 + (inputs + 2 * j) * cw;
			fillRect(cr, rectF(ox, y0, ox + 2 * cw, y0 + headH), withAlpha(accent, dark ? 0.13f : 0.10f));
			drawTextMid(cr, cl_check_output_name(check, outs[j]), rectF(ox, y0 + 3, ox + 2 * cw, y0 + 21), 12, accent, TextAlign::Center, true);
			drawTextMid(cr, "asked", rectF(ox, y0 + 21, ox + cw, y0 + 35), 9.5f, dim, TextAlign::Center);
			drawTextMid(cr, "got", rectF(ox + cw, y0 + 21, ox + 2 * cw, y0 + 35), 9.5f, dim, TextAlign::Center);
		}
		float firstWrong = -1;
		for (size_t i = 0; i < shown.size(); i++) {
			const int r = shown[i];
			const float ry = y0 + headH + i * rowH;
			const bool wrong = cl_check_row_wrong(check, r);
			if (wrong && firstWrong < 0) firstWrong = ry;
			if (wrong) fillRect(cr, rectF(x0, ry, card.right, ry + rowH), withAlpha(kRed, dark ? 0.2f : 0.12f));
			else if (r % 2 == 1) fillRect(cr, rectF(x0, ry, card.right, ry + rowH), withAlpha(ink, 0.03f));
			auto mono = [&](const std::string& t, float cx, Color c, bool bold) {
				drawFace(cr, t, cx + (cw - faceWidth(t, kMono, 13, bold)) / 2, ry + 4, kMono, 13, c, bold);
			};
			for (int c = 0; c < inputs; c++) mono(std::string(1, rows[r][c]), x0 + c * cw, dim, false);
			for (size_t j = 0; j < outs.size(); j++) {
				const int k = outs[j];
				const float ox = x0 + (inputs + 2 * j) * cw;
				const char asked = cl_check_expected_cell(check, r, k), result = cl_check_result(check, r, k);
				const char got = rows[r][cl_check_output_column(check, k)];
				mono(asked == '-' ? "X" : std::string(1, asked), ox, asked == '-' ? kOrange : ink, false);
				mono(std::string(1, got), ox + cw, result == 'x' ? kRed : result == '=' ? ink : dim, result == 'x');
			}
		}
		cairo_restore(cr);
		const Color divider = withAlpha(accent, 0.55f);
		for (size_t j = 0; j < outs.size(); j++) {
			const float dx = x0 + (inputs + 2 * j) * cw;
			fillRect(cr, rectF(dx - 0.75f, y0, dx + 0.75f, card.bottom), divider);
		}
		strokeRound(cr, card, 12, dark ? colorF(1, 1, 1, 0.09f) : colorF(0, 0, 0, 0.08f));
		// The first wrong row in view, once per check.
		if (scrollToWrong) {
			scrollToWrong = false;
			const float view = contentRect.bottom - contentRect.top;
			if (firstWrong >= 0 && firstWrong + rowH - box.top > view) {
				scroll = firstWrong - box.top - view / 2;
				s.redraw();
			}
		}
		return card.bottom + 12 - box.top;
	}

	// Which switch or light an asked-for name is: a menu of them.
	void chooseName(const std::string& name, bool input) {
		struct Pick { TruthWindow* t; std::string name, choice; };
		GtkWidget* menu = gtk_menu_new();
		auto add = [&](const std::string& label, const std::string& choice) {
			GtkWidget* item = gtk_menu_item_new_with_label(label.c_str());
			g_signal_connect_data(item, "activate", CL_CALLBACK(+[](GtkMenuItem*, gpointer d) {
				Pick* p = static_cast<Pick*>(d);
				if (p->choice.empty()) p->t->want.names.erase(p->name);
				else p->t->want.names[p->name] = p->choice;
				p->t->recheck();
			}), new Pick{ this, name, choice }, [](gpointer d, GClosure*) { delete static_cast<Pick*>(d); }, (GConnectFlags)0);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		};
		for (int c = input ? 0 : inputs; c < (input ? inputs : (int)names.size()); c++) add((input ? "Switch " : "Light ") + names[c], names[c]);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
		add("Match by Name", "");
		gtk_menu_attach_to_widget(GTK_MENU(menu), sheet.window, nullptr);
		g_signal_connect(menu, "deactivate", CL_CALLBACK(+[](GtkMenuShell* m, gpointer) {
			g_idle_add([](gpointer m) -> gboolean { gtk_widget_destroy(GTK_WIDGET(m)); return G_SOURCE_REMOVE; }, m);
		}), nullptr);
		gtk_widget_show_all(menu);
		gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
	}

	// This circuit's table as text to edit: "A B | F", then a row each.
	std::string circuitTableText() const {
		auto line = [&](const std::vector<std::string>& cells) {
			std::string s;
			for (size_t c = 0; c < cells.size(); c++) s += (c == 0 ? "" : (int)c == inputs ? " | " : " ") + cells[c];
			return s;
		};
		std::string out = line(names);
		for (const std::string& row : rows) {
			std::vector<std::string> cells;
			for (char ch : row) cells.push_back(std::string(1, ch));
			out += "\n" + line(cells);
		}
		return out;
	}

	float drawTable(Sheet& s, cairo_t* cr, const RectF& box, Color ink, Color dim, Color accent, bool dark) {
		const float cw = 64, headH = 36, rowH = 28;
		const float tableW = cw * names.size(), x0 = box.left, y0 = box.top;
		const Color on = dark ? kNeon : kNeonDeep;
		const RectF card = rectF(x0, y0, x0 + tableW, y0 + headH + rowH * rows.size());
		fillRound(cr, card, 12, dark ? colorF(1, 1, 1, 0.045f) : colorF(1, 1, 1, 1));
		cairo_save(cr);
		roundedPath(cr, card, 12);
		cairo_clip(cr);
		for (size_t c = 0; c < names.size(); c++) {
			const bool out = (int)c >= inputs;
			const RectF r = rectF(x0 + c * cw, y0, x0 + (c + 1) * cw, y0 + headH);
			fillRect(cr, r, out ? withAlpha(accent, dark ? 0.13f : 0.10f) : withAlpha(ink, 0.05f));
			drawTextMid(cr, names[c], r, 13, out ? accent : ink, TextAlign::Center, true);
			const int col = (int)c;
			s.hit(r, [this, col] {
				std::string name = names[col];
				if (askText(GTK_WINDOW(sheet.window), "Rename Column", "The column's name:", name) && !name.empty()) names[col] = name;
			});
		}
		for (size_t r = 0; r < rows.size(); r++) {
			const float y = y0 + headH + r * rowH;
			if (r % 2 == 1) fillRect(cr, rectF(x0, y, x0 + tableW, y + rowH), withAlpha(ink, 0.03f));
			for (size_t c = 0; c < rows[r].size(); c++) {
				const char v = rows[r][c];
				const bool out = (int)c >= inputs;
				const Color color = v == '1' ? on : v == '0' ? dim : v == 'Z' ? kBlue : v == '!' ? kRed : v == 'X' ? kOrange : dim;
				const float cx = x0 + c * cw;
				if (out && v != '0' && v != '-')
					fillRound(cr, rectF(cx + 12, y + 3, cx + cw - 12, y + rowH - 3), 7, withAlpha(color, dark ? 0.16f : 0.12f));
				const std::string t(1, v);
				const bool bold = v == '1';
				drawFace(cr, t, cx + (cw - faceWidth(t, kMono, 14, bold)) / 2, y + 5, kMono, 14, color, bold);
			}
		}
		cairo_restore(cr);
		if (inputs > 0 && inputs < (int)names.size())
			fillRect(cr, rectF(x0 + inputs * cw - 0.75f, y0, x0 + inputs * cw + 0.75f, card.bottom), withAlpha(accent, 0.55f));
		strokeRound(cr, card, 12, dark ? colorF(1, 1, 1, 0.09f) : colorF(0, 0, 0, 0.08f));
		const float ly = card.bottom + 10;
		drawTextMid(cr, "Click a column's name to rename it.   1 on · X unknown · Z floating · ! conflict · – not connected",
		            rectF(x0, ly, box.right, ly + 18), 11.5f, dim);
		return card.bottom + 32 - y0;
	}

	float drawKMaps(Sheet& s, cairo_t* cr, const RectF& box, Color ink, Color dim, Color accent, bool dark) {
		formula::KMapLayout layout;
		if (!formula::KMapLayout::make(n(), layout)) {
			drawTextMid(cr,
			            n() > 4 ? format("Karnaugh maps are drawn for 2 to 4 switches; this table has %d.", n())
			                    : std::string("A Karnaugh map needs at least 2 switches."),
			            rectF(box.left, box.top + 40, box.right, box.top + 70), 13, dim, TextAlign::Center);
			return 80;
		}
		float x = box.left;
		drawTextMid(cr, "Groups of", rectF(x, box.top, x + 70, box.top + 28), 12.5f, dim);
		x += 72;
		const char* segs[] = { "1s (sum of products)", "0s (product of sums)" };
		const float segW[2] = { textWidth(segs[0], 12) + 24, textWidth(segs[1], 12) + 24 };
		fillRound(cr, rectF(x, box.top, x + segW[0] + segW[1] + 4, box.top + 28), 14, withAlpha(ink, 0.06f));
		for (int i = 0; i < 2; i++) {
			const RectF r = rectF(x + 2, box.top + 2, x + 2 + segW[i], box.top + 26);
			const bool on = (i == 0) == groupsOfOnes;
			if (on) fillRound(cr, r, 12, accent);
			drawTextMid(cr, segs[i], r, 12, on ? chrome().onAccent() : ink, TextAlign::Center);
			s.hit(r, [this, i] { groupsOfOnes = i == 0; });
			x += segW[i];
		}
		if (unknownCount() > 0)
			drawTextMid(cr, "X = don't care", rectF(box.right - 120, box.top, box.right, box.top + 28), 11.5f, dim, TextAlign::Trailing);

		const float cell = 46, left = 54, top = 34;
		const int rowsN = (int)layout.rowCodes.size(), colsN = (int)layout.colCodes.size();
		const float mapW = left + colsN * cell + 4, mapH = top + rowsN * cell + 4;
		const float cardW = std::max(300.0f, mapW + 28), cardH = mapH + 28 + 36;
		const int perRow = std::max(1, (int)((box.right - box.left + 16) / (cardW + 16)));
		const std::vector<std::string> ins = inputNames();
		static const Color groupColors[] = { kBlue, kOrange, colorF(0.2f, 0.7f, 0.3f), colorF(0.6f, 0.35f, 0.85f),
		                                     colorF(0.95f, 0.35f, 0.6f), colorF(0.2f, 0.65f, 0.7f), kRed, colorF(0.35f, 0.35f, 0.85f) };
		float bottom = box.top + 40;
		for (int k = 0; k < outputs(); k++) {
			const float cx = box.left + (k % perRow) * (cardW + 16), cy = box.top + 40 + (k / perRow) * (cardH + 16);
			const RectF card = rectF(cx, cy, cx + cardW, cy + cardH);
			fillRound(cr, card, 12, dark ? colorF(1, 1, 1, 0.045f) : colorF(1, 1, 1, 1));
			strokeRound(cr, card, 12, dark ? colorF(1, 1, 1, 0.09f) : colorF(0, 0, 0, 0.08f));
			const std::vector<int> vals = values(k);
			const formula::TwoLevel f = formula::simplest(groupsOfOnes, n(), vals);
			drawFormula(cr, names[inputs + k], f, ins, cx + 14, cy + 12, 16, ink);
			const float mx = cx + 14, my = cy + 50;
			std::string rowNames, colNames;
			for (int i = 0; i < layout.rowVars; i++) rowNames += ins[i];
			for (int i = layout.rowVars; i < n(); i++) colNames += ins[i];
			drawTextMid(cr, rowNames, rectF(mx, my + top - 22, mx + left - 4, my + top - 4), 12, dim, TextAlign::Center, true);
			drawTextMid(cr, colNames, rectF(mx + left, my, mx + left + colsN * cell, my + 16), 12, dim, TextAlign::Center, true);
			auto bits = [](int v, int count) { std::string b; for (int i = count - 1; i >= 0; i--) b += ((v >> i) & 1) ? '1' : '0'; return b; };
			for (int c = 0; c < colsN; c++) {
				const std::string t = bits(layout.colCodes[c], layout.colVars);
				drawFace(cr, t, mx + left + (c + 0.5f) * cell - faceWidth(t, kMono, 12) / 2, my + top - 18, kMono, 12, dim);
			}
			for (int r = 0; r < rowsN; r++) {
				const std::string t = bits(layout.rowCodes[r], layout.rowVars);
				drawFace(cr, t, mx + left - 16 - faceWidth(t, kMono, 12) / 2, my + top + (r + 0.5f) * cell - 8, kMono, 12, dim);
			}
			for (int r = 0; r < rowsN; r++) {
				for (int c = 0; c < colsN; c++) {
					const RectF cb = rectF(mx + left + c * cell, my + top + r * cell, mx + left + (c + 1) * cell, my + top + (r + 1) * cell);
					cairo_rectangle(cr, cb.left, cb.top, cell, cell);
					setColor(cr, withAlpha(dim, 0.4f));
					cairo_set_line_width(cr, 1);
					cairo_stroke(cr);
					const int m = layout.minterm(r, c);
					const int v = m < (int)vals.size() ? vals[m] : -1;
					const std::string t = v == 1 ? "1" : v == 0 ? "0" : "X";
					const bool strong = v == (groupsOfOnes ? 1 : 0);
					drawFace(cr, t, (cb.left + cb.right) / 2 - faceWidth(t, kMono, 17, strong) / 2, (cb.top + cb.bottom) / 2 - 11, kMono, 17,
					         v < 0 ? kOrange : strong ? ink : dim, strong);
					drawText(cr, format("%d", m), rectF(cb.right - 16, cb.bottom - 14, cb.right - 3, cb.bottom - 2), 8.5f, withAlpha(dim, 0.7f),
					         TextAlign::Trailing);
				}
			}
			const RectF grid = rectF(mx + left, my + top, mx + left + colsN * cell, my + top + rowsN * cell);
			cairo_rectangle(cr, grid.left, grid.top, grid.right - grid.left, grid.bottom - grid.top);
			setColor(cr, withAlpha(dim, 0.75f));
			cairo_set_line_width(cr, 1.5);
			cairo_stroke(cr);
			// The groups, clipped to the map so a wrapping one reads as open-ended.
			cairo_save(cr);
			cairo_rectangle(cr, grid.left - 1, grid.top - 1, grid.right - grid.left + 2, grid.bottom - grid.top + 2);
			cairo_clip(cr);
			for (size_t g = 0; g < f.implicants.size(); g++) {
				const Color color = groupColors[g % 8];
				std::vector<std::pair<int, int>> rr, cc;
				layout.runs(f.implicants[g], rr, cc);
				const float inset = 4 + (g % 3) * 3.0f;
				for (auto& rrun : rr) {
					for (auto& crun : cc) {
						RectF r = rectF(grid.left + crun.first * cell + inset, grid.top + rrun.first * cell + inset,
						                grid.left + (crun.second + 1) * cell - inset, grid.top + (rrun.second + 1) * cell - inset);
						if (cc.size() > 1) {
							if (crun.first == 0) r.left -= cell / 2;
							if (crun.second == colsN - 1) r.right += cell / 2;
						}
						if (rr.size() > 1) {
							if (rrun.first == 0) r.top -= cell / 2;
							if (rrun.second == rowsN - 1) r.bottom += cell / 2;
						}
						const float rad = std::min(r.right - r.left, r.bottom - r.top) / 2.4f;
						fillRound(cr, r, rad, withAlpha(color, 0.10f));
						strokeRound(cr, r, rad, withAlpha(color, 0.85f), 2);
					}
				}
			}
			cairo_restore(cr);
			bottom = std::max(bottom, card.bottom);
		}
		return bottom + 8 - box.top;
	}

	float drawFormulas(Sheet& s, cairo_t* cr, const RectF& box, Color ink, Color dim, Color accent, bool dark) {
		const std::vector<std::string> ins = inputNames();
		float y = box.top;
		for (int k = 0; k < outputs(); k++) {
			const RectF card = rectF(box.left, y, box.right, y + 138);
			fillRound(cr, card, 12, dark ? colorF(1, 1, 1, 0.045f) : colorF(1, 1, 1, 1));
			strokeRound(cr, card, 12, dark ? colorF(1, 1, 1, 0.09f) : colorF(0, 0, 0, 0.08f));
			for (int form = 0; form < 2; form++) {
				const bool sop = form == 0;
				const float ry = y + 14 + form * 62;
				drawTextMid(cr, sop ? "SIMPLEST SUM OF PRODUCTS" : "SIMPLEST PRODUCT OF SUMS", rectF(card.left + 14, ry, card.right - 80, ry + 14),
				            10.5f, dim, TextAlign::Leading, true);
				const formula::TwoLevel f = formula::simplest(sop, n(), values(k));
				const std::string key = format("%d-%d", k, form), text = names[inputs + k] + " = " + f.text(ins);
				const RectF copyR = rectF(card.right - 70, ry - 2, card.right - 14, ry + 16);
				drawTextMid(cr, copied == key ? "Copied" : "Copy", copyR, 11.5f, accent, TextAlign::Trailing, true);
				s.hit(copyR, [this, key, text] { copy(key, text); });
				drawFormula(cr, names[inputs + k], f, ins, card.left + 14, ry + 18, 19, ink);
				if (sop) fillRect(cr, rectF(card.left + 14, ry + 54, card.right - 14, ry + 55), withAlpha(ink, 0.08f));
			}
			y = card.bottom + 14;
		}
		if (unknownCount() > 0) {
			drawTextMid(cr, "Rows with no clear 0 or 1 count as don't-cares: the formulas treat them as either.", rectF(box.left, y, box.right, y + 18),
			            11.5f, dim);
			y += 26;
		}
		const float bw = textWidth("Build This as a Circuit…", 12.5f) + 26;
		const RectF b = rectF(box.left, y, box.left + bw, y + 30);
		fillRound(cr, b, 15, withAlpha(ink, s.hotNext() ? 0.10f : 0.06f));
		strokeRound(cr, b, 15, withAlpha(ink, 0.09f));
		drawTextMid(cr, "Build This as a Circuit…", b, 12.5f, ink, TextAlign::Center);
		s.hit(b, [this, &s] { buildText = formulasForBuilding(); s.close(); });
		return y + 40 - box.top;
	}
};

}  // namespace

bool truthTableOpen() { return gOpen > 0; }

void showTruthTable(CircuitWindow* w, int page, bool check) {
	char err[512] = "";
	CLTruthTable* tt = cl_truth_table(w->document(), page, err, sizeof err);
	if (tt == nullptr) {
		showMessage(w->window(), GTK_MESSAGE_INFO, "A truth table couldn't be made for this page",
		            *err ? err : "Add switches (inputs) and lights (outputs) to the page first.");
		return;
	}
	TruthWindow t;
	t.owner = w;
	const int cols = cl_tt_columns(tt), rows = cl_tt_rows(tt);
	t.inputs = cl_tt_inputs(tt);
	for (int c = 0; c < cols; c++) t.names.push_back(cl_tt_name(tt, c));
	for (int r = 0; r < rows; r++) {
		std::string row;
		for (int c = 0; c < cols; c++) row += cl_tt_cell(tt, r, c);
		t.rows.push_back(row);
	}
	t.sequential = cl_tt_sequential(tt);
	t.unsettled = cl_tt_unsettled(tt);
	cl_tt_free(tt);
	t.tab = check ? kCheckTab : gTab;
	// What was last checked is kept per circuit and page.
	t.checkKey = w->filePath().empty() ? format("unsaved-%p#%d", (void*)w, page) : w->filePath() + format("#%d", page);
	loadCheck(t.checkKey, t.want);
	gOpen++;
	t.run();
	gOpen--;
	gTab = t.tab;
	if (!t.buildText.empty()) {
		prefs().lastFormula = t.buildText;
		showBuildFormula(w);
	}
}
