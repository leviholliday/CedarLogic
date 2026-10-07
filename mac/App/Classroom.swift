// Classroom on the Mac (docs/CLASSROOM.md in the website's repository, §4-§5):
// the shared core (mac/CedarCore/Classroom*.cpp through CedarClassroom.h) with
// the hooks of ClassroomHooks.swift, plus the app's own part -- the UI hooks,
// Check My Circuit on hand-ins (check_circuit), a pushed circuit's lights
// (lights_of), the students' copies in Your Circuits, handing in from them,
// hand-ins opened as scratch copies, Download All, and the live view's window.
// ClassroomView.swift draws it; RenderClassroom.swift pictures and tests it.
//
// Behind a flag until the classroom service is deployed (ClassroomFlag): off,
// nothing here starts and no menu shows.

import AppKit
import SwiftUI
import UniformTypeIdentifiers

/// Whether Classroom shows at all: `defaults write <the app's bundle id> ClassroomEnabled -bool YES`
/// or CL_CLASSROOM=1 in the environment (which wins either way). Off by default.
enum ClassroomFlag {
    static let defaultsKey = "ClassroomEnabled"
    static var on: Bool {
        if let e = ProcessInfo.processInfo.environment["CL_CLASSROOM"] { return e == "1" || e.lowercased() == "yes" }
        return UserDefaults.standard.bool(forKey: defaultsKey)
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
    var id: String { studentId }
}

struct CRLive: Hashable {
    var on = false
    var ended = false
    var reveal = false
    var takeOver = false
    var session = ""
    var cdl = ""
    var ver: Int64 = 0
    var step = 0
    var hasPredict = false
    var prompt = ""
    var lights: [String] = []
    var myRight = -1
    var myTotal = 0
    /// "", "connecting", "open", "fallback", "closed".
    var connection = ""
}

struct CRAnswers: Hashable {
    struct Light: Hashable { var name: String; var ones: Int; var zeros: Int }
    var answered = 0
    var students = 0
    var right = 0
    var wrong = 0
    var lights: [Light] = []
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
    /// "Not handed in" / "Handed in 4 Oct 10:31" / "Changed since you handed in" / pending.
    static func handInLine(_ a: CRAssignment) -> String {
        if a.pending { return "Will hand in when you're online." }
        guard let at = a.handedInAt else { return "Not handed in" }
        if a.changedSince { return "Changed since you handed in" }
        return "Handed in \(dayTime(at))"
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
    static let goLiveAsk = "Go live with the circuit on screen? Every student who opens the class sees it; push again whenever you change it."
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
    @Published var live: [String: CRLive] = [:]
    @Published var answers: [String: CRAnswers] = [:]
    /// The latest notice from the core ("You were removed from …"), shown as the window's banner.
    @Published var notice: String?
    /// The class the window shows.
    @Published var selected: String?
    /// The window's sheets (ClassroomView).
    @Published var sheet: ClassroomSheet?
    /// Classes whose live view this student joined (follows the teacher's pushes).
    @Published var following: Set<String> = []

    let preview: Bool
    let dir: URL
    private(set) var engine: OpaquePointer?
    private var platform: ClassroomPlatform?
    private var started = false
    private var watchers: [Any] = []
    private var lastUserActive = Date.distantPast
    /// Which hand-ins views are open, so a change refreshes them.
    private var openSubmissions: Set<String> = []
    /// The live circuit each class's live window last showed (by ver).
    private var liveShown: [String: Int64] = [:]

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
        h.live_changed = { ctx, cid in
            let id = cid.map { String(cString: $0) } ?? ""
            ClassroomCenter.post(ctx) { $0.refreshLive(id) }
        }
        h.answers_changed = { ctx, cid in
            let id = cid.map { String(cString: $0) } ?? ""
            ClassroomCenter.post(ctx) { $0.refreshLive(id) }
        }
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
        // Teacher keys over Sync (§2.5) need the sync engine's side records, which it doesn't
        // have yet: none read, none written (each device adds a class with its teacher key).
        h.sync_side_count = { _ in 0 }
        h.sync_side = { _, _, rid in rid?.pointee = nil; return nil }
        h.sync_put_side = { _, _, _ in }
        h.sync_delete_side = { _, _ in }
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
            refreshLive(c.id)
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
                               warning: Self.str(cl_classroom_class_warning(e, i))))
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

    func refreshLive(_ cid: String) {
        guard let e = engine else { return }
        var l = CRLive()
        l.on = cl_classroom_live_on(e, cid)
        l.ended = cl_classroom_live_ended(e, cid)
        l.reveal = cl_classroom_live_reveal(e, cid)
        l.takeOver = cl_classroom_live_take_over(e, cid)
        l.session = Self.str(cl_classroom_live_session(e, cid))
        l.cdl = Self.str(cl_classroom_live_cdl(e, cid))
        l.ver = cl_classroom_live_ver(e, cid)
        l.step = Int(cl_classroom_live_step(e, cid))
        l.hasPredict = cl_classroom_live_has_predict(e, cid)
        l.prompt = Self.str(cl_classroom_live_prompt(e, cid))
        l.lights = (0..<Int(cl_classroom_live_light_count(e, cid))).map { Self.str(cl_classroom_live_light(e, cid, Int32($0))) }
        l.myRight = Int(cl_classroom_live_my_right(e, cid))
        l.myTotal = Int(cl_classroom_live_my_total(e, cid))
        l.connection = Self.str(cl_classroom_live_connection(e, cid))
        if live[cid] != l {
            live[cid] = l
            liveArrived(cid, l)
        }
        var a = CRAnswers()
        a.answered = Int(cl_classroom_answers_answered(e, cid))
        a.students = Int(cl_classroom_answers_students(e, cid))
        a.right = Int(cl_classroom_answers_right(e, cid))
        a.wrong = Int(cl_classroom_answers_wrong(e, cid))
        a.lights = (0..<Int(cl_classroom_answers_light_count(e, cid))).map { i in
            var ones: Int32 = 0, zeros: Int32 = 0
            let name = Self.str(cl_classroom_answers_light(e, cid, Int32(i), &ones, &zeros))
            return CRAnswers.Light(name: name, ones: Int(ones), zeros: Int(zeros))
        }
        if answers[cid] != a { answers[cid] = a }
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

    func goLive(_ cid: String, cdl: String, done: @escaping Done) { call(done) { cl_classroom_go_live($0, cid, cdl, $1, $2) } }
    func push(_ cid: String, cdl: String, prompt: String?, lights: [String], reveal: Bool, done: @escaping Done) {
        call(done) { e, fn, box in
            Self.withCStrings(lights) { ptrs in
                ptrs.withUnsafeBufferPointer { b in
                    cl_classroom_push(e, cid, cdl, prompt, b.baseAddress, Int32(lights.count), reveal, fn, box)
                }
            }
        }
    }
    func endLive(_ cid: String, done: @escaping Done) { call(done) { cl_classroom_end_live($0, cid, $1, $2) } }
    func takeOverLive(_ cid: String, done: @escaping Done) { call(done) { cl_classroom_take_over_live($0, cid, $1, $2) } }

    // MARK: Student

    /// result: "<class name>\n<1|0 open>".
    func previewJoinCode(_ text: String, done: @escaping Done) { call(done) { cl_classroom_preview_join_code($0, text, $1, $2) } }
    func join(_ text: String, name: String, done: @escaping Done) { call(done) { cl_classroom_join($0, text, name, $1, $2) } }
    func renameMe(_ cid: String, _ name: String, done: @escaping Done) { call(done) { cl_classroom_rename($0, cid, name, $1, $2) } }
    func handIn(_ cid: String, _ aid: String, cdl: String, done: @escaping Done) { call(done) { cl_classroom_hand_in($0, cid, aid, cdl, $1, $2) } }
    func follow(_ cid: String, _ on: Bool) {
        if on { following.insert(cid) } else { following.remove(cid) }
        guard let engine else { return }
        cl_classroom_follow(engine, cid, on)
    }
    func sendAnswer(_ cid: String, _ guesses: [(String, Int)], done: @escaping Done) {
        call(done) { e, fn, box in
            Self.withCStrings(guesses.map(\.0)) { ptrs in
                let values = guesses.map { Int32($0.1) }
                ptrs.withUnsafeBufferPointer { b in
                    values.withUnsafeBufferPointer { v in
                        cl_classroom_send_answer(e, cid, b.baseAddress, v.baseAddress, Int32(guesses.count), fn, box)
                    }
                }
            }
        }
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
        return names.map { n in found.first { $0.name.caseInsensitiveCompare(n) == .orderedSame }?.value ?? -1 }
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

    // MARK: The live view's window (a student following)

    /// A push arrived: a following student's live window shows it (read again in place).
    private func liveArrived(_ cid: String, _ l: CRLive) {
        guard !preview, following.contains(cid), l.on, !l.cdl.isEmpty, LiveWindow.isOpen(cid) else { return }
        if liveShown[cid] == l.ver && !(l.reveal) { return }
        liveShown[cid] = l.ver
        LiveWindow.show(cid, name: classes.first { $0.id == cid }?.name ?? "Class", cdl: l.cdl, predict: l.hasPredict && !l.reveal)
    }

    /// Join Live View: the teacher's circuit in a window of its own.
    func openLiveWindow(_ cid: String) {
        follow(cid, true)
        guard let l = live[cid], l.on, !l.cdl.isEmpty else { return }
        liveShown[cid] = l.ver
        LiveWindow.show(cid, name: classes.first { $0.id == cid }?.name ?? "Class", cdl: l.cdl, predict: l.hasPredict && !l.reveal)
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
    case predict(classId: String)
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
        case .predict(let c): "predict-\(c)"
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

/// A following student's live window: the teacher's circuit in a scratch file
/// that each push rewrites and reads again in place, in Simulation View (with
/// Predict's covers while the teacher asks a prediction).
@MainActor
enum LiveWindow {
    private static var files: [String: URL] = [:]

    static func isOpen(_ cid: String) -> Bool {
        guard let url = files[cid] else { return false }
        return document(url) != nil
    }

    private static func document(_ url: URL) -> NSDocument? {
        NSDocumentController.shared.documents.first { $0.fileURL?.standardizedFileURL == url.standardizedFileURL }
    }

    private static func controllers(_ doc: NSDocument) -> [CanvasController] {
        doc.windowControllers.compactMap { $0.window?.contentView }.flatMap(AssignmentCopies.canvases).compactMap(\.controller)
    }

    static func show(_ cid: String, name: String, cdl: String, predict: Bool) {
        let url: URL
        if let u = files[cid] { url = u } else {
            let dir = FileManager.default.temporaryDirectory.appendingPathComponent("CedarLogic Classroom/live-\(cid)", isDirectory: true)
            try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
            url = dir.appendingPathComponent("Live - \(ScratchCircuit.safeName(name)).cdl")
            files[cid] = url
        }
        try? cdl.write(to: url, atomically: true, encoding: .utf8)
        let after = {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
                MainActor.assumeIsolated {
                    guard let doc = document(url) else { return }
                    for c in controllers(doc) {
                        c.simView = true
                        c.predict.on = predict
                        c.note(predict ? "Your teacher asks for a prediction: click each covered light to guess, then Send My Guess in the Classroom window."
                                       : "Live: your teacher's circuit. Changes here aren't kept; Keep a Copy is in the Classroom window.")
                    }
                }
            }
        }
        if let doc = document(url) {
            try? doc.revert(toContentsOf: url, ofType: doc.fileType ?? UTType.cedarLogicCircuit.identifier)
            after()
        } else {
            NSDocumentController.shared.openDocument(withContentsOf: url, display: true) { _, _, _ in after() }
        }
    }

    /// The guesses made with Simulation View's Predict in the live window, by light name.
    static func guesses(_ cid: String) -> [String: Int] {
        guard let url = files[cid], let doc = document(url) else { return [:] }
        var out: [String: Int] = [:]
        for c in controllers(doc) {
            guard let core = c.document else { continue }
            for l in ClassroomCenter.namedLights(core, page: c.page) {
                if let g = c.predict.guesses[l.gate] { out[l.name] = g }
            }
        }
        return out
    }

    static func close(_ cid: String) {
        guard let url = files[cid] else { return }
        document(url)?.close()
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
        let due = a.dueAt.map { "You can hand in again until the due date (\(ClassroomText.shortDate($0)))." } ?? ""
        guard classroomAsk("Hand in “\(a.title)”?", "Your circuit goes to your teacher, encrypted for them. \(due)", "Hand In").ok else { return }
        handIn(link.classId, a.id, cdl: text) { ok, m, _ in
            let note = ok ? "Handed in." : m
            CanvasController.front?.note(note)
        }
    }
}
