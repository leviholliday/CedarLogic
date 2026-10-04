// The app's dialogs and extra windows (see Dialogs.h).

#include "Dialogs.h"
#include "Window.h"
#include "Chrome.h"
#include "Brand.h"
#include "Collections.h"
#include "Formula.h"

#include <uxtheme.h>

#include <algorithm>
#include <cerrno>
#include <climits>
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
const int kTabsId = 4000;       // Form::pages' row
const UINT kShowFocus = WM_APP + 21;   // a scrolling form: bring the focused control into view

// The dialogs' look, as the Mac's sheets: paper, filled rounded fields,
// soft buttons with the default one in the accent, toggles for yes/no.
struct DialogColors {
	COLORREF back, text, dim, field, line;
	HBRUSH backBrush, fieldBrush;
};
const DialogColors& darkColors() {
	static DialogColors dark = { RGB(28, 31, 37), RGB(226, 230, 238), RGB(150, 156, 168), RGB(41, 45, 53), RGB(58, 63, 72),
	                             CreateSolidBrush(RGB(28, 31, 37)), CreateSolidBrush(RGB(41, 45, 53)) };
	static DialogColors light = { RGB(250, 250, 252), RGB(30, 33, 40), RGB(112, 117, 126), RGB(239, 240, 243), RGB(214, 216, 221),
	                              CreateSolidBrush(RGB(250, 250, 252)), CreateSolidBrush(RGB(239, 240, 243)) };
	return prefs().dark ? dark : light;
}
D2D1_COLOR_F d2d(COLORREF c, float a = 1) { return D2D1::ColorF(GetRValue(c) / 255.0f, GetGValue(c) / 255.0f, GetBValue(c) / 255.0f, a); }

// Draws with Direct2D, in points, into part of a DC.
void drawOnDC(HDC dc, const RECT& r, UINT dpi, const std::function<void(ID2D1RenderTarget*, float, float)>& draw) {
	static ID2D1DCRenderTarget* rt = nullptr;
	if (rt == nullptr) {
		const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
			D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96, 96);
		if (FAILED(d2dFactory()->CreateDCRenderTarget(&props, &rt))) return;
	}
	if (FAILED(rt->BindDC(dc, &r))) return;
	const float s = dpi / 96.0f;
	rt->BeginDraw();
	rt->SetTransform(D2D1::Matrix3x2F::Scale(s, s));
	draw(rt, (r.right - r.left) / s, (r.bottom - r.top) / s);
	if (rt->EndDraw() == D2DERR_RECREATE_TARGET) { rt->Release(); rt = nullptr; }
}

// A toggle's state (owner-drawn buttons keep none of their own).
bool toggleOn(HWND h) { return GetPropW(h, L"clOn") != nullptr; }
void setToggle(HWND h, bool on) {
	if (on) SetPropW(h, L"clOn", (HANDLE)1); else RemovePropW(h, L"clOn");
	InvalidateRect(h, nullptr, FALSE);
}

// The pointer over a button, for its hover look.
LRESULT CALLBACK hoverProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
	if (msg == WM_MOUSEMOVE && !GetPropW(h, L"clHot")) {
		SetPropW(h, L"clHot", (HANDLE)1);
		TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 };
		TrackMouseEvent(&t);
		InvalidateRect(h, nullptr, FALSE);
	} else if (msg == WM_MOUSELEAVE) {
		RemovePropW(h, L"clHot");
		InvalidateRect(h, nullptr, FALSE);
	} else if (msg == WM_NCDESTROY) {
		RemovePropW(h, L"clHot");
		RemovePropW(h, L"clOn");
	}
	return DefSubclassProc(h, msg, wp, lp);
}

// A button: the default one in the accent with its key, Delete in red,
// the rest soft; a check box as a toggle.
void drawButtonItem(const DRAWITEMSTRUCT* di, bool isToggle) {
	const DialogColors& c = darkColors();
	const bool hot = GetPropW(di->hwndItem, L"clHot") != nullptr, down = (di->itemState & ODS_SELECTED) != 0;
	const bool focus = (di->itemState & ODS_FOCUS) && !(di->itemState & ODS_NOFOCUSRECT);
	const bool disabled = (di->itemState & ODS_DISABLED) != 0;
	const std::string label = windowText(di->hwndItem);
	const Chrome ch = chrome();
	drawOnDC(di->hDC, di->rcItem, dpiOf(di->hwndItem), [&](ID2D1RenderTarget* rt, float w, float h) {
		rt->Clear(d2d(c.back));
		if (isToggle) {
			if (disabled) rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), 0.4f),
			                            nullptr);
			const bool on = toggleOn(di->hwndItem);
			const float tw = 40, th = 20, x = 1, y = (h - th) / 2;
			const D2D1_RECT_F r = D2D1::RectF(x, y, x + tw, y + th);
			if (on) {
				fillRound(rt, r, th / 2, hot ? withAlpha(ch.accent(), 0.88f) : ch.accent());
				fillCircle(rt, D2D1::Point2F(r.right - th / 2, y + th / 2), down ? 7.0f : 6.0f, ch.onAccent());
			} else {
				fillRound(rt, r, th / 2, d2d(c.text, hot ? 0.06f : 0.0f));
				strokeRound(rt, D2D1::RectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f), th / 2 - 0.5f, d2d(c.text, 0.55f));
				fillCircle(rt, D2D1::Point2F(r.left + th / 2, y + th / 2), down ? 6.0f : 5.0f, d2d(c.text, 0.7f));
			}
			if (focus) strokeRound(rt, D2D1::RectF(r.left - 2, r.top - 2, r.right + 2, r.bottom + 2), th / 2 + 2, withAlpha(ch.accent(), 0.6f), 1.5f);
			if (disabled) rt->PopLayer();
			return;
		}
		const bool primary = di->CtlID == IDOK;
		const bool danger = label == "Delete" || label == "Delete…";
		const D2D1_RECT_F r = D2D1::RectF(1, 1, w - 1, h - 1);
		D2D1_COLOR_F fill, ink;
		if (primary) {
			fill = withAlpha(ch.accent(), down ? 0.8f : hot ? 0.92f : 1.0f);
			ink = ch.onAccent();
		} else if (danger) {
			fill = D2D1::ColorF(229 / 255.0f, 72 / 255.0f, 77 / 255.0f, down ? 0.26f : hot ? 0.2f : 0.13f);
			ink = D2D1::ColorF(229 / 255.0f, 72 / 255.0f, 77 / 255.0f);
		} else {
			fill = d2d(c.text, down ? 0.16f : hot ? 0.11f : 0.07f);
			ink = d2d(c.text);
		}
		if (disabled) { fill = withAlpha(fill, fill.a * 0.5f); ink = withAlpha(ink, 0.45f); }
		fillRound(rt, r, 8, fill);
		if (focus) strokeRound(rt, D2D1::RectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f), 7.5f,
		                       primary ? withAlpha(ch.onAccent(), 0.5f) : withAlpha(ch.accent(), 0.7f), 1.5f);
		// The default button carries its key, as on the Mac.
		const float tw = textWidth(label, 13, primary);
		const float chip = primary ? 22 : 0, total = tw + (chip ? chip + 7 : 0), x0 = (w - total) / 2;
		drawText(rt, label, D2D1::RectF(x0, (h - 18) / 2, x0 + tw + 2, (h + 18) / 2), 13, ink, TextAlign::Leading, primary);
		if (chip) {
			const D2D1_RECT_F k = D2D1::RectF(x0 + tw + 7, h / 2 - 8.5f, x0 + tw + 7 + chip, h / 2 + 8.5f);
			fillRound(rt, k, 4, withAlpha(ink, 0.16f));
			drawText(rt, "\u21B5", D2D1::RectF(k.left, k.top + 0.5f, k.right, k.bottom), 10.5f, ink, TextAlign::Center, true);
		}
	});
}

// A drop-down choice, closed: its value on a field with a chevron.
LRESULT CALLBACK choiceProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
	if (msg == WM_PAINT) {
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(h, &ps);
		RECT rc;
		GetClientRect(h, &rc);
		const DialogColors& c = darkColors();
		const Chrome ch = chrome();
		const int sel = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
		std::string value;
		if (sel >= 0) {
			const int n = (int)SendMessageW(h, CB_GETLBTEXTLEN, sel, 0);
			std::wstring wv((size_t)std::max(0, n) + 1, L'\0');
			SendMessageW(h, CB_GETLBTEXT, sel, (LPARAM)wv.data());
			wv.resize(wcslen(wv.c_str()));
			char out[1024] = "";
			WideCharToMultiByte(CP_UTF8, 0, wv.c_str(), -1, out, sizeof out, nullptr, nullptr);
			value = out;
		}
		const bool focus = GetFocus() == h || SendMessageW(h, CB_GETDROPPEDSTATE, 0, 0);
		const bool hot = GetPropW(h, L"clHot") != nullptr, enabled = IsWindowEnabled(h) != FALSE;
		drawOnDC(dc, rc, dpiOf(h), [&](ID2D1RenderTarget* rt, float w, float hh) {
			rt->Clear(d2d(c.back));
			const D2D1_RECT_F r = D2D1::RectF(0.5f, 0.5f, w - 0.5f, hh - 0.5f);
			fillRound(rt, r, 7, d2d(c.field));
			if (hot && enabled) fillRound(rt, r, 7, d2d(c.text, 0.04f));
			strokeRound(rt, r, 7, focus ? ch.accent() : d2d(c.line), focus ? 1.5f : 1);
			drawText(rt, value, D2D1::RectF(10, (hh - 18) / 2, w - 30, (hh + 18) / 2), 13, d2d(c.text, enabled ? 1 : 0.45f));
			drawIcon(rt, Icon::ChevronDown, D2D1::RectF(w - 28, 0, w - 6, hh), 9, d2d(c.text, enabled ? 0.6f : 0.3f));
		});
		EndPaint(h, &ps);
		return 0;
	}
	if (msg == WM_ERASEBKGND) return 1;
	if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS || msg == WM_ENABLE) InvalidateRect(h, nullptr, FALSE);
	if (msg == WM_MOUSEMOVE && !GetPropW(h, L"clHot")) {
		SetPropW(h, L"clHot", (HANDLE)1);
		TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 };
		TrackMouseEvent(&t);
		InvalidateRect(h, nullptr, FALSE);
	} else if (msg == WM_MOUSELEAVE) {
		RemovePropW(h, L"clHot");
		InvalidateRect(h, nullptr, FALSE);
	}
	LRESULT r = DefSubclassProc(h, msg, wp, lp);
	if (msg == CB_SETCURSEL) InvalidateRect(h, nullptr, FALSE);
	return r;
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
	const COLORREF back = darkColors().back;
	rt->Clear(D2D1::ColorF(GetRValue(back) / 255.0f, GetGValue(back) / 255.0f, GetBValue(back) / 255.0f));
	if (x.paint) x.paint(rt, (r.right - r.left) / s, (r.bottom - r.top) / s);
	if (rt->EndDraw() == D2DERR_RECREATE_TARGET) { rt->Release(); rt = nullptr; }
}

// Up, Down and the Page keys in a text box, offered to Form::onKey.
LRESULT CALLBACK formKeysProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
	if (msg == WM_KEYDOWN && (wp == VK_UP || wp == VK_DOWN || wp == VK_PRIOR || wp == VK_NEXT)) {
		Form* f = reinterpret_cast<Form*>(data);
		bool used = false;
		if (f->onKey) guarded("a dialog", [&] { used = f->onKey(*f, (int)(id - 100), (UINT)wp); });
		if (used) return 0;
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
		if (id == kTabsId && (code == STN_CLICKED || code == STN_DBLCLK)) {
			HWND tabs = GetDlgItem(d, kTabsId);
			POINT p;
			GetCursorPos(&p);
			ScreenToClient(tabs, &p);
			const float x = p.x / (dpiOf(tabs) / 96.0f);
			for (size_t i = 0; i < f->tabEdges.size(); i++)
				if (x < f->tabEdges[i]) { f->showPage((int)i); break; }
			return TRUE;
		}
		if (id >= kLabelBase && id < kLabelBase + (int)f->fields.size() && code == STN_CLICKED) {
			HWND box = f->fields[id - kLabelBase].hwnd;
			if (!IsWindowEnabled(box)) return TRUE;
			setToggle(box, !toggleOn(box));
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
		if (id >= kFieldBase && id < kFieldBase + (int)f->fields.size() && f->fields[id - kFieldBase].kind == FormField::Check &&
		    (code == BN_CLICKED || code == BN_DOUBLECLICKED)) {
			setToggle(f->fields[id - kFieldBase].hwnd, !toggleOn(f->fields[id - kFieldBase].hwnd));
			if (f->onChange) guarded("a dialog", [&] { f->onChange(*f, id - kFieldBase); });
			return TRUE;
		}
		if (id >= kFieldBase && id < kFieldBase + (int)f->fields.size() && (code == EN_SETFOCUS || code == EN_KILLFOCUS)) {
			const FormField& x = f->fields[id - kFieldBase];
			RECT fr = x.frame;
			InflateRect(&fr, 3, 3);
			OffsetRect(&fr, 0, -f->scrollY);
			InvalidateRect(d, &fr, FALSE);
			if (code == EN_SETFOCUS && f->scrollHeight > 0) PostMessageW(d, kShowFocus, 0, 0);
			if (code == EN_KILLFOCUS && x.kind == FormField::Text && f->onLeave) guarded("a dialog", [&] { f->onLeave(*f, id - kFieldBase); });
			return TRUE;
		}
		if (id >= kFieldBase && id < kFieldBase + (int)f->fields.size() && code == CBN_SETFOCUS && f->scrollHeight > 0)
			PostMessageW(d, kShowFocus, 0, 0);
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
		if (n->code == NM_SETFOCUS && f->scrollHeight > 0) PostMessageW(d, kShowFocus, 0, 0);
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
		if (di->CtlID == kTabsId) {
			const DialogColors& c = darkColors();
			const Chrome ch = chrome();
			drawOnDC(di->hDC, di->rcItem, dpiOf(di->hwndItem), [&](ID2D1RenderTarget* rt, float w, float h) {
				rt->Clear(d2d(c.back));
				fillRound(rt, D2D1::RectF(0, 0, w, h), 9, d2d(c.text, 0.06f));
				f->tabEdges.clear();
				const float seg = (w - 6) / f->pages.size();
				for (size_t i = 0; i < f->pages.size(); i++) {
					const D2D1_RECT_F r = D2D1::RectF(3 + i * seg, 3, 3 + (i + 1) * seg, h - 3);
					const bool on = (int)i == f->page;
					if (on) {
						fillRound(rt, D2D1::RectF(r.left, r.top + 1, r.right, r.bottom + 1), 7, D2D1::ColorF(0, 0, 0, prefs().dark ? 0.25f : 0.06f));
						fillRound(rt, r, 7, prefs().dark ? d2d(c.text, 0.14f) : D2D1::ColorF(1, 1, 1));
					}
					drawText(rt, f->pages[i], D2D1::RectF(r.left, (h - 18) / 2, r.right, (h + 18) / 2), 12.5f,
					         on ? d2d(c.text) : d2d(c.text, 0.65f), TextAlign::Center, on);
					f->tabEdges.push_back(r.right);
				}
				(void)ch;
			});
			return TRUE;
		}
		if (di->CtlType == ODT_BUTTON) {
			// Tabbed to (a scrolling form shows it).
			if ((di->itemAction & ODA_FOCUS) && (di->itemState & ODS_FOCUS) && f->scrollHeight > 0) PostMessageW(d, kShowFocus, 0, 0);
			const int fi = (int)di->CtlID - kFieldBase;
			const bool toggle = fi >= 0 && fi < (int)f->fields.size() && f->fields[fi].kind == FormField::Check;
			guarded("a dialog", [&] { drawButtonItem(di, toggle); });
			return TRUE;
		}
		const int field = (int)di->CtlID - kFieldBase;
		if (field < 0 || field >= (int)f->fields.size() || f->fields[field].kind != FormField::Picture) break;
		guarded("a dialog", [&] { paintPicture(f->fields[field], di); });
		return TRUE;
	}
	case WM_MOUSEWHEEL: {
		POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
		const int delta = GET_WHEEL_DELTA_WPARAM(wp);
		for (size_t i = 0; i < f->fields.size() && f->onWheel; i++) {
			const FormField& x = f->fields[i];
			RECT r;
			if (x.kind != FormField::Picture || !GetWindowRect(x.hwnd, &r) || !PtInRect(&r, p)) continue;
			guarded("a dialog", [&] { f->onWheel(*f, (int)i, delta); });
			SetWindowLongPtrW(d, DWLP_MSGRESULT, 0);
			return TRUE;
		}
		if (f->scrollHeight > 0) {
			// Three rows a notch (a touchpad's small steps move it a little).
			f->scrollTo(f->scrollY - MulDiv(delta, scaled(90, dpiOf(d)), WHEEL_DELTA));
			SetWindowLongPtrW(d, DWLP_MSGRESULT, 0);
			return TRUE;
		}
		break;
	}
	case WM_VSCROLL: {
		if (f->scrollHeight <= 0) break;
		SCROLLINFO si = { sizeof si, SIF_ALL };
		GetScrollInfo(d, SB_VERT, &si);
		const int line = scaled(40, dpiOf(d));
		int to = f->scrollY;
		switch (LOWORD(wp)) {
		case SB_LINEUP: to -= line; break;
		case SB_LINEDOWN: to += line; break;
		case SB_PAGEUP: to -= std::max(line, (int)si.nPage - line); break;
		case SB_PAGEDOWN: to += std::max(line, (int)si.nPage - line); break;
		case SB_THUMBTRACK:
		case SB_THUMBPOSITION: to = si.nTrackPos; break;
		case SB_TOP: to = 0; break;
		case SB_BOTTOM: to = f->scrollHeight; break;
		default: return TRUE;
		}
		f->scrollTo(to);
		return TRUE;
	}
	case kShowFocus:
		f->showFocus();
		return TRUE;
	case WM_CTLCOLORDLG:
	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLORBTN: {
		const DialogColors& c = darkColors();
		// Tips under fields dimmed; what's wrong in red.
		HWND ctl = (HWND)lp;
		COLORREF ink = c.text;
		if (ctl == f->problem) ink = RGB(220, 64, 64);
		else if (GetPropW(ctl, L"clTip") || GetPropW(ctl, L"clDim")) ink = c.dim;
		if (msg == WM_CTLCOLORSTATIC && !IsWindowEnabled(ctl)) ink = c.dim;
		// A disabled or read-only box asks here too: it keeps its field colour.
		wchar_t cls[16] = L"";
		GetClassNameW(ctl, cls, 16);
		if (lstrcmpiW(cls, L"Edit") == 0) {
			SetTextColor((HDC)wp, ink);
			SetBkColor((HDC)wp, c.field);
			return (INT_PTR)c.fieldBrush;
		}
		SetTextColor((HDC)wp, ink);
		SetBkColor((HDC)wp, c.back);
		return (INT_PTR)c.backBrush;
	}
	case WM_CTLCOLOREDIT:
	case WM_CTLCOLORLISTBOX: {
		const DialogColors& c = darkColors();
		SetTextColor((HDC)wp, c.text);
		SetBkColor((HDC)wp, c.field);
		return (INT_PTR)c.fieldBrush;
	}
	case WM_PAINT: {
		// The paper, and a rounded field around each box and list.
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(d, &ps);
		RECT rc;
		GetClientRect(d, &rc);
		const DialogColors& c = darkColors();
		const Chrome ch = chrome();
		HWND focus = GetFocus();
		drawOnDC(dc, rc, dpiOf(d), [&](ID2D1RenderTarget* rt, float, float) {
			rt->Clear(d2d(c.back));
			const float s = dpiOf(d) / 96.0f;
			for (const FormField& x : f->fields) {
				if (x.frame.right <= x.frame.left || (!f->pages.empty() && x.page != f->page)) continue;
				const float top = (float)(x.frame.top - f->scrollY), bottom = (float)(x.frame.bottom - f->scrollY);
				const D2D1_RECT_F r = D2D1::RectF(x.frame.left / s + 0.5f, top / s + 0.5f, x.frame.right / s - 0.5f, bottom / s - 0.5f);
				const bool on = focus == x.hwnd;
				const bool enabled = IsWindowEnabled(x.hwnd) != FALSE;
				fillRound(rt, r, x.kind == FormField::List ? 6.0f : 7.0f, d2d(c.field));
				strokeRound(rt, r, x.kind == FormField::List ? 6.0f : 7.0f, on ? ch.accent() : d2d(c.line, enabled ? 1 : 0.5f), on ? 1.5f : 1);
			}
		});
		EndPaint(d, &ps);
		return TRUE;
	}
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
	const int margin = sc(16), gap = sc(10), rowH = sc(30), checkH = sc(24), lineH = sc(17);
	const int width = sc(this->width);

	int labelW = 0;
	for (const FormField& x : fields)
		if ((x.kind == FormField::Text || x.kind == FormField::Choice) && !x.label.empty())
			labelW = std::max(labelW, textPixels(dialog, font, x.label));
	const int ctrlX = labelW > 0 ? margin + labelW + sc(10) : margin;
	const int ctrlW = width - ctrlX - margin;
	const int fullW = width - 2 * margin;

	int y = margin;
	std::vector<int> pageY;
	if (!pages.empty()) {
		CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW | SS_NOTIFY, margin, y, fullW, sc(34), dialog,
		                (HMENU)(INT_PTR)kTabsId, appInstance(), nullptr);
		y += sc(34) + sc(16);
		pageY.assign(pages.size(), y);
	}
	const int firstY = y;
	for (size_t i = 0; i < fields.size(); i++) {
		FormField& x = fields[i];
		const HMENU id = (HMENU)(INT_PTR)(kFieldBase + (int)i);
		if (!pages.empty()) y = pageY[std::min<size_t>(x.page, pages.size() - 1)];
		if (y != firstY) y += gap;
		switch (x.kind) {
		case FormField::Text: {
			if (!x.label.empty())
				x.extra = CreateWindowExW(0, L"STATIC", W(x.label).c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT, margin,
				                          y + (rowH - lineH) / 2, labelW, lineH, dialog, nullptr, appInstance(), nullptr);
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
			// The box inside a drawn field (painted with the dialog).
			const int fieldW = ctrlW - (browseW ? browseW + sc(6) : 0);
			x.frame = RECT{ ctrlX, y, ctrlX + fieldW, y + boxH };
			const int padX = sc(9), padY = multi ? sc(6) : (boxH - sc(18)) / 2;
			x.hwnd = CreateWindowExW(0, L"EDIT", W(initial).c_str(),
			                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL |
			                             (multi ? ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_VSCROLL : 0),
			                         ctrlX + padX, y + padY, fieldW - 2 * padX + (multi ? sc(5) : 0), boxH - 2 * padY, dialog, id, appInstance(), nullptr);
			if (multi) y += boxH - rowH;
			if (x.browse)
			{
				HWND choose = CreateWindowExW(0, L"BUTTON", L"Choose…", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, ctrlX + ctrlW - browseW,
				                              y, browseW, rowH, dialog, (HMENU)(INT_PTR)(kBrowseBase + (int)i), appInstance(), nullptr);
				SetWindowSubclass(choose, hoverProc, 4, 0);
				x.others.push_back(choose);
			}
			y += rowH;
			if (!x.tip.empty()) {
				HWND tip = CreateWindowExW(0, L"STATIC", W(x.tip).c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT, ctrlX, y + sc(4), ctrlW,
				                           lineH, dialog, nullptr, appInstance(), nullptr);
				SetPropW(tip, L"clTip", (HANDLE)1);
				x.others.push_back(tip);
				y += lineH + sc(4);
			}
			break;
		}
		case FormField::Check: {
			// A toggle (as Windows 11 and the Mac both show yes/no), and its
			// words as a label beside it; a click on either flips it.
			const int box = sc(44);
			x.hwnd = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
			                         ctrlX, y, box, checkH, dialog, id, appInstance(), nullptr);
			if (!x.value.empty()) SetPropW(x.hwnd, L"clOn", (HANDLE)1);
			SetWindowSubclass(x.hwnd, hoverProc, 4, 0);
			const int labelRoom = ctrlW - box - sc(6);
			const bool twoLines = textPixels(dialog, font, x.label) > labelRoom;
			x.extra = CreateWindowExW(0, L"STATIC", W(x.label).c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOTIFY | SS_NOPREFIX,
			                          ctrlX + box + sc(6), y + sc(3), labelRoom, (twoLines ? lineH * 2 : checkH - sc(3)), dialog,
			                          (HMENU)(INT_PTR)(kLabelBase + (int)i), appInstance(), nullptr);
			if (twoLines) y += lineH + sc(2) - (checkH - lineH);
			y += checkH;
			break;
		}
		case FormField::Choice: {
			if (!x.label.empty())
				x.extra = CreateWindowExW(0, L"STATIC", W(x.label).c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT, margin,
				                          y + (rowH - lineH) / 2, labelW, lineH, dialog, nullptr, appInstance(), nullptr);
			x.hwnd = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
			                         ctrlX, y, ctrlW, sc(260), dialog, id, appInstance(), nullptr);
			for (const std::string& c : x.choices) SendMessageW(x.hwnd, CB_ADDSTRING, 0, (LPARAM)W(c).c_str());
			SendMessageW(x.hwnd, CB_SETCURSEL, atoi(x.value.c_str()), 0);
			// As tall as a field, and drawn as one.
			SendMessageW(x.hwnd, CB_SETITEMHEIGHT, (WPARAM)-1, rowH - sc(6));
			SetWindowSubclass(x.hwnd, choiceProc, 5, 0);
			y += rowH;
			break;
		}
		case FormField::List: {
			const bool headers = !x.choices.empty();
			const int h = sc(26) + x.lines * sc(19);
			x.frame = RECT{ margin, y, margin + fullW, y + h };
			x.hwnd = CreateWindowExW(0, WC_LISTVIEWW, L"",
			                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS |
			                             LVS_OWNERDATA | (headers ? 0 : LVS_NOCOLUMNHEADER),
			                         margin + sc(2), y + sc(2), fullW - sc(4), h - sc(4), dialog, id, appInstance(), nullptr);
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
		if (!pages.empty()) pageY[std::min<size_t>(x.page, pages.size() - 1)] = y;
	}
	if (!pages.empty()) {
		pageBottoms = pageY;
		y = *std::max_element(pageY.begin(), pageY.end());
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
	const int btnH = sc(34);
	int x = width - margin;
	for (auto it = all.rbegin(); it != all.rend(); ++it) {
		// Room for the label (bold on the default button, with its key).
		const int bw = std::max(sc(88), textPixels(dialog, font, it->first) + sc(it->second == IDOK ? 64 : 34));
		x -= bw;
		HWND b = CreateWindowExW(0, L"BUTTON", W(it->first).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, x, y, bw, btnH, dialog,
		                         (HMENU)(INT_PTR)it->second, appInstance(), nullptr);
		SetWindowSubclass(b, hoverProc, 4, 0);
		buttonWindows.push_back(b);
		x -= sc(8);
	}
	y += btnH + margin;
	footerGap = gap;
	footerButtonsAt = lineH + sc(4);
	footerBelow = btnH + margin;

	setFontTree(dialog, font);
	for (FormField& f : fields) {
		if ((f.kind == FormField::List || f.kind == FormField::Text) && f.mono) SendMessageW(f.hwnd, WM_SETFONT, (WPARAM)monoFont(dpi), TRUE);
		if (f.kind == FormField::Text && f.arrowsMove >= 0 && f.arrowsMove < (int)fields.size())
			SetWindowSubclass(f.hwnd, arrowsProc, 1, (DWORD_PTR)fields[f.arrowsMove].hwnd);
		if (f.kind == FormField::Text && onKey && f.lines <= 1)
			SetWindowSubclass(f.hwnd, formKeysProc, 100 + (UINT_PTR)(&f - fields.data()), (DWORD_PTR)this);
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
	}
	for (FormField& f : fields)
		if (f.kind == FormField::List && !prefs().dark) {
			const DialogColors& c = darkColors();
			SendMessageW(f.hwnd, LVM_SETBKCOLOR, 0, c.field);
			SendMessageW(f.hwnd, LVM_SETTEXTBKCOLOR, 0, c.field);
			SendMessageW(f.hwnd, LVM_SETTEXTCOLOR, 0, c.text);
		}

	// Size the window around what's in it, centred on its owner.
	RECT rc = { 0, 0, width, y };
	const DWORD style = (DWORD)GetWindowLongW(dialog, GWL_STYLE), ex = (DWORD)GetWindowLongW(dialog, GWL_EXSTYLE);
	AdjustWindowRectExForDpi(&rc, style, FALSE, ex, dpi);
	int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
	RECT anchor;
	HWND owner = GetWindow(dialog, GW_OWNER);
	if (owner == nullptr || !GetWindowRect(owner, &anchor)) {
		HMONITOR m = MonitorFromWindow(dialog, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi = { sizeof mi };
		GetMonitorInfoW(m, &mi);
		anchor = mi.rcWork;
	}
	HMONITOR m = MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi = { sizeof mi };
	const bool onScreen = GetMonitorInfoW(m, &mi) != FALSE;
	// Taller than the screen (a laptop at 125% or 150%): as tall as the
	// screen, and the form scrolls, so its buttons can still be reached.
	scrollY = scrollHeight = 0;
	const int screenH = mi.rcWork.bottom - mi.rcWork.top;
	if (onScreen && pages.empty() && wh > screenH && screenH > 0) {
		scrollHeight = y;
		const int clientH = std::max(sc(120), y - (wh - screenH));
		wh = clientH + (wh - y);
		ww += GetSystemMetricsForDpi(SM_CXVSCROLL, dpi);
		SetWindowLongW(dialog, GWL_STYLE, (LONG)(style | WS_VSCROLL));
		SCROLLINFO si = { sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS };
		si.nMax = y - 1;
		si.nPage = (UINT)clientH;
		SetScrollInfo(dialog, SB_VERT, &si, FALSE);
		if (prefs().dark) darkenControl(dialog, true, L"Explorer");
	}
	int left = (anchor.left + anchor.right - ww) / 2, top = (anchor.top + anchor.bottom - wh) / 2;
	if (onScreen) {
		left = std::max<int>(mi.rcWork.left, std::min<int>(left, mi.rcWork.right - ww));
		top = std::max<int>(mi.rcWork.top, std::min<int>(top, mi.rcWork.bottom - wh));
	}
	SetWindowPos(dialog, nullptr, left, top, ww, wh, SWP_NOZORDER | SWP_NOACTIVATE | (scrollHeight > 0 ? SWP_FRAMECHANGED : 0));
	if (!pages.empty()) showPage(page);
}

void Form::scrollTo(int to) {
	if (dialog == nullptr || scrollHeight <= 0) return;
	RECT rc;
	GetClientRect(dialog, &rc);
	to = std::max(0, std::min(to, scrollHeight - (int)rc.bottom));
	if (to == scrollY) return;
	// The controls move with it; the fields' frames are drawn where they are.
	ScrollWindowEx(dialog, 0, scrollY - to, nullptr, nullptr, nullptr, nullptr, SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
	scrollY = to;
	SCROLLINFO si = { sizeof si, SIF_POS };
	si.nPos = to;
	SetScrollInfo(dialog, SB_VERT, &si, TRUE);
	UpdateWindow(dialog);
}

void Form::showFocus() {
	if (dialog == nullptr || scrollHeight <= 0) return;
	HWND focus = GetFocus();
	if (focus == nullptr || !IsChild(dialog, focus)) return;
	RECT r;
	GetWindowRect(focus, &r);
	MapWindowPoints(nullptr, dialog, (POINT*)&r, 2);
	// A box in a drawn field: all of the field.
	for (const FormField& x : fields)
		if (x.frame.right > x.frame.left && (x.hwnd == focus || IsChild(x.hwnd, focus))) {
			r = x.frame;
			OffsetRect(&r, 0, -scrollY);
		}
	RECT rc;
	GetClientRect(dialog, &rc);
	const int pad = scaled(12, dpiOf(dialog));
	if (r.top - pad < 0) scrollTo(scrollY + r.top - pad);
	else if (r.bottom + pad > rc.bottom) scrollTo(scrollY + r.bottom + pad - rc.bottom);
}

void Form::refresh(int field) {
	if (field >= 0 && field < (int)fields.size() && fields[field].hwnd) InvalidateRect(fields[field].hwnd, nullptr, FALSE);
}

void Form::enable(int field, bool on) {
	if (field < 0 || field >= (int)fields.size()) return;
	if (fields[field].hwnd) EnableWindow(fields[field].hwnd, on);
	if (HWND label = fields[field].extra) {
		// Labels dim (a disabled static is drawn embossed, which reads badly).
		wchar_t cls[16] = L"";
		GetClassNameW(label, cls, 16);
		if (lstrcmpiW(cls, L"Static") == 0) {
			if (on) RemovePropW(label, L"clDim"); else SetPropW(label, L"clDim", (HANDLE)1);
			InvalidateRect(label, nullptr, TRUE);
		} else {
			EnableWindow(label, on);
		}
	}
}

void Form::showPage(int to) {
	page = std::max(0, std::min((int)pages.size() - 1, to));
	if (dialog == nullptr) return;
	HWND focus = nullptr;
	for (FormField& x : fields) {
		const int show = x.page == page ? SW_SHOW : SW_HIDE;
		if (x.hwnd) ShowWindow(x.hwnd, show);
		if (x.extra) ShowWindow(x.extra, show);
		for (HWND o : x.others) ShowWindow(o, show);
		if (!focus && x.page == page && x.hwnd && (x.kind == FormField::Text || x.kind == FormField::Choice || x.kind == FormField::Check))
			focus = x.hwnd;
	}
	if (HWND tabs = GetDlgItem(dialog, kTabsId)) InvalidateRect(tabs, nullptr, FALSE);
	// The window fits the page, as the Mac's Settings does: what's wrong
	// and the buttons move up under its last field.
	if (page < (int)pageBottoms.size()) {
		const int problemY = pageBottoms[page] + footerGap, buttonsY = problemY + footerButtonsAt;
		RECT pr;
		GetWindowRect(problem, &pr);
		MapWindowPoints(nullptr, dialog, (POINT*)&pr, 2);
		SetWindowPos(problem, nullptr, pr.left, problemY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		for (HWND b : buttonWindows) {
			RECT br;
			GetWindowRect(b, &br);
			MapWindowPoints(nullptr, dialog, (POINT*)&br, 2);
			SetWindowPos(b, nullptr, br.left, buttonsY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		}
		RECT client;
		GetClientRect(dialog, &client);
		RECT rc = { 0, 0, client.right, buttonsY + footerBelow };
		const UINT dpi = dpiOf(dialog);
		AdjustWindowRectExForDpi(&rc, (DWORD)GetWindowLongW(dialog, GWL_STYLE), FALSE, (DWORD)GetWindowLongW(dialog, GWL_EXSTYLE), dpi);
		SetWindowPos(dialog, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	}
	InvalidateRect(dialog, nullptr, TRUE);
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
	return toggleOn(x.hwnd);
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
// that suits its type. Each change is one undo step.

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
// -- what's typed when you leave the box (Tab, Enter, Done), so a word or a
// number is one undo step, as on the Mac and Linux; a number once it's
// valid -- and undone with Ctrl+Z; Rotate, Delete, Done.
void showGateSettings(CircuitWindow* w, long gate) {
	// Locked, or in Simulation View: nothing to change (the menu still offers it).
	if (!w->canEdit()) { w->lockNudge(); return; }
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
	// A whole number is just digits: the engine would read 1e3 as 1 and 0x20
	// as 0. `value` is what's applied (a whole number as the engine reads it).
	auto problem = [](const Setting& s, const std::string& v, std::string& value) -> std::string {
		value = v;
		if (s.type != "INT" && s.type != "FLOAT") return "";
		double x = 0;
		char* end = nullptr;
		bool ok = !v.empty();
		if (ok && s.type == "INT") {
			errno = 0;
			const long long whole = strtoll(v.c_str(), &end, 10);
			ok = end != nullptr && *end == 0 && errno != ERANGE;
			x = (double)whole;
			if (ok) value = std::to_string(whole);
		} else if (ok) {
			x = strtod(v.c_str(), &end);
			ok = end != nullptr && *end == 0 && std::isfinite(x) && v.find_first_of("xX") == std::string::npos;
		}
		if (!ok) return strf("%s: enter %s.", s.name.c_str(), s.type == "INT" ? "a whole number" : "a number");
		if (x < s.min || x > s.max)
			return strf("%s must be between %s and %s.", s.name.c_str(), numberText(s.min).c_str(), numberText(s.max).c_str());
		return "";
	};
	// One undo step, when it's valid and something changed.
	auto apply = [&](Setting& s, const std::string& typed) {
		std::string v;
		if (!problem(s, typed, v).empty() || v == s.value) return;
		cl_gate_set_setting(doc, gate, s.name.c_str(), v.c_str());
		s.value = v;
		w->edited();
	};
	// A switch at once; typing shows what's wrong as it goes and is applied
	// when the box is left (a file from Choose... at once).
	f.onChange = [&](Form& form, int field) {
		for (Setting& s : settings) {
			if (s.field != field) continue;
			if (s.type == "BOOL") {
				form.setProblem("");
				apply(s, form.checked(field) ? "true" : "false");
				return;
			}
			const std::string typed = trimmed(form.text(field));
			std::string v;
			form.setProblem(problem(s, typed, v));
			if (GetFocus() != form.fields[field].hwnd) apply(s, typed);
			return;
		}
	};
	f.onLeave = [&](Form& form, int field) {
		for (Setting& s : settings)
			if (s.field == field && s.type != "BOOL") apply(s, trimmed(form.text(field)));
	};
	f.validate = [&](Form& form) -> std::string {
		for (const Setting& s : settings) {
			std::string v;
			const std::string bad = problem(s, trimmed(form.text(s.field)), v);
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
	// Done, Enter, Escape or the close box keeps what was typed last (when
	// it's valid); Delete took the gate.
	if (f.run(w->window()) != 101)
		for (Setting& s : settings)
			if (s.type != "BOOL") apply(s, trimmed(f.fields[s.field].value));
}

// ---- Quick add (A) -------------------------------------------------------------
// Type part of a gate's name; Return puts the first (or chosen) match on the
// pointer.

namespace {

// QuickAddDialog::fuzzyScore, as the Mac has it: a substring beats letters
// in order; a match at the start beats one in the middle; -1 for no match.
int fuzzyScore(const std::string& query, const std::string& target) {
	const std::string q = lowerCase(query), t = lowerCase(target);
	if (q.empty()) return 0;
	const size_t at = t.find(q);
	if (at != std::string::npos) return at == 0 ? 100 : 80;
	size_t qi = 0;
	int score = 0, last = -2;
	for (size_t ti = 0; ti < t.size() && qi < q.size(); ti++) {
		if (t[ti] != q[qi]) continue;
		score += 10;
		if (last == (int)ti - 1) score += 5;
		if (ti == 0 || t[ti - 1] == ' ' || t[ti - 1] == '-' || t[ti - 1] == '_') score += 5;
		last = (int)ti;
		qi++;
	}
	return qi < q.size() ? -1 : score;
}

// "1 - Basic Gates" -> "Basic Gates".
std::string categoryTitle(const std::string& raw) {
	size_t i = 0;
	while (i < raw.size() && (isdigit((unsigned char)raw[i]) || raw[i] == ' ' || raw[i] == '-')) i++;
	return i < raw.size() ? raw.substr(i) : raw;
}

}  // namespace

// Add a gate by name (A), as the Mac's: type part of a name, arrow to the
// one you want, Enter. Each result shows the gate's own picture. The gate
// then follows the pointer onto the canvas until a click puts it down.
void showQuickAdd(CircuitWindow* w) {
	struct Entry { std::string name, caption, category; };
	std::vector<Entry> all;
	for (int c = 0; c < cl_library_category_count(); c++) {
		const std::string cat = categoryTitle(cl_library_category(c) ? cl_library_category(c) : "");
		for (int i = 0; i < cl_library_gate_count(c); i++) {
			const std::string name = cl_library_gate(c, i);
			bool seen = false;
			for (const Entry& e : all) seen = seen || e.name == name;
			if (seen) continue;
			const std::string caption = cl_library_gate_caption(name.c_str()) ? cl_library_gate_caption(name.c_str()) : "";
			all.push_back({ name, caption.empty() ? name : caption, cat });
		}
	}
	for (const parts::Part& part : parts::all()) all.push_back({ part.gate(), part.name, "My Parts" });

	std::vector<int> results;
	int selected = 0;
	float scroll = 0;
	int hot = -1;
	const float kRowH = 54;
	const int listHeight = 400;

	Form f;
	f.title = "Add a Gate";
	f.width = 520;
	f.okText = "Add";
	FormField entry;
	entry.kind = FormField::Text;
	entry.tip = "Type to search, then press Enter. The gate follows your mouse onto the canvas.";
	const int e = f.add(entry);
	FormField list;
	list.kind = FormField::Picture;
	list.height = listHeight;
	list.paint = [&](ID2D1RenderTarget* rt, float pw, float ph) {
		const Chrome c = chrome();
		const D2D1_COLOR_F ink = c.dark ? D2D1::ColorF(0.886f, 0.902f, 0.933f) : D2D1::ColorF(0.118f, 0.13f, 0.157f);
		if (results.empty()) {
			drawText(rt, "No gates match that.", D2D1::RectF(0, 30, pw, 60), 13, withAlpha(ink, 0.55f), TextAlign::Center);
			return;
		}
		const double s = dpiOf(f.dialog) / 96.0;
		const int first = std::max(0, (int)(scroll / kRowH));
		for (int i = first; i < (int)results.size(); i++) {
			const float y = i * kRowH - scroll;
			if (y > ph) break;
			const Entry& en = all[results[i]];
			const D2D1_RECT_F r = D2D1::RectF(4, y + 3, pw - 4, y + kRowH - 3);
			if (i == selected) fillRound(rt, r, 11, withAlpha(c.accent(), c.dark ? 0.26f : 0.16f));
			else if (i == hot) fillRound(rt, r, 11, withAlpha(ink, 0.06f));
			D2D1_MATRIX_3X2_F was;
			rt->GetTransform(&was);
			rt->SetTransform(D2D1::Matrix3x2F::Translation(18, y + 7) * was);
			if (parts::isPart(en.name)) parts::draw(en.name, rt, 40, 40, s, c.dark);
			else cl_library_draw_gate(en.name.c_str(), rt, 40, 40, s, c.dark);
			rt->SetTransform(was);
			drawText(rt, en.caption, D2D1::RectF(74, y + 9, pw - 12, y + 28), 13, ink, TextAlign::Leading, true);
			const std::string sub = en.category == "My Parts" || en.caption == en.name ? en.category : en.name + "  ·  " + en.category;
			drawText(rt, sub, D2D1::RectF(74, y + 29, pw - 12, y + 46), 10.5f, withAlpha(ink, 0.55f));
		}
	};
	const int l = f.add(list);

	auto keepInView = [&](Form& form) {
		const float top = selected * kRowH, bottom = top + kRowH;
		if (top < scroll) scroll = top;
		else if (bottom > scroll + listHeight) scroll = bottom - listHeight;
		form.refresh(l);
	};
	auto filter = [&](Form& form) {
		const std::string q = trimmed(form.text(e));
		std::vector<std::pair<int, int>> scored;   // score, index
		for (int i = 0; i < (int)all.size(); i++) {
			const int sc = q.empty() ? 1 : std::max(fuzzyScore(q, all[i].caption), fuzzyScore(q, all[i].name));
			if (sc > 0) scored.push_back({ sc, i });
		}
		std::stable_sort(scored.begin(), scored.end(), [](auto& a, auto& b) { return a.first > b.first; });
		results.clear();
		for (auto& x : scored) results.push_back(x.second);
		selected = 0;
		scroll = 0;
		form.refresh(l);
	};
	f.onInit = filter;
	f.onChange = [&](Form& form, int field) { if (field == e) filter(form); };
	f.onKey = [&](Form& form, int, UINT vk) {
		if (results.empty()) return true;
		const int page = std::max(1, (int)(listHeight / kRowH) - 1);
		const int d = vk == VK_UP ? -1 : vk == VK_DOWN ? 1 : vk == VK_PRIOR ? -page : page;
		selected = std::max(0, std::min((int)results.size() - 1, selected + d));
		keepInView(form);
		return true;
	};
	f.onWheel = [&](Form& form, int, int delta) {
		const float most = std::max(0.0f, results.size() * kRowH - listHeight);
		scroll = std::max(0.0f, std::min(most, scroll - delta / (float)WHEEL_DELTA * kRowH * 2));
		form.refresh(l);
	};
	// A click chooses; a second click on the chosen one adds it.
	f.onClick = [&](Form& form, int, float, float y) {
		const int i = (int)((y + scroll) / kRowH);
		if (i < 0 || i >= (int)results.size()) return;
		if (i == selected) { SendMessageW(form.dialog, WM_COMMAND, IDOK, 0); return; }
		selected = i;
		form.refresh(l);
		SetFocus(form.fields[e].hwnd);
	};
	std::string chosen;
	f.validate = [&](Form&) -> std::string {
		chosen = selected >= 0 && selected < (int)results.size() ? all[results[selected]].name : std::string();
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
	// Locked, or in Simulation View: it can be watched, not changed.
	const bool editable = w->canEdit();

	const float addrW = 70, headH = 24, rowH = 25;
	const float cellW = std::max(3, std::max(dDigits, decDigits) + 1) * 7.0f + 10;
	const float gridW = 12 + addrW + cols * (cellW + 3) + 10;
	// Tall enough for every row of a small memory; 380 points and a scroll for a big one.
	const int gridHeight = (int)std::min(380.0f, headH + 8 + rowsTotal * rowH + 6);
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
		drawText(rt, strf("%lu addresses × %d bits · %s", words, dataBits, editable ? "click a value to change it" : "locked"),
		         D2D1::RectF(70, 32, pw - 170, 50), 11, withAlpha(k, 0.55f));
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
			// No wider than a word, and not negative (strtoul would wrap -1).
			const unsigned long most = dataBits >= (int)(sizeof(unsigned long) * 8) ? ULONG_MAX : (1UL << std::max(dataBits, 0)) - 1;
			errno = 0;
			const unsigned long v = strtoul(t.c_str(), &end, decimal ? 10 : 16);
			if (!t.empty() && t[0] != '-' && t[0] != '+' && end && *end == 0 && errno != ERANGE && v <= most) {
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
		if (!editable) { w->lockNudge(); return; }
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
	// Three rows a notch; a touchpad's small steps add up to rows.
	int wheelRest = 0;
	f.onWheel = [&](Form& form, int field, int delta) {
		if (field != gridField) return;
		finishEdit(form, true);
		wheelRest += delta;
		const int rows = wheelRest * 3 / WHEEL_DELTA;
		if (rows == 0) return;
		wheelRest -= rows * WHEEL_DELTA / 3;
		scrollTo(form, scrollRow - rows);
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
		if (load && !editable) { w->lockNudge(); return false; }
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

int g_preferencesPage = 0;   // the page it opens on (--page, for CI)
void setPreferencesPage(int page) { g_preferencesPage = page; }

void showPreferencesDialog(HWND parent) {
	Prefs& p = prefs();
	Form f;
	f.title = "Preferences";
	f.width = 460;
	f.okText = "Close";
	f.cancelText = "";
	f.pages = { "General", "Appearance", "Canvas" };
	f.page = g_preferencesPage;
	FormField who;
	who.kind = FormField::Text;
	who.label = "Your name";
	who.value = p.studentName;
	who.tip = "On the Lab Page template and exported pictures.";
	const int name = f.add(who);
	const int askQuit = f.add(checkField("Ask before quitting with Ctrl+Q", p.confirmQuit));
	const int status = f.add(checkField("Show the status bar (zoom, pointer, counts)", p.showStatus));
	f.adding = 1;
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
	f.adding = 2;
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
		else if (field == status) q.showStatus = form.checked(status);
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
	f.width = 440;
	f.okText = "Quit";
	f.buttons = { "Always Quit" };
	// As the Mac asks: the icon, the question, and the reassurance.
	FormField q;
	q.kind = FormField::Picture;
	q.height = 118;
	q.paint = [](ID2D1RenderTarget* rt, float w, float) {
		const D2D1_COLOR_F ink = prefs().dark ? D2D1::ColorF(0.89f, 0.9f, 0.93f) : D2D1::ColorF(0.1f, 0.11f, 0.13f);
		brand::icon(rt, 2, 4, 44, 0);
		drawText(rt, "Are you sure you want to quit CedarLogic?", D2D1::RectF(2, 62, w, 88), 17, ink, TextAlign::Leading, true);
		drawText(rt, "Your circuits are saved; they'll be here when you come back.", D2D1::RectF(2, 90, w, 110), 12.5f, withAlpha(ink, 0.6f));
	};
	f.add(q);
	f.onButton = [](Form&, int) {
		prefs().confirmQuit = false;
		prefs().save();
		return true;
	};
	const int r = f.run(parent);
	return r == IDOK || r == 100;
}

// ---- The oscilloscope: Scope.cpp ----
