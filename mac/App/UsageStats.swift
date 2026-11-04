// Usage stats in the app (UsageReport.swift is the engine; docs/PRIVACY.md
// says what's sent): the switch, the counting -- launches, time the app is
// in front and the Mac isn't idle, time in focus mode and Simulation View,
// Classroom opened, the commands run -- and the send at launch, in the
// background, once a day at most. Nothing is counted while the switch is
// off or a lab's policy (DisableUsageStats) turns it off.
//
// Commands are counted where they're dispatched: ActionID.run (the Actions
// panel and the menus that call it), CanvasController.perform (keys, the
// toolbar, the menus with shortcuts), and the menu bar itself for the items
// that call neither (NSMenu's willSendAction, matched by title to the action
// registry -- never the title itself, so a file name in Open Recent or the
// Window menu can't be counted). One command counts once however many of
// those it passes through (`counting`).

import AppKit
import Combine

@MainActor
final class UsageStats: ObservableObject {
    static let shared = UsageStats()
    let engine: UsageEngine
    /// The switch, as Settings and the consent show it.
    @Published private(set) var on = false
    @Published private(set) var lastSent: String = ""
    @Published private(set) var kept = 0

    static var directory: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("CedarLogic/Usage", isDirectory: true)
    }

    private var timer: Timer?
    private var ticks = 0
    private var started = false
    private var depth = 0
    private var menuHeld = false
    private var observers: [Any] = []
    private static let tick = 5

    private init() {
        engine = UsageEngine(directory: Self.directory)
        engine.syncOn = { SyncCenter.shared.enabled }
        refresh()
    }

    var policyOff: Bool { engine.policyOff }

    func refresh() {
        on = engine.switchedOn && engine.id != nil
        kept = engine.keptCount
        if let at = engine.lastSentAt {
            let f = DateFormatter()
            f.dateStyle = .medium
            f.timeStyle = .short
            lastSent = "\(f.string(from: at)) (\(engine.lastSentKind ?? "report"))"
        } else {
            lastSent = ""
        }
    }

    /// The switch (the consent, Settings > Privacy). Off tells the server to
    /// delete what it has, in the background.
    func set(_ value: Bool) {
        on = value && !policyOff
        Task {
            await engine.set(on: value)
            // This launch counts, from the moment they said yes.
            if value && started { engine.noteSession() }
            refresh()
            if value { await engine.sendIfDue(); refresh() }
        }
    }

    /// Settings > Privacy > Delete what's been sent. True: done; false: the
    /// server couldn't be reached, and it's asked again at the next chance.
    func deleteSent() async -> Bool {
        let done = await engine.deleteSent()
        refresh()
        return done
    }

    /// "See exactly what's sent".
    func preview() -> String { engine.preview() }

    // MARK: Launch

    /// Once the first window is up, in real launches only: the day's report
    /// if one's due, then this launch counted; a look every 5 seconds at
    /// what's in front, the file written every 5 minutes and at quit.
    func start() {
        guard !started else { return }
        started = true
        Task {
            await engine.sendIfDue()
            engine.noteSession()
            engine.save()
            refresh()
        }
        let t = Timer(timeInterval: TimeInterval(Self.tick), repeats: true) { _ in
            MainActor.assumeIsolated { UsageStats.shared.sample() }
        }
        t.tolerance = 1
        RunLoop.main.add(t, forMode: .common)
        timer = t
        let nc = NotificationCenter.default
        observers.append(nc.addObserver(forName: NSApplication.willTerminateNotification, object: nil, queue: .main) { _ in
            MainActor.assumeIsolated { UsageStats.shared.engine.save() }
        })
        observers.append(nc.addObserver(forName: NSMenu.willSendActionNotification, object: nil, queue: nil) { n in
            let title = (n.userInfo?["MenuItem"] as? NSMenuItem)?.title
            MainActor.assumeIsolated { UsageStats.shared.menuWillSend(title) }
        })
        observers.append(nc.addObserver(forName: NSMenu.didSendActionNotification, object: nil, queue: nil) { _ in
            MainActor.assumeIsolated { UsageStats.shared.menuDidSend() }
        })
    }

    /// Every 5 seconds: in front and touched in the last 5 minutes is time
    /// used; with a circuit window in front, in focus mode or Simulation View, that too.
    private func sample() {
        ticks += 1
        if engine.enabled, NSApp.isActive, Self.idleSeconds() < 300 {
            var focus = 0, sim = 0
            if let c = CanvasController.front, let w = c.view?.window, w.isKeyWindow || w.isMainWindow {
                if c.focusModeOn { focus = Self.tick }
                if c.simView { sim = Self.tick }
            }
            engine.addTime(active: Self.tick, focus: focus, simView: sim)
        }
        if ticks % 60 == 0 {
            engine.save()
            Task { await engine.sendIfDue(); refresh() }
        }
    }

    /// Seconds since the last key, click or move anywhere (no permission needed).
    static func idleSeconds() -> Double {
        let any = CGEventType(rawValue: ~0) ?? .mouseMoved
        return CGEventSource.secondsSinceLastEventType(.combinedSessionState, eventType: any)
    }

    // MARK: Counting

    func noteClassroom() { engine.noteClassroom() }

    /// A command, counted once even when one dispatch point calls another.
    func counting(_ id: String, _ run: () -> Void) {
        if depth == 0 { engine.count(id) }
        depth += 1
        defer { depth -= 1 }
        run()
    }

    /// The menu bar's items by title: the registry's actions only.
    private static let menuIds: [String: String] = {
        var m: [String: String] = [:]
        for a in ActionRegistry.all where m[a.title] == nil { m[a.title] = a.id.rawValue }
        return m
    }()

    private func menuWillSend(_ title: String?) {
        guard engine.enabled, !menuHeld, let title, let id = Self.menuIds[title] else { return }
        engine.count(id)
        depth += 1
        menuHeld = true
        // Should didSendAction never come, the hold still goes.
        DispatchQueue.main.async { MainActor.assumeIsolated { UsageStats.shared.menuDidSend() } }
    }

    private func menuDidSend() {
        guard menuHeld else { return }
        menuHeld = false
        depth -= 1
    }
}
