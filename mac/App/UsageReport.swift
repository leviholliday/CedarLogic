// Anonymous usage stats (docs/PRIVACY.md): the part that needs no window,
// so mac/Tools/usage-check.sh builds it on its own and runs it against a
// stand-in for cedarlogic.netlify.app's /api/usage.
//
//   POST   /api/usage          a report (JSON, below) -> {"ok": true, "haveSnapshot": bool}
//   DELETE /api/usage?id=<id>  everything for that id -> {"ok": true}
//
// A report is built only from the allowlist here (UsageSettings.table) and
// the counters (UsageCounters): never free text, never a name, a path, a
// code or a time more exact than the minute (UTC). The first report after
// the switch goes on is a "snapshot" (every setting that isn't its default);
// after that a "delta" (only the settings that changed since the last report
// the server confirmed, and the counters since then), or a "ping" when
// nothing changed and nothing was counted. haveSnapshot false makes the next
// one a snapshot again. A report that couldn't be sent is kept (7 at most)
// and tried again at the next chance; a 429 waits 24 hours.
//
// What's counted lives in a file of its own in Application Support (0600,
// never in the defaults), written every 5 minutes and at quit; the id is in
// the defaults (cl.usageId), made when the switch goes on and forgotten (the
// server told to delete it) when it goes off.

import CryptoKit
import Foundation

// MARK: - Values

/// A setting's value as it's sent: a bool, a number or one of a few fixed words.
enum UsageValue: Equatable, Codable {
    case bool(Bool), int(Int), double(Double), string(String)

    init(from decoder: Decoder) throws {
        let c = try decoder.singleValueContainer()
        if let b = try? c.decode(Bool.self) { self = .bool(b) }
        else if let i = try? c.decode(Int.self) { self = .int(i) }
        else if let d = try? c.decode(Double.self) { self = .double(d) }
        else { self = .string(try c.decode(String.self)) }
    }
    func encode(to encoder: Encoder) throws {
        var c = encoder.singleValueContainer()
        switch self {
        case .bool(let b): try c.encode(b)
        case .int(let i): try c.encode(i)
        case .double(let d): try c.encode(d)
        case .string(let s): try c.encode(s)
        }
    }
    var json: UsageJSON {
        switch self {
        case .bool(let b): .bool(b)
        case .int(let i): .int(i)
        case .double(let d): .double(d)
        case .string(let s): .string(s)
        }
    }
}

/// JSON written the same way every time: keys sorted, numbers as JavaScript
/// writes them (1, not 1.0; 0.18, not 0.17999999999999999), no spaces. The
/// settings hash is SHA-256 over this form of the settings, so the server can
/// make the same one (JSON.stringify of the object with its keys sorted).
indirect enum UsageJSON {
    case bool(Bool), int(Int), double(Double), string(String), array([UsageJSON]), object([String: UsageJSON])

    func text(pretty: Bool = false, indent: String = "") -> String {
        switch self {
        case .bool(let b): return b ? "true" : "false"
        case .int(let i): return String(i)
        case .double(let d):
            guard d.isFinite else { return "0" }
            if d == d.rounded(), abs(d) < 1e15 { return String(Int(d)) }
            return "\(d)"
        case .string(let s): return Self.quoted(s)
        case .array(let a):
            if a.isEmpty { return "[]" }
            if !pretty { return "[" + a.map { $0.text() }.joined(separator: ",") + "]" }
            let inner = indent + "  "
            return "[\n" + a.map { inner + $0.text(pretty: true, indent: inner) }.joined(separator: ",\n") + "\n" + indent + "]"
        case .object(let o):
            if o.isEmpty { return "{}" }
            let keys = o.keys.sorted()
            if !pretty { return "{" + keys.map { Self.quoted($0) + ":" + o[$0]!.text() }.joined(separator: ",") + "}" }
            let inner = indent + "  "
            return "{\n" + keys.map { inner + Self.quoted($0) + ": " + o[$0]!.text(pretty: true, indent: inner) }.joined(separator: ",\n")
                + "\n" + indent + "}"
        }
    }

    static func quoted(_ s: String) -> String {
        var out = "\""
        for u in s.unicodeScalars {
            switch u {
            case "\"": out += "\\\""
            case "\\": out += "\\\\"
            case "\n": out += "\\n"
            case "\r": out += "\\r"
            case "\t": out += "\\t"
            default:
                if u.value < 0x20 { out += String(format: "\\u%04x", u.value) } else { out.unicodeScalars.append(u) }
            }
        }
        return out + "\""
    }
}

// MARK: - The allowlist

/// One setting that may be sent: the name it's sent under, where it's kept,
/// its default, and for a word, the only words it can be.
struct UsageSetting {
    let name: String
    let key: String
    let def: UsageValue
    var words: [String]? = nil
    /// For a setting that isn't a plain value (the template a new circuit
    /// starts from: a built-in one's id, or just "yours").
    var read: ((UserDefaults) -> UsageValue)? = nil
}

enum UsageSettings {
    /// Every setting a report can carry. Nothing else in the defaults is read:
    /// not your name, your circuits, folders, sync or classroom codes, window
    /// places, recent files or shortcuts. (The names sent avoid the words the
    /// server drops as possibly personal -- name, key, code, id, path... --
    /// so "gateLabels", not "showGateNames".)
    static let table: [UsageSetting] = [
        // Appearance
        .init(name: "themeMode", key: "cl.themeMode", def: .int(0)),            // 0 system, 1 light, 2 dark, 3 last time
        .init(name: "accent", key: "cl.accent", def: .int(6)),
        .init(name: "classicTabs", key: "cl.classicTabs", def: .bool(false)),
        .init(name: "showGrid", key: "cl.showGrid", def: .bool(true)),
        .init(name: "gridStyle", key: "cl.gridStyle", def: .int(0)),
        .init(name: "majorGrid", key: "cl.majorGrid", def: .bool(true)),
        .init(name: "wireThickness", key: "cl.wireThickness", def: .int(1)),
        .init(name: "lowWire", key: "cl.lowWire", def: .int(0)),
        .init(name: "wireDots", key: "cl.wireDots", def: .bool(true)),
        .init(name: "wireDotSize", key: "cl.wireDotSize", def: .double(0.18)),
        .init(name: "gateSize", key: "cl.gateSize", def: .int(48)),
        .init(name: "gateLabels", key: "cl.showGateNames", def: .bool(true)),
        .init(name: "categoryShortcuts", key: "cl.showCategoryKeys", def: .bool(true)),
        // Toolbar and window
        .init(name: "toolbarStyle", key: "cl.toolbarStyle", def: .int(3)),
        .init(name: "toolbarHidden", key: "cl.toolbarHidden", def: .int(0)),   // which tool groups are hidden (bits)
        .init(name: "showTitle", key: "cl.showTitle", def: .bool(true)),
        .init(name: "showStepClock", key: "cl.showStepClock", def: .bool(true)),
        .init(name: "showThemeToggle", key: "cl.showThemeToggle", def: .bool(true)),
        .init(name: "showStatus", key: "cl.showStatus", def: .bool(true)),
        .init(name: "actionsOn", key: "cl.actions.on", def: .bool(true)),
        .init(name: "actionsCollapsed", key: "cl.actions.collapsed", def: .bool(false)),
        .init(name: "actionsHoverOpen", key: "cl.actions.hoverOpen", def: .bool(true)),
        // Canvas and editing
        .init(name: "mouseWheel", key: "cl.mouseWheel", def: .int(0)),
        .init(name: "trackpadScroll", key: "cl.trackpadScroll", def: .int(1)),
        .init(name: "reverseWheel", key: "cl.reverseWheel", def: .bool(false)),
        .init(name: "reverseTrackpad", key: "cl.reverseTrackpad", def: .bool(false)),
        .init(name: "rightClickRotate", key: "cl.rightClickRotate", def: .bool(false)),
        .init(name: "duplicateClipboard", key: "cl.duplicateClipboard", def: .bool(false)),
        .init(name: "wireValueTag", key: "cl.wireValueTag", def: .bool(false)),
        .init(name: "tidyMode", key: "tidyMode", def: .int(0)),
        .init(name: "themeShortcutOn", key: "cl.themeShortcutOn", def: .bool(true)),
        // Circuits and pages
        .init(name: "openReplaces", key: "cl.openReplaces", def: .bool(true)),
        .init(name: "newTemplate", key: "cl.newTemplate", def: .string(""), read: { d in
            let t = d.string(forKey: "cl.newTemplate") ?? ""
            if t.isEmpty { return .string("") }
            // A built-in template's id says which; one of yours is just "yours" (its id or name never goes).
            let builtIn = t.hasPrefix("builtin-") && t.count <= 32 && t.allSatisfy { $0.isASCII && ($0.isLetter || $0.isNumber || $0 == "-") }
            return .string(builtIn ? t : "yours")
        }),
        .init(name: "newPagesShareLinks", key: "cl.newPagesShareLinks", def: .bool(true)),
        // Export, simulation, the app
        .init(name: "exportInfo", key: "cl.exportInfo", def: .bool(true)),
        .init(name: "timingColor", key: "cl.timingColor", def: .bool(false)),
        .init(name: "projector", key: "cl.projector", def: .bool(false)),
        .init(name: "confirmQuit", key: "cl.confirmQuit", def: .bool(true)),
        .init(name: "introEveryLaunch", key: "cl.introEveryLaunch", def: .bool(false)),
        .init(name: "testingGroup", key: "testingGroup", def: .string("normal"), words: ["normal", "beta"]),
        .init(name: "classroomEnabled", key: "ClassroomEnabled", def: .bool(true)),
    ]

    /// One setting's value now, made safe: the default when it isn't set or
    /// isn't the right kind; a number rounded to 2 places; a word from its list or "other".
    static func value(_ s: UsageSetting, in d: UserDefaults) -> UsageValue {
        if let read = s.read { return read(d) }
        guard let o = d.object(forKey: s.key) else { return s.def }
        switch s.def {
        case .bool: return (o as? NSNumber).map { .bool($0.boolValue) } ?? s.def
        case .int: return (o as? NSNumber).map { .int(max(-1_000_000, min(1_000_000, $0.intValue))) } ?? s.def
        case .double:
            guard let n = o as? NSNumber, n.doubleValue.isFinite else { return s.def }
            return .double((n.doubleValue * 100).rounded() / 100)
        case .string:
            guard let t = o as? String else { return s.def }
            return .string(s.words?.contains(t) == true ? t : "other")
        }
    }

    /// Every allowlisted setting that differs from its default.
    static func nonDefault(_ d: UserDefaults) -> [String: UsageValue] {
        var out: [String: UsageValue] = [:]
        for s in table {
            let v = value(s, in: d)
            if v != s.def { out[s.name] = v }
        }
        return out
    }

    static func defaultValue(_ name: String) -> UsageValue? { table.first { $0.name == name }?.def }

    /// SHA-256 (hex) of the canonical JSON of the full non-default settings.
    static func hash(_ settings: [String: UsageValue]) -> String {
        let text = UsageJSON.object(settings.mapValues(\.json)).text()
        return SHA256.hash(data: Data(text.utf8)).map { String(format: "%02x", $0) }.joined()
    }
}

// MARK: - What's counted

struct UsageCounters: Codable, Equatable {
    var sessions = 0
    var activeSeconds = 0
    var focusSeconds = 0
    var simViewSeconds = 0
    var classroomSessions = 0
    var actions: [String: Int] = [:]

    static let maxActions = 200

    var isZero: Bool {
        sessions == 0 && activeSeconds == 0 && focusSeconds == 0 && simViewSeconds == 0 && classroomSessions == 0
            && actions.values.allSatisfy { $0 == 0 }
    }

    /// An action's id, as the registry names it (letters and digits): anything else is never counted.
    static func isActionId(_ s: String) -> Bool {
        (1...40).contains(s.count) && s.allSatisfy { $0.isASCII && ($0.isLetter || $0.isNumber || $0 == "_") }
    }

    mutating func count(_ action: String) {
        guard Self.isActionId(action) else { return }
        if actions[action] == nil && actions.count >= Self.maxActions { return }
        actions[action, default: 0] += 1
    }
}

/// A report waiting to go (or kept after a failure): sent as `body(...)`.
struct UsagePending: Codable {
    var kind: String
    var from: String
    var to: String
    /// What it sends (snapshot: all of `full`; delta: what changed; ping: nil).
    var settings: [String: UsageValue]?
    /// Every non-default setting when it was made: what the server has once it's confirmed.
    var full: [String: UsageValue]
    var hash: String
    var counters: UsageCounters?
    var syncOn: Bool
    var tries = 0

    mutating func makeSnapshot() {
        kind = "snapshot"
        settings = full
        if counters == nil { counters = UsageCounters() }
    }
}

/// The local file: counters since the last report, what the server last
/// confirmed, and reports still to send.
struct UsageState: Codable {
    var counters = UsageCounters()
    var periodFrom: String?
    var confirmed: [String: UsageValue]?
    var confirmedHash: String?
    var needSnapshot = true
    var lastSentAt: Date?
    var lastSentKind: String?
    var lastBuiltAt: Date?
    var retryAfter: Date?
    var queue: [UsagePending] = []
    /// Ids the server still has to be told to forget (the switch went off offline).
    var pendingDeletes: [String] = []
}

/// What a report says about the app (no more).
struct UsageApp {
    var platform = "mac"
    var version: String
    var build: Int
    var os: String
    var arch: String
    var lang: String

    static var current: UsageApp {
        let info = Bundle.main.infoDictionary ?? [:]
        let v = ProcessInfo.processInfo.operatingSystemVersion
        #if arch(arm64)
        let arch = "arm64"
        #else
        let arch = "x86_64"
        #endif
        // The language only ("en"), never the region.
        let lang = Locale.preferredLanguages.first.map { Locale(identifier: $0).language.languageCode?.identifier ?? "" } ?? ""
        let safeLang = (2...3).contains(lang.count) && lang.allSatisfy({ $0.isASCII && $0.isLetter }) ? lang.lowercased() : "und"
        let version = (info["CFBundleShortVersionString"] as? String).flatMap { s in
            s.count <= 16 && s.allSatisfy({ $0.isASCII && ($0.isNumber || $0 == ".") }) ? s : nil
        } ?? "0"
        return UsageApp(version: version, build: Int(info["CFBundleVersion"] as? String ?? "") ?? 0,
                        os: "\(v.majorVersion).\(v.minorVersion)", arch: arch, lang: safeLang)
    }

    var json: UsageJSON {
        .object(["platform": .string(platform), "version": .string(version), "build": .int(build),
                 "os": .string(os), "arch": .string(arch), "lang": .string(lang)])
    }
}

// MARK: - The engine

@MainActor
final class UsageEngine {
    static let onKey = "cl.usageStats"
    static let idKey = "cl.usageId"
    static let policyKey = "DisableUsageStats"
    static let maxKept = 7
    static let maxBytes = 16 * 1024
    static let day: TimeInterval = 86_400

    let defaults: UserDefaults
    let file: URL
    var now: () -> Date = { Date() }
    var syncOn: () -> Bool = { false }
    /// Where reports go: the API's own address, .../api/usage (nil: nowhere; see UsageEndpoint).
    var endpoint: () -> URL? = { UsageEndpoint.url }
    var app: UsageApp = .current
    private(set) var state = UsageState()
    private var sending = false
    /// What the last tries did, for the checks ("sent snapshot", "kept: 500"...).
    private(set) var log: [String] = []
    /// The server's last haveSnapshot.
    private(set) var lastHaveSnapshot: Bool?

    init(defaults: UserDefaults = .standard, directory: URL) {
        self.defaults = defaults
        file = directory.appendingPathComponent("usage.json")
        load()
    }

    // MARK: On and off

    var policyOff: Bool { defaults.bool(forKey: Self.policyKey) }
    var switchedOn: Bool { defaults.bool(forKey: Self.onKey) }
    /// Counting and sending: the switch is on and no policy says no.
    var enabled: Bool { switchedOn && !policyOff && id != nil }
    var id: String? {
        guard let s = defaults.string(forKey: Self.idKey), Self.isUsageId(s) else { return nil }
        return s
    }
    static func isUsageId(_ s: String) -> Bool {
        (22...64).contains(s.count) && s.allSatisfy { $0.isASCII && ($0.isLetter || $0.isNumber || $0 == "_" || $0 == "-") }
    }
    /// Random, from nothing about this Mac: a UUID's 32 hex digits.
    static func newId() -> String { UUID().uuidString.replacingOccurrences(of: "-", with: "") }

    /// The switch: on makes a fresh id (and the next report a snapshot); off
    /// forgets it, and what was counted, and asks the server to delete it.
    func set(on: Bool) async {
        if on {
            guard !switchedOn || id == nil else { return }
            defaults.set(true, forKey: Self.onKey)
            defaults.set(Self.newId(), forKey: Self.idKey)
            let deletes = state.pendingDeletes
            state = UsageState()
            state.pendingDeletes = deletes
            state.periodFrom = Self.date(now())
            save()
        } else {
            let old = id
            defaults.set(false, forKey: Self.onKey)
            defaults.removeObject(forKey: Self.idKey)
            let deletes = state.pendingDeletes + (old.map { [$0] } ?? [])
            state = UsageState()
            state.pendingDeletes = deletes
            save()
            await flushDeletes()
        }
    }

    /// Settings > Privacy > Delete what's been sent: the server forgets this
    /// id; a new one (and a snapshot) follows if the switch stays on.
    func deleteSent() async -> Bool {
        guard let old = id else { return true }
        state.pendingDeletes.append(old)
        save()
        await flushDeletes()
        let done = !state.pendingDeletes.contains(old)
        if done && switchedOn {
            defaults.set(Self.newId(), forKey: Self.idKey)
            let deletes = state.pendingDeletes
            state = UsageState()
            state.pendingDeletes = deletes
            state.periodFrom = Self.date(now())
            save()
        }
        return done
    }

    // MARK: Counting

    func noteSession() { guard enabled else { return }; state.counters.sessions += 1 }
    func noteClassroom() { guard enabled else { return }; state.counters.classroomSessions += 1 }
    func count(_ action: String) { guard enabled else { return }; state.counters.count(action) }
    func addTime(active: Int, focus: Int, simView: Int) {
        guard enabled else { return }
        state.counters.activeSeconds += active
        state.counters.focusSeconds += focus
        state.counters.simViewSeconds += simView
    }
    var counters: UsageCounters { state.counters }
    var lastSentAt: Date? { state.lastSentAt }
    var lastSentKind: String? { state.lastSentKind }
    var keptCount: Int { state.queue.count }

    // MARK: Reports

    static func date(_ d: Date) -> String { format(d, "yyyy-MM-dd") }
    /// The minute, in UTC: "2026-11-04T18:05Z".
    static func minute(_ d: Date) -> String { format(d, "yyyy-MM-dd'T'HH:mm'Z'") }
    private static func format(_ d: Date, _ f: String) -> String {
        let df = DateFormatter()
        df.locale = Locale(identifier: "en_US_POSIX")
        df.timeZone = TimeZone(identifier: "UTC")
        df.dateFormat = f
        return df.string(from: d)
    }

    /// The report the next send would make from what's counted now (it's not
    /// queued; the counters stay).
    func nextReport() -> UsagePending {
        let full = UsageSettings.nonDefault(defaults)
        let hash = UsageSettings.hash(full)
        let counters = state.counters
        let from = state.periodFrom ?? Self.date(now())
        let to = Self.date(now())
        let sync = syncOn()
        if state.needSnapshot || state.confirmed == nil {
            return UsagePending(kind: "snapshot", from: from, to: to, settings: full, full: full, hash: hash, counters: counters, syncOn: sync)
        }
        let confirmed = state.confirmed ?? [:]
        var changed: [String: UsageValue] = [:]
        for (k, v) in full where confirmed[k] != v { changed[k] = v }
        // Back to its default: the default is what it is now.
        for k in confirmed.keys where full[k] == nil { if let d = UsageSettings.defaultValue(k) { changed[k] = d } }
        if changed.isEmpty && counters.isZero {
            return UsagePending(kind: "ping", from: from, to: to, settings: nil, full: full, hash: hash, counters: nil, syncOn: sync)
        }
        return UsagePending(kind: "delta", from: from, to: to, settings: changed, full: full, hash: hash, counters: counters, syncOn: sync)
    }

    /// The JSON that goes, as an object.
    func body(_ p: UsagePending, id: String, sentAt: Date) -> UsageJSON {
        var o: [String: UsageJSON] = [
            "v": .int(1), "id": .string(id), "app": app.json, "kind": .string(p.kind),
            "sentAt": .string(Self.minute(sentAt)),
            "period": .object(["from": .string(p.from), "to": .string(p.to)]),
            "settingsHash": .string(p.hash),
        ]
        if p.kind != "ping", let s = p.settings { o["settings"] = .object(s.mapValues(\.json)) }
        if let c = p.counters, !(p.kind == "ping" && c.isZero) {
            o["counters"] = .object([
                "sessions": .int(c.sessions), "activeSeconds": .int(c.activeSeconds), "focusSeconds": .int(c.focusSeconds),
                "simViewSeconds": .int(c.simViewSeconds), "classroomSessions": .int(c.classroomSessions),
                "syncOn": .bool(p.syncOn), "actions": .object(c.actions.mapValues { .int($0) }),
            ])
        }
        return .object(o)
    }

    /// The bytes that go: at most 16 KB (the least-used actions go first if it's more).
    func encoded(_ p: UsagePending, id: String, sentAt: Date) -> Data {
        var p = p
        var text = body(p, id: id, sentAt: sentAt).text()
        while text.utf8.count > Self.maxBytes, var c = p.counters, !c.actions.isEmpty {
            let drop = c.actions.sorted { ($0.value, $1.key) < ($1.value, $0.key) }.prefix(max(1, c.actions.count / 4)).map(\.key)
            for k in drop { c.actions[k] = nil }
            p.counters = c
            text = body(p, id: id, sentAt: sentAt).text()
        }
        return Data(text.utf8)
    }

    /// "See exactly what's sent": the next report, pretty-printed, as it
    /// would go now (before the switch is on, with a stand-in id).
    func preview() -> String {
        body(nextReport(), id: id ?? "made-when-you-turn-this-on", sentAt: now()).text(pretty: true)
    }

    // MARK: Sending

    enum Answer { case ok(haveSnapshot: Bool), tooMany, refused(Int), failed(String) }

    private var session: URLSession {
        let c = URLSessionConfiguration.ephemeral
        c.timeoutIntervalForRequest = 15
        c.timeoutIntervalForResource = 15
        c.httpCookieAcceptPolicy = .never
        c.httpShouldSetCookies = false
        c.httpCookieStorage = nil
        c.urlCache = nil
        c.requestCachePolicy = .reloadIgnoringLocalCacheData
        return URLSession(configuration: c)
    }

    private var userAgent: String { "CedarLogic/\(app.version) (mac)" }

    private func post(_ data: Data, to api: URL) async -> Answer {
        var r = URLRequest(url: api, timeoutInterval: 15)
        r.httpMethod = "POST"
        r.httpBody = data
        r.setValue("application/json", forHTTPHeaderField: "content-type")
        r.setValue(userAgent, forHTTPHeaderField: "user-agent")
        do {
            let (out, resp) = try await session.data(for: r)
            let code = (resp as? HTTPURLResponse)?.statusCode ?? 0
            if code == 200 {
                let j = (try? JSONSerialization.jsonObject(with: out)) as? [String: Any] ?? [:]
                return .ok(haveSnapshot: j["haveSnapshot"] as? Bool ?? true)
            }
            if code == 429 { return .tooMany }
            if (400..<500).contains(code) && code != 408 { return .refused(code) }
            return .failed("\(code)")
        } catch {
            return .failed("no connection")
        }
    }

    /// DELETE for each id the server still has to forget.
    func flushDeletes() async {
        guard !state.pendingDeletes.isEmpty, let base = endpoint() else { return }
        for old in state.pendingDeletes {
            var q = URLComponents(url: base, resolvingAgainstBaseURL: false)!
            q.queryItems = [URLQueryItem(name: "id", value: old)]
            var r = URLRequest(url: q.url!, timeoutInterval: 15)
            r.httpMethod = "DELETE"
            r.setValue(userAgent, forHTTPHeaderField: "user-agent")
            let code = ((try? await session.data(for: r))?.1 as? HTTPURLResponse)?.statusCode ?? 0
            if code == 200 || code == 404 {
                state.pendingDeletes.removeAll { $0 == old }
                log.append("deleted")
            } else {
                log.append("delete kept: \(code)")
            }
        }
        save()
    }

    /// At launch, and every few minutes after: a report if one's due (24 h
    /// since the last one went or was made), then whatever's kept, oldest first.
    @discardableResult
    func sendIfDue(force: Bool = false) async -> Bool {
        guard !sending else { return false }
        sending = true
        defer { sending = false }
        await flushDeletes()
        guard enabled, let id, let base = endpoint() else { return false }
        let t = now()
        if let ra = state.retryAfter, t < ra { return false }
        state.retryAfter = nil
        let last = [state.lastSentAt, state.lastBuiltAt].compactMap { $0 }.max()
        if force || last.map({ t.timeIntervalSince($0) >= Self.day }) ?? true {
            enqueue(nextReport())
            state.lastBuiltAt = t
        }
        var followedUp = false
        var sentAny = false
        while let head = state.queue.first {
            let answer = await post(encoded(head, id: id, sentAt: now()), to: base)
            // The switch went off (or the id changed) while it was going.
            guard self.id == id, enabled else { return sentAny }
            switch answer {
            case .ok(let have):
                lastHaveSnapshot = have
                state.queue.removeFirst()
                state.confirmed = head.full
                state.confirmedHash = head.hash
                state.lastSentAt = now()
                state.lastSentKind = head.kind
                if head.kind == "snapshot" { state.needSnapshot = false }
                sentAny = true
                log.append("sent \(head.kind)")
                if !have {
                    // The server has nothing to apply changes to: a snapshot next.
                    state.needSnapshot = true
                    if !state.queue.isEmpty {
                        state.queue[0].makeSnapshot()
                    } else if head.kind != "snapshot" && !followedUp {
                        followedUp = true
                        enqueue(nextReport())
                    }
                }
            case .tooMany:
                state.retryAfter = now().addingTimeInterval(Self.day)
                log.append("kept: 429")
                save()
                return sentAny
            case .refused(let code):
                // The server won't take this one; tried three times, it goes.
                state.queue[0].tries += 1
                if state.queue[0].tries >= 3 { state.queue.removeFirst() }
                log.append("kept: \(code)")
                save()
                return sentAny
            case .failed(let why):
                state.queue[0].tries += 1
                log.append("kept: \(why)")
                save()
                return sentAny
            }
            save()
        }
        return sentAny
    }

    /// A made report joins the queue (the oldest goes past 7) and the counting starts again.
    private func enqueue(_ p: UsagePending) {
        state.queue.append(p)
        if state.queue.count > Self.maxKept { state.queue.removeFirst(state.queue.count - Self.maxKept) }
        state.counters = UsageCounters()
        state.periodFrom = Self.date(now())
        save()
    }

    // MARK: The file

    func load() {
        guard let data = try? Data(contentsOf: file), let s = try? JSONDecoder().decode(UsageState.self, from: data) else { return }
        state = s
    }

    /// Written whole, then made 0600 (its folder 0700).
    func save() {
        let fm = FileManager.default
        let dir = file.deletingLastPathComponent()
        try? fm.createDirectory(at: dir, withIntermediateDirectories: true, attributes: [.posixPermissions: 0o700])
        guard let data = try? JSONEncoder().encode(state) else { return }
        let tmp = dir.appendingPathComponent(".usage.json.tmp")
        fm.createFile(atPath: tmp.path, contents: data, attributes: [.posixPermissions: 0o600])
        if fm.fileExists(atPath: file.path) {
            _ = try? fm.replaceItemAt(file, withItemAt: tmp)
        } else {
            try? fm.moveItem(at: tmp, to: file)
        }
        try? fm.setAttributes([.posixPermissions: 0o600], ofItemAtPath: file.path)
    }
}

// MARK: - Where it goes

enum UsageEndpoint {
    static let realBundle = "com.leviholliday.cedarlogic.native"

    /// https://cedarlogic.netlify.app/api/usage. CL_USAGE_URL may point at a
    /// stand-in on this Mac (127.0.0.1, localhost) or, over HTTPS, the site or
    /// one of its Netlify drafts (<name>--cedarlogic.netlify.app), and nowhere
    /// else; with or without the /api/usage. A test copy of the app, a
    /// command-line check or CI sends nowhere unless it's pointed at one.
    static var url: URL? {
        let env = ProcessInfo.processInfo.environment
        if let test = env["CL_USAGE_URL"], !test.isEmpty {
            guard let u = URL(string: test), let host = u.host?.lowercased() else { return nil }
            let local = ["127.0.0.1", "localhost", "::1"].contains(host) && ["http", "https"].contains(u.scheme ?? "")
            let site = u.scheme == "https" && (host == "cedarlogic.netlify.app" || host.hasSuffix("--cedarlogic.netlify.app"))
            guard local || site else { return nil }
            return api(u)
        }
        let args = CommandLine.arguments
        let testRun = Bundle.main.bundleIdentifier != realBundle || env["CI"] != nil
            || env.keys.contains { $0.hasPrefix("CL_") }
            || args.contains { $0.hasPrefix("--render") || $0.hasSuffix("-check") || $0.hasSuffix("-selftest") }
        if testRun { return nil }
        let s = Bundle.main.object(forInfoDictionaryKey: "UsageURL") as? String ?? "https://cedarlogic.netlify.app"
        guard let u = URL(string: s), u.scheme == "https", u.host != nil else { return nil }
        return api(u)
    }

    /// The address given, ending in /api/usage.
    static func api(_ u: URL) -> URL {
        u.path.hasSuffix("/api/usage") ? u : u.appendingPathComponent("api/usage")
    }
}

// MARK: - The leak scan

enum UsageLeakScan {
    /// Every string from `needles` (4 characters or more, any case) found in
    /// `report` that isn't simply one of the words a report is made of.
    static func leaks(in report: String, needles: [String]) -> [String] {
        let text = report.lowercased()
        let vocabulary = vocabularyText.lowercased()
        var found: [String] = []
        for raw in Set(needles) {
            let n = raw.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
            guard n.count >= 4, text.contains(n), !vocabulary.contains(n) else { continue }
            found.append(raw)
        }
        return found.sorted()
    }

    /// The words a report can hold whatever the Mac: its keys, the setting
    /// names and words, the counters', the kinds. (A user called "show" would
    /// otherwise "leak" through showGrid.)
    static var vocabularyText: String {
        var words = ["platform", "version", "build", "arch", "lang", "kind", "snapshot", "delta", "ping", "sentAt", "period",
                     "from", "settings", "settingsHash", "counters", "sessions", "activeSeconds", "focusSeconds",
                     "simViewSeconds", "classroomSessions", "syncOn", "actions", "true", "false", "arm64", "x86_64",
                     "made-when-you-turn-this-on", "yours", "other", "api/usage"]
        for s in UsageSettings.table {
            words.append(s.name)
            words += s.words ?? []
            if s.name == "newTemplate" { words.append("builtin-") }
        }
        return words.joined(separator: " ")
    }

    /// The strings that would mean a leak on this Mac: the home folder and
    /// its name, the user and full name (and its parts), the host name.
    static func machineNeedles() -> [String] {
        var n = [NSHomeDirectory(), (NSHomeDirectory() as NSString).lastPathComponent, NSUserName(), NSFullUserName()]
        n += NSFullUserName().split(separator: " ").map(String.init)
        let host = ProcessInfo.processInfo.hostName
        n.append(host)
        if let first = host.split(separator: ".").first { n.append(String(first)) }
        if let name = Host.current().localizedName { n.append(name) }
        return n.filter { !$0.isEmpty }
    }
}
