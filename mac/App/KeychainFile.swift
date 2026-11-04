// Secrets at rest: the sync code (SyncHooks.swift) and a teacher's class keys
// (ClassroomHooks.swift) are kept out of plain files in Application Support,
// where any backup would carry them in the clear.
//
// Why it works the way it does (and asks for no password after an update):
// the app is signed ad hoc (no Developer ID), so to the login Keychain each
// build is a different app -- its identity is its code hash. An item an app
// adds itself trusts only that exact build, twice over: its access list names
// the build, and since macOS 10.12 its partition list does too
// ("cdhash:..."). Opening the access list to every app doesn't help: the
// partition check still stops the next build, which then has to ask for the
// login password -- after every Sparkle update and every test build. And the
// partition list can't be changed without that password.
//
// So the Keychain item is written and read by /usr/bin/security, which Apple
// signs and which is the same program for every CedarLogic build: its items
// trust it alone (partition "apple-tool:"), so every build reads them
// without a prompt. What it holds is a 256-bit key per secret; the secret
// itself is sealed with that key (AES-GCM) in a 0600 file beside the one it
// stands in for ("secret.sealed", "teaching.json.sealed"). The key goes to
// `security` on its stdin, never on a command line.
//
// The trade-off: any program running as this user can run `security` and
// read the key without a prompt, then open the sealed file -- the same
// exposure the 0600 file had before the Keychain, and what an item open to
// every app would have had. Backups still never carry a secret in the clear:
// they get the sealed file (the folders are kept out of Time Machine anyway)
// and the login Keychain, which is encrypted with the login password.
// Signing with a Developer ID would be the stronger fix later: the app's own
// items would then trust it across updates (partition "teamid:...").
//
// Each key item stands in for the file it used to be: a generic password
// whose service is the app's bundle id (a test's own, never the app's) and
// whose account is the sealed file's path, so every Sync or Classroom folder
// has its own. Two older copies are moved in when they're found, newest
// first, and nothing is ever dropped on the way:
//   - the plain file (the fallback, written when this says no: always the
//     newest copy when it's there), deleted once its sealed copy reads back;
//   - an item an older build added itself (account = the plain file's path,
//     the secret in it). Reading it may ask for the password one last time;
//     it's deleted once its sealed copy reads back, and kept if that fails.

import CryptoKit
import Foundation
import Security

struct KeychainFile {
    /// The app's bundle id, or a test's own (never the app's).
    let service: String
    /// The file this item stands in for.
    let file: URL
    /// What Keychain Access shows.
    let label: String

    enum Loaded {
        case data(Data)
        case none
        /// The Keychain wouldn't answer (locked, or access refused): what it holds is unknown.
        case unreadable
    }

    /// The sealed copy, beside the file; also the key item's account.
    var sealedFile: URL { file.appendingPathExtension("sealed") }
    /// What Keychain Access shows as the key item's kind.
    static let keyKind = "CedarLogic file key"

    /// The newest copy: the file, else an older build's item if it's newer
    /// than the sealed copy, else the sealed copy. An older one is moved in
    /// on the way.
    func load() -> Loaded {
        if let fromFile = FileManager.default.contents(atPath: file.path) {
            _ = save(fromFile)   // the file goes once its sealed copy reads back
            return .data(fromFile)
        }
        let legacy = legacyItem()
        let sealedAt = (try? FileManager.default.attributesOfItem(atPath: sealedFile.path))?[.modificationDate] as? Date
        // An older build's item newer than the sealed copy (written by an older
        // build after this one moved it in) wins. Keychain dates are whole seconds.
        if case .present(let at) = legacy, sealedAt.map({ at >= $0.addingTimeInterval(-1) }) ?? true {
            switch readLegacy() {
            case .data(let d):
                _ = save(d)   // the old item goes once the sealed copy reads back
                return .data(d)
            case .unreadable: return .unreadable   // the newest copy is unknown: don't hand back an older one
            case .none: break                      // gone meanwhile
            }
        }
        guard sealedAt != nil else {
            if case .unknown = legacy { return .unreadable }
            return .none
        }
        let opened = openSealed()
        if case .data = opened, case .present = legacy { deleteLegacy() }   // older than the sealed copy
        return opened
    }

    /// Sealed into the file beside, with this item's key (made the first time).
    /// The plain file and an older build's item go once it reads back.
    func save(_ data: Data) -> Bool {
        guard case .key(let key) = key(create: true),
              let sealed = try? AES.GCM.seal(data, using: key).combined,
              Self.writeAtomically(sealed, to: sealedFile),
              let back = FileManager.default.contents(atPath: sealedFile.path),
              let box = try? AES.GCM.SealedBox(combined: back),
              (try? AES.GCM.open(box, using: key)) == data
        else { return false }
        unlink(file.path)
        if case .present = legacyItem() { deleteLegacy() }
        return true
    }

    func delete() {
        if Self.exists(service: service, account: sealedFile.path) == true {
            _ = Self.security(["delete-generic-password", "-s", service, "-a", sealedFile.path])
        }
        Self.forgetKey(service: service, account: sealedFile.path)
        if case .present = legacyItem() { deleteLegacy() }
        unlink(sealedFile.path)
    }

    // MARK: The sealed copy and its key

    private enum Key {
        case key(SymmetricKey)
        case missing
        case failed
    }

    private func openSealed() -> Loaded {
        guard let sealed = FileManager.default.contents(atPath: sealedFile.path) else { return .none }
        switch key(create: false) {
        case .key(let key):
            guard let box = try? AES.GCM.SealedBox(combined: sealed),
                  let data = try? AES.GCM.open(box, using: key) else { return .unreadable }
            return .data(data)
        case .missing: return .none   // no key, no way to open it: as good as nothing (the next save replaces it)
        case .failed: return .unreadable
        }
    }

    /// This item's key: from `security` (it made the item, so it reads it
    /// without a prompt), or a new one when there's none and `create`.
    private func key(create: Bool) -> Key {
        let account = sealedFile.path
        if let key = Self.cachedKey(service: service, account: account) { return .key(key) }
        switch Self.exists(service: service, account: account) {
        case true?:
            guard let r = Self.security(["find-generic-password", "-s", service, "-a", account, "-w"]), r.status == 0,
                  let text = String(data: r.out, encoding: .utf8),
                  let bytes = Self.unhex(text.trimmingCharacters(in: .whitespacesAndNewlines)), bytes.count == 32
            else { return .failed }
            let key = SymmetricKey(data: bytes)
            Self.cacheKey(key, service: service, account: account)
            return .key(key)
        case false?:
            guard create else { return .missing }
            // One line on its stdin (security -i), so the key is never on a command line.
            // Each value is quoted, with \ and " escaped as security's reader takes them
            // (\\ and \"). A line break can't be in a line at all: a value with one gets no
            // key, and the caller keeps the plain file.
            let fields = [service, account, label, Self.keyKind]
            guard let quoted = Self.quotedForSecurity(fields) else { return .failed }
            let hex = SymmetricKey(size: .bits256).withUnsafeBytes { Data($0) }.map { String(format: "%02x", $0) }.joined()
            let line = "add-generic-password -s \(quoted[0]) -a \(quoted[1]) -l \(quoted[2]) -D \(quoted[3]) -w \"\(hex)\"\n"
            _ = Self.security(["-i"], input: line)
            // Read back, whatever it said: if another copy added one first, that's the key.
            return key(create: false)
        case nil:
            return .failed
        }
    }

    private static let cacheLock = NSLock()
    private static var cache: [String: SymmetricKey] = [:]

    private static func cachedKey(service: String, account: String) -> SymmetricKey? {
        cacheLock.lock(); defer { cacheLock.unlock() }
        return cache[service + "\n" + account]
    }

    private static func cacheKey(_ key: SymmetricKey, service: String, account: String) {
        cacheLock.lock(); defer { cacheLock.unlock() }
        cache[service + "\n" + account] = key
    }

    private static func forgetKey(service: String, account: String) {
        cacheLock.lock(); defer { cacheLock.unlock() }
        cache[service + "\n" + account] = nil
    }

    // MARK: An older build's item (the secret itself, account = the plain file's path)

    private enum Legacy {
        case absent
        case present(Date)
        case unknown
    }

    private var legacyQuery: [String: Any] {
        [kSecClass as String: kSecClassGenericPassword,
         kSecAttrService as String: service,
         kSecAttrAccount as String: file.path]
    }

    /// Whether there is one, and when it was last written: its attributes,
    /// which any build reads without a prompt.
    private func legacyItem() -> Legacy {
        var q = legacyQuery
        q[kSecReturnAttributes as String] = true
        q[kSecMatchLimit as String] = kSecMatchLimitOne
        var out: CFTypeRef?
        switch SecItemCopyMatching(q as CFDictionary, &out) {
        case errSecSuccess:
            let attrs = out as? [String: Any]
            return .present(attrs?[kSecAttrModificationDate as String] as? Date ?? .distantPast)
        case errSecItemNotFound: return .absent
        default: return .unknown
        }
    }

    /// Its secret: the build that added it reads it as before; any other asks
    /// for the login password (once: it's moved into the sealed copy after).
    private func readLegacy() -> Loaded {
        var q = legacyQuery
        q[kSecReturnData as String] = true
        q[kSecMatchLimit as String] = kSecMatchLimitOne
        var out: CFTypeRef?
        switch SecItemCopyMatching(q as CFDictionary, &out) {
        case errSecSuccess: return (out as? Data).map { .data($0) } ?? .unreadable
        case errSecItemNotFound: return .none
        default: return .unreadable
        }
    }

    /// `security` deletes it without a prompt, whichever build added it (an
    /// app may only delete its own).
    private func deleteLegacy() {
        _ = Self.security(["delete-generic-password", "-s", service, "-a", file.path])
    }

    // MARK: Helpers

    /// Whether a generic password with this service and account is there (nil: the Keychain didn't say).
    private static func exists(service: String, account: String) -> Bool? {
        let q: [String: Any] = [kSecClass as String: kSecClassGenericPassword,
                                kSecAttrService as String: service,
                                kSecAttrAccount as String: account,
                                kSecMatchLimit as String: kSecMatchLimitOne]
        switch SecItemCopyMatching(q as CFDictionary, nil) {
        case errSecSuccess: return true
        case errSecItemNotFound: return false
        default: return nil
        }
    }

    /// Runs /usr/bin/security with `input` on its stdin; nil if it didn't start.
    private static func security(_ args: [String], input: String? = nil) -> (status: Int32, out: Data)? {
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/usr/bin/security")
        p.arguments = args
        let stdin = Pipe(), stdout = Pipe()
        p.standardInput = stdin
        p.standardOutput = stdout
        p.standardError = FileHandle.nullDevice
        // If it exits before reading, a write gets EPIPE back, not a SIGPIPE that ends the app.
        _ = fcntl(stdin.fileHandleForWriting.fileDescriptor, F_SETNOSIGPIPE, 1)
        do { try p.run() } catch { return nil }
        if let input { try? stdin.fileHandleForWriting.write(contentsOf: Data(input.utf8)) }
        try? stdin.fileHandleForWriting.close()
        let out = stdout.fileHandleForReading.readDataToEndOfFile()
        p.waitUntilExit()
        return (p.terminationStatus, out)
    }

    /// Each value in double quotes for `security -i`, \\ and \" escaped; nil if
    /// any holds a line break or a NUL, which no quoting gets through.
    static func quotedForSecurity(_ values: [String]) -> [String]? {
        var out: [String] = []
        for v in values {
            guard !v.unicodeScalars.contains(where: { $0 == "\n" || $0 == "\r" || $0 == "\0" }) else { return nil }
            out.append("\"" + v.replacingOccurrences(of: "\\", with: "\\\\").replacingOccurrences(of: "\"", with: "\\\"") + "\"")
        }
        return out
    }

    private static func unhex(_ s: String) -> Data? {
        let chars = Array(s.utf8)
        guard chars.count % 2 == 0 else { return nil }
        var out = Data(capacity: chars.count / 2)
        var i = 0
        while i < chars.count {
            guard let hi = hexValue(chars[i]), let lo = hexValue(chars[i + 1]) else { return nil }
            out.append(hi << 4 | lo)
            i += 2
        }
        return out
    }

    private static func hexValue(_ c: UInt8) -> UInt8? {
        switch c {
        case UInt8(ascii: "0")...UInt8(ascii: "9"): return c - UInt8(ascii: "0")
        case UInt8(ascii: "a")...UInt8(ascii: "f"): return c - UInt8(ascii: "a") + 10
        case UInt8(ascii: "A")...UInt8(ascii: "F"): return c - UInt8(ascii: "A") + 10
        default: return nil
        }
    }

    /// Written to a 0600 temporary file, then renamed over the old one: never
    /// half written, never readable by anyone else for a moment.
    private static func writeAtomically(_ data: Data, to url: URL) -> Bool {
        let tmp = url.deletingLastPathComponent().appendingPathComponent(".\(url.lastPathComponent)-\(UUID().uuidString).tmp")
        let fd = Darwin.open(tmp.path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0o600)
        guard fd >= 0 else { return false }
        let wrote = data.withUnsafeBytes { Darwin.write(fd, $0.baseAddress, $0.count) }
        let ok = wrote == data.count && fchmod(fd, 0o600) == 0 && fsync(fd) == 0
        close(fd)
        guard ok, rename(tmp.path, url.path) == 0 else { unlink(tmp.path); return false }
        return true
    }
}

extension URL {
    /// Kept out of Time Machine: the Sync and Classroom folders are this
    /// Mac's own state, and the Classroom one still holds a student's class
    /// keys in a file (memberships.json, too big for the Keychain).
    func excludeFromBackup() {
        var url = self
        var values = URLResourceValues()
        values.isExcludedFromBackup = true
        try? url.setResourceValues(values)
    }
}
