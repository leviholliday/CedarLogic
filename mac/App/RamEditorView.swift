// A RAM's or ROM's contents (the wx app's RamPopupDialog, redesigned): sixteen
// words a row with the address down the side, in hex or decimal. The word
// last read glows green and the one last written amber, as it runs. Click a
// word to change it; jump to an address; load or save a .cdm memory file.
// Only the rows on screen are built, so a 64K memory opens as fast as a 16
// word one.

import SwiftUI
import UniformTypeIdentifiers

struct RamEditorView: View {
    let document: CoreDocument
    let gate: Int
    let canvas: CanvasController
    @ObservedObject private var prefs = Prefs.shared
    @Environment(\.dismiss) private var dismiss
    @State private var decimal = false
    @State private var editing: Int?
    @State private var text = ""
    @State private var jump = ""
    @FocusState private var fieldFocused: Bool

    private var bits: (address: Int, data: Int) {
        var a: Int32 = 0, d: Int32 = 0
        _ = cl_ram_info(document.handle, gate, &a, &d)
        return (Int(a), Int(d))
    }

    var body: some View {
        let (aBits, dBits) = bits
        let words = 1 << min(max(aBits, 0), 20)
        let accent = prefs.accentColor(dark: prefs.dark)
        VStack(alignment: .leading, spacing: 14) {
            // Escape: first the value being typed, then the editor.
            Color.clear.frame(height: 0).onEscape { if editing != nil { editing = nil } else { dismiss() } }
            HStack(alignment: .firstTextBaseline) {
                VStack(alignment: .leading, spacing: 3) {
                    Text("Memory").font(.system(size: 20, weight: .bold))
                    Text("\(words.formatted()) addresses × \(dBits) bits").font(.system(size: 12)).foregroundStyle(.secondary)
                }
                Spacer()
                Picker("", selection: $decimal) { Text("Hex").tag(false); Text("Decimal").tag(true) }
                    .pickerStyle(.segmented).labelsHidden().frame(width: 150)
            }
            HStack(spacing: 14) {
                legend(Color.green, "Last read")
                legend(Color.orange, "Last written")
                Spacer()
                TextField("Go to address (hex)", text: $jump).textFieldStyle(.roundedBorder).frame(width: 170)
                    .onSubmit { }
            }
            ScrollViewReader { proxy in
                ScrollView([.vertical]) {
                    LazyVStack(alignment: .leading, spacing: 2, pinnedViews: [.sectionHeaders]) {
                        Section {
                            ForEach(0..<max(1, words / 16), id: \.self) { r in
                                MemoryRow(document: document, gate: gate, row: r, words: words, dataBits: dBits,
                                          decimal: decimal, status: canvas.status, accent: accent,
                                          editing: $editing, text: $text, fieldFocused: $fieldFocused,
                                          commit: commit)
                                    .id(r)
                            }
                        } header: {
                            HStack(spacing: 3) {
                                Text("").frame(width: 76)
                                ForEach(0..<min(16, words), id: \.self) { c in
                                    Text(String(c, radix: 16).uppercased())
                                        .font(.system(size: 10.5, weight: .semibold, design: .monospaced))
                                        .foregroundStyle(.secondary)
                                        .frame(width: cellWidth(dBits))
                                }
                            }
                            .padding(.vertical, 5)
                            .background(.bar)
                        }
                    }
                    .padding(8)
                }
                .background(RoundedRectangle(cornerRadius: 10).fill(Color.primary.opacity(0.035)))
                .overlay(RoundedRectangle(cornerRadius: 10).strokeBorder(Color.primary.opacity(0.08)))
                .onChange(of: jump) { _, j in
                    if let a = Int(j.trimmingCharacters(in: .whitespaces), radix: 16), a >= 0, a < words {
                        withAnimation(.easeOut(duration: 0.25)) { proxy.scrollTo(a / 16, anchor: .top) }
                    }
                }
            }
            .frame(minHeight: 300)
            HStack {
                Button("Load File…") { load() }
                Button("Save File…") { save() }
                Spacer()
                Text("Click a value to change it.").font(.caption).foregroundStyle(.secondary)
                Button("Done") { dismiss() }.keyboardShortcut(.defaultAction)
            }
        }
        .padding(20)
        .frame(width: 76 + 16 * (cellWidth(bits.data) + 3) + 60, height: 560)
    }

    private func legend(_ c: Color, _ s: String) -> some View {
        HStack(spacing: 6) {
            RoundedRectangle(cornerRadius: 3).fill(c.opacity(0.45)).frame(width: 14, height: 10)
            Text(s).font(.system(size: 11.5)).foregroundStyle(.secondary)
        }
    }

    private func commit(_ addr: Int) {
        let t = text.trimmingCharacters(in: .whitespaces)
        if let v = decimal ? UInt(t) : UInt(t, radix: 16) {
            cl_ram_set(document.handle, gate, UInt(addr), v)
            canvas.markEdited()
            canvas.redraw()
        }
        editing = nil
    }

    private func load() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [UTType(filenameExtension: "cdm") ?? .data, UTType(filenameExtension: "hex") ?? .data]
        guard panel.runModal() == .OK, let url = panel.url else { return }
        cl_ram_load_file(document.handle, gate, url.path)
        canvas.markEdited()
        canvas.redraw()
    }

    private func save() {
        let panel = NSSavePanel()
        panel.nameFieldStringValue = "Memory.cdm"
        panel.allowedContentTypes = [UTType(filenameExtension: "cdm") ?? .data]
        guard panel.runModal() == .OK, let url = panel.url else { return }
        cl_ram_save_file(document.handle, gate, url.path)
    }
}

private func cellWidth(_ dataBits: Int) -> CGFloat { CGFloat(max(3, (dataBits + 3) / 4 + 1)) * 7.5 + 8 }

/// Sixteen words, refreshed as the simulation reads and writes them.
private struct MemoryRow: View {
    let document: CoreDocument
    let gate: Int
    let row: Int
    let words: Int
    let dataBits: Int
    let decimal: Bool
    @ObservedObject var status: CanvasStatus
    let accent: Color
    @Binding var editing: Int?
    @Binding var text: String
    var fieldFocused: FocusState<Bool>.Binding
    let commit: (Int) -> Void

    var body: some View {
        let _ = status.version
        let read = Int(cl_ram_last_read(document.handle, gate)), written = Int(cl_ram_last_written(document.handle, gate))
        let digits = max(1, (dataBits + 3) / 4)
        HStack(spacing: 3) {
            Text(String(format: "0x%0*X", max(1, (Int(log2(Double(max(words, 16)))) + 3) / 4), row * 16))
                .font(.system(size: 11, design: .monospaced))
                .foregroundStyle(.secondary)
                .frame(width: 76, alignment: .leading)
            ForEach(0..<min(16, words), id: \.self) { c in
                let addr = row * 16 + c
                let value = cl_ram_value(document.handle, gate, UInt(addr))
                let shown = decimal ? String(value)
                    : String(repeating: "0", count: max(0, digits - String(value, radix: 16).count)) + String(value, radix: 16).uppercased()
                if editing == addr {
                    TextField("", text: $text)
                        .font(.system(size: 11.5, design: .monospaced))
                        .textFieldStyle(.plain)
                        .multilineTextAlignment(.center)
                        .frame(width: cellWidth(dataBits), height: 22)
                        .background(RoundedRectangle(cornerRadius: 5).fill(Color(nsColor: .textBackgroundColor)))
                        .overlay(RoundedRectangle(cornerRadius: 5).strokeBorder(accent, lineWidth: 1.5))
                        .focused(fieldFocused)
                        .onSubmit { commit(addr) }
                        .onExitCommand { editing = nil }
                } else {
                    Text(shown)
                        .font(.system(size: 11.5, design: .monospaced))
                        .foregroundStyle(value == 0 ? Color.secondary.opacity(0.6) : Color.primary)
                        .frame(width: cellWidth(dataBits), height: 22)
                        .background(RoundedRectangle(cornerRadius: 5).fill(
                            addr == read ? Color.green.opacity(0.35) : addr == written ? Color.orange.opacity(0.35) : Color.primary.opacity(0.04)))
                        .contentShape(Rectangle())
                        .onTapGesture { text = shown; editing = addr; fieldFocused.wrappedValue = true }
                        .animation(.easeOut(duration: 0.25), value: addr == read || addr == written)
                }
            }
        }
    }
}
