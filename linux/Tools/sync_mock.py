#!/usr/bin/env python3
"""A small stand-in for CedarLogic's sync server (docs/SYNC.md section 3), for
CI and for trying the Linux app without the website: the same paths, headers,
bodies and status codes, kept in memory, over plain HTTP on this machine.

    python3 linux/Tools/sync_mock.py [--port 0]

The first line it prints, once it is listening, is JSON:
    {"event":"listening","port":8787,"url":"http://localhost:8787/api/sync/v1"}
Point the app at that url with CL_SYNC_URL (plain http is allowed for localhost
only). It is not the real server: no rate limits, no quotas beyond the
record caps, no clean-up, no test controls. The engine's full self-test
(scenarios against /__mock/ controls) needs the website's own mock server
(cedarlogic-site: scripts/sync-mock-server.mjs); this one is enough to see
two libraries turn on, link, edit, delete and stop syncing.
"""
import argparse
import base64
import hashlib
import json
import os
import re
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

UUID_RE = re.compile(r"^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$")
SPACE_RE = re.compile(r"^[0-9a-f]{32}$")
LIMITS = {"maxRecordBytes": 524288, "maxSmallRecordBytes": 4096, "maxRecords": 1000, "maxDevices": 20,
          "maxBytes": 10485760, "maxEntries": 5000, "maxFetchIds": 50, "maxWriteItems": 50, "pollSeconds": 600}
MESSAGES = {
    "bad_request": "That request wasn't understood.",
    "wrong_code": "This code doesn't match the synced circuits.",
    "wrong_delete_token": "That code can't delete the synced copy.",
    "no_space": "No circuits are synced with this code.",
    "not_found": "Not here.",
    "method_not_allowed": "That can't be done here.",
    "space_deleted": "The synced copy was deleted from another device.",
    "too_large": "That's too much to send at once.",
}

LOCK = threading.Lock()
AUTH, SPACES, GONE = {}, {}, {}


def now():
    return int(time.time() * 1000)


def sha(s):
    return hashlib.sha256(s.encode()).hexdigest()


def unb64u(s):
    return base64.urlsafe_b64decode(s + "=" * (-len(s) % 4))


def entry(rid, r):
    return {"id": rid, "ver": r["ver"], "seq": r["seq"], "size": r["size"], "updatedAt": r["at"], "deleted": r["deleted"], "h": r["h"]}


def status(sp, **extra):
    return {**extra, "epoch": sp["epoch"], "seq": sp["seq"], "purgedSeq": sp["purgedSeq"], "count": sp["count"],
            "bytes": sp["bytes"], "devices": sp["devices"], "createdAt": sp["createdAt"], "activeAt": sp["activeAt"],
            "limits": LIMITS}


def error(code, status_code, **extra):
    return status_code, {"error": code, "message": MESSAGES.get(code, code), **extra}


def space_for(sid, token):
    if sid in GONE:
        return None, error("space_" + GONE[sid], 410)
    a, sp = AUTH.get(sid), SPACES.get(sid)
    if a is None:
        return None, error("no_space", 404)
    if sha(token) != a["authHash"]:
        return None, error("wrong_code", 401)
    if sp is None:
        return None, error("no_space", 404)
    sp["activeAt"] = now()
    return sp, None


def put_space(sid, token, body):
    if sid in GONE:
        return error("space_" + GONE[sid], 410)
    a = AUTH.get(sid)
    if a is not None and sha(token) != a["authHash"]:
        return error("wrong_code", 401)
    if a is None:
        dh = body.get("deleteHash", "") if isinstance(body, dict) else ""
        if not re.match(r"^[0-9a-f]{64}$", dh or ""):
            return error("bad_request", 400)
        AUTH[sid] = {"authHash": sha(token), "deleteHash": dh}
    sp = SPACES.get(sid)
    if sp is not None:
        return 200, status(sp, created=False)
    t = now()
    SPACES[sid] = sp = {"epoch": os.urandom(16).hex(), "createdAt": t, "activeAt": t, "seq": 0, "purgedSeq": 0,
                        "count": 0, "bytes": 0, "devices": 0, "recs": {}}
    return 201, status(sp, created=True)


def write(sp, items):
    if not isinstance(items, list) or not items or len(items) > LIMITS["maxWriteItems"]:
        return error("bad_request", 400)
    results = []
    for w in items:
        rid, base, ver, deleted = w.get("id"), w.get("base"), w.get("ver"), bool(w.get("deleted"))
        device = bool(w.get("device")) and not deleted
        if not (isinstance(rid, str) and UUID_RE.match(rid) and isinstance(base, int) and isinstance(ver, int)
                and 0 <= base < ver <= 2 ** 53 - 1 and isinstance(w.get("data"), str)):
            results.append({"id": rid, "status": 400, "error": "bad_request"})
            continue
        try:
            env = unb64u(w["data"])
        except Exception:
            results.append({"id": rid, "status": 400, "error": "bad_request"})
            continue
        if len(env) < 30 or env[0] != 1 or env[1] not in (0, 1):
            results.append({"id": rid, "status": 400, "error": "bad_request"})
            continue
        if len(env) > (LIMITS["maxSmallRecordBytes"] if deleted or device else LIMITS["maxRecordBytes"]):
            results.append({"id": rid, "status": 413, "error": "record_too_large"})
            continue
        h = hashlib.sha256(env).hexdigest()[:32]
        cur = sp["recs"].get(rid)
        if cur and cur["ver"] == ver and cur["h"] == h:   # the same write again
            results.append({"id": rid, "status": 200, "entry": entry(rid, cur)})
            continue
        if base != (cur["ver"] if cur else 0):
            results.append({"id": rid, "status": 412, "error": "conflict", "current": entry(rid, cur) if cur else None})
            continue
        was = None if cur is None or cur["deleted"] else ("dev" if cur.get("dev") else "rec")
        nowk = None if deleted else ("dev" if device else "rec")
        count = sp["count"] + (nowk == "rec") - (was == "rec")
        devices = sp["devices"] + (nowk == "dev") - (was == "dev")
        total = sp["bytes"] - (cur["size"] if was == "rec" else 0) + (len(env) if nowk == "rec" else 0)
        grows = nowk is not None and (was != nowk or (nowk == "rec" and len(env) > (cur["size"] if cur else 0)))
        if grows and (count > LIMITS["maxRecords"] or total > LIMITS["maxBytes"] or devices > LIMITS["maxDevices"]):
            results.append({"id": rid, "status": 507, "error": "space_full", "count": sp["count"], "bytes": sp["bytes"]})
            continue
        sp["seq"] += 1
        sp["recs"][rid] = {"ver": ver, "seq": sp["seq"], "size": len(env), "at": now(), "deleted": deleted,
                           "data": w["data"], "h": h, "dev": device}
        sp["count"], sp["bytes"], sp["devices"] = count, total, devices
        results.append({"id": rid, "status": 201 if cur is None else 200, "entry": entry(rid, sp["recs"][rid])})
    return 200, {"results": results, "seq": sp["seq"]}


def handle(method, path, query, headers, body):
    m = re.match(r"^/api/sync/v1(/.*)?$", path)
    if not m:
        return error("not_found", 404)
    rest = m.group(1) or ""
    if rest == "/health":
        return 200, {"ok": True, "protocol": 1, "limits": LIMITS}
    m = re.match(r"^/spaces/([^/]+)(/changes|/fetch|/write)?$", rest)
    if not m or not SPACE_RE.match(m.group(1)):
        return error("not_found", 404)
    sid, sub = m.group(1), m.group(2)
    auth = headers.get("authorization", "")
    if not auth.startswith("Bearer "):
        return error("wrong_code", 401)
    token = auth[7:]
    try:
        data = json.loads(body) if body else {}
    except ValueError:
        return error("bad_request", 400)
    with LOCK:
        if sub is None and method == "PUT":
            return put_space(sid, token, data)
        sp, err = space_for(sid, token)
        if err:
            return err
        if sub is None and method == "GET":
            return 200, status(sp)
        if sub is None and method == "DELETE":
            if sha(headers.get("x-cedarlogic-delete", "")) != AUTH[sid]["deleteHash"]:
                return error("wrong_delete_token", 403)
            del SPACES[sid], AUTH[sid]
            GONE[sid] = "deleted"
            return 200, {"deleted": True}
        if sub == "/changes" and method == "GET":
            since = int(query.get("since", ["0"])[0])
            limit = max(1, min(1000, int(query.get("limit", ["1000"])[0])))
            es = sorted((entry(i, r) for i, r in sp["recs"].items() if r["seq"] > since), key=lambda e: e["seq"])
            more = len(es) > limit
            es = es[:limit]
            return 200, {"epoch": sp["epoch"], "seq": sp["seq"], "purgedSeq": sp["purgedSeq"], "entries": es,
                         "more": more, "next": es[-1]["seq"] if more else sp["seq"], "pollSeconds": LIMITS["pollSeconds"]}
        if sub == "/fetch" and method == "POST":
            ids = data.get("ids", [])
            if not isinstance(ids, list) or len(ids) > LIMITS["maxFetchIds"]:
                return error("bad_request", 400)
            out, missing = [], []
            for i in dict.fromkeys(ids):
                r = sp["recs"].get(i)
                if r is None:
                    missing.append(i)
                else:
                    out.append({**entry(i, r), "data": r["data"]})
            return 200, {"records": out, "missing": missing, "deferred": []}
        if sub == "/write" and method == "POST":
            return write(sp, data.get("writes"))
    return error("method_not_allowed", 405)


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def _go(self):
        url = urlparse(self.path)
        n = int(self.headers.get("content-length") or 0)
        body = self.rfile.read(n) if n else b""
        headers = {k.lower(): v for k, v in self.headers.items()}
        status_code, payload = handle(self.command, url.path, parse_qs(url.query), headers, body)
        out = json.dumps(payload).encode()
        self.send_response(status_code)
        self.send_header("content-type", "application/json")
        self.send_header("cache-control", "no-store")
        self.send_header("content-length", str(len(out)))
        self.end_headers()
        self.wfile.write(out)

    do_GET = do_PUT = do_POST = do_DELETE = _go


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=int(os.environ.get("PORT", "0")))
    args = ap.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    port = server.server_address[1]
    print(json.dumps({"event": "listening", "port": port, "url": "http://localhost:%d/api/sync/v1" % port},
                     separators=(",", ":")), flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
