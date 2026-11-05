# A stand-in for Sentry, for CI's crash reports check (linux-native.yml,
# linux/App/CrashReports.h): takes what the app sends (an envelope,
# POST /api/<project>/envelope/, gzipped or not) and appends it to a file.
#   python3 sentry-mock.py <port file> <reports file>   serve (127.0.0.1, any free port)
#   python3 sentry-mock.py --check <reports file> <home dir the crashed copy ran from>
#       the reports: a crash, tagged platform=linux, its paths cleaned (<user>),
#       and nothing of this computer's (the user name, the host name, the home
#       folder) anywhere in it
import getpass, gzip, json, os, socket, sys, uuid, zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

if sys.argv[1] == "--check":
    text = open(sys.argv[2], encoding="utf-8", errors="replace").read() if os.path.exists(sys.argv[2]) else ""
    low = text.lower()
    fails = []
    def need(ok, what):
        print(("PASS " if ok else "FAIL ") + what)
        if not ok: fails.append(what)
    need(bool(text.strip()), "a crash report arrived (%d bytes)" % len(text))
    need("sigsegv" in low and "stacktrace" in low, "...it's the crash: a segmentation fault, with its stack")
    need('"platform":"linux"' in text.replace(" ", ""), "...tagged platform=linux")
    need("<user>" in text, "...its paths cleaned (/home/<user>/...)")
    home = os.environ.get("HOME", "")
    host = socket.gethostname()
    etc = open("/etc/hostname").read().strip() if os.path.exists("/etc/hostname") else ""
    needles = [getpass.getuser(), os.environ.get("USER", ""), host, host.split(".")[0], etc, home,
               home.replace("/", "\\/"), os.path.basename(home)]
    found = sorted({n for n in needles if len(n) >= 4 and n.lower() in low})
    need(not found, "...with nothing of this computer's in it (user, host, home folder): %s" % found)
    for k in ("server_name", "breadcrumbs", '"user"'):
        need(k not in text, "...and no %s" % k)
    sys.exit(1 if fails else 0)

out = sys.argv[2]

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass

    def do_POST(self):
        n = int(self.headers.get("content-length") or 0)
        body = self.rfile.read(n) if n else b""
        enc = (self.headers.get("content-encoding") or "").lower()
        try:
            if enc == "gzip": body = gzip.decompress(body)
            elif enc == "deflate": body = zlib.decompress(body)
        except Exception:
            pass
        with open(out, "ab") as f:
            f.write(("== %s %s\n" % (self.command, self.path)).encode())
            f.write(body + b"\n")
        data = json.dumps({"id": uuid.uuid4().hex}).encode()
        self.send_response(200)
        self.send_header("content-type", "application/json")
        self.send_header("content-length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

s = ThreadingHTTPServer(("127.0.0.1", 0), H)
open(sys.argv[1], "w").write(str(s.server_address[1]))
s.serve_forever()
