#!/usr/bin/env python3
"""Edit CedarLogic's update feeds in a checkout of the releases repo.

Called by publish-update.sh:
  appcast.py add     RELEASES_DIR VERSION beta|normal BASE_URL MANIFEST [NOTES.md]
  appcast.py promote RELEASES_DIR VERSION

Feeds (wx app / native app, normal / beta):
  appcast.xml  appcast-beta.xml  appcast-native.xml  appcast-native-beta.xml

Each item is one build for one platform (sparkle:os = macos | windows | linux,
plus cedarlogic:arch for Linux). Sparkle and WinSparkle skip items for other
platforms; the Linux updater picks its own by arch and checks cedarlogic:sha256.
Items are written as a whole block with a marker comment, so a re-publish of
the same version replaces its item instead of adding a second one.
"""

import html
import os
import re
import sys
from email.utils import formatdate

KEEP = 8  # items kept per platform per feed; older ones drop off

HEADER = """<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle" xmlns:cedarlogic="https://github.com/leviholliday/CedarLogic">
  <channel>
    <title>{title}</title>
    <link>https://github.com/leviholliday/CedarLogic-Releases</link>
    <description>{title}</description>
    <language>en</language>
"""
FOOTER = """  </channel>
</rss>
"""
ITEM_RE = re.compile(r"    <!-- item (\S+) (\S+) -->\n.*?    </item>\n", re.S)


def feed_names(native):
    return ("appcast-native.xml", "appcast-native-beta.xml") if native else \
           ("appcast.xml", "appcast-beta.xml")


def read_items(path):
    """[(key, version, block)] in file order."""
    if not os.path.exists(path):
        return []
    text = open(path, encoding="utf-8").read()
    return [(m.group(1), m.group(2), m.group(0)) for m in ITEM_RE.finditer(text)]


def vkey(v):
    return tuple(int(p) for p in v.split("."))


def write_feed(path, items, title):
    # newest first, KEEP per platform key
    items = sorted(items, key=lambda it: vkey(it[1]), reverse=True)
    seen, kept = {}, []
    for it in items:
        seen[it[0]] = seen.get(it[0], 0) + 1
        if seen[it[0]] <= KEEP:
            kept.append(it)
    with open(path, "w", encoding="utf-8") as f:
        f.write(HEADER.format(title=title))
        for it in kept:
            f.write(it[2])
        f.write(FOOTER)


def upsert(path, block_items, title):
    items = read_items(path)
    new_ids = {(k, v) for k, v, _ in block_items}
    items = [it for it in items if (it[0], it[1]) not in new_ids]
    write_feed(path, items + block_items, title)


def md_to_html(md):
    out, in_list = [], False
    for line in md.strip().splitlines():
        s = line.strip()
        def inline(t):
            t = html.escape(t)
            t = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", t)
            return re.sub(r"`(.+?)`", r"<code>\1</code>", t)
        if s.startswith("- ") or s.startswith("* "):
            if not in_list:
                out.append("<ul>"); in_list = True
            out.append("<li>%s</li>" % inline(s[2:]))
            continue
        if in_list:
            out.append("</ul>"); in_list = False
        if s.startswith("#"):
            out.append("<h3>%s</h3>" % inline(s.lstrip("#").strip()))
        elif s:
            out.append("<p>%s</p>" % inline(s))
    if in_list:
        out.append("</ul>")
    return "\n".join(out)


def item_block(version, short, name, os_, arch, sig, sha, length, url, notes_html):
    key = os_ if not arch else "%s-%s" % (os_, arch)
    attrs = ['url="%s"' % url, 'length="%s"' % length,
             'type="application/octet-stream"', 'sparkle:os="%s"' % os_]
    if sig:
        attrs.append('sparkle:edSignature="%s"' % sig)
    if arch:
        attrs.append('cedarlogic:arch="%s"' % arch)
    if sha:
        attrs.append('cedarlogic:sha256="%s"' % sha)
    if os_ == "windows":
        attrs.append('sparkle:installerArguments="/S"')
    minsys = ""
    if os_ == "macos":
        minsys = "      <sparkle:minimumSystemVersion>%s</sparkle:minimumSystemVersion>\n" % (
            "14.0" if name.startswith("CedarLogic-Native-") else "11.0")
    return (
        "    <!-- item %s %s -->\n"
        "    <item>\n"
        "      <title>CedarLogic %s</title>\n"
        "      <pubDate>%s</pubDate>\n"
        "      <sparkle:version>%s</sparkle:version>\n"
        "      <sparkle:shortVersionString>%s</sparkle:shortVersionString>\n"
        "%s"
        "      <description><![CDATA[%s]]></description>\n"
        "      <enclosure %s/>\n"
        "    </item>\n"
    ) % (key, version, short, formatdate(), version, short, minsys, notes_html,
         "\n        ".join(attrs))


def cmd_add(rel, version, mode, base_url, manifest, notes_path):
    short = version + (" beta" if mode == "beta" else "")
    notes = md_to_html(open(notes_path, encoding="utf-8").read()) if notes_path else \
        "<p>CedarLogic %s</p>" % html.escape(short)
    wx, native = [], []
    for line in open(manifest, encoding="utf-8"):
        cols = line.rstrip("\n").split("\t") + ["", ""]
        name, os_, arch, sig, sha, length, own_v, own_short = cols[:8]
        v_item = own_v or version
        short_item = (own_short + (" beta" if mode == "beta" else "")) if own_short else short
        is_native = os_ == "native"
        if is_native:
            os_ = "macos"
        if os_ in ("macos", "windows") and not sig:
            sys.exit("appcast.py: %s has no signature; the app would refuse it" % name)
        if os_ == "linux" and len(sha) != 64:
            sys.exit("appcast.py: %s has no SHA-256" % name)
        blk = item_block(v_item, short_item, name, os_, arch, sig, sha, length,
                         "%s/%s" % (base_url, name), notes)
        key = os_ if not arch else "%s-%s" % (os_, arch)
        (native if is_native else wx).append((key, v_item, blk))

    for items, is_native in ((wx, False), (native, True)):
        if not items:
            continue
        normal, beta = feed_names(is_native)
        title = "CedarLogic Native updates" if is_native else "CedarLogic updates"
        # Refuse to publish something not newer than what's already out on
        # that platform: testers' apps would never offer it.
        for key, v, _ in items:
            for fname in (beta, normal):
                for k2, v2, _ in read_items(os.path.join(rel, fname)):
                    if k2 == key and vkey(v2) > vkey(v):
                        sys.exit("appcast.py: %s %s is already out in %s; %s would never be offered. "
                                 "Bump the version." % (key, v2, fname, v))
        upsert(os.path.join(rel, beta), items, title + " (beta testers)")
        if mode == "normal":
            upsert(os.path.join(rel, normal), items, title)
        else:
            # keep the normal feed present even before its first release
            p = os.path.join(rel, normal)
            if not os.path.exists(p):
                write_feed(p, [], title)
        print("feeds: %s%s <- %s" % (beta, "" if mode == "beta" else " + " + normal,
                                     ", ".join(k for k, _, _ in items)))


def cmd_promote(rel, version):
    moved = 0
    for is_native in (False, True):
        normal, beta = feed_names(is_native)
        title = "CedarLogic Native updates" if is_native else "CedarLogic updates"
        src = [it for it in read_items(os.path.join(rel, beta))
               if it[1] == version or "/v%s/" % version in it[2]]
        if not src:
            continue
        final = []
        for k, v, blk in src:
            blk = blk.replace(" beta</sparkle:shortVersionString>", "</sparkle:shortVersionString>")
            blk = re.sub(r"(<title>CedarLogic .*?) beta</title>", r"\1</title>", blk)
            final.append((k, v, blk))
        upsert(os.path.join(rel, normal), final, title)
        upsert(os.path.join(rel, beta), final, title + " (beta testers)")
        moved += len(final)
    if not moved:
        sys.exit("appcast.py: %s isn't in the beta feeds" % version)
    print("promoted %s: %d build(s) now offered to everyone" % (version, moved))


if __name__ == "__main__":
    a = sys.argv[1:]
    if a[:1] == ["add"] and len(a) in (6, 7):
        cmd_add(a[1], a[2], a[3], a[4], a[5], a[6] if len(a) == 7 and a[6] else "")
    elif a[:1] == ["promote"] and len(a) == 3:
        cmd_promote(a[1], a[2])
    else:
        sys.exit(__doc__)
