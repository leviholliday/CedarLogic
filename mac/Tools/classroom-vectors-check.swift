// The classroom test vectors (docs/CLASSROOM.md 7.1, tests/classroom/vectors.json)
// checked with what the Mac app's hooks use and nothing else: CryptoKit
// (SHA-256, HMAC, HKDF, AES-GCM, P-256 key agreement), CommonCrypto (PBKDF2
// for the join code) and Compression (raw deflate). It shares no code with
// the website's generator (WebCrypto) or its checker (node:crypto), so the
// three agree only if the document's bytes are right. The core's C++ later
// reproduces the same values through the same hooks.
//
//   mac/Tools/classroom-vectors-check.sh [path/to/vectors.json]
//
// Every line is PASS or FAIL; the exit code is the number of failures.

import Foundation
import CryptoKit
import CommonCrypto
import Compression

// ---- bytes ------------------------------------------------------------------

func hex(_ d: Data) -> String { d.map { String(format: "%02x", $0) }.joined() }
func unhex(_ s: String) -> Data {
    var out = Data()
    var i = s.startIndex
    while i < s.endIndex, let j = s.index(i, offsetBy: 2, limitedBy: s.endIndex) {
        out.append(UInt8(s[i..<j], radix: 16) ?? 0)
        i = j
    }
    return out
}
func b64u(_ d: Data) -> String {
    d.base64EncodedString().replacingOccurrences(of: "+", with: "-").replacingOccurrences(of: "/", with: "_").replacingOccurrences(of: "=", with: "")
}
func unb64u(_ s: String) -> Data {
    var t = s.replacingOccurrences(of: "-", with: "+").replacingOccurrences(of: "_", with: "/")
    while t.count % 4 != 0 { t += "=" }
    return Data(base64Encoded: t) ?? Data()
}
func sha256(_ d: Data) -> Data { Data(SHA256.hash(data: d)) }
func ascii(_ s: String) -> Data { Data(s.utf8) }

// ---- the vectors file ----------------------------------------------------------

let args = CommandLine.arguments
let path = args.count > 1 ? args[1] : "tests/classroom/vectors.json"
guard let raw = FileManager.default.contents(atPath: path),
      let V = try? JSONSerialization.jsonObject(with: raw) as? [String: Any] else {
    print("can't read \(path)")
    exit(2)
}
func s(_ o: Any?, _ k: String) -> String { (o as? [String: Any])?[k] as? String ?? "" }
func i(_ o: Any?, _ k: String) -> Int { ((o as? [String: Any])?[k] as? NSNumber)?.intValue ?? 0 }
func b(_ o: Any?, _ k: String) -> Bool { ((o as? [String: Any])?[k] as? NSNumber)?.boolValue ?? false }
func d(_ o: Any?, _ k: String) -> [String: Any] { (o as? [String: Any])?[k] as? [String: Any] ?? [:] }
func a(_ o: Any?, _ k: String) -> [[String: Any]] { (o as? [String: Any])?[k] as? [[String: Any]] ?? [] }

var passes = 0, fails = 0
struct Fail: Error { let what: String }
func check(_ name: String, _ body: () throws -> Void) {
    do { try body(); passes += 1 }
    catch let f as Fail { fails += 1; print("FAIL \(name): \(f.what)") }
    catch { fails += 1; print("FAIL \(name): \(error)") }
}
func eq<T: Equatable>(_ x: T, _ y: T, _ what: String) throws { if x != y { throw Fail(what: "\(what): \(x) != \(y)") } }

// ---- codes (1.2) -----------------------------------------------------------------

let ALPHA = Array("0123456789ABCDEFGHJKMNPQRSTVWXYZ")
func checksum12(_ secret: Data) -> Int { let h = sha256(secret); return (Int(h[0]) << 4) | (Int(h[1]) >> 4) }
/// secret ‖ checksum12 << 4, read five bits at a time (as the C++ core does it).
func encodeCode(_ secret: Data) -> String {
    let ck = checksum12(secret)
    var packed = [UInt8](secret) + [UInt8(ck >> 4), UInt8((ck & 15) << 4)]
    packed.append(0)
    let symbols = (secret.count * 8 + 12) / 5
    var out = ""
    for k in 0..<symbols {
        let bit = k * 5
        let v = ((Int(packed[bit >> 3]) << 8) | Int(packed[(bit >> 3) + 1])) >> (16 - 5 - (bit & 7)) & 31
        out.append(ALPHA[v])
    }
    return out
}
enum CodeError: Error { case symbol, length, checksum, kind }
func decodeCode(_ code: String, bytes: Int) throws -> Data {
    var bits: [Int] = []
    for c in code {
        guard let v = ALPHA.firstIndex(of: c) else { throw CodeError.symbol }
        for k in stride(from: 4, through: 0, by: -1) { bits.append((v >> k) & 1) }
    }
    guard bits.count == bytes * 8 + 12 else { throw CodeError.length }
    var secret = Data(count: bytes)
    for k in 0..<(bytes * 8) where bits[k] == 1 { secret[k >> 3] |= UInt8(128 >> (k & 7)) }
    var ck = 0
    for k in (bytes * 8)..<bits.count { ck = (ck << 1) | bits[k] }
    guard ck == checksum12(secret) else { throw CodeError.checksum }
    return secret
}
let KIND_KEY = ["teacher": "t", "join": "j", "move": "m"]
let KIND_LEN = ["teacher": 28, "join": 12, "move": 28]
let SKIP: Set<Unicode.Scalar> = ["-", " ", "\t", "\r", "\n", "\u{00A0}"]
let linkRe = try! NSRegularExpression(pattern: "#([tjmks])=([0-9A-Za-z \\-]*)")
let queryRe = try! NSRegularExpression(pattern: "[?&]([tjmks])=([0-9A-Za-z \\-]*)")
/// What a code field makes of typed, pasted or scanned text (1.2): the canonical code, or "error:<why>".
func normalize(_ input: String, kind: String) -> String {
    var text = input
    let ns = text as NSString
    var m = linkRe.firstMatch(in: text, range: NSRange(location: 0, length: ns.length))
    if m == nil, text.count >= 11, text.prefix(11).lowercased() == "cedarlogic:" {
        m = queryRe.firstMatch(in: text, range: NSRange(location: 0, length: ns.length))
    }
    if let m {
        if ns.substring(with: m.range(at: 1)) != KIND_KEY[kind] { return "error:kind" }
        text = ns.substring(with: m.range(at: 2))
    }
    var out = ""
    for u in text.unicodeScalars {
        if SKIP.contains(u) { continue }
        var c = u
        if u.value >= 97 && u.value <= 122 { c = Unicode.Scalar(u.value - 32)! }   // ASCII a-z only
        switch c { case "O": c = "0"; case "I", "L": c = "1"; default: break }
        guard c.isASCII, ALPHA.contains(Character(c)) else { return "error:symbol" }
        out.unicodeScalars.append(c)
    }
    if out.count != KIND_LEN[kind] { return "error:length" }
    if (try? decodeCode(out, bytes: kind == "join" ? 6 : 16)) == nil { return "error:checksum" }
    return out
}
for kind in ["teacher", "join", "move"] {
    for c in a(d(V, "codes"), kind) {
        check("code \(kind) \(s(c, "label"))") {
            let secret = unhex(s(c, "secretHex"))
            try eq(String(format: "%03x", checksum12(secret)), s(c, "checksum12"), "checksum")
            try eq(encodeCode(secret), s(c, "code"), "code")
            try eq(hex(try decodeCode(s(c, "code"), bytes: secret.count)), s(c, "secretHex"), "decode")
            let code = Array(s(c, "code"))
            let grouped = stride(from: 0, to: code.count, by: 4).map { String(code[$0..<min($0 + 4, code.count)]) }.joined(separator: "-")
            try eq(grouped, s(c, "grouped"), "grouped")
            try eq("\(s(V, "webBase"))#\(KIND_KEY[kind]!)=\(s(c, "code"))", s(c, "webLink"), "web link")
            try eq("\(s(V, "appBase"))#\(KIND_KEY[kind]!)=\(s(c, "code"))", s(c, "appLink"), "app link")
        }
    }
}
for p in a(V, "parse") {
    check("parse \(s(p, "kind")): \(s(p, "label"))") { try eq(normalize(s(p, "input"), kind: s(p, "kind")), s(p, "expect"), "result") }
}

// ---- keys (1.3) ------------------------------------------------------------------

let SALT = ascii(s(V, "salt"))
func hkdf(_ ikm: Data, _ info: Data, _ len: Int) -> Data {
    HKDF<SHA256>.deriveKey(inputKeyMaterial: SymmetricKey(data: ikm), salt: SALT, info: info, outputByteCount: len)
        .withUnsafeBytes { Data($0) }
}
func prk(_ ikm: Data) -> String { hex(Data(HMAC<SHA256>.authenticationCode(for: ikm, using: SymmetricKey(data: SALT)))) }
func shaHex(_ token: String) -> String { hex(sha256(ascii(token))) }
/// The join code's stretching: PBKDF2-HMAC-SHA256 through CommonCrypto (CryptoKit has no PBKDF2).
func stretch(_ secret: Data) -> Data {
    let salt = [UInt8](ascii(s(V, "joinSalt")))
    var out = [UInt8](repeating: 0, count: 32)
    let rc = secret.withUnsafeBytes { pw -> Int32 in
        CCKeyDerivationPBKDF(CCPBKDFAlgorithm(kCCPBKDF2), pw.baseAddress!.assumingMemoryBound(to: Int8.self), pw.count,
                             salt, salt.count, CCPseudoRandomAlgorithm(kCCPRFHmacAlgSHA256), UInt32(i(V, "joinIterations")), &out, 32)
    }
    return rc == kCCSuccess ? Data(out) : Data()
}
for k in a(d(V, "keys"), "teacher") {
    check("teacher keys \(s(k, "label"))") {
        let sec = unhex(s(k, "secretHex"))
        try eq(prk(sec), s(k, "prkHex"), "PRK")
        try eq(hex(hkdf(sec, ascii("class-id"), 16)), s(k, "classId"), "classId")
        try eq(b64u(hkdf(sec, ascii("teacher-token"), 32)), s(k, "teacherToken"), "teacherToken")
        try eq(shaHex(s(k, "teacherToken")), s(k, "teacherHash"), "teacherHash")
        try eq(b64u(hkdf(sec, ascii("delete-token"), 32)), s(k, "deleteToken"), "deleteToken")
        try eq(shaHex(s(k, "deleteToken")), s(k, "deleteHash"), "deleteHash")
        try eq(hex(hkdf(sec, ascii("backup-key"), 32)), s(k, "backupKeyHex"), "backupKey")
    }
}
for k in a(d(V, "keys"), "join") {
    check("join keys \(s(k, "label"))") {
        let st = stretch(unhex(s(k, "secretHex")))
        try eq(hex(st), s(k, "stretchedHex"), "stretched")
        try eq(prk(st), s(k, "prkHex"), "PRK")
        try eq(hex(hkdf(st, ascii("join-id"), 16)), s(k, "joinId"), "joinId")
        try eq(b64u(hkdf(st, ascii("join-token"), 32)), s(k, "joinToken"), "joinToken")
        try eq(hex(hkdf(st, ascii("join-key"), 32)), s(k, "joinKeyHex"), "joinKey")
    }
}
/// What the server stores for a join code: HMACs under its pepper, never the joinId or a plain hash (1.3, 3.6).
for k in a(d(V, "keys"), "server") {
    check("server join index \(s(k, "label"))") {
        let pepper = SymmetricKey(data: unhex(s(k, "pepperHex")))
        func mac(_ t: String) -> String { hex(Data(HMAC<SHA256>.authenticationCode(for: ascii(t), using: pepper))) }
        try eq(s(k, "joinId"), s(a(d(V, "keys"), "join")[0], "joinId"), "joinId is the counting code's")
        try eq(mac(s(k, "joinId")), s(k, "joinIndex"), "joinIndex")
        try eq(mac(s(k, "joinToken")), s(k, "joinHash"), "joinHash")
    }
}
check("proof") {
    try eq(b64u(unhex(s(d(V, "proof"), "bytesHex"))), s(d(V, "proof"), "proof"), "proof")
    try eq(s(d(V, "proof"), "proof").count, 43, "length")
}
for k in a(d(V, "keys"), "move") {
    check("move keys \(s(k, "label"))") {
        let sec = unhex(s(k, "secretHex"))
        try eq(hex(hkdf(sec, ascii("move-id"), 16)), s(k, "moveId"), "moveId")
        try eq(hex(hkdf(sec, ascii("move-key"), 32)), s(k, "moveKeyHex"), "moveKey")
    }
}
for t in a(V, "tokens") {
    check("token \(s(t, "label"))") {
        try eq(b64u(unhex(s(t, "bytesHex"))), s(t, "token"), "token")
        try eq(shaHex(s(t, "token")), s(t, "tokenHash"), "hash")
    }
}

// ---- P-256 (1.4) -----------------------------------------------------------------

let P = d(V, "p256")
func privateKey(_ who: String) throws -> P256.KeyAgreement.PrivateKey {
    try P256.KeyAgreement.PrivateKey(rawRepresentation: unb64u(s(d(P, who), "d")))
}
enum Damaged: Error { case format, short, point, tag, version, inflate }
/// The X coordinate of d·Q, after the format rule (65 bytes starting 0x04); CryptoKit refuses a point off the curve.
func ecdh(_ priv: P256.KeyAgreement.PrivateKey, _ peer: Data) throws -> Data {
    guard peer.count == 65, peer[peer.startIndex] == 4 else { throw Damaged.point }
    let pub: P256.KeyAgreement.PublicKey
    do { pub = try P256.KeyAgreement.PublicKey(x963Representation: peer) } catch { throw Damaged.point }
    let shared = try priv.sharedSecretFromKeyAgreement(with: pub)
    return shared.withUnsafeBytes { Data($0) }
}
func sealKey(_ shared: Data, _ E: Data, _ R: Data) -> Data { hkdf(shared, ascii("seal-key") + E + R, 32) }
for who in ["teacher", "ephemeral", "other"] {
    check("p256 \(who) public key from d") {
        let k = try privateKey(who)
        try eq(hex(k.publicKey.x963Representation), s(d(P, who), "pubHex"), "pub")
        try eq(hex(unb64u(s(d(P, who), "d"))), s(d(P, who), "dHex"), "d")
        try eq(b64u(unhex(s(d(P, who), "pubHex"))), s(d(P, who), "pub"), "pub b64u")
        try eq(hex(Data([4]) + unb64u(s(d(P, who), "x")) + unb64u(s(d(P, who), "y"))), s(d(P, who), "pubHex"), "x, y")
    }
}
check("p256 shared secret both ways") {
    try eq(hex(try ecdh(try privateKey("ephemeral"), unhex(s(d(P, "teacher"), "pubHex")))), s(P, "sharedHex"), "e * T")
    try eq(hex(try ecdh(try privateKey("teacher"), unhex(s(d(P, "ephemeral"), "pubHex")))), s(P, "sharedHex"), "t * E")
}
check("p256 seal key") {
    let E = unhex(s(d(P, "ephemeral"), "pubHex")), R = unhex(s(d(P, "teacher"), "pubHex"))
    try eq(hex(ascii("seal-key") + E + R), s(P, "sealInfoHex"), "info")
    try eq(hex(sealKey(unhex(s(P, "sharedHex")), E, R)), s(P, "sealKeyHex"), "key")
}
for bp in a(P, "badPoints") {
    check("p256 bad point refused: \(s(bp, "label"))") {
        if (try? ecdh(try privateKey("teacher"), unhex(s(bp, "pubHex")))) != nil { throw Fail(what: "accepted") }
    }
}

// ---- envelopes (1.5) -----------------------------------------------------------

func aad(_ kind: String, _ classId: String, _ id: String, _ ver: Int, _ flags: Int) -> Data {
    ascii("\(s(V, "aadPrefix"))|\(kind)|\(classId)|\(id)|\(ver)|\(flags)")
}
/// A record's AAD: the classroom form, or the sync form (SYNC.md 1.4) for the sync side record.
func aadOf(_ r: [String: Any], flags: Int) -> Data {
    s(r, "envelope").hasPrefix("sync")
        ? ascii("\(s(V, "syncAadPrefix"))|\(s(r, "id"))|\(i(r, "ver"))|\(flags)")
        : aad(s(r, "kind"), s(r, "classId"), s(r, "id"), i(r, "ver"), flags)
}
func gcmSeal(_ key: Data, _ nonce: Data, _ ad: Data, _ plain: Data) throws -> Data {
    let box = try AES.GCM.seal(plain, using: SymmetricKey(data: key), nonce: AES.GCM.Nonce(data: nonce), authenticating: ad)
    return box.ciphertext + box.tag     // never .combined: the envelope lays the nonce out itself
}
func gcmOpen(_ key: Data, _ nonce: Data, _ ad: Data, _ ctTag: Data) throws -> Data {
    guard ctTag.count >= 16 else { throw Damaged.short }
    let box = try AES.GCM.SealedBox(nonce: AES.GCM.Nonce(data: nonce), ciphertext: ctTag.prefix(ctTag.count - 16), tag: ctTag.suffix(16))
    do { return try AES.GCM.open(box, using: SymmetricKey(data: key), authenticating: ad) } catch { throw Damaged.tag }
}
/// A classroom plaintext's cap (1.5): lower than Sync's, since circuits come from the other party.
let MAX_PLAINTEXT = 4_000_000
/// The one envelope each kind uses (1.5), checked before anything else.
let ENVELOPE_OF: [String: UInt8] = ["teacher": 1, "join": 1, "info": 1, "assignment": 1, "live": 1, "move": 1, "classroom": 1, "item": 1, "membership": 1, "name": 2, "submission": 2, "answer": 2, "key": 2]
check("the envelope of every kind, and the plaintext cap") {
    let given = d(V, "envelopeOf")
    try eq(given.count, ENVELOPE_OF.count, "kinds")
    for (k, v) in ENVELOPE_OF { try eq(i(given, k), Int(v), k) }
    try eq(i(V, "maxPlaintext"), MAX_PLAINTEXT, "maxPlaintext")
}
/// Raw deflate (RFC 1951) through Compression's COMPRESSION_ZLIB, into a buffer one byte past the cap:
/// a stream that fills it is refused, as the app's hooks do (MAX_PLAINTEXT + 1).
func inflate(_ src: Data, cap: Int = MAX_PLAINTEXT) throws -> Data {
    let dst = UnsafeMutablePointer<UInt8>.allocate(capacity: cap + 1)
    defer { dst.deallocate() }
    let n = src.withUnsafeBytes { p in compression_decode_buffer(dst, cap + 1, p.baseAddress!.assumingMemoryBound(to: UInt8.self), src.count, nil, COMPRESSION_ZLIB) }
    guard n > 0, n <= cap else { throw Damaged.inflate }
    return Data(bytes: dst, count: n)
}
/// Opens the envelope of a record of `kind`, as a client does: every failure is "damaged".
func open(_ env: Data, kind: String, ad: (Int) -> Data, key: Data? = nil, priv: P256.KeyAgreement.PrivateKey? = nil) throws -> Data {
    let e = Data(env)   // re-based at 0
    guard e.count >= 1, e[0] == ENVELOPE_OF[kind] else { throw Damaged.version }
    guard e.count >= 2, e[1] & ~1 == 0 else { throw Damaged.format }
    let flags = Int(e[1])
    var plain: Data
    if e[0] == 1 {
        guard e.count >= 30, let key else { throw Damaged.short }
        plain = try gcmOpen(key, e.subdata(in: 2..<14), ad(flags), e.subdata(in: 14..<e.count))
    } else if e[0] == 2 {
        guard e.count >= 95, e[2] == 4, let priv else { throw Damaged.short }
        let E = e.subdata(in: 2..<67)
        let k = sealKey(try ecdh(priv, E), E, priv.publicKey.x963Representation)
        plain = try gcmOpen(k, e.subdata(in: 67..<79), ad(flags), e.subdata(in: 79..<e.count))
    } else { throw Damaged.version }
    if flags & 1 != 0 { plain = try inflate(plain) }
    return plain
}

// ---- payloads (2.2) ------------------------------------------------------------

let KNOWN: Set<String> = ["teacher", "join", "info", "assignment", "live", "name", "submission", "answer", "key", "move", "classroom", "item", "membership"]
let uuidRe = try! NSRegularExpression(pattern: "^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$")
let hex32Re = try! NSRegularExpression(pattern: "^[0-9a-f]{32}$")
let code28Re = try! NSRegularExpression(pattern: "^[0-9A-HJKMNP-TV-Z]{28}$")
let code12Re = try! NSRegularExpression(pattern: "^[0-9A-HJKMNP-TV-Z]{12}$")
let b64uRe = try! NSRegularExpression(pattern: "^[A-Za-z0-9_-]+$")
func matches(_ re: NSRegularExpression, _ v: Any?) -> Bool {
    guard let t = v as? String else { return false }
    return re.firstMatch(in: t, range: NSRange(location: 0, length: (t as NSString).length)) != nil
}
func isStr(_ v: Any?) -> Bool { v is String }
func optStr(_ v: Any?) -> Bool { v == nil || v is String }
func isBool(_ v: Any?) -> Bool { guard let n = v as? NSNumber else { return false }; return CFGetTypeID(n) == CFBooleanGetTypeID() }
/// A JSON number that is a whole number from 0 to 2^53-1 (5, 5.0 and 5e0 are all 5).
func isInt(_ v: Any?) -> Bool {
    guard let n = v as? NSNumber, !isBool(v) else { return false }
    let dv = n.doubleValue
    guard dv.isFinite, dv == dv.rounded(.down), dv >= 0 else { return false }
    return n.int64Value >= 0 && n.int64Value <= 9007199254740991 && dv <= 9007199254740991
}
func isB64(_ v: Any?, _ n: Int) -> Bool { guard let t = v as? String else { return false }; return matches(b64uRe, t) && t.count == (n * 4 + 2) / 3 }
let escRe = try! NSRegularExpression(pattern: "\\\\u([0-9a-fA-F]{4})")
/// The payload, or "newer" (hands off) / "invalid" (treated as damaged); the checks in 2.2's order.
func readPayload(_ bytes: Data, kind expected: String) -> Any {
    guard let text = String(data: bytes, encoding: .utf8) else { return "invalid" }
    // An escaped surrogate with no partner is refused before parsing (Foundation would let it through).
    let ns = text as NSString
    let esc = escRe.matches(in: text, range: NSRange(location: 0, length: ns.length)).map { ($0.range.location, Int(ns.substring(with: $0.range(at: 1)), radix: 16)!) }
    var k = 0
    while k < esc.count {
        let (at, cp) = esc[k]
        if cp >= 0xD800 && cp <= 0xDBFF {
            guard k + 1 < esc.count, esc[k + 1].0 == at + 6, esc[k + 1].1 >= 0xDC00, esc[k + 1].1 <= 0xDFFF else { return "invalid" }
            k += 1
        } else if cp >= 0xDC00 && cp <= 0xDFFF { return "invalid" }
        k += 1
    }
    guard let any = try? JSONSerialization.jsonObject(with: bytes, options: [.fragmentsAllowed]), let p = any as? [String: Any] else { return "invalid" }
    guard isInt(p["v"]), (p["v"] as! NSNumber).intValue >= 1 else { return "invalid" }
    if (p["v"] as! NSNumber).intValue > 1 { return "newer" }
    guard let kind = p["kind"] as? String else { return "invalid" }
    if !KNOWN.contains(kind) { return "newer" }
    if kind != expected { return "invalid" }
    let proofOk = isB64(p["proof"], 32)
    let key = p["key"]
    let keyOk: Bool = {
        if key is NSNull { return true }
        guard let k = key as? [String: Any] else { return false }
        return (isStr(k["text"]) != isStr(k["sealed"])) && optStr(k["names"])
    }()
    let predict = p["predict"]
    let predictOk: Bool = {
        if predict is NSNull { return true }
        guard let q = predict as? [String: Any], isStr(q["prompt"]), let lights = q["lights"] as? [Any] else { return false }
        return lights.allSatisfy { $0 is String }
    }()
    let ok: Bool
    switch kind {
    case "teacher": ok = isStr(p["name"]) && isB64(p["d"], 32) && isB64(p["pub"], 65) && isB64(p["classKey"], 32) && matches(code12Re, p["joinCode"]) && isBool(p["joinOpen"]) && isInt(p["createdAt"]) && isInt(p["modifiedAt"])
    case "join": ok = isStr(p["name"]) && isB64(p["classKey"], 32) && isB64(p["pub"], 65)
    case "info": ok = isStr(p["name"]) && isInt(p["modifiedAt"])
    case "assignment": ok = isStr(p["title"]) && isStr(p["instructions"]) && (p["dueAt"] is NSNull || isInt(p["dueAt"])) && isBool(p["closeAfterDue"]) && isStr(p["cdl"]) && isInt(p["createdAt"]) && isInt(p["modifiedAt"]) && key != nil && keyOk
    case "live":
        let ended = isBool(p["ended"]) && (p["ended"] as! NSNumber).boolValue
        ok = matches(uuidRe, p["session"]) && isInt(p["at"]) && (p["ended"] == nil || isBool(p["ended"])) && (ended || (isStr(p["cdl"]) && isInt(p["step"]) && isBool(p["reveal"]) && predict != nil && predictOk))
    case "name": ok = isStr(p["name"]) && isInt(p["joinedAt"]) && proofOk
    case "submission": ok = isStr(p["name"]) && isStr(p["cdl"]) && isInt(p["handedInAt"]) && isInt(p["attempt"]) && proofOk
    case "answer":
        let lights = p["lights"] as? [String: Any]
        ok = matches(uuidRe, p["session"]) && isInt(p["ver"]) && lights != nil && isInt(p["at"]) && proofOk &&
            lights!.values.allSatisfy { v in !isBool(v) && ((v as? NSNumber).map { $0.doubleValue == 0 || $0.doubleValue == 1 } ?? false) }
    case "key": ok = isStr(p["text"]) && optStr(p["names"])
    case "move": ok = matches(hex32Re, p["classId"]) && matches(uuidRe, p["studentId"]) && isB64(p["token"], 32) && proofOk && isB64(p["classKey"], 32) && isB64(p["pub"], 65) && isStr(p["name"]) && isStr(p["className"])
    case "classroom": ok = matches(hex32Re, p["classId"]) && matches(code28Re, p["teacherKey"]) && isStr(p["name"]) && isInt(p["createdAt"]) && isInt(p["modifiedAt"])
    case "item": ok = isStr(p["type"]) && isStr(p["title"]) && isStr(p["topic"]) && isStr(p["note"]) && isStr(p["cdl"]) && isInt(p["createdAt"]) && isInt(p["modifiedAt"])
    case "membership": ok = matches(hex32Re, p["classId"]) && matches(uuidRe, p["studentId"]) && isB64(p["token"], 32) && proofOk && isB64(p["classKey"], 32) && isB64(p["pub"], 65) && isStr(p["name"]) && isStr(p["className"]) && isInt(p["joinedAt"])
    default: ok = false
    }
    return ok ? p : "invalid"
}

// ---- records (7.1.5) -----------------------------------------------------------

let teacherPriv = try! privateKey("teacher")
for r in a(V, "records") {
    let sealed = s(r, "envelope").hasPrefix("sealed")
    let env = unb64u(s(r, "data"))
    let n = i(r, "n")
    check("record \(n) opens: \(s(r, "label"))") {
        try eq(env.count, i(r, "envelopeBytes"), "length")
        try eq(String(hex(sha256(env)).prefix(32)), s(r, "h"), "h")
        if !s(r, "envelopeHex").isEmpty { try eq(hex(env), s(r, "envelopeHex"), "envelope hex") }
        try eq(String(data: aadOf(r, flags: i(r, "flags")), encoding: .ascii)!, s(r, "aad"), "aad")
        let plain = try open(env, kind: s(r, "kind"), ad: { aadOf(r, flags: $0) }, key: sealed ? nil : unhex(s(r, "keyHex")), priv: sealed ? teacherPriv : nil)
        try eq(String(data: plain, encoding: .utf8) ?? "", s(r, "payload"), "payload")
        try eq(plain.count, i(r, "payloadBytes"), "payload length")
        try eq(hex(sha256(plain)), s(r, "payloadSha256"), "payload sha")
        let p = readPayload(plain, kind: s(r, "kind"))
        guard let obj = p as? [String: Any] else { throw Fail(what: "payload \(p)") }
        try eq(obj["kind"] as? String ?? "", s(r, "kind"), "kind")
        if !s(r, "deflatedHex").isEmpty {
            try eq(String(data: try inflate(unhex(s(r, "deflatedHex"))), encoding: .utf8) ?? "", s(r, "payload"), "the deflated bytes given inflate to the payload")
        }
    }
    if b(r, "byteExact") {
        check("record \(n) seals byte for byte") {
            let payload = ascii(s(r, "payload")), nonce = unhex(s(r, "nonceHex")), flags = i(r, "flags")
            var again: Data
            if sealed {
                let e = try privateKey(s(r, "ephemeral"))
                let E = e.publicKey.x963Representation, R = teacherPriv.publicKey.x963Representation
                again = Data([2, UInt8(flags)]) + E + nonce + (try gcmSeal(sealKey(try ecdh(e, R), E, R), nonce, aadOf(r, flags: flags), payload))
            } else {
                again = Data([1, UInt8(flags)]) + nonce + (try gcmSeal(unhex(s(r, "keyHex")), nonce, aadOf(r, flags: flags), payload))
            }
            try eq(hex(again), hex(env), "envelope")
        }
    }
    let inner = d(r, "inner")
    if !inner.isEmpty {
        check("record \(n) inner sealed key opens") {
            let plain = try open(env, kind: s(r, "kind"), ad: { aadOf(r, flags: $0) }, key: unhex(s(r, "keyHex")))
            guard let p = readPayload(plain, kind: s(r, "kind")) as? [String: Any], let key = p["key"] as? [String: Any], let sealedB64 = key["sealed"] as? String else { throw Fail(what: "no sealed key") }
            let innerEnv = unb64u(sealedB64)
            try eq(hex(innerEnv), s(inner, "envHex"), "inner envelope")
            let ad = { (flags: Int) in aad(s(inner, "kind"), s(r, "classId"), s(inner, "id"), i(inner, "ver"), flags) }
            let innerPlain = try open(innerEnv, kind: s(inner, "kind"), ad: ad, priv: teacherPriv)
            guard let k = readPayload(innerPlain, kind: "key") as? [String: Any] else { throw Fail(what: "inner payload refused") }
            try eq(k["kind"] as? String ?? "", "key", "kind")
            try eq(k["text"] is String, true, "text")
            let e = try privateKey(s(inner, "ephemeral"))
            let E = e.publicKey.x963Representation, R = teacherPriv.publicKey.x963Representation, nonce = unhex(s(inner, "nonce"))
            let again = Data([2, 0]) + E + nonce + (try gcmSeal(sealKey(try ecdh(e, R), E, R), nonce, ad(0), innerPlain))
            try eq(hex(again), s(inner, "envHex"), "inner seals byte for byte")
        }
    }
}
check("two seals of one payload differ (fresh nonce, fresh ephemeral key)") {
    let r0 = a(V, "records")[0]
    let payload = ascii(s(r0, "payload")), R = teacherPriv.publicKey.x963Representation
    func one() throws -> Data {
        let e = P256.KeyAgreement.PrivateKey()
        let E = e.publicKey.x963Representation
        let nonce = Data(AES.GCM.Nonce())
        return Data([2, 0]) + E + nonce + (try gcmSeal(sealKey(try ecdh(e, R), E, R), nonce, aadOf(r0, flags: 0), payload))
    }
    if try hex(one()) == hex(one()) { throw Fail(what: "same") }
}

// ---- refused and tampered (7.1.6, 7.1.7) -----------------------------------------

for r in a(V, "refused") {
    check("refused: \(s(r, "label")) -> \(s(r, "why"))") {
        let ad = { (flags: Int) in aad(s(r, "kind"), s(r, "classId"), s(r, "id"), i(r, "ver"), flags) }
        let plain = s(r, "keyHex").isEmpty
            ? try open(unb64u(s(r, "data")), kind: s(r, "kind"), ad: ad, priv: try privateKey(s(r, "privateKey")))
            : try open(unb64u(s(r, "data")), kind: s(r, "kind"), ad: ad, key: unhex(s(r, "keyHex")))
        try eq(hex(plain), s(r, "payloadHex"), "payload bytes")
        try eq(readPayload(plain, kind: s(r, "kind")) as? String ?? "ok", s(r, "why"), "why")
    }
}
for t in a(V, "tamper") {
    check("must not open: \(s(t, "label"))") {
        let ad = { (flags: Int) in aad(s(t, "kind"), s(t, "classId"), s(t, "id"), i(t, "ver"), flags) }
        let opened: Bool
        if s(t, "keyHex").isEmpty {
            opened = (try? open(unb64u(s(t, "data")), kind: s(t, "kind"), ad: ad, priv: try privateKey(s(t, "privateKey")))) != nil
        } else {
            opened = (try? open(unb64u(s(t, "data")), kind: s(t, "kind"), ad: ad, key: unhex(s(t, "keyHex")))) != nil
        }
        if opened { throw Fail(what: "opened") }
    }
}

print("\(passes) passed, \(fails) failed (\(path))")
exit(Int32(min(fails, 125)))
