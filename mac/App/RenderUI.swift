// `CedarLogic --render-ui <dir> [circuit.cdl]`: draws the CedarLogic
// interface's panels (toolbar styles, tabs, side panel, quick add, the
// shortcut list, Settings pages, the memory editor, the welcome) to PNGs,
// light and dark, then quits -- a way to look at them without clicking
// through the app (the wx app's --render-ui does the same).

import AppKit
import SwiftUI

@MainActor
enum RenderUI {
    static func runIfAsked() {
        let args = CommandLine.arguments
        guard let i = args.firstIndex(of: "--render-ui"), i + 1 < args.count else { return }
        let dir = URL(fileURLWithPath: args[i + 1], isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let doc: CoreDocument
        if i + 2 < args.count, let data = try? Data(contentsOf: URL(fileURLWithPath: args[i + 2])),
           let d = try? CoreDocument(data: data) {
            doc = d
        } else {
            doc = CoreDocument()
        }
        let canvas = CanvasController()
        canvas.drivesClock = false
        canvas.attach(doc)
        let prefs = Prefs.shared
        let savedDark = prefs.dark, savedStyle = prefs.toolbarStyle, savedHidden = prefs.toolbarHidden

        func save<V: View>(_ name: String, _ view: V, width: CGFloat, height: CGFloat? = nil) {
            let r = ImageRenderer(content: view.frame(width: width, height: height)
                .environment(\.colorScheme, prefs.dark ? .dark : .light))
            r.scale = 2
            guard let img = r.cgImage else { return }
            let rep = NSBitmapImageRep(cgImage: img)
            try? rep.representation(using: .png, properties: [:])?.write(to: dir.appendingPathComponent(name + ".png"))
        }

        // With a circuit: run it a while, then its oscilloscope as a timing diagram.
        if i + 2 < args.count {
            for _ in 0..<120 { cl_document_step(doc.handle) }
            let signals = (0..<Int(cl_scope_signal_count(doc.handle))).map {
                TimingDiagram.Signal(index: $0, name: String(cString: cl_scope_signal(doc.handle, Int32($0))))
            }
            let length = Int(cl_scope_length(doc.handle))
            if let png = TimingDiagram.pngData(document: doc, signals: signals, range: 0..<length, title: "Lab 5", color: true) {
                try? png.write(to: dir.appendingPathComponent("timing.png"))
            }
            if let pdf = TimingDiagram.pdfData(document: doc, signals: signals, range: max(0, length - 40)..<length, title: "Lab 5", color: false) {
                try? pdf.write(to: dir.appendingPathComponent("timing.pdf"))
            }
        }
        for dark in [false, true] {
            prefs.dark = dark
            let t = dark ? "dark" : "light"
            prefs.toolbarHidden = 0
            for style in ToolbarStyle.allCases {
                prefs.toolbarStyle = style
                save("toolbar-\(style.name.lowercased())-\(t)",
                     CLToolbar(document: doc, canvas: canvas, status: canvas.status, title: "counter", subtitle: "Page 1",
                               focusMode: .constant(false)), width: 1200, height: 52)
            }
            prefs.toolbarStyle = .seamless
            let split = SplitState()
            save("tabs-\(t)", CLTabStrip(document: doc, canvas: canvas, split: split, pane: 0, page: .constant(0), leftInset: 8),
                 width: 900, height: 36)
            save("status-\(t)", CLStatusBar(document: doc, canvas: canvas, status: canvas.status), width: 900, height: 22)
            save("sidepanel-\(t)", CLSidePanel(document: doc, canvas: canvas, page: 0), width: 220, height: 620)
            save("quickadd-\(t)", QuickAddView { _ in }, width: 520, height: 520)
            save("shortcuts-\(t)", ShortcutsSheet(canvas: canvas), width: 880, height: 620)
            save("simbar-\(t)", SimBar(document: doc, canvas: canvas, page: 0).padding(20).background(Color.black), width: 1100, height: 110)
            save("settings-general-\(t)", GeneralSettingsView().padding(28), width: 640)
            save("settings-appearance-\(t)", CLAppearanceSettings().padding(28), width: 640)
            save("settings-canvas-\(t)", CLCanvasSettings().padding(28), width: 640)
            save("settings-toolbar-\(t)", CLToolbarSettings().padding(28), width: 640)
            save("settings-shortcuts-\(t)", CLShortcutSettings().padding(28), width: 640, height: 520)
            save("welcome-\(t)", WelcomeView(), width: 780, height: 580)
            save("quit-\(t)", QuitConfirm.preview.padding(30).background(Color.gray), width: 480, height: 256)
            // A truth table with two lights, one with don't-cares.
            let f = try! FormulaParser.parse("F(A,B,C,D) = Σm(1,3,7,11,15) + d(0,2,5)\nG(A,B,C,D) = AB + AC + BC")
            let rows: [[Character]] = (0..<16).map { m in
                (0..<4).map { (m >> (3 - $0)) & 1 == 1 ? "1" : "0" } +
                f.functions.map { $0.values[m].map { $0 ? "1" : "0" } ?? "X" }
            }
            let table = TruthTable(names: ["A", "B", "C", "D", "F", "G"], inputs: 4, rows: rows)
            save("truthtable-\(t)", TruthTableView(table: table), width: 900, height: 700)
            // Wide: three inputs, six lights, some floating (the shape that overflowed).
            let wide: [[Character]] = (0..<8).map { m in
                (0..<3).map { (m >> (2 - $0)) & 1 == 1 ? "1" : "0" } + ["1", "1", "1", "Z", "1", "Z"].map { $0 == "1" && m < 2 ? "0" : $0 }
            }
            do {   // Each tab through a real view (scroll views don't draw in ImageRenderer).
                for tab in 0...2 {
                    UserDefaults.standard.set(tab, forKey: "cl.truthTab")
                    let host = NSHostingView(rootView: TruthTableView(table: TruthTable(names: ["A", "B", "C", "Y1", "Y2", "Y3", "Y4", "Y5", "Y6"], inputs: 3, rows: wide)))
                    host.appearance = NSAppearance(named: dark ? .darkAqua : .aqua)
                    host.frame = NSRect(origin: .zero, size: host.fittingSize)
                    host.layoutSubtreeIfNeeded()
                    print("truth table tab \(tab) \(t) size:", host.fittingSize)
                    if let rep = host.bitmapImageRepForCachingDisplay(in: host.bounds) {
                        host.cacheDisplay(in: host.bounds, to: rep)
                        try? rep.representation(using: .png, properties: [:])?.write(to: dir.appendingPathComponent("truthtable-tab\(tab)-\(t).png"))
                    }
                }
                UserDefaults.standard.removeObject(forKey: "cl.truthTab")
            }
            save("truthtable-wide-\(t)", TruthTableView(table: TruthTable(names: ["A", "B", "C", "Y1", "Y2", "Y3", "Y4", "Y5", "Y6"], inputs: 3, rows: wide)),
                 width: 900)
            save("buildformula-\(t)", BuildFormulaView(text: "S = A ^ B ^ Cin\nCout = AB + Cin(A ^ B)", canvas: canvas), width: 560, height: 560)
        }
        prefs.dark = savedDark
        prefs.toolbarStyle = savedStyle
        prefs.toolbarHidden = savedHidden
        exit(0)
    }
}
