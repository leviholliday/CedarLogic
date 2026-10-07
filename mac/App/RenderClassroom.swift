// Classroom without clicking (headless, then quits):
//
//   CedarLogic --render-classroom <dir>
//       the Classroom window and its sheets, light and dark, as PNGs, from
//       made-up classes (no engine, no network).
//   CL_CLASSROOM_SERVICE=http://localhost:8788 CedarLogic --classroom-e2e <dir>
//       a teacher and a student (two engines, each with a folder of its own
//       under <dir>) through the Classroom code the window uses, against a
//       running classroom service: create, join, post with a key students can
//       check, open as the student's own copy, check, hand in, the teacher sees
//       the hand-in with its check result, opens it, Download All, go live,
//       predict, answers, reveal, end, leave, delete. Prints PASS/FAIL lines,
//       pictures the real screens along the way (e2e-*.png), exits 1 on failure.

import AppKit
import SwiftUI

@MainActor
enum RenderClassroom {
    static func runIfAsked() {
        let args = CommandLine.arguments
        if let i = args.firstIndex(of: "--render-classroom"), i + 1 < args.count {
            let dir = URL(fileURLWithPath: args[i + 1], isDirectory: true)
            try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
            renderAll(dir)
            exit(0)
        }
        if let i = args.firstIndex(of: "--classroom-e2e"), i + 1 < args.count {
            let dir = URL(fileURLWithPath: args[i + 1], isDirectory: true)
            try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
            exit(EndToEnd(dir: dir).run() ? 0 : 1)
        }
    }

    // MARK: Pictures

    static func snap<V: View>(_ name: String, _ view: V, dark: Bool, in dir: URL, size: NSSize? = nil) {
        Prefs.shared.dark = dark
        let host = NSHostingView(rootView: view)
        host.appearance = NSAppearance(named: dark ? .darkAqua : .aqua)
        let s = size ?? host.fittingSize
        host.frame = NSRect(origin: .zero, size: s)
        let win = NSWindow(contentRect: NSRect(origin: NSPoint(x: -5000, y: -5000), size: s), styleMask: [.titled], backing: .buffered, defer: false)
        win.contentView = host
        win.appearance = host.appearance
        host.layoutSubtreeIfNeeded()
        RunLoop.main.run(until: Date().addingTimeInterval(0.35))
        if let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(s.width * 2), pixelsHigh: Int(s.height * 2),
                                      bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                                      colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0) {
            rep.size = s
            host.cacheDisplay(in: host.bounds, to: rep)
            try? rep.representation(using: .png, properties: [:])?.write(to: dir.appendingPathComponent(name + ".png"))
        }
        win.close()
    }

    /// A circuit made from formulas (a half adder, or one with a mistake).
    static func circuit(_ formulas: String) -> CoreDocument {
        let doc = CoreDocument()
        guard let f = try? FormulaParser.parse(formulas) else { return doc }
        let plan = FormulaCircuit.plan(f, shape: .asWritten, style: .any, twoInputOnly: false)
        var strings: [UnsafeMutablePointer<CChar>] = []
        func c(_ s: String) -> UnsafePointer<CChar> { let p = strdup(s)!; strings.append(p); return UnsafePointer(p) }
        let gates = plan.parts.map { CLBuildGate(gate: c($0.gate), x: $0.x, y: $0.y, label: $0.label.map(c), angle: 0) }
        let wires = plan.wires.map { CLBuildWire(from: Int32($0.from), fromPin: c($0.fromPin), to: Int32($0.to), toPin: c($0.toPin)) }
        _ = cl_edit_build(doc.handle, 0, gates, Int32(gates.count), wires, Int32(wires.count), "Build")
        strings.forEach { free($0) }
        cl_edit_select_none(doc.handle, 0)
        for _ in 0..<40 { cl_document_step(doc.handle) }
        return doc
    }

    static func front(_ doc: CoreDocument, title: String) -> ClassroomFront.Circuit {
        let parts = (0..<doc.pageCount).reduce(0) { $0 + Int(cl_document_gate_count(doc.handle, Int32($1))) }
        return ClassroomFront.Circuit(title: title, cdl: doc.saveText(), lights: ClassroomCenter.namedLights(doc, page: 0).map(\.name),
                                      parts: parts, pages: doc.pageCount)
    }

    static func renderAll(_ dir: URL) {
        let prefs = Prefs.shared
        let saved = prefs.dark
        let doc = circuit("S = A ^ B\nC = AB")
        ClassroomFront.override = front(doc, title: "Half Adder")
        let now = Date()
        let t = ClassroomCenter.previewing()
        let cid = "9c89e40e9ea981bbfeaf61e93c91be46"
        t.classes = [
            CRClass(id: cid, name: "Digital Logic 101", teaching: true, teacherKey: "000G40R40M30E209185GR38E1YZ4", joinCode: "K7QM4XPD2FJ3",
                    joinOpen: true, expiresAt: now.addingTimeInterval(300 * 86400), live: true),
            CRClass(id: "c2", name: "Computer Architecture", teaching: true, teacherKey: "000G40R40M30E209185GR38E1YZ4", joinCode: "M2GT58X4MPKA", joinOpen: false),
            CRClass(id: "c3", name: "Physics Club", teaching: false, studentName: "Levi"),
        ]
        t.selected = cid
        t.students[cid] = ["Sam Lee", "Ava Chen", "Noah Patel", "Mia Johnson"].enumerated().map { i, n in
            CRStudent(id: "s\(i)", name: n, joinedAt: now.addingTimeInterval(-Double(i + 2) * 86400), seenAt: now.addingTimeInterval(-Double(i) * 900))
        }
        let lab = CRAssignment(id: "a1", title: "Lab 3: half adder", instructions: "Build S and C from A and B.", cdl: doc.saveText(),
                               dueAt: now.addingTimeInterval(3 * 86400), keyText: "S = A ^ B\nC = AB")
        t.assignments[cid] = [lab, CRAssignment(id: "a2", title: "Lab 4: full adder", dueAt: now.addingTimeInterval(10 * 86400), keySealed: true),
                              CRAssignment(id: "a3", title: "Warm-up: gates", dueAt: now.addingTimeInterval(-2 * 86400), closed: true)]
        t.submissions["\(cid)/a1"] = [
            CRSubmission(studentId: "s0", name: "Sam Lee", handedInAt: now.addingTimeInterval(-3600), attempts: 2, verdict: 0),
            CRSubmission(studentId: "s1", name: "Ava Chen", handedInAt: now.addingTimeInterval(-7200), attempts: 1, verdict: 1, summary: "2 rows wrong"),
            CRSubmission(studentId: "s2", name: "Noah Patel", handedInAt: now.addingTimeInterval(-9000), attempts: 1, verdict: 2, summary: "no light named C"),
            CRSubmission(studentId: "s9", name: "Jo Park", handedInAt: now.addingTimeInterval(-90000), attempts: 1, unreadable: true, problem: "Couldn't be verified", left: true),
        ]
        t.submissionsUpdated["\(cid)/a1"] = now
        t.live[cid] = CRLive(on: true, session: "x", cdl: doc.saveText(), ver: 4, step: 4, hasPredict: true, prompt: "What will S be when A = B = 1?",
                             lights: ["S", "C"], connection: "open")
        t.answers[cid] = CRAnswers(answered: 3, students: 4, lights: [.init(name: "S", ones: 1, zeros: 2), .init(name: "C", ones: 3, zeros: 0)])

        let s = ClassroomCenter.previewing()
        s.classes = [CRClass(id: cid, name: "Digital Logic 101", teaching: false, studentName: "Sam Lee", live: true),
                     CRClass(id: "c3", name: "Physics Club", teaching: false, studentName: "Sam")]
        s.selected = cid
        s.assignments[cid] = [
            CRAssignment(id: "a1", title: "Lab 3: half adder", instructions: "Build S and C from A and B. Label the lights S and C.",
                         dueAt: now.addingTimeInterval(3 * 86400), keyText: "S = A ^ B\nC = AB", handedInAt: now.addingTimeInterval(-3600), attempts: 1),
            CRAssignment(id: "a2", title: "Lab 4: full adder", dueAt: now.addingTimeInterval(10 * 86400), keySealed: true, pending: true),
            CRAssignment(id: "a3", title: "Warm-up: gates", dueAt: now.addingTimeInterval(-2 * 86400), closed: true),
        ]
        s.live[cid] = t.live[cid]
        s.following = [cid]
        let sBanner = ClassroomCenter.previewing()
        sBanner.classes = s.classes; sBanner.selected = cid; sBanner.assignments = s.assignments; sBanner.live = s.live
        let sRevealed = ClassroomCenter.previewing()
        sRevealed.classes = s.classes; sRevealed.selected = cid; sRevealed.assignments = s.assignments; sRevealed.following = [cid]
        var rl = t.live[cid]!; rl.reveal = true; rl.myRight = 1; rl.myTotal = 2
        sRevealed.live[cid] = rl
        let empty = ClassroomCenter.previewing()

        let win = NSSize(width: 980, height: 760)
        for dark in [false, true] {
            let m = dark ? "dark" : "light"
            let look = ClassroomLook(dark: dark), accent = prefs.accentColor(dark: dark)
            snap("teacher-assignments-\(m)", ClassroomView(center: t, section: .assignments), dark: dark, in: dir, size: win)
            snap("teacher-students-\(m)", ClassroomView(center: t, section: .students), dark: dark, in: dir, size: win)
            snap("teacher-live-\(m)", ClassroomView(center: t, section: .live), dark: dark, in: dir, size: win)
            snap("teacher-settings-\(m)", ClassroomView(center: t, section: .settings), dark: dark, in: dir, size: win)
            snap("student-class-\(m)", ClassroomView(center: s), dark: dark, in: dir, size: win)
            snap("student-banner-\(m)", ClassroomView(center: sBanner), dark: dark, in: dir, size: win)
            snap("student-revealed-\(m)", ClassroomView(center: sRevealed), dark: dark, in: dir, size: win)
            snap("empty-\(m)", ClassroomView(center: empty), dark: dark, in: dir, size: NSSize(width: 860, height: 560))
            snap("sheet-create-\(m)", CreateClassSheet(center: t, look: look, name: "Digital Logic 101").background(look.paper), dark: dark, in: dir)
            snap("sheet-teacher-key-\(m)", TeacherKeySheet(center: t, classId: cid, look: look).background(look.paper), dark: dark, in: dir)
            snap("sheet-join-\(m)", CodeEntrySheet(center: s, kind: 1, prefill: "K7QM-4XPD-2FJ", look: look).background(look.paper), dark: dark, in: dir)
            snap("sheet-join-name-\(m)", CodeEntrySheet(center: s, kind: 1, prefill: "", look: look, previewFound: "Digital Logic 101").background(look.paper), dark: dark, in: dir)
            snap("sheet-add-key-\(m)", CodeEntrySheet(center: t, kind: 0, prefill: "", look: look, previewFound: "Digital Logic 101").background(look.paper), dark: dark, in: dir)
            snap("sheet-move-code-\(m)", MoveCodeSheet(center: s, code: "M2GT58X4MPKAFA59NANTSBDENX83", look: look).background(look.paper), dark: dark, in: dir)
            snap("sheet-post-\(m)", PostAssignmentSheet(center: t, classId: cid, editing: nil, look: look).background(look.paper), dark: dark, in: dir)
            snap("sheet-handins-\(m)", HandInsSheet(center: t, classId: cid, aid: "a1", look: look).background(look.paper), dark: dark, in: dir)
            snap("sheet-predict-\(m)", PredictSheet(center: t, classId: cid, look: look).background(look.paper), dark: dark, in: dir)
            _ = accent
        }
        snap("recovery-sheet", RecoverySheetView(className: "Digital Logic 101", key: "000G40R40M30E209185GR38E1YZ4",
                                                 link: ClassroomCenter.webLink(kind: 0, "000G40R40M30E209185GR38E1YZ4")), dark: false, in: dir)
        snap("projector", JoinProjectorView(className: "Digital Logic 101", code: "K7QM4XPD2FJ3", link: ClassroomCenter.webLink(kind: 1, "K7QM4XPD2FJ3")),
             dark: true, in: dir, size: NSSize(width: 1280, height: 800))
        prefs.dark = saved
        ClassroomFront.override = nil
    }
}

// MARK: - End to end

@MainActor
final class EndToEnd {
    let dir: URL
    var failures = 0
    init(dir: URL) { self.dir = dir }

    func check(_ name: String, _ ok: Bool, _ detail: String = "") {
        print(ok ? "PASS" : "FAIL", name + (detail.isEmpty || ok ? "" : " -- \(detail)"))
        if !ok { failures += 1 }
    }

    /// Runs the main run loop until `f` holds (or the time's up).
    @discardableResult
    func until(_ seconds: Double = 20, _ f: () -> Bool) -> Bool {
        let end = Date().addingTimeInterval(seconds)
        while Date() < end {
            if f() { return true }
            RunLoop.main.run(until: Date().addingTimeInterval(0.05))
        }
        return f()
    }

    /// One of the center's calls, waited for: (ok, message, result).
    func wait(_ seconds: Double = 30, _ body: (@escaping ClassroomCenter.Done) -> Void) -> (Bool, String, String) {
        var r: (Bool, String, String)?
        body { ok, m, res in r = (ok, m, res) }
        until(seconds) { r != nil }
        return r ?? (false, "timed out", "")
    }

    func run() -> Bool {
        setvbuf(stdout, nil, _IONBF, 0)
        let env = ProcessInfo.processInfo.environment
        guard env["CL_CLASSROOM_SERVICE"] != nil || env["CL_CLASSROOM_URL"] != nil else {
            print("FAIL set CL_CLASSROOM_SERVICE to a running classroom service (node cloudflare/classroom/test/wrangler.mjs --port 8788)")
            return false
        }
        let fm = FileManager.default
        try? fm.removeItem(at: dir.appendingPathComponent("teacher"))
        try? fm.removeItem(at: dir.appendingPathComponent("student"))
        let teacher = ClassroomCenter(dir: dir.appendingPathComponent("teacher/Classroom"))
        let student = ClassroomCenter(dir: dir.appendingPathComponent("student/Classroom"))
        teacher.start(force: true)
        student.start(force: true)
        check("both engines start", teacher.engine != nil && student.engine != nil)
        defer { teacher.stop(); student.stop() }

        // The circuits: the starter, the student's right answer, a wrong one.
        let starter = RenderClassroom.circuit("S = A\nC = B")
        let right = RenderClassroom.circuit("S = A ^ B\nC = AB")
        let wrong = RenderClassroom.circuit("S = A + B\nC = AB")
        let key = "S = A ^ B\nC = AB"
        check("check hook: the right circuit matches", ClassroomCenter.check(cdl: right.saveText(), key: key, names: "").verdict == 0)
        check("check hook: the wrong one has wrong rows", ClassroomCenter.check(cdl: wrong.saveText(), key: key, names: "").verdict == 1)
        let lv = ClassroomCenter.lights(of: right.saveText(), ["S", "C"])
        check("lights hook: S and C read", lv != nil && lv!.count == 2, "\(String(describing: lv))")

        // Teacher: create.
        var r = wait { teacher.createClass("E2E Logic", done: $0) }
        check("teacher: create class", r.0, r.1)
        let cid = r.2
        guard let cls = teacher.classes.first(where: { $0.id == cid }) else { check("teacher: the class is listed", false); return false }
        check("teacher: key and join code", cls.teacherKey.count == 28 && cls.joinCode.count >= 12, "\(cls.teacherKey) \(cls.joinCode)")
        let look = ClassroomLook(dark: false)
        RenderClassroom.snap("e2e-teacher-key", TeacherKeySheet(center: teacher, classId: cid, look: look).background(look.paper), dark: false, in: dir)

        // Student: preview, join.
        r = wait { student.previewJoinCode(ClassroomCenter.grouped(cls.joinCode), done: $0) }
        check("student: preview join code", r.0 && r.2.hasPrefix("E2E Logic\n1"), "\(r.1) [\(r.2)]")
        r = wait { student.join(cls.joinCode, name: "Sam Lee", done: $0) }
        check("student: join", r.0 && r.2 == cid, r.1)
        _ = wait { teacher.fetchStudents(cid, done: $0) }
        check("teacher: sees Sam Lee", teacher.students[cid]?.contains { $0.name == "Sam Lee" } ?? false)

        // Teacher: post with a key students can check.
        ClassroomFront.override = RenderClassroom.front(starter, title: "Half Adder")
        r = wait {
            teacher.postAssignment(cid, editing: nil, title: "Lab 3: half adder", instructions: "Make S = A xor B, C = A and B.",
                                   due: Date().addingTimeInterval(86400), closeAfterDue: false, cdl: starter.saveText(),
                                   keyText: key, keyNames: "", studentsCanCheck: true, done: $0)
        }
        check("teacher: post assignment", r.0, r.1)
        let aid = teacher.assignments[cid]?.first?.id ?? ""
        RenderClassroom.snap("e2e-teacher-page", ClassroomView(center: teacher), dark: false, in: dir, size: NSSize(width: 980, height: 760))

        // Student: sees it, opens a copy, checks, hands in.
        student.pageOpen(cid, true)
        check("student: the assignment arrives", until(30) { student.assignments[cid]?.first?.id == aid })
        guard let a = student.assignments[cid]?.first(where: { $0.id == aid }) else { return false }
        check("student: the key is readable (students can check)", a.keyText == key && !a.keySealed)
        let copyText = a.cdl
        check("student: the starter circuit came through", (try? CoreDocument(data: Data(copyText.utf8))) != nil)
        check("student: checking the starter shows wrong rows", ClassroomCenter.check(cdl: copyText, key: a.keyText, names: a.keyNames).verdict == 1)
        check("student: checking the finished circuit matches", ClassroomCenter.check(cdl: right.saveText(), key: a.keyText, names: a.keyNames).verdict == 0)
        r = wait { student.handIn(cid, aid, cdl: wrong.saveText(), done: $0) }
        check("student: hand in (first try, wrong)", r.0, r.1)
        r = wait { student.handIn(cid, aid, cdl: right.saveText(), done: $0) }
        check("student: hand in again", r.0, r.1)
        student.refresh()
        check("student: shows handed in", until(10) { student.assignments[cid]?.first?.handedInAt != nil })
        RenderClassroom.snap("e2e-student-page", ClassroomView(center: student), dark: true, in: dir, size: NSSize(width: 980, height: 760))

        // Teacher: the hand-in with its check result.
        teacher.submissionsOpen(cid, aid, true)
        r = wait { teacher.fetchSubmissions(cid, aid, done: $0) }
        check("teacher: fetch hand-ins", r.0, r.1)
        until(15) { teacher.submissions["\(cid)/\(aid)"]?.first.map { $0.verdict >= 0 } ?? false || { teacher.refreshSubmissions(cid, aid); return false }() }
        let sub = teacher.submissions["\(cid)/\(aid)"]?.first
        check("teacher: sees Sam's hand-in, attempt 2", sub?.name == "Sam Lee" && sub?.attempts == 2, "\(String(describing: sub))")
        check("teacher: the check says it matches", sub?.verdict == 0, "verdict \(sub?.verdict ?? -9) \(sub?.summary ?? "")")
        check("teacher: the hand-in opens as a circuit", sub.flatMap { try? CoreDocument(data: Data($0.cdl.utf8)) } != nil)
        RenderClassroom.snap("e2e-handins", HandInsSheet(center: teacher, classId: cid, aid: aid, look: look).background(look.paper), dark: false, in: dir)
        if let tA = teacher.assignments[cid]?.first, let subs = teacher.submissions["\(cid)/\(aid)"] {
            let folder = dir.appendingPathComponent("export/" + HandInExport.folderName("E2E Logic", tA.title))
            let zip = dir.appendingPathComponent("export.zip")
            let ok = (try? HandInExport.write(assignment: tA, submissions: subs, to: folder)) != nil && HandInExport.zip(folder, to: zip)
            check("teacher: Download All writes the .cdl files, hand-ins.csv and the zip",
                  ok && fm.fileExists(atPath: folder.appendingPathComponent("Sam Lee.cdl").path)
                     && fm.fileExists(atPath: folder.appendingPathComponent("hand-ins.csv").path))
            try? fm.removeItem(at: dir.appendingPathComponent("export"))
        }
        teacher.submissionsOpen(cid, aid, false)

        // Live: go live, the student follows, predict, answers, reveal, end.
        ClassroomFront.override = RenderClassroom.front(right, title: "Half Adder")
        r = wait { teacher.goLive(cid, cdl: right.saveText(), done: $0) }
        check("teacher: go live", r.0, r.1)
        student.follow(cid, true)
        check("student: sees the live circuit", until(30) { student.live[cid]?.on == true && !(student.live[cid]?.cdl.isEmpty ?? true) })
        // A and B are off: S = 0, C = 0.
        r = wait { teacher.push(cid, cdl: right.saveText(), prompt: "What will S and C show?", lights: ["S", "C"], reveal: false, done: $0) }
        check("teacher: ask a prediction", r.0, r.1)
        check("student: the question arrives", until(30) { student.live[cid]?.hasPredict == true && student.live[cid]?.prompt == "What will S and C show?" })
        RenderClassroom.snap("e2e-student-predict", ClassroomView(center: student), dark: false, in: dir, size: NSSize(width: 980, height: 760))
        r = wait { student.sendAnswer(cid, [("S", 0), ("C", 1)], done: $0) }
        check("student: send my guess", r.0, r.1)
        check("teacher: the answer is counted", until(30) { teacher.answers[cid]?.answered == 1 }, "\(String(describing: teacher.answers[cid]))")
        let perS = teacher.answers[cid]?.lights.first { $0.name == "S" }
        check("teacher: counts per light (S: 0 once)", perS?.zeros == 1, "\(String(describing: teacher.answers[cid]))")
        r = wait { teacher.push(cid, cdl: right.saveText(), prompt: "What will S and C show?", lights: ["S", "C"], reveal: true, done: $0) }
        check("teacher: reveal", r.0, r.1)
        check("teacher: 1 answered, scored (right + wrong = 1)", until(20) {
            let a = teacher.answers[cid]; return (a?.right ?? 0) + (a?.wrong ?? 0) == 1
        }, "\(String(describing: teacher.answers[cid]))")
        check("student: revealed, scored 1 of 2", until(30) { student.live[cid]?.reveal == true && student.live[cid]?.myTotal == 2 },
              "\(String(describing: student.live[cid]))")
        check("student: score is 1 right", student.live[cid]?.myRight == 1)
        RenderClassroom.snap("e2e-teacher-live", ClassroomView(center: teacher, section: .live), dark: true, in: dir, size: NSSize(width: 980, height: 760))
        RenderClassroom.snap("e2e-student-revealed", ClassroomView(center: student), dark: true, in: dir, size: NSSize(width: 980, height: 760))
        r = wait { teacher.endLive(cid, done: $0) }
        check("teacher: end live", r.0, r.1)
        check("student: the live view ended", until(30) { student.live[cid]?.on == false })

        // Joining closed, a new code, a move code.
        r = wait { teacher.setJoinOpen(cid, false, done: $0) }
        check("teacher: close joining", r.0 && teacher.classes.first { $0.id == cid }?.joinOpen == false, r.1)
        let oldCode = teacher.classes.first { $0.id == cid }?.joinCode ?? ""
        r = wait { teacher.newJoinCode(cid, done: $0) }
        check("teacher: change code", r.0 && teacher.classes.first { $0.id == cid }?.joinCode != oldCode, r.1)
        r = wait { student.makeMoveCode(cid, done: $0) }
        check("student: move code", r.0 && r.2.count >= 28, r.1)

        // Leave, delete.
        student.follow(cid, false)
        student.pageOpen(cid, false)
        r = wait { student.leaveClass(cid, done: $0) }
        check("student: leave class", r.0 && !student.classes.contains { $0.id == cid }, r.1)
        r = wait { teacher.deleteClass(cid, done: $0) }
        check("teacher: delete class", r.0 && !teacher.classes.contains { $0.id == cid }, r.1)

        // Errors read as sentences.
        let bad = ClassroomCenter.parse("K7QM-4XPD-2FJ4", kind: 1)
        check("a mistyped join code gets a sentence", bad.code == nil && !(bad.why ?? "").isEmpty, bad.why ?? "")
        r = wait { student.previewJoinCode(oldCode, done: $0) }
        check("a join code that no longer works says so", !r.0 && !r.1.isEmpty, r.1)
        print("note: \(r.1)")
        ClassroomFront.override = nil
        print(failures == 0 ? "classroom-e2e: everything passed" : "classroom-e2e: \(failures) FAILED")
        return failures == 0
    }
}
