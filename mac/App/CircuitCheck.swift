// Check my circuit: what the assignment asks for against the circuit. The kind
// of answer is read from the text alone (cl_check_key_kind): formulas and a
// minterm list (BooleanAlgebra.swift's reader) or a truth table are compared
// with the circuit's truth table; a count, a state table or a timing table is
// run clock pulse by clock pulse on a copy of the circuit
// (docs/CHECK-SEQUENTIAL.md). Matching names, comparing and every message are
// the core's (cl_check_*, shared with the Linux app and the website), so they
// all say the same thing.

import Foundation

struct CircuitCheck {
    /// What the text was read as (CL_KEY_*).
    enum Kind: Int {
        case empty = 0, formula, table, count, states, timing

        /// "Read as a count".
        var reading: String {
            switch self {
            case .empty: "The assignment gives"
            case .formula: "Read as a formula"
            case .table: "Read as a truth table"
            case .count: "Read as a count"
            case .states: "Read as a state table"
            case .timing: "Read as a timing table"
            }
        }
    }
    enum Verdict { case matches, wrong, cannotCheck }
    struct Note: Hashable { let text: String; let kind: Int }   // 0 info, 1 warning, 2 problem
    struct Output { let name: String; let column: Int?; let wrong: Int }
    /// An asked-for name and the column (a truth table's, or a clocked
    /// check's port) it was matched with.
    struct Name: Hashable { let name: String; let isInput: Bool; let column: Int?; let byHand: Bool }

    var kind = Kind.empty
    /// Checked clock pulse by clock pulse (everything but a formula or a
    /// truth table on its own).
    var clocked = false
    var verdict: Verdict = .cannotCheck
    var error = ""
    var summary = ""
    var notes: [Note] = []
    var names: [Name] = []
    /// What a name's column means: the truth table's columns, or the page's
    /// switches then lights.
    var columns: [String] = []
    var columnIsInput: [Bool] = []

    // Against a truth table.
    var outputs: [Output] = []
    /// Per row of the circuit's table, per output: what was asked for
    /// ("0", "1", "-") and whether the circuit gave it ("=", "x", "-").
    var expected: [[Character]] = []
    var result: [[Character]] = []
    var wrongRows: Set<Int> = []

    // Clock pulse by clock pulse: the strip's rows (signals) and columns (steps).
    enum Role: Int { case input = 0, state, next, output }
    struct Signal: Hashable { let name: String; let role: Role; let port: Int }
    enum StepKind: Int { case start = 0, set, pulse }
    struct Step: Hashable {
        let kind: StepKind
        let pulse: Int
        let row: Int
        let state: [Character]
        let inputs: [Character]
        let expected: [Character]
        let got: [Character]
        let wrong: Bool
    }
    var signals: [Signal] = []
    var steps: [Step] = []
    var firstWrong: Int?

    static func kind(of text: String) -> (kind: Kind, options: Bool) {
        var options = false
        let k = Kind(rawValue: Int(cl_check_key_kind(text, &options))) ?? .formula
        return (k, options)
    }

    /// What the formula reader made, as the core reads it.
    static func spec(_ parsed: ParsedFormulas) -> String {
        var lines = [(["in"] + parsed.variables).joined(separator: "\t")]
        for f in parsed.functions {
            lines.append("out\t\(f.name)\t" + String(f.values.map { $0 == true ? "1" : $0 == false ? "0" : "-" }))
        }
        return lines.joined(separator: "\n")
    }

    /// A circuit's truth table: its column names (switches first), how many
    /// switches, and its rows.
    struct Table {
        let names: [String]
        let inputs: Int
        let rows: [[Character]]
        var sequential = false
        var unsettled = 0
    }

    /// Checks `text` against a page: a formula or a truth table against
    /// `table` (nil when the page has none, as a counter without switches:
    /// `noTable` says why), anything else clock pulse by clock pulse on the
    /// document. `byHand` maps an asked-for name to a switch's or light's name.
    init(table: Table?, noTable: String = "", document: OpaquePointer?, page: Int, text: String, byHand: [String: String]) {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        let (kind, options) = Self.kind(of: trimmed)
        self.kind = kind
        if (kind == .formula || kind == .table) && !options {
            guard let table else {
                summary = "Can't check that yet: this page has no truth table."
                notes = [Note(text: noTable + " Check it clock pulse by clock pulse instead, with a count, a state table or a timing table.", kind: 2)]
                return
            }
            compare(table, kind: kind, text: trimmed, mapping: Self.mapping(byHand))
        } else {
            clocked = true
            run(document: document, page: page, text: trimmed, mapping: Self.mapping(byHand))
        }
    }

    /// A formula or a truth table, as `kind` says whatever the text looks
    /// like (the formula checks use it).
    init(names: [String], inputs: Int, rows: [[Character]], sequential: Bool, unsettled: Int,
         kind: Kind, text: String, byHand: [String: String]) {
        self.kind = kind
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.isEmpty {
            summary = kind == .formula ? "Type what the assignment asks for, like S = A ^ B ^ Cin."
                                       : "Paste or type the truth table you were given."
            return
        }
        compare(Table(names: names, inputs: inputs, rows: rows, sequential: sequential, unsettled: unsettled),
                kind: kind, text: trimmed, mapping: Self.mapping(byHand))
    }

    private static func mapping(_ byHand: [String: String]) -> String {
        byHand.filter { !$0.value.isEmpty }.map { "\($0.key)\t\($0.value)" }.sorted().joined(separator: "\n")
    }

    private static func ch(_ v: CChar) -> Character { Character(UnicodeScalar(UInt8(bitPattern: v))) }

    private mutating func readCommon(_ c: OpaquePointer) {
        verdict = [0: .matches, 1: .wrong][cl_check_verdict(c)] ?? .cannotCheck
        error = String(cString: cl_check_error(c))
        summary = String(cString: cl_check_summary(c))
        notes = (0..<cl_check_note_count(c)).map { Note(text: String(cString: cl_check_note(c, $0)), kind: Int(cl_check_note_kind(c, $0))) }
        names = (0..<cl_check_name_count(c)).map { i in
            let col = Int(cl_check_name_column(c, i))
            return Name(name: String(cString: cl_check_name(c, i)), isInput: cl_check_name_is_input(c, i),
                        column: col < 0 ? nil : col, byHand: cl_check_name_by_hand(c, i))
        }
    }

    /// Today's check: the circuit's truth table row by row.
    private mutating func compare(_ table: Table, kind: Kind, text: String, mapping: String) {
        var source = text
        if kind == .formula {
            do { source = Self.spec(try FormulaParser.parse(text)) } catch {
                summary = (error as? FormulaError)?.message ?? "That formula couldn't be read."
                notes = [Note(text: summary, kind: 2)]
                summary = "Can't read that yet."
                return
            }
        }
        let rows = table.rows
        guard let tt = cl_tt_new(Int32(table.inputs), table.sequential, Int32(table.unsettled)) else { return }
        defer { cl_tt_free(tt) }
        for n in table.names { cl_tt_add_name(tt, n) }
        for r in rows { cl_tt_add_row(tt, String(r)) }
        guard let c = kind == .formula ? cl_check_expected(tt, source, mapping) : cl_check_table(tt, source, mapping) else { return }
        defer { cl_check_free(c) }
        readCommon(c)
        columns = table.names
        columnIsInput = table.names.indices.map { $0 < table.inputs }
        let outs = Int(cl_check_outputs(c))
        outputs = (0..<outs).map { k in
            let col = Int(cl_check_output_column(c, Int32(k)))
            return Output(name: String(cString: cl_check_output_name(c, Int32(k))), column: col < 0 ? nil : col,
                          wrong: Int(cl_check_output_wrong(c, Int32(k))))
        }
        expected = rows.indices.map { r in (0..<outs).map { Self.ch(cl_check_expected_cell(c, Int32(r), Int32($0))) } }
        result = rows.indices.map { r in (0..<outs).map { Self.ch(cl_check_result(c, Int32(r), Int32($0))) } }
        wrongRows = Set(rows.indices.filter { cl_check_row_wrong(c, Int32($0)) })
    }

    /// A count, a state table or a timing table, on a copy of the circuit.
    private mutating func run(document: OpaquePointer?, page: Int, text: String, mapping: String) {
        guard let c = cl_check_clocked(document, Int32(page), text, mapping) else { return }
        defer { cl_check_free(c) }
        readCommon(c)
        let ports = Int(cl_check_port_count(c))
        columns = (0..<ports).map { String(cString: cl_check_port_name(c, Int32($0))) }
        columnIsInput = (0..<ports).map { cl_check_port_is_input(c, Int32($0)) }
        signals = (0..<cl_check_signal_count(c)).map { i in
            Signal(name: String(cString: cl_check_signal_name(c, i)), role: Role(rawValue: Int(cl_check_signal_role(c, i))) ?? .output,
                   port: Int(cl_check_signal_port(c, i)))
        }
        func chars(_ i: Int32, _ what: Int32) -> [Character] { Array(String(cString: cl_check_step_text(c, i, what))) }
        steps = (0..<cl_check_step_count(c)).map { i in
            Step(kind: StepKind(rawValue: Int(cl_check_step_kind(c, i))) ?? .pulse, pulse: Int(cl_check_step_pulse(c, i)),
                 row: Int(cl_check_step_row(c, i)), state: chars(i, Int32(CL_STEP_STATE_BITS)), inputs: chars(i, Int32(CL_STEP_INPUTS)),
                 expected: chars(i, Int32(CL_STEP_EXPECTED)), got: chars(i, Int32(CL_STEP_GOT)), wrong: cl_check_step_wrong(c, i))
        }
        let w = Int(cl_check_first_wrong(c))
        firstWrong = w < 0 ? nil : w
    }

    // MARK: The step strip

    /// The signals of a role, in order, with their place in the strip.
    func signals(_ role: Role) -> [Int] { signals.indices.filter { signals[$0].role == role } }

    /// A signal's cells in a step: what was asked for (nil when the step asks
    /// nothing of it) and what the circuit showed. Inputs and the state have
    /// no asked-for value: just what they were.
    func cell(_ step: Step, signal i: Int) -> (asked: Character?, got: Character?) {
        let s = signals[i]
        func at(_ chars: [Character], _ k: Int) -> Character? { k >= 0 && k < chars.count ? chars[k] : nil }
        switch s.role {
        case .input: return (nil, at(step.inputs, signals(.input).firstIndex(of: i) ?? -1))
        case .state: return (nil, at(step.state, signals(.state).firstIndex(of: i) ?? -1))
        case .next, .output:
            let k = (signals(.next) + signals(.output)).firstIndex(of: i) ?? -1
            return (at(step.expected, k), at(step.got, k))
        }
    }

    /// A count's number, from its bits: nil unless every bit is 0 or 1.
    static func number(_ bits: [Character]) -> Int? {
        guard !bits.isEmpty, bits.allSatisfy({ $0 == "0" || $0 == "1" }) else { return nil }
        return bits.reduce(0) { $0 * 2 + ($1 == "1" ? 1 : 0) }
    }

    /// What a step was: "Start", "Pulse 3", "Set".
    static func title(_ s: Step) -> String {
        switch s.kind {
        case .start: "Start"
        case .set: "Set"
        case .pulse: "Pulse \(s.pulse)"
        }
    }

    /// A step in words, for the line under the strip and VoiceOver: "Pulse 3
    /// (row 4), in state Q1 Q0 = 01, with X = 1: asked for Q1+ Q0+ Z = 10-,
    /// the lights showed 110. Wrong."
    func describe(_ index: Int) -> String {
        guard steps.indices.contains(index) else { return "" }
        let s = steps[index]
        func names(_ idx: [Int]) -> String { idx.map { signals[$0].name }.joined(separator: " ") }
        func bits(_ cs: [Character]) -> String { String(cs.map { $0 == "-" ? "–" : $0 }) }
        var head: String
        switch s.kind {
        case .start: head = index == 0 ? "The start" : "Back to the start"
        case .set: head = "The state set with switches"
        case .pulse: head = "Pulse \(s.pulse)" + (s.row > 0 ? " (row \(s.row))" : "")
        }
        let state = signals(.state), ins = signals(.input), shown = signals(.next) + signals(.output)
        if !state.isEmpty, !s.state.isEmpty { head += ", in state \(names(state)) = \(bits(s.state))" }
        if !ins.isEmpty, !s.inputs.isEmpty { head += ", with \(names(ins)) = \(bits(s.inputs))" }
        guard !s.got.isEmpty else { return head + "." }
        // A count says its numbers too.
        func number(_ cs: [Character]) -> String {
            guard kind == .count, cs.count > 1, let n = Self.number(cs) else { return "" }
            return " (\(n))"
        }
        var parts: [String] = []
        if s.expected.contains(where: { $0 != "-" }) {
            parts.append("asked for \(names(shown)) = \(bits(s.expected))\(number(s.expected))")
            parts.append("the lights showed \(bits(s.got))\(number(s.got))")
        } else {
            parts.append("the lights showed \(names(shown)) = \(bits(s.got))\(number(s.got))")
        }
        return head + ": " + parts.joined(separator: ", ") + (s.wrong ? ". Wrong." : ".")
    }
}

/// The last thing checked for each circuit, so it's there next time. The
/// text says what kind it is when it's read back; `kind` is kept as older
/// versions read it (1 a truth table, 0 anything else).
enum CheckMemory {
    struct Saved: Codable { var kind: Int; var text: String; var names: [String: String] }
    private static let key = "cl.checks"

    static func load(_ circuit: String?) -> Saved? {
        guard let circuit, let data = UserDefaults.standard.data(forKey: key),
              let all = try? JSONDecoder().decode([String: Saved].self, from: data) else { return nil }
        return all[circuit]
    }

    static func save(_ circuit: String?, _ saved: Saved?) {
        guard let circuit else { return }
        var all = (UserDefaults.standard.data(forKey: key)).flatMap { try? JSONDecoder().decode([String: Saved].self, from: $0) } ?? [:]
        all[circuit] = saved
        if all.count > 200, let drop = all.keys.first(where: { $0 != circuit }) { all[drop] = nil }
        if let data = try? JSONEncoder().encode(all) { UserDefaults.standard.set(data, forKey: key) }
    }
}
