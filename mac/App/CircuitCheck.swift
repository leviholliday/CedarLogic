// Check my circuit: what the assignment asks for (formulas, a minterm list,
// or a truth table) against the circuit's truth table. The formula reader is
// BooleanAlgebra.swift's; matching names and comparing rows is the core's
// (cl_check_*, shared with the Linux app), so both apps say the same thing.

import Foundation

struct CircuitCheck {
    enum Kind: Int { case formula = 0, table = 1 }
    enum Verdict { case matches, wrong, cannotCheck }
    struct Note: Hashable { let text: String; let kind: Int }   // 0 info, 1 warning, 2 problem
    struct Output { let name: String; let column: Int?; let wrong: Int }
    struct Name: Hashable { let name: String; let isInput: Bool; let column: Int?; let byHand: Bool }

    var verdict: Verdict = .cannotCheck
    var summary = ""
    var notes: [Note] = []
    var outputs: [Output] = []
    /// Per row of the circuit's table, per output: what was asked for
    /// ("0", "1", "-") and whether the circuit gave it ("=", "x", "-").
    var expected: [[Character]] = []
    var result: [[Character]] = []
    var wrongRows: Set<Int> = []
    var names: [Name] = []

    /// What the formula reader made, as the core reads it.
    static func spec(_ parsed: ParsedFormulas) -> String {
        var lines = [(["in"] + parsed.variables).joined(separator: "\t")]
        for f in parsed.functions {
            lines.append("out\t\(f.name)\t" + String(f.values.map { $0 == true ? "1" : $0 == false ? "0" : "-" }))
        }
        return lines.joined(separator: "\n")
    }

    /// Compares; `byHand` maps an asked-for name to a column name of the table.
    init(names tableNames: [String], inputs: Int, rows: [[Character]], sequential: Bool, unsettled: Int,
         kind: Kind, text: String, byHand: [String: String]) {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.isEmpty {
            summary = kind == .formula ? "Type what the assignment asks for, like S = A ^ B ^ Cin."
                                       : "Paste or type the truth table you were given."
            return
        }
        var source = trimmed
        if kind == .formula {
            do { source = Self.spec(try FormulaParser.parse(trimmed)) } catch {
                summary = (error as? FormulaError)?.message ?? "That formula couldn't be read."
                notes = [Note(text: summary, kind: 2)]
                summary = "Can't read that yet."
                return
            }
        }
        guard let tt = cl_tt_new(Int32(inputs), sequential, Int32(unsettled)) else { return }
        defer { cl_tt_free(tt) }
        for n in tableNames { cl_tt_add_name(tt, n) }
        for r in rows { cl_tt_add_row(tt, String(r)) }
        let mapping = byHand.filter { !$0.value.isEmpty }.map { "\($0.key)\t\($0.value)" }.sorted().joined(separator: "\n")
        guard let c = kind == .formula ? cl_check_expected(tt, source, mapping) : cl_check_table(tt, source, mapping) else { return }
        defer { cl_check_free(c) }
        verdict = [0: .matches, 1: .wrong][cl_check_verdict(c)] ?? .cannotCheck
        summary = String(cString: cl_check_summary(c))
        notes = (0..<cl_check_note_count(c)).map { Note(text: String(cString: cl_check_note(c, $0)), kind: Int(cl_check_note_kind(c, $0))) }
        let outs = Int(cl_check_outputs(c))
        outputs = (0..<outs).map { k in
            let col = Int(cl_check_output_column(c, Int32(k)))
            return Output(name: String(cString: cl_check_output_name(c, Int32(k))), column: col < 0 ? nil : col,
                          wrong: Int(cl_check_output_wrong(c, Int32(k))))
        }
        func ch(_ v: CChar) -> Character { Character(UnicodeScalar(UInt8(bitPattern: v))) }
        expected = rows.indices.map { r in (0..<outs).map { ch(cl_check_expected_cell(c, Int32(r), Int32($0))) } }
        result = rows.indices.map { r in (0..<outs).map { ch(cl_check_result(c, Int32(r), Int32($0))) } }
        wrongRows = Set(rows.indices.filter { cl_check_row_wrong(c, Int32($0)) })
        names = (0..<cl_check_name_count(c)).map { i in
            let col = Int(cl_check_name_column(c, i))
            return Name(name: String(cString: cl_check_name(c, i)), isInput: cl_check_name_is_input(c, i),
                        column: col < 0 ? nil : col, byHand: cl_check_name_by_hand(c, i))
        }
    }
}

/// The last thing checked for each circuit, so it's there next time.
enum CheckMemory {
    struct Saved: Codable { var kind: Int; var text: String; var names: [String: String] }
    private static let key = "cl.checks"

    static func load(_ circuit: String?) -> Saved? {
        guard let circuit, let data = UserDefaults.standard.data(forKey: key),
              let all = try? JSONDecoder().decode([String: Saved].self, from: data) else { return nil }
        return all[circuit]
    }

    static func save(_ circuit: String?, _ saved: Saved) {
        guard let circuit else { return }
        var all = (UserDefaults.standard.data(forKey: key)).flatMap { try? JSONDecoder().decode([String: Saved].self, from: $0) } ?? [:]
        all[circuit] = saved
        if all.count > 200, let drop = all.keys.first(where: { $0 != circuit }) { all[drop] = nil }
        if let data = try? JSONEncoder().encode(all) { UserDefaults.standard.set(data, forKey: key) }
    }
}
