// Screenshots and recordings of the app for Send Feedback, made by the app
// drawing its own window: no screen-recording permission to ask for, and
// nothing but CedarLogic in them. A recording draws the pointer in, and a
// ring where it clicks, and stops itself after a minute.

import AppKit
import AVFoundation
import SwiftUI

@MainActor
enum FeedbackCapture {
    /// The window as it looks, title bar row included, at the screen's sharpness.
    static func png(of w: NSWindow) -> Data? {
        guard let v = w.contentView?.superview, let rep = v.bitmapImageRepForCachingDisplay(in: v.bounds) else { return nil }
        v.cacheDisplay(in: v.bounds, to: rep)
        return rep.representation(using: .png, properties: [:])
    }
}

/// A recording of one window, 15 frames a second, into an MP4.
@MainActor
final class FeedbackRecorder: ObservableObject {
    static let maxSeconds: Double = 60

    @Published private(set) var elapsed: Double = 0
    let window: NSWindow
    let url: URL
    private let writer: AVAssetWriter
    private let input: AVAssetWriterInput
    private let adaptor: AVAssetWriterInputPixelBufferAdaptor
    private let pixels: CGSize
    private var timer: Timer?
    private var start: CFTimeInterval = 0
    private var rep: NSBitmapImageRep?
    private var firstFrame: NSImage?
    private var hud: NSPanel?
    private var clicks: [(CGPoint, CFTimeInterval)] = []
    private var mouseMonitor: Any?
    private var finish: ((URL?, Double, NSImage?) -> Void)?

    init?(window: NSWindow) {
        self.window = window
        let size = window.frame.size
        // As sharp as the screen, but no wider than 1600 pixels (a minute
        // stays a few megabytes), in even numbers (H.264 wants them).
        let scale = min(window.backingScaleFactor, 1600 / max(size.width, 1))
        pixels = CGSize(width: (size.width * scale / 2).rounded() * 2, height: (size.height * scale / 2).rounded() * 2)
        url = FeedbackModel.folder.appendingPathComponent("\(UUID().uuidString)-recording.mp4")
        guard let w = try? AVAssetWriter(outputURL: url, fileType: .mp4) else { return nil }
        writer = w
        input = AVAssetWriterInput(mediaType: .video, outputSettings: [
            AVVideoCodecKey: AVVideoCodecType.h264,
            AVVideoWidthKey: Int(pixels.width),
            AVVideoHeightKey: Int(pixels.height),
            AVVideoCompressionPropertiesKey: [
                AVVideoAverageBitRateKey: 2_000_000,
                AVVideoExpectedSourceFrameRateKey: 15,
                AVVideoMaxKeyFrameIntervalKey: 30,
                AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel,
            ],
        ])
        input.expectsMediaDataInRealTime = true
        adaptor = AVAssetWriterInputPixelBufferAdaptor(assetWriterInput: input, sourcePixelBufferAttributes: [
            kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA,
            kCVPixelBufferWidthKey as String: Int(pixels.width),
            kCVPixelBufferHeightKey as String: Int(pixels.height),
        ])
        guard writer.canAdd(input) else { return nil }
        writer.add(input)
    }

    /// Starts; `done` gets the file (nil if cancelled), its length and its first frame.
    func begin(done: @escaping (URL?, Double, NSImage?) -> Void) {
        finish = done
        guard writer.startWriting() else { done(nil, 0, nil); return }
        writer.startSession(atSourceTime: .zero)
        start = CACurrentMediaTime()
        showHUD()
        // A ring where the pointer clicks, so a watcher sees what was pressed.
        mouseMonitor = NSEvent.addLocalMonitorForEvents(matching: [.leftMouseDown, .rightMouseDown]) { [weak self] e in
            MainActor.assumeIsolated {
                guard let self, e.window === self.window else { return }
                self.clicks.append((e.locationInWindow, CACurrentMediaTime()))
            }
            return e
        }
        let t = Timer(timeInterval: 1.0 / 15, repeats: true) { [weak self] _ in MainActor.assumeIsolated { self?.frame() } }
        RunLoop.main.add(t, forMode: .common)
        timer = t
        frame()
    }

    func stop() { end(keep: true) }
    func cancel() { end(keep: false) }

    private func end(keep: Bool) {
        guard let done = finish else { return }
        finish = nil
        timer?.invalidate()
        timer = nil
        if let m = mouseMonitor { NSEvent.removeMonitor(m); mouseMonitor = nil }
        hud?.orderOut(nil)
        hud = nil
        let length = elapsed
        let thumb = firstFrame
        input.markAsFinished()
        writer.finishWriting { [url, writer] in
            let ok = writer.status == .completed
            DispatchQueue.main.async {
                if keep && ok && length > 0.3 {
                    done(url, length, thumb)
                } else {
                    try? FileManager.default.removeItem(at: url)
                    done(nil, 0, nil)
                }
            }
        }
    }

    private func frame() {
        let t = CACurrentMediaTime() - start
        elapsed = t
        if t >= Self.maxSeconds { stop(); return }
        guard input.isReadyForMoreMediaData, let pool = adaptor.pixelBufferPool,
              let view = window.contentView?.superview else { return }
        var buffer: CVPixelBuffer?
        CVPixelBufferPoolCreatePixelBuffer(nil, pool, &buffer)
        guard let buffer else { return }

        // The window drawn straight at the recording's size.
        let bounds = view.bounds
        if rep == nil || rep!.size != bounds.size {
            rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(pixels.width), pixelsHigh: Int(pixels.height),
                                   bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                                   colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)
            rep?.size = bounds.size
        }
        guard let rep else { return }
        view.cacheDisplay(in: bounds, to: rep)
        guard let image = rep.cgImage else { return }
        if firstFrame == nil { firstFrame = NSImage(cgImage: image, size: bounds.size) }

        CVPixelBufferLockBaseAddress(buffer, [])
        defer { CVPixelBufferUnlockBaseAddress(buffer, []) }
        guard let ctx = CGContext(data: CVPixelBufferGetBaseAddress(buffer), width: Int(pixels.width), height: Int(pixels.height),
                                  bitsPerComponent: 8, bytesPerRow: CVPixelBufferGetBytesPerRow(buffer),
                                  space: CGColorSpace(name: CGColorSpace.sRGB)!,
                                  bitmapInfo: CGImageAlphaInfo.premultipliedFirst.rawValue | CGBitmapInfo.byteOrder32Little.rawValue) else { return }
        let full = CGRect(origin: .zero, size: pixels)
        ctx.setFillColor(CGColor(gray: 0.08, alpha: 1))
        ctx.fill(full)
        ctx.draw(image, in: full)

        // Clicks: a ring that grows and fades over half a second.
        let sx = pixels.width / bounds.width, sy = pixels.height / bounds.height
        let now = CACurrentMediaTime()
        clicks.removeAll { now - $0.1 > 0.5 }
        for (p, at) in clicks {
            let f = (now - at) / 0.5
            let r = (10 + 18 * f) * sx
            ctx.setStrokeColor(CGColor(srgbRed: 0.22, green: 1, blue: 0.42, alpha: 0.9 * (1 - f)))
            ctx.setLineWidth(3 * sx)
            ctx.strokeEllipse(in: CGRect(x: p.x * sx - r, y: p.y * sy - r, width: 2 * r, height: 2 * r))
        }
        // The pointer, as it is.
        let m = window.mouseLocationOutsideOfEventStream
        if bounds.contains(m), let cursor = NSCursor.currentSystem,
           let img = cursor.image.cgImage(forProposedRect: nil, context: nil, hints: nil) {
            let sz = cursor.image.size
            let hot = cursor.hotSpot
            ctx.draw(img, in: CGRect(x: (m.x - hot.x) * sx, y: (m.y - (sz.height - hot.y)) * sy, width: sz.width * sx, height: sz.height * sy))
        }
        adaptor.append(buffer, withPresentationTime: CMTime(seconds: t, preferredTimescale: 600))
    }

    /// The floating control as a picture (the feedback self-test looks at it).
    func hudPNG() -> Data? {
        guard let v = hud?.contentView, let rep = v.bitmapImageRepForCachingDisplay(in: v.bounds) else { return nil }
        v.cacheDisplay(in: v.bounds, to: rep)
        return rep.representation(using: .png, properties: [:])
    }

    // MARK: The control, over the window (not in the recording)

    private func showHUD() {
        let p = NSPanel(contentRect: NSRect(x: 0, y: 0, width: 300, height: 48), styleMask: [.borderless, .nonactivatingPanel],
                        backing: .buffered, defer: false)
        p.isOpaque = false
        p.backgroundColor = .clear
        p.hasShadow = true
        p.level = .floating
        p.isMovableByWindowBackground = true
        p.collectionBehavior = [.fullScreenAuxiliary, .moveToActiveSpace]
        let host = NSHostingView(rootView: RecordingHUD(recorder: self))
        host.frame = p.contentRect(forFrameRect: p.frame)
        p.contentView = host
        let wf = window.frame
        p.setFrameOrigin(NSPoint(x: wf.midX - 150, y: wf.maxY - 52 - 62))
        p.orderFront(nil)
        hud = p
    }
}

/// Recording, the time, Stop (keep it) and a cancel.
private struct RecordingHUD: View {
    @ObservedObject var recorder: FeedbackRecorder
    @State private var pulse = false

    private func clock(_ t: Double) -> String { String(format: "%d:%02d", Int(t) / 60, Int(t) % 60) }

    var body: some View {
        HStack(spacing: 12) {
            Circle().fill(Color.red).frame(width: 10, height: 10)
                .opacity(pulse ? 0.35 : 1)
                .animation(.easeInOut(duration: 0.8).repeatForever(autoreverses: true), value: pulse)
                .onAppear { pulse = true }
            VStack(alignment: .leading, spacing: 0) {
                Text("Recording CedarLogic").font(.system(size: 12, weight: .semibold))
                Text("\(clock(recorder.elapsed)) of \(clock(FeedbackRecorder.maxSeconds))")
                    .font(.system(size: 11).monospacedDigit()).foregroundStyle(.secondary)
            }
            Spacer(minLength: 4)
            Button { recorder.cancel() } label: { Image(systemName: "xmark").font(.system(size: 11, weight: .bold)) }
                .buttonStyle(.plain).foregroundStyle(.secondary).help("Throw it away")
            Button { recorder.stop() } label: {
                Text("Stop").font(.system(size: 12, weight: .semibold))
                    .padding(.horizontal, 12).padding(.vertical, 5)
                    .background(Capsule().fill(Color.red))
                    .foregroundStyle(.white)
            }
            .buttonStyle(.plain)
            .keyboardShortcut(.defaultAction)
        }
        .padding(.horizontal, 14)
        .frame(width: 300, height: 48)
        .background(VisualEffect().clipShape(Capsule()))
        .overlay(Capsule().strokeBorder(Color.white.opacity(0.12)))
    }
}

private struct VisualEffect: NSViewRepresentable {
    func makeNSView(context: Context) -> NSVisualEffectView {
        let v = NSVisualEffectView()
        v.material = .hudWindow
        v.blendingMode = .behindWindow
        v.state = .active
        return v
    }
    func updateNSView(_ v: NSVisualEffectView, context: Context) {}
}
