// The inspector: the selected gate's settings, the same ones the wx app's
// parameters dialog lists, each with a control that suits its type. A change
// applies at once and can be undone.

import AppKit
import SwiftUI

struct InspectorView: View {
    let document: CoreDocument
    @ObservedObject var controller: CanvasController

    var body: some View {
        let _ = controller.selectionVersion   // reload when the selection changes
        let page = controller.page
        Form {
            if let gate = document.singleSelectedGate(page: page) {
                Section(document.caption(ofGate: gate)) {
                    let settings = document.settings(ofGate: gate)
                    if settings.isEmpty {
                        Text("This part has no settings.").foregroundStyle(.secondary)
                    }
                    ForEach(settings) { s in
                        SettingRow(setting: s) { value in
                            document.setSetting(gate: gate, name: s.name, value: value)
                            controller.redraw()
                            controller.editsChanged()
                            controller.selectionChanged()
                        }
                    }
                }
                Section {
                    HStack {
                        Button("Rotate") { controller.rotate() }
                            .help("Turn a quarter turn clockwise (R). Parts with wires attached stay put.")
                        Button("Delete", role: .destructive) { controller.deleteSelection() }
                    }
                }
            } else if document.hasSelection(page: page) {
                Text("Several things are selected. Select one part to see its settings.")
                    .foregroundStyle(.secondary)
            } else {
                Text("Select a part to see its settings.")
                    .foregroundStyle(.secondary)
            }
        }
        .formStyle(.grouped)
    }
}

/// One setting, with the control its type calls for.
private struct SettingRow: View {
    let setting: CoreDocument.Setting
    let apply: (String) -> Void
    @State private var text = ""
    @State private var problem: String?

    var body: some View {
        switch setting.type {
        case "BOOL":
            Toggle(setting.label, isOn: Binding(get: { setting.value == "true" },
                                                set: { apply($0 ? "true" : "false") }))
        case "FILE_IN", "FILE_OUT":
            LabeledContent(setting.label) {
                HStack {
                    Text(setting.value.isEmpty ? "None" : (setting.value as NSString).lastPathComponent)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                        .truncationMode(.middle)
                    Button("Choose…") { choose() }
                }
            }
        default:
            VStack(alignment: .leading, spacing: 2) {
                TextField(setting.label, text: $text)
                    .onSubmit { commit() }
                    .onAppear { text = setting.value }
                    .onChange(of: setting.value) { _, v in text = v }
                if let problem {
                    Text(problem).font(.caption).foregroundStyle(.red)
                } else if isNumber, setting.max < 1e30 {
                    Text("\(format(setting.min)) to \(format(setting.max))").font(.caption).foregroundStyle(.secondary)
                }
            }
        }
    }

    private var isNumber: Bool { setting.type == "INT" || setting.type == "FLOAT" }

    private func format(_ v: Double) -> String {
        v == v.rounded() ? String(Int(v)) : String(v)
    }

    /// Numbers are checked against the library's range, as the wx dialog does.
    private func commit() {
        let v = text.trimmingCharacters(in: .whitespaces)
        if isNumber {
            guard let n = Double(v), setting.type == "FLOAT" || n == n.rounded() else {
                problem = setting.type == "INT" ? "Enter a whole number." : "Enter a number."
                return
            }
            if n < setting.min || n > setting.max {
                problem = "Must be between \(format(setting.min)) and \(format(setting.max))."
                return
            }
        }
        problem = nil
        if v != setting.value { apply(v) }
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
