# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""A Peregrine overlay written in Python against pyfvw.

Concentric range rings about a centre the user places with a click: the
rangerings-c example, call for call, so the two draw the same pixels. The
rings are a file overlay (``.prng``, plain text), offer four properties and
hit-test their centre and each ring. The package also contributes one tool,
which opens the sample file shipped in the package.
"""

import copy
import math
import os

import pyfvw

MAX_RINGS = 10
METRES_PER_NM = 1852.0
EARTH_RADIUS_M = 6371008.8
WHITE = (255, 255, 255, 255)

GeoPoint = pyfvw.geo.GeoPoint
draw = pyfvw.draw


def distance_m(a, b):
    """Great-circle distance in metres."""
    rad = math.pi / 180.0
    dlat = (b.lat - a.lat) * rad
    dlon = (b.lon - a.lon) * rad
    h = (math.sin(dlat / 2) ** 2 +
         math.cos(a.lat * rad) * math.cos(b.lat * rad) * math.sin(dlon / 2) ** 2)
    return 2 * EARTH_RADIUS_M * math.asin(math.sqrt(min(h, 1.0)))


def north_of(p, metres):
    """The point ``metres`` due north of ``p``."""
    return GeoPoint(p.lat + metres / EARTH_RADIUS_M * 180.0 / math.pi, p.lon)


class Rings(pyfvw.overlay.Overlay):
    """Range rings; the host names each instance after the manifest entry."""

    def __init__(self):
        super().__init__("Range Rings")
        self.center = GeoPoint(32.7765, -79.9311)  # Charleston, until a click or a file
        self.count = 3
        self.spacing_nm = 1.0
        self.color = (255, 0, 0, 255)
        self.labels = True
        self._proj = None

    # MARK: Drawing

    def on_draw(self, proj, canvas):
        self._proj = copy.copy(proj)  # the mouse handler works in the last frame
        symbols = pyfvw.symbol.BuiltinSymbolLibrary()
        symbols.set_color(self.color)
        d = draw.GeoDraw(proj, canvas, symbols)
        line = draw.preset_line("solid", self.color, 2).add_casing(WHITE, 1)
        d.symbol(self.center, "fv.crosshair", scale=1.5)
        for i in range(1, self.count + 1):
            radius = i * self.spacing_nm * METRES_PER_NM
            d.circle(self.center, radius, line)
            if self.labels:
                try:
                    d.label(north_of(self.center, radius), "%g nm" % (i * self.spacing_nm),
                            self.color, size=12, dy=-2, halo_color=WHITE, halo_width=1,
                            halign=draw.HAlign.CENTER, valign=draw.VAlign.BOTTOM)
                except pyfvw.FvError:
                    pass  # no label font on this canvas; the rings still draw

    # MARK: Document

    def file_new(self):
        pass

    def file_open(self, spec):
        center, count, spacing = self.center, self.count, self.spacing_nm
        with open(spec) as f:
            for line in f:
                words = line.split()
                if len(words) == 3 and words[0] == "center":
                    center = GeoPoint(float(words[1]), float(words[2]))
                elif len(words) == 2 and words[0] == "rings" and 1 <= int(words[1]) <= MAX_RINGS:
                    count = int(words[1])
                elif len(words) == 2 and words[0] == "spacing_nm" and float(words[1]) > 0:
                    spacing = float(words[1])
        self.center, self.count, self.spacing_nm = center, count, spacing

    def file_save_as(self, spec, format_index):
        with open(spec, "w") as f:
            f.write("center %.8f %.8f\nrings %d\nspacing_nm %g\n" %
                    (self.center.lat, self.center.lon, self.count, self.spacing_nm))

    # MARK: Picking

    def hit_test_point(self, proj, x, y, tolerance_px):
        """Feature 0 is the centre; feature n is ring n."""
        cx, cy = proj.geo_to_surface(self.center)
        if math.hypot(cx - x, cy - y) <= tolerance_px:
            return [pyfvw.app.HitItem(feature=0, distance_px=math.hypot(cx - x, cy - y),
                                      hint=pyfvw.app.HintText("Range rings centre"))]
        at = proj.surface_to_geo(x, y)
        m_per_px = distance_m(at, proj.surface_to_geo(x + 1, y))
        metres = distance_m(self.center, at)
        for i in range(1, self.count + 1):
            off_px = abs(metres - i * self.spacing_nm * METRES_PER_NM) / m_per_px
            if off_px <= tolerance_px:
                tip = "Ring %d: %g nm" % (i, i * self.spacing_nm)
                return [pyfvw.app.HitItem(feature=i, distance_px=off_px,
                                          hint=pyfvw.app.HintText(tip))]
        return []

    # MARK: Properties

    def properties_schema(self):
        return [
            {"key": "ring_count", "label": "Rings", "group": "Rings", "type": "int",
             "default": 3, "min": 1, "max": MAX_RINGS},
            {"key": "spacing_nm", "label": "Spacing (nm)", "group": "Rings",
             "type": "double", "default": 1.0, "min": 0.1, "max": 500},
            {"key": "line_color", "label": "Colour", "group": "Style", "type": "color",
             "default": (255, 0, 0, 255)},
            {"key": "labels", "label": "Label each ring", "group": "Style",
             "type": "bool", "default": True},
        ]

    def read_property(self, key):
        return {"ring_count": self.count, "spacing_nm": self.spacing_nm,
                "line_color": self.color, "labels": self.labels}[key]

    def write_property(self, key, value):
        if key == "ring_count":
            self.count = value
        elif key == "spacing_nm":
            self.spacing_nm = value
        elif key == "line_color":
            self.color = tuple(value)
        elif key == "labels":
            self.labels = value

    # MARK: Mouse

    def on_mouse_down(self, e):
        """A left click moves the centre to the clicked point."""
        if self._proj is None or e.button != 0:
            return False
        self.center = self._proj.surface_to_geo(e.x, e.y)
        self.dirty = True
        return True


def open_sample(ctx):
    """Opens the sample rings shipped in the package's resources."""
    ctx.open_file(os.path.join(ctx.package_dir, "resources", "charleston.prng"))
