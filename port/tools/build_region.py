#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""Build a region's map tiles and road graph from a Geofabrik extract.

    python3 port/tools/build_region.py kiawah us-south-261021.osm.pbf
    python3 port/tools/build_region.py atlanta ~/Downloads/us-south-261021.osm.pbf
    python3 port/tools/build_region.py kiawah us-south-261021.osm.pbf --only graph -n

Writes `<region>.osm.pbf`, `<region>.fvroad` and `<region>.mbtiles` into the
output directory (default `testdata/OSM`), copying any existing file to
`.bak` first. A bare input name is looked for in the current directory,
`~/Downloads` and `testdata/OSM`.

Staging the app's data pack is separate: `stage_data.py` reads the outputs
from `testdata/OSM`. The per-region recipes are in `REGIONS` below and match
port/apps/Pippin/BUILDING.md.
"""

import argparse
import re
import shlex
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
TILEMAKER_DIR = Path.home() / "Documents/Source/tilemaker"
DEFAULT_OUT = REPO / "testdata/OSM"
STEPS = ("extract", "graph", "tiles")


@dataclass
class Region:
    """How one region is cut, routed and tiled."""
    bbox: str                      # west,south,east,north
    extract_args: list = field(default_factory=list)
    graph_args: list = field(default_factory=list)
    # Kiawah tiles from the full extract so relations crossing the box stay
    # whole; Atlanta tiles from its own cut, which is much faster.
    tiles_from_source: bool = False


REGIONS = {
    "kiawah": Region(
        bbox="-80.17,32.55,-79.97,32.67",
        # Keep whole ways that cross the box edge so roads are not truncated.
        extract_args=["--strategy=complete_ways"],
        graph_args=["--beach"],
        tiles_from_source=True),
    "atlanta": Region(
        bbox="-84.61,33.67,-84.25,33.97",
        graph_args=["--dem", str(REPO / "testdata/lidar")]),
}


def run(cmd, dry, cwd=None, capture=False):
    """Echoes and runs a command; exits on failure. Returns stdout if captured."""
    where = f"(cd {shlex.quote(str(cwd))}) " if cwd else ""
    print(f"\n$ {where}{shlex.join(str(c) for c in cmd)}", flush=True)
    if dry:
        return ""
    result = subprocess.run([str(c) for c in cmd], cwd=cwd, text=True,
                            stdout=subprocess.PIPE if capture else None)
    if result.returncode != 0:
        sys.exit(f"build_region: command failed ({result.returncode})")
    if capture:
        print(result.stdout, end="")
        return result.stdout
    return ""


def backup(path, dry):
    """Copies an existing output to `<name>.bak` before it is replaced."""
    if path.exists():
        print(f"backup {path.name} -> {path.name}.bak")
        if not dry:
            shutil.copy2(path, path.with_name(path.name + ".bak"))


def find_input(name):
    """Resolves the downloaded extract from a path or a bare file name."""
    given = Path(name).expanduser()
    candidates = [given] if given.is_absolute() or given.parent != Path(".") else [
        Path.cwd() / given, Path.home() / "Downloads" / given, DEFAULT_OUT / given]
    for c in candidates:
        if c.is_file():
            return c.resolve()
    sys.exit("build_region: input not found; looked at\n  "
             + "\n  ".join(str(c) for c in candidates))


def check_access(info):
    """Exits when `fvgraph info` reports no barred or private arcs."""
    counts = [int(n) for line in info.splitlines()
              if re.match(r"\s*access (barred|private)", line)
              for n in re.findall(r"(\d+) \w+", line)]
    if not counts or sum(counts) == 0:
        sys.exit("build_region: graph has no barred/private arcs; access tags were dropped")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("region", choices=sorted(REGIONS))
    ap.add_argument("input", help="downloaded .osm.pbf (path or bare name)")
    ap.add_argument("-o", "--out-dir", type=Path, default=DEFAULT_OUT,
                    help="where the outputs go (default testdata/OSM)")
    ap.add_argument("--only", choices=STEPS, action="append",
                    help="run just this step; repeat for several")
    ap.add_argument("--store", type=Path, default=Path("/tmp/tilemaker-store"),
                    help="tilemaker on-disk store, needs tens of GB when tiling "
                         "the full extract (default /tmp/tilemaker-store)")
    ap.add_argument("-n", "--dry-run", action="store_true", help="print the commands only")
    args = ap.parse_args()

    region = REGIONS[args.region]
    source = find_input(args.input)
    out = args.out_dir.expanduser().resolve()
    steps = args.only or list(STEPS)
    dry = args.dry_run
    if not dry:
        out.mkdir(parents=True, exist_ok=True)

    cut = out / f"{args.region}.osm.pbf"
    graph = out / f"{args.region}.fvroad"
    tiles = out / f"{args.region}.mbtiles"
    print(f"region {args.region}  bbox {region.bbox}\ninput  {source}\noutput {out}")

    if "extract" in steps:
        backup(cut, dry)
        # `--bbox=` with the equals sign: a leading minus is otherwise read as an option.
        run(["osmium", "extract", f"--bbox={region.bbox}", *region.extract_args,
             "--overwrite", "-o", cut, source], dry)

    if "graph" in steps:
        if not dry and not cut.exists():
            sys.exit(f"build_region: {cut} missing; run the extract step")
        run(["cmake", "--build", REPO / "build", "-j", "--target", "fvgraph"], dry)
        fvgraph = REPO / "build/port/Routing/fvgraph"
        backup(graph, dry)
        run([fvgraph, "build", *region.graph_args, "-o", graph, cut], dry)
        info = run([fvgraph, "info", graph], dry, capture=True)
        if not dry:
            check_access(info)

    if "tiles" in steps:
        tile_input = source if region.tiles_from_source else cut
        if not dry and not tile_input.exists():
            sys.exit(f"build_region: {tile_input} missing; run the extract step")
        # tilemaker deletes an existing output after a short warning.
        backup(tiles, dry)
        cmd = ["./tilemaker", "--input", tile_input, "--output", tiles,
               "--config", REPO / "port/Osm/config-peregrine.json",
               "--process", REPO / "port/Osm/process-peregrine.lua",
               f"--bbox={region.bbox}"]
        if region.tiles_from_source:
            if not dry:
                args.store.mkdir(parents=True, exist_ok=True)
            cmd += ["--store", args.store]
        # The config names the coastline and landcover shapefiles relative to the checkout.
        run(cmd, dry, cwd=TILEMAKER_DIR)

    print("\nbuild_region: done")


if __name__ == "__main__":
    main()
