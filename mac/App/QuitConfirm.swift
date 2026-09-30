// Cmd-Q asks first, as Arc and Dia do: a panel in the middle of the screen,
// in the app's own look. Return quits, Escape (or Cancel) stays; "Always
// Quit" turns the question off (Settings > General > Quitting).

import AppKit
import SwiftUI

@MainActor
enum QuitConfirm {
    private static var panel: NSPanel?
    private static var dim: NSWindow?

    static func ask() {
        guard Prefs.shared.confirmQuit else { NSApp.terminate(nil); return }
        if let panel { panel.makeKeyAndOrderFront(nil); return }
        // Roomy enough for the drop and the shadow; the card moves inside it,
        // drawn by Core Animation, rather than moving the window (which
        // steps along instead of gliding).
        let pad: CGFloat = 40
        let p = QuitPanel(contentRect: NSRect(x: 0, y: 0, width: 420 + pad * 2, height: 196 + pad * 2),
                          styleMask: [.borderless, .nonactivatingPanel], backing: .buffered, defer: false)
        p.isOpaque = false
        p.backgroundColor = .clear
        p.hasShadow = false
        p.level = .modalPanel
        p.collectionBehavior = [.fullScreenAuxiliary, .moveToActiveSpace]
        p.isMovableByWindowBackground = true
        let arrival = ArrivalState()
        let host = NSHostingView(rootView: QuitArrival(state: arrival, content: QuitView(
            quit: { quitAnimated() },
            always: { Prefs.shared.confirmQuit = false; quitAnimated() },
            cancel: { close() })))
        host.frame = p.contentRect(forFrameRect: p.frame)
        p.contentView = host
        let screen = NSApp.keyWindow?.screen ?? NSScreen.main ?? NSScreen.screens[0]
        // The screen behind darkens a little; a click on it is Cancel.
        let d = DimWindow(contentRect: screen.frame, styleMask: [.borderless], backing: .buffered, defer: false)
        d.isOpaque = false
        d.backgroundColor = NSColor.black.withAlphaComponent(0.15)
        d.level = .modalPanel
        d.collectionBehavior = [.fullScreenAuxiliary, .moveToActiveSpace]
        d.alphaValue = 0
        d.onClick = { close() }
        d.setFrame(screen.frame, display: false)
        let f = screen.visibleFrame
        p.setFrameOrigin(NSPoint(x: f.midX - 210 - pad, y: f.midY - 98 - pad + f.height * 0.08))
        NSApp.activate()
        d.orderFront(nil)
        p.makeKeyAndOrderFront(nil)
        // Start the card's arrival once the window is really on screen (from
        // onAppear, the very first time, it could be missed).
        DispatchQueue.main.async { arrival.shown = true }
        NSAnimationContext.runAnimationGroup { c in
            c.duration = 0.4
            c.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
            d.animator().alphaValue = 1
        }
        panel = p
        dim = d
    }

    /// Quit: the question shrinks away, then every window fades out with
    /// the dimmed screen, then the app goes.
    private static func quitAnimated() {
        guard let p = panel else { NSApp.terminate(nil); return }
        let d = dim
        panel = nil
        dim = nil
        let windows = NSApp.windows.filter { $0.isVisible && $0 !== p && $0 !== d }
        let f = p.frame
        NSAnimationContext.runAnimationGroup({ c in
            c.duration = 0.38
            c.timingFunction = CAMediaTimingFunction(controlPoints: 0.4, 0, 0.6, 1)
            p.animator().alphaValue = 0
            p.animator().setFrame(f.insetBy(dx: f.width * 0.04, dy: f.height * 0.04), display: true)
            d?.animator().alphaValue = 0
            for w in windows { w.animator().alphaValue = 0 }
        }, completionHandler: {
            MainActor.assumeIsolated {
                NSApp.terminate(nil)
                // Still here: quitting waits on a question (a circuit to
                // save, say), or was cancelled. The windows come back.
                for w in windows { w.alphaValue = 1 }
            }
        })
    }

    /// For --render-ui.
    static var preview: some View { QuitView(quit: {}, always: {}, cancel: {}) }

    private static func close() {
        guard let p = panel else { return }
        let d = dim
        panel = nil
        dim = nil
        NSAnimationContext.runAnimationGroup({ c in
            c.duration = 0.15
            c.timingFunction = CAMediaTimingFunction(controlPoints: 0.4, 0, 1, 1)
            p.animator().alphaValue = 0
            d?.animator().alphaValue = 0
        }, completionHandler: {
            MainActor.assumeIsolated { p.orderOut(nil); d?.orderOut(nil) }
        })
    }
}

@MainActor
private final class ArrivalState: ObservableObject {
    @Published var shown = false
}

/// Like Dia: the card is solid in about 0.14 s while its drop keeps easing
/// into place for about 0.35 s.
private struct QuitArrival<Content: View>: View {
    @ObservedObject var state: ArrivalState
    let content: Content
    private var shown: Bool { state.shown }
    var body: some View {
        content
            .shadow(color: .black.opacity(0.35), radius: 22, y: 10)
            .offset(y: shown ? 0 : -20)
            .animation(.timingCurve(0.2, 0.9, 0.3, 1, duration: 0.35), value: shown)
            .opacity(shown ? 1 : 0)
            .animation(.easeOut(duration: 0.14), value: shown)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

/// The darkened screen behind the question.
private final class DimWindow: NSWindow {
    var onClick: (() -> Void)?
    override func mouseDown(with event: NSEvent) { onClick?() }
}

/// A borderless panel that still takes the keyboard.
private final class QuitPanel: NSPanel {
    override var canBecomeKey: Bool { true }
}

private struct QuitView: View {
    let quit: () -> Void
    let always: () -> Void
    let cancel: () -> Void
    @ObservedObject private var prefs = Prefs.shared

    private var dark: Bool { prefs.dark }
    private var ink: Color { dark ? Color(white: 0.94) : Color(white: 0.1) }
    private var dim: Color { dark ? Color(white: 0.64) : Color(white: 0.4) }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Image(nsImage: Brand.icon).resizable().frame(width: 38, height: 38)
                .clipShape(RoundedRectangle(cornerRadius: 38 * Brand.corner, style: .continuous))
                .shadow(color: .black.opacity(0.35), radius: 4, y: 2)
            Text("Are you sure you want to quit CedarLogic?")
                .font(.system(size: 17, weight: .semibold)).foregroundStyle(ink)
                .padding(.top, 14)
            Text("Your circuits are saved; they'll be here when you come back.")
                .font(.system(size: 12.5)).foregroundStyle(dim)
                .padding(.top, 5)
            Spacer(minLength: 16)
            HStack(spacing: 10) {
                button("Always Quit", key: nil, fill: ink.opacity(0.08), text: ink, action: always)
                    .help("Quit, and stop asking (Settings > General > Quitting)")
                Spacer()
                button("Cancel", key: "esc", fill: ink.opacity(0.08), text: ink, action: cancel)
                    .keyboardShortcut(.cancelAction)
                button("Quit", key: "↩", fill: Brand.neonDeep, text: .white, action: quit)
                    .keyboardShortcut(.defaultAction)
            }
        }
        .padding(22)
        .frame(width: 420, height: 196, alignment: .topLeading)
        .background(
            ZStack {
                VisualEffectBlur()
                (dark ? Color(.sRGB, red: 0.075, green: 0.085, blue: 0.1) : Color(.sRGB, red: 0.97, green: 0.975, blue: 0.98)).opacity(0.86)
            }
        )
        .clipShape(RoundedRectangle(cornerRadius: 18, style: .continuous))
        .overlay(RoundedRectangle(cornerRadius: 18, style: .continuous).strokeBorder(Color.white.opacity(dark ? 0.1 : 0.5)))
        .preferredColorScheme(dark ? .dark : .light)
    }

    private func button(_ title: String, key: String?, fill: Color, text: Color, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack(spacing: 7) {
                Text(title).font(.system(size: 13, weight: .semibold))
                if let key {
                    Text(key).font(.system(size: 10, weight: .semibold))
                        .padding(.horizontal, 5).padding(.vertical, 1.5)
                        .background(RoundedRectangle(cornerRadius: 4).fill(text.opacity(0.14)))
                }
            }
            .padding(.horizontal, 14).padding(.vertical, 8)
            .background(RoundedRectangle(cornerRadius: 9, style: .continuous).fill(fill))
            .foregroundStyle(text)
        }
        .buttonStyle(.plain)
    }
}

private struct VisualEffectBlur: NSViewRepresentable {
    func makeNSView(context: Context) -> NSVisualEffectView {
        let v = NSVisualEffectView()
        v.material = .hudWindow
        v.blendingMode = .behindWindow
        v.state = .active
        return v
    }
    func updateNSView(_ v: NSVisualEffectView, context: Context) {}
}
