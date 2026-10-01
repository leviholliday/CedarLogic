// The truth table window, as the Mac app's (TruthTableView.swift): the
// brand's band across the top with three tabs -- the table (click a column's
// name to rename it), a Karnaugh map for every light with the groups drawn
// on, and each light's simplest sum of products and product of sums, with
// NOT as a bar over the letter and a way to build them as gates. Copy puts
// the table on the clipboard as a tab-separated table that pastes into Word,
// Docs or a spreadsheet; Export saves it as CSV.

#include "Chrome.h"
#include "Dialogs.h"
#include "Formula.h"
#include "Window.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

namespace {

const D2D1_COLOR_F kInk = D2D1::ColorF(0.035f, 0.063f, 0.047f), kInkDeep = D2D1::ColorF(0.012f, 0.024f, 0.018f);
const D2D1_COLOR_F kNeon = D2D1::ColorF(0.22f, 1.0f, 0.42f), kNeonDeep = D2D1::ColorF(0.05f, 0.78f, 0.26f);
const D2D1_COLOR_F kBandDim = D2D1::ColorF(0.62f, 0.74f, 0.66f);
const D2D1_COLOR_F kOrange = D2D1::ColorF(0.96f, 0.58f, 0.13f), kBlue = D2D1::ColorF(0.24f, 0.48f, 0.98f),
                   kRed = D2D1::ColorF(0.92f, 0.26f, 0.24f);

// Text in any face (drawText keeps to the UI font).
struct Face {
	IDWriteTextFormat* f = nullptr;
	Face(const wchar_t* family, float size, bool italic = false, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL) {
		if (IDWriteFactory* dw = dwFactory())
			dw->CreateTextFormat(family, nullptr, weight, italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
			                     DWRITE_FONT_STRETCH_NORMAL, size, L"", &f);
		if (f) {
			f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
			f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
		}
	}
	~Face() { if (f) f->Release(); }
	float width(const std::string& s) const {
		IDWriteTextLayout* l = nullptr;
		const std::wstring w = W(s);
		if (!f || FAILED(dwFactory()->CreateTextLayout(w.c_str(), (UINT32)w.size(), f, 10000, 1000, &l))) return 0;
		DWRITE_TEXT_METRICS m = {};
		l->GetMetrics(&m);
		l->Release();
		return m.widthIncludingTrailingWhitespace;
	}
	void draw(ID2D1RenderTarget* rt, const std::string& s, float x, float y, D2D1_COLOR_F c) const {
		ID2D1SolidColorBrush* b = nullptr;
		if (!f || FAILED(rt->CreateSolidColorBrush(c, &b))) return;
		const std::wstring w = W(s);
		rt->DrawText(w.c_str(), (UINT32)w.size(), f, D2D1::RectF(x, y, x + 4000, y + 200), b);
		b->Release();
	}
};

// A formula as a textbook writes it: the letters in a serif italic, NOT as a
// bar over the letter. Returns the width drawn (or that would be, when rt is
// null).
float drawFormula(ID2D1RenderTarget* rt, const std::string& name, const formula::TwoLevel& t,
                  const std::vector<std::string>& names, float x, float y, float size, D2D1_COLOR_F ink) {
	const Face text(L"Segoe UI", size), letter(L"Cambria", size * 1.05f, true, DWRITE_FONT_WEIGHT_MEDIUM);
	float at = x;
	auto plain = [&](const std::string& s) {
		if (rt) text.draw(rt, s, at, y, ink);
		at += text.width(s);
	};
	auto lit = [&](const formula::Literal& l) {
		const std::string s = names[l.variable];
		const float w = letter.width(s);
		if (rt) {
			letter.draw(rt, s, at + 0.5f, y - size * 0.04f, ink);
			if (l.negated) fillRect(rt, D2D1::RectF(at + 1.5f, y + size * 0.08f, at + w, y + size * 0.08f + std::max(1.2f, size / 14)), ink);
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
	// One capital then small letters and digits reads as one name ("Cin");
	// anything else (say "CLK") would read as several: its first letter.
	bool restSmall = true;
	for (size_t i = 1; i < kept.size(); i++) if (isupper((unsigned char)kept[i])) restSmall = false;
	return kept.size() == 1 || restSmall ? kept : kept.substr(0, 1);
}

class TruthWindow {
public:
	CircuitWindow* owner = nullptr;
	std::vector<std::string> names;
	int inputs = 0;
	std::vector<std::string> rows;   // one char per column
	bool sequential = false;
	int unsettled = 0;
	int tab = 0;
	bool groupsOfOnes = true;
	std::string buildText;   // set when "Build This as a Circuit" was chosen

	void run() {
		static bool registered = false;
		if (!registered) {
			registered = true;
			WNDCLASSEXW wc = {};
			wc.cbSize = sizeof wc;
			wc.style = CS_DBLCLKS;
			wc.lpfnWndProc = proc;
			wc.hInstance = appInstance();
			wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
			wc.hIcon = LoadIconW(appInstance(), MAKEINTRESOURCEW(1));
			wc.lpszClassName = L"CedarLogicTruthTable";
			RegisterClassExW(&wc);
		}
		HWND o = owner->window();
		const UINT dpi = dpiOf(o);
		RECT orc;
		GetWindowRect(o, &orc);
		RECT r = { 0, 0, scaled(760, dpi), scaled(660, dpi) };
		AdjustWindowRectExForDpi(&r, WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, FALSE, 0, dpi);
		const int w = r.right - r.left, h = r.bottom - r.top;
		hwnd = CreateWindowExW(0, L"CedarLogicTruthTable", L"Truth Table", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
		                       (orc.left + orc.right - w) / 2, (orc.top + orc.bottom - h) / 2, w, h, o, nullptr, appInstance(), this);
		setDarkTitleBar(hwnd, true);
		EnableWindow(o, FALSE);
		ShowWindow(hwnd, SW_SHOW);
		MSG m = {};
		while (!done && GetMessageW(&m, nullptr, 0, 0) > 0) {
			TranslateMessage(&m);
			DispatchMessageW(&m);
		}
		if (m.message == WM_QUIT) PostQuitMessage((int)m.wParam);
		EnableWindow(o, TRUE);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
		DestroyWindow(hwnd);
		SetForegroundWindow(o);
	}

private:
	HWND hwnd = nullptr;
	WindowSurface surface;
	bool done = false;
	float scroll = 0, contentH = 0;
	int hot = -1;
	std::string copied;
	double copiedAt = 0;
	struct Hit { D2D1_RECT_F r; std::function<void()> act; };
	std::vector<Hit> hits;
	D2D1_RECT_F contentRect{};

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
	// Every light's simplest sum of products, in names the formula reader takes.
	std::string formulasForBuilding() const {
		std::vector<std::string> ins;
		for (int i = 0; i < inputs; i++) {
			std::string name = formulaName(names[i], std::string(1, (char)('A' + i)));
			if (std::find(ins.begin(), ins.end(), name) != ins.end()) name = std::string(1, (char)('A' + i));
			ins.push_back(name);
		}
		std::string out;
		for (int k = 0; k < outputs(); k++) {
			std::string o = formulaName(names[inputs + k], outputs() == 1 ? "F" : strf("F%d", k + 1));
			if (std::find(ins.begin(), ins.end(), o) != ins.end()) o = strf("F%d", k + 1);
			std::string list;
			for (size_t i = 0; i < ins.size(); i++) list += (i ? "," : "") + ins[i];
			out += (k ? "\n" : "") + o + "(" + list + ") = " + formula::simplest(true, inputs, values(k)).text(ins);
		}
		return out;
	}

	float scale() const { return dpiOf(hwnd) / 96.0f; }
	void redraw() { InvalidateRect(hwnd, nullptr, FALSE); }
	void copy(const std::string& key, const std::string& text) {
		setClipboardText(hwnd, text);
		copied = key;
		copiedAt = nowSeconds();
		SetTimer(hwnd, 1, 1500, nullptr);
		redraw();
	}

	// ---- Drawing ----

	void paint() {
		PAINTSTRUCT ps;
		BeginPaint(hwnd, &ps);
		ID2D1HwndRenderTarget* rt = surface.begin(hwnd);
		if (rt == nullptr) { EndPaint(hwnd, &ps); return; }
		hits.clear();
		const bool dark = prefs().dark;
		const D2D1_COLOR_F accent = chrome().accent();
		const D2D1_COLOR_F ink = dark ? D2D1::ColorF(0.93f, 0.93f, 0.93f) : D2D1::ColorF(0.1f, 0.1f, 0.1f);
		const D2D1_COLOR_F dim = dark ? D2D1::ColorF(0.62f, 0.62f, 0.62f) : D2D1::ColorF(0.42f, 0.42f, 0.42f);
		const D2D1_COLOR_F paper = dark ? D2D1::ColorF(0.075f, 0.085f, 0.1f) : D2D1::ColorF(0.965f, 0.97f, 0.975f);
		RECT rc;
		GetClientRect(hwnd, &rc);
		const float w = rc.right / scale(), h = rc.bottom / scale();
		rt->Clear(paper);

		// The band.
		{
			D2D1_GRADIENT_STOP s[2] = { { 0, kInk }, { 1, D2D1::ColorF(0.06f, 0.12f, 0.08f) } };
			ID2D1GradientStopCollection* stops = nullptr;
			if (SUCCEEDED(rt->CreateGradientStopCollection(s, 2, &stops))) {
				ID2D1LinearGradientBrush* b = nullptr;
				if (SUCCEEDED(rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(w, 104)), stops, &b))) {
					rt->FillRectangle(D2D1::RectF(0, 0, w, 104), b);
					b->Release();
				}
				stops->Release();
			}
			ID2D1SolidColorBrush* trace = nullptr;
			if (SUCCEEDED(rt->CreateSolidColorBrush(withAlpha(kNeon, 0.18f), &trace))) {
				for (int i = 0; i < 4; i++) {
					const float y = 14 + i * 15.0f;
					rt->DrawLine(D2D1::Point2F(w * 0.62f, y), D2D1::Point2F(w * 0.70f + i * 12, y), trace, 1.2f);
					rt->DrawLine(D2D1::Point2F(w * 0.70f + i * 12, y), D2D1::Point2F(w * 0.74f + i * 12, y + 9), trace, 1.2f);
					rt->DrawLine(D2D1::Point2F(w * 0.74f + i * 12, y + 9), D2D1::Point2F(w, y + 9), trace, 1.2f);
				}
				trace->Release();
			}
			drawText(rt, "Truth Table", D2D1::RectF(22, 22, 200, 50), 21, D2D1::ColorF(1, 1, 1, 1), TextAlign::Leading, true);
			drawText(rt, strf("%d switch%s → %d light%s · %d rows", n(), n() == 1 ? "" : "es", outputs(), outputs() == 1 ? "" : "s",
			                  (int)rows.size()),
			         D2D1::RectF(160, 27, w - 22, 50), 12.5f, kBandDim);
			const char* tabs[] = { "Truth Table", "Karnaugh Map", "Formulas" };
			float x = 22;
			for (int i = 0; i < 3; i++) {
				const float tw = textWidth(tabs[i], 12.5f, true) + 26;
				const D2D1_RECT_F r = D2D1::RectF(x, 62, x + tw, 90);
				const bool on = tab == i, isHot = hot == (int)hits.size();
				fillRound(rt, r, 14, on ? kNeon : D2D1::ColorF(1, 1, 1, isHot ? 0.16f : 0.09f));
				drawText(rt, tabs[i], r, 12.5f, on ? kInkDeep : D2D1::ColorF(1, 1, 1, 0.88f), TextAlign::Center, true);
				hits.push_back({ r, [this, i] { tab = i; scroll = 0; redraw(); } });
				x += tw + 8;
			}
		}

		// Notes about the table.
		float y = 104 + 14;
		if (sequential) {
			drawText(rt, "This page has clocks or flip-flops, so outputs can depend on what happened before.",
			         D2D1::RectF(22, y, w - 22, y + 18), 12, dim);
			y += 22;
		}
		if (unsettled > 0) {
			drawText(rt, strf("%d row%s never stopped changing (a clock or an oscillation), so those outputs are a snapshot.", unsettled,
			                  unsettled == 1 ? "" : "s"),
			         D2D1::RectF(22, y, w - 22, y + 18), 12, kOrange);
			y += 22;
		}

		// The footer.
		const float fy = h - 18 - 30;
		auto pill = [&](const std::string& label, float x, std::function<void()> act) {
			const float pw = textWidth(label, 12.5f) + 26;
			const D2D1_RECT_F r = D2D1::RectF(x, fy, x + pw, fy + 30);
			const bool isHot = hot == (int)hits.size();
			fillRound(rt, r, 15, withAlpha(ink, isHot ? 0.10f : 0.06f));
			strokeRound(rt, r, 15, withAlpha(ink, 0.09f));
			drawText(rt, label, r, 12.5f, ink, TextAlign::Center);
			hits.push_back({ r, act });
			return pw;
		};
		float fx = 22;
		fx += pill("Copy Table", fx, [this] { copy("table", tableText('\t')); }) + 10;
		fx += pill("Export as CSV…", fx, [this] {
			const std::string file = chooseSaveFile(hwnd, "Export as CSV", "Truth Table.csv", { { "CSV (*.csv)", "*.csv" } }, ".csv");
			if (file.empty()) return;
			HANDLE f = CreateFileW(W(file).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (f == INVALID_HANDLE_VALUE) { showMessage(hwnd, Tone::Warning, "Couldn't save it there", "Try another folder."); return; }
			const std::string text = tableText(',');
			DWORD wrote = 0;
			WriteFile(f, text.data(), (DWORD)text.size(), &wrote, nullptr);
			CloseHandle(f);
		}) + 10;
		if (copied == "table") drawText(rt, "Copied", D2D1::RectF(fx, fy, fx + 80, fy + 30), 12, dim);
		{
			const D2D1_RECT_F done_ = D2D1::RectF(w - 22 - 90, fy, w - 22, fy + 30);
			const bool isHot = hot == (int)hits.size();
			fillRound(rt, done_, 15, withAlpha(accent, isHot ? 1.0f : 0.92f));
			drawText(rt, "Done", done_, 13, D2D1::ColorF(1, 1, 1, 1), TextAlign::Center, true);
			hits.push_back({ done_, [this] { done = true; } });
		}

		// The tab's content, scrolling.
		contentRect = D2D1::RectF(22, y, w - 22, fy - 14);
		rt->PushAxisAlignedClip(contentRect, D2D1_ANTIALIAS_MODE_ALIASED);
		D2D1_MATRIX_3X2_F base;
		rt->GetTransform(&base);
		rt->SetTransform(D2D1::Matrix3x2F::Translation(0, -scroll) * base);
		const size_t fixedHits = hits.size();
		switch (tab) {
		case 1: contentH = drawKMaps(rt, contentRect, ink, dim, accent, dark); break;
		case 2: contentH = drawFormulas(rt, contentRect, ink, dim, accent, dark); break;
		default: contentH = drawTable(rt, contentRect, ink, dim, accent, dark); break;
		}
		rt->SetTransform(base);
		rt->PopAxisAlignedClip();
		// Hits in the content moved with it.
		for (size_t i = fixedHits; i < hits.size(); i++) {
			hits[i].r.top -= scroll;
			hits[i].r.bottom -= scroll;
			if (hits[i].r.bottom < contentRect.top || hits[i].r.top > contentRect.bottom) hits[i].r = D2D1::RectF(0, 0, 0, 0);
		}
		surface.end();
		EndPaint(hwnd, &ps);
	}

	float drawTable(ID2D1RenderTarget* rt, const D2D1_RECT_F& box, D2D1_COLOR_F ink, D2D1_COLOR_F dim, D2D1_COLOR_F accent, bool dark) {
		const float cw = 64, headH = 36, rowH = 28;
		const float tableW = cw * names.size(), x0 = box.left, y0 = box.top;
		const D2D1_COLOR_F on = dark ? kNeon : kNeonDeep;
		const D2D1_RECT_F card = D2D1::RectF(x0, y0, x0 + tableW, y0 + headH + rowH * rows.size());
		fillRound(rt, card, 12, dark ? D2D1::ColorF(1, 1, 1, 0.045f) : D2D1::ColorF(1, 1, 1, 1));
		for (size_t c = 0; c < names.size(); c++) {
			const bool out = (int)c >= inputs;
			const D2D1_RECT_F r = D2D1::RectF(x0 + c * cw, y0, x0 + (c + 1) * cw, y0 + headH);
			fillRect(rt, r, out ? withAlpha(accent, dark ? 0.13f : 0.10f) : withAlpha(ink, 0.05f));
			drawText(rt, names[c], r, 13, out ? accent : ink, TextAlign::Center, true);
			const int col = (int)c;
			hits.push_back({ r, [this, col] {
				std::string name = names[col];
				if (askText(hwnd, "Rename Column", "The column's name:", name) && !name.empty()) { names[col] = name; redraw(); }
			} });
		}
		const Face mono(L"Consolas", 14);
		const Face monoBold(L"Consolas", 14, false, DWRITE_FONT_WEIGHT_BOLD);
		for (size_t r = 0; r < rows.size(); r++) {
			const float y = y0 + headH + r * rowH;
			if (r % 2 == 1) fillRect(rt, D2D1::RectF(x0, y, x0 + tableW, y + rowH), withAlpha(ink, 0.03f));
			for (size_t c = 0; c < rows[r].size(); c++) {
				const char v = rows[r][c];
				const bool out = (int)c >= inputs;
				const D2D1_COLOR_F color = v == '1' ? on : v == '0' ? dim : v == 'Z' ? kBlue : v == '!' ? kRed : v == 'X' ? kOrange : dim;
				const float cx = x0 + c * cw;
				if (out && v != '0' && v != '-') fillRound(rt, D2D1::RectF(cx + 12, y + 3, cx + cw - 12, y + rowH - 3), 7, withAlpha(color, dark ? 0.16f : 0.12f));
				const std::string s(1, v);
				const Face& f = v == '1' ? monoBold : mono;
				f.draw(rt, s, cx + (cw - f.width(s)) / 2, y + 5, color);
			}
		}
		// The line between the switches and the lights.
		if (inputs > 0 && inputs < (int)names.size())
			fillRect(rt, D2D1::RectF(x0 + inputs * cw - 0.75f, y0, x0 + inputs * cw + 0.75f, card.bottom), withAlpha(accent, 0.55f));
		strokeRound(rt, card, 12, dark ? D2D1::ColorF(1, 1, 1, 0.09f) : D2D1::ColorF(0, 0, 0, 0.08f));
		const float ly = card.bottom + 10;
		drawText(rt, "Click a column's name to rename it.   1 on · X unknown · Z floating · ! conflict · – not connected",
		         D2D1::RectF(x0, ly, box.right, ly + 18), 11.5f, dim);
		return card.bottom + 32 - y0;
	}

	float drawKMaps(ID2D1RenderTarget* rt, const D2D1_RECT_F& box, D2D1_COLOR_F ink, D2D1_COLOR_F dim, D2D1_COLOR_F accent, bool dark) {
		formula::KMapLayout layout;
		if (!formula::KMapLayout::make(n(), layout)) {
			drawText(rt, n() > 4 ? strf("Karnaugh maps are drawn for 2 to 4 switches; this table has %d.", n()) : "A Karnaugh map needs at least 2 switches.",
			         D2D1::RectF(box.left, box.top + 40, box.right, box.top + 70), 13, dim, TextAlign::Center);
			return 80;
		}
		// Groups of 1s or of 0s.
		float x = box.left;
		drawText(rt, "Groups of", D2D1::RectF(x, box.top, x + 70, box.top + 28), 12.5f, dim);
		x += 72;
		const char* segs[] = { "1s (sum of products)", "0s (product of sums)" };
		float segW[2] = { textWidth(segs[0], 12) + 24, textWidth(segs[1], 12) + 24 };
		fillRound(rt, D2D1::RectF(x, box.top, x + segW[0] + segW[1] + 4, box.top + 28), 14, withAlpha(ink, 0.06f));
		for (int i = 0; i < 2; i++) {
			const D2D1_RECT_F r = D2D1::RectF(x + 2, box.top + 2, x + 2 + segW[i], box.top + 26);
			const bool on = (i == 0) == groupsOfOnes;
			if (on) fillRound(rt, r, 12, accent);
			drawText(rt, segs[i], r, 12, on ? chrome().onAccent() : ink, TextAlign::Center);
			hits.push_back({ r, [this, i] { groupsOfOnes = i == 0; redraw(); } });
			x += segW[i];
		}
		if (unknownCount() > 0) drawText(rt, "X = don't care", D2D1::RectF(box.right - 120, box.top, box.right, box.top + 28), 11.5f, dim, TextAlign::Trailing);

		// A card for each light.
		const float cell = 46, left = 54, top = 34;
		const int rowsN = (int)layout.rowCodes.size(), colsN = (int)layout.colCodes.size();
		const float mapW = left + colsN * cell + 4, mapH = top + rowsN * cell + 4;
		const float cardW = std::max(300.0f, mapW + 28), cardH = mapH + 28 + 36;
		const int perRow = std::max(1, (int)((box.right - box.left + 16) / (cardW + 16)));
		const std::vector<std::string> ins = inputNames();
		static const D2D1_COLOR_F groupColors[] = { kBlue, kOrange, D2D1::ColorF(0.2f, 0.7f, 0.3f), D2D1::ColorF(0.6f, 0.35f, 0.85f),
		                                            D2D1::ColorF(0.95f, 0.35f, 0.6f), D2D1::ColorF(0.2f, 0.65f, 0.7f), kRed,
		                                            D2D1::ColorF(0.35f, 0.35f, 0.85f) };
		const Face mono(L"Consolas", 12), cellFace(L"Consolas", 17), cellBold(L"Consolas", 17, false, DWRITE_FONT_WEIGHT_BOLD);
		float bottom = box.top + 40;
		for (int k = 0; k < outputs(); k++) {
			const float cx = box.left + (k % perRow) * (cardW + 16), cy = box.top + 40 + (k / perRow) * (cardH + 16);
			const D2D1_RECT_F card = D2D1::RectF(cx, cy, cx + cardW, cy + cardH);
			fillRound(rt, card, 12, dark ? D2D1::ColorF(1, 1, 1, 0.045f) : D2D1::ColorF(1, 1, 1, 1));
			strokeRound(rt, card, 12, dark ? D2D1::ColorF(1, 1, 1, 0.09f) : D2D1::ColorF(0, 0, 0, 0.08f));
			const std::vector<int> vals = values(k);
			const formula::TwoLevel f = formula::simplest(groupsOfOnes, n(), vals);
			drawFormula(rt, names[inputs + k], f, ins, cx + 14, cy + 12, 16, ink);
			const float mx = cx + 14, my = cy + 50;
			// Headers: the variables, then each row's and column's bits.
			std::string rowNames, colNames;
			for (int i = 0; i < layout.rowVars; i++) rowNames += ins[i];
			for (int i = layout.rowVars; i < n(); i++) colNames += ins[i];
			drawText(rt, rowNames, D2D1::RectF(mx, my + top - 22, mx + left - 4, my + top - 4), 12, dim, TextAlign::Center, true);
			drawText(rt, colNames, D2D1::RectF(mx + left, my, mx + left + colsN * cell, my + 16), 12, dim, TextAlign::Center, true);
			auto bits = [](int v, int count) { std::string s; for (int i = count - 1; i >= 0; i--) s += ((v >> i) & 1) ? '1' : '0'; return s; };
			for (int c = 0; c < colsN; c++) {
				const std::string s = bits(layout.colCodes[c], layout.colVars);
				mono.draw(rt, s, mx + left + (c + 0.5f) * cell - mono.width(s) / 2, my + top - 18, dim);
			}
			for (int r = 0; r < rowsN; r++) {
				const std::string s = bits(layout.rowCodes[r], layout.rowVars);
				mono.draw(rt, s, mx + left - 16 - mono.width(s) / 2, my + top + (r + 0.5f) * cell - 8, dim);
			}
			ID2D1SolidColorBrush* br = nullptr;
			rt->CreateSolidColorBrush(withAlpha(dim, 0.4f), &br);
			for (int r = 0; r < rowsN; r++) {
				for (int c = 0; c < colsN; c++) {
					const D2D1_RECT_F cr = D2D1::RectF(mx + left + c * cell, my + top + r * cell, mx + left + (c + 1) * cell, my + top + (r + 1) * cell);
					if (br) rt->DrawRectangle(cr, br, 1);
					const int m = layout.minterm(r, c);
					const int v = m < (int)vals.size() ? vals[m] : -1;
					const std::string s = v == 1 ? "1" : v == 0 ? "0" : "X";
					const bool strong = v == (groupsOfOnes ? 1 : 0);
					const Face& face = strong ? cellBold : cellFace;
					face.draw(rt, s, (cr.left + cr.right) / 2 - face.width(s) / 2, (cr.top + cr.bottom) / 2 - 11, v < 0 ? kOrange : strong ? ink : dim);
					drawText(rt, strf("%d", m), D2D1::RectF(cr.right - 16, cr.bottom - 14, cr.right - 3, cr.bottom - 2), 8.5f, withAlpha(dim, 0.7f),
					         TextAlign::Trailing);
				}
			}
			const D2D1_RECT_F grid = D2D1::RectF(mx + left, my + top, mx + left + colsN * cell, my + top + rowsN * cell);
			if (br) { br->SetColor(withAlpha(dim, 0.75f)); rt->DrawRectangle(grid, br, 1.5f); br->Release(); }
			// The groups, clipped to the map so a wrapping one reads as open-ended.
			rt->PushAxisAlignedClip(D2D1::RectF(grid.left - 1, grid.top - 1, grid.right + 1, grid.bottom + 1), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			for (size_t g = 0; g < f.implicants.size(); g++) {
				const D2D1_COLOR_F color = groupColors[g % 8];
				std::vector<std::pair<int, int>> rr, cc;
				layout.runs(f.implicants[g], rr, cc);
				const float inset = 4 + (g % 3) * 3.0f;
				for (auto& rrun : rr) {
					for (auto& crun : cc) {
						D2D1_RECT_F r = D2D1::RectF(grid.left + crun.first * cell + inset, grid.top + rrun.first * cell + inset,
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
						fillRound(rt, r, rad, withAlpha(color, 0.10f));
						strokeRound(rt, r, rad, withAlpha(color, 0.85f), 2);
					}
				}
			}
			rt->PopAxisAlignedClip();
			bottom = std::max(bottom, card.bottom);
		}
		return bottom + 8 - box.top;
	}

	float drawFormulas(ID2D1RenderTarget* rt, const D2D1_RECT_F& box, D2D1_COLOR_F ink, D2D1_COLOR_F dim, D2D1_COLOR_F accent, bool dark) {
		const std::vector<std::string> ins = inputNames();
		float y = box.top;
		for (int k = 0; k < outputs(); k++) {
			const D2D1_RECT_F card = D2D1::RectF(box.left, y, box.right, y + 138);
			fillRound(rt, card, 12, dark ? D2D1::ColorF(1, 1, 1, 0.045f) : D2D1::ColorF(1, 1, 1, 1));
			strokeRound(rt, card, 12, dark ? D2D1::ColorF(1, 1, 1, 0.09f) : D2D1::ColorF(0, 0, 0, 0.08f));
			for (int form = 0; form < 2; form++) {
				const bool sop = form == 0;
				const float ry = y + 14 + form * 62;
				drawText(rt, sop ? "SIMPLEST SUM OF PRODUCTS" : "SIMPLEST PRODUCT OF SUMS", D2D1::RectF(card.left + 14, ry, card.right - 80, ry + 14),
				         10.5f, dim, TextAlign::Leading, true);
				const formula::TwoLevel f = formula::simplest(sop, n(), values(k));
				const std::string key = strf("%d-%d", k, form), text = names[inputs + k] + " = " + f.text(ins);
				const D2D1_RECT_F copyR = D2D1::RectF(card.right - 70, ry - 2, card.right - 14, ry + 16);
				drawText(rt, copied == key ? "Copied" : "Copy", copyR, 11.5f, accent, TextAlign::Trailing, true);
				hits.push_back({ copyR, [this, key, text] { copy(key, text); } });
				drawFormula(rt, names[inputs + k], f, ins, card.left + 14, ry + 18, 19, ink);
				if (sop) fillRect(rt, D2D1::RectF(card.left + 14, ry + 54, card.right - 14, ry + 55), withAlpha(ink, 0.08f));
			}
			y = card.bottom + 14;
		}
		if (unknownCount() > 0) {
			drawText(rt, "Rows with no clear 0 or 1 count as don't-cares: the formulas treat them as either.", D2D1::RectF(box.left, y, box.right, y + 18), 11.5f, dim);
			y += 26;
		}
		const float bw = textWidth("Build This as a Circuit…", 12.5f) + 26;
		const D2D1_RECT_F b = D2D1::RectF(box.left, y, box.left + bw, y + 30);
		fillRound(rt, b, 15, withAlpha(ink, 0.06f));
		strokeRound(rt, b, 15, withAlpha(ink, 0.09f));
		drawText(rt, "Build This as a Circuit…", b, 12.5f, ink, TextAlign::Center);
		hits.push_back({ b, [this] { buildText = formulasForBuilding(); done = true; } });
		return y + 40 - box.top;
	}

	// ---- Messages ----

	static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
		if (msg == WM_NCCREATE) {
			TruthWindow* t = static_cast<TruthWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
			t->hwnd = h;
			SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)t);
		}
		TruthWindow* t = reinterpret_cast<TruthWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
		if (t == nullptr) return DefWindowProcW(h, msg, wp, lp);
		LRESULT r = 0;
		bool handled = false;
		guarded("the truth table", [&] { handled = t->handle(msg, wp, lp, r); });
		return handled ? r : DefWindowProcW(h, msg, wp, lp);
	}

	int hitAt(float x, float y) const {
		for (int i = (int)hits.size() - 1; i >= 0; i--) if (inRect(hits[i].r, x, y)) return i;
		return -1;
	}

	bool handle(UINT msg, WPARAM wp, LPARAM lp, LRESULT& r) {
		const float x = GET_X_LPARAM(lp) / scale(), y = GET_Y_LPARAM(lp) / scale();
		switch (msg) {
		case WM_PAINT: paint(); return true;
		case WM_ERASEBKGND: r = 1; return true;
		case WM_SIZE: redraw(); return true;
		case WM_CLOSE: done = true; return true;
		case WM_GETMINMAXINFO: {
			MINMAXINFO* m = reinterpret_cast<MINMAXINFO*>(lp);
			m->ptMinTrackSize = { scaled(620, dpiOf(hwnd)), scaled(480, dpiOf(hwnd)) };
			return true;
		}
		case WM_TIMER:
			KillTimer(hwnd, 1);
			copied.clear();
			redraw();
			return true;
		case WM_MOUSEMOVE: {
			TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, hwnd, 0 };
			TrackMouseEvent(&t);
			const int i = hitAt(x, y);
			if (i != hot) { hot = i; redraw(); }
			SetCursor(LoadCursor(nullptr, i >= 0 ? IDC_HAND : IDC_ARROW));
			return true;
		}
		case WM_MOUSELEAVE: hot = -1; redraw(); return true;
		case WM_LBUTTONUP: {
			const int i = hitAt(x, y);
			if (i >= 0) { auto act = hits[i].act; act(); redraw(); }
			return true;
		}
		case WM_MOUSEWHEEL: {
			const float view = contentRect.bottom - contentRect.top;
			scroll = std::max(0.0f, std::min(scroll - GET_WHEEL_DELTA_WPARAM(wp) * 56.0f / WHEEL_DELTA, contentH - view));
			redraw();
			return true;
		}
		case WM_KEYDOWN:
			if (wp == VK_ESCAPE || wp == VK_RETURN) { done = true; return true; }
			if (wp == VK_TAB) { tab = (tab + ((GetKeyState(VK_SHIFT) & 0x8000) ? 2 : 1)) % 3; scroll = 0; redraw(); return true; }
			if (wp == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) { copy("table", tableText('\t')); return true; }
			return false;
		}
		return false;
	}
};

}  // namespace

void showTruthTable(CircuitWindow* w, int page) {
	char err[512] = "";
	CLTruthTable* tt = cl_truth_table(w->document(), page, err, sizeof err);
	if (tt == nullptr) {
		showMessage(w->window(), Tone::Info, "A truth table couldn't be made for this page",
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
	t.tab = prefs().truthTab;
	t.run();
	if (prefs().truthTab != t.tab) { prefs().truthTab = t.tab; prefs().save(); }
	if (!t.buildText.empty()) {
		prefs().lastFormula = t.buildText;
		showBuildFormula(w);
	}
}
