// Preferences: the wx app's settings, kept in
// UserDefaults. On first launch they're read from the wx app's own
// preferences file, so the look you chose there carries over.

import AppKit
import SwiftUI

/// The wx app's toolbar styles (Segmented was dropped: it was Classic again).
enum ToolbarStyle: Int, CaseIterable, Identifiable {
    case classic = 0, minimal = 2, seamless = 3
    var id: Int { rawValue }
    var name: String {
        switch self {
        case .classic: "Classic"
        case .minimal: "Minimal"
        case .seamless: "Seamless"
        }
    }
    var blurb: String {
        switch self {
        case .classic: "Tools in tidy rounded groups, everything in reach."
        case .minimal: "Just the essentials and your file name; the rest is behind the ••• menu."
        case .seamless: "Blends into the canvas like one surface. Tools stay quiet until you point at them."
        }
    }
}

/// Tool groups that can be hidden, one bit each (the wx app's cl::tb::Group).
enum ToolGroup: Int, CaseIterable, Identifiable {
    case file, undo, clipboard, zoom, sim, run, lock, theme, tab, feedback
    var id: Int { rawValue }
    var name: String {
        ["New, Open, Save", "Undo and Redo", "Copy and Paste", "Zoom", "Pause, Step, Step Clock, Speed",
         "Run (Simulation View)", "Lock", "Dark mode", "New tab", "Send feedback"][rawValue]
    }
}

let accentNames = ["Blue", "Purple", "Pink", "Orange", "Green", "Graphite", "CedarLogic"]
/// The accent choices in the order they're offered: the icon's green first.
let accentOrder = [6, 0, 1, 2, 3, 4, 5]
/// The icon's green: the accent a new install starts with.
let brandAccent = 6

@MainActor
final class Prefs: ObservableObject {
    static let shared = Prefs()
    private let d = UserDefaults.standard

    @Published var testingGroup: TestingGroup {
        didSet {
            guard testingGroup != oldValue else { return }
            d.set(testingGroup.rawValue, forKey: "testingGroup")
            Updates.shared.groupChanged()
        }
    }
    /// 0 follow the system, 1 light, 2 dark, 3 same as last time.
    @Published var themeMode: Int { didSet { d.set(themeMode, forKey: "cl.themeMode"); applyThemeMode() } }
    /// Dark right now (the toolbar switch and the shortcut flip it).
    @Published var dark: Bool { didSet { d.set(dark, forKey: "cl.lastDark") } }
    @Published var accent: Int { didSet { d.set(accent, forKey: "cl.accent") } }
    @Published var showGrid: Bool { didSet { d.set(showGrid, forKey: "cl.showGrid") } }
    @Published var gridStyle: Int { didSet { d.set(gridStyle, forKey: "cl.gridStyle") } }   // 0 lines, 1 dots
    @Published var majorGrid: Bool { didSet { d.set(majorGrid, forKey: "cl.majorGrid") } }
    @Published var wireThickness: Int { didSet { d.set(wireThickness, forKey: "cl.wireThickness") } }
    @Published var wireDotsAtBends: Bool { didSet { d.set(wireDotsAtBends, forKey: "cl.wireDots"); applyWireDots() } }
    @Published var wireDotSize: Double { didSet { d.set(wireDotSize, forKey: "cl.wireDotSize"); applyWireDots() } }
    /// The colour of low (0) wires on the dark canvas (LowWireColor).
    @Published var lowWire: Int { didSet { d.set(lowWire, forKey: "cl.lowWire"); applyWireDots() } }
    @Published var gateSize: Int { didSet { d.set(gateSize, forKey: "cl.gateSize") } }
    @Published var showStatus: Bool { didSet { d.set(showStatus, forKey: "cl.showStatus") } }
    @Published var toolbarStyle: ToolbarStyle { didSet { d.set(toolbarStyle.rawValue, forKey: "cl.toolbarStyle") } }
    @Published var toolbarHidden: Int { didSet { d.set(toolbarHidden, forKey: "cl.toolbarHidden") } }
    @Published var classicTabs: Bool { didSet { d.set(classicTabs, forKey: "cl.classicTabs") } }
    /// What a plain scroll does: 0 zooms, 1 moves around.
    @Published var mouseWheel: Int { didSet { d.set(mouseWheel, forKey: "cl.mouseWheel") } }
    @Published var trackpadScroll: Int { didSet { d.set(trackpadScroll, forKey: "cl.trackpadScroll") } }
    @Published var reverseWheel: Bool { didSet { d.set(reverseWheel, forKey: "cl.reverseWheel") } }
    @Published var reverseTrackpad: Bool { didSet { d.set(reverseTrackpad, forKey: "cl.reverseTrackpad") } }
    @Published var rightClickRotate: Bool { didSet { d.set(rightClickRotate, forKey: "cl.rightClickRotate") } }
    @Published var duplicateUsesClipboard: Bool { didSet { d.set(duplicateUsesClipboard, forKey: "cl.duplicateClipboard") } }
    @Published var themeShortcutOn: Bool { didSet { d.set(themeShortcutOn, forKey: "cl.themeShortcutOn") } }
    /// The dark mode shortcut: a key and ThemeShortcutMod bits (1 shift, 2 option, 4 control, 8 command).
    @Published var themeKey: String { didSet { d.set(themeKey, forKey: "cl.themeKey") } }
    @Published var themeMods: Int { didSet { d.set(themeMods, forKey: "cl.themeMods") } }
    @Published var showThemeToggle: Bool { didSet { d.set(showThemeToggle, forKey: "cl.showThemeToggle") } }
    @Published var studentName: String { didSet { d.set(studentName, forKey: "cl.studentName") } }
    @Published var exportInfo: Bool { didSet { d.set(exportInfo, forKey: "cl.exportInfo") } }
    /// Timing diagrams in colour (green traces) rather than black and white.
    @Published var timingInColor: Bool { didSet { d.set(timingInColor, forKey: "cl.timingColor") } }
    /// Resting on a wire shows what it carries (0, 1, Z...). Off at first.
    @Published var wireValueTag: Bool { didSet { d.set(wireValueTag, forKey: "cl.wireValueTag") } }
    /// Cmd-Q asks first, in a panel in the middle of the screen.
    @Published var confirmQuit: Bool { didSet { d.set(confirmQuit, forKey: "cl.confirmQuit") } }
    @Published var hasSeenWelcome: Bool { didSet { d.set(hasSeenWelcome, forKey: "cl.hasSeenWelcome") } }
    /// Names under the palette's gates, and ⇧1...⇧0 beside its categories.
    @Published var showGateNames: Bool { didSet { d.set(showGateNames, forKey: "cl.showGateNames") } }
    @Published var showCategoryKeys: Bool { didSet { d.set(showCategoryKeys, forKey: "cl.showCategoryKeys") } }
    /// The circuit's name (and its Rename menu) at the toolbar's top left.
    @Published var showTitle: Bool { didSet { d.set(showTitle, forKey: "cl.showTitle") } }
    /// Opening or starting a circuit replaces the one in the window, as in
    /// the wx app (asking to save first); off, each gets its own window.
    @Published var openReplaces: Bool { didSet { d.set(openReplaces, forKey: "cl.openReplaces") } }
    /// What a new circuit starts as: a template's id, or "" for a blank page.
    @Published var newTemplate: String { didSet { d.set(newTemplate, forKey: "cl.newTemplate") } }

    private init() {
        Prefs.importWxPrefsOnce()
        let d = UserDefaults.standard
        func int(_ k: String, _ v: Int) -> Int { d.object(forKey: k) == nil ? v : d.integer(forKey: k) }
        func bool(_ k: String, _ v: Bool) -> Bool { d.object(forKey: k) == nil ? v : d.bool(forKey: k) }
        testingGroup = TestingGroup(rawValue: d.string(forKey: "testingGroup") ?? "") ?? .normal
        themeMode = int("cl.themeMode", 0)
        dark = bool("cl.lastDark", false)
        // The icon's green became the default (and everyone's accent, once).
        if !d.bool(forKey: "cl.brandAccentSet") {
            d.set(true, forKey: "cl.brandAccentSet")
            d.set(brandAccent, forKey: "cl.accent")
        }
        accent = int("cl.accent", brandAccent)
        showGrid = bool("cl.showGrid", true)
        gridStyle = int("cl.gridStyle", 0)
        majorGrid = bool("cl.majorGrid", true)
        wireThickness = int("cl.wireThickness", 1)
        wireDotsAtBends = bool("cl.wireDots", true)
        wireDotSize = d.object(forKey: "cl.wireDotSize") == nil ? 0.18 : d.double(forKey: "cl.wireDotSize")
        lowWire = d.integer(forKey: "cl.lowWire")
        gateSize = int("cl.gateSize", 48)
        showStatus = bool("cl.showStatus", true)
        toolbarStyle = ToolbarStyle(rawValue: int("cl.toolbarStyle", 3)) ?? .classic
        toolbarHidden = int("cl.toolbarHidden", 0)
        classicTabs = bool("cl.classicTabs", false)
        mouseWheel = int("cl.mouseWheel", 0)
        trackpadScroll = int("cl.trackpadScroll", 1)
        reverseWheel = bool("cl.reverseWheel", false)
        reverseTrackpad = bool("cl.reverseTrackpad", false)
        rightClickRotate = bool("cl.rightClickRotate", false)
        duplicateUsesClipboard = bool("cl.duplicateClipboard", false)
        themeShortcutOn = bool("cl.themeShortcutOn", true)
        themeKey = d.string(forKey: "cl.themeKey") ?? "d"
        themeMods = int("cl.themeMods", 9)
        showThemeToggle = bool("cl.showThemeToggle", true)
        studentName = d.string(forKey: "cl.studentName") ?? ""
        exportInfo = bool("cl.exportInfo", true)
        timingInColor = bool("cl.timingColor", false)
        wireValueTag = bool("cl.wireValueTag", false)
        confirmQuit = bool("cl.confirmQuit", true)
        hasSeenWelcome = bool("cl.hasSeenWelcome", false)
        showGateNames = bool("cl.showGateNames", true)
        showCategoryKeys = bool("cl.showCategoryKeys", true)
        showTitle = bool("cl.showTitle", true)
        openReplaces = bool("cl.openReplaces", true)
        newTemplate = d.string(forKey: "cl.newTemplate") ?? ""
        applyThemeMode()
        applyWireDots()
    }


    // MARK: Derived

    func shown(_ g: ToolGroup) -> Bool { toolbarHidden & (1 << g.rawValue) == 0 }
    func setShown(_ g: ToolGroup, _ on: Bool) {
        if on { toolbarHidden &= ~(1 << g.rawValue) } else { toolbarHidden |= 1 << g.rawValue }
    }

    var wireScale: Double { [0.7, 1.0, 1.6][min(max(wireThickness, 0), 2)] }

    func accentColor(dark: Bool) -> Color {
        var r = 0.0, g = 0.0, b = 0.0
        cl_accent_color(Int32(accent), dark, &r, &g, &b)
        return Color(.sRGB, red: r, green: g, blue: b)
    }
    /// Text and symbols on an accent-filled button: white, or the icon's
    /// dark ink on a light accent (the green, on the dark theme) where white
    /// wouldn't read.
    func onAccentColor(dark: Bool) -> Color {
        let (r, g, b) = accentRGB(dark: dark)
        let lum = 0.2126 * r + 0.7152 * g + 0.0722 * b
        return lum > 0.55 ? Brand.ink : .white
    }
    func accentRGB(dark: Bool) -> (Double, Double, Double) {
        var r = 0.0, g = 0.0, b = 0.0
        cl_accent_color(Int32(accent), dark, &r, &g, &b)
        return (r, g, b)
    }

    /// The theme at launch; after that the toolbar switch, the View menu and
    /// the shortcut change `dark`.
    private func applyThemeMode() {
        switch themeMode {
        case 1: dark = false
        case 2: dark = true
        case 3: break   // keep the last one
        default: dark = NSApp?.effectiveAppearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua
        }
    }

    /// Follow the system while "Match System" is chosen.
    func systemAppearanceChanged() { if themeMode == 0 { applyThemeMode() } }

    func applyWireDots() {
        cl_set_wire_dots(wireDotsAtBends, wireDotSize)
        let c = LowWireColor(rawValue: lowWire) ?? .silver
        if let rgb = c.rgb { cl_set_low_wire_color(true, rgb.0, rgb.1, rgb.2) } else { cl_set_low_wire_color(false, 0, 0, 0) }
    }

    var themeShortcutLabel: String {
        var s = ""
        if themeMods & 4 != 0 { s += "⌃" }
        if themeMods & 2 != 0 { s += "⌥" }
        if themeMods & 1 != 0 { s += "⇧" }
        if themeMods & 8 != 0 { s += "⌘" }
        return s + themeKey.uppercased()
    }

    func matchesThemeShortcut(_ event: NSEvent) -> Bool {
        guard themeShortcutOn, event.charactersIgnoringModifiers?.lowercased() == themeKey.lowercased() else { return false }
        let f = event.modifierFlags
        var m = 0
        if f.contains(.shift) { m |= 1 }
        if f.contains(.option) { m |= 2 }
        if f.contains(.control) { m |= 4 }
        if f.contains(.command) { m |= 8 }
        return m == themeMods && m != 0
    }

    // MARK: Carrying over the wx app's settings

    /// Reads ~/Library/Preferences/CedarLogic Preferences (an INI file wxConfig
    /// writes) into UserDefaults, once, for anything not set here yet.
    private static func importWxPrefsOnce() {
        let d = UserDefaults.standard
        guard !d.bool(forKey: "cl.importedWx") else { return }
        d.set(true, forKey: "cl.importedWx")
        let url = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Preferences/CedarLogic Preferences")
        guard let text = try? String(contentsOf: url, encoding: .utf8) else { return }
        var values: [String: String] = [:]
        for line in text.split(separator: "\n") {
            if line.hasPrefix("[") { break }   // only the top-level keys
            guard let eq = line.firstIndex(of: "=") else { continue }
            values[String(line[..<eq])] = String(line[line.index(after: eq)...])
        }
        func int(_ wx: String, _ key: String) { if let v = values[wx], let n = Int(v) { d.set(n, forKey: key) } }
        func bool(_ wx: String, _ key: String) { if let v = values[wx] { d.set(v == "1", forKey: key) } }
        int("ThemeMode", "cl.themeMode")
        bool("ThemeLastDark", "cl.lastDark")
        int("AccentColor", "cl.accent")
        bool("GridlineVisible", "cl.showGrid")
        int("GridStyle", "cl.gridStyle")
        bool("MajorGridVisible", "cl.majorGrid")
        int("WireThickness", "cl.wireThickness")
        bool("WireConnVisible", "cl.wireDots")
        if let v = values["WireConnRadius"], let r = Double(v) { d.set(r, forKey: "cl.wireDotSize") }
        int("PaletteGateSize", "cl.gateSize")
        bool("ShowStatusInfo", "cl.showStatus")
        int("ToolbarStyle", "cl.toolbarStyle")
        int("ToolbarHidden", "cl.toolbarHidden")
        bool("ClassicTabs", "cl.classicTabs")
        int("MouseWheelAction", "cl.mouseWheel")
        int("TrackpadScrollAction", "cl.trackpadScroll")
        bool("ReverseWheelZoom", "cl.reverseWheel")
        bool("ReverseTrackpadZoom", "cl.reverseTrackpad")
        bool("RightClickRotate", "cl.rightClickRotate")
        bool("DuplicateUsesClipboard", "cl.duplicateClipboard")
        bool("ThemeShortcutEnabled", "cl.themeShortcutOn")
        int("ThemeShortcutModifiers", "cl.themeMods")
        if let v = values["ThemeShortcutKeyCode"], let n = Int(v), let u = UnicodeScalar(n) {
            d.set(String(Character(u)).lowercased(), forKey: "cl.themeKey")
        }
        bool("ThemeToggleButtonVisible", "cl.showThemeToggle")
        if let v = values["StudentName"] { d.set(v, forKey: "cl.studentName") }
        bool("ExportInfoEnabled", "cl.exportInfo")
        bool("HasSeenWelcome", "cl.hasSeenWelcome")
        int("TidyMode", "tidyMode")
        if let v = values["TimeStep"], let n = Int(v) { d.set(n, forKey: "cl.stepMs") }
    }
}

/// The CedarLogic interface's colours, from the engine's RenderStyle so the
/// window around the canvas matches what the engine draws.
struct CLPalette {
    let dark: Bool
    let simView: Bool

    var canvas: Color {
        if simView { return Color(.sRGB, red: 0.030, green: 0.038, blue: 0.050) }
        return dark ? Color(.sRGB, red: 0.075, green: 0.082, blue: 0.098) : .white
    }
    var canvasCG: CGColor {
        if simView { return CGColor(srgbRed: 0.030, green: 0.038, blue: 0.050, alpha: 1) }
        return dark ? CGColor(srgbRed: 0.075, green: 0.082, blue: 0.098, alpha: 1) : CGColor(gray: 1, alpha: 1)
    }
    /// RenderStyle::gridColor.
    func grid(_ intensity: Double) -> CGColor {
        if simView { return CGColor(srgbRed: 0.25, green: 0.80, blue: 1.0, alpha: intensity * 0.55) }
        return dark ? CGColor(srgbRed: 1, green: 1, blue: 1, alpha: intensity)
                    : CGColor(srgbRed: 0, green: 0, blue: intensity, alpha: intensity)
    }
    var bar: Color { dark ? Color(.sRGB, red: 28 / 255, green: 31 / 255, blue: 37 / 255) : Color(.sRGB, red: 246 / 255, green: 247 / 255, blue: 249 / 255) }
    var classicBar: Color { dark ? Color(.sRGB, red: 44 / 255, green: 47 / 255, blue: 54 / 255) : Color(.sRGB, red: 236 / 255, green: 237 / 255, blue: 240 / 255) }
    var panel: Color { dark ? Color(.sRGB, red: 0.105, green: 0.115, blue: 0.135) : Color(.sRGB, red: 0.965, green: 0.968, blue: 0.975) }
    var ink: Color { dark ? Color(.sRGB, red: 210 / 255, green: 215 / 255, blue: 224 / 255) : Color(.sRGB, red: 52 / 255, green: 56 / 255, blue: 64 / 255) }
    var hairline: Color { dark ? Color.white.opacity(0.08) : Color.black.opacity(0.09) }
}

/// Low wires on the dark canvas. The engine's grey sat too close to the
/// grid's darker lines to pick out at a glance.
enum LowWireColor: Int, CaseIterable, Identifiable {
    case silver, slate, white, classic
    var id: Int { rawValue }
    var name: String {
        switch self {
        case .silver: "Silver"
        case .slate: "Slate blue"
        case .white: "Soft white"
        case .classic: "Classic grey"
        }
    }
    var rgb: (Double, Double, Double)? {
        switch self {
        case .silver: (0.62, 0.67, 0.76)
        case .slate: (0.47, 0.58, 0.84)
        case .white: (0.86, 0.87, 0.90)
        case .classic: nil
        }
    }
}
