// Simulation View's control bar (GUICanvas::drawSimBarInto): a dark glass
// panel along the bottom with a breathing LIVE light, play/pause and step,
// the speed, a chip for every switch and light, and Done.

import SwiftUI

struct SimBar: View {
    let document: CoreDocument
    @ObservedObject var canvas: CanvasController
    let page: Int

    private let on = Color(.sRGB, red: 0.28, green: 0.93, blue: 1.0)
    private let ink = Color(.sRGB, red: 0.86, green: 0.93, blue: 1.0)
    private let dim = Color(.sRGB, red: 0.48, green: 0.58, blue: 0.68)
    private let live = Color(.sRGB, red: 0.36, green: 1.0, blue: 0.62)
    private let amber = Color(.sRGB, red: 1.0, green: 0.72, blue: 0.25)

    var body: some View {
        TimelineView(.animation(minimumInterval: 1.0 / 20)) { timeline in
            let t = timeline.date.timeIntervalSinceReferenceDate
            content(time: t)
        }
        .frame(maxWidth: 1040)
        .padding(.horizontal, 14)
        .padding(.bottom, 14)
    }

    private func content(time: Double) -> some View {
        let paused = !canvas.isRunning
        let breathe = paused ? 1.0 : 0.6 + 0.4 * sin(time * 3.2)
        let light = paused ? amber : live
        return HStack(spacing: 14) {
            // Status.
            HStack(spacing: 10) {
                ZStack {
                    Circle().fill(light.opacity(0.12 * breathe)).frame(width: 18, height: 18)
                    Circle().fill(light.opacity(0.55 + 0.45 * breathe)).frame(width: 8, height: 8)
                }
                VStack(alignment: .leading, spacing: 1) {
                    Text("SIMULATION").font(.system(size: 9, weight: .medium)).foregroundStyle(dim)
                    Text(paused ? "PAUSED" : "LIVE").font(.system(size: 13, weight: .semibold)).foregroundStyle(paused ? amber : ink)
                }
                .frame(width: 70, alignment: .leading)
            }
            divider
            HStack(spacing: 8) {
                barButton(help: paused ? "Resume (Space)" : "Pause (Space)", lit: paused) {
                    Image(systemName: paused ? "play.fill" : "pause.fill").foregroundStyle(paused ? on : ink)
                } action: { canvas.toggleRunning() }
                barButton(help: "Step once", lit: false) {
                    Image(systemName: "forward.frame.fill").foregroundStyle(ink)
                } action: { canvas.stepOnce() }
            }
            divider
            // Speed, fast on the right.
            VStack(alignment: .leading, spacing: 4) {
                Text("SPEED").font(.system(size: 9, weight: .medium)).foregroundStyle(dim)
                HStack(spacing: 12) {
                    SimSpeedSlider(stepMs: $canvas.stepMs, on: on)
                    Text("\(canvas.stepMs) ms / step").font(.system(size: 11)).monospacedDigit().foregroundStyle(ink)
                        .frame(width: 84, alignment: .leading)
                }
            }
            divider
            chips
            Spacer(minLength: 8)
            Button { canvas.simView = false } label: {
                HStack(spacing: 6) {
                    Text("Done").font(.system(size: 12)).foregroundStyle(ink)
                    Text("esc").font(.system(size: 9)).foregroundStyle(dim)
                }
                .padding(.horizontal, 13).frame(height: 30)
                .background(RoundedRectangle(cornerRadius: 9).fill(Color.white.opacity(0.07)))
                .overlay(RoundedRectangle(cornerRadius: 9).strokeBorder(Color.white.opacity(0.10)))
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
        }
        .padding(.horizontal, 18)
        .frame(height: 52)
        .background(RoundedRectangle(cornerRadius: 14).fill(Color(.sRGB, red: 0.055, green: 0.070, blue: 0.090, opacity: 0.94)))
        .overlay(RoundedRectangle(cornerRadius: 14).strokeBorder(on.opacity(0.22)))
        .shadow(color: .black.opacity(0.35), radius: 6, y: 3)
        .environment(\.colorScheme, .dark)
    }

    private var divider: some View { Rectangle().fill(Color.white.opacity(0.08)).frame(width: 1, height: 28) }

    private func barButton<L: View>(help: String, lit: Bool, @ViewBuilder label: () -> L, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            label()
                .font(.system(size: 13))
                .frame(width: 32, height: 32)
                .background(RoundedRectangle(cornerRadius: 9).fill(lit ? on.opacity(0.18) : Color.white.opacity(0.07)))
                .overlay(RoundedRectangle(cornerRadius: 9).strokeBorder(Color.white.opacity(0.08)))
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .help(help)
    }

    /// Every switch (IN) and light (OUT), top to bottom and left to right
    /// as drawn, lit when on.
    private var chips: some View {
        var buf = [CLSimChip](repeating: CLSimChip(), count: 64)
        let n = Int(cl_simview_chips(document.handle, Int32(page), &buf, Int32(buf.count)))
        let all = Array(buf.prefix(min(n, buf.count)))
        let ins = all.filter { $0.isInput }, outs = all.filter { !$0.isInput }
        return ViewThatFits(in: .horizontal) {
            chipRows(ins: ins, outs: outs, max: 24)
            chipRows(ins: ins, outs: outs, max: 12)
            chipRows(ins: ins, outs: outs, max: 6)
            EmptyView()
        }
    }

    private func chipRows(ins: [CLSimChip], outs: [CLSimChip], max: Int) -> some View {
        HStack(spacing: 14) {
            if !ins.isEmpty { chipRow("IN", Array(ins.prefix(max))) }
            if !outs.isEmpty { chipRow("OUT", Array(outs.prefix(max))) }
        }
        .fixedSize()
    }

    private func chipRow(_ label: String, _ chips: [CLSimChip]) -> some View {
        HStack(spacing: 5) {
            Text(label).font(.system(size: 9, weight: .medium)).foregroundStyle(dim).padding(.trailing, 3)
            ForEach(chips.indices, id: \.self) { i in
                RoundedRectangle(cornerRadius: 3)
                    .fill(chips[i].lit ? on : Color.white.opacity(0.10))
                    .frame(width: 10, height: 10)
                    .background(RoundedRectangle(cornerRadius: 5).fill(chips[i].lit ? on.opacity(0.16) : .clear).padding(-3))
            }
        }
    }
}

private struct SimSpeedSlider: View {
    @Binding var stepMs: Int
    let on: Color
    private let width: CGFloat = 150

    private var fraction: CGFloat { 1 - min(1, max(0, CGFloat(log(Double(max(1, stepMs))) / log(500)))) }

    var body: some View {
        ZStack(alignment: .leading) {
            Capsule().fill(Color.white.opacity(0.12)).frame(width: width, height: 4)
            Capsule().fill(on.opacity(0.75)).frame(width: max(4, width * fraction), height: 4)
            Circle().fill(on.opacity(0.14)).frame(width: 20, height: 20).offset(x: width * fraction - 10)
            Circle().fill(Color(.sRGB, red: 0.92, green: 1, blue: 1)).frame(width: 12, height: 12).offset(x: width * fraction - 6)
        }
        .frame(width: width, height: 20)
        .contentShape(Rectangle())
        .gesture(DragGesture(minimumDistance: 0).onChanged { v in
            let f = min(1, max(0, v.location.x / width))
            stepMs = max(1, min(500, Int(pow(500, 1 - Double(f)).rounded())))
        })
        .help("Simulation speed")
    }
}
