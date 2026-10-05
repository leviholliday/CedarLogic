// Sync on the Mac (SYNC.md): the shared engine (mac/CedarCore/Sync*.cpp,
// through CedarSync.h) with the hooks of SyncHooks.swift, plus everything that
// touches a window -- saving open circuits before a cycle looks at the files,
// telling it which circuits have unsaved edits, reloading a window in place
// when its circuit changed elsewhere, closing one whose circuit was deleted
// elsewhere, the questions, the notes, and quitting. SyncSettingsView.swift
// is Settings > Sync; Your Circuits shows the status line.

import AppKit
import SwiftUI
import SystemConfiguration
import UniformTypeIdentifiers

/// Something handed to the main queue from a thread the compiler can't vouch
/// for (the engine's hooks run there anyway).
private final class Handoff<T>: @unchecked Sendable {
    let value: T
    init(_ value: T) { self.value = value }
}

/// Runs `action` once `tick()` has been called `count` times (at once for 0), from any thread.
private final class Countdown: @unchecked Sendable {
    private let lock = NSLock()
    private var left: Int
    private var action: (() -> Void)?
    init(_ count: Int, _ action: @escaping () -> Void) {
        left = count
        self.action = action
        if count <= 0 { fire() }
    }
    func tick() {
        lock.lock()
        left -= 1
        let now = left == 0
        lock.unlock()
        if now { fire() }
    }
    private func fire() {
        lock.lock()
        let a = action
        action = nil
        lock.unlock()
        a?()
    }
}

/// What a reloaded window looked at: the camera of each page (by index), so
/// the circuit read again (a new CoreDocument, with new page ids) opens where
/// it was. Set just before a revert, taken by the CircuitDocument that the
/// revert reads.
enum ReloadStore {
    private static let lock = NSLock()
    private static var pending: [Int: (CGPoint, CGFloat)]?

    static func set(_ cameras: [Int: (CGPoint, CGFloat)]?) {
        lock.lock(); defer { lock.unlock() }
        pending = cameras
    }

    /// Gives a just-read circuit the cameras waiting for it, if any.
    static func apply(to core: CoreDocument) {
        lock.lock()
        let cams = pending
        pending = nil
        lock.unlock()
        guard let cams else { return }
        for (i, cam) in cams where i < core.pageCount { core.cameras[core.pageID(i)] = cam }
    }
}

@MainActor
final class SyncCenter: ObservableObject {
    static let shared = SyncCenter()

    struct Device: Identifiable, Hashable {
        let name: String
        let lastSync: Date?
        let isThis: Bool
        var id: String { name }
    }

    /// Settings > Sync's sheets.
    enum Sheet: Identifiable, Equatable {
        case code
        case enter
        case confirm(code: String, sentence: String, empty: Bool)
        var id: String {
            switch self {
            case .code: "code"
            case .enter: "enter"
            case .confirm: "confirm"
            }
        }
    }

    // What the engine says (refresh()).
    @Published private(set) var enabled = false
    @Published private(set) var kind = Int(CL_SYNC_OFF)
    @Published private(set) var statusText = ""
    @Published private(set) var circuits = 0
    @Published private(set) var code = ""
    @Published private(set) var deviceName = ""
    @Published private(set) var devices: [Device] = []
    /// Circuits with a problem, by folder id: "Too big to sync (over 512 KB)".
    @Published private(set) var problems: [String: String] = [:]
    /// The latest notice, shown in the sync line for a minute.
    @Published private(set) var recentNotice: String?

    // Settings > Sync's own state.
    @Published var sheet: Sheet?
    /// "Setting up…", "Checking the code…", "Linking…" while one of those runs.
    @Published var working: String?
    @Published var enteredCode = ""
    @Published var fieldError: String?
    /// A sentence under the page's buttons (a failure of Turn On, Delete…).
    @Published var pageMessage: String?

    /// For --render-ui: a model that isn't connected to anything.
    let preview: Bool
    private var engine: OpaquePointer?
    private let platform = SyncPlatform(syncDir: SyncPlatform.defaultSyncDir)
    private var hooks = CLSyncHooks()
    private var started = false
    private(set) var isQuitting = false
    private var quitReplied = false
    private var lastInput: [Int: Int64] = [:]   // window number -> ms since 1970
    private var lastActiveNote = Date.distantPast
    private var watchers: [Any] = []
    private var ticker: Timer?
    private var noticeClear: DispatchWorkItem?

    private init() { preview = false }

    /// A stand-in for screenshots (RenderUI): shows what it's given.
    init(previewEnabled: Bool, status: String = "", kind: Int = Int(CL_SYNC_SYNCED), circuits: Int = 0, code: String = "",
         deviceName: String = "", devices: [Device] = []) {
        preview = true
        enabled = previewEnabled
        statusText = status
        self.kind = kind
        self.circuits = circuits
        self.code = code
        self.deviceName = deviceName
        self.devices = devices
    }

    // MARK: Starting

    /// The engine, made on first use (its state is read in start()).
    private func setUp() {
        guard engine == nil, !preview else { return }
        try? FileManager.default.createDirectory(at: Library.root, withIntermediateDirectories: true)
        platform.makeSyncDir()
        var h = platform.hooks()
        h.flush_open = { _, token in SyncCenter.onMain { $0.flushOpen(token) } }
        h.window_state = { _, folder, open, dirty, last in
            let id = folder.map { String(cString: $0) } ?? ""
            let s = SyncCenter.onMainNow { $0.windowState(id) }
            open?.pointee = s.open
            dirty?.pointee = s.dirty
            last?.pointee = s.last
        }
        h.circuit_replaced = { _, folder, device in
            let id = folder.map { String(cString: $0) } ?? "", from = device.map { String(cString: $0) } ?? ""
            SyncCenter.onMain { $0.reload(id, from: from) }
        }
        h.close_circuit = { _, folder in
            let id = folder.map { String(cString: $0) } ?? ""
            SyncCenter.onMain { $0.close(id) }
        }
        h.library_changed = { _ in
            SyncCenter.onMain { _ in NotificationCenter.default.post(name: .clLibraryChanged, object: nil) }
        }
        h.status_changed = { _ in SyncCenter.onMain { $0.refresh() } }
        h.notice = { _, text in
            let t = text.map { String(cString: $0) } ?? ""
            SyncCenter.onMain { $0.notice(t) }
        }
        h.ask_mass_delete = { _, count, token in SyncCenter.onMain { $0.askMassDelete(Int(count), token) } }
        h.ask_incoming_deletes = { _, count, from, token in
            let devices = from.map { String(cString: $0) } ?? ""
            SyncCenter.onMain { $0.askIncomingDeletes(Int(count), from: devices, token) }
        }
        h.gate_default = cl_sync_core_gate_default
        hooks = h
        let info = Bundle.main.infoDictionary ?? [:]
        let version = info["CFBundleShortVersionString"] as? String ?? "0"
        let build = info["CFBundleVersion"] as? String ?? "0"
        let key = ProcessInfo.processInfo.environment["CL_SYNC_KEY"] ?? info["FeedbackKey"] as? String ?? ""
        engine = cl_sync_create(&h, Library.root.path, platform.syncDir.path, key, "mac/\(version)+\(build)", Self.computerName)
        watch()
    }

    /// Starts syncing if it's on: at launch, a couple of seconds after the first window (4.3).
    func start() {
        guard !preview, !started else { return }
        setUp()
        guard let engine else { return }
        started = true
        cl_sync_start(engine)
        refresh()
    }

    /// "Levi’s MacBook Air" (System Settings > General > Sharing).
    static var computerName: String {
        if let name = SCDynamicStoreCopyComputerName(nil, nil) as String?, !name.isEmpty { return name }
        return Host.current().localizedName ?? "Mac"
    }

    private func watch() {
        let nc = NotificationCenter.default
        watchers.append(nc.addObserver(forName: NSApplication.didBecomeActiveNotification, object: nil, queue: .main) { _ in
            MainActor.assumeIsolated { if let e = SyncCenter.shared.engine { cl_sync_app_activated(e) } }
        })
        watchers.append(nc.addObserver(forName: NSApplication.didResignActiveNotification, object: nil, queue: .main) { _ in
            MainActor.assumeIsolated { if let e = SyncCenter.shared.engine { cl_sync_app_deactivated(e) } }
        })
        watchers.append(nc.addObserver(forName: NSWindow.didBecomeKeyNotification, object: nil, queue: .main) { _ in
            MainActor.assumeIsolated { if let e = SyncCenter.shared.engine { cl_sync_app_activated(e) } }
        })
        // The person's input, per window: a window being used isn't reloaded
        // under them for a switch flip (4.12), and polling keeps going (4.3).
        watchers.append(NSEvent.addLocalMonitorForEvents(matching: [.keyDown, .leftMouseDown, .rightMouseDown, .otherMouseDown, .scrollWheel]) { e in
            MainActor.assumeIsolated { SyncCenter.shared.noteInput(e.window) }
            return e
        } as Any)
        // "Synced 5 min ago" moves on by itself.
        ticker = Timer.scheduledTimer(withTimeInterval: 30, repeats: true) { _ in
            MainActor.assumeIsolated { SyncCenter.shared.refresh() }
        }
    }

    private func noteInput(_ window: NSWindow?) {
        let now = Date()
        if let window { lastInput[window.windowNumber] = Int64(now.timeIntervalSince1970 * 1000) }
        if now.timeIntervalSince(lastActiveNote) > 20, let engine {
            lastActiveNote = now
            cl_sync_user_active(engine)
        }
    }

    /// From a C hook: on the main thread (the engine calls them inside onMain), as the shared center.
    private nonisolated static func onMain(_ fn: @escaping @MainActor (SyncCenter) -> Void) {
        if Thread.isMainThread {
            MainActor.assumeIsolated { fn(SyncCenter.shared) }
        } else {
            let box = Handoff(fn)
            DispatchQueue.main.async { box.value(SyncCenter.shared) }
        }
    }
    private nonisolated static func onMainNow<T>(_ fn: @MainActor (SyncCenter) -> T) -> T {
        if Thread.isMainThread { return MainActor.assumeIsolated { fn(SyncCenter.shared) } }
        return DispatchQueue.main.sync { MainActor.assumeIsolated { fn(SyncCenter.shared) } }
    }

    // MARK: What the engine says

    func refresh() {
        guard let e = engine else { return }
        enabled = cl_sync_enabled(e)
        kind = Int(cl_sync_status_kind(e))
        statusText = String(cString: cl_sync_status_text(e))
        circuits = Int(cl_sync_circuit_count(e))
        code = String(cString: cl_sync_code(e))
        deviceName = String(cString: cl_sync_device_name(e))
        var list: [Device] = []
        for i in 0..<Int(cl_sync_device_count(e)) {
            var last: Int64 = 0
            let name = String(cString: cl_sync_device(e, Int32(i), &last))
            guard !name.isEmpty, !list.contains(where: { $0.name == name }) else { continue }
            list.append(Device(name: name, lastSync: last > 0 ? Date(timeIntervalSince1970: Double(last) / 1000) : nil,
                               isThis: name == deviceName))
        }
        if enabled, !list.contains(where: \.isThis) { list.insert(Device(name: deviceName, lastSync: nil, isThis: true), at: 0) }
        devices = list.sorted { a, b in a.isThis != b.isThis ? a.isThis : (a.lastSync ?? .distantPast) > (b.lastSync ?? .distantPast) }
        var p: [String: String] = [:]
        for i in 0..<Int(cl_sync_problem_count(e)) {
            var folder: UnsafePointer<CChar>?
            let text = String(cString: cl_sync_problem(e, Int32(i), &folder))
            if let folder { p[String(cString: folder)] = text }
        }
        problems = p
    }

    /// One line for Your Circuits: the status, or a notice from the last minute.
    var line: String {
        if let recentNotice { return recentNotice }
        guard enabled else { return statusText }
        if kind == Int(CL_SYNC_SYNCED), circuits > 0 { return "\(statusText) · \(circuits) circuit\(circuits == 1 ? "" : "s")" }
        return statusText
    }

    var symbol: String {
        switch kind {
        case Int(CL_SYNC_SYNCED): "checkmark.circle"
        case Int(CL_SYNC_OFFLINE): "wifi.slash"
        case Int(CL_SYNC_ERROR), Int(CL_SYNC_FULL): "exclamationmark.triangle"
        case Int(CL_SYNC_GONE): "xmark.circle"
        case Int(CL_SYNC_BUSY): "hourglass"
        default: "arrow.triangle.2.circlepath"
        }
    }

    private func notice(_ text: String) {
        guard !text.isEmpty else { return }
        CanvasController.front?.note(text)
        recentNotice = text
        noticeClear?.cancel()
        let clear = DispatchWorkItem { MainActor.assumeIsolated { SyncCenter.shared.recentNotice = nil } }
        noticeClear = clear
        DispatchQueue.main.asyncAfter(deadline: .now() + 60, execute: clear)
    }

    // MARK: Windows (4.12)

    /// The open documents showing a library circuit.
    private func documents(_ folderId: String) -> [NSDocument] {
        NSDocumentController.shared.documents.filter { Library.item(for: $0.fileURL)?.id == folderId }
    }

    /// Saves every open library circuit with unsaved changes; the engine
    /// waits (on its own thread) for the token.
    private func flushOpen(_ token: UnsafeMutableRawPointer?) {
        let docs = NSDocumentController.shared.documents.filter {
            Library.item(for: $0.fileURL) != nil && ($0.isDocumentEdited || $0.hasUnautosavedChanges)
        }
        let count = Countdown(docs.count) { cl_sync_flush_done(token) }
        for d in docs { d.autosave(withImplicitCancellability: false) { _ in count.tick() } }
    }

    private func windowState(_ folderId: String) -> (open: Bool, dirty: Bool, last: Int64) {
        let docs = documents(folderId)
        guard !docs.isEmpty else { return (false, false, 0) }
        let dirty = docs.contains { $0.isDocumentEdited || $0.hasUnautosavedChanges }
        let last = docs.flatMap { $0.windowControllers.compactMap { $0.window?.windowNumber } }
            .compactMap { lastInput[$0] }.max() ?? 0
        return (true, dirty, last)
    }

    /// The circuit changed on another device and its files were written:
    /// every window showing it reads it again, in place -- the same page,
    /// looked at from the same place -- and says so.
    private func reload(_ folderId: String, from device: String) {
        for doc in documents(folderId) {
            guard let url = doc.fileURL, !doc.isDocumentEdited else { continue }
            let views = doc.windowControllers.compactMap { $0.window?.contentView }.flatMap(Self.canvases)
            var cams: [Int: (CGPoint, CGFloat)] = [:]
            if let core = views.first?.document {
                for (id, cam) in core.cameras { if let i = core.pageIndex(of: id) { cams[i] = cam } }
            }
            for v in views where v.bounds.width > 0 { cams[v.page] = (v.visibleCenter, v.unitsPerPoint) }
            ReloadStore.set(cams)
            do {
                try doc.revert(toContentsOf: url, ofType: doc.fileType ?? UTType.cedarLogicCircuit.identifier)
            } catch {
                NSLog("CedarLogic sync: couldn't reload \(folderId): \(error.localizedDescription)")
            }
            ReloadStore.set(nil)
            doc.undoManager?.removeAllActions()
            let controllers = views.compactMap(\.controller)
            DispatchQueue.main.async {
                for c in controllers { c.note(device.isEmpty ? "Updated from another device" : "Updated from \(device)") }
            }
        }
    }

    private static func canvases(_ v: NSView) -> [CircuitCanvasNSView] {
        (v as? CircuitCanvasNSView).map { [$0] } ?? v.subviews.flatMap(canvases)
    }

    /// Deleted on another device: its (clean) windows close without saving.
    private func close(_ folderId: String) {
        for doc in documents(folderId) { doc.close() }
    }

    // MARK: Questions

    private func askMassDelete(_ count: Int, _ token: UnsafeMutableRawPointer?) {
        let alert = NSAlert()
        alert.messageText = "\(count) synced circuits aren\u{2019}t on this computer any more."
        alert.informativeText = "Delete them on your other devices too, or bring them back here from the synced copy."
        alert.addButton(withTitle: "Delete Them Everywhere")
        alert.addButton(withTitle: "Bring Them Back")
        alert.buttons.first?.hasDestructiveAction = true
        Library.present(alert) { yes in cl_sync_answer(token, yes) }
    }

    private func askIncomingDeletes(_ count: Int, from devices: String, _ token: UnsafeMutableRawPointer?) {
        let alert = NSAlert()
        let total = max(count, circuits)
        alert.messageText = "\(devices.isEmpty ? "Another device" : devices) deleted \(count) of your \(total) synced circuits."
        alert.informativeText = "Moved to Recently Deleted, they can still be brought back from the library\u{2019}s trash on this Mac."
        alert.addButton(withTitle: "Move Them to Recently Deleted")
        alert.addButton(withTitle: "Keep Them")
        Library.present(alert) { yes in cl_sync_answer(token, yes) }
    }

    // MARK: Triggers

    /// After any save, rename, import, duplicate, delete or restore in the library.
    nonisolated static func libraryChanged() {
        onMain { c in if let e = c.engine { cl_sync_note_library_changed(e) } }
    }

    func syncNow() {
        start()
        if let engine { cl_sync_now(engine) }
        refresh()
    }

    func setDeviceName(_ name: String) {
        let n = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let engine, !n.isEmpty, n != deviceName else { return }
        cl_sync_set_device_name(engine, n)
        refresh()
    }

    // MARK: Turning on, linking, off (5.1)

    private final class Done: @unchecked Sendable {
        let fn: @MainActor (Bool, String) -> Void
        init(_ fn: @escaping @MainActor (Bool, String) -> Void) { self.fn = fn }
    }
    private final class PreviewDone: @unchecked Sendable {
        let fn: @MainActor (Bool, String, Int) -> Void
        init(_ fn: @escaping @MainActor (Bool, String, Int) -> Void) { self.fn = fn }
    }
    /// A CLSyncDone that runs the Done it's given, on the main thread.
    private static let doneCallback: CLSyncDone = { ctx, ok, message in
        let box = Unmanaged<Done>.fromOpaque(ctx!).takeRetainedValue()
        let text = message.map { String(cString: $0) } ?? ""
        DispatchQueue.main.async { box.fn(ok, text) }
    }
    private func done(_ fn: @escaping @MainActor (Bool, String) -> Void) -> UnsafeMutableRawPointer {
        Unmanaged.passRetained(Done(fn)).toOpaque()
    }

    func turnOn() {
        start()
        guard let engine, working == nil else { return }
        working = "Setting up\u{2026}"
        pageMessage = nil
        cl_sync_turn_on(engine, Self.doneCallback, done { ok, message in
            self.working = nil
            self.refresh()
            if ok { self.sheet = .code } else { self.pageMessage = message }
        })
    }

    /// I Have a Code…, with what a link brought, if anything.
    func haveCode(_ text: String = "") {
        enteredCode = text
        fieldError = nil
        sheet = .enter
    }

    /// The canonical code of what's typed, or why it isn't one.
    func parse(_ text: String) -> (code: String?, why: String?) {
        var h = hooks.sha256 == nil ? platform.hooks() : hooks
        var code = [CChar](repeating: 0, count: 29), why = [CChar](repeating: 0, count: 16)
        if cl_sync_parse_code(&h, text, &code, &why) { return (String(cString: code), nil) }
        return (nil, String(cString: cl_sync_why_text(why, text)))
    }
    func whyKind(_ text: String) -> String? {
        var h = hooks.sha256 == nil ? platform.hooks() : hooks
        var code = [CChar](repeating: 0, count: 29), why = [CChar](repeating: 0, count: 16)
        return cl_sync_parse_code(&h, text, &code, &why) ? nil : String(cString: why)
    }

    /// Continue: the code is checked (this Mac's own? another while syncing?),
    /// then the preview, then the confirmation. Nothing is stored until Link.
    func continueWithCode() {
        let (parsed, why) = parse(enteredCode)
        guard let code = parsed else { fieldError = why; return }
        fieldError = nil
        start()
        if enabled && code == self.code {
            fieldError = "This Mac already syncs with this code."
            return
        }
        if enabled {
            let alert = NSAlert()
            alert.messageText = "This Mac syncs with another code. Switch to this one?"
            alert.informativeText = "Circuits here stay; they\u{2019}ll be added to the other synced circuits."
            alert.addButton(withTitle: "Switch")
            alert.addButton(withTitle: "Cancel")
            Library.present(alert) { yes in if yes { self.runPreview(code) } }
            return
        }
        runPreview(code)
    }

    private func runPreview(_ code: String) {
        guard let engine, working == nil else { return }
        working = "Checking the code\u{2026}"
        let box = Unmanaged.passRetained(PreviewDone { ok, message, n in
            self.working = nil
            // The person closed the sheet while it was checking: nothing pops up later.
            guard self.sheet != nil else { return }
            if ok {
                self.sheet = .confirm(code: code, sentence: message, empty: n == 0)
            } else {
                self.sheet = .enter
                self.fieldError = message
            }
        }).toOpaque()
        cl_sync_preview(engine, code, { ctx, ok, message, n, _ in
            let box = Unmanaged<PreviewDone>.fromOpaque(ctx!).takeRetainedValue()
            let text = message.map { String(cString: $0) } ?? ""
            DispatchQueue.main.async { box.fn(ok, text, Int(n)) }
        }, box)
    }

    func link(_ code: String) {
        guard let engine else { return }
        sheet = nil
        working = "Linking\u{2026}"
        pageMessage = nil
        cl_sync_link(engine, code, Self.doneCallback, done { ok, message in
            self.working = nil
            self.refresh()
            if !ok { self.pageMessage = message }
        })
    }

    func turnOff() {
        guard let engine else { return }
        let alert = NSAlert()
        alert.messageText = "Turn off sync on this Mac?"
        alert.informativeText = "Your circuits stay on this Mac. Your other devices keep syncing with each other."
        let remove = NSButton(checkboxWithTitle: "Also remove the synced circuits from this Mac (they go to the library\u{2019}s trash)",
                              target: nil, action: nil)
        remove.state = .off
        alert.accessoryView = remove
        alert.addButton(withTitle: "Turn Off")
        alert.addButton(withTitle: "Cancel")
        Library.present(alert) { yes in
            guard yes else { return }
            cl_sync_turn_off(engine, remove.state == .on)
            self.refresh()
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { self.refresh() }
        }
    }

    func deleteSyncedCopy() {
        guard let engine else { return }
        let alert = NSAlert()
        alert.messageText = "Delete the synced copy?"
        alert.informativeText = "Sync stops on every device. Each device keeps the circuits it has now. This can\u{2019}t be undone."
        alert.addButton(withTitle: "Delete")
        alert.addButton(withTitle: "Cancel")
        alert.buttons.first?.hasDestructiveAction = true
        Library.present(alert) { yes in
            guard yes else { return }
            self.working = "Deleting the synced copy\u{2026}"
            cl_sync_delete_synced_copy(engine, Self.doneCallback, self.done { ok, message in
                self.working = nil
                self.refresh()
                if !ok { self.pageMessage = message }
            })
        }
    }

    func startOver() {
        guard let engine else { return }
        let alert = NSAlert()
        alert.messageText = "Start over with a new code?"
        alert.informativeText = "The synced copy is deleted and a new code is made. Your circuits here are sent with the new code. Every other device stops syncing until you link it again with the new code. Use this if someone else may have your code."
        alert.addButton(withTitle: "Start Over")
        alert.addButton(withTitle: "Cancel")
        alert.buttons.first?.hasDestructiveAction = true
        Library.present(alert) { yes in
            guard yes else { return }
            self.working = "Making a new code\u{2026}"
            cl_sync_start_over(engine, Self.doneCallback, self.done { ok, message in
                self.working = nil
                self.refresh()
                if ok { self.sheet = .code } else { self.pageMessage = message }
            })
        }
    }

    // MARK: Links (5.3)

    /// cedarlogic://sync#k=…: Settings > Sync with the code filled in, the
    /// preview run, then the confirmation -- never linking by itself.
    func openLink(_ text: String) {
        PrefsWindow.shared.show(page: .sync)
        haveCode(text)
        DispatchQueue.main.async { self.continueWithCode() }
    }

    // MARK: Quitting (4.12)

    /// applicationShouldTerminate: with sync on, the open circuits are saved,
    /// then the engine sends what's left (at most 5 s) before the app goes.
    func terminate() -> NSApplication.TerminateReply {
        guard let engine, started, cl_sync_enabled(engine), !isQuitting else { return .terminateNow }
        isQuitting = true
        quitReplied = false
        let docs = NSDocumentController.shared.documents.filter {
            Library.item(for: $0.fileURL) != nil && ($0.isDocumentEdited || $0.hasUnautosavedChanges)
        }
        let push = Countdown(docs.count) {
            cl_sync_quitting(engine, { _ in
                DispatchQueue.main.async { SyncCenter.shared.quitDone() }
            }, nil)
        }
        for d in docs { d.autosave(withImplicitCancellability: false) { _ in push.tick() } }
        // Never a hang: whatever happens, the app goes.
        DispatchQueue.main.asyncAfter(deadline: .now() + 8) { self.quitDone() }
        return .terminateLater
    }

    private func quitDone() {
        guard isQuitting, !quitReplied else { return }
        quitReplied = true
        NSApp.reply(toApplicationShouldTerminate: true)
    }
}
