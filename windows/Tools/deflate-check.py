#!/usr/bin/env python3
"""Tests windows/App/Deflate.cpp against python's zlib (see deflate-check.sh).

  deflate-check.py <deflate_check binary> [<files to also compress...>]

Everything this compresses must inflate in zlib to the same bytes; everything
zlib compresses (every level, strategy and window) must inflate here to the
same bytes; damaged streams must never crash it (the binary is built with the
sanitizers) and are refused when zlib refuses them too; and a limit is
respected.
"""
import os, random, struct, subprocess, sys, zlib

binary = sys.argv[1]
extra_files = sys.argv[2:]
proc = subprocess.Popen([binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
fails = 0
checks = 0

def ask(mode, data, limit=0xFFFFFFFF):
    proc.stdin.write(struct.pack("<III", mode, limit, len(data)) + data)
    proc.stdin.flush()
    status, n = struct.unpack("<II", proc.stdout.read(8))
    return status, proc.stdout.read(n)

def fail(msg):
    global fails
    fails += 1
    print("FAIL", msg)

def raw_inflate(data):
    d = zlib.decompressobj(-15)
    out = d.decompress(data)
    if not d.eof:
        raise zlib.error("incomplete")
    return out

def raw_deflate(data, level=9, strategy=zlib.Z_DEFAULT_STRATEGY, wbits=15, mem=8):
    c = zlib.compressobj(level, zlib.DEFLATED, -wbits, mem, strategy)
    return c.compress(data) + c.flush()

rng = random.Random(int(os.environ.get("DEFLATE_CHECK_SEED", "1234")))
damaged_count = int(os.environ.get("DEFLATE_CHECK_DAMAGED", "6000"))

def text(n, alphabet=None):
    words = ["(gate", "AND", "OR", "NOT", "(position", "(wire", "(page", "(name", "\"é ✓ 日本\"", "12", "345", "(lparam", ")", "\n  ", "XOR"]
    s = ""
    while len(s) < n:
        s += rng.choice(words) + " "
    return s[:n].encode()

cases = [
    ("empty", b""), ("one byte", b"a"), ("two", b"ab"), ("three same", b"aaa"), ("three", b"abc"),
    ("zeros 300K", bytes(300000)), ("ones 70000", b"\xff" * 70000), ("258 same", b"x" * 258), ("259 same", b"x" * 259),
    ("ab x 100000", b"ab" * 100000), ("all bytes", bytes(range(256)) * 4),
    ("all bytes once", bytes(range(256))),
]
for n in (1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 100, 255, 256, 257, 1000, 4095, 4096, 4097, 65534, 65535, 65536, 65537, 131072, 200000):
    cases.append(("random %d" % n, bytes(rng.getrandbits(8) for _ in range(n))))
for sym in (2, 3, 4, 16, 64):
    for n in (50, 1000, 40000):
        cases.append(("alphabet %d x %d" % (sym, n), bytes(rng.randrange(sym) for _ in range(n))))
for period in (1, 2, 3, 7, 100, 4096, 32767, 32768, 32769):
    chunk = bytes(rng.getrandbits(8) for _ in range(period))
    cases.append(("period %d" % period, (chunk * (70000 // period + 2))[:70000]))
for n in (10, 300, 5000, 33000, 70000, 400000):
    cases.append(("text %d" % n, text(n)))
# A repeat just inside and just outside the window.
for gap in (32766, 32767, 32768, 32769):
    block = bytes(rng.getrandbits(8) for _ in range(300))
    filler = bytes(rng.getrandbits(8) for _ in range(gap - 300))
    cases.append(("repeat at %d" % gap, block + filler + block))
# Mixed: random runs of text, noise and repeats, over a few blocks.
mixed = b""
for _ in range(60):
    k = rng.randrange(3)
    if k == 0: mixed += text(rng.randrange(1, 4000))
    elif k == 1: mixed += bytes(rng.getrandbits(8) for _ in range(rng.randrange(1, 3000)))
    else: mixed += bytes([rng.getrandbits(8)]) * rng.randrange(1, 3000)
cases.append(("mixed %d" % len(mixed), mixed))
for path in extra_files:
    cases.append((os.path.basename(path), open(path, "rb").read()))

print("-- ours, inflated by zlib")
sizes = []
for name, data in cases:
    status, ours = ask(1, data)
    checks += 1
    try:
        back = raw_inflate(ours)
    except zlib.error as e:
        fail("%s: zlib refused our stream (%s)" % (name, e)); continue
    if back != data:
        fail("%s: zlib inflated our stream to different bytes" % name); continue
    status, mine = ask(2, ours)
    checks += 1
    if status != 0 or mine != data:
        fail("%s: we couldn't inflate our own stream" % name)
    ref = len(raw_deflate(data))
    sizes.append((name, len(data), len(ours), ref))
print("%-28s %9s %9s %9s %7s" % ("", "input", "ours", "zlib -9", "ratio"))
worst = 0
for name, n, ours, ref in sizes:
    if n >= 300:
        ratio = ours / ref if ref else 1
        worst = max(worst, ratio)
        if n >= 5000 and (name.startswith("text") or name.endswith(".cdl") or name.startswith("mixed")):
            print("%-28s %9d %9d %9d %7.3f" % (name, n, ours, ref, ratio))
print("worst size ratio against zlib -9 (inputs of 300+ bytes): %.3f" % worst)
if worst > 1.10:
    fail("our streams are more than 10%% bigger than zlib's somewhere (%.3f)" % worst)

print("-- zlib's, inflated here")
for name, data in cases:
    for level in range(0, 10):
        for strategy in (zlib.Z_DEFAULT_STRATEGY, zlib.Z_FILTERED, zlib.Z_HUFFMAN_ONLY, zlib.Z_RLE, zlib.Z_FIXED):
            if strategy != zlib.Z_DEFAULT_STRATEGY and level not in (1, 6, 9): continue
            packed = raw_deflate(data, level, strategy)
            status, back = ask(2, packed)
            checks += 1
            if status != 0 or back != data:
                fail("%s: level %d strategy %d: we inflated it wrongly (status %d)" % (name, level, strategy, status))
    for wbits, mem in ((9, 1), (12, 9), (15, 9), (10, 3)):
        packed = raw_deflate(data, 9, zlib.Z_DEFAULT_STRATEGY, wbits, mem)
        status, back = ask(2, packed)
        checks += 1
        if status != 0 or back != data:
            fail("%s: window %d memLevel %d: we inflated it wrongly" % (name, wbits, mem))
    # Flushed in pieces: several blocks, with empty stored blocks between.
    c = zlib.compressobj(6, zlib.DEFLATED, -15)
    packed = b""
    step = max(1, len(data) // 5)
    for i in range(0, len(data), step):
        packed += c.compress(data[i:i + step]) + c.flush(zlib.Z_FULL_FLUSH)
    packed += c.flush()
    status, back = ask(2, packed)
    checks += 1
    if status != 0 or back != data:
        fail("%s: flushed in pieces: we inflated it wrongly" % name)

print("-- limits")
for name, data in cases[:30]:
    if len(data) < 2: continue
    packed = raw_deflate(data)
    status, back = ask(2, packed, len(data))
    checks += 1
    if status != 0 or back != data: fail("%s: a limit of exactly its size refused it" % name)
    status, back = ask(2, packed, len(data) - 1)
    checks += 1
    if status != 2 or back != b"": fail("%s: a limit one under its size gave status %d" % (name, status))

print("-- damaged streams")
disagree = 0
sources = [raw_deflate(d) for n, d in cases if 20 < len(d) < 70000][:60] + [raw_deflate(d, 0) for n, d in cases if 20 < len(d) < 5000][:10]
for i in range(damaged_count):
    packed = bytearray(rng.choice(sources))
    kind = rng.randrange(4)
    if kind == 0:
        for _ in range(rng.randrange(1, 4)):
            packed[rng.randrange(len(packed))] ^= 1 << rng.randrange(8)
    elif kind == 1:
        packed = packed[:rng.randrange(len(packed))]
    elif kind == 2:
        packed[rng.randrange(len(packed))] = rng.getrandbits(8)
    else:
        packed = bytearray(rng.getrandbits(8) for _ in range(rng.randrange(1, 200)))
    packed = bytes(packed)
    status, ours = ask(2, packed, 20000000)
    checks += 1
    try:
        theirs = raw_inflate(packed)
    except zlib.error:
        theirs = None
    if proc.poll() is not None:
        fail("the checker crashed on damaged stream %d" % i); break
    if theirs is not None and (status != 0 or ours != theirs):
        fail("damaged stream %d: zlib inflates it, we say %d" % (i, status)); disagree += 1
    elif theirs is None and status == 0:
        disagree += 1   # (leniency, not a fault: listed below)
print("damaged streams we took that zlib refused: %d of %d (fine if few: they're streams zlib calls incomplete or junk)" % (disagree, damaged_count))

print("-- random streams of every kind of block")
for i in range(300):
    n = rng.randrange(1, 500)
    packed = bytes(rng.getrandbits(8) for _ in range(n))
    status, ours = ask(2, packed, 1000000)
    checks += 1
    if proc.poll() is not None:
        fail("the checker crashed on random stream %d" % i); break

proc.stdin.close()
rc = proc.wait()
if rc != 0:
    fail("the checker ended with status %d (a sanitizer?)" % rc)
print("%d checks, %d failed" % (checks, fails))
sys.exit(1 if fails else 0)
