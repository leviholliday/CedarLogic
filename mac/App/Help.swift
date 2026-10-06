// Help (F1, ⌘?): pages on how this app works, written for it and quoting
// your own shortcuts (Settings > Shortcuts), the full shortcut list, and
// Classic Help -- the original CedarLogic Help's contents and pages, just as
// they were, kept as a nod to where it came from. One search covers them all.

import AppKit
import SwiftUI
import WebKit

// MARK: - Pages

/// A help page: a title, a line under it, and blocks. In text, `{action}`
/// becomes that command's current shortcut, and **bold** is bold.
struct HelpPage: Identifiable, Hashable {
    enum Block: Hashable {
        case p(String)
        case h(String)
        case keys([Key])
        case tip(String)
    }
    struct Key: Hashable {
        let keys: String   // "{addGate}" or literal keys
        let what: String
    }
    let id: String
    let title: String
    let icon: String
    let line: String
    let blocks: [Block]

    /// Everything on the page, for searching.
    var text: String {
        ([title, line] + blocks.map {
            switch $0 {
            case .p(let s), .h(let s), .tip(let s): return s
            case .keys(let k): return k.map { $0.what }.joined(separator: " ")
            }
        }).joined(separator: " ")
    }
}

@MainActor
enum HelpText {
    /// `{action}` → the shortcut, as it's set now.
    static func fill(_ s: String) -> String {
        var out = s
        for a in ShortcutAction.allCases where out.contains("{\(a.rawValue)}") {
            out = out.replacingOccurrences(of: "{\(a.rawValue)}", with: ShortcutStore.shared.combo(a)?.label ?? "(no shortcut)")
        }
        return out
    }
}

enum HelpBook {
    static let started: [HelpPage] = [
        HelpPage(id: "welcome", title: "Welcome to CedarLogic", icon: "sparkles",
                 line: "Build logic circuits, run them live, and hand them in.",
                 blocks: [
                    .p("CedarLogic is a digital logic simulator. You put gates on a page, wire them together, and the circuit runs as you build it: switches you can click, lights that show what's happening, wires that change colour with their signals."),
                    .p("The window has three parts: the **palette** of gates down the left (with a small map of the page under it), the **canvas** where you build, and the **toolbar** across the top. Each circuit can have several **tabs** (pages), shown above the canvas."),
                    .tip("New here? Help > Guided Tour builds a working AND circuit with you, a step at a time. It takes about five minutes."),
                 ]),
        HelpPage(id: "first", title: "Your First Circuit", icon: "lightbulb",
                 line: "Two switches, an AND gate and a light.",
                 blocks: [
                    .h("1. Two switches"),
                    .p("Pick **Input and Output** in the menu at the top of the palette and drag an **On / Off Toggle Switch** onto the canvas. Drag a second one below it."),
                    .h("2. The gate"),
                    .p("Press **{addGate}**, type **and**, press Return and move the pointer onto the canvas: the gate appears and follows the pointer. Click to put it down to the right of the switches."),
                    .h("3. The light"),
                    .p("Add an **LED** (from Input and Output too) to the right of the gate."),
                    .h("4. Wires"),
                    .p("Drag from each switch's output pin to one of the gate's inputs, and from the gate's output to the light. A small square shows the pin a wire will join."),
                    .h("5. Try it"),
                    .p("Click the switches. The light comes on only when both are on. Press **{truthTable}** for the truth table."),
                 ]),
    ]

    static let using: [HelpPage] = [
        HelpPage(id: "gates", title: "Adding Gates", icon: "square.grid.2x2",
                 line: "From the palette, or by name.",
                 blocks: [
                    .p("**Drag** a gate out of the palette and let go where you want it. Or **click** it: it follows the pointer until you click the canvas."),
                    .p("**Quick Add ({addGate})**: type part of a gate's name, press Return, and move onto the canvas. The gate appears at the pointer; click to put it down. Escape changes your mind."),
                    .p("The menu at the top of the palette picks a family. **Shift+1** to **Shift+0** jump straight to the first ten."),
                    .tip("Settings > Appearance sets how big the palette's gates are, and whether their names show."),
                 ]),
        HelpPage(id: "wiring", title: "Wiring", icon: "point.3.connected.trianglepath.dotted",
                 line: "Pin to pin, or let C do it.",
                 blocks: [
                    .p("**Drag** from one pin to another. While you drag, a small square marks the pin the wire will join. Or **click** a pin, then click the other end."),
                    .p("**{quickCopy} while moving a gate** (dragging it, or with it on the pointer from the palette, Quick Add or a paste) connects its free pins to the pins they're right next to, and keeps it moving. Escape takes back just those connections."),
                    .p("Putting a gate down right next to a free pin connects them too."),
                    .p("**Straighten ({straighten})** tidies the selected wires into clean routes. **Tidy Up ({tidy})** tidies the whole page, showing you first: Return keeps it, Escape doesn't, Tab tries the other way."),
                    .h("Wire colours"),
                    .keys([
                        HelpPage.Key(keys: "Silver", what: "0 (grey on older looks, black on white)"),
                        HelpPage.Key(keys: "Red", what: "1. A bus is redder the more of its bits are 1"),
                        HelpPage.Key(keys: "Green", what: "Nothing driving it (high-Z), like a tri-state buffer that's off"),
                        HelpPage.Key(keys: "Blue", what: "Unknown: the simulation can't tell yet"),
                        HelpPage.Key(keys: "Cyan", what: "Conflict: two outputs fighting over one wire"),
                    ]),
                 ]),
        HelpPage(id: "editing", title: "Selecting and Editing", icon: "cursorarrow.rays",
                 line: "Move, copy, rotate and delete.",
                 blocks: [
                    .p("Click a gate or wire to select it; Shift-click to add more; drag across empty canvas to box-select. **{selectAll}** selects everything on the page."),
                    .keys([
                        HelpPage.Key(keys: "{rotate}", what: "Rotate the selection a quarter turn"),
                        HelpPage.Key(keys: "{quickDuplicate}", what: "Duplicate: the copy follows the pointer"),
                        HelpPage.Key(keys: "{quickCopy}", what: "Copy (when nothing's moving)"),
                        HelpPage.Key(keys: "{quickPaste}", what: "Paste: it follows the pointer until you click"),
                        HelpPage.Key(keys: "{quickCut}", what: "Cut"),
                        HelpPage.Key(keys: "Delete", what: "Delete the selection"),
                        HelpPage.Key(keys: "Arrows", what: "Nudge the selection a square (Shift: five)"),
                        HelpPage.Key(keys: "{undo} / {redo}", what: "Undo and redo"),
                    ]),
                    .p("Double-click a gate for its settings (a clock's speed, a label's text). Double-click a RAM or ROM to edit its contents."),
                 ]),
        HelpPage(id: "moving", title: "Moving Around", icon: "arrow.up.and.down.and.arrow.left.and.right",
                 line: "Zoom, pan and the minimap.",
                 blocks: [
                    .keys([
                        HelpPage.Key(keys: "Scroll / pinch", what: "Zoom or move, as set in Settings > Canvas"),
                        HelpPage.Key(keys: "⌘ + scroll", what: "Always zooms"),
                        HelpPage.Key(keys: "Shift + scroll", what: "Moves sideways"),
                        HelpPage.Key(keys: "Space", what: "Tap to fit the page; hold and drag to move around"),
                        HelpPage.Key(keys: "{zoomIn} / {zoomOut}", what: "Zoom in and out"),
                        HelpPage.Key(keys: "{zoomFit}", what: "Zoom to fit"),
                        HelpPage.Key(keys: "{zoomActual}", what: "Actual size"),
                        HelpPage.Key(keys: "Arrows", what: "Move around (when nothing's selected)"),
                        HelpPage.Key(keys: "{focusMode}", what: "Focus mode: hide the side panel"),
                    ]),
                    .p("The minimap under the palette shows the whole page; its red box is what you can see. Click or drag in it to go there."),
                 ]),
        HelpPage(id: "tabs", title: "Tabs and Split View", icon: "rectangle.split.2x1",
                 line: "Pages side by side.",
                 blocks: [
                    .keys([
                        HelpPage.Key(keys: "{newTab}", what: "New tab"),
                        HelpPage.Key(keys: "{closeTab}", what: "Close the tab (it asks if there's work on it)"),
                        HelpPage.Key(keys: "{reopenTab}", what: "Reopen the tab you closed"),
                        HelpPage.Key(keys: "{nextTab}", what: "Switch tabs: tap for the last one, hold for all of them"),
                        HelpPage.Key(keys: "{splitView}", what: "Split view on or off"),
                    ]),
                    .p("Double-click a tab to rename it; drag it along the strip to reorder. Drag a tab **down over the canvas** to split the view, dropping it on the half you want. With a split open, drag tabs between the two sides; a side that runs out of tabs closes."),
                 ]),
        HelpPage(id: "sim", title: "Running the Simulation", icon: "play.circle",
                 line: "It runs while you build.",
                 blocks: [
                    .p("The circuit runs all the time. The toolbar pauses and resumes it, **{step}** steps once, and the speed slider sets how long each step takes."),
                    .p("Click a switch to flip it; keypads and pulse generators take clicks too."),
                    .p("**Simulation View ({simView})** is a dark, live presentation of the circuit: signals flow along the wires that are on, and the bar at the bottom shows every switch and light. Space pauses, Escape leaves."),
                    .p("**Predict** (in Simulation View's bar) covers every light with a ?. Click a light (or Tab to it and press 0 or 1; a display takes a hex digit) to guess what it will show, then **Reveal** (Return): right guesses ring green, wrong ones red. Flipping a switch or stepping covers them again for the next round."),
                    .p("**Projector mode** (the projector button in the bar, or the View menu) draws Simulation View for the back of a classroom: thick wires, big labels and lights, and a bigger bar. It's remembered on this Mac and never changes the circuit."),
                    .p("**Lock** (in the toolbar) stops edits, so a circuit can be shown and played with but not changed. Switches still work."),
                 ]),
        HelpPage(id: "analysis", title: "Truth Tables and the Oscilloscope", icon: "tablecells",
                 line: "See what a circuit does.",
                 blocks: [
                    .p("**Truth table ({truthTable})** tries every combination of the page's switches and writes down its lights. Circuits with clocks or flip-flops are read row by row, each after the circuit settles."),
                    .p("Beside the table: each light's **simplest sum of products** and **product of sums**, and its **Karnaugh map** with the groups drawn on (groups of 1s or of 0s). Rows with no clear 0 or 1 count as don't-cares. Copy either formula, or **Build This as a Circuit** to make it again as gates."),
                    .p("**Check my circuit ({checkCircuit})** compares the lights with what the assignment asks for: type the formulas (**S = A ^ B ^ Cin**, then **Cout = AB + Cin(A ^ B)**), a minterm list (**F(A,B,C) = Σm(1,3,5) + d(7)**), or paste the truth table you were given. Switches and lights are matched by name (pick by hand under **Names** when they differ), don't-cares are skipped, and wrong rows are shown with what was expected and what the circuit gave. The last check is kept for each circuit."),
                    .p("**Oscilloscope ({scope})** records signals over time. Every **TO** label becomes a signal you can watch. Its share button copies a **timing diagram** for a lab report, or saves it as a PNG or PDF (what's on screen, or the whole recording)."),
                    .p("**Point at a wire** and every branch of it lights up; rest there a moment (at once in Simulation View) and a tag shows what it carries: 0, 1, Z (floating: nothing drives it) or ! (a conflict: two outputs disagree)."),
                 ]),
        HelpPage(id: "formula", title: "Build from a Formula", icon: "function",
                 line: "Type it, get the gates.",
                 blocks: [
                    .p("**Edit > Build from Formula…** turns a formula into switches, gates and a light for each output, labelled and wired, on a new page or beside what's there. One undo takes it back."),
                    .p("Write NOT as **A'** or **~A**, AND as **AB**, **A·B** or **A*B**, OR as **A + B**, XOR as **A ⊕ B** or **A ^ B**. Names are one letter, maybe with digits (**X1**), or a capital and small letters (**Cin**). One output per line: **S = A ^ B ^ Cin** then **Cout = AB + Cin(A ^ B)**. Or list minterms: **F(A,B,C) = Σm(1,3,5) + d(7)**."),
                    .p("Build it **as written**, or as the **simplest sum of products** or **product of sums**; with **any gates**, **NAND only** or **NOR only**; and with **only 2-input gates** if the exercise says so."),
                 ]),
        HelpPage(id: "find", title: "Finding Things", icon: "magnifyingglass",
                 line: "Big circuits, found fast.",
                 blocks: [
                    .p("**Find ({find})** searches every page for labels, **TO/FROM** names and parts (\"flip\", \"AND\", \"LED\"). Return goes to the next one; each is selected and brought to the middle."),
                    .p("With a label or a TO/FROM selected, Find opens looking for its name, so a TO's FROMs are a Return away."),
                 ]),
        HelpPage(id: "memory", title: "Memory: RAM and ROM", icon: "memorychip",
                 line: "Look inside, and change what's there.",
                 blocks: [
                    .p("Double-click a RAM or ROM to open its editor: every address, in hex or decimal, with what the circuit last read and wrote marked. Click a value to change it; jump to an address; load or save a **.cdm** memory file."),
                 ]),
        HelpPage(id: "saving", title: "Saving, Your Circuits and Versions", icon: "clock.arrow.circlepath",
                 line: "It keeps itself.",
                 blocks: [
                    .p("Every circuit lives in **Your Circuits ({openLibrary})** and saves itself a couple of seconds after each change. A new circuit joins it once there's something on it. Open, rename or delete from there, or rename the one you're in by clicking its name at the top of the window."),
                    .p("**Files**: opening a .cdl file (from the Finder, or {importFile}) brings in a copy to work on; the file itself isn't touched, and opening it again finds the copy. To get a file out, **File > Export as CedarLogic File…** saves a copy wherever you like."),
                    .p("**Versions**: a version is kept each time you press **{save}**, when you come back after a break, and every half hour while you work, whenever the circuit itself changed (flipping switches doesn't count). File > Version History shows them with a picture of each; restoring keeps the current one too."),
                    .keys([
                        HelpPage.Key(keys: "{newCircuit}", what: "New circuit"),
                        HelpPage.Key(keys: "{importFile}", what: "Open a .cdl file from anywhere"),
                        HelpPage.Key(keys: "{exportImage}", what: "Export an image (PNG or PDF), with your name under it"),
                        HelpPage.Key(keys: "{exportReport}", what: "Export a lab report: one PDF with the circuit, truth table, Karnaugh maps and timing diagram"),
                        HelpPage.Key(keys: "{exportFile}", what: "Export as a CedarLogic file"),
                        HelpPage.Key(keys: "{print}", what: "Print"),
                    ]),
                    .tip("Opening a circuit takes the place of the one you're in, like classic CedarLogic (it asks first if there's anything unsaved). Settings > General can open each in its own window instead."),
                 ]),
        HelpPage(id: "templates", title: "Templates and My Parts", icon: "square.on.square",
                 line: "Start ahead, and reuse what you've built.",
                 blocks: [
                    .p("**File > New from Template…** starts a circuit from a **Lab Page** (a title block with your name), a **4-Bit Counter**, a **7-Segment Decoder Starter**, or one of your own. **File > Save as Template…** keeps the circuit you're in as one of yours, every tab of it. Settings > General can make every new circuit start from one."),
                    .p("**My Parts**: select some gates and choose **Edit > Save as Part…**, and name it. It's at the bottom of the side panel's list (**My Parts**) to drag or click in, and in **Add a Gate ({addGate})** under its name. It drops in as a copy of those gates and wires, so any CedarLogic can open the circuit."),
                    .tip("Right-click a part (in the side panel or in Add a Gate) to rename or delete it. Your templates rename and delete from New from Template."),
                 ]),
        HelpPage(id: "settings", title: "Settings", icon: "gearshape",
                 line: "Make it yours (⌘,).",
                 blocks: [
                    .p("**General**: your name for exports, how circuits open, what new circuits start from, quitting, the status bar, updates and crash reports. **Appearance**: theme, the app's colour (CedarLogic green, or another), tabs, the grid, wires and the palette. **Canvas**: what scrolling does, right-click, duplicating and Tidy Up. **Toolbar**: its style and which tools it shows. **Shortcuts**: change any shortcut."),
                    .p("**{darkMode}** switches between light and dark any time."),
                 ]),
    ]

    static var all: [HelpPage] { started + using }
}

// MARK: - Classic Help

/// The original CedarLogic Help (res/help): its table of contents
/// (KLS_Logic.hhc), and the pages.
struct ClassicNode: Identifiable, Hashable {
    let id: String
    let name: String
    let file: String?
    var children: [ClassicNode]?
}

@MainActor
enum ClassicHelp {
    static var folder: URL? {
        if let url = Bundle.main.url(forResource: "ClassicHelp", withExtension: nil) { return url }
        let repo = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent().appendingPathComponent("res/help")
        return FileManager.default.fileExists(atPath: repo.path) ? repo : nil
    }

    /// The contents, parsed from the .hhc's nested lists.
    static let contents: [ClassicNode] = {
        guard let folder, let text = try? String(contentsOf: folder.appendingPathComponent("KLS_Logic.hhc"), encoding: .isoLatin1)
        else { return [] }
        var stack: [[ClassicNode]] = [[]]
        var counter = 0
        var i = text.startIndex
        func param(_ obj: Substring, _ name: String) -> String? {
            guard let r = obj.range(of: "name=\"\(name)\" value=\""),
                  let end = obj[r.upperBound...].firstIndex(of: "\"") else { return nil }
            return String(obj[r.upperBound..<end])
        }
        while i < text.endIndex {
            let rest = text[i...]
            if rest.hasPrefix("<ul>") || rest.hasPrefix("<UL>") {
                stack.append([])
                i = text.index(i, offsetBy: 4)
            } else if rest.hasPrefix("</ul>") || rest.hasPrefix("</UL>") {
                // A list ends: it holds the children of the entry just before
                // it -- or, the outermost, the contents themselves.
                let kids = stack.removeLast()
                if stack.isEmpty { stack = [kids]; i = text.index(i, offsetBy: 5); continue }
                let top = stack.count - 1
                if let last = stack[top].indices.last, stack[top][last].children == nil {
                    stack[top][last].children = kids
                } else {
                    stack[top].append(contentsOf: kids)
                }
                i = text.index(i, offsetBy: 5)
            } else if rest.hasPrefix("<object type=\"text/sitemap\">"), let end = rest.range(of: "</object>") {
                let obj = rest[rest.startIndex..<end.lowerBound]
                if let name = param(obj, "Name") {
                    counter += 1
                    stack[stack.count - 1].append(ClassicNode(id: "c\(counter)", name: name, file: param(obj, "Local"), children: nil))
                }
                i = end.upperBound
            } else {
                i = text.index(after: i)
            }
        }
        // The top list is nested in one <ul>.
        return stack.first ?? []
    }()

    static var pages: [ClassicNode] {
        func flat(_ n: [ClassicNode]) -> [ClassicNode] { n.flatMap { [$0] + flat($0.children ?? []) } }
        return flat(contents).filter { $0.file != nil }
    }

    /// Each page's words, for search (read once, tags stripped).
    private static var textCache: [String: String] = [:]
    static func text(of file: String) -> String {
        if let t = textCache[file] { return t }
        guard let folder, let html = try? String(contentsOf: folder.appendingPathComponent(file), encoding: .isoLatin1) else { return "" }
        let t = html.replacingOccurrences(of: "<[^>]+>", with: " ", options: .regularExpression)
            .replacingOccurrences(of: "&nbsp;", with: " ")
        textCache[file] = t
        return t
    }
}

// MARK: - Window

enum HelpItem: Hashable {
    case page(String)
    case shortcuts
    case classic(String, String)   // node id, file
}

struct HelpView: View {
    // (CL_HELP_CLASSIC=<file.htm> opens on that classic page, for development.)
    @State private var selection: HelpItem? = ProcessInfo.processInfo.environment["CL_HELP_CLASSIC"].map { .classic("dev", $0) } ?? .page("welcome")
    @State private var search = ""
    @ObservedObject private var prefs = Prefs.shared
    @ObservedObject private var keys = ShortcutStore.shared

    var body: some View {
        NavigationSplitView {
            sidebar
                .navigationSplitViewColumnWidth(min: 220, ideal: 250, max: 320)
        } detail: {
            detail
        }
        .searchable(text: $search, placement: .sidebar, prompt: "Search Help")
        .frame(minWidth: 760, minHeight: 480)
        .preferredColorScheme(prefs.dark ? .dark : .light)
        .onReceive(NotificationCenter.default.publisher(for: .clShowHelp)) { n in
            if let id = n.object as? String { selection = .page(id) }
        }
    }

    // MARK: Sidebar

    private var query: String { search.trimmingCharacters(in: .whitespaces) }

    @ViewBuilder private var sidebar: some View {
        if query.isEmpty {
            List(selection: $selection) {
                Section("Getting Started") { ForEach(HelpBook.started) { row($0) } }
                Section("Using CedarLogic") {
                    ForEach(HelpBook.using) { row($0) }
                    Label("Keyboard Shortcuts", systemImage: "keyboard").tag(HelpItem.shortcuts)
                }
                Section("Classic Help") {
                    OutlineGroup(ClassicHelp.contents, children: \.children) { node in
                        classicRow(node)
                    }
                }
            }
        } else {
            let pages = HelpBook.all.filter { HelpText.fill($0.text).localizedCaseInsensitiveContains(query) }
            let classic = ClassicHelp.pages.filter {
                $0.name.localizedCaseInsensitiveContains(query) || ClassicHelp.text(of: $0.file!).localizedCaseInsensitiveContains(query)
            }
            let shortcuts = ShortcutAction.allCases.contains { $0.name.localizedCaseInsensitiveContains(query) }
            List(selection: $selection) {
                if !pages.isEmpty || shortcuts {
                    Section("CedarLogic Help") {
                        ForEach(pages) { row($0) }
                        if shortcuts { Label("Keyboard Shortcuts", systemImage: "keyboard").tag(HelpItem.shortcuts) }
                    }
                }
                if !classic.isEmpty {
                    Section("Classic Help") { ForEach(classic) { classicRow($0) } }
                }
                if pages.isEmpty && classic.isEmpty && !shortcuts {
                    Text("Nothing matches \u{201C}\(query)\u{201D}.").foregroundStyle(.secondary)
                }
            }
        }
    }

    private func row(_ p: HelpPage) -> some View {
        Label(p.title, systemImage: p.icon).tag(HelpItem.page(p.id))
    }

    @ViewBuilder private func classicRow(_ n: ClassicNode) -> some View {
        if let file = n.file {
            Label(n.name, systemImage: "doc.text").tag(HelpItem.classic(n.id, file))
        } else {
            Label(n.name, systemImage: "folder")
        }
    }

    // MARK: Detail

    @ViewBuilder private var detail: some View {
        switch selection {
        case .page(let id):
            if let page = HelpBook.all.first(where: { $0.id == id }) { HelpPageView(page: page) }
        case .shortcuts:
            HelpShortcutsView()
        case .classic(_, let file):
            ClassicPageView(file: file)
        case nil:
            Text("Choose a topic.").foregroundStyle(.secondary)
        }
    }
}

extension Notification.Name {
    /// Open Help at a page (object: its id).
    static let clShowHelp = Notification.Name("clShowHelp")
}

private struct HelpPageView: View {
    let page: HelpPage
    @ObservedObject private var keys = ShortcutStore.shared
    @ObservedObject private var prefs = Prefs.shared

    private var accent: Color { prefs.accentColor(dark: prefs.dark) }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                HStack(spacing: 14) {
                    Image(systemName: page.icon).font(.system(size: 26, weight: .medium)).foregroundStyle(accent)
                        .frame(width: 48, height: 48)
                        .background(RoundedRectangle(cornerRadius: 12).fill(accent.opacity(0.14)))
                    VStack(alignment: .leading, spacing: 2) {
                        Text(page.title).font(.system(size: 26, weight: .bold))
                        Text(page.line).font(.system(size: 14)).foregroundStyle(.secondary)
                    }
                }
                .padding(.bottom, 6)
                ForEach(Array(page.blocks.enumerated()), id: \.offset) { _, b in block(b) }
            }
            .padding(.horizontal, 36).padding(.vertical, 30)
            .frame(maxWidth: 720, alignment: .leading)
            .frame(maxWidth: .infinity, alignment: .leading)
            .textSelection(.enabled)
        }
        .navigationTitle(page.title)
    }

    @ViewBuilder private func block(_ b: HelpPage.Block) -> some View {
        switch b {
        case .p(let s):
            Text(.init(HelpText.fill(s))).font(.system(size: 13.5)).lineSpacing(3)
                .fixedSize(horizontal: false, vertical: true)
        case .h(let s):
            Text(s).font(.system(size: 15, weight: .semibold)).padding(.top, 6)
        case .tip(let s):
            HStack(alignment: .top, spacing: 10) {
                Image(systemName: "lightbulb.fill").foregroundStyle(accent)
                Text(.init(HelpText.fill(s))).font(.system(size: 13)).fixedSize(horizontal: false, vertical: true)
            }
            .padding(12)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(RoundedRectangle(cornerRadius: 10).fill(accent.opacity(0.09)))
        case .keys(let rows):
            VStack(alignment: .leading, spacing: 0) {
                ForEach(Array(rows.enumerated()), id: \.offset) { i, r in
                    HStack(alignment: .firstTextBaseline, spacing: 14) {
                        Text(HelpText.fill(r.keys))
                            .font(.system(size: 12.5, weight: .semibold, design: .rounded))
                            .padding(.horizontal, 7).padding(.vertical, 2)
                            .background(RoundedRectangle(cornerRadius: 5).fill(Color.primary.opacity(0.07)))
                            .overlay(RoundedRectangle(cornerRadius: 5).strokeBorder(Color.primary.opacity(0.12)))
                            .frame(width: 150, alignment: .leading)
                        Text(r.what).font(.system(size: 13)).fixedSize(horizontal: false, vertical: true)
                    }
                    .padding(.vertical, 7)
                    if i < rows.count - 1 { Divider() }
                }
            }
            .padding(.horizontal, 14).padding(.vertical, 4)
            .background(RoundedRectangle(cornerRadius: 10).fill(Color.primary.opacity(0.035)))
        }
    }
}

/// Every command and its shortcut, as they're set now.
private struct HelpShortcutsView: View {
    @ObservedObject private var keys = ShortcutStore.shared

    var body: some View {
        let sections = ShortcutAction.allCases.reduce(into: [String]()) { if !$0.contains($1.section) { $0.append($1.section) } }
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                Text("Keyboard Shortcuts").font(.system(size: 26, weight: .bold))
                Text("As they're set now. Change any of them in Settings > Shortcuts.").foregroundStyle(.secondary)
                ForEach(sections, id: \.self) { sec in
                    VStack(alignment: .leading, spacing: 0) {
                        Text(sec.uppercased()).font(.system(size: 11, weight: .bold)).foregroundStyle(.secondary).padding(.bottom, 6)
                        ForEach(ShortcutAction.allCases.filter { $0.section == sec }) { a in
                            HStack {
                                Text(a.name).font(.system(size: 13))
                                Spacer()
                                Text(keys.combo(a)?.label ?? "—")
                                    .font(.system(size: 12.5, weight: .semibold, design: .rounded))
                                    .padding(.horizontal, 7).padding(.vertical, 2)
                                    .background(RoundedRectangle(cornerRadius: 5).fill(Color.primary.opacity(0.07)))
                            }
                            .padding(.vertical, 5)
                            Divider()
                        }
                    }
                }
            }
            .padding(.horizontal, 36).padding(.vertical, 30)
            .frame(maxWidth: 720, alignment: .leading)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .navigationTitle("Keyboard Shortcuts")
    }
}

/// A page of the original help, shown as it was.
private struct ClassicPageView: View {
    let file: String

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 8) {
                Image(systemName: "book.closed").foregroundStyle(.secondary)
                Text("From the original CedarLogic Help. Some of it describes the older app.")
                    .font(.system(size: 11.5)).foregroundStyle(.secondary)
                Spacer()
            }
            .padding(.horizontal, 16).padding(.vertical, 8)
            .background(Color.primary.opacity(0.04))
            Divider()
            ClassicWeb(file: file)
        }
        .navigationTitle(ClassicHelp.pages.first { $0.file == file }?.name ?? "Classic Help")
    }
}

private struct ClassicWeb: NSViewRepresentable {
    let file: String

    final class Coordinator: NSObject, WKNavigationDelegate {
        /// Links between the old pages open here; anything else, in the browser.
        func webView(_ webView: WKWebView, decidePolicyFor action: WKNavigationAction,
                     decisionHandler: @escaping @MainActor (WKNavigationActionPolicy) -> Void) {
            if let url = action.request.url, !url.isFileURL, action.navigationType == .linkActivated {
                NSWorkspace.shared.open(url)
                decisionHandler(.cancel)
            } else {
                decisionHandler(.allow)
            }
        }
    }

    func makeCoordinator() -> Coordinator { Coordinator() }

    func makeNSView(context: Context) -> WKWebView {
        let web = WKWebView()
        web.navigationDelegate = context.coordinator
        web.appearance = NSAppearance(named: .aqua)   // the old pages are black on white
        load(web)
        return web
    }

    func updateNSView(_ web: WKWebView, context: Context) {
        if web.url?.lastPathComponent != file { load(web) }
    }

    private func load(_ web: WKWebView) {
        guard let folder = MainActor.assumeIsolated({ ClassicHelp.folder }) else { return }
        web.loadFileURL(folder.appendingPathComponent(file), allowingReadAccessTo: folder)
    }
}
