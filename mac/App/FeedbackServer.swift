// Send Feedback's side of cedarlogic.netlify.app's /api/feedback (the
// cedarlogic-site repository, netlify/functions/feedback.mjs). Nothing here
// needs the rest of the app, so mac/Tools/feedback-check.sh builds it on its
// own and runs it against a stand-in server.
//
//   POST /api/feedback                              the report (JSON) -> id, upload token, chunk size
//   PUT  /api/feedback/upload?id&file&index&total   one piece of an attachment
//   POST /api/feedback/complete?id                  every piece is in
//   POST /api/feedback/more                         "I have another problem": 10 more for a day
//
// Every report carries this install's id (installId), which is what the
// server counts reports by: 10 in any 24 hours, then 429 with code
// "sender_cap" until one ages out or the tester asks for more (once a day).
// An answer with a "code" is the server's considered answer, so it's never
// tried again; other 429s and 5xx (and a dropped connection) are, a few times.

import Foundation

enum FeedbackServer {
    struct Created: Decodable { let id: String; let uploadToken: String; let chunkSize: Int }

    /// What went wrong, with a sentence to show as it is. `code` is the
    /// server's ("summary_required", "sender_cap", "more_used", "not_capped").
    struct Failure: Error {
        var message: String
        var code: String? = nil
        var status = 0
        /// sender_cap: a Request More button would work.
        var canRequestMore = false
        /// Seconds until one more report fits (sender_cap, more_used).
        var retryAfter: Int? = nil
    }

    static var base: String {
        ProcessInfo.processInfo.environment["CL_FEEDBACK_URL"]
            ?? Bundle.main.object(forInfoDictionaryKey: "FeedbackURL") as? String ?? "https://cedarlogic.netlify.app"
    }
    static var key: String {
        ProcessInfo.processInfo.environment["CL_FEEDBACK_KEY"] ?? Bundle.main.object(forInfoDictionaryKey: "FeedbackKey") as? String ?? ""
    }

    /// This install's id: made once (a UUID without its dashes, 32 of 0-9 A-F,
    /// which the server's 8 to 64 of A-Z a-z 0-9 _ - takes) and kept in the
    /// defaults. It names nobody; it only lets the server count one Mac's
    /// reports. One that isn't the right shape (edited by hand) is made again.
    static var installId: String {
        let d = UserDefaults.standard
        if let id = d.string(forKey: "cl.installId"), isInstallId(id) { return id }
        let id = UUID().uuidString.replacingOccurrences(of: "-", with: "")
        d.set(id, forKey: "cl.installId")
        return id
    }
    static func isInstallId(_ s: String) -> Bool {
        (8...64).contains(s.count) && s.allSatisfy { $0.isASCII && ($0.isLetter || $0.isNumber || $0 == "_" || $0 == "-") }
    }

    private static func request(_ path: String, method: String, token: String? = nil, body: Data?, type: String) -> URLRequest {
        var r = URLRequest(url: URL(string: base + path)!, timeoutInterval: 60)
        r.httpMethod = method
        r.httpBody = body
        r.setValue(type, forHTTPHeaderField: "content-type")
        r.setValue(key, forHTTPHeaderField: "x-cedarlogic-key")
        if let token { r.setValue(token, forHTTPHeaderField: "x-upload-token") }
        return r
    }

    /// The server's no, read from its JSON ({ error, code, ... }).
    static func failure(_ data: Data, status: Int, retryAfterHeader: String? = nil) -> Failure {
        let j = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any] ?? [:]
        var f = Failure(message: j["error"] as? String ?? "The feedback server said no (\(status)).")
        f.code = j["code"] as? String
        f.status = status
        f.canRequestMore = j["canRequestMore"] as? Bool ?? false
        f.retryAfter = (j["retryAfter"] as? NSNumber)?.intValue ?? retryAfterHeader.flatMap { Int($0) }
        return f
    }

    /// Tried again a few times: a flaky connection shouldn't lose a report.
    /// Not a no with a code: asking again gets the same answer.
    private static func perform(_ r: URLRequest, tries: Int = 3) async throws -> Data {
        var last = Failure(message: "It couldn't be sent.")
        for attempt in 0..<tries {
            do {
                let (data, resp) = try await URLSession.shared.data(for: r)
                let http = resp as? HTTPURLResponse
                let code = http?.statusCode ?? 0
                if (200..<300).contains(code) { return data }
                last = failure(data, status: code, retryAfterHeader: http?.value(forHTTPHeaderField: "retry-after"))
                if last.code != nil || (code < 500 && code != 429) { throw last }
            } catch let f as Failure {
                throw f
            } catch {
                last = Failure(message: "Couldn't reach the feedback server. Check the internet connection and try again.")
            }
            if attempt < tries - 1 { try await Task.sleep(nanoseconds: UInt64(pow(2, Double(attempt)) * 700_000_000)) }
        }
        throw last
    }

    /// The report itself. installId is added here, so every report has it.
    static func create(_ body: [String: Any]) async throws -> Created {
        var body = body
        body["installId"] = installId
        let data = try JSONSerialization.data(withJSONObject: body)
        let out = try await perform(request("/api/feedback", method: "POST", body: data, type: "application/json"))
        return try JSONDecoder().decode(Created.self, from: out)
    }

    static func piece(_ c: Created, file: String, index: Int, total: Int, data: Data) async throws {
        var q = URLComponents()
        q.queryItems = [.init(name: "id", value: c.id), .init(name: "file", value: file),
                        .init(name: "index", value: String(index)), .init(name: "total", value: String(total))]
        _ = try await perform(request("/api/feedback/upload?\(q.percentEncodedQuery ?? "")", method: "PUT", token: c.uploadToken,
                                      body: data, type: "application/octet-stream"))
    }

    static func complete(_ c: Created) async throws {
        _ = try await perform(request("/api/feedback/complete?id=\(c.id)", method: "POST", token: c.uploadToken, body: nil, type: "application/json"))
    }

    /// After a sender_cap: asks for more, with a line saying why if there is
    /// one (and who's asking, for the developer's list). Returns how many more
    /// can be sent now (10).
    static func more(message: String, extra: [String: Any] = [:]) async throws -> Int {
        var body = extra
        body["installId"] = installId
        let m = message.trimmingCharacters(in: .whitespacesAndNewlines)
        if !m.isEmpty { body["message"] = m }
        let data = try JSONSerialization.data(withJSONObject: body)
        let out = try await perform(request("/api/feedback/more", method: "POST", body: data, type: "application/json"))
        let j = (try? JSONSerialization.jsonObject(with: out)) as? [String: Any] ?? [:]
        return (j["granted"] as? NSNumber)?.intValue ?? 10
    }
}
