// The Classroom window (CLASSROOM.md §5): the class list on the left, the class
// page on the right -- a teacher's (the join code card for the projector,
// assignments and their hand-ins, students, the live view, settings) or a
// student's (assignments to open, check and hand in, the live banner, moving
// and leaving) -- and the sheets: Create Classroom, the teacher-key sheet and
// its printed recovery sheet, I Have a Teacher Key, Join a Class, a move code,
// Post an Assignment, Hand-ins, Ask a Prediction. Drawn like Your Circuits.
// Every string from the other side lands in a Text (§4.10).

import AppKit
import SwiftUI

struct ClassroomLook {
    let dark: Bool
    static func rgb(_ r: Double, _ g: Double, _ b: Double) -> Color { Color(.sRGB, red: r / 255, green: g / 255, blue: b / 255) }
    var paper: Color { dark ? Self.rgb(28, 31, 37) : Self.rgb(250, 250, 252) }
    var side: Color { dark ? Self.rgb(23, 25, 30) : Self.rgb(241, 242, 246) }
    var card: Color { dark ? Self.rgb(36, 40, 48) : .white }
    var ink: Color { dark ? Self.rgb(226, 230, 238) : Self.rgb(30, 33, 40) }
    var dim: Color { ink.opacity(0.55) }
    var line: Color { ink.opacity(0.09) }
    var live: Color { Self.rgb(224, 64, 64) }
    var good: Color { dark ? Self.rgb(98, 200, 120) : Self.rgb(30, 140, 60) }
    var bad: Color { dark ? Self.rgb(240, 110, 100) : Self.rgb(190, 50, 40) }
}

/// The circuit on screen, for posting and going live (a stand-in for pictures and checks).
@MainActor
enum ClassroomFront {
    struct Circuit { var title: String; var cdl: String; var lights: [String]; var parts: Int; var pages: Int }
    static var override: Circuit?
    static var current: Circuit? {
        if let override { return override }
        guard let c = CanvasController.front, let doc = c.document else { return nil }
        let title = c.view?.window?.title ?? "Circuit"
        let parts = (0..<doc.pageCount).reduce(0) { $0 + Int(cl_document_gate_count(doc.handle, Int32($1))) }
        return Circuit(title: title, cdl: doc.saveText(), lights: ClassroomCenter.namedLights(doc, page: c.page).map(\.name),
                       parts: parts, pages: doc.pageCount)
    }
}

/// Yes-or-no questions, as the app's others (NSAlert).
@MainActor
func classroomAsk(_ title: String, _ text: String, _ button: String, destructive: Bool = false, checkbox: String? = nil) -> (ok: Bool, checked: Bool) {
    let a = NSAlert()
    a.messageText = title
    a.informativeText = text
    a.addButton(withTitle: button)
    a.addButton(withTitle: "Cancel")
    if destructive { a.buttons[0].hasDestructiveAction = true }
    if let checkbox {
        a.showsSuppressionButton = true
        a.suppressionButton?.title = checkbox
        a.suppressionButton?.state = .off
    }
    let ok = a.runModal() == .alertFirstButtonReturn
    return (ok, a.suppressionButton?.state == .on)
}

@MainActor
func classroomTell(_ title: String, _ text: String) {
    let a = NSAlert()
    a.messageText = title
    a.informativeText = text
    a.addButton(withTitle: "OK")
    a.runModal()
}

// MARK: - The window

struct ClassroomView: View {
    @ObservedObject var center: ClassroomCenter
    @ObservedObject private var prefs = Prefs.shared
    /// For pictures: which section of a teacher's page shows.
    var initialSection: TeacherSection = .assignments

    init(center: ClassroomCenter? = nil, section: TeacherSection = .assignments) {
        self.center = center ?? .shared
        initialSection = section
    }

    private var look: ClassroomLook { ClassroomLook(dark: prefs.dark) }
    private var accent: Color { prefs.accentColor(dark: prefs.dark) }
    private var current: CRClass? { center.classes.first { $0.id == center.selected } }

    var body: some View {
        HStack(spacing: 0) {
            sidebar.frame(width: 230)
            Rectangle().fill(look.line).frame(width: 1)
            VStack(spacing: 0) {
                if let n = center.notice {
                    HStack {
                        Image(systemName: "info.circle")
                        Text(n).font(.system(size: 12)).fixedSize(horizontal: false, vertical: true)
                        Spacer()
                        Button("OK") { center.notice = nil }.controlSize(.small)
                    }
                    .padding(10).background(accent.opacity(0.14))
                }
                if let c = current {
                    if c.teaching {
                        TeacherPage(center: center, cls: c, look: look, accent: accent, section: initialSection).id(c.id)
                    } else {
                        StudentPage(center: center, cls: c, look: look, accent: accent).id(c.id)
                    }
                } else {
                    empty
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(look.paper)
        }
        .frame(minWidth: 820, idealWidth: 960, minHeight: 560, idealHeight: 680)
        .preferredColorScheme(prefs.dark ? .dark : .light)
        .sheet(item: $center.sheet) { s in ClassroomSheetView(center: center, sheet: s, look: look, accent: accent) }
        .onAppear { center.start() }
    }

    private var sidebar: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text("Classroom").font(.system(size: 19, weight: .bold)).foregroundStyle(look.ink)
                .padding(.horizontal, 18).padding(.top, 20)
            Text("Classes you teach and classes you're in.").font(.system(size: 12)).foregroundStyle(look.dim)
                .padding(.horizontal, 18).padding(.top, 6)
            ScrollView {
                VStack(spacing: 2) {
                    ForEach(center.classes) { c in classRow(c) }
                }
                .padding(.horizontal, 8).padding(.top, 12)
            }
            Spacer(minLength: 0)
            VStack(alignment: .leading, spacing: 8) {
                Button("Create Classroom…") { center.sheet = .create }.frame(maxWidth: .infinity)
                Button("Join a Class…") { center.sheet = .join(prefill: "") }.frame(maxWidth: .infinity)
                Menu("More") {
                    Button("I Have a Teacher Key…") { center.sheet = .addTeacherKey(prefill: "") }
                    Button("I Have a Move Code…") { center.sheet = .moveIn(prefill: "") }
                }
                .menuStyle(.borderlessButton).foregroundStyle(look.dim)
            }
            .padding(14)
        }
        .background(look.side)
    }

    private func classRow(_ c: CRClass) -> some View {
        let sel = c.id == center.selected
        let sub = c.teaching ? "Teaching · " + ClassroomText.students(center.students[c.id]?.count ?? 0) : "As \(c.studentName)"
        return HStack(spacing: 10) {
            Image(systemName: c.teaching ? "person.3.fill" : "graduationcap.fill")
                .foregroundStyle(accent).frame(width: 30, height: 30)
                .background(RoundedRectangle(cornerRadius: 8).fill(accent.opacity(sel ? 0.24 : 0.13)))
            VStack(alignment: .leading, spacing: 2) {
                Text(c.name).font(.system(size: 13, weight: .semibold)).foregroundStyle(look.ink).lineLimit(1)
                Text(sub).font(.system(size: 11)).foregroundStyle(look.dim).lineLimit(1)
            }
            Spacer(minLength: 4)
            if c.live || (center.live[c.id]?.on ?? false) {
                Text("LIVE").font(.system(size: 9, weight: .bold)).foregroundStyle(.white)
                    .padding(.horizontal, 6).frame(height: 18).background(Capsule().fill(look.live))
            }
        }
        .padding(8)
        .background(RoundedRectangle(cornerRadius: 10).fill(sel ? accent.opacity(look.dark ? 0.22 : 0.14) : .clear))
        .contentShape(Rectangle())
        .onTapGesture { center.selected = c.id }
    }

    private var empty: some View {
        VStack(spacing: 14) {
            Image(systemName: "graduationcap").font(.system(size: 44)).foregroundStyle(accent)
            Text("No classes yet").font(.system(size: 17, weight: .bold)).foregroundStyle(look.ink)
            Text("Teachers: create a classroom, then write its join code on the board.\nStudents: join with the code from the board.")
                .multilineTextAlignment(.center).font(.system(size: 13)).foregroundStyle(look.dim)
            HStack {
                Button("Create Classroom…") { center.sheet = .create }
                Button("Join a Class…") { center.sheet = .join(prefill: "") }.keyboardShortcut(.defaultAction)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

/// A status line for a class: working, offline, an error (the core's sentences).
struct ClassStatusLine: View {
    let cls: CRClass
    let look: ClassroomLook
    var body: some View {
        if !cls.statusText.isEmpty && cls.statusKind != 0 {
            HStack(spacing: 6) {
                Image(systemName: cls.statusKind == 1 ? "arrow.triangle.2.circlepath" : cls.statusKind == 2 ? "wifi.slash" : "exclamationmark.triangle")
                Text(cls.statusText).fixedSize(horizontal: false, vertical: true)
            }
            .font(.system(size: 12)).foregroundStyle(cls.statusKind >= 2 ? look.bad : look.dim)
        }
        if !cls.warning.isEmpty {
            Text(cls.warning).font(.system(size: 12)).foregroundStyle(look.bad).fixedSize(horizontal: false, vertical: true)
        }
    }
}

// MARK: - Teacher

enum TeacherSection: String, CaseIterable { case assignments = "Assignments", students = "Students", live = "Live", settings = "Settings" }

struct TeacherPage: View {
    @ObservedObject var center: ClassroomCenter
    let cls: CRClass
    let look: ClassroomLook
    let accent: Color
    @State var section: TeacherSection
    @State private var message = ""
    @State private var busy = false

    init(center: ClassroomCenter, cls: CRClass, look: ClassroomLook, accent: Color, section: TeacherSection) {
        self.center = center; self.cls = cls; self.look = look; self.accent = accent
        _section = State(initialValue: section)
    }

    private var studentCount: Int { center.students[cls.id]?.count ?? 0 }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                VStack(alignment: .leading, spacing: 4) {
                    Text(cls.name).font(.system(size: 22, weight: .bold)).foregroundStyle(look.ink)
                    ClassStatusLine(cls: cls, look: look)
                }
                JoinCodeCard(center: center, cls: cls, students: studentCount, look: look, accent: accent)
                if center.live[cls.id]?.on == true && section != .live {
                    TeacherLiveBar(center: center, cls: cls, look: look, accent: accent, compact: true)
                }
                Picker("", selection: $section) {
                    ForEach(TeacherSection.allCases, id: \.self) { Text($0.rawValue).tag($0) }
                }
                .pickerStyle(.segmented).labelsHidden().frame(maxWidth: 420)
                switch section {
                case .assignments: assignmentsSection
                case .students: studentsSection
                case .live: TeacherLiveBar(center: center, cls: cls, look: look, accent: accent, compact: false)
                case .settings: TeacherSettings(center: center, cls: cls, look: look)
                }
                if !message.isEmpty { Text(message).font(.system(size: 12)).foregroundStyle(look.bad) }
            }
            .padding(24)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .onAppear { center.pageOpen(cls.id, true) }
        .onDisappear { center.pageOpen(cls.id, false) }
    }

    private var assignmentsSection: some View {
        let list = center.assignments[cls.id] ?? []
        let front = ClassroomFront.current
        return VStack(alignment: .leading, spacing: 10) {
            HStack {
                Text(front.map { "Posts the circuit on screen: \($0.title)" } ?? "Open the circuit you want to post first.")
                    .font(.system(size: 12)).foregroundStyle(look.dim)
                Spacer()
                Button("Post Assignment…") { center.sheet = .post(classId: cls.id, editing: nil) }.disabled(front == nil)
            }
            if list.isEmpty {
                Text("No assignments yet.").font(.system(size: 13)).foregroundStyle(look.dim).padding(.vertical, 20)
            }
            ForEach(list) { a in
                HStack(alignment: .top, spacing: 12) {
                    VStack(alignment: .leading, spacing: 3) {
                        Text(a.title).font(.system(size: 14, weight: .semibold)).foregroundStyle(look.ink)
                        Text(ClassroomText.dueLine(a) + " · " + (a.keySealed ? "Only I check" : a.keyText.isEmpty ? "No answer key" : "Students can check"))
                            .font(.system(size: 11)).foregroundStyle(look.dim)
                        if a.unreadable { Text(a.problem.isEmpty ? "Couldn't be read" : a.problem).font(.system(size: 11)).foregroundStyle(look.bad) }
                    }
                    Spacer()
                    Button("Hand-ins") { center.sheet = .handIns(classId: cls.id, aid: a.id) }
                    Menu("•••") {
                        Button("Edit…") { center.sheet = .post(classId: cls.id, editing: a.id) }
                        Button("Open Starter Circuit") { ScratchCircuit.open(a.cdl, named: a.title) }
                        Divider()
                        Button("Delete…") { deleteAssignment(a) }
                    }
                    .menuStyle(.borderlessButton).fixedSize()
                }
                .padding(12)
                .background(RoundedRectangle(cornerRadius: 10).fill(look.card))
            }
        }
    }

    private func deleteAssignment(_ a: CRAssignment) {
        guard classroomAsk("Delete “\(a.title)”?", "Hand-ins for it are deleted too.", "Delete", destructive: true).ok else { return }
        center.deleteAssignment(cls.id, a.id) { ok, m, _ in message = ok ? "" : m }
    }

    private var studentsSection: some View {
        let list = center.students[cls.id] ?? []
        return VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text(ClassroomText.students(list.count)).font(.system(size: 13, weight: .semibold)).foregroundStyle(look.ink)
                Spacer()
                Button("Refresh") { center.fetchStudents(cls.id) { ok, m, _ in message = ok ? "" : m } }
            }
            if list.isEmpty {
                Text("Nobody has joined yet. Students join with the code above.").font(.system(size: 13)).foregroundStyle(look.dim)
            }
            ForEach(list) { s in
                HStack {
                    Text(s.unreadable ? "Couldn't be read" : s.name).font(.system(size: 13)).foregroundStyle(look.ink)
                    Spacer()
                    Text(s.joinedAt.map { "Joined \(ClassroomText.dayTime($0))" } ?? "").font(.system(size: 11)).foregroundStyle(look.dim)
                    Text(s.seenAt.map { "· seen \(agoText($0))" } ?? "").font(.system(size: 11)).foregroundStyle(look.dim)
                    Menu("•••") { Button("Remove from Class…") { remove(s) } }.menuStyle(.borderlessButton).fixedSize()
                }
                .padding(.vertical, 6).padding(.horizontal, 10)
                .background(RoundedRectangle(cornerRadius: 8).fill(look.card))
            }
        }
    }

    private func remove(_ s: CRStudent) {
        let r = classroomAsk("Remove \(s.name) from the class?", "They can't see the class any more. What they handed in stays in your hand-ins.",
                             "Remove", destructive: true, checkbox: "Also delete everything they handed in")
        guard r.ok else { return }
        center.removeStudents(cls.id, [s.id], deleteHandIns: r.checked) { ok, m, _ in message = ok ? "" : m }
    }
}

/// The join code, very large for the projector, with its QR code.
struct JoinCodeCard: View {
    @ObservedObject var center: ClassroomCenter
    let cls: CRClass
    let students: Int
    let look: ClassroomLook
    let accent: Color
    @State private var message = ""

    var body: some View {
        let link = ClassroomCenter.webLink(kind: 1, cls.joinCode)
        HStack(alignment: .center, spacing: 22) {
            VStack(alignment: .leading, spacing: 8) {
                Text("Join code").font(.system(size: 12, weight: .semibold)).foregroundStyle(look.dim)
                Text(ClassroomCenter.grouped(cls.joinCode))
                    .font(.system(size: 40, weight: .bold, design: .monospaced)).foregroundStyle(cls.joinOpen ? look.ink : look.dim)
                    .textSelection(.enabled).minimumScaleFactor(0.5).lineLimit(1)
                Text("Students: CedarLogic Online › Classroom › Join a Class, or scan.").font(.system(size: 12)).foregroundStyle(look.dim)
                Text((cls.joinOpen ? "Joining is open" : "Joining is closed") + " · " + ClassroomText.students(students))
                    .font(.system(size: 12, weight: .medium)).foregroundStyle(cls.joinOpen ? look.good : look.bad)
                HStack {
                    Button(cls.joinOpen ? "Close Joining" : "Open Joining") {
                        center.setJoinOpen(cls.id, !cls.joinOpen) { ok, m, _ in message = ok ? "" : m }
                    }
                    Button("Change Code…") { changeCode() }
                    Button("Show on Projector") { ProjectorWindow.show(className: cls.name, code: cls.joinCode, link: link) }
                }
                if !message.isEmpty { Text(message).font(.system(size: 12)).foregroundStyle(look.bad) }
            }
            Spacer(minLength: 0)
            SyncQRCode(text: link).frame(width: 150, height: 150)
                .clipShape(RoundedRectangle(cornerRadius: 8))
                .accessibilityLabel("QR code students scan to join")
        }
        .padding(18)
        .background(RoundedRectangle(cornerRadius: 14).fill(look.card))
        .overlay(RoundedRectangle(cornerRadius: 14).stroke(accent.opacity(0.35)))
    }

    private func changeCode() {
        guard classroomAsk("Make a new join code?",
                           "The old one stops working at once. Students who already joined stay in the class; anyone you removed can't see anything new.",
                           "Change").ok else { return }
        center.newJoinCode(cls.id) { ok, m, _ in message = ok ? "" : m }
    }
}

/// ● Live · Digital Logic 101 · 212 students · step 4, the buttons, and the prediction's counts.
struct TeacherLiveBar: View {
    @ObservedObject var center: ClassroomCenter
    let cls: CRClass
    let look: ClassroomLook
    let accent: Color
    let compact: Bool
    @State private var message = ""
    @State private var busy = false

    var body: some View {
        let l = center.live[cls.id] ?? CRLive()
        let a = center.answers[cls.id] ?? CRAnswers()
        VStack(alignment: .leading, spacing: 10) {
            if l.on {
                HStack(spacing: 8) {
                    Circle().fill(look.live).frame(width: 10, height: 10)
                    Text("Live · \(cls.name) · \(ClassroomText.students(center.students[cls.id]?.count ?? a.students)) · step \(l.step)")
                        .font(.system(size: 14, weight: .semibold)).foregroundStyle(look.ink)
                    Spacer()
                    if l.connection == "fallback" { Text("Slow connection").font(.system(size: 11)).foregroundStyle(look.dim) }
                }
                HStack {
                    Button("Push This Circuit") { push(prompt: nil, lights: [], reveal: false) }.disabled(busy || ClassroomFront.current == nil)
                    Button("Ask a Prediction…") { center.sheet = .predict(classId: cls.id) }.disabled(busy || ClassroomFront.current == nil)
                    Button("Reveal") { push(prompt: l.prompt, lights: l.lights, reveal: true) }.disabled(busy || !l.hasPredict || l.reveal)
                    Button("End Live") { end() }.disabled(busy)
                }
                if l.hasPredict {
                    Text("Prediction: “\(l.prompt)” · \(a.answered) of \(a.students) answered"
                         + (l.reveal ? " · \(a.right) right, \(a.wrong) wrong" : ""))
                        .font(.system(size: 13)).foregroundStyle(look.ink).fixedSize(horizontal: false, vertical: true)
                    PredictionBars(lights: a.lights, look: look)
                }
                if l.takeOver {
                    HStack {
                        Text("You're live from another device. Take over here? The other device stops pushing.").font(.system(size: 12))
                        Button("Take Over") { center.takeOverLive(cls.id) { ok, m, _ in message = ok ? "" : m } }
                    }
                }
            } else if !compact {
                Text(l.ended ? "The live view ended." : "Show your circuit on every student's screen as you work.")
                    .font(.system(size: 13)).foregroundStyle(look.dim)
                Text(ClassroomFront.current.map { "Goes live with: \($0.title)" } ?? "Open the circuit you want to show first.")
                    .font(.system(size: 12)).foregroundStyle(look.dim)
                Button("Go Live") { goLive() }.disabled(busy || ClassroomFront.current == nil).keyboardShortcut(.defaultAction)
            }
            if !message.isEmpty { Text(message).font(.system(size: 12)).foregroundStyle(look.bad) }
        }
        .padding(compact ? 12 : 16)
        .background(RoundedRectangle(cornerRadius: 12).fill(l.on ? look.live.opacity(0.08) : look.card))
        .overlay(RoundedRectangle(cornerRadius: 12).stroke(l.on ? look.live.opacity(0.4) : .clear))
    }

    private func finish(_ ok: Bool, _ m: String) {
        busy = false
        message = ok ? "" : m
    }
    private func goLive() {
        guard let f = ClassroomFront.current else { return }
        if ClassroomFront.override == nil, !classroomAsk("Go live?", ClassroomText.goLiveAsk, "Go Live").ok { return }
        busy = true
        center.goLive(cls.id, cdl: f.cdl) { ok, m, _ in finish(ok, m) }
    }
    private func push(prompt: String?, lights: [String], reveal: Bool) {
        guard let f = ClassroomFront.current else { return }
        busy = true
        center.push(cls.id, cdl: f.cdl, prompt: prompt, lights: lights, reveal: reveal) { ok, m, _ in finish(ok, m) }
    }
    private func end() {
        busy = true
        center.endLive(cls.id) { ok, m, _ in finish(ok, m) }
    }
}

struct TeacherSettings: View {
    @ObservedObject var center: ClassroomCenter
    let cls: CRClass
    let look: ClassroomLook
    @State private var name = ""
    @State private var message = ""

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack {
                TextField("Class name", text: $name).textFieldStyle(.roundedBorder).frame(maxWidth: 320)
                Button("Rename") {
                    center.renameClass(cls.id, name.trimmingCharacters(in: .whitespaces)) { ok, m, _ in message = ok ? "Renamed." : m }
                }
                .disabled(name.trimmingCharacters(in: .whitespaces).isEmpty || name == cls.name)
            }
            if let e = cls.expiresAt {
                Text("Removed from the website on \(ClassroomText.longDay(e)) unless someone opens it. Opening it, or a student handing in, keeps it.")
                    .font(.system(size: 12)).foregroundStyle(look.dim)
            }
            HStack {
                Button("Show Teacher Key") { center.sheet = .teacherKey(classId: cls.id) }
                Button("Remove from This Device…") {
                    if classroomAsk("Remove \(cls.name) from this computer?",
                                    "The class stays on the website and on your other devices. To open it here again you'll need your teacher key.",
                                    "Remove").ok { center.forgetClass(cls.id) }
                }
                Button("Delete Class…") {
                    guard classroomAsk("Delete \(cls.name)?",
                                       "Every assignment, hand-in and student in it is deleted from the website. Students keep the circuits on their own devices. This can't be undone.",
                                       "Delete", destructive: true).ok else { return }
                    center.deleteClass(cls.id) { ok, m, _ in message = ok ? "" : m }
                }
            }
            if !message.isEmpty { Text(message).font(.system(size: 12)).foregroundStyle(look.dim) }
        }
        .onAppear { name = cls.name }
    }
}

// MARK: - Student

struct StudentPage: View {
    @ObservedObject var center: ClassroomCenter
    let cls: CRClass
    let look: ClassroomLook
    let accent: Color
    @State private var message = ""
    @State private var checks: [String: (Int, String)] = [:]

    var body: some View {
        let list = center.assignments[cls.id] ?? []
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                VStack(alignment: .leading, spacing: 4) {
                    Text(cls.name).font(.system(size: 22, weight: .bold)).foregroundStyle(look.ink)
                    HStack(spacing: 6) {
                        Text("You're in this class as \(cls.studentName)").font(.system(size: 13)).foregroundStyle(look.dim)
                        Button("Change") { rename() }.buttonStyle(.link).font(.system(size: 13))
                    }
                    ClassStatusLine(cls: cls, look: look)
                }
                StudentLivePanel(center: center, cls: cls, look: look, accent: accent)
                Text("Assignments").font(.system(size: 15, weight: .semibold)).foregroundStyle(look.ink)
                if list.isEmpty {
                    Text("Nothing assigned yet.").font(.system(size: 13)).foregroundStyle(look.dim)
                }
                ForEach(list) { a in row(a) }
                if !message.isEmpty { Text(message).font(.system(size: 12)).foregroundStyle(look.bad) }
                Divider().padding(.vertical, 6)
                HStack {
                    Button("Move to Another Device…") {
                        center.makeMoveCode(cls.id) { ok, m, code in
                            if ok { center.sheet = .moveCode(classId: cls.id, code: code) } else { message = m }
                        }
                    }
                    Button("Remove from This Device…") {
                        if classroomAsk("Remove \(cls.name) from this computer?",
                                        "You stay in the class. To use it here again, make a move code on another device that's in the class, or join again (your teacher will then see you twice).",
                                        "Remove").ok { center.forgetMembership(cls.id) }
                    }
                    Button("Leave Class…") { leave() }
                }
            }
            .padding(24)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .onAppear { center.pageOpen(cls.id, true) }
        .onDisappear { center.pageOpen(cls.id, false) }
    }

    private func row(_ a: CRAssignment) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(alignment: .top) {
                VStack(alignment: .leading, spacing: 3) {
                    Text(a.title).font(.system(size: 14, weight: .semibold)).foregroundStyle(look.ink)
                    Text(ClassroomText.dueLine(a) + " · " + ClassroomText.handInLine(a)).font(.system(size: 11))
                        .foregroundStyle(a.handedInAt != nil && !a.changedSince ? look.good : look.dim)
                }
                Spacer()
                if a.unreadable {
                    Text(a.problem.isEmpty ? "Couldn't be read (the teacher may need to post it again)" : a.problem)
                        .font(.system(size: 11)).foregroundStyle(look.bad)
                } else {
                    Button("Open") { open(a) }
                    if !a.keyText.isEmpty {
                        Button("Check My Circuit") { check(a) }
                    }
                    Button(a.handedInAt == nil ? "Hand In" : "Hand In Again") { handIn(a) }.disabled(a.closed)
                }
            }
            if !a.instructions.isEmpty {
                Text(a.instructions).font(.system(size: 12)).foregroundStyle(look.ink.opacity(0.8)).fixedSize(horizontal: false, vertical: true)
            }
            if a.keySealed {
                Text("Your teacher checks this one.").font(.system(size: 11)).foregroundStyle(look.dim)
            }
            if let r = checks[a.id] {
                Text(r.0 == 0 ? "Matches what was asked." : r.1).font(.system(size: 11)).foregroundStyle(r.0 == 0 ? look.good : look.bad)
            }
        }
        .padding(12)
        .background(RoundedRectangle(cornerRadius: 10).fill(look.card))
    }

    private func open(_ a: CRAssignment) {
        do {
            let item = try AssignmentCopies.make(classId: cls.id, assignment: a)
            Library.open(item.circuit)
        } catch {
            message = "Couldn't make your copy: \(error.localizedDescription)"
        }
    }

    private func check(_ a: CRAssignment) {
        guard let item = try? AssignmentCopies.make(classId: cls.id, assignment: a) else { return }
        let text = AssignmentCopies.text(of: item) ?? a.cdl
        let r = ClassroomCenter.check(cdl: text, key: a.keyText, names: a.keyNames)
        checks[a.id] = (r.verdict, r.summary)
        AssignmentCopies.openAndCheck(item, key: a.keyText, names: a.keyNames)
    }

    private func handIn(_ a: CRAssignment) {
        guard let item = AssignmentCopies.copy(classId: cls.id, aid: a.id), let text = AssignmentCopies.text(of: item) else {
            message = "Open the assignment and build your circuit first."
            return
        }
        let doc = try? CoreDocument(data: Data(text.utf8))
        let parts = doc.map { d in (0..<d.pageCount).reduce(0) { $0 + Int(cl_document_gate_count(d.handle, Int32($1))) } } ?? 0
        let pages = doc?.pageCount ?? 1
        let until = a.dueAt.map { "You can hand in again until the due date (\(ClassroomText.shortDate($0)))." } ?? "You can hand in again whenever you like."
        let unchanged = a.handedInAt != nil && !a.changedSince ? " Nothing changed since you handed in." : ""
        guard classroomAsk("Hand in “\(a.title)”?",
                           "Your circuit (\(parts) parts, \(pages) \(pages == 1 ? "page" : "pages")) goes to your teacher, encrypted for them. \(until)\(unchanged)",
                           "Hand In").ok else { return }
        center.handIn(cls.id, a.id, cdl: text) { ok, m, _ in message = ok ? "" : m }
    }

    private func rename() {
        let a = NSAlert()
        a.messageText = "Your name, as your teacher should see it"
        let f = NSTextField(string: cls.studentName)
        f.frame = NSRect(x: 0, y: 0, width: 260, height: 24)
        a.accessoryView = f
        a.addButton(withTitle: "Change")
        a.addButton(withTitle: "Cancel")
        guard a.runModal() == .alertFirstButtonReturn else { return }
        let n = f.stringValue.trimmingCharacters(in: .whitespaces)
        guard !n.isEmpty else { return }
        center.renameMe(cls.id, n) { ok, m, _ in message = ok ? "" : m }
    }

    private func leave() {
        guard classroomAsk("Leave “\(cls.name)”?",
                           "Your teacher won't see you in the class any more. Work you handed in stays with your teacher until the class is deleted. Your circuits stay on this device.",
                           "Leave", destructive: true).ok else { return }
        center.leaveClass(cls.id) { ok, m, _ in message = ok ? "" : m }
    }
}

/// "Your teacher is live. [Join Live View]", and once joined the prompt, the guesses and the score.
struct StudentLivePanel: View {
    @ObservedObject var center: ClassroomCenter
    let cls: CRClass
    let look: ClassroomLook
    let accent: Color
    @State private var guesses: [String: Int] = [:]
    @State private var sentFor: String?
    @State private var message = ""

    var body: some View {
        let l = center.live[cls.id] ?? CRLive()
        let following = center.following.contains(cls.id)
        if l.on || (l.ended && following) {
            VStack(alignment: .leading, spacing: 10) {
                HStack(spacing: 8) {
                    Circle().fill(l.on ? look.live : look.dim).frame(width: 10, height: 10)
                    if !l.on {
                        Text("The live view ended.").font(.system(size: 14, weight: .semibold)).foregroundStyle(look.ink)
                    } else if following {
                        Text("Live · \(cls.name) · step \(l.step)").font(.system(size: 14, weight: .semibold)).foregroundStyle(look.ink)
                    } else {
                        Text("Your teacher is live.").font(.system(size: 14, weight: .semibold)).foregroundStyle(look.ink)
                    }
                    Spacer()
                    if !following && l.on {
                        Button("Join Live View") { center.openLiveWindow(cls.id) }.keyboardShortcut(.defaultAction)
                    } else {
                        Button("Keep a Copy") { ScratchCircuit.keep(l.cdl, named: "\(cls.name) – live") }.disabled(l.cdl.isEmpty)
                        if l.on { Button("Show Window") { center.openLiveWindow(cls.id) } }
                        if !l.on { Button("Close") { center.follow(cls.id, false); LiveWindow.close(cls.id) } }
                    }
                }
                if following && l.on && l.hasPredict {
                    Text("Your teacher asks: \(l.prompt)").font(.system(size: 13, weight: .medium)).foregroundStyle(look.ink)
                    if l.reveal {
                        Text(l.myRight >= 0 ? "\(l.myRight) of \(l.myTotal) right" : "Revealed.")
                            .font(.system(size: 13, weight: .semibold)).foregroundStyle(l.myRight == l.myTotal ? look.good : look.ink)
                    } else {
                        Text("Guess each light (or click the covered lights in the live window), then send.")
                            .font(.system(size: 12)).foregroundStyle(look.dim)
                        HStack(spacing: 14) {
                            ForEach(l.lights, id: \.self) { name in
                                HStack(spacing: 4) {
                                    Text(name).font(.system(size: 12, weight: .semibold))
                                    Picker("", selection: Binding(get: { guesses[name] ?? -1 }, set: { guesses[name] = $0 })) {
                                        Text("?").tag(-1); Text("0").tag(0); Text("1").tag(1)
                                    }
                                    .pickerStyle(.segmented).labelsHidden().frame(width: 96)
                                }
                            }
                        }
                        HStack {
                            Button("Send My Guess") { send(l) }
                            if sentFor == "\(l.session)/\(l.ver)" { Text("Sent.").font(.system(size: 12)).foregroundStyle(look.good) }
                        }
                    }
                }
                if !message.isEmpty { Text(message).font(.system(size: 12)).foregroundStyle(look.bad) }
            }
            .padding(14)
            .background(RoundedRectangle(cornerRadius: 12).fill(look.live.opacity(0.08)))
            .overlay(RoundedRectangle(cornerRadius: 12).stroke(look.live.opacity(0.4)))
        }
    }

    private func send(_ l: CRLive) {
        let fromWindow = LiveWindow.guesses(cls.id, asked: l.lights)
        let pairs = l.lights.compactMap { n -> (String, Int)? in
            if let g = guesses[n], g >= 0 { return (n, g) }
            if let g = fromWindow[n] { return (n, g) }
            return nil
        }
        guard !pairs.isEmpty else { message = "Guess at least one light first."; return }
        center.sendAnswer(cls.id, pairs) { ok, m, _ in
            message = ok ? "" : m
            if ok { sentFor = "\(l.session)/\(l.ver)" }
        }
    }
}

/// The answers to a prediction, a bar for each light: how many said 1 and how many said 0.
struct PredictionBars: View {
    let lights: [CRAnswers.Light]
    let look: ClassroomLook

    var body: some View {
        VStack(alignment: .leading, spacing: 5) {
            ForEach(lights, id: \.name) { l in
                let total = l.ones + l.zeros
                HStack(spacing: 8) {
                    Text(l.name).font(.system(size: 12, weight: .semibold)).foregroundStyle(look.ink)
                        .lineLimit(1).frame(width: 64, alignment: .leading)
                    GeometryReader { g in
                        HStack(spacing: 0) {
                            Rectangle().fill(look.good).frame(width: total == 0 ? 0 : g.size.width * CGFloat(l.ones) / CGFloat(total))
                            Rectangle().fill(look.ink.opacity(0.35))
                        }
                        .opacity(total == 0 ? 0.25 : 1)
                    }
                    .frame(height: 10).clipShape(RoundedRectangle(cornerRadius: 3)).frame(maxWidth: 220)
                    Text("1: \(l.ones)").font(.system(size: 11, weight: .medium)).monospacedDigit().foregroundStyle(look.good)
                    Text("0: \(l.zeros)").font(.system(size: 11)).monospacedDigit().foregroundStyle(look.dim)
                }
                .accessibilityElement(children: .ignore)
                .accessibilityLabel("\(l.name): \(l.ones) said 1, \(l.zeros) said 0")
            }
        }
    }
}

// MARK: - Sheets

struct ClassroomSheetView: View {
    @ObservedObject var center: ClassroomCenter
    let sheet: ClassroomSheet
    let look: ClassroomLook
    let accent: Color

    var body: some View {
        Group {
            switch sheet {
            case .create: CreateClassSheet(center: center, look: look)
            case .teacherKey(let cid): TeacherKeySheet(center: center, classId: cid, look: look)
            case .addTeacherKey(let p): CodeEntrySheet(center: center, kind: 0, prefill: p, look: look)
            case .join(let p): CodeEntrySheet(center: center, kind: 1, prefill: p, look: look)
            case .moveIn(let p): CodeEntrySheet(center: center, kind: 2, prefill: p, look: look)
            case .moveCode(_, let code): MoveCodeSheet(center: center, code: code, look: look)
            case .post(let cid, let aid): PostAssignmentSheet(center: center, classId: cid, editing: aid, look: look)
            case .handIns(let cid, let aid): HandInsSheet(center: center, classId: cid, aid: aid, look: look)
            case .predict(let cid): PredictSheet(center: center, classId: cid, look: look)
            }
        }
        .background(look.paper)
        .preferredColorScheme(look.dark ? .dark : .light)
    }
}

private struct SheetTitle: View {
    let text: String
    let look: ClassroomLook
    var body: some View { Text(text).font(.system(size: 17, weight: .bold)).foregroundStyle(look.ink) }
}

struct CreateClassSheet: View {
    @ObservedObject var center: ClassroomCenter
    let look: ClassroomLook
    @State var name = ""
    @State private var working = false
    @State private var message = ""

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            SheetTitle(text: "Create a classroom", look: look)
            HStack {
                Text("Class name:").foregroundStyle(look.ink)
                TextField("Digital Logic 101", text: $name).textFieldStyle(.roundedBorder)
            }
            Text(ClassroomText.createBlurb).font(.system(size: 12)).foregroundStyle(look.dim).fixedSize(horizontal: false, vertical: true)
            if working { HStack { ProgressView().controlSize(.small); Text("Creating…").font(.system(size: 12)) } }
            if !message.isEmpty { Text(message).font(.system(size: 12)).foregroundStyle(look.bad).fixedSize(horizontal: false, vertical: true) }
            HStack {
                Spacer()
                Button("Cancel") { center.sheet = nil }.keyboardShortcut(.cancelAction)
                Button("Create") { create() }.keyboardShortcut(.defaultAction)
                    .disabled(working || name.trimmingCharacters(in: .whitespaces).isEmpty || name.count > 100)
            }
        }
        .padding(22).frame(width: 480)
    }

    private func create() {
        working = true
        center.createClass(name.trimmingCharacters(in: .whitespaces)) { ok, m, cid in
            working = false
            if ok {
                center.selected = cid
                center.sheet = .teacherKey(classId: cid)
            } else {
                message = m.isEmpty ? ClassroomText.offline + " Try again." : m
            }
        }
    }
}

/// The teacher key, large, with its QR code and the recovery sheet.
struct TeacherKeySheet: View {
    @ObservedObject var center: ClassroomCenter
    let classId: String
    let look: ClassroomLook
    @State private var copied = ""

    var body: some View {
        let cls = center.classes.first { $0.id == classId }
        let key = cls?.teacherKey ?? ""
        let link = ClassroomCenter.webLink(kind: 0, key)
        VStack(alignment: .leading, spacing: 14) {
            SheetTitle(text: "Your teacher key for \(cls?.name ?? "this class")", look: look)
            HStack(alignment: .top, spacing: 18) {
                VStack(alignment: .leading, spacing: 10) {
                    Text(ClassroomCenter.grouped(key)).font(.system(size: 22, weight: .bold, design: .monospaced))
                        .foregroundStyle(look.ink).textSelection(.enabled)
                    Text(ClassroomText.keyBlurb).font(.system(size: 12)).foregroundStyle(look.ink.opacity(0.85)).fixedSize(horizontal: false, vertical: true)
                    Text(ClassroomText.keyPrivate).font(.system(size: 12)).foregroundStyle(look.dim).fixedSize(horizontal: false, vertical: true)
                }
                SyncQRCode(text: link).frame(width: 140, height: 140)
            }
            if !copied.isEmpty { Text(copied).font(.system(size: 12)).foregroundStyle(look.good) }
            HStack {
                Button("Copy Key") { copy(ClassroomCenter.grouped(key)); copied = "Key copied." }
                Button("Copy Link") { copy(link); copied = "Link copied." }
                Button("Print Recovery Sheet…") { RecoverySheet.print(className: cls?.name ?? "", key: key, link: link) }
                Spacer()
                Button("Done") { center.sheet = nil }.keyboardShortcut(.defaultAction)
            }
        }
        .padding(22).frame(width: 600)
    }

    private func copy(_ s: String) {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(s, forType: .string)
    }
}

/// I Have a Teacher Key (kind 0), Join a Class (1), I Have a Move Code (2): the code, checked as
/// it's typed, then "Checking…", then the question.
struct CodeEntrySheet: View {
    @ObservedObject var center: ClassroomCenter
    let kind: Int
    let look: ClassroomLook
    @State var text: String
    @State private var name = ""
    @State private var working: String?
    @State private var found: String?      // the class name once checked
    @State private var foundStudent = ""   // a move code's student name
    @State private var closed = false
    @State private var message = ""

    init(center: ClassroomCenter, kind: Int, prefill: String, look: ClassroomLook, previewFound: String? = nil) {
        self.center = center; self.kind = kind; self.look = look
        _text = State(initialValue: prefill)
        _found = State(initialValue: previewFound)
    }

    private var title: String { ["Add a class with your teacher key", "Join a class", "Move to this device"][kind] }
    private var field: String { ["Type or paste the key, or a link", "Code from the board", "Move code from your other device"][kind] }
    private var parsed: (code: String?, why: String?) { ClassroomCenter.parse(text, kind: kind) }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            if let found {
                foundView(found)
            } else {
                SheetTitle(text: title, look: look)
                Text(field + ":").foregroundStyle(look.ink)
                TextField(kind == 1 ? "K7QM-4XPD-2FJ3" : "", text: $text).textFieldStyle(.roundedBorder)
                    .font(.system(size: 16, design: .monospaced))
                if let why = parsed.why { Text(why).font(.system(size: 12)).foregroundStyle(look.bad).fixedSize(horizontal: false, vertical: true) }
                if kind == 1 {
                    Button("I have a move code from my other device…") { center.sheet = .moveIn(prefill: "") }.buttonStyle(.link)
                }
            }
            if let working { HStack { ProgressView().controlSize(.small); Text(working).font(.system(size: 12)) } }
            if !message.isEmpty { Text(message).font(.system(size: 12)).foregroundStyle(look.bad).fixedSize(horizontal: false, vertical: true) }
            HStack {
                Spacer()
                Button("Cancel") { center.sheet = nil }.keyboardShortcut(.cancelAction)
                if found == nil {
                    Button("Continue") { check() }.keyboardShortcut(.defaultAction).disabled(parsed.code == nil || working != nil)
                } else if !closed {
                    Button(kind == 0 ? "Add" : "Join") { finish() }.keyboardShortcut(.defaultAction)
                        .disabled(working != nil || (kind == 1 && name.trimmingCharacters(in: .whitespaces).isEmpty))
                }
            }
        }
        .padding(22).frame(width: 500)
    }

    @ViewBuilder private func foundView(_ cls: String) -> some View {
        switch kind {
        case 0:
            SheetTitle(text: "Add “\(cls)” to this device?", look: look)
            Text("You'll see its assignments, hand-ins and live view here too.").font(.system(size: 12)).foregroundStyle(look.dim)
        case 1:
            if closed {
                SheetTitle(text: "Joining “\(cls)” is closed.", look: look)
                Text("Ask your teacher to open it.").font(.system(size: 12)).foregroundStyle(look.dim)
            } else {
                SheetTitle(text: "Join \(cls)?", look: look)
                Text("Your name, as your teacher should see it:").foregroundStyle(look.ink)
                TextField("Sam Lee", text: $name).textFieldStyle(.roundedBorder)
                Text(ClassroomText.joinPrivacy).font(.system(size: 12)).foregroundStyle(look.dim).fixedSize(horizontal: false, vertical: true)
            }
        default:
            SheetTitle(text: "Join “\(cls)” as \(foundStudent) on this device?", look: look)
            Text("This device hands in as you too.").font(.system(size: 12)).foregroundStyle(look.dim)
        }
    }

    private func check() {
        guard let code = parsed.code else { return }
        working = kind == 0 ? "Checking…" : "Checking the code…"
        message = ""
        let done: ClassroomCenter.Done = { ok, m, r in
            working = nil
            guard ok else { message = m; return }
            let lines = r.components(separatedBy: "\n")
            if kind == 1 { closed = lines.count > 1 && lines[1] == "0" }
            if kind == 2 { foundStudent = lines.count > 1 ? lines[1] : "" }
            found = lines.first ?? ""
        }
        switch kind {
        case 0: center.previewTeacherKey(code, done: done)
        case 1: center.previewJoinCode(code, done: done)
        default: center.previewMoveCode(code, done: done)
        }
    }

    private func finish() {
        guard let code = parsed.code else { return }
        working = kind == 0 ? "Adding…" : "Joining…"
        let done: ClassroomCenter.Done = { ok, m, r in
            working = nil
            guard ok else { message = m; return }
            if !r.isEmpty { center.selected = r }
            center.sheet = nil
        }
        switch kind {
        case 0: center.addTeacherKey(code, done: done)
        case 1: center.join(code, name: name.trimmingCharacters(in: .whitespaces), done: done)
        default: center.importMoveCode(code, done: done)
        }
    }
}

struct MoveCodeSheet: View {
    @ObservedObject var center: ClassroomCenter
    let code: String
    let look: ClassroomLook
    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            SheetTitle(text: "Move to another device", look: look)
            HStack(alignment: .top, spacing: 18) {
                VStack(alignment: .leading, spacing: 10) {
                    Text(ClassroomCenter.grouped(code)).font(.system(size: 22, weight: .bold, design: .monospaced)).foregroundStyle(look.ink)
                        .textSelection(.enabled)
                    Text(ClassroomText.moveBlurb).font(.system(size: 12)).foregroundStyle(look.dim).fixedSize(horizontal: false, vertical: true)
                }
                SyncQRCode(text: ClassroomCenter.webLink(kind: 2, code)).frame(width: 130, height: 130)
            }
            HStack {
                Button("Copy Code") {
                    NSPasteboard.general.clearContents()
                    NSPasteboard.general.setString(ClassroomCenter.grouped(code), forType: .string)
                }
                Spacer()
                Button("Done") { center.sheet = nil }.keyboardShortcut(.defaultAction)
            }
        }
        .padding(22).frame(width: 560)
    }
}

/// Post an assignment to a class, from the circuit on screen (or edit one).
struct PostAssignmentSheet: View {
    @ObservedObject var center: ClassroomCenter
    let classId: String
    let editing: String?
    let look: ClassroomLook
    enum KeyChoice: Int { case none, studentsCheck, onlyMe }
    @State private var title = ""
    @State private var instructions = ""
    @State private var hasDue = true
    @State private var due = Calendar.current.date(bySettingHour: 23, minute: 59, second: 0, of: Date().addingTimeInterval(7 * 86400)) ?? Date()
    @State private var closeAfterDue = false
    @State private var keyChoice = KeyChoice.none
    @State private var keyText = ""
    @State private var working = false
    @State private var message = ""
    @State private var loaded = false

    private var cls: CRClass? { center.classes.first { $0.id == classId } }
    private var existing: CRAssignment? { editing.flatMap { e in center.assignments[classId]?.first { $0.id == e } } }

    var body: some View {
        let front = ClassroomFront.current
        VStack(alignment: .leading, spacing: 12) {
            SheetTitle(text: editing == nil ? "Post an assignment to \(cls?.name ?? "the class")" : "Edit “\(existing?.title ?? "")”", look: look)
            HStack {
                Text("Title:").frame(width: 90, alignment: .trailing)
                TextField("Lab 3: half adder", text: $title).textFieldStyle(.roundedBorder)
            }
            HStack {
                Text("Due:").frame(width: 90, alignment: .trailing)
                Toggle("", isOn: $hasDue).labelsHidden()
                DatePicker("", selection: $due).labelsHidden().disabled(!hasDue)
                Toggle("No hand-ins after the due date", isOn: $closeAfterDue).disabled(!hasDue)
            }
            HStack(alignment: .top) {
                Text("Instructions:").frame(width: 90, alignment: .trailing)
                TextEditor(text: $instructions).font(.system(size: 12)).frame(height: 70)
                    .overlay(RoundedRectangle(cornerRadius: 4).stroke(look.line))
            }
            HStack(alignment: .top) {
                Text("Starter circuit:").frame(width: 90, alignment: .trailing)
                Text(editing != nil ? "The circuit on screen replaces the old one: \(front?.title ?? "none open")"
                     : front.map { "the circuit on screen: \($0.title) (\($0.parts) parts, \($0.pages) \($0.pages == 1 ? "page" : "pages")). Parts you locked stay locked." }
                     ?? "Open a circuit first.")
                    .font(.system(size: 12)).foregroundStyle(look.dim).fixedSize(horizontal: false, vertical: true)
            }
            HStack(alignment: .top) {
                Text("Answer key:").frame(width: 90, alignment: .trailing)
                VStack(alignment: .leading, spacing: 6) {
                    Picker("", selection: $keyChoice) {
                        Text("None").tag(KeyChoice.none)
                        Text("Students can check their work").tag(KeyChoice.studentsCheck)
                        Text("Only I check").tag(KeyChoice.onlyMe)
                    }
                    .pickerStyle(.radioGroup).labelsHidden()
                    Text(ClassroomText.keyStudentsCheck).font(.system(size: 11)).foregroundStyle(look.dim).fixedSize(horizontal: false, vertical: true)
                    Text(ClassroomText.keyOnlyMe).font(.system(size: 11)).foregroundStyle(look.dim).fixedSize(horizontal: false, vertical: true)
                    if keyChoice != .none {
                        TextEditor(text: $keyText).font(.system(size: 12, design: .monospaced)).frame(height: 70)
                            .overlay(RoundedRectangle(cornerRadius: 4).stroke(look.line))
                        Text(keyText.isEmpty ? "As in Check My Circuit: a formula (S = A ^ B), a pasted truth table, a count, a state or timing table."
                             : CircuitCheck.kind(of: keyText.trimmingCharacters(in: .whitespacesAndNewlines)).kind.reading)
                            .font(.system(size: 11)).foregroundStyle(look.dim)
                    }
                }
            }
            if working { HStack { ProgressView().controlSize(.small); Text("Posting…").font(.system(size: 12)) } }
            if !message.isEmpty { Text(message).font(.system(size: 12)).foregroundStyle(look.bad).fixedSize(horizontal: false, vertical: true) }
            if editing != nil { Text("Students see the change the next time they look.").font(.system(size: 11)).foregroundStyle(look.dim) }
            HStack {
                Spacer()
                Button("Cancel") { center.sheet = nil }.keyboardShortcut(.cancelAction)
                Button(message.hasPrefix(ClassroomText.offline) ? "Try Again" : editing == nil ? "Post" : "Save") { post() }
                    .keyboardShortcut(.defaultAction)
                    .disabled(working || front == nil || title.trimmingCharacters(in: .whitespaces).isEmpty
                              || (keyChoice != .none && keyText.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty))
            }
        }
        .padding(22).frame(width: 640)
        .onAppear(perform: load)
    }

    private func load() {
        guard !loaded else { return }
        loaded = true
        if let a = existing {
            title = a.title
            instructions = a.instructions
            hasDue = a.dueAt != nil
            if let d = a.dueAt { due = d }
            closeAfterDue = a.closeAfterDue
            keyChoice = a.keySealed ? .onlyMe : a.keyText.isEmpty ? .none : .studentsCheck
            keyText = a.keyText
        } else if let f = ClassroomFront.current {
            title = f.title.replacingOccurrences(of: ".cdl", with: "")
        }
    }

    private func post() {
        guard let f = ClassroomFront.current else { return }
        working = true
        message = ""
        let key = keyChoice == .none ? "" : keyText.trimmingCharacters(in: .whitespacesAndNewlines)
        center.postAssignment(classId, editing: editing, title: title.trimmingCharacters(in: .whitespaces), instructions: instructions,
                              due: hasDue ? due : nil, closeAfterDue: hasDue && closeAfterDue, cdl: f.cdl, keyText: key,
                              keyNames: existing?.keyNames ?? "", studentsCanCheck: keyChoice != .onlyMe) { ok, m, _ in
            working = false
            if ok { center.sheet = nil } else { message = m.isEmpty ? ClassroomText.offline : m }
        }
    }
}

/// An assignment's hand-ins: who, when, attempts, the check, [Open]; Download All, Refresh.
struct HandInsSheet: View {
    @ObservedObject var center: ClassroomCenter
    let classId: String
    let aid: String
    let look: ClassroomLook
    @State private var message = ""
    @State private var working = false

    private var cls: CRClass? { center.classes.first { $0.id == classId } }
    private var assignment: CRAssignment? { center.assignments[classId]?.first { $0.id == aid } }

    var body: some View {
        let list = center.submissions["\(classId)/\(aid)"] ?? []
        let hasKey = assignment?.hasKey ?? false
        let total = center.students[classId]?.count ?? 0
        VStack(alignment: .leading, spacing: 12) {
            SheetTitle(text: "Hand-ins: \(assignment?.title ?? "")", look: look)
            HStack {
                Text("\(list.count) of \(total) handed in").font(.system(size: 13)).foregroundStyle(look.dim)
                Spacer()
                if let u = center.submissionsUpdated["\(classId)/\(aid)"] {
                    Text("Last updated \(u.formatted(date: .omitted, time: .shortened))").font(.system(size: 11)).foregroundStyle(look.dim)
                }
                if working { ProgressView().controlSize(.small) }
            }
            HStack {
                Text("Name").frame(width: 170, alignment: .leading)
                Text("Handed in").frame(width: 130, alignment: .leading)
                Text("Attempts").frame(width: 60, alignment: .leading)
                if hasKey { Text("Check").frame(maxWidth: .infinity, alignment: .leading) } else { Spacer() }
                Text("").frame(width: 60)
            }
            .font(.system(size: 11, weight: .semibold)).foregroundStyle(look.dim)
            ScrollView {
                VStack(spacing: 4) {
                    if list.isEmpty {
                        Text("Nothing handed in yet.").font(.system(size: 13)).foregroundStyle(look.dim).padding(.top, 20)
                    }
                    ForEach(list) { s in
                        HStack {
                            Text(s.name + (s.left ? " (left the class)" : "")).frame(width: 170, alignment: .leading).lineLimit(1)
                            Text(s.handedInAt.map(ClassroomText.dayTime) ?? "").frame(width: 130, alignment: .leading)
                            Text("\(s.attempts)").frame(width: 60, alignment: .leading)
                            if hasKey || s.unreadable {
                                Text(ClassroomText.checkLine(s, hasKey: hasKey))
                                    .foregroundStyle(s.verdict == 0 ? look.good : (s.verdict >= 1 || s.unreadable) ? look.bad : look.dim)
                                    .frame(maxWidth: .infinity, alignment: .leading).lineLimit(2)
                            } else { Spacer() }
                            Button("Open") { open(s) }.disabled(s.unreadable || s.cdl.isEmpty).frame(width: 60)
                        }
                        .font(.system(size: 12)).foregroundStyle(look.ink)
                        .padding(.vertical, 5).padding(.horizontal, 8)
                        .background(RoundedRectangle(cornerRadius: 6).fill(look.card))
                    }
                }
            }
            .frame(height: 300)
            if !message.isEmpty { Text(message).font(.system(size: 12)).foregroundStyle(look.bad) }
            HStack {
                Button("Download All…") { downloadAll(list) }.disabled(list.isEmpty)
                Button("Refresh") { refresh() }
                Spacer()
                Button("Done") { center.sheet = nil }.keyboardShortcut(.defaultAction)
            }
        }
        .padding(22).frame(width: 680)
        .onAppear {
            center.submissionsOpen(classId, aid, true)
            refresh()
        }
        .onDisappear { center.submissionsOpen(classId, aid, false) }
    }

    private func refresh() {
        working = true
        center.fetchSubmissions(classId, aid) { ok, m, _ in
            working = false
            message = ok ? "" : m
        }
    }

    private func open(_ s: CRSubmission) {
        let title = "\(s.name) — \(assignment?.title ?? "")"
        ScratchCircuit.open(s.cdl, named: title)
        let at = s.handedInAt.map(ClassroomText.dayTime) ?? ""
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.8) {
            MainActor.assumeIsolated {
                CanvasController.front?.note("Handed in \(at) · attempt \(s.attempts) · a copy for looking at: File › Save as Template or Your Circuits keeps it.")
            }
        }
    }

    private func downloadAll(_ list: [CRSubmission]) {
        guard let a = assignment else { return }
        let name = HandInExport.folderName(cls?.name ?? "Class", a.title)
        let panel = NSSavePanel()
        panel.nameFieldStringValue = name + ".zip"
        panel.allowedContentTypes = [.zip]
        guard panel.runModal() == .OK, let url = panel.url else { return }
        let tmp = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString).appendingPathComponent(name)
        do {
            try HandInExport.write(assignment: a, submissions: list, to: tmp)
            if !HandInExport.zip(tmp, to: url) { message = "Couldn't make the zip file." }
        } catch {
            message = error.localizedDescription
        }
        try? FileManager.default.removeItem(at: tmp.deletingLastPathComponent())
    }
}

/// Ask a Prediction: the question and the lights (Simulation View's Predict names).
struct PredictSheet: View {
    @ObservedObject var center: ClassroomCenter
    let classId: String
    let look: ClassroomLook
    @State private var prompt = ""
    @State private var chosen: Set<String> = []
    @State private var working = false
    @State private var message = ""

    var body: some View {
        let lights = ClassroomFront.current?.lights ?? []
        VStack(alignment: .leading, spacing: 12) {
            SheetTitle(text: "Ask a prediction", look: look)
            TextField("What will Q be after the next clock?", text: $prompt).textFieldStyle(.roundedBorder)
            Text("Which lights do students guess? They're covered on students' screens until you Reveal.")
                .font(.system(size: 12)).foregroundStyle(look.dim)
            if lights.isEmpty { Text("This page has no lights.").font(.system(size: 12)).foregroundStyle(look.bad) }
            ForEach(lights, id: \.self) { l in
                Toggle(l, isOn: Binding(get: { chosen.contains(l) }, set: { if $0 { chosen.insert(l) } else { chosen.remove(l) } }))
            }
            if !message.isEmpty { Text(message).font(.system(size: 12)).foregroundStyle(look.bad) }
            HStack {
                Spacer()
                Button("Cancel") { center.sheet = nil }.keyboardShortcut(.cancelAction)
                Button("Ask") { ask(lights) }.keyboardShortcut(.defaultAction)
                    .disabled(working || prompt.trimmingCharacters(in: .whitespaces).isEmpty || chosen.isEmpty)
            }
        }
        .padding(22).frame(width: 460)
        .onAppear { if chosen.isEmpty { chosen = Set(lights) } }
    }

    private func ask(_ lights: [String]) {
        guard let f = ClassroomFront.current else { return }
        working = true
        center.push(classId, cdl: f.cdl, prompt: prompt.trimmingCharacters(in: .whitespaces), lights: lights.filter(chosen.contains), reveal: false) { ok, m, _ in
            working = false
            if ok { center.sheet = nil } else { message = m }
        }
    }
}

// MARK: - The projector and the recovery sheet

/// The join code filling a screen (the projector's, if Presenter is showing on one).
struct JoinProjectorView: View {
    let className: String
    let code: String
    let link: String
    var body: some View {
        GeometryReader { g in
            VStack(spacing: g.size.height * 0.04) {
                Text("Join \(className)").font(.system(size: g.size.height * 0.06, weight: .bold))
                Text(ClassroomCenter.grouped(code)).font(.system(size: g.size.width * 0.075, weight: .heavy, design: .monospaced))
                    .minimumScaleFactor(0.3).lineLimit(1)
                SyncQRCode(text: link).frame(width: g.size.height * 0.38, height: g.size.height * 0.38)
                Text("CedarLogic Online › Classroom › Join a Class, or scan.").font(.system(size: g.size.height * 0.035))
            }
            .foregroundStyle(.white)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(Color(.sRGB, red: 0.08, green: 0.09, blue: 0.11))
        }
    }
}

@MainActor
enum ProjectorWindow {
    private static var window: NSWindow?
    static func show(className: String, code: String, link: String) {
        window?.close()
        let screen = NSScreen.screens.count > 1 ? NSScreen.screens.last! : (NSScreen.main ?? NSScreen.screens[0])
        let w = NSWindow(contentRect: screen.frame.insetBy(dx: screen.frame.width * 0.1, dy: screen.frame.height * 0.1),
                         styleMask: [.titled, .closable, .resizable, .fullSizeContentView], backing: .buffered, defer: false, screen: screen)
        w.title = "Join \(className)"
        w.titlebarAppearsTransparent = true
        w.isReleasedWhenClosed = false
        w.contentView = NSHostingView(rootView: JoinProjectorView(className: className, code: code, link: link))
        w.collectionBehavior = [.fullScreenPrimary]
        w.makeKeyAndOrderFront(nil)
        w.toggleFullScreen(nil)
        window = w
    }
}

/// The printed recovery sheet: the class, the date, the key large, its QR code and what to do with it.
struct RecoverySheetView: View {
    let className: String
    let key: String
    let link: String
    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            Text("CedarLogic Classroom — teacher key").font(.system(size: 13, weight: .semibold)).foregroundStyle(.gray)
            Text(className).font(.system(size: 26, weight: .bold))
            Text("Printed \(ClassroomText.longDay(Date()))").font(.system(size: 12)).foregroundStyle(.gray)
            Text(ClassroomCenter.grouped(key)).font(.system(size: 30, weight: .bold, design: .monospaced))
            SyncQRCode(text: link).frame(width: 200, height: 200)
            Text(ClassroomText.keyBlurb).font(.system(size: 12)).fixedSize(horizontal: false, vertical: true)
            Text(ClassroomText.keyPrivate).font(.system(size: 12)).fixedSize(horizontal: false, vertical: true)
            Text(ClassroomText.keyRecoveryUse).font(.system(size: 12, weight: .semibold)).fixedSize(horizontal: false, vertical: true)
            Spacer()
        }
        .foregroundStyle(.black)
        .padding(54)
        .frame(width: 612, height: 792, alignment: .topLeading)
        .background(Color.white)
    }
}

@MainActor
enum RecoverySheet {
    static func print(className: String, key: String, link: String) {
        let v = NSHostingView(rootView: RecoverySheetView(className: className, key: key, link: link))
        v.frame = NSRect(x: 0, y: 0, width: 612, height: 792)
        let info = NSPrintInfo.shared.copy() as! NSPrintInfo
        info.topMargin = 0; info.bottomMargin = 0; info.leftMargin = 0; info.rightMargin = 0
        info.horizontalPagination = .fit; info.verticalPagination = .fit
        let op = NSPrintOperation(view: v, printInfo: info)
        op.jobTitle = "\(className) teacher key"
        op.run()
    }
}
