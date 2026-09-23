#!/usr/bin/env python3
"""Draw the disk image background: a dark stage with two glass panels and a
glowing arrow from the app to Applications.

Run by hand when the design changes, not at build time -- the result is checked
in as res/macos/dmg-background.tiff, so a release needs nothing but the file:

    python3 scripts/make-dmg-background.py

Writes one TIFF holding both a 1x and a 2x representation, which is how a
picture stays sharp on a Retina display and honest on anything else. The
coordinates here and the ones in res/macos/dmg-setup.applescript describe the
same window and have to be changed together.

Finder draws the icon names itself, in black under a light system appearance
and in white under a dark one, and there is no way to choose. So the strip
each name sits on is a mid-grey frosted pill that both colours read against.

Needs Pillow (pip install pillow) and tiffutil, which ships with macOS.
"""

import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

# The Finder window, in points. See dmg-setup.applescript.
WIDTH, HEIGHT = 660, 400

# Icon centres, matching the positions the AppleScript sets.
APP_X, ICON_Y = 165, 185
APPLICATIONS_X = 495

# Glass panel around each icon, and the pill under each name.
PANEL_W, PANEL_H, PANEL_R = 196, 214, 34
PANEL_TOP = ICON_Y - 98
LABEL_Y, LABEL_W, LABEL_H = 262, 150, 26

GREEN = (65, 168, 63)       # the green of the app icon
GREEN_LIGHT = (140, 230, 120)

# Wide-tracked capitals in a geometric sans, in the icon green: the same voice
# as the CedarLogic wordmark.
CAPTION_TEXT = "DRAG TO APPLICATIONS TO INSTALL"
CAPTION_Y = 345
CAPTION_SIZE = 13
CAPTION_TRACKING = 0.32      # extra space between letters, in ems
CAPTION_COLOUR = (72, 196, 92)

FONT = "/System/Library/Fonts/Avenir Next.ttc"
FONT_MEDIUM_INDEX = 5        # Avenir Next Medium inside the collection


def draw_tracked(d, centre, text, font, fill, tracking):
    """Pillow has no letter-spacing, so set the letters one at a time."""
    gap = font.size * tracking
    widths = [font.getlength(c) for c in text]
    total = sum(widths) + gap * (len(text) - 1)
    x = centre[0] - total / 2
    for c, w in zip(text, widths):
        d.text((x, centre[1]), c, font=font, fill=fill, anchor="lm")
        x += w + gap


def vertical_gradient(w, h, top, bottom):
    col = Image.new("RGB", (1, 256))
    for y in range(256):
        t = y / 255
        col.putpixel((0, y), tuple(int(a + (b - a) * t) for a, b in zip(top, bottom)))
    return col.resize((w, h), Image.BICUBIC)


def glow(size, centre, radius, colour, strength, s):
    """A soft coloured light, as an RGB layer meant to be screened on."""
    w, h = size
    layer = Image.new("RGB", (w, h), (0, 0, 0))
    d = ImageDraw.Draw(layer)
    cx, cy = centre[0] * s, centre[1] * s
    r = radius * s
    d.ellipse([cx - r, cy - r, cx + r, cy + r],
              fill=tuple(int(c * strength) for c in colour))
    return layer.filter(ImageFilter.GaussianBlur(r * 0.6))


def rounded_mask(size, box, radius, s):
    m = Image.new("L", size, 0)
    ImageDraw.Draw(m).rounded_rectangle([v * s for v in box], radius=radius * s, fill=255)
    return m


def stage(s):
    size = (WIDTH * s, HEIGHT * s)
    bg = vertical_gradient(*size, (16, 20, 26), (6, 8, 11))
    # Coloured light behind the glass: that is what makes glass read as glass.
    for centre, radius, colour, k in [
        ((APP_X - 20, ICON_Y - 30), 150, GREEN, 0.55),
        ((APPLICATIONS_X + 30, ICON_Y + 40), 160, (40, 110, 200), 0.45),
        ((WIDTH / 2, ICON_Y), 90, (60, 190, 150), 0.25),
    ]:
        bg = ImageChops.screen(bg, glow(size, centre, radius, colour, k, s))
    # A faint dot grid, as on the canvas.
    d = ImageDraw.Draw(bg)
    for gx in range(10, WIDTH, 20):
        for gy in range(10, HEIGHT, 20):
            d.ellipse([(gx - 0.6) * s, (gy - 0.6) * s, (gx + 0.6) * s, (gy + 0.6) * s],
                      fill=(34, 40, 48))
    return bg


def glass_panel(bg, cx, s):
    size = bg.size
    box = (cx - PANEL_W / 2, PANEL_TOP, cx + PANEL_W / 2, PANEL_TOP + PANEL_H)
    mask = rounded_mask(size, box, PANEL_R, s)

    # Shadow first, under everything.
    shadow = Image.new("L", size, 0)
    ImageDraw.Draw(shadow).rounded_rectangle(
        [box[0] * s, (box[1] + 10) * s, box[2] * s, (box[3] + 14) * s],
        radius=PANEL_R * s, fill=150)
    shadow = shadow.filter(ImageFilter.GaussianBlur(18 * s))
    bg = Image.composite(Image.new("RGB", size, (0, 0, 0)), bg, shadow)

    # Frosted body: the stage blurred and lifted, clipped to the panel.
    frost = bg.filter(ImageFilter.GaussianBlur(22 * s))
    frost = Image.blend(frost, Image.new("RGB", size, (255, 255, 255)), 0.08)
    # Sheen: brighter at the top edge, fading down, as light across a lens.
    sheen = vertical_gradient(int((box[2] - box[0]) * s), int((box[3] - box[1]) * s),
                              (255, 255, 255), (0, 0, 0))
    sheen_layer = Image.new("RGB", size, (0, 0, 0))
    sheen_layer.paste(sheen, (int(box[0] * s), int(box[1] * s)))
    frost = Image.blend(frost, ImageChops.screen(frost, sheen_layer), 0.14)
    bg = Image.composite(frost, bg, mask)

    # Rim light: a hairline that is bright along the top and nearly gone at
    # the bottom, which is what gives the edge its thickness.
    rim = Image.new("L", size, 0)
    ImageDraw.Draw(rim).rounded_rectangle(
        [v * s for v in box], radius=PANEL_R * s, outline=255, width=max(1, int(1.2 * s)))
    fade = vertical_gradient(size[0], size[1], (0, 0, 0), (0, 0, 0)).convert("L")
    fd = ImageDraw.Draw(fade)
    for y in range(int(box[1] * s), int(box[3] * s)):
        t = (y - box[1] * s) / ((box[3] - box[1]) * s)
        fd.line([(0, y), (size[0], y)], fill=int(170 * (1 - t) ** 1.6 + 30))
    rim = ImageChops.multiply(rim, fade)
    bg = Image.composite(Image.new("RGB", size, (255, 255, 255)), bg, rim)

    # Specular streak inside the top edge.
    streak = Image.new("L", size, 0)
    ImageDraw.Draw(streak).rounded_rectangle(
        [(box[0] + 22) * s, (box[1] + 5) * s, (box[2] - 22) * s, (box[1] + 9) * s],
        radius=2 * s, fill=70)
    streak = streak.filter(ImageFilter.GaussianBlur(2.5 * s))
    bg = Image.composite(Image.new("RGB", size, (255, 255, 255)), bg, streak)

    # Name pill: mid grey, so Finder's black or white label both read on it.
    pill_box = (cx - LABEL_W / 2, LABEL_Y - LABEL_H / 2, cx + LABEL_W / 2, LABEL_Y + LABEL_H / 2)
    pill = rounded_mask(size, pill_box, LABEL_H / 2, s)
    bg = Image.composite(Image.new("RGB", size, (118, 124, 132)), bg, pill)
    pill_rim = Image.new("L", size, 0)
    ImageDraw.Draw(pill_rim).rounded_rectangle(
        [v * s for v in pill_box], radius=LABEL_H / 2 * s, outline=60, width=max(1, s))
    bg = Image.composite(Image.new("RGB", size, (255, 255, 255)), bg, pill_rim)
    return bg


def arrow(bg, s):
    size = bg.size
    x0, x1, y = APP_X + PANEL_W / 2 + 18, APPLICATIONS_X - PANEL_W / 2 - 18, ICON_Y - 12
    head, shaft = 14, 3.2

    shape = Image.new("L", size, 0)
    d = ImageDraw.Draw(shape)
    d.rounded_rectangle([x0 * s, (y - shaft / 2) * s, (x1 - head * 0.7) * s, (y + shaft / 2) * s],
                        radius=shaft / 2 * s, fill=255)
    d.polygon([(x1 * s, y * s), ((x1 - head) * s, (y - head * 0.66) * s),
               ((x1 - head) * s, (y + head * 0.66) * s)], fill=255)

    halo = shape.filter(ImageFilter.GaussianBlur(9 * s))
    bg = ImageChops.screen(bg, Image.composite(Image.new("RGB", size, GREEN), Image.new("RGB", size), halo))
    # Left-to-right from the icon green to a lighter green: motion toward the target.
    grad = Image.new("RGB", size)
    gd = ImageDraw.Draw(grad)
    for x in range(int(x0 * s), int(x1 * s) + 1):
        t = (x - x0 * s) / ((x1 - x0) * s)
        gd.line([(x, 0), (x, size[1])], fill=tuple(int(a + (b - a) * t) for a, b in zip(GREEN, GREEN_LIGHT)))
    return Image.composite(grad, bg, shape)


def render(scale):
    big = 2 * scale   # oversample the shapes, then scale down
    im = stage(big)
    im = glass_panel(im, APP_X, big)
    im = glass_panel(im, APPLICATIONS_X, big)
    im = arrow(im, big)
    im = im.resize((WIDTH * scale, HEIGHT * scale), Image.LANCZOS)

    # Text at its real size: hinted letterforms survive better than shrunk ones.
    d = ImageDraw.Draw(im)
    font = ImageFont.truetype(FONT, CAPTION_SIZE * scale, index=FONT_MEDIUM_INDEX)
    draw_tracked(d, (WIDTH * scale / 2, CAPTION_Y * scale), CAPTION_TEXT,
                 font, CAPTION_COLOUR, CAPTION_TRACKING)
    return im


def main():
    out = Path(__file__).resolve().parent.parent / "res" / "macos"
    one, two = out / "dmg-1x.png", out / "dmg-2x.png"
    render(1).save(one)
    render(2).save(two)

    target = out / "dmg-background.tiff"
    subprocess.run(
        ["tiffutil", "-cathidpicheck", str(one), str(two), "-out", str(target)],
        check=True, stdout=subprocess.DEVNULL)
    one.unlink()
    two.unlink()
    print(f"wrote {target}")


if __name__ == "__main__":
    sys.exit(main())
