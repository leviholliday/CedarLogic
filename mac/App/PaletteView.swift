// The gate palette: the library's categories as tiles drawn by the engine.
// Click a tile to put that gate in the middle of the view, or drag it onto the
// canvas. Search looks through every category.

import SwiftUI

struct PaletteView: View {
    let controller: CanvasController
    var dark: Bool = false
    @Environment(\.colorScheme) private var scheme
    @AppStorage("paletteCategory") private var categoryIndex = 0
    @State private var search = ""

    private var categories: [GateLibrary.Category] { GateLibrary.categories }

    private var shownGates: [GateLibrary.Gate] {
        let q = search.trimmingCharacters(in: .whitespaces).lowercased()
        if q.isEmpty {
            guard categories.indices.contains(categoryIndex) else { return [] }
            return GateLibrary.gates(in: categories[categoryIndex])
        }
        return categories.flatMap { GateLibrary.gates(in: $0) }
            .filter { $0.caption.lowercased().contains(q) || $0.name.lowercased().contains(q) }
    }

    var body: some View {
        VStack(spacing: 8) {
            TextField("Search parts", text: $search)
                .textFieldStyle(.roundedBorder)
            if search.isEmpty {
                Picker("Category", selection: $categoryIndex) {
                    ForEach(categories) { Text($0.title).tag($0.index) }
                }
                .labelsHidden()
            }
            ScrollView {
                LazyVGrid(columns: [GridItem(.adaptive(minimum: 68), spacing: 6)], spacing: 6) {
                    ForEach(shownGates) { gate in
                        GateTile(gate: gate, dark: dark || scheme == .dark)
                            .onTapGesture { controller.addGate(gate.name) }
                            .onDrag { NSItemProvider(object: (GatePayload.prefix + gate.name) as NSString) }
                            .help(gate.caption)
                    }
                }
                .padding(.bottom, 8)
            }
        }
        .padding(8)
    }
}

/// One palette tile: the gate drawn by the engine, captioned.
struct GateTile: View {
    let gate: GateLibrary.Gate
    let dark: Bool
    @Environment(\.displayScale) private var scale
    @State private var hover = false

    var body: some View {
        VStack(spacing: 2) {
            Canvas { ctx, size in
                ctx.withCGContext { cg in
                    cl_library_draw_gate(gate.name, cg, size.width, size.height, scale, dark)
                }
            }
            .frame(height: 46)
            Text(gate.caption)
                .font(.caption2)
                .lineLimit(1)
                .truncationMode(.tail)
                .foregroundStyle(.secondary)
        }
        .padding(4)
        .background(RoundedRectangle(cornerRadius: 7).fill(hover ? Color.accentColor.opacity(0.12) : Color.clear))
        .contentShape(Rectangle())
        .onHover { hover = $0 }
    }
}
