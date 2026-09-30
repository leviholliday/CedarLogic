// Writes a PNG's pixels as raw RGBA (straight alpha, top row first), for the
// Windows .ico's bitmap sizes: swift png-to-rgba.swift <in.png> <out.rgba>
import AppKit
let a = CommandLine.arguments
let img = NSImage(contentsOfFile: a[1])!.cgImage(forProposedRect: nil, context: nil, hints: nil)!
let w = img.width, h = img.height
var px = [UInt8](repeating: 0, count: w * h * 4)
let ctx = CGContext(data: &px, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4,
                    space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
ctx.draw(img, in: CGRect(x: 0, y: 0, width: w, height: h))
for i in stride(from: 0, to: px.count, by: 4) where px[i + 3] > 0 && px[i + 3] < 255 {   // un-premultiply
    let al = Double(px[i + 3])
    for c in 0..<3 { px[i + c] = UInt8(min(255, (Double(px[i + c]) * 255 / al).rounded())) }
}
try! Data(px).write(to: URL(fileURLWithPath: a[2]))
