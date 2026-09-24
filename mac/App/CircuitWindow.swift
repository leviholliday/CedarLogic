// One circuit's window: pages in the sidebar, the canvas beside them, and the
// simulation and zoom controls in the toolbar.

import SwiftUI

struct CircuitWindow: View {
    let document: CoreDocument
    @EnvironmentObject private var look: LookStore
    @StateObject private var canvas = CanvasController()
    @State private var page: Int? = 0
    @State private var notices: [(text: String, warning: Bool)] = []
    @State private var showNotices = false

    var body: some View {
        NavigationSplitView {
            List(0..<document.pageCount, id: \.self, selection: $page) { index in
                Label(document.pageName(index), systemImage: "square.on.square.dashed")
            }
            .navigationSplitViewColumnWidth(min: 150, ideal: 180)
        } detail: {
            CanvasView(document: document, page: page ?? 0, theme: look.settings.theme, controller: canvas)
                .ignoresSafeArea()
                .focusedSceneObject(canvas)
        }
        .toolbar {
            ToolbarItemGroup(placement: .navigation) {
                Button { canvas.toggleRunning() } label: {
                    Label(canvas.isRunning ? "Pause" : "Run", systemImage: canvas.isRunning ? "pause.fill" : "play.fill")
                }
                .help(canvas.isRunning ? "Pause the simulation (Space)" : "Run the simulation (Space)")
                Button { canvas.stepOnce() } label: { Label("Step", systemImage: "forward.frame.fill") }
                    .help("Advance one step (⇧⌘R)")
                SpeedControl(stepMs: $canvas.stepMs)
            }
            ToolbarItemGroup(placement: .primaryAction) {
                Button { canvas.zoomOut() } label: { Label("Zoom Out", systemImage: "minus.magnifyingglass") }
                    .help("Zoom out (⌘−)")
                Button { canvas.zoomToFit() } label: { Label("Zoom to Fit", systemImage: "arrow.up.left.and.down.right.magnifyingglass") }
                    .help("Zoom to fit (⌘0, or double-tap the trackpad)")
                Button { canvas.zoomIn() } label: { Label("Zoom In", systemImage: "plus.magnifyingglass") }
                    .help("Zoom in (⌘=)")
            }
        }
        .tint(look.settings.theme.accent.color)
        .onAppear {
            notices = document.loadNotices
            showNotices = !notices.isEmpty
        }
        .alert(notices.contains { $0.warning } ? "Some of this circuit couldn't be loaded" : "This circuit was updated",
               isPresented: $showNotices) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(notices.map(\.text).joined(separator: "\n\n"))
        }
    }
}

/// How fast the simulation runs: a slider from slow (500 ms a step) to fast
/// (1 ms), spaced logarithmically like the wx app's.
struct SpeedControl: View {
    @Binding var stepMs: Int

    private var fraction: Binding<Double> {
        Binding(get: { 1 - log(Double(stepMs)) / log(500) },
                set: { stepMs = max(1, min(500, Int((pow(500, 1 - $0)).rounded()))) })
    }

    var body: some View {
        HStack(spacing: 6) {
            Image(systemName: "tortoise").foregroundStyle(.secondary)
            Slider(value: fraction, in: 0...1).frame(width: 110)
            Image(systemName: "hare").foregroundStyle(.secondary)
            Text("\(stepMs) ms").monospacedDigit().foregroundStyle(.secondary).frame(width: 48, alignment: .leading)
        }
        .help("Simulation speed: milliseconds per step")
    }
}
