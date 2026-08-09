# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See LICENSE and NOTICE.md for the full licensing picture.

"""pyfvw binding tests — mirror the gtest pins (port/fvkit/test/) through
the Python surface. Real-data tests skip when FVW_TESTDATA_DIR is absent.

Run via ctest (pyfvw_pytest) or directly:
  PYTHONPATH=build/port/bindings/pyfvw pytest -q port/bindings/pyfvw/test
"""

import glob
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
    # be trusted, and this file's claims the whole world.
    b = s.bounds
    assert b.contains(pyfvw.geo.GeoPoint(33.749, -84.388))
    assert b.ur.lon < -70.0 and b.ll.lon > -110.0


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
