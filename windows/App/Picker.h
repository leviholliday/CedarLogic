// Picker: the window Your Circuits, Version History, New from Template and
// the like are made of, drawn as the Mac app's LibraryView: a heading, a
// quiet line under it, an optional search field, a list of tall rows (a gate
// tile, a name, a second line, a badge), an optional picture beside it, and
// buttons along the bottom. It runs modally over the circuit window.

#ifndef CL_WINDOWS_PICKER_H
#define CL_WINDOWS_PICKER_H

#include "App.h"
#include "Chrome.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace picker {

// LibraryDialogs' colours (the Mac's PickerLook).
struct Look {
	bool dark;
	D2D1_COLOR_F paper() const { return dark ? rgb255(28, 31, 37) : rgb255(250, 250, 252); }
	D2D1_COLOR_F ink(float a = 1) const { return dark ? rgb255(226, 230, 238, a) : rgb255(30, 33, 40, a); }
	D2D1_COLOR_F sheet() const { return dark ? rgb255(22, 24, 29) : rgb255(255, 255, 255); }
};

struct Row {
	std::string id, title, subtitle, badge;
	bool heading = false;   // a section's title ("BUILT IN"), not chosen
	bool tile = true;       // the gate tile in front
};

const float kRowH = 62, kMargin = 22;
const int kSearchId = 10;

// A tile with a logic-gate silhouette, tinted by the accent.
inline void gateTile(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, D2D1_COLOR_F accent, bool on) {
	fillRound(rt, r, 11, withAlpha(accent, on ? 0.22f : 0.13f));
	ID2D1Factory* f = nullptr;
	rt->GetFactory(&f);
	ID2D1PathGeometry* g = nullptr;
	ID2D1SolidColorBrush* b = nullptr;
	if (f && SUCCEEDED(f->CreatePathGeometry(&g)) && SUCCEEDED(rt->CreateSolidColorBrush(withAlpha(accent, 0.95f), &b))) {
		const float s = (r.right - r.left) / 40, cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
		ID2D1GeometrySink* k = nullptr;
		g->Open(&k);
		k->BeginFigure(D2D1::Point2F(cx - 8 * s, cy - 8 * s), D2D1_FIGURE_BEGIN_HOLLOW);
		k->AddLine(D2D1::Point2F(cx - 1 * s, cy - 8 * s));
		k->AddBezier(D2D1::BezierSegment(D2D1::Point2F(cx + 9 * s, cy - 8 * s), D2D1::Point2F(cx + 9 * s, cy + 8 * s),
		                                 D2D1::Point2F(cx - 1 * s, cy + 8 * s)));
		k->AddLine(D2D1::Point2F(cx - 8 * s, cy + 8 * s));
		k->EndFigure(D2D1_FIGURE_END_CLOSED);
		k->Close();
		k->Release();
		rt->DrawGeometry(g, b, 1.6f * s);
		b->SetColor(withAlpha(accent, 0.7f));
		rt->DrawLine(D2D1::Point2F(cx - 14 * s, cy - 4.5f * s), D2D1::Point2F(cx - 8 * s, cy - 4.5f * s), b, 1.6f * s);
		rt->DrawLine(D2D1::Point2F(cx - 14 * s, cy + 4.5f * s), D2D1::Point2F(cx - 8 * s, cy + 4.5f * s), b, 1.6f * s);
		rt->DrawLine(D2D1::Point2F(cx + 6.5f * s, cy), D2D1::Point2F(cx + 14 * s, cy), b, 1.6f * s);
	}
	if (b) b->Release();
	if (g) g->Release();
	if (f) f->Release();
}

// The window both pickers are: drawn with Direct2D, run modally over the
// circuit window.
class Picker {
public:
	std::string title, line, emptyText = "Nothing here yet.";
	bool search = true;
	float width = 600, height = 540, listWidth = 0;   // 0: the list takes the width
	bool listOnLeft = false;                          // with a preview: the list first
	std::vector<std::string> leftButtons, rightButtons;   // the last right one is the default
	std::function<std::vector<Row>(const std::string& query)> rows;
	// A button pressed (left ones 0.., right ones 100..): true closes.
	std::function<bool(Picker&, int button)> onButton;
	std::function<bool(Picker&, UINT vk, bool ctrl)> onKey;
	std::function<void(ID2D1RenderTarget*, const D2D1_RECT_F&)> preview;
	std::function<void(Picker&)> onSelect;

	std::vector<Row> shown;
	int selection = 0;
	HWND hwnd = nullptr, owner = nullptr, edit = nullptr;

	void reload() {
		const std::string id = selection >= 0 && selection < (int)shown.size() ? shown[selection].id : std::string();
		shown = rows(edit ? windowText(edit) : std::string());
		selection = 0;
		while (selection < (int)shown.size() && shown[selection].heading) selection++;
		for (int i = 0; i < (int)shown.size(); i++) if (!shown[i].heading && shown[i].id == id) selection = i;
		clampScroll();
		if (onSelect) onSelect(*this);
		redraw();
	}
	void select(int i, int direction = 1) {
		if (shown.empty()) return;
		const int was = selection;
		i = std::max(0, std::min((int)shown.size() - 1, i));
		// Past a heading, the way the move was going (or back, at an end).
		while (i >= 0 && i < (int)shown.size() && shown[i].heading) i += direction;
		if (i < 0 || i >= (int)shown.size()) {
			i = std::max(0, std::min((int)shown.size() - 1, i));
			while (i >= 0 && i < (int)shown.size() && shown[i].heading) i -= direction;
		}
		if (i < 0 || i >= (int)shown.size()) return;
		selection = i;
		// Into view.
		const D2D1_RECT_F l = listRect();
		const float top = selection * kRowH, viewH = l.bottom - l.top - 8;
		if (top < scroll) scroll = top;
		if (top + kRowH > scroll + viewH) scroll = top + kRowH - viewH;
		clampScroll();
		if (was != selection && onSelect) onSelect(*this);
		redraw();
	}
	const Row* selected() const {
		return selection >= 0 && selection < (int)shown.size() && !shown[selection].heading ? &shown[selection] : nullptr;
	}
	void redraw() { if (hwnd) InvalidateRect(hwnd, nullptr, FALSE); }
	void close() { done = true; }

	void run(HWND ownerWindow) {
		owner = ownerWindow;
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
			wc.lpszClassName = L"CedarLogicPicker";
			RegisterClassExW(&wc);
		}
		const UINT dpi = dpiOf(owner);
		RECT o;
		GetWindowRect(owner, &o);
		RECT r = { 0, 0, scaled((int)width, dpi), scaled((int)height, dpi) };
		AdjustWindowRectExForDpi(&r, WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, FALSE, 0, dpi);
		const int w = r.right - r.left, h = r.bottom - r.top;
		hwnd = CreateWindowExW(0, L"CedarLogicPicker", W(title).c_str(), WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN,
		                       (o.left + o.right - w) / 2, (o.top + o.bottom - h) / 2, w, h, owner, nullptr, appInstance(), this);
		setDarkTitleBar(hwnd, prefs().dark);
		if (search) {
			edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, hwnd,
			                       (HMENU)(INT_PTR)kSearchId, appInstance(), nullptr);
			SendMessageW(edit, EM_SETCUEBANNER, TRUE, (LPARAM)(L"Search " + W(lowerCase(title))).c_str());
			SendMessageW(edit, WM_SETFONT, (WPARAM)uiFont(dpi), TRUE);
			SetWindowSubclass(edit, editProc, 1, (DWORD_PTR)this);
		}
		reload();
		layout();
		EnableWindow(owner, FALSE);
		ShowWindow(hwnd, SW_SHOW);
		SetFocus(edit ? edit : hwnd);
		MSG m = {};
		while (!done && GetMessageW(&m, nullptr, 0, 0) > 0) {
			TranslateMessage(&m);
			DispatchMessageW(&m);
		}
		if (m.message == WM_QUIT) PostQuitMessage((int)m.wParam);
		EnableWindow(owner, TRUE);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
		DestroyWindow(hwnd);
		hwnd = nullptr;
		SetForegroundWindow(owner);
	}

private:
	WindowSurface surface;
	float scroll = 0;
	int hot = -1, hotButton = -1;
	bool done = false;
	struct Button { D2D1_RECT_F rect; int id; std::string label; bool primary; };
	std::vector<Button> buttons;

	float scale() const { return dpiOf(hwnd) / 96.0f; }
	float clientW() const { RECT rc; GetClientRect(hwnd, &rc); return rc.right / scale(); }
	float clientH() const { RECT rc; GetClientRect(hwnd, &rc); return rc.bottom / scale(); }
	float top() const { return kMargin + 34 + 44 + (search ? 44 : 0); }
	D2D1_RECT_F listRect() const {
		const float w = clientW(), h = clientH();
		if (listWidth > 0 && listOnLeft) return D2D1::RectF(kMargin - 8, top(), kMargin + listWidth, h - kMargin - 30 - 18);
		const float left = listWidth > 0 ? w - kMargin - listWidth : kMargin - 8;
		return D2D1::RectF(left, top(), w - kMargin + 8, h - kMargin - 30 - 18);
	}
	void clampScroll() {
		const D2D1_RECT_F l = listRect();
		scroll = std::max(0.0f, std::min(scroll, shown.size() * kRowH - (l.bottom - l.top - 8)));
	}
	void layout() {
		if (edit) {
			const float s = scale();
			HDC dc = GetDC(edit);
			TEXTMETRICW tm = {};
			HGDIOBJ old = SelectObject(dc, uiFont(dpiOf(hwnd)));
			GetTextMetricsW(dc, &tm);
			SelectObject(dc, old);
			ReleaseDC(edit, dc);
			const float fieldTop = kMargin + 34 + 44, fieldH = 30;
			MoveWindow(edit, (int)((kMargin + 30) * s), (int)((fieldTop + fieldH / 2) * s) - tm.tmHeight / 2,
			           (int)((clientW() - 2 * kMargin - 40) * s), tm.tmHeight, TRUE);
		}
		clampScroll();
		redraw();
	}

	void paint() {
		PAINTSTRUCT ps;
		BeginPaint(hwnd, &ps);
		ID2D1HwndRenderTarget* rt = surface.begin(hwnd);
		if (rt == nullptr) { EndPaint(hwnd, &ps); return; }
		const Look look{ prefs().dark };
		const D2D1_COLOR_F accent = chrome().accent();
		const float w = clientW(), h = clientH();
		rt->Clear(look.paper());
		drawText(rt, title, D2D1::RectF(kMargin, kMargin, w - kMargin, kMargin + 26), 19, look.ink(), TextAlign::Leading, true);
		// The line under the heading, wrapped.
		if (IDWriteFactory* dw = dwFactory()) {
			IDWriteTextFormat* f = nullptr;
			if (SUCCEEDED(dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
			                                   DWRITE_FONT_STRETCH_NORMAL, 13, L"", &f))) {
				ID2D1SolidColorBrush* b = nullptr;
				if (SUCCEEDED(rt->CreateSolidColorBrush(look.ink(0.55f), &b))) {
					const std::wstring t = W(line);
					rt->DrawText(t.c_str(), (UINT32)t.size(), f, D2D1::RectF(kMargin, kMargin + 34, w - kMargin, kMargin + 34 + 40), b);
					b->Release();
				}
				f->Release();
			}
		}
		if (search) {
			const float fieldTop = kMargin + 34 + 44;
			const D2D1_RECT_F field = D2D1::RectF(kMargin, fieldTop, w - kMargin, fieldTop + 30);
			fillRound(rt, field, 8, look.dark ? rgb255(40, 44, 52) : rgb255(255, 255, 255));
			strokeRound(rt, field, 8, look.ink(0.14f));
			drawIcon(rt, Icon::Search, D2D1::RectF(field.left + 6, field.top, field.left + 28, field.bottom), 12, look.ink(0.5f));
		}

		if (preview) {
			const D2D1_RECT_F l = listRect();
			const D2D1_RECT_F pr = listOnLeft ? D2D1::RectF(l.right + 14, top(), w - kMargin, l.bottom)
			                                  : D2D1::RectF(kMargin, top(), l.left - 14, l.bottom);
			fillRound(rt, pr, 12, look.sheet());
			strokeRound(rt, pr, 12, look.ink(0.12f));
			preview(rt, D2D1::RectF(pr.left + 12, pr.top + 12, pr.right - 12, pr.bottom - 12));
		}

		// The rows.
		const D2D1_RECT_F l = listRect();
		rt->PushAxisAlignedClip(l, D2D1_ANTIALIAS_MODE_ALIASED);
		for (int i = 0; i < (int)shown.size(); i++) {
			const float y = l.top + 4 + i * kRowH - scroll;
			if (y + kRowH < l.top || y > l.bottom) continue;
			const Row& row = shown[i];
			if (row.heading) {
				std::string h = row.title;
				for (char& c : h) c = (char)toupper((unsigned char)c);
				drawText(rt, h, D2D1::RectF(l.left + 22, y + kRowH - 26, l.right - 8, y + kRowH - 8), 10.5f, look.ink(0.45f),
				         TextAlign::Leading, true);
				if (!row.subtitle.empty())
					drawText(rt, row.subtitle, D2D1::RectF(l.left + 22, y + kRowH, l.right - 8, y + kRowH + 22), 12, look.ink(0.5f));
				continue;
			}
			const bool sel = i == selection, isHot = i == hot;
			const D2D1_RECT_F r = D2D1::RectF(l.left + 8, y + 4, l.right - 8, y + kRowH - 4);
			if (sel) fillRound(rt, r, 12, withAlpha(accent, look.dark ? 0.26f : 0.16f));
			else if (isHot) fillRound(rt, r, 12, look.ink(0.06f));
			const float textLeft = row.tile ? r.left + 68 : r.left + 14;
			if (row.tile) gateTile(rt, D2D1::RectF(r.left + 12, y + 11, r.left + 52, y + 51), accent, sel);
			float right = r.right - 16;
			if (!row.badge.empty()) {
				const float bw = textWidth(row.badge, 9, true) + 16;
				const D2D1_RECT_F badge = D2D1::RectF(right - bw, y + 21, right, y + 41);
				fillRound(rt, badge, 10, withAlpha(accent, 0.18f));
				drawText(rt, row.badge, badge, 9, accent, TextAlign::Center, true);
				right -= bw + 8;
			}
			drawText(rt, row.title, D2D1::RectF(textLeft, y + 12, right, y + 31), 13, look.ink(), TextAlign::Leading, true);
			drawText(rt, row.subtitle, D2D1::RectF(textLeft, y + 32, right, y + 48), 11, look.ink(0.55f));
			if (!sel && !isHot && i + 1 < (int)shown.size() && !shown[i + 1].heading && selection != i + 1 && hot != i + 1)
				fillRect(rt, D2D1::RectF(textLeft, y + kRowH - 1, r.right - 12, y + kRowH), look.ink(0.08f));
		}
		if (shown.empty())
			drawText(rt, emptyText, D2D1::RectF(l.left, l.top + 30, l.right, l.top + 60), 13, look.ink(0.55f), TextAlign::Center);
		rt->PopAxisAlignedClip();

		// The buttons along the bottom.
		buttons.clear();
		const float by = h - kMargin - 30;
		float x = kMargin;
		for (size_t i = 0; i < leftButtons.size(); i++) {
			const float bw = std::max(80.0f, textWidth(leftButtons[i], 12.5f) + 28);
			buttons.push_back({ D2D1::RectF(x, by, x + bw, by + 30), (int)i, leftButtons[i], false });
			x += bw + 8;
		}
		x = w - kMargin;
		for (int i = (int)rightButtons.size() - 1; i >= 0; i--) {
			const float bw = std::max(84.0f, textWidth(rightButtons[i], 12.5f) + 28);
			x -= bw;
			buttons.push_back({ D2D1::RectF(x, by, x + bw, by + 30), 100 + i, rightButtons[i], i == (int)rightButtons.size() - 1 });
			x -= 8;
		}
		for (size_t i = 0; i < buttons.size(); i++) {
			const Button& b = buttons[i];
			const bool bh = (int)i == hotButton;
			if (b.primary) fillRound(rt, b.rect, 8, withAlpha(accent, bh ? 1.0f : 0.92f));
			else {
				fillRound(rt, b.rect, 8, look.ink(bh ? 0.12f : 0.07f));
				strokeRound(rt, b.rect, 8, look.ink(0.10f));
			}
			drawText(rt, b.label, b.rect, 12.5f, b.primary ? chrome().onAccent() : look.ink(), TextAlign::Center, b.primary);
		}
		surface.end();
		EndPaint(hwnd, &ps);
	}

	int rowAt(float x, float y) const {
		const D2D1_RECT_F l = listRect();
		if (!inRect(l, x, y)) return -1;
		const int i = (int)((y - l.top - 4 + scroll) / kRowH);
		return i >= 0 && i < (int)shown.size() ? i : -1;
	}
	int buttonAt(float x, float y) const {
		for (size_t i = 0; i < buttons.size(); i++) if (inRect(buttons[i].rect, x, y)) return (int)i;
		return -1;
	}
	void press(int id) {
		if (onButton && onButton(*this, id)) done = true;
		else redraw();
	}
	bool key(UINT vk) {
		const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
		if (onKey && onKey(*this, vk, ctrl)) return true;
		switch (vk) {
		case VK_DOWN: select(selection + 1); return true;
		case VK_UP: select(selection - 1, -1); return true;
		case VK_NEXT: select(selection + 8); return true;
		case VK_PRIOR: select(selection - 8, -1); return true;
		case VK_RETURN: if (!rightButtons.empty()) press(100 + (int)rightButtons.size() - 1); return true;
		case VK_ESCAPE: done = true; return true;
		default: return false;
		}
	}

	static LRESULT CALLBACK editProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
		Picker* p = reinterpret_cast<Picker*>(data);
		if (msg == WM_KEYDOWN) {
			// Delete too while there's nothing typed (Your Circuits deletes the circuit).
			const bool mine = wp == VK_UP || wp == VK_DOWN || wp == VK_PRIOR || wp == VK_NEXT || wp == VK_RETURN || wp == VK_ESCAPE ||
			                  (GetKeyState(VK_CONTROL) & 0x8000) || (wp == VK_DELETE && GetWindowTextLengthW(h) == 0);
			if (mine && p->key((UINT)wp)) return 0;
		}
		if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) return 0;   // no beep
		return DefSubclassProc(h, msg, wp, lp);
	}

	static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
		if (msg == WM_NCCREATE) SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
		Picker* p = reinterpret_cast<Picker*>(GetWindowLongPtrW(h, GWLP_USERDATA));
		if (p == nullptr) return DefWindowProcW(h, msg, wp, lp);
		if (msg == WM_NCCREATE) p->hwnd = h;
		LRESULT r = 0;
		bool handled = false;
		guarded("Your Circuits", [&] { handled = p->handle(msg, wp, lp, r); });
		return handled ? r : DefWindowProcW(h, msg, wp, lp);
	}

	bool handle(UINT msg, WPARAM wp, LPARAM lp, LRESULT& r) {
		const float s = scale();
		const float x = GET_X_LPARAM(lp) / s, y = GET_Y_LPARAM(lp) / s;
		switch (msg) {
		case WM_PAINT: paint(); return true;
		case WM_ERASEBKGND: r = 1; return true;
		case WM_SIZE: layout(); return true;
		case WM_GETMINMAXINFO: {
			MINMAXINFO* m = reinterpret_cast<MINMAXINFO*>(lp);
			m->ptMinTrackSize = { scaled((int)(width * 0.85f), dpiOf(hwnd)), scaled((int)(height * 0.75f), dpiOf(hwnd)) };
			return true;
		}
		case WM_CLOSE: done = true; return true;
		case WM_COMMAND:
			if (LOWORD(wp) == kSearchId && HIWORD(wp) == EN_CHANGE) { scroll = 0; reload(); }
			return true;
		case WM_CTLCOLOREDIT: {
			const Look look{ prefs().dark };
			static HBRUSH light = CreateSolidBrush(RGB(255, 255, 255)), dark = CreateSolidBrush(RGB(40, 44, 52));
			SetBkColor((HDC)wp, look.dark ? RGB(40, 44, 52) : RGB(255, 255, 255));
			SetTextColor((HDC)wp, look.dark ? RGB(226, 230, 238) : RGB(30, 33, 40));
			r = (LRESULT)(look.dark ? dark : light);
			return true;
		}
		case WM_MOUSEMOVE: {
			TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, hwnd, 0 };
			TrackMouseEvent(&t);
			const int row = rowAt(x, y), b = buttonAt(x, y);
			if (row != hot || b != hotButton) { hot = row; hotButton = b; redraw(); }
			return true;
		}
		case WM_MOUSELEAVE: hot = hotButton = -1; redraw(); return true;
		case WM_LBUTTONDOWN: {
			const int row = rowAt(x, y);
			if (row >= 0 && !shown[row].heading) select(row);
			return true;
		}
		case WM_LBUTTONDBLCLK:
			if (rowAt(x, y) >= 0 && !shown[rowAt(x, y)].heading && !rightButtons.empty()) press(100 + (int)rightButtons.size() - 1);
			return true;
		case WM_LBUTTONUP: {
			const int b = buttonAt(x, y);
			if (b >= 0) press(buttons[b].id);
			return true;
		}
		case WM_MOUSEWHEEL:
			scroll -= GET_WHEEL_DELTA_WPARAM(wp) * 62.0f / WHEEL_DELTA;
			clampScroll();
			redraw();
			return true;
		case WM_KEYDOWN: return key((UINT)wp);
		}
		return false;
	}
};


}  // namespace picker

#endif  // CL_WINDOWS_PICKER_H
