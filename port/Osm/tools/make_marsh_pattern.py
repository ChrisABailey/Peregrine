#!/usr/bin/env python3
"""Add the S-52 MARSHES1 marsh tuft to the OSM sprite sheet as a fill-pattern.

Provenance
----------
The artwork is the IHO S-52 Presentation Library pattern `MARSHES1`
("pattern of symbols for a marsh", RCID 3143), taken verbatim from the HPGL
in `testdata/enc/chartsymbols.xml`:

    SPA;SW2;PU751,765;PD751,499;      the centre blade
    SPA;SW2;PU626,892;PD876,892;      the lower waterline
    SPA;SW2;PU550,810;PD950,810;      the upper waterline
    SPA;SW2;PU664,799;PD592,634;      the left blade
    SPA;SW2;PU830,799;PD901,637;      the right blade

with <origin x="550" y="499"/> subtracted and the 400 x 393 unit box scaled
to a 14 px glyph.  The colour is S-52's own: pen A resolves through
<color-ref>ACHBRN</color-ref> to CHBRN, which is rgb(177,145,57) in the
DAY_BRIGHT table.

Why a 32 px tile holding TWO tufts
----------------------------------
S-52 gives MARSHES1 `<filltype>S</filltype>` — a STAGGERED fill, every other
row offset by half the spacing.  `AreaPatternStyle` has a `staggered` flag for
exactly that, but the OSM path deliberately never sets it (GL tiles a fill
pattern as a seamless texture on a plain grid, and osm_style_test pins that).
So the stagger is baked into the artwork instead: one tuft at the tile's
top-left, a second at its centre, which lays a diagonal lattice when the tile
repeats on a plain grid.  No engine change, and the marsh reads like a chart
rather than like graph paper.

Idempotent: re-running replaces the tile and its index entry in place.
"""

import json
import os
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
SHEET = os.path.normpath(os.path.join(HERE, "..", "styles", "symbols",
                                      "osm-liberty-topo"))

NAME = "marsh_pattern"
TILE = 32           # nominal px; the fill-pattern spacing IS the tile's size
GLYPH = 14.0        # the S-52 400-unit box, in nominal px
SS = 8              # supersampling factor; BOX-averaged back down, no ringing
CHBRN = (177, 145, 57)

# MARSHES1's HPGL, origin-relative, in the pattern's own 0.01 mm units.
K = GLYPH / 400.0
STEM   = (201, 0,   201, 266)   # PU751,765 PD751,499
BAR_HI = (0,   311, 400, 311)   # PU550,810 PD950,810
BAR_LO = (76,  393, 326, 393)   # PU626,892 PD876,892
DIAG_L = (114, 300, 42,  135)   # PU664,799 PD592,634
DIAG_R = (280, 300, 351, 138)   # PU830,799 PD901,637


def snap(v):
    """Put an axis-aligned stroke on a pixel CENTRE.

    At SS=8 a 1 px stroke centred on n+0.5 covers exactly one destination
    pixel, so the blade and the waterlines stay crisp instead of smearing
    across two rows at half alpha — which at a 14 px glyph is the difference
    between a marsh symbol and a smudge.
    """
    return round(v - 0.5) + 0.5


def tuft(draw, ox, oy):
    """One marsh symbol with its 400x393 box's top-left at (ox, oy)."""
    def p(x, y):
        return (ox + x * K, oy + y * K)

    w = SS  # 1 nominal px

    def line(a, b):
        draw.line([a[0] * SS, a[1] * SS, b[0] * SS, b[1] * SS],
                  fill=255, width=w)

    x0, y0, x1, y1 = STEM
    sx = snap(ox + x0 * K)
    line((sx, oy + y0 * K), (sx, oy + y1 * K))
    for bar in (BAR_HI, BAR_LO):
        x0, y0, x1, y1 = bar
        sy = snap(oy + y0 * K)
        line((ox + x0 * K, sy), (ox + x1 * K, sy))
    for d in (DIAG_L, DIAG_R):
        line(p(d[0], d[1]), p(d[2], d[3]))


def render_tile():
    # Drawn as a COVERAGE MASK over a flat colour, never as RGBA: downsampling
    # straight-alpha RGBA averages the undefined colour of transparent pixels
    # into every edge and fringes the strokes.
    mask = Image.new("L", (TILE * SS, TILE * SS), 0)
    d = ImageDraw.Draw(mask)
    tuft(d, 1.0, 1.0)                  # top-left
    tuft(d, 1.0 + TILE / 2, 1.0 + TILE / 2)   # and the half-step stagger
    mask = mask.resize((TILE, TILE), Image.BOX)
    tile = Image.new("RGBA", (TILE, TILE), CHBRN + (0,))
    tile.putalpha(mask)
    return tile


def main():
    sheet = Image.open(SHEET + ".png").convert("RGBA")
    index = json.load(open(SHEET + ".json"))
    tile = render_tile()

    if NAME in index:                  # re-run: overwrite where it already is
        e = index[NAME]
        assert e["width"] == TILE and e["height"] == TILE, "tile size changed"
        x, y = e["x"], e["y"]
    else:                              # first run: a new strip under the sheet
        x, y = 0, sheet.height
        sheet = sheet.crop((0, 0, max(sheet.width, TILE), sheet.height + TILE))
        index[NAME] = {"x": x, "y": y, "width": TILE, "height": TILE,
                       "pixelRatio": 1}

    sheet.paste(tile, (x, y))
    sheet.save(SHEET + ".png")
    # indent=2 + sort_keys is byte-for-byte what the sheet already ships, and
    # no trailing newline, so the diff is the one new entry and nothing else.
    with open(SHEET + ".json", "w") as f:
        f.write(json.dumps(index, indent=2, sort_keys=True))
    print("%s at (%d,%d) %dx%d; sheet now %dx%d"
          % (NAME, x, y, TILE, TILE, sheet.width, sheet.height))


if __name__ == "__main__":
    main()
