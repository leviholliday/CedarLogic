// Cmd-Q asks first, as Arc and Dia do: a panel in the middle of the screen,
// in the app's own look. Return quits, Escape (or Cancel) stays; "Always
// Quit" turns the question off (Settings > General > Quitting).

import AppKit
import SwiftUI

@MainActor
enum QuitConfirm {
    private static var panel: NSPanel?

    static func ask() {
        guard Prefs.shared.confirmQuit else { NSApp.terminate(nil); return }
        if let panel { panel.makeKeyAndOrderFront(nil); return }
        let p = QuitPanel(contentRect: NSRect(x: 0, y: 0, width: 420, height: 196),
                          styleMask: [.borderless, .nonactivatingPanel], backing: .buffered, defer: false)
        p.isOpaque = false
        p.backgroundColor = .clear
        p.hasShadow = true
        p.level = .modalPanel
        p.collectionBehavior = [.fullScreenAuxiliary, .moveToActiveSpace]
        p.isMovableByWindowBackground = true
        let host = NSHostingView(rootView: QuitView(
            quit: { close(); NSApp.terminate(nil) },
            always: { Prefs.shared.confirmQuit = false; close(); NSApp.terminate(nil) },
            cancel: { close() }))
        host.frame = p.contentRect(forFrameRect: p.frame)
        p.contentView = host
        let screen = NSApp.keyWindow?.screen ?? NSScreen.main
        if let f = screen?.visibleFrame {
            p.setFrameOrigin(NSPoint(x: f.midX - 210, y: f.midY - 98 + f.height * 0.08))
        }
        p.alphaValue = 0
        NSApp.activate()
        p.makeKeyAndOrderFront(nil)
        NSAnimationContext.runAnimationGroup { $0.duration = 0.14; p.animator().alphaValue = 1 }
        panel = p
    }

    /// For --render-ui.
    static var preview: some View { QuitView(quit: {}, always: {}, cancel: {}) }

    private static func close() {
        guard let p = panel else { return }
        panel = nil
        NSAnimationContext.runAnimationGroup({ $0.duration = 0.1; p.animator().alphaValue = 0 }, completionHandler: {
            MainActor.assumeIsolated { p.orderOut(nil) }
        })
    }
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
