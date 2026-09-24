// A truth table built by trying every combination of the page's switches
// (or the selected ones). Column names come from nearby labels and can be
// changed here; Copy puts it on the clipboard as a tab-separated table that
// pastes into Word, Docs or a spreadsheet.

import AppKit
import SwiftUI

struct TruthTable: Identifiable {
    let id = UUID()
    var names: [String]
    let inputs: Int
    let rows: [[Character]]
    let sequential: Bool
    let unsettled: Int

    init?(document: CoreDocument, page: Int, error: inout String) {
        var buf = [CChar](repeating: 0, count: 512)
        guard let tt = cl_truth_table(document.handle, Int32(page), &buf, Int32(buf.count)) else {
            error = String(cString: buf)
            return nil
        }
        defer { cl_tt_free(tt) }
        let cols = Int(cl_tt_columns(tt))
        names = (0..<cols).map { String(cString: cl_tt_name(tt, Int32($0))) }
        inputs = Int(cl_tt_inputs(tt))
        rows = (0..<Int(cl_tt_rows(tt))).map { r in
            (0..<cols).map { c in Character(UnicodeScalar(UInt8(bitPattern: cl_tt_cell(tt, Int32(r), Int32(c))))) }
        }
        sequential = cl_tt_sequential(tt)
        unsettled = Int(cl_tt_unsettled(tt))
    }

    func text(separator: String) -> String {
        ([names.joined(separator: separator)] + rows.map { $0.map(String.init).joined(separator: separator) })
            .joined(separator: "\n") + "\n"
    }
}

struct TruthTableView: View {
    @State var table: TruthTable
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Truth Table").font(.title2.weight(.semibold))
            if table.sequential {
                Label("This page has clocks or flip-flops, so outputs can depend on what happened before. Each row is read after the circuit settles from the row above it.",
                      systemImage: "info.circle")
                    .font(.callout).foregroundStyle(.secondary)
            }
            if table.unsettled > 0 {
                Label("\(table.unsettled) row\(table.unsettled == 1 ? "" : "s") never stopped changing (a clock or an oscillation), so those outputs are a snapshot.",
                      systemImage: "exclamationmark.triangle")
                    .font(.callout).foregroundStyle(.orange)
            }
            ScrollView([.vertical, .horizontal]) {
                Grid(horizontalSpacing: 0, verticalSpacing: 0) {
                    GridRow {
                        ForEach(table.names.indices, id: \.self) { c in
                            TextField("", text: $table.names[c])
                                .textFieldStyle(.plain)
                                .multilineTextAlignment(.center)
                                .font(.body.weight(.semibold))
                                .frame(width: 64)
                                .padding(.vertical, 6)
                                .background(c < table.inputs ? Color.secondary.opacity(0.10) : Color.accentColor.opacity(0.14))
                                .overlay(alignment: .leading) { if c == table.inputs { Rectangle().frame(width: 2).foregroundStyle(.secondary) } }
                        }
                    }
                    ForEach(table.rows.indices, id: \.self) { r in
                        GridRow {
                            ForEach(table.rows[r].indices, id: \.self) { c in
                                let v = table.rows[r][c]
                                Text(String(v))
                                    .font(.body.monospaced())
                                    .foregroundStyle(v == "1" ? Color.primary : v == "0" ? Color.secondary : Color.orange)
                                    .frame(width: 64)
                                    .padding(.vertical, 4)
                                    .background(r % 2 == 1 ? Color.secondary.opacity(0.05) : Color.clear)
                                    .overlay(alignment: .leading) { if c == table.inputs { Rectangle().frame(width: 2).foregroundStyle(.secondary) } }
                            }
                        }
                    }
                }
            }
            .frame(minHeight: 160, maxHeight: 420)
            Text("Click a column's name to rename it. X unknown · Z floating · ! conflict · – not connected")
                .font(.caption).foregroundStyle(.secondary)
            HStack {
                Button("Copy") {
                    NSPasteboard.general.clearContents()
                    NSPasteboard.general.setString(table.text(separator: "\t"), forType: .string)
                }
                .help("A tab-separated table: pastes into Word, Docs, Numbers or Excel")
                Button("Export as CSV…") { export() }
                Spacer()
                Button("Done") { dismiss() }.keyboardShortcut(.defaultAction)
            }
        }
        .padding(20)
        .frame(minWidth: 360)
    }

    private func export() {
        let panel = NSSavePanel()
        panel.nameFieldStringValue = "Truth Table.csv"
        panel.allowedContentTypes = [.commaSeparatedText]
        if panel.runModal() == .OK, let url = panel.url {
            try? table.text(separator: ",").write(to: url, atomically: true, encoding: .utf8)
        }
    }
}
