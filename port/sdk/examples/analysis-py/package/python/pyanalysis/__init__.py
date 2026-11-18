# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""Range & Bearing analysis as a Peregrine Python package.

One file overlay (``.pyana``, JSON) holds any number of measurements (a
two-point range and bearing line, a multi-point path or an area) and any
number of viewshed observers. Every number comes from ``pyfvw.analysis``; the
terrain comes from the map data catalog through ``Overlay.host``. This package
supplies the picture, the editor, a terrain-profile inset and the document
format.

``AnalysisEditor`` puts a palette on the toolbar: Line, Path and Area arm a
drawing tool (each click adds a point to a new measurement, Return finishes,
Escape cancels), Viewshed drops one observer, Profile shows the inset for the
current measurement. With no tool armed a press selects and drags a vertex
or an observer. The context menu carries the per-item commands and works
without the editor.

Threads: pointer, menu, property, file and host handlers run on the UI
thread, which alone opens elevation sources; ``on_draw`` runs on the render
worker and reads a snapshot taken under ``_lock``; each viewshed computes on
a worker of its own and asks the host for a redraw when done.
"""

import copy
import json
import math
import os
import threading

import pyfvw

from . import profile as prof
from .viewshed import Observer, draw_result

an = pyfvw.analysis
app = pyfvw.app
GeoPoint = pyfvw.geo.GeoPoint

FORMAT = 2

PATH_COLOR = (220, 30, 30, 255)
CASING_COLOR = (255, 255, 255, 255)
SELECT_COLOR = (255, 220, 0, 255)
LABEL_COLOR = (10, 10, 10, 255)
LABEL_HALO = (255, 255, 255, 255)
OBSERVER_COLOR = (30, 90, 240, 255)

HANDLE_PX = 5
HIT_PX = 8

#: Hit-test feature ids: vertex i of measurement m is m * VERTEX_STRIDE + i;
#: observer i is OBSERVER_FEATURE + i.
VERTEX_STRIDE = 1000
OBSERVER_FEATURE = 1000000

KIND_NAMES = {
    an.MeasurementKind.RANGE_BEARING: "range_bearing",
    an.MeasurementKind.MULTI_POINT: "multi_point",
    an.MeasurementKind.AREA: "area",
}
KIND_BY_NAME = {v: k for k, v in KIND_NAMES.items()}

TOOL_LINE, TOOL_PATH, TOOL_AREA, TOOL_VIEWSHED = "line", "path", "area", "viewshed"
TOOL_KIND = {
    TOOL_LINE: an.MeasurementKind.RANGE_BEARING,
    TOOL_PATH: an.MeasurementKind.MULTI_POINT,
    TOOL_AREA: an.MeasurementKind.AREA,
}

UNITS = [
    ("nautical_miles", "Nautical Miles", an.RangeUnit.NAUTICAL_MILES),
    ("miles", "Statute Miles", an.RangeUnit.MILES),
    ("kilometers", "Kilometers", an.RangeUnit.KILOMETERS),
    ("meters", "Meters", an.RangeUnit.METERS),
    ("yards", "Yards", an.RangeUnit.YARDS),
    ("feet", "Feet", an.RangeUnit.FEET),
]
LINE_TYPES = [
    ("great_circle", "Great Circle", an.LineType.GREAT_CIRCLE),
    ("rhumb", "Rhumb Line", an.LineType.RHUMB),
]


class Measure:
    """One measured line, path or area."""

    def __init__(self, kind, line_type, points=()):
        self.kind = kind
        self.line_type = line_type
        self.points = list(points)

    def copy(self):
        return Measure(self.kind, self.line_type, self.points)

    def geo_path(self):
        return an.GeoPath(list(self.points), self.line_type)

    def measurement(self, style):
        return an.Measurement(self.kind, self.geo_path(), style)

    def to_json(self):
        return {"kind": KIND_NAMES[self.kind],
                "line_type": _key_of(LINE_TYPES, self.line_type),
                "points": [[p.lat, p.lon] for p in self.points]}

    @staticmethod
    def from_json(d, default_line_type):
        return Measure(KIND_BY_NAME.get(d.get("kind"), an.MeasurementKind.MULTI_POINT),
                       _by_key(LINE_TYPES, d.get("line_type"), default_line_type),
                       [GeoPoint(float(lat), float(lon)) for lat, lon in d.get("points", [])])


class Analysis(pyfvw.overlay.Overlay):
    """The measurements and viewsheds of one document; the host names each instance."""

    def __init__(self):
        super().__init__("Analysis")
        self._lock = threading.RLock()
        self.measures = []
        self.current = None        # index of the measurement the profile and line type act on
        self.line_type = an.LineType.GREAT_CIRCLE  # for new measurements
        self.style = an.MeasureStyle()
        self.observers = []

        self.observer_height_m = 30.0
        self.viewshed_range_m = 25000.0
        self.viewshed_max_posts = 500000
        self.viewshed_draw_budget = 2500
        self.show_profile = True
        self.profile_samples = 200
        self.show_masked = False

        self.defining = False      # the current measurement is still taking points
        self.moving = None         # ("vertex", m, i) | ("observer", i) awaiting "Move Here"
        self.tool = None           # the armed palette tool, or None to select and drag
        self.selected = None       # ("vertex", m, i) | ("observer", i)
        self._profile = (None, "")
        self._undo = []
        self._redo = []
        self._proj = None          # copy of the last drawing projection, for mouse events
        self._drag = None
        self._drag_moved = False

    # MARK: Model

    def current_measure(self):
        """The current measurement, or None."""
        if self.current is None or self.current >= len(self.measures):
            return None
        return self.measures[self.current]

    def elevation(self):
        """A new source over the catalog's elevation data; None without one or outside a window."""
        host = self.host
        if host is None:
            return None
        try:
            return host.open_elevation()
        except pyfvw.FvError:
            return None

    def _changed(self, path=True):
        """Marks the document edited and re-samples the profile if a path moved."""
        if path:
            self._resample()
        self.dirty = True

    def _resample(self):
        m = self.current_measure()
        if self.show_profile and m is not None and len(m.points) >= 2:
            result = prof.sample(self.elevation(), m.geo_path(), self.profile_samples)
        else:
            result = (None, "")
        with self._lock:
            self._profile = result

    def _state(self):
        return ([m.copy() for m in self.measures], self.current)

    def _snapshot(self):
        self._undo.append(self._state())
        del self._undo[:-64]
        self._redo = []

    def _restore(self, state):
        with self._lock:
            self.measures = [m.copy() for m in state[0]]
            self.current = state[1]
            self.defining = False
            self.moving = None
            self.selected = None

    def _set_points(self, mi, points):
        with self._lock:
            self.measures[mi].points = points

    # MARK: Viewsheds

    def _start_viewshed(self, o):
        """Recomputes `o` in the background; the host repaints when it is done."""
        o.start(self.elevation(), self.viewshed_max_posts, self._viewshed_done)

    def _viewshed_done(self):
        host = self.host
        if host is not None:
            host.redraw()

    def on_host_changed(self):
        """In a window, or its catalog changed: computes what lacked terrain."""
        for o in list(self.observers):
            if o.result is None and not o.running:
                self._start_viewshed(o)
        self._resample()

    # MARK: Editing operations

    def start(self, kind, at):
        """Starts a new measurement of `kind` at `at` and makes it current."""
        self._snapshot()
        with self._lock:
            self.measures = self.measures + [Measure(kind, self.line_type, [at])]
            self.current = len(self.measures) - 1
            self.defining = True
            self.moving = None
            self.selected = None
        self._changed()

    def add_point(self, at):
        """Appends `at` to the current measurement; a line finishes at its second point."""
        m = self.current_measure()
        if m is None:
            return
        self._snapshot()
        self._set_points(self.current, m.points + [at])
        if m.kind == an.MeasurementKind.RANGE_BEARING and len(m.points) >= 2:
            self._end_defining()
        self._changed()

    def finish(self):
        self._end_defining()
        self._changed(path=False)

    def cancel_defining(self):
        """Drops a measurement that never reached two points; keeps a longer one."""
        m = self.current_measure()
        if m is not None and len(m.points) < 2:
            self._remove_measure(self.current)
        self._end_defining()
        self._changed()

    def _end_defining(self):
        with self._lock:
            self.defining = False
            self.tool = None

    def _remove_measure(self, mi):
        with self._lock:
            self.measures = self.measures[:mi] + self.measures[mi + 1:]
            if not self.measures:
                self.current = None
            elif self.current is not None and self.current >= mi:
                self.current = max(0, self.current - 1)
            self.moving = None
            self.selected = None

    def move_to(self, at):
        """Completes a pending move of a vertex or an observer."""
        if self.moving is None:
            return
        if self.moving[0] == "vertex":
            _, mi, i = self.moving
            if mi < len(self.measures) and i < len(self.measures[mi].points):
                self._snapshot()
                pts = list(self.measures[mi].points)
                pts[i] = at
                self._set_points(mi, pts)
                with self._lock:
                    self.moving = None
                self._changed()
        else:
            i = self.moving[1]
            if i < len(self.observers):
                o = self.observers[i]
                with self._lock:
                    o.at = at
                    self.moving = None
                self._start_viewshed(o)
                self._changed(path=False)

    def delete_vertex(self, mi, i):
        """Deletes a vertex; a measurement left with fewer than two points goes too."""
        if mi >= len(self.measures) or i >= len(self.measures[mi].points):
            return
        self._snapshot()
        pts = self.measures[mi].points[:i] + self.measures[mi].points[i + 1:]
        if len(pts) < 2 and not (self.defining and mi == self.current):
            self._remove_measure(mi)
        else:
            self._set_points(mi, pts)
            with self._lock:
                self.moving = None
                self.selected = None
        self._changed()

    def delete_measure(self, mi):
        if mi >= len(self.measures):
            return
        self._snapshot()
        self._remove_measure(mi)
        self._changed()

    def add_viewshed(self, at):
        o = Observer(at, self.observer_height_m, self.viewshed_range_m)
        with self._lock:
            self.observers = self.observers + [o]
        self._start_viewshed(o)
        self._changed(path=False)
        return o

    def delete_viewshed(self, i):
        if i >= len(self.observers):
            return
        self.observers[i].cancel()
        with self._lock:
            self.observers = self.observers[:i] + self.observers[i + 1:]
            self.moving = None
            self.selected = None
        self._changed(path=False)

    def recompute_viewsheds(self):
        for o in list(self.observers):
            self._start_viewshed(o)
        self._changed(path=False)

    def clear(self):
        """Deletes every measurement."""
        self._snapshot()
        with self._lock:
            self.measures = []
            self.current = None
            self.defining = False
            self.moving = None
            self.selected = None
        self._changed()

    def clear_viewsheds(self):
        for o in self.observers:
            o.cancel()
        with self._lock:
            self.observers = []
            self.moving = None
            self.selected = None
        self._changed(path=False)

    def set_line_type(self, line_type):
        """Sets the current measurement's line type, and the one new measurements take."""
        m = self.current_measure()
        if m is not None:
            self._snapshot()
            with self._lock:
                m.line_type = line_type
        with self._lock:
            self.line_type = line_type
        self._changed()

    def set_tool(self, tool):
        """Arms `tool`; arming the armed tool disarms it. A drawing tool's first
        click starts a new measurement."""
        if self.defining:
            self.cancel_defining()
        with self._lock:
            self.tool = None if tool == self.tool else tool
            self.selected = None

    def select(self, hit):
        """Selects a vertex or an observer; a vertex makes its measurement current."""
        with self._lock:
            self.selected = hit
        if hit is not None and hit[0] == "vertex" and hit[1] != self.current:
            with self._lock:
                self.current = hit[1]
            self._resample()

    def delete_selected(self):
        """Deletes the selected vertex or observer; False when nothing is selected."""
        if self.selected is None:
            return False
        if self.selected[0] == "vertex":
            self.delete_vertex(self.selected[1], self.selected[2])
        else:
            self.delete_viewshed(self.selected[1])
        return True

    def can_undo(self):
        return bool(self._undo)

    def can_redo(self):
        return bool(self._redo)

    def undo(self):
        if self._undo:
            self._redo.append(self._state())
            self._restore(self._undo.pop())
            self._changed()

    def redo(self):
        if self._redo:
            self._undo.append(self._state())
            self._restore(self._redo.pop())
            self._changed()

    # MARK: Drawing

    def on_draw(self, proj, canvas):
        self._proj = _copy_projection(proj)
        with self._lock:
            measures = [m.copy() for m in self.measures]
            current, defining = self.current, self.defining
            observers = list(self.observers)
            highlight = self.moving or self.selected
            show_profile, show_masked = self.show_profile, self.show_masked
            result, message = self._profile
        gd = pyfvw.draw.GeoDraw(proj, canvas)

        for i, o in enumerate(observers):
            draw_result(proj, canvas, o.result, show_masked, self.viewshed_draw_budget)
            ring = pyfvw.draw.solid_line(OBSERVER_COLOR, 1)
            try:
                gd.circle(o.at, o.range_m, ring)
            except pyfvw.FvError:
                pass
            xy = _project(proj, o.at)
            if xy is not None:
                selected = highlight == ("observer", i)
                _handle(canvas, xy, SELECT_COLOR if selected else OBSERVER_COLOR)
                if o.running:
                    _label(gd, xy[0] + 10, xy[1] - 10, "Computing viewshed…", 11.0)
                elif o.error:
                    _label(gd, xy[0] + 10, xy[1] - 10, o.error, 11.0)

        for mi, m in enumerate(measures):
            open_end = defining and mi == current
            self._draw_measure(proj, canvas, gd, mi, m, open_end, highlight)

        m = measures[current] if current is not None and current < len(measures) else None
        if show_profile and m is not None and len(m.points) >= 2:
            prof.draw_inset(proj, canvas, gd, result, message, self.style, _label)

    def _draw_measure(self, proj, canvas, gd, mi, m, open_end, highlight):
        points = m.points
        if len(points) >= 2:
            style = pyfvw.draw.solid_line(PATH_COLOR, 2)
            style.add_casing(CASING_COLOR, 1)
            closed = m.kind == an.MeasurementKind.AREA and len(points) >= 3 and not open_end
            lk = (pyfvw.geo.LineKind.RHUMB if m.line_type == an.LineType.RHUMB
                  else pyfvw.geo.LineKind.GREAT_CIRCLE)
            gd.polyline(points, style, lk, closed)
        screen = [_project(proj, p) for p in points]
        for i, xy in enumerate(screen):
            if xy is not None:
                selected = highlight == ("vertex", mi, i)
                _handle(canvas, xy, SELECT_COLOR if selected else PATH_COLOR)
        if len(points) >= 2:
            self._draw_labels(proj, gd, m, screen)

    def _draw_labels(self, proj, gd, measure, screen):
        """Places the measurement's strings: the leg midpoint for a line, each
        turning point for a path, the centroid for an area."""
        def put(xy, text, dx=8, dy=-8):
            if xy is not None:
                _label(gd, xy[0] + dx, xy[1] + dy, text, 12.0)

        m = measure.measurement(self.style)
        if measure.kind == an.MeasurementKind.RANGE_BEARING:
            path = measure.geo_path()
            total = path.total_length_m
            if total > 0.0:
                try:
                    put(_project(proj, path.point_at_distance(total / 2.0)), m.leg_label(0), 10, -6)
                except pyfvw.FvError:
                    pass
        elif measure.kind == an.MeasurementKind.MULTI_POINT:
            # Labels belong to the vertices whose incoming leg measured.
            vertices = [i for i in range(1, len(measure.points)) if m.leg(i - 1).ok]
            for i, text in zip(vertices, m.labels):
                put(screen[i], text)
        elif measure.kind == an.MeasurementKind.AREA:
            pts = [p for p in screen if p is not None]
            if pts:
                c = (sum(p[0] for p in pts) // len(pts), sum(p[1] for p in pts) // len(pts))
                put(c, m.summary_label, 0, 0)

    # MARK: Picking

    def hit_test_point(self, proj, x, y, tolerance_px):
        hit = self._grab(proj, x, y, tolerance_px + HANDLE_PX)
        if hit is None:
            return []
        if hit[0] == "vertex":
            _, mi, i = hit
            xy = _project(proj, self.measures[mi].points[i])
            tip = f"Point {i + 1}"
            status = self._vertex_status(mi, i)
            feature = mi * VERTEX_STRIDE + i
        else:
            i = hit[1]
            o = self.observers[i]
            xy = _project(proj, o.at)
            tip = f"Viewshed {i + 1}"
            status = (f"Observer {o.height_m:g} m, range "
                      f"{an.format_range(o.range_m, self.style, 1)}"
                      + (" (computing…)" if o.running else f" ({o.error})" if o.error else ""))
            feature = OBSERVER_FEATURE + i
        d = math.hypot(xy[0] - x, xy[1] - y) if xy is not None else 0.0
        return [app.HitItem(feature=feature, distance_px=d, hint=app.HintText(tip, status))]

    def _vertex_status(self, mi, i):
        measure = self.measures[mi]
        if len(measure.points) < 2:
            return "One point"
        m = measure.measurement(self.style)
        if i == 0:
            return m.summary_label
        return m.leg_label(i - 1) or m.summary_label

    def _grab(self, proj, x, y, within):
        """The vertex (preferred, the current measurement's first) or observer
        handle nearest (x, y) within `within` px."""
        best, best_d = None, within + 1e-9
        order = list(range(len(self.measures)))
        if self.current is not None and self.current < len(order):
            order.remove(self.current)
            order.insert(0, self.current)
        for mi in order:
            for i, p in enumerate(self.measures[mi].points):
                xy = _project(proj, p)
                if xy is not None and math.hypot(xy[0] - x, xy[1] - y) < best_d:
                    best, best_d = ("vertex", mi, i), math.hypot(xy[0] - x, xy[1] - y)
        if best is not None:
            return best
        for i, o in enumerate(self.observers):
            xy = _project(proj, o.at)
            if xy is not None and math.hypot(xy[0] - x, xy[1] - y) < best_d:
                best, best_d = ("observer", i), math.hypot(xy[0] - x, xy[1] - y)
        return best

    # MARK: Context menu

    def menu_items(self, proj, x, y):
        """The editing commands for a right-click at (x, y)."""
        try:
            at = proj.surface_to_geo(x, y)
        except pyfvw.FvError:
            return []
        M = app.MenuNode
        items = []
        K = an.MeasurementKind
        if self.moving is not None:
            noun = "Point" if self.moving[0] == "vertex" else "Viewshed"
            n = self.moving[-1] + 1
            items.append(M(label=f"Move {noun} {n} Here", action=lambda: self.move_to(at)))
            items.append(M(label="Cancel Move", action=self._cancel_move))
        elif self.defining:
            m = self.current_measure()
            need = 3 if m is not None and m.kind == K.AREA else 2
            items.append(M(label="Add Point Here", action=lambda: self.add_point(at)))
            items.append(M(label="Finish Measurement",
                           enabled=m is not None and len(m.points) >= need, action=self.finish))
            items.append(M(label="Cancel Measurement", action=self.cancel_defining))
        else:
            hit = self._grab(proj, x, y, HIT_PX + HANDLE_PX)
            if hit is not None:
                self.select(hit)  # the submenu below acts on what was right-clicked
                if hit[0] == "vertex":
                    _, mi, i = hit
                    items.append(M(label=f"Move Point {i + 1}…", action=lambda: self._arm_move(hit)))
                    items.append(M(label=f"Delete Point {i + 1}",
                                   action=lambda: self.delete_vertex(mi, i)))
                    items.append(M(label="Delete Measurement", action=lambda: self.delete_measure(mi)))
                else:
                    i = hit[1]
                    items.append(M(label=f"Move Viewshed {i + 1}…",
                                   action=lambda: self._arm_move(hit)))
                    items.append(M(label=f"Remove Viewshed {i + 1}",
                                   action=lambda: self.delete_viewshed(i)))
                items.append(M(label=""))
            items.append(M(label="Measure Line From Here",
                           action=lambda: self.start(K.RANGE_BEARING, at)))
            items.append(M(label="Measure Path From Here",
                           action=lambda: self.start(K.MULTI_POINT, at)))
            items.append(M(label="Measure Area From Here",
                           action=lambda: self.start(K.AREA, at)))
            items.append(M(label="Add Viewshed Here", action=lambda: self.add_viewshed(at)))

        m = self.current_measure()
        line_type = m.line_type if m is not None else self.line_type
        more = [
            M(label="Great Circle", checked=line_type == an.LineType.GREAT_CIRCLE,
              action=lambda: self.set_line_type(an.LineType.GREAT_CIRCLE)),
            M(label="Rhumb Line", checked=line_type == an.LineType.RHUMB,
              action=lambda: self.set_line_type(an.LineType.RHUMB)),
            M(label=""),
            M(label="Show Terrain Profile", checked=self.show_profile,
              action=lambda: self.write_property("show_profile", not self.show_profile)),
            M(label="Show Masked Ground", checked=self.show_masked,
              action=lambda: self.write_property("show_masked", not self.show_masked)),
            M(label=""),
            M(label="Undo", enabled=self.can_undo(), action=self.undo),
            M(label="Redo", enabled=self.can_redo(), action=self.redo),
            M(label=""),
            M(label="Recompute Viewsheds", enabled=bool(self.observers),
              action=self.recompute_viewsheds),
            M(label="Clear Measurements", enabled=bool(self.measures), action=self.clear),
            M(label="Clear Viewsheds", enabled=bool(self.observers), action=self.clear_viewsheds),
        ]
        if m is not None and len(m.points) >= 2:
            more.insert(0, M(label=m.measurement(self.style).summary_label or "Measurement",
                             enabled=False))
            more.insert(1, M(label=""))
        items.append(M(label=""))
        items.append(M(label=self.name or "Analysis", children=more))
        return items

    def _arm_move(self, hit):
        with self._lock:
            self.moving = hit

    def _cancel_move(self):
        with self._lock:
            self.moving = None

    # MARK: Pointer (while the editor is active)

    def on_mouse_down(self, e):
        """Left press: places a point or an observer with an armed tool, or
        selects a vertex (preferred) or an observer to drag."""
        if self._proj is None or e.button != 0:
            return False
        at = self._unproject(e.x, e.y)
        if at is None:
            return False
        if self.tool == TOOL_VIEWSHED:
            self.add_viewshed(at)
            with self._lock:
                self.tool = None  # one observer per press of the button
            return True
        if self.tool in TOOL_KIND:
            if self.defining:
                self.add_point(at)
            else:
                self.start(TOOL_KIND[self.tool], at)
            return True
        hit = self._grab(self._proj, e.x, e.y, HIT_PX + HANDLE_PX)
        self.select(hit)
        if hit is None:
            return False
        self._drag = hit
        self._drag_moved = False
        if self.host is not None:
            self.host.capture_mouse()
        return True

    def on_mouse_move(self, e):
        if self._drag is None or self._proj is None:
            return False
        at = self._unproject(e.x, e.y)
        if at is None:
            return True
        if self._drag[0] == "vertex":
            _, mi, i = self._drag
            if mi < len(self.measures) and i < len(self.measures[mi].points):
                if not self._drag_moved:
                    self._snapshot()  # the whole drag is one undo entry
                pts = list(self.measures[mi].points)
                pts[i] = at
                self._set_points(mi, pts)
        elif self._drag[1] < len(self.observers):
            with self._lock:
                self.observers[self._drag[1]].at = at
        self._drag_moved = True
        return True

    def on_mouse_up(self, e):
        if self._drag is None:
            return False
        drag, moved = self._drag, self._drag_moved
        self._drag = None
        if self.host is not None:
            self.host.release_mouse()
        if not moved:
            return True
        if drag[0] == "vertex":
            self._changed()
        elif drag[1] < len(self.observers):
            self._start_viewshed(self.observers[drag[1]])  # the old answer is about elsewhere
            self._changed(path=False)
        return True

    def on_key_down(self, ev):
        k = pyfvw.overlay.key
        if ev.key == k.ESCAPE:
            if self.defining:
                self.cancel_defining()
            elif self.moving is not None:
                self._cancel_move()
            elif self.tool is not None:
                self.set_tool(self.tool)
            elif self.selected is not None:
                self.select(None)
            else:
                return False
            return True
        if ev.key == k.RETURN and self.defining:
            self.finish()
            return True
        if ev.key in (k.DELETE, k.BACKSPACE):
            return self.delete_selected()
        return False

    def _unproject(self, x, y):
        try:
            return self._proj.surface_to_geo(x, y)
        except pyfvw.FvError:
            return None

    # MARK: Document

    def file_new(self):
        pass

    def file_open(self, spec):
        """Reads format 2 (a list of measurements) or format 1 (a single one)."""
        with open(spec, encoding="utf-8") as f:
            doc = json.load(f)
        if int(doc.get("format", 1)) > FORMAT:
            raise ValueError(f"{os.path.basename(spec)} needs a newer Analysis package")
        line_type = _by_key(LINE_TYPES, doc.get("line_type"), an.LineType.GREAT_CIRCLE)
        if "measurements" in doc:
            measures = [Measure.from_json(d, line_type) for d in doc["measurements"]]
        elif doc.get("points"):
            measures = [Measure.from_json(doc, line_type)]
        else:
            measures = []
        for o in self.observers:
            o.cancel()
        with self._lock:
            self.measures = measures
            self.current = len(measures) - 1 if measures else None
            self.line_type = line_type
            self.style.units = _by_key(UNITS, doc.get("units"), an.RangeUnit.NAUTICAL_MILES)
            self.observer_height_m = float(doc.get("observer_height_m", self.observer_height_m))
            self.viewshed_range_m = float(doc.get("viewshed_range_m", self.viewshed_range_m))
            self.viewshed_max_posts = int(doc.get("viewshed_max_posts", self.viewshed_max_posts))
            self.show_profile = bool(doc.get("show_profile", self.show_profile))
            self.profile_samples = int(doc.get("profile_samples", self.profile_samples))
            self.show_masked = bool(doc.get("show_masked", self.show_masked))
            self.observers = [Observer.from_json(o) for o in doc.get("observers", [])]
            self.defining = False
            self.moving = None
            self.selected = None
        self._undo, self._redo = [], []
        for o in self.observers:
            self._start_viewshed(o)
        self._resample()

    def file_save_as(self, spec, format_index):
        doc = {
            "format": FORMAT,
            "measurements": [m.to_json() for m in self.measures],
            "line_type": _key_of(LINE_TYPES, self.line_type),
            "observers": [o.to_json() for o in self.observers],
            "units": _key_of(UNITS, self.style.units),
            "observer_height_m": self.observer_height_m,
            "viewshed_range_m": self.viewshed_range_m,
            "viewshed_max_posts": self.viewshed_max_posts,
            "show_profile": self.show_profile,
            "profile_samples": self.profile_samples,
            "show_masked": self.show_masked,
        }
        with open(spec, "w", encoding="utf-8") as f:
            json.dump(doc, f, indent=2)
            f.write("\n")

    # MARK: Properties

    def properties_schema(self):
        return [
            {"key": "units", "label": "Distance units", "group": "Measurement",
             "type": "choice", "choices": [u[1] for u in UNITS], "default": 0},
            {"key": "line_type", "label": "Line type for new measurements",
             "group": "Measurement", "type": "choice",
             "choices": [t[1] for t in LINE_TYPES], "default": 0},
            {"key": "show_profile", "label": "Show terrain profile", "group": "Profile",
             "type": "bool", "default": True},
            {"key": "profile_samples", "label": "Profile samples", "group": "Profile",
             "type": "int", "default": 200, "min": 3, "max": 2000},
            {"key": "observer_height_m", "label": "Observer height (m)", "group": "Viewshed",
             "type": "double", "default": 30.0, "min": 0.0, "max": 10000.0},
            {"key": "viewshed_range_m", "label": "Viewshed range (m)", "group": "Viewshed",
             "type": "double", "default": 25000.0, "min": 100.0, "max": 200000.0},
            {"key": "viewshed_max_posts", "label": "Viewshed post limit", "group": "Viewshed",
             "type": "int", "default": 500000, "min": 10000, "max": 4000000},
            {"key": "show_masked", "label": "Show masked ground", "group": "Viewshed",
             "type": "bool", "default": False},
        ]

    def read_property(self, key):
        return {
            "units": _index_of(UNITS, self.style.units),
            "line_type": _index_of(LINE_TYPES, self.line_type),
            "show_profile": self.show_profile,
            "profile_samples": self.profile_samples,
            "observer_height_m": self.observer_height_m,
            "viewshed_range_m": self.viewshed_range_m,
            "viewshed_max_posts": self.viewshed_max_posts,
            "show_masked": self.show_masked,
        }[key]

    def write_property(self, key, value):
        """Applies one property; profile changes re-sample it."""
        with self._lock:
            if key == "units":
                self.style.units = UNITS[int(value)][2]
            elif key == "line_type":
                self.line_type = LINE_TYPES[int(value)][2]
            elif key == "show_profile":
                self.show_profile = bool(value)
            elif key == "profile_samples":
                self.profile_samples = int(value)
            elif key == "observer_height_m":
                self.observer_height_m = float(value)
            elif key == "viewshed_range_m":
                self.viewshed_range_m = float(value)
            elif key == "viewshed_max_posts":
                self.viewshed_max_posts = int(value)
            elif key == "show_masked":
                self.show_masked = bool(value)
        if key in ("show_profile", "profile_samples"):
            self._resample()
        self.dirty = True


class AnalysisEditor(app.OverlayEditor):
    """The measuring palette. It holds no state: each button reads and acts on
    the edited overlay, so the palette follows a change of current overlay."""

    def tools(self):
        o = self.edited if isinstance(self.edited, Analysis) else None
        M = app.MenuNode

        def arm(label, tool, icon):
            return M(label=label, icon=icon, enabled=o is not None,
                     checked=o is not None and o.tool == tool,
                     action=lambda: o.set_tool(tool))

        return [
            arm("Line", TOOL_LINE, "straight_legs"),
            arm("Path", TOOL_PATH, "draw_line"),
            arm("Area", TOOL_AREA, "draw_area"),
            arm("Viewshed", TOOL_VIEWSHED, "resources/viewshed.svg"),
            M(label="Profile", icon="elevation_profile", enabled=o is not None,
              checked=o is not None and o.show_profile,
              action=lambda: o.write_property("show_profile", not o.show_profile)),
            M(label=""),
            M(label="Delete Selected", icon="delete_vertex",
              enabled=o is not None and o.selected is not None, action=lambda: o.delete_selected()),
            M(label="Clear", icon="delete_feature",
              enabled=o is not None and bool(o.measures or o.observers), action=lambda: _clear(o)),
        ]


def _clear(o):
    o.clear()
    o.clear_viewsheds()


def open_sample(ctx):
    """Opens the sample measurements shipped in the package's resources."""
    ctx.open_file(os.path.join(ctx.package_dir, "resources", "charleston.pyana"))


# MARK: Helpers

def _copy_projection(proj):
    """A projection equal to `proj` that outlives the draw call."""
    return copy.copy(proj)


def _project(proj, p):
    try:
        x, y = proj.geo_to_surface(p)
        return (int(round(x)), int(round(y)))
    except pyfvw.FvError:
        return None


def _handle(canvas, xy, color):
    x, y = xy
    h = HANDLE_PX
    canvas.fill_polygon([[(x - h, y - h), (x + h, y - h), (x + h, y + h), (x - h, y + h)]],
                        fill=color, outline=(0, 0, 0, 255), outline_width=1)


def _label(gd, x, y, text, size):
    """A haloed string; a canvas without a font draws nothing instead of failing."""
    if not text:
        return
    try:
        gd.label_at_pixel(x, y, text, LABEL_COLOR, size=size,
                          halo_color=LABEL_HALO, halo_width=2.0)
    except pyfvw.FvError:
        pass


def _by_key(table, key, default):
    for k, _label_text, v in table:
        if k == key:
            return v
    return default


def _key_of(table, value):
    for k, _label_text, v in table:
        if v == value:
            return k
    return table[0][0]


def _index_of(table, value):
    for i, (_k, _label_text, v) in enumerate(table):
        if v == value:
            return i
    return 0
