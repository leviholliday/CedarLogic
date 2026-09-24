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
import SwiftUI

final class CircuitCanvasNSView: NSView {
    var document: CoreDocument? { didSet { needsFit = true; needsDisplay = true } }
    var page = 0 { didSet { if page != oldValue { needsFit = true; needsDisplay = true } } }
    var theme = LookStore.shared.settings.theme { didSet { if theme != oldValue { needsDisplay = true } } }
    weak var controller: CanvasController?

    private(set) var origin = CGPoint(x: -20, y: 20)
    private(set) var unitsPerPoint: CGFloat = 0.05
    private var needsFit = true

    /// What the current drag does.
    private enum Drag { case none, edit, pan(last: CGPoint) }
    private var drag = Drag.none
    private var spaceDown = false
    private var pannedWhileSpaceDown = false

    private let minUnitsPerPoint: CGFloat = 0.004   // very close
    private let maxUnitsPerPoint: CGFloat = 1.0     // very far

    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }
    override var isOpaque: Bool { true }

    override init(frame: NSRect) {
        super.init(frame: frame)
        registerForDraggedTypes([.string])
    }
    required init?(coder: NSCoder) { fatalError("not used") }

    // MARK: Drawing

    override func draw(_ dirtyRect: NSRect) {
        guard let ctx = NSGraphicsContext.current?.cgContext else { return }
        if needsFit, bounds.width > 0 { zoomToFit() }
        ctx.setFillColor(theme.canvas.cgColor)
        ctx.fill(bounds)
        drawGrid(ctx)
        guard let document else { return }
        let scale = window?.backingScaleFactor ?? 2
        cl_document_draw(document.handle, Int32(page), ctx, scale, origin.x, origin.y,
                         unitsPerPoint, theme.darkCircuit)
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

    private func pan(byPoints dx: CGFloat, _ dy: CGFloat) {
        origin.x -= dx * unitsPerPoint
        origin.y += dy * unitsPerPoint
        needsDisplay = true
    }

    // MARK: Pointer

    override func scrollWheel(with event: NSEvent) {
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
        window?.makeFirstResponder(self)
        let p = convert(event.locationInWindow, from: nil)
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
        if case .edit = drag {
            document?.release(at: worldPoint(p))
            controller?.selectionChanged()
            controller?.editsChanged()
        }
        drag = .none
        NSCursor.arrow.set()
        needsDisplay = true
    }

    // Right or middle drag moves around.
    override func otherMouseDown(with event: NSEvent) { drag = .pan(last: convert(event.locationInWindow, from: nil)) }
    override func otherMouseDragged(with event: NSEvent) { mouseDragged(with: event) }
    override func otherMouseUp(with event: NSEvent) { drag = .none }
    override func rightMouseDown(with event: NSEvent) { drag = .pan(last: convert(event.locationInWindow, from: nil)) }
    override func rightMouseDragged(with event: NSEvent) { mouseDragged(with: event) }
    override func rightMouseUp(with event: NSEvent) { drag = .none }

    // MARK: Keys

    override func keyDown(with event: NSEvent) {
        let plain = event.modifierFlags.intersection([.command, .option, .control]).isEmpty
        let shift = event.modifierFlags.contains(.shift)
        guard plain, let controller else { return super.keyDown(with: event) }
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
            default: super.keyDown(with: event)
            }
        }
    }

    override func keyUp(with event: NSEvent) {
        if event.keyCode == 49 {
            spaceDown = false
            NSCursor.arrow.set()
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

/// What a palette tile puts on the drag pasteboard.
enum GatePayload {
    static let prefix = "cedarlogic-gate:"
}

/// One window's canvas, edits and simulation: the toolbar, menus, palette and
/// inspector all go through here to reach the canvas in front, and it runs the
/// simulation clock while the window is open.
@MainActor
final class CanvasController: ObservableObject {
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

    private var timer: Timer?
    private var lastTick = CACurrentMediaTime()

    func attach(_ document: CoreDocument) {
        guard self.document !== document else { return }
        self.document = document
        isRunning = document.isRunning
        stepMs = document.stepMs
        startClock()
    }

    func redraw() { view?.needsDisplay = true }
    func selectionChanged() { selectionVersion += 1 }
    func editsChanged() { editVersion += 1 }
    private func edited() { redraw(); selectionChanged(); editsChanged() }

    // Camera
    func zoomIn() { view?.zoomAtCenter(by: 1.25) }
    func zoomOut() { view?.zoomAtCenter(by: 0.8) }
    func zoomToFit() { view?.zoomToFit() }

    // Editing
    var hasSelection: Bool { document?.hasSelection(page: page) ?? false }
    var hasGateSelection: Bool { document?.hasGateSelection(page: page) ?? false }
    var canUndo: Bool { document?.canUndo ?? false }
    var canRedo: Bool { document?.canRedo ?? false }
    var undoTitle: String { let n = document?.undoName ?? ""; return n.isEmpty ? "Undo" : "Undo \(n)" }
    var redoTitle: String { let n = document?.redoName ?? ""; return n.isEmpty ? "Redo" : "Redo \(n)" }

    func undo() { if document?.undo() == true { edited() } }
    func redo() { if document?.redo() == true { edited() } }
    func selectAll() { document?.selectAll(page: page); redraw(); selectionChanged() }
    func selectNone() { document?.selectNone(page: page); redraw(); selectionChanged() }
    func deleteSelection() { document?.deleteSelection(page: page); edited() }
    func rotate() { document?.rotateSelection(page: page); edited() }
    func nudge(dx: CGFloat, dy: CGFloat) { document?.nudge(page: page, dx: dx, dy: dy); edited() }
    func showSettings() { if document?.singleSelectedGate(page: page) != nil { settingsRequested = true } }

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
        guard let document, isRunning else { return }
        let result = document.tick(elapsedMs: elapsed)
        if result.changed { redraw() }
        if result.paused { isRunning = false }
    }

    deinit { timer?.invalidate() }
}

struct CanvasView: NSViewRepresentable {
    let document: CoreDocument
    let page: Int
    let theme: Theme
    let controller: CanvasController

    func makeNSView(context: Context) -> CircuitCanvasNSView {
        let view = CircuitCanvasNSView(frame: .zero)
        view.document = document
        view.page = page
        view.theme = theme
        view.controller = controller
        controller.view = view
        controller.page = page
        controller.attach(document)
        return view
    }

    func updateNSView(_ view: CircuitCanvasNSView, context: Context) {
        if view.document !== document { view.document = document }
        if view.page != page {
            document.selectNone(page: view.page)
            view.page = page
            controller.page = page
            DispatchQueue.main.async { controller.selectionChanged() }
        }
        view.theme = theme
        view.controller = controller
        if controller.view !== view { controller.view = view }
        controller.attach(document)
    }
}
