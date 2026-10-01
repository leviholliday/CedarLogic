// The find bar (see FindBar.h).

#include "FindBar.h"
#include "Canvas.h"
#include "Chrome.h"
#include "Window.h"

#include <algorithm>

namespace {
const int kEditId = 1;
const float kEditX = 38, kEditW = 230;
}

FindBar::FindBar(CircuitWindow* window, HWND parent) : win(window) {
	create(parent, WS_CHILD | WS_CLIPCHILDREN);
	edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, hwnd,
	                       (HMENU)(INT_PTR)kEditId, appInstance(), nullptr);
	SendMessageW(edit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Labels, TO/FROM names, parts");
	SetWindowSubclass(edit, editProc, 1, (DWORD_PTR)this);
	dpiChanged();
}

void FindBar::dpiChanged() {
	SendMessageW(edit, WM_SETFONT, (WPARAM)uiFont(dpiOf(hwnd)), TRUE);
	layoutEdit();
}

void FindBar::layoutEdit() {
	const double s = scale();
	HDC dc = GetDC(edit);
	HGDIOBJ old = SelectObject(dc, uiFont(dpiOf(hwnd)));
	TEXTMETRICW tm = {};
	GetTextMetricsW(dc, &tm);
	SelectObject(dc, old);
	ReleaseDC(edit, dc);
	MoveWindow(edit, (int)(kEditX * s), (int)(barHeight() / 2 * s) - tm.tmHeight / 2, (int)(kEditW * s), tm.tmHeight, TRUE);
}

void FindBar::open(const std::string& query) {
	if (!query.empty()) SetWindowTextW(edit, W(query).c_str());
	ShowWindow(hwnd, SW_SHOW);
	SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	layoutEdit();
	SetFocus(edit);
	SendMessageW(edit, EM_SETSEL, 0, -1);
	run(GetWindowTextLengthW(edit) > 0);
	redraw();
}

void FindBar::close() {
	ShowWindow(hwnd, SW_HIDE);
	if (Canvas* c = win->currentCanvas()) c->focus();
}

void FindBar::run(bool jump) {
	hits.clear();
	const std::string q = windowText(edit);
	total = 0;
	index = 0;
	if (!q.empty()) {
		std::vector<CLFindResult> out(500);
		total = cl_find(win->document(), q.c_str(), out.data(), (int)out.size());
		for (int i = 0; i < std::min(total, (int)out.size()); i++)
			hits.push_back({ out[i].page, out[i].gate, out[i].x, out[i].y, out[i].text ? out[i].text : "", out[i].kind ? out[i].kind : "" });
	}
	if (jump && !hits.empty()) show(0);
	redraw();
}

void FindBar::step(int delta) {
	if (hits.empty()) { MessageBeep(MB_OK); return; }
	const int n = (int)hits.size();
	index = ((index + delta) % n + n) % n;
	show(index);
	redraw();
}

// To the result's page, select it, and bring it to the middle.
void FindBar::show(int i) {
	if (i < 0 || i >= (int)hits.size()) return;
	const Hit& h = hits[i];
	win->showFoundGate(h.page, h.gate, h.x, h.y);
}

void FindBar::paint(ID2D1RenderTarget* rt, float w, float h) {
	const Chrome c = chrome();
	const D2D1_COLOR_F ink = c.barInk();
	// The window behind the pill is the canvas's colour, so the pill's
	// rounded ends sit on it.
	Canvas* cv = win->currentCanvas();
	rt->Clear(win->simView() ? D2D1::ColorF(0.030f, 0.038f, 0.050f) : c.canvas());
	(void)cv;
	const D2D1_RECT_F pill = D2D1::RectF(1, 1, w - 1, h - 3);
	fillRound(rt, D2D1::RectF(pill.left, pill.top + 2, pill.right, pill.bottom + 2), h / 2, D2D1::ColorF(0, 0, 0, c.dark ? 0.30f : 0.10f));
	fillRound(rt, pill, (h - 4) / 2, c.dark ? rgb255(40, 43, 50) : rgb255(250, 250, 252));
	strokeRound(rt, pill, (h - 4) / 2, withAlpha(ink, 0.18f));
	const float cy = (pill.top + pill.bottom) / 2;
	drawIcon(rt, Icon::Search, D2D1::RectF(12, cy - 10, 32, cy + 10), 12, withAlpha(ink, 0.55f));
	// What was found.
	std::string status;
	if (total > 0 && index < (int)hits.size()) {
		status = strf("%d of %d · %s", index + 1, total, hits[index].kind.c_str());
		if (cl_document_page_count(win->document()) > 1) status += " · " + win->pageTitle(hits[index].page);
	} else if (GetWindowTextLengthW(edit) > 0) {
		status = "Not found";
	}
	const float sx = kEditX + kEditW + 12;
	up = D2D1::RectF(w - 152, cy - 13, w - 124, cy + 13);
	down = D2D1::RectF(w - 122, cy - 13, w - 94, cy + 13);
	done = D2D1::RectF(w - 86, cy - 13, w - 12, cy + 13);
	drawText(rt, status, D2D1::RectF(sx, pill.top, up.left - 6, pill.bottom), 11.5f,
	         status == "Not found" ? D2D1::ColorF(0.96f, 0.58f, 0.13f) : withAlpha(ink, 0.6f));
	const bool any = total > 0;
	if (hot == 0 && any) fillRound(rt, up, 7, withAlpha(ink, 0.08f));
	if (hot == 1 && any) fillRound(rt, down, 7, withAlpha(ink, 0.08f));
	drawIcon(rt, 0xE70E, up, 10, withAlpha(ink, any ? 0.8f : 0.3f));   // chevron up
	drawIcon(rt, Icon::ChevronDown, down, 10, withAlpha(ink, any ? 0.8f : 0.3f));
	fillRound(rt, done, 8, withAlpha(ink, hot == 2 ? 0.12f : 0.07f));
	drawText(rt, "Done", done, 12, ink, TextAlign::Center);
	setTips({ { up, "Previous (Shift+Enter)" }, { down, "Next (Enter)" } });
}

void FindBar::mouseMove(float x, float y) {
	const int h = inRect(up, x, y) ? 0 : inRect(down, x, y) ? 1 : inRect(done, x, y) ? 2 : -1;
	if (h != hot) { hot = h; redraw(); }
}

void FindBar::mouseLeave() {
	if (hot != -1) { hot = -1; redraw(); }
}

void FindBar::mouseDown(int button, float x, float y, bool) {
	if (GetCapture() == hwnd) ReleaseCapture();
	if (button != 1) return;
	if (inRect(up, x, y)) step(-1);
	else if (inRect(down, x, y)) step(1);
	else if (inRect(done, x, y)) close();
	else SetFocus(edit);
}

LRESULT FindBar::message(UINT msg, WPARAM wp, LPARAM lp, bool& handled) {
	handled = false;
	if (msg == WM_COMMAND && LOWORD(wp) == kEditId && HIWORD(wp) == EN_CHANGE) {
		run(true);
		handled = true;
		return 0;
	}
	if (msg == WM_CTLCOLOREDIT) {
		const Chrome c = chrome();
		static HBRUSH light = CreateSolidBrush(RGB(250, 250, 252)), dark = CreateSolidBrush(RGB(40, 43, 50));
		SetBkColor((HDC)wp, c.dark ? RGB(40, 43, 50) : RGB(250, 250, 252));
		SetTextColor((HDC)wp, c.gdi(c.barInk()));
		handled = true;
		return (LRESULT)(c.dark ? dark : light);
	}
	(void)lp;
	return 0;
}

LRESULT CALLBACK FindBar::editProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
	FindBar* f = reinterpret_cast<FindBar*>(data);
	if (msg == WM_KEYDOWN) {
		const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
		switch (wp) {
		case VK_RETURN: f->step(shift ? -1 : 1); return 0;
		case VK_ESCAPE: f->close(); return 0;
		case VK_UP: f->step(-1); return 0;
		case VK_DOWN: f->step(1); return 0;
		default: break;
		}
	}
	if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) return 0;   // no beep
	return DefSubclassProc(h, msg, wp, lp);
}
