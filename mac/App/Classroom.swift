// Classroom on the Mac (docs/CLASSROOM.md in the website's repository, §4-§5,
// and v2, §3.16): the shared core (mac/CedarCore/Classroom*.cpp through
// CedarClassroom.h) with the hooks of ClassroomHooks.swift, plus the app's own
// part -- the UI hooks, Check My Circuit on hand-ins (check_circuit), the
// students' own copies in Your Circuits (assignments, shared circuits and class
// examples), handing in from them, hand-ins opened as scratch copies, Download
// All, answer keys made from a solution, and the classes Sync carries.
// ClassroomView.swift draws it; RenderClassroom.swift pictures and tests it.
//
// Behind a flag (ClassroomFlag), on by default since the service is deployed: off,
// nothing here starts and no menu shows.

import AppKit
import SwiftUI
import UniformTypeIdentifiers

/// Whether Classroom shows at all: `defaults write <the app's bundle id> ClassroomEnabled -bool YES`
/// or CL_CLASSROOM=1 in the environment (which wins either way). On by default (the service is
/// deployed); `ClassroomEnabled -bool NO` or CL_CLASSROOM=0 turns it off.
enum ClassroomFlag {
    static let defaultsKey = "ClassroomEnabled"
    static var on: Bool {
        if let e = ProcessInfo.processInfo.environment["CL_CLASSROOM"] { return e == "1" || e.lowercased() == "yes" }
        return UserDefaults.standard.object(forKey: defaultsKey) as? Bool ?? true
    }
}

// MARK: - What the views show

struct CRClass: Identifiable, Hashable {
    var id: String
    var name: String
    var teaching: Bool
    var teacherKey = ""
    var joinCode = ""
    var joinOpen = true
    var studentName = ""
    var expiresAt: Date?
    var live = false
    var warning = ""
    /// v2 (3.16.4): when the expiry warning shows.
    var warnAt: Date?
    /// v2, student: shared circuits and examples not opened yet.
    var news = 0
    /// 0 idle, 1 working, 2 offline, 3 error, 4 gone (cl_classroom_status_kind).
    var statusKind = 0
    var statusText = ""
}

struct CRAssignment: Identifiable, Hashable {
    var id: String
    var title: String
    var instructions = ""
    var cdl = ""
    var dueAt: Date?
    var closeAfterDue = false
    var ver: Int64 = 1
    var keyText = ""
    var keyNames = ""
    var keySealed = false
    var handedInAt: Date?
    var attempts = 0
    var changedSince = false
    var pending = false
    var closed = false
    var unreadable = false
    var problem = ""
    /// The teacher posted a key of some kind (readable or sealed).
    var hasKey: Bool { keySealed || !keyText.isEmpty }
}

/// v2 (3.16.2): a shared circuit ("share") or a class example ("example").
struct CRItem: Identifiable, Hashable {
    var id: String
    var type = "example"
    var title = ""
    var topic = ""
    var note = ""
    var cdl = ""
    var ver: Int64 = 1
    var createdAt: Date?
    var releasedAt: Date?
    var hidden = false
    /// Student: "new", "updated" or "".
    var news = ""
    var unreadable = false
    var problem = ""
    var isShare: Bool { type == "share" }
}

/// "Your teacher shared “X”" and the like: one item's news, until it's opened or dismissed.
struct CRNews: Identifiable, Hashable {
    var classId: String
    var itemId: String
    var what: String
    var type: String
    var title: String
    var id: String { "\(classId)/\(itemId)" }
    var text: String {
        what == "updated" ? "Updated: “\(title)”" : type == "share" ? "Your teacher shared “\(title)”" : "Your teacher added “\(title)”"
    }
}

struct CRStudent: Identifiable, Hashable {
    var id: String
    var name: String
    var joinedAt: Date?
    var seenAt: Date?
    var unreadable = false
}

struct CRSubmission: Identifiable, Hashable {
    var studentId: String
    var name: String
    var cdl = ""
    var handedInAt: Date?
    var attempts = 1
    /// -1 not checked, 0 matches, 1 wrong rows, 2 couldn't check.
    var verdict = -1
    var summary = ""
    var unreadable = false
    var problem = ""
    var left = false
    var ver: Int64 = 0
    var id: String { "\(studentId)/\(ver)" }
}

// MARK: - Wording shared by the views (CLASSROOM.md §5)

enum ClassroomText {
    static func shortDate(_ d: Date) -> String {
        let f = DateFormatter()
        f.dateFormat = "EEE d MMM, h:mm a"
        return f.string(from: d)
    }
    static func dayTime(_ d: Date) -> String {
        let f = DateFormatter()
        f.dateFormat = "d MMM h:mm a"
        return f.string(from: d)
    }
    static func longDay(_ d: Date) -> String {
        let f = DateFormatter()
        f.dateFormat = "d MMM yyyy"
        return f.string(from: d)
    }
    /// "Due Fri 10 Oct, 11:59 PM" / "Was due …" / "Hand-ins closed" / "No due date".
    static func dueLine(_ a: CRAssignment, now: Date = Date()) -> String {
        if a.closed { return "Hand-ins closed" }
        guard let due = a.dueAt else { return "No due date" }
        return due < now ? "Was due \(shortDate(due))" : "Due \(shortDate(due))"
    }
    /// "Not handed in" / "Handed in ✓ 4 Oct 10:31" (· changed since) / pending (3.16.3).
    static func handInLine(_ a: CRAssignment, changed: Bool? = nil) -> String {
        if a.pending { return "Will hand in when you're online." }
        guard let at = a.handedInAt else { return "Not handed in" }
        return "Handed in ✓ \(dayTime(at))" + ((changed ?? a.changedSince) ? " · changed since" : "")
    }
    static func checkLine(_ s: CRSubmission, hasKey: Bool) -> String {
        if s.unreadable { return s.problem.isEmpty ? "Couldn't be read" : s.problem }
        switch s.verdict {
        case 0: return "Matches"
        case 1: return s.summary.isEmpty ? "Rows wrong" : s.summary
        case 2: return s.summary.isEmpty ? "Couldn't check" : "Couldn't check: \(s.summary)"
        default: return hasKey ? "Checking…" : "—"
        }
    }
    static func students(_ n: Int) -> String { n == 1 ? "1 student" : "\(n) students" }
    static let createBlurb = "Students join with a short code you write on the board. Their names and what they hand in are encrypted for you before they leave their devices; CedarLogic's website can't read them. There's no account: a teacher key is made for you now — keep it."
    static let keyBlurb = "This key opens the class on any of your devices: CedarLogic Online, the CedarLogic app, your phone. It is the only way back in. Keep a copy somewhere safe — print the recovery sheet or save the link. If you lose every device that has it and this key, nobody can read the hand-ins, not even CedarLogic's website."
    static let keyPrivate = "Keep it private: anyone with it can read everything in the class and change it. (Students never need it.) On a shared computer, use Remove from This Device in the class's settings when you're done."
    static let keyRecoveryUse = "To use it: CedarLogic Online › Classroom › I Have a Teacher Key, or scan the code. Store this sheet like a password."
    static let joinPrivacy = "Your name and what you hand in are encrypted for your teacher; the website can't read them. Nothing else about you is sent. On a shared computer, use Remove from This Device on the class page when you're done."
    static let keyStudentsCheck = "Students can check: the key is sent to students' devices, encrypted for the class, so Check My Circuit works for them — and a curious student can find the answer in it. Good for practice."
    static let keyOnlyMe = "Only I check: the key is encrypted for you alone; you see each hand-in's result when you open the hand-ins."
    static let moveBlurb = "On your other device: Classroom › Join a Class › I have a move code — or scan this. It works for 10 minutes. Anyone who gets this code can hand in as you, so don't share it."
    static let offline = "Can't reach the website."
}

// MARK: - The center

/// Everything the Classroom window shows, and every action, over one engine.
/// `shared` is the app's (~/Library/Application Support/CedarLogic/Classroom);
/// the end-to-end check (RenderClassroom) makes two of its own, a teacher's and
/// a student's, each in a folder of its own; previews make one with no engine.
@MainActor
final class ClassroomCenter: ObservableObject {
    static let shared = ClassroomCenter(dir: ClassroomPlatform.defaultDir)

    @Published var classes: [CRClass] = []
    @Published var assignments: [String: [CRAssignment]] = [:]
    @Published var students: [String: [CRStudent]] = [:]
    /// By "<classId>/<aid>".
    @Published var submissions: [String: [CRSubmission]] = [:]
    @Published var submissionsUpdated: [String: Date] = [:]
    /// v2: shared circuits and class examples, by class.
    @Published var items: [String: [CRItem]] = [:]
    /// v2: a hand-in's earlier attempts, by "<classId>/<aid>/<sid>" (oldest first).
    @Published var history: [String: [CRSubmission]] = [:]
    /// v2: "Your teacher shared …", newest last, until opened or dismissed.
    @Published var news: [CRNews] = []
    /// The latest notice from the core ("You were removed from …"), shown as the window's banner.
    @Published var notice: String?
    /// The class the window shows.
    @Published var selected: String?
    /// The window's sheets (ClassroomView).
    @Published var sheet: ClassroomSheet?

    let preview: Bool
    let dir: URL
    private(set) var engine: OpaquePointer?
    private var platform: ClassroomPlatform?
    private var started = false
    private var watchers: [Any] = []
    private var lastUserActive = Date.distantPast
    /// Which hand-ins views are open, so a change refreshes them.
    private var openSubmissions: Set<String> = []

    init(dir: URL, preview: Bool = false) {
        self.dir = dir
        self.preview = preview
    }

    /// A stand-in for pictures: shows what it's given.
    static func previewing() -> ClassroomCenter { ClassroomCenter(dir: URL(fileURLWithPath: "/dev/null"), preview: true) }

    // MARK: Starting and stopping

    /// Starts the engine (once). Nothing happens with the flag off, except for a center made for checks.
    func start(force: Bool = false) {
        guard !preview, !started, force || ClassroomFlag.on else { return }
        started = true
        let sync = SyncPlatform(syncDir: SyncPlatform.defaultSyncDir)
        let p = ClassroomPlatform(dir: dir, sync: sync)
        platform = p
        var h = p.hooks()
        Self.registry[UInt(bitPattern: Unmanaged.passUnretained(p).toOpaque())] = Weak(self)
        h.classes_changed = { ctx in ClassroomCenter.post(ctx) { $0.refresh() } }
        h.status_changed = { ctx, _ in ClassroomCenter.post(ctx) { $0.refreshClasses() } }
        h.notice = { ctx, text in
            let t = text.map { String(cString: $0) } ?? ""
            ClassroomCenter.post(ctx) { $0.notice = t }
        }
        h.submissions_changed = { ctx, cid, aid in
            let c1 = cid.map { String(cString: $0) } ?? "", a1 = aid.map { String(cString: $0) } ?? ""
            ClassroomCenter.post(ctx) { c in
                if c.openSubmissions.contains("\(c1)/\(a1)") { c.fetchSubmissions(c1, a1) }
            }
        }
        // Taught and joined classes over Sync (3.16.7): the sync engine's side records. Only the app's
        // own center (the folder Sync belongs with); a check's centers have none.
        if dir == ClassroomPlatform.defaultDir {
            h.sync_on = { _ in ClassroomSide.on }
            h.sync_side_count = { _ in ClassroomSide.load() }
            h.sync_side = { _, i, rid in ClassroomSide.record(Int(i), rid) }
            h.sync_put_side = { _, rid, json in
                SyncCenter.shared.putSide(json.map { String(cString: $0) } ?? "", rid: rid.map { String(cString: $0) } ?? "")
            }
            h.sync_delete_side = { _, rid in SyncCenter.shared.deleteSide(rid.map { String(cString: $0) } ?? "") }
        }
        h.items_changed = { ctx, cid, json, cname in
            let c1 = cid.map { String(cString: $0) } ?? "", j = json.map { String(cString: $0) } ?? "[]"
            let name = cname.map { String(cString: $0) } ?? ""
            ClassroomCenter.post(ctx) { $0.itemsArrived(c1, j, className: name) }
        }
        h.check_circuit = { _, cdl, keyText, keyNames, verdict, summary in
            let r = ClassroomCenter.check(cdl: cdl.map { String(cString: $0) } ?? "",
                                          key: keyText.map { String(cString: $0) } ?? "",
                                          names: keyNames.map { String(cString: $0) } ?? "")
            verdict?.pointee = Int32(r.verdict)
            summary?.pointee = strdup(r.summary)
            return true
        }
        h.lights_of = { _, cdl, lights, count, values in
            guard let cdl, let lights, let values else { return false }
            let names = (0..<Int(count)).map { lights[$0].map { String(cString: $0) } ?? "" }
            guard let v = ClassroomCenter.lights(of: String(cString: cdl), names) else { return false }
            for (i, x) in v.enumerated() { values[i] = Int32(x) }
            return true
        }
        let info = Bundle.main.infoDictionary ?? [:]
        let version = info["CFBundleShortVersionString"] as? String ?? "0"
        let build = info["CFBundleVersion"] as? String ?? "0"
        let appKey = ProcessInfo.processInfo.environment["CL_SYNC_KEY"] ?? info["FeedbackKey"] as? String ?? ""
        engine = cl_classroom_create(&h, dir.path, appKey, "mac/\(version)+\(build)")
        guard let engine else { return }
        cl_classroom_set_device_name(engine, SyncCenter.computerName)
        cl_classroom_start(engine)
        watch()
        refresh()
    }

    func stop() {
        guard let engine else { return }
        for w in watchers { NotificationCenter.default.removeObserver(w) }
        watchers.removeAll()
        cl_classroom_destroy(engine)
        self.engine = nil
        if let p = platform { Self.registry[UInt(bitPattern: Unmanaged.passUnretained(p).toOpaque())] = nil }
        platform = nil
        started = false
    }

    private func watch() {
        let nc = NotificationCenter.default
        watchers.append(nc.addObserver(forName: NSApplication.didBecomeActiveNotification, object: nil, queue: .main) { [weak self] _ in
            MainActor.assumeIsolated { if let e = self?.engine { cl_classroom_app_activated(e) } }
        })
        watchers.append(nc.addObserver(forName: NSApplication.didResignActiveNotification, object: nil, queue: .main) { [weak self] _ in
            MainActor.assumeIsolated { if let e = self?.engine { cl_classroom_app_deactivated(e) } }
        })
    }

    /// Someone did something (at most every 30 s): the core's "two hours without input" clock restarts.
    func userActive() {
        guard let engine, Date().timeIntervalSince(lastUserActive) > 30 else { return }
        lastUserActive = Date()
        cl_classroom_user_active(engine)
    }

    // The UI hooks find their center by the platform object the core hands back as ctx.
    private final class Weak { weak var center: ClassroomCenter?; init(_ c: ClassroomCenter) { center = c } }
    private static var registry: [UInt: Weak] = [:]
    nonisolated private static func post(_ ctx: UnsafeMutableRawPointer?, _ body: @escaping @MainActor @Sendable (ClassroomCenter) -> Void) {
        guard let ctx else { return }
        let key = UInt(bitPattern: ctx)
        let run: @Sendable () -> Void = { MainActor.assumeIsolated { if let c = registry[key]?.center { body(c) } } }
        if Thread.isMainThread { run() } else { DispatchQueue.main.async(execute: run) }
    }

    // MARK: Reading the core

    func refresh() {
        refreshClasses()
        for c in classes {
            refreshAssignments(c.id)
            if c.teaching { refreshStudents(c.id) }
            refreshItems(c.id)
        }
        for k in openSubmissions {
            let p = k.split(separator: "/").map(String.init)
            if p.count == 2 { refreshSubmissions(p[0], p[1]) }
        }
    }

    private static func str(_ p: UnsafePointer<CChar>?) -> String { p.map { String(cString: $0) } ?? "" }
    private static func date(_ ms: Int64) -> Date? { ms > 0 ? Date(timeIntervalSince1970: Double(ms) / 1000) : nil }

    func refreshClasses() {
        guard let e = engine else { return }
        let n = Int(cl_classroom_class_count(e))
        var out: [CRClass] = []
        for i in 0..<n {
            let i = Int32(i)
            out.append(CRClass(id: Self.str(cl_classroom_class_id(e, i)), name: Self.str(cl_classroom_class_name(e, i)),
                               teaching: cl_classroom_class_teaching(e, i), teacherKey: Self.str(cl_classroom_class_teacher_key(e, i)),
                               joinCode: Self.str(cl_classroom_class_join_code(e, i)), joinOpen: cl_classroom_class_join_open(e, i),
                               studentName: Self.str(cl_classroom_class_student_name(e, i)),
                               expiresAt: Self.date(cl_classroom_class_expires_at(e, i)), live: cl_classroom_class_live(e, i),
                               warning: Self.str(cl_classroom_class_warning(e, i)), warnAt: Self.date(cl_classroom_class_warn_at(e, i)),
                               news: Int(cl_classroom_class_news(e, i))))
        }
        for k in out.indices {
            out[k].statusKind = Int(cl_classroom_status_kind(e, out[k].id))
            out[k].statusText = Self.str(cl_classroom_status_text(e, out[k].id))
        }
        if out != classes { classes = out }
        if let s = selected, !out.contains(where: { $0.id == s }) { selected = out.first?.id }
        if selected == nil { selected = out.first?.id }
    }

    func refreshAssignments(_ cid: String) {
        guard let e = engine else { return }
        let n = Int(cl_classroom_assignment_count(e, cid))
        var out: [CRAssignment] = []
        for i in 0..<n {
            let i = Int32(i)
            let due = cl_classroom_assignment_due_at(e, i)
            out.append(CRAssignment(
                id: Self.str(cl_classroom_assignment_id(e, i)), title: Self.str(cl_classroom_assignment_title(e, i)),
                instructions: Self.str(cl_classroom_assignment_instructions(e, i)), cdl: Self.str(cl_classroom_assignment_cdl(e, i)),
                dueAt: due < 0 ? nil : Self.date(due), closeAfterDue: cl_classroom_assignment_close_after_due(e, i),
                ver: cl_classroom_assignment_ver(e, i), keyText: Self.str(cl_classroom_assignment_key_text(e, i)),
                keyNames: Self.str(cl_classroom_assignment_key_names(e, i)), keySealed: cl_classroom_assignment_key_sealed(e, i),
                handedInAt: Self.date(cl_classroom_assignment_handed_in_at(e, i)), attempts: Int(cl_classroom_assignment_attempts(e, i)),
                changedSince: cl_classroom_assignment_changed_since(e, i), pending: cl_classroom_assignment_pending(e, i),
                closed: cl_classroom_assignment_closed(e, i), unreadable: cl_classroom_assignment_unreadable(e, i),
                problem: Self.str(cl_classroom_assignment_problem(e, i))))
        }
        if assignments[cid] != out { assignments[cid] = out }
    }

    func refreshStudents(_ cid: String) {
        guard let e = engine else { return }
        let n = Int(cl_classroom_student_count(e, cid))
        let out = (0..<n).map { i -> CRStudent in
            let i = Int32(i)
            return CRStudent(id: Self.str(cl_classroom_student_id(e, i)), name: Self.str(cl_classroom_student_name(e, i)),
                             joinedAt: Self.date(cl_classroom_student_joined_at(e, i)), seenAt: Self.date(cl_classroom_student_seen_at(e, i)),
                             unreadable: cl_classroom_student_unreadable(e, i))
        }
        if students[cid] != out { students[cid] = out }
    }

    func refreshSubmissions(_ cid: String, _ aid: String) {
        guard let e = engine else { return }
        let n = Int(cl_classroom_submission_count(e, cid, aid))
        let out = (0..<n).map { i -> CRSubmission in
            let i = Int32(i)
            return CRSubmission(studentId: Self.str(cl_classroom_submission_student_id(e, i)), name: Self.str(cl_classroom_submission_name(e, i)),
                                cdl: Self.str(cl_classroom_submission_cdl(e, i)),
                                handedInAt: Self.date(cl_classroom_submission_handed_in_at(e, i)),
                                attempts: Int(cl_classroom_submission_attempts(e, i)), verdict: Int(cl_classroom_submission_check_verdict(e, i)),
                                summary: Self.str(cl_classroom_submission_check_summary(e, i)),
                                unreadable: cl_classroom_submission_unreadable(e, i), problem: Self.str(cl_classroom_submission_problem(e, i)),
                                left: cl_classroom_submission_left(e, i))
        }
        if submissions["\(cid)/\(aid)"] != out { submissions["\(cid)/\(aid)"] = out }
    }

    func refreshItems(_ cid: String) {
        guard let e = engine else { return }
        let n = Int(cl_classroom_item_count(e, cid))
        let out = (0..<n).map { i -> CRItem in
            let i = Int32(i)
            return CRItem(id: Self.str(cl_classroom_item_id(e, i)), type: Self.str(cl_classroom_item_type(e, i)),
                          title: Self.str(cl_classroom_item_title(e, i)), topic: Self.str(cl_classroom_item_topic(e, i)),
                          note: Self.str(cl_classroom_item_note(e, i)), cdl: Self.str(cl_classroom_item_cdl(e, i)),
                          ver: cl_classroom_item_ver(e, i), createdAt: Self.date(cl_classroom_item_created_at(e, i)),
                          releasedAt: Self.date(cl_classroom_item_released_at(e, i)), hidden: cl_classroom_item_hidden(e, i),
                          news: Self.str(cl_classroom_item_news(e, i)), unreadable: cl_classroom_item_unreadable(e, i),
                          problem: Self.str(cl_classroom_item_problem(e, i)))
        }
        if items[cid] != out { items[cid] = out }
    }

    /// A hand-in's earlier attempts (v2), as the core last read them.
    func refreshHistory(_ cid: String, _ aid: String, _ sid: String) {
        guard let e = engine else { return }
        let n = Int(cl_classroom_history_count(e, cid, aid, sid))
        let out = (0..<n).map { i -> CRSubmission in
            let i = Int32(i)
            return CRSubmission(studentId: sid, name: Self.str(cl_classroom_submission_name(e, i)), cdl: Self.str(cl_classroom_submission_cdl(e, i)),
                                handedInAt: Self.date(cl_classroom_submission_handed_in_at(e, i)),
                                attempts: Int(cl_classroom_submission_attempts(e, i)), verdict: Int(cl_classroom_submission_check_verdict(e, i)),
                                summary: Self.str(cl_classroom_submission_check_summary(e, i)),
                                unreadable: cl_classroom_submission_unreadable(e, i), problem: Self.str(cl_classroom_submission_problem(e, i)),
                                left: cl_classroom_submission_left(e, i), ver: Int64(i) + 1)
        }
        history["\(cid)/\(aid)/\(sid)"] = out
    }

    /// Items news from the core (a student's class): the window's banner and a note on the front circuit.
    func itemsArrived(_ cid: String, _ json: String, className: String) {
        refreshItems(cid)
        refreshClasses()
        guard let list = try? JSONSerialization.jsonObject(with: Data(json.utf8)) as? [[String: Any]] else { return }
        for o in list {
            let n = CRNews(classId: cid, itemId: o["id"] as? String ?? "", what: o["what"] as? String ?? "new",
                           type: o["type"] as? String ?? "example", title: o["title"] as? String ?? "")
            news.removeAll { $0.id == n.id }
            news.append(n)
        }
        if let last = news.last, !preview {
            CanvasController.front?.note("\(last.text) in \(className). Open it from File › Classroom.")
        }
    }

    /// Sync applied side records, or was turned on (3.16.7).
    func syncSideChanged() {
        guard let engine else { return }
        cl_classroom_sync_side_changed(engine)
    }

    // MARK: Done callbacks

    typealias Done = (_ ok: Bool, _ message: String, _ result: String) -> Void
    private final class DoneBox { let fn: Done; init(_ fn: @escaping Done) { self.fn = fn } }
    private static let doneFn: CLClassroomDone = { ctx, ok, message, result in
        guard let ctx else { return }
        let box = Unmanaged<DoneBox>.fromOpaque(ctx).takeRetainedValue()
        let m = message.map { String(cString: $0) } ?? "", r = result.map { String(cString: $0) } ?? ""
        if Thread.isMainThread { box.fn(ok, m, r) } else { DispatchQueue.main.async { box.fn(ok, m, r) } }
    }

    /// Runs one of the core's asynchronous calls; `done` gets (ok, sentence, result) on the main thread.
    private func call(_ done: @escaping Done, _ body: (OpaquePointer, CLClassroomDone, UnsafeMutableRawPointer) -> Void) {
        guard let engine else { done(false, preview ? "" : "Classroom isn't running.", ""); return }
        userActive()
        let box = Unmanaged.passRetained(DoneBox { [weak self] ok, m, r in
            MainActor.assumeIsolated { self?.refresh() }
            done(ok, m, r)
        }).toOpaque()
        body(engine, Self.doneFn, box)
    }

    private static func withCStrings<R>(_ strings: [String], _ body: ([UnsafePointer<CChar>?]) -> R) -> R {
        let dup = strings.map { strdup($0) }
        defer { dup.forEach { free($0) } }
        return body(dup.map { UnsafePointer($0) })
    }

    // MARK: Teacher

    func createClass(_ name: String, done: @escaping Done) { call(done) { cl_classroom_create_class($0, name, $1, $2) } }
    /// result: the class name.
    func previewTeacherKey(_ text: String, done: @escaping Done) { call(done) { cl_classroom_preview_teacher_key($0, text, $1, $2) } }
    func addTeacherKey(_ text: String, done: @escaping Done) { call(done) { cl_classroom_add_teacher_key($0, text, $1, $2) } }
    func renameClass(_ cid: String, _ name: String, done: @escaping Done) { call(done) { cl_classroom_rename_class($0, cid, name, $1, $2) } }
    func setJoinOpen(_ cid: String, _ open: Bool, done: @escaping Done) { call(done) { cl_classroom_set_join_open($0, cid, open, $1, $2) } }
    func newJoinCode(_ cid: String, done: @escaping Done) { call(done) { cl_classroom_new_join_code($0, cid, $1, $2) } }
    func deleteClass(_ cid: String, done: @escaping Done) { call(done) { cl_classroom_delete_class($0, cid, $1, $2) } }
    func forgetClass(_ cid: String) {
        guard let engine else { return }
        cl_classroom_forget_class(engine, cid)
        refresh()
    }

    func postAssignment(_ cid: String, editing aid: String?, title: String, instructions: String, due: Date?, closeAfterDue: Bool,
                        cdl: String, keyText: String, keyNames: String, studentsCanCheck: Bool, done: @escaping Done) {
        let dueMs = due.map { Int64($0.timeIntervalSince1970 * 1000) } ?? -1
        call(done) {
            cl_classroom_post_assignment($0, cid, aid, title, instructions, dueMs, closeAfterDue, cdl, keyText, keyNames, studentsCanCheck, $1, $2)
        }
    }
    func deleteAssignment(_ cid: String, _ aid: String, done: @escaping Done) { call(done) { cl_classroom_delete_assignment($0, cid, aid, $1, $2) } }

    func fetchStudents(_ cid: String, done: @escaping Done = { _, _, _ in }) { call(done) { cl_classroom_refresh_students($0, cid, $1, $2) } }
    func removeStudents(_ cid: String, _ sids: [String], deleteHandIns: Bool, done: @escaping Done) {
        call(done) { cl_classroom_remove_students($0, cid, sids.joined(separator: "\n"), deleteHandIns, $1, $2) }
    }

    /// The hand-ins view is open (it refreshes as they come) or not.
    func submissionsOpen(_ cid: String, _ aid: String, _ open: Bool) {
        let k = "\(cid)/\(aid)"
        if open { openSubmissions.insert(k) } else { openSubmissions.remove(k) }
    }
    func fetchSubmissions(_ cid: String, _ aid: String, done: @escaping Done = { _, _, _ in }) {
        call({ [weak self] ok, m, r in
            MainActor.assumeIsolated {
                self?.refreshSubmissions(cid, aid)
                if ok { self?.submissionsUpdated["\(cid)/\(aid)"] = Date() }
            }
            done(ok, m, r)
        }) { cl_classroom_refresh_submissions($0, cid, aid, $1, $2) }
    }

    // v2 (3.16.2): shared circuits and class examples. result: the item's id.
    func postItem(_ cid: String, id: String?, type: String, title: String, topic: String, note: String, cdl: String, hidden: Bool,
                  done: @escaping Done) {
        call(done) { cl_classroom_post_item($0, cid, id, type, title, topic, note, cdl, hidden, $1, $2) }
    }
    func setItemHidden(_ cid: String, _ iid: String, _ hidden: Bool, done: @escaping Done) {
        call(done) { cl_classroom_set_item_hidden($0, cid, iid, hidden, $1, $2) }
    }
    func deleteItem(_ cid: String, _ iid: String, done: @escaping Done) { call(done) { cl_classroom_delete_item($0, cid, iid, $1, $2) } }
    /// v2 (3.16.3): a student's earlier hand-ins.
    func loadHistory(_ cid: String, _ aid: String, _ sid: String, done: @escaping Done) {
        call({ [weak self] ok, m, r in
            MainActor.assumeIsolated { self?.refreshHistory(cid, aid, sid) }
            done(ok, m, r)
        }) { cl_classroom_load_history($0, cid, aid, sid, $1, $2) }
    }

    // MARK: Student

    /// result: "<class name>\n<1|0 open>".
    func previewJoinCode(_ text: String, done: @escaping Done) { call(done) { cl_classroom_preview_join_code($0, text, $1, $2) } }
    func join(_ text: String, name: String, done: @escaping Done) { call(done) { cl_classroom_join($0, text, name, $1, $2) } }
    func renameMe(_ cid: String, _ name: String, done: @escaping Done) { call(done) { cl_classroom_rename($0, cid, name, $1, $2) } }
    func handIn(_ cid: String, _ aid: String, cdl: String, done: @escaping Done) { call(done) { cl_classroom_hand_in($0, cid, aid, cdl, $1, $2) } }
    /// Open = the student's own copy (3.16.2): the same version again opens that copy; a newer one makes
    /// a new copy, "Title (updated)". A copy is never written over.
    func openItem(_ cid: String, _ item: CRItem) throws {
        let copy = try ItemCopies.make(classId: cid, item: item)
        Library.open(copy.circuit)
        news.removeAll { $0.classId == cid && $0.itemId == item.id }
        if let engine { cl_classroom_item_opened(engine, cid, item.id) }
        refreshItems(cid)
        refreshClasses()
    }
    func makeMoveCode(_ cid: String, done: @escaping Done) { call(done) { cl_classroom_make_move_code($0, cid, $1, $2) } }
    /// result: "<class name>\n<student name>".
    func previewMoveCode(_ text: String, done: @escaping Done) { call(done) { cl_classroom_preview_move_code($0, text, $1, $2) } }
    func importMoveCode(_ text: String, done: @escaping Done) { call(done) { cl_classroom_import_move_code($0, text, $1, $2) } }
    func leaveClass(_ cid: String, done: @escaping Done) {
        call({ ok, m, r in
            if ok { MainActor.assumeIsolated { AssignmentCopies.unlink(classId: cid) } }
            done(ok, m, r)
        }) { cl_classroom_leave_class($0, cid, $1, $2) }
    }
    func forgetMembership(_ cid: String) {
        guard let engine else { return }
        cl_classroom_forget_membership(engine, cid)
        refresh()
    }

    /// The class page is showing (polls at once and keeps the live connection) or not.
    func pageOpen(_ cid: String, _ open: Bool) {
        guard let engine else { return }
        cl_classroom_page_open(engine, cid, open)
    }

    // MARK: Codes (no engine needed)

    /// The canonical code, or the §1.2 sentence for what's wrong (nil, nil while empty). kind 0 teacher, 1 join, 2 move.
    static func parse(_ text: String, kind: Int) -> (code: String?, why: String?) {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return (nil, nil) }
        let platform = SyncPlatform(syncDir: SyncPlatform.defaultSyncDir)
        var hooks = platform.hooks()
        var code = [CChar](repeating: 0, count: 29), why = [CChar](repeating: 0, count: 16)
        if cl_classroom_parse_code(&hooks, Int32(kind), trimmed, &code, &why) { return (String(cString: code), nil) }
        return (nil, String(cString: cl_classroom_why_text(Int32(kind), why, trimmed)))
    }
    static func grouped(_ code: String) -> String {
        guard !code.isEmpty else { return "" }
        var out = [CChar](repeating: 0, count: 35)
        cl_classroom_group_code(code, &out)
        return String(cString: out)
    }
    static func webLink(kind: Int, _ code: String) -> String { String(cString: cl_classroom_web_link(Int32(kind), code)) }

    // MARK: Check My Circuit and the lights (on the engine thread)

    /// The Check sheet's mapping lines ("name\tswitch or light") as a dictionary.
    nonisolated static func names(_ text: String) -> [String: String] {
        var out: [String: String] = [:]
        for line in text.split(whereSeparator: \.isNewline) {
            let p = line.split(separator: "\t", maxSplits: 1).map(String.init)
            if p.count == 2 { out[p[0]] = p[1] }
        }
        return out
    }

    /// A circuit against a key, as the Check sheet would: (0 matches, 1 wrong rows, 2 couldn't check, the summary).
    nonisolated static func check(cdl: String, key: String, names: String) -> (verdict: Int, summary: String) {
        guard let doc = try? CoreDocument(data: Data(cdl.utf8)) else { return (2, "the circuit couldn't be read") }
        var problem = ""
        let table = TruthTable(document: doc, page: 0, error: &problem)
        let c = CircuitCheck(table: table?.checkTable, noTable: problem, document: doc.handle, page: 0, text: key, byHand: Self.names(names))
        switch c.verdict {
        case .matches: return (0, c.summary.isEmpty ? "Matches" : c.summary)
        case .wrong: return (1, c.summary)
        case .cannotCheck: return (2, c.summary.isEmpty ? c.error : c.summary)
        }
    }

    /// The named lights (Predict's names) of a circuit once it settles: 0 or 1 each (-1 unclear).
    nonisolated static func lights(of cdl: String, _ names: [String]) -> [Int]? {
        guard let doc = try? CoreDocument(data: Data(cdl.utf8)) else { return nil }
        for _ in 0..<40 { cl_document_step(doc.handle) }
        let found = namedLights(doc, page: 0)
        return names.map { n in found.first { lightKey($0.name) == lightKey(n) }?.value ?? -1 }
    }

    /// Two names for the same light (the teacher's board, a student's copy): spaces and capitals don't count.
    nonisolated static func lightKey(_ s: String) -> String {
        s.precomposedStringWithCanonicalMapping.split(whereSeparator: { $0.isWhitespace }).joined(separator: " ").lowercased()
    }

    /// A page's lights (not displays) with Predict's names, in Tab order.
    nonisolated static func namedLights(_ doc: CoreDocument, page: Int) -> [(name: String, gate: Int, value: Int)] {
        var buf = [CLSimLight](repeating: CLSimLight(), count: 256)
        let n = min(Int(cl_simview_lights(doc.handle, Int32(page), &buf, Int32(buf.count))), buf.count)
        return buf.prefix(n).filter { $0.digits == 0 }.map { l in
            var name = [CChar](repeating: 0, count: 128)
            _ = cl_simview_light_name(doc.handle, Int32(page), l.gate, &name, Int32(name.count))
            return (String(cString: name), Int(l.gate), Int(l.value))
        }
    }

}

/// Which sheet the Classroom window shows.
enum ClassroomSheet: Identifiable, Equatable {
    case create
    case teacherKey(classId: String)
    case addTeacherKey(prefill: String)
    case join(prefill: String)
    case moveIn(prefill: String)
    case moveCode(classId: String, code: String)
    case post(classId: String, editing: String?)
    case handIns(classId: String, aid: String)
    case addItem(classId: String, share: Bool, files: [URL])
    var id: String {
        switch self {
        case .create: "create"
        case .teacherKey(let c): "key-\(c)"
        case .addTeacherKey: "addkey"
        case .join: "join"
        case .moveIn: "movein"
        case .moveCode(let c, _): "move-\(c)"
        case .post(let c, let e): "post-\(c)-\(e ?? "")"
        case .handIns(let c, let a): "handins-\(c)-\(a)"
        case .addItem(let c, let share, let f): "item-\(c)-\(share)-\(f.count)"
        }
    }
}

// MARK: - Students' copies in Your Circuits

/// A student's copy of an assignment: a circuit in Your Circuits whose folder holds
/// assignment.json ({classId, assignmentId}). Opening the assignment again opens it.
@MainActor
enum AssignmentCopies {
    struct Link: Codable { var classId: String; var assignmentId: String }
    static let file = "assignment.json"

    static func link(of item: LibraryItem) -> Link? {
        guard let data = FileManager.default.contents(atPath: item.folder.appendingPathComponent(file).path) else { return nil }
        return try? JSONDecoder().decode(Link.self, from: data)
    }

    static func copy(classId: String, aid: String) -> LibraryItem? {
        Library.items().first { link(of: $0).map { $0.classId == classId && $0.assignmentId == aid } ?? false }
    }

    /// The copy, made first if there isn't one yet.
    static func make(classId: String, assignment a: CRAssignment) throws -> LibraryItem {
        if let have = copy(classId: classId, aid: a.id) { return have }
        let item = try Library.create(named: a.title.isEmpty ? "Assignment" : a.title, text: a.cdl)
        let data = try JSONEncoder().encode(Link(classId: classId, assignmentId: a.id))
        try data.write(to: item.folder.appendingPathComponent(file))
        return item
    }

    /// Leaving a class: the circuits stay, their link to it goes.
    static func unlink(classId: String) {
        for item in Library.items() where link(of: item)?.classId == classId {
            try? FileManager.default.removeItem(at: item.folder.appendingPathComponent(file))
        }
    }

    /// The circuit as it is now: from its open window if there is one, else its file.
    static func text(of item: LibraryItem) -> String? {
        for doc in NSDocumentController.shared.documents where doc.fileURL?.standardizedFileURL == item.circuit.standardizedFileURL {
            for v in doc.windowControllers.compactMap({ $0.window?.contentView }) {
                if let core = canvases(v).first?.document { return core.saveText() }
            }
        }
        return try? String(contentsOf: item.circuit, encoding: .utf8)
    }

    /// The assignment the front circuit window is a copy of, if any.
    static func front() -> (item: LibraryItem, link: Link)? {
        guard let w = CanvasController.front?.view?.window, let url = NSDocumentController.shared.document(for: w)?.fileURL,
              let item = Library.item(for: url), let l = link(of: item) else { return nil }
        return (item, l)
    }

    static func canvases(_ v: NSView) -> [CircuitCanvasNSView] {
        (v as? CircuitCanvasNSView).map { [$0] } ?? v.subviews.flatMap(canvases)
    }

    /// Opens the copy, then the Check sheet with the teacher's key filled in.
    static func openAndCheck(_ item: LibraryItem, key: String, names: String) {
        CheckMemory.save(item.circuit.path + "#0", CheckMemory.Saved(kind: 0, text: key, names: ClassroomCenter.names(names)))
        Library.open(item.circuit)
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.8) {
            MainActor.assumeIsolated {
                guard let c = CanvasController.front, c.view?.window?.representedURL?.standardizedFileURL == item.circuit.standardizedFileURL
                else { return }
                c.perform(.checkCircuit)
            }
        }
    }
}

// MARK: - Scratch circuits: hand-ins and the live view

@MainActor
enum ScratchCircuit {
    /// A circuit opened from text as a scratch copy (the temporary folder: never in Your Circuits).
    static func open(_ text: String, named name: String) {
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent("CedarLogic Classroom/\(UUID().uuidString)", isDirectory: true)
        do {
            try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
            let file = dir.appendingPathComponent(safeName(name) + ".cdl")
            try text.write(to: file, atomically: true, encoding: .utf8)
            Library.open(file)
        } catch {
            NSAlert(error: error).runModal()
        }
    }

    /// File-system-safe: / \ : * ? " < > | become -.
    nonisolated static func safeName(_ s: String) -> String {
        var out = s
        for ch in ["/", "\\", ":", "*", "?", "\"", "<", ">", "|"] { out = out.replacingOccurrences(of: ch, with: "-") }
        out = out.trimmingCharacters(in: .whitespacesAndNewlines)
        return out.isEmpty ? "Untitled" : String(out.prefix(120))
    }

    /// Keep a Copy: into Your Circuits, and open it.
    @discardableResult
    static func keep(_ text: String, named name: String) -> LibraryItem? {
        guard let item = try? Library.create(named: name, text: text) else { return nil }
        Library.open(item.circuit)
        return item
    }
}

// MARK: - Download All

enum HandInExport {
    /// `<student name>.cdl` for each (duplicates "(2)") and hand-ins.csv, into `folder`.
    static func write(assignment: CRAssignment, submissions: [CRSubmission], to folder: URL) throws {
        let fm = FileManager.default
        try fm.createDirectory(at: folder, withIntermediateDirectories: true)
        var used: Set<String> = []
        func csv(_ s: String) -> String { "\"" + s.replacingOccurrences(of: "\"", with: "\"\"") + "\"" }
        var lines = ["name,handed in,attempts,check result"]
        let iso = ISO8601DateFormatter()
        for s in submissions {
            lines.append([csv(s.name + (s.left ? " (left the class)" : "")), csv(s.handedInAt.map { iso.string(from: $0) } ?? ""),
                          String(s.attempts), csv(ClassroomText.checkLine(s, hasKey: assignment.hasKey))].joined(separator: ","))
            guard !s.unreadable, !s.cdl.isEmpty else { continue }
            let base = ScratchCircuit.safeName(s.name)
            var name = base, n = 2
            while used.contains(name.lowercased()) { name = "\(base) (\(n))"; n += 1 }
            used.insert(name.lowercased())
            try s.cdl.write(to: folder.appendingPathComponent(name + ".cdl"), atomically: true, encoding: .utf8)
        }
        try (lines.joined(separator: "\n") + "\n").write(to: folder.appendingPathComponent("hand-ins.csv"), atomically: true, encoding: .utf8)
    }

    static func folderName(_ className: String, _ title: String) -> String { ScratchCircuit.safeName("\(className) – \(title)") }

    /// The folder zipped with ditto; true if it worked.
    static func zip(_ folder: URL, to zipURL: URL) -> Bool {
        try? FileManager.default.removeItem(at: zipURL)
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/usr/bin/ditto")
        p.arguments = ["-c", "-k", "--keepParent", folder.path, zipURL.path]
        do { try p.run() } catch { return false }
        p.waitUntilExit()
        return p.terminationStatus == 0
    }
}

extension ClassroomCenter {
    /// File › Classroom › Hand In…: the front circuit, if it's a copy of an assignment.
    func handInFront() {
        start()
        guard let (item, link) = AssignmentCopies.front() else {
            classroomTell("This circuit isn't from an assignment", "Open an assignment from the Classroom window, build it, then hand it in.")
            return
        }
        guard let a = assignments[link.classId]?.first(where: { $0.id == link.assignmentId }), let text = AssignmentCopies.text(of: item) else {
            classroomTell("That assignment isn't here any more", "It may have been deleted, or you left the class.")
            return
        }
        guard !a.closed else {
            classroomTell("Hand-ins are closed", "The due date for “\(a.title)” has passed.")
            return
        }
        handIn(link.classId, a.id, cdl: text) { ok, m, _ in   // one tap (3.16.3)
            CanvasController.front?.note(ok ? "Handed in ✓" : m)
        }
    }
}

// MARK: - v2: students' copies of shared circuits and examples (3.16.2)

/// A student's own copy of a shared circuit or class example: a circuit in Your Circuits whose
/// folder holds item.json ({classId, item, ver, title}).
@MainActor
enum ItemCopies {
    struct Link: Codable { var classId: String; var item: String; var ver: Int64; var title: String }
    static let file = "item.json"

    static func link(of item: LibraryItem) -> Link? {
        guard let data = FileManager.default.contents(atPath: item.folder.appendingPathComponent(file).path) else { return nil }
        return try? JSONDecoder().decode(Link.self, from: data)
    }

    /// The copy of that version, made first if there isn't one ("Title (updated)" when an older version has one).
    static func make(classId: String, item it: CRItem) throws -> LibraryItem {
        let mine = Library.items().compactMap { i in link(of: i).map { (i, $0) } }.filter { $0.1.classId == classId && $0.1.item == it.id }
        if let same = mine.first(where: { $0.1.ver == it.ver }) { return same.0 }
        let title = it.title.isEmpty ? "Untitled" : it.title
        let copy = try Library.create(named: mine.isEmpty ? title : "\(title) (updated)", text: it.cdl)
        let data = try JSONEncoder().encode(Link(classId: classId, item: it.id, ver: it.ver, title: title))
        try data.write(to: copy.folder.appendingPathComponent(file))
        return copy
    }
}

// MARK: - v2: classes on every device (3.16.7)

/// The classroom core's side-record hooks over the app's sync engine (main thread).
@MainActor
enum ClassroomSide {
    private static var records: [(rid: UnsafeMutablePointer<CChar>, json: UnsafeMutablePointer<CChar>)] = []

    nonisolated static var on: Bool { onMain { SyncCenter.shared.sideOn } }

    /// Reads the records again (the strings stay valid until the next load).
    nonisolated static func load() -> Int32 {
        onMain {
            for r in records { free(r.rid); free(r.json) }
            records = SyncCenter.shared.sideRecords().map { (strdup($0.0)!, strdup($0.1)!) }
            return Int32(records.count)
        }
    }

    nonisolated static func record(_ i: Int, _ rid: UnsafeMutablePointer<UnsafePointer<CChar>?>?) -> UnsafePointer<CChar>? {
        onMain {
            guard i >= 0, i < records.count else { rid?.pointee = nil; return nil }
            rid?.pointee = UnsafePointer(records[i].rid)
            return UnsafePointer(records[i].json)
        }
    }

    nonisolated private static func onMain<T>(_ fn: @MainActor () -> T) -> T {
        if Thread.isMainThread { return MainActor.assumeIsolated { fn() } }
        return DispatchQueue.main.sync { MainActor.assumeIsolated { fn() } }
    }
}

// MARK: - v2: an answer key from a solution circuit (3.16.6)

enum AnswerKey {
    struct Made { var text: String; var timing: Bool }
    struct Failure: Error { var message: String }

    /// A name as a key column: spaces become _.
    static func keyName(_ s: String) -> String {
        let t = s.split(whereSeparator: { $0.isWhitespace }).joined(separator: "_")
        return t.isEmpty ? "?" : t
    }
    private static func bit(_ c: Character) -> String { c == "0" || c == "1" ? String(c) : "-" }

    /// The key text Check My Circuit reads, made from what the solution does: a truth table of every switch
    /// combination, or (with flip-flops and a clock) a timing table clock pulse by clock pulse. Run in a
    /// simulator of its own; the same checker that later checks a student runs the solution.
    static func make(cdl: String) throws -> Made {
        guard let doc = try? CoreDocument(data: Data(cdl.utf8)) else { throw Failure(message: "The solution couldn't be read.") }
        var problem = ""
        guard let t = TruthTable(document: doc, page: 0, error: &problem) else {
            let p = problem.replacingOccurrences(of: "A truth table needs", with: "An answer key needs")
            throw Failure(message: p.isEmpty ? "An answer key needs at least one switch and one LED." : p)
        }
        let ins = Array(t.names.prefix(t.inputs)), outs = Array(t.names.dropFirst(t.inputs))
        guard !ins.isEmpty, !outs.isEmpty else { throw Failure(message: "An answer key needs at least one switch and one LED.") }
        func table() -> Made {
            let head = ins.map(keyName).joined(separator: " ") + " | " + outs.map(keyName).joined(separator: " ")
            let rows = t.rows.map { r in
                r.prefix(t.inputs).map { String($0) }.joined(separator: " ") + " | " + r.dropFirst(t.inputs).map(bit).joined(separator: " ")
            }
            return Made(text: ([head] + rows).joined(separator: "\n") + "\n", timing: false)
        }
        if !t.sequential { return table() }
        // Sequential: a timing table. The clock is a clock part, or a switch named CLK or Clock.
        let isClock = { (n: String) in ["clk", "clock"].contains(String(n.lowercased().filter { $0.isLetter || $0.isNumber })) }
        let sw = ins.filter { !isClock($0) }, m = sw.count
        let rows = min(64, max(8, 2 << min(m, 5)))
        let head = m > 0 ? "Pulse | \(sw.map(keyName).joined(separator: " ")) | \(outs.map(keyName).joined(separator: " "))"
                         : "Pulse | \(outs.map(keyName).joined(separator: " "))"
        func bits(_ k: Int) -> String { (0..<m).map { (k >> (m - 1 - $0)) & 1 == 1 ? "1" : "0" }.joined(separator: " ") }
        func line(_ k: Int, _ o: [String]) -> String { "\(k) | " + (m > 0 ? bits(k % (1 << m)) + " | " : "") + o.joined(separator: " ") }
        let probe = ([head] + (0...rows).map { line($0, outs.map { _ in "-" }) }).joined(separator: "\n") + "\n"
        guard let c = cl_check_clocked(doc.handle, 0, probe, "") else { throw Failure(message: "The solution couldn't be run.") }
        defer { cl_check_free(c) }
        let error = String(cString: cl_check_error(c))
        if error == "no_clock" { return table() }   // a latch of gates: a truth table
        if !error.isEmpty { throw Failure(message: String(cString: cl_check_summary(c))) }
        var got: [String] = []
        for i in 0..<Int(cl_check_step_count(c)) {
            let kind = cl_check_step_kind(c, Int32(i))
            guard kind == Int32(CL_STEP_START) || kind == Int32(CL_STEP_PULSE) else { continue }
            got.append(String(cString: cl_check_step_text(c, Int32(i), Int32(CL_STEP_GOT))))
        }
        guard got.count == rows + 1 else { throw Failure(message: "The solution couldn't be run clock pulse by clock pulse.") }
        if let bad = got.firstIndex(where: { $0.contains("~") }) {
            throw Failure(message: "The solution circuit doesn't settle " + (bad == 0 ? "at the start." : "at clock pulse \(bad)."))
        }
        let lines = got.enumerated().map { k, g in line(k, g.map(bit)) }
        return Made(text: ([head] + lines).joined(separator: "\n") + "\n", timing: true)
    }
}

// MARK: - v2: the bar over an assignment's own copy (3.16.3)

/// Over a circuit that is a student's copy of an assignment: the title, Instructions, Check (when the
/// students can check), and Hand In -- one tap; "Handed in ✓ <time>" after, then Hand In Again.
struct AssignmentBar: View {
    @ObservedObject var canvas: CanvasController
    @ObservedObject private var center = ClassroomCenter.shared
    @State private var item: LibraryItem?
    @State private var link: AssignmentCopies.Link?
    @State private var showInstructions = false
    @State private var note = ""
    @State private var sending = false

    var body: some View {
        Group {
            if let link, let a = center.assignments[link.classId]?.first(where: { $0.id == link.assignmentId }) {
                HStack(spacing: 10) {
                    Image(systemName: "graduationcap.fill").foregroundStyle(.secondary)
                    Text(a.title).fontWeight(.semibold).lineLimit(1)
                    if !a.instructions.isEmpty {
                        Button("Instructions") { showInstructions.toggle() }
                            .popover(isPresented: $showInstructions) {
                                ScrollView { Text(a.instructions).textSelection(.enabled).padding(14) }.frame(width: 340).frame(maxHeight: 320)
                            }
                    }
                    if !a.keyText.isEmpty { Button("Check") { check(a) } }
                    Text(note.isEmpty ? (a.closed && a.handedInAt == nil ? "Hand-ins closed" : ClassroomText.handInLine(a)) : note)
                        .foregroundStyle(a.handedInAt != nil && note.isEmpty ? Color.green : Color.secondary).lineLimit(1)
                    Button(a.handedInAt == nil ? "Hand In" : "Hand In Again") { handIn(a) }.disabled(a.closed || sending)
                }
                .font(.callout)
                .padding(.horizontal, 14).padding(.vertical, 6)
                .background(.regularMaterial, in: Capsule())
                .shadow(radius: 4, y: 2)
                .padding(.top, 10)
            }
        }
        .onAppear { DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { MainActor.assumeIsolated { resolve() } } }
        .onReceive(NotificationCenter.default.publisher(for: NSWindow.didBecomeKeyNotification)) { _ in resolve() }
    }

    /// Which assignment this window's circuit is a copy of (none for most circuits).
    private func resolve() {
        guard ClassroomFlag.on, let w = canvas.view?.window, let url = NSDocumentController.shared.document(for: w)?.fileURL,
              let it = Library.item(for: url), let l = AssignmentCopies.link(of: it) else { link = nil; return }
        item = it
        if link?.assignmentId != l.assignmentId { link = l }
        center.start()
    }

    private func check(_ a: CRAssignment) {
        guard let item else { return }
        CheckMemory.save(item.circuit.path + "#0", CheckMemory.Saved(kind: 0, text: a.keyText, names: ClassroomCenter.names(a.keyNames)))
        canvas.perform(.checkCircuit)
    }

    private func handIn(_ a: CRAssignment) {
        guard let item, let link, let text = AssignmentCopies.text(of: item) else { return }
        sending = true
        note = "Handing in…"
        center.handIn(link.classId, a.id, cdl: text) { ok, m, _ in
            sending = false
            note = ok ? "" : m
        }
    }
}
