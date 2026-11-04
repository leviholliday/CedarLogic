// `CedarLogic --render-actions <dir>` (and part of --render-ui): the Actions
// panel in a whole circuit window drawn off screen, light and dark: open with
// a label showing, folded to its tab, folded and slid out over the canvas
// (as on hover), mid-drag (an item moved down, the others making room),
// folded in focus mode (and slid out there), folded with the pointer at the
// edge (the tab peeking, in and out of focus mode), the Customize sheet searched,
// folded by itself in a narrow window, in Simulation View, turned off, and
// the Customize sheet and Settings > Actions; then all of --render-tour.
//
// `CedarLogic --render-tour <dir>`: Settings > Actions (on and off), the
// welcome's Actions panel page (on, off) and What's New's card for it, the
// welcome's offer, and every stop of Show Me Around in a whole window,
// light and dark, then in a small window (the panel folded) and with the
// panel off (its stop passed over). Each window is laid out at once, so the
// PNGs show what a person would see.

import AppKit
import SwiftUI

@MainActor
enum RenderActions {
    static func runIfAsked() {
        let args = CommandLine.arguments
        if let i = args.firstIndex(of: "--render-tour"), i + 1 < args.count {
            runTour(URL(fileURLWithPath: args[i + 1], isDirectory: true))
            exit(0)
        }
        guard let i = args.firstIndex(of: "--render-actions"), i + 1 < args.count else { return }
        run(URL(fileURLWithPath: args[i + 1], isDirectory: true))
        exit(0)
    }

    /// A whole circuit window off screen, laid out and drawn into `name`.png;
    /// `before` runs once it has settled (then it settles again).
    static func shoot(_ dir: URL, _ name: String, doc: CoreDocument, width: CGFloat = 1440, height: CGFloat = 900,
                      simView: Bool = false, focus: Bool = false, label: ActionID? = nil,
                      before: ((CanvasController) -> Void)? = nil) {
        let prefs = Prefs.shared
        ActionsRail.pictureLabel = label
        let canvas = CanvasController()
        canvas.attach(doc)
        let size = NSSize(width: width, height: height)
        let win = NSWindow(contentRect: NSRect(origin: NSPoint(x: -4000, y: -4000), size: size),
                           styleMask: [.titled, .fullSizeContentView, .resizable], backing: .buffered, defer: false)
        win.isReleasedWhenClosed = false
        win.titlebarAppearsTransparent = true
        win.titleVisibility = .hidden
        win.title = "Counter"
        let host = NSHostingView(rootView: CLLayout(document: doc, canvas: canvas, page: .constant(0))
            .frame(width: size.width, height: size.height))
        host.appearance = NSAppearance(named: prefs.dark ? .darkAqua : .aqua)
        win.contentView = host
        host.frame = NSRect(origin: .zero, size: size)
        host.layoutSubtreeIfNeeded()
        RunLoop.main.run(until: Date().addingTimeInterval(0.8))
        if focus { NotificationCenter.default.post(name: .clToggleFocusMode, object: canvas) }
        canvas.simView = simView
        canvas.view?.zoomToFit()
        RunLoop.main.run(until: Date().addingTimeInterval(1.4))
        if let before {
            before(canvas)
            RunLoop.main.run(until: Date().addingTimeInterval(1.0))
        }
        canvas.view?.needsDisplay = true
        RunLoop.main.run(until: Date().addingTimeInterval(0.3))
        if let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(size.width * 2), pixelsHigh: Int(size.height * 2),
                                      bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                                      colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0) {
            rep.size = size
            host.cacheDisplay(in: NSRect(origin: .zero, size: size), to: rep)
            try? rep.representation(using: .png, properties: [:])?.write(to: dir.appendingPathComponent(name + ".png"))
        }
        win.close()
        ActionsRail.pictureLabel = nil
    }

    /// A view on the window's own background (as a sheet or Settings has it), drawn into `name`.png.
    static func snap<V: View>(_ dir: URL, _ name: String, _ view: V) {
        let prefs = Prefs.shared
        let host = NSHostingView(rootView: view.background(Color(nsColor: .windowBackgroundColor)))
        host.appearance = NSAppearance(named: prefs.dark ? .darkAqua : .aqua)
        let size = host.fittingSize
        host.frame = NSRect(origin: .zero, size: size)
        let win = NSWindow(contentRect: host.frame, styleMask: [.titled], backing: .buffered, defer: false)
        win.isReleasedWhenClosed = false
        win.contentView = host
        host.layoutSubtreeIfNeeded()
        RunLoop.main.run(until: Date().addingTimeInterval(0.4))
        if let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(size.width * 2), pixelsHigh: Int(size.height * 2),
                                      bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                                      colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0) {
            rep.size = size
            host.cacheDisplay(in: host.bounds, to: rep)
            try? rep.representation(using: .png, properties: [:])?.write(to: dir.appendingPathComponent(name + ".png"))
        }
        win.close()
    }

    /// Settings > Actions, the welcome's page, What's New's card, the offer
    /// and Show Me Around's stops (see the top of this file).
    static func runTour(_ dir: URL) {
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let prefs = Prefs.shared
        let saved = (prefs.dark, prefs.toolbarStyle, prefs.actionsOn, prefs.actionsCollapsed, prefs.toolbarHidden, prefs.actionsHoverOpen)
        let doc = (try? CoreDocument(data: Data(Templates.builtIn[1].text.utf8))) ?? CoreDocument()
        for _ in 0..<40 { cl_document_step(doc.handle) }
        prefs.toolbarStyle = .classic
        prefs.toolbarHidden = 0
        prefs.actionsHoverOpen = true
        let around = ShowMeAround.shared
        for dark in [false, true] {
            prefs.dark = dark
            let t = dark ? "dark" : "light"
            prefs.actionsOn = true
            prefs.actionsCollapsed = false
            snap(dir, "about-\(t)", AboutPicture())
            snap(dir, "settings-actions-\(t)", CLActionsSettings().padding(.horizontal, 28).padding(.vertical, 22).frame(width: 640))
            prefs.actionsOn = false
            snap(dir, "settings-actions-off-\(t)", CLActionsSettings().padding(.horizontal, 28).padding(.vertical, 22).frame(width: 640))
            prefs.actionsOn = true
            // Every stop, in a 1440 by 900 window.
            var stops: [Int] = []
            shoot(dir, "tour-measure-\(t)", doc: doc) { c in around.preview(on: c, step: 0); stops = around.shown }
            try? FileManager.default.removeItem(at: dir.appendingPathComponent("tour-measure-\(t).png"))
            around.stop()
            print("tour: \(stops.count) stops (\(stops.map { ShowMeAround.steps[$0].spot.rawValue }))")
            for (n, i) in stops.enumerated() {
                shoot(dir, "tour-\(n + 1)-\(ShowMeAround.steps[i].spot.rawValue)-\(t)", doc: doc) { c in around.preview(on: c, step: i) }
                around.stop()
            }
            // The welcome's offer, over the window it closes onto.
            shoot(dir, "tour-offer-\(t)", doc: doc) { c in around.offer(on: c) }
            around.answerOffer(false)
            // A small window: the panel folds by itself; the bubbles still fit.
            for (n, i) in stops.enumerated() where [.palette, .actions, .run].contains(ShowMeAround.steps[i].spot) {
                shoot(dir, "tour-small-\(n + 1)-\(ShowMeAround.steps[i].spot.rawValue)-\(t)", doc: doc, width: 860, height: 600) { c in
                    around.preview(on: c, step: i)
                }
                around.stop()
            }
            // The panel off: its stop is passed over.
            prefs.actionsOn = false
            shoot(dir, "tour-paneloff-\(t)", doc: doc) { c in
                around.preview(on: c, step: 0)
                if let after = around.shown.first(where: { $0 > 2 }) { around.preview(on: c, step: after) }
                print("tour, panel off: \(around.shown.count) stops (\(around.shown.map { ShowMeAround.steps[$0].spot.rawValue }))")
            }
            around.stop()
            prefs.actionsOn = true
        }
        // The welcome and What's New are the brand look either way: once, dark.
        prefs.dark = true
        prefs.actionsOn = true
        snap(dir, "welcome-actions-on", WelcomeView(page: 2).frame(width: 780, height: 580))
        prefs.actionsHoverOpen = false
        snap(dir, "welcome-actions-hover-off", WelcomeView(page: 2).frame(width: 780, height: 580))
        prefs.actionsHoverOpen = true
        prefs.actionsOn = false
        snap(dir, "welcome-actions-off", WelcomeView(page: 2).frame(width: 780, height: 580))
        prefs.actionsOn = true
        snap(dir, "whatsnew-panel-card", WhatsNewView(page: 1, panelChoice: true).frame(width: 820, height: 600))
        snap(dir, "whatsnew-chapter-1", WhatsNewView(page: 1).frame(width: 820, height: 600))
        snap(dir, "whatsnew-finale", WhatsNewView(page: WhatsNewView.pageCount - 1).frame(width: 820, height: 600))
        (prefs.dark, prefs.toolbarStyle, prefs.actionsOn, prefs.actionsCollapsed, prefs.toolbarHidden, prefs.actionsHoverOpen) = saved
    }

    static func run(_ dir: URL) {
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let prefs = Prefs.shared
        let saved = (prefs.dark, prefs.toolbarStyle, prefs.actionsOn, prefs.actionsCollapsed, prefs.toolbarHidden)
        let doc = (try? CoreDocument(data: Data(Templates.builtIn[1].text.utf8))) ?? CoreDocument()
        for _ in 0..<40 { cl_document_step(doc.handle) }

        func shoot(_ name: String, width: CGFloat = 1440, height: CGFloat = 900, simView: Bool = false, focus: Bool = false, label: ActionID? = nil) {
            Self.shoot(dir, name, doc: doc, width: width, height: height, simView: simView, focus: focus, label: label)
        }
        func snap<V: View>(_ name: String, _ view: V) { Self.snap(dir, name, view) }

        // Every symbol named in the list is one macOS has (a missing one draws nothing).
        let missing = ActionRegistry.all.filter { NSImage(systemSymbolName: $0.symbol, accessibilityDescription: nil) == nil }
        print(missing.isEmpty ? "PASS every action's symbol exists" : "FAIL symbols missing: \(missing.map { "\($0.id.rawValue)=\($0.symbol)" })")

        prefs.toolbarStyle = .classic
        prefs.toolbarHidden = 0
        for dark in [false, true] {
            prefs.dark = dark
            let t = dark ? "dark" : "light"
            prefs.actionsOn = true
            prefs.actionsCollapsed = false
            shoot("rail-expanded-\(t)", label: .checkCircuit)
            shoot("rail-sim-\(t)", simView: true, label: .tidy)
            ActionsRail.pictureDrag = (prefs.railActions[1], 52)
            shoot("rail-drag-\(t)")
            ActionsRail.pictureDrag = nil
            prefs.actionsCollapsed = true
            shoot("rail-collapsed-\(t)")
            ActionsRail.picturePeek = true
            shoot("rail-collapsed-peek-\(t)")
            ActionsRail.picturePeek = false
            ActionsRail.pictureSlidOut = true
            shoot("rail-hover-open-\(t)", label: .tidy)
            ActionsRail.pictureSlidOut = false
            prefs.actionsCollapsed = false
            shoot("rail-focus-\(t)", focus: true)
            ActionsRail.picturePeek = true
            shoot("rail-focus-peek-\(t)", focus: true)
            ActionsRail.picturePeek = false
            ActionsRail.pictureSlidOut = true
            shoot("rail-focus-hover-open-\(t)", focus: true, label: .checkCircuit)
            ActionsRail.pictureSlidOut = false
            shoot("rail-narrow-\(t)", width: 860, height: 640)
            prefs.actionsOn = false
            shoot("rail-off-\(t)")
            prefs.actionsOn = true
            snap("rail-customize-\(t)", ActionsCustomizeSheet {})
            snap("rail-customize-search-\(t)", ActionsCustomizeSheet(query: "export") {})
        }
        (prefs.dark, prefs.toolbarStyle, prefs.actionsOn, prefs.actionsCollapsed, prefs.toolbarHidden) = saved
        runTour(dir)
    }
}

/// About, as the Mac's standard panel lays it out (the icon, the name, the
/// version, the credits), for --render-tour's picture of its credits.
private struct AboutPicture: View {
    var body: some View {
        let info = Bundle.main.infoDictionary ?? [:]
        VStack(spacing: 8) {
            Image(nsImage: NSApp.applicationIconImage).resizable().frame(width: 64, height: 64)
            Text("CedarLogic").font(.system(size: 14, weight: .bold))
            Text("Version \(info["CFBundleShortVersionString"] as? String ?? "") (\(info["CFBundleVersion"] as? String ?? ""))")
                .font(.system(size: 10)).foregroundStyle(.secondary)
            CreditsLabel().frame(width: 250)
        }
        .padding(.horizontal, 20).padding(.top, 22).padding(.bottom, 20)
        .frame(width: 290)
    }

    private struct CreditsLabel: NSViewRepresentable {
        func makeNSView(context: Context) -> NSTextField {
            let f = NSTextField(labelWithAttributedString: aboutCredits())
            f.allowsEditingTextAttributes = true
            f.isSelectable = true
            f.maximumNumberOfLines = 0
            f.preferredMaxLayoutWidth = 250
            return f
        }
        func updateNSView(_ f: NSTextField, context: Context) {}
    }
}
