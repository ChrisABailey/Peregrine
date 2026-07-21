#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See LICENSE and NOTICE.md for the full licensing picture.

"""Interactive map viewer over pyfvw's MapEngine (GeoTIFF / CADRG / TIROS).

The compositing loop lives in C++ now (fvkit MapEngine): this script is a
thin tk shell — configure viewport, call engine.render(canvas), overlay via
OverlayManager (including a Python-implemented crosshair overlay), blit.

Usage (from the repo root, after `cmake --build build`):
  python3 port/bindings/pyfvw/demo/pan_viewer.py                # GeoTIFF DOQs
  python3 port/bindings/pyfvw/demo/pan_viewer.py cadrg          # charts, Atlanta
  python3 port/bindings/pyfvw/demo/pan_viewer.py tiros          # TopoBath
  python3 port/bindings/pyfvw/demo/pan_viewer.py cadrg --at "16SGB 47342 34212"
  python3 port/bindings/pyfvw/demo/pan_viewer.py cadrg --shot map.png  # render+save+exit
Keys: arrows pan · +/- zoom · g grid · q/Esc quit · click re-centers
(status bar shows center, scale, frames, ms, and DTED elevation at clicks)
"""

import glob
import os
import sys
import time
import tkinter as tk

import numpy as np

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))
_hits = glob.glob(os.path.join(REPO, "build", "port", "bindings", "pyfvw", "pyfvw*.so"))
if _hits and os.path.dirname(_hits[0]) not in sys.path:
    sys.path.insert(0, os.path.dirname(_hits[0]))
os.environ.setdefault("MSPCCS_DATA", os.path.join(REPO, "fvw_core", "PdfLib", "sdk", "lib"))
TESTDATA = os.environ.get("FVW_TESTDATA_DIR", os.path.join(REPO, "TestData"))

import pyfvw  # noqa: E402

W, H = 900, 650
PAN_PX = 150          # pixels per arrow key
MIN_SCALE, MAX_SCALE = 1_000, 200_000_000


class Crosshair(pyfvw.overlay.Overlay):
    """Python-implemented overlay: center crosshair + last-click marker."""

    def __init__(self):
        super().__init__("crosshair")
        self.click = None  # surface (x, y) of last click

    def on_draw(self, proj, canvas):
        cx, cy = W // 2, H // 2
        canvas.draw_lines([(cx - 12, cy), (cx + 12, cy)], color=(255, 60, 60), width=1)
        canvas.draw_lines([(cx, cy - 12), (cx, cy + 12)], color=(255, 60, 60), width=1)
        if self.click:
            x, y = self.click
            canvas.fill_polygon(
                [[(x, y - 6), (x + 6, y), (x, y + 6), (x - 6, y)]],
                fill=(255, 220, 0, 200))


class PanViewer:
    def __init__(self, root_dir, kind, center=None, scale=None):
        t0 = time.time()
        pyfvw.catalog.register_builtin_formats()
        self.catalog = pyfvw.catalog.Catalog()
        src = self.catalog.add_data_source(root_dir, kind)
        n = self.catalog.scan(src)
        if n == 0:
            raise SystemExit(f"no {kind} frames under {root_dir}")
        print(f"{n} frames cataloged in {time.time() - t0:.2f}s")

        self.engine = pyfvw.engine.MapEngine(self.catalog)
        self.engine.set_surface(W, H)
        dted = os.path.join(TESTDATA, "dted")
        if os.path.isdir(dted):
            self.engine.set_elevation_source(pyfvw.formats.DtedElevationSource(dted))

        if center is None:
            rows = self.catalog.select_by_geo_rect(pyfvw.geo.GeoRect.world())
            mid = rows[len(rows) // 2].bounds
            center = pyfvw.geo.GeoPoint((mid.ll.lat + mid.ur.lat) / 2,
                                        (mid.ll.lon + mid.ur.lon) / 2)
        self.center = center
        if scale is None:
            denoms = [s.scale_denom for s in self.catalog.series() if s.scale_denom > 0]
            scale = min(denoms) if denoms else 500_000
        self.scale = float(scale)

        self.canvas = pyfvw.canvas.CpuCanvas(W, H)
        self.mgr = pyfvw.overlay.OverlayManager()
        self.grid = pyfvw.overlay.GridOverlay()
        self.cross = Crosshair()
        self.mgr.add(self.grid)
        self.mgr.add(self.cross)
        self.elev_text = ""

    # --- rendering ---------------------------------------------------------

    def render_to_numpy(self):
        self.engine.set_center(self.center)
        self.engine.set_scale(self.scale)
        t0 = time.time()
        self.canvas.clear((24, 24, 24))
        frames = self.engine.render(self.canvas)
        self.mgr.draw_all(self.engine.proj, self.canvas)
        ms = (time.time() - t0) * 1000
        return np.asarray(self.canvas.buffer), frames, ms

    # --- tk shell ----------------------------------------------------------

    def run(self):
        self.tk = tk.Tk()
        self.tk.title("pyfvw MapEngine viewer")
        self.label = tk.Label(self.tk)
        self.label.pack()
        self.status = tk.Label(self.tk, anchor="w", font=("Menlo", 11))
        self.status.pack(fill="x")
        for key, dx, dy in (("<Left>", -1, 0), ("<Right>", 1, 0),
                            ("<Up>", 0, -1), ("<Down>", 0, 1)):
            self.tk.bind(key, lambda e, dx=dx, dy=dy: self.pan(dx, dy))
        self.tk.bind("+", lambda e: self.zoom(0.5))
        self.tk.bind("=", lambda e: self.zoom(0.5))
        self.tk.bind("-", lambda e: self.zoom(2.0))
        self.tk.bind("g", lambda e: self.toggle_grid())
        self.tk.bind("q", lambda e: self.tk.destroy())
        self.tk.bind("<Escape>", lambda e: self.tk.destroy())
        self.label.bind("<Button-1>", self.on_click)
        self.refresh()
        self.tk.mainloop()

    def refresh(self):
        arr, frames, ms = self.render_to_numpy()
        ppm = b"P6 %d %d 255\n" % (W, H) + arr[:, :, :3].tobytes()
        self.photo = tk.PhotoImage(data=ppm)  # keep a reference!
        self.label.configure(image=self.photo)
        self.status.configure(
            text=f" {self.center.lat:+.5f} {self.center.lon:+.5f}   "
                 f"1:{self.scale:,.0f}   {frames} frame(s)   {ms:.0f} ms"
                 f"{self.elev_text}   [arrows pan, +/- zoom, g grid, q quit]")

    def pan(self, dx, dy):
        proj = self.engine.proj
        self.center = pyfvw.geo.GeoPoint(
            self.center.lat - dy * PAN_PX * proj.deg_per_pixel_lat,
            self.center.lon + dx * PAN_PX * proj.deg_per_pixel_lon)
        self.center.normalize()
        self.refresh()

    def zoom(self, factor):
        self.scale = min(max(self.scale * factor, MIN_SCALE), MAX_SCALE)
        self.refresh()

    def toggle_grid(self):
        self.grid.visible = not self.grid.visible
        self.refresh()

    def on_click(self, e):
        # overlays get first refusal (top-down); unhandled clicks re-center
        if self.mgr.route_mouse_down(pyfvw.overlay.MouseEvent(e.x, e.y)):
            self.refresh()
            return
        p = self.engine.proj.surface_to_geo(e.x, e.y)
        self.cross.click = None
        self.center = p
        try:
            elev = self.engine.get_elevation(p.lat, p.lon)
            self.elev_text = f"   elev {elev:.0f} m"
        except pyfvw.FvError:
            self.elev_text = ""
        self.refresh()


def _parse_at(args):
    if "--at" in args:
        i = args.index("--at")
        center = pyfvw.geo.parse_location(args[i + 1])
        return center, args[:i] + args[i + 2:]
    return None, args


def _parse_shot(args):
    if "--shot" in args:
        i = args.index("--shot")
        return args[i + 1], args[:i] + args[i + 2:]
    return None, args


if __name__ == "__main__":
    args = sys.argv[1:]
    center, args = _parse_at(args)
    shot, args = _parse_shot(args)

    if args and args[0] == "cadrg":
        root = args[1] if len(args) > 1 else os.path.join(TESTDATA, "rpf", "clfc", "2")
        if center is None:
            center = pyfvw.geo.parse_location("33.7488 -84.3882")  # Atlanta
        viewer = PanViewer(root, "cadrg", center=center)
    elif args and args[0] == "tiros":
        root = args[1] if len(args) > 1 else os.path.join(TESTDATA, "tiros3", "topobath", "500m")
        viewer = PanViewer(root, "tiros", center=center)
    else:
        viewer = PanViewer(os.path.join(TESTDATA, "geotiff"), "geotiff", center=center)

    if shot:
        arr, frames, ms = viewer.render_to_numpy()
        import struct, zlib
        h, w = arr.shape[:2]
        raw = b"".join(b"\x00" + arr[y, :, :3].tobytes() for y in range(h))
        def chunk(t, d):
            c = struct.pack(">I", len(d)) + t + d
            return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
        png = (b"\x89PNG\r\n\x1a\n"
               + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
               + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))
        open(shot, "wb").write(png)
        print(f"wrote {shot}: {frames} frame(s), {ms:.0f} ms")
    else:
        viewer.run()
