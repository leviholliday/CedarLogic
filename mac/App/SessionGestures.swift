// The gestures --session-check (SessionCheck.swift) makes with the canvas's
// own event handlers, as the mouse, keyboard and trackpad send them: a wire
// drawn from pin to pin and from a pin to a wire, a part dragged and
// connected with C on the way (and Escape taking that back), Escape in the
// middle of a drag, a double-click for a part's settings and a setting
// changed there, the arrows nudging, Cmd-D and the click that places the
// copy, a part pulled out of the palette (dropped, or let go off the
// canvas), scrolling and pinching, the find bar, Straighten, and Tidy Up's
// preview kept or put back. Then the window's own: a status message that has
// to go again, the oscilloscope redrawing only itself, the notes pane, a
// sheet that can't be seen cancelled by Make This Window Editable, and Cmd-W
// under a sheet.
//
// The window is the session's, never on screen: for the sheets it's ordered
// in where it is, far off every display, transparent and not taking clicks;
// for C (which looks at where the pointer really is) it's moved under the
// pointer while it's still out of sight. Nothing on screen is clicked.

import AppKit
import SwiftUI

/// Mouse and key events for one canvas, sent to its own handlers.
@MainActor
struct CanvasHands {
    let view: CircuitCanvasNSView

    /// A point on the page, in the window's coordinates (where events are).
    func windowPoint(_ world: CGPoint) -> NSPoint {
        view.convert(view.viewRect(CGRect(origin: world, size: .zero)).origin, to: nil)
    }
    func mouse(_ type: NSEvent.EventType, _ world: CGPoint, clicks: Int = 1, flags: NSEvent.ModifierFlags = []) -> NSEvent? {
        NSEvent.mouseEvent(with: type, location: windowPoint(world), modifierFlags: flags,
                           timestamp: ProcessInfo.processInfo.systemUptime, windowNumber: view.window?.windowNumber ?? 0,
                           context: nil, eventNumber: 0, clickCount: clicks, pressure: type == .leftMouseUp ? 0 : 1)
    }
    func down(_ w: CGPoint, clicks: Int = 1, flags: NSEvent.ModifierFlags = []) {
        if let e = mouse(.leftMouseDown, w, clicks: clicks, flags: flags) { view.mouseDown(with: e) }
    }
    func up(_ w: CGPoint, clicks: Int = 1) {
        if let e = mouse(.leftMouseUp, w, clicks: clicks) { view.mouseUp(with: e) }
    }
    func click(_ w: CGPoint) { down(w); up(w) }
    func doubleClick(_ w: CGPoint) { down(w); up(w); down(w, clicks: 2); up(w, clicks: 2) }
    func move(_ w: CGPoint) { if let e = mouse(.mouseMoved, w, clicks: 0) { view.mouseMoved(with: e) } }
    /// Press, drag there in a few steps, `before` (a key, say) with the
    /// button still down, let go.
    func drag(_ from: CGPoint, _ to: CGPoint, steps: Int = 4, before: (() -> Void)? = nil) {
        down(from)
        let n = max(steps, 1)
        for i in 1...n {
            let t = CGFloat(i) / CGFloat(n)
            if let e = mouse(.leftMouseDragged, CGPoint(x: from.x + (to.x - from.x) * t, y: from.y + (to.y - from.y) * t)) {
                view.mouseDragged(with: e)
            }
        }
        before?()
        up(to)
    }
    func key(_ code: UInt16, _ chars: String, flags: NSEvent.ModifierFlags = []) {
        for type in [NSEvent.EventType.keyDown, .keyUp] {
            guard let e = NSEvent.keyEvent(with: type, location: .zero, modifierFlags: flags,
                                           timestamp: ProcessInfo.processInfo.systemUptime,
                                           windowNumber: view.window?.windowNumber ?? 0, context: nil,
                                           characters: chars, charactersIgnoringModifiers: chars,
                                           isARepeat: false, keyCode: code) else { continue }
            if type == .keyDown { view.keyDown(with: e) } else { view.keyUp(with: e) }
        }
    }
    func key(_ k: (UInt16, String), flags: NSEvent.ModifierFlags = []) { key(k.0, k.1, flags: flags) }
    static let escape: (UInt16, String) = (53, "\u{1B}")
    static let returnKey: (UInt16, String) = (36, "\r")
    static let tab: (UInt16, String) = (48, "\t")
    static func arrow(_ code: UInt16) -> (UInt16, String) {
        let ch: Int = switch code {
        case 123: NSLeftArrowFunctionKey
        case 124: NSRightArrowFunctionKey
        case 125: NSDownArrowFunctionKey
        default: NSUpArrowFunctionKey
        }
        return (code, String(Character(UnicodeScalar(UInt16(ch))!)))
    }
    /// A two-finger scroll on a trackpad (pixel deltas, as a trackpad sends).
    func scroll(dx: Int32, dy: Int32, flags: NSEvent.ModifierFlags = []) {
        guard let cg = CGEvent(scrollWheelEvent2Source: nil, units: .pixel, wheelCount: 2, wheel1: dy, wheel2: dx, wheel3: 0) else { return }
        if flags.contains(.command) { cg.flags = .maskCommand }
        if let e = NSEvent(cgEvent: cg) { view.scrollWheel(with: e) }
    }
}

extension SessionCheck {
    /// How many wires the circuit has.
    static func wireCount(_ core: CoreDocument) -> Int { core.shareText(flags: 0).components(separatedBy: "(wire").count - 1 }
    static func gateCount(_ core: CoreDocument, _ page: Int) -> Int { Int(cl_document_gate_count(core.handle, Int32(page))) }

    /// Change one of a gate's settings, as its Settings sheet's controls do.
    static func applySetting(_ c: CanvasController, gate: Int, name: String, value: String) {
        c.document?.setSetting(gate: gate, name: name, value: value)
        c.redraw()
        c.editsChanged()
        c.selectionChanged()
    }

    /// The scripted gestures, on a new tab of the session's window. `edited`
    /// checks the window was marked edited by a change (given the circuit
    /// before it); `pause` waits for autosave and checks the file on disk.
    static func gestureChecks(_ canvas: CanvasController, _ core: CoreDocument, _ file: NSDocument, _ win: NSWindow,
                              check: (Bool, String) -> Void, turn: (Double) -> Void,
                              edited: (String, String) -> Void, pause: () -> Void) {
        guard let firstView = canvas.view else { check(false, "gestures: the canvas has no view"); return }
        // The canvas's view as it is now: SwiftUI can make a new one (a tab
        // closed, a sheet put away), and the controller follows it.
        var view: CircuitCanvasNSView { canvas.view ?? firstView }
        var hands: CanvasHands { CanvasHands(view: view) }
        let shape = { SessionCheck.shape(core.shareText(flags: 0)) }
        func inStep(_ what: String) {
            if core.mirroredUndo != core.undoCount { check(false, "gestures: \(what): the window's undo steps match the engine's (\(core.mirroredUndo) and \(core.undoCount))") }
        }

        canvas.perform(.newTab)
        turn(0.4)
        let page = canvas.page
        check(page == core.pageCount - 1 && gateCount(core, page) == 0, "gestures: on a new, empty tab (\(page + 1) of \(core.pageCount))")
        view.zoomTo(unitsPerPoint: 0.05)
        view.center(on: CGPoint(x: 0, y: 0))
        view.needsFit = false   // a new tab is fitted when it's first drawn
        turn(0.1)

        // Parts at known places: a switch, an AND, an inverter, a light.
        canvas.addGate("AA_TOGGLE", at: CGPoint(x: -12, y: 1))
        canvas.addGate("AA_AND2", at: CGPoint(x: 0, y: 0))
        canvas.addGate("AA_INVERTER", at: CGPoint(x: -4, y: -8))
        canvas.addGate("GA_LED", at: CGPoint(x: 14, y: 6))
        canvas.selectNone()
        turn(0.1)
        check(gateCount(core, page) == 4, "gestures: four parts placed")

        // A wire from the switch's output to the AND's top input.
        var before = shape(), wires = wireCount(core)
        hands.drag(CGPoint(x: -10, y: 1), CGPoint(x: -3, y: 1))
        turn(0.05)
        check(wireCount(core) == wires + 1, "gestures: a wire drawn from pin to pin (\(wires) to \(wireCount(core)) wires)")
        edited("wire pin to pin", before); inStep("wire pin to pin")

        // From the inverter's output to the middle of that wire.
        before = shape()
        hands.drag(CGPoint(x: -1, y: -8), CGPoint(x: -6.5, y: 1))
        turn(0.05)
        check(shape() != before, "gestures: a wire drawn from a pin to a wire")
        edited("wire to a wire", before); inStep("wire to a wire")

        // Escape in the middle of dragging the AND: it goes back.
        before = shape()
        var steps = core.undoCount
        hands.drag(CGPoint(x: 0, y: 0), CGPoint(x: 6, y: 5), before: { hands.key(CanvasHands.escape) })
        turn(0.05)
        check(shape() == before && core.undoCount == steps, "gestures: Escape mid-drag puts the part back, with no undo step")
        inStep("Escape mid-drag")

        // C while dragging the light onto the AND's output: connected, kept
        // at the drop. C looks at where the pointer really is, so the
        // (unseen) window goes under it for this.
        let home = win.frame.origin
        let mouse = NSEvent.mouseLocation
        let middle = view.convert(NSPoint(x: view.bounds.midX, y: view.bounds.midY), to: nil)
        win.setFrameOrigin(NSPoint(x: mouse.x - middle.x, y: mouse.y - middle.y))
        before = shape(); wires = wireCount(core)
        hands.drag(CGPoint(x: 14, y: 6), CGPoint(x: 4, y: 0), before: { hands.key(8, "c") })
        turn(0.05)
        check(wireCount(core) > wires, "gestures: C while dragging connects the part (\(wires) to \(wireCount(core)) wires)")
        edited("connect while moving", before); inStep("connect while moving")
        // Again with the inverter, and Escape takes the connection back (the move stays).
        before = shape(); wires = wireCount(core)
        hands.drag(CGPoint(x: -4, y: -8), CGPoint(x: 1, y: 1), before: { hands.key(8, "c"); hands.key(CanvasHands.escape) })
        turn(0.05)
        check(wireCount(core) == wires, "gestures: Escape after C takes back just the connections (\(wires) to \(wireCount(core)) wires)")
        edited("C then Escape", before); inStep("C then Escape")
        win.setFrameOrigin(home)

        // A label, double-clicked for its settings; its text changed there.
        canvas.addGate("AA_LABEL", at: CGPoint(x: 0, y: 10))
        canvas.selectNone()
        win.alphaValue = 0
        win.ignoresMouseEvents = true
        win.orderBack(nil)   // where it is, far off every display: sheets attach only to a window that's in
        turn(0.3)
        hands.doubleClick(CGPoint(x: 0, y: 10))
        turn(0.6)
        let label = core.singleSelectedGate(page: page)
        check(label != nil && win.attachedSheet != nil, "gestures: a double-click on a part opens its settings (\(win.attachedSheet != nil ? "sheet up" : "no sheet"))")
        // Done: the sheet's Return.
        if let sheet = win.attachedSheet,
           let e = NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: [], timestamp: ProcessInfo.processInfo.systemUptime,
                                    windowNumber: sheet.windowNumber, context: nil, characters: "\r", charactersIgnoringModifiers: "\r",
                                    isARepeat: false, keyCode: 36) {
            if !sheet.performKeyEquivalent(with: e) { sheet.sendEvent(e) }
            turn(0.6)
        }
        print("INFO gestures: after Return in the settings sheet, \(win.attachedSheet == nil ? "it closed" : "it's still up")")
        if win.attachedSheet != nil { canvas.makeEditable(); turn(0.6) }
        // The text changed as the sheet's field does it (here once the sheet
        // is closed: its Return puts back what its field still holds).
        if let label, let setting = core.settings(ofGate: label).first(where: { $0.name.contains("LABEL_TEXT") }) {
            before = shape()
            applySetting(canvas, gate: label, name: setting.name, value: "Q1")
            turn(0.05)
            check(core.shareText(flags: 0).contains("\"Q1\""), "gestures: the setting changed")
            edited("a setting changed", before); inStep("a setting changed")
        } else {
            check(false, "gestures: the label has a text setting")
        }

        // A sheet that can't be seen, cancelled by View > Make This Window
        // Editable (or the two-second "never stuck" look): it has to go, and
        // open again the next time it's asked for.
        if win.attachedSheet != nil { print("INFO sheets: a sheet was still up before the hidden-sheet check") }
        canvas.showQuickAdd = true
        turn(0.6)
        if let sheet = win.attachedSheet {
            sheet.alphaValue = 0
            canvas.makeEditable()
            turn(0.8)
            check(win.attachedSheet == nil, "sheets: a sheet that can't be seen goes with Make This Window Editable")
            check(!canvas.showQuickAdd, "sheets: and Add a Gate isn't left asked for")
            canvas.showQuickAdd = false
            turn(0.4)
            canvas.showQuickAdd = true
            turn(0.6)
            check(win.attachedSheet != nil && (win.attachedSheet?.alphaValue ?? 0) > 0, "sheets: Add a Gate opens again, and can be seen")
            canvas.showQuickAdd = false
            turn(0.6)
        } else {
            check(false, "sheets: Add a Gate opened a sheet")
        }
        if win.attachedSheet != nil { canvas.makeEditable(); turn(0.6) }

        // Cmd-W (Close Tab) with a sheet up: nothing closes under it. On an
        // empty tab, so closing it never asks (a question would be a window).
        canvas.perform(.newTab)
        turn(0.4)
        canvas.showExportReport = true
        turn(0.6)
        let pagesBefore = core.pageCount
        if win.attachedSheet != nil && gateCount(core, canvas.page) == 0 {
            canvas.perform(.closeTab)
            turn(0.5)
            check(core.pageCount == pagesBefore && file.windowControllers.first?.window === win,
                  "sheets: Cmd-W with a sheet up leaves the tabs and the window (\(pagesBefore) to \(core.pageCount) tabs)")
        } else {
            check(false, "sheets: Export Report opened a sheet")
        }
        canvas.showExportReport = false
        turn(0.6)
        if win.attachedSheet != nil { canvas.makeEditable(); turn(0.6) }
        if core.pageCount > 1 && gateCount(core, canvas.page) == 0 { canvas.perform(.closeTab); turn(0.4) }
        win.orderOut(nil)
        win.alphaValue = 1
        win.ignoresMouseEvents = false
        turn(0.2)
        if canvas.page != page, page < core.pageCount { canvas.pageRequest = page; turn(0.3) }

        // Find: the label, then back to the canvas.
        canvas.selectNone()
        canvas.perform(.find)
        canvas.findQuery = "Q1"
        canvas.runFind(jump: true)
        turn(0.3)
        check(canvas.findActive && canvas.findTotal >= 1 && label != nil && core.singleSelectedGate(page: canvas.page) == label,
              "gestures: the find bar finds the label and selects it (\(canvas.findTotal) found)")
        canvas.findStep(1)
        turn(0.3)
        canvas.closeFind()
        turn(0.3)
        check(!canvas.findActive && win.firstResponder === view,
              "gestures: closing the find bar gives the canvas the keys back (\(win.firstResponder.map { String(describing: type(of: $0)) } ?? "nothing") has them)")

        // The arrows nudge what's selected (Shift: further).
        hands.click(CGPoint(x: 0, y: 0))
        before = shape()
        hands.key(CanvasHands.arrow(124))
        hands.key(CanvasHands.arrow(126), flags: .shift)
        turn(0.05)
        check(core.singleSelectedGate(page: page) != nil && shape() != before, "gestures: the arrows nudge the selected part")
        edited("nudge", before); inStep("nudge")

        // Cmd-D: a copy on the pointer, placed by the next click; Escape
        // instead takes it back.
        var gates = gateCount(core, page)
        before = shape()
        canvas.perform(.duplicate)
        turn(0.05)
        let floated = canvas.isFloating
        hands.move(CGPoint(x: 8, y: -10))
        hands.click(CGPoint(x: 8, y: -10))
        turn(0.05)
        check(floated && !canvas.isFloating && gateCount(core, page) == gates + 1,
              "gestures: Cmd-D puts a copy on the pointer and a click places it (floated \(floated), still \(canvas.isFloating), \(gates) to \(gateCount(core, page)) parts)")
        edited("duplicate", before); inStep("duplicate")
        gates = gateCount(core, page)
        before = shape()
        canvas.perform(.duplicate)
        turn(0.05)
        hands.key(CanvasHands.escape)
        turn(0.05)
        check(!canvas.isFloating && gateCount(core, page) == gates && shape() == before, "gestures: Escape takes back a copy that wasn't placed")
        inStep("duplicate then Escape")

        // A part pulled out of the palette (what PaletteGrid's drag does):
        // dropped on the canvas, then one let go off it.
        before = shape()
        if canvas.addGateFloating("AE_OR2", at: CGPoint(x: -14, y: -12)) {
            for i in 1...4 { _ = core.hover(page: page, at: CGPoint(x: -14 + Double(i), y: -12), unitsPerPoint: view.unitsPerPoint) }
            _ = core.press(page: page, at: CGPoint(x: -10, y: -12), modifiers: [], unitsPerPoint: view.unitsPerPoint)
            core.release(at: CGPoint(x: -10, y: -12))
            canvas.edited()
        }
        turn(0.05)
        check(!canvas.isFloating && gateCount(core, page) == gates + 1, "gestures: a part dragged from the palette lands")
        edited("palette drop", before); inStep("palette drop")
        gates = gateCount(core, page)
        before = shape()
        if canvas.addGateFloating("AE_OR2", at: CGPoint(x: -14, y: -16)) { canvas.cancelFloating() }
        turn(0.05)
        check(!canvas.isFloating && gateCount(core, page) == gates && shape() == before, "gestures: a part let go off the canvas isn't added")
        inStep("palette let go off the canvas")

        // Scrolling, pinching and smart zoom move the camera only.
        before = shape()
        steps = core.undoCount
        let upp0 = view.unitsPerPoint
        hands.scroll(dx: 0, dy: 30)
        hands.scroll(dx: 25, dy: -5)
        hands.scroll(dx: 0, dy: -12, flags: .command)
        view.zoom(by: 1 + 0.3, at: NSPoint(x: view.bounds.midX, y: view.bounds.midY))   // what magnify(with:) does
        view.zoom(by: 1 - 0.2, at: NSPoint(x: 10, y: 10))
        turn(0.05)
        check(view.unitsPerPoint != upp0 && view.unitsPerPoint >= view.minUnitsPerPoint && view.unitsPerPoint <= view.maxUnitsPerPoint,
              String(format: "gestures: scrolling and pinching zoom, within limits (%.4f to %.4f)", upp0, view.unitsPerPoint))
        check(shape() == before && core.undoCount == steps, "gestures: and change nothing in the circuit")

        // Straighten (S), then Tidy Up's preview (Shift-S): Escape puts it
        // back, Tab tries the other kind, Return keeps it.
        canvas.selectAll()
        before = shape()
        hands.key(1, "s")
        turn(0.05)
        edited("Straighten", before); inStep("Straighten")
        canvas.selectNone()
        before = shape()
        hands.key(1, "S", flags: .shift)
        turn(0.05)
        let previewed = canvas.tidyActive
        hands.key(CanvasHands.escape)
        turn(0.05)
        check(previewed && !canvas.tidyActive && shape() == before, "gestures: Tidy Up's preview, Put Back (Escape) leaves the circuit as it was")
        inStep("Tidy Up put back")
        hands.key(1, "S", flags: .shift)
        hands.key(CanvasHands.tab)
        turn(0.05)
        let switched = canvas.tidyActive
        hands.key(CanvasHands.returnKey)
        turn(0.05)
        check(switched && !canvas.tidyActive, "gestures: Tidy Up's preview, the other kind (Tab), then Keep (Return)")
        edited("Tidy Up kept", before); inStep("Tidy Up kept")

        pause()
    }

    /// The window's own: what it shows, and that it lets go of it.
    static func windowChecks(_ canvas: CanvasController, _ core: CoreDocument, check: (Bool, String) -> Void, turn: (Double) -> Void) {
        let prefs = Prefs.shared
        // A status message with the status bar's figures turned off: the bar
        // shows just for it, and has to go again.
        let showStatus = prefs.showStatus
        prefs.showStatus = false
        turn(0.3)
        canvas.note("A message for the status bar.")
        turn(4.6)
        check(canvas.statusMessage.isEmpty, "window: a status message goes again with the bar's figures off (\(canvas.statusMessage.isEmpty ? "gone" : "still there"))")
        prefs.showStatus = true
        turn(0.3)
        canvas.note("Another message.")
        turn(4.6)
        check(canvas.statusMessage.isEmpty, "window: and with them on")
        prefs.showStatus = showStatus

        // The oscilloscope open on a running circuit: it redraws itself as
        // the circuit records, not the whole window.
        let running = canvas.isRunning
        canvas.setRunning(true)
        canvas.showScope = true
        turn(0.5)
        var changes = 0
        let watch = canvas.objectWillChange.sink { _ in changes += 1 }
        turn(2.0)
        watch.cancel()
        check(changes < 6, "window: with the scope open and running, the whole window was told to redraw \(changes) times in 2 s (under 6)")
        canvas.showScope = false
        canvas.setRunning(running)
        turn(0.2)

        // The notes pane, on a circuit with notes (session-check.sh counts
        // SwiftUI's warnings).
        core.notes = "Notes for the session check."
        canvas.markEdited()   // as typing in the notes does
        canvas.showNotes = true
        turn(0.6)
        canvas.showNotes = false
        turn(0.3)
    }
}
