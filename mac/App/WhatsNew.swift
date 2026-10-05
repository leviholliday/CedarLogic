// What's New: after an update, a walk through what's new since 0.3 (sync,
// Check My Circuit, the lab report, Share Link, the rebuilt Full Rearrange),
// in the launch screen's look, a page a theme, most with a way to try it.
// Shown once per version (new installs get the welcome instead); Help >
// What's New in CedarLogic brings it back.

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
        Chapter(eyebrow: "Sync", title: "Your circuits, on every device",
                line: "Settings \u{25B8} Sync \u{25B8} Turn On Sync, and a circuit you change here is there too.",
                points: [
                    ("lock.shield", "Private, with no account", "Turn On Sync gives you a sync code. Circuits are encrypted on this Mac first, so the website can't read them."),
                    ("laptopcomputer.and.iphone", "Every device", "Macs, Windows PCs, Raspberry Pis, CedarLogic Online and the phone app."),
                    ("arrow.triangle.branch", "Nothing gets lost", "Edited in two places? Both are kept: the other one is in Version History. Deletes go to Recently Deleted."),
                ], tryTitle: "Open Sync Settings"),
        Chapter(eyebrow: "Add a device", title: "Scan instead of typing",
                line: "A new device gets the code from a phone that already syncs.",
                points: [
                    ("qrcode", "Show a QR code", "On the new device, Settings \u{25B8} Sync \u{25B8} I Have a Code shows one."),
                    ("qrcode.viewfinder", "Scan it with your phone", "In the phone app: Your Circuits \u{25B8} Sync \u{25B8} Add a Device. The new device gets the code."),
                    ("keyboard", "Typing still works", "Or type or paste the code from another device's Settings \u{25B8} Sync \u{25B8} Show Code."),
                ], tryTitle: nil),
        Chapter(eyebrow: "Check your work", title: "Check My Circuit",
                line: "Compare your circuit with what the assignment asks for. Press \u{21E7}T.",
                points: [
                    ("doc.text", "Type what you were given", "Formulas like S = A ^ B ^ Cin, a minterm list, or a truth table pasted in."),
                    ("link", "Matched by name", "Switches and lights are paired by name. Pick by hand under Names when they differ."),
                    ("xmark.circle", "Wrong rows, shown", "Each one with what was expected and what the circuit gave. Don't-cares are skipped."),
                ], tryTitle: "Check My Circuit"),
        Chapter(eyebrow: "Hand it in", title: "Export Lab Report",
                line: "File \u{25B8} Export Lab Report makes one PDF with everything the lab asks for.",
                points: [
                    ("doc.richtext", "A page that's ready", "Your name, the circuit and the date, then a picture of each page of the circuit."),
                    ("tablecells", "Your analysis too", "The truth table, a Karnaugh map and the simplest formulas for each light, and the timing diagram."),
                    ("checklist", "You choose", "Tick what goes in, in colour or black and white. Headings stay with their content."),
                ], tryTitle: "Export a Lab Report"),
        Chapter(eyebrow: "Show someone", title: "Share Link",
                line: "File \u{25B8} Share Link copies a link to the circuit you have open.",
                points: [
                    ("globe", "Opens in any browser", "In CedarLogic Online. Nothing to install."),
                    ("app.badge.checkmark", "Or in CedarLogic", "A cedarlogic:// link comes in as a new circuit in Your Circuits."),
                    ("lock.open", "Nothing is stored", "The circuit is in the link itself. One too big for a link says so, and you can send a file instead."),
                ], tryTitle: "Copy a Share Link"),
        Chapter(eyebrow: "Tidy Up", title: "Full Rearrange, rebuilt",
                line: "Edit \u{25B8} Tidy Up: Full Rearrange lines up parts and reroutes their wires.",
                points: [
                    ("square.grid.3x3", "Cleaner layouts", "It keeps groups you drew together, labels in order and straight wires straight."),
                    ("hare", "Fast on big pages", "A page of hundreds of parts takes about a second."),
                    ("eye", "A preview first", "Return keeps it, Escape puts it back, Tab tries the other kind of tidy."),
                ], tryTitle: "Try Full Rearrange"),
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
                Text("Your circuits can follow you to your other devices, and there's more to help you check and hand in your work. Here's what's new since 0.3, a minute's read.")
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
            ChapterArt(index: index).frame(width: 270).padding(.top, 50)
        }
        .padding(.horizontal, 48).padding(.top, 34)
    }

    /// The tries that work on a circuit need one open; the window comes forward
    /// so the sheet or preview they start is seen.
    private static func needsCircuit(_ index: Int) -> Bool { index >= 2 }

    private func tryIt(_ index: Int) {
        if index == 0 { PrefsWindow.shared.show(page: .sync); return }
        guard let canvas = CanvasController.front else { return }
        canvas.view?.window?.makeKeyAndOrderFront(nil)
        switch index {
        case 2: canvas.perform(.checkCircuit)
        case 3: canvas.perform(.exportReport)
        case 4: ShareLink.copy(from: canvas)
        case 5: canvas.tidy(mode: 1)
        default: break
        }
    }

    private var finale: some View {
        VStack(alignment: .leading, spacing: 12) {
            BrandHeading(eyebrow: "That's the tour", title: "Go build something",
                         line: "It's under Help \u{25B8} What's New in CedarLogic whenever you want it back.")
                .padding(.bottom, 8)
            finaleTile("Turn on sync", "Settings \u{25B8} Sync. Your circuits on every device.", "arrow.triangle.2.circlepath") {
                finish(); PrefsWindow.shared.show(page: .sync)
            }
            finaleTile("Take the guided tour again", "Two switches, a gate and a light, on a circuit of its own.", "figure.walk") {
                finish(); TourModel.shared.start()
            }
            finaleTile("Open CedarLogic Help", "The features and their keys.", "questionmark.circle") {
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

    private static let wrong = Color(.sRGB, red: 1, green: 0.5, blue: 0.42)

    var body: some View {
        switch index {
        case 0: devices
        case 1: scan
        case 2: check
        case 3: report
        case 4: link
        default: tidy
        }
    }

    // MARK: Sync

    private func device(_ icon: String, _ name: String, _ detail: String, lit: Bool = false) -> some View {
        BrandCard(lit: lit, radius: 11) {
            HStack(spacing: 12) {
                RoundedRectangle(cornerRadius: 7).fill(Brand.neon.opacity(0.16)).frame(width: 30, height: 30)
                    .overlay(Image(systemName: icon).font(.system(size: 13)).foregroundStyle(Brand.neon))
                VStack(alignment: .leading, spacing: 2) {
                    Text(name).font(.system(size: 12, weight: .bold)).foregroundStyle(BrandText.primary)
                    Text(detail).font(.system(size: 10)).foregroundStyle(BrandText.faint)
                }
                Spacer()
                Image(systemName: "checkmark.circle.fill").font(.system(size: 14)).foregroundStyle(Brand.neon)
            }
            .padding(.horizontal, 12).frame(height: 50)
        }
    }

    private var devices: some View {
        VStack(spacing: 8) {
            HStack(spacing: 6) {
                Image(systemName: "lock.fill").font(.system(size: 10, weight: .semibold))
                Text("ENCRYPTED \u{00B7} SAME CIRCUITS EVERYWHERE").font(.system(size: 9, weight: .bold)).kerning(0.8)
            }
            .foregroundStyle(Brand.neon).padding(.bottom, 4)
            device("laptopcomputer", "This Mac", "Lab 5: Traffic Light \u{00B7} just now", lit: true)
            device("desktopcomputer", "Windows PC", "Up to date")
            device("cpu", "Raspberry Pi", "Up to date")
            device("iphone", "Phone", "Up to date")
            device("globe", "CedarLogic Online", "Up to date")
        }
        .frame(width: 290)
    }

    // MARK: Scan

    /// A QR-looking square: three finder squares and a fixed scatter of dots
    /// (a picture of a code, not one that scans).
    private var scan: some View {
        VStack(spacing: 14) {
            ZStack {
                RoundedRectangle(cornerRadius: 12).fill(Color.white).frame(width: 190, height: 190)
                Canvas { ctx, size in
                    let n = 21, cell = size.width / CGFloat(n)
                    func finder(_ x: Int, _ y: Int) {
                        for (inset, black) in [(0, true), (1, false), (2, true)] {
                            let side = 7 - 2 * inset
                            let r = CGRect(x: CGFloat(x + inset) * cell, y: CGFloat(y + inset) * cell,
                                           width: CGFloat(side) * cell, height: CGFloat(side) * cell)
                            ctx.fill(Path(r), with: .color(black ? .black : .white))
                        }
                    }
                    var seed: UInt32 = 20_26
                    for y in 0..<n { for x in 0..<n {
                        seed = seed &* 1_664_525 &+ 1_013_904_223
                        let inFinder = (x < 8 && y < 8) || (x > 12 && y < 8) || (x < 8 && y > 12)
                        if !inFinder, (seed >> 16) % 2 == 0 {
                            ctx.fill(Path(CGRect(x: CGFloat(x) * cell, y: CGFloat(y) * cell, width: cell, height: cell)), with: .color(.black))
                        }
                    } }
                    finder(0, 0); finder(14, 0); finder(0, 14)
                }
                .frame(width: 168, height: 168)
            }
            .shadow(color: Brand.neon.opacity(0.25), radius: 14)
            HStack(spacing: 8) {
                Image(systemName: "iphone").font(.system(size: 13)).foregroundStyle(Brand.neon)
                Text("Scan with your phone").font(.system(size: 12, weight: .semibold)).foregroundStyle(BrandText.primary)
            }
        }
    }

    // MARK: Check

    private var check: some View {
        let rows: [(String, String, String, Bool)] = [("0 0", "0", "0", true), ("0 1", "1", "1", true), ("1 0", "1", "0", false), ("1 1", "0", "0", true)]
        return VStack(spacing: 12) {
            BrandCard(radius: 10) {
                Text("S = A ^ B").font(.system(size: 14, weight: .semibold, design: .monospaced)).foregroundStyle(BrandText.primary)
                    .frame(width: 270, height: 36)
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
                        .padding(.horizontal, 8).frame(height: 26)
                        .background(RoundedRectangle(cornerRadius: 6).fill(r.3 ? Color.white.opacity(0.04) : Self.wrong.opacity(0.14)))
                    }
                }
                .padding(12).frame(width: 270)
            }
            Text("1 row is wrong").font(.system(size: 11, weight: .bold)).foregroundStyle(Self.wrong)
        }
    }

    // MARK: Lab report

    /// A page of the PDF: heading, name and date, the circuit, a table.
    private var report: some View {
        ZStack(alignment: .topLeading) {
            RoundedRectangle(cornerRadius: 6).fill(Color.white)
                .shadow(color: .black.opacity(0.5), radius: 14, y: 8)
            VStack(alignment: .leading, spacing: 7) {
                Text("Lab 5: Traffic Light").font(.system(size: 14, weight: .semibold)).foregroundStyle(.black)
                Text("Lab report").font(.system(size: 8)).foregroundStyle(Color(white: 0.4))
                Rectangle().fill(Color.black).frame(height: 1)
                HStack {
                    Text("Name:").font(.system(size: 7.5)).foregroundStyle(Color(white: 0.4))
                    Rectangle().fill(Color(white: 0.5)).frame(width: 70, height: 0.6).offset(y: 4)
                    Spacer()
                    Text("Date: Oct 5, 2026").font(.system(size: 7.5)).foregroundStyle(Color(white: 0.4)).lineLimit(1)
                }
                Text("Circuit").font(.system(size: 9, weight: .semibold)).foregroundStyle(.black).padding(.top, 4)
                HeroCircuit(accent: Color(.sRGB, red: 0.1, green: 0.55, blue: 0.3), ink: Color(white: 0.2))
                    .frame(height: 64).overlay(Rectangle().strokeBorder(Color(white: 0.82), lineWidth: 0.5))
                Text("Truth table").font(.system(size: 9, weight: .semibold)).foregroundStyle(.black).padding(.top, 2)
                VStack(spacing: 0) {
                    ForEach(0..<4, id: \.self) { i in
                        Rectangle().fill(i == 0 ? Color(white: 0.85) : Color(white: i % 2 == 0 ? 0.96 : 1)).frame(height: 10)
                    }
                }
                .overlay(Rectangle().strokeBorder(Color(white: 0.75), lineWidth: 0.5))
            }
            .padding(16)
        }
        .frame(width: 210, height: 290)
        .overlay(alignment: .bottomTrailing) {
            Text("PDF").font(.system(size: 10, weight: .bold)).foregroundStyle(Brand.ink)
                .padding(.horizontal, 10).frame(height: 22).background(Capsule().fill(Brand.neon))
                .offset(x: 10, y: 10)
        }
    }

    // MARK: Share link

    private var link: some View {
        VStack(spacing: 14) {
            BrandCard(lit: true, radius: 12) {
                HStack(spacing: 8) {
                    Image(systemName: "link").font(.system(size: 13, weight: .semibold)).foregroundStyle(Brand.neon)
                    Text("cedarlogic.netlify.app/\u{2026}#c=\u{2026}").font(.system(size: 11, design: .monospaced))
                        .foregroundStyle(BrandText.primary).lineLimit(1)
                    Spacer(minLength: 0)
                }
                .padding(.horizontal, 12).frame(width: 290, height: 42)
            }
            Image(systemName: "arrow.down").foregroundStyle(BrandText.faint)
            HStack(spacing: 12) {
                destination("globe", "In a browser", "CedarLogic Online")
                destination("cpu", "In the app", "cedarlogic://")
            }
        }
    }

    private func destination(_ icon: String, _ title: String, _ line: String) -> some View {
        BrandCard(radius: 12) {
            VStack(spacing: 7) {
                Image(systemName: icon).font(.system(size: 22, weight: .light)).foregroundStyle(Brand.neon)
                Text(title).font(.system(size: 11.5, weight: .bold)).foregroundStyle(BrandText.primary)
                Text(line).font(.system(size: 10)).foregroundStyle(BrandText.faint)
            }
            .frame(width: 139, height: 92)
        }
    }

    // MARK: Tidy

    /// Gates as boxes, wires as lines: tangled on the left, ordered on the right.
    private var tidy: some View {
        VStack(spacing: 10) {
            HStack(spacing: 14) {
                layout(tidied: false)
                Image(systemName: "arrow.right").foregroundStyle(Brand.neon)
                layout(tidied: true)
            }
            HStack(spacing: 0) {
                Text("Before").frame(width: 130)
                Spacer().frame(width: 40)
                Text("After").frame(width: 130)
            }
            .font(.system(size: 10, weight: .semibold)).foregroundStyle(BrandText.faint)
        }
    }

    private func layout(tidied: Bool) -> some View {
        BrandCard(radius: 12) {
            Canvas { ctx, size in
                // Where each gate sits; the wires go from the first three to the next two, then the last.
                let tangled: [CGPoint] = [CGPoint(x: 20, y: 90), CGPoint(x: 80, y: 20), CGPoint(x: 30, y: 30),
                                          CGPoint(x: 70, y: 100), CGPoint(x: 20, y: 60), CGPoint(x: 90, y: 60)]
                let neat: [CGPoint] = [CGPoint(x: 14, y: 22), CGPoint(x: 14, y: 54), CGPoint(x: 14, y: 86),
                                       CGPoint(x: 56, y: 38), CGPoint(x: 56, y: 70), CGPoint(x: 98, y: 54)]
                let p = tidied ? neat : tangled
                let w: CGFloat = 22, h: CGFloat = 16
                for (a, b) in [(0, 3), (1, 3), (1, 4), (2, 4), (3, 5), (4, 5)] {
                    let s = CGPoint(x: p[a].x + w, y: p[a].y + h / 2), e = CGPoint(x: p[b].x, y: p[b].y + h / 2)
                    var path = Path(); path.move(to: s)
                    if tidied { let mx = (s.x + e.x) / 2; path.addLine(to: CGPoint(x: mx, y: s.y)); path.addLine(to: CGPoint(x: mx, y: e.y)) }
                    path.addLine(to: e)
                    ctx.stroke(path, with: .color(tidied ? Brand.neon : Color.white.opacity(0.45)), lineWidth: 1.4)
                }
                for pt in p {
                    ctx.fill(Path(roundedRect: CGRect(x: pt.x, y: pt.y, width: w, height: h), cornerRadius: 4), with: .color(Color.white.opacity(0.14)))
                    ctx.stroke(Path(roundedRect: CGRect(x: pt.x, y: pt.y, width: w, height: h), cornerRadius: 4), with: .color(Color.white.opacity(0.5)), lineWidth: 1)
                }
            }
            .frame(width: 130, height: 126)
        }
    }
}
