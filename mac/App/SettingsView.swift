// Settings (⌘,), laid out like the wx app's Preferences: the system's row of
// pages in the toolbar, the window easing to each page's height as you switch,
// and each setting a label on the left with its control and a line of
// explanation on the right. General picks the interface: CedarLogic (with
// Appearance, Canvas, Toolbar and Shortcuts) or Simple (Layout and Look).

import AppKit
import SwiftUI

enum SettingsPage: String, CaseIterable, Identifiable {
    case general, appearance, canvas, toolbar, shortcuts, layout, look
    var id: String { rawValue }
    var title: String {
        switch self {
        case .general: "General"
        case .appearance: "Appearance"
        case .canvas: "Canvas"
        case .toolbar: "Toolbar"
        case .shortcuts: "Shortcuts"
        case .layout: "Layout"
        case .look: "Look"
        }
    }
    var icon: String {
        switch self {
        case .general: "gearshape"
        case .appearance: "paintpalette"
        case .canvas: "cursorarrow.rays"
        case .toolbar: "menubar.rectangle"
        case .shortcuts: "keyboard"
        case .layout: "rectangle.3.group"
        case .look: "swatchpalette"
        }
    }
}

/// Settings (⌘,), made the way the wx app's is (wxPreferencesEditor): a
/// window with its pages as icons in the toolbar. Switching pages empties the
/// window, eases it to the new page's height, then fades the page in.
@MainActor
final class PrefsWindow: NSObject, NSToolbarDelegate, NSWindowDelegate {
    static let shared = PrefsWindow()
    private var window: NSWindow?
    private var current: SettingsPage = .general
    private var switching = false
    private var watch: Any?

    private var pages: [SettingsPage] {
        Prefs.shared.interface == .cedarlogic ? [.general, .appearance, .canvas, .toolbar, .shortcuts] : [.general, .layout, .look]
    }

    func show() {
        if window == nil { build() }
        guard let w = window else { return }
        if !pages.contains(current) { current = .general }
        if !w.isVisible {
            select(current, animated: false)
            w.center()
        }
        w.appearance = NSAppearance(named: Prefs.shared.dark ? .darkAqua : .aqua)
        w.makeKeyAndOrderFront(nil)
        NSApp.activate()
    }

    private func build() {
        let w = EscapeClosesWindow(contentRect: NSRect(x: 0, y: 0, width: 640, height: 300),
                         styleMask: [.titled, .closable, .miniaturizable], backing: .buffered, defer: false)
        w.isReleasedWhenClosed = false
        w.toolbarStyle = .preference
        w.delegate = self
        // Over a full-screen circuit rather than off on the desktop (wx
        // MacKeepPanelsOnActiveSpace).
        w.collectionBehavior.insert([.fullScreenAuxiliary, .moveToActiveSpace])
        w.contentView = NSView()
        window = w
        makeToolbar()
        // The pages change with the interface (General picks it), and the
        // window follows the theme.
        watch = Prefs.shared.objectWillChange.sink { [weak self] _ in
            DispatchQueue.main.async {
                guard let self, let w = self.window else { return }
                w.appearance = NSAppearance(named: Prefs.shared.dark ? .darkAqua : .aqua)
                let ids = self.pages.map(\.rawValue)
                if w.toolbar?.items.map(\.itemIdentifier.rawValue) != ids {
                    self.makeToolbar()
                    if !self.pages.contains(self.current) { self.select(.general, animated: true) }
                    else { w.toolbar?.selectedItemIdentifier = .init(self.current.rawValue) }
                }
            }
        }
    }

    private func makeToolbar() {
        let tb = NSToolbar(identifier: "cl.settings.\(Prefs.shared.interface == .cedarlogic ? "cl" : "simple")")
        tb.delegate = self
        tb.displayMode = .iconAndLabel
        tb.allowsUserCustomization = false
        window?.toolbar = tb
        tb.selectedItemIdentifier = .init(current.rawValue)
    }

    // MARK: Toolbar

    func toolbarAllowedItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] { pages.map { .init($0.rawValue) } }
    func toolbarDefaultItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] { pages.map { .init($0.rawValue) } }
    func toolbarSelectableItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] { pages.map { .init($0.rawValue) } }

    func toolbar(_ toolbar: NSToolbar, itemForItemIdentifier id: NSToolbarItem.Identifier,
                 willBeInsertedIntoToolbar flag: Bool) -> NSToolbarItem? {
        guard let page = SettingsPage(rawValue: id.rawValue) else { return nil }
        let item = NSToolbarItem(itemIdentifier: id)
        item.label = page.title
        item.image = NSImage(systemSymbolName: page.icon, accessibilityDescription: page.title)
        item.target = self
        item.action = #selector(pick(_:))
        return item
    }

    @objc private func pick(_ item: NSToolbarItem) {
        guard let page = SettingsPage(rawValue: item.itemIdentifier.rawValue) else { return }
        select(page, animated: true)
    }

    // MARK: Pages

    private var pageController: NSHostingController<AnyView>?

    private func host(_ page: SettingsPage) -> NSHostingController<AnyView> {
        let c = NSHostingController(rootView: AnyView(SettingsPageView(page: page).environmentObject(LookStore.shared)))
        c.sizingOptions = []   // the window's size is ours to animate
        return c
    }

    /// The wx page switch: the old page goes, the window eases to the new
    /// one's height (its top edge staying put), then the new page fades in.
    private func select(_ page: SettingsPage, animated: Bool) {
        guard let w = window, let content = w.contentView, !(switching && animated) else { return }
        current = page
        w.title = page.title
        w.toolbar?.selectedItemIdentifier = .init(page.rawValue)
        let controller = host(page)
        pageController = controller
        let view = controller.view
        // What the page wants: 640 wide, as tall as its content.
        let size = controller.sizeThatFits(in: NSSize(width: 640, height: 10_000))
        let contentRect = NSRect(x: 0, y: 0, width: max(size.width, 640), height: size.height)
        var frame = w.frameRect(forContentRect: contentRect)
        frame.origin.x = w.frame.origin.x
        frame.origin.y = w.frame.maxY - frame.height
        content.subviews.forEach { $0.removeFromSuperview() }
        view.frame = contentRect
        view.autoresizingMask = [.width, .height]
        guard animated, w.isVisible else {
            w.setFrame(frame, display: true)
            content.addSubview(view)
            return
        }
        switching = true
        view.alphaValue = 0
        NSAnimationContext.runAnimationGroup({ ctx in
            ctx.duration = 0.32
            ctx.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
            w.animator().setFrame(frame, display: true)
        }, completionHandler: { [weak self] in
            MainActor.assumeIsolated {
                content.addSubview(view)
                NSAnimationContext.runAnimationGroup { ctx in
                    ctx.duration = 0.28
                    ctx.timingFunction = CAMediaTimingFunction(name: .easeOut)
                    view.animator().alphaValue = 1
                }
                self?.switching = false
            }
        })
    }
}

/// Escape closes Settings, as it does every other window -- but a field
/// being typed in lets go first (wx PrefsPanel), and a shortcut being
/// recorded takes it itself (its key monitor comes first).
final class EscapeClosesWindow: NSWindow {
    override func cancelOperation(_ sender: Any?) {
        if firstResponder is NSText { makeFirstResponder(nil); return }
        performClose(nil)
    }
}

/// One Settings page, padded and sized as it sits in the window.
struct SettingsPageView: View {
    let page: SettingsPage

    var body: some View {
        SettingsView.content(page)
            .padding(.horizontal, 28).padding(.vertical, 22)
            .frame(width: 640, height: page == .shortcuts ? 540 : nil, alignment: .top)
            .fixedSize(horizontal: false, vertical: page != .shortcuts)
    }
}

enum SettingsView {
    @ViewBuilder static func content(_ p: SettingsPage) -> some View {
        switch p {
        case .general: GeneralSettingsView()
        case .appearance: CLAppearanceSettings()
        case .canvas: CLCanvasSettings()
        case .toolbar: CLToolbarSettings()
        case .shortcuts: CLShortcutSettings()
        case .layout: LayoutSettingsView()
        case .look: LookSettingsView()
        }
    }
}

/// One setting: "Label:" on the left, the control and a line of help on the right.
private struct Row<Control: View>: View {
    let label: String
    let hint: String?
    @ViewBuilder let control: () -> Control

    init(_ label: String, hint: String? = nil, @ViewBuilder control: @escaping () -> Control) {
        self.label = label
        self.hint = hint
        self.control = control
    }

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: 12) {
            Text(label.isEmpty ? "" : label + ":")
                .frame(width: 150, alignment: .trailing)
            VStack(alignment: .leading, spacing: 4) {
                control()
                if let hint {
                    Text(hint).font(.system(size: 11)).foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                        .frame(maxWidth: 360, alignment: .leading)
                }
            }
            Spacer(minLength: 0)
        }
    }
}

private struct Page<C: View>: View {
    @ViewBuilder let content: () -> C
    var body: some View { VStack(alignment: .leading, spacing: 12) { content() } }
}

struct GeneralSettingsView: View {
    @AppStorage(CrashReports.key) private var crashReports = false
    @ObservedObject private var prefs = Prefs.shared

    var body: some View {
        Page {
            Row("Interface", hint: prefs.interface.summary) {
                Picker("", selection: $prefs.interface) {
                    ForEach(Interface.allCases) { Text($0.name).tag($0) }
                }
                .labelsHidden().fixedSize()
            }
            Row("Your name", hint: "Printed under your circuit when you export it as an image.") {
                TextField("First and last name", text: $prefs.studentName).frame(width: 240)
            }
            Row("Opening a circuit", hint: prefs.openReplaces
                ? "New and opened circuits take the place of the one you're in, like classic CedarLogic. If it has unsaved changes, you're asked first."
                : "New and opened circuits get a window of their own, so you can have several open side by side.") {
                Picker("", selection: $prefs.openReplaces) {
                    Text("Replace the current one").tag(true)
                    Text("Open in a new window").tag(false)
                }
                .labelsHidden().fixedSize()
            }
            Row("Status bar", hint: "The readout in the bottom-right corner of the window.") {
                Toggle("Show zoom, cursor position, and counts", isOn: $prefs.showStatus)
            }
            Row("Testing group", hint: "Beta testers get new versions first, before they're ready for everyone. Normal testers get them once they're settled. Updates install from inside the app.") {
                HStack {
                    Picker("", selection: $prefs.testingGroup) {
                        ForEach(TestingGroup.allCases) { Text($0.name).tag($0) }
                    }
                    .labelsHidden().fixedSize()
                    Button("Check Now") { Updates.shared.checkNow() }
                }
            }
            if CrashReports.available {
                Row("Crash reports", hint: "If CedarLogic crashes, a report of where it went wrong goes to its developer. Never your circuits, files or name.") {
                    Toggle("Send crash reports", isOn: $crashReports)
                        .onChange(of: crashReports) { _, on in CrashReports.set(on) }
                }
            }
        }
    }
}

struct CLAppearanceSettings: View {
    @ObservedObject private var prefs = Prefs.shared

    var body: some View {
        Page {
            Row("Theme at launch", hint: "Which theme the app opens in. The View menu and the dark mode shortcut switch it any time.") {
                Picker("", selection: $prefs.themeMode) {
                    Text("Match System").tag(0); Text("Light").tag(1); Text("Dark").tag(2); Text("Same as Last Time").tag(3)
                }
                .labelsHidden().fixedSize()
            }
            Row("Accent color", hint: "Used for selections and highlights. Wire colors that show signal state never change.") {
                Picker("", selection: Binding(get: { prefs.accent }, set: { i in withAnimation(.easeOut(duration: 0.15)) { prefs.accent = i } })) {
                    ForEach(0..<6) { i in
                        Label { Text(accentNames[i]) } icon: { Image(nsImage: Self.swatchImage(swatch(i))) }.tag(i)
                    }
                }
                .labelsHidden().fixedSize()
            }
            Row("Tabs", hint: "Modern tabs drag to reorder or to split the view, and rename with a double-click. Classic are plain segments.") {
                Picker("", selection: $prefs.classicTabs) { Text("Modern").tag(false); Text("Classic").tag(true) }
                    .labelsHidden().fixedSize()
            }
            Row("Canvas", hint: "The background grid gates snap to. Printing never includes it.") {
                Toggle("Show the grid", isOn: $prefs.showGrid)
            }
            Row("Grid style", hint: "Dots are quieter; lines make alignment easier to see.") {
                Picker("", selection: $prefs.gridStyle) { Text("Lines").tag(0); Text("Dots").tag(1) }
                    .labelsHidden().fixedSize().disabled(!prefs.showGrid)
            }
            Row("", hint: "Makes distances easy to judge at a glance.") {
                Toggle("Darker line every 5 squares", isOn: $prefs.majorGrid).disabled(!prefs.showGrid)
            }
            Row("Wire thickness", hint: "On screen only. Printouts always use the standard weight.") {
                Picker("", selection: $prefs.wireThickness) { Text("Thin").tag(0); Text("Normal").tag(1); Text("Thick").tag(2) }
                    .labelsHidden().fixedSize()
            }
            Row("Low wires", hint: "The colour of wires carrying a 0 on the dark background, so they stand apart from the grid. Light mode keeps black.") {
                Picker("", selection: $prefs.lowWire) {
                    ForEach(LowWireColor.allCases) { c in Text(c.name).tag(c.rawValue) }
                }
                .labelsHidden().fixedSize()
            }
            Row("", hint: "Marks every corner of a wire. Junctions where wires join always get a dot.") {
                Toggle("Show dots at wire bends", isOn: $prefs.wireDotsAtBends)
            }
            Row("Wire dot size", hint: "Radius of the dots on wires, in grid units.") {
                HStack {
                    Slider(value: $prefs.wireDotSize, in: 0.08...0.4).frame(width: 160)
                    Text(String(format: "%.2f", prefs.wireDotSize)).monospacedDigit().foregroundStyle(.secondary)
                }
            }
            Row("Gate size", hint: "How big the gates in the side panel are.") {
                HStack {
                    Slider(value: Binding(get: { Double(prefs.gateSize) }, set: { prefs.gateSize = Int($0) }), in: 36...96, step: 1)
                        .frame(width: 160)
                    Text("\(prefs.gateSize)").monospacedDigit().foregroundStyle(.secondary)
                }
            }
            Row("Side panel", hint: "The names under the gates, and ⇧1…⇧0 beside the categories that jump to them.") {
                VStack(alignment: .leading, spacing: 6) {
                    Toggle("Show gate names", isOn: $prefs.showGateNames)
                    Toggle("Show category shortcuts", isOn: $prefs.showCategoryKeys)
                }
            }
        }
    }

    /// A round swatch for a menu item.
    static func swatchImage(_ c: Color) -> NSImage {
        let img = NSImage(size: NSSize(width: 12, height: 12), flipped: false) { r in
            NSColor(c).setFill()
            NSBezierPath(ovalIn: r.insetBy(dx: 0.5, dy: 0.5)).fill()
            return true
        }
        img.isTemplate = false
        return img
    }

    private func swatch(_ i: Int) -> Color {
        var r = 0.0, g = 0.0, b = 0.0
        cl_accent_color(Int32(i), prefs.dark, &r, &g, &b)
        return Color(.sRGB, red: r, green: g, blue: b)
    }
}

struct CLCanvasSettings: View {
    @ObservedObject private var prefs = Prefs.shared
    @AppStorage("tidyMode") private var tidyMode = 0

    var body: some View {
        Page {
            Row("Mouse wheel") {
                Picker("", selection: $prefs.mouseWheel) { Text("Zooms").tag(0); Text("Moves around").tag(1) }
                    .labelsHidden().fixedSize()
            }
            Row("", hint: "Flip this if rolling the wheel up zooms out. Apps like Scroll Reverser change the direction.") {
                Toggle("Reverse zoom direction", isOn: $prefs.reverseWheel)
            }
            Row("Trackpad scroll", hint: "Pinching always zooms.") {
                Picker("", selection: $prefs.trackpadScroll) { Text("Zooms").tag(0); Text("Moves around").tag(1) }
                    .labelsHidden().fixedSize()
            }
            Row("", hint: "Only matters when trackpad scrolling is set to zoom. ⌘+scroll always zooms; ⇧+scroll always moves sideways.") {
                Toggle("Reverse zoom direction", isOn: $prefs.reverseTrackpad)
            }
            Row("Right-click", hint: "Off: right-clicking a gate opens a menu with Rotate, Delete and more.") {
                Toggle("Rotates the gate", isOn: $prefs.rightClickRotate)
            }
            Row("Duplicate", hint: "Second option: the copy stays on the clipboard, so paste makes more of it.") {
                Picker("", selection: $prefs.duplicateUsesClipboard) {
                    Text("Leaves the clipboard alone").tag(false)
                    Text("Copies to the clipboard too").tag(true)
                }
                .labelsHidden().fixedSize()
            }
            Row("Tidy Up", hint: "Keeps my layout: lines gates up where they are. Rearranges everything: lays the circuit out by signal flow. The Edit menu always has the other one.") {
                Picker("", selection: $tidyMode) { Text("Keeps my layout").tag(0); Text("Rearranges everything").tag(1) }
                    .labelsHidden().fixedSize()
            }
        }
    }
}

/// The wx Toolbar page: each style by name, with a line about it and a
/// picture of it (drawn by the toolbar itself, so it always matches; click
/// either to choose it), then which tools it shows.
struct CLToolbarSettings: View {
    @ObservedObject private var prefs = Prefs.shared
    private let groups: [ToolGroup] = [.file, .undo, .clipboard, .zoom, .sim, .run, .lock, .tab, .feedback]

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            ForEach(ToolbarStyle.allCases) { st in
                styleChoice(st)
            }
            Text("Show in the toolbar:").font(.system(size: 13, weight: .bold)).padding(.top, 14)
            LazyVGrid(columns: Array(repeating: GridItem(.fixed(170), spacing: 18, alignment: .leading), count: 3),
                      alignment: .leading, spacing: 6) {
                ForEach(groups) { g in
                    Toggle(g.name, isOn: Binding(get: { prefs.shown(g) }, set: { on in prefs.setShown(g, on) }))
                        .toggleStyle(.checkbox)
                }
                Toggle("Circuit name", isOn: $prefs.showTitle).toggleStyle(.checkbox)
            }
            .padding(.top, 10)
            Text("Applies to every style. Hidden tools are still in the menus and keep their shortcuts.")
                .font(.system(size: 11)).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
                .padding(.top, 10)
        }
    }

    private func styleChoice(_ st: ToolbarStyle) -> some View {
        let on = prefs.toolbarStyle == st
        return VStack(alignment: .leading, spacing: 4) {
            HStack(spacing: 6) {
                Image(systemName: on ? "largecircle.fill.circle" : "circle")
                    .foregroundStyle(on ? Color.accentColor : Color.secondary)
                Text(st.name).font(.system(size: 13, weight: .bold))
            }
            Text(st.blurb).font(.system(size: 11)).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
                .padding(.leading, 22)
            ToolbarPicture(style: st)
                .padding(.leading, 22).padding(.top, 2)
        }
        .padding(.vertical, 6)
        .contentShape(Rectangle())
        .onTapGesture { withAnimation(.easeInOut(duration: 0.2)) { prefs.toolbarStyle = st } }
    }
}

/// The toolbar in one style, small and not clickable: the front circuit's
/// own toolbar, so the picture matches what you'll get.
private struct ToolbarPicture: View {
    let style: ToolbarStyle
    @ObservedObject private var prefs = Prefs.shared

    var body: some View {
        let w: CGFloat = 520, h: CGFloat = 52, k: CGFloat = 0.82
        Group {
            if let c = CanvasController.front, let doc = c.document {
                CLToolbar(document: doc, canvas: c, status: c.status, title: "counter", subtitle: "Page 1",
                          focusMode: .constant(false), styleOverride: style)
                    .frame(width: w, height: h)
                    .background(CLChrome(dark: prefs.dark).canvas)
                    .scaleEffect(k, anchor: .topLeading)
                    .frame(width: w * k, height: h * k, alignment: .topLeading)
                    .allowsHitTesting(false)
            } else {
                Color.secondary.opacity(0.1).frame(width: w * k, height: h * k)
            }
        }
        .clipShape(RoundedRectangle(cornerRadius: 8))
        .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(Color.primary.opacity(0.12)))
    }
}

/// Every command's shortcut, changeable: click one, press the new keys.
/// Delete clears it, Escape keeps what was there.
struct CLShortcutSettings: View {
    @ObservedObject private var store = ShortcutStore.shared
    @State private var recording: ShortcutAction?
    @State private var monitor: Any?
    @State private var note = ""
    @State private var search = ""

    private var shown: [ShortcutAction] {
        let q = search.trimmingCharacters(in: .whitespaces).lowercased()
        return ShortcutAction.allCases.filter { q.isEmpty || $0.name.lowercased().contains(q) || $0.section.lowercased().contains(q) }
    }

    var body: some View {
        let actions = shown
        let sections = actions.reduce(into: [String]()) { if !$0.contains($1.section) { $0.append($1.section) } }
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                TextField("Search shortcuts", text: $search).textFieldStyle(.roundedBorder).frame(width: 220)
                Spacer()
                Button("Restore Defaults") { withAnimation { store.resetAll() }; note = "" }
                    .disabled(store.overrides.isEmpty)
            }
            Text(note.isEmpty ? "Click a shortcut, then press the new keys. Keys without ⌘ or ⌃ work while the canvas has the keyboard. Delete removes one." : note)
                .font(.system(size: 11)).foregroundStyle(note.isEmpty ? Color.secondary : Color.orange)
                .fixedSize(horizontal: false, vertical: true)
            ScrollView {
                VStack(alignment: .leading, spacing: 14) {
                    ForEach(sections, id: \.self) { sec in
                        VStack(alignment: .leading, spacing: 4) {
                            Text(sec.uppercased()).font(.system(size: 10.5, weight: .bold)).foregroundStyle(Color.accentColor)
                            ForEach(actions.filter { $0.section == sec }) { a in row(a) }
                        }
                    }
                    VStack(alignment: .leading, spacing: 4) {
                        Text("FIXED").font(.system(size: 10.5, weight: .bold)).foregroundStyle(Color.accentColor)
                        fixed(["Space"], "Tap: zoom to fit. Hold and drag: move around. In Simulation View: pause")
                        fixed(["Esc"], "Cancel a drag, paste or connection; leave Simulation View")
                        fixed(["⌫"], "Delete the selection")
                        fixed(["↑", "↓", "←", "→"], "Nudge the selection, or move around")
                        fixed(["⇧", "1…0"], "Jump to a gate category")
                    }
                }
                .padding(.vertical, 4)
            }
            .frame(height: 380)
        }
        .onDisappear { stop() }
    }

    private func row(_ a: ShortcutAction) -> some View {
        HStack {
            Text(a.name).font(.system(size: 12.5))
            Spacer()
            if store.isCustom(a) {
                Button { withAnimation { store.reset(a) } } label: { Image(systemName: "arrow.uturn.backward").font(.system(size: 10)) }
                    .buttonStyle(.borderless).help("Back to \(a.defaultCombo?.label ?? "none")")
            }
            Button { recording == a ? stop() : start(a) } label: {
                Group {
                    if recording == a {
                        Text("Press keys…").font(.system(size: 11.5)).foregroundStyle(Color.accentColor)
                    } else if let c = store.combo(a) {
                        HStack(spacing: 3) { ForEach(Array(c.caps.enumerated()), id: \.offset) { KeyCap(label: $0.element) } }
                    } else {
                        Text("None").font(.system(size: 11.5)).foregroundStyle(.secondary)
                    }
                }
                .frame(minWidth: 110, alignment: .trailing)
                .padding(.horizontal, 6).padding(.vertical, 3)
                .background(RoundedRectangle(cornerRadius: 6).strokeBorder(recording == a ? Color.accentColor : Color.clear, lineWidth: 1.5))
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
        }
        .padding(.vertical, 2)
    }

    private func fixed(_ keys: [String], _ what: String) -> some View {
        HStack {
            Text(what).font(.system(size: 12.5)).foregroundStyle(.secondary)
            Spacer()
            HStack(spacing: 3) { ForEach(keys, id: \.self) { KeyCap(label: $0) } }
        }
        .padding(.vertical, 2)
    }

    private func start(_ a: ShortcutAction) {
        stop()
        recording = a
        note = ""
        monitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { e in
            guard let a = recording else { return e }
            if e.keyCode == 53 { stop(); return nil }                       // Escape: keep it
            if e.keyCode == 51 || e.keyCode == 117 { store.set(a, nil); stop(); return nil }   // Delete: none
            guard let c = KeyCombo(event: e), !c.key.isEmpty else { return nil }
            if let other = store.owner(of: c, except: a) {
                note = "\(c.label) was \(other.name.lowercased()); that one has no shortcut now."
            }
            withAnimation { store.set(a, c) }
            stop()
            return nil
        }
    }

    private func stop() {
        recording = nil
        if let monitor { NSEvent.removeMonitor(monitor) }
        monitor = nil
    }
}

struct LookSettingsView: View {
    @EnvironmentObject private var look: LookStore

    var body: some View {
        Form {
            Section {
                HStack(spacing: 14) {
                    ForEach(ThemePreset.allCases) { preset in
                        PresetCard(preset: preset, selected: look.settings.preset == preset) {
                            look.settings.choose(preset)
                        }
                    }
                }
                .padding(.vertical, 4)
                Text(look.settings.preset.summary)
                    .font(.callout)
                    .foregroundStyle(.secondary)
            } header: {
                Text("Preset")
            }

            Section {
                ColorRow(title: "Canvas", value: look.settings.theme.canvas, isSet: look.settings.canvas != nil,
                         set: { look.settings.canvas = $0 }, reset: { look.settings.canvas = nil })
                Picker("Grid", selection: Binding(
                    get: { look.settings.theme.gridStyle },
                    set: { look.settings.gridStyle = $0 })) {
                    ForEach(GridStyle.allCases) { Text($0.label).tag($0) }
                }
                ColorRow(title: "Grid", value: look.settings.theme.gridMinor, isSet: look.settings.gridMinor != nil,
                         set: { look.settings.gridMinor = $0 }, reset: { look.settings.gridMinor = nil })
                ColorRow(title: "Grid, every fifth line", value: look.settings.theme.gridMajor,
                         isSet: look.settings.gridMajor != nil,
                         set: { look.settings.gridMajor = $0 }, reset: { look.settings.gridMajor = nil })
                Toggle("Light ink for dark canvases", isOn: Binding(
                    get: { look.settings.theme.darkCircuit },
                    set: { look.settings.darkCircuit = $0 }))
                ColorRow(title: "Accent", value: look.settings.theme.accent, isSet: look.settings.accent != nil,
                         set: { look.settings.accent = $0 }, reset: { look.settings.accent = nil })
            } header: {
                Text("Customize")
            } footer: {
                HStack {
                    Spacer()
                    Button("Reset to \(look.settings.preset.name)") { look.settings.choose(look.settings.preset) }
                        .disabled(!look.settings.isCustomized)
                }
            }
        }
        .formStyle(.columns)
    }
}

/// A color setting, with a small reset button once it's been changed.
private struct ColorRow: View {
    let title: String
    let value: RGBA
    let isSet: Bool
    let set: (RGBA) -> Void
    let reset: () -> Void

    var body: some View {
        HStack {
            ColorPicker(title, selection: Binding(get: { value.color }, set: { set(RGBA($0)) }), supportsOpacity: false)
            if isSet {
                Button(action: reset) { Image(systemName: "arrow.uturn.backward") }
                    .buttonStyle(.borderless)
                    .help("Back to the preset's color")
            }
        }
    }
}

/// A preset as a miniature canvas: its background, grid and a tiny gate.
private struct PresetCard: View {
    let preset: ThemePreset
    let selected: Bool
    let action: () -> Void

    var body: some View {
        let t = preset.theme
        Button(action: action) {
            VStack(spacing: 6) {
                Canvas { ctx, size in
                    ctx.fill(Path(CGRect(origin: .zero, size: size)), with: .color(t.canvas.color))
                    let step: CGFloat = 8
                    for x in stride(from: step, to: size.width, by: step) {
                        for y in stride(from: step, to: size.height, by: step) {
                            if t.gridStyle == .dots {
                                ctx.fill(Path(ellipseIn: CGRect(x: x - 0.7, y: y - 0.7, width: 1.4, height: 1.4)),
                                         with: .color(t.gridMajor.color))
                            }
                        }
                    }
                    if t.gridStyle == .lines {
                        var grid = Path()
                        for x in stride(from: step, to: size.width, by: step) { grid.move(to: CGPoint(x: x, y: 0)); grid.addLine(to: CGPoint(x: x, y: size.height)) }
                        for y in stride(from: step, to: size.height, by: step) { grid.move(to: CGPoint(x: 0, y: y)); grid.addLine(to: CGPoint(x: size.width, y: y)) }
                        ctx.stroke(grid, with: .color(t.gridMinor.color), lineWidth: 0.5)
                    }
                    // An AND gate, in the preset's ink.
                    let ink: Color = t.darkCircuit ? .white : .black
                    var gate = Path()
                    let r = CGRect(x: size.width / 2 - 14, y: size.height / 2 - 11, width: 28, height: 22)
                    gate.move(to: CGPoint(x: r.minX, y: r.minY))
                    gate.addLine(to: CGPoint(x: r.midX, y: r.minY))
                    gate.addArc(center: CGPoint(x: r.midX, y: r.midY), radius: r.height / 2,
                                startAngle: .degrees(-90), endAngle: .degrees(90), clockwise: false)
                    gate.addLine(to: CGPoint(x: r.minX, y: r.maxY))
                    gate.closeSubpath()
                    ctx.stroke(gate, with: .color(ink), lineWidth: 1.2)
                    var wires = Path()
                    wires.move(to: CGPoint(x: r.minX - 12, y: r.minY + 6)); wires.addLine(to: CGPoint(x: r.minX, y: r.minY + 6))
                    wires.move(to: CGPoint(x: r.minX - 12, y: r.maxY - 6)); wires.addLine(to: CGPoint(x: r.minX, y: r.maxY - 6))
                    wires.move(to: CGPoint(x: r.midX + r.height / 2, y: r.midY)); wires.addLine(to: CGPoint(x: r.maxX + 14, y: r.midY))
                    ctx.stroke(wires, with: .color(Color(red: 0, green: 0.7, blue: 0)), lineWidth: 1.2)
                }
                .frame(width: 130, height: 76)
                .clipShape(RoundedRectangle(cornerRadius: 8))
                .overlay(RoundedRectangle(cornerRadius: 8)
                    .strokeBorder(selected ? Color.accentColor : Color.secondary.opacity(0.3), lineWidth: selected ? 2.5 : 1))
                Text(preset.name).font(.callout.weight(selected ? .semibold : .regular))
            }
        }
        .buttonStyle(.plain)
    }
}

/// Which window arrangement to use; either works with any look.
struct LayoutSettingsView: View {
    @AppStorage("layout") private var layout = AppLayout.native.rawValue
    @AppStorage("tidyMode") private var tidyMode = 0

    var body: some View {
        Form {
            Picker("Layout", selection: $layout) {
                ForEach(AppLayout.allCases) { Text($0.name).tag($0.rawValue) }
            }
            .pickerStyle(.radioGroup)
            Text(AppLayout(rawValue: layout)?.summary ?? "")
                .font(.callout)
                .foregroundStyle(.secondary)
            Section("Tidy Up (Shift-S)") {
                Picker("Shift-S", selection: $tidyMode) {
                    Text("Keeps my layout").tag(0)
                    Text("Rearranges everything").tag(1)
                }
                Text("Keeps my layout lines parts up where they are. Rearranges everything lays the circuit out by signal flow. The Edit menu always has both.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }
        }
        .formStyle(.columns)
    }
}
