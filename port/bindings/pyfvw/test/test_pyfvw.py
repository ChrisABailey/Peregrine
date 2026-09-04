# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""pyfvw binding tests — mirror the gtest pins (port/fvkit/test/) through
the Python surface. Real-data tests skip when FVW_TESTDATA_DIR is absent.

Run via ctest (pyfvw_pytest) or directly:
  PYTHONPATH=build/port/bindings/pyfvw pytest -q port/bindings/pyfvw/test
"""

import glob
import math
import os
import sys

import numpy as np
import pytest

import pyfvw


def _testdata(sub):
    d = os.environ.get("FVW_TESTDATA_DIR")
    if not d:
        return None
    p = os.path.join(d, sub)
    return p if os.path.isdir(p) else None


def _count_by_ext(root, exts):
    """Independent (non-enumerator) inventory: {ext: file count} under root.

    Extension case varies in TestData (n40.DT3 vs n30.dt1), so match folded.
    """
    counts = {e: 0 for e in exts}
    for _, _, files in os.walk(root):
        for name in files:
            ext = os.path.splitext(name)[1].lower()
            if ext in counts:
                counts[ext] += 1
    return counts


# ---------------------------------------------------------------------------
# geo primitives (always run)
# ---------------------------------------------------------------------------

def test_normalize_lon():
    assert pyfvw.geo.normalize_lon(180.0) == 180.0
    assert pyfvw.geo.normalize_lon(-180.0) == 180.0  # +/-180 -> +180 (D2)
    assert pyfvw.geo.normalize_lon(190.0) == -170.0
    assert pyfvw.geo.normalize_lon(-190.0) == 170.0


def test_georect_antimeridian():
    r = pyfvw.geo.GeoRect(
        ll=pyfvw.geo.GeoPoint(10.0, 170.0), ur=pyfvw.geo.GeoPoint(20.0, -170.0)
    )
    assert r.crosses_antimeridian
    assert r.contains(pyfvw.geo.GeoPoint(15.0, 180.0))
    assert not r.contains(pyfvw.geo.GeoPoint(15.0, 0.0))
    east = pyfvw.geo.GeoRect(
        ll=pyfvw.geo.GeoPoint(12.0, -179.0), ur=pyfvw.geo.GeoPoint(18.0, -160.0)
    )
    assert r.intersects(east)
    assert pyfvw.geo.GeoRect.world().contains(pyfvw.geo.GeoPoint(90.0, 180.0))


def test_parse_location():
    # decimal
    p = pyfvw.geo.parse_location("33.7488 -84.3882")
    assert math.isclose(p.lat, 33.7488, abs_tol=1e-4)
    assert math.isclose(p.lon, -84.3882, abs_tol=1e-4)
    # degrees-minutes-seconds
    p = pyfvw.geo.parse_location("33 44 55.7 N 84 23 17.5 W")
    assert math.isclose(p.lat, 33.7488, abs_tol=1e-3)
    assert math.isclose(p.lon, -84.3882, abs_tol=1e-3)
    # MGRS / milgrid
    p = pyfvw.geo.parse_location("16SGB 47342 34212")
    assert 32.0 < p.lat < 33.5 and -85.0 < p.lon < -84.0
    # unparseable -> FvError
    with pytest.raises(pyfvw.FvError):
        pyfvw.geo.parse_location("not a place")


def test_fverror_shape():
    src = pyfvw.formats.GeoTiffRasterSource()
    with pytest.raises(pyfvw.FvError) as ei:
        src.open("/nonexistent.tif")
    assert ei.value.code < 0
    assert isinstance(ei.value.message, str) and ei.value.message


# ---------------------------------------------------------------------------
# DTED (real data)
# ---------------------------------------------------------------------------

def test_dted_pins():
    root = _testdata("dted")
    if root is None:
        pytest.skip("no TestData")
    src = pyfvw.formats.DtedElevationSource(root)
    # same pins as dted_adapter_test.cpp, through Python
    for lat, lon, meters in [
        (31.5, -81.5, 9.0),
        (31.25, -81.75, 16.0),
        (31.75, -81.25, 0.0),
        (31.0, -82.0, 25.0),
    ]:
        assert src.get_elevation(lat, lon) == meters
    b = src.bounds
    assert b.ll.lat == 30.0 and b.ur.lat == 41.0

    with pytest.raises(pyfvw.FvError) as ei:
        src.get_elevation(50.0, -82.0)
    assert ei.value.code == pyfvw.OUT_OF_COVERAGE


def test_dted_enumerate():
    root = _testdata("dted")
    if root is None:
        pytest.skip("no TestData")
    frames = pyfvw.formats.DtedFrameEnumerator().frames(root)
    # Counted from the tree, not hardcoded — TestData grows (22 -> 24 cells
    # 2026-07-21, +2 DTED2 2026-07-23) and a literal makes every data drop a
    # spurious failure. Level n on disk must show up as series DTED<n>.
    on_disk = _count_by_ext(root, (".dt1", ".dt2", ".dt3"))
    assert on_disk[".dt1"] > 0, f"no .dt1 cells under {root}"
    assert len(frames) == sum(on_disk.values())
    for ext, n in on_disk.items():
        series = "DTED" + ext[-1]
        assert sum(1 for f in frames if f.series_key == series) == n


# ---------------------------------------------------------------------------
# GeoTIFF (real data)
# ---------------------------------------------------------------------------

def test_geotiff_enumerate():
    root = _testdata("geotiff")
    if root is None:
        pytest.skip("no TestData")
    frames = pyfvw.formats.GeoTiffFrameEnumerator().frames(root)
    # Every .tif on disk must be recognized (15 -> 29 samples on 2026-07-23,
    # when the Charleston SC set arrived); see the DTED note above.
    n_tif = sum(_count_by_ext(root, (".tif", ".tiff")).values())
    assert n_tif > 0, f"no .tif samples under {root}"
    assert len(frames) == n_tif
    paths = [f.path for f in frames]
    assert paths == sorted(paths)
    for f in frames:
        assert f.bounds.ll.lat < f.bounds.ur.lat
        assert f.series_key


def test_geotiff_read_block_numpy():
    root = _testdata("geotiff")
    if root is None:
        pytest.skip("no TestData")
    src = pyfvw.formats.GeoTiffRasterSource()
    src.open(os.path.join(root, "38076g81.tif"))
    info = src.info
    assert info.width > 1000 and info.height > 1000

    buf = src.read_block(info.width // 2, info.height // 2, 128, 128)
    a = np.asarray(buf)  # zero-copy view
    assert a.shape == (128, 128, 4) and a.dtype == np.uint8
    assert (a[:, :, 3] == 255).all()  # opaque
    assert (a[:, :, 0] == a[:, :, 1]).all()  # B&W DOQ: R == G == B
    mean = float(a[:, :, 0].mean())
    assert 2.0 < mean < 253.0
    assert len({int(v) for v in a[:, :, 0].ravel()}) > 8

    with pytest.raises(pyfvw.FvError) as ei:
        src.read_block(-1, 0, 8, 8)
    assert ei.value.code == pyfvw.INVALID_ARG


def test_geotiff_transform_roundtrip():
    root = _testdata("geotiff")
    if root is None:
        pytest.skip("no TestData")
    src = pyfvw.formats.GeoTiffRasterSource()
    src.open(os.path.join(root, "38076g81.tif"))
    info = src.info
    cx, cy = info.width // 2, info.height // 2
    p = src.pixel_to_geo(cx, cy)
    assert src.bounds.contains(p)
    px, py = src.geo_to_pixel(p)
    assert math.isclose(px, cx, abs_tol=1.0)
    assert math.isclose(py, cy, abs_tol=1.0)


# ---------------------------------------------------------------------------
# CADRG (real data)
# ---------------------------------------------------------------------------

def test_cadrg_enumerate():
    root = _testdata("rpf")
    if root is None:
        pytest.skip("no TestData")
    frames = pyfvw.formats.CadrgFrameEnumerator().frames(root)
    assert len(frames) > 500
    assert all(f.bounds.ll.lat < f.bounds.ur.lat for f in frames)
    assert {f.series_key for f in frames} >= {"GNC", "JNC", "LFC"}


def test_cadrg_source_decode_and_transform():
    root = _testdata("rpf")
    if root is None:
        pytest.skip("no TestData")
    path = os.path.join(root, "cgnc", "1", "00024023.gn1")
    if not os.path.exists(path):
        pytest.skip("frame missing")
    src = pyfvw.formats.CadrgRasterSource()
    src.open(path)
    assert src.info.width == 1536 and src.info.height == 1536

    a = np.asarray(src.read_block(640, 640, 256, 256))
    assert a.shape == (256, 256, 4)
    assert (a[:, :, 3] == 255).all()
    assert 10.0 < float(a[:, :, 0].mean()) < 245.0
    assert len({int(v) for v in a[:, :, 0].ravel()}) > 8

    # equal-arc corners + round-trip
    b = src.bounds
    nw = src.pixel_to_geo(0, 0)
    assert math.isclose(nw.lat, b.ur.lat, abs_tol=1e-6)
    assert math.isclose(nw.lon, b.ll.lon, abs_tol=1e-6)
    p = src.pixel_to_geo(700, 900)
    px, py = src.geo_to_pixel(p)
    assert math.isclose(px, 700, abs_tol=1e-6)
    assert math.isclose(py, 900, abs_tol=1e-6)


def test_tiros_enumerate_and_decode():
    root = _testdata("tiros3")
    if root is None:
        pytest.skip("no TestData")
    frames = pyfvw.formats.TirosFrameEnumerator().frames(root)
    assert len(frames) > 100
    assert all(f.bounds.ll.lat < f.bounds.ur.lat for f in frames)

    tile = os.path.join(root, "topobath", "500m", "TopoBath_500m_5530.wld")
    if not os.path.exists(tile):
        pytest.skip("tile missing")
    src = pyfvw.formats.TirosRasterSource()
    src.open(tile)
    assert src.info.width == 1350 and src.info.height == 1350
    a = np.asarray(src.read_block(500, 500, 128, 128))
    assert a.shape == (128, 128, 4)
    assert (a[:, :, 3] == 255).all()
    # equal-arc round-trip
    p = src.pixel_to_geo(600, 700)
    px, py = src.geo_to_pixel(p)
    assert math.isclose(px, 600, abs_tol=1e-6)
    assert math.isclose(py, 700, abs_tol=1e-6)


def test_canvas_draw():
    c = pyfvw.canvas.CpuCanvas(32, 32)
    c.clear((0, 0, 0))
    c.fill_polygon([[(2, 28), (16, 4), (30, 28)]], fill=(255, 0, 0))
    a = np.asarray(c.buffer)
    assert a.shape == (32, 32, 4)
    assert a[20, 16, 0] == 255      # inside the triangle
    assert a[2, 2, 0] == 0          # outside untouched
    # even-odd star: center is a hole
    c.clear((0, 0, 0))
    star = [(16, 2), (7, 30), (30, 12), (2, 12), (25, 30)]
    c.fill_polygon([star], fill=(0, 255, 0))
    a = np.asarray(c.buffer)
    assert a[16, 16, 1] == 0        # hole
    assert a[7, 16, 1] == 255       # top point filled
    with pytest.raises(pyfvw.FvError):
        c.draw_lines([(1, 1)], color=(255, 255, 255))


def test_catalog_scan_select_best():
    root = _testdata("rpf")
    if root is None:
        pytest.skip("no TestData")
    pyfvw.catalog.register_builtin_formats()
    c = pyfvw.catalog.Catalog()
    src = c.add_data_source(root, "cadrg")
    added = c.scan(src)
    assert added > 500
    assert {s.series_key for s in c.series()} >= {"GNC", "JNC", "LFC"}

    atl = pyfvw.geo.GeoRect(ll=pyfvw.geo.GeoPoint(33.6, -84.5),
                            ur=pyfvw.geo.GeoPoint(33.9, -84.2))
    rows = c.select_by_geo_rect(atl)
    assert rows and all(r.bounds.intersects(atl) for r in rows)
    assert any(r.series_key == "LFC" for r in rows)

    best = c.best_series_for_scale(500000)
    assert best.series_key == "LFC"
    # restrict to one series
    lfc_id = next(s.id for s in c.series() if s.series_key == "LFC")
    only_lfc = c.select_by_geo_rect(atl, series_id=lfc_id)
    assert only_lfc and all(r.series_key == "LFC" for r in only_lfc)

    c.remove_data_source(src)
    assert c.select_by_geo_rect(pyfvw.geo.GeoRect.world()) == []


def test_a_geotiff_series_is_ONE_resolution_not_one_name():
    """A GeoTIFF directory holds "Color" sheets at several ground resolutions
    (TestData: 0.3, 0.6, 1, 10 and 50 m/pixel, the last being a scanned
    1:500K sectional). FalconView keys tblMapSeries on (scale, units, series),
    so each is its own map type; the catalog collapsed them into one row until
    schema 2, and the row then reported whichever file was scanned first."""
    root = _testdata("geotiff")
    if root is None:
        pytest.skip("no TestData")
    pyfvw.catalog.register_builtin_formats()
    c = pyfvw.catalog.Catalog()
    src = c.add_data_source(root, "geotiff")
    assert c.scan(src) > 0
    assert not c.needs_rescan          # a catalog made here is already current

    series = c.series()
    colors = [s for s in series if s.series_key == "Color"]
    assert len(colors) > 1, "one key, several resolutions"
    assert len({s.scale for s in colors}) == len(colors)

    # display_name is the handle: unique, and it names the scale in the
    # product's OWN units the way FalconView's map-type list does.
    assert len({s.display_name for s in series}) == len(series)
    assert "Color 1 meter" in {s.display_name for s in series}

    # ...and every frame in a series really is at that series' scale.
    world = pyfvw.geo.GeoRect.world()
    for s in series:
        rows = c.select_by_geo_rect(world, series_id=s.id)
        assert rows, s.display_name
        assert all(r.series_key == s.series_key for r in rows)


def test_engine_render():
    root = _testdata("rpf")
    if root is None:
        pytest.skip("no TestData")
    pyfvw.catalog.register_builtin_formats()
    c = pyfvw.catalog.Catalog()
    c.scan(c.add_data_source(root, "cadrg"))
    e = pyfvw.engine.MapEngine(c)
    e.set_surface(200, 150)
    e.set_center(pyfvw.geo.GeoPoint(33.7488, -84.3882))
    e.set_scale(500000)
    lfc = next(s.id for s in c.series() if s.series_key == "LFC")
    cv = pyfvw.canvas.CpuCanvas(200, 150)
    n = e.render(cv, lfc)
    assert n >= 1
    a = np.asarray(cv.buffer)
    assert len(np.unique(a[:, :, 0])) > 8  # chart content, not one flat color
    # projection accessors round-trip
    p = e.proj.surface_to_geo(100, 75)
    sx, sy = e.proj.geo_to_surface(p)
    assert abs(sx - 100) < 1e-6 and abs(sy - 75) < 1e-6
    # elevation path
    dted = _testdata("dted")
    if dted:
        e.set_elevation_source(pyfvw.formats.DtedElevationSource(dted))
        assert e.get_elevation(31.5, -81.5) == 9.0


def test_projection_rotation_turns_the_chart_clockwise():
    """PR1's turn, PR2's binding. No data: this is the transform itself.

    The sign is the thing worth pinning from Python, because it is the one
    property a shell cannot fix later — set_rotation turns the CHART clockwise
    on screen, so a point due north of the centre swings to the RIGHT at 90.
    """
    p = pyfvw.engine.MapProjection()
    p.set_surface_size(401, 301)
    p.set_center(pyfvw.geo.GeoPoint(32.75, -79.90))
    p.set_resolution(0.0002, 0.0002)
    cx, cy = p.geo_to_surface(p.center)

    north = pyfvw.geo.GeoPoint(32.76, -79.90)
    x0, y0 = p.geo_to_surface(north)
    assert y0 < cy - 40 and abs(x0 - cx) < 1e-9, "north is up on an unturned map"

    assert p.rotation == 0.0
    p.set_rotation(90.0)
    assert p.rotation == 90.0
    x1, y1 = p.geo_to_surface(north)
    assert x1 > cx + 40, "a clockwise quarter turn puts north to the right"
    assert abs(y1 - cy) < 1e-9

    # Any finite angle, wrapped; and the round trip still inverts.
    p.set_rotation(-45.0)
    assert p.rotation == 315.0
    back = p.surface_to_geo(*p.geo_to_surface(north))
    assert abs(back.lat - north.lat) < 1e-9 and abs(back.lon - north.lon) < 1e-9

    # A turned viewport reads a BIGGER box than it draws — the honest price of
    # rotating the projection rather than the image.
    straight = pyfvw.engine.MapProjection()
    straight.set_surface_size(401, 301)
    straight.set_center(pyfvw.geo.GeoPoint(32.75, -79.90))
    straight.set_resolution(0.0002, 0.0002)
    p.set_rotation(45.0)
    assert p.bounds.ur.lat - p.bounds.ll.lat > straight.bounds.ur.lat - straight.bounds.ll.lat
    assert p.bounds.ur.lon - p.bounds.ll.lon > straight.bounds.ur.lon - straight.bounds.ll.lon

    # Back to zero is back to the identity, exactly.
    p.set_rotation(0.0)
    assert p.geo_to_surface(north) == (x0, y0)

    with pytest.raises(Exception):
        p.set_rotation(float("nan"))


def test_engine_render_turns_the_raster_too():
    """PR3, from the shell's side: engine.set_rotation turns the BASE MAP.

    PR2 left the one combination to avoid — turned vectors over image tiles
    still blitted axis-aligned — so what this pins is that the raster really
    moved, and that rotation 0 is still the blit the engine has always done.
    """
    root = _testdata("rpf")
    if root is None:
        pytest.skip("no TestData")
    pyfvw.catalog.register_builtin_formats()
    c = pyfvw.catalog.Catalog()
    c.scan(c.add_data_source(root, "cadrg"))
    lfc = next(s.id for s in c.series() if s.series_key == "LFC")

    def render(deg):
        e = pyfvw.engine.MapEngine(c)
        e.set_surface(200, 150)
        e.set_center(pyfvw.geo.GeoPoint(33.7488, -84.3882))
        e.set_scale(500000)
        e.set_rotation(deg)
        assert e.proj.rotation == deg
        cv = pyfvw.canvas.CpuCanvas(200, 150)
        assert e.render(cv, lfc) >= 1
        return np.asarray(cv.buffer).copy()

    straight = render(0.0)
    turned = render(90.0)
    assert not np.array_equal(straight, turned), "the chart must actually turn"
    # It is the same chart, not a different one: the same colours in about the
    # same quantities, rearranged. (A quarter turn on a square-ish window keeps
    # nearly all of the ink; the histogram is the cheap way to say so.)
    h0 = np.bincount(straight[:, :, 0].ravel(), minlength=256)
    h1 = np.bincount(turned[:, :, 0].ravel(), minlength=256)
    assert np.count_nonzero(h1) > 8, "still chart content, not a flat smear"
    assert abs(int(h0.argmax()) - int(h1.argmax())) <= 2

    # And 0 is the exact identity, byte for byte, after a round trip.
    assert np.array_equal(render(0.0), straight)


def test_engine_physical_scale():
    # A cartographic series draws at its denominator; the mm_per_pixel knob
    # zooms; and the projection has physically-correct aspect (dpp_lon/dpp_lat
    # ~ 1/cos(lat)), not the distorted ratio the old scale path produced.
    root = _testdata("rpf")
    if root is None:
        pytest.skip("no TestData")
    pyfvw.catalog.register_builtin_formats()
    c = pyfvw.catalog.Catalog()
    c.scan(c.add_data_source(root, "cadrg"))
    lfc = next(s for s in c.series() if s.series_key == "LFC")
    e = pyfvw.engine.MapEngine(c)
    e.set_surface(200, 150)
    e.set_center(pyfvw.geo.GeoPoint(35.0, -116.0))

    native = pyfvw.engine.NATIVE_DISPLAY_MM_PER_PIXEL
    e.set_physical_scale(lfc.scale, lfc.scale_units, native)
    assert e.proj.scale == lfc.scale_denom          # denominator passes through
    assert abs(e.proj.mm_per_pixel - native) < 1e-12
    ratio = e.proj.deg_per_pixel_lon / e.proj.deg_per_pixel_lat
    assert abs(ratio - 1.0 / math.cos(math.radians(35.0))) < 0.02

    # Zoom out 2x: twice the ground per pixel.
    dpp0 = e.proj.deg_per_pixel_lat
    e.set_physical_scale(lfc.scale, lfc.scale_units, native * 2)
    assert abs(e.proj.deg_per_pixel_lat - 2 * dpp0) < 1e-12

    # A metres-resolution series (imagery) is displayed at 100% at the native
    # pitch: one screen pixel spans one source metre (for 1 m data).
    e.set_physical_scale(1.0, 4, native)            # units 4 == MAP_SCALE_METERS
    m_per_deg_lat = 111132.92 - 559.82 * math.cos(math.radians(2 * 35.0))
    assert abs(e.proj.deg_per_pixel_lat * m_per_deg_lat - 1.0) < 1e-2


def test_vpf_enumerate_and_catalog():
    root = _testdata("vpf/dnc17")
    if root is None:
        pytest.skip("no TestData")
    frames = pyfvw.formats.VpfFrameEnumerator().frames(root)
    assert len(frames) > 40
    libs = {f.series_key for f in frames}
    assert "h1707330" in libs and "coa17c" in libs  # harbor + coastal
    assert "browse" not in libs  # untiled/degenerate library is skipped
    assert all(f.bounds.ll.lat < f.bounds.ur.lat for f in frames)

    pyfvw.catalog.register_builtin_formats()
    cat = pyfvw.catalog.Catalog()
    n = cat.scan(cat.add_data_source(root, "vpf"))
    assert n > 40
    # Nantucket Sound -> harbor coverage
    around = pyfvw.geo.GeoRect(ll=pyfvw.geo.GeoPoint(41.3, -70.2),
                               ur=pyfvw.geo.GeoPoint(41.4, -70.0))
    rows = cat.select_by_geo_rect(around)
    assert rows and any(r.series_key.startswith("h") for r in rows)
    # Atlanta -> nothing (this DNC library is Cape Cod)
    atl = pyfvw.geo.GeoRect(ll=pyfvw.geo.GeoPoint(33.6, -84.5),
                            ur=pyfvw.geo.GeoPoint(33.9, -84.2))
    assert cat.select_by_geo_rect(atl) == []


def test_tile_pack_write_and_render(tmp_path):
    root = _testdata("rpf")
    if root is None:
        pytest.skip("no TestData")
    pyfvw.catalog.register_builtin_formats()
    src_cat = pyfvw.catalog.Catalog()
    src_cat.scan(src_cat.add_data_source(root, "cadrg"))
    lfc = next(s.id for s in src_cat.series() if s.series_key == "LFC")
    eng = pyfvw.engine.MapEngine(src_cat)

    pack = str(tmp_path / "atl.gpkg")
    w = pyfvw.store.TilePackWriter()
    w.create(pack, "atl",
             pyfvw.geo.GeoRect(ll=pyfvw.geo.GeoPoint(33.4, -84.9),
                               ur=pyfvw.geo.GeoPoint(34.0, -84.0)))
    n0 = w.write_level(eng, 0, lfc)
    n1 = w.write_level(eng, 1, lfc)
    w.close()
    assert n0 >= 1 and n1 >= n0

    # consume the pack through the catalog/engine like any other format
    pack_cat = pyfvw.catalog.Catalog()
    n = pack_cat.scan(pack_cat.add_data_source(str(tmp_path), "gpkg"))
    assert n == 2  # one coverage row per level
    view = pyfvw.engine.MapEngine(pack_cat)
    view.set_surface(160, 120)
    view.set_center(pyfvw.geo.GeoPoint(33.7488, -84.3882))
    view.set_scale(1_000_000)
    cv = pyfvw.canvas.CpuCanvas(160, 120)
    cv.clear((9, 9, 9))
    assert view.render(cv) >= 1
    a = np.asarray(cv.buffer)
    assert (a[:, :, 0] != 9).sum() > 500  # chart pixels from the pack


def test_overlay_python_subclass():
    class Crosshair(pyfvw.overlay.Overlay):
        def __init__(self):
            super().__init__("cross")
            self.clicks = []

        def on_draw(self, proj, canvas):
            canvas.draw_lines([(10, 20), (30, 20)], color=(255, 0, 0), width=1)

        def on_mouse_down(self, e):
            self.clicks.append((e.x, e.y))
            return True

    cat = pyfvw.catalog.Catalog()
    eng = pyfvw.engine.MapEngine(cat)
    eng.set_surface(64, 64)
    eng.set_center(pyfvw.geo.GeoPoint(0.0, 0.0))
    eng.set_scale(1000000)

    mgr = pyfvw.overlay.OverlayManager()
    cross = Crosshair()
    mgr.add(cross)
    mgr.add(pyfvw.overlay.GridOverlay())

    cv = pyfvw.canvas.CpuCanvas(64, 64)
    cv.clear((0, 0, 0))
    mgr.draw_all(eng.proj, cv)
    a = np.asarray(cv.buffer)
    assert a[20, 15, 0] == 255  # python overlay drew

    assert mgr.route_mouse_down(pyfvw.overlay.MouseEvent(5, 6))
    assert cross.clicks == [(5, 6)]
    cross.visible = False
    assert not mgr.route_mouse_down(pyfvw.overlay.MouseEvent(7, 8))

    # trampoline lifetime: a TEMPORARY python overlay must keep its
    # overrides (keep_alive on add) — and its exception must surface as
    # FvError, never cross the SPI raw
    import gc

    class Broken(pyfvw.overlay.Overlay):
        def __init__(self):
            super().__init__("broken")

        def on_draw(self, proj, canvas):
            raise RuntimeError("oops")

    mgr2 = pyfvw.overlay.OverlayManager()
    mgr2.add(Broken())  # no python reference kept on purpose
    gc.collect()
    with pytest.raises(pyfvw.FvError) as ei:
        mgr2.draw_all(eng.proj, cv)
    assert "broken" in ei.value.message and "oops" in ei.value.message


def test_overlay_key_event():
    """KeyEvent reaches a Python overlay whole (2026-08-04).

    Before this the SPI passed a bare int with no documented numbering, so a
    Python overlay could not tell Ctrl-Z from Z and no UI toolkit had a
    correct value to send: tkinter's own `keycode` is a platform-specific
    composite (Left arrives as 2063660802 on Aqua).
    """

    class Keys(pyfvw.overlay.Overlay):
        def __init__(self, handles):
            super().__init__("keys")
            self.seen = []
            self.handles = handles

        def on_key_down(self, e):
            self.seen.append((e.key, e.text, e.shift, e.ctrl, e.alt, e.meta))
            return self.handles

    k = pyfvw.overlay.key
    # Win32 VK values, pinned on the Python side too — these are what an
    # overlay author writes, so a renumbering must break here as well.
    assert (k.LEFT, k.UP, k.RIGHT, k.DOWN) == (0x25, 0x26, 0x27, 0x28)
    assert (k.PAGE_UP, k.PAGE_DOWN) == (0x21, 0x22)
    assert (k.ESCAPE, k.RETURN, k.DELETE, k.SPACE) == (0x1B, 0x0D, 0x2E, 0x20)
    assert k.F1 == 0x70 and k.F12 == 0x7B
    assert k.NONE == 0
    # Letters and digits need no constant at all.
    assert ord("A") == 0x41 and ord("0") == 0x30

    mgr = pyfvw.overlay.OverlayManager()
    top = Keys(handles=False)
    bottom = Keys(handles=True)
    mgr.add(bottom)
    mgr.add(top)  # added last == on top == routed first

    e = pyfvw.overlay.KeyEvent(key=ord("Z"), text=ord("z"), ctrl=True)
    assert mgr.route_key_down(e)
    assert top.seen == [(ord("Z"), ord("z"), False, True, False, False)]
    assert bottom.seen == top.seen  # top declined, so it fell through

    # Defaults: an unmodified named key carries no text.
    bottom.seen.clear()
    top.seen.clear()
    assert mgr.route_key_down(pyfvw.overlay.KeyEvent(key=k.DELETE))
    assert top.seen == [(k.DELETE, 0, False, False, False, False)]

    # An overlay that raises declines rather than propagating (contracts D3).
    class Angry(pyfvw.overlay.Overlay):
        def __init__(self):
            super().__init__("angry")

        def on_key_down(self, e):
            raise RuntimeError("nope")

    mgr2 = pyfvw.overlay.OverlayManager()
    mgr2.add(Angry())
    assert not mgr2.route_key_down(pyfvw.overlay.KeyEvent(key=k.LEFT))


def test_tk_key_adapter():
    """The tkinter -> KeyEvent mapping, without tkinter (2026-08-04).

    port/apps/tk_keys.py takes anything with .keysym/.char/.state, which is
    exactly so this can run with no display, no event loop and no window —
    and so the per-platform modifier bits are asserted rather than assumed.
    """
    import sys

    sys.path.insert(0, os.path.join(os.path.dirname(__file__),
                                    "..", "..", "..", "apps"))
    try:
        import tk_keys
    finally:
        sys.path.pop(0)

    class Ev:
        def __init__(self, keysym, char="", state=0):
            self.keysym, self.char, self.state = keysym, char, state
            # The field that LOOKS right and is not; present here so a
            # future "simplification" to event.keycode fails this test.
            self.keycode = 2063660802

    k = pyfvw.overlay.key

    # Named keys, using Tk's own spellings (note Prior/Next are Page Up/Down).
    assert tk_keys.key_event(Ev("Left")).key == k.LEFT
    assert tk_keys.key_event(Ev("Prior")).key == k.PAGE_UP
    assert tk_keys.key_event(Ev("Next")).key == k.PAGE_DOWN
    assert tk_keys.key_event(Ev("Escape")).key == k.ESCAPE
    assert tk_keys.key_event(Ev("BackSpace")).key == k.BACKSPACE
    assert tk_keys.key_event(Ev("F7")).key == k.F1 + 6
    assert tk_keys.key_event(Ev("space", " ")).key == k.SPACE

    # Letters fold to their uppercase VK whichever case was typed, and the
    # CASE survives in `text` — that is the whole reason for two fields.
    lower = tk_keys.key_event(Ev("a", "a"))
    upper = tk_keys.key_event(Ev("A", "A", state=0x1))
    assert lower.key == upper.key == ord("A")
    assert lower.text == ord("a") and upper.text == ord("A")
    assert upper.shift and not lower.shift

    # Punctuation is deliberately unnamed: VK_OEM_* is layout-specific, so
    # `text` carries it and `key` says "no portable name".
    plus = tk_keys.key_event(Ev("+", "+", state=0x1))
    assert plus.key == k.NONE and plus.text == ord("+")

    # Modifier bits, per platform. The trap: 0x8 is Alt on X11 and COMMAND
    # on Aqua, so a single hardcoded mask would turn every macOS Cmd
    # shortcut into an Alt shortcut.
    assert tk_keys.key_event(Ev("a", "a", state=0x4)).ctrl
    if sys.platform == "darwin":
        assert tk_keys.key_event(Ev("a", "a", state=0x20000)).alt
        assert tk_keys.key_event(Ev("a", "a", state=0x8)).meta
        assert not tk_keys.key_event(Ev("a", "a", state=0x8)).alt
    else:
        assert tk_keys.key_event(Ev("a", "a", state=0x8)).alt

    # A control character is not text an overlay should insert; `key` and
    # `ctrl` carry the meaning instead.
    ctrl_c = tk_keys.key_event(Ev("c", "\x03", state=0x4))
    assert ctrl_c.key == ord("C") and ctrl_c.text == 0 and ctrl_c.ctrl

    # Pressing Shift itself is not a key press an overlay wants.
    assert tk_keys.is_modifier(Ev("Shift_L"))
    assert not tk_keys.is_modifier(Ev("Left"))

    # An unknown keysym is key=0, not a crash and not a wrong guess.
    assert tk_keys.key_event(Ev("XF86AudioPlay")).key == k.NONE


def test_cadrg_cache_lru():
    root = _testdata("rpf")
    if root is None:
        pytest.skip("no TestData")
    frames = pyfvw.formats.CadrgFrameEnumerator().frames(os.path.join(root, "cgnc"))
    if len(frames) < 3:
        pytest.skip("need >=3 frames")
    cache = pyfvw.formats.CadrgFrameCache(2)
    a = cache.get(frames[0].path)
    cache.get(frames[1].path)
    assert cache.size == 2
    assert cache.get(frames[0].path) is a  # hit returns same object
    cache.get(frames[2].path)              # evicts frames[1]
    assert cache.size == 2


# ---------------------------------------------------------------------------
# vector charting: VPF/DNC -> GeoSym -> VectorRenderer (V5c pyfvw slice)
# ---------------------------------------------------------------------------

def _harbor():
    return _testdata(os.path.join("vpf", "dnc17", "h1707300"))


def test_vpf_source_open_layers_bounds():
    lib = _harbor()
    if lib is None:
        pytest.skip("no dnc17 TestData")
    s = pyfvw.vector.VpfVectorSource()
    s.open(lib)
    assert s.is_open()
    layers = s.layers()
    assert len(layers) == 36                 # 25 point/line + 11 area (V5c)
    for want in ("coastl", "hydline", "soundp", "hydarea", "ecrarea"):
        assert want in layers
    b = s.bounds                              # Cape Cod / Nantucket Sound
    assert 41.5 < b.ll.lat < b.ur.lat < 41.9
    assert -70.1 < b.ll.lon < b.ur.lon < -69.7


def test_vpf_render_through_geosym():
    lib = _harbor()
    if lib is None:
        pytest.skip("no dnc17 TestData")
    data_dir = os.environ["FVW_TESTDATA_DIR"]
    if not os.path.isdir(os.path.join(data_dir, "GeoSymbol")):
        pytest.skip("no GeoSym assets")

    src = pyfvw.vector.VpfVectorSource()
    src.open(lib)
    style = pyfvw.vector.GeoSymStyleEngine()
    style.open(data_dir, pyfvw.vector.GEOSYM_DNC)

    proj = pyfvw.engine.MapProjection()
    proj.set_surface_size(256, 256)
    proj.set_center(pyfvw.geo.GeoPoint(41.70, -69.90))
    proj.set_physical_scale(300000, 0.25)
    # Correct latitude-dependent aspect: a lon pixel spans 1/cos(lat) more
    # degrees than a lat pixel, so ground cells are square.
    ratio = proj.deg_per_pixel_lon / proj.deg_per_pixel_lat
    assert math.isclose(ratio, 1.0 / math.cos(math.radians(41.70)), rel_tol=0.02)

    canvas = pyfvw.canvas.CpuCanvas(256, 256)
    canvas.clear((255, 255, 255))
    r = pyfvw.vector.VectorRenderer(src, style)
    r.render(proj, canvas)

    assert r.features_queried > 100
    assert r.draws_emitted > 100
    arr = np.asarray(canvas.buffer)
    nonwhite = int((arr[:, :, :3] != 255).any(axis=2).sum())
    assert nonwhite > 1000, "chart came out blank"


def test_vpf_scale_and_feature_zoom_are_independent():
    """The demo's two knobs: map scale keeps symbol pixel-size fixed; feature
    zoom changes it without moving the map."""
    lib = _harbor()
    if lib is None:
        pytest.skip("no dnc17 TestData")
    data_dir = os.environ["FVW_TESTDATA_DIR"]
    if not os.path.isdir(os.path.join(data_dir, "GeoSymbol")):
        pytest.skip("no GeoSym assets")

    def render(scale, fscale):
        src = pyfvw.vector.VpfVectorSource(); src.open(lib)
        style = pyfvw.vector.GeoSymStyleEngine(); style.open(data_dir, pyfvw.vector.GEOSYM_DNC)
        proj = pyfvw.engine.MapProjection()
        proj.set_surface_size(200, 200)
        proj.set_center(pyfvw.geo.GeoPoint(41.69, -69.95))
        proj.set_physical_scale(scale, 0.25)
        cv = pyfvw.canvas.CpuCanvas(200, 200); cv.clear((255, 255, 255))
        r = pyfvw.vector.VectorRenderer(src, style)
        r.set_symbol_scale(fscale); r.set_device_dpi(96 * fscale)
        r.render(proj, cv)
        arr = np.asarray(cv.buffer)
        return int((arr[:, :, :3] != 255).any(axis=2).sum())

    base = render(150000, 1.0)
    zoomed_features = render(150000, 2.5)   # bigger symbology, same ground
    smaller_scale = render(400000, 1.0)     # more ground, same symbol size
    assert zoomed_features > base           # features grew -> more ink
    assert base > 0 and smaller_scale > 0


# ---------------------------------------------------------------------------
# identify: pick index over the drawn scene + Describe (plan §5.3, R1)
# ---------------------------------------------------------------------------

def test_vpf_describe_decodes_a_feature():
    lib = _harbor()
    if lib is None:
        pytest.skip("no dnc17 TestData")
    s = pyfvw.vector.VpfVectorSource()
    s.open(lib)

    ref = pyfvw.vector.FeatureRef()
    ref.layer = s.layers().index("hydline")
    ref.feature = 1
    d = s.describe(ref)

    assert d.title == "Depth Curve"          # CHAR.VDT decode of f_code BE010
    assert d.layer_name == "hydline"
    assert "h1707300" in d.source_note
    by_code = {a.code: a for a in d.attributes}
    assert by_code["f_code"].raw == "BE010"
    assert by_code["f_code"].display == "Depth Curve"
    assert by_code["acc"].name == "Accuracy Category"
    assert "id" not in by_code and "edg_id" not in by_code   # join keys hidden

    bad = pyfvw.vector.FeatureRef()           # layer -1 -> invalid
    with pytest.raises(pyfvw.FvError):
        s.describe(bad)


def test_vpf_click_to_identify_end_to_end():
    """What the pan-viewer does on a click: render, hit-test the DRAWN scene,
    describe each hit."""
    lib = _harbor()
    if lib is None:
        pytest.skip("no dnc17 TestData")
    data_dir = os.environ["FVW_TESTDATA_DIR"]
    if not os.path.isdir(os.path.join(data_dir, "GeoSymbol")):
        pytest.skip("no GeoSym assets")

    src = pyfvw.vector.VpfVectorSource(); src.open(lib)
    style = pyfvw.vector.GeoSymStyleEngine()
    style.open(data_dir, pyfvw.vector.GEOSYM_DNC)

    proj = pyfvw.engine.MapProjection()
    proj.set_surface_size(256, 256)
    proj.set_center(pyfvw.geo.GeoPoint(41.70, -69.90))
    proj.set_physical_scale(300000, 0.25)

    canvas = pyfvw.canvas.CpuCanvas(256, 256)
    canvas.clear((255, 255, 255))
    r = pyfvw.vector.VectorRenderer(src, style)
    assert r.pick_enabled
    r.render(proj, canvas)
    assert len(r.pick_index) > 100, "nothing was indexed"

    # Sweep the canvas: every pixel with ink should be tappable, and every hit
    # must resolve to a description.
    arr = np.asarray(canvas.buffer)
    described, hit_points = 0, 0
    for y in range(0, 256, 8):
        for x in range(0, 256, 8):
            hits = r.pick_index.hit_test(x, y, 4.0)
            if not hits:
                continue
            hit_points += 1
            # Topmost first, by display priority.
            assert hits[0].priority >= hits[-1].priority
            d = src.describe(hits[0].ref)
            assert d.title and d.layer_name
            described += 1
    assert hit_points > 20, "the chart drew but nothing is pickable"
    assert described == hit_points

    # White space away from the chart hits nothing.
    empty = [ (x, y) for y in range(0, 256, 4) for x in range(0, 256, 4)
              if (arr[y, x, :3] == 255).all() ]
    assert empty
    misses = sum(1 for x, y in empty if not r.pick_index.hit_test(x, y, 0.0))
    assert misses > len(empty) * 0.5, "blank pixels are reporting hits"


def test_pick_index_can_be_disabled():
    lib = _harbor()
    if lib is None:
        pytest.skip("no dnc17 TestData")
    data_dir = os.environ["FVW_TESTDATA_DIR"]
    if not os.path.isdir(os.path.join(data_dir, "GeoSymbol")):
        pytest.skip("no GeoSym assets")
    src = pyfvw.vector.VpfVectorSource(); src.open(lib)
    style = pyfvw.vector.GeoSymStyleEngine()
    style.open(data_dir, pyfvw.vector.GEOSYM_DNC)
    proj = pyfvw.engine.MapProjection()
    proj.set_surface_size(128, 128)
    proj.set_center(pyfvw.geo.GeoPoint(41.70, -69.90))
    proj.set_physical_scale(300000, 0.25)
    cv = pyfvw.canvas.CpuCanvas(128, 128); cv.clear((255, 255, 255))
    r = pyfvw.vector.VectorRenderer(src, style)
    r.set_pick_enabled(False)
    r.render(proj, cv)
    assert r.draws_emitted > 0
    assert len(r.pick_index) == 0


# --- R2: the cross-product rule layer --------------------------------------

def test_rules_parse_errors_raise_fverror():
    """The rule syntax is hand-authored, so a bad line must fail loudly and
    leave the set untouched (all-or-nothing, same as the C++ side)."""
    rs = pyfvw.vector.RuleSet()
    rs.load_text("hide layer=hydline\n")
    assert len(rs) == 1
    with pytest.raises(pyfvw.FvError) as e:
        rs.load_text("hide key=BE010\nhide colour=red\n")
    assert "line 2" in str(e.value)
    assert len(rs) == 1, "the failed load must not have added anything"


def test_geosym_rules_and_viewing_groups_change_the_render():
    """The engine's rules() and viewing_groups() are live references, and both
    knobs visibly thin a real DNC chart."""
    lib = _harbor()
    if lib is None:
        pytest.skip("no dnc17 TestData")
    data_dir = os.environ["FVW_TESTDATA_DIR"]
    if not os.path.isdir(os.path.join(data_dir, "GeoSymbol")):
        pytest.skip("no GeoSym assets")

    def render(configure=None):
        src = pyfvw.vector.VpfVectorSource(); src.open(lib)
        style = pyfvw.vector.GeoSymStyleEngine()
        style.open(data_dir, pyfvw.vector.GEOSYM_DNC)
        if configure is not None:
            configure(style)
        proj = pyfvw.engine.MapProjection()
        proj.set_surface_size(256, 256)
        proj.set_center(pyfvw.geo.GeoPoint(41.70, -69.90))
        proj.set_physical_scale(300000, 0.25)
        cv = pyfvw.canvas.CpuCanvas(256, 256); cv.clear((255, 255, 255))
        r = pyfvw.vector.VectorRenderer(src, style)
        r.render(proj, cv)
        return r.draws_emitted, style.rule_predicate_evaluations

    base, evals = render()
    assert base > 100
    assert evals == 0, "no rules means no predicate work"

    # A key-only rule file still costs zero predicate evaluations.
    def with_rules(style):
        style.rules().load_text("hide key=BE010\n")
    hidden, evals = render(with_rules)
    assert hidden < base
    assert evals == 0

    # Display Base only: far less survives than the full Other set.
    def base_only(style):
        style.viewing_groups().set_max_category(pyfvw.vector.DISPLAY_BASE)
    decluttered, _ = render(base_only)
    assert 0 < decluttered < base


def test_dnc_mariner_depth_ramp_follows_the_safety_contour():
    """The DNC half of the mariner API: GeoSym's ssdc/msdc/mssc under their
    S-52 names. Moving the safety contour re-shades the depth areas — until
    this landed, DNC drew CECDISValues' defaults and a draft could not be
    entered at all."""
    lib = _harbor()
    if lib is None:
        pytest.skip("no dnc17 TestData")
    data_dir = os.environ["FVW_TESTDATA_DIR"]
    if not os.path.isdir(os.path.join(data_dir, "GeoSymbol")):
        pytest.skip("no GeoSym assets")

    src = pyfvw.vector.VpfVectorSource(); src.open(lib)
    style = pyfvw.vector.GeoSymStyleEngine()
    style.open(data_dir, pyfvw.vector.GEOSYM_DNC)
    # GeoSym's own defaults, which are NOT the struct's (those are S-52's).
    assert style.mariner().safety_contour == 10.0
    assert style.mariner().shallow_pattern is True

    proj = pyfvw.engine.MapProjection()
    proj.set_surface_size(200, 200)
    proj.set_center(pyfvw.geo.GeoPoint(41.70, -69.90))
    proj.set_physical_scale(300000, 0.25)
    r = pyfvw.vector.VectorRenderer(src, style)

    def shot(contour):
        m = pyfvw.vector.MarinerSettings()
        m.safety_contour = contour
        m.deep_contour = 30.0
        m.shallow_contour = 2.0
        style.set_mariner(m)
        cv = pyfvw.canvas.CpuCanvas(200, 200); cv.clear((255, 255, 255))
        r.render(proj, cv)
        return np.asarray(cv.buffer).copy()

    shallow_ship = shot(5.0)
    deep_ship = shot(25.0)
    assert not np.array_equal(shallow_ship, deep_ship), \
        "the safety contour did not move DNC's depth ramp"
    # Same object on both products — the S-52 spelling still resolves.
    assert pyfvw.vector.S52MarinerSettings is pyfvw.vector.MarinerSettings


def test_family_set_switches_a_group_of_layers_off(tmp_path):
    """Data families: a name over rule-file selectors, so switching one off is
    just hide rules in the engine's RuleSet."""
    fams = pyfvw.vector.FamilySet()
    fams.load_json("""{
      "product": "dnc",
      "families": [
        {"name": "navaids", "title": "Aids to Navigation",
         "select": ["layer=buoybcnp", "layer=lightsp"]},
        {"name": "bottom", "select": ["layer=botcharp"]}
      ]
    }""")
    assert len(fams) == 2
    assert fams.product == "dnc"
    assert [f.name for f in fams.families] == ["navaids", "bottom"]
    assert fams.enabled("navaids")
    assert fams.enabled("not_declared")      # unknown names hide nothing

    rules = pyfvw.vector.RuleSet()
    fams.append_rules(rules)
    assert len(rules) == 0, "everything on means no rules at all"

    fams.set_enabled("navaids", False)
    assert fams.disabled_count == 1
    fams.append_rules(rules)
    assert len(rules) == 2                    # one hide per selector

    with pytest.raises(KeyError):
        fams.set_enabled("no_such_family", False)


def test_shipped_family_files_load():
    """The three files under port/families/ are the settings surface for this
    feature, so a user's first contact with it must parse. Which families are
    ON is deliberately NOT asserted — those are the user's settings, and
    editing them must not break the build."""
    here = os.path.dirname(os.path.abspath(__file__))
    families = os.path.normpath(
        os.path.join(here, "..", "..", "..", "families"))
    if not os.path.isdir(families):
        pytest.skip("no port/families directory")
    for name, product in (("dnc", "dnc"), ("enc", "enc"), ("osm", "osm")):
        fams = pyfvw.vector.FamilySet()
        fams.load_file(os.path.join(families, name + "-families.json"))
        assert fams.product == product
        assert len(fams) >= 8
        rules = pyfvw.vector.RuleSet()
        fams.append_rules(rules)
        assert len(rules) == sum(len(f.select) for f in fams.families
                                 if not f.enabled)


def test_dnc_families_thin_a_real_chart():
    lib = _harbor()
    if lib is None:
        pytest.skip("no dnc17 TestData")
    data_dir = os.environ["FVW_TESTDATA_DIR"]
    if not os.path.isdir(os.path.join(data_dir, "GeoSymbol")):
        pytest.skip("no GeoSym assets")

    def render(off=None):
        src = pyfvw.vector.VpfVectorSource(); src.open(lib)
        style = pyfvw.vector.GeoSymStyleEngine()
        style.open(data_dir, pyfvw.vector.GEOSYM_DNC)
        if off:
            fams = pyfvw.vector.FamilySet()
            fams.load_json("""{"families": [
              {"name": "depths",
               "select": ["layer=hydarea", "layer=hydline", "layer=soundp"]},
              {"name": "navaids",
               "select": ["layer=buoybcnp", "layer=lightsp"]}]}""")
            for name in off:
                fams.set_enabled(name, False)
            fams.append_rules(style.rules())
        proj = pyfvw.engine.MapProjection()
        proj.set_surface_size(256, 256)
        proj.set_center(pyfvw.geo.GeoPoint(41.70, -69.90))
        proj.set_physical_scale(300000, 0.25)
        cv = pyfvw.canvas.CpuCanvas(256, 256); cv.clear((255, 255, 255))
        r = pyfvw.vector.VectorRenderer(src, style)
        r.render(proj, cv)
        return r.draws_emitted

    full = render()
    without_depths = render(["depths"])
    assert 0 < without_depths < full


# --- settings (the registry replacement) ------------------------------------

def test_settings_read_the_r3a_knobs(tmp_path):
    """The knobs a user is meant to try out, reached from a hand-edited file."""
    ini = tmp_path / "peregrine.ini"
    ini.write_text(
        "# a comment\n"
        "[vector]\n"
        "scene_margin    = 0.5   # trailing comment\n"
        "simplify_pixels = 1.0\n")

    cfg = pyfvw.Settings()
    cfg.load(str(ini))
    assert cfg.path == str(ini)
    assert cfg.get_float("vector.scene_margin", 0.0) == 0.5
    assert cfg.get_float("vector.simplify_pixels", 0.0) == 1.0
    # A key nobody set keeps the caller's default, silently.
    assert cfg.get_float("vector.nope", 0.25) == 0.25
    assert cfg.warnings == []


def test_settings_typo_warns_and_falls_back(tmp_path):
    ini = tmp_path / "peregrine.ini"
    ini.write_text("[vector]\nsimplify_pixels = 0.5px\n")
    cfg = pyfvw.Settings()
    cfg.load(str(ini))
    assert cfg.get_float("vector.simplify_pixels", 0.0) == 0.0
    assert len(cfg.warnings) == 1
    assert "simplify_pixels" in cfg.warnings[0]


def test_settings_malformed_file_raises_with_the_line_number(tmp_path):
    ini = tmp_path / "peregrine.ini"
    ini.write_text("[vector]\nnot a setting\n")
    cfg = pyfvw.Settings()
    with pytest.raises(pyfvw.FvError) as e:
        cfg.load(str(ini))
    assert ":2:" in e.value.message


def test_settings_no_file_anywhere_is_not_an_error(tmp_path, monkeypatch):
    monkeypatch.setenv("FVW_SETTINGS", str(tmp_path / "nope.ini"))
    monkeypatch.chdir(tmp_path)
    monkeypatch.setenv("HOME", str(tmp_path / "nohome"))
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "nohome"))
    cfg = pyfvw.Settings()
    cfg.load()  # '' = the default search path
    assert cfg.path == ""
    assert cfg.keys() == []
    assert pyfvw.default_settings_paths()[0].endswith("nope.ini")


def test_settings_drive_a_real_renderer(tmp_path):
    """End to end: the file changes what the renderer does."""
    lib = _harbor()
    data_dir = os.environ.get("FVW_TESTDATA_DIR")
    if not lib or not data_dir or not _testdata("GeoSymbol"):
        pytest.skip("no dnc17 / GeoSym test data")

    ini = tmp_path / "peregrine.ini"
    ini.write_text("[vector]\nsimplify_pixels = 1.0\n")
    cfg = pyfvw.Settings()
    cfg.load(str(ini))

    src = pyfvw.vector.VpfVectorSource(); src.open(lib)
    style = pyfvw.vector.GeoSymStyleEngine()
    style.open(data_dir, pyfvw.vector.GEOSYM_DNC)
    proj = pyfvw.engine.MapProjection()
    proj.set_surface_size(256, 256)
    proj.set_center(pyfvw.geo.GeoPoint(41.70, -69.90))
    proj.set_resolution(0.0008, 0.0008)
    cv = pyfvw.canvas.CpuCanvas(256, 256); cv.clear((255, 255, 255))

    r = pyfvw.vector.VectorRenderer(src, style)
    r.set_simplify_pixels(cfg.get_float("vector.simplify_pixels", 0.0))
    r.render(proj, cv)
    assert r.simplify_pixels == 1.0
    assert r.scene_vertices < r.scene_vertices_in


# ---------------------------------------------------------------------------
# ENC (S-57 + S-52) through the same seam — phase E4
# ---------------------------------------------------------------------------
#
# The point of these is not that ENC works (the gtests pin that over the same
# cells); it is that ENC reaches Python through the SAME classes DNC does, so
# an app can switch products without a second code path. Each test below has a
# VPF twin above it.


def _enc():
    return _testdata("enc")


def test_enc_registers_as_a_scannable_format():
    pyfvw.catalog.register_builtin_formats()
    # One call registers ENC too, even though C++ has to register it from a
    # different library (fv_enc links fv_fvkit, so fvkit cannot name it).
    assert "enc" in pyfvw.catalog.registered_format_keys()


def test_enc_source_open_layers_bounds():
    root = _enc()
    if root is None:
        pytest.skip("no ENC TestData")
    s = pyfvw.vector.EncVectorSource()
    s.open(root)
    assert s.is_open()
    # The corpus under TestData/enc grows (four coarser cells arrived
    # 2026-07-28 beside the four Charleston harbour ones), so this counts what
    # is on disk rather than pinning a total that a data drop moves.
    on_disk = len(glob.glob(os.path.join(root, "**", "*.000"), recursive=True))
    assert on_disk >= 4
    assert s.cell_count == on_disk
    # Layers are S-57 object-class ACRONYMS, not VPF table names.
    layers = s.layers()
    for want in ("DEPARE", "SOUNDG", "LNDARE", "COALNE"):
        assert want in layers
    # Whatever else is loaded, the Charleston harbour is inside the union.
    b = s.bounds
    assert b.ll.lat <= 32.70 and b.ur.lat >= 32.85
    assert b.ll.lon <= -80.025 and b.ur.lon >= -79.875
    # These cells ship update files the reader does not apply, and saying so
    # is a navigational duty, not a diagnostic nicety.
    assert "BASE EDITION" in s.staleness_warning


def test_enc_open_cells_is_one_usage_band():
    """A series is a usage band, and only naming its cells can say which one.

    TestData/enc holds bands 2-5 over the same water, so a source opened on
    the directory serves all of them at once — which is how the app came to
    draw a 1:1,000,000 general cell under a 1:12,000 harbour chart.
    """
    root = _enc()
    if root is None:
        pytest.skip("no ENC TestData")
    harbour = sorted(glob.glob(os.path.join(root, "US5CHS*", "US5CHS*.000")))
    if not harbour:
        pytest.skip("no Charleston harbour cells")

    band = pyfvw.vector.EncVectorSource()
    band.open_cells(harbour, root)
    assert band.cell_count == len(harbour)
    b = band.bounds
    assert 32.6 < b.ll.lat < b.ur.lat < 32.9
    assert -80.1 < b.ll.lon < b.ur.lon < -79.8

    whole = pyfvw.vector.EncVectorSource()
    whole.open(root, root)
    assert whole.cell_count >= band.cell_count
    if whole.cell_count > band.cell_count:
        # The coarser bands reach well past the harbour box.
        assert whole.bounds.ur.lat > b.ur.lat

    with pytest.raises(pyfvw.FvError):
        pyfvw.vector.EncVectorSource().open_cells([], root)


def test_enc_render_through_s52():
    root = _enc()
    if root is None:
        pytest.skip("no ENC TestData")
    if not os.path.isfile(os.path.join(root, "chartsymbols.xml")):
        pytest.skip("no S-52 presentation library")

    src = pyfvw.vector.EncVectorSource()
    src.open(root)
    style = pyfvw.vector.S52StyleEngine()
    style.open(root)

    proj = pyfvw.engine.MapProjection()
    proj.set_surface_size(256, 256)
    proj.set_center(pyfvw.geo.GeoPoint(32.775, -79.95))
    proj.set_resolution(0.0004, 0.0004)

    canvas = pyfvw.canvas.CpuCanvas(256, 256)
    canvas.clear((255, 255, 255))
    # Same renderer class, same call, as the VPF/GeoSym test above.
    r = pyfvw.vector.VectorRenderer(src, style)
    r.render(proj, canvas)

    assert r.features_queried > 100
    assert r.draws_emitted > 100
    arr = np.asarray(canvas.buffer)
    nonwhite = int((arr[:, :, :3] != 255).any(axis=2).sum())
    assert nonwhite > 1000, "chart came out blank"
    # Every CS procedure the Charleston cells reach is implemented (E3b).
    assert dict(style.unhandled_cs) == {}


def test_enc_mariner_safety_contour_changes_the_chart():
    """S-52's safety contour is a MARINER setting that changes what is drawn,
    not a preference — moving it must move pixels."""
    root = _enc()
    if root is None:
        pytest.skip("no ENC TestData")
    if not os.path.isfile(os.path.join(root, "chartsymbols.xml")):
        pytest.skip("no S-52 presentation library")

    src = pyfvw.vector.EncVectorSource()
    src.open(root)
    style = pyfvw.vector.S52StyleEngine()
    style.open(root)
    proj = pyfvw.engine.MapProjection()
    proj.set_surface_size(200, 200)
    proj.set_center(pyfvw.geo.GeoPoint(32.775, -79.95))
    proj.set_resolution(0.0004, 0.0004)
    r = pyfvw.vector.VectorRenderer(src, style)

    def shot(contour):
        style.mariner().safety_contour = contour
        cv = pyfvw.canvas.CpuCanvas(200, 200)
        cv.clear((255, 255, 255))
        r.render(proj, cv)
        return np.asarray(cv.buffer).copy()

    shallow = shot(2.0)
    deep = shot(30.0)
    assert not np.array_equal(shallow, deep), \
        "the safety contour did not move the depth ramp"


def test_enc_color_scheme_round_trips():
    root = _enc()
    if root is None:
        pytest.skip("no ENC TestData")
    if not os.path.isfile(os.path.join(root, "chartsymbols.xml")):
        pytest.skip("no S-52 presentation library")
    style = pyfvw.vector.S52StyleEngine()
    style.open(root)
    assert style.color_scheme == pyfvw.vector.S52_DAY
    style.set_color_scheme(pyfvw.vector.S52_NIGHT)
    assert style.color_scheme == pyfvw.vector.S52_NIGHT


# ---------------------------------------------------------------------------
# OSM (MVT pyramid + MapLibre style) through the same seam — phase O3
# ---------------------------------------------------------------------------
#
# The port's THIRD vector product, and these assert the same thing the ENC
# block above does: it arrives through the classes DNC and ENC already use, so
# PythonView forks in exactly one method. What is genuinely new is that SCALE
# picks a pyramid level, and that the source and the style engine have to be
# told the same latitude and pixel pitch or they disagree about which level
# that is — so that pairing is what gets pinned here.

_OSM_STYLE = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "..", "..", "Osm", "styles", "peregrine-osm.json")


def _mbtiles():
    d = _testdata("OSM")
    if d is None:
        return None
    p = os.path.join(d, "mbtiles", "us-south.mbtiles")
    return p if os.path.isfile(p) else None


def test_osm_registers_as_a_scannable_format():
    pyfvw.catalog.register_builtin_formats()
    assert "osm" in pyfvw.catalog.registered_format_keys()


def test_osm_source_open_layers_bounds():
    path = _mbtiles()
    if path is None:
        pytest.skip("no OSM TestData")
    s = pyfvw.vector.OsmVectorSource()
    s.open(path)
    assert s.is_open()
    assert s.min_zoom == 0 and s.max_zoom == 14
    # Layers are the OpenMapTiles schema's, not a product dictionary's.
    for want in ("water", "transportation", "building", "place"):
        assert want in s.layers()
    # Bounds are DERIVED from the tile index: an MBTiles `bounds` value cannot
    # be trusted (see Mbtiles.BoundsAreDerivedFromTheTilesNotBelieved — the
    # same declared number was a lie in one cut of this file and the truth in
    # the next). The 2026-08-17 re-cut runs east to the Greenwich meridian, so
    # the standing claim is the latitude band and the western edge.
    b = s.bounds
    assert b.contains(pyfvw.geo.GeoPoint(33.749, -84.388))
    assert b.ur.lon <= 0.0 and b.ll.lon > -110.0
    assert b.ll.lat > 20.0 and b.ur.lat < 45.0


def test_osm_scale_picks_the_zoom_and_clipping_is_on():
    path = _mbtiles()
    if path is None:
        pytest.skip("no OSM TestData")
    src = pyfvw.vector.OsmVectorSource()
    src.open(path)
    style = pyfvw.vector.OsmStyleEngine()
    style.load_file(_OSM_STYLE)
    style.set_reference_latitude(33.755)
    style.set_display_mm_per_pixel(src.display_mm_per_pixel)

    proj = pyfvw.engine.MapProjection()
    proj.set_surface_size(256, 256)
    proj.set_center(pyfvw.geo.GeoPoint(33.755, -84.390))
    proj.set_physical_scale(25000.0, 0.25)
    cv = pyfvw.canvas.CpuCanvas(256, 256)
    cv.clear(style.background(25000.0)[:3])
    r = pyfvw.vector.VectorRenderer(src, style)
    r.render(proj, cv)

    assert src.last_query_zoom == 14
    assert src.last_query_tiles_read >= 1
    assert src.clip_to_tile
    assert src.last_query_clipped > 0
    # 1:25,000 is z14.35 here and the pyramid stops at 14, so a third of a
    # level of overzoom is expected and reported; it is only ever 0 at a
    # scale a level actually covers.
    assert 0.0 < src.last_query_overzoom < 0.5
    assert r.features_queried > 100
    assert r.draws_emitted > 100
    # A picture, not a flat fill.
    arr = np.asarray(cv.buffer)
    assert len(np.unique(arr.reshape(-1, 4), axis=0)) > 8


def test_osm_overzoom_does_not_go_blank():
    path = _mbtiles()
    if path is None:
        pytest.skip("no OSM TestData")
    src = pyfvw.vector.OsmVectorSource()
    src.open(path)
    q_scale = 2000.0                       # about z18; the pyramid stops at 14
    proj = pyfvw.engine.MapProjection()
    proj.set_surface_size(128, 128)
    proj.set_center(pyfvw.geo.GeoPoint(33.755, -84.390))
    proj.set_physical_scale(q_scale, 0.25)
    style = pyfvw.vector.OsmStyleEngine()
    style.load_file(_OSM_STYLE)
    style.set_reference_latitude(33.755)
    cv = pyfvw.canvas.CpuCanvas(128, 128)
    cv.clear((255, 255, 255))
    r = pyfvw.vector.VectorRenderer(src, style)
    r.render(proj, cv)

    assert src.last_query_zoom == src.max_zoom
    assert src.last_query_overzoom > 2.0
    assert r.draws_emitted > 0
    # The style is NOT clamped with the source: it keeps evaluating at the
    # real zoom, which is what makes the map grow instead of freezing.
    assert style.zoom_for_scale(q_scale) > 17.0


def test_osm_style_rejects_what_it_does_not_understand():
    style = pyfvw.vector.OsmStyleEngine()
    style.load_file(_OSM_STYLE)
    name = style.style_name
    with pytest.raises(pyfvw.FvError):
        style.load_text('{"version":8,"layers":[{"id":"x","type":"raster",'
                        '"source-layer":"water"}]}')
    # All-or-nothing: the previous style is still loaded.
    assert style.style_name == name
    assert style.layer_count > 10


# ---------------------------------------------------------------------------
# Road routing — phase O4
# ---------------------------------------------------------------------------
#
# The binding surface the route overlay uses: build or load a graph, snap a
# position to it, ask for a line between two positions. The graph itself is
# tested in C++ (port/Routing/test); what these pin is that the seam survives
# the trip through Python — geometry comes back as GeoPoints, an unreachable
# pair is `found == False` rather than an exception, and an endpoint off the
# network raises FvError rather than silently routing from somewhere else.


def _kiawah_osm():
    d = _testdata("OSM")
    if d is None:
        return None
    paths = [os.path.join(d, n) for n in
             ("map.osm", "map-2.osm", "map-3.osm", "map-4.osm")]
    return paths if all(os.path.isfile(p) for p in paths) else None


@pytest.fixture(scope="module")
def kiawah_graph():
    paths = _kiawah_osm()
    if paths is None:
        pytest.skip("TestData/OSM/map*.osm not present")
    # honor_access=False: Kiawah is a gated island and honouring access=private
    # fragments its driving network into a hundred pieces.
    return pyfvw.routing.RoadGraph.build(paths, honor_access=False)


def test_road_graph_builds_and_reports_its_shape(kiawah_graph):
    assert kiawah_graph.node_count > 500
    assert kiawah_graph.arc_count > 2 * kiawah_graph.node_count / 2
    b = kiawah_graph.bounds
    assert 32.5 < b.ll.lat < 32.7
    assert -80.2 < b.ur.lon < -79.9


def test_nearest_node_snaps_and_reports_the_distance(kiawah_graph):
    hit = kiawah_graph.nearest_node(pyfvw.geo.GeoPoint(32.600, -80.120), 3000.0)
    assert hit is not None
    node, meters = hit
    assert 0 <= node < kiawah_graph.node_count
    assert 0.0 <= meters <= 3000.0
    p = kiawah_graph.location(node)
    assert abs(p.lat - 32.600) < 0.05

    # Out of range is None, not an exception and not node 0.
    assert kiawah_graph.nearest_node(pyfvw.geo.GeoPoint(40.0, -80.0), 500.0) is None


def test_router_returns_a_drawable_line(kiawah_graph):
    router = pyfvw.routing.Router(kiawah_graph)
    route = router.route(pyfvw.geo.GeoPoint(32.590, -80.130),
                         pyfvw.geo.GeoPoint(32.640, -80.005),
                         snap_meters=3000.0)
    assert route.found
    assert route.length_m > 5000.0
    assert route.seconds > 0.0
    assert len(route.geometry) > len(route.nodes)   # road shape, not a chord
    for p in route.geometry:
        assert isinstance(p, pyfvw.geo.GeoPoint)

    # The line starts and ends where the request was snapped ONTO THE ROAD,
    # which since §1d need not be a junction — start_node is the first junction
    # it then reaches. That is what the overlay draws between.
    assert route.geometry[0].lat == pytest.approx(route.start_point.lat)
    assert route.geometry[0].lon == pytest.approx(route.start_point.lon)
    assert route.geometry[-1].lat == pytest.approx(route.end_point.lat)

    # With the arc snap off it is the old answer: the line begins at the node.
    by_node = router.route(pyfvw.geo.GeoPoint(32.590, -80.130),
                           pyfvw.geo.GeoPoint(32.640, -80.005),
                           snap_meters=3000.0, snap_to_arcs=False)
    assert by_node.found
    assert by_node.start_arc == pyfvw.routing.Route.NO_ARC
    start = kiawah_graph.location(by_node.start_node)
    assert by_node.geometry[0].lat == pytest.approx(start.lat)
    assert by_node.geometry[0].lon == pytest.approx(start.lon)
    # And the arc snap did not have to walk to that junction first.
    assert route.start_offset_m <= by_node.start_offset_m

    # Legs add up to the whole and name the roads.
    assert route.legs
    assert sum(l.length_m for l in route.legs) == pytest.approx(route.length_m, abs=1.0)
    assert any(l.name for l in route.legs)
    assert all(l.road_class for l in route.legs)


def test_metric_and_snap_are_honoured(kiawah_graph):
    router = pyfvw.routing.Router(kiawah_graph)
    a = pyfvw.geo.GeoPoint(32.590, -80.130)
    b = pyfvw.geo.GeoPoint(32.640, -80.005)
    fast = router.route(a, b, snap_meters=3000.0, metric="time")
    short = router.route(a, b, snap_meters=3000.0, metric="distance")
    assert fast.found and short.found
    # Each metric is optimal for its own quantity.
    assert fast.seconds <= short.seconds + 1e-6
    assert short.length_m <= fast.length_m + 1e-3

    with pytest.raises(pyfvw.FvError):
        router.route(a, b, metric="cheapest")

    # An endpoint nowhere near a road is an error, not a route from elsewhere.
    with pytest.raises(pyfvw.FvError):
        router.route(pyfvw.geo.GeoPoint(40.0, -80.0), b, snap_meters=500.0)


def test_graph_round_trips_through_a_file(tmp_path, kiawah_graph):
    path = str(tmp_path / "kiawah.fvroad")
    kiawah_graph.save(path)
    loaded = pyfvw.routing.RoadGraph.load(path)
    assert loaded.node_count == kiawah_graph.node_count
    assert loaded.arc_count == kiawah_graph.arc_count

    a = pyfvw.geo.GeoPoint(32.590, -80.130)
    b = pyfvw.geo.GeoPoint(32.640, -80.005)
    first = pyfvw.routing.Router(kiawah_graph).route(a, b, snap_meters=3000.0)
    again = pyfvw.routing.Router(loaded).route(a, b, snap_meters=3000.0)
    assert first.found == again.found
    assert first.seconds == pytest.approx(again.seconds)
    assert len(first.geometry) == len(again.geometry)

    with pytest.raises(pyfvw.FvError):
        pyfvw.routing.RoadGraph.load(str(tmp_path / "absent.fvroad"))


def test_the_route_overlay_follows_roads(tmp_path, kiawah_graph):
    """The app-side path: RouteOverlay.follow_roads over a saved graph."""
    path = str(tmp_path / "kiawah.fvroad")
    kiawah_graph.save(path)

    overlay = _cpp_route("island run", [
        ("WP1", 32.590, -80.130),
        ("WP2", 32.640, -80.005),
    ], graph_path=path)

    assert not overlay.has_plan
    assert overlay.follow_roads(_opts(snap_meters=3000.0))
    assert len(overlay.plan.legs) == 1
    assert len(overlay.plan.legs[0]) > 50
    assert "km" in overlay.status

    # A waypoint nowhere near a road leaves THAT leg straight and says so,
    # rather than discarding the whole route.
    overlay.waypoints = list(overlay.waypoints) + [
        pyfvw.route.RouteWaypoint("WP3", 40.0, -80.0)]
    assert not overlay.follow_roads(_opts(snap_meters=3000.0))
    assert len(overlay.plan.legs) == 2
    assert len(overlay.plan.legs[1]) == 2   # a straight leg: just its ends
    assert "not on the network" in overlay.status

    overlay.clear_roads()
    assert not overlay.has_plan
    assert overlay.status == ""


def test_route_via_goes_through_its_stops(kiawah_graph):
    """O5d: one route through ordered stops, not a string of pairs."""
    router = pyfvw.routing.Router(kiawah_graph)
    a = pyfvw.geo.GeoPoint(32.590, -80.130)
    b = pyfvw.geo.GeoPoint(32.610, -80.070)
    c = pyfvw.geo.GeoPoint(32.640, -80.005)

    via = router.route_via([a, b, c], snap_meters=3000.0)
    assert via.found
    assert len(via.stop_nodes) == 3
    assert len(via.stop_offsets_m) == 3
    assert via.unreachable_leg == pyfvw.routing.Route.NO_LEG

    # Every stop is somewhere on the drawn line, the first at its head and the
    # last at its tail.
    assert list(via.stop_geometry_index) == sorted(via.stop_geometry_index)
    assert via.stop_geometry_index[0] == 0
    assert via.stop_geometry_index[-1] == len(via.geometry) - 1

    # Two stops is exactly route().
    pair = router.route(a, c, snap_meters=3000.0)
    two = router.route_via([a, c], snap_meters=3000.0)
    assert two.found == pair.found
    assert two.length_m == pytest.approx(pair.length_m)
    assert list(two.nodes) == list(pair.nodes)

    # The stop in the middle is a constraint, so it cannot make the route
    # shorter than the direct one.
    assert via.length_m >= pair.length_m - 1e-6


def test_route_via_reports_which_stop_it_cannot_reach(kiawah_graph):
    router = pyfvw.routing.Router(kiawah_graph)
    a = pyfvw.geo.GeoPoint(32.590, -80.130)
    b = pyfvw.geo.GeoPoint(32.640, -80.005)

    # A stop in the middle of the Atlantic is a bad REQUEST, and it says which.
    with pytest.raises(pyfvw.FvError) as err:
        router.route_via([a, pyfvw.geo.GeoPoint(35.0, -70.0), b],
                         snap_meters=3000.0)
    assert err.value.code == pyfvw.OUT_OF_COVERAGE
    assert "stop 1" in err.value.message

    with pytest.raises(pyfvw.FvError):
        router.route_via([a], snap_meters=3000.0)


def test_the_route_overlay_routes_through_its_waypoints(tmp_path, kiawah_graph):
    """The app-side path for O5d: three waypoints come back as ONE route cut
    at the stops, not three separately-routed pairs."""
    path = str(tmp_path / "kiawah-via.fvroad")
    kiawah_graph.save(path)
    overlay = _cpp_route("island run", [
        ("WP1", 32.590, -80.130),
        ("WP2", 32.610, -80.070),
        ("WP3", 32.640, -80.005),
    ], graph_path=path)

    assert overlay.follow_roads(_opts(snap_meters=3000.0))
    legs = overlay.plan.legs
    assert len(legs) == 2                       # one piece per pair of stops
    assert "km" in overlay.status
    assert "pairs" not in overlay.status        # the through route was had

    # The pieces join: each leg ends where the next begins, because they are
    # cuts of one line rather than separate routes.
    for first, second in zip(legs, legs[1:]):
        assert first[-1].lat == pytest.approx(second[0].lat)
        assert first[-1].lon == pytest.approx(second[0].lon)

    # A waypoint in the sea has no through route, so it falls back to pairs
    # and says which answer the user is looking at.
    overlay.waypoints = list(overlay.waypoints) + [
        pyfvw.route.RouteWaypoint("WP4", 40.0, -80.0)]
    assert not overlay.follow_roads(_opts(snap_meters=3000.0))
    assert "pairs" in overlay.status
    assert "not on the network" in overlay.status


def test_the_overlay_says_so_when_no_graph_is_configured():
    # No planner AT ALL, which is what a shell with no [routing] graph key
    # produces -- the overlay says so and keeps its straight legs.
    overlay = pyfvw.route.RouteOverlay("no graph")
    overlay.waypoints = [pyfvw.route.RouteWaypoint("A", 32.6, -80.1),
                         pyfvw.route.RouteWaypoint("B", 32.7, -80.0)]
    assert not overlay.follow_roads()
    assert "no road graph configured" in overlay.status
    assert not overlay.has_plan


# ---------------------------------------------------------------------------
# Tolls and ferries (O5e)
# ---------------------------------------------------------------------------

_BAY_OSM = """<?xml version="1.0" encoding="UTF-8"?>
<osm version="0.6">
 <node id="1" lat="32.7000000" lon="-80.0000000"/>
 <node id="2" lat="32.7000000" lon="-79.9000000"/>
 <node id="3" lat="32.7500000" lon="-79.9500000"/>
 <node id="4" lat="32.4000000" lon="-79.9500000"/>
 <node id="5" lat="32.7510000" lon="-79.9500000"/>
 <way id="201">
  <nd ref="1"/><nd ref="2"/>
  <tag k="highway" v="primary"/><tag k="name" v="Toll Bridge"/>
  <tag k="maxspeed" v="80"/><tag k="toll" v="yes"/>
 </way>
 <way id="202">
  <nd ref="1"/><nd ref="3"/><nd ref="2"/>
  <tag k="route" v="ferry"/><tag k="name" v="Bay Ferry"/>
  <tag k="duration" v="00:45"/>
 </way>
 <way id="203">
  <nd ref="1"/><nd ref="4"/><nd ref="2"/>
  <tag k="highway" v="secondary"/><tag k="name" v="Long Way Round"/>
 </way>
 <way id="204">
  <nd ref="3"/><nd ref="5"/>
  <tag k="highway" v="service"/><tag k="name" v="Island Landing"/>
 </way>
</osm>
"""


@pytest.fixture(scope="module")
def bay_graph(tmp_path_factory):
    """A bay with three crossings — a tolled bridge (fastest), a ferry, and a
    long free road round the head — so which one comes back says what the
    query refused. See the same fixture in port/Routing/test/router_test.cpp."""
    path = tmp_path_factory.mktemp("bay") / "bay.osm"
    path.write_text(_BAY_OSM)
    return pyfvw.routing.RoadGraph.build([str(path)])


def _crossing(route):
    return route.legs[0].name if route.legs else "(none)"


def test_toll_and_ferry_penalties_steer_the_crossing(bay_graph):
    router = pyfvw.routing.Router(bay_graph)
    west = pyfvw.geo.GeoPoint(32.70, -80.00)
    east = pyfvw.geo.GeoPoint(32.70, -79.90)

    # No preference: the fastest crossing, which happens to be the toll bridge.
    assert _crossing(router.route(west, east)) == "Toll Bridge"

    # Both spellings of a refusal, and a number that merely prices.
    assert _crossing(router.route(west, east, toll_penalty="exclude")) == "Bay Ferry"
    assert _crossing(router.route(west, east, toll_penalty=False)) == "Bay Ferry"
    assert _crossing(router.route(west, east, toll_penalty=10.0)) == "Bay Ferry"
    assert _crossing(router.route(west, east, toll_penalty=1.5)) == "Toll Bridge"
    assert _crossing(router.route(west, east, ferry_penalty=False)) == "Toll Bridge"
    assert _crossing(router.route(
        west, east, toll_penalty=False, ferry_penalty=False)) == "Long Way Round"


def test_a_ferry_crossing_is_timed_by_the_boat_not_by_the_traveller(bay_graph):
    router = pyfvw.routing.Router(bay_graph)
    west = pyfvw.geo.GeoPoint(32.70, -80.00)
    island = pyfvw.geo.GeoPoint(32.751, -79.95)

    driven = router.route(west, island)
    walked = router.route(west, island, driving=False)
    assert driven.found and walked.found
    # 00:45 for the whole crossing, whoever is aboard — walking it at 5 km/h
    # would be hours.
    assert abs(walked.seconds - driven.seconds) < 120.0
    assert walked.seconds < 2000.0


def test_refusing_the_ferry_can_strand_an_island(bay_graph):
    router = pyfvw.routing.Router(bay_graph)
    west = pyfvw.geo.GeoPoint(32.70, -80.00)
    island = pyfvw.geo.GeoPoint(32.751, -79.95)

    assert router.route(west, island).found
    # Not an error — an unreachable destination is an answer.
    assert not router.route(west, island, ferry_penalty="exclude").found
    # A price, however steep, never strands anybody.
    assert router.route(west, island, ferry_penalty=1e6).found


def test_the_avoidances_outrank_the_profile(bay_graph, tmp_path):
    """The one place these two differ from every other profile-backed setting:
    the argument wins, so 'this profile, but no ferries today' needs no
    profile of its own."""
    rules = tmp_path / "rules.json"
    rules.write_text("""{
      "version": 1, "default_profile": "car",
      "profiles": { "car": {
        "mode": "motor_vehicle", "speed": {"source":"posted"},
        "ferry_penalty": "exclude",
        "unlisted_classes": 1.0, "classes": {} } } }""")
    router = pyfvw.routing.Router(bay_graph)
    west = pyfvw.geo.GeoPoint(32.70, -80.00)
    island = pyfvw.geo.GeoPoint(32.751, -79.95)

    # The profile refuses ferries, so the island is unreachable...
    assert not router.route(west, island, profile="car", rules=str(rules)).found
    # ... until the query says otherwise.
    assert router.route(west, island, profile="car", rules=str(rules),
                        ferry_penalty=1.0).found


def test_a_nonsense_avoidance_is_rejected_rather_than_ignored(bay_graph):
    router = pyfvw.routing.Router(bay_graph)
    west = pyfvw.geo.GeoPoint(32.70, -80.00)
    east = pyfvw.geo.GeoPoint(32.70, -79.90)
    for bad in (0.0, -3.0, True, "never"):
        with pytest.raises(pyfvw.FvError):
            router.route(west, east, toll_penalty=bad)


def test_a_graph_built_without_ferries_simply_has_none(tmp_path):
    path = tmp_path / "bay.osm"
    path.write_text(_BAY_OSM)
    with_boat = pyfvw.routing.RoadGraph.build([str(path)])
    without = pyfvw.routing.RoadGraph.build([str(path)], include_ferries=False)
    assert without.arc_count < with_boat.arc_count
    router = pyfvw.routing.Router(without)
    assert not router.route(pyfvw.geo.GeoPoint(32.70, -80.00),
                            pyfvw.geo.GeoPoint(32.751, -79.95)).found


# ---------------------------------------------------------------------------
# Dragging a waypoint (the route overlay's press-move-release gesture)
# ---------------------------------------------------------------------------
#
# The gesture is the first thing in the tree to use OverlayManager's mouse
# CAPTURE, so these pin both halves: that the overlay turns pixels into a
# position, and that the stack sends it every move once it has taken a press.


def _canvas_with_font(w, h):
    """A canvas the route overlay can draw on -- it labels its waypoints, and
    text needs a face."""
    canvas = pyfvw.canvas.CpuCanvas(w, h)
    for f in ("/System/Library/Fonts/Supplemental/Arial.ttf",
              "/Library/Fonts/Arial.ttf",
              "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
              "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"):
        if os.path.isfile(f):
            canvas.set_default_font(f)
            return canvas
    pytest.skip("no host TTF font")


def _cpp_route(name, waypoints, graph_path="", rules_path=""):
    """A `pyfvw.route.RouteOverlay` with a planner of its own.

    The planner is BORROWED by the overlay in C++; the binding takes a
    keep_alive so a test does not have to hold one, which is the difference
    between this helper and the C++ one it mirrors.

    `waypoints` is (label, lat, lon) triples, which is what these tests were
    written with when the overlay was a Python class -- `RouteWaypoint` takes
    that spelling for exactly this reason.
    """
    ov = pyfvw.route.RouteOverlay(name)
    ov.set_planner(pyfvw.route.RoutePlanner(graph_path, rules_path))
    ov.waypoints = [pyfvw.route.RouteWaypoint(l, lat, lon)
                    for l, lat, lon in waypoints]
    # These tests predate the document; none of them is about dirtiness.
    ov.dirty = False
    return ov


def _opts(**kw):
    o = pyfvw.route.RoutePlanOptions()
    for k, v in kw.items():
        setattr(o, k, v)
    return o


def _labelled(overlay):
    return dict((w.label, (w.position.lat, w.position.lon))
                for w in overlay.waypoints)


def _route_module():
    sys.path.insert(0, os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "apps"))
    try:
        import route as route_mod
        return route_mod
    finally:
        sys.path.pop(0)


def _drag_fixture():
    """A route of two waypoints in a stack, drawn once so the overlay knows
    where its diamonds ARE — hit testing and the pixel-to-position conversion
    both read what the last frame put on screen.

    THE OVERLAY IS THE C++ ONE (`pyfvw.route.RouteOverlay`) and these tests are
    unchanged in what they assert. That is the point of keeping them: the
    gestures below are now `fv::RouteEditSession`, gtested in
    `port/RouteKit/test/route_edit_test.cpp`, and what THIS file still proves
    is that they arrive through the BINDING and through the stack's own event
    routing — the manager's capture included."""
    mgr = pyfvw.overlay.OverlayManager()
    overlay = _cpp_route("drag me", [
        ("WP1", 32.70, -80.00),
        ("WP2", 32.60, -79.90),
    ])
    mgr.add(overlay)
    overlay.set_manager(mgr)

    proj = pyfvw.engine.MapProjection()
    proj.set_surface_size(400, 400)
    proj.set_center(pyfvw.geo.GeoPoint(32.65, -79.95))
    proj.set_resolution(0.0008, 0.0008)
    canvas = _canvas_with_font(400, 400)
    mgr.draw_all(proj, canvas)
    return mgr, overlay, proj, canvas


def _drawn(overlay, proj, index=0):
    """(label, x, y) for one waypoint, which is what `overlay._drawn` used to
    hand back. The C++ overlay keeps its drawn positions private -- the pick
    is the supported way to ask -- so the projection is asked instead, and it
    is the SAME projection the frame was drawn with."""
    w = overlay.waypoints[index]
    x, y = proj.geo_to_surface(w.position)
    return w.label, int(round(x)), int(round(y))


def _ev(x, y):
    return pyfvw.overlay.MouseEvent(x, y)


def test_a_press_on_a_waypoint_captures_the_mouse_and_a_move_drags_it():
    mgr, overlay, proj, canvas = _drag_fixture()
    (label, wx, wy) = _drawn(overlay, proj, 0)
    before = _labelled(overlay)

    assert mgr.mouse_capture is None
    assert mgr.route_mouse_down(_ev(wx, wy))
    assert overlay.selected == label
    # Capture is the point: the drag survives the cursor leaving the diamond.
    assert mgr.mouse_capture is overlay
    # A press alone is a SELECTION — nothing has moved and nothing is undoable.
    assert not overlay.edit.can_undo()
    assert _labelled(overlay) == before

    assert mgr.route_mouse_move(_ev(wx + 60, wy + 40))
    assert mgr.route_mouse_up(_ev(wx + 60, wy + 40))
    assert mgr.mouse_capture is None

    moved = _labelled(overlay)
    assert moved[label] != before[label]
    # The other waypoint is untouched, and order is preserved.
    other = _drawn(overlay, proj, 1)[0]
    assert moved[other] == before[other]
    assert [w.label for w in overlay.waypoints] == [label, other]

    # It landed where the cursor did, through the same projection that drew it.
    # Nothing exact is under the cursor, so the snap declines and the position
    # is the un-projected pixel — which is what a snap exists to improve on
    # when there IS something there.
    want = proj.surface_to_geo(wx + 60, wy + 40)
    assert moved[label][0] == pytest.approx(want.lat, abs=1e-9)
    assert moved[label][1] == pytest.approx(want.lon, abs=1e-9)

    # The whole drag is ONE undo step, not one per move event.
    assert overlay.edit.can_undo()
    overlay.edit.undo()
    assert _labelled(overlay) == before
    assert not overlay.edit.can_undo()


def test_a_press_on_empty_map_is_declined_so_the_shell_can_pan():
    mgr, overlay, _proj, _canvas = _drag_fixture()
    assert not mgr.route_mouse_down(_ev(5, 5))
    assert mgr.mouse_capture is None
    assert not mgr.route_mouse_move(_ev(50, 50))
    assert not mgr.route_mouse_up(_ev(50, 50))


def test_escape_mid_drag_puts_the_waypoint_back_and_leaves_no_history():
    mgr, overlay, proj, _canvas = _drag_fixture()
    (label, wx, wy) = _drawn(overlay, proj, 0)
    before = _labelled(overlay)

    assert mgr.route_mouse_down(_ev(wx, wy))
    assert mgr.route_mouse_move(_ev(wx + 80, wy - 30))
    assert _labelled(overlay) != before

    k = pyfvw.overlay.key
    assert mgr.route_key_down(pyfvw.overlay.KeyEvent(key=k.ESCAPE))
    assert _labelled(overlay) == before
    assert mgr.mouse_capture is None
    assert not overlay.edit.can_undo()      # a cancelled drag is not history


def test_a_still_press_and_release_is_a_selection_and_not_an_edit():
    mgr, overlay, proj, _canvas = _drag_fixture()
    (label, wx, wy) = _drawn(overlay, proj, 0)
    before = _labelled(overlay)

    assert mgr.route_mouse_down(_ev(wx, wy))
    assert mgr.route_mouse_move(_ev(wx + 1, wy))   # inside the 3px slop
    assert mgr.route_mouse_up(_ev(wx + 1, wy))
    assert _labelled(overlay) == before
    assert not overlay.edit.can_undo()
    assert overlay.selected == label


def test_leaving_edit_focus_cancels_a_drag_in_flight():
    mgr, overlay, proj, _canvas = _drag_fixture()
    (label, wx, wy) = _drawn(overlay, proj, 0)
    before = _labelled(overlay)

    assert mgr.route_mouse_down(_ev(wx, wy))
    assert mgr.route_mouse_move(_ev(wx + 50, wy + 50))
    overlay.edit.release_edit_focus()
    assert _labelled(overlay) == before
    assert mgr.mouse_capture is None
    # And the overlay now declines the mouse entirely.
    overlay.edit.enter_edit_focus()
    assert mgr.route_mouse_down(_ev(wx, wy))
    overlay.edit.release_edit_focus()
    assert not mgr.route_mouse_down(_ev(wx, wy))


# ---------------------------------------------------------------------------
# Geographic contours (G1) through the binding
# ---------------------------------------------------------------------------


def _wide_proj(w=600, h=400, center=(40.0, -40.0), dpp=0.25):
    p = pyfvw.engine.MapProjection()
    p.set_surface_size(w, h)
    p.set_center(pyfvw.geo.GeoPoint(*center))
    p.set_resolution(dpp, dpp)
    return p


def test_a_great_circle_bows_poleward_and_a_rhumb_line_does_not():
    """The property that distinguishes the two, asserted on GEOGRAPHY rather
    than on pixels: between two points at the same latitude the shortest path
    over the sphere goes nearer the pole, the constant-bearing line does not."""
    # 0.05 deg/px over 60 deg of longitude is ~1200 px, so the ~20-px chord
    # gives a real run of points to look at. (At 0.5 deg/px the same line is
    # 120 px wide and correctly comes back as SIX points -- the step tracks
    # the screen, which is the next test.)
    proj = _wide_proj(dpp=0.05)
    a = pyfvw.geo.GeoPoint(45.0, -70.0)
    b = pyfvw.geo.GeoPoint(45.0, -10.0)
    k = pyfvw.geo.LineKind

    gc = pyfvw.geo.line_points(proj, a, b, k.GREAT_CIRCLE, clip=False)
    rh = pyfvw.geo.line_points(proj, a, b, k.RHUMB, clip=False)
    simple = pyfvw.geo.line_points(proj, a, b, k.SIMPLE, clip=False)

    assert len(gc) > 10                     # densified, not two endpoints
    assert max(p.lat for p in gc) > 45.5     # bows toward the pole
    assert max(p.lat for p in rh) == pytest.approx(45.0, abs=1e-6)
    assert len(simple) == 2                  # nothing between the endpoints

    # Both still start and end where they were asked to.
    for run in (gc, rh, simple):
        assert run[0].lat == pytest.approx(a.lat, abs=1e-6)
        assert run[-1].lon == pytest.approx(b.lon, abs=1e-6)


def test_the_step_size_tracks_the_SCREEN_and_not_the_line_length():
    """The reason a 10,000 km arc is affordable: the walk steps in ~20-pixel
    chords off the projection's degrees-per-pixel, so zooming IN over the same
    two points buys more points, not the same ones spread further apart."""
    a = pyfvw.geo.GeoPoint(20.0, -60.0)
    b = pyfvw.geo.GeoPoint(50.0, 10.0)
    k = pyfvw.geo.LineKind
    coarse = pyfvw.geo.line_points(_wide_proj(dpp=0.5), a, b, k.GREAT_CIRCLE,
                                   clip=False)
    fine = pyfvw.geo.line_points(_wide_proj(dpp=0.05), a, b, k.GREAT_CIRCLE,
                                 clip=False)
    assert len(fine) > 3 * len(coarse)


def test_a_line_that_misses_the_viewport_clips_away_to_nothing():
    """Clipping happens in GEOGRAPHIC space BEFORE the densify, which is the
    property worth having: a line nowhere near the screen costs a search."""
    proj = _wide_proj(center=(0.0, 0.0), dpp=0.05)   # a small window at 0,0
    k = pyfvw.geo.LineKind
    far_a = pyfvw.geo.GeoPoint(60.0, 100.0)
    far_b = pyfvw.geo.GeoPoint(61.0, 120.0)
    assert pyfvw.geo.line_path(proj, far_a, far_b, k.GREAT_CIRCLE) == []
    # ... and unclipped it is a real line; the clip is what emptied it.
    assert pyfvw.geo.line_points(proj, far_a, far_b, k.GREAT_CIRCLE,
                                 clip=False)


def test_a_path_comes_back_as_strokable_subpaths():
    proj = _wide_proj()
    k = pyfvw.geo.LineKind
    paths = pyfvw.geo.line_path(proj, pyfvw.geo.GeoPoint(35.0, -50.0),
                                pyfvw.geo.GeoPoint(45.0, -30.0),
                                k.GREAT_CIRCLE)
    assert paths
    for sub in paths:
        assert len(sub) >= 2                 # a 1-point sub-path is dropped
        for x, y in sub:
            assert isinstance(x, float) and isinstance(y, float)


def test_a_polyline_densifies_every_leg_and_a_circle_closes():
    proj = _wide_proj(dpp=0.5)
    k = pyfvw.geo.LineKind
    pts = [pyfvw.geo.GeoPoint(30.0, -60.0),
           pyfvw.geo.GeoPoint(45.0, -40.0),
           pyfvw.geo.GeoPoint(35.0, -20.0)]
    gc = pyfvw.geo.polyline_path(proj, pts, k.GREAT_CIRCLE)
    simple = pyfvw.geo.polyline_path(proj, pts, k.SIMPLE)
    assert sum(len(s) for s in gc) > sum(len(s) for s in simple)
    assert sum(len(s) for s in simple) == 3        # one run, three corners

    ring = pyfvw.geo.circle_path(proj, pyfvw.geo.GeoPoint(40.0, -40.0),
                                 200000.0)
    assert len(ring) == 1
    assert ring[0][0] == pytest.approx(ring[0][-1])   # closed
    arc = pyfvw.geo.arc_path(proj, pyfvw.geo.GeoPoint(40.0, -40.0), 200000.0,
                             0.0, 90.0)
    # A quarter sweep gets a quarter of the points, not the same number.
    assert 0.2 < len(arc[0]) / len(ring[0]) < 0.35
    assert arc[0][0] != arc[0][-1]                    # and is not closed


def test_the_route_overlay_draws_its_legs_as_great_circles_by_default():
    """Step 2's acceptance test: the route's own rendering goes through G1,
    so a long leg is an ARC on screen and not the chord between two pixels."""
    overlay = _cpp_route("transatlantic", [
        ("WP1", 45.0, -70.0),
        ("WP2", 45.0, -10.0),
    ])
    overlay.show_labels = False
    assert overlay.leg_kind == pyfvw.geo.LineKind.GREAT_CIRCLE

    proj = _wide_proj(w=600, h=400, center=(47.0, -40.0), dpp=0.25)
    canvas = _canvas_with_font(600, 400)

    def ink_rows():
        canvas.clear((255, 255, 255))
        overlay.on_draw(proj, canvas)
        arr = np.asarray(canvas.buffer)
        # The row the line occupies at the horizontal centre of the surface.
        col = arr[:, 300, :3]
        return [y for y in range(400) if (col[y] != 255).any()]

    # Endpoints project to the same row, so a straight leg is flat there...
    overlay.leg_kind = pyfvw.geo.LineKind.SIMPLE
    flat = ink_rows()
    overlay.leg_kind = pyfvw.geo.LineKind.GREAT_CIRCLE
    bowed = ink_rows()
    assert flat and bowed
    # ... and the great circle is drawn NORTH of it (smaller y = further up).
    assert min(bowed) < min(flat) - 5


def test_a_clipped_away_leg_breaks_the_run(recwarn):
    """G3 closed this, and the assertion below is the OPPOSITE of the one that
    stood here through G1 and G2.

    contour.cpp's AdvanceLeg always said that a leg which emitted nothing
    (clipped away) BREAKS the run -- "otherwise two disjoint visible stretches
    would be joined by a line that was never there" -- and it correctly kept
    the next leg's first point. What it could not do was SAY so:
    IGeoContour::NextPoint is a flat point stream, and BuildGeoPath broke a
    sub-path only on a projection failure or a >180-degree longitude step, so
    the join happened anyway. G3 added IGeoContour::AtBreak(), asked after
    NextPoint and about the point that call produced, and BuildGeoPath flushes
    on it.
    """
    proj = _wide_proj(center=(0.0, 0.0), dpp=0.02)   # a ~12x8 degree window
    k = pyfvw.geo.LineKind
    pts = [pyfvw.geo.GeoPoint(0.5, -1.0),            # in view
           pyfvw.geo.GeoPoint(60.0, 150.0),          # far away
           pyfvw.geo.GeoPoint(61.0, 155.0),          # this leg misses entirely
           pyfvw.geo.GeoPoint(-0.5, 1.0)]            # back in view
    broken = pyfvw.geo.polyline_path(proj, pts, k.SIMPLE)
    assert len(broken) == 2, "the two visible stretches must not be joined"

    # Leg by leg gives the same picture, which is the workaround RouteOverlay
    # used before this and also the assertion that the clip really did reject
    # the middle leg.
    per_leg = [pyfvw.geo.line_path(proj, a, b, k.SIMPLE)
               for a, b in zip(pts, pts[1:])]
    assert per_leg[1] == []
    assert per_leg[0] and per_leg[2]

    # What does NOT break a run, and is not meant to: a waypoint merely off the
    # edge of the SURFACE still projects, to a coordinate outside 0..w.
    whole = pyfvw.geo.polyline_path(proj, pts, k.SIMPLE, clip=False)
    assert len(whole) == 1 and len(whole[0]) == 4
    assert any(x > 600 or x < 0 for x, _y in whole[0])


# ---------------------------------------------------------------------------
# pyfvw.symbol / pyfvw.draw (G2 + G3)
# ---------------------------------------------------------------------------


def _harbour_proj(w=400, h=300):
    p = pyfvw.engine.MapProjection()
    p.set_surface_size(w, h)
    p.set_center(pyfvw.geo.GeoPoint(32.75, -79.90))
    p.set_resolution(0.0004, 0.0004)
    return p


def _ink(canvas):
    a = np.asarray(canvas.buffer)[:, :, :3]
    return int((a != 255).any(axis=2).sum())


def _count(canvas, rgb):
    a = np.asarray(canvas.buffer)[:, :, :3]
    return int((a == np.array(rgb, dtype=np.uint8)).all(axis=2).sum())


def test_the_line_presets_are_a_table_and_solid_is_the_plain_pen():
    """LineSegmentRenderer.cpp's 15 classes are one operation -- stamp a shape
    every N px along the path -- so they are DATA here, not code. Solid is the
    exception and deliberately carries no pattern: expressing it as a one-run
    cycle would cost a placer walk to draw what draw_lines draws."""
    assert "solid" in pyfvw.draw.PRESETS and "railroad" in pyfvw.draw.PRESETS
    assert not pyfvw.draw.preset_line("solid", (0, 0, 0), 2).has_pattern
    assert pyfvw.draw.preset_line("dash", (0, 0, 0), 2).has_pattern
    # An unknown name draws a LINE rather than nothing.
    assert not pyfvw.draw.preset_line("no-such", (0, 0, 0), 2).has_pattern


def test_geodraw_strokes_a_geodesic_and_a_casing_goes_under_it():
    proj = _harbour_proj()
    canvas = pyfvw.canvas.CpuCanvas(400, 300)
    canvas.clear((255, 255, 255))
    d = pyfvw.draw.GeoDraw(proj, canvas)
    style = pyfvw.draw.solid_line((40, 90, 210), 3)
    style.add_casing((220, 30, 30), 2)
    d.line(pyfvw.geo.GeoPoint(32.75, -79.94), pyfvw.geo.GeoPoint(32.75, -79.86),
           style, pyfvw.geo.LineKind.SIMPLE)
    col = np.asarray(canvas.buffer)[:, 200, :3]
    rows = [y for y in range(300) if (col[y] != 255).any()]
    assert rows
    # The topmost ink in the column is the casing; the line is inside it.
    assert tuple(col[rows[0]]) == (220, 30, 30)
    assert any(tuple(col[y]) == (40, 90, 210) for y in rows)


def test_geodraw_stamps_a_builtin_symbol_and_says_so_when_the_id_is_wrong():
    proj = _harbour_proj()
    canvas = pyfvw.canvas.CpuCanvas(400, 300)
    canvas.clear((255, 255, 255))
    lib = pyfvw.symbol.BuiltinSymbolLibrary()
    lib.set_color((0, 0, 255))
    d = pyfvw.draw.GeoDraw(proj, canvas, lib)
    d.symbol(proj.center, pyfvw.symbol.builtin.DIAMOND, scale=2.0)
    assert d.draws_emitted == 1
    assert tuple(np.asarray(canvas.buffer)[150, 200, :3]) == (0, 0, 255)
    # A mistyped id is the likeliest failure and is invisible otherwise, so it
    # raises rather than drawing nothing.
    with pytest.raises(pyfvw.FvError):
        d.symbol(proj.center, "fv.no-such-symbol")


def test_a_highlighted_symbol_keeps_its_own_colour_and_gains_a_ring():
    """G4. A selected marker used to be REPAINTED in the selection colour,
    which said 'selected' by throwing away the one thing that said which route
    or which category it belongs to. The highlight is its own silhouette
    stamped around it instead, so both facts are on screen at once."""
    proj = _harbour_proj()

    def render(state):
        canvas = pyfvw.canvas.CpuCanvas(400, 300)
        canvas.clear((255, 255, 255))
        lib = pyfvw.symbol.BuiltinSymbolLibrary()
        lib.set_color((0, 0, 255))
        d = pyfvw.draw.GeoDraw(proj, canvas, lib)
        d.state = state
        d.symbol(proj.center, pyfvw.symbol.builtin.DIAMOND, scale=2.0)
        return canvas, d

    plain, dp = render(pyfvw.draw.RenderState.NORMAL)
    lit, dl = render(pyfvw.draw.RenderState.HIGHLIGHTED)
    assert dp.highlight_draws == 0 and dl.highlight_draws > 0
    assert dp.draws_emitted == dl.draws_emitted == 1
    assert _count(plain, (0, 0, 255)) == _count(lit, (0, 0, 255))
    assert _count(lit, (255, 220, 0)) > 0
    assert _count(plain, (255, 220, 0)) == 0
    # The colour is the caller's, not the port's.
    canvas = pyfvw.canvas.CpuCanvas(400, 300)
    canvas.clear((255, 255, 255))
    lib = pyfvw.symbol.BuiltinSymbolLibrary()
    lib.set_color((0, 0, 255))
    d = pyfvw.draw.GeoDraw(proj, canvas, lib)
    d.state = pyfvw.draw.RenderState.HIGHLIGHTED
    d.set_highlight((255, 0, 255), 4.0)
    d.symbol(proj.center, pyfvw.symbol.builtin.DIAMOND, scale=2.0)
    assert _count(canvas, (255, 0, 255)) > 0


def test_geodraw_fills_a_pick_index_from_the_ink_it_emitted():
    proj = _harbour_proj()
    canvas = pyfvw.canvas.CpuCanvas(400, 300)
    canvas.clear((255, 255, 255))
    d = pyfvw.draw.GeoDraw(proj, canvas)
    assert not d.pick_enabled          # off by default, unlike the chart renderer
    d.pick_enabled = True
    d.set_feature(11)
    d.line(pyfvw.geo.GeoPoint(32.75, -79.94), pyfvw.geo.GeoPoint(32.75, -79.86),
           pyfvw.draw.solid_line((0, 0, 0), 3), pyfvw.geo.LineKind.SIMPLE)
    hits = d.hit_test(200, 150, 4.0)
    assert hits and hits[0][0] == 11
    assert d.hit_test(200, 40, 4.0) == []


def test_the_route_line_says_which_MODE_it_was_priced_as(tmp_path, kiawah_graph):
    """A calculated route is blue over a white casing; an uncalculated one is
    the overlay's own red straight legs; and the plan records WHICH mode it was
    priced as, which is what selects the line's preset.

    IT PLANS FOR REAL rather than being handed legs. The Python overlay had a
    writable `road_legs`, so this test used to install geometry and flip a
    flag; the C++ one deliberately has no such door -- a plan belongs to the
    waypoints it was computed from, and a settable one is a stale road wearing
    a document's clothes. So the two pictures come from two real requests.

    WHAT THIS TEST NO LONGER ASSERTS, AND WHY -- read this before adding it
    back. It used to end with `bike_blue < car_blue`: the dashed bicycle line
    inks fewer pixels than the solid car one. THAT IS NO LONGER TRUE ON SCREEN,
    and not because of anything the C++ port changed about drawing. The `dash`
    preset is `dash(8) gap(6)` in pattern units and does not scale with the
    pen, and P13 doubled the route line from 3 device pixels to 6 (a 3-pixel
    line is ONE point on a 3x phone). A 6-wide stroke has 3-pixel round caps at
    each end of every dash, so two neighbouring dashes bridge a 6-unit gap
    exactly and the line reads SOLID. Measured on identical geometry through
    `pyfvw.draw`: at width 3, solid 1506 px vs dash 1188 (21% removed); at
    width 6, solid 3030 vs dash 3024 (0.2%).

    So a bicycle route is currently indistinguishable from a car one at a
    glance, on the phone above all -- which is where the widening was done for.
    Asserting the dash here would pin a picture nobody is being shown. The fix
    is a decision about the preset (square caps on a dash run, a wider gap, or
    a pattern that scales with the pen), which is the ledger's business and not
    this test's.
    """
    path = str(tmp_path / "kiawah-style.fvroad")
    kiawah_graph.save(path)
    proj = pyfvw.engine.MapProjection()
    proj.set_surface_size(600, 500)
    proj.set_center(pyfvw.geo.GeoPoint(32.607, -80.075))
    proj.set_resolution(0.0004, 0.0004)

    overlay = _cpp_route("styled", [
        ("A", 32.590, -80.130),
        ("B", 32.640, -80.005),
    ], graph_path=path)
    overlay.show_labels = False

    def draw():
        canvas = _canvas_with_font(600, 500)
        canvas.clear((200, 200, 200))
        overlay.on_draw(proj, canvas)
        return canvas

    # Uncalculated: the overlay's own red, and no blue road line anywhere. The
    # two must not be confusable -- those are the waypoints joined, not a road
    # anybody can ride.
    straight = draw()
    assert _count(straight, (40, 90, 210)) == 0
    assert _count(straight, overlay.color) > 100

    overlay.follow_roads(_opts(snap_meters=3000.0))
    assert overlay.has_plan
    assert not overlay.plan.is_bicycle
    car = draw()

    overlay.follow_roads(_opts(snap_meters=3000.0, cycle_only=True))
    assert overlay.has_plan
    # The mode is recorded on the PLAN, which is what picks the preset and what
    # a reloaded route is styled from before anyone has pressed route again.
    assert overlay.plan.is_bicycle
    bike = draw()

    # Both are blue roads over a white casing, and the casing is why they read
    # over a chart -- the grey background is what makes it countable.
    for picture in (car, bike):
        assert _count(picture, (40, 90, 210)) > 0
        assert _count(picture, (255, 255, 255)) > 0
    # And a calculated route has replaced the straight legs entirely.
    assert _count(car, overlay.color) < _count(straight, overlay.color)


def test_a_selected_waypoint_keeps_the_routes_colour(tmp_path):
    """G4's acceptance test on the app's own overlay. Before it, selecting a
    waypoint repainted that marker yellow, so a two-route session could not
    tell you which route the selected point belonged to. Now the marker keeps
    the route's colour and the selection is a band around it."""
    proj = _harbour_proj()
    overlay = _cpp_route("sel", [
        ("A", 32.73, -79.94),
        ("B", 32.77, -79.86),
    ])
    overlay.color = (0, 160, 0)

    def draw():
        canvas = _canvas_with_font(400, 300)
        canvas.clear((255, 255, 255))
        overlay.on_draw(proj, canvas)
        return canvas

    none = draw()
    overlay.selected = "A"
    one = draw()

    x, y = proj.geo_to_surface(pyfvw.geo.GeoPoint(32.73, -79.94))
    at = lambda c: tuple(np.asarray(c.buffer)[int(round(y)), int(round(x)), :3])
    assert at(none) == (0, 160, 0)
    assert at(one) == (0, 160, 0), "the selected marker is not recoloured"
    assert _count(none, (255, 220, 0)) == 0
    assert _count(one, (255, 220, 0)) > 0
    # The highlight lands UNDER its own marker and over what was already
    # there, so the leg line loses a few pixels to it — a handful, not the
    # marker. Anything approaching a whole diamond (~50 px at scale 1.6)
    # would mean the marker itself had been repainted.
    assert _count(none, (0, 160, 0)) - _count(one, (0, 160, 0)) < 20


def test_which_requests_count_as_a_bicycle_route():
    """The line's dash follows the ROUTE REQUEST, and a profile named on the
    call wins because it overrides cycle_only in the router too. Matched on the
    name because the rule file owns the profiles and a user may well call
    theirs 'bicycle-winter'."""
    # A free function since P5, and public for the reason the C++ header
    # gives: the OVERLAY needs the same answer to style a line it did not plan
    # -- a route reloaded from disk, drawn before anyone has pressed route.
    is_bike = pyfvw.route.is_bicycle_request
    assert is_bike("bicycle", False)
    assert is_bike("bike-fast", False)
    assert is_bike("", True)
    assert not is_bike("", False)
    assert not is_bike("car_no_tolls", True)


# ---------------------------------------------------------------------------
# The declared property page (fvkit/app/properties.h)
# ---------------------------------------------------------------------------
#
# Bound on Overlay rather than on GridOverlay, so these same four calls work
# for every overlay that declares properties. That is the point of the schema:
# a UI, a script and a settings file all drive an overlay without knowing what
# kind of overlay it is.


def test_property_schema_is_self_describing():
    g = pyfvw.overlay.GridOverlay()
    rows = g.describe_properties()
    assert rows, "the grid declares properties"
    by_key = {r["key"]: r for r in rows}

    # Every row carries what a dialog needs to build a control.
    for r in rows:
        assert r["key"] and r["label"] and r["type"]
        assert "default" in r
    assert by_key["line_width"]["type"] == "int"
    assert by_key["line_width"]["min"] == 1
    assert by_key["line_width"]["max"] == 8
    assert by_key["line_color"]["type"] == "color"
    # Grouping is what a shell lays the page out with.
    assert {r["group"] for r in rows} >= {"Lines", "Labels"}


def test_properties_round_trip_and_refuse_bad_values():
    g = pyfvw.overlay.GridOverlay()
    assert g.get_property("show_ticks") is True
    g.set_property("show_ticks", False)
    assert g.get_property("show_ticks") is False

    g.set_property("line_color", (255, 0, 0))
    assert g.get_property("line_color") == (255, 0, 0, 255)
    g.set_property("line_color", (1, 2, 3, 4))
    assert g.get_property("line_color") == (1, 2, 3, 4)

    with pytest.raises(pyfvw.FvError):
        g.set_property("line_width", 99)      # out of the declared range
    with pytest.raises(pyfvw.FvError):
        g.get_property("no_such_property")

    g.reset_properties()
    assert g.get_property("show_ticks") is True


def test_an_overlay_without_properties_answers_an_empty_schema():
    # Never raises: a caller iterating a stack must not have to ask first.
    class Bare(pyfvw.overlay.Overlay):
        def __init__(self):
            super().__init__("bare")

    b = Bare()
    assert b.describe_properties() == []
    with pytest.raises(pyfvw.FvError):
        b.get_property("anything")


def test_grid_draws_a_real_graticule():
    cat = pyfvw.catalog.Catalog()
    eng = pyfvw.engine.MapEngine(cat)
    eng.set_surface(640, 480)
    eng.set_center(pyfvw.geo.GeoPoint(33.7488, -84.3882))
    eng.set_scale(5_000_000)

    cv = pyfvw.canvas.CpuCanvas(640, 480)
    cv.clear((0, 0, 0))
    g = pyfvw.overlay.GridOverlay()
    g.set_color((255, 255, 255, 255))
    g.on_draw(eng.proj, cv)

    a = np.asarray(cv.buffer)
    # Lines of latitude and longitude, so ink in the interior on both axes --
    # and the grid is not a border round the edge.
    interior = a[100:380, 100:540, 0]
    assert interior.max() > 200

# ---------------------------------------------------------------------------
# The contour overlay (port/contour-plan.md)
# ---------------------------------------------------------------------------


def test_contour_needs_a_source_and_says_so_in_its_stats():
    o = pyfvw.overlay.ContourOverlay()
    proj = pyfvw.engine.MapProjection()
    proj.set_surface_size(320, 240)
    proj.set_center(pyfvw.geo.GeoPoint(34.75, -83.75))
    proj.set_scale(100000)
    cv = pyfvw.canvas.CpuCanvas(320, 240)
    o.on_draw(proj, cv)
    assert o.last_draw["no_source"] is True
    assert o.last_draw["lines_drawn"] == 0
    assert o.cached_tiles == 0


def test_contour_interval_is_major_over_divisions():
    o = pyfvw.overlay.ContourOverlay()
    # FalconView's defaults: 1000 feet in 5 divisions.
    assert o.major_interval_meters == pytest.approx(304.8)
    assert o.interval_meters == pytest.approx(60.96)
    o.set_property("interval_unit", "meters")
    o.set_property("major_interval", 300.0)
    o.set_property("divisions", 6)
    assert o.interval_meters == pytest.approx(50.0)


def test_a_choice_property_takes_its_name_or_its_index():
    o = pyfvw.overlay.ContourOverlay()
    spec = {r["key"]: r for r in o.describe_properties()}["smoothing"]
    assert spec["type"] == "choice"
    assert spec["choices"] == ["none", "chaikin", "spline"]

    o.set_property("smoothing", "spline")
    assert o.get_property("smoothing") == 2      # the index is the value
    o.set_property("smoothing", 0)               # and still works
    assert o.get_property("smoothing") == 0
    with pytest.raises(pyfvw.FvError):
        o.set_property("smoothing", "wobbly")


def test_contour_draws_real_terrain_and_smoothing_only_changes_the_ink():
    root = _testdata("dted")
    if root is None:
        pytest.skip("no DTED in the test data")

    proj = pyfvw.engine.MapProjection()
    proj.set_surface_size(400, 400)
    proj.set_center(pyfvw.geo.GeoPoint(34.75, -83.75))
    proj.set_scale(50000)
    cv = pyfvw.canvas.CpuCanvas(400, 400)

    o = pyfvw.overlay.ContourOverlay()
    o.set_elevation_source(pyfvw.formats.DtedElevationSource(root))
    o.set_property("smoothing", "none")
    o.set_property("thinning_px", 0.0)
    o.on_draw(proj, cv)
    plain = o.last_draw
    assert plain["lines_drawn"] > 0
    assert plain["major_lines"] > 0
    assert plain["shaped_vertices"] == plain["vertices"]

    # Smoothing is a property of the PICTURE: more ink, the same geometry, and
    # not one elevation post re-read.
    o.set_property("smoothing", "chaikin")
    o.on_draw(proj, cv)
    smooth = o.last_draw
    assert smooth["vertices"] == plain["vertices"]
    assert smooth["shaped_vertices"] > smooth["vertices"]
    assert smooth["samples"] == 0
    assert smooth["tiles_traced"] == 0


# ---------------------------------------------------------------------------
# The terrain avoidance mask (port/tamask-plan.md)
# ---------------------------------------------------------------------------


def test_tamask_needs_a_source_and_says_so_in_its_stats():
    o = pyfvw.overlay.TAMaskOverlay()
    proj = pyfvw.engine.MapProjection()
    proj.set_surface_size(320, 240)
    proj.set_center(pyfvw.geo.GeoPoint(34.75, -83.75))
    proj.set_scale(500000)
    cv = pyfvw.canvas.CpuCanvas(320, 240)
    o.on_draw(proj, cv)
    assert o.last_draw["no_source"] is True
    assert o.last_draw["mask_pixels"] == 0
    assert o.cached_tiles == 0


def test_tamask_levels_are_the_altitude_less_each_clearance():
    o = pyfvw.overlay.TAMaskOverlay()
    # FalconView's defaults: 2500 ft with 100 / 300 / 500 ft of clearance.
    assert o.altitude == 2500.0
    assert o.altitude_meters == pytest.approx(2500 * 0.3048)
    assert o.levels["warn_m"] == pytest.approx((2500 - 100) * 0.3048)
    assert o.levels["ok_m"] == pytest.approx((2500 - 500) * 0.3048)

    # The dead band is FalconView's Sensitivity, and it is a property like
    # everything else here.
    assert o.set_altitude(2510.0) is False
    assert o.altitude == 2500.0
    assert o.set_altitude(3000.0) is True
    assert o.altitude == 3000.0

    o.set_property("unit", "meters")
    assert o.altitude_meters == 3000.0


def test_tamask_colours_real_terrain_and_a_climb_re_reads_nothing():
    root = _testdata("dted")
    if root is None:
        pytest.skip("no DTED in the test data")

    proj = pyfvw.engine.MapProjection()
    proj.set_surface_size(320, 320)
    proj.set_center(pyfvw.geo.GeoPoint(34.75, -83.75))
    proj.set_scale(500000)
    cv = pyfvw.canvas.CpuCanvas(320, 320)

    o = pyfvw.overlay.TAMaskOverlay()
    o.set_elevation_source(pyfvw.formats.DtedElevationSource(root))
    # North Georgia tops out around 1400 m, so fly low enough to light up.
    o.set_property("altitude", 4000.0)
    o.set_property("show_labels", False)  # a headless canvas may have no font
    o.on_draw(proj, cv)
    low = o.last_draw
    assert low["tiles_sampled"] > 0
    assert low["samples"] > 0
    assert low["mask_pixels"] > 0
    assert low["band_pixels"]["warn"] > 0
    assert low["peak_drawn"] is True
    assert low["peak_elev_m"] > 0

    # Climb. FalconView rebuilt every tile's byte mask here; this caches the
    # ELEVATION, so the picture changes and not one post is re-read.
    assert o.set_altitude(20000.0) is True
    o.on_draw(proj, cv)
    high = o.last_draw
    assert high["tiles_sampled"] == 0
    assert high["samples"] == 0
    assert high["band_pixels"]["warn"] < low["band_pixels"]["warn"]
    assert high["peak_elev_m"] == low["peak_elev_m"]  # the ground did not move
