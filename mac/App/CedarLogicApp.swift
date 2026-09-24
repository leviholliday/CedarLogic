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
        DocumentGroup(viewing: CircuitDocument.self) { file in
            CircuitWindow(document: file.document.core)
                .environmentObject(look)
        }
        .commands {
            ViewCommands()
            SimulationCommands()
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
