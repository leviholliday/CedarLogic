// The app's alert card (see Alert.h).

#include "Alert.h"
#include "Brand.h"
#include "Chrome.h"

#include <dwmapi.h>

#include <algorithm>
#include <cmath>

namespace {

const wchar_t* kClass = L"CedarLogicAlert";
const float kMinWidth = 440, kInset = 22, kButtonH = 34, kFieldH = 34;
const UINT_PTR kAnimTimer = 1;
const DWRITE_FONT_WEIGHT kBold = DWRITE_FONT_WEIGHT_BOLD, kSemi = DWRITE_FONT_WEIGHT_SEMI_BOLD, kNormal = DWRITE_FONT_WEIGHT_NORMAL;
const char* const kEnterKey = "↵";

bool reduceMotion() {
	BOOL animations = TRUE;
	SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
	return !animations;
}

D2D1_COLOR_F paper(bool dark) { return dark ? D2D1::ColorF(0.075f, 0.085f, 0.1f) : D2D1::ColorF(0.97f, 0.975f, 0.98f); }
D2D1_COLOR_F inkOf(bool dark) { return dark ? D2D1::ColorF(0.94f, 0.94f, 0.94f) : D2D1::ColorF(0.1f, 0.1f, 0.1f); }
COLORREF gdiMix(D2D1_COLOR_F under, D2D1_COLOR_F over, float a) {
	auto ch = [&](float u, float o) { return (BYTE)std::lround(255 * (u + (o - u) * a)); };
	return RGB(ch(under.r, over.r), ch(under.g, over.g), ch(under.b, over.b));
}

std::string trimmed(const std::string& s) {
	const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
	return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// The badge over the icon's corner: amber for a warning, red for a problem,
// the brand's green for a note.
void drawBadge(ID2D1RenderTarget* rt, float cx, float cy, int badge) {
	if (badge == 0) return;
	const D2D1_COLOR_F fill = badge == 2 ? D2D1::ColorF(0.98f, 0.70f, 0.12f) : badge == 3 ? D2D1::ColorF(0.90f, 0.24f, 0.21f) : brand::kNeonDeep;
	fillCircle(rt, D2D1::Point2F(cx, cy), 10.5f, D2D1::ColorF(0, 0, 0, 0.25f));
	fillCircle(rt, D2D1::Point2F(cx, cy), 9, fill);
	const char* mark = badge == 1 ? "i" : badge == 2 ? "!" : "×";
	brand::text(rt, mark, cx - 9, cy - 8.4f, 12, kBold, badge == 2 ? D2D1::ColorF(0.2f, 0.12f, 0) : D2D1::ColorF(1, 1, 1), 18,
	            DWRITE_TEXT_ALIGNMENT_CENTER);
}

struct Card {
	Alert& a;
	HWND hwnd = nullptr, owner = nullptr, edit = nullptr;
	HFONT editFont = nullptr;
	HBRUSH editBrush = nullptr;
	COLORREF editBack = 0;
	WindowSurface surface;
	bool done = false, calm = false, rounded = false, releasing = false;
	bool gone = false;   // destroyed with its owner
	int answer = 0;
	double shown = 0;
	int hot = -1, pressed = -1;
	int focus = -1;   // a button (index into a.buttons); -1 none (Enter is the default's); -2 the field
	std::vector<D2D1_RECT_F> rects;   // each button, in a.buttons' order (points)
	float width = kMinWidth, height = 0, textW = 0, headTop = 0, bodyTop = 0, fieldTop = 0;

	explicit Card(Alert& alert) : a(alert) {}

	const char* keyOf(const AlertButton& b) const {
		return b.answer == a.enter ? kEnterKey : b.answer == a.escape ? "esc" : nullptr;
	}
	float pillWidth(const AlertButton& b) const {
		const char* key = keyOf(b);
		return brand::textWidth(b.label, 13, kSemi) + (key ? brand::textWidth(key, 10, kBold) + 17 : 0) + 28;
	}

	// The card's size and where everything goes, in points.
	void measure() {
		float need = 0;
		bool apart = false;
		for (const AlertButton& b : a.buttons) { need += pillWidth(b) + 10; apart = apart || b.apart; }
		width = std::max(kMinWidth, std::ceil(need - 10 + (apart ? 24 : 0) + 2 * kInset));
		textW = width - 2 * kInset;
		const D2D1_COLOR_F ink = inkOf(prefs().dark);
		const float headH = brand::text(nullptr, a.heading, 0, 0, 16, kBold, ink, textW);
		const float bodyH = a.text.empty() ? 0 : brand::text(nullptr, a.text, 0, 0, 12.5f, kNormal, ink, textW);
		headTop = kInset + 52;
		bodyTop = headTop + headH + 5;
		fieldTop = bodyTop + bodyH + (bodyH > 0 ? 14 : 8);
		height = std::ceil(fieldTop + (a.field ? kFieldH + 20 : 8) + kButtonH + kInset);
		rects.assign(a.buttons.size(), D2D1::RectF());
		const float top = height - kInset - kButtonH;
		float right = width - kInset, left = kInset;
		for (size_t i = 0; i < a.buttons.size(); i++) {
			if (a.buttons[i].apart) continue;
			const float w = pillWidth(a.buttons[i]);
			rects[i] = D2D1::RectF(right - w, top, right, top + kButtonH);
			right -= w + 10;
		}
		for (size_t i = 0; i < a.buttons.size(); i++) {
			if (!a.buttons[i].apart) continue;
			const float w = pillWidth(a.buttons[i]);
			rects[i] = D2D1::RectF(left, top, left + w, top + kButtonH);
			left += w + 10;
		}
	}

	float scale() const { return dpiOf(hwnd) / 96.0f; }
	D2D1_RECT_F fieldRect() const { return D2D1::RectF(kInset, fieldTop, width - kInset, fieldTop + kFieldH); }
	int buttonAt(float x, float y) const {
		for (size_t i = 0; i < rects.size(); i++) if (inRect(rects[i], x, y)) return (int)i;
		return -1;
	}
	void redraw() { InvalidateRect(hwnd, nullptr, FALSE); }
	void choose(int ans) { answer = ans; done = true; }

	// Tab and the arrows: the field, then the buttons as they read, left to right.
	void moveFocus(int dir, bool buttonsOnly) {
		std::vector<int> order;
		if (a.field && !buttonsOnly) order.push_back(-2);
		std::vector<int> byX;
		for (size_t i = 0; i < rects.size(); i++) byX.push_back((int)i);
		std::sort(byX.begin(), byX.end(), [&](int p, int q) { return rects[p].left < rects[q].left; });
		order.insert(order.end(), byX.begin(), byX.end());
		if (order.empty()) return;
		int at = -1;
		for (size_t i = 0; i < order.size(); i++) if (order[i] == focus) at = (int)i;
		if (at < 0 && buttonsOnly) {
			// The arrows start from the default.
			for (size_t i = 0; i < order.size(); i++) if (a.buttons[order[i]].answer == a.enter) at = (int)i;
		}
		const int n = (int)order.size();
		at = at < 0 ? (dir > 0 ? 0 : n - 1) : ((at + dir) % n + n) % n;
		focus = order[at];
		SetFocus(focus == -2 && edit ? edit : hwnd);
		redraw();
	}

	bool key(UINT vk) {
		const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
		switch (vk) {
		case VK_RETURN: choose(focus >= 0 ? a.buttons[focus].answer : a.enter); return true;
		case VK_ESCAPE: choose(a.escape); return true;
		case VK_SPACE: if (focus >= 0) choose(a.buttons[focus].answer); return true;
		case VK_TAB: moveFocus(shift ? -1 : 1, false); return true;
		case VK_LEFT: case VK_RIGHT: moveFocus(vk == VK_LEFT ? -1 : 1, true); return true;
		default: return false;
		}
	}

	// The field: a plain Windows text box in a drawn, rounded field.
	void placeEdit() {
		if (edit == nullptr) return;
		const UINT dpi = dpiOf(hwnd);
		if (editFont) DeleteObject(editFont);
		editFont = CreateFontW(-MulDiv(13, (int)dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
		                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
		SendMessageW(edit, WM_SETFONT, (WPARAM)editFont, TRUE);
		SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
		const float s = dpi / 96.0f;
		const int h = scaled(20, dpi);
		SetWindowPos(edit, nullptr, (int)std::lround((kInset + 11) * s), (int)std::lround(fieldTop * s + (kFieldH * s - h) / 2),
		             (int)std::lround((textW - 22) * s), h, SWP_NOZORDER | SWP_NOACTIVATE);
	}

	void paint(ID2D1RenderTarget* rt, float w, float h) {
		const bool dark = prefs().dark;
		const D2D1_COLOR_F ink = inkOf(dark), dim = dark ? D2D1::ColorF(0.66f, 0.66f, 0.66f) : D2D1::ColorF(0.38f, 0.38f, 0.38f);
		const D2D1_COLOR_F accent = d2dColor(accentColor(dark));
		rt->Clear(paper(dark));
		// A breath of the accent along the top, as the Mac's sheets have.
		D2D1_GRADIENT_STOP st[2] = { { 0, withAlpha(accent, dark ? 0.10f : 0.07f) }, { 1, withAlpha(accent, 0) } };
		ID2D1GradientStopCollection* stops = nullptr;
		ID2D1LinearGradientBrush* tint = nullptr;
		if (SUCCEEDED(rt->CreateGradientStopCollection(st, 2, &stops)) &&
		    SUCCEEDED(rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, 90)), stops, &tint)))
			rt->FillRectangle(D2D1::RectF(0, 0, w, 90), tint);
		if (tint) tint->Release();
		if (stops) stops->Release();
		// Square corners (Windows 10) get a hairline edge; Windows 11 draws its own.
		if (!rounded) strokeRound(rt, D2D1::RectF(0.5f, 0.5f, w - 0.5f, h - 0.5f), 0, withAlpha(ink, dark ? 0.16f : 0.14f));

		// It arrives as the Mac's do: solid in about 0.14 s, settling into
		// place over 0.35 s (not with a field to type in: that box can't move).
		const double t = calm ? 1 : nowSeconds() - shown;
		const float appear = (float)brand::easeOut(t / 0.14), drop = a.field ? 0 : (float)(1 - brand::easeOut(t / 0.35));
		D2D1_MATRIX_3X2_F was;
		rt->GetTransform(&was);
		rt->SetTransform(D2D1::Matrix3x2F::Translation(0, -10 * drop) * was);
		const bool layer = appear < 0.999f;
		if (layer)
			rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), appear),
			              nullptr);
		const float x = kInset;
		brand::icon(rt, x, kInset, 40);
		drawBadge(rt, x + 38, kInset + 38, a.badge);
		brand::text(rt, a.heading, x, headTop, 16, kBold, ink, textW);
		if (!a.text.empty()) brand::text(rt, a.text, x, bodyTop, 12.5f, kNormal, dim, textW);
		if (a.field) {
			const D2D1_RECT_F f = fieldRect();
			const bool focused = edit && GetFocus() == edit;
			fillRound(rt, f, 9, withAlpha(ink, dark ? 0.07f : 0.05f));
			strokeRound(rt, D2D1::RectF(f.left + 0.5f, f.top + 0.5f, f.right - 0.5f, f.bottom - 0.5f), 8.5f,
			            focused ? withAlpha(accent, 0.8f) : withAlpha(ink, 0.12f), focused ? 2.0f : 1.0f);
		}
		const D2D1_COLOR_F red = D2D1::ColorF(0.86f, 0.22f, 0.2f);
		for (size_t i = 0; i < a.buttons.size(); i++) {
			const AlertButton& b = a.buttons[i];
			const D2D1_RECT_F r = rects[i];
			const bool isHot = hot == (int)i, down = isHot && pressed == (int)i;
			const D2D1_COLOR_F fill = b.kind == 1 ? withAlpha(brand::kNeonDeep, down ? 0.8f : isHot ? 1.0f : 0.92f)
			                        : b.kind == 2 ? withAlpha(red, down ? 0.8f : isHot ? 1.0f : 0.9f)
			                                      : withAlpha(ink, down ? 0.16f : isHot ? 0.12f : 0.08f);
			fillRound(rt, r, 9, fill);
			const D2D1_COLOR_F fg = b.kind ? D2D1::ColorF(1, 1, 1) : ink;
			const float lw = brand::textWidth(b.label, 13, kSemi);
			brand::text(rt, b.label, r.left + 14, r.top + (kButtonH - 13 * 1.34f) / 2, 13, kSemi, fg);
			if (const char* k = keyOf(b)) {
				const float kx = r.left + 14 + lw + 7;
				const D2D1_RECT_F kr = D2D1::RectF(kx, r.top + 9, kx + brand::textWidth(k, 10, kBold) + 10, r.bottom - 9);
				fillRound(rt, kr, 4, withAlpha(fg, 0.16f));
				brand::text(rt, k, kr.left, kr.top + (kr.bottom - kr.top - 10 * 1.34f) / 2, 10, kBold, fg, kr.right - kr.left,
				            DWRITE_TEXT_ALIGNMENT_CENTER);
			}
			if (focus == (int)i)
				strokeRound(rt, D2D1::RectF(r.left - 3, r.top - 3, r.right + 3, r.bottom + 3), 12, withAlpha(accent, 0.9f), 2);
		}
		if (layer) rt->PopLayer();
		rt->SetTransform(was);
	}

	LRESULT handle(UINT msg, WPARAM wp, LPARAM lp) {
		const float s = scale();
		switch (msg) {
		case WM_PAINT: {
			PAINTSTRUCT ps;
			BeginPaint(hwnd, &ps);
			if (ID2D1HwndRenderTarget* rt = surface.begin(hwnd)) {
				RECT rc;
				GetClientRect(hwnd, &rc);
				paint(rt, rc.right / s, rc.bottom / s);
				surface.end();
			}
			EndPaint(hwnd, &ps);
			return 0;
		}
		case WM_ERASEBKGND:
			return 1;
		case WM_NCHITTEST: {
			// Moved by anything that isn't a button or the field.
			POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
			ScreenToClient(hwnd, &p);
			return buttonAt(p.x / s, p.y / s) >= 0 || (a.field && inRect(fieldRect(), p.x / s, p.y / s)) ? HTCLIENT : HTCAPTION;
		}
		case WM_MOUSEMOVE: {
			TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, hwnd, 0 };
			TrackMouseEvent(&t);
			const int i = buttonAt(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s);
			if (i != hot) { hot = i; redraw(); }
			return 0;
		}
		case WM_MOUSELEAVE:
		case WM_NCMOUSEMOVE:
			if (hot != -1 && pressed < 0) { hot = -1; redraw(); }
			break;
		case WM_LBUTTONDOWN:
			if (edit && inRect(fieldRect(), GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s)) { SetFocus(edit); return 0; }
			pressed = hot = buttonAt(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s);
			if (pressed >= 0) SetCapture(hwnd);
			redraw();
			return 0;
		case WM_LBUTTONUP: {
			const int was = pressed;
			pressed = -1;
			if (GetCapture() == hwnd) {
				releasing = true;
				ReleaseCapture();
				releasing = false;
			}
			if (was >= 0 && buttonAt(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s) == was) choose(a.buttons[was].answer);
			redraw();
			return 0;
		}
		case WM_CAPTURECHANGED:
			if ((HWND)lp != hwnd && !releasing && pressed >= 0) { pressed = -1; redraw(); }
			return 0;
		case WM_KEYDOWN:
			if (key((UINT)wp)) return 0;
			break;
		case WM_CLOSE:
			choose(a.escape);
			return 0;
		case WM_DESTROY:
			// Gone with the window it belongs to (as a message box goes):
			// the way out, rather than waiting for ever.
			gone = true;
			if (!done) choose(a.escape);
			break;
		case WM_ACTIVATE:
			// Back from another window: the keyboard where it was.
			if (LOWORD(wp) != WA_INACTIVE) SetFocus(focus == -2 && edit ? edit : hwnd);
			return 0;
		case WM_TIMER:
			if (wp == kAnimTimer) {
				redraw();
				if (nowSeconds() - shown > 0.5) KillTimer(hwnd, kAnimTimer);
			}
			return 0;
		case WM_COMMAND:
			if ((HWND)lp == edit && (HIWORD(wp) == EN_SETFOCUS || HIWORD(wp) == EN_KILLFOCUS)) {
				if (HIWORD(wp) == EN_SETFOCUS) focus = -2;
				redraw();
			}
			return 0;
		case WM_CTLCOLOREDIT:
			SetTextColor((HDC)wp, chrome().gdi(inkOf(prefs().dark)));
			SetBkColor((HDC)wp, editBack);
			return (LRESULT)editBrush;
		case WM_DPICHANGED: {
			const RECT* r = reinterpret_cast<const RECT*>(lp);
			const UINT dpi = HIWORD(wp);
			SetWindowPos(hwnd, nullptr, r->left, r->top, (int)std::ceil(width * dpi / 96.0f), (int)std::ceil(height * dpi / 96.0f),
			             SWP_NOZORDER | SWP_NOACTIVATE);
			placeEdit();
			redraw();
			return 0;
		}
		}
		return DefWindowProcW(hwnd, msg, wp, lp);
	}

	static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
		if (msg == WM_NCCREATE) {
			Card* c = static_cast<Card*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
			c->hwnd = h;
			SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)c);
		}
		Card* c = reinterpret_cast<Card*>(GetWindowLongPtrW(h, GWLP_USERDATA));
		if (c == nullptr) return DefWindowProcW(h, msg, wp, lp);
		LRESULT r = 0;
		bool ok = false;
		guarded("a question", [&] { r = c->handle(msg, wp, lp); ok = true; });
		if (!ok && msg != WM_PAINT) return DefWindowProcW(h, msg, wp, lp);
		if (!ok) ValidateRect(h, nullptr);
		return r;
	}

	// The field's keys: Enter, Escape and Tab are the card's.
	static LRESULT CALLBACK editProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
		Card* c = reinterpret_cast<Card*>(data);
		if (msg == WM_KEYDOWN && (wp == VK_RETURN || wp == VK_ESCAPE || wp == VK_TAB)) {
			guarded("a question", [&] { c->key((UINT)wp); });
			return 0;
		}
		// Ctrl+A selects it all; Ctrl+Alt+A is AltGr+A, a letter on some
		// keyboards (Polish's ą), and is typed.
		if (msg == WM_KEYDOWN && wp == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)) {
			SendMessageW(h, EM_SETSEL, 0, -1);
			return 0;
		}
		if (msg == WM_CHAR && (wp == '\r' || wp == 27 || wp == '\t' || wp == 1)) return 0;   // no beep (1 is Ctrl+A)
		return DefSubclassProc(h, msg, wp, lp);
	}
};

// Without a window of its own (Windows wouldn't make one): the stock box,
// with the default as OK and the way out as Cancel.
int fallback(HWND owner, const Alert& a) {
	const UINT icon = a.badge == 3 ? MB_ICONERROR : a.badge == 2 ? MB_ICONWARNING : a.badge == 1 ? MB_ICONINFORMATION : 0;
	const std::string body = a.text.empty() ? a.heading : a.heading + "\n\n" + a.text;
	const bool two = a.buttons.size() > 1 && a.enter != a.escape;
	const int r = MessageBoxW(owner, W(body).c_str(), L"CedarLogic", (two ? MB_OKCANCEL : MB_OK) | icon);
	return r == IDOK ? a.enter : a.escape;
}

}  // namespace

int runAlert(HWND parent, Alert& a) {
	if (a.buttons.empty()) a.buttons = { { "OK", a.enter, 1 } };
	static bool registered = false;
	if (!registered) {
		registered = true;
		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof wc;
		wc.style = CS_DROPSHADOW;
		wc.lpfnWndProc = Card::proc;
		wc.hInstance = appInstance();
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.hIcon = LoadIconW(appInstance(), MAKEINTRESOURCEW(1));
		wc.lpszClassName = kClass;
		RegisterClassExW(&wc);
	}
	HWND owner = parent ? GetAncestor(parent, GA_ROOT) : nullptr;
	Card c(a);
	c.owner = owner;
	c.calm = reduceMotion();
	c.measure();

	// Over the middle of its window (or the screen), on the screen.
	const UINT dpi = owner ? dpiOf(owner) : GetDpiForSystem();
	const int pw = (int)std::ceil(c.width * dpi / 96.0f), ph = (int)std::ceil(c.height * dpi / 96.0f);
	RECT anchor;
	if (owner == nullptr || !GetWindowRect(owner, &anchor)) {
		MONITORINFO mi = { sizeof mi };
		POINT origin = { 0, 0 };
		GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &mi);
		anchor = mi.rcWork;
	}
	int x = (anchor.left + anchor.right - pw) / 2, y = anchor.top + (anchor.bottom - anchor.top - ph) * 2 / 5;
	MONITORINFO mi = { sizeof mi };
	if (GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &mi)) {
		x = std::max<int>(mi.rcWork.left, std::min<int>(x, mi.rcWork.right - pw));
		y = std::max<int>(mi.rcWork.top, std::min<int>(y, mi.rcWork.bottom - ph));
	}
	const std::string title = a.title.empty() ? a.heading : a.title;
	if (CreateWindowExW(owner ? 0 : WS_EX_APPWINDOW, kClass, W(title).c_str(), WS_POPUP | WS_CLIPCHILDREN, x, y, pw, ph, owner, nullptr,
	                    appInstance(), &c) == nullptr)
		return fallback(owner, a);
	const DWORD round = 2;   // DWMWCP_ROUND (Windows 11; Windows 10 says no)
	c.rounded = SUCCEEDED(DwmSetWindowAttribute(c.hwnd, 33, &round, sizeof round));
	setDarkTitleBar(c.hwnd, prefs().dark);
	if (a.field) {
		c.edit = CreateWindowExW(0, L"EDIT", W(a.value).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, c.hwnd,
		                         nullptr, appInstance(), nullptr);
		const bool dark = prefs().dark;
		c.editBack = gdiMix(paper(dark), inkOf(dark), dark ? 0.07f : 0.05f);
		c.editBrush = CreateSolidBrush(c.editBack);
		if (!a.placeholder.empty()) SendMessageW(c.edit, EM_SETCUEBANNER, TRUE, (LPARAM)W(a.placeholder).c_str());
		if (dark) darkenControl(c.edit, true, L"CFD");
		SetWindowSubclass(c.edit, Card::editProc, 1, (DWORD_PTR)&c);
		c.placeEdit();
		SendMessageW(c.edit, EM_SETSEL, 0, -1);
		c.focus = -2;
	}

	const bool ownerWasEnabled = owner && IsWindowEnabled(owner);
	if (ownerWasEnabled) EnableWindow(owner, FALSE);
	c.shown = nowSeconds();
	ShowWindow(c.hwnd, SW_SHOW);
	SetForegroundWindow(c.hwnd);
	SetFocus(c.edit ? c.edit : c.hwnd);
	if (!c.calm) SetTimer(c.hwnd, kAnimTimer, 15, nullptr);
	MSG m = {};
	while (!c.done && GetMessageW(&m, nullptr, 0, 0) > 0) {
		TranslateMessage(&m);
		DispatchMessageW(&m);
	}
	if (m.message == WM_QUIT) PostQuitMessage((int)m.wParam);
	if (c.edit && !c.gone) {
		a.value = trimmed(windowText(c.edit));
		RemoveWindowSubclass(c.edit, Card::editProc, 1);
	}
	// The window it belongs to takes the keyboard back as this one goes.
	if (ownerWasEnabled) EnableWindow(owner, TRUE);
	if (!c.gone) {
		KillTimer(c.hwnd, kAnimTimer);
		SetWindowLongPtrW(c.hwnd, GWLP_USERDATA, 0);
		DestroyWindow(c.hwnd);
	}
	if (owner && ownerWasEnabled) SetForegroundWindow(owner);
	if (c.editFont) DeleteObject(c.editFont);
	if (c.editBrush) DeleteObject(c.editBrush);
	return c.done ? c.answer : a.escape;
}

bool askConfirm(HWND parent, const std::string& heading, const std::string& text, const std::string& yes, const std::string& no,
                bool destructive) {
	Alert a;
	a.heading = heading;
	a.text = text;
	a.badge = destructive ? 2 : 0;
	a.buttons = { { yes, 1, destructive ? 2 : 1 }, { no, 0, 0 } };
	a.escape = 0;
	a.enter = 1;
	return runAlert(parent, a) == 1;
}

// ---- The app's notes and questions (App.h) ---------------------------------------

void showMessage(HWND parent, Tone tone, const std::string& title, const std::string& text) {
	Alert a;
	a.heading = title;
	a.text = text;
	a.badge = tone == Tone::Error ? 3 : tone == Tone::Warning ? 2 : 1;
	a.buttons = { { "OK", 1, 1 } };
	a.escape = a.enter = 1;
	runAlert(parent, a);
}

bool askYesNo(HWND parent, const std::string& title, const std::string& text) { return askConfirm(parent, title, text, "Yes", "No"); }
