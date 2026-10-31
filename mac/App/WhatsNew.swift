// What's New: after an update, a walk through what's new since 0.3
// (Recently Deleted, Clear Drawing asking, the Practice Circuit, steadier
// clicks and wires, Check My Circuit, the lab report, Share Link, sync and
// scanning to add a device, the Keychain, the feedback limit), in the launch
// screen's look, a page a theme. Shown once per version (new installs get the
// welcome instead); Help > What's New in CedarLogic brings it back.

import AppKit
import SwiftUI

@MainActor
enum WhatsNew {
    /// Bumped when there's a new tour of what's new to give.
    static let version = "0.4"
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
        Chapter(eyebrow: "Your circuits", title: "Safe to try things",
                line: "Deleted a circuit or cleared a drawing by mistake? Both can come back now.",
                points: [
                    ("trash", "Recently Deleted", "In Your Circuits. Restore brings a circuit back. It's kept 30 days, on this Mac."),
                    ("scribble", "Clear Drawing asks first", "It says what will go, and that Edit \u{25B8} Undo brings it back."),
                    ("graduationcap", "Open the Practice Circuit", "In the File and Help menus. It opens as a new circuit of your own to play with."),
                ], tryTitle: "Open Your Circuits"),
        Chapter(eyebrow: "On the page", title: "The same every time",
                line: "Clicks and wires do what you expect, however big the circuit gets.",
                points: [
                    ("cursorarrow.click", "Clicks pick the same pin", "Where two pins sit close, or a wire turns a corner, you get the same one every time you open the app."),
                    ("hare", "Connect Nearby, quicker", "Holding C while you move parts, or Connect Nearby Pins, no longer stops to think on a big page."),
                ], tryTitle: nil),
        Chapter(eyebrow: "Check your work", title: "Check it, then hand it in",
                line: "Compare your circuit with the assignment, then hand in a PDF or send a link.",
                points: [
                    ("checkmark.seal", "Check My Circuit, \u{21E7}T", "Type the formulas or a minterm list, or paste the truth table you were given. Wrong rows show what was expected."),
                    ("doc.richtext", "Export Lab Report", "One PDF with your name, the circuit, its truth table, Karnaugh maps and timing diagram. You tick what goes in."),
                    ("link", "Share Link", "Also in the File menu. It copies a link that opens the circuit in any browser. The circuit is in the link; your notes stay out."),
                ], tryTitle: "Check My Circuit"),
        Chapter(eyebrow: "Sync and feedback", title: "On every device, kept safe",
                line: "Your circuits on all your devices, your codes locked away, and a fair share of reports.",
                points: [
                    ("arrow.triangle.2.circlepath", "Your circuits on every device", "Settings \u{25B8} Sync \u{25B8} Turn On Sync. There's no account: a code links your devices, and circuits are encrypted on this Mac first."),
                    ("qrcode.viewfinder", "Scan to add a device", "On the new one, Settings \u{25B8} Sync \u{25B8} I Have a Code shows a QR code. A phone that syncs scans it, or you type the code."),
                    ("key", "Your sync code is in the Keychain", "So are class keys. They used to sit in a file that backups carried along."),
                    ("exclamationmark.bubble", "Send Feedback: 10 a day", "Write a short summary and Send turns on. At the limit, Request more gives you 10 more, once a day."),
                ], tryTitle: nil),
    ]

    /// The intro, a page a chapter, and the finale.
    static var pageCount: Int { chapters.count + 2 }
    private var pages: Int { Self.pageCount }

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
                Text("Circuits you delete can come back, you can check your work and hand it in, and your circuits can follow you to your other devices. Here's what's new since 0.3, a couple of minutes' read.")
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
                    let noCircuit = Self.needsCircuit(index) && CanvasController.front == nil
                    Button(t) { tryIt(index) }.buttonStyle(BrandButtonStyle(primary: false))
                        .disabled(noCircuit)
                        .help(noCircuit ? "Open a circuit first." : "")
                        .padding(.top, 2)
                }
            }
            .frame(width: 420, alignment: .leading)
            ChapterArt(index: index).frame(maxWidth: .infinity).padding(.top, 50)
        }
        .padding(.horizontal, 48).padding(.top, 34)
    }

    /// Check My Circuit works on a circuit, so it needs one open.
    private static func needsCircuit(_ index: Int) -> Bool { index == 2 }

    private func tryIt(_ index: Int) {
        switch index {
        case 0: openWindow(id: "library")
        case 2:
            // The window comes forward so the check it starts is seen.
            guard let canvas = CanvasController.front else { return }
            canvas.view?.window?.makeKeyAndOrderFront(nil)
            canvas.perform(.checkCircuit)
        default: break
        }
    }

    private var finale: some View {
        VStack(alignment: .leading, spacing: 12) {
            BrandHeading(eyebrow: "That's the tour", title: "Go build something",
                         line: "It's under Help \u{25B8} What's New in CedarLogic whenever you want it back.")
                .padding(.bottom, 8)
            finaleTile("Open the Practice Circuit", "A circuit of your own to try things on.", "graduationcap") {
                finish(); Templates.openPractice()
            }
            finaleTile("Turn on sync", "Settings \u{25B8} Sync. Your circuits on every device.", "arrow.triangle.2.circlepath") {
                finish(); PrefsWindow.shared.show(page: .sync)
            }
            finaleTile("Take the guided tour again", "Two switches, a gate and a light, on a circuit of its own.", "figure.walk") {
                finish(); TourModel.shared.start()
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
        case 0: deleted
        case 1: pins
        case 2: check
        default: sync
        }
    }

    private static let wrong = Color(.sRGB, red: 1, green: 0.5, blue: 0.42)

    private func caption(_ icon: String, _ text: String) -> some View {
        HStack(spacing: 6) {
            Image(systemName: icon).font(.system(size: 10, weight: .semibold))
            Text(text).font(.system(size: 9, weight: .bold)).kerning(0.8)
        }
        .foregroundStyle(Brand.neon)
    }

    private func pill(_ text: String, filled: Bool) -> some View {
        Text(text).font(.system(size: 10, weight: .semibold))
            .foregroundStyle(filled ? Brand.ink : BrandText.primary)
            .padding(.horizontal, 10).frame(height: 22)
            .background(Capsule().fill(filled ? Brand.neon : Color.white.opacity(0.1)))
    }

    // MARK: Recently Deleted and Clear Drawing

    private func row(_ name: String, _ detail: String, lit: Bool) -> some View {
        BrandCard(lit: lit, radius: 11) {
            HStack(spacing: 12) {
                RoundedRectangle(cornerRadius: 7).fill(Brand.neon.opacity(0.16)).frame(width: 30, height: 30)
                    .overlay(Image(systemName: "cpu").font(.system(size: 13)).foregroundStyle(Brand.neon))
                VStack(alignment: .leading, spacing: 2) {
                    Text(name).font(.system(size: 12, weight: .bold)).foregroundStyle(BrandText.primary)
                    Text(detail).font(.system(size: 10)).foregroundStyle(BrandText.faint)
                }
                Spacer()
                if lit { pill("Restore", filled: true) }
            }
            .padding(.horizontal, 12).frame(height: 50)
        }
    }

    private var deleted: some View {
        VStack(spacing: 8) {
            caption("trash", "RECENTLY DELETED \u{00B7} KEPT 30 DAYS").padding(.bottom, 4)
            row("Lab 5: Traffic Light", "Deleted today", lit: true)
            row("Full Adder", "Deleted yesterday", lit: false)
            BrandCard(radius: 12) {
                VStack(alignment: .leading, spacing: 6) {
                    Text("Clear all drawing on this page?").font(.system(size: 12, weight: .bold)).foregroundStyle(BrandText.primary)
                    Text("Edit \u{25B8} Undo brings it back.").font(.system(size: 10.5)).foregroundStyle(BrandText.secondary)
                    HStack(spacing: 8) {
                        Spacer()
                        pill("Cancel", filled: false)
                        pill("Clear", filled: true)
                    }
                    .padding(.top, 4)
                }
                .padding(14)
            }
            .padding(.top, 10)
        }
        .frame(width: 290)
    }

    // MARK: Clicks, wires and Connect Nearby

    /// A gate with two inputs close together: the wire to the top one, round
    /// its corner, is the one the pointer picks, every time.
    private var pins: some View {
        VStack(spacing: 14) {
            BrandCard(radius: 14) {
                Canvas { ctx, size in
                    let top = CGFloat(66), bottom = CGFloat(84), pinX = CGFloat(168)
                    // The gate: flat back, round front.
                    var body = Path()
                    body.move(to: CGPoint(x: 178, y: 50))
                    body.addLine(to: CGPoint(x: 206, y: 50))
                    body.addArc(center: CGPoint(x: 206, y: 75), radius: 25, startAngle: .degrees(-90), endAngle: .degrees(90), clockwise: false)
                    body.addLine(to: CGPoint(x: 178, y: 100))
                    body.closeSubpath()
                    ctx.fill(body, with: .color(Color.white.opacity(0.06)))
                    ctx.stroke(body, with: .color(Color.white.opacity(0.7)), lineWidth: 1.6)
                    var out = Path(); out.move(to: CGPoint(x: 231, y: 75)); out.addLine(to: CGPoint(x: 262, y: 75))
                    ctx.stroke(out, with: .color(Color.white.opacity(0.7)), lineWidth: 1.6)
                    // The other wire, plain.
                    var other = Path()
                    other.move(to: CGPoint(x: 24, y: 128)); other.addLine(to: CGPoint(x: 120, y: 128))
                    other.addLine(to: CGPoint(x: 120, y: bottom)); other.addLine(to: CGPoint(x: 178, y: bottom))
                    ctx.stroke(other, with: .color(Color.white.opacity(0.45)), lineWidth: 1.6)
                    // The picked wire, lit.
                    var picked = Path()
                    picked.move(to: CGPoint(x: 24, y: 24)); picked.addLine(to: CGPoint(x: 96, y: 24))
                    picked.addLine(to: CGPoint(x: 96, y: top)); picked.addLine(to: CGPoint(x: 178, y: top))
                    var glow = ctx
                    glow.addFilter(.blur(radius: 4))
                    glow.stroke(picked, with: .color(Brand.neon.opacity(0.7)), lineWidth: 4)
                    ctx.stroke(picked, with: .color(Brand.neon), lineWidth: 2)
                    for (p, lit) in [(CGPoint(x: pinX, y: top), true), (CGPoint(x: pinX, y: bottom), false)] {
                        let r = CGRect(x: p.x - 3.5, y: p.y - 3.5, width: 7, height: 7)
                        ctx.fill(Path(ellipseIn: r), with: .color(lit ? Brand.neon : Color.white.opacity(0.55)))
                    }
                    ctx.fill(Path(ellipseIn: CGRect(x: 92, y: 20, width: 8, height: 8)), with: .color(Brand.neon))
                }
                .frame(width: 290, height: 150)
                .overlay(alignment: .topLeading) {
                    Image(systemName: "cursorarrow").font(.system(size: 20)).foregroundStyle(.white)
                        .shadow(color: .black.opacity(0.6), radius: 3)
                        .offset(x: 166, y: 64)
                }
            }
            BrandCard(lit: true, radius: 12) {
                HStack(spacing: 12) {
                    Text("C").font(.system(size: 15, weight: .bold, design: .rounded)).foregroundStyle(BrandText.primary)
                        .frame(width: 32, height: 32)
                        .background(RoundedRectangle(cornerRadius: 7).fill(Color.white.opacity(0.1)))
                        .overlay(RoundedRectangle(cornerRadius: 7).strokeBorder(Color.white.opacity(0.25), lineWidth: 1))
                    VStack(alignment: .leading, spacing: 2) {
                        Text("Connect Nearby").font(.system(size: 12, weight: .bold)).foregroundStyle(BrandText.primary)
                        Text("Thousands of parts, no waiting").font(.system(size: 10)).foregroundStyle(BrandText.faint)
                    }
                    Spacer()
                    Image(systemName: "hare.fill").font(.system(size: 15)).foregroundStyle(Brand.neon)
                }
                .padding(.horizontal, 14).frame(width: 290, height: 56)
            }
        }
    }

    // MARK: Check My Circuit, the lab report and Share Link

    /// The asked-for formula against the circuit's table, one row wrong, then
    /// the two ways to hand it over.
    private var check: some View {
        let rows: [(String, String, String, Bool)] = [("0 0", "0", "0", true), ("0 1", "1", "1", true), ("1 0", "1", "0", false), ("1 1", "0", "0", true)]
        return VStack(spacing: 10) {
            caption("checkmark.seal", "CHECK MY CIRCUIT \u{00B7} \u{21E7}T").padding(.bottom, 2)
            BrandCard(radius: 10) {
                Text("S = A ^ B").font(.system(size: 14, weight: .semibold, design: .monospaced)).foregroundStyle(BrandText.primary)
                    .frame(width: 290, height: 34)
            }
            BrandCard(radius: 12) {
                VStack(spacing: 4) {
                    HStack {
                        Text("A B").frame(width: 70, alignment: .leading)
                        Text("Asked").frame(width: 56)
                        Text("Circuit").frame(width: 56)
                        Spacer()
                    }
                    .font(.system(size: 10, weight: .bold)).foregroundStyle(BrandText.faint)
                    ForEach(rows, id: \.0) { r in
                        HStack {
                            Text(r.0).frame(width: 70, alignment: .leading)
                            Text(r.1).frame(width: 56)
                            Text(r.2).frame(width: 56)
                            Spacer()
                            Image(systemName: r.3 ? "checkmark" : "xmark").font(.system(size: 11, weight: .bold))
                                .foregroundStyle(r.3 ? Brand.neon : Self.wrong)
                        }
                        .font(.system(size: 12, weight: .semibold, design: .monospaced))
                        .foregroundStyle(r.3 ? BrandText.primary : Self.wrong)
                        .padding(.horizontal, 8).frame(height: 24)
                        .background(RoundedRectangle(cornerRadius: 6).fill(r.3 ? Color.white.opacity(0.04) : Self.wrong.opacity(0.14)))
                    }
                }
                .padding(12).frame(width: 290)
            }
            HStack(spacing: 10) {
                handIn("doc.richtext", "Lab Report", "One PDF", lit: true)
                handIn("link", "Share Link", "In a browser", lit: false)
            }
            .padding(.top, 4)
        }
        .frame(width: 290)
    }

    private func handIn(_ icon: String, _ title: String, _ line: String, lit: Bool) -> some View {
        BrandCard(lit: lit, radius: 12) {
            HStack(spacing: 10) {
                RoundedRectangle(cornerRadius: 7).fill(Brand.neon.opacity(0.16)).frame(width: 28, height: 28)
                    .overlay(Image(systemName: icon).font(.system(size: 12)).foregroundStyle(Brand.neon))
                VStack(alignment: .leading, spacing: 2) {
                    Text(title).font(.system(size: 11.5, weight: .bold)).foregroundStyle(BrandText.primary)
                    Text(line).font(.system(size: 9.5)).foregroundStyle(BrandText.faint).lineLimit(1)
                }
                Spacer(minLength: 0)
            }
            .padding(.horizontal, 10).frame(width: 140, height: 50)
        }
    }

    // MARK: Sync, the Keychain and feedback

    private func device(_ icon: String, _ name: String, _ detail: String, lit: Bool = false) -> some View {
        BrandCard(lit: lit, radius: 11) {
            HStack(spacing: 12) {
                RoundedRectangle(cornerRadius: 7).fill(Brand.neon.opacity(0.16)).frame(width: 28, height: 28)
                    .overlay(Image(systemName: icon).font(.system(size: 12)).foregroundStyle(Brand.neon))
                VStack(alignment: .leading, spacing: 2) {
                    Text(name).font(.system(size: 12, weight: .bold)).foregroundStyle(BrandText.primary)
                    Text(detail).font(.system(size: 10)).foregroundStyle(BrandText.faint)
                }
                Spacer()
                Image(systemName: "checkmark.circle.fill").font(.system(size: 13)).foregroundStyle(Brand.neon)
            }
            .padding(.horizontal, 12).frame(width: 290, height: 44)
        }
    }

    private var sync: some View {
        VStack(spacing: 7) {
            caption("lock.fill", "ENCRYPTED \u{00B7} SAME CIRCUITS EVERYWHERE").padding(.bottom, 3)
            device("laptopcomputer", "This Mac", "Lab 5: Traffic Light \u{00B7} just now", lit: true)
            device("iphone", "Phone", "Up to date")
            device("globe", "CedarLogic Online", "Up to date")
            BrandCard(lit: true, radius: 12) {
                HStack(spacing: 12) {
                    RoundedRectangle(cornerRadius: 7).fill(Brand.neon.opacity(0.16)).frame(width: 28, height: 28)
                        .overlay(Image(systemName: "key.fill").font(.system(size: 13)).foregroundStyle(Brand.neon))
                    VStack(alignment: .leading, spacing: 2) {
                        Text("Sync code").font(.system(size: 12, weight: .bold)).foregroundStyle(BrandText.primary)
                        Text("In your login Keychain").font(.system(size: 10)).foregroundStyle(BrandText.faint)
                    }
                    Spacer()
                    Image(systemName: "lock.fill").font(.system(size: 13)).foregroundStyle(Brand.neon)
                }
                .padding(.horizontal, 12).frame(width: 290, height: 48)
            }
            .padding(.top, 5)
            BrandCard(radius: 12) {
                VStack(alignment: .leading, spacing: 8) {
                    HStack(spacing: 8) {
                        Image(systemName: "exclamationmark.bubble.fill").font(.system(size: 13)).foregroundStyle(Brand.neon)
                        Text("Send Feedback").font(.system(size: 12, weight: .bold)).foregroundStyle(BrandText.primary)
                        Spacer()
                        Text("10 of 10 today").font(.system(size: 10, weight: .semibold)).foregroundStyle(BrandText.faint)
                    }
                    HStack(spacing: 3) {
                        ForEach(0..<10, id: \.self) { _ in
                            Capsule().fill(Brand.neon.opacity(0.85)).frame(height: 5)
                        }
                    }
                    HStack {
                        Text("Need to send more today?").font(.system(size: 10.5)).foregroundStyle(BrandText.secondary)
                        Spacer()
                        pill("Request more", filled: true)
                    }
                }
                .padding(12).frame(width: 290)
            }
            .padding(.top, 5)
        }
        .frame(width: 290)
    }
}
