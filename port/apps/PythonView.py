#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See LICENSE and NOTICE.md for the full licensing picture.

"""PythonView — desktop map viewer over the pyfvw FalconView port.

The successor to the pan_viewer demo: one window over every data family the
port can currently render, driven from the L2 catalog.

  Raster Charts   CADRG (GNC/JNC/LFC/TLM) and GeoPackage tile packs
  Imagery         GeoTIFF DOQs and TIROS world tiles
  Elevation       DTED shaded relief (the "dted-shaded" raster source)
  Vector Charts   DNC libraries through GeoSym, S-57 ENC cells through the
                  official S-52 presentation library, and OSM vector-tile
                  pyramids through a MapLibre GL style - three products over
                  ONE seam (source -> style engine -> VectorRenderer)

The catalog is a persistent SQLite file (File menu: New/Open/Add Map Data /
Manage Data Sources). The Map menu lists every series grouped by family; the
Overlays menu adds a graticule, a crosshair, and a color-coded COVERAGE
overlay showing where each map family has data. The window resizes freely —
the map re-renders at the new surface size.

Usage (from the repo root, after `cmake --build build`):
  python3 port/apps/PythonView.py                     # open (scan on 1st run)
  python3 port/apps/PythonView.py --scan TestData     # (re)build the catalog
  python3 port/apps/PythonView.py --at "32.78 -79.95" --series cadrg/LFC
  python3 port/apps/PythonView.py --shot out.png --series dted-shaded/DTED1
  python3 port/apps/PythonView.py --selftest          # scripted UI self-check

Keys: arrows pan (or drag with the mouse) · -/= zoom · 0 reset ·
PageUp/PageDown walk the scale ladder at the screen center · [/] feature zoom
(vector) · g grid · c coverage · l labels (vector) · q/Esc quit.
Click identifies (vector) or re-centers (raster); shift+click re-centers.
"""

import argparse
import math
import os
import sqlite3
import sys
import time
import traceback


# --- locate the built pyfvw next to this repo checkout -----------------------
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
import glob as _glob
_hits = _glob.glob(os.path.join(REPO, "build", "port", "bindings", "pyfvw", "pyfvw*.so"))
if _hits and os.path.dirname(_hits[0]) not in sys.path:
    sys.path.insert(0, os.path.dirname(_hits[0]))
os.environ.setdefault("MSPCCS_DATA", os.path.join(REPO, "fvw_core", "PdfLib", "sdk", "lib"))
TESTDATA = os.environ.get("FVW_TESTDATA_DIR", os.path.join(REPO, "TestData"))

import numpy as np  # noqa: E402
import pyfvw        # noqa: E402
import tk_keys      # noqa: E402  (sibling module, see its header)
import route as route_mod  # noqa: E402  (sibling module)
from route import RouteOverlay, RouteEditor

# ----------------------------------------------------------------------------
# App-wide constants
# ----------------------------------------------------------------------------

DEFAULT_DB = os.path.join(REPO, "build", "pythonview.sqlite")
# This APPLICATION's own overlay types (the port's own are fv.*). Both are
# static: at most one instance, toggled rather than opened.
CROSSHAIR_TYPE_ID = "app.crosshair"
COVERAGE_TYPE_ID = "app.coverage"
NATIVE_MM_PER_PIXEL = pyfvw.engine.NATIVE_DISPLAY_MM_PER_PIXEL  # 0.25
ZOOM_STEP = 2.0 ** 0.5
FEATURE_STEP = 1.25
PAN_PX = 150
# The device resolution the vector style engines convert physical style units
# with (S-52 line widths are 0.32 mm; GeoSym's are HIMETRIC). It is the SAME
# physical property as display.mm_per_pixel, so it is DERIVED from it rather
# than set independently — those two were 96 dpi and 0.25 mm/px (= 101.6 dpi),
# quietly disagreeing about the screen by 6%.
def _dpi_for(mm_per_pixel):
    return 25.4 / mm_per_pixel if mm_per_pixel > 0 else 96.0

# The data families shown in the Map menu, in display order. "dted" (the
# elevation-query format) is deliberately absent: it backs the cursor
# elevation readout, not a drawable series.
FAMILIES = (
    ("Raster Charts", ("cadrg", "gpkg")),
    ("Imagery", ("geotiff", "tiros")),
    ("Elevation", ("dted-shaded",)),
    ("Vector Charts", ("vpf", "enc", "osm")),
)
# Formats drawn through the vector seam (source -> style engine ->
# VectorRenderer) rather than through MapEngine's raster compositor. Derived
# from FAMILIES so a new vector product is added in exactly one place: before
# ENC arrived, four separate `format == "vpf"` tests stood for "is vector",
# and every one of them had to be found again.
VECTOR_FORMATS = frozenset(dict(FAMILIES)["Vector Charts"])

# Coverage-overlay colors, one per format.
COVERAGE_COLORS = {
    "cadrg": (230, 70, 70),
    "gpkg": (240, 150, 60),
    "geotiff": (70, 200, 90),
    "tiros": (235, 205, 60),
    "dted-shaded": (170, 110, 240),
    "vpf": (80, 150, 240),
    "enc": (200, 80, 190),
    "osm": (110, 190, 190),
}

# dted-shaded series carry scale 0 (elevation cells have no chart scale), so
# each level gets a sensible default display scale; -/= adjusts from there.
DTED_DEFAULT_SCALE = {"DTED0": 2e6, "DTED1": 500e3, "DTED2": 150e3, "DTED3": 50e3}

# (format, conventional subdir) probed by the auto-detect scan, mirroring the
# old demo. Recursive enumerators find frames from the root; geotiff is
# per-directory, hence the subdir fallback. dted is scanned twice on purpose:
# once as the elevation source, once as the shaded-relief raster.
SCAN_FORMATS = (("cadrg", "rpf"), ("tiros", "tiros3"), ("dted", "dted"),
                ("dted-shaded", "dted"), ("geotiff", "geotiff"), ("gpkg", None),
                ("enc", "enc"), ("osm", "OSM"))


def _find_host_font():
    """First readable TTF among the usual macOS/Linux paths."""
    for p in ("/System/Library/Fonts/Supplemental/Arial.ttf",
              "/Library/Fonts/Arial.ttf",
              "/System/Library/Fonts/Supplemental/Times New Roman.ttf",
              "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
              "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"):
        if os.path.isfile(p):
            return p
    return None


def _save_png(path, arr):
    import struct
    import zlib
    h, w = arr.shape[:2]
    raw = b"".join(b"\x00" + arr[y, :, :3].tobytes() for y in range(h))

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


# ----------------------------------------------------------------------------
# Catalog helpers
# ----------------------------------------------------------------------------

def find_vpf_databases(root, max_depth=4):
    """Directories under `root` holding a VPF database header table (dht)."""
    hits = []
    root = os.path.abspath(root)
    base_depth = root.rstrip(os.sep).count(os.sep)
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if not d.startswith(".")]
        if dirpath.rstrip(os.sep).count(os.sep) - base_depth >= max_depth:
            dirnames[:] = []
        if any(f.lower() in ("dht", "dht.") for f in filenames):
            hits.append(dirpath)
            dirnames[:] = []  # a database does not nest another database
    return hits


def scan_into_catalog(cat, root, log=print):
    """Auto-detect scan: probe every known format at `root` (and conventional
    subdirs), plus every VPF database found by walking. Returns
    [(format, path, frames)] for everything that produced frames."""
    results = []

    def try_source(fmt, d):
        try:
            sid = cat.add_data_source(d, fmt)
        except pyfvw.FvError:
            return 0  # already present (UNIQUE path+format)
        try:
            n = cat.scan(sid)
        except pyfvw.FvError as e:
            log(f"  {fmt}: {d}: {e}")
            n = 0
        if n <= 0:
            cat.remove_data_source(sid)
            return 0
        results.append((fmt, d, n))
        log(f"  {fmt:12s} {d} -> {n} frames")
        return n

    for fmt, subdir in SCAN_FORMATS:
        for d in filter(None, (root, os.path.join(root, subdir) if subdir else None)):
            if os.path.isdir(d) and try_source(fmt, d):
                break
    for db in find_vpf_databases(root):
        try_source("vpf", db)
    return results


def list_data_sources(db_path):
    """[(id, format, path, frame_count)] straight from the catalog SQLite."""
    if not db_path or db_path == ":memory:" or not os.path.exists(db_path):
        return []
    try:
        con = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
        rows = con.execute(
            "SELECT ds.id, ds.format, ds.path, COUNT(c.id) FROM data_sources ds"
            " LEFT JOIN coverage c ON c.data_source_id = ds.id"
            " GROUP BY ds.id ORDER BY ds.id").fetchall()
        con.close()
        return rows
    except sqlite3.Error:
        return []


def frame_counts_by_series(db_path):
    """{series_id: frame_count} for menu labels; empty if unreadable."""
    if not db_path or db_path == ":memory:" or not os.path.exists(db_path):
        return {}
    try:
        con = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
        rows = con.execute(
            "SELECT series_id, COUNT(*) FROM coverage GROUP BY series_id").fetchall()
        con.close()
        return dict(rows)
    except sqlite3.Error:
        return {}


# ----------------------------------------------------------------------------
# Overlays
# ----------------------------------------------------------------------------

class Crosshair(pyfvw.overlay.Overlay):
    """Center crosshair + last-click marker."""

    def __init__(self, app):
        super().__init__("crosshair")
        self.app = app
        self.click = None

    def on_draw(self, proj, canvas):
        cx, cy = self.app.W // 2, self.app.H // 2
        canvas.draw_lines([(cx - 12, cy), (cx + 12, cy)], color=(255, 60, 60))
        canvas.draw_lines([(cx, cy - 12), (cx, cy + 12)], color=(255, 60, 60))
        if self.click:
            x, y = self.click
            canvas.fill_polygon(
                [[(x, y - 6), (x + 6, y), (x, y + 6), (x - 6, y)]],
                fill=(255, 220, 0, 200))


class CoverageOverlay(pyfvw.overlay.Overlay):
    """Where is there data? Color-coded footprints of every catalog frame in
    the viewport, one color per format, with an on-canvas legend. Families are
    individually toggleable from the Overlays menu."""

    MAX_ROWS = 4000
    CLAMP = 20000  # keep world-tile boxes from exploding at deep zoom

    def __init__(self, app):
        super().__init__("coverage")
        self.app = app
        # Visible from birth: a static overlay that exists IS on (A6 — the
        # session creates it when the user asks for it and removes it when
        # they do not), so a hidden one would simply never appear.
        self.enabled_formats = set(COVERAGE_COLORS)

    def _draw_box(self, proj, canvas, lat0, lon0, lat1, lon1, color):
        try:
            x0, y0 = proj.geo_to_surface(pyfvw.geo.GeoPoint(lat1, lon0))  # UL
            x1, y1 = proj.geo_to_surface(pyfvw.geo.GeoPoint(lat0, lon1))  # LR
        except pyfvw.FvError:
            return
        c = self.CLAMP
        x0, y0 = max(-c, min(c, x0)), max(-c, min(c, y0))
        x1, y1 = max(-c, min(c, x1)), max(-c, min(c, y1))
        if x1 - x0 < 1 or y1 - y0 < 1:
            return
        ring = [(int(x0), int(y0)), (int(x1), int(y0)),
                (int(x1), int(y1)), (int(x0), int(y1))]
        canvas.fill_polygon([ring], fill=color + (32,),
                            outline=color, outline_width=1)

    def on_draw(self, proj, canvas):
        app = self.app
        if app.catalog is None:
            return
        try:
            viewport = proj.bounds
        except pyfvw.FvError:
            return
        rows = app.catalog.select_by_geo_rect(viewport)
        counts = {}
        for r in rows[: self.MAX_ROWS]:
            s = app.series_by_id.get(r.series_id)
            if s is None or s.format == "dted":  # same cells as dted-shaded
                continue
            if s.format not in self.enabled_formats:
                continue
            color = COVERAGE_COLORS.get(s.format)
            if color is None:
                continue
            counts[s.format] = counts.get(s.format, 0) + 1
            b = r.bounds
            if b.crosses_antimeridian:
                self._draw_box(proj, canvas, b.ll.lat, b.ll.lon, b.ur.lat, 180.0, color)
                self._draw_box(proj, canvas, b.ll.lat, -180.0, b.ur.lat, b.ur.lon, color)
            else:
                self._draw_box(proj, canvas, b.ll.lat, b.ll.lon, b.ur.lat, b.ur.lon, color)

        # Legend (top-left): swatch + "format — n frames in view".
        x, y = 10, 10
        lines = [(fmt, counts.get(fmt, 0)) for fmt in COVERAGE_COLORS
                 if fmt in self.enabled_formats and counts.get(fmt)]
        if not lines:
            return
        box_h = 8 + 18 * len(lines)
        canvas.fill_polygon(
            [[(x - 4, y - 4), (x + 210, y - 4), (x + 210, y + box_h), (x - 4, y + box_h)]],
            fill=(0, 0, 0, 140))
        for fmt, n in lines:
            color = COVERAGE_COLORS[fmt]
            canvas.fill_polygon(
                [[(x, y + 3), (x + 12, y + 3), (x + 12, y + 15), (x, y + 15)]],
                fill=color + (255,), outline=(255, 255, 255))
            if app.font:
                canvas.draw_text(f"{fmt} - {n} in view", x + 18, y + 14,
                                 color=(255, 255, 255), size=12.0)
            y += 18


# ----------------------------------------------------------------------------
# The application
# ----------------------------------------------------------------------------

class PythonView(pyfvw.app.AppShell):
    """Model + tk shell. The model half (catalog/engine/render_array) works
    headless for --shot and tests; run() adds the tk UI on top.

    A6: PythonView IS the AppShell (pyfvw.app.AppShell) — the seam the core
    calls back into whenever it needs a user to decide something. Rule R1 says
    the core never opens a dialog; every `AskSave`, `CFileDialog` and snap-to
    chooser FalconView called inline is one of the methods in the "the shell"
    section below, and the flows in `OverlaySession` cannot tell a tk dialog
    from the scripted `FakeShell` the C++ tests use.

    EVERY SHELL METHOD MUST WORK HEADLESS. The model half runs with no tk at
    all (--shot, the pytest suite), and a flow that reached a dialog there
    would hang a test rather than fail one — so each of them answers "cancel"
    when `self.tk` is None, which is the safe answer for all five.
    """

    def __init__(self, db_path=DEFAULT_DB, settings_path=None):
        super().__init__()
        # FIRST, before anything below can call back into this object as a
        # shell: creating the demo route runs a session flow, which enters an
        # editor, which calls on_editor_changed -- and a shell method that
        # asked whether there was a window yet would find no attribute at all.
        self.tk = None
        self._tkmod = None
        self.label = None              # the map widget; None until run()
        self._hover_hint = ""          # what the pick session last reported
        self._editor_tools = []        # the active editor's palette, as data
        # Settings first: everything below takes its default from the file when
        # the file has an opinion. See port/peregrine.ini.sample for the keys,
        # and fvkit/settings.h for the search path. Read once, at startup.
        self.settings = pyfvw.Settings()
        try:
            self.settings.load(settings_path or "")
        except pyfvw.FvError as e:
            # A broken settings file must not stop the app starting — it says
            # what is wrong (with the line number) and runs on defaults.
            print(f"settings: {e.message}", file=sys.stderr)
        cfg = self.settings
        if cfg.path:
            print(f"settings: {cfg.path}", file=sys.stderr)

        self.db_path = cfg.get("catalog.db", db_path) if db_path == DEFAULT_DB \
            else db_path
        self.catalog = None
        self.W, self.H = 1000, 700
        self.center = pyfvw.geo.GeoPoint(0.0, 0.0)
        self.mm_per_pixel = cfg.get_float("display.mm_per_pixel",
                                          NATIVE_MM_PER_PIXEL)
        self.font = _find_host_font()

        # Raster state.
        self.engine = None
        self.series = None            # active SeriesRow (any family)
        self.series_by_id = {}
        self.fixed_denom = 500e3      # display scale for scale-less series

        # Vector state.
        self.vproj = pyfvw.engine.MapProjection()
        self.style = None             # shared GeoSymStyleEngine (DNC)
        self.s52 = None               # shared S52StyleEngine (ENC)
        self.vstyle = None            # whichever of the two the active series uses
        self.geosym_dir = cfg.get("geosym.data_dir", TESTDATA)
        # Holds chartsymbols.xml (the S-52 presentation library) and the S-57
        # Appendix A CSVs. Both are git-ignored data supplied by the user, so
        # like geosym.data_dir this is a settings key, not a constant.
        self.enc_dir = cfg.get("enc.data_dir", os.path.join(TESTDATA, "enc"))
        self.show_meta = cfg.get_bool("enc.show_meta_objects", False)
        # The MapLibre style sheet OSM is drawn with. Unlike GeoSym's tables
        # and the S-52 presentation library, this one ships WITH the port
        # (port/Osm/styles) rather than with the data — it is a deliverable of
        # O2, not something the user supplies — so the default is a repo path
        # and the settings key exists to point at a different sheet.
        self.osm_style_path = cfg.get(
            "osm.style", os.path.join(REPO, "port", "Osm", "styles",
                                      "peregrine-osm.json"))
        # A6/schema 2: the icon set File > Sample points embeds in the
        # starter document. Loose <name>.png files — the port ships none, so
        # like geosym.data_dir this is user-supplied test data — and the
        # artwork is copied INTO the document, so this path is read once at
        # write time and never again.
        self.point_symbol_dir = cfg.get(
            "points.symbol_dir",
            os.path.join(self.geosym_dir, "GeoSymbol", "makiPng"))
        # O4: the routable road graph the route overlay follows on "r". A
        # build artifact (fvgraph build), not sample data, so there is no
        # sensible default path — without the key the overlay says so and
        # keeps its straight legs.
        self.road_graph_path = cfg.get("routing.graph", "")
        # O5c: the JSON cost rules the route is priced with, and which profile
        # in them "r" follows. Empty path = the weights compiled into the
        # router, which are the O5b numbers exactly — so the key is a tuning
        # surface, never a dependency, and an app told nothing routes as it
        # always did. port/Routing/rules/route-weights.json is that default
        # written out where it can be edited.
        self.route_rules_path = cfg.get("routing.rules", "")
        self.route_profile = cfg.get("routing.profile", "")
        self.osm = None               # shared OsmStyleEngine
        self._osm_ref_lat = None      # latitude that engine is currently set to
        # R3a knobs, applied to every vector renderer as it is created.
        self.scene_margin = cfg.get_float("vector.scene_margin", 0.25)
        self.simplify_px = cfg.get_float("vector.simplify_pixels", 0.0)
        # Label sizing. 0 = constant on-screen size, the core default; a scale
        # denominator makes labels scale with the map, as if their authored
        # pixel size had been chosen at that scale. Off means road names stay
        # legible at every zoom; on means they belong to the ground.
        self.label_ref_scale = cfg.get_float("vector.label_reference_scale", 0.0)
        self.vsources = {}            # series_id -> (source, renderer)
        self.vrenderer = None
        self.vsource = None
        self.vscale = 50e3
        self.feature_scale = 1.0
        self.labels = False
        self.brightness = cfg.get_int("geosym.brightness", 0)
        self.contrast = cfg.get_int("geosym.contrast", 0)

        # The MARINER's depth numbers, shared by DNC and ENC (see
        # fvkit/vector/mariner.h). There is no dialog for these: the ini is the
        # UI. A key that is absent leaves the PRODUCT's own default alone,
        # which is why each getter's fallback is read back off the engine
        # rather than written here — DNC's defaults are not S-52's.
        self.mariner_keys = {
            "safety_contour": cfg.get_float("mariner.safety_contour", -1.0),
            "shallow_contour": cfg.get_float("mariner.shallow_contour", -1.0),
            "deep_contour": cfg.get_float("mariner.deep_contour", -1.0),
            "safety_depth": cfg.get_float("mariner.safety_depth", -1.0),
        }
        self.mariner_two_shades = cfg.get("mariner.two_shades", "")
        self.mariner_shallow_pattern = cfg.get("mariner.shallow_pattern", "")
        # Data families: one JSON file per product, listing named groups with
        # an on/off flag. port/families/*.json ship as starters with every
        # family ON, so pointing at them changes nothing until one is edited.
        self.family_files = {
            "dnc": cfg.get("vector.families_dnc", ""),
            "enc": cfg.get("vector.families_enc", ""),
            "osm": cfg.get("vector.families_osm", ""),
        }
        self.families = {}            # product -> FamilySet, for diagnostics

        self.canvas = None
        self._make_canvas()
        self.mgr = pyfvw.overlay.OverlayManager()

        # --- the app layer (A6) ---------------------------------------------
        # THE REGISTRY IS THE APPLICATION'S INVENTORY: what can be opened, what
        # the menus offer, and how a file spec finds the code that reads it.
        # Two of its three types come from fvkit itself — the graticule and the
        # SQLite point overlay — and the third is written in Python, which is
        # the point: a type is a descriptor, and the language its factory is
        # written in does not reach the framework.
        self.editors = None                 # RouteEditor asks for this; see below
        self.registry = pyfvw.app.OverlayTypeRegistry()
        pyfvw.app.register_builtin_types(self.registry)
        self.registry.register(route_mod.route_type_desc(
            factory=self._make_route, editor_factory=lambda: RouteEditor(self)))
        # With a registry, `add` inserts by the type's display order instead of
        # blindly on top, and the top-most band draws last (A2).
        self.mgr.set_type_registry(self.registry)
        self.session = pyfvw.app.OverlaySession(self.registry, self.mgr, self,
                                                self.settings)
        self.editors = pyfvw.app.EditorManager(self.registry, self.mgr, self)
        # Mutual, and it has to be wired both ways: the editor manager asks the
        # session to CREATE the overlay a mode needs, the session tells the
        # editor manager a document was created.
        self.editors.set_session(self.session)
        self.session.set_editor_manager(self.editors)
        self.pick = pyfvw.app.PickSession(self.mgr, self)

        # The app's own two overlays are STATIC types: at most one of each,
        # toggled rather than opened, which is what a descriptor with no
        # `file` means. Registering them buys three things this app used to
        # hand-roll — the Overlays menu is now the registry, the crosshair
        # lands in the TOP-MOST band (drawn over everything however the stack
        # is reordered, which an untyped overlay cannot ask for), and
        # restore_at_startup puts the crosshair up without a line of code.
        self.registry.register(pyfvw.app.OverlayTypeDesc(
            id=CROSSHAIR_TYPE_ID, display_name="Crosshair", icon="crosshair",
            factory=lambda: Crosshair(self),
            is_top_most=True, restore_at_startup=True))
        self.registry.register(pyfvw.app.OverlayTypeDesc(
            id=COVERAGE_TYPE_ID, display_name="Coverage", icon="coverage",
            factory=lambda: CoverageOverlay(self),
            default_display_order=800))
        self.session.restore_startup_overlays()

        # The demo route, and the app's one EDITABLE overlay: click selects a
        # waypoint, "a" arms add-point (the next click inserts after it), "d"
        # deletes. Created through the FLOW rather than by hand, so it is a
        # document from the first frame: it has a type, the editor auto-enters
        # on create, and File > Save As has something to write.
        # Kiawah Island, because that is where the routable graph is: both
        # ends are exact OSM junctions (they snap 0 m), so "r" and "b" have
        # something to follow the moment the app opens. West end is Ruddy
        # Turnstone; east end is where Boardwalk 12 — beach access 12 — meets
        # Eugenia Avenue (OSM node 2532255833).
        self.session.new_file_overlay(route_mod.ROUTE_TYPE_ID)
        self.route = self.mgr.first_of_type(route_mod.ROUTE_TYPE_ID)
        if self.route is not None:
            # FileNew empties a new document, so the demo waypoints go in
            # after it — and the route is NOT dirty, or quitting would ask to
            # save a route the user never touched.
            self.route.name = "Ruddy Turnstone to the beach"
            self.route.waypoints = [
                ("RTURN", 32.6044007, -80.1083007),
                ("BA12",  32.5957369, -80.1097501),
            ]
            self.route.dirty = False

        self.mouse_readout = ""
        self.render_error = None
        self._frames, self._ms = 0, 0.0
        self.info_lines = []
        self.last_array = None

    # --- the app layer -----------------------------------------------------

    def _make_route(self):
        """The route type's factory. It exists to bind the SETTINGS to the
        overlay — the graph, the rule file and the profile are the
        application's, not the type's — which is exactly why an
        OverlayTypeDesc takes a callable and not a class."""
        return RouteOverlay("Route", [], graph_path=self.road_graph_path,
                            rules_path=self.route_rules_path,
                            profile=self.route_profile,
                            manager=self.mgr)

    @property
    def cross(self):
        """The crosshair, or None when it is off — a static overlay is not
        hidden, it is absent."""
        return self.mgr.first_of_type(CROSSHAIR_TYPE_ID)

    @property
    def coverage(self):
        return self.mgr.first_of_type(COVERAGE_TYPE_ID)

    @property
    def grid(self):
        """The graticule, or None when it is off. It is a STATIC type: at most
        one instance, toggled on and off rather than opened, which is the
        whole meaning of a descriptor with no `file` (plan §1.2)."""
        return self.mgr.first_of_type(pyfvw.app.GRID_TYPE_ID)

    def _set_grid(self, on):
        self._set_static(pyfvw.app.GRID_TYPE_ID, on)

    def _set_static(self, type_id, on):
        """Toggling a static overlay IS the flow — there is no visibility flag
        to set, because for a type with at most one instance 'off' and 'not
        open' are the same state."""
        if on != (self.mgr.first_of_type(type_id) is not None):
            self.session.toggle_static(type_id)

    def _overlay_display_name(self, overlay):
        """What the user calls it: the file name when it has one, else the
        overlay's own name — the same rule the save prompt uses."""
        spec = overlay.file_spec
        return os.path.basename(spec) if spec else overlay.name

    # --- the shell (pyfvw.app.AppShell) ------------------------------------
    #
    # Every method below is the core asking this application to ask the user.
    # None of them decides anything: they carry a question out and an answer
    # back, and each one has a headless answer for when there is no tk.

    def ask_save(self, name):
        A = pyfvw.app.AppShell.SaveAnswer
        if self.tk is None:
            return A.DISCARD          # headless: never block on a prompt
        from tkinter import messagebox
        answer = messagebox.askyesnocancel(
            "PythonView", f"Save changes to {name}?", parent=self.tk)
        if answer is None:
            return A.CANCEL
        return A.SAVE if answer else A.DISCARD

    def choose_files_to_open(self, file_type):
        if self.tk is None:
            return []
        from tkinter import filedialog
        paths = filedialog.askopenfilenames(
            title="Open Overlay",
            filetypes=list(file_type.open_filters) + [("All Files", "*")],
            parent=self.tk)
        return list(paths)

    def choose_save_spec(self, file_type, suggested):
        # An empty path is the cancel; there is no second signal, because
        # "chose nothing" and "cancelled" are the same outcome for the caller.
        if self.tk is None:
            return ("", 0)
        from tkinter import filedialog
        ext = file_type.default_extension
        path = filedialog.asksaveasfilename(
            title="Save Overlay As",
            initialfile=(suggested or "untitled") +
                        (f".{ext}" if ext and "." not in suggested else ""),
            defaultextension=f".{ext}" if ext else "",
            filetypes=list(file_type.save_filters) + [("All Files", "*")],
            parent=self.tk)
        # One format only, so the index is 0. A type with several would map
        # the chosen filter back to its index here.
        return (path or "", 0)

    def choose_from_list(self, title, rows):
        """The ambiguity chooser — FalconView's snptodlg, generalised. This is
        one of the two seams A5 built and nothing called until now."""
        if self.tk is None or not rows:
            return None
        tk = self._tkmod
        win = tk.Toplevel(self.tk)
        win.title(title)
        win.transient(self.tk)
        chosen = []
        lb = tk.Listbox(win, width=52, height=min(10, len(rows)),
                        exportselection=False)
        for r in rows:
            lb.insert("end", r)
        lb.selection_set(0)
        lb.pack(padx=8, pady=8, fill="both", expand=True)

        def take():
            sel = lb.curselection()
            if sel:
                chosen.append(int(sel[0]))
            win.destroy()

        row = tk.Frame(win)
        row.pack(pady=(0, 8))
        tk.Button(row, text="OK", command=take).pack(side="left", padx=4)
        tk.Button(row, text="Cancel", command=win.destroy).pack(side="left")
        lb.bind("<Double-Button-1>", lambda _e: take())
        win.grab_set()
        self.tk.wait_window(win)
        return chosen[0] if chosen else None

    def confirm_revert(self, file_spec):
        if self.tk is None:
            return False
        from tkinter import messagebox
        return bool(messagebox.askyesno(
            "PythonView",
            f"{os.path.basename(file_spec)} is already open and has unsaved "
            "changes.\n\nDiscard them and re-read the file?", parent=self.tk))

    def set_cursor(self, cursor):
        if self.tk is None or self.label is None:
            return
        C = pyfvw.app.CursorId
        self.label.config(cursor={C.CROSSHAIR: "crosshair", C.HAND: "hand2",
                                  C.MOVE: "fleur", C.NO: "X_cursor",
                                  C.WAIT: "watch"}.get(cursor, ""))

    def show_hint(self, hint):
        # The tool tip goes on the status line too: a floating window per
        # mouse move is exactly what UpdateHover's change-only contract is
        # there to make possible, and it is still more UI than this app wants.
        self._hover_hint = hint.status # or hint.tool_tip
        self._update_status()

    def show_context_menu(self, x, y, menu):
        if self.tk is None:
            return
        popup = self._tk_menu_from(menu)
        try:
            popup.tk_popup(self.tk.winfo_rootx() + x, self.tk.winfo_rooty() + y)
        finally:
            popup.grab_release()

    def _tk_menu_from(self, node):
        """A MenuNode tree as a tk menu. The core describes menus as DATA and
        never as widgets (rule R1), so this is the only place that knows tk
        has a Menu class."""
        tk = self._tkmod
        menu = tk.Menu(self.tk, tearoff=0)
        for child in node.children:
            if child.is_separator:
                menu.add_separator()
            elif child.children:
                menu.add_cascade(label=child.label,
                                 menu=self._tk_menu_from(child))
            else:
                menu.add_command(
                    label=child.label,
                    state="normal" if child.enabled else "disabled",
                    command=lambda c=child: (c.invoke(), self.refresh()))
        return menu

    def request_invalidate(self):
        # Whole-view, deliberately: MapEngine plus the retained scene make a
        # full frame cheap (R3c), so region invalidation waits for a profile
        # that asks for it.
        if self.tk is not None:
            self.refresh()

    def on_editor_changed(self, type_id, editor):
        self._editor_tools = editor.tools() if editor is not None else []
        if self.tk is not None:
            self._rebuild_tools_menu()
            self._update_status()

    def report_error(self, code, message):
        if self.tk is None:
            print(f"pythonview: {message} ({code})", file=sys.stderr)
            return
        from tkinter import messagebox
        messagebox.showerror("PythonView", message, parent=self.tk)

    # --- model -------------------------------------------------------------

    def _make_canvas(self):
        self.canvas = pyfvw.canvas.CpuCanvas(self.W, self.H)
        if self.font:
            self.canvas.set_default_font(self.font)

    @property
    def mode(self):
        return "vector" if (self.series and
                            self.series.format in VECTOR_FORMATS) else "raster"

    @property
    def proj(self):
        return self.vproj if self.mode == "vector" else (
            self.engine.proj if self.engine else None)

    def open_catalog(self, db_path):
        pyfvw.catalog.register_builtin_formats()
        self.catalog = pyfvw.catalog.Catalog(db_path)
        self.db_path = db_path
        self._on_catalog_changed()

    def new_catalog_from_scan(self, db_path, scan_root, log=print):
        pyfvw.catalog.register_builtin_formats()
        self.catalog = None            # drop any open handle on this file
        self.engine = None
        if os.path.exists(db_path):
            os.remove(db_path)
        os.makedirs(os.path.dirname(db_path) or ".", exist_ok=True)
        cat = pyfvw.catalog.Catalog(db_path)
        results = scan_into_catalog(cat, scan_root, log=log)
        if not results:
            del cat
            if os.path.exists(db_path):
                os.remove(db_path)
            raise RuntimeError(f"no known map data found under {scan_root}")
        self.catalog = cat
        self.db_path = db_path
        self._on_catalog_changed()
        return results

    def _on_catalog_changed(self):
        """Rebuild engine + series index after any catalog mutation."""
        self.series_by_id = {s.id: s for s in self.catalog.series()}
        self.engine = pyfvw.engine.MapEngine(self.catalog)
        self.engine.set_surface(self.W, self.H)
        self.vsources.clear()
        self.vrenderer = self.vsource = None
        self._attach_elevation()
        if self.series is not None:
            # Re-resolve the active series in the new catalog (ids may move).
            match = [s for s in self.series_by_id.values()
                     if s.format == self.series.format
                     and s.series_key == self.series.series_key]
            self.series = match[0] if match else None
            if self.series is not None and self.series.format in VECTOR_FORMATS:
                try:
                    self._open_vector(self.series)
                except Exception:
                    self.series = None
        if self.series is None:
            self._pick_default_series()

    def _attach_elevation(self):
        for _sid, fmt, path, _n in list_data_sources(self.db_path):
            if fmt == "dted" and os.path.isdir(path):
                try:
                    self.engine.set_elevation_source(
                        pyfvw.formats.DtedElevationSource(path))
                    return
                except pyfvw.FvError:
                    pass

    def _pick_default_series(self):
        """Open at the smallest drawable scale (typically the world tile)."""
        scaled = [s for s in self.series_by_id.values() if s.scale_denom > 0]
        if scaled:
            self.set_series(max(scaled, key=lambda s: s.scale_denom),
                            recenter=True)

    def series_bounds_center(self, series):
        rows = self.catalog.select_by_geo_rect(pyfvw.geo.GeoRect.world(), series.id)
        if not rows:
            return pyfvw.geo.GeoPoint(0.0, 0.0)
        lat = sum((r.bounds.ll.lat + r.bounds.ur.lat) / 2 for r in rows) / len(rows)
        lon = sum((r.bounds.ll.lon + r.bounds.ur.lon) / 2 for r in rows) / len(rows)
        c = pyfvw.geo.GeoPoint(lat, lon)
        # Sparse coverage (e.g. the CADRG samples) can put the centroid in a
        # gap between frames; snap to the center of the nearest frame instead.
        if not self.catalog.select_by_geo_rect(pyfvw.geo.GeoRect(c, c), series.id):
            def dist2(r):
                rl = (r.bounds.ll.lat + r.bounds.ur.lat) / 2
                rn = (r.bounds.ll.lon + r.bounds.ur.lon) / 2
                return (rl - lat) ** 2 + (rn - lon) ** 2
            r = min(rows, key=dist2)
            c = pyfvw.geo.GeoPoint((r.bounds.ll.lat + r.bounds.ur.lat) / 2,
                                   (r.bounds.ll.lon + r.bounds.ur.lon) / 2)
        return c

    def _covers_center(self, series):
        rows = self.catalog.select_by_geo_rect(
            pyfvw.geo.GeoRect(self.center, self.center), series.id)
        return bool(rows)

    def set_series(self, series, recenter=False):
        """Switch the active series (any family). Keeps the current center
        when the new series has data there; otherwise jumps to its centroid.
        Raises on vector-open failure (caller reverts)."""
        prev = self.series
        self.series = series
        try:
            if series.format in VECTOR_FORMATS:
                self._open_vector(series)
            if series.format in VECTOR_FORMATS:
                # Frame the library like the old demo: the reader's own
                # bounds, not the catalog tile union (tighter fit).
                b = self.vsource.bounds
                if recenter or not self._covers_center(series):
                    self.center = pyfvw.geo.GeoPoint(
                        (b.ll.lat + b.ur.lat) / 2.0, (b.ll.lon + b.ur.lon) / 2.0)
                # A series that declares a scale opens AT it. For ENC that is
                # the usage band's own nominal scale (General 1:1,000,000,
                # Coastal 1:300,000, Approach 1:50,000, Harbour 1:12,000) —
                # the scale the cell was compiled for and the only one its
                # SCAMIN thinning is authored against. Fitting the band's
                # whole extent on screen instead, which is what this did,
                # opens a harbour chart zoomed so far out that every cell in
                # the band is on screen at once. DNC libraries carry no chart
                # scale, so they keep the fit.
                self.vscale = (series.scale_denom if series.scale_denom > 0
                               else self._fit_scale_rect(b))
            elif recenter or not self._covers_center(series):
                self.center = self.series_bounds_center(series)
            if series.format == "dted-shaded":
                self.fixed_denom = DTED_DEFAULT_SCALE.get(series.series_key, 250e3)
            if self.cross is not None:
                self.cross.click = None
            self.info_lines = []
        except Exception:
            self.series = prev
            raise

    def _fit_scale_rect(self, b):
        """1:N that fits GeoRect `b` in the current window."""
        clat = math.radians((b.ll.lat + b.ur.lat) / 2.0)
        mpd = 111320.0
        ground_w = max((b.ur.lon - b.ll.lon) * mpd * max(math.cos(clat), 1e-6), 1.0)
        ground_h = max((b.ur.lat - b.ll.lat) * mpd, 1.0)
        screen_w = self.W * NATIVE_MM_PER_PIXEL / 1000.0
        screen_h = self.H * NATIVE_MM_PER_PIXEL / 1000.0
        return max(ground_w / screen_w, ground_h / screen_h) * 1.1

    def _apply_mariner(self, style):
        """Push the [mariner] settings keys onto a freshly opened chart engine.

        Per-key, not wholesale: DNC and ENC ship DIFFERENT defaults (GeoSym's
        safety contour is 10 m with the shallow pattern on, S-52's is 30 m with
        it off), so a key nobody set must leave the product's own number alone
        rather than take the other product's.
        """
        m = style.mariner()
        for name, value in self.mariner_keys.items():
            if value >= 0.0:
                setattr(m, name, value)
        for name, text in (("two_shades", self.mariner_two_shades),
                           ("shallow_pattern", self.mariner_shallow_pattern)):
            if text:
                setattr(m, name, text.strip().lower() in
                        ("1", "on", "yes", "true"))

    def _apply_families(self, product, style):
        """Load this product's data-family file and hide what it says to hide.

        Families load FIRST and the engine's own rules() stay open afterwards,
        so a rule file or a menu can still put one thing back. A missing path
        is the normal case (no key set) and a broken file is reported and
        skipped — a chart with everything on beats no chart.
        """
        path = self.family_files.get(product, "")
        if not path:
            return
        fams = pyfvw.vector.FamilySet()
        try:
            fams.load_file(path)
            fams.append_rules(style.rules())
        except pyfvw.FvError as e:
            print(f"families[{product}]: {e.message}", file=sys.stderr)
            return
        self.families[product] = fams
        if fams.disabled_count:
            off = [f.name for f in fams.families if not f.enabled]
            print(f"families[{product}]: {len(fams)} families, "
                  f"off: {', '.join(off)}", file=sys.stderr)

    def _open_vector(self, series):
        """Open (or reuse) the source + style engine behind a vector series.

        Three products now, one seam: DNC through VPF + GeoSym, ENC through
        S-57 + the official S-52 presentation library, OSM through an MVT
        pyramid + a MapLibre GL style sheet. Everything downstream of
        `renderer` — the retained scene, identify, coverage, the render loop —
        is the same code for all three, which is the whole point of the vector
        seam and is why this method is the only place that forks.
        """
        if series.id in self.vsources:
            self.vsource, self.vrenderer, self.vstyle = self.vsources[series.id]
            return
        rows = self.catalog.select_by_geo_rect(pyfvw.geo.GeoRect.world(), series.id)
        if not rows:
            raise RuntimeError(f"no coverage rows for {series.series_key}")

        if series.format == "osm":
            # One row = one .mbtiles file = one series, so there is no set of
            # cells to choose between and no library directory to build: the
            # catalog's path IS what the source opens.
            source = pyfvw.vector.OsmVectorSource()
            source.open(rows[0].path)
            # The display pitch is half of the zoom<->scale relation, and the
            # source and the style engine must be given the SAME one (the
            # other half, the reference latitude, moves with the viewport and
            # is set per frame in _render_vector).
            source.set_display_mm_per_pixel(self.mm_per_pixel)
            if self.osm is None:
                self.osm = pyfvw.vector.OsmStyleEngine()
                self.osm.load_file(self.osm_style_path)
                self.osm.set_display_mm_per_pixel(self.mm_per_pixel)
                self._osm_ref_lat = None
                self._apply_families("osm", self.osm)
            style = self.osm
        elif series.format == "enc":
            # One ENC row = one cell file, and an ENC series IS a usage band —
            # so the source opens exactly the cells the catalog filed under
            # this band, all of them, and nothing else.
            #
            # It used to open the common ANCESTOR DIRECTORY of those cells, so
            # that panning across a band needed no source swap. That is the
            # right goal and the wrong mechanism: the common ancestor of an
            # exchange set is the exchange set, so every band opened every
            # cell and a harbour chart drew a 1:1,000,000 general cell
            # underneath itself. Invisible while TestData held band 5 alone;
            # the 2026-07-28 cells made it obvious. A band is a SCALE, not a
            # place — only naming the cells can say which one is wanted.
            source = pyfvw.vector.EncVectorSource()
            source.open_cells(sorted({r.path for r in rows}), self.enc_dir)
            if self.s52 is None:
                self.s52 = pyfvw.vector.S52StyleEngine()
                self.s52.open(self.enc_dir)
                self.s52.set_show_meta_objects(self.show_meta)
                self._apply_mariner(self.s52)
                self._apply_families("enc", self.s52)
            style = self.s52
        else:
            db_root = rows[0].path.split("|")[0]
            lib_dir = os.path.join(db_root, series.series_key)
            source = pyfvw.vector.VpfVectorSource()
            source.open(lib_dir)
            if self.style is None:
                self.style = pyfvw.vector.GeoSymStyleEngine()
                self.style.open(self.geosym_dir, pyfvw.vector.GEOSYM_DNC)
                self._apply_mariner(self.style)
                self._apply_families("dnc", self.style)
            style = self.style
        renderer = pyfvw.vector.VectorRenderer(source, style)
        # R3a: retain a scene larger than the window, so a drag-pan re-projects
        # and redraws but does not re-query VPF or re-run GeoSym (~40% of a
        # vector frame); and optionally thin the geometry for drawing. Both are
        # settings-file knobs — [vector] scene_margin / simplify_pixels. The
        # scene rebuilds itself on a zoom, a feature-scale change, or any style
        # change (the engine's epoch).
        renderer.set_scene_margin(self.scene_margin)
        renderer.set_simplify_pixels(self.simplify_px)
        renderer.set_label_reference_scale(self.label_ref_scale)
        self.vsources[series.id] = (source, renderer, style)
        self.vsource, self.vrenderer, self.vstyle = source, renderer, style

    def set_surface_size(self, w, h):
        if (w, h) == (self.W, self.H):
            return
        self.W, self.H = w, h
        self._make_canvas()
        if self.engine:
            self.engine.set_surface(w, h)

    # --- rendering ---------------------------------------------------------

    def render_array(self):
        """Render the current view; returns the (h, w, 4) numpy array."""
        t0 = time.time()
        if self.catalog is None or self.series is None:
            self.canvas.clear((24, 24, 24))
            if self.font:
                self.canvas.draw_text("No map data - File > Add Map Data...",
                                      20, self.H // 2, color=(200, 200, 200),
                                      size=16.0)
            self._frames = 0
        else:
            # A single undecodable frame aborts the engine's whole composite
            # (e.g. the big-endian TIFFs in TestData — see the ledger note);
            # keep the app alive and say so in the status bar instead.
            try:
                self.render_error = None
                if self.mode == "vector":
                    self._render_vector()
                else:
                    self._render_raster()
            except pyfvw.FvError as e:
                self.render_error = str(e)
                self._frames = 0
        self._ms = (time.time() - t0) * 1000
        self.last_array = np.asarray(self.canvas.buffer)
        return self.last_array

    def _render_raster(self):
        self.engine.set_center(self.center)
        if self.series.scale_denom > 0:
            self.engine.set_physical_scale(
                self.series.scale, self.series.scale_units, self.mm_per_pixel)
        else:  # dted-shaded: no chart scale; display at the chosen 1:N
            self.engine.set_physical_scale(self.fixed_denom, 0, self.mm_per_pixel)
        self.canvas.clear((24, 24, 24))
        self._frames = self.engine.render(self.canvas, self.series.id)
        self.mgr.draw_all(self.engine.proj, self.canvas)

    def _render_vector(self):
        self.vproj.set_surface_size(self.W, self.H)
        self.vproj.set_center(self.center)
        self.vproj.set_physical_scale(self.vscale, self.mm_per_pixel)
        self.vrenderer.set_symbol_scale(self.feature_scale)
        self.vrenderer.set_device_dpi(
            _dpi_for(self.mm_per_pixel) * self.feature_scale)
        self.vstyle.set_draw_labels(self.labels and self.font is not None)
        self.vrenderer.set_label_reference_scale(self.label_ref_scale)
        # Brightness/contrast is a GeoSym knob (it adjusts the DNC colour
        # table). S-52 has no equivalent by design — the mariner picks a
        # day/dusk/night COLOUR TABLE instead, which is a chart-correctness
        # rule, not a preference, so there is nothing to forward here.
        if hasattr(self.vstyle, "set_color_adjust"):
            self.vstyle.set_color_adjust(self.brightness, self.contrast)
        if self.series.format == "osm":
            self._sync_osm_zoom_inputs()
        # A paper chart is white; a GL style says what its own background is
        # (`background` is a style layer, not a feature, so it cannot arrive
        # as a StyleResult and the application has to clear with it).
        bg = (255, 255, 255)
        if hasattr(self.vstyle, "background"):
            c = self.vstyle.background(self.vscale)
            if c:
                bg = c[:3]
        self.canvas.clear(bg)
        self.vrenderer.render(self.vproj, self.canvas)
        self._frames = self.vrenderer.features_queried
        self.mgr.draw_all(self.vproj, self.canvas)

    # Latitude step at which the OSM style engine's reference latitude is
    # re-set. Web Mercator's zoom<->scale relation is latitude-dependent, so
    # the engine has to be told where the map is (it gets a scale and no
    # geography) — but setting it every frame would bump the style epoch on
    # every pan and the R3a retained scene would never be reused, which the
    # ledger names as the way that cache silently stops working. Half a degree
    # is worth about 0.01 of a zoom level at these latitudes: far below the
    # rounding the source does when it picks a level, and the visible cost of
    # a rebuilt scene is much larger than the invisible one of that error.
    OSM_REF_LAT_STEP = 0.5

    def _sync_osm_zoom_inputs(self):
        """Keep the OSM style engine's half of the zoom<->scale relation in
        step with the source's. Both must derive the SAME zoom from a scale or
        the style switches layers on at a scale that does not match the tile
        level the source read — the one failure O2 was designed around."""
        self.vsource.set_display_mm_per_pixel(self.mm_per_pixel)
        self.vstyle.set_display_mm_per_pixel(self.mm_per_pixel)
        lat = round(self.center.lat / self.OSM_REF_LAT_STEP) * self.OSM_REF_LAT_STEP
        if lat != self._osm_ref_lat:
            self.vstyle.set_reference_latitude(lat)
            self._osm_ref_lat = lat

    # --- scale ladder / zoom (shared by keys and menus) --------------------

    def _scales_at(self, pt):
        rows = self.catalog.select_by_geo_rect(pyfvw.geo.GeoRect(pt, pt))
        seen = {}
        for r in rows:
            s = self.series_by_id.get(r.series_id)
            if s and s.scale_denom > 0:
                seen[s.id] = s
        return sorted(seen.values(), key=lambda s: s.scale_denom)

    def step_scale(self, delta):
        """PageUp/Down: nearest larger/smaller-scale series AT the center.

        Vector series ride this too, and ENC is the case it was made for: its
        series ARE scale bands (General -> Coastal -> Approach -> Harbour), so
        stepping one is the same gesture as stepping a raster scale ladder and
        should not be a different key. `_scales_at` already ignores series
        that declare no scale, so a DNC library — which is a place, not a
        scale — is never a candidate and nothing steps to or from it.
        """
        if self.catalog is None or self.series is None:
            return None
        cur = self.series.scale_denom
        if cur <= 0:
            return None
        here = self._scales_at(self.center)

        def nearest(pool):
            if delta < 0:
                c = [s for s in pool if s.scale_denom < cur]
                return c[-1] if c else None
            c = [s for s in pool if s.scale_denom > cur]
            return c[0] if c else None

        # The current PRODUCT gets first refusal, and only when it has no next
        # step does the ladder cross to another one. Charleston is the case
        # that needs it: ENC, CADRG, the DOQs and DTED all cover that water, so
        # a flat nearest-scale rule walks a mariner out of the chart he chose
        # and into a topo sheet halfway up the band ladder. Paging within a
        # product until it runs out is what a scale ladder means to someone
        # reading one.
        nxt = nearest([s for s in here if s.format == self.series.format])
        if nxt is None:
            nxt = nearest(here)
        if nxt is not None:
            self.set_series(nxt)
        return nxt

    def zoom(self, factor):
        """-/= or wheel. factor None resets. Semantics per family: scaled
        raster adjusts the assumed pixel pitch (physical zoom); scale-less
        raster (DTED) and vector adjust the display scale 1:N."""
        if self.series is None:
            return
        if self.mode == "vector":
            if factor is None:
                # Reset goes where the series OPENS — its own nominal scale
                # when it declares one (an ENC band), the whole library
                # otherwise (a DNC library declares no chart scale).
                self.vscale = (self.series.scale_denom
                               if self.series.scale_denom > 0
                               else self._fit_scale_rect(self.vsource.bounds))
                self.feature_scale = 1.0
            else:
                self.vscale = max(1e3, min(5e8, self.vscale * factor))
        elif self.series.scale_denom <= 0:
            if factor is None:
                self.fixed_denom = DTED_DEFAULT_SCALE.get(
                    self.series.series_key, 250e3)
            else:
                self.fixed_denom = max(5e3, min(5e8, self.fixed_denom * factor))
        else:
            if factor is None:
                self.mm_per_pixel = NATIVE_MM_PER_PIXEL
            else:
                self.mm_per_pixel = max(NATIVE_MM_PER_PIXEL / 64.0,
                                        min(NATIVE_MM_PER_PIXEL * 256.0,
                                            self.mm_per_pixel * factor))

    def pan_pixels(self, dx, dy):
        proj = self.proj
        if proj is None:
            return
        try:
            dpp_lat, dpp_lon = proj.deg_per_pixel_lat, proj.deg_per_pixel_lon
        except pyfvw.FvError:
            return
        self.center = pyfvw.geo.GeoPoint(self.center.lat - dy * dpp_lat,
                                         self.center.lon + dx * dpp_lon)
        self.center.normalize()

    # --- identify (vector) -------------------------------------------------

    def identify(self, x, y):
        """Hit-test the drawn scene; returns display lines for the panel."""
        try:
            p = self.proj.surface_to_geo(x, y)
            head = f"{p.lat:+.5f} {p.lon:+.5f}"
        except pyfvw.FvError:
            head = "?"
        hits = self.vrenderer.pick_index.hit_test(x, y, 4.0)
        if not hits:
            return [f"{head}   nothing here"]
        lines = [f"{head}   {len(hits)} feature(s) here, topmost first:"]
        for h in hits[:3]:
            try:
                d = self.vsource.describe(h.ref)
            except Exception as exc:
                lines.append(f"  {h.ref}  ({exc})")
                continue
            lines.append(f"  {d.title}  [{d.layer_name}]  pri {h.priority}"
                         f"  {h.distance:.1f}px")
            shown = [a for a in d.attributes if a.code != "f_code" and a.display]
            for a in shown[:4]:
                lines.append(f"      {a.name or a.code}: {a.display}")
            lines.append(f"      -- {d.source_note}")
        for h in hits[3:]:
            lines.append(f"  (also {h.ref})")
        return lines

    # ========================================================================
    # tk shell
    # ========================================================================

    def run(self, first_run_scan_prompt=False, before_mainloop=None):
        import tkinter as tk
        from tkinter import ttk
        self._tkmod = tk
        self._ttk = ttk

        self.tk = tk.Tk()
        self.tk.title("PythonView")
        self.tk.minsize(480, 400)

        self._build_menus()

        # Layout: status bar and identify panel claim the bottom; the map
        # frame takes everything else and drives the render surface size.
        self.status = tk.Label(self.tk, anchor="w", font=("Menlo", 11))
        self.status.pack(side="bottom", fill="x")
        self.info = tk.Text(self.tk, height=9, font=("Menlo", 10),
                            state="disabled", takefocus=0)
        self._info_visible = False

        self.map_frame = tk.Frame(self.tk, width=self.W, height=self.H,
                                  background="#181818")
        self.map_frame.pack(side="top", fill="both", expand=True)
        self.map_frame.pack_propagate(False)
        self.label = tk.Label(self.map_frame, borderwidth=0, background="#181818")
        self.label.pack()

        self._resize_job = None
        self.map_frame.bind("<Configure>", self._on_configure)

        self._bind_keys()
        self.label.bind("<ButtonPress-1>", self._on_press)
        self.label.bind("<B1-Motion>", self._on_drag)
        self.label.bind("<ButtonRelease-1>", self._on_release)
        self.label.bind("<Shift-Button-1>", self._on_shift_click)
        self.label.bind("<Motion>", self._on_motion)
        self.label.bind("<Leave>", lambda e: self._set_readout(""))
        self.label.bind("<MouseWheel>", self._on_wheel)
        self.label.bind("<Button-3>", self._on_right_click)
        self.label.bind("<Control-Button-1>", self._on_right_click)
        self._drag = None
        self._drag_moved = False
        self._last_drag_render = 0.0
        # True between a press an overlay TOOK and its release: the map does
        # not pan, and every move goes to the overlay instead. See _on_press.
        self._overlay_gesture = False

        if first_run_scan_prompt:
            self.tk.after(200, self._first_run_prompt)
        self.refresh()
        if before_mainloop:
            before_mainloop()
        self.tk.mainloop()

    # --- menus -------------------------------------------------------------

    def _build_menus(self):
        tk = self._tkmod
        self.menubar = tk.Menu(self.tk)

        m_file = tk.Menu(self.menubar, tearoff=0)
        # THE FILE MENU IS BUILT FROM THE REGISTRY (A6). Every file type gets a
        # New item without this code knowing what a route or a point set is;
        # the Open item offers the union of every type's filters and dispatches
        # the chosen path by extension.
        m_new = tk.Menu(m_file, tearoff=0)
        for desc in self.registry.all():
            if desc.is_file and desc.display_name:
                m_new.add_command(
                    label=desc.display_name,
                    command=lambda d=desc: self._ui_flow(
                        lambda: self.session.new_file_overlay(d.id)))
        m_file.add_cascade(label="New Overlay", menu=m_new)
        m_file.add_command(
            label="Open Overlay...", accelerator="Ctrl-O",
            command=lambda: self._ui_flow(
                lambda: self.session.open_file_overlays("")))
        m_file.add_command(label="Open Sample Points (demo document)",
                           command=self._ui_sample_points)
        m_file.add_command(label="Save Overlay", accelerator="Ctrl-S",
                           command=self._ui_save)
        m_file.add_command(label="Save Overlay As...", command=self._ui_save_as)
        m_file.add_command(label="Close Overlay", command=self._ui_close)
        m_file.add_separator()
        m_file.add_command(label="Open Catalog...", command=self._ui_open_catalog)
        m_file.add_command(label="New Catalog from Scan...",
                           command=self._ui_new_catalog)
        m_file.add_separator()
        m_file.add_command(label="Add Map Data...", command=self._ui_add_data)
        m_file.add_command(label="Manage Data Sources...",
                           command=self._ui_manage_sources)
        m_file.add_separator()
        m_file.add_command(label="Save Screenshot...", command=self._ui_screenshot)
        m_file.add_separator()
        # Quitting goes through the session, so a dirty document is offered a
        # save and a cancel really does abort the exit — the File-Overlay
        # behaviour the app did not have before A6.
        m_file.add_command(label="Quit", accelerator="q", command=self._ui_quit)
        self.menubar.add_cascade(label="File", menu=m_file)

        self.m_map = tk.Menu(self.menubar, tearoff=0)
        self.menubar.add_cascade(label="Map", menu=self.m_map)
        self.series_var = tk.StringVar(value="")

        m_view = tk.Menu(self.menubar, tearoff=0)
        m_view.add_command(label="Zoom In", accelerator="=",
                           command=lambda: self._ui_zoom(1.0 / ZOOM_STEP))
        m_view.add_command(label="Zoom Out", accelerator="-",
                           command=lambda: self._ui_zoom(ZOOM_STEP))
        m_view.add_command(label="Reset Zoom", accelerator="0",
                           command=lambda: self._ui_zoom(None))
        m_view.add_separator()
        m_view.add_command(label="Larger Scale Here", accelerator="PageUp",
                           command=lambda: self._ui_step_scale(-1))
        m_view.add_command(label="Smaller Scale Here", accelerator="PageDown",
                           command=lambda: self._ui_step_scale(+1))
        m_view.add_separator()
        m_view.add_command(label="Bigger Features (vector)", accelerator="]",
                           command=lambda: self._ui_feature(FEATURE_STEP))
        m_view.add_command(label="Smaller Features (vector)", accelerator="[",
                           command=lambda: self._ui_feature(1.0 / FEATURE_STEP))
        m_view.add_separator()
        m_view.add_command(label="Go To Location...", command=self._ui_goto)
        m_view.add_command(label="Recenter on Data", command=self._ui_recenter)
        self.menubar.add_cascade(label="View", menu=m_view)

        m_ovl = tk.Menu(self.menubar, tearoff=0)
        self.var_grid = tk.BooleanVar(value=self.grid is not None)
        self.var_cross = tk.BooleanVar(value=self.cross is not None)
        self.var_cov = tk.BooleanVar(value=self.coverage is not None)
        m_ovl.add_checkbutton(label="Lat/Lon Grid", accelerator="g",
                              variable=self.var_grid, command=self._ui_apply_overlays)
        m_ovl.add_checkbutton(label="Crosshair", variable=self.var_cross,
                              command=self._ui_apply_overlays)
        m_ovl.add_separator()
        m_ovl.add_checkbutton(label="Coverage Overlay", accelerator="c",
                              variable=self.var_cov, command=self._ui_apply_overlays)
        self.var_cov_fmt = {}
        m_cov = tk.Menu(m_ovl, tearoff=0)
        for fam, fmts in FAMILIES:
            for fmt in fmts:
                v = tk.BooleanVar(value=True)
                self.var_cov_fmt[fmt] = v
                m_cov.add_checkbutton(label=f"{fam}: {fmt}", variable=v,
                                      command=self._ui_apply_overlays)
        m_ovl.add_cascade(label="Coverage Families", menu=m_cov)
        m_ovl.add_separator()
        self.var_labels = tk.BooleanVar(value=self.labels)
        m_ovl.add_checkbutton(label="Feature Labels (vector)", accelerator="l",
                              variable=self.var_labels,
                              command=self._ui_apply_overlays)
        # Turning this on pins the CURRENT scale as the reference, so the text
        # does not jump the moment you tick it — it starts scaling from here.
        self.var_label_ground = tk.BooleanVar(value=self.label_ref_scale > 0)
        m_ovl.add_checkbutton(label="Labels Scale With The Map (vector)",
                              variable=self.var_label_ground,
                              command=self._ui_apply_overlays)
        # S-57 meta objects describe the DATASET, not the world: M_QUAL's
        # zones of confidence, M_COVR's coverage, M_NSYS. Off by default (they
        # pattern over the whole chart), but a mariner does ask for the ZOC
        # overlay, so it is a toggle rather than a hard exclusion.
        self.var_meta = tk.BooleanVar(value=self.show_meta)
        m_ovl.add_checkbutton(label="ENC: Data Quality / Coverage (M_*)",
                              variable=self.var_meta,
                              command=self._ui_apply_overlays)
        self.menubar.add_cascade(label="Overlays", menu=m_ovl)

        self.m_tools = tk.Menu(self.menubar, tearoff=0)
        self.menubar.add_cascade(label="Tools", menu=self.m_tools)
        self._rebuild_tools_menu()

        m_help = tk.Menu(self.menubar, tearoff=0)
        m_help.add_command(label="Keyboard Shortcuts", command=self._ui_shortcuts)
        m_help.add_command(label="About PythonView", command=self._ui_about)
        self.menubar.add_cascade(label="Help", menu=m_help)

        self.tk.config(menu=self.menubar)
        self._rebuild_map_menu()

    def _rebuild_tools_menu(self):
        """The Tools menu, with the EDITOR half built from the registry: one
        toggle per type that has an editor (~ FalconView's drawing-tools
        menu), and the active editor's own palette under it. Rebuilt whenever
        the mode changes, because `tools()` reports live state — whether add
        is armed, whether there is anything to undo."""
        tk = self._tkmod
        m = self.m_tools
        m.delete(0, "end")
        mode = self.editors.current_mode
        for desc in self.registry.with_editors():
            if not desc.display_name:
                continue
            m.add_checkbutton(
                label=f"Edit {desc.display_name}",
                variable=tk.BooleanVar(value=(desc.id == mode)),
                command=lambda d=desc: self._ui_flow(
                    lambda: self.editors.toggle_editor(d.id)))
        for tool in self._editor_tools:
            if tool.is_separator:
                m.add_separator()
            else:
                m.add_command(label=f"    {tool.label}",
                              state="normal" if tool.enabled else "disabled",
                              command=lambda t=tool: (t.invoke(),
                                                      self._rebuild_tools_menu(),
                                                      self.refresh()))
        m.add_separator()
        m.add_command(label="Options...", command=self._ui_options)
        m.add_command(label="Build Tile Pack (fvpack)...",
                      command=self._ui_fvpack_help)

    def _series_menu_key(self, s):
        return f"{s.format}/{s.series_key}"

    def _rebuild_map_menu(self):
        """Repopulate the Map menu from the catalog: one section per family,
        radio items per series, plus disabled placeholders for planned
        formats (WMS) so the roadmap is visible in the UI."""
        m = self.m_map
        m.delete(0, "end")
        counts = frame_counts_by_series(self.db_path)
        by_format = {}
        for s in self.series_by_id.values():
            by_format.setdefault(s.format, []).append(s)
        for fam, fmts in FAMILIES:
            rows = []
            for fmt in fmts:
                rows += sorted(by_format.get(fmt, []),
                               key=lambda s: (-s.scale_denom, s.series_key))
            if not rows:
                continue
            m.add_command(label=f"--- {fam} ---", state="disabled")
            for s in rows:
                if s.scale_denom > 0:
                    detail = f"1:{s.scale_denom:,.0f}"
                else:
                    detail = {"vpf": "DNC library",
                              "enc": "ENC usage band",
                              "osm": "OSM tile pyramid",
                              "dted-shaded": "shaded relief"}.get(s.format, "")
                n = counts.get(s.id)
                label = f"{s.series_key}   {detail}" + (f"   ({n} frames)" if n else "")
                m.add_radiobutton(label=label, variable=self.series_var,
                                  value=self._series_menu_key(s),
                                  command=lambda s=s: self._ui_set_series(s))
        m.add_command(label="--- Planned ---", state="disabled")
        m.add_command(label="WMS - planned", state="disabled")
        if self.series is not None:
            self.series_var.set(self._series_menu_key(self.series))

    # --- keys / mouse ------------------------------------------------------

    def _bind_keys(self):
        # ONE binding, so there is ONE precedence order and it is readable
        # here rather than arbitrated by Tk behind our back.
        #
        # This used to be a dozen specific bindings (<Left>, <Prior>, "g", …).
        # Within a bind tag Tk fires only the BEST-MATCHING pattern, so a
        # generic <Key> handler was unreachable for every key that had its own
        # binding — which meant an overlay hooked to <Key> could never see the
        # arrows, Page Up/Down or Escape, no matter how correct its event was
        # (found 2026-08-04 hooking a route overlay).
        self.tk.bind("<Key>", self._on_key)

    # (vk, char) -> what the app itself does with it. Named keys match on the
    # virtual-key code; punctuation matches on the character it produced,
    # since VK_OEM_* is keyboard-layout-specific and deliberately unmapped.
    def _app_key_action(self, ev):
        k = pyfvw.overlay.key
        by_vk = {
            k.LEFT: lambda: self._ui_pan(-1, 0),
            k.RIGHT: lambda: self._ui_pan(1, 0),
            k.UP: lambda: self._ui_pan(0, -1),
            k.DOWN: lambda: self._ui_pan(0, 1),
            k.PAGE_UP: lambda: self._ui_step_scale(-1),
            k.PAGE_DOWN: lambda: self._ui_step_scale(+1),
            k.ESCAPE: self._ui_quit,
        }
        if ev.key in by_vk:
            return by_vk[ev.key]
        # The document accelerators. Held before the plain characters so that
        # ctrl-S is Save and not whatever "s" would otherwise mean.
        if ev.ctrl:
            by_ctrl = {
                ord("O"): lambda: self._ui_flow(
                    lambda: self.session.open_file_overlays("")),
                ord("S"): self._ui_save,
                ord("N"): lambda: self._ui_flow(
                    lambda: self.session.new_file_overlay(route_mod.ROUTE_TYPE_ID)),
                ord("W"): self._ui_close,
            }
            if ev.key in by_ctrl:
                return by_ctrl[ev.key]
        by_char = {
            "-": lambda: self._ui_zoom(ZOOM_STEP),
            "=": lambda: self._ui_zoom(1.0 / ZOOM_STEP),
            "+": lambda: self._ui_zoom(1.0 / ZOOM_STEP),
            "0": lambda: self._ui_zoom(None),
            "[": lambda: self._ui_feature(1.0 / FEATURE_STEP),
            "]": lambda: self._ui_feature(FEATURE_STEP),
            "g": lambda: self._ui_toggle(self.var_grid),
            "c": lambda: self._ui_toggle(self.var_cov),
            "l": lambda: self._ui_toggle(self.var_labels),
            "q": self._ui_quit,
        }
        ch = chr(ev.text) if ev.text else ""
        return by_char.get(ch)

    def _on_key(self, e):
        # Pressing Shift itself is not a key press anyone wants routed.
        if tk_keys.is_modifier(e):
            return None
        ev = tk_keys.key_event(e)
        # OVERLAYS GET FIRST REFUSAL. An editing overlay has to be able to
        # take Delete, or the arrows while it is dragging a leg, before the
        # map pans out from under it. Returning True stops the routing and
        # this handler.
        if self.mgr.route_key_down(ev):
            self.refresh()
            return "break"
        action = self._app_key_action(ev)
        if action is None:
            return None
        action()
        return "break"

    def _on_configure(self, e):
        if e.width < 60 or e.height < 60:
            return
        if (e.width, e.height) == (self.W, self.H):
            return
        if self._resize_job:
            self.tk.after_cancel(self._resize_job)
        self._resize_job = self.tk.after(
            120, lambda: self._do_resize(e.width, e.height))

    def _do_resize(self, w, h):
        self._resize_job = None
        self.set_surface_size(w, h)
        self.refresh()

    @staticmethod
    def _mouse(e, button=0):
        """A tk event as the SPI's MouseEvent. The modifier bits are tk's
        `state` mask (bit 0 shift, bit 2 control on every platform tk
        supports), and they are carried because an overlay gesture is entitled
        to mean something different with a modifier held — the shell has no
        business deciding that for it."""
        state = getattr(e, "state", 0)
        if not isinstance(state, int):      # tk hands a string for some events
            state = 0
        return pyfvw.overlay.MouseEvent(e.x, e.y, button,
                                        bool(state & 0x0001),
                                        bool(state & 0x0004))

    def _on_press(self, e):
        """The press is offered to the overlays FIRST, and a press that is
        taken starts an OVERLAY gesture rather than a map pan.

        This is the shell's half of the drag contract. Before it, a click was
        synthesized at RELEASE time and only for a press that had not moved,
        so no overlay could express press-drag-release and mouse capture had
        nothing to capture. An overlay that declines still gets the map pan it
        used to get, because declining is what "this click is not mine" means.
        """
        self._drag = None
        self._drag_moved = False
        self._overlay_gesture = False
        if self.mgr.route_mouse_down(self._mouse(e)):
            self._overlay_gesture = True
            self.refresh()
            return
        self._drag = (e.x, e.y, self.center.lat, self.center.lon)

    def _on_drag(self, e):
        if self._overlay_gesture:
            # Capture (phase 0 of the manager's route) sends this to the
            # overlay that took the press and to nobody else.
            if self.mgr.route_mouse_move(self._mouse(e)):
                now = time.time()
                if now - self._last_drag_render > 0.05:
                    self._last_drag_render = now
                    self.refresh()
            return
        if self._drag is None:
            return
        x0, y0, lat0, lon0 = self._drag
        dx, dy = e.x - x0, e.y - y0
        if abs(dx) + abs(dy) < 4 and not self._drag_moved:
            return
        self._drag_moved = True
        proj = self.proj
        if proj is None:
            return
        try:
            dpp_lat, dpp_lon = proj.deg_per_pixel_lat, proj.deg_per_pixel_lon
        except pyfvw.FvError:
            return
        self.center = pyfvw.geo.GeoPoint(lat0 + dy * dpp_lat, lon0 - dx * dpp_lon)
        self.center.normalize()
        now = time.time()
        if now - self._last_drag_render > 0.10:  # throttle live drag redraws
            self._last_drag_render = now
            self.refresh()

    def _on_release(self, e):
        if self._overlay_gesture:
            # The overlay owns the whole gesture, including its end — this is
            # where a drag commits and where capture is released.
            self._overlay_gesture = False
            self.mgr.route_mouse_up(self._mouse(e))
            self.refresh()
            return
        drag, moved = self._drag, self._drag_moved
        self._drag = None
        if drag is None:
            return
        if moved:
            self.refresh()
            return
        # A plain click nobody took at press time. The overlays have already
        # had their refusal, so this is picking only — where before it routed
        # the press here, at release, which is what made a drag impossible.
        proj = self.proj
        if proj is not None:
            hit = self.pick.resolve_click(proj, e.x, e.y)
            if hit is not None:
                # A point overlay's hit carries the row id, so selecting is
                # the overlay's own business and this shell stays generic.
                if isinstance(hit.overlay, pyfvw.overlay.PointOverlay):
                    hit.overlay.selected = hit.feature
                self._hover_hint = hit.hint.status or hit.hint.tool_tip
                self.refresh()
                return
        if self.mode == "vector":
            self.info_lines = self.identify(e.x, e.y)
            self._show_info(True)
            self._update_status()
        else:  
            self._recenter_at(e.x, e.y)

    def _on_shift_click(self, e):
        # A more specific tk binding than <ButtonPress-1>, so _on_press never
        # ran and no gesture was started -- but the release still fires.
        self._drag = None
        self._overlay_gesture = False
        self._recenter_at(e.x, e.y)
        return "break"

    def _recenter_at(self, x, y):
        proj = self.proj
        if proj is None:
            return
        try:
            self.center = proj.surface_to_geo(x, y)
        except pyfvw.FvError:
            return
        if self.cross is not None:
            self.cross.click = None
        self.refresh()

    def _on_wheel(self, e):
        if e.delta == 0:
            return
        step = 2.0 ** (1.0 / 8.0)
        self._ui_zoom(1.0 / step if e.delta > 0 else step)

    def _on_right_click(self, e):
        """The aggregated context menu: every overlay under the point appends
        its own section, top-down. False means nobody contributed, and an
        empty menu flashing open is worse than no menu — so nothing happens."""
        proj = self.proj
        if proj is not None:
            self.pick.show_context_menu(proj, e.x, e.y)
        return "break"

    def _on_motion(self, e):
        if self.mgr.route_mouse_move(self._mouse(e)):
            self.refresh()
            return
        if self._drag is not None:
            return
        proj = self.proj
        if proj is None:
            return
        # What a click here would do. Notified only on a CHANGE, so this costs
        # a hit test per move and nothing else.
        self.pick.update_hover(proj, e.x, e.y)
        try:
            p = proj.surface_to_geo(e.x, e.y)
        except pyfvw.FvError:
            self._set_readout("")
            return
        txt = f" {p.lat:+.4f} {p.lon:+.4f}"
        if self.engine is not None:
            try:
                elev = self.engine.get_elevation(p.lat, p.lon)
                txt += f"  {elev:.0f} m"
            except pyfvw.FvError:
                pass
        self._set_readout(txt)

    def _set_readout(self, txt):
        self.mouse_readout = txt
        self._update_status()

    # --- UI actions --------------------------------------------------------

    def _ui_set_series(self, s):
        try:
            self.set_series(s)
        except Exception as exc:
            from tkinter import messagebox
            messagebox.showerror(
                "PythonView", f"Could not open {s.format}/{s.series_key}:\n{exc}")
            if self.series is not None:
                self.series_var.set(self._series_menu_key(self.series))
            return
        self._show_info(self.mode == "vector" and bool(self.info_lines))
        self.refresh()

    def _ui_zoom(self, factor):
        self.zoom(factor)
        self.refresh()

    def _ui_feature(self, factor):
        if self.mode != "vector":
            return
        self.feature_scale = max(0.2, min(8.0, self.feature_scale * factor))
        self.refresh()

    def _ui_step_scale(self, delta):
        if self.step_scale(delta) is not None:
            self.series_var.set(self._series_menu_key(self.series))
            self.refresh()

    def _ui_pan(self, dx, dy):
        self.pan_pixels(dx * PAN_PX, dy * PAN_PX)
        self.refresh()

    # --- the flows ---------------------------------------------------------

    def _ui_flow(self, run):
        """Run one session/editor flow and put the outcome on screen.

        The three FlowResults are NOT interchangeable and this is where that
        pays: DONE redraws, CANCELED is the user's own answer and says nothing
        (they know — they just cancelled), and FAILED has already been
        reported through report_error, so all that is left is to keep the
        stack view honest."""
        result = run()
        F = pyfvw.app.FlowResult
        if result == F.DONE:
            self.route = self.mgr.first_of_type(route_mod.ROUTE_TYPE_ID)
            self._sync_overlay_menus()
            self.refresh()
        for note in self.session.warnings:
            print(f"pythonview: {note}", file=sys.stderr)
        self.session.clear_warnings()
        self._update_status()
        return result

    def _sync_overlay_menus(self):
        """Keep the menus agreeing with the stack after a flow changed it."""
        if self.tk is None:
            return
        if hasattr(self, "var_grid"):
            self.var_grid.set(self.grid is not None)
            self.var_cross.set(self.cross is not None)
            self.var_cov.set(self.coverage is not None)
        self._rebuild_tools_menu()

    def _current_document(self):
        """The overlay File > Save/Close act on: the CURRENT one when it is a
        document, else the topmost document there is. FalconView's own rule,
        and it is why `current` exists at all."""
        cur = self.mgr.current
        if cur is not None and cur.is_file_overlay:
            return cur
        for o in reversed(self.mgr.overlays):
            if o.is_file_overlay:
                return o
        return None

    def _ui_sample_points(self):
        """Write the C++ point overlay's arbitrary starter document if it is
        not there yet, then OPEN it through the normal flow — the same path a
        file the user chose would take, dedup and all. It is a demo of the
        first C++ file overlay, and the fastest way to have something to pick.
        """
        spec = os.path.join(os.path.dirname(self.db_path) or ".",
                            "sample.fvpoints")
        # The icons ride INTO the file, so a directory that is not there costs
        # the points their artwork and nothing else — they draw as shapes,
        # which is the document this wrote before schema 2.
        icons = self.point_symbol_dir \
            if os.path.isdir(self.point_symbol_dir) else ""
        try:
            if not os.path.exists(spec):
                pyfvw.overlay.PointOverlay.write_sample_file(spec, icons)
        except pyfvw.FvError as e:
            self.report_error(e.code, str(e.message))
            return
        if self._ui_flow(lambda: self.session.open_file("", spec)) != \
                pyfvw.app.FlowResult.DONE:
            return
        points = self.mgr.first_of_type(pyfvw.app.POINTS_TYPE_ID)
        if points is not None and points.points:
            lats = [p.position.lat for p in points.points]
            lons = [p.position.lon for p in points.points]
            self.center = pyfvw.geo.GeoPoint((min(lats) + max(lats)) / 2.0,
                                             (min(lons) + max(lons)) / 2.0)
            self.refresh()

    def _ui_save(self):
        doc = self._current_document()
        if doc is not None:
            self._ui_flow(lambda: self.session.save(doc))

    def _ui_save_as(self):
        doc = self._current_document()
        if doc is not None:
            self._ui_flow(lambda: self.session.save_as(doc))

    def _ui_close(self):
        doc = self._current_document()
        if doc is not None:
            self._ui_flow(lambda: self.session.close(doc))

    def _ui_quit(self):
        # Exit() prompts for every dirty document and a cancel anywhere aborts
        # the whole thing — including the destroy below, which is the point.
        if self.session.exit() == pyfvw.app.FlowResult.CANCELED:
            return
        self.tk.destroy()

    def _ui_toggle(self, var):
        var.set(not var.get())
        self._ui_apply_overlays()

    def _ui_apply_overlays(self):
        self._set_grid(self.var_grid.get())
        self._set_static(CROSSHAIR_TYPE_ID, self.var_cross.get())
        self._set_static(COVERAGE_TYPE_ID, self.var_cov.get())
        if self.coverage is not None:
            self.coverage.enabled_formats = {
                fmt for fmt, v in self.var_cov_fmt.items() if v.get()}
        self.labels = self.var_labels.get()
        if hasattr(self, "var_label_ground"):
            self.label_ref_scale = self.vscale if self.var_label_ground.get() \
                else 0.0
        if hasattr(self, "var_meta"):
            self.show_meta = self.var_meta.get()
            if self.s52 is not None:
                self.s52.set_show_meta_objects(self.show_meta)
        self.refresh()

    def _ui_goto(self):
        from tkinter import simpledialog, messagebox
        s = simpledialog.askstring(
            "Go To Location",
            'Lat/lon ("33.74 -84.39"), DMS ("33 44 55.7 N 84 23 17.5 W"),\n'
            "or MGRS:", parent=self.tk)
        if not s:
            return
        try:
            self.center = pyfvw.geo.parse_location(s)
        except pyfvw.FvError as e:
            messagebox.showerror("PythonView", f"Could not parse location:\n{e}")
            return
        self.refresh()

    def _ui_recenter(self):
        if self.series is None:
            return
        if self.mode == "vector":
            b = self.vsource.bounds
            self.center = pyfvw.geo.GeoPoint(
                (b.ll.lat + b.ur.lat) / 2.0, (b.ll.lon + b.ur.lon) / 2.0)
            self.vscale = self._fit_scale_rect(b)
        else:
            self.center = self.series_bounds_center(self.series)
        self.refresh()

    def _ui_screenshot(self):
        from tkinter import filedialog
        path = filedialog.asksaveasfilename(
            title="Save Screenshot", defaultextension=".png",
            filetypes=[("PNG image", "*.png")])
        if path and self.last_array is not None:
            _save_png(path, self.last_array)

    # --- catalog UI --------------------------------------------------------

    def _first_run_prompt(self):
        from tkinter import messagebox
        if os.path.isdir(TESTDATA):
            if messagebox.askyesno(
                    "PythonView",
                    f"No map catalog found.\n\nScan {TESTDATA} now?\n"
                    "(You can add more data later via File > Add Map Data.)"):
                self._scan_with_progress(self.db_path, TESTDATA)
        else:
            messagebox.showinfo(
                "PythonView",
                "No map catalog found. Use File > New Catalog from Scan to "
                "point PythonView at a map data directory.")

    def _scan_with_progress(self, db_path, root):
        from tkinter import messagebox
        lines = []
        try:
            results = self.new_catalog_from_scan(db_path, root, log=lines.append)
        except Exception as exc:
            messagebox.showerror("PythonView", f"Scan failed:\n{exc}")
            return
        total = sum(n for _f, _p, n in results)
        messagebox.showinfo(
            "PythonView", f"Cataloged {total} frames:\n\n" + "\n".join(lines))
        self._rebuild_map_menu()
        self.refresh()

    def _ui_open_catalog(self):
        from tkinter import filedialog, messagebox
        path = filedialog.askopenfilename(
            title="Open Catalog", filetypes=[("SQLite catalog", "*.sqlite"),
                                             ("All files", "*")])
        if not path:
            return
        try:
            self.open_catalog(path)
        except Exception as exc:
            messagebox.showerror("PythonView", f"Could not open catalog:\n{exc}")
            return
        self._rebuild_map_menu()
        self.refresh()

    def _ui_new_catalog(self):
        from tkinter import filedialog
        root = filedialog.askdirectory(title="Choose a map data directory to scan",
                                       initialdir=TESTDATA)
        if root:
            self._scan_with_progress(self.db_path, root)

    def _ui_add_data(self):
        from tkinter import filedialog, messagebox
        if self.catalog is None:
            self._ui_new_catalog()
            return
        root = filedialog.askdirectory(title="Add map data directory",
                                       initialdir=TESTDATA)
        if not root:
            return
        lines = []
        results = scan_into_catalog(self.catalog, root, log=lines.append)
        if not results:
            messagebox.showinfo(
                "PythonView",
                f"No known map data found under {root}.\n\n"
                "Looked for: CADRG (rpf), TIROS, DTED, GeoTIFF, GeoPackage, "
                "and VPF/DNC databases.")
            return
        total = sum(n for _f, _p, n in results)
        self._on_catalog_changed()
        self._rebuild_map_menu()
        messagebox.showinfo(
            "PythonView", f"Added {total} frames:\n\n" + "\n".join(lines))
        self.refresh()

    def _ui_manage_sources(self):
        tk, ttk = self._tkmod, self._ttk
        from tkinter import messagebox
        win = tk.Toplevel(self.tk)
        win.title("Data Sources")
        win.geometry("680x340")
        tree = ttk.Treeview(win, columns=("format", "path", "frames"),
                            show="headings")
        for col, w in (("format", 100), ("path", 460), ("frames", 80)):
            tree.heading(col, text=col.capitalize())
            tree.column(col, width=w)
        tree.pack(fill="both", expand=True)

        def reload_tree():
            tree.delete(*tree.get_children())
            for sid, fmt, path, n in list_data_sources(self.db_path):
                tree.insert("", "end", iid=str(sid), values=(fmt, path, n))

        def remove_selected():
            sel = tree.selection()
            if not sel:
                return
            items = [tree.item(i, "values") for i in sel]
            names = "\n".join(f"  {f}: {p}" for f, p, _n in items)
            if not messagebox.askyesno(
                    "Remove Data Sources",
                    "Remove from the catalog (files on disk are not "
                    f"touched):\n\n{names}", parent=win):
                return
            for iid in sel:
                self.catalog.remove_data_source(int(iid))
            self._on_catalog_changed()
            self._rebuild_map_menu()
            reload_tree()
            self.refresh()

        bar = tk.Frame(win)
        bar.pack(fill="x")
        tk.Button(bar, text="Add Map Data...",
                  command=lambda: (self._ui_add_data(), reload_tree())).pack(
            side="left", padx=4, pady=4)
        tk.Button(bar, text="Remove Selected", command=remove_selected).pack(
            side="left", padx=4)
        tk.Button(bar, text="Close", command=win.destroy).pack(side="right", padx=4)
        reload_tree()

    # --- options / help ----------------------------------------------------

    # The profile menu's entry for "no profile named": the router's own
    # default, which is the built-in weights when no rule file is set and the
    # file's `default` profile when one is.
    _NO_PROFILE = "(default)"

    def _ui_options(self):
        tk = self._tkmod
        from tkinter import filedialog
        win = tk.Toplevel(self.tk)
        win.title("Options")
        row = 0

        tk.Label(win, text="Display pixel pitch (mm/px):").grid(
            row=row, column=0, sticky="w", padx=8, pady=4)
        v_mm = tk.StringVar(value=f"{self.mm_per_pixel:.4f}")
        tk.Entry(win, textvariable=v_mm, width=10).grid(row=row, column=1)
        row += 1

        tk.Label(win, text="GeoSym assets directory:").grid(
            row=row, column=0, sticky="w", padx=8, pady=4)
        v_gs = tk.StringVar(value=self.geosym_dir)
        tk.Entry(win, textvariable=v_gs, width=36).grid(row=row, column=1)
        tk.Button(win, text="...", command=lambda: v_gs.set(
            filedialog.askdirectory(initialdir=v_gs.get()) or v_gs.get())).grid(
            row=row, column=2)
        row += 1

        tk.Label(win, text="OSM style sheet (MapLibre JSON):").grid(
            row=row, column=0, sticky="w", padx=8, pady=4)
        v_os = tk.StringVar(value=self.osm_style_path)
        tk.Entry(win, textvariable=v_os, width=36).grid(row=row, column=1)
        tk.Button(win, text="...", command=lambda: v_os.set(
            filedialog.askopenfilename(
                initialdir=os.path.dirname(v_os.get()),
                filetypes=[("MapLibre style", "*.json")]) or v_os.get())).grid(
            row=row, column=2)
        row += 1

        # --- routing cost rules (O5c) ---------------------------------------
        # The path and the profile are one setting in two halves: a rule file
        # nobody names a profile from changes nothing, and a profile name is
        # meaningless without the file that defines it. So they sit together,
        # and the menu is rebuilt from whatever path is in the box.
        tk.Label(win, text="Routing cost rules (JSON):").grid(
            row=row, column=0, sticky="w", padx=8, pady=4)
        v_rr = tk.StringVar(value=self.route_rules_path)
        tk.Entry(win, textvariable=v_rr, width=36).grid(row=row, column=1)

        def pick_rules():
            start = os.path.dirname(v_rr.get()) or os.path.join(
                REPO, "port", "Routing", "rules")
            p = filedialog.askopenfilename(
                initialdir=start, title="Routing cost rules",
                filetypes=[("Routing rules", "*.json"), ("All files", "*")])
            if p:
                v_rr.set(p)
                refresh_profiles()

        tk.Button(win, text="...", command=pick_rules).grid(row=row, column=2)
        row += 1

        tk.Label(win, text="Route profile:").grid(
            row=row, column=0, sticky="w", padx=8, pady=4)
        v_rp = tk.StringVar(value=self.route_profile or self._NO_PROFILE)
        profile_menu = tk.OptionMenu(win, v_rp, self._NO_PROFILE)
        profile_menu.config(width=18)
        profile_menu.grid(row=row, column=1, sticky="w", padx=8)
        v_err = tk.StringVar(value="")

        def refresh_profiles():
            """Re-read the rule file and rebuild the menu from it. This is the
            'recognise my edit' button: routing itself rereads the file on
            every route (the core polls its mtime), so the only thing that can
            go stale is this menu and the error line under it — a profile
            added while the app is running appears here, and a syntax error
            says so while the last good weights keep routing."""
            path = v_rr.get()
            try:
                names = list(pyfvw.routing.rule_profiles(path))
                v_err.set(pyfvw.routing.rules_error(path))
            except pyfvw.FvError as exc:
                names = []
                v_err.set(exc.message)
            menu = profile_menu["menu"]
            menu.delete(0, "end")
            for name in [self._NO_PROFILE] + names:
                menu.add_command(label=name,
                                 command=lambda n=name: v_rp.set(n))
            # A profile that no longer exists must not stay selected and
            # silently fail at the next "r".
            if v_rp.get() != self._NO_PROFILE and v_rp.get() not in names:
                v_rp.set(self._NO_PROFILE)

        tk.Button(win, text="Reload Rules", command=refresh_profiles).grid(
            row=row, column=2, padx=4)
        row += 1
        tk.Label(win, textvariable=v_err, fg="#b00000", wraplength=380,
                 justify="left").grid(row=row, column=1, columnspan=2,
                                      sticky="w", padx=8)
        refresh_profiles()
        row += 1

        tk.Label(win, text="Vector brightness / contrast:").grid(
            row=row, column=0, sticky="w", padx=8, pady=4)
        v_b = tk.IntVar(value=self.brightness)
        v_c = tk.IntVar(value=self.contrast)
        tk.Scale(win, from_=-100, to=100, orient="horizontal",
                 variable=v_b, length=140).grid(row=row, column=1)
        tk.Scale(win, from_=-100, to=100, orient="horizontal",
                 variable=v_c, length=140).grid(row=row, column=2)
        row += 1

        def apply_and_close():
            try:
                self.mm_per_pixel = max(0.01, min(64.0, float(v_mm.get())))
            except ValueError:
                pass
            # Either asset change drops the cached sources so the next open
            # rebuilds against the new one. A style sheet that will not load
            # must not take the app down with it: say so and keep the old one.
            reopen = None
            if v_gs.get() != self.geosym_dir:
                self.geosym_dir = v_gs.get()
                self.style = None       # reopen lazily with the new dir
                reopen = "GeoSym"
            if v_os.get() != self.osm_style_path:
                self.osm_style_path = v_os.get()
                self.osm = None
                self._osm_ref_lat = None
                reopen = "OSM style"
            if reopen:
                self.vsources.clear()
                self.vrenderer = self.vsource = self.vstyle = None
                if self.mode == "vector":
                    try:
                        self._open_vector(self.series)
                    except Exception as exc:
                        from tkinter import messagebox
                        messagebox.showerror("PythonView",
                                             f"{reopen} reopen failed:\n{exc}")
            self.brightness, self.contrast = v_b.get(), v_c.get()

            # Routing rules: strings the overlay reads at route time, so there
            # is nothing to reopen — but a route already ON SCREEN was priced
            # with the old ones, and leaving it there would be the dialog
            # quietly lying about what it just changed. Re-run it instead.
            profile = "" if v_rp.get() == self._NO_PROFILE else v_rp.get()
            rerun = (v_rr.get() != self.route_rules_path or
                     profile != self.route_profile)
            self.route_rules_path = self.route.rules_path = v_rr.get()
            self.route_profile = self.route.profile = profile
            if rerun and self.route.road_legs is not None:
                self.route.follow_roads()

            win.destroy()
            self.refresh()

        tk.Button(win, text="Apply", command=apply_and_close).grid(
            row=row, column=1, pady=8)
        tk.Button(win, text="Cancel", command=win.destroy).grid(
            row=row, column=2, pady=8)

    def _ui_fvpack_help(self):
        from tkinter import messagebox
        messagebox.showinfo(
            "Build Tile Pack",
            "Offline GeoPackage tile packs are built with the fvpack CLI "
            "(a UI for this is planned):\n\n"
            "  build/port/fvkit/tools/fvpack --db <catalog.sqlite> \\\n"
            "      --series LFC --center \"33.75 -84.39\" --levels 4 \\\n"
            "      --out pack.gpkg\n\n"
            "The result is a standard GeoPackage: it opens in QGIS, and "
            "scanning it (File > Add Map Data) makes each pyramid level a "
            "selectable series here.")

    def _ui_shortcuts(self):
        from tkinter import messagebox
        messagebox.showinfo("Keyboard Shortcuts", (
            "arrows / drag     pan\n"
            "-  =  wheel       zoom out / in\n"
            "0                 reset zoom\n"
            "PageUp/PageDown   larger / smaller scale at center\n"
            "[  ]              feature size (vector)\n"
            "g                 lat/lon grid\n"
            "c                 coverage overlay\n"
            "l                 feature labels (vector)\n"
            "click             pick / identify (vector) / re-center (raster)\n"
            "right-click       what is under the cursor (every overlay)\n"
            "shift+click       re-center\n"
            "q / Esc           quit (offers to save a changed overlay)\n"
            "\n"
            "Overlay documents:\n"
            "ctrl-N            new route\n"
            "ctrl-O            open an overlay file\n"
            "ctrl-S            save the current overlay\n"
            "ctrl-W            close the current overlay\n"
            "\n"
            "Route overlay (only while its editor is active - Tools menu):\n"
            "click             select a waypoint\n"
            "drag              move a waypoint (Esc mid-drag cancels)\n"
            "a                 arm add-point; next click inserts after it\n"
            "d / Delete        delete the selected waypoint\n"
            "g                 leg geometry: great circle / rhumb / straight\n"
            "u / ctrl-Z        undo the last waypoint edit\n"
            "r                 follow the roads (needs [routing] graph)\n"
            "b                 follow the roads by bicycle\n"
            "                  (r uses the profile set in Options; both\n"
            "                   reread the rule file, so an edited weight\n"
            "                   applies to the very next route)\n"
            "Esc               cancel add mode / straight legs / deselect"))

    def _ui_about(self):
        from tkinter import messagebox
        messagebox.showinfo("About PythonView", (
            "PythonView - the pyfvw desktop viewer\n\n"
            "A Python UI over the cross-platform FalconView port: CADRG, "
            "GeoTIFF, TIROS, GeoPackage and DTED shaded relief through "
            "MapEngine; DNC vector charts through GeoSym, S-57 ENC through "
            "the S-52 presentation library and OSM vector tiles through a "
            "MapLibre GL style - three products over one VectorRenderer; "
            "coverage and identify straight from the L2 catalog."
            f"\n\nCatalog: {self.db_path}"))

    # --- refresh / status ---------------------------------------------------

    def _show_info(self, on):
        if on and not self._info_visible:
            self.info.pack(side="bottom", fill="x")
            self._info_visible = True
        elif not on and self._info_visible:
            self.info.pack_forget()
            self._info_visible = False

    def refresh(self):
        if self.tk is None:
            return
        arr = self.render_array()
        ppm = b"P6 %d %d 255\n" % (self.W, self.H) + arr[:, :, :3].tobytes()
        self.photo = self._tkmod.PhotoImage(data=ppm)  # keep a reference!
        self.label.configure(image=self.photo)
        self._update_status()

    def _update_status(self):
        if self.tk is None:
            return
        if self.series is None:
            self.status.configure(text=" no catalog - File > Add Map Data...")
            return
        s = self.series
        if self.mode == "vector":
            lbl = "on" if (self.labels and self.font) else "off"
            product = {"enc": "ENC", "osm": "OSM"}.get(s.format, "DNC")
            # Base-edition-only cells are a navigational caveat, not a parse
            # problem: the reader does not apply the .001+ updates shipped
            # beside these cells, so say so on the chart rather than letting
            # a stale chart look current.
            if s.format == "enc" and self.vsource.staleness_warning:
                product += " (base ed.)"
            # A pyramid stops at z14 while the display does not, and the
            # difference is worth showing: past it the map is z14 geometry
            # under z15+ rules, so nothing new appears however far you zoom.
            if s.format == "osm":
                product += f" z{self.vsource.last_query_zoom}"
                if self.vsource.last_query_overzoom >= 1.0:
                    product += f"+{self.vsource.last_query_overzoom:.1f}"
            txt = (f" {self.center.lat:+.5f} {self.center.lon:+.5f}   "
                   f"{product}/{s.series_key}  1:{self.vscale:,.0f}   "
                   f"features x{self.feature_scale:.2f}  labels:{lbl}   "
                   f"{self._frames} feat/{self._ms:.0f}ms  {self.mouse_readout}")
        else:
            try:
                eff = self.engine.proj.scale
            except pyfvw.FvError:
                eff = 0
            zoom = NATIVE_MM_PER_PIXEL / self.mm_per_pixel
            txt = (f" {self.center.lat:+.5f} {self.center.lon:+.5f}   "
                   f"{s.format}/{s.series_key}  1:{eff:,.0f}   "
                   f"{zoom:.2f}x @ {self.mm_per_pixel:.3f}mm/px   "
                   f"{self._frames}files/{self._ms:.0f}ms  {self.mouse_readout}")
        if self.render_error:
            txt += f"   [render error: {self.render_error}]"
        # The mode and the hover are what the APP LAYER has to say about the
        # frame, and they belong at the end where the eye is not hunting for
        # coordinates.
        mode = self.editors.current_mode if self.editors else ""
        if mode:
            desc = self.registry.find(mode)
            txt += f"   [editing {desc.display_name if desc else mode}]"
        doc = self._current_document()
        if doc is not None and doc.dirty:
            txt += f"   [{self._overlay_display_name(doc)} *]"
        if self._hover_hint:
            txt += f"   {self._hover_hint}"
        self.status.configure(text=txt)
        if self._info_visible:
            self.info.configure(state="normal")
            self.info.delete("1.0", "end")
            self.info.insert("1.0", "\n".join(self.info_lines[:12]))
            self.info.configure(state="disabled")


# ----------------------------------------------------------------------------
# Self-test: scripted walk through the UI (switch families, coverage overlay,
# window resize, snapshot each state). Exits non-zero on any failure.
# ----------------------------------------------------------------------------

def run_selftest(app, outdir):
    os.makedirs(outdir, exist_ok=True)
    failures = []

    def snap(name):
        if app.last_array is not None:
            _save_png(os.path.join(outdir, f"{name}.png"), app.last_array)

    # Steps are CHAINED: each schedules the next 400 ms after it completes,
    # so the event loop always gets idle time between steps. (Absolute
    # schedules go stale behind slow renders, and with every timer overdue
    # the loop never services the window-server <Configure> round-trip.)
    steps = []

    def step(fn, name, settle_ms=400):
        def wrapped():
            try:
                fn()
                app.refresh()
                snap(name)
                print(f"  selftest: {name:24s} ok  ({app.W}x{app.H}, "
                      f"{app._frames} frames, {app._ms:.0f} ms)", flush=True)
            except Exception:
                failures.append(name)
                traceback.print_exc()
            if steps:
                app.tk.after(settle_ms, steps.pop(0))
        return wrapped

    # One representative series per FORMAT present in the catalog — not per
    # family. Vector Charts holds two products whose only shared code is the
    # renderer (VPF+GeoSym vs S-57+S-52), so a per-family walk would have
    # exercised whichever came first and left the other untested; that is
    # exactly how the ENC path shipped broken in this file's first draft.
    picks = []
    by_format = {}
    for s in app.series_by_id.values():
        by_format.setdefault(s.format, []).append(s)
    for _fam, fmts in FAMILIES:
        for fmt in fmts:
            if by_format.get(fmt):
                picks.append(sorted(by_format[fmt],
                                    key=lambda s: s.series_key)[0])

    for s in picks:
        steps.append(step(lambda s=s: app.set_series(s, recenter=True),
                          f"family_{s.format}"))

    def coverage_on():
        app.var_cov.set(True)
        app._ui_apply_overlays()
    steps.append(step(coverage_on, "coverage_on"))
    # Resize needs a window-manager round-trip plus the 120 ms debounce.
    steps.append(step(lambda: app.tk.geometry("1200x820"), "resize_req",
                      settle_ms=900))
    steps.append(step(lambda: None, "after_resize"))

    def finish():
        snap("final")
        ok = not failures and app.W > 1100  # resize must have taken effect
        app.tk.destroy()
        if not ok:
            print(f"SELFTEST FAILED: {failures or 'resize did not take effect'}",
                  flush=True)
            os._exit(1)
        print(f"SELFTEST OK - snapshots in {outdir}", flush=True)
    steps.append(finish)

    app.tk.after(300, steps.pop(0))


# ----------------------------------------------------------------------------
# main
# ----------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description="PythonView - pyfvw map viewer")
    ap.add_argument("--db", default=DEFAULT_DB, help="catalog SQLite path")
    ap.add_argument("--scan", metavar="DIR",
                    help="(re)build the catalog by scanning DIR, then open")
    ap.add_argument("--at", metavar="LOC",
                    help='initial center: "lat lon", DMS, or MGRS')
    ap.add_argument("--series", metavar="FMT/KEY",
                    help="initial series, e.g. cadrg/LFC or vpf/h1707300")
    ap.add_argument("--mm", type=float,
                    help="assumed display pitch in mm/pixel (default 0.25)")
    ap.add_argument("--shot", metavar="PNG",
                    help="render once headless, save PNG, exit")
    ap.add_argument("--selftest", action="store_true",
                    help="scripted UI walk-through; exits non-zero on failure")
    ap.add_argument("--settings", metavar="INI",
                    help="settings file (default: the FVW_SETTINGS / "
                         "./peregrine.ini / user-config search path; see "
                         "port/peregrine.ini.sample)")
    args = ap.parse_args()

    app = PythonView(db_path=args.db, settings_path=args.settings)
    for w in app.settings.warnings:
        print(f"settings: {w}", file=sys.stderr)

    # app.db_path, not args.db: the settings file may have named a catalog,
    # and an explicit --db still wins (PythonView resolves that).
    db = app.db_path
    first_run = False
    if args.scan:
        print(f"scanning {args.scan} -> {db}")
        app.new_catalog_from_scan(db, args.scan)
    elif os.path.exists(db):
        app.open_catalog(db)
    elif args.shot or args.selftest:
        raise SystemExit(f"no catalog at {db}; run once with --scan DIR")
    else:
        first_run = True

    if args.mm:
        app.mm_per_pixel = args.mm
    if args.series:
        fmt, _, key = args.series.partition("/")
        match = [s for s in app.series_by_id.values()
                 if s.format == fmt and s.series_key == key]
        if not match:
            raise SystemExit(f"--series: no {args.series} in the catalog; have: "
                             + ", ".join(sorted(
                                 f"{s.format}/{s.series_key}"
                                 for s in app.series_by_id.values())))
        app.set_series(match[0], recenter=args.at is None)
    if args.at:
        app.center = pyfvw.geo.parse_location(args.at)

    if args.shot:
        arr = app.render_array()
        _save_png(args.shot, arr)
        s = app.series
        print(f"wrote {args.shot}: {s.format}/{s.series_key}, "
              f"{app._frames} frames, {app._ms:.0f} ms")
        return

    if args.selftest:
        outdir = os.path.join(REPO, "build", "pythonview_selftest")
        app.run(before_mainloop=lambda: run_selftest(app, outdir))
        return

    app.run(first_run_scan_prompt=first_run)


if __name__ == "__main__":
    main()
