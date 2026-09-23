#!/usr/bin/env python3
"""Build the app icon from res/macos/icon-artwork.png.

The artwork arrives as a square picture with the icon tile drawn inside it and
dark space around the tile. macOS wants the opposite: the tile itself, with
everything outside its rounded corners transparent, sized to Apple's icon grid
(an 824pt body centred on a 1024pt canvas). This crops to the tile, masks it to
that shape with antialiased edges, and writes every file the app needs:
res/macos/CedarLogic.icns, res/icon.png and res/icon.ico.

No image library is installed and none is worth adding for this, so the PNG is
decoded and encoded here; sips does the resampling.
"""

import math, os, struct, subprocess, shutil, sys, zlib

BODY, CANVAS = 824, 1024          # Apple's macOS icon grid
RADIUS = 185.4                    # the body's corner radius on that grid
TILE = (80, 78, 1174, 1172)       # where the tile sits in the artwork


def read_png(path):
    data = open(path, 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', "not a PNG"
    pos, idat = 8, b''
    while pos < len(data):
        ln = struct.unpack('>I', data[pos:pos + 4])[0]
        tag = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + ln]
        pos += 12 + ln
        if tag == b'IHDR':
            w, h, bd, ct, _, _, inter = struct.unpack('>IIBBBBB', body)
            assert bd == 8 and inter == 0, "want 8-bit, non-interlaced"
        elif tag == b'IDAT':
            idat += body
        elif tag == b'IEND':
            break
    raw = zlib.decompress(idat)
    nch = {0: 1, 2: 3, 4: 2, 6: 4}[ct]
    stride = w * nch
    out, prev, p = bytearray(h * stride), bytearray(stride), 0
    for y in range(h):
        f = raw[p]; p += 1
        line = bytearray(raw[p:p + stride]); p += stride
        if f == 1:
            for i in range(nch, stride):
                line[i] = (line[i] + line[i - nch]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                a = line[i - nch] if i >= nch else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i - nch] if i >= nch else 0
                b = prev[i]
                c = prev[i - nch] if i >= nch else 0
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return w, h, nch, bytes(out)


def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    open(path, 'wb').write(b"\x89PNG\r\n\x1a\n"
                           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 9))
                           + chunk(b"IEND", b""))


def crop(src, dst, box):
    w, h, nch, px = read_png(src)
    x0, y0, x1, y1 = box
    rows = []
    for y in range(y0, y1):
        row = bytearray()
        for x in range(x0, x1):
            i = (y * w + x) * nch
            row += bytes((px[i], px[i + 1], px[i + 2], 255))
        rows.append(bytes(row))
    write_png(dst, x1 - x0, y1 - y0, rows)


def rounded_rect_sdf(x, y, cx, cy, half, r):
    dx, dy = abs(x - cx) - (half - r), abs(y - cy) - (half - r)
    ax, ay = max(dx, 0.0), max(dy, 0.0)
    return math.hypot(ax, ay) + min(max(dx, dy), 0.0) - r


def mask_to_icon(src, dst):
    """Centre the body on the canvas and round off its corners."""
    w, h, nch, px = read_png(src)
    assert w == h == BODY, (w, h)
    pad = (CANVAS - BODY) // 2
    c = BODY / 2.0
    blank = b"\x00\x00\x00\x00" * CANVAS
    rows = [blank] * pad
    for y in range(BODY):
        row = bytearray(b"\x00\x00\x00\x00" * pad)
        for x in range(BODY):
            d = rounded_rect_sdf(x + 0.5, y + 0.5, c, c, c, RADIUS)
            a = min(1.0, max(0.0, 0.5 - d))
            i = (y * w + x) * nch
            row += bytes((px[i], px[i + 1], px[i + 2], int(a * 255)))
        row += b"\x00\x00\x00\x00" * pad
        rows.append(bytes(row))
    rows += [blank] * pad
    write_png(dst, CANVAS, CANVAS, rows)


def sips(src, dst, px):
    subprocess.run(["sips", "-s", "format", "png", "-z", str(px), str(px), src, "--out", dst],
                   check=True, capture_output=True)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    art = os.path.join(root, "res", "macos", "icon-artwork.png")
    if not os.path.exists(art):
        sys.exit("missing " + art)

    out = "/tmp/cedaricon"
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)

    print("cropping to the tile ...")
    cropped = os.path.join(out, "tile.png")
    crop(art, cropped, TILE)

    body = os.path.join(out, "body.png")
    sips(cropped, body, BODY)

    print("masking to the icon shape ...")
    master = os.path.join(out, "icon_1024.png")
    mask_to_icon(body, master)

    iconset = os.path.join(out, "CedarLogic.iconset")
    os.makedirs(iconset)
    for px, names in [(16, ["icon_16x16.png"]), (32, ["icon_16x16@2x.png", "icon_32x32.png"]),
                      (64, ["icon_32x32@2x.png"]), (128, ["icon_128x128.png"]),
                      (256, ["icon_128x128@2x.png", "icon_256x256.png"]),
                      (512, ["icon_256x256@2x.png", "icon_512x512.png"]),
                      (1024, ["icon_512x512@2x.png"])]:
        for name in names:
            sips(master, os.path.join(iconset, name), px)

    icns = os.path.join(root, "res", "macos", "CedarLogic.icns")
    subprocess.run(["iconutil", "-c", "icns", iconset, "-o", icns], check=True)
    print("wrote", icns)

    sips(master, os.path.join(root, "res", "icon.png"), 64)
    print("wrote res/icon.png")

    # Windows .ico: a directory of embedded PNGs, which modern Windows reads.
    sizes = [16, 32, 48, 64, 128, 256]
    blobs = []
    for px in sizes:
        tmp = os.path.join(out, "ico_%d.png" % px)
        sips(master, tmp, px)
        blobs.append(open(tmp, 'rb').read())
    header = struct.pack("<HHH", 0, 1, len(sizes))
    offset = len(header) + 16 * len(sizes)
    entries, data = b"", b""
    for px, blob in zip(sizes, blobs):
        entries += struct.pack("<BBBBHHII", px if px < 256 else 0, px if px < 256 else 0,
                               0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
        data += blob
    open(os.path.join(root, "res", "icon.ico"), 'wb').write(header + entries + data)
    print("wrote res/icon.ico")
    print("preview:", master)


if __name__ == "__main__":
    main()
