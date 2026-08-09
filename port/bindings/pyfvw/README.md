# pyfvw — a short guide to the Python interface

`pyfvw` is the Python binding over **FvKit**, the portable core of the FalconView
port. It gives you the map stack without COM, MFC, or a Windows display: catalog
a directory of map data, render a base map into a buffer, and draw your own
geospatial features on top — from a script, a notebook, or a service.

Everything below was run against `TestData/` before it was written down.

---

## 1. Getting the module

The binding is built as part of the normal port build (it is skipped gracefully
if no Python dev environment is present):

```bash
cmake -B build && cmake --build build -j
```

That produces `build/port/bindings/pyfvw/pyfvw.cpython-*.so`. There is no
`pip install` step — put its directory on `sys.path`:

```python
import os, sys
REPO = "/path/to/FVW"
sys.path.insert(0, os.path.join(REPO, "build", "port", "bindings", "pyfvw"))
os.environ.setdefault("MSPCCS_DATA",
                      os.path.join(REPO, "fvw_core", "PdfLib", "sdk", "lib"))

import numpy as np
import pyfvw
```

Two environment variables matter. **`MSPCCS_DATA`** points at the GEOTRANS
datum tables and is required for anything that shifts datums — GeoTIFF headers
and `geo.parse_location`. **`FVW_GEODATA_DIR`** is only needed for magnetic
variation. `numpy` is optional but is how you get at pixels.

### Conventions worth knowing before the first call

| | |
|---|---|
| **Latitude before longitude**, always, in WGS-84 decimal degrees | `GeoPoint(33.74, -84.38)` |
| Longitude is canonical `(-180, +180]`; ±180 → +180 | `geo.normalize_lon(-180.0) == 180.0` |
| `ll.lon > ur.lon` means the rect **crosses the antimeridian** | `rect.crosses_antimeridian` |
| Errors are **exceptions**, never return codes — `pyfvw.FvError` carries `.code` and `.message` | `except pyfvw.FvError as e: e.code == pyfvw.OUT_OF_COVERAGE` |
| Out-parameters became return values; parameterless getters became properties | `src.info.width`, not `src.GetInfo(&info)` |
| Pixels are interleaved **RGBA8**, top-down, zero-copy to numpy as `(h, w, 4)` | `np.asarray(canvas.buffer)` |
| Decode and I/O release the GIL | threads actually overlap |

---

## 2. The map catalog

The catalog is the port's replacement for the Windows Map Data Manager: a
SQLite database with an R-tree over frame coverage. You point it at directories,
it identifies what is there through the format registry, and afterwards it
answers "what covers this viewport, at this scale?" in well under a millisecond.

```python
pyfvw.catalog.register_builtin_formats()      # idempotent; call before scan
print(pyfvw.catalog.registered_format_keys())
# ['cadrg', 'dted', 'dted-shaded', 'enc', 'geotiff', 'gpkg', 'tiros', 'vpf']

cat = pyfvw.catalog.Catalog("maps.sqlite")    # omit the path for ":memory:"

ds = cat.add_data_source("TestData/rpf", "cadrg")
print(cat.scan(ds), "frames")                 # 524 frames

ds2 = cat.add_data_source("TestData/geotiff", "geotiff")
print(cat.scan(ds2), "frames")                # 31 frames
```

Scanning groups frames into **series** — one row per (format, series key), with
the scale normalized to a 1:N denominator:

```python
for s in sorted(cat.series(), key=lambda s: s.scale_denom):
    print(f"id={s.id:<3} {s.format:<8} {s.series_key:<8} 1:{int(s.scale_denom):,}")

# id=5   geotiff  Color    1:2,014
# id=6   geotiff  B&W      1:6,714
# id=7   geotiff  GeoTIFF  1:30,000
# id=4   cadrg    TLM      1:50,000
# id=3   cadrg    LFC      1:500,000
# id=2   cadrg    JNC      1:2,000,000
# id=1   cadrg    GNC      1:5,000,000
```

Two queries do most of the work — *what covers this box* and *which series suits
this scale*:

```python
atlanta = pyfvw.geo.GeoRect(ll=pyfvw.geo.GeoPoint(33.6, -84.5),
                            ur=pyfvw.geo.GeoPoint(33.9, -84.2))

for r in cat.select_by_geo_rect(atlanta):      # optional series_id= filter
    print(r.series_key, os.path.basename(r.path), r.bounds)

best = cat.best_series_for_scale(500_000)      # -> SeriesRow(cadrg:LFC, 1:500000)
```

The R-tree is antimeridian-aware: crossing coverage is stored as two boxes and
queries are split and de-duplicated, so a viewport over the dateline needs no
special-casing from you. `remove_data_source(id)` drops a source and its rows.

---

## 3. Rendering a base map

`MapEngine` is the compositing loop that a viewer would otherwise write by hand:
catalog query → per-frame raster source (through an LRU cache) → resample →
canvas. You configure a viewport and call `render`.

```python
center = pyfvw.geo.parse_location("33 44 55.7 N 84 23 17.5 W")
# also accepts "33.7488 -84.3882" and MGRS: "16SGB 47342 34212"

eng = pyfvw.engine.MapEngine(cat)
eng.set_surface(800, 600)
eng.set_center(center)
eng.set_scale(500_000)                         # 1:500,000

canvas = pyfvw.canvas.CpuCanvas(800, 600)
canvas.clear((255, 255, 255))
drawn = eng.render(canvas, best.id)            # series_id=0 renders all series
print(f"composited {drawn} frames")            # composited 2 frames

arr = np.asarray(canvas.buffer)                # zero-copy (600, 800, 4) uint8
```

`arr` is a live view of the canvas, so it is ready for Pillow, OpenCV, an
`imshow`, a Qt/tk widget, or a hand-rolled PNG writer (`PythonView.py`'s
`_save_png` is a dependency-free one, in about fifteen lines).

The engine's projection is exposed for coordinate work — this is what turns a
mouse click into a position:

```python
print(eng.proj.bounds)              # GeoRect(ll=(33.546, -84.956), ur=(33.950, -83.819))
print(eng.proj.deg_per_pixel_lat, eng.proj.deg_per_pixel_lon)

p = eng.proj.surface_to_geo(400, 300)          # pixel -> GeoPoint
sx, sy = eng.proj.geo_to_surface(p)            # and back
```

Attach an elevation source and the engine answers terrain queries at the same
coordinates — meters, `NaN` for a DTED void post, `FvError(OUT_OF_COVERAGE)`
outside the data:

```python
eng.set_elevation_source(pyfvw.formats.DtedElevationSource("TestData/dted"))
print(eng.get_elevation(center.lat, center.lon), "m")     # 338.0 m
```

**Scale vs. physical scale.** `set_scale(N)` is the FalconView-compatible
equal-arc path. When you want a series drawn at its *native* size on a known
display pitch — a 1:500k chart actually at 1:500k, imagery at 100% — use
`set_physical_scale`, which also gives the projection physically correct
latitude-dependent aspect:

```python
lfc = next(s for s in cat.series() if s.series_key == "LFC")
eng.set_physical_scale(lfc.scale, lfc.scale_units,
                       pyfvw.engine.NATIVE_DISPLAY_MM_PER_PIXEL)  # 0.25 mm/px
```

`mm_per_pixel` is then your zoom knob: larger means zoomed out.

To pre-render a region for offline use, `pyfvw.store.TilePackWriter` drives the
same engine into an OGC GeoPackage pyramid, which the catalog reads back as the
`gpkg` format — pyramid levels become series.

---

## 4. Overlays that draw geospatial features

An overlay is your own drawing on top of the map. Subclass
`pyfvw.overlay.Overlay`, override the handlers you care about, and let the
`OverlayManager` own the stack. `on_draw` receives the **projection** and the
**canvas**: converting geography to pixels with `proj.geo_to_surface` is the
whole job.

```python
class RouteOverlay(pyfvw.overlay.Overlay):
    """A named route: leg lines, waypoint diamonds, labels."""

    def __init__(self, name, waypoints, color=(220, 30, 30)):
        super().__init__(name)
        self.waypoints = waypoints          # [(label, lat, lon), ...]
        self.color = color
        self.selected = None
        self._drawn = []                    # where the last on_draw put them

    def on_draw(self, proj, canvas):
        self._drawn = []
        for label, lat, lon in self.waypoints:
            try:
                x, y = proj.geo_to_surface(pyfvw.geo.GeoPoint(lat, lon))
            except pyfvw.FvError:
                continue                    # not on this projection; skip it
            self._drawn.append((label, int(round(x)), int(round(y))))

        if len(self._drawn) >= 2:
            canvas.draw_lines([(x, y) for _, x, y in self._drawn],
                              color=self.color, width=2)

        for label, x, y in self._drawn:
            fill = (255, 220, 0) if label == self.selected else self.color
            canvas.fill_polygon([[(x, y - 7), (x + 7, y), (x, y + 7), (x - 7, y)]],
                                fill=fill, outline=(0, 0, 0), outline_width=1)
            canvas.draw_text(label, x + 10, y - 10, color=(0, 0, 0), size=12.0)

    def on_mouse_down(self, e):
        # Hit-test what was DRAWN, so a click agrees with the screen.
        for label, x, y in self._drawn:
            if abs(e.x - x) <= 8 and abs(e.y - y) <= 8:
                self.selected = label
                return True                 # handled -> stops routing
        return False
```

Stack it under the built-in graticule and draw. Overlays draw **bottom-up** and
receive events **top-down until one returns `True`**:

```python
route = RouteOverlay("KATL departure", [
    ("KATL",  33.6407, -84.4277),
    ("VULCN", 33.75,   -84.30),
    ("ROME",  34.35,   -85.16),
])

mgr = pyfvw.overlay.OverlayManager()
mgr.add(pyfvw.overlay.GridOverlay())        # lat/lon graticule; .set_color(...)
mgr.add(route)

canvas.set_default_font("/System/Library/Fonts/Supplemental/Arial.ttf")
canvas.clear((255, 255, 255))
eng.render(canvas, best.id)                 # base map first
mgr.draw_all(eng.proj, canvas)              # then overlays, bottom-up

sx, sy = eng.proj.geo_to_surface(pyfvw.geo.GeoPoint(33.75, -84.30))
mgr.route_mouse_down(pyfvw.overlay.MouseEvent(int(sx), int(sy)))
print(route.selected)                       # VULCN
```

Notes that save time:

- **Keep a reference or don't** — `add()` keeps the Python half of a subclassed
  overlay alive for the manager's lifetime, so `mgr.add(MyOverlay())` retains
  its overrides even with no other reference.
- **`overlay.visible = False`** skips both drawing and event routing.
- **Exceptions are contained.** An exception in `on_draw` surfaces as
  `FvError` from `draw_all`, naming the overlay
  (`overlay 'broken': python overlay: RuntimeError: boom`); an exception in an
  event handler is logged and treated as unhandled. Nothing raw crosses the SPI.
- `set_default_font` takes a TTF path — text is stb_truetype and ASCII-only for
  now; without a font, `draw_text` has nothing to draw with.
- The other canvas primitives are `clear`, `draw_lines` (with `width=` and a
  `dash=` pattern), `fill_polygon` (even-odd over multiple rings), `draw_text`,
  and `draw_pixmap` for alpha-blending a `PixelBuffer`.

### Features from a real product

When the features are a *map product* rather than your own data, don't hand-roll
it — the vector seam reads, styles, and draws DNC and S-57 ENC through the same
three classes:

```python
src = pyfvw.vector.VpfVectorSource()
src.open("TestData/vpf/dnc17/h1707300")       # a DNC library directory
style = pyfvw.vector.GeoSymStyleEngine()
style.open("TestData", pyfvw.vector.GEOSYM_DNC)

proj = pyfvw.engine.MapProjection()           # standalone, no catalog needed
proj.set_surface_size(512, 512)
proj.set_center(pyfvw.geo.GeoPoint(41.70, -69.90))
proj.set_physical_scale(300_000, 0.25)

cv = pyfvw.canvas.CpuCanvas(512, 512); cv.clear((255, 255, 255))
vr = pyfvw.vector.VectorRenderer(src, style)
vr.render(proj, cv)                           # does NOT clear the canvas
print(vr.features_queried, vr.draws_emitted, vr.query_ms, vr.draw_ms)

for hit in vr.pick_index.hit_test(256, 256, 6.0):   # topmost first
    d = src.describe(hit.ref)
    print(d.title, d.layer_name, [(a.code, a.display) for a in d.attributes[:3]])
    # Depth Curve hydline [('f_code', 'Depth Curve'), ('acc', 'Accurate'), ...]
```

Swap `EncVectorSource` + `S52StyleEngine` for the same call sequence on ENC
cells — including the mariner settings that change *what* is drawn
(`style.mariner().safety_contour = 10.0`), `set_color_scheme(S52_NIGHT)`, and
`src.staleness_warning` when cells have unapplied updates. Every engine exposes
`rules()` (a small text rule language for show/hide/priority) and
`viewing_groups()` for decluttering.

OSM vector tiles are the third product, and the same three classes again — with
one extra step, because a tile pyramid is the one product where the SCALE picks
which data is read:

```python
src = pyfvw.vector.OsmVectorSource()
src.open("TestData/OSM/mbtiles/us-south.mbtiles")
style = pyfvw.vector.OsmStyleEngine()
style.load_file("port/Osm/styles/peregrine-osm.json")   # a MapLibre GL style

# The zoom<->scale relation is latitude-dependent and BOTH halves derive it.
# Give the style engine the viewport's centre latitude and the same pixel
# pitch the source has, or its minzoom will switch layers on at a different
# scale than the tile level the source read.
style.set_reference_latitude(33.755)
style.set_display_mm_per_pixel(src.display_mm_per_pixel)

cv.clear(style.background(25_000)[:3])   # a GL `background` is not a feature
```

`src.last_query_zoom` says which level was read and `src.last_query_overzoom`
how far past the pyramid the display has gone (z14 geometry under z15+ rules —
intended, and how a slippy map keeps growing after it runs out of levels).
A style sheet outside the supported subset raises rather than loading half of
itself; `style.ignored_icons` lists what a sprite sheet would have drawn.

The pick index is built from what the renderer actually inked, and is valid only
for the **last** `render()`.

---

## 5. Where to find more

The bindings are documented in the module itself; nothing here is the only copy.

- **`help(pyfvw)`, `help(pyfvw.vector.VectorRenderer)`, `dir(pyfvw)`** — every
  bound class and method carries its docstring, and pybind11 prints real
  signatures with argument names and defaults. This is the fastest and most
  current reference; start here.
- **[`ICD-MAPPING.md`](ICD-MAPPING.md)** (this directory) — one row per bound
  surface mapping the legacy COM ICD (`PFPS400-MapServer-ICD.doc`) to pyfvw,
  with the **semantic deltas** called out: feet → meters, HRESULT → `FvError`,
  cursor enumeration → whole lists, and so on. Read this if you are porting
  code written against the Windows interfaces.
- **[`test/test_pyfvw.py`](test/test_pyfvw.py)** — the executable spec. Every
  bound surface has a test with pinned values, and the tests are written to be
  read as examples. Run them with
  `ctest --test-dir build -R pyfvw_pytest`.
- **[`../../apps/PythonView.py`](../../apps/PythonView.py)** — a complete
  1,600-line desktop viewer over this API: catalog management, every data
  family, pan/zoom, overlays, click-to-identify, headless `--shot` rendering.
  The best source of realistic usage.
- **[`pyfvw_module.cpp`](pyfvw_module.cpp)** — where the docstrings live. If a
  method's behavior is unclear, its binding is a few lines long and says which
  C++ call it forwards to.
- **`port/include/fvkit/*.h`** — the C++ headers behind the bindings
  (`geo.h`, `raster.h`, `proj.h`, `engine.h`, `canvas/`, `catalog/`, `overlay/`,
  `vector/`, `store/`). Binding names are the snake_case of these, so a header
  comment answers most questions the docstring doesn't.
- **`port/fvkit-contracts.md`** — the binding conventions themselves (D3 error
  handling, D5 naming), i.e. *why* the API looks like this.
- **`port/PORTING.md`** — the port ledger. Status **`P`** on a row means that
  module is exposed in Python; the row says what landed and what did not.
- **CLI equivalents** — `build/port/fvkit/fvrender` (render a map to PNG from
  the command line) and `fvpack` (build a GeoPackage tile pyramid) are the same
  paths without Python, useful for checking whether an issue is in your script.
- **`pyfvw.Settings`** — the port's registry replacement, a hand-edited INI
  (`port/peregrine.ini.sample`); `pyfvw.default_settings_paths()` says where it
  is looked for.
