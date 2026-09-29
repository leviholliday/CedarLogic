// The toolbar's real-mouse test, driver side: real mouse events (the kind a
// trackpad sends), then a check of what the app heard (App/BarTest.swift).
// Run through Tools/bar-test.sh, which starts the app.
//
//   bar-test <dir> <app pid>
//
// It moves your pointer for about a minute. Every press is only sent where
// the test window is the topmost thing on screen, so nothing else is ever
// clicked; a step that can't be done safely fails as "blocked".

import AppKit
import CoreGraphics
import Foundation

let dir = CommandLine.arguments[1]
let appPID = Int(CommandLine.arguments[2])!
func now() -> Double { ProcessInfo.processInfo.systemUptime }
extension Array { subscript(safe i: Int) -> Element? { indices.contains(i) ? self[i] : nil } }
func nap(_ s: Double) { usleep(useconds_t(s * 1_000_000)) }

// MARK: - Where things are (from the app)

struct Coords {
    var version = 0, targets: [[String: Any]] = [], strips: [[Double]] = []
    var bar = CGPoint.zero, barLow = CGPoint.zero, left = CGPoint.zero
    var frame: [Double] = [], visible: [Double] = [], screen: [Double] = [], fullScreen = false, lights: [[Double]] = []
    func t(_ prefix: String, _ part: String = "center") -> CGPoint? {
        guard let x = targets.first(where: { ($0["tip"] as? String ?? "").hasPrefix(prefix) }), let p = x[part] as? [Double] else { return nil }
        return CGPoint(x: p[0], y: p[1])
    }
    func tip(_ prefix: String) -> String { targets.first { ($0["tip"] as? String ?? "").hasPrefix(prefix) }?["tip"] as? String ?? prefix }
}
func point(_ a: Any?) -> CGPoint { let p = a as? [Double] ?? [0, 0]; return CGPoint(x: p[0], y: p[1]) }
func load() -> Coords? {
    guard let d = FileManager.default.contents(atPath: dir + "/coords.json"),
          let j = try? JSONSerialization.jsonObject(with: d) as? [String: Any] else { return nil }
    return Coords(version: j["version"] as? Int ?? 0, targets: j["targets"] as? [[String: Any]] ?? [], strips: j["strips"] as? [[Double]] ?? [],
                  bar: point(j["bar"]), barLow: point(j["barLow"]), left: point(j["left"]),
                  frame: j["frame"] as? [Double] ?? [], visible: j["visible"] as? [Double] ?? [], screen: j["screen"] as? [Double] ?? [],
                  fullScreen: j["fullScreen"] as? Bool ?? false, lights: j["lights"] as? [[Double]] ?? [])
}
func fresh(after v: Int, timeout: Double = 2) -> Coords? {
    let end = now() + timeout
    while now() < end { if let c = load(), c.version > v { return c }; nap(0.1) }
    return load()
}
func cmd(_ s: String) { try? s.write(toFile: dir + "/cmd", atomically: true, encoding: .utf8) }

// MARK: - The mouse

let src = CGEventSource(stateID: .hidSystemState)
var blocked = false
/// Only where the test window is on top: anything else there must not be clicked.
func safe(_ p: CGPoint) -> Bool {
    let list = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as? [[String: Any]] ?? []
    for w in list {
        guard let b = w[kCGWindowBounds as String] as? [String: Any], let x = b["X"] as? Double, let y = b["Y"] as? Double,
              let wd = b["Width"] as? Double, let h = b["Height"] as? Double, (w[kCGWindowAlpha as String] as? Double ?? 1) > 0,
              CGRect(x: x, y: y, width: wd, height: h).contains(p) else { continue }
        if w[kCGWindowOwnerPID as String] as? Int == appPID { return true }
        if (w[kCGWindowOwnerName as String] as? String) == "Window Server" && wd < 64 { continue }
        print("    blocked: \(w[kCGWindowOwnerName as String] ?? "?") is over \(Int(p.x)),\(Int(p.y))")
        blocked = true
        return false
    }
    print("    blocked: nothing of the test's at \(Int(p.x)),\(Int(p.y))")
    blocked = true
    return false
}
func post(_ t: CGEventType, _ p: CGPoint, clicks: Int64 = 1) {
    guard let e = CGEvent(mouseEventSource: src, mouseType: t, mouseCursorPosition: p, mouseButton: .left) else { return }
    e.setIntegerValueField(.mouseEventClickState, value: clicks)
    e.post(tap: .cghidEventTap)
}
func move(_ p: CGPoint) { post(.mouseMoved, p) }
func click(_ p: CGPoint?, wobble: [CGPoint] = []) {
    guard let p, safe(p) else { return }
    move(p); nap(0.06)
    post(.leftMouseDown, p)
    var q = p
    for d in wobble { nap(0.02); q = CGPoint(x: p.x + d.x, y: p.y + d.y); post(.leftMouseDragged, q) }
    nap(0.07)
    post(.leftMouseUp, q)
}
func doubleClick(_ p: CGPoint?) {
    guard let p, safe(p) else { return }
    move(p); nap(0.06)
    post(.leftMouseDown, p, clicks: 1); nap(0.05); post(.leftMouseUp, p, clicks: 1); nap(0.09)
    post(.leftMouseDown, p, clicks: 2); nap(0.05); post(.leftMouseUp, p, clicks: 2)
}
func drag(_ p: CGPoint?, by d: CGPoint, steps: Int = 14) {
    guard let p, safe(p) else { return }
    move(p); nap(0.06)
    post(.leftMouseDown, p); nap(0.1)
    for i in 1...steps { let f = Double(i) / Double(steps); post(.leftMouseDragged, CGPoint(x: p.x + d.x * f, y: p.y + d.y * f)); nap(0.02) }
    nap(0.1)
    post(.leftMouseUp, CGPoint(x: p.x + d.x, y: p.y + d.y))
}

// MARK: - What the app heard

func appLines(from t0: Double, to t1: Double) -> [String] {
    let text = (try? String(contentsOfFile: dir + "/app.log", encoding: .utf8)) ?? ""
    return text.split(separator: "\n").compactMap { l in
        guard let t = Double(l.split(separator: " ").first ?? "") , t >= t0, t <= t1 else { return nil }
        return String(l.split(separator: " ", maxSplits: 2).last ?? "")
    }
}
/// The window's frames logged in the lines ("frame (x,y wxh)"), as x, y, w, h.
func frames(_ lines: [String]) -> [[Double]] {
    lines.compactMap { l in
        guard l.hasPrefix("frame (") else { return nil }
        let nums = l.dropFirst(7).split(whereSeparator: { ",x ) ".contains($0) }).compactMap { Double($0) }
        return nums.count == 4 ? nums : nil
    }
}

var results: [(String, Bool, String)] = []
func step(_ name: String, settle: Double = 0.5, _ act: () -> Void, check: ([String]) -> (Bool, String)) {
    print("  \(name)")
    blocked = false
    let t0 = now()
    act()
    nap(settle)
    let lines = appLines(from: t0, to: now())
    var (ok, why) = check(lines)
    if blocked { ok = false; why = "blocked (something else was on top)" }
    results.append((name, ok, why))
}
func has(_ lines: [String], _ s: String) -> Bool { lines.contains { $0.hasPrefix(s) } }
func count(_ lines: [String], _ s: String) -> Int { lines.filter { $0.hasPrefix(s) }.count }
func noMove(_ lines: [String]) -> Bool { frames(lines).isEmpty }

// MARK: - The test

let before = NSWorkspace.shared.frontmostApplication
let original = CGEvent(source: nil)?.location ?? .zero
guard var c = fresh(after: 0, timeout: 20) else { print("The app never said where its toolbar is."); exit(2) }
nap(0.8)
c = load() ?? c
func menuShowing() -> Bool {
    let list = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as? [[String: Any]] ?? []
    return list.contains { $0[kCGWindowOwnerPID as String] as? Int == appPID && ($0[kCGWindowLayer as String] as? Int ?? 0) == 101 }
}
func key(_ code: CGKeyCode) {
    CGEvent(keyboardEventSource: src, virtualKey: code, keyDown: true)?.post(tap: .cghidEventTap)
    CGEvent(keyboardEventSource: src, virtualKey: code, keyDown: false)?.post(tap: .cghidEventTap)
}

// The Minimal toolbar (STYLE=minimal): its few tools and the ••• menu.
if c.t("Step") == nil, let more = c.t("More") {
    print("Testing the Minimal toolbar (window \(c.frame.map { Int($0) }))")
    let run = c.t("Pause") != nil ? "Pause" : "Resume"
    step("Minimal: ••• lights up and shows its tooltip", settle: 1.5) { move(CGPoint(x: more.x - 2, y: more.y)); nap(0.05); move(more) } check: { l in
        (has(l, "lit More") && has(l, "tip shown: More"), has(l, "lit More") ? "no tooltip" : "not lit")
    }
    var opened = false
    step("Minimal: clicking ••• still opens its menu", settle: 0.2) {
        click(more); nap(0.6); opened = menuShowing(); key(53)   // Escape closes it
    } check: { _ in (opened, "no menu opened") }
    step("Minimal: a tool's top edge clicks, sloppily") {
        click(c.t(run, "top"), wobble: [CGPoint(x: 2, y: 1), CGPoint(x: 5, y: 2)])
    } check: { l in (has(l, "CLICK \(c.tip(run))") && noMove(l), "no click arrived") }
    step("Minimal: drag the empty bar") { drag(c.bar, by: CGPoint(x: 40, y: 30)) } check: { l in (frames(l).count > 1, "didn't move") }
    move(original)
    if let before, before.processIdentifier != pid_t(appPID) { before.activate() }
    print("")
    for (name, ok, why) in results { print("\(ok ? "PASS" : "FAIL")  \(name)\(ok || why.isEmpty ? "" : "  -- \(why)")") }
    let failed = results.filter { !$0.1 }.count
    print("\n\(results.count - failed) of \(results.count) passed")
    exit(failed == 0 ? 0 : 1)
}
guard c.t("Step") != nil else { print("No toolbar tools found (is the CedarLogic interface on, with the full toolbar?)"); exit(2) }
let run = c.t("Pause") != nil ? "Pause" : "Resume"
print("Testing the toolbar (window \(c.frame.map { Int($0) }))")

func clickCheck(_ prefix: String) -> ([String]) -> (Bool, String) {
    { l in
        let ok = has(l, "CLICK \(c.tip(prefix))") && noMove(l)
        return (ok, ok ? "" : has(l, "CLICK") ? "the window moved" : "no click arrived")
    }
}

func lightsLevel(_ c: Coords) -> (Bool, String) {
    let ys = c.lights.map { $0[1] }, xs = c.lights.map { $0[0] }
    let level = (ys.max() ?? 0) - (ys.min() ?? 0) < 0.5
    let inOrder = xs == xs.sorted()
    return (level && inOrder && ys.count == 3, "traffic light heights \(ys), x \(xs)")
}
step("Red, yellow and green buttons sit level", settle: 0.1) { } check: { _ in lightsLevel(c) }
step("Hover lights every tool, top edge included") {
    for t in c.targets { if let p = t["top"] as? [Double] { move(CGPoint(x: p[0] - 2, y: p[1])); nap(0.03); move(CGPoint(x: p[0], y: p[1])); nap(0.3) } }
} check: { l in
    let missing = c.targets.compactMap { $0["tip"] as? String }.filter { !has(l, "lit \($0)") }
    return (missing.isEmpty, missing.isEmpty ? "" : "not lit: \(missing.joined(separator: ", "))")
}
step("A tooltip after resting on a tool", settle: 1.5) { move(c.t("Step", "top")!) } check: { l in
    (has(l, "tip shown: \(c.tip("Step"))"), "no tooltip")
}
step("Moving on shows the next tooltip at once", settle: 0.4) { move(c.t("Lock")!) } check: { l in
    (has(l, "tip shown: \(c.tip("Lock"))"), "no tooltip")
}
step("Click at a tool's top edge") { click(c.t(run, "top")) } check: { clickCheck(run)($0) }
step("Sloppy click (moves 5 points while pressed)") {
    click(c.t("Step"), wobble: [CGPoint(x: 2, y: 1), CGPoint(x: 4, y: 2), CGPoint(x: 5, y: 3)])
} check: { clickCheck("Step")($0) }
step("Sloppy click at the top edge (8 points)") {
    click(c.t("Lock", "top"), wobble: [CGPoint(x: 3, y: -2), CGPoint(x: 6, y: -3), CGPoint(x: 8, y: -4)])
} check: { clickCheck("Lock")($0) }
step("Click the circuit's name") { click(c.t("Rename")) } check: { clickCheck("Rename")($0) }
step("Double-click a tool: two clicks, window stays") { doubleClick(c.t("Step")) } check: { l in
    let ok = count(l, "CLICK \(c.tip("Step"))") == 2 && noMove(l)
    return (ok, "\(count(l, "CLICK")) clicks, \(frames(l).count) moves")
}
step("Pointer off the bar puts the light out") { move(CGPoint(x: c.bar.x, y: c.bar.y + 200)) } check: { l in
    (has(l, "unlit"), "still lit")
}
step("Drag the empty bar moves the window") { drag(c.bar, by: CGPoint(x: 60, y: 40)) } check: { l in
    let f = frames(l)
    guard let a = f.first, let b = f.last, f.count > 1 else { return (false, "didn't move") }
    return (abs(b[0] - a[0]) > 30, "moved \(Int(b[0] - a[0])),\(Int(b[1] - a[1]))")
}
c = fresh(after: c.version) ?? c
let sized = c.frame
step("Buttons level after moving", settle: 0.1) { } check: { _ in lightsLevel(c) }
step("Double-click the empty bar fills the screen", settle: 1.5) { doubleClick(c.bar) } check: { l in
    guard let b = frames(l).last else { return (false, "nothing happened") }
    let ok = b[2] > sized[2] + 200
    return (ok, "now \(Int(b[2]))x\(Int(b[3]))")
}
c = fresh(after: c.version) ?? c
step("Dragging a filled window brings back its size", settle: 1.2) { drag(c.bar, by: CGPoint(x: 0, y: 150), steps: 20) } check: { l in
    guard let b = frames(l).last else { return (false, "didn't move") }
    return (abs(b[2] - sized[2]) < 12, "now \(Int(b[2])) wide, was \(Int(sized[2]))")
}
c = fresh(after: c.version) ?? c
step("Drag from left of the red button") { drag(c.left, by: CGPoint(x: 30, y: 20)) } check: { l in
    (frames(l).count > 1, "didn't move")
}
c = fresh(after: c.version) ?? c
step("Tools still click after the window narrows", settle: 0.5) {
    cmd("narrower"); nap(0.9); c = fresh(after: c.version) ?? c
    click(c.t("New tab", "top"), wobble: [CGPoint(x: 1, y: -1), CGPoint(x: 3, y: -1)])
} check: { l in (has(l, "CLICK \(c.tip("New tab"))"), "no click arrived") }

// Focus mode: the tab strip is the top row.
cmd("focus"); nap(1.0)
c = fresh(after: c.version) ?? c
if let s = c.strips.first {
    let tab0 = CGPoint(x: s[0] + 86 + 40, y: s[1] + s[3] / 2)
    let empty = CGPoint(x: s[0] + s[2] * 0.8, y: s[1] + s[3] / 2)
    step("Focus mode: hovering a tab lights it") { move(CGPoint(x: tab0.x - 3, y: tab0.y)); nap(0.05); move(tab0) } check: { l in
        (has(l, "strip hover 0"), "not lit")
    }
    if let plus = c.t("New tab") {
        step("Focus mode: the + lights and shows its tooltip", settle: 1.5) { move(plus) } check: { l in
            (has(l, "lit New tab") && has(l, "tip shown: New tab"), has(l, "lit New tab") ? "no tooltip" : "not lit")
        }
    }
    step("Focus mode: dragging the strip moves the window") { drag(empty, by: CGPoint(x: 40, y: 30)) } check: { l in
        (frames(l).count > 1, "didn't move")
    }
    c = fresh(after: c.version) ?? c
    if let s1 = c.strips.first {
        step("Focus mode: double-clicking the strip makes a tab") { doubleClick(CGPoint(x: s1[0] + s1[2] * 0.8, y: s1[1] + s1[3] / 2)) } check: { l in
            (has(l, "strip doubleclick"), "nothing")
        }
    }
} else {
    results.append(("Focus mode", false, "no tab strip in the top row"))
}
cmd("focus"); nap(1.0)

// Full screen.
cmd("fullscreen"); nap(2.5)
c = fresh(after: c.version, timeout: 3) ?? c
if c.fullScreen {
    move(CGPoint(x: c.screen[0] + c.screen[2] / 2, y: c.screen[1] + c.screen[3] / 2)); nap(0.6)
    step("Full screen: hover lights a tool") {
        if let p = c.t("Step") { move(CGPoint(x: p.x - 2, y: p.y)); nap(0.05); move(p) }
    } check: { l in (has(l, "lit \(c.tip("Step"))"), "not lit") }
    step("Full screen: a tool clicks") { click(c.t("Step")) } check: { l in (has(l, "CLICK \(c.tip("Step"))"), "no click arrived") }
    step("Full screen: a tool clicks at its top edge") { click(c.t("Lock", "top")) } check: { l in (has(l, "CLICK \(c.tip("Lock"))"), "no click arrived") }
    cmd("fullscreen"); nap(2.5)
} else {
    results.append(("Full screen", false, "the window didn't go full screen"))
}

move(original)
if let before, before.processIdentifier != pid_t(appPID) { before.activate() }

print("")
for (name, ok, why) in results { print("\(ok ? "PASS" : "FAIL")  \(name)\(ok || why.isEmpty ? "" : "  -- \(why)")") }
let failed = results.filter { !$0.1 }.count
print("\n\(results.count - failed) of \(results.count) passed")
exit(failed == 0 ? 0 : 1)
