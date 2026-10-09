// Simulation View's control bar (GUICanvas::drawSimBarInto): a dark glass
// panel along the bottom with a breathing LIVE light, play/pause, step and
// Step Clock, the speed, Predict (and Reveal), a chip for every switch and
// light, projector mode and Done. In projector mode everything is bigger.

import SwiftUI

struct SimBar: View {
    let document: CoreDocument
    @ObservedObject var canvas: CanvasController
    let page: Int
    @ObservedObject private var predict: PredictModel
    @ObservedObject private var prefs = Prefs.shared

    init(document: CoreDocument, canvas: CanvasController, page: Int) {
        self.document = document
        self.canvas = canvas
        self.page = page
        predict = canvas.predict
    }

    /// Projector mode's scale for the bar: readable from the back of the room.
    private var k: CGFloat { prefs.projector ? 1.3 : 1 }
    /// Room for Predict's controls: the speed loses its words.
    private var compact: Bool { predict.on || prefs.projector }
    /// Projector mode and Predict together, with Step Clock beside Step: the
    /// gaps close up and the speed gets shorter, so the bar still fits the
    /// canvas of a default-size window (1280 points, palette open).
    private var crowded: Bool { predict.on && prefs.projector }

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
        .frame(maxWidth: 1040 * k)
        .padding(.horizontal, 14)
        .padding(.bottom, 14)
    }

    private func content(time: Double) -> some View {
        let paused = !canvas.isRunning
        let breathe = paused ? 1.0 : 0.6 + 0.4 * sin(time * 3.2)
        let light = paused ? amber : live
        return HStack(spacing: crowded ? 12 : 14 * k) {
            // Status.
            HStack(spacing: 10 * k) {
                ZStack {
                    Circle().fill(light.opacity(0.12 * breathe)).frame(width: 18 * k, height: 18 * k)
                    Circle().fill(light.opacity(0.55 + 0.45 * breathe)).frame(width: 8 * k, height: 8 * k)
                }
                VStack(alignment: .leading, spacing: 1) {
                    Text("SIMULATION").font(.system(size: 9 * k, weight: .medium)).foregroundStyle(dim)
                    Text(paused ? "PAUSED" : "LIVE").font(.system(size: 13 * k, weight: .semibold)).foregroundStyle(paused ? amber : ink)
                }
                .frame(width: 70 * k, alignment: .leading)
            }
            divider
            HStack(spacing: 8 * k) {
                barButton(help: paused ? "Resume (Space)" : "Pause (Space)", lit: paused) {
                    Image(systemName: paused ? "play.fill" : "pause.fill").foregroundStyle(paused ? on : ink)
                } action: { canvas.toggleRunning() }
                barButton(help: "Step once", lit: false) {
                    Image(systemName: "forward.frame.fill").foregroundStyle(ink)
                } action: { canvas.stepOnce() }
                let clockReady = canvas.hasManualClock
                if prefs.showStepClock {
                    barButton(help: clockReady ? stepClockTip : CanvasController.stepClockOffTip, lit: false, disabled: !clockReady) {
                        Image(systemName: "clock.arrow.2.circlepath").foregroundStyle(ink.opacity(clockReady ? 1 : 0.35))
                    } action: { canvas.perform(.stepClock) }
                }
            }
            divider
            // Speed, fast on the right.
            VStack(alignment: .leading, spacing: 4) {
                Text("SPEED").font(.system(size: 9 * k, weight: .medium)).foregroundStyle(dim)
                HStack(spacing: 12) {
                    SimSpeedSlider(stepMs: $canvas.stepMs, on: on, scale: k, width: crowded ? 84 : compact ? 110 : 150)
                    if !compact {
                        Text("\(canvas.stepMs) ms / step").font(.system(size: 11 * k)).monospacedDigit().foregroundStyle(ink)
                            .frame(width: 84 * k, alignment: .leading)
                    }
                }
            }
            divider
            predictControls
            // Projector mode: no room for the chips, and the lights are big on the page.
            if !prefs.projector {
                divider
                chips
            }
            Spacer(minLength: 8)
            // Draw on the circuit: a teacher marks it up as it runs.
            barButton(help: canvas.drawing ? "Done drawing (Esc)" : "Draw on the circuit", lit: canvas.drawing) {
                Image(systemName: "pencil.tip.crop.circle").foregroundStyle(canvas.drawing ? on : ink)
            } action: { canvas.toggleDrawing() }
            .accessibilityLabel("Draw on the circuit")
            .accessibilityValue(canvas.drawing ? "On" : "Off")
            barButton(help: prefs.projector ? "Leave projector mode" : "Projector mode: thick wires, big labels and lights, for the back of the room",
                      lit: prefs.projector) {
                Image(systemName: prefs.projector ? "videoprojector.fill" : "videoprojector").foregroundStyle(prefs.projector ? on : ink)
            } action: { prefs.projector.toggle() }
            .accessibilityLabel("Projector mode")
            .accessibilityValue(prefs.projector ? "On" : "Off")
            textButton("Done", key: "esc") { canvas.simView = false }
        }
        .padding(.horizontal, 18 * k)
        .frame(height: 52 * k)
        .background(RoundedRectangle(cornerRadius: 14 * k).fill(Color(.sRGB, red: 0.055, green: 0.070, blue: 0.090, opacity: 0.94)))
        .overlay(RoundedRectangle(cornerRadius: 14 * k).strokeBorder(on.opacity(prefs.projector ? 0.4 : 0.22)))
        .shadow(color: .black.opacity(0.35), radius: 6, y: 3)
        .environment(\.colorScheme, .dark)
    }

    private var divider: some View { Rectangle().fill(Color.white.opacity(0.08)).frame(width: 1, height: 28 * k) }

    private var stepClockTip: String {
        if let c = ShortcutStore.shared.combo(.stepClock) { return "Step Clock (\(c.label))" }
        return "Step Clock"
    }

    private func barButton<L: View>(help: String, lit: Bool, disabled: Bool = false, @ViewBuilder label: () -> L,
                                    action: @escaping () -> Void) -> some View {
        // A disabled one ignores clicks but isn't .disabled, so its tip (what
        // turns it on) still shows; VoiceOver hears it's unavailable, and why.
        Button { if !disabled { action() } } label: {
            label()
                .font(.system(size: 13 * k))
                .frame(width: 32 * k, height: 32 * k)
                .background(RoundedRectangle(cornerRadius: 9 * k).fill(lit ? on.opacity(0.18) : Color.white.opacity(0.07)))
                .overlay(RoundedRectangle(cornerRadius: 9 * k).strokeBorder(Color.white.opacity(0.08)))
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .help(help)
        .accessibilityValue(disabled ? "Unavailable" : "")
        .accessibilityHint(disabled ? help : "")
    }

    /// A button with a word on it (Done, Reveal) and its key, small, beside it.
    private func textButton(_ title: String, key: String, lit: Bool = false, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack(spacing: 6 * k) {
                Text(title).font(.system(size: 12 * k, weight: lit ? .semibold : .regular)).foregroundStyle(lit ? on : ink)
                Text(key).font(.system(size: 9 * k)).foregroundStyle(dim)
            }
            .fixedSize()
            .padding(.horizontal, 13 * k).frame(height: 30 * k)
            .background(RoundedRectangle(cornerRadius: 9 * k).fill(lit ? on.opacity(0.16) : Color.white.opacity(0.07)))
            .overlay(RoundedRectangle(cornerRadius: 9 * k).strokeBorder(lit ? on.opacity(0.45) : Color.white.opacity(0.10)))
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }

    /// Predict, then reveal: the toggle, then Reveal (or Cover again) and
    /// how the round is going.
    private var predictControls: some View {
        let lights = canvas.predictLights
        let guessed = lights.filter { predict.guesses[Int($0.gate)] != nil }.count
        return HStack(spacing: 8 * k) {
            barButton(help: lights.isEmpty ? "Predict needs a light on this page to guess"
                                           : predict.on ? "Stop predicting: show the lights again"
                                                        : "Predict, then reveal: cover the lights, guess each one (click it, or Tab and 0 or 1), then Reveal",
                      lit: predict.on) {
                Image(systemName: "questionmark.square.dashed").foregroundStyle(predict.on ? on : ink)
            } action: { canvas.togglePredict() }
            .disabled(lights.isEmpty && !predict.on)
            .opacity(lights.isEmpty && !predict.on ? 0.45 : 1)
            .accessibilityLabel("Predict")
            .accessibilityValue(predict.on ? "On" : "Off")
            if predict.on {
                if !predict.locked {
                    textButton(predict.revealed ? "Cover again" : "Reveal", key: "return", lit: !predict.revealed && guessed > 0) {
                        canvas.revealOrCoverAgain()
                    }
                    .help(predict.revealed ? "Cover the lights and guess again" : "Uncover the lights and see how you did")
                }
                Text(predict.revealed ? predict.score : "\(guessed) of \(lights.count) guessed")
                    .font(.system(size: 12 * k, weight: predict.revealed ? .semibold : .regular)).monospacedDigit()
                    .foregroundStyle(predict.revealed ? (predict.total > 0 && predict.right == predict.total ? live : ink) : dim)
                    .fixedSize()
                    .accessibilityAddTraits(.updatesFrequently)
            } else if !prefs.projector {
                Text("Predict").font(.system(size: 11 * k)).foregroundStyle(dim).fixedSize()
                    .accessibilityHidden(true)
            }
        }
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
        // Predict: the lights' chips would give the answers away.
        let hidden = label == "OUT" && canvas.predictCovers
        return HStack(spacing: 5 * k) {
            Text(label).font(.system(size: 9 * k, weight: .medium)).foregroundStyle(dim).padding(.trailing, 3)
            ForEach(chips.indices, id: \.self) { i in
                let lit = chips[i].lit && !hidden
                RoundedRectangle(cornerRadius: 3 * k)
                    .fill(lit ? on : Color.white.opacity(0.10))
                    .frame(width: 10 * k, height: 10 * k)
                    .overlay { if hidden { Text("?").font(.system(size: 7 * k, weight: .bold)).foregroundStyle(dim) } }
                    .background(RoundedRectangle(cornerRadius: 5 * k).fill(lit ? on.opacity(0.16) : .clear).padding(-3))
            }
        }
    }
}

private struct SimSpeedSlider: View {
    @Binding var stepMs: Int
    let on: Color
    var scale: CGFloat = 1
    var width: CGFloat = 150
    private var w: CGFloat { width * scale }

    private var fraction: CGFloat { 1 - min(1, max(0, CGFloat(log(Double(max(1, stepMs))) / log(500)))) }

    var body: some View {
        ZStack(alignment: .leading) {
            Capsule().fill(Color.white.opacity(0.12)).frame(width: w, height: 4 * scale)
            Capsule().fill(on.opacity(0.75)).frame(width: max(4, w * fraction), height: 4 * scale)
            Circle().fill(on.opacity(0.14)).frame(width: 20 * scale, height: 20 * scale).offset(x: w * fraction - 10 * scale)
            Circle().fill(Color(.sRGB, red: 0.92, green: 1, blue: 1)).frame(width: 12 * scale, height: 12 * scale).offset(x: w * fraction - 6 * scale)
        }
        .frame(width: w, height: 20 * scale)
        .contentShape(Rectangle())
        .gesture(DragGesture(minimumDistance: 0).onChanged { v in
            let f = min(1, max(0, v.location.x / w))
            stepMs = max(1, min(500, Int(pow(500, 1 - Double(f)).rounded())))
        })
        .help("Simulation speed: \(stepMs) ms a step")
    }
}
