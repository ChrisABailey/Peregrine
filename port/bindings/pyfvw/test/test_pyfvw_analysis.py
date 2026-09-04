# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""pyfvw.analysis — the Analysis tools through the Python surface (AN5).

Not a translation of the C++ tests. AN1-AN4 already have 61 gtests that pin
the geodesy, the sampling, the XDraw recurrence and every formatted label;
repeating them here would only test pybind11. What is testable ONLY here is
the seam:

  * a path and a measurement really are values a Python UI can hold, and the
    `points` property really is a copy rather than a window onto C++ memory;
  * the labels survive the crossing as UTF-8 — the degree sign is the one
    character in this whole subsystem that a bad encoding would eat;
  * the viewshed result is a ZERO-COPY buffer, laid out north-row-first, so a
    million posts do not become a million boxed Python floats;
  * the GIL is really released for the computation — a second Python thread
    keeps running while a viewshed is in flight, which is the whole reason
    AN5 was worth doing before AN7;
  * a progress callback that returns False cancels, and one that RAISES
    surfaces its own exception rather than an FvError.

Run via ctest (pyfvw_pytest) or:
  PYTHONPATH=build/port/bindings/pyfvw pytest -q port/bindings/pyfvw/test
"""

import math
import os
import threading
import time

import pytest

import pyfvw

an = pyfvw.analysis
geo = pyfvw.geo


def _testdata(sub):
    d = os.environ.get("FVW_TESTDATA_DIR")
    if not d:
        return None
    p = os.path.join(d, sub)
    return p if os.path.isdir(p) else None


def dted():
    root = _testdata("dted")
    if root is None:
        pytest.skip("no TestData")
    return pyfvw.formats.DtedElevationSource(root)


# A degree of latitude in the Georgia DTED, which every real-data test below
# stands on.
GA = geo.GeoPoint(31.5, -81.5)


def north_leg():
    return an.GeoPath([geo.GeoPoint(31.0, -81.5), geo.GeoPoint(32.0, -81.5)])


# ---------------------------------------------------------------------------
# AN1 — the path as a Python value
# ---------------------------------------------------------------------------

def test_path_is_a_value_and_points_is_a_copy():
    p = north_leg()
    assert len(p) == 2
    assert p.point_count == 2 and p.leg_count == 1
    assert p.measured
    assert p.line_type == an.LineType.GREAT_CIRCLE
    # 60 nautical miles to the degree, on the ellipsoid.
    assert p.total_length_m == pytest.approx(60 * 1852.0, rel=0.01)
    assert p.leg(0).ok
    assert p.leg(0).bearing_deg == pytest.approx(0.0, abs=1e-6)
    assert p.cumulative_at(0) == 0.0
    assert p.cumulative_at(1) == p.total_length_m

    # The property hands out a COPY. Mutating it must not reach the path --
    # the C++ accessor returns a reference and a Python list aliasing it would
    # go stale (or worse) the moment anyone called set_points.
    pts = p.points
    pts.append(geo.GeoPoint(33.0, -81.5))
    assert p.point_count == 2

    # ... and assigning the property really does re-measure.
    p.points = pts
    assert p.point_count == 3
    assert p.total_length_m == pytest.approx(120 * 1852.0, rel=0.01)


def test_line_type_is_a_property_of_the_path():
    a = an.GeoPath([geo.GeoPoint(60.0, -100.0), geo.GeoPoint(60.0, 0.0)],
                   an.LineType.GREAT_CIRCLE)
    b = an.GeoPath([geo.GeoPoint(60.0, -100.0), geo.GeoPoint(60.0, 0.0)],
                   an.LineType.RHUMB)
    assert a.total_length_m < b.total_length_m
    # The rhumb line along a parallel bears due east; the great circle starts
    # well north of east.
    assert b.leg(0).bearing_deg == pytest.approx(90.0, abs=1e-6)
    assert a.leg(0).bearing_deg < 90.0

    # Flipping the line type re-measures in place.
    a.line_type = an.LineType.RHUMB
    assert a.total_length_m == pytest.approx(b.total_length_m)


def test_point_at_distance_densify_resample():
    p = north_leg()
    mid = p.point_at_distance(p.total_length_m / 2.0)
    assert mid.lat == pytest.approx(31.5, abs=0.01)

    # Distances outside the path clamp rather than raise.
    assert p.point_at_distance(-1000.0).lat == pytest.approx(31.0)
    assert p.point_at_distance(1e9).lat == pytest.approx(32.0)

    d = p.densify(10000.0)
    assert len(d) > 10
    assert d[0].lat == pytest.approx(31.0) and d[-1].lat == pytest.approx(32.0)

    r = p.resample(11)
    assert len(r) == 11
    assert r[0].lat == pytest.approx(31.0) and r[-1].lat == pytest.approx(32.0)

    with pytest.raises(pyfvw.FvError):
        an.GeoPath().point_at_distance(0.0)


def test_area_is_the_shoelace():
    box = an.GeoPath([geo.GeoPoint(0.0, 0.0), geo.GeoPoint(1.0, 0.0),
                      geo.GeoPoint(1.0, 1.0), geo.GeoPoint(0.0, 1.0)])
    sq_nm = an.convert_area(box.area_square_meters, an.RangeUnit.NAUTICAL_MILES)
    assert sq_nm == pytest.approx(3600.0, rel=0.02)
    # Fewer than three vertices is zero, not an error.
    assert north_leg().area_square_meters == 0.0


# ---------------------------------------------------------------------------
# AN4 — the labels, and the one character that encoding gets wrong
# ---------------------------------------------------------------------------

def test_labels_cross_as_utf8():
    m = an.Measurement(an.MeasurementKind.RANGE_BEARING, north_leg())
    label = m.leg_label(0)
    assert isinstance(label, str)
    assert an.DEGREE_SIGN == "°"
    assert an.DEGREE_SIGN in label
    # The padding spaces are FalconView's own and are part of the label.
    assert label.startswith(" ") and label.endswith(" ")
    assert "000.0°T / " in label and " NM " in label
    assert m.summary_label == label
    assert m.labels == [label]


def test_style_reaches_the_label():
    m = an.Measurement(an.MeasurementKind.RANGE_BEARING, north_leg())
    s = m.style
    s.units = an.RangeUnit.FEET
    s.angle_units = an.AngleUnit.MILS
    m.style = s
    label = m.leg_label(0)
    assert "0000.0Mils T / " in label
    assert label.rstrip().endswith("ft")
    # The magnitude ladder is on the VALUE: a degree in feet is over 1000, so
    # no decimals at all.
    assert "." not in label.split("/")[1]


def test_multipoint_labels_are_cumulative():
    p = an.GeoPath([geo.GeoPoint(31.0, -81.5), geo.GeoPoint(32.0, -81.5),
                    geo.GeoPoint(32.0, -80.5)])
    m = an.Measurement(an.MeasurementKind.MULTI_POINT, p)
    labels = m.labels
    assert len(labels) == 2
    values = [float(x.split()[0]) for x in labels]
    assert values[0] < values[1]
    assert labels[-1] == m.summary_label
    # Always two places, whatever the magnitude.
    assert all(x.split()[0].split(".")[1] == "00"[:2] or
               len(x.split()[0].split(".")[1]) == 2 for x in labels)

    total = an.Measurement(an.MeasurementKind.TOTAL_DISTANCE, p)
    assert total.summary_label.startswith(" Total Distance: ")

    area = an.Measurement(an.MeasurementKind.AREA, p)
    assert " sq NM" in area.summary_label


def test_unit_table_and_formatters():
    assert an.meters_per_unit(an.RangeUnit.NAUTICAL_MILES) == 1852.0
    assert an.meters_per_unit(an.RangeUnit.FEET) == 0.3048
    assert an.convert_range(1852.0, an.RangeUnit.NAUTICAL_MILES) == 1.0
    assert an.convert_area(1852.0 ** 2, an.RangeUnit.NAUTICAL_MILES) == \
        pytest.approx(1.0)
    assert an.range_unit_name(an.RangeUnit.NAUTICAL_MILES) == "NM"
    assert an.area_unit_name(an.RangeUnit.KILOMETERS) == "sq km"
    assert an.degrees_to_mils(360.0) == pytest.approx(6400.0)
    assert an.range_decimals(99.0) == 2
    assert an.range_decimals(1000.0) == 0

    style = an.MeasureStyle()
    assert an.format_bearing(45.0, style) == "045.0°"
    style.bearing_format = an.BearingFormat.DEGREES_MINUTES_SECONDS
    assert an.format_bearing(44.99999, style) == "044° 59' 59\""
    assert an.format_range(22853.02, style) == "12.34 NM"
    assert an.format_range(22853.02, style, 2) == "12.34 NM"


def test_magnetic_epoch_is_an_argument():
    epoch = an.MagneticEpoch(2020, 6)
    assert not epoch.is_now
    assert an.MagneticEpoch().is_now
    b = an.true_to_magnetic(0.0, GA, epoch)
    assert 0.0 <= b < 360.0
    # Georgia's declination is west, so true north reads east of north.
    assert 0.0 < b < 45.0

    style = an.MeasureStyle()
    style.bearing_reference = an.BearingReference.MAGNETIC
    style.epoch = epoch
    m = an.Measurement(an.MeasurementKind.RANGE_BEARING, north_leg(), style)
    assert "M / " in m.leg_label(0)
    assert m.leg(0).bearing_deg == pytest.approx(
        an.true_to_magnetic(0.0, geo.GeoPoint(31.0, -81.5), epoch))


# ---------------------------------------------------------------------------
# AN2 — the profile, over real DTED
# ---------------------------------------------------------------------------

def test_profile_over_real_terrain():
    src = dted()
    p = an.GeoPath([geo.GeoPoint(31.2, -81.8), geo.GeoPoint(31.8, -81.2)])
    opts = an.ProfileOptions()
    opts.sample_count = 25

    r = an.sample_terrain_profile(src, p, opts)
    assert len(r) == 25
    assert not r.empty
    assert r.total_distance_m == pytest.approx(p.total_length_m)
    assert r.points[0].is_vertex and r.points[-1].is_vertex
    assert r.min_m <= r.max_m
    assert r.gain_m >= 0.0 and r.loss_m >= 0.0

    # The two parallel arrays a chart wants, and they line up with `points`.
    assert len(r.distances_m) == len(r.elevations_m) == 25
    assert r.distances_m[0] == 0.0
    assert r.distances_m[-1] == pytest.approx(r.total_distance_m)
    for pt, d, e in zip(r.points, r.distances_m, r.elevations_m):
        assert d == pt.distance_m
        assert (math.isnan(e) and not pt.has_data) or e == pt.elevation_m


def test_profile_of_a_polyline_keeps_its_turning_points():
    src = dted()
    p = an.GeoPath([geo.GeoPoint(31.2, -81.8), geo.GeoPoint(31.8, -81.8),
                    geo.GeoPoint(31.8, -81.2)])
    opts = an.ProfileOptions()
    opts.step_m = 5000.0
    r = an.sample_terrain_profile(src, p, opts)
    vertices = [pt for pt in r.points if pt.is_vertex]
    assert len(vertices) == 3
    assert r.step_m > 0.0

    # A route is the same call with a different path, which is the whole point
    # of AN2 and the reason AN6 needs no C++.
    assert r.total_distance_m == pytest.approx(p.total_length_m)


def test_profile_outside_coverage_is_an_answer_not_a_failure():
    src = dted()
    p = an.GeoPath([geo.GeoPoint(5.0, 5.0), geo.GeoPoint(5.5, 5.5)])
    opts = an.ProfileOptions()
    opts.sample_count = 8
    r = an.sample_terrain_profile(src, p, opts)
    assert len(r) == 8
    assert r.no_data_count == 8
    assert all(math.isnan(e) for e in r.elevations_m)

    with pytest.raises(pyfvw.FvError):
        an.sample_terrain_profile(src, an.GeoPath(), opts)


# ---------------------------------------------------------------------------
# AN3 — the viewshed: the buffer, the GIL and the callback
# ---------------------------------------------------------------------------

def viewshed_request(range_m=8000.0, step_deg=None):
    src = dted()
    req = an.ViewshedRequest()
    req.observer = GA
    req.observer_height_m = 30.0
    req.range_m = range_m
    req.step_deg = step_deg if step_deg is not None else \
        an.viewshed_step_from_post_spacing(src, GA)
    assert req.step_deg > 0.0, "DTED should know its own post spacing"
    return src, req


def test_viewshed_result_is_a_zero_copy_buffer():
    np = pytest.importorskip("numpy")
    src, req = viewshed_request()
    r = an.compute_viewshed(src, req)
    assert r.valid and r.span > 0 and r.span % 2 == 1

    a = np.asarray(r)
    assert a.dtype == np.float32
    assert a.shape == (r.span, r.span)
    # Zero-copy: the array is a view onto the result's own storage.
    assert not a.flags["OWNDATA"]
    assert a[r.span // 2, r.span // 2] == r.at(r.span // 2, r.span // 2)

    # Row 0 is the NORTHERNMOST and column 0 the WESTERNMOST, and `bounds`
    # names the same two corners.
    assert r.bounds.ur.lat > r.bounds.ll.lat
    assert r.bounds.ur.lon > r.bounds.ll.lon
    assert r.bounds.ll.lat < GA.lat < r.bounds.ur.lat

    # The observer stands at the centre and can see its own post.
    assert r.at(r.span // 2, r.span // 2) == 0.0
    # Something is visible and something is not, or the fixture is useless.
    finite = a[np.isfinite(a)]
    assert (finite == 0.0).any() and (finite > 0.0).any()


def test_viewshed_progress_fires_on_integer_percent_changes():
    src, req = viewshed_request()
    seen = []
    r = an.compute_viewshed(src, req, lambda pct: seen.append(pct))
    assert r.valid
    assert seen, "no progress at all"
    assert seen == sorted(seen)
    assert len(seen) == len(set(seen)), "a percent was reported twice"
    assert 0 <= seen[0] and seen[-1] <= 100
    # Returning None is not a cancel.
    assert r.no_data_count >= 0


def test_viewshed_progress_can_cancel():
    src, req = viewshed_request()
    calls = []

    def cancel_at_ten(pct):
        calls.append(pct)
        return pct < 10

    with pytest.raises(pyfvw.FvError) as ei:
        an.compute_viewshed(src, req, cancel_at_ten)
    assert ei.value.code == pyfvw.INTERRUPTED
    assert calls[-1] >= 10


def test_a_raising_callback_raises_its_own_exception():
    """The cancel is our doing, so the user sees THEIR error, not FvError."""
    src, req = viewshed_request()

    class Boom(Exception):
        pass

    def boom(pct):
        raise Boom("stop")

    with pytest.raises(Boom):
        an.compute_viewshed(src, req, boom)

    # KeyboardInterrupt is the case this exists for: a user hitting ctrl-C
    # during a long viewshed must not have it laundered into an FvError.
    def interrupt(pct):
        raise KeyboardInterrupt

    with pytest.raises(KeyboardInterrupt):
        an.compute_viewshed(src, req, interrupt)

    # A callback returning something that is not a bool is the callback's bug.
    with pytest.raises(TypeError):
        an.compute_viewshed(src, req, lambda pct: object())


def test_the_gil_is_released_for_the_computation():
    """A second Python thread keeps running while a viewshed is in flight.

    NO PROGRESS CALLBACK here on purpose: a callback re-acquires the GIL a
    hundred times and would let the other thread run even if the outer release
    were missing, so it would test nothing.
    """
    src, req = viewshed_request(range_m=25000.0)

    stop = threading.Event()
    ticks = [0]

    def spin():
        while not stop.is_set():
            ticks[0] += 1
            time.sleep(0)

    t = threading.Thread(target=spin, daemon=True)
    t.start()
    try:
        time.sleep(0.02)  # let the thread get going
        before = ticks[0]
        r = an.compute_viewshed(src, req)
        during = ticks[0] - before
    finally:
        stop.set()
        t.join(timeout=2.0)

    assert r.valid
    # With the GIL held throughout, `during` would be 0 -- the other thread
    # cannot execute a single bytecode. The bound is deliberately loose; the
    # claim is "it ran", not "it ran fast".
    assert during > 50, f"other thread advanced only {during} ticks"


def test_viewshed_refuses_ground_it_cannot_stand_on():
    src = dted()
    req = an.ViewshedRequest()
    req.observer = geo.GeoPoint(5.0, 5.0)   # nowhere near the coverage
    req.observer_height_m = 30.0
    req.range_m = 5000.0
    req.step_deg = 0.001
    with pytest.raises(pyfvw.FvError) as ei:
        an.compute_viewshed(src, req)
    assert ei.value.code == pyfvw.OUT_OF_COVERAGE

    req.observer = GA
    req.range_m = 0.0
    with pytest.raises(pyfvw.FvError) as ei:
        an.compute_viewshed(src, req)
    assert ei.value.code == pyfvw.INVALID_ARG


def test_post_budget_widens_the_step_rather_than_cutting_the_range():
    """FalconView's 'Out of Memory. Try reducing the range' is a lie here."""
    src, req = viewshed_request(range_m=25000.0)
    fine = req.step_deg

    req.max_posts = 2500
    r = an.compute_viewshed(src, req)
    assert r.valid
    assert r.step_was_widened
    assert r.step_deg > fine

    # THE BUDGET IS APPROXIMATE, AND DELIBERATELY SO. The span is
    # floor(sqrt(max_posts)) rounded UP to odd, because there must be a centre
    # post to stand on -- so a cap of 2500 gives span 51 and 2601 posts, 4%
    # over. Rounding DOWN to 49 would deliver less range than asked for, which
    # is the one thing this budget exists not to do. With the million-post
    # default the overshoot is 0.2%.
    assert r.span % 2 == 1
    assert r.span * r.span >= 2500
    assert (r.span - 2) * (r.span - 2) < 2500
    # The range asked for is the range delivered, more coarsely: the bounds
    # still span the whole request.
    assert r.bounds.ur.lat - r.bounds.ll.lat == pytest.approx(
        (r.span - 1) * r.step_deg, rel=1e-6)


def test_source_of_none_is_an_argument_error():
    req = an.ViewshedRequest()
    for call in (
        lambda: an.compute_viewshed(None, req),
        lambda: an.sample_terrain_profile(None, an.GeoPath(),
                                          an.ProfileOptions()),
        lambda: an.viewshed_step_from_post_spacing(None, GA),
    ):
        with pytest.raises((pyfvw.FvError, TypeError)):
            call()
