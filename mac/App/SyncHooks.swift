// The sync engine's platform hooks on the Mac (SYNC.md 6.4), the part that
// needs no window: CryptoKit for SHA-256, HMAC and AES-GCM, SecRandomCopyBytes,
// Compression's raw deflate, URLSession for HTTP (on the engine's thread, so
// it waits), the secret in a 0600 file and the sync folder's flock. The app
// adds its window hooks on top (Sync.swift); mac/Tools/sync-check.swift uses
// these as they are to run the engine's self-test.

import Compression
import CryptoKit
import Foundation
import Security

final class SyncPlatform {
    /// ~/Library/Application Support/CedarLogic/Sync: per machine, outside the library (2.5).
    let syncDir: URL
    private var lockFD: Int32 = -1
    private let lockGuard = NSLock()

    init(syncDir: URL) { self.syncDir = syncDir }

    static var defaultSyncDir: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("CedarLogic/Sync", isDirectory: true)
    }

    /// The hooks every use shares; `ctx` is this object (kept alive by its owner).
    /// The window hooks are left empty: flush_open answers at once, no
    /// window is ever open, nothing is asked (the engine keeps the circuits).
    func hooks() -> CLSyncHooks {
        var h = CLSyncHooks()
        h.ctx = Unmanaged.passUnretained(self).toOpaque()
        h.random = { _, out, n in SyncPlatform.random(out, n) }
        h.sha256 = { _, p, n, out in SyncPlatform.sha256(p, n, out) }
        h.hmac_sha256 = { _, key, keyLen, p, n, out in SyncPlatform.hmac(key, keyLen, p, n, out) }
        h.aes_gcm_seal = { _, key, nonce, aad, aadLen, plain, n, out in
            SyncPlatform.seal(key, nonce, aad, aadLen, plain, n, out)
        }
        h.aes_gcm_open = { _, key, nonce, aad, aadLen, ctTag, n, out in
            SyncPlatform.open(key, nonce, aad, aadLen, ctTag, n, out)
        }
        h.deflate_raw = { _, p, n, outLen in SyncPlatform.deflate(p, n, outLen) }
        h.inflate_raw = { _, p, n, maxOut, outLen in SyncPlatform.inflate(p, n, maxOut, outLen) }
        h.http = { _, method, url, headers, body, bodyLen, status, sent, respHeaders, respBody, respLen in
            SyncPlatform.http(method, url, headers, body, bodyLen, status, sent, respHeaders, respBody, respLen)
        }
        h.on_main = { _, fn, arg in
            guard let fn else { return }
            if Thread.isMainThread { fn(arg) } else { DispatchQueue.main.sync { fn(arg) } }
        }
        h.load_secret = { ctx in SyncPlatform.of(ctx).loadSecret() }
        h.save_secret = { ctx, code in SyncPlatform.of(ctx).saveSecret(code) }
        h.forget_secret = { ctx in SyncPlatform.of(ctx).forgetSecret() }
        h.try_lock = { ctx, path in SyncPlatform.of(ctx).tryLock(path) }
        h.unlock = { ctx in SyncPlatform.of(ctx).unlock() }
        h.flush_open = { _, token in cl_sync_flush_done(token) }
        return h
    }

    static func of(_ ctx: UnsafeMutableRawPointer?) -> SyncPlatform {
        Unmanaged<SyncPlatform>.fromOpaque(ctx!).takeUnretainedValue()
    }

    // MARK: Crypto

    static func random(_ out: UnsafeMutablePointer<UInt8>?, _ n: Int) -> Bool {
        guard n > 0 else { return true }
        guard let out else { return false }
        return SecRandomCopyBytes(kSecRandomDefault, n, out) == errSecSuccess
    }

    private static func bytes(_ p: UnsafePointer<UInt8>?, _ n: Int) -> UnsafeRawBufferPointer {
        guard let p, n > 0 else { return UnsafeRawBufferPointer(start: nil, count: 0) }
        return UnsafeRawBufferPointer(start: p, count: n)
    }

    static func sha256(_ p: UnsafePointer<UInt8>?, _ n: Int, _ out: UnsafeMutablePointer<UInt8>?) {
        guard let out else { return }
        let digest = SHA256.hash(data: bytes(p, n))
        var i = 0
        for b in digest { out[i] = b; i += 1 }
    }

    static func hmac(_ key: UnsafePointer<UInt8>?, _ keyLen: Int, _ p: UnsafePointer<UInt8>?, _ n: Int,
                     _ out: UnsafeMutablePointer<UInt8>?) {
        guard let out else { return }
        let mac = HMAC<SHA256>.authenticationCode(for: bytes(p, n), using: SymmetricKey(data: bytes(key, keyLen)))
        var i = 0
        for b in mac { out[i] = b; i += 1 }
    }

    static func seal(_ key: UnsafePointer<UInt8>?, _ nonce: UnsafePointer<UInt8>?, _ aad: UnsafePointer<UInt8>?, _ aadLen: Int,
                     _ plain: UnsafePointer<UInt8>?, _ n: Int, _ out: UnsafeMutablePointer<UInt8>?) -> Bool {
        guard let key, let nonce, let out,
              let box = try? AES.GCM.seal(bytes(plain, n), using: SymmetricKey(data: bytes(key, 32)),
                                          nonce: AES.GCM.Nonce(data: bytes(nonce, 12)), authenticating: bytes(aad, aadLen))
        else { return false }
        let sealed = box.ciphertext + box.tag
        guard sealed.count == n + 16 else { return false }
        sealed.copyBytes(to: out, count: n + 16)
        return true
    }

    static func open(_ key: UnsafePointer<UInt8>?, _ nonce: UnsafePointer<UInt8>?, _ aad: UnsafePointer<UInt8>?, _ aadLen: Int,
                     _ ctTag: UnsafePointer<UInt8>?, _ n: Int, _ out: UnsafeMutablePointer<UInt8>?) -> Bool {
        guard let key, let nonce, let ctTag, let out, n >= 16,
              let box = try? AES.GCM.SealedBox(nonce: AES.GCM.Nonce(data: bytes(nonce, 12)),
                                               ciphertext: Data(bytes(ctTag, n - 16)),
                                               tag: Data(UnsafeRawBufferPointer(start: ctTag + (n - 16), count: 16))),
              let plain = try? AES.GCM.open(box, using: SymmetricKey(data: bytes(key, 32)), authenticating: bytes(aad, aadLen)),
              plain.count == n - 16
        else { return false }
        plain.copyBytes(to: out, count: plain.count)
        return true
    }

    /// Raw deflate (COMPRESSION_ZLIB is RFC 1951 without a header), malloc'd.
    static func deflate(_ p: UnsafePointer<UInt8>?, _ n: Int, _ outLen: UnsafeMutablePointer<Int>?) -> UnsafeMutablePointer<UInt8>? {
        guard let p, n > 0 else { return nil }
        let cap = n + n / 8 + 1024
        guard let out = malloc(cap)?.assumingMemoryBound(to: UInt8.self) else { return nil }
        let got = compression_encode_buffer(out, cap, p, n, nil, COMPRESSION_ZLIB)
        guard got > 0, got < cap else { free(out); return nil }
        outLen?.pointee = got
        return out
    }

    /// Raw inflate, refused past `maxOut` bytes or if the stream doesn't end
    /// properly (a stream, not compression_decode_buffer, which can't tell a
    /// cut-short stream from a whole one). malloc'd.
    static func inflate(_ p: UnsafePointer<UInt8>?, _ n: Int, _ maxOut: Int,
                        _ outLen: UnsafeMutablePointer<Int>?) -> UnsafeMutablePointer<UInt8>? {
        guard let p, n > 0 else { return nil }
        let chunk = 65536
        let scratch = UnsafeMutablePointer<UInt8>.allocate(capacity: chunk)
        defer { scratch.deallocate() }
        var stream = compression_stream(dst_ptr: scratch, dst_size: 0, src_ptr: p, src_size: 0, state: nil)
        guard compression_stream_init(&stream, COMPRESSION_STREAM_DECODE, COMPRESSION_ZLIB) == COMPRESSION_STATUS_OK else { return nil }
        defer { compression_stream_destroy(&stream) }
        stream.src_ptr = p
        stream.src_size = n
        var out = Data()
        while true {
            stream.dst_ptr = scratch
            stream.dst_size = chunk
            let status = compression_stream_process(&stream, Int32(COMPRESSION_STREAM_FINALIZE.rawValue))
            let produced = chunk - stream.dst_size
            if produced > 0 {
                if out.count + produced > maxOut { return nil }
                out.append(scratch, count: produced)
            }
            if status == COMPRESSION_STATUS_END { break }
            // An error, or a stream that stops before its end.
            if status != COMPRESSION_STATUS_OK || (produced == 0 && stream.src_size == 0) { return nil }
        }
        guard let buf = malloc(max(1, out.count))?.assumingMemoryBound(to: UInt8.self) else { return nil }
        out.copyBytes(to: buf, count: out.count)
        outLen?.pointee = out.count
        return buf
    }

    // MARK: HTTP

    private final class NoRedirects: NSObject, URLSessionTaskDelegate {
        func urlSession(_ session: URLSession, task: URLSessionTask, willPerformHTTPRedirection response: HTTPURLResponse,
                        newRequest request: URLRequest, completionHandler: @escaping (URLRequest?) -> Void) {
            completionHandler(nil)
        }
    }

    /// Nothing kept between requests (no cache, no cookies); 20 s without a
    /// byte, 60 s in all.
    private static let session: URLSession = {
        let c = URLSessionConfiguration.ephemeral
        c.timeoutIntervalForRequest = 20
        c.timeoutIntervalForResource = 60
        c.urlCache = nil
        c.httpCookieStorage = nil
        c.httpShouldSetCookies = false
        c.requestCachePolicy = .reloadIgnoringLocalCacheData
        c.waitsForConnectivity = false
        return URLSession(configuration: c, delegate: NoRedirects(), delegateQueue: nil)
    }()

    /// Failures where the request can't have reached the server.
    private static let notSent: Set<URLError.Code> = [
        .badURL, .unsupportedURL, .cannotFindHost, .cannotConnectToHost, .dnsLookupFailed, .notConnectedToInternet,
        .internationalRoamingOff, .dataNotAllowed, .secureConnectionFailed, .serverCertificateUntrusted,
        .serverCertificateHasBadDate, .serverCertificateNotYetValid, .serverCertificateHasUnknownRoot,
        .clientCertificateRejected, .clientCertificateRequired, .appTransportSecurityRequiresSecureConnection,
    ]

    /// HTTPS, or http to this computer only (CL_SYNC_URL for the mock server).
    static func allowed(_ url: URL) -> Bool {
        switch url.scheme?.lowercased() {
        case "https": return true
        case "http": return ["localhost", "127.0.0.1", "::1"].contains(url.host?.lowercased() ?? "")
        default: return false
        }
    }

    /// What one request brought back, handed between URLSession's queue and the waiting thread.
    private final class Reply: @unchecked Sendable {
        var data: Data?
        var response: URLResponse?
        var error: Error?
    }

    static func http(_ method: UnsafePointer<CChar>?, _ url: UnsafePointer<CChar>?, _ headers: UnsafePointer<CChar>?,
                     _ body: UnsafePointer<UInt8>?, _ bodyLen: Int, _ status: UnsafeMutablePointer<Int32>?,
                     _ sent: UnsafeMutablePointer<Bool>?, _ respHeaders: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?,
                     _ respBody: UnsafeMutablePointer<UnsafeMutablePointer<UInt8>?>?, _ respLen: UnsafeMutablePointer<Int>?) {
        status?.pointee = 0
        sent?.pointee = false
        respHeaders?.pointee = nil
        respBody?.pointee = nil
        respLen?.pointee = 0
        guard let method, let url, let u = URL(string: String(cString: url)), allowed(u) else { return }
        var r = URLRequest(url: u)
        r.httpMethod = String(cString: method)
        if let headers {
            for line in String(cString: headers).components(separatedBy: "\r\n") {
                guard let colon = line.firstIndex(of: ":") else { continue }
                let name = String(line[..<colon]).trimmingCharacters(in: .whitespaces)
                let value = String(line[line.index(after: colon)...]).trimmingCharacters(in: .whitespaces)
                if !name.isEmpty { r.setValue(value, forHTTPHeaderField: name) }
            }
        }
        if let body, bodyLen > 0 { r.httpBody = Data(bytes: body, count: bodyLen) }
        let done = DispatchSemaphore(value: 0)
        let reply = Reply()
        let task = session.dataTask(with: r) { d, resp, err in
            reply.data = d
            reply.response = resp
            reply.error = err
            done.signal()
        }
        task.resume()
        if done.wait(timeout: .now() + 75) == .timedOut {
            task.cancel()
            sent?.pointee = true   // it may have got there
            return
        }
        if let resp = reply.response as? HTTPURLResponse {
            status?.pointee = Int32(resp.statusCode)
            sent?.pointee = true
            var lines = ""
            for (k, v) in resp.allHeaderFields { lines += "\(k): \(v)\r\n" }
            respHeaders?.pointee = strdup(lines)
            let data = reply.data ?? Data()
            if let buf = malloc(max(1, data.count))?.assumingMemoryBound(to: UInt8.self) {
                data.copyBytes(to: buf, count: data.count)
                respBody?.pointee = buf
                respLen?.pointee = data.count
            }
            return
        }
        let code = (reply.error as? URLError)?.code
        sent?.pointee = code.map { !notSent.contains($0) } ?? true
    }

    // MARK: The secret and the lock

    private var secretURL: URL { syncDir.appendingPathComponent("secret") }

    /// The sync folder, only this user's (0700).
    @discardableResult
    func makeSyncDir() -> Bool {
        try? FileManager.default.createDirectory(at: syncDir, withIntermediateDirectories: true,
                                                 attributes: [.posixPermissions: 0o700])
        return chmod(syncDir.path, 0o700) == 0
    }

    func loadSecret() -> UnsafeMutablePointer<CChar>? {
        guard let text = try? String(contentsOf: secretURL, encoding: .utf8) else { return nil }
        let code = text.trimmingCharacters(in: .whitespacesAndNewlines)
        return code.isEmpty ? nil : strdup(code)
    }

    /// Written to a 0600 temporary file, then renamed over the old one: never
    /// half written, never readable by anyone else for a moment.
    func saveSecret(_ code: UnsafePointer<CChar>?) -> Bool {
        guard let code, makeSyncDir() else { return false }
        let text = String(cString: code) + "\n"
        let tmp = syncDir.appendingPathComponent(".secret-\(getpid()).tmp")
        unlink(tmp.path)
        let fd = Darwin.open(tmp.path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0o600)
        guard fd >= 0 else { return false }
        let bytes = Array(text.utf8)
        let wrote = bytes.withUnsafeBytes { Darwin.write(fd, $0.baseAddress, $0.count) }
        let ok = wrote == bytes.count && fchmod(fd, 0o600) == 0 && fsync(fd) == 0
        close(fd)
        guard ok, rename(tmp.path, secretURL.path) == 0 else { unlink(tmp.path); return false }
        return true
    }

    func forgetSecret() { unlink(secretURL.path) }

    func tryLock(_ path: UnsafePointer<CChar>?) -> Bool {
        guard let path else { return false }
        lockGuard.lock(); defer { lockGuard.unlock() }
        if lockFD >= 0 { return true }
        makeSyncDir()
        let fd = Darwin.open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0o600)
        guard fd >= 0 else { return false }
        guard flock(fd, LOCK_EX | LOCK_NB) == 0 else { close(fd); return false }
        lockFD = fd
        return true
    }

    func unlock() {
        lockGuard.lock(); defer { lockGuard.unlock() }
        if lockFD >= 0 { flock(lockFD, LOCK_UN); close(lockFD) }
        lockFD = -1
    }
}
