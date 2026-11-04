# A stand-in for cedarlogic.netlify.app's /api/usage, as the contract in
# docs/PRIVACY.md describes it, for mac/Tools/usage-check.sh:
#   POST   /api/usage          a report -> 200 {"ok": true, "haveSnapshot": bool}
#   DELETE /api/usage?id=<id>  forget the id -> 200 {"ok": true}
# Every report is checked against the contract's shape; one that doesn't
# fit is answered 400 and noted. Test controls (never part of the real API):
#   POST /control {"fail": n, "status": 500, "tooMany": n, "noSnapshot": n, "reset": true}
#   GET  /log     every request: method, path, user agent, body, problems
#   GET  /store   what's held per id (the settings it has, reports seen)
#   python3 server.py <port file>
import hashlib, json, re, sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

ID = re.compile(r"^[A-Za-z0-9_-]{22,64}$")
MINUTE = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}Z$")
DAY = re.compile(r"^\d{4}-\d{2}-\d{2}$")
HEX = re.compile(r"^[0-9a-f]{64}$")
# The words the real server drops a setting for (cedarlogic-site netlify/lib/usage.mjs, SENSITIVE):
# an allowlisted name made of one of them would never be stored.
SENSITIVE = set("""name names username user users email mail phone address path paths folder folders dir directory file
files filename recent recents token tokens secret password passcode pin key keys code codes url urls host server account id
uuid device devices ip location school student students teacher signature author""".split())
def words(k): return [w for w in re.split(r"[\s._]+", re.sub(r"([a-z0-9])([A-Z])", r"\1 \2", k).lower()) if w]
COUNTERS = {"sessions", "activeSeconds", "focusSeconds", "simViewSeconds", "classroomSessions", "syncOn", "actions"}

log, store = [], {}
ctl = {"fail": 0, "status": 500, "tooMany": 0, "noSnapshot": 0}

def canonical(settings):
    return json.dumps(settings, sort_keys=True, separators=(",", ":"))

def problems(b, raw):
    p = []
    if len(raw) > 16 * 1024: p.append("over 16 KB")
    if b.get("v") != 1: p.append("v")
    if not isinstance(b.get("id"), str) or not ID.match(b["id"]): p.append("id")
    app = b.get("app") or {}
    if set(app) != {"platform", "version", "build", "os", "arch", "lang"}: p.append("app keys %s" % sorted(app))
    if app.get("platform") != "mac": p.append("platform")
    if not isinstance(app.get("build"), int): p.append("build")
    if not re.match(r"^\d+\.\d+$", str(app.get("os", ""))): p.append("os is major.minor")
    if not re.match(r"^[a-z]{2,3}$", str(app.get("lang", ""))): p.append("lang")
    kind = b.get("kind")
    if kind not in ("snapshot", "delta", "ping"): p.append("kind")
    if not MINUTE.match(str(b.get("sentAt", ""))): p.append("sentAt")
    per = b.get("period") or {}
    if not (DAY.match(str(per.get("from", ""))) and DAY.match(str(per.get("to", "")))): p.append("period")
    if not HEX.match(str(b.get("settingsHash", ""))): p.append("settingsHash")
    s = b.get("settings")
    if kind == "ping":
        if s is not None: p.append("ping with settings")
    elif not isinstance(s, dict): p.append("settings missing")
    else:
        for k, v in s.items():
            if not (isinstance(v, (bool, int, float)) or (isinstance(v, str) and len(v) <= 32)): p.append("setting %s" % k)
            if any(w in SENSITIVE for w in words(k)): p.append("setting %s would be dropped (a personal-looking word)" % k)
            if isinstance(v, str) and (re.search(r"[@/\\]", v) or re.match(r"^[A-Za-z0-9_+=-]{20,}$", v)): p.append("setting %s's value would be dropped" % k)
    c = b.get("counters")
    if c is not None:
        if set(c) != COUNTERS: p.append("counter keys %s" % sorted(c))
        if not isinstance(c.get("syncOn"), bool): p.append("syncOn")
        acts = c.get("actions") or {}
        if len(acts) > 200: p.append("over 200 actions")
        if not all(isinstance(v, int) for v in acts.values()): p.append("action counts")
    elif kind != "ping": p.append("counters missing")
    extra = set(b) - {"v", "id", "app", "kind", "sentAt", "period", "settings", "settingsHash", "counters"}
    if extra: p.append("unknown keys %s" % sorted(extra))
    return p

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass

    def reply(self, code, body):
        data = json.dumps(body).encode()
        self.send_response(code)
        self.send_header("content-type", "application/json")
        self.send_header("content-length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def body(self):
        n = int(self.headers.get("content-length") or 0)
        return self.rfile.read(n) if n else b""

    def do_GET(self):
        if self.path == "/log": return self.reply(200, log)
        if self.path == "/store": return self.reply(200, store)
        self.reply(404, {"error": "not here"})

    def do_DELETE(self):
        u = urlparse(self.path)
        ident = (parse_qs(u.query).get("id") or [""])[0]
        log.append({"method": "DELETE", "path": u.path, "id": ident, "ua": self.headers.get("user-agent", "")})
        if u.path != "/api/usage" or not ID.match(ident): return self.reply(400, {"error": "bad id"})
        store.pop(ident, None)
        self.reply(200, {"ok": True})

    def do_POST(self):
        u = urlparse(self.path)
        raw = self.body()
        if u.path == "/control":
            c = json.loads(raw or b"{}")
            if c.get("reset"):
                log.clear(); store.clear(); ctl.update({"fail": 0, "status": 500, "tooMany": 0, "noSnapshot": 0})
            for k in ("fail", "status", "tooMany", "noSnapshot"):
                if k in c: ctl[k] = c[k]
            return self.reply(200, {"ok": True})
        if u.path != "/api/usage": return self.reply(404, {"error": "not here"})
        try: b = json.loads(raw)
        except Exception: b = {}
        entry = {"method": "POST", "path": u.path, "ua": self.headers.get("user-agent", ""), "body": b, "bytes": len(raw),
                 "raw": raw.decode("utf-8", "replace")}
        log.append(entry)
        if ctl["fail"] > 0:
            ctl["fail"] -= 1; entry["answer"] = ctl["status"]
            return self.reply(ctl["status"], {"error": "down"})
        if ctl["tooMany"] > 0:
            ctl["tooMany"] -= 1; entry["answer"] = 429
            return self.reply(429, {"error": "slow down"})
        bad = problems(b, raw)
        entry["problems"] = bad
        if bad:
            entry["answer"] = 400
            return self.reply(400, {"error": "; ".join(bad)})
        held = store.get(b["id"])
        if b["kind"] == "snapshot":
            held = {"settings": dict(b["settings"]), "reports": 0}
            entry["hashOk"] = hashlib.sha256(canonical(b["settings"]).encode()).hexdigest() == b["settingsHash"]
        elif held is not None and b["kind"] == "delta":
            held["settings"].update(b["settings"])
        if held is not None:
            held["reports"] += 1
            store[b["id"]] = held
        have = held is not None
        if ctl["noSnapshot"] > 0:
            ctl["noSnapshot"] -= 1; have = False; store.pop(b["id"], None)
        entry["answer"] = 200
        self.reply(200, {"ok": True, "haveSnapshot": have})

s = ThreadingHTTPServer(("127.0.0.1", 0), H)
open(sys.argv[1], "w").write(str(s.server_address[1]))
s.serve_forever()
