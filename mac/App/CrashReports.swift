// Crash reports, through Sentry (the service the wx app reports to), into
// the project whose address build.sh writes into Info.plist (SentryDSN).
// A report says where the app was when it crashed: the stack, macOS, the
// version. Never a circuit, a file or a name: Sentry's automatic trail of
// clicks, network requests and failed requests is off, and a path in the
// home folder loses its user name before it goes. Testers are asked once, and
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
    private static var dsn: String {
        // CL_SENTRY_DSN: an address for a test copy of the app (its own
        // bundle id; never the app itself), such as one that can't be reached.
        if Bundle.main.bundleIdentifier != LaunchSelfTest.realBundle,
           let test = ProcessInfo.processInfo.environment["CL_SENTRY_DSN"], !test.isEmpty { return test }
        return Bundle.main.object(forInfoDictionaryKey: "SentryDSN") as? String ?? ""
    }
    static var isStarted: Bool { started }
    /// The tester said yes (nil: not asked yet).
    static var allowed: Bool? {
        get { UserDefaults.standard.object(forKey: key) == nil ? nil : UserDefaults.standard.bool(forKey: key) }
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
            // Crashes only: "app hanging" reports (any 2 s the main thread is
            // busy, a save panel open included) read as errors and aren't.
            o.enableAppHangTracking = false
            // No trail of what led up to it (clicks, windows, the addresses
            // the app talked to), and no reports of failed requests: none of
            // that is "where it went wrong", and the question promises no more.
            o.enableAutoBreadcrumbTracking = false
            o.enableNetworkBreadcrumbs = false
            o.enableNetworkTracking = false
            o.enableCaptureFailedRequests = false
            o.beforeSend = scrubUserNames
        }
        started = true
        // Development: CL_TEST_CRASH=1 crashes the app a few seconds after
        // launch, so a report can be seen arriving (on the next launch).
        if ProcessInfo.processInfo.environment["CL_TEST_CRASH"] != nil {
            DispatchQueue.main.asyncAfter(deadline: .now() + 4) { SentrySDK.crash() }
        }
        #endif
    }
    private static var started = false

    #if canImport(Sentry)
    /// /Users/<name>/ becomes /Users/<user>/ in every path the report carries
    /// (the app, its frameworks and anything in a message), wherever the app
    /// was opened from.
    nonisolated static func scrubUserNames(_ event: Event) -> Event? {
        event.message?.message = scrubbed(event.message?.message)
        for e in event.exceptions ?? [] {
            e.value = scrubbed(e.value)
            scrub(e.stacktrace)
        }
        for t in event.threads ?? [] { scrub(t.stacktrace) }
        scrub(event.stacktrace)
        for image in event.debugMeta ?? [] { image.codeFile = scrubbed(image.codeFile) }
        for crumb in event.breadcrumbs ?? [] {
            crumb.message = scrubbed(crumb.message)
            for (key, value) in scrubbed(crumb.data ?? [:]) { crumb.setData(value: value, key: key) }
        }
        event.extra = event.extra.map(scrubbed)
        event.context = event.context?.mapValues(scrubbed)
        return event
    }

    private nonisolated static func scrub(_ stack: SentryStacktrace?) {
        for f in stack?.frames ?? [] {
            f.package = scrubbed(f.package)
            f.fileName = scrubbed(f.fileName)
        }
    }

    private nonisolated static func scrubbed(_ values: [String: Any]) -> [String: Any] {
        values.mapValues { v in
            if let s = v as? String { return scrubbed(s) }
            if let d = v as? [String: Any] { return scrubbed(d) }
            if let a = v as? [Any] { return a.map { ($0 as? String).map(scrubbed) ?? $0 } }
            return v
        }
    }
    #endif

    nonisolated static func scrubbed(_ text: String) -> String {
        text.replacingOccurrences(of: "/Users/[^/]+/", with: "/Users/<user>/", options: .regularExpression)
    }
    private nonisolated static func scrubbed(_ text: String?) -> String? { text.map { scrubbed($0) } }

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
