// Export Lab Report, as the Mac and Linux apps': one PDF a student can hand
// in. A header with their name, the circuit and the date; each page of the
// circuit as a light, print-friendly picture (Export as Image's drawing); the
// truth table; a Karnaugh map and the simplest sum of products and product of
// sums for each light (2 to 4 switches); and the oscilloscope's recording as
// a timing diagram. A sheet first lets them tick what goes in, and
// remembers the choices.
//
// Windows has no PDF writer, so each US Letter page is drawn off screen with
// Direct2D, at 252 dpi, and put in the file as a picture (Pdf.cpp): it looks
// the same on every computer and prints sharp.

#include "Chrome.h"
#include "Dialogs.h"
#include "Formula.h"
#include "Images.h"
#include "Pdf.h"
#include "Window.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <ctime>
#include <map>
#include <tuple>

// From Export.cpp and Scope.cpp.
bool reportCircuitSize(CLDocument* doc, int page, float& w, float& h);
void reportCircuitDraw(ID2D1RenderTarget* rt, CLDocument* doc, int page, float w, float h, double scale, bool color);
void reportTiming(ID2D1RenderTarget* rt, CLDocument* doc, int from, int count, bool color, float& w, float& h);

bool hasScopeRecording(CLDocument* doc) { return cl_scope_signal_count(doc) > 0 && cl_scope_length(doc) > 1; }

namespace {

const float kPW = 612, kPH = 792;   // US Letter, portrait, in points
const float kMargin = 54, kContentW = kPW - 2 * kMargin, kFooter = 36;
const double kScale = 3.5;           // pixels a point: 252 dpi

typedef D2D1_COLOR_F Col;
Col gray(float v, float a = 1) { return D2D1::ColorF(v, v, v, a); }
Col rgbF(float r, float g, float b) { return D2D1::ColorF(r, g, b); }
D2D1_RECT_F box(float l, float t, float r, float b) { return D2D1::RectF(l, t, r, b); }

// ---- Text (top-left aligned, as the Linux report lays it out) -----------------------

struct Font {
	const wchar_t* family;
	float size;
	bool bold = false, italic = false;
};
const wchar_t* const kUI = L"Segoe UI";
const wchar_t* const kMono = L"Consolas";
const wchar_t* const kSerif = L"Cambria";
Font ui(float size, bool bold = false) { return Font{ kUI, size, bold, false }; }
Font mono(float size, bool bold = false) { return Font{ kMono, size, bold, false }; }

IDWriteTextFormat* faceFormat(const Font& f, TextAlign align, bool wrap, bool middle) {
	typedef std::tuple<std::wstring, int, bool, bool, int, bool, bool> Key;
	static std::map<Key, IDWriteTextFormat*> cache;
	const Key key(f.family, (int)std::lround(f.size * 100), f.bold, f.italic, (int)align, wrap, middle);
	auto it = cache.find(key);
	if (it != cache.end()) return it->second;
	IDWriteTextFormat* t = nullptr;
	if (IDWriteFactory* dw = dwFactory()) {
		const DWRITE_FONT_WEIGHT weight = !f.bold ? DWRITE_FONT_WEIGHT_NORMAL
		                                : wcscmp(f.family, kUI) == 0 ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_BOLD;
		dw->CreateTextFormat(f.family, nullptr, weight, f.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
		                     DWRITE_FONT_STRETCH_NORMAL, f.size, L"", &t);
		if (t) {
			t->SetTextAlignment(align == TextAlign::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
			                    : align == TextAlign::Trailing ? DWRITE_TEXT_ALIGNMENT_TRAILING
			                                                   : DWRITE_TEXT_ALIGNMENT_LEADING);
			t->SetParagraphAlignment(middle ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
			t->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
			if (!wrap) {
				// Too long for its box: end it with an ellipsis.
				IDWriteInlineObject* dots = nullptr;
				if (SUCCEEDED(dw->CreateEllipsisTrimmingSign(t, &dots))) {
					DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
					t->SetTrimming(&trimming, dots);
					dots->Release();
				}
			}
		}
	}
	cache[key] = t;
	return t;
}

IDWriteTextLayout* layoutOf(const Font& f, const std::string& s, float width, bool wrap) {
	IDWriteTextFormat* t = faceFormat(f, TextAlign::Leading, wrap, false);
	IDWriteFactory* dw = dwFactory();
	if (t == nullptr || dw == nullptr || s.empty()) return nullptr;
	const std::wstring w = W(s);
	IDWriteTextLayout* layout = nullptr;
	if (FAILED(dw->CreateTextLayout(w.c_str(), (UINT32)w.size(), t, width, 100000, &layout))) return nullptr;
	return layout;
}

float textW(const Font& f, const std::string& s) {
	IDWriteTextLayout* l = layoutOf(f, s, 10000, false);
	if (l == nullptr) return 0;
	DWRITE_TEXT_METRICS m = {};
	l->GetMetrics(&m);
	l->Release();
	return m.widthIncludingTrailingWhitespace;
}

// How far down from the top of its line the baseline is.
float baselineOf(const Font& f) {
	IDWriteTextLayout* l = layoutOf(f, "Hg", 10000, false);
	if (l == nullptr) return f.size;
	DWRITE_LINE_METRICS m = {};
	UINT32 n = 0;
	l->GetLineMetrics(&m, 1, &n);
	l->Release();
	return n > 0 ? m.baseline : f.size;
}

ID2D1SolidColorBrush* brushOf(ID2D1RenderTarget* rt, const Col& c) {
	ID2D1SolidColorBrush* b = nullptr;
	return rt && SUCCEEDED(rt->CreateSolidColorBrush(c, &b)) ? b : nullptr;
}

// A line of text with its top left at (x, y).
void drawStr(ID2D1RenderTarget* rt, const std::string& s, float x, float y, const Font& f, const Col& c) {
	IDWriteTextFormat* t = faceFormat(f, TextAlign::Leading, false, false);
	ID2D1SolidColorBrush* b = s.empty() || t == nullptr ? nullptr : brushOf(rt, c);
	if (b == nullptr) return;
	const std::wstring w = W(s);
	rt->DrawText(w.c_str(), (UINT32)w.size(), t, box(x, y, x + 4000, y + 400), b);
	b->Release();
}

// A line in a box: from its top, or (middle) centred up and down.
void drawIn(ID2D1RenderTarget* rt, const std::string& s, const D2D1_RECT_F& r, const Font& f, const Col& c,
            TextAlign align = TextAlign::Leading, bool middle = false) {
	IDWriteTextFormat* t = faceFormat(f, align, false, middle);
	ID2D1SolidColorBrush* b = s.empty() || t == nullptr ? nullptr : brushOf(rt, c);
	if (b == nullptr) return;
	const std::wstring w = W(s);
	rt->DrawText(w.c_str(), (UINT32)w.size(), t, r, b);
	b->Release();
}

// Wrapped to the box's width; returns the height it took (drawn when rt).
float wrapped(ID2D1RenderTarget* rt, const std::string& s, const D2D1_RECT_F& r, const Font& f, const Col& c) {
	IDWriteTextLayout* l = layoutOf(f, s, r.right - r.left, true);
	if (l == nullptr) return 0;
	DWRITE_TEXT_METRICS m = {};
	l->GetMetrics(&m);
	if (ID2D1SolidColorBrush* b = brushOf(rt, c)) {
		rt->DrawTextLayout(D2D1::Point2F(r.left, r.top), l, b);
		b->Release();
	}
	l->Release();
	return m.height;
}

// Nothing when there's no page to draw on (the layout still runs, to measure).
void fillBox(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, const Col& c) {
	if (rt) fillRect(rt, r, c);
}

void strokeBox(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, const Col& c, float width) {
	if (ID2D1SolidColorBrush* b = brushOf(rt, c)) {
		rt->DrawRectangle(r, b, width);
		b->Release();
	}
}

std::string trimmed(const std::string& s) {
	const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
	return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

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

// "F = ..." wrapped to `width`, from (x, y); returns the height it took
// (draws only when rt).
float drawFormula(ID2D1RenderTarget* rt, const std::string& name, const formula::TwoLevel& t, const std::vector<std::string>& names, float x,
                  float y, float width, float size, const Col& ink) {
	const Font text = ui(size), letter = Font{ kSerif, size * 1.05f, false, true };
	// The letters sit on the same baseline as the words around them.
	const float baseText = baselineOf(text), letterDy = baseText - baselineOf(letter), line = size * 1.6f;
	auto widthOf = [&](const Piece& p) { return p.literal ? textW(letter, p.s) + 1 : textW(text, p.s); };
	float at = x, top = y;
	auto put = [&](const Piece& p) {
		const float w = widthOf(p);
		if (p.literal) {
			drawStr(rt, p.s, at + 0.5f, top + letterDy, letter, ink);
			if (p.negated && rt) {
				const float thick = std::max(1.0f, size / 14), bottom = top + baseText - size * 0.8f;
				fillBox(rt, box(at + 1.5f, bottom - thick, at + w - 0.5f, bottom), ink);
			}
		} else {
			drawStr(rt, p.s, at, top, text, ink);
		}
		at += w;
	};
	put(Piece{ name + " = " });
	for (const std::vector<Piece>& chunk : formulaChunks(t, names)) {
		float w = 0;
		for (const Piece& p : chunk) w += widthOf(p);
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
	std::string title, name;
	LabReportOptions options;
	pdf::Document file;
	images::Sheet sheet;
	ID2D1RenderTarget* rt = nullptr;   // the page being drawn (null once something failed: the layout carries on, drawing nothing)
	float y = 0;
	int pageNumber = 0;
	bool failed = false;
	std::string error;

	const Col black = gray(0), dark = gray(0.3f), mid = gray(0.4f), rule = gray(0.75f);

	Report(CLDocument* d, const std::string& t, const std::string& n, const LabReportOptions& o)
	    : doc(d), title(t), name(n), options(o), file(t.empty() ? "Lab report" : t + " - lab report", "CedarLogic " CL_VERSION, stamp()) {}

	static std::string stamp() {
		char s[32];
		const time_t now = time(nullptr);
		struct tm lt;
		localtime_s(&lt, &now);
		strftime(s, sizeof s, "%Y%m%d%H%M%S", &lt);
		return s;
	}

	void fail(const std::string& why) {
		if (!failed) error = why;
		failed = true;
		rt = nullptr;
	}

	std::string pageTitle(int page) { return "Page " + std::to_string(page + 1) + ": " + cl_document_page_name(doc, page); }

	// The page just drawn, as a picture in the file: gray when nothing on it
	// has a colour (a third of the bytes).
	void addPicture(IWICBitmap* bmp) {
		UINT w = 0, h = 0;
		if (FAILED(bmp->GetSize(&w, &h)) || w == 0 || h == 0) { fail("A page of the report couldn't be read back."); return; }
		std::vector<unsigned char> src((size_t)w * h * 4);
		if (FAILED(bmp->CopyPixels(nullptr, w * 4, (UINT)src.size(), src.data()))) { fail("A page of the report couldn't be read back."); return; }
		const size_t count = (size_t)w * h;
		bool grayOnly = true;
		for (size_t i = 0; i < count; i++) {
			unsigned char* p = &src[i * 4];   // blue, green, red, alpha (premultiplied): onto white
			if (p[3] != 255) {
				const int add = 255 - p[3];
				for (int k = 0; k < 3; k++) p[k] = (unsigned char)std::min(255, p[k] + add);
			}
			if (p[0] != p[1] || p[1] != p[2]) grayOnly = false;
		}
		std::vector<unsigned char> px(count * (grayOnly ? 1 : 3));
		for (size_t i = 0; i < count; i++) {
			const unsigned char* p = &src[i * 4];
			if (grayOnly) {
				px[i] = p[2];
			} else {
				px[i * 3] = p[2];
				px[i * 3 + 1] = p[1];
				px[i * 3 + 2] = p[0];
			}
		}
		file.addPage(kPW, kPH, (int)w, (int)h, grayOnly ? 1 : 3, px.data());
	}

	void endPage() {
		rt = nullptr;
		IWICBitmap* bmp = sheet.finish();
		if (bmp == nullptr) { fail("A page of the report couldn't be drawn."); return; }
		addPicture(bmp);
		bmp->Release();
		if (options.pump) {
			// Only paint messages, a few: the window stays drawn, and nothing else
			// (a click, a timer) runs in the middle of the report.
			MSG msg;
			for (int i = 0; i < 20 && PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE | PM_QS_PAINT); i++) DispatchMessageW(&msg);
		}
	}

	void footer() {
		std::string line = name;
		if (!title.empty()) line += (line.empty() ? "" : " · ") + title;
		drawIn(rt, line, box(kMargin, kPH - kMargin + 8, kPW - kMargin - 80, kPH - kMargin + 22), ui(9), mid);
		drawIn(rt, strf("Page %d", pageNumber), box(kPW - kMargin - 80, kPH - kMargin + 8, kPW - kMargin, kPH - kMargin + 22), ui(9), mid,
		       TextAlign::Trailing);
	}
	void newPage() {
		if (failed) return;
		if (pageNumber > 0) {
			footer();
			endPage();
			if (failed) return;
		}
		pageNumber++;
		if (!sheet.begin(kPW, kPH, kScale, true)) { fail("A page of the report couldn't be started."); return; }
		rt = sheet.target();
		y = kMargin;
	}
	// Headings wait here until the block under them is placed, so a heading
	// is never left alone at the bottom of a page.
	std::vector<std::pair<std::string, bool>> pending;   // (text, isSection)
	static constexpr float kSectionH = 43, kSubH = 22;
	float pendingH() const {
		float h = 0;
		for (const auto& p : pending) h += p.second ? kSectionH : kSubH;
		return h;
	}
	// Starts a new page unless `h` more points fit (with any waiting
	// headings), then draws the waiting headings.
	void need(float h) {
		if (y + pendingH() + h > kPH - kMargin - kFooter) newPage();
		const std::vector<std::pair<std::string, bool>> queued = pending;
		pending.clear();
		for (const auto& p : queued) { if (p.second) drawHeading(p.first); else drawSubheading(p.first); }
	}

	void paragraph(const std::string& s, float size = 11, Col color = gray(0.3f), float after = 10, float keep = 0) {
		const float h = wrapped(nullptr, s, box(kMargin, y, kPW - kMargin, y + 1000), ui(size), color);
		need(h + keep);
		wrapped(rt, s, box(kMargin, y, kPW - kMargin, y + h), ui(size), color);
		y += h + after;
	}
	void heading(const std::string& s) { pending.push_back({ s, true }); }
	void subheading(const std::string& s) { pending.push_back({ s, false }); }
	void drawHeading(const std::string& s) {
		y += 8;
		drawIn(rt, s, box(kMargin, y, kPW - kMargin, y + 22), ui(15, true), black);
		y += 24;
		fillBox(rt, box(kMargin, y, kPW - kMargin, y + 0.5f), rule);
		y += 10;
	}
	void drawSubheading(const std::string& s) {
		drawIn(rt, s, box(kMargin, y, kPW - kMargin, y + 18), ui(12, true), gray(0.25f));
		y += 22;
	}

	void header() {
		drawIn(rt, title.empty() ? "Circuit" : title, box(kMargin, y, kPW - kMargin, y + 34), ui(26, true), black);
		y += 36;
		drawIn(rt, "Lab report", box(kMargin, y, kPW - kMargin, y + 18), ui(13), dark);
		y += 28;
		fillBox(rt, box(kMargin, y, kPW - kMargin, y + 1), black);
		y += 14;
		const float lw = textW(ui(13), "Name:");
		drawIn(rt, "Name:", box(kMargin, y, kMargin + lw + 4, y + 20), ui(13), dark);
		if (name.empty()) fillBox(rt, box(kMargin + lw + 8, y + 16, kMargin + lw + 190, y + 16.6f), mid);
		else drawIn(rt, name, box(kMargin + lw + 8, y, kPW - kMargin - 200, y + 20), ui(13), black);
		char date[64];
		const time_t now = time(nullptr);
		struct tm lt;
		localtime_s(&lt, &now);
		strftime(date, sizeof date, "%B", &lt);
		drawIn(rt, "Date: " + std::string(date) + strf(" %d, %d", lt.tm_mday, lt.tm_year + 1900), box(kPW - kMargin - 200, y, kPW - kMargin, y + 20), ui(13), dark, TextAlign::Trailing);
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
		if (rt) {
			D2D1_MATRIX_3X2_F was;
			rt->GetTransform(&was);
			rt->SetTransform(D2D1::Matrix3x2F::Scale(s, s) * D2D1::Matrix3x2F::Translation(x, y) * was);
			rt->PushAxisAlignedClip(box(0, 0, w, h), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			reportCircuitDraw(rt, doc, page, w, h, s * kScale, options.color);
			rt->PopAxisAlignedClip();
			rt->SetTransform(was);
			strokeBox(rt, box(x, y, x + dw, y + dh), gray(0.82f), 0.5f);
		}
		y += dh + 16;
	}

	void truthTable(const Table& t) {
		if (t.sequential)
			paragraph("This circuit has clocks or flip-flops, so a light can depend on what happened before. Each row is read after the circuit settles from the row above it.", 10, gray(0.3f), 10, 100);
		if (t.unsettled > 0)
			paragraph(strf("%d row%s never stopped changing (a clock or an oscillation), so those lights are a snapshot.", t.unsettled,
			               t.unsettled == 1 ? "" : "s"), 10, gray(0.3f), 10, 100);
		const int cols = (int)t.names.size();
		const float cw = std::min(64.0f, kContentW / std::max(cols, 1)), tableW = cw * cols, x0 = kMargin + (kContentW - tableW) / 2, rowH = 16;
		auto headerRow = [&] {
			fillBox(rt, box(x0, y, x0 + tableW, y + rowH), gray(0.92f));
			for (int c = 0; c < cols; c++)
				drawIn(rt, t.names[c], box(x0 + c * cw, y, x0 + (c + 1) * cw, y + rowH), ui(10, true), black, TextAlign::Center, true);
			y += rowH;
		};
		need(rowH * 4);
		headerRow();
		float rowTop = y;
		auto endRule = [&] {
			fillBox(rt, box(x0 + t.inputs * cw - 0.5f, rowTop - rowH, x0 + t.inputs * cw + 0.5f, y), black);
			fillBox(rt, box(x0, y, x0 + tableW, y + 0.5f), gray(0.6f));
		};
		for (size_t r = 0; r < t.rows.size(); r++) {
			if (y + rowH > kPH - kMargin - kFooter) { endRule(); newPage(); headerRow(); rowTop = y; }
			if (r % 2 == 1) fillBox(rt, box(x0, y, x0 + tableW, y + rowH), gray(0.965f));
			for (int c = 0; c < cols; c++) {
				const char v = t.rows[r][c];
				Col ink = gray(0.35f);
				if (v == '1') ink = options.color ? rgbF(0.05f, 0.5f, 0.2f) : black;
				else if (v == 'X') ink = options.color ? rgbF(0.8f, 0.45f, 0) : gray(0.3f);
				else if (v == 'Z') ink = options.color ? rgbF(0.1f, 0.3f, 0.85f) : gray(0.3f);
				else if (v == '!') ink = options.color ? rgbF(0.85f, 0.1f, 0.1f) : black;
				drawIn(rt, std::string(1, v), box(x0 + c * cw, y, x0 + (c + 1) * cw, y + rowH), mono(10, v == '1'), ink, TextAlign::Center, true);
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
		drawIn(rt, legend, box(kMargin, y, kPW - kMargin, y + 14), ui(9), mid);
		y += 22;
	}

	// One Karnaugh map at (x, top), with its groups; returns its height
	// (draws only when `target`).
	float kmap(ID2D1RenderTarget* target, float x, float top, const formula::KMapLayout& layout, const std::vector<std::string>& ins,
	           const std::vector<int>& vals, const formula::TwoLevel& f, bool ones) {
		const float cell = 46, left = 54, head = 34;
		const int rowsN = (int)layout.rowCodes.size(), colsN = (int)layout.colCodes.size();
		const Col dim = gray(0.42f), orange = options.color ? rgbF(0.8f, 0.45f, 0) : gray(0.3f);
		std::string rowNames, colNames;
		for (int i = 0; i < layout.rowVars; i++) rowNames += ins[i];
		for (int i = layout.rowVars; i < (int)ins.size(); i++) colNames += ins[i];
		if (target) {
			ID2D1RenderTarget* c = target;
			drawIn(c, rowNames, box(x, top + head - 22, x + left - 4, top + head - 4), ui(12, true), dim, TextAlign::Center, true);
			drawIn(c, colNames, box(x + left, top, x + left + colsN * cell, top + 16), ui(12, true), dim, TextAlign::Center, true);
			auto bits = [](int v, int count) { std::string b; for (int i = count - 1; i >= 0; i--) b += ((v >> i) & 1) ? '1' : '0'; return b; };
			for (int cc = 0; cc < colsN; cc++)
				drawIn(c, bits(layout.colCodes[cc], layout.colVars), box(x + left + cc * cell, top + head - 20, x + left + (cc + 1) * cell, top + head - 4),
				       mono(12), dim, TextAlign::Center, true);
			for (int r = 0; r < rowsN; r++)
				drawIn(c, bits(layout.rowCodes[r], layout.rowVars), box(x + left - 32, top + head + r * cell, x + left, top + head + (r + 1) * cell),
				       mono(12), dim, TextAlign::Center, true);
			for (int r = 0; r < rowsN; r++) {
				for (int cc = 0; cc < colsN; cc++) {
					const D2D1_RECT_F cb = box(x + left + cc * cell, top + head + r * cell, x + left + (cc + 1) * cell, top + head + (r + 1) * cell);
					strokeBox(c, cb, withAlpha(dim, 0.4f), 1);
					const int m = layout.minterm(r, cc);
					const int v = m < (int)vals.size() ? vals[m] : -1;
					const bool strong = v == (ones ? 1 : 0);
					drawIn(c, v == 1 ? "1" : v == 0 ? "0" : "X", cb, mono(17, strong), v < 0 ? orange : strong ? black : dim, TextAlign::Center, true);
					drawIn(c, strf("%d", m), box(cb.right - 16, cb.bottom - 14, cb.right - 3, cb.bottom - 2), ui(8.5f), withAlpha(dim, 0.7f),
					       TextAlign::Trailing, true);
				}
			}
			const D2D1_RECT_F grid = box(x + left, top + head, x + left + colsN * cell, top + head + rowsN * cell);
			strokeBox(c, grid, withAlpha(dim, 0.75f), 1.5f);
			// The groups, clipped to the map so a wrapping one reads as open-ended.
			static const Col colors[] = { rgbF(0.24f, 0.48f, 0.98f), rgbF(0.96f, 0.58f, 0.13f), rgbF(0.2f, 0.7f, 0.3f), rgbF(0.6f, 0.35f, 0.85f),
			                              rgbF(0.95f, 0.35f, 0.6f), rgbF(0.2f, 0.65f, 0.7f), rgbF(0.92f, 0.26f, 0.24f), rgbF(0.35f, 0.35f, 0.85f) };
			c->PushAxisAlignedClip(box(grid.left - 1, grid.top - 1, grid.right + 1, grid.bottom + 1), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			for (size_t g = 0; g < f.implicants.size(); g++) {
				const Col color = options.color ? colors[g % 8] : gray(0.2f);
				std::vector<std::pair<int, int>> rr, cc;
				layout.runs(f.implicants[g], rr, cc);
				const float inset = 4 + (g % 3) * 3.0f;
				for (auto& rrun : rr) {
					for (auto& crun : cc) {
						D2D1_RECT_F r = box(grid.left + crun.first * cell + inset, grid.top + rrun.first * cell + inset,
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
			c->PopAxisAlignedClip();
		}
		return head + rowsN * cell + 4;
	}

	void maps(const Table& t) {
		formula::KMapLayout layout;
		if (!formula::KMapLayout::make(t.inputs, layout)) {
			paragraph(t.inputs > 4 ? strf("Karnaugh maps are drawn for 2 to 4 switches; this circuit has %d.", t.inputs)
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
				drawIn(rt, out + ": " + (i == 0 ? "sum of products" : "product of sums"), box(x, y, x + colW, y + 16), ui(10.5f, true), gray(0.4f));
				kmap(rt, x, y + 20, layout, ins, vals, forms[i], i == 0);
				drawFormula(rt, out, forms[i], ins, x, y + 20 + mapH + 8, colW, 13, black);
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
			paragraph(strf("The recording is %d steps long; this shows the last %d.", length, perChunk * maxChunks), 10);
		}
		float fw, fh;
		reportTiming(nullptr, doc, 0, perChunk, options.color, fw, fh);
		const float s = std::min(kContentW / fw, 1.0f);   // every chunk at one scale
		while (from < length) {
			const int count = std::min(perChunk, length - from);
			float w, h;
			reportTiming(nullptr, doc, from, count, options.color, w, h);
			need(h * s);
			if (rt) {
				D2D1_MATRIX_3X2_F was;
				rt->GetTransform(&was);
				rt->SetTransform(D2D1::Matrix3x2F::Scale(s, s) * D2D1::Matrix3x2F::Translation(kMargin, y) * was);
				reportTiming(rt, doc, from, count, options.color, w, h);
				rt->SetTransform(was);
			}
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
		if (failed) return;
		footer();
		endPage();
	}
};

bool writeFile(const std::string& path, const std::string& bytes, std::string& error) {
	HANDLE h = CreateFileW(W(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) {
		error = GetLastError() == ERROR_SHARING_VIOLATION ? "That file is open in another program. Close it there, or save under another name."
		                                                   : "Windows wouldn't let CedarLogic write there. Try another folder.";
		return false;
	}
	bool ok = true;
	for (size_t at = 0; ok && at < bytes.size();) {
		const DWORD n = (DWORD)std::min<size_t>(bytes.size() - at, 1 << 24);
		DWORD wrote = 0;
		ok = WriteFile(h, bytes.data() + at, n, &wrote, nullptr) && wrote == n;
		at += n;
	}
	ok = CloseHandle(h) && ok;
	if (!ok) {
		DeleteFileW(W(path).c_str());
		error = "The file couldn't be written all the way (is the disk full?).";
	}
	return ok;
}

}  // namespace

bool writeLabReport(CLDocument* doc, const std::string& title, const LabReportOptions& options, const std::string& path, std::string& error) {
	Report r(doc, title, trimmed(prefs().studentName), options);
	r.build();
	if (r.failed) {
		error = r.error;
		return false;
	}
	return writeFile(path, r.file.finish(), error);
}

// ---- The sheet ----

namespace {

FormField noteField(const char* text, int lines = 1) {
	FormField x;
	x.kind = FormField::Note;
	x.label = text;
	x.lines = lines;
	return x;
}

FormField tickField(const char* label, bool on) {
	FormField x;
	x.kind = FormField::Check;
	x.label = label;
	x.value = on ? "1" : "";
	return x;
}

}  // namespace

void showExportReport(CircuitWindow* win) {
	CLDocument* doc = win->document();
	Prefs& p = prefs();
	const bool recorded = hasScopeRecording(doc);

	Form f;
	f.title = "Export Lab Report";
	f.width = 520;
	f.okText = "Export to File…";
	f.add(noteField("One PDF to hand in: your name, the circuit, and the parts of your analysis you pick.", 2));
	FormField nameBox;
	nameBox.kind = FormField::Text;
	nameBox.label = "Your name";
	nameBox.value = p.studentName;
	nameBox.placeholder = "First and last name";
	const int nameField = f.add(nameBox);
	f.add(noteField("Include"));
	const int circuitField = f.add(tickField("The circuit (a picture of each page)", p.reportCircuit));
	const int tableField = f.add(tickField("Truth table", p.reportTable));
	const int formulasField = f.add(tickField("Karnaugh maps and simplest formulas (2 to 4 switches)", p.reportFormulas));
	FormField timingBox = tickField("Timing diagram from the oscilloscope", p.reportTiming && recorded);
	if (!recorded) timingBox.tip = "The oscilloscope has no recording yet. Run the circuit with TO labels on its wires to make one.";
	const int timingField = f.add(timingBox);
	FormField styleBox;
	styleBox.kind = FormField::Choice;
	styleBox.label = "Output style";
	styleBox.choices = { "Color", "Black & White" };
	styleBox.value = p.reportColor ? "0" : "1";
	const int styleField = f.add(styleBox);

	f.onInit = [&](Form& form) { if (!recorded) form.enable(timingField, false); };
	f.onChange = [&](Form& form, int) { form.setProblem(""); };
	f.validate = [&](Form& form) -> std::string {
		const bool any = form.checked(circuitField) || form.checked(tableField) || form.checked(formulasField) || (recorded && form.checked(timingField));
		return any ? std::string() : std::string("Tick at least one thing to include.");
	};
	if (f.run(win->window()) != IDOK) return;

	LabReportOptions o;
	o.pump = true;
	o.circuit = !f.fields[circuitField].value.empty();
	o.truthTable = !f.fields[tableField].value.empty();
	o.formulas = !f.fields[formulasField].value.empty();
	o.timing = recorded && !f.fields[timingField].value.empty();
	o.color = f.fields[styleField].value == "0";
	p.studentName = trimmed(f.fields[nameField].value);
	p.reportCircuit = o.circuit;
	p.reportTable = o.truthTable;
	p.reportFormulas = o.formulas;
	if (recorded) p.reportTiming = o.timing;
	p.reportColor = o.color;
	p.save();

	const std::string title = win->titleText();
	const std::string file = chooseSaveFile(win->window(), "Export Lab Report", title + " lab report.pdf", { { "PDF documents (*.pdf)", "*.pdf" } }, ".pdf");
	if (file.empty()) return;
	// A few seconds of drawing and compressing: the wait cursor stays till it's done.
	const HCURSOR before = SetCursor(LoadCursor(nullptr, IDC_WAIT));
	std::string error;
	const bool ok = writeLabReport(doc, title, o, file, error);
	SetCursor(before);
	if (!ok) {
		showMessage(win->window(), Tone::Error, "The report couldn't be saved", error);
		return;
	}
	win->note("Exported " + baseName(file) + ".");
}
