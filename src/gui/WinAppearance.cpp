/*****************************************************************************
   Project: CEDAR Logic Simulator
   WinAppearance: match the Windows title bar to the app's light/dark theme.
*****************************************************************************/

#ifdef _WIN32

#include "WinAppearance.h"

#include <wx/toplevel.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/glcanvas.h>
#include <wx/textctrl.h>
#include <wx/spinctrl.h>
#include <wx/slider.h>
#include <wx/dcmemory.h>
#include <wx/bitmap.h>
#include <wx/msw/wrapwin.h>
#include <dwmapi.h>
#include <uxtheme.h>

namespace {

// DWMWA_USE_IMMERSIVE_DARK_MODE. The SDK only names it from 10.0.22000 on, and
// Windows 10 1809-1909 answered to 19 before it was made public as 20.
constexpr DWORD kDarkModeAttr = 20;
constexpr DWORD kDarkModeAttrPre2004 = 19;

// The dark-mode switches live in uxtheme.dll, exported by ordinal only. They
// are what Explorer and Notepad++ use, and what wxWidgets 3.3 calls; on a
// Windows without them every pointer stays null and nothing changes.
enum class AppMode { Default = 0, AllowDark = 1, ForceDark = 2, ForceLight = 3 };
using SetPreferredAppModeFn = int (WINAPI*)(int);
using AllowDarkModeForWindowFn = BOOL (WINAPI*)(HWND, BOOL);
using FlushMenuThemesFn = void (WINAPI*)();

struct DarkApi {
	SetPreferredAppModeFn setPreferredAppMode = nullptr;
	AllowDarkModeForWindowFn allowDarkModeForWindow = nullptr;
	FlushMenuThemesFn flushMenuThemes = nullptr;
	DarkApi() {
		HMODULE ux = ::LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (!ux) return;
		setPreferredAppMode = reinterpret_cast<SetPreferredAppModeFn>(::GetProcAddress(ux, MAKEINTRESOURCEA(135)));
		allowDarkModeForWindow = reinterpret_cast<AllowDarkModeForWindowFn>(::GetProcAddress(ux, MAKEINTRESOURCEA(133)));
		flushMenuThemes = reinterpret_cast<FlushMenuThemesFn>(::GetProcAddress(ux, MAKEINTRESOURCEA(136)));
		// Before 1903 ordinal 135 was a different function with a BOOL
		// argument; only trust the set when all three are there.
		if (!setPreferredAppMode || !allowDarkModeForWindow || !flushMenuThemes)
			setPreferredAppMode = nullptr;
	}
	bool ok() const { return setPreferredAppMode != nullptr; }
};

const DarkApi& darkApi() {
	static const DarkApi api;
	return api;
}

void themeOne(wxWindow* w, bool dark) {
	HWND hwnd = static_cast<HWND>(w->GetHWND());
	if (hwnd == nullptr) return;
	// The canvases draw every pixel themselves through OpenGL; leave them be.
	if (wxDynamicCast(w, wxGLCanvas)) return;
	darkApi().allowDarkModeForWindow(hwnd, dark ? TRUE : FALSE);
	// Dropdowns take the "CFD" (common file dialog) theme for a dark field and
	// list; everything else takes Explorer's, which darkens its scrollbars.
	const bool dropdown = wxDynamicCast(w, wxChoice) || wxDynamicCast(w, wxComboBox);
	::SetWindowTheme(hwnd, dark ? (dropdown ? L"DarkMode_CFD" : L"DarkMode_Explorer") : nullptr, nullptr);
	if (dropdown) {
		w->SetBackgroundColour(dark ? wxColour(38, 41, 48) : wxNullColour);
		w->SetForegroundColour(dark ? wxColour(228, 232, 240) : wxNullColour);
	}
	// Fields you type in: a dark field with light text. The stock edit
	// control has no dark theme of its own for its face.
	const bool field = wxDynamicCast(w, wxTextCtrl) || wxDynamicCast(w, wxSpinCtrl) ||
	                   wxDynamicCast(w, wxSpinCtrlDouble);
	if (field) {
		w->SetBackgroundColour(dark ? wxColour(30, 33, 39) : wxNullColour);
		w->SetForegroundColour(dark ? wxColour(228, 232, 240) : wxNullColour);
	}
	// A slider paints its own background: make it whatever it sits on.
	if (wxDynamicCast(w, wxSlider) && w->GetParent())
		w->SetBackgroundColour(w->GetParent()->GetBackgroundColour());
	::SendMessageW(hwnd, WM_THEMECHANGED, 0, 0);
}

}  // namespace

void WinSetAppDarkMode(bool dark) {
	const DarkApi& api = darkApi();
	if (!api.ok()) return;
	api.setPreferredAppMode((int)(dark ? AppMode::ForceDark : AppMode::ForceLight));
	api.flushMenuThemes();   // right-click and dropdown menus
}

void WinThemeControls(wxWindow* root, bool dark) {
	if (root == nullptr || !darkApi().ok()) return;
	themeOne(root, dark);
	for (wxWindowList::compatibility_iterator n = root->GetChildren().GetFirst(); n; n = n->GetNext())
		WinThemeControls(n->GetData(), dark);
	root->Refresh();
}

void WinSetDarkTitlebar(wxTopLevelWindow* window, bool dark) {
	if (window == nullptr) return;
	HWND hwnd = static_cast<HWND>(window->GetHWND());
	if (hwnd == nullptr) return;
	const BOOL value = dark ? TRUE : FALSE;
	if (FAILED(DwmSetWindowAttribute(hwnd, kDarkModeAttr, &value, sizeof(value))))
		DwmSetWindowAttribute(hwnd, kDarkModeAttrPre2004, &value, sizeof(value));
	// A window that is already up keeps its old caption until something makes
	// Windows repaint the frame.
	if (window->IsShown())
		SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
		             SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
		             SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void WinSetCaptionColour(wxTopLevelWindow* window, const wxColour& bar, const wxColour& text) {
	if (window == nullptr) return;
	HWND hwnd = static_cast<HWND>(window->GetHWND());
	if (hwnd == nullptr) return;
	// DWMWA_BORDER_COLOR 34, DWMWA_CAPTION_COLOR 35, DWMWA_TEXT_COLOR 36:
	// Windows 11 only, named from the 10.0.22000 SDK. Windows 10 refuses them.
	const COLORREF caption = RGB(bar.Red(), bar.Green(), bar.Blue());
	const COLORREF ink = RGB(text.Red(), text.Green(), text.Blue());
	DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
	DwmSetWindowAttribute(hwnd, 36, &ink, sizeof(ink));
	DwmSetWindowAttribute(hwnd, 34, &caption, sizeof(caption));
}

bool WinCaptureWindow(wxWindow* window, const wxString& pngPath) {
	if (window == nullptr) return false;
	HWND hwnd = static_cast<HWND>(window->GetHWND());
	RECT rc;
	if (hwnd == nullptr || !::GetWindowRect(hwnd, &rc)) return false;
	const int w = rc.right - rc.left, h = rc.bottom - rc.top;
	if (w <= 0 || h <= 0) return false;
	wxBitmap bmp(w, h, 24);
	{
		wxMemoryDC dc(bmp);
		// PW_RENDERFULLCONTENT (2): what DWM composes, so the OpenGL canvas and
		// the title bar come along, not just what GDI drew.
		if (!::PrintWindow(hwnd, static_cast<HDC>(dc.GetHDC()), 2)) return false;
	}
	return bmp.SaveFile(pngPath, wxBITMAP_TYPE_PNG);
}

void WinRoundCorners(wxTopLevelWindow* window) {
	if (window == nullptr) return;
	HWND hwnd = static_cast<HWND>(window->GetHWND());
	if (hwnd == nullptr) return;
	// DWMWA_WINDOW_CORNER_PREFERENCE = 33, DWMWCP_ROUND = 2: named only in the
	// 10.0.22000 SDK. Windows 10 rejects the attribute, which is fine.
	const DWORD attr = 33;
	const int round = 2;
	DwmSetWindowAttribute(hwnd, attr, &round, sizeof(round));
}

void WinSetDarkTitlebars(bool dark) {
	for (wxWindowList::compatibility_iterator n = wxTopLevelWindows.GetFirst(); n; n = n->GetNext())
		WinSetDarkTitlebar(wxDynamicCast(n->GetData(), wxTopLevelWindow), dark);
}

#endif // _WIN32
