#!/usr/bin/env python3
"""Turns a GPX track into waypoints for `simctl location start` or `devicectl simulate location route`.

Both commands move at one constant speed between waypoints and ignore timestamps, so
repeated fixes (a make_sim_gpx.py hold) are dropped and the speed defaults to the track's
own average. `--format simctl` prints `lat,lon` lines for `simctl location <dev> start -`;
`--format devicectl` prints the JSON that `--route-file` takes.
"""

import argparse
import datetime
import json
import math
import re
import sys

TRKPT = re.compile(r'<trkpt lat="([-\d.]+)" lon="([-\d.]+)"[^>]*>(.*?)</trkpt>', re.S)
TIME = re.compile(r"<time>([^<]+)</time>")


def read_track(path):
    """Returns [(lat, lon, datetime or None)] for every trkpt in the file."""
    text = open(path, encoding="utf-8").read()
    points = []
    for lat, lon, body in TRKPT.findall(text):
        m = TIME.search(body)
        t = datetime.datetime.strptime(m.group(1)[:19], "%Y-%m-%dT%H:%M:%S") if m else None
        points.append((float(lat), float(lon), t))
    return points


def metres(a, b):
    """Haversine distance between two (lat, lon, ...) points."""
    la1, lo1, la2, lo2 = map(math.radians, (a[0], a[1], b[0], b[1]))
    h = math.sin((la2 - la1) / 2) ** 2 + math.cos(la1) * math.cos(la2) * math.sin((lo2 - lo1) / 2) ** 2
    return 2 * 6371000 * math.asin(math.sqrt(h))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("gpx")
    ap.add_argument("--format", choices=("simctl", "devicectl"), default="devicectl")
    ap.add_argument("--speed", type=float,
                    help="metres per second; default is the track's moving average")
    ap.add_argument("--interval", type=float, default=1.0, help="seconds between fixes")
    ap.add_argument("--out-and-back", action="store_true",
                    help="append the track reversed, for runs longer than one pass")
    args = ap.parse_args()

    points = read_track(args.gpx)
    track = [p for i, p in enumerate(points) if i == 0 or p[:2] != points[i - 1][:2]]
    if len(track) < 2:
        sys.exit("%s: fewer than two distinct points" % args.gpx)

    speed = args.speed
    if speed is None:
        timed = [p for p in track if p[2] is not None]
        span = (timed[-1][2] - timed[0][2]).total_seconds() if len(timed) > 1 else 0
        length = sum(metres(a, b) for a, b in zip(track, track[1:]))
        speed = round(length / span, 2) if span > 0 else 5.0

    if args.out_and_back:
        track = track + track[-2::-1]

    if args.format == "simctl":
        for lat, lon, _ in track:
            print("%.7f,%.7f" % (lat, lon))
    else:
        print(json.dumps({"mode": "interval", "interval": args.interval, "speed": speed,
                          "waypoints": [{"latitude": lat, "longitude": lon} for lat, lon, _ in track]}))
    print("%d waypoints at %.2f m/s" % (len(track), speed), file=sys.stderr)


if __name__ == "__main__":
    main()
