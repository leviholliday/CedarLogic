// The Mac's sync hooks, checked without the app (mac/Tools/sync-check.sh
// builds this with mac/App/SyncHooks.swift and libCedarCore.a):
//
//   sync-check selftest <tempDir> [--server <url>]
//       the engine's self-test through the C interface with the app's own
//       hooks (CryptoKit, Compression, URLSession, the secret file, flock):
//       every SYNC.md 7.1 vector, then the 7.2 scenarios on the in-process
//       fake server, and with --server the one-device scenarios against the
//       mock server too.
//   sync-check engine <tempDir> <a.cdl> <b.cdl>
//       two real engines (two libraries, two sync folders) against the mock
//       server named by CL_SYNC_URL, driven as the app drives them: Turn On,
//       the preview, Link, edits both ways, a window with unsaved edits that
//       must not be touched, a delete, quitting. Prints PASS/FAIL lines.
//   sync-check pair <tempDir> <a.cdl>
//       adding a device by scanning (SYNC.md 11), this Mac as D: one engine
//       turns sync on (it stands for the person's phone), another shows a QR
//       code. The tool writes the link to <tempDir>/pair-link.txt and the
//       phone's code to <tempDir>/pair-code.txt and waits for something (a
//       phone, in the tests: mac/Tools/sync-pair-answer.mjs) to answer; then
//       it previews and links with the code that came, as the app does. Also
//       a cancelled QR code (its slot goes) and a QR code with sync already on.
//       Against the mock server named by CL_SYNC_URL.

import Foundation

@main
enum SyncCheck {
    static func main() {
        let args = CommandLine.arguments
        guard args.count >= 3 else {
            print("usage: sync-check selftest <tempDir> [--server <url>] | engine <tempDir> <a.cdl> <b.cdl> | pair <tempDir> <a.cdl>")
            exit(2)
        }
        setvbuf(stdout, nil, _IONBF, 0)
        switch args[1] {
        case "selftest":
            var server: String?
            if let i = args.firstIndex(of: "--server"), i + 1 < args.count { server = args[i + 1] }
            exit(selfTest(tempDir: args[2], server: server) ? 0 : 1)
        case "engine":
            guard args.count >= 5 else { print("engine needs <tempDir> <a.cdl> <b.cdl>"); exit(2) }
            exit(EngineRun(tempDir: URL(fileURLWithPath: args[2]), a: args[3], b: args[4]).run() ? 0 : 1)
        case "pair":
            guard args.count >= 4 else { print("pair needs <tempDir> <a.cdl>"); exit(2) }
            exit(EngineRun(tempDir: URL(fileURLWithPath: args[2]), a: args[3], b: args[3]).runPair() ? 0 : 1)
        default:
            print("unknown mode \(args[1])")
            exit(2)
        }
    }

    static func selfTest(tempDir: String, server: String?) -> Bool {
        let platform = SyncPlatform(syncDir: URL(fileURLWithPath: tempDir).appendingPathComponent("sync-folder"))
        var hooks = platform.hooks()
        var report: UnsafeMutablePointer<CChar>?
        let ok = cl_sync_self_test(&hooks, tempDir, server, &report)
        if let report {
            print(String(cString: report), terminator: "")
            free(report)
        }
        // The hooks' own extras: what the engine doesn't exercise.
        var pass = true
        func check(_ name: String, _ cond: Bool) {
            print(cond ? "PASS" : "FAIL", "mac-hooks: \(name)")
            if !cond { pass = false }
        }
        // A cut-short deflate stream is refused, not half read.
        let text = Array(String(repeating: "CedarLogic sync ", count: 400).utf8)
        var n = 0
        if let packed = SyncPlatform.deflate(text, text.count, &n) {
            var m = 0
            let whole = SyncPlatform.inflate(packed, n, 1 << 20, &m)
            check("inflate gives back what deflate made", whole != nil && m == text.count && memcmp(whole!, text, m) == 0)
            free(whole)
            let cut = SyncPlatform.inflate(packed, n / 2, 1 << 20, &m)
            check("a cut-short stream is refused", cut == nil)
            free(cut)
            let big = SyncPlatform.inflate(packed, n, text.count - 1, &m)
            check("more than maxOut is refused", big == nil)
            free(big)
            let exact = SyncPlatform.inflate(packed, n, text.count, &m)
            check("exactly maxOut is fine", exact != nil && m == text.count)
            free(exact)
            free(packed)
        } else {
            check("deflate", false)
        }
        // The secret: 0600 in a 0700 folder, and gone when forgotten.
        let dir = URL(fileURLWithPath: tempDir).appendingPathComponent("secret-check", isDirectory: true)
        let p = SyncPlatform(syncDir: dir)
        check("secret saved", p.saveSecret("000G40R40M30E209185GR38E1YZ4"))
        var st = stat()
        check("secret file is 0600", stat(dir.appendingPathComponent("secret").path, &st) == 0 && st.st_mode & 0o777 == 0o600)
        check("sync folder is 0700", stat(dir.path, &st) == 0 && st.st_mode & 0o777 == 0o700)
        if let s = p.loadSecret() {
            check("secret reads back", String(cString: s) == "000G40R40M30E209185GR38E1YZ4")
            free(s)
        } else {
            check("secret reads back", false)
        }
        p.forgetSecret()
        check("secret forgotten", p.loadSecret() == nil)
        // One lock per sync folder.
        let lock = dir.appendingPathComponent("lock").path
        let other = SyncPlatform(syncDir: dir)
        check("lock taken", p.tryLock(lock))
        check("a second holder is refused", !other.tryLock(lock))
        p.unlock()
        check("free again after unlock", other.tryLock(lock))
        other.unlock()
        // Plain http only to this computer.
        check("https allowed", SyncPlatform.allowed(URL(string: "https://cedarlogic.netlify.app/api/sync/v1")!))
        check("http localhost allowed", SyncPlatform.allowed(URL(string: "http://localhost:8787/api/sync/v1")!))
        check("http elsewhere refused", !SyncPlatform.allowed(URL(string: "http://example.com/api/sync/v1")!))
        print(pass && ok ? "mac sync-check: all passed" : "mac sync-check: FAILED")
        return ok && pass
    }
}

/// Two engines, as two Macs would run them, against the mock server.
final class EngineRun {
    final class Side {
        let name: String
        let library: URL
        let platform: SyncPlatform
        var engine: OpaquePointer!
        var dirty: Set<String> = []          // folders whose "window" has unsaved edits
        var replaced: [String] = []
        var notices: [String] = []
        init(name: String, root: URL) {
            self.name = name
            library = root.appendingPathComponent("Library", isDirectory: true)
            platform = SyncPlatform(syncDir: root.appendingPathComponent("Sync", isDirectory: true))
            try? FileManager.default.createDirectory(at: library, withIntermediateDirectories: true)
        }
    }
    static var sides: [Side] = []
    static func side(_ ctx: UnsafeMutableRawPointer?) -> Side { sides.first { Unmanaged.passUnretained($0.platform).toOpaque() == ctx }! }

    let a: Side, b: Side
    let cdlA: String, cdlB: String
    var failed = false

    init(tempDir: URL, a: String, b: String) {
        try? FileManager.default.removeItem(at: tempDir)
        self.a = Side(name: "Mac A", root: tempDir.appendingPathComponent("a"))
        self.b = Side(name: "Mac B", root: tempDir.appendingPathComponent("b"))
        cdlA = (try? String(contentsOfFile: a, encoding: .utf8)) ?? ""
        cdlB = (try? String(contentsOfFile: b, encoding: .utf8)) ?? ""
        EngineRun.sides = [self.a, self.b]
    }

    func check(_ name: String, _ cond: Bool) {
        print(cond ? "PASS" : "FAIL", "engine: \(name)")
        if !cond { failed = true }
    }

    /// Spins the main run loop (the engines' onMain lands there) until `cond` or the time is up.
    @discardableResult
    func wait(_ seconds: Double, _ cond: () -> Bool) -> Bool {
        let end = Date().addingTimeInterval(seconds)
        while Date() < end {
            if cond() { return true }
            RunLoop.main.run(until: Date().addingTimeInterval(0.05))
        }
        return cond()
    }

    func make(_ s: Side) {
        var h = s.platform.hooks()
        h.window_state = { ctx, folder, open, dirty, last in
            let side = EngineRun.side(ctx)
            let isDirty = side.dirty.contains(String(cString: folder!))
            open?.pointee = isDirty
            dirty?.pointee = isDirty
            last?.pointee = 0
        }
        h.circuit_replaced = { ctx, folder, _ in EngineRun.side(ctx).replaced.append(String(cString: folder!)) }
        h.notice = { ctx, text in EngineRun.side(ctx).notices.append(String(cString: text!)) }
        h.status_changed = { _ in }
        h.library_changed = { _ in }
        s.engine = cl_sync_create(&h, s.library.path, s.platform.syncDir.path, ProcessInfo.processInfo.environment["CL_SYNC_KEY"] ?? "",
                                  "mac/check", s.name)
        cl_sync_start(s.engine)
    }

    // The library as the app keeps it.
    func addCircuit(_ s: Side, _ name: String, _ cdl: String) -> String {
        let id = "20261005-1200\(String(format: "%02d", Int.random(in: 0...59)))-\(Int.random(in: 10000...99999))"
        let folder = s.library.appendingPathComponent(id, isDirectory: true)
        try? FileManager.default.createDirectory(at: folder.appendingPathComponent("versions"), withIntermediateDirectories: true)
        try? name.write(to: folder.appendingPathComponent("name.txt"), atomically: true, encoding: .utf8)
        try? cdl.write(to: folder.appendingPathComponent("circuit.cdl"), atomically: true, encoding: .utf8)
        cl_sync_note_library_changed(s.engine)
        return id
    }
    func circuits(_ s: Side) -> [String: (name: String, cdl: String)] {
        var out: [String: (String, String)] = [:]
        for id in (try? FileManager.default.contentsOfDirectory(atPath: s.library.path)) ?? [] where !id.hasPrefix(".") {
            let f = s.library.appendingPathComponent(id)
            guard let cdl = try? String(contentsOf: f.appendingPathComponent("circuit.cdl"), encoding: .utf8) else { continue }
            let name = (try? String(contentsOf: f.appendingPathComponent("name.txt"), encoding: .utf8)) ?? ""
            out[id] = (name.trimmingCharacters(in: .whitespacesAndNewlines), cdl)
        }
        return out
    }
    func folder(_ s: Side, named name: String) -> String? { circuits(s).first { $0.value.name == name }?.key }

    /// One full cycle on `s`, started now.
    @discardableResult
    func sync(_ s: Side, _ what: String) -> Bool {
        let before = cl_sync_last_sync(s.engine)
        cl_sync_now(s.engine)
        let ok = wait(30) {
            cl_sync_last_sync(s.engine) > before && cl_sync_status_kind(s.engine) != Int32(CL_SYNC_SYNCING)
        }
        if !ok { print("  (\(s.name) status: \(String(cString: cl_sync_status_text(s.engine))))") }
        check("\(s.name) synced: \(what)", ok)
        return ok
    }

    final class Box {
        var done = false, ok = false, message = "", circuits = 0, devices = ""
    }

    func run() -> Bool {
        guard let url = ProcessInfo.processInfo.environment["CL_SYNC_URL"], !url.isEmpty else {
            print("engine: CL_SYNC_URL isn't set (the mock server's /api/sync/v1 address)")
            return false
        }
        guard !cdlA.isEmpty, !cdlB.isEmpty else { print("engine: couldn't read the circuits"); return false }
        make(a)
        make(b)
        let lab = addCircuit(a, "Lab 5", cdlA)
        _ = addCircuit(b, "Lab 6", cdlB)
        _ = addCircuit(b, "Lab 5", cdlA)        // the same circuit on both: joined, not doubled

        // Turn On (A).
        let on = Box()
        cl_sync_turn_on(a.engine, { ctx, ok, msg in
            let box = Unmanaged<Box>.fromOpaque(ctx!).takeUnretainedValue()
            box.ok = ok; box.message = msg.map { String(cString: $0) } ?? ""; box.done = true
        }, Unmanaged.passUnretained(on).toOpaque())
        wait(20) { on.done }
        check("turn on", on.ok)
        guard on.ok else { print("  \(on.message)"); return false }
        wait(20) { cl_sync_status_kind(a.engine) == Int32(CL_SYNC_SYNCED) && cl_sync_last_sync(a.engine) > 0 }
        let code = String(cString: cl_sync_code(a.engine))
        check("a code of 28 symbols", code.count == 28)
        var st = stat()
        check("the secret file is 0600", stat(a.platform.syncDir.appendingPathComponent("secret").path, &st) == 0 && st.st_mode & 0o777 == 0o600)
        check("the sync folder is 0700", stat(a.platform.syncDir.path, &st) == 0 && st.st_mode & 0o777 == 0o700)
        check("the secret file holds the code",
              (try? String(contentsOf: a.platform.syncDir.appendingPathComponent("secret"), encoding: .utf8))?
                  .trimmingCharacters(in: .whitespacesAndNewlines) == code)

        // Preview, nothing stored (B).
        let pv = Box()
        cl_sync_preview(b.engine, code, { ctx, ok, msg, n, devices in
            let box = Unmanaged<Box>.fromOpaque(ctx!).takeUnretainedValue()
            box.ok = ok; box.message = msg.map { String(cString: $0) } ?? ""; box.circuits = Int(n)
            box.devices = devices.map { String(cString: $0) } ?? ""; box.done = true
        }, Unmanaged.passUnretained(pv).toOpaque())
        wait(20) { pv.done }
        check("preview: 1 circuit from Mac A", pv.ok && pv.circuits == 1 && pv.devices.contains("Mac A"))
        print("  preview: \(pv.message)")
        check("preview stored nothing", !cl_sync_enabled(b.engine)
              && !FileManager.default.fileExists(atPath: b.platform.syncDir.appendingPathComponent("secret").path))

        // Link (B).
        let ln = Box()
        cl_sync_link(b.engine, code, { ctx, ok, msg in
            let box = Unmanaged<Box>.fromOpaque(ctx!).takeUnretainedValue()
            box.ok = ok; box.message = msg.map { String(cString: $0) } ?? ""; box.done = true
        }, Unmanaged.passUnretained(ln).toOpaque())
        wait(20) { ln.done }
        check("link", ln.ok)
        wait(30) { cl_sync_status_kind(b.engine) == Int32(CL_SYNC_SYNCED) && cl_sync_last_sync(b.engine) > 0 }
        check("B: Lab 5 joined, not doubled", circuits(b).values.filter { $0.name == "Lab 5" }.count == 1)
        sync(a, "brings in Lab 6")
        check("A has Lab 6", folder(a, named: "Lab 6") != nil)
        check("A has 2 circuits", circuits(a).count == 2)

        // A rename on A arrives on B.
        try? "Lab 5 adder".write(to: a.library.appendingPathComponent(lab).appendingPathComponent("name.txt"), atomically: true, encoding: .utf8)
        cl_sync_note_library_changed(a.engine)
        sync(a, "sends the rename")
        sync(b, "takes the rename")
        check("B sees the new name", folder(b, named: "Lab 5 adder") != nil)

        // A window with unsaved edits on B is never touched; once it's clean, the change lands.
        guard let bLab = folder(b, named: "Lab 5 adder"), let aLab6 = folder(a, named: "Lab 6") else { return false }
        b.dirty.insert(bLab)
        let side = b
        let reloads = { side.replaced.filter { $0 == bLab }.count }
        let reloadsBefore = reloads()
        let edited = cdlB
        try? edited.write(to: a.library.appendingPathComponent(lab).appendingPathComponent("circuit.cdl"), atomically: true, encoding: .utf8)
        cl_sync_note_library_changed(a.engine)
        sync(a, "sends an edit")
        let held = (try? String(contentsOf: b.library.appendingPathComponent(bLab).appendingPathComponent("circuit.cdl"), encoding: .utf8)) ?? ""
        sync(b, "while the window is dirty")
        let still = (try? String(contentsOf: b.library.appendingPathComponent(bLab).appendingPathComponent("circuit.cdl"), encoding: .utf8)) ?? ""
        check("a dirty window's circuit isn't touched", still == held && reloads() == reloadsBefore)
        b.dirty.remove(bLab)
        sync(b, "once the window is clean")
        let now = (try? String(contentsOf: b.library.appendingPathComponent(bLab).appendingPathComponent("circuit.cdl"), encoding: .utf8)) ?? ""
        check("then the edit lands and the window reloads", now == edited && reloads() > reloadsBefore)
        let versions = (try? FileManager.default.contentsOfDirectory(atPath: b.library.appendingPathComponent(bLab).appendingPathComponent("versions").path)) ?? []
        check("versions kept around the change, with notes", versions.contains { $0.hasSuffix(".cdl") } && versions.contains { $0.hasSuffix(".txt") })

        // A delete on A goes to B's trash.
        try? FileManager.default.createDirectory(at: a.library.appendingPathComponent(".Trash"), withIntermediateDirectories: true)
        try? FileManager.default.moveItem(at: a.library.appendingPathComponent(aLab6), to: a.library.appendingPathComponent(".Trash/\(aLab6)"))
        cl_sync_note_library_changed(a.engine)
        sync(a, "sends the delete")
        sync(b, "takes the delete")
        check("B moved Lab 6 to its trash", folder(b, named: "Lab 6") == nil
              && !((try? FileManager.default.contentsOfDirectory(atPath: b.library.appendingPathComponent(".Trash").path)) ?? []).isEmpty)
        check("B said so", b.notices.contains { $0.contains("Lab 6") })

        // Quitting on A sends an edit within 5 s, without the main thread.
        let quitEdit = cdlA + "\n"
        try? quitEdit.write(to: a.library.appendingPathComponent(lab).appendingPathComponent("circuit.cdl"), atomically: true, encoding: .utf8)
        let quit = Box()
        let started = Date()
        cl_sync_quitting(a.engine, { ctx in
            let box = Unmanaged<Box>.fromOpaque(ctx!).takeUnretainedValue()
            DispatchQueue.main.async { box.done = true }
        }, Unmanaged.passUnretained(quit).toOpaque())
        wait(8) { quit.done }
        check("quitting finished within 5 s", quit.done && Date().timeIntervalSince(started) < 6)
        sync(b, "after A quit")
        let afterQuit = (try? String(contentsOf: b.library.appendingPathComponent(bLab).appendingPathComponent("circuit.cdl"), encoding: .utf8)) ?? ""
        check("the edit made just before quitting arrived", afterQuit == quitEdit)

        // Turn Off on B: circuits stay, the secret goes.
        cl_sync_turn_off(b.engine, false)
        wait(10) { !FileManager.default.fileExists(atPath: b.platform.syncDir.appendingPathComponent("secret").path) }
        check("turn off forgets the secret, keeps the circuits",
              !FileManager.default.fileExists(atPath: b.platform.syncDir.appendingPathComponent("secret").path) && circuits(b).count == 1)
        cl_sync_destroy(b.engine)
        cl_sync_destroy(a.engine)
        print(failed ? "engine run: FAILED" : "engine run: all passed")
        return !failed
    }

    // MARK: Pairing (SYNC.md 11, D)

    final class PairBox {
        var link: String?, result = -1, text = "", from = "", shows = 0, dones = 0
    }

    /// Starts a QR code on `s` the way the app does; the callbacks land on the main run loop.
    func pairStart(_ s: Side) -> PairBox {
        let box = PairBox()
        cl_sync_pair_start(s.engine, { ctx, link in
            let b = Unmanaged<PairBox>.fromOpaque(ctx!).takeUnretainedValue()
            precondition(Thread.isMainThread, "show must be on the main thread")
            b.link = link.map { String(cString: $0) }; b.shows += 1
        }, { ctx, result, text, from in
            let b = Unmanaged<PairBox>.fromOpaque(ctx!).takeUnretainedValue()
            precondition(Thread.isMainThread, "done must be on the main thread")
            b.result = Int(result); b.text = text.map { String(cString: $0) } ?? ""
            b.from = from.map { String(cString: $0) } ?? ""; b.dones += 1
        }, Unmanaged.passUnretained(box).toOpaque())
        return box
    }

    /// The mock server's slots, from its test controls (/__mock/dump).
    func pairSlots(_ base: String) -> Int {
        guard let url = URL(string: base + "/__mock/dump"), let data = try? Data(contentsOf: url),
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let pairs = json["pairs"] as? [String: Any] else { return -1 }
        return pairs.values.filter { !($0 is NSNull) }.count
    }

    func runPair() -> Bool {
        guard let url = ProcessInfo.processInfo.environment["CL_SYNC_URL"], !url.isEmpty else {
            print("pair: CL_SYNC_URL isn't set (the mock server's /api/sync/v1 address)")
            return false
        }
        guard !cdlA.isEmpty else { print("pair: couldn't read the circuit"); return false }
        let base = url.replacingOccurrences(of: "/api/sync/v1", with: "")
        let wantFrom = ProcessInfo.processInfo.environment["CL_PAIR_L_NAME"]
        make(a)
        make(b)
        _ = addCircuit(a, "Lab 5", cdlA)

        // The phone: syncing already.
        let on = Box()
        cl_sync_turn_on(a.engine, { ctx, ok, msg in
            let box = Unmanaged<Box>.fromOpaque(ctx!).takeUnretainedValue()
            box.ok = ok; box.message = msg.map { String(cString: $0) } ?? ""; box.done = true
        }, Unmanaged.passUnretained(on).toOpaque())
        wait(20) { on.done }
        check("the phone's engine turns sync on", on.ok)
        guard on.ok else { print("  \(on.message)"); return false }
        wait(20) { cl_sync_status_kind(a.engine) == Int32(CL_SYNC_SYNCED) && cl_sync_last_sync(a.engine) > 0 }
        let code = String(cString: cl_sync_code(a.engine))
        let dir = a.platform.syncDir.deletingLastPathComponent().deletingLastPathComponent()
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        try? code.write(to: dir.appendingPathComponent("pair-code.txt"), atomically: true, encoding: .utf8)

        // D: the QR code.
        let slotsBefore = pairSlots(base)
        let d = pairStart(b)
        wait(20) { d.link != nil }
        check("the QR code's link comes", d.link != nil)
        guard let link = d.link else { return false }
        let prefix = "https://cedarlogic.netlify.app/sync/#p="
        check("it is the pairing link (SYNC.md 11.2)", link.hasPrefix(prefix) && link.count == prefix.count + 28
              && link.dropFirst(prefix.count).allSatisfy { "0123456789ABCDEFGHJKMNPQRSTVWXYZ".contains($0) })
        check("show was called once, done not yet", d.shows == 1 && d.dones == 0)
        var modules = [UInt8](repeating: 0, count: 177 * 177)
        check("the QR code draws", cl_sync_qr(link, &modules) > 0)
        check("the website holds one more slot", pairSlots(base) == slotsBefore + 1 || slotsBefore < 0)
        try? link.write(to: dir.appendingPathComponent("pair-link.txt"), atomically: true, encoding: .utf8)
        print("LINK \(link)")

        // The phone answers (the tests' node script); D reads it.
        let waitS = Double(ProcessInfo.processInfo.environment["CL_PAIR_WAIT"] ?? "") ?? 60
        wait(waitS) { d.dones > 0 }
        check("an answer came (done called once)", d.dones == 1)
        check("it is the phone's code", d.result == Int(CL_SYNC_PAIR_CODE) && d.text == code)
        check("it says who sent it", !d.from.isEmpty && (wantFrom == nil || d.from == wantFrom))
        print("  from: \(d.from)")
        check("the slot is gone from the website", pairSlots(base) == slotsBefore || slotsBefore < 0)
        guard d.result == Int(CL_SYNC_PAIR_CODE) else { print("  \(d.text)"); return false }

        // As the app does: the preview, then Link.
        let pv = Box()
        cl_sync_preview(b.engine, d.text, { ctx, ok, msg, n, devices in
            let box = Unmanaged<Box>.fromOpaque(ctx!).takeUnretainedValue()
            box.ok = ok; box.message = msg.map { String(cString: $0) } ?? ""; box.circuits = Int(n)
            box.devices = devices.map { String(cString: $0) } ?? ""; box.done = true
        }, Unmanaged.passUnretained(pv).toOpaque())
        wait(20) { pv.done }
        check("preview: 1 circuit from the phone's engine", pv.ok && pv.circuits == 1 && pv.devices.contains("Mac A"))
        let ln = Box()
        cl_sync_link(b.engine, d.text, { ctx, ok, msg in
            let box = Unmanaged<Box>.fromOpaque(ctx!).takeUnretainedValue()
            box.ok = ok; box.message = msg.map { String(cString: $0) } ?? ""; box.done = true
        }, Unmanaged.passUnretained(ln).toOpaque())
        wait(20) { ln.done }
        check("link", ln.ok)
        wait(30) { cl_sync_status_kind(b.engine) == Int32(CL_SYNC_SYNCED) && cl_sync_last_sync(b.engine) > 0 }
        check("D has the phone's circuit", folder(b, named: "Lab 5") != nil)

        // A QR code with sync already on: refused, quietly.
        let on2 = pairStart(b)
        wait(10) { on2.dones > 0 }
        check("a QR code with sync on is refused", on2.dones == 1 && on2.result == Int(CL_SYNC_PAIR_FAILED) && on2.link == nil)

        // A third engine (a computer with sync off): its QR code, cancelled.
        let c = Side(name: "Mac C", root: URL(fileURLWithPath: NSTemporaryDirectory()).appendingPathComponent("cl-pair-c-\(getpid())"))
        EngineRun.sides.append(c)
        make(c)
        let before = pairSlots(base)
        let q = pairStart(c)
        wait(20) { q.link != nil }
        check("a second QR code shows", q.link != nil && pairSlots(base) == before + 1 || before < 0)
        cl_sync_pair_cancel(c.engine)
        wait(10) { pairSlots(base) == before || before < 0 }
        check("cancel deletes its slot", pairSlots(base) == before || before < 0)
        RunLoop.main.run(until: Date().addingTimeInterval(1))
        check("done is never called after cancel", q.dones == 0)
        // A new start replaces one under way: no done for the old one, one slot left.
        let r1 = pairStart(c)
        wait(20) { r1.link != nil }
        let r2 = pairStart(c)
        wait(20) { r2.link != nil }
        check("a new start gives a new link", r2.link != nil && r2.link != r1.link)
        wait(10) { pairSlots(base) == before + 1 || before < 0 }
        check("the replaced slot is gone", pairSlots(base) == before + 1 || before < 0)
        cl_sync_pair_cancel(c.engine)
        wait(10) { pairSlots(base) == before || before < 0 }
        check("and that one too", pairSlots(base) == before || before < 0 )
        check("done never came for the replaced or cancelled ones", r1.dones == 0 && r2.dones == 0)
        cl_sync_destroy(c.engine)
        try? FileManager.default.removeItem(at: c.platform.syncDir.deletingLastPathComponent())
        cl_sync_destroy(b.engine)
        cl_sync_destroy(a.engine)
        print(failed ? "pair run: FAILED" : "pair run: all passed")
        return !failed
    }
}
