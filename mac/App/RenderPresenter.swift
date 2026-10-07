// `CedarLogic --render-presenter <dir>`: the presentation (Presenter.swift)
// checked without a second display. A small circuit in a real canvas, off
// screen, is put in one state after another: a selection with the pin under
// the pointer, a drag box, a wire being connected, a part being moved, one
// being placed, Tidy's preview, a stroke being drawn, Simulation View with
// and without projector mode, dark, and the Simple interface. For each, the
// editor's canvas and the presentation drawn at the editor's size must match
// (they share the camera, so the room sees what the editor shows); the
// presentation is then drawn at 1280x720 (and 1024x768) with the pointer
// and a click's ring, which must show. PNGs of each go in <dir>; PASS or
// FAIL is printed for each and the exit status is 1 after any failure.

import AppKit

@MainActor
enum RenderPresenter {
    static func runIfAsked() {
        let args = CommandLine.arguments
        guard let i = args.firstIndex(of: "--render-presenter"), i + 1 < args.count else { return }
        let dir = URL(fileURLWithPath: args[i + 1], isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let prefs = Prefs.shared
        let saved = (prefs.dark, prefs.projector, prefs.accent, prefs.showGrid)
        prefs.accent = brandAccent
        prefs.showGrid = true
        var failures = 0
        func report(_ ok: Bool, _ what: String) {
            print((ok ? "PASS  " : "FAIL  ") + what)
            if !ok { failures += 1 }
        }

        // Switches A (on) and B, an AND gate, a light; B not wired yet.
        let doc = CoreDocument()
        var strings: [UnsafeMutablePointer<CChar>] = []
        func c(_ s: String) -> UnsafePointer<CChar> { let p = strdup(s)!; strings.append(p); return UnsafePointer(p) }
        let gates = [CLBuildGate(gate: c("AA_TOGGLE"), x: 0, y: 4, label: nil, angle: 0),
                     CLBuildGate(gate: c("AA_TOGGLE"), x: 0, y: -4, label: nil, angle: 0),
                     CLBuildGate(gate: c("AA_AND2"), x: 12, y: 0, label: nil, angle: 0),
                     CLBuildGate(gate: c("GA_LED"), x: 22, y: 0, label: nil, angle: 0),
                     CLBuildGate(gate: c("AA_LABEL"), x: 10, y: 9, label: c("Presenter check"), angle: 0)]
        let wires = [CLBuildWire(from: 0, fromPin: c("OUT_0"), to: 2, toPin: c("IN_0")),
                     CLBuildWire(from: 2, fromPin: c("OUT"), to: 3, toPin: c("N_in0"))]
        _ = cl_edit_build(doc.handle, 0, gates, Int32(gates.count), wires, Int32(wires.count), "Build")
        strings.forEach { free($0) }
        cl_edit_select_none(doc.handle, 0)
        _ = cl_document_click(doc.handle, 0, 0, 4)
        for _ in 0..<40 { cl_document_step(doc.handle) }

        // The editor: a real canvas in a window off screen.
        let size = NSSize(width: 960, height: 600)
        let canvas = CanvasController()
        let ed = CircuitCanvasNSView(frame: NSRect(origin: .zero, size: size))
        ed.document = doc
        ed.page = 0
        ed.pageKey = doc.pageID(0)
        ed.controller = canvas
        ed.clMode = true
        canvas.view = ed
        canvas.page = 0
        canvas.attach(doc)
        canvas.setRunning(false)
        func offscreen(_ v: NSView) -> NSWindow {
            let w = NSWindow(contentRect: NSRect(origin: NSPoint(x: -6000, y: -6000), size: v.frame.size),
                             styleMask: [.borderless], backing: .buffered, defer: false)
            w.isReleasedWhenClosed = false
            w.contentView = v
            return w
        }
        let edWin = offscreen(ed)
        RunLoop.main.run(until: Date().addingTimeInterval(0.3))
        ed.zoomToFit()
        ed.zoom(by: 0.8, at: CGPoint(x: size.width / 2, y: size.height / 2))
        let upp = ed.unitsPerPoint

        // The presentation, the editor's size, then a projector's two shapes.
        func presenter(_ s: NSSize) -> (PresenterView, NSWindow) {
            let v = PresenterView(frame: NSRect(origin: .zero, size: s))
            v.source = canvas
            return (v, offscreen(v))
        }
        let same = presenter(size), wide = presenter(NSSize(width: 1280, height: 720))
        let square = presenter(NSSize(width: 1024, height: 768))

        func capture(_ v: NSView) -> NSBitmapImageRep? {
            v.needsDisplay = true
            guard let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(v.bounds.width * 2), pixelsHigh: Int(v.bounds.height * 2),
                                             bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                                             colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0) else { return nil }
            rep.size = v.bounds.size
            v.cacheDisplay(in: v.bounds, to: rep)
            return rep
        }
        func save(_ rep: NSBitmapImageRep?, _ name: String) {
            try? rep?.representation(using: .png, properties: [:])?.write(to: dir.appendingPathComponent(name + ".png"))
        }
        /// Pixels differing by more than a little in any channel.
        func differing(_ a: NSBitmapImageRep, _ b: NSBitmapImageRep) -> Int {
            guard a.pixelsWide == b.pixelsWide, a.pixelsHigh == b.pixelsHigh, let pa = a.bitmapData, let pb = b.bitmapData else { return Int.max }
            var n = 0
            for y in 0..<a.pixelsHigh {
                for x in 0..<a.pixelsWide {
                    let ia = y * a.bytesPerRow + x * 4, ib = y * b.bytesPerRow + x * 4
                    for k in 0..<3 where abs(Int(pa[ia + k]) - Int(pb[ib + k])) > 24 { n += 1; break }
                }
            }
            return n
        }
        func settle() { RunLoop.main.run(until: Date().addingTimeInterval(0.4)) }
        func reset() {
            doc.cancelGesture()
            if canvas.isFloating { canvas.cancelFloating() }
            if canvas.tidyActive { canvas.endTidy(keep: false) }
            cl_ink_cancel(doc.handle)
            canvas.drawing = false
            canvas.simView = false
            canvas.setRunning(false)
            prefs.projector = false
            ed.clMode = true
            cl_edit_select_none(doc.handle, 0)
            _ = cl_edit_hover_clear(doc.handle)
            ed.unitsPerPoint = upp
            ed.center(on: CGPoint(x: 11, y: 1))
        }

        /// One state: the editor and the presentation at its size must
        /// match; the projector-shaped ones get the pointer.
        func shoot(_ name: String, dark: Bool = false, pointer: MirrorPointer?, press: CGPoint? = nil, square sq: Bool = false,
                   setup: () -> Void) {
            reset()
            prefs.dark = dark
            setup()
            settle()
            let edRep = capture(ed)
            same.0.pointerOverride = .some(nil)
            same.0.pressOverride = nil
            let sameRep = capture(same.0)
            save(edRep, name + "-editor")
            if let edRep, let sameRep {
                let d = differing(edRep, sameRep)
                let total = edRep.pixelsWide * edRep.pixelsHigh
                report(d * 1000 <= total, "\(name): the presentation shows what the editor shows (\(d) of \(total) pixels differ)")
                if d * 1000 > total { save(sameRep, name + "-presenter-same") }
            } else {
                report(false, "\(name): couldn't draw")
            }
            for (v, suffix) in [(wide.0, "")] + (sq ? [(square.0, "-4x3")] : []) {
                v.pointerOverride = .some(nil)
                v.pressOverride = nil
                let bare = capture(v)
                v.pointerOverride = .some(pointer)
                v.pressOverride = press.map { ($0, 0.12) }
                let shown = capture(v)
                save(shown, name + "-presenter" + suffix)
                if pointer != nil || press != nil, let bare, let shown {
                    let d = differing(bare, shown)
                    report(d > 400, "\(name)\(suffix): the pointer shows (\(d) pixels)")
                }
                v.pointerOverride = .some(nil)
                v.pressOverride = nil
            }
        }

        shoot("edit-selected", pointer: MirrorPointer(world: CGPoint(x: 2, y: -4)), press: CGPoint(x: 12, y: 0), square: true) {
            _ = doc.press(page: 0, at: CGPoint(x: 12, y: 0), modifiers: [], unitsPerPoint: upp)
            doc.release(at: CGPoint(x: 12, y: 0))
            canvas.selectionChanged()
            _ = doc.hover(page: 0, at: CGPoint(x: 2, y: -4), unitsPerPoint: upp)
        }
        shoot("edit-dragbox", pointer: MirrorPointer(world: CGPoint(x: 15, y: -2), pressed: true)) {
            _ = doc.press(page: 0, at: CGPoint(x: -5, y: 11), modifiers: [], unitsPerPoint: upp)
            doc.drag(to: CGPoint(x: 15, y: -2))
        }
        shoot("edit-wire", pointer: MirrorPointer(world: CGPoint(x: 7.5, y: -2), pressed: true)) {
            _ = doc.press(page: 0, at: CGPoint(x: 2, y: -4), modifiers: [], unitsPerPoint: upp)
            doc.drag(to: CGPoint(x: 7.5, y: -2))
        }
        shoot("edit-moving", pointer: MirrorPointer(world: CGPoint(x: 24, y: 4), pressed: true)) {
            _ = doc.press(page: 0, at: CGPoint(x: 22, y: 0), modifiers: [], unitsPerPoint: upp)
            doc.drag(to: CGPoint(x: 24, y: 4))
        }
        // Put the light back as a finished move and its undo would.
        doc.release(at: CGPoint(x: 24, y: 4))
        _ = doc.undo()
        var placed = false
        shoot("edit-placing", pointer: MirrorPointer(world: CGPoint(x: 12, y: -10))) {
            placed = canvas.addGateFloating("AE_OR2", at: CGPoint(x: 12, y: -10)) && canvas.isFloating
        }
        report(placed, "edit-placing: a part on the pointer, as from the palette")
        shoot("edit-tidy", pointer: MirrorPointer(world: CGPoint(x: 5, y: 5))) { canvas.tidy() }
        shoot("draw-stroke", pointer: MirrorPointer(world: CGPoint(x: 18, y: -6), pressed: true, pen: true)) {
            canvas.drawing = true
            _ = cl_ink_begin(doc.handle, 0, Int32(CL_INK_PEN), "red", 0.25, upp)
            var pts = (0...24).map { k -> CLInkPoint in
                let t = Double(k) / 24
                return CLInkPoint(x: 2 + 16 * t, y: -6 + 3 * sin(t * .pi * 2), pressure: -1)
            }
            _ = cl_ink_add(doc.handle, &pts, Int32(pts.count))
        }
        shoot("edit-dark", dark: true, pointer: MirrorPointer(world: CGPoint(x: 12, y: 0))) {
            _ = doc.press(page: 0, at: CGPoint(x: 22, y: 0), modifiers: [], unitsPerPoint: upp)
            doc.release(at: CGPoint(x: 22, y: 0))
            canvas.selectionChanged()
        }
        shoot("sim", pointer: MirrorPointer(world: CGPoint(x: 0, y: 4), pressed: true), press: CGPoint(x: 0, y: 4)) {
            canvas.simView = true
            canvas.setRunning(false)
        }
        shoot("sim-projector", pointer: MirrorPointer(world: CGPoint(x: 0, y: 4)), square: true) {
            canvas.simView = true
            canvas.setRunning(false)
            prefs.projector = true
        }
        shoot("simple-interface", pointer: MirrorPointer(world: CGPoint(x: 12, y: 0))) {
            ed.clMode = false
            _ = doc.press(page: 0, at: CGPoint(x: 12, y: 0), modifiers: [], unitsPerPoint: upp)
            doc.release(at: CGPoint(x: 12, y: 0))
        }

        // Drawing with a mouse or trackpad, through the canvas's own event
        // handlers (synthetic events): a drag draws at once at the chosen
        // width with no pressure (a steady line), a tap leaves a dot, a
        // trackpad's Force Touch pressure doesn't change the width, and the
        // width keys pick the width.
        reset()
        let ink = InkSettings.shared
        let savedInk = (ink.tool, ink.penWidth, ink.penColor)
        ink.tool = .pen
        ink.penColor = "red"
        canvas.drawing = true
        func event(_ type: NSEvent.EventType, _ p: CGPoint, chars: String = "") -> NSEvent? {
            let w = ed.convert(p, to: nil)
            if type == .keyDown {
                return NSEvent.keyEvent(with: type, location: w, modifierFlags: [], timestamp: 0, windowNumber: edWin.windowNumber,
                                        context: nil, characters: chars, charactersIgnoringModifiers: chars, isARepeat: false, keyCode: 18)
            }
            return NSEvent.mouseEvent(with: type, location: w, modifierFlags: [], timestamp: 0, windowNumber: edWin.windowNumber,
                                      context: nil, eventNumber: 0, clickCount: 1, pressure: type == .leftMouseUp ? 0 : 1)
        }
        func strokes() -> Int { Int(cl_ink_stroke_count(doc.handle, 0)) }
        func lastStroke() -> String {
            let text = doc.saveText()
            guard let r = text.range(of: "(stroke ", options: .backwards) else { return "" }
            return String(text[r.lowerBound...].prefix(while: { $0 != ")" }))
        }
        func draw(from a: CGPoint, to b: CGPoint, moves: Int, force: Double? = nil) {
            if let e = event(.leftMouseDown, a) { ed.mouseDown(with: e) }
            if let force { ed.forcePressure = force }   // what a Force Touch trackpad's pressure events set
            for k in 1...max(1, moves) {
                let t = CGFloat(k) / CGFloat(max(1, moves))
                if moves > 0, let e = event(.leftMouseDragged, CGPoint(x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t)) { ed.mouseDragged(with: e) }
            }
            if let e = event(.leftMouseUp, b) { ed.mouseUp(with: e) }
        }
        let n0 = strokes()
        if let e = event(.keyDown, .zero, chars: "3") { ed.keyDown(with: e) }
        report(abs(ink.penWidth - InkSettings.penWidths[2]) < 1e-9, "drawing: the 3 key picks the thick line")
        draw(from: CGPoint(x: 200, y: 400), to: CGPoint(x: 420, y: 470), moves: 12)
        let s1 = lastStroke()
        report(strokes() == n0 + 1 && s1.hasPrefix("(stroke pen red 0.5 ") && s1.filter({ $0 == "\"" }).count == 2,
               "drawing: a mouse or trackpad drag draws one steady thick line, no pressure (\(s1.prefix(40))...)")
        if let e = event(.keyDown, .zero, chars: "2") { ed.keyDown(with: e) }
        draw(from: CGPoint(x: 600, y: 300), to: CGPoint(x: 600, y: 300), moves: 0)
        report(strokes() == n0 + 2 && lastStroke().hasPrefix("(stroke pen red 0.25 "), "drawing: a tap (no drag) leaves a dot at once")
        draw(from: CGPoint(x: 220, y: 200), to: CGPoint(x: 500, y: 160), moves: 10, force: 0.9)
        let s3 = lastStroke()
        report(strokes() == n0 + 3 && s3.hasPrefix("(stroke pen red 0.25 ") && s3.filter({ $0 == "\"" }).count == 2,
               "drawing: a Force Touch press keeps the chosen width")
        settle()
        save(capture(ed), "draw-trackpad-editor")
        canvas.drawing = false
        ink.tool = savedInk.0; ink.penWidth = savedInk.1; ink.penColor = savedInk.2

        // The look follows the editor: not Simulation View's dark one while editing.
        reset()
        prefs.dark = false
        settle()
        wide.0.pointerOverride = .some(nil)
        if let rep = capture(wide.0), let px = rep.colorAt(x: 4, y: 4)?.usingColorSpace(.deviceRGB) {
            report(px.brightnessComponent > 0.8, "editing in light: the presentation is light too")
        }
        reset()
        edWin.close(); same.1.close(); wide.1.close(); square.1.close()
        prefs.dark = saved.0; prefs.projector = saved.1; prefs.accent = saved.2; prefs.showGrid = saved.3
        print(failures == 0 ? "All presenter checks passed; pictures in \(dir.path)" : "\(failures) presenter check(s) failed")
        exit(failures == 0 ? 0 : 1)
    }
}
