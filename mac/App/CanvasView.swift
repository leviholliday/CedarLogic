// The circuit canvas: an AppKit view, so macOS itself tracks the mouse and the
// trackpad (no hand-rolled mouse capture to get stuck). It draws the look's
// background and grid, then asks the engine to draw the page on top.
//
// Camera: `origin` is the world point at the view's top-left corner and
// `unitsPerPoint` how many world units one point covers (smaller = closer).
// World y points up; the view is flipped, so screen y points down.

import AppKit
import SwiftUI

final class CircuitCanvasNSView: NSView {
    var document: CoreDocument? { didSet { needsFit = true; needsDisplay = true } }
    var page = 0 { didSet { if page != oldValue { needsFit = true; needsDisplay = true } } }
    var theme = LookStore.shared.settings.theme { didSet { if theme != oldValue { needsDisplay = true } } }

    private var origin = CGPoint(x: -20, y: 20)
    private var unitsPerPoint: CGFloat = 0.05
    private var needsFit = true
    private var dragStart: NSPoint?

    private let minUnitsPerPoint: CGFloat = 0.004   // very close
    private let maxUnitsPerPoint: CGFloat = 1.0     // very far

    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }
    override var isOpaque: Bool { true }

    // MARK: Drawing

    override func draw(_ dirtyRect: NSRect) {
        guard let ctx = NSGraphicsContext.current?.cgContext else { return }
        if needsFit, bounds.width > 0 { zoomToFit(animated: false) }
        ctx.setFillColor(theme.canvas.cgColor)
        ctx.fill(bounds)
        drawGrid(ctx)
        if let document {
            let scale = window?.backingScaleFactor ?? 2
            cl_document_draw(document.handle, Int32(page), ctx, scale, origin.x, origin.y,
                             unitsPerPoint, theme.darkCircuit)
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

    func zoomToFit(animated: Bool = true) {
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
        let world = CGPoint(x: origin.x + p.x * unitsPerPoint, y: origin.y - p.y * unitsPerPoint)
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

    // MARK: Input

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

    // Nothing to edit yet, so a drag on the canvas moves around.
    override func mouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
        dragStart = convert(event.locationInWindow, from: nil)
        if event.clickCount == 2 { zoomToFit() }
    }

    override func mouseDragged(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        if let start = dragStart {
            pan(byPoints: p.x - start.x, p.y - start.y)
            dragStart = p
            NSCursor.closedHand.set()
        }
    }

    override func mouseUp(with event: NSEvent) {
        dragStart = nil
        NSCursor.arrow.set()
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

/// Lets menu commands reach the canvas of the window that's in front.
@MainActor
final class CanvasController: ObservableObject {
    weak var view: CircuitCanvasNSView?
    func zoomIn() { view?.zoomAtCenter(by: 1.25) }
    func zoomOut() { view?.zoomAtCenter(by: 0.8) }
    func zoomToFit() { view?.zoomToFit() }
}

struct CanvasView: NSViewRepresentable {
    let document: CoreDocument
    let page: Int
    let theme: Theme
    let controller: CanvasController

    func makeNSView(context: Context) -> CircuitCanvasNSView {
        let view = CircuitCanvasNSView()
        view.document = document
        view.page = page
        view.theme = theme
        controller.view = view
        return view
    }

    func updateNSView(_ view: CircuitCanvasNSView, context: Context) {
        if view.document !== document { view.document = document }
        view.page = page
        view.theme = theme
        controller.view = view
    }
}
