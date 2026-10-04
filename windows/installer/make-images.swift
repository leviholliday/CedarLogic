// Makes the Windows app's pictures that come from the Mac's artwork:
//   windows/res/CedarLogicDocument.ico -- the icon .cdl files carry in
//     Explorer (the Mac's CedarLogicDocument.icns: a page with CedarLogic's
//     icon and "CDL" on it), the exe's second icon
//   windows/installer/WizardSmall-*.bmp -- the installer's corner icon
//   windows/installer/WizardLarge-*.bmp -- the installer's side panel, in the
//     brand's look (the launch screen's ground, grid, bloom and icon)
// A size for each screen scale Inno Setup picks from. Run from the repo:
//   swift windows/installer/make-images.swift
import AppKit

let root = FileManager.default.currentDirectoryPath
func path(_ p: String) -> String { root + "/" + p }

// Premultiplied RGBA, drawn by `draw` into a w x h context (y up).
func render(_ w: Int, _ h: Int, _ draw: (CGContext) -> Void) -> [UInt8] {
    var px = [UInt8](repeating: 0, count: w * h * 4)
    let cs = CGColorSpace(name: CGColorSpace.sRGB)!
    px.withUnsafeMutableBytes { buf in
        let ctx = CGContext(data: buf.baseAddress, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4,
                            space: cs, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        ctx.interpolationQuality = .high
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(cgContext: ctx, flipped: false)
        draw(ctx)
        NSGraphicsContext.restoreGraphicsState()
    }
    return px
}

func le16(_ v: Int) -> [UInt8] { [UInt8(v & 0xff), UInt8((v >> 8) & 0xff)] }
func le32(_ v: Int) -> [UInt8] { le16(v & 0xffff) + le16((v >> 16) & 0xffff) }

// A 24-bit BMP, flattened onto `back` (Inno Setup draws these as they are).
func bmp(_ px: [UInt8], _ w: Int, _ h: Int, back: (UInt8, UInt8, UInt8)) -> Data {
    let row = (w * 3 + 3) / 4 * 4
    var out = [UInt8]("BM".utf8) + le32(54 + row * h) + le32(0) + le32(54)
    out += le32(40) + le32(w) + le32(h) + le16(1) + le16(24) + le32(0) + le32(row * h) + le32(2835) + le32(2835) + le32(0) + le32(0)
    for y in (0..<h).reversed() {   // bottom-up; the pixels are top-down
        var line = [UInt8]()
        for x in 0..<w {
            let i = (y * w + x) * 4
            let a = Int(px[i + 3])
            func over(_ c: UInt8, _ b: UInt8) -> UInt8 { UInt8(min(255, Int(c) + (255 - a) * Int(b) / 255)) }
            line += [over(px[i + 2], back.2), over(px[i + 1], back.1), over(px[i], back.0)]
        }
        line += [UInt8](repeating: 0, count: row - w * 3)
        out += line
    }
    return Data(out)
}

// An .ico: small sizes as 32-bit bitmaps with their mask, 256 as a PNG.
func ico(_ image: NSImage, sizes: [Int]) -> Data {
    var entries = [UInt8](), blobs = [[UInt8]]()
    var offset = 6 + 16 * sizes.count
    for s in sizes {
        let px = render(s, s) { _ in image.draw(in: NSRect(x: 0, y: 0, width: s, height: s)) }
        var blob = [UInt8]()
        if s >= 256 {
            let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: s, pixelsHigh: s, bitsPerSample: 8, samplesPerPixel: 4,
                                       hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: s * 4, bitsPerPixel: 32)!
            NSGraphicsContext.saveGraphicsState()
            NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
            NSGraphicsContext.current!.imageInterpolation = .high
            image.draw(in: NSRect(x: 0, y: 0, width: s, height: s))
            NSGraphicsContext.restoreGraphicsState()
            blob = [UInt8](rep.representation(using: .png, properties: [:])!)
        } else {
            let maskRow = (s + 31) / 32 * 4
            blob = le32(40) + le32(s) + le32(s * 2) + le16(1) + le16(32) + le32(0) + le32(s * s * 4 + maskRow * s) + le32(0) + le32(0) + le32(0) + le32(0)
            for y in (0..<s).reversed() {
                for x in 0..<s {
                    let i = (y * s + x) * 4
                    let a = Int(px[i + 3])
                    func straight(_ c: UInt8) -> UInt8 { a == 0 ? 0 : UInt8(min(255, Int(c) * 255 / a)) }
                    blob += [straight(px[i + 2]), straight(px[i + 1]), straight(px[i]), px[i + 3]]
                }
            }
            for y in (0..<s).reversed() {   // the mask: set where it's see-through
                var bits = [UInt8](repeating: 0, count: maskRow)
                for x in 0..<s where px[(y * s + x) * 4 + 3] == 0 { bits[x / 8] |= UInt8(0x80 >> (x % 8)) }
                blob += bits
            }
        }
        entries += [UInt8(s >= 256 ? 0 : s), UInt8(s >= 256 ? 0 : s), 0, 0] + le16(1) + le16(32) + le32(blob.count) + le32(offset)
        offset += blob.count
        blobs.append(blob)
    }
    return Data(le16(0) + le16(1) + le16(sizes.count) + entries + blobs.flatMap { $0 })
}

let document = NSImage(contentsOfFile: path("mac/App/CedarLogicDocument.icns"))!
try! ico(document, sizes: [16, 20, 24, 32, 40, 48, 64, 256]).write(to: URL(fileURLWithPath: path("windows/res/CedarLogicDocument.ico")))

let icon = NSImage(contentsOfFile: path("mac/App/LaunchIcon.png"))!
let white: (UInt8, UInt8, UInt8) = (255, 255, 255)

// The corner icon: Inno Setup's sizes for 100%, 125%, 150%, 175% and 200%.
for (scale, w, h) in [(100, 55, 55), (125, 64, 68), (150, 83, 80), (175, 92, 97), (200, 110, 106)] {
    let px = render(w, h) { _ in
        let s = CGFloat(min(w, h)) * 0.9
        icon.draw(in: NSRect(x: (CGFloat(w) - s) / 2, y: (CGFloat(h) - s) / 2, width: s, height: s))
    }
    try! bmp(px, w, h, back: white).write(to: URL(fileURLWithPath: path("windows/installer/WizardSmall-\(scale).bmp")))
}

// The side panel (Inno Setup's sizes for 100%, 150% and 200%): the ground,
// its grid fading out from the icon, a green bloom, the icon glowing in it,
// and the name.
let ink = NSColor(srgbRed: 0.035, green: 0.063, blue: 0.047, alpha: 1)
let inkDeep = NSColor(srgbRed: 0.012, green: 0.024, blue: 0.018, alpha: 1)
let neon = NSColor(srgbRed: 0.22, green: 1.0, blue: 0.42, alpha: 1)
for (scale, w, h) in [(100, 164, 314), (150, 246, 459), (200, 328, 604)] {
    let px = render(w, h) { ctx in
        let W = CGFloat(w), H = CGFloat(h), u = W / 164   // u: a point at this scale
        let cs = CGColorSpace(name: CGColorSpace.sRGB)!
        let ground = CGGradient(colorsSpace: cs, colors: [ink.cgColor, inkDeep.cgColor] as CFArray, locations: [0, 1])!
        ctx.drawLinearGradient(ground, start: CGPoint(x: 0, y: H), end: CGPoint(x: 0, y: 0), options: [])
        let centre = CGPoint(x: W / 2, y: H * 0.6)
        // The grid, as the launch screen's (a step of 28 points at 1.0 here is too coarse: 16).
        ctx.setStrokeColor(neon.withAlphaComponent(0.07).cgColor)
        ctx.setLineWidth(0.5 * u)
        let step = 16 * u
        var x = (W.truncatingRemainder(dividingBy: step)) / 2
        while x < W { ctx.move(to: CGPoint(x: x, y: 0)); ctx.addLine(to: CGPoint(x: x, y: H)); x += step }
        var y = (H.truncatingRemainder(dividingBy: step)) / 2
        while y < H { ctx.move(to: CGPoint(x: 0, y: y)); ctx.addLine(to: CGPoint(x: W, y: y)); y += step }
        ctx.strokePath()
        // The grid fades towards the edges: the ground drawn back over it, more so further out.
        let fade = CGGradient(colorsSpace: cs, colors: [ink.withAlphaComponent(0).cgColor, ink.withAlphaComponent(0).cgColor,
                                                        inkDeep.withAlphaComponent(0.92).cgColor] as CFArray, locations: [0, 0.18, 1])!
        ctx.drawRadialGradient(fade, startCenter: centre, startRadius: 0, endCenter: centre, endRadius: H * 0.62,
                               options: [.drawsAfterEndLocation])
        let bloom = CGGradient(colorsSpace: cs, colors: [neon.withAlphaComponent(0.2).cgColor, neon.withAlphaComponent(0).cgColor] as CFArray,
                               locations: [0, 1])!
        ctx.drawRadialGradient(bloom, startCenter: centre, startRadius: 0, endCenter: centre, endRadius: W * 0.75, options: [])
        // The icon, glowing.
        let s = 88 * u
        ctx.saveGState()
        ctx.setShadow(offset: .zero, blur: 22 * u, color: neon.withAlphaComponent(0.45).cgColor)
        icon.draw(in: NSRect(x: centre.x - s / 2, y: centre.y - s / 2, width: s, height: s))
        ctx.restoreGState()
        // The name, under it.
        let name = NSAttributedString(string: "CedarLogic", attributes: [
            .font: NSFont.systemFont(ofSize: 19 * u, weight: .semibold),
            .foregroundColor: NSColor(white: 0.95, alpha: 1),
            .kern: 0.3 * u,
        ])
        let size = name.size()
        name.draw(at: NSPoint(x: (W - size.width) / 2, y: centre.y - s / 2 - 22 * u - size.height))
    }
    try! bmp(px, w, h, back: (0, 0, 0)).write(to: URL(fileURLWithPath: path("windows/installer/WizardLarge-\(scale).bmp")))
}
print("made windows/res/CedarLogicDocument.ico and windows/installer/Wizard*.bmp")
