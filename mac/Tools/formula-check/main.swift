// Checks for Build from Formula and the truth table's formulas, without the
// app: the simplifier on known answers and random tables, the formula reader,
// and circuits built in the real engine whose truth tables must match.
//   mac/Tools/formula-check.sh        (after mac/build.sh)
import Foundation

var failures = 0
func check(_ ok: Bool, _ what: @autoclosure () -> String) {
    if !ok { failures += 1; print("FAIL: \(what())") }
}

func values(_ n: Int, ones: [Int], dc: [Int] = []) -> [Bool?] {
    (0..<(1 << n)).map { dc.contains($0) ? nil : ones.contains($0) }
}
let abc = ["A", "B", "C"], abcd = ["A", "B", "C", "D"]

// MARK: Known answers
func expect(_ form: TwoLevel.Form, _ n: Int, _ names: [String], ones: [Int], dc: [Int] = [], _ want: String) {
    let got = TwoLevel.simplest(form, n: n, values: values(n, ones: ones, dc: dc)).text(names: names)
    check(got == want, "\(form) of \(ones) dc \(dc): got \(got), want \(want)")
}
expect(.sumOfProducts, 3, abc, ones: [3, 5, 6, 7], "AB + AC + BC")
expect(.productOfSums, 3, abc, ones: [3, 5, 6, 7], "(A + B)(A + C)(B + C)")
expect(.sumOfProducts, 2, ["A", "B"], ones: [1, 2], "AB' + A'B")
expect(.sumOfProducts, 4, abcd, ones: [1, 3, 7, 11, 15], dc: [0, 2, 5], "A'B' + CD")
expect(.sumOfProducts, 3, abc, ones: [], "0")
expect(.sumOfProducts, 3, abc, ones: Array(0..<8), "1")
expect(.sumOfProducts, 4, abcd, ones: [0, 2, 8, 10], "B'D'")          // the four corners
expect(.sumOfProducts, 3, abc, ones: [0, 1, 2, 3], "A'")
expect(.sumOfProducts, 2, ["Cin", "X1"], ones: [3], "Cin·X1")

// MARK: Random tables: the formulas must say exactly the table (don't-cares aside)
func evalTwoLevel(_ t: TwoLevel, _ m: Int, _ n: Int) -> Bool {
    if let c = t.constant { return c }
    func lit(_ l: Literal) -> Bool { ((m >> (n - 1 - l.variable)) & 1 == 1) != l.negated }
    switch t.form {
    case .sumOfProducts: return t.terms.contains { $0.allSatisfy(lit) }
    case .productOfSums: return t.terms.allSatisfy { $0.contains(where: lit) }
    }
}
var rng = SystemRandomNumberGenerator()
for trial in 0..<400 {
    let n = 1 + trial % 6
    let vals: [Bool?] = (0..<(1 << n)).map { _ in
        let r = Int.random(in: 0..<10, using: &rng)
        return r < 1 ? nil : r < 5
    }
    for form in [TwoLevel.Form.sumOfProducts, .productOfSums] {
        let t = TwoLevel.simplest(form, n: n, values: vals)
        for m in vals.indices { if let v = vals[m] { check(evalTwoLevel(t, m, n) == v, "random \(form) n=\(n) m=\(m)") } }
    }
}

// MARK: Reading formulas
func truth(_ s: String) -> [String: [Bool?]]? {
    guard let f = try? FormulaParser.parse(s) else { return nil }
    return Dictionary(uniqueKeysWithValues: f.functions.map { ($0.name, $0.values) })
}
func vars(_ s: String) -> [String] { (try? FormulaParser.parse(s))?.variables ?? [] }
check(truth("F = A'B + AC")?["F"] == values(3, ones: [2, 3, 5, 7]), "F = A'B + AC")
check(truth("A'B + AC")?["F"] == values(3, ones: [2, 3, 5, 7]), "no name")
check(truth("F = (A + B)'C")?["F"] == values(3, ones: [1]), "(A+B)'C")
check(truth("F = NOT A AND B")?["F"] == values(2, ones: [1]), "NOT A AND B")
check(truth("F = ~(A*B)")?["F"] == values(2, ones: [0, 1, 2]), "~(A*B)")
check(truth("F = A ⊕ B")?["F"] == values(2, ones: [1, 2]), "A ⊕ B")
check(truth("F = C + AB")?["F"] == values(3, ones: [1, 3, 5, 6, 7]), "C + AB (alphabetical)")
check(vars("S = A ^ B ^ Cin; Cout = AB + Cin(A ^ B)") == ["A", "B", "Cin"], "full adder names: \(vars("S = A ^ B ^ Cin; Cout = AB + Cin(A ^ B)"))")
check(truth("S = A ^ B ^ Cin; Cout = AB + Cin(A ^ B)")?["Cout"] == values(3, ones: [3, 5, 6, 7]), "full adder carry")
check(truth("F(A,B,C) = Σm(1,3,5) + d(7)")?["F"] == values(3, ones: [1, 3, 5], dc: [7]), "Σm")
check(truth("F(A,B,C) = ΠM(0,2)")?["F"] == values(3, ones: [1, 3, 4, 5, 6, 7]), "ΠM")
check(truth("F(C,B,A) = A")?["F"] == values(3, ones: [1, 3, 5, 7]), "declared order")
check(truth("F = X1 X2' + X3")?["F"] == values(3, ones: [1, 3, 4, 5, 7]), "X1 X2' + X3")
for bad in ["F = A +", "F = (A", "F = A )", "= A", "F = ", "F(A,B) = C", "F = Σm(1,9)", "F(A) = Σm(1,2)"] {
    check((try? FormulaParser.parse(bad)) == nil, "should refuse \"\(bad)\"")
}

// MARK: Check my circuit, on tables made by hand
func tt_spec_only(_ p: ParsedFormulas) -> String { CircuitCheck.spec(p) }
/// A parsed formula as a pasted truth table: "A B | F" then a row per minterm.
func tableText(_ p: ParsedFormulas) -> String {
    let n = p.variables.count
    var lines = [p.variables.joined(separator: " ") + " | " + p.functions.map(\.name).joined(separator: " ")]
    for m in 0..<(1 << n) {
        let ins: [String] = (0..<n).map { (m >> (n - 1 - $0)) & 1 == 1 ? "1" : "0" }
        let outs: [String] = p.functions.map { f -> String in
            guard let v = f.values[m] else { return "X" }
            return v ? "1" : "0"
        }
        lines.append(ins.joined(separator: " ") + " | " + outs.joined(separator: " "))
    }
    return lines.joined(separator: "\n")
}
func table(_ names: [String], _ inputs: Int, _ outs: [(Int) -> Character]) -> [[Character]] {
    (0..<(1 << inputs)).map { m in (0..<inputs).map { (m >> (inputs - 1 - $0)) & 1 == 1 ? "1" : "0" } + outs.map { $0(m) } }
}
func bit(_ b: Bool) -> Character { b ? "1" : "0" }
do {
    // A full adder whose carry is wrong when all three are on.
    let names = ["A", "B", "Cin", "S", "Cout"]
    let rows = table(names, 3, [{ bit($0.nonzeroBitCount % 2 == 1) }, { m in bit(m.nonzeroBitCount >= 2 && m != 7) }])
    func run(_ kind: CircuitCheck.Kind, _ text: String, _ byHand: [String: String] = [:], names: [String] = names, rows r: [[Character]]? = nil,
             sequential: Bool = false) -> CircuitCheck {
        CircuitCheck(names: names, inputs: 3, rows: r ?? rows, sequential: sequential, unsettled: 0, kind: kind, text: text, byHand: byHand)
    }
    let adder = "S = A ^ B ^ Cin; Cout = AB + Cin(A ^ B)"
    var c = run(.formula, adder)
    check(c.verdict == .wrong && c.wrongRows == [7] && c.outputs.map(\.wrong) == [0, 1], "adder: \(c.summary)")
    check(c.summary == "Doesn't match: Cout is wrong on 1 of 8 rows.", "adder summary: \(c.summary)")
    check(c.expected[7] == ["1", "1"] && c.result[7] == ["=", "x"], "adder row 7: \(c.expected[7]) \(c.result[7])")
    c = run(.formula, "S = A ^ B ^ Cin")
    check(c.verdict == .matches && c.notes.contains { $0.text.contains("Light Cout isn't") }, "only S: \(c.summary) \(c.notes)")
    // Don't-cares: the wrong row is a don't-care, so it matches.
    c = run(.formula, "S(A,B,Cin) = Σm(1,2,4,7); Cout(A,B,Cin) = Σm(3,5,6) + d(7)")
    check(c.verdict == .matches && c.summary.contains("1 don't-care"), "don't-care: \(c.summary)")
    // Names: case and spaces don't matter; different names pair up in order, with a note.
    c = run(.formula, "s = a ^ b ^ cin")
    check(c.verdict == .matches, "lower case: \(c.summary)")
    c = run(.formula, "Sum = X ^ Y ^ Z; Carry = XY + Z(X ^ Y)")
    check(c.verdict == .wrong && c.notes.contains { $0.text == "Matched by position, as the names differ: X is A, Y is B, Z is Cin, Sum is S and Carry is Cout." },
          "by position: \(c.summary) \(c.notes)")
    c = run(.formula, "Sum = A ^ B ^ Cin")   // one asked for, two lights: which one isn't clear
    check(c.verdict == .cannotCheck && c.summary == "Can't check yet: there's no light called Sum.", "Sum: \(c.summary)")
    // A switch that isn't there, then picked by hand.
    c = run(.formula, "S = A ^ B ^ Ci; Cout = AB + Ci(A ^ B) + D")
    check(c.verdict == .cannotCheck && c.summary == "Can't check yet: there are no switches called Ci and D.", "missing switches: \(c.summary)")
    c = run(.formula, "S = A ^ Q ^ B")
    check(c.verdict == .matches && c.notes.contains { $0.text.contains("Q is Cin") }, "Q by position: \(c.summary)")
    c = run(.formula, "S = A ^ Q ^ B", ["Q": "Cin"])
    check(c.verdict == .matches && c.names.first { $0.name == "Q" }?.byHand == true, "Q by hand: \(c.summary)")
    // A light that isn't there.
    c = run(.formula, "S = A ^ B ^ Cin; Carry = AB + Cin(A ^ B); Z = A")
    check(c.verdict == .cannotCheck && c.summary == "The rest matches, but there are no lights called Carry and Z.", "missing lights: \(c.summary)")
    // Unknown and floating lights.
    let floating = table(names, 3, [{ m in m == 2 ? "Z" : bit(m.nonzeroBitCount % 2 == 1) }, { m in m < 4 ? "X" : bit(m.nonzeroBitCount >= 2) }])
    c = run(.formula, adder, rows: floating)
    check(c.verdict == .wrong && c.notes.contains { $0.text.contains("shows Z (floating)") } && c.notes.contains { $0.text.contains("On 4 rows the light Cout shows X") },
          "odd values: \(c.summary) \(c.notes)")
    c = run(.formula, adder, sequential: true)
    check(c.notes.contains { $0.text.contains("clocks or flip-flops") }, "sequential note")
    // Formula mistakes come back as they read.
    c = run(.formula, "S = A +")
    check(c.verdict == .cannotCheck && c.notes.first?.kind == 2, "bad formula: \(c.summary)")
    // Pasted tables: with a bar, without one (names tell), as a block of digits, partial, without names.
    c = run(.table, "A B Cin | S Cout\n000 00\n001 10\n010 10\n011 01\n100 10\n101 01\n110 01\n111 11")
    check(c.verdict == .wrong && c.wrongRows == [7], "table: \(c.summary) \(c.notes)")
    c = run(.table, "A\tB\tCin\tS\tCout\n0\t0\t0\t0\t0\n1\t1\t1\t1\tX")
    check(c.verdict == .matches && c.notes.contains { $0.text.contains("leaves out 6 rows") }, "partial table: \(c.summary) \(c.notes)")
    c = run(.table, "0 0 0 0 0\n0 0 1 1 0")
    check(c.verdict == .matches && c.notes.contains { $0.text.contains("no names on top") }, "no header: \(c.summary)")
    c = run(.table, "A B | F\n0 0 | 2")
    check(c.verdict == .cannotCheck && c.summary.contains("\"2\""), "bad value: \(c.summary)")
    c = run(.table, "A B Cin | S\n0 0 0 | 0\n0 0 0 | 1")
    check(c.verdict == .cannotCheck && c.summary.contains("different values"), "clash: \(c.summary)")
    c = run(.table, "A B Cin | S\n0 0 | 0")
    check(c.verdict == .cannotCheck && c.summary.hasPrefix("Row 1 has 3 values"), "short row: \(c.summary)")
}

// MARK: Built in the engine: its truth table must be the formula's
guard cl_library_load(CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "res/cl_gatedefs.xml") else {
    print("FAIL: gate library didn't load"); exit(2)
}
let outDir = CommandLine.arguments.count > 2 ? CommandLine.arguments[2] : nil
var built = 0
let formulas = [
    "F = A'B + AC",
    "S = A ^ B ^ Cin; Cout = AB + Cin(A ^ B)",
    "F(A,B,C,D) = Σm(1,3,7,11,15) + d(0,2,5)",
    "F = (A + B)(A' + C)",
    "F = A",
    "F = A'",
    "F = 1",
    "F = A B C D E",
    "Y = A'B'C' + ABC + A B' D + C D' E",
    "F = (A ⊕ B)'",
    "G = A + B + C + D + E + F1",
    "F = (AB)'",                   // negated products: NAND, built from NORs in NOR-only
    "F = (A'B')' + (ABC)'D",
]
for (fi, text) in formulas.enumerated() {
    let parsed = try! FormulaParser.parse(text)
    for shape in BuildShape.allCases {
        for style in GateStyle.allCases {
            for two in [false, true] {
                let plan = FormulaCircuit.plan(parsed, shape: shape, style: style, twoInputOnly: two)
                let what = "\"\(text)\" \(shape.rawValue) \(style.rawValue)\(two ? " 2-input" : "")"
                guard let doc = cl_document_new() else { check(false, "no document"); continue }
                var strings: [UnsafeMutablePointer<CChar>] = []
                func c(_ s: String) -> UnsafePointer<CChar> { let p = strdup(s)!; strings.append(p); return UnsafePointer(p) }
                let gates = plan.parts.map { CLBuildGate(gate: c($0.gate), x: $0.x, y: $0.y, label: $0.label.map(c)) }
                let wires = plan.wires.map { CLBuildWire(from: Int32($0.from), fromPin: c($0.fromPin), to: Int32($0.to), toPin: c($0.toPin)) }
                let made = cl_edit_build(doc, 0, gates, Int32(gates.count), wires, Int32(wires.count), "Build")
                strings.forEach { free($0) }
                check(made == Int32(plan.parts.count), "\(what): made \(made) of \(plan.parts.count)")
                // Every gate input is wired.
                if style != .any || true {
                    for g in plan.parts.indices where !["AA_TOGGLE", "AA_LABEL", "EE_VDD", "FF_GND"].contains(plan.parts[g].gate) {
                        let want = plan.parts[g].gate == "GA_LED" ? 1 : plan.parts[g].gate == "AA_INVERTER" ? 1 : Int(String(plan.parts[g].gate.last!)) ?? 0
                        let got = plan.wires.filter { $0.to == g }.count
                        check(got == want, "\(what): \(plan.parts[g].gate) has \(got) of \(want) inputs wired")
                    }
                }
                var err = [CChar](repeating: 0, count: 256)
                if parsed.variables.isEmpty {
                    cl_document_close(doc); built += 1; continue   // no switches, no table
                }
                guard let tt = cl_truth_table(doc, 0, &err, Int32(err.count)) else {
                    check(false, "\(what): no truth table: \(String(cString: err))"); cl_document_close(doc); continue
                }
                let cols = Int(cl_tt_columns(tt)), ins = Int(cl_tt_inputs(tt))
                let names = (0..<cols).map { String(cString: cl_tt_name(tt, Int32($0))) }
                check(Array(names.prefix(ins)) == parsed.variables, "\(what): inputs named \(names.prefix(ins)), want \(parsed.variables)")
                for f in parsed.functions {
                    guard let col = names.firstIndex(of: f.name), col >= ins else { check(false, "\(what): no column \(f.name) in \(names)"); continue }
                    for r in 0..<Int(cl_tt_rows(tt)) {
                        guard let want = f.values[r] else { continue }
                        let cell = cl_tt_cell(tt, Int32(r), Int32(col))
                        check(cell == (want ? 49 : 48), "\(what): \(f.name) row \(r) is \(Character(UnicodeScalar(UInt8(cell)))), want \(want ? 1 : 0)")
                    }
                }
                // Check my circuit: the formula itself matches; one row changed doesn't.
                let rows = (0..<Int(cl_tt_rows(tt))).map { r in (0..<cols).map { Character(UnicodeScalar(UInt8(cl_tt_cell(tt, Int32(r), Int32($0))))) } }
                let ok = CircuitCheck(names: names, inputs: ins, rows: rows, sequential: false, unsettled: 0, kind: .formula, text: text, byHand: [:])
                check(ok.verdict == .matches, "\(what): check says \(ok.summary) \(ok.notes.map(\.text))")
                if let r = parsed.functions[0].values.firstIndex(where: { $0 != nil }) {
                    var bad = parsed
                    bad.functions[0].values[r]!.toggle()
                    let wrong = CircuitCheck(names: names, inputs: ins, rows: rows, sequential: false, unsettled: 0, kind: .table,
                                             text: tableText(bad), byHand: [:])
                    check(wrong.verdict == .wrong && wrong.wrongRows == [r] && wrong.outputs[0].wrong == 1,
                          "\(what): one changed row: \(wrong.summary) rows \(wrong.wrongRows)")
                }
                cl_tt_free(tt)
                if let outDir, shape == .asWritten, !two {
                    let path = "\(outDir)/f\(fi)-\(style.rawValue).cdl"
                    try? String(cString: cl_document_save_text(doc)).write(toFile: path, atomically: true, encoding: .utf8)
                }
                cl_document_close(doc)
                built += 1
            }
        }
    }
}
print("built \(built) circuits; \(failures == 0 ? "all checks passed" : "\(failures) failed")")
exit(failures == 0 ? 0 : 1)
