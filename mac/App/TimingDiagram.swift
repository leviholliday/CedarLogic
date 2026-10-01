// The oscilloscope as a timing diagram for a lab report: white paper, black
// traces, each signal's name, the step numbers along the bottom, and a title
// (with your name, as image exports have). Copied as an image, or saved as a
// PNG or a PDF, of what's on screen or the whole recording.

import AppKit

@MainActor
enum TimingDiagram {
    struct Signal { let index: Int; let name: String }

    private static let margin: CGFloat = 28, nameWidth: CGFloat = 110, lane: CGFloat = 38
    private static let titleHeight: CGFloat = 46, axisHeight: CGFloat = 34

    /// The samples `range` of `signals`, as a picture `size` points big.
    static func size(steps: Int, signals: Int) -> (size: CGSize, pointsPerStep: CGFloat) {
        let n = max(1, steps)
        // Readable steps where there's room; squeezed to fit a wide page otherwise.
        let pps = min(24, max(0.5, 1400 / CGFloat(n)))
        let width = margin * 2 + nameWidth + CGFloat(n) * pps
        let height = margin * 2 + titleHeight + CGFloat(max(1, signals)) * lane + axisHeight
        return (CGSize(width: max(width, 520), height: height), pps)
    }

    static func draw(_ ctx: CGContext, document: CoreDocument, signals: [Signal], range: Range<Int>, title: String, color: Bool) {
        let (size, pps) = size(steps: range.count, signals: signals.count)
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(cgContext: ctx, flipped: true)
        defer { NSGraphicsContext.restoreGraphicsState() }
        // Top-left origin, y down, like the screen.
        ctx.saveGState()
        ctx.translateBy(x: 0, y: size.height)
        ctx.scaleBy(x: 1, y: -1)
        defer { ctx.restoreGState() }

        ctx.setFillColor(NSColor.white.cgColor)
        ctx.fill(CGRect(origin: .zero, size: size))

        func text(_ s: String, _ at: CGPoint, size: CGFloat, weight: NSFont.Weight = .regular,
                  color: NSColor = .black, align: NSTextAlignment = .left, mono: Bool = false) {
            let font = mono ? NSFont.monospacedDigitSystemFont(ofSize: size, weight: weight) : NSFont.systemFont(ofSize: size, weight: weight)
            let str = NSAttributedString(string: s, attributes: [.font: font, .foregroundColor: color])
            let w = str.size().width
            let x = align == .right ? at.x - w : align == .center ? at.x - w / 2 : at.x
            str.draw(at: CGPoint(x: x, y: at.y - str.size().height / 2))
        }

        // Title, and who made it.
        text(title, CGPoint(x: margin, y: margin + 10), size: 17, weight: .semibold)
        let prefs = Prefs.shared
        var byline = "Timing diagram"
        if prefs.exportInfo, !prefs.studentName.isEmpty { byline += " · " + prefs.studentName }
        byline += " · " + Date().formatted(date: .abbreviated, time: .omitted)
        text(byline, CGPoint(x: margin, y: margin + 30), size: 11, color: .darkGray)

        let left = margin + nameWidth
        let top = margin + titleHeight
        let bottom = top + CGFloat(signals.count) * lane
        func x(_ i: Int) -> CGFloat { left + CGFloat(i - range.lowerBound) * pps }

        // Time grid and axis: a tick every so many steps.
        let firstStep = Int(cl_scope_first_step(document.handle))
        let every = [1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000].first { CGFloat($0) * pps >= 44 } ?? 10000
        ctx.setLineWidth(0.5)
        var i = range.lowerBound - ((range.lowerBound + firstStep) % every)
        while i <= range.upperBound {
            if i >= range.lowerBound {
                ctx.setStrokeColor(NSColor(white: 0.82, alpha: 1).cgColor)
                ctx.setLineDash(phase: 0, lengths: [2, 3])
                ctx.move(to: CGPoint(x: x(i), y: top)); ctx.addLine(to: CGPoint(x: x(i), y: bottom)); ctx.strokePath()
                ctx.setLineDash(phase: 0, lengths: [])
                ctx.setStrokeColor(NSColor.black.cgColor)
                ctx.move(to: CGPoint(x: x(i), y: bottom)); ctx.addLine(to: CGPoint(x: x(i), y: bottom + 4)); ctx.strokePath()
                text("\(i + firstStep)", CGPoint(x: x(i), y: bottom + 12), size: 9.5, color: .darkGray, align: .center, mono: true)
            }
            i += every
        }
        ctx.setStrokeColor(NSColor.black.cgColor)
        ctx.setLineWidth(1)
        ctx.move(to: CGPoint(x: left, y: bottom)); ctx.addLine(to: CGPoint(x: x(range.upperBound), y: bottom)); ctx.strokePath()
        text("step", CGPoint(x: left + (x(range.upperBound) - left) / 2, y: bottom + 26), size: 9.5, color: .gray, align: .center)

        // The traces.
        var buffer = [UInt8](repeating: 255, count: max(1, range.count))
        for (row, sig) in signals.enumerated() {
            let laneTop = top + CGFloat(row) * lane
            let hi = laneTop + 9, lo = laneTop + lane - 9
            text(sig.name, CGPoint(x: left - 12, y: (hi + lo) / 2), size: 12, weight: .medium, align: .right)
            let n = Int(cl_scope_samples(document.handle, Int32(sig.index), Int64(range.lowerBound), Int32(range.count), &buffer))
            var runStart = 0
            var lastY: CGFloat?
            let path = CGMutablePath()
            while runStart < n {
                let v = buffer[runStart]
                var runEnd = runStart + 1
                while runEnd < n && buffer[runEnd] == v { runEnd += 1 }
                let a = x(range.lowerBound + runStart), b = x(range.lowerBound + runEnd)
                switch v {
                case 0, 1:
                    let y = v == 1 ? hi : lo
                    if let ly = lastY, ly != y { path.move(to: CGPoint(x: a, y: ly)); path.addLine(to: CGPoint(x: a, y: y)) }
                    path.move(to: CGPoint(x: a, y: y)); path.addLine(to: CGPoint(x: b, y: y))
                    if v == 1 {
                        ctx.setFillColor((color ? NSColor(srgbRed: 0.85, green: 0.95, blue: 0.87, alpha: 1) : NSColor(white: 0.9, alpha: 1)).cgColor)
                        ctx.fill(CGRect(x: a, y: hi, width: b - a, height: lo - hi))
                    }
                    lastY = y
                case 2:   // floating: a dashed line in the middle
                    ctx.setStrokeColor((color ? NSColor.systemBlue : NSColor.black).cgColor)
                    ctx.setLineWidth(1)
                    ctx.setLineDash(phase: 0, lengths: [3, 2])
                    ctx.move(to: CGPoint(x: a, y: (hi + lo) / 2)); ctx.addLine(to: CGPoint(x: b, y: (hi + lo) / 2)); ctx.strokePath()
                    ctx.setLineDash(phase: 0, lengths: [])
                    lastY = nil
                case 3, 4:   // conflict or unknown: a hatched band
                    let band = CGRect(x: a, y: hi, width: b - a, height: lo - hi)
                    ctx.saveGState()
                    ctx.clip(to: band)
                    ctx.setStrokeColor((color ? (v == 3 ? NSColor.systemRed : NSColor.systemOrange) : NSColor(white: 0.45, alpha: 1)).cgColor)
                    ctx.setLineWidth(0.6)
                    var hx = band.minX - band.height
                    while hx < band.maxX { ctx.move(to: CGPoint(x: hx, y: band.maxY)); ctx.addLine(to: CGPoint(x: hx + band.height, y: band.minY)); hx += 5 }
                    ctx.strokePath()
                    ctx.restoreGState()
                    ctx.setStrokeColor((color ? (v == 3 ? NSColor.systemRed : NSColor.systemOrange) : NSColor.black).cgColor)
                    ctx.setLineWidth(0.8); ctx.stroke(band)
                    lastY = nil
                default:
                    lastY = nil
                }
                runStart = runEnd
            }
            ctx.setStrokeColor((color ? NSColor(srgbRed: 0.05, green: 0.55, blue: 0.2, alpha: 1) : NSColor.black).cgColor)
            ctx.setLineWidth(1.6)
            ctx.addPath(path)
            ctx.strokePath()
        }
    }

    static func pngData(document: CoreDocument, signals: [Signal], range: Range<Int>, title: String, color: Bool) -> Data? {
        let (size, _) = size(steps: range.count, signals: signals.count)
        let scale: CGFloat = 2
        guard let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(size.width * scale), pixelsHigh: Int(size.height * scale),
                                         bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                                         colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0),
              let g = NSGraphicsContext(bitmapImageRep: rep) else { return nil }
        g.cgContext.scaleBy(x: scale, y: scale)
        draw(g.cgContext, document: document, signals: signals, range: range, title: title, color: color)
        rep.size = size
        return rep.representation(using: .png, properties: [:])
    }

    static func pdfData(document: CoreDocument, signals: [Signal], range: Range<Int>, title: String, color: Bool) -> Data? {
        let (size, _) = size(steps: range.count, signals: signals.count)
        let data = NSMutableData()
        var box = CGRect(origin: .zero, size: size)
        guard let consumer = CGDataConsumer(data: data as CFMutableData),
              let ctx = CGContext(consumer: consumer, mediaBox: &box, nil) else { return nil }
        ctx.beginPDFPage(nil)
        draw(ctx, document: document, signals: signals, range: range, title: title, color: color)
        ctx.endPDFPage()
        ctx.closePDF()
        return data as Data
    }
}
