#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""Write Atlanta Streets Dark from Atlanta Streets.

The dark style is the light one with every colour replaced: layers, filters,
widths and zoom ranges are copied unchanged, so both styles show the same
features and the same riding tiers. Colours live in PALETTE below; edit them
here and rerun rather than editing atlanta-streets-dark.json by hand.

    python3 port/tools/make_atlanta_dark_style.py            # from the repo root
    python3 port/tools/make_atlanta_dark_style.py SRC DST    # other paths

A colour property in the source with no PALETTE entry is an error, so a layer
added to Atlanta Streets cannot reach the dark style still light.
"""

import json
import os
import sys

STYLES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Osm", "styles")

# Halo behind place, path and POI names.
HALO = "#1f2023"
# Layer id -> paint overrides. A stops value replaces the whole function.
PALETTE = {
 "background": {"background-color": "#2b2d30"},
 "wood": {"fill-color": "#26332a"},
 "grass": {"fill-color": "#2c3a2c"},
 "wetland": {"fill-color": "#2b3830"},
 "golf-course": {"fill-color": "#303a2f", "fill-outline-color": {"stops": [[11, "#303a2f"], [14, "#3c4a3a"]]}},
 "nature-reserve": {"fill-color": "#2a3d2c", "fill-outline-color": "#3f5c3f"},
 "park": {"fill-color": "#2f4d2e", "fill-outline-color": {"stops": [[10, "#2f4d2e"], [13, "#4d7a45"]]}},
 "cemetery": {"fill-color": "#2f382e"},
 "campus": {"fill-color": "#36332e"},
 "hospital": {"fill-color": "#3d2f2f"},
 "pitch": {"fill-color": "#34502f", "fill-outline-color": "#4a6a42"},
 "parking": {"fill-color": "#333436", "fill-outline-color": {"stops": [[15.5, "#333436"], [17, "#434346"]]}},
 "water": {"fill-color": "#2f4a6b"},
 "pool": {"fill-color": "#3d5a8f", "fill-outline-color": "#5373a8"},
 "river": {"line-color": "#3a5a85"},
 "stream": {"line-color": "#34506f"},
 "boundary-county": {"line-color": "#4f4a5e"},
 "boundary-state": {"line-color": "#7d7499"},
 "boundary-country": {"line-color": "#948bb3"},
 "building": {"fill-color": "#3a3835", "fill-outline-color": {"stops": [[14.5, "#3a3835"], [15.5, "#57534e"]]}},
 "aeroway-runway": {"line-color": "#45434d"},
 "rail": {"line-color": "#77736d"},
 "rail-hatch": {"line-color": "#2b2d30"},
 "rail-tunnel": {"line-color": "#55524e"},
 "road-driveway-casing": {"line-color": "#3d3b38"},
 # Quiet and service streets stay neutral until close zooms, as in the light style.
 "road-service-casing": {"line-color": {"stops": [[14, "#45443f"], [15, "#4d6b55"]]}},
 "road-quiet-casing": {"line-color": {"stops": [[13.5, "#4a4844"], [14.5, "#5e8a69"]]}},
 # Moderate, busy and avoid are pushed apart in lightness and hue so a busy
 # road does not read as a moderate one on the dark ground.
 "road-moderate-casing": {"line-color": {"stops": [[11, "#6b6150"], [12, "#d9b03a"]]}},
 "road-busy-casing": {"line-color": {"stops": [[9, "#6a5b4a"], [11, "#f39545"]]}},
 "road-avoid-casing": {"line-color": {"stops": [[7, "#5f5246"], [10, "#ef5b52"]]}},
 "road-nobike-casing": {"line-color": "#8a8592"},
 "road-signed-casing": {"line-color": "#3fb56a"},
 "road-interstate-casing": {"line-color": "#8378c9"},
 "road-driveway": {"line-color": "#46443f"},
 "road-service": {"line-color": "#55534f"},
 "road-quiet": {"line-color": "#5c5a55"},
 "road-moderate": {"line-color": "#5f5a4a"},
 "road-busy": {"line-color": "#6e5a44"},
 "road-avoid": {"line-color": "#7a4844"},
 # The interstate is filled like a primary road; its indigo casing alone marks it.
 "road-interstate": {"line-color": "#7a4844"},
 "bridge-edge": {"line-color": "#8c877e"},
 "bridge-casing": {"line-color": [
     "case", ["==", ["get", "bicycle"], "designated"], "#3fb56a", ["==", ["get", "bicycle"], "no"], "#8a8592",
     ["match", ["get", "class"], "motorway", "#8378c9", ["primary", "trunk"], "#ef5b52", "secondary", "#f39545",
      "tertiary", "#d9b03a", "minor", "#5e8a69", "service", "#4d6b55", "#2cc4c9"]]},
 "bridge": {"line-color": [
     "match", ["get", "class"], "motorway", "#7a4844", ["primary", "trunk"], "#7a4844", "secondary", "#6e5a44",
     "tertiary", "#5f5a4a", ["path", "track"], "#173a3d", "#5c5a55"]},
 "tunnel-wash": {"line-color": "#2b2d30"},
 "bike-track": {"line-color": "#2cc4c9"},
 "bike-lane": {"line-color": "#3fb56a"},
 "bike-route": {"line-color": ["match", ["get", "network"],
                               "national", "#7fe0a4", "regional", "#3fb56a", "#2e7d4f"]},
 "bike-shared": {"line-color": "#3fb56a"},
 "track": {"line-color": "#97805a"},
 "sidewalk": {"line-color": "#7a7067"},
 "steps": {"line-color": "#c47a5e"},
 "walk": {"line-color": "#c98b64"},
 "trail-casing": {"line-color": "#2cc4c9"},
 "trail-unpaved": {"line-color": "#2cc4c9"},
 "trail": {"line-color": "#173a3d"},
 "oneway-arrow": {"text-color": "#b8b2a7"},
 "crossing-signals": {"circle-color": "#ff6b5e"},
 "crossing-marked": {"circle-color": "#6fa3d6"},
 "barrier": {"circle-color": "#bdb2a4"},
 "water-name": {"text-color": "#93b3e6", "text-halo-color": "#1d2744"},
 "river-name": {"text-color": "#93b3e6", "text-halo-color": "#1d2744"},
 "road-name-minor": {"text-color": "#e6e1d9", "text-halo-color": "#2b2d30"},
 "road-name-major": {"text-color": "#f5f1ea", "text-halo-color": HALO},
 "trail-name": {"text-color": "#8de3e6", "text-halo-color": "#16292a"},
 "path-name": {"text-color": "#dba584", "text-halo-color": HALO},
 "interstate-ref": {"text-color": "#ffffff", "text-halo-color": "#463f7a"},
 "poi-lodging": {"text-color": "#cbbfb0", "text-halo-color": HALO},
 "poi-food": {"text-color": "#f0a46a", "text-halo-color": HALO},
 "poi-grocery": {"text-color": "#cbbfb0", "text-halo-color": HALO},
 "poi-landmark": {"text-color": "#c9bfe0", "text-halo-color": HALO},
 "poi-bike-shop": {"text-color": "#8fe0a8", "text-halo-color": HALO},
 "poi-bike-repair": {"text-color": "#8de3e6", "text-halo-color": HALO},
 "poi-air": {"text-color": "#8de3e6", "text-halo-color": HALO},
 "poi-station": {"text-color": "#c3c8e6", "text-halo-color": HALO},
 "golf-course-name": {"text-color": "#8d9a7e", "text-halo-color": HALO},
 "park-name": {"text-color": "#9ad48e", "text-halo-color": HALO},
 "place-neighbourhood": {"text-color": "#a99c8b", "text-halo-color": HALO},
 "place-suburb": {"text-color": "#bfb3a2", "text-halo-color": HALO},
 "place-village": {"text-color": "#d0c5b5", "text-halo-color": HALO},
 "place-town": {"text-color": "#ddd2c2", "text-halo-color": HALO},
 "place-city": {"text-color": "#e8dfd0", "text-halo-color": HALO},
 "place-city-major": {"text-color": "#f3ece0", "text-halo-color": HALO},
 "place-state": {"text-color": "#a99fc9", "text-halo-color": HALO},
}

COLOR_KEYS = {"background-color", "fill-color", "fill-outline-color", "line-color",
              "circle-color", "text-color", "text-halo-color"}

METADATA = {
    "peregrine:note": (
        "Atlanta Streets recoloured for a dark display: the same layers, filters and widths, "
        "with a charcoal ground, dark blue water and dark green parks. The riding tiers keep "
        "their hues, brightened to read on the dark ground. Generated by "
        "port/tools/make_atlanta_dark_style.py; edit the palette there."),
    "peregrine:bike-tiers": (
        "Casing colour by tier: trail #2cc4c9; signed route #3fb56a; quiet #5e8a69 "
        "(neutral below z14); moderate #d9b03a; busy #f39545; avoid #ef5b52; "
        "no bicycles #8a8592; interstate #8378c9."),
    "peregrine:palette": (
        "Land #2b2d30, water #2f4a6b, park #2f4d2e, golf #303a2f, wood #26332a, "
        "building #3a3835, parking #333436, rail #77736d, state line #7d7499."),
    "peregrine:derived-from": "atlanta-streets.json",
}


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
        sys.exit("make_atlanta_dark_style: no dark colour for " + ", ".join(missing))
    style["name"] = "Atlanta Streets Dark"
    style["id"] = "atlanta-streets-dark"
    # The legend is labels and tags with no colours, so it is shared as is.
    legend = style.get("metadata", {}).get("peregrine:legend")
    style["metadata"] = dict(METADATA)
    if legend is not None:
        style["metadata"]["peregrine:legend"] = legend


def write_style(style, path):
    """Writes one layer per line, the layout the hand-written styles use."""
    head = json.dumps({k: v for k, v in style.items() if k != "layers"}, indent=2)[:-2]
    layers = ",\n".join("    " + json.dumps(l) for l in style["layers"])
    with open(path, "w") as f:
        f.write(head + ',\n  "layers": [\n' + layers + "\n  ]\n}\n")


def main(argv):
    src = argv[1] if len(argv) > 1 else os.path.join(STYLES, "atlanta-streets.json")
    dst = argv[2] if len(argv) > 2 else os.path.join(STYLES, "atlanta-streets-dark.json")
    with open(src) as f:
        style = json.load(f)
    recolor(style)
    write_style(style, dst)
    print("wrote %s (%d layers)" % (os.path.normpath(dst), len(style["layers"])))


if __name__ == "__main__":
    main(sys.argv)
