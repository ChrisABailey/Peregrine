#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""Build the small, redistributable sample-data tree from the full testdata/.

The output has the same layout as testdata/, so pointing FVW_TESTDATA_DIR at it
(or placing it at <repo>/testdata) runs the data-backed tests that its files
cover; tests pinned to files left out skip themselves. Most files are copied
byte for byte. The GeoTIFFs are windows cut from a much larger sheet, with
their georeferencing rewritten for the window and every other tag kept.

Usage (from the repo root):

    python3 port/tools/make_sampledata.py --src testdata --dest ../Peregrine/testdata

Requires Pillow. The destination must not exist or must be empty.
"""

import argparse
import os
import shutil
import sys

from PIL import Image, TiffImagePlugin, TiffTags

Image.MAX_IMAGE_PIXELS = None

# Whole files copied unchanged, relative to the testdata root.
COPY = [
    # DTED level 1: the pinned cell (w082/n31), Kiawah Island (w081/n32), and
    # two North Georgia / Appalachian relief cells in the upper-case W084
    # directory the case-insensitivity tests look for.
    "dted/w082/n31.dt1",
    "dted/w081/n32.dt1",
    "dted/W084/n34.dt1",
    "dted/W084/n35.dt1",

    # TIROS TopoBath: 16 km and 8 km each cover the whole world (the World
    # level is skipped by the enumerator); the two 500 m tiles are the ones
    # the frame and adapter tests pin.
    "tiros3/topobath/16km",
    "tiros3/topobath/8km",
    "tiros3/topobath/500m/TopoBath_500m_5530.wld",
    "tiros3/topobath/500m/TopoBath_500m_5531.wld",

    # NOAA ENC: the eight Charleston-area cells (usage bands 2-5), the root
    # exchange-set catalogue and NOAA's user agreement, the S-57 object and
    # attribute catalogue, and the S-52 presentation library.
    "enc/CATALOG.031",
    "enc/ENC_ROOT",
    "enc/US2EC02M",
    "enc/US3SC1CB",
    "enc/US4SC1BO",
    "enc/US4SC1CO",
    "enc/US5CHSDC",
    "enc/US5CHSDD",
    "enc/US5CHSEC",
    "enc/US5CHSED",
    "enc/s57objectclasses.csv",
    "enc/s57attributes.csv",
    "enc/s57expectedinput.csv",
    "enc/chartsymbols.xml",
    "enc/rastersymbols-day.png",
    "enc/rastersymbols-dusk.png",
    "enc/rastersymbols-dark.png",

    # OSM, Kiawah Island: the vector-tile cut, its routing graph, and the
    # overlapping XML exports the routing tests enumerate as map*.osm.
    "OSM/kiawah.mbtiles",
    "OSM/kiawah.fvroad",
    "OSM/map-2.osm",
    "OSM/map-3.osm",
    "OSM/map-4.osm",
    "OSM/map-5.osm",
    "OSM/map-6.osm",

    # NOAA CO-OPS tide predictions for the Kiawah River station.
    "tides/8667062.json",

    # A recorded bicycle ride on Kiawah, and the simulated-GPS feeds cut
    # from it for the demo and the location simulator.
    "kiawah_cycle.gpx",
    "sim",
]

# Never copied out of a directory entry above.
SKIP_NAMES = {".DS_Store"}

# GeoTIFF keys (tag 34735) and the model tags the window rewrites.
TAG_PIXEL_SCALE = 33550
TAG_TIEPOINT = 33922
GEO_TAGS = {
    33550: TiffTags.DOUBLE,
    33922: TiffTags.DOUBLE,
    34735: TiffTags.SHORT,
    34736: TiffTags.DOUBLE,
    34737: TiffTags.ASCII,
    270: TiffTags.ASCII,
}


def write_window(src, dst, left, top, width, height, reduce=1):
    """Writes a pixel window of a GeoTIFF with its tiepoint moved to the window.

    `reduce` box-filters the window by that integer factor and scales the
    pixel size to match.
    """
    im = Image.open(src)
    tags = im.tag_v2
    sx, sy = tags[TAG_PIXEL_SCALE][0], tags[TAG_PIXEL_SCALE][1]
    tie = list(tags[TAG_TIEPOINT])
    # Tiepoint maps raster (i, j) to model (x, y); shift it to the window origin.
    tie[3] += (left - tie[0]) * sx
    tie[4] -= (top - tie[1]) * sy
    tie[0], tie[1] = 0.0, 0.0

    out = TiffImagePlugin.ImageFileDirectory_v2()
    for tag, typ in GEO_TAGS.items():
        if tag in tags:
            out[tag] = tags[tag]
            out.tagtype[tag] = typ
    out[TAG_TIEPOINT] = tuple(tie)

    win = im.crop((left, top, left + width, top + height))
    if reduce > 1:
        win = win.reduce(reduce)
        sx, sy = sx * reduce, sy * reduce
        scale = list(tags[TAG_PIXEL_SCALE])
        scale[0], scale[1] = sx, sy
        out[TAG_PIXEL_SCALE] = tuple(scale)
    win.save(dst, tiffinfo=out)
    return tie, (sx, sy)


def write_tfw(path, tie, scale):
    """Writes an ESRI world file whose origin is the centre of the top-left pixel."""
    sx, sy = scale
    with open(path, "w") as f:
        f.write(f"{sx:.17f}\n0.0\n0.0\n{-sy:.17f}\n"
                f"{tie[3] + sx / 2:.17f}\n{tie[4] - sy / 2:.17f}\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--src", default="testdata")
    ap.add_argument("--dest", required=True)
    args = ap.parse_args()

    if os.path.exists(args.dest) and os.listdir(args.dest):
        sys.exit(f"{args.dest} exists and is not empty")

    for rel in COPY:
        s, d = os.path.join(args.src, rel), os.path.join(args.dest, rel)
        if os.path.isdir(s):
            shutil.copytree(s, d, ignore=shutil.ignore_patterns(*SKIP_NAMES))
        else:
            os.makedirs(os.path.dirname(d), exist_ok=True)
            shutil.copy2(s, d)

    gdir = os.path.join(args.dest, "geotiff")
    os.makedirs(gdir, exist_ok=True)

    # Charleston County 2012 1 ft colour orthophoto on SC State Plane (Lambert
    # Conformal Conic 2SP, US feet), Kiawah Island's west end: a 1 ft window
    # of marsh and maritime forest, and a 4 ft overview of the 2048 ft around
    # it. Same series at two resolutions, which the catalogue tests require.
    ortho = os.path.join(args.src, "geotiff/22620e2710n.tif")
    for name, left, top, size, reduce in (("22620e2710n_1ft", 1900, 2200, 1024, 1),
                                          ("22620e2710n_4ft", 1388, 1688, 2048, 4)):
        tie, scale = write_window(ortho, os.path.join(gdir, name + ".tif"),
                                  left, top, size, size, reduce)
        write_tfw(os.path.join(gdir, name + ".tfw"), tie, scale)

    shutil.copy2(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                              "sampledata_README.md"),
                 os.path.join(args.dest, "README.md"))

    total = 0
    for root, _, files in os.walk(args.dest):
        total += sum(os.path.getsize(os.path.join(root, n)) for n in files)
    print(f"{args.dest}: {total / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
