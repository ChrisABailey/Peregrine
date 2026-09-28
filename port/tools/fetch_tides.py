#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""Fetch NOAA's high/low tide predictions for one station into a tides.json.

The table is read by fv::nav::TideTable (port/include/fvkit/nav/tide.h). It
covers whole UTC calendar years; the extremes are padded by two days on each
side so the curve is defined over the full span.

Usage (from the repo root):

    python3 port/tools/fetch_tides.py 8667062 2026-2030 testdata/tides/8667062.json

`years` is one year (`2026`), an inclusive range (`2026-2030`), or one month
(`2026-03`, with `--samples` only). `--samples` also writes NOAA's 6-minute
predictions over the span, which the interpolation test measures against;
NOAA publishes these for harmonic stations only. Data is from
the NOAA CO-OPS API, heights in metres above MLLW, times in GMT. NOAA's
predictions are US Government work and in the public domain.

Output (one extreme per line, so a diff of two fetches is readable):

    {"format": "peregrine-tides/1", "station": {...}, "datum": "MLLW",
     "units": "m", "valid_from": <unix s>, "valid_until": <unix s>,
     "extremes": [[<unix s>, <height m>, "H"|"L"], ...]}
"""

import argparse
import calendar
import datetime
import json
import os
import sys
import time
import urllib.parse
import urllib.request

DATAGETTER = "https://api.tidesandcurrents.noaa.gov/api/prod/datagetter"
MDAPI = "https://api.tidesandcurrents.noaa.gov/mdapi/prod/webapi/stations/%s.json"
APPLICATION = "Peregrine"
PAD_DAYS = 2


def get_json(url):
    """GETs a URL and decodes the JSON body; raises on an HTTP or API error."""
    req = urllib.request.Request(url, headers={"User-Agent": APPLICATION})
    with urllib.request.urlopen(req, timeout=60) as r:
        doc = json.load(r)
    if "error" in doc:
        raise RuntimeError("%s: %s" % (url, doc["error"].get("message", doc["error"])))
    return doc


def station_block(station):
    """The station's name, position and, for a subordinate station, its offsets."""
    doc = get_json(MDAPI % station + "?expand=tidepredoffsets")
    s = doc["stations"][0]
    out = {"id": s["id"], "name": s["name"], "lat": s["lat"], "lon": s["lng"]}
    off = (s.get("tidePredOffsets") or {})
    if off.get("type") == "S":
        out["type"] = "subordinate"
        out["reference_id"] = off.get("refStationId")
        out["offsets"] = {
            "high_time_min": off.get("timeOffsetHighTide"),
            "low_time_min": off.get("timeOffsetLowTide"),
            "high_height": off.get("heightOffsetHighTide"),
            "low_height": off.get("heightOffsetLowTide"),
            "height_type": off.get("heightAdjustedType"),  # R ratio, A additive
        }
    else:
        out["type"] = "harmonic"
    return out


def fetch_hilo(station, begin, end):
    """Every predicted extreme in [begin, end] (dates), as (unix_s, height_m, 'H'|'L')."""
    q = urllib.parse.urlencode({
        "product": "predictions", "interval": "hilo", "datum": "MLLW",
        "time_zone": "gmt", "units": "metric", "format": "json",
        "station": station, "application": APPLICATION,
        "begin_date": begin.strftime("%Y%m%d"),
        "end_date": end.strftime("%Y%m%d"),
    })
    doc = get_json(DATAGETTER + "?" + q)
    rows = []
    for p in doc["predictions"]:
        t = datetime.datetime.strptime(p["t"], "%Y-%m-%d %H:%M")
        rows.append((calendar.timegm(t.timetuple()), float(p["v"]), p["type"]))
    return rows


def fetch_samples(station, begin, end):
    """NOAA's 6-minute predicted heights over [begin, end) as (start_s, [m, ...])."""
    q = urllib.parse.urlencode({
        "product": "predictions", "interval": "6", "datum": "MLLW",
        "time_zone": "gmt", "units": "metric", "format": "json",
        "station": station, "application": APPLICATION,
        "begin_date": begin.strftime("%Y%m%d"),
        "end_date": (end - datetime.timedelta(days=1)).strftime("%Y%m%d"),
    })
    preds = get_json(DATAGETTER + "?" + q)["predictions"]
    t0 = datetime.datetime.strptime(preds[0]["t"], "%Y-%m-%d %H:%M")
    return calendar.timegm(t0.timetuple()), [float(p["v"]) for p in preds]


def parse_span(text):
    """'2026', '2026-2030' or '2026-03' -> (begin date, end date exclusive)."""
    a, _, b = text.partition("-")
    first = int(a)
    if b and len(b) <= 2:  # a single month
        month = int(b)
        end = datetime.date(first + month // 12, month % 12 + 1, 1)
        return datetime.date(first, month, 1), end
    last = int(b or a)
    if last < first:
        raise ValueError("years run backwards: %s" % text)
    return datetime.date(first, 1, 1), datetime.date(last + 1, 1, 1)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("station", help="NOAA station id, e.g. 8667062")
    ap.add_argument("years", help="2026, 2026-2030 (inclusive, UTC) or 2026-03")
    ap.add_argument("out", help="the tides.json to write")
    ap.add_argument("--samples", action="store_true",
                    help="also write the 6-minute predictions (a test fixture)")
    args = ap.parse_args()

    span_begin, span_end = parse_span(args.years)
    pad = datetime.timedelta(days=PAD_DAYS)
    rows = {}
    # One request per year at most: the API caps a hilo request at a year.
    chunk = span_begin - pad
    last_day = span_end - datetime.timedelta(days=1) + pad
    while chunk <= last_day:
        chunk_end = min(datetime.date(chunk.year, 12, 31), last_day)
        for t, h, kind in fetch_hilo(args.station, chunk, chunk_end):
            rows[t] = (t, h, kind)
        chunk = chunk_end + datetime.timedelta(days=1)
        time.sleep(0.5)
    extremes = [rows[t] for t in sorted(rows)]

    doc = {
        "format": "peregrine-tides/1",
        "source": "NOAA CO-OPS tide predictions (public domain)",
        "fetched": datetime.date.today().isoformat(),
        "station": station_block(args.station),
        "datum": "MLLW",
        "units": "m",
        "valid_from": calendar.timegm(span_begin.timetuple()),
        "valid_until": calendar.timegm(span_end.timetuple()),
    }
    if args.samples:
        start, heights = fetch_samples(args.station, span_begin, span_end)
        doc["samples"] = {"start": start, "step_s": 360, "heights_m": heights}
    head = json.dumps(doc, indent=1, separators=(",", ": "))[:-2]
    body = ",\n".join('  [%d, %s, "%s"]' % (t, repr(h), k) for t, h, k in extremes)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w") as f:
        f.write(head + ',\n "extremes": [\n' + body + "\n ]\n}\n")
    print("%s: %d extremes, %s to %s, %.1f KB" % (args.out, len(extremes), span_begin,
                                                 span_end, os.path.getsize(args.out) / 1024.0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
