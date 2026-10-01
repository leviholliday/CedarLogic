// `CedarLogic --render-window <dir>`: a full CedarLogic window, drawn off
// screen, for the website and the README: a full adder (built from its
// formula, a switch or two on), in light, dark and Simulation View.

import AppKit
import SwiftUI

@MainActor
enum RenderWindow {
    static func runIfAsked() {
        let args = CommandLine.arguments
        guard let i = args.firstIndex(of: "--render-window"), i + 1 < args.count else { return }
        let dir = URL(fileURLWithPath: args[i + 1], isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let prefs = Prefs.shared
        let saved = (prefs.dark, prefs.toolbarStyle, prefs.accent, prefs.toolbarHidden)

        // The circuit: a full adder, as written, switches A and Cin on.
        let doc = CoreDocument()
        guard let formulas = try? FormulaParser.parse("S = A ^ B ^ Cin\nCout = AB + Cin(A ^ B)") else { exit(1) }
        let plan = FormulaCircuit.plan(formulas, shape: .asWritten, style: .any, twoInputOnly: false)
        var strings: [UnsafeMutablePointer<CChar>] = []
        func c(_ s: String) -> UnsafePointer<CChar> { let p = strdup(s)!; strings.append(p); return UnsafePointer(p) }
        let gates = plan.parts.map { CLBuildGate(gate: c($0.gate), x: $0.x, y: $0.y, label: $0.label.map(c)) }
        let wires = plan.wires.map { CLBuildWire(from: Int32($0.from), fromPin: c($0.fromPin), to: Int32($0.to), toPin: c($0.toPin)) }
        _ = cl_edit_build(doc.handle, 0, gates, Int32(gates.count), wires, Int32(wires.count), "Build")
        strings.forEach { free($0) }
        cl_edit_select_none(doc.handle, 0)
        doc.renamePage(0, to: "Full Adder")
        // Switch on the first and last switches (A and Cin) with a click on each.
        let switches = plan.parts.filter { $0.gate == "AA_TOGGLE" }
        for s in [switches.first, switches.last].compactMap({ $0 }) {
            _ = doc.press(page: 0, at: CGPoint(x: s.x, y: s.y), modifiers: [], unitsPerPoint: 0.05)
            doc.release(at: CGPoint(x: s.x, y: s.y))
        }
        cl_edit_select_none(doc.handle, 0)
        for _ in 0..<60 { cl_document_step(doc.handle) }

        func shoot(_ name: String, dark: Bool, simView: Bool) {
            prefs.dark = dark
            prefs.toolbarStyle = .classic
            prefs.accent = brandAccent
            prefs.toolbarHidden = 0
            let canvas = CanvasController()
            canvas.attach(doc)
            let size = NSSize(width: 1440, height: 900)
            let win = NSWindow(contentRect: NSRect(origin: NSPoint(x: -4000, y: -4000), size: size),
                               styleMask: [.titled, .fullSizeContentView, .resizable], backing: .buffered, defer: false)
            win.titlebarAppearsTransparent = true
            win.titleVisibility = .hidden
            win.title = "Full Adder"
            let host = NSHostingView(rootView: CLLayout(document: doc, canvas: canvas, page: .constant(0))
                .frame(width: size.width, height: size.height))
            host.appearance = NSAppearance(named: dark ? .darkAqua : .aqua)
            win.contentView = host
            host.frame = NSRect(origin: .zero, size: size)
            host.layoutSubtreeIfNeeded()
            RunLoop.main.run(until: Date().addingTimeInterval(0.8))
            canvas.simView = simView
            canvas.view?.zoomToFit()
            RunLoop.main.run(until: Date().addingTimeInterval(1.2))
            canvas.view?.needsDisplay = true
            RunLoop.main.run(until: Date().addingTimeInterval(0.3))
            // At 2x, as on a Retina screen.
            if let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(size.width * 2), pixelsHigh: Int(size.height * 2),
                                          bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                                          colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0) {
                rep.size = size
                host.cacheDisplay(in: NSRect(origin: .zero, size: size), to: rep)
                try? rep.representation(using: .png, properties: [:])?.write(to: dir.appendingPathComponent(name + ".png"))
            }
            win.close()
        }
        // Its truth table, each tab, through a real view (at 2x).
        func snap<V: View>(_ name: String, _ view: V, dark: Bool) {
            let host = NSHostingView(rootView: view)
            host.appearance = NSAppearance(named: dark ? .darkAqua : .aqua)
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
        }
        prefs.dark = true
        prefs.accent = brandAccent
        var err = ""
        if let table = TruthTable(document: doc, page: 0, error: &err) {
            for tab in 0...2 {
                UserDefaults.standard.set(tab, forKey: "cl.truthTab")
                snap("tt-\(tab)", TruthTableView(table: table), dark: true)
            }
            UserDefaults.standard.removeObject(forKey: "cl.truthTab")
        }
        shoot("window-dark", dark: true, simView: false)
        shoot("window-light", dark: false, simView: false)
        shoot("window-sim", dark: true, simView: true)
        prefs.dark = saved.0; prefs.toolbarStyle = saved.1; prefs.accent = saved.2; prefs.toolbarHidden = saved.3
        exit(0)
    }
}
