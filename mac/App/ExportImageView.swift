// Export as Image (the wx app's): a preview, the grid if wanted, and a strip
// under the circuit with your name and whether it works -- what a grader
// looks for first -- in colour or black and white, at 2x, 4x or 6x. Save as
// PNG or PDF, or copy it. Its look: as the window shows the circuit (a dark
// theme's dark canvas too), light, or black and white.

import AppKit
import SwiftUI

struct ExportInfo {
    var enabled: Bool
    var name: String
    var works: Bool
    var why: String
    var fileName: String
}

@MainActor
enum ImageExport {
    static let pointsPerUnit: CGFloat = 12
    static let margin: CGFloat = 24

    /// The strip's lines and height for a width (PNG and PDF share this).
    static func strip(_ info: ExportInfo, width: CGFloat) -> (lines: [String], height: CGFloat) {
        let pad: CGFloat = 22, body: CGFloat = 17, gap: CGFloat = 8
        let statement = info.works ? "My circuit works properly."
            : "My circuit does not work because " + info.why.trimmingCharacters(in: .whitespacesAndNewlines)
        let font = NSFont.systemFont(ofSize: body)
        var lines: [String] = [], line = ""
        for word in statement.split(separator: " ") {
            let trial = line.isEmpty ? String(word) : line + " " + word
            if !line.isEmpty && (trial as NSString).size(withAttributes: [.font: font]).width > width - 2 * pad {
                lines.append(line); line = String(word)
            } else { line = trial }
        }
        if !line.isEmpty { lines.append(line) }
        let h = pad + body + gap * 1.6 + CGFloat(lines.count) * (body + gap) + pad * 0.5
        return (lines, h)
    }

    /// What the picture holds: the circuit, and the drawing when it's included.
    nonisolated static func box(_ document: CoreDocument, page: Int, ink: Bool) -> CGRect? {
        let circuit = document.bounds(ofPage: page)
        guard ink, let drawing = document.inkBounds(ofPage: page) else { return circuit }
        return circuit.map { $0.union(drawing) } ?? drawing
    }

    /// Size in points of the whole image.
    static func size(_ document: CoreDocument, page: Int, info: ExportInfo?, ink: Bool = false) -> CGSize? {
        guard let box = box(document, page: page, ink: ink) else { return nil }
        let w = max(box.width * pointsPerUnit + 2 * margin, info?.enabled == true ? 420 : 0)
        var h = box.height * pointsPerUnit + 2 * margin
        if let info, info.enabled { h += strip(info, width: w).height }
        return CGSize(width: w, height: h)
    }

    /// Draws into a y-up context `size` points big.
    /// `screen`: the window's theme, to draw the circuit exactly as it shows
    /// (its canvas, dark or light ink, its grid colour); nil, light on white.
    static func draw(_ document: CoreDocument, page: Int, in ctx: CGContext, size: CGSize, scale: CGFloat,
                     blackAndWhite: Bool, grid: Bool, info: ExportInfo?, ink: Bool = false, screen: Theme? = nil) {
        let prefs = Prefs.shared
        let stripH = (info?.enabled == true) ? strip(info!, width: size.width).height : 0
        let circuitH = size.height - stripH
        let theme = blackAndWhite ? nil : screen
        ctx.setFillColor(CGColor(gray: 1, alpha: 1))
        ctx.fill(CGRect(origin: .zero, size: size))
        if let theme {
            ctx.setFillColor(theme.canvas.cgColor)
            ctx.fill(CGRect(x: 0, y: stripH, width: size.width, height: circuitH))
        }
        // The circuit, fitted into the top part; y down inside.
        ctx.saveGState()
        ctx.translateBy(x: 0, y: size.height)
        ctx.scaleBy(x: 1, y: -1)
        if grid, let box = box(document, page: page, ink: ink) {
            let upp = 1 / pointsPerUnit
            let ox = box.midX - size.width / 2 * upp, oy = box.midY + circuitH / 2 * upp
            ctx.setStrokeColor(theme?.gridMajor.cgColor ?? CGColor(srgbRed: 0, green: 0, blue: 0.2, alpha: 0.2))
            ctx.setLineWidth(0.5)
            var x = ceil(ox)
            while x < ox + size.width * upp { let sx = (x - ox) / upp; ctx.move(to: CGPoint(x: sx, y: 0)); ctx.addLine(to: CGPoint(x: sx, y: circuitH)); x += 1 }
            var y = floor(oy)
            while y > oy - circuitH * upp { let sy = (oy - y) / upp; ctx.move(to: CGPoint(x: 0, y: sy)); ctx.addLine(to: CGPoint(x: size.width, y: sy)); y -= 1 }
            ctx.strokePath()
        }
        if blackAndWhite {
            // The drawing in black and white too (DRAWING-NOTES 4.8).
            _ = cl_document_draw_fitted_ink(document.handle, Int32(page), ctx, size.width, circuitH, margin, scale,
                                            Int32(CL_STYLE_PRINT), ink)
        } else if let box = box(document, page: page, ink: ink) {
            let upp = max(box.width / (size.width - 2 * margin), box.height / (circuitH - 2 * margin))
            var o = CLDrawOptions(dark: theme?.darkCircuit ?? false, accent: Int32(prefs.accent), wireScale: 1, simView: false,
                                  thumbnail: false, showSelection: false, selectionFade: 1,
                                  ink: Int32(ink ? CL_INK_ALWAYS_PRINT : CL_INK_NEVER))
            cl_document_draw_ex(document.handle, Int32(page), ctx, scale, box.midX - size.width / 2 * upp,
                                box.midY + circuitH / 2 * upp, upp, &o)
        }
        ctx.restoreGState()

        // The name-and-result strip, black on white so it prints.
        guard let info, info.enabled else { return }
        let lines = strip(info, width: size.width).lines
        let pad: CGFloat = 22, body: CGFloat = 17, small: CGFloat = 11, gap: CGFloat = 8
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(cgContext: ctx, flipped: false)
        let ink = NSColor.black, gray = NSColor(white: 0.4, alpha: 1)
        func text(_ s: String, _ x: CGFloat, _ topY: CGFloat, _ px: CGFloat, _ c: NSColor) {
            let font = NSFont.systemFont(ofSize: px)
            (s as NSString).draw(at: CGPoint(x: x, y: stripH - topY - px), withAttributes: [.font: font, .foregroundColor: c])
        }
        ctx.setStrokeColor(ink.cgColor)
        ctx.setLineWidth(1)
        ctx.move(to: CGPoint(x: pad, y: stripH - 0.5)); ctx.addLine(to: CGPoint(x: size.width - pad, y: stripH - 0.5))
        ctx.strokePath()
        var y = pad
        let label = "Name:"
        text(label, pad, y, body, gray)
        let name = info.name.trimmingCharacters(in: .whitespaces)
        let lw = (label as NSString).size(withAttributes: [.font: NSFont.systemFont(ofSize: body)]).width
        text(name.isEmpty ? "________________" : name, pad + lw + 8, y, body, ink)
        let date = DateFormatter.localizedString(from: Date(), dateStyle: .medium, timeStyle: .none)
        let meta = info.fileName.isEmpty ? date : info.fileName + "   " + date
        let mw = (meta as NSString).size(withAttributes: [.font: NSFont.systemFont(ofSize: small)]).width
        text(meta, size.width - pad - mw, y + body - small, small, gray)
        y += body + gap * 1.6
        for l in lines { text(l, pad, y, body, ink); y += body + gap }
        NSGraphicsContext.restoreGraphicsState()
    }

    static func image(_ document: CoreDocument, page: Int, multiplier: CGFloat, blackAndWhite: Bool, grid: Bool, info: ExportInfo?,
                      ink: Bool = false, screen: Theme? = nil) -> CGImage? {
        guard var size = size(document, page: page, info: info, ink: ink) else { return nil }
        size = CGSize(width: size.width.rounded(.up), height: size.height.rounded(.up))
        var scale = multiplier
        if max(size.width, size.height) * scale > 12000 { scale = 12000 / max(size.width, size.height) }
        guard let ctx = CGContext(data: nil, width: Int(size.width * scale), height: Int(size.height * scale),
                                  bitsPerComponent: 8, bytesPerRow: 0, space: CGColorSpace(name: CGColorSpace.sRGB)!,
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        ctx.scaleBy(x: scale, y: scale)
        draw(document, page: page, in: ctx, size: size, scale: scale, blackAndWhite: blackAndWhite, grid: grid, info: info, ink: ink, screen: screen)
        return ctx.makeImage()
    }

    static func pdf(_ document: CoreDocument, page: Int, blackAndWhite: Bool, grid: Bool, info: ExportInfo?, ink: Bool = false,
                    screen: Theme? = nil) -> Data? {
        guard let size = size(document, page: page, info: info, ink: ink) else { return nil }
        let data = NSMutableData()
        var box = CGRect(origin: .zero, size: size)
        guard let consumer = CGDataConsumer(data: data as CFMutableData),
              let ctx = CGContext(consumer: consumer, mediaBox: &box, nil) else { return nil }
        ctx.beginPDFPage(nil)
        draw(document, page: page, in: ctx, size: size, scale: 1, blackAndWhite: blackAndWhite, grid: grid, info: info, ink: ink, screen: screen)
        ctx.endPDFPage()
        ctx.closePDF()
        return data as Data
    }
}

/// The picture's look: as the window shows it, light, or black and white.
enum ExportLook: Int, CaseIterable, Identifiable {
    case screen, light, blackAndWhite
    var id: Int { rawValue }
    var name: String {
        switch self {
        case .screen: "Match Window"
        case .light: "Light"
        case .blackAndWhite: "Black & White"
        }
    }
}

/// Preview first, then the actions (Copy is the default: Return copies),
/// then the look and the size; the name-and-result strip only when it's on.
/// Every choice is kept for next time.
struct ExportImageView: View {
    let document: CoreDocument
    let page: Int
    let fileName: String
    @ObservedObject private var prefs = Prefs.shared
    @ObservedObject private var look = LookStore.shared
    @Environment(\.dismiss) private var dismiss
    @AppStorage("cl.exportGrid") private var grid = false
    /// (Was cl.exportBW, colour or black and white; its answer is the first
    /// default here.)
    @AppStorage("cl.exportLook") private var lookRaw = UserDefaults.standard.bool(forKey: "cl.exportBW")
        ? ExportLook.blackAndWhite.rawValue : ExportLook.screen.rawValue
    @AppStorage("cl.exportRes") private var multiplier = 4
    @AppStorage("cl.exportWorks") private var works = true
    @AppStorage("cl.exportWhy") private var why = ""
    /// Include drawing: offered when the page has one, starting as the
    /// circuit's show/hide (DRAWING-NOTES 4.10).
    @State private var includeInk: Bool
    @State private var copied = false
    private let pageHasInk: Bool

    init(document: CoreDocument, page: Int, fileName: String) {
        self.document = document
        self.page = page
        self.fileName = fileName
        pageHasInk = document.inkStrokeCount(page: page) > 0
        _includeInk = State(initialValue: document.inkShown)
    }
    private var ink: Bool { pageHasInk && includeInk }
    private var style: ExportLook { ExportLook(rawValue: lookRaw) ?? .screen }
    private var blackAndWhite: Bool { style == .blackAndWhite }
    private var screen: Theme? { style == .screen ? look.settings.theme : nil }

    private var info: ExportInfo {
        ExportInfo(enabled: prefs.exportInfo, name: prefs.studentName, works: works, why: why, fileName: fileName)
    }
    /// A "does not work" answer needs its reason before anything goes out.
    private var ready: Bool { !prefs.exportInfo || works || !why.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty }

    private func picture(_ multiplier: CGFloat) -> CGImage? {
        ImageExport.image(document, page: page, multiplier: multiplier, blackAndWhite: blackAndWhite, grid: grid, info: info,
                          ink: ink, screen: screen)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Export as Image").font(.title3.weight(.semibold))
            preview
                .frame(maxWidth: .infinity)
                .frame(height: 230)
                .background(RoundedRectangle(cornerRadius: 10).fill(Color.secondary.opacity(0.08)))
            HStack(spacing: 8) {
                Button { copy() } label: {
                    Label(copied ? "Copied" : "Copy Image", systemImage: copied ? "checkmark" : "doc.on.doc").frame(maxWidth: .infinity)
                }
                .keyboardShortcut(.defaultAction).disabled(!ready)
                Button { save() } label: { Label("Save…", systemImage: "square.and.arrow.down").frame(maxWidth: .infinity) }
                    .disabled(!ready)
                Button { share() } label: { Label("Share…", systemImage: "square.and.arrow.up").frame(maxWidth: .infinity) }
                .disabled(!ready)
            }
            .controlSize(.large)
            Grid(alignment: .leading, horizontalSpacing: 12, verticalSpacing: 10) {
                GridRow {
                    Text("Look").foregroundStyle(.secondary)
                    Picker("Look", selection: $lookRaw) {
                        ForEach(ExportLook.allCases) { Text($0.name).tag($0.rawValue) }
                    }
                    .pickerStyle(.segmented).labelsHidden()
                }
                GridRow {
                    Text("Size").foregroundStyle(.secondary)
                    Picker("Size", selection: $multiplier) {
                        Text("2×").tag(2)
                        Text("4×").tag(4)
                        Text("6×").tag(6)
                    }
                    .pickerStyle(.segmented).labelsHidden()
                    .help("PNG resolution: 2× for the screen, 4× to print, 6× for the sharpest")
                }
            }
            HStack(spacing: 18) {
                Toggle("Grid lines", isOn: $grid)
                if pageHasInk { Toggle("Include drawing", isOn: $includeInk) }
                Toggle("Name and result", isOn: $prefs.exportInfo)
            }
            if prefs.exportInfo {
                VStack(alignment: .leading, spacing: 8) {
                    TextField("Your name (first and last)", text: $prefs.studentName)
                    Picker("", selection: $works) {
                        Text("My circuit works properly").tag(true)
                        Text("My circuit does not work because…").tag(false)
                    }
                    .pickerStyle(.radioGroup)
                    .labelsHidden()
                    TextField("Explain what doesn't work (required)", text: $why, axis: .vertical)
                        .lineLimit(2...3)
                        .disabled(works)
                }
                .padding(10)
                .background(RoundedRectangle(cornerRadius: 8).fill(Color.secondary.opacity(0.08)))
            }
            HStack {
                if !ready { Text("Say what doesn't work to export it.").font(.callout).foregroundStyle(.red) }
                Spacer()
                Button("Done") { dismiss() }.keyboardShortcut(.cancelAction)
            }
        }
        .padding(20)
        .frame(width: 520)
    }

    private var preview: some View {
        Group {
            if let img = picture(2) {
                Image(decorative: img, scale: 2).resizable().aspectRatio(contentMode: .fit)
                    .clipShape(RoundedRectangle(cornerRadius: 3))
                    .shadow(color: .black.opacity(0.2), radius: 4, y: 1)
                    .padding(12)
            } else {
                Text("This page is empty.").foregroundStyle(.secondary)
            }
        }
    }

    /// The PNG at the chosen size, saying its pixels per inch (72 × the
    /// size), so Pages, Word or Keynote place it at its real size, sharp:
    /// not 4 times too big (with only white margin in sight).
    private func pngData() -> Data? {
        guard let img = picture(CGFloat(multiplier)) else { return nil }
        let rep = NSBitmapImageRep(cgImage: img)
        rep.size = NSSize(width: CGFloat(img.width) / CGFloat(multiplier), height: CGFloat(img.height) / CGFloat(multiplier))
        return rep.representation(using: .png, properties: [:])
    }

    private func copy() {
        guard let img = picture(CGFloat(multiplier)) else { return }
        let rep = NSBitmapImageRep(cgImage: img)
        rep.size = NSSize(width: CGFloat(img.width) / CGFloat(multiplier), height: CGFloat(img.height) / CGFloat(multiplier))
        let pb = NSPasteboard.general
        pb.clearContents()
        // An image object first (what every Mac app pastes), then the PNG
        // and TIFF bytes for the ones that ask for those.
        let image = NSImage(size: rep.size)
        image.addRepresentation(rep)
        pb.writeObjects([image])
        if let png = rep.representation(using: .png, properties: [:]) { pb.setData(png, forType: .png) }
        copied = true
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.6) { dismiss() }
    }

    /// Share…: the PNG, as a file, to the system's share menu (Messages,
    /// Mail, Notes, AirDrop), from under the window's middle.
    private func share() {
        guard let data = pngData(), let view = NSApp.keyWindow?.contentView else { return }
        let name = (fileName.isEmpty ? "Circuit" : fileName).replacingOccurrences(of: "/", with: "-")
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent("CedarLogic Pictures", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let url = dir.appendingPathComponent(name + ".png")
        guard (try? data.write(to: url, options: .atomic)) != nil else { return }
        let picker = NSSharingServicePicker(items: [url])
        let b = view.bounds
        picker.show(relativeTo: NSRect(x: b.midX - 1, y: b.midY, width: 2, height: 2), of: view, preferredEdge: .minY)
    }

    private func save() {
        let panel = NSSavePanel()
        panel.nameFieldStringValue = fileName
        panel.allowedContentTypes = [.png, .pdf]
        panel.isExtensionHidden = false
        let format = NSPopUpButton()
        format.addItems(withTitles: ["PNG Image", "PDF Document"])
        let accessory = NSStackView(views: [NSTextField(labelWithString: "Format:"), format])
        accessory.edgeInsets = NSEdgeInsets(top: 8, left: 8, bottom: 8, right: 8)
        panel.accessoryView = accessory
        let target = MenuAction { panel.allowedContentTypes = [format.indexOfSelectedItem == 1 ? .pdf : .png] }
        format.target = target
        format.action = #selector(MenuAction.run)
        panel.allowedContentTypes = [.png]
        guard panel.runModal() == .OK, let url = panel.url else { return }
        _ = target
        let data: Data?
        if url.pathExtension.lowercased() == "pdf" {
            data = ImageExport.pdf(document, page: page, blackAndWhite: blackAndWhite, grid: grid, info: info, ink: ink, screen: screen)
        } else {
            data = pngData()
        }
        do {
            guard let data else { throw CocoaError(.fileWriteUnknown) }
            try data.write(to: url, options: .atomic)
            dismiss()
        } catch { NSAlert(error: error).runModal() }
    }
}
