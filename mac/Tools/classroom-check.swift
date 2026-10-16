// The Mac's classroom hooks, checked without the app (mac/Tools/classroom-check.sh
// builds this with mac/App/SyncHooks.swift, mac/App/ClassroomHooks.swift and
// libCedarCore.a):
//
//   classroom-check selftest <tempDir> [--server <url> [--live <url>]]
//       the core's self-test through the C interface with the app's own hooks
//       (CryptoKit, CommonCrypto, Compression, URLSession, the Classroom
//       folder's files and flock): every CLASSROOM.md 7.1 vector, the 7.2
//       scenarios on the in-process fake server, the threaded engine, and with
//       --server a class's round trip against the mock server too; then the
//       hooks' own extras.

import CryptoKit
import Foundation

@main
enum ClassroomCheck {
    static func main() {
        let args = CommandLine.arguments
        guard args.count >= 3, args[1] == "selftest" else {
            print("usage: classroom-check selftest <tempDir> [--server <url> [--live <url>]]")
            exit(2)
        }
        setvbuf(stdout, nil, _IONBF, 0)
        var server: String?, live: String?
        if let i = args.firstIndex(of: "--server"), i + 1 < args.count { server = args[i + 1] }
        if let i = args.firstIndex(of: "--live"), i + 1 < args.count { live = args[i + 1] }
        exit(selfTest(tempDir: args[2], server: server, live: live) ? 0 : 1)
    }

    static func selfTest(tempDir: String, server: String?, live: String?) -> Bool {
        let base = URL(fileURLWithPath: tempDir)
        let sync = SyncPlatform(syncDir: base.appendingPathComponent("sync-folder"))
        let platform = ClassroomPlatform(dir: base.appendingPathComponent("Classroom"), sync: sync)
        var hooks = platform.hooks()
        var report: UnsafeMutablePointer<CChar>?
        let started = Date()
        let ok = cl_classroom_self_test(&hooks, tempDir, server, live, &report)
        if let report {
            print(String(cString: report), terminator: "")
            free(report)
        }
        print(String(format: "(the self-test took %.1f s)", Date().timeIntervalSince(started)))

        // The hooks' own extras: what the core doesn't exercise.
        var pass = true
        func check(_ name: String, _ cond: Bool) {
            print(cond ? "PASS" : "FAIL", "mac-hooks: \(name)")
            if !cond { pass = false }
        }
        // P-256: the scalar 0 and the order n are refused; a fresh pair agrees both ways.
        var d1 = [UInt8](repeating: 0, count: 32), d2 = d1, p1 = [UInt8](repeating: 0, count: 65), p2 = p1, x1 = d1, x2 = d1
        check("a zero scalar is refused", !ClassroomPlatform.p256Public(d1, &p1))
        let order: [UInt8] = [0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                              0xbc, 0xe6, 0xfa, 0xad, 0xa7, 0x17, 0x9e, 0x84, 0xf3, 0xb9, 0xca, 0xc2, 0xfc, 0x63, 0x25, 0x51]
        check("the curve's order as a scalar is refused", !ClassroomPlatform.p256Public(order, &p1))
        check("two fresh key pairs agree both ways",
              ClassroomPlatform.p256Generate(&d1, &p1) && ClassroomPlatform.p256Generate(&d2, &p2) &&
                  ClassroomPlatform.p256Ecdh(d1, p2, &x1) && ClassroomPlatform.p256Ecdh(d2, p1, &x2) && x1 == x2 && d1 != d2)
        var compressed = [UInt8](p1.prefix(33))
        compressed[0] = 0x02 | (p1[64] & 1)
        compressed += [UInt8](repeating: 0, count: 32)
        check("a compressed point is refused", !ClassroomPlatform.p256Ecdh(d1, compressed, &x1))
        var off = p1
        off[64] ^= 1
        check("a point off the curve is refused", !ClassroomPlatform.p256Ecdh(d1, off, &x1))
        // PBKDF2: CommonCrypto's 600,000 rounds, and how long the join code's stretching takes here.
        var out = [UInt8](repeating: 0, count: 32)
        let pw: [UInt8] = [0, 1, 2, 3, 4, 5], salt = Array("cedarlogic-classroom-join-v1".utf8)
        let t0 = Date()
        let stretched = ClassroomPlatform.pbkdf2(pw, pw.count, salt, salt.count, 600_000, &out)
        let ms = Date().timeIntervalSince(t0) * 1000
        check(String(format: "PBKDF2 600,000 rounds in %.0f ms gives the vector", ms),
              stretched && out.map { String(format: "%02x", $0) }.joined() == "5bfd32af2d66869db77c9544e4b984d817b20b341f023909bc5cb0f39c27917c")
        // The Classroom folder: 0700, files 0600, written whole, names that would leave it refused.
        let saved = platform.saveFile("cache/0123/assignments/a.json", "{\"x\":1}")
        let file = platform.dir.appendingPathComponent("cache/0123/assignments/a.json")
        let mode = ((try? FileManager.default.attributesOfItem(atPath: file.path))?[.posixPermissions] as? NSNumber)?.intValue ?? 0
        let dirMode = ((try? FileManager.default.attributesOfItem(atPath: platform.dir.path))?[.posixPermissions] as? NSNumber)?.intValue ?? 0
        check("a file is saved at 0600 in a 0700 folder", saved && mode == 0o600 && dirMode == 0o700)
        if let back = platform.loadFile("cache/0123/assignments/a.json") {
            check("and loads back", String(cString: back) == "{\"x\":1}")
            free(back)
        } else {
            check("and loads back", false)
        }
        check("a name with .. is refused", !platform.saveFile("../escape.json", "x") && !platform.saveFile("cache/../../x", "x") &&
              !platform.saveFile("/tmp/x", "x"))
        platform.removeTree("cache/0123")
        check("removeTree removes a class's cache", !FileManager.default.fileExists(atPath: file.path))
        // The lock: one holder at a time.
        let lockPath = platform.dir.appendingPathComponent("lock").path
        let other = ClassroomPlatform(dir: platform.dir, sync: sync)
        check("the folder's lock is held by one copy at a time",
              platform.tryLock(lockPath) && !other.tryLock(lockPath))
        platform.unlock()
        check("and given back", other.tryLock(lockPath))
        other.unlock()
        return ok && pass
    }
}
