// The Mac app's disk image background: the wx app's (res/macos/dmg-background.tiff)
// with the Applications folder's icon drawn onto the right-hand panel, exactly
// under where Finder draws the Applications link (mac/dmg-settings.py: 128 pt
// icons, centred at 495, 185). Finder on macOS 26 sometimes shows that link's
// icon for a moment and then drops it; with the icon in the picture as well,
// the window looks the same either way.
//
//     swift mac/Tools/make-dmg-background.swift <out dir>   (run from the repo root)
//     tiffutil -cathidpicheck <out>/bg.png <out>/bg@2x.png -out mac/dmg-background.tiff
import AppKit

let out = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "."
guard let base = NSImage(contentsOfFile: "res/macos/dmg-background.tiff") else { fatalError("no background") }
let folder = NSWorkspace.shared.icon(forFile: "/Applications")
let size = NSSize(width: 660, height: 400)
let center = NSPoint(x: 495, y: 185)   // from the top left, as Finder counts
let icon: CGFloat = 128

for scale in [1, 2] {
    let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(size.width) * scale, pixelsHigh: Int(size.height) * scale,
                               bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                               colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
    rep.size = size
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
    NSGraphicsContext.current?.imageInterpolation = .high
    base.draw(in: NSRect(origin: .zero, size: size))
    let rect = NSRect(x: center.x - icon / 2, y: size.height - center.y - icon / 2, width: icon, height: icon)
    folder.draw(in: rect, from: .zero, operation: .sourceOver, fraction: 1,
                respectFlipped: true, hints: [.ctm: AffineTransform(scale: CGFloat(scale))])
    NSGraphicsContext.restoreGraphicsState()
    let png = rep.representation(using: .png, properties: [:])!
    try! png.write(to: URL(fileURLWithPath: out).appendingPathComponent(scale == 1 ? "bg.png" : "bg@2x.png"))
}
print("ok")
