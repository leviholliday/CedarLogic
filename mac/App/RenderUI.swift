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
        // `--render-sync <dir>`: just Settings > Sync and its sheets (SyncRender).
        if let j = args.firstIndex(of: "--render-sync"), j + 1 < args.count {
            SyncRender.run(URL(fileURLWithPath: args[j + 1], isDirectory: true))
            exit(0)
        }
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
        TruthTableView.checksAtOnce = true   // drawn at once: no onAppear, no waiting
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
            // The same as a worksheet: the first half of the signals left blank for students.
            let worksheet = signals.enumerated().map { TimingDiagram.Signal(index: $1.index, name: $1.name, blank: $0 < (signals.count + 1) / 2) }
            if let png = TimingDiagram.pngData(document: doc, signals: worksheet, range: 0..<length, title: "Lab 5", color: true) {
                try? png.write(to: dir.appendingPathComponent("timing-worksheet.png"))
            }
            if let pdf = TimingDiagram.pdfData(document: doc, signals: worksheet, range: max(0, length - 40)..<length, title: "Lab 5", color: false) {
                try? pdf.write(to: dir.appendingPathComponent("timing-worksheet.pdf"))
            }
        }
        // The built-in templates, as files and pictures (a flip-flop's running
        // clock choice as -running).
        let builtIn = Templates.builtIn.map { (id: $0.id, text: $0.text) }
            + Templates.builtIn.compactMap { t in t.runningText.map { (id: t.id + "-running", text: $0) } }
        for t in builtIn {
            try? t.text.write(to: dir.appendingPathComponent("template-\(t.id).cdl"), atomically: true, encoding: .utf8)
            if let d = try? CoreDocument(data: Data(t.text.utf8)),
               let ctx = CGContext(data: nil, width: 1600, height: 1000, bitsPerComponent: 8, bytesPerRow: 0,
                                   space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) {
                ctx.setFillColor(.white); ctx.fill(CGRect(x: 0, y: 0, width: 1600, height: 1000))
                ctx.translateBy(x: 0, y: 1000); ctx.scaleBy(x: 2, y: -2)
                _ = cl_document_draw_fitted(d.handle, 0, ctx, 800, 500, 16, 2, Int32(CL_STYLE_LIGHT))
                if let img = ctx.makeImage() {
                    try? NSBitmapImageRep(cgImage: img).representation(using: .png, properties: [:])?
                        .write(to: dir.appendingPathComponent("template-\(t.id).png"))
                }
            }
        }
        // A saved part's tile: the counter template, selected and copied.
        if let d = try? CoreDocument(data: Data(Templates.builtIn[1].text.utf8)) {
            cl_edit_select_all(d.handle, 0)
            let text = d.copySelection(page: 0)
            let folder = FileManager.default.temporaryDirectory.appendingPathComponent("render-part", isDirectory: true)
            try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
            try? text.write(to: folder.appendingPathComponent("part.txt"), atomically: true, encoding: .utf8)
            let part = SavedPart(id: "render", name: "Counter", folder: folder)
            if let ctx = CGContext(data: nil, width: 240, height: 192, bitsPerComponent: 8, bytesPerRow: 0,
                                   space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) {
                ctx.setFillColor(.white); ctx.fill(CGRect(x: 0, y: 0, width: 240, height: 192))
                ctx.translateBy(x: 0, y: 192); ctx.scaleBy(x: 4, y: -4)
                MyParts.draw(part, in: ctx, width: 60, height: 48, scale: 4, dark: false)
                if let img = ctx.makeImage() {
                    try? NSBitmapImageRep(cgImage: img).representation(using: .png, properties: [:])?
                        .write(to: dir.appendingPathComponent("part-tile.png"))
                }
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
            if !dark {   // the welcome and tour are the brand look either way
                for pg in 0..<5 { save("welcome-\(pg)", WelcomeView(page: pg), width: 780, height: 580) }
                for pg in 0..<7 { save("whatsnew-\(pg)", WhatsNewView(page: pg), width: 820, height: 600) }
                for st in [0, 8, 10] {
                    TourModel.shared.step = st
                    save("tour-\(st)", TourCard(canvas: canvas).padding(30).background(Color(white: 0.9)), width: 420)
                }
                TourModel.shared.step = 0
            }
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
            do {   // Check: a full adder whose carry is wrong on the last row.
                let adder: [[Character]] = (0..<8).map { m in
                    (0..<3).map { (m >> (2 - $0)) & 1 == 1 ? "1" : "0" } +
                    [m.nonzeroBitCount % 2 == 1 ? "1" : "0", m.nonzeroBitCount >= 2 && m != 7 ? "1" : "0"]
                }
                let floating: [[Character]] = adder.map { r in Array(r.prefix(4)) + [r[0] == "0" ? "Z" : r[4]] }
                let shots: [(String, Int, String, [[Character]])] = [
                    ("wrong", 0, "S = A ^ B ^ Cin\nCout = AB + Cin(A ^ B)", adder),
                    ("matches", 0, "S(A,B,Cin) = Σm(1,2,4,7)\nCout(A,B,Cin) = Σm(3,5,6) + d(7)", adder),
                    ("table", 1, "A B Cin | S Cout\n0 0 0 | 0 0\n0 0 1 | 1 0\n0 1 0 | 1 0\n0 1 1 | 0 1\n1 0 0 | 1 0\n1 0 1 | 0 1\n1 1 0 | 0 1\n1 1 1 | 1 1", adder),
                    ("missing", 0, "S = A ^ B ^ Ci + D\nCarry = AB", adder),
                    ("floating", 0, "S = A ^ B ^ Cin\nCout = AB + Cin(A ^ B)", floating),
                    ("empty", 0, "", adder),
                ]
                UserDefaults.standard.set(TruthTableView.checkTab, forKey: "cl.truthTab")
                for (name, kind, text, rows) in shots {
                    let key = "render-ui-check-\(name)"
                    CheckMemory.save(key, .init(kind: kind, text: text, names: [:]))
                    var table = TruthTable(names: ["A", "B", "Cin", "S", "Cout"], inputs: 3, rows: rows)
                    table.checkKey = key
                    let host = NSHostingView(rootView: TruthTableView(table: table))
                    host.appearance = NSAppearance(named: dark ? .darkAqua : .aqua)
                    host.frame = NSRect(origin: .zero, size: CGSize(width: 720, height: host.fittingSize.height))
                    host.layoutSubtreeIfNeeded()
                    if let rep = host.bitmapImageRepForCachingDisplay(in: host.bounds) {
                        host.cacheDisplay(in: host.bounds, to: rep)
                        try? rep.representation(using: .png, properties: [:])?.write(to: dir.appendingPathComponent("check-\(name)-\(t).png"))
                    }
                    CheckMemory.save(key, nil)
                }
                UserDefaults.standard.removeObject(forKey: "cl.truthTab")
            }
            do {   // Check, clock pulse by clock pulse: a 3-bit J-K counter with one wire wrong (T2 from Q1
                   // alone), which has no switches and so no truth table; and a timing table on it.
                let counter = sequentialCheckCircuit()
                let shots: [(String, String)] = [
                    ("count-wrong", "0, 1, 2, 3, 4, 5, 6, 7, repeat"),
                    ("timing-wrong", "Pulse | Q2 Q1 Q0\n1 | 0 0 1\n2 | 0 1 0\n3 | 0 1 1\n4 | 1 0 0"),
                    ("states-wrong", "Q2 Q1 Q0 | Q2+ Q1+ Q0+\n0 0 0 | 0 0 1\n0 0 1 | 0 1 0\n0 1 0 | 0 1 1\n0 1 1 | 1 0 0\n1 0 0 | 1 0 1\n1 0 1 | 1 1 0\n1 1 0 | 1 1 1\n1 1 1 | 0 0 0"),
                ]
                for (name, text) in shots {
                    let key = "render-ui-check-\(name)"
                    CheckMemory.save(key, .init(kind: 0, text: text, names: [:]))
                    var table = TruthTable(checkOnly: counter, page: 0, problem: "A truth table needs at least one switch (an input) and one light (an output) on this page.")
                    table.checkKey = key
                    let host = NSHostingView(rootView: TruthTableView(table: table))
                    host.appearance = NSAppearance(named: dark ? .darkAqua : .aqua)
                    host.frame = NSRect(origin: .zero, size: CGSize(width: 720, height: host.fittingSize.height))
                    host.layoutSubtreeIfNeeded()
                    if let rep = host.bitmapImageRepForCachingDisplay(in: host.bounds) {
                        host.cacheDisplay(in: host.bounds, to: rep)
                        try? rep.representation(using: .png, properties: [:])?.write(to: dir.appendingPathComponent("check-\(name)-\(t).png"))
                    }
                    CheckMemory.save(key, nil)
                }
            }
            save("truthtable-wide-\(t)", TruthTableView(table: TruthTable(names: ["A", "B", "C", "Y1", "Y2", "Y3", "Y4", "Y5", "Y6"], inputs: 3, rows: wide)),
                 width: 900)
            // New from Template on the J-K flip-flop, with its Clock line
            // (through a real view: the list and the Clock control are AppKit).
            let picker = NSHostingView(rootView: TemplatePicker(selection: "builtin-ff-jk"))
            picker.appearance = NSAppearance(named: dark ? .darkAqua : .aqua)
            picker.frame = NSRect(x: 0, y: 0, width: 940, height: 620)
            picker.layoutSubtreeIfNeeded()
            if let rep = picker.bitmapImageRepForCachingDisplay(in: picker.bounds) {
                picker.cacheDisplay(in: picker.bounds, to: rep)
                try? rep.representation(using: .png, properties: [:])?.write(to: dir.appendingPathComponent("templates-\(t).png"))
            }
            save("buildformula-\(t)", BuildFormulaView(text: "S = A ^ B ^ Cin\nCout = AB + Cin(A ^ B)", canvas: canvas), width: 560, height: 560)
        }
        // The memory editor, on an 8x8 RAM with a few words in it (drawn
        // below, through a real view: its list is a scroll view).
        var ramShots: [(Bool, Int)] = []
        let ramDoc = CoreDocument()
        let ramCanvas = CanvasController()
        ramCanvas.drivesClock = false
        ramCanvas.attach(ramDoc)
        if let name = GateLibrary.categories.flatMap({ GateLibrary.gates(in: $0) }).first(where: { $0.caption == "8x8 RAM" })?.name,
           ramDoc.addGate(name, page: 0, at: .zero),
           let ram = (0..<50).first(where: { ramDoc.libraryName(ofGate: $0) == name }) {
            for a in 0..<20 { cl_ram_set(ramDoc.handle, ram, UInt(a), UInt(a * 7 % 256)) }
            for dark in [false, true] {
                prefs.dark = dark
                ramShots.append((dark, ram))
            }
        }
        // Your Circuits and a gate's settings sheet, through real views.
        func snap<V: View>(_ name: String, _ view: V, _ size: NSSize, dark: Bool) {
            let host = NSHostingView(rootView: view.frame(width: size.width, height: size.height))
            host.appearance = NSAppearance(named: dark ? .darkAqua : .aqua)
            host.frame = NSRect(origin: .zero, size: size)
            let win = NSWindow(contentRect: host.frame, styleMask: [.titled], backing: .buffered, defer: false)
            win.contentView = host
            host.layoutSubtreeIfNeeded()
            RunLoop.main.run(until: Date().addingTimeInterval(0.3))
            if let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(size.width * 2), pixelsHigh: Int(size.height * 2),
                                          bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                                          colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0) {
                rep.size = size
                host.cacheDisplay(in: host.bounds, to: rep)
                try? rep.representation(using: .png, properties: [:])?.write(to: dir.appendingPathComponent(name + ".png"))
            }
        }
        for (dark, ram) in ramShots {
            prefs.dark = dark
            snap("ram-\(dark ? "dark" : "light")", RamEditorView(document: ramDoc, gate: ram, canvas: ramCanvas), NSSize(width: 700, height: 580), dark: dark)
        }
        for dark in [false, true] {
            prefs.dark = dark
            let t = dark ? "dark" : "light"
            snap("library-\(t)", LibraryView(), NSSize(width: 600, height: 540), dark: dark)
            for style in ToolbarStyle.allCases {
                prefs.toolbarStyle = style
                snap("bar-\(style.name.lowercased())-\(t)", CLToolbar(document: doc, canvas: canvas, status: canvas.status, title: "Untitled Circuit", subtitle: "Page 1",
                                                        focusMode: .constant(false)), NSSize(width: 1200, height: 52), dark: dark)
            }
            prefs.toolbarStyle = savedStyle
            if i + 2 < args.count, let id = (0..<2000).first(where: { doc.libraryName(ofGate: $0) == "AA_REGISTER4" }) {
                if let pg = (0..<doc.pageCount).first(where: { _ = cl_edit_select_gate(doc.handle, Int32($0), id); return doc.singleSelectedGate(page: $0) == id }) { canvas.page = pg }
                canvas.selectionChanged()
                snap("inspector-\(t)", GateSettingsSheet(document: doc, controller: canvas) {}, NSSize(width: 420, height: 440), dark: dark)
            }
        }
        // Drawing and notes: the drawing bar, the notes pane, a circuit with a
        // drawing on it (the canvas, as in the window), and the presentation.
        let ink = inkSample()
        let inkCanvas = CanvasController()
        inkCanvas.drivesClock = false
        inkCanvas.attach(ink)
        let savedTool = InkSettings.shared.tool
        for dark in [false, true] {
            prefs.dark = dark
            let t = dark ? "dark" : "light"
            for tool in [InkTool.pen, .highlighter, .eraser] {
                InkSettings.shared.tool = tool
                snap("drawingbar-\(tool.rawValue)-\(t)", DrawingBar(canvas: inkCanvas, forceShown: true).padding(12),
                     NSSize(width: 620, height: 72), dark: dark)
            }
            InkSettings.shared.tool = .pen
            snap("notes-\(t)", NotesPanel(canvas: inkCanvas, document: ink), NSSize(width: 280, height: 420), dark: dark)
            snap("ink-canvas-\(t)", CLCanvasHost(document: ink, page: 0, controller: inkCanvas)
                    .overlay(alignment: .bottom) { DrawingBar(canvas: inkCanvas, forceShown: true).padding(.bottom, 14) },
                 NSSize(width: 960, height: 600), dark: dark)
        }
        InkSettings.shared.tool = savedTool
        for (name, pw, ph) in [("presenter", 1280.0, 720.0), ("presenter-4x3", 1024.0, 768.0)] {
            guard let ctx = CGContext(data: nil, width: Int(pw * 2), height: Int(ph * 2), bitsPerComponent: 8, bytesPerRow: 0,
                                      space: CGColorSpace(name: CGColorSpace.sRGB)!,
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { continue }
            ctx.translateBy(x: 0, y: CGFloat(ph * 2)); ctx.scaleBy(x: 2, y: -2)
            ctx.setFillColor(CLPalette(dark: true, simView: true).canvasCG)
            ctx.fill(CGRect(x: 0, y: 0, width: pw, height: ph))
            let box = (ink.fitBounds(ofPage: 0) ?? CGRect(x: -20, y: -15, width: 40, height: 30)).insetBy(dx: -3, dy: -3)
            let upp = max(box.width / pw, box.height / ph)
            PresenterView.drawPage(ctx, document: ink, controller: nil, page: 0,
                                   origin: CGPoint(x: box.midX - pw * upp / 2, y: box.midY + ph * upp / 2), upp: upp,
                                   size: CGSize(width: pw, height: ph), scale: 2)
            if let img = ctx.makeImage() {
                try? NSBitmapImageRep(cgImage: img).representation(using: .png, properties: [:])?
                    .write(to: dir.appendingPathComponent(name + ".png"))
            }
        }
        prefs.dark = savedDark
        prefs.toolbarStyle = savedStyle
        prefs.toolbarHidden = savedHidden
        SyncRender.run(dir)
        exit(0)
    }

    /// The 4-bit counter template with a drawing on it (a highlighter under
    /// the register, a pen circle and an arrow, a pressure stroke) and notes.
    static func inkSample() -> CoreDocument {
        let doc = (try? CoreDocument(data: Data(Templates.builtIn[1].text.utf8))) ?? CoreDocument()
        func stroke(_ tool: Int32, _ color: String, _ width: Double, _ pts: [(Double, Double, Double)]) {
            guard cl_ink_begin(doc.handle, 0, tool, color, width, 0.05) == Int32(CL_INK_OK) else { return }
            var arr = pts.map { CLInkPoint(x: $0.0, y: $0.1, pressure: $0.2) }
            cl_ink_add(doc.handle, &arr, Int32(arr.count))
            cl_ink_end(doc.handle)
        }
        var band: [(Double, Double, Double)] = []
        for k in 0...20 {
            let t: Double = Double(k)
            band.append((8 + t * 0.8, 6.5 + 0.08 * sin(t), -1))
        }
        stroke(Int32(CL_INK_HIGHLIGHTER), "yellow", 1.2, band)
        var ring: [(Double, Double, Double)] = []
        for k in 0...48 {
            let a: Double = Double(k) / 48 * 2 * Double.pi
            ring.append((32 + 7 * cos(a), 0.5 + 5.5 * sin(a), -1))
        }
        stroke(Int32(CL_INK_PEN), "red", 0.25, ring)
        var wave: [(Double, Double, Double)] = []
        for k in 0...30 {
            let t: Double = Double(k)
            let p: Double = 0.25 + 0.7 * abs(sin(t / 6))
            wave.append((40 + t * 0.4, 9 + 2 * sin(t / 4), p))
        }
        stroke(Int32(CL_INK_PEN), "blue", 0.25, wave)
        stroke(Int32(CL_INK_PEN), "ink", 0.25, [(46, 12, -1), (39.5, 5.5, -1), (41.5, 5.5, -1), (39.5, 5.5, -1), (39.5, 7.5, -1)])
        doc.notes = "Counts 0 to F, then wraps.\nThe clock drives the register's clock pin; enable and up are tied high.\n\nQuestion: what happens if count_up goes low?"
        return doc
    }

    /// A 3-bit up counter from J-K flip-flops, Q0 on top, with the AND gate
    /// for T2 left out (as tests/check-sequential's counter3-jk-wrong-wire).
    static func sequentialCheckCircuit() -> CoreDocument {
        let doc = CoreDocument()
        let parts: [(String, Double, Double, String?)] = [
            ("BB_CLOCK", 0, -24, nil), ("EE_VDD", 8, 24, nil),
            ("BE_JKFF_LOW", 24, 14, nil), ("BE_JKFF_LOW", 24, 0, nil), ("BE_JKFF_LOW", 24, -14, nil),
            ("GA_LED", 60, 10, nil), ("GA_LED", 60, 5, nil), ("GA_LED", 60, 0, nil),
            ("AA_LABEL", 63.2, 10, "Q0"), ("AA_LABEL", 63.2, 5, "Q1"), ("AA_LABEL", 63.2, 0, "Q2"),
        ]
        let links: [(Int, String, Int, String)] = [
            (0, "CLK", 2, "clock"), (0, "CLK", 3, "clock"), (0, "CLK", 4, "clock"),
            (1, "OUT_0", 2, "J"), (1, "OUT_0", 2, "K"), (2, "Q", 3, "J"), (2, "Q", 3, "K"),
            (3, "Q", 4, "J"), (3, "Q", 4, "K"),
            (2, "Q", 5, "N_in0"), (3, "Q", 6, "N_in0"), (4, "Q", 7, "N_in0"),
        ]
        var keep: [UnsafeMutablePointer<CChar>] = []
        func c(_ s: String) -> UnsafePointer<CChar> { let p = strdup(s)!; keep.append(p); return UnsafePointer(p) }
        let gates = parts.map { CLBuildGate(gate: c($0.0), x: $0.1, y: $0.2, label: $0.3.map(c), angle: 0) }
        let wires = links.map { CLBuildWire(from: Int32($0.0), fromPin: c($0.1), to: Int32($0.2), toPin: c($0.3)) }
        _ = cl_edit_build(doc.handle, 0, gates, Int32(gates.count), wires, Int32(wires.count), "Counter")
        keep.forEach { free($0) }
        return doc
    }
}
