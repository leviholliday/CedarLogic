// Makes the icon .cdl circuit files carry in Finder: a page with a folded
// corner, CedarLogic's icon on it and "CDL" underneath.
//   iconutil -c iconset mac/App/CedarLogicNative.icns -o /tmp/app.iconset
//   swift mac/Tools/make-doc-icon.swift /tmp/app.iconset /tmp/doc.iconset
//   iconutil -c icns /tmp/doc.iconset -o mac/App/CedarLogicDocument.icns
import AppKit

let args = CommandLine.arguments
let src = URL(fileURLWithPath: args[1]), dst = URL(fileURLWithPath: args[2])
try? FileManager.default.createDirectory(at: dst, withIntermediateDirectories: true)

for name in try FileManager.default.contentsOfDirectory(atPath: src.path) where name.hasSuffix(".png") {
    guard let image = NSImage(contentsOf: src.appendingPathComponent(name)),
          let rep = image.representations.first else { continue }
    let w = rep.pixelsWide, h = rep.pixelsHigh
    let out = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: w, pixelsHigh: h, bitsPerSample: 8,
                               samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                               colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: out)
    let W = CGFloat(w), H = CGFloat(h)

    // The page: 78% of the height, 62% of the width, centred, top-right corner folded.
    let pw = W * 0.62, ph = H * 0.80
    let page = NSRect(x: (W - pw) / 2, y: H * 0.09, width: pw, height: ph)
    let fold = pw * 0.26
    let r = W * 0.035
    let shape = NSBezierPath()
    shape.move(to: NSPoint(x: page.minX + r, y: page.minY))
    shape.line(to: NSPoint(x: page.maxX - r, y: page.minY))
    shape.curve(to: NSPoint(x: page.maxX, y: page.minY + r), controlPoint1: NSPoint(x: page.maxX, y: page.minY), controlPoint2: NSPoint(x: page.maxX, y: page.minY))
    shape.line(to: NSPoint(x: page.maxX, y: page.maxY - fold))
    shape.line(to: NSPoint(x: page.maxX - fold, y: page.maxY))
    shape.line(to: NSPoint(x: page.minX + r, y: page.maxY))
    shape.curve(to: NSPoint(x: page.minX, y: page.maxY - r), controlPoint1: NSPoint(x: page.minX, y: page.maxY), controlPoint2: NSPoint(x: page.minX, y: page.maxY))
    shape.line(to: NSPoint(x: page.minX, y: page.minY + r))
    shape.curve(to: NSPoint(x: page.minX + r, y: page.minY), controlPoint1: NSPoint(x: page.minX, y: page.minY), controlPoint2: NSPoint(x: page.minX, y: page.minY))
    shape.close()

    NSGraphicsContext.saveGraphicsState()
    let shadow = NSShadow()
    shadow.shadowColor = NSColor.black.withAlphaComponent(0.35)
    shadow.shadowBlurRadius = W * 0.025
    shadow.shadowOffset = NSSize(width: 0, height: -W * 0.012)
    shadow.set()
    NSColor(calibratedWhite: 0.97, alpha: 1).setFill()
    shape.fill()
    NSGraphicsContext.restoreGraphicsState()
    NSColor(calibratedWhite: 0.78, alpha: 1).setStroke()
    shape.lineWidth = max(1, W * 0.004)
    shape.stroke()

    // The folded corner.
    let corner = NSBezierPath()
    corner.move(to: NSPoint(x: page.maxX - fold, y: page.maxY))
    corner.line(to: NSPoint(x: page.maxX - fold, y: page.maxY - fold))
    corner.line(to: NSPoint(x: page.maxX, y: page.maxY - fold))
    corner.close()
    NSColor(calibratedWhite: 0.88, alpha: 1).setFill()
    corner.fill()
    NSColor(calibratedWhite: 0.75, alpha: 1).setStroke()
    corner.lineWidth = max(1, W * 0.004)
    corner.stroke()

    // CedarLogic's icon on the page.
    let side = pw * 0.66
    image.draw(in: NSRect(x: page.midX - side / 2, y: page.minY + ph * 0.30, width: side, height: side),
               from: .zero, operation: .sourceOver, fraction: 1)

    // "CDL" underneath.
    if w >= 64 {
        let font = NSFont.systemFont(ofSize: ph * 0.11, weight: .heavy)
        let text = NSAttributedString(string: "CDL", attributes: [
            .font: font, .foregroundColor: NSColor(calibratedRed: 0.05, green: 0.55, blue: 0.22, alpha: 1), .kern: ph * 0.012])
        let size = text.size()
        text.draw(at: NSPoint(x: page.midX - size.width / 2, y: page.minY + ph * 0.10))
    }
    NSGraphicsContext.restoreGraphicsState()
    try out.representation(using: .png, properties: [:])!.write(to: dst.appendingPathComponent(name))
}
