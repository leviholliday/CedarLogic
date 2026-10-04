// A side's tabs (see TabStrip.h).

#include "TabStrip.h"
#include "Canvas.h"
#include "Chrome.h"
#include "Window.h"

#include <algorithm>
#include <cmath>

namespace {
const float kCardH = 26, kMinW = 72, kMaxW = 220, kGap = 4, kCaptionW = 46;
const int kEditId = 1;
const UINT_PTR kBackTimer = 1;
const double kBackTime = 0.18;

// Windows' "Show animations in Windows", off: things change at once.
bool reduceMotion() {
	BOOL animations = TRUE;
	SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
	return !animations;
}
}

TabStrip::TabStrip(CircuitWindow* window, HWND parent, int paneIndex) : win(window), pane(paneIndex) {
	create(parent, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN);
}

TabStrip::~TabStrip() {
	if (edit) {
		RemoveWindowSubclass(edit, editProc, 1);
		DestroyWindow(edit);
	}
	if (editFont) DeleteObject(editFont);
}

void TabStrip::setTitleRow(bool on) {
	if (on == titleRow) return;
	titleRow = on;
	hot = hotClose = hotCap = pressedCap = -1;
	plusHot = maxHot = maxPressed = false;
	redraw();
}

bool TabStrip::activeSide() const { return !win->splitOpen() || win->focusedPane() == pane; }

float TabStrip::backAmount() const {
	if (backStart < 0) return backTo;
	const double t = std::min(1.0, (nowSeconds() - backStart) / kBackTime);
	return backFrom + (backTo - backFrom) * (float)(1 - std::pow(1 - t, 3));
}

void TabStrip::layout(float w) {
	pages = win->panePages(pane);
	const int n = (int)pages.size();
	// The window's buttons, at the right of the top row in focus mode.
	capsShown = titleRow && win->stripIsRightmost(pane);
	const float h = (float)stripHeight();
	float right = w;
	if (capsShown) {
		for (int i = CapClose; i >= CapMin; i--) {
			right -= kCaptionW;
			caps[i] = D2D1::RectF(right, 0, right + kCaptionW, h);
		}
		right -= 8;   // room to drag the window by
	}
	cards.assign((size_t)n, D2D1_RECT_F{});
	std::vector<float> want((size_t)n);
	float total = 0;
	for (int k = 0; k < n; k++) {
		want[k] = std::min(kMaxW, std::max(kMinW + 20, textWidth(win->pageName(pages[k]), 12, true) + 50));
		total += want[k] + kGap;
	}
	// When they don't all fit, the others are squeezed; the one in front
	// keeps its name readable.
	const float room = right - 8 - 40;
	const int shown = win->shownPage(pane);
	int front = -1;
	for (int k = 0; k < n; k++) if (pages[k] == shown) front = k;
	float others = 0;
	for (int k = 0; k < n; k++) if (k != front) others += want[k] + kGap;
	const float frontW = front >= 0 ? want[front] + kGap : 0;
	const float squeeze = total > room && others > 0 ? std::max(kMinW / kMaxW, (room - frontW) / others) : 1.0f;
	const float y = std::floor((h - kCardH) / 2) + 1;
	float x = 8;
	for (int k = 0; k < n; k++) {
		const float cw = k == front ? want[k] : std::max(kMinW, std::floor(want[k] * squeeze));
		cards[k] = D2D1::RectF(x, y, x + cw, y + kCardH);
		x += cw + kGap;
	}
	plus = D2D1::RectF(x + 2, y, x + 2 + kCardH, y + kCardH);
	if (renamingKey) placeEdit();
}

D2D1_RECT_F TabStrip::closeRect(int k) const {
	const D2D1_RECT_F& c = cards[k];
	const float cy = (c.top + c.bottom) / 2;
	return D2D1::RectF(c.right - 22, cy - 8, c.right - 6, cy + 8);
}

int TabStrip::tabAt(float x, float y) const {
	for (int k = 0; k < (int)cards.size(); k++) if (inRect(cards[k], x, y)) return k;
	return -1;
}

int TabStrip::captionAt(float x, float y) const {
	if (!capsShown) return -1;
	for (int i = 0; i < CapCount; i++) if (inRect(caps[i], x, y)) return i;
	return -1;
}

void TabStrip::paint(ID2D1RenderTarget* rt, float w, float h) {
	layout(w);
	const Chrome c = chrome();
	const bool dark = c.dark;
	rt->Clear(c.tabBar());
	const D2D1_COLOR_F ink = c.tabInk();
	const int shown = win->shownPage(pane);
	const int n = (int)cards.size();
	const D2D1_COLOR_F green = dark ? rgb255(64, 214, 110) : rgb255(36, 168, 76);
	CLDocument* doc = win->document();
	const bool closable = cl_document_page_count(doc) > 1;

	auto drawCard = [&](int k, D2D1_RECT_F r) {
		const int p = pages[k];
		const bool front = p == shown;
		if (front) {
			fillRound(rt, D2D1::RectF(r.left, r.top + 1, r.right, r.bottom + 1), 8, D2D1::ColorF(0, 0, 0, dark ? 0.26f : 0.10f));
			fillRound(rt, r, 8, dark ? c.tabCard() : D2D1::ColorF(1, 1, 1, 1));
			if (dark) fillRound(rt, r, 8, rgb255(44, 48, 56));
			strokeRound(rt, r, 8, withAlpha(ink, dark ? 0.18f : 0.10f));
		} else if (k == hot || (dragging && k == pressed)) {
			fillRound(rt, r, 8, withAlpha(ink, 0.06f));
		}
		const float cy = (r.top + r.bottom) / 2;
		fillCircle(rt, D2D1::Point2F(r.left + 13, cy), 3, front && win->running() ? green : withAlpha(ink, front ? 0.35f : 0.22f));
		if (renamingKey == cl_document_page_id(doc, p)) return;   // the text box is over it
		const bool showClose = closable && (front || k == hot);
		const float textRight = r.right - (showClose ? 26 : 10);
		drawText(rt, win->pageName(p), D2D1::RectF(r.left + 23, r.top, textRight, r.bottom), 12, withAlpha(ink, front ? 1.0f : 0.62f),
		         TextAlign::Leading, front);
		if (showClose) {
			const D2D1_RECT_F x = closeRect(k);
			if (k == hotClose) fillRound(rt, x, 5, withAlpha(ink, 0.16f));
			drawIcon(rt, Icon::Dismiss, x, 8, withAlpha(ink, k == hotClose ? 0.9f : 0.5f));
		}
	};
	for (int k = 0; k < n; k++) {
		if (dragging && k == pressed) continue;
		drawCard(k, cards[k]);
	}
	// The one being dragged, under the pointer.
	if (dragging && pressed >= 0 && pressed < n) {
		const float cw = cards[pressed].right - cards[pressed].left;
		const float right = capsShown ? caps[CapMin].left - 4 : w - 4;
		const float left = std::max(4.0f, std::min(right - cw, dragX - grabOffset));
		drawCard(pressed, D2D1::RectF(left, cards[pressed].top, left + cw, cards[pressed].bottom));
	}
	if (plusHot) fillRound(rt, plus, 9, withAlpha(ink, 0.08f));
	drawIcon(rt, Icon::Add, plus, 11, withAlpha(ink, 0.75f));

	// A hairline under the strip -- or, in a split, an accent rail under the
	// side you're in, and the other side stepping back.
	const bool split = win->splitOpen();
	const float want = split && !activeSide() ? 1.0f : 0.0f;
	if (want != backTo) {
		backFrom = backAmount();
		backTo = want;
		backStart = reduceMotion() ? -1 : nowSeconds();
		if (backStart >= 0) SetTimer(hwnd, kBackTimer, 15, nullptr);
	}
	const float back = backAmount();
	fillRect(rt, D2D1::RectF(0, h - 1, w, h), c.hairline());
	if (split && back < 0.99f) fillRect(rt, D2D1::RectF(0, h - 2, w, h), withAlpha(c.accent(), 1 - back));
	if (back > 0.01f) fillRect(rt, D2D1::RectF(0, 0, w, h - 1), withAlpha(c.tabBar(), 0.45f * back));

	// Focus mode: the window's own buttons, as the toolbar draws them.
	if (capsShown) {
		const bool active = GetActiveWindow() == GetAncestor(hwnd, GA_ROOT);
		const D2D1_COLOR_F barInk = c.barInk();
		for (int i = 0; i < CapCount; i++) {
			const D2D1_RECT_F r = caps[i];
			const bool isHot = i == CapMax ? maxHot : i == hotCap;
			const bool isPressed = i == CapMax ? maxPressed : (i == pressedCap && i == hotCap);
			if (i == CapClose && (isHot || isPressed)) fillRect(rt, r, isPressed ? rgb255(148, 31, 21) : rgb255(196, 43, 28));
			else if (isHot) fillRect(rt, r, withAlpha(barInk, isPressed ? 0.14f : 0.08f));
			const wchar_t glyph = i == CapMin ? Icon::Minimize
			                    : i == CapMax ? (IsZoomed(GetAncestor(hwnd, GA_ROOT)) ? Icon::Restore : Icon::Maximize)
			                                  : Icon::Close;
			const D2D1_COLOR_F g = i == CapClose && (isHot || isPressed) ? D2D1::ColorF(1, 1, 1, 1) : withAlpha(barInk, active ? 0.9f : 0.45f);
			drawIcon(rt, glyph, r, 10, g);
		}
	}

	std::vector<Tip> tips;
	tips.push_back({ plus, "New tab (Ctrl+T)" });
	for (const D2D1_RECT_F& r : cards) tips.push_back({ r, "Double-click to rename. Drag along to move, or down onto the canvas to split the view" });
	if (capsShown) {
		tips.push_back({ caps[CapMin], "Minimize" });
		tips.push_back({ caps[CapClose], "Close" });
	}
	setTips(tips);
	if (renamingKey && edit) InvalidateRect(edit, nullptr, FALSE);   // the text box over its card, on top
}

// ---- The pointer -------------------------------------------------------------------

void TabStrip::mouseMove(float x, float y) {
	if (pressed >= 0 && !dragging && (std::fabs(x - pressX) > 5 || std::fabs(y - pressY) > 8)) dragging = true;
	if (dragging) {
		dragX = x;
		// Held over the canvas: drop it there to split the view (or move
		// it to the other side); along the strip, it moves among the tabs.
		POINT sp = { (LONG)std::lround(x * scale()), (LONG)std::lround(y * scale()) };
		ClientToScreen(hwnd, &sp);
		const CircuitWindow::DropHint h = win->dropHintAt(pane, sp);
		win->showDropHint(h);
		if (h.kind == 0 && y > -12 && y < stripHeight() + 12 && pressed < (int)cards.size()) {
			// Move it when its middle passes a neighbour's.
			const float cw = cards[pressed].right - cards[pressed].left;
			const float mid = dragX - grabOffset + cw / 2;
			int to = pressed;
			for (int k = 0; k < (int)cards.size(); k++) {
				const float m = (cards[k].left + cards[k].right) / 2;
				if (k < pressed && mid < m) { to = k; break; }
				if (k > pressed && mid > m) to = k;
			}
			if (to != pressed) {
				win->moveTab(pages[pressed], pages[to]);
				pressed = to;
				layout((float)width());
			}
		}
		redraw();
		return;
	}
	const int cap = captionAt(x, y);
	const int t = cap >= 0 ? -1 : tabAt(x, y);
	const int tc = t >= 0 && inRect(closeRect(t), x, y) ? t : -1;
	const bool ph = cap < 0 && inRect(plus, x, y);
	const int hc = cap == CapMax ? -1 : cap;
	if (t != hot || tc != hotClose || ph != plusHot || hc != hotCap) {
		hot = t;
		hotClose = tc;
		plusHot = ph;
		hotCap = hc;
		redraw();
	}
}

void TabStrip::mouseLeave() {
	if (hot != -1 || hotClose != -1 || plusHot || hotCap != -1) {
		hot = hotClose = hotCap = -1;
		plusHot = false;
		redraw();
	}
}

void TabStrip::mouseDown(int button, float x, float y, bool doubleClick) {
	const int cap = captionAt(x, y);
	if (cap >= 0) {
		if (button == 1 && cap != CapMax) { pressedCap = cap; hotCap = cap; redraw(); }
		return;
	}
	const int k = tabAt(x, y);
	if (button == 3) {
		POINT p;
		GetCursorPos(&p);
		if (k < 0) win->activatePane(pane);   // its New Tab opens on this side
		win->tabContextMenu(k >= 0 ? pages[k] : -1, p);
		return;
	}
	if (button == 2) {
		if (k >= 0) win->closeTab(pages[k]);
		return;
	}
	if (button != 1) return;
	auto letGo = [&] { if (GetCapture() == hwnd) ReleaseCapture(); };
	if (inRect(plus, x, y)) {
		win->activatePane(pane);
		win->run(CMD_NEW_TAB);
		return;
	}
	if (k < 0) {
		// The empty strip: work in this side; twice, a new tab.
		win->activatePane(pane);
		if (doubleClick) win->run(CMD_NEW_TAB);
		return;
	}
	const int p = pages[k];
	if (doubleClick) {
		letGo();
		beginRename(p);
		return;
	}
	if (cl_document_page_count(win->document()) > 1 && inRect(closeRect(k), x, y) && (p == win->shownPage(pane) || k == hot)) {
		letGo();
		win->closeTab(p);
		hot = hotClose = -1;
		return;
	}
	win->showPage(p);
	pressed = k;
	pressX = x;
	pressY = y;
	dragX = x;
	grabOffset = x - cards[k].left;
	redraw();
}

void TabStrip::endDrag() {
	pressed = -1;
	dragging = false;
	win->showDropHint(CircuitWindow::DropHint());
	redraw();
}

void TabStrip::mouseUp(int button, float x, float y) {
	if (button != 1) return;
	if (pressedCap >= 0) {
		const int cap = pressedCap;
		pressedCap = -1;
		redraw();
		if (captionAt(x, y) != cap) return;
		HWND top = GetAncestor(hwnd, GA_ROOT);
		if (cap == CapMin) ShowWindow(top, SW_MINIMIZE);
		else if (cap == CapClose) PostMessageW(top, WM_CLOSE, 0, 0);
		return;
	}
	const bool wasDragging = dragging;
	const int k = pressed;
	POINT sp = { (LONG)std::lround(x * scale()), (LONG)std::lround(y * scale()) };
	ClientToScreen(hwnd, &sp);
	const CircuitWindow::DropHint h = wasDragging ? win->dropHintAt(pane, sp) : CircuitWindow::DropHint();
	const int page = k >= 0 && k < (int)pages.size() ? pages[k] : -1;
	endDrag();
	if (h.kind != 0 && page >= 0) win->tabDropped(page, h);
}

void TabStrip::captureLost() {
	pressedCap = -1;
	if (pressed >= 0 || dragging) endDrag();
}

// In focus mode the empty strip is the window's title bar (Windows drags
// it, and its maximize button shows the snap layouts); the line between
// the sides, and the top edge, are the frame's.
LRESULT TabStrip::hitTest(float x, float y) {
	const double s = scale();
	POINT sp = { (LONG)std::lround(x * s), (LONG)std::lround(y * s) };
	ClientToScreen(hwnd, &sp);
	if (win->onDivider(sp)) return HTTRANSPARENT;
	if (!titleRow) return HTCLIENT;
	if (y < 4 && !IsZoomed(GetAncestor(hwnd, GA_ROOT))) return HTTRANSPARENT;
	const int cap = captionAt(x, y);
	if (cap >= 0) return cap == CapMax ? HTTRANSPARENT : HTCLIENT;
	if (tabAt(x, y) >= 0 || inRect(plus, x, y)) return HTCLIENT;
	return HTTRANSPARENT;
}

RECT TabStrip::maximizeRect() const {
	if (!capsShown || !IsWindowVisible(hwnd)) return RECT{ 0, 0, 0, 0 };
	const double s = scale();
	const D2D1_RECT_F& m = caps[CapMax];
	RECT r = { (LONG)(m.left * s), (LONG)(m.top * s), (LONG)(m.right * s), (LONG)(m.bottom * s) };
	MapWindowPoints(hwnd, GetParent(hwnd), (POINT*)&r, 2);
	return r;
}

void TabStrip::setMaximizeHot(bool isHot, bool isPressed) {
	if (isHot == maxHot && isPressed == maxPressed) return;
	maxHot = isHot;
	maxPressed = isPressed;
	redraw();
}

bool TabStrip::buttonPoint(int which, POINT& p) {
	layout((float)width());
	D2D1_RECT_F r;
	if (which == kPlusButton) r = plus;
	else if (!capsShown) return false;
	else r = caps[which == kMinimizeButton ? CapMin : CapClose];
	const double s = scale();
	p = { (LONG)((r.left + r.right) / 2 * s), (LONG)((r.top + r.bottom) / 2 * s) };
	return true;
}

// ---- Renaming in place -----------------------------------------------------------

void TabStrip::beginRename(int page) {
	CLDocument* doc = win->document();
	if (page < 0 || page >= cl_document_page_count(doc)) return;
	commitRename(true, false);
	win->showPage(page);
	layout((float)width());
	if (std::find(pages.begin(), pages.end(), page) == pages.end()) return;
	if (edit == nullptr) {
		edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)kEditId, appInstance(),
		                       nullptr);
		SetWindowSubclass(edit, editProc, 1, (DWORD_PTR)this);
	}
	// The card's own type: the UI font, semibold, at its size (made at the
	// screen's scale now, so a move to another screen is caught next time).
	NONCLIENTMETRICSW m = {};
	m.cbSize = sizeof m;
	SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof m, &m, 0);
	const HFONT old = editFont;
	editFont = CreateFontW(-MulDiv(12, (int)dpiOf(hwnd), 96), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
	                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, m.lfMessageFont.lfFaceName);
	SendMessageW(edit, WM_SETFONT, (WPARAM)editFont, FALSE);
	if (old) DeleteObject(old);
	SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
	renamingKey = cl_document_page_id(doc, page);
	SetWindowTextW(edit, W(win->pageName(page)).c_str());
	placeEdit();
	ShowWindow(edit, SW_SHOW);
	SetFocus(edit);
	SendMessageW(edit, EM_SETSEL, 0, -1);
	redraw();
}

// Over the name on its card.
void TabStrip::placeEdit() {
	if (edit == nullptr || renamingKey == 0) return;
	CLDocument* doc = win->document();
	int k = -1;
	for (int i = 0; i < (int)pages.size(); i++) if (cl_document_page_id(doc, pages[i]) == renamingKey) k = i;
	if (k < 0) return;
	const double s = scale();
	HDC dc = GetDC(edit);
	HGDIOBJ old = SelectObject(dc, editFont);
	TEXTMETRICW tm = {};
	GetTextMetricsW(dc, &tm);
	SelectObject(dc, old);
	ReleaseDC(edit, dc);
	const D2D1_RECT_F& r = cards[k];
	const int left = (int)std::lround((r.left + 22) * s), right = (int)std::lround((r.right - 8) * s);
	const int mid = (int)std::lround((r.top + r.bottom) / 2 * s);
	RECT now;
	GetWindowRect(edit, &now);
	MapWindowPoints(nullptr, hwnd, (POINT*)&now, 2);
	if (now.left != left || now.right != right || now.top != mid - tm.tmHeight / 2)
		SetWindowPos(edit, nullptr, left, mid - tm.tmHeight / 2, right - left, tm.tmHeight, SWP_NOZORDER | SWP_NOACTIVATE);
}

void TabStrip::commitRename(bool keep, bool refocus) {
	if (renamingKey == 0) return;
	const uint64_t key = renamingKey;
	renamingKey = 0;   // first: the box losing the keyboard below would commit again
	const std::string text = windowText(edit);
	if (refocus) {
		if (Canvas* c = win->paneCanvas(pane)) c->focus();
	}
	ShowWindow(edit, SW_HIDE);
	CLDocument* doc = win->document();
	const int page = cl_document_page_index(doc, key);
	const size_t a = text.find_first_not_of(" \t"), b = text.find_last_not_of(" \t");
	const std::string name = a == std::string::npos ? std::string() : text.substr(a, b - a + 1);
	if (keep && page >= 0 && !name.empty() && name != win->pageName(page)) {
		cl_document_rename_page(doc, page, name.c_str());
		win->edited();   // saved a moment later, as any change is
	}
	redraw();
}

LRESULT CALLBACK TabStrip::editProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
	TabStrip* t = reinterpret_cast<TabStrip*>(data);
	switch (msg) {
	case WM_KEYDOWN:
		if (wp == VK_RETURN) { guarded("renaming a tab", [&] { t->commitRename(true, true); }); return 0; }
		if (wp == VK_ESCAPE) { guarded("renaming a tab", [&] { t->commitRename(false, true); }); return 0; }
		break;
	case WM_CHAR:
		if (wp == VK_RETURN || wp == VK_ESCAPE) return 0;   // no beep
		break;
	case WM_GETDLGCODE:
		return DLGC_WANTALLKEYS | DefSubclassProc(h, msg, wp, lp);
	case WM_KILLFOCUS:
		guarded("renaming a tab", [&] { t->commitRename(true, false); });
		break;
	default:
		break;
	}
	return DefSubclassProc(h, msg, wp, lp);
}

LRESULT TabStrip::message(UINT msg, WPARAM wp, LPARAM lp, bool& handled) {
	handled = false;
	if (msg == WM_TIMER && wp == kBackTimer) {
		if (backStart < 0 || nowSeconds() - backStart >= kBackTime) {
			backStart = -1;
			KillTimer(hwnd, kBackTimer);
		}
		redraw();
		handled = true;
		return 0;
	}
	if (msg == WM_CTLCOLOREDIT && (HWND)lp == edit) {
		// The front card's colour, under the name being typed.
		const Chrome c = chrome();
		static HBRUSH light = CreateSolidBrush(RGB(255, 255, 255)), dark = CreateSolidBrush(RGB(44, 48, 56));
		SetBkColor((HDC)wp, c.dark ? RGB(44, 48, 56) : RGB(255, 255, 255));
		SetTextColor((HDC)wp, c.gdi(c.tabInk()));
		handled = true;
		return (LRESULT)(c.dark ? dark : light);
	}
	return 0;
}
