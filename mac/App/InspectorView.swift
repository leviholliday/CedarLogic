// A gate's settings (double-click it): the same ones the wx app's
// parameters dialog lists, each with a control that suits its type, in the
// app's own look -- the gate's picture and name on top, a card of settings,
// then Rotate and Delete, and Done. A change applies as you make it (a
// number when it's valid) and can be undone. Return is Done; Escape too.

import AppKit
import SwiftUI

struct GateSettingsSheet: View {
    let document: CoreDocument
    @ObservedObject var controller: CanvasController
    let done: () -> Void
    @ObservedObject private var prefs = Prefs.shared
    @Environment(\.displayScale) private var scale

    private var dark: Bool { prefs.dark }
    private var paper: Color { dark ? CLChrome.rgb(28, 31, 37) : CLChrome.rgb(250, 250, 252) }
    private var card: Color { dark ? CLChrome.rgb(36, 40, 47) : .white }
    private var ink: Color { dark ? CLChrome.rgb(226, 230, 238) : CLChrome.rgb(30, 33, 40) }
    private var line: Color { ink.opacity(dark ? 0.08 : 0.09) }

    var body: some View {
        let _ = controller.selectionVersion   // reload when the selection changes
        let page = controller.page
        let accent = prefs.accentColor(dark: dark)
        VStack(spacing: 0) {
            if let gate = document.singleSelectedGate(page: page) {
                let settings = document.settings(ofGate: gate)
                header(gate)
                ScrollView {
                    VStack(spacing: 0) {
                        if settings.isEmpty {
                            Text("This part has no settings.")
                                .font(.system(size: 12.5)).foregroundStyle(ink.opacity(0.55))
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .padding(.horizontal, 14).padding(.vertical, 12)
                        }
                        ForEach(Array(settings.enumerated()), id: \.element.id) { i, s in
                            if i > 0 { Rectangle().fill(line).frame(height: 1).padding(.leading, 14) }
                            SettingRow(setting: s, ink: ink, accent: accent, dark: dark, done: done) { value in
                                document.setSetting(gate: gate, name: s.name, value: value)
                                controller.redraw()
                                controller.editsChanged()
                                controller.selectionChanged()
                            }
                        }
                    }
                    .background(RoundedRectangle(cornerRadius: 12, style: .continuous).fill(card))
                    .overlay(RoundedRectangle(cornerRadius: 12, style: .continuous).strokeBorder(line))
                    .padding(.horizontal, 20)
                }
                .scrollBounceBehavior(.basedOnSize)
                .frame(maxHeight: 300)
                .fixedSize(horizontal: false, vertical: true)
            } else {
                Text(document.hasSelection(page: page)
                     ? "Several things are selected. Select one part to see its settings."
                     : "Select a part to see its settings.")
                    .font(.system(size: 13)).foregroundStyle(ink.opacity(0.6))
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(22)
            }
            footer(accent: accent, hasGate: document.singleSelectedGate(page: page) != nil)
        }
        .frame(width: 420)
        .background(paper)
        .preferredColorScheme(dark ? .dark : .light)
    }

    private func header(_ gate: Int) -> some View {
        HStack(spacing: 14) {
            Image(nsImage: TileCache.image(document.libraryName(ofGate: gate), size: 44, dark: dark, scale: scale))
                .frame(width: 44, height: 36)
                .padding(6)
                .background(RoundedRectangle(cornerRadius: 10, style: .continuous).fill(card))
                .overlay(RoundedRectangle(cornerRadius: 10, style: .continuous).strokeBorder(line))
            VStack(alignment: .leading, spacing: 3) {
                Text(document.caption(ofGate: gate))
                    .font(.system(size: 16, weight: .bold)).foregroundStyle(ink).lineLimit(2)
                Text("Changes apply as you make them \u{00B7} \u{2318}Z undoes")
                    .font(.system(size: 11)).foregroundStyle(ink.opacity(0.5))
            }
            Spacer(minLength: 0)
        }
        .padding(.horizontal, 20).padding(.top, 20).padding(.bottom, 14)
    }

    private func footer(accent: Color, hasGate: Bool) -> some View {
        HStack(spacing: 8) {
            if hasGate {
                footButton("arrow.clockwise", "Rotate", tint: ink) { controller.rotate() }
                    .help("Turn a quarter turn clockwise (R). Parts with wires attached stay put.")
                footButton("trash", "Delete", tint: CLChrome.rgb(229, 72, 77)) { controller.deleteSelection(); done() }
                    .help("Delete this part (⌫)")
            }
            Spacer()
            Button(action: done) {
                HStack(spacing: 7) {
                    Text("Done").font(.system(size: 13, weight: .semibold))
                    Text("\u{21A9}").font(.system(size: 10, weight: .semibold))
                        .padding(.horizontal, 5).padding(.vertical, 1.5)
                        .background(RoundedRectangle(cornerRadius: 4).fill(prefs.onAccentColor(dark: dark).opacity(0.16)))
                }
                .padding(.horizontal, 16).padding(.vertical, 8)
                .background(RoundedRectangle(cornerRadius: 9, style: .continuous).fill(accent))
                .foregroundStyle(prefs.onAccentColor(dark: dark))
            }
            .buttonStyle(.plain)
            .keyboardShortcut(.defaultAction)
        }
        .padding(.horizontal, 20).padding(.top, 16).padding(.bottom, 18)
    }

    private func footButton(_ icon: String, _ title: String, tint: Color, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack(spacing: 6) {
                Image(systemName: icon).font(.system(size: 11, weight: .semibold))
                Text(title).font(.system(size: 12.5, weight: .medium))
            }
            .padding(.horizontal, 12).padding(.vertical, 7)
            .background(RoundedRectangle(cornerRadius: 8, style: .continuous).fill(tint.opacity(0.1)))
            .foregroundStyle(tint)
        }
        .buttonStyle(.plain)
    }
}

/// One setting: its name on the left, the control its type calls for on the
/// right, and the allowed range or a problem under it.
private struct SettingRow: View {
    let setting: CoreDocument.Setting
    let ink: Color
    let accent: Color
    let dark: Bool
    let done: () -> Void
    let apply: (String) -> Void
    @State private var text = ""
    @State private var problem: String?
    @FocusState private var focused: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack(spacing: 12) {
                Text(setting.label).font(.system(size: 13)).foregroundStyle(ink)
                    .lineLimit(2).fixedSize(horizontal: false, vertical: true)
                Spacer(minLength: 8)
                control
            }
            if let problem {
                Text(problem).font(.system(size: 11)).foregroundStyle(CLChrome.rgb(229, 72, 77))
            } else if isNumber, setting.max < 1e30 {
                Text("\(format(setting.min)) to \(format(setting.max))")
                    .font(.system(size: 11)).foregroundStyle(ink.opacity(0.45))
            }
        }
        .padding(.horizontal, 14).padding(.vertical, 10)
    }

    @ViewBuilder private var control: some View {
        switch setting.type {
        case "BOOL":
            Toggle("", isOn: Binding(get: { setting.value == "true" }, set: { apply($0 ? "true" : "false") }))
                .labelsHidden().toggleStyle(.switch).controlSize(.small).tint(accent)
        case "FILE_IN", "FILE_OUT":
            HStack(spacing: 8) {
                Text(setting.value.isEmpty ? "None" : (setting.value as NSString).lastPathComponent)
                    .font(.system(size: 12)).foregroundStyle(ink.opacity(0.55))
                    .lineLimit(1).truncationMode(.middle)
                Button("Choose\u{2026}") { choose() }.controlSize(.small)
            }
        default:
            HStack(spacing: 6) {
                TextField("", text: $text)
                    .textFieldStyle(.plain)
                    .font(.system(size: 13, design: isNumber ? .monospaced : .default))
                    .multilineTextAlignment(isNumber ? .trailing : .leading)
                    .focused($focused)
                    .padding(.horizontal, 8).padding(.vertical, 5)
                    .frame(width: isNumber ? 90 : 170)
                    .background(RoundedRectangle(cornerRadius: 7, style: .continuous).fill(ink.opacity(dark ? 0.07 : 0.05)))
                    .overlay(RoundedRectangle(cornerRadius: 7, style: .continuous)
                        .strokeBorder(problem != nil ? CLChrome.rgb(229, 72, 77) : (focused ? accent.opacity(0.8) : ink.opacity(0.12)),
                                      lineWidth: focused || problem != nil ? 1.5 : 1))
                    .onSubmit {
                        // Return keeps the value and closes, like Done.
                        if commit() { done() }
                    }
                    .onAppear { text = setting.value }
                    .onChange(of: setting.value) { _, v in if !focused { text = v } }
                    .onChange(of: focused) { _, f in if !f { _ = commit() } }
                    .onDisappear { _ = commit() }
                if setting.type == "INT" && setting.max < 1e30 {
                    Stepper("", onIncrement: { step(1) }, onDecrement: { step(-1) })
                        .labelsHidden().controlSize(.small)
                }
            }
        }
    }

    private var isNumber: Bool { setting.type == "INT" || setting.type == "FLOAT" }

    private func format(_ v: Double) -> String {
        v == v.rounded() ? String(Int(v)) : String(v)
    }

    private func step(_ d: Int) {
        let n = Int(Double(text.trimmingCharacters(in: .whitespaces)) ?? Double(setting.value) ?? 0) + d
        text = String(Int(min(max(Double(n), setting.min), setting.max)))
        _ = commit()
    }

    /// Numbers are checked against the library's range, as the wx dialog
    /// does. True when the value is fine (and applied if it changed).
    @discardableResult
    private func commit() -> Bool {
        let v = text.trimmingCharacters(in: .whitespaces)
        if isNumber {
            guard let n = Double(v), setting.type == "FLOAT" || n == n.rounded() else {
                problem = setting.type == "INT" ? "Enter a whole number." : "Enter a number."
                return false
            }
            if n < setting.min || n > setting.max {
                problem = "Must be between \(format(setting.min)) and \(format(setting.max))."
                return false
            }
        }
        problem = nil
        if v != setting.value { apply(v) }
        return true
    }

    private func choose() {
        if setting.type == "FILE_IN" {
            let panel = NSOpenPanel()
            panel.canChooseDirectories = false
            if panel.runModal() == .OK, let url = panel.url { apply(url.path) }
        } else {
            let panel = NSSavePanel()
            if panel.runModal() == .OK, let url = panel.url { apply(url.path) }
        }
    }
}
