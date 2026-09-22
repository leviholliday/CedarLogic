/*****************************************************************************
   Project: CEDAR Logic Simulator
   TabSwitcherMac: the Ctrl+Tab page switcher's window -- a glass panel of
   page snapshots with a highlight that glides to the chosen one.
*****************************************************************************/

#ifndef TABSWITCHERMAC_H_
#define TABSWITCHERMAC_H_

#ifdef __APPLE__

#include <functional>
#include <vector>
#include <wx/string.h>
#include <wx/bitmap.h>

class wxWindow;

struct TabSwitcherCard {
	wxString title;
	wxBitmap thumbnail;
};

// Show the panel centered over `over`'s window. `onHover` fires when the
// mouse (actually moved, not just sitting there) goes over a card; `onClick`
// when one is clicked. Replaces any panel already showing.
void MacTabSwitcher_Show(wxWindow* over, const std::vector<TabSwitcherCard>& cards,
                         int selected, bool dark,
                         std::function<void(int)> onHover,
                         std::function<void(int)> onClick);

// Move the highlight to card `index` (animated).
void MacTabSwitcher_Select(int index);

// Fade the panel out and drop it. Safe to call when nothing is showing.
void MacTabSwitcher_Hide();

// macOS consumes Ctrl+Tab (focus navigation) before wx's key events ever see
// it, so catch it app-wide first. `onCtrlTab(shift)` and `onEscape()` return
// whether they handled the key; unhandled keys carry on as normal. Call once.
void MacTabSwitcher_InstallKeyMonitor(std::function<bool(bool)> onCtrlTab,
                                      std::function<bool()> onEscape);

// Whether the physical Control key is down right now.
bool MacControlKeyDown();

#endif
#endif
