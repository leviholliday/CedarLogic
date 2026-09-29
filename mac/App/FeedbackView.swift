// The Send Feedback window: the icon's dark band across the top, then what
// happened, tags, how much it matters, pictures of it, and who's writing.
// The model is FeedbackModel (Feedback.swift).

import AppKit
import SwiftUI
import UniformTypeIdentifiers

struct FeedbackView: View {
    @ObservedObject private var model = FeedbackModel.shared
    @ObservedObject private var prefs = Prefs.shared
    @Environment(\.dismiss) private var dismiss
    @FocusState private var titleFocused: Bool
    @State private var showDevice = false
    @State private var dropping = false

    private var dark: Bool { prefs.dark }
    private var accent: Color { prefs.accentColor(dark: dark) }
    private var ink: Color { dark ? Color(white: 0.93) : Color(white: 0.1) }
    private var dim: Color { dark ? Color(white: 0.62) : Color(white: 0.42) }
    private var paper: Color { dark ? Color(.sRGB, red: 0.075, green: 0.085, blue: 0.1) : Color(.sRGB, red: 0.965, green: 0.97, blue: 0.975) }
    private var card: Color { dark ? Color(white: 1, opacity: 0.045) : .white }
    private var line: Color { dark ? Color(white: 1, opacity: 0.08) : Color(white: 0, opacity: 0.07) }

    var body: some View {
        VStack(spacing: 0) {
            header
            if model.phase == .sent {
                sent.transition(.opacity.combined(with: .scale(scale: 0.97)))
            } else {
                form
                footer
            }
        }
        .frame(width: 620, height: 760)
        .background(paper)
        // The band runs up under the title bar, the window's buttons on it.
        .ignoresSafeArea(.container, edges: .top)
        // The window keeps the title bar's height at the bottom; paper there too.
        .frame(maxHeight: .infinity, alignment: .top)
        .background(paper.ignoresSafeArea())
        .preferredColorScheme(dark ? .dark : .light)
        .tint(accent)
        .onEscape { dismiss() }
        .onAppear { if model.title.isEmpty { titleFocused = true } }
    }

    // MARK: The band

    private var header: some View {
        ZStack(alignment: .bottomLeading) {
            LinearGradient(colors: [Brand.ink, Color(.sRGB, red: 0.06, green: 0.12, blue: 0.08)], startPoint: .topLeading, endPoint: .bottomTrailing)
            // A few traces, as on a circuit board, fading off to the right.
            Canvas { ctx, size in
                var p = Path()
                for i in 0..<5 {
                    let y = 26 + CGFloat(i) * 17
                    p.move(to: CGPoint(x: size.width * 0.52, y: y))
                    p.addLine(to: CGPoint(x: size.width * 0.66 + CGFloat(i) * 14, y: y))
                    p.addLine(to: CGPoint(x: size.width * 0.70 + CGFloat(i) * 14, y: y + 10))
                    p.addLine(to: CGPoint(x: size.width, y: y + 10))
                }
                ctx.stroke(p, with: .linearGradient(Gradient(colors: [Brand.neon.opacity(0), Brand.neon.opacity(0.22)]),
                                                    startPoint: CGPoint(x: size.width * 0.5, y: 0), endPoint: CGPoint(x: size.width, y: 0)),
                           lineWidth: 1.2)
            }
            HStack(alignment: .center, spacing: 14) {
                Image(nsImage: Brand.icon).resizable().frame(width: 46, height: 46)
                    .clipShape(RoundedRectangle(cornerRadius: 46 * Brand.corner, style: .continuous))
                    .shadow(color: .black.opacity(0.4), radius: 6, y: 3)
                VStack(alignment: .leading, spacing: 3) {
                    HStack(spacing: 8) {
                        Text("Send Feedback").font(.system(size: 21, weight: .semibold)).foregroundStyle(.white)
                        Text("BETA").font(.system(size: 9.5, weight: .heavy)).tracking(1)
                            .padding(.horizontal, 6).padding(.vertical, 2)
                            .background(Capsule().fill(Brand.neon)).foregroundStyle(Brand.inkDeep)
                    }
                    Text("What's working, what's broken, what you'd love to see. It goes straight to Levi.")
                        .font(.system(size: 12.5)).foregroundStyle(Brand.dim)
                }
            }
            .padding(.horizontal, 26).padding(.bottom, 18)
        }
        .frame(height: 132)
    }

    // MARK: The form

    private var form: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 22) {
                section("What's on your mind?") {
                    TextField("A short summary, like “C doesn't connect the wire”", text: $model.title)
                        .textFieldStyle(.plain)
                        .font(.system(size: 15, weight: .medium))
                        .padding(.horizontal, 12).frame(height: 38)
                        .background(field)
                        .focused($titleFocused)
                    ZStack(alignment: .topLeading) {
                        if model.details.isEmpty {
                            Text("What happened, what you expected, and how to make it happen again (if you know).")
                                .font(.system(size: 13)).foregroundStyle(dim.opacity(0.8))
                                .padding(.horizontal, 12).padding(.vertical, 10)
                                .allowsHitTesting(false)
                        }
                        TextEditor(text: $model.details)
                            .font(.system(size: 13))
                            .scrollContentBackground(.hidden)
                            .padding(.horizontal, 7).padding(.vertical, 8)
                    }
                    .frame(minHeight: 110)
                    .background(field)
                }

                section("Tags", note: model.suggested.isEmpty ? nil : "✦ suggested from what you wrote") {
                    Flow(spacing: 7) {
                        ForEach(FeedbackModel.tags, id: \.self) { tag in chip(tag) }
                    }
                }

                section("How much does it matter?") {
                    HStack(spacing: 8) {
                        ForEach(FeedbackPriority.allCases) { p in priorityPill(p) }
                    }
                    Text(model.priority.hint).font(.system(size: 11.5)).foregroundStyle(dim)
                        .animation(nil, value: model.priority)
                }

                section("Show it", note: "Only the CedarLogic window is captured") {
                    HStack(alignment: .top, spacing: 10) {
                        captureTile("camera.viewfinder", "Screenshot", model.images.count < FeedbackModel.maxImages) { model.screenshot() }
                        captureTile("record.circle", model.video == nil ? "Record" : "Record again", true) {
                            model.record(from: NSApp.keyWindow)
                        }
                        captureTile("photo.on.rectangle", "Add image…", model.images.count < FeedbackModel.maxImages) { pickImages() }
                    }
                    if !model.attachments.isEmpty {
                        ScrollView(.horizontal, showsIndicators: false) {
                            HStack(spacing: 10) {
                                ForEach(model.attachments) { a in thumbnail(a) }
                            }
                            .padding(.vertical, 4)
                        }
                    }
                    Toggle(isOn: $model.includeCircuit) {
                        VStack(alignment: .leading, spacing: 1) {
                            Text("Send this circuit too").font(.system(size: 12.5))
                            Text("Makes a problem easy to repeat. It's only sent with this.").font(.system(size: 11)).foregroundStyle(dim)
                        }
                    }
                    .toggleStyle(.checkbox)
                    .padding(.top, 2)
                }
                .onDrop(of: [.fileURL], isTargeted: $dropping) { providers in
                    for p in providers {
                        _ = p.loadObject(ofClass: URL.self) { url, _ in
                            if let url { DispatchQueue.main.async { model.addImages([url]) } }
                        }
                    }
                    return true
                }

                section("About you") {
                    HStack(spacing: 10) {
                        labeled("Name") {
                            TextField("First and last name", text: $prefs.studentName)
                                .textFieldStyle(.plain).padding(.horizontal, 10).frame(height: 32).background(field)
                        }
                        labeled("Email (if you'd like a reply)") {
                            TextField("you@example.com", text: $model.email)
                                .textFieldStyle(.plain).padding(.horizontal, 10).frame(height: 32).background(field)
                        }
                    }
                    Toggle("Levi can email me about this", isOn: $model.contactOK)
                        .toggleStyle(.checkbox)
                        .font(.system(size: 12.5))
                        .disabled(model.email.trimmingCharacters(in: .whitespaces).isEmpty)
                }
            }
            .padding(.horizontal, 26).padding(.top, 22).padding(.bottom, 18)
        }
    }

    private var field: some View {
        RoundedRectangle(cornerRadius: 9, style: .continuous).fill(card)
            .overlay(RoundedRectangle(cornerRadius: 9, style: .continuous).strokeBorder(line))
    }

    private func section<C: View>(_ title: String, note: String? = nil, @ViewBuilder _ content: () -> C) -> some View {
        VStack(alignment: .leading, spacing: 9) {
            HStack(alignment: .firstTextBaseline) {
                Text(title.uppercased()).font(.system(size: 10.5, weight: .semibold)).tracking(0.7).foregroundStyle(dim)
                Spacer()
                if let note { Text(note).font(.system(size: 11)).foregroundStyle(dim.opacity(0.85)) }
            }
            content()
        }
    }

    private func labeled<C: View>(_ label: String, @ViewBuilder _ content: () -> C) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(label).font(.system(size: 11)).foregroundStyle(dim)
            content()
        }
    }

    private func chip(_ tag: String) -> some View {
        let on = model.isOn(tag)
        let suggested = model.suggested.contains(tag)
        return Button { withAnimation(.easeOut(duration: 0.15)) { model.toggle(tag) } } label: {
            HStack(spacing: 4) {
                if suggested { Text("✦").font(.system(size: 9)).foregroundStyle(accent) }
                Text(tag).font(.system(size: 12, weight: on ? .semibold : .regular))
            }
            .padding(.horizontal, 11).padding(.vertical, 5)
            .foregroundStyle(on ? accent : ink.opacity(0.75))
            .background(Capsule().fill(on ? accent.opacity(dark ? 0.2 : 0.12) : card))
            .overlay(Capsule().strokeBorder(on ? accent.opacity(0.55) : line,
                                            style: StrokeStyle(lineWidth: 1, dash: suggested && !on ? [3, 2] : [])))
        }
        .buttonStyle(.plain)
        .help(suggested ? (on ? "Suggested from what you wrote. Click to remove." : "Suggested, then removed. Click to add back.") : "")
    }

    private func priorityPill(_ p: FeedbackPriority) -> some View {
        let on = model.priority == p
        return Button { withAnimation(.easeOut(duration: 0.15)) { model.priority = p } } label: {
            HStack(spacing: 6) {
                Circle().fill(p.color).frame(width: 8, height: 8)
                Text(p.name).font(.system(size: 12.5, weight: on ? .semibold : .regular))
            }
            .frame(maxWidth: .infinity).frame(height: 32)
            .foregroundStyle(on ? ink : ink.opacity(0.7))
            .background(RoundedRectangle(cornerRadius: 9, style: .continuous).fill(on ? p.color.opacity(dark ? 0.22 : 0.14) : card))
            .overlay(RoundedRectangle(cornerRadius: 9, style: .continuous).strokeBorder(on ? p.color.opacity(0.7) : line, lineWidth: on ? 1.5 : 1))
        }
        .buttonStyle(.plain)
    }

    private func captureTile(_ icon: String, _ label: String, _ enabled: Bool, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            VStack(spacing: 6) {
                Image(systemName: icon).font(.system(size: 19, weight: .regular))
                    .foregroundStyle(icon == "record.circle" ? Color.red : accent)
                Text(label).font(.system(size: 12, weight: .medium))
            }
            .frame(maxWidth: .infinity).frame(height: 70)
            .foregroundStyle(ink)
            .background(RoundedRectangle(cornerRadius: 11, style: .continuous).fill(card))
            .overlay(RoundedRectangle(cornerRadius: 11, style: .continuous)
                .strokeBorder(dropping ? accent : line, style: StrokeStyle(lineWidth: dropping ? 1.5 : 1, dash: dropping ? [5, 3] : [])))
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .disabled(!enabled || model.recording != nil)
        .opacity(enabled ? 1 : 0.45)
    }

    private func thumbnail(_ a: FeedbackAttachment) -> some View {
        ZStack(alignment: .topTrailing) {
            ZStack(alignment: .bottomLeading) {
                Group {
                    if let img = a.thumbnail {
                        Image(nsImage: img).resizable().aspectRatio(contentMode: .fill)
                    } else {
                        Color.black.opacity(0.3)
                    }
                }
                .frame(width: 132, height: 84)
                .clipped()
                if a.kind == .recording {
                    HStack(spacing: 4) {
                        Image(systemName: "play.fill").font(.system(size: 8))
                        Text(String(format: "%d:%02d", Int(a.duration) / 60, Int(a.duration) % 60)).font(.system(size: 10.5, weight: .semibold).monospacedDigit())
                    }
                    .padding(.horizontal, 6).padding(.vertical, 3)
                    .background(Capsule().fill(.black.opacity(0.6))).foregroundStyle(.white)
                    .padding(6)
                }
            }
            .clipShape(RoundedRectangle(cornerRadius: 9, style: .continuous))
            .overlay(RoundedRectangle(cornerRadius: 9, style: .continuous).strokeBorder(line))
            .onTapGesture { NSWorkspace.shared.open(a.url) }
            .help("Click to look at it")
            Button { model.remove(a) } label: {
                Image(systemName: "xmark.circle.fill").font(.system(size: 17))
                    .symbolRenderingMode(.palette).foregroundStyle(.white, .black.opacity(0.65))
            }
            .buttonStyle(.plain).offset(x: 6, y: -6).help("Remove")
        }
        .padding(.top, 6).padding(.trailing, 6)
        .transition(.scale(scale: 0.8).combined(with: .opacity))
    }

    private func pickImages() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.png, .jpeg]
        panel.allowsMultipleSelection = true
        panel.message = "Pictures to go with your feedback (up to \(FeedbackModel.maxImages))."
        if panel.runModal() == .OK { model.addImages(panel.urls) }
    }

    // MARK: The footer

    private var footer: some View {
        VStack(spacing: 0) {
            Rectangle().fill(line).frame(height: 1)
            HStack(spacing: 12) {
                Button { showDevice.toggle() } label: {
                    HStack(spacing: 5) {
                        Image(systemName: "info.circle").font(.system(size: 11))
                        Text("Also sent: \(model.deviceLine)").font(.system(size: 11)).lineLimit(1).truncationMode(.tail)
                    }
                    .foregroundStyle(dim)
                }
                .buttonStyle(.plain)
                .popover(isPresented: $showDevice, arrowEdge: .top) { deviceList }
                Spacer(minLength: 8)
                switch model.phase {
                case .sending(let f, let note):
                    VStack(alignment: .trailing, spacing: 4) {
                        ProgressView(value: f).frame(width: 150)
                        Text(note).font(.system(size: 10.5)).foregroundStyle(dim)
                    }
                default:
                    Button("Close") { dismiss() }.keyboardShortcut(.cancelAction)
                    Button {
                        model.send()
                    } label: {
                        Text(model.phase == .writing || model.phase == .sent ? "Send Feedback" : "Try Again")
                            .font(.system(size: 13, weight: .semibold)).padding(.horizontal, 6)
                    }
                    .keyboardShortcut(.return, modifiers: .command)
                    .buttonStyle(.borderedProminent)
                    .disabled(!model.canSend)
                }
            }
            .padding(.horizontal, 22).frame(height: 58)
            if case .failed(let why) = model.phase {
                Text(why).font(.system(size: 11.5)).foregroundStyle(Color(.sRGB, red: 0.92, green: 0.3, blue: 0.27))
                    .frame(maxWidth: .infinity, alignment: .leading).padding(.horizontal, 22).padding(.bottom, 12)
            }
        }
        .background(paper)
    }

    private var deviceList: some View {
        let d = model.device
        return VStack(alignment: .leading, spacing: 10) {
            Text("Sent with your feedback").font(.system(size: 13, weight: .semibold))
            Text("So Levi knows where it happened. Nothing else leaves your Mac.")
                .font(.system(size: 11)).foregroundStyle(.secondary)
            ForEach(["app", "system", "context"], id: \.self) { group in
                VStack(alignment: .leading, spacing: 3) {
                    Text(["app": "App", "system": "This Mac", "context": "Right now"][group] ?? group)
                        .font(.system(size: 11, weight: .semibold)).foregroundStyle(.secondary)
                    ForEach((d[group] ?? [:]).keys.sorted(), id: \.self) { k in
                        HStack(alignment: .top) {
                            Text(k).font(.system(size: 11)).foregroundStyle(.secondary).frame(width: 110, alignment: .leading)
                            Text(describe(d[group]?[k])).font(.system(size: 11)).textSelection(.enabled)
                        }
                    }
                }
            }
        }
        .padding(16).frame(width: 380)
    }
    private func describe(_ v: Any?) -> String {
        switch v {
        case let a as [String]: a.joined(separator: ", ")
        case let b as Bool: b ? "Yes" : "No"
        case let x?: "\(x)"
        default: ""
        }
    }

    // MARK: Sent

    private var sent: some View {
        VStack(spacing: 16) {
            Spacer()
            SentCheck(color: Brand.neonDeep)
            Text(prefs.studentName.isEmpty ? "Thank you!" : "Thank you, \(prefs.studentName.split(separator: " ").first.map(String.init) ?? prefs.studentName)!")
                .font(.system(size: 24, weight: .semibold)).foregroundStyle(ink)
            Text("Your feedback is on its way to Levi.\(model.contactOK && !model.email.isEmpty ? " If he has a question, he'll write to \(model.email)." : "")")
                .font(.system(size: 13.5)).foregroundStyle(dim).multilineTextAlignment(.center).frame(maxWidth: 380)
            HStack(spacing: 10) {
                Button("Send Another") { withAnimation { model.phase = .writing } }
                Button("Done") { dismiss() }.keyboardShortcut(.defaultAction).buttonStyle(.borderedProminent)
            }
            .padding(.top, 6)
            Spacer()
            Spacer()
        }
        .frame(maxWidth: .infinity)
    }
}

/// A check that draws itself in a ring.
private struct SentCheck: View {
    let color: Color
    @State private var t: CGFloat = 0

    var body: some View {
        ZStack {
            Circle().trim(from: 0, to: t).stroke(color, style: StrokeStyle(lineWidth: 4, lineCap: .round))
                .rotationEffect(.degrees(-90))
            Path { p in
                p.move(to: CGPoint(x: 27, y: 45))
                p.addLine(to: CGPoint(x: 39, y: 57))
                p.addLine(to: CGPoint(x: 62, y: 32))
            }
            .trim(from: 0, to: max(0, (t - 0.5) * 2))
            .stroke(color, style: StrokeStyle(lineWidth: 5, lineCap: .round, lineJoin: .round))
        }
        .frame(width: 88, height: 88)
        .onAppear { withAnimation(.easeInOut(duration: 0.9)) { t = 1 } }
    }
}

/// Chips in rows, wrapping.
private struct Flow: Layout {
    var spacing: CGFloat = 8

    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        let width = proposal.width ?? 520
        var x: CGFloat = 0, y: CGFloat = 0, row: CGFloat = 0
        for s in subviews {
            let sz = s.sizeThatFits(.unspecified)
            if x > 0 && x + sz.width > width { x = 0; y += row + spacing; row = 0 }
            x += sz.width + spacing
            row = max(row, sz.height)
        }
        return CGSize(width: width, height: y + row)
    }

    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
        var x = bounds.minX, y = bounds.minY, row: CGFloat = 0
        for s in subviews {
            let sz = s.sizeThatFits(.unspecified)
            if x > bounds.minX && x + sz.width > bounds.maxX { x = bounds.minX; y += row + spacing; row = 0 }
            s.place(at: CGPoint(x: x, y: y), proposal: ProposedViewSize(sz))
            x += sz.width + spacing
            row = max(row, sz.height)
        }
    }
}

extension FeedbackModel {
    /// Hides the form, records the circuit window until Stop (or a minute),
    /// then brings the form back with the recording on it.
    func record(from form: NSWindow?) {
        guard recording == nil, let w = target ?? CanvasController.front?.view?.window, let r = FeedbackRecorder(window: w) else {
            NSSound.beep()
            return
        }
        recording = r
        form?.orderOut(nil)
        w.makeKeyAndOrderFront(nil)
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) {
            r.begin { [weak self] url, length, thumb in
                guard let self else { return }
                self.recording = nil
                if let url { self.addRecording(url, duration: length, thumbnail: thumb) }
                form?.makeKeyAndOrderFront(nil)
            }
        }
    }
}
