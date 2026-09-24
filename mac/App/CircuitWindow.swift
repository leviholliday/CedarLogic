// One circuit's window, in either layout (Settings > Layout):
//   Native  -- pages and parts in a sidebar, the canvas, and an inspector on
//              the right for the selected part's settings.
//   Classic -- CedarLogic's own arrangement: the parts palette down the left,
//              page tabs over the canvas, a full toolbar, and settings in a
//              sheet on double-click.
// Both are the same canvas, controller and panels, arranged differently.

import SwiftUI

enum AppLayout: String, CaseIterable, Identifiable {
    case native, classic
    var id: String { rawValue }
    var name: String { self == .native ? "Native" : "Classic" }
    var summary: String {
        self == .native
            ? "A Mac-style window: pages and parts in a sidebar, settings in an inspector."
            : "CedarLogic's layout: parts palette on the left, page tabs, a full toolbar."
    }
}

struct CircuitWindow: View {
    let document: CoreDocument
    @EnvironmentObject private var look: LookStore
    @AppStorage("layout") private var layout = AppLayout.native.rawValue
    @StateObject private var canvas = CanvasController()
    @State private var page = 0
    @State private var notices: [(text: String, warning: Bool)] = []
    @State private var showNotices = false

    private var theme: Theme { look.settings.theme }

    var body: some View {
        Group {
            if AppLayout(rawValue: layout) == .classic {
                ClassicLayout(document: document, canvas: canvas, page: $page, theme: theme)
            } else {
                NativeLayout(document: document, canvas: canvas, page: $page, theme: theme)
            }
        }
        .focusedSceneObject(canvas)
        .tint(theme.accent.color)
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

// MARK: - Native

private struct NativeLayout: View {
    let document: CoreDocument
    @ObservedObject var canvas: CanvasController
    @Binding var page: Int
    let theme: Theme
    @AppStorage("sidebarShowsParts") private var showParts = false
    @AppStorage("inspectorShown") private var showInspector = true

    var body: some View {
        NavigationSplitView {
            VStack(spacing: 0) {
                Picker("", selection: $showParts) {
                    Text("Pages").tag(false)
                    Text("Parts").tag(true)
                }
                .pickerStyle(.segmented)
                .labelsHidden()
                .padding(8)
                if showParts {
                    PaletteView(controller: canvas, dark: false)
                } else {
                    List(0..<document.pageCount, id: \.self, selection: Binding(get: { page }, set: { page = $0 ?? page })) { index in
                        Label(document.pageName(index), systemImage: "square.on.square.dashed")
                    }
                }
            }
            .navigationSplitViewColumnWidth(min: 190, ideal: 230)
        } detail: {
            CanvasView(document: document, page: page, theme: theme, controller: canvas)
                .ignoresSafeArea()
                .inspector(isPresented: $showInspector) {
                    InspectorView(document: document, controller: canvas)
                        .inspectorColumnWidth(min: 220, ideal: 260, max: 360)
                }
        }
        .toolbar {
            ToolbarItemGroup(placement: .navigation) { SimulationControls(canvas: canvas) }
            ToolbarItemGroup(placement: .primaryAction) {
                ZoomControls(canvas: canvas)
                Button { showInspector.toggle() } label: { Label("Inspector", systemImage: "sidebar.right") }
                    .help("Show or hide the selected part's settings")
            }
        }
        .onChange(of: canvas.settingsRequested) { _, asked in
            if asked { showInspector = true; canvas.settingsRequested = false }
        }
    }
}

// MARK: - Classic

private struct ClassicLayout: View {
    let document: CoreDocument
    @ObservedObject var canvas: CanvasController
    @Binding var page: Int
    let theme: Theme
    @State private var showSettings = false

    var body: some View {
        HStack(spacing: 0) {
            PaletteView(controller: canvas, dark: false)
                .frame(width: 230)
                .background(.background.secondary)
            Divider()
            VStack(spacing: 0) {
                PageTabs(document: document, page: $page)
                Divider()
                CanvasView(document: document, page: page, theme: theme, controller: canvas)
            }
        }
        .toolbar {
            ToolbarItemGroup(placement: .navigation) {
                Button { canvas.undo() } label: { Label("Undo", systemImage: "arrow.uturn.backward") }
                    .help("Undo (⌘Z)").disabled(!canvas.canUndo)
                Button { canvas.redo() } label: { Label("Redo", systemImage: "arrow.uturn.forward") }
                    .help("Redo (⇧⌘Z)").disabled(!canvas.canRedo)
                Divider()
                Button { canvas.cut() } label: { Label("Cut", systemImage: "scissors") }.help("Cut (⌘X)")
                Button { canvas.copy() } label: { Label("Copy", systemImage: "doc.on.doc") }.help("Copy (⌘C)")
                Button { canvas.paste() } label: { Label("Paste", systemImage: "clipboard") }.help("Paste (⌘V)")
            }
            ToolbarItemGroup(placement: .principal) { SimulationControls(canvas: canvas) }
            ToolbarItemGroup(placement: .primaryAction) { ZoomControls(canvas: canvas) }
        }
        .onChange(of: canvas.settingsRequested) { _, asked in
            if asked { showSettings = true; canvas.settingsRequested = false }
        }
        .sheet(isPresented: $showSettings) {
            VStack(spacing: 0) {
                InspectorView(document: document, controller: canvas)
                    .frame(width: 380, height: 320)
                Divider()
                HStack { Spacer(); Button("Done") { showSettings = false }.keyboardShortcut(.defaultAction) }
                    .padding(12)
            }
        }
    }
}

/// Page tabs across the top of the canvas, as in the wx app.
private struct PageTabs: View {
    let document: CoreDocument
    @Binding var page: Int

    var body: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 2) {
                ForEach(0..<document.pageCount, id: \.self) { i in
                    Button { page = i } label: {
                        Text(document.pageName(i))
                            .font(.callout.weight(page == i ? .semibold : .regular))
                            .padding(.horizontal, 14).padding(.vertical, 6)
                            .background(RoundedRectangle(cornerRadius: 6)
                                .fill(page == i ? Color.accentColor.opacity(0.16) : Color.clear))
                    }
                    .buttonStyle(.plain)
                }
            }
            .padding(.horizontal, 8).padding(.vertical, 4)
        }
        .background(.bar)
    }
}

// MARK: - Shared toolbar pieces

private struct SimulationControls: View {
    @ObservedObject var canvas: CanvasController

    var body: some View {
        Button { canvas.toggleRunning() } label: {
            Label(canvas.isRunning ? "Pause" : "Run", systemImage: canvas.isRunning ? "pause.fill" : "play.fill")
        }
        .help(canvas.isRunning ? "Pause the simulation (Space)" : "Run the simulation (Space)")
        Button { canvas.stepOnce() } label: { Label("Step", systemImage: "forward.frame.fill") }
            .help("Advance one step (⇧⌘R)")
        SpeedControl(stepMs: $canvas.stepMs)
    }
}

private struct ZoomControls: View {
    let canvas: CanvasController

    var body: some View {
        Button { canvas.zoomOut() } label: { Label("Zoom Out", systemImage: "minus.magnifyingglass") }
            .help("Zoom out (⌘−)")
        Button { canvas.zoomToFit() } label: { Label("Zoom to Fit", systemImage: "arrow.up.left.and.down.right.magnifyingglass") }
            .help("Zoom to fit (⌘0, or double-tap the trackpad)")
        Button { canvas.zoomIn() } label: { Label("Zoom In", systemImage: "plus.magnifyingglass") }
            .help("Zoom in (⌘=)")
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
