// Looks: a few ready-made presets, each of which the user can adjust. What
// they've changed is kept as overrides on top of the preset they picked, so
// "Reset" puts one setting back without losing the rest, and switching preset
// keeps nothing stale.

import SwiftUI

struct RGBA: Codable, Equatable {
    var r: Double, g: Double, b: Double, a: Double = 1

    var color: Color { Color(.sRGB, red: r, green: g, blue: b, opacity: a) }
    var cgColor: CGColor { CGColor(srgbRed: r, green: g, blue: b, alpha: a) }

    init(_ r: Double, _ g: Double, _ b: Double, _ a: Double = 1) {
        self.r = r; self.g = g; self.b = b; self.a = a
    }

    init(_ color: Color) {
        let c = NSColor(color).usingColorSpace(.sRGB) ?? .black
        self.init(c.redComponent, c.greenComponent, c.blueComponent, c.alphaComponent)
    }
}

enum GridStyle: String, Codable, CaseIterable, Identifiable {
    case lines, dots, none
    var id: String { rawValue }
    var label: String {
        switch self {
        case .lines: "Lines"
        case .dots: "Dots"
        case .none: "None"
        }
    }
}

/// Everything a look decides. A preset fills all of it; overrides replace parts.
struct Theme: Equatable {
    var canvas: RGBA
    var gridMinor: RGBA
    var gridMajor: RGBA
    var gridStyle: GridStyle
    /// Gates and wires drawn for a dark background (light outlines).
    var darkCircuit: Bool
    var accent: RGBA
}

enum ThemePreset: String, CaseIterable, Identifiable, Codable {
    case classic, studio, graphite
    var id: String { rawValue }

    var name: String {
        switch self {
        case .classic: "Classic"
        case .studio: "Studio"
        case .graphite: "Graphite"
        }
    }

    var summary: String {
        switch self {
        case .classic: "White paper and a line grid, like CedarLogic has always looked."
        case .studio: "Soft off-white canvas with a dot grid. Calm and modern."
        case .graphite: "Dark canvas for late nights; circuits drawn in light ink."
        }
    }

    var theme: Theme {
        switch self {
        case .classic:
            Theme(canvas: RGBA(1, 1, 1), gridMinor: RGBA(0.88, 0.89, 0.92), gridMajor: RGBA(0.78, 0.80, 0.85),
                  gridStyle: .lines, darkCircuit: false, accent: RGBA(0.04, 0.47, 0.98))
        case .studio:
            Theme(canvas: RGBA(0.975, 0.973, 0.965), gridMinor: RGBA(0.80, 0.80, 0.79), gridMajor: RGBA(0.62, 0.62, 0.61),
                  gridStyle: .dots, darkCircuit: false, accent: RGBA(0.35, 0.34, 0.84))
        case .graphite:
            Theme(canvas: RGBA(0.075, 0.082, 0.098), gridMinor: RGBA(0.16, 0.17, 0.20), gridMajor: RGBA(0.23, 0.25, 0.29),
                  gridStyle: .lines, darkCircuit: true, accent: RGBA(0.36, 0.64, 1.0))
        }
    }
}

/// The user's choice: a preset plus whatever they've changed on it.
struct LookSettings: Codable, Equatable {
    var preset: ThemePreset = .studio
    var canvas: RGBA?
    var gridMinor: RGBA?
    var gridMajor: RGBA?
    var gridStyle: GridStyle?
    var darkCircuit: Bool?
    var accent: RGBA?

    var theme: Theme {
        var t = preset.theme
        if let canvas { t.canvas = canvas }
        if let gridMinor { t.gridMinor = gridMinor }
        if let gridMajor { t.gridMajor = gridMajor }
        if let gridStyle { t.gridStyle = gridStyle }
        if let darkCircuit { t.darkCircuit = darkCircuit }
        if let accent { t.accent = accent }
        return t
    }

    var isCustomized: Bool {
        canvas != nil || gridMinor != nil || gridMajor != nil || gridStyle != nil || darkCircuit != nil || accent != nil
    }

    mutating func choose(_ p: ThemePreset) { self = LookSettings(preset: p) }
}

/// Kept in UserDefaults as JSON, shared by every window.
@MainActor
final class LookStore: ObservableObject {
    static let shared = LookStore()
    private let key = "look"

    @Published var settings: LookSettings {
        didSet { save() }
    }

    private init() {
        if let data = UserDefaults.standard.data(forKey: key),
           let s = try? JSONDecoder().decode(LookSettings.self, from: data) {
            settings = s
        } else {
            settings = LookSettings()
        }
    }

    private func save() {
        if let data = try? JSONEncoder().encode(settings) { UserDefaults.standard.set(data, forKey: key) }
    }
}
