// Checks of UsageReport.swift (the usage stats engine) against server.py, a
// stand-in for the site's /api/usage: snapshot first, then a delta with
// only what changed, a ping when nothing did, haveSnapshot false forcing a
// snapshot, DELETE when the switch goes off, reports kept (7 at most) when
// the server is down, 429 waiting a day, nothing sent when off or turned off
// by policy, the 16 KB cap, the 0600 file, and the leak scan. CL_USAGE_URL
// says where server.py is; the clock is the check's own.
//   mac/Tools/usage-check.sh
import Foundation

var fails = 0
func ok(_ c: Bool, _ m: String) { print((c ? "ok   " : "FAIL ") + m); if !c { fails += 1 } }

let base = ProcessInfo.processInfo.environment["CL_USAGE_URL"]!

// --live: snapshot, delta, ping and DELETE against the real server (a
// Netlify draft, say), with a throwaway id; checks its haveSnapshot answers.
if CommandLine.arguments.contains("--live") {
    let suite = "usage-check-live"
    let d = UserDefaults(suiteName: suite)!
    d.removePersistentDomain(forName: suite)
    let e = UsageEngine(defaults: d, directory: URL(fileURLWithPath: CommandLine.arguments[1]).appendingPathComponent("Live"))
    e.app = UsageApp(version: "0.4", build: 0, os: "14.6", arch: "arm64", lang: "en")
    ok(UsageEndpoint.url != nil, "live: pointed at \(UsageEndpoint.url?.absoluteString ?? "nowhere (CL_USAGE_URL must be the site or a draft)")")
    d.set(false, forKey: "cl.showGrid"); d.set(2, forKey: "cl.accent")
    await e.set(on: true)
    let id = e.id ?? ""
    e.noteSession(); e.count("save"); e.addTime(active: 60, focus: 0, simView: 0)
    await e.sendIfDue(force: true)
    ok(e.lastSentKind == "snapshot" && e.lastHaveSnapshot == true, "live snapshot: taken, haveSnapshot true (\(e.log))")
    d.set(60, forKey: "cl.gateSize"); e.count("tidy")
    await e.sendIfDue(force: true)
    ok(e.lastSentKind == "delta" && e.lastHaveSnapshot == true, "live delta: taken, haveSnapshot true (\(e.log.suffix(1)))")
    await e.sendIfDue(force: true)
    ok(e.lastSentKind == "ping" && e.lastHaveSnapshot == true, "live ping: taken, its hash matches what the server holds (\(e.log.suffix(1)))")
    await e.set(on: false)
    ok(e.state.pendingDeletes.isEmpty && e.log.last == "deleted", "live DELETE ?id=: 200 (\(e.log.suffix(1)))")
    // The id is gone there: a ping for it finds no snapshot.
    var r = URLRequest(url: UsageEndpoint.url!)
    r.httpMethod = "POST"
    r.setValue("application/json", forHTTPHeaderField: "content-type")
    r.setValue("CedarLogic/0.4 (mac)", forHTTPHeaderField: "user-agent")
    let ping = UsagePending(kind: "ping", from: UsageEngine.date(Date()), to: UsageEngine.date(Date()), settings: nil, full: [:],
                            hash: UsageSettings.hash([:]), counters: nil, syncOn: false)
    r.httpBody = e.encoded(ping, id: id, sentAt: Date())
    let (out, resp) = (try? await URLSession.shared.data(for: r)) ?? (Data(), nil)
    let j = (try? JSONSerialization.jsonObject(with: out)) as? [String: Any] ?? [:]
    ok((resp as? HTTPURLResponse)?.statusCode == 200 && j["haveSnapshot"] as? Bool == false, "after DELETE, a ping for the id: haveSnapshot false (\(j))")
    var del = URLComponents(url: UsageEndpoint.url!, resolvingAgainstBaseURL: false)!
    del.queryItems = [URLQueryItem(name: "id", value: id)]
    var dr = URLRequest(url: del.url!); dr.httpMethod = "DELETE"
    let code = ((try? await URLSession.shared.data(for: dr))?.1 as? HTTPURLResponse)?.statusCode
    ok(code == 200, "DELETE again: 200 (it's idempotent), and the id is gone (\(code ?? 0))")
    d.removePersistentDomain(forName: suite)
    print(fails == 0 ? "usage-check --live: all passed" : "usage-check --live: \(fails) failed")
    exit(fails == 0 ? 0 : 1)
}
func get(_ path: String) async -> Any {
    let (d, _) = try! await URLSession.shared.data(from: URL(string: base + path)!)
    return try! JSONSerialization.jsonObject(with: d)
}
func control(_ body: [String: Any]) async {
    var r = URLRequest(url: URL(string: base + "/control")!)
    r.httpMethod = "POST"; r.httpBody = try! JSONSerialization.data(withJSONObject: body)
    _ = try? await URLSession.shared.data(for: r)
}
func requests() async -> [[String: Any]] { await get("/log") as? [[String: Any]] ?? [] }
func posts() async -> [[String: Any]] { await requests().filter { $0["method"] as? String == "POST" } }
func lastBody() async -> [String: Any] { (await posts()).last?["body"] as? [String: Any] ?? [:] }

let suite = "usage-check"
let d = UserDefaults(suiteName: suite)!
d.removePersistentDomain(forName: suite)
let dir = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true).appendingPathComponent("Usage", isDirectory: true)
var clock = Date(timeIntervalSince1970: 1_793_000_000)   // 2026-10-26
let engine = UsageEngine(defaults: d, directory: dir)
engine.now = { clock }
var syncOn = false
engine.syncOn = { syncOn }
func advance(hours: Double) { clock = clock.addingTimeInterval(hours * 3600) }

// Where it goes.
ok(UsageEndpoint.url?.absoluteString == base + "/api/usage", "CL_USAGE_URL points it at the stand-in on this Mac (\(base))")
setenv("CL_USAGE_URL", "https://usage-stats-test--cedarlogic.netlify.app/api/usage", 1)
ok(UsageEndpoint.url?.absoluteString == "https://usage-stats-test--cedarlogic.netlify.app/api/usage", "...or at one of the site's drafts, over HTTPS")
setenv("CL_USAGE_URL", "http://usage-stats-test--cedarlogic.netlify.app", 1)
ok(UsageEndpoint.url == nil, "...never over plain HTTP")
setenv("CL_USAGE_URL", "https://example.com", 1)
ok(UsageEndpoint.url == nil, "...but never at another host")
setenv("CL_USAGE_URL", "", 1)
ok(UsageEndpoint.url == nil, "a command-line check without one sends nowhere")
setenv("CL_USAGE_URL", base, 1)

// Off: nothing at all.
await control(["reset": true])
engine.count("save"); engine.noteSession()
await engine.sendIfDue(force: true)
ok(await requests().isEmpty && engine.counters.isZero && engine.id == nil, "off: nothing counted, nothing sent, no id")

// A few settings changed, and things that must never go.
d.set(false, forKey: "cl.showGrid")
d.set(2, forKey: "cl.accent")
d.set(0.25, forKey: "cl.wireDotSize")
d.set("Secret Student Name", forKey: "cl.studentName")
d.set(NSHomeDirectory() + "/Templates/My Lab", forKey: "cl.newTemplate")
d.set("beta", forKey: "testingGroup")
d.set(false, forKey: "cl.showGateNames")

// On: a fresh id, then a snapshot.
await engine.set(on: true)
let id1 = engine.id ?? ""
ok(UsageEngine.isUsageId(id1) && id1.count == 32, "switched on: a random id of 32 letters and digits (\(id1))")
engine.noteSession(); engine.count("save"); engine.count("save"); engine.count("tidy")
engine.count("not an id!"); engine.addTime(active: 300, focus: 60, simView: 30)
syncOn = true
await engine.sendIfDue()
var p = await posts()
var b = await lastBody()
ok(p.count == 1 && b["kind"] as? String == "snapshot", "the first report is a snapshot (\(p.count) sent)")
ok((p.last?["problems"] as? [Any])?.isEmpty == true && p.last?["answer"] as? Int == 200, "...in the contract's shape (\(p.last?["problems"] ?? "-"))")
ok(p.last?["hashOk"] as? Bool == true, "...its settingsHash is SHA-256 of the canonical JSON (the server makes the same one)")
let s1 = b["settings"] as? [String: Any] ?? [:]
ok(Set(s1.keys) == ["showGrid", "accent", "wireDotSize", "newTemplate", "testingGroup", "gateLabels"], "...with every setting that isn't its default (\(s1.keys.sorted()))")
ok(s1["newTemplate"] as? String == "yours" && (s1["wireDotSize"] as? Double) == 0.25, "...a template of yours is just \"yours\"; numbers as they are")
let c1 = b["counters"] as? [String: Any] ?? [:]
ok(c1["sessions"] as? Int == 1 && c1["activeSeconds"] as? Int == 300 && c1["focusSeconds"] as? Int == 60 && c1["simViewSeconds"] as? Int == 30
   && c1["syncOn"] as? Bool == true, "...and the counters (\(c1))")
ok((c1["actions"] as? [String: Int]) == ["save": 2, "tidy": 1], "...actions counted by id, nothing that isn't one")
ok((b["sentAt"] as? String)?.hasSuffix("Z") == true && (b["sentAt"] as? String)?.count == 17, "sentAt is UTC to the minute (\(b["sentAt"] ?? "-"))")
ok((p.last?["ua"] as? String)?.hasPrefix("CedarLogic/") == true && (p.last?["ua"] as? String)?.hasSuffix("(mac)") == true,
   "the User-Agent is CedarLogic/<version> (mac) (\(p.last?["ua"] ?? "-"))")
ok(engine.counters.isZero && engine.lastSentKind == "snapshot", "the counters start again once it's confirmed")

// The file: 0600, in a 0700 folder.
engine.count("save"); engine.save()
let attrs = try? FileManager.default.attributesOfItem(atPath: dir.appendingPathComponent("usage.json").path)
let dattrs = try? FileManager.default.attributesOfItem(atPath: dir.path)
ok((attrs?[.posixPermissions] as? Int) == 0o600 && (dattrs?[.posixPermissions] as? Int) == 0o700, "the local file is 0600, its folder 0700")
ok(d.object(forKey: "cl.usage") == nil && d.dictionaryRepresentation().keys.filter { $0.hasPrefix("cl.usage") }.sorted() == ["cl.usageId", "cl.usageStats"],
   "nothing counted is in the defaults (only the switch and the id)")
let reloaded = UsageEngine(defaults: d, directory: dir)
ok(reloaded.counters.actions["save"] == 1 && reloaded.lastSentKind == "snapshot", "...and it reads back")

// Not due again within the day.
advance(hours: 3)
await engine.sendIfDue()
ok(await posts().count == 1, "not again within 24 hours")

// A day later, one setting changed: a delta with only that.
advance(hours: 22)
d.set(60, forKey: "cl.gateSize")
engine.count("zoomIn")
await engine.sendIfDue()
b = await lastBody()
ok(b["kind"] as? String == "delta" && (b["settings"] as? [String: Any]).map { Set($0.keys) } == ["gateSize"],
   "a day on, a delta with only the setting that changed (\(b["settings"] ?? "-"))")
ok(((b["counters"] as? [String: Any])?["actions"] as? [String: Int]) == ["save": 1, "zoomIn": 1], "...and the counters since the last one")

// Nothing changed, nothing counted: a ping.
advance(hours: 25)
await engine.sendIfDue()
b = await lastBody()
ok(b["kind"] as? String == "ping" && b["settings"] == nil && b["counters"] == nil && b["settingsHash"] != nil,
   "nothing new: a ping, with no settings or counters, just the hash")

// A setting back to its default: a delta saying so.
advance(hours: 25)
d.removeObject(forKey: "cl.showGrid")
await engine.sendIfDue()
b = await lastBody()
ok(b["kind"] as? String == "delta" && (b["settings"] as? [String: Any])?["showGrid"] as? Bool == true && (b["settings"] as? [String: Any])?.count == 1,
   "a setting put back to its default goes as a delta with the default (\(b["settings"] ?? "-"))")
let held = (await get("/store") as? [String: Any])?[id1] as? [String: Any]
ok(((held?["settings"] as? [String: Any])?["gateSize"] as? Int) == 60, "the server has the snapshot with the deltas applied")

// haveSnapshot false: a snapshot follows.
await control(["noSnapshot": 1])
advance(hours: 25)
engine.count("save")
let before = await posts().count
await engine.sendIfDue()
p = await posts()
let kinds = p.suffix(p.count - before).compactMap { ($0["body"] as? [String: Any])?["kind"] as? String }
ok(kinds == ["delta", "snapshot"], "haveSnapshot false: a snapshot goes straight after (\(kinds))")
ok(((p.last?["body"] as? [String: Any])?["settings"] as? [String: Any]).map { Set($0.keys) } == ["accent", "wireDotSize", "newTemplate", "testingGroup", "gateSize", "gateLabels"],
   "...with every non-default setting")

// The server down: reports kept, 7 at most, then all sent.
await control(["fail": 1000, "status": 503])
for _ in 0..<9 { advance(hours: 25); engine.count("save"); await engine.sendIfDue() }
ok(engine.keptCount == 7, "the server down for 9 days: 7 reports kept (\(engine.keptCount))")
await control(["fail": 0])
let before2 = await posts().count
advance(hours: 1)
await engine.sendIfDue()
p = await posts()
ok(engine.keptCount == 0 && p.count - before2 == 7, "back up: the 7 kept go at the next chance, oldest first (\(p.count - before2))")
ok(p.suffix(7).allSatisfy { $0["answer"] as? Int == 200 }, "...and each is taken")

// 429: wait a day.
await control(["tooMany": 1])
advance(hours: 25); engine.count("save")
await engine.sendIfDue()
let after429 = await posts().count
advance(hours: 6)
await engine.sendIfDue()
ok(await posts().count == after429 && engine.keptCount == 1, "429: kept, and not tried again for 24 hours")
advance(hours: 19)
await engine.sendIfDue()
ok(await posts().count == after429 + 2 && engine.keptCount == 0, "...then it goes (with the day's new one after it)")

// Policy: DisableUsageStats.
d.set(true, forKey: UsageEngine.policyKey)
advance(hours: 25)
engine.count("save")
let beforePolicy = await requests().count
await engine.sendIfDue(force: true)
ok(await requests().count == beforePolicy && engine.counters.isZero, "DisableUsageStats: nothing counted, nothing sent")
d.removeObject(forKey: UsageEngine.policyKey)

// The 16 KB cap and 200 actions.
for i in 0..<230 { engine.count(String(format: "action%034d", i)) }
ok(engine.counters.actions.count == 200, "at most 200 actions are counted (\(engine.counters.actions.count))")
let big = engine.encoded(engine.nextReport(), id: id1, sentAt: clock)
ok(big.count <= 16 * 1024, "a report is 16 KB at most (\(big.count) bytes)")
advance(hours: 25)
await engine.sendIfDue()
let bigPost = await posts().last
ok((bigPost?["problems"] as? [Any])?.isEmpty == true && (bigPost?["bytes"] as? Int ?? 99999) <= 16384,
   "...and the server takes it")

// The leak scan, on real reports.
let fakeSync = "K7QX-2MPL-9WZD-4HRT", fakeClass = "CLASS-ZQ81-PX44", fakeKey = "TK-7731-QQZP-ABCD"
d.set(fakeSync, forKey: "cl.sync.code"); d.set([fakeClass], forKey: "cl.classroom.codes"); d.set(fakeKey, forKey: "cl.teacherKey")
d.set(["/Users/\(NSUserName())/Documents/Half Adder Lab.cdl"], forKey: "NSRecentDocuments")
d.set(NSFullUserName(), forKey: "cl.studentName")
engine.count("save")
let texts = [engine.preview(), String(decoding: engine.encoded(engine.nextReport(), id: id1, sentAt: clock), as: UTF8.self)]
    + (await posts()).compactMap { $0["raw"] as? String }
let needles = UsageLeakScan.machineNeedles() + [fakeSync, fakeClass, fakeKey, "Half Adder Lab", "Secret Student Name", "Templates/My Lab"]
let leaks = texts.flatMap { UsageLeakScan.leaks(in: $0, needles: needles) }
ok(leaks.isEmpty, "the leak scan finds nothing in \(texts.count) reports (home, user, host, recent files, codes): \(Set(leaks).sorted())")
ok(!UsageLeakScan.leaks(in: "{\"x\":\"\(NSHomeDirectory())\"}", needles: needles).isEmpty, "...and does find one when it's there")

// Delete what's been sent: the server forgets it, a new id follows.
let beforeDelete = await requests().count
let deleted = await engine.deleteSent()
var r = await requests()
ok(deleted && r.count == beforeDelete + 1 && r.last?["method"] as? String == "DELETE" && r.last?["id"] as? String == id1,
   "Delete what's been sent: DELETE ?id=<the id>")
ok((await get("/store") as? [String: Any])?[id1] == nil, "...the server has nothing for it")
let id2 = engine.id ?? ""
ok(UsageEngine.isUsageId(id2) && id2 != id1, "...and a new id, still on")
advance(hours: 1)
await engine.sendIfDue()
b = await lastBody()
ok(b["kind"] as? String == "snapshot" && b["id"] as? String == id2, "...whose first report is a snapshot")

// Off: DELETE, the id and the counts gone.
engine.count("save")
await engine.set(on: false)
r = await requests()
ok(r.last?["method"] as? String == "DELETE" && r.last?["id"] as? String == id2 && engine.id == nil && d.string(forKey: UsageEngine.idKey) == nil,
   "switched off: the server is told to delete it, and the id is forgotten")
ok(engine.counters.isZero && engine.keptCount == 0, "...and what was counted is gone too")
let beforeOff = await requests().count
advance(hours: 48); engine.count("save")
await engine.sendIfDue(force: true)
ok(await requests().count == beforeOff, "off: nothing sent")

// On again: a fresh id. Off with the server unreachable: the DELETE waits.
await engine.set(on: true)
let id3 = engine.id ?? ""
ok(id3 != id1 && id3 != id2 && UsageEngine.isUsageId(id3), "on again: a fresh id")
engine.endpoint = { URL(string: "http://127.0.0.1:9") }
await engine.set(on: false)
ok(engine.state.pendingDeletes == [id3], "off with no connection: the DELETE is kept")
engine.endpoint = { UsageEndpoint.url }
await engine.sendIfDue()
r = await requests()
ok(r.last?["method"] as? String == "DELETE" && r.last?["id"] as? String == id3 && engine.state.pendingDeletes.isEmpty, "...and goes at the next chance")

// Every report the server saw fitted the contract.
let bad = await posts().filter { !(($0["problems"] as? [Any])?.isEmpty ?? true) }
ok(bad.isEmpty, "every report fitted the contract (\(bad.count) didn't)")

// Samples for the record.
if let out = ProcessInfo.processInfo.environment["USAGE_SAMPLES"] {
    let all = await posts()
    var seen = Set<String>(), lines: [String] = []
    for e in all {
        guard let k = (e["body"] as? [String: Any])?["kind"] as? String, !seen.contains(k), let raw = e["raw"] as? String,
              let j = try? JSONSerialization.jsonObject(with: Data(raw.utf8)),
              let pretty = try? JSONSerialization.data(withJSONObject: j, options: [.prettyPrinted, .sortedKeys]) else { continue }
        seen.insert(k); lines.append("// \(k)\n" + String(decoding: pretty, as: UTF8.self))
    }
    try? lines.joined(separator: "\n\n").write(toFile: out, atomically: true, encoding: .utf8)
}

d.removePersistentDomain(forName: suite)
print(fails == 0 ? "usage-check: all passed" : "usage-check: \(fails) failed")
exit(fails == 0 ? 0 : 1)
