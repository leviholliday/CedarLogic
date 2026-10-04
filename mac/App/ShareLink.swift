// File > Share Link…, and cedarlogic://open links coming in. The format is
// ShareLinkCodec.swift's.

import SwiftUI

@MainActor
enum ShareLink {
    /// A link was handed over (before the launch's own reopening of the last
    /// circuit, which it would only crowd).
    static var handedOver = false

    /// Copies the circuit's link: the website's, which opens in the browser
    /// (CedarLogic Online) and can offer the app. Too long for a link says so.
    static func copy(from canvas: CanvasController) {
        guard let document = canvas.document else { return }
        guard document.hasGates else { canvas.note("There's nothing on the circuit to share yet."); return }
        guard let data = ShareCodec.encode(document.saveText()) else { canvas.note("Couldn't make a link for this circuit."); return }
        let link = ShareCodec.webBase + "#" + ShareCodec.fragment(data: data, name: canvas.documentTitle)
        if link.count > ShareCodec.maxWebLink {
            let alert = NSAlert()
            alert.messageText = "This circuit is too big for a link"
            alert.informativeText = "A link holds the whole circuit, and this one would be about \(link.count / 1024) KB. Use File \u{25B8} Export as CedarLogic File… and send the file instead."
            alert.addButton(withTitle: "OK")
            alert.runModal()
            return
        }
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(link, forType: .string)
        canvas.note("Link copied. Anyone who opens it gets this circuit; it isn't stored anywhere.")
    }

    /// cedarlogic://open#c=<data>: the circuit comes in as a new one in Your
    /// Circuits, as File > Import does.
    static func open(_ url: URL) {
        guard url.scheme?.lowercased() == "cedarlogic", let link = ShareCodec.parse(url.absoluteString) else { return }
        do {
            let text = try ShareCodec.decode(link.data)
            guard text.contains("circuit") || text.contains("cedarlogic") else { throw ShareCodec.Failure(reason: "not a circuit") }
            var name = link.name.trimmingCharacters(in: .whitespacesAndNewlines)
            name = name.replacingOccurrences(of: "/", with: "-").replacingOccurrences(of: ":", with: "-")
            if name.isEmpty { name = "Shared circuit" }
            let dir = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
            try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
            let file = dir.appendingPathComponent("\(name).cdl")
            try text.write(to: file, atomically: true, encoding: .utf8)
            handedOver = true
            Library.open(file)
        } catch {
            let alert = NSAlert()
            alert.messageText = "That link couldn't be opened"
            alert.informativeText = "It isn't a CedarLogic circuit link, or it was cut short when it was copied."
            alert.addButton(withTitle: "OK")
            alert.runModal()
        }
    }

    /// Registers for the cedarlogic:// links (the Info.plist's URL type).
    static func install() {
        NSAppleEventManager.shared().setEventHandler(Handler.shared, andSelector: #selector(Handler.handle(_:reply:)),
                                                     forEventClass: AEEventClass(kInternetEventClass), andEventID: AEEventID(kAEGetURL))
    }

    private final class Handler: NSObject {
        static let shared = Handler()
        @objc func handle(_ event: NSAppleEventDescriptor, reply: NSAppleEventDescriptor) {
            guard let s = event.paramDescriptor(forKeyword: keyDirectObject)?.stringValue, let url = URL(string: s) else { return }
            DispatchQueue.main.async { MainActor.assumeIsolated { ShareLink.open(url) } }
        }
    }
}
