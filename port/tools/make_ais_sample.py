#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""Write a synthetic AIS recording of Charleston harbour for the fv.ais tests.

Ten minutes of made-up traffic, encoded as NMEA 0183 `!AIVDM` sentences with a
TAG block (`s:`, `c:`) on each, to port/fvkit/ais/test/data/charleston-synthetic.nmea:
class A ships (types 1 and 5), class B craft (18 and 24), an aid to navigation
(21), a base station (4), a SART, one ship that falls silent half way, and the
receiver's own transponder as `!AIVDO`. Live captures are not committed because
their sources' terms vary; this file has none.

    python3 port/tools/make_ais_sample.py
"""

import calendar
import math
import os

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
OUT = os.path.join(ROOT, "port", "fvkit", "ais", "test", "data", "charleston-synthetic.nmea")
T0 = calendar.timegm((2026, 10, 26, 12, 0, 0))
DURATION_S = 600
KNOT = 1852.0 / 3600.0
M_PER_DEG = 6371008.8 * math.pi / 180.0
STATION = "SYNTH01"


class Bits:
    def __init__(self):
        self.bits = []

    def u(self, value, width):
        value &= (1 << width) - 1
        self.bits += [(value >> (width - 1 - i)) & 1 for i in range(width)]
        return self

    def s(self, value, width):
        return self.u(value + (1 << width) if value < 0 else value, width)

    def text(self, value, chars):
        value = value.upper().ljust(chars, "@")[:chars]
        for c in value:
            o = ord(c)
            self.u(o - 64 if o >= 64 else o, 6)
        return self


def armour(bits):
    fill = (-len(bits)) % 6
    bits = bits + [0] * fill
    out = []
    for i in range(0, len(bits), 6):
        v = int("".join(map(str, bits[i:i + 6])), 2)
        out.append(chr(v + 48 if v < 40 else v + 56))
    return "".join(out), fill


def checksum(body):
    c = 0
    for ch in body:
        c ^= ord(ch)
    return "%02X" % c


def sentences(bits, t, seq, channel="A", own=False):
    payload, fill = armour(bits.bits)
    parts = [payload[i:i + 60] for i in range(0, len(payload), 60)]
    tag = "s:%s,c:%d" % (STATION, t)
    lines = []
    for n, part in enumerate(parts, 1):
        body = "AIVD%s,%d,%d,%s,%s,%s,%d" % ("O" if own else "M", len(parts), n,
                                            seq if len(parts) > 1 else "", channel, part,
                                            fill if n == len(parts) else 0)
        line = "!%s*%s" % (body, checksum(body))
        lines.append(("\\%s*%s\\" % (tag, checksum(tag)) if n == 1 else "") + line)
    return lines


def lonlat(b, lat, lon):
    return b.s(round(lon * 600000), 28).s(round(lat * 600000), 27)


def type1(mmsi, lat, lon, sog_kn, cog, hdg, status, t):
    b = Bits().u(1, 6).u(0, 2).u(mmsi, 30).u(status, 4).s(0, 8).u(round(sog_kn * 10), 10).u(1, 1)
    lonlat(b, lat, lon).u(round(cog * 10), 12).u(hdg, 9).u(t % 60, 6).u(0, 2).u(0, 3).u(0, 1)
    return b.u(0, 19)


def type18(mmsi, lat, lon, sog_kn, cog, hdg, t):
    b = Bits().u(18, 6).u(0, 2).u(mmsi, 30).u(0, 8).u(round(sog_kn * 10), 10).u(0, 1)
    lonlat(b, lat, lon).u(round(cog * 10), 12).u(hdg, 9).u(t % 60, 6).u(0, 2)
    return b.u(1, 1).u(1, 1).u(1, 1).u(1, 1).u(1, 1).u(0, 1).u(0, 1).u(0, 20)


def type5(mmsi, imo, call, name, ship_type, dims, draught_m, dest):
    b = Bits().u(5, 6).u(0, 2).u(mmsi, 30).u(0, 2).u(imo, 30).text(call, 7).text(name, 20)
    b.u(ship_type, 8).u(dims[0], 9).u(dims[1], 9).u(dims[2], 6).u(dims[3], 6).u(1, 4)
    b.u(10, 4).u(26, 5).u(18, 5).u(30, 6).u(round(draught_m * 10), 8).text(dest, 20)
    return b.u(0, 1).u(0, 1)


def type24a(mmsi, name):
    return Bits().u(24, 6).u(0, 2).u(mmsi, 30).u(0, 2).text(name, 20)


def type24b(mmsi, ship_type, call, dims):
    b = Bits().u(24, 6).u(0, 2).u(mmsi, 30).u(1, 2).u(ship_type, 8).u(0, 42).text(call, 7)
    return b.u(dims[0], 9).u(dims[1], 9).u(dims[2], 6).u(dims[3], 6).u(1, 4).u(0, 2)


def type21(mmsi, aid_type, name, lat, lon, t, virtual):
    b = Bits().u(21, 6).u(0, 2).u(mmsi, 30).u(aid_type, 5).text(name, 20).u(1, 1)
    lonlat(b, lat, lon).u(0, 9).u(0, 9).u(0, 6).u(0, 6).u(1, 4).u(t % 60, 6).u(0, 1).u(0, 8)
    return b.u(0, 1).u(1 if virtual else 0, 1).u(0, 1).u(0, 1)


def type4(mmsi, lat, lon, t):
    y, mo, d, h, mi, s = __import__("time").gmtime(t)[:6]
    b = Bits().u(4, 6).u(0, 2).u(mmsi, 30).u(y, 14).u(mo, 4).u(d, 5).u(h, 5).u(mi, 6).u(s, 6)
    return lonlat(b.u(1, 1), lat, lon).u(7, 4).u(0, 10).u(0, 1).u(0, 19)


class Mover:
    """A craft on a straight course from a start point."""

    def __init__(self, mmsi, lat, lon, sog_kn, cog, period_s, cls, offset_s=0, until_s=None):
        self.mmsi, self.lat, self.lon = mmsi, lat, lon
        self.sog_kn, self.cog, self.period_s, self.cls = sog_kn, cog, period_s, cls
        self.offset_s, self.until_s = offset_s, until_s

    def at(self, dt):
        d = self.sog_kn * KNOT * dt
        lat = self.lat + d * math.cos(math.radians(self.cog)) / M_PER_DEG
        lon = self.lon + d * math.sin(math.radians(self.cog)) / (
            M_PER_DEG * math.cos(math.radians(self.lat)))
        return lat, lon


def main():
    movers = [
        Mover(366000101, 32.700, -79.820, 10.0, 300.0, 10, "A"),           # cargo, inbound
        Mover(366000102, 32.770, -79.920, 6.0, 135.0, 10, "A", 3),         # tug, outbound
        Mover(366000105, 32.720, -79.870, 2.0, 45.0, 10, "A", 6, 300),     # fishing, falls silent
        Mover(338000103, 32.745, -79.900, 5.0, 200.0, 30, "B", 11),        # sailboat
        Mover(338000104, 32.735, -79.850, 18.0, 330.0, 30, "B", 17),       # pleasure craft
        Mover(970000001, 32.725, -79.885, 0.5, 70.0, 60, "A", 23),         # SART, drifting
    ]
    own = Mover(366000999, 32.760, -79.880, 4.0, 180.0, 10, "A", 1)

    events = []  # (time, order, lines)
    seq = [0]

    def emit(t, bits, own_ship=False, channel="A"):
        seq[0] = seq[0] % 9 + 1
        events.append((t, len(events), sentences(bits, t, seq[0], channel, own_ship)))

    for dt in range(0, DURATION_S):
        t = T0 + dt
        for m in movers + [own]:
            if dt < m.offset_s or (dt - m.offset_s) % m.period_s:
                continue
            if m.until_s is not None and dt > m.until_s:
                continue
            lat, lon = m.at(dt)
            hdg = round(m.cog) % 360
            if m.cls == "A":
                emit(t, type1(m.mmsi, lat, lon, m.sog_kn, m.cog, hdg, 0, t), m is own)
            else:
                emit(t, type18(m.mmsi, lat, lon, m.sog_kn, m.cog, hdg, t), channel="B")
        if dt % 120 == 5:
            emit(t, type5(366000101, 9300001, "WCRG7", "ATLANTIC STAR", 70, (150, 40, 16, 16),
                          9.5, "CHARLESTON"))
            emit(t, type5(366000102, 0, "WTUG2", "HARBOR TUG", 52, (20, 10, 5, 5), 3.2,
                          "SEA BUOY"))
            emit(t, type5(366000105, 0, "WFSH5", "MISS KATIE", 30, (12, 6, 3, 3), 1.8, ""))
            emit(t, type5(366000999, 0, "WOWN9", "OWN SHIP", 37, (10, 4, 2, 2), 1.5, ""), True)
        if dt % 180 == 8:
            emit(t, type24a(338000103, "SEA WIND"), channel="B")
            emit(t, type24b(338000103, 36, "WSW1234", (9, 3, 2, 2)), channel="B")
            emit(t, type24a(338000104, "BLUE MOON"), channel="B")
            emit(t, type24b(338000104, 37, "WBM5678", (8, 2, 2, 1)), channel="B")
        if dt % 180 == 15:
            emit(t, type21(993660001, 19, "CHARLESTON ENT LB C", 32.710, -79.835, t, False))
            emit(t, type21(993660002, 30, "FORT SUMTER VAIS", 32.752, -79.874, t, True))
        if dt % 10 == 0:
            emit(t, type4(3669999, 32.781, -79.930, t))

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", newline="\r\n") as f:
        for _, _, lines in sorted(events):
            for line in lines:
                f.write(line + "\n")
    print("%d messages -> %s" % (len(events), os.path.relpath(OUT, ROOT)))


if __name__ == "__main__":
    main()
