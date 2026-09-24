// Settings (⌘,). Stage 0 has the Look pane: pick a preset, then adjust it.

import SwiftUI

struct SettingsView: View {
    var body: some View {
        TabView {
            LayoutSettingsView()
                .tabItem { Label("Layout", systemImage: "rectangle.3.group") }
            LookSettingsView()
                .tabItem { Label("Look", systemImage: "paintpalette") }
        }
        .frame(width: 560)
    }
}

struct LookSettingsView: View {
    @EnvironmentObject private var look: LookStore

    var body: some View {
        Form {
            Section {
                HStack(spacing: 14) {
                    ForEach(ThemePreset.allCases) { preset in
                        PresetCard(preset: preset, selected: look.settings.preset == preset) {
                            look.settings.choose(preset)
                        }
                    }
                }
                .padding(.vertical, 4)
                Text(look.settings.preset.summary)
                    .font(.callout)
                    .foregroundStyle(.secondary)
            } header: {
                Text("Preset")
            }

            Section {
                ColorRow(title: "Canvas", value: look.settings.theme.canvas, isSet: look.settings.canvas != nil,
                         set: { look.settings.canvas = $0 }, reset: { look.settings.canvas = nil })
                Picker("Grid", selection: Binding(
                    get: { look.settings.theme.gridStyle },
                    set: { look.settings.gridStyle = $0 })) {
                    ForEach(GridStyle.allCases) { Text($0.label).tag($0) }
                }
                ColorRow(title: "Grid", value: look.settings.theme.gridMinor, isSet: look.settings.gridMinor != nil,
                         set: { look.settings.gridMinor = $0 }, reset: { look.settings.gridMinor = nil })
                ColorRow(title: "Grid, every fifth line", value: look.settings.theme.gridMajor,
                         isSet: look.settings.gridMajor != nil,
                         set: { look.settings.gridMajor = $0 }, reset: { look.settings.gridMajor = nil })
                Toggle("Light ink for dark canvases", isOn: Binding(
                    get: { look.settings.theme.darkCircuit },
                    set: { look.settings.darkCircuit = $0 }))
                ColorRow(title: "Accent", value: look.settings.theme.accent, isSet: look.settings.accent != nil,
                         set: { look.settings.accent = $0 }, reset: { look.settings.accent = nil })
            } header: {
                Text("Customize")
            } footer: {
                HStack {
                    Spacer()
                    Button("Reset to \(look.settings.preset.name)") { look.settings.choose(look.settings.preset) }
                        .disabled(!look.settings.isCustomized)
                }
            }
        }
        .formStyle(.grouped)
        .padding(.vertical, 8)
    }
}

/// A color setting, with a small reset button once it's been changed.
private struct ColorRow: View {
    let title: String
    let value: RGBA
    let isSet: Bool
    let set: (RGBA) -> Void
    let reset: () -> Void

    var body: some View {
        HStack {
            ColorPicker(title, selection: Binding(get: { value.color }, set: { set(RGBA($0)) }), supportsOpacity: false)
            if isSet {
                Button(action: reset) { Image(systemName: "arrow.uturn.backward") }
                    .buttonStyle(.borderless)
                    .help("Back to the preset's color")
            }
        }
    }
}

/// A preset as a miniature canvas: its background, grid and a tiny gate.
private struct PresetCard: View {
    let preset: ThemePreset
    let selected: Bool
    let action: () -> Void

    var body: some View {
        let t = preset.theme
        Button(action: action) {
            VStack(spacing: 6) {
                Canvas { ctx, size in
                    ctx.fill(Path(CGRect(origin: .zero, size: size)), with: .color(t.canvas.color))
                    let step: CGFloat = 8
                    for x in stride(from: step, to: size.width, by: step) {
                        for y in stride(from: step, to: size.height, by: step) {
                            if t.gridStyle == .dots {
                                ctx.fill(Path(ellipseIn: CGRect(x: x - 0.7, y: y - 0.7, width: 1.4, height: 1.4)),
                                         with: .color(t.gridMajor.color))
                            }
                        }
                    }
                    if t.gridStyle == .lines {
                        var grid = Path()
                        for x in stride(from: step, to: size.width, by: step) { grid.move(to: CGPoint(x: x, y: 0)); grid.addLine(to: CGPoint(x: x, y: size.height)) }
                        for y in stride(from: step, to: size.height, by: step) { grid.move(to: CGPoint(x: 0, y: y)); grid.addLine(to: CGPoint(x: size.width, y: y)) }
                        ctx.stroke(grid, with: .color(t.gridMinor.color), lineWidth: 0.5)
                    }
                    // An AND gate, in the preset's ink.
                    let ink: Color = t.darkCircuit ? .white : .black
                    var gate = Path()
                    let r = CGRect(x: size.width / 2 - 14, y: size.height / 2 - 11, width: 28, height: 22)
                    gate.move(to: CGPoint(x: r.minX, y: r.minY))
                    gate.addLine(to: CGPoint(x: r.midX, y: r.minY))
                    gate.addArc(center: CGPoint(x: r.midX, y: r.midY), radius: r.height / 2,
                                startAngle: .degrees(-90), endAngle: .degrees(90), clockwise: false)
                    gate.addLine(to: CGPoint(x: r.minX, y: r.maxY))
                    gate.closeSubpath()
                    ctx.stroke(gate, with: .color(ink), lineWidth: 1.2)
                    var wires = Path()
                    wires.move(to: CGPoint(x: r.minX - 12, y: r.minY + 6)); wires.addLine(to: CGPoint(x: r.minX, y: r.minY + 6))
                    wires.move(to: CGPoint(x: r.minX - 12, y: r.maxY - 6)); wires.addLine(to: CGPoint(x: r.minX, y: r.maxY - 6))
                    wires.move(to: CGPoint(x: r.midX + r.height / 2, y: r.midY)); wires.addLine(to: CGPoint(x: r.maxX + 14, y: r.midY))
                    ctx.stroke(wires, with: .color(Color(red: 0, green: 0.7, blue: 0)), lineWidth: 1.2)
                }
                .frame(width: 130, height: 76)
                .clipShape(RoundedRectangle(cornerRadius: 8))
                .overlay(RoundedRectangle(cornerRadius: 8)
                    .strokeBorder(selected ? Color.accentColor : Color.secondary.opacity(0.3), lineWidth: selected ? 2.5 : 1))
                Text(preset.name).font(.callout.weight(selected ? .semibold : .regular))
            }
        }
        .buttonStyle(.plain)
    }
}

/// Which window arrangement to use; either works with any look.
struct LayoutSettingsView: View {
    @AppStorage("layout") private var layout = AppLayout.native.rawValue
    @AppStorage("tidyMode") private var tidyMode = 0

    var body: some View {
        Form {
            Picker("Layout", selection: $layout) {
                ForEach(AppLayout.allCases) { Text($0.name).tag($0.rawValue) }
            }
            .pickerStyle(.radioGroup)
            Text(AppLayout(rawValue: layout)?.summary ?? "")
                .font(.callout)
                .foregroundStyle(.secondary)
            Section("Tidy Up (Shift-S)") {
                Picker("Shift-S", selection: $tidyMode) {
                    Text("Keeps my layout").tag(0)
                    Text("Rearranges everything").tag(1)
                }
                Text("Keeps my layout lines parts up where they are. Rearranges everything lays the circuit out by signal flow. The Edit menu always has both.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }
        }
        .formStyle(.grouped)
        .padding(.vertical, 8)
    }
}
