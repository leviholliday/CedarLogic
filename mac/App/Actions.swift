// The Actions rail: every command the app's menus offer, in one list (its
// name, symbol, shortcut, when it can run and what it runs), so the rail on
// the window's right edge, the toolbar's version of it and the Customize
// sheet all work from the same place. What each one runs is the menu's own
// handler: CanvasController.perform for the commands with a shortcut, and
// the menus' other calls (Share Link, Predict, Recently Deleted...) here,
// where the menus call them too.

import AppKit
import SwiftUI

/// Every action, by the name it's saved under (cl.actions.order / .shown).
enum ActionID: String, CaseIterable, Identifiable {
    // File
    case newCircuit, openLibrary, recentlyDeleted, importFile, newFromTemplate, practice, saveAsTemplate
    case classroom, createClassroom, joinClass, teacherKey, handIn
    case newTab, closeTab, reopenTab, splitView, closeSplit, switchPane, connectAllPages, disconnectAllPages
    case save, exportImage, exportReport, exportFile, shareLink, versionHistory, exportV2, exportV1, print
    // Edit
    case undo, redo, cut, copy, paste, duplicate, delete, selectAll
    case addGate, rotate, lockInPlace, unlockAllParts, find, findNext, findPrevious, saveAsPart, buildFormula
    case straighten, tidy, tidyOtherWay
    // View
    case zoomIn, zoomOut, zoomFit, zoomActual, focusMode, gridlines, wireDots, darkMode
    case truthTable, checkCircuit, simView, projector, scope, draw, clearDrawing, showDrawing, notes, present, makeEditable
    // Simulation
    case pause, step, stepClock, predict, reveal, lock
    // Help and the app
    case help, feedback, shortcuts, whatsNew, welcome, setUp, tour, settings, checkForUpdates

    var id: String { rawValue }
    @MainActor var action: AppAction { ActionRegistry.action(self) }

    /// What the rail starts with, and Reset to Default goes back to.
    static let defaults: [ActionID] = [.checkCircuit, .predict, .tidy, .shareLink, .exportImage, .exportReport]

    /// Runs it, if it can run now (the rail, the toolbar, the menus).
    @MainActor func run(_ canvas: CanvasController?) {
        let a = action
        guard a.available(), a.enabled(canvas) else { NSSound.beep(); return }
        a.run(canvas)
    }
    @MainActor func enabled(_ canvas: CanvasController?) -> Bool { action.available() && action.enabled(canvas) }
}

/// One action: what it's called (as in its menu), where it lives, its
/// symbol and shortcut, and its handler.
struct AppAction {
    let id: ActionID
    let title: String
    /// The menu it's in (the Customize sheet groups them so).
    let menu: String
    let symbol: String
    /// Its changeable shortcut (Settings > Shortcuts), if it has one...
    var shortcut: ShortcutAction? = nil
    /// ...or one that's fixed (Settings is always ⌘,).
    var fixedKey: String? = nil
    /// Offered at all (Classroom's items only while Classroom is on).
    var available: @MainActor () -> Bool = { true }
    /// Can run now. Most need a circuit window.
    var enabled: @MainActor (CanvasController?) -> Bool = { $0 != nil }
    /// Switched on right now (drawn lit, as the toolbar's toggles are).
    var isOn: (@MainActor (CanvasController?) -> Bool)? = nil
    let run: @MainActor (CanvasController?) -> Void

    /// The shortcut as it reads in a label ("⇧T"), or nil.
    @MainActor var keyLabel: String? {
        if let shortcut { return ShortcutStore.shared.combo(shortcut)?.label }
        return fixedKey
    }
    /// "Check My Circuit (⇧T)": a tooltip and an accessibility label.
    @MainActor var titleWithKey: String {
        let t = title.replacingOccurrences(of: "\u{2026}", with: "")
        return keyLabel.map { "\(t) (\($0))" } ?? t
    }
}

@MainActor
enum ActionRegistry {
    static let menus = ["File", "Edit", "View", "Simulation", "Help"]

    static func action(_ id: ActionID) -> AppAction {
        if table.isEmpty { table = Dictionary(uniqueKeysWithValues: all.map { ($0.id, $0) }) }
        return table[id]!
    }
    private static var table: [ActionID: AppAction] = [:]

    private static func open(_ id: String) { AppActions.openWindow?(id: id) }
    /// The window's canvas, or the front one for commands that need none.
    private static func perform(_ a: ShortcutAction) -> @MainActor (CanvasController?) -> Void {
        { c in (c ?? CanvasController.front)?.perform(a) }
    }
    private static let editable: @MainActor (CanvasController?) -> Bool = { $0?.routed.canEdit == true }
    private static let anyWindow: @MainActor (CanvasController?) -> Bool = { _ in true }
    private static var tidyMode: Int { UserDefaults.standard.integer(forKey: "tidyMode") }

    /// In menu order: File, Edit, View, Simulation, Help.
    static let all: [AppAction] = [
        // File
        AppAction(id: .newCircuit, title: "New Circuit", menu: "File", symbol: "doc.badge.plus", shortcut: .newCircuit,
                  enabled: anyWindow, run: { c in if let c { c.perform(.newCircuit) } else { NSDocumentController.shared.newDocument(nil) } }),
        AppAction(id: .openLibrary, title: "Your Circuits\u{2026}", menu: "File", symbol: "folder", shortcut: .openLibrary,
                  enabled: anyWindow, run: { _ in AppActions.openLibrary() }),
        AppAction(id: .recentlyDeleted, title: "Recently Deleted\u{2026}", menu: "File", symbol: "trash",
                  enabled: anyWindow, run: { _ in LibraryRequest.showRecentlyDeleted() }),
        AppAction(id: .importFile, title: "Import File\u{2026}", menu: "File", symbol: "square.and.arrow.down.on.square", shortcut: .importFile,
                  enabled: anyWindow, run: { _ in NSDocumentController.shared.openDocument(nil) }),
        AppAction(id: .newFromTemplate, title: "New from Template\u{2026}", menu: "File", symbol: "square.grid.2x2",
                  enabled: anyWindow, run: { _ in open("templates") }),
        AppAction(id: .practice, title: "Open the Practice Circuit", menu: "File", symbol: "graduationcap",
                  enabled: anyWindow, run: { _ in Templates.openPractice() }),
        AppAction(id: .saveAsTemplate, title: "Save as Template\u{2026}", menu: "File", symbol: "rectangle.stack.badge.plus",
                  run: { c in if let c { Templates.saveCurrent(from: c) } }),
        AppAction(id: .classroom, title: "Classroom\u{2026}", menu: "File", symbol: "person.3", available: { ClassroomFlag.on },
                  enabled: anyWindow, run: { _ in open("classroom") }),
        AppAction(id: .createClassroom, title: "Create Classroom\u{2026}", menu: "File", symbol: "person.3.sequence", available: { ClassroomFlag.on },
                  enabled: anyWindow, run: { _ in ClassroomCenter.shared.sheet = .create; open("classroom") }),
        AppAction(id: .joinClass, title: "Join a Class\u{2026}", menu: "File", symbol: "person.badge.plus", available: { ClassroomFlag.on },
                  enabled: anyWindow, run: { _ in ClassroomCenter.shared.sheet = .join(prefill: ""); open("classroom") }),
        AppAction(id: .teacherKey, title: "I Have a Teacher Key\u{2026}", menu: "File", symbol: "key", available: { ClassroomFlag.on },
                  enabled: anyWindow, run: { _ in ClassroomCenter.shared.sheet = .addTeacherKey(prefill: ""); open("classroom") }),
        AppAction(id: .handIn, title: "Hand In", menu: "File", symbol: "tray.and.arrow.up", available: { ClassroomFlag.on },
                  run: { _ in ClassroomCenter.shared.handInFront() }),
        AppAction(id: .newTab, title: "New Tab", menu: "File", symbol: "plus.square.on.square", shortcut: .newTab, run: perform(.newTab)),
        AppAction(id: .closeTab, title: "Close Tab", menu: "File", symbol: "xmark.square", shortcut: .closeTab, run: perform(.closeTab)),
        AppAction(id: .reopenTab, title: "Reopen Closed Tab", menu: "File", symbol: "arrow.uturn.backward.square", shortcut: .reopenTab, run: perform(.reopenTab)),
        AppAction(id: .splitView, title: "Split View", menu: "File", symbol: "rectangle.split.2x1", shortcut: .splitView, run: perform(.splitView)),
        AppAction(id: .closeSplit, title: "Close Split", menu: "File", symbol: "rectangle", shortcut: .closeSplit, run: perform(.closeSplit)),
        AppAction(id: .switchPane, title: "Switch Pane", menu: "File", symbol: "arrow.left.arrow.right", shortcut: .switchPane, run: perform(.switchPane)),
        AppAction(id: .connectAllPages, title: "Connect All Pages", menu: "File", symbol: "link.circle",
                  enabled: { $0?.canConnectAllPages ?? false }, run: { $0?.connectAllPages() }),
        AppAction(id: .disconnectAllPages, title: "Disconnect All Pages", menu: "File", symbol: "personalhotspot.slash",
                  enabled: { $0?.canDisconnectAllPages ?? false }, run: { $0?.disconnectAllPages() }),
        AppAction(id: .save, title: "Save", menu: "File", symbol: "square.and.arrow.down", shortcut: .save, run: perform(.save)),
        AppAction(id: .exportImage, title: "Export as Image\u{2026}", menu: "File", symbol: "photo", shortcut: .exportImage, run: perform(.exportImage)),
        AppAction(id: .exportReport, title: "Export Lab Report\u{2026}", menu: "File", symbol: "doc.richtext", shortcut: .exportReport, run: perform(.exportReport)),
        AppAction(id: .exportFile, title: "Export as CedarLogic File\u{2026}", menu: "File", symbol: "square.and.arrow.up", shortcut: .exportFile, run: perform(.exportFile)),
        AppAction(id: .shareLink, title: "Share Link\u{2026}", menu: "File", symbol: "link",
                  run: { c in if let c { ShareLink.copy(from: c) } }),
        AppAction(id: .versionHistory, title: "Version History\u{2026}", menu: "File", symbol: "clock.arrow.circlepath",
                  run: { _ in open("versions") }),
        AppAction(id: .exportV2, title: "Export as V2 (legacy XML)\u{2026}", menu: "File", symbol: "doc.badge.clock",
                  run: { $0?.exportOlder(2) }),
        AppAction(id: .exportV1, title: "Export as V1.x Compatible\u{2026}", menu: "File", symbol: "doc.badge.gearshape",
                  run: { $0?.exportOlder(1) }),
        AppAction(id: .print, title: "Print\u{2026}", menu: "File", symbol: "printer", shortcut: .print, run: { $0?.printPage() }),

        // Edit
        AppAction(id: .undo, title: "Undo", menu: "Edit", symbol: "arrow.uturn.backward", shortcut: .undo,
                  enabled: { $0?.canUndo == true }, run: { $0?.undo() }),
        AppAction(id: .redo, title: "Redo", menu: "Edit", symbol: "arrow.uturn.forward", shortcut: .redo,
                  enabled: { $0?.canRedo == true }, run: { $0?.redo() }),
        AppAction(id: .cut, title: "Cut", menu: "Edit", symbol: "scissors", shortcut: .cut, run: perform(.cut)),
        AppAction(id: .copy, title: "Copy", menu: "Edit", symbol: "doc.on.doc", shortcut: .copy, run: { $0?.copy() }),
        AppAction(id: .paste, title: "Paste", menu: "Edit", symbol: "clipboard", shortcut: .paste, run: perform(.paste)),
        AppAction(id: .duplicate, title: "Duplicate", menu: "Edit", symbol: "plus.rectangle.on.rectangle", shortcut: .duplicate,
                  enabled: { $0?.hasGateSelection == true }, run: perform(.duplicate)),
        AppAction(id: .delete, title: "Delete", menu: "Edit", symbol: "delete.left", run: { $0?.deleteSelection() }),
        AppAction(id: .selectAll, title: "Select All", menu: "Edit", symbol: "checkmark.rectangle", shortcut: .selectAll, run: { $0?.selectAll() }),
        AppAction(id: .addGate, title: "Add a Gate\u{2026}", menu: "Edit", symbol: "plus.circle", shortcut: .addGate, run: perform(.addGate)),
        AppAction(id: .rotate, title: "Rotate", menu: "Edit", symbol: "rotate.right", shortcut: .rotate,
                  enabled: { $0?.hasGateSelection == true }, run: perform(.rotate)),
        AppAction(id: .lockInPlace, title: "Lock in Place", menu: "Edit", symbol: "pin",
                  enabled: { $0?.routed.hasGateSelection == true }, run: { $0?.routed.toggleLockInPlace() }),
        AppAction(id: .unlockAllParts, title: "Unlock All Parts", menu: "Edit", symbol: "pin.slash",
                  enabled: { $0?.routed.hasLockedParts == true }, run: { $0?.routed.unlockAll() }),
        AppAction(id: .find, title: "Find\u{2026}", menu: "Edit", symbol: "magnifyingglass", shortcut: .find, run: perform(.find)),
        AppAction(id: .findNext, title: "Find Next", menu: "Edit", symbol: "chevron.down.circle", shortcut: .findNext, run: perform(.findNext)),
        AppAction(id: .findPrevious, title: "Find Previous", menu: "Edit", symbol: "chevron.up.circle", shortcut: .findPrevious, run: perform(.findPrevious)),
        AppAction(id: .saveAsPart, title: "Save as Part\u{2026}", menu: "Edit", symbol: "shippingbox",
                  enabled: { $0?.hasGateSelection == true }, run: { c in if let c { MyParts.saveSelection(of: c) } }),
        AppAction(id: .buildFormula, title: "Build from Formula\u{2026}", menu: "Edit", symbol: "function", shortcut: .buildFormula, run: perform(.buildFormula)),
        AppAction(id: .straighten, title: "Straighten Wires", menu: "Edit", symbol: "line.diagonal", shortcut: .straighten,
                  enabled: editable, run: perform(.straighten)),
        AppAction(id: .tidy, title: "Tidy Up", menu: "Edit", symbol: "wand.and.stars", shortcut: .tidy,
                  enabled: editable, run: { c in if let c = c?.routed { if c.canEdit { c.tidy(mode: tidyMode) } else { c.lockNudge() } } }),
        AppAction(id: .tidyOtherWay, title: "Tidy Up the Other Way", menu: "Edit", symbol: "wand.and.rays",
                  enabled: editable, run: { c in if let c = c?.routed { if c.canEdit { c.tidy(mode: 1 - tidyMode) } else { c.lockNudge() } } }),

        // View
        AppAction(id: .zoomIn, title: "Zoom In", menu: "View", symbol: "plus.magnifyingglass", shortcut: .zoomIn, run: { $0?.zoomIn() }),
        AppAction(id: .zoomOut, title: "Zoom Out", menu: "View", symbol: "minus.magnifyingglass", shortcut: .zoomOut, run: { $0?.zoomOut() }),
        AppAction(id: .zoomFit, title: "Zoom to Fit", menu: "View", symbol: "arrow.up.left.and.down.right.magnifyingglass", shortcut: .zoomFit, run: { $0?.zoomToFit() }),
        AppAction(id: .zoomActual, title: "Actual Size", menu: "View", symbol: "1.magnifyingglass", shortcut: .zoomActual, run: { $0?.zoomActual() }),
        AppAction(id: .focusMode, title: "Focus Mode", menu: "View", symbol: "arrow.up.left.and.arrow.down.right", shortcut: .focusMode, run: perform(.focusMode)),
        AppAction(id: .gridlines, title: "Display Gridlines", menu: "View", symbol: "grid",
                  enabled: anyWindow, isOn: { _ in Prefs.shared.showGrid }, run: { _ in Prefs.shared.showGrid.toggle() }),
        AppAction(id: .wireDots, title: "Display Wire Connection Points", menu: "View", symbol: "circle.grid.cross",
                  enabled: anyWindow, isOn: { _ in Prefs.shared.wireDotsAtBends }, run: { _ in Prefs.shared.wireDotsAtBends.toggle() }),
        AppAction(id: .darkMode, title: "Dark Mode", menu: "View", symbol: "moon", shortcut: .darkMode,
                  enabled: anyWindow, isOn: { _ in Prefs.shared.dark }, run: { _ in Prefs.shared.dark.toggle() }),
        AppAction(id: .truthTable, title: "Truth Table\u{2026}", menu: "View", symbol: "tablecells", shortcut: .truthTable, run: { $0?.makeTruthTable() }),
        AppAction(id: .checkCircuit, title: "Check My Circuit\u{2026}", menu: "View", symbol: "checkmark.seal", shortcut: .checkCircuit, run: perform(.checkCircuit)),
        AppAction(id: .simView, title: "Simulation View", menu: "View", symbol: "play.rectangle", shortcut: .simView,
                  isOn: { $0?.simView == true }, run: { $0?.simView.toggle() }),
        AppAction(id: .projector, title: "Projector Mode", menu: "View", symbol: "videoprojector",
                  enabled: anyWindow, isOn: { _ in Prefs.shared.projector }, run: { _ in Prefs.shared.projector.toggle() }),
        AppAction(id: .scope, title: "Oscilloscope", menu: "View", symbol: "waveform.path.ecg", shortcut: .scope,
                  isOn: { $0?.showScope == true }, run: { $0?.showScope.toggle() }),
        AppAction(id: .draw, title: "Draw on the Circuit", menu: "View", symbol: "pencil.tip.crop.circle", shortcut: .draw,
                  isOn: { $0?.drawing == true }, run: perform(.draw)),
        AppAction(id: .clearDrawing, title: "Clear Drawing on This Page", menu: "View", symbol: "eraser",
                  enabled: { $0?.routed.pageHasInk == true && $0?.routed.pageIsInkReadOnly != true }, run: { $0?.routed.clearDrawing() }),
        AppAction(id: .showDrawing, title: "Show or Hide Drawing", menu: "View", symbol: "eye", shortcut: .showDrawing, run: perform(.showDrawing)),
        AppAction(id: .notes, title: "Notes", menu: "View", symbol: "note.text", shortcut: .notes,
                  isOn: { $0?.sheetHost.showNotes == true }, run: perform(.notes)),
        AppAction(id: .present, title: "Present", menu: "View", symbol: "display", shortcut: .present,
                  isOn: { _ in Presenter.shared.active }, run: perform(.present)),
        AppAction(id: .makeEditable, title: "Make This Window Editable", menu: "View", symbol: "hand.point.up.left",
                  run: { $0?.makeEditable() }),

        // Simulation
        AppAction(id: .pause, title: "Pause or Resume", menu: "Simulation", symbol: "pause.fill",
                  isOn: { $0?.isRunning == false }, run: { $0?.toggleRunning() }),
        AppAction(id: .step, title: "Step", menu: "Simulation", symbol: "forward.frame.fill", shortcut: .step, run: { $0?.stepOnce() }),
        AppAction(id: .stepClock, title: "Step Clock", menu: "Simulation", symbol: "clock.arrow.2.circlepath", shortcut: .stepClock,
                  enabled: { $0?.routed.hasManualClock == true }, run: perform(.stepClock)),
        AppAction(id: .predict, title: "Predict, Then Reveal", menu: "Simulation", symbol: "questionmark.square.dashed",
                  enabled: { c in guard let c else { return false }; return c.predict.on || !c.predictLights.isEmpty },
                  isOn: { $0?.predict.on == true }, run: { c in
                      // From the rail, outside Simulation View: into it, with the lights covered.
                      guard let c else { return }
                      if !c.simView { c.simView = true; if !c.predict.on { c.togglePredict() } } else { c.togglePredict() }
                  }),
        AppAction(id: .reveal, title: "Reveal", menu: "Simulation", symbol: "eye.circle",
                  enabled: { c in c?.simView == true && c?.predict.on == true && c?.predict.locked != true },
                  run: { $0?.revealOrCoverAgain() }),
        AppAction(id: .lock, title: "Lock the Circuit", menu: "Simulation", symbol: "lock", shortcut: .lock,
                  isOn: { $0?.locked == true }, run: { $0?.locked.toggle() }),

        // Help, and the app's own
        AppAction(id: .help, title: "CedarLogic Help", menu: "Help", symbol: "questionmark.circle", fixedKey: "⌘?",
                  enabled: anyWindow, run: { _ in AppActions.openHelp() }),
        AppAction(id: .feedback, title: "Send Feedback\u{2026}", menu: "Help", symbol: "exclamationmark.bubble", shortcut: .feedback,
                  enabled: anyWindow, run: { _ in FeedbackModel.aim(); open("feedback") }),
        AppAction(id: .shortcuts, title: "Keyboard Shortcuts", menu: "Help", symbol: "keyboard", shortcut: .shortcuts,
                  enabled: { ($0 ?? CanvasController.front) != nil }, run: { c in (c ?? CanvasController.front)?.showShortcuts = true }),
        AppAction(id: .whatsNew, title: "What\u{2019}s New in CedarLogic\u{2026}", menu: "Help", symbol: "sparkles",
                  enabled: anyWindow, run: { _ in open("whatsnew") }),
        AppAction(id: .welcome, title: "Welcome to CedarLogic\u{2026}", menu: "Help", symbol: "hand.wave",
                  enabled: anyWindow, run: { _ in open("welcome") }),
        AppAction(id: .setUp, title: "Set Up CedarLogic\u{2026}", menu: "Help", symbol: "slider.horizontal.3",
                  enabled: anyWindow, run: { _ in WelcomeRequest.setup(); open("welcome") }),
        AppAction(id: .tour, title: "Guided Tour", menu: "Help", symbol: "figure.walk",
                  enabled: anyWindow, run: { _ in TourModel.shared.start() }),
        AppAction(id: .settings, title: "Settings\u{2026}", menu: "Help", symbol: "gearshape", fixedKey: "⌘,",
                  enabled: anyWindow, run: { _ in PrefsWindow.shared.show() }),
        AppAction(id: .checkForUpdates, title: "Check for Updates\u{2026}", menu: "Help", symbol: "arrow.down.circle",
                  enabled: anyWindow, run: { _ in Updates.shared.checkNow() }),
    ]
}

/// Where the actions go (Settings > Actions).
enum ActionsMode: Int, CaseIterable, Identifiable {
    /// The rail on the window's right edge.
    case onScreen = 0
    /// The same items as toolbar buttons; no rail.
    case toolbar = 1
    case hidden = 2
    var id: Int { rawValue }
    var name: String {
        switch self {
        case .onScreen: "On screen"
        case .toolbar: "In the toolbar"
        case .hidden: "Hidden"
        }
    }
}

/// Your Circuits, opened at Recently Deleted (File > Recently Deleted…).
@MainActor
enum LibraryRequest {
    /// Read (and cleared) by Your Circuits as it appears.
    static var wantsRecentlyDeleted = false
    static func showRecentlyDeleted() {
        wantsRecentlyDeleted = true
        AppActions.openLibrary()
        // Already open: it hears this instead.
        NotificationCenter.default.post(name: .clShowRecentlyDeleted, object: nil)
    }
}

extension Notification.Name {
    static let clShowRecentlyDeleted = Notification.Name("clShowRecentlyDeleted")
}

// MARK: - The rail

/// The rail on the window's right edge, the palette's mirror: a narrow strip
/// of the chosen actions, each named (with its shortcut) as the pointer
/// rests on it. A click runs it; nothing opens or docks. Dragging one up or
/// down (past a few points, or after holding it a moment) moves it, the
/// others making room, in the same order the Customize list keeps. A click
/// on its left edge folds it to a 6-point tab, a click on the tab brings it
/// back, and it stays as it was left. Folded, resting on the tab slides it
/// out over the canvas until the pointer leaves (Settings > Actions, on by
/// default, as Arc's sidebar does). In a narrow window it folds by itself.
struct ActionsRail: View {
    @ObservedObject var canvas: CanvasController
    /// The window is too narrow for it: folded unless `peek`.
    let narrow: Bool
    /// Unfolded in a narrow window by a click on its tab.
    @Binding var peek: Bool
    var onCustomize: () -> Void
    /// Drawn with this item's label showing (--render-actions' pictures).
    static var pictureLabel: ActionID?
    /// Drawn folded with the rail slid out over the canvas, as on hover.
    static var pictureSlidOut = false
    /// Drawn mid-drag: this item held this many points below where it was.
    static var pictureDrag: (id: ActionID, dy: CGFloat)?
    private var showLabel: ActionID? { Self.pictureLabel }
    @ObservedObject private var prefs = Prefs.shared
    @ObservedObject private var keys = ShortcutStore.shared
    @State private var hovered: ActionID?
    @State private var edgeHover = false
    @State private var tabHover = false
    // Folded, slid out on hover.
    @State private var slidOut = false
    @State private var overTab = false
    @State private var overRail = false
    @State private var menuOpen = false
    @State private var hoverWait: DispatchWorkItem?
    // A drag to reorder.
    @State private var drag: RailDrag?
    @State private var pressed: ActionID?
    @State private var held: ActionID?
    @State private var holdWait: DispatchWorkItem?

    static let width: CGFloat = 46
    static let tabWidth: CGFloat = 6
    /// Under this window width the rail folds by itself.
    static let narrowWidth: CGFloat = 900
    /// One item and the gap under it.
    static let pitch: CGFloat = 36
    /// Moved this far with the button down, a press is a drag...
    static let dragStart: CGFloat = 6
    /// ...or held this long (then any movement drags).
    static let holdTime = 0.45
    /// Resting on the tab this long slides the rail out (passing doesn't)...
    static let openDelay = 0.15
    /// ...and it goes back this long after the pointer leaves it.
    static let closeDelay = 0.3

    struct RailDrag { let id: ActionID; let from: Int; var dy: CGFloat }

    var folded: Bool { (prefs.actionsCollapsed || narrow) && !peek }

    private var chrome: CLChrome { CLChrome(dark: prefs.dark) }
    private var accent: Color { prefs.accentColor(dark: prefs.dark) }

    var body: some View {
        let _ = canvas.editVersion, _ = canvas.selectionVersion
        Group {
            if folded { tab } else { rail(slid: false) }
        }
        .animation(.easeInOut(duration: 0.2), value: folded)
        .contextMenu { menu }
        // Its right-click menu open keeps it out; closed, it goes if the pointer has.
        .onReceive(NotificationCenter.default.publisher(for: NSMenu.didBeginTrackingNotification)) { _ in
            if slidOut && (overRail || overTab) { menuOpen = true; hoverWait?.cancel() }
        }
        .onReceive(NotificationCenter.default.publisher(for: NSMenu.didEndTrackingNotification)) { _ in
            if menuOpen { menuOpen = false; hoverChanged() }
        }
        .onChange(of: folded) { _, f in if !f { hoverWait?.cancel(); slidOut = false; overRail = false } }
        .onChange(of: prefs.actionsHoverOpen) { _, on in if !on && slidOut { slide(false) } }
    }

    @ViewBuilder private var menu: some View {
        Button("Customize Actions\u{2026}") { onCustomize() }
        Divider()
        Button(folded ? (slidOut ? "Keep Actions Open" : "Show Actions") : "Fold Actions Away") { folded ? unfold() : fold() }
        Button("Show Them in the Toolbar Instead") { prefs.actionsMode = .toolbar }
        Button("Hide Actions") { prefs.actionsMode = .hidden }
    }

    private func fold() { withAnimation(.easeInOut(duration: 0.2)) { peek = false; prefs.actionsCollapsed = true } }
    private func unfold() {
        withAnimation(.easeInOut(duration: 0.2)) {
            prefs.actionsCollapsed = false
            if narrow { peek = true }
        }
    }

    // MARK: Slid out on hover

    private func slide(_ out: Bool) {
        hoverWait?.cancel()
        withAnimation(.easeOut(duration: 0.18)) { slidOut = out }
        if !out { hovered = nil; overRail = false }
    }

    /// The pointer came onto (or left) the tab or the slid-out rail: after
    /// a moment, out or back, unless it's moved on again by then.
    private func hoverChanged() {
        hoverWait?.cancel()
        guard folded, prefs.actionsHoverOpen else { return }
        let inside = overTab || overRail
        if inside && !slidOut {
            later(Self.openDelay) { if folded && prefs.actionsHoverOpen && (overTab || overRail) { slide(true) } }
        } else if !inside && slidOut && !menuOpen && drag == nil {
            later(Self.closeDelay) { if !(overTab || overRail) && !menuOpen && drag == nil { slide(false) } }
        }
    }

    private func later(_ seconds: Double, _ f: @escaping @MainActor () -> Void) {
        let w = DispatchWorkItem { MainActor.assumeIsolated { f() } }
        hoverWait = w
        DispatchQueue.main.asyncAfter(deadline: .now() + seconds, execute: w)
    }

    /// Folded: a 6-point tab the height of the window, lit under the pointer.
    /// Resting on it (with Settings > Actions' hover on) slides the rail out
    /// over the canvas; a click keeps it open.
    private var tab: some View {
        ZStack {
            Rectangle().fill(tabHover ? accent.opacity(0.35) : chrome.tabBar)
                .overlay(alignment: .leading) { Rectangle().fill(chrome.sash).frame(width: 1) }
            Capsule().fill(tabHover ? accent : chrome.barInk.opacity(0.45))
                .frame(width: 2, height: 32)
        }
        .frame(width: Self.tabWidth)
        .frame(maxHeight: .infinity)
        .contentShape(Rectangle())
        .onHover { h in
            withAnimation(.easeOut(duration: 0.12)) { tabHover = h }
            overTab = h
            hoverChanged()
        }
        .onTapGesture { unfold() }
        .help("Show Actions")
        .accessibilityElement()
        .accessibilityLabel("Show Actions")
        .accessibilityAddTraits(.isButton)
        .accessibilityAction { unfold() }
        // Over the canvas, not pushing it: the tab's width stays its own.
        .overlay(alignment: .trailing) {
            if slidOut || Self.pictureSlidOut {
                rail(slid: true)
                    .onHover { h in overRail = h; hoverChanged() }
                    .transition(.move(edge: .trailing))
            }
        }
        .transition(.move(edge: .trailing).combined(with: .opacity))
    }

    private var items: [ActionID] { prefs.railActions }

    /// `slid`: out over the canvas from the tab (its edge then keeps it open).
    private func rail(slid: Bool) -> some View {
        let ids = items
        let d = activeDrag(ids)
        return VStack(spacing: 4) {
            ForEach(Array(ids.enumerated()), id: \.element) { i, id in item(id, index: i, count: ids.count, drag: d) }
            Spacer(minLength: 0)
        }
        // Where the dragged one will go: a dashed space the others open up.
        .background(alignment: .top) {
            if let d {
                RoundedRectangle(cornerRadius: 6)
                    .fill(accent.opacity(0.07))
                    .overlay(RoundedRectangle(cornerRadius: 6)
                        .strokeBorder(accent.opacity(0.6), style: StrokeStyle(lineWidth: 1.5, dash: [3, 3])))
                    .frame(width: 34, height: 32)
                    .offset(y: CGFloat(target(d, count: ids.count)) * Self.pitch)
                    .animation(.easeOut(duration: 0.16), value: target(d, count: ids.count))
                    .accessibilityHidden(true)
            }
        }
        .coordinateSpace(name: "rail")
        .padding(.top, 8)
        .frame(width: Self.width)
        .frame(maxHeight: .infinity)
        .background(chrome.canvas)
        // The edge toward the canvas: the palette's sash, which folds the rail.
        .overlay(alignment: .leading) { edge(slid: slid) }
        // Out over the canvas, a soft shadow on it.
        .shadow(color: .black.opacity(slid ? (prefs.dark ? 0.4 : 0.14) : 0), radius: 8, x: -2)
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Actions")
        .transition(.move(edge: .trailing).combined(with: .opacity))
    }

    private func edge(slid: Bool) -> some View {
        let tip = slid ? "Keep Actions open" : "Fold Actions away"
        return ZStack {
            Rectangle().fill(edgeHover ? accent.opacity(0.6) : chrome.sash).frame(width: edgeHover ? 2 : 1)
            if edgeHover {
                Image(systemName: slid ? "chevron.compact.left" : "chevron.compact.right").font(.system(size: 13, weight: .semibold))
                    .foregroundStyle(accent)
                    .frame(width: 10, height: 30)
                    .background(RoundedRectangle(cornerRadius: 5).fill(chrome.tabCard))
                    .overlay(RoundedRectangle(cornerRadius: 5).strokeBorder(accent.opacity(0.5)))
                    .transition(.opacity)
            }
        }
        .frame(width: Self.tabWidth)
        .frame(maxHeight: .infinity)
        .contentShape(Rectangle())
        .offset(x: -Self.tabWidth / 2)
        .onHover { h in withAnimation(.easeOut(duration: 0.12)) { edgeHover = h } }
        .onTapGesture { slid ? unfold() : fold() }
        .help(tip)
        .accessibilityElement()
        .accessibilityLabel(tip)
        .accessibilityAddTraits(.isButton)
        .accessibilityAction { slid ? unfold() : fold() }
    }

    // MARK: Drag to reorder

    private func activeDrag(_ ids: [ActionID]) -> RailDrag? {
        if let drag { return drag }
        guard let p = Self.pictureDrag, let i = ids.firstIndex(of: p.id) else { return nil }
        return RailDrag(id: p.id, from: i, dy: p.dy)
    }
    /// The place it would go if let go now.
    private func target(_ d: RailDrag, count: Int) -> Int {
        min(max(d.from + Int((d.dy / Self.pitch).rounded()), 0), max(count - 1, 0))
    }
    /// How far an item is drawn from its place: the dragged one under the
    /// pointer (no further than the ends), the others out of its way.
    private func shift(_ i: Int, count: Int, drag d: RailDrag?) -> CGFloat {
        guard let d else { return 0 }
        if i == d.from {
            return min(max(d.dy, -CGFloat(d.from) * Self.pitch - 4), CGFloat(count - 1 - d.from) * Self.pitch + 4)
        }
        let to = target(d, count: count)
        if d.from < to && i > d.from && i <= to { return -Self.pitch }
        if to < d.from && i >= to && i < d.from { return Self.pitch }
        return 0
    }

    /// A press on an item: let go where it was, a click (the action runs);
    /// moved past dragStart, or held for holdTime, a drag that moves it.
    private func press(_ id: ActionID, index: Int, count: Int, enabled: Bool) -> some Gesture {
        DragGesture(minimumDistance: 0, coordinateSpace: .named("rail"))
            .onChanged { v in
                if pressed != id {
                    pressed = id
                    holdWait?.cancel()
                    let w = DispatchWorkItem {
                        MainActor.assumeIsolated {
                            if pressed == id && drag == nil { withAnimation(.easeOut(duration: 0.12)) { held = id } }
                        }
                    }
                    holdWait = w
                    DispatchQueue.main.asyncAfter(deadline: .now() + Self.holdTime, execute: w)
                }
                let t = v.translation
                if drag == nil, hypot(t.width, t.height) >= (held == id ? 1 : Self.dragStart) {
                    holdWait?.cancel()
                    hovered = nil
                    drag = RailDrag(id: id, from: index, dy: t.height)
                } else if drag?.id == id {
                    drag?.dy = t.height
                }
            }
            .onEnded { _ in
                holdWait?.cancel()
                let wasHeld = held == id
                pressed = nil
                if let d = drag, d.id == id {
                    let to = target(d, count: count)
                    withAnimation(.easeOut(duration: 0.18)) {
                        drag = nil
                        held = nil
                        prefs.moveRailAction(id, to: to)
                    }
                    // Dragged out of a slid-out rail: it goes back now.
                    hoverChanged()
                } else {
                    withAnimation(.easeOut(duration: 0.12)) { held = nil }
                    // Held to move but not moved: nothing runs.
                    if enabled && !wasHeld { id.run(canvas) }
                }
            }
    }

    private func item(_ id: ActionID, index: Int, count: Int, drag d: RailDrag?) -> some View {
        let a = id.action
        let enabled = id.enabled(canvas)
        let on = a.isOn?(canvas) ?? false
        let dragging = d?.id == id
        let lifted = dragging || held == id
        let lit = (hovered == id && enabled) || lifted
        let dy = shift(index, count: count, drag: d)
        return Image(systemName: a.symbol)
            .font(.system(size: 15, weight: .regular))
            .foregroundStyle(on ? accent : chrome.barInk.opacity(enabled ? 0.92 : 0.3))
            .frame(width: 34, height: 32)
            .background {
                // The palette tiles' hover: the accent, softly, in their 6-point corners.
                RoundedRectangle(cornerRadius: 6)
                    .fill(lifted ? chrome.tabCard : .clear)
                RoundedRectangle(cornerRadius: 6)
                    .fill(on ? accent.opacity(lit ? 0.24 : 0.18) : accent.opacity(lit ? 0.10 : 0))
                RoundedRectangle(cornerRadius: 6)
                    .strokeBorder(accent.opacity(lit ? 0.9 : 0), lineWidth: 1.5)
            }
            .scaleEffect(lifted ? 1.08 : 1)
            .shadow(color: .black.opacity(lifted ? (prefs.dark ? 0.45 : 0.18) : 0), radius: 5, y: 2)
            .contentShape(Rectangle())
            .onHover { h in
                guard drag == nil else { return }
                withAnimation(.easeOut(duration: 0.12)) { if h { hovered = id } else if hovered == id { hovered = nil } }
            }
            .gesture(press(id, index: index, count: count, enabled: enabled))
            .overlay(alignment: .trailing) {
                if d == nil && (hovered == id || showLabel == id) { label(a, enabled: enabled) }
            }
            .offset(y: dy)
            .animation(dragging ? nil : .easeOut(duration: 0.16), value: dy)
            .zIndex(dragging ? 1 : 0)
            .accessibilityElement()
            .accessibilityLabel(a.titleWithKey)
            .accessibilityAddTraits(on ? [.isButton, .isSelected] : .isButton)
            .accessibilityAction { if enabled { id.run(canvas) } }
            .accessibilityAction(named: "Move Up") { if index > 0 { prefs.moveRailAction(id, to: index - 1) } }
            .accessibilityAction(named: "Move Down") { if index < count - 1 { prefs.moveRailAction(id, to: index + 1) } }
    }

    /// The name and the shortcut, out to the left of the rail over the canvas.
    private func label(_ a: AppAction, enabled: Bool) -> some View {
        HStack(spacing: 8) {
            Text(a.title.replacingOccurrences(of: "\u{2026}", with: ""))
                .font(.system(size: 12, weight: .medium))
                .foregroundStyle(chrome.tabInk.opacity(enabled ? 1 : 0.5))
            if let k = a.keyLabel {
                Text(k).font(.system(size: 11.5)).monospacedDigit()
                    .foregroundStyle(chrome.tabInk.opacity(0.5))
            }
        }
        .lineLimit(1)
        .fixedSize()
        .padding(.horizontal, 9).frame(height: 26)
        .background(RoundedRectangle(cornerRadius: 6).fill(chrome.tabCard))
        .overlay(RoundedRectangle(cornerRadius: 6).strokeBorder(chrome.tabInk.opacity(0.12)))
        .shadow(color: .black.opacity(prefs.dark ? 0.4 : 0.14), radius: 6, y: 2)
        .offset(x: -(34 + 12))
        .allowsHitTesting(false)
        .accessibilityHidden(true)
        .transition(.opacity)
    }
}

// MARK: - Customize

/// Every action in the app, a checkbox each, in the order they'll show
/// (drag to reorder, here or on the rail itself), and Reset to Default. The rail's right-click opens it
/// as a sheet; Settings > Actions has it on its page.
struct ActionsCustomizeList: View {
    @ObservedObject private var prefs = Prefs.shared
    @ObservedObject private var keys = ShortcutStore.shared
    var height: CGFloat = 330

    var body: some View {
        let order = prefs.actionsOrderAll
        VStack(alignment: .leading, spacing: 8) {
            List {
                ForEach(order) { id in row(id) }
                    .onMove { from, to in prefs.moveActions(from: from, to: to) }
            }
            .listStyle(.bordered(alternatesRowBackgrounds: true))
            .frame(height: height)
            HStack {
                Text("\(prefs.railActions.count) shown. Drag to change the order, here or on the rail.")
                    .font(.system(size: 11)).foregroundStyle(.secondary)
                Spacer()
                Button("Reset to Default") { withAnimation { prefs.resetActions() } }
                    .disabled(prefs.actionsAreDefault)
            }
        }
    }

    private func row(_ id: ActionID) -> some View {
        let a = id.action
        return HStack(spacing: 10) {
            Toggle("", isOn: Binding(get: { prefs.actionShown(id) }, set: { prefs.setAction(id, shown: $0) }))
                .labelsHidden().toggleStyle(.checkbox)
            Image(systemName: a.symbol).frame(width: 20).foregroundStyle(Color.accentColor)
            Text(a.title).font(.system(size: 12.5))
            Spacer()
            if let k = a.keyLabel { Text(k).font(.system(size: 11.5)).foregroundStyle(.secondary) }
            Text(a.menu).font(.system(size: 11)).foregroundStyle(.tertiary).frame(width: 70, alignment: .trailing)
            Image(systemName: "line.3.horizontal").font(.system(size: 11)).foregroundStyle(.tertiary)
        }
        .padding(.vertical, 1)
        .accessibilityElement(children: .combine)
        .accessibilityLabel("\(a.title), \(prefs.actionShown(id) ? "shown" : "not shown"), \(a.menu) menu")
    }
}

/// The Customize sheet over a circuit window.
struct ActionsCustomizeSheet: View {
    var done: () -> Void
    @ObservedObject private var prefs = Prefs.shared

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Customize Actions").font(.system(size: 15, weight: .bold))
            Text("Tick what goes on the Actions rail, anything from any menu. They show in this order, top to bottom. You can also drag them up or down on the rail itself.")
                .font(.system(size: 12)).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            ActionsCustomizeList(height: 360)
            HStack {
                Text("Show:").font(.system(size: 12))
                Picker("", selection: $prefs.actionsMode) {
                    ForEach(ActionsMode.allCases) { Text($0.name).tag($0) }
                }
                .labelsHidden().fixedSize()
                Spacer()
                Button("Done") { done() }.keyboardShortcut(.defaultAction)
            }
        }
        .padding(20)
        .frame(width: 520)
        .onEscape { done() }
    }
}

/// Settings > Actions.
struct CLActionsSettings: View {
    @ObservedObject private var prefs = Prefs.shared

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(alignment: .firstTextBaseline, spacing: 12) {
                Text("Actions:").frame(width: 150, alignment: .trailing)
                VStack(alignment: .leading, spacing: 4) {
                    Picker("", selection: $prefs.actionsMode) {
                        ForEach(ActionsMode.allCases) { Text($0.name).tag($0) }
                    }
                    .labelsHidden().fixedSize()
                    Toggle("Open it when you rest on its folded tab", isOn: $prefs.actionsHoverOpen)
                        .toggleStyle(.checkbox)
                        .disabled(prefs.actionsMode != .onScreen)
                    Text(hint).font(.system(size: 11)).foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                        .frame(maxWidth: 360, alignment: .leading)
                }
                Spacer(minLength: 0)
            }
            Text("Show these, in this order:").font(.system(size: 13, weight: .bold)).padding(.top, 6)
            ActionsCustomizeList(height: 300)
                .disabled(prefs.actionsMode == .hidden)
        }
    }

    private var hint: String {
        switch prefs.actionsMode {
        case .onScreen: "A strip down the right edge of the window, the palette's mirror. Rest on one for its name and shortcut; drag one up or down to move it. Click its edge to fold it away. Right-click it to choose your own."
        case .toolbar: "The same actions as buttons in the toolbar, with no strip on the right."
        case .hidden: "No actions strip. Everything is still in the menus, with its shortcut."
        }
    }
}

// MARK: - The window's part

/// What a circuit window does for the rail: watches its width (the rail
/// folds under ActionsRail.narrowWidth and comes back over it), shows the
/// Customize sheet, and tells someone who used CedarLogic before the rail
/// where it is, once.
struct ActionsWindowParts: ViewModifier {
    let canvas: CanvasController
    @Binding var width: CGFloat
    @Binding var peek: Bool
    @Binding var customizing: Bool
    @ObservedObject private var prefs = Prefs.shared

    /// The status line, once, for those who had CedarLogic before the rail.
    static let hint = "New: an Actions rail. Settings \u{25B8} Actions."

    func body(content: Content) -> some View {
        content
            .onChange(of: width) { _, w in
                if w >= ActionsRail.narrowWidth && peek { peek = false }
            }
            .sheet(isPresented: $customizing) {
                ActionsCustomizeSheet { customizing = false }
                    .onClickOutside { customizing = false }
            }
            .onAppear { if prefs.actionsHintPending { tellLater(tries: 0) } }
    }

    /// After the launch (and any welcome or What's New over it) is out of the way.
    private func tellLater(tries: Int) {
        guard !Self.quiet else { return }
        DispatchQueue.main.asyncAfter(deadline: .now() + (tries == 0 ? 4 : 2)) {
            guard prefs.actionsHintPending, let w = canvas.view?.window, w.isVisible else { return }
            let greeting = NSApp.windows.contains { o in
                o.isVisible && ["welcome", "whatsnew"].contains { o.identifier?.rawValue.contains($0) == true }
            }
            if (greeting || w.attachedSheet != nil) && tries < 60 { tellLater(tries: tries + 1); return }
            prefs.actionsHintPending = false
            canvas.note(Self.hint, for: 10)
        }
    }

    /// The test and picture runs leave the hint for the real launch.
    static var quiet: Bool {
        CommandLine.arguments.contains { $0.hasPrefix("--render") || $0.hasSuffix("-check") || $0.hasSuffix("-selftest") }
            || ProcessInfo.processInfo.environment["CL_SNAPSHOT"] != nil || BarTest.on
    }
}
