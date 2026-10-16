// Presenter mode (Mac only): View ▸ Present On ▸ <display> opens a clean
// full-screen picture of the circuit on that display -- no toolbars, palette,
// notes or other panels -- while the window on the laptop stays the editor.
// It mirrors the editor, so the room sees what the teacher points at without
// them turning round: the page, middle and zoom (fitted to the display), the
// look (the editing view, light or dark as the editor has it; Simulation
// View, with its projector mode and Predict covers, only while the editor is
// in it), the selection, the drag box, the part, pin or wire under the
// pointer, a part being moved or placed, a wire being connected, Tidy's
// ghost, strokes as they're drawn, the simulation, and the pointer itself
// (drawn big, with a ring at each click). Escape (with the presentation
// clicked) or View ▸ Stop Presenting ends it; the display is remembered by
// name; unplugging it ends the presentation.
//
// With one display, Present in This Window makes the circuit window itself
// the presentation: full screen, Simulation View in projector mode, no side
// panel or toolbar; leaving Simulation View (Escape) or Stop Presenting
// puts everything back.

import AppKit
import Combine
import SwiftUI

@MainActor
final class Presenter: ObservableObject {
    static let shared = Presenter()

    /// The displays macOS knows now (refreshed as they come and go).
    @Published private(set) var screens: [NSScreen] = NSScreen.screens
    /// Presenting on another display.
    @Published private(set) var external = false
    /// Presenting in the circuit window itself.
    @Published private(set) var inWindow = false
    var active: Bool { external || inWindow }
    /// The display last chosen, by name (its id can change when replugged).
    var remembered: String? {
        get { UserDefaults.standard.string(forKey: "cl.present.display") }
        set { UserDefaults.standard.set(newValue, forKey: "cl.present.display") }
    }

    private var window: NSWindow?
    private var view: PresenterView?
    private weak var source: CanvasController?
    private var timer: Timer?
    private var watchers: [NSObjectProtocol] = []
    /// What Present in This Window changed, to put back.
    private var restore: (simView: Bool, projector: Bool, fullScreen: Bool)?
    private var simWatch: AnyCancellable?

    private init() {
        watchers.append(NotificationCenter.default.addObserver(forName: NSApplication.didChangeScreenParametersNotification,
                                                               object: nil, queue: .main) { [weak self] _ in
            MainActor.assumeIsolated { self?.screensChanged() }
        })
    }

    static func name(_ s: NSScreen) -> String { s.localizedName }

    /// The displays other than the one a canvas's window is on.
    func otherScreens(than canvas: CanvasController?) -> [NSScreen] {
        let mine = canvas?.view?.window?.screen
        return screens.filter { $0 != mine }
    }

    /// The display ⇧⌘P presents on: the remembered one if it's there, else
    /// the first other one.
    func preferredScreen(for canvas: CanvasController?) -> NSScreen? {
        let others = otherScreens(than: canvas)
        return others.first { Self.name($0) == remembered } ?? others.first
    }

    func toggle(_ canvas: CanvasController?) {
        if active { stop(); return }
        guard let canvas else { return }
        if let s = preferredScreen(for: canvas) { present(canvas, on: s) } else { presentInWindow(canvas) }
    }

    // MARK: On another display

    func present(_ canvas: CanvasController, on screen: NSScreen) {
        stop()
        let src = canvas.sheetHost
        source = src
        remembered = Self.name(screen)
        let w = PresenterWindow(contentRect: screen.frame, styleMask: [.borderless], backing: .buffered, defer: false)
        w.setFrame(screen.frame, display: false)
        // Over that display's menu bar and Dock, on every Space; the editor
        // keeps the keyboard.
        w.level = .statusBar
        w.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .stationary]
        w.isReleasedWhenClosed = false
        w.backgroundColor = Prefs.shared.dark ? .black : .white
        w.title = "CedarLogic Presentation"
        w.onEscape = { [weak self] in self?.stop() }
        let v = PresenterView(frame: NSRect(origin: .zero, size: screen.frame.size))
        v.source = src
        w.contentView = v
        w.orderFrontRegardless()
        window = w
        view = v
        external = true
        // Redrawn as the editor's circuit runs and is drawn on.
        timer = Timer.scheduledTimer(withTimeInterval: 1.0 / 30, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.tick() }
        }
        if let ew = src.view?.window {
            watchers.append(NotificationCenter.default.addObserver(forName: NSWindow.willCloseNotification, object: ew, queue: .main) { [weak self] _ in
                MainActor.assumeIsolated { self?.stop() }
            })
        }
        src.note("Presenting on \(Self.name(screen)). It follows this window; View \u{25B8} Stop Presenting ends it.")
        announce("Presenting on \(Self.name(screen))")
    }

    private func tick() {
        guard let view, let src = source, src.document != nil else { stop(); return }
        view.needsDisplay = true
    }

    // MARK: In this window

    func presentInWindow(_ canvas: CanvasController) {
        stop()
        let src = canvas.sheetHost
        source = src
        let w = src.view?.window
        let wasFull = w?.styleMask.contains(.fullScreen) ?? false
        restore = (src.simView, Prefs.shared.projector, wasFull)
        Prefs.shared.projector = true
        src.simView = true
        src.presentingHere = true
        if let w, !wasFull { w.toggleFullScreen(nil) }
        inWindow = true
        // Leaving Simulation View (Escape) ends it.
        simWatch = src.$simView.dropFirst().sink { [weak self] on in
            if !on { DispatchQueue.main.async { MainActor.assumeIsolated { self?.stop() } } }
        }
        src.note("Presenting in this window. Escape ends it.")
        announce("Presenting in this window")
    }

    // MARK: Ending

    func stop() {
        timer?.invalidate()
        timer = nil
        if let window {
            window.orderOut(nil)
            window.close()
        }
        window = nil
        view = nil
        let src = source
        if inWindow, let src, let r = restore {
            simWatch = nil
            src.presentingHere = false
            Prefs.shared.projector = r.projector
            if src.simView != r.simView { src.simView = r.simView }
            if let w = src.view?.window, !r.fullScreen, w.styleMask.contains(.fullScreen) { w.toggleFullScreen(nil) }
        }
        simWatch = nil
        restore = nil
        if external || inWindow { src?.note("Stopped presenting.") }
        external = false
        inWindow = false
        source = nil
        // Keep only the screen watcher.
        while watchers.count > 1 { NotificationCenter.default.removeObserver(watchers.removeLast()) }
    }

    private func screensChanged() {
        screens = NSScreen.screens
        guard external, let window else { return }
        // Its display went: end it, rather than land on the laptop's screen.
        if let s = window.screen, screens.contains(s), s.frame.size == window.frame.size { return }
        if let s = screens.first(where: { Self.name($0) == remembered }), s != source?.view?.window?.screen {
            window.setFrame(s.frame, display: true)
            view?.frame = NSRect(origin: .zero, size: s.frame.size)
            return
        }
        let src = source
        stop()
        src?.note("The display was disconnected, so the presentation ended.")
    }
}

/// A borderless window that can take the keyboard when clicked (Escape ends it).
final class PresenterWindow: NSWindow {
    var onEscape: (() -> Void)?
    override var canBecomeKey: Bool { true }
    override func keyDown(with event: NSEvent) {
        if event.keyCode == 53 { onEscape?() } else { super.keyDown(with: event) }
    }
    override func cancelOperation(_ sender: Any?) { onEscape?() }
}

/// The pointer as the presentation shows it (world, y up).
struct MirrorPointer {
    var world: CGPoint
    var pressed = false
    /// Draw mode: a ring round the pen's point instead of an arrow.
    var pen = false
}

/// The presentation: what the editor's canvas shows, with its own camera.
final class PresenterView: NSView {
    weak var source: CanvasController?
    /// For pictures (--render-presenter): the pointer to show instead of the
    /// real one (.some(nil) shows none), and a click ring's age in seconds.
    var pointerOverride: MirrorPointer??
    var pressOverride: (at: CGPoint, age: Double)?
    override var isFlipped: Bool { true }
    override var isOpaque: Bool { true }

    override init(frame: NSRect) {
        super.init(frame: frame)
        wantsLayer = true
        layerContentsRedrawPolicy = .onSetNeedsDisplay
    }
    required init?(coder: NSCoder) { fatalError("not used") }

    /// The side being worked in: in a split, the one clicked last.
    var side: CanvasController? { source?.routed }

    /// The camera: the world the editor shows, fitted to this view.
    func camera() -> (origin: CGPoint, upp: CGFloat)? {
        guard let src = side, let ed = src.view, let doc = src.document, bounds.width > 0, bounds.height > 0 else { return nil }
        var world = ed.bounds.width > 0 ? ed.visibleWorldRect : (doc.fitBounds(ofPage: src.page) ?? CGRect(x: -20, y: -15, width: 40, height: 30))
        if world.width <= 0 || world.height <= 0 { world = CGRect(x: -20, y: -15, width: 40, height: 30) }
        let upp = max(world.width / bounds.width, world.height / bounds.height)
        return (CGPoint(x: world.midX - bounds.width * upp / 2, y: world.midY + bounds.height * upp / 2), upp)
    }

    override func draw(_ dirtyRect: NSRect) {
        guard let ctx = NSGraphicsContext.current?.cgContext else { return }
        guard let src = side, let doc = src.document, let cam = camera() else {
            ctx.setFillColor(CGColor(gray: 0, alpha: 1))
            ctx.fill(bounds)
            return
        }
        Self.drawPage(ctx, document: doc, controller: src, page: src.page, origin: cam.origin, upp: cam.upp,
                      size: bounds.size, scale: window?.backingScaleFactor ?? 2)
        drawPointer(ctx, origin: cam.origin, upp: cam.upp)
    }

    /// The page as the editor shows it, for this camera (also the pictures'
    /// of --render-ui and --render-presenter). No controller: the editing
    /// view of the CedarLogic interface.
    static func drawPage(_ ctx: CGContext, document doc: CoreDocument, controller src: CanvasController?, page: Int,
                         origin: CGPoint, upp: CGFloat, size: CGSize, scale: CGFloat) {
        if let ed = src?.view, !ed.clMode {
            CircuitCanvasNSView.paintSimple(ctx, document: doc, theme: ed.theme, page: page, origin: origin, upp: upp,
                                            size: size, scale: scale)
        } else {
            CircuitCanvasNSView.paintCL(ctx, document: doc, controller: src, page: page, origin: origin, upp: upp,
                                        size: size, scale: scale)
        }
    }

    // MARK: The pointer

    /// Where the teacher's pointer is: over the editor's canvas (not over
    /// a menu, another window or the panels), in its world.
    func livePointer() -> MirrorPointer? {
        if let o = pointerOverride { return o }
        guard let src = side, let ed = src.view, let w = ed.window, w.isVisible else { return nil }
        let at = NSEvent.mouseLocation
        guard NSWindow.windowNumber(at: at, belowWindowWithWindowNumber: 0) == w.windowNumber else { return nil }
        let p = ed.convert(w.convertPoint(fromScreen: at), from: nil)
        guard ed.visibleRect.contains(p) else { return nil }
        return MirrorPointer(world: ed.worldPoint(p), pressed: NSEvent.pressedMouseButtons & 1 != 0, pen: src.drawing)
    }

    /// How long a click's ring lasts.
    static let pressRingTime = 0.5

    /// The pointer, sized for the back of the room whatever the zoom: an
    /// arrow (or, drawing, a ring round the pen's point) on a soft halo in
    /// the accent colour, the halo stronger while the button is down, and a
    /// ring spreading from each click.
    private func drawPointer(_ ctx: CGContext, origin: CGPoint, upp: CGFloat) {
        func view(_ w: CGPoint) -> CGPoint { CGPoint(x: (w.x - origin.x) / upp, y: (origin.y - w.y) / upp) }
        let unit = max(1, min(bounds.height / 720, 2.5))   // 1 on a 720-point display
        let prefs = Prefs.shared
        let dark = prefs.dark || (side?.simView ?? false)
        let a = prefs.accentRGB(dark: dark)
        let accent = CGColor(srgbRed: a.0, green: a.1, blue: a.2, alpha: 1)
        // A click's ring.
        let press: (at: CGPoint, age: Double)? = pressOverride ?? side?.view?.lastPress.map { ($0.at, CACurrentMediaTime() - $0.time) }
        if let press, press.age >= 0, press.age < Self.pressRingTime {
            let t = press.age / Self.pressRingTime
            let c = view(press.at)
            let r = (12 + 34 * (1 - pow(1 - t, 2))) * unit
            ctx.setStrokeColor(accent.copy(alpha: 0.9 * (1 - t))!)
            ctx.setLineWidth(4 * unit)
            ctx.strokeEllipse(in: CGRect(x: c.x - r, y: c.y - r, width: 2 * r, height: 2 * r))
        }
        guard let ptr = livePointer() else { return }
        let tip = view(ptr.world)
        let halo = (ptr.pressed ? 20 : 24) * unit
        ctx.setFillColor(accent.copy(alpha: ptr.pressed ? 0.45 : 0.25)!)
        ctx.fillEllipse(in: CGRect(x: tip.x - halo, y: tip.y - halo, width: 2 * halo, height: 2 * halo))
        if ptr.pen {
            let r = 9 * unit
            let ring = CGRect(x: tip.x - r, y: tip.y - r, width: 2 * r, height: 2 * r)
            ctx.setStrokeColor(CGColor(gray: dark ? 0 : 1, alpha: 0.9))
            ctx.setLineWidth(5 * unit)
            ctx.strokeEllipse(in: ring)
            ctx.setStrokeColor(accent)
            ctx.setLineWidth(2.5 * unit)
            ctx.strokeEllipse(in: ring)
            return
        }
        // The arrow: the system's shape, white with a dark edge, tip at the point.
        let s = 1.7 * unit
        let shape: [CGFloat] = [0, 0, 0, 17, 4.2, 13, 7, 19.5, 9.6, 18.4, 6.9, 12.2, 12.3, 12.2]
        var pts: [CGPoint] = []
        for k in stride(from: 0, to: shape.count, by: 2) { pts.append(CGPoint(x: tip.x + shape[k] * s, y: tip.y + shape[k + 1] * s)) }
        ctx.saveGState()
        ctx.setShadow(offset: CGSize(width: 0, height: 1.5 * unit), blur: 4 * unit, color: CGColor(gray: 0, alpha: 0.45))
        ctx.addLines(between: pts)
        ctx.closePath()
        ctx.setFillColor(CGColor(gray: 1, alpha: 1))
        ctx.fillPath()
        ctx.restoreGState()
        ctx.addLines(between: pts)
        ctx.closePath()
        ctx.setStrokeColor(CGColor(gray: 0, alpha: 1))
        ctx.setLineWidth(1.4 * unit)
        ctx.setLineJoin(.round)
        ctx.strokePath()
    }
}

/// View ▸ Present On, Present in This Window, Stop Presenting.
struct PresentMenu: View {
    let canvas: CanvasController?
    @ObservedObject private var presenter = Presenter.shared
    @ObservedObject private var keys = ShortcutStore.shared

    var body: some View {
        let others = presenter.otherScreens(than: canvas)
        if presenter.active {
            Button("Stop Presenting") { presenter.stop() }
                .keyboardShortcut(keys.menu(.present))
        } else {
            if others.isEmpty {
                // Greyed, saying why.
                Button("Present on Another Display (Connect One First)") {}
                    .disabled(true)
            } else if others.count == 1, let s = others.first {
                Button("Present on \u{201C}\(Presenter.name(s))\u{201D}") { if let canvas { presenter.present(canvas, on: s) } }
                    .keyboardShortcut(keys.menu(.present))
                    .disabled(canvas == nil)
            } else {
                Menu("Present On") {
                    ForEach(others, id: \.self) { s in
                        Button(Presenter.name(s) + (Presenter.name(s) == presenter.remembered ? " (Last Used)" : "")) {
                            if let canvas { presenter.present(canvas, on: s) }
                        }
                    }
                }
                .disabled(canvas == nil)
            }
            Button("Present in This Window") { if let canvas { presenter.presentInWindow(canvas) } }
                .keyboardShortcut(others.isEmpty ? keys.menu(.present) : nil)
                .disabled(canvas == nil)
        }
    }
}
