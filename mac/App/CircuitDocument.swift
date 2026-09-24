// A .cdl file opened in the app. The circuit itself lives in the C++ engine
// (CedarCore); this holds the handle and closes it when the last reference goes.

import SwiftUI
import UniformTypeIdentifiers

extension UTType {
    /// CedarLogic circuits (.cdl). The wx app never declared a type, so this
    /// app declares it (Info.plist) for both.
    static let cedarLogicCircuit = UTType(importedAs: "edu.cedarville.cedarlogic.circuit", conformingTo: .data)
}

/// The engine's copy of an open circuit.
final class CoreDocument: ObservableObject {
    let handle: OpaquePointer

    init(data: Data) throws {
        var error = [CChar](repeating: 0, count: 512)
        let opened: OpaquePointer? = data.withUnsafeBytes { raw in
            let text = raw.bindMemory(to: CChar.self).baseAddress
            return cl_document_open_text(text, raw.count, &error, Int32(error.count))
        }
        guard let opened else {
            throw CocoaError(.fileReadCorruptFile, userInfo: [NSLocalizedDescriptionKey: String(cString: error)])
        }
        handle = opened
    }

    /// A new, empty circuit.
    init() { handle = cl_document_new() }

    deinit { cl_document_close(handle) }

    /// The circuit as .cdl text, for saving.
    func saveText() -> String { String(cString: cl_document_save_text(handle)) }

    // Pages.
    func addPage() -> Int { Int(cl_document_add_page(handle)) }
    func renamePage(_ page: Int, to name: String) { cl_document_rename_page(handle, Int32(page), name) }
    func deletePage(_ page: Int) { cl_document_delete_page(handle, Int32(page)) }
    var undoCount: Int { Int(cl_edit_undo_count(handle)) }

    var pageCount: Int { Int(cl_document_page_count(handle)) }

    func pageName(_ page: Int) -> String {
        let name = String(cString: cl_document_page_name(handle, Int32(page)))
        return name.isEmpty ? "Page \(page + 1)" : name
    }

    // Simulation.
    /// Advance by wall time; returns whether anything changed and whether a
    /// part asked to pause.
    func tick(elapsedMs: Double) -> (changed: Bool, paused: Bool) {
        let f = Int(cl_document_tick(handle, elapsedMs))
        return (f & CL_TICK_CHANGED != 0, f & CL_TICK_PAUSED != 0)
    }
    var isRunning: Bool {
        get { cl_document_is_running(handle) }
        set { cl_document_set_running(handle, newValue) }
    }
    var stepMs: Int {
        get { Int(cl_document_step_ms(handle)) }
        set { cl_document_set_step_ms(handle, Int32(newValue)) }
    }
    func stepOnce() { cl_document_step(handle) }
    /// A click at a world point; true when a switch, keypad or the like took it.
    func click(page: Int, at p: CGPoint) -> Bool { cl_document_click(handle, Int32(page), p.x, p.y) }

    // Editing (every change goes on the engine's undo stack).
    struct Modifiers: OptionSet {
        let rawValue: Int32
        static let shift = Modifiers(rawValue: Int32(CL_MOD_SHIFT))
        static let command = Modifiers(rawValue: Int32(CL_MOD_COMMAND))
        static let option = Modifiers(rawValue: Int32(CL_MOD_OPTION))
    }
    enum Press { case nothing, part, box }

    func press(page: Int, at p: CGPoint, modifiers: Modifiers, unitsPerPoint: CGFloat) -> Press {
        switch Int(cl_edit_press(handle, Int32(page), p.x, p.y, modifiers.rawValue, unitsPerPoint)) {
        case CL_PRESS_PART: return .part
        case CL_PRESS_BOX: return .box
        default: return .nothing
        }
    }
    func drag(to p: CGPoint) { cl_edit_drag(handle, p.x, p.y) }
    func release(at p: CGPoint) { cl_edit_release(handle, p.x, p.y) }
    func cancelGesture() { cl_edit_cancel(handle) }
    var selectionBox: CGRect? {
        var l = 0.0, b = 0.0, r = 0.0, t = 0.0
        guard cl_edit_box(handle, &l, &b, &r, &t) else { return nil }
        return CGRect(x: l, y: b, width: r - l, height: t - b)
    }

    func selectAll(page: Int) { cl_edit_select_all(handle, Int32(page)) }
    func selectNone(page: Int) { cl_edit_select_none(handle, Int32(page)) }
    func hasSelection(page: Int) -> Bool {
        cl_edit_selected_gate_count(handle, Int32(page)) + cl_edit_selected_wire_count(handle, Int32(page)) > 0
    }
    func hasGateSelection(page: Int) -> Bool { cl_edit_selected_gate_count(handle, Int32(page)) > 0 }
    func deleteSelection(page: Int) { cl_edit_delete(handle, Int32(page)) }
    func rotateSelection(page: Int) { cl_edit_rotate(handle, Int32(page)) }
    func nudge(page: Int, dx: CGFloat, dy: CGFloat) { cl_edit_nudge(handle, Int32(page), dx, dy) }
    @discardableResult
    func addGate(_ name: String, page: Int, at p: CGPoint) -> Bool { cl_edit_add_gate(handle, Int32(page), name, p.x, p.y) }
    func copySelection(page: Int) -> String { String(cString: cl_edit_copy(handle, Int32(page))) }
    /// Paste CedarLogic clipboard text; returns text to put back on the
    /// clipboard (junction ids counting up), or nil if nothing was pasted.
    func paste(_ text: String, page: Int, at p: CGPoint, shift: Bool) -> String? {
        var back: UnsafePointer<CChar>? = nil
        guard cl_edit_paste(handle, Int32(page), text, p.x, p.y, shift, &back) else { return nil }
        return back.map { String(cString: $0) } ?? ""
    }

    func undo() -> Bool { cl_edit_undo(handle) }
    func redo() -> Bool { cl_edit_redo(handle) }
    var canUndo: Bool { cl_edit_can_undo(handle) }
    var canRedo: Bool { cl_edit_can_redo(handle) }
    var undoName: String { String(cString: cl_edit_undo_name(handle)) }
    var redoName: String { String(cString: cl_edit_redo_name(handle)) }
    var isEdited: Bool { cl_document_is_edited(handle) }

    // Wires.
    /// Pointer moved (no button): updates the pin highlight and a click-started
    /// connection's line. True when the canvas should redraw.
    func hover(page: Int, at p: CGPoint, unitsPerPoint: CGFloat) -> Bool {
        cl_edit_hover(handle, Int32(page), p.x, p.y, unitsPerPoint)
    }
    var isConnecting: Bool { cl_edit_is_connecting(handle) }
    enum ContextTarget { case nothing, pin, wire, gate }
    func contextTarget(page: Int, at p: CGPoint, unitsPerPoint: CGFloat) -> ContextTarget {
        switch Int(cl_edit_context(handle, Int32(page), p.x, p.y, unitsPerPoint)) {
        case CL_CONTEXT_PIN: return .pin
        case CL_CONTEXT_WIRE: return .wire
        case CL_CONTEXT_GATE: return .gate
        default: return .nothing
        }
    }
    func disconnectPin(page: Int, at p: CGPoint, unitsPerPoint: CGFloat) {
        cl_edit_disconnect_pin(handle, Int32(page), p.x, p.y, unitsPerPoint)
    }
    func straighten(page: Int) { cl_edit_straighten(handle, Int32(page)) }
    @discardableResult
    func beginTidy(page: Int, mode: Int) -> Bool { cl_edit_tidy_begin(handle, Int32(page), Int32(mode)) }
    func endTidy(keep: Bool) { cl_edit_tidy_end(handle, keep) }
    var tidyActive: Bool { cl_edit_tidy_active(handle) }
    var tidyMode: Int { Int(cl_edit_tidy_mode(handle)) }

    // The inspector.
    struct Setting: Identifiable {
        let label: String, name: String, type: String, value: String
        let min: Double, max: Double
        var id: String { name }
    }
    func singleSelectedGate(page: Int) -> Int? {
        let g = cl_edit_single_gate(handle, Int32(page))
        return g < 0 ? nil : g
    }
    func caption(ofGate gate: Int) -> String { String(cString: cl_gate_caption(handle, gate)) }
    func settings(ofGate gate: Int) -> [Setting] {
        (0..<Int(cl_gate_setting_count(handle, gate))).compactMap { i in
            var s = CLGateSetting()
            guard cl_gate_setting(handle, gate, Int32(i), &s) else { return nil }
            return Setting(label: String(cString: s.label), name: String(cString: s.name),
                           type: String(cString: s.type), value: String(cString: s.value),
                           min: s.min, max: s.max)
        }
    }
    func setSetting(gate: Int, name: String, value: String) { cl_gate_set_setting(handle, gate, name, value) }

    /// What loading had to say, warnings flagged.
    var loadNotices: [(text: String, warning: Bool)] {
        (0..<Int(cl_document_notice_count(handle))).map {
            (String(cString: cl_document_notice(handle, Int32($0))), cl_document_notice_is_warning(handle, Int32($0)))
        }
    }

    /// World-space extent of a page, y up; nil when the page is empty.
    func bounds(ofPage page: Int) -> CGRect? {
        var l = 0.0, b = 0.0, r = 0.0, t = 0.0
        guard cl_document_page_bounds(handle, Int32(page), &l, &b, &r, &t) else { return nil }
        return CGRect(x: l, y: b, width: r - l, height: t - b)
    }
}

/// A circuit file. Opening builds the engine's copy; saving asks the engine
/// for the file text, written exactly as the wx app writes it. macOS's own
/// document handling supplies autosave, the edited dot and version history.
final class CircuitDocument: ReferenceFileDocument {
    typealias Snapshot = Data

    static var readableContentTypes: [UTType] { [.cedarLogicCircuit] }
    static var writableContentTypes: [UTType] { [.cedarLogicCircuit] }

    let core: CoreDocument

    /// A new, empty circuit.
    init() { core = CoreDocument() }

    init(configuration: ReadConfiguration) throws {
        guard let data = configuration.file.regularFileContents else {
            throw CocoaError(.fileReadCorruptFile)
        }
        core = try CoreDocument(data: data)
    }

    func snapshot(contentType: UTType) throws -> Data {
        Data(core.saveText().utf8)
    }

    func fileWrapper(snapshot: Data, configuration: WriteConfiguration) throws -> FileWrapper {
        FileWrapper(regularFileWithContents: snapshot)
    }
}

/// The gate library, for the palette.
enum GateLibrary {
    struct Category: Identifiable, Hashable {
        let index: Int
        let name: String
        /// The library's names start with a sort prefix ("A - Basic Gates");
        /// the palette shows what follows it.
        var title: String {
            if let r = name.range(of: " - ") { return String(name[r.upperBound...]) }
            return name
        }
        var id: Int { index }
    }
    struct Gate: Identifiable, Hashable {
        let name: String
        let caption: String
        var id: String { name }
    }

    static var categories: [Category] {
        (0..<Int(cl_library_category_count())).map { Category(index: $0, name: String(cString: cl_library_category(Int32($0)))) }
    }

    static func gates(in category: Category) -> [Gate] {
        (0..<Int(cl_library_gate_count(Int32(category.index)))).map {
            let name = String(cString: cl_library_gate(Int32(category.index), Int32($0)))
            return Gate(name: name, caption: String(cString: cl_library_gate_caption(name)))
        }
    }
}
