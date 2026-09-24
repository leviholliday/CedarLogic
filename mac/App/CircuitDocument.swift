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
final class CoreDocument {
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

    deinit { cl_document_close(handle) }

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

/// Stage 0 opens circuits to look at; saving arrives with editing.
struct CircuitDocument: FileDocument {
    static var readableContentTypes: [UTType] { [.cedarLogicCircuit] }

    let core: CoreDocument

    init(configuration: ReadConfiguration) throws {
        guard let data = configuration.file.regularFileContents else {
            throw CocoaError(.fileReadCorruptFile)
        }
        core = try CoreDocument(data: data)
    }

    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper {
        throw CocoaError(.featureUnsupported)
    }
}
