// For mac/Tools/sync-selftest.sh: reads blocks of "size" + size rows of 0/1
// (sync_qr_dump) from stdin, draws each with a 4-module quiet zone and prints
// "size text" as Core Image's QR detector reads it.
import CoreImage
import Foundation

let lines = String(data: FileHandle.standardInput.readDataToEndOfFile(), encoding: .utf8)!
    .split(separator: "\n").map(String.init)
var i = 0
while i < lines.count {
    let size = Int(lines[i])!
    i += 1
    let rows = lines[i..<i + size].map { Array($0) }
    i += size
    let scale = 8, quiet = 4, w = (size + 2 * quiet) * scale
    var px = [UInt8](repeating: 255, count: w * w)
    for y in 0..<size {
        for x in 0..<size where rows[y][x] == "1" {
            for dy in 0..<scale {
                for dx in 0..<scale { px[((y + quiet) * scale + dy) * w + (x + quiet) * scale + dx] = 0 }
            }
        }
    }
    let provider = CGDataProvider(data: Data(px) as CFData)!
    let image = CGImage(width: w, height: w, bitsPerComponent: 8, bitsPerPixel: 8, bytesPerRow: w,
                        space: CGColorSpaceCreateDeviceGray(), bitmapInfo: CGBitmapInfo(rawValue: 0),
                        provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)!
    let detector = CIDetector(ofType: CIDetectorTypeQRCode, context: nil,
                              options: [CIDetectorAccuracy: CIDetectorAccuracyHigh])!
    let found = detector.features(in: CIImage(cgImage: image)).compactMap { ($0 as? CIQRCodeFeature)?.messageString }
    print(size, found.first ?? "<not read>")
}
