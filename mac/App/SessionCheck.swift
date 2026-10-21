// `CedarLogic --session-check [actions] [seed] [start]`: a long working
// session in one circuit window, headless, on a new circuit or one started
// from `start` (a .cdl file, or template:<id>: see startText). The real window (CircuitWindow, so the
// toolbar, tabs, palette, minimap, status bar and canvas all update) is put
// in an NSWindow that's never ordered in, over a document that autosaves in
// place into Your Circuits, the way a circuit window does. Then a few
// thousand edits a few hundredths of a second apart, as a student would make
// them over ten minutes or more: parts placed, moved, wired, deleted,
// undone and redone, tabs opened, switched and closed, a split opened and
// closed and used, Simulation View and the scope on and off, the clock run
// and stepped -- with a pause every so often for autosave.
//
// Checked all along: an edit always leaves the window edited (or autosave
// skips it: the 500th-undo bug was one way), each pause leaves it saved with
// the file on disk the circuit as it is, the engine's undo steps and the
// window's stay in step, each side shows a page that exists, Version History
// keeps a version after a break (the break is made by backdating the file,
// not by waiting), memory doesn't grow without bound, and closing the
// window lets the controller, its clock and the circuit go -- and the
// engine's count of open documents (cl_document_live_count) comes back to
// where it was before the window opened.
//
// It writes into Your Circuits, so it only runs with a home folder of its
// own (CFFIXED_USER_HOME, as mac/Tools/session-check.sh sets): never in
// yours. PASS or FAIL for each; exit 1 on a failure.

import AppKit
import SwiftUI
import UniformTypeIdentifiers

/// A circuit file, as macOS's document handling sees one: saved in place,
/// as the engine writes it.
private final class SessionFile: NSDocument {
    let core: CoreDocument
    init(core: CoreDocument) { self.core = core; super.init() }
    override class var autosavesInPlace: Bool { true }
    override class var readableTypes: [String] { [UTType.cedarLogicCircuit.identifier] }
    override class var writableTypes: [String] { [UTType.cedarLogicCircuit.identifier] }
    override class func isNativeType(_ type: String) -> Bool { true }
    override func data(ofType typeName: String) throws -> Data { Data(core.saveText().utf8) }
    override func read(from data: Data, ofType typeName: String) throws {}
}

/// The window hands out its document's undo manager, as a document window does.
private final class SessionWindowDelegate: NSObject, NSWindowDelegate {
    weak var file: NSDocument?
    func windowWillReturnUndoManager(_ window: NSWindow) -> UndoManager? { file?.undoManager }
}

@MainActor
enum SessionCheck {
    static func runIfAsked() {
        let args = CommandLine.arguments
        guard let i = args.firstIndex(of: "--session-check") else { return }
        let actions = args.count > i + 1 ? Int(args[i + 1]) ?? 2000 : 2000
        let seed = args.count > i + 2 ? UInt64(args[i + 2]) ?? 1 : 1
        let start = args.count > i + 3 && !args[i + 3].hasPrefix("-") ? args[i + 3] : nil
        setvbuf(stdout, nil, _IOLBF, 0)
        guard isolatedHome() else {
            print("session-check: needs a home folder of its own (CFFIXED_USER_HOME); run mac/Tools/session-check.sh")
            exit(2)
        }
        exit(run(actions: actions, seed: seed, start: start) ? 0 : 1)
    }

    /// The circuit a session starts from, as Your Circuits would hold it:
    /// a file (as importing it makes it: opened by the engine and written
    /// back), `template:<id>` (a built-in template; `:running` or `:pulse`
    /// after it for that Clock choice), or nil for a new, empty one.
    static func startText(_ start: String?) -> String? {
        guard let start else { return CoreDocument().saveText() }
        if start.hasPrefix("template:") {
            let parts = start.dropFirst("template:".count).split(separator: ":").map(String.init)
            guard let id = parts.first, let t = Templates.template(id: id) else { return nil }
            let clock = parts.count > 1 ? TemplateClock(rawValue: parts[1]) ?? .manual : .manual
            return t.with(clock: clock).text
        }
        guard let data = try? Data(contentsOf: URL(fileURLWithPath: start)), let doc = try? CoreDocument(data: data) else { return nil }
        return doc.saveText()
    }

    /// Your Circuits is inside CFFIXED_USER_HOME, so nothing of the owner's is touched.
    static func isolatedHome() -> Bool {
        guard let home = ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"], !home.isEmpty else { return false }
        let h = URL(fileURLWithPath: home).standardizedFileURL.path
        return Library.root.standardizedFileURL.path.hasPrefix(h + "/")
    }

    /// The memory the system counts against the app (Activity Monitor's).
    static func footprintMB() -> Double {
        var info = task_vm_info_data_t()
        var count = mach_msg_type_number_t(MemoryLayout<task_vm_info_data_t>.size / MemoryLayout<natural_t>.size)
        let r = withUnsafeMutablePointer(to: &info) {
            $0.withMemoryRebound(to: integer_t.self, capacity: Int(count)) { task_info(mach_task_self_, task_flavor_t(TASK_VM_INFO), $0, &count) }
        }
        return r == KERN_SUCCESS ? Double(info.phys_footprint) / 1_048_576 : 0
    }

    struct Random {
        var state: UInt64
        mutating func next() -> UInt64 {
            state &+= 0x9E3779B97F4A7C15
            var z = state
            z = (z ^ (z >> 30)) &* 0xBF58476D1CE4E5B9
            z = (z ^ (z >> 27)) &* 0x94D049BB133111EB
            return z ^ (z >> 31)
        }
        mutating func int(_ n: Int) -> Int { Int(next() % UInt64(max(n, 1))) }
        mutating func unit() -> Double { Double(next() % 1_000_000) / 1_000_000 }
    }

    static func run(actions: Int, seed: UInt64, start: String? = nil) -> Bool {
        var failures = 0
        func check(_ ok: Bool, _ what: String) {
            print((ok ? "PASS " : "FAIL ") + what)
            if !ok { failures += 1 }
        }
        func turn(_ seconds: Double) { RunLoop.main.run(until: Date().addingTimeInterval(seconds)) }

        splitRunChecks(check)

        // No welcome, What's New or tour on top (they're windows of their own).
        Prefs.shared.hasSeenWelcome = true
        WhatsNew.markSeen()
        let prefs = Prefs.shared
        let savedOpenReplaces = prefs.openReplaces
        prefs.openReplaces = false

        // A circuit in Your Circuits, open in a window.
        guard let text = autoreleasepool(invoking: { startText(start) }) else {
            print("FAIL couldn't read the circuit to start from: \(start ?? "")")
            return false
        }
        guard let item = try? Library.create(named: "Session check", text: text) else {
            print("FAIL couldn't make a circuit in Your Circuits at \(Library.root.path)")
            return false
        }
        if let start { print("session: starting from \(start)") }
        // The engine's open documents now; closing the window must bring
        // the count back here (any document the window or its sheets,
        // thumbnails, versions or checks opened, and didn't close, shows).
        let documentsAtStart = Int(cl_document_live_count())
        var documentsOpen = 0
        weak var goneCanvas: CanvasController?
        weak var goneCore: CoreDocument?
        weak var goneFile: NSDocument?
        weak var goneWindow: NSWindow?
        weak var goneHost: NSView?
        var editsChecked = 0, editsLost = 0, firstLost = -1, kind = "", lostWhat = ""
        var pausesChecked = 0, pausesUnsaved = 0, pausesStale = 0, firstStale = -1, firstUnsaved = -1
        var stepMismatch = 0, firstMismatch = -1
        var badPage = 0, firstBadPage = -1
        var maxSteps = 0, splits = 0, tabsMade = 0, tabsClosed = 0, undos = 0, redos = 0
        var versionAfterBreak: Bool?
        var memory: [Double] = []
        let delegate = SessionWindowDelegate()

        autoreleasepool { MainActor.assumeIsolated {
            guard let data = try? Data(contentsOf: item.circuit), let core = try? CoreDocument(data: data) else {
                print("FAIL couldn't open the circuit just made")
                failures += 1
                return
            }
            let file = SessionFile(core: core)
            file.fileURL = item.circuit
            file.fileType = UTType.cedarLogicCircuit.identifier
            file.fileModificationDate = (try? item.circuit.resourceValues(forKeys: [.contentModificationDateKey]))?.contentModificationDate
            delegate.file = file
            let size = NSSize(width: 1280, height: 800)
            let win = NSWindow(contentRect: NSRect(origin: NSPoint(x: -6000, y: -6000), size: size),
                               styleMask: [.titled, .closable, .miniaturizable, .resizable, .fullSizeContentView],
                               backing: .buffered, defer: false)
            win.isReleasedWhenClosed = false
            win.delegate = delegate
            let wc = NSWindowController(window: win)
            file.addWindowController(wc)
            NSDocumentController.shared.addDocument(file)
            let host = NSHostingView(rootView: CircuitWindow(document: core).environmentObject(LookStore.shared))
            host.frame = NSRect(origin: .zero, size: size)
            win.contentView = host
            host.layoutSubtreeIfNeeded()
            turn(1.0)
            documentsOpen = Int(cl_document_live_count())

            @MainActor func findCanvas(_ v: NSView) -> CircuitCanvasNSView? {
                if let c = v as? CircuitCanvasNSView, c.controller?.drivesClock == true { return c }
                for s in v.subviews { if let c = findCanvas(s) { return c } }
                return nil
            }
            guard let canvasView = findCanvas(host), let canvas = canvasView.controller else {
                print("FAIL the window has no canvas")
                failures += 1
                return
            }
            check(canvas.undoManager === file.undoManager, "the canvas uses the document's undo manager")
            if canvas.undoManager !== file.undoManager { canvas.undoManager = file.undoManager }
            goneCanvas = canvas; goneCore = core; goneFile = file; goneWindow = win; goneHost = host

            var rng = Random(state: seed)
            // A page fuller than this is cleared (Select All, Delete): past
            // what a circuit started from a file already has on a page.
            let crowded = max(36, (0..<core.pageCount).map { Int(cl_document_gate_count(core.handle, Int32($0))) }.max() ?? 0) + 12
            let parts = ["AA_AND2", "AE_OR2", "AA_INVERTER", "AI_XOR2", "AA_TOGGLE", "GA_LED", "BB_CLOCK", "AA_DFF", "AA_LABEL"]
            /// The side edits go to: the first, or the second while a split has it.
            @MainActor func side() -> CanvasController { canvas.routed }
            @MainActor func gates(_ c: CanvasController) -> Int { Int(cl_document_gate_count(core.handle, Int32(c.page))) }
            @MainActor func spot(_ c: CanvasController) -> CGPoint {
                let b = core.bounds(ofPage: c.page) ?? CGRect(x: -20, y: -20, width: 40, height: 40)
                return CGPoint(x: b.minX - 6 + rng.unit() * (b.width + 12), y: b.minY - 6 + rng.unit() * (b.height + 12))
            }
            @MainActor func checkPages(_ at: Int) {
                let n = core.pageCount
                var ok = canvas.page >= 0 && canvas.page < n
                if let p = canvas.partner, p.view != nil { ok = ok && p.page >= 0 && p.page < n }
                if !ok { badPage += 1; if firstBadPage < 0 { firstBadPage = at } }
            }
            /// The circuit changed: the window must be edited now, or autosave
            /// and closing would leave it unsaved. (Not the engine's own
            /// flag: an undo back to the saved circuit leaves that on, and
            /// the window rightly unedited.)
            @MainActor func checkEdited(_ at: Int, from before: String) {
                let now = shape(core.shareText(flags: 0))
                guard now != before else { return }
                editsChecked += 1
                // Unedited is right if autosave already wrote this very
                // circuit (it can land between an edit and this look).
                if !file.isDocumentEdited, (try? String(contentsOf: item.circuit, encoding: .utf8)).map(shape) != now {
                    editsLost += 1
                    if firstLost < 0 {
                        firstLost = at
                        lostWhat = "\(kind); \(core.undoCount) steps, engine \(core.isEdited ? "edited" : "unedited"), \(core.canRedo ? "with" : "no") redo"
                    }
                }
            }
            @MainActor func pause(_ at: Int) {
                turn(2.6)   // autosave comes 2 s after the last change
                pausesChecked += 1
                if file.isDocumentEdited { pausesUnsaved += 1; if firstUnsaved < 0 { firstUnsaved = at } }
                let disk = (try? String(contentsOf: item.circuit, encoding: .utf8)).map(shape)
                if disk != shape(core.shareText(flags: 0)) { pausesStale += 1; if firstStale < 0 { firstStale = at } }
            }

            // The window's own checks, then the gestures students use most,
            // scripted (SessionGestures.swift), before the random session.
            windowChecks(canvas, core, check: check, turn: turn)
            gestureChecks(canvas, core, file, win, check: check, turn: turn,
                          edited: { what, before in
                              guard shape(core.shareText(flags: 0)) != before else { return }
                              check(file.isDocumentEdited, "gestures: \(what) leaves the window edited")
                          }, pause: { pause(-1) })
            check(pausesChecked == 1 && pausesUnsaved == 0 && pausesStale == 0, "gestures: autosave saved them, and the file on disk is the circuit")
            pausesChecked = 0; pausesUnsaved = 0; pausesStale = 0; firstStale = -1; firstUnsaved = -1
            var gestures = 0, escapeChanged = 0, putBackChanged = 0, firstPutBack = -1

            memory.append(footprintMB())
            for a in 0..<actions {
                let c = side()
                let roll = rng.int(100)
                var isEdit = false
                let before = shape(core.shareText(flags: 0))
                if rng.int(4) == 0, let v = c.view {
                    // A gesture through the canvas's own handlers.
                    gestures += 1
                    let hands = CanvasHands(view: v)
                    let editable = c.canEdit
                    let g = rng.int(10)
                    kind = "gesture \(g)"
                    switch g {
                    case 0, 1:   // a drag: a part, a wire end, a pin to somewhere, a box
                        let from = spot(c)
                        hands.drag(from, CGPoint(x: from.x + CGFloat(rng.int(15)) - 7, y: from.y + CGFloat(rng.int(15)) - 7))
                        isEdit = true
                    case 2:   // Escape in the middle of a drag: nothing changes
                        let from = spot(c)
                        hands.drag(from, CGPoint(x: from.x + 5, y: from.y - 3), before: { hands.key(CanvasHands.escape) })
                        if editable && shape(core.shareText(flags: 0)) != before { escapeChanged += 1 }
                    case 3:   // the arrows
                        hands.click(spot(c))
                        hands.key(CanvasHands.arrow(UInt16(123 + rng.int(4))), flags: rng.int(3) == 0 ? .shift : [])
                        isEdit = true
                    case 4:   // scroll, pinch
                        if rng.int(2) == 0 { hands.scroll(dx: Int32(rng.int(41)) - 20, dy: Int32(rng.int(41)) - 20) }
                        else { v.zoom(by: 0.8 + 0.4 * rng.unit(), at: NSPoint(x: v.bounds.midX, y: v.bounds.midY)) }
                    case 5:   // a part's settings: a label's text
                        hands.click(spot(c))
                        if let g = core.singleSelectedGate(page: c.page),
                           let s = core.settings(ofGate: g).first(where: { $0.name.contains("LABEL_TEXT") }) {
                            applySetting(c, gate: g, name: s.name, value: "L\(rng.int(50))")
                            isEdit = true
                        }
                    case 6:   // Cmd-D, then a click places it, or Escape takes it back
                        if c.hasSelection && c.canEdit {
                            c.perform(.duplicate)
                            if rng.int(2) == 0 {
                                let at = spot(c)
                                hands.move(at); hands.click(at)
                                isEdit = true
                            } else {
                                hands.key(CanvasHands.escape)
                                if shape(core.shareText(flags: 0)) != before { escapeChanged += 1 }
                            }
                        } else {
                            hands.click(spot(c))
                        }
                    case 7:   // out of the palette: dropped, or let go off the canvas
                        let at = spot(c)
                        if c.addGateFloating(parts[rng.int(parts.count)], at: at) {
                            if rng.int(3) == 0 {
                                c.cancelFloating()
                            } else {
                                _ = core.hover(page: c.page, at: CGPoint(x: at.x + 2, y: at.y), unitsPerPoint: v.unitsPerPoint)
                                _ = core.press(page: c.page, at: CGPoint(x: at.x + 2, y: at.y), modifiers: [], unitsPerPoint: v.unitsPerPoint)
                                core.release(at: CGPoint(x: at.x + 2, y: at.y))
                                c.edited()
                            }
                        }
                        isEdit = true
                    case 8:   // Tidy Up's preview: put back, or kept
                        hands.key(1, "S", flags: .shift)
                        if rng.int(3) == 0 { hands.key(CanvasHands.tab) }
                        if rng.int(2) == 0 {
                            hands.key(CanvasHands.escape)
                            if editable && shape(core.shareText(flags: 0)) != before { putBackChanged += 1; if firstPutBack < 0 { firstPutBack = a } }
                        } else {
                            hands.key(CanvasHands.returnKey)
                            isEdit = true
                        }
                    default:   // the find bar
                        c.perform(.find)
                        c.findQuery = rng.int(2) == 0 ? "L\(rng.int(50))" : "AND"
                        c.runFind(jump: true)
                        c.findStep(1)
                        c.closeFind()
                    }
                } else {
                kind = "action \(roll)"
                switch roll {
                case 0..<22:   // place a part
                    if gates(c) > crowded {
                        c.selectAll(); c.deleteSelection()
                    } else {
                        c.addGate(parts[rng.int(parts.count)], at: spot(c))
                    }
                    isEdit = true
                case 22..<36:  // drag something (or a box), as the mouse does
                    let from = spot(c), to = CGPoint(x: from.x + CGFloat(rng.int(13)) - 6, y: from.y + CGFloat(rng.int(13)) - 6)
                    if c.canEdit {
                        _ = core.press(page: c.page, at: from, modifiers: [], unitsPerPoint: c.view?.unitsPerPoint ?? 0.1)
                        core.drag(to: CGPoint(x: (from.x + to.x) / 2, y: (from.y + to.y) / 2))
                        core.drag(to: to)
                        core.release(at: to)
                        c.edited()
                        isEdit = true
                    }
                case 36..<42: c.connectNearby(quietly: true); isEdit = true
                case 42..<46: c.nudge(dx: CGFloat(rng.int(3)) - 1, dy: CGFloat(rng.int(3)) - 1); isEdit = true
                case 46..<49: c.rotate(); isEdit = true
                case 49..<52:  // delete what's selected
                    c.deleteSelection(); isEdit = true
                case 52..<62 where rng.int(3) == 0:
                    for _ in 0..<(1 + rng.int(4)) { if canvas.canUndo { canvas.undo(); undos += 1 } }
                case 62..<68 where rng.int(3) == 0:
                    for _ in 0..<(1 + rng.int(3)) { if canvas.canRedo { canvas.redo(); redos += 1 } }
                case 52..<68: c.addGate(parts[rng.int(parts.count)], at: spot(c)); isEdit = true
                case 68..<71: _ = core.click(page: c.page, at: spot(c)); c.redraw()
                case 71..<73: c.perform(.newTab); tabsMade += 1
                case 73..<76: c.perform(.nextTab)
                case 76..<78:  // close the tab if it's empty (one with parts asks first)
                    if core.pageCount > 1 && gates(c) == 0 { c.perform(.closeTab); tabsClosed += 1 }
                case 78: c.perform(.reopenTab)
                case 79..<81: canvas.perform(.splitView); splits += 1
                case 81..<83: canvas.perform(.switchPane)
                case 83..<85: c.perform(.simView)
                case 85..<87: c.perform(.scope)
                case 87..<89: canvas.setRunning(!canvas.isRunning)
                case 89..<91: c.perform(.step)
                case 91..<93: c.straighten(); isEdit = true
                case 93..<95: c.perform(.selectAll)
                case 95..<97: c.perform(.zoomFit)
                default: c.selectNone()
                }
                }
                turn(0.02 + 0.03 * rng.unit())
                if isEdit { checkEdited(a, from: before) }
                if core.mirroredUndo != core.undoCount { stepMismatch += 1; if firstMismatch < 0 { firstMismatch = a } }
                maxSteps = max(maxSteps, core.undoCount)
                checkPages(a)
                if a % 40 == 39 {
                    // Simulation View stops editing; a student turns it off to carry on.
                    for s in [canvas, canvas.partner].compactMap({ $0 }) where s.simView { s.simView = false }
                    pause(a)
                }
                if a == actions / 2 {
                    // Coming back after a break: the last save was 11 minutes
                    // ago. First the work so far is saved and that save seen
                    // (Your Circuits' every-2-s look notes a save only while
                    // the window is unedited: see docs/MAC-APP-AUDIT.md).
                    for s in [canvas, canvas.partner].compactMap({ $0 }) where s.simView { s.simView = false }
                    pause(a)
                    turn(2.2)
                    let pending = item.versionsFolder.appendingPathComponent(".pending.cdl")
                    let versionsBefore = Library.versions(of: item).count
                    try? FileManager.default.setAttributes([.modificationDate: Date().addingTimeInterval(-11 * 60)], ofItemAtPath: pending.path)
                    let shapeBefore = shape(core.shareText(flags: 0))
                    c.addGate("AA_AND2", at: spot(c))
                    turn(0.03)   // the window's edited state follows as the run loop turns
                    checkEdited(a, from: shapeBefore)
                    pause(a)
                    turn(2.2)   // the save is noticed on the next 2-second look
                    // Only if the circuit changed since the newest version
                    // (the same circuit twice in a row isn't kept).
                    versionAfterBreak = Library.versions(of: item).count > versionsBefore
                    if versionAfterBreak != true {
                        print("INFO the break: \(versionsBefore) versions before, \(Library.versions(of: item).count) after; .pending.cdl \(FileManager.default.fileExists(atPath: pending.path) ? "there" : "not there"), the window \(file.isDocumentEdited ? "edited" : "saved")")
                    }
                }
                if a == actions / 4 || a == actions - 1 { memory.append(footprintMB()) }
            }
            if canvas.isRunning == false { canvas.setRunning(true) }
            turn(1)
            check(escapeChanged == 0, "Escape mid-drag or on an unplaced copy changed nothing (\(escapeChanged) times it did, of \(gestures) gestures)")
            check(putBackChanged == 0, "Tidy Up's Put Back left the circuit as it was\(firstPutBack >= 0 ? " (\(putBackChanged) times not, first at action \(firstPutBack))" : "")")
            // Undo all the way back, then Redo all the way: one press, one step.
            for s in [canvas, canvas.partner].compactMap({ $0 }) where s.simView { s.simView = false }
            let beforeWalk = shape(core.shareText(flags: 0)), stepsBeforeWalk = core.undoCount
            var presses = 0, nothing = 0, several = 0
            while canvas.canUndo && presses < 3000 {
                let n = core.undoCount
                canvas.undo(); presses += 1
                if core.undoCount == n { nothing += 1 } else if n - core.undoCount > 1 { several += 1 }
                if presses % 50 == 0 { turn(0.01) }
            }
            let undone = core.undoCount
            // Back to where the walk started -- not past it, into steps the
            // session itself had undone before the walk.
            var redoes = 0
            while canvas.canRedo && core.undoCount < stepsBeforeWalk && redoes < 3000 { canvas.redo(); redoes += 1; if redoes % 50 == 0 { turn(0.01) } }
            turn(0.1)
            print("INFO undo walk: \(presses) Undo presses (\(nothing) did nothing, \(several) took back more than one step, \(undone) steps left), \(redoes) Redo presses")
            let afterWalk = shape(core.shareText(flags: 0))
            /// Less the wires' drawn paths (the engine may route a wire put
            /// back by Redo another way): parts, settings and connections.
            func logical(_ t: String) -> [String] {
                t.split(separator: "\n").compactMap { line -> String? in
                    var l = String(line.drop { $0 == " " })
                    if l.hasPrefix("(pts ") || l.hasPrefix("(seg ") || l.hasPrefix("(cross ") { return nil }
                    while l.hasSuffix(")") { l.removeLast() }
                    return l
                }.sorted()
            }
            if afterWalk != beforeWalk {
                // What differs (for the engine: undo and redo are its commands).
                let a = afterWalk.split(separator: "\n"), b = beforeWalk.split(separator: "\n")
                let gone = b.filter { !a.contains($0) }.prefix(6), came = a.filter { !b.contains($0) }.prefix(6)
                print("INFO undo walk: lines lost \(gone.map { $0.trimmingCharacters(in: .whitespaces) }), lines new \(came.map { $0.trimmingCharacters(in: .whitespaces) })")
            }
            check(logical(afterWalk) == logical(beforeWalk), "Undo all the way back and Redo all the way gives the circuit back (its parts, settings and connections\(afterWalk == beforeWalk ? ", and its wires' paths" : "; a wire's path differs, see INFO"))")
            pause(actions)
            print(String(format: "session: %d actions, %d undo steps at most, %d undos, %d redos, %d splits, %d tabs made, %d closed, %d pages now",
                         actions, maxSteps, undos, redos, splits, tabsMade, tabsClosed, core.pageCount))

            // Closing the window, as the red button does (it's saved by now).
            file.close()
            win.contentView = nil
            delegate.file = nil
        } }
        turn(1.5)

        check((editsChecked > 0 || actions == 0) && editsLost == 0,
              "every edit leaves the window edited, so autosave saves it (\(editsChecked - editsLost) of \(editsChecked)\(firstLost >= 0 ? ", first lost at action \(firstLost): \(lostWhat)" : ""))")
        check(pausesChecked > 0 && pausesUnsaved == 0, "autosave saved the circuit at every pause (\(pausesChecked - pausesUnsaved) of \(pausesChecked)\(firstUnsaved >= 0 ? ", first unsaved at action \(firstUnsaved)" : ""))")
        check(pausesStale == 0, "the file on disk is the circuit as it is, at every pause (\(pausesChecked - pausesStale) of \(pausesChecked)\(firstStale >= 0 ? ", first stale at action \(firstStale)" : ""))")
        check(stepMismatch == 0, "the window's undo steps match the engine's after every action\(firstMismatch >= 0 ? " (\(stepMismatch) mismatches, first at action \(firstMismatch))" : "")")
        check(badPage == 0, "each side shows a page that exists\(firstBadPage >= 0 ? " (\(badPage) times not, first at action \(firstBadPage))" : "")")
        check(maxSteps >= CanvasController.engineHistoryLimit || actions < 2000, "the session reached the engine's full history (\(maxSteps) steps)")
        if let v = versionAfterBreak { check(v, "Version History kept a version after an 11-minute break") }
        let versions = Library.versions(of: item).count
        check(versions >= 1, "the circuit has versions (\(versions))")
        if memory.count >= 3 {
            let growth = memory[2] - memory[1]
            print(String(format: "memory: %.0f MB at the start, %.0f MB a quarter in, %.0f MB at the end", memory[0], memory[1], memory[2]))
            check(growth < 150, String(format: "memory over the last three quarters grew %.0f MB (under 150)", growth))
        }
        print("after closing: window \(goneWindow == nil ? "gone" : "kept"), its view \(goneHost == nil ? "gone" : "kept"), document \(goneFile == nil ? "gone" : "kept")")
        check(goneCanvas == nil, "the window's controller (and its 60-a-second clock) went with the window")
        check(goneCore == nil, "the circuit went with the window")
        let documentsAfter = Int(cl_document_live_count())
        check(documentsOpen > documentsAtStart && documentsAfter == documentsAtStart,
              "the engine's open documents are back to where they were before the window opened (\(documentsAtStart) before, \(documentsOpen) with the window open, \(documentsAfter) after)")
        check(goneFile == nil || !NSDocumentController.shared.documents.contains { $0 === goneFile }, "the document closed")
        prefs.openReplaces = savedOpenReplaces
        print(failures == 0 ? "session-check: all passed" : "session-check: \(failures) failed")
        return failures == 0
    }

    /// Run and pause from either side of a split: both sides and the engine
    /// agree (Space on the second side once paused only the engine and that
    /// side; the first side, whose clock runs the circuit, still said
    /// running -- or the other way round, a running engine nothing ticked).
    static func splitRunChecks(_ check: (Bool, String) -> Void) {
        let core = CoreDocument()
        let main = CanvasController(), side = CanvasController()
        side.drivesClock = false
        main.partner = side; side.partner = main
        main.attach(core); side.attach(core)
        func agree() -> Bool { main.isRunning == core.isRunning && side.isRunning == core.isRunning }
        side.toggleRunning()
        check(agree() && !core.isRunning, "split: Space on the second side pauses both sides")
        side.toggleRunning()
        check(agree() && core.isRunning, "split: and runs both again")
        main.toggleRunning()
        check(agree() && !core.isRunning, "split: pausing on the first side pauses the second too")
        side.simView = true
        check(agree() && core.isRunning, "split: Simulation View on the second side runs both")
        side.stepOnce()
        check(agree() && !core.isRunning, "split: a step on the second side pauses both")
        side.simView = false
        main.partner = nil; side.partner = nil
    }

    /// A circuit's text less what running it changes (as Version History
    /// compares them): switches, registers, the app that wrote it.
    static func shape(_ text: String) -> String {
        text.split(separator: "\n").filter { line in
            let t = line.drop { $0 == " " }
            return !(t.hasPrefix("(lparam \"OUTPUT_NUM\"") || t.hasPrefix("(lparam \"CURRENT_VALUE\"")
                     || t.hasPrefix("(generator "))
        }.joined(separator: "\n")
    }
}
