# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""AN6/AN7 — the analysis overlay, its palette and the button bar's model.

THE FIRST TESTS OVER `port/apps`. Everything under there was previously
covered only by `PythonView.py --selftest`, which takes screenshots and
therefore proves that nothing crashed rather than that anything is right.
What is tested here is the half of these modules that has no tk in it, which
is deliberately most of them:

  * the GESTURE STATE MACHINE — what a click means with each tool armed, and
    that a click means NOTHING at all without edit focus (an overlay that took
    `d` whenever it was on screen would delete a vertex during someone else's
    work);
  * the label PLACEMENT contract — `Measurement::Labels` skips a leg the
    geodesy refused, so the vertices this file draws them at have to skip the
    same ones or every label after a bad leg is against the wrong point;
  * the viewshed picture's RING BUDGET, which is the only reason it draws in
    single-digit milliseconds;
  * the route path, which follows the PLAN when there is one;
  * the button bar's model — that a palette is rendered as data and that a
    changed shape rebuilds while a changed state does not.

Nothing here opens a window. `ProfileWindow` is exercised through the pieces
of it that are not tk (`sample_profile`, `elevation_unit`, and the vertical
scale's ceiling in `elevation_window`) plus the run splitter, which is the
part that draws a hole as a hole.

Run via ctest (pythonview_pytest) or:
  PYTHONPATH=build/port/bindings/pyfvw:port/apps pytest -q port/apps/test
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

pyfvw = pytest.importorskip("pyfvw")
np = pytest.importorskip("numpy")

import analysis                                              # noqa: E402
import toolbar                                               # noqa: E402

an = pyfvw.analysis
geo = pyfvw.geo


def _dted():
    d = os.environ.get("FVW_TESTDATA_DIR")
    if not d:
        pytest.skip("no TestData")
    root = os.path.join(d, "dted")
    if not os.path.isdir(root):
        pytest.skip("no TestData/dted")
    return pyfvw.formats.DtedElevationSource(root)


def _proj(w=700, h=500, at=geo.GeoPoint(31.5, -81.5), denom=200e3):
    p = pyfvw.engine.MapProjection()
    p.set_surface_size(w, h)
    p.set_center(at)
    p.set_physical_scale(denom, 0.25)
    return p


def _armed(tool=None):
    """An overlay with a projection cached and edit focus taken — the state a
    click has to be in to mean anything. `on_draw` is how the projection gets
    there, exactly as it does in the application."""
    o = analysis.AnalysisOverlay("A")
    o.on_draw(_proj(), pyfvw.canvas.CpuCanvas(700, 500))
    o.enter_edit_focus()
    if tool:
        o.set_tool(tool)
    return o


def _click(o, x, y):
    return o.on_mouse_down(pyfvw.overlay.MouseEvent(x, y, 0))


# ---------------------------------------------------------------------------
# The gate: nothing routes without edit focus
# ---------------------------------------------------------------------------

def test_a_click_does_nothing_without_edit_focus():
    o = analysis.AnalysisOverlay("A")
    o.on_draw(_proj(), pyfvw.canvas.CpuCanvas(700, 500))
    o.set_tool(analysis.TOOL_LINE)
    assert _click(o, 100, 100) is False
    assert o.points == []


def test_keys_do_nothing_without_edit_focus():
    o = analysis.AnalysisOverlay("A")
    o.points = [geo.GeoPoint(31.0, -81.0), geo.GeoPoint(31.1, -81.1)]
    o.selected = ("vertex", 1)
    ev = pyfvw.overlay.KeyEvent(key=pyfvw.overlay.key.DELETE)
    assert o.on_key_down(ev) is False
    assert len(o.points) == 2


def test_releasing_focus_ends_the_gesture():
    o = _armed(analysis.TOOL_POLYLINE)
    _click(o, 100, 100)
    assert o.defining
    o.release_edit_focus()
    assert not o.defining
    assert o.on_mouse_move(pyfvw.overlay.MouseEvent(120, 120)) is False


# ---------------------------------------------------------------------------
# Defining and refining
# ---------------------------------------------------------------------------

def test_the_line_tool_takes_two_clicks_and_disarms():
    o = _armed(analysis.TOOL_LINE)
    assert _click(o, 100, 100) is True
    assert o.defining and len(o.points) == 1
    assert _click(o, 300, 260) is True
    assert len(o.points) == 2
    # A two-point object is finished by its second click: staying armed would
    # mean the next click somewhere else silently started a third leg.
    assert not o.defining
    assert o.tool == analysis.TOOL_SELECT
    assert o.kind == an.MeasurementKind.RANGE_BEARING


def test_the_polyline_tool_keeps_taking_clicks_until_return():
    o = _armed(analysis.TOOL_POLYLINE)
    for i in range(4):
        _click(o, 100 + 40 * i, 100 + 30 * i)
    assert len(o.points) == 4 and o.defining
    assert o.kind == an.MeasurementKind.MULTI_POINT
    o.on_key_down(pyfvw.overlay.KeyEvent(key=pyfvw.overlay.key.RETURN))
    assert not o.defining and o.tool == analysis.TOOL_SELECT


def test_pressing_the_armed_tool_again_disarms_it():
    o = _armed()
    o.set_tool(analysis.TOOL_AREA)
    assert o.tool == analysis.TOOL_AREA
    o.set_tool(analysis.TOOL_AREA)
    assert o.tool == analysis.TOOL_SELECT


def test_a_vertex_is_grabbed_and_dragged():
    o = _armed(analysis.TOOL_LINE)
    _click(o, 100, 100)
    _click(o, 300, 260)
    before = o.points[1]
    # Press ON the second vertex, move, release. This is the whole of
    # "refine the line".
    assert _click(o, 300, 260) is True
    assert o.selected == ("vertex", 1)
    assert o.on_mouse_move(pyfvw.overlay.MouseEvent(340, 200)) is True
    assert o.on_mouse_up(pyfvw.overlay.MouseEvent(340, 200)) is True
    after = o.points[1]
    assert (after.lat, after.lon) != (before.lat, before.lon)
    # And it went where the cursor was, not somewhere near it.
    x, y = _proj().geo_to_surface(after)
    assert abs(x - 340) <= 1 and abs(y - 200) <= 1


def test_a_press_on_empty_map_is_declined_so_the_map_still_pans():
    o = _armed()
    assert _click(o, 10, 10) is False


def test_undo_restores_the_path_and_redo_puts_it_back():
    o = _armed(analysis.TOOL_LINE)
    _click(o, 100, 100)
    _click(o, 300, 260)
    o.set_points([geo.GeoPoint(31.0, -81.0), geo.GeoPoint(31.2, -81.2),
                  geo.GeoPoint(31.4, -81.4)])
    assert len(o.points) == 3
    assert o.can_undo() and o.undo()
    assert len(o.points) == 2
    assert o.can_redo() and o.redo()
    assert len(o.points) == 3


def test_delete_removes_the_selected_vertex():
    o = _armed()
    o.enter_edit_focus()
    o.points = [geo.GeoPoint(31.0, -81.0), geo.GeoPoint(31.2, -81.2),
                geo.GeoPoint(31.4, -81.4)]
    o.selected = ("vertex", 1)
    assert o.on_key_down(pyfvw.overlay.KeyEvent(
        key=pyfvw.overlay.key.DELETE)) is True
    assert len(o.points) == 2
    assert o.selected is None
    assert o.can_undo()          # and it is undoable, which is the point


def test_the_line_type_belongs_to_the_path():
    o = _armed()
    o.points = [geo.GeoPoint(60.0, -100.0), geo.GeoPoint(60.0, 0.0)]
    gc = o.geo_path().point_at_distance(o.geo_path().total_length_m / 2).lat
    o.cycle_line_type()
    assert o.line_type == an.LineType.RHUMB
    rh = o.geo_path().point_at_distance(o.geo_path().total_length_m / 2).lat
    # AN1's own pinned property, reached through the overlay: the great-circle
    # midpoint at 60 N is past 62, the rhumb one is exactly 60.
    assert rh == pytest.approx(60.0, abs=1e-6)
    assert gc > 62.0


# ---------------------------------------------------------------------------
# What gets drawn
# ---------------------------------------------------------------------------

def test_a_measurement_draws_with_no_font_installed():
    # An `on_draw` that raised would come back as an FvError from the whole
    # stack. A canvas with no default font is the state of every test and of
    # `--shot` on a machine with no host font.
    o = _armed()
    o.points = [geo.GeoPoint(31.2, -81.7), geo.GeoPoint(31.8, -81.2)]
    o.kind = an.MeasurementKind.MULTI_POINT
    canvas = pyfvw.canvas.CpuCanvas(700, 500)
    canvas.clear((255, 255, 255))
    o.on_draw(_proj(), canvas)
    assert int((np.asarray(canvas.buffer)[:, :, :3] != 255).any(axis=2).sum()) > 0


def test_multi_point_labels_line_up_with_the_vertices_they_belong_to():
    # `Measurement::Labels` is the CUMULATIVE distance at every turning point
    # after the first, and a leg the geodesy refused contributes NOTHING. The
    # overlay picks the vertices with the same filter; if the two ever drift,
    # every label after a bad leg is drawn against the wrong point.
    o = analysis.AnalysisOverlay("A")
    o.points = [geo.GeoPoint(31.0, -81.0), geo.GeoPoint(31.2, -81.1),
                geo.GeoPoint(31.4, -81.2), geo.GeoPoint(31.6, -81.3)]
    o.kind = an.MeasurementKind.MULTI_POINT
    m = o.measurement()
    vertices = [i for i in range(1, len(o.points)) if m.leg(i - 1).ok]
    assert len(vertices) == len(m.labels)


def test_the_area_tool_closes_the_ring():
    o = _armed(analysis.TOOL_AREA)
    for x, y in ((100, 100), (300, 100), (300, 300)):
        _click(o, x, y)
    o.on_key_down(pyfvw.overlay.KeyEvent(key=pyfvw.overlay.key.RETURN))
    m = o.measurement()
    assert m.area_square_meters > 0.0
    assert "sq" in m.summary_label


# ---------------------------------------------------------------------------
# AN7 — the viewshed
# ---------------------------------------------------------------------------

def test_a_viewshed_runs_on_a_worker_and_comes_back():
    src = _dted()
    o = analysis.AnalysisOverlay("A")
    o.set_elevation_source(src)
    obs = o.add_viewshed(geo.GeoPoint(31.5, -81.5))
    obs.wait(120)
    assert not obs.running
    assert obs.error == ""
    assert obs.result is not None and obs.result.valid
    grid = np.asarray(obs.result)
    assert grid.shape == (obs.result.span, obs.result.span)
    # The observer stands on itself, so the centre post is always visible.
    c = obs.result.span // 2
    assert grid[c][c] == 0.0


def test_an_observer_with_no_terrain_is_still_a_mark_on_the_map():
    o = analysis.AnalysisOverlay("A")
    obs = o.add_viewshed(geo.GeoPoint(31.5, -81.5))
    obs.wait(5)
    assert obs.result is None
    assert obs.error
    # ...and drawing it does not raise: the ring and the handle are what say
    # an observer is there at all.
    o.on_draw(_proj(), pyfvw.canvas.CpuCanvas(700, 500))


def test_the_status_line_reports_the_failure_and_then_nothing():
    o = analysis.AnalysisOverlay("A")
    assert o.status_line() == ""
    obs = o.add_viewshed(geo.GeoPoint(31.5, -81.5))
    obs.wait(5)
    assert "Viewshed" in o.status_line()


def test_the_ring_budget_bounds_the_drawing_cost():
    # A checkerboard is the worst case there is: every other post flips, so
    # every row breaks into as many runs as it has columns.
    grid = np.indices((201, 201)).sum(axis=0) % 2
    grid = grid.astype(np.float32)
    stride, rows, c0, c1 = analysis._ring_budget(
        grid, lambda g: g == 0.0, 1, 500)
    assert stride is not None
    assert c0.size <= 500 or stride >= 201 // 4
    assert rows.size == c0.size == c1.size


def test_the_stride_follows_the_screen_and_not_the_post_count():
    # A viewshed drawn 40 pixels across costs the same whether it holds ten
    # thousand posts or a million.
    assert analysis.AnalysisOverlay._draw_stride(1001, 40) > 20
    assert analysis.AnalysisOverlay._draw_stride(1001, 4000) == 1


def test_runs_2d_finds_edge_touching_runs():
    m = np.array([[1, 1, 0, 1], [0, 0, 0, 0], [1, 1, 1, 1]], dtype=bool)
    rows, c0, c1 = analysis._runs2d(m)
    assert list(zip(rows.tolist(), c0.tolist(), c1.tolist())) == [
        (0, 0, 2), (0, 3, 4), (2, 0, 4)]


# ---------------------------------------------------------------------------
# AN6 — the profile
# ---------------------------------------------------------------------------

def test_the_profile_of_a_measured_path():
    src = _dted()
    o = analysis.AnalysisOverlay("A")
    o.points = [geo.GeoPoint(31.2, -81.8), geo.GeoPoint(31.8, -81.2)]
    r, err = analysis.sample_profile(src, o.geo_path(), 50)
    assert err == "" and r is not None
    assert len(r.points) == 50
    assert r.max_m >= r.min_m
    assert r.points[0].is_vertex and r.points[-1].is_vertex


def test_nothing_to_profile_is_not_an_error():
    assert analysis.sample_profile(None, None, 10) == (None, "")
    o = analysis.AnalysisOverlay("A")
    o.points = [geo.GeoPoint(31.0, -81.0)]
    assert analysis.sample_profile(None, o.geo_path(), 10) == (None, "")


def test_no_elevation_source_says_so_rather_than_raising():
    o = analysis.AnalysisOverlay("A")
    o.points = [geo.GeoPoint(31.0, -81.0), geo.GeoPoint(31.2, -81.2)]
    r, err = analysis.sample_profile(None, o.geo_path(), 10)
    assert r is None and "elevation" in err


class _P:
    def __init__(self, d, e, has, vertex=False):
        self.distance_m, self.elevation_m = d, e
        self.has_data, self.is_vertex = has, vertex


def test_a_hole_splits_the_line_rather_than_bridging_it():
    pts = [_P(0, 1, True), _P(1, 2, True), _P(2, float("nan"), False),
           _P(3, 4, True)]
    runs = analysis._profile_runs(pts)
    assert [len(r) for r in runs] == [2, 1]


def test_elevation_units_follow_the_distance_units():
    st = an.MeasureStyle()
    st.units = an.RangeUnit.NAUTICAL_MILES
    assert analysis.elevation_unit(st)[0] == "ft"
    st.units = an.RangeUnit.KILOMETERS
    assert analysis.elevation_unit(st) == ("m", 1.0)


def test_a_half_per_cent_rise_uses_a_quarter_of_the_chart():
    # 5 m over a kilometre is a flat road. The frame is 2% of the length, so
    # the bump gets a quarter of it and reads as the ripple it is.
    lo, hi = analysis.elevation_window(10.0, 15.0, 1000.0, 1.0)
    assert hi - lo == pytest.approx(20.0)
    assert 5.0 / (hi - lo) == pytest.approx(0.25)
    # and the spare room is split, so the line sits across the middle
    assert (lo + hi) / 2.0 == pytest.approx(12.5)


def test_two_per_cent_and_steeper_fill_the_chart():
    for relief in (20.0, 200.0):
        lo, hi = analysis.elevation_window(0.0, relief, 1000.0, 1.0)
        # the data and its 8% margin, top and bottom — the floor never bites
        assert hi - lo == pytest.approx(relief * 1.16)


def test_the_ceiling_is_a_grade_and_so_survives_the_units():
    per_ft = 1.0 / 0.3048
    lo, hi = analysis.elevation_window(10.0, 15.0, 1000.0, per_ft)
    assert 5.0 * per_ft / (hi - lo) == pytest.approx(0.25)


def test_dead_flat_ground_still_gets_a_window_to_draw_in():
    lo, hi = analysis.elevation_window(3.0, 3.0, 0.0, 1.0)
    assert hi > lo


# ---------------------------------------------------------------------------
# The route's profile — AN6's second way in
# ---------------------------------------------------------------------------

class _FakeRoute:
    """Only what `route_geo_path` reads. A real RouteOverlay needs a planner,
    a graph and a stack; what is under test is which of two geometries the
    path is taken from."""

    class _Plan:
        def __init__(self, legs):
            self.legs = legs

    class _Wp:
        def __init__(self, lat, lon):
            self.lat, self.lon = lat, lon

    def __init__(self, waypoints, legs=None):
        self.waypoints = [self._Wp(*w) for w in waypoints]
        self.has_plan = legs is not None
        self.plan = self._Plan(legs or [])
        self.leg_kind = geo.LineKind.GREAT_CIRCLE


def test_a_route_with_no_plan_profiles_its_waypoints():
    r = _FakeRoute([(31.0, -81.0), (31.5, -81.5)])
    p = analysis.route_geo_path(r)
    assert p.point_count == 2


def test_a_route_that_follows_roads_profiles_the_ROADS():
    # The waypoints are 2 points and the plan is 5; profiling the waypoints
    # would sample a straight line over ground the route never touches. The
    # shared join between consecutive legs is dropped exactly once.
    leg1 = [geo.GeoPoint(31.0, -81.0), geo.GeoPoint(31.1, -81.05),
            geo.GeoPoint(31.2, -81.1)]
    leg2 = [geo.GeoPoint(31.2, -81.1), geo.GeoPoint(31.3, -81.2),
            geo.GeoPoint(31.5, -81.5)]
    r = _FakeRoute([(31.0, -81.0), (31.5, -81.5)], legs=[leg1, leg2])
    p = analysis.route_geo_path(r)
    assert p.point_count == 5
    assert p.points[2].lat == pytest.approx(31.2)


def test_a_one_waypoint_route_has_no_path():
    assert analysis.route_geo_path(_FakeRoute([(31.0, -81.0)])) is None
    assert analysis.route_geo_path(None) is None


# ---------------------------------------------------------------------------
# The palette and the bar's model
# ---------------------------------------------------------------------------

class _FakeDesc:
    def __init__(self, tid, name):
        self.id, self.display_name = tid, name


class _FakeRegistry:
    def __init__(self, descs):
        self._d = descs

    def with_editors(self):
        return self._d


class _FakeEditors:
    def __init__(self, mode, edited=None):
        self.current_mode, self.edited = mode, edited


def test_mode_nodes_check_the_active_mode_and_nothing_else():
    reg = _FakeRegistry([_FakeDesc("a", "Route"), _FakeDesc("b", "Analysis")])
    pressed = []
    nodes = toolbar.mode_nodes(reg, _FakeEditors("b"), pressed.append,
                               pyfvw.app.MenuNode)
    assert [n.label for n in nodes] == ["Route", "Analysis"]
    assert [n.checked for n in nodes] == [False, True]
    nodes[0].invoke()
    assert pressed == ["a"]


class _FakeApp:
    def __init__(self, overlay):
        self.editors = _FakeEditors("app.analysis", overlay)
        self.opened = []

    def open_profile_window(self, provider, **kw):
        self.opened.append((provider, kw))


def test_the_palette_reports_live_state():
    o = analysis.AnalysisOverlay("A")
    ed = analysis.AnalysisEditor(_FakeApp(o))
    by_label = {n.label: n for n in ed.tools() if not n.is_separator}
    # Nothing drawn yet: there is nothing to profile and nothing to clear.
    assert by_label["Profile..."].enabled is False
    assert by_label["Clear"].enabled is False
    assert by_label["Line"].checked is False

    by_label["Line"].invoke()
    assert o.tool == analysis.TOOL_LINE
    assert {n.label: n for n in ed.tools()}["Line"].checked is True

    o.points = [geo.GeoPoint(31.0, -81.0), geo.GeoPoint(31.2, -81.2)]
    by_label = {n.label: n for n in ed.tools() if not n.is_separator}
    assert by_label["Profile..."].enabled is True
    assert by_label["Clear"].enabled is True


def test_the_palette_button_opens_the_shell_s_window_and_not_its_own():
    o = analysis.AnalysisOverlay("A")
    o.points = [geo.GeoPoint(31.0, -81.0), geo.GeoPoint(31.2, -81.2)]
    shell = _FakeApp(o)
    ed = analysis.AnalysisEditor(shell)
    {n.label: n for n in ed.tools()}["Profile..."].invoke()
    assert len(shell.opened) == 1
    provider, kw = shell.opened[0]
    # A PROVIDER, not a path: the chart re-reads what the user is editing, so
    # refining the line under it redraws it.
    assert provider().point_count == 2
    assert kw["style"] is o.style


def test_an_editor_with_no_overlay_still_renders_a_palette():
    # The bar is built before anything is created and after everything is
    # closed; `tools()` reaching for a `None` overlay is the commonest way a
    # palette takes the whole window down.
    ed = analysis.AnalysisEditor(None)
    nodes = ed.tools()
    assert nodes and all(n.is_separator or not n.enabled or n.label
                         for n in nodes)
    for n in nodes:
        n.invoke()                # must not raise


def test_the_type_descriptor_is_static_and_above_the_route():
    desc = analysis.analysis_type_desc(factory=lambda: None,
                                       editor_factory=lambda: None)
    assert desc.id == analysis.ANALYSIS_TYPE_ID
    assert desc.file is None                     # no document: `.rbo` is out
    assert desc.default_display_order > 1000     # drawn over the route
