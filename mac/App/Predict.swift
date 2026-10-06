// Predict, then reveal (Simulation View): every light is covered with a "?",
// students guess what each will show, then Reveal says how they did. The
// covers, the rings and the wires that keep the answer to themselves are
// drawn by the core (cl_simview_draw_page, cl_simview_draw_predict); the
// guesses, the rounds, the keyboard and the screen reader live here.

import AppKit

/// One round of guesses. While the lights are covered the circuit can move on
/// (a switch, a keypad key, Step, Step Clock) and the guesses stay: "what will
/// Q be after the next edge?". Once revealed, the next such move starts a new
/// round (CanvasController.circuitAdvancedByUser), as does another page.
@MainActor
final class PredictModel: ObservableObject {
    /// The Predict toggle in the control bar.
    @Published var on = false { didSet { if on != oldValue { newRound() } } }
    @Published private(set) var revealed = false
    /// The guesses, by gate: 0 or 1 for a light, the number for a display.
    @Published private(set) var guesses: [Int: Int] = [:]
    /// The light the keyboard is on.
    @Published var focus: Int?
    /// What each light showed at Reveal (-1 when it wasn't a clean value).
    private(set) var answers: [Int: Int] = [:]

    var covering: Bool { on && !revealed }

    func newRound() {
        revealed = false
        guesses = [:]
        answers = [:]
    }

    func setGuess(_ gate: Int, _ value: Int?) {
        guard !revealed else { return }
        guesses[gate] = value
    }

    func reveal(_ lights: [CLSimLight]) {
        answers = Dictionary(lights.map { (Int($0.gate), Int($0.value)) }, uniquingKeysWith: { a, _ in a })
        revealed = true
    }

    /// How a light did: right, wrong or not guessed (after Reveal). A light
    /// with no clear value (unknown, floating, conflicting) can't be guessed
    /// right, so it gets its own mark and isn't scored.
    func mark(_ gate: Int) -> Int32 {
        guard revealed else { return Int32(CL_PREDICT_COVERED) }
        guard let answer = answers[gate], answer >= 0 else { return Int32(CL_PREDICT_UNCLEAR) }
        guard let g = guesses[gate] else { return Int32(CL_PREDICT_UNGUESSED) }
        return Int32(g == answer ? CL_PREDICT_RIGHT : CL_PREDICT_WRONG)
    }

    /// "3 of 4 right": counted against the lights that showed a clear value at Reveal.
    var right: Int { answers.filter { $0.value >= 0 && guesses[$0.key] == $0.value }.count }
    var total: Int { answers.filter { $0.value >= 0 }.count }
    /// What the bar and VoiceOver say after Reveal.
    var score: String { total == 0 ? "No clear values to score" : "\(right) of \(total) right" }
}

extension CanvasController {
    /// The page's lights and displays, top to bottom, left to right (the
    /// order Tab goes in).
    var predictLights: [CLSimLight] {
        guard let document else { return [] }
        var buf = [CLSimLight](repeating: CLSimLight(), count: 256)
        let n = Int(cl_simview_lights(document.handle, Int32(page), &buf, Int32(buf.count)))
        return Array(buf.prefix(min(n, buf.count)))
    }

    /// The lights are covered right now (Simulation View, Predict on, not revealed).
    var predictCovers: Bool { simView && predict.covering }

    /// The circuit moved on because someone acted: a switch flipped, a keypad
    /// key pressed, Step or Step Clock. While covered the guesses stay (they're
    /// about where the circuit goes next); after Reveal, Predict covers the
    /// lights again and clears the guesses for the next round.
    func circuitAdvancedByUser() {
        guard predict.on, predict.revealed else { return }
        predict.newRound()
        redraw()
        view?.predictChanged()
    }

    /// Another page: its lights get a round of their own.
    func predictPageChanged() {
        guard predict.on else { return }
        predict.newRound()
        predict.focus = nil
        redraw()
        view?.predictChanged()
        announce("Another page. The lights are covered: guess each one, then Reveal.")
    }

    func togglePredict() {
        predict.on.toggle()
        predict.focus = nil
        redraw()
        view?.predictChanged()
        announce(predict.on ? "Predict is on. The lights are covered: guess each one, then Reveal." : "Predict is off.")
    }

    /// Reveal, or (once revealed) cover the lights again for another round.
    func revealOrCoverAgain() {
        guard predict.on else { return }
        if predict.revealed {
            predict.newRound()
            announce("Covered again. Make your guesses.")
        } else {
            predict.reveal(predictLights)
            announce("\(predict.score).")
        }
        redraw()
        view?.predictChanged()
    }

    /// A click or tap on a covered light: cycles its guess (?, 0, 1 for a
    /// light; ?, 0...F for each digit of a display). Returns true when it
    /// took the click.
    func predictTap(at world: CGPoint) -> Bool {
        guard predictCovers, let document else { return false }
        var digit: Int32 = 0
        let gate = cl_simview_light_at(document.handle, Int32(page), world.x, world.y, &digit)
        guard gate >= 0 else { return false }
        predict.focus = gate
        cycleGuess(gate, digit: Int(digit))
        return true
    }

    private func light(_ gate: Int) -> CLSimLight? { predictLights.first { $0.gate == gate } }

    private func cycleGuess(_ gate: Int, digit: Int) {
        guard let l = light(gate) else { return }
        let old = predict.guesses[gate]
        if l.digits == 0 {
            predict.setGuess(gate, old == nil ? 0 : old == 0 ? 1 : nil)
        } else if let old {
            let n = Int(l.digits), shift = 4 * (n - 1 - min(max(digit, 0), n - 1))
            let d = (old >> shift) & 0xF
            if n == 1 && d == 15 {
                predict.setGuess(gate, nil)
            } else {
                predict.setGuess(gate, (old & ~(0xF << shift)) | (((d + 1) & 0xF) << shift))
            }
        } else {
            predict.setGuess(gate, 0)
        }
        redraw()
        view?.predictChanged()
    }

    /// 0/1 for a light, a hex digit for a display (a second digit shifts in,
    /// as on a calculator). Returns true when the key was a guess.
    func typePredictGuess(_ ch: Character) -> Bool {
        guard predictCovers, let digit = ch.hexDigitValue else { return false }
        let lights = predictLights
        guard !lights.isEmpty else { return false }
        if predict.focus == nil || !lights.contains(where: { $0.gate == predict.focus! }) { predict.focus = lights[0].gate }
        guard let gate = predict.focus, let l = lights.first(where: { $0.gate == gate }) else { return false }
        if l.digits == 0 {
            guard digit <= 1 else { return false }
            predict.setGuess(gate, digit)
        } else {
            let mask = (1 << (4 * Int(l.digits))) - 1
            predict.setGuess(gate, (((predict.guesses[gate] ?? 0) << 4) | digit) & mask)
        }
        redraw()
        view?.predictChanged()
        return true
    }

    func clearPredictGuess() {
        guard predictCovers, let gate = predict.focus else { return }
        predict.setGuess(gate, nil)
        redraw()
        view?.predictChanged()
    }

    /// Tab, Shift-Tab and the arrows: the next or previous light.
    func movePredictFocus(_ delta: Int) {
        let lights = predictLights
        guard !lights.isEmpty else { return }
        let at = predict.focus.flatMap { f in lights.firstIndex { $0.gate == f } }
        let next = at.map { (($0 + delta) % lights.count + lights.count) % lights.count } ?? (delta > 0 ? 0 : lights.count - 1)
        predict.focus = lights[next].gate
        redraw()
        view?.predictChanged()
        announce(predictDescription(lights[next]))
    }

    /// What's drawn over each light.
    var predictMarks: [CLPredictMark] {
        predictLights.map { l in
            let g = Int(l.gate)
            return CLPredictMark(gate: l.gate, guess: Int32(predict.guesses[g] ?? -1), mark: predict.mark(g),
                                 focused: predict.focus == g)
        }
    }

    // MARK: Screen readers

    func predictName(_ l: CLSimLight) -> String {
        guard let document else { return "" }
        var buf = [CChar](repeating: 0, count: 64)
        _ = cl_simview_light_name(document.handle, Int32(page), l.gate, &buf, Int32(buf.count))
        let name = String(cString: buf)
        let kind = l.digits == 0 ? "Light" : "Display"
        return name.isEmpty ? kind : "\(kind) \(name)"
    }

    private func text(_ value: Int, _ l: CLSimLight) -> String {
        l.digits == 0 ? String(value) : String(format: "%0\(l.digits)X", value)
    }

    func predictDescription(_ l: CLSimLight) -> String {
        let name = predictName(l), g = Int(l.gate)
        let guess = predict.guesses[g]
        if !predict.revealed {
            return guess.map { "\(name), covered, your guess \(text($0, l))" } ?? "\(name), covered, no guess yet"
        }
        let answer = predict.answers[g] ?? -1
        let shows = answer >= 0 ? "shows \(text(answer, l))" : "shows no clear value"
        guard let guess else { return "\(name) \(shows), not guessed" }
        if answer < 0 { return "\(name) \(shows), your guess \(text(guess, l)), not scored" }
        return "\(name) \(shows), your guess \(text(guess, l)), \(guess == answer ? "right" : "wrong")"
    }

    func announce(_ message: String) {
        guard let window = view?.window else { return }
        NSAccessibility.post(element: window, notification: .announcementRequested,
                             userInfo: [.announcement: message, .priority: NSAccessibilityPriorityLevel.high.rawValue])
    }
}

/// A light, for VoiceOver: a button that cycles the guess while covered.
final class PredictLightElement: NSAccessibilityElement {
    weak var controller: CanvasController?
    var gate = 0

    override func accessibilityPerformPress() -> Bool {
        MainActor.assumeIsolated { press() }
    }

    @MainActor private func press() -> Bool {
        guard let controller, controller.predictCovers, let l = controller.predictLights.first(where: { $0.gate == gate }) else { return false }
        controller.predict.focus = gate
        _ = controller.predictTap(at: CGPoint(x: (l.left + l.right) / 2, y: (l.bottom + l.top) / 2))
        controller.announce(controller.predictDescription(l))
        return true
    }
}

extension CircuitCanvasNSView {
    /// While Predict is on, the canvas offers its lights to screen readers.
    func predictAccessibilityChildren() -> [Any]? {
        guard let controller, controller.simView, controller.predict.on else { return nil }
        return controller.predictLights.map { l in
            let e = PredictLightElement()
            e.controller = controller
            e.gate = Int(l.gate)
            e.setAccessibilityParent(self)
            e.setAccessibilityRole(.button)
            e.setAccessibilityLabel(controller.predictDescription(l))
            e.setAccessibilityHelp(controller.predictCovers ? "Press to change your guess: 0, 1, or none" : "")
            e.setAccessibilityFrameInParentSpace(viewRect(CGRect(x: l.left, y: l.bottom, width: l.right - l.left,
                                                                 height: l.top - l.bottom)).insetBy(dx: -4, dy: -4))
            return e
        }
    }

    func predictChanged() {
        needsDisplay = true
        NSAccessibility.post(element: self, notification: .layoutChanged)
    }
}
