// The classroom core's platform hooks on the Mac (CLASSROOM.md 6.4), the part
// that needs no window: CryptoKit's P-256 for the class's key pair and the
// sealed envelopes, CommonCrypto's PBKDF2 for the join code, the Classroom
// folder (0700) with its files written atomically at 0600, and its flock. The
// crypto, HTTP and main-thread hooks are Sync's (SyncHooks.swift) as they are.
// The app adds its UI hooks on top (Group 7); mac/Tools/classroom-check.swift
// uses these as they are to run the core's self-test.

import CommonCrypto
import CryptoKit
import Foundation

final class ClassroomPlatform {
    /// ~/Library/Application Support/CedarLogic/Classroom: per machine, outside the library (2.4).
    let dir: URL
    let sync: SyncPlatform
    /// Sync's hooks, kept at a fixed address for as long as this object lives (the core keeps the pointer).
    private let syncHooks: UnsafeMutablePointer<CLSyncHooks>
    private var lockFD: Int32 = -1
    private let lockGuard = NSLock()

    init(dir: URL, sync: SyncPlatform) {
        self.dir = dir
        self.sync = sync
        syncHooks = UnsafeMutablePointer<CLSyncHooks>.allocate(capacity: 1)
        syncHooks.initialize(to: sync.hooks())
    }

    deinit {
        unlock()
        syncHooks.deinitialize(count: 1)
        syncHooks.deallocate()
    }

    static var defaultDir: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("CedarLogic/Classroom", isDirectory: true)
    }

    /// The hooks every use shares; `ctx` is this object (kept alive by its owner). The UI
    /// hooks (classes_changed ... sync_delete_side, check_circuit, lights_of) are left empty
    /// for the app to fill.
    func hooks() -> CLClassroomHooks {
        var h = CLClassroomHooks()
        h.ctx = Unmanaged.passUnretained(self).toOpaque()
        h.sync = UnsafePointer(syncHooks)
        h.p256_generate = { _, d, pub in ClassroomPlatform.p256Generate(d, pub) }
        h.p256_public = { _, d, pub in ClassroomPlatform.p256Public(d, pub) }
        h.p256_ecdh = { _, d, peer, x in ClassroomPlatform.p256Ecdh(d, peer, x) }
        h.pbkdf2_sha256 = { _, pw, pwLen, salt, saltLen, rounds, out in
            ClassroomPlatform.pbkdf2(pw, pwLen, salt, saltLen, rounds, out)
        }
        h.load_file = { ctx, name in ClassroomPlatform.of(ctx).loadFile(name) }
        h.save_file = { ctx, name, text in ClassroomPlatform.of(ctx).saveFile(name, text) }
        h.remove_tree = { ctx, name in ClassroomPlatform.of(ctx).removeTree(name) }
        h.try_lock = { ctx, path in ClassroomPlatform.of(ctx).tryLock(path) }
        h.unlock = { ctx in ClassroomPlatform.of(ctx).unlock() }
        return h
    }

    static func of(_ ctx: UnsafeMutableRawPointer?) -> ClassroomPlatform {
        Unmanaged<ClassroomPlatform>.fromOpaque(ctx!).takeUnretainedValue()
    }

    // MARK: P-256 (1.4)

    private static func copy(_ d: Data, to out: UnsafeMutablePointer<UInt8>, _ n: Int) -> Bool {
        guard d.count == n else { return false }
        d.copyBytes(to: out, count: n)
        return true
    }

    /// A fresh key pair: d (32 bytes) and the public key, X9.63 uncompressed (65 bytes).
    static func p256Generate(_ d: UnsafeMutablePointer<UInt8>?, _ pub: UnsafeMutablePointer<UInt8>?) -> Bool {
        guard let d, let pub else { return false }
        let k = P256.KeyAgreement.PrivateKey()
        return copy(k.rawRepresentation, to: d, 32) && copy(k.publicKey.x963Representation, to: pub, 65)
    }

    /// The public key of d; false for a scalar that isn't one (0, or not below the order).
    static func p256Public(_ d: UnsafePointer<UInt8>?, _ pub: UnsafeMutablePointer<UInt8>?) -> Bool {
        guard let d, let pub,
              let k = try? P256.KeyAgreement.PrivateKey(rawRepresentation: Data(bytes: d, count: 32))
        else { return false }
        return copy(k.publicKey.x963Representation, to: pub, 65)
    }

    /// The X coordinate of d * peer; false unless peer is 04 | X | Y on the curve (CryptoKit's
    /// import checks the curve; the compressed forms are refused here first).
    static func p256Ecdh(_ d: UnsafePointer<UInt8>?, _ peer: UnsafePointer<UInt8>?, _ x: UnsafeMutablePointer<UInt8>?) -> Bool {
        guard let d, let peer, let x, peer[0] == 4,
              let k = try? P256.KeyAgreement.PrivateKey(rawRepresentation: Data(bytes: d, count: 32)),
              let q = try? P256.KeyAgreement.PublicKey(x963Representation: Data(bytes: peer, count: 65)),
              let shared = try? k.sharedSecretFromKeyAgreement(with: q)
        else { return false }
        let raw = shared.withUnsafeBytes { Data($0) }
        return copy(raw, to: x, 32)
    }

    /// PBKDF2-HMAC-SHA256 (the join code's 600,000 rounds, about 70 ms here).
    static func pbkdf2(_ pw: UnsafePointer<UInt8>?, _ pwLen: Int, _ salt: UnsafePointer<UInt8>?, _ saltLen: Int, _ rounds: UInt32,
                       _ out: UnsafeMutablePointer<UInt8>?) -> Bool {
        guard let pw, let salt, let out, rounds > 0 else { return false }
        let status = pw.withMemoryRebound(to: CChar.self, capacity: max(pwLen, 1)) { p in
            CCKeyDerivationPBKDF(CCPBKDFAlgorithm(kCCPBKDF2), p, pwLen, salt, saltLen,
                                 CCPseudoRandomAlgorithm(kCCPRFHmacAlgSHA256), rounds, out, 32)
        }
        return status == kCCSuccess
    }

    // MARK: The Classroom folder (2.4)

    /// A name the core gave ("teaching.json", "cache/<classId>/assignments/<aid>.json") as a
    /// file in the folder; nil for anything that would leave it.
    func url(_ name: UnsafePointer<CChar>?) -> URL? {
        guard let name else { return nil }
        let n = String(cString: name)
        let parts = n.split(separator: "/", omittingEmptySubsequences: false)
        guard !n.isEmpty, !n.hasPrefix("/"), !parts.contains(where: { $0.isEmpty || $0 == "." || $0 == ".." }) else { return nil }
        return parts.reduce(dir) { $0.appendingPathComponent(String($1)) }
    }

    /// A folder only this user can open (0700), and its parents.
    @discardableResult
    func makeDir(_ url: URL) -> Bool {
        try? FileManager.default.createDirectory(at: url, withIntermediateDirectories: true, attributes: [.posixPermissions: 0o700])
        return chmod(url.path, 0o700) == 0
    }

    func loadFile(_ name: UnsafePointer<CChar>?) -> UnsafeMutablePointer<CChar>? {
        guard let u = url(name), let data = FileManager.default.contents(atPath: u.path) else { return nil }
        return data.withUnsafeBytes { raw -> UnsafeMutablePointer<CChar>? in
            guard let buf = malloc(data.count + 1)?.assumingMemoryBound(to: CChar.self) else { return nil }
            if data.count > 0 { memcpy(buf, raw.baseAddress!, data.count) }
            buf[data.count] = 0
            return buf
        }
    }

    /// Written to a 0600 temporary file beside it, then renamed over the old one: never half
    /// written, never readable by anyone else for a moment (as Sync's secret).
    func saveFile(_ name: UnsafePointer<CChar>?, _ text: UnsafePointer<CChar>?) -> Bool {
        guard let u = url(name), let text, makeDir(dir), makeDir(u.deletingLastPathComponent()) else { return false }
        let tmp = u.deletingLastPathComponent().appendingPathComponent(".\(u.lastPathComponent)-\(getpid()).tmp")
        unlink(tmp.path)
        let fd = Darwin.open(tmp.path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0o600)
        guard fd >= 0 else { return false }
        let n = strlen(text)
        let wrote = Darwin.write(fd, text, n)
        let ok = wrote == n && fchmod(fd, 0o600) == 0 && fsync(fd) == 0
        close(fd)
        guard ok, rename(tmp.path, u.path) == 0 else { unlink(tmp.path); return false }
        return true
    }

    func removeTree(_ name: UnsafePointer<CChar>?) {
        guard let u = url(name) else { return }
        try? FileManager.default.removeItem(at: u)
    }

    /// The folder's lock, held while the engine runs, so two copies of the app don't poll and hand in at once.
    func tryLock(_ path: UnsafePointer<CChar>?) -> Bool {
        guard let path else { return false }
        lockGuard.lock(); defer { lockGuard.unlock() }
        if lockFD >= 0 { return true }
        makeDir(dir)
        let fd = Darwin.open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0o600)
        guard fd >= 0 else { return false }
        guard flock(fd, LOCK_EX | LOCK_NB) == 0 else { close(fd); return false }
        lockFD = fd
        return true
    }

    func unlock() {
        lockGuard.lock(); defer { lockGuard.unlock() }
        if lockFD >= 0 { flock(lockFD, LOCK_UN); close(lockFD) }
        lockFD = -1
    }
}
