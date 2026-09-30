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
