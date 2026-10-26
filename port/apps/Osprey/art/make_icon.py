#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""Writes osprey_icon.svg and renders it into Osprey's AppIcon set.

    python3 port/apps/Osprey/art/make_icon.py

An osprey's crooked "M" wing silhouette in white over a chart-blue sea with
depth contours and a compass ring. Rendered with macOS QuickLook
(`qlmanage`) and flattened to the opaque RGB PNG an app icon must be.
"""

import os
import subprocess
import tempfile

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "osprey_icon.svg")
PNG = os.path.join(HERE, "..", "Assets.xcassets", "AppIcon.appiconset",
                   "AppIcon.png")

SEA_DEEP = "#0B2E4F"
SEA = "#1C5D8C"
SHALLOW = "#7FB8D8"
CONTOUR = "#A9D3EA"
WHITE = "#FFFFFF"
BUFF = "#E9C46A"


def wing_path():
    """The bird seen head-on from below: body, crooked wings, wrists low."""
    return ("M 512 520 "
            # Right wing: up to the shoulder, out and down to the wrist,
            # then up and out to the primaries.
            "C 560 470 610 430 660 440 "
            "C 700 448 720 500 760 500 "
            "C 810 500 860 430 900 360 "
            "C 880 450 840 540 770 570 "
            "C 720 590 690 540 650 520 "
            "C 610 505 570 540 540 580 "
            # Tail and the left wing, mirrored.
            "L 512 640 L 484 580 "
            "C 454 540 414 505 374 520 "
            "C 334 540 304 590 254 570 "
            "C 184 540 144 450 124 360 "
            "C 164 430 214 500 264 500 "
            "C 304 500 324 448 364 440 "
            "C 414 430 464 470 512 520 Z")


def main():
    svg = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024" '
           'width="1024" height="1024">',
           "<defs>",
           f'<radialGradient id="sea" cx="0.5" cy="0.4" r="0.75">'
           f'<stop offset="0" stop-color="{SEA}"/>'
           f'<stop offset="1" stop-color="{SEA_DEEP}"/></radialGradient>',
           "</defs>",
           '<rect width="1024" height="1024" fill="url(#sea)"/>',
           # A shoal in the lower left, edged by two depth contours.
           f'<path d="M 0 760 C 180 700 300 800 420 1024 L 0 1024 Z" '
           f'fill="{SHALLOW}" opacity="0.35"/>']
    for offset, dash in ((0, ""), (70, ' stroke-dasharray="18 14"')):
        svg.append(f'<path d="M 0 {690 - offset} C {200 + offset} {630 - offset} '
                   f'{360 + offset} {760 - offset} {500 + offset} 1024" fill="none" '
                   f'stroke="{CONTOUR}" stroke-width="7" opacity="0.6"{dash}/>')
    # The compass ring with ticks every 10 degrees, longer at the cardinals.
    svg.append(f'<circle cx="512" cy="512" r="410" fill="none" stroke="{CONTOUR}" '
               f'stroke-width="6" opacity="0.55"/>')
    for deg in range(0, 360, 10):
        length = 44 if deg % 90 == 0 else 22
        svg.append(f'<line x1="512" y1="{102}" x2="512" y2="{102 + length}" '
                   f'stroke="{CONTOUR}" stroke-width="{8 if deg % 90 == 0 else 5}" '
                   f'opacity="0.7" transform="rotate({deg} 512 512)"/>')
    # North marker.
    svg.append(f'<polygon points="512,70 538,128 486,128" fill="{BUFF}"/>')
    svg.append(f'<path d="{wing_path()}" fill="{WHITE}" stroke="{SEA_DEEP}" '
               f'stroke-width="10" stroke-linejoin="round" paint-order="stroke"/>')
    svg.append("</svg>")

    with open(OUT, "w") as f:
        f.write("\n".join(svg) + "\n")
    print(OUT)
    render()


def render():
    """Renders the SVG at 1024 and writes it as the icon set's opaque PNG."""
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(["qlmanage", "-t", "-s", "1024", "-o", tmp, OUT],
                       check=True, capture_output=True)
        img = Image.open(os.path.join(tmp, os.path.basename(OUT) + ".png"))
        img.convert("RGB").resize((1024, 1024), Image.LANCZOS).save(PNG)
    print(os.path.normpath(PNG))


if __name__ == "__main__":
    main()
