// Boolean algebra for the class: the simplest sum of products and product of
// sums for a truth table (Quine-McCluskey, then the smallest cover), Karnaugh
// map layouts, and reading a formula someone types ("F = A'B + AC",
// "S = A xor B xor Cin", "F(A,B,C) = Σm(1,3,5) + d(7)") into a truth table.
//
// Minterm numbers follow the truth table: the first variable is the most
// significant bit, so row r of the table is minterm r.

import Foundation

// MARK: - Minimizing

/// A product of some of the variables: `mask` bits are the ones left out
/// (a dash in the tabular method), `value` the rest.
struct Implicant: Hashable {
    var value: Int
    var mask: Int
    func covers(_ m: Int) -> Bool { (m & ~mask) == value }
    func literals(_ n: Int) -> Int { n - (mask & ((1 << n) - 1)).nonzeroBitCount }
}

enum Minimizer {
    /// Every prime implicant of the ones and don't-cares.
    static func primes(_ terms: [Int]) -> [Implicant] {
        var current = Set(terms.map { Implicant(value: $0, mask: 0) })
        var primes = Set<Implicant>()
        while !current.isEmpty {
            var next = Set<Implicant>(), used = Set<Implicant>()
            var byMask: [Int: [Implicant]] = [:]
            for i in current { byMask[i.mask, default: []].append(i) }
            for group in byMask.values where group.count > 1 {
                for a in 0..<(group.count - 1) {
                    for b in (a + 1)..<group.count {
                        let d = group[a].value ^ group[b].value
                        guard d.nonzeroBitCount == 1 else { continue }
                        next.insert(Implicant(value: group[a].value & ~d, mask: group[a].mask | d))
                        used.insert(group[a]); used.insert(group[b])
                    }
                }
            }
            primes.formUnion(current.subtracting(used))
            current = next
        }
        // Always the same order, so equally good answers don't swap about.
        return primes.sorted { a, b in
            let da = a.mask.nonzeroBitCount, db = b.mask.nonzeroBitCount
            if da != db { return da > db }             // fewest letters first
            return a.value != b.value ? a.value < b.value : a.mask < b.mask
        }
    }

    /// The fewest prime implicants that cover every one (then the fewest
    /// letters): the essential ones first, then a search, which gives up and
    /// keeps its best so far on a very large table.
    static func minimize(n: Int, ones: [Int], dontCares: [Int]) -> [Implicant] {
        guard !ones.isEmpty else { return [] }
        let all = primes(ones + dontCares)
        var chosen: [Implicant] = []
        var remaining = Set(ones)
        var candidates = all.filter { p in ones.contains(where: p.covers) }

        // Essential primes: the only one covering some one.
        var changed = true
        while changed {
            changed = false
            for m in remaining.sorted() {
                let by = candidates.filter { $0.covers(m) }
                if by.count == 1, let p = by.first {
                    chosen.append(p)
                    remaining = remaining.filter { !p.covers($0) }
                    candidates.removeAll { $0 == p }
                    changed = true
                    break
                }
            }
        }
        candidates = candidates.filter { p in remaining.contains(where: p.covers) }
        guard !remaining.isEmpty else { return sorted(chosen, n) }

        func cost(_ s: [Implicant]) -> (Int, Int) { (s.count, s.reduce(0) { $0 + $1.literals(n) }) }
        func better(_ a: (Int, Int), _ b: (Int, Int)) -> Bool { a.0 < b.0 || (a.0 == b.0 && a.1 < b.1) }

        // A first answer, greedily: most still-needed ones covered, fewest letters.
        var greedy: [Implicant] = [], left = remaining
        while !left.isEmpty {
            guard let p = candidates.max(by: { a, b in
                let ca = left.filter(a.covers).count, cb = left.filter(b.covers).count
                return ca != cb ? ca < cb : a.literals(n) > b.literals(n)
            }) else { break }
            greedy.append(p)
            left = left.filter { !p.covers($0) }
        }
        var best = greedy
        var nodes = 0
        func search(_ left: Set<Int>, _ picked: [Implicant]) {
            nodes += 1
            if nodes > 40000 { return }
            if left.isEmpty {
                if better(cost(picked), cost(best)) { best = picked }
                return
            }
            if picked.count + 1 > best.count { return }
            // Branch on the one with the fewest ways to cover it.
            guard let m = left.sorted().min(by: { a, b in
                candidates.filter { $0.covers(a) }.count < candidates.filter { $0.covers(b) }.count
            }) else { return }
            let ways = candidates.filter { $0.covers(m) }   // already fewest letters first
            for p in ways { search(left.filter { !p.covers($0) }, picked + [p]) }
        }
        search(remaining, [])
        return sorted(chosen + best, n)
    }

    /// Terms in reading order: A before A', and before terms without A.
    static func sorted(_ imps: [Implicant], _ n: Int) -> [Implicant] {
        func key(_ p: Implicant) -> [Int] {
            (0..<n).map { i in
                let bit = 1 << (n - 1 - i)
                return p.mask & bit != 0 ? 2 : (p.value & bit != 0 ? 0 : 1)
            }
        }
        return imps.sorted { key($0).lexicographicallyPrecedes(key($1)) }
    }
}

// MARK: - Two-level formulas

struct Literal: Hashable {
    let variable: Int
    let negated: Bool
}

/// A simplified formula: a sum of products or a product of sums, or a constant.
struct TwoLevel {
    enum Form { case sumOfProducts, productOfSums }
    let form: Form
    let terms: [[Literal]]
    let constant: Bool?
    let implicants: [Implicant]   // the groups on a Karnaugh map

    /// The simplest form of a function given as each minterm's value (nil: either).
    static func simplest(_ form: Form, n: Int, values: [Bool?]) -> TwoLevel {
        let ones = values.indices.filter { values[$0] == true }
        let zeros = values.indices.filter { values[$0] == false }
        let dc = values.indices.filter { values[$0] == nil }
        if zeros.isEmpty { return TwoLevel(form: form, terms: [], constant: !ones.isEmpty || !dc.isEmpty ? true : false, implicants: []) }
        if ones.isEmpty { return TwoLevel(form: form, terms: [], constant: false, implicants: []) }
        switch form {
        case .sumOfProducts:
            let imps = Minimizer.minimize(n: n, ones: ones, dontCares: dc)
            let terms = imps.map { p in
                (0..<n).compactMap { i -> Literal? in
                    let bit = 1 << (n - 1 - i)
                    return p.mask & bit != 0 ? nil : Literal(variable: i, negated: p.value & bit == 0)
                }
            }
            return TwoLevel(form: form, terms: terms, constant: nil, implicants: imps)
        case .productOfSums:
            // The simplest sum of products of the zeros, turned inside out.
            let imps = Minimizer.minimize(n: n, ones: zeros, dontCares: dc)
            let terms = imps.map { p in
                (0..<n).compactMap { i -> Literal? in
                    let bit = 1 << (n - 1 - i)
                    return p.mask & bit != 0 ? nil : Literal(variable: i, negated: p.value & bit != 0)
                }
            }
            return TwoLevel(form: form, terms: terms, constant: nil, implicants: imps)
        }
    }

    /// As text, with ' for NOT: "A'B + AC", "(A + B)(A' + C)".
    func text(names: [String]) -> String {
        tokens(names: names).map { t in
            switch t {
            case .literal(let s, let neg): s + (neg ? "'" : "")
            case .text(let s): s
            }
        }.joined()
    }

    /// For drawing, with a bar over each NOT.
    func tokens(names: [String]) -> [FormulaToken] {
        if let constant { return [.text(constant ? "1" : "0")] }
        let tight = names.allSatisfy { $0.count == 1 }
        var out: [FormulaToken] = []
        switch form {
        case .sumOfProducts:
            for (t, term) in terms.enumerated() {
                if t > 0 { out.append(.text(" + ")) }
                for (i, l) in term.enumerated() {
                    if i > 0 && !tight { out.append(.text("·")) }
                    out.append(.literal(names[l.variable], l.negated))
                }
            }
        case .productOfSums:
            let bare = terms.count == 1
            for clause in terms {
                let paren = !bare && clause.count > 1
                if paren { out.append(.text("(")) }
                for (i, l) in clause.enumerated() {
                    if i > 0 { out.append(.text(" + ")) }
                    out.append(.literal(names[l.variable], l.negated))
                }
                if paren { out.append(.text(")")) }
            }
        }
        return out
    }
}

enum FormulaToken: Hashable {
    case literal(String, Bool)   // name, with a bar over it
    case text(String)
}

// MARK: - Karnaugh maps

/// Where each minterm goes on a map of 2 to 4 variables: the first variables
/// down the side, the rest across the top, both in Gray code order.
struct KMapLayout {
    let n: Int
    let rowVars: Int, colVars: Int
    let rowCodes: [Int], colCodes: [Int]

    init?(n: Int) {
        guard (2...4).contains(n) else { return nil }
        self.n = n
        rowVars = n / 2
        colVars = n - rowVars
        func gray(_ bits: Int) -> [Int] { bits == 1 ? [0, 1] : [0b00, 0b01, 0b11, 0b10] }
        rowCodes = gray(rowVars)
        colCodes = gray(colVars)
    }

    func minterm(row: Int, col: Int) -> Int { (rowCodes[row] << colVars) | colCodes[col] }

    /// The rows and columns a group covers, each as runs of neighbouring
    /// cells (two runs when it wraps round an edge).
    func runs(_ p: Implicant) -> (rows: [ClosedRange<Int>], cols: [ClosedRange<Int>]) {
        let colMask = (1 << colVars) - 1
        let rows = rowCodes.indices.filter { r in
            ((rowCodes[r] & ~(p.mask >> colVars)) == (p.value >> colVars))
        }
        let cols = colCodes.indices.filter { c in
            ((colCodes[c] & ~(p.mask & colMask)) == (p.value & colMask))
        }
        return (Self.split(rows), Self.split(cols))
    }

    private static func split(_ idx: [Int]) -> [ClosedRange<Int>] {
        guard let first = idx.first else { return [] }
        var out: [ClosedRange<Int>] = []
        var start = first, prev = first
        for i in idx.dropFirst() {
            if i == prev + 1 { prev = i; continue }
            out.append(start...prev)
            start = i; prev = i
        }
        out.append(start...prev)
        return out
    }
}

// MARK: - Reading formulas

indirect enum BoolExpr {
    case variable(Int)
    case constant(Bool)
    case not(BoolExpr)
    case and([BoolExpr])
    case or([BoolExpr])
    case xor([BoolExpr])

    func eval(_ m: Int, n: Int) -> Bool {
        switch self {
        case .variable(let i): return (m >> (n - 1 - i)) & 1 == 1
        case .constant(let b): return b
        case .not(let e): return !e.eval(m, n: n)
        case .and(let es): return es.allSatisfy { $0.eval(m, n: n) }
        case .or(let es): return es.contains { $0.eval(m, n: n) }
        case .xor(let es): return es.reduce(false) { $0 != $1.eval(m, n: n) }
        }
    }
}

/// One output: its name, and either a formula or a list of minterms.
struct BoolFunction {
    var name: String
    var expr: BoolExpr?            // nil for a minterm list
    var values: [Bool?]            // per minterm of all the variables; nil: don't care
}

/// What a formula (or several, one per line) says: the variables and each output.
struct ParsedFormulas {
    var variables: [String]
    var functions: [BoolFunction]
}

struct FormulaError: Error { let message: String }

enum FormulaParser {
    static let maxVariables = 8

    /// "F = A'B + C" on each line (or separated by ";"). A line without a
    /// name is called F (then G, H...). Variables are one letter, maybe with
    /// digits after (A, X1) or a capital and small letters (Cin, Sel).
    static func parse(_ source: String) throws -> ParsedFormulas {
        let lines = source.replacingOccurrences(of: ";", with: "\n")
            .split(separator: "\n").map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty }
        guard !lines.isEmpty else { throw FormulaError(message: "Type a formula, like F = A'B + AC.") }

        struct Line { var name: String; var declared: [String]?; var rhs: String }
        var parsed: [Line] = []
        let defaultNames = ["F", "G", "H", "K", "P", "Q"]
        for (i, line) in lines.enumerated() {
            var name = i < defaultNames.count ? defaultNames[i] : "F\(i + 1)"
            var declared: [String]?
            var rhs = line
            if let eq = line.firstIndex(of: "=") {
                var lhs = line[..<eq].trimmingCharacters(in: .whitespaces)
                rhs = String(line[line.index(after: eq)...]).trimmingCharacters(in: .whitespaces)
                if let open = lhs.firstIndex(of: "("), lhs.hasSuffix(")") {
                    let inside = lhs[lhs.index(after: open)..<lhs.index(before: lhs.endIndex)]
                    declared = inside.split(separator: ",").map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty }
                    lhs = String(lhs[..<open]).trimmingCharacters(in: .whitespaces)
                }
                guard !lhs.isEmpty, lhs.allSatisfy({ $0.isLetter || $0.isNumber || $0 == "_" }) else {
                    throw FormulaError(message: "\"\(lhs)\" can't be an output's name. Use letters and digits, like F or Y1.")
                }
                name = lhs
            }
            guard !rhs.isEmpty else { throw FormulaError(message: "\(name) = what? There's nothing after the =.") }
            parsed.append(Line(name: name, declared: declared, rhs: rhs))
        }

        // The variables: a declared list wins; otherwise every name used, in order.
        var variables: [String] = []
        if let d = parsed.first(where: { $0.declared != nil })?.declared { variables = d }
        var exprs: [BoolExpr?] = []
        var minterms: [(ones: [Int], dc: [Int], maxterms: Bool)?] = []
        for line in parsed {
            if let list = mintermList(line.rhs) {
                guard line.declared != nil || !variables.isEmpty else {
                    throw FormulaError(message: "For a list of minterms, name the variables too: \(line.name)(A,B,C) = \(line.rhs)")
                }
                exprs.append(nil); minterms.append(list)
            } else {
                var p = Parser(text: line.rhs, variables: variables, fixed: line.declared != nil || parsed.contains { $0.declared != nil })
                let e = try p.parseAll()
                variables = p.variables
                exprs.append(e); minterms.append(nil)
            }
        }
        if !parsed.contains(where: { $0.declared != nil }) {
            // Undeclared: alphabetical, so "C + AB" still has A first.
            let order = variables.sorted { $0.localizedStandardCompare($1) == .orderedAscending }
            let remap = Dictionary(uniqueKeysWithValues: variables.enumerated().map { ($0.element, order.firstIndex(of: $0.element)!) })
            exprs = exprs.map { $0.map { renumber($0, variables, remap) } }
            variables = order
        }
        guard !variables.isEmpty || exprs.contains(where: { $0 != nil }) else {
            throw FormulaError(message: "There are no variables in that.")
        }
        guard variables.count <= maxVariables else {
            throw FormulaError(message: "That's \(variables.count) variables. Up to \(maxVariables) can be built at once.")
        }
        let n = variables.count
        var functions: [BoolFunction] = []
        for (i, line) in parsed.enumerated() {
            if let e = exprs[i] {
                functions.append(BoolFunction(name: line.name, expr: e, values: (0..<(1 << n)).map { e.eval($0, n: n) }))
            } else if let list = minterms[i] {
                let size = 1 << n
                if let bad = (list.ones + list.dc).first(where: { $0 >= size }) {
                    throw FormulaError(message: "\(bad) is too big for \(n) variables (the last minterm is \(size - 1)).")
                }
                var values = [Bool?](repeating: list.maxterms, count: size)
                for m in list.ones { values[m] = !list.maxterms }
                for m in list.dc { values[m] = nil }
                functions.append(BoolFunction(name: line.name, expr: nil, values: values))
            }
        }
        if Set(functions.map(\.name)).count != functions.count {
            throw FormulaError(message: "Two lines have the same name. Give each output its own, like F and G.")
        }
        if let clash = functions.first(where: { variables.contains($0.name) }) {
            throw FormulaError(message: "\(clash.name) is both an output and a variable.")
        }
        return ParsedFormulas(variables: variables, functions: functions)
    }

    private static func renumber(_ e: BoolExpr, _ old: [String], _ map: [String: Int]) -> BoolExpr {
        switch e {
        case .variable(let i): return .variable(map[old[i]]!)
        case .constant: return e
        case .not(let x): return .not(renumber(x, old, map))
        case .and(let xs): return .and(xs.map { renumber($0, old, map) })
        case .or(let xs): return .or(xs.map { renumber($0, old, map) })
        case .xor(let xs): return .xor(xs.map { renumber($0, old, map) })
        }
    }

    /// "Σm(1,3,5)", "sum m(1,3) + d(7)", "m(1,2)", "ΠM(0,2)", "M(0,4)".
    private static func mintermList(_ s: String) -> (ones: [Int], dc: [Int], maxterms: Bool)? {
        let t = s.replacingOccurrences(of: " ", with: "")
        let pattern = #"^(?:Σ|∑|sum|SUM|Sum|Π|∏|prod|PROD|Prod)?([mM])\(([\d,]*)\)(?:\+d\(([\d,]*)\))?$"#
        guard let re = try? NSRegularExpression(pattern: pattern),
              let m = re.firstMatch(in: t, range: NSRange(t.startIndex..., in: t)) else { return nil }
        func group(_ i: Int) -> String? {
            guard let r = Range(m.range(at: i), in: t) else { return nil }
            return String(t[r])
        }
        func numbers(_ s: String?) -> [Int] { (s ?? "").split(separator: ",").compactMap { Int($0) } }
        let maxterms = group(1) == "M" || t.hasPrefix("Π") || t.hasPrefix("∏") || t.lowercased().hasPrefix("prod")
        return (numbers(group(2)), numbers(group(3)), maxterms)
    }

    /// A recursive-descent reader: OR, then XOR, then AND (written or side
    /// by side), then NOT and the prime.
    struct Parser {
        let text: [Character]
        var i = 0
        var variables: [String]
        let fixed: Bool

        init(text: String, variables: [String], fixed: Bool) {
            self.text = Array(text)
            self.variables = variables
            self.fixed = fixed
        }

        enum Token: Equatable { case name(String), zero, one, lparen, rparen, or, xor, and, not, prime, end }

        mutating func parseAll() throws -> BoolExpr {
            let e = try parseOr()
            let t = peek()
            if t != .end { throw FormulaError(message: t == .rparen ? "There's a ) without a (." : "Something's off near \"\(rest())\".") }
            return e
        }

        private func rest() -> String { String(text[min(i, text.count)...].prefix(12)) }

        private func skipSpace(_ j: inout Int) { while j < text.count, text[j].isWhitespace { j += 1 } }

        /// The next token and where it ends, without taking it.
        private func scan() -> (Token, Int) {
            var j = i
            skipSpace(&j)
            guard j < text.count else { return (.end, j) }
            let c = text[j]
            switch c {
            case "(", "[": return (.lparen, j + 1)
            case ")", "]": return (.rparen, j + 1)
            case "+", "|", "∨": return (.or, j + 1)
            case "⊕", "^": return (.xor, j + 1)
            case "*", "·", "&", "∧", "•", ".", "⋅": return (.and, j + 1)
            case "~", "!", "¬", "-": return (.not, j + 1)
            case "'", "’", "`": return (.prime, j + 1)
            case "0": return (.zero, j + 1)
            case "1": return (.one, j + 1)
            default: break
            }
            guard c.isLetter else { return (.name(String(c)), j + 1) }
            // A word: an operator if it's one, else a name. Capitals start a
            // new name (AB is A and B), small letters and digits continue it
            // (Cin, X1, sel).
            var k = j
            while k < text.count, text[k].isLetter || text[k].isNumber || text[k] == "_" { k += 1 }
            let word = String(text[j..<k])
            switch word.uppercased() {
            case "OR": return (.or, k)
            case "XOR": return (.xor, k)
            case "AND": return (.and, k)
            case "NOT": return (.not, k)
            default: break
            }
            var e = j + 1
            while e < k, !text[e].isUppercase || text[j].isLowercase { e += 1 }
            return (.name(String(text[j..<e])), e)
        }

        private func peek() -> Token { scan().0 }
        private mutating func take() -> Token { let (t, e) = scan(); i = e; return t }

        private mutating func parseOr() throws -> BoolExpr {
            var parts = [try parseXor()]
            while peek() == .or { _ = take(); parts.append(try parseXor()) }
            return parts.count == 1 ? parts[0] : .or(parts)
        }

        private mutating func parseXor() throws -> BoolExpr {
            var parts = [try parseAnd()]
            while peek() == .xor { _ = take(); parts.append(try parseAnd()) }
            return parts.count == 1 ? parts[0] : .xor(parts)
        }

        private mutating func parseAnd() throws -> BoolExpr {
            var parts = [try parseUnary()]
            while true {
                let t = peek()
                if t == .and { _ = take(); parts.append(try parseUnary()); continue }
                // Side by side: AB, A(B + C), (A + B)(C + D).
                switch t {
                case .name, .zero, .one, .lparen, .not: parts.append(try parseUnary())
                default: return parts.count == 1 ? parts[0] : .and(parts)
                }
            }
        }

        private mutating func parseUnary() throws -> BoolExpr {
            if peek() == .not { _ = take(); return .not(try parseUnary()) }
            var e = try parsePrimary()
            while peek() == .prime { _ = take(); e = .not(e) }
            return e
        }

        private mutating func parsePrimary() throws -> BoolExpr {
            let t = take()
            switch t {
            case .zero: return .constant(false)
            case .one: return .constant(true)
            case .lparen:
                let e = try parseOr()
                guard take() == .rparen else { throw FormulaError(message: "A ( isn't closed with a ).") }
                return e
            case .name(let s):
                guard s.first?.isLetter == true else { throw FormulaError(message: "\"\(s)\" isn't something a formula can have.") }
                if let k = variables.firstIndex(of: s) { return .variable(k) }
                if fixed { throw FormulaError(message: "\(s) isn't one of the variables listed in the ( ).") }
                variables.append(s)
                return .variable(variables.count - 1)
            case .end: throw FormulaError(message: "The formula stops too soon; something's missing at the end.")
            case .rparen: throw FormulaError(message: "There's a ) where something should be.")
            default: throw FormulaError(message: "Something's missing before \"\(rest())\".")
            }
        }
    }
}
