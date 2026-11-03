// `CedarLogic --launch-selftest setup <state>` then `--launch-selftest check
// <state>` (two runs, as two launches): what a launch decides and does, from
// a saved state, without opening a window. mac/Tools/launch-selftest.sh
// runs every state many times over, each in a copy of the app with its own
// settings and home folder; this refuses to run as the real app or in your
// home folder, since `setup` replaces Your Circuits and the settings.
//
// The states: first (a first launch), normal, updated (What's New due),
// nolibrary (no Your Circuits folder at all), missinglast (the circuit you
// were last in is gone; a recent one is there), corruptlast (it's damaged),
// emptylast (it's an empty file), unsaved (edited after its last version,
// as a quit mid-session leaves it), brokenlibrary (folders with no circuit,
// unreadable names, a damaged circuit, a versions "folder" that's a file),
// damaged (as normal, then damaged copies of the sample circuits), offline
// (as normal, with Sparkle's last update check a month ago), trash (a
// circuit deleted from Your Circuits, one deleted 31 days ago, one moved
// into the trash by sync with no date, a second copy of a circuit that's
// still in the library, and the "repeated versions" folder), actions (as
// normal, with the Actions rail folded, hover-to-open off and its items
// reordered last time).
//
// `check` then goes through the launch: the welcome or What's New choice
// and the first-launch screen (CircuitWindow / Splash), which circuit
// reopens (LaunchDelegate.circuitToReopen) and that it opens or falls back
// to a new one, Your Circuits' list and its rows, the launch's clean-up of
// repeated versions, the first save's version, Sync and Classroom starting
// with the network unreachable (CL_CLASSROOM_SERVICE points nowhere) --
// none of which may block the main thread for more than a second; in
// `offline` the updater and crash reports with no network (pointed at
// addresses that can't be looked up) start and stay quiet -- and, in
// `damaged`, copies of the sample circuits cut short or with bytes changed
// (CL_DAMAGE_ROUND picks which), each opening or being refused, never
// crashing; in `trash`, Recently Deleted: the list, the 30-day sweep,
// Restore (under its own id, or a new one when that's taken) and Delete Now;
// in every state, where the Actions rail is (on screen for a first launch,
// hidden for anyone before), and in `actions` that a folded rail stays
// folded, a drag on the rail reorders the Customize list too, focus mode
// folds it (not hides it) and gives back what it was, the Customize
// list's search finds by name, menu and shortcut, and the rail,
// its tab (slid out on hover too) and the toolbar's six buttons laid out in
// real windows off screen (actionsChecks).
// PASS or FAIL for each; exit 1 on a failure.

import AppKit
import SwiftUI

@MainActor
enum LaunchSelfTest {
    nonisolated static let realBundle = "com.leviholliday.cedarlogic.native"

    static func runIfAsked() {
        let args = CommandLine.arguments
        guard let i = args.firstIndex(of: "--launch-selftest"), args.count > i + 2 else { return }
        setvbuf(stdout, nil, _IOLBF, 0)
        guard let id = Bundle.main.bundleIdentifier, id != realBundle, SessionCheck.isolatedHome() else {
            print("launch-selftest: only in a copy of the app with a home folder of its own; run mac/Tools/launch-selftest.sh")
            exit(2)
        }
        let state = args[i + 2]
        switch args[i + 1] {
        case "setup": setUp(state, bundle: id); exit(0)
        case "check": exit(check(state) ? 0 : 1)
        default: exit(2)
        }
    }

    private static func circuitText(gates: Int) -> String {
        let doc = CoreDocument()
        for g in 0..<gates { _ = doc.addGate(g % 2 == 0 ? "AA_AND2" : "AA_TOGGLE", page: 0, at: CGPoint(x: g * 8, y: 0)) }
        return doc.saveText()
    }
    private static let recentsKey = "cl.selftest.recents"

    // MARK: setup

    static func setUp(_ state: String, bundle: String) {
        let d = UserDefaults.standard
        d.removePersistentDomain(forName: bundle)
        let fm = FileManager.default
        try? fm.removeItem(at: Library.root.deletingLastPathComponent())   // .../CedarLogic, as a new Mac has it
        if state != "first" {
            d.set(true, forKey: "cl.hasSeenWelcome")
            d.set(true, forKey: "cl.firstLaunchPlayed")
            d.set(state == "updated" ? "0.2" : WhatsNew.version, forKey: "cl.whatsNewSeen")
            d.set(state == "offline", forKey: "cl.crashReports")
        }
        if state == "offline" {
            // Sparkle's own record: launched before, last looked a long time
            // ago, so its schedule looks again straight away.
            d.set(true, forKey: "SUHasLaunchedBefore")
            d.set(true, forKey: "SUEnableAutomaticChecks")
            d.set(Date(timeIntervalSinceNow: -30 * 86400), forKey: "SULastCheckTime")
        }
        if state == "actions" {
            // Someone who folded the rail and put its items in another order.
            d.set(ActionsMode.onScreen.rawValue, forKey: "cl.actions.mode")
            d.set(true, forKey: "cl.actions.collapsed")
            d.set(false, forKey: "cl.actions.hoverOpen")
            d.set(false, forKey: "cl.actions.hint")
            d.set(ActionID.defaults.reversed().map(\.rawValue), forKey: "cl.actions.order")
            d.set(ActionID.defaults.map(\.rawValue), forKey: "cl.actions.shown")
        }
        if state == "first" || state == "nolibrary" { d.synchronize(); return }
        var made: [LibraryItem] = []
        for (n, name) in ["Half adder", "Lab 5", "Counter"].enumerated() {
            if let item = try? Library.create(named: name, text: circuitText(gates: 2 + n * 3)) {
                Library.noteSaved(item.circuit)
                made.append(item)
            }
        }
        guard let last = made.first else { return }
        d.set(made.dropFirst().map(\.circuit.path), forKey: recentsKey)
        d.set(last.circuit.path, forKey: "cl.lastCircuit")
        switch state {
        case "missinglast": try? fm.removeItem(at: last.folder)
        case "corruptlast":
            let text = circuitText(gates: 9)
            try? Data(text.utf8.prefix(text.utf8.count / 3) + [0xFF, 0x00, 0xFE]).write(to: last.circuit)
        case "emptylast": try? Data().write(to: last.circuit)
        case "unsaved":
            // Worked on, and autosaved, an hour ago since its last version:
            // that work waits in versions/.pending.cdl for the next save.
            let text = circuitText(gates: 14)
            try? text.write(to: last.circuit, atomically: true, encoding: .utf8)
            let pending = last.versionsFolder.appendingPathComponent(".pending.cdl")
            try? text.write(to: pending, atomically: true, encoding: .utf8)
            try? fm.setAttributes([.modificationDate: Date().addingTimeInterval(-3600)], ofItemAtPath: pending.path)
        case "brokenlibrary":
            let root = Library.root
            try? fm.createDirectory(at: root.appendingPathComponent("no-circuit"), withIntermediateDirectories: true)
            let odd = root.appendingPathComponent("odd-name")
            try? fm.createDirectory(at: odd, withIntermediateDirectories: true)
            try? Data([0xFF, 0xFE, 0x00, 0xC3]).write(to: odd.appendingPathComponent("name.txt"))
            try? circuitText(gates: 1).write(to: odd.appendingPathComponent("circuit.cdl"), atomically: true, encoding: .utf8)
            let bad = root.appendingPathComponent("damaged")
            try? fm.createDirectory(at: bad, withIntermediateDirectories: true)
            try? "(circuit (page".write(to: bad.appendingPathComponent("circuit.cdl"), atomically: true, encoding: .utf8)
            try? Data("versions".utf8).write(to: bad.appendingPathComponent("versions"))
            try? Data("x".utf8).write(to: root.appendingPathComponent("a-file-not-a-folder"))
        case "trash":
            if made.count == 3 { Library.moveToTrash(made[2]) }   // "Counter", deleted just now
            let bin = RecentlyDeleted.trash(in: Library.root)
            func trashed(_ folder: String, _ name: String, deleted: Date?) {
                let f = bin.appendingPathComponent(folder, isDirectory: true)
                try? fm.createDirectory(at: f.appendingPathComponent("versions"), withIntermediateDirectories: true)
                try? name.write(to: f.appendingPathComponent("name.txt"), atomically: true, encoding: .utf8)
                try? circuitText(gates: 3).write(to: f.appendingPathComponent("circuit.cdl"), atomically: true, encoding: .utf8)
                if let deleted { RecentlyDeleted.stamp(f, at: deleted) }
            }
            trashed("20240101-120000-11111", "Old one", deleted: Date(timeIntervalSinceNow: -31 * 86400))
            trashed("20250101-120000-22222", "From the phone", deleted: nil)
            trashed(last.id + " 2", "Half adder again", deleted: Date(timeIntervalSinceNow: -86400))
            let repeated = bin.appendingPathComponent("repeated versions/\(last.id)", isDirectory: true)
            try? fm.createDirectory(at: repeated, withIntermediateDirectories: true)
            try? circuitText(gates: 1).write(to: repeated.appendingPathComponent("20240101-000000.cdl"), atomically: true, encoding: .utf8)
        default: break
        }
        d.synchronize()
    }

    // MARK: offline

    /// No network: the updater (pointed by launch-selftest.sh at a feed whose
    /// name can't be looked up, as with Wi-Fi off) and crash reports (at an
    /// address that can't be either) start, Sparkle's scheduled check fails
    /// quietly -- no alert, no window, nothing holding the main thread -- and
    /// the launch carries on.
    static func offlineChecks(check: (Bool, String) -> Void, timed: (String, Double, () -> Void) -> Void, turn: (Double) -> Void) {
        let updates = Updates.shared
        check(Updates.testFeed != nil, "the updater's feed is the test's (\(Updates.testFeed ?? "none"))")
        timed("the updater starts", 1.0) { updates.start() }
        check(updates.started, "the updater is running")
        timed("crash reports start", 1.0) { CrashReports.start() }
        check(CrashReports.isStarted || !CrashReports.available, "crash reports are \(CrashReports.isStarted ? "running" : "not built in")")
        // Sparkle's own schedule looks first (the last look was a month ago);
        // if it hasn't within 8 s, the same background check is asked for.
        var waited = 0.0
        while updates.checksFinished == 0 && waited < 8 { turn(0.25); waited += 0.25 }
        let scheduled = updates.checksFinished > 0
        if !scheduled { timed("a background update check is asked for", 1.0) { updates.checkInBackground() } }
        waited = 0
        while updates.checksFinished == 0 && waited < 20 { turn(0.25); waited += 0.25 }
        print("INFO offline: \(scheduled ? "Sparkle's scheduled check" : "a background check") ended: \(updates.lastCheckError ?? "no error")")
        check(updates.checksFinished > 0, "the update check gave up (within \(Int(waited)) s)")
        check(updates.lastCheckError != nil, "and knew it couldn't reach the feed")
        turn(1.0)
        let shown = NSApp.windows.filter { $0.isVisible }
        check(shown.isEmpty, "no alert or window came up (\(shown.map { $0.title.isEmpty ? String(describing: type(of: $0)) : $0.title }))")
        check(NSApp.modalWindow == nil, "no question waits for an answer")
    }

    // MARK: actions

    /// The Actions rail (Actions.swift): on screen for a new install, hidden
    /// (with the status line's hint due) for someone who had CedarLogic
    /// before; in `actions`, a rail folded and reordered last time is still
    /// so, and laid out in a real window (off screen, never shown) it takes
    /// its width, or its tab's, from the canvas; in the toolbar instead, the
    /// toolbar has the six buttons and the rail is gone.
    static func actionsChecks(_ state: String, check: (Bool, String) -> Void, turn: (Double) -> Void) {
        let prefs = Prefs.shared
        switch state {
        case "first":
            check(prefs.actionsMode == .onScreen, "a first launch has the Actions rail on screen (\(prefs.actionsMode.name))")
            check(!prefs.actionsCollapsed && prefs.railActions == ActionID.defaults,
                  "open, with the six default actions (\(prefs.railActions.map(\.rawValue)))")
            check(!prefs.actionsHintPending, "and no status line about it")
            check(prefs.actionsHoverOpen, "folded, it slides out on hover (the default)")
        case "actions":
            check(prefs.actionsMode == .onScreen, "the rail is still on screen")
            check(prefs.actionsCollapsed, "it's still folded, as it was left")
            check(prefs.railActions == ActionID.defaults.reversed(), "in the order it was given (\(prefs.railActions.map(\.rawValue)))")
            check(!prefs.actionsHintPending, "no status line (they've seen it)")
            check(!prefs.actionsHoverOpen, "hover-to-open still off, as it was set")
            // A drag on the rail: the Customize list has the same order.
            let before = prefs.railActions
            prefs.moveRailAction(before[0], to: 2)
            let after = prefs.railActions
            check(after == [before[1], before[2], before[0]] + before[3...], "dragging the top one down two places on the rail moves it there (\(after.map(\.rawValue)))")
            check(prefs.actionsOrderAll.filter { prefs.actionShown($0) } == after, "and the Customize list has that order too")
            prefs.moveRailAction(before[0], to: 0)
            check(prefs.railActions == before, "and back to the top again")
            prefs.moveRailAction(before[5], to: 99)
            check(prefs.railActions == before, "a drop past the end leaves the last one last")
        default:
            check(prefs.actionsMode == .hidden, "someone who had CedarLogic before keeps their window: no rail (\(prefs.actionsMode.name))")
            check(prefs.actionsHintPending, "and the status line tells them once where it is")
        }
        guard state == "actions" else { return }

        let doc = CoreDocument()
        _ = doc.addGate("AA_AND2", page: 0, at: .zero)
        /// The canvas's width in a whole circuit window laid out off screen.
        func canvasWidth(_ mode: ActionsMode, folded: Bool, width: CGFloat = 1440) -> CGFloat {
            prefs.actionsMode = mode
            prefs.actionsCollapsed = folded
            let canvas = CanvasController()
            canvas.attach(doc)
            let size = NSSize(width: width, height: 800)
            let win = NSWindow(contentRect: NSRect(origin: NSPoint(x: -6000, y: -6000), size: size),
                               styleMask: [.titled, .fullSizeContentView], backing: .buffered, defer: false)
            win.isReleasedWhenClosed = false
            let host = NSHostingView(rootView: CLLayout(document: doc, canvas: canvas, page: .constant(0))
                .frame(width: size.width, height: size.height))
            win.contentView = host
            host.frame = NSRect(origin: .zero, size: size)
            host.layoutSubtreeIfNeeded()
            turn(1.0)
            let w = canvas.view?.frame.width ?? -1
            print("INFO actions: a \(Int(width))-point window, \(mode.name)\(folded ? ", folded" : ""): the canvas is \(Int(w)) wide")
            win.close()
            return w
        }
        /// The canvas's width in focus mode, then out of it again (View > Focus Mode's notification).
        func focusWidths(_ mode: ActionsMode, folded: Bool) -> (inFocus: CGFloat, after: CGFloat) {
            prefs.actionsMode = mode
            prefs.actionsCollapsed = folded
            let canvas = CanvasController()
            canvas.attach(doc)
            let size = NSSize(width: 1440, height: 800)
            let win = NSWindow(contentRect: NSRect(origin: NSPoint(x: -6000, y: -6000), size: size),
                               styleMask: [.titled, .fullSizeContentView], backing: .buffered, defer: false)
            win.isReleasedWhenClosed = false
            let host = NSHostingView(rootView: CLLayout(document: doc, canvas: canvas, page: .constant(0))
                .frame(width: size.width, height: size.height))
            win.contentView = host
            host.frame = NSRect(origin: .zero, size: size)
            host.layoutSubtreeIfNeeded()
            turn(1.0)
            NotificationCenter.default.post(name: .clToggleFocusMode, object: canvas)
            turn(1.0)
            let inFocus = canvas.view?.frame.width ?? -1
            NotificationCenter.default.post(name: .clToggleFocusMode, object: canvas)
            turn(1.0)
            let after = canvas.view?.frame.width ?? -1
            print("INFO actions: focus mode, \(mode.name)\(folded ? ", folded" : ""): the canvas is \(Int(inFocus)) wide in it, \(Int(after)) after")
            win.close()
            return (inFocus, after)
        }
        let none = canvasWidth(.hidden, folded: false)
        let open = canvasWidth(.onScreen, folded: false)
        let folded = canvasWidth(.onScreen, folded: true)
        let inBar = canvasWidth(.toolbar, folded: false)
        ActionsRail.pictureSlidOut = true
        let slid = canvasWidth(.onScreen, folded: true)
        ActionsRail.pictureSlidOut = false
        check(none > 0 && abs((none - open) - ActionsRail.width) < 2, "open, the rail takes its \(Int(ActionsRail.width)) points (\(none) to \(open))")
        check(abs((none - folded) - ActionsRail.tabWidth) < 2, "folded, just its tab's \(Int(ActionsRail.tabWidth)) (\(folded))")
        check(abs(none - inBar) < 1, "in the toolbar instead, no rail (\(inBar))")
        check(abs(slid - folded) < 1, "slid out on hover, it's over the canvas, not pushing it (\(slid))")
        let focusNone = focusWidths(.hidden, folded: false)
        let focusOpen = focusWidths(.onScreen, folded: false)
        check(focusOpen.inFocus > open && abs((focusNone.inFocus - focusOpen.inFocus) - ActionsRail.tabWidth) < 2,
              "in focus mode an open rail folds to its tab, not gone (\(focusNone.inFocus) to \(focusOpen.inFocus))")
        check(abs(focusOpen.after - open) < 1 && !prefs.actionsCollapsed, "out of focus mode it's open again, as it was (\(focusOpen.after))")
        let focusFolded = focusWidths(.onScreen, folded: true)
        check(abs(focusFolded.inFocus - focusOpen.inFocus) < 1 && abs(focusFolded.after - folded) < 1 && prefs.actionsCollapsed,
              "a folded rail stays folded through focus mode and after (\(focusFolded.after))")

        // Searching the Customize list: by name, by menu, by shortcut (symbols or words).
        let find = { (q: String) in prefs.actionsOrderAll.filter { ActionsCustomizeList.matches($0, q) } }
        let exports = find("export")
        check(exports.contains(.exportImage) && exports.contains(.exportReport) && !exports.contains(.tidy),
              "searching \u{201C}export\u{201D} finds the exports only (\(exports.map(\.rawValue)))")
        check(find("simulation").contains(.predict), "a menu's name finds what's in it")
        let byKey = ActionID.checkCircuit.action.keyLabel ?? ""
        check(!byKey.isEmpty && find(byKey).contains(.checkCircuit), "its shortcut finds it (\(byKey))")
        check(find("cmd e").contains(.exportImage) || ActionID.exportImage.action.keyLabel != "⌘E", "and so does the shortcut in words (cmd e)")
        check(find("zzzz").isEmpty && find("").count == prefs.actionsOrderAll.count, "nothing for nonsense, everything for an empty search")
        let narrowNone = canvasWidth(.hidden, folded: false, width: 860)
        let narrowOpen = canvasWidth(.onScreen, folded: false, width: 860)
        check(abs((narrowNone - narrowOpen) - ActionsRail.tabWidth) < 2, "in a window under \(Int(ActionsRail.narrowWidth)) points it folds by itself (\(narrowNone) to \(narrowOpen))")

        /// The toolbar's buttons that are actions, by their tips (real views: BarClickArea).
        func toolbarActionButtons(_ mode: ActionsMode, width: CGFloat) -> Int {
            prefs.actionsMode = mode
            let canvas = CanvasController()
            canvas.attach(doc)
            let size = NSSize(width: width, height: 52)
            let win = NSWindow(contentRect: NSRect(origin: NSPoint(x: -6000, y: -6000), size: size),
                               styleMask: [.borderless], backing: .buffered, defer: false)
            win.isReleasedWhenClosed = false
            let host = NSHostingView(rootView: CLToolbar(document: doc, canvas: canvas, status: canvas.status, title: "Lab 5",
                                                         subtitle: "Page 1", focusMode: .constant(false))
                .frame(width: size.width, height: size.height))
            win.contentView = host
            host.frame = NSRect(origin: .zero, size: size)
            host.layoutSubtreeIfNeeded()
            turn(0.4)
            let tips = Set(prefs.railActions.map { $0.action.titleWithKey })
            var n = 0
            func walk(_ v: NSView) {
                if let c = v as? BarControl, let t = c.tip, tips.contains(t) { n += 1 }
                v.subviews.forEach(walk)
            }
            walk(host)
            win.close()
            return n
        }
        for w in [1280.0, 1000.0] {
            let n = toolbarActionButtons(.toolbar, width: w)
            check(n == 6, "in the toolbar, it has the six buttons at \(Int(w)) points wide (\(n))")
        }
        check(toolbarActionButtons(.onScreen, width: 1280) == 0, "with the rail on screen, the toolbar has none of them")
        prefs.actionsMode = .onScreen
        prefs.actionsCollapsed = true
    }

    // MARK: trash

    /// Recently Deleted, from what setUp("trash") left in the library's trash.
    static func trashChecks(check: (Bool, String) -> Void, timed: (String, Double, () -> Void) -> Void) {
        let fm = FileManager.default
        let bin = RecentlyDeleted.trash(in: Library.root)
        var before: [DeletedCircuit] = []
        timed("Recently Deleted lists the trash", 1.0) { before = RecentlyDeleted.list() }
        let names = Set(before.map(\.name))
        check(before.count == 4 && names == ["Counter", "Old one", "From the phone", "Half adder again"],
              "Recently Deleted shows the 4 deleted circuits (\(before.map(\.name)))")
        check(["Counter", "From the phone"].contains(before.first?.name ?? ""), "newest first (\(before.first?.name ?? "none"))")
        check(!before.contains { $0.id == "repeated versions" } && fm.fileExists(atPath: bin.appendingPathComponent("repeated versions").path),
              "the repeated versions folder isn't listed, and is left alone")
        if let c = before.first(where: { $0.name == "Counter" }) {
            check(abs(c.deleted.timeIntervalSinceNow) < 600, "a circuit deleted from Your Circuits is dated when it was deleted")
        }
        if let p = before.first(where: { $0.name == "From the phone" }) {
            check(fm.fileExists(atPath: p.folder.appendingPathComponent(RecentlyDeleted.stampFile).path) && abs(p.deleted.timeIntervalSinceNow) < 600,
                  "one sync moved there with no date is dated when it was moved, and stamped")
        }
        var swept = 0
        timed("the 30-day sweep", 1.0) { swept = RecentlyDeleted.sweep() }
        let after = RecentlyDeleted.list()
        check(swept == 1 && !after.contains { $0.name == "Old one" }, "the sweep deleted the one from 31 days ago, only (\(swept))")
        check(RecentlyDeleted.sweep() == 0, "a second sweep deletes nothing")

        // Restore: under its own id when that's free.
        if let c = after.first(where: { $0.name == "Counter" }) {
            let id = RecentlyDeleted.restore(c)
            let back = Library.items().first { $0.id == id }
            check(id == c.id && back?.name == "Counter", "Restore puts \u{201C}Counter\u{201D} back in Your Circuits under its own id")
            check(back.map { !fm.fileExists(atPath: $0.folder.appendingPathComponent(RecentlyDeleted.stampFile).path) } ?? false,
                  "without its deleted date")
            check(Library.items().first?.name == "Counter", "at the top of Your Circuits")
        } else { check(false, "Counter is in Recently Deleted") }
        // ...and under a new one when its id is taken (a second copy).
        if let c = after.first(where: { $0.name == "Half adder again" }) {
            let taken = String(c.id.dropLast(2))
            let id = RecentlyDeleted.restore(c)
            let items = Library.items()
            check(id != nil && id != taken && id?.contains(" ") == false && items.contains { $0.id == taken } && items.contains { $0.id == id && $0.name == "Half adder again" },
                  "a second copy of a circuit that's still there comes back beside it, under a new id (\(id ?? "none"))")
        } else { check(false, "the second copy is in Recently Deleted") }
        check(Library.items().count == 4, "Your Circuits has 4 circuits now (\(Library.items().count))")
        // Delete Now.
        if let c = RecentlyDeleted.list().first(where: { $0.name == "From the phone" }) {
            check(RecentlyDeleted.deleteNow(c) && !fm.fileExists(atPath: c.folder.path), "Delete Now deletes it for good")
        } else { check(false, "the sync-trashed circuit is in Recently Deleted") }
        check(RecentlyDeleted.list().isEmpty, "Recently Deleted is empty")
    }

    // MARK: check

    static func check(_ state: String) -> Bool {
        var failures = 0
        func check(_ ok: Bool, _ what: String) {
            print((ok ? "PASS " : "FAIL ") + "\(state): " + what)
            if !ok { failures += 1 }
        }
        /// How long the main thread was held: a launch step that waits on the
        /// network or a question shows here.
        func timed(_ what: String, limit: Double = 1.0, _ f: () -> Void) {
            let t = CACurrentMediaTime()
            f()
            let s = CACurrentMediaTime() - t
            check(s < limit, String(format: "%@ (%.2f s)", what, s))
        }
        func turn(_ seconds: Double) {
            let t = CACurrentMediaTime()
            RunLoop.main.run(until: Date().addingTimeInterval(seconds))
            let s = CACurrentMediaTime() - t
            if s > seconds + 1 { check(false, String(format: "the main thread was held %.1f s", s - seconds)) }
        }

        // The greeting, and the launch screen's first-launch version.
        let welcome = !Prefs.shared.hasSeenWelcome, whatsNew = WhatsNew.shouldShow
        check(welcome == (state == "first"), "the welcome \(welcome ? "shows" : "doesn't show")")
        check(whatsNew == (state == "updated"), "What's New \(whatsNew ? "shows" : "doesn't show")")
        check(!(welcome && whatsNew), "never the welcome and What's New together")
        let firstLaunch = !UserDefaults.standard.bool(forKey: "cl.hasSeenWelcome") && !UserDefaults.standard.bool(forKey: "cl.firstLaunchPlayed")
        check(firstLaunch == (state == "first"), "the launch screen is \(firstLaunch ? "the first launch's" : "the usual one")")

        // Which circuit reopens, and that it opens (or a new one does).
        let recents = (UserDefaults.standard.stringArray(forKey: recentsKey) ?? []).map { URL(fileURLWithPath: $0) }
        let chosen = LaunchDelegate.circuitToReopen(remembered: LastCircuit.url, recents: recents)
        switch state {
        case "first", "nolibrary": check(chosen == nil, "a new circuit opens (nothing to reopen)")
        case "missinglast": check(chosen != nil && chosen != LastCircuit.url, "a recent circuit reopens in place of the missing one")
        default: check(chosen != nil && chosen == LastCircuit.url, "the circuit you were last in reopens")
        }
        if let chosen {
            var opened = false
            timed("opening \(chosen.deletingLastPathComponent().lastPathComponent)") {
                if let data = try? Data(contentsOf: chosen), let doc = try? CoreDocument(data: data) {
                    opened = true
                    _ = doc.loadNotices
                    _ = doc.saveText()
                }
            }
            if ["corruptlast", "emptylast"].contains(state) {
                print("INFO \(state): the damaged circuit \(opened ? "opened (with what could be read)" : "was refused, so a new circuit opens instead")")
            } else {
                check(opened, "it opens")
            }
        }

        // Your Circuits: the list and what each row reads.
        timed("Your Circuits lists its circuits") {
            let items = Library.items()
            for item in items {
                _ = GateCounter.line(item.circuit, "x")
                _ = Library.versions(of: item)
                _ = Library.displayName(for: item.circuit)
            }
            let expected = ["first": 0, "nolibrary": 0, "missinglast": 2, "brokenlibrary": 5, "trash": 2][state] ?? 3
            check(items.count == expected, "Your Circuits shows \(items.count) circuit(s) (expected \(expected))")
        }
        // The launch's clean-up (on a background queue there, here directly).
        timed("the repeated-versions clean-up") { Library.removeRepeats() }
        // A save, as autosave makes: the first one keeps a version; after
        // an hour's break the work before it is kept as a version too.
        if let item = Library.items().first(where: { Library.item(for: $0.circuit) != nil && $0.id != "damaged" }) {
            let before = Library.versions(of: item).count
            timed("noting a save") { Library.noteSaved(item.circuit) }
            let after = Library.versions(of: item).count
            if state == "unsaved", item.circuit == LastCircuit.url {
                check(after == before + 1, "the work from before the break became a version (\(before) to \(after))")
            } else {
                check(after >= max(before, 1), "versions kept (\(before) to \(after))")
            }
        }

        // Sync and Classroom start (sync isn't set up; Classroom's service
        // can't be reached): neither may hold the launch up.
        timed("Sync starts") { SyncCenter.shared.start() }
        timed("Classroom starts") { ClassroomCenter.shared.start() }
        actionsChecks(state, check: check, turn: turn)
        if state == "offline" { offlineChecks(check: check, timed: timed, turn: turn) }
        if state == "trash" { trashChecks(check: check, timed: timed) }
        turn(2.5)
        check(NSApp?.modalWindow == nil, "no question was left up")

        guard state == "damaged" else {
            print(failures == 0 ? "launch-selftest \(state): all passed" : "launch-selftest \(state): \(failures) failed")
            return failures == 0
        }
        // Damaged circuits: cut short and with bytes changed, as a disk or a
        // download can leave them. Each opens or is refused. Each is written
        // to damaged-current.cdl in the home folder first, so one that
        // crashes the app is left there to look at.
        let current = URL(fileURLWithPath: ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] ?? NSTemporaryDirectory())
            .appendingPathComponent("damaged-current.cdl")
        let repo = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        let samples = ["res/samples/practice.cdl", "format/tests/fixtures/Lab5.cdl", "format/tests/fixtures/lab6.cdl",
                       "format/tests/fixtures/drawing/cdl/base-half-adder.cdl", "format/tests/fixtures/pagelinks/three-pages.cdl"]
        var seed = (UInt64(ProcessInfo.processInfo.environment["CL_DAMAGE_ROUND"] ?? "") ?? 1) &* 0x9E3779B97F4A7C15 | 1
        func next() -> Int { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return Int(seed % 1_000_000_007) }
        var tried = 0, opened = 0
        for s in samples {
            guard let data = try? Data(contentsOf: repo.appendingPathComponent(s)), data.count > 10 else { continue }
            for k in 0..<24 {
                var bytes = [UInt8](data)
                if k < 12 {
                    bytes = Array(bytes.prefix(next() % bytes.count))
                } else {
                    for _ in 0..<(1 + next() % 6) { bytes[next() % bytes.count] = UInt8(truncatingIfNeeded: next()) }
                }
                tried += 1
                try? Data(bytes).write(to: current)
                if let doc = try? CoreDocument(data: Data(bytes)) { opened += 1; _ = doc.saveText() }
            }
        }
        try? FileManager.default.removeItem(at: current)
        check(tried > 0, "\(tried) damaged circuits opened (\(opened)) or refused (\(tried - opened)), none crashed")
        print(failures == 0 ? "launch-selftest \(state): all passed" : "launch-selftest \(state): \(failures) failed")
        return failures == 0
    }
}
