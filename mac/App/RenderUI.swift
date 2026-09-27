// `CedarLogic --render-ui <dir> [circuit.cdl]`: draws the CedarLogic
// interface's panels (toolbar styles, tabs, side panel, quick add, the
// shortcut list, Settings pages, the memory editor, the welcome) to PNGs,
// light and dark, then quits -- a way to look at them without clicking
// through the app (the wx app's --render-ui does the same).

import AppKit
import SwiftUI

@MainActor
enum RenderUI {
    static func runIfAsked() {
        let args = CommandLine.arguments
        guard let i = args.firstIndex(of: "--render-ui"), i + 1 < args.count else { return }
        let dir = URL(fileURLWithPath: args[i + 1], isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let doc: CoreDocument
        if i + 2 < args.count, let data = try? Data(contentsOf: URL(fileURLWithPath: args[i + 2])),
           let d = try? CoreDocument(data: data) {
            doc = d
        } else {
            doc = CoreDocument()
        }
        let canvas = CanvasController()
        canvas.drivesClock = false
        canvas.attach(doc)
        let prefs = Prefs.shared
        let savedDark = prefs.dark, savedStyle = prefs.toolbarStyle, savedHidden = prefs.toolbarHidden

        func save<V: View>(_ name: String, _ view: V, width: CGFloat, height: CGFloat? = nil) {
            let r = ImageRenderer(content: view.frame(width: width, height: height)
                .environment(\.colorScheme, prefs.dark ? .dark : .light))
            r.scale = 2
            guard let img = r.cgImage else { return }
            let rep = NSBitmapImageRep(cgImage: img)
            try? rep.representation(using: .png, properties: [:])?.write(to: dir.appendingPathComponent(name + ".png"))
        }

        for dark in [false, true] {
            prefs.dark = dark
            let t = dark ? "dark" : "light"
            prefs.toolbarHidden = 0
            for style in ToolbarStyle.allCases {
                prefs.toolbarStyle = style
                save("toolbar-\(style.name.lowercased())-\(t)",
                     CLToolbar(document: doc, canvas: canvas, status: canvas.status, title: "counter", subtitle: "Page 1",
                               focusMode: .constant(false)), width: 1200, height: 52)
            }
            prefs.toolbarStyle = .seamless
            let split = SplitState()
            save("tabs-\(t)", CLTabStrip(document: doc, canvas: canvas, split: split, pane: 0, page: .constant(0), leftInset: 8),
                 width: 900, height: 36)
            save("status-\(t)", CLStatusBar(document: doc, canvas: canvas, status: canvas.status), width: 900, height: 22)
            save("sidepanel-\(t)", CLSidePanel(document: doc, canvas: canvas, page: 0), width: 220, height: 620)
            save("quickadd-\(t)", QuickAddView { _ in }, width: 520, height: 520)
            save("shortcuts-\(t)", ShortcutsSheet(canvas: canvas), width: 880, height: 620)
            save("simbar-\(t)", SimBar(document: doc, canvas: canvas, page: 0).padding(20).background(Color.black), width: 1100, height: 110)
            save("settings-general-\(t)", GeneralSettingsView().padding(28), width: 640)
            save("settings-appearance-\(t)", CLAppearanceSettings().padding(28), width: 640)
            save("settings-canvas-\(t)", CLCanvasSettings().padding(28), width: 640)
            save("settings-toolbar-\(t)", CLToolbarSettings().padding(28), width: 640)
            save("settings-shortcuts-\(t)", CLShortcutSettings().padding(28), width: 640, height: 520)
            save("welcome-\(t)", WelcomeView(), width: 780, height: 580)
        }
        prefs.dark = savedDark
        prefs.toolbarStyle = savedStyle
        prefs.toolbarHidden = savedHidden
        exit(0)
    }
}
