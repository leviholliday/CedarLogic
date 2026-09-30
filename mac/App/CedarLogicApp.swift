// CedarLogic for Mac: the native front end. The engine underneath is the same
// C++ the wx app runs.

import SwiftUI
import UniformTypeIdentifiers

@main
struct CedarLogicApp: App {
    @StateObject private var look = LookStore.shared
    @NSApplicationDelegateAdaptor(LaunchDelegate.self) private var launch

    init() {
        // CedarLogic has page tabs of its own; the Mac's window tabs (View >
        // Show Tab Bar, Window > Merge All Windows and the rest) only confuse.
        NSWindow.allowsAutomaticWindowTabbing = false
        // The Edit menu's Start Dictation and Emoji & Symbols are no use in a
        // circuit editor (MenuFixup takes out what's left).
        UserDefaults.standard.set(true, forKey: "NSDisabledDictationMenuItem")
        UserDefaults.standard.set(true, forKey: "NSDisabledCharacterPaletteMenuItem")
        loadGateLibrary()
        // Loading the library sets the drawing's defaults; now the user's.
        MainActor.assumeIsolated {
            Prefs.shared.applyWireDots()
            KeyMonitor.install()
            TabSwitcher.shared.install()
            MenuFixup.install()
            RenderUI.runIfAsked()
            RenderWindow.runIfAsked()
            DispatchQueue.main.async { Updates.shared.start() }
            DispatchQueue.global(qos: .utility).async { Library.removeRepeats() }
        }
    }

    var body: some Scene {
        DocumentGroup(newDocument: { CircuitDocument() }) { file in
            CircuitWindow(document: file.document.core)
                .environmentObject(look)
        }
        .defaultSize(width: 1280, height: 800)
        .commands {
            NewOpenCommands()
            EditCommands()
            ViewCommands()
            SimulationCommands()
            FileCommands()
            HelpCommands()
            CommandGroup(replacing: .appInfo) {
                Button("About CedarLogic") { showAbout() }
                Button("Check for Updates…") { Updates.shared.checkNow() }
            }
            CommandGroup(replacing: .appTermination) {
                // Only Cmd-Q and this item ask: an update's relaunch or the
                // Mac shutting down quit straight away.
                Button("Quit CedarLogic") { QuitConfirm.ask() }
                    .keyboardShortcut("q", modifiers: .command)
            }
            CommandGroup(replacing: .appSettings) {
                Button("Settings…") { PrefsWindow.shared.show() }
                    .keyboardShortcut(",", modifiers: .command)
            }
        }

        Window("Welcome to CedarLogic", id: "welcome") {
            WelcomeView()
        }
        .windowResizability(.contentSize)
        .windowStyle(.hiddenTitleBar)
        .defaultPosition(.center)

        Window("What's New in CedarLogic", id: "whatsnew") {
            WhatsNewView()
        }
        .windowResizability(.contentSize)
        .windowStyle(.hiddenTitleBar)
        .defaultPosition(.center)

        Window("Your Circuits", id: "library") {
            LibraryView()
        }
        .windowResizability(.contentMinSize)
        .defaultSize(width: 600, height: 540)
        .defaultPosition(.center)

        Window("CedarLogic Help", id: "help") {
            HelpView()
        }
        .defaultSize(width: 1000, height: 700)
        .defaultPosition(.center)

        Window("Send Feedback", id: "feedback") {
            FeedbackView()
        }
        .windowStyle(.hiddenTitleBar)
        .windowResizability(.contentSize)
        .defaultPosition(.center)

        Window("New from Template", id: "templates") {
            TemplatePicker()
        }
        .windowResizability(.contentMinSize)
        .defaultSize(width: 940, height: 620)
        .defaultPosition(.center)

        Window("Version History", id: "versions") {
            VersionHistoryView()
        }
        .windowResizability(.contentMinSize)
        .defaultSize(width: 940, height: 620)
        .defaultPosition(.center)

        // Settings is an AppKit window of its own (PrefsWindow): its page
        // switch is the wx one, which SwiftUI's settings window can't do.
    }
}

/// What happens at launch: the wx app reopens the circuit you had last, so
/// this does too -- no Open panel, no empty "Untitled" on top of it. With
/// nothing to reopen, a new circuit.
final class LaunchDelegate: NSObject, NSApplicationDelegate {
    override init() {
        super.init()
        // Without this, a document app shows an Open panel at launch.
        UserDefaults.standard.register(defaults: ["NSShowAppCentricOpenPanelInsteadOfUntitledFile": false])
    }

    func applicationShouldOpenUntitledFile(_ sender: NSApplication) -> Bool { false }

    /// The launch screen goes up before any window, so they can wait for it.
    func applicationWillFinishLaunching(_ notification: Notification) {
        CrashReports.start()
        Splash.shared.showIfLaunching()
        // Asked once, once the circuit is there to ask over.
        if Splash.shared.active {
            NotificationCenter.default.addObserver(forName: .clSplashDone, object: nil, queue: .main) { _ in
                DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) { MainActor.assumeIsolated { CrashReports.askOnce() } }
            }
        } else {
            DispatchQueue.main.asyncAfter(deadline: .now() + 2.5) { MainActor.assumeIsolated { CrashReports.askOnce() } }
        }
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        // CL_MENU_DUMP=<file>: as a menu opens (MenuFixup runs then), write the
        // titles in the File, Edit, View and Window menus to <file> and quit.
        if let out = ProcessInfo.processInfo.environment["CL_MENU_DUMP"] {
            DispatchQueue.main.asyncAfter(deadline: .now() + 4) {
                NotificationCenter.default.post(name: NSMenu.didBeginTrackingNotification, object: nil)
                var text = ""
                for item in NSApp.mainMenu?.items ?? [] where ["File", "Edit", "View", "Window"].contains(item.submenu?.title ?? "") {
                    text += "\(item.submenu?.title ?? ""): " + (item.submenu?.items.map { $0.isSeparatorItem ? "-" : $0.title }.joined(separator: " | ") ?? "") + "\n"
                }
                try? text.write(toFile: out, atomically: true, encoding: .utf8)
                exit(0)
            }
        }
        DevSnapshot.runIfAsked()
        BarTest.start()
        FeedbackModel.selfTestIfAsked()
        // Once anything macOS was asked to open (a file double-clicked in
        // Finder) has arrived: the circuit you were last in (LastCircuit), as
        // the wx app reopens the one you had; else the most recent; else a
        // new one. It opens hidden behind the launch screen, which plays only
        // after it's open and drawn, so the loading can't stutter its
        // animation.
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.05) {
            let dc = NSDocumentController.shared
            let launched = { Self.afterFirstDraw { Splash.shared.begin() } }
            guard dc.documents.isEmpty, !CommandLine.arguments.contains("--render-ui") else { launched(); return }
            // The toolbar test works on a new circuit, never one of yours.
            if BarTest.on && ProcessInfo.processInfo.environment["CL_BAR_WATCH"] == nil { Self.newHidden(); launched(); return }
            let remembered = LastCircuit.url.map { [$0] } ?? []
            if let last = (remembered + dc.recentDocumentURLs).first(where: { FileManager.default.fileExists(atPath: $0.path) }) {
                dc.openDocument(withContentsOf: last, display: false) { doc, _, error in
                    if let doc { Self.showHidden(doc) } else { Self.newHidden() }
                    launched()
                }
            } else {
                Self.newHidden()
                launched()
            }
        }
    }

    /// A document's window, made and hidden before it's ever ordered in:
    /// it waits behind the launch screen without a single frame showing.
    @MainActor private static func showHidden(_ doc: NSDocument) {
        if doc.windowControllers.isEmpty { doc.makeWindowControllers() }
        for wc in doc.windowControllers {
            if let w = wc.window { Splash.shared.holdEarly(w, from: "launch") }
        }
        doc.showWindows()
    }

    @MainActor private static func newHidden() {
        if let doc = try? NSDocumentController.shared.openUntitledDocumentAndDisplay(false) { showHidden(doc) }
        else { NSDocumentController.shared.newDocument(nil) }
    }

    /// After the windows that just opened have laid out, drawn once and
    /// settled (their title bars, first status), so the launch screen plays
    /// on a main thread with nothing else to do.
    @MainActor private static func afterFirstDraw(_ then: @escaping @MainActor () -> Void) {
        DispatchQueue.main.async {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.14) { then() }
        }
    }

    /// Clicking the Dock icon with no window open: the same.
    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        if !flag && NSDocumentController.shared.documents.isEmpty { NSDocumentController.shared.newDocument(nil) }
        return false
    }
}

/// `CL_SNAPSHOT=<dir>`: the circuit window draws itself to PNGs (as it
/// opens, then split, then with the second side active) -- a look at the
/// window without screen capture, for development.
@MainActor
enum DevSnapshot {
    static func runIfAsked() {
        guard let dir = ProcessInfo.processInfo.environment["CL_SNAPSHOT"] else { return }
        let url = URL(fileURLWithPath: dir, isDirectory: true)
        // CL_SNAPSHOT_OPEN=<window id>: open that window and draw it instead.
        if let id = ProcessInfo.processInfo.environment["CL_SNAPSHOT_OPEN"] {
            try? FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
            DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) { CanvasController.front?.openWindow?(id) }
            DispatchQueue.main.asyncAfter(deadline: .now() + 4.0) {
                for (i, w) in NSApp.windows.enumerated() where w.isVisible && CanvasController.front?.view?.window !== w {
                    guard let v = w.contentView?.superview, let rep = v.bitmapImageRepForCachingDisplay(in: v.bounds) else { continue }
                    v.cacheDisplay(in: v.bounds, to: rep)
                    try? rep.representation(using: .png, properties: [:])?.write(to: url.appendingPathComponent("\(id)-\(i).png"))
                }
                exit(0)
            }
            return
        }
        try? FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        func shot(_ name: String) {
            guard let w = NSApp.windows.first(where: { $0.isVisible && $0.frame.width > 400 && $0.title != "Welcome" }),
                  let v = w.contentView?.superview else { return }
            guard let rep = v.bitmapImageRepForCachingDisplay(in: v.bounds) else { return }
            v.cacheDisplay(in: v.bounds, to: rep)
            try? rep.representation(using: .png, properties: [:])?.write(to: url.appendingPathComponent(name + ".png"))
        }
        func frames(_ label: String) {
            guard let w = NSApp.windows.first(where: { $0.isVisible && $0.frame.width > 400 }) else { return }
            func walk(_ v: NSView) {
                if let c = v as? CircuitCanvasNSView {
                    print(label, "canvas page \(c.page) frame-in-window \(c.convert(c.bounds, to: nil)) visible \(c.visibleRect)")
                }
                v.subviews.forEach(walk)
            }
            if let root = w.contentView { walk(root) }
        }
        setvbuf(stdout, nil, _IONBF, 0)
        let steps: [(Double, () -> Void)] = [
            (2.5, { shot("1-open") }),
            (2.6, { CanvasController.front?.perform(.splitView) }),
            (3.4, {
                frames("split")
                // A gate on the second side, where its view's middle is.
                if let side = CanvasController.front?.partner { side.addGateFloating("AA_AND2", at: side.view?.visibleCenter) }
            }),
            (3.5, { CanvasController.front?.partner?.view.map { v in
                if let d = v.document { _ = d.press(page: v.page, at: v.visibleCenter, modifiers: [], unitsPerPoint: v.unitsPerPoint); d.release(at: v.visibleCenter) }
                CanvasController.front?.partner?.edited()
            } }),
            (3.6, { shot("2-split") }),
            (3.7, { CanvasController.front?.perform(.newTab) }),
            (4.6, { shot("3-newtab") }),
            (4.7, { CanvasController.front?.perform(.splitView) }),
            (5.6, { shot("4-closed"); exit(0) }),
        ]
        for (t, f) in steps { DispatchQueue.main.asyncAfter(deadline: .now() + t) { f() } }
    }
}

/// The standard About panel, plus which build this is: the Native app and the
/// wx app can both be installed, and several builds of either.
private func showAbout() {
    let info = Bundle.main.infoDictionary ?? [:]
    let commit = info["CLCommit"] as? String ?? "unknown"
    let credits = NSAttributedString(
        string: "The native Mac app (SwiftUI), built from commit \(commit).\nThe wx app is CedarLogic 4.x; this one is separate.",
        attributes: [.font: NSFont.systemFont(ofSize: NSFont.smallSystemFontSize),
                     .foregroundColor: NSColor.secondaryLabelColor])
    NSApp.orderFrontStandardAboutPanel(options: [.credits: credits])
    NSApp.activate()
}

/// The gate library ships in the app's Resources. `swift`-style dev builds
/// without a bundle fall back to the copy in the repo.
private func loadGateLibrary() {
    var candidates: [String] = []
    if let bundled = Bundle.main.path(forResource: "cl_gatedefs", ofType: "xml") { candidates.append(bundled) }
    let repo = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        .deletingLastPathComponent().deletingLastPathComponent()
        .appendingPathComponent("res/cl_gatedefs.xml").path
    candidates.append(repo)
    for path in candidates where FileManager.default.fileExists(atPath: path) {
        if cl_library_load(path) { return }
    }
    NSLog("CedarLogic: the gate library couldn't be loaded")
}

/// The File items. Open is Your Circuits and Import
/// opens a file, as in the wx app: the keys are answered by KeyMonitor and
/// the Mac's own Open item is relabelled to match (MenuFixup) -- replacing
/// that group outright upsets SwiftUI's document handling.
struct NewOpenCommands: Commands {
    @FocusedObject private var canvas: CanvasController?
    @ObservedObject private var keys = ShortcutStore.shared
    @Environment(\.openWindow) private var openWindow

    var body: some Commands {
        let _ = { AppActions.openWindow = openWindow }()
        CommandGroup(after: .newItem) {
            Button("Your Circuits…") { openWindow(id: "library") }
            Button("New from Template…") { openWindow(id: "templates") }
            Button("Save as Template…") { if let canvas { Templates.saveCurrent(from: canvas) } }
                .disabled(canvas == nil)
            Divider()
            Button("New Tab") { canvas?.perform(.newTab) }
                .keyboardShortcut(keys.menu(.newTab))
                .disabled(canvas == nil)
            Button("Close Tab") { canvas?.perform(.closeTab) }
                .disabled(canvas == nil)
            Button("Reopen Closed Tab") { canvas?.perform(.reopenTab) }
                .keyboardShortcut(keys.menu(.reopenTab))
                .disabled(canvas == nil)
            Button("Split View") { canvas?.perform(.splitView) }
                .keyboardShortcut(keys.menu(.splitView))
                .disabled(canvas == nil)
            Button("Close Split") { canvas?.perform(.closeSplit) }
                .keyboardShortcut(keys.menu(.closeSplit))
                .disabled(canvas == nil)
            Button("Switch Pane") { canvas?.perform(.switchPane) }
                .keyboardShortcut(keys.menu(.switchPane))
                .disabled(canvas == nil)
        }
    }
}

/// The circuit you were last working in: the last saved circuit whose window
/// was in front. Launching reopens it (window restoration is off for
/// circuit windows, so it's this, not whatever macOS kept).
@MainActor
enum LastCircuit {
    private static let key = "cl.lastCircuit"
    static var url: URL? {
        UserDefaults.standard.string(forKey: key).map { URL(fileURLWithPath: $0) }
    }
    static func note(_ window: NSWindow?) {
        guard let window, let url = NSDocumentController.shared.document(for: window)?.fileURL else { return }
        // Not a scratch copy (an old version opened to look at, say).
        let temp = FileManager.default.temporaryDirectory.standardizedFileURL.path
        guard !url.standardizedFileURL.path.hasPrefix(temp), !url.path.contains("/.Trash/") else { return }
        UserDefaults.standard.set(url.path, forKey: key)
    }
}

/// Things the key monitor needs from SwiftUI.
@MainActor
enum AppActions {
    static var openWindow: OpenWindowAction?
    static func openLibrary() { openWindow?(id: "library") }
    static func openHelp() { openWindow?(id: "help") }
}

/// Which app opens a .cdl file double-clicked in Finder. The classic
/// CedarLogic claims them too, and the Mac lets only one win.
@MainActor
enum CDLHandler {
    private static var types: [UTType] {
        [UTType("edu.cedarville.cedarlogic.circuit"), UTType(filenameExtension: "cdl")].compactMap { $0 }
    }
    private static var current: URL? { types.lazy.compactMap { NSWorkspace.shared.urlForApplication(toOpen: $0) }.first }
    static var isUs: Bool { current?.standardizedFileURL == Bundle.main.bundleURL.standardizedFileURL }
    static var currentName: String {
        guard let url = current else { return "no app" }
        return FileManager.default.displayName(atPath: url.path).replacingOccurrences(of: ".app", with: "")
    }
    static func makeUs() async {
        for t in types { try? await NSWorkspace.shared.setDefaultApplication(at: Bundle.main.bundleURL, toOpen: t) }
    }
}

/// Shows CedarLogic's keys on the Mac's own File items as a
/// menu opens: Open… (⌘O) is Your Circuits, Import File… (⇧⌘O) a .cdl file,
/// and Close Tab carries ⌘W while the window's Close moves off it.
@MainActor
enum MenuFixup {
    private static var observer: NSObjectProtocol?

    private static var editWatch: [NSObjectProtocol] = []
    private static var tidying = false

    static func install() {
        guard observer == nil else { return }
        // The Mac adds Writing Tools and AutoFill to the Edit menu as it opens,
        // after any tidying done before; so take them out as they arrive.
        for name in [NSMenu.didAddItemNotification, NSMenu.didChangeItemNotification] {
            editWatch.append(NotificationCenter.default.addObserver(forName: name, object: nil, queue: .main) { note in
                MainActor.assumeIsolated {
                    guard !tidying, let menu = note.object as? NSMenu, menu.title == "Edit" else { return }
                    tidying = true
                    tidyEdit()
                    tidying = false
                }
            })
        }
        observer = NotificationCenter.default.addObserver(forName: NSMenu.didBeginTrackingNotification, object: nil, queue: .main) { _ in
            MainActor.assumeIsolated { apply() }
        }
    }

    private static func set(_ item: NSMenuItem, _ c: KeyCombo?) {
        guard let c else { item.keyEquivalent = ""; return }
        item.keyEquivalent = c.key == "tab" ? "\t" : c.key
        var m: NSEvent.ModifierFlags = []
        if c.mods & KeyCombo.shift != 0 { m.insert(.shift) }
        if c.mods & KeyCombo.option != 0 { m.insert(.option) }
        if c.mods & KeyCombo.control != 0 { m.insert(.control) }
        if c.mods & KeyCombo.command != 0 { m.insert(.command) }
        item.keyEquivalentModifierMask = m
    }

    /// What the Mac adds to the Edit menu that a circuit editor has no use for,
    /// and the separators left round the gaps.
    private static func tidyEdit() {
        guard let edit = NSApp.mainMenu?.items.first(where: { $0.submenu?.title == "Edit" })?.submenu else { return }
        // Hidden, not taken out: taking items out while the menu was
        // updating for a key press lost that key (the first ⌘A or ⌘N after
        // launch did nothing), and doing it later made the menu flicker as
        // the Mac put them back.
        let unwanted = ["Writing Tools", "AutoFill", "Start Dictation", "Emoji & Symbols"]
        for item in edit.items where !item.isHidden && (unwanted.contains(where: { item.title.hasPrefix($0) })
            || item.action == NSSelectorFromString("startDictation:")
            || item.action == NSSelectorFromString("orderFrontCharacterPalette:")) {
            item.isHidden = true
        }
        // Separators: none twice in a row, none at the end.
        var lastShownIsSeparator = true
        var lastSeparator: NSMenuItem?
        for item in edit.items {
            if item.isSeparatorItem {
                let hide = lastShownIsSeparator
                if item.isHidden != hide { item.isHidden = hide }
                if !hide { lastShownIsSeparator = true; lastSeparator = item }
            } else if !item.isHidden {
                lastShownIsSeparator = false
            }
        }
        if lastShownIsSeparator, let sep = lastSeparator, !sep.isHidden { sep.isHidden = true }
    }

    static func apply() {
        tidyEdit()
        guard let file = NSApp.mainMenu?.items.first(where: { $0.submenu?.title == "File" })?.submenu else { return }
        let keys = ShortcutStore.shared
        for item in file.items {
            if item.action == #selector(NSDocumentController.openDocument(_:)) {
                item.title = "Import File…"
                set(item, keys.combo(.importFile))
            } else if item.title == "Your Circuits…" || item.title == "Open…" && item.action != #selector(NSDocumentController.openDocument(_:)) {
                item.title = "Open…"
                set(item, keys.combo(.openLibrary))
            } else if item.title == "Close Tab" {
                set(item, keys.combo(.closeTab))
            } else if item.action == NSSelectorFromString("duplicateDocument:") {
                // Not Edit > Duplicate (the selected parts): a copy of the whole circuit.
                item.title = "Duplicate Circuit"
            } else if item.action == #selector(NSWindow.performClose(_:)) {
                item.title = "Close Circuit"
                set(item, KeyCombo(key: "w", mods: KeyCombo.command | KeyCombo.shift))
            }
        }
    }
}

struct ViewCommands: Commands {
    @FocusedObject private var canvas: CanvasController?
    @AppStorage("cl.showGrid") private var showGrid = true
    @AppStorage("cl.wireDots") private var wireDots = true
    @AppStorage("cl.lastDark") private var dark = false
    @ObservedObject private var keys = ShortcutStore.shared

    var body: some Commands {
        CommandGroup(after: .toolbar) {
            Button("Zoom In") { canvas?.zoomIn() }
                .keyboardShortcut(keys.menu(.zoomIn))
                .disabled(canvas == nil)
            Button("Zoom Out") { canvas?.zoomOut() }
                .keyboardShortcut(keys.menu(.zoomOut))
                .disabled(canvas == nil)
            Button("Zoom to Fit") { canvas?.zoomToFit() }
                .keyboardShortcut(keys.menu(.zoomFit))
                .disabled(canvas == nil)
            Button("Actual Size") { canvas?.zoomActual() }
                .keyboardShortcut(keys.menu(.zoomActual))
                .disabled(canvas == nil)
            Divider()
            Button("Focus Mode") { canvas?.perform(.focusMode) }
                .keyboardShortcut(keys.menu(.focusMode))
                .disabled(canvas == nil)
            Toggle("Display Gridlines", isOn: Binding(get: { showGrid }, set: { Prefs.shared.showGrid = $0 }))
            Toggle("Display Wire Connection Points", isOn: Binding(get: { wireDots }, set: { Prefs.shared.wireDotsAtBends = $0 }))
            Toggle("Dark Mode", isOn: Binding(get: { dark }, set: { Prefs.shared.dark = $0 }))
                .keyboardShortcut(keys.menu(.darkMode))
            Divider()
            Button("Truth Table…") { canvas?.makeTruthTable() }
                .keyboardShortcut(keys.menu(.truthTable))
                .disabled(canvas == nil)
            Button(canvas?.simView == true ? "Leave Simulation View" : "Simulation View") { canvas?.simView.toggle() }
                .keyboardShortcut(keys.menu(.simView))
                .disabled(canvas == nil)
            Divider()
            Button(canvas?.showScope == true ? "Hide Oscilloscope" : "Show Oscilloscope") { canvas?.showScope.toggle() }
                .keyboardShortcut(keys.menu(.scope))
                .disabled(canvas == nil)
            Divider()
        }
    }
}

struct SimulationCommands: Commands {
    @FocusedObject private var canvas: CanvasController?
    @ObservedObject private var keys = ShortcutStore.shared

    var body: some Commands {
        CommandMenu("Simulation") {
            Button(canvas?.simView == true ? "Leave Simulation View" : "Simulation View") { canvas?.simView.toggle() }
                .disabled(canvas == nil)
            Button(canvas?.isRunning == false ? "Resume" : "Pause") { canvas?.toggleRunning() }
                .disabled(canvas == nil)
            Button("Step") { canvas?.stepOnce() }
                .keyboardShortcut(keys.menu(.step))
                .disabled(canvas == nil)
            Divider()
            Button("Truth Table…") { canvas?.makeTruthTable() }
                .help("T: every combination of the page's switches (or the selected ones) and what the lights show")
                .disabled(canvas == nil)
            Divider()
            Button(canvas?.locked == true ? "Unlock the Circuit" : "Lock the Circuit") { canvas?.locked.toggle() }
                .keyboardShortcut(keys.menu(.lock))
                .disabled(canvas == nil)
        }
    }
}

/// Edit menu. While a text field has the keyboard (the palette search, an
/// inspector field), the usual text commands go to it instead.
struct EditCommands: Commands {
    @FocusedObject private var canvas: CanvasController?
    @AppStorage("tidyMode") private var defaultMode = 0
    @ObservedObject private var keys = ShortcutStore.shared

    private func tidyTitle(_ mode: Int) -> String { mode == 1 ? "Tidy Up: Full Rearrange" : "Tidy Up: Keep My Layout" }

    private var typing: Bool { NSApp.keyWindow?.firstResponder is NSText }

    private func textAction(_ selector: Selector) { NSApp.sendAction(selector, to: nil, from: nil) }


    var body: some Commands {
        CommandGroup(replacing: .undoRedo) {
            Button("Undo") { if typing { textAction(Selector(("undo:"))) } else { canvas?.undo() } }
                .keyboardShortcut(keys.menu(.undo))
                .disabled(canvas?.canUndo != true && !typing)
            Button("Redo") { if typing { textAction(Selector(("redo:"))) } else { canvas?.redo() } }
                .keyboardShortcut(keys.menu(.redo))
                .disabled(canvas?.canRedo != true && !typing)
        }
        CommandGroup(replacing: .pasteboard) {
            Button("Cut") { if typing { textAction(#selector(NSText.cut(_:))) } else { canvas?.perform(.cut) } }
                .keyboardShortcut(keys.menu(.cut))
            Button("Copy") { if typing { textAction(#selector(NSText.copy(_:))) } else { canvas?.copy() } }
                .keyboardShortcut(keys.menu(.copy))
            Button("Paste") {
                if typing { textAction(#selector(NSText.paste(_:))) } else { canvas?.perform(.paste) }
            }
            .keyboardShortcut(keys.menu(.paste))
            Button("Duplicate") { canvas?.perform(.duplicate) }
                .keyboardShortcut(keys.menu(.duplicate))
                .disabled(canvas?.hasGateSelection != true)
            Button("Delete") { if typing { textAction(#selector(NSText.delete(_:))) } else { canvas?.deleteSelection() } }
            Button("Select All") { if typing { textAction(#selector(NSText.selectAll(_:))) } else { canvas?.selectAll() } }
                .keyboardShortcut(keys.menu(.selectAll))
            Divider()
            Button("Add a Gate…") { canvas?.perform(.addGate) }
                .keyboardShortcut(keys.menu(.addGate))
                .disabled(canvas == nil)
            Button("Rotate") { canvas?.perform(.rotate) }
                .keyboardShortcut(keys.menu(.rotate))
                .disabled(canvas?.hasGateSelection != true)
            Menu("Find") {
                Button("Find…") { canvas?.perform(.find) }
                    .keyboardShortcut(keys.menu(.find))
                Button("Find Next") { canvas?.perform(.findNext) }
                    .keyboardShortcut(keys.menu(.findNext))
                Button("Find Previous") { canvas?.perform(.findPrevious) }
                    .keyboardShortcut(keys.menu(.findPrevious))
            }
            .disabled(canvas == nil)
            Button("Save as Part…") { if let canvas { MyParts.saveSelection(of: canvas) } }
                .disabled(canvas?.hasGateSelection != true)
            Button("Build from Formula…") { canvas?.perform(.buildFormula) }
                .keyboardShortcut(keys.menu(.buildFormula))
                .disabled(canvas == nil)
            Button("Straighten Wires") { canvas?.perform(.straighten) }
                .keyboardShortcut(keys.menu(.straighten))
                .help("S: the selected wires, or the wires of the selected parts")
            Button(tidyTitle(defaultMode)) { canvas?.tidy(mode: defaultMode) }
                .help("Shift-S. Lines parts up and reroutes their wires (the selection, or the whole page).")
            Button(tidyTitle(1 - defaultMode)) { canvas?.tidy(mode: 1 - defaultMode) }
        }
    }
}

struct FileCommands: Commands {
    @FocusedObject private var canvas: CanvasController?
    @ObservedObject private var keys = ShortcutStore.shared
    @Environment(\.openWindow) private var openWindow

    var body: some Commands {
        CommandGroup(after: .saveItem) {
            Button("Export as Image…") { canvas?.perform(.exportImage) }
                .keyboardShortcut(keys.menu(.exportImage))
                .disabled(canvas == nil)
            Button("Export as CedarLogic File…") { canvas?.perform(.exportFile) }
                .keyboardShortcut(keys.menu(.exportFile))
                .disabled(canvas == nil)
            Button("Version History…") { openWindow(id: "versions") }
                .disabled(canvas == nil)
            // As in wx's File menu: copies an older CedarLogic can open
            // (a school's lab computers, say).
            Button("Export as V2 (legacy XML)…") { canvas?.exportOlder(2) }
                .disabled(canvas == nil)
            Button("Export as V1.x Compatible…") { canvas?.exportOlder(1) }
                .disabled(canvas == nil)
        }
        CommandGroup(replacing: .printItem) {
            Button("Print…") { canvas?.printPage() }
                .keyboardShortcut(keys.menu(.print))
                .disabled(canvas == nil)
        }
    }
}

struct HelpCommands: Commands {
    @FocusedObject private var canvas: CanvasController?
    @Environment(\.openWindow) private var openWindow
    @ObservedObject private var keys = ShortcutStore.shared

    var body: some Commands {
        CommandGroup(replacing: .help) {
            // Contents… on F1, as in the wx app's Help menu.
            Button("CedarLogic Help") { openWindow(id: "help") }
                .keyboardShortcut("?", modifiers: .command)
            Button("Contents…") { openWindow(id: "help") }
                .keyboardShortcut(KeyEquivalent(Character(UnicodeScalar(NSF1FunctionKey)!)), modifiers: [])
            Divider()
            Button("Send Feedback…") { FeedbackModel.aim(); openWindow(id: "feedback") }
                .keyboardShortcut(keys.menu(.feedback))
            Divider()
            Button("Keyboard Shortcuts") { (canvas ?? CanvasController.front)?.showShortcuts = true }
            Divider()
            Button("What's New in CedarLogic…") { openWindow(id: "whatsnew") }
            Button("Welcome to CedarLogic…") { openWindow(id: "welcome") }
            Button("Set Up CedarLogic…") { WelcomeRequest.setup(); openWindow(id: "welcome") }
            Button("Guided Tour") { TourModel.shared.start() }
        }
    }
}

/// Keys the menus can't take: Close Tab on ⌘W (the window's Close owns it
/// in the menu), and the canvas's bare keys while a sheet or panel over the
/// canvas doesn't want them. Watched app-wide, before the menus see them.
@MainActor
enum KeyMonitor {
    private static var monitor: Any?

    static func install() {
        guard monitor == nil else { return }
        monitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { e in
            // F1: Help, as in the wx app.
            if e.keyCode == 122 && e.modifierFlags.intersection([.command, .option, .control, .shift]).isEmpty {
                AppActions.openHelp()
                return nil
            }
            // ⌘Q asks here rather than through the Quit menu item, whose key
            // is missed the first time after launch.
            if e.charactersIgnoringModifiers?.lowercased() == "q",
               e.modifierFlags.intersection([.command, .option, .control, .shift]) == .command {
                QuitConfirm.ask()
                return nil
            }
            guard let c = KeyCombo(event: e) else { return e }
            let keys = ShortcutStore.shared
            let typing = NSApp.keyWindow?.firstResponder is NSText
            if !typing && NSApp.keyWindow?.attachedSheet == nil {
                if c == keys.combo(.openLibrary) { AppActions.openLibrary(); return nil }
                if c == keys.combo(.importFile) { NSDocumentController.shared.openDocument(nil); return nil }
            }
            // ⇧⌘W closes the whole circuit (⌘W is the tab's).
            if c == KeyCombo(key: "w", mods: KeyCombo.command | KeyCombo.shift), c != keys.combo(.closeTab),
               let w = NSApp.keyWindow, CanvasController.front?.view?.window === w {
                w.performClose(nil)
                return nil
            }
            guard let window = NSApp.keyWindow, let canvas = CanvasController.front,
                  canvas.view?.window === window else { return e }
            // Close Tab: while there's more than one tab; the last one's ⌘W
            // closes the window, as usual.
            if c == ShortcutStore.shared.combo(.closeTab), let doc = canvas.document, doc.pageCount > 1,
               window.attachedSheet == nil, !(window.firstResponder is NSText) {
                canvas.perform(.closeTab)
                return nil
            }
            return e
        }
    }
}
