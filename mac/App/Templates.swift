// Templates: File ▸ New from Template starts a circuit from a built-in
// starter (a lab page, a counter, a 7-segment decoder, each kind of
// flip-flop and latch, ready to run, five registers, and four circuits with
// a classic mistake in them to find) or one of yours;
// File ▸ Save as Template keeps the circuit you're in as one of yours.
//
// Yours are folders in ~/Library/Application Support/CedarLogic/Templates,
// each holding name.txt and template.cdl. The built-in ones are built by
// the engine when first asked for, so they're always in the current format.

import AppKit
import SwiftUI

struct CircuitTemplate: Identifiable, Hashable {
    let id: String
    let name: String
    let detail: String
    let text: String
    /// Yours (a folder), or nil for a built-in one.
    let folder: URL?
    /// The picker's heading for a built-in one.
    var group = "Built In"
    /// A flip-flop's text with a running clock (`text` has one that moves
    /// only on Step Clock): the picker's Clock line chooses.
    var runningText: String? = nil

    /// This one with its clock running, if it has the choice.
    var running: CircuitTemplate {
        guard let runningText else { return self }
        return CircuitTemplate(id: id, name: name, detail: detail, text: runningText, folder: folder, group: group)
    }
}

@MainActor
enum Templates {
    /// Set just before a new document is made from a template; the document
    /// takes it (CircuitDocument.init).
    nonisolated(unsafe) static var pendingText: String?
    nonisolated(unsafe) static var pendingName: String?

    static var root: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("CedarLogic/Templates", isDirectory: true)
    }

    static func yours() -> [CircuitTemplate] {
        let fm = FileManager.default
        let ids = (try? fm.contentsOfDirectory(atPath: root.path)) ?? []
        return ids.filter { !$0.hasPrefix(".") }.compactMap { id -> (CircuitTemplate, Date)? in
            let folder = root.appendingPathComponent(id, isDirectory: true)
            let file = folder.appendingPathComponent("template.cdl")
            guard let text = try? String(contentsOf: file, encoding: .utf8) else { return nil }
            let name = (try? String(contentsOf: folder.appendingPathComponent("name.txt"), encoding: .utf8))?
                .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
            let date = (try? fm.attributesOfItem(atPath: file.path)[.modificationDate] as? Date) ?? .distantPast
            return (CircuitTemplate(id: id, name: name.isEmpty ? "Untitled Template" : name,
                                    detail: "Saved " + friendlyTime(date), text: text, folder: folder), date)
        }
        .sorted { $0.1 > $1.1 }
        .map(\.0)
    }

    static func use(_ t: CircuitTemplate) {
        pendingText = t.text
        pendingName = t.name
        NSDocumentController.shared.newDocument(nil)
        pendingText = nil
    }


    static func saveCurrent(from canvas: CanvasController) {
        guard let document = canvas.document else { return }
        let text = document.saveText()
        guard let name = MyParts.askName(title: "Save as Template",
                                         message: "Name your template. Start a circuit from it with File \u{25B8} New from Template\u{2026}",
                                         initial: canvas.documentTitle, button: "Save") else { return }
        let f = DateFormatter()
        f.dateFormat = "yyyyMMdd-HHmmss"
        let folder = root.appendingPathComponent("\(f.string(from: Date()))-\(Int.random(in: 1000...99999))", isDirectory: true)
        do {
            try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
            try name.write(to: folder.appendingPathComponent("name.txt"), atomically: true, encoding: .utf8)
            try text.write(to: folder.appendingPathComponent("template.cdl"), atomically: true, encoding: .utf8)
            canvas.note("Saved \u{201C}\(name)\u{201D} as a template.")
        } catch {
            canvas.note("Couldn't save that template.")
        }
    }

    static func rename(_ t: CircuitTemplate, to name: String) {
        guard let folder = t.folder else { return }
        try? name.write(to: folder.appendingPathComponent("name.txt"), atomically: true, encoding: .utf8)
    }

    static func delete(_ t: CircuitTemplate) {
        guard let folder = t.folder else { return }
        try? FileManager.default.trashItem(at: folder, resultingItemURL: nil)
    }

    // MARK: Built in

    /// New circuits' starting point (Settings > General), or nil for a blank page.
    nonisolated(unsafe) static var nextIsBlank = false

    static func template(id: String) -> CircuitTemplate? {
        builtIn.first { $0.id == id } ?? yours().first { $0.id == id }
    }

    /// Built by the engine when first asked for (and again when your name
    /// or the Step Clock key changes: the lab page and flip-flops carry them).
    private static var builtCache: (name: String, key: String?, list: [CircuitTemplate])?
    static var builtIn: [CircuitTemplate] {
        let name = Prefs.shared.studentName.trimmingCharacters(in: .whitespacesAndNewlines)
        let key = ShortcutStore.shared.combo(.stepClock)?.label
        if let c = builtCache, c.name == name, c.key == key { return c.list }
        let list = makeBuiltIn(name: name)
        builtCache = (name, key, list)
        return list
    }

    private static func makeBuiltIn(name: String) -> [CircuitTemplate] { [
        CircuitTemplate(id: "builtin-lab", name: "Lab Page",
                        detail: "A title, your name, the date and the course, with room for inputs and outputs",
                        text: labPage(name: name), folder: nil),
        CircuitTemplate(id: "builtin-counter", name: "4-Bit Counter",
                        detail: "A clock driving a counting register, shown on a hex display",
                        text: counter(), folder: nil),
        CircuitTemplate(id: "builtin-7seg", name: "7-Segment Decoder Starter",
                        detail: "Four switches and seven segment lights: build the decoder between them",
                        text: sevenSegment(), folder: nil),
    ] + flipFlops() + registers() + classicMistakes() }

    /// The group of circuits with a mistake to find: offered in New from
    /// Template, but never as the start of every new circuit.
    static let mistakesGroup = "Classic Mistakes"

    /// Built-in groups, in the picker's order.
    static var builtInGroups: [(name: String, list: [CircuitTemplate])] {
        var out: [(name: String, list: [CircuitTemplate])] = []
        for t in builtIn {
            if let i = out.firstIndex(where: { $0.name == t.group }) { out[i].list.append(t) } else { out.append((t.group, [t])) }
        }
        return out
    }

    /// A switch with `on` starts at 1; `angle` turns a part before it's
    /// wired; `params` are settings made once it's built (a clock's
    /// HALF_CYCLE, a TO or FROM's JUNCTION_ID).
    private struct Part {
        let gate: String; let x: Double; let y: Double; var label: String? = nil; var on = false
        var angle = 0.0
        var params: [String: String] = [:]
    }
    private struct Wire { let from: Int; let fromPin: String; let to: Int; let toPin: String }

    /// Builds parts and wires on a new circuit and returns its file text.
    /// Labels named in `big` get larger text; `manualClock` sets the clocks
    /// to "Only on Step Clock".
    private static func build(_ parts: [Part], _ wires: [Wire], big: [String: Double] = [:],
                              manualClock: Bool = false) -> String {
        let doc = CoreDocument()
        var strings: [UnsafeMutablePointer<CChar>] = []
        defer { strings.forEach { free($0) } }
        func c(_ s: String) -> UnsafePointer<CChar> { let p = strdup(s)!; strings.append(p); return UnsafePointer(p) }
        let gates = parts.map { CLBuildGate(gate: c($0.gate), x: $0.x, y: $0.y, label: $0.label.map(c), angle: $0.angle) }
        let links = wires.map { CLBuildWire(from: Int32($0.from), fromPin: c($0.fromPin), to: Int32($0.to), toPin: c($0.toPin)) }
        _ = cl_edit_build(doc.handle, 0, gates, Int32(gates.count), links, Int32(links.count), "Template")
        // Settings: a new circuit numbers its parts 1, 2, 3... in the order built.
        for (k, p) in parts.enumerated() where !p.params.isEmpty {
            let gate = k + 1
            guard doc.libraryName(ofGate: gate) == p.gate else { continue }
            for (name, value) in p.params.sorted(by: { $0.key < $1.key }) { _ = cl_gate_set_setting(doc.handle, gate, name, value) }
        }
        if manualClock {
            // Each clock, picked by its place like a click would.
            for p in parts where p.gate == "BB_CLOCK" {
                cl_edit_select_none(doc.handle, 0)
                _ = cl_edit_press(doc.handle, 0, p.x, p.y, 0, 0.05)
                cl_edit_release(doc.handle, p.x, p.y)
                let gate = cl_edit_single_gate(doc.handle, 0)
                if gate >= 0 { _ = cl_gate_set_setting(doc.handle, gate, "MANUAL", "true") }
            }
        }
        cl_edit_select_none(doc.handle, 0)
        for p in parts where p.on { _ = cl_document_click(doc.handle, 0, p.x, p.y) }
        var text = doc.saveText()
        for (label, height) in big {
            let quoted = NSRegularExpression.escapedPattern(for: label)
            if let re = try? NSRegularExpression(pattern: "(\\(gparam \"LABEL_TEXT\" \"\(quoted)\"\\)\\s*\\(gparam \"TEXT_HEIGHT\" \")[^\"]*") {
                text = re.stringByReplacingMatches(in: text, range: NSRange(text.startIndex..., in: text),
                                                   withTemplate: "$1\(height)")
            }
        }
        return text
    }

    /// A label whose left edge is at x (labels are placed by their middle).
    private static func label(_ text: String, x: Double, y: Double, height: Double = 2) -> Part {
        Part(gate: "AA_LABEL", x: x + Double(text.count) * 0.3 * height, y: y, label: text)
    }

    /// A label whose right edge is at x.
    private static func label(_ text: String, right x: Double, y: Double, height: Double = 2) -> Part {
        label(text, x: x - Double(text.count) * 0.6 * height, y: y, height: height)
    }

    private static func labPage(name: String) -> String {
        build([
            label("Lab 1: Title", x: -3.5, y: 12, height: 4),
            label(name.isEmpty ? "Name: ______________________" : "Name: \(name)", x: 0, y: 5),
            label("Date: ____________", x: 44, y: 5),
            label("Course: ____________", x: -1.6, y: 1),
            label("Inputs", x: -1.4, y: -8),
            label("Outputs", x: 60, y: -8),
        ], [], big: ["Lab 1: Title": 4])
    }

    private static func counter() -> String {
        var p: [Part] = [
            Part(gate: "BB_CLOCK", x: 0, y: 0),               // 0
            Part(gate: "AA_REGISTER4", x: 16, y: 0),          // 1
            Part(gate: "EE_VDD", x: 10, y: 11),               // 2
            Part(gate: "FF_GND", x: 10, y: -11),              // 3
            Part(gate: "GE_LED_DISPLAY_4BIT", x: 32, y: 0),   // 4
        ]
        p += [label("4-Bit Counter", x: 0, y: 18, height: 3),
              label("Clock", x: -3, y: -4),
              label("Count", x: 29, y: -5)]
        var w = [Wire(from: 0, fromPin: "CLK", to: 1, toPin: "clock"),
                 Wire(from: 2, fromPin: "OUT_0", to: 1, toPin: "count_enable"),
                 Wire(from: 2, fromPin: "OUT_0", to: 1, toPin: "count_up"),
                 Wire(from: 3, fromPin: "OUT_0", to: 1, toPin: "load"),
                 Wire(from: 3, fromPin: "OUT_0", to: 1, toPin: "clear")]
        for i in 0..<4 { w.append(Wire(from: 1, fromPin: "OUT_\(i)", to: 4, toPin: "IN_\(i)")) }
        return build(p, w, big: ["4-Bit Counter": 3])
    }

    private static func sevenSegment() -> String {
        var p: [Part] = []
        var w: [Wire] = []
        // Switches D (top, most significant) down to A.
        let names = ["D", "C", "B", "A"]
        for (i, n) in names.enumerated() {
            let y = 6.0 - Double(i) * 4
            p.append(Part(gate: "AA_TOGGLE", x: 0, y: y))
            p.append(label(n, x: -4, y: y))
        }
        let display = p.count
        p.append(Part(gate: "GE_LED_DISPLAY_4BIT", x: 14, y: 16))
        for (i, _) in names.enumerated() {
            w.append(Wire(from: i * 2, fromPin: "OUT_0", to: display, toPin: "IN_\(3 - i)"))
        }
        // The segments, laid out as the digit: a on top, g in the middle.
        let segments: [(String, Double, Double)] = [("a", 70, 12), ("b", 75, 7), ("c", 75, -3), ("d", 70, -8),
                                                     ("e", 65, -3), ("f", 65, 7), ("g", 70, 2)]
        for (n, x, y) in segments {
            p.append(Part(gate: "GA_LED", x: x, y: y))
            p.append(label(n, x: x + 1.6, y: y + 1.6))
        }
        p += [label("7-Segment Decoder", x: 0, y: 26, height: 3),
              label("Hex value", x: 10, y: 9),
              label("Build your decoder here", x: 26, y: -12),
              label("Segments", x: 64, y: 17)]
        return build(p, w, big: ["7-Segment Decoder": 3])
    }

    // MARK: Flip-flops and latches

    private static func flipFlops() -> [CircuitTemplate] {
        func t(_ id: String, _ name: String, _ detail: String, _ text: String) -> CircuitTemplate {
            CircuitTemplate(id: "builtin-ff-" + id, name: name, detail: detail, text: text, folder: nil, group: "Flip-Flops and Latches")
        }
        // A clocked one: manual (Step Clock) as given, running as the choice.
        func t(_ id: String, _ name: String, _ detail: String, _ make: (Bool) -> String) -> CircuitTemplate {
            CircuitTemplate(id: "builtin-ff-" + id, name: name, detail: detail, text: make(true), folder: nil,
                            group: "Flip-Flops and Latches", runningText: make(false))
        }
        return [
            t("d", "D Flip-Flop", "Q takes D on each rising clock edge; PRE' and CLR' set or clear it at any time",
              { flipFlop("AE_DFF_LOW", title: "D Flip-Flop", inputs: [("D", "IN_0", 2)], clockY: -1, q: ("OUT_0", 2), nq: ("OUTINV_0", -1), manual: $0) }),
            t("d-nt", "D Flip-Flop, Falling Edge", "The same, triggered as the clock falls from 1 to 0",
              { flipFlop("AE_DFF_LOW_NT", title: "D Flip-Flop, Falling Edge", inputs: [("D", "IN_0", 2)], clockY: -1, q: ("OUT_0", 2), nq: ("OUTINV_0", -1), manual: $0) }),
            t("d-ce", "D Flip-Flop with Clock Enable", "Q takes D on a rising edge only while CE is 1",
              { flipFlop("AF_DFF_LOW", title: "D Flip-Flop with Clock Enable", inputs: [("D", "IN_0", 2), ("CE", "clock_enable", -2)], clockY: 0,
                       q: ("OUT_0", 2), nq: ("OUTINV_0", -1), manual: $0) }),
            t("jk", "J-K Flip-Flop", "On each rising edge: J sets, K resets, both toggle, neither holds; PRE' and CLR' act at once",
              { flipFlop("BE_JKFF_LOW", title: "J-K Flip-Flop", inputs: [("J", "J", 2), ("K", "K", -2)], clockY: 0, q: ("Q", 2), nq: ("nQ", -2), manual: $0) }),
            t("jk-nt", "J-K Flip-Flop, Falling Edge", "The same, triggered as the clock falls from 1 to 0",
              { flipFlop("BE_JKFF_LOW_NT", title: "J-K Flip-Flop, Falling Edge", inputs: [("J", "J", 2), ("K", "K", -2)], clockY: 0, q: ("Q", 2), nq: ("nQ", -2), manual: $0) }),
            t("t", "T Flip-Flop", "A J-K flip-flop with J and K tied together: while T is 1, Q flips on every rising edge",
              { flipFlop("BE_JKFF_LOW", title: "T Flip-Flop", inputs: [("T", "J", 2)], tied: "K", clockY: 0, q: ("Q", 2), nq: ("nQ", -2), manual: $0) }),
            t("sr", "SR Latch", "Two NOR gates holding one bit: S sets it, R resets it, no clock",
              srLatch()),
            t("gated-d", "Gated D Latch", "Four NAND gates and an inverter: Q follows D while EN is 1 and holds when it's 0",
              gatedDLatch()),
        ]
    }

    /// A flip-flop from the library at (24, 0) with a switch for each data
    /// input (left), a clock (`manual`: it moves only on Step Clock), PRE'
    /// and CLR' switches (above and below, on, so the flip-flop runs) and
    /// lights on Q and Q'. `inputs` are (name, pin, the pin's height); `tied`
    /// is a second pin the first switch also drives.
    private static func flipFlop(_ gate: String, title: String, inputs: [(String, String, Double)], tied: String? = nil,
                                 clockY: Double, q: (String, Double), nq: (String, Double), manual: Bool) -> String {
        let fx = 24.0
        var p: [Part] = [Part(gate: gate, x: fx, y: 0),
                         Part(gate: "BB_CLOCK", x: 4, y: clockY),
                         Part(gate: "AA_TOGGLE", x: 6, y: 14, on: true),
                         Part(gate: "AA_TOGGLE", x: 6, y: -14, on: true),
                         Part(gate: "GA_LED", x: 34, y: q.1),
                         Part(gate: "GA_LED", x: 34, y: nq.1)]
        var w = [Wire(from: 1, fromPin: "CLK", to: 0, toPin: "clock"),
                 Wire(from: 2, fromPin: "OUT_0", to: 0, toPin: "set"),
                 Wire(from: 3, fromPin: "OUT_0", to: 0, toPin: "clear"),
                 Wire(from: 0, fromPin: q.0, to: 4, toPin: "N_in0"),
                 Wire(from: 0, fromPin: nq.0, to: 5, toPin: "N_in0")]
        p += [label("PRE'", right: 3.6, y: 14), label("CLR'", right: 3.6, y: -14),
              label("Clock", x: 1, y: clockY - 4.2),
              label("Q", x: 36, y: q.1), label("Q'", x: 36, y: nq.1)]
        for (name, pin, y) in inputs {
            let sy = y > 0 ? 8.0 : -8.0
            p.append(Part(gate: "AA_TOGGLE", x: 6, y: sy))
            w.append(Wire(from: p.count - 1, fromPin: "OUT_0", to: 0, toPin: pin))
            if let tied, pin == inputs[0].1 { w.append(Wire(from: p.count - 1, fromPin: "OUT_0", to: 0, toPin: tied)) }
            p.append(label(name, right: 3.6, y: sy))
        }
        // The key named as a key, so it isn't read as the K switch.
        let step = ShortcutStore.shared.combo(.stepClock).map { "The \($0.label) key" } ?? "Simulation \u{25B8} Step Clock"
        let hint = (manual ? "\(step) steps the clock." : "The clock runs by itself.")
            + " Turn PRE' or CLR' off to set or clear Q."
        p += [label(title, x: 0, y: 21, height: 3), label(hint, x: 0, y: -19.5, height: 1.4)]
        return build(p, w, big: [title: 3, hint: 1.4], manualClock: manual)
    }

    // MARK: Registers

    /// Five registers from the library's parts, the same circuits as
    /// CedarLogic Online's Registers examples (the website's
    /// docs/REGISTER-EXAMPLES.md): parts, places, settings and wires as
    /// listed there, each clock moving only on Step Clock (or running, as
    /// the picker's Clock line chooses).
    private static func registers() -> [CircuitTemplate] {
        func t(_ id: String, _ name: String, _ detail: String, _ make: (Bool) -> String) -> CircuitTemplate {
            CircuitTemplate(id: "builtin-reg-" + id, name: name, detail: detail, text: make(true), folder: nil,
                            group: "Registers", runningText: make(false))
        }
        return [
            t("register", "4-Bit Register (Load and Hold)",
              "Load on: each clock stores D3-D0. Load off: it holds. Clear wins over Load", register4),
            t("shift", "4-Bit Shift Register",
              "Shift on: each clock moves the bits left or right, Serial In coming in. Shift off holds; Load and Clear too", shiftRegister),
            t("sipo", "Serial-In, Parallel-Out",
              "Four D flip-flops in a chain: each clock moves every bit one along", serialInParallelOut),
            t("ring", "Ring Counter",
              "A shift register whose last bit comes back to the start: load a 1 and it goes round", ringCounter),
            t("johnson", "Johnson Counter",
              "Four flip-flops with Q3' fed back: 0001, 0011, 0111, 1111, 1110, 1100, 1000, 0000", johnsonCounter),
        ]
    }

    /// The clock every register uses: HALF_CYCLE 20, as the website's.
    private static func regClock(_ x: Double, _ y: Double) -> Part {
        Part(gate: "BB_CLOCK", x: x, y: y, params: ["HALF_CYCLE": "20"])
    }
    /// A named switch (its name a label on its left; turned round, on its right).
    private static func regSwitch(_ name: String, _ x: Double, _ y: Double, on: Bool = false, turned: Bool = false) -> [Part] {
        [Part(gate: "AA_TOGGLE", x: x, y: y, on: on, angle: turned ? 180 : 0),
         turned ? label(name, x: x + 2.2, y: y, height: 1.4) : label(name, right: x - 2.2, y: y, height: 1.4)]
    }
    /// A named light, its name on its right (or under it).
    private static func regLight(_ name: String, _ x: Double, _ y: Double, below: Bool = false) -> [Part] {
        [Part(gate: "GA_LED", x: x, y: y),
         below ? Part(gate: "AA_LABEL", x: x, y: y - 2.6, label: name) : label(name, x: x + 2, y: y, height: 1.4)]
    }
    /// The line of text at the top (TEXT_HEIGHT 1), placed by its middle.
    private static func regTitle(_ text: String, _ x: Double, _ y: Double) -> Part { Part(gate: "AA_LABEL", x: x, y: y, label: text) }

    /// The names of `parts` (labels at 1.4, the title at 1).
    private static func regSizes(_ parts: [Part], title: String) -> [String: Double] {
        var big: [String: Double] = [title: 1]
        for p in parts where p.gate == "AA_LABEL" && p.label != title { if let l = p.label { big[l] = 1.4 } }
        return big
    }

    /// The register's (or flip-flop's) index in `parts`, by what's built where.
    private static func index(_ parts: [Part], _ gate: String, _ x: Double, _ y: Double) -> Int {
        parts.firstIndex { $0.gate == gate && $0.x == x && $0.y == y }!
    }
    private static func indexOfSwitch(_ parts: [Part], _ name: String) -> Int {
        let l = parts.firstIndex { $0.gate == "AA_LABEL" && $0.label == name }!
        return l - 1
    }

    private static func register4(_ manual: Bool) -> String {
        let title = "Load on: Step Clock stores D3-D0. Load off: it holds."
        var p: [Part] = [Part(gate: "AA_REGISTER4", x: 30, y: -20)]
        p += regSwitch("Load", 14, -9, on: true)
        p += regSwitch("D3", 14, -13)
        p += regSwitch("D2", 14, -17, on: true)
        p += regSwitch("D1", 14, -23)
        p += regSwitch("D0", 14, -27, on: true)
        p.append(regClock(20, -31))
        p += regSwitch("Clear", 19, -36)
        for (n, y) in [("Q3", -13.0), ("Q2", -17.0), ("Q1", -23.0), ("Q0", -27.0)] { p += regLight(n, 46, y) }
        p.append(regTitle(title, 30, -3))
        let r = 0, clock = index(p, "BB_CLOCK", 20, -31)
        var w = [Wire(from: indexOfSwitch(p, "Load"), fromPin: "OUT_0", to: r, toPin: "load"),
                 Wire(from: clock, fromPin: "CLK", to: r, toPin: "clock"),
                 Wire(from: indexOfSwitch(p, "Clear"), fromPin: "OUT_0", to: r, toPin: "clear")]
        for b in 0..<4 {
            w.append(Wire(from: indexOfSwitch(p, "D\(b)"), fromPin: "OUT_0", to: r, toPin: "IN_\(b)"))
            w.append(Wire(from: r, fromPin: "OUT_\(b)", to: index(p, "GA_LED", 46, [-27.0, -23, -17, -13][b]), toPin: "N_in0"))
        }
        return build(p, w, big: regSizes(p, title: title), manualClock: manual)
    }

    /// The shift register and its switches, shared with the ring counter.
    private static func shiftRegisterParts(serialIn: Bool, clear: Bool, load: Bool, left: Bool, d: [Bool]) -> [Part] {
        var p: [Part] = [Part(gate: "BA_SHIFT_REGISTER_4", x: 30, y: -20)]
        if clear { p += regSwitch("Clear", 19, -20) }
        p.append(regClock(14, -26))
        if serialIn { p += regSwitch("Serial In", 46, -15, on: true, turned: true) }
        p += regSwitch("Shift", 46, -20, on: true, turned: true)
        p += regSwitch("Load", 46, -25, on: load, turned: true)
        if left { p += regSwitch("Left", 46, -30, on: true, turned: true) }
        for (b, y) in [(3, -12.0), (2, -9.0), (1, -6.0), (0, -3.0)] { p += regSwitch("D\(b)", 22, y, on: d[b]) }
        for (b, x) in [(3, 21.0), (2, 27.0), (1, 33.0), (0, 39.0)] { p += regLight("Q\(b)", x, -32, below: true) }
        return p
    }

    private static func shiftRegisterWires(_ p: [Part]) -> [Wire] {
        let r = 0
        var w = [Wire(from: index(p, "BB_CLOCK", 14, -26), fromPin: "CLK", to: r, toPin: "clock")]
        for (name, pin) in [("Clear", "clear"), ("Serial In", "carry_in"), ("Shift", "shift_enable"), ("Load", "load"), ("Left", "shift_left")]
            where p.contains(where: { $0.label == name }) {
            w.append(Wire(from: indexOfSwitch(p, name), fromPin: "OUT_0", to: r, toPin: pin))
        }
        for (b, x) in [(3, 21.0), (2, 27.0), (1, 33.0), (0, 39.0)] {
            w.append(Wire(from: indexOfSwitch(p, "D\(b)"), fromPin: "OUT_0", to: r, toPin: "IN_\(b)"))
            w.append(Wire(from: r, fromPin: "OUT_\(b)", to: index(p, "GA_LED", x, -32), toPin: "N_in3"))
        }
        return w
    }

    private static func shiftRegister(_ manual: Bool) -> String {
        let title = "Shift on: each Step Clock moves the bits. Shift off: they hold."
        var p = shiftRegisterParts(serialIn: true, clear: true, load: false, left: true, d: [true, false, true, false])
        p.append(regTitle(title, 30, 1))
        return build(p, shiftRegisterWires(p), big: regSizes(p, title: title), manualClock: manual)
    }

    private static func ringCounter(_ manual: Bool) -> String {
        let title = "Step Clock once to load D3-D0, turn Load off, then step: the 1 goes round."
        var p = shiftRegisterParts(serialIn: false, clear: false, load: true, left: false, d: [true, false, false, false])
        p.append(Part(gate: "DE_TO", x: 18, y: -15, angle: 180, params: ["JUNCTION_ID": "Q3"]))
        p.append(Part(gate: "DA_FROM", x: 41, y: -16, angle: 180, params: ["JUNCTION_ID": "Q3"]))
        p.append(regTitle(title, 30, 1))
        var w = shiftRegisterWires(p)
        // The feedback: what shifts out of Q3 comes back in, through a TO and a FROM.
        w.append(Wire(from: 0, fromPin: "carry_out", to: index(p, "DE_TO", 18, -15), toPin: "IN_0"))
        w.append(Wire(from: index(p, "DA_FROM", 41, -16), fromPin: "IN_0", to: 0, toPin: "carry_in"))
        return build(p, w, big: regSizes(p, title: title), manualClock: manual)
    }

    /// Four D flip-flops in a chain with their PRE', CLR' and clock shared,
    /// the first one's D from `first`.
    private static func chain(title: String, firstSwitch: Bool) -> (parts: [Part], wires: [Wire]) {
        var p: [Part] = [24.0, 36, 48, 60].map { Part(gate: "AE_DFF_LOW", x: $0, y: -20) }
        if firstSwitch { p += regSwitch("Serial In", 15, -18, on: true) }
        else { p.append(Part(gate: "DA_FROM", x: 16, y: -18, params: ["JUNCTION_ID": "Q3'"])) }
        p += regSwitch("PRE'", 15, -10, on: true)
        p += regSwitch("CLR'", 11, -29, on: true)
        p.append(regClock(12, -35))
        for (b, x) in [(0, 30.0), (1, 42.0), (2, 54.0), (3, 66.0)] { p += regLight("Q\(b)", x, -12) }
        p.append(regTitle(title, 42, -3))
        let clock = index(p, "BB_CLOCK", 12, -35)
        var w: [Wire] = []
        for i in 0..<3 { w.append(Wire(from: i, fromPin: "OUT_0", to: i + 1, toPin: "IN_0")) }
        w.append(firstSwitch ? Wire(from: indexOfSwitch(p, "Serial In"), fromPin: "OUT_0", to: 0, toPin: "IN_0")
                             : Wire(from: index(p, "DA_FROM", 16, -18), fromPin: "IN_0", to: 0, toPin: "IN_0"))
        for (i, x) in [30.0, 42, 54, 66].enumerated() {
            w.append(Wire(from: indexOfSwitch(p, "PRE'"), fromPin: "OUT_0", to: i, toPin: "set"))
            w.append(Wire(from: indexOfSwitch(p, "CLR'"), fromPin: "OUT_0", to: i, toPin: "clear"))
            w.append(Wire(from: clock, fromPin: "CLK", to: i, toPin: "clock"))
            w.append(Wire(from: i, fromPin: "OUT_0", to: index(p, "GA_LED", x, -12), toPin: "N_in2"))
        }
        return (p, w)
    }

    private static func serialInParallelOut(_ manual: Bool) -> String {
        let title = "Each Step Clock moves every bit one flip-flop to the right."
        let c = chain(title: title, firstSwitch: true)
        return build(c.parts, c.wires, big: regSizes(c.parts, title: title), manualClock: manual)
    }

    private static func johnsonCounter(_ manual: Bool) -> String {
        let title = "Each Step Clock passes Q3' back to the start: 4 ones in, then 4 zeros."
        var c = chain(title: title, firstSwitch: false)
        c.parts.append(Part(gate: "DE_TO", x: 68, y: -21, params: ["JUNCTION_ID": "Q3'"]))
        c.wires.append(Wire(from: 3, fromPin: "OUTINV_0", to: c.parts.count - 1, toPin: "IN_0"))
        return build(c.parts, c.wires, big: regSizes(c.parts, title: title), manualClock: manual)
    }

    // MARK: Classic mistakes

    /// Four small circuits that each show a classic mistake in the
    /// simulator, with a big "What's wrong here?" and a hint that doesn't
    /// give the answer away.
    private static func classicMistakes() -> [CircuitTemplate] {
        func t(_ id: String, _ name: String, _ detail: String, _ text: String) -> CircuitTemplate {
            CircuitTemplate(id: "builtin-mistake-" + id, name: name, detail: detail, text: text, folder: nil, group: mistakesGroup)
        }
        return [
            t("latch", "J-K Latch from Gates", "A J-K latch made from gates. Turn Reset off and EN on, then watch Q and Q'",
              mistakeLatch()),
            t("gated-clock", "Every Other Count", "A counter that should go up on every other press of Step Clock. Press it and watch the count",
              mistakeGatedClock()),
            t("floating", "A and B Light", "A light that should come on when A and B are both on. Try every combination",
              mistakeFloating()),
            t("two-outputs", "Two-Switch Light", "A light that A and B both control. Try every combination",
              mistakeTwoOutputs()),
        ]
    }

    /// The big question and the hint under it, at the top left of a mistake
    /// template (the labels are scaled by `build`).
    private static func mistakeLabels(_ hint: String, x: Double, y: Double) -> (parts: [Part], big: [String: Double]) {
        let question = "What's wrong here?"
        return ([label(question, x: x, y: y, height: 3), label(hint, x: x, y: y - 5, height: 1.4)], [question: 3, hint: 1.4])
    }

    /// A J-K latch from gates: S = J.EN.Q' and R = K.EN.Q feed a pair of
    /// cross-coupled NOR gates. With J = K = 1 and EN on, each change of Q
    /// switches the other gate on, so Q never settles. Reset (an extra input
    /// on Q's NOR gate, and inverted on S's AND gate so Q' holds too) starts
    /// it at Q = 0, since a latch like this has no way to start from
    /// nothing; it starts reset, with J = K = 1 and EN off.
    private static func mistakeLatch() -> String {
        var p: [Part] = [
            Part(gate: "AA_TOGGLE", x: 0, y: 15, on: true),    // 0 Reset
            Part(gate: "AA_TOGGLE", x: 0, y: 8, on: true),     // 1 K
            Part(gate: "AA_TOGGLE", x: 0, y: 0),               // 2 EN
            Part(gate: "AA_TOGGLE", x: 0, y: -8, on: true),    // 3 J
            Part(gate: "AA_AND3", x: 12, y: 8),                // 4 R = K.EN.Q
            Part(gate: "AA_AND4", x: 12, y: -8),               // 5 S = J.EN.Q'.Reset'
            Part(gate: "BE_NOR3", x: 26, y: 8),                // 6 Q
            Part(gate: "BE_NOR2", x: 26, y: -8),               // 7 Q'
            Part(gate: "GA_LED", x: 36, y: 8),                 // 8
            Part(gate: "GA_LED", x: 36, y: -8),                // 9
            Part(gate: "AA_INVERTER", x: 6, y: -15),           // 10 Reset'
        ]
        p += [label("Reset", right: -2.4, y: 15), label("K", right: -2.4, y: 8),
              label("EN", right: -2.4, y: 0), label("J", right: -2.4, y: -8),
              label("Q", x: 38, y: 8), label("Q'", x: 38, y: -8)]
        let w = [Wire(from: 1, fromPin: "OUT_0", to: 4, toPin: "IN_0"),
                 Wire(from: 2, fromPin: "OUT_0", to: 4, toPin: "IN_1"),
                 Wire(from: 6, fromPin: "OUT", to: 4, toPin: "IN_2"),
                 Wire(from: 3, fromPin: "OUT_0", to: 5, toPin: "IN_0"),
                 Wire(from: 2, fromPin: "OUT_0", to: 5, toPin: "IN_1"),
                 Wire(from: 7, fromPin: "OUT", to: 5, toPin: "IN_2"),
                 Wire(from: 0, fromPin: "OUT_0", to: 10, toPin: "IN_0"),
                 Wire(from: 10, fromPin: "OUT_0", to: 5, toPin: "IN_3"),
                 Wire(from: 0, fromPin: "OUT_0", to: 6, toPin: "IN_0"),
                 Wire(from: 4, fromPin: "OUT", to: 6, toPin: "IN_1"),
                 Wire(from: 7, fromPin: "OUT", to: 6, toPin: "IN_2"),
                 Wire(from: 5, fromPin: "OUT", to: 7, toPin: "IN_0"),
                 Wire(from: 6, fromPin: "OUT", to: 7, toPin: "IN_1"),
                 Wire(from: 6, fromPin: "OUT", to: 8, toPin: "N_in0"),
                 Wire(from: 7, fromPin: "OUT", to: 9, toPin: "N_in0")]
        let (labels, big) = mistakeLabels("Turn Reset off, then switch EN on. Watch Q and Q'.", x: -4, y: 26)
        return build(p + labels, w, big: big)
    }

    /// A toggle flip-flop (J = K = 1) makes Enable alternate 1, 0, 1, 0 right
    /// after each rising edge, while the clock is still high, and an AND
    /// gate passes the clock to the counter only while Enable is 1. When
    /// Enable is 0 at the edge and turns on a moment later, the AND gate's
    /// output rises: an extra edge, so the counter counts on every press
    /// instead of every other one.
    private static func mistakeGatedClock() -> String {
        var p: [Part] = [
            Part(gate: "BB_CLOCK", x: 0, y: 0),                // 0
            Part(gate: "BE_JKFF_LOW", x: 16, y: 0),            // 1 toggles
            Part(gate: "EE_VDD", x: 12, y: 11),                // 2 J and PRE'
            Part(gate: "EE_VDD", x: 12, y: -11),               // 3 K and CLR'
            Part(gate: "AA_AND2", x: 32, y: 0),                // 4 the gated clock
            Part(gate: "AA_REGISTER4", x: 48, y: 0),           // 5 the counter
            Part(gate: "EE_VDD", x: 42, y: 11),                // 6
            Part(gate: "FF_GND", x: 42, y: -11),               // 7
            Part(gate: "GE_LED_DISPLAY_4BIT", x: 64, y: 0),    // 8
            Part(gate: "GA_LED", x: 24, y: 8),                 // 9 Enable
        ]
        p += [label("Clock", x: 1, y: -4.2), label("Enable", x: 20.4, y: 11), label("Count", x: 61, y: -5)]
        let w = [Wire(from: 0, fromPin: "CLK", to: 1, toPin: "clock"),
                 Wire(from: 2, fromPin: "OUT_0", to: 1, toPin: "J"),
                 Wire(from: 2, fromPin: "OUT_0", to: 1, toPin: "set"),
                 Wire(from: 3, fromPin: "OUT_0", to: 1, toPin: "K"),
                 Wire(from: 3, fromPin: "OUT_0", to: 1, toPin: "clear"),
                 Wire(from: 1, fromPin: "Q", to: 4, toPin: "IN_0"),
                 Wire(from: 0, fromPin: "CLK", to: 4, toPin: "IN_1"),
                 Wire(from: 1, fromPin: "Q", to: 9, toPin: "N_in0"),
                 Wire(from: 4, fromPin: "OUT", to: 5, toPin: "clock"),
                 Wire(from: 6, fromPin: "OUT_0", to: 5, toPin: "count_enable"),
                 Wire(from: 6, fromPin: "OUT_0", to: 5, toPin: "count_up"),
                 Wire(from: 7, fromPin: "OUT_0", to: 5, toPin: "load"),
                 Wire(from: 7, fromPin: "OUT_0", to: 5, toPin: "clear")]
        var wires = w
        for i in 0..<4 { wires.append(Wire(from: 5, fromPin: "OUT_\(i)", to: 8, toPin: "IN_\(i)")) }
        // The key named as a key, as on the flip-flops (it can be changed in
        // Settings > Shortcuts, or have none).
        let press = ShortcutStore.shared.combo(.stepClock).map { "Press Step Clock (the \($0.label) key)" } ?? "Choose Simulation \u{25B8} Step Clock"
        let (labels, big) = mistakeLabels("Enable should let the count go up only on every other press. \(press) eight times.", x: -4, y: 24)
        return build(p + labels, wires, big: big, manualClock: true)
    }

    /// A 3-input AND gate with one input left unwired. While A or B is 0 the
    /// output is 0 whatever the open input is; with both on it is unknown.
    private static func mistakeFloating() -> String {
        var p: [Part] = [
            Part(gate: "AA_TOGGLE", x: 0, y: 4, on: true),     // 0 A
            Part(gate: "AA_TOGGLE", x: 0, y: -4, on: true),    // 1 B
            Part(gate: "AA_AND3", x: 14, y: 0),                // 2
            Part(gate: "GA_LED", x: 26, y: 0),                 // 3
        ]
        p += [label("A", right: -2.4, y: 4), label("B", right: -2.4, y: -4), label("Light", x: 24.2, y: 4)]
        let w = [Wire(from: 0, fromPin: "OUT_0", to: 2, toPin: "IN_0"),
                 Wire(from: 1, fromPin: "OUT_0", to: 2, toPin: "IN_1"),
                 Wire(from: 2, fromPin: "OUT", to: 3, toPin: "N_in0")]
        let (labels, big) = mistakeLabels("Try every combination of A and B. Does the light always make sense?", x: -4, y: 14)
        return build(p + labels, w, big: big)
    }

    /// An AND gate and an OR gate, both fed by A and B, both wired to one
    /// light. Where they differ (A and B different) the wire is in conflict.
    private static func mistakeTwoOutputs() -> String {
        var p: [Part] = [
            Part(gate: "AA_TOGGLE", x: 0, y: 6, on: true),     // 0 A
            Part(gate: "AA_TOGGLE", x: 0, y: -6),              // 1 B
            Part(gate: "AA_AND2", x: 14, y: 6),                // 2
            Part(gate: "AE_OR2", x: 14, y: -6),                // 3
            Part(gate: "GA_LED", x: 28, y: 0),                 // 4
        ]
        p += [label("A", right: -2.4, y: 6), label("B", right: -2.4, y: -6), label("Light", x: 26.2, y: 4)]
        let w = [Wire(from: 0, fromPin: "OUT_0", to: 2, toPin: "IN_0"),
                 Wire(from: 1, fromPin: "OUT_0", to: 2, toPin: "IN_1"),
                 Wire(from: 0, fromPin: "OUT_0", to: 3, toPin: "IN_0"),
                 Wire(from: 1, fromPin: "OUT_0", to: 3, toPin: "IN_1"),
                 Wire(from: 2, fromPin: "OUT", to: 4, toPin: "N_in0"),
                 Wire(from: 3, fromPin: "OUT", to: 4, toPin: "N_in0")]
        let (labels, big) = mistakeLabels("Watch the light as you change A and B.", x: -4, y: 16)
        return build(p + labels, w, big: big)
    }

    private static func srLatch() -> String {
        let p: [Part] = [
            Part(gate: "AA_TOGGLE", x: 0, y: 6, on: true),   // 0 R (on: it starts reset)
            Part(gate: "AA_TOGGLE", x: 0, y: -6),            // 1 S
            Part(gate: "BE_NOR2", x: 16, y: 5),              // 2 Q
            Part(gate: "BE_NOR2", x: 16, y: -5),             // 3 Q'
            Part(gate: "GA_LED", x: 26, y: 5),               // 4
            Part(gate: "GA_LED", x: 26, y: -5),              // 5
            label("R", right: -2.4, y: 6), label("S", right: -2.4, y: -6),
            label("Q", x: 28, y: 5), label("Q'", x: 28, y: -5),
            label("SR Latch", x: -4, y: 14, height: 3),
            label("S = 1 sets Q, R = 1 resets it, both 0 holds. Both 1 isn't allowed.", x: -4, y: -12, height: 1.4),
        ]
        let w = [Wire(from: 0, fromPin: "OUT_0", to: 2, toPin: "IN_0"),
                 Wire(from: 3, fromPin: "OUT", to: 2, toPin: "IN_1"),
                 Wire(from: 2, fromPin: "OUT", to: 3, toPin: "IN_0"),
                 Wire(from: 1, fromPin: "OUT_0", to: 3, toPin: "IN_1"),
                 Wire(from: 2, fromPin: "OUT", to: 4, toPin: "N_in0"),
                 Wire(from: 3, fromPin: "OUT", to: 5, toPin: "N_in0")]
        return build(p, w, big: ["SR Latch": 3, "S = 1 sets Q, R = 1 resets it, both 0 holds. Both 1 isn't allowed.": 1.4])
    }

    private static func gatedDLatch() -> String {
        let p: [Part] = [
            Part(gate: "AA_TOGGLE", x: 0, y: 7),             // 0 D
            Part(gate: "AA_TOGGLE", x: 0, y: -1, on: true),  // 1 EN
            Part(gate: "BA_NAND2", x: 15, y: 6),             // 2 S'
            Part(gate: "AA_INVERTER", x: 10, y: -9),         // 3 D'
            Part(gate: "BA_NAND2", x: 19, y: -6),            // 4 R'
            Part(gate: "BA_NAND2", x: 30, y: 4),             // 5 Q
            Part(gate: "BA_NAND2", x: 30, y: -4),            // 6 Q'
            Part(gate: "GA_LED", x: 40, y: 4),               // 7
            Part(gate: "GA_LED", x: 40, y: -4),              // 8
            label("D", right: -2.4, y: 7), label("EN", right: -2.4, y: -1),
            label("Q", x: 42, y: 4), label("Q'", x: 42, y: -4),
            label("Gated D Latch", x: -4, y: 15, height: 3),
            label("While EN is 1, Q follows D. Turn EN off and Q keeps its last value.", x: -4, y: -15, height: 1.4),
        ]
        let w = [Wire(from: 0, fromPin: "OUT_0", to: 2, toPin: "IN_0"),
                 Wire(from: 1, fromPin: "OUT_0", to: 2, toPin: "IN_1"),
                 Wire(from: 0, fromPin: "OUT_0", to: 3, toPin: "IN_0"),
                 Wire(from: 1, fromPin: "OUT_0", to: 4, toPin: "IN_0"),
                 Wire(from: 3, fromPin: "OUT_0", to: 4, toPin: "IN_1"),
                 Wire(from: 2, fromPin: "OUT", to: 5, toPin: "IN_0"),
                 Wire(from: 6, fromPin: "OUT", to: 5, toPin: "IN_1"),
                 Wire(from: 5, fromPin: "OUT", to: 6, toPin: "IN_0"),
                 Wire(from: 4, fromPin: "OUT", to: 6, toPin: "IN_1"),
                 Wire(from: 5, fromPin: "OUT", to: 7, toPin: "N_in0"),
                 Wire(from: 6, fromPin: "OUT", to: 8, toPin: "N_in0")]
        return build(p, w, big: ["Gated D Latch": 3, "While EN is 1, Q follows D. Turn EN off and Q keeps its last value.": 1.4])
    }
}

// MARK: - The picker

struct TemplatePicker: View {
    @Environment(\.dismiss) private var dismiss
    @ObservedObject private var prefs = Prefs.shared
    @State private var mine: [CircuitTemplate] = []
    @State private var selection: String
    /// The flip-flops' Clock line: manual (Step Clock) unless you pick running.
    @AppStorage("cl.templateClockRunning") private var runningClock = false

    init(selection: String? = nil) {
        _selection = State(initialValue: selection ?? Templates.builtIn[0].id)
    }

    private var dark: Bool { prefs.dark }
    private var paper: Color { dark ? CLChrome.rgb(28, 31, 37) : CLChrome.rgb(250, 250, 252) }
    private var ink: Color { dark ? CLChrome.rgb(226, 230, 238) : CLChrome.rgb(30, 33, 40) }
    private var all: [CircuitTemplate] { Templates.builtIn + mine }
    private var selected: CircuitTemplate? { all.first { $0.id == selection } }
    /// The selected one, with the clock the Clock line picks.
    private var chosen: CircuitTemplate? { selected.map { runningClock ? $0.running : $0 } }

    var body: some View {
        let accent = prefs.accentColor(dark: dark)
        VStack(alignment: .leading, spacing: 0) {
            Text("New from Template").font(.system(size: 19, weight: .bold)).foregroundStyle(ink)
                .padding(.horizontal, 22).padding(.top, 22)
            Text("Start a circuit from one of these. Save your own with File \u{25B8} Save as Template\u{2026}")
                .font(.system(size: 13)).foregroundStyle(ink.opacity(0.55))
                .padding(.horizontal, 22).padding(.top, 8)
            HStack(spacing: 14) {
                ScrollView {
                    VStack(alignment: .leading, spacing: 2) {
                        ForEach(Templates.builtInGroups, id: \.name) { group in
                            heading(group.name)
                            ForEach(group.list) { row($0, accent: accent) }
                        }
                        heading("Yours")
                        if mine.isEmpty {
                            Text("None yet.").font(.system(size: 12)).foregroundStyle(ink.opacity(0.5))
                                .padding(.horizontal, 14).padding(.vertical, 6)
                        }
                        ForEach(mine) { row($0, accent: accent) }
                    }
                    .padding(.vertical, 6)
                }
                .frame(width: 320)
                TemplatePreview(text: chosen?.text, dark: dark, ink: ink)
            }
            .padding(14)
            HStack(spacing: 8) {
                if let t = selected, t.folder != nil {
                    Button("Rename\u{2026}") { rename(t) }
                    Button("Delete\u{2026}") { delete(t) }
                }
                if selected?.runningText != nil {
                    Text("Clock").font(.system(size: 13, weight: .semibold)).foregroundStyle(ink)
                    Picker("Clock", selection: $runningClock) {
                        Text("Manual").tag(false)
                        Text("Running").tag(true)
                    }
                    .pickerStyle(.segmented).labelsHidden().fixedSize()
                    .help(clockHelp)
                    Text(runningClock ? "It ticks by itself." : "It moves only on Step Clock (\(stepClockName)).")
                        .font(.system(size: 12)).foregroundStyle(ink.opacity(0.55))
                }
                Spacer()
                Button("Cancel") { dismiss() }.keyboardShortcut(.cancelAction)
                Button("Use Template", action: useSelected).keyboardShortcut(.defaultAction).disabled(selected == nil)
            }
            .padding(.horizontal, 22).padding(.bottom, 22)
        }
        .frame(minWidth: 760, idealWidth: 940, minHeight: 460, idealHeight: 620)
        .background(paper)
        .background(OverFrontCircuit())
        .preferredColorScheme(dark ? .dark : .light)
        .onAppear(perform: reload)
        .onReceive(NotificationCenter.default.publisher(for: NSWindow.didBecomeKeyNotification)) { _ in reload() }
    }

    private func rename(_ t: CircuitTemplate) {
        guard let name = MyParts.askName(title: "Rename Template", message: "", initial: t.name, button: "Rename") else { return }
        Templates.rename(t, to: name)
        reload()
    }

    private func delete(_ t: CircuitTemplate) {
        let alert = NSAlert()
        alert.messageText = "Delete \u{201C}\(t.name)\u{201D}?"
        alert.informativeText = "It goes to the Trash. Circuits you made from it aren't touched."
        alert.addButton(withTitle: "Delete")
        alert.addButton(withTitle: "Cancel")
        alert.buttons.first?.hasDestructiveAction = true
        Library.present(alert) { ok in
            guard ok else { return }
            Templates.delete(t)
            reload()
            selection = Templates.builtIn[0].id
        }
    }

    private func reload() {
        mine = Templates.yours()
        if selected == nil { selection = Templates.builtIn[0].id }
    }

    private var stepClockName: String { ShortcutStore.shared.combo(.stepClock).map { "the \($0.label) key" } ?? "Simulation \u{25B8} Step Clock" }
    private var clockHelp: String {
        "Manual: the clock holds still, and each Step Clock (\(stepClockName)) makes one full cycle. Running: it ticks by itself."
    }

    private func useSelected() {
        guard let t = chosen else { return }
        dismiss()
        DispatchQueue.main.async { Templates.use(t) }
    }

    private func heading(_ s: String) -> some View {
        Text(s.uppercased()).font(.system(size: 10.5, weight: .semibold)).tracking(0.6)
            .foregroundStyle(ink.opacity(0.45))
            .padding(.horizontal, 14).padding(.top, 10).padding(.bottom, 4)
    }

    private func row(_ t: CircuitTemplate, accent: Color) -> some View {
        let sel = t.id == selection
        return VStack(alignment: .leading, spacing: 3) {
            Text(t.name).font(.system(size: 13, weight: .bold)).foregroundStyle(ink).lineLimit(1)
            Text(t.detail).font(.system(size: 11)).foregroundStyle(ink.opacity(0.55)).lineLimit(2)
        }
        .padding(.horizontal, 14).padding(.vertical, 9)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(RoundedRectangle(cornerRadius: 10).fill(sel ? accent.opacity(dark ? 0.26 : 0.16) : .clear))
        .contentShape(Rectangle())
        .contextMenu {
            if t.folder != nil {
                Button("Rename\u{2026}") { rename(t) }
                Button("Delete\u{2026}") { delete(t) }
            }
        }
        .onTapGesture(count: 2) { selection = t.id; useSelected() }
        .onTapGesture { selection = t.id }
        .padding(.horizontal, 6)
    }
}

/// A template drawn fitted, on paper.
private struct TemplatePreview: View {
    let text: String?
    let dark: Bool
    let ink: Color
    @State private var image: CGImage?
    private static var cache: [String: CGImage] = [:]

    var body: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 12).fill(dark ? CLChrome.rgb(22, 24, 29) : .white)
                .overlay(RoundedRectangle(cornerRadius: 12).strokeBorder(ink.opacity(0.12)))
            if let image {
                Image(decorative: image, scale: 2).resizable().scaledToFit().padding(12)
            } else {
                Text("An empty page").font(.system(size: 12)).foregroundStyle(ink.opacity(0.5))
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .onAppear(perform: render)
        .onChange(of: text) { _, _ in render() }
        .onChange(of: dark) { _, _ in render() }
    }

    private func render() {
        guard let text else { image = nil; return }
        let key = "\(text.hashValue)|\(dark)"
        if let hit = Self.cache[key] { image = hit; return }
        guard let doc = try? CoreDocument(data: Data(text.utf8)) else { image = nil; return }
        let w = 1200, h = 820
        guard let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: 0,
                                  space: CGColorSpace(name: CGColorSpace.sRGB)!,
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return }
        ctx.translateBy(x: 0, y: CGFloat(h))
        ctx.scaleBy(x: 2, y: -2)
        guard cl_document_draw_fitted(doc.handle, 0, ctx, Double(w) / 2, Double(h) / 2, 16, 2,
                                      Int32(dark ? CL_STYLE_DARK : CL_STYLE_LIGHT)) else { image = nil; return }
        image = ctx.makeImage()
        if let image { Self.cache[key] = image }
    }
}
