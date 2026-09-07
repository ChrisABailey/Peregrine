#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""Turn the artwork in this directory into Pippin's app icon.

    python3 port/apps/Pippin/art/make_icon.py

The artwork (`turnstone_icon.svg`, and `turnstone_icon_1024.png` rendered from
it) draws its OWN rounded rectangle: the corners of the 1024 square are
transparent. iOS does not want that. The system applies the superellipse mask
itself, and an icon that arrives pre-rounded is either masked twice — a rounded
square floating inside a rounded square — or shows black where the alpha is.
An app icon has to be a full, OPAQUE square.

So this script paints the square the artwork's own background gradient would
have covered, composites the artwork over it, and drops the alpha channel. The
interior is unchanged to within rounding (the background under the bird is the
same gradient); only the four corners gain pixels, and iOS masks most of those
away again — which is exactly the point.
"""

import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "turnstone_icon_1024.png")
DST = os.path.join(HERE, "..", "Pippin", "Assets.xcassets",
                   "AppIcon.appiconset", "AppIcon.png")

SIZE = 1024

# The `bg` linearGradient in turnstone_icon.svg: top to bottom, sRGB stops.
BG_STOPS = [(0.00, (0x12, 0x60, 0x6D)),
            (0.55, (0x2C, 0x80, 0x90)),
            (1.00, (0x57, 0xAD, 0xB6))]

# The `glow` radialGradient: white, 0.22 alpha at the centre falling to 0 at
# the edge, centred at 0.32/0.24 of the box with a radius of 0.85 of it.
GLOW_CENTER = (0.32, 0.24)
GLOW_RADIUS = 0.85
GLOW_ALPHA = 0.22


def _bg_color(t):
    for (t0, c0), (t1, c1) in zip(BG_STOPS, BG_STOPS[1:]):
        if t <= t1:
            f = 0.0 if t1 == t0 else (t - t0) / (t1 - t0)
            return tuple(a + (b - a) * f for a, b in zip(c0, c1))
    return tuple(float(v) for v in BG_STOPS[-1][1])


def background(size):
    """The two background rects of the SVG, without the rounded clip."""
    img = Image.new("RGB", (size, size))
    px = img.load()
    cx, cy = GLOW_CENTER[0] * size, GLOW_CENTER[1] * size
    r = GLOW_RADIUS * size
    for y in range(size):
        base = _bg_color((y + 0.5) / size)
        for x in range(size):
            d = (((x + 0.5) - cx) ** 2 + ((y + 0.5) - cy) ** 2) ** 0.5
            a = GLOW_ALPHA * max(0.0, 1.0 - d / r)
            px[x, y] = tuple(int(round(c * (1.0 - a) + 255.0 * a)) for c in base)
    return img


def main():
    if not os.path.exists(SRC):
        sys.exit("missing artwork: %s" % SRC)
    art = Image.open(SRC).convert("RGBA")
    if art.size != (SIZE, SIZE):
        art = art.resize((SIZE, SIZE), Image.LANCZOS)
    out = background(SIZE)
    out.paste(art, (0, 0), art)          # alpha composite, then no alpha left
    out.save(DST, "PNG", optimize=True)
    print("wrote %s (%dx%d, opaque)" % (os.path.relpath(DST, HERE), SIZE, SIZE))


if __name__ == "__main__":
    main()
