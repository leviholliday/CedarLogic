// What's New: after an update, a walk through everything that's new since
// the first versions testers installed (0.2), in the launch screen's look,
// a page a theme, each with a way to try it. Shown once per version (new
// installs get the welcome instead); Help > What's New in CedarLogic brings
// it back.

import AppKit
import SwiftUI

@MainActor
enum WhatsNew {
    /// Bumped when there's a new tour of what's new to give.
    static let version = "0.3"
    private static let key = "cl.whatsNewSeen"

    /// Someone who's used CedarLogic before, and hasn't seen this one.
    static var shouldShow: Bool {
        Prefs.shared.hasSeenWelcome && UserDefaults.standard.string(forKey: key) != version
    }
    static func markSeen() { UserDefaults.standard.set(version, forKey: key) }
}

struct WhatsNewView: View {
    @Environment(\.dismiss) private var dismiss
    @Environment(\.openWindow) private var openWindow
    @State private var page: Int
    @State private var forward = true
    @State private var keyMonitor: Any?

    init(page: Int = 0) { _page = State(initialValue: page) }

    private struct Chapter {
        let eyebrow: String, title: String, line: String
        let points: [(String, String, String)]   // icon, title, line
        let tryTitle: String?
    }

    private static let chapters: [Chapter] = [
        Chapter(eyebrow: "Your circuits", title: "Everything in one place",
                line: "Every circuit lives in Your Circuits and saves itself as you go. No files to lose.",
                points: [
                    ("tray.full", "Your Circuits (\u{2318}O)", "Open, rename, delete. A new circuit joins as soon as there's something on it."),
                    ("doc.on.doc", "Files come in as copies", "Open a .cdl from anywhere and you work on a copy; File \u{25B8} Export gets one out."),
                    ("clock.arrow.circlepath", "Versions that mean something", "A version only when the circuit changed, not every time a switch flips."),
                ], tryTitle: "Open Your Circuits"),
        Chapter(eyebrow: "Start ahead", title: "Templates and your own parts",
                line: "Stop rebuilding the same thing every lab.",
                points: [
                    ("square.on.square", "New from Template", "A Lab Page with your name on it, a 4-bit counter, a 7-segment starter, or your own."),
                    ("shippingbox", "My Parts", "Select some gates, Edit \u{25B8} Save as Part, name it. Drag it from the side panel or find it with A."),
                    ("gearshape", "Start every circuit your way", "Settings \u{25B8} General \u{25B8} New circuits can start each one from a template."),
                ], tryTitle: "Browse Templates"),
        Chapter(eyebrow: "Check your work", title: "Truth tables that do the algebra",
                line: "Press T, and CedarLogic hands you the simplest answer too.",
                points: [
                    ("tablecells", "Karnaugh maps and formulas", "The truth table has tabs: the table, a K-map for each light, and the simplest SOP and POS."),
                    ("function", "Build from a Formula", "Type F = AB + C' (or \u{03A3}m(1,3,5)) and get the gates, wired and labelled."),
                    ("magnifyingglass", "Find (\u{2318}F)", "Labels, TO/FROM names and parts on every page, one Return away."),
                ], tryTitle: "Build from a Formula"),
        Chapter(eyebrow: "See it think", title: "Watch the signals",
                line: "The circuit shows you what it's doing.",
                points: [
                    ("point.3.connected.trianglepath.dotted", "Point at a wire", "Every branch of it lights up, so you can follow it across the page."),
                    ("waveform.path", "Timing diagrams", "Share the oscilloscope (\u{2318}G) as a picture or a PDF, in colour or black and white."),
                    ("play.circle", "Simulation View", "Lit wires with the signal marching along them. Press \u{2318}R."),
                ], tryTitle: nil),
        Chapter(eyebrow: "Made for the Mac", title: "Faster, calmer, greener",
                line: "A new look, and a lot of care in the small things.",
                points: [
                    ("paintpalette", "CedarLogic green", "The icon's colour is the app's colour now. Settings \u{25B8} Appearance has the others."),
                    ("slider.horizontal.3", "Gate settings and memory", "Double-click a gate or a RAM for clean, quick editors. Return is Done."),
                    ("exclamationmark.bubble", "Send Feedback", "The speech bubble in the toolbar sends a note, a screenshot or a short recording."),
                ], tryTitle: nil),
    ]

    private var pages: Int { Self.chapters.count + 2 }

    var body: some View {
        VStack(spacing: 0) {
            ZStack(alignment: .topTrailing) {
                Group {
                    if page == 0 { intro }
                    else if page <= Self.chapters.count { chapter(Self.chapters[page - 1], index: page - 1) }
                    else { finale }
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
                .transition(.asymmetric(insertion: .offset(x: forward ? 60 : -60).combined(with: .opacity),
                                        removal: .offset(x: forward ? -60 : 60).combined(with: .opacity)))
                .id(page)
                if page < pages - 1 {
                    Button("Skip") { finish() }.buttonStyle(.plain)
                        .font(.system(size: 12.5, weight: .medium)).foregroundStyle(BrandText.faint)
                        .padding(.horizontal, 26).padding(.top, 22)
                }
            }
            .clipped()
            Rectangle().fill(Color.white.opacity(0.07)).frame(height: 1)
            HStack {
                HStack(spacing: 6) {
                    ForEach(0..<pages, id: \.self) { i in
                        Capsule().fill(i == page ? Brand.neon : Color.white.opacity(0.18))
                            .frame(width: i == page ? 22 : 8, height: 8)
                            .shadow(color: i == page ? Brand.neon.opacity(0.6) : .clear, radius: 5)
                    }
                }
                .animation(WelcomeView.slide, value: page)
                Spacer()
                if page > 0 {
                    Button("Back") { go(-1) }.buttonStyle(BrandButtonStyle(primary: false, width: 96))
                }
                if page < pages - 1 {
                    Button(page == 0 ? "Show Me" : "Next") { go(1) }.buttonStyle(BrandButtonStyle(width: 142))
                } else {
                    Button("Start Building") { finish() }.buttonStyle(BrandButtonStyle(width: 150))
                }
            }
            .padding(.horizontal, 44).frame(height: 74)
        }
        .frame(width: 820, height: 600)
        .background(BrandBackground())
        .preferredColorScheme(.dark)
        .tint(Brand.neon)
        .onAppear { WhatsNew.markSeen(); installKeys() }
        .onDisappear { if let m = keyMonitor { NSEvent.removeMonitor(m); keyMonitor = nil } }
    }

    private func go(_ d: Int) {
        let to = min(pages - 1, max(0, page + d))
        guard to != page else { return }
        forward = d > 0
        withAnimation(WelcomeView.slide) { page = to }
    }

    private func finish() {
        dismiss()
        NSApp.windows.first { $0.identifier?.rawValue.contains("whatsnew") == true }?.close()
    }

    /// ← → between pages, Return on, Escape closes.
    private func installKeys() {
        guard keyMonitor == nil else { return }
        keyMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { e in
            guard e.window?.identifier?.rawValue.contains("whatsnew") == true,
                  e.modifierFlags.intersection([.command, .control, .option]).isEmpty else { return e }
            switch e.keyCode {
            case 53: finish(); return nil
            case 36, 76: if page == pages - 1 { finish() } else { go(1) }; return nil
            case 123: go(-1); return nil
            case 124: go(1); return nil
            default: return e
            }
        }
    }

    // MARK: Pages

    private var intro: some View {
        HStack(alignment: .center, spacing: 40) {
            VStack(alignment: .leading, spacing: 14) {
                Text("WHAT'S NEW").font(.system(size: 11, weight: .bold)).kerning(1.8).foregroundStyle(Brand.neon)
                Text("CedarLogic \(WhatsNew.version)").font(.system(size: 34, weight: .bold)).foregroundStyle(Brand.silver)
                    .shadow(color: Brand.neon.opacity(0.3), radius: 10)
                Text("It has a new name (just CedarLogic), a new icon, and a lot more inside. Here's everything that's new since the first versions, a minute's read.")
                    .font(.system(size: 14)).foregroundStyle(BrandText.secondary).fixedSize(horizontal: false, vertical: true)
                VStack(alignment: .leading, spacing: 8) {
                    ForEach(Array(Self.chapters.enumerated()), id: \.offset) { i, c in
                        Button { forward = true; withAnimation(WelcomeView.slide) { page = i + 1 } } label: {
                            HStack(spacing: 10) {
                                Image(systemName: c.points[0].0).font(.system(size: 12, weight: .semibold))
                                    .foregroundStyle(Brand.neon).frame(width: 18)
                                Text(c.title).font(.system(size: 13, weight: .semibold)).foregroundStyle(BrandText.primary)
                                Text(c.eyebrow).font(.system(size: 11)).foregroundStyle(BrandText.faint)
                            }
                            .contentShape(Rectangle())
                        }
                        .buttonStyle(.plain)
                    }
                }
                .padding(.top, 6)
            }
            .frame(maxWidth: 400, alignment: .leading)
            IconShowcase()
        }
        .padding(.horizontal, 56).padding(.top, 40)
    }

    private func chapter(_ c: Chapter, index: Int) -> some View {
        HStack(alignment: .top, spacing: 34) {
            VStack(alignment: .leading, spacing: 14) {
                BrandHeading(eyebrow: c.eyebrow, title: c.title, line: c.line)
                VStack(alignment: .leading, spacing: 10) {
                    ForEach(c.points, id: \.1) { p in
                        BrandCard {
                            HStack(alignment: .top, spacing: 12) {
                                Image(systemName: p.0).font(.system(size: 15, weight: .semibold)).foregroundStyle(Brand.neon)
                                    .frame(width: 24).padding(.top, 1)
                                VStack(alignment: .leading, spacing: 3) {
                                    Text(p.1).font(.system(size: 13, weight: .bold)).foregroundStyle(BrandText.primary)
                                    Text(p.2).font(.system(size: 11.5)).foregroundStyle(BrandText.secondary)
                                        .fixedSize(horizontal: false, vertical: true)
                                }
                                Spacer(minLength: 0)
                            }
                            .padding(12)
                        }
                    }
                }
                .padding(.top, 4)
                if let t = c.tryTitle {
                    Button(t) { tryIt(index) }.buttonStyle(BrandButtonStyle(primary: false))
                        .padding(.top, 2)
                }
            }
            .frame(width: 420, alignment: .leading)
            ChapterArt(index: index).frame(maxWidth: .infinity).padding(.top, 50)
        }
        .padding(.horizontal, 48).padding(.top, 34)
    }

    private func tryIt(_ index: Int) {
        switch index {
        case 0: openWindow(id: "library")
        case 1: openWindow(id: "templates")
        case 2: CanvasController.front?.perform(.buildFormula)
        default: break
        }
    }

    private var finale: some View {
        VStack(alignment: .leading, spacing: 12) {
            BrandHeading(eyebrow: "That's the tour", title: "Go build something",
                         line: "Everything here is in Help too, whenever you want it.")
                .padding(.bottom, 8)
            finaleTile("Take the guided tour again", "It's new too: two switches, a gate and a light, on a circuit of its own.", "figure.walk") {
                finish(); TourModel.shared.start()
            }
            finaleTile("Start from a template", "The Lab Page has your name on it already.", "square.on.square") {
                finish(); openWindow(id: "templates")
            }
            finaleTile("Open CedarLogic Help", "Every feature, with its keys.", "questionmark.circle") {
                finish(); openWindow(id: "help")
            }
        }
        .padding(.horizontal, 56).padding(.top, 34)
    }

    private func finaleTile(_ title: String, _ line: String, _ icon: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            BrandCard(radius: 13) {
                HStack(spacing: 14) {
                    Image(systemName: icon).font(.system(size: 18)).foregroundStyle(Brand.neon).frame(width: 30)
                    VStack(alignment: .leading, spacing: 4) {
                        Text(title).font(.system(size: 13.5, weight: .bold)).foregroundStyle(BrandText.primary)
                        Text(line).font(.system(size: 11.5)).foregroundStyle(BrandText.secondary)
                    }
                    Spacer()
                    Image(systemName: "arrow.right").foregroundStyle(BrandText.faint)
                }
                .padding(.horizontal, 18).frame(height: 66)
            }
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }
}

/// The new icon, large, glowing, gently floating.
private struct IconShowcase: View {
    @State private var start = Date()
    var body: some View {
        TimelineView(.animation(minimumInterval: 1.0 / 60)) { tl in
            let t = tl.date.timeIntervalSince(start)
            ZStack {
                Circle().fill(RadialGradient(colors: [Brand.neon.opacity(0.28), .clear], center: .center, startRadius: 10, endRadius: 170))
                    .frame(width: 340, height: 340)
                    .scaleEffect(1 + 0.04 * sin(t * 1.4))
                Image(nsImage: Brand.icon).resizable().interpolation(.high)
                    .frame(width: 210, height: 210)
                    .clipShape(RoundedRectangle(cornerRadius: 210 * 0.2237, style: .continuous))
                    .shadow(color: Brand.neon.opacity(0.4), radius: 24)
                    .shadow(color: .black.opacity(0.6), radius: 18, y: 14)
                    .offset(y: 5 * sin(t * 1.1))
                    .rotation3DEffect(.degrees(4 * sin(t * 0.7)), axis: (x: 0, y: 1, z: 0))
            }
        }
        .frame(width: 300, height: 360)
    }
}

/// A picture for each chapter, drawn rather than screenshotted so it's
/// sharp and in the brand's colours.
private struct ChapterArt: View {
    let index: Int

    var body: some View {
        switch index {
        case 0: circuits
        case 1: templates
        case 2: kmap
        case 3: signals
        default: palette
        }
    }

    private func row(_ name: String, _ detail: String, open: Bool, lit: Bool) -> some View {
        BrandCard(lit: lit, radius: 11) {
            HStack(spacing: 12) {
                RoundedRectangle(cornerRadius: 7).fill(Brand.neon.opacity(0.16)).frame(width: 30, height: 30)
                    .overlay(Image(systemName: "cpu").font(.system(size: 13)).foregroundStyle(Brand.neon))
                VStack(alignment: .leading, spacing: 2) {
                    Text(name).font(.system(size: 12, weight: .bold)).foregroundStyle(BrandText.primary)
                    Text(detail).font(.system(size: 10)).foregroundStyle(BrandText.faint)
                }
                Spacer()
                if open {
                    Text("OPEN").font(.system(size: 8.5, weight: .bold)).foregroundStyle(Brand.neon)
                        .padding(.horizontal, 7).frame(height: 17).background(Capsule().fill(Brand.neon.opacity(0.16)))
                }
            }
            .padding(.horizontal, 12).frame(height: 50)
        }
    }

    private var circuits: some View {
        VStack(spacing: 8) {
            row("Lab 5: Traffic Light", "42 gates \u{00B7} Today at 2:14 PM", open: true, lit: true)
            row("Full Adder", "18 gates \u{00B7} Yesterday", open: false, lit: false)
            row("BCD to 7 Segment", "96 gates \u{00B7} Sep 24", open: false, lit: false)
            row("Counter", "12 gates \u{00B7} Sep 22", open: false, lit: false)
        }
        .frame(width: 290)
    }

    private var templates: some View {
        VStack(spacing: 12) {
            HStack(spacing: 12) {
                tile("doc.text", "Lab Page")
                tile("123.rectangle", "Counter")
                tile("8.square", "7-Segment")
            }
            BrandCard(lit: true, radius: 12) {
                HStack(spacing: 12) {
                    Image(systemName: "shippingbox.fill").font(.system(size: 20)).foregroundStyle(Brand.neon)
                    VStack(alignment: .leading, spacing: 2) {
                        Text("My Parts").font(.system(size: 12, weight: .bold)).foregroundStyle(BrandText.primary)
                        Text("Full Adder \u{00B7} 2-to-4 Decoder \u{00B7} Debouncer").font(.system(size: 10)).foregroundStyle(BrandText.faint)
                    }
                    Spacer()
                }
                .padding(14)
            }
        }
        .frame(width: 290)
    }

    private func tile(_ icon: String, _ name: String) -> some View {
        BrandCard(radius: 12) {
            VStack(spacing: 8) {
                Image(systemName: icon).font(.system(size: 24, weight: .light)).foregroundStyle(Brand.neon)
                Text(name).font(.system(size: 10.5, weight: .semibold)).foregroundStyle(BrandText.secondary)
            }
            .frame(width: 86, height: 92)
        }
    }

    /// A 4-variable K-map (rows AB, columns CD) with its two groups circled,
    /// and the answer under it: A'D is the top two rows' middle columns, BD
    /// the middle two rows'.
    private var kmap: some View {
        let ones: Set<Int> = [1, 3, 5, 7, 13, 15]
        return VStack(spacing: 14) {
            ZStack(alignment: .topLeading) {
                Grid(horizontalSpacing: 4, verticalSpacing: 4) {
                    ForEach(0..<4, id: \.self) { r in
                        GridRow {
                            ForEach(0..<4, id: \.self) { c in
                                // Rows AB and columns CD in Gray order (00 01 11 10).
                                let gray = [0, 1, 3, 2]
                                let m = gray[r] * 4 + gray[c]
                                Text(ones.contains(m) ? "1" : "0")
                                    .font(.system(size: 15, weight: .bold, design: .monospaced))
                                    .foregroundStyle(ones.contains(m) ? Brand.neon : BrandText.faint)
                                    .frame(width: 48, height: 40)
                                    .background(RoundedRectangle(cornerRadius: 6).fill(Color.white.opacity(0.05)))
                            }
                        }
                    }
                }
                RoundedRectangle(cornerRadius: 10).strokeBorder(Brand.neon, lineWidth: 2)
                    .frame(width: 104, height: 84).offset(x: 50, y: 0)
                    .shadow(color: Brand.neon.opacity(0.6), radius: 6)
                RoundedRectangle(cornerRadius: 10).strokeBorder(Color(.sRGB, red: 0.45, green: 0.8, blue: 1), lineWidth: 2)
                    .frame(width: 104, height: 84).offset(x: 50, y: 44)
            }
            Text("F = A'D + BD").font(.system(size: 17, weight: .bold, design: .monospaced)).foregroundStyle(BrandText.primary)
        }
    }

    /// The animated AND circuit over a timing diagram.
    private var signals: some View {
        VStack(spacing: 10) {
            HeroCircuit(accent: Brand.neon, ink: .white).frame(width: 300, height: 120)
            BrandCard(radius: 12) {
                Canvas { ctx, size in
                    let rows: [[Int]] = [[0, 0, 1, 1, 0, 0, 1, 1, 0, 0], [0, 1, 0, 1, 0, 1, 0, 1, 0, 1], [0, 0, 0, 1, 0, 0, 0, 1, 0, 0]]
                    for (i, bits) in rows.enumerated() {
                        let top = 14 + CGFloat(i) * 30, h: CGFloat = 16
                        var p = Path()
                        let step = (size.width - 20) / CGFloat(bits.count)
                        for (j, b) in bits.enumerated() {
                            let x = 10 + CGFloat(j) * step, y = b == 1 ? top : top + h
                            if j == 0 { p.move(to: CGPoint(x: x, y: y)) } else { p.addLine(to: CGPoint(x: x, y: y)) }
                            p.addLine(to: CGPoint(x: x + step, y: y))
                        }
                        ctx.stroke(p, with: .color(i == 2 ? Brand.neon : Color.white.opacity(0.55)), lineWidth: 1.6)
                    }
                }
                .frame(width: 300, height: 100)
            }
        }
    }

    private var palette: some View {
        VStack(spacing: 14) {
            HStack(spacing: 12) {
                ForEach(accentOrder, id: \.self) { i in
                    var r = 0.0, g = 0.0, b = 0.0
                    let _ = cl_accent_color(Int32(i), true, &r, &g, &b)
                    Circle().fill(Color(.sRGB, red: r, green: g, blue: b)).frame(width: i == brandAccent ? 34 : 22, height: i == brandAccent ? 34 : 22)
                        .shadow(color: i == brandAccent ? Brand.neon.opacity(0.8) : .clear, radius: 10)
                }
            }
            BrandCard(radius: 14) {
                VStack(alignment: .leading, spacing: 10) {
                    HStack(spacing: 10) {
                        RoundedRectangle(cornerRadius: 8).fill(Color.white.opacity(0.06)).frame(width: 36, height: 30)
                            .overlay(Image(systemName: "memorychip").foregroundStyle(Brand.neon))
                        Text("8x8 RAM").font(.system(size: 13, weight: .bold)).foregroundStyle(BrandText.primary)
                        Spacer()
                        Text("Done \u{21A9}").font(.system(size: 11, weight: .semibold)).foregroundStyle(Brand.ink)
                            .padding(.horizontal, 10).padding(.vertical, 5).background(Capsule().fill(Brand.neon))
                    }
                    HStack(spacing: 4) {
                        ForEach(["00", "07", "0E", "15", "1C", "23"], id: \.self) { v in
                            Text(v).font(.system(size: 11, design: .monospaced)).foregroundStyle(BrandText.primary)
                                .frame(width: 34, height: 22)
                                .background(RoundedRectangle(cornerRadius: 5).fill(v == "0E" ? Brand.neon.opacity(0.35) : Color.white.opacity(0.05)))
                        }
                    }
                }
                .padding(14)
            }
            .frame(width: 290)
        }
    }
}
