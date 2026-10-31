// Checks of FeedbackServer.swift against server.py (a stand-in for the
// site's /api/feedback): the install id, the required summary, the
// per-sender cap (no retrying it), Request More, and that other 429s are
// still tried again. CL_FEEDBACK_URL says where server.py is.
//   mac/Tools/feedback-check.sh
import Foundation

var fails = 0
func ok(_ c: Bool, _ m: String) { print((c ? "ok   " : "FAIL ") + m); if !c { fails += 1 } }

let base = FeedbackServer.base
func stats() async -> (hits: [String: Int], installs: [String]) {
    let (d, _) = try! await URLSession.shared.data(from: URL(string: base + "/stats")!)
    let j = try! JSONSerialization.jsonObject(with: d) as! [String: Any]
    return (j["hits"] as? [String: Int] ?? [:], j["installs"] as? [String] ?? [])
}
func report(_ title: String) -> [String: Any] { ["title": title, "details": "", "platform": "macos", "attachments": []] }
func failure(_ f: () async throws -> Void) async -> FeedbackServer.Failure? {
    do { try await f(); return nil } catch let e as FeedbackServer.Failure { return e } catch { return FeedbackServer.Failure(message: "\(error)") }
}

// The install id: made once, kept, and remade if it isn't the right shape.
UserDefaults.standard.removeObject(forKey: "cl.installId")
let id = FeedbackServer.installId
ok(id.count == 32 && FeedbackServer.isInstallId(id) && !id.contains("-"), "the install id is 32 letters and digits (\(id))")
ok(FeedbackServer.installId == id, "...and the same the next time")
UserDefaults.standard.set("not one!", forKey: "cl.installId")
let again = FeedbackServer.installId
ok(again != "not one!" && FeedbackServer.isInstallId(again), "one that isn't the right shape is made again")
ok(!FeedbackServer.isInstallId("short") && !FeedbackServer.isInstallId(String(repeating: "a", count: 65)) && FeedbackServer.isInstallId("abc_DEF-123"),
   "isInstallId takes 8 to 64 of A-Z a-z 0-9 _ -")

// The summary.
let blank = await failure { _ = try await FeedbackServer.create(report("  ")) }
ok(blank?.code == "summary_required" && blank?.status == 400 && blank?.message.hasPrefix("Add a summary") == true,
   "an empty summary: 400 summary_required, with the server's sentence")

// Ten, then the cap: answered once, not tried again.
var sent = 0
for i in 1...10 {
    if let c = try? await FeedbackServer.create(report("Report \(i)")) { try? await FeedbackServer.complete(c); sent += 1 }
}
ok(sent == 10, "ten reports go (\(sent))")
let before = await stats().hits["/api/feedback"] ?? 0
let t0 = Date()
let cap = await failure { _ = try await FeedbackServer.create(report("Report 11")) }
let after = await stats().hits["/api/feedback"] ?? 0
ok(cap?.code == "sender_cap" && cap?.status == 429 && cap?.canRequestMore == true && cap?.retryAfter == 82800,
   "the eleventh: 429 sender_cap, canRequestMore, retryAfter")
ok(cap?.message.contains("Request more") == true, "...with the server's sentence to show (\(cap?.message ?? "-"))")
ok(after - before == 1 && Date().timeIntervalSince(t0) < 1, "...asked once, not tried again (\(after - before) requests)")

// Request More: ten more, then not again.
let n = try? await FeedbackServer.more(message: "Another bug in the clock", extra: ["platform": "macos"])
ok(n == 10, "Request more gives 10 more (\(n.map(String.init) ?? "failed"))")
let more11 = try? await FeedbackServer.create(report("Report 11, after asking"))
ok(more11 != nil, "...and the next report goes")
let twice = await failure { _ = try await FeedbackServer.more(message: "") }
ok(twice?.code == "not_capped", "asking while there's room: not_capped (\(twice?.message ?? "-"))")
for i in 12...20 { _ = try? await FeedbackServer.create(report("Report \(i)")) }
let capped2 = await failure { _ = try await FeedbackServer.create(report("Report 21")) }
ok(capped2?.code == "sender_cap" && capped2?.canRequestMore == false, "capped again: no Request more this time")
let used = await failure { _ = try await FeedbackServer.more(message: "") }
ok(used?.code == "more_used" && used?.status == 429, "asking again: more_used, not tried again")

// A 429 without a code (too many at once) is still tried again.
var r = URLRequest(url: URL(string: base + "/api/feedback/plain429")!)
r.httpMethod = "POST"; r.httpBody = #"{"n": 2}"#.data(using: .utf8)
_ = try? await URLSession.shared.data(for: r)
UserDefaults.standard.set("othermac-0001", forKey: "cl.installId")   // another sender, not capped
let h0 = await stats().hits["/api/feedback"] ?? 0
let retried = try? await FeedbackServer.create(report("After two plain 429s"))
let h1 = await stats().hits["/api/feedback"] ?? 0
ok(retried != nil && h1 - h0 == 3, "a plain 429 is tried again, three tries in all (\(h1 - h0))")

let ids = Set(await stats().installs).subtracting([""])   // ("" is the plain429 set-up)
ok(ids == [again, "othermac-0001"], "every report and Request more carried the install id (\(ids.sorted()))")

UserDefaults.standard.removeObject(forKey: "cl.installId")
print(fails == 0 ? "feedback-check: all passed" : "feedback-check: \(fails) failed")
exit(fails == 0 ? 0 : 1)
