#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""Write Kiawah Trails Dark from Kiawah Trails.

The dark style is the light one with every colour replaced: layers, filters,
widths and zoom ranges are copied unchanged, so both styles show the same
features. Colours live in PALETTE below; edit them here and rerun rather than
editing kiawah-trails-dark.json by hand, or the next run discards the edit.

Also writes symbols/kiawah-dark.{png,json}: the shared sprite sheet with the
marsh tufts muted, because a pattern stamp ignores fill-opacity in
OsmStyleEngine.

    python3 port/tools/make_dark_style.py            # from the repo root
    python3 port/tools/make_dark_style.py SRC DST    # other paths

A colour property in the source with no PALETTE entry is an error, so a layer
added to Kiawah Trails cannot reach the dark style still light.
"""

import json
import os
import shutil
import sys

from PIL import Image

STYLES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Osm", "styles")

# Halo behind path, place and POI names.
HALO = "#2e2418"
# Layer id -> paint overrides. A stops value replaces the whole expression.
PALETTE = {
 "background": {"background-color": "#30342c"},
 "wood": {"fill-color": "#243624"},
 "golf-course": {"fill-color": "#26382a", "fill-outline-color": "#34503a"},
 "park": {"fill-color": "#283b28"},
 "grass": {"fill-color": "#2d482c", "fill-outline-color": {"stops": [[14, "#2d482c"], [16, "#3b5c37"]]}},
 "farmland": {"fill-color": "#433c31"},
 "marsh": {"fill-color": "#2a3a2d"},
 "tidalflat": {"fill-color": "#3a3628"},
 "beach": {"fill-color": "#54442e"},
 "sand": {"fill-color": "#58462b", "fill-outline-color": {"stops": [[14, "#58462b"], [15, "#715a35"]]}},
 "pitch": {"fill-color": "#2f472e", "fill-outline-color": "#3f5e3b"},
 "cemetery": {"fill-color": "#2e352b"},
 "parking": {"fill-color": "#443e35", "fill-outline-color": "#544c41"},
 "water": {"fill-color": "#33467a"},
 "pool": {"fill-color": "#3d5a8f", "fill-outline-color": "#5373a8"},
 "waterway": {"line-color": "#3d5590"},
 "golf-rough": {"fill-color": "#2a4029"},
 "golf-fairway": {"fill-color": "#31512f"},
 "golf-tee": {"fill-color": "#3a5e35", "fill-outline-color": "#4f7847"},
 "golf-green": {"fill-color": "#447242", "fill-outline-color": "#5c8c54"},
 "golf-bunker": {"fill-color": "#5e4b2e", "fill-outline-color": "#7c6439"},
 "golf-hazard": {"fill-color": "#33467a"},
 "golf-hole": {"line-color": "#d0d0d0", "line-opacity": 0.5},
 "building": {"fill-color": "#26221d", "fill-outline-color": {"stops": [[14, "#5a5a5a"], [15, "#8a8a8a"]]}},
 "pier-area": {"fill-color": "#5a4a35", "fill-outline-color": "#75603f"},
 "pier": {"line-color": "#7a6445"},
 "road-service-casing": {"line-color": "#1f1e1c"},
 "road-driveway-casing": {"line-color": "#232220"},
 "road-minor-casing": {"line-color": "#1f1e1c"},
 "road-major-casing": {"line-color": "#2a2419"},
 "road-service": {"line-color": "#5e574b"},
 "road-driveway": {"line-color": "#4b453c"},
 "road-minor": {"line-color": "#6b6252"},
 "road-major": {"line-color": "#94805a"},
 "golf-path": {"line-color": "#7a9168"},
 "track": {"line-color": "#97805a"},
 "boardwalk-casing": {"line-color": "#2a1d12"},
 "boardwalk": {"line-color": "#a07d4f"},
 "walk": {"line-color": "#c98b64"},
 "bike-casing": {"line-color": "#e2563d"},
 "bike-bridge-casing": {"line-color": "#7a7066"},
 "bike": {"line-color": "#f2b6a6"},
 "gate": {"circle-color": "#bdb2a4"},
 "crossing-ring": {"circle-color": "#c9bfb2"},
 "crossing-core": {"circle-color": "#30342c"},
 "golf-hole-marker": {"circle-color": "#3f8a44"},
 "golf-hole-number": {"text-color": "#ffffff"},
 "water-name": {"text-color": "#93b3e6", "text-halo-color": "#1d2744"},
 "river-name": {"text-color": "#93b3e6", "text-halo-color": "#1d2744"},
 "boardwalk-name": {"text-color": "#dcbc8c", "text-halo-color": HALO},
 "path-name": {"text-color": "#dba584", "text-halo-color": HALO},
 "road-name-minor": {"text-color": "#f1ede6", "text-halo-color": "#4d3e2e"},
 "road-name-major": {"text-color": "#f8f5ef", "text-halo-color": "#3d2e1e"},
 "poi-food": {"text-color": "#f0a46a", "text-halo-color": HALO},
 "poi-places": {"text-color": "#cbbfb0", "text-halo-color": HALO},
 "place-neighbourhood": {"text-color": "#ab9b82", "text-halo-color": HALO},
 "golf-course-name": {"text-color": "#92d48c", "text-halo-color": HALO},
 "place-town": {"text-color": "#e3d7c3", "text-halo-color": HALO},
 "place-island": {"text-color": "#d6c7aa", "text-halo-color": HALO},
}


COLOR_KEYS = {"background-color", "fill-color", "fill-outline-color", "line-color",
              "circle-color", "text-color", "text-halo-color"}

METADATA = {
    "peregrine:note": (
        "Kiawah Trails recoloured for a dark display: the same layers, filters and widths, "
        "with a dark brown-grey ground, dark blue water, dark green vegetation and dark brown "
        "sand. Not a night palette; every feature class stays. Generated by "
        "port/tools/make_dark_style.py; edit the palette there."),
    "peregrine:palette": (
        "Land #30342c, water #33467a, marsh #2a3a2d, beach #54442e, course #26382a, "
        "fairway #31512f, green #447242, bunker #5e4b2e, bike #e2563d, walk #c98b64, "
        "boardwalk #a07d4f, building #26221d outlined #8a8a8a."),
    "peregrine:derived-from": "kiawah-trails.json",
}

# The muted marsh tuft: opaque pixels become this colour at 60% of their alpha.
MARSH_TUFT = (0x6A, 0x84, 0x42)
MARSH_TUFT_ALPHA = 0.6


def recolor(style):
    """Applies PALETTE to `style` in place; exits naming any unmapped colour."""
    missing = []
    for layer in style["layers"]:
        paint = layer.get("paint", {})
        over = PALETTE.get(layer["id"], {})
        missing += ["%s.%s" % (layer["id"], k) for k in paint
                    if k in COLOR_KEYS and k not in over]
        paint.update(over)
    if missing:
        sys.exit("make_dark_style: no dark colour for " + ", ".join(missing))
    style["name"] = "Kiawah Trails Dark"
    style["id"] = "kiawah-trails-dark"
    style["sprite"] = "symbols/kiawah-dark"
    style.setdefault("metadata", {}).update(METADATA)


def write_style(style, path):
    """Writes one layer per line, the layout the hand-written styles use."""
    head = json.dumps({k: v for k, v in style.items() if k != "layers"}, indent=2)[:-2]
    layers = ",\n".join("    " + json.dumps(l) for l in style["layers"])
    with open(path, "w") as f:
        f.write(head + ',\n  "layers": [\n' + layers + "\n  ]\n}\n")


def write_sprite(styles_dir):
    """Copies the shared sheet to symbols/kiawah-dark with the marsh tile muted."""
    src = os.path.join(styles_dir, "symbols", "osm-liberty-topo")
    dst = os.path.join(styles_dir, "symbols", "kiawah-dark")
    shutil.copyfile(src + ".json", dst + ".json")
    with open(src + ".json") as f:
        t = json.load(f)["marsh_pattern"]
    img = Image.open(src + ".png").convert("RGBA")
    box = (t["x"], t["y"], t["x"] + t["width"], t["y"] + t["height"])
    tile = img.crop(box)
    px = tile.load()
    for y in range(tile.height):
        for x in range(tile.width):
            a = px[x, y][3]
            if a:
                px[x, y] = MARSH_TUFT + (int(a * MARSH_TUFT_ALPHA),)
    img.paste(tile, box)
    img.save(dst + ".png", optimize=True)
    return dst


def main(argv):
    src = argv[1] if len(argv) > 1 else os.path.join(STYLES, "kiawah-trails.json")
    dst = argv[2] if len(argv) > 2 else os.path.join(STYLES, "kiawah-trails-dark.json")
    with open(src) as f:
        style = json.load(f)
    recolor(style)
    write_style(style, dst)
    print("wrote %s (%d layers)" % (os.path.normpath(dst), len(style["layers"])))
    sheet = write_sprite(os.path.dirname(dst))
    print("wrote %s.{png,json}" % os.path.normpath(sheet))


if __name__ == "__main__":
    main(sys.argv)
