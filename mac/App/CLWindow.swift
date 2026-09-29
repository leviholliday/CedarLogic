// The CedarLogic interface: the wx app's window, rebuilt natively with its
// sizes, colours and motion. Its drawn toolbar sits in the title bar row with
// the window buttons centred on its left (MacSetCustomTitlebar); under it the
// tab strip (TabStrip), the palette and minimap down the left, the canvas
// (Simulation View's bar, the oscilloscope, an optional split), and a status
// bar along the bottom.

import AppKit
import SwiftUI

/// The wx app's chrome colours (MainFrame::ApplyTheme, TabStrip, ModernToolbar).
struct CLChrome {
    let dark: Bool
    static func rgb(_ r: Double, _ g: Double, _ b: Double) -> Color { Color(.sRGB, red: r / 255, green: g / 255, blue: b / 255) }

    var canvas: Color { dark ? Self.rgb(19, 21, 25) : .white }
    var tabBar: Color { dark ? Self.rgb(22, 24, 28) : Self.rgb(233, 234, 238) }
    var tabBarTop: Color { dark ? Self.rgb(38, 41, 48) : Self.rgb(250, 250, 252) }
    var tabCard: Color { dark ? Self.rgb(32, 35, 41) : .white }
    var tabCardLit: Color { dark ? Self.rgb(44, 48, 56) : .white }
    var tabInk: Color { dark ? Self.rgb(228, 232, 240) : Self.rgb(32, 35, 42) }
    var barInk: Color { dark ? Self.rgb(210, 215, 224) : Self.rgb(52, 56, 64) }
    var classicBar: Color { dark ? Self.rgb(44, 47, 54) : Self.rgb(236, 237, 240) }
    var minimalBar: Color { dark ? Self.rgb(28, 31, 37) : Self.rgb(246, 247, 249) }
    var sash: Color { dark ? Self.rgb(40, 43, 50) : Self.rgb(218, 220, 224) }
}

struct CLLayout: View {
    let document: CoreDocument
    @ObservedObject var canvas: CanvasController
    @Binding var page: Int
    @ObservedObject private var prefs = Prefs.shared
    @ObservedObject private var tour = TourModel.shared
    @Environment(\.openWindow) private var openWindow
    @State private var windowTitle = "Untitled"
    @State private var focusMode = false
    @State private var settingsFor: CanvasController?
    /// The circuit-opening card: covering the canvas from the window's very
    /// first frame (so the circuit never flashes up first), then playing
    /// from `opening`, once the window has landed. Not at launch: the
    /// launch screen does the introducing.
    @State private var covered = !Splash.shared.active
    @State private var opening: Date?
    @StateObject private var split = SplitState()

    private var chrome: CLChrome { CLChrome(dark: prefs.dark) }
    /// What the opening card says under the name.
    private var openingDetail: String {
        let tabs = document.pageCount
        let gates = (0..<tabs).reduce(0) { $0 + Int(cl_document_gate_count(document.handle, Int32($1))) }
        if gates == 0 { return tabs > 1 ? "\(tabs) empty tabs" : "A blank page, ready to build" }
        return "\(tabs) tab\(tabs == 1 ? "" : "s")  ·  \(gates) gate\(gates == 1 ? "" : "s")"
    }
    /// The page of the side you're working in.
    private var workingPage: Int { canvas.splitFocus ? (split.sidePage(document) ?? page) : page }

    var body: some View {
        VStack(spacing: 0) {
            if !focusMode {
                CLToolbar(document: document, canvas: canvas, status: canvas.status, title: windowTitle,
                          subtitle: document.pageName(min(workingPage, document.pageCount - 1)), focusMode: $focusMode)
                    .transition(.move(edge: .top).combined(with: .opacity))
            }
            HStack(spacing: 0) {
                if !focusMode {
                    CLSidePanel(document: document, canvas: canvas, mapCanvas: canvas.routed, page: workingPage)
                        .frame(width: CGFloat(max(176, min(prefs.gateSize * 3 + 30, 340))))
                        .transition(.move(edge: .leading))
                    Rectangle().fill(chrome.sash).frame(width: 1)
                }
                CLCanvasArea(document: document, canvas: canvas, page: $page, split: split,
                             leftInset: focusMode ? 86 : 8, titleRow: focusMode)
                    .overlay {
                        if covered {
                            OpeningCard(start: opening ?? .distantFuture, title: windowTitle, detail: openingDetail, dark: prefs.dark,
                                        onReveal: { canvas.playAppear() }, onDone: { covered = false; opening = nil })
                        }
                    }
            }
            if prefs.showStatus || !canvas.statusMessage.isEmpty || !canvas.routed.statusMessage.isEmpty {
                // The side you're working in: its zoom, pointer and count.
                CLStatusBar(document: document, canvas: canvas.routed, status: canvas.routed.status)
                    .id(ObjectIdentifier(canvas.routed))
            }
        }
        .background(chrome.canvas)
        .ignoresSafeArea(.container, edges: .top)
        .background(WindowConfigurator(fullBleed: true, barHeight: focusMode ? 36 : 52) { w in
            // A circuit from Your Circuits is its name there, not "circuit".
            let url = NSDocumentController.shared.document(for: w)?.fileURL
            let t = Library.displayName(for: url) ?? w.title
            if windowTitle != t { DispatchQueue.main.async { windowTitle = t } }
        })
        .preferredColorScheme(prefs.dark ? .dark : .light)
        .tint(prefs.accentColor(dark: prefs.dark))
        .animation(.easeInOut(duration: 0.24), value: focusMode)
        .onChange(of: canvas.pageRequest) { _, p in
            guard let p else { return }
            canvas.pageRequest = nil
            canvas.pageRequestAfterClose = false
            split.showMain(p, document, leftPage: $page)
            split.reconcile(document, leftPage: $page)
        }
        .onChange(of: split.controller.pageRequest) { _, p in
            guard let p else { return }
            let c = split.controller
            c.pageRequest = nil
            // After a close the neighbour is found by reconcile, on this side.
            if !c.pageRequestAfterClose { split.showSide(p, document, leftPage: $page) }
            c.pageRequestAfterClose = false
            split.reconcile(document, leftPage: $page)
        }
        .onReceive(document.objectWillChange) { _ in
            DispatchQueue.main.async { split.reconcile(document, leftPage: $page) }
        }
        // Switching tabs is a cut, straight to the tab as it was left; only a
        // new or reopened tab fades in (wx: playAppearAnimation).
        .onChange(of: page) { _, p in split.noteMain(p, document) }
        // Lock and Simulation View are the window's, as in wx: both sides.
        .onReceive(canvas.$locked) { v in if split.controller.locked != v { split.controller.locked = v } }
        .onReceive(split.controller.$locked) { v in if canvas.locked != v { canvas.locked = v } }
        .onReceive(canvas.$simView) { v in if split.controller.simView != v { split.controller.simView = v } }
        .onReceive(split.controller.$simView) { v in if canvas.simView != v { canvas.simView = v } }
        .onChange(of: prefs.classicTabs) { _, classic in if classic { split.close(document, leftPage: $page) } }
        // Revealed by the launch screen: the grid fades in as it goes.
        .onReceive(NotificationCenter.default.publisher(for: .clSplashDone)) { _ in canvas.playAppear() }
        .onChange(of: canvas.settingsRequested) { _, asked in
            guard asked else { return }
            canvas.settingsRequested = false
            let owner = canvas.settingsGate.flatMap { g in
                [canvas, split.controller].first { $0.document?.singleSelectedGate(page: $0.page) == g }
            } ?? canvas
            if let g = canvas.settingsGate, cl_ram_info(document.handle, g, nil, nil) {
                canvas.ramGate = g
            } else {
                settingsFor = owner
            }
        }
        .onReceive(NotificationCenter.default.publisher(for: .clToggleFocusMode)) { n in
            if (n.object as? CanvasController) === canvas || (n.object as? CanvasController) === split.controller { focusMode.toggle() }
        }
        .splitPaneCommands(canvas: canvas, split: split, document: document, page: $page, classicTabs: prefs.classicTabs)
        .sheet(isPresented: $canvas.showQuickAdd) {
            QuickAddView { name in (canvas.quickAddTarget ?? canvas).addGateOnNextMove(name) }
        }
        .sheet(isPresented: $canvas.showShortcuts) { ShortcutsSheet(canvas: canvas.routed) }
        .sheet(isPresented: $canvas.showExportImage) {
            ExportImageView(document: document, page: canvas.exportPage, fileName: windowTitle)
        }
        .sheet(item: Binding(get: { canvas.ramGate.map { RamRef(id: $0) } }, set: { canvas.ramGate = $0?.id })) { ref in
            RamEditorView(document: document, gate: ref.id, canvas: canvas)
        }
        .sheet(item: $settingsFor) { c in
            VStack(spacing: 0) {
                InspectorView(document: document, controller: c).frame(width: 380, height: 320)
                Divider()
                HStack { Spacer(); Button("Done") { settingsFor = nil }.keyboardShortcut(.defaultAction) }
                    .padding(12)
            }
            .onEscape { settingsFor = nil }
        }
        .overlay(alignment: .bottomTrailing) {
            if tour.active {
                TourCard(canvas: canvas, document: document, page: page)
                    .padding(.trailing, 18).padding(.bottom, 44)
                    .transition(.move(edge: .trailing).combined(with: .opacity))
            }
        }
        .animation(.spring(response: 0.35, dampingFraction: 0.85), value: tour.active)
        .onAppear {
            canvas.openWindow = { id in openWindow(id: id) }
            split.primary = canvas
            split.noteMain(page, document)
            canvas.paneOrder = { [weak split, weak document] in
                guard let split, let document, split.isOpen else { return nil }
                return split.pages(0, document)
            }
            split.controller.paneOrder = { [weak split, weak document] in
                guard let split, let document else { return nil }
                return split.pages(1, document)
            }
            let pageBinding = $page
            canvas.tabSwitch = { [weak canvas, weak split, weak document] in
                guard let canvas, let split, let document else { return nil }
                let n = document.pageCount
                let front = canvas.splitFocus ? (split.sidePage(document) ?? pageBinding.wrappedValue) : pageBinding.wrappedValue
                var order = [front]
                for id in split.recent { if let i = document.pageIndex(of: id), !order.contains(i) { order.append(i) } }
                for i in 0..<n where !order.contains(i) { order.append(i) }
                return TabSwitchOffer(document: document, pages: order) { [weak canvas, weak split] p in
                    guard let canvas, let split, p < document.pageCount else { return }
                    let id = document.pageID(p)
                    if split.isOpen && split.pages(1, document).contains(p) {
                        split.select(id)
                        split.controller.onActivate?()
                        split.controller.view?.window?.makeFirstResponder(split.controller.view)
                    } else {
                        split.showMain(p, document, leftPage: pageBinding)
                        canvas.onActivate?()
                        canvas.view?.window?.makeFirstResponder(canvas.view)
                    }
                }
            }
            split.controller.onNewPage = { [weak split, weak document] i in
                guard let split, let document else { return }
                split.adoptNew(i, document)
            }
            canvas.onActivate = { [weak canvas, weak split] in
                guard let canvas, let split else { return }
                if canvas.splitFocus { canvas.splitFocus = false }
                CanvasController.front = canvas
                split.objectWillChange.send()
            }
            split.controller.onActivate = { [weak canvas, weak split] in
                guard let canvas, let split else { return }
                if !canvas.splitFocus { canvas.splitFocus = true }
                split.objectWillChange.send()
            }
            canvas.playAppear()
            // The card plays once the window is in place (and drawn).
            if covered { DispatchQueue.main.asyncAfter(deadline: .now() + 0.08) { opening = Date() } }
            if !prefs.hasSeenWelcome { openWindow(id: "welcome") }
        }
    }
}

extension View {
    /// Escape does this (the wx dialogs' Cancel), whatever has the focus.
    func onEscape(_ action: @escaping () -> Void) -> some View {
        background(Button("", action: action).keyboardShortcut(.cancelAction)
            .opacity(0).frame(width: 0, height: 0).accessibilityHidden(true))
    }
}

extension CanvasController: Identifiable {
    nonisolated var id: ObjectIdentifier { ObjectIdentifier(self) }
}

struct RamRef: Identifiable { let id: Int }

extension Notification.Name {
    static let clToggleFocusMode = Notification.Name("clToggleFocusMode")
    static let clSplit = Notification.Name("clSplit")
    static let clSwitchPane = Notification.Name("clSwitchPane")
    static let clCloseSplit = Notification.Name("clCloseSplit")
}

/// Split View, Switch Pane and Close Split, wherever the command comes from
/// (the menus, their shortcuts). Split out of CLLayout.body: three
/// .onReceive calls in that one already-large expression were too much for
/// the type checker.
extension View {
    fileprivate func splitPaneCommands(canvas: CanvasController, split: SplitState, document: CoreDocument,
                                       page: Binding<Int>, classicTabs: Bool) -> some View {
        func isOurs(_ n: Notification) -> Bool {
            (n.object as? CanvasController) === canvas || (n.object as? CanvasController) === split.controller
        }
        return self
            .onReceive(NotificationCenter.default.publisher(for: .clSplit)) { n in
                guard isOurs(n) else { return }
                guard !classicTabs else { NSSound.beep(); return }
                split.toggle(document, leftPage: page)
            }
            .onReceive(NotificationCenter.default.publisher(for: .clSwitchPane)) { n in
                guard isOurs(n) else { return }
                // wx: FocusOtherPane -- a bell if there's nothing to switch to.
                guard split.isOpen else { NSSound.beep(); return }
                let other = canvas.splitFocus ? canvas : split.controller
                other.onActivate?()
                other.view?.window?.makeFirstResponder(other.view)
            }
            .onReceive(NotificationCenter.default.publisher(for: .clCloseSplit)) { n in
                guard isOurs(n) else { return }
                guard split.isOpen else { NSSound.beep(); return }
                split.close(document, leftPage: page)
            }
    }
}

// MARK: - The title bar

/// The window around the CedarLogic interface: content up under the title
/// bar with the title hidden, and the red/yellow/green buttons moved down to
/// sit centred on the drawn toolbar's left (MacSetCustomTitlebar). The
/// system's own toolbar is kept hidden -- when SwiftUI brings it back, it
/// covers the drawn one. Simple puts all of it back.
struct WindowConfigurator: NSViewRepresentable {
    let fullBleed: Bool
    var barHeight: CGFloat = 52
    var onWindow: (NSWindow) -> Void = { _ in }

    /// Tells the launch screen about its window the moment it joins one,
    /// before the window is first drawn, so it can wait unseen.
    final class Probe: NSView {
        override func viewWillMove(toWindow newWindow: NSWindow?) {
            super.viewWillMove(toWindow: newWindow)
            if let newWindow { MainActor.assumeIsolated { Splash.shared.holdEarly(newWindow, from: "probe") } }
        }
    }

    func makeNSView(context: Context) -> NSView { Probe() }

    func updateNSView(_ view: NSView, context: Context) {
        DispatchQueue.main.async {
            guard let w = view.window else { return }
            TitlebarManager.shared.apply(to: w, custom: fullBleed, barHeight: barHeight)
            onWindow(w)
        }
    }
}

@MainActor
final class TitlebarManager {
    static let shared = TitlebarManager()

    private struct State {
        var custom = false
        var barHeight: CGFloat = 52
        var origContainer: NSRect
        var origButtons: [NSPoint]
        var observers: [NSObjectProtocol] = []
        var toolbarWatch: NSKeyValueObservation?
        var titleWatch: NSKeyValueObservation?
        var watch: Timer?
    }
    private var states: [ObjectIdentifier: State] = [:]

    private func buttons(_ w: NSWindow) -> [NSButton]? {
        guard let c = w.standardWindowButton(.closeButton), let m = w.standardWindowButton(.miniaturizeButton),
              let z = w.standardWindowButton(.zoomButton) else { return nil }
        return [c, m, z]
    }

    func apply(to w: NSWindow, custom: Bool, barHeight: CGFloat) {
        let id = ObjectIdentifier(w)
        if states[id] == nil {
            guard let b = buttons(w), let container = b[0].superview?.superview else { return }
            var st = State(origContainer: container.frame, origButtons: b.map { $0.frame.origin })
            // AppKit re-lays the title bar (and brings its glass back) on all
            // of these -- going to another window or app and back included,
            // which left the bar faded until a click made the window key.
            for name in [NSWindow.didResizeNotification, NSWindow.didEndLiveResizeNotification,
                         NSWindow.didExitFullScreenNotification, NSWindow.didBecomeKeyNotification,
                         NSWindow.didResignKeyNotification, NSWindow.didBecomeMainNotification,
                         NSWindow.didResignMainNotification, NSWindow.didChangeOcclusionStateNotification,
                         NSWindow.didEnterFullScreenNotification, NSWindow.didDeminiaturizeNotification,
                         NSWindow.didChangeScreenNotification, NSWindow.didChangeBackingPropertiesNotification,
                         NSWindow.willEnterFullScreenNotification, NSWindow.willExitFullScreenNotification,
                         NSWindow.didEndSheetNotification] {
                st.observers.append(NotificationCenter.default.addObserver(forName: name, object: w, queue: .main) { [weak self, weak w] _ in
                    MainActor.assumeIsolated { if let w { self?.relayoutSoon(w) } }
                })
            }
            for name in [NSApplication.didBecomeActiveNotification, NSApplication.didResignActiveNotification] {
                st.observers.append(NotificationCenter.default.addObserver(forName: name, object: nil, queue: .main) { [weak self, weak w] _ in
                    MainActor.assumeIsolated { if let w { self?.relayoutSoon(w) } }
                })
            }
            st.observers.append(NotificationCenter.default.addObserver(forName: NSWindow.willCloseNotification, object: w, queue: .main) { [weak self] _ in
                MainActor.assumeIsolated { self?.forget(id) }
            })
            // And whenever AppKit puts the title bar or its buttons back its
            // own way, at once, before that's ever drawn or clicked: a title
            // bar left full width takes the toolbar's presses (see layout).
            for v in [container] + b {
                v.postsFrameChangedNotifications = true
                st.observers.append(NotificationCenter.default.addObserver(forName: NSView.frameDidChangeNotification, object: v, queue: nil) { [weak self, weak w] _ in
                    MainActor.assumeIsolated { if let w { self?.reassert(w) } }
                })
            }
            // Nothing says when AppKit moves a button's layer (see layout),
            // so a look every so often, while the window can be seen.
            st.watch = Timer.scheduledTimer(withTimeInterval: 0.25, repeats: true) { [weak self, weak w] _ in
                MainActor.assumeIsolated { if let w, w.isVisible { self?.layout(w) } }
            }
            states[id] = st
        }
        states[id]?.custom = custom
        states[id]?.barHeight = barHeight
        w.isRestorable = false
        LastCircuit.note(w)
        // A circuit from Your Circuits is called by its name there (in the
        // Window menu, Mission Control, the Dock), not "circuit.cdl" -- and
        // stays so when SwiftUI sets the title again later.
        Self.nameFromLibrary(w)
        if states[id]?.titleWatch == nil {
            states[id]?.titleWatch = w.observe(\.title, options: [.new]) { w, _ in
                DispatchQueue.main.async { MainActor.assumeIsolated { Self.nameFromLibrary(w) } }
            }
        }
        if custom {
            // The window server is told the whole title bar height across the
            // window is somewhere to drag it from -- the toolbar's buttons
            // included, since SwiftUI can't say they aren't -- and it moved
            // the window when a click on one wobbled. So it isn't to move this
            // window by itself: the bar's background does (WindowDragArea).
            w.isMovable = false
            w.styleMask.insert(.fullSizeContentView)
            w.titlebarAppearsTransparent = true
            w.titleVisibility = .hidden
            w.standardWindowButton(.documentIconButton)?.isHidden = true
            hideToolbar(w)
            if states[id]?.toolbarWatch == nil, let tb = w.toolbar {
                states[id]?.toolbarWatch = tb.observe(\.isVisible, options: [.new]) { [weak self, weak w] _, _ in
                    DispatchQueue.main.async { if let w { self?.hideToolbar(w) } }
                }
            }
        } else {
            states[id]?.toolbarWatch = nil
            w.isMovable = true
            w.titlebarAppearsTransparent = false
            w.titleVisibility = .visible
            w.styleMask.remove(.fullSizeContentView)
            w.toolbar?.isVisible = true
            w.standardWindowButton(.documentIconButton)?.isHidden = false
        }
        layout(w)
    }

    /// The layout put back straight after AppKit changed it. (Not in a loop:
    /// should AppKit keep answering, it's left to the next notification.)
    private func reassert(_ w: NSWindow) {
        guard !reasserting, states[ObjectIdentifier(w)]?.custom == true else { return }
        let now = ProcessInfo.processInfo.systemUptime
        if now - burst.start > 1 { burst = (now, 0) }
        burst.count += 1
        guard burst.count <= 40 else { return }
        reasserting = true
        defer { reasserting = false }
        layout(w)
    }
    private var reasserting = false
    private var burst: (start: TimeInterval, count: Int) = (0, 0)

    /// Now, and again once AppKit has finished its own pass.
    private func relayoutSoon(_ w: NSWindow) {
        layout(w)
        DispatchQueue.main.async { [weak self, weak w] in if let w { self?.layout(w) } }
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.12) { [weak self, weak w] in if let w { self?.layout(w) } }
    }

    private func hideToolbar(_ w: NSWindow) {
        guard states[ObjectIdentifier(w)]?.custom == true, let tb = w.toolbar, tb.isVisible else { return }
        tb.isVisible = false
        layout(w)
    }

    static func nameFromLibrary(_ w: NSWindow) {
        guard let name = Library.displayName(for: NSDocumentController.shared.document(for: w)?.fileURL),
              w.title != name else { return }
        w.title = name
    }

    private func forget(_ id: ObjectIdentifier) {
        states[id]?.watch?.invalidate()
        for o in states[id]?.observers ?? [] { NotificationCenter.default.removeObserver(o) }
        states[id] = nil
    }

    /// layoutTrafficLights: the title bar made as tall as the toolbar, and the
    /// buttons centred in it -- and no wider than them. AppKit's title bar
    /// decides what a press over it is before the toolbar under it hears of
    /// it: with the bar full width, most of each toolbar button was a place
    /// to drag or zoom the window from (its bottom edge alone clicked it), or
    /// a click that wobbled a pixel moved the window instead. Beside the
    /// buttons, the drawn toolbar handles its own presses, background
    /// included (WindowDragArea).
    func layout(_ w: NSWindow) {
        guard let st = states[ObjectIdentifier(w)], let b = buttons(w), let container = b[0].superview?.superview else { return }
        if !st.custom {
            container.frame = NSRect(x: st.origContainer.origin.x, y: w.frame.height - st.origContainer.height,
                                     width: w.frame.width, height: st.origContainer.height)
            for (i, btn) in b.enumerated() { btn.setFrameOrigin(st.origButtons[i]) }
            systemBits(in: container, hidden: false)
            return
        }
        systemBits(in: container, hidden: true)
        if w.styleMask.contains(.fullScreen) { return }
        let spacing = st.origButtons[1].x - st.origButtons[0].x
        // Just around the buttons, with room past the green one for a plain
        // bit of title bar (WindowDragArea's double-click lands there); left
        // of the red one is the bar's, to drag by.
        let f = NSRect(x: 16, y: w.frame.height - st.barHeight,
                       width: 2 + 2 * spacing + b[2].frame.width + 6, height: st.barHeight)
        if container.frame != f { container.frame = f }
        for (i, btn) in b.enumerated() {
            let o = NSPoint(x: 2 + CGFloat(i) * spacing, y: ((st.barHeight - btn.frame.height) / 2).rounded())
            // AppKit also moves a button's layer without its frame following
            // (the green one, in a window that isn't active yet): the frame
            // then looks right and the button is drawn in the wrong place.
            let drawn = btn.layer?.position ?? o
            if btn.frame.origin != o || abs(drawn.x - o.x) > 0.5 || abs(drawn.y - o.y) > 0.5 {
                btn.setFrameOrigin(NSPoint(x: o.x + 1, y: o.y))
                btn.setFrameOrigin(o)
            }
        }
    }
}

extension TitlebarManager {
    /// What the system adds to the title bar that the drawn toolbar replaces:
    /// the title's own chevron menu (the drawn title has one), and macOS 26's
    /// glass over the bar, which blurred the drawn toolbar under it. The wx
    /// window has neither, having no system toolbar.
    fileprivate func systemBits(in container: NSView, hidden: Bool) {
        // Also macOS 26's glass that fades what scrolls under a title bar:
        // the title bar's background (with its NSScrollPocket) and the
        // backdrop behind it, which it turns on whenever the window becomes
        // active -- that was the toolbar going faded after switching back.
        let names = ["TitlebarDecoration", "AutosaveButton", "TitlebarBackground", "ScrollPocket"]
        if let frame = container.superview {
            for v in frame.subviews where String(describing: type(of: v)).contains("BackdropView") {
                if v.isHidden != hidden { v.isHidden = hidden }
            }
        }
        func walk(_ v: NSView) {
            let name = String(describing: type(of: v))
            if names.contains(where: { name.contains($0) }) {
                // Hidden, and see-through as well: AppKit un-hides these on
                // its own, but leaves their transparency alone.
                if v.isHidden != hidden { v.isHidden = hidden }
                let alpha: CGFloat = hidden ? 0 : 1
                if v.alphaValue != alpha { v.alphaValue = alpha }
            }
            for sub in v.subviews { walk(sub) }
        }
        walk(container)
    }
}

// MARK: - The bar's controls

/// A control in the bar, as a real view of its own. The bar is the window's
/// title bar row, and up there macOS decides what a press is before SwiftUI
/// hears of it, and keeps hover to itself (BarHover). This view says it isn't
/// title bar, takes the first click even when the window is at the back, and
/// is lit, with its tip showing (BarTip), while the pointer is on it.
class BarControl: NSView {
    var onHover: (Bool) -> Void = { _ in }
    /// What it is, as its tooltip says.
    var tip: String?
    /// Held down: lit, wherever the pointer goes, until it's let go.
    var held = false
    private(set) var lit = false

    override var isFlipped: Bool { true }
    override var mouseDownCanMoveWindow: Bool { false }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    func light(_ on: Bool) {
        guard on != lit else { return }
        lit = on
        onHover(on)
    }

    // Below the title bar's height macOS's own hover does reach the view (in
    // a window at the back too), and has the pointer looked up. Asking for
    // the moves also has them sent for the whole window, title bar included.
    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        trackingAreas.forEach(removeTrackingArea)
        addTrackingArea(NSTrackingArea(rect: .zero, options: [.mouseEnteredAndExited, .mouseMoved, .activeAlways, .inVisibleRect],
                                       owner: self, userInfo: nil))
    }
    override func mouseEntered(with event: NSEvent) { BarHover.refresh(near: true) }
    override func mouseExited(with event: NSEvent) { BarHover.refresh(near: true) }
    override func mouseMoved(with event: NSEvent) { BarHover.refresh(near: true) }
    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        BarHover.install()
        guard window == nil else { return }
        held = false
        light(false)
        // Gone (its window closed, the bar changed): the tip goes with it.
        DispatchQueue.main.async { BarHover.refresh() }
    }
}

/// A tool's click target: pressed while held, the click on release inside
/// (with some slack), or the position as it's dragged (the speed slider).
/// The circuit's name has its own (TitleClickArea).
struct BarClickArea: NSViewRepresentable {
    var tip: String? = nil
    var onHover: (Bool) -> Void = { _ in }
    var onPress: (Bool) -> Void = { _ in }
    var onClick: () -> Void = {}
    /// For a slider: where along the control the press is (0...1).
    var onDrag: ((CGFloat) -> Void)? = nil

    final class Area: BarControl {
        var onPress: (Bool) -> Void = { _ in }
        var onClick: () -> Void = {}
        var onDrag: ((CGFloat) -> Void)?

        private func point(_ e: NSEvent) -> NSPoint { convert(e.locationInWindow, from: nil) }
        private func inside(_ p: NSPoint) -> Bool { bounds.insetBy(dx: -14, dy: -14).contains(p) }
        private func fraction(_ p: NSPoint) -> CGFloat { min(1, max(0, p.x / max(1, bounds.width))) }

        override func mouseDown(with e: NSEvent) {
            BarTest.note("down \(tip ?? "") clicks=\(e.clickCount)")
            held = true
            onPress(true)
            if !BarTest.on { onDrag?(fraction(point(e))) }
        }
        override func mouseDragged(with e: NSEvent) {
            let p = point(e)
            if BarTest.on { return }
            if let onDrag { onDrag(fraction(p)) } else { onPress(inside(p)) }
        }
        override func mouseUp(with e: NSEvent) {
            held = false
            onPress(false)
            // Under the toolbar test (BarTest) a click is logged, not done.
            if BarTest.on { if inside(point(e)) { BarTest.note("CLICK \(tip ?? "")") } }
            else if onDrag == nil && inside(point(e)) { onClick() }
            BarHover.refresh()
        }
    }

    func makeNSView(context: Context) -> Area { Area() }
    func updateNSView(_ v: Area, context: Context) {
        v.tip = tip
        v.onHover = onHover
        v.onPress = onPress
        v.onClick = onClick
        v.onDrag = onDrag
    }
}

/// A tip and a light for a control that takes its own clicks (a SwiftUI
/// Menu, say): BarHover finds it by where the pointer is, never by a hit
/// test, so it's never between the control and a click.
struct BarTipArea: NSViewRepresentable {
    var tip: String
    var onHover: (Bool) -> Void = { _ in }

    final class Area: BarControl {
        override func hitTest(_ point: NSPoint) -> NSView? { nil }
        override func viewDidMoveToWindow() {
            super.viewDidMoveToWindow()
            if window != nil { BarHover.region(self) }
        }
    }

    func makeNSView(context: Context) -> Area { Area() }
    func updateNSView(_ v: Area, context: Context) {
        v.tip = tip
        v.onHover = onHover
    }
}

/// Which bar control is under the pointer. In the top 32 points of a window
/// (the title bar's height) macOS keeps the pointer's comings and goings to
/// itself: the views under the title bar never hear of them, which left the
/// tools lighting up only along their bottom edge. The pointer's moves do
/// still come to the app, so on each one the control under it is looked up,
/// as the wx app's toolbar does from its mouse motion. A tab strip that's the
/// top row (focus mode) is told where the pointer is, for the same reason.
@MainActor
enum BarHover {
    private static var monitor: Any?
    private(set) static weak var lit: BarControl?
    private static var watch: Timer?
    private struct Follower { weak var view: WindowDragArea.DragView?; var last: CGPoint? }
    private static var followers: [ObjectIdentifier: Follower] = [:]

    static func install() {
        guard monitor == nil else { return }
        monitor = NSEvent.addLocalMonitorForEvents(matching: [.mouseMoved, .leftMouseUp, .leftMouseDown, .rightMouseDown,
                                                              .otherMouseDown, .scrollWheel, .keyDown]) { e in
            MainActor.assumeIsolated {
                switch e.type {
                case .mouseMoved, .leftMouseUp: refresh()
                default: BarTip.dismiss()
                }
            }
            return e
        }
    }

    static func follow(_ v: WindowDragArea.DragView) {
        install()
        followers[ObjectIdentifier(v)] = Follower(view: v, last: nil)
    }
    static func unfollow(_ v: WindowDragArea.DragView) { followers[ObjectIdentifier(v)] = nil }

    private struct Region { weak var view: BarControl? }
    private static var regions: [Region] = []
    /// A BarTipArea: found by where the pointer is (see refresh).
    static func region(_ v: BarControl) {
        install()
        regions.removeAll { $0.view == nil || $0.view === v }
        regions.append(Region(view: v))
    }

    /// What's under the pointer now: the control there lit, the one before
    /// not. (`near`: it's known to be by a control.)
    static func refresh(near: Bool = false) {
        let m = NSEvent.mouseLocation
        let following = followers.values.contains { $0.last != nil }
        // Nowhere near a window's top rows, with nothing lit: nothing to do.
        let atTop = near || NSApp.windows.contains { $0.isVisible && $0.frame.contains(m) && m.y > $0.frame.maxY - 96 }
        guard atTop || lit != nil || following else { stopWatching(); return }
        let w = NSApp.window(withWindowNumber: NSWindow.windowNumber(at: m, belowWindowWithWindowNumber: 0))
        var found: BarControl?
        if let w, w.attachedSheet == nil, let frame = w.contentView?.superview {
            let p = w.convertPoint(fromScreen: m)
            var v = frame.hitTest(p)
            while let x = v, found == nil { found = x as? BarControl; v = x.superview }
            if found == nil {
                found = regions.lazy.compactMap(\.view).first {
                    $0.window === w && !$0.isHiddenOrHasHiddenAncestor && $0.convert($0.bounds, to: nil).contains(p)
                }
            }
        }
        if lit?.held != true, found !== lit {
            if found == nil && lit != nil { BarTest.note("unlit") }
            if let found { BarTest.note("lit \(found.tip ?? "")") }
            lit?.light(false)
            lit = found
            found?.light(true)
            BarTip.follow(found)
        }
        for (id, f) in followers {
            guard let view = f.view else { followers[id] = nil; continue }
            var p: CGPoint?
            if let vw = view.window, vw === w {
                let q = view.convert(vw.convertPoint(fromScreen: m), from: nil)
                if view.bounds.contains(q) { p = q }
            }
            if p != f.last {
                followers[id]?.last = p
                view.onPointer?(p)
            }
        }
        // Off the window, or over to another app, no moves come: a look now
        // and then puts the lights out.
        if lit != nil || followers.values.contains(where: { $0.last != nil }) {
            if watch == nil {
                watch = Timer.scheduledTimer(withTimeInterval: 0.12, repeats: true) { _ in MainActor.assumeIsolated { refresh() } }
            }
        } else {
            stopWatching()
        }
    }

    private static func stopWatching() {
        watch?.invalidate()
        watch = nil
    }
}

/// A bar control's tooltip, shown the way macOS shows one: after a moment's
/// hover, straight away when moving on from one that's showing, gone on a
/// click or a key. (macOS's own come with its hover, which it keeps to itself
/// up there.)
@MainActor
enum BarTip {
    private static var panel: NSPanel?
    private static var label: NSTextField?
    private static var pending: DispatchWorkItem?
    private static weak var shownFor: BarControl?
    /// Clicked while lit: no tip again until the pointer moves on.
    private static weak var quiet: BarControl?
    private static var hiddenAt: TimeInterval = 0

    static func follow(_ c: BarControl?) {
        pending?.cancel()
        pending = nil
        let showing = shownFor != nil
        if c !== shownFor { hide() }
        if c !== quiet { quiet = nil }
        guard let c, c !== quiet, let text = c.tip, !text.isEmpty else { return }
        let soon = showing || ProcessInfo.processInfo.systemUptime - hiddenAt < 0.5
        let work = DispatchWorkItem { MainActor.assumeIsolated { show(text, for: c) } }
        pending = work
        DispatchQueue.main.asyncAfter(deadline: .now() + (soon ? 0.05 : 1.0), execute: work)
    }

    static func dismiss() {
        pending?.cancel()
        pending = nil
        if let c = BarHover.lit { quiet = c }
        hide()
    }

    private static func show(_ text: String, for c: BarControl) {
        guard c.lit, let w = c.window, w.isVisible, NSApp.isActive else { return }
        let (p, l) = make()
        l.stringValue = text
        p.appearance = w.effectiveAppearance
        let size = l.fittingSize
        let box = NSSize(width: ceil(size.width) + 14, height: ceil(size.height) + 6)
        l.frame = NSRect(x: 7, y: 3, width: ceil(size.width), height: ceil(size.height))
        let m = NSEvent.mouseLocation
        var f = NSRect(x: m.x - 4, y: m.y - 22 - box.height, width: box.width, height: box.height)
        if let s = (NSScreen.screens.first { $0.frame.contains(m) } ?? w.screen)?.visibleFrame {
            f.origin.x = min(max(f.minX, s.minX + 2), s.maxX - f.width - 2)
            if f.minY < s.minY + 2 { f.origin.y = m.y + 8 }
        }
        p.setFrame(f, display: true)
        p.invalidateShadow()
        p.orderFront(nil)
        shownFor = c
        BarTest.note("tip shown: \(text)")
    }

    private static func hide() {
        shownFor = nil
        guard let p = panel, p.isVisible else { return }
        p.orderOut(nil)
        hiddenAt = ProcessInfo.processInfo.systemUptime
    }

    private static func make() -> (NSPanel, NSTextField) {
        if let panel, let label { return (panel, label) }
        let p = NSPanel(contentRect: .zero, styleMask: [.borderless, .nonactivatingPanel], backing: .buffered, defer: true)
        p.isOpaque = false
        p.backgroundColor = .clear
        p.hasShadow = true
        p.ignoresMouseEvents = true
        p.level = .popUpMenu
        p.hidesOnDeactivate = true
        p.isReleasedWhenClosed = false
        p.collectionBehavior = [.transient, .ignoresCycle, .fullScreenAuxiliary]
        let fx = NSVisualEffectView()
        fx.material = .toolTip
        fx.blendingMode = .behindWindow
        fx.state = .active
        let corners = NSImage(size: NSSize(width: 13, height: 13), flipped: false) { r in
            NSBezierPath(roundedRect: r, xRadius: 6, yRadius: 6).fill()
            return true
        }
        corners.capInsets = NSEdgeInsets(top: 6, left: 6, bottom: 6, right: 6)
        corners.resizingMode = .stretch
        fx.maskImage = corners
        let l = NSTextField(labelWithString: "")
        l.font = .toolTipsFont(ofSize: 0)
        l.textColor = .labelColor
        fx.addSubview(l)
        p.contentView = fx
        panel = p
        label = l
        return (p, l)
    }
}

/// The toolbar's background, doing what a title bar does: it drags the
/// window, and a double-click does the System Settings action. (macOS isn't
/// left to move this window by itself -- see TitlebarManager -- so this is
/// what moves it.) A tab strip that's the top row has one too.
struct WindowDragArea: NSViewRepresentable {
    /// A press, before the window drags (a tab strip: its side comes forward).
    var onPress: (() -> Void)? = nil
    /// Instead of the title bar's double-click (a tab strip: a new tab).
    var onDoubleClick: (() -> Void)? = nil
    /// Where the pointer is over this, or nil (BarHover).
    var onPointer: ((CGPoint?) -> Void)? = nil

    final class DragView: NSView {
        var onPress: (() -> Void)?
        var onDoubleClick: (() -> Void)?
        var onPointer: ((CGPoint?) -> Void)? {
            didSet { if (onPointer == nil) != (oldValue == nil) { refollow() } }
        }

        override var isFlipped: Bool { true }
        /// Presses here are this view's own (below), never the window
        /// server's to start moving the window by itself.
        override var mouseDownCanMoveWindow: Bool { false }
        /// A title bar moves its window on the first click, even in the
        /// background.
        override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

        override func viewDidMoveToWindow() {
            super.viewDidMoveToWindow()
            refollow()
        }
        private func refollow() {
            if window != nil && onPointer != nil { BarHover.follow(self) } else { BarHover.unfollow(self) }
        }

        override func mouseDown(with event: NSEvent) {
            guard let w = window else { return }
            BarTest.note("dragview down clicks=\(event.clickCount)")
            if event.clickCount >= 2 {
                if let onDoubleClick { onDoubleClick() } else { Self.titlebarDoubleClick(w) }
                return
            }
            onPress?()
            // The system's own window drag: to other screens and Spaces, and
            // a filled or tiled window goes back to its size as it's dragged.
            w.performDrag(with: event)
        }

        /// Whatever a title bar's double-click does in System Settings (Fill,
        /// Zoom, Minimize or nothing), done by AppKit itself: the window's
        /// frame (the title bar's own handler) is handed the double-click as
        /// if it had landed on the plain title bar just past the green button.
        /// Straight to it: sent through the window, a second click goes back
        /// to the view that had the first. (Nothing public performs Fill.)
        static func titlebarDoubleClick(_ w: NSWindow) {
            guard let zoom = w.standardWindowButton(.zoomButton), let bar = zoom.superview,
                  let frame = w.contentView?.superview else { return }
            let z = zoom.convert(zoom.bounds, to: nil)
            let b = bar.convert(bar.bounds, to: nil)
            let p = NSPoint(x: min(z.maxX + 3, b.maxX - 1), y: b.maxY - 8)
            func event(_ type: NSEvent.EventType) -> NSEvent? {
                NSEvent.mouseEvent(with: type, location: p, modifierFlags: [], timestamp: ProcessInfo.processInfo.systemUptime,
                                   windowNumber: w.windowNumber, context: nil, eventNumber: 0, clickCount: 2, pressure: 1)
            }
            guard let down = event(.leftMouseDown), let up = event(.leftMouseUp) else { return }
            frame.mouseDown(with: down)
            frame.mouseUp(with: up)
        }
    }

    func makeNSView(context: Context) -> DragView { DragView() }
    func updateNSView(_ v: DragView, context: Context) {
        v.onPress = onPress
        v.onDoubleClick = onDoubleClick
        v.onPointer = onPointer
    }
}

// MARK: - Toolbar

/// The drawn toolbar (the wx app's ModernToolbar), 52 points tall in the title
/// bar row. Classic groups its tools in capsules, Seamless blends into the
/// canvas and keeps idle tools quiet until you point at them, Minimal has the
/// essentials and the circuit's name, with the rest behind •••.
struct CLToolbar: View {
    let document: CoreDocument
    @ObservedObject var canvas: CanvasController
    @ObservedObject var status: CanvasStatus
    let title: String
    let subtitle: String
    @Binding var focusMode: Bool
    /// Drawn in this style instead of the chosen one (Settings' pictures).
    var styleOverride: ToolbarStyle? = nil
    @ObservedObject private var prefs = Prefs.shared
    @ObservedObject private var keys = ShortcutStore.shared
    @State private var moreHover = false

    private var style: ToolbarStyle { styleOverride ?? prefs.toolbarStyle }
    private var dark: Bool { prefs.dark }
    private var chrome: CLChrome { CLChrome(dark: dark) }
    private var ink: Color { chrome.barInk }
    private var accent: Color { prefs.accentColor(dark: dark) }

    private var barColor: Color {
        switch style {
        case .seamless: canvas.simView ? CLPalette(dark: true, simView: true).canvas : chrome.canvas
        case .classic: chrome.classicBar
        case .minimal: chrome.minimalBar
        }
    }

    private func tip(_ text: String, _ a: ShortcutAction) -> String {
        if let c = keys.combo(a) { return "\(text) (\(c.label))" }
        return text
    }

    var body: some View {
        ZStack {
            barColor
            if styleOverride == nil { WindowDragArea() }
            if style == .minimal { minimal } else { full }
        }
        .frame(height: 52)
        .overlay(alignment: .bottom) {
            if style != .seamless { Rectangle().fill(dark ? Color.black.opacity(0.35) : Color.black.opacity(0.08)).frame(height: 1) }
        }
        .animation(.easeOut(duration: 0.18), value: canvas.simView)
    }

    private func button(_ icon: String, _ tip: String, on: Bool = false, disabled: Bool = false,
                        colored: Bool = false, action: @escaping () -> Void) -> some View {
        TBButton(icon: icon, tip: tip, on: on, disabled: disabled, colored: colored, quiet: style == .seamless,
                 ink: ink, accent: accent, dark: dark, action: action)
    }

    /// Classic groups related tools in a soft capsule, the way macOS does.
    private func group<C: View>(@ViewBuilder _ content: () -> C) -> some View {
        HStack(spacing: 0) { content() }
            .padding(.horizontal, 3).padding(.vertical, 1)
            .background {
                if style == .classic {
                    Capsule().fill(dark ? Color.white.opacity(0.063) : Color.black.opacity(0.043))
                        .overlay(Capsule().strokeBorder(dark ? Color.white.opacity(0.07) : Color.black.opacity(0.063)))
                }
            }
    }

    private var runButton: some View {
        button(canvas.simView ? "stop.fill" : "play.fill",
               canvas.simView ? "Leave Simulation View (Esc)" : tip("Simulation View", .simView),
               on: canvas.simView, colored: true) { canvas.simView.toggle() }
    }

    private var pauseButton: some View {
        button(canvas.isRunning ? "pause.fill" : "play.fill",
               canvas.isRunning ? "Pause the simulation" : "Resume the simulation",
               on: !canvas.isRunning) { canvas.toggleRunning() }
    }

    @ViewBuilder private var leftTools: some View {
        if prefs.shown(.file) {
            group {
                button("doc.badge.plus", tip("New circuit", .newCircuit)) { canvas.perform(.newCircuit) }
                button("folder", tip("Your circuits", .openLibrary)) { canvas.perform(.openLibrary) }
                button("square.and.arrow.down", tip("Save", .save)) { canvas.perform(.save) }
            }
        }
        if prefs.shown(.undo) {
            group {
                button("arrow.uturn.backward", tip("Undo", .undo), disabled: !canvas.canUndo) { canvas.undo() }
                button("arrow.uturn.forward", tip("Redo", .redo), disabled: !canvas.canRedo) { canvas.redo() }
            }
        }
        if prefs.shown(.clipboard) {
            group {
                button("doc.on.doc", tip("Copy", .copy)) { canvas.copy() }
                button("clipboard", tip("Paste", .paste)) { canvas.perform(.paste) }
            }
        }
        if prefs.shown(.zoom) {
            group {
                button("minus.magnifyingglass", tip("Zoom out", .zoomOut)) { canvas.zoomOut() }
                ZoomReadout(canvas: canvas, status: status, ink: ink, quiet: style == .seamless)
                button("plus.magnifyingglass", tip("Zoom in", .zoomIn)) { canvas.zoomIn() }
            }
        }
    }

    @ViewBuilder private var rightTools: some View {
        if prefs.shown(.sim) {
            group {
                pauseButton
                button("forward.frame.fill", tip("Step once", .step)) { canvas.stepOnce() }
                TBSpeed(stepMs: $canvas.stepMs, ink: ink, quiet: style == .seamless)
            }
        }
        if prefs.shown(.run) { group { runButton } }
        if prefs.shown(.lock) {
            group {
                button(canvas.locked ? "lock.fill" : "lock.open",
                       canvas.locked ? "Unlock the circuit" : "Lock the circuit so it can't be edited",
                       on: canvas.locked) { canvas.locked.toggle() }
            }
        }
        if prefs.shown(.tab) {
            group { button("plus.square.on.square", tip("New tab", .newTab)) { canvas.newPage() } }
        }
        if prefs.shown(.feedback) {
            group { button("exclamationmark.bubble", tip("Send feedback: a bug, an idea, anything", .feedback)) { canvas.perform(.feedback) } }
        }
    }

    private var full: some View {
        HStack(spacing: 12) {
            Color.clear.frame(width: 74, height: 1)
            if prefs.showTitle { TitleMenu(title: title, subtitle: nil, ink: ink, live: styleOverride == nil) }
            ViewThatFits(in: .horizontal) {
                HStack(spacing: 12) { leftTools }
                EmptyView()
            }
            Spacer(minLength: 8)
            rightTools
        }
        .padding(.trailing, 12)
    }

    private var minimal: some View {
        ZStack {
            if prefs.showTitle { TitleMenu(title: title, subtitle: subtitle, ink: ink, live: styleOverride == nil) }
            HStack(spacing: 6) {
                Color.clear.frame(width: 74, height: 1)
                if prefs.shown(.undo) {
                    button("arrow.uturn.backward", tip("Undo", .undo), disabled: !canvas.canUndo) { canvas.undo() }
                    button("arrow.uturn.forward", tip("Redo", .redo), disabled: !canvas.canRedo) { canvas.redo() }
                }
                Spacer()
                if prefs.shown(.sim) { pauseButton }
                if prefs.shown(.run) { runButton }
                Menu {
                    Button("New Circuit") { canvas.perform(.newCircuit) }
                    Button("Your Circuits…") { canvas.perform(.openLibrary) }
                    Button("Save") { canvas.perform(.save) }
                    Divider()
                    Button("Copy") { canvas.copy() }
                    Button("Paste") { canvas.perform(.paste) }
                    Divider()
                    Button("Zoom In") { canvas.zoomIn() }
                    Button("Zoom Out") { canvas.zoomOut() }
                    Button("Actual Size") { canvas.zoomActual() }
                    Button("Zoom to Fit") { canvas.zoomToFit() }
                    Divider()
                    Button("Step") { canvas.stepOnce() }
                    Button(canvas.locked ? "Unlock" : "Lock") { canvas.locked.toggle() }
                    Button("New Tab") { canvas.newPage() }
                    Button(focusMode ? "Leave Focus Mode" : "Focus Mode") { focusMode.toggle() }
                    Divider()
                    Button("Send Feedback…") { canvas.perform(.feedback) }
                } label: {
                    Image(systemName: "ellipsis").font(.system(size: 15)).foregroundStyle(ink.opacity(0.9))
                        .frame(width: 34, height: 30)
                }
                .menuStyle(.borderlessButton)
                .menuIndicator(.hidden)
                .fixedSize()
                .background(RoundedRectangle(cornerRadius: 7).fill(ink.opacity(moreHover ? 0.08 : 0)))
                .overlay(BarTipArea(tip: "More", onHover: { h in withAnimation(.easeOut(duration: 0.12)) { moreHover = h } })
                    .allowsHitTesting(false))
            }
            .padding(.trailing, 12)
        }
    }
}

/// The circuit's name at the top left: click for Rename, Move To, Duplicate
/// and the saved versions -- the menu macOS puts on a window's title. One
/// chevron (a SwiftUI Menu adds its own), and hidden with Settings >
/// Toolbar > Show the circuit's name.
private struct TitleMenu: View {
    let title: String
    let subtitle: String?
    let ink: Color
    /// False in a picture of the toolbar: nothing to click.
    var live = true
    @State private var hover = false

    var body: some View {
        VStack(spacing: 1) {
            HStack(spacing: 5) {
                Text(title).font(.system(size: 13, weight: .semibold)).foregroundStyle(ink)
                Image(systemName: "chevron.down").font(.system(size: 8, weight: .bold))
                    .foregroundStyle(ink.opacity(hover ? 0.7 : 0.35))
            }
            if let subtitle { Text(subtitle).font(.system(size: 11)).foregroundStyle(ink.opacity(0.5)) }
        }
        .lineLimit(1)
        .padding(.horizontal, 8).padding(.vertical, 4)
        .background(RoundedRectangle(cornerRadius: 7).fill(ink.opacity(hover ? 0.08 : 0)))
        // The whole name is one button, and a real view: the bar behind it
        // drags the window, and it used to take clicks everywhere but one
        // spot of the name.
        .overlay { if live { TitleClickArea(hover: $hover) } }
        .fixedSize()
    }
}

/// The title's click target: pops the circuit's menu under itself.
private struct TitleClickArea: NSViewRepresentable {
    @Binding var hover: Bool

    final class Area: BarControl {
        override func mouseDown(with event: NSEvent) {
            BarTest.note("down \(tip ?? "") clicks=\(event.clickCount)")
            if BarTest.on { BarTest.note("CLICK \(tip ?? "")"); return }
            TitleActions.popUp(in: self)
            BarHover.refresh()
        }
    }

    func makeNSView(context: Context) -> Area {
        let a = Area()
        a.tip = "Rename, duplicate or look back through this circuit"
        return a
    }
    func updateNSView(_ v: Area, context: Context) {
        v.onHover = { h in withAnimation(.easeOut(duration: 0.12)) { hover = h } }
    }
}

/// The circuit's menu (the title's). A circuit from Your Circuits is renamed
/// there, and isn't moved out of it: its file is the library's.
@MainActor
enum TitleActions {
    private final class Target: NSObject {
        static let shared = Target()
        @objc func renameInLibrary() { MainActor.assumeIsolated { TitleActions.renameInLibrary() } }
    }

    static func popUp(in anchor: NSView) {
        let doc = anchor.window.flatMap { NSDocumentController.shared.document(for: $0) }
        let inLibrary = Library.item(for: doc?.fileURL) != nil
        let menu = NSMenu()
        func add(_ t: String, _ sel: Selector, target: AnyObject? = nil) {
            let i = NSMenuItem(title: t, action: sel, keyEquivalent: "")
            i.target = target
            menu.addItem(i)
        }
        if inLibrary {
            add("Rename…", #selector(Target.renameInLibrary), target: Target.shared)
        } else {
            add("Rename…", #selector(NSDocument.rename(_:)))
            add("Move To…", #selector(NSDocument.move(_:)))
        }
        add("Duplicate", #selector(NSDocument.duplicate(_:)))
        menu.addItem(.separator())
        add("Revert to Last Saved", #selector(NSDocument.revertToSaved(_:)))
        add("Version History…", #selector(VersionsOpener.open), target: VersionsOpener.shared)
        menu.popUp(positioning: nil, at: NSPoint(x: 0, y: anchor.bounds.height + 4), in: anchor)
    }

    static func renameInLibrary() {
        guard let w = CanvasController.front?.view?.window,
              let item = Library.item(for: NSDocumentController.shared.document(for: w)?.fileURL) else { return }
        let alert = NSAlert()
        alert.messageText = "Rename Circuit"
        alert.informativeText = "The name it has in Your Circuits."
        let field = NSTextField(string: item.name)
        field.frame = NSRect(x: 0, y: 0, width: 260, height: 24)
        alert.accessoryView = field
        alert.addButton(withTitle: "Rename")
        alert.addButton(withTitle: "Cancel")
        alert.window.initialFirstResponder = field
        alert.beginSheetModal(for: w) { r in
            let name = field.stringValue.trimmingCharacters(in: .whitespaces)
            guard r == .alertFirstButtonReturn, !name.isEmpty else { return }
            Library.rename(item, to: name)
            CanvasController.front?.objectWillChange.send()   // the title reads it again
        }
    }
}

/// Opens Version History from AppKit menus (the title's menu).
@MainActor
final class VersionsOpener: NSObject {
    static let shared = VersionsOpener()
    @objc func open() { CanvasController.front?.openWindow?("versions") }
}

private struct ZoomReadout: View {
    let canvas: CanvasController
    @ObservedObject var status: CanvasStatus
    let ink: Color
    let quiet: Bool
    @State private var hover = false

    var body: some View {
        let _ = status.version
        Text("\(canvas.zoomPercent)%").font(.system(size: 12)).monospacedDigit()
            .foregroundStyle(ink.opacity(quiet && !hover ? 0.5 : 0.92))
            .frame(width: 48, height: 30)
            .background(RoundedRectangle(cornerRadius: 7).fill(ink.opacity(hover ? 0.08 : 0)).padding(.horizontal, 2))
            .overlay(BarClickArea(
                tip: "Zoom level. Click for 100%",
                onHover: { h in withAnimation(.easeOut(duration: 0.12)) { hover = h } },
                onClick: { canvas.zoomActual() }))
    }
}

/// One toolbar tool: an icon that fades up under the pointer, filled with the
/// accent when it's switched on. Seamless keeps idle tools quiet.
private struct TBButton: View {
    let icon: String
    let tip: String
    let on: Bool
    let disabled: Bool
    let colored: Bool
    let quiet: Bool
    let ink: Color
    let accent: Color
    let dark: Bool
    let action: () -> Void
    @State private var hover = false
    @State private var pressed = false

    var body: some View {
        Image(systemName: icon)
            .font(.system(size: 14.5, weight: .regular))
            .foregroundStyle(fg)
            .frame(width: 34, height: 30)
            .background {
                RoundedRectangle(cornerRadius: 7)
                    .fill(on ? accent.opacity(pressed ? 0.30 : (hover ? 0.24 : 0.18))
                          : (dark ? Color.white.opacity(pressed ? 0.13 : 0.08) : Color.black.opacity(pressed ? 0.094 : 0.05)))
                    .opacity(on || hover ? 1 : 0)
                    .padding(.horizontal, 2).padding(.vertical, 1)
            }
            .overlay(BarClickArea(
                tip: tip,
                onHover: { h in withAnimation(.easeOut(duration: 0.12)) { hover = h && !disabled } },
                onPress: { p in if pressed != (p && !disabled) { pressed = p && !disabled } },
                onClick: { if !disabled { action() } }))
            .animation(.easeOut(duration: 0.15), value: on)
            .accessibilityElement()
            .accessibilityLabel(tip)
            .accessibilityAddTraits(.isButton)
            .accessibilityAction { if !disabled { action() } }
    }

    private var fg: Color {
        if on || colored { return accent }
        let alpha = disabled ? 0.3 : (quiet && !hover ? 0.5 : 0.92)
        return ink.opacity(alpha)
    }
}

/// The simulation speed: a gauge and a slim slider, fast on the right.
private struct TBSpeed: View {
    @Binding var stepMs: Int
    let ink: Color
    let quiet: Bool
    @State private var hover = false
    private let trackWidth: CGFloat = 58

    private var fraction: CGFloat { 1 - min(1, max(0, CGFloat(log(Double(max(1, stepMs))) / log(500)))) }

    var body: some View {
        HStack(spacing: 6) {
            Image(systemName: "gauge.with.dots.needle.50percent").font(.system(size: 13))
                .foregroundStyle(ink.opacity(quiet && !hover ? 0.5 : 0.9))
            ZStack(alignment: .leading) {
                Capsule().fill(ink.opacity(0.16)).frame(width: trackWidth, height: 3)
                Capsule().fill(ink.opacity(quiet && !hover ? 0.45 : 0.7)).frame(width: max(3, trackWidth * fraction), height: 3)
                Circle().fill(Color.white).overlay(Circle().strokeBorder(Color.black.opacity(0.2)))
                    .frame(width: 13, height: 13)
                    .shadow(color: .black.opacity(0.2), radius: 1, y: 0.5)
                    .offset(x: trackWidth * fraction - 6.5)
            }
            .frame(width: trackWidth, height: 30)
            .overlay(BarClickArea(
                tip: "Simulation speed: \(stepMs) ms a step",
                onHover: { h in withAnimation(.easeOut(duration: 0.12)) { hover = h } },
                onDrag: { f in stepMs = max(1, min(500, Int(pow(500, 1 - Double(f)).rounded()))) }))
        }
        .padding(.horizontal, 6)
    }
}

// MARK: - Tabs and the split view

/// What a tab held out of its strip would do if let go there.
enum TabDropHint: Equatable {
    case none
    /// Down over the canvas with no split yet: open one, the tab on this
    /// half (-1 left, 1 right).
    case split(Int)
    /// Over the other side of a split: move the tab there.
    case move(Int)
}

/// The two sides of the canvas (MainFrame's panes). The first side's page is
/// the window's `page`; the second has its own controller and the tabs in
/// `sideIDs`. Pages are known by CoreDocument.pageID, so both sides follow
/// their tabs as tabs move, close and reopen. A side that runs out of tabs
/// closes the split, as in the wx app.
@MainActor
final class SplitState: ObservableObject {
    /// The tabs in the second side. Empty: no split.
    @Published private(set) var sideIDs: Set<UInt64> = []
    /// The second side's page.
    @Published private(set) var sideActive: UInt64 = 0
    /// The second side sits left of the first (a tab split onto the left half).
    @Published private(set) var sideFirst = false
    @Published var hint = TabDropHint.none
    /// The left side's share of the width.
    @Published var fraction: CGFloat = 0.5
    /// The first side's page, by id.
    private(set) var mainID: UInt64 = 0
    /// Tabs most recently used first (the partner a new split opens with,
    /// and Ctrl+Tab's order).
    private(set) var recent: [UInt64] = []
    /// Where each side is in the canvas area (for drops).
    var paneFrames: [Int: CGRect] = [:]
    /// The whole canvas area (tab strips included), kept up to date by its
    /// own layout: with no split, it's what a tab is dropped on.
    var areaSize = CGSize.zero
    weak var primary: CanvasController?

    let controller: CanvasController = {
        let c = CanvasController()
        c.drivesClock = false
        return c
    }()

    var isOpen: Bool { !sideIDs.isEmpty }

    /// The pages in a side's tab strip, in order.
    func pages(_ pane: Int, _ doc: CoreDocument) -> [Int] {
        let n = doc.pageCount
        guard isOpen else { return pane == 0 ? Array(0..<n) : [] }
        return (0..<n).filter { sideIDs.contains(doc.pageID($0)) == (pane == 1) }
    }

    func sidePage(_ doc: CoreDocument) -> Int? { isOpen ? doc.pageIndex(of: sideActive) : nil }

    private func used(_ id: UInt64) {
        recent.removeAll { $0 == id }
        recent.insert(id, at: 0)
        if recent.count > 40 { recent.removeLast() }
    }

    /// The first side now shows `page`.
    func noteMain(_ page: Int, _ doc: CoreDocument) {
        guard page >= 0, page < doc.pageCount else { return }
        let id = doc.pageID(page)
        if id != mainID { mainID = id }
        used(id)
    }

    /// The first side was asked to show a page (a new tab, the next tab, a
    /// reopened one). A tab that lives in the second side stays there.
    func showMain(_ p: Int, _ doc: CoreDocument, leftPage: Binding<Int>) {
        guard p >= 0, p < doc.pageCount, !sideIDs.contains(doc.pageID(p)) else { return }
        mainID = doc.pageID(p)
        used(mainID)
        if leftPage.wrappedValue != p { leftPage.wrappedValue = p }
    }

    /// The second side was asked to show a page. One of its own: shown there.
    /// Any other (a reopened tab): back in the first strip, like wx.
    func showSide(_ p: Int, _ doc: CoreDocument, leftPage: Binding<Int>) {
        guard p >= 0, p < doc.pageCount else { return }
        let id = doc.pageID(p)
        if sideIDs.contains(id) {
            select(id)
        } else {
            showMain(p, doc, leftPage: leftPage)
            primary?.splitFocus = false
        }
    }

    /// A tab made from the second side's strip belongs to it.
    func adoptNew(_ p: Int, _ doc: CoreDocument) {
        guard p >= 0, p < doc.pageCount else { return }
        let id = doc.pageID(p)
        sideIDs.insert(id)
        select(id)
    }

    func select(_ id: UInt64) {
        if sideActive != id { sideActive = id }
        used(id)
    }

    /// Everything back in line after pages were added, closed, reopened or
    /// moved: the first side follows its page by id, a gone tab gives way to
    /// its neighbour, and a side left without tabs closes the split.
    func reconcile(_ doc: CoreDocument, leftPage: Binding<Int>) {
        let n = doc.pageCount
        guard n > 0 else { return }
        let ids = (0..<n).map { doc.pageID($0) }
        var side = sideIDs.intersection(ids)
        if !side.isEmpty && side.count == n {
            // The first side ran out: the second side's tabs become its own
            // (MainFrame::collapsePaneIfEmpty), still showing the same page.
            if let i = doc.pageIndex(of: sideActive) { mainID = ids[i] }
            side = []
        }
        var main = doc.pageIndex(of: mainID) ?? min(max(leftPage.wrappedValue, 0), n - 1)
        if side.contains(ids[main]) {
            main = Self.nearest(main, n) { !side.contains(ids[$0]) } ?? main
        }
        if !side.isEmpty && !side.contains(sideActive) {
            sideActive = recent.first { side.contains($0) } ?? ids.first { side.contains($0) } ?? 0
        }
        if side != sideIDs {
            sideIDs = side
            if side.isEmpty { primary?.splitFocus = false }
        }
        mainID = ids[main]
        if leftPage.wrappedValue != main { leftPage.wrappedValue = main }
    }

    private static func nearest(_ from: Int, _ n: Int, _ ok: (Int) -> Bool) -> Int? {
        for d in 0..<n {
            if from + d < n, ok(from + d) { return from + d }
            if from - d >= 0, ok(from - d) { return from - d }
        }
        return nil
    }

    /// Split the view with tab `p` on one side (MainFrame::SplitWith). The
    /// first side can't be left empty: taking its last tab gives it a new one.
    func splitWith(_ p: Int, onRight: Bool, _ doc: CoreDocument, leftPage: Binding<Int>) {
        guard !isOpen, p >= 0, p < doc.pageCount else { return }
        let id = doc.pageID(p)
        if doc.pageCount < 2 {
            guard let j = primary?.addBlankPage() else { return }
            mainID = doc.pageID(j)
            leftPage.wrappedValue = j
        }
        sideFirst = !onRight
        fraction = 0.5
        sideActive = id
        used(id)
        sideIDs = [id]
        primary?.splitFocus = true
        reconcile(doc, leftPage: leftPage)
        primary?.note("Split view. Drag tabs between the two sides; the split closes when a side runs out.")
    }

    /// Split View from the menu or keys: the tab used most recently beside
    /// the one in front, or a new tab if it's the only one. Again: closes it.
    func toggle(_ doc: CoreDocument, leftPage: Binding<Int>) {
        if isOpen { close(doc, leftPage: leftPage); return }
        let front = doc.pageID(leftPage.wrappedValue)
        var partner = recent.first { $0 != front && doc.pageIndex(of: $0) != nil }.flatMap { doc.pageIndex(of: $0) }
        if partner == nil { partner = (0..<doc.pageCount).first { doc.pageID($0) != front } }
        if partner == nil { partner = primary?.addBlankPage() }
        guard let p = partner else { return }
        splitWith(p, onRight: true, doc, leftPage: leftPage)
    }

    /// Move tab `p` to the other side (MainFrame::MoveCanvasToPane).
    func move(_ p: Int, to pane: Int, _ doc: CoreDocument, leftPage: Binding<Int>) {
        guard isOpen, p >= 0, p < doc.pageCount else { return }
        let id = doc.pageID(p)
        if pane == 1 {
            sideIDs.insert(id)
            select(id)
            primary?.splitFocus = true
        } else {
            var side = sideIDs
            side.remove(id)
            mainID = id
            used(id)
            leftPage.wrappedValue = p
            primary?.splitFocus = false
            sideIDs = side
        }
        reconcile(doc, leftPage: leftPage)
    }

    /// One strip again, with every tab; the side you were in stays in front.
    func close(_ doc: CoreDocument, leftPage: Binding<Int>) {
        guard isOpen else { return }
        if primary?.splitFocus == true, let s = sidePage(doc) {
            mainID = sideActive
            leftPage.wrappedValue = s
        }
        sideIDs = []
        primary?.splitFocus = false
        primary?.note("")
        hint = .none
    }
}

/// The wx app's TabStrip: a glassy strip with the active tab as a card of the
/// canvas's own colour, a dot that's the accent on the tab you're in, a close
/// cross on the active and hovered tab, and a new-tab plus. Drag a tab along
/// the strip to reorder; down over the canvas to split the view; over the
/// other side of a split to move it there. Double-click a tab to rename it,
/// the empty strip for a new tab. Each side of a split has its own strip;
/// the side you're in has an accent rail under it, the other steps back.
struct CLTabStrip: View {
    @ObservedObject var document: CoreDocument
    /// The window's first controller (the second is split.controller).
    @ObservedObject var canvas: CanvasController
    @ObservedObject var split: SplitState
    let pane: Int
    @Binding var page: Int
    let leftInset: CGFloat
    /// The window's top row (focus mode): its background drags the window,
    /// and the hover comes from BarHover, SwiftUI's own not reaching up there.
    var titleRow = false
    @ObservedObject private var prefs = Prefs.shared
    @State private var hover: Int?          // a place in the strip; -2: the plus
    @State private var hoverClose: Int?
    @State private var dragID: UInt64?
    @State private var dragDX: CGFloat = 0
    @State private var dragMoving = false
    @State private var lastClickID: UInt64?
    @State private var lastClickTime = Date.distantPast
    @State private var renaming: UInt64?
    @State private var newName = ""
    @State private var stripWidth: CGFloat = 800
    @FocusState private var nameFocused: Bool

    static let height: CGFloat = 36
    private let tabH: CGFloat = 30, gap: CGFloat = 2, edge: CGFloat = 8, plusW: CGFloat = 26

    private var chrome: CLChrome { CLChrome(dark: prefs.dark) }
    private var controller: CanvasController { pane == 0 ? canvas : split.controller }
    private var pages: [Int] { split.pages(pane, document) }
    /// The page this side shows.
    private var shown: Int? { pane == 0 ? page : split.sidePage(document) }
    /// The side you're working in (always, without a split).
    private var activePane: Bool { !split.isOpen || (pane == 1) == canvas.splitFocus }

    private func tabWidth(_ n: Int) -> CGFloat {
        let n = CGFloat(max(1, n))
        let room = stripWidth - leftInset - edge - plusW - gap
        var tw = (room - gap * (n - 1)) / n
        tw = max(86, min(220, tw))
        if n * (220 + gap) < room { tw = min(tw, 160) }
        return tw
    }

    private func x(_ k: Int, _ tw: CGFloat) -> CGFloat { leftInset + CGFloat(k) * (tw + gap) }

    var body: some View {
        if prefs.classicTabs {
            classic
        } else {
            modern
        }
    }

    private var modern: some View {
        let pages = self.pages
        let tw = tabWidth(pages.count)
        let accent = prefs.accentColor(dark: prefs.dark)
        return ZStack(alignment: .topLeading) {
            // Glass: a gentle gradient with a bright hairline along the top.
            if titleRow {
                LinearGradient(colors: [chrome.tabBarTop, chrome.tabBar], startPoint: .top, endPoint: .bottom)
                    .allowsHitTesting(false)
                WindowDragArea(onPress: { activate() }, onDoubleClick: { if BarTest.on { BarTest.note("strip doubleclick") } else { controller.newPage() } },
                               onPointer: { pointer($0, pages, tw) })
            } else {
                LinearGradient(colors: [chrome.tabBarTop, chrome.tabBar], startPoint: .top, endPoint: .bottom)
                    .contentShape(Rectangle())
                    .onTapGesture(count: 2) { controller.newPage() }
                    .onTapGesture { activate() }
            }
            Rectangle().fill(Color.white.opacity(prefs.dark ? 0.07 : 0.9)).frame(height: 1)
                .allowsHitTesting(false)
            dropGap(pages, tw)
            ForEach(pages.enumerated().map { TabItem(k: $0.offset, p: $0.element, id: document.pageID($0.element)) }) { t in
                let k = t.k, p = t.p, id = t.id
                tab(k, p, id: id, pages: pages, tw: tw)
                    .offset(x: dragID == id && dragMoving ? x(k, tw) + dragDX : x(k, tw) + makeRoom(k, pages, tw),
                            y: (Self.height - tabH) / 2)
                    .zIndex(dragID == id ? 1 : 0)
                    .animation(dragID == id ? nil : .spring(response: 0.26, dampingFraction: 0.86), value: makeRoom(k, pages, tw))
            }
            plus.offset(x: x(pages.count, tw), y: (Self.height - tabH) / 2)
            // A hairline under the strip -- or, in a split, an accent rail
            // under the side in use, and the other side stepping back.
            if split.isOpen && activePane {
                Rectangle().fill(accent).frame(height: 2).offset(y: Self.height - 2)
                    .allowsHitTesting(false)
            } else {
                Rectangle().fill(chrome.tabInk.opacity(0.12)).frame(height: 1).offset(y: Self.height - 1)
                    .allowsHitTesting(false)
            }
            if split.isOpen && !activePane {
                Rectangle().fill(chrome.tabBar.opacity(0.45)).frame(height: Self.height - 1)
                    .allowsHitTesting(false)
            }
        }
        .frame(height: Self.height)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(GeometryReader { g in Color.clear.onAppear { stripWidth = g.size.width }
            .onChange(of: g.size.width) { _, w in stripWidth = w } })
        // In or out of the title row, the hover starts over.
        .onChange(of: titleRow) { _, _ in hover = nil; hoverClose = nil }
        .clipped()
        .animation(.easeOut(duration: 0.14), value: hover)
        .animation(.easeOut(duration: 0.14), value: shown)
        .animation(.easeOut(duration: 0.18), value: activePane)
    }

    /// The system's own tabs, as the wx app's classic tabs are: centred, no
    /// dragging and no split.
    private var classic: some View {
        ZStack {
            Picker("", selection: $page) {
                ForEach(0..<document.pageCount, id: \.self) { Text(document.pageName($0)).tag($0) }
            }
            .pickerStyle(.segmented).labelsHidden().fixedSize()
            HStack {
                Spacer()
                Button { canvas.newPage() } label: { Image(systemName: "plus") }.buttonStyle(.borderless).help("New tab")
            }
        }
        .padding(.leading, leftInset).padding(.trailing, 10).frame(height: Self.height)
        .background {
            ZStack {
                chrome.tabBar
                if titleRow { WindowDragArea(onPress: { activate() }) }
            }
        }
    }

    /// The hover in the title row, from where the pointer is: the tab under
    /// it (and its close button), or the plus.
    private func pointer(_ pt: CGPoint?, _ pages: [Int], _ tw: CGFloat) {
        var h: Int?
        var close: Int?
        if let pt, abs(pt.y - Self.height / 2) <= tabH / 2 {
            for k in pages.indices {
                let x0 = x(k, tw) + makeRoom(k, pages, tw)
                guard pt.x >= x0 && pt.x < x0 + tw else { continue }
                h = k
                if pt.x > x0 + tw - 28 && pt.x < x0 + tw - 4 { close = k }
                break
            }
            // The "+" lights itself (BarClickArea), but this runs on every
            // move over the strip too: it must agree, not put the light out.
            let px = x(pages.count, tw)
            if h == nil && pt.x >= px && pt.x < px + plusW { h = -2 }
        }
        if hover != h || hoverClose != close { BarTest.note("strip hover \(h.map(String.init) ?? "-") close \(close.map(String.init) ?? "-")") }
        if hover != h { hover = h }
        if hoverClose != close { hoverClose = close }
    }

    private func activate() {
        controller.onActivate?()
        if renaming == nil { controller.view?.window?.makeFirstResponder(controller.view) }
    }

    /// Where the dragged tab would land in this strip: the slot under its middle.
    private func dropSlot(_ pages: [Int], _ tw: CGFloat) -> Int? {
        guard let id = dragID, dragMoving, split.hint == .none,
              let k = pages.firstIndex(where: { document.pageID($0) == id }) else { return nil }
        let cx = x(k, tw) + dragDX + tw / 2
        for j in 0..<pages.count where cx < x(j, tw) + tw { return j }
        return pages.count
    }

    /// While a tab is dragged along the strip, the tabs it passes slide
    /// aside to open the gap it would drop into.
    private func makeRoom(_ k: Int, _ pages: [Int], _ tw: CGFloat) -> CGFloat {
        guard let at = dropSlot(pages, tw), let id = dragID,
              let d = pages.firstIndex(where: { document.pageID($0) == id }), k != d else { return 0 }
        if at > d + 1 && k > d && k < at { return -(tw + gap) }
        if at < d && k >= at && k < d { return tw + gap }
        return 0
    }

    /// The gap itself: an accent outline where the tab will land.
    @ViewBuilder private func dropGap(_ pages: [Int], _ tw: CGFloat) -> some View {
        if let at = dropSlot(pages, tw), let id = dragID,
           let d = pages.firstIndex(where: { document.pageID($0) == id }), at != d, at != d + 1 {
            let accent = prefs.accentColor(dark: prefs.dark)
            let landing = at > d ? at - 1 : at
            RoundedRectangle(cornerRadius: 8)
                .fill(accent.opacity(0.14))
                .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(accent.opacity(0.9), style: StrokeStyle(lineWidth: 1.5, dash: [5, 3])))
                .frame(width: tw, height: tabH)
                .offset(x: x(landing, tw), y: (Self.height - tabH) / 2)
                .allowsHitTesting(false)
                .transition(.opacity)
                .animation(.spring(response: 0.26, dampingFraction: 0.86), value: landing)
        }
    }

    /// A BarClickArea, not plain SwiftUI hover/tap: in focus mode this strip
    /// is the window's top row, where AppKit tells no view about the pointer
    /// entering or leaving (see BarHover) -- neither the "+" lighting up nor
    /// its tooltip would otherwise ever show there.
    private var plus: some View {
        Image(systemName: "plus").font(.system(size: 12, weight: .medium))
            .foregroundStyle(chrome.tabInk.opacity(0.75))
            .frame(width: plusW, height: tabH)
            .background(RoundedRectangle(cornerRadius: 9).fill(chrome.tabInk.opacity(hover == -2 ? 0.08 : 0)))
            .overlay(BarClickArea(
                tip: "New tab",
                onHover: { h in withAnimation(.easeOut(duration: 0.12)) { hover = h ? -2 : (hover == -2 ? nil : hover) } },
                onClick: { controller.onActivate?(); controller.newPage() }))
    }

    @ViewBuilder private func tab(_ k: Int, _ p: Int, id: UInt64, pages: [Int], tw: CGFloat) -> some View {
        // As in the wx app, the card and the accent dot belong to the one tab
        // you're working in; the other side's page reads as a quiet tab.
        let active = p == shown && activePane
        let hot = hover == k
        let moving = dragID == id && dragMoving
        let accent = prefs.accentColor(dark: prefs.dark)
        let ink = chrome.tabInk
        let nextLoud = k + 1 < pages.count && ((pages[k + 1] == shown && activePane) || hover == k + 1)
        ZStack(alignment: .leading) {
            if active {
                RoundedRectangle(cornerRadius: 8).fill(Color.black.opacity(prefs.dark ? 0.26 : 0.10)).offset(y: 1)
                RoundedRectangle(cornerRadius: 8)
                    .fill(LinearGradient(colors: [chrome.tabCardLit, chrome.tabCard], startPoint: .top, endPoint: .bottom))
                    .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(ink.opacity(prefs.dark ? 0.18 : 0.10)))
                    .overlay(alignment: .top) {
                        Rectangle().fill(Color.white.opacity(prefs.dark ? 0.10 : 0.85)).frame(height: 1)
                            .padding(.horizontal, 8).padding(.top, 1)
                    }
            } else {
                RoundedRectangle(cornerRadius: 8).fill(ink.opacity(hot || moving ? 0.06 : 0))
                if k + 1 < pages.count && !nextLoud && !hot && !moving {
                    Rectangle().fill(ink.opacity(0.14)).frame(width: 1, height: tabH - 13)
                        .offset(x: tw + gap / 2 - 0.5)
                }
            }
            HStack(spacing: 0) {
                Circle().fill(active ? accent : ink.opacity(0.28)).frame(width: 6, height: 6).padding(.leading, 9)
                if renaming == id {
                    TextField("", text: $newName)
                        .textFieldStyle(.plain)
                        .font(.system(size: 11.5, weight: .bold))
                        .foregroundStyle(ink)
                        .focused($nameFocused)
                        .onSubmit { commitRename(id) }
                        .onExitCommand { renaming = nil; activate() }
                        .padding(.leading, 9)
                        .onChange(of: nameFocused) { _, f in if !f && renaming == id { commitRename(id) } }
                } else {
                    Text(document.pageName(p))
                        .font(.system(size: 11.5, weight: active ? .bold : .regular))
                        .foregroundStyle(ink.opacity(active ? 1 : 0.7))
                        .lineLimit(1).truncationMode(.tail)
                        .padding(.leading, 9)
                }
                Spacer(minLength: 26)
            }
            if document.pageCount > 1 && (active || hot) && renaming != id {
                Image(systemName: "xmark").font(.system(size: 8.5, weight: .semibold))
                    .foregroundStyle(ink.opacity(0.7))
                    .frame(width: 20, height: 20)
                    .background(RoundedRectangle(cornerRadius: 6).fill(ink.opacity(hoverClose == k ? 0.16 : 0)))
                    .offset(x: tw - 26)
            }
        }
        .frame(width: tw, height: tabH)
        .contentShape(Rectangle())
        .shadow(color: moving ? .black.opacity(0.3) : .clear, radius: 6, y: 2)
        .scaleEffect(moving ? 1.03 : 1)
        .onContinuousHover { phase in
            guard !titleRow else { return }
            switch phase {
            case .active(let pt):
                if hover != k { hover = k }
                let onClose = pt.x > tw - 28 && pt.x < tw - 4
                if (hoverClose == k) != onClose { hoverClose = onClose ? k : nil }
            case .ended:
                if hover == k { hover = nil }
                if hoverClose == k { hoverClose = nil }
            }
        }
        .gesture(tabGesture(p, id: id, tw: tw))
        .contextMenu {
            Button("Rename…") { beginRename(id) }
            if split.isOpen {
                Button("Move to Other Side") {
                    if let i = document.pageIndex(of: id) { split.move(i, to: 1 - pane, document, leftPage: $page) }
                }
                Button("Close Split View") { split.close(document, leftPage: $page) }
            } else if !prefs.classicTabs {
                Button("Open in Split View") {
                    if let i = document.pageIndex(of: id) { split.splitWith(i, onRight: true, document, leftPage: $page) }
                }
            }
            Divider()
            Button("Close Tab") {
                if let i = document.pageIndex(of: id) { controller.closePage(i) }
            }
            .disabled(document.pageCount < 2)
        }
    }

    private func select(_ p: Int) {
        if pane == 0 {
            split.noteMain(p, document)
            page = p
        } else {
            split.select(document.pageID(p))
        }
        controller.onActivate?()
    }

    /// Where a tab held at `loc` (the canvas area's space) would go.
    private func hint(at loc: CGPoint) -> TabDropHint {
        if split.isOpen {
            let other = 1 - pane
            if let f = split.paneFrames[other], f.contains(loc) { return .move(other) }
            return .none
        }
        guard pane == 0, document.pageCount > 0, split.areaSize.width > 0 else { return .none }
        let f = CGRect(origin: .zero, size: split.areaSize)
        if loc.y < f.minY + Self.height + 12 || loc.y > f.maxY || loc.x < f.minX || loc.x > f.maxX { return .none }
        return .split(loc.x < f.midX ? -1 : 1)
    }

    private func tabGesture(_ p: Int, id: UInt64, tw: CGFloat) -> some Gesture {
        DragGesture(minimumDistance: 0, coordinateSpace: .named("clArea"))
            .onChanged { v in
                if dragID != id {
                    dragID = id; dragDX = 0; dragMoving = false
                    if renaming != id, let i = document.pageIndex(of: id) { select(i) }
                }
                dragDX = v.translation.width
                if !dragMoving && (abs(v.translation.width) > 6 || abs(v.translation.height) > 8) { dragMoving = true }
                let h = dragMoving && !prefs.classicTabs ? hint(at: v.location) : .none
                if split.hint != h { withAnimation(.easeOut(duration: 0.15)) { split.hint = h } }
            }
            .onEnded { v in
                let moved = dragMoving
                let pages = self.pages
                let slot = dropSlot(pages, tw)
                let h = split.hint
                if h != .none { withAnimation(.easeOut(duration: 0.15)) { split.hint = .none } }
                withAnimation(.spring(response: 0.28, dampingFraction: 0.85)) { dragID = nil; dragMoving = false; dragDX = 0 }
                guard let i = document.pageIndex(of: id), let k = pages.firstIndex(of: i) else { return }
                if !moved {
                    // A click: on the cross it closes; twice on the name renames.
                    let localX = v.startLocation.x - (split.paneFrames[pane]?.minX ?? 0) - x(k, tw)
                    if document.pageCount > 1 && localX > tw - 28 { controller.closePage(i); return }
                    if lastClickID == id && Date().timeIntervalSince(lastClickTime) < 0.4 {
                        beginRename(id)
                        lastClickID = nil
                    } else {
                        lastClickID = id
                        lastClickTime = Date()
                        activate()
                    }
                    return
                }
                switch h {
                case .move(let to):
                    split.move(i, to: to, document, leftPage: $page)
                    return
                case .split(let side):
                    split.splitWith(i, onRight: side > 0, document, leftPage: $page)
                    return
                case .none:
                    break
                }
                if let at = slot, at != k, at != k + 1 {
                    // Within this strip: before the tab now at `at`, or after
                    // the one before it (the move takes the tab out first).
                    let to = at < k ? pages[at] : pages[at - 1]
                    withAnimation(.spring(response: 0.3, dampingFraction: 0.86)) {
                        controller.movePage(from: i, to: to)
                        split.reconcile(document, leftPage: $page)
                    }
                }
                activate()
            }
    }

    private func beginRename(_ id: UInt64) {
        guard let i = document.pageIndex(of: id) else { return }
        newName = document.pageName(i)
        renaming = id
        DispatchQueue.main.async { nameFocused = true }
    }

    private func commitRename(_ id: UInt64) {
        guard renaming == id else { return }
        renaming = nil
        let name = newName.trimmingCharacters(in: .whitespaces)
        if let i = document.pageIndex(of: id), !name.isEmpty, name != document.pageName(i) {
            document.renamePage(i, to: name)
            document.objectWillChange.send()
            controller.markEdited()
        }
        controller.view?.window?.makeFirstResponder(controller.view)
    }
}

private struct TabItem: Identifiable {
    let k: Int, p: Int, id: UInt64
}

// MARK: - Canvas area

/// The canvas, or two side by side (each with its own tab strip, the wx
/// app's panes), with the oscilloscope under them.
struct CLCanvasArea: View {
    let document: CoreDocument
    @ObservedObject var canvas: CanvasController
    @Binding var page: Int
    @ObservedObject var split: SplitState
    let leftInset: CGFloat
    /// Focus mode: the tab strips are the window's top row, its title bar.
    var titleRow = false
    @ObservedObject private var prefs = Prefs.shared
    @State private var sashStart: CGFloat?
    @State private var sashHover = false

    private var chrome: CLChrome { CLChrome(dark: prefs.dark) }

    var body: some View {
        VSplitView {
            panes
                .frame(minHeight: 200)
            if canvas.showScope {
                ScopeView(document: document, canvas: canvas)
                    .frame(minHeight: 140, idealHeight: 220)
            }
        }
    }

    private var panes: some View {
        // GeometryReader has no size of its own: without this the window
        // opened a few points across.
        GeometryReader { g in
            let W = g.size.width
            let open = split.isOpen && !prefs.classicTabs
            let order = open ? (split.sideFirst ? [1, 0] : [0, 1]) : [0]
            let leftW = open ? (W * split.fraction).rounded() : W
            // Opening or closing the split is a cut, as in wx.
            HStack(spacing: 1) {
                ForEach(order, id: \.self) { p in
                    paneView(p, inset: p == order[0] ? leftInset : 8)
                        .frame(width: max(0, p == order[0] ? leftW : W - leftW - 1))
                }
            }
            .background(chrome.sash)
            .overlay(alignment: .topLeading) {
                if open { sash(at: leftW, width: W, height: g.size.height) }
            }
            .overlay { dropHint }
            .background(Color.clear.onAppear { split.areaSize = g.size }
                .onChange(of: g.size) { _, sz in split.areaSize = sz })
        }
        .coordinateSpace(name: "clArea")
        .frame(minWidth: 440, idealWidth: 1000, minHeight: 200, idealHeight: 640)
    }

    /// The divider between the sides: drag it, or double-click for halves.
    private func sash(at x: CGFloat, width: CGFloat, height: CGFloat) -> some View {
        Rectangle().fill(Color.accentColor.opacity(sashHover ? 0.5 : 0))
            .frame(width: 3, height: height)
            .padding(.horizontal, 3)
            .contentShape(Rectangle())
            .offset(x: x - 4)
            .onHover { h in
                withAnimation(.easeOut(duration: 0.12)) { sashHover = h }
                if h { NSCursor.resizeLeftRight.push() } else { NSCursor.pop() }
            }
            .onTapGesture(count: 2) { withAnimation(.easeInOut(duration: 0.2)) { split.fraction = 0.5 } }
            .gesture(DragGesture(minimumDistance: 1, coordinateSpace: .named("clArea"))
                .onChanged { v in
                    if sashStart == nil { sashStart = split.fraction * width }
                    let lo = min(0.45, 220 / max(width, 1))
                    split.fraction = min(max(((sashStart ?? 0) + v.translation.width) / max(width, 1), lo), 1 - lo)
                }
                .onEnded { _ in sashStart = nil })
    }

    @ViewBuilder
    private func paneView(_ p: Int, inset: CGFloat) -> some View {
        let c = p == 0 ? canvas : split.controller
        let shown = p == 0 ? page : (split.sidePage(document) ?? 0)
        VStack(spacing: 0) {
            CLTabStrip(document: document, canvas: canvas, split: split, pane: p, page: $page, leftInset: inset,
                       titleRow: titleRow)
            if shown < document.pageCount {
                CLCanvasHost(document: document, page: shown, controller: c)
                    .overlay(alignment: .top) { TidyBanner(canvas: c) }
                    .overlay { EmptyHint(document: document, controller: c, page: shown) }
                    .overlay(alignment: .topTrailing) { LockBadge(canvas: c) }
                    .overlay(alignment: .bottom) {
                        if canvas.simView && p == 0 {
                            SimBar(document: document, canvas: canvas, page: shown)
                                .transition(.move(edge: .bottom).combined(with: .opacity))
                        }
                    }
                    .animation(.spring(response: 0.34, dampingFraction: 0.86), value: canvas.simView)
            } else {
                chrome.canvas
            }
        }
        .frame(minWidth: 0)
        .background(GeometryReader { g in
            Color.clear
                .onAppear { split.paneFrames[p] = g.frame(in: .named("clArea")) }
                .onChange(of: g.frame(in: .named("clArea"))) { _, f in split.paneFrames[p] = f }
        })
        .onAppear {
            guard p == 1 else { return }
            c.undoManager = canvas.undoManager
            c.openWindow = canvas.openWindow
            c.locked = canvas.locked
            canvas.partner = c
            c.partner = canvas
        }
        .onDisappear {
            if p == 1 {
                canvas.partner = nil
                split.paneFrames[1] = nil
            }
        }
    }

    /// "Drop to split here" over half the canvas, or "Drop to move here" over
    /// the other side, while a tab is held there.
    @ViewBuilder private var dropHint: some View {
        let accent = prefs.accentColor(dark: prefs.dark)
        let rect: CGRect? = {
            switch split.hint {
            case .none: return nil
            case .split(let side):
                let f = CGRect(origin: .zero, size: split.areaSize)
                let top = f.minY + CLTabStrip.height
                let w = f.width / 2
                return CGRect(x: side < 0 ? f.minX : f.minX + w, y: top, width: w, height: f.maxY - top)
            case .move(let to):
                return split.paneFrames[to]
            }
        }()
        if let r = rect {
            let label: String = { if case .move = split.hint { return "Drop to move here" }; return "Drop to split here" }()
            RoundedRectangle(cornerRadius: 14)
                .fill(accent.opacity(0.18))
                .overlay(RoundedRectangle(cornerRadius: 14).strokeBorder(accent, lineWidth: 2))
                .overlay(Text(label).font(.system(size: 13, weight: .bold)).foregroundStyle(accent))
                .frame(width: max(0, r.width - 6), height: max(0, r.height - 6))
                .position(x: r.midX, y: r.midY)
                .allowsHitTesting(false)
                .transition(.opacity)
        }
    }
}

/// "Drag a gate here to start" on an empty page, drifting up into place as
/// the page appears (GUICanvas::drawEmptyHintInto).
private struct EmptyHint: View {
    let document: CoreDocument
    @ObservedObject var controller: CanvasController
    let page: Int
    @ObservedObject private var prefs = Prefs.shared
    @State private var shown = false

    var body: some View {
        let _ = controller.editVersion
        let empty = document.bounds(ofPage: page) == nil && !controller.simView
        Text("Drag a gate here to start")
            .font(.system(size: 16))
            .foregroundStyle((prefs.dark ? Color.white : Color.black).opacity(0.22))
            .opacity(empty && shown ? 1 : 0)
            .offset(y: shown ? 0 : 14)
            .allowsHitTesting(false)
            .onAppear { withAnimation(.easeOut(duration: 0.32)) { shown = true } }
            .onChange(of: page) { _, _ in
                shown = false
                withAnimation(.easeOut(duration: 0.32)) { shown = true }
            }
    }
}

/// While the circuit is locked: a badge saying so, with Unlock. It bounces
/// when something was refused, so it's clear why nothing happened.
private struct LockBadge: View {
    @ObservedObject var canvas: CanvasController
    @ObservedObject private var prefs = Prefs.shared
    @State private var bounce = false

    var body: some View {
        if canvas.locked && !canvas.simView {
            HStack(spacing: 8) {
                Image(systemName: "lock.fill")
                Text("Locked").fontWeight(.semibold)
                Button("Unlock") { withAnimation { canvas.locked = false } }
                    .buttonStyle(.borderless)
                    .foregroundStyle(prefs.accentColor(dark: prefs.dark))
            }
            .font(.system(size: 12))
            .padding(.horizontal, 12).padding(.vertical, 7)
            .background(Capsule().fill(.regularMaterial))
            .overlay(Capsule().strokeBorder(Color.primary.opacity(0.1)))
            .shadow(color: .black.opacity(0.2), radius: 6, y: 2)
            .scaleEffect(bounce ? 1.08 : 1)
            .padding(12)
            .transition(.move(edge: .top).combined(with: .opacity))
            .onChange(of: canvas.lockNudgeCount) { _, _ in
                withAnimation(.spring(response: 0.18, dampingFraction: 0.4)) { bounce = true }
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.18) {
                    withAnimation(.spring(response: 0.3, dampingFraction: 0.6)) { bounce = false }
                }
            }
            .help("Editing is off. Switches and keypads still work.")
        }
    }
}

/// The canvas view in CedarLogic mode.
struct CLCanvasHost: View {
    let document: CoreDocument
    let page: Int
    let controller: CanvasController
    @ObservedObject private var prefs = Prefs.shared

    var body: some View {
        let _ = prefs.dark, _ = prefs.accent, _ = prefs.showGrid, _ = prefs.gridStyle, _ = prefs.majorGrid
        let _ = prefs.wireThickness, _ = prefs.wireDotsAtBends, _ = prefs.wireDotSize, _ = prefs.lowWire
        CanvasView(document: document, page: page, theme: LookStore.shared.settings.theme, controller: controller,
                   clMode: true)
    }
}

// MARK: - Status bar

struct CLStatusBar: View {
    let document: CoreDocument
    let canvas: CanvasController
    @ObservedObject var status: CanvasStatus
    @ObservedObject private var prefs = Prefs.shared

    var body: some View {
        let _ = status.version
        let chrome = CLChrome(dark: prefs.dark)
        HStack(spacing: 18) {
            StatusMessage(canvas: canvas)
            Spacer()
            if prefs.showStatus {
                Text("\(canvas.zoomPercent)%").monospacedDigit()
                Text(String(format: "x %.1f   y %.1f", canvas.pointer.x, canvas.pointer.y)).monospacedDigit()
                Text(counts)
            }
        }
        .font(.system(size: 11))
        .foregroundStyle(chrome.tabInk.opacity(0.6))
        .padding(.horizontal, 12)
        .frame(height: 22)
        .background(chrome.tabBar)
        .overlay(alignment: .top) { Rectangle().fill(chrome.tabInk.opacity(0.10)).frame(height: 1) }
    }

    private var counts: String {
        let gates = Int(cl_document_gate_count(document.handle, Int32(canvas.page)))
        let sel = Int(cl_edit_selected_gate_count(document.handle, Int32(canvas.page)) +
                      cl_edit_selected_wire_count(document.handle, Int32(canvas.page)))
        var s = "\(gates) gate\(gates == 1 ? "" : "s")"
        if sel > 0 { s += " · \(sel) selected" }
        return s
    }
}

/// The status line's message, fading in, and out again after a while.
private struct StatusMessage: View {
    @ObservedObject var canvas: CanvasController
    var body: some View {
        Text(canvas.statusMessage).lineLimit(1)
            .id(canvas.statusMessage)
            .transition(.opacity)
            .animation(.easeOut(duration: 0.2), value: canvas.statusMessage)
            .onChange(of: canvas.statusMessage) { _, m in
                guard !m.isEmpty else { return }
                DispatchQueue.main.asyncAfter(deadline: .now() + 4) { if canvas.statusMessage == m { canvas.statusMessage = "" } }
            }
    }
}
