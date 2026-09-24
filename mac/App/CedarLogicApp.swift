// CedarLogic for Mac: the native front end. Stage 0 opens circuits and shows
// them; the engine underneath is the same C++ the wx app runs.

import SwiftUI

@main
struct CedarLogicApp: App {
    @StateObject private var look = LookStore.shared

    init() {
        loadGateLibrary()
    }

    var body: some Scene {
        DocumentGroup(newDocument: { CircuitDocument() }) { file in
            CircuitWindow(document: file.document.core)
                .environmentObject(look)
        }
        .commands {
            EditCommands()
            ViewCommands()
            SimulationCommands()
        }

        Window("Your Circuits", id: "library") {
            LibraryView()
        }
        .commands {
            LibraryCommands()
        }

        Settings {
            SettingsView()
                .environmentObject(look)
        }
    }
}

/// The gate library ships in the app's Resources. `swift`-style dev builds
/// without a bundle fall back to the copy in the repo.
private func loadGateLibrary() {
    var candidates: [String] = []
    if let bundled = Bundle.main.path(forResource: "cl_gatedefs", ofType: "xml") { candidates.append(bundled) }
    let repo = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        .deletingLastPathComponent().deletingLastPathComponent()
        .appendingPathComponent("res/cl_gatedefs.xml").path
    candidates.append(repo)
    for path in candidates where FileManager.default.fileExists(atPath: path) {
        if cl_library_load(path) { return }
    }
    NSLog("CedarLogic: the gate library couldn't be loaded")
}

struct ViewCommands: Commands {
    @FocusedObject private var canvas: CanvasController?

    var body: some Commands {
        CommandGroup(after: .toolbar) {
            Button("Zoom In") { canvas?.zoomIn() }
                .keyboardShortcut("=", modifiers: .command)
                .disabled(canvas == nil)
            Button("Zoom Out") { canvas?.zoomOut() }
                .keyboardShortcut("-", modifiers: .command)
                .disabled(canvas == nil)
            Button("Zoom to Fit") { canvas?.zoomToFit() }
                .keyboardShortcut("0", modifiers: .command)
                .disabled(canvas == nil)
            Divider()
        }
    }
}

struct SimulationCommands: Commands {
    @FocusedObject private var canvas: CanvasController?

    var body: some Commands {
        CommandMenu("Simulation") {
            Button(canvas?.isRunning == false ? "Run" : "Pause") { canvas?.toggleRunning() }
                .keyboardShortcut("r", modifiers: .command)
                .disabled(canvas == nil)
            Button("Step") { canvas?.stepOnce() }
                .keyboardShortcut("r", modifiers: [.command, .shift])
                .disabled(canvas == nil)
        }
    }
}

/// Edit menu. While a text field has the keyboard (the palette search, an
/// inspector field), the usual text commands go to it instead.
struct EditCommands: Commands {
    @FocusedObject private var canvas: CanvasController?
    @AppStorage("tidyMode") private var defaultMode = 0

    private func tidyTitle(_ mode: Int) -> String { mode == 1 ? "Tidy Up: Full Rearrange" : "Tidy Up: Keep My Layout" }

    private var typing: Bool { NSApp.keyWindow?.firstResponder is NSText }

    private func textAction(_ selector: Selector) { NSApp.sendAction(selector, to: nil, from: nil) }

    var body: some Commands {
        CommandGroup(replacing: .pasteboard) {
            Button("Cut") { if typing { textAction(#selector(NSText.cut(_:))) } else { canvas?.cut() } }
                .keyboardShortcut("x", modifiers: .command)
            Button("Copy") { if typing { textAction(#selector(NSText.copy(_:))) } else { canvas?.copy() } }
                .keyboardShortcut("c", modifiers: .command)
            Button("Paste") { if typing { textAction(#selector(NSText.paste(_:))) } else { canvas?.paste() } }
                .keyboardShortcut("v", modifiers: .command)
            Button("Duplicate") { canvas?.duplicate() }
                .keyboardShortcut("d", modifiers: .command)
                .disabled(canvas?.hasGateSelection != true)
            Button("Delete") { if typing { textAction(#selector(NSText.delete(_:))) } else { canvas?.deleteSelection() } }
            Button("Select All") { if typing { textAction(#selector(NSText.selectAll(_:))) } else { canvas?.selectAll() } }
                .keyboardShortcut("a", modifiers: .command)
            Divider()
            Button("Rotate") { canvas?.rotate() }
                .disabled(canvas?.hasGateSelection != true)
            Button("Straighten Wires") { canvas?.straighten() }
                .help("S: the selected wires, or the wires of the selected parts")
            Button(tidyTitle(defaultMode)) { canvas?.tidy(mode: defaultMode) }
                .help("Shift-S. Lines parts up and reroutes their wires (the selection, or the whole page).")
            Button(tidyTitle(1 - defaultMode)) { canvas?.tidy(mode: 1 - defaultMode) }
        }
    }
}

struct LibraryCommands: Commands {
    @Environment(\.openWindow) private var openWindow

    var body: some Commands {
        CommandGroup(after: .newItem) {
            Button("Your Circuits…") { openWindow(id: "library") }
                .keyboardShortcut("o", modifiers: [.command, .shift])
        }
    }
}
