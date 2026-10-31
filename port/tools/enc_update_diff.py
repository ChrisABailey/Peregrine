#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""Compare fv_enc's S-57 update application with GDAL's (CH-D7).

For every base cell under the given roots that has update files, reads the
cell with GDAL's S-57 driver (`ogrinfo -json -features`, UPDATES=APPLY) and
with `fv_enc_dump`, and compares feature by feature: the FOID set, OBJL,
attributes and the set of vertices. The base editions are compared the same
way (GDAL UPDATES=IGNORE, `fv_enc_dump --base-only`), so a difference in how
the two readers present a feature is told apart from a difference in what an
update did. Development only: needs GDAL (`brew install gdal`); never part of
the build or the tests.

Usage (from the repo root, after building fv_enc_dump):

    python3 port/tools/enc_update_diff.py \
        --dump build/port/Enc/fv_enc_dump --catalog testdata/enc \
        testdata/enc-regions/FL testdata/enc-regions/NY

Prints one line per cell and a summary; `--report FILE` writes every
difference. Exit status 1 when any updated cell differs where its base
edition agreed. A cancelled cell (EDTN 0) is counted, not compared: GDAL
keeps its features.
"""

import argparse
import csv
import json
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

GDAL_OPTIONS = ["SPLIT_MULTIPOINT=OFF", "ADD_SOUNDG_DEPTH=OFF", "RETURN_PRIMITIVES=OFF",
                "RETURN_LINKAGES=OFF", "LNAM_REFS=OFF", "RECODE_BY_DSSI=ON"]
# GDAL fields that are record bookkeeping rather than S-57 attributes.
GDAL_FIXED = {"RCID", "PRIM", "GRUP", "OBJL", "RVER", "AGEN", "FIDN", "FIDS", "LNAM",
              "LNAM_REFS", "FFPT_RIND", "DSNM"}
COORD_DIGITS = 7


def load_attribute_codes(catalog_dir):
    """Acronym -> ATTL code from s57attributes.csv."""
    codes = {}
    with open(os.path.join(catalog_dir, "s57attributes.csv"), newline="", encoding="latin-1") as f:
        for row in csv.DictReader(f):
            try:
                codes[row["Acronym"].strip()] = int(row["Code"])
            except (KeyError, ValueError):
                pass
    return codes


def base_cells_with_updates(roots):
    """Every `*.000` under `roots` that has a `.001` beside it, sorted."""
    out = []
    for root in roots:
        for d, _, files in os.walk(root):
            for name in files:
                if name.endswith(".000") and os.path.exists(os.path.join(d, name[:-4] + ".001")):
                    out.append(os.path.join(d, name))
    return sorted(out)


def norm_value(v):
    """An attribute value as a tuple of comparable tokens."""
    if v is None:
        return None
    items = v if isinstance(v, list) else str(v).split(",")
    out = []
    for item in items:
        s = str(item).strip()
        if s == "":
            continue  # GDAL drops the empty items of a list ("4," reads as [4])
        try:
            out.append(round(float(s), 6))
        except ValueError:
            out.append(s)
    if not out:
        return None
    return tuple(out)


def coords_of(geometry):
    """Every (lat, lon) in a GeoJSON geometry, rounded, as a set."""
    pts = set()

    def walk(c):
        if not c:
            return
        if isinstance(c[0], (int, float)):
            pts.add((round(c[1], COORD_DIGITS), round(c[0], COORD_DIGITS)))
        else:
            for x in c:
                walk(x)

    if geometry:
        if geometry.get("type") == "GeometryCollection":
            for g in geometry.get("geometries", []):
                pts |= coords_of(g)
        else:
            walk(geometry.get("coordinates"))
    return frozenset(pts)


def read_gdal(cell, apply_updates, codes):
    """FOID -> (OBJL, attributes, vertices) as GDAL reads the cell."""
    cmd = ["ogrinfo", "-ro", "-json", "-features", "-q"]
    for opt in GDAL_OPTIONS + ["UPDATES=" + ("APPLY" if apply_updates else "IGNORE")]:
        cmd += ["-oo", opt]
    cmd.append(cell)
    res = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    if res.returncode != 0:
        raise RuntimeError("ogrinfo failed on %s: %s" % (cell, res.stderr.strip()[:300]))
    data = json.loads(res.stdout)
    feats = {}
    for layer in data.get("layers", []):
        for f in layer.get("features", []):
            p = f.get("properties", {})
            if "OBJL" not in p or p.get("FIDN") is None:
                continue
            # GDAL types FIDN as a signed 32-bit integer; S-57 stores b14 unsigned.
            foid = "%s:%s:%s" % (p["AGEN"], int(p["FIDN"]) & 0xFFFFFFFF, p["FIDS"])
            attrs = {}
            for k, v in p.items():
                if k in GDAL_FIXED or k not in codes:
                    continue
                nv = norm_value(v)
                if nv is not None:
                    attrs[codes[k]] = nv
            feats[foid] = (int(p["OBJL"]), attrs, coords_of(f.get("geometry")))
    return feats


def read_ours(dump, cell, apply_updates):
    """FOID -> (OBJL, attributes, vertices) as fv_enc reads the cell."""
    cmd = [dump] + ([] if apply_updates else ["--base-only"]) + [cell]
    # ATVL at lexical level 1 is ISO 8859-1, printed as stored; GDAL recodes it.
    res = subprocess.run(cmd, capture_output=True, text=True, encoding="latin-1")
    if res.returncode != 0:
        raise RuntimeError("fv_enc_dump failed on %s: %s" % (cell, res.stderr.strip()[:300]))
    feats = {}
    for line in res.stdout.splitlines():
        foid, objl, _prim, attr_text, vert_text = line.split("\t")
        attrs = {}
        if attr_text:
            for a in attr_text.split("\x1f"):
                code, _, value = a.partition("=")
                nv = norm_value(value)
                if nv is not None:
                    attrs[int(code)] = nv
        pts = set()
        if vert_text:
            for v in vert_text.split(";"):
                lat, lon = v.split(",")[:2]
                pts.add((round(float(lat), COORD_DIGITS), round(float(lon), COORD_DIGITS)))
        feats[foid] = (int(objl), attrs, frozenset(pts))
    return feats, res.stderr.strip()


def compare(ours, gdal):
    """Differences between two readings, as (kind, foid, detail) tuples."""
    diffs = []
    for foid in sorted(set(ours) - set(gdal)):
        diffs.append(("only_fv", foid, "OBJL %d" % ours[foid][0]))
    for foid in sorted(set(gdal) - set(ours)):
        diffs.append(("only_gdal", foid, "OBJL %d" % gdal[foid][0]))
    for foid in sorted(set(ours) & set(gdal)):
        (o_objl, o_attrs, o_pts), (g_objl, g_attrs, g_pts) = ours[foid], gdal[foid]
        if o_objl != g_objl:
            diffs.append(("objl", foid, "%d vs %d" % (o_objl, g_objl)))
        if o_attrs != g_attrs:
            keys = sorted(set(o_attrs) | set(g_attrs))
            detail = "; ".join("%d: %s vs %s" % (k, o_attrs.get(k), g_attrs.get(k))
                               for k in keys if o_attrs.get(k) != g_attrs.get(k))
            diffs.append(("attributes", foid, detail))
        if o_pts != g_pts:
            diffs.append(("vertices", foid, "%d vs %d points, %d only fv, %d only GDAL" % (
                len(o_pts), len(g_pts), len(o_pts - g_pts), len(g_pts - o_pts))))
    return diffs


def check_cell(cell, dump, codes):
    """The base and applied differences for one cell."""
    out = {"cell": cell}
    try:
        base_ours, _ = read_ours(dump, cell, False)
        base_gdal = read_gdal(cell, False, codes)
        upd_ours, warning = read_ours(dump, cell, True)
        upd_gdal = read_gdal(cell, True, codes)
    except (RuntimeError, ValueError) as e:
        out["error"] = str(e)
        return out
    out["base"] = compare(base_ours, base_gdal)
    out["applied"] = compare(upd_ours, upd_gdal)
    out["warning"] = warning
    # GDAL does not act on a cancellation (EDTN 0): it keeps the features.
    out["cancelled"] = "was cancelled by update" in warning
    out["features"] = len(upd_ours)
    # A difference already present in the base edition is a presentation
    # difference between the readers, not an update difference.
    base_keys = {(k, f) for k, f, _ in out["base"]}
    out["update_only"] = [] if out["cancelled"] else [
        d for d in out["applied"] if (d[0], d[1]) not in base_keys]
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--dump", required=True, help="path to fv_enc_dump")
    ap.add_argument("--catalog", required=True, help="directory holding s57attributes.csv")
    ap.add_argument("--report", help="write every difference to this file")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("roots", nargs="+")
    args = ap.parse_args()

    codes = load_attribute_codes(args.catalog)
    cells = base_cells_with_updates(args.roots)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(lambda c: check_cell(c, args.dump, codes), cells))

    report = open(args.report, "w") if args.report else None
    bad = errors = cancelled = 0
    base_total = applied_total = 0
    for r in results:
        name = os.path.basename(r["cell"])
        if "error" in r:
            errors += 1
            print("%s\tERROR\t%s" % (name, r["error"]))
            continue
        cancelled += r["cancelled"]
        base_total += len(r["base"])
        applied_total += len(r["applied"])
        if r["update_only"]:
            bad += 1
        print("%s\tfeatures=%d\tbase_diffs=%d\tapplied_diffs=%d\tupdate_only=%d%s" % (
            name, r["features"], len(r["base"]), len(r["applied"]), len(r["update_only"]),
            "\t" + r["warning"] if r["warning"] else ""))
        if report:
            for phase in ("base", "applied"):
                for kind, foid, detail in r[phase]:
                    tag = phase
                    if phase == "applied" and (kind, foid, detail) in r["update_only"]:
                        tag = "UPDATE"
                    report.write("%s\t%s\t%s\t%s\t%s\n" % (name, tag, kind, foid, detail))
    if report:
        report.close()
    print("cells=%d errors=%d cancelled=%d cells_with_update_only_diffs=%d base_diffs=%d "
          "applied_diffs=%d" % (len(results), errors, cancelled, bad, base_total, applied_total))
    return 1 if bad or errors else 0


if __name__ == "__main__":
    sys.exit(main())
