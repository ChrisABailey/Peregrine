#!/usr/bin/env python3
"""Builds an Xcode location-simulation GPX from a recorded track.

The output holds the first fix for a dwell period so a demo can start the route
while stationary, then replays the track. Xcode interpolates between waypoints
using their <time> values, so the dwell and the playback speed are both carried
by the timestamps.
"""

import argparse
import datetime
import math
import re
import sys

TRKPT = re.compile(
    r'<trkpt lat="([-\d.]+)" lon="([-\d.]+)">\s*<ele>([-\d.]+)</ele>\s*'
    r'<time>([^<]+)</time>'
)
BASE = datetime.datetime(2026, 1, 1, 0, 0, 0)


def read_track(path):
    """Returns [(lat, lon, ele, seconds-from-track-start)] for a 1.1 GPX track."""
    text = open(path, encoding="utf-8").read()
    points = []
    for lat, lon, ele, stamp in TRKPT.findall(text):
        t = datetime.datetime.strptime(stamp, "%Y-%m-%dT%H:%M:%SZ")
        points.append((float(lat), float(lon), float(ele), t))
    if not points:
        sys.exit(f"{path}: no <trkpt> with <ele> and <time> found")
    t0 = points[0][3]
    return [(p[0], p[1], p[2], (p[3] - t0).total_seconds()) for p in points]


def haversine(a, b):
    """Great-circle distance in metres between (lat, lon) pairs."""
    r = 6371000.0
    la1, lo1, la2, lo2 = map(math.radians, (a[0], a[1], b[0], b[1]))
    h = (math.sin((la2 - la1) / 2) ** 2
         + math.cos(la1) * math.cos(la2) * math.sin((lo2 - lo1) / 2) ** 2)
    return 2 * r * math.asin(math.sqrt(h))


def sample(points, t):
    """Linearly interpolates (lat, lon, ele) at t seconds into the source track."""
    if t <= points[0][3]:
        return points[0][:3]
    if t >= points[-1][3]:
        return points[-1][:3]
    lo, hi = 0, len(points) - 1
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if points[mid][3] <= t:
            lo = mid
        else:
            hi = mid
    a, b = points[lo], points[hi]
    span = b[3] - a[3]
    f = 0.0 if span <= 0 else (t - a[3]) / span
    return tuple(a[i] + (b[i] - a[i]) * f for i in range(3))


def build(points, hold, speed, motion_limit, step):
    """Returns [(lat, lon, ele, seconds)] for the simulation timeline.

    Playback is resampled onto a uniform `step` grid rather than taking the
    source points as they come: at any speed but 1x their spacing no longer
    matches the output cadence, and uneven spacing at a uniform cadence is
    what a consumer that derives speed from the geometry reads as lurching.
    """
    out = []
    lat0, lon0, ele0, _ = points[0]
    t = 0.0
    while t < hold:
        out.append((lat0, lon0, ele0, t))
        t += step
    duration = points[-1][3] / speed
    if motion_limit is not None:
        duration = min(duration, motion_limit)
    t = 0.0
    while t <= duration + 1e-9:
        lat, lon, ele = sample(points, t * speed)
        out.append((lat, lon, ele, hold + t))
        t += step
    return out


def write(path, name, rows):
    stamp = lambda s: (BASE + datetime.timedelta(seconds=s)).strftime("%Y-%m-%dT%H:%M:%SZ")
    with open(path, "w", encoding="utf-8") as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n')
        f.write('<gpx version="1.1" creator="make_sim_gpx.py" '
                'xmlns="http://www.topografix.com/GPX/1/1">\n')
        f.write(f"  <metadata><name>{name}</name></metadata>\n")
        f.write(f"  <trk>\n    <name>{name}</name>\n    <trkseg>\n")
        for lat, lon, ele, s in rows:
            f.write(f'      <trkpt lat="{lat:.7f}" lon="{lon:.7f}">'
                    f"<ele>{ele:.1f}</ele><time>{stamp(s)}</time></trkpt>\n")
        f.write("    </trkseg>\n  </trk>\n</gpx>\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("source")
    ap.add_argument("output")
    ap.add_argument("--hold", type=float, default=15.0,
                    help="seconds stationary on the first fix (default 15)")
    ap.add_argument("--speed", type=float, default=1.0,
                    help="playback speed multiplier (default 1)")
    ap.add_argument("--motion-seconds", type=float, default=None,
                    help="trim playback after this many seconds of motion")
    ap.add_argument("--step", type=float, default=1.0,
                    help="seconds between the stationary fixes (default 1)")
    ap.add_argument("--name", default="Pippin demo")
    args = ap.parse_args()

    points = read_track(args.source)
    rows = build(points, args.hold, args.speed, args.motion_seconds, args.step)
    write(args.output, args.name, rows)

    moving = [r for r in rows[1:] if (r[0], r[1]) != (rows[0][0], rows[0][1])]
    dist = sum(haversine(moving[i], moving[i + 1]) for i in range(len(moving) - 1))
    total = rows[-1][3]
    print(f"{args.output}: {len(rows)} fixes, {total:.0f}s total "
          f"({args.hold:.0f}s hold + {total - args.hold:.0f}s motion), "
          f"{dist / 1000:.2f} km at {args.speed:g}x "
          f"({dist / max(total - args.hold, 1) * 3.6:.1f} km/h on screen)")


if __name__ == "__main__":
    main()
