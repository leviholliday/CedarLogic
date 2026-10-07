// Presenter mode (Mac only): View ▸ Present On ▸ <display> opens a clean
// full-screen picture of the circuit on that display -- Simulation View's
// projector styling, no toolbars or panels -- while the window on the
// laptop stays the editor. Both show the same circuit live: the simulation,
// switches flipped in the editor, and strokes as they're drawn. The
// picture follows the editor's page, middle and zoom (fitted to the
// display), so the teacher drives it from the laptop. Escape (with the
// presentation clicked) or View ▸ Stop Presenting ends it; the display is
// remembered by name; unplugging it ends the presentation.
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
        w.backgroundColor = .black
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

/// The presentation: the editor's page, middle and zoom, in projector styling.
final class PresenterView: NSView {
    weak var source: CanvasController?
    override var isFlipped: Bool { true }
    override var isOpaque: Bool { true }

    override init(frame: NSRect) {
        super.init(frame: frame)
        wantsLayer = true
        layerContentsRedrawPolicy = .onSetNeedsDisplay
    }
    required init?(coder: NSCoder) { fatalError("not used") }

    /// The camera: the world the editor shows, fitted to this view.
    func camera() -> (origin: CGPoint, upp: CGFloat)? {
        guard let src = source, let ed = src.view, let doc = src.document, bounds.width > 0, bounds.height > 0 else { return nil }
        var world = ed.bounds.width > 0 ? ed.visibleWorldRect : (doc.fitBounds(ofPage: src.page) ?? CGRect(x: -20, y: -15, width: 40, height: 30))
        if world.width <= 0 || world.height <= 0 { world = CGRect(x: -20, y: -15, width: 40, height: 30) }
        let upp = max(world.width / bounds.width, world.height / bounds.height)
        return (CGPoint(x: world.midX - bounds.width * upp / 2, y: world.midY + bounds.height * upp / 2), upp)
    }

    override func draw(_ dirtyRect: NSRect) {
        guard let ctx = NSGraphicsContext.current?.cgContext else { return }
        let pal = CLPalette(dark: true, simView: true)
        ctx.setFillColor(pal.canvasCG)
        ctx.fill(bounds)
        guard let src = source, let doc = src.document, let cam = camera() else { return }
        Self.drawPage(ctx, document: doc, controller: src, page: src.page, origin: cam.origin, upp: cam.upp,
                      size: bounds.size, scale: window?.backingScaleFactor ?? 2)
    }

    /// The page as the projector shows it (also --render-ui's picture).
    static func drawPage(_ ctx: CGContext, document doc: CoreDocument, controller src: CanvasController?, page: Int,
                         origin: CGPoint, upp: CGFloat, size: CGSize, scale: CGFloat) {
        let prefs = Prefs.shared
        let pal = CLPalette(dark: true, simView: true)
        if prefs.showGrid {
            CLGrid.draw(ctx, pal: pal, scale: scale, fade: 1, origin: origin, unitsPerPoint: upp, size: size)
        }
        // Predict's covers stay on: the projector must not give the answer away.
        let covered = src?.predictCovers ?? false
        var st = CLSimViewStyle(accent: Int32(prefs.accent), wireScale: prefs.wireScale, projector: true, predict: covered,
                                ink: Int32(CL_INK_FOLLOW))
        cl_simview_draw_page(doc.handle, Int32(page), ctx, scale, origin.x, origin.y, upp, &st)
        if !covered {
            cl_simview_draw_flow(doc.handle, Int32(page), ctx, scale, origin.x, origin.y, upp, src?.flowPhase ?? 0,
                                 prefs.wireScale * CL_PROJECTOR_WIRE_SCALE)
        }
        if let src, src.simView, src.predict.on {
            let marks = src.predictMarks
            cl_simview_draw_predict(doc.handle, Int32(page), ctx, scale, origin.x, origin.y, upp, marks, Int32(marks.count), true)
        }
        // A stroke as it's drawn in the editor.
        if cl_ink_live_page(doc.handle) == Int32(page) {
            cl_ink_draw_live(doc.handle, ctx, scale, origin.x, origin.y, upp, true, true)
        }
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
