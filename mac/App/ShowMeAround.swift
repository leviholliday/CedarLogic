// Help > Show Me Around: a short walk round the real window, as other apps
// do on their first run. Everything is dimmed but one place at a time (the
// palette, the canvas, the toolbar, the Actions panel, the run controls, the
// tabs, then up to the menu bar for Help and Settings), with a bubble
// beside it saying what it's for in a sentence. Next, Back and Skip in the
// bubble; → (or Return, or Space) and ← step, Escape stops. The spotlight
// slides from place to place (a cut and a quick fade with Reduce Motion).
// A place that isn't in the window just now is passed over: the Actions
// panel turned off, a toolbar too narrow to show its tools.
//
// It starts by itself until it has been seen once (finished or skipped:
// `cl.showMeAroundSeen`): after the first-run welcome closes, whichever way,
// and for an update's first launch after What's New -- never over the
// launch screen, the intro or a greeting, only once a circuit window is up
// and has the keys. Skip and Escape are the way out. Help > Show Me Around
// plays it again any time. "Learn Logic: Guided Tour" (Welcome.swift) is
// the other kind, a lesson that builds a circuit with you: chosen from the
// welcome it starts after this, and this walk's last stop offers it.
//
// Kept portable on purpose (Windows and Linux follow this): a dim with one
// rounded hole, a ring round it, a card with a small pointer, all flat
// shapes Direct2D and cairo draw as easily.

import AppKit
import SwiftUI

/// The places the walk stops at. Views mark themselves with `.tourSpot(_:)`.
enum TourSpot: String, CaseIterable {
    case palette, canvas, toolbar, actions, run, tabs
    /// Not in the window: the menu bar above it (Help, and Settings in the app menu).
    case menuBar
    /// The overlay's own frame, to measure the others from.
    case root
}

extension View {
    /// Marks this view as one of Show Me Around's places. (An AppKit view
    /// behind it, so its frame can be read in window coordinates from
    /// anywhere, split views and all.)
    func tourSpot(_ spot: TourSpot) -> some View {
        background(TourSpotMarker(spot: spot).allowsHitTesting(false).accessibilityHidden(true))
    }
}

struct TourSpotMarker: NSViewRepresentable {
    let spot: TourSpot
    func makeNSView(context: Context) -> MarkerView {
        let v = MarkerView()
        v.spot = spot
        MarkerView.all.add(v)
        return v
    }
    func updateNSView(_ v: MarkerView, context: Context) { v.spot = spot }

    final class MarkerView: NSView {
        var spot: TourSpot = .root
        @MainActor static let all = NSHashTable<MarkerView>.weakObjects()
        override func hitTest(_ point: NSPoint) -> NSView? { nil }
        /// Its frame in its window's coordinates, if it's showing.
        var windowFrame: NSRect? {
            guard window != nil, !isHiddenOrHasHiddenAncestor, bounds.width > 0.5, bounds.height > 0.5 else { return nil }
            return convert(bounds, to: nil)
        }
    }
}

@MainActor
final class ShowMeAround: ObservableObject {
    static let shared = ShowMeAround()

    @Published private(set) var active = false
    /// Which of `steps` is showing.
    @Published private(set) var step = 0
    /// Each place's frame in the overlay's coordinates (top-left origin),
    /// for the target window; only the ones showing.
    @Published private(set) var frames: [TourSpot: CGRect] = [:]
    /// Reduce Motion: the bubble fades out and in around a cut.
    @Published private(set) var fade = 1.0
    /// The window it's walking round.
    private(set) weak var target: CanvasController?
    /// Set once it has been finished or skipped; until then it starts by itself.
    static let seenKey = "cl.showMeAroundSeen"
    static var seen: Bool {
        get { UserDefaults.standard.bool(forKey: seenKey) }
        set { UserDefaults.standard.set(newValue, forKey: seenKey) }
    }
    /// What comes after it (the welcome's Learn Logic choice: the Guided Tour).
    var then: (@MainActor () -> Void)?
    private var autoTimer: Timer?
    /// Asked to start by itself, and not started yet.
    private var wanted = false
    private var readySince: Date?

    private var timer: Timer?
    private var keyMonitor: Any?

    enum Side { case right, left, below, above, inside }

    struct Step {
        let spot: TourSpot
        let title: String
        let body: @MainActor (ShowMeAround) -> String
        /// Where the bubble would rather go, before trying the others.
        let sides: [Side]
    }

    private static func key(_ a: ShortcutAction) -> String { ShortcutStore.shared.combo(a)?.label ?? "" }
    private static func withKey(_ a: ShortcutAction) -> String { let k = key(a); return k.isEmpty ? "" : " (\(k))" }

    static let steps: [Step] = [
        Step(spot: .palette, title: "Parts",
             body: { _ in "Drag any part from here onto the canvas, or press \(key(.addGate).isEmpty ? "A" : key(.addGate)) and type its name." },
             sides: [.right]),
        Step(spot: .canvas, title: "Your circuit",
             body: { _ in "Wire two parts by dragging from one pin to another, and click a switch to flip it." },
             sides: [.inside]),
        Step(spot: .toolbar, title: "The toolbar",
             body: { _ in "New, undo, copy and paste, and zoom live up here; rest on a button for its name and shortcut." },
             sides: [.below]),
        Step(spot: .actions, title: "The Actions panel",
             body: { _ in
                 let p = Prefs.shared
                 return p.actionsCollapsed
                     ? "The buttons you use most, down the right edge. You keep it folded to a thin strip there: \(p.actionsHoverOpen ? "rest the pointer on it or click it" : "click it") to bring it out."
                     : "The buttons you use most, down the right edge. Click its left edge to fold it away, and right-click it to choose what\u{2019}s on it."
             },
             sides: [.left]),
        Step(spot: .run, title: "Run it",
             body: { _ in "Pause, step and set the speed here; the play button opens Simulation View\(withKey(.simView)) to watch signals move." },
             sides: [.below]),
        Step(spot: .tabs, title: "Tabs",
             body: { _ in "A circuit can have several pages: the + adds one, and a click on a tab goes to it." },
             sides: [.below]),
        Step(spot: .menuBar, title: "Help and Settings",
             body: { _ in "Help is in the menu bar at the top of the screen, and Settings is under CedarLogic (⌘,). Help \u{25B8} Show Me Around plays this again." },
             sides: [.below]),
    ]

    /// The panel is folded to its tab just now (by a click, a narrow window, focus mode).
    var panelFolded: Bool { (frames[.actions]?.width ?? ActionsRail.width) < ActionsRail.width / 2 }

    /// The steps that apply in the window now, in order.
    var shown: [Int] { Self.steps.indices.filter { available(Self.steps[$0]) } }

    private func available(_ s: Step) -> Bool {
        switch s.spot {
        case .menuBar: return true
        case .actions: return Prefs.shared.actionsOn && frames[.actions] != nil
        default: return frames[s.spot] != nil
        }
    }

    // MARK: Starting and stopping

    /// Help > Show Me Around, or its first time by itself: on this window, out of
    /// focus mode and Simulation View first (they hide the places it shows).
    /// `settle`: how long that takes before the first stop.
    /// `bringForward` false: the self-test's windows, off screen, stay out.
    func start(on c: CanvasController, settle: Double = 0.35, bringForward: Bool = true) {
        stop()
        stopWatching()
        if TourModel.shared.active { TourModel.shared.stop() }
        target = c
        if bringForward { c.view?.window?.makeKeyAndOrderFront(nil) }
        NotificationCenter.default.post(name: .clShowMeAroundWillStart, object: c)
        step = 0; fade = 1
        let begin = { [weak self] in
            guard let self, self.target === c else { return }
            self.measure()
            self.step = self.shown.first ?? 0
            withAnimation(.easeOut(duration: 0.2)) { self.active = true }
            self.remeasureSoon()
            self.watch()
        }
        if settle > 0 {
            DispatchQueue.main.asyncAfter(deadline: .now() + settle) { MainActor.assumeIsolated { begin() } }
        } else {
            begin()
        }
    }

    /// For pictures (RenderActions): on this window at this stop, nothing
    /// watched, no keys taken.
    func preview(on c: CanvasController, step n: Int) {
        target = c
        measure()
        step = n; fade = 1
        active = true
        remeasureSoon()
    }

    /// Once the overlay is up its own frame is the one to measure from
    /// (the window's content can be wider than the window, by the toolbar).
    func remeasureSoon() {
        for t in [0.05, 0.22, 0.4] {
            DispatchQueue.main.asyncAfter(deadline: .now() + t) {
                MainActor.assumeIsolated { if self.active { withAnimation(Self.move) { self.measure() } } }
            }
        }
    }

    /// Done, Skip or Escape: it has been seen (it won't start by itself
    /// again), and whatever was to come after it comes now.
    func end() {
        guard active else { return }
        Self.seen = true
        stop()
        let after = then
        then = nil
        if let after { DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) { MainActor.assumeIsolated { after() } } }
    }

    func stop() {
        timer?.invalidate(); timer = nil
        if let m = keyMonitor { NSEvent.removeMonitor(m); keyMonitor = nil }
        if active { withAnimation(.easeOut(duration: 0.2)) { active = false } }
        target = nil
    }

    /// Its window closed: so does the walk (not seen yet: it starts again next time).
    func windowClosed(_ c: CanvasController) {
        if target === c { stop() }
    }

    // MARK: Stepping

    /// "2 of 7", counting only the stops this window has.
    var position: (index: Int, count: Int) {
        let s = shown
        return ((s.firstIndex(of: step) ?? 0) + 1, max(s.count, 1))
    }
    var isLast: Bool { shown.last.map { step >= $0 } ?? true }
    var isFirst: Bool { shown.first.map { step <= $0 } ?? true }

    func next() {
        guard active else { return }
        if let n = shown.first(where: { $0 > step }) { go(n) } else { end() }
    }
    func back() {
        guard active, let n = shown.last(where: { $0 < step }) else { return }
        go(n)
    }

    /// The spotlight slides there; with Reduce Motion, a cut inside a quick fade.
    private func go(_ n: Int) {
        guard n != step else { return }
        if Splash.reduceMotion {
            withAnimation(.easeOut(duration: 0.1)) { fade = 0 }
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
                MainActor.assumeIsolated {
                    var t = Transaction()
                    t.disablesAnimations = true
                    withTransaction(t) { self.step = n }
                    withAnimation(.easeIn(duration: 0.14)) { self.fade = 1 }
                }
            }
        } else {
            withAnimation(Self.move) { step = n }
        }
        // The panel opens for its stop (and folds back after): where things are moves.
        remeasureSoon()
    }

    /// The spotlight's slide: quick, settling without a bounce.
    static let move = Animation.spring(response: 0.42, dampingFraction: 0.88)

    /// A key while it's on (in its window, no sheet over it): true when
    /// it's taken. Plain keys don't reach the canvas meanwhile.
    func key(_ code: UInt16, plain: Bool) -> Bool {
        guard active else { return false }
        switch code {
        case 53: end(); return true                           // Escape
        case 124, 36, 76, 49: next(); return true             // →, Return, Enter, Space
        case 123: back(); return true                         // ←
        default: return plain
        }
    }

    private func watch() {
        timer?.invalidate()
        // Frames move with the window (a resize, the panel folding): looked at
        // four times a second, published only when they change.
        let t = Timer(timeInterval: 0.25, repeats: true) { _ in
            MainActor.assumeIsolated {
                let s = ShowMeAround.shared
                guard s.active else { return }
                if s.target?.view?.window == nil { s.stop(); return }
                withAnimation(Self.move) { s.measure() }
                // The place it's on went (the panel turned off): on to the next.
                if !s.shown.contains(s.step) {
                    if let n = s.shown.first(where: { $0 > s.step }) ?? s.shown.last { s.step = n } else { s.stop() }
                }
            }
        }
        RunLoop.main.add(t, forMode: .common)
        timer = t
        if keyMonitor == nil {
            keyMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { e in
                let window = e.window.map(ObjectIdentifier.init)
                let plain = e.modifierFlags.intersection([.command, .control, .option]).isEmpty
                let code = e.keyCode
                let took = MainActor.assumeIsolated { () -> Bool in
                    let s = ShowMeAround.shared
                    guard s.active, plain, let w = s.target?.view?.window, window == ObjectIdentifier(w),
                          w.attachedSheet == nil else { return false }
                    return s.key(code, plain: plain)
                }
                return took ? nil : e
            }
        }
    }

    /// Reads where each place is now, in the target window.
    func measure() {
        guard let w = target?.view?.window else { if !frames.isEmpty { frames = [:] }; return }
        var rects: [TourSpot: NSRect] = [:]
        for m in TourSpotMarker.MarkerView.all.allObjects where m.window === w {
            guard let r = m.windowFrame else { continue }
            rects[m.spot] = rects[m.spot].map { $0.union(r) } ?? r
        }
        let root = rects[.root] ?? (w.contentView.map { $0.convert($0.bounds, to: nil) } ?? .zero)
        var out: [TourSpot: CGRect] = [:]
        for (spot, r) in rects where spot != .root {
            // Flipped, from the overlay's top left; clipped to it.
            let f = CGRect(x: r.minX - root.minX, y: root.maxY - r.maxY, width: r.width, height: r.height)
                .intersection(CGRect(origin: .zero, size: root.size))
            if !f.isNull && f.width > 0.5 && f.height > 0.5 { out[spot] = f }
        }
        if out != frames { frames = out }
    }

    // MARK: Starting by itself

    /// How the first-run welcome was closed.
    enum WelcomeExit: CaseIterable { case guidedTour, template, blank, shortcuts, skip }

    /// The first-run welcome closed: the walk comes first, whichever way;
    /// the Guided Tour, if that was the choice, after it. False: it's been
    /// seen, so the tour (if chosen) should start now.
    @discardableResult
    func welcomeClosed(_ exit: WelcomeExit) -> Bool {
        guard !Self.seen else { return false }
        if exit == .guidedTour { then = { TourModel.shared.start() } }
        startWhenReady()
        return true
    }

    /// Until it's been seen: watches for the moment a circuit window is up
    /// with the keys -- no launch screen, intro, welcome, What's New, sheet or
    /// lesson over it -- and starts there. (A launch, a window appearing, the
    /// welcome or What's New closing all call this; one watch at a time.)
    func startWhenReady() {
        guard !Self.seen, !active else { return }
        wanted = true
        guard !Self.quiet, autoTimer == nil else { return }
        readySince = nil
        let t = Timer(timeInterval: 0.3, repeats: true) { _ in
            MainActor.assumeIsolated { ShowMeAround.shared.autoTick() }
        }
        RunLoop.main.add(t, forMode: .common)
        autoTimer = t
    }

    /// Its watch for the moment to start is on.
    var watching: Bool { wanted }
    func stopWatching() { autoTimer?.invalidate(); autoTimer = nil; wanted = false }

    private func autoTick() {
        guard !Self.seen, !active else { stopWatching(); return }
        guard let c = Self.readyCanvas() else { readySince = nil; return }
        // Ready for a moment (the window settled, the circuit's card played).
        if let since = readySince, Date().timeIntervalSince(since) > 0.7 {
            stopWatching()
            start(on: c, settle: 0.2)
        } else if readySince == nil {
            readySince = Date()
        }
    }

    /// The circuit window it can start on now, if any.
    static func readyCanvas() -> CanvasController? {
        // The privacy question (Privacy.swift), if it's owed, comes first.
        guard NSApp.isActive, !Splash.shared.showing, !TourModel.shared.active, NSApp.modalWindow == nil,
              Greeting.due == nil, !PrivacyConsent.holdsShowMeAround else { return nil }
        let greeting = NSApp.windows.contains { o in
            o.isVisible && Greeting.ids.contains { o.identifier?.rawValue.contains($0) == true }
        }
        guard !greeting, let c = CanvasController.front, let w = c.view?.window,
              w.isVisible, w.isKeyWindow, w.attachedSheet == nil else { return nil }
        return c
    }

    /// The panel is held open for its stop (folded, it opens for it and folds back after).
    func holdsPanelOpen(_ c: CanvasController) -> Bool {
        active && target === c && Self.steps[min(step, Self.steps.count - 1)].spot == .actions
    }

    /// Test and picture runs never start it by themselves (the launch
    /// focus check does, as a real launch would).
    static var quiet: Bool {
        CommandLine.arguments.contains { $0.hasPrefix("--render") || $0.hasSuffix("-check") || $0.hasSuffix("-selftest") }
            || ProcessInfo.processInfo.environment["CL_SNAPSHOT"] != nil || BarTest.on
    }
}

extension Notification.Name {
    /// Show Me Around is starting on this window (its object): out of focus mode and Simulation View.
    static let clShowMeAroundWillStart = Notification.Name("clShowMeAroundWillStart")
}

// MARK: - The overlay

/// What a circuit window does for it: the overlay while it's on this
/// window, and out of focus mode and Simulation View as it starts.
struct ShowMeAroundWindowParts: ViewModifier {
    let canvas: CanvasController
    @Binding var focusMode: Bool
    @ObservedObject private var around = ShowMeAround.shared
    @ObservedObject private var prefs = Prefs.shared

    func body(content: Content) -> some View {
        content
            .overlay {
                if around.active && around.target === canvas {
                    ShowMeAroundOverlay().transition(.opacity)
                }
            }
            .onReceive(NotificationCenter.default.publisher(for: .clShowMeAroundWillStart)) { n in
                guard (n.object as? CanvasController) === canvas else { return }
                if focusMode { focusMode = false }
                if canvas.simView { canvas.simView = false }
            }
    }
}

/// The dim with its hole: the whole overlay, less a rounded rectangle.
struct SpotlightShape: Shape {
    var hole: CGRect
    var radius: CGFloat
    /// Just the hole's outline (the ring), not the dim.
    var ringOnly = false

    var animatableData: AnimatablePair<AnimatablePair<CGFloat, CGFloat>, AnimatablePair<CGFloat, CGFloat>> {
        get { .init(.init(hole.minX, hole.minY), .init(hole.width, hole.height)) }
        set { hole = CGRect(x: newValue.first.first, y: newValue.first.second, width: newValue.second.first, height: newValue.second.second) }
    }

    func path(in rect: CGRect) -> Path {
        var p = ringOnly ? Path() : Path(rect)
        p.addRoundedRect(in: hole, cornerSize: CGSize(width: radius, height: radius), style: .continuous)
        return p
    }
}

/// Over a circuit window while it's walking round it.
struct ShowMeAroundOverlay: View {
    @ObservedObject private var model = ShowMeAround.shared
    @ObservedObject private var prefs = Prefs.shared
    @State private var bubble = CGSize(width: 300, height: 150)

    static let pad: CGFloat = 6
    static let gap: CGFloat = 14
    static let margin: CGFloat = 12

    var body: some View {
        GeometryReader { g in
            let size = g.size
            let s = ShowMeAround.steps[min(model.step, ShowMeAround.steps.count - 1)]
            let hole = holeRect(s.spot, size: size)
            let bw = min(300, max(200, size.width - 2 * Self.margin))
            let placed = place(s, hole: hole, size: size, bubble: CGSize(width: bw, height: bubble.height))
            ZStack(alignment: .topLeading) {
                SpotlightShape(hole: hole, radius: 10)
                    .fill(Color.black.opacity(prefs.dark ? 0.6 : 0.48), style: FillStyle(eoFill: true))
                    // Clicks on the dim go nowhere; in the hole they reach what's lit.
                    .contentShape(SpotlightShape(hole: hole, radius: 10), eoFill: true)
                    .onTapGesture {}
                SpotlightShape(hole: hole, radius: 10, ringOnly: true)
                    .stroke(Brand.neon.opacity(s.spot == .menuBar ? 0 : 0.85), lineWidth: 2)
                    .shadow(color: Brand.neon.opacity(0.6), radius: 8)
                    .allowsHitTesting(false)
                ShowMeAroundBubble(model: model, step: s, caret: placed.caret)
                    .frame(width: bw)
                    .fixedSize(horizontal: false, vertical: true)
                    .onGeometryChange(for: CGSize.self) { $0.size } action: { bubble = $0 }
                    .offset(x: placed.origin.x, y: placed.origin.y)
                    .opacity(model.fade)
            }
            .frame(width: size.width, height: size.height, alignment: .topLeading)
        }
        .background(TourSpotMarker(spot: .root).allowsHitTesting(false))
        .ignoresSafeArea()
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Show Me Around")
    }

    /// The lit place, a little larger than it is, and at least a finger's
    /// width (the folded panel's tab is 6 points). The menu bar's is above
    /// the window, out of sight.
    private func holeRect(_ spot: TourSpot, size: CGSize) -> CGRect {
        guard spot != .menuBar, let f = model.frames[spot] else {
            return CGRect(x: size.width / 2 - 60, y: -40, width: 120, height: 20)
        }
        var r = f.insetBy(dx: -Self.pad, dy: -Self.pad)
        if r.width < 28 { r = r.insetBy(dx: -(28 - r.width) / 2, dy: 0) }
        let inside = r.intersection(CGRect(origin: .zero, size: size).insetBy(dx: 2, dy: 2))
        return inside.isNull ? r : inside
    }

    struct Placement { var origin: CGPoint; var caret: Caret? }
    /// The bubble's pointer: on which edge, and how far along it.
    struct Caret: Equatable { var edge: Edge; var at: CGFloat }

    /// Beside the place, on the side it prefers if it fits there, else any
    /// side that does, else inside a big place, else the window's foot.
    private func place(_ s: ShowMeAround.Step, hole h: CGRect, size: CGSize, bubble b: CGSize) -> Placement {
        let m = Self.margin, gap = Self.gap
        func clampX(_ x: CGFloat) -> CGFloat { min(max(x, m), max(m, size.width - b.width - m)) }
        func clampY(_ y: CGFloat) -> CGFloat { min(max(y, m), max(m, size.height - b.height - m)) }
        if s.spot == .menuBar {
            let o = CGPoint(x: clampX(size.width / 2 - b.width / 2), y: min(64, max(m, size.height - b.height - m)))
            return Placement(origin: o, caret: Caret(edge: .top, at: size.width / 2 - o.x))
        }
        for side in s.sides + [.below, .right, .left, .above, .inside] {
            switch side {
            case .right where size.width - h.maxX - gap - m >= b.width:
                let o = CGPoint(x: h.maxX + gap, y: clampY(h.midY - b.height / 2))
                return Placement(origin: o, caret: Caret(edge: .leading, at: h.midY - o.y))
            case .left where h.minX - gap - m >= b.width:
                let o = CGPoint(x: h.minX - gap - b.width, y: clampY(h.midY - b.height / 2))
                return Placement(origin: o, caret: Caret(edge: .trailing, at: h.midY - o.y))
            case .below where size.height - h.maxY - gap - m >= b.height:
                let o = CGPoint(x: clampX(h.midX - b.width / 2), y: h.maxY + gap)
                return Placement(origin: o, caret: Caret(edge: .top, at: h.midX - o.x))
            case .above where h.minY - gap - m >= b.height:
                let o = CGPoint(x: clampX(h.midX - b.width / 2), y: h.minY - gap - b.height)
                return Placement(origin: o, caret: Caret(edge: .bottom, at: h.midX - o.x))
            case .inside where h.width >= b.width + 2 * m && h.height >= b.height + 2 * m:
                return Placement(origin: CGPoint(x: clampX(h.midX - b.width / 2), y: clampY(h.midY - b.height / 2)), caret: nil)
            default: continue
            }
        }
        return Placement(origin: CGPoint(x: clampX(size.width / 2 - b.width / 2), y: clampY(size.height - b.height - m)), caret: nil)
    }
}

/// The bubble: where you are in the walk, what this place is for, and the buttons.
struct ShowMeAroundBubble: View {
    @ObservedObject var model: ShowMeAround
    let step: ShowMeAround.Step
    var caret: ShowMeAroundOverlay.Caret?
    @ObservedObject private var prefs = Prefs.shared

    var body: some View {
        let pos = model.position
        let last = model.isLast
        VStack(alignment: .leading, spacing: 9) {
            HStack(spacing: 7) {
                Image(systemName: "binoculars.fill").font(.system(size: 10, weight: .bold)).foregroundStyle(Brand.neon)
                Text("SHOW ME AROUND  \u{00B7}  \(pos.index) OF \(pos.count)")
                    .font(.system(size: 10, weight: .bold)).kerning(1.1).foregroundStyle(Brand.neon)
                    .lineLimit(1)
                Spacer(minLength: 4)
                if !last {
                    Button("Skip") { model.end() }.buttonStyle(.plain)
                        .font(.system(size: 11.5, weight: .medium)).foregroundStyle(BrandText.faint)
                        .help("Stop showing me around (Esc)")
                }
            }
            if pos.index == 1 {
                Text("A quick look around CedarLogic, \(pos.count) stops. Skip any time.")
                    .font(.system(size: 12, weight: .semibold)).foregroundStyle(BrandText.primary.opacity(0.85))
                    .fixedSize(horizontal: false, vertical: true)
            }
            Text(step.title).font(.system(size: 16, weight: .bold)).foregroundStyle(BrandText.primary)
            Text(step.body(model)).font(.system(size: 12.5)).foregroundStyle(BrandText.secondary)
                .fixedSize(horizontal: false, vertical: true)
            if step.spot == .menuBar { menuBarPicture.padding(.top, 2) }
            HStack(spacing: 8) {
                HStack(spacing: 4) {
                    ForEach(0..<pos.count, id: \.self) { i in
                        Capsule().fill(i == pos.index - 1 ? Brand.neon : Color.white.opacity(0.2))
                            .frame(width: i == pos.index - 1 ? 14 : 5, height: 5)
                    }
                }
                .accessibilityHidden(true)
                Spacer(minLength: 6)
                if !model.isFirst {
                    Button("Back") { model.back() }.buttonStyle(BrandButtonStyle(primary: false, width: 64))
                }
                Button(last ? "Done" : "Next") { model.next() }
                    .buttonStyle(BrandButtonStyle(width: 74))
            }
            .padding(.top, 2)
            if last {
                Button {
                    if model.then == nil { model.then = { TourModel.shared.start() } }
                    model.end()
                } label: {
                    HStack(spacing: 5) {
                        Image(systemName: "figure.walk").font(.system(size: 11))
                        Text("Next, learn logic: the Guided Tour builds a circuit with you").font(.system(size: 11.5, weight: .medium))
                    }
                    .foregroundStyle(Brand.neon.opacity(0.9))
                    .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
            }
        }
        .padding(16)
        .background(Brand.ink)
        .background(BrandBackground(bloom: .init(x: 0.1, y: 0), gridStep: 22))
        .clipShape(RoundedRectangle(cornerRadius: 14, style: .continuous))
        .overlay(RoundedRectangle(cornerRadius: 14, style: .continuous)
            .strokeBorder(LinearGradient(colors: [Color.white.opacity(0.22), Brand.neon.opacity(0.22), Color.white.opacity(0.05)],
                                         startPoint: .top, endPoint: .bottom), lineWidth: 1))
        .background(alignment: .topLeading) { caretView }
        .shadow(color: .black.opacity(0.4), radius: 16, y: 6)
        .environment(\.colorScheme, .dark)
        .accessibilityElement(children: .contain)
    }

    /// The menu bar, small: CedarLogic (where Settings is) and Help, lit.
    private var menuBarPicture: some View {
        HStack(spacing: 0) {
            Image(systemName: "apple.logo").font(.system(size: 10)).padding(.horizontal, 6)
            ForEach(["CedarLogic", "File", "Edit", "View", "Simulation", "Help"], id: \.self) { m in
                let lit = m == "CedarLogic" || m == "Help"
                Text(m).font(.system(size: 10.5, weight: lit ? .bold : .regular))
                    .foregroundStyle(lit ? Brand.ink : BrandText.secondary)
                    .lineLimit(1).fixedSize()
                    .padding(.horizontal, 4).frame(height: 18)
                    .background(RoundedRectangle(cornerRadius: 4).fill(lit ? Brand.neon : .clear))
            }
            Spacer(minLength: 0)
        }
        .foregroundStyle(BrandText.secondary)
        .padding(.vertical, 3)
        .background(RoundedRectangle(cornerRadius: 6).fill(Color.white.opacity(0.07)))
        .clipped()
        .accessibilityHidden(true)
    }

    /// A small triangle on the bubble's edge, pointing at the place.
    @ViewBuilder private var caretView: some View {
        if let c = caret {
            GeometryReader { g in
                let w: CGFloat = 16, d: CGFloat = 8
                let along = c.edge == .top || c.edge == .bottom ? g.size.width : g.size.height
                let at = min(max(c.at, 20), along - 20)
                Path { p in
                    switch c.edge {
                    case .top:
                        p.move(to: CGPoint(x: at - w / 2, y: 1)); p.addLine(to: CGPoint(x: at, y: -d)); p.addLine(to: CGPoint(x: at + w / 2, y: 1))
                    case .bottom:
                        let y = g.size.height - 1
                        p.move(to: CGPoint(x: at - w / 2, y: y)); p.addLine(to: CGPoint(x: at, y: y + d)); p.addLine(to: CGPoint(x: at + w / 2, y: y))
                    case .leading:
                        p.move(to: CGPoint(x: 1, y: at - w / 2)); p.addLine(to: CGPoint(x: -d, y: at)); p.addLine(to: CGPoint(x: 1, y: at + w / 2))
                    case .trailing:
                        let x = g.size.width - 1
                        p.move(to: CGPoint(x: x, y: at - w / 2)); p.addLine(to: CGPoint(x: x + d, y: at)); p.addLine(to: CGPoint(x: x, y: at + w / 2))
                    }
                    p.closeSubpath()
                }
                .fill(Brand.ink)
            }
        }
    }
}
