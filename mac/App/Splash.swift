// The launch screen: on a fresh launch, a glass panel in the middle of the
// screen in the icon's own colours. The icon rises and sharpens out of a
// blur, its circuit traces light up, "CedarLogic" writes itself in, a line
// says what's being done and a thin neon bar along the bottom fills as it's
// done: the gate library, the palette's pictures, the circuit being opened.
// Then it dissolves into the window. About two and a half seconds, and it
// can't be skipped.
//
// And the smaller card that opens a circuit: its name on glass, a quick
// sweep, and the circuit fades up (OpeningCard).

import AppKit
import SwiftUI

/// The icon's palette: near-black green, neon green, brushed silver.
enum Brand {
    static let neon = Color(.sRGB, red: 0.22, green: 1.0, blue: 0.42)
    static let neonDeep = Color(.sRGB, red: 0.05, green: 0.78, blue: 0.26)
    static let ink = Color(.sRGB, red: 0.035, green: 0.063, blue: 0.047)
    static let inkDeep = Color(.sRGB, red: 0.012, green: 0.024, blue: 0.018)
    static let silver = LinearGradient(colors: [Color(white: 0.97), Color(white: 0.74), Color(white: 0.9)],
                                       startPoint: .top, endPoint: .bottom)
    static let dim = Color(.sRGB, red: 0.62, green: 0.74, blue: 0.66)
    /// The icon tile's corner, as a share of its size.
    static let corner: CGFloat = 0.19

    static var icon: NSImage {
        if let url = Bundle.main.url(forResource: "LaunchIcon", withExtension: "png"), let img = NSImage(contentsOf: url) {
            return img
        }
        return NSApp.applicationIconImage
    }
}

// MARK: - Launch screen

/// CL_DEBUG_SPLASH=1: timings to stderr (what happens when, dropped frames).
enum SplashDebug {
    static let on = ProcessInfo.processInfo.environment["CL_DEBUG_SPLASH"] != nil
    static let t0 = CACurrentMediaTime()
    nonisolated(unsafe) static var last: CFTimeInterval = 0
    static func log(_ s: String) {
        guard on else { return }
        FileHandle.standardError.write(String(format: "[%6.3f] %@\n", CACurrentMediaTime() - t0, s).data(using: .utf8)!)
    }
    static func frame(_ t: Double) {
        guard on else { return }
        let now = CACurrentMediaTime()
        if last > 0 && now - last > 0.03 { log(String(format: "frame gap %.0f ms (t=%.2f)", (now - last) * 1000, t)) }
        last = now
    }
}

extension Notification.Name {
    /// The launch screen is going: the windows it held are coming in.
    static let clSplashDone = Notification.Name("clSplashDone")
}

@MainActor
final class Splash: ObservableObject {
    static let shared = Splash()

    /// What's being done, and how far along the real work is (0...1).
    @Published private(set) var status = "Starting up"
    @Published private(set) var target: Double = 0
    /// When the panel should start dissolving (moves later if work runs long).
    @Published private(set) var dissolveAt: Double = 2.25
    /// Up (from launch until it has dissolved): windows opening meanwhile
    /// wait, unseen.
    private(set) var active = false
    /// Its clock: set when it begins, once the heavy work of launching is
    /// done, so nothing can hold up the animation while it plays.
    @Published private(set) var start = Date.distantFuture
    @Published private(set) var began = false
    /// Development: `CL_SPLASH_FREEZE=<seconds>` holds the panel at that
    /// moment, to look at it.
    let freezeAt = ProcessInfo.processInfo.environment["CL_SPLASH_FREEZE"].flatMap(Double.init)
    private var panel: NSWindow?
    private var held: [NSWindow] = []
    private var watcher: Timer?
    private var circuitNamed: String?
    /// When the work was all done (the bar glides home from there).
    @Published private(set) var readyAt: Double?
    private var lastStepAt: Double = 0

    static let dissolveTime = 0.42
    static var reduceMotion: Bool { NSWorkspace.shared.accessibilityDisplayShouldReduceMotion }

    var elapsed: Double { Date().timeIntervalSince(start) }

    /// On a fresh launch (not a dev run): the panel is made, unseen, and any
    /// window that opens from now on is hidden before it's first drawn. The
    /// launch goes on (the last circuit opens, the palette's pictures are
    /// drawn) and then `begin` plays the panel with nothing else to do.
    func showIfLaunching() {
        let args = CommandLine.arguments
        let env = ProcessInfo.processInfo.environment
        guard !active, panel == nil, !args.contains("--render-ui"), env["CL_SNAPSHOT"] == nil,
              env["CL_NO_SPLASH"] == nil else { return }
        active = true
        // Timed to the frame, so no App Nap until it's done: an app launched
        // in the background (behind another app, in Stage Manager's strip)
        // would otherwise have its timers held back for seconds.
        activity = ProcessInfo.processInfo.beginActivity(options: [.userInitiated, .latencyCritical],
                                                         reason: "The launch screen")
        if Self.reduceMotion { dissolveAt = 1.1 }
        let size = NSSize(width: 560, height: 372)
        // Where you're looking: the screen with the pointer on it.
        let mouse = NSEvent.mouseLocation
        let screen = NSScreen.screens.first { NSMouseInRect(mouse, $0.frame, false) } ?? NSScreen.main ?? NSScreen.screens.first
        let vis = screen?.visibleFrame ?? NSRect(x: 0, y: 0, width: 1440, height: 900)
        let frame = NSRect(x: (vis.midX - size.width / 2).rounded(), y: (vis.midY - size.height / 2 + vis.height * 0.04).rounded(),
                           width: size.width, height: size.height)
        let w = NSWindow(contentRect: frame, styleMask: [.borderless], backing: .buffered, defer: false)
        w.isOpaque = false
        w.backgroundColor = .clear
        w.hasShadow = true
        w.level = .floating
        w.ignoresMouseEvents = true   // it can't be skipped
        w.isReleasedWhenClosed = false
        w.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .ignoresCycle]
        w.appearance = NSAppearance(named: .darkAqua)
        let host = NSHostingView(rootView: SplashView(model: self))
        host.frame = NSRect(origin: .zero, size: size)
        if #available(macOS 26.0, *) {
            let glass = NSGlassEffectView(frame: host.frame)
            glass.cornerRadius = 30
            glass.style = .regular
            glass.tintColor = NSColor(srgbRed: 0.02, green: 0.07, blue: 0.04, alpha: 0.72)
            glass.contentView = host
            w.contentView = glass
        } else {
            let blur = NSVisualEffectView(frame: host.frame)
            blur.material = .hudWindow
            blur.blendingMode = .behindWindow
            blur.state = .active
            blur.wantsLayer = true
            blur.layer?.cornerRadius = 30
            blur.layer?.masksToBounds = true
            blur.addSubview(host)
            w.contentView = blur
        }
        w.alphaValue = 0
        w.orderFrontRegardless()   // unseen: drawn once now, not as it begins
        panel = w
        DispatchQueue.main.async { [weak self] in self?.prewarmPalette() }

        // Anything else that opens while this is up waits for it: caught as
        // it becomes key or main (inside its ordering in, before it's drawn),
        // and swept for every few milliseconds besides. Circuit windows are
        // caught earlier still (WindowConfigurator calls `holdEarly`).
        for name in [NSWindow.didBecomeKeyNotification, NSWindow.didBecomeMainNotification] {
            observers.append(NotificationCenter.default.addObserver(forName: name, object: nil, queue: nil) { [weak self] n in
                MainActor.assumeIsolated { if let w = n.object as? NSWindow { self?.holdEarly(w, from: name.rawValue) } }
            })
        }
        watcher = Timer.scheduledTimer(withTimeInterval: 0.01, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.tick() }
        }
        RunLoop.main.add(watcher!, forMode: .common)
        // Whatever happens, it plays.
        DispatchQueue.main.asyncAfter(deadline: .now() + 3) { [weak self] in self?.begin() }
    }
    private var observers: [NSObjectProtocol] = []
    private var activity: NSObjectProtocol?

    /// A window arriving while the panel is up: hidden before it's seen.
    func holdEarly(_ w: NSWindow, from: String = "sweep") {
        guard active, w !== panel, w.level == .normal, !held.contains(where: { $0 === w }) else { return }
        SplashDebug.log("hold (\(from)) \(w.title) visible=\(w.isVisible)")
        w.alphaValue = 0
        held.append(w)
    }

    /// The launch's heavy work is done (LaunchDelegate): play the panel now,
    /// on an otherwise idle main thread.
    func begin() {
        guard active, !began, let w = panel else { return }
        began = true
        SplashDebug.log("begin")
        start = Date()
        NSAnimationContext.runAnimationGroup { ctx in
            ctx.duration = 0.2
            w.animator().alphaValue = 1
        }
        work()
    }

    /// The palette's pictures for the family it opens on, drawn before the
    /// panel plays so the window is ready the moment it shows.
    private func prewarmPalette() {
        let prefs = Prefs.shared
        let categories = GateLibrary.categories
        let index = UserDefaults.standard.integer(forKey: "paletteCategory")
        guard let family = categories.indices.contains(index) ? categories[index] : categories.first else { return }
        for g in GateLibrary.gates(in: family) {
            _ = TileCache.image(g.name, size: CGFloat(prefs.gateSize), dark: prefs.dark, scale: 2)
        }
    }

    // MARK: The real work, step by step

    private func step(_ text: String, _ progress: Double) {
        // Each line stays up long enough to be read.
        let at = max(elapsed, lastStepAt + (Self.reduceMotion ? 0.12 : 0.32))
        lastStepAt = at
        DispatchQueue.main.asyncAfter(deadline: .now() + max(0, at - elapsed)) { [weak self] in
            guard let self else { return }
            self.status = text
            self.target = max(self.target, progress)
        }
    }

    /// What was done, a line at a time: the library, the palette, the
    /// simulator, the circuit (all done by now; each line stays up long
    /// enough to read).
    private func work() {
        step("Loading the gate library", 0.12)
        let categories = GateLibrary.categories
        let gates = categories.reduce(0) { $0 + GateLibrary.gates(in: $1).count }
        step("\(gates) gates in \(categories.count) families", 0.34)
        step("Drawing the palette", 0.5)
        step("Starting the simulator", 0.66)
        let w = held.first { NSDocumentController.shared.document(for: $0) != nil }
        let url = w.flatMap { NSDocumentController.shared.document(for: $0)?.fileURL }
        let name = Library.displayName(for: url) ?? url?.deletingPathExtension().lastPathComponent
        step(name.map { "Opening \u{201C}\($0)\u{201D}" } ?? "Opening a new circuit", 0.86)
        step("Ready", 1)
        markReady()
    }

    private func markReady() {
        let at = lastStepAt + 0.3
        readyAt = at
        // Time for the bar to glide home after the last step.
        dissolveAt = min(6, max(Self.reduceMotion ? 1.1 : 2.3, at + 0.3))
    }

    // MARK: Holding windows, and the end

    private func tick() {
        guard active else { return }
        for w in NSApp.windows where w.isVisible { holdEarly(w) }
        guard began else { return }
        if let at = freezeAt, elapsed >= at { return }
        if elapsed >= dissolveAt { finish() }
    }

    private func finish() {
        guard active else { return }
        active = false
        watcher?.invalidate()
        watcher = nil
        for o in observers { NotificationCenter.default.removeObserver(o) }
        observers = []
        let windows = held
        held = []
        SplashDebug.log("finish: \(windows.count) window(s) coming in")
        // They come in where the launch screen was (the screen you were
        // looking at), not wherever macOS put them.
        if let screen = panel?.screen {
            let vis = screen.visibleFrame
            for w in windows where !w.styleMask.contains(.fullScreen) && w.screen !== screen {
                var f = w.frame
                f.size.width = min(f.width, vis.width)
                f.size.height = min(f.height, vis.height)
                f.origin = NSPoint(x: (vis.midX - f.width / 2).rounded(), y: (vis.midY - f.height / 2).rounded())
                w.setFrame(f, display: false)
            }
        }
        // The canvases fade their grid in with the windows (their own appear
        // played while they were hidden).
        NotificationCenter.default.post(name: .clSplashDone, object: nil)
        NSAnimationContext.runAnimationGroup { ctx in
            ctx.duration = Self.dissolveTime
            ctx.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
            for w in windows { w.animator().alphaValue = 1 }
            panel?.animator().alphaValue = 0
        }
        // Done on a clock of its own, not the animation's completion: when
        // macOS doesn't run the fade (the windows in Stage Manager's strip,
        // say), that never comes. Every window it held ends fully visible.
        DispatchQueue.main.asyncAfter(deadline: .now() + Self.dissolveTime + 0.05) { [weak self] in
            for w in windows where w.alphaValue < 1 { w.alphaValue = 1 }
            SplashDebug.log("dissolved")
            self?.panel?.orderOut(nil)
            self?.panel = nil
            if let a = self?.activity { ProcessInfo.processInfo.endActivity(a); self?.activity = nil }
            windows.first(where: { NSDocumentController.shared.document(for: $0) != nil })?.makeKeyAndOrderFront(nil)
        }
    }
}

private struct SplashView: View {
    @ObservedObject var model: Splash

    var body: some View {
        TimelineView(.animation(minimumInterval: 1.0 / 120, paused: !model.began)) { tl in
            let t = min(tl.date.timeIntervalSince(model.start), model.freezeAt ?? .infinity)
            let _ = SplashDebug.frame(t)
            content(t)
        }
        .frame(width: 560, height: 372)
    }

    private static func ease(_ x: Double) -> Double { let c = min(1, max(0, x)); return 1 - pow(1 - c, 3) }
    private static func span(_ t: Double, _ a: Double, _ b: Double) -> Double { ease((t - a) / (b - a)) }

    @ViewBuilder private func content(_ t: Double) -> some View {
        let calm = Splash.reduceMotion
        let rise = calm ? Self.span(t, 0, 0.3) : Self.span(t, 0.08, 0.85)
        let out = Self.span(t, model.dissolveAt, model.dissolveAt + Splash.dissolveTime)
        let glow = Self.span(t, 0.3, 1.1) * (0.85 + 0.15 * sin(t * 2.6))
        ZStack {
            // The icon's ground: near-black green, its grid showing faintly.
            LinearGradient(colors: [Brand.ink.opacity(0.78), Brand.inkDeep.opacity(0.9)], startPoint: .top, endPoint: .bottom)
            Grid(fade: Self.span(t, 0.1, 0.9))
            // A green bloom behind the icon.
            RadialGradient(colors: [Brand.neon.opacity(0.30 * glow), Brand.neon.opacity(0)],
                           center: .init(x: 0.5, y: 0.36), startRadius: 4, endRadius: 210)
            Traces(progress: calm ? 1 : Self.span(t, 0.35, 1.15), glow: glow)
            VStack(spacing: 0) {
                Image(nsImage: Brand.icon).resizable().interpolation(.high)
                    .frame(width: 128, height: 128)
                    .clipShape(RoundedRectangle(cornerRadius: 128 * Brand.corner, style: .continuous))
                    .shadow(color: Brand.neon.opacity(0.45 * glow), radius: 22)
                    .shadow(color: .black.opacity(0.5), radius: 12, y: 8)
                    .scaleEffect(0.9 + 0.1 * rise)
                    .offset(y: calm ? 0 : 26 * (1 - rise))
                    .blur(radius: calm ? 0 : 16 * (1 - rise))
                    .opacity(rise)
                    .padding(.top, 44)
                Title(t: t, calm: calm).padding(.top, 20)
                Text("LOGIC SIMULATOR  ·  FOR MAC")
                    .font(.system(size: 10, weight: .semibold)).kerning(2.4)
                    .foregroundStyle(Brand.dim.opacity(0.75))
                    .opacity(Self.span(t, 1.05, 1.45))
                    .padding(.top, 8)
                Spacer()
                Text(model.status)
                    .font(.system(size: 11.5, weight: .medium)).monospacedDigit()
                    .foregroundStyle(Brand.dim)
                    .contentTransition(.opacity)
                    .animation(.easeOut(duration: 0.2), value: model.status)
                    .opacity(Self.span(t, 0.5, 0.9))
                    .padding(.bottom, 14)
                Bar(fill: shownProgress(t)).padding(.horizontal, 30).padding(.bottom, 24)
            }
            if !calm { Sweep(phase: Self.span(t, 0.85, 1.75)) }
        }
        .clipShape(RoundedRectangle(cornerRadius: 30, style: .continuous))
        .overlay(RoundedRectangle(cornerRadius: 30, style: .continuous)
            .strokeBorder(LinearGradient(colors: [Color.white.opacity(0.22), Brand.neon.opacity(0.12), Color.white.opacity(0.05)],
                                         startPoint: .top, endPoint: .bottom), lineWidth: 1))
        .scaleEffect(calm ? 1 : 1 + 0.035 * out)
        .blur(radius: calm ? 0 : 7 * out)
        .opacity(1 - out)
    }

    /// The bar follows the real work, at a steady pace: never ahead of what's
    /// done, never faster than the panel's own time.
    /// The bar glides, every frame: along one smooth curve towards 92% while
    /// the work goes on, then from wherever it is to the end once the work
    /// is done. The work decides when it finishes; nothing jumps or stalls.
    private func shownProgress(_ t: Double) -> Double {
        // A fixed pace (not tied to when the panel goes, which can move).
        let pace = Splash.reduceMotion ? 0.9 : 2.1
        func smooth(_ x: Double) -> Double { let c = min(1, max(0, x)); return c * c * (3 - 2 * c) }
        func early(_ t: Double) -> Double { 0.92 * smooth(t / pace) }
        guard let r = model.readyAt, t > r else { return early(t) }
        let from = early(r)
        let end = max(r + 0.25, model.dissolveAt - 0.05)
        return from + (1 - from) * smooth((t - r) / (end - r))
    }

    /// The icon's faint grid, strongest in the middle.
    private struct Grid: View {
        let fade: Double
        var body: some View {
            Canvas { ctx, size in
                var p = Path()
                let step: CGFloat = 28
                var x: CGFloat = size.width.truncatingRemainder(dividingBy: step) / 2
                while x < size.width { p.move(to: CGPoint(x: x, y: 0)); p.addLine(to: CGPoint(x: x, y: size.height)); x += step }
                var y: CGFloat = size.height.truncatingRemainder(dividingBy: step) / 2
                while y < size.height { p.move(to: CGPoint(x: 0, y: y)); p.addLine(to: CGPoint(x: size.width, y: y)); y += step }
                ctx.stroke(p, with: .color(Brand.neon.opacity(0.07 * fade)), lineWidth: 0.5)
            }
            .mask(RadialGradient(colors: [.white, .white.opacity(0)], center: .center, startRadius: 40, endRadius: 320))
        }
    }

    /// The traces from the icon, drawn in: two coming in from the left, one
    /// leaving to the right, each ending in a dot.
    private struct Traces: View {
        let progress: Double
        let glow: Double
        var body: some View {
            Canvas { ctx, size in
                let cy: CGFloat = 44 + 64
                let left = size.width / 2 - 64 - 6, right = size.width / 2 + 64 + 6
                let lines: [[CGPoint]] = [
                    [CGPoint(x: 26, y: cy - 12), CGPoint(x: left - 40, y: cy - 12), CGPoint(x: left - 22, y: cy - 4), CGPoint(x: left, y: cy - 4)],
                    [CGPoint(x: 26, y: cy + 12), CGPoint(x: left - 40, y: cy + 12), CGPoint(x: left - 22, y: cy + 4), CGPoint(x: left, y: cy + 4)],
                    [CGPoint(x: right, y: cy + 14), CGPoint(x: size.width - 26, y: cy + 14)],
                ]
                for (i, pts) in lines.enumerated() {
                    // The incoming traces draw from the edge towards the
                    // icon, the outgoing one from the icon out.
                    var path = Path()
                    path.addLines(pts)
                    let trimmed = path.trimmedPath(from: 0, to: progress)
                    ctx.stroke(trimmed, with: .color(Brand.neon.opacity(0.18 * glow)), style: StrokeStyle(lineWidth: 7, lineCap: .round, lineJoin: .round))
                    ctx.stroke(trimmed, with: .color(Brand.neon.opacity(0.85)), style: StrokeStyle(lineWidth: 1.6, lineCap: .round, lineJoin: .round))
                    // The dot at the far end appears once the line reaches it.
                    let end = i < 2 ? pts[0] : pts[pts.count - 1]
                    if progress > 0.96 {
                        let r: CGFloat = 4
                        ctx.fill(Path(ellipseIn: CGRect(x: end.x - r * 2, y: end.y - r * 2, width: r * 4, height: r * 4)), with: .color(Brand.neon.opacity(0.18 * glow)))
                        ctx.fill(Path(ellipseIn: CGRect(x: end.x - r, y: end.y - r, width: r * 2, height: r * 2)), with: .color(Brand.neon))
                    }
                }
            }
            .allowsHitTesting(false)
        }
    }

    /// "CedarLogic", a letter at a time, in brushed silver with a green glow.
    private struct Title: View {
        let t: Double
        let calm: Bool
        var body: some View {
            HStack(spacing: 0.5) {
                ForEach(Array("CedarLogic".enumerated()), id: \.offset) { i, ch in
                    let p = calm ? SplashView.span(t, 0.2, 0.5) : SplashView.span(t, 0.55 + Double(i) * 0.045, 0.9 + Double(i) * 0.045)
                    Text(String(ch))
                        .font(.system(size: 36, weight: .semibold, design: .default))
                        .foregroundStyle(Brand.silver)
                        .shadow(color: Brand.neon.opacity(0.35 * p), radius: 10)
                        .opacity(p)
                        .offset(y: calm ? 0 : 9 * (1 - p))
                        .blur(radius: calm ? 0 : 5 * (1 - p))
                }
            }
        }
    }

    /// A soft band of light passing across the glass.
    private struct Sweep: View {
        let phase: Double
        var body: some View {
            GeometryReader { g in
                LinearGradient(colors: [.white.opacity(0), .white.opacity(0.13), .white.opacity(0)],
                               startPoint: .leading, endPoint: .trailing)
                    .frame(width: 150, height: g.size.height * 1.6)
                    .rotationEffect(.degrees(18))
                    .offset(x: -200 + (g.size.width + 400) * phase, y: -g.size.height * 0.3)
                    .blendMode(.plusLighter)
                    .opacity(phase > 0 && phase < 1 ? 1 : 0)
            }
            .allowsHitTesting(false)
        }
    }

    /// The thin neon bar along the bottom, with a bright head.
    private struct Bar: View {
        let fill: Double
        var body: some View {
            GeometryReader { g in
                let w = g.size.width * fill
                ZStack(alignment: .leading) {
                    Capsule().fill(Color.white.opacity(0.08))
                    Capsule().fill(LinearGradient(colors: [Brand.neonDeep, Brand.neon], startPoint: .leading, endPoint: .trailing))
                        .frame(width: max(0, w))
                        .shadow(color: Brand.neon.opacity(0.8), radius: 6)
                    Circle().fill(Color.white).frame(width: 5, height: 5)
                        .shadow(color: Brand.neon, radius: 6)
                        .offset(x: max(0, w - 2.5))
                        .opacity(fill > 0.01 && fill < 0.999 ? 1 : 0)
                }
            }
            .frame(height: 3)
        }
    }
}

// MARK: - Opening a circuit

/// A circuit opening: over the canvas, a small glass card with its name and
/// what's in it, a sweep of light and a quick line filling; then the card
/// lifts away and the circuit fades up. About 0.7 seconds.
struct OpeningCard: View {
    let start: Date
    let title: String
    let detail: String
    let dark: Bool
    let onReveal: () -> Void
    let onDone: () -> Void
    @State private var revealed = false

    static let total = 0.88
    static let freezeAt = ProcessInfo.processInfo.environment["CL_CARD_FREEZE"].flatMap(Double.init)

    private static func ease(_ x: Double) -> Double { let c = min(1, max(0, x)); return 1 - pow(1 - c, 3) }
    /// Soft at both ends: for things leaving, which shouldn't start abruptly.
    private static func smooth(_ x: Double) -> Double { let c = min(1, max(0, x)); return c * c * c * (c * (c * 6 - 15) + 10) }

    var body: some View {
        TimelineView(.animation(minimumInterval: 1.0 / 120)) { tl in
            // Before `start` (the window still landing) it's just the cover.
            // (CL_CARD_FREEZE=<seconds> holds it there, for development.)
            let t = min(tl.date.timeIntervalSince(start), Self.freezeAt ?? .infinity)
            let calm = Splash.reduceMotion
            let inP = Self.ease(t / 0.24)
            let line = Self.smooth((t - 0.1) / 0.45)
            let out = Self.smooth((t - 0.58) / 0.3)
            ZStack {
                // The canvas, held back until the card lifts.
                CLChrome(dark: dark).canvas.opacity(1 - out)
                card(line: line, sweep: calm ? 0 : Self.smooth((t - 0.14) / 0.5))
                    .scaleEffect(calm ? 1 : (0.94 + 0.06 * inP) * (1 + 0.04 * out))
                    .blur(radius: calm ? 0 : 6 * (1 - inP) + 5 * out)
                    .opacity(inP * (1 - out))
            }
            .onChange(of: t >= 0.58) { _, now in if now && !revealed { revealed = true; onReveal() } }
            .onChange(of: t >= Self.total) { _, now in if now { onDone() } }
        }
        .allowsHitTesting(false)
    }

    private func card(line: Double, sweep: Double) -> some View {
        HStack(spacing: 14) {
            Image(nsImage: Brand.icon).resizable().interpolation(.high)
                .frame(width: 46, height: 46)
                .clipShape(RoundedRectangle(cornerRadius: 46 * Brand.corner, style: .continuous))
                .shadow(color: Brand.neon.opacity(0.35), radius: 8)
            VStack(alignment: .leading, spacing: 3) {
                Text(title).font(.system(size: 15, weight: .semibold)).lineLimit(1)
                    .foregroundStyle(dark ? Color.white : Color.black.opacity(0.85))
                Text(detail).font(.system(size: 11.5)).foregroundStyle(.secondary).lineLimit(1)
                GeometryReader { g in
                    ZStack(alignment: .leading) {
                        Capsule().fill(Color.primary.opacity(0.08))
                        Capsule().fill(LinearGradient(colors: [Brand.neonDeep, Brand.neon], startPoint: .leading, endPoint: .trailing))
                            .frame(width: g.size.width * line)
                            .shadow(color: Brand.neon.opacity(0.6), radius: 4)
                    }
                }
                .frame(width: 170, height: 2.5)
                .padding(.top, 6)
            }
        }
        .padding(.horizontal, 20).padding(.vertical, 16)
        .frame(minWidth: 300)
        .background { glass }
        .overlay {
            GeometryReader { g in
                LinearGradient(colors: [.white.opacity(0), .white.opacity(dark ? 0.12 : 0.35), .white.opacity(0)],
                               startPoint: .leading, endPoint: .trailing)
                    .frame(width: 90, height: g.size.height * 2)
                    .rotationEffect(.degrees(18))
                    .offset(x: -120 + (g.size.width + 240) * sweep, y: -g.size.height * 0.5)
                    .blendMode(.plusLighter)
                    .opacity(sweep > 0 && sweep < 1 ? 1 : 0)
            }
            .clipShape(RoundedRectangle(cornerRadius: 20, style: .continuous))
        }
        .shadow(color: .black.opacity(dark ? 0.45 : 0.18), radius: 22, y: 10)
    }

    @ViewBuilder private var glass: some View {
        let shape = RoundedRectangle(cornerRadius: 20, style: .continuous)
        if #available(macOS 26.0, *) {
            shape.fill(.clear).glassEffect(.regular, in: shape)
                .overlay(shape.strokeBorder(Color.white.opacity(dark ? 0.14 : 0.5), lineWidth: 0.8))
        } else {
            shape.fill(.regularMaterial)
                .overlay(shape.strokeBorder(Color.white.opacity(dark ? 0.14 : 0.5), lineWidth: 0.8))
        }
    }
}
