// Send Feedback (Help > Send Feedback…, and the toolbar's speech bubble):
// what the tester writes, tags (the app suggests some from the words),
// a priority, their name and, if they like, an email, plus screenshots and a
// recording of the app (FeedbackCapture.swift). The version, the Mac and
// the moment it was sent from are added by themselves. It goes to
// cedarlogic.netlify.app (the cedarlogic-site repository), which tells the developer.

import AppKit
import SwiftUI

enum FeedbackPriority: String, CaseIterable, Identifiable {
    case low, normal, high, blocking
    var id: String { rawValue }
    var name: String { rawValue.capitalized }
    var hint: String {
        switch self {
        case .low: "Small thing, whenever"
        case .normal: "Worth fixing"
        case .high: "Gets in my way"
        case .blocking: "I can't do my work"
        }
    }
    var color: Color {
        switch self {
        case .low: Color(white: 0.55)
        case .normal: Color(.sRGB, red: 0.23, green: 0.52, blue: 0.95)
        case .high: Color(.sRGB, red: 0.95, green: 0.55, blue: 0.12)
        case .blocking: Color(.sRGB, red: 0.92, green: 0.27, blue: 0.24)
        }
    }
}

struct FeedbackAttachment: Identifiable, Equatable {
    enum Kind { case screenshot, recording, image, circuit }
    let id = UUID()
    let kind: Kind
    let url: URL
    let name: String
    let type: String
    var duration: Double = 0
    var thumbnail: NSImage?
    var size: Int { (try? url.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0 }
    static func == (a: Self, b: Self) -> Bool { a.id == b.id }
}

@MainActor
final class FeedbackModel: ObservableObject {
    static let shared = FeedbackModel()

    enum Phase: Equatable {
        case writing
        case sending(Double, String)
        case sent
        case failed(String)
    }

    static let tags = ["Bug", "Idea", "Design", "Toolbar", "Canvas & wiring", "Simulation", "Files & saving", "Performance", "Other"]
    static let maxImages = 3

    // The draft, kept between openings (and launches) until it's sent.
    @Published var title = UserDefaults.standard.string(forKey: "cl.feedbackTitle") ?? "" { didSet { save() } }
    @Published var details = UserDefaults.standard.string(forKey: "cl.feedbackDetails") ?? "" { didSet { save() } }
    @Published var chosen = Set(UserDefaults.standard.stringArray(forKey: "cl.feedbackTags") ?? []) { didSet { save() } }
    /// Suggested tags the tester turned off.
    @Published var declined = Set<String>()
    @Published var priority = FeedbackPriority(rawValue: UserDefaults.standard.string(forKey: "cl.feedbackPriority") ?? "") ?? .normal { didSet { save() } }
    @Published var email = UserDefaults.standard.string(forKey: "cl.feedbackEmail") ?? "" { didSet { save() } }
    @Published var contactOK = UserDefaults.standard.object(forKey: "cl.feedbackContact") as? Bool ?? true { didSet { save() } }
    @Published var includeCircuit = false
    @Published private(set) var attachments: [FeedbackAttachment] = []
    @Published var phase = Phase.writing
    @Published var recording: FeedbackRecorder?

    /// The circuit window it's about: the one in front when the form opened.
    weak var target: NSWindow?

    private func save() {
        let d = UserDefaults.standard
        d.set(title, forKey: "cl.feedbackTitle")
        d.set(details, forKey: "cl.feedbackDetails")
        d.set(Array(chosen), forKey: "cl.feedbackTags")
        d.set(priority.rawValue, forKey: "cl.feedbackPriority")
        d.set(email, forKey: "cl.feedbackEmail")
        d.set(contactOK, forKey: "cl.feedbackContact")
    }

    /// Before the window opens: which circuit window it's about.
    static func aim() {
        let front = CanvasController.front?.view?.window
            ?? NSApp.orderedWindows.first { NSDocumentController.shared.document(for: $0) != nil }
        shared.target = front
        if case .sent = shared.phase { shared.phase = .writing }
    }

    // MARK: Tags

    /// What the words suggest (and Simulation View, if that's on).
    var suggested: [String] {
        let text = "\(title) \(details)".lowercased()
        var out: [String] = []
        for (tag, words) in Self.clues where words.contains(where: { Self.has(text, $0) }) { out.append(tag) }
        if CanvasController.front?.simView == true, !out.contains("Simulation") { out.append("Simulation") }
        return Self.tags.filter(out.contains)
    }
    /// The tags it goes with: the ones picked, and the suggestions kept.
    var tags: [String] {
        let s = Set(suggested).subtracting(declined)
        return Self.tags.filter { chosen.contains($0) || s.contains($0) }
    }
    func isOn(_ tag: String) -> Bool { tags.contains(tag) }
    func toggle(_ tag: String) {
        if suggested.contains(tag) {
            if isOn(tag) { declined.insert(tag); chosen.remove(tag) } else { declined.remove(tag) }
        } else if chosen.contains(tag) {
            chosen.remove(tag)
        } else {
            chosen.insert(tag)
        }
    }

    private static let clues: [(String, [String])] = [
        ("Bug", ["bug", "crash", "broke", "doesn't", "does not", "didn't", "isn't", "not working", "error", "wrong",
                 "glitch", "stuck", "freez", "fail", "won't", "can't", "cannot", "missing", "disappear", "weird", "jump",
                 "flicker", "should"]),
        ("Idea", ["idea", "feature", "would be nice", "would love", "could you", "can you add", "please add", "wish",
                  "suggest", "it'd be", "it would be", "maybe add", "what if"]),
        ("Design", ["look", "color", "colour", "font", "icon", "ugly", "design", "theme", "dark mode", "light mode",
                    "layout", "spacing", "align", "animation", "pretty"]),
        ("Toolbar", ["toolbar", "button", "top bar", "title bar", "tooltip"]),
        ("Canvas & wiring", ["wire", "wiring", "gate", "canvas", "connect", "pin", "drag", "zoom", "select", "paste",
                             "copy", "rotate", "grid", "palette"]),
        ("Simulation", ["simulat", "clock", "oscilloscope", "scope", "truth table", "timing", "signal", "step"]),
        ("Files & saving", ["save", "saving", "open", "file", "export", "import", "version history", "library",
                            "your circuits", "autosave", ".cdl", "print"]),
        ("Performance", ["slow", "lag", "battery", "cpu", "hang", "stutter", "sluggish", "performance", "memory", "fan"]),
    ]
    /// The word at the start of a word ("freez" finds "freezes", "pin" not "spinning").
    private static func has(_ text: String, _ word: String) -> Bool {
        var from = text.startIndex
        while let r = text.range(of: word, range: from..<text.endIndex) {
            if r.lowerBound == text.startIndex || !text[text.index(before: r.lowerBound)].isLetter { return true }
            from = r.upperBound
        }
        return false
    }

    // MARK: Attachments

    var images: [FeedbackAttachment] { attachments.filter { $0.kind == .screenshot || $0.kind == .image } }
    var video: FeedbackAttachment? { attachments.first { $0.kind == .recording } }

    static var folder: URL {
        let u = FileManager.default.temporaryDirectory.appendingPathComponent("CedarLogic Feedback", isDirectory: true)
        try? FileManager.default.createDirectory(at: u, withIntermediateDirectories: true)
        return u
    }
    private func freeName(_ base: String, _ ext: String) -> String {
        var n = 1
        while attachments.contains(where: { $0.name == "\(base)-\(n).\(ext)" }) { n += 1 }
        return "\(base)-\(n).\(ext)"
    }

    /// The circuit window, as it looks now.
    func screenshot() {
        guard images.count < Self.maxImages, let w = target ?? CanvasController.front?.view?.window,
              let png = FeedbackCapture.png(of: w) else { NSSound.beep(); return }
        let name = freeName("screenshot", "png")
        let url = Self.folder.appendingPathComponent("\(UUID().uuidString)-\(name)")
        guard (try? png.write(to: url)) != nil else { return }
        withAnimation(.spring(response: 0.35, dampingFraction: 0.8)) {
            attachments.append(FeedbackAttachment(kind: .screenshot, url: url, name: name, type: "image/png", thumbnail: NSImage(data: png)))
        }
    }

    func addImages(_ urls: [URL]) {
        for u in urls where images.count < Self.maxImages {
            let ext = u.pathExtension.lowercased()
            guard ["png", "jpg", "jpeg"].contains(ext), let data = try? Data(contentsOf: u), data.count < 12_000_000 else { continue }
            let name = freeName("image", ext == "png" ? "png" : "jpg")
            let url = Self.folder.appendingPathComponent("\(UUID().uuidString)-\(name)")
            guard (try? data.write(to: url)) != nil else { continue }
            withAnimation(.spring(response: 0.35, dampingFraction: 0.8)) {
                attachments.append(FeedbackAttachment(kind: .image, url: url, name: name, type: ext == "png" ? "image/png" : "image/jpeg",
                                                      thumbnail: NSImage(data: data)))
            }
        }
    }

    func addRecording(_ url: URL, duration: Double, thumbnail: NSImage?) {
        if let old = video { remove(old) }
        withAnimation(.spring(response: 0.35, dampingFraction: 0.8)) {
            attachments.append(FeedbackAttachment(kind: .recording, url: url, name: "recording.mp4", type: "video/mp4",
                                                  duration: duration, thumbnail: thumbnail))
        }
    }

    func remove(_ a: FeedbackAttachment) {
        try? FileManager.default.removeItem(at: a.url)
        withAnimation(.easeOut(duration: 0.2)) { attachments.removeAll { $0.id == a.id } }
    }

    // MARK: About the Mac

    var device: [String: [String: Any]] {
        let info = Bundle.main.infoDictionary ?? [:]
        let prefs = Prefs.shared
        var app: [String: Any] = [
            "name": "CedarLogic for Mac",
            "version": info["CFBundleShortVersionString"] as? String ?? "?",
            "build": info["CFBundleVersion"] as? String ?? "?",
            "channel": prefs.testingGroup.name,
        ]
        if let c = info["CLCommit"] as? String { app["commit"] = c }
        let v = ProcessInfo.processInfo.operatingSystemVersion
        var system: [String: Any] = [
            "os": "macOS \(v.majorVersion).\(v.minorVersion)\(v.patchVersion > 0 ? ".\(v.patchVersion)" : "") (\(Self.sysctl("kern.osversion") ?? "?"))",
            "model": Self.sysctl("hw.model") ?? "?",
            "memoryGB": Int((Double(ProcessInfo.processInfo.physicalMemory) / 1_073_741_824).rounded()),
            "cpus": ProcessInfo.processInfo.processorCount,
            "displays": NSScreen.screens.map { "\(Int($0.frame.width))×\(Int($0.frame.height)) @\(Int($0.backingScaleFactor))x" },
            "locale": Locale.current.identifier,
            "appearance": NSApp.effectiveAppearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua ? "Dark" : "Light",
        ]
        if let chip = Self.sysctl("machdep.cpu.brand_string") { system["chip"] = chip }
        var context: [String: Any] = [
            "toolbar": prefs.toolbarStyle.name,
            "dark": prefs.dark,
        ]
        if let c = CanvasController.front, let d = c.document {
            context["pages"] = d.pageCount
            context["gates"] = (0..<d.pageCount).reduce(0) { $0 + Int(cl_document_gate_count(d.handle, Int32($1))) }
            context["simulating"] = c.isRunning
            context["simulationView"] = c.simView
            context["splitView"] = c.partner != nil
            context["locked"] = c.locked
            context["zoom"] = "\(c.zoomPercent)%"
        }
        return ["app": app, "system": system, "context": context]
    }
    /// One line of it, for the form's footer.
    var deviceLine: String {
        let d = device
        let v = "\(d["app"]?["version"] ?? "?") (\(d["app"]?["build"] ?? "?"))"
        let os = (d["system"]?["os"] as? String)?.components(separatedBy: " (").first ?? "macOS"
        return "version \(v), \(os), \(d["system"]?["model"] ?? "Mac")"
    }
    private static func sysctl(_ name: String) -> String? {
        var size = 0
        guard sysctlbyname(name, nil, &size, nil, 0) == 0, size > 0 else { return nil }
        var buf = [CChar](repeating: 0, count: size)
        guard sysctlbyname(name, &buf, &size, nil, 0) == 0 else { return nil }
        return String(cString: buf)
    }

    // MARK: Sending

    var canSend: Bool {
        if case .sending = phase { return false }
        return !title.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty && recording == nil
    }

    func send() {
        guard canSend else { return }
        var files = attachments
        if includeCircuit, let d = (target.flatMap { w in CanvasController.front.flatMap { $0.view?.window === w ? $0 : nil } } ?? CanvasController.front)?.document {
            let url = Self.folder.appendingPathComponent("\(UUID().uuidString)-circuit.cdl")
            // Without the student's notes: they're theirs, not the bug's.
            if (try? d.shareText(flags: Int32(CL_SAVE_NO_NOTES)).write(to: url, atomically: true, encoding: .utf8)) != nil {
                files.append(FeedbackAttachment(kind: .circuit, url: url, name: "circuit.cdl", type: "text/plain"))
            }
        }
        var body: [String: Any] = [
            "title": title.trimmingCharacters(in: .whitespacesAndNewlines),
            "details": details.trimmingCharacters(in: .whitespacesAndNewlines),
            "tags": tags,
            "autoTags": suggested,
            "priority": priority.rawValue,
            "name": Prefs.shared.studentName.trimmingCharacters(in: .whitespaces),
            "email": email.trimmingCharacters(in: .whitespaces),
            "contactOK": contactOK && !email.trimmingCharacters(in: .whitespaces).isEmpty,
            "platform": "macos",
            "attachments": files.map { f -> [String: Any] in
                var m: [String: Any] = ["name": f.name, "type": f.type, "size": f.size]
                if f.duration > 0 { m["duration"] = f.duration }
                return m
            },
        ]
        for (k, v) in device { body[k] = v }
        phase = .sending(0, "Sending…")
        Task { await upload(body, files) }
    }

    private func upload(_ body: [String: Any], _ files: [FeedbackAttachment]) async {
        let total = max(1, files.reduce(0) { $0 + $1.size })
        var done = 0
        func progress(_ note: String) {
            let mb = { (n: Int) in String(format: "%.1f", Double(n) / 1_000_000) }
            phase = .sending(Double(done) / Double(total), files.isEmpty ? note : "\(note) \(mb(done)) of \(mb(total)) MB")
        }
        do {
            let created = try await FeedbackServer.create(body)
            for f in files {
                let data = try Data(contentsOf: f.url)
                let pieces = max(1, Int((Double(data.count) / Double(created.chunkSize)).rounded(.up)))
                for i in 0..<pieces {
                    let part = data.subdata(in: (i * created.chunkSize)..<min(data.count, (i + 1) * created.chunkSize))
                    try await FeedbackServer.piece(created, file: f.name, index: i, total: pieces, data: part)
                    done += part.count
                    progress("Sending \(f.kind == .recording ? "the recording" : f.name)…")
                }
            }
            try await FeedbackServer.complete(created)
            finished()
        } catch {
            phase = .failed((error as? FeedbackServer.Failure)?.message ?? "It couldn't be sent: \(error.localizedDescription)")
        }
    }

    private func finished() {
        for a in attachments { try? FileManager.default.removeItem(at: a.url) }
        withAnimation(.spring(response: 0.45, dampingFraction: 0.8)) {
            attachments = []
            title = ""
            details = ""
            chosen = []
            declined = []
            priority = .normal
            includeCircuit = false
            phase = .sent
        }
    }
}

/// cedarlogic.netlify.app's /api/feedback (the cedarlogic-site repository).
enum FeedbackServer {
    struct Created: Decodable { let id: String; let uploadToken: String; let chunkSize: Int }
    struct Failure: Error { let message: String }

    static var base: String {
        ProcessInfo.processInfo.environment["CL_FEEDBACK_URL"]
            ?? Bundle.main.object(forInfoDictionaryKey: "FeedbackURL") as? String ?? "https://cedarlogic.netlify.app"
    }
    static var key: String {
        ProcessInfo.processInfo.environment["CL_FEEDBACK_KEY"] ?? Bundle.main.object(forInfoDictionaryKey: "FeedbackKey") as? String ?? ""
    }

    private static func request(_ path: String, method: String, token: String? = nil, body: Data?, type: String) -> URLRequest {
        var r = URLRequest(url: URL(string: base + path)!, timeoutInterval: 60)
        r.httpMethod = method
        r.httpBody = body
        r.setValue(type, forHTTPHeaderField: "content-type")
        r.setValue(key, forHTTPHeaderField: "x-cedarlogic-key")
        if let token { r.setValue(token, forHTTPHeaderField: "x-upload-token") }
        return r
    }

    /// Tried again a few times: a flaky connection shouldn't lose a report.
    private static func perform(_ r: URLRequest, tries: Int = 3) async throws -> Data {
        var last: Error = Failure(message: "It couldn't be sent.")
        for attempt in 0..<tries {
            do {
                let (data, resp) = try await URLSession.shared.data(for: r)
                let code = (resp as? HTTPURLResponse)?.statusCode ?? 0
                if (200..<300).contains(code) { return data }
                let msg = (try? JSONSerialization.jsonObject(with: data) as? [String: Any])?["error"] as? String
                last = Failure(message: msg ?? "The feedback server said no (\(code)).")
                if code < 500 && code != 429 { throw last }
            } catch let f as Failure {
                throw f
            } catch {
                last = Failure(message: "Couldn't reach the feedback server. Check the internet connection and try again.")
            }
            try await Task.sleep(nanoseconds: UInt64(pow(2, Double(attempt)) * 700_000_000))
        }
        throw last
    }

    static func create(_ body: [String: Any]) async throws -> Created {
        let data = try JSONSerialization.data(withJSONObject: body)
        let out = try await perform(request("/api/feedback", method: "POST", body: data, type: "application/json"))
        return try JSONDecoder().decode(Created.self, from: out)
    }

    static func piece(_ c: Created, file: String, index: Int, total: Int, data: Data) async throws {
        var q = URLComponents()
        q.queryItems = [.init(name: "id", value: c.id), .init(name: "file", value: file),
                        .init(name: "index", value: String(index)), .init(name: "total", value: String(total))]
        _ = try await perform(request("/api/feedback/upload?\(q.percentEncodedQuery ?? "")", method: "PUT", token: c.uploadToken,
                                      body: data, type: "application/octet-stream"))
    }

    static func complete(_ c: Created) async throws {
        _ = try await perform(request("/api/feedback/complete?id=\(c.id)", method: "POST", token: c.uploadToken, body: nil, type: "application/json"))
    }
}

extension FeedbackModel {
    /// Development: CL_FEEDBACK_SELFTEST=1 (with CL_FEEDBACK_URL pointing at
    /// `netlify dev`) takes a screenshot and a three-second recording of the
    /// circuit window, sends them, says SENT or FAILED on stderr and quits.
    /// A draft in progress is put back afterwards.
    static func selfTestIfAsked() {
        guard ProcessInfo.processInfo.environment["CL_FEEDBACK_SELFTEST"] != nil else { return }
        func say(_ s: String) { FileHandle.standardError.write(("feedback self-test: " + s + "\n").data(using: .utf8)!) }
        DispatchQueue.main.asyncAfter(deadline: .now() + 5) {
            let m = shared
            let keep = (m.title, m.details, m.chosen, m.priority)
            aim()
            guard let w = m.target, let r = FeedbackRecorder(window: w) else { say("no circuit window"); exit(1) }
            m.title = "Self-test: the wire doesn't connect when I press C"
            m.details = "Sent by the app's own check: a screenshot and a three-second recording."
            m.priority = .high
            m.screenshot()
            m.recording = r
            r.begin { url, length, thumb in
                m.recording = nil
                if let url { m.addRecording(url, duration: length, thumbnail: thumb) }
                for a in m.attachments {
                    let copy = URL(fileURLWithPath: "/tmp/cl-selftest-\(a.name)")
                    try? FileManager.default.removeItem(at: copy)
                    try? FileManager.default.copyItem(at: a.url, to: copy)
                }
                say("suggested \(m.suggested), tags \(m.tags), attachments \(m.attachments.map { "\($0.name) \($0.size) B \(String(format: "%.1f", $0.duration)) s" })")
                m.send()
                Timer.scheduledTimer(withTimeInterval: 0.25, repeats: true) { _ in
                    MainActor.assumeIsolated {
                        switch m.phase {
                        case .sent:
                            say("SENT")
                            (m.title, m.details, m.chosen, m.priority) = keep
                            exit(0)
                        case .failed(let why):
                            say("FAILED: \(why)")
                            (m.title, m.details, m.chosen, m.priority) = keep
                            exit(1)
                        case .sending(let f, let note): say(String(format: "%.0f%% %@", f * 100, note))
                        default: break
                        }
                    }
                }
            }
            DispatchQueue.main.asyncAfter(deadline: .now() + 2) { try? r.hudPNG()?.write(to: URL(fileURLWithPath: "/tmp/cl-selftest-hud.png")) }
            DispatchQueue.main.asyncAfter(deadline: .now() + 3) { r.stop() }
        }
    }
}
