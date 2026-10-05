#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""Writes atlanta_icon.svg and renders it into the AppIconAtlanta icon set.

    python3 port/apps/Pippin/art/make_atlanta_icon.py

The render uses macOS QuickLook (`qlmanage`) and flattens the result to an
opaque RGB PNG, which is what an app icon must be.

An "A" drawn as a bicycle path running away to a vanishing point at the apex,
with the crossbar as a path crossing it. Red with a white keyline on navy.
The square is full and opaque; iOS applies the corner mask.
"""

import os
import subprocess
import tempfile

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "atlanta_icon.svg")
PNG = os.path.join(HERE, "..", "Pippin", "Assets.xcassets",
                   "AppIconAtlanta.appiconset", "AppIcon.png")

NAVY = "#13274F"
NAVY_DEEP = "#0B1A38"
RED = "#CE1141"
ROAD = "#22375F"
WHITE = "#FFFFFF"

# Geometry in a 1024 square.
APEX = (512, 150)          # vanishing point, the tip of the A
INNER_APEX_Y = 290         # where the inner edges of the legs meet
BASE_Y = 880               # bottom of the legs
OUTER_HALF = 360           # half-width of the A at the base, outside
INNER_HALF = 205           # half-width of the road surface at the base
BAR_Y, BAR_H = 600, 78     # crossing path: centre line and height
FOOT_W, FOOT_H = 120, 40   # serif feet
BIKE_Y = 690               # top of the bike-lane symbol


def x_at(half, y, top):
    """X offset from centre of an edge that runs from (512, top) to the base."""
    return half * (y - top) / (BASE_Y - top)


def leg(side):
    """One leg as a polygon: outer edge to the apex, inner edge to the inner apex."""
    s = -1 if side == "left" else 1
    cx = APEX[0]
    return [(cx, APEX[1]),
            (cx + s * OUTER_HALF, BASE_Y),
            (cx + s * INNER_HALF, BASE_Y),
            (cx, INNER_APEX_Y)]


def feet():
    """Slab serifs flaring outward from each leg's base."""
    cx = APEX[0]
    out = []
    for s in (-1, 1):
        x_out = cx + s * (OUTER_HALF + FOOT_W * 0.55)
        x_in = cx + s * (INNER_HALF - FOOT_W * 0.25)
        x0, x1 = sorted((x_out, x_in))
        out.append((x0, BASE_Y - FOOT_H, x1 - x0, FOOT_H + 6))
    return out


def bike_symbol():
    """The painted bike-lane bicycle, foreshortened, on the near road surface."""
    frame = [((45, 80), (95, 80)), ((95, 80), (80, 35)), ((80, 35), (45, 80)),
             ((95, 80), (140, 40)), ((140, 40), (155, 80)), ((80, 35), (140, 40)),
             ((130, 26), (152, 26)), ((140, 40), (142, 26)), ((68, 30), (92, 30))]
    parts = [f'<circle cx="{x}" cy="80" r="38"/>' for x in (45, 155)]
    parts += [f'<line x1="{a[0]}" y1="{a[1]}" x2="{b[0]}" y2="{b[1]}"/>' for a, b in frame]
    return (f'<g transform="translate({APEX[0] - 125},{BIKE_Y}) scale(1.25,0.8)" fill="none" '
            f'stroke="{WHITE}" stroke-width="9" stroke-linecap="round" stroke-linejoin="round">'
            + "".join(parts) + "</g>")


def centre_dashes():
    """Lane dashes on the far road surface, shrinking toward the vanishing point."""
    cx = APEX[0]
    dashes = []
    # Equal steps in depth (1/z), which bunch up toward the horizon.
    near, far = 1.6, 9.0
    n = 10
    for i in range(n):
        z0 = near + (far - near) * i / n
        z1 = z0 + (far - near) / n * 0.55
        y0 = INNER_APEX_Y + (BASE_Y - INNER_APEX_Y) / z0
        y1 = INNER_APEX_Y + (BASE_Y - INNER_APEX_Y) / z1
        if y1 < INNER_APEX_Y + 30:
            break
        if y0 > BAR_Y - BAR_H / 2:
            continue
        w0 = 22 / z0
        w1 = 22 / z1
        dashes.append([(cx - w0 / 2, y0), (cx + w0 / 2, y0),
                       (cx + w1 / 2, y1), (cx - w1 / 2, y1)])
    return dashes


def pts(poly):
    return " ".join(f"{x:.1f},{y:.1f}" for x, y in poly)


def main():
    cx = APEX[0]
    road = [(cx, INNER_APEX_Y), (cx + INNER_HALF, BASE_Y), (cx - INNER_HALF, BASE_Y)]
    bar_top, bar_bot = BAR_Y - BAR_H / 2, BAR_Y + BAR_H / 2
    # The crossing path runs past the legs and fades out on both sides.
    bar_reach = x_at(OUTER_HALF, BAR_Y, APEX[1]) + 150
    a_parts = [leg("left"), leg("right")]
    lx0, lx1 = cx - bar_reach, cx + bar_reach

    svg = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024" width="1024" height="1024">',
           "<defs>",
           f'<linearGradient id="bg" x1="0" y1="0" x2="0" y2="1">'
           f'<stop offset="0" stop-color="{NAVY_DEEP}"/><stop offset="1" stop-color="{NAVY}"/></linearGradient>',
           f'<linearGradient id="bar" x1="0" y1="0" x2="1" y2="0">'
           f'<stop offset="0" stop-color="{RED}" stop-opacity="0"/>'
           f'<stop offset="0.16" stop-color="{RED}"/><stop offset="0.84" stop-color="{RED}"/>'
           f'<stop offset="1" stop-color="{RED}" stop-opacity="0"/></linearGradient>',
           f'<linearGradient id="barline" x1="0" y1="0" x2="1" y2="0">'
           f'<stop offset="0" stop-color="{WHITE}" stop-opacity="0"/>'
           f'<stop offset="0.2" stop-color="{WHITE}"/><stop offset="0.8" stop-color="{WHITE}"/>'
           f'<stop offset="1" stop-color="{WHITE}" stop-opacity="0"/></linearGradient>',
           f'<linearGradient id="dashfade" gradientUnits="userSpaceOnUse" x1="{lx0:.0f}" y1="0" x2="{lx1:.0f}" y2="0">'
           f'<stop offset="0" stop-color="{WHITE}" stop-opacity="0"/>'
           f'<stop offset="0.2" stop-color="{WHITE}"/><stop offset="0.8" stop-color="{WHITE}"/>'
           f'<stop offset="1" stop-color="{WHITE}" stop-opacity="0"/></linearGradient>',
           "</defs>",
           '<rect width="1024" height="1024" fill="url(#bg)"/>',
           # The road surface inside the A, with its lane line.
           f'<polygon points="{pts(road)}" fill="{ROAD}"/>']
    for d in centre_dashes():
        svg.append(f'<polygon points="{pts(d)}" fill="{WHITE}" opacity="0.9"/>')
    svg.append(bike_symbol())

    # The crossing path: a white keyline band, the red band, a dashed centre line.
    x0, x1 = cx - bar_reach, cx + bar_reach
    svg.append(f'<rect x="{x0:.1f}" y="{bar_top - 9:.1f}" width="{x1 - x0:.1f}" height="{BAR_H + 18}" fill="url(#barline)"/>')
    svg.append(f'<rect x="{x0:.1f}" y="{bar_top:.1f}" width="{x1 - x0:.1f}" height="{BAR_H}" fill="url(#bar)"/>')
    svg.append(f'<line x1="{x0 + 60:.1f}" y1="{BAR_Y}" x2="{x1 - 60:.1f}" y2="{BAR_Y}" stroke="url(#dashfade)" '
               f'stroke-width="9" stroke-dasharray="34 26" stroke-linecap="round"/>')

    # The legs and feet over the crossing, keylined in white.
    for poly in a_parts:
        svg.append(f'<polygon points="{pts(poly)}" fill="{RED}" stroke="{WHITE}" '
                   f'stroke-width="18" stroke-linejoin="miter" paint-order="stroke"/>')
    for x, y, w, h in feet():
        svg.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h}" rx="6" fill="{RED}" '
                   f'stroke="{WHITE}" stroke-width="18" paint-order="stroke"/>')
    # Re-fill the legs so the feet's keyline does not cut across them.
    for poly in a_parts:
        svg.append(f'<polygon points="{pts(poly)}" fill="{RED}"/>')
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
