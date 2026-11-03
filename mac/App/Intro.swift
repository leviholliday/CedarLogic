// The intro: the very first launch, and the first launch after an update
// with a What's New tour, open with the circuit drawing itself before the
// launch screen. The same intro both times (only the first launch has the
// sound). About 5 seconds:
//
//   0.00-0.45  over the circuit window, the dark canvas fades in from
//              nothing, its grid and the two input pins easing in with it.
//   0.40-1.96  two wires trace in from their pins, a bright head running
//              along them and slowing into each end; the letters of the
//              icon snap into place as the wires reach them (the C, the D
//              -- an AND gate -- and the L, which the output runs down to
//              the LED); a seven-segment display draws in beside it.
//   1.99-2.58  a clock pulse runs through, the D lights, and as it lands
//              the LED lights (the sound's bell chord lands here) and the
//              display spells the version, a digit at a time.
//   2.58-3.05  a beat, everything lit, so it registers.
//   3.05-3.64  the LED blooms into the icon itself, which flies into the
//              launch screen's panel (Splash.swift) as that fades in over it,
//              part-way into its own appearance: its traces drawing in, its
//              name writing itself in, its bar filling ...
//   3.52-4.42  ... and its sweep of light, at full length, as on any launch;
//   4.42-4.97  then it dissolves into the window (and the welcome, or What's
//              New, on top), a crossfade.
//
// Any key or click skips it; with Reduce Motion it's just the launch
// screen's still panel and a 0.4 s crossfade.
//
// Once drawn, the wires are the canvas's own: 1 pt lines in the dark
// canvas's low-wire colour with a dot where each meets its pin, and
// green while they're high, with the glow Simulation View gives a lit wire.
//
// `CedarLogic --render-intro <dir> [first|update]` draws it, frame by frame
// at 60 fps, to PNGs (mac/Tools/render-intro.sh makes a video), and a still
// of a real canvas wire beside the intro's for comparison.
// CL_INTRO=first|update plays it again on a normal launch, for development;
// CL_NO_INTRO=1 (or CL_NO_SPLASH) never.

import AppKit
import AVFoundation
import SwiftUI

@MainActor
enum Intro {
    enum Trigger: String { case firstLaunch = "first", update }

    /// Played once each: the first launch's, and the update's (keyed by the
    /// What's New version it introduced).
    static let firstKey = "cl.introFirstLaunchPlayed"
    static let updateKey = "cl.introUpdatePlayed"

    /// The timeline, in seconds (the header has it all). The canvas fades
    /// in over `fadeIn` (the intro's panel, as a whole) ...
    static let fadeIn = 0.45
    /// ... and the LED lights at `ledAt`; the first launch's sound starts
    /// part-way in so its bell chord (4.0 s into FirstLaunch.m4a) lands on it.
    static let ledAt = 2.58
    static let soundHit = 4.0
    /// It holds, lit, until it blooms.
    static let bloomAt = ledAt + 0.47
    /// The launch screen's panel comes in over the bloom ...
    static let cardIn = bloomAt + 0.27
    static let cardFade = 0.32
    /// ... at this moment of its own clock (its icon up, its name just
    /// starting to write itself in) ...
    static let splashAt = 0.65
    /// ... plays the rest of its appearance and its sweep, at full length
    /// (Splash's own timing) ...
    static var dissolveAt: Double { cardIn + Splash.sweepStart + Splash.sweepTime - splashAt }
    /// ... and dissolves into the window.
    static let fade = 0.55
    static var total: Double { dissolveAt + fade }
    /// How far the intro's panel has faded in, `t` seconds in: it's faded
    /// in as a whole by the window server (ease in and out), so it can't be
    /// missed if the first frames are slow; a picture of it uses this.
    static func fadeInShown(_ t: Double) -> Double {
        let c = min(1, max(0, t / fadeIn))
        return c < 0.5 ? 2 * c * c : 1 - pow(-2 * c + 2, 2) / 2
    }
    /// Reduce Motion: the launch screen's still, briefly, then a crossfade.
    static let calmHold = 0.5
    static let calmFade = 0.4

    /// The version the picture shows.
    static var version: String { WhatsNew.version }

    /// Which intro the saved settings say this launch is owed (none in the
    /// usual case). The same test as the launch screen's first-launch look
    /// and What's New, so it can't disagree with them.
    static func owed(_ d: UserDefaults = .standard) -> Trigger? {
        if !d.bool(forKey: "cl.hasSeenWelcome") && !d.bool(forKey: "cl.firstLaunchPlayed") && !d.bool(forKey: firstKey) {
            return .firstLaunch
        }
        if WhatsNew.shouldShow && d.string(forKey: updateKey) != WhatsNew.version { return .update }
        return nil
    }

    /// Never in the self-tests, the picture modes, the toolbar test, CI or
    /// any run with a `--` option (they all are tests or pictures).
    static var allowed: Bool {
        let env = ProcessInfo.processInfo.environment
        if ["CL_NO_INTRO", "CL_NO_SPLASH", "CL_SNAPSHOT", "CL_MENU_DUMP", "CL_BAR_TEST", "CL_FEEDBACK_SELFTEST", "CI"]
            .contains(where: { env[$0] != nil }) { return false }
        return !CommandLine.arguments.dropFirst().contains { $0.hasPrefix("--") }
    }

    /// The intro to play at this launch, if any.
    static func pending() -> Trigger? {
        guard allowed else { return nil }
        if let forced = ProcessInfo.processInfo.environment["CL_INTRO"] { return Trigger(rawValue: forced) ?? .firstLaunch }
        return owed()
    }

    static func markPlayed(_ t: Trigger) {
        switch t {
        case .firstLaunch: UserDefaults.standard.set(true, forKey: firstKey)
        case .update: UserDefaults.standard.set(WhatsNew.version, forKey: updateKey)
        }
    }

    // MARK: Sound

    /// The first launch's sound, from part-way in (see `ledAt`), faded up;
    /// with Reduce Motion, from just before its bell, as the window comes in.
    /// Off the main thread, as the launch screen's is (Splash.playSound).
    static func playSound(calm: Bool) {
        UserDefaults.standard.set(true, forKey: "cl.firstLaunchPlayed")
        guard let url = Bundle.main.url(forResource: "FirstLaunch", withExtension: "m4a") else { return }
        let from = calm ? soundHit - 0.15 - calmHold : soundHit - ledAt
        soundQueue.async {
            guard let p = try? AVAudioPlayer(contentsOf: url) else { return }
            p.prepareToPlay()
            p.currentTime = from
            p.volume = 0
            p.play()
            p.setVolume(0.75, fadeDuration: 0.3)
            player = p
            soundQueue.asyncAfter(deadline: .now() + 6) { if player === p { player = nil } }
        }
    }
    /// Skipped: the sound fades out with the picture.
    static func fadeSound() {
        soundQueue.async { player?.setVolume(0, fadeDuration: 0.35) }
    }
    private nonisolated static let soundQueue = DispatchQueue(label: "CedarLogic.introSound", qos: .userInitiated)
    nonisolated(unsafe) private static var player: AVAudioPlayer?
}

// MARK: - The picture

/// How the canvas draws a wire (guiWire::drawToScene, RenderStyle::wire):
/// a line `width` points wide, butt ends, square bends, a dot of `dot`
/// points' radius where it meets each pin (when the dots are on), in the
/// dark canvas's low-wire colour; lit, Simulation View's glow around it.
struct IntroWireLook {
    var width: CGFloat = 1
    var dot: CGFloat = 0.18 / 0.05   // wireDotSize (world units) at the canvas's opening zoom
    var dots = true
    var low = CGColor(srgbRed: 0.62, green: 0.67, blue: 0.76, alpha: 1)   // LowWireColor.silver

    @MainActor static var current: IntroWireLook {
        let p = Prefs.shared
        var l = IntroWireLook()
        l.width = p.wireScale
        l.dot = p.wireDotSize / 0.05
        l.dots = p.wireDotsAtBends
        if let rgb = (LowWireColor(rawValue: p.lowWire) ?? .silver).rgb {
            l.low = CGColor(srgbRed: rgb.0, green: rgb.1, blue: rgb.2, alpha: 1)
        } else {
            l.low = CGColor(srgbRed: 0.35, green: 0.35, blue: 0.35, alpha: 1)
        }
        return l
    }
}

/// The intro's layers, laid out for one size, and where everything is at a
/// moment (`apply`). Plain Core Animation, set frame by frame and never
/// animated implicitly, so a live frame and a rendered one are the same.
@MainActor
final class IntroScene {
    let root = CALayer()
    let size: CGSize
    let trigger: Intro.Trigger
    let look: IntroWireLook
    /// Where the launch screen's icon will be (its centre, in this scene's
    /// points): the bloomed icon flies there.
    let cardIcon: CGPoint
    static let cardIconSize: CGFloat = 128

    // The icon's art is 512 units square; the circuit is laid out in those
    // units and drawn at `k` points a unit, centred.
    private let k: CGFloat
    private let xf: CGAffineTransform

    private let ground = CALayer()
    private let grid = CAShapeLayer()
    private let vignette = CALayer()
    private let circuit = CALayer()   // the wires, pins, letters and LED, faded as one
    private let display = CALayer()   // the seven-segment display, which stays a little longer
    private var wires: [Wire] = []
    private var pins: [CAShapeLayer] = []
    private var letters: [Letter] = []
    private var digits: [Digit] = []
    private let housing = CAShapeLayer()
    private let gateGlow = CAShapeLayer()
    private let ledRing = CAShapeLayer()
    private let ledCore = CAShapeLayer()
    private let ledGlow = CALayer()
    private let bloom = CALayer()
    private let iconGlow = CALayer()
    private let icon = CALayer()

    /// A wire: drawn in (`draw`), then high behind the clock pulse (`pulse`).
    private struct Wire {
        let rest = CAShapeLayer(), highGlow1 = CAShapeLayer(), highGlow2 = CAShapeLayer(), high = CAShapeLayer()
        let trail: [CAShapeLayer]
        let trailGlow = CAShapeLayer()
        let head = CALayer()
        let bends: [(at: CGFloat, low: CAShapeLayer, high: CAShapeLayer)]
        let points: [CGPoint]
        let lengths: [CGFloat]   // running length at each point
        let draw: (Double, Double)
        let pulse: (Double, Double)
        var length: CGFloat { lengths.last ?? 1 }
    }
    private struct Letter {
        let outline = CAShapeLayer(), glow = CAShapeLayer()
        let popAt: Double
        let trace: (Double, Double)
    }
    private struct Digit {
        let lit = CAShapeLayer(), glow = CAShapeLayer()
        let at: Double
    }

    static let neon = CGColor(srgbRed: 0.22, green: 1.0, blue: 0.42, alpha: 1)
    static let headColor = CGColor(srgbRed: 0.82, green: 1.0, blue: 0.86, alpha: 1)
    static let silver = CGColor(srgbRed: 0.86, green: 0.9, blue: 0.88, alpha: 1)
    /// The Graphite theme's canvas and grid (Theme.swift): the app's dark look.
    static let canvas = CGColor(srgbRed: 0.075, green: 0.082, blue: 0.098, alpha: 1)
    static let gridMinor = CGColor(srgbRed: 0.16, green: 0.17, blue: 0.20, alpha: 1)
    static let gridMajor = CGColor(srgbRed: 0.23, green: 0.25, blue: 0.29, alpha: 1)

    init(size: CGSize, trigger: Intro.Trigger, scale: CGFloat, look: IntroWireLook = IntroWireLook(), cardIcon: CGPoint? = nil) {
        self.size = size
        self.trigger = trigger
        self.look = look
        // The launch screen sits a little above the middle (Splash), its
        // icon 44 points down from its top.
        self.cardIcon = cardIcon ?? CGPoint(x: size.width / 2, y: size.height / 2 + 36 + 186 - 44 - 64)
        // The circuit and its display span about 580 units across.
        k = min(size.width * 0.55 / 580, size.height * 0.84 / 512)
        // Icon units (y down, the circuit's middle at 300, 247) to points (y up).
        xf = CGAffineTransform(a: k, b: 0, c: 0, d: -k, tx: size.width / 2 - 300 * k, ty: size.height / 2 + 247 * k)
        root.frame = CGRect(origin: .zero, size: size)
        root.masksToBounds = true
        build()
        setScale(root, scale)
        apply(0)
    }

    private func p(_ x: CGFloat, _ y: CGFloat) -> CGPoint { CGPoint(x: x, y: y).applying(xf) }

    // MARK: Building

    private func build() {
        ground.frame = root.bounds
        ground.backgroundColor = Self.canvas
        root.addSublayer(ground)

        // The canvas's grid, minor and major lines, as the app draws it.
        let gridPath = CGMutablePath(), majorPath = CGMutablePath()
        let step: CGFloat = 20
        let cx = size.width / 2, cy = size.height / 2
        for i in -Int(cx / step) - 1...Int(cx / step) + 1 {
            let x = cx + CGFloat(i) * step
            (i % 5 == 0 ? majorPath : gridPath).addLines(between: [CGPoint(x: x, y: 0), CGPoint(x: x, y: size.height)])
        }
        for i in -Int(cy / step) - 1...Int(cy / step) + 1 {
            let y = cy + CGFloat(i) * step
            (i % 5 == 0 ? majorPath : gridPath).addLines(between: [CGPoint(x: 0, y: y), CGPoint(x: size.width, y: y)])
        }
        grid.frame = root.bounds
        grid.path = gridPath
        grid.strokeColor = Self.gridMinor
        grid.lineWidth = 0.5
        let major = CAShapeLayer()
        major.frame = grid.bounds
        major.path = majorPath
        major.strokeColor = Self.gridMajor
        major.lineWidth = 0.75
        grid.addSublayer(major)
        root.addSublayer(grid)
        // Darker towards the edges, so the eye goes to the middle.
        vignette.frame = root.bounds
        vignette.contents = Self.radial(size: 256, stops: [(0, CGColor(gray: 0, alpha: 0)), (0.45, CGColor(gray: 0, alpha: 0.02)),
                                                          (0.8, CGColor(gray: 0, alpha: 0.16)), (1, CGColor(gray: 0, alpha: 0.3))])
        root.addSublayer(vignette)

        circuit.frame = root.bounds
        root.addSublayer(circuit)
        display.frame = root.bounds
        root.addSublayer(display)

        // The AND gate's inside, lit when its output goes high (behind all).
        let inside = CGMutablePath()
        inside.move(to: CGPoint(x: 220, y: 148)); inside.addLine(to: CGPoint(x: 228, y: 148))
        inside.addArc(center: CGPoint(x: 228, y: 247), radius: 99, startAngle: -.pi / 2, endAngle: .pi / 2, clockwise: false)
        inside.addLine(to: CGPoint(x: 220, y: 346)); inside.closeSubpath()
        gateGlow.frame = root.bounds
        gateGlow.path = inside.copy(using: [xf])
        gateGlow.fillColor = Self.neon
        circuit.addSublayer(gateGlow)

        // The wires (in icon units), routed as the canvas routes them, square
        // bends: two inputs through the C into the D (an AND gate), and its
        // output down the L to the LED.
        wires = [makeWire([CGPoint(x: 20, y: 217.5), CGPoint(x: 124, y: 217.5), CGPoint(x: 124, y: 206), CGPoint(x: 196, y: 206)],
                          draw: (0.40, 1.29), pulse: (1.99, 2.29)),
                 makeWire([CGPoint(x: 20, y: 253.5), CGPoint(x: 132, y: 253.5), CGPoint(x: 132, y: 288), CGPoint(x: 196, y: 288)],
                          draw: (0.45, 1.31), pulse: (1.99, 2.29)),
                 makeWire([CGPoint(x: 351, y: 247), CGPoint(x: 376, y: 247), CGPoint(x: 376, y: 316), CGPoint(x: 486, y: 316)],
                          draw: (1.43, 1.96), pulse: (2.31, Intro.ledAt))]

        // The input pins: small rings, as in the icon.
        for y in [217.5, 253.5] as [CGFloat] {
            let ring = CAShapeLayer()
            let c = p(14, y), r = 6 * k
            ring.bounds = CGRect(x: 0, y: 0, width: r * 2 + 8, height: r * 2 + 8)
            ring.position = c
            ring.path = CGPath(ellipseIn: ring.bounds.insetBy(dx: 4, dy: 4), transform: nil)
            ring.fillColor = Self.canvas
            ring.strokeColor = Self.neon
            ring.lineWidth = max(1.5, 1.4 * k)
            circuit.addSublayer(ring)
            pins.append(ring)
        }

        // The letters, as outlines of the icon's: the C ...
        let c = CGMutablePath()
        c.addArc(center: CGPoint(x: 156, y: 247), radius: 88, startAngle: .pi * 55 / 180, endAngle: .pi * 305 / 180, clockwise: false)
        let cOutline = c.copy(strokingWithWidth: 24, lineCap: .butt, lineJoin: .miter, miterLimit: 10)
        // ... the D, which is an AND gate (its outside, then its inside) ...
        let dOutline = CGMutablePath()
        for (x, r) in [(CGFloat(196), CGFloat(123)), (220, 99)] {
            dOutline.move(to: CGPoint(x: x, y: 247 - r)); dOutline.addLine(to: CGPoint(x: 228, y: 247 - r))
            dOutline.addArc(center: CGPoint(x: 228, y: 247), radius: r, startAngle: -.pi / 2, endAngle: .pi / 2, clockwise: false)
            dOutline.addLine(to: CGPoint(x: x, y: 247 + r)); dOutline.closeSubpath()
        }
        // ... and the L, leaning as the icon's does.
        let l = CGMutablePath()
        l.addLines(between: [CGPoint(x: 395, y: 172), CGPoint(x: 419, y: 172), CGPoint(x: 377, y: 325),
                             CGPoint(x: 470, y: 325), CGPoint(x: 455, y: 350), CGPoint(x: 345, y: 350)])
        l.closeSubpath()
        letters = [makeLetter(cOutline, popAt: 0.64, trace: (0.54, 1.12)),
                   makeLetter(dOutline, popAt: 1.26, trace: (1.19, 1.58)),
                   makeLetter(l, popAt: 1.55, trace: (1.48, 1.89))]

        // The LED at the end of the output.
        let ledC = p(494, 316), lr = 8 * k
        for (layer, inset) in [(ledRing, CGFloat(0)), (ledCore, 2.5 * k)] {
            layer.bounds = CGRect(x: 0, y: 0, width: lr * 2 + 12, height: lr * 2 + 12)
            layer.position = ledC
            layer.path = CGPath(ellipseIn: layer.bounds.insetBy(dx: 6 + inset, dy: 6 + inset), transform: nil)
        }
        ledRing.fillColor = Self.canvas
        ledRing.strokeColor = Self.neon
        ledRing.lineWidth = max(1.5, 1.6 * k)
        ledCore.fillColor = CGColor(srgbRed: 0.85, green: 1, blue: 0.9, alpha: 1)
        ledGlow.bounds = CGRect(x: 0, y: 0, width: 150 * k, height: 150 * k)
        ledGlow.position = ledC
        ledGlow.contents = Self.glowImage
        circuit.addSublayer(ledGlow)
        circuit.addSublayer(ledRing)
        circuit.addSublayer(ledCore)

        buildDisplay()

        // The bloom, and the icon (where its own art lines up with the
        // circuit, to begin with).
        let side = max(size.width, size.height) * 1.6
        bloom.bounds = CGRect(x: 0, y: 0, width: side, height: side)
        bloom.position = ledC
        bloom.contents = Self.radial(size: 512, stops: [(0, CGColor(srgbRed: 0.9, green: 1, blue: 0.93, alpha: 1)),
                                                        (0.05, CGColor(srgbRed: 0.45, green: 1, blue: 0.6, alpha: 0.7)),
                                                        (0.22, CGColor(srgbRed: 0.22, green: 1, blue: 0.42, alpha: 0.16)),
                                                        (0.5, CGColor(srgbRed: 0.22, green: 1, blue: 0.42, alpha: 0.04)),
                                                        (1, CGColor(srgbRed: 0.22, green: 1, blue: 0.42, alpha: 0))])
        iconGlow.contents = Self.glowImage
        root.addSublayer(iconGlow)
        icon.contents = Self.iconImage
        icon.contentsGravity = .resizeAspect
        root.addSublayer(icon)
        root.addSublayer(bloom)
    }

    private func setScale(_ l: CALayer, _ s: CGFloat) {
        l.contentsScale = s
        for c in l.sublayers ?? [] { setScale(c, s) }
    }

    private func stroke(_ layer: CAShapeLayer, _ path: CGPath, _ color: CGColor, _ width: CGFloat, round: Bool = false) {
        layer.frame = root.bounds
        layer.path = path
        layer.fillColor = nil
        layer.strokeColor = color
        layer.lineWidth = width
        layer.lineCap = round ? .round : .butt
        layer.lineJoin = round ? .round : .miter
        circuit.addSublayer(layer)
    }

    private func makeWire(_ pts: [CGPoint], draw: (Double, Double), pulse: (Double, Double)) -> Wire {
        let points = pts.map { p($0.x, $0.y) }
        var lengths: [CGFloat] = [0]
        for i in 1..<points.count { lengths.append(lengths[i - 1] + hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y)) }
        // The dots where it meets its pins, low and lit (the canvas draws
        // them at the ends, its bends square: see wire-compare.png).
        var bends: [(at: CGFloat, low: CAShapeLayer, high: CAShapeLayer)] = []
        if look.dots {
            for i in [0, points.count - 1] {
                let r = look.dot
                let dot = CGPath(ellipseIn: CGRect(x: points[i].x - r, y: points[i].y - r, width: r * 2, height: r * 2), transform: nil)
                let low = CAShapeLayer(), high = CAShapeLayer()
                for (l, c) in [(low, look.low), (high, Self.neon)] {
                    l.frame = root.bounds
                    l.path = dot
                    l.fillColor = c
                }
                bends.append((lengths[i] / lengths[points.count - 1], low, high))
            }
        }
        // A bright trail behind the head, brightest at the head, in steps.
        let trail = (0..<6).map { _ in CAShapeLayer() }
        let w = Wire(trail: trail, bends: bends, points: points, lengths: lengths, draw: draw, pulse: pulse)
        let path = CGMutablePath()
        path.addLines(between: points)
        // At rest, as the canvas draws it ...
        stroke(w.rest, path, look.low, look.width)
        for b in bends { circuit.addSublayer(b.low) }
        // ... high, in green with Simulation View's glow ...
        stroke(w.highGlow1, path, Self.neon.copy(alpha: 0.10)!, look.width + 7)
        stroke(w.highGlow2, path, Self.neon.copy(alpha: 0.22)!, look.width + 3)
        stroke(w.high, path, Self.neon, look.width)
        for b in bends { circuit.addSublayer(b.high) }
        // ... and the head's trail and glow, on top.
        stroke(w.trailGlow, path, Self.neon.copy(alpha: 0.28)!, look.width + 5, round: true)
        for (i, t) in trail.enumerated() {
            stroke(t, path, Self.headColor.copy(alpha: 0.22 + 0.13 * CGFloat(i))!, look.width + 0.6, round: true)
        }
        w.head.bounds = CGRect(x: 0, y: 0, width: 30, height: 30)
        w.head.contents = Self.glowImage
        circuit.addSublayer(w.head)
        return w
    }

    private func makeLetter(_ outline: CGPath, popAt: Double, trace: (Double, Double)) -> Letter {
        let l = Letter(popAt: popAt, trace: trace)
        let path = outline.copy(using: [xf])!
        let box = path.boundingBoxOfPath.insetBy(dx: -12, dy: -12)
        var shift = CGAffineTransform(translationX: -box.minX, y: -box.minY)
        let local = path.copy(using: &shift)
        for (layer, width, color) in [(l.glow, 7 * k, Self.neon.copy(alpha: 0.22)!), (l.outline, max(1.4, 1.5 * k), Self.silver)] {
            layer.bounds = CGRect(origin: .zero, size: box.size)
            layer.position = CGPoint(x: box.midX, y: box.midY)
            layer.path = local
            layer.strokeColor = color
            layer.lineWidth = width
            layer.lineJoin = .round
            layer.fillColor = layer === l.outline ? Self.silver : nil
            layer.fillRule = .evenOdd
            circuit.addSublayer(layer)
        }
        return l
    }

    /// A seven-segment display beside the LED, in the outlines' style, that
    /// spells the version as the LED lights, a digit at a time.
    private func buildDisplay() {
        let chars = Array(Intro.version)
        let digitsCount = max(1, chars.filter(\.isNumber).count)
        let cellW: CGFloat = 24, gap: CGFloat = 16, h: CGFloat = 46
        let x0: CGFloat = 528, y0: CGFloat = 293
        let boxW = CGFloat(digitsCount) * cellW + CGFloat(digitsCount - 1) * gap + 22
        let box = CGPath(roundedRect: CGRect(x: x0 - 11, y: y0 - 12, width: boxW, height: h + 24), cornerWidth: 7, cornerHeight: 7, transform: nil)
        housing.frame = root.bounds
        housing.path = box.copy(using: [xf])
        housing.strokeColor = Self.silver
        housing.fillColor = Self.silver.copy(alpha: 0.06)
        housing.lineWidth = max(1.4, 1.5 * k)
        display.addSublayer(housing)

        // The segments of one digit (a to g), as hexagonal bars, leaning.
        let t: CGFloat = 4.6
        func bar(_ a: CGPoint, _ b: CGPoint) -> CGPath {
            let p = CGMutablePath()
            if a.y == b.y {   // across
                p.addLines(between: [CGPoint(x: a.x + t / 2, y: a.y), CGPoint(x: a.x + t, y: a.y - t / 2), CGPoint(x: b.x - t, y: b.y - t / 2),
                                     CGPoint(x: b.x - t / 2, y: b.y), CGPoint(x: b.x - t, y: b.y + t / 2), CGPoint(x: a.x + t, y: a.y + t / 2)])
            } else {          // down
                p.addLines(between: [CGPoint(x: a.x, y: a.y + t / 2), CGPoint(x: a.x + t / 2, y: a.y + t), CGPoint(x: b.x + t / 2, y: b.y - t),
                                     CGPoint(x: b.x, y: b.y - t / 2), CGPoint(x: b.x - t / 2, y: b.y - t), CGPoint(x: a.x - t / 2, y: a.y + t)])
            }
            p.closeSubpath()
            return p
        }
        func segments(at x: CGFloat) -> [CGPath] {
            let l = x, r = x + cellW, top = y0, mid = y0 + h / 2, bottom = y0 + h
            return [bar(CGPoint(x: l, y: top), CGPoint(x: r, y: top)),          // a
                    bar(CGPoint(x: r, y: top), CGPoint(x: r, y: mid)),          // b
                    bar(CGPoint(x: r, y: mid), CGPoint(x: r, y: bottom)),       // c
                    bar(CGPoint(x: l, y: bottom), CGPoint(x: r, y: bottom)),    // d
                    bar(CGPoint(x: l, y: mid), CGPoint(x: l, y: bottom)),       // e
                    bar(CGPoint(x: l, y: top), CGPoint(x: l, y: mid)),          // f
                    bar(CGPoint(x: l, y: mid), CGPoint(x: r, y: mid))]          // g
        }
        let lit: [Character: [Int]] = ["0": [0, 1, 2, 3, 4, 5], "1": [1, 2], "2": [0, 1, 6, 4, 3], "3": [0, 1, 6, 2, 3],
                                       "4": [5, 6, 1, 2], "5": [0, 5, 6, 2, 3], "6": [0, 5, 4, 3, 2, 6], "7": [0, 1, 2],
                                       "8": [0, 1, 2, 3, 4, 5, 6], "9": [0, 1, 2, 3, 5, 6]]
        // Leaning a little, as a display's digits do (about the baseline).
        let lean = CGAffineTransform(translationX: 0, y: -(y0 + h))
            .concatenating(CGAffineTransform(a: 1, b: 0, c: -0.1, d: 1, tx: 0, ty: 0))
            .concatenating(CGAffineTransform(translationX: 0, y: y0 + h))
            .concatenating(xf)
        var x = x0, order = 0
        for ch in chars {
            let off = CGMutablePath(), on = CGMutablePath()
            if ch.isNumber {
                let segs = segments(at: x)
                for (i, s) in segs.enumerated() { (lit[ch]?.contains(i) == true ? on : off).addPath(s, transform: lean) }
                x += cellW + gap
            } else if ch == "." {
                let dp = CGPath(ellipseIn: CGRect(x: x - gap / 2 - 3, y: y0 + h - 3, width: 6, height: 6), transform: nil)
                on.addPath(dp, transform: lean)
            } else { continue }
            let dim = CAShapeLayer()
            dim.frame = root.bounds
            dim.path = off
            dim.fillColor = Self.neon.copy(alpha: 0.07)
            display.addSublayer(dim)
            let d = Digit(at: Intro.ledAt + 0.1 * Double(order))
            d.glow.frame = root.bounds
            d.glow.path = on
            d.glow.fillColor = nil
            d.glow.strokeColor = Self.neon.copy(alpha: 0.3)
            d.glow.lineWidth = 5
            d.glow.lineJoin = .round
            d.lit.frame = root.bounds
            d.lit.path = on
            d.lit.fillColor = Self.neon
            display.addSublayer(d.glow)
            display.addSublayer(d.lit)
            digits.append(d)
            order += 1
        }
    }

    // MARK: Time

    private static func clamp(_ x: Double) -> Double { min(1, max(0, x)) }
    /// 0...1 between a and b, soft at both ends.
    private static func smooth(_ t: Double, _ a: Double, _ b: Double) -> Double {
        let c = clamp((t - a) / (b - a)); return c * c * (3 - 2 * c)
    }
    /// 0...1 between a and b, fast out of the gate.
    private static func ease(_ t: Double, _ a: Double, _ b: Double) -> Double {
        let c = clamp((t - a) / (b - a)); return 1 - pow(1 - c, 3)
    }
    /// A signal's run along a wire: away gently, then slowing all the way
    /// into the far end (an ease out, never past it).
    private static func run(_ t: Double, _ a: Double, _ b: Double) -> Double {
        let c = clamp((t - a) / (b - a))
        let lift = c * c * (3 - 2 * c)          // a soft start ...
        let land = 1 - pow(1 - c, 3.2)          // ... and a long, slow landing
        return lift * 0.25 + land * 0.75
    }
    /// A snap into place: from small up past full size and back.
    private static func pop(_ t: Double, _ at: Double, from: Double = 0.6) -> Double {
        let u = clamp((t - at) / 0.3)
        if u <= 0 { return from }
        let s = 1 - pow(1 - u, 3)
        return from + (1 - from) * s + 0.22 * sin(u * .pi) * (1 - u)
    }

    /// Everything where it is `t` seconds in.
    func apply(_ t: Double) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        defer { CATransaction.commit() }
        let S = Self.self
        let bloomAt = Intro.bloomAt
        // The canvas itself fades in with the intro's panel as a whole (in
        // a picture, `Intro.fadeInShown`); its grid comes up a little after.
        grid.opacity = Float(S.smooth(t, 0.1, 0.75) * 0.85)

        // The circuit goes as the icon arrives over it; the display (and
        // the canvas) as the launch screen's panel comes in.
        circuit.opacity = Float(1 - S.smooth(t, bloomAt + 0.12, bloomAt + 0.32))
        display.opacity = Float(S.smooth(t, 1.86, 2.0) * (1 - S.smooth(t, Intro.cardIn, Intro.cardIn + Intro.cardFade)))

        // The pins ease in with the canvas, just before the wires leave them.
        for (i, pin) in pins.enumerated() {
            let at = 0.16 + 0.07 * Double(i)
            pin.opacity = Float(S.smooth(t, at, at + 0.2))
            pin.setAffineTransform(CGAffineTransform(scaleX: S.pop(t, at, from: 0.2), y: S.pop(t, at, from: 0.2)))
        }

        for w in wires { applyWire(w, t) }

        for l in letters {
            let tr = S.smooth(t, l.trace.0, l.trace.1)
            let shown = S.smooth(t, l.popAt - 0.06, l.popAt + 0.04)
            let sc = S.pop(t, l.popAt, from: 0.82)
            for layer in [l.outline, l.glow] {
                layer.strokeEnd = tr
                layer.opacity = Float(shown)
                layer.setAffineTransform(CGAffineTransform(scaleX: sc, y: sc))
            }
            // Filled faintly once outlined: silver glass.
            l.outline.fillColor = Self.silver.copy(alpha: 0.13 * S.smooth(t, l.trace.1 - 0.15, l.trace.1 + 0.1))
            // The clock pulse catches the outlines as it passes, and they
            // stay lit until the bloom.
            l.glow.strokeColor = Self.neon.copy(alpha: 0.22 + 0.4 * S.smooth(t, 2.16, 2.4) * (1 - S.smooth(t, bloomAt, bloomAt + 0.2)))
        }
        // The gate fires: its inside glows green.
        gateGlow.opacity = Float(0.08 + 0.32 * S.ease(t, 2.29, 2.45))
        gateGlow.isHidden = t < 1.26

        // The LED: in when the output reaches it, lit as the pulse lands
        // with a flash that settles, held lit, then swelling into the bloom.
        let ledIn = S.pop(t, 1.91, from: 0.3)
        ledRing.setAffineTransform(CGAffineTransform(scaleX: ledIn, y: ledIn))
        ledRing.opacity = Float(S.smooth(t, 1.88, 1.98))
        let lit = S.ease(t, Intro.ledAt, Intro.ledAt + 0.06)
        ledCore.opacity = Float(lit)
        let core = S.pop(t, Intro.ledAt, from: 0.7)
        ledCore.setAffineTransform(CGAffineTransform(scaleX: core, y: core))
        ledGlow.opacity = Float(lit)
        let fu = S.clamp((t - Intro.ledAt) / 0.45)
        let flash = fu > 0 ? pow(1 - fu, 2) * min(1, fu / 0.08) : 0
        let g = 0.6 + 0.8 * lit + 0.55 * flash + 0.4 * S.smooth(t, bloomAt - 0.22, bloomAt)
        ledGlow.setAffineTransform(CGAffineTransform(scaleX: g, y: g))

        // The version, a digit at a time, each flaring as it lights.
        housing.strokeEnd = S.smooth(t, 1.84, 2.13)
        for d in digits {
            let on = S.ease(t, d.at, d.at + 0.06)
            d.lit.opacity = Float(on)
            d.glow.opacity = Float(on * (0.6 + 0.4 * (1 - S.smooth(t, d.at + 0.06, d.at + 0.36))))
        }

        // The bloom: a flash out of the LED that fills the canvas.
        let bl = S.ease(t, bloomAt, bloomAt + 0.44)
        let bs = 0.04 + 0.96 * bl
        bloom.setAffineTransform(CGAffineTransform(scaleX: bs, y: bs))
        bloom.opacity = Float(0.9 * S.smooth(t, bloomAt - 0.02, bloomAt + 0.08) * (1 - S.smooth(t, bloomAt + 0.14, bloomAt + 0.56)))

        // The icon: over the circuit, its own art where the circuit was, then
        // flying to where the launch screen's own icon is, and handing over.
        let big = CGRect(origin: p(0, 512), size: CGSize(width: 512 * k, height: 512 * k))
        let small = Self.cardIconSize
        let target = CGRect(x: cardIcon.x - small / 2, y: cardIcon.y - small / 2, width: small, height: small)
        // (Landed just as the panel is half in, so they don't double up.)
        let fly = S.smooth(t, bloomAt + 0.14, bloomAt + 0.44)
        func mix(_ a: CGFloat, _ b: CGFloat) -> CGFloat { a + (b - a) * fly }
        let frame = CGRect(x: mix(big.minX, target.minX), y: mix(big.minY, target.minY),
                           width: mix(big.width, target.width), height: mix(big.height, target.height))
        icon.bounds = CGRect(origin: .zero, size: frame.size)
        icon.position = CGPoint(x: frame.midX, y: frame.midY)
        // Gone into the panel's own icon as the panel finishes coming in.
        let landed = Intro.cardIn + Intro.cardFade
        icon.opacity = Float(S.smooth(t, bloomAt + 0.05, bloomAt + 0.26) * (1 - S.smooth(t, landed - 0.14, landed)))
        iconGlow.bounds = CGRect(x: 0, y: 0, width: frame.width * 1.9, height: frame.height * 1.9)
        iconGlow.position = icon.position
        iconGlow.opacity = icon.opacity * Float(0.55 + 0.25 * (1 - fly))
    }

    /// A wire at `t`: laid in behind its head, which slows into the far end
    /// and settles there as its trail draws in after it; then the same
    /// again, brighter, for the clock pulse, the wire high behind it.
    private func applyWire(_ w: Wire, _ t: Double) {
        let S = Self.self
        let d = S.run(t, w.draw.0, w.draw.1)
        let q = S.run(t, w.pulse.0, w.pulse.1)
        w.rest.strokeEnd = d
        w.rest.isHidden = d <= 0
        // The near dot as the run leaves it, the far one as it lands.
        for b in w.bends {
            let lowAt = b.at < 0.5 ? w.draw.0 : w.draw.1 - 0.02, highAt = b.at < 0.5 ? w.pulse.0 : w.pulse.1 - 0.02
            b.low.opacity = Float(S.smooth(t, lowAt, lowAt + 0.08))
            b.high.opacity = Float(S.smooth(t, highAt, highAt + 0.08))
        }
        for l in [w.high, w.highGlow1, w.highGlow2] {
            l.strokeEnd = q
            l.isHidden = q <= 0
        }
        // Whichever run is going (or has just landed): the pulse, else the
        // drawing in.
        let pulsing = t >= w.pulse.0
        let (f, start, end) = pulsing ? (q, w.pulse.0, w.pulse.1) : (d, w.draw.0, w.draw.1)
        // After landing, the trail draws in after the head and it all fades.
        let settle = S.ease(t, end, end + 0.22)
        let shown = S.smooth(t, start, start + 0.05) * (1 - settle)
        // A trail about 90 points long, or the wire, whichever is shorter.
        let trailFrac = min(1, Double(90 / w.length)) * (1 - settle)
        let n = w.trail.count
        for (i, layer) in w.trail.enumerated() {
            // The steps overlap at the head: brightest there, fading back.
            let part = Double(n - i) / Double(n)
            layer.strokeStart = max(0, f - trailFrac * part)
            layer.strokeEnd = f
            layer.opacity = Float(shown * (pulsing ? 1 : 0.8))
            layer.isHidden = shown <= 0
        }
        w.trailGlow.strokeStart = max(0, f - trailFrac * 0.6)
        w.trailGlow.strokeEnd = f
        w.trailGlow.opacity = Float(shown)
        w.trailGlow.isHidden = shown <= 0
        // The head: a soft light that narrows as it lands.
        w.head.position = point(on: w, at: f)
        let sc = (pulsing ? 1.15 : 0.9) * (1 - 0.45 * settle)
        w.head.setAffineTransform(CGAffineTransform(scaleX: sc, y: sc))
        w.head.opacity = Float(shown)
    }

    private func point(on w: Wire, at f: Double) -> CGPoint {
        let target = CGFloat(f) * w.length
        guard let i = w.lengths.firstIndex(where: { $0 >= target }), i > 0 else { return w.points.first ?? .zero }
        let a = w.points[i - 1], b = w.points[i]
        let span = w.lengths[i] - w.lengths[i - 1]
        let u = span > 0 ? (target - w.lengths[i - 1]) / span : 0
        return CGPoint(x: a.x + (b.x - a.x) * u, y: a.y + (b.y - a.y) * u)
    }

    /// For the comparison still: the first wire's middle, in points.
    var firstWireBend: CGPoint { wires.first?.points[1] ?? .zero }

    // MARK: Images

    private static func radial(size: Int, stops: [(CGFloat, CGColor)]) -> CGImage? {
        guard let ctx = CGContext(data: nil, width: size, height: size, bitsPerComponent: 8, bytesPerRow: 0,
                                  space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue),
              let g = CGGradient(colorsSpace: CGColorSpace(name: CGColorSpace.sRGB), colors: stops.map(\.1) as CFArray,
                                 locations: stops.map(\.0)) else { return nil }
        let c = CGPoint(x: CGFloat(size) / 2, y: CGFloat(size) / 2)
        // Out to the corners: the last colour carries on past the circle.
        ctx.drawRadialGradient(g, startCenter: c, startRadius: 0, endCenter: c, endRadius: CGFloat(size) / 2, options: [.drawsAfterEndLocation])
        return ctx.makeImage()
    }
    /// A soft green glow, white in the middle (wire heads, the LED, the icon).
    private static let glowImage = radial(size: 128, stops: [(0, CGColor(srgbRed: 1, green: 1, blue: 1, alpha: 1)),
                                                              (0.12, CGColor(srgbRed: 0.6, green: 1, blue: 0.72, alpha: 0.85)),
                                                              (0.4, CGColor(srgbRed: 0.22, green: 1, blue: 0.42, alpha: 0.25)),
                                                              (1, CGColor(srgbRed: 0.22, green: 1, blue: 0.42, alpha: 0))])
    private static var iconImage: CGImage? {
        guard let url = Bundle.main.url(forResource: "LaunchIcon", withExtension: "png"),
              let src = CGImageSourceCreateWithURL(url as CFURL, nil) else { return nil }
        return CGImageSourceCreateImageAtIndex(src, 0, nil)
    }
}

// MARK: - Playing it

/// The intro's view in its panel: the scene, moved on every screen refresh.
final class IntroView: NSView {
    private(set) var scene: IntroScene?
    private var link: CADisplayLink?
    private var started: CFTimeInterval = 0
    /// Development: `CL_INTRO_FREEZE=<seconds>` holds it at that moment.
    private let freezeAt = ProcessInfo.processInfo.environment["CL_INTRO_FREEZE"].flatMap(Double.init)

    override var isFlipped: Bool { false }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    @MainActor func prepare(_ trigger: Intro.Trigger, size: CGSize, cornerRadius: CGFloat, cardIcon: CGPoint) {
        let s = IntroScene(size: size, trigger: trigger, scale: window?.backingScaleFactor ?? 2,
                           look: .current, cardIcon: cardIcon)
        layer = s.root
        wantsLayer = true
        s.root.cornerRadius = cornerRadius
        s.root.cornerCurve = .continuous
        scene = s
    }

    /// From the start, every frame.
    @MainActor func play() {
        started = CACurrentMediaTime()
        let l = displayLink(target: self, selector: #selector(frame(_:)))
        l.add(to: .main, forMode: .common)
        link = l
    }

    @objc private func frame(_ l: CADisplayLink) {
        let t = min(CACurrentMediaTime() - started, freezeAt ?? .infinity)
        SplashDebug.frame(t)
        MainActor.assumeIsolated {
            scene?.apply(t)
            if t >= Intro.total + 0.1 { stop() }
        }
    }

    func stop() {
        link?.invalidate()
        link = nil
    }
}

// MARK: - Rendering it

/// `CedarLogic --render-intro <dir> [first|update]`: the intro at 60 fps,
/// a PNG a frame (frame-0000.png on), at 2x, on black: the intro over a picture of a
/// new circuit's window (dark), with the launch screen's panel drawn over
/// it from its own view, fading into the window as they do on screen. Also
/// wire-compare.png: a wire as the canvas draws it (a real canvas, at its
/// opening zoom) above the intro's first wire at rest, at the same scale.
/// Then quits.
@MainActor
enum RenderIntro {
    static func runIfAsked() {
        let args = CommandLine.arguments
        guard let i = args.firstIndex(of: "--render-intro"), i + 1 < args.count else { return }
        let dir = URL(fileURLWithPath: args[i + 1], isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let trigger = i + 2 < args.count ? Intro.Trigger(rawValue: args[i + 2]) ?? .firstLaunch : .firstLaunch
        let size = CGSize(width: 1280, height: 800)
        let scale: CGFloat = 2
        let prefs = Prefs.shared
        let savedDark = prefs.dark
        prefs.dark = true
        defer { prefs.dark = savedDark }
        let look = IntroWireLook.current
        let (behind, reference) = (windowPicture(size: size, wire: false), windowPicture(size: size, wire: true))
        // The launch screen's panel: 560 by 372, a little above the middle.
        let card = CGRect(x: size.width / 2 - 280, y: size.height / 2 + 36 - 186, width: 560, height: 372)
        let scene = IntroScene(size: size, trigger: trigger, scale: scale, look: look,
                               cardIcon: CGPoint(x: card.midX, y: card.maxY - 44 - 64))
        let splash = Splash.shared
        splash.configureAfterIntro(trigger)
        let space = CGColorSpace(name: CGColorSpace.sRGB)!
        func context() -> CGContext {
            let ctx = CGContext(data: nil, width: Int(size.width * scale), height: Int(size.height * scale), bitsPerComponent: 8,
                                bytesPerRow: 0, space: space, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
            ctx.scaleBy(x: scale, y: scale)
            return ctx
        }
        func save(_ img: CGImage?, _ name: String) {
            guard let img, let dest = CGImageDestinationCreateWithURL(dir.appendingPathComponent(name) as CFURL,
                                                                    "public.png" as CFString, 1, nil) else { exit(1) }
            CGImageDestinationAddImage(dest, img, nil)
            CGImageDestinationFinalize(dest)
        }
        func ease(_ u: Double) -> Double { let c = min(1, max(0, u)); return c < 0.5 ? 2 * c * c : 1 - pow(-2 * c + 2, 2) / 2 }
        let frames = Int((Intro.total * 60).rounded())
        for f in 0...frames {
            let t = Double(f) / 60
            scene.apply(t)
            let ctx = context()
            // On black: the intro fades in from nothing (over the desktop,
            // on screen), and a video can't show see-through.
            ctx.setFillColor(CGColor(gray: 0, alpha: 1))
            ctx.fill(CGRect(origin: .zero, size: size))
            // The window under it all: put in under the intro's panel as the
            // dissolve begins (Splash), the panels fading off it.
            let shown = ease((t - Intro.dissolveAt) / Intro.fade)
            if let behind, t >= Intro.dissolveAt {
                ctx.setAlpha(1)
                ctx.draw(behind, in: CGRect(origin: .zero, size: size))
            }
            // The intro's panel, fading in at first as a whole (as Splash
            // fades its window in).
            ctx.setAlpha((1 - shown) * Intro.fadeInShown(t))
            ctx.beginTransparencyLayer(auxiliaryInfo: nil)
            scene.root.render(in: ctx)
            ctx.endTransparencyLayer()
            // The launch screen's panel, from its own view at its own time.
            if t >= Intro.cardIn {
                let r = ImageRenderer(content: SplashView(model: splash, at: t - Intro.cardIn + Intro.splashAt))
                r.scale = scale
                if let img = r.cgImage {
                    ctx.setAlpha(ease((t - Intro.cardIn) / Intro.cardFade) * (1 - shown))
                    ctx.setShadow(offset: CGSize(width: 0, height: -10), blur: 30, color: CGColor(gray: 0, alpha: 0.5))
                    ctx.draw(img, in: card)
                    ctx.setShadow(offset: .zero, blur: 0, color: nil)
                }
            }
            save(ctx.makeImage(), String(format: "frame-%04d.png", f))
        }
        // A real wire beside the intro's, both at 2x, around a bend.
        if let reference {
            let ctx = context()
            ctx.setFillColor(IntroScene.canvas)
            ctx.fill(CGRect(origin: .zero, size: size))
            let crop = CGSize(width: 320, height: 200)
            ctx.draw(reference, in: CGRect(origin: .zero, size: size))
            let real = ctx.makeImage()?.cropping(to: CGRect(x: (size.width / 2 - crop.width / 2) * scale, y: (size.height / 2 - crop.height / 2) * scale,
                                                            width: crop.width * scale, height: crop.height * scale))
            scene.apply(1.8)
            let ctx2 = context()
            scene.root.render(in: ctx2)
            let b = scene.firstWireBend
            let ours = ctx2.makeImage()?.cropping(to: CGRect(x: (b.x - crop.width / 2) * scale, y: (size.height - b.y - crop.height / 2) * scale,
                                                             width: crop.width * scale, height: crop.height * scale))
            let both = CGContext(data: nil, width: Int(crop.width * scale), height: Int(crop.height * scale * 2 + 8), bitsPerComponent: 8,
                                 bytesPerRow: 0, space: space, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
            both.setFillColor(CGColor(gray: 1, alpha: 1))
            both.fill(CGRect(x: 0, y: 0, width: crop.width * scale, height: crop.height * scale * 2 + 8))
            if let real { both.draw(real, in: CGRect(x: 0, y: crop.height * scale + 8, width: crop.width * scale, height: crop.height * scale)) }
            if let ours { both.draw(ours, in: CGRect(x: 0, y: 0, width: crop.width * scale, height: crop.height * scale)) }
            save(both.makeImage(), "wire-compare.png")
        }
        print("render-intro: \(frames + 1) frames (\(trigger.rawValue)) in \(dir.path)")
        exit(0)
    }

    /// A new circuit's window, dark, drawn off screen (as --render-window);
    /// with `wire`, just its canvas at the opening zoom, a switch wired to an
    /// AND gate with two bends in the middle.
    private static func windowPicture(size: CGSize, wire: Bool) -> CGImage? {
        let doc = CoreDocument()
        var strings: [UnsafeMutablePointer<CChar>] = []
        defer { strings.forEach { free($0) } }
        func c(_ s: String) -> UnsafePointer<CChar> { let p = strdup(s)!; strings.append(p); return UnsafePointer(p) }
        if wire {
            let gates = [CLBuildGate(gate: c("AA_TOGGLE"), x: -8, y: 2, label: nil, angle: 0),
                         CLBuildGate(gate: c("AA_AND2"), x: 8, y: -2, label: nil, angle: 0)]
            let wires = [CLBuildWire(from: 0, fromPin: c("OUT_0"), to: 1, toPin: c("IN_0"))]
            _ = cl_edit_build(doc.handle, 0, gates, Int32(gates.count), wires, Int32(wires.count), "Build")
            cl_edit_select_none(doc.handle, 0)
        }
        let canvas = CanvasController()
        canvas.attach(doc)
        let win = NSWindow(contentRect: NSRect(origin: NSPoint(x: -4000, y: -4000), size: size),
                           styleMask: [.titled, .fullSizeContentView, .resizable], backing: .buffered, defer: false)
        win.titlebarAppearsTransparent = true
        win.titleVisibility = .hidden
        let host = NSHostingView(rootView: CLLayout(document: doc, canvas: canvas, page: .constant(0))
            .frame(width: size.width, height: size.height))
        host.appearance = NSAppearance(named: .darkAqua)
        win.contentView = host
        host.frame = NSRect(origin: .zero, size: size)
        host.layoutSubtreeIfNeeded()
        RunLoop.main.run(until: Date().addingTimeInterval(1.2))
        defer { win.close() }
        if wire {
            // Only the canvas, at the zoom a new circuit opens at, the wire's
            // middle in the middle of the picture.
            guard let view = canvas.view else { return nil }
            view.needsFit = false
            view.unitsPerPoint = 0.05
            let vb = view.bounds
            view.origin = CGPoint(x: 0 - vb.width / 2 * 0.05, y: 0 + vb.height / 2 * 0.05)
            view.needsDisplay = true
            RunLoop.main.run(until: Date().addingTimeInterval(0.4))
            guard let rep = view.bitmapImageRepForCachingDisplay(in: vb) else { return nil }
            view.cacheDisplay(in: vb, to: rep)
            guard let img = rep.cgImage else { return nil }
            // Centred in a picture the size of the others.
            let ctx = CGContext(data: nil, width: Int(size.width * 2), height: Int(size.height * 2), bitsPerComponent: 8, bytesPerRow: 0,
                                space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
            ctx.scaleBy(x: 2, y: 2)
            ctx.draw(img, in: CGRect(x: size.width / 2 - vb.width / 2, y: size.height / 2 - vb.height / 2, width: vb.width, height: vb.height))
            return ctx.makeImage()
        }
        canvas.view?.needsDisplay = true
        RunLoop.main.run(until: Date().addingTimeInterval(0.3))
        guard let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(size.width * 2), pixelsHigh: Int(size.height * 2),
                                         bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                                         colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0) else { return nil }
        rep.size = size
        host.cacheDisplay(in: NSRect(origin: .zero, size: size), to: rep)
        return rep.cgImage
    }
}
