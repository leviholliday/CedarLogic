// Export as Image (the wx app's): a preview, the grid if wanted, and a strip
// under the circuit with your name and whether it works -- what a grader
// looks for first -- in colour or black and white, at 2x, 4x or 6x. Save as
// PNG or PDF, or copy it.

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

    /// Size in points of the whole image.
    static func size(_ document: CoreDocument, page: Int, info: ExportInfo?) -> CGSize? {
        guard let box = document.bounds(ofPage: page) else { return nil }
        let w = max(box.width * pointsPerUnit + 2 * margin, info?.enabled == true ? 420 : 0)
        var h = box.height * pointsPerUnit + 2 * margin
        if let info, info.enabled { h += strip(info, width: w).height }
        return CGSize(width: w, height: h)
    }

    /// Draws into a y-up context `size` points big.
    static func draw(_ document: CoreDocument, page: Int, in ctx: CGContext, size: CGSize, scale: CGFloat,
                     blackAndWhite: Bool, grid: Bool, info: ExportInfo?) {
        let prefs = Prefs.shared
        let stripH = (info?.enabled == true) ? strip(info!, width: size.width).height : 0
        let circuitH = size.height - stripH
        ctx.setFillColor(CGColor(gray: 1, alpha: 1))
        ctx.fill(CGRect(origin: .zero, size: size))
        // The circuit, fitted into the top part; y down inside.
        ctx.saveGState()
        ctx.translateBy(x: 0, y: size.height)
        ctx.scaleBy(x: 1, y: -1)
        if grid, let box = document.bounds(ofPage: page) {
            let upp = 1 / pointsPerUnit
            let ox = box.midX - size.width / 2 * upp, oy = box.midY + circuitH / 2 * upp
            ctx.setStrokeColor(CGColor(srgbRed: 0, green: 0, blue: 0.2, alpha: 0.2))
            ctx.setLineWidth(0.5)
            var x = ceil(ox)
            while x < ox + size.width * upp { let sx = (x - ox) / upp; ctx.move(to: CGPoint(x: sx, y: 0)); ctx.addLine(to: CGPoint(x: sx, y: circuitH)); x += 1 }
            var y = floor(oy)
            while y > oy - circuitH * upp { let sy = (oy - y) / upp; ctx.move(to: CGPoint(x: 0, y: sy)); ctx.addLine(to: CGPoint(x: size.width, y: sy)); y -= 1 }
            ctx.strokePath()
        }
        if blackAndWhite {
            _ = cl_document_draw_fitted(document.handle, Int32(page), ctx, size.width, circuitH, margin, scale, Int32(CL_STYLE_PRINT))
        } else if let box = document.bounds(ofPage: page) {
            let upp = max(box.width / (size.width - 2 * margin), box.height / (circuitH - 2 * margin))
            var o = CLDrawOptions(dark: false, accent: Int32(prefs.accent), wireScale: 1, simView: false,
                                  thumbnail: false, showSelection: false, selectionFade: 1)
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

    static func image(_ document: CoreDocument, page: Int, multiplier: CGFloat, blackAndWhite: Bool, grid: Bool, info: ExportInfo?) -> CGImage? {
        guard var size = size(document, page: page, info: info) else { return nil }
        size = CGSize(width: size.width.rounded(.up), height: size.height.rounded(.up))
        var scale = multiplier
        if max(size.width, size.height) * scale > 12000 { scale = 12000 / max(size.width, size.height) }
        guard let ctx = CGContext(data: nil, width: Int(size.width * scale), height: Int(size.height * scale),
                                  bitsPerComponent: 8, bytesPerRow: 0, space: CGColorSpace(name: CGColorSpace.sRGB)!,
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        ctx.scaleBy(x: scale, y: scale)
        draw(document, page: page, in: ctx, size: size, scale: scale, blackAndWhite: blackAndWhite, grid: grid, info: info)
        return ctx.makeImage()
    }

    static func pdf(_ document: CoreDocument, page: Int, blackAndWhite: Bool, grid: Bool, info: ExportInfo?) -> Data? {
        guard let size = size(document, page: page, info: info) else { return nil }
        let data = NSMutableData()
        var box = CGRect(origin: .zero, size: size)
        guard let consumer = CGDataConsumer(data: data as CFMutableData),
              let ctx = CGContext(consumer: consumer, mediaBox: &box, nil) else { return nil }
        ctx.beginPDFPage(nil)
        draw(document, page: page, in: ctx, size: size, scale: 1, blackAndWhite: blackAndWhite, grid: grid, info: info)
        ctx.endPDFPage()
        ctx.closePDF()
        return data as Data
    }
}

struct ExportImageView: View {
    let document: CoreDocument
    let page: Int
    let fileName: String
    @ObservedObject private var prefs = Prefs.shared
    @Environment(\.dismiss) private var dismiss
    @AppStorage("cl.exportGrid") private var grid = false
    @AppStorage("cl.exportBW") private var blackAndWhite = false
    @AppStorage("cl.exportRes") private var multiplier = 4
    @AppStorage("cl.exportWorks") private var works = true
    @AppStorage("cl.exportWhy") private var why = ""

    private var info: ExportInfo {
        ExportInfo(enabled: prefs.exportInfo, name: prefs.studentName, works: works, why: why, fileName: fileName)
    }
    /// A "does not work" answer needs its reason before anything goes out.
    private var ready: Bool { !prefs.exportInfo || works || !why.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty }

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Export as Image").font(.title2.weight(.semibold))
            preview
                .frame(maxWidth: .infinity)
                .frame(height: 220)
                .background(RoundedRectangle(cornerRadius: 8).fill(Color.secondary.opacity(0.08)))
            Toggle("Include grid lines", isOn: $grid)
            GroupBox("Name and result") {
                VStack(alignment: .leading, spacing: 8) {
                    Toggle("Add my name and whether the circuit works", isOn: $prefs.exportInfo)
                    HStack {
                        Text("Your name:")
                        TextField("First and last name", text: $prefs.studentName)
                    }
                    .disabled(!prefs.exportInfo)
                    Picker("", selection: $works) {
                        Text("My circuit works properly").tag(true)
                        Text("My circuit does not work because…").tag(false)
                    }
                    .pickerStyle(.radioGroup)
                    .labelsHidden()
                    .disabled(!prefs.exportInfo)
                    TextField("Explain what doesn't work (required)", text: $why, axis: .vertical)
                        .lineLimit(2...3)
                        .disabled(!prefs.exportInfo || works)
                }
                .padding(4)
            }
            HStack(alignment: .top, spacing: 20) {
                GroupBox("Output style") {
                    Picker("", selection: $blackAndWhite) {
                        Text("Color").tag(false)
                        Text("Black & White").tag(true)
                    }
                    .pickerStyle(.radioGroup).labelsHidden().padding(4)
                    .frame(maxWidth: .infinity, alignment: .leading)
                }
                GroupBox("Resolution (PNG)") {
                    Picker("", selection: $multiplier) {
                        Text("Screen (2×)").tag(2)
                        Text("Print (4×)").tag(4)
                        Text("High Quality (6×)").tag(6)
                    }
                    .pickerStyle(.radioGroup).labelsHidden().padding(4)
                    .frame(maxWidth: .infinity, alignment: .leading)
                }
            }
            HStack {
                Spacer()
                Button("Cancel") { dismiss() }.keyboardShortcut(.cancelAction)
                Button("Copy to Clipboard") { copy() }.disabled(!ready)
                Button("Export to File…") { save() }.keyboardShortcut(.defaultAction).disabled(!ready)
            }
        }
        .padding(20)
        .frame(width: 600)
    }

    private var preview: some View {
        Group {
            if let img = ImageExport.image(document, page: page, multiplier: 1, blackAndWhite: blackAndWhite, grid: grid, info: info) {
                Image(decorative: img, scale: 1).resizable().aspectRatio(contentMode: .fit).padding(8)
            } else {
                Text("This page is empty.").foregroundStyle(.secondary)
            }
        }
    }

    private func copy() {
        guard let img = ImageExport.image(document, page: page, multiplier: CGFloat(multiplier), blackAndWhite: blackAndWhite, grid: grid, info: info) else { return }
        let rep = NSBitmapImageRep(cgImage: img)
        NSPasteboard.general.clearContents()
        if let png = rep.representation(using: .png, properties: [:]) { NSPasteboard.general.setData(png, forType: .png) }
        if let tiff = rep.tiffRepresentation { NSPasteboard.general.setData(tiff, forType: .tiff) }
        dismiss()
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
            data = ImageExport.pdf(document, page: page, blackAndWhite: blackAndWhite, grid: grid, info: info)
        } else if let img = ImageExport.image(document, page: page, multiplier: CGFloat(multiplier), blackAndWhite: blackAndWhite, grid: grid, info: info) {
            data = NSBitmapImageRep(cgImage: img).representation(using: .png, properties: [:])
        } else { data = nil }
        do {
            guard let data else { throw CocoaError(.fileWriteUnknown) }
            try data.write(to: url, options: .atomic)
            dismiss()
        } catch { NSAlert(error: error).runModal() }
    }
}
