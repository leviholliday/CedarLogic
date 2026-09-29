// Crash reports, through Sentry (the service the wx app reports to), into
// the project whose address build.sh writes into Info.plist (SentryDSN).
// A report says where the app was when it crashed: the stack, macOS, the
// version. Never a circuit, a file or a name. Testers are asked once, and
// Settings > General turns it on or off; a lab can switch it off for
// everyone with the wx app's policy key, DisableCrashReporting.

import AppKit
#if canImport(Sentry)
import Sentry
#endif

@MainActor
enum CrashReports {
    static let key = "cl.crashReports"

    /// This build knows where to send them.
    static var available: Bool {
        #if canImport(Sentry)
        return !dsn.isEmpty && !UserDefaults.standard.bool(forKey: "DisableCrashReporting")
        #else
        return false
        #endif
    }
    private static var dsn: String { Bundle.main.object(forInfoDictionaryKey: "SentryDSN") as? String ?? "" }
    /// The tester said yes (nil: not asked yet).
    static var allowed: Bool? {
        get { UserDefaults.standard.object(forKey: key) as? Bool }
        set { UserDefaults.standard.set(newValue, forKey: key) }
    }

    /// At launch, before anything else can go wrong.
    static func start() {
        guard available, allowed == true, !started else { return }
        #if canImport(Sentry)
        let info = Bundle.main.infoDictionary ?? [:]
        let version = info["CFBundleShortVersionString"] as? String ?? "?"
        let build = info["CFBundleVersion"] as? String ?? "?"
        SentrySDK.start { o in
            o.dsn = dsn
            o.releaseName = "cedarlogic-native@\(version)+\(build)"
            o.environment = UserDefaults.standard.string(forKey: "testingGroup") ?? "normal"
            o.sendDefaultPii = false
            o.tracesSampleRate = 0
            o.enableAutoPerformanceTracing = false
        }
        started = true
        #endif
    }
    private static var started = false

    /// Settings: on or off. Off takes effect from the next launch, so a
    /// report already being written still goes.
    static func set(_ on: Bool) {
        allowed = on
        if on { start() }
    }

    /// Once, the first time the app opens with somewhere to send them:
    /// the question, over the circuit window.
    static func askOnce() {
        guard available, allowed == nil, !CommandLine.arguments.contains("--render-ui"),
              ProcessInfo.processInfo.environment["CL_BAR_TEST"] == nil,
              ProcessInfo.processInfo.environment["CL_SNAPSHOT"] == nil,
              let w = NSApp.windows.first(where: { $0.isVisible && NSDocumentController.shared.document(for: $0) != nil }) else { return }
        let a = NSAlert()
        a.messageText = "Send crash reports?"
        a.informativeText = "If CedarLogic crashes, it can send a report of where it went wrong, to help get it fixed. A report never includes your circuits, files or name. You can change this in Settings > General."
        a.addButton(withTitle: "Send Reports")
        a.addButton(withTitle: "Don't Send")
        a.beginSheetModal(for: w) { r in
            MainActor.assumeIsolated { set(r == .alertFirstButtonReturn) }
        }
    }
}
