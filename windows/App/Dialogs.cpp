// The app's dialogs and extra windows (see Dialogs.h).

#include "Dialogs.h"
#include "Window.h"
#include "Collections.h"

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
			x.hwnd = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", W(x.value).c_str(),
			                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, ctrlX, y,
			                         ctrlW - (browseW ? browseW + sc(6) : 0), rowH, dialog, id, appInstance(), nullptr);
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
		if (f.kind == FormField::List && f.mono) SendMessageW(f.hwnd, WM_SETFONT, (WPARAM)monoFont(dpi), TRUE);
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

void showGateSettings(CircuitWindow* w, long gate) {
	CLDocument* doc = w->document();
	struct Setting { std::string name, type, value; double min, max; int field; };
	std::vector<Setting> settings;
	Form f;
	f.title = cl_gate_caption(doc, gate);
	f.width = 420;
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
	// Numbers are checked against the library's range, as the wx dialog does.
	f.validate = [&](Form& form) -> std::string {
		for (const Setting& s : settings) {
			if (s.type != "INT" && s.type != "FLOAT") continue;
			const std::string v = trimmed(form.text(s.field));
			char* end = nullptr;
			const double x = strtod(v.c_str(), &end);
			if (v.empty() || end == nullptr || *end != 0 || !std::isfinite(x) || (s.type == "INT" && x != std::floor(x)))
				return strf("%s: enter %s.", s.name.c_str(), s.type == "INT" ? "a whole number" : "a number");
			if (x < s.min || x > s.max)
				return strf("%s must be between %s and %s.", s.name.c_str(), numberText(s.min).c_str(), numberText(s.max).c_str());
		}
		return std::string();
	};
	if (f.run(w->window()) != IDOK) return;
	bool changed = false;
	for (const Setting& s : settings) {
		const FormField& x = f.fields[s.field];
		const std::string v = s.type == "BOOL" ? (x.value.empty() ? "false" : "true") : trimmed(x.value);
		if (v == s.value) continue;
		cl_gate_set_setting(doc, gate, s.name.c_str(), v.c_str());
		changed = true;
	}
	if (changed) w->edited();
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

// ---- Truth tables ----------------------------------------------------------------

void showTruthTable(CircuitWindow* w, int page) {
	char err[512] = "";
	CLTruthTable* tt = cl_truth_table(w->document(), page, err, sizeof err);
	if (tt == nullptr) {
		showMessage(w->window(), Tone::Info, "A truth table couldn't be made for this page",
		            *err ? err : "Add switches (inputs) and lights (outputs) to the page first.");
		return;
	}
	const int cols = cl_tt_columns(tt), rows = cl_tt_rows(tt), inputs = cl_tt_inputs(tt);
	Form f;
	f.title = "Truth Table";
	f.width = std::min(900, std::max(360, 40 + cols * 64));
	f.okText = "Close";
	f.cancelText = "";
	std::string about = strf("%d input%s, %d output%s, %d row%s.", inputs, inputs == 1 ? "" : "s", cols - inputs,
	                         cols - inputs == 1 ? "" : "s", rows, rows == 1 ? "" : "s");
	if (cl_tt_sequential(tt)) about += " This page has clocks or flip-flops, so outputs can depend on what came before.";
	if (cl_tt_unsettled(tt) > 0)
		about += strf(" %d row%s never settled.", cl_tt_unsettled(tt), cl_tt_unsettled(tt) == 1 ? "" : "s");
	FormField info;
	info.kind = FormField::Note;
	info.label = about;
	info.lines = 2;
	f.add(info);
	FormField table;
	table.kind = FormField::List;
	table.mono = true;
	table.lines = std::min(22, std::max(4, rows));
	// Outputs are marked, after a gap, as the other apps set them apart.
	for (int c = 0; c < cols; c++) {
		std::string name = cl_tt_name(tt, c);
		if (c == inputs && c > 0) name = "→ " + name;
		table.choices.push_back(name);
		table.columnWidths.push_back(std::max(52, (int)textWidth(name, 12) + 22));
	}
	const int t = f.add(table);
	FormField legend;
	legend.kind = FormField::Note;
	legend.label = "X unknown · Z floating · ! conflict · - not connected";
	f.add(legend);
	std::vector<std::vector<std::string>> data((size_t)rows);
	for (int r = 0; r < rows; r++)
		for (int c = 0; c < cols; c++) data[r].push_back(std::string(1, cl_tt_cell(tt, r, c)));
	cl_tt_free(tt);
	f.onInit = [&](Form& form) { form.setRows(t, data); };
	f.run(w->window());
}

// ---- Memory (RAM and ROM contents) -----------------------------------------------

void showRamEditor(CircuitWindow* w, long gate) {
	int addressBits = 0, dataBits = 0;
	CLDocument* doc = w->document();
	if (!cl_ram_info(doc, gate, &addressBits, &dataBits)) return;
	const unsigned long words = 1UL << std::min(std::max(addressBits, 0), 20);
	const int aDigits = std::max(1, (addressBits + 3) / 4), dDigits = std::max(1, (dataBits + 3) / 4);
	long lastRead = -2, lastWritten = -2;

	Form f;
	f.title = "Memory";
	f.width = 460;
	f.okText = "Close";
	f.cancelText = "";
	f.buttons = { "Load File…", "Save File…", "Settings…" };
	FormField info;
	info.kind = FormField::Note;
	info.label = strf("%lu addresses × %d bits. Double-click a value to change it (in hex).", words, dataBits);
	f.add(info);
	FormField list;
	list.kind = FormField::List;
	list.mono = true;
	list.lines = 20;
	list.choices = { "Address", "Value (hex)", "Decimal", "" };
	list.columnWidths = { 90, 110, 110, 0 };
	const int l = f.add(list);

	auto row = [&](unsigned long a) -> std::vector<std::string> {
		const unsigned long v = cl_ram_value(doc, gate, a);
		const char* mark = (long)a == lastWritten ? "written last" : (long)a == lastRead ? "read last" : "";
		return { strf("%0*lX", aDigits, a), strf("%0*lX", dDigits, v), strf("%lu", v), mark };
	};
	auto fill = [&](Form& form) {
		lastRead = cl_ram_last_read(doc, gate);
		lastWritten = cl_ram_last_written(doc, gate);
		std::vector<std::vector<std::string>> rows;
		rows.reserve(words);
		for (unsigned long a = 0; a < words; a++) rows.push_back(row(a));
		form.setRows(l, rows);
	};
	f.onInit = fill;
	// The words last read and written change as the circuit runs.
	f.timerMs = 250;
	f.onTimer = [&](Form& form) {
		const long lw = cl_ram_last_written(doc, gate), lr = cl_ram_last_read(doc, gate);
		if (lw == lastWritten && lr == lastRead) return;
		const long before[] = { lastRead, lastWritten };
		lastRead = lr;
		lastWritten = lw;
		for (long a : { before[0], before[1], lr, lw }) {
			if (a < 0 || (unsigned long)a >= words) continue;
			const std::vector<std::string> r = row((unsigned long)a);
			for (int c = 0; c < 4; c++) form.setCell(l, (int)a, c, r[c]);
		}
	};
	f.onActivate = [&](Form& form, int, int r) {
		std::string text = strf("%0*lX", dDigits, cl_ram_value(doc, gate, (unsigned long)r));
		if (!askText(form.dialog, "Change a Value", strf("The value at %0*X, in hex:", aDigits, r), text)) return;
		char* end = nullptr;
		const unsigned long v = strtoul(text.c_str(), &end, 16);
		if (text.empty() || end == nullptr || *end != 0) { MessageBeep(MB_ICONWARNING); return; }
		cl_ram_set(doc, gate, (unsigned long)r, v);
		const std::vector<std::string> cells = row((unsigned long)r);
		for (int c = 0; c < 4; c++) form.setCell(l, r, c, cells[c]);
		w->edited();
	};
	bool openSettings = false;
	f.onButton = [&](Form& form, int b) -> bool {
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
		if (load) { cl_ram_load_file(doc, gate, file.c_str()); fill(form); w->edited(); }
		else cl_ram_save_file(doc, gate, file.c_str());
		return false;
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
	const int accent = f.add(choiceField("Accent", { "Blue", "Purple", "Pink", "Orange", "Green", "Graphite" }, p.accent));
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
		if (field == accent) q.accent = form.choice(accent);
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

// ---- Export style ------------------------------------------------------------------

bool chooseExportStyle(HWND parent, int& style) {
	Form f;
	f.title = "Export as Image";
	f.width = 380;
	f.okText = "Choose File…";
	FormField s = choiceField("Style", { "Black on white, for printing", "Light, with signal colours", "Dark, with signal colours" },
	                          style == CL_STYLE_PRINT ? 0 : style == CL_STYLE_DARK ? 2 : 1);
	const int field = f.add(s);
	FormField note;
	note.kind = FormField::Note;
	note.label = "Saved as a PNG picture, twice the size it shows at on screen.";
	f.add(note);
	if (f.run(parent) != IDOK) return false;
	const int c = atoi(f.fields[field].value.c_str());
	style = c == 0 ? CL_STYLE_PRINT : c == 2 ? CL_STYLE_DARK : CL_STYLE_LIGHT;
	return true;
}

// ---- The oscilloscope --------------------------------------------------------------

namespace {
const wchar_t* kScopeClass = L"CedarLogicScope";
enum { kScopeClear = 1, kScopeIn, kScopeOut };
}  // namespace

void registerDialogClasses() {
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof wc;
	wc.lpfnWndProc = ScopeWindow::proc;
	wc.hInstance = appInstance();
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hIcon = LoadIconW(appInstance(), MAKEINTRESOURCEW(1));
	wc.lpszClassName = kScopeClass;
	RegisterClassExW(&wc);
}

ScopeWindow::ScopeWindow(CircuitWindow* o) : owner(o) {
	const UINT dpi = dpiOf(owner->window());
	hwnd = CreateWindowExW(0, kScopeClass, L"Oscilloscope", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
	                       CW_USEDEFAULT, scaled(820, dpi), scaled(360, dpi), owner->window(), nullptr, appInstance(), this);
	clearButton = CreateWindowExW(0, L"BUTTON", L"Clear", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 10, 10,
	                              hwnd, (HMENU)kScopeClear, appInstance(), nullptr);
	outButton = CreateWindowExW(0, L"BUTTON", L"Zoom Out", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 10, 10,
	                            hwnd, (HMENU)kScopeOut, appInstance(), nullptr);
	inButton = CreateWindowExW(0, L"BUTTON", L"Zoom In", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 10, 10,
	                           hwnd, (HMENU)kScopeIn, appInstance(), nullptr);
	info = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 0, 0, 10, 10, hwnd, nullptr,
	                       appInstance(), nullptr);
	setFontTree(hwnd, uiFont(dpiOf(hwnd)));
	setDarkTitleBar(hwnd, prefs().dark);
	layout();
}

ScopeWindow::~ScopeWindow() {
	if (hwnd) {
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
		DestroyWindow(hwnd);
	}
}

int ScopeWindow::barHeight() const { return scaled(38, dpiOf(hwnd)); }

void ScopeWindow::layout() {
	const UINT dpi = dpiOf(hwnd);
	auto sc = [&](int v) { return scaled(v, dpi); };
	RECT rc;
	GetClientRect(hwnd, &rc);
	int x = sc(6);
	const int y = sc(6), h = sc(26);
	MoveWindow(clearButton, x, y, sc(70), h, TRUE); x += sc(76);
	MoveWindow(outButton, x, y, sc(80), h, TRUE); x += sc(86);
	MoveWindow(inButton, x, y, sc(80), h, TRUE); x += sc(92);
	MoveWindow(info, x, y, std::max<int>(10, rc.right - x - sc(6)), h, TRUE);
	InvalidateRect(hwnd, nullptr, FALSE);
}

void ScopeWindow::present() {
	setDarkTitleBar(hwnd, prefs().dark);
	ShowWindow(hwnd, SW_SHOWNORMAL);
	SetForegroundWindow(hwnd);
	update();
}

void ScopeWindow::close() { ShowWindow(hwnd, SW_HIDE); }
bool ScopeWindow::visible() const { return IsWindowVisible(hwnd) != FALSE; }

void ScopeWindow::update() {
	CLDocument* doc = owner->document();
	const long long len = cl_scope_length(doc);
	setWindowText(info, strf("%d signal%s · %lld steps recorded · %d ms a step", cl_scope_signal_count(doc),
	                         cl_scope_signal_count(doc) == 1 ? "" : "s", len, cl_document_step_ms(doc)));
	RECT rc;
	GetClientRect(hwnd, &rc);
	rc.top = barHeight();
	InvalidateRect(hwnd, &rc, FALSE);
}

LRESULT CALLBACK ScopeWindow::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
	if (msg == WM_NCCREATE) {
		auto* self = static_cast<ScopeWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
		self->hwnd = h;
		SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
	}
	auto* self = reinterpret_cast<ScopeWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
	if (self == nullptr) return DefWindowProcW(h, msg, wp, lp);
	LRESULT r = 0;
	bool ok = false;
	guarded("the oscilloscope", [&] { r = self->handle(msg, wp, lp); ok = true; });
	return ok ? r : DefWindowProcW(h, msg, wp, lp);
}

LRESULT ScopeWindow::handle(UINT msg, WPARAM wp, LPARAM lp) {
	switch (msg) {
	case WM_PAINT:
		paint();
		return 0;
	case WM_ERASEBKGND:
		return 1;
	case WM_SIZE:
		layout();
		return 0;
	case WM_CLOSE:
		ShowWindow(hwnd, SW_HIDE);   // kept for next time
		return 0;
	case WM_DPICHANGED: {
		const RECT* r = reinterpret_cast<const RECT*>(lp);
		SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
		setFontTree(hwnd, uiFont(dpiOf(hwnd)));
		layout();
		return 0;
	}
	case WM_COMMAND:
		switch (LOWORD(wp)) {
		case kScopeClear: cl_scope_clear(owner->document()); update(); break;
		case kScopeIn: zoom = std::min(48, zoom * 2); update(); break;
		case kScopeOut: zoom = std::max(1, zoom / 2); update(); break;
		}
		SetFocus(hwnd);
		return 0;
	case WM_KEYDOWN:
		// Escape or Ctrl+G puts it away; Space runs and pauses the circuit.
		if (wp == VK_ESCAPE || (wp == 'G' && (GetKeyState(VK_CONTROL) & 0x8000))) {
			close();
			SetForegroundWindow(owner->window());
			return 0;
		}
		if (wp == VK_SPACE) { owner->toggleRunning(); return 0; }
		break;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

// One lane per signal: low and high as a line at the bottom or the top,
// floating in the middle, unknown and conflict as shaded blocks. The newest
// step is at the right edge.
void ScopeWindow::paint() {
	PAINTSTRUCT ps;
	BeginPaint(hwnd, &ps);
	ID2D1HwndRenderTarget* rt = surface.begin(hwnd);
	if (rt == nullptr) { EndPaint(hwnd, &ps); return; }
	const double s = surface.scale();
	CLDocument* doc = owner->document();
	RECT rc;
	GetClientRect(hwnd, &rc);
	const float bar = (float)(barHeight() / s);
	const float w = (float)(rc.right / s), h = (float)(rc.bottom / s) - bar;
	const bool dark = prefs().dark;
	const RGBA bg = Palette{ dark, false }.canvas();
	rt->Clear(d2dColor(bg));
	rt->SetTransform(D2D1::Matrix3x2F::Translation(0, bar) * D2D1::Matrix3x2F::Scale((float)s, (float)s));
	const int n = cl_scope_signal_count(doc);
	const float ink = dark ? 0.85f : 0.15f;
	ID2D1SolidColorBrush* brush = nullptr;
	rt->CreateSolidColorBrush(D2D1::ColorF(ink, ink, ink, 1), &brush);
	if (brush == nullptr) { surface.end(); EndPaint(hwnd, &ps); return; }
	if (n == 0) {
		drawText(rt, "Add a TO label to a wire, and its signal shows up here.", D2D1::RectF(0, 0, w, h), 12,
		         D2D1::ColorF(ink, ink, ink, 1), TextAlign::Center);
	} else {
		const float nameW = 130, lane = (float)std::min(34.0, std::max(18.0, (h - 8.0) / n));
		const long long len = cl_scope_length(doc);
		const int visible = std::max(1, (int)((w - nameW - 8) / zoom));
		const long long from = std::max(0LL, len - visible);
		const int count = (int)std::min<long long>(visible, len - from);
		std::vector<unsigned char> buf((size_t)std::max(count, 1));
		const RGBA accent = accentColor(dark);
		for (int sig = 0; sig < n; sig++) {
			const float top = 4 + sig * lane, hi = top + 4, lo = top + lane - 6, mid = (hi + lo) / 2;
			// Lane separator and name.
			brush->SetColor(D2D1::ColorF(ink, ink, ink, 0.12f));
			rt->FillRectangle(D2D1::RectF(0, top + lane - 1, w, top + lane), brush);
			drawText(rt, cl_scope_signal(doc, sig), D2D1::RectF(8, top, nameW - 6, top + lane), 12,
			         D2D1::ColorF(ink, ink, ink, 1));
			if (count <= 0) continue;
			cl_scope_samples(doc, sig, from, count, buf.data());
			float prevY = -1;
			for (int i = 0; i < count; i++) {
				const float x0 = nameW + (float)i * zoom, x1 = x0 + zoom;
				const unsigned char v = buf[i];
				if (v == 0 || v == 1 || v == 2) {
					const float y = v == 1 ? hi : v == 0 ? lo : mid;
					if (v == 2) brush->SetColor(D2D1::ColorF(0.0f, 0.7f, 0.0f, 1));
					else brush->SetColor(d2dColor(accent));
					if (prevY >= 0 && prevY != y) rt->DrawLine(D2D1::Point2F(x0, prevY), D2D1::Point2F(x0, y), brush, 1.5f);
					rt->DrawLine(D2D1::Point2F(x0, y), D2D1::Point2F(x1, y), brush, 1.5f);
					prevY = y;
				} else if (v == 3 || v == 4) {
					// Conflict red, unknown blue, as the canvas colours them.
					brush->SetColor(v == 3 ? D2D1::ColorF(0.9f, 0.2f, 0.2f, 0.5f) : D2D1::ColorF(0.3f, 0.3f, 1.0f, 0.4f));
					rt->FillRectangle(D2D1::RectF(x0, hi, x1, lo), brush);
					prevY = -1;
				} else {
					prevY = -1;
				}
			}
		}
	}
	brush->Release();
	surface.end();
	EndPaint(hwnd, &ps);
}
