// Every shortcut (the ? key; the wx app's ShortcutsSheet): sections in two
// columns, keys drawn as keycaps, a search across the top. Arrow to a row and
// press Return (or click it) to do it. The keys shown are yours: change them
// in Settings > Shortcuts.

import SwiftUI

struct ShortcutsSheet: View {
    @ObservedObject var canvas: CanvasController
    @Environment(\.dismiss) private var dismiss
    @ObservedObject private var prefs = Prefs.shared
    @ObservedObject private var store = ShortcutStore.shared
    @State private var search = ""
    @State private var selected = 0
    @State private var hover: String?
    @FocusState private var focused: Bool

    struct Row: Identifiable {
        let section: String
        let keys: [String]
        let what: String
        var action: ShortcutAction? = nil
        var id: String { section + what + keys.joined() }
    }

    /// Commands from the store, plus the keys that can't change.
    private var rows: [Row] {
        var out: [Row] = []
        for a in ShortcutAction.allCases {
            out.append(Row(section: a.section, keys: store.combo(a)?.caps ?? ["none"], what: a.name, action: a))
            if a == .selectAll {
                out.append(Row(section: "Editing", keys: ["⌫"], what: "Delete the selection"))
                out.append(Row(section: "Editing", keys: ["Esc"], what: "Cancel a drag, paste or connection"))
                out.append(Row(section: "Editing", keys: ["⇧", "click"], what: "Add to or remove from the selection"))
            }
            if a == .tidy {
                out.append(Row(section: "Building", keys: ["⇧", "1-0"], what: "Jump to a gate category"))
                out.append(Row(section: "Building", keys: ["↑", "↓", "←", "→"], what: "Nudge the selection (Shift: 5 squares)"))
                out.append(Row(section: "Building", keys: ["click a pin, then another"], what: "Connect them"))
                out.append(Row(section: "Building", keys: ["C", "while dragging"], what: "Drop it and connect to pins nearby"))
                out.append(Row(section: "Building", keys: ["right-click a wire"], what: "Straighten or delete it"))
                out.append(Row(section: "Building", keys: ["double-click a gate"], what: "Change its settings"))
            }
            if a == .focusMode {
                out.append(Row(section: "Moving around", keys: ["Space"], what: "Zoom to fit (tap)"))
                out.append(Row(section: "Moving around", keys: ["Space", "drag"], what: "Move around"))
                out.append(Row(section: "Moving around", keys: ["⌘", "scroll"], what: "Zoom (or pinch)"))
                out.append(Row(section: "Moving around", keys: ["⇧", "scroll"], what: "Move sideways"))
            }
            if a == .lock {
                out.append(Row(section: "Simulation", keys: ["Space"], what: "Pause or resume (in simulation view)"))
                out.append(Row(section: "Simulation", keys: ["Esc"], what: "Leave simulation view"))
            }
            if a == .draw {
                out.append(Row(section: "Drawing and notes", keys: ["P"], what: "Pen (while drawing)"))
                out.append(Row(section: "Drawing and notes", keys: ["H"], what: "Highlighter (while drawing)"))
                out.append(Row(section: "Drawing and notes", keys: ["E"], what: "Eraser: whole marks (while drawing)"))
                out.append(Row(section: "Drawing and notes", keys: ["Esc"], what: "Done drawing"))
            }
            if a == .previousTab {
                out.append(Row(section: "Tabs and split view", keys: ["double-click a tab"], what: "Rename it"))
                out.append(Row(section: "Tabs and split view", keys: ["drag a tab aside"], what: "Split the view"))
            }
        }
        return out
    }

    private var shown: [Row] {
        let q = search.trimmingCharacters(in: .whitespaces).lowercased()
        if q.isEmpty { return rows }
        return rows.filter { ($0.what + " " + $0.section + " " + $0.keys.joined(separator: " ")).lowercased().contains(q) }
    }

    private var dark: Bool { prefs.dark }
    private var paper: Color { dark ? CLChrome.rgb(28, 31, 37) : CLChrome.rgb(250, 250, 252) }
    private var ink: Color { dark ? CLChrome.rgb(226, 230, 238) : CLChrome.rgb(30, 33, 40) }

    var body: some View {
        let list = shown
        let accent = prefs.accentColor(dark: dark)
        let sections = list.reduce(into: [String]()) { if !$0.contains($1.section) { $0.append($1.section) } }
        let half = (sections.count + 1) / 2
        VStack(spacing: 0) {
            HStack(spacing: 12) {
                Text("Keyboard Shortcuts").font(.system(size: 19, weight: .bold)).foregroundStyle(ink)
                Spacer()
                HStack(spacing: 6) {
                    Image(systemName: "magnifyingglass").foregroundStyle(ink.opacity(0.45))
                    TextField("Search", text: $search).textFieldStyle(.plain).focused($focused)
                        .onChange(of: search) { _, _ in selected = 0 }
                        .onKeyPress(.downArrow) { move(1, list.count); return .handled }
                        .onKeyPress(.upArrow) { move(-1, list.count); return .handled }
                        .onKeyPress(.return) { run(list, selected); return .handled }
                        .onKeyPress(.escape) { dismiss(); return .handled }
                }
                .padding(.horizontal, 10).padding(.vertical, 6)
                .frame(width: 240)
                .background(RoundedRectangle(cornerRadius: 8).fill(ink.opacity(0.06)))
                .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(focused ? accent.opacity(0.7) : ink.opacity(0.1), lineWidth: focused ? 2 : 1))
                // Escape clears the search first, then closes (wx).
                Button { if search.isEmpty { dismiss() } else { search = "" } } label: { Image(systemName: "xmark").font(.system(size: 12, weight: .semibold)).foregroundStyle(ink.opacity(0.5)) }
                    .buttonStyle(.plain).keyboardShortcut(.cancelAction)
            }
            .padding(.horizontal, 22).padding(.vertical, 16)
            ScrollViewReader { proxy in
                ScrollView {
                    HStack(alignment: .top, spacing: 24) {
                        column(Array(sections.prefix(half)), list, accent: accent)
                        column(Array(sections.dropFirst(half)), list, accent: accent)
                    }
                    .padding(.horizontal, 22).padding(.bottom, 18)
                    if list.isEmpty {
                        Text("No shortcuts match that.").foregroundStyle(ink.opacity(0.55)).padding(30)
                    }
                }
                .onChange(of: selected) { _, s in
                    if list.indices.contains(s) { withAnimation(.easeOut(duration: 0.18)) { proxy.scrollTo(list[s].id, anchor: .center) } }
                }
            }
        }
        .frame(width: 880, height: 620)
        .background(paper)
        .onAppear { focused = true }
    }

    private func move(_ d: Int, _ n: Int) {
        guard n > 0 else { return }
        withAnimation(.easeOut(duration: 0.12)) { selected = max(0, min(n - 1, selected + d)) }
    }

    private func run(_ list: [Row], _ i: Int) {
        guard list.indices.contains(i), let a = list[i].action else { return }
        dismiss()
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.15) { canvas.perform(a) }
    }

    private func column(_ sections: [String], _ list: [Row], accent: Color) -> some View {
        VStack(alignment: .leading, spacing: 18) {
            ForEach(sections, id: \.self) { sec in
                VStack(alignment: .leading, spacing: 2) {
                    Text(sec.uppercased()).font(.system(size: 11, weight: .bold)).foregroundStyle(accent).padding(.bottom, 6)
                    ForEach(list.indices.filter { list[$0].section == sec }, id: \.self) { i in
                        rowView(list[i], index: i, list: list, accent: accent)
                    }
                }
            }
        }
        .frame(maxWidth: .infinity, alignment: .topLeading)
    }

    private func rowView(_ r: Row, index i: Int, list: [Row], accent: Color) -> some View {
        let sel = i == selected
        return HStack(spacing: 10) {
            HStack(spacing: 4) {
                ForEach(Array(r.keys.enumerated()), id: \.offset) { KeyCap(label: $0.element) }
            }
            .frame(width: 124, alignment: .leading)
            Text(r.what).font(.system(size: 12.5)).foregroundStyle(ink)
            Spacer(minLength: 4)
            if sel && r.action != nil {
                Text("Return to do it").font(.system(size: 10.5)).foregroundStyle(ink.opacity(0.5)).transition(.opacity)
            }
        }
        .padding(.horizontal, 8).frame(height: 30)
        .background(RoundedRectangle(cornerRadius: 8).fill(sel ? accent.opacity(dark ? 0.24 : 0.14) : ink.opacity(hover == r.id ? 0.05 : 0)))
        .contentShape(Rectangle())
        .id(r.id)
        .onHover { h in withAnimation(.easeOut(duration: 0.1)) { hover = h ? r.id : (hover == r.id ? nil : hover) } }
        .onTapGesture { selected = i; run(list, i) }
        .help(r.action == nil ? "" : "Click to do it")
    }
}

/// A key drawn as a keycap; longer phrases ("click a pin") as plain text.
struct KeyCap: View {
    let label: String
    var body: some View {
        if label.count > 6 && label.contains(" ") || label == "none" || label == "click" || label == "drag" || label == "scroll" {
            Text(label).font(.system(size: 11)).foregroundStyle(.secondary)
        } else {
            Text(label)
                .font(.system(size: 11, weight: .semibold))
                .padding(.horizontal, 6)
                .frame(minWidth: 22, minHeight: 22)
                .background(RoundedRectangle(cornerRadius: 5).fill(Color.primary.opacity(0.09)))
                .overlay(RoundedRectangle(cornerRadius: 5).strokeBorder(Color.primary.opacity(0.2)))
                .shadow(color: .black.opacity(0.12), radius: 0, y: 1)
        }
    }
}
