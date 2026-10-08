// File > Share Link…, and cedarlogic://open links coming in. The format is
// ShareLinkCodec.swift's. cedarlogic://sync#k=… links (a sync code, SYNC.md
// 5.3) go to Settings > Sync, which previews the code and asks first.

import SwiftUI

@MainActor
enum ShareLink {
    /// A link was handed over (before the launch's own reopening of the last
    /// circuit, which it would only crowd).
    static var handedOver = false

    /// Copies the circuit's link: the website's, which opens in the browser
    /// (CedarLogic Online) and can offer the app. Too long for a link says so.
    /// The notes never go in a link (they're the student's own, and links get
    /// pasted into chats); the drawing goes when it's shown and the link
    /// still fits (the website's DRAWING-NOTES.md 4.11).
    static func copy(from canvas: CanvasController) {
        guard let document = canvas.document else { return }
        guard document.hasGates else { canvas.note("There's nothing on the circuit to share yet."); return }
        func make(_ flags: Int32) -> String? {
            ShareCodec.encode(document.shareText(flags: flags))
                .map { ShareCodec.webBase + "#" + ShareCodec.fragment(data: $0, name: canvas.documentTitle) }
        }
        let withInk = document.hasInk && document.inkShown
        let plain = Int32(CL_SAVE_NO_NOTES | CL_SAVE_NO_INK)
        guard var link = make(withInk ? Int32(CL_SAVE_NO_NOTES) : plain) else { canvas.note("Couldn't make a link for this circuit."); return }
        var leftOutDrawing = false
        if withInk && link.count > ShareCodec.maxWebLink, let without = make(plain) {
            link = without
            leftOutDrawing = true
        }
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
        if leftOutDrawing {
            canvas.note("Link copied. The link leaves out the drawing; it's too big for a link. Send the file to include it.")
        } else {
            canvas.note("Link copied. Anyone who opens it gets this circuit; it isn't stored anywhere.")
        }
    }

    /// cedarlogic://open#c=<data>: the circuit comes in as a new one in Your
    /// Circuits, as File > Import does.
    static func open(_ url: URL) {
        guard url.scheme?.lowercased() == "cedarlogic" else { return }
        if url.host?.lowercased() == "classroom" {
            // cedarlogic://classroom#t= / #j= / #m=: the matching sheet with the code filled in, never more (§5.5).
            guard ClassroomFlag.on, let frag = url.fragment, frag.count > 2 else { return }
            let code = String(frag.dropFirst(2))
            let c = ClassroomCenter.shared
            switch frag.prefix(2) {
            case "t=": c.sheet = .addTeacherKey(prefill: code)
            case "j=": c.sheet = .join(prefill: code)
            case "m=": c.sheet = .moveIn(prefill: code)
            case "s=": c.sheet = .passIn(prefill: code)      // a class pass (3.17)
            default: return
            }
            AppActions.openWindow?(id: "classroom")
            return
        }
        if url.host?.lowercased() == "sync" {
            SyncCenter.shared.openLink(url.absoluteString)
            return
        }
        guard let link = ShareCodec.parse(url.absoluteString) else { return }
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
