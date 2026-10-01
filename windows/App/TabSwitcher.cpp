// Ctrl+Tab (see TabSwitcher.h).

#include "TabSwitcher.h"
#include "Canvas.h"
#include "Chrome.h"
#include "Window.h"

#include <dwmapi.h>

#include <algorithm>

namespace {
// In points, as the Mac's.
const float kThumbW = 208, kThumbH = 130, kLabelH = 18, kLabelGap = 8, kCardPad = 10, kGap = 6, kPad = 14;
const int kPerRow = 5;
const wchar_t* kClass = L"CedarLogicTabSwitcher";
}

TabSwitcher::~TabSwitcher() { hide(); }

bool TabSwitcher::key(bool backwards) {
	if (isActive) {
		const int n = (int)tabs.size();
		if (n > 0) selected = (selected + (backwards ? n - 1 : 1)) % n;
		if (panel) InvalidateRect(panel, nullptr, FALSE);
		return true;
	}
	tabs = win->recentTabs();
	if (tabs.size() > 10) tabs.resize(10);
	if (tabs.size() < 2) return false;
	isActive = true;
	selected = backwards ? (int)tabs.size() - 1 : 1;
	started = nowSeconds();
	return true;
}

void TabSwitcher::tick() {
	if (!isActive) return;
	if (!(GetAsyncKeyState(VK_CONTROL) & 0x8000)) { commit(selected); return; }
	// Only if Ctrl is still down after a moment: a quick Ctrl+Tab just flips
	// to the previous tab.
	if (panel == nullptr && nowSeconds() - started >= 0.18) show();
}

void TabSwitcher::commit(int index) {
	if (!isActive) return;
	const int target = index >= 0 && index < (int)tabs.size() ? tabs[index] : -1;
	cancel();
	if (target >= 0 && index != 0) win->showPage(target);
}

void TabSwitcher::cancel() {
	isActive = false;
	hide();
}

void TabSwitcher::show() {
	static bool registered = false;
	if (!registered) {
		registered = true;
		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof wc;
		wc.style = CS_DROPSHADOW;
		wc.lpfnWndProc = proc;
		wc.hInstance = appInstance();
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.lpszClassName = kClass;
		RegisterClassExW(&wc);
	}
	HWND owner = win->window();
	const UINT dpi = dpiOf(owner);
	const int n = (int)tabs.size(), perRow = std::min(n, kPerRow), rows = (n + kPerRow - 1) / kPerRow;
	const float cellW = kThumbW + 2 * kCardPad, cellH = kCardPad + kThumbH + kLabelGap + kLabelH + kCardPad;
	const float w = perRow * cellW + (perRow - 1) * kGap + 2 * kPad, h = rows * cellH + (rows - 1) * kGap + 2 * kPad;
	cards.clear();
	for (int i = 0; i < n; i++) {
		const float x = kPad + (i % kPerRow) * (cellW + kGap), y = kPad + (i / kPerRow) * (cellH + kGap);
		cards.push_back(D2D1::RectF(x, y, x + cellW, y + cellH));
	}
	RECT o;
	GetWindowRect(owner, &o);
	const int pw = scaled((int)w, dpi), ph = scaled((int)h, dpi);
	panel = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST, kClass, L"", WS_POPUP, (o.left + o.right - pw) / 2,
	                        (o.top + o.bottom - ph) / 2, pw, ph, owner, nullptr, appInstance(), this);
	const DWORD round = 2;
	DwmSetWindowAttribute(panel, 33, &round, sizeof round);
	GetCursorPos(&shownAt);
	ShowWindow(panel, SW_SHOWNA);
}

void TabSwitcher::hide() {
	if (panel == nullptr) return;
	surface.release();
	SetWindowLongPtrW(panel, GWLP_USERDATA, 0);
	DestroyWindow(panel);
	panel = nullptr;
}

void TabSwitcher::paint() {
	PAINTSTRUCT ps;
	BeginPaint(panel, &ps);
	ID2D1HwndRenderTarget* rt = surface.begin(panel);
	if (rt) {
		const Chrome c = chrome();
		const bool dark = c.dark;
		rt->Clear(dark ? rgb255(34, 37, 43) : rgb255(242, 243, 246));
		const D2D1_COLOR_F ink = dark ? D2D1::ColorF(1, 1, 1, 1) : D2D1::ColorF(0, 0, 0, 1);
		const double s = surface.scale();
		for (size_t i = 0; i < cards.size() && i < tabs.size(); i++) {
			const D2D1_RECT_F& r = cards[i];
			const bool on = (int)i == selected;
			if (on) {
				fillRound(rt, r, 18, withAlpha(ink, dark ? 0.16f : 0.08f));
				strokeRound(rt, r, 18, withAlpha(ink, dark ? 0.14f : 0.06f));
			}
			const D2D1_RECT_F thumb = D2D1::RectF(r.left + kCardPad, r.top + kCardPad, r.left + kCardPad + kThumbW, r.top + kCardPad + kThumbH);
			fillRound(rt, thumb, 10, c.canvas());
			// The page, fitted.
			const int page = win->pageOfTab(tabs[i]);
			if (page >= 0) {
				D2D1_MATRIX_3X2_F t;
				rt->GetTransform(&t);
				rt->PushAxisAlignedClip(thumb, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
				rt->SetTransform(D2D1::Matrix3x2F::Translation(thumb.left, thumb.top) * t);
				cl_document_draw_fitted(win->document(), page, rt, kThumbW, kThumbH, 10, s, dark ? CL_STYLE_DARK : CL_STYLE_LIGHT);
				rt->SetTransform(t);
				rt->PopAxisAlignedClip();
			}
			strokeRound(rt, thumb, 10, withAlpha(ink, dark ? 0.14f : 0.12f), 0.5f);
			drawText(rt, win->tabName(tabs[i]), D2D1::RectF(thumb.left, thumb.bottom + kLabelGap, thumb.right, thumb.bottom + kLabelGap + kLabelH),
			         12, withAlpha(ink, on ? 0.95f : 0.6f), TextAlign::Center, on);
		}
		surface.end();
	}
	EndPaint(panel, &ps);
}

LRESULT CALLBACK TabSwitcher::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
	if (msg == WM_NCCREATE) SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
	TabSwitcher* t = reinterpret_cast<TabSwitcher*>(GetWindowLongPtrW(h, GWLP_USERDATA));
	if (t == nullptr) return DefWindowProcW(h, msg, wp, lp);
	if (msg == WM_NCCREATE) t->panel = h;
	const float s = dpiOf(h) / 96.0f;
	const float x = GET_X_LPARAM(lp) / s, y = GET_Y_LPARAM(lp) / s;
	switch (msg) {
	case WM_PAINT: guarded("the tab switcher", [&] { t->paint(); }); return 0;
	case WM_ERASEBKGND: return 1;
	case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
	case WM_MOUSEMOVE: {
		POINT p;
		GetCursorPos(&p);
		if (p.x == t->shownAt.x && p.y == t->shownAt.y) return 0;   // it opened under the pointer
		for (size_t i = 0; i < t->cards.size(); i++)
			if (inRect(t->cards[i], x, y) && (int)i != t->selected) { t->selected = (int)i; InvalidateRect(h, nullptr, FALSE); }
		return 0;
	}
	case WM_LBUTTONUP:
		for (size_t i = 0; i < t->cards.size(); i++)
			if (inRect(t->cards[i], x, y)) { const int idx = (int)i; PostMessageW(h, WM_APP, idx, 0); }
		return 0;
	case WM_APP: t->commit((int)wp); return 0;
	}
	return DefWindowProcW(h, msg, wp, lp);
}
