// One circuit's window: the CedarLogic layout (CLWindow.swift), plus the
// sheets every circuit window can raise (truth table, load notices) and the
// "replace the current circuit" behaviour of Settings > General.

import SwiftUI

struct CircuitWindow: View {
    let document: CoreDocument
    @EnvironmentObject private var look: LookStore
    @StateObject private var canvas = CanvasController()
    @Environment(\.undoManager) private var undoManager
    @State private var page = 0
    @State private var notices: [(text: String, warning: Bool)] = []
    @State private var showNotices = false
    @State private var replaceChecked = false
    @ObservedObject private var prefs = Prefs.shared

    private var theme: Theme { look.settings.theme }

    var body: some View {
        CLLayout(document: document, canvas: canvas, page: $page)
        .onReceive(NotificationCenter.default.publisher(for: NSWindow.didBecomeKeyNotification)) { n in
            if (n.object as? NSWindow) === canvas.view?.window {
                CanvasController.front = canvas
                LastCircuit.note(canvas.view?.window)
            }
        }
        .focusedSceneObject(canvas)
        .tint(theme.accent.color)
        .onChange(of: undoManager) { _, um in canvas.undoManager = um }
        .onAppear {
            let previous = CanvasController.front
            CanvasController.front = canvas
            if !replaceChecked {
                replaceChecked = true
                if prefs.openReplaces, !TourModel.shared.waitingForCircuit, let previous, previous !== canvas {
                    WindowReplacer.replace(previous, with: canvas)
                }
            }
            canvas.undoManager = undoManager
            notices = document.loadNotices
            showNotices = !notices.isEmpty
            TourModel.shared.windowAppeared(canvas)
        }
        .onDisappear { TourModel.shared.windowClosed(canvas) }
        .sheet(item: $canvas.truthTable) { t in TruthTableView(table: t) { canvas.buildFromTable($0) } }
        .sheet(item: $canvas.formulaRequest) { r in BuildFormulaView(text: r.text, canvas: canvas) }
        .alert("No Truth Table", isPresented: Binding(get: { canvas.truthTableProblem != nil },
                                                      set: { if !$0 { canvas.truthTableProblem = nil } })) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(canvas.truthTableProblem ?? "")
        }
        .alert(notices.contains { $0.warning } ? "Some of this circuit couldn't be loaded" : "This circuit was updated",
               isPresented: $showNotices) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(notices.map(\.text).joined(separator: "\n\n"))
        }
    }
}

/// While a Tidy Up is on show: what it is and how to finish it.
struct TidyBanner: View {
    @ObservedObject var canvas: CanvasController

    var body: some View {
        if canvas.tidyActive {
            HStack(spacing: 12) {
                Text("Tidy Up preview").fontWeight(.semibold)
                Button("Keep") { canvas.endTidy(keep: true) }
                    .keyboardShortcut(.defaultAction)
                Button("Put Back") { canvas.endTidy(keep: false) }
                Button(canvas.tidyMode == 1 ? "Keep My Layout Instead" : "Full Rearrange Instead") { canvas.switchTidyMode() }
                    .help("Tab")
            }
            .font(.callout)
            .padding(.horizontal, 16).padding(.vertical, 8)
            .background(.regularMaterial, in: Capsule())
            .shadow(radius: 6, y: 2)
            .padding(.top, 12)
            .transition(.move(edge: .top).combined(with: .opacity))
        }
    }
}

/// Settings > General > Opening a circuit, "Replace the current one": the
/// new circuit's window takes the old one's place (the wx app has one window
/// and loads into it). An old circuit with unsaved changes asks first; saying
/// Cancel keeps it and drops the new one.
@MainActor
final class WindowReplacer: NSObject {
    private static var pending: [WindowReplacer] = []
    private let old: NSWindow, new: NSWindow
    private let newDoc: NSDocument
    var fullScreen = false

    private init(old: NSWindow, new: NSWindow, newDoc: NSDocument) {
        self.old = old; self.new = new; self.newDoc = newDoc
    }

    static func replace(_ previous: CanvasController, with canvas: CanvasController, tries: Int = 0) {
        // Right away, before the new window is first drawn: put it where the
        // old one is, rather than cascaded beside it and then jumping over.
        if tries == 0, let old = previous.view?.window, !old.styleMask.contains(.fullScreen),
           let newDoc = NSDocumentController.shared.documents.last,
           NSDocumentController.shared.document(for: old) !== newDoc,
           let nw = newDoc.windowControllers.first?.window, nw !== old {
            nw.setFrame(old.frame, display: false)
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + (tries == 0 ? 0 : 0.05)) {
            guard let old = previous.view?.window, old.isVisible else { return }
            guard let new = canvas.view?.window else {
                if tries < 20 { replace(previous, with: canvas, tries: tries + 1) }
                return
            }
            let dc = NSDocumentController.shared
            guard old !== new, let oldDoc = dc.document(for: old), let newDoc = dc.document(for: new),
                  oldDoc !== newDoc else { return }
            let r = WindowReplacer(old: old, new: new, newDoc: newDoc)
            pending.append(r)
            // Where the old window was, and in its tab group if it had one.
            let fullScreen = old.styleMask.contains(.fullScreen)
            if !fullScreen { new.setFrame(old.frame, display: false) }
            if let tabs = old.tabbedWindows, tabs.count > 1, !tabs.contains(new) {
                old.addTabbedWindow(new, ordered: .above)
            }
            r.fullScreen = fullScreen
            if oldDoc.isDocumentEdited {
                // The question belongs on the circuit it's about.
                new.orderOut(nil)
                old.makeKeyAndOrderFront(nil)
            }
            oldDoc.canClose(withDelegate: r, shouldClose: #selector(document(_:shouldClose:contextInfo:)), contextInfo: nil)
        }
    }

    @objc private func document(_ doc: NSDocument, shouldClose: Bool, contextInfo: UnsafeMutableRawPointer?) {
        if shouldClose {
            new.makeKeyAndOrderFront(nil)
            doc.close()
            // A full-screen circuit's replacement is full screen too.
            if fullScreen && !new.styleMask.contains(.fullScreen) {
                let w = new
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { w.toggleFullScreen(nil) }
            }
        } else {
            newDoc.close()
            old.makeKeyAndOrderFront(nil)
        }
        Self.pending.removeAll { $0 === self }
    }
}
