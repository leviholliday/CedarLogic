// Settings > Sync (SYNC.md 5.1): turning sync on and showing the code (with
// its QR code), linking with a code (checked as it's typed, then a preview of
// what the code holds, then a confirmation), and while it's on: the status,
// this Mac's name, the devices, the code again, Turn Off, Start Over, Delete
// Synced Copy. The work is SyncCenter's (Sync.swift).

import AppKit
import SwiftUI

struct SyncSettingsView: View {
    @ObservedObject var center: SyncCenter
    @State private var name = ""
    @FocusState private var nameFocused: Bool

    /// What changes the page's height: the window follows it.
    private var layoutKey: String {
        "\(center.enabled)|\(center.kind)|\(center.devices.count)|\(center.pageMessage ?? "")|\(center.working ?? "")"
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            if center.enabled { onPage } else { offPage }
            if let message = center.pageMessage {
                SyncRow("") {
                    Text(message).foregroundStyle(.red).fixedSize(horizontal: false, vertical: true)
                }
            }
        }
        .onAppear { name = center.deviceName; center.refresh() }
        .onChange(of: center.deviceName) { _, n in if !nameFocused { name = n } }
        .onChange(of: nameFocused) { _, focused in if !focused { center.setDeviceName(name) } }
        .onChange(of: layoutKey) { _, _ in DispatchQueue.main.async { PrefsWindow.shared.refit() } }
        .sheet(item: $center.sheet) { sheet in
            switch sheet {
            case .code: SyncCodeSheet(center: center)
            case .enter: SyncEnterCodeSheet(center: center)
            case .confirm(let code, let sentence, let empty): SyncConfirmSheet(center: center, code: code, sentence: sentence, empty: empty)
            }
        }
    }

    // MARK: Off

    /// Stopped from another device (or the copy expired): said above the way back in.
    private var showsGone: Bool { center.kind == Int(CL_SYNC_GONE) && !center.statusText.isEmpty }

    private var offPage: some View {
        VStack(alignment: .leading, spacing: 14) {
            if showsGone {
                SyncRow("Sync") {
                    Label { Text(center.statusText).fixedSize(horizontal: false, vertical: true) } icon: {
                        Image(systemName: "xmark.circle").foregroundStyle(.secondary)
                    }
                    .frame(maxWidth: 400, alignment: .leading)
                }
            }
            SyncRow(showsGone ? "" : "Sync") {
                VStack(alignment: .leading, spacing: 10) {
                    Text("Sync Your Circuits").font(.system(size: 13, weight: .semibold))
                    Text("Keep Your Circuits the same on this Mac, your other computers, your phone and CedarLogic Online. There\u{2019}s no account: a secret code links your devices, and circuits are encrypted on this Mac before they\u{2019}re sent, so only your devices can read them.")
                        .fixedSize(horizontal: false, vertical: true)
                    HStack(spacing: 8) {
                        Button("Turn On Sync") { center.turnOn() }.disabled(center.working != nil)
                        Button("I Have a Code\u{2026}") { center.haveCode() }.disabled(center.working != nil)
                        if let working = center.working { WorkingLabel(text: working) }
                    }
                    Text("Free. Up to 1,000 circuits. If none of your devices syncs for a year, the synced copy is removed (your devices keep theirs).")
                        .font(.system(size: 11)).foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
                .frame(maxWidth: 420, alignment: .leading)
            }
        }
    }

    // MARK: On

    private var onPage: some View {
        VStack(alignment: .leading, spacing: 14) {
            SyncRow("Sync") {
                HStack(spacing: 8) {
                    Image(systemName: center.symbol).foregroundStyle(.secondary)
                    Text(statusLine).lineLimit(2).fixedSize(horizontal: false, vertical: true)
                    Spacer(minLength: 8)
                    Button("Sync Now") { center.syncNow() }
                }
                .frame(maxWidth: 420, alignment: .leading)
            }
            SyncRow("This Mac\u{2019}s name", hint: "Shown on your other devices when a circuit comes from here.") {
                TextField("Name", text: $name)
                    .focused($nameFocused)
                    .onSubmit { center.setDeviceName(name) }
                    .frame(width: 240)
            }
            SyncRow("Devices", hint: "Don\u{2019}t recognise one? Start over with a new code.") {
                VStack(alignment: .leading, spacing: 3) {
                    ForEach(center.devices) { d in
                        Text(deviceLine(d)).foregroundStyle(d.isThis ? .primary : .secondary).lineLimit(1)
                    }
                }
            }
            SyncRow("Sync code") {
                HStack(spacing: 10) {
                    Text(masked).font(.system(size: 12, design: .monospaced)).foregroundStyle(.secondary)
                    Button("Show Code") { center.sheet = .code }
                }
            }
            SyncRow("") {
                VStack(alignment: .leading, spacing: 8) {
                    HStack(spacing: 8) {
                        Button("Turn Off Sync\u{2026}") { center.turnOff() }
                        Button("Delete Synced Copy\u{2026}") { center.deleteSyncedCopy() }
                    }
                    Button("Start Over with a New Code\u{2026}") { center.startOver() }
                }
                .disabled(center.working != nil)
                if let working = center.working { WorkingLabel(text: working) }
            }
        }
    }

    private var statusLine: String {
        center.kind == Int(CL_SYNC_SYNCED) && center.circuits > 0
            ? "\(center.statusText) · \(center.circuits) circuit\(center.circuits == 1 ? "" : "s")" : center.statusText
    }

    private var masked: String {
        let last = center.code.count == 28 ? String(center.code.suffix(4)) : "••••"
        return Array(repeating: "••••", count: 6).joined(separator: "-") + "-" + last
    }

    private func deviceLine(_ d: SyncCenter.Device) -> String {
        if d.isThis { return "\(d.name) (this Mac)" }
        guard let t = d.lastSync else { return d.name }
        return "\(d.name), synced \(Self.ago(t))"
    }

    /// "just now", "5 min ago", "2 days ago", "3 months ago".
    static func ago(_ t: Date) -> String {
        let days = Int(Date().timeIntervalSince(t) / 86400)
        if days < 1 { return agoText(t) }
        if days == 1 { return "yesterday" }
        if days < 45 { return "\(days) days ago" }
        if days < 365 { return "\(Int((Double(days) / 30.4).rounded())) months ago" }
        let years = days / 365
        return years == 1 ? "a year ago" : "\(years) years ago"
    }
}

/// "Label:" on the left, the control and a line of help on the right (as Settings' other pages).
struct SyncRow<Control: View>: View {
    let label: String
    let hint: String?
    @ViewBuilder let control: () -> Control

    init(_ label: String, hint: String? = nil, @ViewBuilder control: @escaping () -> Control) {
        self.label = label
        self.hint = hint
        self.control = control
    }

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: 12) {
            Text(label.isEmpty ? "" : label + ":")
                .frame(width: 150, alignment: .trailing)
            VStack(alignment: .leading, spacing: 4) {
                control()
                if let hint {
                    Text(hint).font(.system(size: 11)).foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                        .frame(maxWidth: 360, alignment: .leading)
                }
            }
            Spacer(minLength: 0)
        }
    }
}

private struct WorkingLabel: View {
    let text: String
    var body: some View {
        HStack(spacing: 6) {
            ProgressView().controlSize(.small)
            Text(text).foregroundStyle(.secondary)
        }
    }
}

// MARK: - The code

/// The QR code of a sync link (1.2): always dark modules on white with the
/// four-module quiet zone, drawn module for module.
struct SyncQRCode: View {
    let text: String
    var body: some View {
        if let image = Self.image(text) {
            Image(decorative: image, scale: 1).resizable().interpolation(.none)
                .aspectRatio(1, contentMode: .fit)
        } else {
            Color.white
        }
    }

    static func image(_ text: String) -> CGImage? {
        var modules = [UInt8](repeating: 0, count: 177 * 177)
        let size = Int(cl_sync_qr(text, &modules))
        guard size > 0 else { return nil }
        let quiet = 4, k = 8, side = (size + 2 * quiet) * k
        var pixels = [UInt8](repeating: 255, count: side * side)
        for y in 0..<size {
            for x in 0..<size where modules[y * size + x] != 0 {
                for dy in 0..<k {
                    let row = ((y + quiet) * k + dy) * side + (x + quiet) * k
                    for dx in 0..<k { pixels[row + dx] = 0 }
                }
            }
        }
        guard let provider = CGDataProvider(data: Data(pixels) as CFData) else { return nil }
        return CGImage(width: side, height: side, bitsPerComponent: 8, bitsPerPixel: 8, bytesPerRow: side,
                       space: CGColorSpaceCreateDeviceGray(), bitmapInfo: CGBitmapInfo(rawValue: 0), provider: provider,
                       decode: nil, shouldInterpolate: false, intent: .defaultIntent)
    }
}

struct SyncCodeSheet: View {
    @ObservedObject var center: SyncCenter
    @Environment(\.dismiss) private var dismiss
    @State private var copied: String?

    private var grouped: String {
        var out = [CChar](repeating: 0, count: 35)
        cl_sync_group_code(center.code, &out)
        return String(cString: out)
    }
    private var link: String { String(cString: cl_sync_web_link(center.code)) }

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Your sync code").font(.system(size: 17, weight: .bold))
            Text(grouped)
                .font(.system(size: 22, weight: .semibold, design: .monospaced))
                .textSelection(.enabled)
                .fixedSize()
            HStack(alignment: .top, spacing: 18) {
                SyncQRCode(text: link).frame(width: 176, height: 176)
                    .clipShape(RoundedRectangle(cornerRadius: 6))
                    .overlay(RoundedRectangle(cornerRadius: 6).strokeBorder(Color.primary.opacity(0.12)))
                VStack(alignment: .leading, spacing: 10) {
                    Text("On your other computer: Settings \u{203A} Sync \u{203A} I Have a Code. On your phone: scan this with the camera, or open cedarlogic.netlify.app/app, then Your Circuits \u{203A} Sync \u{203A} Scan Code.")
                    Text("Keep it private: anyone with this code can see and change your synced circuits. Keep a copy somewhere safe: if you lose every device that has it and this code, nobody can open the synced circuits \u{2014} not even CedarLogic\u{2019}s website.")
                        .font(.system(size: 11)).foregroundStyle(.secondary)
                }
                .fixedSize(horizontal: false, vertical: true)
            }
            HStack(spacing: 8) {
                Button(copied == "code" ? "Copied" : "Copy Code") { copy(grouped, "code") }
                Button(copied == "link" ? "Copied" : "Copy Link") { copy(link, "link") }
                Spacer()
                Button("Done") { dismiss() }.keyboardShortcut(.defaultAction)
            }
        }
        .padding(24)
        .frame(width: 560)
    }

    private func copy(_ text: String, _ what: String) {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
        copied = what
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) { if copied == what { copied = nil } }
    }
}

// MARK: - Linking

struct SyncEnterCodeSheet: View {
    @ObservedObject var center: SyncCenter
    @Environment(\.dismiss) private var dismiss
    @FocusState private var focused: Bool

    /// As it's typed: a tick once it's a whole, correct code; a wrong
    /// character or a typo (checksum) at once; "too short" only on Continue.
    private var live: (ok: Bool, problem: String?) {
        let text = center.enteredCode
        guard !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { return (false, nil) }
        switch center.whyKind(text) {
        case nil: return (true, nil)
        case "length": return (false, nil)
        default: return (false, center.parse(text).why)
        }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Link this Mac").font(.system(size: 17, weight: .bold))
            Text("Enter the sync code from your other device: Settings \u{203A} Sync \u{203A} Show Code there.")
                .fixedSize(horizontal: false, vertical: true)
            HStack(spacing: 8) {
                TextField("Type or paste the code, or a sync link", text: $center.enteredCode)
                    .font(.system(size: 14, design: .monospaced))
                    .textFieldStyle(.roundedBorder)
                    .focused($focused)
                    .onSubmit { center.continueWithCode() }
                    .onChange(of: center.enteredCode) { _, _ in center.fieldError = nil }
                Image(systemName: "checkmark.circle.fill").foregroundStyle(.green)
                    .opacity(live.ok ? 1 : 0)
                    .accessibilityLabel(live.ok ? "A valid code" : "")
            }
            if let problem = center.fieldError ?? live.problem {
                Text(problem).font(.system(size: 12)).foregroundStyle(.red).fixedSize(horizontal: false, vertical: true)
            }
            HStack(spacing: 8) {
                if let working = center.working { WorkingLabel(text: working) }
                Spacer()
                Button("Cancel") { dismiss() }.keyboardShortcut(.cancelAction)
                Button("Continue") { center.continueWithCode() }
                    .keyboardShortcut(.defaultAction)
                    .disabled(center.enteredCode.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty || center.working != nil)
            }
        }
        .padding(24)
        .frame(width: 500)
        .onAppear { focused = true }
    }
}

struct SyncConfirmSheet: View {
    @ObservedObject var center: SyncCenter
    let code: String
    let sentence: String
    let empty: Bool
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Link this Mac?").font(.system(size: 17, weight: .bold))
            // The names in it come from the code's circuits: plain text, never markup.
            Text(verbatim: sentence).fixedSize(horizontal: false, vertical: true)
            HStack(spacing: 8) {
                Spacer()
                Button("Cancel") { dismiss() }.keyboardShortcut(.cancelAction)
                Button("Link") { center.link(code) }.keyboardShortcut(.defaultAction)
            }
        }
        .padding(24)
        .frame(width: 500)
    }
}

// MARK: - Pictures

/// `CedarLogic --render-sync <dir>` (and part of --render-ui): Settings >
/// Sync, its sheets and Your Circuits' sync line, light and dark, drawn to
/// PNGs from stand-in models -- nothing is sent anywhere.
@MainActor
enum SyncRender {
    static func run(_ dir: URL) {
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let code = "000G40R40M30E209185GR38E1YZ4"
        let now = Date()
        let devices = [
            SyncCenter.Device(name: "Levi\u{2019}s MacBook Air", lastSync: now, isThis: true),
            SyncCenter.Device(name: "Safari on iPhone", lastSync: now.addingTimeInterval(-2 * 86400), isThis: false),
            SyncCenter.Device(name: "Raspberry Pi", lastSync: now.addingTimeInterval(-90 * 86400), isThis: false),
        ]
        let on = SyncCenter(previewEnabled: true, status: "Synced just now", circuits: 42, code: code,
                            deviceName: "Levi\u{2019}s MacBook Air", devices: devices)
        let off = SyncCenter(previewEnabled: false, kind: Int(CL_SYNC_OFF))
        let gone = SyncCenter(previewEnabled: false,
                              status: "Sync was turned off from another device, and the synced copy was deleted. Your circuits here are kept. If you started over with a new code, link this device again with it.",
                              kind: Int(CL_SYNC_GONE))
        let typo = SyncCenter(previewEnabled: false, kind: Int(CL_SYNC_OFF))
        typo.enteredCode = "000G-40R4-0M30-E209-185G-R38E-1YZ5"
        let good = SyncCenter(previewEnabled: false, kind: Int(CL_SYNC_OFF))
        good.enteredCode = "000g 40r4 0m30 e209 185g r38e 1yz4"
        good.working = "Checking the code\u{2026}"
        let confirm = "This code has 14 circuits from Bob\u{2019}s laptop and Chrome on Android, last changed yesterday: \u{201C}ALU\u{201D}, \u{201C}Lab 3 adder\u{201D}, \u{201C}Traffic light\u{201D}, \u{2026}. Linking adds your 23 circuits here to them. Circuits that are already the same aren't doubled. Anyone with this code can see and change all of them. Only link with a code you made yourself."
        let busy = SyncCenter(previewEnabled: true, status: "Bringing in 12 of 37 circuits\u{2026}", kind: Int(CL_SYNC_SYNCING), circuits: 12,
                              code: code, deviceName: "Levi\u{2019}s MacBook Air", devices: devices)
        for dark in [false, true] {
            let t = dark ? "dark" : "light"
            func page(_ c: SyncCenter) -> some View {
                SyncSettingsView(center: c).padding(.horizontal, 28).padding(.vertical, 22).frame(width: 640)
            }
            snap(dir, "sync-off-\(t)", page(off), width: 640, dark: dark)
            snap(dir, "sync-gone-\(t)", page(gone), width: 640, dark: dark)
            snap(dir, "sync-on-\(t)", page(on), width: 640, dark: dark)
            snap(dir, "sync-code-\(t)", SyncCodeSheet(center: on), width: 560, dark: dark)
            snap(dir, "sync-enter-typo-\(t)", SyncEnterCodeSheet(center: typo), width: 500, dark: dark)
            snap(dir, "sync-enter-checking-\(t)", SyncEnterCodeSheet(center: good), width: 500, dark: dark)
            snap(dir, "sync-confirm-\(t)", SyncConfirmSheet(center: on, code: code, sentence: confirm, empty: false), width: 500, dark: dark)
            snap(dir, "sync-library-\(t)", LibraryView(sync: busy), width: 600, height: 540, dark: dark)
            snap(dir, "sync-library-off-\(t)", LibraryView(sync: off), width: 600, height: 540, dark: dark)
        }
    }

    /// Through a real window, so text fields and buttons draw as they do on screen.
    private static func snap<V: View>(_ dir: URL, _ name: String, _ view: V, width: CGFloat, height: CGFloat? = nil, dark: Bool) {
        let prefs = Prefs.shared
        let saved = prefs.dark
        prefs.dark = dark
        defer { prefs.dark = saved }
        let host = NSHostingView(rootView: view.background(Color(nsColor: .windowBackgroundColor))
            .environment(\.colorScheme, dark ? .dark : .light))
        host.appearance = NSAppearance(named: dark ? .darkAqua : .aqua)
        let size = NSSize(width: width, height: height ?? host.fittingSize.height)
        host.frame = NSRect(origin: .zero, size: size)
        let win = NSWindow(contentRect: host.frame, styleMask: [.titled], backing: .buffered, defer: false)
        win.appearance = host.appearance
        win.contentView = host
        host.layoutSubtreeIfNeeded()
        RunLoop.main.run(until: Date().addingTimeInterval(0.3))
        guard let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(size.width * 2), pixelsHigh: Int(size.height * 2),
                                         bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                                         colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0) else { return }
        rep.size = size
        host.cacheDisplay(in: host.bounds, to: rep)
        try? rep.representation(using: .png, properties: [:])?
            .write(to: dir.appendingPathComponent(name + ".png"))
    }
}
