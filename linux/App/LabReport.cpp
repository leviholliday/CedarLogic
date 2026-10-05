// Export Lab Report, as the Mac app's (LabReport.swift): one PDF a student
// can hand in. A header with their name, the circuit and the date; each page
// of the circuit as a light, print-friendly picture (Export as Image's
// drawing); the truth table; a Karnaugh map and the simplest sum of products
// and product of sums for each light (2 to 4 switches); and the
// oscilloscope's recording as a timing diagram. A sheet first lets them tick
// what goes in, and remembers the choices.

#include "Alert.h"
#include "Chrome.h"
#include "Dialogs.h"
#include "Formula.h"
#include "Window.h"

#include <cairo-pdf.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>

// From ExportImage.cpp and Scope.cpp.
bool reportCircuitSize(CLDocument* doc, int page, float& w, float& h);
void reportCircuitDraw(cairo_t* cr, CLDocument* doc, int page, float w, float h, bool color);
void reportTiming(cairo_t* cr, CLDocument* doc, int from, int count, bool color, float& w, float& h);
std::string safeFileName(const std::string& name);

bool hasScopeRecording(CLDocument* doc) { return cl_scope_signal_count(doc) > 0 && cl_scope_length(doc) > 1; }

namespace {

const float kPW = 612, kPH = 792;   // US Letter, portrait
const float kMargin = 54, kContentW = kPW - 2 * kMargin, kFooter = 36;
const char* const kMono = "Monospace";
const char* const kSerif = "Serif";

struct Table {
	std::vector<std::string> names;
	int inputs = 0;
	std::vector<std::string> rows;
	bool sequential = false;
	int unsettled = 0;
	int outputs() const { return (int)names.size() - inputs; }
	std::vector<int> values(int k) const {
		std::vector<int> v;
		for (const std::string& row : rows) {
			const char c = row[inputs + k];
			v.push_back(c == '1' ? 1 : c == '0' ? 0 : -1);
		}
		return v;
	}
};

bool loadTable(CLDocument* doc, int page, Table& t, std::string& error) {
	char err[512] = "";
	CLTruthTable* tt = cl_truth_table(doc, page, err, sizeof err);
	if (tt == nullptr) { error = err; return false; }
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
	return true;
}

// ---- A formula, as a textbook writes it: NOT as a bar over the letter ----

struct Piece { std::string s; bool literal = false, negated = false; };

// The pieces of a formula, in chunks a line may break between.
std::vector<std::vector<Piece>> formulaChunks(const formula::TwoLevel& t, const std::vector<std::string>& names) {
	std::vector<std::vector<Piece>> out;
	auto lit = [&](const formula::Literal& l) { return Piece{ names[l.variable], true, l.negated }; };
	if (t.constant >= 0) { out.push_back({ Piece{ t.constant ? "1" : "0" } }); return out; }
	bool tight = true;
	for (const std::string& s : names) tight = tight && s.size() == 1;
	if (t.sumOfProducts) {
		for (size_t k = 0; k < t.terms.size(); k++) {
			std::vector<Piece> chunk;
			if (k > 0) chunk.push_back(Piece{ " + " });
			for (size_t i = 0; i < t.terms[k].size(); i++) {
				if (i > 0 && !tight) chunk.push_back(Piece{ "·" });
				chunk.push_back(lit(t.terms[k][i]));
			}
			out.push_back(chunk);
		}
	} else {
		const bool bare = t.terms.size() == 1;
		for (const std::vector<formula::Literal>& clause : t.terms) {
			std::vector<Piece> chunk;
			const bool paren = !bare && clause.size() > 1;
			if (paren) chunk.push_back(Piece{ "(" });
			for (size_t i = 0; i < clause.size(); i++) {
				if (i > 0) chunk.push_back(Piece{ " + " });
				chunk.push_back(lit(clause[i]));
			}
			if (paren) chunk.push_back(Piece{ ")" });
			out.push_back(chunk);
		}
	}
	return out;
}

float pieceWidth(const Piece& p, float size) {
	return p.literal ? faceWidth(p.s, kSerif, size * 1.05f, false, true) + 1 : textWidth(p.s, size);
}

// "F = ..." wrapped to `width`, from (x, y); returns the height it took
// (draws only when cr).
float drawFormula(cairo_t* cr, const std::string& name, const formula::TwoLevel& t, const std::vector<std::string>& names, float x, float y,
                  float width, float size, const Color& ink) {
	const float line = size * 1.6f;
	float at = x, top = y;
	auto put = [&](const Piece& p) {
		const float w = pieceWidth(p, size);
		if (cr) {
			if (p.literal) {
				drawFace(cr, p.s, at + 0.5f, top + size * 0.05f, kSerif, size * 1.05f, ink, false, true);
				if (p.negated) fillRect(cr, rectF(at + 1.5f, top + size * 0.05f, at + w - 0.5f, top + size * 0.05f + std::max(1.0f, size / 14)), ink);
			} else {
				drawText(cr, p.s, rectF(at, top, at + w + 4, top + line), size, ink);
			}
		}
		at += w;
	};
	put(Piece{ name + " = " });
	for (const std::vector<Piece>& chunk : formulaChunks(t, names)) {
		float w = 0;
		for (const Piece& p : chunk) w += pieceWidth(p, size);
		if (at + w > x + width && at > x + 1) { at = x; top += line; }
		for (const Piece& p : chunk) {
			if (at == x && p.s == " + ") continue;   // a line doesn't start with the plus
			put(p);
		}
	}
	return top + line - y;
}

// ---- The report ----

struct Report {
	CLDocument* doc;
	cairo_t* cr;
	std::string title, name;
	LabReportOptions options;
	float y = 0;
	int pageNumber = 0;
	// Headings wait here until the block under them is placed, so a heading
	// is never left alone at the bottom of a page.
	std::vector<std::pair<std::string, bool>> pending;   // (text, isSection)
	static constexpr float kSectionH = 43, kSubH = 22;
	float pendingH() const {
		float h = 0;
		for (const auto& p : pending) h += p.second ? kSectionH : kSubH;
		return h;
	}

	const Color black = colorF(0, 0, 0), gray = colorF(0.4f, 0.4f, 0.4f), rule = colorF(0.75f, 0.75f, 0.75f);

	std::string pageTitle(int page) { return "Page " + std::to_string(page + 1) + ": " + cl_document_page_name(doc, page); }

	void footer() {
		std::string line = name;
		if (!title.empty()) line += (line.empty() ? "" : " · ") + title;
		drawText(cr, line, rectF(kMargin, kPH - kMargin + 8, kPW - kMargin - 80, kPH - kMargin + 22), 9, gray);
		drawText(cr, format("Page %d", pageNumber), rectF(kPW - kMargin - 80, kPH - kMargin + 8, kPW - kMargin, kPH - kMargin + 22), 9, gray,
		         TextAlign::Trailing);
	}
	void newPage() {
		if (pageNumber > 0) { footer(); cairo_show_page(cr); }
		pageNumber++;
		fillRect(cr, rectF(0, 0, kPW, kPH), colorF(1, 1, 1));
		y = kMargin;
	}
	// Starts a new page unless `h` more points fit (with any waiting
	// headings), then draws the waiting headings.
	void need(float h) {
		if (y + pendingH() + h > kPH - kMargin - kFooter) newPage();
		const std::vector<std::pair<std::string, bool>> queued = pending;
		pending.clear();
		for (const auto& p : queued) { if (p.second) drawHeading(p.first); else drawSubheading(p.first); }
	}

	void paragraph(const std::string& s, float size = 11, Color color = colorF(0.3f, 0.3f, 0.3f), float after = 10) {
		const RectF box = rectF(kMargin, y, kPW - kMargin, y + 1000);
		const float h = drawWrapped(nullptr, s, box, size, color);
		need(h);
		drawWrapped(cr, s, rectF(kMargin, y, kPW - kMargin, y + h), size, color);
		y += h + after;
	}
	void heading(const std::string& s) { pending.push_back({ s, true }); }
	void subheading(const std::string& s) { pending.push_back({ s, false }); }
	void drawHeading(const std::string& s) {
		y += 8;
		drawText(cr, s, rectF(kMargin, y, kPW - kMargin, y + 22), 15, black, TextAlign::Leading, true);
		y += 24;
		fillRect(cr, rectF(kMargin, y, kPW - kMargin, y + 0.5f), rule);
		y += 10;
	}
	void drawSubheading(const std::string& s) {
		drawText(cr, s, rectF(kMargin, y, kPW - kMargin, y + 18), 12, colorF(0.25f, 0.25f, 0.25f), TextAlign::Leading, true);
		y += 22;
	}

	void header() {
		drawText(cr, title.empty() ? "Circuit" : title, rectF(kMargin, y, kPW - kMargin, y + 34), 26, black, TextAlign::Leading, true);
		y += 36;
		drawText(cr, "Lab report", rectF(kMargin, y, kPW - kMargin, y + 18), 13, colorF(0.3f, 0.3f, 0.3f));
		y += 28;
		fillRect(cr, rectF(kMargin, y, kPW - kMargin, y + 1), black);
		y += 14;
		const float lw = textWidth("Name:", 13);
		drawText(cr, "Name:", rectF(kMargin, y, kMargin + lw + 4, y + 20), 13, colorF(0.3f, 0.3f, 0.3f));
		if (name.empty()) fillRect(cr, rectF(kMargin + lw + 8, y + 16, kMargin + lw + 190, y + 16.6f), colorF(0.4f, 0.4f, 0.4f));
		else drawText(cr, name, rectF(kMargin + lw + 8, y, kPW - kMargin - 200, y + 20), 13, black);
		char date[64];
		const time_t now = time(nullptr);
		struct tm lt;
		localtime_r(&now, &lt);
		strftime(date, sizeof date, "%B", &lt);
		drawText(cr, "Date: " + std::string(date) + format(" %d, %d", lt.tm_mday, lt.tm_year + 1900), rectF(kPW - kMargin - 200, y, kPW - kMargin, y + 20), 13, colorF(0.3f, 0.3f, 0.3f),
		         TextAlign::Trailing);
		y += 34;
	}

	void circuitPicture(int page, int count) {
		float w, h;
		if (!reportCircuitSize(doc, page, w, h)) return;
		if (count > 1) subheading(pageTitle(page));
		const float room = kPH - 2 * kMargin - kFooter - pendingH();   // what a fresh page gives
		const float s = std::min({ kContentW / w, room / h, 1.5f });
		const float dw = w * s, dh = h * s, x = kMargin + (kContentW - dw) / 2;
		need(dh);
		cairo_save(cr);
		cairo_translate(cr, x, y);
		cairo_rectangle(cr, 0, 0, dw, dh);
		cairo_clip(cr);
		cairo_scale(cr, s, s);
		reportCircuitDraw(cr, doc, page, w, h, options.color);
		cairo_restore(cr);
		cairo_rectangle(cr, x, y, dw, dh);
		setColor(cr, colorF(0.82f, 0.82f, 0.82f));
		cairo_set_line_width(cr, 0.5);
		cairo_stroke(cr);
		y += dh + 16;
	}

	void truthTable(const Table& t) {
		if (t.sequential)
			paragraph("This circuit has clocks or flip-flops, so a light can depend on what happened before. Each row is read after the circuit settles from the row above it.", 10);
		if (t.unsettled > 0)
			paragraph(format("%d row%s never stopped changing (a clock or an oscillation), so those lights are a snapshot.", t.unsettled,
			                 t.unsettled == 1 ? "" : "s"), 10);
		const int cols = (int)t.names.size();
		const float cw = std::min(64.0f, kContentW / std::max(cols, 1)), tableW = cw * cols, x0 = kMargin + (kContentW - tableW) / 2, rowH = 16;
		auto headerRow = [&] {
			fillRect(cr, rectF(x0, y, x0 + tableW, y + rowH), colorF(0.92f, 0.92f, 0.92f));
			for (int c = 0; c < cols; c++) {
				cairo_save(cr);
				cairo_rectangle(cr, x0 + c * cw, y, cw, rowH);
				cairo_clip(cr);
				drawTextMid(cr, t.names[c], rectF(x0 + c * cw, y, x0 + (c + 1) * cw, y + rowH), 10, black, TextAlign::Center, true);
				cairo_restore(cr);
			}
			y += rowH;
		};
		need(rowH * 4);
		headerRow();
		float rowTop = y;
		auto endRule = [&] {
			fillRect(cr, rectF(x0 + t.inputs * cw - 0.5f, rowTop - rowH, x0 + t.inputs * cw + 0.5f, y), black);
			fillRect(cr, rectF(x0, y, x0 + tableW, y + 0.5f), colorF(0.6f, 0.6f, 0.6f));
		};
		for (size_t r = 0; r < t.rows.size(); r++) {
			if (y + rowH > kPH - kMargin - kFooter) { endRule(); newPage(); headerRow(); rowTop = y; }
			if (r % 2 == 1) fillRect(cr, rectF(x0, y, x0 + tableW, y + rowH), colorF(0.965f, 0.965f, 0.965f));
			for (int c = 0; c < cols; c++) {
				const char v = t.rows[r][c];
				Color ink = colorF(0.35f, 0.35f, 0.35f);
				if (v == '1') ink = options.color ? colorF(0.05f, 0.5f, 0.2f) : black;
				else if (v == 'X') ink = options.color ? colorF(0.8f, 0.45f, 0) : colorF(0.3f, 0.3f, 0.3f);
				else if (v == 'Z') ink = options.color ? colorF(0.1f, 0.3f, 0.85f) : colorF(0.3f, 0.3f, 0.3f);
				else if (v == '!') ink = options.color ? colorF(0.85f, 0.1f, 0.1f) : black;
				const std::string s(1, v);
				const bool bold = v == '1';
				drawFace(cr, s, x0 + (c + 0.5f) * cw - faceWidth(s, kMono, 10, bold) / 2, y + 2, kMono, 10, ink, bold);
			}
			y += rowH;
		}
		endRule();
		std::string legend = "1 = on, 0 = off";
		bool has[256] = {};
		for (const std::string& row : t.rows) for (char c : row) has[(unsigned char)c] = true;
		if (has[(unsigned char)'X']) legend += ", X = unknown";
		if (has[(unsigned char)'Z']) legend += ", Z = floating";
		if (has[(unsigned char)'!']) legend += ", ! = conflict";
		if (has[(unsigned char)'-']) legend += ", - = not connected";
		y += 6;
		drawText(cr, legend, rectF(kMargin, y, kPW - kMargin, y + 14), 9, gray);
		y += 22;
	}

	// One Karnaugh map at (x, top), with its groups; returns its height.
	float kmap(cairo_t* c, float x, float top, const formula::KMapLayout& layout, const std::vector<std::string>& ins, const std::vector<int>& vals,
	           const formula::TwoLevel& f, bool ones) {
		const float cell = 46, left = 54, head = 34;
		const int rowsN = (int)layout.rowCodes.size(), colsN = (int)layout.colCodes.size();
		const Color dim = colorF(0.42f, 0.42f, 0.42f), orange = options.color ? colorF(0.8f, 0.45f, 0) : colorF(0.3f, 0.3f, 0.3f);
		std::string rowNames, colNames;
		for (int i = 0; i < layout.rowVars; i++) rowNames += ins[i];
		for (int i = layout.rowVars; i < (int)ins.size(); i++) colNames += ins[i];
		if (c) {
			drawTextMid(c, rowNames, rectF(x, top + head - 22, x + left - 4, top + head - 4), 12, dim, TextAlign::Center, true);
			drawTextMid(c, colNames, rectF(x + left, top, x + left + colsN * cell, top + 16), 12, dim, TextAlign::Center, true);
			auto bits = [](int v, int count) { std::string b; for (int i = count - 1; i >= 0; i--) b += ((v >> i) & 1) ? '1' : '0'; return b; };
			for (int cc = 0; cc < colsN; cc++) {
				const std::string t = bits(layout.colCodes[cc], layout.colVars);
				drawFace(c, t, x + left + (cc + 0.5f) * cell - faceWidth(t, kMono, 12) / 2, top + head - 18, kMono, 12, dim);
			}
			for (int r = 0; r < rowsN; r++) {
				const std::string t = bits(layout.rowCodes[r], layout.rowVars);
				drawFace(c, t, x + left - 16 - faceWidth(t, kMono, 12) / 2, top + head + (r + 0.5f) * cell - 8, kMono, 12, dim);
			}
			for (int r = 0; r < rowsN; r++) {
				for (int cc = 0; cc < colsN; cc++) {
					const RectF cb = rectF(x + left + cc * cell, top + head + r * cell, x + left + (cc + 1) * cell, top + head + (r + 1) * cell);
					cairo_rectangle(c, cb.left, cb.top, cell, cell);
					setColor(c, withAlpha(dim, 0.4f));
					cairo_set_line_width(c, 1);
					cairo_stroke(c);
					const int m = layout.minterm(r, cc);
					const int v = m < (int)vals.size() ? vals[m] : -1;
					const std::string s = v == 1 ? "1" : v == 0 ? "0" : "X";
					const bool strong = v == (ones ? 1 : 0);
					drawFace(c, s, (cb.left + cb.right) / 2 - faceWidth(s, kMono, 17, strong) / 2, (cb.top + cb.bottom) / 2 - 11, kMono, 17,
					         v < 0 ? orange : strong ? black : dim, strong);
					drawText(c, format("%d", m), rectF(cb.right - 16, cb.bottom - 14, cb.right - 3, cb.bottom - 2), 8.5f, withAlpha(dim, 0.7f),
					         TextAlign::Trailing);
				}
			}
			const RectF grid = rectF(x + left, top + head, x + left + colsN * cell, top + head + rowsN * cell);
			cairo_rectangle(c, grid.left, grid.top, grid.right - grid.left, grid.bottom - grid.top);
			setColor(c, withAlpha(dim, 0.75f));
			cairo_set_line_width(c, 1.5);
			cairo_stroke(c);
			// The groups, clipped to the map so a wrapping one reads as open-ended.
			static const Color colors[] = { colorF(0.24f, 0.48f, 0.98f), colorF(0.96f, 0.58f, 0.13f), colorF(0.2f, 0.7f, 0.3f), colorF(0.6f, 0.35f, 0.85f),
			                                colorF(0.95f, 0.35f, 0.6f), colorF(0.2f, 0.65f, 0.7f), colorF(0.92f, 0.26f, 0.24f), colorF(0.35f, 0.35f, 0.85f) };
			cairo_save(c);
			cairo_rectangle(c, grid.left - 1, grid.top - 1, grid.right - grid.left + 2, grid.bottom - grid.top + 2);
			cairo_clip(c);
			for (size_t g = 0; g < f.implicants.size(); g++) {
				const Color color = options.color ? colors[g % 8] : colorF(0.2f, 0.2f, 0.2f);
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
						if (options.color) fillRound(c, r, rad, withAlpha(color, 0.10f));
						strokeRound(c, r, rad, withAlpha(color, 0.85f), 2);
					}
				}
			}
			cairo_restore(c);
		}
		return head + rowsN * cell + 4;
	}

	void maps(const Table& t) {
		formula::KMapLayout layout;
		if (!formula::KMapLayout::make(t.inputs, layout)) {
			paragraph(t.inputs > 4 ? format("Karnaugh maps are drawn for 2 to 4 switches; this circuit has %d.", t.inputs)
			                       : std::string("A Karnaugh map needs at least 2 switches."));
			return;
		}
		const std::vector<std::string> ins(t.names.begin(), t.names.begin() + t.inputs);
		const float colW = (kContentW - 24) / 2;
		bool anyUnknown = false;
		for (int k = 0; k < t.outputs(); k++) {
			const std::vector<int> vals = t.values(k);
			for (int v : vals) if (v < 0) anyUnknown = true;
			const std::string& out = t.names[t.inputs + k];
			const formula::TwoLevel forms[2] = { formula::simplest(true, t.inputs, vals), formula::simplest(false, t.inputs, vals) };
			float mapH = 0, textH = 0;
			for (int i = 0; i < 2; i++) {
				mapH = kmap(nullptr, 0, 0, layout, ins, vals, forms[i], i == 0);
				textH = std::max(textH, drawFormula(nullptr, out, forms[i], ins, 0, 0, colW, 13, black));
			}
			const float h = 20 + mapH + 8 + textH + 16;
			need(h);
			for (int i = 0; i < 2; i++) {
				const float x = kMargin + i * (colW + 24);
				drawText(cr, out + ": " + (i == 0 ? "sum of products" : "product of sums"), rectF(x, y, x + colW, y + 16), 10.5f,
				         colorF(0.4f, 0.4f, 0.4f), TextAlign::Leading, true);
				kmap(cr, x, y + 20, layout, ins, vals, forms[i], i == 0);
				drawFormula(cr, out, forms[i], ins, x, y + 20 + mapH + 8, colW, 13, black);
			}
			y += h;
		}
		if (anyUnknown)
			paragraph("X means the light was not clearly on or off for that row, so the formulas treat it as either (a don't-care).", 9.5f);
	}

	void analysis(const std::vector<int>& pages) {
		struct One { int page; Table table; bool ok; std::string error; };
		std::vector<One> all;
		for (int p : pages) {
			One one{ p, Table(), false, "" };
			one.ok = loadTable(doc, p, one.table, one.error);
			all.push_back(one);
		}
		if (options.truthTable) {
			heading("Truth table");
			for (const One& o : all) {
				if (pages.size() > 1) subheading(pageTitle(o.page));
				if (o.ok) truthTable(o.table); else paragraph("No truth table for this page. " + o.error);
			}
		}
		if (options.formulas) {
			heading("Karnaugh maps and formulas");
			for (const One& o : all) {
				if (pages.size() > 1) subheading(pageTitle(o.page));
				if (o.ok) maps(o.table); else paragraph("No Karnaugh maps for this page. " + o.error);
			}
		}
	}

	void timing() {
		heading("Timing diagram");
		if (!hasScopeRecording(doc)) {
			paragraph("The oscilloscope has no recording. Run the circuit with TO labels on the wires you want to see, then export again.");
			return;
		}
		const int length = (int)cl_scope_length(doc), perChunk = 28, maxChunks = 6;
		int from = 0;
		if (length > perChunk * maxChunks) {
			from = length - perChunk * maxChunks;
			paragraph(format("The recording is %d steps long; this shows the last %d.", length, perChunk * maxChunks), 10);
		}
		float fw, fh;
		reportTiming(nullptr, doc, 0, perChunk, options.color, fw, fh);
		const float s = std::min(kContentW / fw, 1.0f);   // every chunk at one scale
		while (from < length) {
			const int count = std::min(perChunk, length - from);
			float w, h;
			reportTiming(nullptr, doc, from, count, options.color, w, h);
			need(h * s);
			cairo_save(cr);
			cairo_translate(cr, kMargin, y);
			cairo_scale(cr, s, s);
			reportTiming(cr, doc, from, count, options.color, w, h);
			cairo_restore(cr);
			y += h * s + 6;
			from += count;
		}
	}

	void build() {
		std::vector<int> pages;
		for (int p = 0; p < cl_document_page_count(doc); p++) if (cl_document_gate_count(doc, p) > 0) pages.push_back(p);
		newPage();
		header();
		if (pages.empty()) paragraph("This circuit is empty, so there is nothing to show.");
		if (options.circuit && !pages.empty()) {
			heading("Circuit");
			for (int p : pages) circuitPicture(p, (int)pages.size());
		}
		if (options.truthTable || options.formulas) analysis(pages);
		if (options.timing) timing();
		footer();
	}
};

std::string trimmed(const std::string& s) {
	const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
	return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

}  // namespace

bool writeLabReport(CLDocument* doc, const std::string& title, const LabReportOptions& options, const std::string& file, std::string& error) {
	cairo_surface_t* surface = cairo_pdf_surface_create(file.c_str(), kPW, kPH);
	cairo_t* cr = cairo_create(surface);
	{
		Report r{ doc, cr, title, trimmed(prefs().studentName), options };
		r.build();
	}
	cairo_destroy(cr);
	cairo_surface_finish(surface);
	const cairo_status_t st = cairo_surface_status(surface);
	cairo_surface_destroy(surface);
	if (st != CAIRO_STATUS_SUCCESS) { error = cairo_status_to_string(st); return false; }
	return true;
}

// ---- The sheet ----

void showExportReport(CircuitWindow* win) {
	CLDocument* doc = win->document();
	Prefs& p = prefs();
	const bool recorded = hasScopeRecording(doc);
	GtkWidget* dialog = gtk_dialog_new();
	gtk_window_set_title(GTK_WINDOW(dialog), "Export Lab Report");
	gtk_window_set_transient_for(GTK_WINDOW(dialog), win->window());
	gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
	gtk_window_set_default_size(GTK_WINDOW(dialog), 520, -1);
	gtk_dialog_add_button(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL);
	GtkWidget* save = gtk_dialog_add_button(GTK_DIALOG(dialog), "Export to File…", GTK_RESPONSE_OK);
	gtk_style_context_add_class(gtk_widget_get_style_context(save), "suggested-action");
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
	GtkWidget* box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	gtk_container_set_border_width(GTK_CONTAINER(box), 16);
	gtk_box_set_spacing(GTK_BOX(box), 10);

	GtkWidget* intro = gtk_label_new("One PDF to hand in: your name, the circuit, and the parts of your analysis you pick.");
	gtk_label_set_xalign(GTK_LABEL(intro), 0);
	gtk_label_set_line_wrap(GTK_LABEL(intro), TRUE);
	gtk_box_pack_start(GTK_BOX(box), intro, FALSE, FALSE, 0);
	GtkWidget* nameRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_box_pack_start(GTK_BOX(nameRow), gtk_label_new("Your name"), FALSE, FALSE, 0);
	GtkWidget* name = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(name), p.studentName.c_str());
	gtk_entry_set_placeholder_text(GTK_ENTRY(name), "First and last name");
	gtk_widget_set_hexpand(name, TRUE);
	gtk_box_pack_start(GTK_BOX(nameRow), name, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(box), nameRow, FALSE, FALSE, 0);

	GtkWidget* frame = gtk_frame_new("Include");
	GtkWidget* inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_container_set_border_width(GTK_CONTAINER(inner), 10);
	gtk_container_add(GTK_CONTAINER(frame), inner);
	auto check = [&](const char* label, bool on) {
		GtkWidget* c = gtk_check_button_new_with_label(label);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(c), on);
		gtk_box_pack_start(GTK_BOX(inner), c, FALSE, FALSE, 0);
		return c;
	};
	GtkWidget* cCircuit = check("The circuit (a picture of each page)", p.reportCircuit);
	GtkWidget* cTable = check("Truth table", p.reportTable);
	GtkWidget* cFormulas = check("Karnaugh maps and simplest formulas (2 to 4 switches)", p.reportFormulas);
	GtkWidget* cTiming = check("Timing diagram from the oscilloscope", p.reportTiming && recorded);
	if (!recorded) {
		gtk_widget_set_sensitive(cTiming, FALSE);
		GtkWidget* note = gtk_label_new("The oscilloscope has no recording yet. Run the circuit with TO labels on its wires to make one.");
		gtk_label_set_xalign(GTK_LABEL(note), 0);
		gtk_label_set_line_wrap(GTK_LABEL(note), TRUE);
		gtk_style_context_add_class(gtk_widget_get_style_context(note), "dim-label");
		gtk_box_pack_start(GTK_BOX(inner), note, FALSE, FALSE, 0);
	}
	gtk_box_pack_start(GTK_BOX(box), frame, FALSE, FALSE, 0);

	GtkWidget* styleRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_box_pack_start(GTK_BOX(styleRow), gtk_label_new("Output style"), FALSE, FALSE, 0);
	GtkWidget* style = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(style), "Color");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(style), "Black & White");
	gtk_combo_box_set_active(GTK_COMBO_BOX(style), p.reportColor ? 0 : 1);
	gtk_box_pack_start(GTK_BOX(styleRow), style, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), styleRow, FALSE, FALSE, 0);
	gtk_widget_show_all(dialog);

	auto on = [](GtkWidget* c) { return gtk_widget_get_sensitive(c) && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(c)); };
	std::string file, title = win->titleText();
	LabReportOptions o;
	for (;;) {
		if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_OK) break;
		o.circuit = on(cCircuit); o.truthTable = on(cTable); o.formulas = on(cFormulas); o.timing = on(cTiming);
		o.color = gtk_combo_box_get_active(GTK_COMBO_BOX(style)) == 0;
		p.studentName = trimmed(gtk_entry_get_text(GTK_ENTRY(name)));
		p.reportCircuit = o.circuit; p.reportTable = o.truthTable; p.reportFormulas = o.formulas;
		if (recorded) p.reportTiming = o.timing;
		p.reportColor = o.color;
		p.save();
		if (!(o.circuit || o.truthTable || o.formulas || o.timing)) { gtk_widget_error_bell(dialog); continue; }
		GtkFileChooserNative* c = gtk_file_chooser_native_new("Export Lab Report", GTK_WINDOW(dialog), GTK_FILE_CHOOSER_ACTION_SAVE, "_Export", "_Cancel");
		gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(c), TRUE);
		gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(c), (safeFileName(title) + " lab report.pdf").c_str());
		if (!p.lastFolder.empty()) gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(c), p.lastFolder.c_str());
		GtkFileFilter* filter = gtk_file_filter_new();
		gtk_file_filter_set_name(filter, "PDF documents");
		gtk_file_filter_add_pattern(filter, "*.pdf");
		gtk_file_filter_add_pattern(filter, "*.PDF");
		gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(c), filter);
		if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(c)) == GTK_RESPONSE_ACCEPT)
			if (gchar* f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(c))) { file = f; g_free(f); }
		g_object_unref(c);
		if (file.empty()) continue;
		break;
	}
	gtk_widget_destroy(dialog);
	if (file.empty()) return;
	if (file.size() < 4 || g_ascii_strcasecmp(file.c_str() + file.size() - 4, ".pdf") != 0) file += ".pdf";
	std::string error;
	if (!writeLabReport(doc, title, o, file, error)) {
		showMessage(win->window(), GTK_MESSAGE_ERROR, "The report couldn't be saved", error);
		return;
	}
	gchar* dir = g_path_get_dirname(file.c_str());
	prefs().lastFolder = dir;
	g_free(dir);
	win->note("Exported " + baseName(file) + ".");
}
