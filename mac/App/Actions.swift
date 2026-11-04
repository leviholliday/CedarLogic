// The Actions panel: every command the app's menus offer, in one list (its
// name, symbol, shortcut, when it can run and what it runs), so the panel on
// the window's right edge (the "rail" in the code) and the Customize sheet
// work from the same place. What each one runs is the menu's own
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
    case help, feedback, shortcuts, whatsNew, playIntro, welcome, setUp, tour, showMeAround, settings, checkForUpdates

    var id: String { rawValue }
    @MainActor var action: AppAction { ActionRegistry.action(self) }

    /// What the rail starts with, and Reset to Default goes back to.
    static let defaults: [ActionID] = [.checkCircuit, .predict, .tidy, .shareLink, .exportImage, .exportReport]

    /// Runs it, if it can run now (the panel, the menus).
    @MainActor func run(_ canvas: CanvasController?) {
        let a = action
        guard a.available(), a.enabled(canvas) else { NSSound.beep(); return }
        UsageStats.shared.counting(rawValue) { a.run(canvas) }
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
    /// The menu bar's menus, in its order ("CedarLogic" is the app menu).
    static let menus = ["CedarLogic", "File", "Edit", "View", "Simulation", "Help"]

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

    /// In menu order: File, Edit, View, Simulation, Help (and the app menu's).
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
        AppAction(id: .playIntro, title: "Play the Intro", menu: "Help", symbol: "play.circle",
                  enabled: { _ in !Splash.shared.showing }, run: { _ in Splash.shared.playIntroNow() }),
        AppAction(id: .welcome, title: "Welcome to CedarLogic\u{2026}", menu: "Help", symbol: "hand.wave",
                  enabled: anyWindow, run: { _ in open("welcome") }),
        AppAction(id: .setUp, title: "Set Up CedarLogic\u{2026}", menu: "Help", symbol: "slider.horizontal.3",
                  enabled: anyWindow, run: { _ in WelcomeRequest.setup(); open("welcome") }),
        AppAction(id: .tour, title: "Learn Logic: Guided Tour", menu: "Help", symbol: "figure.walk",
                  enabled: anyWindow, run: { _ in TourModel.shared.start() }),
        AppAction(id: .showMeAround, title: "Show Me Around", menu: "Help", symbol: "binoculars",
                  enabled: { ($0 ?? CanvasController.front) != nil },
                  run: { c in if let c = c ?? CanvasController.front { ShowMeAround.shared.start(on: c) } }),
        // On the Mac these two are in the app's own menu, CedarLogic.
        AppAction(id: .settings, title: "Settings\u{2026}", menu: "CedarLogic", symbol: "gearshape", fixedKey: "⌘,",
                  enabled: anyWindow, run: { _ in PrefsWindow.shared.show() }),
        AppAction(id: .checkForUpdates, title: "Check for Updates\u{2026}", menu: "CedarLogic", symbol: "arrow.down.circle",
                  enabled: anyWindow, run: { _ in Updates.shared.checkNow() }),
    ]
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

/// The Actions panel on the window's right edge, the palette's mirror: a narrow strip
/// of the chosen actions, each named (with its shortcut) as the pointer
/// rests on it. A click runs it; nothing opens or docks. Dragging one up or
/// down (past a few points, or after holding it a moment) moves it, the
/// others making room, in the same order the Customize list keeps. A click
/// on its left edge folds it to a 6-point tab, a click on the tab brings it
/// back, and it stays as it was left. Folded, resting on the tab slides it
/// out over the canvas until the pointer leaves (Settings > Actions > Open on
/// hover, on by default, as Arc's sidebar does). In a narrow window, and in focus mode,
/// it folds by itself: a click on the tab opens it until it's folded again
/// or the window leaves focus mode, and then it's as it was before.
struct ActionsRail: View {
    @ObservedObject var canvas: CanvasController
    /// The window is too narrow for it: folded unless `peek`.
    let narrow: Bool
    /// The window is in focus mode: folded unless `peek`, and its tab quieter.
    var focus = false
    /// Unfolded in a narrow window, or in focus mode, by a click on its tab.
    @Binding var peek: Bool
    /// Bumped by View > Show/Hide Actions (and its shortcut): open or fold it.
    var toggleRequest = 0
    var onCustomize: () -> Void
    /// Drawn with this item's label showing (--render-actions' pictures).
    static var pictureLabel: ActionID?
    /// Drawn folded with the rail slid out over the canvas, as on hover.
    static var pictureSlidOut = false
    /// Drawn folded with the pointer at the window's edge: the tab peeking.
    static var picturePeek = false
    /// Drawn mid-drag: this item held this many points below where it was.
    static var pictureDrag: (id: ActionID, dy: CGFloat)?
    private var showLabel: ActionID? { Self.pictureLabel }
    @ObservedObject private var prefs = Prefs.shared
    @ObservedObject private var keys = ShortcutStore.shared
    /// Show Me Around opens it for its stop.
    @ObservedObject private var around = ShowMeAround.shared
    @State private var hovered: ActionID?
    @State private var edgeHover = false
    @State private var tabHover = false
    // Folded, slid out on hover.
    @State private var slidOut = false
    /// The pointer is in the edge's hot strip (the tab and a little more).
    @State private var overTab = false
    @State private var overRail = false
    @State private var menuOpen = false
    @State private var hoverWait: DispatchWorkItem?
    /// Escape put it back: it stays in until the pointer has left the tab.
    @State private var dismissed = false
    @State private var escapeMonitor: Any?
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
    /// Folded, the strip along the window's right edge, the whole height of
    /// the canvas, that slides it out: the tab and a little over the canvas,
    /// to the last column of pixels (pushed against the screen's edge, the
    /// pointer can't miss it).
    static let hotWidth: CGFloat = 12
    /// The tab as the pointer comes into the strip, before the rail is out.
    static let peekWidth: CGFloat = 10
    /// In the strip this long slides the rail out (passing through doesn't)...
    static let openDelay = 0.1
    /// ...and it goes back this long after the pointer leaves it and the rail.
    static let closeDelay = 0.4

    struct RailDrag { let id: ActionID; let from: Int; var dy: CGFloat }

    var folded: Bool { (prefs.actionsCollapsed || narrow || focus) && !peek && !around.holdsPanelOpen(canvas) }

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
        .onChange(of: folded) { _, f in if !f { hoverWait?.cancel(); slidOut = false; overRail = false; watchEscape(false) } }
        // Turned off (Settings > Actions): View's item says Show Actions again.
        .onDisappear { watchEscape(false); canvas.actionsRailShown = false }
        // Help > Show Me Around points here.
        .tourSpot(.actions)
        .onChange(of: prefs.actionsHoverOpen) { _, on in if !on && slidOut { slide(false) } }
        .onChange(of: toggleRequest) { _, _ in toggle() }
        // For View > Show/Hide Actions' name.
        .onChange(of: shown) { _, v in canvas.actionsRailShown = v }
        .onAppear { canvas.actionsRailShown = shown }
    }

    /// Open on screen: pinned, or slid out over the canvas.
    private var shown: Bool { !folded || slidOut }

    /// View > Show/Hide Actions: slid out, back in (and not out again until
    /// the pointer has left the edge); folded, open; open, folded.
    private func toggle() {
        if slidOut {
            dismissed = overTab || overRail
            slide(false)
        } else if folded {
            unfold()
        } else {
            fold()
        }
    }

    /// The window's Show/Hide Actions: with the panel off, on and open;
    /// otherwise the panel's own toggle.
    static func toggle(_ prefs: Prefs, bump: () -> Void) {
        if !prefs.actionsOn {
            prefs.actionsCollapsed = false
            prefs.actionsOn = true
        } else {
            bump()
        }
    }

    @ViewBuilder private var menu: some View {
        Button("Customize Actions\u{2026}") { onCustomize() }
        Divider()
        Button(folded ? (slidOut ? "Keep Actions Open" : "Show Actions") : "Fold Actions Away") { folded ? unfold() : fold() }
        Button("Turn Off the Actions Panel") { prefs.actionsOn = false }
    }

    /// In focus mode these change only `peek`, so leaving it brings back the saved state.
    private func fold() {
        withAnimation(.easeInOut(duration: 0.2)) { peek = false; if !focus { prefs.actionsCollapsed = true } }
    }
    private func unfold() {
        withAnimation(.easeInOut(duration: 0.2)) {
            if focus { peek = true; return }
            prefs.actionsCollapsed = false
            if narrow { peek = true }
        }
    }

    // MARK: Slid out on hover

    private func slide(_ out: Bool) {
        hoverWait?.cancel()
        withAnimation(.easeOut(duration: 0.18)) { slidOut = out }
        if !out { hovered = nil; overRail = false }
        watchEscape(out)
    }

    /// While it's slid out, Escape in its window puts it back.
    private func watchEscape(_ on: Bool) {
        if let m = escapeMonitor { NSEvent.removeMonitor(m); escapeMonitor = nil }
        guard on else { return }
        escapeMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { e in
            guard e.keyCode == 53 else { return e }
            let window = e.window.map(ObjectIdentifier.init)
            let took = MainActor.assumeIsolated { () -> Bool in
                guard slidOut, drag == nil, !menuOpen, let w = canvas.view?.window,
                      window == ObjectIdentifier(w), w.attachedSheet == nil else { return false }
                dismissed = true
                slide(false)
                return true
            }
            return took ? nil : e
        }
    }

    /// The pointer came onto (or left) the tab or the slid-out rail: after
    /// a moment, out or back, unless it's moved on again by then.
    private func hoverChanged() {
        hoverWait?.cancel()
        if !(overTab || overRail) { dismissed = false }
        guard folded, prefs.actionsHoverOpen, !dismissed else { return }
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

    /// Folded: a 6-point tab the height of the window. As the pointer comes
    /// into the hot strip along the edge it peeks (lit, a few points wider,
    /// and all of that clickable) at once; after openDelay there (with
    /// Settings > Actions > Open on hover) the panel slides out over the canvas; a
    /// click keeps it open. In focus mode it's the canvas's own colour, its
    /// grip fainter, until the pointer comes.
    private var tab: some View {
        let lit = tabHover || Self.picturePeek
        return ZStack {
            Rectangle().fill(lit ? accent.opacity(0.35) : focus ? chrome.canvas : chrome.tabBar)
                .overlay(alignment: .leading) {
                    if lit { Rectangle().fill(accent.opacity(0.7)).frame(width: 1) }
                    else if !focus { Rectangle().fill(chrome.sash).frame(width: 1) }
                }
            Capsule().fill(lit ? accent : chrome.barInk.opacity(focus ? 0.22 : 0.45))
                .frame(width: lit ? 3 : 2, height: lit ? 40 : 32)
        }
        .frame(width: lit ? Self.peekWidth : Self.tabWidth)
        .frame(maxHeight: .infinity)
        .contentShape(Rectangle())
        .onTapGesture { unfold() }
        // It takes only the tab's width from the canvas; peeking, it's over it.
        .frame(width: Self.tabWidth, alignment: .trailing)
        // The hot strip: watches the pointer, takes no clicks.
        .overlay(alignment: .trailing) {
            EdgeHotStrip { inside in
                withAnimation(.easeOut(duration: 0.08)) { tabHover = inside }
                overTab = inside
                hoverChanged()
            }
            .frame(width: Self.hotWidth)
            .frame(maxHeight: .infinity)
            .allowsHitTesting(false)
            .accessibilityHidden(true)
        }
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

/// The folded rail's hot strip along the window's right edge: told when the
/// pointer comes into it or leaves (an AppKit tracking area, so it sees the
/// pointer whatever is drawn over it), and never in the way of a click.
struct EdgeHotStrip: NSViewRepresentable {
    var onChange: @MainActor (Bool) -> Void

    func makeNSView(context: Context) -> StripView {
        let v = StripView()
        v.onChange = onChange
        return v
    }
    func updateNSView(_ v: StripView, context: Context) { v.onChange = onChange }

    final class StripView: NSView {
        var onChange: (@MainActor (Bool) -> Void)?
        private(set) var inside = false

        override init(frame: NSRect) {
            super.init(frame: frame)
            addTrackingArea(NSTrackingArea(rect: .zero, options: [.mouseEnteredAndExited, .mouseMoved, .activeInActiveApp, .inVisibleRect],
                                           owner: self, userInfo: nil))
        }
        required init?(coder: NSCoder) { fatalError() }

        override func hitTest(_ point: NSPoint) -> NSView? { nil }
        override func mouseEntered(with e: NSEvent) { pointer(at: e.locationInWindow) }
        override func mouseMoved(with e: NSEvent) { pointer(at: e.locationInWindow) }
        override func mouseExited(with e: NSEvent) { pointer(at: e.locationInWindow) }

        /// The pointer is here, in window coordinates: in the strip or not.
        /// Its right edge counts too (the last column, against the screen's edge).
        func pointer(at p: NSPoint) {
            let q = convert(p, from: nil), b = bounds
            set(q.x >= b.minX && q.x <= b.maxX + 1 && q.y >= b.minY && q.y <= b.maxY)
        }
        private func set(_ v: Bool) {
            guard v != inside else { return }
            inside = v
            onChange?(v)
        }
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
    /// What's typed in its search field (the sheet's Escape clears it first).
    @Binding var query: String

    /// The words a search looks in: its name, its menu, and its shortcut,
    /// as symbols and spelled out ("shift cmd t").
    static func words(_ id: ActionID) -> String {
        let a = id.action
        var w = [a.title.replacingOccurrences(of: "\u{2026}", with: ""), a.menu]
        if let k = a.keyLabel {
            w.append(k)
            var spelled: [String] = []
            for ch in k {
                switch ch {
                case "⌘": spelled.append("command cmd")
                case "⇧": spelled.append("shift")
                case "⌥": spelled.append("option opt alt")
                case "⌃": spelled.append("control ctrl")
                default: spelled.append(String(ch))
                }
            }
            w.append(spelled.joined(separator: " "))
        }
        return w.joined(separator: " ").lowercased()
    }
    /// Every word typed is somewhere in the action's words.
    static func matches(_ id: ActionID, _ query: String) -> Bool {
        let hay = words(id)
        return query.lowercased().split(whereSeparator: \.isWhitespace).allSatisfy { hay.contains($0) }
    }
    private var searching: Bool { !query.trimmingCharacters(in: .whitespaces).isEmpty }

    var body: some View {
        let all = prefs.actionsOrderAll
        let order = searching ? all.filter { Self.matches($0, query) } : all
        VStack(alignment: .leading, spacing: 8) {
            TextField("Search actions", text: $query)
                .textFieldStyle(.roundedBorder)
                .onKeyPress(.escape) {
                    guard !query.isEmpty else { return .ignored }
                    query = ""
                    return .handled
                }
                .accessibilityLabel("Search actions by name, menu or shortcut")
            List {
                // While searching, the list is only some of them: no dragging then.
                ForEach(order) { id in row(id) }
                    .onMove(perform: searching ? nil : { from, to in prefs.moveActions(from: from, to: to) })
                if order.isEmpty {
                    Text("No actions match \u{201C}\(query)\u{201D}.").font(.system(size: 12)).foregroundStyle(.secondary)
                }
            }
            .listStyle(.bordered(alternatesRowBackgrounds: true))
            .frame(height: height)
            HStack {
                Text(searching
                     ? "\(order.count) of \(all.count) match. Clear the search to drag them into order."
                     : "\(prefs.railActions.count) shown. Drag to change the order, here or on the panel.")
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
                .opacity(searching ? 0 : 1)
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
    @State private var query: String
    init(query: String = "", done: @escaping () -> Void) {
        _query = State(initialValue: query)
        self.done = done
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Customize Actions").font(.system(size: 15, weight: .bold))
            Text("Tick what goes on the Actions panel, anything from any menu. They show in this order, top to bottom. You can also drag them up or down on the panel itself.")
                .font(.system(size: 12)).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            ActionsCustomizeList(height: 360, query: $query)
            HStack {
                Toggle("Show the Actions panel", isOn: $prefs.actionsOn).toggleStyle(.checkbox)
                Toggle("Open on hover", isOn: $prefs.actionsHoverOpen).toggleStyle(.checkbox)
                    .disabled(!prefs.actionsOn)
                    .help(ActionsSwitches.hoverHint)
                Spacer()
                Button("Done") { done() }.keyboardShortcut(.defaultAction)
            }
        }
        .padding(20)
        .frame(width: 520)
        // Escape clears a search first, then closes.
        .onEscape { if query.isEmpty { done() } else { query = "" } }
    }
}

/// The panel's two switches, as Settings > Actions, the welcome and What's
/// New word them.
enum ActionsSwitches {
    static let show = "Show the Actions panel"
    static let hover = "Open on hover"
    static let showHint = "The commands you use most, down the right edge of the window. Rest on one for its name and shortcut, click to run it, drag it up or down to move it."
    static let hoverHint = "When the panel is folded, move the pointer to the window\u{2019}s right edge and it slides out. Move away and it goes back."
    @MainActor static var offHint: String {
        let key = ShortcutStore.shared.combo(.toggleActions).map { " (\($0.label))" } ?? ""
        return "Everything is still in the menus, with its shortcut. View \u{25B8} Show Actions\(key) turns it back on."
    }
}

/// Settings > Actions: the panel on or off, opening on hover, and what's on it.
struct CLActionsSettings: View {
    @ObservedObject private var prefs = Prefs.shared
    @ObservedObject private var keys = ShortcutStore.shared
    @State private var query: String
    init(query: String = "") { _query = State(initialValue: query) }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(alignment: .firstTextBaseline, spacing: 12) {
                Text("Actions:").frame(width: 150, alignment: .trailing)
                VStack(alignment: .leading, spacing: 4) {
                    Toggle(ActionsSwitches.show, isOn: $prefs.actionsOn.animation(.easeInOut(duration: 0.24)))
                        .toggleStyle(.checkbox)
                    hint(prefs.actionsOn ? ActionsSwitches.showHint : ActionsSwitches.offHint)
                        .padding(.leading, 20)
                    Toggle(ActionsSwitches.hover, isOn: $prefs.actionsHoverOpen)
                        .toggleStyle(.checkbox)
                        .disabled(!prefs.actionsOn)
                        .padding(.top, 6)
                    hint(ActionsSwitches.hoverHint)
                        .padding(.leading, 20)
                        .opacity(prefs.actionsOn ? 1 : 0.55)
                }
                Spacer(minLength: 0)
            }
            Text("Show these, in this order:").font(.system(size: 13, weight: .bold)).padding(.top, 6)
            ActionsCustomizeList(height: 300, query: $query)
                .disabled(!prefs.actionsOn)
            Text(foldHint).font(.system(size: 11)).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    private func hint(_ s: String) -> some View {
        Text(s).font(.system(size: 11)).foregroundStyle(.secondary)
            .fixedSize(horizontal: false, vertical: true)
            .frame(maxWidth: 360, alignment: .leading)
    }

    private var foldHint: String {
        let key = keys.combo(.toggleActions).map { " (\($0.label))" } ?? ""
        return "Click the panel\u{2019}s left edge to fold it away, or use View \u{25B8} Hide Actions\(key). Right-click it to change what\u{2019}s on it. Help \u{25B8} Show Me Around points it out."
    }
}

// MARK: - The window's part

/// What a circuit window does for the rail: watches its width (the rail
/// folds under ActionsRail.narrowWidth and comes back over it) and focus
/// mode (folded in it, as it was after it), and shows the Customize sheet.
struct ActionsWindowParts: ViewModifier {
    let canvas: CanvasController
    /// The split view's other side, which can ask for Show/Hide Actions too.
    let partner: CanvasController
    @Binding var width: CGFloat
    let focusMode: Bool
    @Binding var peek: Bool
    /// The rail's toggleRequest.
    @Binding var toggle: Int
    /// What `peek` was as the window went into focus mode, for coming out.
    @State private var peekBeforeFocus = false
    @Binding var customizing: Bool
    @ObservedObject private var prefs = Prefs.shared

    func body(content: Content) -> some View {
        content
            .onChange(of: width) { _, w in
                if w >= ActionsRail.narrowWidth && peek && !focusMode { peek = false }
            }
            // View > Show/Hide Actions, from either side of a split.
            .onReceive(NotificationCenter.default.publisher(for: .clToggleActionsRail)) { n in
                if let c = n.object as? CanvasController, c === canvas || c === partner {
                    ActionsRail.toggle(prefs) { toggle += 1 }
                }
            }
            .onReceive(canvas.$actionsRailShown) { v in if partner.actionsRailShown != v { partner.actionsRailShown = v } }
            // Into focus mode: folded (peek off). Out: as it was before.
            .onChange(of: focusMode) { _, on in
                if on { peekBeforeFocus = peek; peek = false } else { peek = peekBeforeFocus }
            }
            .sheet(isPresented: $customizing) {
                ActionsCustomizeSheet { customizing = false }
                    .onClickOutside { customizing = false }
            }
    }
}

// MARK: - The welcome's picture and switches

/// The panel's two switches beside a small picture of a window with the
/// panel in it, in the brand look: the first-run welcome's page and What's
/// New's "Show or Hide It" card. They change the real setting as they're
/// flipped. Flat shapes only, so Windows and Linux can draw the same.
struct ActionsPanelChoice: View {
    @ObservedObject private var prefs = Prefs.shared
    @ObservedObject private var keys = ShortcutStore.shared

    var body: some View {
        HStack(alignment: .top, spacing: 30) {
            VStack(alignment: .leading, spacing: 16) {
                BrandSwitchRow(title: ActionsSwitches.show,
                               line: "The commands you use most, a click each, down the window\u{2019}s right edge.",
                               isOn: $prefs.actionsOn.animation(.spring(response: 0.38, dampingFraction: 0.86)))
                BrandSwitchRow(title: ActionsSwitches.hover,
                               line: "Folded away, it slides out when the pointer reaches the right edge, and goes back when you move off.",
                               isOn: $prefs.actionsHoverOpen.animation(.easeOut(duration: 0.2)))
                    .disabled(!prefs.actionsOn)
                    .opacity(prefs.actionsOn ? 1 : 0.45)
                Text("Change it any time: Settings \u{25B8} Actions, or View \u{25B8} \(prefs.actionsOn ? "Hide" : "Show") Actions\(keys.combo(.toggleActions).map { " (\($0.label))" } ?? "").")
                    .font(.system(size: 11.5)).foregroundStyle(BrandText.faint)
                    .fixedSize(horizontal: false, vertical: true)
            }
            .frame(width: 290, alignment: .leading)
            ActionsPanelPicture(on: prefs.actionsOn, hover: prefs.actionsHoverOpen)
        }
    }
}

/// A switch on the brand ground: its name, a line under it, and the switch.
struct BrandSwitchRow: View {
    let title: String
    let line: String
    @Binding var isOn: Bool

    var body: some View {
        Toggle(isOn: $isOn) {
            VStack(alignment: .leading, spacing: 4) {
                Text(title).font(.system(size: 13.5, weight: .bold)).foregroundStyle(BrandText.primary)
                Text(line).font(.system(size: 11.5)).foregroundStyle(BrandText.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        .toggleStyle(BrandSwitchStyle())
        .accessibilityHint(line)
    }
}

/// A neon switch: a capsule with a knob, lit when on.
struct BrandSwitchStyle: ToggleStyle {
    func makeBody(configuration: Configuration) -> some View {
        HStack(alignment: .top, spacing: 14) {
            configuration.label
            Spacer(minLength: 0)
            Capsule()
                .fill(configuration.isOn ? AnyShapeStyle(Brand.neon) : AnyShapeStyle(Color.white.opacity(0.14)))
                .overlay(Capsule().strokeBorder(Color.white.opacity(configuration.isOn ? 0.35 : 0.16)))
                .frame(width: 40, height: 23)
                .overlay(alignment: configuration.isOn ? .trailing : .leading) {
                    Circle().fill(configuration.isOn ? Brand.ink : Color.white.opacity(0.85))
                        .frame(width: 17, height: 17).padding(3)
                }
                .shadow(color: configuration.isOn ? Brand.neon.opacity(0.45) : .clear, radius: 7)
                .animation(.easeOut(duration: 0.16), value: configuration.isOn)
                .padding(.top, 1)
        }
        .contentShape(Rectangle())
        .onTapGesture { configuration.isOn.toggle() }
        .accessibilityElement(children: .combine)
        .accessibilityAddTraits(.isButton)
        .accessibilityValue(configuration.isOn ? "On" : "Off")
        .accessibilityAction { configuration.isOn.toggle() }
    }
}

/// A small CedarLogic window, dark: the toolbar, the palette, a gate and a
/// light on the canvas, and (when `on`) the Actions panel down the right
/// edge, one of its buttons named as the pointer rests on it. With `hover`
/// a pointer waits at the edge.
struct ActionsPanelPicture: View {
    let on: Bool
    let hover: Bool

    private static let w: CGFloat = 300, h: CGFloat = 200
    private let chrome = CLChrome(dark: true)

    var body: some View {
        let shape = RoundedRectangle(cornerRadius: 10, style: .continuous)
        VStack(alignment: .leading, spacing: 8) {
            ZStack(alignment: .topLeading) {
                chrome.canvas
                VStack(spacing: 0) {
                    toolbar
                    HStack(spacing: 0) {
                        palette
                        canvas
                        if on { rail.transition(.move(edge: .trailing).combined(with: .opacity)) }
                    }
                }
            }
            .frame(width: Self.w, height: Self.h)
            .clipShape(shape)
            .overlay(shape.strokeBorder(Color.white.opacity(0.16)))
            .shadow(color: .black.opacity(0.5), radius: 16, y: 8)
            .shadow(color: on ? Brand.neon.opacity(0.12) : .clear, radius: 20)
            Text(on ? (hover ? "On, and it opens on hover when folded." : "On. Folded, a click on its edge opens it.")
                    : "Off. Everything is still in the menus.")
                .font(.system(size: 10.5, weight: .semibold)).kerning(0.4)
                .foregroundStyle(on ? Brand.neon : BrandText.faint)
                .frame(width: Self.w)
        }
        .accessibilityElement()
        .accessibilityLabel(on ? "A window with the Actions panel on its right edge" : "A window without the Actions panel")
    }

    private var toolbar: some View {
        HStack(spacing: 5) {
            ForEach([Color(red: 1, green: 0.37, blue: 0.34), Color(red: 1, green: 0.74, blue: 0.18), Color(red: 0.16, green: 0.79, blue: 0.25)], id: \.self) {
                Circle().fill($0).frame(width: 7, height: 7)
            }
            ForEach(0..<4, id: \.self) { _ in
                RoundedRectangle(cornerRadius: 2).fill(chrome.barInk.opacity(0.35)).frame(width: 12, height: 8)
            }
            .padding(.leading, 6)
            Spacer()
            Image(systemName: "play.fill").font(.system(size: 7)).foregroundStyle(Brand.neon.opacity(0.8))
        }
        .padding(.horizontal, 8)
        .frame(height: 22)
        .background(chrome.classicBar)
    }

    private var palette: some View {
        VStack(spacing: 5) {
            ForEach(0..<5, id: \.self) { _ in
                HStack(spacing: 5) {
                    ForEach(0..<2, id: \.self) { _ in
                        RoundedRectangle(cornerRadius: 3).strokeBorder(chrome.barInk.opacity(0.3)).frame(width: 18, height: 16)
                    }
                }
            }
            Spacer(minLength: 0)
        }
        .padding(.top, 8)
        .frame(width: 54)
        .frame(maxHeight: .infinity)
        .background(chrome.tabBar)
        .overlay(alignment: .trailing) { Rectangle().fill(chrome.sash).frame(width: 1) }
    }

    private var canvas: some View {
        Canvas { ctx, size in
            var dots = Path()
            var x: CGFloat = 8
            while x < size.width { var y: CGFloat = 8; while y < size.height { dots.addRect(CGRect(x: x, y: y, width: 1, height: 1)); y += 12 }; x += 12 }
            ctx.fill(dots, with: .color(.white.opacity(0.12)))
            let cy = size.height / 2, gx = size.width / 2 - 26
            var wires = Path()
            wires.move(to: CGPoint(x: 18, y: cy - 10)); wires.addLine(to: CGPoint(x: gx, y: cy - 10))
            wires.move(to: CGPoint(x: 18, y: cy + 10)); wires.addLine(to: CGPoint(x: gx, y: cy + 10))
            wires.move(to: CGPoint(x: gx + 40, y: cy)); wires.addLine(to: CGPoint(x: gx + 70, y: cy))
            ctx.stroke(wires, with: .color(Brand.neon), lineWidth: 1.6)
            var gate = Path()
            gate.move(to: CGPoint(x: gx, y: cy - 18)); gate.addLine(to: CGPoint(x: gx + 18, y: cy - 18))
            gate.addCurve(to: CGPoint(x: gx + 18, y: cy + 18), control1: CGPoint(x: gx + 42, y: cy - 18), control2: CGPoint(x: gx + 42, y: cy + 18))
            gate.addLine(to: CGPoint(x: gx, y: cy + 18)); gate.closeSubpath()
            ctx.fill(gate, with: .color(Brand.neon.opacity(0.12)))
            ctx.stroke(gate, with: .color(Brand.neon), lineWidth: 1.6)
            let lamp = CGRect(x: gx + 70, y: cy - 8, width: 16, height: 16)
            ctx.fill(Path(ellipseIn: lamp.insetBy(dx: -5, dy: -5)), with: .color(Color(red: 1, green: 0.77, blue: 0.25).opacity(0.18)))
            ctx.fill(Path(ellipseIn: lamp), with: .color(Color(red: 1, green: 0.77, blue: 0.25)))
            for y in [cy - 10, cy + 10] {
                ctx.stroke(Path(roundedRect: CGRect(x: 6, y: y - 6, width: 12, height: 12), cornerRadius: 3), with: .color(Brand.neon), lineWidth: 1.3)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .overlay(alignment: .trailing) {
            // Open on hover: a pointer resting at the edge (with the panel
            // off it has nowhere to open, so none).
            if on && hover {
                Image(systemName: "cursorarrow").font(.system(size: 13, weight: .semibold))
                    .foregroundStyle(.white).shadow(color: .black.opacity(0.6), radius: 2)
                    .offset(x: -2, y: 66)
                    .transition(.opacity)
            }
        }
    }

    private var rail: some View {
        VStack(spacing: 4) {
            ForEach(Array(ActionID.defaults.enumerated()), id: \.offset) { i, id in
                Image(systemName: id.action.symbol).font(.system(size: 10))
                    .foregroundStyle(chrome.barInk.opacity(0.9))
                    .frame(width: 22, height: 21)
                    .background(RoundedRectangle(cornerRadius: 4).fill(Brand.neon.opacity(i == 0 ? 0.14 : 0)))
                    .overlay(RoundedRectangle(cornerRadius: 4).strokeBorder(Brand.neon.opacity(i == 0 ? 0.9 : 0), lineWidth: 1))
                    .overlay(alignment: .trailing) {
                        if i == 0 {
                            Text("Check My Circuit").font(.system(size: 8.5, weight: .medium)).foregroundStyle(chrome.tabInk)
                                .fixedSize()
                                .padding(.horizontal, 5).frame(height: 15)
                                .background(RoundedRectangle(cornerRadius: 4).fill(chrome.tabCard))
                                .overlay(RoundedRectangle(cornerRadius: 4).strokeBorder(Color.white.opacity(0.12)))
                                .offset(x: -30)
                        }
                    }
            }
            Spacer(minLength: 0)
        }
        .padding(.top, 6)
        .frame(width: 30)
        .frame(maxHeight: .infinity)
        .background(chrome.canvas)
        .overlay(alignment: .leading) { Rectangle().fill(hover ? Brand.neon.opacity(0.5) : chrome.sash).frame(width: 1) }
    }
}
