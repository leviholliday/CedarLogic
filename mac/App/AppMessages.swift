// A message from CedarLogic (CedarCore/include/CedarMessage.h): the first is a thank-you a year
// after 0.4, with a link to a short form. After Sparkle's daily background check finds no update
// (so only while automatic update checks are on, and never on top of an update), the app reads
// message.json from the releases repo, next to the update feeds, and the engine says whether a
// message is due today. It's shown in its own window, in the brand look (MessageView), with three
// answers: the form (done with), Remind Me Later (again in a few days; closing the window is the
// same) and No Thanks (done with). If a sheet or a dialog is up, it waits for the next day's
// check. Nothing is sent: it's one plain download.

import AppKit
import SwiftUI

@MainActor
enum AppMessages {
    /// What's kept of the answers (cl_message_remember's line).
    static let shownKey = "shownMessages"
    private static var looking = false
    private static var window: NSWindow?

    /// CL_MESSAGE_URL: another file, for a test copy of the app only (its own bundle id).
    nonisolated static var testURL: URL? {
        guard Bundle.main.bundleIdentifier != LaunchSelfTest.realBundle,
              let s = ProcessInfo.processInfo.environment["CL_MESSAGE_URL"], !s.isEmpty else { return nil }
        return URL(string: s)
    }
    nonisolated static var url: URL { testURL ?? URL(string: CL_MESSAGE_URL)! }

    /// A test copy given CL_MESSAGE_URL looks a few seconds after launch, without waiting for an
    /// update check (a file:// address works too); mac/Tools/message-try.sh sets one up.
    static func checkSoonIfTesting() {
        guard testURL != nil else { return }
        DispatchQueue.main.asyncAfter(deadline: .now() + 4) { check() }
    }

    static func check() {
        guard !looking, window == nil else { return }
        looking = true
        var req = URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: 15)
        req.setValue("CedarLogic", forHTTPHeaderField: "User-Agent")
        let session = URLSession(configuration: .ephemeral)
        session.dataTask(with: req) { data, response, _ in
            let ok = (response as? HTTPURLResponse)?.statusCode == 200 || (response?.url?.isFileURL == true && data != nil)
            let body = ok ? data : nil
            DispatchQueue.main.async {
                looking = false
                if let body { decide(body) }
            }
        }.resume()
        session.finishTasksAndInvalidate()
    }

    private static func decide(_ data: Data) {
        let shown = UserDefaults.standard.string(forKey: shownKey) ?? ""
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? ""
        guard let p = data.withUnsafeBytes({ raw in
            cl_message_pick(raw.baseAddress?.assumingMemoryBound(to: CChar.self), data.count, today(), "mac", version, shown)
        }) else { return }
        defer { cl_message_free(p) }
        let m = p.pointee
        guard let link = URL(string: String(cString: m.link)) else { return }
        let message = MessageView.Message(id: String(cString: m.id), eyebrow: String(cString: m.eyebrow), title: String(cString: m.title),
                                          text: String(cString: m.text), note: String(cString: m.note), button: String(cString: m.button),
                                          link: link, later: Int(m.later))
        // Not over something the person is in the middle of: the next check will do.
        guard NSApp.modalWindow == nil, NSApp.windows.allSatisfy({ $0.attachedSheet == nil }) else { return }
        show(message)
    }

    enum Answer { case open, later, noThanks }

    private static func show(_ m: MessageView.Message) {
        var answered = false
        let answer: (Answer) -> Void = { a in
            guard !answered else { return }
            answered = true
            remember(m, a)
            if a == .open { NSWorkspace.shared.open(m.link) }
            close()
        }
        let host = NSHostingView(rootView: MessageView(message: m, answer: answer))
        let w = MessageWindow(contentRect: NSRect(origin: .zero, size: MessageView.size),
                              styleMask: [.titled, .closable, .fullSizeContentView], backing: .buffered, defer: false)
        w.titlebarAppearsTransparent = true
        w.titleVisibility = .hidden
        w.title = m.title
        w.isMovableByWindowBackground = true
        w.isReleasedWhenClosed = false
        w.appearance = NSAppearance(named: .darkAqua)
        w.backgroundColor = NSColor(Brand.ink)
        w.contentView = host
        w.onClose = { answer(.later) }   // the red button: Remind Me Later
        w.center()
        w.alphaValue = 0
        window = w
        w.makeKeyAndOrderFront(nil)
        NSAnimationContext.runAnimationGroup { $0.duration = 0.35; w.animator().alphaValue = 1 }
    }

    private static func close() {
        guard let w = window else { return }
        window = nil
        (w as? MessageWindow)?.onClose = nil
        NSAnimationContext.runAnimationGroup({ $0.duration = 0.2; w.animator().alphaValue = 0 }, completionHandler: { w.orderOut(nil); w.close() })
    }

    private static func remember(_ m: MessageView.Message, _ a: Answer) {
        let shown = UserDefaults.standard.string(forKey: shownKey) ?? ""
        guard let kept = cl_message_remember(shown, m.id, today(), a == .later ? Int32(m.later) : 0) else { return }
        UserDefaults.standard.set(String(cString: kept), forKey: shownKey)
        free(kept)
    }

    /// The computer's own date, as the engine reads it.
    private static func today() -> String {
        let f = DateFormatter()
        f.locale = Locale(identifier: "en_US_POSIX")
        f.calendar = Calendar(identifier: .gregorian)
        f.timeZone = .current
        f.dateFormat = "yyyy-MM-dd"
        return f.string(from: Date())
    }
}

/// The message's window: closing it is Remind Me Later. (Return is the form and Escape Remind Me
/// Later: the buttons' keyboard shortcuts.)
private final class MessageWindow: NSWindow {
    var onClose: (() -> Void)?
    override var canBecomeKey: Bool { true }
    override func performClose(_ sender: Any?) {
        if let c = onClose { c() } else { super.performClose(sender) }
    }
}

// MARK: - The look

/// The message in the brand look: the launch screen's ground, a wire that lights up and runs a
/// signal into the icon (with its year on a badge), the words, and the three answers.
struct MessageView: View {
    struct Message {
        var id, eyebrow, title, text, note, button: String
        var link: URL
        var later: Int
    }
    static let size = CGSize(width: 760, height: 460)

    let message: Message
    let answer: (AppMessages.Answer) -> Void
    /// False draws it settled (the --render-ui pictures).
    let animate: Bool
    @State private var shown: Bool
    @State private var start = Date()

    init(message: Message, answer: @escaping (AppMessages.Answer) -> Void = { _ in }, animate: Bool = true) {
        self.message = message
        self.answer = answer
        self.animate = animate
        _shown = State(initialValue: !animate)
    }

    /// The one-year message, for the --render-ui pictures.
    static let preview = Message(
        id: "one-year-2027", eyebrow: "One year of CedarLogic", title: "It's been a whole year!",
        text: "Thank you so much for using CedarLogic! It really means a lot. If you have a couple of minutes, I'd love to hear how it's going and whether there's anything else you're looking for.",
        note: "Six quick questions, all optional. Opens in your browser.", button: "Fill Out the Form",
        link: URL(string: "https://cedarlogic.netlify.app/one-year/")!, later: 3)

    var body: some View {
        VStack(spacing: 0) {
            HStack(alignment: .center, spacing: 0) {
                words.frame(maxWidth: 430, alignment: .leading)
                Spacer(minLength: 10)
                art.frame(width: 260, height: 300)
            }
            .padding(.leading, 52).padding(.trailing, 36).padding(.top, 34)
            .frame(maxHeight: .infinity)
            Rectangle().fill(Color.white.opacity(0.07)).frame(height: 1)
            buttons.padding(.horizontal, 32).frame(height: 76)
        }
        .frame(width: Self.size.width, height: Self.size.height)
        .background(BrandBackground(bloom: .init(x: 0.78, y: 0.42)))
        .preferredColorScheme(.dark)
        .tint(Brand.neon)
        .onAppear {
            start = Date()
            if animate { shown = true }
        }
    }

    // Each line rises into place, one after another.
    private func rise(_ i: Double) -> some ViewModifier { Rise(on: shown, delay: animate ? i * 0.07 : 0) }

    private var words: some View {
        VStack(alignment: .leading, spacing: 0) {
            if !message.eyebrow.isEmpty {
                HStack(spacing: 8) {
                    Capsule().fill(LinearGradient(colors: [Brand.neon.opacity(0), Brand.neon], startPoint: .leading, endPoint: .trailing))
                        .frame(width: 22, height: 2).shadow(color: Brand.neon, radius: 4)
                    Text(message.eyebrow.uppercased()).font(.system(size: 11, weight: .bold)).kerning(1.8).foregroundStyle(Brand.neon)
                }
                .modifier(rise(0))
            }
            Text(message.title)
                .font(.system(size: 36, weight: .bold)).tracking(-0.6)
                .foregroundStyle(Brand.silver)
                .shadow(color: Brand.neon.opacity(0.28), radius: 12)
                .fixedSize(horizontal: false, vertical: true)
                .padding(.top, 14)
                .modifier(rise(1))
            Text(message.text)
                .font(.system(size: 15)).lineSpacing(3.5)
                .foregroundStyle(BrandText.secondary)
                .fixedSize(horizontal: false, vertical: true)
                .padding(.top, 14)
                .modifier(rise(2))
            if !message.note.isEmpty {
                HStack(spacing: 7) {
                    Image(systemName: "clock").font(.system(size: 11, weight: .semibold)).foregroundStyle(Brand.neon.opacity(0.85))
                    Text(message.note).font(.system(size: 12)).foregroundStyle(BrandText.faint)
                }
                .padding(.top, 18)
                .modifier(rise(3))
            }
        }
    }

    private var buttons: some View {
        HStack(spacing: 10) {
            Button("No Thanks") { answer(.noThanks) }
                .buttonStyle(.plain).font(.system(size: 13, weight: .medium)).foregroundStyle(BrandText.faint)
                .padding(.horizontal, 10).frame(minHeight: 36).contentShape(Rectangle())
                .help("Don't show this again")
            Spacer()
            Button("Remind Me Later") { answer(.later) }
                .buttonStyle(BrandButtonStyle(primary: false, width: 150))
                .keyboardShortcut(.cancelAction)
                .help(message.later == 1 ? "Ask again tomorrow" : "Ask again in \(message.later) days")
            Button { answer(.open) } label: {
                HStack(spacing: 7) {
                    Text(message.button)
                    Image(systemName: "arrow.up.right").font(.system(size: 11, weight: .bold))
                }
            }
            .buttonStyle(BrandButtonStyle(width: 190))
            .keyboardShortcut(.defaultAction)
        }
        .opacity(shown ? 1 : 0)
        .animation(animate ? .easeOut(duration: 0.5).delay(0.35) : nil, value: shown)
    }

    // MARK: The art

    /// The icon, floating in its bloom, a "1 YEAR" badge on its corner, and a wire from the bottom
    /// left that lights up and keeps sending a pulse into it, with a few sparks drifting up.
    private var art: some View {
        TimelineView(.animation(minimumInterval: 1.0 / 60, paused: !animate)) { tl in
            let t = animate ? tl.date.timeIntervalSince(start) : 2.4
            let lit = min(1, max(0, (t - 0.25) / 0.9))          // the wire, drawn on
            let pop = min(1, max(0, (t - 1.0) / 0.5))           // the badge
            ZStack {
                Circle().fill(RadialGradient(colors: [Brand.neon.opacity(0.26 + 0.06 * lit), .clear], center: .center, startRadius: 8, endRadius: 150))
                    .frame(width: 300, height: 300)
                    .scaleEffect(1 + 0.04 * sin(t * 1.4))
                Wire(progress: lit, pulse: t > 1.15 ? (t - 1.15).truncatingRemainder(dividingBy: 2.2) / 2.2 : -1)
                Sparks(t: t).opacity(lit)
                ZStack(alignment: .topTrailing) {
                    Image(nsImage: Brand.icon).resizable().interpolation(.high)
                        .frame(width: 156, height: 156)
                        .clipShape(RoundedRectangle(cornerRadius: 156 * 0.2237, style: .continuous))
                        .shadow(color: Brand.neon.opacity(0.25 + 0.25 * lit), radius: 22)
                        .shadow(color: .black.opacity(0.6), radius: 16, y: 12)
                    Text("1 YEAR")
                        .font(.system(size: 11, weight: .heavy, design: .rounded)).kerning(0.8)
                        .foregroundStyle(Brand.ink)
                        .padding(.horizontal, 9).frame(height: 24)
                        .background(Capsule().fill(LinearGradient(colors: [Brand.neon, Brand.neonDeep], startPoint: .top, endPoint: .bottom)))
                        .overlay(Capsule().strokeBorder(Color.white.opacity(0.45), lineWidth: 1))
                        .shadow(color: Brand.neon.opacity(0.7), radius: 10)
                        .offset(x: 16, y: -10)
                        .scaleEffect(0.4 + 0.6 * spring(pop), anchor: .center)
                        .opacity(pop)
                }
                .offset(y: 5 * sin(t * 1.1))
                .rotation3DEffect(.degrees(4 * sin(t * 0.7)), axis: (x: 0, y: 1, z: 0))
            }
        }
    }

    /// An overshoot, for the badge.
    private func spring(_ x: Double) -> Double { x >= 1 ? 1 : 1 - pow(1 - x, 3) * cos(x * 4.2) }
}

private struct Rise: ViewModifier {
    let on: Bool
    let delay: Double
    func body(content: Content) -> some View {
        content.opacity(on ? 1 : 0).offset(y: on ? 0 : 14).blur(radius: on ? 0 : 4)
            .animation(.timingCurve(0.16, 1, 0.3, 1, duration: 0.8).delay(delay), value: on)
    }
}

/// The wire: in from the bottom left, a turn, up into the icon. `progress` draws it on; `pulse`
/// (0 to 1, or below 0 for none) is the signal on its way.
private struct Wire: View {
    let progress: Double
    let pulse: Double

    static func path(in r: CGRect) -> Path {
        var p = Path()
        p.move(to: CGPoint(x: r.minX - 40, y: r.maxY - 26))
        p.addLine(to: CGPoint(x: r.midX - 54, y: r.maxY - 26))
        p.addQuadCurve(to: CGPoint(x: r.midX - 30, y: r.maxY - 50), control: CGPoint(x: r.midX - 30, y: r.maxY - 26))
        p.addLine(to: CGPoint(x: r.midX - 30, y: r.midY + 60))
        return p
    }

    var body: some View {
        GeometryReader { g in
            let r = CGRect(origin: .zero, size: g.size)
            let path = Self.path(in: r)
            ZStack {
                path.stroke(Color.white.opacity(0.08), style: StrokeStyle(lineWidth: 3, lineCap: .round, lineJoin: .round))
                path.trimmedPath(from: 0, to: progress)
                    .stroke(Brand.neon, style: StrokeStyle(lineWidth: 2.6, lineCap: .round, lineJoin: .round))
                    .shadow(color: Brand.neon.opacity(0.9), radius: 6)
                if pulse >= 0, let at = path.trimmedPath(from: 0, to: max(0.001, pulse)).currentPoint {
                    Circle().fill(Color.white).frame(width: 7, height: 7)
                        .shadow(color: Brand.neon, radius: 8).shadow(color: Brand.neon, radius: 3)
                        .position(at)
                        .opacity(sin(.pi * pulse))
                }
                // The pin where it meets the icon.
                Circle().fill(progress >= 1 ? Brand.neon : Color.white.opacity(0.15)).frame(width: 9, height: 9)
                    .shadow(color: progress >= 1 ? Brand.neon : .clear, radius: 8)
                    .position(CGPoint(x: r.midX - 30, y: r.midY + 60))
            }
        }
    }
}

/// A few sparks drifting up around the icon.
private struct Sparks: View {
    let t: Double
    private static let seeds: [(x: Double, speed: Double, phase: Double, size: Double)] = [
        (0.18, 0.11, 0.0, 4), (0.82, 0.09, 0.35, 3), (0.3, 0.13, 0.62, 2.5), (0.7, 0.1, 0.15, 3.5), (0.5, 0.08, 0.8, 2.5), (0.9, 0.12, 0.5, 2),
    ]
    var body: some View {
        GeometryReader { g in
            ForEach(Array(Self.seeds.enumerated()), id: \.offset) { _, s in
                let k = (t * s.speed + s.phase).truncatingRemainder(dividingBy: 1)
                Circle().fill(s.size > 3 ? Brand.neon : Color.white)
                    .frame(width: s.size, height: s.size)
                    .shadow(color: Brand.neon, radius: 4)
                    .position(x: g.size.width * (s.x + 0.04 * sin(t * 1.3 + s.phase * 6)), y: g.size.height * (1 - k))
                    .opacity(0.8 * sin(.pi * k))
            }
        }
    }
}
