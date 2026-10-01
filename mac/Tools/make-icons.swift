// Every CedarLogic icon from the one piece of artwork (res/macos/icon-artwork.png,
// 1254 px, the rounded tile on a dark ground): the tile is cut out along the
// Mac's own icon shape (a continuous-corner square), given a fresh rim of
// light and a faint sheen, and placed on Apple's icon grid (824 of 1024, with
// its shadow). Out come:
//   <out>/app.iconset       the Mac app's icon (iconutil -c icns)
//   <out>/LaunchIcon.png    the bare tile, 512 px (launch screen, welcome)
//   <out>/tile-<n>.png      the tile with a little room round it, for the
//                           Windows .ico, Linux, and the website
//   swift mac/Tools/make-icons.swift res/macos/icon-artwork.png <out>
// (mac/Tools/make-icons.sh runs it and puts everything in place.)
import AppKit
import SwiftUI

let args = CommandLine.arguments
guard args.count >= 3, let src = NSImage(contentsOfFile: args[1]),
      let source = src.cgImage(forProposedRect: nil, context: nil, hints: nil) else {
    print("usage: make-icons.swift <artwork.png> <out dir>"); exit(1)
}
let out = URL(fileURLWithPath: args[2])
try? FileManager.default.createDirectory(at: out, withIntermediateDirectories: true)

// The tile in the artwork (pixels, top-left origin), measured along its rim,
// then a few pixels in so the old rim and the dark ground stay outside.
let sw = CGFloat(source.width) / 1254
let tile = CGRect(x: 91 * sw, y: 81 * sw, width: (1162 - 91) * sw, height: (1164 - 81) * sw)
guard let cropped = source.cropping(to: tile) else { print("crop failed"); exit(1) }

/// The Mac's icon shape: a continuous-corner square, its corners 22.37% of its side.
func shape(_ r: CGRect) -> CGPath {
    Path { $0.addRoundedRect(in: r, cornerSize: CGSize(width: r.width * 0.2237, height: r.height * 0.2237), style: .continuous) }.cgPath
}

func context(_ w: Int, _ h: Int) -> CGContext {
    let c = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: 0,
                      space: CGColorSpace(name: CGColorSpace.sRGB)!,
                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    c.interpolationQuality = .high
    c.setShouldAntialias(true)
    return c
}

/// The tile drawn into `r` (y up): the artwork clipped to the icon shape, a
/// soft sheen over the top, and a rim lit from above.
func drawTile(_ c: CGContext, in r: CGRect, detail: Bool) {
    let path = shape(r)
    c.saveGState()
    c.addPath(path); c.clip()
    c.draw(cropped, in: r)
    if detail {
        // A faint sheen across the top third, as light on glass.
        let sheen = CGGradient(colorsSpace: CGColorSpace(name: CGColorSpace.sRGB),
                               colors: [CGColor(gray: 1, alpha: 0.075), CGColor(gray: 1, alpha: 0)] as CFArray,
                               locations: [0, 1])!
        c.drawLinearGradient(sheen, start: CGPoint(x: r.midX, y: r.maxY), end: CGPoint(x: r.midX, y: r.maxY - r.height * 0.42), options: [])
    }
    c.restoreGState()
    // The rim: bright along the top, fading down the sides to almost nothing.
    let lw = max(1, r.width * (detail ? 0.0045 : 0.008))
    c.saveGState()
    c.addPath(shape(r.insetBy(dx: lw / 2, dy: lw / 2)))
    c.setLineWidth(lw)
    c.replacePathWithStrokedPath()
    c.clip()
    let rim = CGGradient(colorsSpace: CGColorSpace(name: CGColorSpace.sRGB),
                         colors: [CGColor(gray: 1, alpha: detail ? 0.42 : 0.3), CGColor(gray: 1, alpha: 0.10),
                                  CGColor(gray: 1, alpha: 0.05)] as CFArray,
                         locations: [0, 0.5, 1])!
    c.drawLinearGradient(rim, start: CGPoint(x: r.midX, y: r.maxY), end: CGPoint(x: r.midX, y: r.minY), options: [])
    c.restoreGState()
}

func write(_ c: CGContext, _ url: URL) {
    let rep = NSBitmapImageRep(cgImage: c.makeImage()!)
    try! rep.representation(using: .png, properties: [:])!.write(to: url)
}

// The Mac app icon: Apple's grid (the tile 824 of 1024, a little above the
// middle to leave room for its shadow).
let iconset = out.appendingPathComponent("app.iconset")
try? FileManager.default.createDirectory(at: iconset, withIntermediateDirectories: true)
for (points, scale) in [(16, 1), (16, 2), (32, 1), (32, 2), (128, 1), (128, 2), (256, 1), (256, 2), (512, 1), (512, 2)] {
    let px = points * scale
    let c = context(px, px)
    let k = CGFloat(px) / 1024
    let r = CGRect(x: 100 * k, y: 100 * k + 6 * k, width: 824 * k, height: 824 * k)
    c.saveGState()
    c.setShadow(offset: CGSize(width: 0, height: -12 * k), blur: 28 * k, color: CGColor(gray: 0, alpha: 0.5))
    c.addPath(shape(r)); c.setFillColor(CGColor(gray: 0.02, alpha: 1)); c.fillPath()
    c.restoreGState()
    drawTile(c, in: r, detail: px >= 64)
    write(c, iconset.appendingPathComponent("icon_\(points)x\(points)\(scale == 2 ? "@2x" : "").png"))
}

// The bare tile, for the app's own screens (they round and shadow it themselves).
do {
    let c = context(512, 512)
    drawTile(c, in: CGRect(x: 0, y: 0, width: 512, height: 512), detail: true)
    write(c, out.appendingPathComponent("LaunchIcon.png"))
}

// The tile with a little room round it: Windows, Linux, the website.
for px in [16, 24, 32, 48, 64, 128, 180, 192, 256, 512, 1024] {
    let c = context(px, px)
    let pad = CGFloat(px) * (px <= 32 ? 0.02 : 0.04)
    drawTile(c, in: CGRect(x: pad, y: pad, width: CGFloat(px) - 2 * pad, height: CGFloat(px) - 2 * pad), detail: px >= 64)
    write(c, out.appendingPathComponent("tile-\(px).png"))
}
print("icons in \(out.path)")
