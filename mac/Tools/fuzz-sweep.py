#!/usr/bin/env python3
"""Run a fuzzer over many seeds in parallel, one process a seed, with a time cap.

    fuzz-sweep.py [options] <binary> <first-seed> <seed-count> <ops> [start.cdl ...]

The binary is fuzz_edit (built by fuzz-check.sh) or fuzz_load: anything taking
"<gatedefs> <first-seed> <count> <ops> [start...]" and exiting 0 when a seed
passes. The CL_FUZZ_* settings are read from the environment as usual.

Each seed ends as passed, failed (the fuzzer's own check said so), crashed (a
sanitizer report or a signal) or unfinished (over the time cap: usually a seed
that pasted the page into itself until it is huge, not a fault). The output of
every seed that did not pass is kept in --logs. Exits 1 when any seed failed or
crashed.
"""
import argparse
import os
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def run_seed(args, seed):
    cmd = [args.binary, args.gatedefs, str(seed), "1", str(args.ops)] + args.starts
    t0 = time.time()
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=args.timeout)
        out = p.stdout.decode("utf-8", "replace")
        if p.returncode == 0:
            status = "passed"
        elif p.returncode == 1 and "ERROR: " not in out and "runtime error" not in out:
            status = "failed"
        else:
            status = "crashed"
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or b"").decode("utf-8", "replace")
        status = "unfinished"
    return seed, status, out, time.time() - t0


def first_line(out):
    for line in out.splitlines():
        if line.startswith("seed ") or "ERROR:" in line or "runtime error" in line or "Assertion" in line:
            return line.strip()
    return out.strip().splitlines()[-1] if out.strip() else ""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 2))
    ap.add_argument("--timeout", type=float, default=120, help="seconds a seed may take")
    ap.add_argument("--logs", default=os.path.join(ROOT, "mac", "build", "fuzz-logs"))
    ap.add_argument("--gatedefs", default=os.path.join(ROOT, "res", "cl_gatedefs.xml"))
    ap.add_argument("binary")
    ap.add_argument("first", type=int)
    ap.add_argument("count", type=int)
    ap.add_argument("ops", type=int)
    ap.add_argument("starts", nargs="*")
    args = ap.parse_args()
    os.makedirs(args.logs, exist_ok=True)

    tally = {"passed": [], "failed": [], "crashed": [], "unfinished": []}
    t0 = time.time()
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for seed, status, out, secs in pool.map(lambda s: run_seed(args, s), range(args.first, args.first + args.count)):
            tally[status].append(seed)
            if status != "passed":
                with open(os.path.join(args.logs, "seed%d.log" % seed), "w") as f:
                    f.write(out)
                print("seed %d %s (%.0f s): %s" % (seed, status, secs, first_line(out)), flush=True)
    print("%d seeds in %.0f s: %d passed, %d failed, %d crashed, %d unfinished"
          % (args.count, time.time() - t0, len(tally["passed"]), len(tally["failed"]),
             len(tally["crashed"]), len(tally["unfinished"])))
    for k in ("failed", "crashed", "unfinished"):
        if tally[k]:
            print("  %s: %s" % (k, " ".join(map(str, sorted(tally[k])))))
    return 1 if tally["failed"] or tally["crashed"] else 0


if __name__ == "__main__":
    sys.exit(main())
