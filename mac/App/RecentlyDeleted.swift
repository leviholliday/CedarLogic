// Recently Deleted (docs/RECENTLY-DELETED.md): the circuits in the library's
// .Trash folder, where deleting one from Your Circuits (and a delete synced
// from another device) puts it. Each can be restored or deleted now; they go
// for good 30 days after they were deleted, swept at launch and whenever the
// list is read. Only files, no windows, so the Windows and Linux apps can
// mirror it line for line; the sheet is RecentlyDeletedView (LibraryView.swift).

import Foundation

struct DeletedCircuit: Identifiable, Hashable {
    /// The folder's name in .Trash ("<id>", or "<id> 2" for a second one).
    let id: String
    let name: String
    let folder: URL
    let deleted: Date
    var circuit: URL { folder.appendingPathComponent("circuit.cdl") }
}

enum RecentlyDeleted {
    static let keepDays = 30
    /// In each trashed circuit's folder: when it was deleted (ISO 8601, UTC).
    static let stampFile = "deleted.txt"

    static func trash(in root: URL) -> URL { root.appendingPathComponent(".Trash", isDirectory: true) }

    private static var iso: ISO8601DateFormatter {
        let f = ISO8601DateFormatter()
        f.formatOptions = [.withInternetDateTime]
        return f
    }

    /// Notes when a circuit went into the trash (Library.moveToTrash).
    static func stamp(_ folder: URL, at date: Date = Date()) {
        try? (iso.string(from: date) + "\n").write(to: folder.appendingPathComponent(stampFile), atomically: true, encoding: .utf8)
    }

    /// When it was deleted. A folder with no stamp (deleted by sync, which
    /// moves folders without one, or by an older app) is stamped the first
    /// time it's seen: with the time it was moved into the trash (the
    /// folder's status-change time) when that's recent, or else now, so an
    /// old one still gets its 30 days in the list before it goes.
    static func deletedDate(of folder: URL, now: Date = Date()) -> Date {
        if let text = try? String(contentsOf: folder.appendingPathComponent(stampFile), encoding: .utf8),
           let d = iso.date(from: text.trimmingCharacters(in: .whitespacesAndNewlines)) {
            return d
        }
        let moved = (try? folder.resourceValues(forKeys: [.attributeModificationDateKey]).attributeModificationDate) ?? now
        let fresh = moved <= now && now.timeIntervalSince(moved) < Double(keepDays) * 86400
        let d = fresh ? moved : now
        stamp(folder, at: d)
        return d
    }

    /// The deleted circuits, newest first. Folders in .Trash with no
    /// circuit.cdl ("repeated versions", Library.removeRepeats) aren't
    /// circuits and are left alone.
    static func list(in root: URL = Library.root, now: Date = Date()) -> [DeletedCircuit] {
        let fm = FileManager.default
        let bin = trash(in: root)
        guard let names = try? fm.contentsOfDirectory(atPath: bin.path) else { return [] }
        return names.filter { !$0.hasPrefix(".") }.compactMap { n in
            let folder = bin.appendingPathComponent(n, isDirectory: true)
            guard fm.fileExists(atPath: folder.appendingPathComponent("circuit.cdl").path) else { return nil }
            let name = (try? String(contentsOf: folder.appendingPathComponent("name.txt"), encoding: .utf8))?
                .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
            return DeletedCircuit(id: n, name: name.isEmpty ? "Untitled" : name, folder: folder,
                                  deleted: deletedDate(of: folder, now: now))
        }
        .sorted { $0.deleted > $1.deleted }
    }

    /// Those deleted more than 30 days ago go for good. How many went.
    @discardableResult
    static func sweep(in root: URL = Library.root, now: Date = Date()) -> Int {
        let limit = now.addingTimeInterval(-Double(keepDays) * 86400)
        var n = 0
        for c in list(in: root, now: now) where c.deleted < limit {
            if (try? FileManager.default.removeItem(at: c.folder)) != nil { n += 1 }
        }
        return n
    }

    /// Back into Your Circuits, under the id it had (or a new one if that's
    /// taken), at the top of the list. Its library id, or nil if it
    /// couldn't be moved.
    static func restore(_ c: DeletedCircuit, in root: URL = Library.root) -> String? {
        let fm = FileManager.default
        // "<id> 2" was the second circuit with that id in the trash.
        var id = c.id
        if let r = id.range(of: #" \d+$"#, options: .regularExpression) { id = String(id[..<r.lowerBound]) }
        if id.isEmpty || fm.fileExists(atPath: root.appendingPathComponent(id).path) {
            let f = DateFormatter()
            f.dateFormat = "yyyyMMdd-HHmmss"
            f.locale = Locale(identifier: "en_US_POSIX")
            id = "\(f.string(from: Date()))-\(Int.random(in: 1000...99999))"
        }
        let dest = root.appendingPathComponent(id, isDirectory: true)
        guard (try? fm.moveItem(at: c.folder, to: dest)) != nil else { return nil }
        try? fm.removeItem(at: dest.appendingPathComponent(stampFile))
        try? fm.setAttributes([.modificationDate: Date()], ofItemAtPath: dest.appendingPathComponent("circuit.cdl").path)
        return id
    }

    /// Gone for good, now.
    static func deleteNow(_ c: DeletedCircuit) -> Bool {
        (try? FileManager.default.removeItem(at: c.folder)) != nil
    }
}
