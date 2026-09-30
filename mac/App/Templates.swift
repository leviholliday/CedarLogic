// Templates: File ▸ New from Template starts a circuit from a built-in
// starter (a lab page, a counter, a 7-segment decoder) or one of yours;
// File ▸ Save as Template keeps the circuit you're in as one of yours.
//
// Yours are folders in ~/Library/Application Support/CedarLogic/Templates,
// each holding name.txt and template.cdl. The built-in ones are built by
// the engine when first asked for, so they're always in the current format.

import AppKit
import SwiftUI

struct CircuitTemplate: Identifiable, Hashable {
    let id: String
    let name: String
    let detail: String
    let text: String
    /// Yours (a folder), or nil for a built-in one.
    let folder: URL?
}

@MainActor
enum Templates {
    /// Set just before a new document is made from a template; the document
    /// takes it (CircuitDocument.init).
    nonisolated(unsafe) static var pendingText: String?
    nonisolated(unsafe) static var pendingName: String?

    static var root: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("CedarLogic/Templates", isDirectory: true)
    }

    static func yours() -> [CircuitTemplate] {
        let fm = FileManager.default
        let ids = (try? fm.contentsOfDirectory(atPath: root.path)) ?? []
        return ids.filter { !$0.hasPrefix(".") }.compactMap { id -> (CircuitTemplate, Date)? in
            let folder = root.appendingPathComponent(id, isDirectory: true)
            let file = folder.appendingPathComponent("template.cdl")
            guard let text = try? String(contentsOf: file, encoding: .utf8) else { return nil }
            let name = (try? String(contentsOf: folder.appendingPathComponent("name.txt"), encoding: .utf8))?
                .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
            let date = (try? fm.attributesOfItem(atPath: file.path)[.modificationDate] as? Date) ?? .distantPast
            return (CircuitTemplate(id: id, name: name.isEmpty ? "Untitled Template" : name,
                                    detail: "Saved " + friendlyTime(date), text: text, folder: folder), date)
        }
        .sorted { $0.1 > $1.1 }
        .map(\.0)
    }

    static func use(_ t: CircuitTemplate) {
        pendingText = t.text
        pendingName = t.name
        NSDocumentController.shared.newDocument(nil)
        pendingText = nil
    }


    static func saveCurrent(from canvas: CanvasController) {
        guard let document = canvas.document else { return }
        let text = document.saveText()
        guard let name = MyParts.askName(title: "Save as Template",
                                         message: "Name your template. Start a circuit from it with File \u{25B8} New from Template\u{2026}",
                                         initial: canvas.documentTitle, button: "Save") else { return }
        let f = DateFormatter()
        f.dateFormat = "yyyyMMdd-HHmmss"
        let folder = root.appendingPathComponent("\(f.string(from: Date()))-\(Int.random(in: 1000...99999))", isDirectory: true)
        do {
            try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
            try name.write(to: folder.appendingPathComponent("name.txt"), atomically: true, encoding: .utf8)
            try text.write(to: folder.appendingPathComponent("template.cdl"), atomically: true, encoding: .utf8)
            canvas.note("Saved \u{201C}\(name)\u{201D} as a template.")
        } catch {
            canvas.note("Couldn't save that template.")
        }
    }

    static func rename(_ t: CircuitTemplate, to name: String) {
        guard let folder = t.folder else { return }
        try? name.write(to: folder.appendingPathComponent("name.txt"), atomically: true, encoding: .utf8)
    }

    static func delete(_ t: CircuitTemplate) {
        guard let folder = t.folder else { return }
        try? FileManager.default.trashItem(at: folder, resultingItemURL: nil)
    }

    // MARK: Built in

    /// New circuits' starting point (Settings > General), or nil for a blank page.
    nonisolated(unsafe) static var nextIsBlank = false

    static func template(id: String) -> CircuitTemplate? {
        builtIn.first { $0.id == id } ?? yours().first { $0.id == id }
    }

    /// Built by the engine when first asked for (and again when your name
    /// changes: the lab page carries it).
    private static var builtCache: (name: String, list: [CircuitTemplate])?
    static var builtIn: [CircuitTemplate] {
        let name = Prefs.shared.studentName.trimmingCharacters(in: .whitespacesAndNewlines)
        if let c = builtCache, c.name == name { return c.list }
        let list = makeBuiltIn(name: name)
        builtCache = (name, list)
        return list
    }

    private static func makeBuiltIn(name: String) -> [CircuitTemplate] { [
        CircuitTemplate(id: "builtin-lab", name: "Lab Page",
                        detail: "A title, your name, the date and the course, with room for inputs and outputs",
                        text: labPage(name: name), folder: nil),
        CircuitTemplate(id: "builtin-counter", name: "4-Bit Counter",
                        detail: "A clock driving a counting register, shown on a hex display",
                        text: counter(), folder: nil),
        CircuitTemplate(id: "builtin-7seg", name: "7-Segment Decoder Starter",
                        detail: "Four switches and seven segment lights: build the decoder between them",
                        text: sevenSegment(), folder: nil),
    ] }

    private struct Part { let gate: String; let x: Double; let y: Double; var label: String? = nil }
    private struct Wire { let from: Int; let fromPin: String; let to: Int; let toPin: String }

    /// Builds parts and wires on a new circuit and returns its file text.
    /// Labels named in `big` get larger text.
    private static func build(_ parts: [Part], _ wires: [Wire], big: [String: Double] = [:]) -> String {
        let doc = CoreDocument()
        var strings: [UnsafeMutablePointer<CChar>] = []
        defer { strings.forEach { free($0) } }
        func c(_ s: String) -> UnsafePointer<CChar> { let p = strdup(s)!; strings.append(p); return UnsafePointer(p) }
        let gates = parts.map { CLBuildGate(gate: c($0.gate), x: $0.x, y: $0.y, label: $0.label.map(c)) }
        let links = wires.map { CLBuildWire(from: Int32($0.from), fromPin: c($0.fromPin), to: Int32($0.to), toPin: c($0.toPin)) }
        _ = cl_edit_build(doc.handle, 0, gates, Int32(gates.count), links, Int32(links.count), "Template")
        cl_edit_select_none(doc.handle, 0)
        var text = doc.saveText()
        for (label, height) in big {
            let quoted = NSRegularExpression.escapedPattern(for: label)
            if let re = try? NSRegularExpression(pattern: "(\\(gparam \"LABEL_TEXT\" \"\(quoted)\"\\)\\s*\\(gparam \"TEXT_HEIGHT\" \")[^\"]*") {
                text = re.stringByReplacingMatches(in: text, range: NSRange(text.startIndex..., in: text),
                                                   withTemplate: "$1\(height)")
            }
        }
        return text
    }

    /// A label whose left edge is at x (labels are placed by their middle).
    private static func label(_ text: String, x: Double, y: Double, height: Double = 2) -> Part {
        Part(gate: "AA_LABEL", x: x + Double(text.count) * 0.3 * height, y: y, label: text)
    }

    private static func labPage(name: String) -> String {
        build([
            label("Lab 1: Title", x: -3.5, y: 12, height: 4),
            label(name.isEmpty ? "Name: ______________________" : "Name: \(name)", x: 0, y: 5),
            label("Date: ____________", x: 44, y: 5),
            label("Course: ____________", x: -1.6, y: 1),
            label("Inputs", x: -1.4, y: -8),
            label("Outputs", x: 60, y: -8),
        ], [], big: ["Lab 1: Title": 4])
    }

    private static func counter() -> String {
        var p: [Part] = [
            Part(gate: "BB_CLOCK", x: 0, y: 0),               // 0
            Part(gate: "AA_REGISTER4", x: 16, y: 0),          // 1
            Part(gate: "EE_VDD", x: 10, y: 11),               // 2
            Part(gate: "FF_GND", x: 10, y: -11),              // 3
            Part(gate: "GE_LED_DISPLAY_4BIT", x: 32, y: 0),   // 4
        ]
        p += [label("4-Bit Counter", x: 0, y: 18, height: 3),
              label("Clock", x: -3, y: -4),
              label("Count", x: 29, y: -5)]
        var w = [Wire(from: 0, fromPin: "CLK", to: 1, toPin: "clock"),
                 Wire(from: 2, fromPin: "OUT_0", to: 1, toPin: "count_enable"),
                 Wire(from: 2, fromPin: "OUT_0", to: 1, toPin: "count_up"),
                 Wire(from: 3, fromPin: "OUT_0", to: 1, toPin: "load"),
                 Wire(from: 3, fromPin: "OUT_0", to: 1, toPin: "clear")]
        for i in 0..<4 { w.append(Wire(from: 1, fromPin: "OUT_\(i)", to: 4, toPin: "IN_\(i)")) }
        return build(p, w, big: ["4-Bit Counter": 3])
    }

    private static func sevenSegment() -> String {
        var p: [Part] = []
        var w: [Wire] = []
        // Switches D (top, most significant) down to A.
        let names = ["D", "C", "B", "A"]
        for (i, n) in names.enumerated() {
            let y = 6.0 - Double(i) * 4
            p.append(Part(gate: "AA_TOGGLE", x: 0, y: y))
            p.append(label(n, x: -4, y: y))
        }
        let display = p.count
        p.append(Part(gate: "GE_LED_DISPLAY_4BIT", x: 14, y: 16))
        for (i, _) in names.enumerated() {
            w.append(Wire(from: i * 2, fromPin: "OUT_0", to: display, toPin: "IN_\(3 - i)"))
        }
        // The segments, laid out as the digit: a on top, g in the middle.
        let segments: [(String, Double, Double)] = [("a", 70, 12), ("b", 75, 7), ("c", 75, -3), ("d", 70, -8),
                                                     ("e", 65, -3), ("f", 65, 7), ("g", 70, 2)]
        for (n, x, y) in segments {
            p.append(Part(gate: "GA_LED", x: x, y: y))
            p.append(label(n, x: x + 1.6, y: y + 1.6))
        }
        p += [label("7-Segment Decoder", x: 0, y: 26, height: 3),
              label("Hex value", x: 10, y: 9),
              label("Build your decoder here", x: 26, y: -12),
              label("Segments", x: 64, y: 17)]
        return build(p, w, big: ["7-Segment Decoder": 3])
    }
}

// MARK: - The picker

struct TemplatePicker: View {
    @Environment(\.dismiss) private var dismiss
    @ObservedObject private var prefs = Prefs.shared
    @State private var mine: [CircuitTemplate] = []
    @State private var selection: String = Templates.builtIn[0].id

    private var dark: Bool { prefs.dark }
    private var paper: Color { dark ? CLChrome.rgb(28, 31, 37) : CLChrome.rgb(250, 250, 252) }
    private var ink: Color { dark ? CLChrome.rgb(226, 230, 238) : CLChrome.rgb(30, 33, 40) }
    private var all: [CircuitTemplate] { Templates.builtIn + mine }
    private var selected: CircuitTemplate? { all.first { $0.id == selection } }

    var body: some View {
        let accent = prefs.accentColor(dark: dark)
        VStack(alignment: .leading, spacing: 0) {
            Text("New from Template").font(.system(size: 19, weight: .bold)).foregroundStyle(ink)
                .padding(.horizontal, 22).padding(.top, 22)
            Text("Start a circuit from one of these. Save your own with File \u{25B8} Save as Template\u{2026}")
                .font(.system(size: 13)).foregroundStyle(ink.opacity(0.55))
                .padding(.horizontal, 22).padding(.top, 8)
            HStack(spacing: 14) {
                ScrollView {
                    VStack(alignment: .leading, spacing: 2) {
                        heading("Built In")
                        ForEach(Templates.builtIn) { row($0, accent: accent) }
                        heading("Yours")
                        if mine.isEmpty {
                            Text("None yet.").font(.system(size: 12)).foregroundStyle(ink.opacity(0.5))
                                .padding(.horizontal, 14).padding(.vertical, 6)
                        }
                        ForEach(mine) { row($0, accent: accent) }
                    }
                    .padding(.vertical, 6)
                }
                .frame(width: 320)
                TemplatePreview(text: selected?.text, dark: dark, ink: ink)
            }
            .padding(14)
            HStack(spacing: 8) {
                if let t = selected, t.folder != nil {
                    Button("Rename\u{2026}") { rename(t) }
                    Button("Delete\u{2026}") { delete(t) }
                }
                Spacer()
                Button("Cancel") { dismiss() }.keyboardShortcut(.cancelAction)
                Button("Use Template", action: useSelected).keyboardShortcut(.defaultAction).disabled(selected == nil)
            }
            .padding(.horizontal, 22).padding(.bottom, 22)
        }
        .frame(minWidth: 760, idealWidth: 940, minHeight: 460, idealHeight: 620)
        .background(paper)
        .background(OverFrontCircuit())
        .preferredColorScheme(dark ? .dark : .light)
        .onAppear(perform: reload)
        .onReceive(NotificationCenter.default.publisher(for: NSWindow.didBecomeKeyNotification)) { _ in reload() }
    }

    private func rename(_ t: CircuitTemplate) {
        guard let name = MyParts.askName(title: "Rename Template", message: "", initial: t.name, button: "Rename") else { return }
        Templates.rename(t, to: name)
        reload()
    }

    private func delete(_ t: CircuitTemplate) {
        let alert = NSAlert()
        alert.messageText = "Delete \u{201C}\(t.name)\u{201D}?"
        alert.informativeText = "It goes to the Trash. Circuits you made from it aren't touched."
        alert.addButton(withTitle: "Delete")
        alert.addButton(withTitle: "Cancel")
        alert.buttons.first?.hasDestructiveAction = true
        Library.present(alert) { ok in
            guard ok else { return }
            Templates.delete(t)
            reload()
            selection = Templates.builtIn[0].id
        }
    }

    private func reload() {
        mine = Templates.yours()
        if selected == nil { selection = Templates.builtIn[0].id }
    }

    private func useSelected() {
        guard let t = selected else { return }
        dismiss()
        DispatchQueue.main.async { Templates.use(t) }
    }

    private func heading(_ s: String) -> some View {
        Text(s.uppercased()).font(.system(size: 10.5, weight: .semibold)).tracking(0.6)
            .foregroundStyle(ink.opacity(0.45))
            .padding(.horizontal, 14).padding(.top, 10).padding(.bottom, 4)
    }

    private func row(_ t: CircuitTemplate, accent: Color) -> some View {
        let sel = t.id == selection
        return VStack(alignment: .leading, spacing: 3) {
            Text(t.name).font(.system(size: 13, weight: .bold)).foregroundStyle(ink).lineLimit(1)
            Text(t.detail).font(.system(size: 11)).foregroundStyle(ink.opacity(0.55)).lineLimit(2)
        }
        .padding(.horizontal, 14).padding(.vertical, 9)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(RoundedRectangle(cornerRadius: 10).fill(sel ? accent.opacity(dark ? 0.26 : 0.16) : .clear))
        .contentShape(Rectangle())
        .contextMenu {
            if t.folder != nil {
                Button("Rename\u{2026}") { rename(t) }
                Button("Delete\u{2026}") { delete(t) }
            }
        }
        .onTapGesture(count: 2) { selection = t.id; useSelected() }
        .onTapGesture { selection = t.id }
        .padding(.horizontal, 6)
    }
}

/// A template drawn fitted, on paper.
private struct TemplatePreview: View {
    let text: String?
    let dark: Bool
    let ink: Color
    @State private var image: CGImage?
    private static var cache: [String: CGImage] = [:]

    var body: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 12).fill(dark ? CLChrome.rgb(22, 24, 29) : .white)
                .overlay(RoundedRectangle(cornerRadius: 12).strokeBorder(ink.opacity(0.12)))
            if let image {
                Image(decorative: image, scale: 2).resizable().scaledToFit().padding(12)
            } else {
                Text("An empty page").font(.system(size: 12)).foregroundStyle(ink.opacity(0.5))
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .onAppear(perform: render)
        .onChange(of: text) { _, _ in render() }
        .onChange(of: dark) { _, _ in render() }
    }

    private func render() {
        guard let text else { image = nil; return }
        let key = "\(text.hashValue)|\(dark)"
        if let hit = Self.cache[key] { image = hit; return }
        guard let doc = try? CoreDocument(data: Data(text.utf8)) else { image = nil; return }
        let w = 1200, h = 820
        guard let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: 0,
                                  space: CGColorSpace(name: CGColorSpace.sRGB)!,
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return }
        ctx.translateBy(x: 0, y: CGFloat(h))
        ctx.scaleBy(x: 2, y: -2)
        guard cl_document_draw_fitted(doc.handle, 0, ctx, Double(w) / 2, Double(h) / 2, 16, 2,
                                      Int32(dark ? CL_STYLE_DARK : CL_STYLE_LIGHT)) else { image = nil; return }
        image = ctx.makeImage()
        if let image { Self.cache[key] = image }
    }
}
