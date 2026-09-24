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
    @Environment(\.undoManager) private var undoManager
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
        .onChange(of: undoManager) { _, um in canvas.undoManager = um }
        .onAppear {
            canvas.undoManager = undoManager
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
                    PageList(document: document, canvas: canvas, page: $page)
                }
            }
            .navigationSplitViewColumnWidth(min: 190, ideal: 230)
        } detail: {
            CanvasView(document: document, page: page, theme: theme, controller: canvas)
                .ignoresSafeArea()
                .overlay(alignment: .top) { TidyBanner(canvas: canvas) }
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
                PageTabs(document: document, canvas: canvas, page: $page)
                Divider()
                CanvasView(document: document, page: page, theme: theme, controller: canvas)
                    .overlay(alignment: .top) { TidyBanner(canvas: canvas) }
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
    @ObservedObject var document: CoreDocument
    let canvas: CanvasController
    @Binding var page: Int
    @StateObject private var pages = PageActions()

    var body: some View {
        HStack(spacing: 0) {
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
                        .contextMenu { pages.menu(for: i, document: document, canvas: canvas, page: $page) }
                    }
                }
                .padding(.horizontal, 8).padding(.vertical, 4)
            }
            Button { pages.add(document: document, canvas: canvas, page: $page) } label: { Image(systemName: "plus") }
                .buttonStyle(.borderless)
                .help("Add a page")
                .padding(.horizontal, 10)
        }
        .background(.bar)
        .modifier(PageSheets(pages: pages, document: document, canvas: canvas, page: $page))
    }
}

/// The pages in the Native sidebar.
private struct PageList: View {
    @ObservedObject var document: CoreDocument
    let canvas: CanvasController
    @Binding var page: Int
    @StateObject private var pages = PageActions()

    var body: some View {
        List(0..<document.pageCount, id: \.self, selection: Binding(get: { page }, set: { page = $0 ?? page })) { index in
            Label(document.pageName(index), systemImage: "square.on.square.dashed")
                .contextMenu { pages.menu(for: index, document: document, canvas: canvas, page: $page) }
        }
        .safeAreaInset(edge: .bottom) {
            HStack {
                Button { pages.add(document: document, canvas: canvas, page: $page) } label: { Label("Add Page", systemImage: "plus") }
                    .buttonStyle(.borderless)
                Spacer()
            }
            .padding(8)
        }
        .modifier(PageSheets(pages: pages, document: document, canvas: canvas, page: $page))
    }
}

/// Adding, renaming and removing pages, shared by the tabs and the sidebar.
/// None of these are undo steps; each marks the document changed.
@MainActor
private final class PageActions: ObservableObject {
    @Published var renaming: Int?
    @Published var newName = ""
    @Published var deleting: Int?

    func add(document: CoreDocument, canvas: CanvasController, page: Binding<Int>) {
        page.wrappedValue = document.addPage()
        document.objectWillChange.send()
        canvas.markEdited()
    }

    @ViewBuilder
    func menu(for i: Int, document: CoreDocument, canvas: CanvasController, page: Binding<Int>) -> some View {
        Button("Rename…") { self.newName = document.pageName(i); self.renaming = i }
        Button("Delete Page…") { self.deleting = i }
            .disabled(document.pageCount < 2)
    }
}

private struct PageSheets: ViewModifier {
    @ObservedObject var pages: PageActions
    let document: CoreDocument
    let canvas: CanvasController
    @Binding var page: Int

    func body(content: Content) -> some View {
        content
            .alert("Rename Page", isPresented: Binding(get: { pages.renaming != nil }, set: { if !$0 { pages.renaming = nil } })) {
                TextField("Name", text: $pages.newName)
                Button("Rename") {
                    if let i = pages.renaming {
                        document.renamePage(i, to: pages.newName.trimmingCharacters(in: .whitespaces))
                        document.objectWillChange.send()
                        canvas.markEdited()
                    }
                    pages.renaming = nil
                }
                Button("Cancel", role: .cancel) { pages.renaming = nil }
            }
            .alert("Delete this page?", isPresented: Binding(get: { pages.deleting != nil }, set: { if !$0 { pages.deleting = nil } })) {
                Button("Delete", role: .destructive) {
                    if let i = pages.deleting {
                        canvas.selectNone()
                        document.deletePage(i)
                        page = min(page, document.pageCount - 1)
                        document.objectWillChange.send()
                        canvas.markEdited()
                        canvas.edited()
                    }
                    pages.deleting = nil
                }
                Button("Cancel", role: .cancel) { pages.deleting = nil }
            } message: {
                Text("Everything on it goes too, and this can't be undone (earlier undo steps are cleared as well).")
            }
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

/// While a Tidy Up is on show: what it is and how to finish it.
private struct TidyBanner: View {
    @ObservedObject var canvas: CanvasController

    var body: some View {
        if canvas.tidyActive {
            HStack(spacing: 12) {
                Text("Tidy Up preview").fontWeight(.semibold)
                Button("Keep") { canvas.endTidy(keep: true) }
                    .keyboardShortcut(.defaultAction)
                Button("Put Back") { canvas.endTidy(keep: false) }
                Button(canvas.tidyMode == 1 ? "Keep My Layout Instead" : "Full Rearrange Instead") { canvas.switchTidyMode() }
                    .help("Tab")
            }
            .font(.callout)
            .padding(.horizontal, 16).padding(.vertical, 8)
            .background(.regularMaterial, in: Capsule())
            .shadow(radius: 6, y: 2)
            .padding(.top, 12)
            .transition(.move(edge: .top).combined(with: .opacity))
        }
    }
}
