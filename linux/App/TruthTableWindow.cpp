// The truth table window, as the Mac app's (TruthTableView.swift) and the
// Windows app's: the brand's band across the top with three tabs -- the
// table (click a column's name to rename it), a Karnaugh map for every light
// with the groups drawn on, and each light's simplest sum of products and
// product of sums, with NOT as a bar over the letter and a way to build them
// as gates. Copy puts the table on the clipboard tab-separated (it pastes
// into a document or a spreadsheet); Export saves it as CSV.

#include "Brand.h"
#include "Dialogs.h"
#include "Formula.h"
#include "Sheet.h"
#include "Window.h"

#include <algorithm>
#include <cmath>
#include <functional>

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
std::string formulaName(const std::string& s, const std::string& fallback) {
	std::string kept;
	for (char c : s) if (isalnum((unsigned char)c) || c == '_') kept += c;
	if (kept.empty() || !isalpha((unsigned char)kept[0])) return fallback;
	bool restSmall = true;
	for (size_t i = 1; i < kept.size(); i++) if (isupper((unsigned char)kept[i])) restSmall = false;
	return kept.size() == 1 || restSmall ? kept : kept.substr(0, 1);
}

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
		std::string s;
		for (size_t c = 0; c < names.size(); c++) s += (c ? std::string(1, sep) : "") + names[c];
		s += "\n";
		for (const std::string& row : rows) {
			for (size_t c = 0; c < row.size(); c++) { if (c) s += sep; s += row[c]; }
			s += "\n";
		}
		return s;
	}
	std::string formulasForBuilding() const {
		std::vector<std::string> ins;
		for (int i = 0; i < inputs; i++) {
			std::string name = formulaName(names[i], std::string(1, (char)('A' + i)));
			if (std::find(ins.begin(), ins.end(), name) != ins.end()) name = std::string(1, (char)('A' + i));
			ins.push_back(name);
		}
		std::string out;
		for (int k = 0; k < outputs(); k++) {
			std::string o = formulaName(names[inputs + k], outputs() == 1 ? "F" : format("F%d", k + 1));
			if (std::find(ins.begin(), ins.end(), o) != ins.end()) o = format("F%d", k + 1);
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
			if (k == GDK_KEY_Return || k == GDK_KEY_KP_Enter) { s.close(); return true; }
			if (k == GDK_KEY_Tab || k == GDK_KEY_ISO_Left_Tab) {
				tab = (tab + ((state & GDK_SHIFT_MASK) || k == GDK_KEY_ISO_Left_Tab ? 2 : 1)) % 3;
				scroll = 0;
				return true;
			}
			if ((state & GDK_CONTROL_MASK) && (k == GDK_KEY_c || k == GDK_KEY_C)) { copy("table", tableText('\t')); return true; }
			return false;
		};
		sheet.run(owner->window());
		if (copiedTimer) g_source_remove(copiedTimer);
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
			const char* tabs[] = { "Truth Table", "Karnaugh Map", "Formulas" };
			float x = 22;
			for (int i = 0; i < 3; i++) {
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
		if (sequential) {
			drawTextMid(cr, "This page has clocks or flip-flops, so outputs can depend on what happened before.", rectF(22, y, w - 22, y + 18),
			            12, dim);
			y += 22;
		}
		if (unsettled > 0) {
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
		default: contentH = drawTable(s, cr, contentRect, ink, dim, accent, dark); break;
		}
		cairo_restore(cr);
		for (size_t i = fixedHits; i < s.hits.size(); i++) {
			s.hits[i].r.top -= scroll;
			s.hits[i].r.bottom -= scroll;
			if (s.hits[i].r.bottom < contentRect.top || s.hits[i].r.top > contentRect.bottom) s.hits[i].r = rectF(0, 0, 0, 0);
		}
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

void showTruthTable(CircuitWindow* w, int page) {
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
	t.tab = gTab;
	gOpen++;
	t.run();
	gOpen--;
	gTab = t.tab;
	if (!t.buildText.empty()) {
		prefs().lastFormula = t.buildText;
		showBuildFormula(w);
	}
}
