// The find bar (Cmd-F) over the canvas: labels, TO/FROM names and parts on
// every page. Return goes to the next one, the arrows either way, Escape
// closes it. With a label or a TO/FROM selected, it opens looking for that
// name, so a TO's FROMs are one Return away.

import SwiftUI

struct FindBar: View {
    @ObservedObject var canvas: CanvasController
    @ObservedObject var document: CoreDocument
    @FocusState private var focused: Bool

    var body: some View {
        if canvas.findActive {
            HStack(spacing: 8) {
                Image(systemName: "magnifyingglass").foregroundStyle(.secondary)
                TextField("Labels, TO/FROM names, parts", text: $canvas.findQuery)
                    .textFieldStyle(.plain)
                    .frame(width: 220)
                    .focused($focused)
                    .onSubmit { canvas.findStep(1) }
                    .onExitCommand { canvas.closeFind() }
                Text(status)
                    .font(.caption).monospacedDigit()
                    .foregroundStyle(canvas.findTotal == 0 && !canvas.findQuery.isEmpty ? Color.orange : Color.secondary)
                    .lineLimit(1)
                    .frame(minWidth: 90, alignment: .leading)
                Button { canvas.findStep(-1) } label: { Image(systemName: "chevron.up") }
                    .help("Previous").disabled(canvas.findTotal == 0)
                Button { canvas.findStep(1) } label: { Image(systemName: "chevron.down") }
                    .help("Next (Return)").disabled(canvas.findTotal == 0)
                Button("Done") { canvas.closeFind() }
            }
            .buttonStyle(.borderless)
            .padding(.horizontal, 14).padding(.vertical, 7)
            .background(.regularMaterial, in: Capsule())
            .overlay(Capsule().strokeBorder(Color.secondary.opacity(0.25)))
            .shadow(color: .black.opacity(0.18), radius: 8, y: 2)
            .padding(.top, 12)
            .transition(.move(edge: .top).combined(with: .opacity))
            .onAppear { focusSoon() }
            .onChange(of: canvas.findFocusTick) { _, _ in focusSoon() }
            .onChange(of: canvas.findQuery) { _, _ in canvas.runFind(jump: true) }
            .onChange(of: canvas.editVersion) { _, _ in canvas.runFind(jump: false) }
        }
    }

    /// Into the field, selecting what's there so typing replaces it. A beat
    /// later, once the bar is on screen.
    private func focusSoon() {
        for delay in [0.0, 0.08] {
            DispatchQueue.main.asyncAfter(deadline: .now() + delay) {
                focused = true
                DispatchQueue.main.async { NSApp.keyWindow?.fieldEditor(false, for: nil)?.selectAll(nil) }
            }
        }
    }

    private var status: String {
        guard canvas.findTotal > 0, canvas.findHits.indices.contains(canvas.findIndex) else {
            return canvas.findQuery.isEmpty ? "" : "Not found"
        }
        let hit = canvas.findHits[canvas.findIndex]
        var s = "\(canvas.findIndex + 1) of \(canvas.findTotal) · \(hit.kind)"
        if document.pageCount > 1 { s += " · " + document.pageName(hit.page) }
        return s
    }
}
