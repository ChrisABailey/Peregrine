# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""pyfvw.overlay's point editor through the Python surface.

The gestures themselves are pinned in C++ (fvkit/test/point_edit_test.cpp).
What is only testable here is the seam a shell actually uses: that `edit` is
the overlay's own session and not a copy, that a MapPoint round-trips through
`add_at` and `update` with every field intact, and that `dimmed` is a window
state rather than a document change.
"""

import pyfvw

ovl = pyfvw.overlay
geo = pyfvw.geo


def harbour_proj():
    p = pyfvw.engine.MapProjection()
    p.set_surface_size(800, 600)
    p.set_center(geo.GeoPoint(32.74, -79.89))
    p.set_scale(50000.0)
    return p


def drawn(overlay, proj):
    """One frame, which is what gives the overlay the projection its gestures
    resolve pixels against."""
    canvas = pyfvw.canvas.CpuCanvas(800, 600)
    overlay.on_draw(proj, canvas)
    return overlay


def test_edit_is_the_overlays_own_session():
    o = ovl.PointOverlay("Points")
    o.edit.adding = True
    assert o.edit.adding is True
    o.edit.adding = False
    assert o.edit.adding is False


def test_add_at_carries_the_whole_prototype():
    o = ovl.PointOverlay("Points")
    proto = ovl.MapPoint(name="Beach hut", shape="diamond", size_px=22.0,
                         color=(20, 120, 200, 255), category="shelter",
                         elevation_ft=12.0, remarks="blue door",
                         phone="843 555 0100", url="example.org")
    pid = o.edit.add_at(proto, geo.GeoPoint(32.60, -80.11))
    assert pid != 0
    assert o.selected == pid

    p = o.find(pid)
    assert p.name == "Beach hut"
    assert p.shape == "diamond"
    assert p.size_px == 22.0
    assert p.category == "shelter"
    assert p.elevation_ft == 12.0
    assert p.remarks == "blue door"
    assert p.phone == "843 555 0100"
    assert p.url == "example.org"
    assert p.position.lat == 32.60


def test_update_writes_a_whole_row_and_undoes_as_one():
    o = ovl.PointOverlay("Points")
    pid = o.edit.add_at(ovl.MapPoint(name="Before"), geo.GeoPoint(32.6, -80.1))

    edited = o.find(pid)
    edited.name = "After"
    edited.url = "example.org/after"
    assert o.edit.update(edited) is True
    assert o.find(pid).name == "After"

    o.edit.undo()
    assert o.find(pid).name == "Before"
    assert o.find(pid).url == ""

    # A stale id is refused rather than silently inserted.
    ghost = ovl.MapPoint(name="Ghost")
    ghost.id = 9999
    assert o.edit.update(ghost) is False


def test_point_at_finds_what_was_drawn():
    o = ovl.PointOverlay("Points")
    proj = harbour_proj()
    pid = o.edit.add_at(ovl.MapPoint(name="Marker", size_px=20.0),
                        geo.GeoPoint(32.74, -79.89))
    drawn(o, proj)
    assert o.edit.point_at(400, 300) == pid
    assert o.edit.point_at(50, 50) == 0


def test_resolve_pixel_reports_whether_it_snapped():
    mgr = ovl.OverlayManager()
    target = ovl.PointOverlay("Survey")
    target.add_point(ovl.MapPoint(name="Ruddy Turnstone"))
    target.set_points([ovl.MapPoint(name="Ruddy Turnstone")])
    working = ovl.PointOverlay("Working")
    working.set_manager(mgr)
    mgr.add(target)
    mgr.add(working)

    proj = harbour_proj()
    # The survey point sits at the surface centre.
    pts = target.points
    pts[0].position = geo.GeoPoint(32.74, -79.89)
    target.set_points(pts)
    drawn(target, proj)
    drawn(working, proj)

    hit = working.edit.resolve_pixel(400, 300)
    assert hit.valid and hit.snapped
    assert "Ruddy Turnstone" in hit.snapped_to
    assert hit.position.lat == 32.74

    miss = working.edit.resolve_pixel(100, 100)
    assert miss.valid and not miss.snapped
    assert miss.snapped_to == ""


def test_rows_come_out_as_copies():
    """A row handed to Python is a COPY, from `find` and from `points` alike.

    A live view would let a shell move a point with no undo entry and no dirty
    flag -- silently, and only from Python, since both C++ accessors are const.
    The bargain is: the row comes out, is edited, and goes back through
    `update_point`."""
    o = ovl.PointOverlay("Points")
    # add_point, not set_points: set_points takes a document's rows as they
    # are and only add_point mints an id for a row that has none.
    o.add_point(ovl.MapPoint(name="x", lat=1.0, lon=2.0))

    from_list = o.points[0]
    from_find = o.find(from_list.id)
    assert from_list is not from_find

    from_find.position = geo.GeoPoint(9.0, 9.0)
    assert o.update_point(from_find) is True
    # Neither earlier row tracked the write.
    assert from_list.position.lat == 1.0
    assert o.find(from_list.id).position.lat == 9.0

    # And writing to a handed-out row changes nothing in the document.
    stale = o.points[0]
    stale.name = "not written"
    assert o.points[0].name == "x"


def test_dimming_is_a_window_state():
    o = ovl.PointOverlay("Points")
    o.set_points([ovl.MapPoint(name="A")])
    o.dirty = False
    assert o.dimmed is False
    o.dimmed = True
    assert o.dimmed is True
    assert o.dirty is False


def test_edit_position_is_one_type_under_two_names():
    assert pyfvw.route.EditPosition is ovl.EditPosition
