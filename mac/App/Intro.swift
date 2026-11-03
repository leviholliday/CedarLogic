// The intro: the very first launch, and the first launch after an update
// with a What's New tour, open with the circuit drawing itself instead of
// the launch screen's panel. Over the circuit window, on a dark canvas, two
// wires trace in from their pins with a bright head running along them;
// the letters of the icon snap into place as the wires reach them (the C,
// the D -- an AND gate -- and the L, which the output wire runs down); a
// clock pulse runs through, the D lights, and the LED at the end blooms
// into the icon itself, which settles over its name. Then it all breaks up
// into sparks and the canvas fades into the real window (and the welcome,
// or What's New, on top). Three seconds; any key or click skips it; with
// Reduce Motion it's the last still and a 0.4 s crossfade.
//
// Splash.swift plays it (the windows wait, hidden, as for the launch
// screen); `CedarLogic --render-intro <dir> [first|update]` draws it, frame
// by frame at 60 fps, to PNGs (mac/Tools/render-intro.sh makes a video).
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

    /// The timeline, in seconds.
    static let total = 3.0
    /// The sparks and the fade into the window start here.
    static let dissolveAt = 2.52
    static var fade: Double { total - dissolveAt }
    /// The LED lights. The first launch's sound is started part-way in so
    /// its bell chord (4.0 s into FirstLaunch.m4a) lands on this.
    static let ledAt = 1.95
    static let soundHit = 4.0
    /// Reduce Motion: the last still, briefly, then a crossfade.
    static let calmHold = 0.5
    static let calmFade = 0.4
    static let stillAt = 2.5

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

/// The intro's layers, laid out for one size, and where everything is at a
/// moment (`apply`). Plain Core Animation, set frame by frame and never
/// animated implicitly, so a live frame and a rendered one are the same.
@MainActor
final class IntroScene {
    let root = CALayer()
    let size: CGSize
    let trigger: Intro.Trigger

    // The icon's art is 512 units square; the circuit is laid out in those
    // units and drawn at `k` points a unit, centred.
    private let k: CGFloat
    private let xf: CGAffineTransform

    private let ground = CALayer()
    private let grid = CAShapeLayer()
    private let vignette = CALayer()
    private var wires: [Wire] = []
    private var pins: [CAShapeLayer] = []
    private var letters: [Letter] = []
    private let gateGlow = CAShapeLayer()
    private let ledRing = CAShapeLayer()
    private let ledCore = CAShapeLayer()
    private let ledGlow = CALayer()
    private let bloom = CALayer()
    private let iconGlow = CALayer()
    private let icon = CALayer()
    private let title = CATextLayer()
    private let subtitle = CATextLayer()
    private var sparks: [Spark] = []
    private let circuit = CALayer()   // the wires, pins, letters and LED, faded as one

    private struct Wire {
        let glow = CAShapeLayer(), core = CAShapeLayer(), high = CAShapeLayer(), head = CAShapeLayer()
        let dot = CALayer()
        let points: [CGPoint]
        let lengths: [CGFloat]   // running length at each point
        let draw: (Double, Double)   // traced in between these times
        let pulse: (Double, Double)  // the clock pulse runs along it between these
    }
    private struct Letter {
        let outline = CAShapeLayer(), glow = CAShapeLayer()
        let popAt: Double
        let trace: (Double, Double)
    }
    private struct Spark {
        let layer: CALayer
        let from: CGPoint, velocity: CGVector
        let delay: Double, life: Double
    }

    static let neon = CGColor(srgbRed: 0.22, green: 1.0, blue: 0.42, alpha: 1)
    static let neonDeep = CGColor(srgbRed: 0.05, green: 0.78, blue: 0.26, alpha: 1)
    static let silver = CGColor(srgbRed: 0.86, green: 0.9, blue: 0.88, alpha: 1)
    /// The Graphite theme's canvas and grid (Theme.swift): the app's dark look.
    static let canvas = CGColor(srgbRed: 0.075, green: 0.082, blue: 0.098, alpha: 1)
    static let gridMinor = CGColor(srgbRed: 0.16, green: 0.17, blue: 0.20, alpha: 1)
    static let gridMajor = CGColor(srgbRed: 0.23, green: 0.25, blue: 0.29, alpha: 1)

    init(size: CGSize, trigger: Intro.Trigger, scale: CGFloat) {
        self.size = size
        self.trigger = trigger
        // The circuit spans about 490 units across and the icon 512 up.
        k = min(size.width * 0.5 / 490, size.height * 0.84 / 512)
        // Icon units (y down, the circuit's middle at 256, 247) to points (y up).
        xf = CGAffineTransform(a: k, b: 0, c: 0, d: -k, tx: size.width / 2 - 256 * k, ty: size.height / 2 + 247 * k)
        root.frame = CGRect(origin: .zero, size: size)
        root.masksToBounds = true
        build(scale: scale)
        apply(0)
    }

    private func p(_ x: CGFloat, _ y: CGFloat) -> CGPoint { CGPoint(x: x, y: y).applying(xf) }

    // MARK: Building

    private func build(scale: CGFloat) {
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

        // The wires (in icon units): two inputs that bend through the C into
        // the D (an AND gate), and its output, down the L to the LED.
        func s(_ a: CGPoint, _ b: CGPoint) -> [CGPoint] {
            (1...14).map { i in
                let u = CGFloat(i) / 14, e = u * u * (3 - 2 * u)
                return CGPoint(x: a.x + (b.x - a.x) * u, y: a.y + (b.y - a.y) * e)
            }
        }
        let a: [CGPoint] = [CGPoint(x: 20, y: 217.5), CGPoint(x: 100, y: 217.5)] + s(CGPoint(x: 100, y: 217.5), CGPoint(x: 128, y: 203))
            + [CGPoint(x: 148, y: 203)] + s(CGPoint(x: 148, y: 203), CGPoint(x: 176, y: 226)) + [CGPoint(x: 196, y: 226)]
        let b: [CGPoint] = [CGPoint(x: 20, y: 253.5), CGPoint(x: 100, y: 253.5)] + s(CGPoint(x: 100, y: 253.5), CGPoint(x: 128, y: 290))
            + [CGPoint(x: 148, y: 290)] + s(CGPoint(x: 148, y: 290), CGPoint(x: 176, y: 268)) + [CGPoint(x: 196, y: 268)]
        let out: [CGPoint] = [CGPoint(x: 351, y: 247), CGPoint(x: 385, y: 247), CGPoint(x: 367, y: 316), CGPoint(x: 486, y: 316)]
        wires = [makeWire(a, draw: (0.14, 0.86), pulse: (1.50, 1.74)),
                 makeWire(b, draw: (0.18, 0.88), pulse: (1.50, 1.74)),
                 makeWire(out, draw: (1.0, 1.42), pulse: (1.74, ledAtPulseEnd))]

        // The input pins: small rings, as in the icon.
        for y in [217.5, 253.5] as [CGFloat] {
            let ring = CAShapeLayer()
            let c = p(14, y), r = 6 * k
            ring.bounds = CGRect(x: 0, y: 0, width: r * 2 + 8, height: r * 2 + 8)
            ring.position = c
            ring.path = CGPath(ellipseIn: ring.bounds.insetBy(dx: 4, dy: 4), transform: nil)
            ring.fillColor = Self.canvas
            ring.strokeColor = Self.neon
            ring.lineWidth = max(1.5, 1.6 * k)
            ring.shadowColor = Self.neon; ring.shadowRadius = 5; ring.shadowOpacity = 0.8; ring.shadowOffset = .zero
            circuit.addSublayer(ring)
            pins.append(ring)
        }

        // The letters, as outlines of the icon's: the C ...
        let c = CGMutablePath()
        c.addArc(center: CGPoint(x: 156, y: 247), radius: 88, startAngle: .pi * 55 / 180, endAngle: .pi * 305 / 180, clockwise: false)
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
        let cOutline = c.copy(strokingWithWidth: 24, lineCap: .butt, lineJoin: .miter, miterLimit: 10)
        letters = [makeLetter(cOutline, popAt: 0.34, trace: (0.26, 0.74)),
                   makeLetter(dOutline, popAt: 0.86, trace: (0.80, 1.12)),
                   makeLetter(l, popAt: 1.10, trace: (1.04, 1.38))]

        // The AND gate's inside, lit when its output goes high.
        let inside = CGMutablePath()
        inside.move(to: CGPoint(x: 220, y: 148)); inside.addLine(to: CGPoint(x: 228, y: 148))
        inside.addArc(center: CGPoint(x: 228, y: 247), radius: 99, startAngle: -.pi / 2, endAngle: .pi / 2, clockwise: false)
        inside.addLine(to: CGPoint(x: 220, y: 346)); inside.closeSubpath()
        gateGlow.frame = root.bounds
        gateGlow.path = inside.copy(using: [xf])
        gateGlow.fillColor = Self.neon
        circuit.insertSublayer(gateGlow, at: 0)

        // The LED at the end of the output.
        let ledC = p(494, 316), lr = 8 * k
        for (layer, inset) in [(ledRing, CGFloat(0)), (ledCore, 2.5 * k)] {
            layer.bounds = CGRect(x: 0, y: 0, width: lr * 2 + 12, height: lr * 2 + 12)
            layer.position = ledC
            layer.path = CGPath(ellipseIn: layer.bounds.insetBy(dx: 6 + inset, dy: 6 + inset), transform: nil)
        }
        ledRing.fillColor = Self.canvas
        ledRing.strokeColor = Self.neon
        ledRing.lineWidth = max(1.5, 1.8 * k)
        ledCore.fillColor = CGColor(srgbRed: 0.85, green: 1, blue: 0.9, alpha: 1)
        ledGlow.bounds = CGRect(x: 0, y: 0, width: 180 * k, height: 180 * k)
        ledGlow.position = ledC
        ledGlow.contents = Self.glowImage
        circuit.addSublayer(ledGlow)
        circuit.addSublayer(ledRing)
        circuit.addSublayer(ledCore)

        // The bloom, the icon (where its own art lines up with the circuit,
        // to begin with) and its name.
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

        let silver = NSColor(srgbRed: 0.93, green: 0.95, blue: 0.94, alpha: 1)
        title.string = NSAttributedString(string: trigger == .update ? "CedarLogic \(WhatsNew.version)" : "CedarLogic", attributes: [
            .font: NSFont.systemFont(ofSize: 34, weight: .semibold), .foregroundColor: silver, .kern: 0.4])
        subtitle.string = NSAttributedString(string: trigger == .update ? "WHAT'S NEW" : "LOGIC SIMULATOR  ·  FOR MAC", attributes: [
            .font: NSFont.systemFont(ofSize: 11, weight: .bold), .foregroundColor: NSColor(cgColor: Self.neon)!, .kern: 2.2])
        for (layer, h) in [(title, CGFloat(44)), (subtitle, CGFloat(18))] {
            layer.alignmentMode = .center
            layer.bounds = CGRect(x: 0, y: 0, width: 520, height: h)
            layer.contentsScale = scale
            root.addSublayer(layer)
        }

        makeSparks()
        setScale(root, scale)
    }

    private var ledAtPulseEnd: Double { Intro.ledAt }

    private func setScale(_ l: CALayer, _ s: CGFloat) {
        l.contentsScale = s
        for c in l.sublayers ?? [] { setScale(c, s) }
    }

    private func makeWire(_ pts: [CGPoint], draw: (Double, Double), pulse: (Double, Double)) -> Wire {
        let points = pts.map { p($0.x, $0.y) }
        var lengths: [CGFloat] = [0]
        for i in 1..<points.count { lengths.append(lengths[i - 1] + hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y)) }
        let w = Wire(points: points, lengths: lengths, draw: draw, pulse: pulse)
        let path = CGMutablePath()
        path.addLines(between: points)
        for (layer, width, color, alpha) in [(w.glow, 7 * k, Self.neon, 0.16), (w.core, max(1.6, 1.7 * k), Self.neonDeep, 0.75),
                                             (w.high, max(2.2, 2.4 * k), CGColor(srgbRed: 0.6, green: 1, blue: 0.7, alpha: 1), 1),
                                             (w.head, max(2.6, 2.8 * k), CGColor(gray: 1, alpha: 1), 1)] as [(CAShapeLayer, CGFloat, CGColor, CGFloat)] {
            layer.frame = root.bounds
            layer.path = path
            layer.fillColor = nil
            layer.strokeColor = color.copy(alpha: alpha)
            layer.lineWidth = width
            layer.lineCap = .round
            layer.lineJoin = .round
            circuit.addSublayer(layer)
        }
        w.dot.bounds = CGRect(x: 0, y: 0, width: 46 * k, height: 46 * k)
        w.dot.contents = Self.glowImage
        circuit.addSublayer(w.dot)
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

    /// The sparks the icon breaks up into: a fixed scatter, so every run
    /// (and every rendered frame) is the same.
    private func makeSparks() {
        var rng = SplitMix(seed: 0xCEDA)
        for i in 0..<260 {
            let layer = CALayer()
            let r = CGFloat(0.8 + 1.6 * rng.next())
            layer.bounds = CGRect(x: 0, y: 0, width: r * 2, height: r * 2)
            layer.cornerRadius = r
            layer.backgroundColor = i % 4 == 0 ? CGColor(srgbRed: 0.85, green: 1, blue: 0.9, alpha: 1) : Self.neon
            // From the icon (filled in when it has settled: `apply`), outwards.
            let ang = rng.next() * 2 * .pi
            let speed = 70 + 360 * pow(rng.next(), 1.6)
            sparks.append(Spark(layer: layer, from: CGPoint(x: rng.next() * 2 - 1, y: rng.next() * 2 - 1),
                                velocity: CGVector(dx: cos(ang) * speed, dy: sin(ang) * speed + 50),
                                delay: 0.1 * rng.next(), life: 0.3 + 0.2 * rng.next()))
            layer.opacity = 0
            root.addSublayer(layer)
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
    /// A snap into place: from small up past full size and back.
    private static func pop(_ t: Double, _ at: Double, from: Double = 0.6) -> Double {
        let u = clamp((t - at) / 0.26)
        if u <= 0 { return from }
        // An overshoot of about 8%, settled by the end.
        let s = 1 - pow(1 - u, 3)
        return from + (1 - from) * s + 0.22 * sin(u * .pi) * (1 - u)
    }

    /// Everything where it is `t` seconds in.
    func apply(_ t: Double) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        defer { CATransaction.commit() }
        let S = Self.self
        let out = S.smooth(t, Intro.dissolveAt, Intro.total - 0.02)
        ground.opacity = Float(S.smooth(t, 0, 0.22) * (1 - out))
        grid.opacity = Float(S.smooth(t, 0.04, 0.5) * 0.85 * (1 - out))
        vignette.opacity = ground.opacity

        // The circuit goes as the icon arrives over it.
        circuit.opacity = Float(1 - S.smooth(t, 2.06, 2.24))

        for (i, pin) in pins.enumerated() {
            let at = 0.08 + 0.05 * Double(i)
            pin.opacity = Float(S.smooth(t, at, at + 0.08))
            pin.setAffineTransform(CGAffineTransform(scaleX: S.pop(t, at, from: 0.2), y: S.pop(t, at, from: 0.2)))
        }

        for w in wires {
            let d = S.smooth(t, w.draw.0, w.draw.1)
            for l in [w.glow, w.core] { l.strokeEnd = d; l.isHidden = d <= 0 }
            // The bright head, a short run behind the tip, fading as it lands.
            w.head.strokeStart = max(0, d - 0.07)
            w.head.strokeEnd = d
            w.head.opacity = d <= 0 ? 0 : d < 1 ? 1 : Float(max(0, 1 - (t - w.draw.1) / 0.12))
            // The clock pulse: the wire goes high behind it, and glows more.
            let q = S.smooth(t, w.pulse.0, w.pulse.1)
            w.high.strokeEnd = q
            w.high.isHidden = q <= 0
            w.glow.strokeColor = Self.neon.copy(alpha: 0.16 + 0.22 * q)
            let tip: Double?, travelling = q > 0 && q < 1
            if travelling {
                w.head.strokeStart = max(0, q - 0.12); w.head.strokeEnd = q; w.head.opacity = 1
                tip = q
            } else {
                tip = d > 0 && d < 1 ? d : nil
            }
            if let tip {
                w.dot.position = point(on: w, at: tip)
                w.dot.opacity = Float(travelling ? 1 : 0.85)
            } else {
                w.dot.opacity = 0
            }
        }

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
            // The clock pulse catches the outlines as it passes.
            l.glow.strokeColor = Self.neon.copy(alpha: 0.22 + 0.4 * S.smooth(t, 1.62, 1.8) * (1 - S.smooth(t, 1.9, 2.1)))
        }
        // The gate fires: its inside glows green.
        let fired = S.ease(t, 1.72, 1.86)
        gateGlow.opacity = Float(0.08 + 0.32 * fired)
        gateGlow.isHidden = t < 0.86

        // The LED: in when the output reaches it, lit by the pulse.
        let ledIn = S.pop(t, 1.40, from: 0.3)
        ledRing.setAffineTransform(CGAffineTransform(scaleX: ledIn, y: ledIn))
        ledRing.opacity = Float(S.smooth(t, 1.38, 1.46))
        let lit = S.ease(t, Intro.ledAt, Intro.ledAt + 0.08)
        ledCore.opacity = Float(lit)
        ledGlow.opacity = Float(lit)
        let g = 0.6 + 0.8 * lit
        ledGlow.setAffineTransform(CGAffineTransform(scaleX: g, y: g))

        // The bloom: a flash out of the LED that fills the canvas.
        let bl = S.ease(t, Intro.ledAt + 0.02, Intro.ledAt + 0.34)
        let bs = 0.04 + 0.96 * bl
        bloom.setAffineTransform(CGAffineTransform(scaleX: bs, y: bs))
        bloom.opacity = Float(0.9 * S.smooth(t, Intro.ledAt, Intro.ledAt + 0.06) * (1 - S.smooth(t, Intro.ledAt + 0.1, Intro.ledAt + 0.42)))

        // The icon: over the circuit, its own art where the circuit was, then
        // settling to its size over its name.
        let big = CGRect(origin: p(0, 512), size: CGSize(width: 512 * k, height: 512 * k))
        let small: CGFloat = min(176, size.height * 0.24)
        let smallFrame = CGRect(x: size.width / 2 - small / 2, y: size.height / 2 + 34 - small / 2, width: small, height: small)
        let settle = S.smooth(t, 2.14, 2.5)
        let frame = CGRect(x: big.minX + (smallFrame.minX - big.minX) * settle, y: big.minY + (smallFrame.minY - big.minY) * settle,
                           width: big.width + (smallFrame.width - big.width) * settle,
                           height: big.height + (smallFrame.height - big.height) * settle)
        let gone = S.smooth(t, Intro.dissolveAt, Intro.dissolveAt + 0.26)
        let grow = 1 + 0.08 * gone
        icon.bounds = CGRect(origin: .zero, size: frame.size)
        icon.position = CGPoint(x: frame.midX, y: frame.midY)
        icon.setAffineTransform(CGAffineTransform(scaleX: grow, y: grow))
        icon.opacity = Float(S.smooth(t, Intro.ledAt + 0.04, Intro.ledAt + 0.22) * (1 - gone))
        iconGlow.bounds = CGRect(x: 0, y: 0, width: frame.width * 1.9, height: frame.height * 1.9)
        iconGlow.position = icon.position
        iconGlow.opacity = icon.opacity * Float(0.55 + 0.25 * (1 - settle))

        let words = S.ease(t, 2.32, 2.56) * (1 - gone)
        title.position = CGPoint(x: size.width / 2, y: smallFrame.minY - 40 - 10 * (1 - words))
        subtitle.position = CGPoint(x: size.width / 2, y: smallFrame.minY - 70 - 10 * (1 - words))
        title.opacity = Float(words)
        subtitle.opacity = Float(words * 0.9)
        let tg = CGAffineTransform(scaleX: 1 + 0.04 * gone, y: 1 + 0.04 * gone)
        title.setAffineTransform(tg)
        subtitle.setAffineTransform(tg)

        // The sparks: out of the settled icon and its name, slowing, gone.
        let centre = CGPoint(x: smallFrame.midX, y: smallFrame.midY - 20)
        for s in sparks {
            let age = t - Intro.dissolveAt - s.delay
            guard age > 0, age < s.life else { s.layer.opacity = 0; continue }
            let drag = 3.2, travel = (1 - exp(-drag * age)) / drag
            let from = CGPoint(x: centre.x + s.from.x * small * 0.55, y: centre.y + s.from.y * (small * 0.5 + 30))
            s.layer.position = CGPoint(x: from.x + s.velocity.dx * travel, y: from.y + s.velocity.dy * travel)
            s.layer.opacity = Float(pow(1 - age / s.life, 1.4) * min(1, age / 0.04))
        }
    }

    private func point(on w: Wire, at f: Double) -> CGPoint {
        let target = CGFloat(f) * (w.lengths.last ?? 0)
        guard let i = w.lengths.firstIndex(where: { $0 >= target }), i > 0 else { return w.points.first ?? .zero }
        let a = w.points[i - 1], b = w.points[i]
        let span = w.lengths[i] - w.lengths[i - 1]
        let u = span > 0 ? (target - w.lengths[i - 1]) / span : 0
        return CGPoint(x: a.x + (b.x - a.x) * u, y: a.y + (b.y - a.y) * u)
    }

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

/// A small, fixed random sequence (0..<1).
private struct SplitMix {
    var state: UInt64
    init(seed: UInt64) { state = seed }
    mutating func next() -> CGFloat {
        state &+= 0x9E37_79B9_7F4A_7C15
        var z = state
        z = (z ^ (z >> 30)) &* 0xBF58_476D_1CE4_E5B9
        z = (z ^ (z >> 27)) &* 0x94D0_49BB_1331_11EB
        z ^= z >> 31
        return CGFloat(z >> 11) / CGFloat(1 << 53)
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

    @MainActor func prepare(_ trigger: Intro.Trigger, size: CGSize, cornerRadius: CGFloat) {
        let s = IntroScene(size: size, trigger: trigger, scale: window?.backingScaleFactor ?? 2)
        layer = s.root
        wantsLayer = true
        s.root.cornerRadius = cornerRadius
        s.root.cornerCurve = .continuous
        scene = s
    }

    /// From the start, every frame; or (Reduce Motion) just the still.
    @MainActor func play(calm: Bool) {
        guard let scene else { return }
        if calm { scene.apply(Intro.stillAt); return }
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
/// a PNG a frame (frame-0000.png on), at 2x, over a picture of a new
/// circuit's window (dark) that it fades into, as it does over the real
/// one. Then quits.
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
        let behind = windowPicture(size: size)
        let scene = IntroScene(size: size, trigger: trigger, scale: scale)
        let space = CGColorSpace(name: CGColorSpace.sRGB)!
        let frames = Int((Intro.total * 60).rounded())
        for f in 0...frames {
            let t = Double(f) / 60
            scene.apply(t)
            guard let ctx = CGContext(data: nil, width: Int(size.width * scale), height: Int(size.height * scale), bitsPerComponent: 8,
                                      bytesPerRow: 0, space: space, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { exit(1) }
            ctx.scaleBy(x: scale, y: scale)
            // The window under it, coming in as the intro goes (Splash fades
            // it in over the dissolve, ease in and out).
            let u = min(1, max(0, (t - Intro.dissolveAt) / Intro.fade))
            let shown = u < 0.5 ? 2 * u * u : 1 - pow(-2 * u + 2, 2) / 2
            if let behind {
                ctx.setAlpha(shown)
                ctx.draw(behind, in: CGRect(origin: .zero, size: size))
                ctx.setAlpha(1)
            }
            scene.root.render(in: ctx)
            guard let img = ctx.makeImage(),
                  let dest = CGImageDestinationCreateWithURL(dir.appendingPathComponent(String(format: "frame-%04d.png", f)) as CFURL,
                                                             "public.png" as CFString, 1, nil) else { exit(1) }
            CGImageDestinationAddImage(dest, img, nil)
            CGImageDestinationFinalize(dest)
        }
        print("render-intro: \(frames + 1) frames (\(trigger.rawValue)) in \(dir.path)")
        exit(0)
    }

    /// A new circuit's window, dark, drawn off screen (as --render-window).
    private static func windowPicture(size: CGSize) -> CGImage? {
        let prefs = Prefs.shared
        let saved = prefs.dark
        prefs.dark = true
        defer { prefs.dark = saved }
        let doc = CoreDocument()
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
        canvas.view?.needsDisplay = true
        RunLoop.main.run(until: Date().addingTimeInterval(0.3))
        defer { win.close() }
        guard let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(size.width * 2), pixelsHigh: Int(size.height * 2),
                                         bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                                         colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0) else { return nil }
        rep.size = size
        host.cacheDisplay(in: NSRect(origin: .zero, size: size), to: rep)
        return rep.cgImage
    }
}
