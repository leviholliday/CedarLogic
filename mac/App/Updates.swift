// Updates for CedarLogic on the Mac, through Sparkle (the same updater the wx app
// uses). Testers pick a group in Settings > General: normal testers follow
// appcast-native.xml, beta testers appcast-native-beta.xml, which also carries
// every normal release. scripts/publish-update.sh --native writes both.

import AppKit
import Sparkle

enum TestingGroup: String, CaseIterable, Identifiable {
    case normal, beta
    var id: String { rawValue }
    var name: String { self == .beta ? "Beta tester" : "Normal tester" }
    var feed: String {
        let base = "https://raw.githubusercontent.com/leviholliday/CedarLogic-Releases/main/"
        return base + (self == .beta ? "appcast-native-beta.xml" : "appcast-native.xml")
    }
}

@MainActor
final class Updates: NSObject, SPUUpdaterDelegate {
    static let shared = Updates()
    private var controller: SPUStandardUpdaterController?

    /// Sparkle refuses to start without the public key, and can't replace an
    /// app running from a disk image; in either case the menu item explains.
    private var usable: Bool {
        let key = Bundle.main.object(forInfoDictionaryKey: "SUPublicEDKey") as? String ?? ""
        return !key.isEmpty && FileManager.default.isWritableFile(atPath: Bundle.main.bundlePath)
    }

    func start() {
        guard controller == nil, usable else { return }
        controller = SPUStandardUpdaterController(startingUpdater: true, updaterDelegate: self,
                                                  userDriverDelegate: nil)
    }

    func checkNow() {
        if let c = controller { c.checkForUpdates(nil); return }
        let a = NSAlert()
        a.messageText = "This copy can't update itself."
        a.informativeText = "Move CedarLogic to your Applications folder and open it from there."
        a.runModal()
    }

    /// The testing group changed: Sparkle asks for the feed on every check, so
    /// just look again now.
    func groupChanged() {
        guard let u = controller?.updater, !u.sessionInProgress else { return }
        u.checkForUpdatesInBackground()
    }

    nonisolated func feedURLString(for updater: SPUUpdater) -> String? {
        let raw = UserDefaults.standard.string(forKey: "testingGroup") ?? ""
        return (TestingGroup(rawValue: raw) ?? .normal).feed
    }
}
