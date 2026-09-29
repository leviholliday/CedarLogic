// Every command's keyboard shortcut, changeable in Settings > Shortcuts.
// Combos with ⌘ or ⌃ go on the menus; bare keys (A, T, ⇧S...) are the
// canvas's own, answered while it has the keyboard, as in the wx app.

import AppKit
import SwiftUI

struct KeyCombo: Codable, Hashable {
    var key: String      // one character, lower case ("a", "=", "."), or "tab"
    var mods: Int        // 1 shift, 2 option, 4 control, 8 command

    static let shift = 1, option = 2, control = 4, command = 8

    var label: String {
        var s = ""
        if mods & Self.control != 0 { s += "⌃" }
        if mods & Self.option != 0 { s += "⌥" }
        if mods & Self.shift != 0 { s += "⇧" }
        if mods & Self.command != 0 { s += "⌘" }
        return s + keyLabel
    }
    var keyLabel: String {
        switch key {
        case "tab": "⇥"
        case "-": "−"
        case "right": "→"
        default: key.uppercased()
        }
    }
    /// The pieces, for drawing as keycaps.
    var caps: [String] {
        var out: [String] = []
        if mods & Self.control != 0 { out.append("⌃") }
        if mods & Self.option != 0 { out.append("⌥") }
        if mods & Self.shift != 0 { out.append("⇧") }
        if mods & Self.command != 0 { out.append("⌘") }
        out.append(keyLabel)
        return out
    }
    /// Goes on a menu (it has ⌘ or ⌃); otherwise the canvas answers it.
    var isMenuShortcut: Bool { mods & (Self.command | Self.control) != 0 }

    var swiftUI: KeyboardShortcut? {
        guard isMenuShortcut else { return nil }
        let k: KeyEquivalent = key == "tab" ? .tab : key == "right" ? .rightArrow : KeyEquivalent(Character(key))
        var m: EventModifiers = []
        if mods & Self.shift != 0 { m.insert(.shift) }
        if mods & Self.option != 0 { m.insert(.option) }
        if mods & Self.control != 0 { m.insert(.control) }
        if mods & Self.command != 0 { m.insert(.command) }
        return KeyboardShortcut(k, modifiers: m)
    }

    /// The combo an event is, or nil for a lone modifier.
    init?(event e: NSEvent) {
        var m = 0
        let f = e.modifierFlags
        if f.contains(.shift) { m |= Self.shift }
        if f.contains(.option) { m |= Self.option }
        if f.contains(.control) { m |= Self.control }
        if f.contains(.command) { m |= Self.command }
        if e.keyCode == 48 { self.init(key: "tab", mods: m); return }
        if e.keyCode == 124 { self.init(key: "right", mods: m); return }
        // The unshifted key: ⇧1 is "1" with shift, not "!".
        guard let k = e.charactersIgnoringModifiers?.lowercased(), let c = k.first else { return nil }
        let shiftedDigits: [Character: String] = ["!": "1", "@": "2", "#": "3", "$": "4", "%": "5", "^": "6", "&": "7", "*": "8", "(": "9", ")": "0", "?": "/", "+": "=", "_": "-", ">": ".", "<": ","]
        self.init(key: shiftedDigits[c] ?? String(c), mods: m)
    }
    init(key: String, mods: Int) { self.key = key; self.mods = mods }
}

enum ShortcutAction: String, CaseIterable, Identifiable, Codable {
    // Circuits
    case newCircuit, openLibrary, importFile, save, exportImage, exportFile, print
    // Editing
    case undo, redo, cut, copy, paste, duplicate, selectAll
    // Building
    case addGate, rotate, straighten, tidy, quickCopy, quickPaste, quickCut, quickDuplicate
    // Moving around
    case zoomIn, zoomOut, zoomFit, zoomActual, focusMode
    // Simulation
    case simView, step, truthTable, scope, lock
    // Tabs
    case newTab, closeTab, reopenTab, splitView, switchPane, closeSplit, nextTab, previousTab
    // App
    case shortcuts, darkMode, feedback

    var id: String { rawValue }

    var section: String {
        switch self {
        case .newCircuit, .openLibrary, .importFile, .save, .exportImage, .exportFile, .print: "Circuits"
        case .undo, .redo, .cut, .copy, .paste, .duplicate, .selectAll: "Editing"
        case .addGate, .rotate, .straighten, .tidy, .quickCopy, .quickPaste, .quickCut, .quickDuplicate: "Building"
        case .zoomIn, .zoomOut, .zoomFit, .zoomActual, .focusMode: "Moving around"
        case .simView, .step, .truthTable, .scope, .lock: "Simulation"
        case .newTab, .closeTab, .reopenTab, .splitView, .switchPane, .closeSplit, .nextTab, .previousTab: "Tabs and split view"
        case .shortcuts, .darkMode, .feedback: "App"
        }
    }

    var name: String {
        switch self {
        case .newCircuit: "New circuit"
        case .openLibrary: "Open one of your circuits"
        case .importFile: "Import a .cdl file"
        case .save: "Save now and keep a version"
        case .exportImage: "Export as an image"
        case .exportFile: "Export as a CedarLogic file"
        case .print: "Print"
        case .undo: "Undo"
        case .redo: "Redo"
        case .cut: "Cut"
        case .copy: "Copy"
        case .paste: "Paste (it follows your mouse)"
        case .duplicate: "Duplicate the selection"
        case .selectAll: "Select everything on the page"
        case .addGate: "Add a gate by name"
        case .rotate: "Rotate the selection"
        case .straighten: "Straighten the selected wires"
        case .tidy: "Tidy up (preview first)"
        case .quickCopy: "Copy (quick key)"
        case .quickPaste: "Paste (quick key)"
        case .quickCut: "Cut (quick key)"
        case .quickDuplicate: "Duplicate (quick key)"
        case .zoomIn: "Zoom in"
        case .zoomOut: "Zoom out"
        case .zoomFit: "Zoom to fit"
        case .zoomActual: "Actual size"
        case .focusMode: "Focus mode: hide the side panel"
        case .simView: "Simulation view"
        case .step: "Step once"
        case .truthTable: "Truth table"
        case .scope: "Oscilloscope"
        case .lock: "Lock the circuit"
        case .newTab: "New tab"
        case .closeTab: "Close tab"
        case .reopenTab: "Reopen the tab you closed"
        case .splitView: "Split view"
        case .switchPane: "Switch pane (in a split view)"
        case .closeSplit: "Close split view"
        case .nextTab: "Next tab"
        case .previousTab: "Previous tab"
        case .shortcuts: "Every shortcut (this list)"
        case .darkMode: "Dark mode"
        case .feedback: "Send feedback"
        }
    }

    var defaultCombo: KeyCombo? {
        let c = KeyCombo.command, s = KeyCombo.shift, o = KeyCombo.option, ctl = KeyCombo.control
        switch self {
        case .newCircuit: return KeyCombo(key: "n", mods: c)
        case .openLibrary: return KeyCombo(key: "o", mods: c)
        case .importFile: return KeyCombo(key: "o", mods: c | s)
        case .save: return KeyCombo(key: "s", mods: c)
        case .exportImage: return KeyCombo(key: "e", mods: c)
        case .exportFile: return KeyCombo(key: "e", mods: c | s)
        case .print: return KeyCombo(key: "p", mods: c)
        case .undo: return KeyCombo(key: "z", mods: c)
        case .redo: return KeyCombo(key: "z", mods: c | s)
        case .cut: return KeyCombo(key: "x", mods: c)
        case .copy: return KeyCombo(key: "c", mods: c)
        case .paste: return KeyCombo(key: "v", mods: c)
        case .duplicate: return KeyCombo(key: "d", mods: c)
        case .selectAll: return KeyCombo(key: "a", mods: c)
        case .addGate: return KeyCombo(key: "a", mods: 0)
        case .rotate: return KeyCombo(key: "r", mods: 0)
        case .straighten: return KeyCombo(key: "s", mods: 0)
        case .tidy: return KeyCombo(key: "s", mods: s)
        case .quickCopy: return KeyCombo(key: "c", mods: 0)
        case .quickPaste: return KeyCombo(key: "v", mods: 0)
        case .quickCut: return KeyCombo(key: "x", mods: 0)
        case .quickDuplicate: return KeyCombo(key: "d", mods: 0)
        case .zoomIn: return KeyCombo(key: "=", mods: c)
        case .zoomOut: return KeyCombo(key: "-", mods: c)
        case .zoomFit: return KeyCombo(key: "0", mods: c)
        case .zoomActual: return KeyCombo(key: "1", mods: c)
        case .focusMode: return KeyCombo(key: ".", mods: c)
        case .simView: return KeyCombo(key: "r", mods: c)
        case .step: return KeyCombo(key: "r", mods: c | s)
        case .truthTable: return KeyCombo(key: "t", mods: 0)
        case .scope: return KeyCombo(key: "g", mods: c)
        case .lock: return nil
        case .newTab: return KeyCombo(key: "t", mods: c)
        case .closeTab: return KeyCombo(key: "w", mods: c)
        case .reopenTab: return KeyCombo(key: "t", mods: c | s)
        case .splitView: return KeyCombo(key: "s", mods: c | o)
        case .switchPane: return KeyCombo(key: "right", mods: c | o)
        case .closeSplit: return KeyCombo(key: "w", mods: c | o)
        case .nextTab: return KeyCombo(key: "tab", mods: ctl)
        case .previousTab: return KeyCombo(key: "tab", mods: ctl | s)
        case .shortcuts: return KeyCombo(key: "/", mods: s)
        case .darkMode: return KeyCombo(key: "d", mods: c | s)
        case .feedback: return nil
        }
    }
}

/// The user's shortcuts: the defaults, with their changes on top.
@MainActor
final class ShortcutStore: ObservableObject {
    static let shared = ShortcutStore()
    private let key = "cl.shortcuts"

    /// Changed ones only; `nil` means "no shortcut".
    @Published private(set) var overrides: [String: KeyCombo?] = [:] {
        didSet { save() }
    }

    private init() {
        if let data = UserDefaults.standard.data(forKey: key),
           let o = try? JSONDecoder().decode([String: KeyCombo?].self, from: data) {
            overrides = o
        } else if UserDefaults.standard.object(forKey: "cl.themeKey") != nil {
            // The dark mode shortcut the wx app's settings carried over.
            let k = UserDefaults.standard.string(forKey: "cl.themeKey") ?? "d"
            let m = UserDefaults.standard.integer(forKey: "cl.themeMods")
            if m != 0 { overrides[ShortcutAction.darkMode.rawValue] = KeyCombo(key: k, mods: m) }
        }
    }

    private func save() {
        if let data = try? JSONEncoder().encode(overrides) { UserDefaults.standard.set(data, forKey: key) }
    }

    func combo(_ a: ShortcutAction) -> KeyCombo? {
        if let o = overrides[a.rawValue] { return o }
        return a.defaultCombo
    }
    func menu(_ a: ShortcutAction) -> KeyboardShortcut? { combo(a)?.swiftUI }
    func isCustom(_ a: ShortcutAction) -> Bool { overrides[a.rawValue] != nil }

    func set(_ a: ShortcutAction, _ c: KeyCombo?) {
        // Taking a combo from another command leaves that one without.
        if let c { for other in ShortcutAction.allCases where other != a && combo(other) == c { overrides[other.rawValue] = .some(nil) } }
        overrides[a.rawValue] = .some(c)
        if c == a.defaultCombo { overrides[a.rawValue] = nil }
    }
    func reset(_ a: ShortcutAction) { overrides[a.rawValue] = nil }
    func resetAll() { overrides = [:] }

    /// The command that already uses a combo, for the Settings warning.
    func owner(of c: KeyCombo, except a: ShortcutAction) -> ShortcutAction? {
        ShortcutAction.allCases.first { $0 != a && combo($0) == c }
    }

    /// The canvas's own keys: which command a key press is, if any.
    func canvasAction(for e: NSEvent) -> ShortcutAction? {
        guard let c = KeyCombo(event: e), !c.isMenuShortcut || c.key == "tab" else { return nil }
        return ShortcutAction.allCases.first { combo($0) == c }
    }
}
