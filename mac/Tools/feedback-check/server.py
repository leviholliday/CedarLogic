# A stand-in for cedarlogic.netlify.app's /api/feedback, just the parts
# mac/Tools/feedback-check.sh needs: the required summary, the per-sender cap
# (10 a day, by installId) and /api/feedback/more, answering as
# netlify/functions/feedback.mjs (cedarlogic-site) does. Also GET /stats:
# how many requests each path had, and the installIds seen.
#   python3 server.py <port file>
import json, sys, re
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

CAP, GRANT = 10, 10
INSTALL = re.compile(r"^[A-Za-z0-9_-]{8,64}$")
senders = {}          # installId -> {"sent": n, "grants": n}
hits = {}             # path -> count
installs = []
plain429 = {"left": 0}

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass

    def reply(self, code, body, headers=None):
        data = json.dumps(body).encode()
        self.send_response(code)
        self.send_header("content-type", "application/json")
        for k, v in (headers or {}).items(): self.send_header(k, v)
        self.send_header("content-length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path == "/stats": return self.reply(200, {"hits": hits, "installs": installs})
        self.reply(404, {"error": "Not here."})

    def do_POST(self):
        path = self.path.split("?")[0]
        hits[path] = hits.get(path, 0) + 1
        n = int(self.headers.get("content-length") or 0)
        b = json.loads(self.rfile.read(n) or b"{}")
        iid = b.get("installId") or (b.get("app") or {}).get("installId") or ""
        installs.append(iid)
        s = senders.setdefault(iid if INSTALL.match(iid) else "address", {"sent": 0, "grants": 0})
        limit = CAP + GRANT * s["grants"]
        if path == "/api/feedback/plain429":   # set up some plain 429s (no code) for the next reports
            plain429["left"] = int(b.get("n", 0))
            return self.reply(200, {"ok": True})
        if path == "/api/feedback":
            if not str(b.get("title", "")).strip():
                return self.reply(400, {"error": "Add a summary first: a few words saying what went wrong.", "code": "summary_required", "field": "title"})
            if plain429["left"] > 0:
                plain429["left"] -= 1
                return self.reply(429, {"error": "That's a lot of feedback at once. Try again in a while."})
            if s["sent"] >= limit:
                can = s["grants"] < 1
                msg = (f"You've sent {s['sent']} reports in the last day. Tap 'Request more' if you have another problem." if can
                       else f"You've sent {s['sent']} reports in the last day, and asked for more already. You can send another in about 23 hours.")
                return self.reply(429, {"error": msg, "code": "sender_cap", "sent": s["sent"], "limit": limit,
                                        "canRequestMore": can, "retryAfter": 82800}, {"retry-after": "82800"})
            s["sent"] += 1
            return self.reply(201, {"id": "20261031-120000-%08x" % hits[path], "uploadToken": "t", "chunkSize": 4_000_000})
        if path == "/api/feedback/complete":
            return self.reply(200, {"ok": True})
        if path == "/api/feedback/more":
            if s["sent"] < limit:
                return self.reply(409, {"error": f"You can still send {limit - s['sent']} more today. No need to ask.", "code": "not_capped"})
            if s["grants"] >= 1:
                return self.reply(429, {"error": "You've already been given more today. You can send another in about 23 hours.",
                                        "code": "more_used", "retryAfter": 82800}, {"retry-after": "82800"})
            s["grants"] += 1
            return self.reply(200, {"ok": True, "granted": GRANT, "limit": limit + GRANT, "sent": s["sent"]})
        self.reply(404, {"error": "Not here."})

srv = ThreadingHTTPServer(("127.0.0.1", 0), H)
open(sys.argv[1], "w").write(str(srv.server_address[1]))
srv.serve_forever()
