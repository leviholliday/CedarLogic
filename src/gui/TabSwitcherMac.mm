/*****************************************************************************
   Project: CEDAR Logic Simulator
   TabSwitcherMac: the Ctrl+Tab page switcher's window.
   (Manual retain/release: this target isn't built with ARC.)
*****************************************************************************/

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include "TabSwitcherMac.h"
#include <wx/window.h>
#include <wx/toplevel.h>
#include <wx/osx/core/cfstring.h>
#include <algorithm>

static const CGFloat kThumbW = 208, kThumbH = 130;
static const CGFloat kLabelH = 18, kLabelGap = 8, kCardPad = 10;
static const CGFloat kGap = 6, kPanelPad = 14, kPanelRadius = 28;
static const int kPerRow = 5;

static NSPanel* gPanel = nil;
static NSView* gHighlight = nil;
static NSMutableArray* gCards = nil;    // CLTabCardView*
static NSMutableArray* gLabels = nil;   // NSTextField*
static NSPoint gShowMouse;
static std::function<void(int)> gOnHover, gOnClick;

@interface CLSwitcherPanel : NSPanel
@end
@implementation CLSwitcherPanel
// Never take focus from the main window: it has to keep receiving the Tab
// presses and noticing when Ctrl is let go.
- (BOOL)canBecomeKeyWindow { return NO; }
- (BOOL)canBecomeMainWindow { return NO; }
@end

@interface CLTabCardView : NSView {
@public
	int index;
}
@end
@implementation CLTabCardView
- (void)updateTrackingAreas {
	[super updateTrackingAreas];
	for (NSTrackingArea* a in [[self.trackingAreas copy] autorelease]) [self removeTrackingArea:a];
	NSTrackingArea* area = [[NSTrackingArea alloc] initWithRect:NSZeroRect
		options:NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved |
		        NSTrackingActiveAlways | NSTrackingInVisibleRect
		owner:self userInfo:nil];
	[self addTrackingArea:area];
	[area release];
}
- (void)hovered {
	// The panel opens under wherever the pointer already is; only react once
	// it actually moves, or keyboard picks would get overridden.
	if (NSEqualPoints([NSEvent mouseLocation], gShowMouse)) return;
	if (gOnHover) gOnHover(index);
}
- (void)mouseEntered:(NSEvent*)e { [self hovered]; }
- (void)mouseMoved:(NSEvent*)e { [self hovered]; }
- (BOOL)acceptsFirstMouse:(NSEvent*)e { return YES; }
// Ctrl is held down the whole time, so a click may arrive as a right-click.
- (void)mouseDown:(NSEvent*)e { if (gOnClick) gOnClick(index); }
- (void)rightMouseDown:(NSEvent*)e { if (gOnClick) gOnClick(index); }
- (NSMenu*)menuForEvent:(NSEvent*)e { return nil; }
// Clicks on the thumbnail or label count as clicks on the card.
- (NSView*)hitTest:(NSPoint)p { return [super hitTest:p] ? self : nil; }
@end

static void styleLabels(int selected) {
	for (NSUInteger i = 0; i < gLabels.count; i++) {
		NSTextField* l = gLabels[i];
		const bool on = (int)i == selected;
		l.textColor = on ? NSColor.labelColor : NSColor.secondaryLabelColor;
		l.font = [NSFont systemFontOfSize:12 weight:on ? NSFontWeightSemibold : NSFontWeightRegular];
	}
}

void MacTabSwitcher_Show(wxWindow* over, const std::vector<TabSwitcherCard>& cards,
                         int selected, bool dark,
                         std::function<void(int)> onHover,
                         std::function<void(int)> onClick) {
	@autoreleasepool {
	MacTabSwitcher_Hide();
	if (cards.empty()) return;
	wxWindow* top = wxGetTopLevelParent(over);
	NSWindow* parent = top ? (NSWindow*)top->MacGetTopLevelWindowRef() : nil;
	if (parent == nil) return;

	gOnHover = onHover;
	gOnClick = onClick;
	gShowMouse = [NSEvent mouseLocation];

	const int n = (int)cards.size();
	const int perRow = std::min(n, kPerRow);
	const int rows = (n + kPerRow - 1) / kPerRow;
	const CGFloat cellW = kThumbW + 2 * kCardPad;
	const CGFloat cellH = kCardPad + kThumbH + kLabelGap + kLabelH + kCardPad;
	const CGFloat contentW = perRow * cellW + (perRow - 1) * kGap;
	const CGFloat contentH = rows * cellH + (rows - 1) * kGap;
	const CGFloat W = contentW + 2 * kPanelPad, H = contentH + 2 * kPanelPad;
	const NSRect pf = parent.frame;
	const NSRect frame = NSMakeRect(round(NSMidX(pf) - W / 2), round(NSMidY(pf) - H / 2), W, H);

	gPanel = [[CLSwitcherPanel alloc] initWithContentRect:frame
		styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
		backing:NSBackingStoreBuffered defer:NO];
	gPanel.opaque = NO;
	gPanel.backgroundColor = NSColor.clearColor;
	gPanel.hasShadow = YES;
	gPanel.releasedWhenClosed = NO;
	gPanel.appearance = [NSAppearance appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];

	const NSRect bounds = NSMakeRect(0, 0, W, H);
	NSView* content = [[[NSView alloc] initWithFrame:bounds] autorelease];
	content.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
	if (@available(macOS 26.0, *)) {
		NSGlassEffectView* glass = [[[NSGlassEffectView alloc] initWithFrame:bounds] autorelease];
		glass.cornerRadius = kPanelRadius;
		glass.style = NSGlassEffectViewStyleRegular;
		glass.contentView = content;
		gPanel.contentView = glass;
	} else {
		NSVisualEffectView* blur = [[[NSVisualEffectView alloc] initWithFrame:bounds] autorelease];
		blur.material = NSVisualEffectMaterialHUDWindow;
		blur.blendingMode = NSVisualEffectBlendingModeBehindWindow;
		blur.state = NSVisualEffectStateActive;
		blur.wantsLayer = YES;
		blur.layer.cornerRadius = kPanelRadius;
		blur.layer.masksToBounds = YES;
		[blur addSubview:content];
		gPanel.contentView = blur;
	}

	// The selection highlight sits under the cards and glides between them.
	gHighlight = [[NSView alloc] initWithFrame:NSZeroRect];
	gHighlight.wantsLayer = YES;
	gHighlight.layer.cornerRadius = 18;
	gHighlight.layer.backgroundColor = dark
		? [NSColor colorWithWhite:1.0 alpha:0.16].CGColor
		: [NSColor colorWithWhite:0.0 alpha:0.08].CGColor;
	gHighlight.layer.borderWidth = 1.0;
	gHighlight.layer.borderColor = dark
		? [NSColor colorWithWhite:1.0 alpha:0.14].CGColor
		: [NSColor colorWithWhite:0.0 alpha:0.06].CGColor;
	[content addSubview:gHighlight];

	gCards = [[NSMutableArray alloc] init];
	gLabels = [[NSMutableArray alloc] init];
	for (int i = 0; i < n; i++) {
		const int row = i / kPerRow, col = i % kPerRow;
		const int inRow = std::min(kPerRow, n - row * kPerRow);
		const CGFloat rowW = inRow * cellW + (inRow - 1) * kGap;
		const CGFloat x = kPanelPad + (contentW - rowW) / 2 + col * (cellW + kGap);
		const CGFloat y = H - kPanelPad - (row + 1) * cellH - row * kGap;

		CLTabCardView* card = [[[CLTabCardView alloc] initWithFrame:NSMakeRect(x, y, cellW, cellH)] autorelease];
		card->index = i;

		NSImageView* thumb = [[[NSImageView alloc] initWithFrame:
			NSMakeRect(kCardPad, kCardPad + kLabelH + kLabelGap, kThumbW, kThumbH)] autorelease];
		if (cards[i].thumbnail.IsOk()) thumb.image = cards[i].thumbnail.GetNSImage();
		thumb.imageScaling = NSImageScaleProportionallyUpOrDown;
		thumb.wantsLayer = YES;
		thumb.layer.cornerRadius = 10;
		thumb.layer.masksToBounds = YES;
		thumb.layer.borderWidth = 0.5;
		thumb.layer.borderColor = dark
			? [NSColor colorWithWhite:1.0 alpha:0.14].CGColor
			: [NSColor colorWithWhite:0.0 alpha:0.12].CGColor;
		[card addSubview:thumb];

		NSTextField* label = [NSTextField labelWithString:wxCFStringRef(cards[i].title).AsNSString()];
		label.frame = NSMakeRect(kCardPad, kCardPad, kThumbW, kLabelH);
		label.alignment = NSTextAlignmentCenter;
		label.lineBreakMode = NSLineBreakByTruncatingTail;
		[card addSubview:label];

		[content addSubview:card];
		[gCards addObject:card];
		[gLabels addObject:label];
	}

	if (selected >= 0 && selected < n) gHighlight.frame = [gCards[selected] frame];
	styleLabels(selected);

	gPanel.alphaValue = 0.0;
	[parent addChildWindow:gPanel ordered:NSWindowAbove];
	[gPanel orderFront:nil];
	[NSAnimationContext runAnimationGroup:^(NSAnimationContext* ctx) {
		ctx.duration = 0.12;
		ctx.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseOut];
		gPanel.animator.alphaValue = 1.0;
	} completionHandler:nil];
	}
}

void MacTabSwitcher_Select(int index) {
	if (gPanel == nil || index < 0 || index >= (int)gCards.count) return;
	// Snap straight there; the layer's implicit animation is disabled so it
	// doesn't glide.
	[CATransaction begin];
	[CATransaction setDisableActions:YES];
	gHighlight.frame = [gCards[index] frame];
	[CATransaction commit];
	styleLabels(index);
}

static std::function<bool(bool)> gOnCtrlTab;
static std::function<bool()> gOnEscape;

void MacTabSwitcher_InstallKeyMonitor(std::function<bool(bool)> onCtrlTab,
                                      std::function<bool()> onEscape) {
	static bool installed = false;
	gOnCtrlTab = onCtrlTab;
	gOnEscape = onEscape;
	if (installed) return;
	installed = true;
	[NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown handler:^NSEvent*(NSEvent* e) {
		const NSEventModifierFlags f = e.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
		const bool ctrl = (f & NSEventModifierFlagControl) && !(f & (NSEventModifierFlagCommand | NSEventModifierFlagOption));
		if (e.keyCode == 48 /* Tab */ && ctrl && gOnCtrlTab) {
			if (gOnCtrlTab((f & NSEventModifierFlagShift) != 0)) return nil;
		} else if (e.keyCode == 53 /* Escape */ && gOnEscape) {
			if (gOnEscape()) return nil;
		}
		return e;
	}];
}

bool MacControlKeyDown() {
	return ([NSEvent modifierFlags] & NSEventModifierFlagControl) != 0;
}

void MacTabSwitcher_Hide() {
	gOnHover = nullptr;
	gOnClick = nullptr;
	if (gPanel == nil) return;
	NSPanel* panel = gPanel;   // owned by the fade-out below until it finishes
	gPanel = nil;
	[gHighlight release]; gHighlight = nil;
	[gCards release]; gCards = nil;
	[gLabels release]; gLabels = nil;
	[panel.parentWindow removeChildWindow:panel];
	[NSAnimationContext runAnimationGroup:^(NSAnimationContext* ctx) {
		ctx.duration = 0.10;
		panel.animator.alphaValue = 0.0;
	} completionHandler:^{
		[panel orderOut:nil];
		[panel release];
	}];
}

#endif
