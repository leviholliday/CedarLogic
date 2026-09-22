/*****************************************************************************
   Project: CEDAR Logic Simulator
   MacAppearance: pin macOS-native chrome to an app surface's own appearance
*****************************************************************************/

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#include "MacAppearance.h"

void MacForceLightAppearance(void* nsView) {
    if (nsView == NULL) return;
    NSView *view = (NSView *)nsView;
    // NSAppearance inherits down the view tree, so this reaches the NSScroller
    // wxWidgets parents to the window as well as the window itself.
    view.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
}

void MacSetViewAppearance(void* nsView, int mode) {
    if (nsView == NULL) return;
    NSView *view = (NSView *)nsView;
    NSAppearance *appearance = nil;   // nil = stop pinning, inherit again
    if (mode == 1) {
        appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
    } else if (mode == 2) {
        appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    }
    view.appearance = appearance;
}

void MacSetApplicationAppearance(int mode) {
    NSAppearance *appearance = nil;   // nil = inherit the system setting
    if (mode == 1) {
        appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
    } else if (mode == 2) {
        appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    }
    // NSApp.appearance cascades to every window's native chrome (menu bar,
    // dialogs, scrollbars) that hasn't pinned its own -- which is exactly the
    // in-app manual toggle this app wants: independent of, but not fighting,
    // the system setting the person picked in System Settings.
    NSApp.appearance = appearance;
}

#endif // __APPLE__
