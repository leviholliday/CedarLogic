// Secrets at rest: the sync code (SyncHooks.swift) and a teacher's class keys
// (ClassroomHooks.swift) live in the login Keychain, not in files in
// Application Support, where any backup would carry them in the clear.
//
// Each one stands in for the file it used to be: a generic password whose
// service is the app's bundle id and whose account is that file's path, so
// every Sync or Classroom folder (a test's own included) has its own item.
// The file is still the fallback: written when the Keychain says no, and
// whenever one is there it's the newest copy, so it's moved into the
// Keychain the next time it's read and deleted once the Keychain gives it
// back. Nothing is ever dropped on the way.

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

    private var query: [String: Any] {
        [kSecClass as String: kSecClassGenericPassword,
         kSecAttrService as String: service,
         kSecAttrAccount as String: file.path]
    }

    /// The newest copy: the file if there is one (moved into the Keychain on
    /// the way), else the Keychain's.
    func load() -> Loaded {
        if let fromFile = FileManager.default.contents(atPath: file.path) {
            if store(fromFile), case .data(let back) = read(), back == fromFile { unlink(file.path) }
            return .data(fromFile)
        }
        return read()
    }

    /// Into the Keychain, readable once the Mac has been unlocked after it
    /// starts, and never copied to another Mac. The old file goes once it's in.
    func save(_ data: Data) -> Bool {
        guard store(data) else { return false }
        unlink(file.path)
        return true
    }

    func delete() {
        SecItemDelete(query as CFDictionary)
    }

    private func read() -> Loaded {
        var q = query
        q[kSecReturnData as String] = true
        q[kSecMatchLimit as String] = kSecMatchLimitOne
        var out: CFTypeRef?
        switch SecItemCopyMatching(q as CFDictionary, &out) {
        case errSecSuccess: return (out as? Data).map { .data($0) } ?? .unreadable
        case errSecItemNotFound: return .none
        default: return .unreadable
        }
    }

    private func store(_ data: Data) -> Bool {
        let update: [String: Any] = [kSecValueData as String: data, kSecAttrLabel as String: label]
        let status = SecItemUpdate(query as CFDictionary, update as CFDictionary)
        if status == errSecSuccess { return true }
        guard status == errSecItemNotFound else { return false }
        var add = query
        add.merge(update) { new, _ in new }
        add[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
        return SecItemAdd(add as CFDictionary, nil) == errSecSuccess
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
