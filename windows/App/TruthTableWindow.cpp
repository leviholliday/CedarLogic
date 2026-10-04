// The truth table window, as the Mac app's (TruthTableView.swift): the
// brand's band across the top with three tabs -- the table (click a column's
// name to rename it), a Karnaugh map for every light with the groups drawn
// on, and each light's simplest sum of products and product of sums, with
// NOT as a bar over the letter and a way to build them as gates. And Check:
// the lights against a formula or truth table the assignment gives (the
// core's cl_check_*, as the Mac and Linux apps), wrong rows shown. Copy puts
// the table on the clipboard as a tab-separated table that pastes into Word,
// Docs or a spreadsheet; Export saves it as CSV.

#include "Brand.h"
#include "Chrome.h"
#include "Dialogs.h"
#include "Formula.h"
#include "Window.h"

#include <algorithm>
#include <cmath>
#include <fstream>
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

// ---- The last check of each circuit ----
// Saved circuits' in %APPDATA%\CedarLogic\checks.ini (a section per file and
// page); unsaved ones' only while the app runs.
struct SavedCheck {
	int kind = 0;   // 0 a formula, 1 a truth table
	std::string text;
	std::map<std::string, std::string> names;   // asked-for name -> the circuit's
};
std::map<std::string, SavedCheck> gUnsavedChecks;
// --check: what the Check tab opens with for CI's pictures (not kept).
std::string gCheckForTesting;
bool gHasCheckForTesting = false;

std::string checksFile() { return settingsDir() + "\\checks.ini"; }
std::string checkSection(std::string key) {
	for (char& c : key) if (c == '[' || c == ']' || c == '\n' || c == '\r') c = '_';
	return key;
}
// Several lines on one line of the file: new lines kept as \x1F, as native.ini keeps them.
std::string oneLine(const std::string& text) {
	std::string one;
	for (char c : text) if (c != '\r') one += c == '\n' ? '\x1F' : c;
	return one;
}
std::string manyLines(std::string text) {
	for (char& c : text) if (c == '\x1F') c = '\n';
	return text;
}
bool blank(const std::string& s) {
	for (char c : s) if (!isspace((unsigned char)c)) return false;
	return true;
}
// The text box wants \r\n between lines.
std::string crlf(const std::string& s) {
	std::string out;
	for (char c : s) {
		if (c == '\r') continue;
		if (c == '\n') out += '\r';
		out += c;
	}
	return out;
}

using CheckSections = std::map<std::string, std::map<std::string, std::string>>;
CheckSections readChecks() {
	CheckSections out;
	std::ifstream in(W(checksFile()).c_str(), std::ios::binary);
	std::string line, section;
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.size() >= 2 && line.front() == '[' && line.back() == ']') {
			section = line.substr(1, line.size() - 2);
			out[section];
			continue;
		}
		const size_t eq = line.find('=');
		if (section.empty() || eq == std::string::npos) continue;
		out[section][line.substr(0, eq)] = line.substr(eq + 1);
	}
	return out;
}

bool loadCheck(const std::string& key, SavedCheck& out) {
	if (key.empty()) return false;
	if (key.rfind("unsaved-", 0) == 0) {
		auto it = gUnsavedChecks.find(key);
		if (it == gUnsavedChecks.end()) return false;
		out = it->second;
		return true;
	}
	const CheckSections all = readChecks();
	auto it = all.find(checkSection(key));
	if (it == all.end()) return false;
	auto value = [&](const char* name) {
		auto v = it->second.find(name);
		return v == it->second.end() ? std::string() : v->second;
	};
	out.kind = value("kind") == "1" ? 1 : 0;
	out.text = manyLines(value("text"));
	const std::string names = manyLines(value("names"));
	size_t at = 0;
	while (at < names.size()) {
		size_t end = names.find('\n', at);
		if (end == std::string::npos) end = names.size();
		const std::string line = names.substr(at, end - at);
		const size_t tab = line.find('\t');
		if (tab != std::string::npos) out.names[line.substr(0, tab)] = line.substr(tab + 1);
		at = end + 1;
	}
	return true;
}

void saveCheck(const std::string& key, const SavedCheck& c) {
	if (key.empty()) return;
	if (key.rfind("unsaved-", 0) == 0) { gUnsavedChecks[key] = c; return; }
	CheckSections all = readChecks();
	std::map<std::string, std::string>& s = all[checkSection(key)];
	s["kind"] = c.kind == 1 ? "1" : "0";
	s["text"] = oneLine(c.text);
	std::string names;
	for (auto& n : c.names) names += n.first + "\t" + n.second + "\n";
	s["names"] = oneLine(names);
	std::string text;
	for (auto& section : all) {
		text += "[" + section.first + "]\n";
		for (auto& kv : section.second) text += kv.first + "=" + kv.second + "\n";
	}
	// A temporary beside it, then moved over: a crash mid-write can't leave half a file.
	const std::wstring file = W(checksFile()), temp = file + L".new";
	HANDLE f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f == INVALID_HANDLE_VALUE) return;
	DWORD wrote = 0;
	const bool ok = WriteFile(f, text.data(), (DWORD)text.size(), &wrote, nullptr) && wrote == text.size();
	CloseHandle(f);
	if (!ok || !MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING)) DeleteFileW(temp.c_str());
}

// Names match whatever their case or spacing, as the core matches them.
std::string nameKey(const std::string& s) {
	std::string k;
	for (unsigned char c : s) if (isalnum(c) || c >= 0x80) k += (char)tolower(c);
	return k;
}

const int kCheckTab = 3, kTabs = 4;
const int kEditId = 10;
// Icon font glyphs for the verdict and the notes.
const wchar_t kGlyphCheck = 0xE73E, kGlyphCross = 0xE711, kGlyphInfo = 0xE946, kGlyphWarning = 0xE7BA, kGlyphError = 0xEA39;

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
	// Check: what the assignment gives (kept per circuit and page).
	std::string checkKey;
	SavedCheck want;

	~TruthWindow() {
		if (check) cl_check_free(check);
		if (editFont) DeleteObject(editFont);
		if (editBrush) DeleteObject(editBrush);
	}

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
		hwnd = CreateWindowExW(0, L"CedarLogicTruthTable", L"Truth Table", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN,
		                       (orc.left + orc.right - w) / 2, (orc.top + orc.bottom - h) / 2, w, h, o, nullptr, appInstance(), this);
		setDarkTitleBar(hwnd, true);
		// Check's text box: the formula or table the assignment gives (placed
		// and shown while the Check tab is).
		edit = CreateWindowExW(0, L"EDIT", W(crlf(want.text)).c_str(),
		                       WS_CHILD | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | ES_AUTOHSCROLL, 0, 0, 10,
		                       10, hwnd, (HMENU)(INT_PTR)kEditId, appInstance(), nullptr);
		SendMessageW(edit, EM_SETLIMITTEXT, 0, 0);
		SetWindowSubclass(edit, editProc, 1, (DWORD_PTR)this);
		if (prefs().dark) darkenControl(edit, true, L"CFD");
		recheck();
		focusEditor = tab == kCheckTab;
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
	// Check: how the circuit compares, and its text box.
	CLCheck* check = nullptr;
	std::string checkError;   // the formula couldn't be read
	bool onlyWrong = false, scrollToWrong = false;
	HWND edit = nullptr;
	HWND focusBefore = nullptr;   // what had the keyboard when the window went to the back
	bool focusEditor = false;     // give the box the keyboard once it's shown
	RECT placed{};
	HFONT editFont = nullptr;
	UINT editFontDpi = 0;
	HBRUSH editBrush = nullptr;
	COLORREF editBack = 0;

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
		// CSV: a name with a comma, quote or line break in quotes.
		auto field = [sep](const std::string& f) {
			if (sep != ',' || f.find_first_of(",\"\r\n") == std::string::npos) return f;
			std::string q = "\"";
			for (char ch : f) { if (ch == '"') q += '"'; q += ch; }
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
	// Every light's simplest sum of products, in names the formula reader takes.
	std::string formulasForBuilding() const {
		std::vector<std::string> ins;
		for (int i = 0; i < inputs; i++) {
			std::string name = formulaName(names[i], std::string(1, (char)('A' + i)));
			if (std::find(ins.begin(), ins.end(), name) != ins.end()) name = std::string(1, (char)('A' + i));
			ins.push_back(name);
		}
		std::string out;
		std::vector<std::string> used = ins;
		for (int k = 0; k < outputs(); k++) {
			std::string o = formulaName(names[inputs + k], outputs() == 1 ? "F" : strf("F%d", k + 1));
			// Taken by an input or another light (OUT1 and OUT2 both read
			// as O): F1, F2... instead, the first that's free.
			for (int m = k + 1; std::find(used.begin(), used.end(), o) != used.end(); m++) o = strf("F%d", m);
			used.push_back(o);
			std::string list;
			for (size_t i = 0; i < ins.size(); i++) list += (i ? "," : "") + ins[i];
			out += (k ? "\n" : "") + o + "(" + list + ") = " + formula::simplest(true, inputs, values(k)).text(ins);
		}
		return out;
	}

	float scale() const { return dpiOf(hwnd) / 96.0f; }
	void redraw() { if (hwnd) InvalidateRect(hwnd, nullptr, FALSE); }
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
			const char* tabs[] = { "Truth Table", "Karnaugh Map", "Formulas", "Check" };
			float x = 22;
			for (int i = 0; i < kTabs; i++) {
				const float tw = textWidth(tabs[i], 12.5f, true) + 26;
				const D2D1_RECT_F r = D2D1::RectF(x, 62, x + tw, 90);
				const bool on = tab == i, isHot = hot == (int)hits.size();
				fillRound(rt, r, 14, on ? kNeon : D2D1::ColorF(1, 1, 1, isHot ? 0.16f : 0.09f));
				drawText(rt, tabs[i], r, 12.5f, on ? kInkDeep : D2D1::ColorF(1, 1, 1, 0.88f), TextAlign::Center, true);
				hits.push_back({ r, [this, i] { setTab(i); } });
				x += tw + 8;
			}
		}

		// Notes about the table (Check has the core's own).
		float y = 104 + 14;
		if (sequential && tab != kCheckTab) {
			drawText(rt, "This page has clocks or flip-flops, so outputs can depend on what happened before.",
			         D2D1::RectF(22, y, w - 22, y + 18), 12, dim);
			y += 22;
		}
		if (unsettled > 0 && tab != kCheckTab) {
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
			// UTF-8 marked as such, so Excel shows names like A→B as typed.
			const std::string text = "\xEF\xBB\xBF" + tableText(',');
			DWORD wrote = 0;
			const bool ok = WriteFile(f, text.data(), (DWORD)text.size(), &wrote, nullptr) && wrote == text.size();
			CloseHandle(f);
			if (!ok) showMessage(hwnd, Tone::Warning, "Couldn't save it there", "Try another folder.");
		}) + 10;
		if (copied == "table") drawText(rt, "Copied", D2D1::RectF(fx, fy, fx + 80, fy + 30), 12, dim);
		{
			const D2D1_RECT_F done_ = D2D1::RectF(w - 22 - 90, fy, w - 22, fy + 30);
			const bool isHot = hot == (int)hits.size();
			fillRound(rt, done_, 15, withAlpha(accent, isHot ? 1.0f : 0.92f));
			drawText(rt, "Done", done_, 13, D2D1::ColorF(1, 1, 1, 1), TextAlign::Center, true);
			hits.push_back({ done_, [this] { done = true; } });
		}

		// Check's text box stays put above what scrolls.
		if (tab == kCheckTab) y = drawCheckControls(rt, y, w, ink, dim, accent, dark);
		else placeEditor(false);

		// The tab's content, scrolling.
		contentRect = D2D1::RectF(22, y, w - 22, fy - 14);
		{
			const float view = contentRect.bottom - contentRect.top;
			scroll = std::max(0.0f, std::min(scroll, std::max(0.0f, contentH - view)));
		}
		rt->PushAxisAlignedClip(contentRect, D2D1_ANTIALIAS_MODE_ALIASED);
		D2D1_MATRIX_3X2_F base;
		rt->GetTransform(&base);
		rt->SetTransform(D2D1::Matrix3x2F::Translation(0, -scroll) * base);
		const size_t fixedHits = hits.size();
		switch (tab) {
		case 1: contentH = drawKMaps(rt, contentRect, ink, dim, accent, dark); break;
		case 2: contentH = drawFormulas(rt, contentRect, ink, dim, accent, dark); break;
		case kCheckTab: contentH = drawCheck(rt, contentRect, ink, dim, accent, dark); break;
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
				if (askText(hwnd, "Rename Column", "The column's name:", name) && !name.empty()) { names[col] = name; recheck(); }
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

	// ---- Check ----

	void setTab(int i) {
		tab = i;
		scroll = 0;
		if (tab == kCheckTab) focusEditor = true;
		else if (edit && GetFocus() == edit) SetFocus(hwnd);
		redraw();
	}

	void recheck() {
		if (check) { cl_check_free(check); check = nullptr; }
		checkError.clear();
		if (!blank(want.text)) {
			CLTruthTable* tt = cl_tt_new(inputs, sequential, unsettled);
			for (const std::string& name : names) cl_tt_add_name(tt, name.c_str());
			for (const std::string& row : rows) cl_tt_add_row(tt, row.c_str());
			std::string mapping;
			for (auto& n : want.names) mapping += n.first + "\t" + n.second + "\n";
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
		redraw();
	}

	// The box's text changed (typed, pasted, or filled in): check again.
	void textChanged() {
		std::string text = windowText(edit);
		text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
		if (text == want.text) return;
		want.text = text;
		InvalidateRect(edit, nullptr, TRUE);   // its hint comes and goes
		recheck();
	}

	// The box's paper, under it and in it.
	static D2D1_COLOR_F boxPaper(bool dark) { return dark ? D2D1::ColorF(0.118f, 0.126f, 0.141f) : D2D1::ColorF(1, 1, 1); }
	static COLORREF gdiColor(D2D1_COLOR_F c) {
		return RGB((int)std::lround(c.r * 255), (int)std::lround(c.g * 255), (int)std::lround(c.b * 255));
	}

	HFONT fontFor(UINT dpi) {
		if (editFont && editFontDpi == dpi) return editFont;
		if (editFont) DeleteObject(editFont);
		LOGFONTW lf = {};
		lf.lfHeight = -MulDiv(13, (int)dpi, 96);
		lf.lfWeight = FW_NORMAL;
		lf.lfQuality = CLEARTYPE_QUALITY;
		wcscpy(lf.lfFaceName, L"Consolas");
		editFont = CreateFontIndirectW(&lf);
		editFontDpi = dpi;
		return editFont;
	}

	// The text box over the drawn one (box, in points), or hidden.
	void placeEditor(bool show, const D2D1_RECT_F& box = D2D1_RECT_F{}) {
		if (edit == nullptr) return;
		if (!show) {
			if (IsWindowVisible(edit)) {
				if (GetFocus() == edit) SetFocus(hwnd);
				ShowWindow(edit, SW_HIDE);
			}
			return;
		}
		const float s = scale();
		const UINT dpi = dpiOf(hwnd);
		const RECT to = { (LONG)std::lround((box.left + 10) * s), (LONG)std::lround((box.top + 7) * s), (LONG)std::lround((box.right - 3) * s),
		                  (LONG)std::lround((box.bottom - 3) * s) };
		if (!EqualRect(&to, &placed)) {
			placed = to;
			SetWindowPos(edit, nullptr, to.left, to.top, to.right - to.left, to.bottom - to.top, SWP_NOZORDER | SWP_NOACTIVATE);
		}
		if (editFontDpi != dpi) SendMessageW(edit, WM_SETFONT, (WPARAM)fontFor(dpi), TRUE);
		if (!IsWindowVisible(edit)) ShowWindow(edit, SW_SHOW);
		if (focusEditor) {
			focusEditor = false;
			SetFocus(edit);
		}
	}

	// While the box is empty: what to type, dimmed (a multi-line box has no
	// cue banner of its own).
	void paintHint(HWND h) {
		const bool dark = prefs().dark;
		const D2D1_COLOR_F dim = dark ? D2D1::ColorF(0.62f, 0.62f, 0.62f) : D2D1::ColorF(0.42f, 0.42f, 0.42f), paper = boxPaper(dark);
		const D2D1_COLOR_F mix = D2D1::ColorF(paper.r + (dim.r - paper.r) * 0.75f, paper.g + (dim.g - paper.g) * 0.75f, paper.b + (dim.b - paper.b) * 0.75f);
		const std::wstring hint = W(want.kind == 0 ? "S = A ^ B ^ Cin\nCout = AB + Cin(A ^ B)        or   F(A,B,C) = Σm(1,3,5) + d(7)"
		                                           : "A  B  Cin | S  Cout\n0  0  0   | 0  0\n0  0  1   | 1  0\n…  (X for don't care)");
		RECT r = {};
		SendMessageW(h, EM_GETRECT, 0, (LPARAM)&r);
		HideCaret(h);   // drawn over, it would blink back inverted
		HDC dc = GetDC(h);
		HGDIOBJ old = SelectObject(dc, (HGDIOBJ)SendMessageW(h, WM_GETFONT, 0, 0));
		SetBkMode(dc, TRANSPARENT);
		SetTextColor(dc, gdiColor(mix));
		DrawTextW(dc, hint.c_str(), (int)hint.size(), &r, DT_LEFT | DT_TOP | DT_NOPREFIX);
		SelectObject(dc, old);
		ReleaseDC(h, dc);
		ShowCaret(h);
	}

	static LRESULT CALLBACK editProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
		TruthWindow* t = reinterpret_cast<TruthWindow*>(data);
		const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
		switch (msg) {
		case WM_KEYDOWN:
			// Escape closes the window, as anywhere in it; Ctrl+Tab goes on to the next tab.
			if (wp == VK_ESCAPE) { t->done = true; return 0; }
			if (wp == 'A' && ctrl) { SendMessageW(h, EM_SETSEL, 0, -1); return 0; }
			if (wp == VK_TAB && ctrl) { t->setTab((t->tab + ((GetKeyState(VK_SHIFT) & 0x8000) ? kTabs - 1 : 1)) % kTabs); return 0; }
			break;
		case WM_CHAR:
			if (wp == 0x1B || (ctrl && (wp == 0x01 || wp == '\t' || wp == 0x7F))) return 0;   // no beep, no stray characters
			break;
		case WM_MOUSEWHEEL: {
			// Not over the box: the window's to scroll.
			const POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
			if (WindowFromPoint(p) != h) return SendMessageW(t->hwnd, msg, wp, lp);
			break;
		}
		case WM_PAINT: {
			const LRESULT r = DefSubclassProc(h, msg, wp, lp);
			if (GetWindowTextLengthW(h) == 0) t->paintHint(h);
			return r;
		}
		case WM_NCDESTROY:
			RemoveWindowSubclass(h, editProc, 1);
			break;
		}
		return DefSubclassProc(h, msg, wp, lp);
	}

	// What the assignment gives: a formula or a table, and the box to type it in.
	float drawCheckControls(ID2D1RenderTarget* rt, float y, float w, D2D1_COLOR_F ink, D2D1_COLOR_F dim, D2D1_COLOR_F accent, bool dark) {
		float x = 22;
		const char* intro = "The assignment gives";
		drawText(rt, intro, D2D1::RectF(x, y, x + 200, y + 28), 12.5f, dim);
		x += textWidth(intro, 12.5f) + 10;
		const char* segs[] = { "A formula", "A truth table" };
		const float segW[2] = { textWidth(segs[0], 12) + 24, textWidth(segs[1], 12) + 24 };
		fillRound(rt, D2D1::RectF(x, y, x + segW[0] + segW[1] + 4, y + 28), 14, withAlpha(ink, 0.06f));
		for (int i = 0; i < 2; i++) {
			const D2D1_RECT_F r = D2D1::RectF(x + 2, y + 2, x + 2 + segW[i], y + 26);
			const bool on = want.kind == i, isHot = hot == (int)hits.size();
			if (on) fillRound(rt, r, 12, accent);
			else if (isHot) fillRound(rt, r, 12, withAlpha(ink, 0.06f));
			drawText(rt, segs[i], r, 12, on ? chrome().onAccent() : ink, TextAlign::Center);
			hits.push_back({ r, [this, i] {
				if (want.kind == i) return;
				want.kind = i;
				InvalidateRect(edit, nullptr, TRUE);   // the other hint
				recheck();
			} });
			x += segW[i];
		}
		if (want.kind == 1) {
			const char* fill = "Fill In This Circuit's Rows";
			const float fw = textWidth(fill, 11.5f, true);
			const D2D1_RECT_F r = D2D1::RectF(w - 22 - fw, y, w - 22, y + 28);
			const bool isHot = hot == (int)hits.size();
			drawText(rt, fill, r, 11.5f, withAlpha(accent, isHot ? 0.75f : 1.0f), TextAlign::Trailing, true);
			hits.push_back({ r, [this] {
				// A multi-line box doesn't say it changed when it's set: checked here.
				setWindowText(edit, crlf(circuitTableText()));
				textChanged();
				focusEditor = true;
			} });
		}
		y += 36;
		const float h = want.kind == 1 ? 104 : 62;
		const D2D1_RECT_F box = D2D1::RectF(22, y, w - 22, y + h);
		fillRound(rt, box, 10, boxPaper(dark));
		strokeRound(rt, box, 10, dark ? D2D1::ColorF(1, 1, 1, 0.09f) : D2D1::ColorF(0, 0, 0, 0.08f));
		placeEditor(true, box);
		return box.bottom + 10;
	}

	// The verdict, its notes, the names, and the rows.
	float drawCheck(ID2D1RenderTarget* rt, const D2D1_RECT_F& box, D2D1_COLOR_F ink, D2D1_COLOR_F dim, D2D1_COLOR_F accent, bool dark) {
		const D2D1_COLOR_F on = dark ? kNeon : kNeonDeep, white = D2D1::ColorF(1, 1, 1);
		float y = box.top;
		const bool empty = blank(want.text);
		const int verdict = check ? cl_check_verdict(check) : 2;
		const std::string summary = empty ? (want.kind == 0 ? "Type what the assignment asks for, like S = A ^ B ^ Cin."
		                                                    : "Paste or type the truth table you were given.")
		                          : check ? cl_check_summary(check) : "Can't read that yet.";
		const D2D1_COLOR_F color = empty ? dim : verdict == 0 ? on : verdict == 1 ? kRed : kOrange;
		{
			const D2D1_RECT_F banner = D2D1::RectF(box.left, y, box.right, y + 38);
			fillRound(rt, banner, 10, withAlpha(color, empty ? 0.06f : dark ? 0.16f : 0.11f));
			const D2D1_POINT_2F c = D2D1::Point2F(banner.left + 22, (banner.top + banner.bottom) / 2);
			fillCircle(rt, c, 9.5f, color);
			const D2D1_RECT_F mark = D2D1::RectF(c.x - 9, c.y - 9, c.x + 9, c.y + 9);
			if (!empty && verdict == 0) drawIcon(rt, kGlyphCheck, mark, 10, white);
			else if (!empty && verdict == 1) drawIcon(rt, kGlyphCross, mark, 8.5f, white);
			else drawText(rt, empty ? "…" : "?", mark, 11, white, TextAlign::Center, true);
			float right = banner.right - 12;
			if (verdict == 1 && check) {
				const char* label = "Only wrong rows";
				const float lw = textWidth(label, 11.5f) + 22;
				const D2D1_RECT_F r = D2D1::RectF(right - lw, banner.top + 8, right, banner.bottom - 8);
				const D2D1_RECT_F tick = D2D1::RectF(r.left, r.top + 4, r.left + 14, r.top + 18);
				const bool isHot = hot == (int)hits.size();
				fillRound(rt, tick, 4, onlyWrong ? accent : withAlpha(ink, isHot ? 0.14f : 0.08f));
				if (onlyWrong) drawIcon(rt, kGlyphCheck, tick, 9, chrome().onAccent());
				drawText(rt, label, D2D1::RectF(r.left + 20, r.top, r.right, r.bottom), 11.5f, dim);
				hits.push_back({ r, [this] { onlyWrong = !onlyWrong; scroll = 0; } });
				right = r.left - 10;
			}
			drawText(rt, summary, D2D1::RectF(banner.left + 42, banner.top, right, banner.bottom), 13.5f, empty ? dim : ink, TextAlign::Leading,
			         !empty);
			y = banner.bottom + 10;
		}
		// Notes: problems in red, warnings in orange, the rest quiet.
		auto note = [&](const std::string& text, int kind) {
			const D2D1_COLOR_F c = kind == 2 ? kRed : kind == 1 ? kOrange : dim;
			drawIcon(rt, kind == 2 ? kGlyphError : kind == 1 ? kGlyphWarning : kGlyphInfo, D2D1::RectF(box.left, y, box.left + 16, y + 16), 11, c);
			const float h = brand::text(rt, text, box.left + 20, y, 11.5f, DWRITE_FONT_WEIGHT_NORMAL, c, box.right - box.left - 20);
			y += std::max(16.0f, h) + 5;
		};
		if (!checkError.empty()) note(checkError, 2);
		for (int i = 0; check && i < cl_check_note_count(check); i++) note(cl_check_note(check, i), cl_check_note_kind(check, i));
		if (!check) return y - box.top;

		// Names, when one isn't simply the circuit's: click to choose.
		bool showNames = false;
		for (int i = 0; i < cl_check_name_count(check); i++) {
			const int col = cl_check_name_column(check, i);
			if (col < 0 || col >= (int)names.size() || cl_check_name_by_hand(check, i) || nameKey(names[col]) != nameKey(cl_check_name(check, i)))
				showNames = true;
		}
		if (showNames) {
			y += 4;
			drawText(rt, "Names", D2D1::RectF(box.left, y, box.left + 50, y + 24), 11.5f, dim, TextAlign::Leading, true);
			float x = box.left + 52;
			for (int i = 0; i < cl_check_name_count(check); i++) {
				const std::string name = cl_check_name(check, i);
				const int col = cl_check_name_column(check, i);
				const bool matched = col >= 0 && col < (int)names.size(), input = cl_check_name_is_input(check, i);
				const std::string label = name + " → " + (matched ? names[col] : std::string("?"));
				const bool bold = cl_check_name_by_hand(check, i);
				const float cw = textWidth(label, 11.5f, bold) + 20;
				if (x + cw > box.right && x > box.left + 52) { x = box.left + 52; y += 30; }
				const D2D1_RECT_F r = D2D1::RectF(x, y, x + cw, y + 24);
				const bool isHot = hot == (int)hits.size();
				fillRound(rt, r, 12, !matched ? withAlpha(kRed, 0.14f) : withAlpha(ink, isHot ? 0.11f : 0.06f));
				strokeRound(rt, r, 12, !matched ? withAlpha(kRed, 0.5f) : withAlpha(ink, 0.09f));
				drawText(rt, label, r, 11.5f, !matched ? kRed : accent, TextAlign::Center, bold);
				hits.push_back({ r, [this, name, input] { chooseName(name, input); } });
				x += cw + 6;
			}
			y += 34;
		}

		// The rows: the circuit's inputs, then for each output what was asked for and what it gave.
		std::vector<int> outs;
		for (int k = 0; k < cl_check_outputs(check); k++) {
			const int col = cl_check_output_column(check, k);
			if (col >= inputs && col < (int)names.size()) outs.push_back(k);
		}
		if (outs.empty()) return y - box.top;
		const float cw = 50, headH = 38, rowH = 24;
		std::vector<int> shown;
		for (int r = 0; r < (int)rows.size(); r++) if (!onlyWrong || cl_check_row_wrong(check, r)) shown.push_back(r);
		const float tableW = cw * (inputs + 2 * outs.size()), x0 = box.left, y0 = y;
		const D2D1_RECT_F card = D2D1::RectF(x0, y0, x0 + tableW, y0 + headH + rowH * shown.size());
		fillRound(rt, card, 12, dark ? D2D1::ColorF(1, 1, 1, 0.045f) : D2D1::ColorF(1, 1, 1, 1));
		// The header and the row bands kept inside the card's corners.
		ID2D1RoundedRectangleGeometry* shape = nullptr;
		ID2D1Layer* layer = nullptr;
		if (SUCCEEDED(d2dFactory()->CreateRoundedRectangleGeometry(D2D1::RoundedRect(card, 12, 12), &shape)) && SUCCEEDED(rt->CreateLayer(&layer)))
			rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), shape), layer);
		fillRect(rt, D2D1::RectF(x0, y0, card.right, y0 + headH), withAlpha(ink, 0.05f));
		for (int i = 0; i < inputs; i++)
			drawText(rt, names[i], D2D1::RectF(x0 + i * cw, y0, x0 + (i + 1) * cw, y0 + headH), 12, ink, TextAlign::Center, true);
		for (size_t j = 0; j < outs.size(); j++) {
			const float ox = x0 + (inputs + 2 * j) * cw;
			fillRect(rt, D2D1::RectF(ox, y0, ox + 2 * cw, y0 + headH), withAlpha(accent, dark ? 0.13f : 0.10f));
			drawText(rt, cl_check_output_name(check, outs[j]), D2D1::RectF(ox, y0 + 3, ox + 2 * cw, y0 + 21), 12, accent, TextAlign::Center, true);
			drawText(rt, "asked", D2D1::RectF(ox, y0 + 21, ox + cw, y0 + 35), 9.5f, dim, TextAlign::Center);
			drawText(rt, "got", D2D1::RectF(ox + cw, y0 + 21, ox + 2 * cw, y0 + 35), 9.5f, dim, TextAlign::Center);
		}
		const Face mono(L"Consolas", 13), monoBold(L"Consolas", 13, false, DWRITE_FONT_WEIGHT_BOLD);
		float firstWrong = -1;
		for (size_t i = 0; i < shown.size(); i++) {
			const int r = shown[i];
			const float ry = y0 + headH + i * rowH;
			const bool wrong = cl_check_row_wrong(check, r);
			if (wrong && firstWrong < 0) firstWrong = ry;
			if (wrong) fillRect(rt, D2D1::RectF(x0, ry, card.right, ry + rowH), withAlpha(kRed, dark ? 0.2f : 0.12f));
			else if (r % 2 == 1) fillRect(rt, D2D1::RectF(x0, ry, card.right, ry + rowH), withAlpha(ink, 0.03f));
			auto cell = [&](const std::string& t, float cx, D2D1_COLOR_F c, bool bold) {
				const Face& f = bold ? monoBold : mono;
				f.draw(rt, t, cx + (cw - f.width(t)) / 2, ry + 4, c);
			};
			for (int c = 0; c < inputs; c++) cell(std::string(1, rows[r][c]), x0 + c * cw, dim, false);
			for (size_t j = 0; j < outs.size(); j++) {
				const int k = outs[j];
				const float ox = x0 + (inputs + 2 * j) * cw;
				const char asked = cl_check_expected_cell(check, r, k), result = cl_check_result(check, r, k);
				const char got = rows[r][cl_check_output_column(check, k)];
				cell(asked == '-' ? "X" : std::string(1, asked), ox, asked == '-' ? kOrange : ink, false);
				cell(std::string(1, got), ox + cw, result == 'x' ? kRed : result == '=' ? ink : dim, result == 'x');
			}
		}
		if (layer) {
			rt->PopLayer();
			layer->Release();
		}
		if (shape) shape->Release();
		const D2D1_COLOR_F divider = withAlpha(accent, 0.55f);
		for (size_t j = 0; j < outs.size(); j++) {
			const float dx = x0 + (inputs + 2 * j) * cw;
			fillRect(rt, D2D1::RectF(dx - 0.75f, y0, dx + 0.75f, card.bottom), divider);
		}
		strokeRound(rt, card, 12, dark ? D2D1::ColorF(1, 1, 1, 0.09f) : D2D1::ColorF(0, 0, 0, 0.08f));
		// The first wrong row in view, once per check.
		if (scrollToWrong) {
			scrollToWrong = false;
			const float view = contentRect.bottom - contentRect.top;
			if (firstWrong >= 0 && firstWrong + rowH - box.top > view) {
				scroll = firstWrong - box.top - view / 2;
				redraw();
			}
		}
		return card.bottom + 12 - box.top;
	}

	// Which switch or light an asked-for name is: a menu of them.
	void chooseName(const std::string& name, bool input) {
		HMENU menu = CreatePopupMenu();
		std::vector<std::string> choices;
		auto byHand = want.names.find(name);
		auto menuText = [](std::string s) {
			std::string out;
			for (char c : s) { if (c == '&') out += '&'; out += c; }
			return out;
		};
		for (int c = input ? 0 : inputs; c < (input ? inputs : (int)names.size()); c++) {
			choices.push_back(names[c]);
			const bool chosen = byHand != want.names.end() && byHand->second == names[c];
			AppendMenuW(menu, MF_STRING | (chosen ? MF_CHECKED : 0), choices.size(), W(menuText((input ? "Switch " : "Light ") + names[c])).c_str());
		}
		AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
		AppendMenuW(menu, MF_STRING | (byHand == want.names.end() ? MF_CHECKED : 0), 1000, L"Match by Name");
		POINT p;
		GetCursorPos(&p);
		const int picked = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, nullptr);
		DestroyMenu(menu);
		if (picked <= 0) return;
		if (picked == 1000) want.names.erase(name);
		else if (picked <= (int)choices.size()) want.names[name] = choices[picked - 1];
		recheck();
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
		case WM_COMMAND:
			if ((HWND)lp == edit && edit && HIWORD(wp) == EN_CHANGE) textChanged();
			return true;
		case WM_CTLCOLOREDIT: {
			// Check's box: its text on the drawn box's paper.
			const bool dark = prefs().dark;
			const COLORREF back = gdiColor(boxPaper(dark));
			if (editBrush == nullptr || editBack != back) {
				if (editBrush) DeleteObject(editBrush);
				editBrush = CreateSolidBrush(back);
				editBack = back;
			}
			SetBkColor((HDC)wp, back);
			SetTextColor((HDC)wp, dark ? RGB(238, 238, 238) : RGB(26, 26, 26));
			r = (LRESULT)editBrush;
			return true;
		}
		case WM_ACTIVATE:
			// Back in front: the keyboard where it was (the box, say), not the window.
			if (LOWORD(wp) == WA_INACTIVE) {
				focusBefore = GetFocus();
				return false;
			}
			if (focusBefore && focusBefore == edit && IsWindowVisible(edit)) {
				SetFocus(edit);
				return true;
			}
			return false;
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
			if (wp == VK_TAB) { setTab((tab + ((GetKeyState(VK_SHIFT) & 0x8000) ? kTabs - 1 : 1)) % kTabs); return true; }
			if (wp == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) { copy("table", tableText('\t')); return true; }
			return false;
		}
		return false;
	}
};

}  // namespace

namespace { int g_truthTablesOpen = 0; }
bool truthTableOpen() { return g_truthTablesOpen > 0; }

void setCheckForTesting(const std::string& text) {
	gCheckForTesting = text;
	gHasCheckForTesting = true;
}

void showTruthTable(CircuitWindow* w, int page, bool check) {
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
	t.tab = check ? kCheckTab : std::min(std::max(prefs().truthTab, 0), kTabs - 1);
	// What was last checked is kept per circuit and page (CI's --check isn't).
	t.checkKey = w->filePath().empty() ? strf("unsaved-%p#%d", (void*)w, page) : w->filePath() + strf("#%d", page);
	const bool hadCheck = loadCheck(t.checkKey, t.want);
	if (gHasCheckForTesting) {
		t.want = SavedCheck();
		t.want.text = gCheckForTesting;
	}
	g_truthTablesOpen++;
	t.run();
	g_truthTablesOpen--;
	if (!gHasCheckForTesting && (hadCheck || !blank(t.want.text))) saveCheck(t.checkKey, t.want);
	if (prefs().truthTab != t.tab) { prefs().truthTab = t.tab; prefs().save(); }
	if (!t.buildText.empty()) {
		// Kept for Build from Formula either way; a locked circuit isn't added to.
		prefs().lastFormula = t.buildText;
		if (w->canEdit()) showBuildFormula(w);
		else w->lockNudge();
	}
}
