// Export Lab Report: one PDF a student can hand in. A header with their name,
// the circuit and the date; each page of the circuit as a light, print-friendly
// picture (Export as Image's drawing); the truth table; a Karnaugh map and the
// simplest sum of products and product of sums for each light (2 to 4
// switches); and the oscilloscope's recording as a timing diagram. A sheet
// first lets them tick what goes in, and remembers the choices.

import AppKit
import SwiftUI
import UniformTypeIdentifiers

struct LabReportOptions {
    var circuit = true
    var truthTable = true
    var formulas = true
    var timing = true
    /// Oscilloscope signals the timing page leaves empty, for a student worksheet.
    var blankSignals: Set<String> = []
    var blackAndWhite = false
    /// The drawing on the circuit's pictures (starts as it's shown).
    var ink = false
    /// "My notes": the student's notes for the circuit, after the header.
    var notes = true
}

@MainActor
enum LabReport {
    private static let pageW: CGFloat = 612, pageH: CGFloat = 792   // US Letter, portrait
    private static let margin: CGFloat = 54
    private static var contentW: CGFloat { pageW - 2 * margin }
    private static let footerH: CGFloat = 36

    /// What's on the oscilloscope: nothing yet means no timing diagram.
    static func hasRecording(_ document: CoreDocument) -> Bool {
        cl_scope_signal_count(document.handle) > 0 && cl_scope_length(document.handle) > 1
    }

    static func pdf(_ document: CoreDocument, title: String, options: LabReportOptions) -> Data? {
        let data = NSMutableData()
        var box = CGRect(x: 0, y: 0, width: pageW, height: pageH)
        guard let consumer = CGDataConsumer(data: data as CFMutableData),
              let ctx = CGContext(consumer: consumer, mediaBox: &box, nil) else { return nil }
        var builder = Builder(document: document, ctx: ctx, title: title, options: options)
        builder.build()
        ctx.closePDF()
        return data as Data
    }

    @MainActor struct Builder {
        let document: CoreDocument
        let ctx: CGContext
        let title: String
        let options: LabReportOptions
        var y: CGFloat = 0          // from the top of the page
        var pageNumber = 0
        let name: String = Prefs.shared.studentName.trimmingCharacters(in: .whitespaces)
        let color: Bool

        init(document: CoreDocument, ctx: CGContext, title: String, options: LabReportOptions) {
            self.document = document; self.ctx = ctx; self.title = title; self.options = options
            color = !options.blackAndWhite
        }

        // MARK: Pages and text

        mutating func newPage() {
            if pageNumber > 0 { footer(); ctx.endPDFPage() }
            ctx.beginPDFPage(nil)
            pageNumber += 1
            ctx.setFillColor(CGColor(gray: 1, alpha: 1))
            ctx.fill(CGRect(x: 0, y: 0, width: pageW, height: pageH))
            y = margin
        }

        /// Headings wait here until the block under them is placed, so a
        /// heading is never left alone at the bottom of a page.
        var pending: [(String, Bool)] = []   // (text, isSection)
        static let sectionH: CGFloat = 42, subH: CGFloat = 24
        var pendingH: CGFloat { pending.reduce(0) { $0 + ($1.1 ? Builder.sectionH : Builder.subH) } }

        /// Starts a new page unless `h` more points fit (with any waiting
        /// headings), then draws the waiting headings.
        mutating func need(_ h: CGFloat) {
            if y + pendingH + h > pageH - margin - footerH { newPage() }
            let queued = pending
            pending = []
            for (s, section) in queued { section ? drawHeading(s) : drawSubheading(s) }
        }

        private func footer() {
            let line = [name, title].filter { !$0.isEmpty }.joined(separator: " · ")
            text(line, x: margin, top: pageH - margin + 8, size: 9, color: .gray)
            text("Page \(pageNumber)", x: pageW - margin, top: pageH - margin + 8, size: 9, color: .gray, align: .right)
        }

        @discardableResult
        func text(_ s: String, x: CGFloat, top: CGFloat, size: CGFloat, weight: NSFont.Weight = .regular,
                  color: NSColor = .black, align: NSTextAlignment = .left, mono: Bool = false) -> CGFloat {
            let font = mono ? NSFont.monospacedSystemFont(ofSize: size, weight: weight) : NSFont.systemFont(ofSize: size, weight: weight)
            let str = NSAttributedString(string: s, attributes: [.font: font, .foregroundColor: color])
            let sz = str.size()
            let px = align == .right ? x - sz.width : align == .center ? x - sz.width / 2 : x
            NSGraphicsContext.saveGraphicsState()
            NSGraphicsContext.current = NSGraphicsContext(cgContext: ctx, flipped: false)
            str.draw(at: CGPoint(x: px, y: pageH - top - sz.height))
            NSGraphicsContext.restoreGraphicsState()
            return sz.height
        }

        /// A paragraph that wraps; moves down past it.
        /// `keep`: how much of the block under it must fit on the same page too.
        mutating func paragraph(_ s: String, size: CGFloat = 11, color: NSColor = .darkGray, gapAfter: CGFloat = 10, keep: CGFloat = 0) {
            let para = NSMutableParagraphStyle(); para.lineSpacing = 2
            let str = NSAttributedString(string: s, attributes: [.font: NSFont.systemFont(ofSize: size), .foregroundColor: color, .paragraphStyle: para])
            let h = ceil(str.boundingRect(with: CGSize(width: contentW, height: 10000), options: [.usesLineFragmentOrigin]).height)
            need(h + keep)
            NSGraphicsContext.saveGraphicsState()
            NSGraphicsContext.current = NSGraphicsContext(cgContext: ctx, flipped: false)
            str.draw(with: CGRect(x: margin, y: pageH - y - h, width: contentW, height: h), options: [.usesLineFragmentOrigin])
            NSGraphicsContext.restoreGraphicsState()
            y += h + gapAfter
        }

        mutating func heading(_ s: String) { pending.append((s, true)) }
        mutating func subheading(_ s: String) { pending.append((s, false)) }

        private mutating func drawHeading(_ s: String) {
            y += 8
            let h = text(s, x: margin, top: y, size: 15, weight: .semibold)
            y += h + 4
            ctx.setStrokeColor(CGColor(gray: 0.75, alpha: 1)); ctx.setLineWidth(0.5)
            ctx.move(to: CGPoint(x: margin, y: pageH - y)); ctx.addLine(to: CGPoint(x: pageW - margin, y: pageH - y)); ctx.strokePath()
            y += 10
        }

        private mutating func drawSubheading(_ s: String) {
            let h = text(s, x: margin, top: y, size: 12, weight: .medium, color: NSColor(white: 0.25, alpha: 1))
            y += h + 8
        }

        // MARK: The report

        mutating func build() {
            newPage()
            header()
            if options.notes { myNotes() }
            let pages = (0..<document.pageCount).filter { cl_document_gate_count(document.handle, Int32($0)) > 0 }
            if pages.isEmpty { paragraph("This circuit is empty, so there is nothing to show.") }
            if options.circuit {
                if !pages.isEmpty { heading("Circuit") }
                for p in pages { circuitPicture(p, of: pages.count) }
            }
            if options.truthTable || options.formulas { analysis(pages) }
            if options.timing { timing() }
            footer()
            ctx.endPDFPage()
        }

        mutating func header() {
            let h = text(title.isEmpty ? "Circuit" : title, x: margin, top: y, size: 26, weight: .semibold)
            y += h + 2
            y += text("Lab report", x: margin, top: y, size: 13, color: .darkGray) + 14
            ctx.setStrokeColor(CGColor(gray: 0, alpha: 1)); ctx.setLineWidth(1)
            ctx.move(to: CGPoint(x: margin, y: pageH - y)); ctx.addLine(to: CGPoint(x: pageW - margin, y: pageH - y)); ctx.strokePath()
            y += 12
            let label = "Name:"
            let lw = (label as NSString).size(withAttributes: [.font: NSFont.systemFont(ofSize: 13)]).width
            text(label, x: margin, top: y, size: 13, color: .darkGray)
            if name.isEmpty {
                ctx.setStrokeColor(CGColor(gray: 0.4, alpha: 1)); ctx.setLineWidth(0.6)
                ctx.move(to: CGPoint(x: margin + lw + 8, y: pageH - y - 14)); ctx.addLine(to: CGPoint(x: margin + lw + 190, y: pageH - y - 14)); ctx.strokePath()
            } else {
                text(name, x: margin + lw + 8, top: y, size: 13)
            }
            let date = DateFormatter.localizedString(from: Date(), dateStyle: .long, timeStyle: .none)
            text("Date: " + date, x: pageW - margin, top: y, size: 13, color: .darkGray, align: .right)
            y += 30
        }

        // MARK: My notes

        /// The notes as written, a paragraph at a time (so they flow across
        /// pages), long paragraphs in pieces.
        mutating func myNotes() {
            let notes = document.notes
            guard notes.contains(where: { $0 != " " && $0 != "\t" && $0 != "\n" }) else { return }
            heading("My notes")
            for para in notes.components(separatedBy: "\n") {
                if para.trimmingCharacters(in: .whitespaces).isEmpty { y += 6; continue }
                var rest = Substring(para)
                while !rest.isEmpty {
                    var piece = rest.prefix(1200)
                    if piece.count < rest.count, let space = piece.lastIndex(of: " ") { piece = rest[rest.startIndex..<space] }
                    paragraph(String(piece), size: 11, color: .black, gapAfter: 4)
                    rest = rest[piece.endIndex...].drop(while: { $0 == " " })
                }
            }
            y += 8
        }

        // MARK: Circuit pictures

        mutating func circuitPicture(_ page: Int, of count: Int) {
            guard let size = ImageExport.size(document, page: page, info: nil, ink: options.ink) else { return }
            if count > 1 { subheading("Page \(page + 1): " + document.pageName(page)) }
            let room = pageH - margin - footerH - 2 * margin - pendingH   // what a fresh page gives
            let s = min(contentW / size.width, room / size.height, 1.5)
            let w = size.width * s, h = size.height * s
            need(h)
            let x = margin + (contentW - w) / 2
            ctx.saveGState()
            ctx.translateBy(x: x, y: pageH - y - h)
            ctx.clip(to: CGRect(x: 0, y: 0, width: w, height: h))
            ctx.scaleBy(x: s, y: s)
            ImageExport.draw(document, page: page, in: ctx, size: size, scale: s, blackAndWhite: options.blackAndWhite, grid: false,
                             info: nil, ink: options.ink)
            ctx.restoreGState()
            ctx.setStrokeColor(CGColor(gray: 0.82, alpha: 1)); ctx.setLineWidth(0.5)
            ctx.stroke(CGRect(x: x, y: pageH - y - h, width: w, height: h))
            y += h + 16
        }

        // MARK: Truth tables, maps and formulas

        mutating func analysis(_ pages: [Int]) {
            var tables: [(Int, TruthTable?, String)] = []
            for p in pages {
                var error = ""
                let t = TruthTable(document: document, page: p, error: &error)
                tables.append((p, t, error))
            }
            if options.truthTable {
                heading("Truth table")
                for (p, t, error) in tables {
                    if pages.count > 1 { subheading("Page \(p + 1): " + document.pageName(p)) }
                    if let t { truthTable(t) } else { paragraph("No truth table for this page. " + error) }
                }
            }
            if options.formulas {
                heading("Karnaugh maps and formulas")
                for (p, t, error) in tables {
                    if pages.count > 1 { subheading("Page \(p + 1): " + document.pageName(p)) }
                    if let t { maps(t) } else { paragraph("No Karnaugh maps for this page. " + error) }
                }
            }
        }

        mutating func truthTable(_ t: TruthTable) {
            if t.sequential {
                paragraph("This circuit has clocks or flip-flops, so a light can depend on what happened before. Each row is read after the circuit settles from the row above it.", size: 10, keep: 100)
            }
            if t.unsettled > 0 {
                paragraph("\(t.unsettled) row\(t.unsettled == 1 ? "" : "s") never stopped changing (a clock or an oscillation), so those lights are a snapshot.", size: 10, keep: 100)
            }
            let cols = t.names.count
            let cw = min(64, contentW / CGFloat(max(cols, 1)))
            let tableW = cw * CGFloat(cols)
            let x0 = margin + (contentW - tableW) / 2
            let rowH: CGFloat = 16
            func headerRow() {
                ctx.setFillColor(CGColor(gray: 0.92, alpha: 1))
                ctx.fill(CGRect(x: x0, y: pageH - y - rowH, width: tableW, height: rowH))
                for c in 0..<cols {
                    ctx.saveGState()
                    ctx.clip(to: CGRect(x: x0 + CGFloat(c) * cw, y: pageH - y - rowH, width: cw, height: rowH))
                    text(t.names[c], x: x0 + (CGFloat(c) + 0.5) * cw, top: y + 2, size: 10, weight: .semibold, align: .center)
                    ctx.restoreGState()
                }
                y += rowH
            }
            need(rowH * 4)
            headerRow()
            var rowTop = y
            func rule() {
                // The line between switches and lights, and a hairline under the last row.
                let bx = x0 + CGFloat(t.inputs) * cw
                ctx.setStrokeColor(CGColor(gray: 0, alpha: 1)); ctx.setLineWidth(1)
                ctx.move(to: CGPoint(x: bx, y: pageH - rowTop + rowH)); ctx.addLine(to: CGPoint(x: bx, y: pageH - y)); ctx.strokePath()
                ctx.setStrokeColor(CGColor(gray: 0.6, alpha: 1)); ctx.setLineWidth(0.5)
                ctx.move(to: CGPoint(x: x0, y: pageH - y)); ctx.addLine(to: CGPoint(x: x0 + tableW, y: pageH - y)); ctx.strokePath()
            }
            for (r, row) in t.rows.enumerated() {
                if y + rowH > pageH - margin - footerH {
                    rule(); newPage(); headerRow(); rowTop = y
                }
                if r % 2 == 1 {
                    ctx.setFillColor(CGColor(gray: 0.965, alpha: 1))
                    ctx.fill(CGRect(x: x0, y: pageH - y - rowH, width: tableW, height: rowH))
                }
                for (c, v) in row.enumerated() {
                    let ink: NSColor
                    switch v {
                    case "1": ink = color ? NSColor(srgbRed: 0.05, green: 0.5, blue: 0.2, alpha: 1) : .black
                    case "X": ink = color ? NSColor(srgbRed: 0.8, green: 0.45, blue: 0, alpha: 1) : .darkGray
                    case "Z": ink = color ? NSColor(srgbRed: 0.1, green: 0.3, blue: 0.85, alpha: 1) : .darkGray
                    case "!": ink = color ? .red : .black
                    default: ink = NSColor(white: 0.35, alpha: 1)
                    }
                    text(String(v), x: x0 + (CGFloat(c) + 0.5) * cw, top: y + 2, size: 10, weight: v == "1" ? .bold : .regular, color: ink, align: .center, mono: true)
                }
                y += rowH
            }
            rule()
            var legend = "1 = on, 0 = off"
            let all = Set(t.rows.flatMap { $0 })
            if all.contains("X") { legend += ", X = unknown" }
            if all.contains("Z") { legend += ", Z = floating" }
            if all.contains("!") { legend += ", ! = conflict" }
            if all.contains("-") { legend += ", - = not connected" }
            y += 6
            text(legend, x: margin, top: y, size: 9, color: .gray)
            y += 22
        }

        /// A SwiftUI view drawn into the page as vector art, `width` wide.
        mutating func place<V: View>(_ view: V, width: CGFloat, gapAfter: CGFloat = 14) {
            let content = view
                .environment(\.colorScheme, .light)
                .saturation(options.blackAndWhite ? 0 : 1)
            let r = ImageRenderer(content: content)
            r.proposedSize = ProposedViewSize(width: width, height: nil)
            var size = CGSize.zero
            r.render { s, _ in size = s }
            need(size.height)
            let top = y
            let ctx = self.ctx
            r.render(rasterizationScale: 3) { s, draw in
                ctx.saveGState()
                ctx.translateBy(x: margin, y: pageH - top - s.height)
                draw(ctx)
                ctx.restoreGState()
            }
            y += size.height + gapAfter
        }

        mutating func maps(_ t: TruthTable) {
            let n = t.inputs
            guard let layout = KMapLayout(n: n) else {
                paragraph(n > 4 ? "Karnaugh maps are drawn for 2 to 4 switches; this circuit has \(n)." : "A Karnaugh map needs at least 2 switches.")
                return
            }
            let names = Array(t.names.prefix(n))
            let colW = (contentW - 24) / 2
            var anyUnknown = false
            for k in 0..<t.outputCount {
                let values = t.values(output: k)
                if values.contains(where: { $0 == nil }) { anyUnknown = true }
                let output = t.names[n + k]
                let sop = TwoLevel.simplest(.sumOfProducts, n: n, values: values)
                let pos = TwoLevel.simplest(.productOfSums, n: n, values: values)
                place(HStack(alignment: .top, spacing: 24) {
                    ReportMapCard(title: "Sum of products", layout: layout, names: names, values: values, form: sop, ones: true, output: output)
                        .frame(width: colW, alignment: .leading)
                    ReportMapCard(title: "Product of sums", layout: layout, names: names, values: values, form: pos, ones: false, output: output)
                        .frame(width: colW, alignment: .leading)
                }, width: contentW)
            }
            if anyUnknown {
                paragraph("X means the light was not clearly on or off for that row, so the formulas treat it as either (a don't-care).", size: 9.5)
            }
        }

        // MARK: Timing diagram

        mutating func timing() {
            heading("Timing diagram")
            guard LabReport.hasRecording(document) else {
                paragraph("The oscilloscope has no recording. Run the circuit with TO labels on the wires you want to see, then export again.")
                return
            }
            let count = Int(cl_scope_signal_count(document.handle))
            let signals = (0..<count).map { TimingDiagram.Signal(index: $0, name: String(cString: cl_scope_signal(document.handle, Int32($0)))) }
                .map { var s = $0; s.blank = options.blankSignals.contains(s.name); return s }
            let length = Int(cl_scope_length(document.handle))
            let perChunk = 28, maxChunks = 6
            var start = 0
            if length > perChunk * maxChunks {
                start = length - perChunk * maxChunks
                paragraph("The recording is \(length) steps long; this shows the last \(perChunk * maxChunks).", size: 10)
            }
            // One clock for the guide lines on every chunk, worked out over all of them.
            let guide = signals.contains(where: \.blank) ? TimingDiagram.guide(document, signals: signals, range: start..<length) : nil
            if signals.contains(where: \.blank) {
                paragraph(guide.map { "Fill in the empty rows. The dark lines mark each time \($0.name) changes." } ?? "Fill in the empty rows.", size: 10)
            }
            var from = start
            while from < length {
                let range = from..<min(length, from + perChunk)
                from = range.upperBound
                let (size, _) = TimingDiagram.size(steps: range.count, signals: signals.count, titled: false)
                let full = TimingDiagram.size(steps: perChunk, signals: signals.count, titled: false).size
                let s = min(contentW / full.width, 1)   // every chunk at one scale
                let h = size.height * s
                need(h)
                ctx.saveGState()
                ctx.translateBy(x: margin, y: pageH - y - h)
                ctx.scaleBy(x: s, y: s)
                TimingDiagram.draw(ctx, document: document, signals: signals, range: range, title: title, color: color,
                                   guide: guide, titled: false)
                ctx.restoreGState()
                y += h + 6
            }
        }
    }
}

/// One Karnaugh map with its formula, for the lab report (white paper).
private struct ReportMapCard: View {
    let title: String
    let layout: KMapLayout
    let names: [String]
    let values: [Bool?]
    let form: TwoLevel
    let ones: Bool
    let output: String

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(output + ": " + title.lowercased()).font(.system(size: 10.5, weight: .semibold)).foregroundStyle(.secondary)
            KMapView(layout: layout, names: names, values: values, groups: form.implicants, groupingOnes: ones)
            FormulaText(name: output, tokens: form.tokens(names: names), size: 13)
        }
        .foregroundStyle(.black)
    }
}

// MARK: The sheet

struct ExportReportView: View {
    let document: CoreDocument
    let fileName: String
    @ObservedObject private var prefs = Prefs.shared
    @Environment(\.dismiss) private var dismiss
    @AppStorage("cl.reportCircuit") private var circuit = true
    @AppStorage("cl.reportTable") private var truthTable = true
    @AppStorage("cl.reportFormulas") private var formulas = true
    @AppStorage("cl.reportTiming") private var timing = true
    @AppStorage("cl.reportBW") private var blackAndWhite = false
    /// "Include my notes": on unless turned off (remembered).
    @AppStorage("cl.reportNotes") private var includeNotes = true
    /// The drawing on the pictures: starts as the circuit shows it.
    @State private var includeInk: Bool
    init(document: CoreDocument, fileName: String) {
        self.document = document
        self.fileName = fileName
        _includeInk = State(initialValue: document.inkShown)
    }
    private var hasNotes: Bool { document.notes.contains { $0 != " " && $0 != "\t" && $0 != "\n" } }
    /// Signals the timing page leaves empty for students (not remembered: it depends on the circuit).
    @State private var blankSignals: Set<String> = []

    private var recorded: Bool { LabReport.hasRecording(document) }
    private var signalNames: [String] {
        (0..<Int(cl_scope_signal_count(document.handle))).map { String(cString: cl_scope_signal(document.handle, Int32($0))) }
    }
    private var anything: Bool { circuit || truthTable || formulas || (timing && recorded) || (hasNotes && includeNotes) }

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Export Lab Report").font(.title2.weight(.semibold))
            Text("One PDF to hand in: your name, the circuit, and the parts of your analysis you pick.")
                .foregroundStyle(.secondary)
            HStack {
                Text("Your name:")
                TextField("First and last name", text: $prefs.studentName)
            }
            GroupBox("Include") {
                VStack(alignment: .leading, spacing: 8) {
                    if hasNotes { Toggle("Include my notes", isOn: $includeNotes) }
                    Toggle("The circuit (a picture of each page)", isOn: $circuit)
                    if document.hasInk {
                        Toggle("Include drawing", isOn: $includeInk)
                            .disabled(!circuit)
                            .padding(.leading, 20)
                    }
                    Toggle("Truth table", isOn: $truthTable)
                    Toggle("Karnaugh maps and simplest formulas (2 to 4 switches)", isOn: $formulas)
                    Toggle("Timing diagram from the oscilloscope", isOn: $timing).disabled(!recorded)
                    if recorded && timing {
                        VStack(alignment: .leading, spacing: 4) {
                            Text("Leave blank for students:").font(.callout)
                            ScrollView {
                                VStack(alignment: .leading, spacing: 4) {
                                    ForEach(signalNames, id: \.self) { name in
                                        Toggle(name, isOn: Binding(
                                            get: { blankSignals.contains(name) },
                                            set: { on in if on { blankSignals.insert(name) } else { blankSignals.remove(name) } }))
                                        .accessibilityLabel("Leave \(name) blank")
                                    }
                                }
                                .frame(maxWidth: .infinity, alignment: .leading)
                            }
                            .frame(height: min(110, CGFloat(signalNames.count) * 22))
                            Text("Those rows are drawn empty, with the time grid and a dark line each time the clock changes, to fill in on paper. The clock is a signal named CLK, CLOCK or CP, or one that ticks evenly, that isn't left blank. Leave them all unticked for the answer key.")
                                .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
                        }
                        .padding(.leading, 20)
                    }
                    if !recorded {
                        Text("The oscilloscope has no recording yet. Run the circuit with TO labels on its wires to make one.")
                            .font(.caption).foregroundStyle(.secondary)
                    }
                }
                .padding(4)
                .frame(maxWidth: .infinity, alignment: .leading)
            }
            GroupBox("Output style") {
                Picker("", selection: $blackAndWhite) {
                    Text("Color").tag(false)
                    Text("Black & White").tag(true)
                }
                .pickerStyle(.radioGroup).labelsHidden().padding(4)
                .frame(maxWidth: .infinity, alignment: .leading)
            }
            HStack {
                Spacer()
                Button("Cancel") { dismiss() }.keyboardShortcut(.cancelAction)
                Button("Export to File…") { save() }.keyboardShortcut(.defaultAction).disabled(!anything)
            }
        }
        .padding(20)
        .frame(width: 520)
    }

    private func save() {
        let panel = NSSavePanel()
        panel.nameFieldStringValue = fileName + " lab report.pdf"
        panel.allowedContentTypes = [.pdf]
        panel.isExtensionHidden = false
        panel.canCreateDirectories = true
        guard panel.runModal() == .OK, let url = panel.url else { return }
        let options = LabReportOptions(circuit: circuit, truthTable: truthTable, formulas: formulas,
                                       timing: timing && recorded, blankSignals: blankSignals.intersection(signalNames),
                                       blackAndWhite: blackAndWhite, ink: includeInk && document.hasInk, notes: includeNotes)
        do {
            guard let data = LabReport.pdf(document, title: fileName, options: options) else { throw CocoaError(.fileWriteUnknown) }
            try data.write(to: url, options: .atomic)
            dismiss()
        } catch { NSAlert(error: error).runModal() }
    }
}

// MARK: Without the app's window (for checks)

extension LabReport {
    /// `CedarLogic --lab-report in.cdl out.pdf [bw] [blank=A,B]`: runs the circuit a while
    /// (so the oscilloscope has a recording) and writes the report.
    static func runIfAsked() {
        let args = CommandLine.arguments
        guard let i = args.firstIndex(of: "--lab-report"), i + 2 < args.count else { return }
        guard let data = try? Data(contentsOf: URL(fileURLWithPath: args[i + 1])), let doc = try? CoreDocument(data: data) else {
            print("could not open", args[i + 1]); exit(1)
        }
        for _ in 0..<120 { cl_document_step(doc.handle) }
        var options = LabReportOptions()
        options.blackAndWhite = args.contains("bw")
        options.ink = doc.inkShown && doc.hasInk
        options.notes = !args.contains("nonotes")
        // blank=A,B leaves those oscilloscope signals empty (a worksheet).
        if let b = args.first(where: { $0.hasPrefix("blank=") }) {
            options.blankSignals = Set(b.dropFirst(6).split(separator: ",").map(String.init))
        }
        if Prefs.shared.studentName.isEmpty { Prefs.shared.studentName = "Alex Student" }
        let title = URL(fileURLWithPath: args[i + 1]).deletingPathExtension().lastPathComponent
        guard let pdf = pdf(doc, title: title, options: options) else { print("no pdf"); exit(1) }
        try? pdf.write(to: URL(fileURLWithPath: args[i + 2]))
        exit(0)
    }
}
