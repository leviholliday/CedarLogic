// `CedarLogic --undo-check`: the engine's undo steps reach macOS's undo
// manager (CanvasController.mirrorNewSteps), which is what makes a window
// edited, autosaved and asked about on closing. Headless: a circuit, its
// controller and a document's undo manager, no window. Edits past the
// engine's 500-step history must still mark the document edited (they once
// didn't, so nothing after the 500th step was saved); undo and redo still
// work after that. PASS or FAIL for each; exit 1 on a failure.

import AppKit

@MainActor
enum UndoCheck {
    static func runIfAsked() {
        guard CommandLine.arguments.contains("--undo-check") else { return }
        var failures = 0
        func check(_ ok: Bool, _ what: String) {
            print((ok ? "PASS " : "FAIL ") + what)
            if !ok { failures += 1 }
        }
        let core = CoreDocument()
        let canvas = CanvasController()
        canvas.attach(core)
        // A plain NSDocument keeps the edited state from its undo manager,
        // as the circuit's window does.
        let file = NSDocument()
        // Grouped by event, as in a window: a group opens only when something
        // is registered, and closes as the run loop turns.
        let um = UndoManager()
        file.undoManager = um
        canvas.undoManager = um
        func turn() { RunLoop.main.run(mode: .default, before: Date()) }

        var placed = 0
        func edit() {
            let x = CGFloat(placed % 40) * 8, y = CGFloat(placed / 40) * 8
            if core.addGate("AA_AND2", page: 0, at: CGPoint(x: x, y: y)) { placed += 1 }
            canvas.edited()
            turn()
        }
        /// As a save does: the engine's text written, the file's change count cleared.
        func save() {
            _ = core.saveText()
            file.updateChangeCount(.changeCleared)
        }

        for _ in 0..<10 { edit() }
        check(file.isDocumentEdited && core.undoCount == 10, "10 edits: edited, 10 steps")
        save()
        for i in 0..<(CanvasController.engineHistoryLimit + 20) {
            edit()
            if i == CanvasController.engineHistoryLimit + 5 { save() }
        }
        check(core.undoCount == CanvasController.engineHistoryLimit, "the engine's history is full (\(core.undoCount) steps)")
        check(file.isDocumentEdited, "edits after a save at a full history mark it edited")
        // Each edit at a full history: saved, then one more edit.
        var marked = 0
        for _ in 0..<30 {
            save()
            edit()
            if file.isDocumentEdited { marked += 1 }
        }
        check(marked == 30, "every edit at a full history marks it edited (\(marked) of 30)")
        // Undo and redo still go through to the engine.
        let gates = { Int(cl_document_gate_count(core.handle, 0)) }
        let count0 = gates()
        um.undo(); turn()
        check(gates() == count0 - 1, "undo takes back the last gate")
        um.redo(); turn()
        check(gates() == count0, "redo puts it back")
        for _ in 0..<5 { um.undo(); turn() }
        check(gates() == count0 - 5 && core.canRedo, "five undos take back five gates")
        edit()
        check(gates() == count0 - 4 && !um.canRedo && !core.canRedo, "a new edit after undos drops redo on both sides")
        print(failures == 0 ? "undo-check: all passed" : "undo-check: \(failures) failed")
        exit(failures == 0 ? 0 : 1)
    }
}
