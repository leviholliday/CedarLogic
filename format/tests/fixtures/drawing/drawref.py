"""Reference implementation of docs/DRAWING-NOTES.md: the ink codec, reading and
writing the (drawing ...), (notes ...) and (show-drawing ...) nodes of a v3
.cdl, the sync structure lines they add, and the stroke simplifier. Python 3,
standard library only. It is the oracle for the fixtures in this folder:

    python3 scripts/fixtures/drawing/drawref.py --check      # every fixture agrees
    python3 scripts/fixtures/drawing/drawref.py --generate   # rewrite them (review the diff!)
    python3 scripts/fixtures/drawing/drawref.py --sizes      # the size table in the spec

Nothing here is used by the site or the apps at run time.
"""

from __future__ import annotations

import json
import math
import os
import re
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
CDL = os.path.join(HERE, "cdl")

# ------------------------------------------------------------------ limits --

ALPHA = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
AIDX = {c: i for i, c in enumerate(ALPHA)}
MAX_ABS = 100_000_000          # centi-units: |coordinate| <= 1,000,000 world units
MAX_VLQ_CHARS = 6              # 30 bits: enough for MAX_ABS, and safe for 32-bit shifts
TOKEN = re.compile(r"^[a-z][a-z0-9-]{0,31}$")
DRAWING_VERSION = 1

# What a reader accepts (generous: a later writer may allow more).
READ_STROKE_POINTS = 10_000
READ_PAGE_STROKES = 10_000
READ_PAGE_POINTS = 100_000
READ_DOC_POINTS = 300_000
READ_NOTES_CHARS = 200_000
# What the editors let a person make (and what writers produce).
WRITE_STROKE_POINTS = 2_000
WRITE_PAGE_STROKES = 1_000
WRITE_PAGE_POINTS = 20_000
WRITE_DOC_POINTS = 60_000
WRITE_NOTES_CHARS = 20_000

WIDTH_MIN_C, WIDTH_MAX_C = 2, 2000   # 0.02 .. 20 world units, in centi-units


class InkError(ValueError):
    def __init__(self, kind: str):
        super().__init__(kind)
        self.kind = kind


# ---------------------------------------------------------------- the codec --

def q(v: float) -> int:
    """World units -> centi-units (the stored quantum), half up."""
    return math.floor(v * 100 + 0.5)


def vlq_encode(v: int) -> str:
    n = ((-v) << 1) | 1 if v < 0 else v << 1
    out = ""
    while True:
        d = n & 31
        n >>= 5
        if n:
            d |= 32
        out += ALPHA[d]
        if not n:
            return out


def vlq_decode_all(s: str) -> list[int]:
    vals, n, shift, chars = [], 0, 0, 0
    for c in s:
        d = AIDX.get(c)
        if d is None:
            raise InkError("bad-char")
        chars += 1
        if chars > MAX_VLQ_CHARS:
            raise InkError("too-long")
        n |= (d & 31) << shift
        shift += 5
        if d & 32:
            continue
        v = -(n >> 1) if n & 1 else n >> 1
        if abs(v) > MAX_ABS:
            raise InkError("out-of-range")
        vals.append(v)
        n, shift, chars = 0, 0, 0
    if chars:
        raise InkError("cut-short")
    return vals


def encode_points(centi: list[tuple[int, int]]) -> str:
    out, px, py = [], 0, 0
    for x, y in centi:
        out.append(vlq_encode(x - px) + vlq_encode(y - py))
        px, py = x, y
    return "".join(out)


def decode_points(s: str, limit: int = READ_STROKE_POINTS) -> list[tuple[int, int]]:
    vals = vlq_decode_all(s)
    if not vals:
        raise InkError("empty")
    if len(vals) % 2:
        raise InkError("odd")
    if len(vals) // 2 > limit:
        raise InkError("too-many-points")
    pts, x, y = [], 0, 0
    for i in range(0, len(vals), 2):
        x += vals[i]
        y += vals[i + 1]
        if abs(x) > MAX_ABS or abs(y) > MAX_ABS:
            raise InkError("out-of-range")
        pts.append((x, y))
    return pts


def encode_pressure(ps: list[float]) -> str:
    return "".join(ALPHA[min(63, max(0, math.floor(p * 63 + 0.5)))] for p in ps)


def pressure_ok(s: str, n: int) -> bool:
    return len(s) == n and all(c in AIDX for c in s)


# ---------------------------------------------- making a stroke from samples --

def _seg_dist(p, a, b) -> float:
    ax, ay = a
    bx, by = b
    px, py = p
    dx, dy = bx - ax, by - ay
    L = dx * dx + dy * dy
    if L == 0:
        return math.hypot(px - ax, py - ay)
    t = ((px - ax) * dx + (py - ay) * dy) / L
    t = 0.0 if t < 0 else 1.0 if t > 1 else t
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def simplify(pts: list[tuple[float, float]], eps: float) -> list[int]:
    """Ramer-Douglas-Peucker over world points: the indices kept. The first
    and last always stay; a point stays when it is farther than eps (strictly)
    from the segment between the kept points around it; ties go to the
    lowest index."""
    n = len(pts)
    if n <= 2:
        return list(range(n))
    keep = [False] * n
    keep[0] = keep[-1] = True
    stack = [(0, n - 1)]
    while stack:
        a, b = stack.pop()
        best, bi = -1.0, -1
        for i in range(a + 1, b):
            d = _seg_dist(pts[i], pts[a], pts[b])
            if d > best:
                best, bi = d, i
        if bi >= 0 and best > eps:
            keep[bi] = True
            stack.append((a, bi))
            stack.append((bi, b))
    return [i for i in range(n) if keep[i]]


def make_stroke(tool, color, width, samples, pressure=None, eps=0.01):
    """samples: world (x, y) floats as captured. Simplify, quantize, drop
    repeats; returns the stroke as the model keeps it."""
    idx = simplify(samples, eps)
    centi, pr = [], []
    for i in idx:
        c = (q(samples[i][0]), q(samples[i][1]))
        if centi and centi[-1] == c:
            continue
        centi.append(c)
        if pressure is not None:
            pr.append(pressure[i])
    st = {"tool": tool, "color": color, "w": min(WIDTH_MAX_C, max(WIDTH_MIN_C, q(width))), "pts": centi}
    if pressure is not None:
        st["pr"] = encode_pressure(pr)
    return st


# ------------------------------------------------------------ s-expressions --
# Mirrors format/sexpr.cpp: nodes are ("list", [..], span) | ("sym", text, span)
# | ("str", text, span); span = (start, end) offsets in the source.

class SexprError(ValueError):
    pass


def parse_sexpr(t: str):
    i, depth = 0, 0
    n = len(t)

    def ws():
        nonlocal i
        while i < n and t[i] in " \t\r\n":
            i += 1

    def node():
        nonlocal i, depth
        ws()
        if i >= n:
            raise SexprError("unexpected end of input")
        c = t[i]
        st = i
        if c == "(":
            depth += 1
            if depth > 64:
                raise SexprError("nesting too deep")
            i += 1
            items = []
            while True:
                ws()
                if i >= n:
                    raise SexprError("unbalanced '('")
                if t[i] == ")":
                    i += 1
                    depth -= 1
                    return ("list", items, (st, i))
                items.append(node())
        if c == ")":
            raise SexprError("unexpected ')'")
        if c == '"':
            i += 1
            out = []
            while i < n:
                ch = t[i]
                i += 1
                if ch == "\\":
                    if i < n:
                        out.append(t[i])
                        i += 1
                elif ch == '"':
                    return ("str", "".join(out), (st, i))
                else:
                    out.append(ch)
            raise SexprError("unterminated string")
        while i < n and t[i] not in ' \t\r\n()"':
            i += 1
        return ("sym", t[st:i], (st, i))

    root = node()
    if t[i:].strip(" \t\r\n"):
        raise SexprError("trailing content after the document")
    return root


def head(x) -> str:
    return x[1][0][1] if x[0] == "list" and x[1] and x[1][0][0] == "sym" else ""


def atom(x, k):
    """Item k of a list as text, or None when missing or a list."""
    if x[0] != "list" or k >= len(x[1]) or x[1][k][0] == "list":
        return None
    return x[1][k][1]


def write_atom_str(s: str) -> str:
    """DRAWING-NOTES.md 3.4: '\\' and '"' are escaped, and so is the character
    after every '<', so no string can put "<version>" into the file's bytes."""
    out, after_lt = ['"'], False
    for ch in s:
        if ch in '\\"' or after_lt:
            out.append("\\")
        out.append(ch)
        after_lt = ch == "<"
    out.append('"')
    return "".join(out)


def write_node(x, depth=0) -> str:
    if x[0] == "sym":
        return x[1]
    if x[0] == "str":
        return write_atom_str(x[1])
    out = "("
    first = True
    for c in x[1]:
        if c[0] == "list":
            out += "\n" + "  " * (depth + 1)
        elif not first:
            out += " "
        out += write_node(c, depth + 1)
        first = False
    return out + ")"


def L(*items):
    return ("list", list(items), None)


def S(t):
    return ("sym", t, None)


def Q(t):
    return ("str", t, None)


# ------------------------------------------------------------ the numbers --

NUM = re.compile(r"^[+-]?(\d+(\.\d*)?|\.\d+)([eE][+-]?\d+)?$")


def g10(v: float) -> str:
    """format/circuit_file_io.cpp num(): integral below 1e15 bare, else %.10g."""
    if v == math.floor(v) and abs(v) < 1e15:
        return str(int(v))
    return "%.10g" % v


def width_text(wc: int) -> str:
    return g10(wc / 100)


# ------------------------------------------------------------- reading --

def read_stroke(x):
    """A (stroke ...) list -> the stroke, or raises InkError(kind)."""
    items = x[1]
    if len(items) < 5:
        raise InkError("short")
    for k in range(1, 5):
        if items[k][0] == "list":
            raise InkError("list-in-value")
    tool, color, wtext, ptext = (items[k][1] for k in range(1, 5))
    if not NUM.match(wtext.strip(" \t\r\n")):
        raise InkError("bad-width")
    w = float(wtext)
    if not math.isfinite(w) or w <= 0:
        raise InkError("bad-width")
    st = {
        "tool": tool if TOKEN.match(tool) else "pen",
        "color": color if TOKEN.match(color) else "ink",
        "w": min(WIDTH_MAX_C, max(WIDTH_MIN_C, q(w))),
        "pts": decode_points(ptext),
    }
    if len(items) >= 6 and items[5][0] != "list" and pressure_ok(items[5][1], len(st["pts"])):
        st["pr"] = items[5][1]
    return st


def read_ext(text: str) -> dict:
    """The drawing, notes and show flag of a v3 document (anything else in it
    is the ordinary reader's business)."""
    root = parse_sexpr(text.lstrip("﻿"))
    res = {"notes": "", "hidden": False, "pages": {}, "notices": []}
    notes_seen = flag_seen = False
    doc_points = 0
    dropped = 0
    for c in root[1]:
        h = head(c)
        if h == "notes" and not notes_seen:
            notes_seen = True
            v = atom(c, 1)
            if v is not None:
                res["notes"] = normalize_notes(v, READ_NOTES_CHARS)
        elif h == "show-drawing" and not flag_seen:
            flag_seen = True
            res["hidden"] = atom(c, 1) == "no"
        elif h == "page":
            ix = atom(c, 1)
            if ix is None or not re.match(r"^[+-]?\d+$", ix):
                continue
            ix = int(ix)
            drawings = [e for e in c[1] if head(e) == "drawing"]
            if not drawings:
                continue
            pg = res["pages"].setdefault(ix, {"strokes": [], "foreign": [], "readOnly": False})
            versions = [atom(d, 1) for d in drawings]
            if any(v is None or not re.match(r"^\d+$", v) or int(v) != DRAWING_VERSION for v in versions):
                pg["readOnly"] = True
                pg["foreign"] += drawings      # kept whole, written again as they are (3.5)
                continue
            page_points = 0
            for d in drawings:
                for s in d[1]:
                    if head(s) != "stroke":
                        continue
                    try:
                        st = read_stroke(s)
                    except InkError:
                        dropped += 1
                        continue
                    n = len(st["pts"])
                    if (len(pg["strokes"]) >= READ_PAGE_STROKES or page_points + n > READ_PAGE_POINTS
                            or doc_points + n > READ_DOC_POINTS):
                        dropped += 1
                        continue
                    page_points += n
                    doc_points += n
                    pg["strokes"].append(st)
    res["pages"] = {k: v for k, v in sorted(res["pages"].items()) if v["strokes"] or v["foreign"]}
    if dropped:
        res["notices"].append(f"drawing: {dropped} left out")
    return res


# -------------------------------------------------------------- notes --

_CTRL = re.compile("[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]")


def normalize_notes(s: str, cap: int = READ_NOTES_CHARS) -> str:
    s = s.lstrip("﻿").replace("\r\n", "\n").replace("\r", "\n")
    s = _CTRL.sub("", s)
    s = "".join("�" if 0xD800 <= ord(ch) <= 0xDFFF else ch for ch in s)
    return s[:cap]


def notes_worth_writing(s: str) -> bool:
    return any(ch not in " \t\n" for ch in s)


# -------------------------------------------------------------- writing --

def stroke_node(st):
    items = [S("stroke"), S(st["tool"]), S(st["color"]), S(width_text(st["w"])), Q(encode_points(st["pts"]))]
    if st.get("pr"):
        items.append(Q(st["pr"]))
    return L(*items)


def write_with_ext(base_text: str, ext: dict) -> str:
    """Puts ext (notes, hidden, pages{ix: strokes}) into a v3 document
    written by the apps (no drawing in it yet): the layout of 3.3."""
    root = parse_sexpr(base_text)
    items = [c for c in root[1] if head(c) not in ("notes", "show-drawing")]
    out = []
    any_drawing = any(p.get("strokes") or p.get("foreign") for p in ext.get("pages", {}).values())
    for c in items:
        if c[0] == "list" and head(c) == "page":
            if not any(head(o) == "page" for o in out):
                if notes_worth_writing(ext.get("notes", "")):
                    out.append(L(S("notes"), Q(ext["notes"])))
                if ext.get("hidden") and any_drawing:
                    out.append(L(S("show-drawing"), S("no")))
            ix = int(atom(c, 1))
            kids = [e for e in c[1] if head(e) != "drawing"]
            pg = ext.get("pages", {}).get(ix)
            if pg and pg.get("foreign"):
                kids += pg["foreign"]
            elif pg and pg.get("strokes"):
                kids.append(L(S("drawing"), S("1"), *[stroke_node(s) for s in pg["strokes"]]))
            c = ("list", kids, None)
        out.append(c)
    return write_node(("list", out, None)) + "\n"


def link_text(text: str) -> str:
    """A Share Link's text (6.4): no notes; the drawing only while it shows."""
    ext = read_ext(text)
    ext["notes"] = ""
    if ext["hidden"]:
        ext["pages"] = {}
    base = strip_ext(text)
    return write_with_ext(base, ext)


def strip_ext(text: str) -> str:
    root = parse_sexpr(text)
    out = []
    for c in root[1]:
        if head(c) in ("notes", "show-drawing"):
            continue
        if head(c) == "page":
            c = ("list", [e for e in c[1] if head(e) != "drawing"], None)
        out.append(c)
    return write_node(("list", out, None)) + "\n"


# ---------------------------------------------------------- sync lines --

SAFE = re.compile(r"[A-Za-z0-9_.:-]")


def esc(s: str) -> str:
    return "".join(chr(b) if b < 128 and SAFE.match(chr(b)) else "%%%02X" % b for b in s.encode("utf-8"))


def structure_lines(text: str) -> list[str]:
    """SYNC.md 2.4 as amended by DRAWING-NOTES.md 3.6: the D and N lines."""
    ext = read_ext(text)
    lines = []
    for ix, pg in ext["pages"].items():
        for st in pg["strokes"]:
            pts = ",".join(f"{x},{y}" for x, y in st["pts"])
            line = f"D {ix} {esc(st['tool'])} {esc(st['color'])} {st['w'] * 10} {pts}"
            if st.get("pr"):
                line += " p:" + st["pr"]
            lines.append(line)
    if notes_worth_writing(ext["notes"]):
        lines.append("N " + esc(ext["notes"]))
    return lines


# ----------------------------------------------------------- the palette --

PALETTE = {
    #          light      dark       print (colour export)
    "ink":    ("#1d1d1f", "#f5f5f7", "#000000"),
    "red":    ("#d70015", "#ff6961", "#d70015"),
    "orange": ("#c93400", "#ffb340", "#c93400"),
    "yellow": ("#b25000", "#ffd60a", "#b25000"),
    "green":  ("#248a3d", "#30d158", "#248a3d"),
    "blue":   ("#0040dd", "#409cff", "#0040dd"),
    "purple": ("#8944ab", "#da8fff", "#8944ab"),
    "pink":   ("#d30f45", "#ff6482", "#d30f45"),
}
HIGHLIGHT = {
    "yellow": "#ffd60a", "green": "#30d158", "blue": "#64d2ff", "pink": "#ff6482",
    "orange": "#ff9f0a", "red": "#ff453a", "purple": "#bf5af2", "ink": "#8e8e93",
}

# =============================================================== fixtures ==

def load(name):
    with open(os.path.join(HERE, name), encoding="utf-8") as f:
        return json.load(f)


def read_text(path):
    with open(path, "rb") as f:
        return f.read().decode("utf-8")


def vlq_vectors():
    vals = [0, 1, -1, 15, -15, 16, -16, 31, 32, 100, -100, 511, 512, 1000, -1000, 16383, 16384,
            123456, -123456, 1048575, 1048576, 33554431, 33554432, MAX_ABS, -MAX_ABS]
    good = [{"value": v, "text": vlq_encode(v)} for v in vals]
    lenient = [{"text": "B", "values": [0], "why": "minus zero reads as 0; writers never make it"},
               {"text": "gA", "values": [0], "why": "a needless continuation: the value is what counts"}]
    bad = [{"text": "g", "error": "cut-short"}, {"text": "A+", "error": "bad-char"},
           {"text": "A=", "error": "bad-char"}, {"text": "A A", "error": "bad-char"},
           {"text": "gggggggA", "error": "too-long"}, {"text": "ggggggA", "error": "too-long"}, {"text": vlq_encode(MAX_ABS + 1), "error": "out-of-range"}]
    return {"alphabet": ALPHA, "good": good, "lenient": lenient, "bad": bad}


def point_vectors():
    cases = []
    for world in ([[0, 0]], [[1.5, -2.25], [1.75, -2.25], [2, -2]], [[-0.005, 0.005], [0.004, -0.004]],
                  [[10000.12, -9999.99], [10000.13, -9999.98]], [[0.125, 1.005], [3.333, 2.675]]):
        centi = [(q(x), q(y)) for x, y in world]
        cases.append({"world": world, "centi": [list(c) for c in centi], "text": encode_points(centi)})
    bad = [{"text": "", "error": "empty"}, {"text": "AAC", "error": "odd"},
           {"text": "A" * 2 * (READ_STROKE_POINTS + 1), "error": "too-many-points"},
           {"text": vlq_encode(MAX_ABS) + "A" + vlq_encode(1) + "A", "error": "out-of-range"},
           {"text": "AA!A", "error": "bad-char"}]
    pres = [{"pressure": [0, 0.5, 1, 0.25, 0.999], "text": encode_pressure([0, 0.5, 1, 0.25, 0.999])}]
    return {"quantum": 0.01, "cases": cases, "bad": bad, "pressure": pres}


def simplify_vectors():
    zig = [[i * 0.1, (0.05 if i % 2 else 0)] for i in range(11)]
    arc = [[math.cos(a / 20 * math.pi), math.sin(a / 20 * math.pi)] for a in range(21)]
    line = [[i * 0.25, i * 0.5] for i in range(9)]
    out = []
    for name, pts, eps in (("straight line", line, 0.01), ("zigzag below eps", zig, 0.06),
                           ("zigzag above eps", zig, 0.04), ("half circle", arc, 0.02),
                           ("half circle coarse", arc, 0.2), ("one point", [[3, 4]], 0.01),
                           ("repeats", [[1, 1], [1.001, 1.002], [1, 1], [2, 2]], 0.01)):
        idx = simplify([tuple(p) for p in pts], eps)
        st = make_stroke("pen", "ink", 0.25, [tuple(p) for p in pts], eps=eps)
        out.append({"name": name, "eps": eps, "points": pts, "kept": idx,
                    "stroke": {"centi": [list(c) for c in st["pts"]], "text": encode_points(st["pts"])}})
    return out


def stroke_vectors():
    good_pts = encode_points([(100, 200), (150, 210), (180, 260)])
    cases = [
        ("plain pen", f'(stroke pen red 0.25 "{good_pts}")', "ok"),
        ("with pressure", f'(stroke pen ink 0.3 "{good_pts}" "gAz")', "ok"),
        ("highlighter", f'(stroke highlighter yellow 1 "{good_pts}")', "ok"),
        ("strings where symbols are written", f'(stroke "pen" "blue" "0.5" "{good_pts}")', "ok"),
        ("unknown tool and colour kept", f'(stroke laser teal 0.25 "{good_pts}")', "ok"),
        ("token with capitals -> default", f'(stroke Pen Red 0.25 "{good_pts}")', "ok"),
        ("token with a space -> default", f'(stroke "fancy pen" "dark red" 0.25 "{good_pts}")', "ok"),
        ("width clamped up", f'(stroke pen ink 0.001 "{good_pts}")', "ok"),
        ("width clamped down", f'(stroke pen ink 500 "{good_pts}")', "ok"),
        ("width rounded to centi", f'(stroke pen ink 0.256 "{good_pts}")', "ok"),
        ("pressure of the wrong length ignored", f'(stroke pen ink 0.3 "{good_pts}" "gA")', "ok"),
        ("pressure with a bad char ignored", f'(stroke pen ink 0.3 "{good_pts}" "g+z")', "ok"),
        ("extra items ignored", f'(stroke pen ink 0.3 "{good_pts}" "gAz" later (things 1))', "ok"),
        ("a dot", '(stroke pen ink 0.5 "8DwE")', "ok"),
        ("too short", '(stroke pen ink 0.3)', "short"),
        ("list where the width goes", f'(stroke pen ink (w 1) "{good_pts}")', "list-in-value"),
        ("width not a number", f'(stroke pen ink wide "{good_pts}")', "bad-width"),
        ("width with junk", f'(stroke pen ink 0.3px "{good_pts}")', "bad-width"),
        ("width zero", f'(stroke pen ink 0 "{good_pts}")', "bad-width"),
        ("width negative", f'(stroke pen ink -1 "{good_pts}")', "bad-width"),
        ("width nan", f'(stroke pen ink nan "{good_pts}")', "bad-width"),
        ("points cut short", '(stroke pen ink 0.3 "AAg")', "cut-short"),
        ("points odd", '(stroke pen ink 0.3 "AAC")', "odd"),
        ("points empty", '(stroke pen ink 0.3 "")', "empty"),
        ("points bad char", '(stroke pen ink 0.3 "AA=A")', "bad-char"),
    ]
    out = []
    for name, src, want in cases:
        node = parse_sexpr(src)
        try:
            st = read_stroke(node)
            got = {"tool": st["tool"], "color": st["color"], "width": st["w"] / 100,
                   "centi": [list(p) for p in st["pts"]], "pressure": st.get("pr")}
            rewrite = write_node(stroke_node(st))
            out.append({"name": name, "text": src, "result": got, "rewritten": rewrite})
        except InkError as e:
            assert e.kind == want, (name, e.kind, want)
            out.append({"name": name, "text": src, "error": e.kind})
        else:
            assert want == "ok", (name, want)
    return out


def notes_vectors():
    cases = [
        ("plain", "Half adder: S = A xor B, C = A and B."),
        ("CRLF and CR to LF", "line 1\r\nline 2\rline 3"),
        ("controls removed, tab kept", "a\x00b\x07c\td\x1be\x7ff"),
        ("leading BOM dropped", "﻿hello"),
        ("quotes, backslash and <", 'He said "x < y" \\ <version>9.0</version>'),
        ("unicode kept", "é ü 中文 😀   nbsp: ."),
        ("only whitespace: not written", " \n\t \n"),
        ("lone surrogate becomes U+FFFD", "a\ud83db"),
    ]
    out = []
    for name, raw in cases:
        norm = normalize_notes(raw)
        enc = write_atom_str(norm)
        entry = {"name": name, "input": raw, "normalized": norm, "written": notes_worth_writing(norm)}
        if notes_worth_writing(norm):
            entry["node"] = "(notes " + enc + ")"
            assert read_ext("(cedarlogic (version 3) (generator \"\") " + entry["node"] + ")")["notes"] == norm
        out.append(entry)
    long = "x" * (READ_NOTES_CHARS + 5)
    out.append({"name": "reader cap", "input_repeat": ["x", READ_NOTES_CHARS + 5],
                "normalized_length": len(normalize_notes(long)), "written": True})
    return out


# ---- the .cdl samples ----

def wiggle(x0, y0, n, dx, amp, seed):
    pts = []
    for i in range(n):
        pts.append((x0 + i * dx, y0 + amp * math.sin(i * 0.7 + seed) + 0.03 * math.cos(i * 2.3 + seed)))
    return pts


def circle(cx, cy, r, n):
    return [(cx + r * math.cos(2 * math.pi * i / (n - 1)), cy + r * math.sin(2 * math.pi * i / (n - 1))) for i in range(n)]


def samples_ext():
    """Every sample's drawing/notes, made the way an editor makes them."""
    pen = make_stroke("pen", "ink", 0.25, wiggle(8, -5, 30, 0.2, 0.3, 1),
                      pressure=[0.3 + 0.5 * abs(math.sin(i / 5)) for i in range(30)], eps=0.02)
    ring = make_stroke("pen", "red", 0.3, circle(18, -11, 4.5, 40), eps=0.02)
    hl = make_stroke("highlighter", "yellow", 1.2, [(3, -8 + 0.0), (9, -8.1), (12, -8.05)], eps=0.02)
    dot = make_stroke("pen", "blue", 0.6, [(25, -3)])
    note2 = make_stroke("pen", "green", 0.25, wiggle(4, 2, 25, 0.25, 0.4, 3), eps=0.02)
    notes = ("Half adder\nS = A xor B, C = A and B.\n\tCheck: 1 + 1 = 10 ✔\n"
             'Teacher said "use < and > carefully" \\ and <version>9.0</version> is just text.\n'
             "Café 中文 \U0001F600")
    return {
        "drawing-and-notes": ("base-half-adder.cdl", {"notes": notes, "hidden": False,
                                                       "pages": {0: {"strokes": [hl, pen, ring, dot]},
                                                                 1: {"strokes": [note2]}}}),
        "hidden-drawing": ("base-and.cdl", {"notes": "", "hidden": True, "pages": {0: {"strokes": [ring]}}}),
        "notes-only": ("base-and.cdl", {"notes": "Only notes here.\nSecond line.", "hidden": False, "pages": {}}),
    }


def big_ext(points_per_page=20_000, pages=3, pressure=True, seed=7):
    """Handwriting-like strokes: a random walk that turns smoothly, about
    0.1 to 0.2 world units between kept points (what RDP leaves of a pen)."""
    import random
    rnd = random.Random(seed)
    strokes = {}
    for p in range(pages):
        lst, total, k = [], 0, 0
        while total < points_per_page:
            n = min(rnd.randint(8, 60), points_per_page - total)
            x, y, a = rnd.uniform(0, 60), rnd.uniform(-40, 0), rnd.uniform(0, 6.28)
            centi, pr, pz = [], [], rnd.uniform(0.3, 0.7)
            for i in range(n):
                c = (q(x), q(y))
                if not centi or centi[-1] != c:
                    centi.append(c)
                    pz = min(1, max(0.05, pz + rnd.gauss(0, 0.05)))
                    pr.append(pz)
                a += rnd.gauss(0, 0.45)
                step = rnd.uniform(0.08, 0.22)
                x += step * math.cos(a)
                y += step * math.sin(a)
            st = {"tool": "pen", "color": rnd.choice(["ink", "red", "blue"]), "w": 25, "pts": centi}
            if pressure:
                st["pr"] = encode_pressure(pr)
            lst.append(st)
            total += len(centi)
            k += 1
        strokes[p] = {"strokes": lst}
    return strokes


def expected_for(text: str) -> dict:
    ext = read_ext(text)
    pages = []
    for ix, pg in ext["pages"].items():
        pages.append({"index": ix, "readOnly": pg["readOnly"], "foreign": [write_node(d, 2) for d in pg["foreign"]],
                      "strokes": [{"tool": s["tool"], "color": s["color"], "width": s["w"] / 100,
                                   "centi": [list(p) for p in s["pts"]], "pressure": s.get("pr")}
                                  for s in pg["strokes"]]})
    return {"notes": ext["notes"], "hidden": ext["hidden"], "pages": pages, "notices": ext["notices"],
            "structureLines": structure_lines(text)}


ODD = r'''(cedarlogic
  (version 3)
  (generator "hand-written")
  (notes "first notes wins")
  (notes "second is ignored")
  (show-drawing yes)
  (page 0
    (drawing 1
      (stroke pen ink 0.25 "AAgDA")
      (stroke pen ink 0.25 "gD")
      (stroke "pen" "blue" "0.5" "8DwE")
      (stroke Pen Red 0.0001 "8DwE" "z")
      (stroke pen ink 0.3 "8DwEEE" "AB")
      (stroke pen ink wide "8DwE")
      (stroke pen ink 0.3 "8DwE" "g" later (things 1))
      (not-a-stroke 1 2 3)
      (stroke))
    (drawing 1
      (stroke highlighter pink 2 "AAoBA"))))
'''

NEWER = r'''(cedarlogic
  (version 3)
  (generator "CedarLogic 9.0.0-native")
  (notes "Made by a later CedarLogic.")
  (page 0
    (drawing 2
      (layer "under"
        (stroke pen ink 0.25 "AAgDA"))
      (shape rect 1 2 3 4)))
  (page 1
    (drawing 1
      (stroke pen red 0.25 "AAgDA"))
    (drawing x)))
'''

HAZARD = '''(cedarlogic
  (version 3)
  (generator "hand-written: notes NOT escaped (no writer may make this)")
  (notes "see <version>9.0</version> here")
  (page 0))
'''


def build_samples() -> dict:
    """name -> text for every .cdl sample."""
    base_dir = os.path.join(HERE, "cdl")
    out = {}
    for name, (base, ext) in samples_ext().items():
        out[name] = write_with_ext(read_text(os.path.join(base_dir, base)), ext)
    # A flag with no drawing to hide (a reader takes it; a writer leaves it out).
    base = read_text(os.path.join(base_dir, "base-and.cdl"))
    out["hidden-flag-without-drawing"] = base.replace('  (generator "CedarLogic Online")\n',
                                                      '  (generator "CedarLogic Online")\n  (show-drawing no)\n', 1)
    out["odd-strokes"] = ODD
    out["newer-drawing"] = NEWER
    out["notes-version-hazard"] = HAZARD
    out["drawing-and-notes.link"] = link_text(out["drawing-and-notes"])
    out["hidden-drawing.link"] = link_text(out["hidden-drawing"])
    return out


def rewrite(text: str) -> str:
    """What a drawing-aware app writes after reading text (no other change),
    for the documents whose drawing it can read."""
    return write_with_ext(strip_ext(text), read_ext(text))


def generate():
    files = {
        "ink-vlq.json": vlq_vectors(),
        "ink-points.json": point_vectors(),
        "ink-simplify.json": simplify_vectors(),
        "ink-strokes.json": stroke_vectors(),
        "notes.json": notes_vectors(),
        "palette.json": {"pen": {k: {"light": v[0], "dark": v[1], "print": v[2]} for k, v in PALETTE.items()},
                         "highlighter": HIGHLIGHT, "highlighterAlpha": {"light": 0.4, "dark": 0.35},
                         "fallback": {"pen": "ink", "highlighter": "yellow"}},
    }
    for name, data in files.items():
        with open(os.path.join(HERE, name), "w", encoding="utf-8") as f:
            # (notes.json holds a lone surrogate, which only an escape can carry)
            json.dump(data, f, ensure_ascii=(name == "notes.json"), indent=1)
            f.write("\n")
    samples = build_samples()
    expected = {}
    for name, text in samples.items():
        with open(os.path.join(CDL, name + ".cdl"), "w", encoding="utf-8", newline="") as f:
            f.write(text)
        if name == "notes-version-hazard":
            expected[name] = {"refusedByEveryApp": True, "why": "the raw bytes hold <version>9.0</version> (CDL-Format 8.1)"}
            continue
        e = expected_for(text)
        if True:
            e["rewriteSame"] = rewrite(text) == text
            if not e["rewriteSame"]:
                rw = rewrite(text)
                with open(os.path.join(CDL, name + ".rewritten.cdl"), "w", encoding="utf-8", newline="") as f:
                    f.write(rw)
                e["rewrite"] = name + ".rewritten.cdl"
        expected[name] = e
    with open(os.path.join(HERE, "expected.json"), "w", encoding="utf-8") as f:
        json.dump(expected, f, ensure_ascii=False, indent=1)
        f.write("\n")
    print("generated", len(files) + len(samples), "fixtures")


def check() -> int:
    bad = 0

    def ok(cond, what):
        nonlocal bad
        if not cond:
            bad += 1
            print("FAIL", what)

    v = load("ink-vlq.json")
    for c in v["good"]:
        ok(vlq_encode(c["value"]) == c["text"], f"vlq encode {c['value']}")
        ok(vlq_decode_all(c["text"]) == [c["value"]], f"vlq decode {c['text']}")
    for c in v["lenient"]:
        ok(vlq_decode_all(c["text"]) == c["values"], f"vlq lenient {c['text']}")
    for c in v["bad"]:
        try:
            vlq_decode_all(c["text"])
            ok(False, f"vlq bad {c['text']}")
        except InkError as e:
            ok(e.kind == c["error"], f"vlq bad kind {c['text']}")
    p = load("ink-points.json")
    for c in p["cases"]:
        centi = [(q(x), q(y)) for x, y in c["world"]]
        ok([list(t) for t in centi] == c["centi"], f"quantize {c['world']}")
        ok(encode_points(centi) == c["text"], f"points encode {c['world']}")
        ok([list(t) for t in decode_points(c["text"])] == c["centi"], f"points decode {c['text']}")
    for c in p["bad"]:
        try:
            decode_points(c["text"])
            ok(False, f"points bad {c['text'][:20]}")
        except InkError as e:
            ok(e.kind == c["error"], f"points bad kind {c['text'][:20]}: {e.kind}")
    for c in load("ink-simplify.json"):
        pts = [tuple(x) for x in c["points"]]
        ok(simplify(pts, c["eps"]) == c["kept"], "simplify " + c["name"])
        st = make_stroke("pen", "ink", 0.25, pts, eps=c["eps"])
        ok(encode_points(st["pts"]) == c["stroke"]["text"], "make_stroke " + c["name"])
    for c in load("ink-strokes.json"):
        node = parse_sexpr(c["text"])
        try:
            st = read_stroke(node)
            ok("result" in c and c["result"]["centi"] == [list(t) for t in st["pts"]], "stroke " + c["name"])
            ok(write_node(stroke_node(st)) == c["rewritten"], "stroke rewrite " + c["name"])
        except InkError as e:
            ok(c.get("error") == e.kind, "stroke error " + c["name"])
    for c in load("notes.json"):
        if "input" in c:
            ok(normalize_notes(c["input"]) == c["normalized"], "notes " + c["name"])
            ok(notes_worth_writing(c["normalized"]) == c["written"], "notes written " + c["name"])
    exp = load("expected.json")
    for name, e in exp.items():
        text = read_text(os.path.join(CDL, name + ".cdl"))
        if e.get("refusedByEveryApp"):
            ok("<version>" in text, name)
            continue
        got = expected_for(text)
        for k in ("notes", "hidden", "pages", "notices", "structureLines"):
            ok(got[k] == e[k], f"{name}: {k}")
        if "rewriteSame" in e:
            rw = rewrite(text)
            ok((rw == text) == e["rewriteSame"], f"{name}: rewrite same")
            if not e["rewriteSame"]:
                ok(rw == read_text(os.path.join(CDL, e["rewrite"])), f"{name}: rewrite text")
    # Every sample a drawing-aware writer made: no raw "<version>" anywhere.
    for name in exp:
        text = read_text(os.path.join(CDL, name + ".cdl"))
        if name not in ("notes-version-hazard",):
            ok("<version>" not in text, f"{name}: no raw <version>")
    print("ok" if not bad else f"{bad} failed")
    return 1 if bad else 0


def sizes():
    base = read_text(os.path.join(CDL, "base-half-adder.cdl"))
    rows = []
    for label, pages, per, pr in (("one page at the cap, pressure", 1, 20_000, True),
                                  ("document cap (3 x 20,000), pressure", 3, 20_000, True),
                                  ("document cap, no pressure", 3, 20_000, False),
                                  ("a 500-point annotation", 1, 500, True),
                                  ("a 1,500-point annotation", 1, 1500, True)):
        ext = {"notes": "", "hidden": False, "pages": big_ext(per, pages, pr)}
        t = write_with_ext(base, ext)
        raw = len(t.encode())
        comp = zlib.compressobj(9, zlib.DEFLATED, -15)
        d = comp.compress(t.encode()) + comp.flush()
        link = len("https://cedarlogic.netlify.app/online-logic-gate-simulator/#c=") + math.ceil(len(d) * 4 / 3)
        rows.append((label, raw, len(d), link))
    comp = zlib.compressobj(9, zlib.DEFLATED, -15)
    d0 = comp.compress(base.encode()) + comp.flush()
    print(f"{'circuit alone':40s} {len(base):>9,} B file {len(d0):>8,} B deflated")
    for label, raw, dl, link in rows:
        print(f"{label:40s} {raw:>9,} B file {dl:>8,} B deflated  link {link:>8,} chars")


if __name__ == "__main__":
    if "--generate" in sys.argv:
        generate()
    if "--sizes" in sys.argv:
        sizes()
    if "--check" in sys.argv or len(sys.argv) == 1:
        sys.exit(check())
