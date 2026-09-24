// The oscilloscope, redesigned: every signal a TO label names, drawn as a
// clean waveform under the canvas, with a time cursor that reads every
// signal's value at once, zoom and scroll through time, and the keyboard to
// drive it:
//   Left/Right          move the cursor a step (Shift: 10 steps)
//   Option-Left/Right   jump to the previous/next change on the chosen signal
//   Up/Down             choose a signal
//   + / -               zoom in and out in time
//   Home / End          first sample / follow the live end again
//   H                   hide or show the chosen signal
//   Space               run or pause the simulation
//   C                   clear the recording

import AppKit
import SwiftUI

@MainActor
final class ScopeModel: ObservableObject {
    @Published var pointsPerStep: CGFloat = 6
    /// The cursor's sample index; nil follows the live end.
    @Published var cursor: Int?
    @Published var chosen = 0
    @Published var hidden: Set<String> = []

    func clampZoom() { pointsPerStep = min(max(pointsPerStep, 0.25), 48) }
}

struct ScopeView: View {
    let document: CoreDocument
    @ObservedObject var canvas: CanvasController
    @StateObject private var model = ScopeModel()
    @FocusState private var focused: Bool

    private let nameWidth: CGFloat = 130
    private let laneHeight: CGFloat = 30
    private let rulerHeight: CGFloat = 22

    var body: some View {
        let _ = canvas.scopeVersion   // redraw as the simulation records
        let signals = (0..<Int(cl_scope_signal_count(document.handle))).map { String(cString: cl_scope_signal(document.handle, Int32($0))) }
        let length = Int(cl_scope_length(document.handle))
        VStack(spacing: 0) {
            header(signals: signals, length: length)
            Divider()
            if signals.isEmpty {
                Text("Add a TO label to a wire, and its signal shows up here.")
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                GeometryReader { geo in
                    let shown = signals.enumerated().filter { !model.hidden.contains($0.element) }
                    ScrollView(.vertical) {
                        Canvas { ctx, size in
                            draw(ctx, size: size, shown: shown, length: length)
                        }
                        .frame(height: rulerHeight + CGFloat(shown.count) * laneHeight)
                        .contentShape(Rectangle())
                        .gesture(DragGesture(minimumDistance: 0).onChanged { v in
                            focused = true
                            setCursor(atX: v.location.x, width: geo.size.width, length: length)
                        })
                    }
                }
            }
        }
        .background(Color(nsColor: .textBackgroundColor))
        .focusable()
        .focused($focused)
        .focusEffectDisabled()
        .onKeyPress(phases: .down) { press in handle(press, signals: signals, length: length) }
    }

    // MARK: Header

    private func header(signals: [String], length: Int) -> some View {
        HStack(spacing: 10) {
            Image(systemName: "waveform.path.ecg").foregroundStyle(.secondary)
            Text("Oscilloscope").font(.headline)
            if let c = model.cursor, c < length {
                Text("step \(Int(cl_scope_first_step(document.handle)) + c)")
                    .monospacedDigit().foregroundStyle(.secondary)
            } else {
                Text("live").foregroundStyle(.secondary)
            }
            Spacer()
            if !model.hidden.isEmpty {
                Menu("\(model.hidden.count) hidden") {
                    ForEach(Array(model.hidden).sorted(), id: \.self) { n in
                        Button("Show \(n)") { model.hidden.remove(n) }
                    }
                    Button("Show All") { model.hidden.removeAll() }
                }
                .fixedSize()
            }
            Button { model.pointsPerStep /= 1.5; model.clampZoom() } label: { Image(systemName: "minus.magnifyingglass") }
                .help("Zoom out in time (−)")
            Button { model.pointsPerStep *= 1.5; model.clampZoom() } label: { Image(systemName: "plus.magnifyingglass") }
                .help("Zoom in in time (+)")
            Button { model.cursor = nil } label: { Image(systemName: "arrow.right.to.line") }
                .help("Follow the live end (End)")
            Button { cl_scope_clear(document.handle); model.cursor = nil; canvas.scopeChanged() } label: { Image(systemName: "trash") }
                .help("Clear the recording (C)")
        }
        .buttonStyle(.borderless)
        .padding(.horizontal, 10).padding(.vertical, 6)
    }

    // MARK: Drawing

    /// The sample range on screen: ending at the cursor's page, or live.
    private func window(width: CGFloat, length: Int) -> (start: Int, count: Int) {
        let count = max(1, Int((width - nameWidth) / model.pointsPerStep))
        var end = length
        if let c = model.cursor, c < length - count / 2 { end = min(length, max(c + count / 2, count)) }
        return (max(0, end - count), count)
    }

    private func draw(_ ctx: GraphicsContext, size: CGSize, shown: [(offset: Int, element: String)], length: Int) {
        let (start, count) = window(width: size.width, length: length)
        let pps = model.pointsPerStep
        let x0 = nameWidth
        func x(_ i: Int) -> CGFloat { x0 + CGFloat(i - start) * pps }

        // Ruler: a tick every so many steps, labelled with the step number.
        let firstStep = Int(cl_scope_first_step(document.handle))
        let every = [1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000].first { CGFloat($0) * pps >= 60 } ?? 10000
        var i = start - ((start + firstStep) % every)
        while i <= start + count {
            if i >= start {
                ctx.stroke(Path { $0.move(to: CGPoint(x: x(i), y: rulerHeight - 6)); $0.addLine(to: CGPoint(x: x(i), y: size.height)) },
                           with: .color(.secondary.opacity(0.15)), lineWidth: 1)
                ctx.draw(Text("\(i + firstStep)").font(.caption2).monospacedDigit().foregroundColor(.secondary),
                         at: CGPoint(x: x(i) + 2, y: 4), anchor: .topLeading)
            }
            i += every
        }

        var buffer = [UInt8](repeating: 255, count: count + 1)
        let cursorIndex = model.cursor.map { min($0, length - 1) } ?? (length - 1)
        for (row, sig) in shown.enumerated() {
            let top = rulerHeight + CGFloat(row) * laneHeight
            let hi = top + 7, lo = top + laneHeight - 7
            let isChosen = sig.offset == model.chosen
            if isChosen {
                ctx.fill(Path(CGRect(x: 0, y: top, width: size.width, height: laneHeight)), with: .color(.accentColor.opacity(0.07)))
            }
            // Name and the value under the cursor.
            var value: UInt8 = 255
            _ = cl_scope_samples(document.handle, Int32(sig.offset), Int64(cursorIndex), 1, &value)
            ctx.draw(Text(sig.element).font(.callout.weight(isChosen ? .semibold : .regular)),
                     at: CGPoint(x: 10, y: top + laneHeight / 2), anchor: .leading)
            ctx.draw(Text(label(value)).font(.callout.monospacedDigit()).foregroundColor(color(value)),
                     at: CGPoint(x: nameWidth - 10, y: top + laneHeight / 2), anchor: .trailing)

            // The trace, as runs of equal samples.
            let n = Int(cl_scope_samples(document.handle, Int32(sig.offset), Int64(start), Int32(count), &buffer))
            var runStart = 0
            var path = Path()
            var lastY: CGFloat?
            while runStart < n {
                let v = buffer[runStart]
                var runEnd = runStart + 1
                while runEnd < n && buffer[runEnd] == v { runEnd += 1 }
                let a = x(start + runStart), b = x(start + runEnd)
                switch v {
                case 0, 1:
                    let y = v == 1 ? hi : lo
                    if let ly = lastY { path.move(to: CGPoint(x: a, y: ly)); path.addLine(to: CGPoint(x: a, y: y)) }
                    path.move(to: CGPoint(x: a, y: y)); path.addLine(to: CGPoint(x: b, y: y))
                    lastY = y
                    if v == 1 {
                        ctx.fill(Path(CGRect(x: a, y: hi, width: b - a, height: lo - hi)), with: .color(Color.green.opacity(0.10)))
                    }
                case 2:   // high-Z: a line in the middle
                    ctx.stroke(Path { $0.move(to: CGPoint(x: a, y: (hi + lo) / 2)); $0.addLine(to: CGPoint(x: b, y: (hi + lo) / 2)) },
                               with: .color(.blue), lineWidth: 1.5)
                    lastY = nil
                case 3, 4:   // conflict / unknown: a shaded band
                    ctx.fill(Path(CGRect(x: a, y: hi, width: b - a, height: lo - hi)),
                             with: .color((v == 3 ? Color.red : Color.orange).opacity(0.25)))
                    lastY = nil
                default:
                    lastY = nil
                }
                runStart = runEnd
            }
            ctx.stroke(path, with: .color(Color.green), lineWidth: 1.6)
            ctx.stroke(Path { $0.move(to: CGPoint(x: 0, y: top + laneHeight)); $0.addLine(to: CGPoint(x: size.width, y: top + laneHeight)) },
                       with: .color(.secondary.opacity(0.12)), lineWidth: 1)
        }

        // Names column edge, and the cursor.
        ctx.stroke(Path { $0.move(to: CGPoint(x: nameWidth, y: 0)); $0.addLine(to: CGPoint(x: nameWidth, y: size.height)) },
                   with: .color(.secondary.opacity(0.25)), lineWidth: 1)
        if let c = model.cursor, c >= start, c <= start + count {
            let cx = x(c) + pps / 2
            ctx.stroke(Path { $0.move(to: CGPoint(x: cx, y: 0)); $0.addLine(to: CGPoint(x: cx, y: size.height)) },
                       with: .color(.accentColor), lineWidth: 1.5)
        }
    }

    private func label(_ v: UInt8) -> String {
        switch v { case 0: "0"; case 1: "1"; case 2: "Z"; case 3: "!"; case 4: "?"; default: "–" }
    }

    private func color(_ v: UInt8) -> Color {
        switch v { case 1: .green; case 2: .blue; case 3: .red; case 4: .orange; default: .secondary }
    }

    // MARK: Input

    private func setCursor(atX px: CGFloat, width: CGFloat, length: Int) {
        guard length > 0, px > nameWidth else { return }
        let (start, _) = window(width: width, length: length)
        model.cursor = min(length - 1, max(0, start + Int((px - nameWidth) / model.pointsPerStep)))
    }

    private func handle(_ press: KeyPress, signals: [String], length: Int) -> KeyPress.Result {
        guard length > 0 || press.characters == " " else { return .ignored }
        let current = model.cursor ?? (length - 1)
        let shift = press.modifiers.contains(.shift), option = press.modifiers.contains(.option)
        switch press.key {
        case .leftArrow:
            model.cursor = option ? change(from: current, forward: false) : max(0, current - (shift ? 10 : 1))
        case .rightArrow:
            let next = option ? change(from: current, forward: true) : current + (shift ? 10 : 1)
            model.cursor = next >= length - 1 ? nil : next
        case .upArrow:
            model.chosen = max(0, model.chosen - 1)
        case .downArrow:
            model.chosen = min(signals.count - 1, model.chosen + 1)
        case .home:
            model.cursor = 0
        case .end:
            model.cursor = nil
        default:
            switch press.characters {
            case "+", "=": model.pointsPerStep *= 1.5; model.clampZoom()
            case "-": model.pointsPerStep /= 1.5; model.clampZoom()
            case "h", "H":
                if signals.indices.contains(model.chosen) {
                    let n = signals[model.chosen]
                    if model.hidden.contains(n) { model.hidden.remove(n) } else { model.hidden.insert(n) }
                }
            case "c", "C": cl_scope_clear(document.handle); model.cursor = nil; canvas.scopeChanged()
            case " ": canvas.toggleRunning()
            default: return .ignored
            }
        }
        return .handled
    }

    /// The nearest sample where the chosen signal changes, looking one way.
    private func change(from index: Int, forward: Bool) -> Int {
        let length = Int(cl_scope_length(document.handle))
        guard length > 1 else { return index }
        var all = [UInt8](repeating: 0, count: length)
        _ = cl_scope_samples(document.handle, Int32(model.chosen), 0, Int32(length), &all)
        let here = all[min(max(index, 0), length - 1)]
        if forward {
            var i = index + 1
            while i < length && all[i] == here { i += 1 }
            return min(i, length - 1)
        } else {
            var i = index - 1
            while i > 0 && all[i] == here { i -= 1 }
            // Land on the first sample of the earlier run's value change.
            return max(i, 0)
        }
    }
}
