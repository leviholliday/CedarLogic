// Checks of ShareLinkCodec.swift: round trips, links made elsewhere (python's
// zlib: the website's fixtures), and links that aren't.
//   share-check [<link file> <the .cdl it holds>] [<file to write a link to>]
import Foundation

var fails = 0
func ok(_ c: Bool, _ m: String) { print((c ? "ok   " : "FAIL ") + m); if !c { fails += 1 } }

let texts = ["(cedarlogic (version 3) (page 0 (name \"é ✓ 日本\")))",
             String(repeating: "gate AND 12 34\n", count: 20_000),
             "x"]
for t in texts {
    let data = ShareCodec.encode(t)
    ok(data != nil && (try? ShareCodec.decode(data!)) == t, "round trip of \(t.utf8.count) bytes")
}
ok(ShareCodec.encode("") == nil, "nothing to encode makes no link")
ok(!(ShareCodec.encode(texts[0]) ?? "").contains(where: { "+/=".contains($0) }), "the data is base64url, no padding")

let p = ShareCodec.parse("https://cedarlogic.netlify.app/online-logic-gate-simulator/#c=abc&n=A%20B")
ok(p?.data == "abc" && p?.name == "A B", "parse reads the data and the name")
ok(ShareCodec.parse("cedarlogic://open?c=xyz")?.data == "xyz", "...and the ? form")
ok(ShareCodec.parse("cedarlogic://open") == nil && ShareCodec.parse("https://x/#compare") == nil, "...and refuses what has no data")
ok(ShareCodec.fragment(data: "abc", name: "Half adder & more") == "c=abc&n=Half%20adder%20%26%20more", "fragment encodes the name")

for bad in ["not*base64", "AAAA", "", "QUJD"] {
    ok((try? ShareCodec.decode(bad)) == nil, "garbage '\(bad)' is refused")
}
let cut = String((ShareCodec.encode(texts[1]) ?? "").prefix(40))
ok((try? ShareCodec.decode(cut)) == nil, "a link cut short is refused")

let args = Array(CommandLine.arguments.dropFirst())
if args.count >= 2 {
    let link = (try! String(contentsOfFile: args[0], encoding: .utf8)).trimmingCharacters(in: .whitespacesAndNewlines)
    let want = try! String(contentsOfFile: args[1], encoding: .utf8)
    let got = ShareCodec.parse("#" + link).flatMap { try? ShareCodec.decode($0.data) }
    ok(got == want, "a link made by python's zlib (raw deflate) decodes to the file (\(want.utf8.count) bytes)")
}
if args.count >= 3 {
    let want = try! String(contentsOfFile: args[1], encoding: .utf8)
    try! (ShareCodec.encode(want) ?? "").write(toFile: args[2], atomically: true, encoding: .utf8)
}
print(fails == 0 ? "all good" : "\(fails) failed")
exit(fails == 0 ? 0 : 1)
