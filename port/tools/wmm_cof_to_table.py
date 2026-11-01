#!/usr/bin/env python3
"""Rewrites the compiled-in World Magnetic Model table in geo_tool/geomag.cpp from a WMM.COF.

NOAA/NGA publish a new WMM every five years (WMM.COF in the WMM<year>COF.zip at
https://www.ncei.noaa.gov/products/world-magnetic-model/wmm-coefficients).
Replaces the 90 coefficient rows, the epoch and the model name in
load_mem_mag_data(). Run from the repository root:

    python3 port/tools/wmm_cof_to_table.py path/to/WMM.COF
"""

import re
import sys
from pathlib import Path

GEOMAG = Path("fvw_core/geo_tool/geomag.cpp")


def read_cof(path):
    """Returns (epoch, model, rows) from a WMM.COF; rows are (n, m, g, h, dg, dh)."""
    lines = Path(path).read_text().splitlines()
    header = lines[0].split()
    epoch, model = float(header[0]), header[1]
    rows = []
    for line in lines[1:]:
        fields = line.split()
        if len(fields) != 6:
            break
        n, m = int(fields[0]), int(fields[1])
        rows.append((n, m, *map(float, fields[2:])))
    if len(rows) != 90:
        sys.exit(f"{path}: expected 90 coefficient rows, found {len(rows)}")
    return epoch, model, rows


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    epoch, model, rows = read_cof(sys.argv[1])
    body = ",\n".join(
        f"      {{ {n:2d}, {m:2d}, {g:9.1f}, {h:9.1f}, {dg:10.1f}, {dh:10.1f}}}"
        for n, m, g, h, dg, dh in rows)

    # geomag.cpp has CRLF line endings; match on LF and write back what was read.
    with open(GEOMAG, newline="") as f:
        raw = f.read()
    eol = "\r\n" if "\r\n" in raw else "\n"
    src = raw.replace(eol, "\n")
    src, count = re.subn(
        r"(mag_param_t mag2\[MAX_ORD\] =\n   \{\n).*?(\n   \};)",
        lambda mo: mo.group(1) + body + mo.group(2), src, count=1, flags=re.S)
    if count != 1:
        sys.exit("coefficient table not found in load_mem_mag_data")
    src, count = re.subn(
        r"(\*epoch = )[0-9.]+;(\n   strcpy_s\(model, model_len, \")[^\"]*(\"\);)",
        lambda mo: f"{mo.group(1)}{epoch:.1f};{mo.group(2)}{model}{mo.group(3)}",
        src, count=1)
    if count != 1:
        sys.exit("epoch/model assignment not found in load_mem_mag_data")
    GEOMAG.write_text(src.replace("\n", eol), newline="")
    print(f"{GEOMAG}: {model}, epoch {epoch:.1f}")


if __name__ == "__main__":
    main()
