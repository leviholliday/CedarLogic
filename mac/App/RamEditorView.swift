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
    @Environment(\.displayScale) private var scale
    @State private var decimal = false
    @State private var editing: Int?
    @State private var text = ""
    @State private var jump = ""
    @FocusState private var fieldFocused: Bool

    private var dark: Bool { prefs.dark }
    private var paper: Color { dark ? CLChrome.rgb(28, 31, 37) : CLChrome.rgb(250, 250, 252) }
    private var card: Color { dark ? CLChrome.rgb(36, 40, 47) : .white }
    private var ink: Color { dark ? CLChrome.rgb(226, 230, 238) : CLChrome.rgb(30, 33, 40) }
    private var line: Color { ink.opacity(dark ? 0.08 : 0.09) }
    /// The word last read (green, as a lit wire) and last written (amber).
    static func readColor(_ dark: Bool) -> Color { dark ? Color(.sRGB, red: 0.22, green: 0.96, blue: 0.44) : Color(.sRGB, red: 0.05, green: 0.68, blue: 0.27) }
    static func writtenColor(_ dark: Bool) -> Color { dark ? Color(.sRGB, red: 1, green: 0.72, blue: 0.25) : Color(.sRGB, red: 0.93, green: 0.55, blue: 0.05) }

    private var bits: (address: Int, data: Int) {
        var a: Int32 = 0, d: Int32 = 0
        _ = cl_ram_info(document.handle, gate, &a, &d)
        return (Int(a), Int(d))
    }

    var body: some View {
        let (aBits, dBits) = bits
        let words = 1 << min(max(aBits, 0), 20)
        let accent = prefs.accentColor(dark: dark)
        VStack(alignment: .leading, spacing: 0) {
            // Escape: first the value being typed, then the editor.
            Color.clear.frame(height: 0).onEscape { if editing != nil { editing = nil } else { dismiss() } }
            // The part's picture and name, as in its settings.
            HStack(spacing: 14) {
                Image(nsImage: TileCache.image(document.libraryName(ofGate: gate), size: 44, dark: dark, scale: scale))
                    .frame(width: 44, height: 36).padding(6)
                    .background(RoundedRectangle(cornerRadius: 10, style: .continuous).fill(card))
                    .overlay(RoundedRectangle(cornerRadius: 10, style: .continuous).strokeBorder(line))
                VStack(alignment: .leading, spacing: 3) {
                    Text(document.caption(ofGate: gate)).font(.system(size: 16, weight: .bold)).foregroundStyle(ink).lineLimit(1)
                    Text("\(words.formatted()) addresses \u{00D7} \(dBits) bits \u{00B7} click a value to change it")
                        .font(.system(size: 11)).foregroundStyle(ink.opacity(0.5))
                }
                Spacer()
                Segmented(options: ["Hex", "Decimal"], selection: Binding(get: { decimal ? 1 : 0 }, set: { decimal = $0 == 1 }),
                          ink: ink, accent: accent, onAccent: prefs.onAccentColor(dark: dark))
            }
            .padding(.horizontal, 20).padding(.top, 20).padding(.bottom, 12)
            HStack(spacing: 14) {
                legend(Self.readColor(dark), "Last read")
                legend(Self.writtenColor(dark), "Last written")
                Spacer()
                HStack(spacing: 6) {
                    Image(systemName: "arrow.down.to.line").font(.system(size: 11)).foregroundStyle(ink.opacity(0.45))
                    TextField("Go to address (hex)", text: $jump)
                        .textFieldStyle(.plain).font(.system(size: 12, design: .monospaced))
                }
                .padding(.horizontal, 9).padding(.vertical, 5)
                .frame(width: 180)
                .background(RoundedRectangle(cornerRadius: 7, style: .continuous).fill(ink.opacity(dark ? 0.07 : 0.05)))
                .overlay(RoundedRectangle(cornerRadius: 7, style: .continuous).strokeBorder(ink.opacity(0.12)))
            }
            .padding(.horizontal, 20).padding(.bottom, 10)
            ScrollViewReader { proxy in
                ScrollView([.vertical]) {
                    LazyVStack(alignment: .leading, spacing: 2, pinnedViews: [.sectionHeaders]) {
                        Section {
                            ForEach(0..<max(1, words / 16), id: \.self) { r in
                                MemoryRow(document: document, gate: gate, row: r, words: words, dataBits: dBits,
                                          decimal: decimal, status: canvas.status, accent: accent, ink: ink, dark: dark,
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
                                        .foregroundStyle(ink.opacity(0.45))
                                        .frame(width: cellWidth(dBits))
                                }
                            }
                            .padding(.vertical, 6)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .background(card)
                        }
                    }
                    .padding(.horizontal, 10).padding(.bottom, 10)
                }
                .background(RoundedRectangle(cornerRadius: 12, style: .continuous).fill(card))
                .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
                .overlay(RoundedRectangle(cornerRadius: 12, style: .continuous).strokeBorder(line))
                .onChange(of: jump) { _, j in
                    if let a = Int(j.trimmingCharacters(in: .whitespaces), radix: 16), a >= 0, a < words {
                        withAnimation(.easeOut(duration: 0.25)) { proxy.scrollTo(a / 16, anchor: .top) }
                    }
                }
            }
            .frame(minHeight: 300)
            .padding(.horizontal, 20)
            HStack(spacing: 8) {
                footButton("square.and.arrow.down", "Load File\u{2026}") { load() }
                    .help("Fill the memory from a .cdm or .hex file")
                footButton("square.and.arrow.up", "Save File\u{2026}") { save() }
                    .help("Save what's in the memory as a .cdm file")
                Spacer()
                Button { dismiss() } label: {
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
                .keyboardShortcut(editing == nil ? .defaultAction : nil)
            }
            .padding(.horizontal, 20).padding(.top, 14).padding(.bottom, 18)
        }
        .frame(width: 76 + 16 * (cellWidth(bits.data) + 3) + 80, height: 580)
        .background(paper)
        .preferredColorScheme(dark ? .dark : .light)
    }

    private func legend(_ c: Color, _ s: String) -> some View {
        HStack(spacing: 6) {
            RoundedRectangle(cornerRadius: 3).fill(c.opacity(0.4)).frame(width: 14, height: 10)
                .overlay(RoundedRectangle(cornerRadius: 3).strokeBorder(c.opacity(0.8), lineWidth: 1))
            Text(s).font(.system(size: 11.5)).foregroundStyle(ink.opacity(0.6))
        }
    }

    private func footButton(_ icon: String, _ title: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack(spacing: 6) {
                Image(systemName: icon).font(.system(size: 11, weight: .semibold))
                Text(title).font(.system(size: 12.5, weight: .medium))
            }
            .padding(.horizontal, 12).padding(.vertical, 7)
            .background(RoundedRectangle(cornerRadius: 8, style: .continuous).fill(ink.opacity(0.08)))
            .foregroundStyle(ink)
        }
        .buttonStyle(.plain)
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

/// Two or three choices in a pill, the chosen one filled with the accent.
private struct Segmented: View {
    let options: [String]
    @Binding var selection: Int
    let ink: Color
    let accent: Color
    let onAccent: Color

    var body: some View {
        HStack(spacing: 2) {
            ForEach(options.indices, id: \.self) { i in
                let on = i == selection
                Button { withAnimation(.easeOut(duration: 0.15)) { selection = i } } label: {
                    Text(options[i]).font(.system(size: 12, weight: on ? .semibold : .medium))
                        .foregroundStyle(on ? onAccent : ink.opacity(0.75))
                        .padding(.horizontal, 12).frame(height: 24)
                        .background(Capsule().fill(on ? accent : .clear))
                        .contentShape(Capsule())
                }
                .buttonStyle(.plain)
            }
        }
        .padding(2)
        .background(Capsule().fill(ink.opacity(0.07)))
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
    let ink: Color
    let dark: Bool
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
                .foregroundStyle(ink.opacity(0.45))
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
                        .foregroundStyle(value == 0 ? ink.opacity(0.35) : ink)
                        .frame(width: cellWidth(dataBits), height: 22)
                        .background(RoundedRectangle(cornerRadius: 5).fill(
                            addr == read ? RamEditorView.readColor(dark).opacity(0.35)
                                : addr == written ? RamEditorView.writtenColor(dark).opacity(0.35) : ink.opacity(0.045)))
                        .contentShape(Rectangle())
                        .onTapGesture { text = shown; editing = addr; fieldFocused.wrappedValue = true }
                        .animation(.easeOut(duration: 0.25), value: addr == read || addr == written)
                }
            }
        }
    }
}
