// The notes pane (see Notes.h).

#include "Notes.h"
#include "Canvas.h"
#include "Chrome.h"
#include "Window.h"

#include <algorithm>

namespace {

const int kEditId = 1;
const float kHeaderH = 38;
const char* kPlaceholder = "Notes for this circuit. They're saved with it.";

std::string withCrlf(const std::string& s) {
	std::string out;
	for (char c : s) {
		if (c == '\r') continue;
		if (c == '\n') out += '\r';
		out += c;
	}
	return out;
}

std::string withoutCr(std::string s) {
	s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
	return s;
}

bool dark(CircuitWindow* w) { return prefs().dark || (w && w->simView()); }

// The system's UI font at 13 points (the Mac's notes are 13 point), at a DPI.
HFONT notesFont(UINT dpi) {
	static HFONT font = nullptr;
	static UINT madeFor = 0;
	if (font && madeFor == dpi) return font;
	if (font) DeleteObject(font);
	NONCLIENTMETRICSW m = {};
	m.cbSize = sizeof m;
	SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof m, &m, 0, dpi);
	LOGFONTW lf = m.lfMessageFont;
	lf.lfHeight = -MulDiv(13, (int)dpi, 96);
	lf.lfQuality = CLEARTYPE_QUALITY;
	font = CreateFontIndirectW(&lf);
	madeFor = dpi;
	return font;
}

}  // namespace

NotesPanel::NotesPanel(CircuitWindow* window, HWND parent) : win(window) {
	create(parent, WS_CHILD | WS_CLIPCHILDREN);
	edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | ES_NOHIDESEL,
	                       0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)kEditId, appInstance(), nullptr);
	SendMessageW(edit, EM_SETLIMITTEXT, kMax, 0);
	SetWindowSubclass(edit, editProc, 1, (DWORD_PTR)this);
	dpiChanged();
	themeChanged();
	load();
}

void NotesPanel::dpiChanged() {
	SendMessageW(edit, WM_SETFONT, (WPARAM)notesFont(dpiOf(hwnd)), TRUE);
	// Room either side of the text, as the Mac's inset.
	const int m = scaled(8, dpiOf(hwnd));
	SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(m, m));
	layoutEdit();
}

void NotesPanel::themeChanged() {
	darkenControl(edit, dark(win), L"Explorer");   // its scroll bar
	InvalidateRect(edit, nullptr, TRUE);
	redraw();
}

void NotesPanel::layoutEdit() {
	RECT rc;
	GetClientRect(hwnd, &rc);
	const int top = (int)(kHeaderH * scale());
	MoveWindow(edit, scaled(4, dpiOf(hwnd)), top, std::max(0, (int)rc.right - scaled(4, dpiOf(hwnd))), std::max(0, (int)rc.bottom - top), TRUE);
}

void NotesPanel::load() {
	loading = true;
	const std::string text = cl_notes(win->document());
	SetWindowTextW(edit, W(withCrlf(text)).c_str());
	SendMessageW(edit, EM_EMPTYUNDOBUFFER, 0, 0);   // its own undo starts afresh
	count = GetWindowTextLengthW(edit);
	loading = false;
	redraw();
}

void NotesPanel::focus() { SetFocus(edit); }

// Typed, pasted or deleted: into the circuit, which saves itself (and syncs)
// as after any change, without an undo step.
void NotesPanel::changed() {
	if (loading) return;
	const std::string text = withoutCr(windowText(edit));
	if (text == cl_notes(win->document())) return;
	cl_notes_set(win->document(), text.c_str());
	const int was = count;
	count = GetWindowTextLengthW(edit);
	if (count > kCounterFrom || was > kCounterFrom) redraw();
	if ((was == 0) != (count == 0)) InvalidateRect(edit, nullptr, TRUE);   // the placeholder comes and goes
	win->markEdited();
}

void NotesPanel::select(const std::string& query) {
	if (query.empty()) return;
	const std::wstring text = W(withoutCr(windowText(edit))), q = W(query);
	auto lower = [](std::wstring s) { CharLowerBuffW(&s[0], (DWORD)s.size()); return s; };
	const std::wstring t = lower(text), lq = lower(q);
	DWORD from = 0, to = 0;
	SendMessageW(edit, EM_GETSEL, (WPARAM)&from, (LPARAM)&to);
	// The box counts a line break as two (\r\n): back to the text's own places.
	auto toText = [&](DWORD boxAt) {
		const std::wstring box = W(windowText(edit));
		size_t at = 0;
		for (size_t i = 0; i < box.size() && i < boxAt; i++) if (box[i] != L'\r') at++;
		return at;
	};
	auto toBox = [&](size_t textAt) {
		const std::wstring box = W(windowText(edit));
		size_t seen = 0, i = 0;
		for (; i < box.size() && seen < textAt; i++) if (box[i] != L'\r') seen++;
		return (DWORD)i;
	};
	size_t at = t.find(lq, toText(to));
	if (at == std::wstring::npos) at = t.find(lq);
	if (at == std::wstring::npos) return;
	SetFocus(edit);
	SendMessageW(edit, EM_SETSEL, toBox(at), toBox(at + lq.size()));
	SendMessageW(edit, EM_SCROLLCARET, 0, 0);
}

void NotesPanel::paint(ID2D1RenderTarget* rt, float w, float h) {
	const Chrome c{ dark(win) };
	rt->Clear(c.panel());
	fillRect(rt, D2D1::RectF(0, 0, 1, h), c.hairline());
	const D2D1_COLOR_F ink = c.barInk();
	drawText(rt, "Notes", D2D1::RectF(14, 0, w - 40, kHeaderH), 13, ink, TextAlign::Leading, true);
	if (count > kCounterFrom) {
		const std::string n = strf("%d,%03d / 20,000", count / 1000, count % 1000);
		drawText(rt, n, D2D1::RectF(60, 0, w - 40, kHeaderH), 11, count >= kMax ? D2D1::ColorF(0.96f, 0.58f, 0.13f) : withAlpha(ink, 0.55f),
		         TextAlign::Trailing);
	}
	closeBox = D2D1::RectF(w - 34, (kHeaderH - 24) / 2, w - 10, (kHeaderH + 24) / 2);
	if (closeHot) fillRound(rt, closeBox, 6, withAlpha(ink, 0.08f));
	drawIcon(rt, Icon::Close, closeBox, 9, withAlpha(ink, 0.7f));
	setTips({ { closeBox, "Hide Notes" + std::string() } });
}

void NotesPanel::mouseMove(float x, float y) {
	const bool hot = inRect(closeBox, x, y);
	if (hot != closeHot) { closeHot = hot; redraw(); }
}

void NotesPanel::mouseLeave() {
	if (closeHot) { closeHot = false; redraw(); }
}

void NotesPanel::mouseDown(int button, float x, float y, bool) {
	if (GetCapture() == hwnd) ReleaseCapture();
	if (button != 1) return;
	if (inRect(closeBox, x, y)) win->run(CMD_NOTES);
	else SetFocus(edit);
}

LRESULT NotesPanel::message(UINT msg, WPARAM wp, LPARAM lp, bool& handled) {
	handled = false;
	if (msg == WM_SIZE) { layoutEdit(); return 0; }
	if (msg == WM_COMMAND && LOWORD(wp) == kEditId) {
		if (HIWORD(wp) == EN_CHANGE) changed();
		else if (HIWORD(wp) == EN_MAXTEXT) {
			MessageBeep(MB_OK);
			win->note("The notes hold 20,000 characters, so they're full.");
		}
		handled = true;
		return 0;
	}
	if (msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORSTATIC) {
		const Chrome c{ dark(win) };
		const COLORREF back = c.gdi(c.panel());
		if (paper == nullptr || paperColor != back) {
			if (paper) DeleteObject(paper);
			paper = CreateSolidBrush(back);
			paperColor = back;
		}
		SetBkColor((HDC)wp, back);
		SetTextColor((HDC)wp, c.dark ? RGB(235, 235, 235) : RGB(26, 26, 26));
		handled = true;
		return (LRESULT)paper;
	}
	if (msg == WM_DESTROY && paper) { DeleteObject(paper); paper = nullptr; }
	(void)lp;
	return 0;
}

LRESULT CALLBACK NotesPanel::editProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
	NotesPanel* n = reinterpret_cast<NotesPanel*>(data);
	switch (msg) {
	case WM_KEYDOWN:
		// Escape: back to the circuit. Ctrl+A: everything in the box.
		if (wp == VK_ESCAPE) { if (Canvas* c = n->win->currentCanvas()) c->focus(); return 0; }
		if (wp == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) { SendMessageW(h, EM_SETSEL, 0, -1); return 0; }
		break;
	case WM_CHAR:
		if (wp == 0x1B || wp == 0x01) return 0;   // no beep, no stray character
		break;
	case WM_PAINT: {
		const LRESULT r = DefSubclassProc(h, msg, wp, lp);
		if (GetWindowTextLengthW(h) == 0) {
			// The placeholder (a multi-line box has no cue banner of its own).
			RECT rc = {};
			SendMessageW(h, EM_GETRECT, 0, (LPARAM)&rc);
			HideCaret(h);
			HDC dc = GetDC(h);
			HGDIOBJ old = SelectObject(dc, (HGDIOBJ)SendMessageW(h, WM_GETFONT, 0, 0));
			SetBkMode(dc, TRANSPARENT);
			SetTextColor(dc, dark(n->win) ? RGB(130, 130, 130) : RGB(150, 150, 150));
			const std::wstring t = W(kPlaceholder);
			DrawTextW(dc, t.c_str(), (int)t.size(), &rc, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
			SelectObject(dc, old);
			ReleaseDC(h, dc);
			ShowCaret(h);
		}
		return r;
	}
	case WM_NCDESTROY:
		RemoveWindowSubclass(h, editProc, 1);
		break;
	}
	return DefSubclassProc(h, msg, wp, lp);
}
