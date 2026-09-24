// Your Circuits: the same library the wx app keeps, in
// ~/Library/Application Support/CedarLogic/Library -- one folder per circuit
// holding name.txt, circuit.cdl and versions/<timestamp>.cdl. Both apps read
// and write it, so a circuit saved in one shows up in the other.

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

    static func versions(of item: LibraryItem) -> [LibraryVersion] {
        let fm = FileManager.default
        guard let files = try? fm.contentsOfDirectory(at: item.versionsFolder, includingPropertiesForKeys: [.contentModificationDateKey]) else { return [] }
        return files.filter { $0.pathExtension == "cdl" }.map { url in
            let date = (try? url.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate) ?? .distantPast
            return LibraryVersion(url: url, date: date)
        }
        .sorted { $0.date > $1.date }
    }

    /// A new, empty circuit in the library, named `name`.
    static func create(named name: String) throws -> LibraryItem {
        let f = DateFormatter()
        f.dateFormat = "yyyyMMdd-HHmmss"
        let id = "\(f.string(from: Date()))-\(Int.random(in: 1000...99999))"
        let folder = root.appendingPathComponent(id, isDirectory: true)
        try FileManager.default.createDirectory(at: folder.appendingPathComponent("versions"), withIntermediateDirectories: true)
        try name.write(to: folder.appendingPathComponent("name.txt"), atomically: true, encoding: .utf8)
        let empty = CoreDocument()
        try empty.saveText().write(to: folder.appendingPathComponent("circuit.cdl"), atomically: true, encoding: .utf8)
        return LibraryItem(id: id, name: name, folder: folder, modified: Date())
    }

    static func rename(_ item: LibraryItem, to name: String) {
        try? name.write(to: item.folder.appendingPathComponent("name.txt"), atomically: true, encoding: .utf8)
    }

    /// Into the library's own trash folder, where the wx app puts deleted
    /// circuits too.
    static func moveToTrash(_ item: LibraryItem) {
        let trash = root.appendingPathComponent(".Trash", isDirectory: true)
        try? FileManager.default.createDirectory(at: trash, withIntermediateDirectories: true)
        try? FileManager.default.moveItem(at: item.folder, to: trash.appendingPathComponent(item.id))
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

struct LibraryView: View {
    @State private var items: [LibraryItem] = []
    @State private var selection: LibraryItem.ID?
    @State private var search = ""
    @State private var renaming: LibraryItem?
    @State private var newName = ""
    @State private var creating = false

    private var shown: [LibraryItem] {
        search.isEmpty ? items : items.filter { $0.name.localizedCaseInsensitiveContains(search) }
    }
    private var selected: LibraryItem? { items.first { $0.id == selection } }

    var body: some View {
        NavigationSplitView {
            List(shown, selection: $selection) { item in
                VStack(alignment: .leading, spacing: 2) {
                    Text(item.name)
                    Text(item.modified, style: .relative).font(.caption).foregroundStyle(.secondary)
                }
                .tag(item.id)
                .contextMenu {
                    Button("Open") { Library.open(item.circuit) }
                    Button("Rename…") { newName = item.name; renaming = item }
                    Divider()
                    Button("Move to Trash", role: .destructive) { Library.moveToTrash(item); reload() }
                }
                .onTapGesture(count: 2) { Library.open(item.circuit) }
            }
            .searchable(text: $search, placement: .sidebar)
            .navigationSplitViewColumnWidth(min: 220, ideal: 260)
            .toolbar {
                ToolbarItem {
                    Button { newName = "Untitled"; creating = true } label: { Label("New Circuit", systemImage: "plus") }
                }
            }
        } detail: {
            if let item = selected {
                LibraryDetail(item: item)
            } else {
                Text(items.isEmpty ? "No circuits in your library yet." : "Select a circuit.")
                    .foregroundStyle(.secondary)
            }
        }
        .frame(minWidth: 640, minHeight: 400)
        .onAppear(perform: reload)
        .onReceive(NotificationCenter.default.publisher(for: NSWindow.didBecomeKeyNotification)) { _ in reload() }
        .alert("Rename Circuit", isPresented: Binding(get: { renaming != nil }, set: { if !$0 { renaming = nil } })) {
            TextField("Name", text: $newName)
            Button("Rename") { if let r = renaming { Library.rename(r, to: newName); reload() } }
            Button("Cancel", role: .cancel) {}
        }
        .alert("New Circuit", isPresented: $creating) {
            TextField("Name", text: $newName)
            Button("Create") {
                if let item = try? Library.create(named: newName.isEmpty ? "Untitled" : newName) {
                    reload()
                    Library.open(item.circuit)
                }
            }
            Button("Cancel", role: .cancel) {}
        }
    }

    private func reload() { items = Library.items() }
}

private struct LibraryDetail: View {
    let item: LibraryItem

    var body: some View {
        let versions = Library.versions(of: item)
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                VStack(alignment: .leading) {
                    Text(item.name).font(.title2.weight(.semibold))
                    Text("Changed \(item.modified.formatted(date: .abbreviated, time: .shortened))")
                        .foregroundStyle(.secondary)
                }
                Spacer()
                Button("Open") { Library.open(item.circuit) }
                    .keyboardShortcut(.defaultAction)
            }
            Divider()
            Text("Saved Versions").font(.headline)
            if versions.isEmpty {
                Text("No saved versions yet.").foregroundStyle(.secondary)
            } else {
                List(versions) { v in
                    HStack {
                        Text(v.date.formatted(date: .abbreviated, time: .shortened))
                        Spacer()
                        Button("Open a Copy") { Library.openCopy(of: v, named: item.name) }
                    }
                }
            }
            Text("Opening a version opens a copy, so the version itself stays as it was. Circuits you open also keep their own history under File > Revert To > Browse All Versions.")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .padding()
    }
}
