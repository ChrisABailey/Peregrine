# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""The terrain profile along the measured path, drawn as an inset chart.

Sampling happens on the UI thread when the path or the elevation data
changes; drawing only reads the cached `ProfileResult`, so the render worker
never touches the elevation source.
"""

import pyfvw

an = pyfvw.analysis

#: The steepest grade drawn at full chart height. A flatter profile keeps a
#: proportionally shorter vertical span, so level ground does not look hilly.
FULL_SCALE_GRADE = 0.02

BOX_FILL = (255, 255, 255, 215)
BOX_EDGE = (120, 120, 120, 255)
GROUND_FILL = (201, 221, 192, 255)
GROUND_LINE = (47, 107, 47, 255)
GRID_LINE = (225, 225, 225, 255)
VERTEX_LINE = (170, 170, 170, 255)
TEXT = (40, 40, 40, 255)
HALO = (255, 255, 255, 255)


def sample(source, path, samples):
    """(ProfileResult or None, message). The message explains a missing chart."""
    if path is None or path.point_count < 2:
        return (None, "")
    if source is None:
        return (None, "No elevation data in the map data catalog")
    opts = an.ProfileOptions()
    opts.sample_count = max(3, int(samples))
    try:
        return (an.sample_terrain_profile(source, path, opts), "")
    except pyfvw.FvError as e:
        return (None, e.message)


def elevation_unit(style):
    """(name, multiplier from metres): metres for metric range units, else feet."""
    metric = style.units in (an.RangeUnit.KILOMETERS, an.RangeUnit.METERS)
    return ("m", 1.0) if metric else ("ft", 1.0 / 0.3048)


def elevation_window(min_m, max_m, total_distance_m, per_m):
    """(lo, hi) of the elevation axis in display units, never flatter than
    `FULL_SCALE_GRADE` of the path length and centred on the data."""
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


def runs(points):
    """The samples split into contiguous runs with data; a hole ends a run."""
    out, cur = [], []
    for p in points:
        if p.has_data:
            cur.append(p)
        elif cur:
            out.append(cur)
            cur = []
    if cur:
        out.append(cur)
    return out


def draw_inset(proj, canvas, gd, result, message, style, label):
    """Draws the chart in the lower-left corner of the surface.

    `label` is the overlay's haloed-text helper, which tolerates a canvas
    with no font.
    """
    w, h = proj.surface_size
    k = _ui_scale(proj)
    bw = int(min(380 * k, w - 20 * k))
    bh = int(130 * k)
    if bw < 120 or h < bh + 40 * k:
        return
    x0 = int(10 * k)
    y0 = int(h - bh - 10 * k)
    canvas.fill_polygon([[(x0, y0), (x0 + bw, y0), (x0 + bw, y0 + bh), (x0, y0 + bh)]],
                        fill=BOX_FILL, outline=BOX_EDGE, outline_width=1)
    if result is None or result.empty:
        label(gd, x0 + 8 * k, y0 + bh / 2, message or "Profile: measure a path", 11.0 * k)
        return

    eu, per_m = elevation_unit(style)
    lo, hi = elevation_window(result.min_m, result.max_m, result.total_distance_m, per_m)
    total = result.total_distance_m
    if total <= 0.0:
        return
    px0, px1 = x0 + int(46 * k), x0 + bw - int(8 * k)
    py0, py1 = y0 + int(8 * k), y0 + bh - int(24 * k)

    def sx(d_m):
        return int(px0 + (px1 - px0) * (d_m / total))

    def sy(e_m):
        return int(py1 - (py1 - py0) * ((e_m * per_m - lo) / (hi - lo)))

    for i in range(5):
        y = int(py1 - (py1 - py0) * i / 4.0)
        canvas.draw_lines([(px0, y), (px1, y)], GRID_LINE, 1)
        label(gd, x0 + 4 * k, y + 4 * k, f"{lo + (hi - lo) * i / 4.0:,.0f}", 9.0 * k)

    for p in result.points:
        if p.is_vertex and 0.0 < p.distance_m < total:
            canvas.draw_lines([(sx(p.distance_m), py0), (sx(p.distance_m), py1)],
                              VERTEX_LINE, 1, [3.0, 3.0])

    for run in runs(result.points):
        line = [(sx(p.distance_m), sy(p.elevation_m)) for p in run]
        if len(line) >= 2:
            ring = [(line[0][0], py1)] + line + [(line[-1][0], py1)]
            canvas.fill_polygon([ring], fill=GROUND_FILL)
            canvas.draw_lines(line, GROUND_LINE, max(1, int(2 * k)))

    canvas.draw_lines([(px0, py0), (px1, py0), (px1, py1), (px0, py1), (px0, py0)], BOX_EDGE, 1)
    parts = [an.format_range(total, style, 2),
             f"min {result.min_m * per_m:,.0f} {eu}",
             f"max {result.max_m * per_m:,.0f} {eu}",
             f"gain {result.gain_m * per_m:,.0f}",
             f"loss {result.loss_m * per_m:,.0f}"]
    if result.no_data_count:
        parts.append(f"{result.no_data_count} no data")
    label(gd, px0, y0 + bh - 8 * k, "  ".join(parts), 10.0 * k)


def _ui_scale(proj):
    """Pixels per 96-dpi pixel, from the projection's device pixel size."""
    try:
        mm = proj.mm_per_pixel
        return max(1.0, min(4.0, 0.2646 / mm)) if mm > 0 else 1.0
    except (AttributeError, pyfvw.FvError):
        return 1.0
