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
// (as normal, with Sparkle's last update check a month ago).
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
// crashing. PASS or FAIL for each; exit 1 on a failure.

import AppKit

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
            let expected = ["first": 0, "nolibrary": 0, "missinglast": 2, "brokenlibrary": 5][state] ?? 3
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
        if state == "offline" { offlineChecks(check: check, timed: timed, turn: turn) }
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
