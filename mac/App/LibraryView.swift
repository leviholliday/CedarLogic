// Your Circuits (⌘O) and Version History, drawn like the wx app's
// LibraryDialogs: a heading, a quiet line under it, a search field, and a
// list of rows each with a gate tile, a name and a second line, then the
// buttons. Version History puts a picture of the chosen version beside it.
//
// The library is the same one the wx app keeps, in
// ~/Library/Application Support/CedarLogic/Library -- one folder per circuit
// holding name.txt, circuit.cdl and versions/<timestamp>.cdl. Both apps read
// and write it, so a circuit saved in one shows up in the other. With sync
// on (Sync.swift), every change here is noted for the next sync, a line under
// the list says how syncing is going, and a version that came from another
// device carries a note (versions/<stamp>.txt) saying where from.

import AppKit
import SwiftUI

struct LibraryItem: Identifiable, Hashable {
    let id: String
    let name: String
    let folder: URL
    let modified: Date
    var circuit: URL { folder.appendingPathComponent("circuit.cdl") }
    var versionsFolder: URL { folder.appendingPathComponent("versions") }
}

struct LibraryVersion: Identifiable, Hashable {
    let url: URL
    let date: Date
    var id: URL { url }
}

enum Library {
    static var root: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("CedarLogic/Library", isDirectory: true)
    }

    static func items() -> [LibraryItem] {
        let fm = FileManager.default
        guard let ids = try? fm.contentsOfDirectory(atPath: root.path) else { return [] }
        return ids.filter { !$0.hasPrefix(".") }.compactMap { id in
            let folder = root.appendingPathComponent(id, isDirectory: true)
            let circuit = folder.appendingPathComponent("circuit.cdl")
            guard fm.fileExists(atPath: circuit.path) else { return nil }
            let name = (try? String(contentsOf: folder.appendingPathComponent("name.txt"), encoding: .utf8))?
                .trimmingCharacters(in: .whitespacesAndNewlines) ?? id
            let modified = (try? fm.attributesOfItem(atPath: circuit.path)[.modificationDate] as? Date) ?? .distantPast
            return LibraryItem(id: id, name: name.isEmpty ? "Untitled" : name, folder: folder, modified: modified)
        }
        .sorted { $0.modified > $1.modified }
    }

    /// The library circuit a file is, if it's one.
    static func item(for url: URL?) -> LibraryItem? {
        guard let url, url.lastPathComponent == "circuit.cdl" else { return nil }
        let folder = url.deletingLastPathComponent().standardizedFileURL
        guard folder.deletingLastPathComponent().path == root.standardizedFileURL.path else { return nil }
        let name = (try? String(contentsOf: folder.appendingPathComponent("name.txt"), encoding: .utf8))?
            .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        return LibraryItem(id: folder.lastPathComponent, name: name.isEmpty ? "Untitled" : name, folder: folder, modified: Date())
    }
    /// What to call a circuit's window: its name in Your Circuits.
    static func displayName(for url: URL?) -> String? { item(for: url)?.name }

    static func versions(of item: LibraryItem) -> [LibraryVersion] {
        let fm = FileManager.default
        guard let files = try? fm.contentsOfDirectory(at: item.versionsFolder, includingPropertiesForKeys: [.contentModificationDateKey]) else { return [] }
        return files.filter { $0.pathExtension == "cdl" && !$0.lastPathComponent.hasPrefix(".") }.map { url in
            let date = (try? url.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate) ?? .distantPast
            return LibraryVersion(url: url, date: date)
        }
        .sorted { $0.date > $1.date }
    }

    /// A new, empty circuit in the library, named `name`.
    static func create(named name: String) throws -> LibraryItem {
        try create(named: name, text: CoreDocument().saveText())
    }

    /// A new circuit in the library holding `text`; `source` is the file it
    /// was imported from, if any, so opening that file again finds it.
    static func create(named name: String, text: String, source: URL? = nil) throws -> LibraryItem {
        let f = DateFormatter()
        f.dateFormat = "yyyyMMdd-HHmmss"
        let id = "\(f.string(from: Date()))-\(Int.random(in: 1000...99999))"
        let folder = root.appendingPathComponent(id, isDirectory: true)
        try FileManager.default.createDirectory(at: folder.appendingPathComponent("versions"), withIntermediateDirectories: true)
        try name.write(to: folder.appendingPathComponent("name.txt"), atomically: true, encoding: .utf8)
        try text.write(to: folder.appendingPathComponent("circuit.cdl"), atomically: true, encoding: .utf8)
        // The file as it is (not as the engine writes it back), to know it again.
        if let source, let original = try? String(contentsOf: source, encoding: .utf8) {
            try? sourceMark(source, text: original).write(to: folder.appendingPathComponent("source.txt"), atomically: true, encoding: .utf8)
        }
        SyncCenter.libraryChanged()
        return LibraryItem(id: id, name: name, folder: folder, modified: Date())
    }

    /// Whether a file is part of the library (a circuit or one of its versions).
    static func contains(_ url: URL) -> Bool {
        url.standardizedFileURL.path.hasPrefix(root.standardizedFileURL.path + "/")
    }

    /// What an imported circuit remembers of its file: where it was and
    /// what was in it, so opening that same file again finds the copy --
    /// but a file that's changed since (a newer download) comes in afresh.
    private static func sourceMark(_ url: URL, text: String) -> String {
        url.standardizedFileURL.path + "\n" + String(format: "%016llx", fnv1a(text))
    }
    private static func fnv1a(_ s: String) -> UInt64 {
        var h: UInt64 = 0xcbf29ce484222325
        for b in s.utf8 { h = (h ^ UInt64(b)) &* 0x100000001b3 }
        return h
    }

    /// The circuit already imported from `url` as it is now, if there is one.
    static func imported(from url: URL) -> LibraryItem? {
        guard let text = try? String(contentsOf: url, encoding: .utf8) else { return nil }
        let mark = sourceMark(url, text: text)
        return items().first { item in
            let stored = try? String(contentsOf: item.folder.appendingPathComponent("source.txt"), encoding: .utf8)
            // (The first imports kept only the path.)
            return stored == mark || stored == url.standardizedFileURL.path
        }
    }

    static func rename(_ item: LibraryItem, to name: String) {
        try? name.write(to: item.folder.appendingPathComponent("name.txt"), atomically: true, encoding: .utf8)
        SyncCenter.libraryChanged()
        // Open windows show it by this name: they read it again.
        NotificationCenter.default.post(name: .clLibraryChanged, object: nil)
    }

    /// Into the library's own trash folder, where the wx app puts deleted
    /// circuits too. False if it couldn't be moved.
    @discardableResult
    static func moveToTrash(_ item: LibraryItem) -> Bool {
        let fm = FileManager.default
        let trash = root.appendingPathComponent(".Trash", isDirectory: true)
        try? fm.createDirectory(at: trash, withIntermediateDirectories: true)
        var dest = trash.appendingPathComponent(item.id)
        var n = 2
        while fm.fileExists(atPath: dest.path) { dest = trash.appendingPathComponent("\(item.id) \(n)"); n += 1 }
        let moved = (try? fm.moveItem(at: item.folder, to: dest)) != nil
        if moved { SyncCenter.libraryChanged() }
        return moved
    }

    /// The library circuits open in windows now, by id.
    @MainActor static func openIDs() -> Set<String> {
        Set(NSDocumentController.shared.documents.compactMap { item(for: $0.fileURL)?.id })
    }

    /// Deletes a circuit: its windows close first (without saving -- it's
    /// going), then it goes to the library's trash.
    @MainActor static func delete(_ item: LibraryItem) -> Bool {
        for doc in NSDocumentController.shared.documents where Library.item(for: doc.fileURL)?.id == item.id {
            doc.close()
        }
        let ok = moveToTrash(item)
        NotificationCenter.default.post(name: .clLibraryChanged, object: nil)
        return ok
    }

    /// An alert as a sheet on the window in front (the library's), or on
    /// its own; `done` hears whether the first button was chosen.
    @MainActor static func present(_ alert: NSAlert, _ done: @escaping (Bool) -> Void) {
        if let w = NSApp.keyWindow, w.attachedSheet == nil {
            alert.beginSheetModal(for: w) { r in done(r == .alertFirstButtonReturn) }
        } else {
            done(alert.runModal() == .alertFirstButtonReturn)
        }
    }

    /// A library circuit was saved. It saves itself every few seconds, like
    /// Google Docs, so not every save is a version: the latest save waits in
    /// versions/.pending.cdl, and becomes a version when you come back after
    /// a break (10 minutes without saving), after half an hour of steady
    /// work, or at once on ⌘S. Old versions thin as the wx app's do:
    /// everything from the last day, then one an hour for a week, then one a day.
    static func noteSaved(_ url: URL?, explicit: Bool = false) {
        guard let url, url.lastPathComponent == "circuit.cdl" else { return }
        let folder = url.deletingLastPathComponent()
        guard folder.deletingLastPathComponent().standardizedFileURL.path == root.standardizedFileURL.path else { return }
        let fm = FileManager.default
        let item = LibraryItem(id: folder.lastPathComponent, name: "", folder: folder, modified: Date())
        try? fm.createDirectory(at: item.versionsFolder, withIntermediateDirectories: true)
        let pending = item.versionsFolder.appendingPathComponent(".pending.cdl")
        let stamp = DateFormatter()
        stamp.dateFormat = "yyyyMMdd-HHmmss"
        let now = Date()
        let newest = versions(of: item).first
        func keep(_ src: URL, at date: Date) {
            // Not twice the same circuit in a row -- the same parts in the
            // same places, whatever its switches or wires were doing.
            if let n = newest, let a = shape(of: n.url), a == shape(of: src) { return }
            let dest = item.versionsFolder.appendingPathComponent(stamp.string(from: date) + ".cdl")
            try? fm.removeItem(at: dest)
            try? fm.copyItem(at: src, to: dest)
        }
        if explicit {
            keep(url, at: now)
        } else if fm.fileExists(atPath: pending.path) {
            let pendingDate = (try? pending.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate) ?? now
            let afterBreak = now.timeIntervalSince(pendingDate) > 10 * 60
            let longSession = now.timeIntervalSince(newest?.date ?? .distantPast) > 30 * 60
            // The work before the break (or the last half hour's) is a version.
            if afterBreak || longSession { keep(pending, at: pendingDate) }
        } else if newest == nil {
            keep(url, at: now)   // a circuit's first save
        }
        try? fm.removeItem(at: pending)
        try? fm.copyItem(at: url, to: pending)
        thin(item)
        SyncCenter.libraryChanged()
    }

    /// What a version is, for telling versions apart: the file less what
    /// running it changes -- switch and keypad settings (OUTPUT_NUM), what
    /// registers and counters hold (CURRENT_VALUE) -- and less the app that
    /// wrote it. Gates, where they sit, wires, labels and settings remain.
    static func shape(of url: URL) -> String? {
        guard let text = try? String(contentsOf: url, encoding: .utf8) else { return nil }
        return text.split(separator: "\n").filter { line in
            let t = line.drop { $0 == " " }
            return !(t.hasPrefix("(lparam \"OUTPUT_NUM\"") || t.hasPrefix("(lparam \"CURRENT_VALUE\"")
                     || t.hasPrefix("(generator "))
        }.joined(separator: "\n")
    }

    /// Versions that are the same circuit as the one before them (from before
    /// versions were told apart that way) go to the library's trash, once.
    static func removeRepeats() {
        let flag = "cl.versionRepeatsRemoved"
        guard !UserDefaults.standard.bool(forKey: flag) else { return }
        let fm = FileManager.default
        let trash = root.appendingPathComponent(".Trash/repeated versions", isDirectory: true)
        for item in items() {
            var last: String?
            for v in versions(of: item).reversed() {   // oldest first
                let s = shape(of: v.url)
                if let s, s == last {
                    let dest = trash.appendingPathComponent(item.id, isDirectory: true)
                    try? fm.createDirectory(at: dest, withIntermediateDirectories: true)
                    try? fm.moveItem(at: v.url, to: dest.appendingPathComponent(v.url.lastPathComponent))
                } else {
                    last = s
                }
            }
        }
        UserDefaults.standard.set(true, forKey: flag)
    }

    private static func thin(_ item: LibraryItem) {
        var kept = Set<String>()
        let hour = DateFormatter(), day = DateFormatter()
        hour.dateFormat = "'h'yyyyMMddHH"
        day.dateFormat = "'d'yyyyMMdd"
        for v in versions(of: item) {
            let age = Date().timeIntervalSince(v.date)
            if age < 86400 { continue }
            let bucket = age < 7 * 86400 ? hour.string(from: v.date) : day.string(from: v.date)
            if kept.contains(bucket) {
                try? FileManager.default.removeItem(at: v.url)
                // and the note beside it, if sync left one
                try? FileManager.default.removeItem(at: v.url.deletingPathExtension().appendingPathExtension("txt"))
            } else {
                kept.insert(bucket)
            }
        }
    }

    /// A version's note, if it has one: where a version made by sync came
    /// from ("From Levi’s iPhone · edited 4 Oct 10:31").
    static func note(of version: URL) -> String? {
        let url = version.deletingPathExtension().appendingPathExtension("txt")
        guard let text = try? String(contentsOf: url, encoding: .utf8) else { return nil }
        let line = text.split(whereSeparator: \.isNewline).first.map(String.init)?.trimmingCharacters(in: .whitespaces)
        return line?.isEmpty == false ? line : nil
    }

    static func open(_ url: URL) {
        NSDocumentController.shared.openDocument(withContentsOf: url, display: true) { _, _, error in
            if let error { NSAlert(error: error).runModal() }
        }
    }

    /// An old version opens as a copy, so looking at it can't change it.
    static func openCopy(of version: LibraryVersion, named name: String) {
        let f = DateFormatter()
        f.dateFormat = "MMM d, h.mm a"
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let copy = dir.appendingPathComponent("\(name) (\(f.string(from: version.date))).cdl")
        do {
            try FileManager.default.copyItem(at: version.url, to: copy)
            open(copy)
        } catch {
            NSAlert(error: error).runModal()
        }
    }
}

// MARK: - Look

/// LibraryDialogs' colours.
private struct PickerLook {
    let dark: Bool
    static func rgb(_ r: Double, _ g: Double, _ b: Double) -> Color { Color(.sRGB, red: r / 255, green: g / 255, blue: b / 255) }
    var paper: Color { dark ? Self.rgb(28, 31, 37) : Self.rgb(250, 250, 252) }
    var ink: Color { dark ? Self.rgb(226, 230, 238) : Self.rgb(30, 33, 40) }
    var dim: Color { ink.opacity(0.55) }
    var sheet: Color { dark ? Self.rgb(22, 24, 29) : .white }
}

/// "Today at 3:42 PM", "Yesterday at 9:10 AM", "Mar 4 at 1:15 PM".
func friendlyTime(_ t: Date) -> String {
    let cal = Calendar.current
    let clock = t.formatted(date: .omitted, time: .shortened)
    if cal.isDateInToday(t) { return "Today at \(clock)" }
    if cal.isDateInYesterday(t) { return "Yesterday at \(clock)" }
    let f = DateFormatter()
    f.dateFormat = cal.component(.year, from: t) == cal.component(.year, from: Date()) ? "MMM d" : "MMM d, yyyy"
    return "\(f.string(from: t)) at \(clock)"
}

func agoText(_ t: Date) -> String {
    let s = Date().timeIntervalSince(t)
    if s < 60 { return "just now" }
    if s < 3600 { return "\(Int(s / 60)) min ago" }
    if s < 86400 { return "\(Int(s / 3600)) hr ago" }
    return "\(Int(s / 86400)) days ago"
}

/// How many gates a saved circuit holds (counting its "(gate " markers, as
/// the wx app does), remembered by file and date.
enum GateCounter {
    private static var cache: [String: Int] = [:]
    static func count(_ url: URL) -> Int? {
        let date = (try? url.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate) ?? .distantPast
        let key = url.path + "|\(date.timeIntervalSince1970)"
        if let n = cache[key] { return n }
        guard let data = try? Data(contentsOf: url), data.count < 8 << 20 else { return nil }
        let marker = Array("(gate ".utf8)
        var n = 0, i = 0
        let bytes = [UInt8](data)
        while i + marker.count <= bytes.count {
            if bytes[i] == marker[0] && Array(bytes[i..<(i + marker.count)]) == marker { n += 1; i += marker.count } else { i += 1 }
        }
        cache[key] = n
        return n
    }
    static func line(_ url: URL, _ tail: String) -> String {
        guard let n = count(url) else { return tail }
        return "\(n) gate\(n == 1 ? "" : "s") · \(tail)"
    }
}

private struct PickerRowData: Identifiable {
    let id: String
    let title: String
    let subtitle: String
    let badge: String?
}

/// The wx RowList: 62-point rows, a tile with a gate in the accent, the
/// name, a quiet second line, a badge, and an accent-tinted selected row.
private struct PickerList: View {
    let rows: [PickerRowData]
    @Binding var selection: Int
    let look: PickerLook
    let accent: Color
    var empty = "Nothing here yet."
    let onActivate: () -> Void
    /// The row under the pointer, by its id: rows can move (a list reloads)
    /// without the highlight staying behind on whatever takes the place.
    @State private var hover: String?

    var body: some View {
        ScrollViewReader { proxy in
            ScrollView {
                LazyVStack(spacing: 0) {
                    ForEach(Array(rows.enumerated()), id: \.element.id) { i, row in
                        rowView(i, row)
                    }
                }
                .padding(.vertical, 2)
            }
            .overlay { if rows.isEmpty { Text(empty).font(.system(size: 13)).foregroundStyle(look.dim).padding(.top, 40).frame(maxHeight: .infinity, alignment: .top) } }
            .onChange(of: selection) { _, s in
                guard rows.indices.contains(s) else { return }
                withAnimation(.easeOut(duration: 0.18)) { proxy.scrollTo(rows[s].id) }
            }
            .onChange(of: rows.map(\.id)) { _, ids in if let h = hover, !ids.contains(h) { hover = nil } }
        }
        .background(look.paper)
    }

    private func rowView(_ i: Int, _ row: PickerRowData) -> some View {
        let sel = i == selection, hot = hover == row.id
        let next = rows.indices.contains(i + 1) ? rows[i + 1].id : nil
        return HStack(spacing: 16) {
            LibraryGateTile(accent: accent, on: sel).frame(width: 40, height: 40)
            VStack(alignment: .leading, spacing: 3) {
                Text(row.title).font(.system(size: 13, weight: .bold)).foregroundStyle(look.ink).lineLimit(1)
                Text(row.subtitle).font(.system(size: 11)).foregroundStyle(look.dim).lineLimit(1)
            }
            Spacer(minLength: 8)
            if let b = row.badge {
                Text(b).font(.system(size: 9, weight: .bold)).foregroundStyle(accent)
                    .padding(.horizontal, 8).frame(height: 20)
                    .background(Capsule().fill(accent.opacity(0.18)))
            }
        }
        .padding(.leading, 12).padding(.trailing, 16)
        .frame(height: 62)
        .background(
            RoundedRectangle(cornerRadius: 12)
                .fill(sel ? accent.opacity(look.dark ? 0.26 : 0.16) : look.ink.opacity(hot ? 0.06 : 0))
                .padding(.vertical, 4)
        )
        .overlay(alignment: .bottom) {
            if let next, !sel && !hot && selection != i + 1 && hover != next {
                Rectangle().fill(look.ink.opacity(0.08)).frame(height: 1).padding(.leading, 68).padding(.trailing, 12)
            }
        }
        .padding(.horizontal, 8)
        .contentShape(Rectangle())
        .id(row.id)
        .onHover { h in if h { hover = row.id } else if hover == row.id { hover = nil } }
        .onTapGesture(count: 2) { selection = i; onActivate() }
        .simultaneousGesture(TapGesture().onEnded { selection = i })
    }
}

/// A small rounded tile with a logic-gate silhouette, tinted by the accent.
private struct LibraryGateTile: View {
    let accent: Color
    let on: Bool
    var body: some View {
        Canvas { ctx, size in
            ctx.fill(Path(roundedRect: CGRect(origin: .zero, size: size), cornerRadius: 11), with: .color(accent.opacity(on ? 0.22 : 0.13)))
            let s = size.width / 40, cx = size.width / 2, cy = size.height / 2
            var body = Path()
            body.move(to: CGPoint(x: cx - 8 * s, y: cy - 8 * s))
            body.addLine(to: CGPoint(x: cx - 1 * s, y: cy - 8 * s))
            body.addCurve(to: CGPoint(x: cx - 1 * s, y: cy + 8 * s), control1: CGPoint(x: cx + 9 * s, y: cy - 8 * s), control2: CGPoint(x: cx + 9 * s, y: cy + 8 * s))
            body.addLine(to: CGPoint(x: cx - 8 * s, y: cy + 8 * s))
            body.closeSubpath()
            ctx.stroke(body, with: .color(accent.opacity(0.95)), lineWidth: 1.6 * s)
            var leads = Path()
            leads.move(to: CGPoint(x: cx - 14 * s, y: cy - 4.5 * s)); leads.addLine(to: CGPoint(x: cx - 8 * s, y: cy - 4.5 * s))
            leads.move(to: CGPoint(x: cx - 14 * s, y: cy + 4.5 * s)); leads.addLine(to: CGPoint(x: cx - 8 * s, y: cy + 4.5 * s))
            leads.move(to: CGPoint(x: cx + 6.5 * s, y: cy)); leads.addLine(to: CGPoint(x: cx + 14 * s, y: cy))
            ctx.stroke(leads, with: .color(accent.opacity(0.7)), lineWidth: 1.6 * s)
        }
    }
}

/// Keys for a picker window, whatever has the focus in it (wx: CHAR_HOOK).
private final class PickerKeys {
    private var monitor: Any?
    func install(window id: String, _ handle: @escaping (NSEvent) -> Bool) {
        guard monitor == nil else { return }
        monitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { e in
            guard e.window?.identifier?.rawValue.contains(id) == true else { return e }
            return handle(e) ? nil : e
        }
    }
    func remove() { if let m = monitor { NSEvent.removeMonitor(m) }; monitor = nil }
}

/// The circuit file behind the front window, if it has one.
@MainActor
private func frontFileURL() -> URL? {
    guard let w = CanvasController.front?.view?.window else { return nil }
    return NSDocumentController.shared.document(for: w)?.fileURL
}

// MARK: - Your Circuits

struct LibraryView: View {
    @ObservedObject private var prefs = Prefs.shared
    @ObservedObject private var sync: SyncCenter
    @Environment(\.dismiss) private var dismiss
    @State private var items: [LibraryItem] = []
    /// The chosen circuit, by its id, so it stays chosen when the list
    /// reloads; nil means the first one shown.
    @State private var selectedID: String?
    @State private var search = ""
    @State private var keys = PickerKeys()
    @FocusState private var searchFocused: Bool

    /// `sync` is SyncCenter.shared but for pictures (SyncRender).
    init(sync: SyncCenter? = nil) { self.sync = sync ?? .shared }

    private var look: PickerLook { PickerLook(dark: prefs.dark) }
    private var accent: Color { prefs.accentColor(dark: prefs.dark) }
    private var shown: [LibraryItem] {
        let q = search.trimmingCharacters(in: .whitespaces)
        return q.isEmpty ? items : items.filter { $0.name.localizedCaseInsensitiveContains(q) }
    }
    private var selected: LibraryItem? { shown.first { $0.id == selectedID } ?? shown.first }
    private var selectionIndex: Binding<Int> {
        Binding(get: { shown.firstIndex { $0.id == selected?.id } ?? 0 },
                set: { i in if shown.indices.contains(i) { selectedID = shown[i].id } })
    }

    var body: some View {
        let open = Library.openIDs()
        VStack(alignment: .leading, spacing: 0) {
            Text("Your Circuits").font(.system(size: 19, weight: .bold)).foregroundStyle(look.ink)
                .padding(.horizontal, 22).padding(.top, 22)
            Text("Everything here saves itself. Use Import to bring in a .cdl file, and File \u{25B8} Export to get one out.")
                .font(.system(size: 13)).foregroundStyle(look.dim)
                .padding(.horizontal, 22).padding(.top, 10)
            TextField("Search circuits", text: $search)
                .textFieldStyle(.roundedBorder)
                .focused($searchFocused)
                .padding(.horizontal, 22).padding(.top, 16)
            PickerList(rows: shown.map { item in
                PickerRowData(id: item.id, title: item.name,
                              subtitle: sync.problems[item.id] ?? GateCounter.line(item.circuit, friendlyTime(item.modified)),
                              badge: open.contains(item.id) ? "OPEN" : nil)
            }, selection: selectionIndex, look: look, accent: accent,
               empty: items.isEmpty ? "Nothing here yet." : "No circuits match.", onActivate: openSelected)
                .padding(.horizontal, 14).padding(.top, 14).padding(.bottom, 6)
            SyncLine(center: sync, look: look)
                .padding(.horizontal, 22).padding(.bottom, 12)
            HStack(spacing: 8) {
                Button("Import File…", action: importFile).help("Bring in a .cdl file  (⌘I)")
                Button("Rename…", action: rename).help("Rename the selected circuit  (⌘R)").disabled(selected == nil)
                Button("Delete…", action: delete).help("Delete the selected circuit  (⌘⌫)").disabled(selected == nil)
                Spacer()
                Button("Cancel") { dismiss() }.keyboardShortcut(.cancelAction)
                Button("Open", action: openSelected).keyboardShortcut(.defaultAction).disabled(selected == nil)
            }
            .padding(.horizontal, 22).padding(.bottom, 22)
        }
        .frame(minWidth: 520, idealWidth: 600, minHeight: 420, idealHeight: 540)
        .background(look.paper)
        .background(OverFrontCircuit())
        .preferredColorScheme(prefs.dark ? .dark : .light)
        .onAppear {
            // Newest first each time it opens; while it's open, the order
            // holds (a circuit saving itself doesn't jump it to the top).
            items = Library.items()
            selectedID = nil
            searchFocused = true
            keys.install(window: "library") { e in handleKey(e) }
        }
        .onDisappear { keys.remove() }
        .onChange(of: search) { _, _ in selectedID = nil }
        .onReceive(NotificationCenter.default.publisher(for: NSWindow.didBecomeKeyNotification)) { _ in reload() }
        .onReceive(NotificationCenter.default.publisher(for: .clLibraryChanged)) { _ in reload() }
    }

    /// The list again, in the order it's in: renamed circuits take their new
    /// names, deleted ones go, new ones come in at the top.
    private func reload() {
        let fresh = Library.items()
        let byID = Dictionary(fresh.map { ($0.id, $0) }, uniquingKeysWith: { a, _ in a })
        let kept = items.compactMap { byID[$0.id] }
        let known = Set(items.map(\.id))
        items = fresh.filter { !known.contains($0.id) } + kept
    }

    private func openSelected() {
        guard let item = selected else { return }
        Library.open(item.circuit)
        dismiss()
    }
    private func importFile() {
        dismiss()
        DispatchQueue.main.async { NSDocumentController.shared.openDocument(nil) }
    }

    private func rename() {
        guard let item = selected else { return }
        let alert = NSAlert()
        alert.messageText = "Rename Circuit"
        alert.informativeText = "The name it has in Your Circuits."
        let field = NSTextField(string: item.name)
        field.frame = NSRect(x: 0, y: 0, width: 260, height: 24)
        alert.accessoryView = field
        alert.addButton(withTitle: "Rename")
        alert.addButton(withTitle: "Cancel")
        alert.window.initialFirstResponder = field
        Library.present(alert) { ok in
            let name = field.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
            guard ok, !name.isEmpty else { return }
            Library.rename(item, to: name)
            reload()
        }
    }

    /// Asks first. A circuit that's open closes, then it and its versions
    /// go to the library's trash (where the wx app puts deleted circuits).
    private func delete() {
        guard let item = selected else { return }
        let index = shown.firstIndex { $0.id == item.id } ?? 0
        let isOpen = Library.openIDs().contains(item.id)
        let alert = NSAlert()
        alert.messageText = "Delete \u{201C}\(item.name)\u{201D}?"
        alert.informativeText = (isOpen ? "It's open, so its window will close. " : "") + "It and all its versions will be deleted."
        alert.addButton(withTitle: "Delete")
        alert.addButton(withTitle: "Cancel")
        alert.buttons.first?.hasDestructiveAction = true
        Library.present(alert) { ok in
            guard ok else { return }
            if Library.delete(item) {
                reload()
                // The next one down is chosen (or the last).
                let rows = shown
                selectedID = rows.isEmpty ? nil : rows[min(index, rows.count - 1)].id
            } else {
                let fail = NSAlert()
                fail.messageText = "Couldn\u{2019}t delete \u{201C}\(item.name)\u{201D}."
                Library.present(fail) { _ in }
            }
        }
    }

    /// Type to search, arrows to move, Return to open, wherever the focus is.
    private func handleKey(_ e: NSEvent) -> Bool {
        let cmd = e.modifierFlags.contains(.command)
        let n = shown.count
        let i = selectionIndex.wrappedValue
        func go(_ to: Int) { if n > 0 { selectionIndex.wrappedValue = max(0, min(n - 1, to)) } }
        switch e.keyCode {
        case 125: go(i + 1); return true            // ↓
        case 126: go(i - 1); return true            // ↑
        case 121: go(i + 8); return true            // page down
        case 116: go(i - 8); return true            // page up
        case 115 where cmd: go(0); return true      // ⌘home
        case 119 where cmd: go(n - 1); return true  // ⌘end
        case 51 where cmd, 117 where cmd: delete(); return true            // ⌘⌫
        default: break
        }
        guard cmd, let k = e.charactersIgnoringModifiers?.lowercased() else { return false }
        switch k {
        case "r": rename(); return true
        case "i": importFile(); return true
        case "f": searchFocused = true; return true
        default: return false
        }
    }
}

/// Under Your Circuits' list (5.2): how syncing is going (or a notice from
/// the last minute) and Sync Now; while sync is off, a quiet way to Settings.
private struct SyncLine: View {
    @ObservedObject var center: SyncCenter
    let look: PickerLook

    var body: some View {
        HStack(spacing: 6) {
            if center.enabled || center.kind == Int(CL_SYNC_GONE) {
                Image(systemName: center.symbol).font(.system(size: 11)).foregroundStyle(look.dim)
                Text(center.line).font(.system(size: 11)).foregroundStyle(look.dim)
                    .lineLimit(1).truncationMode(.tail).help(center.line)
            }
            Spacer(minLength: 6)
            if center.enabled {
                Button { center.syncNow() } label: {
                    Image(systemName: "arrow.triangle.2.circlepath").font(.system(size: 11))
                }
                .buttonStyle(.borderless).help("Sync Now")
            } else {
                Button("Sync\u{2026}") { PrefsWindow.shared.show(page: .sync) }
                    .buttonStyle(.link).font(.system(size: 11))
                    .help("Keep Your Circuits the same on your other devices (Settings \u{203A} Sync)")
            }
        }
        .frame(height: 18)
    }
}

// MARK: - Version History

/// The front circuit's saved versions, a picture of the chosen one beside
/// the list (wx ShowVersionHistoryDialog). A circuit from Your Circuits uses
/// the library's versions; any other file, the ones macOS keeps.
struct VersionHistoryView: View {
    @ObservedObject private var prefs = Prefs.shared
    @Environment(\.dismiss) private var dismiss
    @State private var fileURL: URL?
    @State private var versions: [(url: URL, date: Date, fileVersion: NSFileVersion?)] = []
    @State private var selection = 0
    @State private var keys = PickerKeys()
    @State private var problem: String?

    private var look: PickerLook { PickerLook(dark: prefs.dark) }
    private var accent: Color { prefs.accentColor(dark: prefs.dark) }
    private var libraryItem: LibraryItem? {
        guard let url = fileURL, url.lastPathComponent == "circuit.cdl" else { return nil }
        let folder = url.deletingLastPathComponent()
        return Library.items().first { $0.folder.standardizedFileURL == folder.standardizedFileURL }
    }
    private var circuitName: String {
        libraryItem?.name ?? fileURL?.deletingPathExtension().lastPathComponent ?? "Untitled"
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text(circuitName).font(.system(size: 19, weight: .bold)).foregroundStyle(look.ink)
                .padding(.horizontal, 22).padding(.top, 22)
            Text(versions.isEmpty
                 ? "No earlier versions yet. Your work saves itself as you go; a version is kept each time you press ⌘S, when you come back after a break, and every half hour while you work."
                 : "Pick a version to see it. Restoring keeps your current one too, so you can always come back.")
                .font(.system(size: 13)).foregroundStyle(look.dim)
                .padding(.horizontal, 22).padding(.top, 10)
            HStack(spacing: 14) {
                VersionPreview(url: versions.indices.contains(selection) ? versions[selection].url : nil, look: look, dark: prefs.dark)
                PickerList(rows: versions.enumerated().map { i, v in
                    PickerRowData(id: v.url.path, title: friendlyTime(v.date),
                                  subtitle: Library.note(of: v.url).map { "\($0) · \(agoText(v.date))" } ?? GateCounter.line(v.url, agoText(v.date)),
                                  badge: i == 0 ? "NEWEST" : nil)
                }, selection: $selection, look: look, accent: accent, empty: "No versions yet.", onActivate: restore)
                    .frame(width: 300)
            }
            .padding(22)
            HStack(spacing: 8) {
                Button("Export Copy…", action: exportCopy).help("Save a copy of the selected version  (⌘E)").disabled(versions.isEmpty)
                Spacer()
                Button("Close") { dismiss() }.keyboardShortcut(.cancelAction)
                Button("Restore", action: restore).help("Bring the selected version back  (↩)")
                    .keyboardShortcut(.defaultAction).disabled(versions.isEmpty)
            }
            .padding(.horizontal, 22).padding(.bottom, 22)
        }
        .frame(minWidth: 760, idealWidth: 940, minHeight: 480, idealHeight: 620)
        .background(look.paper)
        .background(OverFrontCircuit())
        .preferredColorScheme(prefs.dark ? .dark : .light)
        .onAppear {
            load()
            keys.install(window: "versions") { e in
                switch e.keyCode {
                case 125: selection = min(versions.count - 1, selection + 1); return true
                case 126: selection = max(0, selection - 1); return true
                case 121: selection = min(versions.count - 1, selection + 8); return true
                case 116: selection = max(0, selection - 8); return true
                default: break
                }
                if e.modifierFlags.contains(.command), e.charactersIgnoringModifiers?.lowercased() == "e" { exportCopy(); return true }
                return false
            }
        }
        .onDisappear { keys.remove() }
        .alert("Version History", isPresented: Binding(get: { problem != nil }, set: { if !$0 { problem = nil } })) {
            Button("OK", role: .cancel) {}
        } message: { Text(problem ?? "") }
    }

    private func load() {
        fileURL = frontFileURL()
        selection = 0
        guard let url = fileURL else { versions = []; return }
        if let item = libraryItem {
            versions = Library.versions(of: item).map { ($0.url, $0.date, nil) }
        } else {
            versions = (NSFileVersion.otherVersionsOfItem(at: url) ?? [])
                .map { ($0.url, $0.modificationDate ?? .distantPast, Optional($0)) }
                .sorted { $0.1 > $1.1 }
        }
    }

    /// The chosen version becomes the circuit; the one it replaces is kept
    /// as a version first, so nothing is lost.
    private func restore() {
        guard versions.indices.contains(selection), let url = fileURL,
              let w = CanvasController.front?.view?.window,
              let doc = NSDocumentController.shared.document(for: w) else { return }
        let v = versions[selection]
        do {
            if doc.isDocumentEdited { doc.save(nil) }
            if let fv = v.fileVersion {
                try fv.replaceItem(at: url)
            } else {
                if let item = libraryItem {
                    let f = DateFormatter()
                    f.dateFormat = "yyyyMMdd-HHmmss"
                    try? FileManager.default.createDirectory(at: item.versionsFolder, withIntermediateDirectories: true)
                    try? FileManager.default.copyItem(at: url, to: item.versionsFolder.appendingPathComponent(f.string(from: Date()) + ".cdl"))
                }
                let data = try Data(contentsOf: v.url)
                try data.write(to: url, options: .atomic)
            }
            try doc.revert(toContentsOf: url, ofType: doc.fileType ?? "org.cedarlogic.cdl")
            SyncCenter.libraryChanged()
            dismiss()
        } catch {
            problem = "Couldn't restore that version: \(error.localizedDescription)"
        }
    }

    private func exportCopy() {
        guard versions.indices.contains(selection) else { return }
        let v = versions[selection]
        let panel = NSSavePanel()
        let f = DateFormatter()
        f.dateFormat = "MMM d HH-mm"
        panel.nameFieldStringValue = "\(circuitName) (\(f.string(from: v.date))).cdl"
        panel.allowedContentTypes = [.init(filenameExtension: "cdl") ?? .data]
        guard panel.runModal() == .OK, let dest = panel.url else { return }
        do {
            if FileManager.default.fileExists(atPath: dest.path) { try FileManager.default.removeItem(at: dest) }
            try FileManager.default.copyItem(at: v.url, to: dest)
        } catch {
            problem = "Couldn't save a copy there. Try another folder."
        }
    }
}

/// A picture of one saved version, on a page of the canvas's colour. The
/// version loads as its own document just long enough to be drawn.
private struct VersionPreview: View {
    let url: URL?
    let look: PickerLook
    let dark: Bool
    @State private var image: CGImage?
    @State private var busy = false
    private static var cache: [String: CGImage] = [:]

    var body: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 12).fill(look.sheet)
                .overlay(RoundedRectangle(cornerRadius: 12).strokeBorder(look.ink.opacity(0.12)))
            if let image {
                Image(decorative: image, scale: 2).resizable().scaledToFit().padding(12)
                    .transition(.opacity)
            } else {
                Text(busy ? "Drawing this version…" : (url == nil ? "" : "No preview for this version."))
                    .font(.system(size: 12)).foregroundStyle(look.dim)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .animation(.easeOut(duration: 0.15), value: image != nil)
        .onAppear { render() }
        .onChange(of: url) { _, _ in render() }
        .onChange(of: dark) { _, _ in render() }
    }

    private func render() {
        guard let url else { image = nil; return }
        let key = url.path + (dark ? "|d" : "|l")
        if let hit = Self.cache[key] { image = hit; return }
        image = nil
        busy = true
        DispatchQueue.main.async {
            defer { busy = false }
            guard let data = try? Data(contentsOf: url), let doc = try? CoreDocument(data: data) else { return }
            let w = 1200, h = 820
            guard let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: 0,
                                      space: CGColorSpace(name: CGColorSpace.sRGB)!,
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return }
            // Points, top-left origin, at 2x: what the drawing calls expect.
            ctx.translateBy(x: 0, y: CGFloat(h))
            ctx.scaleBy(x: 2, y: -2)
            _ = cl_document_draw_fitted(doc.handle, 0, ctx, Double(w) / 2, Double(h) / 2, 16, 2,
                                        Int32(dark ? CL_STYLE_DARK : CL_STYLE_LIGHT))
            if let img = ctx.makeImage() {
                Self.cache[key] = img
                if self.url == url { image = img }
            }
        }
    }
}

/// Opens a panel-like window over the circuit you're in: centred on it (on
/// its screen, and in its full-screen space), each time it opens, instead
/// of wherever it was last -- which could be another monitor. It can still
/// be moved while it's open.
struct OverFrontCircuit: NSViewRepresentable {
    final class Probe: NSView {
        override func viewDidMoveToWindow() {
            super.viewDidMoveToWindow()
            guard let w = window else { return }
            w.isRestorable = false
            w.setFrameAutosaveName("")
            w.collectionBehavior.insert([.fullScreenAuxiliary, .moveToActiveSpace])
            w.collectionBehavior.remove(.fullScreenPrimary)
            place(w)
            // SwiftUI may still size or restore it once shown: again after.
            DispatchQueue.main.async { [weak w] in if let w { self.place(w) } }
        }
        func place(_ w: NSWindow) {
            guard let front = CanvasController.front?.view?.window, front !== w else { w.center(); return }
            let f = front.frame
            var r = w.frame
            r.origin = NSPoint(x: (f.midX - r.width / 2).rounded(), y: (f.midY - r.height / 2).rounded())
            if let vis = (front.screen ?? NSScreen.main)?.visibleFrame {
                r.origin.x = min(max(r.origin.x, vis.minX), vis.maxX - r.width)
                r.origin.y = min(max(r.origin.y, vis.minY), vis.maxY - r.height)
            }
            w.setFrame(r, display: true)
        }
    }
    func makeNSView(context: Context) -> NSView { Probe() }
    func updateNSView(_ nsView: NSView, context: Context) {}
}

extension Notification.Name {
    /// A circuit in Your Circuits was renamed or deleted.
    static let clLibraryChanged = Notification.Name("clLibraryChanged")
}
