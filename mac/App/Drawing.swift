// Drawing on the circuit (the website's docs/DRAWING-NOTES.md): a Draw mode
// where the pointer draws with a pen or a highlighter, or erases whole
// strokes, instead of editing. The strokes are the core's (cl_ink_*): it
// captures, simplifies, caps and draws them in their layers (highlighter
// under the parts, pen over them), and each stroke, eraser drag or Clear is
// one undo step. Here: the tools and their memory, the pointer (mouse,
// trackpad with its force, a tablet pen with its pressure and eraser end),
// the keys (P, H, E, Escape), and the drawing bar.

import AppKit
import SwiftUI

enum InkTool: String, CaseIterable {
    case pen, highlighter, eraser
    var name: String { ["Pen", "Highlighter", "Eraser"][index] }
    var icon: String { ["pencil.tip", "highlighter", "eraser"][index] }
    var key: String { ["P", "H", "E"][index] }
    private var index: Int { InkTool.allCases.firstIndex(of: self)! }
}

/// The last tool, colour and width, remembered on this Mac.
@MainActor
final class InkSettings: ObservableObject {
    static let shared = InkSettings()
    private let d = UserDefaults.standard

    static let penColors = ["ink", "red", "orange", "green", "blue", "purple"]
    static let highlighterColors = ["yellow", "green", "blue", "pink"]
    static let penWidths = [0.12, 0.25, 0.5]
    static let highlighterWidths = [0.8, 1.2, 2.0]

    @Published var tool: InkTool { didSet { d.set(tool.rawValue, forKey: "cl.ink.tool") } }
    @Published var penColor: String { didSet { d.set(penColor, forKey: "cl.ink.penColor") } }
    @Published var penWidth: Double { didSet { d.set(penWidth, forKey: "cl.ink.penWidth") } }
    @Published var highlighterColor: String { didSet { d.set(highlighterColor, forKey: "cl.ink.highlighterColor") } }
    @Published var highlighterWidth: Double { didSet { d.set(highlighterWidth, forKey: "cl.ink.highlighterWidth") } }

    private init() {
        tool = InkTool(rawValue: d.string(forKey: "cl.ink.tool") ?? "") ?? .pen
        let pc = d.string(forKey: "cl.ink.penColor") ?? "ink"
        penColor = Self.penColors.contains(pc) ? pc : "ink"
        let pw = d.double(forKey: "cl.ink.penWidth")
        penWidth = Self.penWidths.contains(pw) ? pw : 0.25
        let hc = d.string(forKey: "cl.ink.highlighterColor") ?? "yellow"
        highlighterColor = Self.highlighterColors.contains(hc) ? hc : "yellow"
        let hw = d.double(forKey: "cl.ink.highlighterWidth")
        highlighterWidth = Self.highlighterWidths.contains(hw) ? hw : 1.2
    }

    /// The tool that draws (the eraser's colour and width are the pen's).
    var drawsWithHighlighter: Bool { tool == .highlighter }
    var color: String { drawsWithHighlighter ? highlighterColor : penColor }
    var width: Double { drawsWithHighlighter ? highlighterWidth : penWidth }
    var colors: [String] { drawsWithHighlighter ? Self.highlighterColors : Self.penColors }
    var widths: [Double] { drawsWithHighlighter ? Self.highlighterWidths : Self.penWidths }
    func setColor(_ c: String) { if drawsWithHighlighter { highlighterColor = c } else { penColor = c } }
    func setWidth(_ w: Double) { if drawsWithHighlighter { highlighterWidth = w } else { penWidth = w } }
    /// The three widths by name (keys 1, 2 and 3 while drawing). A mouse or
    /// trackpad has no pressure, so its line is always the chosen width; a
    /// tablet pen's pressure thins it a little.
    static let widthNames = ["Thin", "Medium", "Thick"]
    var widthIndex: Int { widths.firstIndex { abs($0 - width) < 1e-9 } ?? 1 }
    func setWidthIndex(_ i: Int) { if widths.indices.contains(i) { setWidth(widths[i]) } }

    /// "Red pen", "Yellow highlighter": a colour in plain words.
    static func colorName(_ token: String, highlighter: Bool) -> String {
        token.prefix(1).uppercased() + token.dropFirst() + (highlighter ? " highlighter" : " pen")
    }

    /// A token's colour as the canvas draws it (light or dark).
    static func swatch(_ token: String, highlighter: Bool, dark: Bool) -> Color {
        var r = 0.0, g = 0.0, b = 0.0, a = 0.0
        cl_ink_color(token, Int32(highlighter ? CL_INK_HIGHLIGHTER : CL_INK_PEN), dark ? 1 : 0, &r, &g, &b, &a)
        // A highlighter's swatch at full strength, so it can be told apart.
        return Color(.sRGB, red: r, green: g, blue: b, opacity: highlighter ? 1 : a)
    }
}

/// Says something to VoiceOver.
@MainActor
func announce(_ text: String) {
    guard let w = NSApp.keyWindow ?? NSApp.mainWindow else { return }
    NSAccessibility.post(element: w, notification: .announcementRequested,
                         userInfo: [.announcement: text, .priority: NSAccessibilityPriorityLevel.high.rawValue])
}

// MARK: - The controller

extension CanvasController {
    static let inkFullNote = "This drawing is full. Erase some, or clear a page."
    static let inkReadOnlyNote = "This page has a drawing from a newer CedarLogic. It's kept, but this version can't show or change it."

    /// Called as Draw mode turns on or off.
    func drawingChanged() {
        guard let document else { return }
        if drawing {
            if isFloating { cancelFloating() }
            document.cancelGesture()
            if !document.inkShown {
                document.setInkShown(true)
                markEdited()
                note("Drawing shown.")
                announce("Drawing on. Drawing shown.")
            } else {
                note("Draw: P pen, H highlighter, E eraser, 1 2 3 width. Escape when you're done.")
                announce("Drawing on")
            }
            if let why = inkBlockedReason { note(why) }
        } else {
            cl_ink_cancel(document.handle)
            cl_ink_hover_clear(document.handle)
            if statusMessage.hasPrefix("Draw") || statusMessage.hasPrefix("This drawing") { note("") }
            announce("Drawing off")
            NSCursor.arrow.set()
        }
        inkVersion += 1
        redraw()
    }

    /// Why the tools can't draw on this page, if they can't.
    var inkBlockedReason: String? {
        guard let document else { return nil }
        switch cl_ink_can_draw(document.handle, Int32(page)) {
        case Int32(CL_INK_READ_ONLY): return Self.inkReadOnlyNote
        case Int32(CL_INK_FULL): return Self.inkFullNote
        default: return nil
        }
    }
    var pageIsInkReadOnly: Bool { document.map { cl_ink_page_read_only($0.handle, Int32(page)) } ?? false }
    var pageHasInk: Bool { (document?.inkStrokeCount(page: page) ?? 0) > 0 }
    var drawingShown: Bool { document?.inkShown ?? true }
    /// A drawing that's hidden: the Draw button shows a dot.
    var hasHiddenDrawing: Bool { document.map { $0.hasInk && !$0.inkShown } ?? false }

    func toggleDrawing() { drawing.toggle() }

    /// Clear Drawing: this page's strokes, one undo step, without asking.
    func clearDrawing() {
        guard let document else { return }
        if pageIsInkReadOnly { note(Self.inkReadOnlyNote); return }
        let n = Int(cl_ink_clear(document.handle, Int32(page)))
        guard n > 0 else { note("Nothing drawn on this page."); return }
        edited()
        inkVersion += 1
        note("Cleared the drawing on this page. Undo brings it back.")
        announce("Drawing cleared")
    }

    /// Show or Hide Drawing: saved with the circuit, not an undo step.
    func setDrawingShown(_ shown: Bool) {
        guard let document, document.inkShown != shown else { return }
        if !shown && drawing { drawing = false }
        document.setInkShown(shown)
        markEdited()
        inkVersion += 1
        redraw()
        let s = shown ? "Drawing shown." : "Drawing hidden."
        note(s)
        announce(s)
    }

    func toggleDrawingShown() { setDrawingShown(!drawingShown) }
}

// MARK: - The pointer and keys

extension CircuitCanvasNSView {
    /// World units the eraser reaches: 10 points on screen.
    private var eraserRadius: Double { 10 * unitsPerPoint }

    /// A tablet pen's eraser end, or the eraser tool.
    private func erases(_ e: NSEvent) -> Bool { InkSettings.shared.tool == .eraser || tabletEraser }

    /// A tablet's pressure (0...1), or the trackpad's force while it's
    /// pressed (light 0.4, a deep press 1), or none (-1).
    private func pressure(_ e: NSEvent) -> Double {
        if e.subtype == .tabletPoint { return Double(max(0, min(1, e.pressure))) }
        if let f = forcePressure { return 0.4 + 0.6 * f }
        return -1
    }

    private func sample(_ e: NSEvent, at p: CGPoint) {
        guard let document else { return }
        let w = worldPoint(p)
        var pt = CLInkPoint(x: w.x, y: w.y, pressure: pressure(e))
        if cl_ink_add(document.handle, &pt, 1) == Int32(CL_INK_FULL) {
            // Cut at the cap: what's drawn so far is kept.
            controller?.note(CanvasController.inkFullNote)
        }
    }

    func inkMouseDown(_ e: NSEvent, at p: CGPoint) -> Bool {
        guard let controller, let document else { return false }
        // Space or Cmd held: move around, as ever.
        if spaceDown || e.modifierFlags.contains(.command) { return false }
        zoomAnim = nil
        hideInkHoverIfNeeded()
        let w = worldPoint(p)
        if erases(e) {
            if controller.pageIsInkReadOnly { controller.note(CanvasController.inkReadOnlyNote); return true }
            cl_ink_erase_begin(document.handle, Int32(page))
            inkErasing = true
            _ = cl_ink_erase_to(document.handle, w.x, w.y, eraserRadius)
        } else {
            let s = InkSettings.shared
            let r = cl_ink_begin(document.handle, Int32(page), Int32(s.drawsWithHighlighter ? CL_INK_HIGHLIGHTER : CL_INK_PEN),
                                 s.color, s.width, unitsPerPoint)
            if r != Int32(CL_INK_OK) {
                controller.note(r == Int32(CL_INK_FULL) ? CanvasController.inkFullNote : CanvasController.inkReadOnlyNote)
                NSSound.beep()
                return true
            }
            inkErasing = false
            sample(e, at: p)
        }
        // Every sample, not just one a frame: a line follows the pen.
        NSEvent.isMouseCoalescingEnabled = false
        drag = .ink
        needsDisplay = true
        return true
    }

    func inkMouseDragged(_ e: NSEvent, at p: CGPoint) {
        guard let document else { return }
        if inkErasing {
            let before = cl_ink_stroke_count(document.handle, Int32(page))
            let w = worldPoint(p)
            _ = cl_ink_erase_to(document.handle, w.x, w.y, eraserRadius)
            cl_ink_hover(document.handle, Int32(page), w.x, w.y, Int32(CL_INK_ERASER), "ink", eraserRadius)
            if cl_ink_stroke_count(document.handle, Int32(page)) != before { controller?.redraw() }
        } else {
            sample(e, at: p)
        }
        needsDisplay = true
    }

    func inkMouseUp(_ e: NSEvent, at p: CGPoint) {
        NSEvent.isMouseCoalescingEnabled = true
        guard let controller, let document else { return }
        if inkErasing {
            inkErasing = false
            let n = Int(cl_ink_erase_end(document.handle))
            if n > 0 { controller.edited(); controller.inkVersion += 1 }
        } else {
            sample(e, at: p)
            if cl_ink_end(document.handle) > 0 { controller.edited(); controller.inkVersion += 1 }
            if cl_ink_can_draw(document.handle, Int32(page)) == Int32(CL_INK_FULL) { controller.note(CanvasController.inkFullNote) }
        }
        forcePressure = nil
        inkHover(at: p)
        controller.redraw()
    }

    /// What the pointer would draw: a dot of the stroke, or the eraser's ring.
    func inkHover(at p: CGPoint) {
        guard let document else { return }
        let w = worldPoint(p)
        let s = InkSettings.shared
        if s.tool == .eraser || tabletEraser {
            cl_ink_hover(document.handle, Int32(page), w.x, w.y, Int32(CL_INK_ERASER), "ink", eraserRadius)
        } else {
            cl_ink_hover(document.handle, Int32(page), w.x, w.y, Int32(s.drawsWithHighlighter ? CL_INK_HIGHLIGHTER : CL_INK_PEN),
                         s.color, s.width)
        }
        NSCursor.crosshair.set()
        needsDisplay = true
    }

    private func hideInkHoverIfNeeded() {
        if let document { cl_ink_hover_clear(document.handle) }
    }

    func inkTabletProximity(_ e: NSEvent) {
        tabletEraser = e.isEnteringProximity && e.pointingDeviceType == .eraser
        if let w = window, controller?.drawing == true {
            inkHover(at: convert(w.mouseLocationOutsideOfEventStream, from: nil))
        }
    }

    func inkPressureChange(_ e: NSEvent) {
        guard case .ink = drag else { forcePressure = nil; return }
        forcePressure = e.stage >= 2 ? 1.0 : e.stage == 1 ? Double(max(0, min(1, e.pressure))) : nil
    }

    func inkRightMouseDown(_ e: NSEvent) {
        guard let controller else { return }
        let menu = NSMenu()
        func item(_ title: String, enabled: Bool = true, _ action: @escaping () -> Void) {
            let i = NSMenuItem(title: title, action: enabled ? #selector(MenuAction.run) : nil, keyEquivalent: "")
            let target = MenuAction(action)
            i.target = target
            i.representedObject = target
            i.isEnabled = enabled
            menu.addItem(i)
        }
        item("Clear Drawing on This Page", enabled: controller.pageHasInk && !controller.pageIsInkReadOnly) { controller.clearDrawing() }
        item("Hide Drawing") { controller.setDrawingShown(false) }
        menu.addItem(.separator())
        item("Done Drawing") { controller.drawing = false }
        NSMenu.popUpContextMenu(menu, with: e, for: self)
    }

    /// P, H and E pick a tool, 1, 2 and 3 a width; Escape ends a stroke,
    /// then Draw mode.
    func inkKeyDown(_ e: NSEvent) -> Bool {
        guard let controller else { return false }
        let bare = e.modifierFlags.intersection([.command, .option, .control, .shift]).isEmpty
        if e.keyCode == 53 {
            if case .ink = drag {
                NSEvent.isMouseCoalescingEnabled = true
                if let document {
                    if inkErasing { if cl_ink_erase_end(document.handle) > 0 { controller.edited() } } else { cl_ink_cancel(document.handle) }
                }
                inkErasing = false
                drag = .none
                controller.redraw()
            } else {
                controller.drawing = false
            }
            return true
        }
        guard bare, let ch = e.charactersIgnoringModifiers?.lowercased() else { return false }
        if let n = ["1": 0, "2": 1, "3": 2][ch] {
            let s = InkSettings.shared
            guard s.tool != .eraser else { return true }
            s.setWidthIndex(n)
            let name = InkSettings.widthNames[n] + " line"
            controller.note(name)
            announce(name)
            if let w = window { inkHover(at: convert(w.mouseLocationOutsideOfEventStream, from: nil)) }
            return true
        }
        let tool: InkTool? = ch == "p" ? .pen : ch == "h" ? .highlighter : ch == "e" ? .eraser : nil
        guard let tool else { return false }
        InkSettings.shared.tool = tool
        controller.note(tool.name)
        announce(tool.name)
        if let w = window { inkHover(at: convert(w.mouseLocationOutsideOfEventStream, from: nil)) }
        return true
    }
}

// MARK: - The drawing bar

/// The tools while Draw is on: pen, highlighter, eraser; the tool's colours
/// and three widths; Clear; Show/Hide; Done. Over the bottom of the canvas.
struct DrawingBar: View {
    @ObservedObject var canvas: CanvasController
    @ObservedObject private var ink = InkSettings.shared
    @ObservedObject private var prefs = Prefs.shared
    /// Drawn for a picture (--render-ui): shown whatever Draw mode says.
    var forceShown = false

    private var dark: Bool { prefs.dark || canvas.simView }
    private var accent: Color { prefs.accentColor(dark: dark) }

    var body: some View {
        if canvas.drawing || forceShown {
            let _ = canvas.inkVersion
            let blocked = canvas.inkBlockedReason
            let readOnly = canvas.pageIsInkReadOnly
            VStack(spacing: 6) {
                if let blocked { Text(blocked).font(.system(size: 11)).foregroundStyle(.secondary) }
                HStack(spacing: 4) {
                    ForEach(InkTool.allCases, id: \.self) { t in
                        toolButton(t, disabled: readOnly || (t != .eraser && blocked != nil))
                    }
                    divider
                    if ink.tool == .eraser {
                        Text("Erases whole marks").font(.system(size: 11)).foregroundStyle(.secondary).padding(.horizontal, 6)
                    } else {
                        ForEach(ink.colors, id: \.self) { colorButton($0) }
                        divider
                        Text("Width").font(.system(size: 11, weight: .medium)).foregroundStyle(.secondary)
                            .padding(.leading, 2).padding(.trailing, 2)
                            .help("How thick the line is: 1, 2 or 3. A mouse or trackpad draws it steady; a pen tablet's pressure thins it a little.")
                        ForEach(Array(ink.widths.enumerated()), id: \.offset) { i, w in widthButton(w, index: i) }
                    }
                    divider
                    iconButton("trash", "Clear Drawing on This Page", disabled: !canvas.pageHasInk || readOnly) { canvas.clearDrawing() }
                    iconButton(canvas.drawingShown ? "eye" : "eye.slash",
                               canvas.drawingShown ? "Hide Drawing" : "Show Drawing") { canvas.toggleDrawingShown() }
                    Button("Done") { canvas.drawing = false }
                        .buttonStyle(.borderless)
                        .font(.system(size: 12, weight: .semibold))
                        .padding(.horizontal, 8)
                        .help("Done drawing (Esc)")
                }
            }
            .padding(.horizontal, 10).padding(.vertical, 6)
            .background(.regularMaterial, in: Capsule())
            .overlay(Capsule().strokeBorder(Color.secondary.opacity(0.25)))
            .shadow(color: .black.opacity(0.18), radius: 8, y: 2)
            .environment(\.colorScheme, dark ? .dark : .light)
            .accessibilityElement(children: .contain)
            .accessibilityLabel("Drawing tools")
            .transition(.move(edge: .bottom).combined(with: .opacity))
        }
    }

    private var divider: some View { Rectangle().fill(Color.secondary.opacity(0.3)).frame(width: 1, height: 18).padding(.horizontal, 4) }

    private func toolButton(_ t: InkTool, disabled: Bool) -> some View {
        let on = ink.tool == t
        return Button { ink.tool = t } label: {
            Image(systemName: t.icon)
                .font(.system(size: 14, weight: on ? .semibold : .regular))
                .frame(width: 28, height: 26)
                .foregroundStyle(on ? accent : Color.primary.opacity(0.8))
                .background(RoundedRectangle(cornerRadius: 6).fill(on ? accent.opacity(0.16) : .clear))
        }
        .buttonStyle(.plain)
        .disabled(disabled)
        .opacity(disabled ? 0.4 : 1)
        .help("\(t.name) (\(t.key))")
        .accessibilityLabel(t.name)
        .accessibilityAddTraits(on ? .isSelected : [])
    }

    private func colorButton(_ token: String) -> some View {
        let hl = ink.drawsWithHighlighter
        let on = ink.color == token
        let name = InkSettings.colorName(token, highlighter: hl)
        return Button { ink.setColor(token) } label: {
            Circle().fill(InkSettings.swatch(token, highlighter: hl, dark: dark))
                .frame(width: 16, height: 16)
                .overlay(Circle().strokeBorder(Color.primary.opacity(0.25), lineWidth: 0.5))
                .padding(3)
                .overlay(Circle().strokeBorder(on ? accent : .clear, lineWidth: 2))
                .frame(width: 26, height: 26)
        }
        .buttonStyle(.plain)
        .help(name)
        .accessibilityLabel(name)
        .accessibilityAddTraits(on ? .isSelected : [])
    }

    /// A width: a short line that thick, in the tool's colour.
    private func widthButton(_ w: Double, index: Int) -> some View {
        let on = abs(ink.width - w) < 1e-9
        let name = InkSettings.widthNames[index]
        let colour = ink.drawsWithHighlighter ? InkSettings.swatch(ink.color, highlighter: true, dark: dark)
                                              : InkSettings.swatch(ink.color, highlighter: false, dark: dark)
        return Button { ink.setWidth(w) } label: {
            Capsule().fill(colour.opacity(on ? 1 : 0.6))
                .frame(width: 18, height: [2.0, 4.0, 7.0][index])
                .frame(width: 30, height: 26)
                .background(RoundedRectangle(cornerRadius: 6).fill(on ? accent.opacity(0.16) : .clear))
                .overlay(RoundedRectangle(cornerRadius: 6).strokeBorder(on ? accent.opacity(0.7) : .clear, lineWidth: 1))
        }
        .buttonStyle(.plain)
        .help("\(name) line (\(index + 1))")
        .accessibilityLabel(name + " line")
        .accessibilityAddTraits(on ? .isSelected : [])
    }

    private func iconButton(_ icon: String, _ tip: String, disabled: Bool = false, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: icon).font(.system(size: 13)).frame(width: 28, height: 26)
                .foregroundStyle(Color.primary.opacity(0.8))
        }
        .buttonStyle(.plain)
        .disabled(disabled)
        .opacity(disabled ? 0.4 : 1)
        .help(tip)
        .accessibilityLabel(tip)
    }
}
