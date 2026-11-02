// Clicking off a sheet (on the window behind it) closes it, as a popover
// would: Add a Gate, a part's settings, the truth table and the rest. A sheet
// holding changes that aren't kept yet (a memory's new values, a formula
// being typed) asks first, from its own handler. The click that closes the
// sheet does nothing else.

import AppKit
import SwiftUI

extension View {
    /// `action` runs when the window under this sheet is clicked.
    func onClickOutside(_ action: @escaping () -> Void) -> some View {
        background(ClickOutside(action: action).frame(width: 0, height: 0))
    }
}

private struct ClickOutside: NSViewRepresentable {
    let action: () -> Void

    func makeNSView(context: Context) -> Watcher { Watcher() }
    func updateNSView(_ view: Watcher, context: Context) { view.action = action }
    static func dismantleNSView(_ view: Watcher, coordinator: ()) { view.stop() }

    final class Watcher: NSView {
        var action: () -> Void = {}
        private var monitor: Any?

        override func viewDidMoveToWindow() {
            super.viewDidMoveToWindow()
            stop()
            guard window != nil else { return }
            monitor = NSEvent.addLocalMonitorForEvents(matching: [.leftMouseDown, .rightMouseDown, .otherMouseDown]) { [weak self] e in
                guard let self, let sheet = self.window, let parent = sheet.sheetParent,
                      e.window === parent,             // the window behind this sheet
                      sheet.attachedSheet == nil,      // not while it's asking something
                      NSApp.modalWindow == nil,        // nor with an open or save panel up
                      parent.attachedSheet === sheet   // and this is the sheet on top
                else { return e }
                self.action()
                return nil
            }
        }

        func stop() {
            if let monitor { NSEvent.removeMonitor(monitor) }
            monitor = nil
        }

        deinit { stop() }
    }
}
