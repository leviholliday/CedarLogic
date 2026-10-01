// The toolbar's real-mouse test, app side (Tools/bar-test.sh drives it).
// With CL_BAR_TEST=<dir> the app opens a new, untitled circuit, puts its
// window in a known place, and writes what the bar hears to <dir>/app.log:
// presses, clicks, hover, tooltips, window moves. Clicks on tools are logged
// instead of done, so a test never changes anything. It also writes where
// every control is on screen (<dir>/coords.json) and takes a few commands
// from <dir>/cmd. Nothing here runs without the switch.

import AppKit

@MainActor
enum BarTest {
    static let on = ProcessInfo.processInfo.environment["CL_BAR_TEST"] != nil
    private static let dir = ProcessInfo.processInfo.environment["CL_BAR_TEST"] ?? ""
    private static var handle: FileHandle?
    private static var version = 0
    private static var pending = false

    static func note(_ s: @autoclosure () -> String) {
        guard on else { return }
        if handle == nil {
            FileManager.default.createFile(atPath: dir + "/app.log", contents: nil)
            handle = FileHandle(forWritingAtPath: dir + "/app.log")
        }
        let line = String(format: "%.3f APP ", ProcessInfo.processInfo.systemUptime) + s() + "\n"
        handle?.write(line.data(using: .utf8)!)
    }

    static func start() {
        guard on else { return }
        if ProcessInfo.processInfo.environment["CL_BAR_WATCH"] != nil {
            // Development: the window buttons' positions, sampled through launch.
            Timer.scheduledTimer(withTimeInterval: 0.2, repeats: true) { _ in
                MainActor.assumeIsolated {
                    guard let w = NSApp.windows.first(where: { NSDocumentController.shared.document(for: $0) != nil }) else { return }
                    let l = [NSWindow.ButtonType.closeButton, .miniaturizeButton, .zoomButton].compactMap { w.standardWindowButton($0) }
                        .map { b -> String in let r = b.convert(b.bounds, to: nil); return "(\(Int(r.minX)),\(Int(r.minY)))" }
                    let inner = [NSWindow.ButtonType.closeButton, .miniaturizeButton, .zoomButton].compactMap { w.standardWindowButton($0) }
                        .map { b in b.subviews.map { v in "\(Int(v.frame.minX)),\(Int(v.frame.minY)) \(Int(v.frame.width))x\(Int(v.frame.height)) t\(Int(v.layer?.affineTransform().tx ?? 0)),\(Int(v.layer?.affineTransform().ty ?? 0))" }.joined() + " bt\(Int(b.layer?.affineTransform().tx ?? 0)),\(Int(b.layer?.affineTransform().ty ?? 0)) \(type(of: b))" }
                    let shown = [NSWindow.ButtonType.closeButton, .miniaturizeButton, .zoomButton].compactMap { w.standardWindowButton($0) }
                        .map { b -> String in
                            let m = b.layer?.position ?? .zero, p = b.layer?.presentation()?.position ?? .zero
                            return "model(\(Int(m.x)),\(Int(m.y))) shown(\(Int(p.x)),\(Int(p.y))) anims \(b.layer?.animationKeys() ?? [])"
                        }
                    note("shown \(shown)")
                    note("inner \(inner)")
                    note("lights \(l.joined(separator: " ")) alpha \(w.alphaValue) key \(w.isKeyWindow) active \(NSApp.isActive)")
                }
            }
        }
        func wait() {
            guard let w = window() else {
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) { MainActor.assumeIsolated { wait() } }
                return
            }
            setUp(w)
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 1) { MainActor.assumeIsolated { wait() } }
    }

    private static func window() -> NSWindow? {
        NSApp.windows.first { $0.isVisible && $0.styleMask.contains(.fullSizeContentView) && NSDocumentController.shared.document(for: $0) != nil }
    }

    private static func frame(_ r: NSRect) -> String { "(\(Int(r.minX)),\(Int(r.minY)) \(Int(r.width))x\(Int(r.height)))" }

    private static func setUp(_ w: NSWindow) {
        let screens = NSScreen.screens
        let si = Int(ProcessInfo.processInfo.environment["CL_BAR_SCREEN"] ?? "0") ?? 0
        let vf = screens[max(0, min(si, screens.count - 1))].visibleFrame
        w.setFrame(NSRect(x: vf.minX + 150, y: vf.maxY - 80 - 700, width: 1100, height: 700), display: true)
        if ProcessInfo.processInfo.environment["CL_BAR_INACTIVE"] == nil {
            NSApp.activate(ignoringOtherApps: true)
            w.makeKeyAndOrderFront(nil)
        }
        for name in [NSWindow.didMoveNotification, NSWindow.didResizeNotification] {
            NotificationCenter.default.addObserver(forName: name, object: w, queue: .main) { _ in
                MainActor.assumeIsolated { note("frame \(frame(w.frame))"); publishSoon(w) }
            }
        }
        for name in [NSWindow.didEnterFullScreenNotification, NSWindow.didExitFullScreenNotification] {
            NotificationCenter.default.addObserver(forName: name, object: w, queue: .main) { n in
                MainActor.assumeIsolated { note(n.name.rawValue); publishSoon(w) }
            }
        }
        publishSoon(w)
        Timer.scheduledTimer(withTimeInterval: 0.05, repeats: true) { _ in
            MainActor.assumeIsolated {
                let path = dir + "/cmd"
                guard let c = try? String(contentsOfFile: path, encoding: .utf8) else { return }
                try? FileManager.default.removeItem(atPath: path)
                let cmd = c.trimmingCharacters(in: .whitespacesAndNewlines)
                note("cmd \(cmd)")
                switch cmd {
                case "deactivate": NSApp.deactivate()
                case "narrower": var f = w.frame; f.size.width -= 220; w.setFrame(f, display: true)
                case "focus":
                    if CanvasController.front == nil { note("no front canvas for focus mode") }
                    NotificationCenter.default.post(name: .clToggleFocusMode, object: CanvasController.front)
                    publishSoon(w)
                case "fullscreen": w.toggleFullScreen(nil)
                case "otherscreen":
                    let screens = NSScreen.screens
                    if let cur = w.screen, let to = screens.first(where: { $0 != cur }) {
                        let vf = to.visibleFrame
                        w.setFrame(NSRect(x: vf.minX + 100, y: vf.maxY - 60 - w.frame.height, width: w.frame.width, height: w.frame.height), display: true)
                    }
                case "publish": publish(w)
                default: break
                }
            }
        }
    }

    private static func publishSoon(_ w: NSWindow) {
        guard !pending else { return }
        pending = true
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.35) {
            MainActor.assumeIsolated { pending = false; publish(w) }
        }
    }

    /// Where everything is, in the screen coordinates mouse events use (top
    /// left of the main screen, y down).
    private static func publish(_ w: NSWindow) {
        guard let cv = w.contentView else { return }
        let mainH = NSScreen.screens[0].frame.maxY
        func cg(_ p: NSPoint) -> [Double] {
            let s = w.convertPoint(toScreen: p)
            return [Double(s.x), Double(mainH - s.y)]
        }
        var targets: [[String: Any]] = []
        var strips: [[Double]] = []
        var titleMaxX: CGFloat = 0, toolMinX = CGFloat.infinity
        func walk(_ v: NSView) {
            if let c = v as? BarControl, let tip = c.tip, !v.isHiddenOrHasHiddenAncestor {
                let r = v.convert(v.bounds, to: nil)
                if tip.hasPrefix("Rename") { titleMaxX = r.maxX } else if r.minY > w.frame.height - 60 { toolMinX = min(toolMinX, r.minX) }
                targets.append(["tip": tip, "center": cg(NSPoint(x: r.midX, y: r.midY)),
                                "top": cg(NSPoint(x: r.midX, y: r.maxY - 4)), "bottom": cg(NSPoint(x: r.midX, y: r.minY + 3))])
            }
            if let d = v as? WindowDragArea.DragView, d.onPointer != nil {
                let r = d.convert(d.bounds, to: nil)
                let tl = cg(NSPoint(x: r.minX, y: r.maxY))
                strips.append([tl[0], tl[1], Double(r.width), Double(r.height)])
            }
            v.subviews.forEach(walk)
        }
        walk(cv)
        let h = w.frame.height
        let gapX = titleMaxX > 0 && toolMinX < .infinity ? (titleMaxX + toolMinX) / 2 : w.frame.width / 2
        let vf = w.screen?.visibleFrame ?? .zero
        let sf = w.screen?.frame ?? .zero
        version += 1
        let json: [String: Any] = [
            "version": version, "targets": targets, "strips": strips,
            "bar": cg(NSPoint(x: gapX, y: h - 26)), "barLow": cg(NSPoint(x: gapX, y: h - 47)), "left": cg(NSPoint(x: 8, y: h - 26)),
            "frame": [Double(w.frame.minX), Double(mainH - w.frame.maxY), Double(w.frame.width), Double(w.frame.height)],
            "visible": [Double(vf.minX), Double(mainH - vf.maxY), Double(vf.width), Double(vf.height)],
            "screen": [Double(sf.minX), Double(mainH - sf.maxY), Double(sf.width), Double(sf.height)],
            "fullScreen": w.styleMask.contains(.fullScreen),
            "drawnOff": [NSWindow.ButtonType.closeButton, .miniaturizeButton, .zoomButton].compactMap { w.standardWindowButton($0) }
                .map { b -> Double in let p = b.layer?.position ?? b.frame.origin; return Double(max(abs(p.x - b.frame.origin.x), abs(p.y - b.frame.origin.y))) },
            "lights": [NSWindow.ButtonType.closeButton, .miniaturizeButton, .zoomButton].compactMap { w.standardWindowButton($0) }
                .map { b -> [Double] in let r = b.convert(b.bounds, to: nil); return [Double(r.minX), Double(r.minY), Double(r.width), Double(r.height)] },
        ]
        if let d = try? JSONSerialization.data(withJSONObject: json, options: [.prettyPrinted]) {
            try? d.write(to: URL(fileURLWithPath: dir + "/coords.json"), options: .atomic)
        }
        note("published v\(version) frame \(frame(w.frame))")
    }
}
