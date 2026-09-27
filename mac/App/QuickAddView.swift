// Add a gate by name (the A key; the wx app's QuickAddDialog): type part of a
// name, arrow to the one you want, Return. Each result is drawn with the
// gate's own picture; the list glides to keep the choice in view. The gate
// then follows the pointer until a click drops it. Escape closes.

import SwiftUI

struct QuickAddView: View {
    let choose: (String) -> Void
    @Environment(\.dismiss) private var dismiss
    @ObservedObject private var prefs = Prefs.shared
    @State private var query = ""
    @State private var selected = 0
    @State private var hover: Int?
    @FocusState private var focused: Bool

    private struct Entry: Identifiable {
        let gate: GateLibrary.Gate
        let category: String
        var id: String { gate.name }
    }

    private static let all: [Entry] = GateLibrary.categories.flatMap { c in
        GateLibrary.gates(in: c).map { Entry(gate: $0, category: c.title) }
    }

    /// QuickAddDialog::fuzzyScore: a substring beats letters in order; a
    /// match at the start beats one in the middle; -1 for no match.
    static func score(_ query: String, _ target: String) -> Int {
        let q = Array(query.lowercased()), t = Array(target.lowercased())
        if q.isEmpty { return 0 }
        if let r = target.lowercased().range(of: query.lowercased()) {
            return r.lowerBound == target.lowercased().startIndex ? 100 : 80
        }
        var qi = 0, score = 0, last = -2
        for ti in 0..<t.count where qi < q.count {
            if t[ti] == q[qi] {
                score += 10
                if last == ti - 1 { score += 5 }
                if ti == 0 || " -_".contains(t[ti - 1]) { score += 5 }
                last = ti
                qi += 1
            }
        }
        return qi < q.count ? -1 : score
    }

    private var results: [Entry] {
        let q = query.trimmingCharacters(in: .whitespaces)
        if q.isEmpty { return Self.all }
        var scored: [(index: Int, entry: Entry, score: Int)] = []
        for (i, e) in Self.all.enumerated() {
            let sc = max(Self.score(q, e.gate.caption), Self.score(q, e.gate.name))
            if sc > 0 { scored.append((i, e, sc)) }
        }
        scored.sort { a, b in a.score != b.score ? a.score > b.score : a.index < b.index }
        return scored.map { $0.entry }
    }

    private var dark: Bool { prefs.dark }
    private var paper: Color { dark ? CLChrome.rgb(28, 31, 37) : CLChrome.rgb(250, 250, 252) }
    private var ink: Color { dark ? CLChrome.rgb(226, 230, 238) : CLChrome.rgb(30, 33, 40) }

    var body: some View {
        let rows = results
        let accent = prefs.accentColor(dark: dark)
        VStack(alignment: .leading, spacing: 0) {
            Text("Add a Gate").font(.system(size: 19, weight: .bold)).foregroundStyle(ink)
                .padding(.horizontal, 22).padding(.top, 22)
            Text("Type to search, then press Return. The gate follows your mouse onto the canvas.")
                .font(.system(size: 12.5)).foregroundStyle(ink.opacity(0.55))
                .padding(.horizontal, 22).padding(.top, 6)
            HStack(spacing: 7) {
                Image(systemName: "magnifyingglass").foregroundStyle(ink.opacity(0.45))
                TextField("Search gates", text: $query)
                    .textFieldStyle(.plain)
                    .font(.system(size: 14))
                    .focused($focused)
                    .onSubmit { pick(rows) }
                    .onChange(of: query) { _, _ in selected = 0 }
                    .onKeyPress(.downArrow) { move(1, rows.count); return .handled }
                    .onKeyPress(.upArrow) { move(-1, rows.count); return .handled }
                    .onKeyPress(.escape) { dismiss(); return .handled }
                if !query.isEmpty {
                    Button { query = "" } label: { Image(systemName: "xmark.circle.fill").foregroundStyle(ink.opacity(0.35)) }
                        .buttonStyle(.plain)
                }
            }
            .padding(.horizontal, 10).padding(.vertical, 7)
            .background(RoundedRectangle(cornerRadius: 8).fill(ink.opacity(0.06)))
            .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(focused ? accent.opacity(0.7) : ink.opacity(0.1), lineWidth: focused ? 2 : 1))
            .padding(.horizontal, 22).padding(.top, 14)
            if rows.isEmpty {
                Text("No gates match that.").font(.system(size: 13)).foregroundStyle(ink.opacity(0.55))
                    .frame(maxWidth: .infinity).padding(.top, 30)
                Spacer()
            } else {
                ScrollViewReader { proxy in
                    ScrollView {
                        LazyVStack(spacing: 0) {
                            ForEach(rows.indices, id: \.self) { i in
                                row(rows[i], index: i, accent: accent)
                                    .id(i)
                            }
                        }
                        .padding(.vertical, 6)
                    }
                    .onChange(of: selected) { _, s in withAnimation(.easeOut(duration: 0.18)) { proxy.scrollTo(s) } }
                }
                .padding(.horizontal, 6).padding(.top, 8)
            }
        }
        .frame(width: 520, height: 520)
        .background(paper)
        .onAppear { focused = true }
        .onExitCommand { dismiss() }
    }

    private func move(_ d: Int, _ count: Int) {
        guard count > 0 else { return }
        withAnimation(.easeOut(duration: 0.12)) { selected = max(0, min(count - 1, selected + d)) }
    }

    private func row(_ e: Entry, index i: Int, accent: Color) -> some View {
        let sel = i == selected
        return HStack(spacing: 16) {
            Image(nsImage: TileCache.image(e.gate.name, size: 40, dark: dark, scale: 2))
                .frame(width: 40, height: 40)
            VStack(alignment: .leading, spacing: 3) {
                Text(e.gate.caption.isEmpty ? e.gate.name : e.gate.caption)
                    .font(.system(size: 13, weight: .bold)).foregroundStyle(ink)
                Text(e.gate.caption != e.gate.name ? "\(e.gate.name)  ·  \(e.category)" : e.category)
                    .font(.system(size: 10.5)).foregroundStyle(ink.opacity(0.55))
            }
            Spacer()
        }
        .padding(.leading, 14)
        .frame(height: 48)
        .background(RoundedRectangle(cornerRadius: 11)
            .fill(sel ? accent.opacity(dark ? 0.26 : 0.16) : ink.opacity(hover == i ? 0.06 : 0)))
        .padding(.horizontal, 8).padding(.vertical, 3)
        .contentShape(Rectangle())
        .onHover { h in withAnimation(.easeOut(duration: 0.1)) { hover = h ? i : (hover == i ? nil : hover) } }
        .onTapGesture(count: 2) { selected = i; pick(results) }
        .onTapGesture { withAnimation(.easeOut(duration: 0.12)) { selected = i } }
    }

    private func pick(_ rows: [Entry]) {
        guard rows.indices.contains(selected) else { return }
        let name = rows[selected].gate.name
        dismiss()
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.15) { choose(name) }
    }
}
