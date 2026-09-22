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

void MacSetSeamlessTitlebar(void* nsWindow, bool on, unsigned char r, unsigned char g, unsigned char b) {
    if (nsWindow == NULL) return;
    NSWindow *window = (NSWindow *)nsWindow;
    window.titlebarAppearsTransparent = on;
    window.backgroundColor = on
        ? [NSColor colorWithSRGBRed:r / 255.0 green:g / 255.0 blue:b / 255.0 alpha:1.0]
        : [NSColor windowBackgroundColor];
}

static bool gCustomTitlebar = false;
static double gBarHeight = 52.0;
static NSRect gOrigContainer;
static NSPoint gOrigButtons[3];
static bool gSavedOrig = false;
static bool gObserving = false;

static NSArray* windowButtons(NSWindow* w) {
    NSButton* c = [w standardWindowButton:NSWindowCloseButton];
    NSButton* m = [w standardWindowButton:NSWindowMiniaturizeButton];
    NSButton* z = [w standardWindowButton:NSWindowZoomButton];
    if (!c || !m || !z) return nil;
    return @[c, m, z];
}

static void layoutTrafficLights(NSWindow* w) {
    NSArray* buttons = windowButtons(w);
    if (!buttons) return;
    NSView* container = [[buttons[0] superview] superview];
    if (!container) return;
    if (!gSavedOrig) {
        gOrigContainer = container.frame;
        for (int i = 0; i < 3; i++) gOrigButtons[i] = [buttons[i] frame].origin;
        gSavedOrig = true;
    }
    if (!gCustomTitlebar) {
        container.frame = NSMakeRect(gOrigContainer.origin.x, NSHeight(w.frame) - NSHeight(gOrigContainer),
                                     NSWidth(container.frame), NSHeight(gOrigContainer));
        for (int i = 0; i < 3; i++) [buttons[i] setFrameOrigin:gOrigButtons[i]];
        return;
    }
    if (w.styleMask & NSWindowStyleMaskFullScreen) return;
    NSRect f = container.frame;
    f.size.height = gBarHeight;
    f.origin.y = NSHeight(w.frame) - gBarHeight;
    container.frame = f;
    const CGFloat spacing = gOrigButtons[1].x - gOrigButtons[0].x;
    const CGFloat x0 = 18.0;
    for (int i = 0; i < 3; i++) {
        NSButton* b = buttons[i];
        [b setFrameOrigin:NSMakePoint(x0 + i * spacing, round((gBarHeight - NSHeight(b.frame)) / 2.0))];
    }
}

void MacSetCustomTitlebar(void* nsWindow, bool on, double barHeight) {
    if (nsWindow == NULL) return;
    NSWindow* w = (NSWindow*)nsWindow;
    gCustomTitlebar = on;
    gBarHeight = barHeight;
    if (on) w.styleMask |= NSWindowStyleMaskFullSizeContentView;
    else w.styleMask &= ~NSWindowStyleMaskFullSizeContentView;
    w.titlebarAppearsTransparent = on;
    w.titleVisibility = on ? NSWindowTitleHidden : NSWindowTitleVisible;
    if (!gObserving) {
        gObserving = true;
        // AppKit re-lays the title bar on these; put the buttons back each time.
        for (NSString* name in @[NSWindowDidResizeNotification, NSWindowDidEndLiveResizeNotification,
                                 NSWindowDidExitFullScreenNotification, NSWindowDidBecomeKeyNotification]) {
            [[NSNotificationCenter defaultCenter] addObserverForName:name object:w queue:nil
                usingBlock:^(NSNotification*) { layoutTrafficLights(w); }];
        }
    }
    layoutTrafficLights(w);
}

void MacSetBackgroundApp() {
	// Accessory: no Dock tile, no menu bar, never becomes the active app. The
	// render window still draws, it just does not come to the front.
	[NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
}

void MacDragWindow(void* nsWindow) {
    NSWindow* w = (NSWindow*)nsWindow;
    NSEvent* e = [NSApp currentEvent];
    if (w && e && e.type == NSEventTypeLeftMouseDown) [w performWindowDragWithEvent:e];
}

void MacTitlebarDoubleClick(void* nsWindow) {
    NSWindow* w = (NSWindow*)nsWindow;
    if (!w) return;
    NSString* action = [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleActionOnDoubleClick"];
    if ([action isEqualToString:@"Minimize"]) [w performMiniaturize:nil];
    else if (![action isEqualToString:@"None"]) [w performZoom:nil];
}

#endif // __APPLE__
