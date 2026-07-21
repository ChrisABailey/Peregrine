# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See LICENSE and NOTICE.md for the full licensing picture.

"""pyfvw binding tests — mirror the gtest pins (port/fvkit/test/) through
the Python surface. Real-data tests skip when FVW_TESTDATA_DIR is absent.

Run via ctest (pyfvw_pytest) or directly:
  PYTHONPATH=build/port/bindings/pyfvw pytest -q port/bindings/pyfvw/test
"""

import math
import os

import numpy as np
import pytest

import pyfvw


def _testdata(sub):
    d = os.environ.get("FVW_TESTDATA_DIR")
    if not d:
        return None
    p = os.path.join(d, sub)
    return p if os.path.isdir(p) else None


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
    assert len(frames) == 22
    assert sum(1 for f in frames if f.series_key == "DTED1") == 21


# ---------------------------------------------------------------------------
# GeoTIFF (real data)
# ---------------------------------------------------------------------------

def test_geotiff_enumerate():
    root = _testdata("geotiff")
    if root is None:
        pytest.skip("no TestData")
    frames = pyfvw.formats.GeoTiffFrameEnumerator().frames(root)
    assert len(frames) == 15  # 14 Chesapeake quads + chocta.tif (2026-07-16)
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
