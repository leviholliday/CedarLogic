// What people see of sync (docs/SYNC.md section 5): Settings > Sync (turn on,
// I Have a Code, the code and its QR code, devices, turn off, start over,
// delete the synced copy), the link preview and its confirmation, the sync
// line under Your Circuits, and what a cedarlogic://sync link does.

#ifndef CL_LINUX_SYNCUI_H
#define CL_LINUX_SYNCUI_H

#include "App.h"
#include <string>

class CircuitWindow;

namespace syncui {

// Settings' sixth page (Settings.cpp puts it in its window).
GtkWidget* settingsPage();

// A cedarlogic://sync#k=<code> link: Settings > Sync, the code checked, its preview and the
// "Link this computer?" question. Never links by itself.
void linkFromUrl(const std::string& link);

// Your Circuits' line under the list: "Synced just now · 3 circuits" with Sync Now while sync is on,
// a quiet "Sync…" while it's off. `button` is what the line's button says, "" for none.
struct Line {
	std::string text, button;
	bool on = false;
};
Line yourCircuitsLine();
// Its button: Sync Now, or Settings > Sync.
void yourCircuitsAction(CircuitWindow* from);
// A circuit's own problem line ("Too big to sync (over 512 KB)"), or "".
std::string problemFor(const std::string& folderId);

// For the screenshot runs: --show sync (the Settings page), synccode (the code and its QR code),
// synclink (I Have a Code, typed), syncconfirm (the question before linking).
void showForScreenshot(CircuitWindow* from, const std::string& what);

}  // namespace syncui

#endif  // CL_LINUX_SYNCUI_H
