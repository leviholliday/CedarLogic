// One consent for crash reports (CrashReports.swift) and usage stats
// (UsageStats.swift), asked once: on the welcome's "A small favour" page for
// a new install, or as a sheet over the circuit window for everyone else
// (after What's New, before Show Me Around). Settings > Privacy changes it
// later, shows what's sent and deletes it. A lab's policy keys
// (DisableCrashReporting, DisableUsageStats) switch either off for everyone
// and grey it out. docs/PRIVACY.md has the same words as "See exactly what's sent".

import AppKit
import SwiftUI

enum PrivacyChoice: String { case yes, no, custom }

@MainActor
enum PrivacyConsent {
    static let askedKey = "cl.privacyAsked"
    static let choiceKey = "cl.privacyChoice"
    static let crashPolicyKey = "DisableCrashReporting"

    static var asked: Bool { UserDefaults.standard.bool(forKey: askedKey) }
    /// What was chosen: yes, no or custom (nil: not asked yet).
    static var choice: PrivacyChoice? { UserDefaults.standard.string(forKey: choiceKey).flatMap(PrivacyChoice.init(rawValue:)) }
    static var crashPolicyOff: Bool { UserDefaults.standard.bool(forKey: crashPolicyKey) }
    static var crashOffered: Bool { CrashReports.available }
    static var usageOffered: Bool { !UsageStats.shared.policyOff }
    static var anythingToAsk: Bool { crashOffered || usageOffered }

    /// The answer, from the welcome, the sheet or Settings. "No thanks"
    /// leaves crash reports on for someone who'd said yes to them before.
    static func record(_ choice: PrivacyChoice, crash: Bool, usage: Bool) {
        let d = UserDefaults.standard
        if crashOffered && CrashReports.allowed != crash { CrashReports.set(crash) }
        if usageOffered && UsageStats.shared.on != usage { UsageStats.shared.set(usage) }
        d.set(true, forKey: askedKey)
        d.set(choice.rawValue, forKey: choiceKey)
    }
    /// Yes, No thanks, or the two switches as they're set.
    static func answer(_ choice: PrivacyChoice, crash: Bool = false, usage: Bool = false) {
        switch choice {
        case .yes: record(.yes, crash: true, usage: true)
        case .no: record(.no, crash: CrashReports.allowed ?? false, usage: false)
        case .custom: record(.custom, crash: crash, usage: usage)
        }
    }

    /// The sheet is owed: someone past the welcome who hasn't been asked.
    static var owed: Bool { Prefs.shared.hasSeenWelcome && !asked && anythingToAsk }

    // MARK: The sheet

    private static var timer: Timer?
    private static var readySince: Date?
    private static var sheet: NSWindow?
    private static weak var sheetParent: NSWindow?
    static var showing: Bool { sheet != nil }
    /// Show Me Around waits for this: the question comes first.
    static var holdsShowMeAround: Bool { showing || (owed && timer != nil) }

    /// At launch (and as the welcome closes): watches for the moment a
    /// circuit window has the keys, with no launch screen, welcome, What's
    /// New or other sheet over it, and asks there. Never in test and picture
    /// runs (the launch focus check does, as a real launch would).
    static func askWhenReady() {
        guard !asked, anythingToAsk, !ShowMeAround.quiet, timer == nil, sheet == nil else { return }
        readySince = nil
        let t = Timer(timeInterval: 0.3, repeats: true) { _ in MainActor.assumeIsolated { PrivacyConsent.tick() } }
        RunLoop.main.add(t, forMode: .common)
        timer = t
    }

    private static func stopWatching() { timer?.invalidate(); timer = nil }

    private static func tick() {
        guard !asked, anythingToAsk else { stopWatching(); return }
        guard owed, let w = readyWindow() else { readySince = nil; return }
        if readySince == nil { readySince = Date(); return }
        guard Date().timeIntervalSince(readySince!) > 0.6 else { return }
        stopWatching()
        present(on: w)
    }

    static func readyWindow() -> NSWindow? {
        guard NSApp.isActive, !Splash.shared.showing, !TourModel.shared.active, NSApp.modalWindow == nil, Greeting.due == nil else { return nil }
        let greeting = NSApp.windows.contains { o in o.isVisible && Greeting.ids.contains { o.identifier?.rawValue.contains($0) == true } }
        guard !greeting, !ShowMeAround.shared.active, let c = CanvasController.front, let w = c.view?.window,
              w.isVisible, w.isKeyWindow, w.attachedSheet == nil else { return nil }
        return w
    }

    static func present(on w: NSWindow) {
        guard sheet == nil else { return }
        let host = NSHostingController(rootView: PrivacyAskSheet())
        host.sizingOptions = [.preferredContentSize]
        let s = NSWindow(contentViewController: host)
        s.identifier = NSUserInterfaceItemIdentifier("cl.privacyAsk")
        s.appearance = NSAppearance(named: .darkAqua)
        sheet = s
        sheetParent = w
        w.beginSheet(s)
    }

    /// The sheet's buttons.
    static func sheetAnswered(_ choice: PrivacyChoice, crash: Bool, usage: Bool) {
        answer(choice, crash: crash, usage: usage)
        if let s = sheet { (sheetParent ?? s.sheetParent)?.endSheet(s); s.orderOut(nil) }
        sheet = nil
        ShowMeAround.shared.startWhenReady()
    }
}

// MARK: - The words

enum PrivacyCopy {
    static let eyebrow = "Help improve CedarLogic"
    static let title = "A small favour"
    static let note = "CedarLogic is built by one person, in spare time, for free. I can't see what breaks or what people actually use unless you tell me. If you're willing, let the app send anonymous crash reports and usage stats. It would genuinely help, and nothing identifies you: no name, no circuits, no files. Thank you either way."
    static let signature = "The person who makes CedarLogic"
    static let yes = "Yes, send anonymous reports"
    static let no = "No thanks"
    static let later = "You can change this any time in Settings > Privacy."

    static let crashLine = "If CedarLogic crashes: where in the app it went wrong (the code's stack), the macOS and CedarLogic versions. Paths in it lose your user name. Sent through Sentry, only after a crash."
    static let usageLine = "At most once a day: how often CedarLogic is opened and for how long, the time spent in focus mode and Simulation View, how often Classroom is opened, whether sync is on, how many times each command is used, and which settings you've changed from their defaults. Only what's new since the last report goes; when nothing is, a short \"still here\"."
    static let never: [String] = [
        "Your name, or the name on your exports",
        "Your circuits: their names, contents, notes or drawings",
        "File names, folders or anything you type",
        "Your sync code, classroom codes, class names or students",
        "Your Mac's name, your user name, its serial number or IP address (the server keeps no IP)",
        "Your time zone or exact times (dates, and the minute in UTC)",
    ]
    static let idLine = "Each report carries a random id, made when you turn this on (nothing about your Mac). Turning it off, or Delete What's Been Sent, deletes everything sent under it."
}

// MARK: - The ask (welcome page and sheet)

/// The note, the two equal buttons, and the finer choice under them.
struct PrivacyAskView: View {
    /// Done: what was chosen (the welcome moves on; the sheet closes).
    let onAnswer: (PrivacyChoice, Bool, Bool) -> Void
    var answered: PrivacyChoice? = nil
    var compact = false
    /// The two switches showing ("Choose what to send").
    @State var choosing = false
    @State private var crash = CrashReports.allowed ?? false
    @State private var usage = UsageStats.shared.on
    @State private var showSent = false
    @State private var glow = false

    var body: some View {
        VStack(alignment: .leading, spacing: compact ? 14 : 18) {
            VStack(alignment: .leading, spacing: 8) {
                Text(PrivacyCopy.eyebrow.uppercased()).font(.system(size: 11, weight: .bold)).kerning(1.6).foregroundStyle(Brand.neon)
                Text(PrivacyCopy.title).font(.system(size: 28, weight: .bold)).foregroundStyle(BrandText.primary)
            }
            note
            if choosing { chooser } else { buttons }
            HStack(spacing: 18) {
                Button { withAnimation(.easeOut(duration: 0.2)) { choosing.toggle() } } label: {
                    Label(choosing ? "Just yes or no" : "Choose what to send", systemImage: choosing ? "chevron.up" : "slider.horizontal.3")
                }
                Button { showSent = true } label: { Label("See exactly what's sent", systemImage: "doc.text.magnifyingglass") }
            }
            .buttonStyle(.plain).font(.system(size: 12.5, weight: .semibold)).foregroundStyle(Brand.neon)
            Text(PrivacyCopy.later).font(.system(size: 11.5)).foregroundStyle(BrandText.faint)
        }
        .sheet(isPresented: $showSent) { WhatsSentView { showSent = false } }
        .onAppear {
            // Once, as the page comes up: the card's edge lights and settles.
            withAnimation(.easeOut(duration: 0.5).delay(0.25)) { glow = true }
            DispatchQueue.main.asyncAfter(deadline: .now() + 1.6) { withAnimation(.easeInOut(duration: 0.9)) { glow = false } }
        }
    }

    private var note: some View {
        HStack(alignment: .top, spacing: 16) {
            // The intro's lamp, lit.
            ZStack {
                Circle().fill(Color(.sRGB, red: 1, green: 196 / 255, blue: 64 / 255).opacity(0.18)).frame(width: 34, height: 34)
                Circle().fill(Color(.sRGB, red: 1, green: 196 / 255, blue: 64 / 255)).frame(width: 18, height: 18)
                    .overlay(Circle().strokeBorder(Color(.sRGB, red: 230 / 255, green: 160 / 255, blue: 20 / 255), lineWidth: 2))
                    .shadow(color: Color(.sRGB, red: 1, green: 196 / 255, blue: 64 / 255).opacity(0.8), radius: 8)
            }
            .padding(.top, 2)
            VStack(alignment: .leading, spacing: 10) {
                Text("\u{201C}" + PrivacyCopy.note + "\u{201D}")
                    .font(.system(size: compact ? 13.5 : 14.5)).lineSpacing(2.5).foregroundStyle(BrandText.primary)
                    .fixedSize(horizontal: false, vertical: true)
                Text("\u{2014} " + PrivacyCopy.signature).font(.system(size: 12).italic()).foregroundStyle(BrandText.secondary)
            }
        }
        .padding(18)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(RoundedRectangle(cornerRadius: 14, style: .continuous).fill(Brand.neon.opacity(glow ? 0.1 : 0.05)))
        .overlay(alignment: .leading) {
            RoundedRectangle(cornerRadius: 2).fill(Brand.neon.opacity(0.8)).frame(width: 3).padding(.vertical, 14)
        }
        .overlay(RoundedRectangle(cornerRadius: 14, style: .continuous)
            .strokeBorder(Brand.neon.opacity(glow ? 0.7 : 0.22), lineWidth: glow ? 1.4 : 1))
        .shadow(color: Brand.neon.opacity(glow ? 0.3 : 0), radius: 14)
    }

    private var buttons: some View {
        HStack(spacing: 14) {
            Button { onAnswer(.yes, true, true) } label: { choiceLabel(PrivacyCopy.yes, chosen: answered == .yes) }
            Button { onAnswer(.no, CrashReports.allowed ?? false, false) } label: { choiceLabel(PrivacyCopy.no, chosen: answered == .no) }
        }
        .buttonStyle(PrivacyChoiceStyle())
    }

    private func choiceLabel(_ s: String, chosen: Bool) -> some View {
        HStack(spacing: 7) {
            if chosen { Image(systemName: "checkmark").font(.system(size: 11, weight: .bold)) }
            Text(s)
        }
        .frame(maxWidth: .infinity)
    }

    private var chooser: some View {
        VStack(alignment: .leading, spacing: 10) {
            toggle("Send crash reports", "Where it went wrong, if CedarLogic crashes.", isOn: $crash,
                   off: !PrivacyConsent.crashOffered ? (PrivacyConsent.crashPolicyOff ? "Turned off on this Mac by its administrator." : "Not in this build.") : nil)
            toggle("Send anonymous usage stats", "How often it's open and for how long, and which features are used.", isOn: $usage,
                   off: !PrivacyConsent.usageOffered ? "Turned off on this Mac by its administrator." : nil)
            Button { onAnswer(.custom, crash, usage) } label: { choiceLabel("Continue with these", chosen: answered == .custom) }
                .buttonStyle(PrivacyChoiceStyle()).frame(width: 260).padding(.top, 2)
        }
    }

    private func toggle(_ title: String, _ line: String, isOn: Binding<Bool>, off: String?) -> some View {
        HStack(spacing: 12) {
            Toggle(title, isOn: isOn).labelsHidden().toggleStyle(.switch).tint(Brand.neon).disabled(off != nil)
            VStack(alignment: .leading, spacing: 2) {
                Text(title).font(.system(size: 13, weight: .semibold)).foregroundStyle(BrandText.primary)
                Text(off ?? line).font(.system(size: 11.5)).foregroundStyle(BrandText.secondary)
            }
            .opacity(off != nil ? 0.6 : 1)
            .onTapGesture { if off == nil { isOn.wrappedValue.toggle() } }
        }
    }
}

/// Yes and No: the same size, weight and colour.
struct PrivacyChoiceStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 13.5, weight: .semibold))
            .foregroundStyle(BrandText.primary)
            .padding(.horizontal, 18)
            .frame(minHeight: 40)
            .background(Capsule().fill(Color.white.opacity(configuration.isPressed ? 0.16 : 0.08)))
            .overlay(Capsule().strokeBorder(Brand.neon.opacity(0.45), lineWidth: 1))
            .scaleEffect(configuration.isPressed ? 0.98 : 1)
            .animation(.easeOut(duration: 0.12), value: configuration.isPressed)
            .contentShape(Capsule())
    }
}

/// The updaters' sheet: the same question, over the circuit window. Escape is No thanks.
struct PrivacyAskSheet: View {
    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            if CrashReports.allowed == true && PrivacyConsent.crashOffered {
                Label("Crash reports are on, as you chose before. This adds usage stats, if you'd like.", systemImage: "checkmark.circle")
                    .font(.system(size: 11.5)).foregroundStyle(BrandText.secondary)
            }
            PrivacyAskView(onAnswer: { c, crash, usage in PrivacyConsent.sheetAnswered(c, crash: crash, usage: usage) }, compact: true)
        }
        .padding(.horizontal, 36).padding(.vertical, 30)
        .frame(width: 640)
        .background(BrandBackground())
        .preferredColorScheme(.dark)
        .tint(Brand.neon)
        .onExitCommand { PrivacyConsent.sheetAnswered(.no, crash: CrashReports.allowed ?? false, usage: false) }
    }
}

// MARK: - What's sent

/// "See exactly what's sent": the report the app would send now, and in
/// plain words what never is.
struct WhatsSentView: View {
    let done: () -> Void
    @State private var json = UsageStats.shared.preview()

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Exactly what's sent").font(.system(size: 20, weight: .bold))
            Text(PrivacyCopy.usageLine).font(.system(size: 12)).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            Text("The next usage report, as it would go now:").font(.system(size: 12, weight: .semibold))
            ScrollView {
                Text(json).font(.system(size: 11, design: .monospaced)).textSelection(.enabled)
                    .frame(maxWidth: .infinity, alignment: .leading).padding(10)
            }
            .frame(height: 210)
            .background(RoundedRectangle(cornerRadius: 8).fill(Color.primary.opacity(0.05)))
            .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(Color.primary.opacity(0.1)))
            Text("Crash reports").font(.system(size: 12, weight: .semibold))
            Text(PrivacyCopy.crashLine).font(.system(size: 12)).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            Text("Never sent").font(.system(size: 12, weight: .semibold))
            VStack(alignment: .leading, spacing: 3) {
                ForEach(PrivacyCopy.never, id: \.self) { line in
                    HStack(alignment: .firstTextBaseline, spacing: 6) {
                        Image(systemName: "xmark").font(.system(size: 9, weight: .bold)).foregroundStyle(.secondary)
                        Text(line).font(.system(size: 12)).fixedSize(horizontal: false, vertical: true)
                    }
                }
            }
            Text(PrivacyCopy.idLine).font(.system(size: 11)).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            HStack {
                Spacer()
                Button("Done", action: done).keyboardShortcut(.defaultAction)
            }
        }
        .padding(24)
        .frame(width: 600)
    }
}

// MARK: - Settings > Privacy

struct PrivacySettingsView: View {
    @ObservedObject private var usage = UsageStats.shared
    @State private var crash = CrashReports.allowed ?? false
    @State private var showSent = false
    @State private var confirmDelete = false
    @State private var deleteNote = ""
    @State private var deleting = false

    var body: some View {
        Page {
            Row("Crash reports", hint: crashHint) {
                Toggle("Send crash reports", isOn: $crash)
                    .disabled(!PrivacyConsent.crashOffered)
                    .onChange(of: crash) { _, on in
                        PrivacyConsent.record(.custom, crash: on, usage: usage.on)
                    }
            }
            Row("Usage stats", hint: usage.policyOff ? "Turned off on this Mac by its administrator (DisableUsageStats)." : PrivacyCopy.usageLine) {
                Toggle("Send anonymous usage stats", isOn: Binding(get: { usage.on }, set: { on in
                    PrivacyConsent.record(.custom, crash: CrashReports.allowed ?? false, usage: on)
                }))
                .disabled(usage.policyOff)
            }
            Row("What's sent", hint: "The next report exactly as it would go, and what's never sent.") {
                Button("See What's Sent\u{2026}") { showSent = true }
            }
            Row("Last sent", hint: usage.kept > 0 ? "\(usage.kept) report\(usage.kept == 1 ? "" : "s") waiting to go (no connection last time)." : nil) {
                Text(usage.lastSent.isEmpty ? (usage.on ? "Nothing yet" : "Not sending") : usage.lastSent).foregroundStyle(.secondary)
            }
            Row("Your data", hint: deleteNote.isEmpty ? "Deletes every usage report sent from this Mac. (Crash reports are deleted by Sentry after its retention period.)" : deleteNote) {
                Button("Delete What's Been Sent\u{2026}") { confirmDelete = true }
                    .disabled(deleting || usage.engine.id == nil)
            }
        }
        .sheet(isPresented: $showSent) { WhatsSentView { showSent = false } }
        .alert("Delete what's been sent?", isPresented: $confirmDelete) {
            Button("Delete", role: .destructive) {
                deleting = true
                Task {
                    let done = await usage.deleteSent()
                    deleteNote = done ? "Deleted. \(usage.on ? "Reports start again under a new id." : "")"
                        : "Couldn't reach the server. It's asked again the next time CedarLogic opens."
                    deleting = false
                }
            }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("The server forgets every usage report sent from this Mac. If usage stats stay on, they start again under a new random id.")
        }
        .onAppear { usage.refresh() }
    }

    private var crashHint: String {
        if PrivacyConsent.crashPolicyOff { return "Turned off on this Mac by its administrator (DisableCrashReporting)." }
        if !CrashReports.available { return "Not in this build." }
        return PrivacyCopy.crashLine + " Off takes effect the next time CedarLogic opens."
    }
}

// MARK: - Pictures

/// `CedarLogic --render-privacy <dir>`: the welcome's page (both ways), the
/// updaters' sheet, "See exactly what's sent" and Settings > Privacy, light
/// and dark, to PNGs (mac/Tools/privacy-preview/).
@MainActor
enum PrivacyRender {
    static func runIfAsked() {
        let args = CommandLine.arguments
        guard let i = args.firstIndex(of: "--render-privacy"), i + 1 < args.count else { return }
        let dir = URL(fileURLWithPath: args[i + 1], isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        func shot<V: View>(_ name: String, _ view: V, dark: Bool, width: CGFloat, height: CGFloat? = nil) {
            let host = NSHostingView(rootView: view.environment(\.colorScheme, dark ? .dark : .light))
            host.appearance = NSAppearance(named: dark ? .darkAqua : .aqua)
            let fit = host.fittingSize
            host.frame = NSRect(x: 0, y: 0, width: width, height: height ?? fit.height)
            host.layoutSubtreeIfNeeded()
            guard let rep = host.bitmapImageRepForCachingDisplay(in: host.bounds) else { return }
            host.cacheDisplay(in: host.bounds, to: rep)
            try? rep.representation(using: .png, properties: [:])?.write(to: dir.appendingPathComponent(name + ".png"))
            print("wrote \(name).png")
        }
        let savedDark = Prefs.shared.dark
        for dark in [false, true] {
            let t = dark ? "dark" : "light"
            Prefs.shared.dark = dark
            // The welcome and the sheet are the brand look either way (dark ground).
            shot("welcome-privacy-\(t)", WelcomeView(page: WelcomeView.privacyPage), dark: dark, width: 780, height: 580)
            shot("welcome-privacy-choose-\(t)", PrivacyAskView(onAnswer: { _, _, _ in }, choosing: true)
                .padding(.horizontal, 56).padding(.vertical, 28).frame(width: 780).background(BrandBackground()), dark: dark, width: 780)
            shot("updater-sheet-\(t)", PrivacyAskSheet(), dark: dark, width: 640)
            shot("whats-sent-\(t)", WhatsSentView {}.background(Color(nsColor: .windowBackgroundColor)), dark: dark, width: 600)
            shot("settings-privacy-\(t)", SettingsPageView(page: .privacy).background(Color(nsColor: .windowBackgroundColor)), dark: dark, width: 640)
        }
        Prefs.shared.dark = savedDark
        exit(0)
    }
}
