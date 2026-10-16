// Share links: a circuit's .cdl text, deflated (raw, no header: what
// Compression's COMPRESSION_ZLIB is) and written base64url, after "#c=".
//
//   https://cedarlogic.netlify.app/online-logic-gate-simulator/#c=<data>&n=<name>
//   cedarlogic://open#c=<data>&n=<name>
//
// The same links CedarLogic Online makes and opens (public/assets/js/
// sim-share.js on the website), and the Linux app. Nothing here needs the
// app, so mac/Tools/share-check.sh runs it on its own.

import Foundation
import Compression

enum ShareCodec {
    /// Longer than this isn't a link to paste around (the website says so).
    static let maxWebLink = 8192
    /// A circuit may inflate to this much.
    static let maxText = 20_000_000
    static let webBase = "https://cedarlogic.netlify.app/online-logic-gate-simulator/"

    struct Failure: Error { let reason: String }

    /// The data part of a link, or nil if it couldn't be compressed.
    static func encode(_ text: String) -> String? {
        let src = Array(text.utf8)
        guard !src.isEmpty else { return nil }
        var out = [UInt8](repeating: 0, count: src.count + src.count / 8 + 1024)
        let n = out.withUnsafeMutableBufferPointer { dst in
            src.withUnsafeBufferPointer { s in
                compression_encode_buffer(dst.baseAddress!, dst.count, s.baseAddress!, s.count, nil, COMPRESSION_ZLIB)
            }
        }
        guard n > 0 else { return nil }
        return base64url(Data(out[0..<n]))
    }

    /// The .cdl text a link's data holds.
    static func decode(_ data: String) throws -> String {
        guard let bytes = fromBase64url(data), !bytes.isEmpty else { throw Failure(reason: "not a CedarLogic link") }
        let dummy = UnsafeMutablePointer<UInt8>.allocate(capacity: 1)
        defer { dummy.deallocate() }
        var stream = compression_stream(dst_ptr: dummy, dst_size: 0, src_ptr: UnsafePointer(dummy), src_size: 0, state: nil)
        guard compression_stream_init(&stream, COMPRESSION_STREAM_DECODE, COMPRESSION_ZLIB) == COMPRESSION_STATUS_OK else {
            throw Failure(reason: "couldn't inflate")
        }
        defer { compression_stream_destroy(&stream) }
        var result = Data()
        let chunk = 1 << 16
        let buffer = UnsafeMutablePointer<UInt8>.allocate(capacity: chunk)
        defer { buffer.deallocate() }
        try bytes.withUnsafeBufferPointer { src in
            stream.src_ptr = src.baseAddress!
            stream.src_size = src.count
            while true {
                stream.dst_ptr = buffer
                stream.dst_size = chunk
                let status = compression_stream_process(&stream, Int32(COMPRESSION_STREAM_FINALIZE.rawValue))
                result.append(buffer, count: chunk - stream.dst_size)
                if result.count > maxText { throw Failure(reason: "too big to be a circuit") }
                if status == COMPRESSION_STATUS_END { break }
                // (Out of input without the end of the data: cut short.)
                if status == COMPRESSION_STATUS_ERROR || (status == COMPRESSION_STATUS_OK && stream.src_size == 0 && stream.dst_size != 0) {
                    throw Failure(reason: "the link was cut short")
                }
            }
        }
        guard let text = String(data: result, encoding: .utf8) else { throw Failure(reason: "not text") }
        return text
    }

    /// "c=<data>&n=<name>" out of a link, from its "#" part, or its "?" part:
    /// the data and the circuit's name (empty if none).
    static func parse(_ link: String) -> (data: String, name: String)? {
        guard let cut = link.firstIndex(where: { $0 == "#" || $0 == "?" }) else { return nil }
        var data: String?, name = ""
        for pair in link[link.index(after: cut)...].split(separator: "&") {
            if pair.hasPrefix("c=") { data = String(pair.dropFirst(2)) }
            else if pair.hasPrefix("n=") { name = String(pair.dropFirst(2)).removingPercentEncoding ?? "" }
        }
        guard let data, !data.isEmpty else { return nil }
        return (data, String(name.prefix(80)))
    }

    static func fragment(data: String, name: String) -> String {
        "c=" + data + (name.isEmpty ? "" : "&n=" + (name.addingPercentEncoding(withAllowedCharacters: .urlUnreserved) ?? ""))
    }

    private static func base64url(_ d: Data) -> String {
        d.base64EncodedString().replacingOccurrences(of: "+", with: "-").replacingOccurrences(of: "/", with: "_")
            .replacingOccurrences(of: "=", with: "")
    }
    private static func fromBase64url(_ s: String) -> [UInt8]? {
        var t = s.replacingOccurrences(of: "-", with: "+").replacingOccurrences(of: "_", with: "/")
        t = t.filter { !$0.isWhitespace && $0 != "=" }
        guard t.allSatisfy({ $0.isASCII && ($0.isLetter || $0.isNumber || $0 == "+" || $0 == "/") }) else { return nil }
        t += String(repeating: "=", count: (4 - t.count % 4) % 4)
        return Data(base64Encoded: t).map { Array($0) }
    }
}

private extension CharacterSet {
    /// What a URL leaves as it is (RFC 3986's unreserved).
    static let urlUnreserved = CharacterSet(charactersIn: "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~")
}
