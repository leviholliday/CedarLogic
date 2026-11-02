// My Parts: a selection saved under a name you give it, to use again. It
// sits in the side panel's "My Parts" (drag it out, or click it) and in
// Add a Gate (the A key), and drops in as a copy of those gates and wires --
// ordinary gates, so any CedarLogic can open the circuit.
//
// Each part is a folder in ~/Library/Application Support/CedarLogic/Parts
// holding name.txt and part.txt (the clipboard text of the selection).

import AppKit
import SwiftUI

struct SavedPart: Identifiable, Hashable {
    let id: String
    let name: String
    let folder: URL
    var text: String? { try? String(contentsOf: folder.appendingPathComponent("part.txt"), encoding: .utf8) }
    /// How the palette and Add a Gate name it: a "gate" whose name starts
    /// with "part:".
    var gate: GateLibrary.Gate { GateLibrary.Gate(name: MyParts.prefix + id, caption: name) }
}

@MainActor
final class MyParts: ObservableObject {
    static let shared = MyParts()
    nonisolated static let prefix = "part:"
    @Published private(set) var parts: [SavedPart] = []

    static var root: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("CedarLogic/Parts", isDirectory: true)
    }

    private init() { reload() }

    func reload() {
        let fm = FileManager.default
        let ids = (try? fm.contentsOfDirectory(atPath: Self.root.path)) ?? []
        parts = ids.filter { !$0.hasPrefix(".") }.compactMap { id in
            let folder = Self.root.appendingPathComponent(id, isDirectory: true)
            guard fm.fileExists(atPath: folder.appendingPathComponent("part.txt").path) else { return nil }
            let name = (try? String(contentsOf: folder.appendingPathComponent("name.txt"), encoding: .utf8))?
                .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
            return SavedPart(id: id, name: name.isEmpty ? "Untitled Part" : name, folder: folder)
        }
        .sorted { $0.name.localizedStandardCompare($1.name) == .orderedAscending }
    }

    func part(named gateName: String) -> SavedPart? {
        guard gateName.hasPrefix(Self.prefix) else { return nil }
        let id = String(gateName.dropFirst(Self.prefix.count))
        return parts.first { $0.id == id }
    }

    @discardableResult
    func save(text: String, named name: String) -> Bool {
        let f = DateFormatter()
        f.dateFormat = "yyyyMMdd-HHmmss"
        let id = "\(f.string(from: Date()))-\(Int.random(in: 1000...99999))"
        let folder = Self.root.appendingPathComponent(id, isDirectory: true)
        do {
            try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
            try name.write(to: folder.appendingPathComponent("name.txt"), atomically: true, encoding: .utf8)
            try text.write(to: folder.appendingPathComponent("part.txt"), atomically: true, encoding: .utf8)
        } catch { return false }
        reload()
        return true
    }

    func rename(_ part: SavedPart, to name: String) {
        try? name.write(to: part.folder.appendingPathComponent("name.txt"), atomically: true, encoding: .utf8)
        TileCache.forget(MyParts.prefix + part.id)
        reload()
    }

    /// Into the Trash, so a mistake can be taken back from the Finder.
    func delete(_ part: SavedPart) {
        try? FileManager.default.trashItem(at: part.folder, resultingItemURL: nil)
        TileCache.forget(MyParts.prefix + part.id)
        reload()
    }

    /// A picture of the part: its gates pasted onto a scratch circuit and
    /// drawn fitted, as the palette's tiles are.
    static func draw(_ part: SavedPart, in ctx: CGContext, width: CGFloat, height: CGFloat, scale: CGFloat, dark: Bool) {
        guard let text = part.text else { return }
        let doc = CoreDocument()
        guard doc.paste(text, page: 0, at: .zero, shift: true) != nil else { return }
        cl_edit_select_none(doc.handle, 0)
        _ = cl_document_draw_fitted(doc.handle, 0, ctx, width, height, 2, scale, Int32(dark ? CL_STYLE_DARK : CL_STYLE_LIGHT))
    }

    /// Asks for a name, then keeps the selection as a part.
    static func saveSelection(of canvas: CanvasController) {
        guard let document = canvas.document else { return }
        let text = document.copySelection(page: canvas.page)
        guard !text.isEmpty else {
            canvas.note("Select the gates for your part first.")
            return
        }
        guard let name = askName(title: "Save as Part",
                                 message: "Name your part. It'll be in the side panel under My Parts, and in Add a Gate (A).",
                                 initial: "", button: "Save") else { return }
        if shared.save(text: text, named: name) {
            canvas.note("Saved \u{201C}\(name)\u{201D} to My Parts.")
        } else {
            canvas.note("Couldn't save that part.")
        }
    }

    static func renameAsking(_ part: SavedPart) {
        if let name = askName(title: "Rename Part", message: "", initial: part.name, button: "Rename") {
            shared.rename(part, to: name)
        }
    }

    /// Asks first; the part goes to the Trash.
    static func deleteAsking(_ part: SavedPart) {
        let alert = NSAlert()
        alert.messageText = "Delete \u{201C}\(part.name)\u{201D}?"
        alert.informativeText = "It goes from My Parts to the Trash. Circuits that already use it keep their copy."
        alert.addButton(withTitle: "Delete")
        alert.addButton(withTitle: "Cancel")
        alert.buttons.first?.hasDestructiveAction = true
        if alert.runModal() == .alertFirstButtonReturn { shared.delete(part) }
    }

    /// A small name prompt; nil when cancelled or left empty.
    static func askName(title: String, message: String, initial: String, button: String) -> String? {
        let alert = NSAlert()
        alert.messageText = title
        alert.informativeText = message
        alert.addButton(withTitle: button)
        alert.addButton(withTitle: "Cancel")
        let field = NSTextField(frame: NSRect(x: 0, y: 0, width: 260, height: 24))
        field.stringValue = initial
        field.placeholderString = "Name"
        alert.accessoryView = field
        alert.window.initialFirstResponder = field
        guard alert.runModal() == .alertFirstButtonReturn else { return nil }
        let name = field.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        return name.isEmpty ? nil : name
    }
}
