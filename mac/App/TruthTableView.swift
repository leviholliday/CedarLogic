// A truth table built by trying every combination of the page's switches
// (or the selected ones). Column names come from nearby labels and can be
// changed here; Copy puts it on the clipboard as a tab-separated table that
// pastes into Word, Docs or a spreadsheet. In tabs: the table, a Karnaugh map
// for every light with the groups drawn on, and each light's simplest sum of
// products and product of sums (BooleanAlgebra.swift), with a way to build
// them as gates. And Check: the lights against a formula or truth table the
// assignment gives (CircuitCheck.swift), with the wrong rows shown.

import AppKit
import SwiftUI

struct TruthTable: Identifiable {
    let id = UUID()
    var names: [String]
    let inputs: Int
    let rows: [[Character]]
    let sequential: Bool
    let unsettled: Int
    /// Where the last check is kept (the circuit and page); nil keeps nothing.
    var checkKey: String?
    /// The circuit, for checks clock pulse by clock pulse (nil in previews).
    var document: CoreDocument?
    var page = 0
    /// Why there's no table, for a page that can only be checked clock pulse
    /// by clock pulse (a counter has lights but no switches).
    var problem: String?
    var hasTable: Bool { problem == nil }
    /// The table as Check My Circuit compares it.
    var checkTable: CircuitCheck.Table? {
        hasTable ? CircuitCheck.Table(names: names, inputs: inputs, rows: rows, sequential: sequential, unsettled: unsettled) : nil
    }

    init?(document: CoreDocument, page: Int, error: inout String) {
        var buf = [CChar](repeating: 0, count: 512)
        self.document = document
        self.page = page
        guard let tt = cl_truth_table(document.handle, Int32(page), &buf, Int32(buf.count)) else {
            error = String(cString: buf)
            return nil
        }
        defer { cl_tt_free(tt) }
        let cols = Int(cl_tt_columns(tt))
        names = (0..<cols).map { String(cString: cl_tt_name(tt, Int32($0))) }
        inputs = Int(cl_tt_inputs(tt))
        rows = (0..<Int(cl_tt_rows(tt))).map { r in
            (0..<cols).map { c in Character(UnicodeScalar(UInt8(bitPattern: cl_tt_cell(tt, Int32(r), Int32(c))))) }
        }
        sequential = cl_tt_sequential(tt)
        unsettled = Int(cl_tt_unsettled(tt))
    }

    /// For checks and previews: a table given outright.
    init(names: [String], inputs: Int, rows: [[Character]]) {
        self.names = names; self.inputs = inputs; self.rows = rows
        sequential = false; unsettled = 0
    }

    /// No table, just the Check tab: a page with lights that a truth table
    /// can't be made for (no switches, or too many).
    init(checkOnly document: CoreDocument, page: Int, problem: String) {
        names = []; inputs = 0; rows = []; sequential = true; unsettled = 0
        self.document = document
        self.page = page
        self.problem = problem
    }

    func text(separator: String) -> String {
        ([names.joined(separator: separator)] + rows.map { $0.map(String.init).joined(separator: separator) })
            .joined(separator: "\n") + "\n"
    }

    var outputCount: Int { names.count - inputs }

    /// One output's value for each row (minterm): nil where the light was
    /// neither on nor off (unknown, floating, a conflict), which a formula
    /// can treat as either.
    func values(output k: Int) -> [Bool?] {
        rows.map { row in
            let v = row[inputs + k]
            return v == "1" ? true : v == "0" ? false : nil
        }
    }

    /// A name a formula can use: letters, digits and _, starting with a letter.
    static func formulaName(_ s: String, fallback: String) -> String {
        let kept = String(s.filter { $0.isLetter || $0.isNumber || $0 == "_" })
        guard let first = kept.first, first.isLetter else { return fallback }
        // One capital then small letters and digits reads as one name ("Cin");
        // anything else (say "CLK") would read as several, so keep its first letter.
        let rest = kept.dropFirst()
        if kept.count == 1 || rest.allSatisfy({ !$0.isUppercase }) { return kept }
        return String(first)
    }
}

struct TruthTableView: View {
    @State var table: TruthTable
    var onBuild: ((String) -> Void)?
    @ObservedObject private var prefs = Prefs.shared
    @Environment(\.dismiss) private var dismiss
    @AppStorage("cl.truthTab") private var tab = 0
    @State private var groupsOfOnes = true
    @State private var copied: String?
    @State private var hoverRow: Int?
    @State private var checkText = ""
    @State private var checkNames: [String: String] = [:]
    @State private var check: CircuitCheck?
    @State private var onlyWrong = false
    /// The step strip's chosen step (the first wrong one when a check runs).
    @State private var selectedStep: Int?
    @FocusState private var stripFocused: Bool
    /// A clocked check waits for typing to pause (it runs the circuit).
    @State private var pendingCheck: DispatchWorkItem?

    static let checkTab = 3

    init(table: TruthTable, onBuild: ((String) -> Void)? = nil) {
        _table = State(initialValue: table)
        self.onBuild = onBuild
        // The last check of this circuit, checked again.
        let saved = CheckMemory.load(table.checkKey)
        _checkText = State(initialValue: saved?.text ?? "")
        _checkNames = State(initialValue: saved?.names ?? [:])
        let check = CircuitCheck(table: table.checkTable, noTable: table.problem ?? "", document: table.document?.handle, page: table.page,
                                 text: saved?.text ?? "", byHand: saved?.names ?? [:])
        _check = State(initialValue: check)
        _selectedStep = State(initialValue: check.firstWrong)
    }

    private var n: Int { table.inputs }
    /// The tab on show: only Check when the page has no truth table.
    private var shownTab: Int { table.hasTable ? tab : Self.checkTab }
    private var inputNames: [String] { Array(table.names.prefix(n)) }

    // The app's own paper and ink (as in Send Feedback).
    private var dark: Bool { prefs.dark }
    private var accent: Color { prefs.accentColor(dark: dark) }
    private var ink: Color { dark ? Color(white: 0.93) : Color(white: 0.1) }
    private var dim: Color { dark ? Color(white: 0.62) : Color(white: 0.42) }
    private var paper: Color { dark ? Color(.sRGB, red: 0.075, green: 0.085, blue: 0.1) : Color(.sRGB, red: 0.965, green: 0.97, blue: 0.975) }
    private var card: Color { dark ? Color(white: 1, opacity: 0.045) : .white }
    private var line: Color { dark ? Color(white: 1, opacity: 0.09) : Color(white: 0, opacity: 0.08) }
    private var on: Color { dark ? Brand.neon : Brand.neonDeep }

    var body: some View {
        VStack(spacing: 0) {
            Color.clear.frame(height: 0).onEscape { dismiss() }
            band
            VStack(alignment: .leading, spacing: 12) {
                if table.sequential && shownTab != Self.checkTab {
                    Label("This page has clocks or flip-flops, so outputs can depend on what happened before. Each row is read after the circuit settles from the row above it.",
                          systemImage: "info.circle")
                        .font(.callout).foregroundStyle(dim)
                        .fixedSize(horizontal: false, vertical: true)
                }
                if table.unsettled > 0 && shownTab != Self.checkTab {
                    Label("\(table.unsettled) row\(table.unsettled == 1 ? "" : "s") never stopped changing (a clock or an oscillation), so those outputs are a snapshot.",
                          systemImage: "exclamationmark.triangle")
                        .font(.callout).foregroundStyle(.orange)
                        .fixedSize(horizontal: false, vertical: true)
                }
                Group {
                    switch shownTab {
                    case 1: karnaughTab
                    case 2: formulasTab
                    case Self.checkTab: checkTab
                    default: tableTab
                    }
                }
                .frame(maxWidth: .infinity, minHeight: tabHeight, maxHeight: tabHeight, alignment: .topLeading)
                HStack(spacing: 10) {
                    if table.hasTable {
                        pill("Copy Table", "doc.on.doc") {
                            NSPasteboard.general.clearContents()
                            NSPasteboard.general.setString(table.text(separator: "\t"), forType: .string)
                            copied = "table"
                            DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) { if copied == "table" { copied = nil } }
                        }
                        .help("A tab-separated table: pastes into Word, Docs, Numbers or Excel")
                        pill("Export as CSV…", "square.and.arrow.up") { export() }
                    }
                    if copied == "table" { Text("Copied").font(.caption).foregroundStyle(dim) }
                    Spacer()
                    Button { dismiss() } label: {
                        Text("Done").font(.system(size: 13, weight: .semibold))
                            .padding(.horizontal, 22).padding(.vertical, 7)
                            .background(Capsule().fill(accent))
                            .foregroundStyle(Prefs.shared.onAccentColor(dark: Prefs.shared.dark))
                    }
                    .buttonStyle(.plain).keyboardShortcut(.defaultAction)
                }
            }
            .padding(.horizontal, 22).padding(.top, 16).padding(.bottom, 18)
        }
        .frame(minWidth: 620, idealWidth: 700, maxWidth: 880)
        .background(paper)
        .preferredColorScheme(dark ? .dark : .light)
        .tint(accent)
        .onChange(of: checkText) { runCheck() }
        .onChange(of: checkNames) { runCheck(now: true) }
        .onChange(of: table.names) { runCheck(now: true) }
    }

    /// The Check tab grows for the step strip.
    private var tabHeight: CGFloat { shownTab == Self.checkTab && !(check?.steps.isEmpty ?? true) ? 470 : 400 }

    // MARK: The band

    private var band: some View {
        ZStack(alignment: .bottomLeading) {
            LinearGradient(colors: [Brand.ink, Color(.sRGB, red: 0.06, green: 0.12, blue: 0.08)], startPoint: .topLeading, endPoint: .bottomTrailing)
            Canvas { ctx, size in   // a few traces, as on a circuit board
                var p = Path()
                for i in 0..<4 {
                    let y = 14 + CGFloat(i) * 15
                    p.move(to: CGPoint(x: size.width * 0.55, y: y))
                    p.addLine(to: CGPoint(x: size.width * 0.70 + CGFloat(i) * 12, y: y))
                    p.addLine(to: CGPoint(x: size.width * 0.74 + CGFloat(i) * 12, y: y + 9))
                    p.addLine(to: CGPoint(x: size.width, y: y + 9))
                }
                ctx.stroke(p, with: .linearGradient(Gradient(colors: [Brand.neon.opacity(0), Brand.neon.opacity(0.22)]),
                                                    startPoint: CGPoint(x: size.width * 0.5, y: 0), endPoint: CGPoint(x: size.width, y: 0)),
                           lineWidth: 1.2)
            }
            VStack(alignment: .leading, spacing: 10) {
                HStack(alignment: .firstTextBaseline, spacing: 10) {
                    Text(table.hasTable ? "Truth Table" : "Check My Circuit").font(.system(size: 21, weight: .semibold)).foregroundStyle(.white)
                    Text(table.hasTable ? "\(n) switch\(n == 1 ? "" : "es") → \(table.outputCount) light\(table.outputCount == 1 ? "" : "s") · \(table.rows.count) rows"
                                        : "No truth table for this page: check it clock pulse by clock pulse")
                        .font(.system(size: 12.5)).foregroundStyle(Brand.dim)
                }
                HStack(spacing: 8) {
                    if table.hasTable {
                        tabButton("Truth Table", "tablecells", 0)
                        tabButton("Karnaugh Map", "square.grid.3x3", 1)
                        tabButton("Formulas", "function", 2)
                    }
                    tabButton("Check", "checkmark.seal", Self.checkTab)
                }
            }
            .padding(.horizontal, 22).padding(.bottom, 14)
        }
        .frame(height: 104)
    }

    private func tabButton(_ title: String, _ icon: String, _ i: Int) -> some View {
        Button { withAnimation(.easeOut(duration: 0.15)) { tab = i } } label: {
            HStack(spacing: 6) {
                Image(systemName: icon).font(.system(size: 11.5, weight: .semibold))
                Text(title).font(.system(size: 12.5, weight: .semibold))
            }
            .padding(.horizontal, 13).padding(.vertical, 6)
            .background(Capsule().fill(shownTab == i ? Brand.neon : Color.white.opacity(0.09)))
            .foregroundStyle(shownTab == i ? Brand.inkDeep : Color.white.opacity(0.88))
        }
        .buttonStyle(.plain)
    }

    private func pill(_ title: String, _ icon: String, _ action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack(spacing: 6) {
                Image(systemName: icon).font(.system(size: 11.5))
                Text(title).font(.system(size: 12.5, weight: .medium))
            }
            .padding(.horizontal, 12).padding(.vertical, 6)
            .background(Capsule().fill(ink.opacity(0.06)))
            .overlay(Capsule().strokeBorder(line))
            .foregroundStyle(ink)
        }
        .buttonStyle(.plain)
    }

    // MARK: Tabs

    private var tableTab: some View {
        VStack(alignment: .leading, spacing: 8) {
            grid
            HStack(spacing: 14) {
                Text("Click a column's name to rename it.").foregroundStyle(dim)
                legend("1", "on", on)
                legend("X", "unknown", .orange)
                legend("Z", "floating", .blue)
                legend("!", "conflict", .red)
                legend("–", "not connected", dim)
            }
            .font(.caption)
        }
    }

    private func legend(_ symbol: String, _ label: String, _ color: Color) -> some View {
        HStack(spacing: 4) {
            Text(symbol).font(.system(size: 11, weight: .bold, design: .monospaced)).foregroundStyle(color)
            Text(label).foregroundStyle(dim)
        }
    }

    /// One map for every light on the page.
    private var karnaughTab: some View {
        VStack(alignment: .leading, spacing: 10) {
            if let layout = KMapLayout(n: n) {
                HStack {
                    Text("Groups of").font(.callout).foregroundStyle(dim)
                    HStack(spacing: 0) {
                        segment("1s (sum of products)", groupsOfOnes) { groupsOfOnes = true }
                        segment("0s (product of sums)", !groupsOfOnes) { groupsOfOnes = false }
                    }
                    .padding(2).background(Capsule().fill(ink.opacity(0.06)))
                    Spacer()
                    if unknownCount > 0 {
                        Label("X = don't care", systemImage: "info.circle").font(.caption).foregroundStyle(dim)
                    }
                }
                ScrollView {
                    LazyVGrid(columns: [GridItem(.adaptive(minimum: 300), spacing: 16, alignment: .top)], alignment: .leading, spacing: 16) {
                        ForEach(0..<table.outputCount, id: \.self) { k in
                            let values = table.values(output: k)
                            let f = TwoLevel.simplest(groupsOfOnes ? .sumOfProducts : .productOfSums, n: n, values: values)
                            VStack(alignment: .leading, spacing: 8) {
                                FormulaText(name: table.names[n + k], tokens: f.tokens(names: inputNames), size: 16)
                                KMapView(layout: layout, names: inputNames, values: values,
                                         groups: f.implicants, groupingOnes: groupsOfOnes)
                            }
                            .padding(14)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .background(cardShape)
                        }
                    }
                    .padding(.bottom, 4)
                }
            } else {
                Text(n > 4 ? "Karnaugh maps are drawn for 2 to 4 switches; this table has \(n)." : "A Karnaugh map needs at least 2 switches.")
                    .foregroundStyle(dim)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
    }

    private func segment(_ title: String, _ selected: Bool, _ action: @escaping () -> Void) -> some View {
        Button(action: { withAnimation(.easeOut(duration: 0.12)) { action() } }) {
            Text(title).font(.system(size: 12, weight: .medium))
                .padding(.horizontal, 12).padding(.vertical, 4)
                .background(Capsule().fill(selected ? accent : .clear))
                .foregroundStyle(selected ? Color.white : ink)
        }
        .buttonStyle(.plain)
    }

    private var cardShape: some View {
        RoundedRectangle(cornerRadius: 12, style: .continuous).fill(card)
            .overlay(RoundedRectangle(cornerRadius: 12, style: .continuous).strokeBorder(line))
    }

    private var formulasTab: some View {
        VStack(alignment: .leading, spacing: 10) {
            ScrollView {
                VStack(alignment: .leading, spacing: 14) {
                    ForEach(0..<table.outputCount, id: \.self) { k in
                        let values = table.values(output: k)
                        VStack(alignment: .leading, spacing: 12) {
                            formulaRow("simplest sum of products", .sumOfProducts, k, values)
                            Divider().opacity(0.6)
                            formulaRow("simplest product of sums", .productOfSums, k, values)
                        }
                        .padding(14)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .background(cardShape)
                    }
                    if unknownCount > 0 {
                        Label("Rows with no clear 0 or 1 count as don't-cares: the formulas treat them as either.",
                              systemImage: "info.circle")
                            .font(.caption).foregroundStyle(dim)
                    }
                }
                .padding(.bottom, 4)
            }
            pill("Build This as a Circuit…", "wand.and.stars") { onBuild?(buildText()) }
                .help("Opens Build from Formula with these sums of products, to make them as gates (NAND only, NOR only…)")
        }
    }

    private var unknownCount: Int {
        (0..<table.outputCount).reduce(0) { $0 + table.values(output: $1).filter { $0 == nil }.count }
    }

    private func formulaRow(_ title: String, _ form: TwoLevel.Form, _ k: Int, _ values: [Bool?]) -> some View {
        let f = TwoLevel.simplest(form, n: n, values: values)
        let text = "\(table.names[n + k]) = " + f.text(names: inputNames)
        let key = "\(k)-\(title)"
        return VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(title.uppercased()).font(.system(size: 10.5, weight: .semibold)).tracking(0.6).foregroundStyle(dim)
                Spacer()
                Button(copied == key ? "Copied" : "Copy") {
                    NSPasteboard.general.clearContents()
                    NSPasteboard.general.setString(text, forType: .string)
                    copied = key
                    DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) { if copied == key { copied = nil } }
                }
                .buttonStyle(.plain).font(.caption.weight(.medium)).foregroundStyle(accent)
                .help("Copies \(text)")
            }
            FormulaText(name: table.names[n + k], tokens: f.tokens(names: inputNames), size: 19)
                .textSelection(.enabled)
        }
    }

    /// Every light's simplest sum of products, in names the formula reader takes.
    private func buildText() -> String {
        var used = Set<String>()
        let names = inputNames.enumerated().map { i, s -> String in
            var name = TruthTable.formulaName(s, fallback: String(UnicodeScalar(UInt8(65 + i))))
            if used.contains(name) { name = String(UnicodeScalar(UInt8(65 + i))) }
            used.insert(name)
            return name
        }
        return (0..<table.outputCount).map { k in
            var out = TruthTable.formulaName(table.names[n + k], fallback: table.outputCount == 1 ? "F" : "F\(k + 1)")
            if names.contains(out) { out = "F\(k + 1)" }
            let f = TwoLevel.simplest(.sumOfProducts, n: n, values: table.values(output: k))
            return "\(out)(\(names.joined(separator: ","))) = " + f.text(names: names)
        }.joined(separator: "\n")
    }

    // MARK: The table

    private let cellWidth: CGFloat = 64

    private var grid: some View {
        ScrollView([.vertical, .horizontal]) {
            VStack(spacing: 0) {
                // Names: inputs quiet, outputs in the accent.
                HStack(spacing: 0) {
                    ForEach(table.names.indices, id: \.self) { c in
                        let isOutput = c >= table.inputs
                        TextField("", text: $table.names[c])
                            .textFieldStyle(.plain)
                            .multilineTextAlignment(.center)
                            .font(.system(size: 13, weight: .semibold))
                            .foregroundStyle(isOutput ? accent : ink)
                            .frame(width: cellWidth)
                            .padding(.vertical, 9)
                            .background(isOutput ? accent.opacity(dark ? 0.13 : 0.10) : ink.opacity(0.05))
                            .overlay(alignment: .leading) { if c == table.inputs { divider } }
                    }
                }
                .clipShape(UnevenRoundedRectangle(topLeadingRadius: 11, topTrailingRadius: 11))
                ForEach(table.rows.indices, id: \.self) { r in
                    HStack(spacing: 0) {
                        ForEach(table.rows[r].indices, id: \.self) { c in
                            cell(table.rows[r][c], isOutput: c >= table.inputs)
                                .overlay(alignment: .leading) { if c == table.inputs { divider } }
                        }
                    }
                    .background(hoverRow == r ? accent.opacity(0.10) : (r % 2 == 1 ? ink.opacity(0.03) : .clear))
                    .onHover { inside in if inside { hoverRow = r } else if hoverRow == r { hoverRow = nil } }
                }
            }
            .background(RoundedRectangle(cornerRadius: 12, style: .continuous).fill(card))
            .overlay(RoundedRectangle(cornerRadius: 12, style: .continuous).strokeBorder(line))
            .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
            .padding(1)
            .fixedSize()
        }
    }

    private var divider: some View { Rectangle().frame(width: 1.5).foregroundStyle(accent.opacity(0.55)) }

    /// A value: a lit chip for 1, quiet for 0, coloured for the rest.
    private func cell(_ v: Character, isOutput: Bool) -> some View {
        let color: Color = v == "1" ? on : v == "0" ? dim : v == "Z" ? .blue : v == "!" ? .red : v == "X" ? .orange : dim
        return Text(String(v))
            .font(.system(size: 14, weight: v == "1" ? .bold : .regular, design: .monospaced))
            .foregroundStyle(color)
            .frame(width: cellWidth, height: 28)
            .background {
                if isOutput && v != "0" && v != "-" {
                    RoundedRectangle(cornerRadius: 7, style: .continuous).fill(color.opacity(dark ? 0.16 : 0.12)).padding(.horizontal, 12).padding(.vertical, 3)
                }
            }
    }

    private func export() {
        let panel = NSSavePanel()
        panel.nameFieldStringValue = "Truth Table.csv"
        panel.allowedContentTypes = [.commaSeparatedText]
        if panel.runModal() == .OK, let url = panel.url {
            try? table.text(separator: ",").write(to: url, atomically: true, encoding: .utf8)
        }
    }
}

/// A formula as a textbook writes it: NOT as a bar over the letter.
struct FormulaText: View {
    let name: String
    let tokens: [FormulaToken]
    var size: CGFloat = 17

    var body: some View {
        Flow(spacing: 0) {
            piece([.text(name + " = ")])
            ForEach(Array(chunks.enumerated()), id: \.offset) { _, chunk in piece(chunk) }
        }
    }

    /// Terms kept together on a line: a line breaks only between them.
    private var chunks: [[FormulaToken]] {
        var out: [[FormulaToken]] = [[]]
        var depth = 0
        for t in tokens {
            if case .text(let s) = t {
                if s == " + " && depth == 0 && !(out.last?.isEmpty ?? true) { out.append([]) }
                depth += s.filter { $0 == "(" }.count - s.filter { $0 == ")" }.count
                if s == "(" && depth == 1 && !(out.last?.isEmpty ?? true) { out.append([]) }
            }
            out[out.count - 1].append(t)
        }
        return out.filter { !$0.isEmpty }
    }

    private func piece(_ ts: [FormulaToken]) -> some View {
        HStack(spacing: 0) {
            ForEach(Array(ts.enumerated()), id: \.offset) { _, t in
                switch t {
                case .text(let s):
                    Text(s).font(.system(size: size))
                case .literal(let s, let bar):
                    Text(s).font(.system(size: size, weight: .medium, design: .serif).italic())
                        .padding(.horizontal, 0.5)
                        .overlay(alignment: .top) {
                            if bar { Rectangle().frame(height: 1.3).padding(.horizontal, 1).offset(y: 1) }
                        }
                }
            }
        }
        .padding(.top, 3)
    }
}

/// A Karnaugh map: the first variables down the side, the rest across the
/// top, in Gray code order; each cell's value with its minterm number in the
/// corner; and the chosen groups drawn round their cells (open at the edge
/// when a group wraps round to the other side).
struct KMapView: View {
    let layout: KMapLayout
    let names: [String]
    let values: [Bool?]
    let groups: [Implicant]
    let groupingOnes: Bool

    private let cell: CGFloat = 46
    private let left: CGFloat = 54
    private let top: CGFloat = 34
    private static let colors: [Color] = [.blue, .orange, .green, .purple, .pink, .teal, .red, .indigo, .brown, .mint, .cyan, .yellow]

    var body: some View {
        let rows = layout.rowCodes.count, cols = layout.colCodes.count
        Canvas { ctx, _ in
            let grid = CGRect(x: left, y: top, width: CGFloat(cols) * cell, height: CGFloat(rows) * cell)
            // Headers: the variables, then each row's and column's bits.
            let rowNames = names.prefix(layout.rowVars).joined()
            let colNames = names.suffix(layout.colVars).joined()
            ctx.draw(Text(rowNames).font(.system(size: 12, weight: .semibold)).foregroundStyle(.secondary),
                     at: CGPoint(x: left / 2, y: top - 10))
            ctx.draw(Text(colNames).font(.system(size: 12, weight: .semibold)).foregroundStyle(.secondary),
                     at: CGPoint(x: left + grid.width / 2, y: 9))
            var diagonal = Path()
            diagonal.move(to: CGPoint(x: left - 20, y: top - 20))
            diagonal.addLine(to: CGPoint(x: left, y: top))
            ctx.stroke(diagonal, with: .color(.secondary.opacity(0.4)), lineWidth: 1)
            func bits(_ v: Int, _ count: Int) -> String {
                String((0..<count).map { (v >> (count - 1 - $0)) & 1 == 1 ? "1" : "0" })
            }
            for c in 0..<cols {
                ctx.draw(Text(bits(layout.colCodes[c], layout.colVars)).font(.system(size: 12).monospaced()).foregroundStyle(.secondary),
                         at: CGPoint(x: left + (CGFloat(c) + 0.5) * cell, y: top - 10))
            }
            for r in 0..<rows {
                ctx.draw(Text(bits(layout.rowCodes[r], layout.rowVars)).font(.system(size: 12).monospaced()).foregroundStyle(.secondary),
                         at: CGPoint(x: left - 16, y: top + (CGFloat(r) + 0.5) * cell))
            }
            // Cells.
            for r in 0..<rows {
                for c in 0..<cols {
                    let rect = CGRect(x: left + CGFloat(c) * cell, y: top + CGFloat(r) * cell, width: cell, height: cell)
                    ctx.stroke(Path(rect), with: .color(.secondary.opacity(0.35)), lineWidth: 1)
                    let m = layout.minterm(row: r, col: c)
                    let v = m < values.count ? values[m] : nil
                    let s = v == true ? "1" : v == false ? "0" : "X"
                    let strong = v == groupingOnes
                    ctx.draw(Text(s).font(.system(size: 17, weight: strong ? .semibold : .regular).monospaced())
                                .foregroundStyle(v == nil ? AnyShapeStyle(Color.orange) : strong ? AnyShapeStyle(.primary) : AnyShapeStyle(.secondary)),
                             at: CGPoint(x: rect.midX, y: rect.midY + 1))
                    ctx.draw(Text("\(m)").font(.system(size: 8.5)).foregroundStyle(.tertiary),
                             at: CGPoint(x: rect.maxX - 7, y: rect.maxY - 7))
                }
            }
            ctx.stroke(Path(grid), with: .color(.secondary.opacity(0.7)), lineWidth: 1.5)
            // The groups, clipped to the map so a wrapping one reads as open-ended.
            var clipped = ctx
            clipped.clip(to: Path(grid.insetBy(dx: -1, dy: -1)))
            for (k, g) in groups.enumerated() {
                let color = Self.colors[k % Self.colors.count]
                let (rowRuns, colRuns) = layout.runs(g)
                let inset = 4 + CGFloat(k % 3) * 3
                for rr in rowRuns {
                    for cr in colRuns {
                        var rect = CGRect(x: left + CGFloat(cr.lowerBound) * cell, y: top + CGFloat(rr.lowerBound) * cell,
                                          width: CGFloat(cr.count) * cell, height: CGFloat(rr.count) * cell)
                            .insetBy(dx: inset, dy: inset)
                        if colRuns.count > 1 {
                            if cr.lowerBound == 0 { rect.origin.x -= cell / 2; rect.size.width += cell / 2 }
                            if cr.upperBound == cols - 1 { rect.size.width += cell / 2 }
                        }
                        if rowRuns.count > 1 {
                            if rr.lowerBound == 0 { rect.origin.y -= cell / 2; rect.size.height += cell / 2 }
                            if rr.upperBound == rows - 1 { rect.size.height += cell / 2 }
                        }
                        let shape = Path(roundedRect: rect, cornerRadius: min(rect.width, rect.height) / 2.4)
                        clipped.fill(shape, with: .color(color.opacity(0.10)))
                        clipped.stroke(shape, with: .color(color.opacity(0.85)), lineWidth: 2)
                    }
                }
            }
        }
        .frame(width: left + CGFloat(cols) * cell + 4, height: top + CGFloat(rows) * cell + 4)
        .accessibilityLabel("Karnaugh map")
    }
}

// MARK: - Check

extension TruthTableView {
    /// One example of each kind of answer.
    private var checkPlaceholder: String {
        """
        S = A ^ B ^ Cin              a formula (or Σm(1,3,5))
        A B | F, then a row each     a truth table
        0, 1, 2, 3, repeat           a count
        Q1 Q0 X | Q1+ Q0+ | Z …      a state table
        Pulse | X | Z, then rows     a timing table
        """
    }

    /// Room for the placeholder's five examples, else for what's typed (a
    /// formula needs less than a table).
    private var editorHeight: CGFloat {
        if checkText.isEmpty { return 100 }
        let lines = checkText.split(separator: "\n", omittingEmptySubsequences: false).count
        return min(100, max(58, CGFloat(lines) * 17 + 16))
    }

    var checkTab: some View {
        VStack(alignment: .leading, spacing: 9) {
            HStack(spacing: 8) {
                let kind = CircuitCheck.kind(of: checkText).kind
                Image(systemName: kind == .empty ? "text.cursor" : "text.magnifyingglass").font(.system(size: 12)).foregroundStyle(dim)
                Text(kind.reading).font(.callout).foregroundStyle(dim)
                Spacer()
                if table.hasTable && (kind == .empty || kind == .table) {
                    Button("Fill In This Circuit's Rows") { checkText = circuitTableText() }
                        .buttonStyle(.plain).font(.caption.weight(.medium)).foregroundStyle(accent)
                        .help("Writes this circuit's table here, to change into the one you were given")
                }
            }
            ZStack(alignment: .topLeading) {
                TextEditor(text: $checkText)
                    .font(.system(size: 13, design: .monospaced))
                    .scrollContentBackground(.hidden)
                    .autocorrectionDisabled()
                    .padding(.horizontal, 7).padding(.vertical, 6)
                if checkText.isEmpty {
                    Text(checkPlaceholder)
                        .font(.system(size: 13, design: .monospaced)).foregroundStyle(dim.opacity(0.75))
                        .padding(.horizontal, 12).padding(.vertical, 6)
                        .allowsHitTesting(false)
                }
            }
            .frame(height: editorHeight)
            .background(cardShape)
            if let check {
                verdictBanner(check)
                ForEach(check.notes, id: \.self) { note in
                    Label(note.text, systemImage: note.kind == 2 ? "exclamationmark.circle" : note.kind == 1 ? "exclamationmark.triangle" : "info.circle")
                        .font(.caption)
                        .foregroundStyle(note.kind == 2 ? Color.red : note.kind == 1 ? Color.orange : dim)
                        .fixedSize(horizontal: false, vertical: true)
                }
                if showNames(check) { namesRow(check) }
                if check.clocked {
                    if !check.steps.isEmpty { stepStrip(check) }
                } else if check.outputs.contains(where: { $0.column != nil }) {
                    checkGrid(check)
                }
            }
            Spacer(minLength: 0)
        }
    }

    /// Checks again: at once against a truth table, or once typing pauses
    /// when the circuit has to be run (`now` for a name picked from a menu).
    func runCheck(now: Bool = false) {
        pendingCheck?.cancel()
        let (kind, options) = CircuitCheck.kind(of: checkText)
        let clocked = options || [.count, .states, .timing].contains(kind)
        let work = DispatchWorkItem { [self] in
            let c = CircuitCheck(table: table.checkTable, noTable: table.problem ?? "", document: table.document?.handle, page: table.page,
                                 text: checkText, byHand: checkNames)
            check = c
            selectedStep = c.firstWrong
            if !checkText.isEmpty || CheckMemory.load(table.checkKey) != nil {
                CheckMemory.save(table.checkKey, .init(kind: c.kind == .table ? 1 : 0, text: checkText, names: checkNames))
            }
        }
        if now || !clocked { work.perform() } else {
            pendingCheck = work
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.3, execute: work)
        }
    }

    private func verdictBanner(_ c: CircuitCheck) -> some View {
        let empty = checkText.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
        let color: Color = empty ? dim : c.verdict == .matches ? on : c.verdict == .wrong ? .red : .orange
        let icon = empty ? "text.cursor" : c.verdict == .matches ? "checkmark.circle.fill" : c.verdict == .wrong ? "xmark.circle.fill" : "questionmark.circle.fill"
        return HStack(spacing: 9) {
            Image(systemName: icon).font(.system(size: 17, weight: .semibold)).foregroundStyle(color)
            Text(c.summary).font(.system(size: 13.5, weight: empty ? .regular : .semibold)).foregroundStyle(empty ? dim : ink)
                .fixedSize(horizontal: false, vertical: true)
            Spacer(minLength: 0)
            if c.verdict == .wrong && !c.clocked {
                Toggle("Only wrong rows", isOn: $onlyWrong).toggleStyle(.checkbox).font(.caption)
            }
        }
        .padding(.horizontal, 12).padding(.vertical, 8)
        .background(RoundedRectangle(cornerRadius: 10, style: .continuous).fill(color.opacity(empty ? 0.06 : dark ? 0.16 : 0.11)))
    }

    /// Names are shown when one isn't simply the same as the circuit's.
    private func showNames(_ c: CircuitCheck) -> Bool {
        func key(_ s: String) -> String { s.lowercased().filter { $0.isLetter || $0.isNumber } }
        return c.names.contains { name in
            guard let col = name.column, col < c.columns.count else { return true }
            return name.byHand || key(c.columns[col]) != key(name.name)
        }
    }

    /// Each asked-for name and the switch or light it was matched with; click to choose.
    private func namesRow(_ c: CircuitCheck) -> some View {
        HStack(alignment: .firstTextBaseline, spacing: 8) {
            Text("Names").font(.caption.weight(.semibold)).foregroundStyle(dim)
            Flow(spacing: 6) {
                ForEach(c.names, id: \.self) { name in nameChip(c, name) }
            }
        }
    }

    private func nameChip(_ c: CircuitCheck, _ name: CircuitCheck.Name) -> some View {
        var choices: [String] = []
        for (i, col) in c.columns.enumerated() where c.columnIsInput[i] == name.isInput && !choices.contains(col) { choices.append(col) }
        let matched: String = name.column.flatMap { $0 < c.columns.count ? c.columns[$0] : nil } ?? "?"
        let missing = name.column == nil
        let kind = name.isInput ? "Switch " : "Light "
        return Menu {
            ForEach(choices, id: \.self) { choice in
                Button(kind + choice) { checkNames[name.name] = choice }
            }
            Divider()
            Button("Match by Name") { checkNames[name.name] = nil }
        } label: {
            Text("\(name.name) → \(matched)").font(.system(size: 11.5, weight: name.byHand ? .semibold : .regular))
        }
        .menuStyle(.borderlessButton).menuIndicator(.hidden).fixedSize()
        .padding(.horizontal, 9).padding(.vertical, 3)
        .background(Capsule().fill(missing ? Color.red.opacity(0.14) : ink.opacity(0.06)))
        .overlay(Capsule().strokeBorder(missing ? Color.red.opacity(0.5) : line))
        .help("Which " + kind.lowercased() + "is " + name.name)
    }

    // MARK: The step strip

    private static let stepWidth: CGFloat = 56
    private static let nameWidth: CGFloat = 86
    private static let headHeight: CGFloat = 36
    private static let oneHeight: CGFloat = 24      // inputs and the state: what they were
    private static let pairHeight: CGFloat = 38     // the next state and outputs: asked for over got

    /// A column per step (the start, then each clock pulse), a row per
    /// signal: inputs and the state as they were, then the next state and the
    /// lights, what was asked for over what they showed. The first wrong step
    /// is marked and scrolled to; ← and → move along the steps.
    private func stepStrip(_ c: CircuitCheck) -> some View {
        let rows = stripRows(c)
        let selected = selectedStep.flatMap { c.steps.indices.contains($0) ? $0 : nil }
        return VStack(alignment: .leading, spacing: 6) {
            ScrollView(.vertical) {
                HStack(alignment: .top, spacing: 0) {
                    // The signals' names stay put while the steps scroll.
                    VStack(alignment: .leading, spacing: 0) {
                        Text(c.kind == .states ? "Row" : "Step").font(.system(size: 10.5, weight: .semibold)).foregroundStyle(dim)
                            .frame(width: Self.nameWidth, height: Self.headHeight, alignment: .leading).padding(.leading, 10)
                        ForEach(rows, id: \.self) { r in
                            stripName(c, r).frame(height: r.height).padding(.leading, 10)
                        }
                    }
                    .background(ink.opacity(0.04))
                    .overlay(alignment: .trailing) { Rectangle().frame(width: 1).foregroundStyle(line) }
                    ScrollViewReader { reader in
                        ScrollView(.horizontal) {
                            HStack(spacing: 0) {
                                ForEach(c.steps.indices, id: \.self) { i in
                                    stepColumn(c, i, rows, selected: selected == i).id(i)
                                }
                            }
                        }
                        .onAppear { if let w = c.firstWrong { reader.scrollTo(w, anchor: .center) } }
                        .onChange(of: selectedStep) { if let s = selectedStep { withAnimation(.easeOut(duration: 0.15)) { reader.scrollTo(s) } } }
                    }
                }
            }
            .frame(maxHeight: CGFloat(rows.reduce(0) { $0 + $1.height }) + Self.headHeight + 2)
            .background(RoundedRectangle(cornerRadius: 12, style: .continuous).fill(card))
            .overlay(RoundedRectangle(cornerRadius: 12, style: .continuous).strokeBorder(stripFocused ? accent.opacity(0.7) : line))
            .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
            .focusable()
            .focused($stripFocused)
            .focusEffectDisabled()
            .onMoveCommand { direction in
                let last = c.steps.count - 1
                let at = selectedStep ?? c.firstWrong ?? 0
                switch direction {
                case .left: selectedStep = max(0, at - 1)
                case .right: selectedStep = min(last, at + 1)
                default: break
                }
            }
            .accessibilityElement(children: .contain)
            .accessibilityLabel("Clock pulse by clock pulse")
            HStack(spacing: 8) {
                if let s = selected {
                    Text(c.describe(s)).font(.caption).foregroundStyle(c.steps[s].wrong ? Color.red : dim)
                        .fixedSize(horizontal: false, vertical: true)
                } else {
                    Text("Click a step, or use ← and →, to read it.").font(.caption).foregroundStyle(dim)
                }
                Spacer(minLength: 0)
                if let w = c.firstWrong, selected != w {
                    Button("Show the First Wrong Step") { selectedStep = w }
                        .buttonStyle(.plain).font(.caption.weight(.medium)).foregroundStyle(accent)
                }
            }
        }
    }

    /// A row of the strip: a count's number, or a signal.
    private struct StripRow: Hashable {
        var signal: Int?          // nil: the count, as a number
        var pair: Bool
        var height: CGFloat { pair ? TruthTableView.pairHeight : TruthTableView.oneHeight }
    }

    private func stripRows(_ c: CircuitCheck) -> [StripRow] {
        var rows: [StripRow] = []
        if c.kind == .count && c.signals.count > 1 { rows.append(StripRow(signal: nil, pair: true)) }
        for role in [CircuitCheck.Role.input, .state, .next, .output] {
            for i in c.signals(role) { rows.append(StripRow(signal: i, pair: role == .next || role == .output)) }
        }
        return rows
    }

    private func stripName(_ c: CircuitCheck, _ r: StripRow) -> some View {
        let role = r.signal.map { c.signals[$0].role }
        let name = r.signal.map { c.signals[$0].name } ?? "Count"
        let color: Color = role == .input || role == .state ? ink : accent
        return HStack(spacing: 4) {
            Text(name).font(.system(size: 12, weight: .semibold)).foregroundStyle(color).lineLimit(1)
            if r.pair {
                VStack(alignment: .trailing, spacing: 1) {
                    Text("asked")
                    Text("got")
                }
                .font(.system(size: 9)).foregroundStyle(dim)
            }
        }
        .frame(width: Self.nameWidth - 6, alignment: .leading)
    }

    private func stepColumn(_ c: CircuitCheck, _ i: Int, _ rows: [StripRow], selected: Bool) -> some View {
        let step = c.steps[i]
        let first = c.firstWrong == i
        let tint: Color = first ? Color.red.opacity(dark ? 0.26 : 0.16) : step.wrong ? Color.red.opacity(dark ? 0.12 : 0.07)
                        : (i % 2 == 1 ? ink.opacity(0.025) : .clear)
        return Button { selectedStep = i; stripFocused = true } label: {
            VStack(spacing: 0) {
                VStack(spacing: 1) {
                    Text(CircuitCheck.title(step)).font(.system(size: 10.5, weight: .semibold))
                        .foregroundStyle(first ? Color.red : ink)
                    Text(step.row > 0 ? "row \(step.row)" : first ? "first wrong" : " ")
                        .font(.system(size: 9)).foregroundStyle(first ? Color.red : dim)
                }
                .frame(width: Self.stepWidth, height: Self.headHeight)
                ForEach(rows, id: \.self) { r in
                    stripCell(c, step, r).frame(width: Self.stepWidth, height: r.height)
                }
            }
            .background(tint)
            .overlay {
                if selected { RoundedRectangle(cornerRadius: 6, style: .continuous).strokeBorder(accent, lineWidth: 2).padding(1) }
                else if first { Rectangle().strokeBorder(Color.red.opacity(0.7), lineWidth: 1.5) }
            }
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(c.describe(i))
        .accessibilityAddTraits(selected ? .isSelected : [])
    }

    @ViewBuilder
    private func stripCell(_ c: CircuitCheck, _ step: CircuitCheck.Step, _ r: StripRow) -> some View {
        if let i = r.signal {
            let (asked, got) = c.cell(step, signal: i)
            if r.pair {
                let wrong = asked != nil && asked != "-" && got != nil && got != asked
                VStack(spacing: 1) {
                    Text(asked.map { $0 == "-" ? "–" : String($0) } ?? " ")
                        .font(.system(size: 11.5, design: .monospaced)).foregroundStyle(dim)
                    stripValue(got, wrong: wrong)
                }
            } else {
                Text(got.map { String($0) } ?? " ")
                    .font(.system(size: 13, design: .monospaced)).foregroundStyle(dim)
            }
        } else {
            // A count: the number over the bits.
            let asked = CircuitCheck.number(step.expected), got = CircuitCheck.number(step.got)
            VStack(spacing: 1) {
                Text(asked.map { String($0) } ?? " ").font(.system(size: 11.5, design: .monospaced)).foregroundStyle(dim)
                Text(got.map { String($0) } ?? (step.got.isEmpty ? " " : "?"))
                    .font(.system(size: 13.5, weight: .bold, design: .monospaced))
                    .foregroundStyle(step.wrong ? Color.red : ink)
            }
        }
    }

    /// What a light showed: 1 lit, 0 quiet, the rest in their colours; red when wrong.
    private func stripValue(_ v: Character?, wrong: Bool) -> some View {
        let s = v.map { String($0) } ?? " "
        let color: Color = wrong ? .red : v == "1" ? on : v == "0" ? ink : v == "Z" ? .blue : v == "!" ? .red : v == "X" ? .orange : dim
        return Text(s)
            .font(.system(size: 13.5, weight: wrong || v == "1" ? .bold : .regular, design: .monospaced))
            .foregroundStyle(color)
            .frame(width: 26, height: 18)
            .background { if wrong { RoundedRectangle(cornerRadius: 5, style: .continuous).fill(Color.red.opacity(dark ? 0.22 : 0.14)) } }
    }

    private static let checkCell: CGFloat = 50

    /// The circuit's inputs, then for each output what was asked for and what it gave.
    private func checkGrid(_ c: CircuitCheck) -> some View {
        let outs: [Int] = c.outputs.indices.filter { c.outputs[$0].column != nil }
        let rows: [Int] = table.rows.indices.filter { !onlyWrong || c.wrongRows.contains($0) }
        return ScrollViewReader { reader in
          ScrollView([.vertical, .horizontal]) {
            VStack(spacing: 0) {
                checkHeader(c, outs)
                LazyVStack(spacing: 0) {
                    ForEach(rows, id: \.self) { r in checkRow(c, outs, r).id(r) }
                }
            }
            .background(RoundedRectangle(cornerRadius: 12, style: .continuous).fill(card))
            .overlay(RoundedRectangle(cornerRadius: 12, style: .continuous).strokeBorder(line))
            .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
            .padding(1)
            .fixedSize(horizontal: true, vertical: false)
          }
          // The first wrong row in view.
          .onAppear { if let r = c.wrongRows.min() { reader.scrollTo(r, anchor: .center) } }
          .onChange(of: c.wrongRows) { if let r = c.wrongRows.min() { withAnimation { reader.scrollTo(r, anchor: .center) } } }
        }
    }

    private func checkHeader(_ c: CircuitCheck, _ outs: [Int]) -> some View {
        let w = Self.checkCell
        return HStack(spacing: 0) {
            ForEach(0..<n, id: \.self) { i in
                Text(table.names[i]).font(.system(size: 12, weight: .semibold)).foregroundStyle(ink)
                    .lineLimit(1).frame(width: w).padding(.vertical, 7)
            }
            ForEach(outs, id: \.self) { k in
                VStack(spacing: 0) {
                    Text(c.outputs[k].name).font(.system(size: 12, weight: .semibold)).foregroundStyle(accent).lineLimit(1)
                    HStack(spacing: 0) {
                        Text("asked").frame(width: w)
                        Text("got").frame(width: w)
                    }
                    .font(.system(size: 9.5)).foregroundStyle(dim)
                }
                .padding(.vertical, 3)
                .background(accent.opacity(dark ? 0.13 : 0.10))
                .overlay(alignment: .leading) { divider }
            }
        }
        .background(ink.opacity(0.05))
    }

    private func checkRow(_ c: CircuitCheck, _ outs: [Int], _ r: Int) -> some View {
        let w = Self.checkCell
        let wrong = c.wrongRows.contains(r)
        let tip: String = !wrong ? "" : "Row \(r): " + outs.filter { c.result[r][$0] == "x" }.map { k -> String in
            let got = table.rows[r][c.outputs[k].column!]
            return "\(c.outputs[k].name) should be \(c.expected[r][k]); the circuit gives \(got)"
        }.joined(separator: "; ")
        let stripe: Color = wrong ? Color.red.opacity(dark ? 0.2 : 0.12) : (r % 2 == 1 ? ink.opacity(0.03) : .clear)
        return HStack(spacing: 0) {
            ForEach(0..<n, id: \.self) { i in
                Text(String(table.rows[r][i])).font(.system(size: 13, design: .monospaced)).foregroundStyle(dim).frame(width: w, height: 24)
            }
            ForEach(outs, id: \.self) { k in checkPair(c, r, k) }
        }
        .background(stripe)
        .help(tip)
    }

    private func checkPair(_ c: CircuitCheck, _ r: Int, _ k: Int) -> some View {
        let w = Self.checkCell
        let asked: Character = c.expected[r][k]
        let result: Character = c.result[r][k]
        let got: Character = table.rows[r][c.outputs[k].column!]
        let gotColor: Color = result == "x" ? .red : result == "=" ? ink : dim
        return HStack(spacing: 0) {
            Text(asked == "-" ? "X" : String(asked))
                .font(.system(size: 13, design: .monospaced))
                .foregroundStyle(asked == "-" ? Color.orange : ink)
                .frame(width: w, height: 24)
            Text(String(got))
                .font(.system(size: 13, weight: result == "x" ? .bold : .regular, design: .monospaced))
                .foregroundStyle(gotColor)
                .frame(width: w, height: 24)
        }
        .overlay(alignment: .leading) { divider }
    }

    /// This circuit's table as text to edit: "A B | F", then a row each.
    private func circuitTableText() -> String {
        func line(_ cells: [String]) -> String {
            let ins: String = cells.prefix(n).joined(separator: " ")
            let outs: String = cells.dropFirst(n).joined(separator: " ")
            return ins + " | " + outs
        }
        var lines: [String] = [line(table.names)]
        for row in table.rows { lines.append(line(row.map { String($0) })) }
        return lines.joined(separator: "\n")
    }
}
