# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See LICENSE and NOTICE.md for the full licensing picture.

"""A route overlay: the smallest thing that EDITS a map rather than drawing on
one, and therefore the test of whether the overlay SPI is really enough.

Editing:
    click       select the waypoint under the cursor
    a           arm "add a point"; the next click on the map inserts a new
                waypoint AFTER the selected one (at the end when nothing is
                selected), and selects it
    d / Delete  delete the selected waypoint
    Esc        deselect, or disarm add mode

WHY ADD IS TWO GESTURES AND NOT ONE. A click has to keep meaning "select" —
an editor where every click on open map appends a point cannot be used to pick
anything — so the key ARMS the mode and the click supplies the position. That
is FalconView's own route editor's behaviour and every drawing tool's, and it
costs the overlay one bool.
"""

import pyfvw


class RouteOverlay(pyfvw.overlay.Overlay):
    """A named route: leg lines, waypoint diamonds, labels."""

    def __init__(self, name, waypoints, color=(220, 30, 30)):
        super().__init__(name)
        self.waypoints = waypoints          # [(label, lat, lon), ...]
        self.color = color
        self.selected = None
        self.adding = False                 # armed by "a", spent by a click
        self._drawn = []                    # where the last on_draw put them
        # A COPY of the projection the last frame was drawn with. The SPI
        # hands a projection to on_draw and none to on_mouse_down, so an
        # overlay that turns a click into a position has to keep one — and
        # keeping a copy beats keeping the borrowed reference, whose lifetime
        # is the caller's business (PythonView replaces its whole engine, and
        # its projection with it, on any catalog change).
        self._snap = pyfvw.engine.MapProjection()
        self._snap_ok = False

    # --- drawing -----------------------------------------------------------

    def on_draw(self, proj, canvas):
        # set_resolution reproduces the transform whatever mode the original
        # was in: below this layer a projection IS a centre plus a
        # degrees-per-pixel pair, and scale/physical-scale are two ways of
        # arriving at that pair.
        try:
            w, h = proj.surface_size
            self._snap.set_surface_size(w, h)
            self._snap.set_resolution(proj.deg_per_pixel_lat,
                                      proj.deg_per_pixel_lon)
            self._snap.set_center(proj.center)
            self._snap_ok = True
        except pyfvw.FvError:
            self._snap_ok = False

        self._drawn = []
        for label, lat, lon in self.waypoints:
            try:
                x, y = proj.geo_to_surface(pyfvw.geo.GeoPoint(lat, lon))
            except pyfvw.FvError:
                continue                    # not on this projection; skip it
            self._drawn.append((label, int(round(x)), int(round(y))))

        if len(self._drawn) >= 2:
            canvas.draw_lines([(x, y) for _, x, y in self._drawn],
                              color=self.color, width=2)

        for label, x, y in self._drawn:
            fill = (255, 220, 0) if label == self.selected else self.color
            canvas.fill_polygon([[(x, y - 7), (x + 7, y), (x, y + 7), (x - 7, y)]],
                                fill=fill, outline=(0, 0, 0), outline_width=1)
            canvas.draw_text(label, x + 10, y - 10, color=(0, 0, 0), size=12.0)

        # Armed mode is a MODE, so it has to be visible: a click that is about
        # to mean something different than usual should say so before it is
        # clicked, not after.
        if self.adding:
            where = f"after {self.selected}" if self.selected else "at the end"
            canvas.draw_text(f"add point {where} - click the map (Esc cancels)",
                             10, 20, color=self.color, size=13.0)

    # --- editing -----------------------------------------------------------

    def _insert_index(self):
        """Where a new point goes: after the selected one, else at the end."""
        for i, (label, _lat, _lon) in enumerate(self.waypoints):
            if label == self.selected:
                return i + 1
        return len(self.waypoints)

    def _new_label(self):
        """A short unique label. Names have to stay unique because selection
        and deletion are BY LABEL — two waypoints called WP3 would delete as
        one."""
        used = {w[0] for w in self.waypoints}
        n = len(self.waypoints) + 1
        while f"WP{n}" in used:
            n += 1
        return f"WP{n}"

    def on_mouse_down(self, e):
        if self.adding:
            # The overlay is handed surface pixels and has to turn them into a
            # position itself, through the projection that drew the frame the
            # user is looking at.
            if not self._snap_ok:
                self.adding = False
                return True
            try:
                p = self._snap.surface_to_geo(e.x, e.y)
            except pyfvw.FvError:
                self.adding = False
                return True
            label = self._new_label()
            self.waypoints.insert(self._insert_index(), (label, p.lat, p.lon))
            self.selected = label           # ready to add the next one after it
            self.adding = False
            return True                     # handled -> stops routing

        # Hit-test what was DRAWN, so a click agrees with the screen.
        for label, x, y in self._drawn:
            if abs(e.x - x) <= 8 and abs(e.y - y) <= 8:
                self.selected = label
                return True                 # handled -> stops routing
        return False

    def on_key_down(self, e):
        # e is a pyfvw.overlay.KeyEvent: e.key is a virtual-key code
        # (pyfvw.overlay.key.*), e.text the character the layout produced,
        # plus e.shift / e.ctrl / e.alt / e.meta.
        k = pyfvw.overlay.key
        if e.key == k.ESCAPE:
            # Escape backs out of one thing at a time — the mode first, since
            # that is the one that changes what a click will do.
            if self.adding:
                self.adding = False
                return True
            if self.selected is not None:
                self.selected = None
                return True
            return False                    # nothing to cancel: let the app quit
        if e.key == ord("A"):
            self.adding = not self.adding
            return True
        if self.selected is None:
            return False
        if e.key == k.DELETE or e.key == k.BACKSPACE or e.key == ord("D"):
            self.waypoints = [w for w in self.waypoints
                              if w[0] != self.selected]
            self.selected = None
            return True                     # handled -> stops routing
        return False
