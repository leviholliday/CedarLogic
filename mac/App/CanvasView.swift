// The circuit canvas: an AppKit view, so macOS itself tracks the mouse and the
// trackpad (no hand-rolled mouse capture to get stuck). It draws the look's
// background and grid, asks the engine to draw the page on top, and turns
// clicks, drags and keys into edits.
//
// Camera: `origin` is the world point at the view's top-left corner and
// `unitsPerPoint` how many world units one point covers (smaller = closer).
// World y points up; the view is flipped, so screen y points down.

import AppKit
import QuartzCore
import UniformTypeIdentifiers
import SwiftUI

final class CircuitCanvasNSView: NSView {
    /// Whether anyone can see this canvas: the window isn't covered,
    /// minimized, on another Space or in Stage Manager's strip. A running
    /// clock redraws 60 times a second; drawing a canvas nobody can see was
    /// most of the app's CPU. The simulation runs on regardless.
    var seen: Bool { window?.occlusionState.contains(.visible) ?? false }

    /// A redraw for the clock and animations: only when seen, and drawn up
    /// to date as soon as the window can be seen again (below).
    func redrawIfSeen() {
        if seen { needsDisplay = true } else { missedDraw = true }
    }
    private var missedDraw = false
    private var occlusionWatch: NSObjectProtocol?

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        if let o = occlusionWatch { NotificationCenter.default.removeObserver(o); occlusionWatch = nil }
        guard let w = window else { return }
        occlusionWatch = NotificationCenter.default.addObserver(forName: NSWindow.didChangeOcclusionStateNotification,
                                                                object: w, queue: .main) { [weak self] _ in
            MainActor.assumeIsolated {
                guard let self, self.missedDraw, self.seen else { return }
                self.missedDraw = false
                self.needsDisplay = true
            }
        }
    }

    var document: CoreDocument? { didSet { needsFit = true; needsDisplay = true } }
    var page = 0 { didSet { if page != oldValue { needsDisplay = true } } }
    /// Which page this is showing (CoreDocument.pageID): its index changes
    /// when tabs move, but it's the same page and keeps its camera.
    var pageKey: UInt64 = 0

    /// Shows another page, where it was last looked at (the wx app keeps a
    /// camera per tab), or fitted the first time.
    func show(page newPage: Int, key: UInt64) {
        guard let document else { page = newPage; pageKey = key; return }
        if key == pageKey { page = newPage; return }
        if pageKey != 0 {
            document.cameras[pageKey] = (visibleCenter, unitsPerPoint)
            if let old = document.pageIndex(of: pageKey) { document.selectNone(page: old) }
        }
        zoomAnim = nil
        pageKey = key
        page = newPage
        if let cam = document.cameras[key], bounds.width > 0 {
            unitsPerPoint = cam.1
            origin = CGPoint(x: cam.0.x - bounds.width / 2 * cam.1, y: cam.0.y + bounds.height / 2 * cam.1)
            needsFit = false
        } else {
            needsFit = true
        }
        needsDisplay = true
        controller?.cameraMoved()   // the status bar's counts are this page's now
    }
    var theme = LookStore.shared.settings.theme { didSet { if theme != oldValue { needsDisplay = true } } }
    weak var controller: CanvasController?

    var origin = CGPoint(x: -20, y: 20) { didSet { controller?.cameraMoved() } }
    var unitsPerPoint: CGFloat = 0.05 { didSet { controller?.cameraMoved() } }
    var needsFit = true
    var pendingCamera: (CGPoint, CGFloat)?
    /// The CedarLogic interface: its colours, grid, keys and pointer rules
    /// (CLCanvas.swift). Off, the view is the Simple interface's.
    var clMode = false { didSet { if clMode != oldValue { needsDisplay = true } } }

    /// What the current drag does.
    enum Drag { case none, edit, pan(last: CGPoint) }
    var drag = Drag.none
    var spaceDown = false
    var pannedWhileSpaceDown = false

    /// An eased zoom in progress (CLCanvas.swift).
    var zoomAnim: ZoomAnimation?

    let minUnitsPerPoint: CGFloat = 0.004   // very close
    let maxUnitsPerPoint: CGFloat = 1.0     // very far

    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }
    override var isOpaque: Bool { true }

    override init(frame: NSRect) {
        super.init(frame: frame)
        wantsLayer = true
        layerContentsRedrawPolicy = .onSetNeedsDisplay
        // Since macOS 14 a view may draw outside its bounds unless told not
        // to: gates near the edge were painted over the tab strip and, in a
        // split, over the other side.
        clipsToBounds = true
        layer?.masksToBounds = true
        registerForDraggedTypes([.string])
        addTrackingArea(NSTrackingArea(rect: .zero, options: [.mouseMoved, .mouseEnteredAndExited, .activeInKeyWindow, .inVisibleRect],
                                       owner: self, userInfo: nil))
    }

    override func mouseMoved(with event: NSEvent) {
        guard let document else { return }
        let p = convert(event.locationInWindow, from: nil)
        controller?.pointerMoved(worldPoint(p))
        if CanvasController.pendingGate != nil, controller?.placePendingGate(at: worldPoint(p)) == true {
            needsDisplay = true
            return
        }
        let w = worldPoint(p)
        if clMode && controller?.simView == true {
            if cl_edit_hover_wire(document.handle, Int32(page), w.x, w.y, unitsPerPoint) { needsDisplay = true }
        } else if document.hover(page: page, at: w, unitsPerPoint: unitsPerPoint) {
            needsDisplay = true
        }
        updateWireTag(at: p)
    }

    override func mouseExited(with event: NSEvent) {
        guard let document else { return }
        if cl_edit_hover_clear(document.handle) { needsDisplay = true }
        hideWireTag()
    }

    // MARK: What a wire carries

    /// The value of the wire under the pointer, shown beside it once the
    /// pointer has rested there a moment (at once in Simulation View). Read
    /// as it's drawn, so it follows the simulation.
    private var wireTagAt: CGPoint?
    private var wireTagWaiting = false
    private var wireTagTimer: Timer?

    private func hideWireTag() {
        wireTagTimer?.invalidate(); wireTagTimer = nil
        wireTagWaiting = false
        if wireTagAt != nil { wireTagAt = nil; needsDisplay = true }
    }

    private func updateWireTag(at p: CGPoint) {
        guard clMode, Prefs.shared.wireValueTag, wireTagText() != nil else { hideWireTag(); return }
        if wireTagAt != nil { wireTagAt = p; needsDisplay = true; return }
        guard !wireTagWaiting else { return }
        wireTagWaiting = true
        let delay = controller?.simView == true ? 0.6 : 1.1
        wireTagTimer = Timer.scheduledTimer(withTimeInterval: delay, repeats: false) { [weak self] _ in
            MainActor.assumeIsolated {
                guard let self, let w = self.window else { return }
                self.wireTagTimer = nil
                self.wireTagWaiting = false
                guard self.wireTagText() != nil else { return }
                self.wireTagAt = self.convert(w.mouseLocationOutsideOfEventStream, from: nil)
                self.needsDisplay = true
            }
        }
    }

    private func wireTagText() -> (text: String, state: Character)? {
        guard let document else { return nil }
        var buf = [CChar](repeating: 0, count: 72)
        let bits = Int(cl_edit_hover_wire_state(document.handle, Int32(page), &buf, 72))
        guard bits > 0 else { return nil }
        let s = String(cString: buf)
        let state: Character = s.contains("!") ? "!" : s.contains("X") ? "X" : s.contains("Z") ? "Z" : bits == 1 ? s.first! : "b"
        if bits == 1 {
            switch state {
            case "1": return ("1", state)
            case "0": return ("0", state)
            case "Z": return ("Z · floating (nothing drives it)", state)
            case "!": return ("! · conflict (outputs disagree)", state)
            default: return ("X · unknown", state)
            }
        }
        if state == "b", let v = Int(s, radix: 2) { return (s + " = \(v)", state) }
        return (s, state)
    }

    /// A small chip beside the pointer: green for 1, grey for 0, blue for
    /// floating, red for a conflict, orange for unknown.
    func drawWireTag(_ ctx: CGContext) {
        guard let at = wireTagAt, let t = wireTagText() else { return }
        let tag = (text: t.text, state: t.state, at: at)
        let color: NSColor = switch tag.state {
        case "1": NSColor(srgbRed: 0.13, green: 0.68, blue: 0.3, alpha: 1)
        case "0": NSColor(white: 0.42, alpha: 1)
        case "Z": NSColor.systemBlue
        case "!": NSColor.systemRed
        case "X": NSColor.systemOrange
        default: NSColor(white: 0.25, alpha: 1)
        }
        let font = NSFont.monospacedSystemFont(ofSize: 12, weight: .semibold)
        let str = NSAttributedString(string: tag.text, attributes: [.font: font, .foregroundColor: NSColor.white])
        let sz = str.size()
        var r = CGRect(x: tag.at.x + 14, y: tag.at.y - 30, width: sz.width + 14, height: sz.height + 6)
        if r.maxX > bounds.maxX - 4 { r.origin.x = tag.at.x - 14 - r.width }
        if r.minY < bounds.minY + 4 { r.origin.y = tag.at.y + 14 }
        NSGraphicsContext.saveGraphicsState()
        let path = NSBezierPath(roundedRect: r, xRadius: r.height / 2, yRadius: r.height / 2)
        let shadow = NSShadow()
        shadow.shadowColor = NSColor.black.withAlphaComponent(0.3)
        shadow.shadowBlurRadius = 4
        shadow.shadowOffset = NSSize(width: 0, height: -1)
        shadow.set()
        color.setFill()
        path.fill()
        NSGraphicsContext.restoreGraphicsState()
        str.draw(at: CGPoint(x: r.minX + 7, y: r.minY + 3))
    }
    required init?(coder: NSCoder) { fatalError("not used") }

    // MARK: Drawing

    override func draw(_ dirtyRect: NSRect) {
        guard let ctx = NSGraphicsContext.current?.cgContext else { return }
        if let cam = pendingCamera, bounds.width > 0 {
            pendingCamera = nil
            unitsPerPoint = cam.1
            origin = CGPoint(x: cam.0.x - bounds.width / 2 * cam.1, y: cam.0.y + bounds.height / 2 * cam.1)
            needsFit = false
        }
        if needsFit, bounds.width > 0 { zoomToFit() }
        if clMode { drawCL(ctx); drawWireTag(ctx); return }
        ctx.setFillColor(theme.canvas.cgColor)
        ctx.fill(bounds)
        drawGrid(ctx)
        guard let document else { return }
        let scale = window?.backingScaleFactor ?? 2
        cl_document_draw(document.handle, Int32(page), ctx, scale, origin.x, origin.y,
                         unitsPerPoint, theme.darkCircuit)
        cl_edit_draw_overlay(document.handle, Int32(page), ctx, scale, origin.x, origin.y, unitsPerPoint,
                             theme.accent.r, theme.accent.g, theme.accent.b)
        if let box = document.selectionBox {
            let r = viewRect(box)
            ctx.setFillColor(theme.accent.cgColor.copy(alpha: 0.12)!)
            ctx.fill(r)
            ctx.setStrokeColor(theme.accent.cgColor.copy(alpha: 0.8)!)
            ctx.setLineWidth(1)
            ctx.stroke(r.insetBy(dx: 0.5, dy: 0.5))
        }
    }

    /// Minor lines every world unit, major every five; minor ones drop out
    /// when they'd crowd closer than a few points.
    private func drawGrid(_ ctx: CGContext) {
        guard theme.gridStyle != .none else { return }
        let spacing = 1.0 / unitsPerPoint   // points per world unit
        let showMinor = spacing >= 7
        let step = showMinor ? 1 : 5
        let x0 = Int(floor(origin.x)), x1 = Int(ceil(origin.x + bounds.width * unitsPerPoint))
        let y1 = Int(ceil(origin.y)), y0 = Int(floor(origin.y - bounds.height * unitsPerPoint))
        let startX = x0 - ((x0 % step) + step) % step
        let startY = y0 - ((y0 % step) + step) % step
        func sx(_ x: Int) -> CGFloat { (CGFloat(x) - origin.x) / unitsPerPoint }
        func sy(_ y: Int) -> CGFloat { (origin.y - CGFloat(y)) / unitsPerPoint }

        if theme.gridStyle == .lines {
            for pass in 0..<2 {   // minor, then major on top
                let major = pass == 1
                if !major && !showMinor { continue }
                ctx.setStrokeColor((major ? theme.gridMajor : theme.gridMinor).cgColor)
                ctx.setLineWidth(major ? 1 : 0.5)
                for x in stride(from: startX, through: x1, by: step) where (x % 5 == 0) == major {
                    ctx.move(to: CGPoint(x: sx(x), y: 0)); ctx.addLine(to: CGPoint(x: sx(x), y: bounds.height))
                }
                for y in stride(from: startY, through: y1, by: step) where (y % 5 == 0) == major {
                    ctx.move(to: CGPoint(x: 0, y: sy(y))); ctx.addLine(to: CGPoint(x: bounds.width, y: sy(y)))
                }
                ctx.strokePath()
            }
        } else {
            for x in stride(from: startX, through: x1, by: step) {
                for y in stride(from: startY, through: y1, by: step) {
                    let major = x % 5 == 0 && y % 5 == 0
                    let r: CGFloat = major ? 1.3 : 0.9
                    ctx.setFillColor((major ? theme.gridMajor : theme.gridMinor).cgColor)
                    ctx.fillEllipse(in: CGRect(x: sx(x) - r, y: sy(y) - r, width: 2 * r, height: 2 * r))
                }
            }
        }
    }

    // MARK: Camera

    func worldPoint(_ p: CGPoint) -> CGPoint {
        CGPoint(x: origin.x + p.x * unitsPerPoint, y: origin.y - p.y * unitsPerPoint)
    }

    func viewRect(_ world: CGRect) -> CGRect {
        CGRect(x: (world.minX - origin.x) / unitsPerPoint, y: (origin.y - world.maxY) / unitsPerPoint,
               width: world.width / unitsPerPoint, height: world.height / unitsPerPoint)
    }

    var visibleCenter: CGPoint { worldPoint(CGPoint(x: bounds.midX, y: bounds.midY)) }

    func zoomToFit() {
        needsFit = false
        guard bounds.width > 0, bounds.height > 0 else { return }
        let box = document?.bounds(ofPage: page) ?? CGRect(x: -20, y: -15, width: 40, height: 30)
        let pad: CGFloat = 3
        let target = max((box.width + 2 * pad) / bounds.width, (box.height + 2 * pad) / bounds.height)
        unitsPerPoint = min(max(target, minUnitsPerPoint), maxUnitsPerPoint)
        origin = CGPoint(x: box.midX - bounds.width * unitsPerPoint / 2,
                         y: box.midY + bounds.height * unitsPerPoint / 2)
        needsDisplay = true
    }

    /// Zoom by `factor` (>1 closer) keeping the world point under `p` still.
    func zoom(by factor: CGFloat, at p: CGPoint) {
        let world = worldPoint(p)
        unitsPerPoint = min(max(unitsPerPoint / factor, minUnitsPerPoint), maxUnitsPerPoint)
        origin = CGPoint(x: world.x - p.x * unitsPerPoint, y: world.y + p.y * unitsPerPoint)
        needsDisplay = true
    }

    func zoomAtCenter(by factor: CGFloat) { zoom(by: factor, at: CGPoint(x: bounds.midX, y: bounds.midY)) }

    func pan(byPoints dx: CGFloat, _ dy: CGFloat) {
        origin.x -= dx * unitsPerPoint
        origin.y += dy * unitsPerPoint
        needsDisplay = true
    }

    // MARK: Pointer

    override func scrollWheel(with event: NSEvent) {
        if clMode { clScrollWheel(event); return }
        let p = convert(event.locationInWindow, from: nil)
        // A trackpad (precise deltas) moves around; a wheel mouse, or Cmd with
        // anything, zooms at the pointer.
        if event.hasPreciseScrollingDeltas && !event.modifierFlags.contains(.command) {
            pan(byPoints: event.scrollingDeltaX, event.scrollingDeltaY)
        } else {
            let dy = event.hasPreciseScrollingDeltas ? event.scrollingDeltaY / 40 : event.scrollingDeltaY / 4
            zoom(by: pow(1.25, dy), at: p)
        }
    }

    override func magnify(with event: NSEvent) {
        zoom(by: 1 + event.magnification, at: convert(event.locationInWindow, from: nil))
    }

    override func smartMagnify(with event: NSEvent) { zoomToFit() }

    override func mouseDown(with event: NSEvent) {
        hideWireTag()
        if let d = document, cl_edit_hover_clear(d.handle) { needsDisplay = true }
        window?.makeFirstResponder(self)
        controller?.onActivate?()
        let p = convert(event.locationInWindow, from: nil)
        if clMode, clMouseDown(event, at: p) { return }
        // Space or Cmd held: move around instead of editing.
        if spaceDown || event.modifierFlags.contains(.command) {
            drag = .pan(last: p)
            pannedWhileSpaceDown = true
            NSCursor.closedHand.set()
            return
        }
        guard let document else { return }
        var mods: CoreDocument.Modifiers = []
        if event.modifierFlags.contains(.shift) { mods.insert(.shift) }
        if event.modifierFlags.contains(.option) { mods.insert(.option) }
        let what = document.press(page: page, at: worldPoint(p), modifiers: mods, unitsPerPoint: unitsPerPoint)
        if event.clickCount == 2 {
            if what == .box { zoomToFit() } else { controller?.showSettings() }
        }
        drag = .edit
        needsDisplay = true
        controller?.selectionChanged()
    }

    override func mouseDragged(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        controller?.pointerMoved(worldPoint(p))
        switch drag {
        case .pan(let last):
            pan(byPoints: p.x - last.x, p.y - last.y)
            drag = .pan(last: p)
        case .edit:
            document?.drag(to: worldPoint(p))
            needsDisplay = true
        case .none:
            break
        }
    }

    override func mouseUp(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        if clMode, case .edit = drag, let box = document?.selectionBox { controller?.fadeOutDragBox(box) }
        if case .edit = drag {
            document?.release(at: worldPoint(p))
            controller?.edited()
        }
        drag = .none
        NSCursor.arrow.set()
        needsDisplay = true
    }

    // Right or middle drag moves around.
    override func otherMouseDown(with event: NSEvent) { drag = .pan(last: convert(event.locationInWindow, from: nil)) }
    override func otherMouseDragged(with event: NSEvent) { mouseDragged(with: event) }
    override func otherMouseUp(with event: NSEvent) { drag = .none }
    // Right-click: a menu for what's under the pointer, as in the wx app
    // (a connected pin's disconnect, a wire's straighten, a gate's settings).
    override func rightMouseDown(with event: NSEvent) {
        controller?.onActivate?()
        if clMode, clRightMouseDown(event) { return }
        guard let document, let controller else { return }
        let p = worldPoint(convert(event.locationInWindow, from: nil))
        let menu = NSMenu()
        func item(_ title: String, _ action: @escaping () -> Void) {
            let i = NSMenuItem(title: title, action: #selector(MenuAction.run), keyEquivalent: "")
            let target = MenuAction(action)
            i.target = target
            i.representedObject = target
            menu.addItem(i)
        }
        switch document.contextTarget(page: page, at: p, unitsPerPoint: unitsPerPoint) {
        case .pin:
            item("Disconnect") { [weak self] in
                guard let self else { return }
                document.disconnectPin(page: self.page, at: p, unitsPerPoint: self.unitsPerPoint)
                controller.edited()
            }
        case .wire:
            item("Straighten Route") { controller.straighten() }
            menu.addItem(.separator())
            item("Delete") { controller.deleteSelection() }
        case .gate:
            item("Settings…") { controller.showSettings() }
            item("Rotate") { controller.rotate() }
            item("Straighten Its Wires") { controller.straighten() }
            menu.addItem(.separator())
            item("Delete") { controller.deleteSelection() }
        case .nothing:
            item("Select All") { controller.selectAll() }
            item("Paste") { controller.paste() }
        }
        controller.selectionChanged()
        needsDisplay = true
        NSMenu.popUpContextMenu(menu, with: event, for: self)
    }

    // MARK: Keys

    override func keyDown(with event: NSEvent) {
        if clMode, clKeyDown(event) { return }
        let plain = event.modifierFlags.intersection([.command, .option, .control]).isEmpty
        let shift = event.modifierFlags.contains(.shift)
        guard plain, let controller else { return super.keyDown(with: event) }
        // Tidy Up on show: Return keeps it, Escape puts it back, Tab tries the
        // other mode. Anything else keeps it and carries on.
        if controller.tidyActive {
            switch event.keyCode {
            case 36, 76: controller.endTidy(keep: true); return
            case 53: controller.endTidy(keep: false); return
            case 48: controller.switchTidyMode(); return
            default: controller.endTidy(keep: true)
            }
        }
        if event.keyCode == 53, document?.isConnecting == true {
            document?.cancelGesture(); needsDisplay = true; return
        }
        switch event.keyCode {
        case 49:   // space: held, it pans with a drag; tapped, it runs/pauses
            if !event.isARepeat { spaceDown = true; pannedWhileSpaceDown = false; NSCursor.openHand.set() }
        case 51, 117:   // delete, forward delete
            controller.deleteSelection()
        case 53:   // escape
            if case .edit = drag { document?.cancelGesture(); drag = .none } else { controller.selectNone() }
            needsDisplay = true
        case 123: controller.nudge(dx: shift ? -2.5 : -0.5, dy: 0)
        case 124: controller.nudge(dx: shift ? 2.5 : 0.5, dy: 0)
        case 125: controller.nudge(dx: 0, dy: shift ? -2.5 : -0.5)
        case 126: controller.nudge(dx: 0, dy: shift ? 2.5 : 0.5)
        default:
            switch event.charactersIgnoringModifiers?.lowercased() {
            case "r": controller.rotate()
            case "s": if shift { controller.tidy() } else { controller.straighten() }
            case "t" where !shift: controller.makeTruthTable()
            default: super.keyDown(with: event)
            }
        }
    }

    override func keyUp(with event: NSEvent) {
        if event.keyCode == 49 {
            spaceDown = false
            NSCursor.arrow.set()
            if clMode { clSpaceTapped(panned: pannedWhileSpaceDown); return }
            if !pannedWhileSpaceDown { controller?.toggleRunning() }
        } else {
            super.keyUp(with: event)
        }
    }

    // MARK: Dropping gates from the palette

    override func draggingEntered(_ sender: NSDraggingInfo) -> NSDragOperation {
        let s = sender.draggingPasteboard.string(forType: .string) ?? ""
        return s.hasPrefix(GatePayload.prefix) ? .copy : []
    }

    override func performDragOperation(_ sender: NSDraggingInfo) -> Bool {
        guard let s = sender.draggingPasteboard.string(forType: .string),
              s.hasPrefix(GatePayload.prefix) else { return false }
        let p = convert(sender.draggingLocation, from: nil)
        controller?.addGate(String(s.dropFirst(GatePayload.prefix.count)), at: worldPoint(p))
        return true
    }

    override func setFrameSize(_ newSize: NSSize) {
        // Keep the middle of the view where it was as the window resizes.
        let old = bounds.size
        super.setFrameSize(newSize)
        if old.width > 0 && !needsFit {
            origin.x -= (newSize.width - old.width) / 2 * unitsPerPoint
            origin.y += (newSize.height - old.height) / 2 * unitsPerPoint
        }
    }
}

/// Runs a closure from an NSMenuItem.
final class MenuAction: NSObject {
    private let action: () -> Void
    init(_ action: @escaping () -> Void) { self.action = action }
    @objc func run() { action() }
}

/// What a palette tile puts on the drag pasteboard.
enum GatePayload {
    static let prefix = "cedarlogic-gate:"
}

/// One window's canvas, edits and simulation: the toolbar, menus, palette and
/// inspector all go through here to reach the canvas in front, and it runs the
/// simulation clock while the window is open.
/// What changes as the pointer and the camera move: its own object, watched
/// only by the status bar, the zoom readout and the minimap.
@MainActor
final class CanvasStatus: ObservableObject {
    @Published var version = 0
}

@MainActor
final class CanvasController: ObservableObject {
    /// The canvas in the frontmost circuit window.
    static weak var front: CanvasController?
    weak var view: CircuitCanvasNSView?
    private(set) var document: CoreDocument?
    var page = 0

    @Published private(set) var isRunning = true
    @Published var stepMs = 25 { didSet { document?.stepMs = stepMs } }
    /// Bumped whenever the selection may have changed (the inspector reloads).
    @Published private(set) var selectionVersion = 0
    /// Bumped after every edit (menus re-read undo names).
    @Published private(set) var editVersion = 0
    @Published var settingsRequested = false
    /// Bumped as the oscilloscope records (throttled), so its panel redraws.
    @Published private(set) var scopeVersion = 0
    @Published var showScope = false
    private var lastScopeBump = CACurrentMediaTime()
    func scopeChanged() { scopeVersion += 1 }

    private var timer: Timer?
    private var lastTick = CACurrentMediaTime()
    private var lastStatus = CACurrentMediaTime()
    private var lastSaveCheck = CACurrentMediaTime()
    private var lastSeenSave: Date?

    /// When the file on disk changes from a save, a circuit from Your
    /// Circuits keeps a version.
    private func noticeSaves() {
        if !adopted, let w = view?.window, let d = NSDocumentController.shared.document(for: w) {
            adopted = adoptIntoLibrary(d)
        }
        guard let w = view?.window, let d = NSDocumentController.shared.document(for: w),
              let date = d.fileModificationDate else { return }
        if let last = lastSeenSave, date != last, !d.isDocumentEdited {
            // ⌘S keeps a version on the spot (as in wx); saving on its own
            // doesn't say so, and keeps one only now and then (Library).
            let explicit = CACurrentMediaTime() - explicitSaveAt < 10
            Library.noteSaved(d.fileURL, explicit: explicit)
            if explicit { note("Saved, and a version was kept.") }
            explicitSaveAt = 0
        }
        lastSeenSave = date
    }
    fileprivate(set) var explicitSaveAt: CFTimeInterval = 0
    private var adopted = false

    /// Every circuit lives in Your Circuits: a new one joins it once there's
    /// something on it (so an empty ⌘N leaves nothing behind), and a .cdl
    /// file opened from elsewhere carries on as a copy there (the file
    /// itself is left alone; File ▸ Export gets a file out). Opening the
    /// same file again finds the copy. Old versions' read-only copies (in
    /// the temporary folder) stay as they are. False to try again later.
    private func adoptIntoLibrary(_ d: NSDocument) -> Bool {
        guard let document else { return false }
        let type = d.fileType ?? UTType.cedarLogicCircuit.identifier
        if let url = d.fileURL {
            let temp = FileManager.default.temporaryDirectory.standardizedFileURL.path
            if Library.contains(url) || url.standardizedFileURL.path.hasPrefix(temp) { return true }
            if let existing = Library.imported(from: url) {
                d.close()
                Library.open(existing.circuit)
                return true
            }
            guard let item = try? Library.create(named: url.deletingPathExtension().lastPathComponent,
                                                 text: document.saveText(), source: url) else { return true }
            d.save(to: item.circuit, ofType: type, for: .saveAsOperation) { _ in }
        } else {
            guard document.hasGates else { return false }
            let name = document.libraryName ?? "Untitled Circuit"
            guard let item = try? Library.create(named: name, text: document.saveText()) else { return true }
            d.save(to: item.circuit, ofType: type, for: .saveAsOperation) { _ in }
        }
        objectWillChange.send()   // the title reads the library name
        return true
    }

    /// Saving as you go, the way Google Docs does: a couple of seconds after
    /// the last change, the circuit is written to its file.
    private var autosaveWork: DispatchWorkItem?
    private func scheduleAutosave() {
        autosaveWork?.cancel()
        let work = DispatchWorkItem { [weak self] in
            guard let w = self?.sheetHost.view?.window, let d = NSDocumentController.shared.document(for: w),
                  d.isDocumentEdited, d.fileURL != nil else { return }
            d.autosave(withImplicitCancellability: true) { _ in }
        }
        autosaveWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 2, execute: work)
    }

    /// Whether this controller runs the simulation clock (the second side of
    /// a split view doesn't: its partner does, and redraws it).
    var drivesClock = true

    func attach(_ document: CoreDocument) {
        guard self.document !== document else { return }
        self.document = document
        isRunning = document.isRunning
        stepMs = document.stepMs
        lastPageCount = document.pageCount
        // The second side of a split runs no simulation (its partner does),
        // but still needs the ticks for its own view: the status bar, eased
        // zooms, fades and Simulation View's moving dashes.
        startClock()
    }

    // MARK: The CedarLogic interface

    /// Simulation View: the dark live presentation; no editing while it's on.
    @Published var simView = false {
        didSet {
            guard simView != oldValue else { return }
            if simView {
                document?.cancelGesture()
                selectNone()
                if !isRunning { setRunning(true) }   // "Run" means run
            }
            redraw()
        }
    }
    /// Locked: parts can be clicked (switches, keypads) but nothing edited.
    @Published var locked = false
    /// Where the pointer is on the page, for the status bar.
    private(set) var pointer = CGPoint.zero
    /// Bumped (at most ten times a second) when the status bar, the minimap
    /// or the toolbar's zoom should re-read. Its own object, so a moving
    /// pointer redraws those and nothing else in the window.
    let status = CanvasStatus()
    private var statusDirty = false

    // Animations (the wx canvas's): a new selection's halo fades in, a
    // released drag-select box fades out, a page that appears fades its grid
    // up, and a closing tab dims away.
    private(set) var selectionChangedAt: CFTimeInterval = 0
    private var selectionSignature = ""
    private(set) var appearStart: CFTimeInterval = 0
    private(set) var closingStart: CFTimeInterval?
    private(set) var dragFadeBox: CGRect?
    private(set) var dragFadeStart: CFTimeInterval = 0
    static let selectionFadeTime = 0.13, appearTime = 0.32, dragFadeTime = 0.18, closeTime = 0.14

    var selectionFade: Double { min(1, max(0, (CACurrentMediaTime() - selectionChangedAt) / Self.selectionFadeTime)) }
    /// 0 to 1, eased out, as a page appears.
    var appearProgress: Double {
        let t = min(1, max(0, (CACurrentMediaTime() - appearStart) / Self.appearTime))
        return 1 - pow(1 - t, 3)
    }
    /// 0 while staying, heading to 1 as a tab goes (eased in).
    var closeProgress: Double {
        guard let closingStart else { return 0 }
        let t = min(1, max(0, (CACurrentMediaTime() - closingStart) / Self.closeTime))
        return t * t
    }
    var dragFadeAlpha: Double { max(0, 1 - (CACurrentMediaTime() - dragFadeStart) / Self.dragFadeTime) }

    func playAppear() { appearStart = CACurrentMediaTime(); redraw() }
    func fadeOutDragBox(_ box: CGRect) { dragFadeBox = box; dragFadeStart = CACurrentMediaTime(); redraw() }
    private var animating: Bool {
        let now = CACurrentMediaTime()
        return now - selectionChangedAt < Self.selectionFadeTime || now - appearStart < Self.appearTime ||
            (dragFadeBox != nil && now - dragFadeStart < Self.dragFadeTime) || closingStart != nil ||
            (view?.zoomAnimating ?? false)
    }
    /// Simulation View's marching dashes, in points travelled.
    private(set) var flowPhase: Double = 0
    @Published var showQuickAdd = false
    @Published var showShortcuts = false
    @Published var showExportImage = false
    @Published var ramGate: Int?
    /// A one-line message for the status bar ("Saved.", "Nothing to paste.").
    @Published var statusMessage = ""
    /// Called every clock tick (the guided tour watches from here).
    var onTick: (() -> Void)?

    func pointerMoved(_ world: CGPoint) { pointer = world; statusDirty = true }

    /// Something was refused because the circuit is locked: the lock badge
    /// on the canvas gives a little bounce so it's clear why.
    @Published private(set) var lockNudgeCount = 0
    func lockNudge() { lockNudgeCount += 1 }

    // MARK: Split view
    // The second side of a split has its own controller (drivesClock false);
    // the window's sheets live on the first, which shows them for either.

    /// The first side: true when the second side was clicked last.
    @Published var splitFocus = false
    /// Called when this side is clicked, so the window can make it the active one.
    var onActivate: (() -> Void)?
    var isSecondary: Bool { !drivesClock }
    /// The controller whose window shows sheets (the first side).
    var sheetHost: CanvasController { isSecondary ? (partner ?? self) : self }
    /// The side commands go to: this one, or the second side when it's active.
    var routed: CanvasController { splitFocus ? (partner ?? self) : self }
    /// What the sheets act on: the side that asked.
    var quickAddTarget: CanvasController?
    var exportPage = 0
    var settingsGate: Int?

    /// Opens a window by id (set by the window, which has SwiftUI's openWindow).
    var openWindow: ((String) -> Void)?

    /// Every command, whichever way it arrives: the menus, the canvas's own
    /// keys (Settings > Shortcuts), the toolbar and the shortcut list.
    func perform(_ a: ShortcutAction) {
        // In a split, the side you last clicked takes the page commands.
        if splitFocus, let p = partner, a.actsOnPage { p.perform(a); return }
        switch a {
        case .newCircuit: NSDocumentController.shared.newDocument(nil)
        case .openLibrary: openWindow?("library")
        case .importFile: NSDocumentController.shared.openDocument(nil)
        case .save:
            sheetHost.explicitSaveAt = CACurrentMediaTime()
            NSApp.sendAction(#selector(NSDocument.save(_:)), to: nil, from: nil)
        case .exportImage: sheetHost.exportPage = page; sheetHost.showExportImage = true
        // A copy out of Your Circuits; the circuit itself stays there.
        case .exportFile: NSApp.sendAction(#selector(NSDocument.saveTo(_:)), to: nil, from: nil)
        case .print: printPage()
        case .undo: undo()
        case .redo: redo()
        case .cut, .quickCut: if canEdit { cut() } else { lockNudge() }
        case .copy, .quickCopy: copy()
        case .paste, .quickPaste: if canEdit { pasteFloating() } else { lockNudge() }
        case .duplicate, .quickDuplicate: if canEdit { duplicateFloating() } else { lockNudge() }
        case .selectAll: selectAll()
        case .addGate: if canEdit { sheetHost.quickAddTarget = self; sheetHost.showQuickAdd = true } else { lockNudge() }
        case .rotate: if canEdit { rotate() } else { lockNudge() }
        case .straighten: if canEdit { straighten() } else { lockNudge() }
        case .tidy: if canEdit { tidy() } else { lockNudge() }
        case .zoomIn: zoomIn()
        case .zoomOut: zoomOut()
        case .zoomFit: zoomToFit()
        case .zoomActual: zoomActual()
        case .focusMode: NotificationCenter.default.post(name: .clToggleFocusMode, object: self)
        case .simView: simView.toggle()
        case .step: stepOnce()
        case .truthTable: makeTruthTable()
        case .checkCircuit: makeTruthTable(check: true)
        case .find: openFind()
        case .findNext: if findActive { findStep(1) } else { openFind() }
        case .findPrevious: if findActive { findStep(-1) } else { openFind() }
        case .buildFormula: if canEdit { sheetHost.formulaRequest = FormulaRequest(text: "") } else { lockNudge() }
        case .scope: showScope.toggle()
        case .lock: locked.toggle()
        case .newTab: newPage()
        case .closeTab:
            if (document?.pageCount ?? 1) > 1 { closePage(page) } else { view?.window?.performClose(nil) }
        case .reopenTab: reopenPage()
        case .splitView: NotificationCenter.default.post(name: .clSplit, object: self)
        case .switchPane: NotificationCenter.default.post(name: .clSwitchPane, object: self)
        case .closeSplit: NotificationCenter.default.post(name: .clCloseSplit, object: self)
        case .nextTab: cyclePage(1)
        case .previousTab: cyclePage(-1)
        case .shortcuts: sheetHost.showShortcuts = true
        case .darkMode: Prefs.shared.dark.toggle()
        case .feedback: FeedbackModel.aim(); openWindow?("feedback")
        }
    }
    /// Where the pointer is on the page right now. Asked of the window, not
    /// remembered: while a gate is dragged out of the palette the canvas
    /// sees no mouse events, and what it last saw was the edge it left by.
    var pointerOrCenter: CGPoint {
        guard let view, let w = view.window else { return pointer }
        return view.worldPoint(view.convert(w.mouseLocationOutsideOfEventStream, from: nil))
    }
    /// Shift+1...9 asks the palette for a category.
    @Published var paletteCategoryRequest: Int?
    /// The pages in this side's tab strip, in order (set in a split).
    var paneOrder: (() -> [Int]?)?
    func cyclePage(_ delta: Int) {
        guard let document else { return }
        let order = (paneOrder?() ?? nil) ?? Array(0..<document.pageCount)
        guard order.count > 1 else { return }
        let n = order.count
        let at = order.firstIndex(of: page) ?? 0
        pageRequest = order[((at + delta) % n + n) % n]
    }
    func cameraMoved() { statusDirty = true }
    func note(_ message: String) { statusMessage = message }

    var zoomPercent: Int {
        guard let upp = view?.unitsPerPoint, upp > 0 else { return 100 }
        return Int((100 * 0.1 / upp).rounded())
    }
    func zoomActual() { view?.animateZoom(by: (view?.unitsPerPoint ?? 0.1) / 0.1) }

    var canEdit: Bool { !locked && !simView }

    /// Where a paste or a new gate appears: at the pointer when it's over the
    /// canvas, else the middle of the view.
    private var placePoint: CGPoint? {
        guard let view else { return nil }
        if let w = view.window {
            let p = view.convert(w.mouseLocationOutsideOfEventStream, from: nil)
            if view.bounds.contains(p) { return view.worldPoint(p) }
        }
        return view.visibleCenter
    }

    /// Put the selection on the pointer until the next click drops it.
    private func floatSelection(at point: CGPoint? = nil) {
        guard let document, let at = point ?? placePoint else { return }
        if cl_edit_float_begin(document.handle, Int32(page), at.x, at.y) {
            view?.window?.makeFirstResponder(view)
            NSCursor.closedHand.set()
        }
        edited()
    }
    var isFloating: Bool { document.map { cl_edit_is_floating($0.handle) } ?? false }

    /// Escape while something floats: it was never placed, so take it back.
    func cancelFloating() {
        guard let document, isFloating else { return }
        document.cancelGesture()
        undo()
        NSCursor.arrow.set()
        edited()
    }

    /// A new gate on the pointer (or at `world`, when a palette tile is
    /// being dragged there), following it until a click drops it.
    @discardableResult
    func addGateFloating(_ name: String, at world: CGPoint? = nil) -> Bool {
        guard canEdit else { lockNudge(); return false }
        guard let document, let at = world ?? placePoint else { return false }
        if name.hasPrefix(MyParts.prefix) {
            // A saved part: its gates and wires, pasted where the pointer is.
            guard let text = MyParts.shared.part(named: name)?.text,
                  document.paste(text, page: page, at: at, shift: true) != nil else { return false }
            floatSelection(at: at)
            return true
        }
        guard document.addGate(name, page: page, at: at) else { return false }
        floatSelection(at: at)
        return true
    }

    func pasteFloating() {
        guard canEdit, let document, let text = NSPasteboard.general.string(forType: .string), let at = placePoint else { return }
        let shift = NSEvent.modifierFlags.contains(.shift)
        guard let back = document.paste(text, page: page, at: at, shift: shift) else { note("Nothing to paste."); return }
        if !back.isEmpty {
            NSPasteboard.general.clearContents()
            NSPasteboard.general.setString(back, forType: .string)
        }
        floatSelection()
    }

    func duplicateFloating() {
        guard canEdit, let document, let at = placePoint else { return }
        let text = document.copySelection(page: page)
        guard !text.isEmpty else { return }
        if Prefs.shared.duplicateUsesClipboard {
            NSPasteboard.general.clearContents()
            NSPasteboard.general.setString(text, forType: .string)
        }
        _ = document.paste(text, page: page, at: at, shift: false)
        floatSelection()
    }

    /// Connects free pins to what's unambiguously next to them. Quietly:
    /// after a drop, which may already have connected them.
    func connectNearby(quietly: Bool = false) {
        guard canEdit, let document, let upp = view?.unitsPerPoint else { return }
        let n = Int(cl_edit_connect_nearby(document.handle, Int32(page), upp))
        if n > 0 { note("Connected \(n) pin\(n == 1 ? "" : "s").") }
        else if !quietly { note("Nothing close enough to connect.") }
        edited()
    }

    /// The gate chosen in Quick Add (A). As in wx, it appears on the pointer
    /// at the next mouse move over a canvas, not at once where the pointer
    /// happened to be.
    static var pendingGate: String?
    func addGateOnNextMove(_ name: String) {
        guard canEdit else { lockNudge(); return }
        Self.pendingGate = name
        view?.window?.makeFirstResponder(view)
    }
    /// The pointer moved over this canvas: a gate waiting from Quick Add
    /// appears there.
    @discardableResult
    func placePendingGate(at world: CGPoint) -> Bool {
        guard let name = Self.pendingGate else { return false }
        Self.pendingGate = nil
        onActivate?()
        return addGateFloating(name, at: world)
    }

    // Pages, as tabs: closing is an undo step, and Reopen undoes it. The tab
    // on screen dims away first (140 ms), as in the wx app.
    func closePage(_ i: Int) {
        guard let document, document.pageCount > 1, closingStart == nil else { return }
        // A tab with work on it asks first (wx CloseTabCanvas).
        if cl_document_gate_count(document.handle, Int32(i)) > 0 &&
            !Self.askYesNo("Close Tab", "All work on this tab will be lost. Would you like to close it?", dark: Prefs.shared.dark) {
            return
        }
        if i == page {
            closingStart = CACurrentMediaTime()
            redraw()
            DispatchQueue.main.asyncAfter(deadline: .now() + Self.closeTime) { [weak self] in
                self?.closingStart = nil
                self?.closePageNow(i)
            }
        } else {
            closePageNow(i)
        }
    }

    private func closePageNow(_ i: Int) {
        guard let document, document.pageCount > 1 else { return }
        selectNone()
        if cl_document_close_page(document.handle, Int32(i)) {
            lastPageCount = document.pageCount
            document.objectWillChange.send()
            pageRequestAfterClose = true
            pageRequest = min(Int(cl_document_page_to_show(document.handle)), document.pageCount - 1)
            edited()
        }
    }
    /// The system alert with Yes and No, and the Y and N keys (wx MacAskYesNo).
    static func askYesNo(_ title: String, _ message: String, dark: Bool) -> Bool {
        let alert = NSAlert()
        alert.messageText = title
        alert.informativeText = message
        alert.alertStyle = .warning
        alert.window.appearance = NSAppearance(named: dark ? .darkAqua : .aqua)
        let yes = alert.addButton(withTitle: "Yes")
        let no = alert.addButton(withTitle: "No")
        yes.keyEquivalent = "\r"
        no.keyEquivalent = "\u{1b}"
        let monitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { e in
            switch e.charactersIgnoringModifiers?.lowercased() {
            case "y": NSApp.stopModal(withCode: .alertFirstButtonReturn); return nil
            case "n": NSApp.stopModal(withCode: .alertSecondButtonReturn); return nil
            default: return e
            }
        }
        let response = alert.runModal()
        if let monitor { NSEvent.removeMonitor(monitor) }
        return response == .alertFirstButtonReturn
    }

    var canReopenPage: Bool { document.map { cl_edit_undo_is_close_page($0.handle) } ?? false }
    func reopenPage() {
        guard canReopenPage else { note("No closed tab to reopen."); NSSound.beep(); return }
        undo()
        playAppear()
        note("Reopened the closed tab.")
    }
    /// Set in a split: where this side's new tabs go.
    var onNewPage: ((Int) -> Void)?
    /// The window's tabs for Ctrl+Tab (set by the CedarLogic window).
    var tabSwitch: (() -> TabSwitchOffer?)?
    func newPage() {
        guard let i = addBlankPage() else { return }
        if let onNewPage { onNewPage(i) } else { pageRequest = i }
        playAppear()
    }
    /// A new, empty page named "Page N", without showing it.
    @discardableResult
    func addBlankPage() -> Int? {
        guard let document else { return nil }
        let taken = Set((0..<document.pageCount).map { document.pageName($0) })
        let i = document.addPage()
        var n = document.pageCount
        while taken.contains("Page \(n)") { n += 1 }
        document.renamePage(i, to: "Page \(n)")
        document.objectWillChange.send()
        lastPageCount = document.pageCount
        markEdited()
        return i
    }
    func movePage(from: Int, to: Int) {
        guard let document else { return }
        // A tab without a name of its own is called by its place ("Page 2");
        // pin those names first, so moving a tab doesn't rename the others.
        for i in 0..<document.pageCount where String(cString: cl_document_page_name(document.handle, Int32(i))).isEmpty {
            document.renamePage(i, to: document.pageName(i))
        }
        cl_document_move_page(document.handle, Int32(from), Int32(to))
        document.objectWillChange.send()
        markEdited()
    }

    func redraw() { view?.redrawIfSeen(); partner?.view?.redrawIfSeen() }

    /// A new selection's halo fades in; clicking what's already selected
    /// doesn't restart it.
    private func noteSelection() {
        guard let document else { return }
        let h = document.handle
        let sig = "\(cl_edit_selected_gate_count(h, Int32(page)))/\(cl_edit_selected_wire_count(h, Int32(page)))/\(cl_edit_single_gate(h, Int32(page)))"
        if sig != selectionSignature {
            selectionSignature = sig
            if sig != "0/0/-1" { selectionChangedAt = CACurrentMediaTime(); redraw() }
        }
    }
    /// The other side of a split view, redrawn with this one.
    weak var partner: CanvasController?
    /// Set to ask the window to show a page (after closing or reopening one).
    @Published var pageRequest: Int?
    /// The request is the neighbour to show after a tab closed (not a tab
    /// being opened or reopened).
    var pageRequestAfterClose = false
    func selectionChanged() { selectionVersion += 1; noteSelection() }
    func editsChanged() { editVersion += 1 }
    func edited() { redraw(); selectionChanged(); editsChanged(); syncTidy(); mirrorNewSteps(); sheetHost.scheduleAutosave() }

    // MARK: macOS undo
    // The engine keeps the real undo stack (the wx app's commands). Each step
    // it gains is mirrored into the window's UndoManager, whose undo and redo
    // call back into the engine -- so Cmd-Z, the Edit menu, the edited dot and
    // autosave are all macOS's own.

    weak var undoManager: UndoManager?
    /// Engine steps registered with the undo manager: kept on the document,
    /// so the two sides of a split view share one count.
    private var mirrored: Int {
        get { document?.mirroredUndo ?? 0 }
        set { document?.mirroredUndo = newValue }
    }

    private func mirrorNewSteps() {
        guard let document else { return }
        let n = document.undoCount
        if n < mirrored {
            // The engine dropped history (a page was deleted): so must macOS.
            undoManager?.removeAllActions()
            mirrored = n
        }
        while mirrored < n {
            let name = document.undoName
            undoManager?.registerUndo(withTarget: self) { $0.engineUndo() }
            undoManager?.setActionName(name)
            mirrored += 1
        }
    }

    private func engineUndo() {
        guard let document, document.undo() else { return }
        mirrored -= 1
        let name = document.redoName
        undoManager?.registerUndo(withTarget: self) { $0.engineRedo() }
        undoManager?.setActionName(name)
        refreshAfterHistory()
    }

    private func engineRedo() {
        guard let document, document.redo() else { return }
        mirrored += 1
        let name = document.undoName
        undoManager?.registerUndo(withTarget: self) { $0.engineUndo() }
        undoManager?.setActionName(name)
        refreshAfterHistory()
    }

    private func refreshAfterHistory() {
        // An undo or redo that closed or reopened a page shows that page.
        if let document {
            let show = Int(cl_document_page_to_show(document.handle))
            if document.pageCount != lastPageCount {
                lastPageCount = document.pageCount
                document.objectWillChange.send()
                if show >= 0 { pageRequest = min(show, document.pageCount - 1) }
            }
        }
        redraw(); selectionChanged(); editsChanged(); syncTidy()
    }
    /// The page count last seen, kept on the document so both sides of a
    /// split agree on it.
    private var lastPageCount: Int {
        get { document?.shownPageCount ?? 1 }
        set { document?.shownPageCount = newValue }
    }

    /// For changes that aren't undo steps (pages added, renamed or removed):
    /// tell the document it changed so it's saved.
    func markEdited() {
        if let window = view?.window, let doc = NSDocumentController.shared.document(for: window) {
            doc.updateChangeCount(.changeDone)
        }
        editsChanged()
        sheetHost.scheduleAutosave()
    }

    // Camera (eased in the CedarLogic interface, as the wx app's zoom is)
    func zoomIn() { if let v = view, v.clMode { v.animateZoom(by: 1 / 0.75) } else { view?.zoomAtCenter(by: 1.25) } }
    func zoomOut() { if let v = view, v.clMode { v.animateZoom(by: 0.75) } else { view?.zoomAtCenter(by: 0.8) } }
    func zoomToFit() { if let v = view, v.clMode { v.animateZoomToFit() } else { view?.zoomToFit() } }

    // Editing
    var hasSelection: Bool { document?.hasSelection(page: page) ?? false }
    var hasGateSelection: Bool { document?.hasGateSelection(page: page) ?? false }
    var canUndo: Bool { undoManager?.canUndo ?? false }
    var canRedo: Bool { undoManager?.canRedo ?? false }

    func undo() { undoManager?.undo() }
    func redo() { undoManager?.redo() }
    func selectAll() { document?.selectAll(page: page); redraw(); selectionChanged() }
    func selectNone() { document?.selectNone(page: page); redraw(); selectionChanged() }
    func deleteSelection() { document?.deleteSelection(page: page); edited() }
    func rotate() { document?.rotateSelection(page: page); edited() }
    func nudge(dx: CGFloat, dy: CGFloat) { document?.nudge(page: page, dx: dx, dy: dy); edited() }
    func straighten() { document?.straighten(page: page); edited() }

    // MARK: Truth tables, export, print

    @Published var truthTable: TruthTable?
    @Published var truthTableProblem: String?
    @Published var formulaRequest: FormulaRequest?

    // MARK: Find

    struct FindHit: Equatable { let page: Int; let gate: Int; let point: CGPoint; let text: String; let kind: String }
    @Published var findActive = false
    @Published var findFocusTick = 0
    @Published var findQuery = ""
    @Published private(set) var findHits: [FindHit] = []
    @Published private(set) var findTotal = 0
    @Published private(set) var findIndex = 0

    /// Opens the find bar; with a label or a TO/FROM selected, looks for its name.
    func openFind() {
        if let document {
            let g = cl_edit_single_gate(document.handle, Int32(page))
            if g > 0, let name = cl_gate_find_name(document.handle, g).map({ String(cString: $0) }), !name.isEmpty {
                findQuery = name
            }
        }
        findActive = true
        findFocusTick += 1
        // Out of the canvas, so typing goes to the find field at once.
        view?.window?.makeFirstResponder(nil)
        runFind(jump: !findQuery.isEmpty)
    }

    func closeFind() {
        findActive = false
        view?.window?.makeFirstResponder(view)
    }

    func runFind(jump: Bool) {
        guard let document else { return }
        var out = [CLFindResult](repeating: CLFindResult(), count: 500)
        let total = Int(cl_find(document.handle, findQuery, &out, Int32(out.count)))
        findTotal = total
        findHits = out.prefix(min(total, out.count)).map {
            FindHit(page: Int($0.page), gate: $0.gate, point: CGPoint(x: $0.x, y: $0.y),
                    text: String(cString: $0.text), kind: String(cString: $0.kind))
        }
        findIndex = 0
        if jump, !findHits.isEmpty { showFind(0) }
    }

    func findStep(_ delta: Int) {
        guard !findHits.isEmpty else { NSSound.beep(); return }
        let n = findHits.count
        findIndex = ((findIndex + delta) % n + n) % n
        showFind(findIndex)
    }

    /// To the result's page, select it, and bring it to the middle.
    private func showFind(_ i: Int) {
        guard let document, findHits.indices.contains(i) else { return }
        let hit = findHits[i]
        let go = { [weak self] in
            guard let self else { return }
            _ = cl_edit_select_gate(document.handle, Int32(hit.page), hit.gate)
            self.view?.animateCenter(on: hit.point)
            self.redraw()
            self.selectionChanged()
        }
        if hit.page != page {
            pageRequest = hit.page
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.15, execute: go)
        } else {
            go()
        }
    }

    /// The truth table's "Build This as a Circuit": once its sheet is gone.
    func buildFromTable(_ text: String) {
        sheetHost.truthTable = nil
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.35) { [weak self] in
            guard let self else { return }
            if self.canEdit { self.sheetHost.formulaRequest = FormulaRequest(text: text) } else { self.lockNudge() }
        }
    }

    /// Makes a planned circuit: on a new page named `pageName`, or to the right
    /// of what's on this one. One undo step; the new parts are left selected.
    @discardableResult
    func build(_ plan: CircuitPlan, onNewPage: Bool, pageName: String) -> Bool {
        guard let document, !plan.parts.isEmpty else { return false }
        var target = page
        var dx = 0.0, dy = 0.0
        if onNewPage {
            guard let i = addBlankPage() else { return false }
            target = i
            if !pageName.isEmpty { document.renamePage(i, to: pageName) }
        } else if let b = document.bounds(ofPage: page), b.width > 0 || b.height > 0 {
            let minX = plan.parts.map(\.x).min() ?? 0, maxY = plan.parts.map(\.y).max() ?? 0
            dx = b.maxX + 16 - minX
            dy = b.maxY - maxY
        }
        var strings: [UnsafeMutablePointer<CChar>] = []
        defer { strings.forEach { free($0) } }
        func c(_ s: String) -> UnsafePointer<CChar> { let p = strdup(s)!; strings.append(p); return UnsafePointer(p) }
        let gates = plan.parts.map { p in
            CLBuildGate(gate: c(p.gate), x: p.x + dx, y: p.y + dy, label: p.label.map(c))
        }
        let wires = plan.wires.map { w in CLBuildWire(from: Int32(w.from), fromPin: c(w.fromPin), to: Int32(w.to), toPin: c(w.toPin)) }
        let made = cl_edit_build(document.handle, Int32(target), gates, Int32(gates.count), wires, Int32(wires.count), "Build from Formula")
        guard made > 0 else { return false }
        document.objectWillChange.send()
        if onNewPage {
            if let onNewPage = self.onNewPage { onNewPage(target) } else { pageRequest = target }
        }
        edited()
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.4) { [weak self] in self?.zoomToFit() }
        return true
    }

    /// The truth table; with `check`, open at its Check tab.
    func makeTruthTable(check: Bool = false) {
        guard let document else { return }
        var error = ""
        if var table = TruthTable(document: document, page: page, error: &error) {
            // What was last checked is kept per circuit and page.
            let circuit = view?.window?.representedURL?.path ?? view?.window?.title ?? "Circuit"
            table.checkKey = circuit + "#\(page)"
            if check { UserDefaults.standard.set(TruthTableView.checkTab, forKey: "cl.truthTab") }
            sheetHost.truthTable = table
        } else {
            sheetHost.truthTableProblem = error.isEmpty ? "A truth table couldn't be made for this page." : error
        }
        redraw()   // it leaves the switches as they were, but the circuit settles again
    }

    var documentTitle: String {
        let name = document.map { $0.pageCount > 1 ? " - \($0.pageName(page))" : "" } ?? ""
        let title = (view?.window?.representedURL?.deletingPathExtension().lastPathComponent
            ?? view?.window?.title ?? "Circuit")
        return title + name
    }

    func export() {
        guard let document else { return }
        PageExport.run(document, page: page, suggestedName: documentTitle, window: view?.window)
    }

    /// A copy for an older CedarLogic (wx: Export as V2 / V1.x Compatible):
    /// 2 is the v2 XML, 1 the v1.x compatible format. The circuit you're in
    /// stays as it is.
    func exportOlder(_ format: Int32) {
        guard let document, let w = view?.window else { return }
        let panel = NSSavePanel()
        let name = (w.title as NSString).deletingPathExtension
        panel.nameFieldStringValue = "\(name) (\(format == 1 ? "v1.x" : "v2")).cdl"
        panel.allowedContentTypes = [.init(filenameExtension: "cdl") ?? .data]
        panel.canCreateDirectories = true
        panel.isExtensionHidden = false
        panel.message = format == 1 ? "A copy the oldest CedarLogic (v1.x) can open."
                                    : "A copy CedarLogic versions before 3 can open."
        panel.beginSheetModal(for: w) { r in
            guard r == .OK, let url = panel.url else { return }
            var why = [CChar](repeating: 0, count: 512)
            let rc = cl_document_export_legacy(document.handle, url.path, format, &why, Int32(why.count))
            guard rc != 0 else { return }
            let a = NSAlert()
            a.messageText = rc > 0 ? "Exported, with one thing left out" : "The circuit couldn't be exported"
            a.informativeText = String(cString: why)
            a.alertStyle = rc > 0 ? .informational : .warning
            a.beginSheetModal(for: w)
        }
    }

    func printPage() {
        guard let document else { return }
        PagePrintView.print(document, page: page, window: view?.window)
    }

    // Tidy Up. Which mode Shift-S uses is a setting; the menu offers both.
    @Published private(set) var tidyActive = false
    @Published private(set) var tidyMode = 0
    var defaultTidyMode: Int { UserDefaults.standard.integer(forKey: "tidyMode") }
    func tidy(mode: Int? = nil) {
        document?.beginTidy(page: page, mode: mode ?? defaultTidyMode)
        edited()
    }
    func endTidy(keep: Bool) { document?.endTidy(keep: keep); edited() }
    func switchTidyMode() {
        let other = 1 - tidyMode
        document?.endTidy(keep: false)
        document?.beginTidy(page: page, mode: other)
        edited()
    }
    private func syncTidy() {
        let active = document?.tidyActive ?? false
        if active != tidyActive { tidyActive = active }
        let mode = document?.tidyMode ?? 0
        if mode != tidyMode { tidyMode = mode }
    }

    func showSettings() {
        guard let g = document?.singleSelectedGate(page: page) else { return }
        sheetHost.settingsGate = g
        sheetHost.settingsRequested = true
    }

    func addGate(_ name: String, at world: CGPoint? = nil) {
        guard let document, let point = world ?? view?.visibleCenter else { return }
        if document.addGate(name, page: page, at: point) { edited() }
        view?.window?.makeFirstResponder(view)
    }

    func copy() {
        guard let text = document?.copySelection(page: page), !text.isEmpty else { return }
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
    }

    func cut() { copy(); deleteSelection() }

    /// Paste in the middle of the view.
    func paste() {
        guard let document, let text = NSPasteboard.general.string(forType: .string), let view else { return }
        let shift = NSEvent.modifierFlags.contains(.shift)
        if let back = document.paste(text, page: page, at: view.visibleCenter, shift: shift) {
            if !back.isEmpty {
                NSPasteboard.general.clearContents()
                NSPasteboard.general.setString(back, forType: .string)
            }
            edited()
        }
    }

    /// Copy and paste in one step, without touching the clipboard.
    func duplicate() {
        guard let document, let view else { return }
        let text = document.copySelection(page: page)
        guard !text.isEmpty else { return }
        let c = view.visibleCenter
        _ = document.paste(text, page: page, at: CGPoint(x: c.x + 1, y: c.y - 1), shift: false)
        edited()
    }

    // Simulation
    func toggleRunning() { setRunning(!isRunning) }

    func setRunning(_ running: Bool) {
        document?.isRunning = running
        isRunning = running
        lastTick = CACurrentMediaTime()
    }

    /// One step; pauses first, since stepping a running circuit means little.
    func stepOnce() {
        if isRunning { setRunning(false) }
        document?.stepOnce()
        redraw()
        scopeChanged()
    }

    /// 60 times a second, hand the engine the time that passed. The engine
    /// turns it into steps at the chosen speed and says if anything changed.
    private func startClock() {
        timer?.invalidate()
        lastTick = CACurrentMediaTime()
        let t = Timer(timeInterval: 1.0 / 60.0, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.tick() }
        }
        RunLoop.main.add(t, forMode: .common)   // keeps running during scrolls and drags
        timer = t
    }

    private func tick() {
        let now = CACurrentMediaTime()
        let elapsed = (now - lastTick) * 1000
        lastTick = now
        if statusDirty && now - lastStatus > 0.1 { statusDirty = false; lastStatus = now; status.version += 1 }
        view?.stepZoomAnimation()
        if animating { redraw() }
        if dragFadeBox != nil && now - dragFadeStart >= Self.dragFadeTime { dragFadeBox = nil; redraw() }
        if drivesClock && now - lastSaveCheck > 2 { lastSaveCheck = now; noticeSaves() }
        onTick?()
        // The dashes march at the simulation's speed: 40 points a second at
        // 25 ms a step, faster as steps get shorter.
        // The second side follows its partner's run/pause.
        if simView && (drivesClock ? isRunning : (partner?.isRunning ?? isRunning)) {
            let pps = min(240, max(8, 40 * (25.0 / Double(max(stepMs, 1))).squareRoot()))
            flowPhase += min(0.05, elapsed / 1000) * pps
            redraw()
        }
        guard drivesClock, let document, isRunning else { return }
        let result = document.tick(elapsedMs: elapsed)
        if result.changed {
            redraw()
            if showScope && now - lastScopeBump > 1.0 / 15 { lastScopeBump = now; scopeChanged() }
        }
        if result.paused { isRunning = false }
    }

    deinit { timer?.invalidate() }
}

struct CanvasView: NSViewRepresentable {
    let document: CoreDocument
    let page: Int
    let theme: Theme
    let controller: CanvasController
    var clMode = false

    func makeNSView(context: Context) -> CircuitCanvasNSView {
        let view = CircuitCanvasNSView(frame: .zero)
        view.document = document
        view.page = page
        view.pageKey = document.pageID(page)
        if let cam = document.cameras[view.pageKey] {
            // Shown before (in the other side of a split, say): as it was.
            view.pendingCamera = cam
        }
        view.theme = theme
        view.controller = controller
        view.clMode = clMode
        controller.view = view
        controller.page = page
        controller.attach(document)
        return view
    }

    static func dismantleNSView(_ view: CircuitCanvasNSView, coordinator: ()) {
        // A pane going away (a split closing) leaves its page's camera behind.
        if let document = view.document, view.pageKey != 0, view.bounds.width > 0 {
            document.cameras[view.pageKey] = (view.visibleCenter, view.unitsPerPoint)
        }
    }

    func updateNSView(_ view: CircuitCanvasNSView, context: Context) {
        if view.document !== document { view.document = document }
        if view.page != page || view.pageKey != document.pageID(page) {
            let switched = view.pageKey != document.pageID(page)
            view.show(page: page, key: document.pageID(page))
            controller.page = page
            if switched { DispatchQueue.main.async { controller.selectionChanged() } }
        }
        view.theme = theme
        view.controller = controller
        view.clMode = clMode
        if clMode { view.needsDisplay = true }   // an Appearance setting may have changed
        if controller.view !== view { controller.view = view }
        controller.attach(document)
    }
}
