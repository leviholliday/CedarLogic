#!/usr/bin/env python3
# Writes res/templates: the Mac app's built-in templates (File > New from
# Template), as files the Windows app reads, so the two offer the same
# circuits. The Mac builds them with the engine when first asked for
# (mac/App/Templates.swift, TemplatesExamples.swift); its --render-ui writes
# each one out, a flip-flop's or register's running clock as -running and
# its pulse button as -pulse. Their names, lines and groups are read from
# Templates.swift. The Lab Page isn't copied: it carries your name, so the
# Windows app builds it itself.
#
#   mac/build.sh
#   mac/build/CedarLogic.app/Contents/MacOS/CedarLogic --render-ui /tmp/ui
#   python3 windows/make-templates.py /tmp/ui
import os
import re
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "res", "templates")
GROUPS = [("builtin-ff-", "Flip-Flops and Latches"), ("builtin-reg-", "Registers"), ("builtin-comb-", "Combinational"),
          ("builtin-count-", "Counters and Displays"), ("builtin-mistake-", "Classic Mistakes")]
SKIP = {"builtin-lab"}


def templates(swift):
    """(id, name, detail) in the Mac's order: makeBuiltIn's list."""
    text = open(swift, encoding="utf-8").read()
    found = {}
    s = r'"((?:[^"\\]|\\.)*)"'
    for m in re.finditer(r'CircuitTemplate\(id: ' + s + r', name: ' + s + r',\s*detail: ' + s, text):
        found[m.group(1)] = (m.group(2), m.group(3))
    funcs = {"flipFlops": "builtin-ff-", "registers": "builtin-reg-", "combinational": "builtin-comb-",
             "classicMistakes": "builtin-mistake-"}
    for func, prefix in funcs.items():
        body = text[text.index("private static func %s()" % func):]
        body = body[:body.index("\n    }\n")]
        for m in re.finditer(r'\bt\(' + s + r',\s*' + s + r',\s*' + s, body):
            found[prefix + m.group(1)] = (m.group(2), m.group(3))
    order = re.search(r'makeBuiltIn\(name: String\) -> \[CircuitTemplate\] \{ \[(.*?)\] \+ (.*?) \}', text, re.S)
    ids = re.findall(r'id: "([^"]+)"', order.group(1))
    for func in re.findall(r'(\w+)\(\)', order.group(2)):
        if func in funcs:
            ids += [k for k in found if k.startswith(funcs[func])]
        elif func == "counters":
            ids += [k for k in found if k.startswith("builtin-count-")]
    return [(i, found[i][0], found[i][1]) for i in ids]


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: make-templates.py <the folder CedarLogic --render-ui wrote>")
    rendered = sys.argv[1]
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)
    lines = ["# The built-in templates (windows/make-templates.py writes this folder): id, group, name, line,",
             "# and whether it has a Clock choice (<id>-running.cdl and <id>-pulse.cdl beside <id>.cdl). Tab separated."]
    for tid, name, detail in templates(os.path.join(ROOT, "mac", "App", "Templates.swift")):
        if tid in SKIP:
            continue
        group = next((g for p, g in GROUPS if tid.startswith(p)), "Built In")
        clock = os.path.exists(os.path.join(rendered, "template-%s-running.cdl" % tid))
        for suffix in ["", "-running", "-pulse"] if clock else [""]:
            shutil.copyfile(os.path.join(rendered, "template-%s%s.cdl" % (tid, suffix)), os.path.join(OUT, tid + suffix + ".cdl"))
        unescape = lambda v: v.replace('\\"', '"').replace("\\'", "'")
        lines.append("\t".join([tid, group, unescape(name), unescape(detail), "clock" if clock else ""]))
    with open(os.path.join(OUT, "templates.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("%d templates in %s" % (len(lines) - 2, OUT))


main()
