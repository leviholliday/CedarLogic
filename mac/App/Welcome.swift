// The first-run welcome and the guided tour (the wx app's Welcome.cpp). The
// welcome is five pages: what CedarLogic is, making it yours, your name,
// six keys worth knowing, and what to do first. The tour builds a working
// AND circuit with you, one step at a time, moving on as each is done.

import AppKit
import SwiftUI

// MARK: - Welcome

/// Help > Set Up CedarLogic…: opens the welcome window straight to the setup
/// page (wx: Help_SetUp -> ShowWelcome(this, true)). If it's already open,
/// opening the Window scene again wouldn't re-run onAppear, so this notifies
/// it directly.
extension Notification.Name {
    static let clStartAtSetup = Notification.Name("clStartAtSetup")
}
@MainActor
enum WelcomeRequest {
    /// Read once, by whichever welcome window appears next.
    static var startAtSetup = false
    static func setup() {
        startAtSetup = true
        NotificationCenter.default.post(name: .clStartAtSetup, object: nil)
    }
}

struct WelcomeView: View {
    @ObservedObject private var prefs = Prefs.shared
    @Environment(\.dismiss) private var dismiss
    @Environment(\.openWindow) private var openWindow
    @State private var page = 0
    @State private var forward = true
    @State private var readyChoice = 0
    @State private var pressed: Set<String> = []
    @State private var keyMonitor: Any?
    var startAtSetup = false

    init(startAtSetup: Bool = false, page: Int = 0) {
        self.startAtSetup = startAtSetup
        _page = State(initialValue: page)
    }

    private static let pages = 5
    private static let readyCount = 4

    var body: some View {
        VStack(spacing: 0) {
            ZStack(alignment: .topTrailing) {
                Group {
                    switch page {
                    case 0: intro
                    case 1: setup
                    case 2: name
                    case 3: keys
                    default: ready
                    }
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
                // The wx slide: the new page drifts 60 points in as it fades
                // up, the old one 60 points away as it fades out.
                .transition(.asymmetric(insertion: .offset(x: forward ? 60 : -60).combined(with: .opacity),
                                        removal: .offset(x: forward ? -60 : 60).combined(with: .opacity)))
                .id(page)
                if page < Self.pages - 1 {
                    Button("Skip") { finish() }.buttonStyle(.plain)
                        .font(.system(size: 12.5, weight: .medium)).foregroundStyle(BrandText.faint)
                        .padding(.horizontal, 26).padding(.top, 22)
                }
            }
            .clipped()
            Rectangle().fill(Color.white.opacity(0.07)).frame(height: 1)
            HStack {
                HStack(spacing: 6) {
                    ForEach(0..<Self.pages, id: \.self) { i in
                        Capsule().fill(i == page ? Brand.neon : Color.white.opacity(0.18))
                            .frame(width: i == page ? 22 : 8, height: 8)
                            .shadow(color: i == page ? Brand.neon.opacity(0.6) : .clear, radius: 5)
                    }
                }
                .animation(Self.slide, value: page)
                Spacer()
                if page > 0 {
                    Button("Back") { go(-1) }.buttonStyle(BrandButtonStyle(primary: false, width: 96))
                }
                if page < Self.pages - 1 {
                    Button(page == 0 ? "Get Started" : "Continue") { go(1) }.buttonStyle(BrandButtonStyle(width: 142))
                } else {
                    Button("Just Start") { finish() }.buttonStyle(BrandButtonStyle(primary: false, width: 110))
                }
            }
            .padding(.horizontal, 44).frame(height: 74)
        }
        .frame(width: 780, height: 580)
        .background(BrandBackground())
        .preferredColorScheme(.dark)
        .tint(Brand.neon)
        .onAppear {
            if startAtSetup || WelcomeRequest.startAtSetup { withAnimation { page = 1 } }
            WelcomeRequest.startAtSetup = false
            installKeys()
        }
        .onReceive(NotificationCenter.default.publisher(for: .clStartAtSetup)) { _ in
            forward = page < 1
            withAnimation { page = 1 }
        }
        .onDisappear { if let m = keyMonitor { NSEvent.removeMonitor(m); keyMonitor = nil } }
    }

    /// The slide: 0.32 s, easing out (the wx app's cubic easeOut).
    static let slide = Animation.timingCurve(0.33, 1, 0.68, 1, duration: 0.32)

    private func go(_ d: Int) {
        let to = min(Self.pages - 1, max(0, page + d))
        guard to != page else { return }
        forward = d > 0
        withAnimation(Self.slide) { page = to }
    }

    /// The wx welcome's keys: ← → between pages (not while typing a name),
    /// Return goes on (on the last page, the choice), Escape closes, ? for
    /// the shortcut list, and on the keys page each key it shows answers.
    private func installKeys() {
        guard keyMonitor == nil else { return }
        keyMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { e in
            guard e.window?.identifier?.rawValue.contains("welcome") == true else { return e }
            let typing = e.window?.firstResponder is NSTextView
            let mods = e.modifierFlags.intersection([.command, .control, .option])
            guard mods.isEmpty else { return e }
            switch e.keyCode {
            case 53: finish(); return nil                                   // Escape
            case 36, 76:                                                    // Return
                if page == Self.pages - 1 { readyAction(readyChoice)() } else { go(1) }
                return nil
            case 123 where !typing: go(-1); return nil                     // ←
            case 124 where !typing: go(1); return nil                      // →
            case 125 where page == Self.pages - 1:                         // ↓
                withAnimation(.easeOut(duration: 0.15)) { readyChoice = min(Self.readyCount - 1, readyChoice + 1) }; return nil
            case 126 where page == Self.pages - 1:                         // ↑
                withAnimation(.easeOut(duration: 0.15)) { readyChoice = max(0, readyChoice - 1) }; return nil
            default: break
            }
            guard !typing, let k = e.charactersIgnoringModifiers?.lowercased(), k.count == 1 else { return e }
            if k == "?" || (k == "/" && e.modifierFlags.contains(.shift)) { readyAction(3)(); return nil }
            if page == 3 && "acdrst".contains(k) {
                withAnimation(.easeOut(duration: 0.12)) { _ = pressed.insert(k) }
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.45) { withAnimation(.easeOut(duration: 0.3)) { _ = pressed.remove(k) } }
                return nil
            }
            return e
        }
    }

    private func label(_ s: String) -> some View {
        Text(s.uppercased()).font(.system(size: 10.5, weight: .bold)).kerning(1.2).foregroundStyle(BrandText.faint)
    }

    // Page 0: what it is.
    private var intro: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack(spacing: 16) {
                Image(nsImage: Brand.icon).resizable().interpolation(.high)
                    .frame(width: 58, height: 58)
                    .clipShape(RoundedRectangle(cornerRadius: 58 * Brand.corner, style: .continuous))
                    .shadow(color: Brand.neon.opacity(0.45), radius: 14)
                VStack(alignment: .leading, spacing: 2) {
                    Text("WELCOME TO").font(.system(size: 10.5, weight: .semibold)).kerning(2.2).foregroundStyle(Brand.dim)
                    Text("CedarLogic").font(.system(size: 30, weight: .semibold)).foregroundStyle(Brand.silver)
                        .shadow(color: Brand.neon.opacity(0.35), radius: 10)
                }
            }
            .padding(.horizontal, 56).padding(.top, 34)
            HeroCircuit(accent: Brand.neon, ink: .white)
                .frame(height: 160).frame(maxWidth: .infinity)
                .padding(.top, 8)
            VStack(alignment: .leading, spacing: 6) {
                Text("Build it. Watch it think.").font(.system(size: 24, weight: .bold)).foregroundStyle(BrandText.primary)
                Text("Design logic circuits, run them live, and hand them in, in the time it takes to sketch one on paper.")
                    .font(.system(size: 13.5)).foregroundStyle(BrandText.secondary).fixedSize(horizontal: false, vertical: true)
            }
            .padding(.horizontal, 56).padding(.top, 4)
            HStack(alignment: .top, spacing: 14) {
                point("tray.full", "It keeps itself", "Everything saves as you go, in Your Circuits, with versions to go back to.")
                point("waveform.path", "It shows its work", "Live wires, a truth table on one key, and an oscilloscope.")
                point("keyboard", "It stays out of the way", "Nearly everything has a key. You won't need the menus.")
            }
            .padding(.horizontal, 56).padding(.top, 20)
        }
    }

    private func point(_ icon: String, _ title: String, _ line: String) -> some View {
        BrandCard {
            VStack(alignment: .leading, spacing: 7) {
                HStack(spacing: 8) {
                    Image(systemName: icon).font(.system(size: 12, weight: .semibold)).foregroundStyle(Brand.neon)
                    Text(title).font(.system(size: 13, weight: .bold)).foregroundStyle(BrandText.primary)
                }
                Text(line).font(.system(size: 11.5)).foregroundStyle(BrandText.secondary).fixedSize(horizontal: false, vertical: true)
            }
            .padding(14).frame(maxWidth: .infinity, minHeight: 88, alignment: .topLeading)
        }
    }

    // Page 1: make it yours.
    private var setup: some View {
        VStack(alignment: .leading, spacing: 16) {
            BrandHeading(eyebrow: "Make it yours", title: "Your canvas, your way",
                         line: "It all applies as you pick it; the window behind this one is the preview. Settings (⌘,) has the rest.")
            label("Appearance").padding(.top, 4)
            BrandSegmented(options: ["System", "Light", "Dark"],
                           selection: Binding(get: { min(prefs.themeMode, 2) }, set: { prefs.themeMode = $0 }))
            label("The app's colour")
            HStack(spacing: 12) {
                ForEach(accentOrder, id: \.self) { i in
                    let on = prefs.accent == i
                    Button { withAnimation(.easeOut(duration: 0.15)) { prefs.accent = i } } label: {
                        VStack(spacing: 6) {
                            Circle().fill(swatch(i)).frame(width: 28, height: 28)
                                .overlay(Circle().strokeBorder(Color.white.opacity(on ? 0.9 : 0), lineWidth: 2).padding(-4))
                                .shadow(color: on ? swatch(i).opacity(0.7) : .clear, radius: 8)
                            Text(accentNames[i]).font(.system(size: 10, weight: on ? .bold : .regular))
                                .foregroundStyle(on ? BrandText.primary : BrandText.faint)
                        }
                        .frame(width: 62)
                    }
                    .buttonStyle(.plain)
                }
            }
            label("Toolbar")
            BrandSegmented(options: ToolbarStyle.allCases.map(\.name),
                           selection: Binding(get: { ToolbarStyle.allCases.firstIndex(of: prefs.toolbarStyle) ?? 0 },
                                              set: { prefs.toolbarStyle = ToolbarStyle.allCases[$0] }))
            Text(prefs.toolbarStyle.blurb).font(.system(size: 11.5)).foregroundStyle(BrandText.secondary)
        }
        .padding(.horizontal, 56).padding(.top, 34)
    }

    private func swatch(_ i: Int) -> Color {
        var r = 0.0, g = 0.0, b = 0.0
        cl_accent_color(Int32(i), true, &r, &g, &b)
        return Color(.sRGB, red: r, green: g, blue: b)
    }

    // Page 2: your name.
    private var name: some View {
        HStack(alignment: .top, spacing: 34) {
            VStack(alignment: .leading, spacing: 16) {
                BrandHeading(eyebrow: "One more thing", title: "Who's handing this in?",
                             line: "Your name goes on every circuit you export, above the line that says whether it works: the first thing a grader looks for.")
                label("Your name").padding(.top, 4)
                TextField("First and last name", text: $prefs.studentName)
                    .textFieldStyle(.plain).font(.system(size: 15)).foregroundStyle(BrandText.primary)
                    .padding(.horizontal, 12).padding(.vertical, 9)
                    .background(RoundedRectangle(cornerRadius: 9, style: .continuous).fill(Color.white.opacity(0.07)))
                    .overlay(RoundedRectangle(cornerRadius: 9, style: .continuous).strokeBorder(Brand.neon.opacity(0.45)))
                    .frame(width: 300)
                Text("Rather not? Leave it blank. It's in Settings whenever you want it.")
                    .font(.system(size: 11.5)).foregroundStyle(BrandText.faint)
            }
            // What an export's footer looks like, with the name in it.
            VStack(alignment: .leading, spacing: 10) {
                Image(systemName: "rectangle.connected.to.line.below").font(.system(size: 40, weight: .ultraLight))
                    .foregroundStyle(Color.black.opacity(0.4)).frame(maxWidth: .infinity)
                Rectangle().fill(Color.black.opacity(0.15)).frame(height: 1)
                Text(prefs.studentName.isEmpty ? "________________" : prefs.studentName)
                    .font(.system(size: 15, weight: .bold)).foregroundStyle(.black)
                Text("This circuit works as specified.").font(.system(size: 11)).foregroundStyle(Color.black.opacity(0.6))
            }
            .padding(20)
            .frame(width: 250)
            .background(RoundedRectangle(cornerRadius: 12).fill(.white))
            .shadow(color: Brand.neon.opacity(0.2), radius: 18)
            .rotationEffect(.degrees(2))
            .padding(.top, 70)
        }
        .padding(.horizontal, 56).padding(.top, 34)
    }

    // Page 3: the keys.
    private let keyCards: [(String, String, String)] = [
        ("A", "Add a gate", "Type part of its name, press Return, click to drop it."),
        ("C", "Copy, or connect", "Copies the selection. While dragging a gate, drops it and wires it to pins nearby."),
        ("D", "Duplicate", "Copies the selection and puts the copy on your mouse."),
        ("R", "Rotate", "Turns the selected gates a quarter turn."),
        ("S", "Straighten", "Tidies the selected wires into clean routes."),
        ("T", "Truth table", "Tries every switch combination and writes down the lights."),
    ]

    private var keys: some View {
        VStack(alignment: .leading, spacing: 18) {
            BrandHeading(eyebrow: "The fast way", title: "Six keys worth knowing",
                         line: "Try them now: press any of these and watch it light up. Press ? any time for the full list.")
            LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 14), count: 3), spacing: 14) {
                ForEach(keyCards, id: \.0) { k in
                    let lit = pressed.contains(k.0.lowercased())
                    BrandCard(lit: lit) {
                        VStack(alignment: .leading, spacing: 8) {
                            BrandKeyCap(label: k.0, lit: lit)
                            Text(k.1).font(.system(size: 13, weight: .bold)).foregroundStyle(BrandText.primary)
                            Text(k.2).font(.system(size: 11)).foregroundStyle(BrandText.secondary).fixedSize(horizontal: false, vertical: true)
                        }
                        .padding(14).frame(maxWidth: .infinity, minHeight: 136, alignment: .topLeading)
                    }
                }
            }
        }
        .padding(.horizontal, 56).padding(.top, 34)
    }

    // Page 4: what next.
    /// The ways to start; ↑↓ choose, Return goes (the tour first).
    private var ready: some View {
        VStack(alignment: .leading, spacing: 10) {
            BrandHeading(eyebrow: "Ready", title: "Build your first circuit",
                         line: "↑↓ and Return work here too; ← goes back.")
                .padding(.bottom, 6)
            tile("Take the guided tour", "Two switches, a gate and a light, in about five minutes. Recommended.", icon: nil, index: 0)
            tile("Start from a template", "A lab page with your name, a counter, a 7-segment starter, or your own.", icon: "square.on.square", index: 1)
            tile("Start with a blank canvas", "Jump straight in. Help \u{25B8} Guided Tour replays the tour.", icon: "plus.square", index: 2)
            tile("See every shortcut", "The whole list, searchable.", icon: "keyboard", index: 3)
            HStack(alignment: .top, spacing: 8) {
                Image(systemName: "exclamationmark.bubble").font(.system(size: 12)).foregroundStyle(Brand.neon)
                Text("Something odd, or an idea? The speech bubble in the toolbar (or Help \u{25B8} Send Feedback) sends it straight to Levi.")
                    .font(.system(size: 11.5)).foregroundStyle(BrandText.faint).fixedSize(horizontal: false, vertical: true)
            }
            .padding(.top, 4)
        }
        .padding(.horizontal, 56).padding(.top, 30)
    }

    private func readyAction(_ i: Int) -> () -> Void {
        switch i {
        case 0: return { finish(); TourModel.shared.start() }
        case 1: return { finish(); openWindow(id: "templates") }
        case 2: return { finish() }
        default: return { finish(); CanvasController.front?.showShortcuts = true }
        }
    }

    private func tile(_ title: String, _ line: String, icon: String?, index: Int) -> some View {
        let on = index == readyChoice
        return Button(action: readyAction(index)) {
            BrandCard(lit: on, radius: 13) {
                HStack(spacing: 14) {
                    VStack(alignment: .leading, spacing: 4) {
                        Text(title).font(.system(size: 13.5, weight: .bold)).foregroundStyle(BrandText.primary)
                        Text(line).font(.system(size: 11.5)).foregroundStyle(BrandText.secondary).lineLimit(1)
                    }
                    Spacer()
                    if let icon {
                        Image(systemName: icon).font(.system(size: 17)).foregroundStyle(on ? Brand.neon : BrandText.faint)
                            .frame(width: 60)
                    } else {
                        // The tour's tile carries the live circuit, as in wx.
                        HeroCircuit(accent: Brand.neon, ink: .white).frame(width: 170, height: 50)
                    }
                }
                .padding(.leading, 18).padding(.trailing, 10).frame(height: 62)
            }
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .onHover { h in if h { withAnimation(.easeOut(duration: 0.15)) { readyChoice = index } } }
    }

    private func finish() {
        prefs.hasSeenWelcome = true
        WhatsNew.markSeen()
        dismiss()
        NSApp.windows.first { $0.identifier?.rawValue.contains("welcome") == true }?.close()
    }
}

/// A row of options on the brand ground, the chosen one lit in neon.
private struct BrandSegmented: View {
    let options: [String]
    @Binding var selection: Int

    var body: some View {
        HStack(spacing: 2) {
            ForEach(options.indices, id: \.self) { i in
                let on = i == selection
                Button { withAnimation(.easeOut(duration: 0.15)) { selection = i } } label: {
                    Text(options[i]).font(.system(size: 12.5, weight: on ? .bold : .medium))
                        .foregroundStyle(on ? Brand.ink : BrandText.primary.opacity(0.85))
                        .padding(.horizontal, 16).frame(height: 28)
                        .background(Capsule().fill(on ? Brand.neon : .clear))
                        .contentShape(Capsule())
                }
                .buttonStyle(.plain)
            }
        }
        .padding(3)
        .background(Capsule().fill(Color.white.opacity(0.06)))
        .overlay(Capsule().strokeBorder(Color.white.opacity(0.1)))
    }
}

/// The app's own subject, alive (the wx app's drawHero): two switches
/// stepping through 00, 01, 10, 11 into an AND gate, and a lamp that lights
/// only for the last. Wires carrying a 1 glow in the accent with signals
/// running along them.
struct HeroCircuit: View {
    let accent: Color
    let ink: Color
    @State private var start = Date()

    var body: some View {
        TimelineView(.animation(minimumInterval: 1.0 / 60)) { tl in
            let t = tl.date.timeIntervalSince(start)
            Canvas { ctx, size in Self.draw(&ctx, size: size, t: t, accent: accent, ink: ink) }
        }
    }

    private static func along(_ pts: [CGPoint], _ d: Double) -> CGPoint {
        var d = d
        for i in 1..<pts.count {
            let seg = hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y)
            if d <= seg || i + 1 == pts.count {
                let f = seg > 0 ? min(1, d / seg) : 0
                return CGPoint(x: pts[i - 1].x + (pts[i].x - pts[i - 1].x) * f, y: pts[i - 1].y + (pts[i].y - pts[i - 1].y) * f)
            }
            d -= seg
        }
        return pts.last ?? .zero
    }

    static func draw(_ ctx: inout GraphicsContext, size: CGSize, t: Double, accent: Color, ink: Color) {
        let lampOn = Color(.sRGB, red: 1, green: 196 / 255, blue: 64 / 255)
        let cx = size.width / 2, cy = size.height / 2
        let k = min(size.width / 520, size.height / 200)
        let state = Int(t / 0.95) % 4
        let a = state & 2 != 0, b = state & 1 != 0, out = a && b
        let bodyL = cx - 30 * k, bodyR = cx + 30 * k, hh = 44 * k, nose = bodyR + 44 * k

        let wires: [([CGPoint], Bool)] = [
            ([CGPoint(x: cx - 188 * k, y: cy - 50 * k), CGPoint(x: cx - 100 * k, y: cy - 50 * k),
              CGPoint(x: cx - 100 * k, y: cy - 24 * k), CGPoint(x: bodyL, y: cy - 24 * k)], a),
            ([CGPoint(x: cx - 188 * k, y: cy + 50 * k), CGPoint(x: cx - 100 * k, y: cy + 50 * k),
              CGPoint(x: cx - 100 * k, y: cy + 24 * k), CGPoint(x: bodyL, y: cy + 24 * k)], b),
            ([CGPoint(x: nose, y: cy), CGPoint(x: cx + 150 * k, y: cy)], out),
        ]
        for (pts, on) in wires {
            var p = Path()
            p.addLines(pts)
            if on { ctx.stroke(p, with: .color(accent.opacity(0.18)), lineWidth: 9 * k) }
            ctx.stroke(p, with: .color(on ? accent : ink.opacity(0.32)), lineWidth: 3 * k)
            guard on else { continue }
            var len = 0.0
            for i in 1..<pts.count { len += hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y) }
            let spacing = 34 * k
            var d = (t * 95 * k).truncatingRemainder(dividingBy: spacing)
            while d < len {
                let q = along(pts, d)
                ctx.fill(Path(ellipseIn: CGRect(x: q.x - 2.6 * k, y: q.y - 2.6 * k, width: 5.2 * k, height: 5.2 * k)), with: .color(.white))
                d += spacing
            }
        }

        for (y, on) in [(cy - 50 * k, a), (cy + 50 * k, b)] {
            let r = CGRect(x: cx - 232 * k, y: y - 18 * k, width: 44 * k, height: 36 * k)
            let shape = Path(roundedRect: r, cornerRadius: 8 * k)
            ctx.fill(shape, with: .color(on ? accent.opacity(0.22) : ink.opacity(0.05)))
            ctx.stroke(shape, with: .color(on ? accent : ink.opacity(0.35)), lineWidth: 2)
            ctx.draw(Text(on ? "1" : "0").font(.system(size: max(8, 15 * k), weight: .bold))
                        .foregroundColor(on ? accent : ink.opacity(0.55)), at: CGPoint(x: r.midX, y: r.midY))
        }

        var body = Path()
        body.move(to: CGPoint(x: bodyL, y: cy - hh))
        body.addLine(to: CGPoint(x: bodyR, y: cy - hh))
        body.addCurve(to: CGPoint(x: bodyR, y: cy + hh), control1: CGPoint(x: bodyR + 60 * k, y: cy - hh),
                      control2: CGPoint(x: bodyR + 60 * k, y: cy + hh))
        body.addLine(to: CGPoint(x: bodyL, y: cy + hh))
        body.closeSubpath()
        ctx.fill(body, with: .color(accent.opacity(0.12)))
        ctx.stroke(body, with: .color(accent), lineWidth: 3.2 * k)
        ctx.draw(Text("AND").font(.system(size: max(8, 13 * k), weight: .bold)).foregroundColor(accent.opacity(0.9)),
                 at: CGPoint(x: cx + 8 * k, y: cy))

        let lx = cx + 172 * k, lr = 22 * k
        if out {
            for i in stride(from: 3, through: 1, by: -1) {
                let gr = lr + Double(4 - i) * 9 * k
                ctx.fill(Path(ellipseIn: CGRect(x: lx - gr, y: cy - gr, width: gr * 2, height: gr * 2)),
                         with: .color(lampOn.opacity(0.10 * Double(i))))
            }
        }
        let lamp = Path(ellipseIn: CGRect(x: lx - lr, y: cy - lr, width: lr * 2, height: lr * 2))
        ctx.fill(lamp, with: .color(out ? lampOn : ink.opacity(0.06)))
        ctx.stroke(lamp, with: .color(out ? Color(.sRGB, red: 230 / 255, green: 160 / 255, blue: 20 / 255) : ink.opacity(0.35)),
                   lineWidth: 2.5 * k)
    }
}

// MARK: - The guided tour

/// The tour builds an AND circuit with you on a circuit of its own (a new
/// one, so it never touches your work, and whatever's on another circuit
/// can't tick its steps off). Its card sits on that circuit's window; each
/// step moves on by itself once it's done.
@MainActor
final class TourModel: ObservableObject {
    static let shared = TourModel()
    @Published var active = false
    @Published var step = 0
    @Published var done = false   // this step's goal was met; moving on shortly
    /// The circuit the tour is on.
    private(set) weak var target: CanvasController?
    /// Started, and waiting for its new circuit's window to come up (which
    /// opens beside yours rather than in its place).
    private(set) var waitingForCircuit = false
    private var stepStart = Date()
    private var doneAt: Date?
    private var lastCheck = Date.distantPast
    var sawLit = false, sawSimView = false, sawTruthTable = false

    struct Step {
        let title: String
        let body: (TourModel, CanvasController) -> String
        let keys: (TourModel, CanvasController) -> [String]
        /// nil: press Next.
        let check: ((TourModel, CanvasController, CLTourStatus) -> Bool)?
    }

    /// A command's keys as the user has them (Settings > Shortcuts).
    private static func keys(_ a: ShortcutAction) -> [String] { ShortcutStore.shared.combo(a)?.caps ?? [] }
    private static func name(_ a: ShortcutAction) -> String { ShortcutStore.shared.combo(a)?.label ?? "its shortcut" }

    static let steps: [Step] = [
        Step(title: "Add a switch",
             body: { _, _ in "Switches are your inputs. Press \(name(.addGate)), type toggle and press Return; the switch follows your mouse, and a click drops it. (Or drag a Toggle Switch out of Input/Output in the panel on the left.)" },
             keys: { _, _ in keys(.addGate) }, check: { _, _, s in s.switches >= 1 }),
        Step(title: "Add a second switch",
             body: { _, _ in "An AND gate has two inputs, so it needs two switches. Put another one below the first: press \(name(.addGate)) again, or select the first and press \(name(.quickDuplicate)) to duplicate it." },
             keys: { _, _ in keys(.quickDuplicate) }, check: { _, _, s in s.switches >= 2 }),
        Step(title: "Add an AND gate",
             body: { _, _ in "Press \(name(.addGate)) and type and. Drop the gate to the right of your switches." },
             keys: { _, _ in keys(.addGate) }, check: { _, _, s in s.hasAnd }),
        Step(title: "Add a light",
             body: { _, _ in "A light (an LED) shows an output. Press \(name(.addGate)), type led, and drop it to the right of the gate." },
             keys: { _, _ in keys(.addGate) }, check: { _, _, s in s.lights > 0 }),
        Step(title: "Wire in the switches",
             body: { _, _ in "Drag from a switch's pin (the little stub on its edge) to one of the gate's inputs. Or click the pin, let go, and click the other one. Do it for both switches." },
             keys: { _, _ in ["drag"] }, check: { _, _, s in s.andInputsWired >= 2 }),
        Step(title: "Wire in the light",
             body: { _, _ in "Now connect the gate's output, on its right-hand side, to the light." },
             keys: { _, _ in ["drag"] }, check: { _, _, s in s.lightWired }),
        Step(title: "Switch them both on",
             body: { _, _ in "Click the middle of each switch. When both are on, the light comes on: that is all AND means, this and that." },
             keys: { _, _ in ["click"] }, check: { m, _, s in if s.lightOn { m.sawLit = true }; return m.sawLit }),
        Step(title: "Now turn one off",
             body: { _, _ in "Click either switch. The light goes out: AND needs every input on." },
             keys: { _, _ in ["click"] }, check: { _, _, s in !s.lightOn && s.switchesOn < 2 }),
        Step(title: "Watch it run",
             body: { m, c in c.simView || m.sawSimView
                ? "Signals move along every wire carrying a 1. Flip a switch and watch them go. Press Escape when you've seen enough."
                : "Simulation View shows the circuit working: lit wires, with the signal marching along them. Press \(name(.simView))." },
             keys: { m, c in c.simView || m.sawSimView ? ["Esc"] : keys(.simView) },
             check: { m, c, _ in if c.simView { m.sawSimView = true }; return m.sawSimView && !c.simView }),
        Step(title: "Check it with a truth table",
             body: { _, _ in "Press \(name(.truthTable)). CedarLogic tries every combination of the switches and writes down what the light did: a quick way to check your work before you hand it in. Close it when you're done." },
             keys: { _, _ in keys(.truthTable) },
             check: { m, c, _ in if c.truthTable != nil { m.sawTruthTable = true }; return m.sawTruthTable && c.truthTable == nil }),
        Step(title: "Give it a name",
             body: { _, _ in "It's already in Your Circuits (\(name(.openLibrary))) and saves itself as you go. Click \u{201C}Untitled Circuit\u{201D} at the top of the window and choose Rename\u{2026} to name it. (\(name(.save)) keeps a version you can go back to.)" },
             keys: { _, _ in [] },
             check: { _, c, _ in
                 guard let w = c.view?.window, let url = NSDocumentController.shared.document(for: w)?.fileURL,
                       let name = Library.displayName(for: url) else { return false }
                 return name != "Untitled Circuit" }),
        Step(title: "You built a working circuit",
             body: { _, _ in "That's the loop: add, wire, try it, check it. Press ? whenever you want every shortcut, and Help \u{25B8} Guided Tour brings this back. Something odd, or an idea? The speech bubble in the toolbar sends it to Levi." },
             keys: { _, _ in ["?"] }, check: nil),
    ]

    /// On a new circuit of its own, in a new window (or in place of the one
    /// in front, as Settings > General says circuits open).
    func start() {
        step = 0; done = false; doneAt = nil
        sawLit = false; sawSimView = false; sawTruthTable = false
        target = nil
        waitingForCircuit = true
        active = true
        Templates.nextIsBlank = true   // the tour builds on a blank page, whatever new circuits start as
        NSDocumentController.shared.newDocument(nil)
    }

    /// A circuit window has come up: if the tour is waiting for one, this is it.
    func windowAppeared(_ c: CanvasController) {
        guard active, waitingForCircuit else { return }
        waitingForCircuit = false
        target = c
        stepStart = Date()
        c.onTick = { [weak c] in if let c { TourModel.shared.tick(c) } }
    }

    /// Its circuit's window went: so does the tour.
    func windowClosed(_ c: CanvasController) {
        if target === c { stop() }
    }

    func stop() {
        target?.onTick = nil
        target = nil
        waitingForCircuit = false
        active = false
    }

    func next() {
        if step + 1 >= Self.steps.count { stop(); return }
        step += 1; done = false; doneAt = nil; stepStart = Date()
    }

    /// Called from the tour circuit's clock; checks four times a second.
    func tick(_ c: CanvasController) {
        guard active, c === target, Date().timeIntervalSince(lastCheck) > 0.25, let doc = c.document else { return }
        lastCheck = Date()
        let s = Self.steps[step]
        if let doneAt {
            if Date().timeIntervalSince(doneAt) > 1.1 { next() }
            return
        }
        // A gate still following the pointer isn't placed yet.
        guard let check = s.check, Date().timeIntervalSince(stepStart) > 0.6, !c.isFloating else { return }
        var st = CLTourStatus()
        cl_tour_status(doc.handle, Int32(c.page), &st)
        if check(self, c, st) { withAnimation(.easeOut(duration: 0.2)) { done = true }; doneAt = Date() }
    }
}

/// The tour's card, in the launch screen's look: the step, what to do, the
/// keys for it, and a neon bar for how far along you are.
struct TourCard: View {
    @ObservedObject var canvas: CanvasController
    @ObservedObject private var tour = TourModel.shared

    var body: some View {
        let s = TourModel.steps[tour.step]
        let last = tour.step == TourModel.steps.count - 1
        let count = TourModel.steps.count
        VStack(alignment: .leading, spacing: 12) {
            HStack(spacing: 8) {
                Image(nsImage: Brand.icon).resizable().interpolation(.high)
                    .frame(width: 18, height: 18)
                    .clipShape(RoundedRectangle(cornerRadius: 18 * Brand.corner, style: .continuous))
                Text("GUIDED TOUR  \u{00B7}  \(tour.step + 1) OF \(count)")
                    .font(.system(size: 10.5, weight: .bold)).kerning(1.2).foregroundStyle(Brand.neon)
                Spacer()
                Button { tour.stop() } label: {
                    Image(systemName: "xmark").font(.system(size: 10, weight: .bold))
                        .frame(width: 22, height: 22)
                        .background(Circle().fill(Color.white.opacity(0.08)))
                }
                .buttonStyle(.plain).foregroundStyle(BrandText.secondary).help("End the tour")
            }
            GeometryReader { g in
                ZStack(alignment: .leading) {
                    Capsule().fill(Color.white.opacity(0.1))
                    Capsule().fill(LinearGradient(colors: [Brand.neonDeep, Brand.neon], startPoint: .leading, endPoint: .trailing))
                        .frame(width: max(4, g.size.width * CGFloat(tour.step + (tour.done ? 1 : 0)) / CGFloat(count)))
                        .shadow(color: Brand.neon.opacity(0.7), radius: 4)
                }
            }
            .frame(height: 3)
            .animation(.easeOut(duration: 0.35), value: tour.step)
            .animation(.easeOut(duration: 0.35), value: tour.done)
            Text(s.title).font(.system(size: 17, weight: .bold)).foregroundStyle(BrandText.primary)
            Text(s.body(tour, canvas)).font(.system(size: 12.5)).foregroundStyle(BrandText.secondary)
                .fixedSize(horizontal: false, vertical: true)
            let keys = s.keys(tour, canvas)
            if !keys.isEmpty {
                HStack(spacing: 5) {
                    ForEach(keys, id: \.self) { k in
                        if ["click", "drag"].contains(k) {
                            Text(k == "click" ? "Click" : "Drag").font(.system(size: 11, weight: .semibold))
                                .foregroundStyle(BrandText.secondary)
                                .padding(.horizontal, 9).frame(height: 24)
                                .background(Capsule().fill(Color.white.opacity(0.07)))
                        } else {
                            BrandKeyCap(label: k, size: 26)
                        }
                    }
                }
            }
            HStack(spacing: 8) {
                if s.check != nil {
                    Image(systemName: tour.done ? "checkmark.circle.fill" : "circle.dotted")
                        .foregroundStyle(tour.done ? Brand.neon : BrandText.faint)
                        .shadow(color: tour.done ? Brand.neon.opacity(0.7) : .clear, radius: 6)
                    Text(tour.done ? "Nice, that's it." : "Your turn. This moves on by itself.")
                        .font(.system(size: 12)).foregroundStyle(tour.done ? BrandText.primary : BrandText.faint)
                }
                Spacer()
                Button(last ? "Finish" : (s.check != nil && !tour.done ? "Skip" : "Next")) { tour.next() }
                    .buttonStyle(BrandButtonStyle(primary: last || s.check == nil || tour.done, width: 84))
            }
        }
        .padding(18)
        .frame(width: 360)
        .background(BrandBackground(bloom: .init(x: 0.15, y: 0), gridStep: 22))
        .clipShape(RoundedRectangle(cornerRadius: 18, style: .continuous))
        .overlay(RoundedRectangle(cornerRadius: 18, style: .continuous)
            .strokeBorder(LinearGradient(colors: [Color.white.opacity(0.22), Brand.neon.opacity(0.18), Color.white.opacity(0.05)],
                                         startPoint: .top, endPoint: .bottom), lineWidth: 1))
        .shadow(color: .black.opacity(0.35), radius: 18, y: 8)
        .environment(\.colorScheme, .dark)
        .animation(.easeOut(duration: 0.2), value: tour.step)
    }
}
