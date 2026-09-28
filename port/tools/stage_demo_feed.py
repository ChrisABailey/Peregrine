#!/usr/bin/env python3
"""Points the staged data pack's demo feed at a simulation track.

`stage_data.py` copies the tracked `pippin.ini` over the pack's, which puts
the demo keys back to their shipping values. Run this after it to get the
hold-then-ride track back.
"""

import argparse
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PACK = os.path.join(ROOT, "port", "apps", "Pippin", "Data")
KEYS = [("demo_track", '"%s"'), ("demo_time_scale", "%s"), ("demo_loop", "%s")]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--track", default="testdata/sim/kiawah_demo_2x.gpx",
                    help="the GPX to stage, relative to the repository root")
    ap.add_argument("--time-scale", default="1.0",
                    help="playback multiplier; 1.0 keeps the file's own timing, "
                         "including its stationary hold")
    ap.add_argument("--loop", default="false")
    args = ap.parse_args()

    source = os.path.join(ROOT, args.track)
    if not os.path.isfile(source):
        sys.exit(f"{args.track}: not there. port/tools/make_sim_gpx.py writes it.")
    name = os.path.basename(source)
    shutil.copy2(source, os.path.join(PACK, name))

    path = os.path.join(PACK, "pippin.ini")
    if not os.path.isfile(path):
        sys.exit("the pack is not staged: python3 port/apps/Pippin/stage_data.py")
    values = [name, args.time_scale, args.loop]
    lines = open(path, encoding="utf-8").read().splitlines(keepends=True)
    for i, line in enumerate(lines):
        for (key, form), value in zip(KEYS, values):
            if line.startswith(key + " "):
                lines[i] = "%s = %s\n" % (key, form % value)
    open(path, "w", encoding="utf-8").writelines(lines)
    print(f"demo feed: {name}, time_scale {args.time_scale}, loop {args.loop}")


if __name__ == "__main__":
    main()
