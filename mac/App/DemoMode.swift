// `CedarLogic --demo`: a short montage to screen-record. The app builds a
// half adder by itself (parts pulled out of the palette, wires drawn pin to
// pin, the switches flipped), then opens the 4-bit counter running in
// Simulation View and glides the camera over it. Your pointer moves along
// with every step, so the recording looks like someone working.
//
// The gestures go to the canvas's own handlers (as --session-check's do);
// the pointer is only warped to match, so nothing outside the app can ever
// be clicked, and no Accessibility permission is needed. Run it through
// mac/Tools/demo.sh, which uses a copy of the app with a home folder of its
// own (your settings and Your Circuits are never touched).
//
//   CL_DEMO_SPEED=1.5   faster (or 0.7 slower)
//   CL_DEMO_CHECK=1     a check run: no pointer, the window off screen,
//                       PASS/FAIL for what was built, then quit
//   CL_DEMO_SHOTS=dir   (with CHECK) a PNG of the window at each stage

import AppKit
import SwiftUI

@MainActor
enum DemoMode {
    static let on = CommandLine.arguments.contains("--demo")
    static let checking = ProcessInfo.processInfo.environment["CL_DEMO_CHECK"] != nil
    static let speed = max(0.2, Double(ProcessInfo.processInfo.environment["CL_DEMO_SPEED"] ?? "") ?? 1)
    /// Palette tiles by gate name, in SwiftUI's global space (the window's
    /// content, top left), kept by CLGateTile while the demo runs.
    static var tiles: [String: CGRect] = [:]
    /// The Simulation bar's speed slider, the same way.
    static var speedSlider: CGRect?

    static func runIfAsked() {
        guard on else { return }
        setvbuf(stdout, nil, _IOLBF, 0)
        Prefs.shared.hasSeenWelcome = true
        Prefs.shared.dark = true   // the demo's own settings, never yours
        WhatsNew.markSeen()
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) { waitForWindow(tries: 40) }
    }

    private static func waitForWindow(tries: Int) {
        if let (win, view) = circuitWindow(except: nil) {
            run(win, view)
        } else if tries > 0 {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) { waitForWindow(tries: tries - 1) }
        } else {
            print("demo: no circuit window opened")
            exit(1)
        }
    }

    static func circuitWindow(except other: NSWindow?) -> (NSWindow, CircuitCanvasNSView)? {
        func find(_ v: NSView) -> CircuitCanvasNSView? {
            if let c = v as? CircuitCanvasNSView, c.controller?.drivesClock == true { return c }
            for s in v.subviews { if let c = find(s) { return c } }
            return nil
        }
        for w in NSApp.windows where w !== other && w.isVisible {
            if let content = w.contentView, let c = find(content) { return (w, c) }
        }
        return nil
    }

    // MARK: - Time and the pointer

    /// Wait, with the app still answering events: a nested run loop alone
    /// leaves AppKit's event queue unserved, and macOS shows the busy pointer.
    static func turn(_ seconds: Double, real: Bool = false) {
        pump(until: Date().addingTimeInterval(checking && !real ? min(seconds, 0.05) : seconds / (real ? 1 : speed)))
    }
    static func pump(until end: Date) {
        repeat {
            if let e = NSApp.nextEvent(matching: .any, until: Date().addingTimeInterval(0.004), inMode: .default, dequeue: true) {
                NSApp.sendEvent(e)
            }
        } while Date() < end
    }
    /// A PNG of the window now (a check run with CL_DEMO_SHOTS=<dir>).
    static func shot(_ win: NSWindow, _ name: String) {
        guard checking, let dir = ProcessInfo.processInfo.environment["CL_DEMO_SHOTS"], let png = FeedbackCapture.png(of: win) else { return }
        try? FileManager.default.createDirectory(atPath: dir, withIntermediateDirectories: true)
        try? png.write(to: URL(fileURLWithPath: dir + "/" + name + ".png"))
    }
    /// A repeatable bit of randomness, so a take looks the same twice.
    static var seed: UInt64 = 7
    static func jitter(_ spread: Double) -> Double {
        seed = seed &* 6364136223846793005 &+ 1442695040888963407
        return (Double(seed >> 33) / Double(1 << 31) - 0.5) * 2 * spread
    }
    static var pointer = CGPoint.zero   // global display coordinates (top left of the main display)
    static func screen(_ win: NSWindow, _ windowPoint: NSPoint) -> CGPoint {
        let s = win.convertPoint(toScreen: windowPoint)
        return CGPoint(x: s.x, y: (NSScreen.screens.first?.frame.height ?? 0) - s.y)
    }
    static func windowPoint(_ win: NSWindow, _ screen: CGPoint) -> NSPoint {
        win.convertPoint(fromScreen: NSPoint(x: screen.x, y: (NSScreen.screens.first?.frame.height ?? 0) - screen.y))
    }
    static func warp(_ p: CGPoint) {
        pointer = p
        if !checking { CGWarpMouseCursorPosition(p); CGAssociateMouseAndMouseCursorPosition(1) }
    }
    /// Move the pointer to `to` the way a hand does: quick in the middle,
    /// slowing to land, a slight bow, a small overshoot on a long move that
    /// settles back, and a faint tremor. `each` gets every point on the way.
    /// A person between steps: a short pause, sometimes longer, and now and
    /// then the pointer drifts a little while they look for the next thing.
    static func think() {
        NSCursor.arrow.set()
        guard !checking else { return }
        let r = jitter(1)
        if r > 0.55 {
            let p = pointer
            leg(from: p, to: CGPoint(x: p.x + jitter(90), y: p.y + jitter(60)), 0.5 + abs(jitter(0.3)), bow: CGFloat(jitter(20)), each: { _ in })
        }
        turn(0.35 + abs(jitter(0.6)) + (r < -0.6 ? 0.8 : 0))
    }

    /// Move the pointer the way a thumb on a trackball with a fast pointer
    /// (Glideball) does: one quick, nearly straight flick that covers most
    /// of the way, a short stop as the thumb catches the ball again, then a
    /// slow, careful roll onto the target. Short moves are just the careful
    /// roll. `each` gets every point on the way.
    /// A keystroke badge near the bottom of the window ("⌘A  Select All"),
    /// faded in, held, faded out: what a key press looks like on screen.
    static func badge(_ win: NSWindow, keys: String, label: String, hold: Double = 1.0) {
        let text = NSMutableAttributedString(string: keys, attributes: [
            .font: NSFont.systemFont(ofSize: 30, weight: .semibold), .foregroundColor: NSColor.white])
        text.append(NSAttributedString(string: "   " + label, attributes: [
            .font: NSFont.systemFont(ofSize: 22, weight: .medium), .foregroundColor: NSColor.white.withAlphaComponent(0.8)]))
        let field = NSTextField(labelWithAttributedString: text)
        field.sizeToFit()
        let pad = CGSize(width: 28, height: 16)
        let size = CGSize(width: field.frame.width + pad.width * 2, height: field.frame.height + pad.height * 2)
        // Its own small window over the circuit window: a view added to the
        // window's content ends up under the SwiftUI layers and never shows.
        let frame = win.frame
        let panel = NSPanel(contentRect: NSRect(x: frame.midX - size.width / 2, y: frame.minY + 120, width: size.width, height: size.height),
                            styleMask: [.borderless, .nonactivatingPanel], backing: .buffered, defer: false)
        panel.isOpaque = false
        panel.backgroundColor = .clear
        panel.hasShadow = true
        panel.ignoresMouseEvents = true
        panel.level = .floating
        let box = NSView(frame: NSRect(origin: .zero, size: size))
        box.wantsLayer = true
        box.layer?.backgroundColor = NSColor(white: 0.08, alpha: 0.9).cgColor
        box.layer?.cornerRadius = 16
        box.layer?.borderWidth = 1
        box.layer?.borderColor = NSColor.white.withAlphaComponent(0.15).cgColor
        field.frame.origin = CGPoint(x: pad.width, y: pad.height)
        box.addSubview(field)
        panel.contentView = box
        panel.alphaValue = 0
        win.addChildWindow(panel, ordered: .above)
        panel.orderFront(nil)
        NSAnimationContext.runAnimationGroup { $0.duration = 0.15; panel.animator().alphaValue = 1 }
        turn(0.15 + hold)
        NSAnimationContext.runAnimationGroup({ $0.duration = 0.25; panel.animator().alphaValue = 0 }, completionHandler: {
            win.removeChildWindow(panel); panel.orderOut(nil)
        })
    }

    // MARK: - Real clicks, for screen recorders that zoom on clicks
    //
    // The gestures go straight to the canvas, so the system never sees a
    // click. Each press and release is also posted as a real one, landing on
    // a clear shield over the window that swallows it (so nothing acts twice
    // and nothing else on screen can be clicked).
    static var shield: NSPanel?
    static var realClicks = false
    static func raiseShield(over win: NSWindow) {
        guard !checking else { return }
        realClicks = CGPreflightPostEventAccess() || CGRequestPostEventAccess()
        guard realClicks else { print("demo: no permission to post clicks; your recorder won't see them"); return }
        let p = NSPanel(contentRect: win.frame, styleMask: [.borderless, .nonactivatingPanel], backing: .buffered, defer: false)
        p.isOpaque = false
        p.backgroundColor = NSColor(white: 0, alpha: 0.001)   // clear, but still takes clicks
        p.hasShadow = false
        p.contentView = ClickSink()
        win.addChildWindow(p, ordered: .above)
        p.orderFront(nil)
        shield = p
    }
    static func real(_ type: CGEventType) {
        guard realClicks, let shield, shield.frame.contains(windowPoint(shield, pointer)) || true else { return }
        let src = CGEventSource(stateID: .hidSystemState)
        CGEvent(mouseEventSource: src, mouseType: type, mouseCursorPosition: pointer, mouseButton: .left)?.post(tap: .cghidEventTap)
    }
    final class ClickSink: NSView {
        override func mouseDown(with event: NSEvent) {}
        override func mouseDragged(with event: NSEvent) {}
        override func mouseUp(with event: NSEvent) {}
        override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }
    }

    static func glide(to: CGPoint, _ seconds: Double, each: (CGPoint) -> Void = { _ in }) {
        let from = pointer
        let dx = to.x - from.x, dy = to.y - from.y
        let dist = hypot(dx, dy)
        guard dist > 0.5 else { return }
        if dist < 90 || checking {
            leg(from: from, to: to, max(0.18, seconds * 0.6), bow: CGFloat(jitter(4)), each: each)
            return
        }
        // The flick stops a little short (or past) and to one side.
        let reach = 0.86 + jitter(0.07)
        let miss = CGPoint(x: from.x + dx * reach + jitter(10), y: from.y + dy * reach + jitter(10))
        leg(from: from, to: miss, 0.11 + dist / 7000, bow: CGFloat(jitter(10)), each: each)
        turn(0.05 + abs(jitter(0.06)))   // the thumb catching the ball
        leg(from: miss, to: to, 0.22 + min(0.25, hypot(to.x - miss.x, to.y - miss.y) / 400), bow: CGFloat(jitter(3)), each: each)
    }

    private static func leg(from: CGPoint, to: CGPoint, _ seconds: Double, bow: CGFloat, each: (CGPoint) -> Void) {
        let dx = to.x - from.x, dy = to.y - from.y
        let dist = hypot(dx, dy)
        let frames = checking ? 6 : max(3, Int(seconds / speed * 60))
        let nx = dist > 0 ? -dy / dist : 0, ny = dist > 0 ? dx / dist : 0
        var tx = 0.0, ty = 0.0
        for i in 1...frames {
            let t = Double(i) / Double(frames)
            // Fast in the middle, soft at both ends (a minimum-jerk curve).
            let e = t * t * t * (10 - 15 * t + 6 * t * t)
            let arc = sin(.pi * t) * bow
            // A tremor that fades out as the pointer lands.
            tx = tx * 0.8 + jitter(0.15); ty = ty * 0.8 + jitter(0.15)
            let calm = checking ? 0 : sin(.pi * t)
            let p = CGPoint(x: from.x + dx * e + nx * arc + tx * calm, y: from.y + dy * e + ny * arc + ty * calm)
            warp(p)
            each(p)
            pump(until: Date().addingTimeInterval(checking ? 0.001 : 1.0 / 60))
        }
        warp(to)
        each(to)
    }

    // MARK: - The montage

    private static func run(_ win: NSWindow, _ firstView: CircuitCanvasNSView) {
        guard let canvas = firstView.controller, let core = canvas.document else { print("demo: no canvas"); exit(1) }
        var failures = 0
        func check(_ ok: Bool, _ what: String) {
            if checking { print((ok ? "PASS " : "FAIL ") + what) }
            if !ok { failures += 1 }
        }
        var view: CircuitCanvasNSView { canvas.view ?? firstView }
        var hands: CanvasHands { CanvasHands(view: view) }
        func at(_ world: CGPoint) -> CGPoint { screen(win, hands.windowPoint(world)) }
        func world(_ screenPoint: CGPoint) -> CGPoint { view.worldPoint(view.convert(windowPoint(win, screenPoint), from: nil)) }

        // The window: most of the main display, in front (a check run's
        // stays far off every display).
        if let main = NSScreen.screens.first {
            if checking {
                win.setFrame(NSRect(x: -6000, y: -6000, width: 1440, height: 900), display: true)
            } else {
                win.setFrame(main.visibleFrame, display: true, animate: false)
                NSApp.activate(ignoringOtherApps: true)
                win.makeKeyAndOrderFront(nil)
            }
        }
        turn(0.8)
        raiseShield(over: win)

        // The page: empty, the build area filling the view.
        view.needsFit = false
        view.zoomTo(unitsPerPoint: max(0.02, max(46 / max(view.bounds.width, 400), 30 / max(view.bounds.height, 300))))
        view.center(on: CGPoint(x: -3, y: -1.5))
        canvas.selectNone()
        let page = canvas.page

        // The steps.

        func fromPalette(_ name: String, to spot: CGPoint) {
            think()
            if let cat = GateLibrary.categories.first(where: { GateLibrary.gates(in: $0).contains { $0.name == name } }) {
                NotificationCenter.default.post(name: .clPaletteCategory, object: canvas, userInfo: ["index": cat.index])
            }
            turn(0.25)
            guard let f = tiles[name], let content = win.contentView else {
                print("demo: \(name) isn't showing in the palette; placing it straight away")
                canvas.addGate(name, at: spot)
                return
            }
            let tile = screen(win, NSPoint(x: f.midX, y: content.bounds.height - f.midY))
            glide(to: tile, 0.45)
            turn(0.12)
            real(.leftMouseDown)
            guard canvas.addGateFloating(name, at: world(tile)) else { real(.leftMouseUp); return }
            glide(to: at(spot), 0.55) { p in
                let w = world(p)
                canvas.pointerMoved(w)
                _ = core.hover(page: page, at: w, unitsPerPoint: view.unitsPerPoint)
                view.needsDisplay = true
            }
            _ = core.press(page: page, at: spot, modifiers: [], unitsPerPoint: view.unitsPerPoint)
            core.release(at: spot); real(.leftMouseUp)
            NSCursor.arrow.set()   // the hand let go: back to the arrow
            canvas.edited()
            win.makeFirstResponder(view)
            turn(0.15)
        }

        func wire(_ a: CGPoint, _ b: CGPoint) {
            think()
            glide(to: at(a), 0.35) { p in hands.move(world(p)) }
            turn(0.08)
            hands.down(a); real(.leftMouseDown)
            glide(to: at(b), 0.5) { p in
                if let e = hands.mouse(.leftMouseDragged, world(p)) { view.mouseDragged(with: e) }
            }
            hands.up(b); real(.leftMouseUp)
            NSCursor.arrow.set()   // the hand let go: back to the arrow
            turn(0.15)
        }

        func wireCount(_ core: CoreDocument) -> Int { core.shareText(flags: 0).components(separatedBy: "(wire").count - 1 }

        /// Pick a part up where it is and put it down at `to`.
        func grab(_ from: CGPoint, to: CGPoint) {
            think()
            glide(to: at(from), 0.4) { p in hands.move(world(p)) }
            turn(0.15)
            hands.down(from); real(.leftMouseDown)
            turn(0.12)
            glide(to: at(to), 0.6) { p in
                if let e = hands.mouse(.leftMouseDragged, world(p)) { view.mouseDragged(with: e) }
            }
            hands.up(to); real(.leftMouseUp)
            NSCursor.arrow.set()   // the hand let go: back to the arrow
            turn(0.2)
        }

        func click(_ w: CGPoint) {
            think()
            glide(to: at(w), 0.35) { p in hands.move(world(p)) }
            turn(0.06)
            real(.leftMouseDown); hands.click(w); real(.leftMouseUp)
            NSCursor.arrow.set()   // the hand let go: back to the arrow
        }
        pointer = at(CGPoint(x: 8, y: -12))
        warp(pointer)
        turn(1.0)

        // Parts: two switches, an XOR and an AND, two lights (a half adder).
        // A and B line up with the gates they feed, so the wires start out
        // short and straight; the two that cross are the ones to tidy.
        let parts: [(String, CGPoint)] = [
            ("AA_TOGGLE", CGPoint(x: -18, y: 7)), ("AA_TOGGLE", CGPoint(x: -18, y: -7)),
            ("AI_XOR2", CGPoint(x: 0, y: 6)), ("AA_AND2", CGPoint(x: 0, y: -6)),
            ("GA_LED", CGPoint(x: 11, y: 6)), ("GA_LED", CGPoint(x: 11, y: -6)),
        ]
        for (name, spot) in parts { fromPalette(name, to: spot) }
        check(Int(cl_document_gate_count(core.handle, Int32(page))) == parts.count, "six parts placed from the palette")
        shot(win, "1-parts")

        // Wires, pin to pin.
        let wires: [(CGPoint, CGPoint)] = [
            (CGPoint(x: -16, y: 7), CGPoint(x: -3, y: 7)),      // A to the XOR
            (CGPoint(x: -16, y: -7), CGPoint(x: -3, y: -7)),    // B to the AND
            (CGPoint(x: 3, y: 6), CGPoint(x: 10, y: 6)),        // XOR to the Sum light
            (CGPoint(x: 3, y: -6), CGPoint(x: 10, y: -6)),      // AND to the Carry light
            (CGPoint(x: -3, y: -5), CGPoint(x: -16, y: 7)),     // the AND's free input back to A
            (CGPoint(x: -3, y: 5), CGPoint(x: -16, y: -7)),     // the XOR's free input back to B
        ]
        for (a, b) in wires { wire(a, b) }
        check(wireCount(core) >= 4, "the wires drawn (\(wireCount(core)))")
        shot(win, "2-wired")
        turn(0.6)

        // The two long wires run down the same line, one over the other:
        // slide the AND down and over to give them room, then Straighten.
        turn(0.5)
        grab(CGPoint(x: 0, y: -6), to: CGPoint(x: 1, y: -10))
        turn(0.4)
        grab(CGPoint(x: 11, y: -6), to: CGPoint(x: 11, y: -10))   // its light follows
        shot(win, "3-moved")
        turn(0.7)
        glide(to: at(CGPoint(x: -9, y: 2.5)), 0.5)
        canvas.perform(.selectAll)
        badge(win, keys: "⌘A", label: "Select All")
        turn(0.3)
        canvas.perform(.straighten)
        badge(win, keys: "S", label: "Straighten")
        turn(0.3)
        canvas.selectNone()
        win.makeFirstResponder(view)
        shot(win, "4-straightened")
        turn(1.0)

        // Flip the switches: A, then B, then A off.
        func lights() -> [Int] {
            var out = [CLSimLight](repeating: CLSimLight(), count: 8)
            let n = Int(cl_simview_lights(core.handle, Int32(page), &out, 8))
            return out.prefix(n).map { Int($0.value) }   // top to bottom: Sum, Carry
        }
        check(lights() == [0, 0], "both lights off to start (\(lights()))")
        click(CGPoint(x: -18, y: 7)); turn(0.8)
        check(lights() == [1, 0], "A on: Sum on, Carry off (\(lights()))")
        click(CGPoint(x: -18, y: -7)); turn(1.0)
        check(lights() == [0, 1], "A and B on: Sum off, Carry on (\(lights()))")
        click(CGPoint(x: -18, y: 7)); turn(0.8)
        check(lights() == [1, 0], "B on: Sum on, Carry off (\(lights()))")

        // Simulation View on the half adder for a moment.
        glide(to: at(CGPoint(x: 6, y: 0)), 0.6)
        badge(win, keys: "⌘R", label: "Simulation View", hold: 0.7)
        canvas.perform(.simView)
        turn(1.6)
        shot(win, "5-simview")
        click(CGPoint(x: -18, y: 7)); turn(1.4)
        badge(win, keys: "esc", label: "Done", hold: 0.7)
        canvas.perform(.simView)
        turn(0.6)

        // Something bigger: the 4-bit counter, running, in Simulation View.
        guard let counter = Templates.template(id: "builtin-counter") else { check(false, "the counter template"); finish(failures); return }
        // As a new tab of this same window (a second window plays its
        // opening animation first, and that needs a window on screen).
        badge(win, keys: "⌘T", label: "New Tab", hold: 0.6)
        canvas.perform(.newTab)
        turn(0.6)
        let page2 = canvas.page
        // The template's circuit, copied out of a copy of it, pasted here.
        var copied = ""
        if let data = counter.with(clock: .running).text.data(using: .utf8), let temp = try? CoreDocument(data: data) {
            temp.selectAll(page: 0)
            copied = temp.copySelection(page: 0)
        }
        badge(win, keys: "⌘V", label: "Paste a 4-Bit Counter", hold: 0.8)
        check(core.paste(copied, page: page2, at: CGPoint(x: 0, y: 0), shift: false) != nil, "the counter pasted into a new tab")
        canvas.edited()
        canvas.selectNone()
        turn(0.5)
        check(Int(cl_document_gate_count(core.handle, Int32(page2))) >= 6, "the counter's parts are on the new tab (\(cl_document_gate_count(core.handle, Int32(page2))))")
        let win2 = win, view2 = view, canvas2 = canvas
        view2.needsFit = false
        // It lands off to one side: Space glides the view onto it.
        turn(0.6)
        badge(win, keys: "Space", label: "Fit to Window", hold: 0.5)
        view2.animateZoomToFit()
        turn(0.6)
        badge(win, keys: "⌘R", label: "Simulation View", hold: 0.7)
        canvas2.perform(.simView)
        turn(0.4)
        canvas2.setRunning(true)
        turn(1.2)
        // Slow it down where the viewer can see it: the pointer goes to the
        // Speed slider and drags its knob left.
        if let f = speedSlider, let content = win.contentView {
            func x(_ ms: Int) -> CGFloat { f.minX + f.width * (1 - CGFloat(log(Double(ms)) / log(500))) }
            func onScreen(_ px: CGFloat) -> CGPoint { screen(win, NSPoint(x: px, y: content.bounds.height - f.midY)) }
            think()
            glide(to: onScreen(x(canvas2.stepMs)), 0.7)
            turn(0.25)
            real(.leftMouseDown)
            glide(to: onScreen(x(60)), 0.6) { p in
                let local = windowPoint(win, p).x - f.minX
                let frac = min(1, max(0, Double(local / f.width)))
                canvas2.stepMs = max(1, min(500, Int(pow(500, 1 - frac).rounded())))
            }
            canvas2.stepMs = 60
            real(.leftMouseUp)
            NSCursor.arrow.set()
        } else {
            canvas2.stepMs = 60
        }
        turn(0.6)
        turn(0.6)
        // The camera: fit the whole circuit, ease in on the middle, back out.
        view2.needsFit = false
        turn(0.4)
        let fitUPP = view2.unitsPerPoint
        let box = canvas2.document?.fitBounds(ofPage: canvas2.page) ?? .zero
        let middle = CGPoint(x: box.midX, y: box.midY)
        check(view2.visibleWorldRect.contains(box), "the whole counter is in view (\(box) in \(view2.visibleWorldRect))")
        shot(win2, "6-counter")
        glide(to: screen(win2, view2.convert(NSPoint(x: view2.bounds.midX + 120, y: view2.bounds.midY + 80), to: nil)), 0.3)
        turn(2.0)
        turn(2.5)
        turn(3.0)
        check(view2.visibleWorldRect.contains(box), "the whole counter is back in view")
        check(canvas2.simView && (canvas2.document?.isRunning ?? false), "the counter runs in Simulation View")

        finish(failures)
    }

    /// Zoom smoothly between two scales about one point.
    private static func camera(_ view: CircuitCanvasNSView, from: CGFloat, to: CGFloat, center: CGPoint, seconds: Double) {
        let frames = checking ? 4 : max(2, Int(seconds / speed * 60))
        for i in 1...frames {
            let t = Double(i) / Double(frames)
            let e = CGFloat(t < 0.5 ? 2 * t * t : 1 - pow(-2 * t + 2, 2) / 2)
            view.zoomTo(unitsPerPoint: from * pow(to / from, e))
            view.center(on: center)
            pump(until: Date().addingTimeInterval(checking ? 0.001 : 1.0 / 60))
        }
    }

    private static func finish(_ failures: Int) {
        if checking {
            print(failures == 0 ? "demo: all passed" : "demo: \(failures) failed")
            exit(failures == 0 ? 0 : 1)
        }
        print("demo: done (stop recording; quit the app with Cmd-Q)")
    }
}
