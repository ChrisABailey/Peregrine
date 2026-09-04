# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""The Analysis tools as a shell sees them — AN6 and AN7 of
`port/analysis-plan.md`.

WHAT IS AND IS NOT HERE. Every number in this file comes from
`pyfvw.analysis`, which is AN1-AN5: the geodesy, the terrain profile, the XDraw
viewshed and every label FalconView draws are C++ in FvKit and are pinned by 61
gtests and 20 pytests. What is written here is the three things a headless
library cannot have — a picture, a gesture and a window:

  AnalysisOverlay   draws the path, its labels and the viewsheds; takes the
                    clicks that DEFINE and REFINE the path
  AnalysisEditor    the palette — the buttons that pick the tool
  ProfileWindow     AN6's tk chart over `sample_terrain_profile`

THE PALETTE IS THE POINT (Chris, 2026-09-02). FalconView's Range & Bearing
overlay put up a small button bar: you clicked a tool, drew with it, and the
menu bar stayed the size it was. `toolbar.py` renders that from the `MenuNode`
list `tools()` has always returned, so this file describes its palette as data
and never touches tk to do it — the same list also fills the Tools menu.

THREE DECISIONS WORTH NOT RE-DERIVING.

1. **The path lives on the OVERLAY and the tool lives on the overlay too**,
   not on the editor. It is the same split RouteKit settled: `RouteEditSession`
   hangs off `RouteOverlay` because the gesture state belongs to the document
   being edited, and a second shell (a phone) shares it while sharing none of
   the palette. The editor here holds no state at all beyond a back-pointer.

2. **Nothing routes unless the overlay has EDIT FOCUS.** Keys and mouse reach
   every visible overlay, not only the edited one, so an overlay that took
   `d` whenever it was on screen would delete a vertex while the user was
   doing something else entirely. `enter_edit_focus`/`release_edit_focus` is
   the app layer's per-instance half of the editor contract and this is what
   it is for.

3. **The viewshed picture is QUADS, not a raster.** `ICanvas` can blend a
   `PixelBuffer` but pyfvw cannot MAKE one, so TA4's alpha-blended pixmap has
   no Python spelling. It does not need one: the projection is affine (equal
   arc plus a rotation about the centre), so the lattice's screen positions are
   `P00 + c*U + r*V` from THREE `geo_to_surface` calls, and a row of visible
   posts is one quad per run. One `fill_polygon` call takes every quad at once.
   The drawn lattice is decimated to the SCREEN (`_draw_stride`), so the cost
   is a property of the window and not of `max_posts`.
"""

import math
import threading

import pyfvw

an = pyfvw.analysis
app = pyfvw.app

#: The registered type id. STATIC — at most one instance, toggled rather than
#: opened, because `.rbo` is deliberately out of scope (analysis-plan §2) and
#: an overlay with no document is exactly what a static descriptor means.
ANALYSIS_TYPE_ID = "app.analysis"

# The tools, spelled once. `TOOL_SELECT` is not a mode so much as the absence
# of one: it refines what is already drawn.
TOOL_SELECT = "select"
TOOL_LINE = "line"
TOOL_POLYLINE = "polyline"
TOOL_AREA = "area"
TOOL_VIEWSHED = "viewshed"

#: Which measurement each drawing tool produces. FalconView had four objects
#: and four classes; AN4 has one class and this table.
TOOL_KIND = {
    TOOL_LINE: an.MeasurementKind.RANGE_BEARING,
    TOOL_POLYLINE: an.MeasurementKind.MULTI_POINT,
    TOOL_AREA: an.MeasurementKind.AREA,
}

PATH_COLOR = (220, 30, 30)
CASING_COLOR = (255, 255, 255)
SELECT_COLOR = (255, 220, 0)
LABEL_COLOR = (10, 10, 10)
LABEL_HALO = (255, 255, 255)
VISIBLE_COLOR = (40, 200, 60, 110)
MASKED_COLOR = (200, 40, 40, 80)
OBSERVER_COLOR = (30, 90, 240)

HANDLE_PX = 5           # half-width of a vertex handle
HIT_PX = 8              # how close a click has to be to grab one


# ---------------------------------------------------------------------------
# AN7 — one viewshed, computed off the UI thread
# ---------------------------------------------------------------------------

class ViewshedObserver:
    """An observer, its request, and the result when it arrives.

    THE WORKER IS THE WHOLE REASON THIS IS A CLASS. AN5 releases the GIL for
    the computation precisely so a UI thread keeps painting through it — a
    million posts is ~105 ms of arithmetic in Release and several times that in
    a default (unoptimised) build, which is why FalconView grew a progress
    dialog. The result is polled rather than pushed: tk is not thread-safe and
    a callback that touched a widget from here would crash rarely and
    unreproducibly.

    Each job gets its OWN elevation source when the caller can make one. A
    `DtedElevationSource` makes no thread-safety promise, and sharing the one
    the map is drawing with is the kind of bug that shows up as a wrong pixel
    once a week.
    """

    def __init__(self, at, height_m, range_m, method=None, sector=None):
        self.at = at
        self.height_m = height_m
        self.range_m = range_m
        self.method = method or an.HeightMethod.INTERPOLATED
        self.sector = sector          # None, or (bearing_deg, angle_deg)
        self.result = None
        self.error = ""
        self.percent = 0
        self._cancel = False
        self._thread = None

    # --- the request --------------------------------------------------------

    def request(self, step_deg):
        req = an.ViewshedRequest()
        req.observer = self.at
        req.observer_height_m = self.height_m
        req.range_m = self.range_m
        req.step_deg = step_deg
        req.method = self.method
        if self.sector is not None:
            req.has_sector = True
            req.sector = an.ViewshedSector(bearing_deg=self.sector[0],
                                           angle_deg=self.sector[1])
        return req

    # --- running ------------------------------------------------------------

    @property
    def running(self):
        return self._thread is not None and self._thread.is_alive()

    def start(self, source, source_factory=None, max_posts=0):
        """Kick the computation off. Returns False when there is nothing to
        compute with, which is a state the caller has to be able to draw (an
        observer with no terrain is still a mark on the map)."""
        if source is None and source_factory is None:
            self.error = "no elevation data"
            return False
        self.cancel()
        self.result = None
        self.error = ""
        self.percent = 0
        self._cancel = False
        self._thread = threading.Thread(
            target=self._run, args=(source, source_factory, max_posts),
            daemon=True)
        self._thread.start()
        return True

    def cancel(self):
        self._cancel = True
        t = self._thread
        if t is not None and t.is_alive():
            t.join(timeout=5.0)
        self._thread = None
        self._cancel = False

    def wait(self, timeout=None):
        """Block until the answer is in. Only a test and a headless run call
        this; the application polls."""
        t = self._thread
        if t is not None:
            t.join(timeout)

    def _run(self, source, source_factory, max_posts):
        try:
            src = source
            if source_factory is not None:
                made = source_factory()
                if made is not None:
                    src = made
            if src is None:
                self.error = "no elevation data"
                return
            step = an.viewshed_step_from_post_spacing(src, self.at)
            if step <= 0.0:
                # A source with no opinion about its own spacing is legal (the
                # binding says so). One arcsecond is DTED level 2 and is a
                # smaller lie than any step derived from the screen.
                step = 1.0 / 3600.0
            req = self.request(step)
            if max_posts > 0:
                req.max_posts = max_posts
            self.result = an.compute_viewshed(src, req, self._progress)
        except pyfvw.FvError as e:
            self.result = None
            self.error = e.message
        except Exception as e:                       # noqa: BLE001
            self.result = None
            self.error = str(e)

    def _progress(self, percent):
        self.percent = percent
        # False cancels; None does NOT (AN5 note 2), so this must return a
        # bool and not fall off the end.
        return not self._cancel


# ---------------------------------------------------------------------------
# The overlay
# ---------------------------------------------------------------------------

class AnalysisOverlay(pyfvw.overlay.Overlay):
    """The Range & Bearing picture: one measured path plus any number of
    viewsheds, drawn over whatever the map is showing."""

    def __init__(self, name="Analysis", manager=None, settings=None):
        super().__init__(name)
        self.manager = manager      # for mouse CAPTURE; None in a test
        self.elevation = None
        self.elevation_factory = None

        self.points = []            # [GeoPoint] — the path being measured
        self.line_type = an.LineType.GREAT_CIRCLE
        self.kind = an.MeasurementKind.RANGE_BEARING
        self.style = an.MeasureStyle()
        self.observers = []
        self.selected = None        # ("vertex", i) | ("observer", i) | None

        self.tool = TOOL_SELECT
        self.defining = False
        self.show_masked = False

        # Viewshed defaults. FalconView read these from a property sheet; here
        # the ini is the property sheet, as it is everywhere else in the port.
        cfg = settings
        self.vs_height_m = cfg.get_float("analysis.observer_height_m", 30.0) \
            if cfg else 30.0
        self.vs_range_m = cfg.get_float("analysis.viewshed_range_m", 25000.0) \
            if cfg else 25000.0
        self.vs_max_posts = cfg.get_int("analysis.viewshed_max_posts", 0) \
            if cfg else 0
        self.profile_samples = cfg.get_int("analysis.profile_samples", 200) \
            if cfg else 200
        # How many quads the viewshed picture may cost per frame. See
        # `_ring_budget` — this is a FRAME TIME knob, not a quality one.
        self.draw_budget = cfg.get_int("analysis.viewshed_draw_rings", 2500) \
            if cfg else 2500

        self._edit_focus = False
        self._proj = None           # last projection drawn with; see on_draw
        self._rubber = None         # where the next click would land
        self._drag = None           # what a move is currently editing
        self._undo = []
        self._redo = []
        self._changed = None        # set by the shell: "the path moved"

    # --- what the shell wires up -------------------------------------------

    def set_elevation_source(self, source, factory=None):
        """The terrain under everything here. `factory` makes a SECOND source
        for a viewshed worker to own (see ViewshedObserver); without one the
        worker shares this object and the caller has said that is safe."""
        self.elevation = source
        self.elevation_factory = factory

    def set_on_changed(self, fn):
        """Called after anything that changes the measured path. The profile
        window subscribes, so a dragged vertex redraws the chart."""
        self._changed = fn

    # --- the path -----------------------------------------------------------

    def geo_path(self):
        return an.GeoPath(list(self.points), self.line_type)

    def measurement(self):
        return an.Measurement(self.kind, self.geo_path(), self.style)

    def set_points(self, points, kind=None):
        self._snapshot()
        self.points = list(points)
        if kind is not None:
            self.kind = kind
        self._notify()

    def clear(self):
        self._snapshot()
        self.points = []
        self.defining = False
        self.selected = None
        self._rubber = None
        self._notify()

    def clear_viewsheds(self):
        for o in self.observers:
            o.cancel()
        self.observers = []
        if self.selected and self.selected[0] == "observer":
            self.selected = None

    def _notify(self):
        if self._changed is not None:
            try:
                self._changed()
            except Exception:                        # noqa: BLE001
                pass

    # --- undo (the per-instance editor contract) ----------------------------

    def _snapshot(self):
        self._undo.append(list(self.points))
        del self._undo[:-64]
        self._redo = []

    def can_undo(self):
        return bool(self._undo)

    def can_redo(self):
        return bool(self._redo)

    def undo(self):
        if not self._undo:
            return False
        self._redo.append(list(self.points))
        self.points = self._undo.pop()
        self.selected = None
        self._notify()
        return True

    def redo(self):
        if not self._redo:
            return False
        self._undo.append(list(self.points))
        self.points = self._redo.pop()
        self.selected = None
        self._notify()
        return True

    def enter_edit_focus(self):
        self._edit_focus = True

    def release_edit_focus(self):
        self._edit_focus = False
        self._end_gesture()
        self.defining = False
        self._rubber = None

    @property
    def has_edit_focus(self):
        return self._edit_focus

    # --- viewsheds ----------------------------------------------------------

    def add_viewshed(self, at):
        o = ViewshedObserver(at, self.vs_height_m, self.vs_range_m)
        self.observers.append(o)
        o.start(self.elevation, self.elevation_factory, self.vs_max_posts)
        return o

    def recompute_viewsheds(self):
        for o in self.observers:
            o.start(self.elevation, self.elevation_factory, self.vs_max_posts)

    @property
    def viewshed_running(self):
        return any(o.running for o in self.observers)

    def status_line(self):
        """One line for the shell's status bar — the progress a long
        computation owes the user, and nothing when there is none."""
        for o in self.observers:
            if o.running:
                return f"Viewshed {o.percent}%"
        errs = [o.error for o in self.observers if o.error]
        if errs:
            return "Viewshed: " + errs[0]
        return ""

    # --- drawing ------------------------------------------------------------

    def on_draw(self, proj, canvas):
        # The projection is REMEMBERED here because a mouse event arrives
        # without one and a click has to mean the same thing the last frame
        # drew. RouteOverlay keeps `has_projection` for the same reason.
        self._proj = proj
        draw = pyfvw.draw.GeoDraw(proj, canvas)

        for i, o in enumerate(self.observers):
            self._draw_viewshed(proj, canvas, draw, i, o)

        pts = list(self.points)
        if self._rubber is not None and self.defining and pts:
            pts = pts + [self._rubber]
        if len(pts) >= 2:
            style = pyfvw.draw.solid_line(PATH_COLOR, 2)
            style.add_casing(CASING_COLOR, 1)
            closed = (self.kind == an.MeasurementKind.AREA and
                      len(pts) >= 3 and not self.defining)
            draw.polyline(pts, style, self._line_kind(), closed)

        screen = self._project(proj, self.points)
        for i, xy in enumerate(screen):
            if xy is None:
                continue
            sel = self.selected == ("vertex", i)
            self._handle(canvas, xy, SELECT_COLOR if sel else PATH_COLOR)

        self._draw_labels(proj, draw, screen)

    def _line_kind(self):
        g = pyfvw.geo.LineKind
        return (g.RHUMB if self.line_type == an.LineType.RHUMB
                else g.GREAT_CIRCLE)

    @staticmethod
    def _project(proj, points):
        out = []
        for p in points:
            try:
                x, y = proj.geo_to_surface(p)
                out.append((int(round(x)), int(round(y))))
            except pyfvw.FvError:
                out.append(None)
        return out

    @staticmethod
    def _handle(canvas, xy, color):
        x, y = xy
        h = HANDLE_PX
        canvas.fill_polygon([[(x - h, y - h), (x + h, y - h),
                              (x + h, y + h), (x - h, y + h)]],
                            fill=color, outline=(0, 0, 0), outline_width=1)

    def _draw_labels(self, proj, draw, screen):
        """Where each of AN4's strings goes. The STRINGS are FalconView's,
        padding spaces included; only the placement is this file's."""
        if len(self.points) < 2:
            return
        m = self.measurement()

        def put(xy, text, dx=8, dy=-8):
            if xy is not None:
                _label(draw, xy[0] + dx, xy[1] + dy, text, 12.0)

        if self.kind == an.MeasurementKind.RANGE_BEARING:
            # At the midpoint of the one leg, which is where the Windows
            # object draws it.
            put(self._midpoint_pixel(proj), m.leg_label(0), dx=10, dy=-6)
            return

        if self.kind == an.MeasurementKind.MULTI_POINT:
            # A CUMULATIVE distance at each turning point after the first, and
            # a leg the geodesy refused contributes nothing at all — so the
            # vertices these belong to are exactly the ones whose incoming leg
            # measured. Same filter as `Measurement::Labels`.
            labels = m.labels
            vertices = [i for i in range(1, len(self.points))
                        if m.leg(i - 1).ok]
            for i, text in zip(vertices, labels):
                put(screen[i] if i < len(screen) else None, text)
            return

        if self.kind == an.MeasurementKind.AREA:
            put(self._centroid_pixel(screen), m.summary_label, dx=0, dy=0)
            return

        put(screen[-1] if screen else None, m.summary_label)

    def _midpoint_pixel(self, proj):
        path = self.geo_path()
        total = path.total_length_m
        if total <= 0.0:
            return None
        try:
            mid = path.point_at_distance(total / 2.0)
            x, y = proj.geo_to_surface(mid)
            return (int(round(x)), int(round(y)))
        except pyfvw.FvError:
            return None

    @staticmethod
    def _centroid_pixel(screen):
        pts = [p for p in screen if p is not None]
        if not pts:
            return None
        return (sum(p[0] for p in pts) // len(pts),
                sum(p[1] for p in pts) // len(pts))

    # --- the viewshed picture ----------------------------------------------

    def _draw_viewshed(self, proj, canvas, draw, index, obs):
        r = obs.result
        if r is not None and r.valid:
            self._draw_viewshed_grid(proj, canvas, r)
        # The ring is drawn whatever the state of the computation: an observer
        # still thinking, and one whose terrain never arrived, are both marks
        # on the map and both have to be draggable.
        ring = pyfvw.draw.solid_line(OBSERVER_COLOR, 1)
        try:
            if obs.sector is None:
                draw.circle(obs.at, obs.range_m, ring)
            else:
                # The wedge is drawn as its ARC. G1's arc is a real geographic
                # contour, so it clips and densifies like every other line
                # here; the two radial edges are left out because there is no
                # `end_point` in pyfvw.geo to place them with and an arc alone
                # already says where the crop is.
                bearing, angle = obs.sector
                draw.arc(obs.at, obs.range_m, bearing - angle / 2.0, angle,
                         ring)
        except pyfvw.FvError:
            pass
        try:
            x, y = proj.geo_to_surface(obs.at)
        except pyfvw.FvError:
            return
        sel = self.selected == ("observer", index)
        self._handle(canvas, (int(round(x)), int(round(y))),
                     SELECT_COLOR if sel else OBSERVER_COLOR)
        if obs.running:
            _label(draw, x + 10, y - 10, f"{obs.percent}%", 11.0)
        elif obs.error:
            _label(draw, x + 10, y - 10, obs.error, 11.0)

    @staticmethod
    def _draw_stride(span, span_px, target_px=2.0):
        """How many posts one drawn cell is worth, from the SCREEN — so a
        million-post viewshed and a ten-thousand-post one cost the same to
        draw, and a viewshed zoomed out to a thumbnail costs almost nothing.

        This is the FLOOR on the decimation; `_ring_budget` raises it further
        when the runs come out too numerous, because how many runs a mask
        breaks into is a property of the terrain and cannot be predicted from
        the geometry."""
        if span <= 0 or span_px <= 0:
            return 1
        cells = max(8, min(span, int(round(span_px / target_px))))
        return max(1, span // cells)

    def _draw_viewshed_grid(self, proj, canvas, r):
        try:
            import numpy as np
        except ImportError:
            return
        b = r.bounds
        span = r.span
        if span < 2:
            return
        # THE AFFINE TRICK (see this file's header, note 3). Three corners of
        # the lattice give the two screen vectors one grid step moves by; every
        # other node is arithmetic. It is exact for equal-arc plus a rotation
        # about the centre, which is every projection fvkit has.
        try:
            x00, y00 = proj.geo_to_surface(pyfvw.geo.GeoPoint(b.ur.lat, b.ll.lon))
            x01, y01 = proj.geo_to_surface(pyfvw.geo.GeoPoint(b.ur.lat, b.ur.lon))
            x10, y10 = proj.geo_to_surface(pyfvw.geo.GeoPoint(b.ll.lat, b.ll.lon))
        except pyfvw.FvError:
            return
        n = span - 1
        ux, uy = (x01 - x00) / n, (y01 - y00) / n      # one column east
        vx, vy = (x10 - x00) / n, (y10 - y00) / n      # one row south
        span_px = max(abs(x01 - x00) + abs(x10 - x00),
                      abs(y01 - y00) + abs(y10 - y00))
        floor = self._draw_stride(span, span_px)

        full = np.asarray(r)
        bands = [(lambda g: g == 0.0, VISIBLE_COLOR)]
        if self.show_masked:
            bands.insert(0, (lambda g: np.isfinite(g) & (g != 0.0),
                             MASKED_COLOR))

        for predicate, color in bands:
            stride, rows_i, c0, c1 = _ring_budget(full, predicate, floor,
                                                  self.draw_budget)
            if stride is None:
                continue
            a = c0 * stride
            b = np.minimum(c1 * stride, n)
            t = rows_i * stride
            u = np.minimum((rows_i + 1) * stride, n)
            # Every corner at once — the affine map above, applied as
            # arithmetic. There is one Python-level loop left and it is the
            # one that hands the polygons across the binding.
            xs = np.column_stack([x00 + a * ux + t * vx, x00 + b * ux + t * vx,
                                  x00 + b * ux + u * vx, x00 + a * ux + u * vx])
            ys = np.column_stack([y00 + a * uy + t * vy, y00 + b * uy + t * vy,
                                  y00 + b * uy + u * vy, y00 + a * uy + u * vy])
            rings = [list(zip(xr, yr)) for xr, yr in
                     zip(xs.astype(int).tolist(), ys.astype(int).tolist())]
            if rings:
                # ONE crossing for the whole picture. The runs are disjoint, so
                # an even-odd fill over all of them is their union.
                canvas.fill_polygon(rings, fill=color)

    # --- picking ------------------------------------------------------------

    def hit_test_point(self, proj, x, y, tolerance_px):
        out = []
        for i, xy in enumerate(self._project(proj, self.points)):
            if xy is None:
                continue
            d = math.hypot(xy[0] - x, xy[1] - y)
            if d <= tolerance_px + HANDLE_PX:
                out.append(app.HitItem(
                    feature=i, distance_px=d,
                    hint=app.HintText(f"vertex {i + 1}", self._vertex_hint(i))))
        return out

    def _vertex_hint(self, i):
        if len(self.points) < 2:
            return "analysis: one point"
        m = self.measurement()
        if i == 0:
            return m.summary_label
        return m.leg_label(i - 1) or m.summary_label

    # --- gestures -----------------------------------------------------------

    def on_mouse_down(self, e):
        if not self._edit_focus or self._proj is None or e.button != 0:
            return False
        pt = self._unproject(e.x, e.y)
        if pt is None:
            return False

        if self.tool == TOOL_VIEWSHED:
            self.add_viewshed(pt)
            # ONE SHOT, back to select. Dropping observers is rare and moving
            # the one you just dropped is not, so staying armed would put the
            # commonest next gesture behind a second button press.
            self.tool = TOOL_SELECT
            return True

        if self.tool in TOOL_KIND:
            if not self.defining:
                self._snapshot()
                self.points = [pt]
                self.kind = TOOL_KIND[self.tool]
                self.defining = True
            else:
                self.points.append(pt)
                if (self.tool == TOOL_LINE and len(self.points) >= 2):
                    self._finish_defining()
            self._notify()
            return True

        grabbed = self._grab(e.x, e.y)
        if grabbed is not None:
            self.selected = grabbed
            if grabbed[0] == "vertex":
                self._snapshot()
            self._drag = grabbed
            if self.manager is not None:
                self.manager.capture_mouse(self)
            return True
        return False

    def on_mouse_move(self, e):
        if not self._edit_focus or self._proj is None:
            return False
        if self._drag is not None:
            pt = self._unproject(e.x, e.y)
            if pt is None:
                return True
            what, i = self._drag
            if what == "vertex" and i < len(self.points):
                self.points[i] = pt
            elif what == "observer" and i < len(self.observers):
                self.observers[i].at = pt
            return True
        if self.defining:
            self._rubber = self._unproject(e.x, e.y)
            return self._rubber is not None
        return False

    def on_mouse_up(self, e):
        if self._drag is None:
            return False
        what, i = self._drag
        self._end_gesture()
        if what == "vertex":
            self._notify()
        elif what == "observer" and i < len(self.observers):
            # A moved observer sees different ground; the old answer is about
            # somewhere else and keeping it on screen would be a lie.
            o = self.observers[i]
            o.start(self.elevation, self.elevation_factory, self.vs_max_posts)
        return True

    def on_key_down(self, ev):
        if not self._edit_focus:
            return False
        k = pyfvw.overlay.key
        ch = chr(ev.text) if ev.text else ""
        if ev.key == k.ESCAPE:
            if self.defining:
                self._finish_defining()
                return True
            if self._drag is not None:
                self._end_gesture()
                return True
            if self.tool != TOOL_SELECT:
                self.tool = TOOL_SELECT
                return True
            if self.selected is not None:
                self.selected = None
                return True
            return False
        if ev.key == k.RETURN:
            if self.defining:
                self._finish_defining()
                return True
            return False
        if ev.key == k.DELETE or ch == "d":
            return self.delete_selected()
        if ch == "u":
            return self.undo()
        if ch == "g":
            self.cycle_line_type()
            return True
        return False

    def delete_selected(self):
        if self.selected is None:
            return False
        what, i = self.selected
        if what == "vertex" and i < len(self.points):
            self._snapshot()
            del self.points[i]
            self.selected = None
            self._notify()
            return True
        if what == "observer" and i < len(self.observers):
            self.observers[i].cancel()
            del self.observers[i]
            self.selected = None
            return True
        return False

    def cycle_line_type(self):
        self.line_type = (an.LineType.RHUMB
                          if self.line_type == an.LineType.GREAT_CIRCLE
                          else an.LineType.GREAT_CIRCLE)
        self._notify()

    def set_tool(self, tool):
        """Arming a DRAWING tool starts a new object; the old one is replaced,
        which is what a one-object overlay means. Re-pressing the armed tool
        disarms it, so a button can be got out of."""
        if tool == self.tool:
            self.tool = TOOL_SELECT
            self.defining = False
            self._rubber = None
            return
        self.tool = tool
        self._rubber = None
        if tool in TOOL_KIND:
            self.defining = False    # the first click starts the new path
            self.selected = None

    # --- gesture plumbing ---------------------------------------------------

    def _finish_defining(self):
        self.defining = False
        self._rubber = None
        self.tool = TOOL_SELECT
        self._notify()

    def _end_gesture(self):
        self._drag = None
        if self.manager is not None:
            try:
                self.manager.release_mouse()
            except pyfvw.FvError:
                pass

    def _unproject(self, x, y):
        try:
            return self._proj.surface_to_geo(x, y)
        except (pyfvw.FvError, AttributeError):
            return None

    def _grab(self, x, y):
        """What a press at (x, y) takes hold of: a vertex first, an observer
        second. Vertices win because they are what the user is refining and
        an observer's ring is a much bigger target."""
        best, best_d = None, HIT_PX + HANDLE_PX + 1.0
        for i, xy in enumerate(self._project(self._proj, self.points)):
            if xy is None:
                continue
            d = math.hypot(xy[0] - x, xy[1] - y)
            if d < best_d:
                best, best_d = ("vertex", i), d
        if best is not None:
            return best
        for i, o in enumerate(self.observers):
            xy = self._project(self._proj, [o.at])[0]
            if xy is None:
                continue
            d = math.hypot(xy[0] - x, xy[1] - y)
            if d < best_d:
                best, best_d = ("observer", i), d
        return best


def _label(draw, x, y, text, size):
    """One haloed string, and a MISSING FONT IS NOT A DRAWING FAILURE. A shell
    that never called `set_default_font` — a test, a `--shot` on a machine with
    no host font — would otherwise have the whole overlay stack come back as an
    FvError because a measurement wanted to say how long it was."""
    if not text:
        return
    try:
        draw.label_at_pixel(x, y, text, LABEL_COLOR, size=size,
                            halo_color=LABEL_HALO, halo_width=2.0)
    except pyfvw.FvError:
        pass


def _runs2d(mask):
    """(row, start, end_exclusive) for every horizontal run of True, as three
    parallel arrays — the whole 2-D mask in one numpy pass rather than a loop
    over rows.

    The padding column on each side is what makes a run touching an edge come
    out of the same `diff` as every other one; `nonzero` walks row-major, so
    the k-th start and the k-th end are the same run and no sorting is needed.
    """
    import numpy as np
    m = np.ascontiguousarray(mask, dtype=np.int8)
    rows, cols = m.shape
    pad = np.zeros((rows, cols + 2), np.int8)
    pad[:, 1:-1] = m
    d = np.diff(pad, axis=1)
    r0, c0 = np.nonzero(d == 1)
    _r1, c1 = np.nonzero(d == -1)
    return r0, c0, c1


def _ring_budget(grid, predicate, floor, budget):
    """(stride, rows, starts, ends) — the runs at the COARSEST stride that is
    still at least `floor`, decimated further until there are no more than
    `budget` of them.

    THE BUDGET IS ON THE RING COUNT AND NOT ON THE CELL SIZE, because the ring
    count is what costs: a few thousand small polygons cross the binding one
    Python list at a time, and that crossing — not the rasterizer, and not the
    terrain — is where a viewshed's frame time goes (measured: ~5 us a ring).
    How many runs a mask breaks into depends on the ground, so it cannot be
    predicted from the geometry and has to be counted.
    """
    stride = max(1, int(floor))
    for _ in range(6):
        sub = grid[::stride, ::stride]
        if sub.shape[0] < 2 or sub.shape[1] < 2:
            return (None, None, None, None)
        r0, c0, c1 = _runs2d(predicate(sub))
        if r0.size <= budget or stride >= grid.shape[0] // 4:
            return (stride, r0, c0, c1)
        stride *= 2
    return (stride, r0, c0, c1)


# ---------------------------------------------------------------------------
# AN6 — the profile window
# ---------------------------------------------------------------------------

class ProfileWindow:
    """The terrain profile as a tk chart: distance across, elevation up.

    It replaces `Elevation_Chart` plus the `2DLineGraph` control — 3k lines of
    MFC that are out of scope by name in the plan — and it is deliberately not
    a transcription of them. Two behaviours are different on purpose, and both
    are AN2's decisions rather than this window's: a HOLE IN THE DATA IS A GAP
    IN THE LINE and not a refusal to draw (Windows popped "No data available."
    and drew nothing at all), and the samples are walked along the PATH, so a
    polyline and a route profile are the same call as a straight line.

    `path_provider` is a callable, not a path: the chart is opened over
    whatever the user is editing and re-reads it, so refining the line under it
    redraws it.
    """

    WIDTH, HEIGHT = 720, 340
    PAD_L, PAD_R, PAD_T, PAD_B = 62, 14, 12, 34

    def __init__(self, parent, path_provider, elevation, style=None,
                 title="Terrain Profile", samples=200, on_close=None):
        import tkinter as tk
        from tkinter import ttk
        self._tk = tk
        self.path_provider = path_provider
        self.elevation = elevation
        self.style = style or an.MeasureStyle()
        self.on_close = on_close
        self.result = None
        self._error = ""

        self.win = tk.Toplevel(parent)
        self.win.title(title)
        self.win.minsize(420, 240)
        self.win.protocol("WM_DELETE_WINDOW", self.close)

        bar = ttk.Frame(self.win)
        bar.pack(side="top", fill="x")
        ttk.Label(bar, text="Segments:").pack(side="left", padx=(8, 2), pady=4)
        self.samples = tk.IntVar(value=samples)
        spin = ttk.Spinbox(bar, from_=3, to=2000, width=6,
                           textvariable=self.samples,
                           command=self.refresh)
        spin.pack(side="left", pady=4)
        spin.bind("<Return>", lambda _e: self.refresh())
        ttk.Button(bar, text="Refresh", command=self.refresh).pack(
            side="left", padx=6)
        ttk.Button(bar, text="Close", command=self.close).pack(
            side="right", padx=8)

        self.stats = tk.Label(self.win, anchor="w", font=("Menlo", 10))
        self.stats.pack(side="bottom", fill="x")
        self.readout = tk.Label(self.win, anchor="w", font=("Menlo", 10))
        self.readout.pack(side="bottom", fill="x")

        self.canvas = tk.Canvas(self.win, width=self.WIDTH, height=self.HEIGHT,
                                background="#ffffff", highlightthickness=0)
        self.canvas.pack(side="top", fill="both", expand=True)
        self.canvas.bind("<Configure>", lambda _e: self._redraw())
        self.canvas.bind("<Motion>", self._on_motion)
        self.canvas.bind("<Leave>", lambda _e: self.readout.config(text=""))

        self.refresh()

    # --- lifecycle ----------------------------------------------------------

    @property
    def alive(self):
        try:
            return bool(self.win.winfo_exists())
        except Exception:                            # noqa: BLE001
            return False

    def close(self):
        if self.on_close is not None:
            self.on_close(self)
        try:
            self.win.destroy()
        except Exception:                            # noqa: BLE001
            pass

    def lift(self):
        try:
            self.win.lift()
        except Exception:                            # noqa: BLE001
            pass

    # --- the sampling -------------------------------------------------------

    def refresh(self):
        """Re-sample and redraw. Cheap enough to call whenever the path moves:
        AN2 costs N elevation reads for N samples."""
        if not self.alive:
            return
        self.result, self._error = sample_profile(
            self.elevation, self.path_provider(), self.samples.get())
        self._redraw()

    # --- drawing ------------------------------------------------------------

    def _plot_box(self):
        w = max(self.canvas.winfo_width(), 200)
        h = max(self.canvas.winfo_height(), 140)
        return (self.PAD_L, self.PAD_T, w - self.PAD_R, h - self.PAD_B)

    def _scales(self):
        """(x0, y0, x1, y1, total_in_units, lo, hi) — the plot box and the two
        axis ranges, in DISPLAY units. Returns None when there is nothing to
        draw."""
        r = self.result
        if r is None or r.empty:
            return None
        x0, y0, x1, y1 = self._plot_box()
        total = an.convert_range(r.total_distance_m, self.style.units)
        if total <= 0.0:
            return None
        _, per_m = elevation_unit(self.style)
        lo, hi = elevation_window(r.min_m, r.max_m, r.total_distance_m, per_m)
        return (x0, y0, x1, y1, total, lo, hi)

    def _redraw(self):
        c = self.canvas
        c.delete("all")
        s = self._scales()
        if s is None:
            x0, y0, x1, y1 = self._plot_box()
            msg = self._error or "Nothing to profile — draw a line first."
            c.create_text((x0 + x1) // 2, (y0 + y1) // 2, text=msg,
                          fill="#777777")
            self.stats.config(text="")
            return
        x0, y0, x1, y1, total, lo, hi = s
        r = self.result
        _, per_m = elevation_unit(self.style)

        def sx(d_m):
            return x0 + (x1 - x0) * (an.convert_range(d_m, self.style.units) /
                                     total)

        def sy(e_m):
            return y1 - (y1 - y0) * ((e_m * per_m - lo) / (hi - lo))

        self._grid(c, x0, y0, x1, y1, total, lo, hi)

        # THE TERRAIN, in contiguous runs. A NaN sample ends the run and the
        # next known sample starts a new one, so a hole is a hole and not a
        # straight line drawn across ground nobody has measured.
        for run in _profile_runs(r.points):
            xs = [(sx(p.distance_m), sy(p.elevation_m)) for p in run]
            if len(xs) >= 2:
                poly = [(xs[0][0], y1)] + xs + [(xs[-1][0], y1)]
                c.create_polygon([v for pt in poly for v in pt],
                                 fill="#c9ddc0", outline="")
                c.create_line([v for pt in xs for v in pt],
                              fill="#2f6b2f", width=2)
            elif xs:
                c.create_oval(xs[0][0] - 2, xs[0][1] - 2,
                              xs[0][0] + 2, xs[0][1] + 2, fill="#2f6b2f")

        # The turning points. A polyline or a route profile that does not show
        # where its legs meet is a chart you cannot read a waypoint off.
        for p in r.points:
            if p.is_vertex and 0.0 < p.distance_m < r.total_distance_m:
                c.create_line(sx(p.distance_m), y0, sx(p.distance_m), y1,
                              fill="#b0b0b0", dash=(2, 3))

        c.create_rectangle(x0, y0, x1, y1, outline="#888888")
        self.stats.config(text=self._stats_text())

    def _grid(self, c, x0, y0, x1, y1, total, lo, hi):
        eu, _ = elevation_unit(self.style)
        du = an.range_unit_name(self.style.units)
        for i in range(6):
            v = lo + (hi - lo) * i / 5.0
            y = y1 - (y1 - y0) * i / 5.0
            c.create_line(x0, y, x1, y, fill="#e6e6e6")
            c.create_text(x0 - 6, y, anchor="e", text=f"{v:,.0f} {eu}",
                          font=("Menlo", 9), fill="#444444")
        for i in range(6):
            v = total * i / 5.0
            x = x0 + (x1 - x0) * i / 5.0
            c.create_line(x, y0, x, y1, fill="#e6e6e6")
            c.create_text(x, y1 + 6, anchor="n",
                          text=f"{v:,.1f} {du}", font=("Menlo", 9),
                          fill="#444444")

    def _stats_text(self):
        r = self.result
        if r is None:
            return ""
        eu, per_m = elevation_unit(self.style)
        parts = [
            f"{an.format_range(r.total_distance_m, self.style, 2)}",
            f"min {r.min_m * per_m:,.0f} {eu}",
            f"max {r.max_m * per_m:,.0f} {eu}",
            f"gain {r.gain_m * per_m:,.0f} {eu}",
            f"loss {r.loss_m * per_m:,.0f} {eu}",
            f"{len(r.points)} samples",
        ]
        if r.no_data_count:
            parts.append(f"{r.no_data_count} no data")
        if r.step_was_clamped:
            parts.append("step clamped to the posts")
        return "  ·  ".join(parts)

    def _on_motion(self, ev):
        s = self._scales()
        if s is None:
            return
        x0, y0, x1, y1, total, lo, hi = s
        if not (x0 <= ev.x <= x1):
            self.readout.config(text="")
            return
        r = self.result
        d_m = r.total_distance_m * (ev.x - x0) / max(1, (x1 - x0))
        # Nearest sample, not an interpolation: the chart should read back the
        # value it drew.
        p = min(r.points, key=lambda q: abs(q.distance_m - d_m))
        eu, per_m = elevation_unit(self.style)
        e = (f"{p.elevation_m * per_m:,.0f} {eu}" if p.has_data else "no data")
        self.readout.config(
            text=f"  {an.format_range(p.distance_m, self.style, 2)}   {e}"
                 f"   {p.at.lat:.5f}, {p.at.lon:.5f}")
        self.canvas.delete("cursor")
        self.canvas.create_line(ev.x, y0, ev.x, y1, fill="#3070c0",
                                tags="cursor")


# ---------------------------------------------------------------------------
# The pieces a shell and a test both want, with no tk in them
# ---------------------------------------------------------------------------

def elevation_unit(style):
    """(name, multiplier from metres). FalconView's elevation chart is in feet
    and its distance is in whatever the object's units say; metric units get a
    metric chart, which is the one thing that would otherwise be absurd."""
    metric = style.units in (an.RangeUnit.KILOMETERS, an.RangeUnit.METERS)
    return ("m", 1.0) if metric else ("ft", 1.0 / 0.3048)


# The steepest grade the chart ever draws at full height, and so the CEILING
# on its vertical scale. Terrain is not a stock price: 5 m of rise over a
# kilometre is a road a cyclist calls flat, and a chart that stretches that to
# fill the window says "hilly" about level ground — Kiawah, every time. Under
# this rule that half a per cent occupies a quarter of the frame, 2% fills it,
# and anything steeper goes back to fitting the data, because a mountain
# SHOULD fill the chart.
FULL_SCALE_GRADE = 0.02


def elevation_window(min_m, max_m, total_distance_m, per_m):
    """(lo, hi) for the elevation axis, in DISPLAY units.

    The data range plus a margin, except that the window never spans less than
    `FULL_SCALE_GRADE` of the path's own length. The extra room is split above
    and below, so a flat profile sits across the middle rather than being
    pinned to an edge."""
    lo, hi = min_m * per_m, max_m * per_m
    if not (hi > lo):
        hi, lo = lo + 1.0, lo - 1.0
    pad = (hi - lo) * 0.08
    lo, hi = lo - pad, hi + pad
    floor = max(0.0, total_distance_m) * FULL_SCALE_GRADE * per_m
    if (hi - lo) < floor:
        mid = (lo + hi) * 0.5
        lo, hi = mid - floor * 0.5, mid + floor * 0.5
    return (lo, hi)


def sample_profile(source, path, samples):
    """(ProfileResult|None, error). Every refusal a chart can meet, answered
    once so the window and a test agree about what "nothing to draw" is."""
    if path is None or path.point_count < 2:
        return (None, "")
    if source is None:
        return (None, "No elevation data is open — add a DTED source.")
    opts = an.ProfileOptions()
    opts.sample_count = max(3, int(samples))
    try:
        return (an.sample_terrain_profile(source, path, opts), "")
    except pyfvw.FvError as e:
        return (None, e.message)


def _profile_runs(points):
    """The samples split into contiguous runs of known ground."""
    runs, cur = [], []
    for p in points:
        if p.has_data:
            cur.append(p)
        elif cur:
            runs.append(cur)
            cur = []
    if cur:
        runs.append(cur)
    return runs


def route_geo_path(route):
    """A route as an `an.GeoPath` — AN6's second way in, and the one Chris
    asked for.

    IT FOLLOWS THE PLAN WHEN THERE IS ONE. A route that follows roads is a
    polyline of hundreds of points and the waypoints are three of them; taking
    the waypoints would profile a straight line over ground the route never
    touches. `plan.legs` are the per-leg polylines in order, and consecutive
    legs share their join, so the first point of every leg after the first is
    dropped."""
    if route is None:
        return None
    pts = []
    if getattr(route, "has_plan", False):
        for leg in route.plan.legs:
            pts.extend(list(leg)[1:] if pts else list(leg))
    if not pts:
        pts = [pyfvw.geo.GeoPoint(w.lat, w.lon) for w in route.waypoints]
    if len(pts) < 2:
        return None
    kind = getattr(route, "leg_kind", None)
    line = (an.LineType.RHUMB if kind == pyfvw.geo.LineKind.RHUMB
            else an.LineType.GREAT_CIRCLE)
    return an.GeoPath(pts, line)


# ---------------------------------------------------------------------------
# The editor — a palette and nothing else
# ---------------------------------------------------------------------------

class AnalysisEditor:
    """The ANALYSIS TOOL. One per type, whatever is on the map, exactly as
    `RouteEditor` is — "I am measuring" is a statement about the user.

    It holds no editing state: every button below reads the overlay and calls
    into it, which is what leaves the same overlay usable from a shell with no
    toolbar at all."""

    def __init__(self, app_window=None):
        self.app = app_window
        self.active = False

    def activate(self):
        self.active = True

    def deactivate(self):
        self.active = False

    def default_cursor(self):
        return app.CursorId.CROSSHAIR

    def ui_constraints(self):
        return app.EditorUiConstraints()

    def auto_enter_on_create(self):
        return True

    # --- the palette --------------------------------------------------------

    def tools(self):
        o = self._overlay()
        n = len(o.points) if o is not None else 0
        has_path = n >= 2
        M = app.MenuNode

        def tool_button(label, tool):
            return M(label=label,
                     checked=bool(o is not None and o.tool == tool),
                     enabled=o is not None,
                     action=lambda: o is not None and o.set_tool(tool))

        return [
            tool_button("Line", TOOL_LINE),
            tool_button("Polyline", TOOL_POLYLINE),
            tool_button("Area", TOOL_AREA),
            M(label=""),
            tool_button("Viewshed", TOOL_VIEWSHED),
            M(label="Profile...", enabled=has_path, action=self.show_profile),
            M(label=""),
            M(label=("Rhumb" if o is not None and
                     o.line_type == an.LineType.RHUMB else "Great circle"),
              enabled=has_path,
              action=lambda: o is not None and o.cycle_line_type()),
            M(label="Delete point",
              enabled=bool(o is not None and o.selected is not None),
              action=lambda: o is not None and o.delete_selected()),
            M(label="Undo",
              enabled=bool(o is not None and o.can_undo()),
              action=lambda: o is not None and o.undo()),
            M(label="Clear",
              enabled=bool(o is not None and (n or o.observers)),
              action=self._clear),
        ]

    # --- what the buttons act on -------------------------------------------

    def _overlay(self):
        """The overlay being edited. Asked of the EditorManager rather than
        remembered, so the palette is right when the user makes a different
        overlay current."""
        if self.app is None or getattr(self.app, "editors", None) is None:
            return None
        edited = self.app.editors.edited
        return edited if isinstance(edited, AnalysisOverlay) else None

    def _clear(self):
        o = self._overlay()
        if o is None:
            return
        o.clear()
        o.clear_viewsheds()

    def show_profile(self):
        o = self._overlay()
        if o is None or self.app is None:
            return
        opener = getattr(self.app, "open_profile_window", None)
        if opener is None:
            return
        opener(o.geo_path, title="Terrain Profile — measurement",
               style=o.style, samples=o.profile_samples)


def analysis_type_desc(factory, editor_factory):
    """The registered type. STATIC (no `file`), so toggling it on and off is
    the whole of its document lifecycle — see ANALYSIS_TYPE_ID."""
    return app.OverlayTypeDesc(
        id=ANALYSIS_TYPE_ID,
        display_name="Analysis",
        icon="analysis",
        factory=factory,
        editor_factory=editor_factory,
        # Above the route (1000): a measurement is drawn ON what it measures.
        default_display_order=1100)
