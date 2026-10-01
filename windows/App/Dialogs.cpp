// The app's dialogs and extra windows (see Dialogs.h).

#include "Dialogs.h"
#include "Window.h"
#include "Chrome.h"
#include "Collections.h"
#include "Formula.h"

#include <uxtheme.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>

// ---- Form ----------------------------------------------------------------------------

namespace {

const int kFieldBase = 1000;    // a field's controls: kFieldBase + index
const int kBrowseBase = 2000;   // its Choose... button
const int kButtonBase = 100;    // Form::buttons
const int kLabelBase = 3000;    // a check box's label (a click on it ticks the box)

// The dialogs' colours in dark mode (the Mac's dark sheets).
struct DialogColors {
	COLORREF back = RGB(36, 39, 45), text = RGB(228, 232, 240), field = RGB(24, 26, 31);
	HBRUSH backBrush = CreateSolidBrush(RGB(36, 39, 45)), fieldBrush = CreateSolidBrush(RGB(24, 26, 31));
};
const DialogColors& darkColors() {
	static DialogColors c;
	return c;
}
const UINT_PTR kFormTimer = 1;

// A Form's list rows are asked for as they come into view (a virtual list
// view), so a truth table or a memory with thousands of rows opens at once.
struct ListData {
	std::vector<std::vector<std::string>> rows;
	std::wstring scratch;
};
std::map<HWND, ListData>& listData() {
	static std::map<HWND, ListData> m;
	return m;
}

HFONT monoFont(UINT dpi) {
	static std::map<UINT, HFONT> fonts;
	auto it = fonts.find(dpi);
	if (it != fonts.end()) return it->second;
	LOGFONTW lf = {};
	lf.lfHeight = -MulDiv(10, (int)dpi, 72);
	lf.lfWeight = FW_NORMAL;
	wcscpy(lf.lfFaceName, L"Consolas");
	HFONT f = CreateFontIndirectW(&lf);
	fonts[dpi] = f;
	return f;
}

int textPixels(HWND ref, HFONT font, const std::string& text) {
	HDC dc = GetDC(ref);
	HGDIOBJ old = SelectObject(dc, font);
	SIZE sz = {};
	const std::wstring w = W(text);
	GetTextExtentPoint32W(dc, w.c_str(), (int)w.size(), &sz);
	SelectObject(dc, old);
	ReleaseDC(ref, dc);
	return sz.cx;
}

// Up and Down in a search box move the list under it.
LRESULT CALLBACK arrowsProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
	if (msg == WM_KEYDOWN && (wp == VK_UP || wp == VK_DOWN)) {
		HWND list = (HWND)data;
		const int n = (int)SendMessageW(list, LVM_GETITEMCOUNT, 0, 0);
		int at = (int)SendMessageW(list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
		at = std::min(std::max(0, at + (wp == VK_UP ? -1 : 1)), n - 1);
		if (n > 0) {
			LVITEMW it = {};
			it.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
			it.state = LVIS_SELECTED | LVIS_FOCUSED;
			SendMessageW(list, LVM_SETITEMSTATE, at, (LPARAM)&it);
			SendMessageW(list, LVM_ENSUREVISIBLE, at, FALSE);
		}
		return 0;
	}
	return DefSubclassProc(h, msg, wp, lp);
}

// A list's column titles in light text, in dark mode (the dark header theme
// leaves them dark grey).
LRESULT CALLBACK darkHeaderProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
	if (msg == WM_NOTIFY) {
		NMHDR* n = reinterpret_cast<NMHDR*>(lp);
		if (n->code == NM_CUSTOMDRAW && n->hwndFrom == (HWND)SendMessageW(h, LVM_GETHEADER, 0, 0)) {
			NMCUSTOMDRAW* cd = reinterpret_cast<NMCUSTOMDRAW*>(lp);
			if (cd->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
			if (cd->dwDrawStage == CDDS_ITEMPREPAINT) {
				SetTextColor(cd->hdc, RGB(200, 206, 216));
				return CDRF_DODEFAULT;
			}
		}
	}
	return DefSubclassProc(h, msg, wp, lp);
}

// A Picture field, drawn into the control's DC with Direct2D.
void paintPicture(const FormField& x, const DRAWITEMSTRUCT* di) {
	static ID2D1DCRenderTarget* rt = nullptr;
	if (rt == nullptr) {
		const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
			D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96, 96);
		if (FAILED(d2dFactory()->CreateDCRenderTarget(&props, &rt))) return;
	}
	const RECT& r = di->rcItem;
	if (FAILED(rt->BindDC(di->hDC, &r))) return;
	const float s = dpiOf(di->hwndItem) / 96.0f;
	rt->BeginDraw();
	rt->SetTransform(D2D1::Matrix3x2F::Scale(s, s));
	const COLORREF back = prefs().dark ? darkColors().back : GetSysColor(COLOR_BTNFACE);
	rt->Clear(D2D1::ColorF(GetRValue(back) / 255.0f, GetGValue(back) / 255.0f, GetBValue(back) / 255.0f));
	if (x.paint) x.paint(rt, (r.right - r.left) / s, (r.bottom - r.top) / s);
	if (rt->EndDraw() == D2DERR_RECREATE_TARGET) { rt->Release(); rt = nullptr; }
}

INT_PTR CALLBACK formProc(HWND d, UINT msg, WPARAM wp, LPARAM lp) {
	Form* f = reinterpret_cast<Form*>(GetWindowLongPtrW(d, DWLP_USER));
	if (msg == WM_INITDIALOG) {
		f = reinterpret_cast<Form*>(lp);
		SetWindowLongPtrW(d, DWLP_USER, (LONG_PTR)f);
		f->dialog = d;
		guarded("a dialog", [&] {
			f->build();
			if (f->onInit) f->onInit(*f);
		});
		if (f->timerMs > 0) SetTimer(d, kFormTimer, (UINT)f->timerMs, nullptr);
		// The first box to type in, or the first list, has the keyboard.
		for (FormField& x : f->fields) {
			if (x.kind == FormField::Text || x.kind == FormField::List) { SetFocus(x.hwnd); return FALSE; }
		}
		return TRUE;
	}
	if (f == nullptr) return FALSE;
	switch (msg) {
	case WM_COMMAND: {
		const int id = LOWORD(wp), code = HIWORD(wp);
		if (id == IDOK) {
			if (f->okText.empty()) return TRUE;
			std::string bad;
			guarded("a dialog", [&] { if (f->validate) bad = f->validate(*f); });
			if (!bad.empty()) { f->setProblem(bad); MessageBeep(MB_ICONWARNING); return TRUE; }
			f->capture();
			EndDialog(d, IDOK);
			return TRUE;
		}
		if (id == IDCANCEL) {
			// Escape, or the close box: with no Cancel, that's OK.
			f->capture();
			EndDialog(d, f->cancelText.empty() && !f->okText.empty() && !f->validate ? IDOK : IDCANCEL);
			return TRUE;
		}
		if (id >= kButtonBase && id < kButtonBase + (int)f->buttons.size()) {
			bool close = true;
			guarded("a dialog", [&] { close = !f->onButton || f->onButton(*f, id - kButtonBase); });
			if (close) { f->capture(); EndDialog(d, id); }
			return TRUE;
		}
		if (id >= kBrowseBase && id < kBrowseBase + (int)f->fields.size()) {
			FormField& x = f->fields[id - kBrowseBase];
			std::string chosen;
			if (x.browseSave) chosen = chooseSaveFile(d, "Save to File", baseName(f->text(id - kBrowseBase)), { { "All files", "*.*" } }, nullptr);
			else {
				const std::vector<std::string> files = chooseOpenFiles(d, "Choose a File", { { "All files", "*.*" } }, false);
				if (!files.empty()) chosen = files.front();
			}
			if (!chosen.empty()) f->setText(id - kBrowseBase, chosen);
			return TRUE;
		}
		if (id >= kLabelBase && id < kLabelBase + (int)f->fields.size() && code == STN_CLICKED) {
			HWND box = f->fields[id - kLabelBase].hwnd;
			SendMessageW(box, BM_SETCHECK, SendMessageW(box, BM_GETCHECK, 0, 0) == BST_CHECKED ? BST_UNCHECKED : BST_CHECKED, 0);
			SetFocus(box);
			if (f->onChange) guarded("a dialog", [&] { f->onChange(*f, id - kLabelBase); });
			return TRUE;
		}
		if (id >= kFieldBase && id < kFieldBase + (int)f->fields.size() && f->fields[id - kFieldBase].kind == FormField::Picture) {
			if ((code == STN_CLICKED || code == STN_DBLCLK) && f->onClick) {
				HWND pic = f->fields[id - kFieldBase].hwnd;
				POINT p;
				GetCursorPos(&p);
				ScreenToClient(pic, &p);
				const float s = dpiOf(pic) / 96.0f;
				guarded("a dialog", [&] { f->onClick(*f, id - kFieldBase, p.x / s, p.y / s); });
			}
			return TRUE;
		}
		if (id >= kFieldBase && id < kFieldBase + (int)f->fields.size() && f->onChange) {
			const FormField& x = f->fields[id - kFieldBase];
			const bool change = (x.kind == FormField::Text && code == EN_CHANGE) ||
			                    (x.kind == FormField::Choice && code == CBN_SELCHANGE) ||
			                    (x.kind == FormField::Check && code == BN_CLICKED);
			if (change) guarded("a dialog", [&] { f->onChange(*f, id - kFieldBase); });
			return TRUE;
		}
		break;
	}
	case WM_NOTIFY: {
		const NMHDR* n = reinterpret_cast<const NMHDR*>(lp);
		const int field = (int)n->idFrom - kFieldBase;
		if (field < 0 || field >= (int)f->fields.size() || f->fields[field].kind != FormField::List) break;
		if (n->code == LVN_GETDISPINFOW) {
			NMLVDISPINFOW* di = reinterpret_cast<NMLVDISPINFOW*>(lp);
			if (di->item.mask & LVIF_TEXT) {
				ListData& data = listData()[n->hwndFrom];
				const int r = di->item.iItem, c = di->item.iSubItem;
				data.scratch = r >= 0 && r < (int)data.rows.size() && c >= 0 && c < (int)data.rows[r].size()
				                   ? W(data.rows[r][c]) : std::wstring();
				di->item.pszText = const_cast<wchar_t*>(data.scratch.c_str());
			}
			return TRUE;
		}
		if (n->code == LVN_ITEMACTIVATE && f->onActivate) {
			const int row = reinterpret_cast<const NMITEMACTIVATE*>(lp)->iItem;
			if (row >= 0) guarded("a dialog", [&] { f->onActivate(*f, field, row); });
			return TRUE;
		}
		break;
	}
	case WM_DRAWITEM: {
		const DRAWITEMSTRUCT* di = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
		const int field = (int)di->CtlID - kFieldBase;
		if (field < 0 || field >= (int)f->fields.size() || f->fields[field].kind != FormField::Picture) break;
		guarded("a dialog", [&] { paintPicture(f->fields[field], di); });
		return TRUE;
	}
	case WM_MOUSEWHEEL: {
		if (!f->onWheel) break;
		POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
		for (size_t i = 0; i < f->fields.size(); i++) {
			const FormField& x = f->fields[i];
			RECT r;
			if (x.kind != FormField::Picture || !GetWindowRect(x.hwnd, &r) || !PtInRect(&r, p)) continue;
			const int delta = GET_WHEEL_DELTA_WPARAM(wp);
			guarded("a dialog", [&] { f->onWheel(*f, (int)i, delta); });
			SetWindowLongPtrW(d, DWLP_MSGRESULT, 0);
			return TRUE;
		}
		break;
	}
	case WM_CTLCOLORDLG:
	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLORBTN:
		if (prefs().dark) {
			const DialogColors& c = darkColors();
			SetTextColor((HDC)wp, c.text);
			SetBkColor((HDC)wp, c.back);
			return (INT_PTR)c.backBrush;
		}
		break;
	case WM_CTLCOLOREDIT:
	case WM_CTLCOLORLISTBOX:
		if (prefs().dark) {
			const DialogColors& c = darkColors();
			SetTextColor((HDC)wp, c.text);
			SetBkColor((HDC)wp, c.field);
			return (INT_PTR)c.fieldBrush;
		}
		break;
	case WM_TIMER:
		if (wp == kFormTimer && f->onTimer) guarded("a dialog", [&] { f->onTimer(*f); });
		return TRUE;
	case WM_DESTROY:
		KillTimer(d, kFormTimer);
		for (FormField& x : f->fields) if (x.kind == FormField::List) listData().erase(x.hwnd);
		break;
	}
	return FALSE;
}

}  // namespace

void Form::build() {
	const UINT dpi = dpiOf(dialog);
	auto sc = [&](int v) { return scaled(v, dpi); };
	HFONT font = uiFont(dpi);
	const int margin = sc(12), gap = sc(8), rowH = sc(23), checkH = sc(20), lineH = sc(17);
	const int width = sc(this->width);

	int labelW = 0;
	for (const FormField& x : fields)
		if ((x.kind == FormField::Text || x.kind == FormField::Choice) && !x.label.empty())
			labelW = std::max(labelW, textPixels(dialog, font, x.label));
	const int ctrlX = labelW > 0 ? margin + labelW + sc(10) : margin;
	const int ctrlW = width - ctrlX - margin;
	const int fullW = width - 2 * margin;

	int y = margin;
	for (size_t i = 0; i < fields.size(); i++) {
		FormField& x = fields[i];
		const HMENU id = (HMENU)(INT_PTR)(kFieldBase + (int)i);
		if (i > 0) y += gap;
		switch (x.kind) {
		case FormField::Text: {
			if (!x.label.empty())
				x.extra = CreateWindowExW(0, L"STATIC", W(x.label).c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT, margin,
				                          y + sc(3), labelW, lineH, dialog, nullptr, appInstance(), nullptr);
			const int browseW = x.browse ? sc(84) : 0;
			// Several lines: a box to type them in (Enter starts a new one).
			const bool multi = x.lines > 1;
			const int boxH = multi ? x.lines * sc(20) + sc(8) : rowH;
			std::string initial = x.value;
			if (multi) {
				std::string crlf;
				for (char c : initial) { if (c == '\n') crlf += '\r'; crlf += c; }
				initial = crlf;
			}
			x.hwnd = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", W(initial).c_str(),
			                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL |
			                             (multi ? ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_VSCROLL : 0),
			                         ctrlX, y, ctrlW - (browseW ? browseW + sc(6) : 0), boxH, dialog, id, appInstance(), nullptr);
			if (multi) y += boxH - rowH;
			if (x.browse)
				CreateWindowExW(0, L"BUTTON", L"Choose…", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
				                ctrlX + ctrlW - browseW, y, browseW, rowH, dialog, (HMENU)(INT_PTR)(kBrowseBase + (int)i),
				                appInstance(), nullptr);
			y += rowH;
			if (!x.tip.empty()) {
				CreateWindowExW(0, L"STATIC", W(x.tip).c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT, ctrlX, y + sc(2), ctrlW,
				                lineH, dialog, nullptr, appInstance(), nullptr);
				y += lineH + sc(2);
			}
			break;
		}
		case FormField::Check: {
			// The box, and its words as a label of their own beside it (a
			// themed check box draws its words in black, even in dark mode).
			const int box = GetSystemMetricsForDpi(SM_CXMENUCHECK, dpi) + sc(2);
			x.hwnd = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
			                         ctrlX, y, box, checkH, dialog, id, appInstance(), nullptr);
			SendMessageW(x.hwnd, BM_SETCHECK, x.value.empty() ? BST_UNCHECKED : BST_CHECKED, 0);
			x.extra = CreateWindowExW(0, L"STATIC", W(x.label).c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOTIFY | SS_NOPREFIX,
			                          ctrlX + box + sc(4), y + sc(2), ctrlW - box - sc(4), checkH - sc(2), dialog,
			                          (HMENU)(INT_PTR)(kLabelBase + (int)i), appInstance(), nullptr);
			y += checkH;
			break;
		}
		case FormField::Choice: {
			if (!x.label.empty())
				x.extra = CreateWindowExW(0, L"STATIC", W(x.label).c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT, margin,
				                          y + sc(3), labelW, lineH, dialog, nullptr, appInstance(), nullptr);
			x.hwnd = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
			                         ctrlX, y, ctrlW, sc(260), dialog, id, appInstance(), nullptr);
			for (const std::string& c : x.choices) SendMessageW(x.hwnd, CB_ADDSTRING, 0, (LPARAM)W(c).c_str());
			SendMessageW(x.hwnd, CB_SETCURSEL, atoi(x.value.c_str()), 0);
			y += rowH;
			break;
		}
		case FormField::List: {
			const bool headers = !x.choices.empty();
			const int h = sc(26) + x.lines * sc(19);
			x.hwnd = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
			                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS |
			                             LVS_OWNERDATA | (headers ? 0 : LVS_NOCOLUMNHEADER),
			                         margin, y, fullW, h, dialog, id, appInstance(), nullptr);
			SendMessageW(x.hwnd, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
			             LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | (headers ? LVS_EX_GRIDLINES : 0));
			listData()[x.hwnd] = ListData();
			const int cols = std::max<int>(1, (int)x.choices.size());
			int fixed = 0, flexible = 0;
			for (int c = 0; c < cols; c++) {
				const int wpt = c < (int)x.columnWidths.size() ? x.columnWidths[c] : 0;
				if (wpt > 0) fixed += sc(wpt); else flexible++;
			}
			const int room = std::max(sc(40), fullW - fixed - GetSystemMetricsForDpi(SM_CXVSCROLL, dpi) - sc(6));
			for (int c = 0; c < cols; c++) {
				const int wpt = c < (int)x.columnWidths.size() ? x.columnWidths[c] : 0;
				const std::wstring title = headers ? W(x.choices[c]) : std::wstring();
				LVCOLUMNW col = {};
				col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
				col.fmt = LVCFMT_LEFT;
				col.cx = wpt > 0 ? sc(wpt) : room / std::max(1, flexible);
				col.pszText = const_cast<wchar_t*>(title.c_str());
				SendMessageW(x.hwnd, LVM_INSERTCOLUMNW, c, (LPARAM)&col);
			}
			y += h;
			break;
		}
		case FormField::Picture: {
			const int h = sc(x.height);
			x.hwnd = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_OWNERDRAW | SS_NOTIFY, margin, y, fullW, h, dialog, id,
			                         appInstance(), nullptr);
			y += h;
			break;
		}
		case FormField::Note: {
			const int h = x.lines * lineH;
			x.hwnd = CreateWindowExW(0, L"STATIC", W(x.label).c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
			                         margin, y, fullW, h, dialog, id, appInstance(), nullptr);
			y += h;
			break;
		}
		}
	}

	y += gap;
	problem = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, margin, y, fullW, lineH,
	                          dialog, nullptr, appInstance(), nullptr);
	y += lineH + sc(4);

	// The buttons, along the bottom on the right: the extra ones, then OK
	// and Cancel.
	std::vector<std::pair<std::string, int>> all;
	for (size_t i = 0; i < buttons.size(); i++) all.push_back({ buttons[i], kButtonBase + (int)i });
	if (!okText.empty()) all.push_back({ okText, IDOK });
	if (!cancelText.empty()) all.push_back({ cancelText, IDCANCEL });
	const int btnH = sc(26);
	int x = width - margin;
	for (auto it = all.rbegin(); it != all.rend(); ++it) {
		const int bw = std::max(sc(84), textPixels(dialog, font, it->first) + sc(28));
		x -= bw;
		const DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | (it->second == IDOK ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON);
		buttonWindows.push_back(CreateWindowExW(0, L"BUTTON", W(it->first).c_str(), style, x, y, bw, btnH, dialog,
		                                        (HMENU)(INT_PTR)it->second, appInstance(), nullptr));
		x -= sc(8);
	}
	y += btnH + margin;

	setFontTree(dialog, font);
	for (FormField& f : fields) {
		if ((f.kind == FormField::List || f.kind == FormField::Text) && f.mono) SendMessageW(f.hwnd, WM_SETFONT, (WPARAM)monoFont(dpi), TRUE);
		if (f.kind == FormField::Text && f.arrowsMove >= 0 && f.arrowsMove < (int)fields.size())
			SetWindowSubclass(f.hwnd, arrowsProc, 1, (DWORD_PTR)fields[f.arrowsMove].hwnd);
	}
	SetWindowTextW(dialog, W(title).c_str());

	// Dark mode: the title bar and Windows' own dark styles for the controls.
	if (prefs().dark) {
		setDarkTitleBar(dialog, true);
		const DialogColors& c = darkColors();
		for (FormField& f : fields) {
			switch (f.kind) {
			case FormField::Text: darkenControl(f.hwnd, true, L"CFD"); break;
			case FormField::Choice: darkenControl(f.hwnd, true, L"CFD"); break;
			case FormField::Check: darkenControl(f.hwnd, true, L"Explorer"); break;
			case FormField::List:
				darkenControl(f.hwnd, true, L"ItemsView");
				if (HWND header = (HWND)SendMessageW(f.hwnd, LVM_GETHEADER, 0, 0)) darkenControl(header, true, L"ItemsView");
				SetWindowSubclass(f.hwnd, darkHeaderProc, 2, 0);
				SendMessageW(f.hwnd, LVM_SETBKCOLOR, 0, c.field);
				SendMessageW(f.hwnd, LVM_SETTEXTBKCOLOR, 0, c.field);
				SendMessageW(f.hwnd, LVM_SETTEXTCOLOR, 0, c.text);
				break;
			default: break;
			}
		}
		for (HWND b : buttonWindows) darkenControl(b, true, L"Explorer");
		EnumChildWindows(dialog, [](HWND child, LPARAM) -> BOOL {
			wchar_t cls[32] = L"";
			GetClassNameW(child, cls, 32);
			if (lstrcmpiW(cls, L"Button") == 0 && GetWindowTextLengthW(child) > 0) darkenControl(child, true, L"Explorer");
			return TRUE;
		}, 0);
	}

	// Size the window around what's in it, centred on its owner.
	RECT rc = { 0, 0, width, y };
	const DWORD style = (DWORD)GetWindowLongW(dialog, GWL_STYLE), ex = (DWORD)GetWindowLongW(dialog, GWL_EXSTYLE);
	AdjustWindowRectExForDpi(&rc, style, FALSE, ex, dpi);
	const int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
	RECT anchor;
	HWND owner = GetWindow(dialog, GW_OWNER);
	if (owner == nullptr || !GetWindowRect(owner, &anchor)) {
		HMONITOR m = MonitorFromWindow(dialog, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi = { sizeof mi };
		GetMonitorInfoW(m, &mi);
		anchor = mi.rcWork;
	}
	int left = (anchor.left + anchor.right - ww) / 2, top = (anchor.top + anchor.bottom - wh) / 2;
	HMONITOR m = MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi = { sizeof mi };
	if (GetMonitorInfoW(m, &mi)) {
		left = std::max<int>(mi.rcWork.left, std::min<int>(left, mi.rcWork.right - ww));
		top = std::max<int>(mi.rcWork.top, std::min<int>(top, mi.rcWork.bottom - wh));
	}
	SetWindowPos(dialog, nullptr, left, top, ww, wh, SWP_NOZORDER | SWP_NOACTIVATE);
}

void Form::refresh(int field) {
	if (field >= 0 && field < (int)fields.size() && fields[field].hwnd) InvalidateRect(fields[field].hwnd, nullptr, FALSE);
}

void Form::enable(int field, bool on) {
	if (field < 0 || field >= (int)fields.size()) return;
	if (fields[field].hwnd) EnableWindow(fields[field].hwnd, on);
	if (fields[field].extra) EnableWindow(fields[field].extra, on);
}

void Form::capture() {
	for (size_t i = 0; i < fields.size(); i++) {
		FormField& x = fields[i];
		if (x.kind == FormField::Text) x.value = text((int)i);
		else if (x.kind == FormField::Check) x.value = checked((int)i) ? "1" : "";
		else if (x.kind == FormField::Choice) x.value = std::to_string(choice((int)i));
	}
}

int Form::run(HWND owner) {
	// An empty template: the dialog's frame, in the system's font. Its
	// controls are made in WM_INITDIALOG (Form::build).
	std::vector<WORD> t;
	auto dword = [&](DWORD v) { t.push_back(LOWORD(v)); t.push_back(HIWORD(v)); };
	auto str = [&](const wchar_t* s) { for (; *s; s++) t.push_back((WORD)*s); t.push_back(0); };
	dword(WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT);
	dword(WS_EX_DLGMODALFRAME);
	t.push_back(0);                                          // no controls
	t.push_back(0); t.push_back(0); t.push_back(200); t.push_back(100);   // placed and sized in build()
	t.push_back(0);                                          // no menu
	t.push_back(0);                                          // the standard dialog class
	str(L"");                                                // the title comes in build()
	t.push_back(9);                                          // points
	str(L"Segoe UI");
	const INT_PTR r = DialogBoxIndirectParamW(appInstance(), reinterpret_cast<LPCDLGTEMPLATEW>(t.data()), owner, formProc,
	                                          (LPARAM)this);
	dialog = nullptr;
	return (int)r;
}

std::string Form::text(int field) const {
	const FormField& x = fields[field];
	if (dialog == nullptr || x.hwnd == nullptr) return x.value;
	return windowText(x.hwnd);
}

void Form::setText(int field, const std::string& text) {
	FormField& x = fields[field];
	x.value = text;
	if (x.hwnd) SetWindowTextW(x.hwnd, W(text).c_str());
}

bool Form::checked(int field) const {
	const FormField& x = fields[field];
	if (dialog == nullptr || x.hwnd == nullptr) return !x.value.empty();
	return SendMessageW(x.hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

int Form::choice(int field) const {
	const FormField& x = fields[field];
	if (dialog == nullptr || x.hwnd == nullptr) return atoi(x.value.c_str());
	return (int)SendMessageW(x.hwnd, CB_GETCURSEL, 0, 0);
}

void Form::setRows(int field, const std::vector<std::vector<std::string>>& rows) {
	HWND list = fields[field].hwnd;
	if (list == nullptr) return;
	listData()[list].rows = rows;
	SendMessageW(list, LVM_SETITEMCOUNT, rows.size(), LVSICF_NOSCROLL);
	InvalidateRect(list, nullptr, FALSE);
}

void Form::setCell(int field, int row, int column, const std::string& text) {
	HWND list = fields[field].hwnd;
	if (list == nullptr) return;
	ListData& d = listData()[list];
	if (row < 0 || row >= (int)d.rows.size()) return;
	if ((int)d.rows[row].size() <= column) d.rows[row].resize((size_t)column + 1);
	d.rows[row][column] = text;
	SendMessageW(list, LVM_REDRAWITEMS, row, row);
}

int Form::selectedRow(int field) const {
	HWND list = fields[field].hwnd;
	return list ? (int)SendMessageW(list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED) : -1;
}

void Form::selectRow(int field, int row) {
	HWND list = fields[field].hwnd;
	if (list == nullptr) return;
	LVITEMW it = {};
	it.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
	it.state = LVIS_SELECTED | LVIS_FOCUSED;
	SendMessageW(list, LVM_SETITEMSTATE, row, (LPARAM)&it);
	SendMessageW(list, LVM_ENSUREVISIBLE, row, FALSE);
}

void Form::setProblem(const std::string& text) {
	if (problem) SetWindowTextW(problem, W(text).c_str());
}

// ---- A line of text ----------------------------------------------------------------

bool askText(HWND parent, const std::string& title, const std::string& prompt, std::string& value) {
	Form f;
	f.title = title;
	f.width = 360;
	FormField label;
	label.kind = FormField::Note;
	label.label = prompt;
	f.add(label);
	FormField entry;
	entry.kind = FormField::Text;
	entry.value = value;
	const int e = f.add(entry);
	if (f.run(parent) != IDOK) return false;
	std::string v = f.fields[e].value;
	const size_t a = v.find_first_not_of(" \t"), b = v.find_last_not_of(" \t");
	value = a == std::string::npos ? std::string() : v.substr(a, b - a + 1);
	return true;
}

// ---- A gate's settings -----------------------------------------------------------
// The same settings the wx app's parameters dialog lists, each with a control
// that suits its type. OK applies what changed (one undo step each).

namespace {

std::string numberText(double v) {
	if (v == std::floor(v) && std::fabs(v) < 1e15) return strf("%.0f", v);
	return strf("%g", v);
}

std::string trimmed(const std::string& s) {
	const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
	return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

}  // namespace

// A gate's settings (double-click it), as the Mac's inspector: the gate's
// picture and name on top, then its settings, each applied as you make it
// (a number once it's valid) and undone with Ctrl+Z; Rotate, Delete, Done.
void showGateSettings(CircuitWindow* w, long gate) {
	CLDocument* doc = w->document();
	struct Setting { std::string name, type, value; double min, max; int field; };
	std::vector<Setting> settings;
	const std::string libName = cl_gate_library_name(doc, gate) ? cl_gate_library_name(doc, gate) : "";
	const std::string caption = cl_gate_caption(doc, gate);
	Form f;
	f.title = caption;
	f.width = 420;
	f.okText = "Done";
	f.cancelText = "";
	f.buttons = { "Rotate", "Delete" };
	FormField head;
	head.kind = FormField::Picture;
	head.height = 58;
	head.paint = [&](ID2D1RenderTarget* rt, float pw, float) {
		const Chrome c = chrome();
		const D2D1_COLOR_F ink = c.dark ? D2D1::ColorF(0.89f, 0.9f, 0.93f) : D2D1::ColorF(0.12f, 0.13f, 0.16f);
		const D2D1_RECT_F tile = D2D1::RectF(0, 2, 56, 54);
		fillRound(rt, tile, 10, c.dark ? D2D1::ColorF(0.14f, 0.16f, 0.18f) : D2D1::ColorF(1, 1, 1));
		strokeRound(rt, tile, 10, withAlpha(ink, c.dark ? 0.08f : 0.09f));
		if (!libName.empty()) {
			D2D1_MATRIX_3X2_F was;
			rt->GetTransform(&was);
			rt->SetTransform(D2D1::Matrix3x2F::Translation(6, 8) * was);
			cl_library_draw_gate(libName.c_str(), rt, 44, 36, was._11, c.dark);
			rt->SetTransform(was);
		}
		drawText(rt, caption, D2D1::RectF(70, 8, pw, 32), 16, ink, TextAlign::Leading, true);
		drawText(rt, "Changes apply as you make them \u00B7 Ctrl+Z undoes", D2D1::RectF(70, 32, pw, 50), 11, withAlpha(ink, 0.55f));
	};
	f.add(head);
	const int n = cl_gate_setting_count(doc, gate);
	for (int i = 0; i < n; i++) {
		CLGateSetting s;
		if (!cl_gate_setting(doc, gate, i, &s)) continue;
		Setting st{ s.name, s.type, s.value ? s.value : "", s.min, s.max, -1 };
		FormField x;
		const std::string label = s.label ? s.label : s.name;
		if (st.type == "BOOL") {
			x.kind = FormField::Check;
			x.label = label;
			x.value = st.value == "true" ? "1" : "";
		} else {
			x.kind = FormField::Text;
			x.label = label;
			x.value = st.value;
			if ((st.type == "INT" || st.type == "FLOAT") && st.max < 1e30)
				x.tip = strf("%s to %s", numberText(st.min).c_str(), numberText(st.max).c_str());
			if (st.type == "FILE_IN" || st.type == "FILE_OUT") {
				x.browse = true;
				x.browseSave = st.type == "FILE_OUT";
			}
		}
		st.field = f.add(x);
		settings.push_back(st);
	}
	if (settings.empty()) {
		FormField x;
		x.kind = FormField::Note;
		x.label = "This part has no settings.";
		f.add(x);
	}
	// A number is checked against the library's range, as the wx dialog does.
	auto problem = [](const Setting& s, const std::string& v) -> std::string {
		if (s.type != "INT" && s.type != "FLOAT") return "";
		char* end = nullptr;
		const double x = strtod(v.c_str(), &end);
		if (v.empty() || end == nullptr || *end != 0 || !std::isfinite(x) || (s.type == "INT" && x != std::floor(x)))
			return strf("%s: enter %s.", s.name.c_str(), s.type == "INT" ? "a whole number" : "a number");
		if (x < s.min || x > s.max)
			return strf("%s must be between %s and %s.", s.name.c_str(), numberText(s.min).c_str(), numberText(s.max).c_str());
		return "";
	};
	// Each change as it's made, when it's valid.
	f.onChange = [&](Form& form, int field) {
		for (Setting& s : settings) {
			if (s.field != field) continue;
			const std::string v = s.type == "BOOL" ? (form.checked(field) ? "true" : "false") : trimmed(form.text(field));
			const std::string bad = problem(s, v);
			form.setProblem(bad);
			if (!bad.empty() || v == s.value) return;
			cl_gate_set_setting(doc, gate, s.name.c_str(), v.c_str());
			s.value = v;
			w->edited();
		}
	};
	f.validate = [&](Form& form) -> std::string {
		for (const Setting& s : settings) {
			const std::string bad = problem(s, trimmed(form.text(s.field)));
			if (!bad.empty()) return bad;
		}
		return std::string();
	};
	f.onButton = [&](Form& form, int button) {
		if (button == 0) {
			w->run(CMD_ROTATE);   // through the menus' checks (a locked circuit says so)
			form.refresh(0);
			return false;
		}
		w->run(CMD_DELETE);
		return true;
	};
	f.run(w->window());
}

// ---- Quick add (A) -------------------------------------------------------------
// Type part of a gate's name; Return puts the first (or chosen) match on the
// pointer.

namespace {

// How well a gate matches what's typed (lower is better; -1 not at all):
// the whole name, then the start of it, then the start of a word, then
// anywhere.
int matchRank(const std::string& caption, const std::string& name, const std::string& text) {
	if (text.empty()) return 0;
	const std::string c = lowerCase(caption), n = lowerCase(name);
	if (c == text || n == text) return 0;
	if (c.compare(0, text.size(), text) == 0) return 1;
	for (size_t at = c.find(text); at != std::string::npos; at = c.find(text, at + 1))
		if (at > 0 && !isalnum((unsigned char)c[at - 1])) return 2;
	if (c.find(text) != std::string::npos || n.find(text) != std::string::npos) return 3;
	return -1;
}

}  // namespace

void showQuickAdd(CircuitWindow* w) {
	std::vector<std::pair<std::string, std::string>> all;   // name, caption
	for (int c = 0; c < cl_library_category_count(); c++)
		for (int i = 0; i < cl_library_gate_count(c); i++) {
			std::string name = cl_library_gate(c, i);
			std::string caption = cl_library_gate_caption(name.c_str());
			all.push_back({ name, caption.empty() ? name : caption });
		}
	std::sort(all.begin(), all.end(), [](auto& a, auto& b) { return lowerCase(a.second) < lowerCase(b.second); });
	all.erase(std::unique(all.begin(), all.end()), all.end());
	// My Parts too, after the gates.
	for (const parts::Part& part : parts::all()) all.push_back({ part.gate(), part.name + "  (My Parts)" });

	std::vector<std::string> names;   // what the list shows, in order
	Form f;
	f.title = "Add a Gate";
	f.width = 380;
	f.okText = "Add";
	FormField entry;
	entry.kind = FormField::Text;
	entry.tip = "Type part of a gate's name. Up and Down choose; Enter adds it.";
	const int e = f.add(entry);
	FormField list;
	list.kind = FormField::List;
	list.lines = 14;
	const int l = f.add(list);
	f.fields[e].arrowsMove = l;
	auto filter = [&](Form& form) {
		const std::string text = lowerCase(trimmed(form.text(e)));
		std::vector<std::pair<int, size_t>> hits;
		for (size_t i = 0; i < all.size(); i++) {
			const int r = matchRank(all[i].second, all[i].first, text);
			if (r >= 0) hits.push_back({ r, i });
		}
		std::stable_sort(hits.begin(), hits.end(), [](auto& a, auto& b) { return a.first < b.first; });
		std::vector<std::vector<std::string>> rows;
		names.clear();
		for (auto& h : hits) {
			rows.push_back({ all[h.second].second });
			names.push_back(all[h.second].first);
			if (rows.size() >= 300) break;
		}
		form.setRows(l, rows);
		if (!rows.empty()) form.selectRow(l, 0);
	};
	f.onInit = filter;
	f.onChange = [&](Form& form, int field) { if (field == e) filter(form); };
	f.onActivate = [&](Form& form, int, int) { SendMessageW(form.dialog, WM_COMMAND, IDOK, 0); };
	std::string chosen;
	f.validate = [&](Form& form) -> std::string {
		const int row = form.selectedRow(l);
		chosen = row >= 0 && row < (int)names.size() ? names[row] : std::string();
		return chosen.empty() ? "Nothing matches that." : std::string();
	};
	// As in wx, it appears on the pointer at the next move over the canvas.
	if (f.run(w->window()) == IDOK && !chosen.empty()) w->addGateOnNextMove(chosen);
}

// ---- Truth tables: TruthTableWindow.cpp ----

// ---- Memory (RAM and ROM contents) -----------------------------------------------

namespace {

// Fixed-width text for the memory grid.
void monoText(ID2D1RenderTarget* rt, const std::string& text, const D2D1_RECT_F& box, float size, const D2D1_COLOR_F& color,
              DWRITE_TEXT_ALIGNMENT align) {
	static std::map<std::pair<int, int>, IDWriteTextFormat*> formats;
	IDWriteTextFormat*& f = formats[{ (int)(size * 10), (int)align }];
	if (f == nullptr && dwFactory()) {
		dwFactory()->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
		                              size, L"", &f);
		if (f) {
			f->SetTextAlignment(align);
			f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
		}
	}
	ID2D1SolidColorBrush* b = nullptr;
	if (f == nullptr || FAILED(rt->CreateSolidColorBrush(color, &b))) return;
	const std::wstring w = W(text);
	rt->DrawText(w.c_str(), (UINT32)w.size(), f, box, b);
	b->Release();
}

// A value typed into a word: Enter keeps it, Escape doesn't, clicking away keeps it.
struct WordEdit { std::function<void(bool keep)> done; };
LRESULT CALLBACK wordEditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
	WordEdit* e = reinterpret_cast<WordEdit*>(data);
	if (msg == WM_GETDLGCODE) return DLGC_WANTALLKEYS;
	if (msg == WM_KEYDOWN && (wp == VK_RETURN || wp == VK_ESCAPE)) { e->done(wp == VK_RETURN); return 0; }
	if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) return 0;
	if (msg == WM_KILLFOCUS) { LRESULT r = DefSubclassProc(h, msg, wp, lp); e->done(true); return r; }
	return DefSubclassProc(h, msg, wp, lp);
}

}  // namespace

// A RAM's or ROM's contents, as the Mac's: sixteen words a row with the
// address down the side, in hex or decimal. The word last read glows green
// and the one last written amber, as it runs. Click a word to change it;
// jump to an address; load or save a .cdm memory file.
void showRamEditor(CircuitWindow* w, long gate) {
	int addressBits = 0, dataBits = 0;
	CLDocument* doc = w->document();
	if (!cl_ram_info(doc, gate, &addressBits, &dataBits)) return;
	const unsigned long words = 1UL << std::min(std::max(addressBits, 0), 20);
	const int cols = (int)std::min<unsigned long>(16, words);
	const unsigned long rowsTotal = std::max(1UL, words / 16);
	const int aDigits = std::max(1, (std::max(addressBits, 4) + 3) / 4), dDigits = std::max(1, (dataBits + 3) / 4);
	const int decDigits = (int)std::ceil(dataBits * 0.30103) + 1;
	bool decimal = false;
	long scrollRow = 0;
	long editing = -1;
	HWND editBox = nullptr;
	const std::string libName = cl_gate_library_name(doc, gate) ? cl_gate_library_name(doc, gate) : "";
	const std::string caption = cl_gate_caption(doc, gate);

	const float addrW = 70, headH = 24, rowH = 25;
	const float cellW = std::max(3, std::max(dDigits, decDigits) + 1) * 7.0f + 10;
	const float gridW = 12 + addrW + cols * (cellW + 3) + 10;
	const int gridHeight = 380;
	const int visibleRows = (int)((gridHeight - headH - 8) / rowH);

	auto ink = [] { return prefs().dark ? D2D1::ColorF(0.886f, 0.902f, 0.933f) : D2D1::ColorF(0.118f, 0.13f, 0.157f); };
	auto readColor = [] { return prefs().dark ? D2D1::ColorF(0.22f, 0.96f, 0.44f) : D2D1::ColorF(0.05f, 0.68f, 0.27f); };
	auto writtenColor = [] { return prefs().dark ? D2D1::ColorF(1, 0.72f, 0.25f) : D2D1::ColorF(0.93f, 0.55f, 0.05f); };
	auto shown = [&](unsigned long v) { return decimal ? strf("%lu", v) : strf("%0*lX", dDigits, v); };

	Form f;
	f.title = caption;
	f.width = std::max(460, (int)std::ceil(gridW) + 24);
	f.okText = "Done";
	f.cancelText = "";
	f.buttons = { "Load File…", "Save File…", "Settings…" };
	f.timerMs = 200;

	// The part's picture and name, as in its settings, and Hex | Decimal.
	D2D1_RECT_F segs[2] = {};
	FormField head;
	head.kind = FormField::Picture;
	head.height = 58;
	head.paint = [&](ID2D1RenderTarget* rt, float pw, float) {
		const Chrome c = chrome();
		const D2D1_COLOR_F k = ink();
		const D2D1_RECT_F tile = D2D1::RectF(0, 2, 56, 54);
		fillRound(rt, tile, 10, c.dark ? D2D1::ColorF(0.14f, 0.16f, 0.18f) : D2D1::ColorF(1, 1, 1));
		strokeRound(rt, tile, 10, withAlpha(k, 0.09f));
		if (!libName.empty()) {
			D2D1_MATRIX_3X2_F was;
			rt->GetTransform(&was);
			rt->SetTransform(D2D1::Matrix3x2F::Translation(6, 8) * was);
			cl_library_draw_gate(libName.c_str(), rt, 44, 36, was._11, c.dark);
			rt->SetTransform(was);
		}
		drawText(rt, caption, D2D1::RectF(70, 8, pw - 170, 32), 16, k, TextAlign::Leading, true);
		drawText(rt, strf("%lu addresses × %d bits · click a value to change it", words, dataBits), D2D1::RectF(70, 32, pw - 170, 50), 11,
		         withAlpha(k, 0.55f));
		const char* names[] = { "Hex", "Decimal" };
		float x = pw - 2 - (textWidth("Hex", 12, true) + 24) - (textWidth("Decimal", 12, true) + 24) - 4;
		fillRound(rt, D2D1::RectF(x, 16, pw - 2, 44), 14, withAlpha(k, 0.07f));
		x += 2;
		for (int i = 0; i < 2; i++) {
			const float sw = textWidth(names[i], 12, true) + 24;
			segs[i] = D2D1::RectF(x, 18, x + sw, 42);
			const bool on = (i == 1) == decimal;
			if (on) fillRound(rt, segs[i], 12, c.accent());
			drawText(rt, names[i], D2D1::RectF(segs[i].left, segs[i].top + 4, segs[i].right, segs[i].bottom), 12,
			         on ? c.onAccent() : withAlpha(k, 0.75f), TextAlign::Center, on);
			x += sw;
		}
	};
	const int headField = f.add(head);

	FormField jump;
	jump.kind = FormField::Text;
	jump.label = "Go to address";
	jump.mono = true;
	jump.tip = "In hex. The legend: green was read last, amber written last.";
	const int jumpField = f.add(jump);

	FormField grid;
	grid.kind = FormField::Picture;
	grid.height = gridHeight;
	auto cellRect = [&](unsigned long addr) {
		const long r = (long)(addr / 16) - scrollRow;
		const int c = (int)(addr % 16);
		const float x = 12 + addrW + c * (cellW + 3), y = headH + 4 + r * rowH;
		return D2D1::RectF(x, y, x + cellW, y + rowH - 3);
	};
	grid.paint = [&](ID2D1RenderTarget* rt, float pw, float ph) {
		const Chrome c = chrome();
		const D2D1_COLOR_F k = ink();
		const D2D1_RECT_F card = D2D1::RectF(0.5f, 0.5f, pw - 0.5f, ph - 0.5f);
		fillRound(rt, card, 12, c.dark ? D2D1::ColorF(0.141f, 0.157f, 0.184f) : D2D1::ColorF(1, 1, 1));
		strokeRound(rt, card, 12, withAlpha(k, c.dark ? 0.08f : 0.09f));
		for (int col = 0; col < cols; col++) {
			const float x = 12 + addrW + col * (cellW + 3);
			monoText(rt, strf("%X", col), D2D1::RectF(x, 4, x + cellW, headH), 10.5f, withAlpha(k, 0.45f), DWRITE_TEXT_ALIGNMENT_CENTER);
		}
		const long read = cl_ram_last_read(doc, gate), written = cl_ram_last_written(doc, gate);
		rt->PushAxisAlignedClip(D2D1::RectF(0, headH, pw, ph - 4), D2D1_ANTIALIAS_MODE_ALIASED);
		for (int r = 0; r <= visibleRows && scrollRow + r < (long)rowsTotal; r++) {
			const unsigned long row = (unsigned long)(scrollRow + r);
			const float y = headH + 4 + r * rowH;
			monoText(rt, strf("0x%0*lX", aDigits, row * 16), D2D1::RectF(12, y, 12 + addrW, y + rowH - 3), 11, withAlpha(k, 0.45f),
			         DWRITE_TEXT_ALIGNMENT_LEADING);
			for (int col = 0; col < cols; col++) {
				const unsigned long addr = row * 16 + col;
				if (addr >= words) break;
				const D2D1_RECT_F cell = cellRect(addr);
				const unsigned long v = cl_ram_value(doc, gate, addr);
				const D2D1_COLOR_F back = (long)addr == read ? withAlpha(readColor(), 0.35f)
				                        : (long)addr == written ? withAlpha(writtenColor(), 0.35f) : withAlpha(k, 0.045f);
				fillRound(rt, cell, 5, back);
				if ((long)addr == editing) continue;
				monoText(rt, shown(v), cell, 11.5f, v == 0 ? withAlpha(k, 0.35f) : k, DWRITE_TEXT_ALIGNMENT_CENTER);
			}
		}
		rt->PopAxisAlignedClip();
		// Where the list is, when there's more than fits.
		if ((long)rowsTotal > visibleRows) {
			const float track = ph - headH - 12, thumb = std::max(24.0f, track * visibleRows / rowsTotal);
			const float ty = headH + 4 + (track - thumb) * scrollRow / std::max(1L, (long)rowsTotal - visibleRows);
			fillRound(rt, D2D1::RectF(pw - 8, ty, pw - 4, ty + thumb), 2, withAlpha(k, 0.25f));
		}
	};
	const int gridField = f.add(grid);

	auto scrollTo = [&](Form& form, long row) {
		scrollRow = std::max(0L, std::min(row, (long)rowsTotal - visibleRows));
		form.refresh(gridField);
	};
	WordEdit we;
	std::vector<HWND> finished;
	auto finishEdit = [&](Form& form, bool keep) {
		if (editing < 0 || editBox == nullptr) return;
		const long addr = editing;
		editing = -1;
		HWND box = editBox;
		editBox = nullptr;
		if (keep) {
			const std::string t = trimmed(windowText(box));
			char* end = nullptr;
			const unsigned long v = strtoul(t.c_str(), &end, decimal ? 10 : 16);
			if (!t.empty() && end && *end == 0) {
				cl_ram_set(doc, gate, (unsigned long)addr, v);
				w->edited();
			} else if (!t.empty()) {
				MessageBeep(MB_ICONWARNING);
			}
		}
		// Hidden now, destroyed on the next tick: this can run inside the
		// box's own messages (Enter, or losing the keyboard).
		ShowWindow(box, SW_HIDE);
		finished.push_back(box);
		form.refresh(gridField);
	};
	f.onClick = [&](Form& form, int field, float x, float y) {
		if (field == headField) {
			for (int i = 0; i < 2; i++)
				if (inRect(segs[i], x, y)) { finishEdit(form, true); decimal = i == 1; form.refresh(headField); form.refresh(gridField); }
			return;
		}
		if (field != gridField) return;
		finishEdit(form, true);
		for (int r = 0; r <= visibleRows; r++) {
			for (int col = 0; col < cols; col++) {
				const unsigned long addr = (unsigned long)(scrollRow + r) * 16 + col;
				if (addr >= words || !inRect(cellRect(addr), x, y)) continue;
				// A box over the word to type in.
				const D2D1_RECT_F cell = cellRect(addr);
				HWND pic = form.fields[gridField].hwnd;
				const float s = dpiOf(pic) / 96.0f;
				POINT p = { (LONG)(cell.left * s), (LONG)(cell.top * s) };
				MapWindowPoints(pic, form.dialog, &p, 1);
				editing = (long)addr;
				editBox = CreateWindowExW(0, L"EDIT", W(shown(cl_ram_value(doc, gate, addr))).c_str(),
				                          WS_CHILD | WS_VISIBLE | WS_BORDER | ES_CENTER | ES_AUTOHSCROLL, p.x, p.y, (int)((cell.right - cell.left) * s),
				                          (int)((cell.bottom - cell.top) * s), form.dialog, nullptr, appInstance(), nullptr);
				SendMessageW(editBox, WM_SETFONT, (WPARAM)monoFont(dpiOf(pic)), TRUE);
				if (prefs().dark) darkenControl(editBox, true, L"CFD");
				SetWindowPos(editBox, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
				we.done = [&form, &finishEdit](bool keep) { finishEdit(form, keep); };
				SetWindowSubclass(editBox, wordEditProc, 3, (DWORD_PTR)&we);
				SetFocus(editBox);
				SendMessageW(editBox, EM_SETSEL, 0, -1);
				form.refresh(gridField);
				return;
			}
		}
	};
	f.onWheel = [&](Form& form, int field, int delta) {
		if (field != gridField) return;
		finishEdit(form, true);
		scrollTo(form, scrollRow - delta / WHEEL_DELTA * 3);
	};
	f.onChange = [&](Form& form, int field) {
		if (field != jumpField) return;
		const std::string t = trimmed(form.text(jumpField));
		char* end = nullptr;
		const unsigned long a = strtoul(t.c_str(), &end, 16);
		if (!t.empty() && end && *end == 0 && a < words) { finishEdit(form, true); scrollTo(form, (long)(a / 16)); }
	};
	// The words last read and written change as the circuit runs.
	long seenRead = -2, seenWritten = -2;
	f.onTimer = [&](Form& form) {
		for (HWND h : finished) DestroyWindow(h);
		finished.clear();
		const long r = cl_ram_last_read(doc, gate), wr = cl_ram_last_written(doc, gate);
		if (r == seenRead && wr == seenWritten) return;
		seenRead = r;
		seenWritten = wr;
		form.refresh(gridField);
	};
	bool openSettings = false;
	f.onButton = [&](Form& form, int b) -> bool {
		finishEdit(form, true);
		if (b == 2) { openSettings = true; return true; }
		const bool load = b == 0;
		const std::vector<FileFilter> filters = { { "Memory files (*.cdm)", "*.cdm" }, { "All files", "*.*" } };
		std::string file;
		if (load) {
			const std::vector<std::string> files = chooseOpenFiles(form.dialog, "Load Memory", filters, false);
			if (!files.empty()) file = files.front();
		} else {
			file = chooseSaveFile(form.dialog, "Save Memory", "memory.cdm", filters, ".cdm");
		}
		if (file.empty()) return false;
		if (load) { cl_ram_load_file(doc, gate, file.c_str()); form.refresh(gridField); w->edited(); }
		else cl_ram_save_file(doc, gate, file.c_str());
		return false;
	};
	f.validate = [&](Form& form) -> std::string {
		finishEdit(form, true);
		return std::string();
	};
	f.run(w->window());
	if (openSettings) showGateSettings(w, gate);
}

// ---- Preferences ---------------------------------------------------------------

namespace {

void prefsApply() {
	prefs().applyWireDots();
	prefs().save();
	for (CircuitWindow* w : circuitWindows()) w->prefsChanged();
}

FormField choiceField(const char* label, std::vector<std::string> items, int active) {
	FormField x;
	x.kind = FormField::Choice;
	x.label = label;
	x.choices = std::move(items);
	x.value = std::to_string(active);
	return x;
}

FormField checkField(const char* label, bool on) {
	FormField x;
	x.kind = FormField::Check;
	x.label = label;
	x.value = on ? "1" : "";
	return x;
}

FormField heading(const char* text) {
	FormField x;
	x.kind = FormField::Note;
	x.label = text;
	return x;
}

}  // namespace

void showPreferencesDialog(HWND parent) {
	Prefs& p = prefs();
	Form f;
	f.title = "Preferences";
	f.width = 440;
	f.okText = "Close";
	f.cancelText = "";
	FormField who;
	who.kind = FormField::Text;
	who.label = "Your name";
	who.value = p.studentName;
	who.tip = "On the Lab Page template and exported pictures.";
	const int name = f.add(who);
	f.add(heading("Appearance"));
	const int theme = f.add(choiceField("Theme", { "Match Windows", "Light", "Dark", "As I left it" }, p.themeMode));
	// The icon's green first, as the Mac app offers them (the engine's index 6).
	static const int kAccentOrder[] = { 6, 0, 1, 2, 3, 4, 5 };
	int accentAt = 0;
	for (int i = 0; i < 7; i++) if (kAccentOrder[i] == p.accent) accentAt = i;
	const int accent = f.add(choiceField("Accent", { "CedarLogic green", "Blue", "Purple", "Pink", "Orange", "Green", "Graphite" }, accentAt));
	const int grid = f.add(choiceField("Grid", { "Lines", "Dots" }, p.gridStyle));
	const int showGrid = f.add(checkField("Show the grid", p.showGrid));
	const int major = f.add(checkField("Every fifth line darker", p.majorGrid));
	const int wires = f.add(choiceField("Wires", { "Thin", "Normal", "Thick" }, p.wireThickness));
	const int dots = f.add(checkField("Dots at every bend (off: only where wires join)", p.wireDots));
	const int low = f.add(choiceField("Low wires when dark", { "Silver", "Slate blue", "Soft white", "Classic grey" }, p.lowWire));
	const int names = f.add(checkField("Names under the palette's gates", p.showGateNames));
	f.add(heading("Canvas"));
	const int wheel = f.add(choiceField("Mouse wheel", { "Zooms", "Moves around" }, p.mouseWheel));
	const int touchpad = f.add(choiceField("Touchpad scrolling", { "Zooms", "Moves around" }, p.touchpadScroll));
	const int reverse = f.add(checkField("Reverse the wheel's zoom", p.reverseWheel));
	const int rightRotate = f.add(checkField("Right-click a gate to rotate it", p.rightClickRotate));
	const int dupClip = f.add(checkField("Duplicate (D) also copies to the clipboard", p.duplicateUsesClipboard));
	const int tidy = f.add(choiceField("Tidy Up (Shift+S)", { "Keeps the layout's shape", "Arranges by signal flow" }, p.tidyMode));
	const int askQuit = f.add(checkField("Ask before quitting with Ctrl+Q", p.confirmQuit));

	// Every change applies at once.
	f.onChange = [&](Form& form, int field) {
		Prefs& q = prefs();
		if (field == name) {
			q.studentName = form.text(name);
			q.save();
			return;
		}
		if (field == theme) {
			q.themeMode = form.choice(theme);
			if (q.themeMode == 1) q.dark = false;
			else if (q.themeMode == 2) q.dark = true;
			else if (q.themeMode == 0) q.dark = systemPrefersDark();
			q.save();
			applyTheme();
			return;
		}
		if (field == accent) q.accent = kAccentOrder[std::max(0, std::min(6, form.choice(accent)))];
		else if (field == grid) q.gridStyle = form.choice(grid);
		else if (field == showGrid) q.showGrid = form.checked(showGrid);
		else if (field == major) q.majorGrid = form.checked(major);
		else if (field == wires) q.wireThickness = form.choice(wires);
		else if (field == dots) q.wireDots = form.checked(dots);
		else if (field == low) q.lowWire = form.choice(low);
		else if (field == names) q.showGateNames = form.checked(names);
		else if (field == wheel) q.mouseWheel = form.choice(wheel);
		else if (field == touchpad) q.touchpadScroll = form.choice(touchpad);
		else if (field == reverse) q.reverseWheel = form.checked(reverse);
		else if (field == rightRotate) q.rightClickRotate = form.checked(rightRotate);
		else if (field == dupClip) q.duplicateUsesClipboard = form.checked(dupClip);
		else if (field == tidy) q.tidyMode = form.choice(tidy);
		else if (field == askQuit) q.confirmQuit = form.checked(askQuit);
		prefsApply();
	};
	f.run(parent);
}

// ---- Every shortcut ----------------------------------------------------------------

void showShortcutsWindow(HWND parent) {
	struct Key { const char* keys; const char* what; };
	struct Group { const char* title; std::vector<Key> keys; };
	const std::vector<Group> groups = {
		{ "Circuits", { { "Ctrl+N", "New circuit" }, { "Ctrl+O", "Open" }, { "Ctrl+S", "Save" },
		                { "Ctrl+Shift+S", "Save as" }, { "Ctrl+E", "Export as an image" },
		                { "Ctrl+P", "Print" }, { "Ctrl+Q", "Quit" } } },
		{ "Editing", { { "Ctrl+Z", "Undo" }, { "Ctrl+Y or Ctrl+Shift+Z", "Redo" }, { "Ctrl+X", "Cut" },
		               { "Ctrl+C", "Copy" }, { "Ctrl+V", "Paste (it follows the pointer)" },
		               { "Ctrl+D", "Duplicate" }, { "Ctrl+A", "Select all" }, { "Delete", "Delete" },
		               { "Escape", "Let go, or drop the selection" } } },
		{ "Building (on the canvas)", { { "A", "Add a gate by name" }, { "R", "Rotate" }, { "S", "Straighten wires" },
		               { "Shift+S", "Tidy up (preview first)" }, { "C", "Copy; while moving, connect nearby pins" },
		               { "V", "Paste" }, { "X", "Cut" }, { "D", "Duplicate" }, { "Arrow keys", "Nudge the selection" },
		               { "Shift+1 … Shift+0", "Palette category 1 … 10" } } },
		{ "Moving around", { { "Ctrl+=", "Zoom in" }, { "Ctrl+-", "Zoom out" }, { "Ctrl+0", "Zoom to fit" },
		               { "Space", "Tap: zoom to fit. Hold and drag: move around" }, { "Ctrl+1", "Actual size" },
		               { "Ctrl+.", "Show or hide the palette" }, { "Middle button drag", "Move around" } } },
		{ "Simulation", { { "Ctrl+R", "Simulation View" }, { "Ctrl+Shift+R", "Step once" }, { "T", "Truth table" },
		               { "Ctrl+G", "Oscilloscope" } } },
		{ "Tabs", { { "Ctrl+T", "New tab" }, { "Ctrl+W", "Close tab" }, { "Ctrl+Shift+T", "Reopen the tab you closed" },
		            { "Ctrl+Tab", "Next tab" }, { "Ctrl+Shift+Tab", "Previous tab" } } },
		{ "App", { { "?", "Every shortcut (this list)" }, { "Ctrl+Shift+D", "Dark mode" },
		           { "Ctrl+,", "Preferences" }, { "F1", "Help" } } },
	};
	std::vector<std::vector<std::string>> rows;
	for (const Group& g : groups) {
		if (!rows.empty()) rows.push_back({ "", "" });
		rows.push_back({ std::string(g.title), "" });
		for (const Key& k : g.keys) rows.push_back({ std::string("    ") + k.keys, k.what });
	}
	Form f;
	f.title = "Keyboard Shortcuts";
	f.width = 520;
	f.okText = "Close";
	f.cancelText = "";
	FormField list;
	list.kind = FormField::List;
	list.lines = 24;
	list.choices = { "Keys", "What it does" };
	list.columnWidths = { 190, 0 };
	const int l = f.add(list);
	f.onInit = [&](Form& form) { form.setRows(l, rows); };
	f.run(parent);
}

// ---- Build from Formula ---------------------------------------------------------------
// Type a formula (or a list of minterms), see what it means as you type, and
// build it as switches, gates and lights -- as written or simplified, with
// any gates or only NAND or only NOR (BuildFormulaView.swift).

void showBuildFormula(CircuitWindow* w) {
	Prefs& pr = prefs();
	Form f;
	f.title = "Build from Formula";
	f.width = 560;
	f.okText = "Build";
	FormField intro;
	intro.kind = FormField::Note;
	intro.label = "Switches for the variables, gates for the formula, and a light for each output, labelled and wired.";
	f.add(intro);
	FormField text;
	text.kind = FormField::Text;
	text.lines = 3;
	text.mono = true;
	text.value = pr.lastFormula;
	const int t = f.add(text);
	FormField help;
	help.kind = FormField::Note;
	help.lines = 2;
	help.label = "One output per line. NOT: A' or ~A \u00B7 AND: AB, A\u00B7B or A*B \u00B7 OR: A + B \u00B7 XOR: A ^ B "
	             "\u00B7 or minterms: F(A,B,C) = m(1,3,5) + d(7)";
	f.add(help);
	FormField preview;
	preview.kind = FormField::Note;
	preview.lines = 5;
	const int pv = f.add(preview);
	const int shape = f.add(choiceField("Build it", { "As written", "Simplest sum of products", "Simplest product of sums" }, pr.buildShape));
	const int style = f.add(choiceField("With", { "Any gates", "NAND only", "NOR only" }, pr.buildStyle));
	const int two = f.add(checkField("Only 2-input gates", pr.buildTwoInput));
	const int where = f.add(choiceField("Put it", { "On a new page", "Beside this page's circuit" }, pr.buildNewPage ? 0 : 1));

	formula::Parsed parsed;
	auto update = [&](Form& form) {
		std::string error;
		const std::string src = form.text(t);
		bool blank = true;
		for (char c : src) if (!isspace((unsigned char)c)) blank = false;
		if (blank) { form.setText(pv, ""); return; }
		if (!formula::parse(src, parsed, error)) { form.setText(pv, "\u26A0  " + error); return; }
		std::string s = parsed.variables.empty() ? "No variables" : strf("%d variable%s: ", (int)parsed.variables.size(),
		                                                                   parsed.variables.size() == 1 ? "" : "s");
		for (size_t i = 0; i < parsed.variables.size(); i++) s += (i ? ", " : "") + parsed.variables[i];
		for (const formula::Function& fn : parsed.functions)
			s += "\nSimplest:  " + fn.name + " = " + formula::simplest(true, (int)parsed.variables.size(), fn.values).text(parsed.variables);
		const formula::Plan plan = formula::plan(parsed, (formula::Shape)form.choice(shape), (formula::Style)form.choice(style),
		                                         form.checked(two));
		s += "\n" + plan.summary();
		form.setText(pv, s);
	};
	f.onInit = update;
	f.onChange = [&](Form& form, int) { update(form); };
	f.validate = [&](Form& form) -> std::string {
		std::string error;
		if (!formula::parse(form.text(t), parsed, error)) return error;
		return std::string();
	};
	if (f.run(w->window()) != IDOK) return;
	pr.lastFormula = f.fields[t].value;
	pr.buildShape = atoi(f.fields[shape].value.c_str());
	pr.buildStyle = atoi(f.fields[style].value.c_str());
	pr.buildTwoInput = !f.fields[two].value.empty();
	pr.buildNewPage = atoi(f.fields[where].value.c_str()) == 0;
	pr.save();
	const formula::Plan plan = formula::plan(parsed, (formula::Shape)pr.buildShape, (formula::Style)pr.buildStyle, pr.buildTwoInput);
	std::string name;
	for (size_t i = 0; i < parsed.functions.size(); i++) name += (i ? ", " : "") + parsed.functions[i].name;
	if (!w->buildPlan(plan, pr.buildNewPage, name)) MessageBeep(MB_ICONWARNING);
}

// ---- Quitting ------------------------------------------------------------------------

// Ctrl+Q sits beside Ctrl+W (close the tab): asked first, as the Mac app
// asks about Cmd+Q, unless "Always Quit" was chosen.
bool confirmQuit(HWND parent) {
	if (!prefs().confirmQuit) return true;
	Form f;
	f.title = "Quit CedarLogic";
	f.width = 400;
	f.okText = "Quit";
	f.buttons = { "Always Quit" };
	FormField q;
	q.kind = FormField::Note;
	q.label = "Are you sure you want to quit CedarLogic?";
	f.add(q);
	FormField note;
	note.kind = FormField::Note;
	note.label = "Your circuits are saved; they'll be here when you come back.";
	f.add(note);
	f.onButton = [](Form&, int) {
		prefs().confirmQuit = false;
		prefs().save();
		return true;
	};
	const int r = f.run(parent);
	return r == IDOK || r == 100;
}

// ---- The oscilloscope: Scope.cpp ----
