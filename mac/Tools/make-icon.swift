// Makes the native app's icon: CedarLogic's icon with a "NATIVE" badge, so it
// can't be mistaken for the wx app in the Dock or Applications.
//   swift mac/Tools/make-icon.swift <in.iconset> <out.iconset>
// then: iconutil -c icns <out.iconset> -o mac/App/CedarLogicNative.icns
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
    image.draw(in: NSRect(x: 0, y: 0, width: W, height: H))
    // A pill across the bottom of the tile (y up: the tile's bottom edge is
    // about 10% in from the image's).
    let pill = NSRect(x: W * 0.22, y: H * 0.14, width: W * 0.56, height: H * 0.15)
    let path = NSBezierPath(roundedRect: pill, xRadius: pill.height / 2, yRadius: pill.height / 2)
    NSColor(calibratedRed: 0.13, green: 0.47, blue: 1.0, alpha: 1).setFill()
    path.fill()
    NSColor.white.withAlphaComponent(0.9).setStroke()
    path.lineWidth = max(1, W * 0.008)
    path.stroke()
    if w >= 64 {
        let font = NSFont.systemFont(ofSize: pill.height * 0.62, weight: .heavy)
        let text = NSAttributedString(string: "NATIVE", attributes: [
            .font: font, .foregroundColor: NSColor.white, .kern: pill.height * 0.06])
        let size = text.size()
        text.draw(at: NSPoint(x: pill.midX - size.width / 2, y: pill.midY - size.height / 2 + font.descender * 0.1))
    }
    NSGraphicsContext.restoreGraphicsState()
    try out.representation(using: .png, properties: [:])!.write(to: dst.appendingPathComponent(name))
}
