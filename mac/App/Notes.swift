// Notes: the student's own notes for a circuit (the website's
// docs/DRAWING-NOTES.md 4.14), plain text saved inside it and synced with
// it. A pane at the right of the circuit window (View ▸ Show Notes), in
// Simulation View too. Not the circuit's undo steps: the text view has its
// own undo, so ⌘Z in the notes undoes typing and ⌘Z on the canvas undoes
// the circuit. ("circuitNotes" in the core: Version History's notes, a
// line kept with each version, are another thing.)

import AppKit
import SwiftUI

enum NotesLimits {
    static let max = 20_000
    static let counterFrom = 18_000
    static let placeholder = "Notes for this circuit. They're saved with it."
}

/// The notes pane.
struct NotesPanel: View {
    @ObservedObject var canvas: CanvasController
    @ObservedObject var document: CoreDocument
    @ObservedObject private var prefs = Prefs.shared
    @State private var count = 0
    /// Drawn for a picture (--render-ui): this text instead of the circuit's.
    var sample: String? = nil

    private var dark: Bool { prefs.dark || canvas.simView }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                Text("Notes").font(.system(size: 13, weight: .semibold))
                Spacer()
                if count > NotesLimits.counterFrom {
                    Text("\(count.formatted()) / \(NotesLimits.max.formatted())")
                        .font(.system(size: 11)).monospacedDigit()
                        .foregroundStyle(count >= NotesLimits.max ? Color.orange : Color.secondary)
                }
                Button { canvas.showNotes = false } label: { Image(systemName: "xmark").font(.system(size: 11, weight: .semibold)) }
                    .buttonStyle(.borderless)
                    .help("Hide Notes")
                    .accessibilityLabel("Hide Notes")
            }
            .padding(.horizontal, 12).padding(.top, 10).padding(.bottom, 8)
            NotesTextView(document: document, canvas: canvas, dark: dark, sample: sample) { count = $0 }
        }
        .background(CLPalette(dark: dark, simView: false).panel)
        .environment(\.colorScheme, dark ? .dark : .light)
    }
}

/// An NSTextView for the notes: plain text, a placeholder, the 20,000
/// character limit, its own undo, and Find's matches selected.
struct NotesTextView: NSViewRepresentable {
    let document: CoreDocument
    let canvas: CanvasController
    let dark: Bool
    var sample: String? = nil
    let onCount: (Int) -> Void

    func makeCoordinator() -> Coordinator { Coordinator(self) }

    func makeNSView(context: Context) -> NSScrollView {
        let scroll = NSTextView.scrollableTextView()
        scroll.drawsBackground = false
        scroll.hasVerticalScroller = true
        guard let tv = scroll.documentView as? NSTextView else { return scroll }
        tv.delegate = context.coordinator
        tv.isRichText = false
        tv.importsGraphics = false
        tv.allowsUndo = true
        tv.isAutomaticQuoteSubstitutionEnabled = false
        tv.isAutomaticDashSubstitutionEnabled = false
        tv.font = NSFont.systemFont(ofSize: 13)
        tv.textContainerInset = NSSize(width: 8, height: 6)
        tv.drawsBackground = false
        tv.setAccessibilityLabel("Notes for this circuit")
        tv.setAccessibilityPlaceholderValue(NotesLimits.placeholder)
        context.coordinator.textView = tv
        context.coordinator.load()
        return scroll
    }

    func updateNSView(_ scroll: NSScrollView, context: Context) {
        let c = context.coordinator
        c.parent = self
        c.textView?.textColor = dark ? NSColor(white: 0.92, alpha: 1) : NSColor(white: 0.1, alpha: 1)
        c.textView?.insertionPointColor = dark ? .white : .black
        // Another document in the same window (a sync replaced it): its notes.
        if c.loadedFrom !== document { c.load() }
    }

    @MainActor
    final class Coordinator: NSObject, NSTextViewDelegate {
        var parent: NotesTextView
        weak var textView: NSTextView?
        weak var loadedFrom: CoreDocument?
        /// The notes' own undo, apart from the circuit's.
        private let undo = UndoManager()
        private var findObserver: NSObjectProtocol?

        init(_ parent: NotesTextView) {
            self.parent = parent
            super.init()
            findObserver = NotificationCenter.default.addObserver(forName: .clNotesFind, object: nil, queue: .main) { [weak self] n in
                MainActor.assumeIsolated {
                    guard let self, let q = n.userInfo?["query"] as? String,
                          (n.object as? CoreDocument) === self.parent.document else { return }
                    self.select(q)
                }
            }
        }

        func load() {
            guard let tv = textView else { return }
            let text = parent.sample ?? parent.document.notes
            tv.string = text
            loadedFrom = parent.document
            undo.removeAllActions()
            placeholder(tv)
            parent.onCount(text.unicodeScalars.count)
        }

        func undoManager(for view: NSTextView) -> UndoManager? { undo }

        private func placeholder(_ tv: NSTextView) {
            // The placeholder: grey text drawn when the notes are empty.
            tv.setValue(NSAttributedString(string: NotesLimits.placeholder,
                                           attributes: [.foregroundColor: NSColor.placeholderTextColor,
                                                        .font: NSFont.systemFont(ofSize: 13)]),
                        forKey: "placeholderAttributedString")
        }

        /// 20,000 characters: typing that would lengthen the text past that is
        /// refused (deleting works); a paste is cut to fit, with a note.
        func textView(_ tv: NSTextView, shouldChangeTextIn range: NSRange, replacementString text: String?) -> Bool {
            guard let text, parent.sample == nil else { return parent.sample == nil }
            let current = tv.string as NSString
            let removed = current.substring(with: range).unicodeScalars.count
            let total = tv.string.unicodeScalars.count - removed + text.unicodeScalars.count
            guard total > NotesLimits.max else { return true }
            let room = NotesLimits.max - (tv.string.unicodeScalars.count - removed)
            if text.unicodeScalars.count > 1 && room > 0 {
                var cut = String.UnicodeScalarView()
                cut.append(contentsOf: text.unicodeScalars.prefix(room))
                tv.insertText(String(cut), replacementRange: range)
                parent.canvas.note("The notes hold \(NotesLimits.max.formatted()) characters, so the paste was cut to fit.")
            } else {
                NSSound.beep()
                parent.canvas.note("The notes are full (\(NotesLimits.max.formatted()) characters).")
            }
            return false
        }

        func textDidChange(_ n: Notification) {
            guard let tv = textView, parent.sample == nil else { return }
            parent.document.notes = tv.string
            parent.onCount(tv.string.unicodeScalars.count)
            // Not an undo step: the document is marked edited and saves
            // itself (and syncs) as after any change.
            parent.canvas.sheetHost.markEdited()
        }

        /// Find: select the next match of `q` after the selection.
        func select(_ q: String) {
            guard let tv = textView, !q.isEmpty else { return }
            let s = tv.string as NSString
            let from = NSMaxRange(tv.selectedRange())
            var r = s.range(of: q, options: .caseInsensitive, range: NSRange(location: min(from, s.length), length: s.length - min(from, s.length)))
            if r.location == NSNotFound { r = s.range(of: q, options: .caseInsensitive) }
            guard r.location != NSNotFound else { return }
            tv.setSelectedRange(r)
            tv.scrollRangeToVisible(r)
            tv.showFindIndicator(for: r)
        }
    }
}

extension Notification.Name {
    /// Find landed on the notes: object the CoreDocument, userInfo["query"].
    static let clNotesFind = Notification.Name("clNotesFind")
}
