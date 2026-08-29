#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""Cut a bounding-box subset out of an MBTiles pyramid (Pippin plan, P1).

The whole point is that a phone carries a REGION, not a continent:
`us-south.mbtiles` is 4.4 GB of 4.9 M tiles and Kiawah Island is a few MB of
it. This is pure SQLite — the tiles are copied byte for byte, so a frame
rendered from the cut is pixel-identical to the same frame rendered from the
source (pinned by `OsmRender.KiawahCutMatchesSource`).

Usage (from the repo root):

    python3 port/tools/mbtiles_cut.py \
        --in  testdata/OSM/mbtiles/us-south.mbtiles \
        --out testdata/OSM/kiawah.mbtiles \
        --bounds=-80.17,32.55,-79.97,32.67

`--bounds` is west,south,east,north in degrees (spell it with an `=` — a
leading minus makes argparse read a separate argument as an option) — the order MBTiles' own
`metadata.bounds` uses, because that is the row this tool rewrites. (Note it
is NOT fvpack's/fvrender's lat-first order; the file format wins here.)

Two facts about the shape of the output:

  * A TILE IS ATOMIC. Every tile that INTERSECTS the box is copied whole, so
    the data actually present runs out to the tile grid, which at z0-z8 is
    most of a hemisphere. That is a feature — it is what gives a zoomed-out
    phone a map instead of a hole — and it is why `--bounds` is written to
    the metadata as the area of interest rather than as a claim about
    coverage. (The port derives coverage from the tiles regardless; the
    ledger's standing rule is that an MBTiles' declared bounds cannot be
    trusted.)
  * EVERY ZOOM IN THE SOURCE IS KEPT unless `--minzoom`/`--maxzoom` narrow
    it, so the cut zooms out as far as the original did.
"""

import argparse
import math
import os
import sqlite3
import sys


def tile_x_range(west, east, z):
    """Half-open [x0, x1] inclusive tile-column range covering [west, east]."""
    n = 1 << z
    def x_of(lon):
        return int(math.floor((lon + 180.0) / 360.0 * n))
    x0 = max(0, min(n - 1, x_of(west)))
    x1 = max(0, min(n - 1, x_of(east)))
    return x0, x1


def tile_y_range(south, north, z):
    """Inclusive TMS tile_row range covering [south, north].

    MBTiles rows are TMS (y counted from the SOUTH), while the Web Mercator
    formula gives XYZ (from the north), so the two ends swap: the northern
    edge is the SMALLEST XYZ y and therefore the LARGEST TMS row.
    """
    n = 1 << z
    def xyz_y_of(lat):
        lat = max(-85.0511287798, min(85.0511287798, lat))
        r = math.radians(lat)
        y = (1.0 - math.log(math.tan(r) + 1.0 / math.cos(r)) / math.pi) / 2.0
        return int(math.floor(y * n))
    y_north = max(0, min(n - 1, xyz_y_of(north)))
    y_south = max(0, min(n - 1, xyz_y_of(south)))
    # XYZ [y_north .. y_south] -> TMS [n-1-y_south .. n-1-y_north].
    return n - 1 - y_south, n - 1 - y_north


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--in", dest="src", required=True, help="source .mbtiles")
    ap.add_argument("--out", dest="dst", required=True, help="destination .mbtiles")
    ap.add_argument("--bounds", required=True,
                    help="west,south,east,north in degrees")
    ap.add_argument("--minzoom", type=int, default=None)
    ap.add_argument("--maxzoom", type=int, default=None)
    ap.add_argument("--name", default=None, help="metadata name for the cut")
    ap.add_argument("--force", action="store_true", help="overwrite --out")
    args = ap.parse_args()

    try:
        west, south, east, north = (float(v) for v in args.bounds.split(","))
    except ValueError:
        sys.exit("mbtiles_cut: --bounds wants west,south,east,north")
    if west >= east or south >= north:
        sys.exit("mbtiles_cut: --bounds must be west<east and south<north")
    if not os.path.isfile(args.src):
        sys.exit("mbtiles_cut: no such file: " + args.src)
    if os.path.exists(args.dst):
        if not args.force:
            sys.exit("mbtiles_cut: %s exists (use --force)" % args.dst)
        os.remove(args.dst)

    src = sqlite3.connect("file:%s?mode=ro" % args.src, uri=True)
    meta = dict(src.execute("SELECT name, value FROM metadata").fetchall())
    zooms = [z for (z,) in src.execute(
        "SELECT DISTINCT zoom_level FROM tiles ORDER BY zoom_level")]
    if not zooms:
        sys.exit("mbtiles_cut: source has no tiles")
    zmin = args.minzoom if args.minzoom is not None else zooms[0]
    zmax = args.maxzoom if args.maxzoom is not None else zooms[-1]

    dst = sqlite3.connect(args.dst)
    dst.execute("PRAGMA journal_mode=OFF")
    dst.execute("PRAGMA synchronous=OFF")
    dst.execute("CREATE TABLE metadata (name text, value text, UNIQUE (name))")
    dst.execute("CREATE TABLE tiles (zoom_level integer, tile_column integer,"
                " tile_row integer, tile_data blob)")
    dst.execute("CREATE UNIQUE INDEX tile_index on tiles"
                " (zoom_level, tile_column, tile_row)")

    total = 0
    kept_zooms = []
    for z in zooms:
        if z < zmin or z > zmax:
            continue
        x0, x1 = tile_x_range(west, east, z)
        y0, y1 = tile_y_range(south, north, z)
        rows = src.execute(
            "SELECT zoom_level, tile_column, tile_row, tile_data FROM tiles"
            " WHERE zoom_level=? AND tile_column BETWEEN ? AND ?"
            "   AND tile_row BETWEEN ? AND ?", (z, x0, x1, y0, y1)).fetchall()
        if rows:
            dst.executemany("INSERT INTO tiles VALUES (?,?,?,?)", rows)
            kept_zooms.append(z)
        total += len(rows)
        print("  z%-2d x %d..%d  y %d..%d  ->  %d tile(s)"
              % (z, x0, x1, y0, y1, len(rows)))

    if total == 0:
        sys.exit("mbtiles_cut: the box selected no tiles")

    # The source's metadata carried over, then the rows a cut invalidates.
    # `json` (the vector_layers manifest) is deliberately kept as-is: the cut
    # copies tiles whole, so every layer the source declared can still appear.
    meta["bounds"] = "%.6f,%.6f,%.6f,%.6f" % (west, south, east, north)
    meta["center"] = "%.6f,%.6f,%d" % ((west + east) / 2.0,
                                       (south + north) / 2.0, kept_zooms[-1])
    meta["minzoom"] = str(kept_zooms[0])
    meta["maxzoom"] = str(kept_zooms[-1])
    if args.name:
        meta["name"] = args.name
    dst.executemany("INSERT INTO metadata VALUES (?,?)", sorted(meta.items()))
    dst.commit()
    dst.execute("VACUUM")
    dst.close()
    src.close()

    print("mbtiles_cut: %s -> %s: %d tiles, z%d..%d, %.1f MB"
          % (args.src, args.dst, total, kept_zooms[0], kept_zooms[-1],
             os.path.getsize(args.dst) / 1048576.0))


if __name__ == "__main__":
    main()
