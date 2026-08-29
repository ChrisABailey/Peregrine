#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.
"""The oracle MvtRealData's pins come from, kept so a re-cut can be re-pinned.

Walks the MVT protobuf wire format by hand and deliberately shares no code with
port/Osm/fv_mvt.cpp: the point is a SECOND OPINION on layer names, per-layer
feature counts, version/extent and the vertex count of the one pinned tile.
Needs no third-party module (mapbox_vector_tile is not installed anywhere this
has run) — just sqlite3, gzip and a varint reader.

    python3 port/Osm/test/mvt_oracle.py TestData/OSM/mbtiles/us-south.mbtiles

ONE CONVENTION TO KNOW before comparing its output with the decoder's: this
counts the parameter pairs the command integers carry, so a ClosePath
contributes NOTHING, while fv::MvtTile closes each ring by re-emitting its
first point. The decoder is therefore longer by exactly one vertex per
ClosePath, which is the reconciliation MvtRealData.EveryVertexLandsNearTheTile
ItCameFrom writes down. Do not "fix" either side to make the two equal.
"""
import gzip
import sqlite3
import sys

MB = sys.argv[1]
Z, X, Y = 14, 4351, 6558


def varint(b, i):
    r = s = 0
    while True:
        c = b[i]
        i += 1
        r |= (c & 0x7F) << s
        if not c & 0x80:
            return r, i
        s += 7


def fields(b):
    """Yield (field_number, wire_type, payload_or_value)."""
    i = 0
    while i < len(b):
        key, i = varint(b, i)
        fn, wt = key >> 3, key & 7
        if wt == 0:
            v, i = varint(b, i)
            yield fn, wt, v
        elif wt == 2:
            n, i = varint(b, i)
            yield fn, wt, b[i:i + n]
            i += n
        elif wt == 5:
            yield fn, wt, b[i:i + 4]
            i += 4
        elif wt == 1:
            yield fn, wt, b[i:i + 8]
            i += 8
        else:
            raise ValueError("wire type %d" % wt)


def zigzag(u):
    return (u >> 1) ^ -(u & 1)


def count_vertices(geom):
    """Walk the command integers. Returns (parameter pairs, ClosePath count):
    MoveTo and LineTo each carry one pair per vertex, ClosePath carries none.
    The decoder's own total is pairs + closes — see this file's docstring."""
    i, n, closes = 0, 0, 0
    while i < len(geom):
        cmd = geom[i]
        i += 1
        cid, count = cmd & 7, cmd >> 3
        if cid in (1, 2):          # MoveTo, LineTo
            n += count
            i += 2 * count
        elif cid == 7:             # ClosePath
            closes += count
        else:
            raise ValueError("command %d" % cid)
    return n, closes


con = sqlite3.connect("file:%s?mode=ro" % MB, uri=True)
tms = (1 << Z) - 1 - Y
blob = con.execute(
    "SELECT tile_data FROM tiles WHERE zoom_level=? AND tile_column=? "
    "AND tile_row=?", (Z, X, tms)).fetchone()[0]
if blob[:2] == b"\x1f\x8b":
    blob = gzip.decompress(blob)

total_features = total_pairs = total_closes = 0
rows = []
for fn, wt, payload in fields(blob):
    if fn != 3:
        continue
    name = None
    nfeat = 0
    version = extent = None
    pairs = closes = 0
    for lfn, lwt, lp in fields(payload):
        if lfn == 1:
            name = lp.decode()
        elif lfn == 2:
            nfeat += 1
            for ffn, fwt, fp in fields(lp):
                if ffn == 4:
                    # geometry: packed uint32 command/parameter stream
                    g, gi = [], 0
                    while gi < len(fp):
                        v, gi = varint(fp, gi)
                        g.append(v)
                    p, c = count_vertices(g)
                    pairs += p
                    closes += c
        elif lfn == 5:
            extent = lp
        elif lfn == 15:
            version = lp
    rows.append((name, nfeat, version, extent, pairs, closes))
    total_features += nfeat
    total_pairs += pairs
    total_closes += closes

print("tile %d/%d/%d" % (Z, X, Y))
print("layers:   %d" % len(rows))
print("features: %d" % total_features)
print("vertices: %d parameter pairs + %d ClosePath = %d as fv::MvtTile counts"
      % (total_pairs, total_closes, total_pairs + total_closes))
print()
for name, nfeat, version, extent, pairs, closes in rows:
    print("  {%-22s %5d}, // v%s extent %s, %d+%d vertices"
          % ('"%s",' % name, nfeat, version, extent, pairs, closes))
