// Just enough of the Mac app for Templates.swift to build its templates
// headless (no windows): the engine document, the student's name (none),
// and the Step Clock key (K, the default).
import Foundation
import AppKit

final class CoreDocument {
    let handle: OpaquePointer
    init() { handle = cl_document_new() }
    deinit { cl_document_close(handle) }
    func saveText() -> String { String(cString: cl_document_save_text(handle)) }
    func libraryName(ofGate gate: Int) -> String { String(cString: cl_gate_library_name(handle, gate)) }
}

final class Prefs {
    static let shared = Prefs()
    var studentName = ""
}

struct KeyCombo {
    var key: String
    var label: String { key.uppercased() }
}

enum ShortcutAction { case stepClock }

final class ShortcutStore {
    static let shared = ShortcutStore()
    func combo(_ a: ShortcutAction) -> KeyCombo? { KeyCombo(key: "k") }
}

final class CanvasController {
    var document: CoreDocument? { nil }
    var documentTitle: String { "" }
    func note(_ s: String) {}
}

enum MyParts {
    static func askName(title: String, message: String, initial: String, button: String) -> String? { nil }
}

func friendlyTime(_ d: Date) -> String { "" }
