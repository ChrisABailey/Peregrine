# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""Viewshed observers and the quads that draw a computed viewshed.

The computation is `pyfvw.analysis.compute_viewshed` on a worker thread; it
releases the GIL, so the map keeps drawing while it runs. The picture is drawn as one
even-odd `fill_polygon` of horizontal runs of visible posts, placed with the
affine map three `geo_to_surface` calls define (the projections are equal-arc
plus a rotation about the centre).
"""

import threading

import pyfvw

an = pyfvw.analysis
GeoPoint = pyfvw.geo.GeoPoint

VISIBLE_COLOR = (40, 200, 60, 110)
MASKED_COLOR = (200, 40, 40, 80)


class Observer:
    """One viewshed: where, how high, how far, and the last result.

    `start` computes on a worker thread that is the only user of the source
    it is given (elevation sources make no thread-safety promise). A later
    `start` or `cancel` supersedes a running job, whose answer is then dropped.
    """

    def __init__(self, at, height_m, range_m):
        self.at = at
        self.height_m = height_m
        self.range_m = range_m
        self.result = None
        self.error = ""
        self._job = 0
        self._thread = None

    def to_json(self):
        return {"lat": self.at.lat, "lon": self.at.lon,
                "height_m": self.height_m, "range_m": self.range_m}

    @staticmethod
    def from_json(d):
        return Observer(GeoPoint(float(d["lat"]), float(d["lon"])),
                        float(d.get("height_m", 30.0)), float(d.get("range_m", 25000.0)))

    @property
    def running(self):
        t = self._thread
        return t is not None and t.is_alive()

    def start(self, source, max_posts, done=None):
        """Computes over `source` in the background; the job takes it over.

        `done()` runs on the worker after the result (or `error`) is set,
        unless the job was superseded first.
        """
        self._job += 1
        job = self._job
        self.result = None
        self.error = ""
        if source is None:
            self.error = "no elevation data"
            self._thread = None
            return
        self._thread = threading.Thread(
            target=self._run, args=(job, source, self.at, max_posts, done), daemon=True)
        self._thread.start()

    def cancel(self):
        """Supersedes any running job without waiting for it."""
        self._job += 1

    def wait(self, timeout=None):
        """Blocks until the running job, if any, has finished."""
        t = self._thread
        if t is not None:
            t.join(timeout)

    def _run(self, job, source, at, max_posts, done):
        result, error = None, ""
        try:
            step = an.viewshed_step_from_post_spacing(source, at)
            if step <= 0.0:
                step = 1.0 / 3600.0  # one arcsecond: DTED level 2 spacing
            req = an.ViewshedRequest()
            req.observer = at
            req.observer_height_m = self.height_m
            req.range_m = self.range_m
            req.step_deg = step
            req.method = an.HeightMethod.INTERPOLATED
            if max_posts > 0:
                req.max_posts = max_posts
            # The callback must return a bool: False cancels, None does not.
            result = an.compute_viewshed(source, req, lambda _pct: self._job == job)
        except pyfvw.FvError as e:
            error = e.message
        except Exception as e:  # noqa: BLE001 - a worker has nowhere to raise to
            error = str(e)
        if self._job != job:
            return
        self.result, self.error = result, error
        if done is not None:
            done()


def draw_result(proj, canvas, result, show_masked, budget):
    """Fills the visible (and optionally masked) ground of `result`.

    Needs numpy; without it only the observer's ring is drawn. `budget` caps
    the quads per band, since crossing the binding is the cost per frame.
    """
    try:
        import numpy as np
    except ImportError:
        return
    if result is None or not result.valid or result.span < 2:
        return
    b = result.bounds
    span = result.span
    try:
        x00, y00 = proj.geo_to_surface(GeoPoint(b.ur.lat, b.ll.lon))
        x01, y01 = proj.geo_to_surface(GeoPoint(b.ur.lat, b.ur.lon))
        x10, y10 = proj.geo_to_surface(GeoPoint(b.ll.lat, b.ll.lon))
    except pyfvw.FvError:
        return
    n = span - 1
    ux, uy = (x01 - x00) / n, (y01 - y00) / n  # one column east
    vx, vy = (x10 - x00) / n, (y10 - y00) / n  # one row south
    span_px = max(abs(x01 - x00) + abs(x10 - x00), abs(y01 - y00) + abs(y10 - y00))
    floor = _screen_stride(span, span_px)

    grid = np.asarray(result)
    bands = [(lambda g: g == 0.0, VISIBLE_COLOR)]
    if show_masked:
        bands.insert(0, (lambda g: np.isfinite(g) & (g != 0.0), MASKED_COLOR))
    for predicate, color in bands:
        stride, rows, c0, c1 = _runs_within_budget(np, grid, predicate, floor, budget)
        if stride is None or rows.size == 0:
            continue
        a = c0 * stride
        e = np.minimum(c1 * stride, n)
        t = rows * stride
        u = np.minimum((rows + 1) * stride, n)
        xs = np.column_stack([x00 + a * ux + t * vx, x00 + e * ux + t * vx,
                              x00 + e * ux + u * vx, x00 + a * ux + u * vx])
        ys = np.column_stack([y00 + a * uy + t * vy, y00 + e * uy + t * vy,
                              y00 + e * uy + u * vy, y00 + a * uy + u * vy])
        rings = [list(zip(xr, yr)) for xr, yr in
                 zip(xs.astype(int).tolist(), ys.astype(int).tolist())]
        # The runs are disjoint, so an even-odd fill of all of them is their union.
        canvas.fill_polygon(rings, fill=color)


def _screen_stride(span, span_px, target_px=2.0):
    """Posts per drawn cell so that a cell is about `target_px` on screen."""
    if span <= 0 or span_px <= 0:
        return 1
    cells = max(8, min(span, int(round(span_px / target_px))))
    return max(1, span // cells)


def _runs(np, mask):
    """(row, start, end_exclusive) arrays for every horizontal run of True."""
    m = np.ascontiguousarray(mask, dtype=np.int8)
    rows, cols = m.shape
    pad = np.zeros((rows, cols + 2), np.int8)
    pad[:, 1:-1] = m
    d = np.diff(pad, axis=1)
    r0, c0 = np.nonzero(d == 1)
    _r1, c1 = np.nonzero(d == -1)
    return r0, c0, c1


def _runs_within_budget(np, grid, predicate, floor, budget):
    """The runs at the finest stride >= `floor` that yields <= `budget` of them."""
    stride = max(1, int(floor))
    r0 = c0 = c1 = None
    for _ in range(6):
        sub = grid[::stride, ::stride]
        if sub.shape[0] < 2 or sub.shape[1] < 2:
            return (None, None, None, None)
        r0, c0, c1 = _runs(np, predicate(sub))
        if r0.size <= budget or stride >= grid.shape[0] // 4:
            break
        stride *= 2
    return (stride, r0, c0, c1)
