// One circuit's window: pages in the sidebar, the canvas beside them, and the
// zoom controls in the toolbar.

import SwiftUI

struct CircuitWindow: View {
    let document: CoreDocument
    @EnvironmentObject private var look: LookStore
    @StateObject private var canvas = CanvasController()
    @State private var page: Int? = 0

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
    }
}
