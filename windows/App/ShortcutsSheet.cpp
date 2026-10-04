// Every shortcut (Ctrl+/ or ?), as the Mac app's ShortcutsSheet and the Linux
// app's: sections in two columns, keys drawn as key caps, a search across the
// top. Arrow to a row and press Enter (or click it again) to do it. The keys
// shown are yours: Change Shortcuts… opens Settings > Shortcuts.

#include "Dialogs.h"
#include "Chrome.h"
#include "Shortcuts.h"
#include "Window.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

struct Row {
	std::string section, what;
	std::vector<std::string> keys;
	const shortcuts::Action* action = nullptr;   // what it does, when it's a command
};

std::vector<Row> allRows() {
	std::vector<Row> out;
	auto capsOf = [](const char* id) {
		const shortcuts::Action* a = shortcuts::find(id);
		const shortcuts::Key k = a ? shortcuts::first(*a) : shortcuts::Key();
		return k.valid() ? shortcuts::caps(k) : std::vector<std::string>{ "none" };
	};
	for (const shortcuts::Action& a : shortcuts::all()) {
		out.push_back({ a.section, a.name, capsOf(a.id), &a });
		const std::string id = a.id;
		auto extra = [&](const char* section, std::vector<std::string> keys, const char* what) {
			out.push_back({ section, what, std::move(keys), nullptr });
		};
		if (id == "selectAll") {
			extra("Editing", { "Del" }, "Delete the selection");
			extra("Editing", { "Esc" }, "Cancel a drag, paste or connection");
			extra("Editing", { "Shift", "click" }, "Add to or remove from the selection");
		}
		if (id == "tidy") {
			std::vector<std::string> connect = capsOf("quickCopy");
			connect.push_back("while dragging");
			extra("Building", { "Shift", "1-0" }, "Jump to a gate category");
			extra("Building", { "←", "↑", "→", "↓" }, "Nudge the selection (Shift: 5 squares)");
			extra("Building", { "click a pin, then another" }, "Connect them");
			extra("Building", connect, "Drop it and connect to pins nearby");
			extra("Building", { "double-click a gate" }, "Change its settings");
		}
		if (id == "palette") {
			extra("Moving around", { "Space" }, "Zoom to fit (tap)");
			extra("Moving around", { "Space", "drag" }, "Move around");
			extra("Moving around", { "middle button", "drag" }, "Move around");
			extra("Moving around", { "Ctrl", "scroll" }, "Zoom (or pinch)");
			extra("Moving around", { "Shift", "scroll" }, "Move sideways");
		}
		if (id == "lock") {
			extra("Simulation", { "Space" }, "Pause or resume (in Simulation View)");
			extra("Simulation", { "Esc" }, "Leave Simulation View");
		}
		if (id == "previousTab") {
			extra("Tabs", { "double-click a tab" }, "Rename it");
			extra("Tabs", { "Ctrl", "Tab" }, "Switch tabs (hold for a picture of each)");
		}
		if (id == "feedback") {
			extra("App", { "Alt" }, "Every menu (F10 too)");
			extra("App", { "?" }, "This list, from the canvas");
		}
	}
	return out;
}

const float kMargin = 22, kTop = 66, kRowH = 28, kHeadH = 30, kFooterH = 58;

class ShortcutsSheet {
public:
	std::vector<Row> rows = allRows();
	std::vector<int> shown;          // indexes into rows
	int selected = 0, hot = -1;
	const Row* chosen = nullptr;     // what to do once it's closed
	bool toSettings = false;
	HWND hwnd = nullptr, owner = nullptr, edit = nullptr;

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
			wc.lpszClassName = L"CedarLogicShortcuts";
			RegisterClassExW(&wc);
		}
		refilter();
		const UINT dpi = dpiOf(owner);
		RECT o;
		GetWindowRect(owner, &o);
		// 880 x 620 points, or the screen's size when that's smaller.
		MONITORINFO mi = { sizeof mi };
		GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &mi);
		RECT r = { 0, 0, scaled(880, dpi), scaled(620, dpi) };
		AdjustWindowRectExForDpi(&r, WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, FALSE, 0, dpi);
		const int w = std::min<int>(r.right - r.left, mi.rcWork.right - mi.rcWork.left);
		const int h = std::min<int>(r.bottom - r.top, mi.rcWork.bottom - mi.rcWork.top);
		int x = (o.left + o.right - w) / 2, y = (o.top + o.bottom - h) / 2;
		x = std::max<int>(mi.rcWork.left, std::min<int>(x, mi.rcWork.right - w));
		y = std::max<int>(mi.rcWork.top, std::min<int>(y, mi.rcWork.bottom - h));
		hwnd = CreateWindowExW(0, L"CedarLogicShortcuts", L"Keyboard Shortcuts", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN,
		                       x, y, w, h, owner, nullptr, appInstance(), this);
		setDarkTitleBar(hwnd, prefs().dark);
		edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, hwnd, nullptr, appInstance(), nullptr);
		SendMessageW(edit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search");
		SendMessageW(edit, WM_SETFONT, (WPARAM)uiFont(dpi), TRUE);
		SetWindowSubclass(edit, editProc, 1, (DWORD_PTR)this);
		layout();
		EnableWindow(owner, FALSE);
		ShowWindow(hwnd, SW_SHOW);
		SetFocus(edit);
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
	float scroll = 0, contentH = 0;
	bool done = false;
	bool changeHot = false;
	std::vector<float> rowTop;       // each shown row's top, unscrolled (as last drawn)
	struct Hit { D2D1_RECT_F rect; int index; };
	std::vector<Hit> hits;
	D2D1_RECT_F changeButton = D2D1::RectF(0, 0, 0, 0);

	float scale() const { return dpiOf(hwnd) / 96.0f; }
	float clientW() const { RECT rc; GetClientRect(hwnd, &rc); return rc.right / scale(); }
	float clientH() const { RECT rc; GetClientRect(hwnd, &rc); return rc.bottom / scale(); }
	float viewH() const { return clientH() - kTop - kFooterH; }
	D2D1_RECT_F field() const { const float w = clientW(); return D2D1::RectF(w - kMargin - 260, 18, w - kMargin, 46); }
	void redraw() { if (hwnd) InvalidateRect(hwnd, nullptr, FALSE); }

	void refilter() {
		shown.clear();
		const std::string q = lowerCase(edit ? windowText(edit) : std::string());
		for (int i = 0; i < (int)rows.size(); i++) {
			const Row& r = rows[i];
			std::string all = r.what + " " + r.section;
			for (const std::string& k : r.keys) all += " " + k;
			if (q.empty() || lowerCase(all).find(q) != std::string::npos) shown.push_back(i);
		}
		selected = std::min(selected, std::max(0, (int)shown.size() - 1));
		scroll = 0;
		hot = -1;
		rowTop.clear();
	}
	void clampScroll() { scroll = std::max(0.0f, std::min(scroll, contentH - viewH())); }
	void layout() {
		if (edit) {
			const float s = scale();
			HDC dc = GetDC(edit);
			TEXTMETRICW tm = {};
			HGDIOBJ old = SelectObject(dc, uiFont(dpiOf(hwnd)));
			GetTextMetricsW(dc, &tm);
			SelectObject(dc, old);
			ReleaseDC(edit, dc);
			const D2D1_RECT_F f = field();
			MoveWindow(edit, (int)((f.left + 28) * s), (int)((f.top + f.bottom) / 2 * s) - tm.tmHeight / 2, (int)((f.right - f.left - 36) * s),
			           tm.tmHeight, TRUE);
		}
		clampScroll();
		redraw();
	}
	// The chosen row in view.
	void select(int k) {
		if (shown.empty()) return;
		selected = std::max(0, std::min((int)shown.size() - 1, k));
		if (selected < (int)rowTop.size()) {
			const float top = rowTop[selected];
			if (top - kHeadH < scroll) scroll = std::max(0.0f, top - kHeadH);
			if (top + kRowH > scroll + viewH()) scroll = top + kRowH - viewH();
			clampScroll();
		}
		redraw();
	}
	void choose(int k) {
		if (k < 0 || k >= (int)shown.size()) return;
		const Row& r = rows[shown[k]];
		if (r.action == nullptr) { MessageBeep(MB_OK); return; }   // a fixed key: nothing to do from here
		chosen = &r;
		done = true;
	}

	void paint() {
		PAINTSTRUCT ps;
		BeginPaint(hwnd, &ps);
		ID2D1HwndRenderTarget* rt = surface.begin(hwnd);
		if (rt == nullptr) { EndPaint(hwnd, &ps); return; }
		const bool dark = prefs().dark;
		const D2D1_COLOR_F paper = dark ? rgb255(28, 31, 37) : rgb255(250, 250, 252);
		const D2D1_COLOR_F ink = dark ? rgb255(226, 230, 238) : rgb255(30, 33, 40);
		const D2D1_COLOR_F accent = chrome().accent();
		const float w = clientW(), h = clientH();
		rt->Clear(paper);
		drawText(rt, "Keyboard Shortcuts", D2D1::RectF(kMargin, 16, w - kMargin - 280, 48), 19, ink, TextAlign::Leading, true);
		const D2D1_RECT_F f = field();
		const bool focus = GetFocus() == edit;
		fillRound(rt, f, 8, withAlpha(ink, 0.06f));
		strokeRound(rt, f, 8, focus ? withAlpha(accent, 0.7f) : withAlpha(ink, 0.1f), focus ? 2.0f : 1.0f);
		drawIcon(rt, Icon::Search, D2D1::RectF(f.left + 6, f.top, f.left + 26, f.bottom), 12, withAlpha(ink, 0.45f));

		// Sections in two columns, about the same length.
		std::vector<std::string> sections;
		std::vector<int> counts;
		for (int i : shown) {
			auto at = std::find(sections.begin(), sections.end(), rows[i].section);
			if (at == sections.end()) { sections.push_back(rows[i].section); counts.push_back(1); }
			else counts[at - sections.begin()]++;
		}
		int total = 0, sum = 0;
		for (int c : counts) total += c;
		size_t half = 0;
		while (half < sections.size() && (half == 0 || sum + counts[half] / 2 <= total / 2)) sum += counts[half++];
		const float colW = (w - 2 * kMargin - 28) / 2;
		const D2D1_RECT_F view = D2D1::RectF(0, kTop, w, h - kFooterH);
		rt->PushAxisAlignedClip(view, D2D1_ANTIALIAS_MODE_ALIASED);
		hits.clear();
		rowTop.assign(shown.size(), 0);
		float tallest = 0;
		for (int col = 0; col < 2; col++) {
			float y = kTop + 4 - scroll;
			const float x0 = kMargin + col * (colW + 28);
			for (size_t si = col == 0 ? 0 : half; si < (col == 0 ? half : sections.size()); si++) {
				std::string up = sections[si];
				for (char& c : up) c = (char)toupper((unsigned char)c);
				drawText(rt, up, D2D1::RectF(x0, y + 6, x0 + colW, y + 24), 10.5f, accent, TextAlign::Leading, true);
				y += kHeadH;
				for (int k = 0; k < (int)shown.size(); k++) {
					const Row& r = rows[shown[k]];
					if (r.section != sections[si]) continue;
					const D2D1_RECT_F rr = D2D1::RectF(x0 - 8, y, x0 + colW + 8, y + kRowH);
					rowTop[k] = y + scroll - kTop;
					if (k == selected) fillRound(rt, rr, 7, withAlpha(accent, dark ? 0.24f : 0.14f));
					else if (k == hot) fillRound(rt, rr, 7, withAlpha(ink, 0.05f));
					const float capsW = shortcuts::capsWidth(r.keys);
					drawText(rt, r.what, D2D1::RectF(x0, y, x0 + colW - capsW - 10, y + kRowH), 12.5f, withAlpha(ink, r.action ? 0.95f : 0.62f));
					shortcuts::drawCaps(rt, r.keys, x0 + colW, y + kRowH / 2, ink);
					// Rows scrolled out of the view can't be clicked there.
					if (rr.bottom > view.top && rr.top < view.bottom) hits.push_back({ rr, k });
					y += kRowH;
				}
				y += 12;
			}
			tallest = std::max(tallest, y + scroll - kTop);
		}
		contentH = tallest;
		if (shown.empty()) drawText(rt, "No shortcuts match that.", D2D1::RectF(0, kTop + 30, w, kTop + 60), 13, withAlpha(ink, 0.55f), TextAlign::Center);
		rt->PopAxisAlignedClip();
		// A thin bar on the right, when there's more than fits.
		const float vh = viewH();
		if (contentH > vh + 1) {
			const float trackH = vh - 8, thumbH = std::max(30.0f, trackH * vh / contentH);
			const float at = kTop + 4 + (trackH - thumbH) * (scroll / std::max(1.0f, contentH - vh));
			fillRound(rt, D2D1::RectF(w - 9, at, w - 5, at + thumbH), 2, withAlpha(ink, 0.22f));
		}

		// The footer: what a row does, and the way to change them.
		fillRect(rt, D2D1::RectF(0, h - kFooterH, w, h - kFooterH + 1), withAlpha(ink, 0.08f));
		drawText(rt, "Enter, or a second click, does the one chosen. The keys are yours to change.",
		         D2D1::RectF(kMargin, h - kFooterH, w - kMargin - 200, h), 12, withAlpha(ink, 0.55f));
		const float bw = std::max(150.0f, textWidth("Change Shortcuts…", 12.5f) + 28);
		changeButton = D2D1::RectF(w - kMargin - bw, h - kFooterH / 2 - 15, w - kMargin, h - kFooterH / 2 + 15);
		fillRound(rt, changeButton, 8, withAlpha(ink, changeHot ? 0.12f : 0.07f));
		strokeRound(rt, changeButton, 8, withAlpha(ink, 0.10f));
		drawText(rt, "Change Shortcuts…", changeButton, 12.5f, ink, TextAlign::Center);
		surface.end();
		EndPaint(hwnd, &ps);
	}

	int hitAt(float x, float y) const {
		for (const Hit& h : hits) if (inRect(h.rect, x, y)) return h.index;
		return -1;
	}

	bool key(UINT vk) {
		switch (vk) {
		case VK_DOWN: select(selected + 1); return true;
		case VK_UP: select(selected - 1); return true;
		case VK_NEXT: select(selected + 10); return true;
		case VK_PRIOR: select(selected - 10); return true;
		case VK_RETURN: choose(selected); return true;
		case VK_ESCAPE:
			// Escape empties the search first, then closes (as the wx app's).
			if (GetWindowTextLengthW(edit) > 0) SetWindowTextW(edit, L"");
			else done = true;
			return true;
		default: return false;
		}
	}

	static LRESULT CALLBACK editProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
		ShortcutsSheet* s = reinterpret_cast<ShortcutsSheet*>(data);
		if (msg == WM_KEYDOWN) {
			const bool mine = wp == VK_UP || wp == VK_DOWN || wp == VK_PRIOR || wp == VK_NEXT || wp == VK_RETURN || wp == VK_ESCAPE;
			if (mine && s->key((UINT)wp)) return 0;
		}
		if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) return 0;   // no beep
		if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) s->redraw();
		return DefSubclassProc(h, msg, wp, lp);
	}

	static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
		if (msg == WM_NCCREATE) SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
		ShortcutsSheet* s = reinterpret_cast<ShortcutsSheet*>(GetWindowLongPtrW(h, GWLP_USERDATA));
		if (s == nullptr) return DefWindowProcW(h, msg, wp, lp);
		if (msg == WM_NCCREATE) s->hwnd = h;
		LRESULT r = 0;
		bool handled = false;
		guarded("the shortcut list", [&] { handled = s->handle(msg, wp, lp, r); });
		return handled ? r : DefWindowProcW(h, msg, wp, lp);
	}

	bool handle(UINT msg, WPARAM wp, LPARAM lp, LRESULT& r) {
		const float sc = scale();
		const float x = GET_X_LPARAM(lp) / sc, y = GET_Y_LPARAM(lp) / sc;
		switch (msg) {
		case WM_PAINT: paint(); return true;
		case WM_ERASEBKGND: r = 1; return true;
		case WM_SIZE: layout(); return true;
		case WM_GETMINMAXINFO: {
			MINMAXINFO* m = reinterpret_cast<MINMAXINFO*>(lp);
			m->ptMinTrackSize = { scaled(720, dpiOf(hwnd)), scaled(420, dpiOf(hwnd)) };
			return true;
		}
		case WM_CLOSE: done = true; return true;
		case WM_COMMAND:
			if ((HWND)lp == edit && HIWORD(wp) == EN_CHANGE) { refilter(); redraw(); }
			return true;
		case WM_CTLCOLOREDIT: {
			// The field's own colour behind the text.
			static HBRUSH light = CreateSolidBrush(RGB(236, 237, 240)), dark = CreateSolidBrush(RGB(40, 43, 49));
			SetBkColor((HDC)wp, prefs().dark ? RGB(40, 43, 49) : RGB(236, 237, 240));
			SetTextColor((HDC)wp, prefs().dark ? RGB(226, 230, 238) : RGB(30, 33, 40));
			r = (LRESULT)(prefs().dark ? dark : light);
			return true;
		}
		case WM_MOUSEMOVE: {
			TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, hwnd, 0 };
			TrackMouseEvent(&t);
			const int k = hitAt(x, y);
			const bool ch = inRect(changeButton, x, y);
			if (k != hot || ch != changeHot) { hot = k; changeHot = ch; redraw(); }
			return true;
		}
		case WM_MOUSELEAVE: hot = -1; changeHot = false; redraw(); return true;
		case WM_LBUTTONDOWN: {
			if (inRect(changeButton, x, y)) { toSettings = true; done = true; return true; }
			const int k = hitAt(x, y);
			if (k < 0) return true;
			// A second click on the chosen row does it.
			if (k == selected) choose(k);
			else { selected = k; redraw(); }
			return true;
		}
		case WM_LBUTTONDBLCLK: {
			const int k = hitAt(x, y);
			if (k >= 0) choose(k);
			return true;
		}
		case WM_MOUSEWHEEL:
			scroll -= GET_WHEEL_DELTA_WPARAM(wp) * 84.0f / WHEEL_DELTA;
			clampScroll();
			redraw();
			return true;
		case WM_KEYDOWN: return key((UINT)wp);
		}
		return false;
	}
};

}  // namespace

void showShortcutsWindow(HWND parent) {
	CircuitWindow* owner = nullptr;
	for (CircuitWindow* w : circuitWindows()) if (w->window() == parent) owner = w;
	ShortcutsSheet sheet;
	sheet.run(parent);
	if (sheet.toSettings) {
		setPreferencesPage(4);
		showPreferencesDialog(parent);
		return;
	}
	if (sheet.chosen == nullptr || owner == nullptr) return;
	// Done once the list has gone, as the canvas or the menu would do it.
	const shortcuts::Action* a = sheet.chosen->action;
	if (std::strcmp(a->id, "tidy") == 0) {
		if (owner->canEdit()) owner->tidy();
		else owner->lockNudge();
		return;
	}
	owner->run(a->command);
}
