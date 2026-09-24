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
