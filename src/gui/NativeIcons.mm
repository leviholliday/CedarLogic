/*****************************************************************************
   Project: CEDAR Logic Simulator
   NativeIcons: macOS native SF Symbol toolbar icons
*****************************************************************************/

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#include "wx/bitmap.h"
#include "wx/toolbar.h"
#include "wx/frame.h"
#include "wx/image.h"
#include "NativeIcons.h"

wxBitmap NativeIcon_GetSFSymbol(const char* symbolName, int pointSize) {
    @autoreleasepool {
        NSString *name = [NSString stringWithUTF8String:symbolName];
        NSImage *symbol = [NSImage imageWithSystemSymbolName:name
                                    accessibilityDescription:nil];
        if (!symbol) return wxNullBitmap;

        NSImageSymbolConfiguration *config = [NSImageSymbolConfiguration
            configurationWithPointSize:(CGFloat)pointSize
            weight:NSFontWeightRegular
            scale:NSImageSymbolScaleMedium];
        symbol = [symbol imageWithSymbolConfiguration:config];

        return wxBitmap((__bridge WXImage)symbol);
    }
}

void NativeIcon_SetToolbarSFSymbol(wxToolBar* toolbar, int toolId,
                                    const char* symbolName, int pointSize) {
    @autoreleasepool {
        NSString *name = [NSString stringWithUTF8String:symbolName];
        NSImage *symbol = [NSImage imageWithSystemSymbolName:name
                                    accessibilityDescription:nil];
        if (!symbol) return;

        NSImageSymbolConfiguration *config = [NSImageSymbolConfiguration
            configurationWithPointSize:(CGFloat)pointSize
            weight:NSFontWeightRegular
            scale:NSImageSymbolScaleMedium];
        symbol = [symbol imageWithSymbolConfiguration:config];

        // wxWidgets uses the wxToolBarTool pointer address (as a string) for
        // the NSToolbarItem identifier. Find the tool, format its address,
        // and match against NSToolbar items.
        wxToolBarToolBase* tool = toolbar->FindById(toolId);
        if (!tool) return;

        NSString *identifier = [NSString stringWithFormat:@"%ld", (long)tool];

        NSWindow *window = [((NSView*)toolbar->GetHandle()) window];
        NSToolbar *nstoolbar = [window toolbar];
        if (!nstoolbar) return;

        for (NSToolbarItem *item in [nstoolbar items]) {
            if ([[item itemIdentifier] isEqualToString:identifier]) {
                [item setImage:symbol];
                return;
            }
        }
    }
}

void NativeIcon_ConfigureEmbeddedToggleTool(wxToolBar* toolbar, int toolId,
                                             const char* normalSymbol,
                                             const char* alternateSymbol,
                                             int pointSize) {
    @autoreleasepool {
        NSImage* (^makeSFSymbol)(const char*) = ^NSImage*(const char* symName) {
            NSString *name = [NSString stringWithUTF8String:symName];
            NSImage *symbol = [NSImage imageWithSystemSymbolName:name
                                        accessibilityDescription:nil];
            if (!symbol) return nil;
            NSImageSymbolConfiguration *config = [NSImageSymbolConfiguration
                configurationWithPointSize:(CGFloat)pointSize
                weight:NSFontWeightRegular
                scale:NSImageSymbolScaleMedium];
            return [symbol imageWithSymbolConfiguration:config];
        };

        NSImage* normalImage = makeSFSymbol(normalSymbol);
        NSImage* altImage = makeSFSymbol(alternateSymbol);
        if (!normalImage || !altImage) return;

        // For embedded wxToolBar, each tool is an NSButton subview.
        int toolPos = toolbar->GetToolPos(toolId);
        if (toolPos == wxNOT_FOUND) return;

        int buttonIndex = 0;
        for (int i = 0; i < toolPos; i++) {
            const wxToolBarToolBase* t = toolbar->GetToolByPos(i);
            if (t && !t->IsSeparator()) buttonIndex++;
        }

        NSView* tbView = (NSView*)toolbar->GetHandle();
        int count = 0;
        for (NSView* subview in [tbView subviews]) {
            if ([subview isKindOfClass:[NSButton class]]) {
                if (count == buttonIndex) {
                    NSButton* button = (NSButton*)subview;
                    [button setImage:normalImage];
                    [button setAlternateImage:altImage];
                    return;
                }
                count++;
            }
        }
    }
}

void NativeWindow_ConfigureTitleBar(wxFrame* frame) {
    @autoreleasepool {
        NSView* view = (NSView*)frame->GetHandle();
        if (!view) return;

        NSWindow *window = [view window];
        if (!window) return;

        // Merge the title bar and toolbar into one unified area
        window.styleMask |= NSWindowStyleMaskUnifiedTitleAndToolbar;

        // Keep the title visible alongside toolbar items
        window.titleVisibility = NSWindowTitleVisible;

        // Use the modern unified toolbar style (macOS 11+)
        if (@available(macOS 11.0, *)) {
            window.toolbarStyle = NSWindowToolbarStyleUnified;
        }
    }
}

wxBitmap NativeIcon_TintedSFSymbol(const char* symbolName, double pointSize,
                                   unsigned char r, unsigned char g, unsigned char b,
                                   unsigned char a, double scale) {
    @autoreleasepool {
        NSImage* base = [NSImage imageWithSystemSymbolName:[NSString stringWithUTF8String:symbolName]
                                  accessibilityDescription:nil];
        if (base == nil) return wxNullBitmap;
        NSImageSymbolConfiguration* cfg =
            [NSImageSymbolConfiguration configurationWithPointSize:pointSize weight:NSFontWeightRegular];
        NSImage* img = [base imageWithSymbolConfiguration:cfg];
        const NSSize sz = img.size;
        const int pw = (int)ceil(sz.width * scale), ph = (int)ceil(sz.height * scale);
        if (pw <= 0 || ph <= 0) return wxNullBitmap;

        // Draw the symbol, then paint the colour over just its pixels.
        NSBitmapImageRep* rep = [[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
            pixelsWide:pw pixelsHigh:ph bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
            colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:pw * 4 bitsPerPixel:32] autorelease];
        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext:[NSGraphicsContext graphicsContextWithBitmapImageRep:rep]];
        [img drawInRect:NSMakeRect(0, 0, pw, ph)];
        [[NSColor colorWithDeviceRed:r / 255.0 green:g / 255.0 blue:b / 255.0 alpha:1.0] set];
        NSRectFillUsingOperation(NSMakeRect(0, 0, pw, ph), NSCompositingOperationSourceAtop);
        [NSGraphicsContext restoreGraphicsState];

        // Into a wxImage, un-premultiplying, with the requested opacity.
        wxImage out(pw, ph);
        out.InitAlpha();
        const unsigned char* px = [rep bitmapData];
        unsigned char* rgb = out.GetData();
        unsigned char* alpha = out.GetAlpha();
        for (int i = 0; i < pw * ph; i++) {
            const unsigned char pa = px[i * 4 + 3];
            rgb[i * 3 + 0] = r; rgb[i * 3 + 1] = g; rgb[i * 3 + 2] = b;
            alpha[i] = (unsigned char)(pa * a / 255);
        }
        return wxBitmap(out, -1, scale);
    }
}

#endif // __APPLE__
