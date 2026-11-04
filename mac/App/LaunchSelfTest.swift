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
// normal, with the Actions rail on screen (before 0.4's migration), folded,
// hover-to-open off and its items reordered last time), migrated (as
// normal, with the actions In the toolbar, as 0.3.x could), paneloff (as
// normal, migrated, then the panel turned off), owner (the owner's own
// settings: the first 0.4 preview's migration done, the panel folded),
// privacy (as normal, from before the one consent: crash reports said yes
// to, usage stats on, a name set, and the privacy question not yet asked).
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
// in every state, the intro owed and its sound (the first launch's and an
// update's only), "Play the intro every time CedarLogic
// opens" (off unless chosen; on, a later launch plays it, silently) and Help > Play
// the Intro in the menu bar; the Actions panel (on for a first launch and
// for every install before, the old toolbar and hidden modes turned on by
// 0.4's migration, once, and a choice made after it kept), and in `actions` that a folded rail stays
// folded, a drag on the rail reorders the Customize list too, focus mode
// folds it (not hides it) and gives back what it was, the Customize
// list's search finds by name, menu and shortcut, the folded rail's hot
// strip (a pointer in it opens it, passing through doesn't) and View >
// Show/Hide Actions (turning a panel that's off on), and the panel and
// its tab (slid out on hover too) laid out in real windows off screen; and
// Show Me Around there: its places found, the keys stepping it, the panel's
// stop passed over when it's off, Escape ending it (actionsChecks); and
// the one consent for crash reports and usage stats (privacyChecks): the
// welcome's page, Settings > Privacy, who is asked (a first launch on the
// welcome, an update by the sheet, once), an earlier yes to crash reports
// kept, the old crash reports question gone, and the leak scan of a real
// report against this Mac's names, recent files, sync and classroom codes.
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
    /// The order the owner's settings had (build 1702's ActionID list, without Play the Intro or Show Me Around).
    static let ownerOrder = ["checkCircuit", "predict", "tidy", "shareLink", "exportImage", "exportReport", "newCircuit", "openLibrary",
        "recentlyDeleted", "importFile", "newFromTemplate", "practice", "saveAsTemplate", "classroom", "createClassroom", "joinClass",
        "teacherKey", "handIn", "newTab", "closeTab", "reopenTab", "splitView", "closeSplit", "switchPane", "connectAllPages",
        "disconnectAllPages", "save", "exportFile", "versionHistory", "exportV2", "exportV1", "print", "undo", "redo", "cut", "copy",
        "paste", "duplicate", "delete", "selectAll", "addGate", "rotate", "lockInPlace", "unlockAllParts", "find", "findNext",
        "findPrevious", "saveAsPart", "buildFormula", "straighten", "tidyOtherWay", "zoomIn", "zoomOut", "zoomFit", "zoomActual",
        "focusMode", "gridlines", "wireDots", "darkMode", "truthTable", "simView", "projector", "scope", "draw", "clearDrawing",
        "showDrawing", "notes", "present", "makeEditable", "pause", "step", "stepClock", "reveal", "lock", "help", "feedback",
        "shortcuts", "whatsNew", "welcome", "setUp", "tour", "settings", "checkForUpdates"]

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
            d.set(state == "offline" || state == "privacy", forKey: "cl.crashReports")
            // Asked already, but in an update from before the one consent (and its own state).
            if !["updated", "privacy"].contains(state) { d.set(true, forKey: PrivacyConsent.askedKey) }
        }
        if state == "privacy" {
            d.set(true, forKey: UsageEngine.onKey)
            d.set(UsageEngine.newId(), forKey: UsageEngine.idKey)
            d.set(NSFullUserName(), forKey: "cl.studentName")
            d.set(false, forKey: "cl.showGrid")
        }
        if state == "offline" {
            // Sparkle's own record: launched before, last looked a long time
            // ago, so its schedule looks again straight away.
            d.set(true, forKey: "SUHasLaunchedBefore")
            d.set(true, forKey: "SUEnableAutomaticChecks")
            d.set(Date(timeIntervalSinceNow: -30 * 86400), forKey: "SULastCheckTime")
        }
        if state == "actions" {
            // Someone on 0.4 (both of its migrations done) who folded the
            // panel, turned Open on hover off and put its items in another order.
            d.set(true, forKey: Prefs.actionsMigrationKey)
            d.set(true, forKey: Prefs.actionsOpenedKey)
            d.set(true, forKey: "cl.actions.on")
            d.set(true, forKey: "cl.actions.collapsed")
            d.set(false, forKey: "cl.actions.hoverOpen")
            d.set(ActionID.defaults.reversed().map(\.rawValue), forKey: "cl.actions.order")
            d.set(ActionID.defaults.map(\.rawValue), forKey: "cl.actions.shown")
        }
        if state == "migrated" {
            // Had the actions In the toolbar (and the rail folded once), and the status line still due.
            d.set(1, forKey: "cl.actions.mode")
            d.set(true, forKey: "cl.actions.collapsed")
            d.set(true, forKey: "cl.actions.hint")
        }
        if state == "paneloff" {
            // Got 0.4's migrations, then turned the panel off.
            d.set(true, forKey: Prefs.actionsMigrationKey)
            d.set(true, forKey: Prefs.actionsOpenedKey)
            d.set(false, forKey: "cl.actions.on")
        }
        if state == "owner" {
            // The owner's own settings as he found the panel hard to find:
            // build 1702's rail folded, then the first 0.4 preview (1703)
            // turned it on but kept the fold. Read from his defaults, 2026-11-04.
            d.set(true, forKey: Prefs.actionsMigrationKey)
            d.set(true, forKey: "cl.actions.on")
            d.set(true, forKey: "cl.actions.collapsed")
            d.set(Self.ownerOrder, forKey: "cl.actions.order")
            d.set(["checkCircuit", "exportImage", "exportReport", "predict", "shareLink", "tidy"], forKey: "cl.actions.shown")
            d.set(783, forKey: "cl.toolbarHidden")
            d.set(3, forKey: "cl.toolbarStyle")
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

    // MARK: privacy

    /// The one consent (Privacy.swift) and the usage report (UsageReport.swift).
    static func privacyChecks(_ state: String, check: (Bool, String) -> Void, turn: (Double) -> Void) {
        check(WelcomeView.pages == 7 && WelcomeView.privacyPage == 5 && WelcomeView.privacyPage < WelcomeView.pages - 1,
              "the welcome has its \u{201C}A small favour\u{201D} page, before the last")
        check(SettingsPage.allCases.contains(.privacy) && SettingsPage.privacy.title == "Privacy", "Settings has a Privacy page")
        let page = NSHostingController(rootView: SettingsPageView(page: .privacy))
        let size = page.sizeThatFits(in: NSSize(width: 640, height: 10_000))
        check(size.height > 150, "...which lays out (\(Int(size.height)) points tall)")
        // Who's asked, and how.
        let owedNow = PrivacyConsent.owed
        check(owedNow == ["updated", "privacy"].contains(state),
              "the privacy question \(owedNow ? "is owed: a sheet over the circuit" : state == "first" ? "is on the welcome" : "was answered before")")
        if state == "first" { check(!PrivacyConsent.asked && !UsageStats.shared.on, "a first launch: not asked yet, nothing on") }
        if state == "offline" || state == "privacy" { check(CrashReports.allowed == true, "an earlier yes to crash reports is kept") }
        // The old crash reports question, gone: nothing asks by itself.
        turn(0.3)
        @MainActor func says(_ v: NSView, _ text: String) -> Bool {
            if let f = v as? NSTextField, f.stringValue.contains(text) { return true }
            return v.subviews.contains { says($0, text) }
        }
        let old = NSApp.windows.contains { w in w.contentView.map { says($0, "Send crash reports?") } ?? false }
        check(!old, "crash reports' own question no longer shows (one consent asks for both)")
        // The leak scan, on the report this copy would send now.
        let engine = UsageStats.shared.engine
        let report = [UsageStats.shared.preview(),
                      String(decoding: engine.encoded(engine.nextReport(), id: engine.id ?? "x", sentAt: Date()), as: UTF8.self)]
        var needles = UsageLeakScan.machineNeedles()
        needles += NSDocumentController.shared.recentDocumentURLs.map { $0.deletingPathExtension().lastPathComponent }
        needles += (UserDefaults.standard.stringArray(forKey: recentsKey) ?? []).map { ($0 as NSString).lastPathComponent }
        needles += Library.items().map(\.name)
        needles += [Prefs.shared.studentName, NSHomeDirectory(), Library.root.path]
        let sync = SyncCenter.shared
        needles += [sync.code, sync.deviceName] + sync.devices.map(\.name)
        for c in ClassroomCenter.shared.classes { needles += [c.name, c.joinCode, c.teacherKey, c.studentName, c.id] }
        let leaks = report.flatMap { UsageLeakScan.leaks(in: $0, needles: needles) }
        check(leaks.isEmpty, "the usage report holds none of this Mac's names, paths, recent files or codes (\(needles.filter { !$0.isEmpty }.count) looked for)\(leaks.isEmpty ? "" : ": \(leaks)")")
        check(report.allSatisfy { $0.utf8.count <= UsageEngine.maxBytes }, "...and it's 16 KB at most")
        // Answered once, it isn't asked again; No thanks keeps an earlier yes to crash reports.
        if owedNow {
            let crashBefore = CrashReports.allowed
            PrivacyConsent.answer(.no)
            check(!PrivacyConsent.owed && PrivacyConsent.asked && PrivacyConsent.choice == .no, "answered (No thanks), it's never asked again")
            check(CrashReports.allowed == (crashBefore ?? false), "...crash reports as they were (\(crashBefore.map { $0 ? "yes" : "no" } ?? "unasked"))")
            turn(0.5)   // the switch goes off in the background (it tells the server)
            check(!UsageStats.shared.on && UsageStats.shared.engine.id == nil, "...usage stats off, the id forgotten")
        }
    }

    // MARK: actions

    /// The Actions panel (Actions.swift): on for everyone after 0.4's
    /// migration -- a first launch, an install that never chose (it started
    /// Hidden before), one that had it In the toolbar or Hidden ("migrated")
    /// -- and, once migrated, the choice sticks ("paneloff"); in `actions`, a
    /// panel folded and reordered last time is still so, and laid out in a
    /// real window (off screen, never shown) it takes its width, or its
    /// tab's, from the canvas; turned off, it's gone and the toolbar has
    /// none of its buttons; Show Me Around finds its places in that window,
    /// steps with the keys, passes over the panel when it's off, and Escape
    /// ends it.
    static func actionsChecks(_ state: String, check: (Bool, String) -> Void, turn: (Double) -> Void) {
        let prefs = Prefs.shared
        let d = UserDefaults.standard
        check(d.bool(forKey: Prefs.actionsMigrationKey) && d.bool(forKey: Prefs.actionsOpenedKey),
              "0.4's Actions panel migrations have run (\(Prefs.actionsMigrationKey), \(Prefs.actionsOpenedKey))")
        check(!ShowMeAround.seen, "Show Me Around hasn't been seen: it starts by itself once a circuit window has the keys")
        check(d.object(forKey: "cl.actions.mode") == nil && d.object(forKey: "cl.actions.hint") == nil,
              "and the old On screen / In the toolbar / Hidden setting and its status line are gone")
        switch state {
        case "first":
            check(prefs.actionsOn, "a first launch has the Actions panel on")
            check(!prefs.actionsCollapsed && prefs.railActions == ActionID.defaults,
                  "open, with the six default actions (\(prefs.railActions.map(\.rawValue)))")
            check(prefs.actionsHoverOpen, "folded, it opens on hover (the default)")
        case "actions":
            check(prefs.actionsOn, "the panel is on")
            check(prefs.actionsCollapsed, "folded after 0.4's migrations, it's still folded, as it was left")
            check(prefs.railActions == ActionID.defaults.reversed(), "in the order it was given (\(prefs.railActions.map(\.rawValue)))")
            check(!prefs.actionsHoverOpen, "Open on hover still off, as it was set")
            // A drag on the rail: the Customize list has the same order.
            let before = prefs.railActions
            prefs.moveRailAction(before[0], to: 2)
            let after = prefs.railActions
            check(after == [before[1], before[2], before[0]] + before[3...], "dragging the top one down two places on the panel moves it there (\(after.map(\.rawValue)))")
            check(prefs.actionsOrderAll.filter { prefs.actionShown($0) } == after, "and the Customize list has that order too")
            prefs.moveRailAction(before[0], to: 0)
            check(prefs.railActions == before, "and back to the top again")
            prefs.moveRailAction(before[5], to: 99)
            check(prefs.railActions == before, "a drop past the end leaves the last one last")
        case "migrated":
            check(prefs.actionsOn, "a copy that had the actions In the toolbar (or Hidden) has the panel on after the update")
            check(!prefs.actionsCollapsed, "open, not folded")
        case "owner":
            check(prefs.actionsOn && !prefs.actionsCollapsed, "the owner's settings (on, folded by an earlier build): on and open now, so it shows")
            check(prefs.railActions == ActionID.defaults, "with the six he had (\(prefs.railActions.map(\.rawValue)))")
            check(prefs.actionsOrderAll.contains(.showMeAround) && prefs.actionsOrderAll.contains(.playIntro),
                  "and the Customize list has the actions his order didn't know (Show Me Around, Play the Intro)")
            check(prefs.toolbarStyle.rawValue == 3 && prefs.toolbarHidden == 783, "his toolbar is as it was")
        case "paneloff":
            check(!prefs.actionsOn, "turned off after the migration, it stays off: the choice sticks")
        default:
            check(prefs.actionsOn, "someone who had CedarLogic before (and never chose) gets the panel on")
            check(!prefs.actionsCollapsed, "open")
        }
        // The migration itself, on settings of its own: every old setting
        // ends up on, and once run it never turns the panel on again.
        let suite = "cl.selftest.actions"
        if let s = UserDefaults(suiteName: suite) {
            // Builds up to 1702 (cl.actions.mode, or nothing before the rail:
            // 1077), and the first 0.4 preview's (1703: on, maybe folded).
            for (mode, collapsed, preview) in [(nil, false, false), (0, true, false), (0, false, false), (1, true, false), (2, false, false),
                                               (2, true, false), (nil, true, true), (nil, false, true)] as [(Int?, Bool, Bool)] {
                s.removePersistentDomain(forName: suite)
                if let mode { s.set(mode, forKey: "cl.actions.mode"); s.set(true, forKey: "cl.actions.hint") }
                s.set(collapsed, forKey: "cl.actions.collapsed")
                if preview { s.set(true, forKey: Prefs.actionsMigrationKey); s.set(true, forKey: "cl.actions.on") }
                Prefs.migrateActions(s)
                let name = preview ? "the first 0.4 preview's" : mode.map { ["On screen", "In the toolbar", "Hidden"][$0] } ?? "never chosen"
                check(s.bool(forKey: "cl.actions.on") && !s.bool(forKey: "cl.actions.collapsed") && s.object(forKey: "cl.actions.mode") == nil
                      && s.object(forKey: "cl.actions.hint") == nil,
                      "migrating \(name)\(collapsed ? ", folded" : ""): on and open")
                s.set(false, forKey: "cl.actions.on")
                s.set(true, forKey: "cl.actions.collapsed")
                Prefs.migrateActions(s)
                check(!s.bool(forKey: "cl.actions.on") && s.bool(forKey: "cl.actions.collapsed"),
                      "and turned off (and folded) afterwards, a later launch leaves it so")
            }
            s.removePersistentDomain(forName: suite)
        } else {
            check(false, "settings of its own for the migration check")
        }
        check(ActionID.checkForUpdates.action.menu == "CedarLogic" && ActionID.settings.action.menu == "CedarLogic",
              "Check for Updates and Settings are listed under the CedarLogic menu, where the Mac has them")
        check(ActionID.showMeAround.action.menu == "Help" && ActionID.showMeAround.action.title == "Show Me Around",
              "Show Me Around is among the actions, in Help")
        check(ActionID.tour.action.title == "Learn Logic: Guided Tour", "and the logic lesson reads differently: \(ActionID.tour.action.title)")
        check((6...8).contains(ShowMeAround.steps.count) && ShowMeAround.steps.allSatisfy { !$0.title.isEmpty && !$0.body(ShowMeAround.shared).isEmpty },
              "Show Me Around has \(ShowMeAround.steps.count) stops, each with a title and a line")
        check(Set(ShowMeAround.steps.map(\.spot)) == Set(TourSpot.allCases).subtracting([.root]),
              "one for each place: \(ShowMeAround.steps.map(\.spot.rawValue).joined(separator: ", "))")
        guard state == "actions" else { return }

        let doc = CoreDocument()
        _ = doc.addGate("AA_AND2", page: 0, at: .zero)
        /// A whole circuit window laid out off screen (never shown); `body` runs in it.
        func inWindow(width: CGFloat = 1440, _ body: @MainActor (CanvasController, NSWindow) -> Void) {
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
            body(canvas, win)
            win.close()
        }
        /// The canvas's width in a whole circuit window laid out off screen.
        func canvasWidth(on: Bool, folded: Bool, width: CGFloat = 1440) -> CGFloat {
            prefs.actionsOn = on
            prefs.actionsCollapsed = folded
            var w: CGFloat = -1
            inWindow(width: width) { canvas, _ in w = canvas.view?.frame.width ?? -1 }
            print("INFO actions: a \(Int(width))-point window, panel \(on ? "on" : "off")\(folded ? ", folded" : ""): the canvas is \(Int(w)) wide")
            return w
        }
        /// The canvas's width in focus mode, then out of it again (View > Focus Mode's notification).
        func focusWidths(on: Bool, folded: Bool) -> (inFocus: CGFloat, after: CGFloat) {
            prefs.actionsOn = on
            prefs.actionsCollapsed = folded
            var inFocus: CGFloat = -1, after: CGFloat = -1
            inWindow { canvas, _ in
                NotificationCenter.default.post(name: .clToggleFocusMode, object: canvas)
                turn(1.0)
                inFocus = canvas.view?.frame.width ?? -1
                NotificationCenter.default.post(name: .clToggleFocusMode, object: canvas)
                turn(1.0)
                after = canvas.view?.frame.width ?? -1
            }
            print("INFO actions: focus mode, panel \(on ? "on" : "off")\(folded ? ", folded" : ""): the canvas is \(Int(inFocus)) wide in it, \(Int(after)) after")
            return (inFocus, after)
        }
        let none = canvasWidth(on: false, folded: false)
        let open = canvasWidth(on: true, folded: false)
        let folded = canvasWidth(on: true, folded: true)
        ActionsRail.pictureSlidOut = true
        let slid = canvasWidth(on: true, folded: true)
        ActionsRail.pictureSlidOut = false
        check(none > 0 && abs((none - open) - ActionsRail.width) < 2, "open, the panel takes its \(Int(ActionsRail.width)) points (\(none) to \(open))")
        check(abs((none - folded) - ActionsRail.tabWidth) < 2, "folded, just its tab's \(Int(ActionsRail.tabWidth)) (\(folded))")
        check(abs(slid - folded) < 1, "slid out on hover, it's over the canvas, not pushing it (\(slid))")
        let focusNone = focusWidths(on: false, folded: false)
        let focusOpen = focusWidths(on: true, folded: false)
        check(focusOpen.inFocus > open && abs((focusNone.inFocus - focusOpen.inFocus) - ActionsRail.tabWidth) < 2,
              "in focus mode an open panel folds to its tab, not gone (\(focusNone.inFocus) to \(focusOpen.inFocus))")
        check(abs(focusOpen.after - open) < 1 && !prefs.actionsCollapsed, "out of focus mode it's open again, as it was (\(focusOpen.after))")
        let focusFolded = focusWidths(on: true, folded: true)
        check(abs(focusFolded.inFocus - focusOpen.inFocus) < 1 && abs(focusFolded.after - folded) < 1 && prefs.actionsCollapsed,
              "a folded panel stays folded through focus mode and after (\(focusFolded.after))")

        // Folded, with Open on hover: the edge's hot strip, a pointer in it
        // (as its tracking area hands it on), and View > Show/Hide Actions.
        prefs.actionsHoverOpen = true
        prefs.actionsOn = true
        prefs.actionsCollapsed = true
        inWindow { canvas, _ in
            let size = NSSize(width: 1440, height: 800)
            func strip(_ v: NSView) -> EdgeHotStrip.StripView? {
                if let s = v as? EdgeHotStrip.StripView { return s }
                for c in v.subviews { if let s = strip(c) { return s } }
                return nil
            }
            if let host = canvas.view?.window?.contentView, let st = strip(host), let cv = canvas.view {
                let r = st.convert(st.bounds, to: nil), c = cv.convert(cv.bounds, to: nil)
                check(abs(r.width - ActionsRail.hotWidth) < 1 && abs(r.maxX - size.width) < 1,
                      "folded, the hot strip is \(Int(ActionsRail.hotWidth)) points at the window's right edge (\(r))")
                check(r.height >= c.height - 1, "and the canvas's whole height (\(Int(r.height)) for \(Int(c.height)))")
                // Off the tab (which is the last 6 points), still in the strip.
                let offTab = NSPoint(x: r.maxX - ActionsRail.tabWidth - 3, y: r.midY)
                let away = NSPoint(x: r.minX - 200, y: r.midY)
                st.pointer(at: offTab)
                turn(0.35)
                check(canvas.actionsRailShown, "a pointer in the strip, off the tab, slides the panel out")
                st.pointer(at: away)
                turn(0.2)
                check(canvas.actionsRailShown, "it's still out just after the pointer leaves (the grace)")
                turn(0.6)
                check(!canvas.actionsRailShown, "and back in after it")
                st.pointer(at: offTab)
                turn(0.04)
                st.pointer(at: away)
                turn(0.4)
                check(!canvas.actionsRailShown, "a pointer passing through the strip doesn't open it")
                st.pointer(at: NSPoint(x: r.maxX - 0.5, y: r.minY + 20))
                turn(0.35)
                check(canvas.actionsRailShown, "the last column of pixels (the screen's edge) opens it, low down too")
                st.pointer(at: away)
                turn(0.8)
            } else {
                check(false, "the folded panel has its hot strip")
            }
            let foldedWidth = canvas.view?.frame.width ?? -1
            canvas.perform(.toggleActions)
            turn(0.8)
            let openWidth = canvas.view?.frame.width ?? -1
            check(canvas.actionsRailShown && !prefs.actionsCollapsed && abs((foldedWidth - openWidth) - (ActionsRail.width - ActionsRail.tabWidth)) < 2,
                  "View > Show Actions (\(ShortcutStore.shared.combo(.toggleActions)?.label ?? "no key")) opens it (\(foldedWidth) to \(openWidth))")
            canvas.perform(.toggleActions)
            turn(0.8)
            check(!canvas.actionsRailShown && prefs.actionsCollapsed, "and Hide Actions folds it again")
            prefs.actionsOn = false
            turn(0.5)
            check(!canvas.actionsRailShown, "turned off, it's gone (View says Show Actions)")
            canvas.perform(.toggleActions)
            turn(0.8)
            check(prefs.actionsOn && canvas.actionsRailShown, "with the panel off, Show Actions turns it on, open")
        }
        prefs.actionsHoverOpen = false

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
        let narrowNone = canvasWidth(on: false, folded: false, width: 860)
        let narrowOpen = canvasWidth(on: true, folded: false, width: 860)
        check(abs((narrowNone - narrowOpen) - ActionsRail.tabWidth) < 2, "in a window under \(Int(ActionsRail.narrowWidth)) points it folds by itself (\(narrowNone) to \(narrowOpen))")

        /// The toolbar's buttons that are actions, by their tips (real views: BarClickArea).
        func toolbarActionButtons(width: CGFloat) -> Int {
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
            // Those of the panel's that aren't the toolbar's own tools.
            let tips = Set(prefs.railActions.filter { ![.simView, .pause, .step, .newTab, .feedback, .lock].contains($0) }.map { $0.action.titleWithKey })
            var n = 0
            func walk(_ v: NSView) {
                if let c = v as? BarControl, let t = c.tip, tips.contains(t) { n += 1 }
                v.subviews.forEach(walk)
            }
            walk(host)
            win.close()
            return n
        }
        prefs.actionsOn = false
        check(toolbarActionButtons(width: 1280) == 0, "with the panel off, the toolbar doesn't take its buttons (there's no toolbar mode)")
        prefs.actionsOn = true
        check(toolbarActionButtons(width: 1280) == 0, "nor with it on")

        // Show Me Around, in a real window (off screen): its places, the keys, and Escape.
        let around = ShowMeAround.shared
        for on in [true, false] {
            prefs.actionsOn = on
            prefs.actionsCollapsed = false
            inWindow { canvas, win in
                around.start(on: canvas, settle: 0, bringForward: false)
                turn(0.5)
                let spots = around.shown.map { ShowMeAround.steps[$0].spot }
                print("INFO Show Me Around, panel \(on ? "on" : "off"): \(spots.map(\.rawValue)) at \(around.frames.mapValues { "\(Int($0.minX)),\(Int($0.minY)) \(Int($0.width))x\(Int($0.height))" })")
                check(around.active && around.target === canvas, "Show Me Around starts on the window (panel \(on ? "on" : "off"))")
                if on {
                    check(spots == ShowMeAround.steps.map(\.spot), "it finds every place in a 1440-point window (\(spots.map(\.rawValue)))")
                    if let p = around.frames[.palette], let c = around.frames[.canvas], let a = around.frames[.actions],
                       let t = around.frames[.toolbar], let tabs = around.frames[.tabs] {
                        check(p.maxX <= c.minX + 2 && c.maxX <= a.minX + 2 && t.maxY <= tabs.minY + 2 && tabs.maxY <= c.minY + 2,
                              "where they are: palette, canvas, panel left to right; toolbar, tabs, canvas top to bottom")
                        check(abs(a.width - ActionsRail.width) < 2 && abs(a.maxX - win.frame.width) < 2, "the panel's place is the panel (\(a))")
                    }
                } else {
                    check(!spots.contains(.actions) && spots.count == ShowMeAround.steps.count - 1, "with the panel off, its stop is passed over (\(spots.count) stops)")
                }
                let first = around.step
                check(first == around.shown.first, "at the first stop (\(ShowMeAround.steps[first].spot.rawValue))")
                check(around.key(124, plain: true) && around.step == around.shown.dropFirst().first, "→ goes to the next stop")
                check(around.key(123, plain: true) && around.step == first, "← back")
                check(!around.isLast && around.isFirst, "first, not last")
                var n = 1
                while around.active && n < 20 { _ = around.key(36, plain: true); n += 1 }
                check(!around.active && n == around.shown.count + 1, "Return goes through all \(around.shown.count) and Done ends it")
                around.start(on: canvas, settle: 0, bringForward: false)
                turn(0.3)
                check(around.key(0, plain: true), "a plain key (A) doesn't reach the canvas meanwhile")
                check(!around.key(0, plain: false) || !around.active, "a menu key does")
                check(around.key(53, plain: true) && !around.active, "Escape ends it")
                check(!around.key(53, plain: true), "and after that Escape is the window's again")
            }
        }
        around.stop()

        // Starting by itself: after the welcome, whichever way it closes
        // (the lesson after it when that was the choice); never once seen or skipped.
        let wasSeen = ShowMeAround.seen
        for exit in ShowMeAround.WelcomeExit.allCases {
            ShowMeAround.seen = false
            around.then = nil
            around.stopWatching()
            let first = around.welcomeClosed(exit)
            check(first && around.watching && (around.then != nil) == (exit == .guidedTour),
                  "the welcome closed (\(exit)): Show Me Around starts once the window has the keys\(exit == .guidedTour ? ", the lesson after it" : "")")
        }
        around.stopWatching()
        around.then = nil
        prefs.actionsOn = true
        prefs.actionsCollapsed = true
        inWindow { canvas, _ in
            var lesson = false
            ShowMeAround.seen = false
            around.then = { lesson = true }
            around.start(on: canvas, settle: 0, bringForward: false)
            turn(0.5)
            if let i = around.shown.firstIndex(where: { ShowMeAround.steps[$0].spot == .actions }) {
                var k = 0
                while around.step != around.shown[i] && k < 10 { _ = around.key(124, plain: true); turn(0.15); k += 1 }
                turn(0.6)
                check(canvas.actionsRailShown && prefs.actionsCollapsed && (around.frames[.actions]?.width ?? 0) > ActionsRail.width - 2,
                      "a folded panel opens for its stop (\(Int(around.frames[.actions]?.width ?? 0)) wide), its setting still folded")
                _ = around.key(124, plain: true)
                turn(0.6)
                check(!canvas.actionsRailShown && prefs.actionsCollapsed, "and folds back after it")
            } else {
                check(false, "the folded panel has its stop")
            }
            _ = around.key(53, plain: true)
            turn(0.5)
            check(ShowMeAround.seen && !around.active, "Escape (Skip) ends it, and it counts as seen")
            check(lesson, "the lesson chosen from the welcome starts after it")
            around.startWhenReady()
            check(!around.watching, "seen, it never starts by itself again")
            check(!around.welcomeClosed(.guidedTour) && around.then == nil, "and the welcome's lesson then starts straight away")
            ShowMeAround.seen = false
            around.start(on: canvas, settle: 0, bringForward: false)
            turn(0.3)
            var n = 0
            while around.active && n < 20 { _ = around.key(36, plain: true); n += 1 }
            check(ShowMeAround.seen, "going through to Done counts as seen too")
        }
        ShowMeAround.seen = wasSeen
        prefs.actionsOn = true
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
        // The intro (Intro.swift): owed on the first launch and after the
        // update, never played in a test run like this one.
        let owed = Intro.owed()
        check(owed == (state == "first" ? .firstLaunch : state == "updated" ? .update : nil),
              "the intro owed: \(owed?.rawValue ?? "none")")
        // The sound: the first launch's intro and an update's, never any other.
        if let owed { check(owed.hasSound, "the \(owed.rawValue) intro has the sound") }
        check(Intro.pending() == nil, "no intro plays in a self-test")
        // Settings > General > "Play the intro every time CedarLogic opens":
        // off unless chosen; on, a launch that's owed nothing plays the
        // first launch's intro (tried on settings of its own).
        check(!Prefs.shared.introEveryLaunch, "the intro doesn't play at every launch unless chosen")
        let suite = "cl.selftest.intro"
        if let s = UserDefaults(suiteName: suite) {
            s.removePersistentDomain(forName: suite)
            for key in ["cl.hasSeenWelcome", "cl.firstLaunchPlayed", Intro.firstKey] { s.set(true, forKey: key) }
            let due: Intro.Trigger? = state == "updated" ? .update : nil
            check(Intro.forLaunch(s) == due, "a later launch plays \(due.map { "the \($0.rawValue) intro" } ?? "no intro") with the setting off")
            s.set(true, forKey: Intro.everyLaunchKey)
            let every = Intro.forLaunch(s)
            check(every == (due ?? .everyLaunch), "with the setting on, a later launch plays the intro (\(every?.rawValue ?? "none"))")
            check(every?.hasSound == (due != nil),
                  due == nil ? "an every-launch intro has no sound" : "an update's intro has the sound, setting or not")
            s.removePersistentDomain(forName: suite)
        } else {
            check(false, "settings of its own for the intro check")
        }
        check(Intro.Trigger.firstLaunch.hasSound && Intro.Trigger.update.hasSound && !Intro.Trigger.everyLaunch.hasSound,
              "sound for the first launch and updates only")
        let play = ActionID.playIntro.action
        check(play.menu == "Help" && play.title == "Play the Intro", "Play the Intro is among the actions, in Help")

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
        privacyChecks(state, check: check, turn: turn)
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

// MARK: - Focus after launch (a real launch)

/// `CL_FOCUS_CHECK=<state>`, in a copy of the app set up by `--launch-selftest
/// setup <state>` (mac/Tools/launch-selftest.sh with FOCUS=1): a real launch,
/// its windows on screen, nothing clicked. Once the launch screen has gone --
/// and, in `first` and `updated`, the welcome or What's New has been closed as
/// its last button closes it -- the circuit window has to be the key window,
/// its canvas has to have the keyboard and the menus have to see the circuit
/// (File > Export as Image… enabled, and its key, typed, asking for the
/// export). The owner's "the top bar is greyed out until I click the grid"
/// was these failing. PASS or FAIL for each, a TRACE
/// line whenever who has the keys changes; exit 1 on a failure.
@MainActor
enum LaunchFocusCheck {
    private static var failures = 0
    private static var lastTrace = ""
    private static let t0 = CACurrentMediaTime()

    static func startIfAsked() {
        guard let state = ProcessInfo.processInfo.environment["CL_FOCUS_CHECK"], !state.isEmpty else { return }
        setvbuf(stdout, nil, _IOLBF, 0)
        guard let id = Bundle.main.bundleIdentifier, id != LaunchSelfTest.realBundle, SessionCheck.isolatedHome() else {
            print("focus-check: only in a copy of the app with a home folder of its own; run FOCUS=1 mac/Tools/launch-selftest.sh")
            exit(2)
        }
        let trace = Timer(timeInterval: 0.05, repeats: true) { _ in MainActor.assumeIsolated { traceKeys() } }
        RunLoop.main.add(trace, forMode: .common)
        Task { @MainActor in
            guard await run(state) else {
                // Nothing to check: macOS kept another app in front (the
                // screen locked, or you typing elsewhere as it launched).
                let locked = (CGSessionCopyCurrentDictionary() as? [String: Any])?["CGSSessionScreenIsLocked"] as? Bool ?? false
                print("SKIP focus-check \(state): CedarLogic wasn't brought to the front (\(locked ? "the screen is locked" : "another app kept it"))")
                exit(3)
            }
            print(failures == 0 ? "focus-check \(state): all passed" : "focus-check \(state): \(failures) failed")
            exit(failures == 0 ? 0 : 1)
        }
    }

    private static func check(_ state: String, _ ok: Bool, _ what: String) {
        print((ok ? "PASS " : "FAIL ") + "\(state): " + what)
        if !ok { failures += 1 }
    }

    private static func wait(_ seconds: Double) async {
        try? await Task.sleep(nanoseconds: UInt64(seconds * 1_000_000_000))
    }

    private static func name(_ w: NSWindow?) -> String {
        guard let w else { return "none" }
        if NSDocumentController.shared.document(for: w) != nil { return "circuit" }
        if let id = w.identifier?.rawValue, !id.isEmpty { return id.components(separatedBy: "-").first ?? id }
        return w.title.isEmpty ? String(describing: type(of: w)) : w.title
    }

    static var circuitWindow: NSWindow? {
        NSApp.windows.first { $0.isVisible && NSDocumentController.shared.document(for: $0) != nil }
    }

    /// A menu bar item by its title, anywhere in the menus.
    static func menuItem(_ title: String) -> NSMenuItem? {
        func find(_ menu: NSMenu) -> NSMenuItem? {
            menu.update()
            for item in menu.items {
                if item.title == title { return item }
                if let sub = item.submenu, let f = find(sub) { return f }
            }
            return nil
        }
        return NSApp.mainMenu.flatMap(find)
    }

    /// A key press of `combo` for window `w`, as the keyboard sends it.
    private static func keyDown(_ combo: KeyCombo, in w: NSWindow) -> NSEvent? {
        var m: NSEvent.ModifierFlags = []
        if combo.mods & KeyCombo.shift != 0 { m.insert(.shift) }
        if combo.mods & KeyCombo.option != 0 { m.insert(.option) }
        if combo.mods & KeyCombo.control != 0 { m.insert(.control) }
        if combo.mods & KeyCombo.command != 0 { m.insert(.command) }
        let codes: [String: UInt16] = ["a": 0, "s": 1, "d": 2, "f": 3, "h": 4, "g": 5, "z": 6, "x": 7, "c": 8, "v": 9, "b": 11,
                                       "q": 12, "w": 13, "e": 14, "r": 15, "y": 16, "t": 17, "o": 31, "u": 32, "i": 34, "p": 35,
                                       "l": 37, "j": 38, "k": 40, "n": 45, "m": 46]
        guard combo.key.count == 1, let code = codes[combo.key] else { return nil }
        let chars = m.contains(.shift) ? combo.key.uppercased() : combo.key
        return NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: m, timestamp: ProcessInfo.processInfo.systemUptime,
                                windowNumber: w.windowNumber, context: nil, characters: chars,
                                charactersIgnoringModifiers: combo.key, isARepeat: false, keyCode: code)
    }

    /// File > Export as Image…, as the menu bar shows it now.
    static func exportEnabled() -> Bool? {
        func find(_ menu: NSMenu) -> NSMenuItem? {
            menu.update()
            for item in menu.items {
                if item.title.hasPrefix("Export as Image") { return item }
                if let sub = item.submenu, let f = find(sub) { return f }
            }
            return nil
        }
        guard let main = NSApp.mainMenu, let item = find(main) else { return nil }
        return item.isEnabled
    }

    private static func traceKeys() {
        let fr = NSApp.keyWindow?.firstResponder.map { String(describing: type(of: $0)) } ?? "none"
        let line = "active=\(NSApp.isActive) key=\(name(NSApp.keyWindow)) main=\(name(NSApp.mainWindow)) firstResponder=\(fr) "
            + "export=\(exportEnabled().map { $0 ? "on" : "off" } ?? "missing") splash=\(Splash.shared.showing)"
        guard line != lastTrace else { return }
        lastTrace = line
        print(String(format: "TRACE %5.2f s ", CACurrentMediaTime() - t0) + line)
    }

    /// A key that isn't a letter (Escape, the arrows), for window `w`.
    private static func rawKey(_ code: UInt16, _ chars: String, in w: NSWindow) -> NSEvent? {
        NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: [], timestamp: ProcessInfo.processInfo.systemUptime,
                         windowNumber: w.windowNumber, context: nil, characters: chars,
                         charactersIgnoringModifiers: chars, isARepeat: false, keyCode: code)
    }

    /// Help > Show Me Around in the menu bar, then the walk on the real
    /// window: on, its stops, → and ← through the event queue, and Escape ending it.
    private static func showMeAround(_ state: String, _ w: NSWindow) async {
        check(state, menuItem("Show Me Around") != nil, "Help > Show Me Around is in the menu bar")
        guard let c = CanvasController.front, c.view?.window === w else { check(state, false, "a front canvas for Show Me Around"); return }
        let around = ShowMeAround.shared
        around.start(on: c)
        await wait(1.0)
        let stops = around.shown.map { ShowMeAround.steps[$0].spot.rawValue }
        check(state, around.active && stops.count >= 6, "Show Me Around is on, with \(stops.count) stops (\(stops.joined(separator: ", ")))")
        let first = around.step
        func send(_ code: UInt16, _ ch: Int) async {
            if let e = rawKey(code, String(Character(UnicodeScalar(ch)!)), in: w) { NSApp.postEvent(e, atStart: false) }
            await wait(0.6)
        }
        await send(124, NSRightArrowFunctionKey)
        check(state, around.active && around.step != first, "→ moves it on (to \(ShowMeAround.steps[around.step].spot.rawValue))")
        await send(123, NSLeftArrowFunctionKey)
        check(state, around.step == first, "← brings it back")
        await send(53, 27)
        check(state, !around.active, "Escape ends it")
        await wait(0.4)
    }

    /// False when the app never came to the front, so there's nothing to check.
    private static func run(_ state: String) async -> Bool {
        // The launch screen plays, then dissolves into the window.
        var waited = 0.0
        while (Splash.shared.showing || circuitWindow == nil) && waited < 20 { await wait(0.1); waited += 0.1 }
        await wait(1.0)
        guard NSApp.isActive else { return false }
        check(state, !Splash.shared.showing, String(format: "the launch screen went (%.1f s)", waited))
        let greetings = NSApp.windows.filter { w in
            w.isVisible && Greeting.ids.contains { w.identifier?.rawValue.contains($0) == true }
        }
        let greeting = greetings.first
        // Only the one owed: none brought back from the last session.
        let expected = ["first": "welcome", "updated": "whatsnew"][state]
        check(state, expected.map { e in greetings.count == 1 && greeting?.identifier?.rawValue.contains(e) == true } ?? greetings.isEmpty,
              "\(expected ?? "no greeting") is up (\(greetings.isEmpty ? "none" : greetings.map(name).joined(separator: ", ")))")
        if let greeting {
            check(state, greeting.isKeyWindow, "it has the keys, over the circuit (key: \(name(NSApp.keyWindow)))")
            // As its last page's button does: it closes.
            greeting.close()
            await wait(1.0)
        }
        check(state, NSApp.isActive, "CedarLogic is the active app")
        let w = circuitWindow
        check(state, w != nil, "a circuit window is open")
        guard let w else { return true }
        // Show Me Around starts by itself the first time: after the welcome
        // closes (as its Just Start does), after What's New, or on any launch
        // before it's been seen. Escape ends it and it counts as seen.
        if state == "first" {
            Prefs.shared.hasSeenWelcome = true
            WhatsNew.markSeen()
            ShowMeAround.shared.welcomeClosed(.blank)
            // As the welcome's finish does when it was closed before "A small favour".
            PrivacyConsent.askWhenReady()
        }
        // The privacy question: once, over the circuit, for anyone not asked
        // (an update; a first launch whose welcome closed before its page).
        if ["first", "updated"].contains(state) {
            var waitedAsk = 0.0
            while !PrivacyConsent.showing && waitedAsk < 8 { await wait(0.1); waitedAsk += 0.1 }
            check(state, PrivacyConsent.showing && w.attachedSheet?.identifier?.rawValue == "cl.privacyAsk",
                  String(format: "the privacy question came up as a sheet on the circuit window (%.1f s)", waitedAsk))
            check(state, !ShowMeAround.shared.active, "Show Me Around waits for it")
            await wait(0.5)
            PrivacyConsent.sheetAnswered(.no, crash: false, usage: false)
            await wait(0.6)
            check(state, !PrivacyConsent.showing && PrivacyConsent.asked && w.attachedSheet == nil, "No thanks put it away, for good")
        } else {
            await wait(1.0)
            check(state, !PrivacyConsent.showing, "no privacy question (answered before)")
        }
        let around = ShowMeAround.shared
        var waitedAround = 0.0
        while !around.active && waitedAround < 8 { await wait(0.1); waitedAround += 0.1 }
        check(state, around.active && around.target?.view?.window === w,
              String(format: "Show Me Around started by itself on the circuit window (%.1f s)", waitedAround))
        if around.active, let e = rawKey(53, "\u{1b}", in: w) {
            NSApp.postEvent(e, atStart: false)
            await wait(0.8)
        }
        check(state, !around.active && ShowMeAround.seen, "Escape ended it, and it won't start by itself again")
        func inFront(_ when: String) async {
            check(state, w.isKeyWindow, "\(when): the circuit window is the key window (key: \(name(NSApp.keyWindow)))")
            check(state, w.isMainWindow, "\(when): and the main window (main: \(name(NSApp.mainWindow)))")
            let fr = w.firstResponder
            check(state, fr is CircuitCanvasNSView, "\(when): its canvas has the keyboard (\(fr.map { String(describing: type(of: $0)) } ?? "none"))")
            check(state, CanvasController.front?.view?.window === w, "\(when): it's the front canvas")
            let export = exportEnabled()
            check(state, export == true, "\(when): File > Export as Image… is enabled (\(export.map { $0 ? "on" : "off" } ?? "missing"))")
            // Its key, typed (through the event queue, as a key press comes):
            // the export sheet is asked for, then put away again.
            let combo = ShortcutStore.shared.combo(.exportImage)
            let key = combo.flatMap { keyDown($0, in: w) }
            check(state, key != nil, "\(when): Export as Image… has a key to type (\(combo?.label ?? "none"))")
            if let c = CanvasController.front, c.view?.window === w, let combo, let e = key {
                c.sheetHost.showExportImage = false
                NSApp.postEvent(e, atStart: false)
                var asked = false
                for _ in 0..<20 where !asked { await wait(0.05); asked = c.sheetHost.showExportImage }
                check(state, asked, "\(when): \(combo.label) (Export as Image…) works, nothing clicked")
                c.sheetHost.showExportImage = false
                await wait(0.6)
            }
        }
        await inFront("after launch")
        check(state, menuItem("Play the Intro") != nil, "Help > Play the Intro is in the menu bar")
        await showMeAround(state, w)
        // Another window in front for a moment, then gone: the circuit's
        // window comes back in front with nothing else changing.
        let other = NSWindow(contentRect: NSRect(x: w.frame.midX - 160, y: w.frame.midY - 90, width: 320, height: 180),
                             styleMask: [.titled, .closable], backing: .buffered, defer: false)
        other.isReleasedWhenClosed = false
        other.title = "Focus check"
        other.makeKeyAndOrderFront(nil)
        await wait(0.6)
        check(state, other.isKeyWindow, "another window in front takes the keys")
        other.close()
        await wait(1.0)
        await inFront("after another window")
        return true
    }
}
