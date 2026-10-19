// `CedarLogic --leak-check`: a circuit window's controller goes when its
// window does. One that stays keeps its circuit and its 60-a-second clock
// running for as long as the app is open (the simulation of a circuit
// nobody can see any more), and every closed or replaced window adds one.
// Headless: controllers attached to a circuit, used the ways that store a
// controller somewhere (Add a Gate, a split's second side), then let go.
// PASS or FAIL for each; exit 1 on a failure.

import AppKit

@MainActor
enum LeakCheck {
    static func runIfAsked() {
        guard CommandLine.arguments.contains("--leak-check") else { return }
        var failures = 0
        func turn() { for _ in 0..<3 { RunLoop.main.run(mode: .default, before: Date()) } }
        func check(_ what: String, _ use: (CanvasController, CanvasController) -> Void) {
            weak var gone: CanvasController?, goneSide: CanvasController?, goneDoc: CoreDocument?
            autoreleasepool {
                let doc = CoreDocument()
                let main = CanvasController(), side = CanvasController()
                side.drivesClock = false
                main.partner = side
                side.partner = main
                main.attach(doc)
                side.attach(doc)
                use(main, side)
                turn()
                gone = main; goneSide = side; goneDoc = doc
            }
            turn()
            let ok = gone == nil && goneSide == nil && goneDoc == nil
            print((ok ? "PASS " : "FAIL ") + what + (ok ? "" : ": still there (controller \(gone != nil), second side \(goneSide != nil), circuit \(goneDoc != nil))"))
            if !ok { failures += 1 }
        }
        check("a window that was only looked at") { _, _ in }
        check("after Add a Gate (A)") { main, _ in main.perform(.addGate) }
        check("after Add a Gate on a split's second side") { _, side in side.perform(.addGate) }
        check("after Simulation View and the scope") { main, _ in main.simView = true; main.showScope = true }
        print(failures == 0 ? "leak-check: all passed" : "leak-check: \(failures) failed")
        exit(failures == 0 ? 0 : 1)
    }
}
