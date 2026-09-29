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

    private var accent: Color { prefs.accentColor(dark: prefs.dark) }
    private var ink: Color { prefs.dark ? Color(white: 0.93) : Color(white: 0.1) }
    private var dim: Color { prefs.dark ? Color(white: 0.65) : Color(white: 0.4) }
    private var paper: Color { prefs.dark ? Color(.sRGB, red: 0.10, green: 0.11, blue: 0.13) : Color(.sRGB, red: 0.985, green: 0.985, blue: 0.99) }

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
                if page < 4 {
                    Button("Skip") { finish() }.buttonStyle(.plain).foregroundStyle(dim).padding(24)
                }
            }
            .clipped()
            Rectangle().fill(ink.opacity(0.08)).frame(height: 1)
            HStack {
                HStack(spacing: 6) {
                    ForEach(0..<5) { i in
                        Capsule().fill(i == page ? accent : ink.opacity(0.2)).frame(width: i == page ? 22 : 8, height: 8)
                    }
                }
                Spacer()
                if page > 0 { pill("Back", primary: false) { go(-1) } }
                if page < 4 {
                    pill(page == 0 ? "Get Started" : "Continue", primary: true) { go(1) }
                } else {
                    pill("Just Start", primary: false) { finish() }
                }
            }
            .padding(.horizontal, 60).frame(height: 74)
        }
        .frame(width: 780, height: 580)
        .background(paper)
        .preferredColorScheme(prefs.dark ? .dark : .light)
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
        let to = min(4, max(0, page + d))
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
                if page == 4 { readyAction(readyChoice)() } else { go(1) }
                return nil
            case 123 where !typing: go(-1); return nil                     // ←
            case 124 where !typing: go(1); return nil                      // →
            case 125 where page == 4:                                      // ↓
                withAnimation(.easeOut(duration: 0.15)) { readyChoice = min(2, readyChoice + 1) }; return nil
            case 126 where page == 4:                                      // ↑
                withAnimation(.easeOut(duration: 0.15)) { readyChoice = max(0, readyChoice - 1) }; return nil
            default: break
            }
            guard !typing, let k = e.charactersIgnoringModifiers?.lowercased(), k.count == 1 else { return e }
            if k == "?" || (k == "/" && e.modifierFlags.contains(.shift)) { readyAction(2)(); return nil }
            if page == 3 && "acdrst".contains(k) {
                withAnimation(.easeOut(duration: 0.15)) { _ = pressed.insert(k) }
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.45) { withAnimation { _ = pressed.remove(k) } }
                return nil
            }
            return e
        }
    }

    private func pill(_ title: String, primary: Bool, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(title).font(.system(size: 13, weight: .semibold))
                .foregroundStyle(primary ? Color.white : ink)
                .frame(width: primary ? 142 : 96, height: 36)
                .background(Capsule().fill(primary ? accent : ink.opacity(0.08)))
                .overlay(Capsule().strokeBorder(primary ? .clear : ink.opacity(0.12)))
        }
        .buttonStyle(.plain)
    }

    private func heading(_ eyebrow: String, _ title: String, _ line: String) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(eyebrow.uppercased()).font(.system(size: 11, weight: .bold)).foregroundStyle(accent)
            Text(title).font(.system(size: 28, weight: .bold)).foregroundStyle(ink)
            Text(line).font(.system(size: 13.5)).foregroundStyle(dim).fixedSize(horizontal: false, vertical: true)
        }
    }

    private func card<C: View>(@ViewBuilder _ c: () -> C) -> some View {
        c().padding(16).frame(maxWidth: .infinity, alignment: .topLeading)
            .background(RoundedRectangle(cornerRadius: 14).fill(ink.opacity(0.045)))
    }

    // Page 0: what it is.
    private var intro: some View {
        VStack(alignment: .leading, spacing: 0) {
            HeroCircuit(accent: accent, ink: ink)
                .frame(height: 230)
                .frame(maxWidth: .infinity)
                .background(LinearGradient(colors: [accent.opacity(0.16), accent.opacity(0)], startPoint: .top, endPoint: .bottom))
            heading("CedarLogic", "Build it. Watch it think.",
                    "Design logic circuits, run them live, and hand them in — in the time it takes to sketch one on paper.")
                .padding(.horizontal, 60).padding(.top, 8)
            HStack(alignment: .top, spacing: 16) {
                point("It keeps itself", "Saves as you go, with versions to go back to.")
                point("It shows its work", "Live wires, a truth table on one key, and an oscilloscope.")
                point("It stays out of the way", "Nearly everything has a key. You won't need the menus.")
            }
            .padding(.horizontal, 60).padding(.top, 22)
        }
    }

    private func point(_ title: String, _ line: String) -> some View {
        card {
            VStack(alignment: .leading, spacing: 8) {
                HStack(spacing: 8) {
                    Circle().fill(accent).frame(width: 8, height: 8)
                    Text(title).font(.system(size: 13, weight: .bold)).foregroundStyle(ink)
                }
                Text(line).font(.system(size: 11.5)).foregroundStyle(dim).fixedSize(horizontal: false, vertical: true)
            }
        }
        .frame(height: 96)
    }

    // Page 1: make it yours.
    private var setup: some View {
        HStack(alignment: .top, spacing: 36) {
            VStack(alignment: .leading, spacing: 18) {
                heading("Make it yours", "Your canvas, your way", "It all applies as you pick it — the window behind this one is the preview.")
                label("Appearance")
                Picker("", selection: Binding(get: { min(prefs.themeMode, 2) }, set: { prefs.themeMode = $0 })) {
                    Text("System").tag(0); Text("Light").tag(1); Text("Dark").tag(2)
                }
                .pickerStyle(.segmented).labelsHidden().frame(width: 300)
                label("Accent colour")
                HStack(spacing: 14) {
                    ForEach(0..<6) { i in
                        Button { prefs.accent = i } label: {
                            Circle().fill(swatch(i)).frame(width: 28, height: 28)
                                .overlay(Circle().strokeBorder(ink.opacity(prefs.accent == i ? 0.8 : 0), lineWidth: 2).padding(-4))
                        }
                        .buttonStyle(.plain).help(accentNames[i])
                    }
                }
                .padding(.leading, 4)
                label("Toolbar")
                Picker("", selection: $prefs.toolbarStyle) {
                    ForEach(ToolbarStyle.allCases) { Text($0.name).tag($0) }
                }
                .pickerStyle(.segmented).labelsHidden().frame(width: 360)
                label("Tabs")
                Picker("", selection: $prefs.classicTabs) {
                    Text("Modern").tag(false); Text("Classic").tag(true)
                }
                .pickerStyle(.segmented).labelsHidden().frame(width: 240)
                Text("Drag to reorder; double-click to rename.").font(.caption).foregroundStyle(dim)
            }
        }
        .padding(.horizontal, 60).padding(.top, 36)
    }

    private func swatch(_ i: Int) -> Color {
        var r = 0.0, g = 0.0, b = 0.0
        cl_accent_color(Int32(i), prefs.dark, &r, &g, &b)
        return Color(.sRGB, red: r, green: g, blue: b)
    }

    private func label(_ s: String) -> some View {
        Text(s.uppercased()).font(.system(size: 11, weight: .bold)).foregroundStyle(dim)
    }

    // Page 2: your name.
    private var name: some View {
        VStack(alignment: .leading, spacing: 18) {
            heading("One more thing", "Who's handing this in?",
                    "Your name goes on every circuit you export, above the line that says whether it works — the first thing a grader looks for.")
            label("Your name")
            TextField("First and last name", text: $prefs.studentName)
                .textFieldStyle(.roundedBorder).font(.system(size: 15)).frame(width: 320)
            Text("Rather not? Leave it blank. It's in Settings whenever you want it.").font(.caption).foregroundStyle(dim)
            VStack(alignment: .leading, spacing: 10) {
                Image(systemName: "rectangle.connected.to.line.below").font(.system(size: 40, weight: .ultraLight))
                    .foregroundStyle(Color.black.opacity(0.4)).frame(maxWidth: .infinity)
                Rectangle().fill(Color.black.opacity(0.15)).frame(height: 1)
                Text(prefs.studentName.isEmpty ? "________________" : prefs.studentName)
                    .font(.system(size: 15, weight: .bold)).foregroundStyle(.black)
                Text("This circuit works as specified.").font(.system(size: 11)).foregroundStyle(Color.black.opacity(0.6))
            }
            .padding(20)
            .background(RoundedRectangle(cornerRadius: 12).fill(.white).shadow(color: .black.opacity(0.15), radius: 4, y: 2))
        }
        .padding(.horizontal, 60).padding(.top, 36)
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
        VStack(alignment: .leading, spacing: 20) {
            heading("The fast way", "Six keys worth knowing", "Try them now — press any of these and watch it light up. Press ? any time for the full list.")
            LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 16), count: 3), spacing: 16) {
                ForEach(keyCards, id: \.0) { k in
                    card {
                        VStack(alignment: .leading, spacing: 8) {
                            Text(k.0).font(.system(size: 16, weight: .bold)).foregroundStyle(ink)
                                .frame(width: 38, height: 38)
                                .background(RoundedRectangle(cornerRadius: 8).fill(pressed.contains(k.0.lowercased()) ? accent.opacity(0.5) : ink.opacity(0.08)))
                                .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(ink.opacity(0.18)))
                            Text(k.1).font(.system(size: 13, weight: .bold)).foregroundStyle(ink)
                            Text(k.2).font(.system(size: 11)).foregroundStyle(dim).fixedSize(horizontal: false, vertical: true)
                        }
                    }
                    .frame(height: 142, alignment: .top)
                }
            }
        }
        .padding(.horizontal, 60).padding(.top, 36)
    }

    // Page 4: what next.
    /// The three ways to start; ↑↓ choose, Return goes (the tour first).
    private var ready: some View {
        VStack(alignment: .leading, spacing: 16) {
            heading("Ready", "Build your first circuit", "About five minutes, and it moves on by itself as you go. ↑↓ and Return work here too; ← goes back.")
            tile("Take the guided tour", "Two switches, a gate and a light. Recommended.", index: 0, action: readyAction(0))
            tile("Start with a blank canvas", "Jump straight in. Help > Guided Tour replays it.", index: 1, action: readyAction(1))
            tile("See every shortcut", "The whole list, searchable.", index: 2, action: readyAction(2))
            HStack(alignment: .top, spacing: 10) {
                Image(systemName: "exclamationmark.bubble").font(.system(size: 15)).foregroundStyle(accent)
                Text("Found something odd, or have an idea? The speech bubble at the right of the toolbar (or Help > Send Feedback) sends it straight to Levi, with a screenshot or a quick recording if you like.")
                    .font(.system(size: 12.5)).foregroundStyle(dim).fixedSize(horizontal: false, vertical: true)
            }
            .padding(.top, 6)
        }
        .padding(.horizontal, 60).padding(.top, 36)
    }

    private func readyAction(_ i: Int) -> () -> Void {
        switch i {
        case 0: return { finish(); TourModel.shared.start() }
        case 1: return { finish() }
        default: return { finish(); CanvasController.front?.showShortcuts = true }
        }
    }

    private func tile(_ title: String, _ line: String, index: Int, action: @escaping () -> Void) -> some View {
        let primary = index == readyChoice
        return Button(action: action) {
            HStack {
                VStack(alignment: .leading, spacing: 6) {
                    Text(title).font(.system(size: 14, weight: .bold)).foregroundStyle(ink)
                    Text(line).font(.system(size: 12)).foregroundStyle(dim)
                }
                Spacer()
                if index == 0 {
                    // The tour's tile carries the live circuit, as in wx.
                    HeroCircuit(accent: accent, ink: ink).frame(width: 220, height: 74)
                } else {
                    Image(systemName: "arrow.right").font(.system(size: 18)).foregroundStyle(primary ? accent : dim)
                }
            }
            .padding(.leading, 24).padding(.trailing, index == 0 ? 6 : 24).frame(height: 82)
            .background(RoundedRectangle(cornerRadius: 14).fill(primary ? accent.opacity(0.14) : ink.opacity(0.045)))
            .overlay(RoundedRectangle(cornerRadius: 14).strokeBorder(primary ? accent.opacity(0.7) : ink.opacity(0.1), lineWidth: primary ? 1.5 : 1))
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .onHover { h in if h { withAnimation(.easeOut(duration: 0.15)) { readyChoice = index } } }
    }

    private func finish() {
        prefs.hasSeenWelcome = true
        dismiss()
        NSApp.windows.first { $0.identifier?.rawValue.contains("welcome") == true }?.close()
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

@MainActor
final class TourModel: ObservableObject {
    static let shared = TourModel()
    @Published var active = false
    @Published var step = 0
    @Published var done = false   // this step's goal was met; moving on shortly
    private var stepStart = Date()
    private var doneAt: Date?
    private var lastCheck = Date.distantPast
    var sawLit = false, sawSimView = false, sawTruthTable = false
    var savedAtStart: Date?

    struct Step {
        let title: String
        let body: (TourModel, CanvasController) -> String
        let keys: (TourModel, CanvasController) -> [String]
        /// nil: press Next.
        let check: ((TourModel, CanvasController, CLTourStatus) -> Bool)?
    }

    static let steps: [Step] = [
        Step(title: "Add a switch",
             body: { _, _ in "Switches are your inputs. Press A, type toggle and press Return — the switch follows your mouse; click to drop it. (Or drag a Toggle Switch from Input/Output in the panel on the left.)" },
             keys: { _, _ in ["A"] }, check: { _, _, s in s.switches >= 1 }),
        Step(title: "Add a second switch",
             body: { _, _ in "An AND gate has two inputs, so it needs two switches. Put another one below the first — press A again, or select the first and press D to duplicate it." },
             keys: { _, _ in ["D"] }, check: { _, _, s in s.switches >= 2 }),
        Step(title: "Add an AND gate",
             body: { _, _ in "Press A and type and. Drop the gate to the right of your switches." },
             keys: { _, _ in ["A"] }, check: { _, _, s in s.hasAnd }),
        Step(title: "Add a light",
             body: { _, _ in "A light (an LED) shows an output. Press A, type led, and drop it to the right of the gate." },
             keys: { _, _ in ["A"] }, check: { _, _, s in s.lights > 0 }),
        Step(title: "Wire in the switches",
             body: { _, _ in "Drag from a switch's pin — the little stub on its edge — to one of the gate's inputs. Or click the pin, let go, and click the other one. Do it for both switches." },
             keys: { _, _ in [] }, check: { _, _, s in s.andInputsWired >= 2 }),
        Step(title: "Wire in the light",
             body: { _, _ in "Now connect the gate's output, on its right-hand side, to the light." },
             keys: { _, _ in [] }, check: { _, _, s in s.lightWired }),
        Step(title: "Switch them both on",
             body: { _, _ in "Click the middle of each switch. When both are on, the light comes on — that is all AND means: this and that." },
             keys: { _, _ in ["click"] }, check: { m, _, s in if s.lightOn { m.sawLit = true }; return m.sawLit }),
        Step(title: "Now turn one off",
             body: { _, _ in "Click either switch. The light goes out: AND needs every input on." },
             keys: { _, _ in ["click"] }, check: { _, _, s in !s.lightOn && s.switchesOn < 2 }),
        Step(title: "Watch it run",
             body: { m, c in c.simView || m.sawSimView
                ? "Signals move along every wire carrying a 1. Flip a switch and watch them go. Press Escape when you've seen enough."
                : "Simulation View shows the circuit working: lit wires, with the signal marching along them." },
             keys: { m, c in c.simView || m.sawSimView ? ["Esc"] : ["⌘", "R"] },
             check: { m, c, _ in if c.simView { m.sawSimView = true }; return m.sawSimView && !c.simView }),
        Step(title: "Check it with a truth table",
             body: { _, _ in "Press T. CedarLogic tries every combination of the switches and writes down what the light did — a quick way to check your work before you hand it in." },
             keys: { _, _ in ["T"] },
             check: { m, c, _ in if c.truthTable != nil { m.sawTruthTable = true }; return m.sawTruthTable && c.truthTable == nil }),
        Step(title: "Save it",
             body: { _, _ in "Press ⌘S and give it a name. From then on it saves itself as you go, and File > Revert To (or Your Circuits) takes you back to an earlier version." },
             keys: { _, _ in ["⌘", "S"] },
             check: { m, c, _ in
                 guard let w = c.view?.window, let d = NSDocumentController.shared.document(for: w) else { return false }
                 return d.fileURL != nil && d.fileModificationDate != m.savedAtStart && !d.isDocumentEdited }),
        Step(title: "You built a working circuit",
             body: { _, _ in "That's the loop: add, wire, try it, check it. Press ? whenever you want every shortcut, and Help > Guided Tour brings this back. Something odd, or an idea? The speech bubble at the right of the toolbar sends it to Levi." },
             keys: { _, _ in ["?"] }, check: nil),
    ]

    func start() {
        step = 0; done = false; doneAt = nil
        sawLit = false; sawSimView = false; sawTruthTable = false
        stepStart = Date()
        active = true
        if CanvasController.front == nil { NSDocumentController.shared.newDocument(nil) }
    }

    func stop() { active = false }

    func next(_ c: CanvasController?) {
        if step + 1 >= Self.steps.count { stop(); return }
        step += 1; done = false; doneAt = nil; stepStart = Date()
        if step == 10, let c, let w = c.view?.window, let d = NSDocumentController.shared.document(for: w) {
            savedAtStart = d.fileModificationDate
        }
    }

    /// Called from the canvas's clock; checks four times a second.
    func tick(_ c: CanvasController) {
        guard active, Date().timeIntervalSince(lastCheck) > 0.25, let doc = c.document else { return }
        lastCheck = Date()
        let s = Self.steps[step]
        if let doneAt {
            if Date().timeIntervalSince(doneAt) > 1.1 { next(c) }
            return
        }
        // A gate still following the pointer isn't placed yet.
        guard let check = s.check, Date().timeIntervalSince(stepStart) > 0.6, !c.isFloating else { return }
        var st = CLTourStatus()
        cl_tour_status(doc.handle, Int32(c.page), &st)
        if check(self, c, st) { done = true; doneAt = Date() }
    }
}

struct TourCard: View {
    @ObservedObject var canvas: CanvasController
    let document: CoreDocument
    let page: Int
    @ObservedObject private var tour = TourModel.shared
    @ObservedObject private var prefs = Prefs.shared

    var body: some View {
        let s = TourModel.steps[tour.step]
        let accent = prefs.accentColor(dark: prefs.dark)
        let last = tour.step == TourModel.steps.count - 1
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Text("GUIDED TOUR  ·  \(tour.step + 1) OF \(TourModel.steps.count)")
                    .font(.system(size: 11, weight: .bold)).foregroundStyle(accent)
                Spacer()
                Button { tour.stop() } label: { Image(systemName: "xmark").font(.system(size: 11, weight: .semibold)) }
                    .buttonStyle(.plain).foregroundStyle(.secondary).help("End the tour")
            }
            GeometryReader { g in
                ZStack(alignment: .leading) {
                    Capsule().fill(Color.primary.opacity(0.1))
                    Capsule().fill(accent)
                        .frame(width: max(4, g.size.width * CGFloat(tour.step + (tour.done ? 1 : 0)) / CGFloat(TourModel.steps.count)))
                }
            }
            .frame(height: 4)
            Text(s.title).font(.system(size: 17, weight: .bold))
            Text(s.body(tour, canvas)).font(.system(size: 12.5)).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            let keys = s.keys(tour, canvas)
            if !keys.isEmpty { HStack(spacing: 4) { ForEach(keys, id: \.self) { KeyCap(label: $0) } } }
            if s.check != nil {
                HStack(spacing: 8) {
                    Image(systemName: tour.done ? "checkmark.circle.fill" : "circle.dotted")
                        .foregroundStyle(tour.done ? Color.green : accent)
                    Text(tour.done ? "Nice — that's it." : "Your turn — this moves on by itself.")
                        .font(.system(size: 12)).foregroundStyle(.secondary)
                }
            }
            HStack {
                Spacer()
                Button(last ? "Finish" : (s.check != nil && !tour.done ? "Skip" : "Next")) { tour.next(canvas) }
                    .controlSize(.large)
            }
        }
        .padding(18)
        .frame(width: 360)
        .background(RoundedRectangle(cornerRadius: 14).fill(.regularMaterial))
        .overlay(RoundedRectangle(cornerRadius: 14).strokeBorder(Color.primary.opacity(0.1)))
        .shadow(color: .black.opacity(0.25), radius: 12, y: 4)
        .onAppear { canvas.onTick = { [weak canvas] in if let canvas { TourModel.shared.tick(canvas) } } }
    }
}
