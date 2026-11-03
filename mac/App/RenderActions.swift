// `CedarLogic --render-actions <dir>` (and part of --render-ui): the Actions
// rail in a whole circuit window drawn off screen, light and dark: open with
// a label showing, folded to its tab, folded and slid out over the canvas
// (as on hover), mid-drag (an item moved down, the others making room),
// folded by itself in a narrow window,
// in Simulation View, in the toolbar instead (at 1280 and 1440 wide), and
// the Customize sheet and Settings > Actions. Each window is laid out at
// once, so the PNGs show what a person would see.

import AppKit
import SwiftUI

@MainActor
enum RenderActions {
    static func runIfAsked() {
        let args = CommandLine.arguments
        guard let i = args.firstIndex(of: "--render-actions"), i + 1 < args.count else { return }
        run(URL(fileURLWithPath: args[i + 1], isDirectory: true))
        exit(0)
    }

    static func run(_ dir: URL) {
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let prefs = Prefs.shared
        let saved = (prefs.dark, prefs.toolbarStyle, prefs.actionsMode, prefs.actionsCollapsed, prefs.toolbarHidden)
        let doc = (try? CoreDocument(data: Data(Templates.builtIn[1].text.utf8))) ?? CoreDocument()
        for _ in 0..<40 { cl_document_step(doc.handle) }

        func shoot(_ name: String, width: CGFloat = 1440, height: CGFloat = 900, simView: Bool = false, label: ActionID? = nil) {
            ActionsRail.pictureLabel = label
            let canvas = CanvasController()
            canvas.attach(doc)
            let size = NSSize(width: width, height: height)
            let win = NSWindow(contentRect: NSRect(origin: NSPoint(x: -4000, y: -4000), size: size),
                               styleMask: [.titled, .fullSizeContentView, .resizable], backing: .buffered, defer: false)
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
            canvas.simView = simView
            canvas.view?.zoomToFit()
            RunLoop.main.run(until: Date().addingTimeInterval(1.4))
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
        func snap<V: View>(_ name: String, _ view: V) {
            // On the window's own background, as a sheet or Settings has it.
            let host = NSHostingView(rootView: view.background(Color(nsColor: .windowBackgroundColor)))
            host.appearance = NSAppearance(named: prefs.dark ? .darkAqua : .aqua)
            let size = host.fittingSize
            host.frame = NSRect(origin: .zero, size: size)
            let win = NSWindow(contentRect: host.frame, styleMask: [.titled], backing: .buffered, defer: false)
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

        // Every symbol named in the list is one macOS has (a missing one draws nothing).
        let missing = ActionRegistry.all.filter { NSImage(systemSymbolName: $0.symbol, accessibilityDescription: nil) == nil }
        print(missing.isEmpty ? "PASS every action's symbol exists" : "FAIL symbols missing: \(missing.map { "\($0.id.rawValue)=\($0.symbol)" })")

        prefs.toolbarStyle = .classic
        prefs.toolbarHidden = 0
        for dark in [false, true] {
            prefs.dark = dark
            let t = dark ? "dark" : "light"
            prefs.actionsMode = .onScreen
            prefs.actionsCollapsed = false
            shoot("rail-expanded-\(t)", label: .checkCircuit)
            shoot("rail-sim-\(t)", simView: true, label: .tidy)
            ActionsRail.pictureDrag = (prefs.railActions[1], 52)
            shoot("rail-drag-\(t)")
            ActionsRail.pictureDrag = nil
            prefs.actionsCollapsed = true
            shoot("rail-collapsed-\(t)")
            ActionsRail.pictureSlidOut = true
            shoot("rail-hover-open-\(t)", label: .tidy)
            ActionsRail.pictureSlidOut = false
            prefs.actionsCollapsed = false
            shoot("rail-narrow-\(t)", width: 860, height: 640)
            prefs.actionsMode = .toolbar
            shoot("rail-toolbar-\(t)")
            shoot("rail-toolbar-1280-\(t)", width: 1280, height: 800)
            prefs.actionsMode = .onScreen
            snap("rail-customize-\(t)", ActionsCustomizeSheet {})
            snap("settings-actions-\(t)", CLActionsSettings().padding(.horizontal, 28).padding(.vertical, 22).frame(width: 640))
        }
        (prefs.dark, prefs.toolbarStyle, prefs.actionsMode, prefs.actionsCollapsed, prefs.toolbarHidden) = saved
    }
}
