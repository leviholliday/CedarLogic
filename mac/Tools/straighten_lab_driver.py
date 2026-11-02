#!/usr/bin/env python3
"""Overnight loop for mac/build/straighten_lab (started by straighten-lab.sh).

  straighten_lab_driver.py <tool> <lib.xml> <results dir> <hours> <fixture.cdl>...

Each cycle: every fixture page (as saved, and moved about with a fresh seed),
then a batch of random circuits whose size ramps from 3 to 300+ parts over the
run. Every run is a line in runs.jsonl. Only the worst KEEP cases (by badness)
and every failure keep their .cdl files and PNGs, under cases/. index.html is
rewritten every few minutes. Resumable: seeds continue from runs.jsonl.
"""
import html, json, os, random, shutil, subprocess, sys, time

TOOL, LIB, OUT, HOURS = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4])
FIXTURES = sys.argv[5:]
KEEP, MIN_FREE_GB, MAX_GB, CASE_TIMEOUT = 200, 3.0, 2.0, 90
CASES, STAGE = os.path.join(OUT, "cases"), os.path.join(OUT, "stage")
RUNS = os.path.join(OUT, "runs.jsonl")
os.makedirs(CASES, exist_ok=True)
os.makedirs(STAGE, exist_ok=True)
start, deadline = time.time(), time.time() + HOURS * 3600


def log(msg):
    print(time.strftime("%H:%M:%S"), msg, flush=True)


def free_gb():
    st = os.statvfs(OUT)
    return st.f_bavail * st.f_frsize / 1e9


def dir_gb(d):
    total = 0
    for root, _, files in os.walk(d):
        for f in files:
            try: total += os.path.getsize(os.path.join(root, f))
            except OSError: pass
    return total / 1e9


def badness(r):
    """Bigger is worse: what's still wrong after Straighten, per wire, plus
    what it made worse. Failures go to the top."""
    if r.get("fail"): return 1e6 + len(r["fail"])
    w = max(1, r.get("a_wires", 1))
    s = 0.0
    s += 40 * r.get("a_wrong_pin", 0) / w          # runs over a pin it isn't on
    s += 25 * r.get("a_through", 0) / w            # through a part's body
    s += 15 * r.get("a_overlaps", 0) / w           # shares a run with another wire
    s += 30 * max(0, r.get("a_split", 0) - r.get("b_split", 0))   # one net drawn as separate pieces
    s += 2 * r.get("a_corners", 0) / w             # bends per wire
    hp = max(1.0, r.get("a_hpwl", 1))
    s += 20 * max(0.0, r.get("a_length", hp) / hp - 1)   # detour
    s += 5 * max(0, r.get("a_crossings", 0) - r.get("b_crossings", 0)) / w
    s += 5 * max(0, r.get("a_corners", 0) - r.get("b_corners", 0)) / w
    s += min(10.0, r.get("secs", 0))
    return round(s, 3)


def case_id(r):
    if r["kind"] == "gen": return "gen-seed%d-n%d" % (r["seed"], r["target"])
    base = os.path.splitext(r["file"])[0].replace(" ", "_")
    return "fix-%s-p%d-seed%d" % (base, r["page"], r["seed"])


def render(cid, page):
    d = os.path.join(CASES, cid)
    for which in ("before", "after", "tidy"):
        cdl = os.path.join(d, which + ".cdl")
        if os.path.exists(cdl) and not os.path.exists(os.path.join(d, which + ".png")):
            try:
                subprocess.run([TOOL, "render", LIB, cdl, str(page), os.path.join(d, which + ".png"), "1200", "800"],
                               timeout=60, capture_output=True)
            except subprocess.TimeoutExpired:
                pass


# ---- state (resume) ----
records, kept = [], {}   # kept: case id -> record
if os.path.exists(RUNS):
    with open(RUNS) as f:
        for line in f:
            try: records.append(json.loads(line))
            except ValueError: pass
for r in records:
    if os.path.isdir(os.path.join(CASES, r.get("id", ""))): kept[r["id"]] = r
next_seed = 1 + max([r.get("seed", 0) for r in records if r.get("kind") == "gen"] or [1000])
log("resuming with %d runs, %d kept cases, next seed %d" % (len(records), len(kept), next_seed))


def consider(r, prefix):
    """Keep this case's files if it failed or is among the worst."""
    cid = r["id"]
    keep = bool(r.get("fail"))
    if not keep:
        normal = sorted((k for k in kept.values() if not k.get("fail")), key=lambda k: k["badness"])
        keep = len(normal) < KEEP or r["badness"] > normal[0]["badness"]
        if keep and len(normal) >= KEEP:
            drop = normal[0]
            shutil.rmtree(os.path.join(CASES, drop["id"]), ignore_errors=True)
            del kept[drop["id"]]
    if keep:
        d = os.path.join(CASES, cid)
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d)
        for which in ("before", "after", "tidy"):
            src = prefix + "." + which + ".cdl"
            if os.path.exists(src): shutil.move(src, os.path.join(d, which + ".cdl"))
        kept[cid] = r
        render(cid, r.get("page", 0))
    for which in ("before", "after", "tidy"):
        try: os.remove(prefix + "." + which + ".cdl")
        except OSError: pass


def run_case(args, head):
    prefix = os.path.join(STAGE, "case")
    t0 = time.time()
    try:
        p = subprocess.run([TOOL] + args + [prefix], timeout=CASE_TIMEOUT, capture_output=True, text=True)
        line = next((l for l in p.stdout.splitlines() if l.startswith("{")), None)
        if p.returncode < 0 or line is None:
            r = dict(head, fail=["crash" if p.returncode < 0 else "no_output"], returncode=p.returncode,
                     stderr=p.stderr[-400:])
        else:
            r = json.loads(line)
    except subprocess.TimeoutExpired:
        r = dict(head, fail=["timeout_killed"], secs=CASE_TIMEOUT)
    r["wall"] = round(time.time() - t0, 3)
    r["t"] = time.strftime("%Y-%m-%dT%H:%M:%S")
    r.setdefault("page", head.get("page", 0))
    r["id"] = case_id(r)
    r["badness"] = badness(r)
    with open(RUNS, "a") as f: f.write(json.dumps(r) + "\n")
    records.append(r)
    consider(r, prefix)
    if r.get("fail"): log("FAIL %s %s" % (r["id"], r["fail"]))
    return r


def img(cid, which):
    p = os.path.join("cases", cid, which + ".png")
    return '<a href="%s"><img src="%s" loading="lazy"></a>' % (p, p) if os.path.exists(os.path.join(OUT, p)) else "<div class=none>no image</div>"


def report():
    n = len(records)
    fails = [r for r in records if r.get("fail")]
    gen = [r for r in records if r.get("kind") == "gen" and not r.get("fail")]
    def avg(key, rs):
        rs = [r for r in rs if key in r]
        return sum(r[key] for r in rs) / len(rs) if rs else 0
    def tot(key, rs): return sum(r.get(key, 0) for r in rs)
    rows = []
    metrics = [("crossings", "crossings"), ("corners", "bends"), ("overlaps", "overlaps"), ("through", "thru parts"),
               ("wrong_pin", "wrong pin"), ("length", "length")]
    for r in sorted(kept.values(), key=lambda r: -r["badness"]):
        cells = []
        for k, label in metrics:
            b, a = r.get("b_" + k), r.get("a_" + k)
            t = r.get("t_" + k)
            if b is None: cells.append("<td>-</td>"); continue
            d = a - b
            cls = "worse" if d > 0 else "better" if d < 0 else ""
            cells.append('<td>%s &rarr; <b>%s</b> <span class="%s">(%+g)</span>%s</td>' % (
                round(b, 1), round(a, 1), cls, round(d, 1), "<br><small>tidy %s</small>" % round(t, 1) if t is not None else ""))
        hp = r.get("a_hpwl") or 0
        detour = "%.2f" % (r["a_length"] / hp) if hp and "a_length" in r else "-"
        rows.append("""<section><h3>%s <small>badness %s &middot; %s parts &middot; %s wires &middot; straighten %ss &middot; detour %s %s</small></h3>
<table><tr>%s</tr><tr>%s</tr></table>
<div class=imgs><figure>%s<figcaption>before</figcaption></figure><figure>%s<figcaption>after Straighten</figcaption></figure><figure>%s<figcaption>Tidy Up keep-shape</figcaption></figure></div>
<p class=files><a href="cases/%s/before.cdl">before.cdl</a> &middot; <a href="cases/%s/after.cdl">after.cdl</a> &middot; <a href="cases/%s/tidy.cdl">tidy.cdl</a></p></section>""" % (
            html.escape(r["id"]), r["badness"], r.get("b_parts", "?"), r.get("b_wires", "?"), r.get("secs", "?"), detour,
            '<span class=fail>FAIL: %s</span>' % html.escape(", ".join(r["fail"])) if r.get("fail") else "",
            "".join("<th>%s</th>" % l for _, l in metrics), "".join(cells),
            img(r["id"], "before"), img(r["id"], "after"), img(r["id"], "tidy"), r["id"], r["id"], r["id"]))
    summary = """<p>%d runs (%d random circuits, %d fixture runs) &middot; <b class=fail>%d failures</b> &middot; largest %d parts &middot; slowest Straighten %.2fs &middot; updated %s</p>
<p>Random circuits, totals before &rarr; after Straighten: crossings %d &rarr; %d, bends %d &rarr; %d, overlaps %d &rarr; %d,
wires through parts %d &rarr; %d, wires over a wrong pin %d &rarr; %d, length / lower bound %.3f &rarr; %.3f.</p>""" % (
        n, len([r for r in records if r.get("kind") == "gen"]), len([r for r in records if r.get("kind") == "file"]), len(fails),
        max([r.get("b_parts", 0) for r in records] or [0]), max([r.get("secs", 0) for r in records] or [0]),
        time.strftime("%Y-%m-%d %H:%M"),
        tot("b_crossings", gen), tot("a_crossings", gen), tot("b_corners", gen), tot("a_corners", gen),
        tot("b_overlaps", gen), tot("a_overlaps", gen), tot("b_through", gen), tot("a_through", gen),
        tot("b_wrong_pin", gen), tot("a_wrong_pin", gen),
        tot("b_length", gen) / max(1, tot("b_hpwl", gen)), tot("a_length", gen) / max(1, tot("a_hpwl", gen)))
    page = """<!doctype html><meta charset=utf-8><title>Straighten lab</title>
<style>body{font:14px -apple-system,sans-serif;margin:16px;background:#111;color:#ddd}a{color:#8cf}
section{border-top:1px solid #333;padding:8px 0}h3{margin:4px 0}small{color:#999;font-weight:normal}
table{border-collapse:collapse}td,th{border:1px solid #333;padding:3px 8px;font-size:12px;text-align:left}
.worse{color:#f77}.better{color:#7d7}.fail{color:#f55}.imgs{display:flex;gap:8px}figure{margin:0;flex:1}
img{width:100%%;border:1px solid #333}.none{color:#666;padding:40px}figcaption{color:#999;font-size:12px}</style>
<h1>Straighten lab</h1>%s<p>Worst first: every failure, then the %d highest badness scores. Raw data: <a href="runs.jsonl">runs.jsonl</a>, <a href="runs.csv">runs.csv</a>.</p>%s""" % (
        summary, KEEP, "\n".join(rows))
    with open(os.path.join(OUT, "index.html.tmp"), "w") as f: f.write(page)
    os.replace(os.path.join(OUT, "index.html.tmp"), os.path.join(OUT, "index.html"))
    keys = []
    for r in records:
        for k in r:
            if k not in keys and k not in ("stderr",): keys.append(k)
    with open(os.path.join(OUT, "runs.csv"), "w") as f:
        f.write(",".join(keys) + "\n")
        for r in records:
            f.write(",".join('"%s"' % ";".join(v) if isinstance(v, list) else str(v) for v in (r.get(k, "") for k in keys)) + "\n")


def fixture_pages(path):
    try:
        p = subprocess.run([TOOL, "pages", LIB, path], timeout=60, capture_output=True, text=True)
        return [int(x) for x in p.stdout.split()]
    except Exception:
        return []


rng = random.Random(next_seed)
pages = {fx: fixture_pages(fx) for fx in FIXTURES}
last_report, cycle = 0, 0
stop_reason = "deadline"
while time.time() < deadline:
    if os.path.exists(os.path.join(OUT, "STOP")): stop_reason = "STOP file"; break
    if free_gb() < MIN_FREE_GB: stop_reason = "disk under %g GB free" % MIN_FREE_GB; break
    if dir_gb(CASES) > MAX_GB: stop_reason = "results over %g GB" % MAX_GB; break
    cycle += 1
    for fx, ps in pages.items():
        for pg in ps:
            name = os.path.basename(fx)
            for mseed in (-1, next_seed):
                run_case(["file", LIB, fx, str(pg), str(mseed)], {"kind": "file", "file": name, "page": pg, "seed": mseed})
    frac = min(1.0, (time.time() - start) / max(1.0, deadline - start))
    for _ in range(25):
        top = 3 + 340 * frac ** 1.3
        n = max(3, int(rng.uniform(0.3, 1.0) * top))
        run_case(["gen", LIB, str(next_seed), str(n)], {"kind": "gen", "seed": next_seed, "target": n})
        next_seed += 1
        if time.time() >= deadline: break
    if time.time() - last_report > 300 or cycle == 1:
        report(); last_report = time.time()
        log("cycle %d: %d runs, %d failures, %d kept, next seed %d" % (cycle, len(records),
            len([r for r in records if r.get("fail")]), len(kept), next_seed))
report()
shutil.rmtree(STAGE, ignore_errors=True)
log("stopped: %s after %d runs" % (stop_reason, len(records)))
