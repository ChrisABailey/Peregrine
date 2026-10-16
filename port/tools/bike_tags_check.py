#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""Compare the bike and speed attributes the tile profile and the graph builder read (CR6).

For every highway way in an OSM extract, applies the rules of the tile profile
(`process-peregrine.lua`: `CyclewayOnRoad`, `PostedSpeedKph`, `LanesCount`,
`BikeRouteOnWay`) and of the graph builder (`fv_road_graph.cpp`: `SideProvision`,
`ParseMaxSpeedKph`, `ParseLanes`, `BikeRouteFromNetwork`), and lists the ways
where the two readings differ.

The graph side and the tile side are transcribed here; keep them in step with
the C++ and the Lua when either changes. The check does not decode the tiles, so
it cannot see a bug in the Lua itself, only a disagreement between the rules.

Usage (from the repo root):

    python3 port/tools/bike_tags_check.py testdata/OSM/atlanta.osm.pbf

Needs `osmium` (osmium-tool) on the PATH; the extract is converted to OSM XML
in a temporary directory.

With `--legend STYLE`, checks a style's `peregrine:legend` instead: each
entry's raw `osm` tags are run through the real `process-peregrine.lua` (via
`legend_way_harness.lua`, needs `lua` on the PATH), and the tile attributes it
writes are compared with the entry's specimen `tags`. A specimen must carry
every attribute the Lua writes that the style reads on that layer.

    python3 port/tools/bike_tags_check.py --legend port/Osm/styles/atlanta-streets.json
"""

import json

import argparse
import collections
import os
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

# Tile side: cycleway ranks as CyclewayOnRoad orders them (strongest wins).
LUA_CYCLEWAY_RANK = {
    "track": 4, "opposite_track": 4,
    "lane": 3, "opposite_lane": 3,
    "share_busway": 2, "opposite_share_busway": 2,
    "shared_lane": 1,
}
LUA_CYCLEWAY_NAME = {4: "track", 3: "lane", 2: "share_busway", 1: "shared_lane"}

# Graph side: the provision each value gives, and the ones it treats as none.
GRAPH_PROVISION = {"track": "track", "lane": "lane", "buffered_lane": "lane",
                   "share_busway": "lane", "shared_lane": "shared_lane"}
GRAPH_RANK = {"track": 4, "lane": 3, "shared_lane": 1}
# The tile's cycleway name as the graph's provision, for comparison.
LUA_TO_PROVISION = {"track": "track", "lane": "lane", "share_busway": "lane", "shared_lane": "shared_lane", None: None}

ROUTE_RANK = {"local": 1, "regional": 2, "national": 3}

HERE = os.path.dirname(os.path.abspath(__file__))
PROFILE = os.path.join(HERE, "..", "Osm", "process-peregrine.lua")
HARNESS = os.path.join(HERE, "legend_way_harness.lua")

# The route relation each `peregrine:route_network` pseudo-tag stands for.
ROUTE_RELATION = {"national": "ncn", "regional": "rcn", "local": "lcn"}

LEGACY_KEY_OPS = {"==", "!=", "<", "<=", ">", ">=", "in", "!in", "has", "!has"}


def lua_posted_kph(v):
    """PostedSpeedKph in process-peregrine.lua; None when nothing usable."""
    if v is None or v == "":
        return None
    if v == "walk":
        return 5
    m = re.match(r"^\s*(\d+)", v)
    if not m:
        return None
    value = int(m.group(1))
    if re.match(r"^\s*\d+\s*mph", v):
        value = int(value * 1.609344 + 0.5)
    if value < 1:
        return None
    return min(value, 255)


def graph_posted_kph(v):
    """ParseMaxSpeedKph in fv_road_graph.cpp; 0 means nothing usable (the C++ returns 0)."""
    if v is None:
        return 0
    m = re.match(r"^[ \t]*(\d+)", v)
    if not m:
        return 5 if v == "walk" else 0
    value = int(m.group(1))
    rest = v[m.end():].lstrip(" ")
    if rest.startswith("mph"):
        value = int(value * 1.609344 + 0.5)
    if value < 1:
        return 0
    return min(value, 255)


def lua_lanes(v):
    """LanesCount in process-peregrine.lua; None when absent."""
    if v is None:
        return None
    m = re.match(r"^\s*(-?\d+)", v)
    if not m:
        return None
    return max(0, min(15, int(m.group(1))))


def graph_lanes(v):
    """ParseLanes in fv_road_graph.cpp: atoi, clamped to 0..15; 0 when absent."""
    if v is None:
        return 0
    m = re.match(r"^\s*(-?\d+)", v)
    n = int(m.group(1)) if m else 0
    return max(0, min(15, n))


def lua_cycleway(tags):
    """CyclewayOnRoad: strongest across cycleway, :both, :right, :left; None if none."""
    best = 0
    for key in ("cycleway", "cycleway:both", "cycleway:right", "cycleway:left"):
        r = LUA_CYCLEWAY_RANK.get(tags.get(key), 0)
        best = max(best, r)
    return LUA_CYCLEWAY_NAME.get(best)


def graph_cycleway(tags):
    """Strongest provision the graph gives either side (SideProvision for right and left).

    Returns the same names the tile uses, so the two can be compared directly.
    `opposite_*` values are none on the graph side, which is the disagreement
    the check exists to show.
    """
    best = None
    for side in ("right", "left"):
        value = None
        for key in (f"cycleway:{side}", "cycleway:both", "cycleway"):
            if key in tags:
                value = tags[key]
                break
        prov = GRAPH_PROVISION.get(value)
        if prov and (best is None or GRAPH_RANK[prov] > GRAPH_RANK[best]):
            best = prov
    return best


def lua_route(networks):
    """BikeRouteOnWay: strongest network name among the membership, None if none."""
    best = 0
    for network in networks:
        rank = 3 if network in ("ncn", "icn") else 2 if network == "rcn" else 1
        best = max(best, rank)
    return {v: k for k, v in ROUTE_RANK.items()}.get(best)


def graph_route(networks):
    """BikeRouteFromNetwork per membership (a missing network is local); strongest wins."""
    names = {1: "local", 2: "regional", 3: "national"}
    best = 0
    for network in networks:
        if network in ("ncn", "icn"):
            rank = 3
        elif network == "rcn":
            rank = 2
        else:
            rank = 1
        best = max(best, rank)
    return names.get(best)


def read_extract(pbf, workdir):
    """Convert the extract to OSM XML and return ways and route-relation memberships."""
    xml_path = os.path.join(workdir, "extract.osm")
    subprocess.run(["osmium", "cat", pbf, "-o", xml_path, "--overwrite"], check=True)
    ways = {}
    route_networks = collections.defaultdict(list)
    route_names = {}
    for _, el in ET.iterparse(xml_path, events=("end",)):
        if el.tag == "way":
            ways[el.get("id")] = {t.get("k"): t.get("v") for t in el.findall("tag")}
            el.clear()
        elif el.tag == "relation":
            tags = {t.get("k"): t.get("v") for t in el.findall("tag")}
            if tags.get("type") == "route" and tags.get("route") == "bicycle":
                network = tags.get("network")
                for m in el.findall("member"):
                    if m.get("type") == "way":
                        route_networks[m.get("ref")].append(network)
            el.clear()
    return ways, route_networks


def keys_read(value, out):
    """Adds to `out` the attribute keys a filter or paint value reads."""
    if isinstance(value, dict):
        for v in value.values():
            keys_read(v, out)
    elif isinstance(value, list):
        if len(value) >= 2 and isinstance(value[0], str) and isinstance(value[1], str):
            if value[0] in ("get", "has", "!has") or value[0] in LEGACY_KEY_OPS:
                if not value[1].startswith("$"):
                    out.add(value[1])
        for v in value:
            keys_read(v, out)


def lua_literal(v):
    """A Python str or dict of strs as a Lua literal."""
    if isinstance(v, dict):
        return "{" + ", ".join("[%s]=%s" % (lua_literal(k), lua_literal(x)) for k, x in v.items()) + "}"
    return '"' + str(v).replace("\\", "\\\\").replace('"', '\\"') + '"'


def check_legend(style_path):
    """Runs each legend entry's OSM tags through the profile; returns the mismatch count."""
    with open(style_path) as f:
        style = json.load(f)
    legend = style.get("metadata", {}).get("peregrine:legend")
    if legend is None:
        print(f"{style_path}: no peregrine:legend")
        return 1
    read = collections.defaultdict(set)
    for layer in style["layers"]:
        if "source-layer" in layer:
            for part in ("filter", "paint", "layout"):
                keys_read(layer.get(part), read[layer["source-layer"]])

    entries = [e for g in legend["groups"] for e in g["entries"]]
    cases = []
    for e in entries:
        tags = dict(e.get("osm", {}))
        network = tags.pop("peregrine:route_network", None)
        rels = [{"type": "route", "route": "bicycle", "network": ROUTE_RELATION[network]}] if network else []
        cases.append("{id=%s, tags=%s, relations={%s}}" % (
            lua_literal(e["id"]), lua_literal(tags), ", ".join(lua_literal(r) for r in rels)))

    with tempfile.TemporaryDirectory() as workdir:
        cases_path = os.path.join(workdir, "cases.lua")
        with open(cases_path, "w") as f:
            f.write("return {\n  " + ",\n  ".join(cases) + "\n}\n")
        run = subprocess.run(["lua", HARNESS, PROFILE, cases_path],
                             capture_output=True, text=True)
    if run.returncode != 0:
        print(run.stderr)
        return 1

    written = collections.defaultdict(lambda: collections.defaultdict(dict))
    for line in run.stdout.splitlines():
        eid, layer, key, value = line.split("\t")
        written[eid][layer]
        if key:
            written[eid][layer][key] = value

    problems = 0
    for e in entries:
        if not e.get("osm"):
            continue
        for sp in e["specimen"]:
            layer, want = sp["layer"], {k: str(v) for k, v in sp.get("tags", {}).items()}
            if layer not in written[e["id"]]:
                print(f"{e['id']}: the profile writes nothing to {layer} for {e['osm']}")
                problems += 1
                continue
            got = written[e["id"]][layer]
            for k, v in want.items():
                if got.get(k) != v:
                    print(f"{e['id']}: {layer}.{k} is {got.get(k)!r} from the profile, {v!r} in the specimen")
                    problems += 1
            for k in sorted(read[layer] & set(got) - set(want)):
                print(f"{e['id']}: the profile writes {layer}.{k}={got[k]!r}, which the style reads, and the specimen omits it")
                problems += 1
    print(f"{len(entries)} legend entries checked, {problems} mismatches")
    return 1 if problems else 0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("pbf", nargs="?", help="OSM extract (.osm.pbf)")
    ap.add_argument("--legend", metavar="STYLE", help="check a style's legend against the profile")
    ap.add_argument("--show", type=int, default=20, help="ways to print per category")
    args = ap.parse_args(argv)
    if args.legend:
        return check_legend(args.legend)
    if not args.pbf:
        ap.error("an OSM extract or --legend is required")

    with tempfile.TemporaryDirectory() as workdir:
        ways, route_networks = read_extract(args.pbf, workdir)

    counts = collections.Counter()
    examples = collections.defaultdict(list)

    for wid, tags in ways.items():
        if "highway" not in tags:
            continue
        counts["highway ways"] += 1

        lc, gc = lua_cycleway(tags), graph_cycleway(tags)
        lc = LUA_TO_PROVISION.get(lc)
        if lc != gc:
            key = f"cycleway: tile {lc} / graph {gc}"
            counts[key] += 1
            examples[key].append(wid)

        lk, gk = lua_posted_kph(tags.get("maxspeed")), graph_posted_kph(tags.get("maxspeed"))
        gk = gk or None
        if lk != gk:
            key = f"maxspeed: tile {lk} / graph {gk}"
            counts[key] += 1
            examples[key].append(wid)

        ll, gl = lua_lanes(tags.get("lanes")), graph_lanes(tags.get("lanes"))
        if (ll or 0) != gl:
            key = f"lanes: tile {ll} / graph {gl}"
            counts[key] += 1
            examples[key].append(wid)

        lr = lua_route(route_networks.get(wid, []))
        gr = graph_route(route_networks.get(wid, []))
        if lr != gr:
            key = f"bike route: tile {lr} / graph {gr}"
            counts[key] += 1
            examples[key].append(wid)

    print(f"{counts['highway ways']} highway ways read")
    disagreements = sum(v for k, v in counts.items() if k != "highway ways")
    print(f"{disagreements} disagreements in total")
    for key in sorted(k for k in counts if k != "highway ways"):
        print(f"  {counts[key]:6d}  {key}")
        for wid in examples[key][: args.show]:
            print(f"            way {wid}  {ways[wid].get('highway')}  {ways[wid]}"[:220])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
