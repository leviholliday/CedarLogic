/*****************************************************************************
   Project: CEDAR Logic Simulator
   WinAppearance: match the Windows title bar to the app's light/dark theme.
*****************************************************************************/

#ifdef _WIN32

#include "WinAppearance.h"

#include <wx/toplevel.h>
#include <wx/msw/wrapwin.h>
#include <dwmapi.h>

namespace {

// DWMWA_USE_IMMERSIVE_DARK_MODE. The SDK only names it from 10.0.22000 on, and
// Windows 10 1809-1909 answered to 19 before it was made public as 20.
constexpr DWORD kDarkModeAttr = 20;
constexpr DWORD kDarkModeAttrPre2004 = 19;

}  // namespace

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

void WinSetDarkTitlebars(bool dark) {
	for (wxWindowList::compatibility_iterator n = wxTopLevelWindows.GetFirst(); n; n = n->GetNext())
		WinSetDarkTitlebar(wxDynamicCast(n->GetData(), wxTopLevelWindow), dark);
}

#endif // _WIN32
