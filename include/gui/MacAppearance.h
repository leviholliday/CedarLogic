/*****************************************************************************
   Project: CEDAR Logic Simulator
   MacAppearance: pin macOS-native chrome to an app surface's own appearance
*****************************************************************************/

#ifndef MACAPPEARANCE_H
#define MACAPPEARANCE_H

#ifdef __APPLE__

// Force `nsView` (from wxWindow::GetHandle()) and everything nested inside it to
// the light appearance, whatever the system is set to. For panels the app paints
// light itself: their native chrome -- scrollbars above all -- otherwise inherits
// the system appearance and renders for a dark background that isn't there.
void MacForceLightAppearance(void* nsView);

// Generalized MacForceLightAppearance: pin `nsView` (and everything nested
// inside it, e.g. a scrollbar) to a specific appearance so its native chrome
// matches a panel this app paints itself, rather than whatever the app-wide
// appearance is. mode: 0 = inherit (stop pinning, follow the app-wide
// appearance again), 1 = force light, 2 = force dark. Used by panels whose own
// background follows the in-app theme toggle (e.g. the gate palette), where
// the pin needs to flip along with it instead of staying fixed to light.
void MacSetViewAppearance(void* nsView, int mode);

// Set the whole app's appearance (every window's native chrome -- menu bar,
// dialogs, scrollbars) so it follows the in-app dark-mode toggle instead of
// only the system setting. mode: 0 = follow system, 1 = force light,
// 2 = force dark. A per-view pin from MacForceLightAppearance above still wins
// for that view, since a view's own appearance always overrides the app's.
void MacSetApplicationAppearance(int mode);

#endif // __APPLE__

#endif // MACAPPEARANCE_H
