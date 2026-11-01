#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""Plays a recorded NMEA file as an AIS receiver would send it.

Serves the lines over TCP (any number of clients) and/or sends them as unicast
UDP, paced by the file's TAG block times (c:, seconds or milliseconds) or at a
fixed rate when there are none. TAG blocks are stripped unless --keep-tags,
since a receiver on a boat does not send them. With --ownship the file's own
transponder reports (!AIVDO types 1-3 and 18) also go out as $GPRMC and $HEHDT
stamped with the current time, for testing "Use Receiver Position".

Examples (the iOS simulator reaches the Mac at 127.0.0.1):
  port/tools/ais_replay.py --tcp 10110 --loop --ownship
  port/tools/ais_replay.py --udp 127.0.0.1:2000 --speed 4
"""

import argparse
import datetime
import os
import re
import socket
import sys
import threading
import time

DEFAULT_FILE = os.path.join(os.path.dirname(__file__), "..", "fvkit", "ais", "test", "data",
                            "charleston-synthetic.nmea")
TAG_RE = re.compile(r"^\\([^\\]*)\\(.*)$")


def checksum(body):
    """XOR of the bytes between '$'/'!' and '*', as two hex digits."""
    x = 0
    for ch in body.encode("ascii"):
        x ^= ch
    return "%02X" % x


def tag_time(tag):
    """The c: field of a TAG block in epoch seconds, or None."""
    for field in tag.split("*", 1)[0].split(","):
        if field.startswith("c:"):
            try:
                v = int(field[2:])
            except ValueError:
                return None
            return v / 1000.0 if v > 10**11 else float(v)
    return None


def payload_bits(payload, fill):
    bits = []
    for ch in payload:
        v = ord(ch) - 48
        if v > 40:
            v -= 8
        bits.extend((v >> (5 - i)) & 1 for i in range(6))
    return bits[:len(bits) - fill] if fill else bits


def uint(bits, start, n):
    v = 0
    for b in bits[start:start + n]:
        v = (v << 1) | b
    return v


def sint(bits, start, n):
    v = uint(bits, start, n)
    return v - (1 << n) if v & (1 << (n - 1)) else v


def own_position(sentence):
    """(lat, lon, sog_kn, cog, heading) from a single-part !xxVDO type 1-3/18, else None."""
    parts = sentence.split("*", 1)[0].split(",")
    if len(parts) < 7 or not parts[0].endswith("VDO") or parts[1] != "1":
        return None
    bits = payload_bits(parts[5], int(parts[6] or 0))
    kind = uint(bits, 0, 6)
    if kind in (1, 2, 3):
        sog, lon, lat, cog, hdg = (uint(bits, 50, 10), sint(bits, 61, 28), sint(bits, 89, 27),
                                   uint(bits, 116, 12), uint(bits, 128, 9))
    elif kind == 18:
        sog, lon, lat, cog, hdg = (uint(bits, 46, 10), sint(bits, 57, 28), sint(bits, 85, 27),
                                   uint(bits, 112, 12), uint(bits, 124, 9))
    else:
        return None
    lat, lon = lat / 600000.0, lon / 600000.0
    if abs(lat) > 90 or abs(lon) > 180:
        return None
    return (lat, lon, None if sog == 1023 else sog / 10.0,
            None if cog >= 3600 else cog / 10.0, None if hdg == 511 else float(hdg))


def ownship_sentences(pos):
    """$GPRMC (and $HEHDT when a heading is known) for `pos`, stamped now."""
    lat, lon, sog, cog, hdg = pos
    now = datetime.datetime.now(datetime.timezone.utc)
    la, lo = abs(lat), abs(lon)
    body = "GPRMC,%s,A,%02d%07.4f,%s,%03d%07.4f,%s,%s,%s,%s,," % (
        now.strftime("%H%M%S.00"), int(la), (la - int(la)) * 60, "N" if lat >= 0 else "S",
        int(lo), (lo - int(lo)) * 60, "E" if lon >= 0 else "W",
        "" if sog is None else "%.1f" % sog, "" if cog is None else "%.1f" % cog,
        now.strftime("%d%m%y"))
    out = ["$%s*%s" % (body, checksum(body))]
    if hdg is not None:
        h = "HEHDT,%.1f,T" % hdg
        out.append("$%s*%s" % (h, checksum(h)))
    return out


class TcpServer:
    """Accepts clients on `port` and writes every line to all of them."""

    def __init__(self, port):
        self.clients = []
        self.lock = threading.Lock()
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("0.0.0.0", port))
        self.sock.listen(8)
        threading.Thread(target=self._accept, daemon=True).start()

    def _accept(self):
        while True:
            conn, addr = self.sock.accept()
            print("tcp client %s:%d connected" % addr, file=sys.stderr)
            with self.lock:
                self.clients.append(conn)

    def send(self, data):
        with self.lock:
            for c in list(self.clients):
                try:
                    c.sendall(data)
                except OSError:
                    print("tcp client dropped", file=sys.stderr)
                    self.clients.remove(c)
                    c.close()


def read_lines(path, keep_tags):
    """[(time or None, line)], multipart continuations taking the previous time."""
    out = []
    last = None
    with open(path, encoding="ascii", errors="replace") as f:
        for raw in f:
            line = raw.strip()
            if not line:
                continue
            m = TAG_RE.match(line)
            t = None
            if m:
                t = tag_time(m.group(1))
                if not keep_tags:
                    line = m.group(2)
            out.append((t if t is not None else last, line))
            if t is not None:
                last = t
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file", nargs="?", default=DEFAULT_FILE, help="NMEA file (default: the Charleston sample)")
    ap.add_argument("--tcp", type=int, metavar="PORT", help="serve over TCP on PORT")
    ap.add_argument("--udp", metavar="HOST:PORT", help="send unicast UDP to HOST:PORT")
    ap.add_argument("--speed", type=float, default=1.0, help="time multiplier for TAG pacing")
    ap.add_argument("--rate", type=float, default=10.0, help="lines per second when there are no TAG times")
    ap.add_argument("--loop", action="store_true", help="start again at the end")
    ap.add_argument("--keep-tags", action="store_true", help="send TAG blocks as recorded")
    ap.add_argument("--ownship", action="store_true", help="also send $GPRMC/$HEHDT from the file's !AIVDO reports")
    args = ap.parse_args()
    if not args.tcp and not args.udp:
        ap.error("give --tcp PORT and/or --udp HOST:PORT")

    server = TcpServer(args.tcp) if args.tcp else None
    udp = None
    if args.udp:
        host, _, port = args.udp.rpartition(":")
        udp = (socket.socket(socket.AF_INET, socket.SOCK_DGRAM), (host or "127.0.0.1", int(port)))
    lines = read_lines(args.file, args.keep_tags)
    if not lines:
        sys.exit("no lines in %s" % args.file)
    print("%d lines from %s" % (len(lines), args.file), file=sys.stderr)

    def emit(line):
        data = (line + "\r\n").encode("ascii")
        if server:
            server.send(data)
        if udp:
            udp[0].sendto(data, udp[1])

    try:
        while True:
            start_wall = time.monotonic()
            start_file = next((t for t, _ in lines if t is not None), None)
            for i, (t, line) in enumerate(lines):
                if t is not None and start_file is not None:
                    due = start_wall + (t - start_file) / args.speed
                else:
                    due = start_wall + i / args.rate
                delay = due - time.monotonic()
                if delay > 0:
                    time.sleep(delay)
                emit(line)
                if args.ownship:
                    pos = own_position(TAG_RE.sub(r"\2", line))
                    if pos:
                        for s in ownship_sentences(pos):
                            emit(s)
            if not args.loop:
                break
            print("looping", file=sys.stderr)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
