// Formula -> circuit: switches for the variables, gates for the formula, a
// light for each output, laid out left to right in columns by how many gates
// a signal has gone through, and labelled so the truth table finds the names.
// Gates can be any kind, only NAND or only NOR (the usual exercises), with up
// to 4 inputs each or only 2.

import Foundation

enum GateStyle: String, CaseIterable, Identifiable {
    case any, nand, nor
    var id: String { rawValue }
    var name: String {
        switch self {
        case .any: "Any gates"
        case .nand: "NAND only"
        case .nor: "NOR only"
        }
    }
}

enum BuildShape: String, CaseIterable, Identifiable {
    case asWritten, sumOfProducts, productOfSums
    var id: String { rawValue }
    var name: String {
        switch self {
        case .asWritten: "As written"
        case .sumOfProducts: "Simplest sum of products"
        case .productOfSums: "Simplest product of sums"
        }
    }
}

/// What gets built: the gates (with places) and the wires between their pins.
struct CircuitPlan {
    struct Part { var gate: String; var x: Double; var y: Double; var label: String? }
    struct Wire { var from: Int; var fromPin: String; var to: Int; var toPin: String }
    var parts: [Part] = []
    var wires: [Wire] = []
    var logicGates = 0
    var gateCounts: [String: Int] = [:]   // "NAND" -> 5, for the summary

    var summary: String {
        guard logicGates > 0 else { return "No gates needed: just switches and lights." }
        let order = ["AND", "OR", "NOT", "NAND", "NOR", "XOR", "XNOR"]
        let parts = order.compactMap { k in gateCounts[k].map { "\($0) \(k)" } }
        return "\(logicGates) gate\(logicGates == 1 ? "" : "s"): " + parts.joined(separator: ", ")
    }
}

enum FormulaCircuit {
    static func plan(_ formulas: ParsedFormulas, shape: BuildShape, style: GateStyle, twoInputOnly: Bool) -> CircuitPlan {
        let n = formulas.variables.count
        let s = Synth(nVars: n, style: style, maxIn: twoInputOnly ? 2 : 4)
        var outputs: [(name: String, op: Synth.Operand)] = []
        for f in formulas.functions {
            let expr: BoolExpr
            switch (shape, f.expr) {
            case (.asWritten, let e?): expr = e
            case (.productOfSums, _): expr = twoLevelExpr(TwoLevel.simplest(.productOfSums, n: n, values: f.values))
            default: expr = twoLevelExpr(TwoLevel.simplest(.sumOfProducts, n: n, values: f.values))
            }
            outputs.append((f.name, s.compile(expr, negated: false)))
        }
        return layout(s, variables: formulas.variables, outputs: outputs)
    }

    static func twoLevelExpr(_ t: TwoLevel) -> BoolExpr {
        if let c = t.constant { return .constant(c) }
        func lit(_ l: Literal) -> BoolExpr { l.negated ? .not(.variable(l.variable)) : .variable(l.variable) }
        switch t.form {
        case .sumOfProducts:
            let terms = t.terms.map { $0.count == 1 ? lit($0[0]) : .and($0.map(lit)) }
            return terms.count == 1 ? terms[0] : .or(terms)
        case .productOfSums:
            let clauses = t.terms.map { $0.count == 1 ? lit($0[0]) : .or($0.map(lit)) }
            return clauses.count == 1 ? clauses[0] : .and(clauses)
        }
    }

    // MARK: Layout

    private static let column = 16.0       // between gate columns
    private static let gap = 2.0           // between gates in a column

    private static func height(_ inputs: Int) -> Double { inputs <= 1 ? 3 : Double(inputs) * 2 + 1 }

    private static func layout(_ s: Synth, variables: [String], outputs: [(name: String, op: Synth.Operand)]) -> CircuitPlan {
        var plan = CircuitPlan()
        let n = s.nVars
        // Only what the outputs use (a formula can make a gate it then doesn't need).
        var needed = Set<Int>()
        func mark(_ net: Int) {
            guard net >= n, !needed.contains(net) else { return }
            needed.insert(net)
            for i in s.gates[net - n].inputs { mark(i) }
        }
        for o in outputs { if case .net(let x) = o.op { mark(x) } }

        // Columns: a switch is in column 0; a gate one past its latest input.
        var level = [Int](repeating: 0, count: n + s.gates.count)
        for g in s.gates.indices where needed.contains(n + g) {
            level[n + g] = 1 + (s.gates[g].inputs.map { level[$0] }.max() ?? 0)
        }
        let lastLevel = max(1, level.max() ?? 0)

        // Down each column (y goes up, so down is negative): switches in
        // order, then each column's gates near the middle of what feeds them.
        var y = [Double](repeating: 0, count: n + s.gates.count)
        let switchPitch = 6.0
        for v in 0..<n { y[v] = -Double(v) * switchPitch }
        var byLevel: [Int: [Int]] = [:]
        for net in needed { byLevel[level[net], default: []].append(net) }
        for l in (1...lastLevel) {
            guard var nets = byLevel[l] else { continue }
            func want(_ net: Int) -> Double {
                let ins = s.gates[net - n].inputs
                return ins.map { y[$0] }.reduce(0, +) / Double(max(1, ins.count))
            }
            nets.sort { want($0) > want($1) }
            var floor = Double.infinity       // the bottom edge of the gate above
            for net in nets {
                let h = height(s.gates[net - n].inputs.count)
                var at = want(net)
                if at + h / 2 + gap > floor { at = floor - gap - h / 2 }
                y[net] = at
                floor = at - h / 2
            }
        }

        // The parts: switches (with their labels), gates, then lights.
        var index: [Int: Int] = [:]   // net -> part
        func add(_ gate: String, _ x: Double, _ y: Double, label: String? = nil) -> Int {
            plan.parts.append(.init(gate: gate, x: x, y: y, label: label))
            return plan.parts.count - 1
        }
        // Every variable gets its switch, used or not, so the truth table has
        // a column for each.
        for v in 0..<n {
            index[v] = add("AA_TOGGLE", 0, y[v])
            let name = variables[v]
            _ = add("AA_LABEL", -3.5 - Double(name.count) * 0.6, y[v], label: name)
        }
        for net in needed.sorted() {
            let g = s.gates[net - n]
            index[net] = add(g.libraryName, Double(level[net]) * column, y[net])
            plan.logicGates += 1
            plan.gateCounts[g.kind.caption, default: 0] += 1
        }

        // Each gate's inputs: the highest source on the top pin, and so on down.
        for net in needed.sorted() {
            let g = s.gates[net - n]
            let sources = g.inputs.sorted { y[$0] > y[$1] }
            for (pin, src) in sources.enumerated() {
                guard let from = index[src], let to = index[net] else { continue }
                plan.wires.append(.init(from: from, fromPin: Synth.outputPin(src < n ? nil : s.gates[src - n].kind),
                                        to: to, toPin: g.kind == .not ? "IN_0" : "IN_\(pin)"))
            }
        }

        // The lights, in a column past the last gate, beside what drives them.
        let lightX = Double(lastLevel + 1) * column - 6
        // Top to bottom by where their wires come from, so they don't cross.
        func wanted(_ k: Int) -> Double {
            if case .net(let x) = outputs[k].op { return y[x] }
            return -Double(k) * 6
        }
        var floor = Double.infinity
        for k in outputs.indices.sorted(by: { wanted($0) > wanted($1) }) {
            let o = outputs[k]
            var want = wanted(k)
            if want + 3 > floor { want = floor - 3 }
            floor = want - 3
            let light = add("GA_LED", lightX, want)
            _ = add("AA_LABEL", lightX + 3 + Double(o.name.count) * 0.6, want, label: o.name)
            switch o.op {
            case .net(let x):
                if let from = index[x] {
                    plan.wires.append(.init(from: from, fromPin: Synth.outputPin(x < n ? nil : s.gates[x - n].kind),
                                            to: light, toPin: "N_in0"))
                }
            case .constant(let on):
                let power = add(on ? "EE_VDD" : "FF_GND", lightX - 5, want + (on ? 2 : -2))
                plan.wires.append(.init(from: power, fromPin: "OUT_0", to: light, toPin: "N_in0"))
            }
        }
        return plan
    }
}

/// The gates a formula needs, in the style asked for: each gate is made once
/// (the same inputs give the same wire), and NOT of a NOT is the original.
final class Synth {
    enum Kind: String {
        case and, or, nand, nor, xor, xnor, not
        var caption: String { self == .not ? "NOT" : rawValue.uppercased() }
    }
    struct Gate {
        let kind: Kind
        let inputs: [Int]
        var libraryName: String {
            let k = inputs.count
            switch kind {
            case .and: return "AA_AND\(k)"
            case .or: return "AE_OR\(k)"
            case .nand: return "BA_NAND\(k)"
            case .nor: return "BE_NOR\(k)"
            case .xor: return "AI_XOR2"
            case .xnor: return "AO_XNOR2"
            case .not: return "AA_INVERTER"
            }
        }
    }
    enum Operand { case net(Int), constant(Bool) }

    let nVars: Int
    let style: GateStyle
    let maxIn: Int
    private(set) var gates: [Gate] = []
    private var made: [String: Int] = [:]
    private var inverse: [Int: Int] = [:]

    init(nVars: Int, style: GateStyle, maxIn: Int) {
        self.nVars = nVars; self.style = style; self.maxIn = maxIn
    }

    static func outputPin(_ kind: Kind?) -> String {
        guard let kind else { return "OUT_0" }      // a switch
        return kind == .not ? "OUT_0" : "OUT"
    }

    private func make(_ kind: Kind, _ inputs: [Int]) -> Int {
        let ordered = kind == .not ? inputs : inputs.sorted()
        let key = "\(kind.rawValue):" + ordered.map(String.init).joined(separator: ",")
        if let net = made[key] { return net }
        gates.append(Gate(kind: kind, inputs: ordered))
        let net = nVars + gates.count - 1
        made[key] = net
        return net
    }

    // MARK: The gates, whatever style

    func NOT(_ x: Int) -> Int {
        if let y = inverse[x] { return y }
        let y: Int
        switch style {
        case .any: y = make(.not, [x])
        case .nand: y = make(.nand, [x, x])
        case .nor: y = make(.nor, [x, x])
        }
        inverse[x] = y
        inverse[y] = x
        return y
    }

    /// Too many inputs for one gate: combine them in groups first.
    private func fanIn(_ xs: [Int], combine: ([Int]) -> Int) -> [Int] {
        var xs = xs
        while xs.count > maxIn {
            let groups = stride(from: 0, to: xs.count, by: maxIn).map { Array(xs[$0..<min($0 + maxIn, xs.count)]) }
            xs = groups.map { $0.count == 1 ? $0[0] : combine($0) }
        }
        return xs
    }

    func AND(_ xs: [Int]) -> Int {
        let xs = unique(xs)
        if xs.count == 1 { return xs[0] }
        switch style {
        case .any: return make(.and, fanIn(xs, combine: AND))
        case .nand: return NOT(NAND(xs))
        case .nor: return NOR(xs.map(NOT))
        }
    }

    func OR(_ xs: [Int]) -> Int {
        let xs = unique(xs)
        if xs.count == 1 { return xs[0] }
        switch style {
        case .any: return make(.or, fanIn(xs, combine: OR))
        case .nand: return NAND(xs.map(NOT))
        case .nor: return NOT(NOR(xs))
        }
    }

    func NAND(_ xs: [Int]) -> Int {
        let xs = unique(xs)
        if xs.count == 1 { return NOT(xs[0]) }
        switch style {
        case .any, .nand:
            let net = make(.nand, fanIn(xs, combine: AND))
            if xs.count <= maxIn, let and = made["and:" + xs.sorted().map(String.init).joined(separator: ",")] {
                inverse[net] = and; inverse[and] = net
            }
            return net
        case .nor: return NOT(OR(xs))
        }
    }

    func NOR(_ xs: [Int]) -> Int {
        let xs = unique(xs)
        if xs.count == 1 { return NOT(xs[0]) }
        switch style {
        case .any, .nor: return make(.nor, fanIn(xs, combine: OR))
        case .nand: return NOT(OR(xs))
        }
    }

    func XOR(_ a: Int, _ b: Int) -> Int {
        switch style {
        case .any: return make(.xor, [a, b])
        case .nand:
            let t = NAND([a, b])
            return NAND([NAND([a, t]), NAND([b, t])])
        case .nor: return NOT(XNOR(a, b))
        }
    }

    func XNOR(_ a: Int, _ b: Int) -> Int {
        switch style {
        case .any: return make(.xnor, [a, b])
        case .nand: return NOT(XOR(a, b))
        case .nor:
            let t = NOR([a, b])
            return NOR([NOR([a, t]), NOR([b, t])])
        }
    }

    private func unique(_ xs: [Int]) -> [Int] {
        var seen = Set<Int>()
        return xs.filter { seen.insert($0).inserted }
    }

    // MARK: From a formula

    /// The formula (or its NOT), folding away constants: A + 1 is 1.
    func compile(_ e: BoolExpr, negated neg: Bool) -> Operand {
        switch e {
        case .variable(let v): return .net(neg ? NOT(v) : v)
        case .constant(let b): return .constant(b != neg)
        case .not(let x): return compile(x, negated: !neg)
        case .and(let xs), .or(let xs):
            let isAnd: Bool = { if case .and = e { return true } else { return false } }()
            var nets: [Int] = []
            for x in xs {
                switch compile(x, negated: false) {
                case .constant(let b):
                    // AND with 0 is 0, OR with 1 is 1; the other constant drops out.
                    if b != isAnd { return .constant(isAnd == neg) }
                case .net(let net): nets.append(net)
                }
            }
            nets = unique(nets)
            if nets.isEmpty { return .constant(isAnd != neg) }
            if nets.count == 1 { return .net(neg ? NOT(nets[0]) : nets[0]) }
            return .net(isAnd ? (neg ? NAND(nets) : AND(nets)) : (neg ? NOR(nets) : OR(nets)))
        case .xor(let xs):
            var flip = neg
            var nets: [Int] = []
            for x in xs {
                switch compile(x, negated: false) {
                case .constant(let b): if b { flip.toggle() }
                case .net(let net):
                    // A xor A is 0: a net twice cancels.
                    if let i = nets.firstIndex(of: net) { nets.remove(at: i) } else { nets.append(net) }
                }
            }
            if nets.isEmpty { return .constant(flip) }
            if nets.count == 1 { return .net(flip ? NOT(nets[0]) : nets[0]) }
            var acc = nets[0]
            for (k, net) in nets.dropFirst().enumerated() {
                let last = k == nets.count - 2
                acc = last && flip ? XNOR(acc, net) : XOR(acc, net)
            }
            return .net(acc)
        }
    }
}
