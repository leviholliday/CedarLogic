// Edit > Build from Formula…: type a formula (or a list of minterms), see
// what it means as you type, and build it as switches, gates and lights --
// as written or simplified, with any gates or only NAND or only NOR.

import SwiftUI

struct FormulaRequest: Identifiable {
    let id = UUID()
    var text: String
}

struct BuildFormulaView: View {
    @State var text: String
    let canvas: CanvasController
    @Environment(\.dismiss) private var dismiss
    @AppStorage("cl.buildShape") private var shape = BuildShape.asWritten.rawValue
    @AppStorage("cl.buildStyle") private var style = GateStyle.any.rawValue
    @AppStorage("cl.buildTwoInput") private var twoInputOnly = false
    @AppStorage("cl.buildNewPage") private var newPage = true
    @FocusState private var editing: Bool

    private var result: Result<ParsedFormulas, FormulaError> {
        do { return .success(try FormulaParser.parse(text)) } catch let e as FormulaError { return .failure(e) } catch {
            return .failure(FormulaError(message: "That couldn't be read."))
        }
    }

    private var chosenShape: BuildShape { BuildShape(rawValue: shape) ?? .asWritten }
    private var chosenStyle: GateStyle { GateStyle(rawValue: style) ?? .any }

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Color.clear.frame(height: 0).onEscape { dismiss() }
            Text("Build from Formula").font(.title2.weight(.semibold))
            Text("Switches for the variables, gates for the formula, and a light for each output, labelled and wired.")
                .font(.callout).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)

            TextEditor(text: $text)
                .font(.system(size: 15, design: .monospaced))
                .scrollContentBackground(.hidden)
                .padding(8)
                .frame(height: 78)
                .background(RoundedRectangle(cornerRadius: 8).fill(Color(nsColor: .textBackgroundColor)))
                .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(Color.secondary.opacity(0.3)))
                .overlay(alignment: .topLeading) {
                    if text.isEmpty {
                        Text("F = A'B + AC").font(.system(size: 15, design: .monospaced))
                            .foregroundStyle(.tertiary).padding(.leading, 13).padding(.top, 8)
                            .allowsHitTesting(false)
                    }
                }
                .focused($editing)
            Text("One output per line. NOT: A' or ~A · AND: AB, A·B or A*B · OR: A + B · XOR: A ⊕ B or A ^ B · or minterms: F(A,B,C) = Σm(1,3,5) + d(7)")
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)

            preview

            Grid(alignment: .leading, horizontalSpacing: 12, verticalSpacing: 10) {
                GridRow {
                    Text("Build it").gridColumnAlignment(.trailing)
                    Picker("", selection: $shape) {
                        ForEach(BuildShape.allCases) { Text($0.name).tag($0.rawValue) }
                    }
                    .labelsHidden().fixedSize()
                }
                GridRow {
                    Text("With")
                    HStack(spacing: 14) {
                        Picker("", selection: $style) {
                            ForEach(GateStyle.allCases) { Text($0.name).tag($0.rawValue) }
                        }
                        .pickerStyle(.segmented).labelsHidden().fixedSize()
                        Toggle("Only 2-input gates", isOn: $twoInputOnly)
                    }
                }
                GridRow {
                    Text("Put it")
                    Picker("", selection: $newPage) {
                        Text("On a new page").tag(true)
                        Text("Beside this page's circuit").tag(false)
                    }
                    .labelsHidden().fixedSize()
                }
            }

            HStack {
                Spacer()
                Button("Cancel") { dismiss() }.keyboardShortcut(.cancelAction)
                Button("Build") { build() }
                    .keyboardShortcut(.defaultAction)
                    .disabled((try? result.get()) == nil)
            }
        }
        .padding(20)
        .frame(width: 560)
        .onAppear { editing = true }
    }

    @ViewBuilder private var preview: some View {
        switch result {
        case .failure(let e):
            if text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                EmptyView()
            } else {
                Label(e.message, systemImage: "exclamationmark.circle")
                    .font(.callout).foregroundStyle(.orange)
                    .fixedSize(horizontal: false, vertical: true)
            }
        case .success(let f):
            VStack(alignment: .leading, spacing: 8) {
                Text(f.variables.isEmpty ? "No variables" :
                        "\(f.variables.count) variable\(f.variables.count == 1 ? "" : "s"): " + f.variables.joined(separator: ", "))
                    .font(.caption).foregroundStyle(.secondary)
                ForEach(f.functions.indices, id: \.self) { i in
                    let fn = f.functions[i]
                    let simplest = TwoLevel.simplest(.sumOfProducts, n: f.variables.count, values: fn.values)
                    HStack(alignment: .firstTextBaseline, spacing: 6) {
                        Text("Simplest:").font(.caption).foregroundStyle(.secondary)
                        FormulaText(name: fn.name, tokens: simplest.tokens(names: f.variables), size: 14)
                    }
                }
                Label(FormulaCircuit.plan(f, shape: chosenShape, style: chosenStyle, twoInputOnly: twoInputOnly).summary,
                      systemImage: "square.grid.3x3.middleleft.filled")
                    .font(.callout)
            }
            .padding(10)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(RoundedRectangle(cornerRadius: 8).fill(Color.secondary.opacity(0.07)))
        }
    }

    private func build() {
        guard case .success(let f) = result else { return }
        let plan = FormulaCircuit.plan(f, shape: chosenShape, style: chosenStyle, twoInputOnly: twoInputOnly)
        let name = f.functions.map(\.name).joined(separator: ", ")
        dismiss()
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) {
            if !canvas.build(plan, onNewPage: newPage, pageName: name) { NSSound.beep() }
        }
    }
}
